#ifndef COLOUR_H
#define COLOUR_H

#include <iostream>
#include <cmath>

#include "vec3.h"

// Alias
using colour = vec3;

// Gamma correction helper (gamma 2.0)
inline double linear_to_gamma(double linear_component) {
    // Guard against NaNs/Infs and negative values. Clamp extremely large
    // values to avoid producing huge gammas that overflow later stages.
    if (!std::isfinite(linear_component) || linear_component <= 0.0) return 0.0;
    const double MAX_LINEAR = 1e6; // allow HDR but bound it
    double v = linear_component;
    if (v > MAX_LINEAR) v = MAX_LINEAR;
    return std::sqrt(v);
}

// Simple clamp to [0.0, 0.999]
// Clamp to [0.0, 0.999], but handle NaN/Inf safely by returning 0.
inline double clamp01_999(double x) {
    if (!std::isfinite(x)) return 0.0;
    if (x < 0.0)   return 0.0;
    if (x > 0.999) return 0.999;
    return x;
}

// Write a colour to an output stream with gamma correction and [0,255] clamp
inline void write_colour(std::ostream& out, const colour& pixel_colour) {
    auto r = pixel_colour.x();
    auto g = pixel_colour.y();
    auto b = pixel_colour.z();

    // Sanitize components (replace NaN/Inf/negatives and clamp extremes),
    // then gamma-correct from linear space.
    r = linear_to_gamma(r);
    g = linear_to_gamma(g);
    b = linear_to_gamma(b);

    int ir = static_cast<int>(256 * clamp01_999(r));
    int ig = static_cast<int>(256 * clamp01_999(g));
    int ib = static_cast<int>(256 * clamp01_999(b));

    out << ir << ' ' << ig << ' ' << ib << '\n';
}

#endif // COLOUR_H