#include "vesper/scatter.h"

double vs_fresnel(double cos_i,double ni,double nt) {
    if (ni==nt) return 0;
    cos_i=fmin(1,fmax(0,cos_i));
    double ratio=ni/nt, sin2=ratio*ratio*(1-cos_i*cos_i);
    if (sin2>=1) return 1;
    double cos_t=sqrt(1-sin2);
    double s=(ni*cos_i-nt*cos_t)/(ni*cos_i+nt*cos_t);
    double p=(nt*cos_i-ni*cos_t)/(nt*cos_i+ni*cos_t);
    return .5*(s*s+p*p);
}

double vs_conductor_fresnel(double cosine,double eta,double k) {
    if (eta==1 && k==0) return 0;
    double c=fmin(1,fmax(0,cosine)), c2=c*c, s2=1-c2;
    double a=eta*eta-k*k-s2, b=hypot(a,2*eta*k);
    double x=sqrt(.5*(b+a));
    double rs=(b+c2-2*x*c)/(b+c2+2*x*c);
    double rp=rs*(c2*b+s2*s2-2*x*c*s2)/(c2*b+s2*s2+2*x*c*s2);
    return fmin(1,fmax(0,.5*(rs+rp)));
}

VsVec3 vs_direction(VsVec3 axis,double cosine,double phi) {
    VsVec3 basis=fabs(axis.z)<.9?vs_v(0,0,1):vs_v(0,1,0);
    VsVec3 x=vs_unit(vs_cross(basis,axis)), y=vs_cross(axis,x);
    double r=sqrt(fmax(0,1-cosine*cosine));
    return vs_add(vs_mul(axis,cosine),vs_add(vs_mul(x,r*cos(phi)),vs_mul(y,r*sin(phi))));
}

static double distribution(double c,double alpha) {
    double aa=alpha*alpha, d=1-c*c+aa*c*c;
    return aa/(VS_PI*d*d);
}

static double masking(double c,double alpha) {
    return 2*c/(c+sqrt(c*c+alpha*alpha*(1-c*c)));
}

VsBsdfValue vs_bsdf_eval(const VsMaterial *m,double nm,VsVec3 n,VsVec3 wo,VsVec3 wi,double ni,double nt) {
    double co=vs_dot(wo,n), ci=vs_dot(wi,n);
    VsBsdfValue value={0};
    if (!(co>0) || ci==0) return value;
    if (m->kind==VS_DIFFUSE) {
        if (ci>0) value=(VsBsdfValue){vs_spectrum(m->albedo,nm)/VS_PI,ci/VS_PI};
        return value;
    }
    bool metal=m->kind==VS_CONDUCTOR, transmission=ci<0;
    if ((!metal && m->kind!=VS_ROUGH_DIELECTRIC) || (metal && transmission) || (!metal && ni==nt))
        return value;
    double eta=nt/ni;
    VsVec3 raw=vs_add(wo,vs_mul(wi,transmission?eta:1));
    if (vs_dot(raw,raw)==0) return value;
    VsVec3 h=vs_unit(raw);
    if (vs_dot(h,n)<0) h=vs_mul(h,-1);
    double ch=vs_dot(h,n), oh=vs_dot(wo,h), ih=vs_dot(wi,h);
    if (!(ch>0 && oh>0) || (transmission?ih>=0:ih<=0)) return value;
    double alpha=m->roughness*m->roughness;
    double d=distribution(ch,alpha), g=masking(co,alpha)*masking(fabs(ci),alpha);
    double f=metal?vs_conductor_fresnel(oh,vs_spectrum(m->eta,nm)/ni,vs_spectrum(m->k,nm)/ni):
        vs_fresnel(oh,ni,nt);
    if (!transmission)
        return (VsBsdfValue){f*d*g/(4*co*ci),d*ch/(4*oh)*(metal?1:f)};
    double denominator=oh+eta*ih;
    if (denominator==0) return value;
    double jacobian=fabs(ih)/(denominator*denominator);
    return (VsBsdfValue){(1-f)*d*g*oh*jacobian/(co*fabs(ci)),(1-f)*d*ch*eta*eta*jacobian};
}

static VsVec3 reflect(VsVec3 wo, VsVec3 h) {
    return vs_sub(vs_mul(h,2*vs_dot(wo,h)),wo);
}

static VsVec3 refract(VsVec3 wo, VsVec3 h,double ni,double nt) {
    double ratio=ni/nt, c=vs_dot(wo,h), ct=sqrt(fmax(0,1-ratio*ratio*(1-c*c)));
    return vs_sub(vs_mul(h,ratio*c-ct),vs_mul(wo,ratio));
}

VsScatter vs_bsdf_sample(const VsMaterial *m,double nm,VsVec3 n,VsVec3 wo,double ni,double nt,
                         double u,double v,double branch) {
    VsScatter s={0};
    if (m->kind==VS_NULL || (m->kind==VS_ROUGH_DIELECTRIC && ni==nt))
        return (VsScatter){vs_mul(wo,-1),1,1,true,true};
    if (m->kind==VS_DIELECTRIC) {
        double f=vs_fresnel(vs_dot(wo,n),ni,nt);
        if (branch<f) return (VsScatter){reflect(wo,n),1,f,true,false};
        return (VsScatter){refract(wo,n,ni,nt),(ni/nt)*(ni/nt),1-f,true,true};
    }
    if (m->kind==VS_DIFFUSE) s.wi=vs_direction(n,sqrt(u),2*VS_PI*v);
    else {
        double alpha=m->roughness*m->roughness;
        double cosine=sqrt((1-u)/(1-u+alpha*alpha*u));
        VsVec3 h=vs_direction(n,cosine,2*VS_PI*v);
        double oh=vs_dot(wo,h);
        if (oh<=0) return s;
        s.transmit=m->kind==VS_ROUGH_DIELECTRIC && branch>=vs_fresnel(oh,ni,nt);
        s.wi=s.transmit?refract(wo,h,ni,nt):reflect(wo,h);
        if ((vs_dot(s.wi,n)<0)!=s.transmit) return (VsScatter){0};
    }
    VsBsdfValue value=vs_bsdf_eval(m,nm,n,wo,s.wi,ni,nt);
    s.pdf=value.pdf;
    if (s.pdf>0) s.weight=value.f*fabs(vs_dot(n,s.wi))/s.pdf;
    return s;
}

double vs_phase(double cosine,double g) {
    double k=1+g*g-2*g*fmin(1,fmax(-1,cosine));
    return (1-g*g)/(4*VS_PI*k*sqrt(k));
}

VsVec3 vs_phase_sample(VsVec3 forward,double g,double u,double v) {
    double x=2*u-1;
    if (fabs(g)>1e-6) {
        double r=(1-g*g)/(1+g*x);
        x=fmin(1,fmax(-1,(1+g*g-r*r)/(2*g)));
    }
    return vs_direction(forward,x,2*VS_PI*v);
}
