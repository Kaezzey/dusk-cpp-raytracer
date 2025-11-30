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
                pixel_col += cam.ray_colour(r, depth, world);
            }

            double scale = 1.0 / spp;
            double r_lin = scale * pixel_col.x();
            double g_lin = scale * pixel_col.y();
            double b_lin = scale * pixel_col.z();

            double r_gam = linear_to_gamma(r_lin);
            double g_gam = linear_to_gamma(g_lin);
            double b_gam = linear_to_gamma(b_lin);

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
