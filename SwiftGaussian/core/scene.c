#include "scene.h"
#include "camera.h"
#include "rendering/splat.glsl.h"
#include "rendering/depth.glsl.h"
#include "rendering/sort.glsl.h"
#include "utils/logger.h"
#include "loader/spzloader.h"
#include "core/splat_texture.h"
#include <assert.h>
#include "utils/handmademath.h"
#include "utils/quaternion.h"
#include <memory.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <float.h>

#ifdef _OPENMP
#include <omp.h>
#endif

static struct
{
    sg_pipeline pip;
    sg_bindings bind;
    sg_pass_action pass_action;
    bool initialized;
    Camera *camera;

    // Splat data as texture
    splat_texture_t splat_texture;

    // Packed Gaussian splat data
    PackedSplat *packed_splats;
    uint32_t splat_count;
    BoundingBox splat_bounds;
    bool splats_initialized;

    // Cached uniforms to avoid per-frame allocations
    vs_params_t vs_params;
    bool uniforms_dirty;

    struct
    {
        sg_buffer depth_buffer;
        sg_buffer index_buffer;
        uint32_t padded_splat_count;

        sg_view depth_buffer_view;
        sg_view index_buffer_view;

        sg_bindings depth_bindings;
        sg_bindings sort_bindings;

        sg_pipeline compute_depth_pip;
        sg_pipeline compute_sort_pip;
    } compute;

} g_scene_state = {0};

static uint32_t next_power_of_2(uint32_t n)
{
    if (n == 0)
        return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

void set_up_compute_pipeline(void)
{
    g_scene_state.compute.padded_splat_count = next_power_of_2(g_scene_state.splat_count);
    g_scene_state.compute.depth_buffer = sg_make_buffer(&(sg_buffer_desc){
        .size = g_scene_state.compute.padded_splat_count * sizeof(float),
        .usage = {.storage_buffer = true},
        .label = "depth-buffer"});

    g_scene_state.compute.index_buffer = sg_make_buffer(&(sg_buffer_desc){
        .size = g_scene_state.compute.padded_splat_count * sizeof(uint32_t),
        .usage = {.storage_buffer = true, .vertex_buffer = true},
        .label = "index-buffer"});

    g_scene_state.compute.depth_buffer_view = sg_make_view(&(sg_view_desc){
        .storage_buffer = {.buffer = g_scene_state.compute.depth_buffer},
        .label = "depth-buffer-view"});

    g_scene_state.compute.index_buffer_view = sg_make_view(&(sg_view_desc){
        .storage_buffer = {.buffer = g_scene_state.compute.index_buffer},
        .label = "index-buffer-view"});

    g_scene_state.compute.compute_depth_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .compute = true,
        .shader = sg_make_shader(depth_shader_desc(sg_query_backend())),
        .label = "depth-pipeline"});

    g_scene_state.compute.compute_sort_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .compute = true,
        .shader = sg_make_shader(sort_shader_desc(sg_query_backend())),
        .label = "sort-pipeline"});

    // bindings
    g_scene_state.compute.depth_bindings = (sg_bindings){
        .views = {
            [VIEW_splat_texture] = g_scene_state.splat_texture.view,
            [VIEW_depth_output] = g_scene_state.compute.depth_buffer_view,
            [VIEW_index_output] = g_scene_state.compute.index_buffer_view},
        .samplers = {[SMP_splat_sampler] = g_scene_state.splat_texture.sampler}};

    g_scene_state.compute.sort_bindings = (sg_bindings){
        .views = {
            [VIEW_depth_input] = g_scene_state.compute.depth_buffer_view,
            [VIEW_index_buffer] = g_scene_state.compute.index_buffer_view}};

    print("compute pipeline is ready ");
}

