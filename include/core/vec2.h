#ifndef DUSK_VEC2_H
#define DUSK_VEC2_H

#include <cmath>

struct vec2 {
    double e[2];

    vec2() : e{0,0} {}
    vec2(double e0, double e1) : e{e0, e1} {}

    double x() const { return e[0]; }
    double y() const { return e[1]; }

    double u() const { return e[0]; }
    double v() const { return e[1]; }

    double& x() { return e[0]; }
    double& y() { return e[1]; }
};

#endif
