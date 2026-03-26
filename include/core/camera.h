#ifndef CAMERA_H
#define CAMERA_H

#include "hittable.h"
#include "materials/material.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include "caustics.h"
#include "mnee.h"
#include <vector>
#include <atomic>
#include <limits>

class renderer;

class camera {

  public:
    friend class renderer;

    //ratio of image width over height
    double aspect_ratio = 1.0; 
    
    //rendered image width in pixel count
    int image_width  = 100;
    int samples_per_pixel = 10;
    int max_depth = 10; //max recursion depth for ray tracing

    colour background = colour(0.0, 0.0, 0.0); //default sky color

    //vertical field of view in degrees
    double vfov = 90.0; 

    //camera position and orientation
    point3 lookfrom = point3(0,0,0);

    //point the camera is looking at
    point3 lookat   = point3(0,0,-1);

    //"up" direction of the camera
    vec3   vup      = vec3(0,1,0);

    //lens parameters for depth of field
    double defocus_angle = 0;  
    double focus_dist = 10;   
    
    int image_height;

    // sampling method (choose HALTON for high-quality, efficient AA)
    enum sampling_method_e { RANDOM_SAMPLES = 0, HALTON_SAMPLES = 1 };
    sampling_method_e sampling_method = HALTON_SAMPLES;

    // Camera-side directional sun fields (can be mirrored from scene directional light)
    bool   use_sun = false;
    vec3   sun_dir = vec3(0,0,0);
    colour sun_radiance = colour(0,0,0);
    double sun_angular_radius = 0.0;
    int    sun_shadow_samples = 8; // number of shadow samples for soft sun (0 = off)

    // Point lights copied from the editor scene. Simple point lights with
    // position, radiance, and optional range (range<=0 => infinite).
    struct point_light {
        point3 position = point3(0,0,0);
        colour  radiance = colour(1,1,1);
        double  range = 0.0; // if >0, max influence distance
    };

    std::vector<point_light> point_lights;

    // Emissive area lights (surfaces with emissive materials)
    struct emissive_surface {
        point3 position;     // sample point (center for spheres/quads)
        vec3   normal;       // surface normal
        double area;         // surface area
        colour emission;     // emitted radiance
        // For advanced sampling: could add triangle vertices, quad corners, etc.
    };
    std::vector<emissive_surface> emissive_surfaces;
    std::vector<double> emissive_cdf; // Importance sampling CDF (by power = emission*area)

    // MNEE single-sphere caustics toggle and parameters (populated by editor)
    bool   enable_mnee = true;
    bool   mnee_has_sphere = false;
    point3 mnee_sphere_center = point3(0,0,0);
    double mnee_sphere_radius = 0.0;
    double mnee_sphere_ior    = 1.5;
    // MNEE solver tuning
    int    mnee_per_thread_budget = 16;  // Low default for point lights (1024 for sun caustics)
    int    mnee_newton_max_iters  = 8;
    double mnee_newton_tol        = 1e-5;
    double mnee_step_eps          = 1e-3;
    // Sun disc sampling and gain control for broader/brighter caustics
    int    mnee_sun_samples       = 4;
    double mnee_gain_scale        = 1.0;

    // NEE + MIS for diffuse materials (reduces noise at low SPP)
    int    direct_light_samples   = 0;  // samples per diffuse hit (0 = disabled)
    bool   enable_mis             = true; // Multiple Importance Sampling

