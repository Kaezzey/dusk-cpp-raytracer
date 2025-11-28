#include "../../include/core/dusktracer.h"          // brings in colour, texture, materials, point3, etc.
#include "../../include/core/scene.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/sphere.h"
#include "../../include/core/materials/material.h"
#include "../../include/core/mesh_loader.h"
#include "../../include/core/BVH.h"
#include "../../include/core/triangle.h"
#include "../../include/core/transform.h"

static void ensure_mesh_loaded(scene_mesh_asset& asset,
                               const scene_material& mat_desc)
{
    if (asset.mesh_bvh) return;

    auto mat = build_rt_material(mat_desc);

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


// Helper: build one concrete material from a scene_material
std::shared_ptr<material> build_rt_material(const scene_material& m)
{
    switch (m.model) {

    case scene_material_model::lambert: {
        std::shared_ptr<texture> base =
            m.base_tex
            ? m.base_tex
            : std::make_shared<solid_colour>(m.base_color);

        return std::make_shared<lambertian>(base);
    }

    case scene_material_model::metal: {
        std::shared_ptr<texture> base =
            m.base_tex
            ? m.base_tex
            : std::make_shared<solid_colour>(m.base_color);

        return std::make_shared<metal>(base, m.fuzz);
    }

    case scene_material_model::dielectric: {
        // assuming your dielectric ctor is (double ior, colour tint)
        return std::make_shared<dielectric>(m.ior, m.base_color);
    }

    case scene_material_model::diffuse_light: {
        std::shared_ptr<texture> emit_tex =
            m.base_tex
            ? m.base_tex
            : std::make_shared<solid_colour>(m.emission);

        return std::make_shared<diffuse_light>(emit_tex);
    }

    case scene_material_model::pbr: {
        // Base albedo
        std::shared_ptr<texture> base =
            m.base_tex
            ? m.base_tex
            : std::make_shared<solid_colour>(m.base_color);

        // Metallic scalar -> greyscale texture if no texture bound
        std::shared_ptr<texture> metallic_tex =
            m.metallic_tex
            ? m.metallic_tex
            : std::make_shared<solid_colour>(colour(m.metallic, m.metallic, m.metallic));

        // Roughness scalar -> greyscale texture if no texture bound
        std::shared_ptr<texture> roughness_tex =
            m.roughness_tex
            ? m.roughness_tex
            : std::make_shared<solid_colour>(colour(m.roughness, m.roughness, m.roughness));

        std::shared_ptr<texture> normal_tex = m.normal_tex; // can be nullptr

        // pbr_material is defined in material.h
        return std::make_shared<pbr_material>(
            base,
            metallic_tex,
            roughness_tex,
            normal_tex,
            m.normal_strength,
            m.dielectric_F0
        );
    }

    case scene_material_model::isotropic:
        // if you actually use isotropic for volumes, wire it however you want
        // simple fallback:
        return std::make_shared<lambertian>(
            std::make_shared<solid_colour>(m.base_color)
        );
    }

    // Fallback if model enum is invalid
    return std::make_shared<lambertian>(
        std::make_shared<solid_colour>(colour(0.5, 0.5, 0.5))
    );
}

hittable_list build_world_from_scene(const scene& scn)
{
    hittable_list world;

    // Cache concrete materials so multiple objects can share them
    std::vector<std::shared_ptr<material>> built_materials(scn.materials.size());

    auto get_material = [&](int idx) -> std::shared_ptr<material> {
        if (idx < 0 || idx >= static_cast<int>(scn.materials.size()))
            return std::make_shared<lambertian>(colour(0.5, 0.5, 0.5));

        if (!built_materials[idx])
            built_materials[idx] = build_rt_material(scn.materials[idx]);

        return built_materials[idx];
    };

    // Build objects
    for (const auto& obj : scn.objects) {
        auto mat = get_material(obj.material_index);

        switch (obj.type) {
        case scene_object_type::sphere:
            world.add(std::make_shared<sphere>(obj.center, obj.radius, mat));
            break;

        case scene_object_type::mesh_instance: {
            if (obj.mesh_index < 0 ||
                obj.mesh_index >= static_cast<int>(scn.meshes.size())) {
                break;
            }

            auto& asset = const_cast<scene_mesh_asset&>(scn.meshes[obj.mesh_index]);

            // Load mesh BVH once (if not already)
            ensure_mesh_loaded(asset, scn.materials[obj.material_index]);
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


