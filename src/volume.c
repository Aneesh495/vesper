#include "vesper/volume.h"

double vs_density(const VsMedium *m,VsVec3 p) {
    if (!m->density) return 1;
    double t[3], f[3];
    uint32_t i[3], size[]={m->nx,m->ny,m->nz};
    for (unsigned a=0;a<3;++a) {
        double lo=vs_at(m->bounds.lo,a), hi=vs_at(m->bounds.hi,a);
        t[a]=(vs_at(p,a)-lo)/(hi-lo);
        if (!(t[a]>=0 && t[a]<=1)) return 0;
        t[a]*=size[a]-1;
        i[a]=(uint32_t)fmin(t[a],size[a]-2);
        f[a]=t[a]-i[a];
    }
    double result=0;
    for (unsigned z=0;z<2;++z) for (unsigned y=0;y<2;++y) for (unsigned x=0;x<2;++x) {
        size_t index=((size_t)(i[2]+z)*m->ny+i[1]+y)*m->nx+i[0]+x;
        double w=(x?f[0]:1-f[0])*(y?f[1]:1-f[1])*(z?f[2]:1-f[2]);
        result+=w*m->density[index];
    }
    return result;
}

static bool active_interval(const VsMedium *m,VsRay r,double *start,double *end) {
    *start=0;
    if (!m->density) return *end>0;
    for (unsigned axis=0;axis<3;++axis) {
        double o=vs_at(r.origin,axis),d=vs_at(r.direction,axis);
        double a=vs_at(m->bounds.lo,axis),b=vs_at(m->bounds.hi,axis);
        if (d==0) {
            if (o<a || o>b) return false;
        } else {
            double x=(a-o)/d,y=(b-o)/d;
            *start=fmax(*start,fmin(x,y));
            *end=fmin(*end,fmax(x,y));
        }
    }
    return *end>*start;
}

static bool ratio(const VsMedium *m,VsRay ray,double t,double sigma,double *r,VsError *error) {
    double extinction=sigma*vs_density(m,vs_add(ray.origin,vs_mul(ray.direction,t)));
    if (extinction>m->majorant)
        return vs_fail(error,"medium %s violated its majorant at %.12g m",m->name,t);
    *r=extinction/m->majorant;
    return true;
}

bool vs_collision(const VsMedium *m,VsRay ray,double end,double nm,VsSample rng,uint32_t dim,
                  VsCollision *out,VsError *error) {
    *out=(VsCollision){0};
    double scatter=vs_spectrum(m->scattering,nm);
    double sigma=scatter+vs_spectrum(m->absorption,nm);
    double t;
    if (sigma==0 || !active_interval(m,ray,&t,&end)) return true;
    if (!m->density) {
        t=-log1p(-vs_sample(rng,dim))/sigma;
        if (t<end) *out=(VsCollision){t,scatter/sigma,0,true};
        return true;
    }
    for (uint32_t k=0;k<VS_TRACK_LIMIT;++k) {
        t-=log1p(-vs_sample(rng,dim+2*k))/m->majorant;
        if (t>=end) return true;
        double r;
        if (!ratio(m,ray,t,sigma,&r,error)) return false;
        if (vs_sample(rng,dim+2*k+1)<r) {
            *out=(VsCollision){t,scatter/sigma,k,true};
            return true;
        }
        ++out->nulls;
    }
    return vs_fail(error,"free-flight tracking exceeded %u proposals",VS_TRACK_LIMIT);
}

bool vs_transmittance(const VsMedium *m,VsRay ray,double end,double nm,VsSample rng,uint32_t dim,
                      double *out,uint32_t *nulls,VsError *error) {
    *out=1;
    double sigma=vs_spectrum(m->scattering,nm)+vs_spectrum(m->absorption,nm);
    double t;
    if (sigma==0 || !active_interval(m,ray,&t,&end)) return true;
    if (!m->density) {
        *out=exp(-sigma*end);
        return true;
    }
    for (uint32_t k=0;k<VS_TRACK_LIMIT;++k) {
        t-=log1p(-vs_sample(rng,dim+k))/m->majorant;
        if (t>=end) return true;
        double r;
        if (!ratio(m,ray,t,sigma,&r,error)) return false;
        *out*=1-r;
        ++*nulls;
        if (*out==0) return true;
    }
    return vs_fail(error,"ratio tracking exceeded %u proposals",VS_TRACK_LIMIT);
}

double vs_pulse_cdf(double time,double start,double pulse,double response) {
    if (pulse==0 && response==0) return time>start?1:0;
    double t=time-start, a=fmax(pulse,response), b=fmin(pulse,response);
    if (t<=0) return 0;
    if (t>=a+b) return 1;
    if (b==0) return t/a;
    if (t<b) return .5*(t/a)*(t/b);
    if (t<a) return (t-.5*b)/a;
    double tail=a+b-t;
    return 1-.5*(tail/a)*(tail/b);
}
