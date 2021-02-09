#ifndef VESPER_FILM_H
#define VESPER_FILM_H

#include "vesper/transport.h"

#define VS_MAX_BINS 2048u
#define VS_MAX_SPP 16777216u
#define VS_FILM_BUDGET (UINT64_C(640)*1024*1024)
#define VS_FILM_ABI 3u

typedef struct { uint32_t bins; double start,width; } VsGate;
typedef struct {
    VsVec3 mean;
    double m2_y,timed_y,excluded_y;
    float spectral[VS_SPECTRAL_KNOTS];
    uint32_t samples;
} VsPixel;
typedef struct {
    VsRender render;
    VsGate gate;
    uint64_t scene,device;
    VsPixel *pixels;
    float *time;
} VsFilm;

typedef struct { uint32_t x,y,width,height; } VsRegion;
typedef struct { uint64_t samples,rays,shadows,nulls; } VsStats;

bool vs_film_init(VsFilm *film,const VsScene *scene,VsRender render,VsGate gate,
                  uint64_t device,VsError *error);
void vs_film_destroy(VsFilm *film);
void vs_film_clear(VsFilm *film);
bool vs_film_add(VsFilm *film,uint32_t pixel,const VsPath *path,double response,VsError *error);
VsVec3 vs_film_slice(const VsFilm *film,uint32_t pixel,uint32_t bin);
double vs_film_variance(const VsPixel *pixel);
bool vs_region_valid(const VsFilm *film,VsRegion region);
bool vs_render_cpu(const VsScene *scene,VsFilm *film,VsRegion region,uint32_t target,
                   VsStats *stats,VsError *error);
bool vs_checkpoint_write(const VsFilm *film,const char *path,VsError *error);
bool vs_checkpoint_read(VsFilm *film,const char *path,const VsScene *scene,VsRender render,
                        VsGate gate,uint64_t device,VsError *error);
bool vs_export_exr(const VsFilm *film,const char *path,VsError *error);
bool vs_export_ppm(const VsFilm *film,const char *path,double exposure,uint32_t bin,VsError *error);
bool vs_export_time_csv(const VsFilm *film,const char *path,uint32_t pixel,VsError *error);

#endif