    void render(const hittable& world, std::ostream& out = std::cout){
        
        initialize();

        out << "P3\n" << image_width << ' '
            << image_height << "\n255\n";

        using clock = std::chrono::steady_clock;
        auto render_start = clock::now();

        // ---------- FRAMEBUFFER ----------
        std::vector<colour> framebuffer(image_width * image_height);

        // ---------- PROGRESS + ETA ----------
        std::clog << "\x1b[?25l" << std::flush; //hide cursor
        int total_scanlines = image_height;
        std::atomic<int> completed_scanlines{0};

        const int bar_width = 40;

        // Rolling ETA (shared, updated in critical section)
        const int chunk_size = 4;
        int accumulated_lines = 0;
        double accumulated_time = 0.0;
        double eta_longterm = 0.0;
        double eta_shortterm = 0.0;

        auto chunk_start = clock::now();

        // ---------- PARALLEL RENDER ----------
        #pragma omp parallel for schedule(dynamic)
        for (int j = 0; j < image_height; j++)
        {
            for (int i = 0; i < image_width; i++)
            {
                colour pixel_colour(0,0,0);

                for (int s = 0; s < samples_per_pixel; s++) {
                    ray r = get_ray(i, j, s);
                    pixel_colour += ray_colour(r, max_depth, world);
                }

                framebuffer[j * image_width + i] =
                    pixel_samples_scale * pixel_colour;
            }

            // ONE scanline finished
            int done = ++completed_scanlines;

            // Progress + ETA update (guarded with critical)
            if (done % chunk_size == 0) {
                #pragma omp critical
                {
                    auto now = clock::now();
                    double chunk_seconds =
                        std::chrono::duration<double>(now - chunk_start).count();

                    chunk_start = now;

                    // short-term ETA
                    double short_avg = chunk_seconds / chunk_size;
                    eta_shortterm = short_avg * (total_scanlines - done);

                    // long-term ETA
                    accumulated_time += chunk_seconds;
                    accumulated_lines += chunk_size;

                    double long_avg = accumulated_time / accumulated_lines;
                    eta_longterm = long_avg * (total_scanlines - done);

                    // blended ETA
                    double eta = 0.75 * eta_longterm + 0.25 * eta_shortterm;

                    double elapsed =
                        std::chrono::duration<double>(now - render_start).count();

                    double pct = double(done) / total_scanlines;

                    // ---------- PROGRESS BAR ----------
                    std::clog << "\r[";

                    int pos = int(bar_width * pct);
                    for (int k = 0; k < bar_width; k++) {
                        if (k < pos) std::clog << "#";
                        else std::clog << "-";
                    }

                    int rem = int(eta);
                    int rem_min = rem / 60;
                    int rem_sec = rem % 60;

                    std::clog << "] "
                            << int(pct * 100.0) << "% "
                            << "| Elapsed: " << int(elapsed) << "s "
                            << "| Remaining: " << rem_min << ":"
                            << (rem_sec < 10 ? "0" : "") << rem_sec
                            << std::flush;
                }
            }
        }

        // ---------- OUTPUT THE FRAMEBUFFER ----------
        for (int j = 0; j < image_height; j++) {
            for (int i = 0; i < image_width; i++) {
                write_colour(out, framebuffer[j * image_width + i]);
            }
        }

        // ---------- FINAL TIME ----------
        double total_time =
            std::chrono::duration<double>(clock::now() - render_start).count();

        int tm = int(total_time) / 60;
        int ts = int(total_time) % 60;

        std::clog << "\nDone. Total time: "
                << tm << ":" << (ts < 10 ? "0" : "") << ts << "\n";

        std::clog << "\x1b[?25h" << std::flush; //show cursor again
    }

  private:
    
    
    //camera parameters
    point3 center;
    double pixel_samples_scale;         
    point3 pixel00_loc;    
    vec3 pixel_delta_u;  
    vec3 pixel_delta_v;  

    //camera coordinate system basis vectors
    vec3 u, v, w;
    
    vec3 defocus_disk_u;
    vec3 defocus_disk_v;

    void initialize() {
        // If the caller explicitly set an image_height (e.g. via the Editor UI),
        // honour that value. Otherwise compute height from width and aspect ratio.
        if (image_height <= 0) {
            image_height = int(image_width / aspect_ratio);
        } else {
            // Keep aspect ratio in sync with the explicitly requested resolution.
            aspect_ratio = double(image_width) / double(image_height);
        }
        image_height = (image_height < 1) ? 1 : image_height;

        pixel_samples_scale = 1.0 / samples_per_pixel;

        center = lookfrom;

        //determine viewport dimensions.
        auto theta = degrees_to_radians(vfov);
        auto h = tan(theta/2);
        auto viewport_height = 2.0 * h * focus_dist;
        auto viewport_width = viewport_height * (double(image_width)/image_height);

        //calculate the u, v, w basis vectors for the camera coordinate system.
        w = unit_vector(lookfrom - lookat);
        u = unit_vector(cross(vup, w));
        v = cross(w, u);

        //calculate the vectors spanning the viewport.
        vec3 viewport_u = viewport_width * u;
        vec3 viewport_v = viewport_height * -v;

        //calculate the horizontal and vertical delta vectors from pixel to pixel.
        pixel_delta_u = viewport_u / image_width;
        pixel_delta_v = viewport_v / image_height;

        //calculate the location of the upper left pixel.
        auto viewport_upper_left = center - (focus_dist * w) - viewport_u / 2 - viewport_v / 2;
            
        pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

        auto defocus_radius = focus_dist * tan(degrees_to_radians(defocus_angle) / 2.0);
        defocus_disk_u = defocus_radius * u;
        defocus_disk_v = defocus_radius * v;
    }