void dispatch_compute_sort(void)
{
    if (!g_scene_state.initialized || !g_scene_state.camera)
    {
        return;
    }
    const HMM_Vec3 *camera_pos = &g_scene_state.camera->position;
    HMM_Mat4 view = camera_get_view_matrix(g_scene_state.camera);
    HMM_Vec3 camera_forward = HMM_V3(-view.Elements[0][2], -view.Elements[1][2], -view.Elements[2][2]);
    camera_forward = HMM_NormV3(camera_forward);
    sg_begin_pass(&(sg_pass){.compute = true, .label = "sort-compute-pass"});
    // STEP 1: Calculate depths and initialize indices
    {
        // Calculate bounds_size
        HMM_Vec3 bounds_size = HMM_Sub(g_scene_state.splat_bounds.max, g_scene_state.splat_bounds.min);

        depth_params_t params = {
            .viewMat_row0 = {view.Elements[0][0], view.Elements[0][1], view.Elements[0][2], view.Elements[0][3]},
            .viewMat_row1 = {view.Elements[1][0], view.Elements[1][1], view.Elements[1][2], view.Elements[1][3]},
            .viewMat_row2 = {view.Elements[2][0], view.Elements[2][1], view.Elements[2][2], view.Elements[2][3]},
            .viewMat_row3 = {view.Elements[3][0], view.Elements[3][1], view.Elements[3][2], view.Elements[3][3]},
            .camera_position = {camera_pos->X, camera_pos->Y, camera_pos->Z},
            ._pad0 = 0.0f,
            .camera_forward = {camera_forward.X, camera_forward.Y, camera_forward.Z},
            ._pad1 = 0.0f,
            .bounds_min = {g_scene_state.splat_bounds.min.X, g_scene_state.splat_bounds.min.Y, g_scene_state.splat_bounds.min.Z},
            ._pad2 = 0.0f,
            .bounds_size = {bounds_size.X, bounds_size.Y, bounds_size.Z},
            .splat_count = (int)g_scene_state.splat_count,
            .texture_width = g_scene_state.splat_texture.width,
            .texture_height = g_scene_state.splat_texture.height,
            .splats_per_layer = g_scene_state.splat_texture.width * g_scene_state.splat_texture.height,
            ._pad3 = 0.0f};

        sg_apply_pipeline(g_scene_state.compute.compute_depth_pip);
        sg_apply_uniforms(UB_depth_params, &SG_RANGE(params));
        sg_apply_bindings(&g_scene_state.compute.depth_bindings);

        // Dispatch with enough work groups to cover all splats (256 threads per work group)
        uint32_t num_work_groups = (g_scene_state.compute.padded_splat_count + 255) / 256;
        sg_dispatch(num_work_groups, 1, 1);
    }

    // STEP 2: Bitonic sort - requires multiple dispatches
    {
        sg_apply_pipeline(g_scene_state.compute.compute_sort_pip);
        sg_apply_bindings(&g_scene_state.compute.sort_bindings);

        // Calculate number of stages for bitonic sort
        // For n elements (power of 2), we need log2(n) stages
        int num_stages = 0;
        uint32_t temp = g_scene_state.compute.padded_splat_count;
        while (temp > 1)
        {
            temp >>= 1;
            num_stages++;
        }

        // Bitonic sort algorithm: nested loops over stages and steps
        for (int stage = 0; stage < num_stages; stage++)
        {
            // Each stage has (stage + 1) steps
            for (int step = stage; step >= 0; step--)
            {
                sort_params_t sort_params = {
                    .stage = stage,
                    ._step = step,
                    .count = (int)g_scene_state.compute.padded_splat_count,
                    ._pad = 0};

                sg_apply_uniforms(UB_sort_params, &SG_RANGE(sort_params));

                // Number of compare-swap operations = padded_count / 2
                uint32_t num_comparisons = g_scene_state.compute.padded_splat_count / 2;
                uint32_t num_work_groups = (num_comparisons + 255) / 256;
                sg_dispatch(num_work_groups, 1, 1);
            }
        }
    }

    sg_end_pass();
}

