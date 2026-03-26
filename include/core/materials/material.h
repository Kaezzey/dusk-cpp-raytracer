#ifndef MATERIAL_H
#define MATERIAL_H

#include "../hittable.h"
#include "texture.h"
#include "sss.h"
#include <cstdio>
#include <limits>

// ISPC specular helper (defined in src/core). Forward declaration to avoid
// pulling source headers into this public include.
void ispc_compute_specular(const float* Nx, const float* Ny, const float* Nz,
                           const float* Vx, const float* Vy, const float* Vz,
                           const float* Lx, const float* Ly, const float* Lz,
                           const float* F0r, const float* F0g, const float* F0b,
                           const float* alpha, int count,
                           float* out_r, float* out_g, float* out_b);

inline vec3 refract(const vec3& uv, const vec3& n, double etai_over_etat) {
    double cos_theta = fmin(dot(-uv, n), 1.0);
    vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    vec3 r_out_parallel = -sqrt(fmax(0.0, 1.0 - r_out_perp.length_squared())) * n;
    return r_out_perp + r_out_parallel;
}

inline double clamp01(double x) {
    return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
}

inline double pow5(double x) {
    double x2 = x * x;
    return x2 * x2 * x;
}

inline vec3 schlick_fresnel(double cosTheta, const vec3& F0) {
    cosTheta = clamp01(cosTheta);
    double x  = 1.0 - cosTheta;
    return F0 + (vec3(1.0,1.0,1.0) - F0) * pow5(x);
}

// Map perceptual roughness in [0,1] to GGX alpha
inline double perceptual_to_alpha(double roughness) {
    roughness = clamp01(roughness);
    roughness = roughness * roughness;  // square for a more perceptual curve
    // No floor - allow roughness=1 to be truly diffuse (alpha=1.0)
    // For numerical stability in division, we clamp in the GGX functions themselves if needed
    return roughness;
}

// Sample GGX VNDF (approx) in local space (around +Z) – simple version
inline vec3 sample_ggx_half_vector(double alpha, double xi1, double xi2) {
    double a2 = alpha * alpha;

    double cosTheta = sqrt((1.0 - xi2) / (1.0 + (a2 - 1.0) * xi2));
    double sinTheta = sqrt(std::max(0.0, 1.0 - cosTheta * cosTheta));
    double phi = 2.0 * pi * xi1;

    double x = sinTheta * cos(phi);
    double y = sinTheta * sin(phi);
    double z = cosTheta;

    return vec3(x, y, z); // local space
}

// Cosine-weighted hemisphere sampling around +Z
inline vec3 sample_cosine_hemisphere(double xi1, double xi2) {
    double r = sqrt(xi1);
    double theta = 2.0 * pi * xi2;

    double x = r * cos(theta);
    double y = r * sin(theta);
    double z = sqrt(std::max(0.0, 1.0 - xi1));

    return vec3(x, y, z);
}

inline double luminance(const colour& c) {
    return clamp01(0.2126*c.x() + 0.7152*c.y() + 0.0722*c.z());
}

class material {
  public:
    virtual ~material() = default;

    //emission colour, default black
    virtual colour emitted(double u, double v, const point3& p) const {
        return colour(0,0,0);
    }

    virtual bool scatter(
        const ray& r_in, const hit_record& rec, colour& attenuation, ray& scattered
    ) const {
        return false;
    }

    // By default materials are not purely specular. Override in materials
    // that are fully specular (mirror/dielectric) so renderer can skip
    // direct-diffuse lighting (e.g. lambert terms) for them.
        virtual bool is_specular() const { return false; }

    // Identify dielectric materials explicitly (glass, water, etc.)
    // Default false; overridden by dielectric.
    virtual bool is_dielectric() const { return false; }

    // Direct-shading hook for direct lights. Default is
    // Lambertian: albedo * Li * (NdotL / pi). V is the view direction
    // (pointing out from the surface).
    // `vis` is the deterministic visibility/transmittance of the light
    // toward the shading point (in [0,1]). Materials should multiply their
    // surface BRDF contribution by `vis` but may perform independent
    // visibility checks for subsurface/external contributions.
    virtual colour shade_direct(const hit_record& rec, const vec3& V, const vec3& Ldir, const colour& Li, const hittable& world, double vis = 1.0) const {
        double NdotL = std::max(0.0, dot(rec.normal, unit_vector(Ldir)));
        if (NdotL <= 0.0) return colour(0,0,0);
        colour base = albedo(rec);
        return base * Li * (NdotL / pi) * (float)vis;
    }

    // Optional subsurface shading hook. Default implementation returns
    // zero so materials that don't implement SSS don't contribute.
    virtual colour shade_sss(const hit_record& /*rec*/, const vec3& /*V*/, const vec3& /*Ldir*/, const colour& /*Li*/, const hittable& /*world*/, double /*vis*/ = 1.0) const {
        return colour(0,0,0);
    }

    virtual double bsdf_pdf(const hit_record& rec, const vec3& /*V*/, const vec3& Ldir) const {
        double NdotL = std::max(0.0, dot(rec.normal, unit_vector(Ldir)));
        return NdotL / pi;
    }

    //diffuse "base color" hook (used later for direct lighting)
    virtual colour albedo(const hit_record& rec) const {
        return colour(1,1,1);
    }

    // Alpha/mask test: return true if this material wants the current
    // hit to be treated as transparent (i.e. discarded) based on opacity.
    virtual bool is_masked_transparent(const hit_record& rec) const { return false; }