    ray get_ray(int i, int j, int sample) const {
        auto offset = sample_square(i, j, sample);
        auto pixel_sample = pixel00_loc
            + (i + offset.x()) * pixel_delta_u
            + (j + offset.y()) * pixel_delta_v;

        auto ray_origin =  (defocus_angle <= 0) ? center : defocus_disk_sample();
        auto ray_direction = pixel_sample - ray_origin;
        auto ray_time = random_double();

        return ray(ray_origin, ray_direction, ray_time);

    }

    vec3 sample_square(int i, int j, int sample) const {
        if (sampling_method == RANDOM_SAMPLES) {
            return vec3(random_double() - 0.5, random_double() - 0.5, 0);
        }

        // HALTON_SAMPLES (fast, low-discrepancy 2D sampling)
        double u = halton(sample + 1, 2); // base 2
        double v = halton(sample + 1, 3); // base 3

        // deterministic per-pixel scramble to avoid visible correlation
        double scr_x = pixel_hash_double(i, j);
        double scr_y = pixel_hash_double(j, i);

        u = u + scr_x;
        u = u - std::floor(u); // wrap into [0,1)
        v = v + scr_y;
        v = v - std::floor(v);

        return vec3(u - 0.5, v - 0.5, 0);
    }

    point3 defocus_disk_sample() const {
        double r = sqrt(random_double());
        double theta = 2.0 * pi * random_double();
        return center + r * cos(theta) * defocus_disk_u + r * sin(theta) * defocus_disk_v;
    }

    static double halton(int index, int base) {
        double result = 0.0;
        double f = 1.0;
        int i = index;
        while (i > 0) {
            f /= (double)base;
            result += f * double(i % base);
            i /= base;
        }
        return result;
    }

    public:
    // Public setter for caustics photon map
    void set_caustics(const photon_map& pm, double radius) {
        caustics = pm;
        caustics_radius = radius;
    }

    // Public setter for emissive area lights
    void set_emissive_surfaces(const std::vector<emissive_surface>& surfaces) {
        emissive_surfaces = surfaces;
        
        // Build importance sampling CDF by power (emission * area)
        emissive_cdf.clear();
        if (surfaces.empty()) return;
        
        emissive_cdf.resize(surfaces.size());
        double total_power = 0.0;
        
        for (size_t i = 0; i < surfaces.size(); ++i) {
            const auto& surf = surfaces[i];
            double lum = 0.2126 * surf.emission.x() + 0.7152 * surf.emission.y() + 0.0722 * surf.emission.z();
            double power = lum * surf.area;
            total_power += power;
            emissive_cdf[i] = total_power;
        }
        
        // Normalize CDF
        if (total_power > 1e-10) {
            for (auto& val : emissive_cdf) {
                val /= total_power;
            }
        }
    }

