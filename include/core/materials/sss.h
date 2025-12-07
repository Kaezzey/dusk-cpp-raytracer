#ifndef SSS_H
#define SSS_H

#include "../vec3.h"
#include <cmath>
#include <algorithm>

// Local PI constant to avoid M_PI portability issues
static constexpr double SSS_PI = 3.14159265358979323846;

// Simple Burley diffusion profile helpers (lightweight approximation)
namespace sss {

// Evaluate a Burley-like diffusion profile Rd(r).
// This is a pragmatic, lightweight approximation useful for initial
// integration. 'albedo' is a luminance-like scalar in [0,1], 'radius'
// controls the falloff (mean free path).
inline double burley_Rd(double r, double albedo, double radius) {
    if (r <= 0.0) r = 1e-6;
    double invR = 1.0 / std::max(1e-6, radius);
    // Simple normalized exponential radial profile scaled by albedo.
    // Normalization chosen so integral over 2D plane ~= albedo.
    double norm = albedo * invR * invR / (2.0 * SSS_PI);
    return norm * std::exp(-r * invR);
}

// Sample radial distance r from an exponential profile with scale 'radius'
// using a uniform u in [0,1). Returns r >= 0.
inline double sample_r_exponential(double radius, double u) {
    u = std::clamp(u, 1e-9, 1.0 - 1e-9);
    return -radius * std::log(1.0 - u);
}

// Area PDF for a radial-exponential sampling scheme where r is drawn from
// p_r(r) = (1/radius) * exp(-r/radius) and theta uniform in [0,2pi).
// The PDF over area (per unit area) is p_area(r) = p_r(r) / (2*pi*r)
inline double pdf_area_from_radius(double r, double radius) {
    if (r <= 0.0) r = 1e-6;
    double invR = 1.0 / std::max(1e-6, radius);
    double pr = invR * std::exp(-r * invR);
    return pr / (2.0 * SSS_PI * r);
}

} // namespace sss

#endif // SSS_H
