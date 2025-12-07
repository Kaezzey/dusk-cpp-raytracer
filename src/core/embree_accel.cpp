#include "../../include/core/embree_accel.h"
#include "../../include/core/triangle.h"
#include "../../include/core/dusktracer.h"

#include <cstdio>

#ifdef HAVE_EMBREE

embree_triangle_accel::embree_triangle_accel(const triangle_mesh& mesh)
{
    device = rtcNewDevice(nullptr);
    scene  = rtcNewScene(device);

    // Tune scene build for higher-quality BVH and compact layout for faster traversal.
    // Use high build quality (better SAH/trees) and compact the resulting scene
    // for improved memory locality. Also enable robust mode to avoid numerical
    // precision issues on some platforms.
#if defined(RTC_BUILD_QUALITY_HIGH)
    rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_HIGH);
#endif
#if defined(RTC_SCENE_FLAG_COMPACT) && defined(RTC_SCENE_FLAG_ROBUST)
    rtcSetSceneFlags(scene, RTC_SCENE_FLAG_COMPACT | RTC_SCENE_FLAG_ROBUST);
#endif

    // Collect triangles and pack them into a single Embree triangle geometry
    std::vector<std::shared_ptr<triangle>> tris;
    tris.reserve(mesh.triangles.size());

    for (size_t i = 0; i < mesh.triangles.size(); ++i) {
        auto tri_h = mesh.triangles[i];
        auto tri_ptr = std::dynamic_pointer_cast<triangle>(tri_h);
        if (tri_ptr) tris.push_back(tri_ptr);
    }

    if (!tris.empty()) {
        // Create one large triangle geometry
        RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);

        struct Vertex { float x,y,z; };
        // allocate vertex buffer for all triangles (3 verts per tri)
        Vertex* verts = (Vertex*)rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, sizeof(Vertex), (unsigned int)(tris.size() * 3));

        // allocate index buffer: one triangle per prim
        struct TriIdx { unsigned int i0,i1,i2; };
        TriIdx* idx = (TriIdx*)rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, sizeof(TriIdx), (unsigned int)tris.size());

        // Fill buffers and per-prim TriangleData
        m_triangle_data.resize(tris.size());
        bool first = true;
        for (size_t t = 0; t < tris.size(); ++t) {
            auto& tp = tris[t];
            // vertices for this triangle
            size_t vbase = t * 3;
            verts[vbase + 0].x = (float)tp->v0.x(); verts[vbase + 0].y = (float)tp->v0.y(); verts[vbase + 0].z = (float)tp->v0.z();
            verts[vbase + 1].x = (float)tp->v1.x(); verts[vbase + 1].y = (float)tp->v1.y(); verts[vbase + 1].z = (float)tp->v1.z();
            verts[vbase + 2].x = (float)tp->v2.x(); verts[vbase + 2].y = (float)tp->v2.y(); verts[vbase + 2].z = (float)tp->v2.z();

            idx[t].i0 = (unsigned int)(vbase + 0);
            idx[t].i1 = (unsigned int)(vbase + 1);
            idx[t].i2 = (unsigned int)(vbase + 2);

            TriangleData td;
            td.v0 = tp->v0; td.v1 = tp->v1; td.v2 = tp->v2;
            td.n0 = tp->n0; td.n1 = tp->n1; td.n2 = tp->n2;
            td.t0 = tp->t0; td.t1 = tp->t1; td.t2 = tp->t2;
            td.b0 = tp->b0; td.b1 = tp->b1; td.b2 = tp->b2;
            td.u0 = tp->u0; td.v0_uv = tp->v0_uv;
            td.u1 = tp->u1; td.v1_uv = tp->v1_uv;
            td.u2 = tp->u2; td.v2_uv = tp->v2_uv;
            td.mat = tp->mat;
            m_triangle_data[t] = std::move(td);

            aabb tb = tp->bounding_box();
            if (first) { m_bbox = tb; first = false; }
            else { m_bbox = aabb(m_bbox, tb); }
        }

        rtcCommitGeometry(geom);
        unsigned int geomID = rtcAttachGeometry(scene, geom);

        // We keep triangle data indexed by primID; geomID is constant for this mesh's scene
        (void)geomID; // geomID may be used for debugging

        rtcReleaseGeometry(geom);
    }

    rtcCommitScene(scene);
}

embree_triangle_accel::~embree_triangle_accel()
{
    if (scene) rtcReleaseScene(scene);
    if (device) rtcReleaseDevice(device);
}

