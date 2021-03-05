#include "render_budget.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

bool vs_film_init(VsFilm *film,const VsScene *s,VsRender cfg,VsGate g,uint64_t device,VsError *error) {
    if (!s->identity || !vs_render_valid(cfg,error)) return vs_fail(error,"scene or render is invalid");
    if (g.bins>VS_MAX_BINS || (g.bins && !(g.width>=1e-15 && g.width<=1 &&
        isfinite(g.start) && fabs(g.start)<=1 && g.start+g.width>g.start)) ||
        (!g.bins && (g.start!=0 || g.width!=0))) return vs_fail(error,"invalid temporal gate");
    if ((cfg.backend==VS_BACKEND_CPU)!=(device==0))
        return vs_fail(error,"invalid backend/device contract");
    uint64_t count=(uint64_t)cfg.width*cfg.height, bins=count*g.bins*3;
    if (count*sizeof(VsPixel)+bins*sizeof(float)>VS_FILM_BUDGET)
        return vs_fail(error,"film exceeds %llu MiB budget",(unsigned long long)(VS_FILM_BUDGET>>20));
    VsFilm next={.render=cfg,.gate=g,.scene=s->identity,.device=device};
    next.pixels=calloc((size_t)count,sizeof *next.pixels);
    next.time=bins?calloc((size_t)bins,sizeof *next.time):NULL;
    if (!next.pixels || (bins && !next.time)) {
        vs_film_destroy(&next);
        return vs_fail(error,"film allocation failed");
    }
    vs_film_destroy(film);
    *film=next;
    return true;
}

void vs_film_destroy(VsFilm *f) {
    if (!f) return;
    free(f->pixels);
    free(f->time);
    *f=(VsFilm){0};
}

void vs_film_clear(VsFilm *f) {
    size_t count=(size_t)f->render.width*f->render.height;
    memset(f->pixels,0,count*sizeof *f->pixels);
    if (f->time) memset(f->time,0,count*f->gate.bins*3*sizeof *f->time);
}

static double deposit(VsFilm *f,uint32_t pixel,const VsContribution *c,VsVec3 sensor,double response) {
    VsGate g=f->gate;
    double start=c->delay+c->pulse.start, stop=start+c->pulse.width+response;
    if (!g.bins || stop<g.start || start>=g.start+g.bins*g.width) return 0;
    uint32_t first=(uint32_t)fmin(g.bins,fmax(0,floor((start-g.start)/g.width)));
    uint32_t last=(uint32_t)fmin(g.bins,fmax(first+1,floor((stop-g.start)/g.width)+1));
    float *out=f->time+(size_t)pixel*g.bins*3;
    double cdf=vs_pulse_cdf(g.start+first*g.width,start,c->pulse.width,response);
    double kept=0;
    for (uint32_t b=first;b<last;++b) {
        double next=vs_pulse_cdf(g.start+(b+1)*g.width,start,c->pulse.width,response);
        double value=c->value*(next-cdf);
        out[3*b]+=(float)(sensor.x*value);
        out[3*b+1]+=(float)(sensor.y*value);
        out[3*b+2]+=(float)(sensor.z*value);
        kept+=value;
        cdf=next;
    }
    return kept;
}

static bool path_radiance(const VsPath *p,double *radiance,VsError *error) {
    if (p->count>VS_MAX_EVENTS || !(p->wavelength.nm>=VS_LAMBDA_MIN &&
        p->wavelength.nm<=VS_LAMBDA_MAX && p->wavelength.pdf>0 && isfinite(p->wavelength.pdf)))
        return vs_fail(error,"invalid wavelength or path event count");
    double total=0;
    for (uint32_t i=0;i<p->count;++i) {
        const VsContribution *c=p->events+i;
        if (!(c->value>=0 && isfinite(c->value) && c->delay>=0 && isfinite(c->delay) &&
              c->pulse.width>=0 && c->pulse.width<=1 && fabs(c->pulse.start)<=1))
            return vs_fail(error,"invalid path contribution");
        total+=c->value;
    }
    if (!(total<FLT_MAX/(4.0*VS_MAX_SPP))) return vs_fail(error,"film sample exceeds numeric range");
    *radiance=total;
    return true;
}

bool vs_film_add(VsFilm *f,uint32_t pixel,const VsPath *p,double response,VsError *error) {
    if (!f->pixels || pixel>=f->render.width*f->render.height || !(response>=0 && response<=1))
        return vs_fail(error,"invalid film sample");
    VsPixel *dst=f->pixels+pixel;
    if (dst->samples>=VS_MAX_SPP) return vs_fail(error,"sample index limit reached");
    double total;
    if (!path_radiance(p,&total,error)) return false;
    VsVec3 sensor=vs_observer(p->wavelength.nm);
    double timed=0,kept=0;
    for (uint32_t i=0;i<p->count;++i) {
        const VsContribution *c=p->events+i;
        if (!c->timed) continue;
        timed+=c->value;
        kept+=deposit(f,pixel,c,sensor,response);
    }
    uint32_t n=++dst->samples;
    VsVec3 xyz=vs_mul(sensor,total), delta=vs_sub(xyz,dst->mean);
    dst->mean=vs_add(dst->mean,vs_mul(delta,1.0/n));
    dst->m2_y+=delta.y*(xyz.y-dst->mean.y);
    dst->timed_y+=(timed*sensor.y-dst->timed_y)/n;
    dst->excluded_y+=((total-kept)*sensor.y-dst->excluded_y)/n;
    unsigned band=(unsigned)fmin(VS_SPECTRAL_KNOTS-1,(p->wavelength.nm-VS_LAMBDA_MIN)*
                                VS_SPECTRAL_KNOTS/(VS_LAMBDA_MAX-VS_LAMBDA_MIN));
    dst->spectral[band]+=(float)total;
    return true;
}