    // Deterministic opacity query in [0,1]. Default is fully opaque (1.0).
    // This is used for visibility/shadow queries where we need a stable
    // alpha value rather than a stochastic discard.
    virtual double opacity_at(const hit_record& rec) const { (void)rec; return 1.0; }
};

class lambertian : public material {
  public:
    lambertian(const colour& albedo) : tex(make_shared<solid_colour>(albedo)) {}
    lambertian(shared_ptr<texture> tex) : tex(tex) {}

    bool scatter(const ray& r_in, const hit_record& rec, colour& attenuation, ray& scattered)
    const override {
        auto scatter_direction = rec.normal + random_unit_vector();

        if (scatter_direction.near_zero())
            scatter_direction = rec.normal;

        scattered = ray(rec.p, scatter_direction, r_in.time());
        attenuation = tex->value(rec.u, rec.v, rec.p);
        return true;
    }

    colour albedo(const hit_record& rec) const override {
        return tex->value(rec.u, rec.v, rec.p);
    } 

  private:
    shared_ptr<texture> tex;
};

class metal : public material {
  public:
    // Existing "flat colour" ctor still works
    metal(const colour& albedo, double fuzz)
        : tex(make_shared<solid_colour>(albedo)),
          fuzz(fuzz < 1 ? fuzz : 1) {}

    // New: textured metal
    metal(shared_ptr<texture> tex, double fuzz)
        : tex(std::move(tex)),
          fuzz(fuzz < 1 ? fuzz : 1) {}

    bool scatter(const ray& r_in, const hit_record& rec,
                 colour& attenuation, ray& scattered) const override
    {
        vec3 reflected = reflect(r_in.direction(), rec.normal);
        reflected = unit_vector(reflected) + (fuzz * random_unit_vector());
        scattered = ray(rec.p, reflected, r_in.time());

        // Sample from the texture instead of a flat colour
        attenuation = tex->value(rec.u, rec.v, rec.p);

        return (dot(scattered.direction(), rec.normal) > 0);
    }

    bool is_specular() const override { return true; }
    colour shade_direct(const hit_record& rec, const vec3& V, const vec3& Ldir, const colour& Li, const hittable& /*world*/, double vis = 1.0) const override {
        return colour(0,0,0);
    }

    colour albedo(const hit_record& rec) const override {
        return tex->value(rec.u, rec.v, rec.p);
    }

  private:
    shared_ptr<texture> tex;
    double fuzz;
};

class dielectric : public material {
  public:
    // Default: clear glass (white attenuation)
    dielectric(double refraction_index)
        : refraction_index(refraction_index),
          tex(make_shared<solid_colour>(colour(1.0, 1.0, 1.0))) {}

    // Coloured glass with flat colour
    dielectric(double refraction_index, const colour& tint)
        : refraction_index(refraction_index),
          tex(make_shared<solid_colour>(tint)) {}

    // Textured “stained” glass
    dielectric(double refraction_index, shared_ptr<texture> tex)
        : refraction_index(refraction_index),
          tex(std::move(tex)) {}

    bool scatter(const ray& r_in, const hit_record& rec,
                 colour& attenuation, ray& scattered) const override
    {
        // Physically-based dielectric (clear glass by default)
        // Determine incident/transmitted indices
        const double ior = refraction_index;
        double etai = 1.0;
        double etat = ior;
        if (!rec.front_face) std::swap(etai, etat);
        double etai_over_etat = etai / etat;

        vec3 unit_direction = unit_vector(r_in.direction());
        double cos_theta = std::fmin(dot(-unit_direction, rec.normal), 1.0);
        double sin_theta = std::sqrt(std::max(0.0, 1.0 - cos_theta * cos_theta));

        // Fresnel reflectance (probability of reflection)
        double reflect_prob = reflectance(cos_theta, etai, etat);

        vec3 direction;
        bool reflected = false;

        // Total internal reflection check
        if (etai_over_etat * sin_theta > 1.0) {
            direction = reflect(unit_direction, rec.normal);
            reflected = true;
        } else {
            if (random_double() < reflect_prob) {
                direction = reflect(unit_direction, rec.normal);
                reflected = true;
            } else {
                direction = refract(unit_direction, rec.normal, etai_over_etat);
            }
        }

        // Offset ray origin slightly to avoid self-intersections.
        // Use the same bias used elsewhere (0.001) to be consistent and
        // avoid the reflected ray immediately re-hitting the same surface.
        const double eps = 1e-3;
        vec3 n = rec.normal;
        // Ensure direction is unit-length for the dot test
        vec3 dir_norm = unit_vector(direction);
        vec3 origin = rec.p + ((dot(dir_norm, n) > 0.0) ? (eps * n) : (-eps * n));

        // Reflections stay untinted. Refracted paths can carry stained-glass colour.
        attenuation = reflected ? colour(1.0, 1.0, 1.0)
                                : tex->value(rec.u, rec.v, rec.p);

        // normalize outgoing direction to avoid non-normalized rays later
        scattered = ray(origin, dir_norm, r_in.time());
        return true;
    }
    
    bool is_specular() const override { return true; }
    bool is_dielectric() const override { return true; }

