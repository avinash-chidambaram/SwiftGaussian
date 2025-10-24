#ifndef SPLAT_TEXTURE_H
#define SPLAT_TEXTURE_H

#include <stdint.h>
#include "sokol/sokol_gfx.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        sg_image texture;
        sg_sampler sampler;
        sg_view view;
        int width;
        int height;
        int num_layers;
    } splat_texture_t;

    typedef struct
    {
        // Position: 3 * 16-bit normalized values
        uint16_t pos_x, pos_y, pos_z;

        // Rotation: 3 bytes using axis-angle + octahedral encoding
        uint8_t rot_axis_u; // Octahedral U coordinate (0-255)
        uint8_t rot_axis_v; // Octahedral V coordinate (0-255)
        uint8_t rot_angle;  // Angle in range [0, π] encoded as 0-255

        // Scale: 3 * 8-bit values (log scale)
        uint8_t scale_x, scale_y, scale_z;

        // Color: 4 * 8-bit values for RGBA (alpha channel is opacity)
        uint8_t r, g, b, a;

        // Total: 16 bytes per splat (tightly packed)
    } PackedSplat;

    // Texture creation and management
    void create_splat_texture_from_data(splat_texture_t *texture, PackedSplat *splats, uint32_t splat_count);
    void cleanup_splat_texture(splat_texture_t *texture);

#ifdef __cplusplus
}
#endif

#endif // SPLAT_TEXTURE_H

