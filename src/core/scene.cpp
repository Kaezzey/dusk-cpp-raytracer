#include "../../include/core/dusktracer.h"          // brings in colour, texture, materials, point3, etc.
#include "../../include/core/scene.h"
#include "../../include/core/hittable_list.h"
#include "../../include/core/sphere.h"
#include "../../include/core/materials/material.h"

// Helper: build one concrete material from a scene_material
static std::shared_ptr<material> build_rt_material(const scene_material& m)
{
    switch (m.model) {

    case scene_material_model::lambert: {
        if (m.base_tex)
            return std::make_shared<lambertian>(m.base_tex);
        return std::make_shared<lambertian>(colour(m.base_color));
    }

    case scene_material_model::metal: {
        if (m.base_tex)
            return std::make_shared<metal>(m.base_tex, m.fuzz);
        return std::make_shared<metal>(colour(m.base_color), m.fuzz);
    }

    case scene_material_model::dielectric: {
        // If base_tex set → textured stained glass
        if (m.base_tex)
            return std::make_shared<dielectric>(m.ior, m.base_tex);

        // If base_color != white → coloured glass
        if (m.base_color.x() != 1.0 || m.base_color.y() != 1.0 || m.base_color.z() != 1.0)
            return std::make_shared<dielectric>(m.ior, colour(m.base_color));

        // Default clear glass
        return std::make_shared<dielectric>(m.ior);
    }

    case scene_material_model::diffuse_light: {
        if (m.base_tex)
            return std::make_shared<diffuse_light>(m.base_tex);
        return std::make_shared<diffuse_light>(colour(m.emission));
    }

    case scene_material_model::isotropic: {
        if (m.base_tex)
            return std::make_shared<isotropic>(m.base_tex);
        return std::make_shared<isotropic>(colour(m.base_color));
    }

    case scene_material_model::pbr: {
        // Choose which pbr_material constructor to use based on which textures exist.
        const bool has_base   = (m.base_tex      != nullptr);
        const bool has_metal  = (m.metallic_tex  != nullptr);
        const bool has_rough  = (m.roughness_tex != nullptr);
        const bool has_normal = (m.normal_tex    != nullptr);

        if (has_base && has_metal && has_rough && has_normal) {
            // (C) Fully textured PBR: base, metallic, roughness, normal
            return std::make_shared<pbr_material>(
                m.base_tex,
                m.metallic_tex,
                m.roughness_tex,
                m.normal_tex,
                m.normal_strength,
                colour(m.dielectric_F0)
            );
        }

        if (has_base && has_normal) {
            // (B) Textured base + scalar metallic/rough + normal map
            return std::make_shared<pbr_material>(
                m.base_tex,
                m.metallic,
                m.roughness,
                m.normal_tex,
                m.normal_strength,
                colour(m.dielectric_F0)
            );
        }

        // (A) Constant base/metal/rough with optional normal map
        return std::make_shared<pbr_material>(
            colour(m.base_color),
            m.metallic,
            m.roughness,
            has_normal ? m.normal_tex : nullptr,
            m.normal_strength,
            colour(m.dielectric_F0)
        );
    }

    default:
        // Fallback: grey lambert
        return std::make_shared<lambertian>(colour(0.5, 0.5, 0.5));
    }
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
        default:
            // ignore unsupported types for now
            break;
        }
    }

    return world;
}
