#ifndef VESPER_SCATTER_H
#define VESPER_SCATTER_H

#include "vesper/scene.h"

typedef struct { double f,pdf; } VsBsdfValue;
typedef struct {
    VsVec3 wi;
    double weight,pdf;
    bool delta,transmit;
} VsScatter;

double vs_fresnel(double cos_i,double ni,double nt);
double vs_conductor_fresnel(double cosine,double eta,double k);
VsVec3 vs_direction(VsVec3 axis,double cosine,double phi);
VsBsdfValue vs_bsdf_eval(const VsMaterial *m,double nm,VsVec3 n,VsVec3 wo,VsVec3 wi,double ni,double nt);
VsScatter vs_bsdf_sample(const VsMaterial *m,double nm,VsVec3 n,VsVec3 wo,double ni,double nt,
                         double u,double v,double branch);
double vs_phase(double cosine,double g);
VsVec3 vs_phase_sample(VsVec3 forward,double g,double u,double v);

#endif