int init_scene(void)
{
    if (g_scene_state.initialized)
    {
        return 1;
    }

    // Initialize camera
    g_scene_state.camera = camera_create();
    camera_set_radius(g_scene_state.camera, 10.0f);
    if (!g_scene_state.camera)
    {
        print("ERROR: Failed to create camera\n");
        return 0;
    }

    float vertices[] = {
        -1.0f, -1.0f, // Bottom-left
        1.0f, -1.0f,  // Bottom-right
        -1.0f, 1.0f,  // Top-left
        1.0f, 1.0f    // Top-right
    };

    g_scene_state.bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .usage = {.vertex_buffer = true, .immutable = true},
        .data = SG_RANGE(vertices),
        .label = "quad-vertices"});

    sg_shader shd = sg_make_shader(quad_shader_desc(sg_query_backend()));

    //  pipeline
    g_scene_state.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .colors[0] = {
            .blend = {
                .enabled = true,
                .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .src_factor_alpha = SG_BLENDFACTOR_ONE,
                .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA},
        },
        .index_type = SG_INDEXTYPE_NONE,
        .layout = {.attrs = {[ATTR_quad_position] = {.format = SG_VERTEXFORMAT_FLOAT2, .buffer_index = 0}, [ATTR_quad_sorted_index] = {.format = SG_VERTEXFORMAT_UINT, .buffer_index = 1}}, .buffers = {[0] = {.stride = 8, .step_func = SG_VERTEXSTEP_PER_VERTEX}, [1] = {.stride = 4, .step_func = SG_VERTEXSTEP_PER_INSTANCE}}},
        .depth = {.write_enabled = false, .compare = SG_COMPAREFUNC_ALWAYS},
        .cull_mode = SG_CULLMODE_NONE,
        .label = "splat-pipeline"});

    g_scene_state.pass_action = (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = {0.0f, 0.0f, 0.0f, 1.0f}}};

    g_scene_state.uniforms_dirty = true;

    g_scene_state.initialized = true;
    return 1;
}

void render_scene(sg_swapchain swapchain)
{
    // Early exits before expensive calculations
    if (!g_scene_state.initialized || !g_scene_state.camera)
    {
        return;
    }

    if (g_scene_state.splat_texture.view.id == SG_INVALID_ID ||
        g_scene_state.splat_texture.sampler.id == SG_INVALID_ID)
    {
        return;
    }

    // Calculate matrices only after validation
    HMM_Mat4 view = camera_get_view_matrix(g_scene_state.camera);
    float aspect_ratio = (float)swapchain.width / (float)swapchain.height;
    HMM_Mat4 projection = camera_get_projection_matrix(g_scene_state.camera, aspect_ratio);

    memcpy(g_scene_state.vs_params.viewMat, &view, sizeof(float) * 16);
    memcpy(g_scene_state.vs_params.projMat, &projection, sizeof(float) * 16);

    if (g_scene_state.uniforms_dirty)
    {
        g_scene_state.vs_params.bounds_min[0] = g_scene_state.splat_bounds.min.X;
        g_scene_state.vs_params.bounds_min[1] = g_scene_state.splat_bounds.min.Y;
        g_scene_state.vs_params.bounds_min[2] = g_scene_state.splat_bounds.min.Z;

        g_scene_state.vs_params.bounds_max[0] = g_scene_state.splat_bounds.max.X;
        g_scene_state.vs_params.bounds_max[1] = g_scene_state.splat_bounds.max.Y;
        g_scene_state.vs_params.bounds_max[2] = g_scene_state.splat_bounds.max.Z;

        // Precompute bounds_size to avoid per-splat calculation in shader
        HMM_Vec3 bounds_size = HMM_Sub(g_scene_state.splat_bounds.max, g_scene_state.splat_bounds.min);
        g_scene_state.vs_params.bounds_size[0] = bounds_size.X;
        g_scene_state.vs_params.bounds_size[1] = bounds_size.Y;
        g_scene_state.vs_params.bounds_size[2] = bounds_size.Z;

        g_scene_state.vs_params.texture_width = g_scene_state.splat_texture.width;
        g_scene_state.vs_params.texture_height = g_scene_state.splat_texture.height;
        g_scene_state.vs_params.splats_per_layer = g_scene_state.splat_texture.width * g_scene_state.splat_texture.height;

        // Set bindings once when uniforms change (they're static)
        g_scene_state.bind.views[VIEW_splat_texture] = g_scene_state.splat_texture.view;
        g_scene_state.bind.samplers[SMP_splat_sampler] = g_scene_state.splat_texture.sampler;

        g_scene_state.uniforms_dirty = false;
    }

    dispatch_compute_sort();

    // Bind sorted index buffer as vertex buffer (updated every frame by compute shader)
    g_scene_state.bind.vertex_buffers[1] = g_scene_state.compute.index_buffer;

    // Begin render pass
    sg_begin_pass(&(sg_pass){
        .action = g_scene_state.pass_action,
        .swapchain = swapchain});

    sg_apply_pipeline(g_scene_state.pip);
    sg_apply_bindings(&g_scene_state.bind);
    sg_apply_uniforms(UB_vs_params, &SG_RANGE(g_scene_state.vs_params));
    sg_draw(0, 4, g_scene_state.splat_count);

    sg_end_pass();
    sg_commit();
}

