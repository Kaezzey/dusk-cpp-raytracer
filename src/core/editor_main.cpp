// editor_main.cpp

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

// -----------------------------------------------------------------------------
// Scene setup helpers
// -----------------------------------------------------------------------------

// Build your default test scene: 4 spheres + 1 mesh asset + 1 mesh instance
static void build_default_test_scene(editor_scene& escn) {
    scene& scn = escn.runtime;

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
    scn.materials.back().name            = "PBR Metal";
    scn.materials.back().model           = scene_material_model::pbr;
    scn.materials.back().base_color      = colour(0.8, 0.6, 0.2);
    scn.materials.back().metallic        = 1.0;
    scn.materials.back().roughness       = 0.2;
    scn.materials.back().normal_strength = 1.0;
    scn.materials.back().dielectric_F0   = colour(0.04, 0.04, 0.04);

    // -------------------------
    // Spheres (objects)
    // -------------------------

    scn.objects.push_back({
        "GroundSphere",
        scene_object_type::sphere,
        ground_mat,
        point3(0, -100.5, -1),
        100.0,
        -1,                    // mesh_index (unused for sphere)
        vec3(0,0,0),
        vec3(0,0,0),
        vec3(1,1,1)
    });

    scn.objects.push_back({
        "CenterSphere",
        scene_object_type::sphere,
        center_mat,
        point3(0, 0, -1),
        0.5,
        -1,
        vec3(0,0,0),
        vec3(0,0,0),
        vec3(1,1,1)
    });

    scn.objects.push_back({
        "GlassSphere",
        scene_object_type::sphere,
        glass_mat,
        point3(-1, 0, -1),
        0.5,
        -1,
        vec3(0,0,0),
        vec3(0,0,0),
        vec3(1,1,1)
    });

    scn.objects.push_back({
        "PBRMetalSphere",
        scene_object_type::sphere,
        pbr_metal_mat,
        point3(1, 0, -1),
        0.5,
        -1,
        vec3(0,0,0),
        vec3(0,0,0),
        vec3(1,1,1)
    });

    // -------------------------
    // Mesh asset (FBX Spartan)
    // -------------------------
    {
        scene_mesh_asset mesh_asset;
        mesh_asset.name      = "AtlasMKIV";
        mesh_asset.file_path = "models/AtlastedMKIV.fbx";  // must exist relative to exe

        int mesh_index = (int)scn.meshes.size();
        scn.meshes.push_back(std::move(mesh_asset));

        // -------------------------
        // Mesh instance object
        // -------------------------
        scene_object mesh_obj;
        mesh_obj.name           = "AtlasInstance";
        mesh_obj.type           = scene_object_type::mesh_instance;
        mesh_obj.material_index = pbr_metal_mat;   // reuse PBR metal material

        // sphere fields unused for mesh
        mesh_obj.center = point3(0, 0, 0);
        mesh_obj.radius = 1.0;

        mesh_obj.mesh_index    = mesh_index;
        mesh_obj.translation   = vec3(0.0, 0.0, -3.0);  // in front of camera (logical)
        mesh_obj.rotation_eul  = vec3(0.0, 0.0, 0.0);   // radians
        mesh_obj.scale         = vec3(1.0, 1.0, 1.0);

        scn.objects.push_back(std::move(mesh_obj));
    }

    // -------------------------
    // Optional: one directional light placeholder (data only)
    // -------------------------
    {
        scene_light sun;
        sun.name      = "Sun";
        sun.type      = scene_light_type::directional;
        sun.radiance  = vec3(3.0, 3.0, 3.0);         // bright white-ish
        sun.direction = unit_vector(vec3(-1.0, -1.0, -0.5));
        scn.lights.push_back(std::move(sun));
    }
}

static void reset_editor_camera(editor_scene& escn) {
    editor_camera_state& ecs = escn.camera_state;

    // Match your old camera pose approximately:
    // old: lookfrom = (3, 3, 2), lookat = (0, 0, -1)
    ecs.position = point3(3, 3, 2);
    ecs.vfov     = 40.0;

    point3 target  = point3(0, 0, -1);
    vec3   forward = unit_vector(target - ecs.position);

    ecs.yaw   = std::atan2(forward.z(), forward.x());
    ecs.pitch = std::asin(forward.y());
    ecs.update_basis();
}

static void update_render_camera_from_editor(const editor_scene& escn, camera& cam) {
    // Bridge editor camera state -> render camera
    to_shirley_camera(escn.camera_state, cam);
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main() {
    editor_scene escn;
    build_default_test_scene(escn);

    // Build hittable world from editor scene runtime (spheres + mesh instance)
    hittable_list world = build_world_from_editor(escn);

    // Editor camera
    reset_editor_camera(escn);

    // Render camera (resolution / samples / depth)
    camera cam;
    cam.aspect_ratio      = 16.0 / 9.0;
    cam.image_width       = 400;
    cam.samples_per_pixel = 50;
    cam.max_depth         = 50;
    cam.background        = colour(0.70, 0.80, 1.00);

    update_render_camera_from_editor(escn, cam);

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
    // - moves mouse right a bit (look right)
    // - moves mouse up slightly (look up)
    // - presses W for 1 second (forward)
    editor_camera_state& ecs = escn.camera_state;

    ecs.look(-10.0, +40.0);        // mouse delta in pixels (tune as needed)

    double dt = 1.0;               // 1 second of "held down"
    double move_forward_axis = +1.0; // W
    double move_right_axis   = 0.0;
    double move_up_axis      = 0.0;

    ecs.move_from_input(move_forward_axis,
                        move_right_axis,
                        move_up_axis,
                        dt);

    update_render_camera_from_editor(escn, cam);

    cancel_flag.store(false);
    render_result img1 = r.render(world, cam, &cancel_flag);
    write_ppm("phase2_cam1.ppm", img1);

    return 0;
}
