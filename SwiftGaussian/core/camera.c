#include "camera.h"
#include "utils/logger.h"
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>

Camera *camera_create(void)
{
    Camera *camera = (Camera *)malloc(sizeof(Camera));
    if (!camera)
    {
        printf("ERROR: Failed to allocate memory for camera");
        return NULL;
    }

    // Initialize camera with default values
    camera->position = HMM_V3(0.0f, 0.0f, 1.0f);
    camera->target = HMM_V3(0.0f, 0.0f, 0.0f);
    camera->up = HMM_V3(0.0f, 1.0f, 0.0f);

    // Initialize camera controls
    camera->yaw = 0.0f;
    camera->pitch = 0.0f;
    camera->sensitivity = 0.005f;
    camera->lastX = 0.0f;
    camera->lastY = 0.0f;
    camera->firstTouch = true;

    // Initialize camera properties
    camera->radius = 1.0f;
    camera->fov = 45.0f;
    camera->nearPlane = 0.1f;
    camera->farPlane = 1500.0f;

    printf("Camera created successfully");
    return camera;
}

void camera_destroy(Camera *camera)
{
    if (camera)
    {
        free(camera);
        printf("Camera destroyed");
    }
}

void camera_handle_input(Camera *camera, float x, float y)
{
    if (!camera)
        return;

    if (camera->firstTouch)
    {
        camera->lastX = x;
        camera->lastY = y;
        camera->firstTouch = false;
        return;
    }

    // Calculate relative movement (delta)
    float dx = x - camera->lastX;
    float dy = camera->lastY - y; // invert Y so dragging up looks up

    // Update last position for next frame
    camera->lastX = x;
    camera->lastY = y;

    // Apply movement with sensitivity (inverted)
    camera->yaw -= dx * camera->sensitivity;
    camera->pitch -= dy * camera->sensitivity;

    // Clamp pitch to prevent over-rotation
    if (camera->pitch > 1.5f)
        camera->pitch = 1.5f;
    if (camera->pitch < -1.5f)
        camera->pitch = -1.5f;
}

void camera_update_position(Camera *camera)
{
    if (!camera)
        return;

    // Calculate camera position based on spherical coordinates
    float theta = camera->yaw; // horizontal angle (around Y axis)
    float phi = camera->pitch; // vertical angle

    camera->position.X = camera->radius * cosf(phi) * sinf(theta);
    camera->position.Y = camera->radius * sinf(phi);
    camera->position.Z = camera->radius * cosf(phi) * cosf(theta);
}

HMM_Mat4 camera_get_view_matrix(Camera *camera)
{
    if (!camera)
    {
        return HMM_M4D(1.0f); // return identity matrix
    }

    // Update position before calculating view matrix
    camera_update_position(camera);

    return HMM_LookAt_RH(camera->position, camera->target, camera->up);
}

HMM_Mat4 camera_get_projection_matrix(Camera *camera, float aspect_ratio)
{
    if (!camera)
    {
        return HMM_M4D(1.0f); // return identity matrix
    }

    return HMM_Perspective_RH_NO(camera->fov, aspect_ratio, camera->nearPlane, camera->farPlane);
}

void camera_set_radius(Camera *camera, float radius)
{
    if (camera)
    {
        camera->radius = radius;
        if (camera->radius < 0.1f)
            camera->radius = 0.1f;
        if (camera->radius > 300.0f)
            camera->radius = 300.0f;
    }
}

void camera_set_fov(Camera *camera, float fov)
{
    if (camera)
    {
        camera->fov = fov;
    }
}

void camera_set_sensitivity(Camera *camera, float sensitivity)
{
    if (camera)
    {
        camera->sensitivity = sensitivity;
    }
}

void camera_reset_orientation(Camera *camera)
{
    if (camera)
    {
        camera->yaw = 0.0f;
        camera->pitch = 0.0f;
        camera->firstTouch = true;
    }
}

void camera_reset_touch_state(Camera *camera)
{
    if (camera)
    {
        camera->firstTouch = true;
    }
}

void camera_handle_pinch(Camera *camera, float factor)
{
    if (camera)
    {
        float new_radius = camera->radius / factor;
        camera_set_radius(camera, new_radius);
    }
}
