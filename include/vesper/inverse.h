#ifndef VS_INVERSE_H
#define VS_INVERSE_H

#include "vesper/scene.h"

#define VS_PARAMETERS 3u
#define VS_MEASUREMENTS 64u

typedef enum { VS_REFLECTANCE,VS_EMITTER,VS_EXTINCTION,VS_PRODUCT } VsInverseKind;
typedef enum { VS_IDENTIFIED,VS_UNIDENTIFIABLE,VS_MODEL_MISMATCH,VS_NOT_CONVERGED } VsFitStatus;

typedef struct {
    double gain,distance,basis[VS_PARAMETERS],value,sigma;
} VsObservation;
typedef struct {
    VsInverseKind kind;
    uint32_t count;
    VsObservation rows[VS_MEASUREMENTS];
} VsCalibration;
typedef struct {
    double parameter[VS_PARAMETERS],uncertainty[VS_PARAMETERS];
    double chi2,condition;
    uint32_t iterations,rank;
    VsFitStatus status;
} VsFit;

unsigned vs_parameter_count(VsInverseKind kind);
double vs_forward(VsInverseKind kind,const VsObservation *row,const double parameter[VS_PARAMETERS],
                  double derivative[VS_PARAMETERS]);
bool vs_inverse_fit(const VsCalibration *cal,const double initial[VS_PARAMETERS],VsFit *fit,VsError *error);

#endif
