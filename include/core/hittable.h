#ifndef HITTABLE_H
#define HITTABLE_H\

#include "aabb.h"

class material;

class hit_record{

    public:
        point3 p;
        vec3 normal;
        shared_ptr<material> mat;
        double t;
        double u;
        double v;
        bool front_face;

        vec3 tangent;
        vec3 bitangent;

        void set_face_normal(const ray& r, const vec3& outward_normal){

            front_face = dot(r.direction(), outward_normal) < 0;
            normal = front_face ? outward_normal : -outward_normal;
        }
};

class hittable{

    public:
        virtual ~hittable() = default;
        virtual bool hit(const ray& r, interval ray_t, hit_record& rec) const = 0;
        virtual aabb bounding_box() const = 0;
};

class transform : public hittable {
    public:
        transform(shared_ptr<hittable> p,
                const vec3& translate,
                const vec3& rotate_deg,
                double      scale = 1.0)
            : ptr(p), t(translate)
        {
            // --- Declare rotation matrices ---
            double Rx[3][3], Ry[3][3], Rz[3][3];
            double Rtemp[3][3];
            double R[3][3];       // <-- now defined before use

            // --- Build rotation matrices from Euler angles ---

            double rx = degrees_to_radians(rotate_deg.x());
            double ry = degrees_to_radians(rotate_deg.y());
            double rz = degrees_to_radians(rotate_deg.z());

            double cx = std::cos(rx), sx = std::sin(rx);
            double cy = std::cos(ry), sy = std::sin(ry);
            double cz = std::cos(rz), sz = std::sin(rz);

            // Rotation X
            Rx[0][0] = 1;  Rx[0][1] = 0;   Rx[0][2] = 0;
            Rx[1][0] = 0;  Rx[1][1] = cx;  Rx[1][2] = -sx;
            Rx[2][0] = 0;  Rx[2][1] = sx;  Rx[2][2] = cx;

            // Rotation Y
            Ry[0][0] = cy; Ry[0][1] = 0;   Ry[0][2] = sy;
            Ry[1][0] = 0;  Ry[1][1] = 1;   Ry[1][2] = 0;
            Ry[2][0] = -sy;Ry[2][1] = 0;   Ry[2][2] = cy;

            // Rotation Z
            Rz[0][0] = cz; Rz[0][1] = -sz; Rz[0][2] = 0;
            Rz[1][0] = sz; Rz[1][1] =  cz; Rz[1][2] = 0;
            Rz[2][0] = 0;  Rz[2][1] =  0;  Rz[2][2] = 1;

            // --- Combine: R = Rz * (Ry * Rx) ---
            mat_mul(Ry, Rx, Rtemp);
            mat_mul(Rz, Rtemp, R);

            // --- Build transform matrices M (forward) and M_inv (inverse) ---
            double s = scale;
            double inv_s = (s == 0) ? 1.0 : 1.0 / s;

            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) {
                    M[i][j]     = R[i][j] * s;       // scale then rotate
                    M_inv[i][j] = R[j][i] * inv_s;   // inverse = R^T * (1/s)
                }

            // --- Build world-space bounding box via 8 corner transform ---
            aabb box_local = ptr->bounding_box();
            vec3 minp( infinity,  infinity,  infinity);
            vec3 maxp(-infinity, -infinity, -infinity);

            for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
            {
                double x = i ? box_local.x.max : box_local.x.min;
                double y = j ? box_local.y.max : box_local.y.min;
                double z = k ? box_local.z.max : box_local.z.min;

                vec3 p_local(x,y,z);
                vec3 p_world = apply(M, p_local) + t;

                minp = min(minp, p_world);
                maxp = max(maxp, p_world);
            }

            bbox_world = aabb(minp, maxp);
        }

        bool hit(const ray& r_in, interval t_range, hit_record& rec) const override {
            // Transform ray into object space
            vec3 origin_local    = apply(M_inv, r_in.origin() - t);
            vec3 direction_local = apply(M_inv, r_in.direction());

            ray r_local(origin_local, direction_local, r_in.time());

            if (!ptr->hit(r_local, t_range, rec))
                return false;

            // Transform point
            rec.p = apply(M, rec.p) + t;

            // Transform frame
            vec3 n_world = unit_vector(apply(M, rec.normal));
            vec3 t_world = unit_vector(apply(M, rec.tangent));
            vec3 b_world = unit_vector(apply(M, rec.bitangent));

            rec.normal   = n_world;
            rec.tangent  = t_world;
            rec.bitangent = b_world;

            // Re-orient to face the ray
            rec.set_face_normal(r_in, rec.normal);

            // Rebuild tangent frame to be orthogonal to the final normal
            vec3 N = rec.normal;
            rec.tangent = rec.tangent - dot(rec.tangent, N) * N;
            if (rec.tangent.length_squared() < 1e-6) {
                vec3 up = (std::fabs(N.y()) < 0.999) ? vec3(0,1,0) : vec3(1,0,0);
                rec.tangent = cross(up, N);
            }
            rec.tangent   = unit_vector(rec.tangent);
            rec.bitangent = cross(N, rec.tangent);

            return true;
        }

        aabb bounding_box() const override {
            return bbox_world;
        }

    private:
        shared_ptr<hittable> ptr;
        vec3 t;                  // translation
        double M[3][3];          // forward (rotation + scale)
        double M_inv[3][3];      // inverse
        aabb bbox_world;

        // Utility: matrix multiply A*B -> out
        static void mat_mul(const double A[3][3], const double B[3][3],
                            double out[3][3])
        {
            for (int i=0;i<3;++i)
                for (int j=0;j<3;++j) {
                    out[i][j] = 0;
                    for (int k=0;k<3;++k)
                        out[i][j] += A[i][k] * B[k][j];
                }
        }

        static vec3 apply(const double M[3][3], const vec3& v) {
            return vec3(
                M[0][0]*v.x() + M[0][1]*v.y() + M[0][2]*v.z(),
                M[1][0]*v.x() + M[1][1]*v.y() + M[1][2]*v.z(),
                M[2][0]*v.x() + M[2][1]*v.y() + M[2][2]*v.z()
            );
        }

        static vec3 min(const vec3& a, const vec3& b) {
            return vec3(std::fmin(a.x(),b.x()), std::fmin(a.y(),b.y()), std::fmin(a.z(),b.z()));
        }

        static vec3 max(const vec3& a, const vec3& b) {
            return vec3(std::fmax(a.x(),b.x()), std::fmax(a.y(),b.y()), std::fmax(a.z(),b.z()));
        }
};

#endif