VsVec3 vs_film_slice(const VsFilm *f,uint32_t pixel,uint32_t bin) {
    if (bin>=f->gate.bins || !f->pixels[pixel].samples) return vs_v(0,0,0);
    const float *value=f->time+((size_t)pixel*f->gate.bins+bin)*3;
    return vs_mul(vs_v(value[0],value[1],value[2]),1.0/f->pixels[pixel].samples);
}

double vs_film_variance(const VsPixel *p) {
    return p->samples>1?p->m2_y/(p->samples-1):0;
}

bool vs_region_valid(const VsFilm *f,VsRegion r) {
    return r.width && r.height && r.x<f->render.width && r.y<f->render.height &&
        r.width<=f->render.width-r.x && r.height<=f->render.height-r.y;
}

static void count_path(VsStats *stats,const VsPath *p) {
    if (!stats) return;
    ++stats->samples;
    stats->rays+=p->rays;
    stats->shadows+=p->shadow_rays;
    stats->nulls+=p->nulls;
}

bool vs_adaptive_budget(const VsScene *s,VsRender render,VsRegion region,
                        uint32_t max_spp,double relative_error,uint32_t *targets,
                        VsStats *pilot_stats,VsError *error) {
    if (!s->identity || !targets || !vs_render_valid(render,error) ||
        !vs_region_valid(&(VsFilm){.render=render},region) ||
        max_spp<VS_ADAPTIVE_PILOT_SPP || max_spp>VS_MAX_SPP ||
        !(relative_error>=.001 && relative_error<=1) || render.sampler!=VS_RANDOM)
        return vs_fail(error,"adaptive sampling requires random sampling, 16 or more spp and relative error in [.001,1]");
    /* The pilot uses a disjoint seed and never writes to the estimated film. */
    VsRender pilot=render;
    pilot.backend=VS_BACKEND_CPU;
    pilot.seed^=UINT32_C(0xa511e9b3);
    for (uint32_t y=0;y<region.height;++y) for (uint32_t x=0;x<region.width;++x) {
        uint32_t pixel=(region.y+y)*render.width+region.x+x;
        double mean=0,m2=0;
        for (uint32_t n=1;n<=VS_ADAPTIVE_PILOT_SPP;++n) {
            VsPath path;
            double total;
            if (!vs_trace_path(s,pilot,pixel,n-1,&path,NULL,error) || !path_radiance(&path,&total,error))
                return false;
            double measurement=vs_observer(path.wavelength.nm).y*total;
            double delta=measurement-mean;
            mean+=delta/n;
            m2+=delta*(measurement-mean);
            count_path(pilot_stats,&path);
        }
        double variance=m2/(VS_ADAPTIVE_PILOT_SPP-1);
        double scale=relative_error*fmax(1,mean);
        double wanted=ceil(variance/(scale*scale));
        targets[y*region.width+x]=(uint32_t)fmax(VS_ADAPTIVE_PILOT_SPP,fmin(max_spp,wanted));
    }
    return true;
}

bool vs_render_cpu_budget(const VsScene *s,VsFilm *f,VsSampleBudget budget,
                          VsStats *stats,VsError *error) {
    if (!f->pixels || f->render.backend!=VS_BACKEND_CPU || f->scene!=s->identity ||
        !vs_budget_valid(f,budget))
        return vs_fail(error,"invalid CPU render region, scene or sample target");
    VsRegion region=budget.region;
    const uint32_t tile=16;
    for (uint32_t ty=region.y;ty<region.y+region.height;ty+=tile)
        for (uint32_t tx=region.x;tx<region.x+region.width;tx+=tile)
            for (uint32_t y=ty;y<ty+tile && y<region.y+region.height;++y)
                for (uint32_t x=tx;x<tx+tile && x<region.x+region.width;++x) {
                    uint32_t id=y*f->render.width+x;
                    uint32_t target=vs_budget_target(budget,x,y);
                    for (uint32_t i=f->pixels[id].samples;i<target;++i) {
                        VsPath p;
                        if (!vs_trace_path(s,f->render,id,i,&p,NULL,error) ||
                            !vs_film_add(f,id,&p,s->response,error)) return false;
                        count_path(stats,&p);
                    }
                }
    return true;
}


bool vs_render_cpu(const VsScene *s,VsFilm *f,VsRegion region,uint32_t target,
                   VsStats *stats,VsError *error) {
    return vs_render_cpu_budget(s,f,(VsSampleBudget){region,target,NULL},stats,error);
}
