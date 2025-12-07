#include <unordered_map>
#include <cstring>
#include <cctype>

#include "../../include/core/dusktracer.h"          // colour, texture, materials, point3, etc.
#include "../../include/core/scene.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/sphere.h"
#include "../../include/core/materials/material.h"
#include "../../include/core/mesh_loader.h"
#include "../../include/core/BVH.h"
#include "../../include/core/triangle.h"
#include "../../include/core/transform.h"
#include "../../include/core/quad.h"               // <-- your quad/box implementation

// ------------------------------------------------------------
// Case-insensitive extension check
// ------------------------------------------------------------
static bool has_extension_ci(const std::string& path, const char* ext)
{
    const size_t lenp = path.size();
    const size_t lene = std::strlen(ext);
    if (lenp < lene) return false;
    const size_t off = lenp - lene;
    for (size_t i = 0; i < lene; ++i) {
        char c1 = (char)std::tolower(path[off + i]);
        char c2 = (char)std::tolower(ext[i]);
        if (c1 != c2) return false;
    }
    return true;
}

// ------------------------------------------------------------
// Texture cache: map file path -> shared_ptr<texture>
// ------------------------------------------------------------
static std::shared_ptr<texture> load_scene_texture(const scene& scn, int tex_index)
{
    if (tex_index < 0 || tex_index >= (int)scn.textures.size())
        return nullptr;

    const auto& t = scn.textures[tex_index];

    // Cache by path
    static std::unordered_map<std::string, std::weak_ptr<texture>> tex_cache;

    auto it = tex_cache.find(t.path);
    if (it != tex_cache.end()) {
        if (auto existing = it->second.lock()) {
            return existing;
        }
    }

    std::shared_ptr<texture> tex = std::make_shared<image_texture>(t.path.c_str());
    tex_cache[t.path] = tex;
    return tex;
}

// ------------------------------------------------------------
// Helper: lightweight default materials (shared once)
// ------------------------------------------------------------
static std::shared_ptr<material> get_default_grey_mat()
{
    static std::shared_ptr<material> mat =
        std::make_shared<lambertian>(
            std::make_shared<solid_colour>(colour(0.5, 0.5, 0.5))
        );
    return mat;
}

static std::shared_ptr<material> get_default_magenta_mat()
{
    static std::shared_ptr<material> mat =
        std::make_shared<lambertian>(
            std::make_shared<solid_colour>(colour(1.0, 0.0, 1.0))
        );
    return mat;
}

