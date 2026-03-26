#ifndef CAUSTICS_H
#define CAUSTICS_H

#include <algorithm>
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

    // Fixed-radius query around p; return accumulated radiance estimate
    colour query_radius(const point3& p, double radius) const {
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

    // Backward-compatible query: if knn_k > 0 use k-NN, else fixed radius
    colour query(const point3& p, double radius) const {
        if (knn_k > 0) return query_knn(p, knn_k);
        return query_radius(p, radius);
    }

    // k-NN query with cone filter: gather closest k photons and normalize by an effective radius
    colour query_knn(const point3& p, int k) const {
        if (count == 0 || k <= 0) return colour(0,0,0);

        // Collect candidate photons from neighboring buckets
        struct Cand { const photon* ph; double dist2; };
        thread_local std::vector<Cand> cands;
        cands.clear();
        if (cands.capacity() < (size_t)k * 4) {
            cands.reserve((size_t)k * 4);
        }

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
                cands.push_back(Cand{&ph, dist2});
            }
        }

        if (cands.empty()) return colour(0,0,0);

        // Partial sort to get k closest
        int kk = std::min<int>(k, (int)cands.size());
        std::nth_element(cands.begin(), cands.begin()+kk, cands.end(),
                         [](const Cand& a, const Cand& b){ return a.dist2 < b.dist2; });

        // Effective radius = distance to k-th neighbor
        double r2_eff = cands[kk-1].dist2;
        if (r2_eff <= 0.0) r2_eff = 1e-12; // avoid zero
        double r_eff = std::sqrt(r2_eff);

        colour sum(0,0,0);
        for (int i = 0; i < kk; ++i) {
            const photon* ph = cands[i].ph;
            double dist = std::sqrt(cands[i].dist2);
            double weight = 1.0 - (dist / r_eff);
            if (weight < 0.0) weight = 0.0;
            weight = weight * weight;
            sum += ph->power * (float)weight;
        }

        // Normalize by cone kernel integral using r_eff
        double norm = (2.0 / 3.0) * M_PI * r_eff * r_eff;
        return sum * (float)(intensity_scale / norm);
    }

    size_t size() const { return count; }

    // Configuration
    double cell_size = 0.2; // world units; tune per scene scale
    double intensity_scale = 1.0; // global gain applied to queries
    int    knn_k = 64; // k for k-NN queries

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
    int    min_specular_events= 2;       // require N specular events before deposit
    int    knn_k              = 64;      // k-NN k
};

// Build a caustics photon map by emitting photons from sun
inline void build_sun_caustics(const vec3& sun_dir, const colour& sun_radiance,
                               const hittable& world,
                               const caustics_config& cfg,
                               photon_map& out_map)
{
    out_map.clear();
    out_map.intensity_scale = cfg.intensity_scale;
    out_map.knn_k = cfg.knn_k;
    
    // L is direction FROM scene toward sun; we shoot photons in -L direction
    vec3 L = unit_vector(sun_dir);
    vec3 shoot_dir = -L;  // from sun toward scene
    
    // Build orthonormal basis for sampling plane perpendicular to shoot direction
    vec3 w = shoot_dir;
    vec3 a = (std::fabs(w.x()) > 0.1) ? vec3(0,1,0) : vec3(1,0,0);
    vec3 u = unit_vector(cross(a, w));
    vec3 v = cross(w, u);
    
    // Per-photon power: divide sun radiance by photon count
    colour flux = sun_radiance * (float)(1.0 / std::max(1, cfg.photon_count));
    
    // Emit photons from a broad plane covering the scene (e.g., 50x50 world units)
    double plane_size = 50.0;  // adjust based on your scene bounds
    double plane_distance = 100.0;  // distance from origin
    
    int deposited = 0;
    
    for (int i=0; i<cfg.photon_count; ++i) {
        // Random point on plane perpendicular to sun direction
        double ru = (random_double() - 0.5) * plane_size;
        double rv = (random_double() - 0.5) * plane_size;
        
        // Start position: plane center offset in -shoot_dir, then offset by ru, rv
        point3 plane_center = point3(0,0,0) + shoot_dir * -plane_distance;
        point3 origin = plane_center + u * ru + v * rv;
        
        ray r(origin, shoot_dir, 0.0);
        colour throughput = flux;
        bool saw_dielectric = false; // require at least one dielectric event before deposit
        int  spec_events = 0;

        for (int b=0; b<cfg.max_bounces; ++b) {
            hit_record rec;
            if (!world.hit(r, interval(0.001, infinity), rec)) {
                break;
            }

            // If we hit a diffuse surface, deposit and terminate
            if (!rec.mat || (!rec.mat->is_specular())) {
                if (saw_dielectric && spec_events >= cfg.min_specular_events) {
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
            spec_events++;
            
            // Specular: scatter (reflect/refract)
            ray scattered;
            colour atten;
            if (!rec.mat->scatter(r, rec, atten, scattered)) break;
            throughput = throughput * atten;
            if (rec.mat && rec.mat->is_dielectric()) saw_dielectric = true;
            r = scattered;
        }
    }
}

#endif
