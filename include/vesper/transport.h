#ifndef VESPER_TRANSPORT_H
#define VESPER_TRANSPORT_H

#include "vesper/scatter.h"
#include "vesper/volume.h"

#define VS_MAX_DEPTH 32u
#define VS_MAX_EVENTS (2u*VS_MAX_DEPTH+1u)
#define VS_TRACE_CAP 192u
#define VS_MAX_BOUNDARIES 16u

typedef enum { VS_BACKEND_CPU, VS_BACKEND_VULKAN } VsBackend;
typedef struct {
    uint32_t width,height,seed,max_depth;
    VsSampler sampler;
    VsWavelengthMode wavelength;
    VsBackend backend;
    bool nee,roulette;
} VsRender;

typedef struct {
    double value,delay;
    VsPulse pulse;
    bool timed;
} VsContribution;

typedef struct {
    VsWavelength wavelength;
    VsContribution events[VS_MAX_EVENTS];
    uint32_t count,rays,shadow_rays,nulls;
} VsPath;

typedef enum {
    VS_EVENT_WAVELENGTH, VS_EVENT_EMISSION, VS_EVENT_SHADOW, VS_EVENT_SCATTER,
    VS_EVENT_BOUNDARY, VS_EVENT_VOLUME, VS_EVENT_ROULETTE, VS_EVENT_END
} VsEventKind;

typedef struct {
    VsEventKind kind;
    uint32_t depth,medium,primitive;
    double distance,delay,pdf,weight,throughput;
    bool delta;
} VsTraceEvent;

typedef struct { VsTraceEvent events[VS_TRACE_CAP]; uint32_t count,dropped; } VsTrace;

bool vs_render_valid(VsRender cfg,VsError *error);
bool vs_trace_path(const VsScene *scene,VsRender cfg,uint32_t pixel,uint32_t index,
                   VsPath *result,VsTrace *trace,VsError *error);

#endif
