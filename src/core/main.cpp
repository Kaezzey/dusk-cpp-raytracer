#include "../../include/core/dusktracer.h"

#include "../../include/core/BVH.h"
#include "../../include/core/camera.h"
#include "../../include/core/editor_camera.h"
#include "../../include/core/hittable.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/materials/material.h"
#include "../../include/core/quad.h"
#include "../../include/core/sphere.h"
#include "../../include/core/materials/texture.h"

#include <fstream>
#include <cmath>

void bouncing_spheres(){

    hittable_list world;

    auto checker = make_shared<checker_texture>(0.32, colour(.2, .3, .1), colour(.9, .9, .9));
    world.add(make_shared<sphere>(point3(0,-1000,0), 1000, make_shared<lambertian>(checker)));

    for (int a = -11; a < 11; a++) {
        for (int b = -11; b < 11; b++) {
            auto choose_mat = random_double();
            point3 center(a + 0.9*random_double(), 0.2, b + 0.9*random_double());

            if ((center - point3(4, 0.2, 0)).length() > 0.9) {
                shared_ptr<material> sphere_material;

                if (choose_mat < 0.8) {
                    // diffuse
                    auto albedo = colour::random() * colour::random();
                    sphere_material = make_shared<lambertian>(albedo);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                } else if (choose_mat < 0.95) {
                    // metal
                    auto albedo = colour::random(0.5, 1);
                    auto fuzz = random_double(0, 0.5);
                    sphere_material = make_shared<metal>(albedo, fuzz);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                } else {
                    // glass
                    sphere_material = make_shared<dielectric>(1.5);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                }
            }
        }
    }

    auto earth_tex = make_shared<image_texture>("textures/planet.png");

    auto glass = make_shared<dielectric>(1.5); 
    world.add(make_shared<sphere>(point3(0, 1, 0), 1.0, glass));

    auto material2 = make_shared<lambertian>(colour(0.4, 0.2, 0.1));
    world.add(make_shared<sphere>(point3(-4, 1, 0), 1.0, material2));

    auto earth_metal = make_shared<metal>(colour(1.0, 0, 1.0), 0.0);   // polished textured metal
    world.add(make_shared<sphere>(point3(4,1,0), 1.0, earth_metal));

    world = hittable_list(make_shared<bvh_node>(world));

    camera cam;

    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 1080;
    cam.samples_per_pixel = 200;
    cam.max_depth         = 50;
    cam.background        = colour(0.70, 0.80, 1.00);

    cam.vfov     = 20;
    cam.lookfrom = point3(13,2,3);
    cam.lookat   = point3(0,0,0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0.6;
    cam.focus_dist    = 10.0;

    std::ofstream file("output.ppm");
    cam.render(world, file);

}

void checkered_spheres() {

    hittable_list world;

    auto checker = make_shared<checker_texture>(0.32, colour(.2, .3, .1), colour(.9, .9, .9));

    world.add(make_shared<sphere>(point3(0,-10, 0), 10, make_shared<lambertian>(checker)));
    world.add(make_shared<sphere>(point3(0, 10, 0), 10, make_shared<lambertian>(checker)));

    world = hittable_list(make_shared<bvh_node>(world));

    camera cam;

    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 400;
    cam.samples_per_pixel = 100;
    cam.max_depth         = 50;
    cam.background        = colour(0.70, 0.80, 1.00);

    cam.vfov     = 20;
    cam.lookfrom = point3(13,2,3);
    cam.lookat   = point3(0,0,0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0;

    std::ofstream file("output.ppm");
    cam.render(world, file);
}

void sphere_texture(){
    hittable_list world;

    auto earth_tex = make_shared<image_texture>("textures/planet.png");
    auto earth_metal = make_shared<metal>(earth_tex, 0.0);   // polished textured metal
    world.add(make_shared<sphere>(point3(0,0,0), 2.0, earth_metal));

    camera cam;

    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 400;
    cam.samples_per_pixel = 100;
    cam.max_depth         = 50;
    cam.background        = colour(0.70, 0.80, 1.00);

    cam.vfov     = 20;
    cam.lookfrom = point3(13,2,3);
    cam.lookat   = point3(0,0,0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0;

    std::ofstream file("output.ppm");
    cam.render(world, file);
}

void perlin_spheres(){
    hittable_list world;

    auto pertext = make_shared<noise_texture>(4.0);
    world.add(make_shared<sphere>(point3(0,-1000,0), 1000, make_shared<lambertian>(pertext)));
    world.add(make_shared<sphere>(point3(0,2,0), 2, make_shared<lambertian>(pertext)));

    world = hittable_list(make_shared<bvh_node>(world));

    camera cam;

    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 400;
    cam.samples_per_pixel = 100;
    cam.max_depth         = 50;
    cam.background        = colour(0.70, 0.80, 1.00);

    cam.vfov     = 20;
    cam.lookfrom = point3(13,2,3);
    cam.lookat   = point3(0,0,0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0;

    std::ofstream file("output.ppm");

    cam.render(world, file);
}

void quads() {
    hittable_list world;

    // Materials
    auto left_red     = make_shared<lambertian>(colour(1.0, 0.2, 0.2));
    auto back_green   = make_shared<lambertian>(colour(0.2, 1.0, 0.2));
    auto right_blue   = make_shared<lambertian>(colour(0.2, 0.2, 1.0));
    auto upper_orange = make_shared<lambertian>(colour(1.0, 0.5, 0.0));
    auto lower_teal   = make_shared<lambertian>(colour(0.2, 0.8, 0.8));

    // Quads
    world.add(make_shared<quad>(point3(-3,-2, 5), vec3(0, 0,-4), vec3(0, 4, 0), left_red));
    world.add(make_shared<quad>(point3(-2,-2, 0), vec3(4, 0, 0), vec3(0, 4, 0), back_green));
    world.add(make_shared<quad>(point3( 3,-2, 1), vec3(0, 0, 4), vec3(0, 4, 0), right_blue));
    world.add(make_shared<quad>(point3(-2, 3, 1), vec3(4, 0, 0), vec3(0, 0, 4), upper_orange));
    world.add(make_shared<quad>(point3(-2,-3, 5), vec3(4, 0, 0), vec3(0, 0,-4), lower_teal));

    world = hittable_list(make_shared<bvh_node>(world));

    camera cam;

    cam.aspect_ratio      = 1.0;
    cam.image_width       = 800;
    cam.samples_per_pixel = 200;
    cam.max_depth         = 50;
    cam.background        = colour(0.70, 0.80, 1.00);

    cam.vfov     = 80;
    cam.lookfrom = point3(0,0,9);
    cam.lookat   = point3(0,0,0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0;

    std::ofstream file("output.ppm");
    cam.render(world, file);
}

void simple_light() {
    hittable_list world;

    auto pertext = make_shared<noise_texture>(4);
    world.add(make_shared<sphere>(point3(0,-1000,0), 1000, make_shared<lambertian>(pertext)));
    world.add(make_shared<sphere>(point3(0,2,0), 2, make_shared<lambertian>(pertext)));

    auto difflight = make_shared<diffuse_light>(colour(4,4,4));
    world.add(make_shared<sphere>(point3(0,7,0), 2, difflight));
    world.add(make_shared<quad>(point3(3,1,-2), vec3(2,0,0), vec3(0,2,0), difflight));

    world = hittable_list(make_shared<bvh_node>(world));

    camera cam;

    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 1920;
    cam.samples_per_pixel = 200;
    cam.max_depth         = 50;
    cam.background        = colour(0,0,0);

    cam.vfov     = 20;
    cam.lookfrom = point3(26,3,6);
    cam.lookat   = point3(0,2,0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0;

    std::ofstream file("output.ppm");
    cam.render(world, file);
}

void cornell_box() {
    hittable_list world;

    auto red   = make_shared<lambertian>(colour(.65, .05, .05));
    auto white = make_shared<lambertian>(colour(.73, .73, .73));
    auto green = make_shared<lambertian>(colour(.12, .45, .15));
    auto light = make_shared<diffuse_light>(colour(15, 15, 15));

    world.add(make_shared<quad>(point3(555,0,0), vec3(0,555,0), vec3(0,0,555), green));
    world.add(make_shared<quad>(point3(0,0,0), vec3(0,555,0), vec3(0,0,555), red));
    world.add(make_shared<quad>(point3(343, 554, 332), vec3(-130,0,0), vec3(0,0,-105), light));
    world.add(make_shared<quad>(point3(0,0,0), vec3(555,0,0), vec3(0,0,555), white));
    world.add(make_shared<quad>(point3(555,555,555), vec3(-555,0,0), vec3(0,0,-555), white));
    world.add(make_shared<quad>(point3(0,0,555), vec3(555,0,0), vec3(0,555,0), white));

    // ---------------- Tall box ----------------
    auto tall_box = box(
        point3(0, 0, 0),
        point3(165, 330, 165),
        white
    );

    world.add(make_shared<transform>(
        tall_box,
        vec3(265, 0, 295),   // translation
        vec3(0, 15, 0),      // rotation (deg): +15° about Y
        1.0                  // scale
    ));

    // ---------------- Short box ----------------
    auto short_box = box(
        point3(0, 0, 0),
        point3(165, 165, 165),
        white
    );

    world.add(make_shared<transform>(
        short_box,
        vec3(130, 0, 65),    // translation
        vec3(0, -18, 0),     // rotation (deg): -18° about Y
        1.0                  // scale
    ));

    world = hittable_list(make_shared<bvh_node>(world));

    camera cam;

    cam.aspect_ratio      = 1.0;
    cam.image_width       = 1080;
    cam.samples_per_pixel = 200;
    cam.max_depth         = 50;
    cam.background        = colour(0,0,0);

    cam.vfov     = 40;
    cam.lookfrom = point3(278, 278, -800);
    cam.lookat   = point3(278, 278, 0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0;

    std::ofstream file("output.ppm");
    cam.render(world, file);
}

int main() {

    int scene = 6;

    switch (scene){
        case 0: bouncing_spheres();
            break;
        case 1: checkered_spheres();
            break;
        case 2: sphere_texture();
            break;
        case 3: perlin_spheres();
            break;
        case 4: quads();
            break;
        case 5: simple_light();
            break;
        case 6: cornell_box();
            break;
    }

    return 0;
    
}