    // Direct shading for dielectrics: only add the reflective lobe.
    // Refractive focusing / caustics are handled by the specular transport,
    // photon map, and optional MNEE paths rather than a fake direct-light term.
    colour shade_direct(const hit_record& rec, const vec3& V, const vec3& Ldir, const colour& Li, const hittable& /*world*/, double vis = 1.0) const override {
        vec3 N = rec.normal;
        vec3 L = unit_vector(Ldir);
        vec3 Vn = unit_vector(V);

        double NdotL = std::max(0.0, dot(N, L));
        double NdotV = std::max(0.0, dot(N, Vn));
        if (NdotL <= 0.0 || NdotV <= 0.0) return colour(0,0,0);

        vec3 Hsum = L + Vn;
        if (Hsum.length_squared() <= 1e-12) return colour(0,0,0);
        vec3 H = unit_vector(Hsum);
        double NdotH = std::max(0.0001, dot(N, H));
        double VdotH = std::max(0.0001, dot(Vn, H));

        // Keep direct-light highlights sharp for clear glass while avoiding
        // purely singular direct terms against analytic lights.
        double rough = 0.02;
        double alpha = rough * rough;

        // GGX / Trowbridge-Reitz D term
        double a2 = alpha * alpha;
        double denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
        double D = (a2) / (pi * denom * denom + 1e-12);

        double k = rough + 1.0;
        k = (k * k) * 0.125;
        auto geometry_schlick_ggx = [](double NdotX, double k_val) {
            return NdotX / (NdotX * (1.0 - k_val) + k_val);
        };
        double G = geometry_schlick_ggx(NdotV, k) * geometry_schlick_ggx(NdotL, k);

        // Fresnel using Schlick with F0 from IOR
        double etai = 1.0;
        double etat = refraction_index;
        if (!rec.front_face) std::swap(etai, etat);
        // compute F0
        double r0 = (etai - etat) / (etai + etat);
        r0 = r0 * r0;
        colour F0((float)r0, (float)r0, (float)r0);
        colour F = schlick_fresnel(std::max(0.0, VdotH), F0);
        colour spec = F * (float)((D * G) / std::max(1e-6, 4.0 * NdotV * NdotL));
        return spec * Li * (float)(NdotL * vis);
    }

    colour albedo(const hit_record& rec) const override {
        return colour(1.0, 1.0, 1.0);
    }

  private:
    double refraction_index;
    shared_ptr<texture> tex;

    // Fresnel (Schlick) using explicit incident/transmitted indices
    static double reflectance(double cosine, double etai, double etat) {
        // compute F0 from indices
        double r0 = (etai - etat) / (etai + etat);
        r0 = r0 * r0;
        double x = 1.0 - std::clamp(cosine, 0.0, 1.0);
        return r0 + (1.0 - r0) * std::pow(x, 5.0);
    }
};

class diffuse_light : public material {
  public:
    diffuse_light(shared_ptr<texture> tex) : tex(tex) {}
    diffuse_light(const colour& emit) : tex(make_shared<solid_colour>(emit)) {}

    colour emitted(double u, double v, const point3& p) const override {
        return tex->value(u, v, p);
    }

    colour albedo(const hit_record& rec) const override {
        return tex->value(0, 0, 0);
    }

  private:
    shared_ptr<texture> tex;
};

class isotropic : public material {
  public:
    isotropic(const colour& albedo) : tex(make_shared<solid_colour>(albedo)) {}
    isotropic(shared_ptr<texture> tex) : tex(tex) {}

    bool scatter(const ray& r_in, const hit_record& rec, colour& attenuation, ray& scattered)
    const override {
        scattered = ray(rec.p, random_unit_vector(), r_in.time());
        attenuation = tex->value(rec.u, rec.v, rec.p);
        return true;
    }

    colour albedo(const hit_record& rec) const override {
        return tex->value(rec.u, rec.v, rec.p);
    }

  private:
    shared_ptr<texture> tex;
};

// ----------------------------
// PBR material
// ----------------------------

// SSS model choices
enum SSSModel {
    SSS_NONE = 0,
    SSS_SINGLE_SCATTER = 1,
    SSS_MULTI_SINGLE_SCATTER = 2,
    SSS_DIPOLE_BURLEY = 3,
    SSS_SKIN = 4,      // Skin-like surface scattering / transmission
    SSS_FOLIAGE = 5    // Thin transmission model for leaves and petals
};

class pbr_material : public material {
public:
    // Base color, metallic, roughness, specular F0, normal map
    shared_ptr<texture> base_tex;
    shared_ptr<texture> metallic_tex;   // greyscale in [0,1]
    shared_ptr<texture> roughness_tex;  // greyscale in [0,1]
    shared_ptr<texture> normal_tex;     // tangent-space normal map
    double normal_strength;

    // Optional alpha mask texture (single-channel or image alpha)
    shared_ptr<texture> alpha_tex;
    bool alpha_double_sided = true;
    double alpha_cutoff = 0.5;

    // Dielectric F0 when metallic = 0 (0.04 is common)
    colour  dielectric_F0;

    // Simple thin-subsurface approximation parameters
    // sss_strength: [0,1] how much light is transmitted/scattered through the thin material
    // sss_scale: user-facing scale controlling amount of transmission (mean free path)
    // The SSS tint now uses the material albedo (baseColor) so no explicit
    // sss tint colour is required.
    double sss_strength = 0.0;
    double sss_scale = 1.0;
    // Dipole/diffusion parameters
    SSSModel sss_model = SSS_SINGLE_SCATTER;
    int      sss_samples = 4;        // number of exit-point samples for dipole
    double   sss_radius  = 1.0;      // mean free path / diffusion radius
    double   sss_eta     = 1.3;      // relative index (not yet used)
    bool     sss_color_override = false;
    colour   sss_color_override_col = colour(1.0, 1.0, 1.0);
    // If true, sample combined textures using Unreal-style channel packing
    // (green = roughness, blue = metallic)
    bool     use_unreal_pbr = false;

public:
    // (A) Constant base/metal/rough with optional normal map
    pbr_material(
        const colour& base_color,
        double metallic,
        double roughness,
        shared_ptr<texture> normal_map = nullptr,
        double normal_strength_in = 1.0,
        const colour& dielectric_specular = colour(0.04, 0.04, 0.04)
    )
        : dielectric_F0(dielectric_specular),
          base_tex(make_shared<solid_colour>(base_color)),
          metallic_tex(make_shared<solid_colour>(colour(clamp01(metallic), clamp01(metallic), clamp01(metallic)))),
          roughness_tex(make_shared<solid_colour>(colour(clamp01(roughness), clamp01(roughness), clamp01(roughness)))),
          normal_tex(normal_map),
          normal_strength(normal_strength_in)
    {}


