#ifndef VESPER_GPU_H
#define VESPER_GPU_H

#include "vesper/film.h"

typedef struct VsGpu VsGpu;
typedef struct {
    char name[256],driver[256];
    uint32_t api_version,driver_version,vendor_id,device_id,timestamp_bits;
    double timestamp_period_ns;
    bool float64,validation;
    uint64_t identity;
} VsDevice;

typedef struct {
    uint64_t start,end;
    double period_ns;
    uint32_t bits,samples,rays,shadows,nulls;
} VsGpuTiming;

/* Called synchronously after a successful fence and before film readback. */
typedef bool (*VsGpuTimingSink)(const VsGpuTiming *batch,void *context);

bool vs_gpu_enable_timing(VsGpu *gpu,VsGpuTimingSink sink,void *context,VsError *error);
bool vs_gpu_open(VsGpu **gpu,const VsScene *scene,VsRender config,VsError *error);
void vs_gpu_close(VsGpu *gpu);
const VsDevice *vs_gpu_device(const VsGpu *gpu);
bool vs_gpu_render_budget(VsGpu *gpu,const VsScene *scene,VsFilm *film,VsSampleBudget budget,
                          VsStats *stats,VsError *error);
bool vs_gpu_render(VsGpu *gpu,const VsScene *scene,VsFilm *film,VsRegion region,
                   uint32_t target,VsStats *stats,VsError *error);

#endif
