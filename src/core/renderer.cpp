#include "../../include/core/dusktracer.h"
#include "../../include/core/renderer.h"
#include "../../include/core/interval.h"

#include <cmath>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>
#ifdef HAVE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif
#include "../../include/core/image_io.h"

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

    const int w  = out.width;
    const int h = out.height;
    const int spp    = cam.samples_per_pixel;
    const int depth  = cam.max_depth;

    // HDR linear float buffer (3 floats per pixel) used for optional denoising
    std::vector<float> hdr_buffer((size_t)w * (size_t)h * 3, 0.0f);
    // Albedo (base color) and normal AOVs for OIDN guidance
    std::vector<float> albedo_buffer((size_t)w * (size_t)h * 3, 0.0f);
    std::vector<float> normal_buffer((size_t)w * (size_t)h * 3, 0.0f);

    const int total_scanlines = h;
    const int chunk_size      = 4; // update ETA every N lines
    const int progress_min_interval_ms = 500; // throttle partial updates

    // Track which scanlines are fully written to HDR buffer
    std::vector<char> row_done(h, 0);

    // Throttle progress callbacks across all OMP workers
    static std::atomic<long long> s_last_progress_ms{0};

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
    for (int j = 0; j < h; ++j)
    {
        if (cancel_flag && cancel_flag->load()) {
            continue;
        }

        for (int i = 0; i < w; ++i) {
            // We'll perform per-pixel sampling with optional adaptive stopping.
            // Welford accumulators for mean & M2 (per-channel radiance)
            double mean_r = 0.0, mean_g = 0.0, mean_b = 0.0;
            double m2_r = 0.0, m2_g = 0.0, m2_b = 0.0;
            // Welford accumulators for albedo (per-channel)
            double mean_ar = 0.0, mean_ag = 0.0, mean_ab = 0.0;
            double m2_ar = 0.0, m2_ag = 0.0, m2_ab = 0.0; // not used for now but reserved
            // Normal accumulator (vector sum)
            double normal_sum_x = 0.0, normal_sum_y = 0.0, normal_sum_z = 0.0;

            int n = 0;

            // Keep a running mean luminance for simple outlier scaling (fireflies)
            double running_mean_lum = 0.0;
            const double MAX_SAMPLE_LUM = 1e4; // absolute clamp for sample luminance
            const double OUTLIER_FACTOR = 10.0; // allow samples up to this * running_mean

            // Convenience aliases for adaptive settings
            bool adaptive = this->adaptive_sampling;
            int min_samples = this->adaptive_min_samples;
            int check_interval = this->adaptive_check_interval;
            double rel_thresh = this->adaptive_rel_threshold;
            double abs_thresh = this->adaptive_abs_threshold;

            for (int s = 0; s < spp; ++s) {
                ray r = cam.get_ray(i, j, s);
                // Get one sample's contribution and the first-hit albedo/normal
                colour samp;
                colour samp_albedo(0,0,0);
                vec3   samp_normal(0,0,0);
                samp = cam.ray_colour(r, depth, world, &samp_albedo, &samp_normal);

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

                // Sample luminance
                double lum = 0.2126*sr + 0.7152*sg + 0.0722*sb;

                // Determine adaptive threshold: max(absolute, factor * running_mean)
                double adaptive_thresh = MAX_SAMPLE_LUM;
                if (n > 0) {
                    adaptive_thresh = std::max(MAX_SAMPLE_LUM, OUTLIER_FACTOR * running_mean_lum);
                }

                // If this sample is an outlier, scale it down to threshold while preserving colour ratios
                if (lum > 0.0 && lum > adaptive_thresh) {
                    double scale_out = adaptive_thresh / lum;
                    sr *= scale_out; sg *= scale_out; sb *= scale_out;
                    lum *= scale_out;
                }

                // Update running mean luminance (simple incremental mean)
                running_mean_lum = (running_mean_lum * n + lum) / (n + 1);

                // Welford update for each channel (radiance)
                ++n;
                double delta_r = sr - mean_r;
                mean_r += delta_r / n;
                m2_r += delta_r * (sr - mean_r);

                double delta_g = sg - mean_g;
                mean_g += delta_g / n;
                m2_g += delta_g * (sg - mean_g);

                double delta_b = sb - mean_b;
                mean_b += delta_b / n;
                m2_b += delta_b * (sb - mean_b);

                // Incremental mean for albedo (per-channel)
                double ar = samp_albedo.x();
                double ag = samp_albedo.y();
                double ab = samp_albedo.z();
                double delta_ar = ar - mean_ar;
                mean_ar += delta_ar / n;
                double delta_ag = ag - mean_ag;
                mean_ag += delta_ag / n;
                double delta_ab = ab - mean_ab;
                mean_ab += delta_ab / n;

                // Accumulate normals as simple vector sum (will normalize at end)
                normal_sum_x += samp_normal.x();
                normal_sum_y += samp_normal.y();
                normal_sum_z += samp_normal.z();

                // Adaptive convergence check: after min samples and on intervals
                if (adaptive && n >= min_samples && (n % check_interval) == 0) {
                    // compute sample variances (population estimator uses / (n-1) if n>1)
                    double var_r = (n > 1) ? (m2_r / (n - 1)) : 0.0;
                    double var_g = (n > 1) ? (m2_g / (n - 1)) : 0.0;
                    double var_b = (n > 1) ? (m2_b / (n - 1)) : 0.0;

                    double std_err_r = (n > 0) ? (std::sqrt(var_r) / std::sqrt((double)n)) : 0.0;
                    double std_err_g = (n > 0) ? (std::sqrt(var_g) / std::sqrt((double)n)) : 0.0;
                    double std_err_b = (n > 0) ? (std::sqrt(var_b) / std::sqrt((double)n)) : 0.0;

                    double rel_r = (std::abs(mean_r) > 1e-12) ? (std_err_r / std::abs(mean_r)) : std_err_r;
                    double rel_g = (std::abs(mean_g) > 1e-12) ? (std_err_g / std::abs(mean_g)) : std_err_g;
                    double rel_b = (std::abs(mean_b) > 1e-12) ? (std_err_b / std::abs(mean_b)) : std_err_b;

                    double rel_max = std::max(rel_r, std::max(rel_g, rel_b));

                    // If relative standard error is below threshold, or absolute std_err small, stop
                    if (rel_max < rel_thresh ||
                        (std_err_r < abs_thresh && std_err_g < abs_thresh && std_err_b < abs_thresh))
                    {
                        break;
                    }
                }
            }

            // finalize per-pixel linear value by using the Welford mean
            double scale = 1.0 / std::max(1, n);
            double r_lin = mean_r;
            double g_lin = mean_g;
            double b_lin = mean_b;

            // finalize albedo mean
            double a_lin_r = mean_ar;
            double a_lin_g = mean_ag;
            double a_lin_b = mean_ab;

            // finalize normal (average and renormalize)
            vec3 avgN((float)(normal_sum_x * scale), (float)(normal_sum_y * scale), (float)(normal_sum_z * scale));
            if (!avgN.near_zero()) avgN = unit_vector(avgN);

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

            // Store into HDR float buffer (float32)
            int idxf = 3 * (j * w + i);
            hdr_buffer[idxf + 0] = (float)r_lin;
            hdr_buffer[idxf + 1] = (float)g_lin;
            hdr_buffer[idxf + 2] = (float)b_lin;
            // Store albedo AOV (mean albedo per pixel)
            albedo_buffer[idxf + 0] = (float)a_lin_r;
            albedo_buffer[idxf + 1] = (float)a_lin_g;
            albedo_buffer[idxf + 2] = (float)a_lin_b;

            // Store normal AOV (normalized average normal)
            normal_buffer[idxf + 0] = avgN.x();
            normal_buffer[idxf + 1] = avgN.y();
            normal_buffer[idxf + 2] = avgN.z();
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
                }
            }
        }

        // Mark this scanline as complete for safe partial display
        row_done[j] = 1;

        // Outside critical section: throttled partial image callback
        if (progress_callback) {
            auto now = clock::now();
            long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            long long last_ms = s_last_progress_ms.load();
            if (now_ms - last_ms >= progress_min_interval_ms) {
                if (s_last_progress_ms.compare_exchange_strong(last_ms, now_ms)) {
                    // Build partial 8-bit image from completed scanlines only
                    render_result partial;
                    partial.width = w;
                    partial.height = h;
                    partial.pixels.resize((size_t)w * (size_t)h * 3);

                    auto aces_film_small = [](double x) -> double {
                        const double a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
                        double v = (x * (a * x + b)) / (x * (c * x + d) + e);
                        if (!std::isfinite(v) || v < 0.0) return 0.0;
                        if (v > 1.0) return 1.0;
                        return v;
                    };

                    for (int yy = 0; yy < h; ++yy) {
                        bool done_line = (row_done[yy] != 0);
                        for (int xx = 0; xx < w; ++xx) {
                            int idx = 3 * (yy * w + xx);
                            if (done_line) {
                                int hidx = idx;
                                double r_lin = (double)hdr_buffer[hidx + 0];
                                double g_lin = (double)hdr_buffer[hidx + 1];
                                double b_lin = (double)hdr_buffer[hidx + 2];
                                double r_t = aces_film_small(r_lin);
                                double g_t = aces_film_small(g_lin);
                                double b_t = aces_film_small(b_lin);
                                double r_g = linear_to_gamma(r_t);
                                double g_g = linear_to_gamma(g_t);
                                double b_g = linear_to_gamma(b_t);
                                partial.pixels[idx + 0] = (std::uint8_t)(int(256 * intensity.clamp(r_g)));
                                partial.pixels[idx + 1] = (std::uint8_t)(int(256 * intensity.clamp(g_g)));
                                partial.pixels[idx + 2] = (std::uint8_t)(int(256 * intensity.clamp(b_g)));
                            } else {
                                partial.pixels[idx + 0] = 0;
                                partial.pixels[idx + 1] = 0;
                                partial.pixels[idx + 2] = 0;
                            }
                        }
                    }

                    try {
                        progress_callback(partial);
                    } catch (...) {
                        // swallow any callback exceptions to avoid breaking render
                    }
                }
            }
        }
    }

    // Optionally run OpenImageDenoise on the linear HDR buffer
    std::vector<float> denoised_buffer;
    denoised_buffer = hdr_buffer; // default: copy

