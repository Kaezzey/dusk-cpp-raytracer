#ifndef TRIANGLE_H
#define TRIANGLE_H

#include "hittable.h"
#include "BVH.h"
#include "AABB.h"
#include "vec3.h"

inline void compute_triangle_tangent_bitangent(
    const point3& p0, const point3& p1, const point3& p2,
    double u0, double v0,
    double u1, double v1,
    double u2, double v2,
    vec3& tangent,
    vec3& bitangent
) {
    vec3 edge1 = p1 - p0;
    vec3 edge2 = p2 - p0;

    double du1 = u1 - u0;
    double dv1 = v1 - v0;
    double du2 = u2 - u0;
    double dv2 = v2 - v0;

    double det = du1 * dv2 - dv1 * du2;
    if (fabs(det) < 1e-8) {
        // Degenerate UV mapping – fall back to something arbitrary.
        tangent   = vec3(1, 0, 0);
        bitangent = vec3(0, 1, 0);
        return;
    }

    double r = 1.0 / det;

    tangent   = (edge1 * dv2 - edge2 * dv1) * r;
    bitangent = (edge2 * du1 - edge1 * du2) * r;
}

class triangle : public hittable {
public:
    // Positions
    point3 v0, v1, v2;

    // Vertex normals (can be flat or smooth; required)
    vec3   n0, n1, n2;

    // UVs
    double u0, v0_uv;
    double u1, v1_uv;
    double u2, v2_uv;

    // Vertex tangents / bitangents (for TBN, normal maps)
    vec3   t0, t1, t2;
    vec3   b0, b1, b2;

    shared_ptr<material> mat;
    aabb bbox;

public:
    // Constructor with full per-vertex data (recommended path)
    triangle(
        const point3& v0, const point3& v1, const point3& v2,
        const vec3&   n0, const vec3&   n1, const vec3&   n2,
        double u0, double v0_uv,
        double u1, double v1_uv,
        double u2, double v2_uv,
        const vec3&   t0, const vec3&   t1, const vec3&   t2,
        const vec3&   b0, const vec3&   b1, const vec3&   b2,
        shared_ptr<material> mat
    )
        : v0(v0), v1(v1), v2(v2),
          n0(n0), n1(n1), n2(n2),
          u0(u0), v0_uv(v0_uv),
          u1(u1), v1_uv(v1_uv),
          u2(u2), v2_uv(v2_uv),
          t0(t0), t1(t1), t2(t2),
          b0(b0), b1(b1), b2(b2),
          mat(mat)
    {
        compute_bbox();
    }

    // Convenience ctor: per-vertex position + normal + UV, but single face T/B
    // (useful if you only compute tangents/bitangents per triangle)
    triangle(
        const point3& v0, const point3& v1, const point3& v2,
        const vec3&   n0, const vec3&   n1, const vec3&   n2,
        double u0, double v0_uv,
        double u1, double v1_uv,
        double u2, double v2_uv,
        const vec3&   face_tangent,
        const vec3&   face_bitangent,
        shared_ptr<material> mat
    )
        : v0(v0), v1(v1), v2(v2),
          n0(n0), n1(n1), n2(n2),
          u0(u0), v0_uv(v0_uv),
          u1(u1), v1_uv(v1_uv),
          u2(u2), v2_uv(v2_uv),
          t0(face_tangent), t1(face_tangent), t2(face_tangent),
          b0(face_bitangent), b1(face_bitangent), b2(face_bitangent),
          mat(mat)
    {
        compute_bbox();
    }

    // Minimal ctor: only positions + UV; compute flat normal & face T/B
    // (good for quick tests / simple models)
    triangle(
        const point3& v0, const point3& v1, const point3& v2,
        double u0, double v0_uv,
        double u1, double v1_uv,
        double u2, double v2_uv,
        shared_ptr<material> mat
    )
        : v0(v0), v1(v1), v2(v2),
          u0(u0), v0_uv(v0_uv),
          u1(u1), v1_uv(v1_uv),
          u2(u2), v2_uv(v2_uv),
          mat(mat)
    {
        // Flat geometric normal for all vertices
        vec3 faceN = unit_vector(cross(v1 - v0, v2 - v0));
        n0 = n1 = n2 = faceN;

        // Face tangent/bitangent
        vec3 faceT, faceB;
        compute_triangle_tangent_bitangent(
            v0, v1, v2,
            u0, v0_uv,
            u1, v1_uv,
            u2, v2_uv,
            faceT, faceB
        );
        t0 = t1 = t2 = unit_vector(faceT);
        b0 = b1 = b2 = unit_vector(faceB);

        compute_bbox();
    }

