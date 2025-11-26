#include <fstream>
#include <atomic>

#include "../../include/core/camera.h"
#include "../../include/core/renderer.h"
#include "../../include/core/scene.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/image_io.h"

int main() {
    scene scn;

    // -------------------------
    // Materials
    // -------------------------

    // Ground: lambert
    int ground_mat = (int)scn.materials.size();
    scn.materials.push_back({});
    scn.materials.back().name       = "Ground";
    scn.materials.back().model      = scene_material_model::lambert;
    scn.materials.back().base_color = colour(0.8, 0.8, 0.0);

    // Center: lambert
    int center_mat = (int)scn.materials.size();
    scn.materials.push_back({});
    scn.materials.back().name       = "Center";
    scn.materials.back().model      = scene_material_model::lambert;
    scn.materials.back().base_color = colour(0.1, 0.2, 0.5);

    // Left: dielectric glass
    int glass_mat = (int)scn.materials.size();
    scn.materials.push_back({});
    scn.materials.back().name   = "Glass";
    scn.materials.back().model  = scene_material_model::dielectric;
    scn.materials.back().ior    = 1.5;
    scn.materials.back().base_color = colour(1.0, 1.0, 1.0); // clear

    // Right: PBR metal ball (scalar params)
    int pbr_metal_mat = (int)scn.materials.size();
    scn.materials.push_back({});
    scn.materials.back().name          = "PBR Metal";
    scn.materials.back().model         = scene_material_model::pbr;
    scn.materials.back().base_color    = colour(0.8, 0.6, 0.2);
    scn.materials.back().metallic      = 1.0;   // full metal
    scn.materials.back().roughness     = 0.2;   // somewhat shiny
    scn.materials.back().normal_strength = 1.0; // no normal map yet
    scn.materials.back().dielectric_F0 = colour(0.04, 0.04, 0.04);

    // -------------------------
    // Objects (spheres)
    // -------------------------

    scn.objects.push_back({
        "GroundSphere",
        scene_object_type::sphere,
        ground_mat,
        point3(0, -100.5, -1),
        100.0
    });

    scn.objects.push_back({
        "CenterSphere",
        scene_object_type::sphere,
        center_mat,
        point3(0, 0, -1),
        0.5
    });

    scn.objects.push_back({
        "GlassSphere",
        scene_object_type::sphere,
        glass_mat,
        point3(-1, 0, -1),
        0.5
    });

    scn.objects.push_back({
        "PBRMetalSphere",
        scene_object_type::sphere,
        pbr_metal_mat,
        point3(1, 0, -1),
        0.5
    });

    // -------------------------
    // Build world + camera
    // -------------------------

    hittable_list world = build_world_from_scene(scn);

    camera cam;
    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 400;
    cam.samples_per_pixel = 50;
    cam.max_depth         = 50;
    cam.vfov              = 40;
    cam.lookfrom          = point3(3, 3, 2);
    cam.lookat            = point3(0, 0, -1);

    renderer r;
    std::atomic<bool> cancel_flag{false};

    render_result img = r.render(world, cam, &cancel_flag);

    write_ppm("phase2.ppm", img);

    return 0;
}
