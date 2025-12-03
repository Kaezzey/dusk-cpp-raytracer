#include "../../include/core/dusktracer.h"
#include "../../include/core/renderer.h"
#include "../../include/core/interval.h"

#include <cmath>
#include <atomic>
#include <chrono>
#include <algorithm>

// 1) Simple overload: forwards to the full version with progress = nullptr
render_result renderer::render(
    const hittable& world,
    camera&         cam,
    std::atomic<bool>* cancel_flag
) const
{
    return render(world, cam, cancel_flag, nullptr, nullptr);
}

// 2) Full version with progress + ETA
render_result renderer::render(
    const hittable&        world,
    camera&                cam,
    std::atomic<bool>*     cancel_flag,
    render_progress_state* progress,
    std::function<void(const render_result&)> progress_callback
) const
{
    using clock = std::chrono::steady_clock;

    cam.initialize();

    render_result out;
    out.width  = cam.image_width;
    out.height = cam.image_height;
    out.pixels.resize(out.width * out.height * 3);

    const int width  = out.width;
    const int height = out.height;
    const int spp    = cam.samples_per_pixel;
    const int depth  = cam.max_depth;

    const int total_scanlines = height;
    const int chunk_size      = 4; // update ETA every N lines

    if (progress) {
        progress->total_scanlines           = total_scanlines;
        progress->completed_scanlines.store(0);
        progress->eta_seconds.store(0.0);
        progress->elapsed_seconds.store(0.0);
    }

    int    accumulated_lines = 0;
    double accumulated_time  = 0.0;
    double eta_longterm      = 0.0;
    double eta_shortterm     = 0.0;

    auto render_start = clock::now();
    auto chunk_start  = render_start;

    static const interval intensity(0.000, 0.999);

    #pragma omp parallel for schedule(dynamic)
    for (int j = 0; j < height; ++j)
    {
        if (cancel_flag && cancel_flag->load()) {
            continue;
        }

        for (int i = 0; i < width; ++i) {
            colour pixel_col(0,0,0);

            for (int s = 0; s < spp; ++s) {
                ray r = cam.get_ray(i, j, s);
                // Get one sample's contribution
                colour samp = cam.ray_colour(r, depth, world);

                // Per-sample sanitization: guard against NaN/Inf and negatives
                auto samp_sanitize = [](double v) -> double {
                    if (!std::isfinite(v) || v <= 0.0) return 0.0;
                    const double MAX_SAMPLE = 1e6; // cap per-component sample value
                    if (v > MAX_SAMPLE) v = MAX_SAMPLE;
                    return v;
                };

                double sr = samp_sanitize(samp.x());
                double sg = samp_sanitize(samp.y());
                double sb = samp_sanitize(samp.z());

                // Luminance-based clamp to reduce fireflies (preserve color ratios)
                double lum = 0.2126*sr + 0.7152*sg + 0.0722*sb;
                const double MAX_SAMPLE_LUM = 1e4; // tweakable; protects against outliers
                if (lum > MAX_SAMPLE_LUM) {
                    double scale = MAX_SAMPLE_LUM / lum;
                    sr *= scale; sg *= scale; sb *= scale;
                }

                pixel_col += colour(sr, sg, sb);
            }

            double scale = 1.0 / spp;
            double r_lin = scale * pixel_col.x();
            double g_lin = scale * pixel_col.y();
            double b_lin = scale * pixel_col.z();

            // Sanitize components to avoid NaN/Inf and extremely large values
            auto sanitize = [](double v) -> double {
                if (!std::isfinite(v) || v <= 0.0) return 0.0;
                const double MAX_LINEAR = 1e6;
                if (v > MAX_LINEAR) v = MAX_LINEAR;
                return v;
            };

            r_lin = sanitize(r_lin);
            g_lin = sanitize(g_lin);
            b_lin = sanitize(b_lin);

            // Exposure (linear multiplier) comes from renderer state
            double exposure = this->exposure;
            r_lin *= exposure; g_lin *= exposure; b_lin *= exposure;

            // ACES RRT+ODT approximation (film-like tonemapping)
            auto aces_film = [](double x) -> double {
                // fit constants from common ACES approximation
                const double a = 2.51;
                const double b = 0.03;
                const double c = 2.43;
                const double d = 0.59;
                const double e = 0.14;
                double v = (x * (a * x + b)) / (x * (c * x + d) + e);
                if (!std::isfinite(v)) return 0.0;
                if (v < 0.0) return 0.0;
                if (v > 1.0) return 1.0;
                return v;
            };

            double r_ton = aces_film(r_lin);
            double g_ton = aces_film(g_lin);
            double b_ton = aces_film(b_lin);

            // Gamma-correct the tone-mapped (display) values
            double r_gam = linear_to_gamma(r_ton);
            double g_gam = linear_to_gamma(g_ton);
            double b_gam = linear_to_gamma(b_ton);

            int rbyte = int(256 * intensity.clamp(r_gam));
            int gbyte = int(256 * intensity.clamp(g_gam));
            int bbyte = int(256 * intensity.clamp(b_gam));

            int idx = 3 * (j * width + i);
            out.pixels[idx + 0] = static_cast<unsigned char>(rbyte);
            out.pixels[idx + 1] = static_cast<unsigned char>(gbyte);
            out.pixels[idx + 2] = static_cast<unsigned char>(bbyte);
        }

        if (progress) {
            int done = ++progress->completed_scanlines; // atomic

            if (done % chunk_size == 0) {
                #pragma omp critical
                {
                    auto   now           = clock::now();
                    double chunk_seconds =
                        std::chrono::duration<double>(now - chunk_start).count();
                    chunk_start = now;

                    double short_avg   = chunk_seconds / chunk_size;
                    eta_shortterm      = short_avg * (total_scanlines - done);

                    accumulated_time  += chunk_seconds;
                    accumulated_lines += chunk_size;
                    double long_avg    = accumulated_time /
                                         std::max(1, accumulated_lines);
                    eta_longterm       = long_avg * (total_scanlines - done);

                    double eta     = 0.75 * eta_longterm + 0.25 * eta_shortterm;
                    double elapsed =
                        std::chrono::duration<double>(now - render_start).count();

                    progress->eta_seconds.store(eta);
                    progress->elapsed_seconds.store(elapsed);

                    // If UI provided a progress callback, give it a partial image
                    if (progress_callback) {
                        // pass a copy of current output to the callback
                        render_result partial = out;
                        progress_callback(partial);
                    }
                }
            }
        }
    }

    return out;
}
