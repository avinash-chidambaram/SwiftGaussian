#include "renderer.h"
#include "scene.h"
#include "utils/logger.h"
#include <assert.h>

#ifdef MACOS
#include "sokol/sokol_glue.h"
#endif

static struct
{
    sg_swapchain swapchain;
    bool initialized;
#if defined(__APPLE__) && TARGET_OS_IOS
    bool swapchain_needs_update;
    int last_drawable_width;
    int last_drawable_height;
#endif
} g_renderer_state = {0};

int init_renderer(int width, int height)
{
    if (g_renderer_state.initialized)
    {
        return 1;
    }

    // Initialize the scene first
    if (!init_scene())
    {
        printf("ERROR: Failed to initialize scene");
        return 0;
    }

    // Setup pass action
    // Initialize swap chain - use sglue_swapchain for Metal, manual for others
#ifdef MACOS
    // For Metal, use the swapchain provided by sglue_swapchain
    g_renderer_state.swapchain = sglue_swapchain();

#elif defined(__APPLE__) && TARGET_OS_IOS

    g_renderer_state.swapchain = (sg_swapchain){
        .width = width,
        .height = height,
        .sample_count = 1,
        .color_format = SG_PIXELFORMAT_BGRA8,
        .depth_format = SG_PIXELFORMAT_NONE,
        .metal = {
            .current_drawable = NULL, // Will be set per frame
            .depth_stencil_texture = NULL}};
    printf("initialized renderer for ios");
#else
    // For other backends, create manual swapchain
    g_renderer_state.swapchain = (sg_swapchain){
        .width = width,
        .height = height,
        .sample_count = 4,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL};
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
    g_renderer_state.swapchain_needs_update = false;
    g_renderer_state.last_drawable_width = width;
    g_renderer_state.last_drawable_height = height;
#endif

    g_renderer_state.initialized = true;
    printf("Renderer initialized successfully");
    return 1;
}

void render_frame(void)
{
    if (!g_renderer_state.initialized)
    {
        printf("ERROR: Renderer not initialized");
        return;
    }

    if (!is_scene_initialized())
    {
        printf("ERROR: Scene not initialized");
        return;
    }

#ifdef MACOS
    // For Metal, refresh the swapchain on each frame
    g_renderer_state.swapchain = sglue_swapchain();
#endif

    render_scene(g_renderer_state.swapchain);
}

void update_swapchain(int width, int height)
{
    if (!g_renderer_state.initialized)
    {
        return;
    }

#ifdef MACOS
    // For Metal, refresh the swapchain from sglue
    g_renderer_state.swapchain = sglue_swapchain();
#else
    // For other backends, update manual swapchain
    g_renderer_state.swapchain.width = width;
    g_renderer_state.swapchain.height = height;
#endif
    printf("Swapchain updated to %dx%d", width, height);
}

void cleanup_renderer(void)
{
    if (!g_renderer_state.initialized)
    {
        return;
    }

    // Clean up scene resources
    cleanup_scene();

    g_renderer_state.initialized = false;
    printf("Renderer cleaned up");
}

bool is_renderer_initialized(void)
{
    return g_renderer_state.initialized;
}

void get_swapchain_dimensions(int *width, int *height)
{
    if (width)
        *width = g_renderer_state.swapchain.width;
    if (height)
        *height = g_renderer_state.swapchain.height;
}

#if defined(__APPLE__) && TARGET_OS_IOS
void render_frame_ios(void *drawable, void *depth_stencil_texture)
{
    if (!g_renderer_state.initialized)
    {
        printf("ERROR: Renderer not initialized");
        return;
    }

    if (!is_scene_initialized())
    {
        printf("ERROR: Scene not initialized");
        return;
    }

    if (!drawable)
    {
        printf("ERROR: No drawable provided");
        return;
    }

    // Update the current drawable and depth texture for this frame
    g_renderer_state.swapchain.metal.current_drawable = drawable;
    g_renderer_state.swapchain.metal.depth_stencil_texture = depth_stencil_texture;

    render_scene(g_renderer_state.swapchain);
}

void update_swapchain_ios(int width, int height)
{
    if (!g_renderer_state.initialized)
    {
        return;
    }

    // Check if we need to update the swapchain
    if (g_renderer_state.swapchain_needs_update ||
        width != g_renderer_state.last_drawable_width ||
        height != g_renderer_state.last_drawable_height)
    {

        // Create new swapchain with current size
        g_renderer_state.swapchain = (sg_swapchain){
            .width = width,
            .height = height,
            .sample_count = 1,
            .color_format = SG_PIXELFORMAT_BGRA8,
            .depth_format = SG_PIXELFORMAT_NONE,
            .metal = {
                .current_drawable = NULL, // Will be set per frame
                .depth_stencil_texture = NULL}};

        g_renderer_state.last_drawable_width = width;
        g_renderer_state.last_drawable_height = height;
        g_renderer_state.swapchain_needs_update = false;

        printf("iOS Swapchain updated to %dx%d", width, height);
    }
}

void *get_ios_swapchain(void)
{
    return &g_renderer_state.swapchain;
}

void mark_swapchain_needs_update_ios(void)
{
    g_renderer_state.swapchain_needs_update = true;
}
#endif
