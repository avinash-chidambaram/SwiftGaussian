#ifndef SPZLOADER_H
#define SPZLOADER_H

#include <stdint.h>
#include <stddef.h>
#include "utils/handmademath.h"
#include "scene.h"
#include "utils/logger.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // SPZ format structures based on Niantic SPZ specification
    typedef struct
    {
        uint32_t magic;         // Always 0x5053474e ('N' 'G' 'S' 'P')
        uint32_t version;       // Currently valid versions are 2 and 3
        uint32_t numPoints;     // Number of gaussians
        uint8_t shDegree;       // Degree of spherical harmonics (0-3)
        uint8_t fractionalBits; // Number of bits for fractional part of coordinates
        uint8_t flags;          // Bit field containing flags
        uint8_t reserved;       // Reserved for future use, must be 0
    } __attribute__((packed)) PackedGaussiansHeader;

    // SPZ parsing result structure
    typedef struct
    {
        PackedSplat *splats;
        uint32_t splat_count;
        BoundingBox bounds;
        bool success;
    } SPZParseResult;

    int parse_spz_data_to_splats(const uint8_t *decompressed_data, size_t decompressed_size,
                                 PackedSplat **out_splats, uint32_t *out_count, BoundingBox *out_bounds);

    // Optimized: Parse SPZ data directly to texture format (eliminates intermediate PackedSplat array)
    int parse_spz_data_to_texture(const uint8_t *decompressed_data, size_t decompressed_size,
                                  uint32_t **out_texture_data, uint32_t *out_splat_count,
                                  BoundingBox *out_bounds,
                                  int *out_width, int *out_height, int *out_num_layers);

#ifdef __cplusplus
}
#endif

#endif // SPZLOADER_H
