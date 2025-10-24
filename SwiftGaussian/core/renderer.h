#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include "sokol/sokol_gfx.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Renderer initialization and management
    int init_renderer(int width, int height);
    void render_frame(void);
    void update_swapchain(int width, int height);
    void cleanup_renderer(void);
    bool is_renderer_initialized(void);
    void get_swapchain_dimensions(int *width, int *height);

#if defined(__APPLE__) && TARGET_OS_IOS
// iOS-specific rendering functions
    void render_frame_ios(void *drawable, void *depth_stencil_texture);
    void update_swapchain_ios(int width, int height);
    void *get_ios_swapchain(void);
    void mark_swapchain_needs_update_ios(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // RENDERER_H
