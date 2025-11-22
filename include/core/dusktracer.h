#ifndef DUSKTRACER_H
#define DUSKTRACER_H

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <thread>
#include <functional>

using std::make_shared;
using std::shared_ptr;

//constants
const double infinity = std::numeric_limits<double>::infinity();
const double pi = 3.1415926535897932385;

//utility functions e.g degrees to radians
inline double degrees_to_radians(double degrees) {
    return degrees * pi / 180.0;
}

// ------------------------------------------------------
//       FAST PER-THREAD RNG (xorshift64*)
// ------------------------------------------------------
inline uint64_t xorshift64star(uint64_t& state) {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 2685821657736338717ULL;
}

inline double random_double() {

    //each thread gets its own RNG state
    thread_local uint64_t rng_state = []{

        //thread-specific seed (hashed)
        uint64_t h = std::hash<std::thread::id>{}(std::this_thread::get_id());
        h ^= 0x9e3779b97f4a7c15ULL;

        //avoid forbidden zero state
        if (h == 0) h = 0x123456789abcdefULL; 

        return h;
    }();

    uint64_t x = xorshift64star(rng_state);

    //convert to double in [0,1)
    const double inv = 1.0 / 9007199254740992.0; // 2^53
    return double(x >> 11) * inv;
}

inline double random_double(double min, double max) {
    return min + (max - min) * random_double();
}

inline int random_int(int min, int max) {
    
    //integer in [min, max]
    return min + int((max - min + 1) * random_double());
}


//include core headers
#include "colour.h"
#include "interval.h"
#include "ray.h"
#include "vec3.h"

#endif