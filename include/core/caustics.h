#ifndef CAUSTICS_H
#define CAUSTICS_H

#include <vector>
#include <unordered_map>
#include <cmath>
#include "vec3.h"
#include "ray.h"
#include "scene.h"
#include "hittable.h"
#include "materials/material.h"

struct photon {
    point3 pos;
    vec3   dir;     // incoming light direction at deposit
    colour power;   // flux carried
};

// Simple spatial hash grid photon map for caustics (floor deposits)
class photon_map {
public:
    photon_map() {}

    void clear() { buckets.clear(); count = 0; }

    void insert(const photon& p) {
        auto key = hash_key(p.pos);
        buckets[key].push_back(p);
        ++count;
    }

    // Query photons within radius r around p; return accumulated radiance estimate
    colour query(const point3& p, double radius) const {
        if (count == 0) return colour(0,0,0);
        colour sum(0,0,0);
        double r2 = radius * radius;
        int num_found = 0;
        
        // Search neighboring buckets (3x3x3)
        ivec3 base = cell_coord(p);
        for (int dz=-1; dz<=1; ++dz)
        for (int dy=-1; dy<=1; ++dy)
        for (int dx=-1; dx<=1; ++dx) {
            ivec3 c = ivec3(base.x+dx, base.y+dy, base.z+dz);
            auto it = buckets.find(cell_hash(c));
            if (it == buckets.end()) continue;
            for (const auto& ph : it->second) {
                vec3 d = ph.pos - p;
                double dist2 = dot(d,d);
                if (dist2 <= r2) {
                    // Apply cone filter for smoother falloff
                    double dist = std::sqrt(dist2);
                    double weight = 1.0 - (dist / radius);  // linear falloff
                    weight = weight * weight;  // squared for smoother
                    sum += ph.power * (float)weight;
                    num_found++;
                }
            }
        }
        
        if (num_found == 0) return colour(0,0,0);
        
        // Normalize by filter kernel integral, not just area
        double norm = (2.0 / 3.0) * M_PI * radius * radius;  // cone filter normalization
        return sum * (float)(intensity_scale / norm);
    }

    size_t size() const { return count; }

    // Configuration
    double cell_size = 0.2; // world units; tune per scene scale
    double intensity_scale = 1.0; // global gain applied to queries

private:
    struct ivec3 { int x,y,z; ivec3(int a=0,int b=0,int c=0):x(a),y(b),z(c){} };

    std::unordered_map<size_t, std::vector<photon>> buckets;
    size_t count = 0;

    ivec3 cell_coord(const point3& p) const {
        return ivec3(
            (int)std::floor(p.x() / cell_size),
            (int)std::floor(p.y() / cell_size),
            (int)std::floor(p.z() / cell_size)
        );
    }
    size_t cell_hash(const ivec3& c) const {
        // mix integers
        size_t h = 1469598103934665603ull;
        auto mix = [&](int v){ h ^= (size_t)(v*11400714819323198485ull); h *= 1099511628211ull; };
        mix(c.x); mix(c.y); mix(c.z);
        return h;
    }
    size_t hash_key(const point3& p) const { return cell_hash(cell_coord(p)); }
};

struct caustics_config {
    int    photon_count       = 200000;  // number of sun photons
    double max_bounces        = 8;       // limit for specular path
    double deposit_radius     = 0.05;    // gather radius
    double termination_prob   = 0.0;     // optional RR
    double intensity_scale    = 50.0;    // boost to match sun energy scale
};

