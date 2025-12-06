#ifndef CAMERA_H
#define CAMERA_H

#include "hittable.h"
#include "materials/material.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <vector>
#include <atomic>

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

    bool   use_sun = false;
    vec3   sun_dir = vec3(0,0,0);
    colour sun_radiance = colour(0,0,0);
    double sun_angular_radius = 0.0;
    int    sun_shadow_samples = 8; // number of shadow samples for soft sun (0 = off)

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

    // simple integer hash -> double in [0,1)
    static double pixel_hash_double(int a, int b) {
        unsigned int n = (unsigned int)(a * 73856093u ^ b * 19349663u);
        n = (n << 13) ^ n;
        unsigned int nn = (n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu;
        return double(nn) / double(0x7fffffffu);
    }

    // Optionally returns the first-hit surface albedo and normal via out parameters
    colour ray_colour(const ray& r0, int max_depth, const hittable& world, colour* out_albedo = nullptr, vec3* out_normal = nullptr) const {
        ray    current_ray = r0;
        colour throughput(1.0, 1.0, 1.0);
        colour result(0,0,0);

        for (int depth = 0; depth < max_depth; ++depth) {

            hit_record rec;

            // Miss: accumulate background and stop
            if (!world.hit(current_ray, interval(0.001, infinity), rec)) {
                result += throughput * background;
                break;
            }

            // On the first surface hit, optionally return albedo and normal for AOVs
            if (depth == 0) {
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

            // Scatter
            ray    scattered;
            colour attenuation;

            if (!rec.mat->scatter(current_ray, rec, attenuation, scattered)) {
                // No scattering (pure light or absorption) – we're done
                break;
            }

            // Sun lighting (directional/emissive sun disk) with soft shadow sampling.
            if (use_sun) {
                // central sun direction (FROM scene toward sun)
                vec3 Lc = unit_vector(sun_dir);
                double NdotLc = dot(rec.normal, Lc);
                if (NdotLc > 0.0) {
                    // If angular radius is effectively zero or samples == 1, do a single shadow ray
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

                            // sample direction in local coordinates (z = center)
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

                        double NdotL = dot(rec.normal, Ls);
                        if (NdotL <= 0.0) continue;

                        // shadow ray from slightly offset point
                        ray shadow_ray(rec.p + rec.normal * 0.001, Ls, current_ray.time());
                        hit_record shadow_rec;
                        if (!world.hit(shadow_ray, interval(0.001, infinity), shadow_rec)) {
                            vis += 1.0;
                        }
                    }

                    vis /= double(samples);
                    if (vis > 0.0) {
                        // Evaluate material-specific direct shading (PBR-aware override)
                        vec3 V = -unit_vector(current_ray.direction());
                        colour direct = rec.mat->shade_direct(rec, V, Lc, sun_radiance);
                        result += throughput * (direct * (float)vis);
                    }
                }
            }

            // Update throughput & ray
            throughput = throughput * attenuation;
            current_ray = scattered;

            // Russian roulette after some depth
            if (depth > 25) {
                double luminance = 0.2126 * throughput.x()
                                + 0.7152 * throughput.y()
                                + 0.0722 * throughput.z();

                double p = std::min(std::max(luminance, 0.1), 0.95);

                if (random_double() > p)
                    break;

                throughput /= p;
            }
        }

        return result;
    }

};

#endif