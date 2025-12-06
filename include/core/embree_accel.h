// Embree acceleration wrapper
#pragma once

#include "hittable.h"
// Forward declare triangle_mesh to avoid circular include; implementation file
// includes the full triangle header.
class triangle_mesh;
#include <memory>
#include <vector>
#include <array>

#ifdef HAVE_EMBREE
// Support multiple Embree header layouts (embree3/, embree4/, or direct rtcore.h).
#if defined(__has_include)
#  if __has_include(<embree3/rtcore.h>)
#    include <embree3/rtcore.h>
#  elif __has_include(<embree4/rtcore.h>)
#    include <embree4/rtcore.h>
#  elif __has_include(<rtcore.h>)
#    include <rtcore.h>
#  else
#    error "Embree headers not found (tried embree3/rtcore.h, embree4/rtcore.h, rtcore.h)"
#  endif
#else
// Fallback: prefer embree3 path
#  include <embree3/rtcore.h>
#endif
#endif

// Simple wrapper that builds an Embree scene from a triangle_mesh and
// implements the hittable interface. When Embree is not available, a
// stub is provided that forwards to the triangle_mesh's existing accel.

class embree_triangle_accel : public hittable {
public:
    embree_triangle_accel() = default;
    // Build from a triangle_mesh (copies triangles)
    explicit embree_triangle_accel(const triangle_mesh& mesh);
    ~embree_triangle_accel();

    bool hit(const ray& r, interval ray_t, hit_record& rec) const override;
    // Packet intersection: trace up to 4 rays at once. Implementations may use
    // Embree's packet/intersector APIs when available; default fallback will
    // trace rays individually.
    bool hit_packet(const std::array<ray,4>& rays, const interval& ray_t, std::array<hit_record,4>& out_recs) const;
    aabb bounding_box() const override;

private:
#ifdef HAVE_EMBREE
    RTCDevice device = nullptr;
    RTCScene  scene  = nullptr;
    // Map geometry ID -> material (geomID indexes into this vector)
    // Per-geometry triangle data for interpolation at hit time
    struct TriangleData {
        point3 v0, v1, v2;
        vec3   n0, n1, n2;
        vec3   t0, t1, t2;
        vec3   b0, b1, b2;
        double u0, v0_uv, u1, v1_uv, u2, v2_uv;
        std::shared_ptr<material> mat;
    };
    std::vector<TriangleData> m_triangle_data;
    aabb m_bbox;
#else
    // Fallback: keep a local accel pointer (BVH) to use when Embree is not compiled in.
    std::shared_ptr<hittable> m_fallback;
#endif
};
