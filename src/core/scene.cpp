#include <unordered_map>

#include "../../include/core/dusktracer.h"          // brings in colour, texture, materials, point3, etc.
#include "../../include/core/scene.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/sphere.h"
#include "../../include/core/materials/material.h"
#include "../../include/core/mesh_loader.h"
#include "../../include/core/BVH.h"
#include "../../include/core/triangle.h"
#include "../../include/core/transform.h"

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

    // NOTE: adjust this ctor if your image_texture uses (const char*) or something else
    std::shared_ptr<texture> tex = std::make_shared<image_texture>(t.path.c_str());
    tex_cache[t.path] = tex;
    return tex;
}

// ------------------------------------------------------------
// Helper: build one concrete material from a scene_material
// using scene.texture indices
// ------------------------------------------------------------
std::shared_ptr<material> build_rt_material(const scene& scn,
                                            const scene_material& m)
{
    auto make_base_colour = [&](const vec3& c) {
        return std::make_shared<solid_colour>(colour(c.x(), c.y(), c.z()));
    };

    auto make_grey = [&](double v) {
        return std::make_shared<solid_colour>(colour(v, v, v));
    };

    switch (m.model) {

    case scene_material_model::lambert:
    {
        std::shared_ptr<texture> base_tex =
            load_scene_texture(scn, m.albedo_tex)
                ? load_scene_texture(scn, m.albedo_tex)
                : make_base_colour(m.base_color);

        return std::make_shared<lambertian>(base_tex);
    }

    case scene_material_model::metal:
    {
        std::shared_ptr<texture> base_tex =
            load_scene_texture(scn, m.albedo_tex)
                ? load_scene_texture(scn, m.albedo_tex)
                : make_base_colour(m.base_color);

        // Metal in Shirley usually uses a fuzz scalar
        return std::make_shared<metal>(base_tex, m.fuzz);
    }

    case scene_material_model::dielectric:
    {
        // assuming your dielectric ctor is (double ior, colour tint)
        return std::make_shared<dielectric>(m.ior, colour(
            m.base_color.x(), m.base_color.y(), m.base_color.z()
        ));
    }

    case scene_material_model::diffuse_light:
    {
        // Use emission if set; otherwise base_color as emissive
        vec3 emit_col = (m.emission.x() != 0.0 ||
                         m.emission.y() != 0.0 ||
                         m.emission.z() != 0.0)
                        ? m.emission
                        : m.base_color;

        std::shared_ptr<texture> emit_tex =
            load_scene_texture(scn, m.albedo_tex)
                ? load_scene_texture(scn, m.albedo_tex)
                : make_base_colour(emit_col);

        return std::make_shared<diffuse_light>(emit_tex);
    }

    case scene_material_model::pbr:
    {
        // Base albedo
        std::shared_ptr<texture> base_tex =
            load_scene_texture(scn, m.albedo_tex)
                ? load_scene_texture(scn, m.albedo_tex)
                : make_base_colour(m.base_color);

        // Metallic scalar -> greyscale texture if no texture bound
        std::shared_ptr<texture> metallic_tex =
            load_scene_texture(scn, m.metallic_tex)
                ? load_scene_texture(scn, m.metallic_tex)
                : make_grey(m.metallic);

        // Roughness scalar -> greyscale texture if no texture bound
        std::shared_ptr<texture> roughness_tex =
            load_scene_texture(scn, m.roughness_tex)
                ? load_scene_texture(scn, m.roughness_tex)
                : make_grey(m.roughness);

        // Normal map (tangent-space)
        std::shared_ptr<texture> normal_tex =
            load_scene_texture(scn, m.normal_tex);

        return std::make_shared<pbr_material>(
            base_tex,
            metallic_tex,
            roughness_tex,
            normal_tex,              // can be nullptr
            m.normal_strength,
            colour(m.dielectric_F0.x(),
                   m.dielectric_F0.y(),
                   m.dielectric_F0.z())
        );
    }

    case scene_material_model::isotropic:
        // If you actually use isotropic for volumes, wire it properly.
        // For now, simple diffuse fallback:
        return std::make_shared<lambertian>(make_base_colour(m.base_color));
    }

    // Fallback if model enum is invalid
    return std::make_shared<lambertian>(make_base_colour(vec3(0.5, 0.5, 0.5)));
}

// ------------------------------------------------------------
// Mesh loading helper
// ------------------------------------------------------------
static void ensure_mesh_loaded(const scene& scn,
                               scene_mesh_asset& asset,
                               const scene_material& mat_desc)
{
    if (asset.mesh_bvh) return;

    auto mat = build_rt_material(scn, mat_desc);

    auto tri_list = load_mesh_as_triangles(
        asset.file_path,
        mat,
        /*z_up*/ true,
        /*normalise_to_unit*/ true,
        /*user_scale*/ 2.0
    );

    if (!tri_list || tri_list->objects.empty()) {
        asset.mesh_bvh.reset();
        return;
    }

    asset.mesh_bvh = std::make_shared<bvh_node>(
        tri_list->objects, 0, tri_list->objects.size()
    );
}

// ------------------------------------------------------------
// Convert editor scene into runtime hittables for the renderer.
// ------------------------------------------------------------
hittable_list build_world_from_scene(const scene& scn)
{
    hittable_list world;

    // Cache concrete materials so multiple objects can share them
    std::vector<std::shared_ptr<material>> built_materials(scn.materials.size());

    auto get_material = [&](int idx) -> std::shared_ptr<material> {
        if (idx < 0 || idx >= static_cast<int>(scn.materials.size()))
            return std::make_shared<lambertian>(
                std::make_shared<solid_colour>(colour(0.5, 0.5, 0.5))
            );

        if (!built_materials[idx])
            built_materials[idx] = build_rt_material(scn, scn.materials[idx]);

        return built_materials[idx];
    };

    // Build objects
    for (const auto& obj : scn.objects) {
        auto mat = get_material(obj.material_index);

        switch (obj.type) {
        case scene_object_type::sphere:
            world.add(std::make_shared<sphere>(obj.center, obj.radius, mat));
            break;

        case scene_object_type::mesh_instance:
        {
            if (obj.mesh_index < 0 ||
                obj.mesh_index >= static_cast<int>(scn.meshes.size())) {
                break;
            }

            auto& asset = const_cast<scene_mesh_asset&>(scn.meshes[obj.mesh_index]);

            // Load mesh BVH once (if not already)
            if (obj.material_index >= 0 &&
                obj.material_index < (int)scn.materials.size()) {
                ensure_mesh_loaded(scn, asset, scn.materials[obj.material_index]);
            } else {
                // Fallback: use some default material if needed
                scene_material dummy = {};
                dummy.model      = scene_material_model::lambert;
                dummy.base_color = vec3(0.7, 0.2, 0.2);
                ensure_mesh_loaded(scn, asset, dummy);
            }

            if (!asset.mesh_bvh)
                break;

            // Use translation, rotation_deg (degrees), and UNIFORM scale from x
            double s = obj.scale.x();
            if (s <= 0.0) s = 1.0;

            auto inst = std::make_shared<transform>(
                asset.mesh_bvh,
                obj.translation,
                obj.rotation_deg,  // degrees
                s
            );

            world.add(inst);
            break;
        }

        default:
            break;
        }
    }

    return world;
}