    // (B) Textured base + scalar metallic/rough + normal map
    pbr_material(
        shared_ptr<texture> base_color,
        double metallic,
        double roughness,
        shared_ptr<texture> normal_map,
        double normal_strength_in = 1.0,
        const colour& dielectric_specular = colour(0.04, 0.04, 0.04)
    )
        : dielectric_F0(dielectric_specular),
          base_tex(base_color),
          metallic_tex(make_shared<solid_colour>(colour(clamp01(metallic), clamp01(metallic), clamp01(metallic)))),
          roughness_tex(make_shared<solid_colour>(colour(clamp01(roughness), clamp01(roughness), clamp01(roughness)))),
          normal_tex(normal_map),
          normal_strength(normal_strength_in)
    {}


    // (C) Fully textured PBR: base, metallic, roughness, normal
    pbr_material(
        shared_ptr<texture> base_color,
        shared_ptr<texture> metallic,
        shared_ptr<texture> roughness,
        shared_ptr<texture> normal_map,
        double normal_strength_in = 1.0,
        const colour& dielectric_specular = colour(0.04, 0.04, 0.04),
        shared_ptr<texture> alpha_map = nullptr,
        bool alpha_double_sided_in = true,
        double alpha_cutoff_in = 0.5
    )
        : dielectric_F0(dielectric_specular),
          base_tex(base_color),
          metallic_tex(metallic),
          roughness_tex(roughness),
          normal_tex(normal_map),
          normal_strength(normal_strength_in),
          alpha_tex(alpha_map),
          alpha_double_sided(alpha_double_sided_in),
          alpha_cutoff(alpha_cutoff_in)
    {}

  private:
    static bool is_scalar_fallback_texture(const shared_ptr<texture>& tex) {
        return tex && dynamic_cast<const solid_colour*>(tex.get()) != nullptr;
    }

    double specular_sampling_probability(const colour& F0, double metallic) const {
        if (metallic >= 0.999) return 1.0;
        double f0_peak = std::max(F0.x(), std::max(F0.y(), F0.z()));
        return std::clamp(f0_peak, 0.05, 0.98);
    }

    void sample_surface_params(const hit_record& rec,
                               colour& baseColor,
                               double& metallic,
                               double& rough) const
    {
        baseColor = base_tex ? base_tex->value(rec.u, rec.v, rec.p)
                             : colour(1,1,1);

        metallic = 0.0;
        rough = 0.5;

        if (use_unreal_pbr) {
            const bool has_metallic_texture =
                metallic_tex && !is_scalar_fallback_texture(metallic_tex);
            const bool has_roughness_texture =
                roughness_tex && !is_scalar_fallback_texture(roughness_tex);

            // Start from scalar fallbacks so materials without authored maps still work.
            if (metallic_tex) {
                metallic = clamp01(metallic_tex->value(rec.u, rec.v, rec.p).x());
            }
            if (roughness_tex) {
                rough = clamp01(roughness_tex->value(rec.u, rec.v, rec.p).x());
            }

            // Accept a packed map from either slot. This matches common editor usage where
            // artists may place the ORM/MR texture in only one of the fields.
            shared_ptr<texture> packed_tex = has_metallic_texture ? metallic_tex
                                                                  : (has_roughness_texture ? roughness_tex : nullptr);
            if (packed_tex) {
                colour mr = packed_tex->value(rec.u, rec.v, rec.p);
                rough = clamp01(mr.y());
                metallic = clamp01(mr.z());
            }

            // If dedicated authored grayscale maps are supplied separately, let them
            // override the corresponding packed channels while leaving scalar fallbacks alone.
            if (has_roughness_texture && roughness_tex.get() != packed_tex.get()) {
                rough = clamp01(roughness_tex->value(rec.u, rec.v, rec.p).x());
            }
            if (has_metallic_texture && metallic_tex.get() != packed_tex.get()) {
                metallic = clamp01(metallic_tex->value(rec.u, rec.v, rec.p).x());
            }
            return;
        }

        if (metallic_tex) {
            // Separate metal maps are data textures; for grayscale authoring the
            // value is replicated across channels, so sample the first channel.
            metallic = clamp01(metallic_tex->value(rec.u, rec.v, rec.p).x());
        }
        if (roughness_tex) {
            rough = clamp01(roughness_tex->value(rec.u, rec.v, rec.p).x());
        }
    }

  public:
    virtual colour albedo(const hit_record& rec) const override {
        return base_tex ? base_tex->value(rec.u, rec.v, rec.p)
                        : colour(1,1,1);
    }

