#ifndef VESPER_SPECTRAL_H
#define VESPER_SPECTRAL_H

#include "vesper/geometry.h"

#define VS_LAMBDA_MIN 360.0
#define VS_LAMBDA_MAX 830.0
#define VS_SPECTRAL_KNOTS 8u

typedef enum { VS_TABULATED, VS_GAUSSIAN } VsCurve;
typedef enum { VS_RANDOM, VS_STRATIFIED, VS_HALTON } VsSampler;
typedef enum { VS_WAVELENGTH_UNIFORM, VS_WAVELENGTH_MIXTURE } VsWavelengthMode;

typedef struct {
    double values[VS_SPECTRAL_KNOTS];
    VsCurve kind;
} VsSpectrum;

typedef struct { double nm,pdf; } VsWavelength;
typedef struct { uint32_t pixel,index,seed; VsSampler mode; } VsSample;

uint32_t vs_hash32(uint32_t value);
double vs_sample(VsSample sample, uint32_t dimension);
VsWavelength vs_wavelength(double u, VsWavelengthMode mode);
double vs_wavelength_pdf(double nm, VsWavelengthMode mode);
VsSpectrum vs_flat(double value);
VsSpectrum vs_model_rgb(VsVec3 rgb);
VsSpectrum vs_gaussian(double amplitude, double center_nm, double sigma_nm);
bool vs_spectrum_valid(VsSpectrum spectrum, double limit);
double vs_spectrum(VsSpectrum spectrum, double nm);
VsVec3 vs_observer(double nm);
VsVec3 vs_display(VsVec3 xyz, double exposure);

#endif
