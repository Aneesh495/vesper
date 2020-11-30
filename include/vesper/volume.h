#ifndef VESPER_VOLUME_H
#define VESPER_VOLUME_H

#include "vesper/scene.h"

#define VS_TRACK_LIMIT 16384u

typedef struct {
    double t,albedo;
    uint32_t nulls;
    bool collision;
} VsCollision;

double vs_density(const VsMedium *m,VsVec3 p);
bool vs_collision(const VsMedium *m,VsRay ray,double end,double nm,VsSample rng,uint32_t dimension,
                  VsCollision *result,VsError *error);
bool vs_transmittance(const VsMedium *m,VsRay ray,double end,double nm,VsSample rng,uint32_t dimension,
                      double *result,uint32_t *nulls,VsError *error);
double vs_pulse_cdf(double time,double start,double pulse_width,double response_width);

#endif
