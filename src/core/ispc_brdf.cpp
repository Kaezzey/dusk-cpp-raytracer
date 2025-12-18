#include "ispc_brdf.h"
#include <cmath>
#include <cstring>

#if defined(HAVE_ISPC)
extern "C" void normalize_batch(float* xyz_inout, int count);
#endif

void ispc_normalize_batch(float* xyz_inout, int count) {
#if defined(HAVE_ISPC)
    // Call ISPC-exported function with (pointer, int) signature
    normalize_batch(xyz_inout, count);
#else
    // Fallback CPU implementation
    for (int i = 0; i < count; ++i) {
        float* p = xyz_inout + 3*i;
        float x = p[0], y = p[1], z = p[2];
        float len = std::sqrt(x*x + y*y + z*z);
        if (len > 1e-12f) {
            float inv = 1.0f / len;
            p[0] = x * inv;
            p[1] = y * inv;
            p[2] = z * inv;
        }
    }
#endif
}
