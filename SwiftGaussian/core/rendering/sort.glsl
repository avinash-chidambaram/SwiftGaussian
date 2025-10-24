// Bitonic sort compute shader for Gaussian Splat depth sorting
// Sorts indices based on depth values (back-to-front for correct blending)

@cs bitonic_sort
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

// Uniform parameters
layout(binding=0) uniform sort_params {
    int stage;      // Current sorting stage (0 to log2(n))
    int step;       // Current step within stage
    int count;      // Total number of elements (padded to power of 2)
    int _pad;
};

// Storage buffers
struct SortDepthValue {
    float value;
};

struct SortIndexData {
    uint value;
};

layout(binding=0) readonly buffer depth_input {
    SortDepthValue depths[];
};

layout(binding=1) buffer index_buffer {
    SortIndexData indices[];
};

// Bitonic sort: compare-and-swap pairs of elements
void main() {
    uint idx = gl_GlobalInvocationID.x;

    // Calculate comparison distance for current step
    uint step_distance = 1u << uint(step);

    // Calculate which pair this thread handles
    uint pair_distance = 2u << uint(step);
    uint block_idx = idx / step_distance;
    uint idx_in_block = idx % step_distance;

    // Calculate indices to compare
    uint idx_a = block_idx * pair_distance + idx_in_block;
    uint idx_b = idx_a + step_distance;

    // Bounds check
    if (idx_b >= uint(count)) {
        return;
    }

    // Determine sort direction based on bitonic sequence structure
    // For stage k, we alternate direction every 2^(k+1) elements
    uint stage_distance = 2u << uint(stage);
    bool ascending = ((idx_a / stage_distance) & 1u) == 0u;

    // Get indices and their depths
    uint index_a = indices[idx_a].value;
    uint index_b = indices[idx_b].value;
    float depth_a = depths[index_a].value;
    float depth_b = depths[index_b].value;

    // Compare and swap if needed
    // For back-to-front rendering, we want descending order (far to near)
    bool should_swap = ascending ? (depth_a < depth_b) : (depth_a > depth_b);

    if (should_swap) {
        // Swap indices
        indices[idx_a].value = index_b;
        indices[idx_b].value = index_a;
    }
}

@end

@program sort bitonic_sort