@vs vs
in vec2 position;
in uint sorted_index;  // Per-instance vertex attribute from sorted index buffer

out vec4 color;
out vec2 quad_coord;

layout(binding = 0) uniform vs_params {
    mat4 viewMat;
    mat4 projMat;
    vec3 bounds_min;
    vec3 bounds_max;
    vec3 bounds_size;
    int texture_width;
    int texture_height;
    int splats_per_layer;
};

@image_sample_type splat_texture uint
layout(binding = 1) uniform utexture2DArray splat_texture;
@sampler_type splat_sampler nonfiltering
layout(binding = 2) uniform sampler splat_sampler;

vec3 octahedral_decode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.xy += mix(vec2(t), vec2(-t), greaterThanEqual(n.xy, vec2(0.0)));
    return normalize(n);
}

mat3 quat_to_mat3(vec4 q) {
    // Pre-compute common terms
    float qxx = q.x * q.x;
    float qyy = q.y * q.y;
    float qzz = q.z * q.z;
    float qxz = q.x * q.z;
    float qxy = q.x * q.y;
    float qyz = q.y * q.z;
    float qwx = q.w * q.x;
    float qwy = q.w * q.y;
    float qwz = q.w * q.z;
    
    return mat3(
        1.0 - 2.0 * (qyy + qzz), 2.0 * (qxy - qwz), 2.0 * (qxz + qwy),
        2.0 * (qxy + qwz), 1.0 - 2.0 * (qxx + qzz), 2.0 * (qyz - qwx),
        2.0 * (qxz - qwy), 2.0 * (qyz + qwx), 1.0 - 2.0 * (qxx + qyy)
    );
}

void main() {
    // Use sorted index from vertex attribute (provided by index buffer as per-instance data)
    int splat_idx = int(sorted_index);
    
    // Calculate texture coordinates - avoid modulo for better performance
    int layer = splat_idx / splats_per_layer;
    int pixel_in_layer = splat_idx - (layer * splats_per_layer);
    int pixel_x = pixel_in_layer % texture_width;
    int pixel_y = pixel_in_layer / texture_width;
    
    // Single texture fetch - all splat data in one RGBA32UI pixel
    uvec4 packed = texelFetch(
        usampler2DArray(splat_texture, splat_sampler), 
        ivec3(pixel_x, pixel_y, layer), 
        0
    );
    
    // Early alpha test - skip expensive unpacking for transparent splats
    uint alpha = packed.a & 0xFFu;
    if (alpha < 3u) {  // 3/255 ≈ 0.01
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }
    
    // Unpack position - use multiplication instead of division
    const float inv_65535 = 1.0 / 65535.0;
    vec3 norm_pos = vec3(
        float((packed.r >> 16u) & 0xFFFFu),
        float(packed.r & 0xFFFFu),
        float((packed.g >> 16u) & 0xFFFFu)
    ) * inv_65535;
    
    vec3 splat_pos = bounds_min + norm_pos * bounds_size;
    
    // Unpack rotation - octahedral encoded axis + angle
    const float inv_255 = 1.0 / 255.0;
    vec2 oct = vec2(
        float((packed.g >> 8u) & 0xFFu),
        float(packed.g & 0xFFu)
    ) * inv_255;
    
    float angle = (float((packed.b >> 24u) & 0xFFu) * inv_255) * 3.14159265359;
    vec3 axis = octahedral_decode(oct);
    
    // Build quaternion inline - avoid function call overhead
    float half_angle = angle * 0.5;
    float s = sin(half_angle);
    vec4 quat = vec4(axis * s, cos(half_angle));
    
    // Unpack scale - log encoded
    vec3 scale = exp(vec3(
        float((packed.b >> 16u) & 0xFFu),
        float((packed.b >> 8u) & 0xFFu),
        float(packed.b & 0xFFu)
    ) * (1.0 / 25.5) - 5.0);
    
    // Unpack color
    color = vec4(
        float((packed.a >> 24u) & 0xFFu),
        float((packed.a >> 16u) & 0xFFu),
        float((packed.a >> 8u) & 0xFFu),
        float(alpha)
    ) * inv_255;
    
    mat3 rot_mat = quat_to_mat3(quat);
    vec3 world_pos = splat_pos + rot_mat * (vec3(position, 0.0) * scale);
    
    vec4 view_pos = viewMat * vec4(world_pos, 1.0);
    gl_Position = projMat * view_pos;
    
    quad_coord = position;
}
@end


@fs fs
in vec4 color;
in vec2 quad_coord;
out vec4 frag_color;

void main() {
    float dist_sq = dot(quad_coord, quad_coord);
    
    const float radius_sq = 0.25;
    if (dist_sq > radius_sq) {
        discard;
    }
    
    const float gaussian_scale = 8.0;
    float gaussian = exp(-dist_sq * gaussian_scale);
    
    // Optimized: smoothstep with squared distance to avoid sqrt
    const float edge_start_sq = 0.0;
    const float edge_end_sq = 0.25;  // 0.5^2
    float t = clamp((dist_sq - edge_start_sq) / (edge_end_sq - edge_start_sq), 0.0, 1.0);
    float circular_falloff = 1.0 - (t * t * (3.0 - 2.0 * t));  // smoothstep formula
    
    float alpha = color.a * gaussian * circular_falloff;
    
    frag_color = vec4(color.rgb, alpha);
}
@end

@program quad vs fs