// ------------------------------------------------------------
// Helper: build one concrete material from a scene_material
// using scene.texture indices
// ------------------------------------------------------------
std::shared_ptr<material> build_rt_material(const scene& scn,
                                            const scene_material& m)
{
    auto make_base_colour = [&](const vec3& c) {
        return std::make_shared<solid_colour>(
            colour(c.x(), c.y(), c.z())
        );
    };

    auto make_grey = [&](double v) {
        return std::make_shared<solid_colour>(
            colour(v, v, v)
        );
    };

    switch (m.model) {

    case scene_material_model::lambert:
    {
        auto tex = load_scene_texture(scn, m.albedo_tex);
        std::shared_ptr<texture> base_tex =
            tex ? tex : make_base_colour(m.base_color);

        return std::make_shared<lambertian>(base_tex);
    }

    case scene_material_model::metal:
    {
        auto tex = load_scene_texture(scn, m.albedo_tex);
        std::shared_ptr<texture> base_tex =
            tex ? tex : make_base_colour(m.base_color);

        return std::make_shared<metal>(base_tex, m.fuzz);
    }

    case scene_material_model::dielectric:
    {
        // assuming your dielectric ctor is (double ior, colour tint)
        return std::make_shared<dielectric>(
            m.ior,
            colour(m.base_color.x(),
                   m.base_color.y(),
                   m.base_color.z())
        );
    }

    case scene_material_model::diffuse_light:
    {
        // Use emission colour if set; otherwise base_color as emissive.
        // Apply emission_intensity as a scalar multiplier so colour + intensity
        // are independently controllable from the editor.
        vec3 base_emit = (m.emission.x() != 0.0 ||
                          m.emission.y() != 0.0 ||
                          m.emission.z() != 0.0)
                         ? m.emission
                         : m.base_color;

        vec3 emit_col = base_emit * (float)m.emission_intensity;

        auto tex = load_scene_texture(scn, m.albedo_tex);
        std::shared_ptr<texture> emit_tex =
            tex ? tex : make_base_colour(emit_col);

        return std::make_shared<diffuse_light>(emit_tex);
    }

    case scene_material_model::pbr:
    {
        // Base albedo
        auto base_tex_loaded = load_scene_texture(scn, m.albedo_tex);
        std::shared_ptr<texture> base_tex =
            base_tex_loaded ? base_tex_loaded : make_base_colour(m.base_color);

        // Metallic scalar -> greyscale texture if no texture bound
        auto metal_tex_loaded = load_scene_texture(scn, m.metallic_tex);
        std::shared_ptr<texture> metallic_tex =
            metal_tex_loaded ? metal_tex_loaded : make_grey(m.metallic);

        // Roughness scalar -> greyscale texture if no texture bound
        auto rough_tex_loaded = load_scene_texture(scn, m.roughness_tex);
        std::shared_ptr<texture> roughness_tex =
            rough_tex_loaded ? rough_tex_loaded : make_grey(m.roughness);

        // Normal map (tangent-space)
        std::shared_ptr<texture> normal_tex =
            load_scene_texture(scn, m.normal_tex); // can be nullptr

        // Alpha map (optional)
        std::shared_ptr<texture> alpha_tex =
            load_scene_texture(scn, m.alpha_tex); // can be nullptr

        // Force double-sided masking if either an explicit alpha map is
        // provided or the albedo texture contains an alpha channel.
        bool effective_double_sided = m.alpha_double_sided;
        if (alpha_tex) effective_double_sided = true;
        else {
            // If base_tex is an image_texture, query whether it has alpha.
            if (auto img_tex = std::dynamic_pointer_cast<image_texture>(base_tex)) {
                if (img_tex->has_alpha()) effective_double_sided = true;
            }
        }

        return std::make_shared<pbr_material>(
            base_tex,
            metallic_tex,
            roughness_tex,
            normal_tex,
            m.normal_strength,
            colour(m.dielectric_F0.x(),
                   m.dielectric_F0.y(),
                   m.dielectric_F0.z()),
            alpha_tex,
            effective_double_sided,
            m.alpha_cutoff
        );
    }

    case scene_material_model::isotropic:
        // If you actually use isotropic for volumes, wire it properly.
        // For now, simple diffuse fallback:
        return std::make_shared<lambertian>(make_base_colour(m.base_color));
    }

    // Fallback if model enum is invalid
    return get_default_grey_mat();
}

