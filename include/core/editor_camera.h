#ifndef EDITOR_CAMERA_H
#define EDITOR_CAMERA_H

#include <cmath>
#include "camera.h"   // for camera, point3, vec3

struct editor_camera_state {
    // Pose
    point3 position = point3(0, 0, 5);
    double yaw      = 0.0;
    double pitch    = 0.0;

    // Lens
    float vfov     = 40.0;

    // World up
    vec3 world_up   = vec3(0, 1, 0);

    // Derived basis
    vec3 forward    = vec3(0, 0, -1);
    vec3 right      = vec3(1, 0, 0);
    vec3 up         = vec3(0, 1, 0);

    // Limits
    double min_pitch = -1.4;
    double max_pitch = +1.4;

    // Tunables
    double mouse_sensitivity = 0.0025;
    double move_speed        = 5.0;

    void update_basis()
    {
        double cp = std::cos(pitch);
        double sp = std::sin(pitch);
        double cy = std::cos(yaw);
        double sy = std::sin(yaw);

        forward = vec3(
            cp * cy,
            sp,
            cp * sy
        );
        forward = unit_vector(forward);

        right = unit_vector(cross(forward, world_up));
        up    = cross(right, forward);
    }

    // Initialise from lookfrom/lookat style
    void set_from_lookat(const point3& pos, const point3& target)
    {
        position = pos;
        vec3 f = unit_vector(target - pos);

        yaw   = std::atan2(f.z(), f.x());
        pitch = std::asin(f.y());

        update_basis();
    }

    void look(double dx, double dy)
    {
        yaw   += dx * mouse_sensitivity;
        pitch += dy * mouse_sensitivity;

        if (pitch < min_pitch) pitch = min_pitch;
        if (pitch > max_pitch) pitch = max_pitch;

        update_basis();
    }

    void move_forward(double amount) { position += forward * amount; }
    void move_right(double amount)   { position += right   * amount; }
    void move_up_axis(double amount) { position += world_up * amount; }

    void move_from_input(double move_forward_axis,
                         double move_right_axis,
                         double move_up_axis_world,
                         double dt,
                         double speed_scale = 1.0)
    {
        double amt = move_speed * speed_scale * dt;

        if (move_forward_axis != 0.0)
            move_forward(move_forward_axis * amt);

        if (move_right_axis != 0.0)
            move_right(move_right_axis * amt);

        if (move_up_axis_world != 0.0)
            move_up_axis(move_up_axis_world * amt);
    }
};

inline void to_shirley_camera(const editor_camera_state& s, camera& cam)
{
    cam.lookfrom = s.position;
    cam.lookat   = s.position + s.forward;
    cam.vup      = s.up;
    cam.vfov     = s.vfov;
}

#endif // EDITOR_CAMERA_H
