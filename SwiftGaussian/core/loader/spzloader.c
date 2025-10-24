#include "spzloader.h"
#include "core/utils/quaternion.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

#ifdef _OPENMP
#include <omp.h>
#endif

static inline float clamp_fast(float x, float min_val, float max_val)
{
    return fminf(fmaxf(x, min_val), max_val);
}



int parse_spz_data_to_splats(const uint8_t *decompressed_data, size_t decompressed_size,
                             PackedSplat **out_splats, uint32_t *out_count, BoundingBox *out_bounds)
{
    if (decompressed_size < sizeof(PackedGaussiansHeader))
    {
        print("ERROR: SPZ data too small for header\n");
        return -1;
    }

    // Parse header (little-endian)
    const PackedGaussiansHeader *header = (const PackedGaussiansHeader *)decompressed_data;

    // Check magic number
    if (header->magic != 0x5053474e) // 'N' 'G' 'S' 'P'
    {
        print("ERROR: Invalid SPZ magic number: 0x%08x\n", header->magic);
        return -1;
    }

    // Check version
    if (header->version != 2 && header->version != 3)
    {
        print("ERROR: Unsupported SPZ version: %u\n", header->version);
        return -1;
    }

    *out_count = header->numPoints;

    print("Parsing SPZ data: %u points, version %u, SH degree %u, fractional bits %u\n",
          header->numPoints, header->version, header->shDegree, header->fractionalBits);

    // Allocate output splats
    PackedSplat *splats = (PackedSplat *)malloc(header->numPoints * sizeof(PackedSplat));
    if (!splats)
    {
        print("ERROR: Failed to allocate memory for splats\n");
        return -1;
    }

    size_t offset = sizeof(PackedGaussiansHeader);

    // Positions: 3 * 24 bits = 9 bytes per point
    const uint8_t *positions = decompressed_data + offset;
    offset += header->numPoints * 9;

    // Alphas: 1 byte per point
    const uint8_t *alphas = decompressed_data + offset;
    offset += header->numPoints * 1;

    // Colors: 3 bytes per point (RGB)
    const uint8_t *colors = decompressed_data + offset;
    offset += header->numPoints * 3;

    // Scales: 3 bytes per point (log scale)
    const uint8_t *scales = decompressed_data + offset;
    offset += header->numPoints * 3;

    // Rotations: version dependent
    const uint8_t *rotations = decompressed_data + offset;
    if (header->version == 3)
    {
        offset += header->numPoints * 4; // Version 3: 4 bytes per point
    }
    else
    {
        offset += header->numPoints * 3; // Version 2: 3 bytes per point
    }

    // Verify we have enough data
    if (offset > decompressed_size)
    {
        print("ERROR: SPZ data size mismatch. Expected at least %zu bytes, got %zu\n",
              offset, decompressed_size);
        free(splats);
        return -1;
    }

    // Pre-calculate scale factor for fixed-point conversion
    const float scale_factor = 1.0f / (float)(1 << header->fractionalBits);

    // PASS 1: Calculate bounding box (must be serial, but optimized)
    HMM_Vec3 min_pos = {FLT_MAX, FLT_MAX, FLT_MAX};
    HMM_Vec3 max_pos = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    // Use branchless min/max for better performance
    for (uint32_t i = 0; i < header->numPoints; i++)
    {
        const uint8_t *pos_ptr = positions + (i * 9);

        // Extract 24-bit little-endian values
        uint32_t pos_x_raw = pos_ptr[0] | (pos_ptr[1] << 8) | (pos_ptr[2] << 16);
        uint32_t pos_y_raw = pos_ptr[3] | (pos_ptr[4] << 8) | (pos_ptr[5] << 16);
        uint32_t pos_z_raw = pos_ptr[6] | (pos_ptr[7] << 8) | (pos_ptr[8] << 16);

        // Sign extend 24-bit to 32-bit and convert to float
        float pos_x = ((int32_t)(pos_x_raw << 8) >> 8) * scale_factor;
        float pos_y = ((int32_t)(pos_y_raw << 8) >> 8) * scale_factor;
        float pos_z = ((int32_t)(pos_z_raw << 8) >> 8) * scale_factor;

        // Branchless min/max (compiles to MINSS/MAXSS on x86, optimal on ARM)
        min_pos.X = fminf(min_pos.X, pos_x);
        min_pos.Y = fminf(min_pos.Y, pos_y);
        min_pos.Z = fminf(min_pos.Z, pos_z);
        max_pos.X = fmaxf(max_pos.X, pos_x);
        max_pos.Y = fmaxf(max_pos.Y, pos_y);
        max_pos.Z = fmaxf(max_pos.Z, pos_z);
    }

    print("Position bounds: min(%.3f, %.3f, %.3f) max(%.3f, %.3f, %.3f)\n",
          min_pos.X, min_pos.Y, min_pos.Z, max_pos.X, max_pos.Y, max_pos.Z);

    // Handle Y-coordinate flip at bounds level (more efficient than per-splat)
    float temp_y = min_pos.Y;
    min_pos.Y = -max_pos.Y;
    max_pos.Y = -temp_y;

    print("After Y-flip: min(%.3f, %.3f, %.3f) max(%.3f, %.3f, %.3f)\n",
          min_pos.X, min_pos.Y, min_pos.Z, max_pos.X, max_pos.Y, max_pos.Z);

    // Pre-calculate inverse range for normalization (hoist division out of loop)
    const float inv_range_x = 1.0f / (max_pos.X - min_pos.X);
    const float inv_range_y = 1.0f / (max_pos.Y - min_pos.Y);
    const float inv_range_z = 1.0f / (max_pos.Z - min_pos.Z);

    // Pre-calculate constants (avoid recomputation in loop)
    const float inv_512 = 1.0f / 512.0f;
    const float inv_128 = 1.0f / 128.0f;
    const float inv_pi = 1.0f / HMM_PI;
    const int is_version_3 = (header->version == 3);

#ifdef _OPENMP
    int num_threads = omp_get_max_threads();
    print("Using OpenMP with %d threads for parallel processing\n", num_threads);
#endif

#ifdef _OPENMP
#pragma omp parallel for schedule(static, 512) if (header->numPoints > 10000)
#endif
    for (uint32_t i = 0; i < header->numPoints; i++)
    {
        PackedSplat *splat = &splats[i];

        // === POSITION ===
        // Re-parse position (eliminates temp buffer allocation)
        const uint8_t *pos_ptr = positions + (i * 9);
        uint32_t pos_x_raw = pos_ptr[0] | (pos_ptr[1] << 8) | (pos_ptr[2] << 16);
        uint32_t pos_y_raw = pos_ptr[3] | (pos_ptr[4] << 8) | (pos_ptr[5] << 16);
        uint32_t pos_z_raw = pos_ptr[6] | (pos_ptr[7] << 8) | (pos_ptr[8] << 16);

        float pos_x = ((int32_t)(pos_x_raw << 8) >> 8) * scale_factor;
        float pos_y = -((int32_t)(pos_y_raw << 8) >> 8) * scale_factor; // Y-flip inline
        float pos_z = ((int32_t)(pos_z_raw << 8) >> 8) * scale_factor;

        // Normalize to [0, 1] range with branchless clamp
        float normalized_x = clamp_fast((pos_x - min_pos.X) * inv_range_x, 0.0f, 1.0f);
        float normalized_y = clamp_fast((pos_y - min_pos.Y) * inv_range_y, 0.0f, 1.0f);
        float normalized_z = clamp_fast((pos_z - min_pos.Z) * inv_range_z, 0.0f, 1.0f);

        // Pack to 16-bit
        splat->pos_x = (uint16_t)(normalized_x * 65535.0f);
        splat->pos_y = (uint16_t)(normalized_y * 65535.0f);
        splat->pos_z = (uint16_t)(normalized_z * 65535.0f);

        // === ROTATION ===
        HMM_Quat rotation;

        if (is_version_3)
        {
            // Version 3: 3 components * 10 bits + 2 bits for largest component index
            const uint8_t *rot_ptr = rotations + (i * 4);
            uint32_t rot_data = rot_ptr[0] | (rot_ptr[1] << 8) |
                                (rot_ptr[2] << 16) | (rot_ptr[3] << 24);

            // Extract largest component index (lowest 2 bits)
            uint8_t largest_idx = rot_data & 0x3;
            uint32_t comp_data = rot_data >> 2;

            // Decode quaternion components (unrolled for better optimization)
            float q[4] = {0, 0, 0, 0};
            int shift = 0;
            for (int j = 0; j < 4; j++)
            {
                if (j != largest_idx)
                {
                    // Extract 10-bit signed value (range: -512 to 511)
                    int16_t val = (int16_t)((comp_data >> shift) & 0x3FF) - 512;
                    q[j] = (float)val * inv_512;
                    shift += 10;
                }
            }

            // Calculate largest component to ensure unit quaternion (branchless clamp)
            float sum_sq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
            q[largest_idx] = sqrtf(fmaxf(0.0f, 1.0f - sum_sq));

            rotation.X = q[0];
            rotation.Y = q[1];
            rotation.Z = q[2];
            rotation.W = q[3];
        }
        else // Version 2
        {
            // Version 2: x, y, z components as 8-bit signed integers
            const uint8_t *rot_ptr = rotations + (i * 3);
            rotation.X = (float)((int8_t)rot_ptr[0]) * inv_128;
            rotation.Y = (float)((int8_t)rot_ptr[1]) * inv_128;
            rotation.Z = (float)((int8_t)rot_ptr[2]) * inv_128;

            // Calculate W component to maintain unit quaternion (branchless)
            float w_sq = 1.0f - (rotation.X * rotation.X +
                                 rotation.Y * rotation.Y +
                                 rotation.Z * rotation.Z);
            rotation.W = sqrtf(fmaxf(0.0f, w_sq));
        }

        // Convert quaternion to axis-angle representation
        HMM_Vec3 rot_axis;
        float rot_angle;
        quat_to_axis_angle(rotation, &rot_axis, &rot_angle);

        // Encode axis using octahedral mapping
        HMM_Vec2 oct = octahedral_encode(rot_axis);

        // Branchless clamp and pack rotation data
        splat->rot_axis_u = (uint8_t)(clamp_fast(oct.X, 0.0f, 1.0f) * 255.0f);
        splat->rot_axis_v = (uint8_t)(clamp_fast(oct.Y, 0.0f, 1.0f) * 255.0f);
        splat->rot_angle = (uint8_t)(clamp_fast(rot_angle, 0.0f, HMM_PI) * inv_pi * 255.0f);

        // === SCALE ===
        // SPZ stores scales in log space, we keep them as-is (direct copy, fastest)
        const uint8_t *scale_ptr = scales + (i * 3);
        splat->scale_x = scale_ptr[0];
        splat->scale_y = scale_ptr[1];
        splat->scale_z = scale_ptr[2];

        // === COLOR & ALPHA ===
        const uint8_t *color_ptr = colors + (i * 3);
        splat->r = color_ptr[0];
        splat->g = color_ptr[1];
        splat->b = color_ptr[2];
        splat->a = alphas[i];
    }

    // Set output values
    *out_splats = splats;
    *out_count = header->numPoints;
    out_bounds->min = min_pos;
    out_bounds->max = max_pos;

    print("Successfully parsed %u SPZ splats (parallel optimized)\n", header->numPoints);
    print("Memory: %.2f MB (SPZ) -> %.2f MB (PackedSplat)\n",
          decompressed_size / (1024.0f * 1024.0f),
          (header->numPoints * sizeof(PackedSplat)) / (1024.0f * 1024.0f));

    return 0;
}
