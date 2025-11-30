#include "../../include/core/dusktracer.h"

#include "../../include/core/BVH.h"
#include "../../include/core/camera.h"
#include "../../include/core/constant_medium.h"
#include "../../include/core/hittable.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/materials/material.h"
#include "../../include/core/quad.h"
#include "../../include/core/sphere.h"
#include "../../include/core/triangle.h"
#include "../../include/core/sun.h"
#include "../../include/core/materials/texture.h"
#include "../../include/core/mesh_loader.h"

#include <unordered_map>
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

    auto albedo_tex    = make_shared<image_texture>("textures/steel-vented-siding_albedo.png");
    auto metallic_tex  = make_shared<image_texture>("textures/steel-vented-siding_metallic.png");
    auto roughness_tex = make_shared<image_texture>("textures/steel-vented-siding_roughness.png");
    auto normal_tex    = make_shared<image_texture>("textures/steel-vented-siding_normal-ogl.png");

    auto pbr_metal = make_shared<pbr_material>(
        albedo_tex,   // same baseColor
        metallic_tex,
        roughness_tex,
        normal_tex,
        1.0                     // normal strength          
    );

    // ---------------- Tall box ----------------
    auto tall_box = box(
        point3(0, 0, 0),
        point3(165, 330, 165),
        pbr_metal
    );

    world.add(make_shared<transform>(
        tall_box,
        vec3(265, 0, 295),   // translation
        vec3(0, 15, 0),      // rotation (deg): +15° about Y
        vec3(1.0, 1.0, 1.0)  // scale
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
        vec3(1.0, 1.0, 1.0)  // scale
    ));

    world = hittable_list(make_shared<bvh_node>(world));

    camera cam;

    cam.aspect_ratio      = 1.0;
    cam.image_width       = 1080;
    cam.samples_per_pixel = 150;
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

