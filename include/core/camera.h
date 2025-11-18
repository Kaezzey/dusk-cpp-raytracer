#ifndef CAMERA_H
#define CAMERA_H

#include "hittable.h"
#include "material.h"
#include <cmath>
#include <chrono>

class camera {

  public:
    //ratio of image width over height
    double aspect_ratio = 1.0; 
    
    //rendered image width in pixel count
    int image_width  = 100;
    int samples_per_pixel = 10;
    int max_depth = 10; //max recursion depth for ray tracing

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

    // sampling method (choose HALTON for high-quality, efficient AA)
    enum sampling_method_e { RANDOM_SAMPLES = 0, HALTON_SAMPLES = 1 };
    sampling_method_e sampling_method = HALTON_SAMPLES;

    void render(const hittable& world, std::ostream& out = std::cout) {

        initialize();

        out << "P3\n" << image_width << ' '
            << image_height << "\n255\n";

        using clock = std::chrono::steady_clock;

        auto render_start = clock::now();

        int total_scanlines = image_height;
        int completed = 0;

        // Rolling chunk settings
        const int chunk_size = 4;   // update every N scanlines
        int chunk_counter = 0;

        auto chunk_start = clock::now();

        // Long-term accumulators
        double accumulated_time = 0.0;
        int accumulated_lines = 0;

        double eta_longterm = 0.0;
        double eta_shortterm = 0.0;

        // Progress bar settings
        const int bar_width = 40;

        for (int j = 0; j < image_height; j++) {

            completed++;
            chunk_counter++;

            // ---- render scanline ----
            for (int i = 0; i < image_width; i++) {

                colour pixel_colour(0,0,0);

                for (int s = 0; s < samples_per_pixel; s++) {
                    ray r = get_ray(i, j, s);
                    pixel_colour += ray_colour(r, max_depth, world);
                }

                write_colour(out, pixel_samples_scale * pixel_colour);
            }

            // ---- ETA + progress bar update every chunk ----
            if (chunk_counter >= chunk_size) {
                chunk_counter = 0;

                auto now = clock::now();
                double chunk_seconds =
                    std::chrono::duration<double>(now - chunk_start).count();

                chunk_start = now;

                // Short-term (chunk) average
                double avg_chunk_time = chunk_seconds / chunk_size;
                eta_shortterm = avg_chunk_time * (total_scanlines - completed);

                // Long-term average
                accumulated_time += chunk_seconds;
                accumulated_lines += chunk_size;

                double long_avg = accumulated_time / accumulated_lines;
                eta_longterm = long_avg * (total_scanlines - completed);

                // Blended estimate
                double eta = 0.75 * eta_longterm + 0.25 * eta_shortterm;

                // Elapsed
                double elapsed =
                    std::chrono::duration<double>(now - render_start).count();

                // Progress %
                double pct = double(completed) / total_scanlines;

                // Draw progress bar
                std::clog << "\r[";
                int pos = int(bar_width * pct);
                for (int k = 0; k < bar_width; k++) {
                    if (k < pos) std::clog << "#";
                    else std::clog << "-";
                }

                // Time formatting
                int rem_i = int(eta);
                int rem_m = rem_i / 60;
                int rem_s = rem_i % 60;

                std::clog << "] "
                        << int(pct * 100.0) << "% "
                        << "| Elapsed: " << int(elapsed) << "s "
                        << "| Remaining: " << rem_m << ":"
                        << (rem_s < 10 ? "0" : "") << rem_s
                        << std::flush;
            }
        }

        // ---- Final time ----
        auto end = clock::now();
        double total_time =
            std::chrono::duration<double>(end - render_start).count();

        int tm = int(total_time) / 60;
        int ts = int(total_time) % 60;

        std::clog << "\nDone. Total time: "
                << tm << ":" << (ts < 10 ? "0" : "") << ts << "\n";
    }

  private:
    int image_height;
    
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
        image_height = int(image_width / aspect_ratio);
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

        return ray(ray_origin, ray_direction);

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

    colour ray_colour(const ray& r, int depth, const hittable& world) const {

        if (depth <= 0) {
            return colour(0,0,0);
        }

        hit_record rec;

        if (world.hit(r, interval(0.001, infinity), rec)) {
            ray scattered;
            colour attenuation;
            if (rec.mat->scatter(r, rec, attenuation, scattered))
                return attenuation * ray_colour(scattered, depth-1, world);
            return colour(0,0,0);
        }

        vec3 unit_direction = unit_vector(r.direction());
        auto a = 0.5*(unit_direction.y() + 1.0);
        return (1.0-a)*colour(1.0, 1.0, 1.0) + a*colour(0.5, 0.7, 1.0);
    }
};

#endif