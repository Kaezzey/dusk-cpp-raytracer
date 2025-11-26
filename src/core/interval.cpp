#include "../../include/core/dusktracer.h"
#include "../../include/core/interval.h"

const interval interval::empty    = interval(+infinity, -infinity);
const interval interval::universe = interval(-infinity, +infinity);