int parse_spz_data(const uint8_t *decompressed_data, size_t decompressed_size)
{
    PackedSplat *parsed_splats = NULL;
    uint32_t splat_count = 0;
    BoundingBox bounds = {0};

    int result = parse_spz_data_to_splats(decompressed_data, decompressed_size,
                                          &parsed_splats, &splat_count, &bounds);

    if (result != 0)
    {
        print("ERROR: Failed to parse SPZ data (error %d)\n", result);
        return result;
    }

    print("SPZ header indicates %u splats\n", splat_count);

    initialize_gaussian_splats(splat_count);

    if (g_scene_state.packed_splats)
    {
        free(g_scene_state.packed_splats);
    }

    create_splat_texture_from_data(&g_scene_state.splat_texture, parsed_splats, splat_count);

    g_scene_state.packed_splats = parsed_splats;
    g_scene_state.splat_count = splat_count;
    g_scene_state.splat_bounds = bounds;
    g_scene_state.splats_initialized = true;

    mark_uniforms_dirty();

    set_up_compute_pipeline();

    print("Loaded %u splats from SPZ data\n", splat_count);
    return 0;
}

void handle_input(float x, float y)
{
    if (g_scene_state.camera)
    {
        camera_handle_input(g_scene_state.camera, x, y);
    }
}

void handle_touch_down(float x, float y)
{
    if (g_scene_state.camera)
    {
        camera_reset_touch_state(g_scene_state.camera);
        camera_handle_input(g_scene_state.camera, x, y);
    }
}

void handle_pinch(float factor)
{
    if (g_scene_state.camera)
    {
        camera_handle_pinch(g_scene_state.camera, factor);
    }
}

void handle_touch_up(void)
{
    if (g_scene_state.camera)
    {
        camera_reset_touch_state(g_scene_state.camera);
    }
}

void initialize_gaussian_splats(uint32_t capacity)
{
    if (g_scene_state.packed_splats)
    {
        free(g_scene_state.packed_splats);
    }

    g_scene_state.packed_splats = (PackedSplat *)malloc(capacity * sizeof(PackedSplat));
    g_scene_state.splat_count = 0;
    g_scene_state.splats_initialized = false;

    if (!g_scene_state.packed_splats)
    {
        print("ERROR: Failed to allocate splat buffer\n");
    }
}

bool is_scene_initialized(void)
{
    return g_scene_state.initialized;
}

void cleanup_scene(void)
{
    if (!g_scene_state.initialized)
    {
        return;
    }

    // Clean up camera
    if (g_scene_state.camera)
    {
        camera_destroy(g_scene_state.camera);
        g_scene_state.camera = NULL;
    }

    // Clean up splat data
    if (g_scene_state.packed_splats)
    {
        free(g_scene_state.packed_splats);
        g_scene_state.packed_splats = NULL;
    }

    // Clean up GPU resources
    if (g_scene_state.pip.id != SG_INVALID_ID)
    {
        sg_destroy_pipeline(g_scene_state.pip);
        g_scene_state.pip.id = SG_INVALID_ID;
    }

    if (g_scene_state.bind.vertex_buffers[0].id != SG_INVALID_ID)
    {
        sg_destroy_buffer(g_scene_state.bind.vertex_buffers[0]);
        g_scene_state.bind.vertex_buffers[0].id = SG_INVALID_ID;
    }

    if (g_scene_state.bind.index_buffer.id != SG_INVALID_ID)
    {
        sg_destroy_buffer(g_scene_state.bind.index_buffer);
        g_scene_state.bind.index_buffer.id = SG_INVALID_ID;
    }

    cleanup_splat_texture(&g_scene_state.splat_texture);

    g_scene_state.initialized = false;
}

// Mark uniforms as dirty when splat data changes
void mark_uniforms_dirty(void)
{
    g_scene_state.uniforms_dirty = true;
}
