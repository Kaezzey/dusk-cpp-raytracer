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

        scattered = ray(rec.p, direction, r_in.time());
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
    // Base color, metallic, roughness, specular F0, normal map
    shared_ptr<texture> base_tex;
    shared_ptr<texture> metallic_tex;   // greyscale in [0,1]
    shared_ptr<texture> roughness_tex;  // greyscale in [0,1]
    shared_ptr<texture> normal_tex;     // tangent-space normal map
    double normal_strength;

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
        const colour& dielectric_specular = colour(0.04, 0.04, 0.04)
    )
        : dielectric_F0(dielectric_specular),
          base_tex(base_color),
          metallic_tex(metallic),
          roughness_tex(roughness),
          normal_tex(normal_map),
          normal_strength(normal_strength_in)
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

        // ----------------
        // Fetch parameters
        // ----------------
        colour baseColor = base_tex
            ? base_tex->value(rec.u, rec.v, rec.p)
            : colour(1,1,1);

        double metallic = metallic_tex
            ? metallic_tex->value(rec.u, rec.v, rec.p).x()
            : 0.0;
        metallic = clamp01(metallic);

        double rough = roughness_tex
            ? roughness_tex->value(rec.u, rec.v, rec.p).x()
            : 0.5;
        rough = clamp01(rough);

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

        vec3 wo = -unit_vector(r_in.direction()); // view dir in world space

        // ---------------
        // Lobe selection
        // ---------------
        // Simple heuristic: more metallic = more likely specular
        double spec_prob = 0.25 + 0.7 * metallic;  // [0.25, 0.95] roughly
        spec_prob = clamp01(spec_prob);

        double xi_lobe = random_double();

        vec3 wi;        // scattered direction
        colour weight;  // BRDF-ish weight used as attenuation

        if (xi_lobe < spec_prob) {
            // --------------------------
            // Specular GGX reflection
            // --------------------------
            double xi1 = random_double();
            double xi2 = random_double();

            // Sample half-vector in local coordinates (around +Z)
            vec3 h_local = sample_ggx_half_vector(alpha, xi1, xi2);

            // Transform to world space using the same T,B,N basis
            vec3 h = unit_vector(
                  h_local.x() * T
                + h_local.y() * B
                + h_local.z() * N
            );

            // Reflect wo about h
            wi = reflect(-wo, h);

            // If below the surface, kill the ray
            if (dot(wi, N) <= 0.0) {
                return false;
            }

            double cosTheta = std::max(0.0, dot(wi, N));
            colour F = schlick_fresnel(std::max(0.0, dot(h, wo)), F0);

            // This is not a fully correct BRDF/pdf ratio, but good enough visually:
            colour spec_col = F;
            spec_col *= 1.0 / std::max(0.05, spec_prob); // roughly compensate lobe prob

            weight = spec_col;
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
};

#endif // MATERIAL_H