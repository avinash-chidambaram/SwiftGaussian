#ifndef QUATERNION_H
#define QUATERNION_H

#include "handmademath.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Octahedral encoding of a 3D unit vector to 2D coordinates
     * Maps sphere surface to a square [0, 1] x [0, 1]
     *
     * @param v Input 3D vector (will be normalized)
     * @return 2D coordinates in range [0, 1]
     */
    HMM_Vec2 octahedral_encode(HMM_Vec3 v);

    /**
     * Convert quaternion to axis-angle representation
     *
     * @param q Input quaternion
     * @param out_axis Output normalized axis vector
     * @param out_angle Output angle in radians [0, π]
     */
    void quat_to_axis_angle(HMM_Quat q, HMM_Vec3 *out_axis, float *out_angle);

#ifdef __cplusplus
}
#endif

#endif // QUATERNION_H
