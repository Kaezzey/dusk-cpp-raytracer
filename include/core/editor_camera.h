#ifndef EDITOR_CAMERA_H
#define EDITOR_CAMERA_H

struct CameraState {
    point3 pos;
    double yaw;    // radians, around Y
    double pitch;  // radians, up/down

    double vfov       = 20.0;
    double aperture   = 0.0;
    double focus_dist = 10.0;

    double move_speed = 1.0;       // units per second
    double mouse_sensitivity = 0.002; // radians per pixel
};

#endif