    // Core scatter
    virtual bool scatter(
        const ray& r_in,
        const hit_record& rec,
        colour& attenuation,
        ray& scattered
    ) const override {

        // Alpha mask: if present and indicates transparent, behave as if no hit.
        // For triangle/Embree paths this is already checked at intersection time
        // via is_masked_transparent(). For other primitives (quads/spheres) we
        // need to continue the ray through the surface when masked instead of
        // terminating the path. We implement that by spawning a continuation
        // ray in the same direction and returning 'true' with neutral
        // attenuation so the renderer treats it as a miss.
        // Resolve alpha: prefer explicit alpha map, otherwise fall back to
        // the base/albedo texture's alpha channel (if any).
        auto resolve_alpha = [&](const hit_record& hrec) {
            if (alpha_tex) return alpha_tex->mask_alpha_at(hrec.u, hrec.v, hrec.p);
            if (base_tex) return base_tex->alpha_at(hrec.u, hrec.v, hrec.p);
            return 1.0;
        };

        double a_res = resolve_alpha(rec);
        if (alpha_double_sided || rec.front_face) {
            // Fast-path fully transparent or opaque
            if (a_res <= 0.0 || a_res < alpha_cutoff) {
                scattered = ray(rec.p + r_in.direction() * 0.001, r_in.direction(), r_in.time());
                attenuation = colour(1.0, 1.0, 1.0);
                return true;
            }

            if (a_res >= 1.0) {
                // fully opaque, continue into regular scattering
            } else {
                // Stochastic alpha: treat the hit as opaque with probability a_res,
                // otherwise continue the ray (transparent). This produces soft
                // alpha edges when averaged across multiple samples.
                if (random_double() > a_res) {
                    scattered = ray(rec.p + r_in.direction() * 0.001, r_in.direction(), r_in.time());
                    attenuation = colour(1.0, 1.0, 1.0);
                    return true;
                }
            }
        }

        // ----------------
        // Fetch parameters
        // ----------------
        colour baseColor(1,1,1);
        double metallic = 0.0;
        double rough = 0.5;
        sample_surface_params(rec, baseColor, metallic, rough);

        double alpha = perceptual_to_alpha(rough);

        // Fresnel base reflectivity: mix between dielectric F0 and baseColornormal_strength_in
        colour F0 = (vec3(1.0,1.0,1.0) - vec3(metallic, metallic, metallic)) * dielectric_F0
                    + vec3(metallic, metallic, metallic) * baseColor;

        // ----------------
        // Build world-space normal from normal map (if present)
        // ----------------
        vec3 Ngeom = rec.normal;
        vec3 N = Ngeom;

        // Use tangent & bitangent from hit_record as the ONB everywhere
        vec3 T = rec.tangent;
        vec3 B = rec.bitangent;

        if (normal_tex) {
            colour n_tex = normal_tex->value(rec.u, rec.v, rec.p);

            // Decode tangent-space normal from [0,1] → [-1,1]
            vec3 n_tan_raw(
                2.0 * n_tex.x() - 1.0,
                2.0 * n_tex.y() - 1.0,
                2.0 * n_tex.z() - 1.0
            );

            // Apply intensity (only to X and Y)
            vec3 n_tan(
                n_tan_raw.x() * normal_strength,
                n_tan_raw.y() * normal_strength,
                n_tan_raw.z()
            );

            n_tan = unit_vector(n_tan);

            // Transform into world space using T,B,Ngeom
            N = unit_vector_fast(
                n_tan.x() * T +
                n_tan.y() * B +
                n_tan.z() * Ngeom
            );
        }

        // --- NEW FIX: GRAZING ANGLE DAMPING ---
        // As the view angle gets shallower, we fade the normal map out.
        // This prevents the "Impossible Reflection" paradox before it happens.
        
        vec3 view_dir = -unit_vector(r_in.direction());
        
        // Calculate how much we are facing the geometry (0.0 = edge, 1.0 = center)
        double NdotV = std::max(0.0, dot(Ngeom, view_dir));
        
        // Fade factor: Start fading when within the last 20% of the edge.
        // smoothstep helps make it look organic.
        double strength = NdotV * 5.0; 
        strength = std::clamp(strength, 0.0, 1.0);

        // Blend the Bumpy Normal (N) into the Safe Geometry Normal (Ngeom)
        N = unit_vector(N * strength + Ngeom * (1.0 - strength));

        vec3 wo = -unit_vector(r_in.direction()); // view dir in world space

        // ---------------
        // Lobe selection
        // ---------------
        // Sampling probability should track specular energy, not arbitrarily zero out
        // rough specular. Otherwise mixed metal/rough materials collapse toward diffuse.
        double spec_prob = specular_sampling_probability(F0, metallic);

        double xi_lobe = random_double();

        vec3 wi;        // scattered direction
        colour weight;  // BRDF-ish weight used as attenuation

        if (xi_lobe < spec_prob) {
            // 1. Generate GGX Sample
            double xi1 = random_double();
            double xi2 = random_double();
            vec3 h_local = sample_ggx_half_vector(alpha, xi1, xi2);
            vec3 h = unit_vector_fast(h_local.x() * T + h_local.y() * B + h_local.z() * N);

            // 2. Calculate Reflection Direction
            wi = reflect(-wo, h);

            // --- HORIZON LIFTING FIX ---
            
            // Check: Is this ray pointing "underground" relative to the REAL geometry?
            double dot_geom = dot(wi, rec.normal);
            double geometry_shadowing = 1.0; // Default to keeping all light

            if (dot_geom < 0.0) {
                // 1. Always Lift the ray to avoid pitch-black artifacts
                // Subtract the underground component so it skids along the surface
                wi = wi - (dot_geom * rec.normal);
                wi = unit_vector(wi);
                
                // 2. Penalize the energy based on Roughness
                // The deeper the ray was pointing (dot_geom), the more we darken it.
                // If Roughness is 0.0 (Mirror), we don't darken at all.
                // If Roughness is 1.0 (Matte), we darken significantly.
                
                // This creates a "Soft Shadow" that removes the dark spots 
                // but keeps the matte look at the bottom.
                double lift_amount = -dot_geom; 
                geometry_shadowing = 1.0 - (rough * lift_amount);
                geometry_shadowing = std::clamp(geometry_shadowing, 0.0, 1.0);
            }
            // -------------------------------------------

              // Safety Check (for the shading normal)
              if (dot(wi, N) <= 0.0) {
                  wi = reflect(-wo, N);
                  if (dot(wi, N) <= 0.0) return false;
              }

            // Standard Fresnel & Weight
            double cosTheta = std::max(0.0, dot(wi, N));
            colour F = schlick_fresnel(std::max(0.0, dot(h, wo)), F0);
            
            weight = F * (1.0 / std::max(0.001, spec_prob));
            
            // 3. Apply the Soft Shadowing Factor
            weight *= geometry_shadowing;
        } else {
            // --------------------------
            // Diffuse (Lambertian) lobe
            // --------------------------
            double xi1 = random_double();
            double xi2 = random_double();

            vec3 d_local = sample_cosine_hemisphere(xi1, xi2);

            wi = unit_vector(
                  d_local.x() * T
                + d_local.y() * B
                + d_local.z() * N
            );

            // Default-lit real-time workflows use baseColor * (1 - metallic) for the
            // diffuse lobe and leave Fresnel energy handling to the direct BRDF.
            colour kd = baseColor * (1.0 - metallic);
            weight = kd / std::max(0.05, 1.0 - spec_prob);
        }

        scattered   = ray(rec.p, wi, r_in.time());
        attenuation = weight;

        return true;
    }

