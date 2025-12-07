#ifndef MATERIAL_H
#define MATERIAL_H

#include "../hittable.h"
#include "texture.h"

inline vec3 refract(const vec3& uv, const vec3& n, double etai_over_etat) {
    double cos_theta = fmin(dot(-uv, n), 1.0);
    vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    vec3 r_out_parallel = -sqrt(fmax(0.0, 1.0 - r_out_perp.length_squared())) * n;
    return r_out_perp + r_out_parallel;
}

inline double clamp01(double x) {
    return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
}

inline vec3 schlick_fresnel(double cosTheta, const vec3& F0) {
    cosTheta = clamp01(cosTheta);
    double x  = 1.0 - cosTheta;
    double x2 = x * x;
    double x5 = x2 * x2 * x;  // x^5 without pow()
    return F0 + (vec3(1.0,1.0,1.0) - F0) * x5;
}

// Map perceptual roughness in [0,1] to GGX alpha with floor
inline double perceptual_to_alpha(double roughness) {
    roughness = clamp01(roughness);
    roughness = roughness * roughness;  // square for a more perceptual curve
    const double min_alpha = 0.02;
    return std::max(roughness, min_alpha);
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

    // Direct-shading hook for direct lights. Default is
    // Lambertian: albedo * Li * (NdotL / pi). V is the view direction
    // (pointing out from the surface).
    virtual colour shade_direct(const hit_record& rec, const vec3& V, const vec3& Ldir, const colour& Li) const {
        double NdotL = std::max(0.0, dot(rec.normal, unit_vector(Ldir)));
        if (NdotL <= 0.0) return colour(0,0,0);
        colour base = albedo(rec);
        return base * Li * (NdotL / pi);
    }

    //diffuse "base color" hook (used later for direct lighting)
    virtual colour albedo(const hit_record& rec) const {
        return colour(1,1,1);
    }

    // Alpha/mask test: return true if this material wants the current
    // hit to be treated as transparent (i.e. discarded) based on opacity.
    virtual bool is_masked_transparent(const hit_record& rec) const { return false; }
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
    colour shade_direct(const hit_record& rec, const vec3& V, const vec3& Ldir, const colour& Li) const override {
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

        // Total internal reflection check
        if (etai_over_etat * sin_theta > 1.0) {
            direction = reflect(unit_direction, rec.normal);
        } else {
            if (random_double() < reflect_prob) {
                direction = reflect(unit_direction, rec.normal);
            } else {
                direction = refract(unit_direction, rec.normal, etai_over_etat);
            }
        }

        // Offset ray origin slightly to avoid self-intersections
        const double eps = 1e-4;
        vec3 n = rec.normal;
        vec3 origin = rec.p + ((dot(direction, n) > 0.0) ? (eps * n) : (-eps * n));

        // Clear glass: do not tint reflections or transmissions by default.
        // This keeps glass looking crystal-clear irrespective of light intensity.
        attenuation = colour(1.0, 1.0, 1.0);

        scattered = ray(origin, direction, r_in.time());
        return true;
    }
    
    bool is_specular() const override { return true; }

    // Direct shading for dielectrics: only a specular Fresnel reflection
    // (no diffuse). Transmission of light into the scene is handled by
    // refracted scattering paths, so shadows behind the glass remain
    // dark unless light is transmitted through the medium.
    colour shade_direct(const hit_record& rec, const vec3& V, const vec3& Ldir, const colour& Li) const override {
        vec3 N = rec.normal;
        vec3 L = unit_vector(Ldir);
        vec3 Vn = unit_vector(V);

        double NdotL = std::max(0.0001, dot(N, L));
        double NdotV = std::max(0.0001, dot(N, Vn));

        // Half vector
        vec3 H = unit_vector(L + Vn);
        double NdotH = std::max(0.0001, dot(N, H));
        double VdotH = std::max(0.0001, dot(Vn, H));

        // Use a small roughness for clear glass; increase slightly so
        // highlights are broader for direct lights. We'll also apply an
        // intensity-dependent boost to make strong lights produce larger
        // specular highlights.
        double rough = 0.04; // slightly larger -> broader highlight
        double alpha = rough * rough;

        // GGX / Trowbridge-Reitz D term
        double a2 = alpha * alpha;
        double denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
        double D = (a2) / (pi * denom * denom + 1e-12);

        // Smith G
        double k = (alpha * alpha) / 2.0;
        double G = 1.0;
        {
            auto geometry_schlick_ggx = [](double NdotX, double k) {
                return NdotX / (NdotX * (1.0 - k) + k);
            };
            G = geometry_schlick_ggx(NdotV, k) * geometry_schlick_ggx(NdotL, k);
        }

        // Fresnel using Schlick with F0 from IOR
        double etai = 1.0;
        double etat = refraction_index;
        if (!rec.front_face) std::swap(etai, etat);
        // compute F0
        double r0 = (etai - etat) / (etai + etat);
        r0 = r0 * r0;
        colour F = schlick_fresnel(std::max(0.0, VdotH), colour((float)r0, (float)r0, (float)r0));

        // Specular BRDF (microfacet). Convert to outgoing radiance contribution.
        // brdf_spec = D * G * F / (4 * NdotV * NdotL)
        // contribution = brdf_spec * Li * NdotL -> simplifies to (D*G*F)/(4*NdotV) * Li
        colour spec_term = F * (float)((D * G) / (4.0 * NdotV + 1e-12));

        // Final outgoing radiance: specular term * incoming radiance
        // (No ad-hoc intensity boost; keep energy consistent.)
        return spec_term * Li * (float)NdotL;
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
            if (alpha_tex) return alpha_tex->alpha_at(hrec.u, hrec.v, hrec.p);
            if (base_tex) return base_tex->alpha_at(hrec.u, hrec.v, hrec.p);
            return 1.0;
        };

        double a_res = resolve_alpha(rec);
        if (alpha_double_sided || rec.front_face) {
            if (a_res < alpha_cutoff) {
                // Continue ray through transparent mask
                scattered = ray(rec.p + r_in.direction() * 0.001, r_in.direction(), r_in.time());
                attenuation = colour(1.0, 1.0, 1.0);
                return true;
            }
        }

        // ----------------
        // Fetch parameters
        // ----------------
        colour baseColor = base_tex
            ? base_tex->value(rec.u, rec.v, rec.p)
            : colour(1,1,1);

        double metallic = metallic_tex
        ? luminance(metallic_tex->value(rec.u, rec.v, rec.p))
        : 0.0;

        double rough = roughness_tex
        ? luminance(roughness_tex->value(rec.u, rec.v, rec.p))
        : 0.5;

        metallic = clamp01(metallic);
        rough    = clamp01(rough);

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
            N = unit_vector(
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
        // Simple heuristic: more metallic = more likely specular
        double spec_prob;
        if (metallic > 0.9) {
            spec_prob = 1.0;
        } else {
            spec_prob = 0.25 + 0.7 * metallic;
            spec_prob = clamp01(spec_prob);
        }

        double xi_lobe = random_double();

        vec3 wi;        // scattered direction
        colour weight;  // BRDF-ish weight used as attenuation

        if (xi_lobe < spec_prob) {
            // 1. Generate GGX Sample
            double xi1 = random_double();
            double xi2 = random_double();
            vec3 h_local = sample_ggx_half_vector(alpha, xi1, xi2);
            vec3 h = unit_vector(h_local.x() * T + h_local.y() * B + h_local.z() * N);

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

            // Standard Lambertian diffuse energy is (1 - metallic)
            colour kd = baseColor * (1.0 - metallic);

            // Optional: apply (1 - average(F0)) factor for more correct energy
            double F0_avg = (F0.x() + F0.y() + F0.z()) / 3.0;
            kd *= (1.0 - F0_avg);

            weight = kd / std::max(0.05, 1.0 - spec_prob);
        }

        scattered   = ray(rec.p, wi, r_in.time());
        attenuation = weight;

        return true;
    }

    // Proper PBR direct shading using GGX microfacet BRDF (energy-conserving)
    virtual colour shade_direct(const hit_record& rec, const vec3& V, const vec3& Ldir, const colour& Li) const override {
        // Alpha mask: if present and transparent, contribute nothing
        // Alpha mask: skip contribution if transparent (alpha map or albedo alpha)
        double a_ds = 1.0;
        if (alpha_tex) a_ds = alpha_tex->alpha_at(rec.u, rec.v, rec.p);
        else if (base_tex) a_ds = base_tex->alpha_at(rec.u, rec.v, rec.p);
        if (alpha_double_sided || rec.front_face) {
            if (a_ds < alpha_cutoff) return colour(0,0,0);
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
        N = unit_vector(N * strength + Ngeom * (1.0 - strength));
        vec3 L = unit_vector(Ldir);
        vec3 Vn = unit_vector(V);

        double NdotL = std::max(0.0, dot(N, L));
        double NdotV = std::max(0.0, dot(N, Vn));
        if (NdotL <= 0.0 || NdotV <= 0.0) return colour(0,0,0);

        // Fetch material parameters at this shading point
        colour baseColor = base_tex ? base_tex->value(rec.u, rec.v, rec.p) : colour(1,1,1);
        double metallic = metallic_tex ? luminance(metallic_tex->value(rec.u, rec.v, rec.p)) : 0.0;
        double rough = roughness_tex ? luminance(roughness_tex->value(rec.u, rec.v, rec.p)) : 0.5;
        metallic = clamp01(metallic);
        rough    = clamp01(rough);
        double alpha = perceptual_to_alpha(rough);

        // F0 mix between dielectric F0 and baseColor for metals
        colour F0 = (vec3(1.0,1.0,1.0) - vec3(metallic, metallic, metallic)) * dielectric_F0
                    + vec3(metallic, metallic, metallic) * baseColor;

        // Half vector
        vec3 H = unit_vector(L + Vn);
        double NdotH = std::max(1e-6, dot(N, H));
        double VdotH = std::max(1e-6, dot(Vn, H));

        // Microfacet D term (GGX)
        double a2 = alpha * alpha;
        double denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
        double D = (a2) / (pi * denom * denom + 1e-12);

        // Smith G (Schlick-GGX)
        double k = (alpha * alpha) / 2.0;
        auto geometry_schlick = [&](double NdotX) {
            return NdotX / (NdotX * (1.0 - k) + k);
        };
        double G = geometry_schlick(NdotV) * geometry_schlick(NdotL);

        // Fresnel
        colour F = schlick_fresnel(std::max(0.0, VdotH), F0);

        // Specular BRDF value
        colour spec = F * (float)((D * G) / (4.0 * NdotV * NdotL + 1e-12));

        // Diffuse term (energy-conserving): (1 - metallic) * baseColor * (1 - Favg) / pi
        double Favg = (F0.x() + F0.y() + F0.z()) / 3.0;
        colour kd = baseColor * (1.0 - metallic);
        kd *= (1.0 - Favg);

        colour brdf = kd * (float)(1.0 / pi) + spec;

        // Final outgoing radiance: f * Li * cosθ
        return brdf * Li * (float)NdotL;
    }

        // Mask test for triangle-level discard
        virtual bool is_masked_transparent(const hit_record& rec) const override {
            double a = 1.0;
            if (alpha_tex) a = alpha_tex->alpha_at(rec.u, rec.v, rec.p);
            else if (base_tex) a = base_tex->alpha_at(rec.u, rec.v, rec.p);
            if (alpha_double_sided || rec.front_face) {
                return a < alpha_cutoff;
            }
            return false;
        }
};


#endif // MATERIAL_H