void final_scene(int image_width, int samples_per_pixel, int max_depth) {
    hittable_list boxes1;
    auto ground = make_shared<lambertian>(colour(0.45, 0.83, 0.81));

    int boxes_per_side = 20;
    for (int i = 0; i < boxes_per_side; i++) {
        for (int j = 0; j < boxes_per_side; j++) {
            auto w = 100.0;
            auto x0 = -1000.0 + i*w;
            auto z0 = -1000.0 + j*w;
            auto y0 = 0.0;
            auto x1 = x0 + w;
            auto y1 = random_double(1,101);
            auto z1 = z0 + w;

            boxes1.add(box(point3(x0,y0,z0), point3(x1,y1,z1), ground));
        }
    }

    hittable_list world;

    world.add(make_shared<bvh_node>(boxes1));

    auto light = make_shared<diffuse_light>(colour(7, 7, 7));
    world.add(make_shared<quad>(point3(123,554,147), vec3(300,0,0), vec3(0,0,265), light));

    auto center1 = point3(400, 400, 200);
    auto center2 = center1 + vec3(30,0,0);
    auto sphere_material = make_shared<lambertian>(colour(0.7, 0.3, 0.1));
    world.add(make_shared<sphere>(center1, center2, 50, sphere_material));

    world.add(make_shared<sphere>(point3(260, 150, 45), 50, make_shared<dielectric>(1.5)));
    world.add(make_shared<sphere>(
        point3(0, 150, 145), 50, make_shared<metal>(colour(0.8, 0.8, 0.9), 1.0)
    ));

    auto boundary = make_shared<sphere>(point3(360,150,145), 70, make_shared<dielectric>(1.5));
    world.add(boundary);
    world.add(make_shared<constant_medium>(boundary, 0.2, colour(0.2, 0.4, 0.9)));
    boundary = make_shared<sphere>(point3(0,0,0), 5000, make_shared<dielectric>(1.5));
    world.add(make_shared<constant_medium>(boundary, .0001, colour(1,1,1)));

    auto albedo_tex    = make_shared<image_texture>("textures/rusted-panels_albedo.png");
    auto metallic_tex  = make_shared<image_texture>("textures/rusted-panels_metallic.png");
    auto roughness_tex = make_shared<image_texture>("textures/rusted-panels_roughness.png");
    auto normal_tex    = make_shared<image_texture>("textures/rusted-panels_normal-ogl.png");

    auto rough = make_shared<pbr_material>(
        albedo_tex,   // same baseColor
        metallic_tex,
        roughness_tex,
        normal_tex,
        1.0                     // normal strength          
    );

    world.add(make_shared<sphere>(point3( 400, 200, 400), 100, rough));

    auto pertext = make_shared<noise_texture>(0.2);
    world.add(make_shared<sphere>(point3(220,280,300), 80, make_shared<lambertian>(pertext)));

    hittable_list boxes2;
    auto white = make_shared<lambertian>(colour(.73, .73, .73));
    int ns = 1000;
    for (int j = 0; j < ns; j++) {
        boxes2.add(make_shared<sphere>(point3::random(0,165), 10, white));
    }

    auto object = make_shared<bvh_node>(boxes2);

    world.add(make_shared<transform>(
        object,
        vec3(-100, 270, 395),   // translation
        vec3(0, 15, 0),          // rotation degrees (rx, ry, rz)
        vec3(1.0, 1.0, 1.0)      // scale
    ));

    world = hittable_list(make_shared<bvh_node>(world));

    camera cam;

    cam.aspect_ratio      = 1.0;
    cam.image_width       = image_width;
    cam.samples_per_pixel = samples_per_pixel;
    cam.max_depth         = max_depth;
    cam.background        = colour(0,0,0);

    cam.vfov     = 40;
    cam.lookfrom = point3(478, 278, -600);
    cam.lookat   = point3(278, 278, 0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0;

    std::ofstream file("output.ppm");
    cam.render(world, file);
}

void PBR_Scene() {
    hittable_list world;

    auto checker = make_shared<checker_texture>(0.32, colour(0.2, 0.2, 0.2), colour(.9, .9, .9));
    world.add(make_shared<sphere>(point3(0,-1000,0), 1000, make_shared<lambertian>(checker)));

    auto albedo_tex    = make_shared<image_texture>("textures/rusted-panels_albedo.png");
    auto metallic_tex  = make_shared<image_texture>("textures/rusted-panels_metallic.png");
    auto roughness_tex = make_shared<image_texture>("textures/rusted-panels_roughness.png");
    auto normal_tex    = make_shared<image_texture>("textures/rusted-panels_normal-ogl.png");

    auto shiny = make_shared<pbr_material>(
        colour(0.8, 0.2, 0.2),   // red
        0.0,                     // non-metal
        0.02,
        nullptr                    // very glossy
    );

    auto metal_normal = make_shared<pbr_material>(
        albedo_tex,   // same baseColor
        metallic_tex,
        roughness_tex,
        normal_tex,
        1.0                     // normal strength          
    );

    world.add(make_shared<sphere>(point3(-1.5, 1, 0), 1.0, shiny));
    world.add(make_shared<sphere>(point3( 1.5, 1, 0), 1.0, metal_normal));


    world = hittable_list(make_shared<bvh_node>(world));

    camera cam;

    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 1080;
    cam.samples_per_pixel = 400;
    cam.max_depth         = 50;
    cam.background        = colour(0.70, 0.80, 1.00);

    cam.vfov     = 30;
    cam.lookfrom = point3(6,5,7);
    cam.lookat   = point3(2,1,0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0;

    std::ofstream file("output.ppm");
    cam.render(world, file);
}

void model_Scene(){
    
    hittable_list world;

    auto difflight = make_shared<diffuse_light>(colour(5, 5, 5));
    world.add(make_shared<sphere>(point3(2,5,2), 2, difflight));

    auto checker = make_shared<checker_texture>(0.32, colour(0.2, 0.2, 0.2), colour(.9, .9, .9));
    world.add(make_shared<sphere>(point3(0,-1000,0), 1000, make_shared<lambertian>(checker)));

    auto legs_albedo_tex    = make_shared<image_texture>("models/Legs_Base_color.png");
    auto legs_metallic_tex  = make_shared<image_texture>("models/Legs_Metallic.png");
    auto legs_roughness_tex = make_shared<image_texture>("models/Legs_Roughness.png");
    auto legs_normal_tex    = make_shared<image_texture>("models/Legs_Normal_OpenGL.png");

    auto chest_albedo_tex    = make_shared<image_texture>("models/Chest_Base_color.png");
    auto chest_metallic_tex  = make_shared<image_texture>("models/Chest_Metallic.png");
    auto chest_roughness_tex = make_shared<image_texture>("models/Chest_Roughness.png");
    auto chest_normal_tex    = make_shared<image_texture>("models/Chest_Normal_OpenGL.png");

    auto arms_albedo_tex    = make_shared<image_texture>("models/Arms_Base_color.png");
    auto arms_metallic_tex  = make_shared<image_texture>("models/Arms_Metallic.png");
    auto arms_roughness_tex = make_shared<image_texture>("models/Arms_Roughness.png");
    auto arms_normal_tex    = make_shared<image_texture>("models/Arms_Normal_OpenGL.png");

    auto helmet_albedo_tex    = make_shared<image_texture>("models/Helmet_Base_color.png");
    auto helmet_metallic_tex  = make_shared<image_texture>("models/Helmet_Metallic.png");
    auto helmet_roughness_tex = make_shared<image_texture>("models/Helmet_Roughness.png");
    auto helmet_normal_tex    = make_shared<image_texture>("models/Helmet_Normal_OpenGL.png");

    auto legs_mat = make_shared<pbr_material>(
        legs_albedo_tex,   
        legs_metallic_tex,
        legs_roughness_tex,
        legs_normal_tex,
        1.0                     
    );

    auto chest_mat = make_shared<pbr_material>(
        chest_albedo_tex,   
        chest_metallic_tex,
        chest_roughness_tex,
        chest_normal_tex,
        1.0                     
    );

    auto arms_mat = make_shared<pbr_material>(
        arms_albedo_tex,   
        arms_metallic_tex,
        arms_roughness_tex,
        arms_normal_tex,
        1.0                     
    );

    auto helmet_mat = make_shared<pbr_material>(
        helmet_albedo_tex,   
        helmet_metallic_tex,
        helmet_roughness_tex,
        helmet_normal_tex,
        1.0                     
    );

    std::unordered_map<std::string, std::shared_ptr<material>> material_map;

    material_map["Legs"]   = legs_mat;
    material_map["Chest"]  = chest_mat;
    material_map["Arms"]   = arms_mat;
    material_map["Helmet"] = helmet_mat;

    auto body_mat = make_shared<lambertian>(colour(0.8, 0.1, 0.1));

    auto default_mat = body_mat;

    std::shared_ptr<hittable_list> mesh_tris;

    try {
        mesh_tris = load_mesh_as_triangles("models/AtlastedMKIV.fbx", material_map, default_mat, true, true, 1.5);
    } catch (const std::exception& e) {
        std::cerr << "Mesh load failed: " << e.what() << "\n";
    }

    auto mesh_bvh = std::make_shared<bvh_node>(
        mesh_tris->objects,
        0,
        mesh_tris->objects.size()
    );

    world.add(make_shared<transform>(
        mesh_bvh,
        vec3(0, 0, 0),   // translation
        vec3(0, 90, 0),      // rotation (deg): +15° about Y
        vec3(1.0, 1.0, 1.0)  // scale
    ));

    camera cam;

    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 1080;
    cam.samples_per_pixel = 150;
    cam.max_depth         = 50;
    cam.background        = colour(0.70, 0.80, 1.00);

    cam.vfov     = 20;
    cam.lookfrom = point3(2, 1.5, 0);
    cam.lookat   = point3(0, 1.3, 0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0;

    std::ofstream file("output.ppm");
    cam.render(world, file);
}

int main() {

    int scene = 9;

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
        case 7: final_scene(1080, 150, 50);
            break;
        case 8: PBR_Scene();
            break;
        case 9: model_Scene();
            break;
    }

    return 0;
    
}



