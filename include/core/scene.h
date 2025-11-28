#ifndef DUSK_SCENE_H
#define DUSK_SCENE_H

#include <string>
#include <vector>
#include <memory>

#include "vec3.h"    // for vec3 / point3

// Forward declarations
class texture;
class material;
class hittable;
class hittable_list;

// ----------------------------------------
// Material “model” types the editor can pick
// ----------------------------------------
enum class scene_material_model {
    lambert,
    metal,
    dielectric,
    diffuse_light,
    isotropic,
    pbr          // uses your pbr_material class
};

// ----------------------------------------
// Editor-facing material description
// ----------------------------------------
struct scene_material {
    std::string          name;
    scene_material_model model = scene_material_model::lambert;

    // --- Shared scalar parameters ---
    vec3   base_color       = vec3(0.8, 0.8, 0.8);    // lambert/metal/pbr/etc.
    double metallic         = 0.0;                    // [0,1] PBR
    double roughness        = 0.5;                    // [0,1] PBR
    double fuzz             = 0.0;                    // metal fuzz
    double ior              = 1.5;                    // dielectric IOR
    vec3   emission         = vec3(0, 0, 0);          // diffuse_light color
    vec3   dielectric_F0    = vec3(0.04, 0.04, 0.04); // PBR dielectric F0
    double normal_strength  = 1.0;                    // PBR normal strength

    // --- Textures (editor fills these) ---
    // If nullptr, fall back to scalar params.
    std::shared_ptr<texture> base_tex      = nullptr;
    std::shared_ptr<texture> metallic_tex  = nullptr;  // greyscale [0,1]
    std::shared_ptr<texture> roughness_tex = nullptr;  // greyscale [0,1]
    std::shared_ptr<texture> normal_tex    = nullptr;  // tangent-space normal
};

// Convert an editor-facing scene_material into a runtime material*
std::shared_ptr<material> build_rt_material(const scene_material& desc);

// ----------------------------------------
// Geometry types
// ----------------------------------------
enum class scene_object_type {
    sphere,
    mesh_instance,   // mesh placed in the world with a transform
};

// ----------------------------------------
// Editor-facing object description
// ----------------------------------------
struct scene_object {
    std::string       name;
    scene_object_type type = scene_object_type::sphere;

    int material_index = -1;   // index into scene.materials

    // Sphere params (used when type == sphere)
    point3 center = point3(0,0,0);
    double radius = 0.5;

    // Mesh instance params (used when type == mesh_instance)
    int  mesh_index   = -1;           // index into scene.meshes
    vec3 translation  = vec3(0,0,0);  // world-space translation
    vec3 rotation_deg = vec3(0,0,0);  // degrees, XYZ order
    vec3 scale        = vec3(1,1,1);  // non-uniform scale allowed
};

// ----------------------------------------
// Mesh asset: result of importing an FBX/OBJ
// ----------------------------------------
// This is *not* an instance; it's the shared geometry data.
struct scene_mesh_asset {
    std::string name;
    std::string file_path;

    // Baked geometry for this mesh (BVH over triangles)
    std::shared_ptr<hittable> mesh_bvh;
};

// ----------------------------------------
// Lights
// ----------------------------------------
enum class scene_light_type {
    directional,
    point,
};

struct scene_light {
    std::string      name;
    scene_light_type type = scene_light_type::directional;

    // Radiance / intensity * colour
    vec3   radiance  = vec3(1.0, 1.0, 1.0);

    // Directional light: direction *from light toward scene*, normalized
    vec3   direction = vec3(-1, -1, -1);

    // Point light: world position + simple range
    point3 position  = point3(0, 5, 0);
    double range     = 10.0;   // falloff / influence radius
};

// ----------------------------------------
// Whole scene: what the editor owns
// ----------------------------------------
struct scene {
    std::vector<scene_material>   materials;
    std::vector<scene_object>     objects;
    std::vector<scene_mesh_asset> meshes;   // mesh assets (FBX/OBJ etc.)
    std::vector<scene_light>      lights;   // lights in the scene
};

// Convert editor scene into runtime hittables for the renderer.
hittable_list build_world_from_scene(const scene& scn);

#endif // DUSK_SCENE_H