    // Proper PBR direct shading using GGX microfacet BRDF (energy-conserving)
    virtual colour shade_direct(const hit_record& rec, const vec3& V, const vec3& Ldir, const colour& Li, const hittable& world, double vis = 1.0) const override {
        (void)world;
        double a_ds = 1.0;
        if (alpha_tex) a_ds = alpha_tex->mask_alpha_at(rec.u, rec.v, rec.p);
        else if (base_tex) a_ds = base_tex->alpha_at(rec.u, rec.v, rec.p);
        if (alpha_double_sided || rec.front_face) {
            if (a_ds <= 0.0 || a_ds < alpha_cutoff) return colour(0,0,0);
            a_ds = clamp01(a_ds);
        } else {
            a_ds = 1.0;
        }
        // Reconstruct the shading normal from the normal map (if present)
        vec3 Ngeom = rec.normal;
        vec3 N = Ngeom;

        // Use tangent & bitangent from hit_record as the ONB
        vec3 T = rec.tangent;
        vec3 B = rec.bitangent;

        if (normal_tex) {
            colour n_tex = normal_tex->value(rec.u, rec.v, rec.p);
            vec3 n_tan_raw(
                2.0 * n_tex.x() - 1.0,
                2.0 * n_tex.y() - 1.0,
                2.0 * n_tex.z() - 1.0
            );

            vec3 n_tan(
                n_tan_raw.x() * normal_strength,
                n_tan_raw.y() * normal_strength,
                n_tan_raw.z()
            );

            n_tan = unit_vector(n_tan);

            N = unit_vector(
                n_tan.x() * T +
                n_tan.y() * B +
                n_tan.z() * Ngeom
            );
        }

        // Same normal-map fade used in scatter: fade normal map at grazing angles
        vec3 view_dir = unit_vector(V);
        double NdotV_geo = std::max(0.0, dot(Ngeom, view_dir));
        double strength = NdotV_geo * 5.0;
        strength = std::clamp(strength, 0.0, 1.0);
        N = unit_vector_fast(N * strength + Ngeom * (1.0 - strength));
        vec3 L = unit_vector(Ldir);
        vec3 Vn = unit_vector(V);

        double NdotL = std::max(0.0, dot(N, L));
        double NdotV = std::max(0.0, dot(N, Vn));
        if (NdotL <= 0.0 || NdotV <= 0.0) return colour(0,0,0);

        // Fetch material parameters at this shading point
        colour baseColor(1,1,1);
        double metallic = 0.0;
        double rough = 0.5;
        sample_surface_params(rec, baseColor, metallic, rough);
        double alpha = perceptual_to_alpha(rough);

        // F0 mix between dielectric F0 and baseColor for metals
        colour F0 = (vec3(1.0,1.0,1.0) - vec3(metallic, metallic, metallic)) * dielectric_F0
                    + vec3(metallic, metallic, metallic) * baseColor;

        vec3 Hsum = L + Vn;
        if (Hsum.length_squared() <= 1e-12) return colour(0,0,0);
        vec3 H = unit_vector(Hsum);
        double NdotH = std::max(1e-6, dot(N, H));
        double VdotH = std::max(1e-6, dot(Vn, H));

        // Microfacet D term (GGX)
        double a2 = alpha * alpha;
        double denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
        double D = (a2) / (pi * denom * denom + 1e-12);

        // Analytic-light roughness remap used by common real-time GGX implementations.
        double k = rough + 1.0;
        k = (k * k) * 0.125;
        auto geometry_schlick = [&](double NdotX) {
            return NdotX / (NdotX * (1.0 - k) + k);
        };
        double G = geometry_schlick(NdotV) * geometry_schlick(NdotL);

        // Fresnel
        colour F = schlick_fresnel(std::max(0.0, VdotH), F0);

        // Specular BRDF value
        colour spec = F * (float)((D * G) / (4.0 * NdotV * NdotL + 1e-12));

        // Default-lit PBR diffuse: baseColor for dielectrics, black for metals.
        colour kd = baseColor * (1.0 - metallic);

        colour brdf = kd * (float)(1.0 / pi) + spec;

        // Final outgoing radiance: f * Li * cosθ
        colour direct = brdf * Li * (float)NdotL;

        // SSS is handled separately via `shade_sss()` to allow the renderer
        // to compute the main BRDF using the fast ISPC path and then add
        // the SSS contribution afterwards. See `shade_sss()` implementation
        // below in pbr_material.

        return direct * (float)(a_ds * vis);
    }

