#include "../../include/core/dusktracer.h"

#include "../../include/core/camera.h"
#include "../../include/core/editor_camera.h"
#include "../../include/core/hittable.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/material.h"
#include "../../include/core/sphere.h"

#include <fstream>
#include <cmath>

// Helper: return random integer in [min, max] using existing random_double
inline int random_int(int min, int max) {
    // random_double(a, b) is expected to return a double in [a, b)
    return static_cast<int>(std::floor(random_double(static_cast<double>(min), static_cast<double>(max) + 1.0)));
}

hittable_list scene(){

    hittable_list world;

    // Ground plane
    auto ground = make_shared<lambertian>(colour(0.45, 0.45, 0.47));
    world.add(make_shared<sphere>(point3(0,-1000,0), 1000, ground));

    // -------- PALETTE --------
    // Vibrant but controlled colours
    std::vector<colour> palette = {
        colour(0.93, 0.33, 0.33),  // red
        colour(0.98, 0.63, 0.17),  // orange
        colour(0.93, 0.89, 0.16),  // yellow
        colour(0.20, 0.80, 0.35),  // green
        colour(0.17, 0.55, 0.98),  // blue
        colour(0.58, 0.27, 0.83),  // purple
        colour(0.96, 0.47, 0.67)   // pink
    };


    // ===== HERO SPHERES (foreground) =====

    // Center: crystal-clear glass
    auto glass_main = make_shared<dielectric>(1.5);
    world.add(make_shared<sphere>(point3(0, 1.0, 0), 1.0, glass_main));

    // Left: coloured metallic
    auto hero_metal = make_shared<metal>(colour(0.8, 0.6, 0.4), 0.05);
    world.add(make_shared<sphere>(point3(-4, 1, 0), 1.0, hero_metal));

    // Right: coloured diffuse
    auto hero_diffuse = make_shared<lambertian>(colour(0.3, 0.2, 0.75));
    world.add(make_shared<sphere>(point3(4, 1, 0), 1.0, hero_diffuse));


    // ===== BIG BACKGROUND STRUCTURE =====
    auto giant_glass = make_shared<dielectric>(1.5);
    world.add(make_shared<sphere>(point3(0, 6, -8), 3.0, giant_glass));

    auto tall_dark = make_shared<metal>(colour(0.25, 0.25, 0.32), 0.0);
    world.add(make_shared<sphere>(point3(8, 8, -4), 5.0, tall_dark));


    // ===== CRAZY RANDOM SPHERES (COLORFUL) =====

    for (int a = -10; a < 10; a++) {
        for (int b = -10; b < 10; b++) {

            auto choose = random_double();

            point3 center(
                a + 0.9*random_double(),
                random_double(0.10, 1.5),  // random heights
                b + 0.9*random_double()
            );

            if ((center - point3(4, 0.2, 0)).length() < 1.0) continue;

            double radius = random_double(0.12, 0.35);

            shared_ptr<material> mat;

            if (choose < 0.40) {
                // Vibrant diffuse
                colour al = palette[random_int(0, palette.size()-1)];
                al *= random_double(0.5, 1.0);
                mat = make_shared<lambertian>(al);

            } else if (choose < 0.85) {
                // Tinted semi-transparent glass
                double ir = random_double(1.3, 1.7);
                mat = make_shared<dielectric>(ir);

            } else {
                // Shiny metal
                colour al = palette[random_int(0, palette.size()-1)];
                al *= 0.6;
                double fuzz = random_double(0, 0.3);
                mat = make_shared<metal>(al, fuzz);
            }

            world.add(make_shared<sphere>(center, radius, mat));
        }
    }


    // ===== FLOATING ORBS =====

    for (int i = 0; i < 15; i++) {
        point3 c(
            random_double(-5, 5),
            random_double(1.0, 5),
            random_double(-3, 3)
        );

        double r = random_double(0.15, 0.4);

        auto col = palette[random_int(0, palette.size()-1)] * random_double(0.4, 1.0);

        auto mat = make_shared<lambertian>(col);
        world.add(make_shared<sphere>(c, r, mat));
    }


    // ===== A FEW “GLOW BALLS” FOR SPICE =====

    // Fake emissives (not true emissive material, but bright Lambertian)
    for (int i = 0; i < 5; i++) {
        point3 c(
            random_double(-2, 2),
            random_double(0.5, 3.0),
            random_double(-5, -3)
        );
        auto glow = make_shared<lambertian>(colour(2.0, 1.8, 1.5)); // bright warm
        world.add(make_shared<sphere>(c, 0.25, glow));
    }

    return world;
}

int main() {
    
    hittable_list world = scene();

    camera cam;

    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 1280;
    cam.samples_per_pixel = 100;
    cam.max_depth         = 50;


    cam.vfov     = 22;
    cam.lookfrom = point3(13, 3, 4);
    cam.lookat   = point3(0, 1, 0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0.4;
    cam.focus_dist    = 13.0;

    std::ofstream file("output.ppm");
            
    cam.render(world, file);
    

}



