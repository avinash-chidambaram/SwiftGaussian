#ifndef SCENE_H
#define SCENE_H

#include <stdbool.h>
#include <stdint.h>
#include "sokol/sokol_gfx.h"
#include "camera.h"
#include "utils/handmademath.h"
#include "splat_texture.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        HMM_Vec3 min;
        HMM_Vec3 max;
    } BoundingBox;

    // Scene management functions
    int init_scene(void);
    void cleanup_scene(void);
    bool is_scene_initialized(void);

    int parse_spz_data(const uint8_t *decompressed_data, size_t decompressed_size);

    // Scene rendering function
    void render_scene(sg_swapchain swapchain);

    // Input handling functions
    void handle_input(float x, float y);
    void handle_touch_down(float x, float y);
    void handle_touch_up(void);
    void handle_pinch(float factor);

    // Gaussian splat management
    void initialize_gaussian_splats(uint32_t capacity);

    // Optimization functions
    void mark_uniforms_dirty(void);

#ifdef __cplusplus
}
#endif

#endif // SCENE_H