    virtual double bsdf_pdf(const hit_record& rec, const vec3& V, const vec3& Ldir) const override {
        double a_ds = 1.0;
        if (alpha_tex) a_ds = alpha_tex->mask_alpha_at(rec.u, rec.v, rec.p);
        else if (base_tex) a_ds = base_tex->alpha_at(rec.u, rec.v, rec.p);
        if ((alpha_double_sided || rec.front_face) && (a_ds <= 0.0 || a_ds < alpha_cutoff)) {
            return 0.0;
        }

        vec3 Ngeom = rec.normal;
        vec3 N = Ngeom;
        vec3 T = rec.tangent;
        vec3 B = rec.bitangent;

        if (normal_tex) {
            colour n_tex = normal_tex->value(rec.u, rec.v, rec.p);
            vec3 n_tan(
                (2.0 * n_tex.x() - 1.0) * normal_strength,
                (2.0 * n_tex.y() - 1.0) * normal_strength,
                2.0 * n_tex.z() - 1.0
            );
            n_tan = unit_vector(n_tan);
            N = unit_vector_fast(
                n_tan.x() * T +
                n_tan.y() * B +
                n_tan.z() * Ngeom
            );
        }

        vec3 Vn = unit_vector(V);
        double NdotV_geo = std::max(0.0, dot(Ngeom, Vn));
        double strength = std::clamp(NdotV_geo * 5.0, 0.0, 1.0);
        N = unit_vector_fast(N * strength + Ngeom * (1.0 - strength));
        vec3 L = unit_vector(Ldir);

        double NdotL = std::max(0.0, dot(N, L));
        double NdotV = std::max(0.0, dot(N, Vn));
        if (NdotL <= 0.0 || NdotV <= 0.0) return 0.0;

        colour baseColor(1,1,1);
        double metallic = 0.0;
        double rough = 0.5;
        sample_surface_params(rec, baseColor, metallic, rough);

        colour F0 = (vec3(1.0,1.0,1.0) - vec3(metallic, metallic, metallic)) * dielectric_F0
                    + vec3(metallic, metallic, metallic) * baseColor;
        double spec_prob = specular_sampling_probability(F0, metallic);

        double diffuse_pdf = NdotL / pi;
        double alpha = perceptual_to_alpha(rough);
        vec3 Hsum = L + Vn;
        if (Hsum.length_squared() <= 1e-12) return diffuse_pdf;

        vec3 H = unit_vector(Hsum);
        double NdotH = std::max(1e-6, dot(N, H));
        double VdotH = std::max(1e-6, dot(Vn, H));
        double a2 = alpha * alpha;
        double denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
        double D = a2 / (pi * denom * denom + 1e-12);
        double spec_pdf = (D * NdotH) / std::max(1e-6, 4.0 * VdotH);

        return (1.0 - spec_prob) * diffuse_pdf + spec_prob * spec_pdf;
    }

