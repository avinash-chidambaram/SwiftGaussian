#ifndef INIT_H
#define INIT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Sokol initialization functions only
    void initGpu(void);
    void cleanup_rendering(void);
    bool is_rendering_initialized(void);
    
#if defined(__APPLE__) && TARGET_OS_IOS
    // iOS-specific initialization functions
    void initGpuWithMetalDevice(void *metal_device);
    void *getMetalDevice(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // INIT_H
