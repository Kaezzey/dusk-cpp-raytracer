#include "../../include/core/AABB.h"    

// These definitions must exist in exactly ONE .cpp file.
const aabb aabb::empty =
    aabb(interval::empty, interval::empty, interval::empty);

const aabb aabb::universe =
    aabb(interval::universe, interval::universe, interval::universe);
