#include "vesper/spectral.h"

static const VsVec3 observer[]={
#include "observer.inc"
};

uint32_t vs_hash32(uint32_t value) {
    value^=value>>16;
    value*=UINT32_C(0x7feb352d);
    value^=value>>15;
    value*=UINT32_C(0x846ca68b);
    return value^(value>>16);
}

static double unit(uint32_t bits) {
    /* Open interval with 23 effective bits, exactly representable in float. */
    return ((bits>>9)+0.5)*0x1p-23;
}

static double radical_inverse(uint32_t index, unsigned base) {
    double step=1.0/base, result=0;
    while (index) {
        result+=(index%base)*step;
        index/=base;
        step/=base;
    }
    return result;
}

double vs_sample(VsSample sample, uint32_t dimension) {
    static const unsigned bases[]={2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53};
    uint32_t key=vs_hash32(sample.seed^vs_hash32(sample.pixel)^vs_hash32(dimension+1));
    double random=unit(vs_hash32(key^vs_hash32(sample.index)));
    if (sample.mode==VS_STRATIFIED) {
        uint32_t block=vs_hash32(key^(sample.index>>4));
        uint32_t stratum=((sample.index&15u)*(block|1u)+(block>>16))&15u;
        return (stratum+random)/16;
    }
    if (sample.mode==VS_HALTON && dimension<sizeof bases/sizeof *bases) {
        double x=radical_inverse(sample.index,bases[dimension])+unit(key);
        return x-floor(x);
    }
    return random;
}

double vs_wavelength_pdf(double nm, VsWavelengthMode mode) {
    if (nm<VS_LAMBDA_MIN || nm>VS_LAMBDA_MAX) return 0;
    double uniform=1/(VS_LAMBDA_MAX-VS_LAMBDA_MIN);
    if (mode==VS_WAVELENGTH_UNIFORM) return uniform;
    return .5*(uniform+fmax(0,1-fabs(nm-550)/100)/100);
}

VsWavelength vs_wavelength(double u, VsWavelengthMode mode) {
    double nm;
    if (mode==VS_WAVELENGTH_MIXTURE && u>=.5) {
        double v=2*u-1;
        nm=v<.5?450+100*sqrt(2*v):650-100*sqrt(2*(1-v));
    } else {
        nm=VS_LAMBDA_MIN+(VS_LAMBDA_MAX-VS_LAMBDA_MIN)*
            (mode==VS_WAVELENGTH_MIXTURE?2*u:u);
    }
    return (VsWavelength){nm,vs_wavelength_pdf(nm,mode)};
}

VsSpectrum vs_flat(double value) {
    VsSpectrum s={0};
    for (unsigned i=0;i<VS_SPECTRAL_KNOTS;++i) s.values[i]=value;
    return s;
}

VsSpectrum vs_gaussian(double amplitude, double center_nm, double sigma_nm) {
    return (VsSpectrum){.kind=VS_GAUSSIAN,.values={amplitude,center_nm,sigma_nm}};
}

VsSpectrum vs_model_rgb(VsVec3 rgb) {
    VsSpectrum s={0};
    for (unsigned i=0;i<VS_SPECTRAL_KNOTS;++i) {
        double nm=VS_LAMBDA_MIN+i*(VS_LAMBDA_MAX-VS_LAMBDA_MIN)/(VS_SPECTRAL_KNOTS-1);
        double rb=exp(-pow((nm-610)/53,2));
        double gb=exp(-pow((nm-545)/42,2));
        double bb=exp(-pow((nm-450)/45,2));
        s.values[i]=fmin(1,fmax(0,(rgb.x*rb+rgb.y*gb+rgb.z*bb)/(rb+gb+bb)));
    }
    return s;
}

bool vs_spectrum_valid(VsSpectrum s, double limit) {
    if (s.kind==VS_GAUSSIAN)
        return s.values[0]>=0 && s.values[0]<=limit && isfinite(s.values[0]) &&
            s.values[1]>=VS_LAMBDA_MIN && s.values[1]<=VS_LAMBDA_MAX &&
            s.values[2]>=.01 && s.values[2]<=1000;
    if (s.kind!=VS_TABULATED) return false;
    for (unsigned i=0;i<VS_SPECTRAL_KNOTS;++i)
        if (!(s.values[i]>=0 && s.values[i]<=limit && isfinite(s.values[i]))) return false;
    return true;
}

double vs_spectrum(VsSpectrum s, double nm) {
    if (s.kind==VS_GAUSSIAN) {
        double z=(nm-s.values[1])/s.values[2];
        return s.values[0]*exp(-.5*z*z);
    }
    double x=fmin(VS_SPECTRAL_KNOTS-1,fmax(0,(nm-VS_LAMBDA_MIN)*
                     (VS_SPECTRAL_KNOTS-1)/(VS_LAMBDA_MAX-VS_LAMBDA_MIN)));
    unsigned i=(unsigned)fmin(x,VS_SPECTRAL_KNOTS-2);
    return s.values[i]+(x-i)*(s.values[i+1]-s.values[i]);
}

VsVec3 vs_observer(double nm) {
    if (!(nm>=VS_LAMBDA_MIN && nm<=VS_LAMBDA_MAX)) return vs_v(0,0,0);
    double x=nm-VS_LAMBDA_MIN;
    unsigned i=(unsigned)fmin(x,sizeof observer/sizeof *observer-2);
    return vs_add(observer[i],vs_mul(vs_sub(observer[i+1],observer[i]),x-i));
}

static double display_channel(double x) {
    x=fmax(0,x);
    x=isfinite(x)?x/(1+x):1;
    return x<=.0031308?12.92*x:1.055*pow(x,1.0/2.4)-.055;
}

VsVec3 vs_display(VsVec3 xyz, double exposure) {
    /* The 106.8569 divisor is a display gain for unit equal-energy density. */
    xyz=vs_mul(xyz,exp2(exposure)/106.8569);
    return vs_v(display_channel(3.2406*xyz.x-1.5372*xyz.y-.4986*xyz.z),
                display_channel(-.9689*xyz.x+1.8758*xyz.y+.0415*xyz.z),
                display_channel(.0557*xyz.x-.2040*xyz.y+1.0570*xyz.z));
}
