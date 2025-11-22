#ifndef SPHERE_H
#define SPHERE_H

#include "hittable.h"

class sphere : public hittable {

    public:
        ray centre;
        double radius;
        shared_ptr<material> mat;
        aabb bbox;

        //Stationary sphere constructor
        sphere(const point3& static_centre, double radius, shared_ptr<material> mat) : centre(static_centre, vec3(0, 0, 0), 0.0), radius(std::fmax(0, radius)), mat(mat) {

            auto rvec = vec3(radius, radius, radius);
            bbox = aabb(static_centre - rvec, static_centre + rvec);
        }

        //Moving sphere constructor
        sphere(const point3& centre0, const point3& centre1, double radius, shared_ptr<material> mat) : centre(centre0, (centre1 - centre0), 0.0), radius(std::fmax(0, radius)), mat(mat) {

            auto revec = vec3(radius, radius, radius);
            aabb box0(centre.at(0.0) - revec, centre.at(0.0) + revec);
            aabb box1(centre.at(1.0) - revec, centre.at(1.0) + revec);
            bbox = aabb(box0, box1);
        }

        bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
            point3 current_centre = centre.at(r.time());
            vec3 oc = current_centre - r.origin();
            auto a = r.direction().length_squared();
            auto h = dot(r.direction(), oc);
            auto c = oc.length_squared() - radius * radius;
            auto discriminant = h * h - a * c;

            if (discriminant < 0) return false;
            auto sqrtd = std::sqrt(discriminant);

            auto root = (h - sqrtd) / a;
            if (!ray_t.surrounds(root)) {
                root = (h + sqrtd) / a;
                if (!ray_t.surrounds(root)) return false;
            }

            rec.t = root;
            rec.p = r.at(root);
            vec3 outward_normal = (rec.p - current_centre) / radius;
            rec.set_face_normal(r, outward_normal);
            
            // 1. GET UVs
            get_sphere_uv(outward_normal, rec.u, rec.v);

            // 2. FIX: Calculate Analytical Tangents for a Sphere
            // This ensures the Tangent (T) aligns perfectly with the U texture direction
            // Tangent is derivative of position with respect to phi (horizontal)
            // T = (-z, 0, x) derived from spherical coords
            rec.tangent = vec3(-outward_normal.z(), 0.0, outward_normal.x());
            
            if (rec.tangent.length_squared() < 1e-8) {
                rec.tangent = vec3(1,0,0); // Handle poles
            } else {
                rec.tangent = unit_vector(rec.tangent);
            }

            // 3. Bitangent
            rec.bitangent = cross(rec.normal, rec.tangent);

            rec.mat = mat;
            return true;
        }

        aabb bounding_box() const override {
            return bbox;
        }

    private:

        static void get_sphere_uv(const point3& p, double& u, double& v) {
        // p: a given point on the sphere of radius one, centered at the origin.
        // u: returned value [0,1] of angle around the Y axis from X=-1.
        // v: returned value [0,1] of angle from Y=-1 to Y=+1.
        //     <1 0 0> yields <0.50 0.50>       <-1  0  0> yields <0.00 0.50>
        //     <0 1 0> yields <0.50 1.00>       < 0 -1  0> yields <0.50 0.00>
        //     <0 0 1> yields <0.25 0.50>       < 0  0 -1> yields <0.75 0.50>

        auto theta = std::acos(-p.y());
        auto phi = std::atan2(-p.z(), p.x()) + pi;

        u = phi / (2*pi);
        v = theta / pi;
    }
        
};

#endif 