bool embree_triangle_accel::hit(const ray& r, interval ray_t, hit_record& rec) const
{
    if (!scene) return false;

    RTCRayHit rh;
    rh.ray.org_x = (float)r.origin().x();
    rh.ray.org_y = (float)r.origin().y();
    rh.ray.org_z = (float)r.origin().z();
    rh.ray.dir_x = (float)r.direction().x();
    rh.ray.dir_y = (float)r.direction().y();
    rh.ray.dir_z = (float)r.direction().z();
    rh.ray.tnear = (float)ray_t.min;
    rh.ray.tfar  = (float)ray_t.max;
    rh.ray.time = 0.0f;
    rh.ray.mask = -1;
    rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rh.hit.primID = RTC_INVALID_GEOMETRY_ID;

    // Use the appropriate Embree intersection API depending on version.
    // Embree 4 introduced RTCIntersectArguments / rtcInitIntersectArguments.
#if defined(RTC_VERSION_MAJOR) && (RTC_VERSION_MAJOR >= 4)
    struct RTCIntersectArguments args;
    rtcInitIntersectArguments(&args);
    rtcIntersect1(scene, &rh, &args);
#else
    RTCIntersectContext ctx;
    rtcInitIntersectContext(&ctx);
    rtcIntersect1(scene, &ctx, &rh);
#endif

    if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID) return false;

    double t = (double)rh.ray.tfar;
    if (!ray_t.surrounds(t)) return false;

    rec.t = t;
    rec.p = r.at(t);

    // Barycentrics (u,v) available in hit.u / hit.v; w = 1-u-v
    double u = rh.hit.u;
    double v = rh.hit.v;
    double w = 1.0 - u - v;

    // Interpolate texture coordinates (Embree gives barycentrics; we need UVs)
    // Match the behavior of triangle::hit which stores interpolated UVs in rec.u/rec.v
    rec.u = u; rec.v = v; // temporary assignment (will be overridden if we have per-prim data)

    unsigned int prim = rh.hit.primID;
    if (prim < m_triangle_data.size()) {
        const TriangleData& td = m_triangle_data[prim];

        // Interpolate texture coordinates from per-vertex UVs
        double iu = w * td.u0 + u * td.u1 + v * td.u2;
        double iv = w * td.v0_uv + u * td.v1_uv + v * td.v2_uv;
        rec.u = iu; rec.v = iv;

        // Interpolate smooth normal
        vec3 interpN = (td.n0 * (float)w) + (td.n1 * (float)u) + (td.n2 * (float)v);
        interpN = unit_vector(interpN);
        rec.set_face_normal(r, interpN);

        // Interpolate tangent & bitangent and Gram-Schmidt them to normal
        vec3 interpT = (td.t0 * (float)w) + (td.t1 * (float)u) + (td.t2 * (float)v);
        vec3 interpB = (td.b0 * (float)w) + (td.b1 * (float)u) + (td.b2 * (float)v);

        if (!interpT.near_zero()) {
            interpT = interpT - interpN * dot(interpT, interpN);
            interpT = unit_vector(interpT);
        } else {
            vec3 up = (std::fabs(interpN.y()) < 0.999) ? vec3(0,1,0) : vec3(1,0,0);
            interpT = unit_vector(cross(up, interpN));
        }

        if (!interpB.near_zero()) {
            interpB = interpB - interpN * dot(interpB, interpN);
            interpB = unit_vector(interpB);
        } else {
            interpB = cross(interpN, interpT);
        }

        if (interpB.near_zero()) {
            interpB = cross(interpN, interpT);
        }

        rec.tangent = interpT;
        rec.bitangent = interpB;

        rec.mat = td.mat;
        // If material indicates masked transparency at this UV, treat as a miss
        if (rec.mat) {
            hit_record tmp = rec;
            tmp.mat = rec.mat;
            if (rec.mat->is_masked_transparent(tmp)) {
                return false;
            }
        }
    } else {
        // Fallback: use geometric normal from Embree if we didn't store triangle data
        vec3 Ng((double)rh.hit.Ng_x, (double)rh.hit.Ng_y, (double)rh.hit.Ng_z);
        vec3 N = unit_vector(Ng);
        rec.set_face_normal(r, N);
        rec.mat.reset();
    }

    return true;
}

bool embree_triangle_accel::hit_packet(const std::array<ray,4>& rays, const interval& ray_t, std::array<hit_record,4>& out_recs) const
{
    // Default implementation: loop over rays and call single-ray hit().
    // This keeps behavior correct on all builds. Future optimization: replace
    // this loop with Embree's rtcIntersect4/rtcIntersectN packet APIs for
    // real SIMD traversal.
    bool any_hit = false;
    for (int i = 0; i < 4; ++i) {
        hit_record rec;
        bool h = hit(rays[i], ray_t, rec);
        out_recs[i] = rec;
        any_hit = any_hit || h;
    }
    return any_hit;
}

aabb embree_triangle_accel::bounding_box() const
{
    return m_bbox;
}

#else // HAVE_EMBREE

embree_triangle_accel::embree_triangle_accel(const triangle_mesh& mesh)
{
    // Fallback: use existing accel from triangle_mesh
    m_fallback = mesh.accel;
}

embree_triangle_accel::~embree_triangle_accel() = default;

bool embree_triangle_accel::hit(const ray& r, interval ray_t, hit_record& rec) const
{
    if (m_fallback) return m_fallback->hit(r, ray_t, rec);
    return false;
}

aabb embree_triangle_accel::bounding_box() const
{
    if (m_fallback) return m_fallback->bounding_box();
    return aabb();
}

#endif
