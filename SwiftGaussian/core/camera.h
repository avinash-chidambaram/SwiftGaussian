#ifndef CAMERA_H
#define CAMERA_H

#include <stdbool.h>
#include "utils/handmademath.h"

typedef struct
{
    // Camera position and orientation
    HMM_Vec3 position;
    HMM_Vec3 target;
    HMM_Vec3 up;

    // Camera controls
    float yaw;
    float pitch;
    float sensitivity;
    float lastX, lastY;
    bool firstTouch;

    // Camera properties
    float radius;
    float fov;
    float nearPlane;
    float farPlane;
} Camera;

Camera *camera_create(void);
void camera_destroy(Camera *camera);

// Camera control functions
void camera_handle_input(Camera *camera, float x, float y);
void camera_handle_pinch(Camera *camera, float factor);
void camera_update_position(Camera *camera);

// Camera matrix functions
HMM_Mat4 camera_get_view_matrix(Camera *camera);
HMM_Mat4 camera_get_projection_matrix(Camera *camera, float aspect_ratio);

// Camera property getters/setters
void camera_set_radius(Camera *camera, float radius);
void camera_set_fov(Camera *camera, float fov);
void camera_set_sensitivity(Camera *camera, float sensitivity);
void camera_reset_orientation(Camera *camera);
void camera_reset_touch_state(Camera *camera);

#endif // CAMERA_H
