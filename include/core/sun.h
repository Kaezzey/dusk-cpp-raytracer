#ifndef SUN_H
#define SUN_H

#include "hittable_list.h"
#include "sphere.h"

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

inline double deg_to_rad(double deg) {
    return deg * pi / 180.0;
}

// Convert elevation + azimuth to a 3D unit direction.
// elevation: angle above horizon (0..90 deg)
// azimuth: rotation around Y axis (0 = +X, 90 = +Z)
inline vec3 sun_direction_from_angles(double elevation_deg, double azimuth_deg) {
    double el = deg_to_rad(elevation_deg);
    double az = deg_to_rad(azimuth_deg);

    double x = std::cos(el) * std::cos(az);
    double y = std::sin(el);
    double z = std::cos(el) * std::sin(az);

    return unit_vector(vec3(x, y, z));
}

// ------------------------------------------------------------
// make_sun(): world.add(make_sun(...))
// ------------------------------------------------------------
//
// sun_dir_in  : FROM scene TOWARD the sun
// sun_angle   : angular radius in degrees (e.g. 0.27 = realistic,
//                higher for softer shadows)
// radiance    : colour/intensity
// distance    : where to place the sun sphere
//
inline shared_ptr<hittable> make_sun(
    const vec3& sun_dir_in,
    double sun_angle_deg,
    const colour& radiance,
    double distance = 100.0
) {
    vec3 sun_dir = unit_vector(sun_dir_in);

    // Position in opposite direction of light
    point3 center = sun_dir * distance;

    // Convert angular radius to sphere radius
    double theta  = deg_to_rad(sun_angle_deg);
    double radius = distance * std::tan(theta);

    auto emit_mat = make_shared<diffuse_light>(radiance);

    return make_shared<sphere>(center, radius, emit_mat);
}

#endif // SUN_H
