#include "splat_texture.h"
#include "utils/logger.h"
#include <stdlib.h>
#include <memory.h>
#include <stdint.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#define TEXTURE_WIDTH 1024
#define TEXTURE_HEIGHT 1024
#define MAX_SPLATS_PER_LAYER (TEXTURE_WIDTH * TEXTURE_HEIGHT)

static uint32_t *convert_splats_to_texture_data(PackedSplat *splats, uint32_t splat_count,
                                                int texture_width, int texture_height, int num_layers)
{
    size_t pixels_per_layer = (size_t)texture_width * texture_height;
    size_t total_pixels = pixels_per_layer * num_layers;
    size_t total_bytes = total_pixels * 4 * sizeof(uint32_t);

    uint32_t *texture_data = (uint32_t *)malloc(total_bytes);
    if (!texture_data)
    {
        print("ERROR: Failed to allocate %zu bytes for texture data\n", total_bytes);
        return NULL;
    }

    memset(texture_data, 0, total_bytes);

#ifdef _OPENMP
#pragma omp parallel for schedule(static, 1024) if (splat_count > 5000)
#endif
    for (uint32_t i = 0; i < splat_count; i++)
    {
        PackedSplat *splat = &splats[i];
        size_t pixel_index = (size_t)i * 4;

        texture_data[pixel_index + 0] = ((uint32_t)splat->pos_x << 16) | splat->pos_y;
        texture_data[pixel_index + 1] = ((uint32_t)splat->pos_z << 16) |
                                        ((uint32_t)splat->rot_axis_u << 8) |
                                        splat->rot_axis_v;
        texture_data[pixel_index + 2] = ((uint32_t)splat->rot_angle << 24) |
                                        ((uint32_t)splat->scale_x << 16) |
                                        ((uint32_t)splat->scale_y << 8) |
                                        splat->scale_z;
        texture_data[pixel_index + 3] = ((uint32_t)splat->r << 24) |
                                        ((uint32_t)splat->g << 16) |
                                        ((uint32_t)splat->b << 8) |
                                        splat->a;
    }

    return texture_data;
}

static void calculate_texture_dimensions(uint32_t splat_count, int *width, int *height, int *num_layers)
{
    uint32_t required_pixels = splat_count;

    int selected_width = 0, selected_height = 0, selected_layers = 0;
    size_t min_waste = SIZE_MAX;

    int sizes[] = {256, 512, 1024, 2048, 4096};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int i = 0; i < num_sizes; i++)
    {
        int texture_width = sizes[i];
        int texture_height = sizes[i];
        int pixels_per_layer = texture_width * texture_height;
        int layers = (int)((required_pixels + pixels_per_layer - 1) / pixels_per_layer);
        size_t total_pixels = (size_t)pixels_per_layer * layers;
        size_t waste = total_pixels - required_pixels;

        if (waste < min_waste)
        {
            selected_width = texture_width;
            selected_height = texture_height;
            selected_layers = layers;
            min_waste = waste;
        }

        if (waste < (float)pixels_per_layer * 0.1f)
        {
            break;
        }
    }

    if (selected_width == 0)
    {
        selected_width = TEXTURE_WIDTH;
        selected_height = TEXTURE_HEIGHT;
        selected_layers = (splat_count + MAX_SPLATS_PER_LAYER - 1) / MAX_SPLATS_PER_LAYER;
    }

    *width = selected_width;
    *height = selected_height;
    *num_layers = selected_layers;
}

void create_splat_texture_from_data(splat_texture_t *texture, PackedSplat *splats, uint32_t splat_count)
{
    int width, height, num_layers;
    calculate_texture_dimensions(splat_count, &width, &height, &num_layers);

    texture->width = width;
    texture->height = height;
    texture->num_layers = num_layers;

    size_t required_pixels = splat_count;
    size_t allocated_pixels = (size_t)width * height * num_layers;
    float efficiency = (float)required_pixels / allocated_pixels * 100.0f;

    print("Texture: %u splats, %dx%d, %d layers, %.1f%% efficiency\n",
          splat_count, width, height, num_layers, efficiency);

    uint32_t *texture_data = convert_splats_to_texture_data(
        splats, splat_count,
        texture->width,
        texture->height,
        num_layers);

    if (!texture_data)
    {
        print("ERROR: Failed to convert splat data\n");
        return;
    }

    size_t total_size = (size_t)texture->width *
                        texture->height *
                        num_layers * sizeof(uint32_t) * 4;

    sg_image_data image_data = {0};
    image_data.mip_levels[0] = (sg_range){
        .ptr = texture_data,
        .size = total_size};

    texture->texture = sg_make_image(&(sg_image_desc){
        .type = SG_IMAGETYPE_ARRAY,
        .width = texture->width,
        .height = texture->height,
        .num_slices = num_layers,
        .pixel_format = SG_PIXELFORMAT_RGBA32UI,
        .usage = {.immutable = true},
        .data = image_data,
        .label = "splat-texture"});

    texture->sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST,
        .mag_filter = SG_FILTER_NEAREST,
        .mipmap_filter = SG_FILTER_NEAREST,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
        .label = "splat-sampler"});

    texture->view = sg_make_view(&(sg_view_desc){
        .texture = {
            .image = texture->texture,
        },
        .label = "splat-texture-view"});

    free(texture_data);

    if (texture->texture.id == SG_INVALID_ID ||
        texture->sampler.id == SG_INVALID_ID ||
        texture->view.id == SG_INVALID_ID)
    {
        print("ERROR: Failed to create texture resources\n");
    }
}

void cleanup_splat_texture(splat_texture_t *texture)
{
    if (texture->view.id != SG_INVALID_ID)
    {
        sg_destroy_view(texture->view);
        texture->view.id = SG_INVALID_ID;
    }

    if (texture->sampler.id != SG_INVALID_ID)
    {
        sg_destroy_sampler(texture->sampler);
        texture->sampler.id = SG_INVALID_ID;
    }

    if (texture->texture.id != SG_INVALID_ID)
    {
        sg_destroy_image(texture->texture);
        texture->texture.id = SG_INVALID_ID;
    }
}