// ------------------------------------------------------------
// Convert editor scene into runtime hittables for the renderer.
// Uses per-slot mesh materials via mesh_loader's multi-material
// overload.
// ------------------------------------------------------------
hittable_list build_world_from_scene(const scene& scn)
{
    hittable_list world;

    // Rough reservation to avoid repeated reallocs:
    // spheres + cubes + each mesh instance will at least add one transform.
    world.objects.reserve(scn.objects.size() * 2);

    // Cache concrete materials so multiple objects can share them
    std::vector<std::shared_ptr<material>> built_materials(scn.materials.size());

    auto get_material = [&](int idx) -> std::shared_ptr<material> {
        if (idx < 0 || idx >= static_cast<int>(scn.materials.size()))
            return get_default_grey_mat();

        if (!built_materials[idx])
            built_materials[idx] = build_rt_material(scn, scn.materials[idx]);

        return built_materials[idx];
    };

    // --------------------------------------------------------
    // 1) Simple primitives: spheres + cubes
    // --------------------------------------------------------
    for (const auto& obj : scn.objects) {

        if (obj.type == scene_object_type::sphere) {
            auto mat = get_material(obj.material_index);

            // Create unit sphere at origin and apply editor transforms via transform wrapper.
            // Use uniform scale = obj.scale.x * obj.radius (obj.scale.x used as uniform multiplier)
            double s = obj.scale.x();
            if (s <= 0.0) s = 1.0;
            s *= obj.radius;

            vec3 translation = obj.translation + vec3(obj.center.x(), obj.center.y(), obj.center.z());

            auto unit_sphere = std::make_shared<sphere>(point3(0,0,0), 1.0, mat);

            auto inst = std::make_shared<transform>(
                unit_sphere,
                translation,
                obj.rotation_deg,
                vec3(s, s, s)
            );

            world.add(inst);
        }
        else if (obj.type == scene_object_type::cube) {
            auto mat = get_material(obj.material_index);

            // Build a unit cube as a box from [-0.5, -0.5, -0.5] to [0.5, 0.5, 0.5]
            auto unit_cube = box(
                point3(-0.5, -0.5, -0.5),
                point3( 0.5,  0.5,  0.5),
                mat
            );

            // Use scale.x as uniform scale (if <=0, clamp to 1)
            vec3 scl = obj.scale;
            if (scl.x() <= 0.0) scl = vec3(1.0, scl.y(), scl.z());
            if (scl.y() <= 0.0) scl = vec3(scl.x(), 1.0, scl.z());
            if (scl.z() <= 0.0) scl = vec3(scl.x(), scl.y(), 1.0);

            // Combine center + translation for instance transform
            vec3 translation = obj.translation + vec3(obj.center.x(), obj.center.y(), obj.center.z());

            auto inst = std::make_shared<transform>(
                unit_cube,
                translation,
                obj.rotation_deg,
                scl
            );

            world.add(inst);
        }
    }

    // --------------------------------------------------------
    // 2) Mesh instances with per-slot materials (Legs/Chest/…)
    // --------------------------------------------------------
    for (const auto& obj : scn.objects) {
        if (obj.type != scene_object_type::mesh_instance)
            continue;

        if (obj.mesh_index < 0 ||
            obj.mesh_index >= static_cast<int>(scn.meshes.size()))
            continue;

        const scene_mesh_asset& asset = scn.meshes[obj.mesh_index];

        // Build mapping: FBX material name ("Legs", "Chest", ... )
        // -> runtime material.
        std::unordered_map<std::string, std::shared_ptr<material>> mat_table;
        mat_table.reserve(asset.slot_names.size());

        size_t num_slots = asset.slot_names.size();
        if (obj.mesh_slot_materials.size() < num_slots)
            num_slots = obj.mesh_slot_materials.size();

        for (size_t s = 0; s < num_slots; ++s) {
            int scene_mat_idx = obj.mesh_slot_materials[s];
            if (scene_mat_idx < 0 ||
                scene_mat_idx >= static_cast<int>(scn.materials.size()))
                continue;

            const std::string& fbx_mat_name = asset.slot_names[s];

            mat_table[fbx_mat_name] = get_material(scene_mat_idx);
        }

        // Default fallback material for any FBX material name not in the table.
        std::shared_ptr<material> default_mat;
        if (obj.material_index >= 0 &&
            obj.material_index < static_cast<int>(scn.materials.size())) {
            default_mat = get_material(obj.material_index);
        } else {
            default_mat = get_default_magenta_mat(); // magenta debug
        }

        const bool z_up = has_extension_ci(asset.file_path, ".fbx");

        // Multi-material mesh load
        std::shared_ptr<hittable_list> tri_list =
            load_mesh_as_triangles(
                asset.file_path,
                mat_table,
                default_mat,
                z_up,
                /*normalise_to_unit=*/true,
                /*user_scale=*/2.0
            );

        if (!tri_list || tri_list->objects.empty())
            continue;

        // Build an acceleration structure for this instance's triangles.
        // Prefer `triangle_mesh`, which will use an Embree-backed accel when
        // compiled with Embree support (HAVE_EMBREE). If Embree is not
        // available, `triangle_mesh` falls back to a BVH.
        auto tri_mesh = std::make_shared<triangle_mesh>(tri_list->objects);

        // Apply instance transform (translation, rotation_deg, uniform scale)
        vec3 scl = obj.scale;
            if (scl.x() <= 0.0) scl = vec3(1.0, scl.y(), scl.z());
            if (scl.y() <= 0.0) scl = vec3(scl.x(), 1.0, scl.z());
            if (scl.z() <= 0.0) scl = vec3(scl.x(), scl.y(), 1.0);

            vec3 translation = obj.translation + vec3(obj.center.x(), obj.center.y(), obj.center.z());

            auto inst = std::make_shared<transform>(
                tri_mesh,
                translation,
                obj.rotation_deg,  // degrees
                scl
            );

        world.add(inst);
    }

    // If there are no objects, just return the empty world.
    if (world.objects.empty()) return world;

    // Wrap the scene objects in a top-level BVH to accelerate ray traversal.
    // We return a hittable_list containing a single BVH node so the rest of
    // the renderer (which expects a hittable_list) can remain unchanged.
    auto top_bvh = std::make_shared<bvh_node>(world.objects, 0, world.objects.size());
    hittable_list wrapped(top_bvh);
    return wrapped;
}
