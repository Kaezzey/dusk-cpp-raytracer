#include <fstream>
#include <atomic>
#include <cmath>

#include "../../include/core/camera.h"
#include "../../include/core/renderer.h"
#include "../../include/core/scene.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/image_io.h"
#include "../../include/core/editor_camera.h"
#include "../../include/core/editor_scene.h"

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
    scn.materials.back().name       = "Glass";
    scn.materials.back().model      = scene_material_model::dielectric;
    scn.materials.back().ior        = 1.5;
    scn.materials.back().base_color = colour(1.0, 1.0, 1.0); // clear

    // Right: PBR metal ball (scalar params)
    int pbr_metal_mat = (int)scn.materials.size();
    scn.materials.push_back({});
    scn.materials.back().name           = "PBR Metal";
    scn.materials.back().model          = scene_material_model::pbr;
    scn.materials.back().base_color     = colour(0.8, 0.6, 0.2);
    scn.materials.back().metallic       = 1.0;
    scn.materials.back().roughness      = 0.2;
    scn.materials.back().normal_strength = 1.0;
    scn.materials.back().dielectric_F0  = colour(0.04, 0.04, 0.04);

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
    // Build world
    // -------------------------

    hittable_list world = build_world_from_scene(scn);

    // -------------------------
    // Editor camera (Phase 2)
    // -------------------------

    editor_camera_state ecs;

    // Match your old camera pose approximately:
    // old: lookfrom = (3, 3, 2), lookat = (0, 0, -1)
    ecs.position = point3(3, 3, 2);
    ecs.vfov     = 40.0;

    {
        vec3 target  = point3(0, 0, -1);
        vec3 forward = unit_vector(target - ecs.position);

        // Recover yaw/pitch from the forward vector
        ecs.yaw   = std::atan2(forward.z(), forward.x());
        ecs.pitch = std::asin(forward.y());
        ecs.update_basis();
    }

    // Render camera: holds resolution / samples / depth etc.
    camera cam;
    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 400;
    cam.samples_per_pixel = 50;
    cam.max_depth         = 50;
    cam.background        = colour(0.70, 0.80, 1.00);

    // Copy pose from editor camera into render camera
    to_shirley_camera(ecs, cam);

    renderer r;
    std::atomic<bool> cancel_flag{false};

    // -------------------------
    // Baseline: initial pose
    // -------------------------

    render_result img0 = r.render(world, cam, &cancel_flag);
    write_ppm("phase2_cam0.ppm", img0);

    // -------------------------
    // Simulate some editor input
    // -------------------------
    // Pretend the user:
    //  - moves mouse right a bit (look right)
    //  - moves mouse up slightly (look up)
    //  - presses W and A for 1 second

    ecs.look(-10.0, +40.00);  // pixels of mouse delta, tweak as needed

    double dt = 1.0;  // 1 second of "held down"
    double move_forward_axis = +1.0;  // W
    double move_right_axis   = +0.0;  // A
    double move_up_axis      = +0.0;   // no vertical move

    ecs.move_from_input(move_forward_axis, move_right_axis, move_up_axis, dt);

    // Update render camera with new pose
    to_shirley_camera(ecs, cam);
    cancel_flag.store(false);

    render_result img1 = r.render(world, cam, &cancel_flag);
    write_ppm("phase2_cam1.ppm", img1);

    return 0;
}
