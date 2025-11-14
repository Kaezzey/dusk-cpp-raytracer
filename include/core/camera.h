#ifndef CAMERA_H
#define CAMERA_H

#include "hittable.h"
#include <cmath>

class camera {
    
  public:
    //ratio of image width over height
    double aspect_ratio = 1.0; 
    
    //rendered image width in pixel count
    int image_width  = 100;
    int samples_per_pixel = 10;
    // sampling method (choose HALTON for high-quality, efficient AA)
    enum sampling_method_e { RANDOM_SAMPLES = 0, HALTON_SAMPLES = 1 };
    sampling_method_e sampling_method = HALTON_SAMPLES;

    void render(const hittable& world) {

        initialize();

        std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";

        for (int j = 0; j < image_height; j++) {
            std::clog << "\rScanlines remaining: " << (image_height - j) << ' ' << std::flush;
            for (int i = 0; i < image_width; i++) {
                colour pixel_colour(0,0,0);
                for (int sample = 0; sample < samples_per_pixel; sample++) {
                    ray r = get_ray(i, j, sample);
                    pixel_colour += ray_colour(r, world);
                }
                write_colour(std::cout, pixel_samples_scale * pixel_colour);
            }
        }

        std::clog << "\rDone.                 \n";
    }

  private:
    int image_height;
    
    //camera parameters
    point3 center;
    double pixel_samples_scale;         
    point3 pixel00_loc;    
    vec3 pixel_delta_u;  
    vec3 pixel_delta_v;  

    void initialize() {
        image_height = int(image_width / aspect_ratio);
        image_height = (image_height < 1) ? 1 : image_height;

        pixel_samples_scale = 1.0 / samples_per_pixel;

        center = point3(0, 0, 0);

        //determine viewport dimensions.
        auto focal_length = 1.0;
        auto viewport_height = 2.0;
        auto viewport_width = viewport_height * (double(image_width)/image_height);

        //calculate the vectors across the horizontal and down the vertical viewport edges.
        auto viewport_u = vec3(viewport_width, 0, 0);
        auto viewport_v = vec3(0, -viewport_height, 0);

        //calculate the horizontal and vertical delta vectors from pixel to pixel.
        pixel_delta_u = viewport_u / image_width;
        pixel_delta_v = viewport_v / image_height;

        //calculate the location of the upper left pixel.
        auto viewport_upper_left =
            center - vec3(0, 0, focal_length) - viewport_u/2 - viewport_v/2;
            
        pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);
    }

    ray get_ray(int i, int j, int sample) const {
        auto offset = sample_square(i, j, sample);
        auto pixel_sample = pixel00_loc
            + (i + offset.x()) * pixel_delta_u
            + (j + offset.y()) * pixel_delta_v;

        auto ray_origin =  center;
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

    colour ray_colour(const ray& r, const hittable& world) const {

        hit_record rec;

        if (world.hit(r, interval(0, infinity), rec)) {
            return 0.5 * (rec.normal + colour(1,1,1));
        }

        vec3 unit_direction = unit_vector(r.direction());
        auto a = 0.5*(unit_direction.y() + 1.0);
        return (1.0-a)*colour(1.0, 1.0, 1.0) + a*colour(0.5, 0.7, 1.0);
    }
};

#endif