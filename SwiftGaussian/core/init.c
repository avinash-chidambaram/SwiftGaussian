#include "init.h"
#ifdef USE_SOKOL
#define SOKOL_IMPL
#endif // USE_SOKOL

#ifdef ANDROID
#define SOKOL_GLES3
#include <assert.h>
#include "utils/logger.h"
#endif // ANDROID

#ifdef EMSCRIPTEN
#define SOKOL_GLES3
#include <assert.h>
#include "utils/logger.h"

#endif // EMSCRIPTEN

#ifdef USE_SOKOL
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_log.h"
#endif // USE_SOKOL

// PLTFORM SPECIFIC CODE----------------------------------------------

#ifdef ANDROID
void initGpu()
{
#ifdef USE_SOKOL
    sg_environment env;
    env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    env.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    env.defaults.sample_count = 4;
    env.metal.device = NULL;
    env.d3d11.device = NULL;
    env.wgpu.device = NULL;
    sg_desc desc = (sg_desc){
        .environment = env,
        .logger = slog_func,
    };
    sg_setup(&desc);
    assert(sg_isvalid());
#endif // USE_SOKOL
    print("done initial setup");
}

void cleanup_rendering(void)
{
#ifdef USE_SOKOL
    sg_shutdown();
#endif
}

bool is_rendering_initialized(void)
{
#ifdef USE_SOKOL
    return sg_isvalid();
#else
    return false;
#endif
}

#endif // ANDROID

#ifdef EMSCRIPTEN
#ifdef USE_SOKOL
#include "sokol/sokol_fetch.h"
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif

static const char *_emsc_canvas_name = 0;
static int _emsc_sample_count = 4;
static double _emsc_width = 0;
static double _emsc_height = 0;
static GLint _emsc_framebuffer = 0;

enum
{
    EMSC_NONE = 0,
    EMSC_ANTIALIAS = 1
};

/* track CSS element size changes and update the WebGL canvas size */
static EM_BOOL _emsc_size_changed(int event_type, const EmscriptenUiEvent *ui_event, void *user_data)
{
    assert(ui_event != NULL && "_emsc_size_changed: ui_event is NULL");

    (void)event_type;
    (void)ui_event;
    (void)user_data;
    emscripten_get_element_css_size(_emsc_canvas_name, &_emsc_width, &_emsc_height);
    emscripten_set_canvas_element_size(_emsc_canvas_name, _emsc_width, _emsc_height);
    return true;
}

/* initialize WebGL context and canvas */
void emsc_init(const char *canvas_name, int flags)
{
    assert(canvas_name != NULL && "emsc_init: canvas_name is NULL");

    _emsc_canvas_name = canvas_name;
    emscripten_get_element_css_size(canvas_name, &_emsc_width, &_emsc_height);
    emscripten_set_canvas_element_size(canvas_name, _emsc_width, _emsc_height);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, false, _emsc_size_changed);
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx;
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.antialias = flags & EMSC_ANTIALIAS;
    attrs.majorVersion = 2;
    _emsc_sample_count = 4;
    ctx = emscripten_webgl_create_context(canvas_name, &attrs);
    emscripten_webgl_make_context_current(ctx);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *)&_emsc_framebuffer);
}

int emsc_width(void)
{
    return (int)_emsc_width;
}

int emsc_height(void)
{
    return (int)_emsc_height;
}

sg_environment emsc_environment(void)
{
    return (sg_environment){
        .defaults = {
            .color_format = SG_PIXELFORMAT_RGBA8,
            .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
            .sample_count = _emsc_sample_count,
        }};
}

sg_swapchain emsc_swapchain(void)
{
    return (sg_swapchain){
        .width = (int)_emsc_width,
        .height = (int)_emsc_height,
        .sample_count = _emsc_sample_count,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
        .gl = {
            .framebuffer = (uint32_t)_emsc_framebuffer,
        }};
}

void initGpu()
{
    emsc_init("#canvas", EMSC_NONE);

    // setup sokol_gfx
    sg_desc desc = {
        .environment = emsc_environment(),
        .logger.func = slog_func};
    sg_setup(&desc);
    assert(sg_isvalid());
}

void cleanup_rendering(void)
{
#ifdef USE_SOKOL
    sg_shutdown();
#endif
}

bool is_rendering_initialized(void)
{
#ifdef USE_SOKOL
    return sg_isvalid();
#else
    return false;
#endif
}

#endif // USE_SOKOL

#endif // EMSCRIPTEN

#ifdef MACOS
#ifdef USE_SOKOL
#include "sokol/sokol_app.h"
#include "sokol/sokol_glue.h"

void initGpu()
{
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
}

void cleanup_rendering(void)
{
    sg_shutdown();
}
#endif // USE_SOKOL
#endif // MACOS


#if defined(__APPLE__) && TARGET_OS_IOS
#define SOKOL_METAL
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_log.h"
#include <stdio.h>

// Global Metal device reference for iOS , we don't use glue
static void *g_metal_device = NULL;

void initGpuWithMetalDevice(void *metal_device)
{
    g_metal_device = metal_device;

    // Set up Sokol environment for Metal
    sg_environment env = {
        .defaults = {
            .color_format = SG_PIXELFORMAT_BGRA8,
            .depth_format = SG_PIXELFORMAT_NONE,
            .sample_count = 1},
        .metal.device = metal_device};

    sg_desc desc = {
        .environment = env,
        .logger.func = slog_func};

    sg_setup(&desc);

    if (!sg_isvalid())
    {
        printf("Failed to initialize Sokol with Metal device\n");
        return;
    }

    printf("Sokol initialized successfully with Metal backend\n");
}

void *getMetalDevice(void)
{
    return g_metal_device;
}

void initGpu(void)
{
    printf("using ios");
    // For iOS, we need the Metal device to be set first
    // This function should be called after initGpuWithMetalDevice
    if (g_metal_device == NULL)
    {
        // Log error - Metal device not set
        return;
    }
    // Sokol should already be initialized by initGpuWithMetalDevice
}

void cleanup_rendering(void)
{
    sg_shutdown();
    g_metal_device = NULL;
}

bool is_rendering_initialized(void)
{
    return sg_isvalid() && g_metal_device != NULL;
}

#endif // IOS
