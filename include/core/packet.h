#ifndef PACKET_H
#define PACKET_H

#include "ray.h"
#include "hittable.h"

// Simple 4-wide ray packet for packet traversal.
struct RayPacket4 {
    ray r[4];
    double tmin[4];
    double tmax[4];
    // active lanes: bit i set => lane i is active
    unsigned int active_mask = 0;
};

// Helper: initialize tmin/tmax from packet rays
inline void init_packet_tbounds(RayPacket4& pk, double default_tmin = 0.001, double default_tmax = 1e30) {
    pk.active_mask = 0;
    for (int i = 0; i < 4; ++i) {
        pk.tmin[i] = default_tmin;
        pk.tmax[i] = default_tmax;
        // mark lane active if direction is finite (basic sanity)
        const auto& d = pk.r[i].direction();
        if (std::isfinite(d.x()) && std::isfinite(d.y()) && std::isfinite(d.z()))
            pk.active_mask |= (1u << i);
    }
}

// Per-lane AABB test: returns bitmask of lanes that intersect the box within their bounds.
inline unsigned int aabb_packet_test(const aabb& box, const RayPacket4& pk) {
    unsigned int mask = 0;
    for (int i = 0; i < 4; ++i) {
        if (!(pk.active_mask & (1u << i))) continue;
        if (box.hit(pk.r[i], interval(pk.tmin[i], pk.tmax[i]))) mask |= (1u << i);
    }
    return mask;
}

#endif
