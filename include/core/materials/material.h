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

    //diffuse "base color" hook (used later for direct lighting)
    virtual colour albedo(const hit_record& rec) const {
        return colour(1,1,1);
    }
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
        // Sample attenuation from texture
        attenuation = tex->value(rec.u, rec.v, rec.p);

        double ri = rec.front_face ? (1.0 / refraction_index) : refraction_index;

        vec3 unit_direction = unit_vector(r_in.direction());
        double cos_theta = std::fmin(dot(-unit_direction, rec.normal), 1.0);
        double sin_theta = std::sqrt(1.0 - cos_theta*cos_theta);

        bool cannot_refract = ri * sin_theta > 1.0;
        vec3 direction;

        if (cannot_refract || reflectance(cos_theta, ri) > random_double())
            direction = reflect(unit_direction, rec.normal);
        else
            direction = refract(unit_direction, rec.normal, ri);

        // Small offset to avoid self-intersections when spawning refracted/reflect
        // rays from surfaces (especially important for thin glass and grazing exits).
        // Use the surface normal to push the origin away from the surface in the
        // correct hemisphere — this avoids accidentally nudging the ray back
        // into the surface when the sampled direction points slightly inward.
        const double eps = 1e-4;
        vec3 n = rec.normal;
        vec3 origin_offset = (dot(direction, n) > 0.0) ? (eps * n) : (-eps * n);

        scattered = ray(rec.p + origin_offset, direction, r_in.time());
        return true;
    }

    colour albedo(const hit_record& rec) const override {
        return tex->value(0, 0, 0);
    }

  private:
    double refraction_index;
    shared_ptr<texture> tex;

    static double reflectance(double cosine, double refraction_index) {
        auto r0 = (1 - refraction_index) / (1 + refraction_index);
        r0 = r0*r0;
        return r0 + (1-r0)*std::pow((1 - cosine),5);
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
    // PBR Textures
    shared_ptr<texture> base_tex;
    shared_ptr<texture> metallic_tex;
    shared_ptr<texture> roughness_tex;
    shared_ptr<texture> normal_tex;
    double normal_strength;
    colour dielectric_F0;

public:
    // (A) Constant parameters
    pbr_material(const colour& base_color, double metallic, double roughness,
                 shared_ptr<texture> normal_map = nullptr, double normal_strength_in = 1.0,
                 const colour& dielectric_specular = colour(0.04, 0.04, 0.04))
        : dielectric_F0(dielectric_specular),
          base_tex(make_shared<solid_colour>(base_color)),
          metallic_tex(make_shared<solid_colour>(colour(clamp01(metallic), clamp01(metallic), clamp01(metallic)))),
          roughness_tex(make_shared<solid_colour>(colour(clamp01(roughness), clamp01(roughness), clamp01(roughness)))),
          normal_tex(normal_map),
          normal_strength(normal_strength_in) {}

    // (B) Textured base
    pbr_material(shared_ptr<texture> base_color, double metallic, double roughness,
                 shared_ptr<texture> normal_map, double normal_strength_in = 1.0,
                 const colour& dielectric_specular = colour(0.04, 0.04, 0.04))
        : dielectric_F0(dielectric_specular),
          base_tex(base_color),
          metallic_tex(make_shared<solid_colour>(colour(clamp01(metallic), clamp01(metallic), clamp01(metallic)))),
          roughness_tex(make_shared<solid_colour>(colour(clamp01(roughness), clamp01(roughness), clamp01(roughness)))),
          normal_tex(normal_map),
          normal_strength(normal_strength_in) {}

    // (C) Fully textured
    pbr_material(shared_ptr<texture> base_color, shared_ptr<texture> metallic,
                 shared_ptr<texture> roughness, shared_ptr<texture> normal_map,
                 double normal_strength_in = 1.0, const colour& dielectric_specular = colour(0.04, 0.04, 0.04))
        : dielectric_F0(dielectric_specular),
          base_tex(base_color),
          metallic_tex(metallic),
          roughness_tex(roughness),
          normal_tex(normal_map),
          normal_strength(normal_strength_in) {}

    virtual colour albedo(const hit_record& rec) const override {
        return base_tex ? base_tex->value(rec.u, rec.v, rec.p) : colour(1, 1, 1);
    }

    virtual bool scatter(
        const ray& r_in,
        const hit_record& rec,
        colour& attenuation,
        ray& scattered
    ) const override {
        
        // 1. Fetch Material Parameters
        colour baseColor = base_tex ? base_tex->value(rec.u, rec.v, rec.p) : colour(1, 1, 1);
        
        double metallic = metallic_tex ? metallic_tex->value(rec.u, rec.v, rec.p).x() : 0.0;
        double rough_val = roughness_tex ? roughness_tex->value(rec.u, rec.v, rec.p).x() : 0.5;

        metallic = clamp01(metallic);
        rough_val = clamp01(rough_val);

        // Map perceptual roughness to linear alpha (Disney/Unreal convention: alpha = roughness^2)
        double alpha = rough_val * rough_val;

        // Calculate F0 (Reflectance at normal incidence)
        colour F0 = (vec3(1.0, 1.0, 1.0) - vec3(metallic, metallic, metallic)) * dielectric_F0 
                  + vec3(metallic, metallic, metallic) * baseColor;

        // 2. Normal Mapping with Grazing Angle Damping
        vec3 Ngeom = rec.normal;
        vec3 N = Ngeom;
        vec3 T = rec.tangent;
        vec3 B = rec.bitangent;

        // View direction (pointing out from surface)
        vec3 V = -unit_vector(r_in.direction());

        if (normal_tex) {
            colour n_tex = normal_tex->value(rec.u, rec.v, rec.p);
            vec3 n_tan_raw(2.0 * n_tex.x() - 1.0, 2.0 * n_tex.y() - 1.0, 2.0 * n_tex.z() - 1.0);
            
            vec3 n_tan(n_tan_raw.x() * normal_strength, n_tan_raw.y() * normal_strength, n_tan_raw.z());
            n_tan = unit_vector(n_tan);

            vec3 N_bumpy = unit_vector(n_tan.x() * T + n_tan.y() * B + n_tan.z() * Ngeom);

            // Grazing angle damping to prevent black artifacts
            double NdotV_geom = std::max(0.0, dot(Ngeom, V));
            double strength_factor = std::clamp(NdotV_geom * 5.0, 0.0, 1.0); // Fade in last 20%
            
            // Blend safe geometry normal with bumpy normal
            N = unit_vector(N_bumpy * strength_factor + Ngeom * (1.0 - strength_factor));
        }

        // Ensure N points towards viewer
        if (dot(N, V) < 0) N = -N;

        double NdotV = std::max(0.0001, dot(N, V));

        // 3. Fresnel-based Lobe Selection Probability
        colour F_view = schlick_fresnel(NdotV, F0);
        double F_avg = (F_view.x() + F_view.y() + F_view.z()) / 3.0;
        
        // Probability to sample specular lobe
        double spec_prob = std::clamp(F_avg, 0.05, 0.95);
        if (metallic > 0.5) spec_prob = std::max(spec_prob, metallic); // Metals are mostly specular

        // 4. Sampling
        if (random_double() < spec_prob) {
            // ------------------------------------------
            // SPECULAR LOBE (GGX)
            // ------------------------------------------
            
            // Sample Half Vector H
            double xi1 = random_double();
            double xi2 = random_double();
            vec3 h_local = sample_ggx_half_vector(alpha, xi1, xi2);
            
            // Transform H to World Space
            vec3 H = unit_vector(h_local.x() * T + h_local.y() * B + h_local.z() * N);
            
            // Reflected Vector L
            vec3 L = reflect(-V, H);

            // --- Horizon Lifting Fix (Your implementation) ---
            double dot_geom = dot(L, Ngeom);
            double geometry_shadowing = 1.0;
            if (dot_geom < 0.0) {
                L = unit_vector(L - (dot_geom * Ngeom)); // Skid along surface
                double lift_amount = -dot_geom;
                geometry_shadowing = std::clamp(1.0 - (rough_val * lift_amount), 0.0, 1.0);
            }
            // -----------------------------------------------

            double NdotL = std::max(0.0001, dot(N, L));
            double NdotH = std::max(0.0001, dot(N, H));
            double VdotH = std::max(0.0001, dot(V, H));

            if (NdotL <= 0.0 || dot(L, N) <= 0.0) return false;

            // Geometry Term (Smith)
            // For Path Tracing, k = alpha^2 / 2
            double k = (alpha * alpha) / 2.0;
            double G = geometry_smith(N, V, L, k);

            // Fresnel (recalculated with actual H)
            colour F = schlick_fresnel(VdotH, F0);

            // Specular Weight Calculation
            // Weight = F * G * (V.H) / (N.V * N.H)
            colour spec_weight = F * G * VdotH / (NdotV * NdotH);

            // Apply Horizon Shadowing & PDF Compensation
            attenuation = spec_weight * geometry_shadowing / spec_prob;
            scattered = ray(rec.p, L, r_in.time());

        } else {
            // ------------------------------------------
            // DIFFUSE LOBE (Lambertian)
            // ------------------------------------------
            
            // Cosine weighted sampling
            vec3 d_local = sample_cosine_hemisphere(random_double(), random_double());
            vec3 L = unit_vector(d_local.x() * T + d_local.y() * B + d_local.z() * N);

            // Calculate Diffuse Color (kD)
            // Conservation of energy: Diffuse is whatever isn't reflected
            colour kD = (vec3(1.0, 1.0, 1.0) - F_view) * (1.0 - metallic);
            
            // Lambertian Weight = Albedo
            colour diff_weight = kD * baseColor;

            // Apply PDF Compensation
            attenuation = diff_weight / (1.0 - spec_prob);
            scattered = ray(rec.p, L, r_in.time());
        }

        return true;
    }

private:
    // Schlick Fresnel approximation
    colour schlick_fresnel(double cosine, const colour& f0) const {
        return f0 + (colour(1,1,1) - f0) * pow(1.0 - cosine, 5.0);
    }

    // Smith Geometry Function (G)
    // Combines shadowing (V) and masking (L)
    double geometry_smith(const vec3& N, const vec3& V, const vec3& L, double k) const {
        double NdotV = std::max(0.0, dot(N, V));
        double NdotL = std::max(0.0, dot(N, L));
        return geometry_schlick_ggx(NdotV, k) * geometry_schlick_ggx(NdotL, k);
    }

    double geometry_schlick_ggx(double NdotX, double k) const {
        return NdotX / (NdotX * (1.0 - k) + k);
    }

    // GGX Importance Sampling
    // Returns a Half-Vector (H) in tangent space
    vec3 sample_ggx_half_vector(double alpha, double xi1, double xi2) const {
        double phi = 2.0 * 3.14159265359 * xi1;
        double cos_theta = sqrt((1.0 - xi2) / (1.0 + (alpha * alpha - 1.0) * xi2));
        double sin_theta = sqrt(1.0 - cos_theta * cos_theta);

        return vec3(
            sin_theta * cos(phi),
            sin_theta * sin(phi),
            cos_theta
        );
    }

    // Cosine Weighted Hemisphere Sampling
    // Returns a Light Vector (L) in tangent space
    vec3 sample_cosine_hemisphere(double xi1, double xi2) const {
        double r = sqrt(xi1);
        double phi = 2.0 * 3.14159265359 * xi2;
        
        double x = r * cos(phi);
        double y = r * sin(phi);
        double z = sqrt(std::max(0.0, 1.0 - xi1));

        return vec3(x, y, z);
    }

    // Helper utilities
    double luminance(const colour& c) const {
        return dot(c, vec3(0.2126, 0.7152, 0.0722));
    }

    double clamp01(double x) const {
        return x < 0 ? 0 : (x > 1 ? 1 : x);
    }
};

#endif // MATERIAL_H