// Build a caustics photon map by emitting photons from sun
inline void build_sun_caustics(const vec3& sun_dir, const colour& sun_radiance,
                               const hittable& world,
                               const caustics_config& cfg,
                               photon_map& out_map)
{
    std::fprintf(stderr, "[CAUSTICS::BUILD] Entry\n");
    out_map.clear();
    out_map.intensity_scale = cfg.intensity_scale;
    std::fprintf(stderr, "[CAUSTICS::BUILD] Map cleared, intensity_scale=%.1f\n", cfg.intensity_scale);
    
    // L is direction FROM scene toward sun; we shoot photons in -L direction
    vec3 L = unit_vector(sun_dir);
    vec3 shoot_dir = -L;  // from sun toward scene
    
    std::fprintf(stderr, "[CAUSTICS::BUILD] Shoot direction: (%.3f, %.3f, %.3f)\n", 
        shoot_dir.x(), shoot_dir.y(), shoot_dir.z());
    
    // Build orthonormal basis for sampling plane perpendicular to shoot direction
    vec3 w = shoot_dir;
    vec3 a = (std::fabs(w.x()) > 0.1) ? vec3(0,1,0) : vec3(1,0,0);
    vec3 u = unit_vector(cross(a, w));
    vec3 v = cross(w, u);
    
    // Per-photon power: divide sun radiance by photon count
    colour flux = sun_radiance * (float)(1.0 / std::max(1, cfg.photon_count));
    
    std::fprintf(stderr, "[CAUSTICS::BUILD] Per-photon flux: (%.6f, %.6f, %.6f)\n",
        flux.x(), flux.y(), flux.z());
    
    // Emit photons from a broad plane covering the scene (e.g., 50x50 world units)
    double plane_size = 50.0;  // adjust based on your scene bounds
    double plane_distance = 100.0;  // distance from origin
    
    std::fprintf(stderr, "[CAUSTICS::BUILD] Emission plane: size=%.1f, distance=%.1f\n", 
        plane_size, plane_distance);
    std::fprintf(stderr, "[CAUSTICS::BUILD] Emitting %d photons...\n", cfg.photon_count);
    
    int deposited = 0;
    int hit_specular = 0;
    int missed = 0;
    int progress_interval = cfg.photon_count / 10;
    
    for (int i=0; i<cfg.photon_count; ++i) {
        if (progress_interval > 0 && i % progress_interval == 0) {
            std::fprintf(stderr, "[CAUSTICS::BUILD] Progress: %d/%d photons (%.0f%%), deposited=%d\n",
                i, cfg.photon_count, 100.0 * i / cfg.photon_count, deposited);
        }
        
        // Random point on plane perpendicular to sun direction
        double ru = (random_double() - 0.5) * plane_size;
        double rv = (random_double() - 0.5) * plane_size;
        
        // Start position: plane center offset in -shoot_dir, then offset by ru, rv
        point3 plane_center = point3(0,0,0) + shoot_dir * -plane_distance;
        point3 origin = plane_center + u * ru + v * rv;
        
        ray r(origin, shoot_dir, 0.0);
        colour throughput = flux;
        bool saw_dielectric = false; // require at least one dielectric event before deposit

        for (int b=0; b<cfg.max_bounces; ++b) {
            hit_record rec;
            if (!world.hit(r, interval(0.001, infinity), rec)) {
                missed++;
                break;
            }

            // If we hit a diffuse surface, deposit and terminate
            if (!rec.mat || (!rec.mat->is_specular())) {
                if (saw_dielectric) {
                    photon ph;
                    ph.pos   = rec.p;
                    ph.dir   = unit_vector(r.direction());  // direction photon was traveling
                    ph.power = throughput;
                    out_map.insert(ph);
                    deposited++;
                }
                // Regardless, terminate on diffuse
                break;
            }

            hit_specular++;
            
            // Specular: scatter (reflect/refract)
            ray scattered;
            colour atten;
            if (!rec.mat->scatter(r, rec, atten, scattered)) break;
            throughput = throughput * atten;
            if (rec.mat && rec.mat->is_dielectric()) saw_dielectric = true;
            r = scattered;
        }
    }
    
    std::fprintf(stderr, "[CAUSTICS::BUILD] Complete: deposited=%d, hit_specular=%d, missed=%d\n",
        deposited, hit_specular, missed);
    std::fprintf(stderr, "[CAUSTICS::BUILD] Final photon map size: %zu\n", out_map.size());
    std::fprintf(stderr, "[CAUSTICS::BUILD] Exit\n");
}

#endif
