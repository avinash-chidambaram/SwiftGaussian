#include "quaternion.h"
#include <math.h>

HMM_Vec2 octahedral_encode(HMM_Vec3 v)
{
    // Normalize if not already
    v = HMM_NormV3(v);

    // Project to octahedron, then to 2D
    float l1norm = fabsf(v.X) + fabsf(v.Y) + fabsf(v.Z);
    HMM_Vec2 result = HMM_V2(v.X / l1norm, v.Y / l1norm);

    // If z is negative, reflect across diagonals
    if (v.Z < 0.0f)
    {
        float x = result.X;
        float y = result.Y;
        result.X = (1.0f - fabsf(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        result.Y = (1.0f - fabsf(x)) * (y >= 0.0f ? 1.0f : -1.0f);
    }

    // Map from [-1, 1] to [0, 1]
    result.X = result.X * 0.5f + 0.5f;
    result.Y = result.Y * 0.5f + 0.5f;

    return result;
}

void quat_to_axis_angle(HMM_Quat q, HMM_Vec3 *out_axis, float *out_angle)
{
    // Normalize quaternion first
    q = HMM_NormQ(q);

    // Handle identity rotation (or near-identity)
    if (q.W >= 1.0f - 1e-6f)
    {
        *out_axis = HMM_V3(1.0f, 0.0f, 0.0f); // arbitrary axis
        *out_angle = 0.0f;
        return;
    }

    // Calculate angle
    *out_angle = 2.0f * acosf(HMM_Clamp(-1.0f, 1.0f, q.W));

    // Calculate axis
    float s = sqrtf(1.0f - q.W * q.W);
    if (s < 1e-6f)
    {
        // Angle is very small, use arbitrary axis
        *out_axis = HMM_V3(1.0f, 0.0f, 0.0f);
    }
    else
    {
        *out_axis = HMM_V3(q.X / s, q.Y / s, q.Z / s);
    }
}