    // Core intersection
    bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
        // Möller–Trumbore

        const vec3 edge1 = v1 - v0;
        const vec3 edge2 = v2 - v0;

        const vec3 pvec = cross(r.direction(), edge2);
        const double det = dot(edge1, pvec);

        // Treat nearly-zero det as no hit (double-sided triangle)
        if (fabs(det) < 1e-8)
            return false;

        const double invDet = 1.0 / det;

        const vec3 tvec = r.origin() - v0;
        const double u = dot(tvec, pvec) * invDet;
        if (u < 0.0 || u > 1.0)
            return false;

        const vec3 qvec = cross(tvec, edge1);
        const double v = dot(r.direction(), qvec) * invDet;
        if (v < 0.0 || u + v > 1.0)
            return false;

        const double t = dot(edge2, qvec) * invDet;
        if (!ray_t.surrounds(t))
            return false;

        rec.t = t;
        rec.p = r.at(t);

        // Barycentric weights (w for v0)
        const double w = 1.0 - u - v;

        // Interpolate UV
        rec.u = w * u0      + u * u1      + v * u2;
        rec.v = w * v0_uv   + u * v1_uv   + v * v2_uv;

        // Interpolate normal (smooth)
        vec3 interpN = unit_vector(
            w * n0 +
            u * n1 +
            v * n2
        );
        rec.set_face_normal(r, interpN);

        // Interpolate tangent and bitangent
        vec3 interpT = w * t0 + u * t1 + v * t2;
        vec3 interpB = w * b0 + u * b1 + v * b2;

        // Orthonormalise once here for safety
        vec3 T = unit_vector(interpT - dot(interpT, rec.normal) * rec.normal);
        vec3 B = cross(rec.normal, T);

        // Fallback if degenerate
        if (T.near_zero() || B.near_zero()) {
            vec3 up = (fabs(rec.normal.y()) < 0.999) ? vec3(0,1,0) : vec3(1,0,0);
            T = unit_vector(cross(up, rec.normal));
            B = cross(rec.normal, T);
        }

        rec.tangent   = T;
        rec.bitangent = B;

        rec.mat = mat;

        return true;
    }

    aabb bounding_box() const override {
        return bbox;
    }

private:
    void compute_bbox() {
        point3 min_p(
            fmin(v0.x(), fmin(v1.x(), v2.x())),
            fmin(v0.y(), fmin(v1.y(), v2.y())),
            fmin(v0.z(), fmin(v1.z(), v2.z()))
        );

        point3 max_p(
            fmax(v0.x(), fmax(v1.x(), v2.x())),
            fmax(v0.y(), fmax(v1.y(), v2.y())),
            fmax(v0.z(), fmax(v1.z(), v2.z()))
        );

        // Small epsilon to avoid zero-thickness boxes
        const double eps = 1e-4;
        min_p = min_p - vec3(eps, eps, eps);
        max_p += vec3(eps, eps, eps);

        bbox = aabb(min_p, max_p);
    }
};

class triangle_mesh : public hittable {
public:
    std::vector<shared_ptr<hittable>> triangles;
    shared_ptr<hittable> accel; // BVH or hittable_list fallback

    triangle_mesh() = default;

    // Build from an existing vector of triangles
    triangle_mesh(std::vector<shared_ptr<hittable>> tris)
        : triangles(std::move(tris))
    {
        build_accel();
    }

    // Add one triangle and (optionally) rebuild later
    void add(const shared_ptr<hittable>& tri) {
        triangles.push_back(tri);
    }

    // Build / rebuild the acceleration structure
    void build_accel() {
        if (triangles.empty()) {
            accel = nullptr;
            return;
        }

        // Wrap in BVH directly
        accel = make_shared<bvh_node>(triangles, 0, triangles.size());
    }

    bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
        if (!accel) return false;
        return accel->hit(r, ray_t, rec);
    }

    aabb bounding_box() const override {
        if (!accel) return aabb();
        return accel->bounding_box();
    }
};

#endif // TRIANGLE_H