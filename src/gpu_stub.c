#include "vesper/gpu.h"

bool vs_gpu_open(VsGpu **gpu,const VsScene *scene,VsRender config,VsError *error) {
    (void)gpu; (void)scene; (void)config;
    return vs_fail(error,"Vulkan backend was not built (requires Vulkan SDK and glslangValidator)");
}

void vs_gpu_close(VsGpu *gpu) { (void)gpu; }
const VsDevice *vs_gpu_device(const VsGpu *gpu) { (void)gpu; return NULL; }

bool vs_gpu_render(VsGpu *gpu,const VsScene *s,VsFilm *f,VsRegion region,
                   uint32_t target,VsStats *stats,VsError *error) {
    (void)gpu; (void)s; (void)f; (void)region; (void)target; (void)stats;
    return vs_fail(error,"Vulkan backend was not built");
}
