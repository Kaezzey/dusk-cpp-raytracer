#include <unordered_map>
#include <cstring>
#include <cctype>

#include "../../include/core/dusktracer.h"          // colour, texture, materials, point3, etc.
#include "../../include/core/scene.h"
#include "../../include/core/camera.h"              // for camera::emissive_surface
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
// Texture cache: map file path + sample-space -> shared_ptr<texture>
// ------------------------------------------------------------
static std::shared_ptr<texture> load_scene_texture(const scene& scn, int tex_index,
                                                   texture_sample_space sample_space = texture_sample_space::srgb_color)
{
    if (tex_index < 0 || tex_index >= (int)scn.textures.size())
        return nullptr;

    const auto& t = scn.textures[tex_index];

    // Cache by path
    static std::unordered_map<std::string, std::weak_ptr<texture>> tex_cache;

    std::string cache_key = t.path + ((sample_space == texture_sample_space::srgb_color) ? "|srgb" : "|linear");

    auto it = tex_cache.find(cache_key);
    if (it != tex_cache.end()) {
        if (auto existing = it->second.lock()) {
            return existing;
        }
    }

    std::shared_ptr<texture> tex = std::make_shared<image_texture>(t.path.c_str(), sample_space);
    tex_cache[cache_key] = tex;
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
        auto tex = load_scene_texture(scn, m.albedo_tex, texture_sample_space::srgb_color);
        std::shared_ptr<texture> base_tex =
            tex ? tex : make_base_colour(m.base_color);

        return std::make_shared<lambertian>(base_tex);
    }

    case scene_material_model::metal:
    {
        auto tex = load_scene_texture(scn, m.albedo_tex, texture_sample_space::srgb_color);
        std::shared_ptr<texture> base_tex =
            tex ? tex : make_base_colour(m.base_color);

        return std::make_shared<metal>(base_tex, m.fuzz);
    }

    case scene_material_model::dielectric:
    {
        auto tex = load_scene_texture(scn, m.albedo_tex, texture_sample_space::srgb_color);
        if (tex) {
            return std::make_shared<dielectric>(m.ior, tex);
        }
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

        auto tex = load_scene_texture(scn, m.albedo_tex, texture_sample_space::srgb_color);
        std::shared_ptr<texture> emit_tex =
            tex ? tex : make_base_colour(emit_col);

        return std::make_shared<diffuse_light>(emit_tex);
    }

    case scene_material_model::pbr:
    {
        // Base albedo
        auto base_tex_loaded = load_scene_texture(scn, m.albedo_tex, texture_sample_space::srgb_color);
        std::shared_ptr<texture> base_tex =
            base_tex_loaded ? base_tex_loaded : make_base_colour(m.base_color);

        // Metallic scalar -> greyscale texture if no texture bound
        auto metal_tex_loaded = load_scene_texture(scn, m.metallic_tex, texture_sample_space::linear_data);
        std::shared_ptr<texture> metallic_tex =
            metal_tex_loaded ? metal_tex_loaded : make_grey(m.metallic);

        // Roughness scalar -> greyscale texture if no texture bound
        auto rough_tex_loaded = load_scene_texture(scn, m.roughness_tex, texture_sample_space::linear_data);
        std::shared_ptr<texture> roughness_tex =
            rough_tex_loaded ? rough_tex_loaded : make_grey(m.roughness);

        // Normal map (tangent-space)
        std::shared_ptr<texture> normal_tex =
            load_scene_texture(scn, m.normal_tex, texture_sample_space::linear_data); // can be nullptr

        // Alpha map (optional)
        std::shared_ptr<texture> alpha_tex =
            load_scene_texture(scn, m.alpha_tex, texture_sample_space::linear_data); // can be nullptr

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

        auto mat = std::make_shared<pbr_material>(
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

        // Propagate subsurface scattering (SSS) settings from editor material
        // Only apply SSS if use_sss is enabled
        mat->sss_strength = m.use_sss ? m.sss_strength : 0.0;
        mat->sss_scale = m.sss_scale;
        // Extended dipole/diffusion params
        mat->sss_model = static_cast<SSSModel>(m.sss_model);
        mat->sss_samples = m.sss_samples;
        mat->sss_radius = m.sss_radius;
        mat->sss_eta = m.sss_eta;
        mat->sss_color_override = m.sss_color_override_enabled;
        mat->sss_color_override_col = colour(
            (float)m.sss_color_override_color.x(),
            (float)m.sss_color_override_color.y(),
            (float)m.sss_color_override_color.z()
        );
        // Unreal PBR packing flag
        mat->use_unreal_pbr = m.unreal_pbr;

        return mat;
    }

    case scene_material_model::isotropic:
    {
        auto tex = load_scene_texture(scn, m.albedo_tex, texture_sample_space::srgb_color);
        std::shared_ptr<texture> base_tex =
            tex ? tex : make_base_colour(m.base_color);
        return std::make_shared<isotropic>(base_tex);
    }
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

        // Diagnostic log: print mesh instance / asset info so we can debug
        // cases where raster shows the mesh but the raytracer skips it.
        std::printf("[scene] Loading mesh_instance '%s' -> mesh_index=%d, file='%s'\n",
                    obj.name.c_str(), obj.mesh_index, asset.file_path.c_str());

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

        if (!tri_list) {
            std::fprintf(stderr, "[scene] load_mesh_as_triangles returned null for '%s'\n", asset.file_path.c_str());
        }

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

// ------------------------------------------------------------
// Collect emissive surfaces from the scene
// ------------------------------------------------------------
void build_emissive_surfaces(const scene& scn, camera& cam)
{
    std::vector<camera::emissive_surface> surfaces;

    for (const auto& obj : scn.objects) {
        int mat_idx = obj.material_index;
        if (mat_idx < 0 || mat_idx >= (int)scn.materials.size())
            continue;

        const auto& mat = scn.materials[mat_idx];
        if (mat.model != scene_material_model::diffuse_light)
            continue;

        // Compute emission (use emission field or base_color as fallback)
        vec3 base_emit = (mat.emission.x() != 0.0 ||
                          mat.emission.y() != 0.0 ||
                          mat.emission.z() != 0.0)
                         ? mat.emission
                         : mat.base_color;

        vec3 emit_col = base_emit * (float)mat.emission_intensity;
        colour emission(emit_col.x(), emit_col.y(), emit_col.z());

        // Skip if emission is essentially zero
        double lum = 0.2126 * emission.x() + 0.7152 * emission.y() + 0.0722 * emission.z();
        if (lum < 1e-6) continue;

        // Collect geometry data based on object type
        if (obj.type == scene_object_type::sphere) {
            // Sphere: use center + transformed radius for area
            double s = obj.scale.x();
            if (s <= 0.0) s = 1.0;
            double radius = obj.radius * s;
            double area = 4.0 * pi * radius * radius;

            vec3 translation = obj.translation + vec3(obj.center.x(), obj.center.y(), obj.center.z());
            point3 center = point3(translation.x(), translation.y(), translation.z());
            
            // For sphere, normal points outward from center (we'll use average normal = up)
            vec3 normal(0, 1, 0);

            camera::emissive_surface surf;
            surf.position = center;
            surf.normal = normal;
            surf.area = area;
            surf.emission = emission;
            surfaces.push_back(surf);
        }
        else if (obj.type == scene_object_type::cube) {
            // Cube: 6 faces, each face has area = side^2
            vec3 scl = obj.scale;
            if (scl.x() <= 0.0) scl = vec3(1.0, scl.y(), scl.z());
            if (scl.y() <= 0.0) scl = vec3(scl.x(), 1.0, scl.z());
            if (scl.z() <= 0.0) scl = vec3(scl.x(), scl.y(), 1.0);

            double side_x = scl.x();
            double side_y = scl.y();
            double side_z = scl.z();

            // Total surface area = 2*(xy + yz + xz)
            double total_area = 2.0 * (side_x * side_y + side_y * side_z + side_x * side_z);

            vec3 translation = obj.translation + vec3(obj.center.x(), obj.center.y(), obj.center.z());
            point3 center = point3(translation.x(), translation.y(), translation.z());
            vec3 normal(0, 1, 0); // approximate

            camera::emissive_surface surf;
            surf.position = center;
            surf.normal = normal;
            surf.area = total_area;
            surf.emission = emission;
            surfaces.push_back(surf);
        }
        // TODO: mesh instances with emissive materials (requires triangle extraction)
    }

    cam.set_emissive_surfaces(surfaces);
}