    // simple integer hash -> double in [0,1)
    static double pixel_hash_double(int a, int b) {
        unsigned int n = (unsigned int)(a * 73856093u ^ b * 19349663u);
        n = (n << 13) ^ n;
        unsigned int nn = (n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu;
        return double(nn) / double(0x7fffffffu);
    }

    static double colour_luminance(const colour& c) {
        return 0.2126 * c.x() + 0.7152 * c.y() + 0.0722 * c.z();
    }

    double point_light_sampling_weight(const point_light& pl, const point3& p, const vec3& normal) const {
        vec3 to_light = pl.position - p;
        double dist = to_light.length();
        if (dist <= 1e-6) return 0.0;
        if (pl.range > 0.0 && dist > pl.range) return 0.0;

        vec3 Ldir = unit_vector(to_light);
        double NdotL = dot(normal, Ldir);
        if (NdotL <= 0.0) return 0.0;

        double att = 1.0 / std::max(1e-4, dist * dist);
        return att * NdotL * colour_luminance(pl.radiance);
    }

    double total_point_light_sampling_weight(const point3& p, const vec3& normal) const {
        double total = 0.0;
        for (const auto& pl : point_lights) {
            total += point_light_sampling_weight(pl, p, normal);
        }
        return total;
    }

    bool sample_point_light(const point3& p, const vec3& normal, double total_weight,
                            const point_light*& out_light, double& out_pick_prob) const {
        out_light = nullptr;
        out_pick_prob = 0.0;
        if (point_lights.empty() || total_weight <= 1e-10) return false;

        double target = random_double() * total_weight;
        double accum = 0.0;
        const point_light* fallback_light = nullptr;
        double fallback_weight = 0.0;

        for (const auto& pl : point_lights) {
            double weight = point_light_sampling_weight(pl, p, normal);
            if (weight <= 1e-10) continue;

            fallback_light = &pl;
            fallback_weight = weight;
            accum += weight;
            if (target <= accum) {
                out_light = &pl;
                out_pick_prob = weight / total_weight;
                return true;
            }
        }

        if (!fallback_light) return false;
        out_light = fallback_light;
        out_pick_prob = fallback_weight / total_weight;
        return true;
    }

    // Optionally returns the first-hit surface albedo and normal via out parameters
    colour ray_colour(const ray& r0, int max_depth, const hittable& world, colour* out_albedo = nullptr, vec3* out_normal = nullptr, const hit_record* prehit = nullptr, const colour* precomputed_direct = nullptr) const {
        ray    current_ray = r0;
        colour throughput(1.0, 1.0, 1.0);
        colour result(0,0,0);

        for (int depth = 0; depth < max_depth; ++depth) {

            hit_record rec;

            // If caller supplied a precomputed first-hit record, use it for the first depth
            bool have_prehit = (prehit != nullptr && depth == 0);
            bool did_hit = false;
            if (have_prehit) {
                rec = *prehit;
                did_hit = true;
            } else {
                // Miss: accumulate environment background (optionally sun disc) and stop
                if (!world.hit(current_ray, interval(0.001, infinity), rec)) {
                    did_hit = false;
                } else {
                    did_hit = true;
                }
            }

            if (!did_hit) {
                colour env = background;

                // If sun is enabled, add environment sun radiance when ray
                // points toward the sun disc. This lets path-traced rays
                // pick up sun light like emissive backgrounds.
                if (use_sun) {
                    vec3 w = unit_vector(current_ray.direction());   // from camera into scene
                    vec3 to_sun = unit_vector(sun_dir);               // scene -> sun

                    double ang_rad = degrees_to_radians(sun_angular_radius);
                    double cos_max = std::cos(std::max(0.0, ang_rad));
                    double cos_theta = dot(w, to_sun);
                    if (cos_theta >= cos_max) {
                        // Soft edge weighting: map cosθ in [cos_max, 1] to [0,1]
                        double t = (cos_theta - cos_max) / std::max(1e-6, (1.0 - cos_max));
                        t = std::clamp(t, 0.0, 1.0);
                        env += sun_radiance * (float)t;
                    }
                }

                result += throughput * env;
                break;
            }

            // On the first surface hit, optionally return albedo and normal for AOVs
            if (depth == 0 && did_hit) {
                if (out_albedo) {
                    if (rec.mat) *out_albedo = rec.mat->albedo(rec);
                    else *out_albedo = colour(0,0,0);
                }
                if (out_normal) {
                    *out_normal = rec.normal;
                }
            }

            // Emission at the hit point
            colour emitted = rec.mat->emitted(rec.u, rec.v, rec.p);
            result += throughput * emitted;

            // Caustics lookup on diffuse receivers.
            // Since the photon map stores only caustic photons (paths that
            // went through a dielectric), we can query unconditionally on
            // non-specular hits.
            if (!rec.mat->is_specular() && depth <= 1) {
                if (caustics.size() > 0) {
                    colour Lc = caustics.query(rec.p, caustics_radius);
                    result += throughput * Lc;
                }

                if (enable_mnee && mnee_has_sphere) {
                    mnee_config mc;
                    mc.newton_max_iters = mnee_newton_max_iters;
                    mc.newton_tol       = mnee_newton_tol;
                    mc.step_eps         = mnee_step_eps;
                    mc.per_thread_budget= mnee_per_thread_budget;
                    mc.sun_ang_radius   = sun_angular_radius * (pi / 180.0);
                    mc.sun_samples      = mnee_sun_samples;
                    mc.gain_scale       = mnee_gain_scale;
                    // Sun-directed MNEE (if sun enabled)
                    if (use_sun) {
                        colour Lm = mnee_single_sphere_estimate(
                            rec.p, rec.normal,
                            unit_vector(sun_dir), sun_radiance,
                            mnee_sphere_center, mnee_sphere_radius, mnee_sphere_ior,
                            world, mc
                        );
                        result += throughput * Lm;
                    }

                    // Point-light MNEE: DISABLED - creates circular banding artifacts
                    // with low budgets and is extremely expensive. Point light caustics
                    // are better handled by regular path tracing with sufficient SPP.
                    // Sun caustics use Newton solver which is much more accurate.
                    // To re-enable: uncomment code and set mnee_per_thread_budget > 256
                }
            }

            // Scatter
            ray    scattered;
            colour attenuation;
            double pdf_bsdf = 1.0;

            if (!rec.mat->scatter(current_ray, rec, attenuation, scattered)) {
                // No scattering (pure light or absorption) – we're done
                break;
            }

            // For non-specular materials, do explicit light sampling (NEE) with MIS
            bool did_nee = false;
            if (!rec.mat->is_specular() && enable_mis) {
                vec3 V = -unit_vector(current_ray.direction());
                int num_direct_samples = std::max(0, direct_light_samples);

                // Sample sun with NEE+MIS (only if sampling enabled)
                if (num_direct_samples > 0 && use_sun) {
                    for (int ls = 0; ls < num_direct_samples; ++ls) {
                        vec3 Lc = unit_vector(sun_dir);
                        double ang_rad = degrees_to_radians(sun_angular_radius);
                        double cos_theta_max = std::cos(ang_rad);
                        
                        // Sample direction within sun disc
                        vec3 Ls = Lc;
                        if (ang_rad > 0.0) {
                            double u = random_double();
                            double v = random_double();
                            double cos_theta = (1.0 - u) + u * cos_theta_max;
                            double sin_theta = std::sqrt(std::max(0.0, 1.0 - cos_theta * cos_theta));
                            double phi = 2.0 * pi * v;
                            
                            double x = sin_theta * std::cos(phi);
                            double y = sin_theta * std::sin(phi);
                            double z = cos_theta;
                            
                            vec3 w_s = Lc;
                            vec3 a = (std::fabs(w_s.x()) > 0.1) ? vec3(0,1,0) : vec3(1,0,0);
                            vec3 u_s = unit_vector(cross(a, w_s));
                            vec3 v_s = cross(w_s, u_s);
                            Ls = unit_vector(u_s * (float)x + v_s * (float)y + w_s * (float)z);
                        }
                        
                        ray shadow_ray(rec.p + rec.normal * 0.001, Ls, current_ray.time());
                        double tr = compute_transmittance(shadow_ray, std::numeric_limits<double>::infinity(), world);
                        if (tr > 0.0) {
                            colour Li = sun_radiance * (float)tr;
                            colour direct = rec.mat->shade_direct(rec, V, Ls, Li, world);
                            colour sss = rec.mat->shade_sss(rec, V, Ls, Li, world);

                            double sample_weight = 1.0 / num_direct_samples;
                            double solid_angle = 2.0 * pi * (1.0 - cos_theta_max);
                            if (solid_angle > 1e-10) {
                                double pdf_light = 1.0 / solid_angle;
                                double pdf_bsdf_light = rec.mat->bsdf_pdf(rec, V, Ls);
                                double w_light = (pdf_light * pdf_light) /
                                                (pdf_light * pdf_light + pdf_bsdf_light * pdf_bsdf_light);
                                sample_weight = w_light / (pdf_light * num_direct_samples);
                            }

                            result += throughput * (direct + sss) * (float)sample_weight;
                            did_nee = true;
                        }
                    }
                }
                
                // Sample point lights with NEE+MIS
                // OPTIMIZATION: Sample ONE random light per bounce (weighted by contribution)
                // and multiply by N to get unbiased result at O(1) cost instead of O(N)
                if (num_direct_samples > 0 && !point_lights.empty()) {
                    double total_weight = total_point_light_sampling_weight(rec.p, rec.normal);
                    if (total_weight > 1e-10) {
                        for (int ls = 0; ls < num_direct_samples; ++ls) {
                            const point_light* sampled_light = nullptr;
                            double pick_prob = 0.0;
                            if (!sample_point_light(rec.p, rec.normal, total_weight, sampled_light, pick_prob)) continue;

                            const auto& pl = *sampled_light;
                            vec3 toLight = pl.position - rec.p;
                            double dist = toLight.length();
                            vec3 Ldir = unit_vector(toLight);
                            ray shadow_ray(rec.p + rec.normal * 0.001, Ldir, current_ray.time());
                            double tr = compute_transmittance(shadow_ray, dist - 0.001, world);
                            if (tr <= 0.0) continue;

                            double att = 1.0 / std::max(1e-4, dist * dist);
                            colour Li = pl.radiance * (float)(att * tr);

                            colour direct = rec.mat->shade_direct(rec, V, Ldir, Li, world);
                            colour sss = rec.mat->shade_sss(rec, V, Ldir, Li, world);
                            result += throughput * (direct + sss) * (float)(1.0 / (num_direct_samples * pick_prob));
                            did_nee = true;
                        }
                    }
                }
                
                // Sample emissive area lights with NEE+MIS
                if (num_direct_samples > 0 && !emissive_surfaces.empty() && !emissive_cdf.empty()) {
                    for (int ls = 0; ls < num_direct_samples; ++ls) {
                        // Importance sample one emissive surface by power
                        double r = random_double();
                        int sampled_idx = 0;
                        for (size_t i = 0; i < emissive_cdf.size(); ++i) {
                            if (r <= emissive_cdf[i]) {
                                sampled_idx = (int)i;
                                break;
                            }
                        }
                        
                        const auto& surf = emissive_surfaces[sampled_idx];
                        double pick_prob = (sampled_idx == 0) 
                            ? emissive_cdf[0] 
                            : (emissive_cdf[sampled_idx] - emissive_cdf[sampled_idx - 1]);
                        
                        if (pick_prob <= 1e-10) continue;
                        
                        // Sample a point on the emissive surface (for now use center; improve with random sampling later)
                        point3 light_pos = surf.position;
                        vec3 light_normal = surf.normal;
                        
                        vec3 toLight = light_pos - rec.p;
                        double dist = toLight.length();
                        if (dist <= 1e-6) continue;
                        
                        vec3 Ldir = unit_vector(toLight);
                        double NdotL = dot(rec.normal, Ldir);
                        if (NdotL <= 0.0) continue;
                        
                        double NdotL_light = dot(light_normal, -Ldir);
                        if (NdotL_light <= 0.0) continue; // backfacing
                        
                        // Visibility test
                        ray shadow_ray(rec.p + rec.normal * 0.001, Ldir, current_ray.time());
                        double tr = compute_transmittance(shadow_ray, dist - 0.001, world);
                        if (tr <= 0.0) continue;
                        
                        // PDF for light sampling: 1 / area (uniform sampling on surface)
                        double pdf_light = 1.0 / std::max(1e-10, surf.area);
                        
                        // Convert to solid angle PDF: pdf_omega = pdf_area * r^2 / (N_light · -L)
                        double pdf_light_omega = pdf_light * dist * dist / std::max(1e-10, NdotL_light);
                        double pdf_light_omega_total = pdf_light_omega * pick_prob;
                        
                        double pdf_bsdf_light = rec.mat->bsdf_pdf(rec, V, Ldir);
                        
                        // MIS balance heuristic weight
                        double w_light = (pdf_light_omega_total * pdf_light_omega_total) / 
                                        (pdf_light_omega_total * pdf_light_omega_total + pdf_bsdf_light * pdf_bsdf_light);
                        
                        colour Li = surf.emission * (float)((NdotL_light / std::max(1e-4, dist * dist)) * tr);
                        colour direct = rec.mat->shade_direct(rec, V, Ldir, Li, world);
                        colour sss = rec.mat->shade_sss(rec, V, Ldir, Li, world);
                        result += throughput * (direct + sss) * (float)(w_light / (pdf_light * pick_prob * num_direct_samples));
                        did_nee = true;
                    }
                }
            }

            // If direct lighting is disabled (direct_light_samples==0), SSS would otherwise never
            // show up for emissive/area lights. Do a single light-sample for SSS-only.
            if (direct_light_samples <= 0 && !emissive_surfaces.empty() && !emissive_cdf.empty()) {
                // Only bother if material actually has SSS enabled.
                const pbr_material* pbr = dynamic_cast<const pbr_material*>(rec.mat.get());
                const bool do_sss = (pbr && pbr->sss_strength > 0.0 && pbr->sss_model != SSS_NONE);
                if (do_sss) {
                    // Importance sample one emissive surface by power
                    double r = random_double();
                    int sampled_idx = 0;
                    for (size_t i = 0; i < emissive_cdf.size(); ++i) {
                        if (r <= emissive_cdf[i]) { sampled_idx = (int)i; break; }
                    }

                    const auto& surf = emissive_surfaces[sampled_idx];
                    double pick_prob = (sampled_idx == 0)
                        ? emissive_cdf[0]
                        : (emissive_cdf[sampled_idx] - emissive_cdf[sampled_idx - 1]);
                    if (pick_prob > 1e-10) {
                        point3 light_pos = surf.position;
                        vec3 light_normal = surf.normal;

                        vec3 toLight = light_pos - rec.p;
                        double dist = toLight.length();
                        if (dist > 1e-6) {
                            vec3 Ldir = unit_vector(toLight);
                            double NdotL = dot(rec.normal, Ldir);
                            double NdotL_light = dot(light_normal, -Ldir);
                            if (NdotL > 0.0 && NdotL_light > 0.0) {
                                ray shadow_ray(rec.p + rec.normal * 0.001, Ldir, current_ray.time());
                                double tr = compute_transmittance(shadow_ray, dist - 0.001, world);
                                if (tr > 0.0) {
                                    double pdf_light = 1.0 / std::max(1e-10, surf.area);
                                    double pdf_light_omega = pdf_light * dist * dist / std::max(1e-10, NdotL_light);
                                    vec3 V = -unit_vector(current_ray.direction());
                                    double pdf_light_omega_total = pdf_light_omega * pick_prob;
                                    double pdf_bsdf_light = rec.mat->bsdf_pdf(rec, V, Ldir);
                                    double w_light = (pdf_light_omega_total * pdf_light_omega_total) /
                                                    (pdf_light_omega_total * pdf_light_omega_total + pdf_bsdf_light * pdf_bsdf_light);

                                    colour Li = surf.emission * (float)((NdotL_light / std::max(1e-4, dist * dist)) * tr);
                                    colour sss = rec.mat->shade_sss(rec, V, Ldir, Li, world);
                                    result += throughput * sss * (float)(w_light / (pdf_light * pick_prob));
                                }
                            }
                        }
                    }
                }
            }

            // Directional sun (camera-mirrored) support: sample soft sun/shadows if enabled.
            // By default we only do this for specular materials or if MIS is disabled.
            // However, SSS needs an explicit direct-light evaluation to be visible.
            if (use_sun && (rec.mat->is_specular() || !enable_mis || direct_light_samples <= 0)) {
                // central sun direction (FROM scene toward sun)
                vec3 Lc = unit_vector(sun_dir);
                int samples = std::max(1, sun_shadow_samples);
                double ang_rad = degrees_to_radians(sun_angular_radius);
                double cos_theta_max = std::cos(ang_rad);

                double vis = 0.0;
                for (int si = 0; si < samples; ++si) {
                    vec3 Ls = Lc;
                    if (ang_rad > 0.0 && samples > 1) {
                        // sample a direction within the spherical cap around Lc
                        double u = random_double();
                        double v = random_double();
                        double cos_theta = (1.0 - u) + u * cos_theta_max; // mix in [1, cos_theta_max]
                        double sin_theta = std::sqrt(std::max(0.0, 1.0 - cos_theta * cos_theta));
                        double phi = 2.0 * pi * v;

                        double x = sin_theta * std::cos(phi);
                        double y = sin_theta * std::sin(phi);
                        double z = cos_theta;

                        // build orthonormal basis around Lc
                        vec3 w_s = Lc;
                        vec3 a = (std::fabs(w_s.x()) > 0.1) ? vec3(0,1,0) : vec3(1,0,0);
                        vec3 u_s = unit_vector(cross(a, w_s));
                        vec3 v_s = cross(w_s, u_s);

                        Ls = unit_vector(u_s * (float)x + v_s * (float)y + w_s * (float)z);
                    }

                    // Shadow ray transmittance regardless of N⋅L sign so dielectrics can transmit
                    ray shadow_ray(rec.p + rec.normal * 0.001, Ls, current_ray.time());
                    double tr = compute_transmittance(shadow_ray, std::numeric_limits<double>::infinity(), world);
                    vis += tr;
                }

                vis /= double(samples);
                if (vis > 0.0) {
                    vec3 V = -unit_vector(current_ray.direction());

                    colour direct = rec.mat->shade_direct(rec, V, Lc, sun_radiance, world);
                    colour sss = rec.mat->shade_sss(rec, V, Lc, sun_radiance, world);
                    result += throughput * ((direct + sss) * (float)vis);
                }
            }

            // Point lights: simple single-sample point lights with inverse-square falloff
            // If the caller supplied a precomputed direct contribution (for depth==0), use it instead of computing here.
            // Skip if we already did NEE+MIS above for non-specular materials
            if (depth == 0 && precomputed_direct) {
                result += throughput * (*precomputed_direct);
            }

            // Point lights: sample one random point light. We normally only do this for
            // specular materials or if MIS is disabled, but SSS needs explicit direct-light
            // evaluation even when MIS is enabled.
            if ((rec.mat->is_specular() || !enable_mis || direct_light_samples <= 0) && !point_lights.empty()) {
                double total_weight = total_point_light_sampling_weight(rec.p, rec.normal);
                if (total_weight > 1e-10) {
                    const point_light* sampled_light = nullptr;
                    double pick_prob = 0.0;
                    if (sample_point_light(rec.p, rec.normal, total_weight, sampled_light, pick_prob)) {
                        const auto& pl = *sampled_light;
                        vec3 toLight = pl.position - rec.p;
                        double dist = toLight.length();
                        vec3 Ldir = unit_vector(toLight);

                        ray shadow_ray(rec.p + rec.normal * 0.001, Ldir, current_ray.time());
                        double tr = compute_transmittance(shadow_ray, dist - 0.001, world);
                        if (tr > 0.0) {
                            double att = 1.0 / std::max(1e-4, dist * dist);
                            colour Li = pl.radiance * (float)(att * tr);

                            vec3 V = -unit_vector(current_ray.direction());

                            colour direct = rec.mat->shade_direct(rec, V, Ldir, Li, world);
                            colour sss = rec.mat->shade_sss(rec, V, Ldir, Li, world);
                            result += throughput * (direct + sss) * (float)(1.0 / pick_prob);
                        }
                    }
                }
            }

            // Update throughput & ray
            throughput = throughput * attenuation;
            current_ray = scattered;

            // Russian roulette after some depth
            if (depth >= 8) {
                double luminance = colour_luminance(throughput);

                double p = std::min(std::max(luminance, 0.1), 0.95);

                if (random_double() > p)
                    break;

                throughput /= p;
            }
        }

        return result;
    }

    public:
        // Caustics photon map (built externally before rendering); queried on diffuse hits
        photon_map caustics;
        double     caustics_radius = 0.08; // gather radius

    // Compute deterministic transmittance along a ray up to max_t by
    // accumulating per-hit material opacity (alpha). Returns value in
    // [0,1], where 0 = fully blocked, 1 = fully clear.
    double compute_transmittance(const ray& r, double max_t, const hittable& world) const {
        double T = 1.0;
        double tmin = 0.001;
        double tmax = max_t;
        hit_record rec;

        while (tmin < tmax && world.hit(r, interval(tmin, tmax), rec)) {
            double opacity = 1.0;
            if (rec.mat) opacity = rec.mat->opacity_at(rec);

            // transparency is (1 - opacity)
            double trans = 1.0 - clamp01(opacity);

            // If fully opaque, blocked
            if (trans <= 1e-6) return 0.0;

            T *= trans;
            if (T <= 1e-6) return 0.0;

            // Advance min t to just beyond this hit and continue
            tmin = rec.t + 0.001;
        }

        return T;
    }

};

#endif