#ifdef HAVE_OIDN
    if (this->use_denoiser) {
        try {
            // Compute basic HDR statistics before denoising
            double min_lum = std::numeric_limits<double>::infinity();
            double max_lum = 0.0;
            double sum_lum = 0.0;
            double sum_rgb = 0.0;
            const int pixels = w * h;
            for (int p = 0; p < pixels; ++p) {
                int id = 3 * p;
                double r = (double)hdr_buffer[id + 0];
                double g = (double)hdr_buffer[id + 1];
                double b = (double)hdr_buffer[id + 2];
                double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                if (lum < min_lum) min_lum = lum;
                if (lum > max_lum) max_lum = lum;
                sum_lum += lum;
                sum_rgb += std::abs(r) + std::abs(g) + std::abs(b);
            }
            double mean_lum = sum_lum / std::max(1, pixels);
            std::fprintf(stdout, "OIDN: starting denoiser (strength=%.3f) -- pre stats: min=%.6e max=%.6e mean=%.6e sumRGB=%.6e\n",
                         (double)this->denoiser_strength, min_lum, max_lum, mean_lum, sum_rgb);

            // Also append a small line to the debug stats file recording the strength used
            {
                FILE* df = std::fopen("Renders/Debug_OIDN_stats.txt", "a");
                if (df) {
                    std::fprintf(df, "OIDN_RUN: strength=%.6f pre_min=%.6e pre_max=%.6e pre_mean=%.6e\n",
                                 (double)this->denoiser_strength, min_lum, max_lum, mean_lum);
                    std::fclose(df);
                }
            }

            oidn::DeviceRef device = oidn::newDevice();
            device.commit();

            oidn::FilterRef filter = device.newFilter("RT");

            // Create device-accessible buffers and copy host HDR data into them
            size_t bufBytes = (size_t)pixels * 3 * sizeof(float);
            oidn::BufferRef colorBuf = device.newBuffer(bufBytes);
            oidn::BufferRef outBuf = device.newBuffer(bufBytes);
            // Copy HDR floats into the device buffer
            void* colorPtr = colorBuf.getData();
            if (colorPtr) {
                std::memcpy(colorPtr, hdr_buffer.data(), bufBytes);
            }

            // Create and copy AOV buffers (albedo + normal) into device buffers
            oidn::BufferRef albedoBuf = device.newBuffer(bufBytes);
            oidn::BufferRef normalBuf = device.newBuffer(bufBytes);
            void* albPtr = albedoBuf.getData();
            if (albPtr) {
                std::memcpy(albPtr, albedo_buffer.data(), bufBytes);
            }
            void* nrmPtr = normalBuf.getData();
            if (nrmPtr) {
                std::memcpy(nrmPtr, normal_buffer.data(), bufBytes);
            }

            filter.setImage("color", colorBuf, oidn::Format::Float3, w, h);
            filter.setImage("albedo", albedoBuf, oidn::Format::Float3, w, h);
            filter.setImage("normal", normalBuf, oidn::Format::Float3, w, h);
            filter.setImage("output", outBuf, oidn::Format::Float3, w, h);
            filter.set("hdr", true);
            filter.set("srgb", false);
            // strength: 0.0 = off, typical 0.2..0.5
            filter.set("strength", (float)this->denoiser_strength);
            filter.commit();
            filter.execute();

            // Copy denoised data back to host-visible vector
            void* outPtr = outBuf.getData();
            if (outPtr) {
                std::memcpy(denoised_buffer.data(), outPtr, bufBytes);
            }

            // Release device-side resources ASAP to avoid holding large device allocations
            filter = oidn::FilterRef();
            colorBuf = oidn::BufferRef();
            albedoBuf = oidn::BufferRef();
            normalBuf = oidn::BufferRef();
            outBuf = oidn::BufferRef();
            // Note: device can be reused but we destroy it to free potential backend memory
            device = oidn::DeviceRef();

            // Check for device errors reported by OIDN
            {
                const char* errorMessage = nullptr;
                if (device.getError(errorMessage) != oidn::Error::None) {
                    std::fprintf(stderr, "OIDN device error after execute: %s\n", errorMessage ? errorMessage : "(null)");
                }
            }

            // Compute HDR statistics after denoising and a simple diff metric
            double min_lum2 = std::numeric_limits<double>::infinity();
            double max_lum2 = 0.0;
            double sum_lum2 = 0.0;
            double sum_rgb2 = 0.0;
            double sum_abs_diff = 0.0;
            for (int p = 0; p < pixels; ++p) {
                int id = 3 * p;
                double r = (double)denoised_buffer[id + 0];
                double g = (double)denoised_buffer[id + 1];
                double b = (double)denoised_buffer[id + 2];
                double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                if (lum < min_lum2) min_lum2 = lum;
                if (lum > max_lum2) max_lum2 = lum;
                sum_lum2 += lum;
                sum_rgb2 += std::abs(r) + std::abs(g) + std::abs(b);
                // abs diff from original
                double r0 = (double)hdr_buffer[id + 0];
                double g0 = (double)hdr_buffer[id + 1];
                double b0 = (double)hdr_buffer[id + 2];
                sum_abs_diff += std::abs(r - r0) + std::abs(g - g0) + std::abs(b - b0);
            }
            double mean_lum2 = sum_lum2 / std::max(1, pixels);
            std::fprintf(stdout, "OIDN: denoiser finished -- post stats: min=%.6e max=%.6e mean=%.6e sumRGB=%.6e absDiffSum=%.6e\n",
                         min_lum2, max_lum2, mean_lum2, sum_rgb2, sum_abs_diff);

            if (sum_abs_diff == 0.0) {
                std::fprintf(stdout, "OIDN: WARNING: denoised buffer identical to input (absDiffSum == 0).\n");
            }

            // Write tonemapped debug PPMs for visual comparison
            try {
                render_result dbg_orig;
                render_result dbg_den;
                dbg_orig.width = w; dbg_orig.height = h; dbg_orig.pixels.resize(w * h * 3);
                dbg_den.width = w; dbg_den.height = h; dbg_den.pixels.resize(w * h * 3);

                for (int p = 0; p < pixels; ++p) {
                    int id = 3 * p;
                    auto tonemap_and_pack = [&](const float* src, std::uint8_t* dst) {
                        double r_lin = (double)src[id + 0];
                        double g_lin = (double)src[id + 1];
                        double b_lin = (double)src[id + 2];
                        auto aces = [&](double x) {
                            const double a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
                            double v = (x * (a * x + b)) / (x * (c * x + d) + e);
                            if (!std::isfinite(v) || v < 0.0) return 0.0;
                            if (v > 1.0) return 1.0;
                            return v;
                        };
                        double r_t = aces(r_lin);
                        double g_t = aces(g_lin);
                        double b_t = aces(b_lin);
                        double r_g = linear_to_gamma(r_t);
                        double g_g = linear_to_gamma(g_t);
                        double b_g = linear_to_gamma(b_t);
                        auto clamp01 = [&](double v){ if (!std::isfinite(v) || v < 0.0) return 0.0; if (v>0.999) return 0.999; return v; };
                        dst[id + 0] = (std::uint8_t)(int(256 * clamp01(r_g)));
                        dst[id + 1] = (std::uint8_t)(int(256 * clamp01(g_g)));
                        dst[id + 2] = (std::uint8_t)(int(256 * clamp01(b_g)));
                    };

                    tonemap_and_pack(hdr_buffer.data(), dbg_orig.pixels.data());
                    tonemap_and_pack(denoised_buffer.data(), dbg_den.pixels.data());
                }

            } catch (...) {
                // don't let debug IO break the render
            }

            // Also append stats to a small debug file so user can inspect them without a console
            {
                FILE* f = std::fopen("Renders/Debug_OIDN_stats.txt", "a");
                if (f) {
                    std::fprintf(f, "pre:  min=%.6e max=%.6e mean=%.6e sumRGB=%.6e\n",
                                 min_lum, max_lum, mean_lum, sum_rgb);
                    std::fprintf(f, "post: min=%.6e max=%.6e mean=%.6e sumRGB=%.6e absDiffSum=%.6e\n",
                                 min_lum2, max_lum2, mean_lum2, sum_rgb2, sum_abs_diff);
                    if (sum_abs_diff == 0.0) {
                        std::fprintf(f, "WARNING: Denoised buffer identical to input (absDiffSum == 0).\n");
                    }
                    std::fprintf(f, "\n");
                    std::fclose(f);
                }
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "OIDN exception: %s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "Unknown exception while running OIDN\n");
        }
    }
#else
    (void)denoised_buffer; // silence unused warning when OIDN disabled
#endif

    // Final tonemap/gamma pass: convert denoised linear floats -> 8-bit
    auto aces_film = [](double x) -> double {
        const double a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
        double v = (x * (a * x + b)) / (x * (c * x + d) + e);
        if (!std::isfinite(v) || v < 0.0) return 0.0;
        if (v > 1.0) return 1.0;
        return v;
    };

    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            int idx = 3 * (j * w + i);
            double r_lin = (double)denoised_buffer[idx + 0];
            double g_lin = (double)denoised_buffer[idx + 1];
            double b_lin = (double)denoised_buffer[idx + 2];

            double r_t = aces_film(r_lin);
            double g_t = aces_film(g_lin);
            double b_t = aces_film(b_lin);

            double r_g = linear_to_gamma(r_t);
            double g_g = linear_to_gamma(g_t);
            double b_g = linear_to_gamma(b_t);

            int rbyte = int(256 * intensity.clamp(r_g));
            int gbyte = int(256 * intensity.clamp(g_g));
            int bbyte = int(256 * intensity.clamp(b_g));

            out.pixels[idx + 0] = static_cast<unsigned char>(rbyte);
            out.pixels[idx + 1] = static_cast<unsigned char>(gbyte);
            out.pixels[idx + 2] = static_cast<unsigned char>(bbyte);
        }
    }

    // Free large temporary HDR buffers before returning to reduce peak resident memory
    {
        std::vector<float>().swap(hdr_buffer);
        std::vector<float>().swap(denoised_buffer);
    }

    return out;
}
