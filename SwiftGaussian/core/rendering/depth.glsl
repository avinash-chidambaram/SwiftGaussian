// Depth calculation compute shader for Gaussian Splat sorting
// Calculates view-space depth for each splat and initializes index buffer

@cs depth_calc
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

// Uniform parameters
layout(binding=0) uniform depth_params {
    vec4 viewMat_row0;
    vec4 viewMat_row1;
    vec4 viewMat_row2;
    vec4 viewMat_row3;
    vec3 camera_position;
    float _pad0;
    vec3 camera_forward;
    float _pad1;
    vec3 bounds_min;
    float _pad2;
    vec3 bounds_size;
    int splat_count;
    int texture_width;
    int texture_height;
    int splats_per_layer;
    float _pad3;
};

// Texture for packed splat data
@image_sample_type splat_texture uint
layout(binding = 1) uniform utexture2DArray splat_texture;
@sampler_type splat_sampler nonfiltering
layout(binding = 2) uniform sampler splat_sampler;

// Storage buffers - using struct with single flexible array member
struct DepthValue {
    float value;
};

struct DepthIndexData {
    uint value;
};

layout(binding=3) writeonly buffer depth_output {
    DepthValue depths[];
};

layout(binding=4) buffer index_output {
    DepthIndexData indices[];
};

// Unpack position from packed format
vec3 unpack_position(uint splat_idx) {
    // Calculate texture coordinates - same as splat.glsl
    int layer = int(splat_idx) / splats_per_layer;
    int pixel_in_layer = int(splat_idx) - (layer * splats_per_layer);
    int pixel_x = pixel_in_layer % texture_width;
    int pixel_y = pixel_in_layer / texture_width;
    
    // Single texture fetch - all splat data in one RGBA32UI pixel
    uvec4 packed = texelFetch(
        usampler2DArray(splat_texture, splat_sampler), 
        ivec3(pixel_x, pixel_y, layer), 
        0
    );
    
    // Unpack position - use multiplication instead of division
    const float inv_65535 = 1.0 / 65535.0;
    vec3 norm_pos = vec3(
        float((packed.r >> 16u) & 0xFFFFu),
        float(packed.r & 0xFFFFu),
        float((packed.g >> 16u) & 0xFFFFu)
    ) * inv_65535;
    
    // Denormalize to world space using bounding box
    return bounds_min + norm_pos * bounds_size;
}

void main() {
    uint idx = gl_GlobalInvocationID.x;

    // Bounds check
    if (int(idx) >= splat_count) {
        return;
    }

    // Unpack splat position
    vec3 splat_pos = unpack_position(idx);

    // Calculate view-space depth (distance along camera forward vector)
    // Negative because camera looks down -Z in view space
    vec3 to_splat = splat_pos - camera_position;
    float depth = dot(to_splat, camera_forward);

    // Store depth (we want back-to-front, so larger depth = further)
    depths[idx].value = depth;

    // Initialize index
    indices[idx].value = idx;
}

@end

@program depth depth_calc