#ifndef RAY_H
#define RAY_H

#include "vec3.h"

class ray {
    public:
        //default constructor
        ray() {}

        //parameterized constructor
        ray(const point3& origin, const vec3& direction) : orig(origin), dir(direction) {}

        //getter functions
        const point3& origin() const { return orig; }
        const vec3& direction() const { return dir; }

        //point along the ray at parameter t
        point3 at(double t) const {
            return orig + t*dir;
        }

    //private member variables
    private:
        point3 orig;
        vec3 dir;
};

#endif