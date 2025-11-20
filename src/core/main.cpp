#include "../../include/core/dusktracer.h"

#include "../../include/core/BVH.h"
#include "../../include/core/camera.h"
#include "../../include/core/editor_camera.h"
#include "../../include/core/hittable.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/materials/material.h"
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

    auto earth_metal = make_shared<metal>(earth_tex, 0.0);   // polished textured metal
    world.add(make_shared<sphere>(point3(4,1,0), 1.0, earth_metal));

    world = hittable_list(make_shared<bvh_node>(world));

    camera cam;

    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 1080;
    cam.samples_per_pixel = 200;
    cam.max_depth         = 50;

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

    cam.vfov     = 20;
    cam.lookfrom = point3(13,2,3);
    cam.lookat   = point3(0,0,0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0;

    std::ofstream file("output.ppm");
    cam.render(world, file);
}

int main() {

    int scene = 0;

    switch (scene){
        case 0: bouncing_spheres();
            break;
        case 1: checkered_spheres();
            break;
        case 2: sphere_texture();
            break;
    }

    return 0;
    
}



