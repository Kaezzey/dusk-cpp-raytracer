#ifndef DUSKTRACER_H
#define DUSKTRACER_H

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>

using std::make_shared;
using std::shared_ptr;

//constants
const double infinity = std::numeric_limits<double>::infinity();
const double pi = 3.1415926535897932385;

//utility functions e.g degrees to radians
inline double degrees_to_radians(double degrees) {
    return degrees * pi / 180.0;
}

//include core headers
#include "colour.h"
#include "ray.h"
#include "vec3.h"

#endif