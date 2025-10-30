#ifndef SPHERE_H
#define SPHERE_H

#include "hittable.h"

class sphere : public hittable {

    public:
        sphere(const point3& centre, double radius) : centre(centre), radius(std::fmax(0, radius)) {}

        bool hit(const ray& r, double t_min, double t_max, hit_record& rec) const override {

            //vector from ray origin to sphere centre
            vec3 oc = centre - r.origin();

            //quadratic formula components
            auto a = r.direction().length_squared();
            auto h = dot(r.direction(), oc);
            auto c = oc.length_squared() - radius * radius;

            //discriminant
            auto discriminant = h * h - a * c;

            //if no real roots, ray misses sphere
            if (discriminant < 0) {
                return false;
            }

            auto sqrtd = std::sqrt(discriminant);

            //find nearest root in acceptable range
            auto root = (h - sqrtd) / a;

            //check if root is within t_min and t_max
            if (root <= t_min || t_max <= root) {

                //try the other root
                root = (h + sqrtd) / a;

                //check if this root is within t_min and t_max
                if (root <= t_min || t_max <= root) {
                    return false;
                }
            }

            //record hit information
            rec.t = root;
            rec.p = r.at(rec.t);
            vec3 outward_normal = (rec.p - centre) / radius;
            rec.set_face_normal(r, outward_normal);

            return true;
        }

    private:
        point3 centre;
        double radius;
};

#endif 