    // pbr_material: SSS-only shading hook (called by renderer after BRDF)
    colour shade_sss(const hit_record& rec, const vec3& V, const vec3& Ldir, const colour& Li, const hittable& world, double vis = 1.0) const override {
        if (sss_strength <= 0.0 || sss_model == SSS_NONE) return colour(0,0,0);

        double alpha_sss = 1.0;
        if (alpha_tex) alpha_sss = alpha_tex->mask_alpha_at(rec.u, rec.v, rec.p);
        else if (base_tex) alpha_sss = base_tex->alpha_at(rec.u, rec.v, rec.p);
        if ((alpha_double_sided || rec.front_face) && (alpha_sss <= 0.0 || alpha_sss < alpha_cutoff)) {
            return colour(0,0,0);
        }

        vec3 L = unit_vector(Ldir);
        vec3 Vn = unit_vector(V);
        double NdotL_geom = dot(rec.normal, L);
        double NdotV_geom = std::max(0.0, dot(rec.normal, Vn));
        colour sss_tint = sss_color_override ? sss_color_override_col : albedo(rec);

        colour baseColor_dummy(1,1,1);
        double metallic = 0.0;
        double rough_dummy = 0.5;
        sample_surface_params(rec, baseColor_dummy, metallic, rough_dummy);

        double strength = sss_strength * (1.0 - metallic);
        strength *= clamp01(alpha_sss) * vis;
        if (strength <= 0.0) return colour(0,0,0);

        auto transmission_tint = [&](double optical_depth, const colour& channel_bias) {
            optical_depth = std::max(0.0, optical_depth);
            return colour(
                std::pow(std::clamp(sss_tint.x(), 0.02, 0.999), optical_depth * channel_bias.x()),
                std::pow(std::clamp(sss_tint.y(), 0.02, 0.999), optical_depth * channel_bias.y()),
                std::pow(std::clamp(sss_tint.z(), 0.02, 0.999), optical_depth * channel_bias.z())
            );
        };

        auto estimate_thickness = [&](double fallback, double& thickness_ws) {
            thickness_ws = fallback;
            vec3 inside_dir = -L;
            ray into_ray(rec.p + inside_dir * 0.001, inside_dir, 0.0);
            hit_record exit_rec;
            double max_distance = std::max(0.05, sss_radius * 6.0);
            if (!world.hit(into_ray, interval(0.001, max_distance), exit_rec)) return false;
            thickness_ws = std::max(0.0, (exit_rec.p - rec.p).length());
            return thickness_ws > 1e-6;
        };

        if (sss_model == SSS_FOLIAGE) {
            double thickness_ws = std::max(0.05, sss_radius);
            estimate_thickness(thickness_ws, thickness_ws);
            double optical_depth = thickness_ws / std::max(0.05, sss_scale);
            colour trans_color = transmission_tint(optical_depth, colour(0.75, 1.0, 1.25));

            double lambert = clamp01(NdotL_geom);
            double wrapped = std::clamp((NdotL_geom + 0.35) / 1.35, 0.0, 1.0);
            double wrap_gain = std::max(0.0, wrapped - lambert);
            double view_through = clamp01(dot(-Vn, L));
            double back_scatter = clamp01(-NdotL_geom) * (0.35 + 0.65 * view_through);
            double scatter_term = wrap_gain * 0.35 + back_scatter;
            return trans_color * Li * (float)(strength * scatter_term);
        }

        if (sss_model == SSS_SKIN) {
            double thickness_ws = std::max(0.05, sss_radius);
            estimate_thickness(thickness_ws, thickness_ws);
            double optical_depth = thickness_ws / std::max(0.05, sss_scale);
            colour back_color = transmission_tint(optical_depth, colour(0.45, 1.0, 2.0));

            double lambert = clamp01(NdotL_geom);
            double wrapped = std::clamp((NdotL_geom + 0.45) / 1.45, 0.0, 1.0);
            double forward_scatter = std::max(0.0, wrapped - lambert);
            double back_scatter = clamp01(-NdotL_geom) * NdotV_geom;
            colour scatter_color =
                sss_tint * (float)(forward_scatter * 0.75) +
                back_color * (float)(back_scatter * 0.5);
            return scatter_color * Li * (float)strength;
        }

        if (sss_model == SSS_DIPOLE_BURLEY) {
            double NdotL = clamp01(NdotL_geom);
            if (NdotL <= 0.0) return colour(0,0,0);

            colour sss_acc(0,0,0);
            int ns = std::max(1, sss_samples);
            vec3 T = rec.tangent;
            vec3 B = rec.bitangent;
            for (int si = 0; si < ns; ++si) {
                double u1 = random_double();
                double u2 = random_double();
                double r = sss::sample_r_exponential(sss_radius, u1);
                double theta = 2.0 * pi * u2;
                double dx = r * std::cos(theta);
                double dy = r * std::sin(theta);

                point3 exit_probe = rec.p + T * (float)dx + B * (float)dy;
                point3 ray_origin = exit_probe + rec.normal * (float)(sss_radius * 0.5);
                ray probe_ray(ray_origin, -rec.normal, 0.0);
                hit_record exit_rec;
                if (!world.hit(probe_ray, interval(0.001, sss_radius * 2.0), exit_rec)) continue;

                double scatter_dist = (exit_rec.p - rec.p).length();
                double trans = std::exp(-scatter_dist / std::max(1e-6, sss_radius));
                double Rd = sss::burley_Rd(r, std::max(0.1, luminance(sss_tint)), sss_radius);
                double pdf_area = sss::pdf_area_from_radius(r, sss_radius);
                if (pdf_area <= 1e-9) continue;

                double NdotV_exit = std::max(0.0, dot(exit_rec.normal, Vn));
                if (NdotV_exit <= 0.0) continue;

                colour contrib = sss_tint * (float)(strength * trans * Rd / pdf_area)
                                 * Li * (float)NdotL * (float)NdotV_exit;
                sss_acc += contrib;
            }
            return sss_acc * (float)(1.0 / std::max(1, sss_samples));
        }

        double thickness_ws = std::max(0.05, sss_radius);
        estimate_thickness(thickness_ws, thickness_ws);
        double optical_depth = thickness_ws / std::max(0.05, sss_scale);
        colour trans_color = transmission_tint(optical_depth, colour(0.65, 1.0, 1.4));
        double wrapped = std::clamp((NdotL_geom + 0.3) / 1.3, 0.0, 1.0);
        double back_scatter = clamp01(-NdotL_geom);
        double scatter_term = (sss_model == SSS_MULTI_SINGLE_SCATTER)
            ? (wrapped * 0.55 + back_scatter * 0.85)
            : (wrapped * 0.35 + back_scatter * 0.75);
        return trans_color * Li * (float)(strength * scatter_term);
    }

        // Mask test for triangle-level discard
        virtual bool is_masked_transparent(const hit_record& rec) const override {
            double a = 1.0;
            if (alpha_tex) a = alpha_tex->mask_alpha_at(rec.u, rec.v, rec.p);
            else if (base_tex) a = base_tex->alpha_at(rec.u, rec.v, rec.p);
            if (alpha_double_sided || rec.front_face) {
                // Fast discard when clearly below cutoff or fully transparent.
                if (a <= 0.0 || a < alpha_cutoff) return true;
                // Stochastic discard: treat as miss with probability (1-a).
                if (a < 1.0 && random_double() > a) return true;
            }
            return false;
        }

        // Deterministic opacity query used for visibility/shadow calculations.
        virtual double opacity_at(const hit_record& rec) const override {
                double a = 1.0;
                if (alpha_tex) a = alpha_tex->mask_alpha_at(rec.u, rec.v, rec.p);
                else if (base_tex) a = base_tex->alpha_at(rec.u, rec.v, rec.p);
                return clamp01(a);
        }
};


#endif // MATERIAL_H
