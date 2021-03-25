#include "vesper/transport.h"
#include <float.h>

typedef struct { uint32_t medium,id; double phase,group; } Interior;
typedef struct { Interior state[VS_MAX_BOUNDARIES]; uint32_t size; } MediumStack;

typedef struct {
    VsVec3 wi,endpoint;
    double distance,radiance,pdf;
    bool delta,timed;
    VsPulse pulse;
} Connection;

typedef struct {
    VsVec3 point,n,shading,wo,previous;
    const VsMaterial *material;
    double delay,beta,eta_balance,previous_pdf,g;
    uint32_t depth,primitive,iteration;
    bool previous_delta,volume;
} Vertex;

static Interior current(const MediumStack *s) { return s->state[s->size-1]; }

static bool cross_boundary(const MediumStack *s,const VsPrimitive *p,const VsMaterial *m,
                           bool front,double nm,MediumStack *next,VsError *error) {
    *next=*s;
    if (!front) {
        if (s->size<=1 || current(s).id!=p->boundary)
            return vs_fail(error,"boundary %u exit did not match the medium stack",p->boundary);
        --next->size;
        return true;
    }
    if (s->size==VS_MAX_BOUNDARIES || !p->boundary)
        return vs_fail(error,"boundary stack overflow or unassigned boundary");
    for (uint32_t i=1;i<s->size;++i)
        if (s->state[i].id==p->boundary) return vs_fail(error,"boundary %u was entered twice",p->boundary);
    double phase=m->kind==VS_NULL?current(s).phase:vs_phase_index(m,nm);
    double group=m->kind==VS_NULL?current(s).group:vs_group_index(m,nm);
    next->state[next->size++]=(Interior){m->medium,p->boundary,phase,group};
    return true;
}

static void record(VsTrace *trace,VsEventKind kind,Vertex v,uint32_t medium,
                   double distance,double pdf,double weight,bool delta) {
    if (!trace) return;
    if (trace->count>=VS_TRACE_CAP) { ++trace->dropped; return; }
    trace->events[trace->count++]=(VsTraceEvent){kind,v.depth,medium,v.primitive,
        distance,v.delay,pdf,weight,v.beta,delta};
}

static bool contribute(VsPath *path,double value,double delay,VsPulse pulse,bool timed,VsError *error) {
    if (value==0) return true;
    if (!(value>0 && isfinite(value) && isfinite(delay)) || path->count>=VS_MAX_EVENTS)
        return vs_fail(error,"nonfinite contribution or path event capacity exceeded");
    path->events[path->count++]=(VsContribution){value,delay,pulse,timed};
    return true;
}

static double mis(double a,double b) {
    if (b==0) return 1;
    double r=b/a;
    return 1/(1+r*r);
}

static double area_pdf(const VsPrimitive *p,VsVec3 wi,VsVec3 n,double distance,uint32_t lights) {
    double cosine=-vs_dot(wi,n);
    return cosine>0?distance*distance/(cosine*vs_area(p)*lights):0;
}

static Connection light_sample(const VsScene *s,VsVec3 from,double time,double nm,
                               uint32_t index,double u,double v) {
    const VsLight *l=s->lights+index;
    double le=vs_spectrum(l->power,nm), selection=1.0/s->light_count;
    Connection c={.radiance=le,.pulse=l->pulse,.distance=INFINITY,.pdf=selection};
    if (l->kind==VS_ENVIRONMENT) {
        c.wi=vs_direction(vs_v(0,0,1),2*u-1,2*VS_PI*v);
        c.pdf*=1/(4*VS_PI);
        return c;
    }
    if (l->kind==VS_DISTANT) {
        c.wi=vs_unit(l->vector);
        c.delta=true;
        return c;
    }
    VsVec3 endpoint=l->vector, normal={0};
    const VsPrimitive *p=NULL;
    if (l->kind==VS_AREA) {
        p=s->primitives+l->primitive;
        if (p->shape==VS_SPHERE) {
            normal=vs_direction(vs_v(0,0,1),2*u-1,2*VS_PI*v);
            endpoint=vs_add(p->a,vs_mul(normal,p->radius));
        } else {
            double r=sqrt(u);
            endpoint=vs_add(vs_mul(p->a,1-r),vs_add(vs_mul(p->b,r*(1-v)),vs_mul(p->c,r*v)));
            normal=vs_unit(vs_cross(vs_sub(p->b,p->a),vs_sub(p->c,p->a)));
        }
        endpoint=vs_add(endpoint,vs_mul(p->velocity,time));
    }
    VsVec3 delta=vs_sub(endpoint,from);
    c.distance=vs_length(delta);
    if (c.distance<=1e-12) return (Connection){0};
    c.wi=vs_mul(delta,1/c.distance);
    c.endpoint=endpoint;
    c.timed=true;
    if (p) c.pdf=area_pdf(p,c.wi,normal,c.distance,s->light_count);
    else {
        c.radiance/=c.distance*c.distance;
        c.delta=true;
    }
    return c;
}

static bool shadow(const VsScene *s,Vertex vertex,Connection c,double time,VsSample rng,
                   double nm,MediumStack media,VsPath *out,double *tr,double *delay,VsError *error) {
    *tr=1;
    *delay=vertex.delay;
    VsVec3 origin=vertex.volume?vertex.point:vs_offset(s->primitives+vertex.primitive,vertex.point,vertex.n,c.wi);
    VsVec3 position=vertex.point;
    VsVec3 endpoint=c.endpoint;
    for (uint32_t i=0;i<64;++i) {
        Interior state=current(&media);
        VsVec3 toward=vs_sub(endpoint,origin);
        double length=c.timed?vs_length(toward):INFINITY;
        if (length==0) return true;
        VsVec3 direction=c.timed?vs_mul(toward,1/length):c.wi;
        double tolerance=128*DBL_EPSILON*(vs_length(origin)+(c.timed?vs_length(endpoint)+length:0));
        VsRay ray={origin,direction,0,fmax(0,length-tolerance),time};
        VsHit hit;
        ++out->shadow_rays;
        bool blocked=vs_bvh_hit(&s->bvh,s->primitives,ray,&hit);
        double segment=blocked?hit.t:length;
        double attenuate;
        uint32_t dim=UINT32_C(0x80000000)+(vertex.iteration*64u+i)*VS_TRACK_LIMIT;
        if (!vs_transmittance(s->media+state.medium,ray,segment,nm,rng,dim,
                              &attenuate,&out->nulls,error)) return false;
        *tr*=attenuate;
        if (c.timed) {
            VsVec3 next=blocked?hit.point:endpoint;
            *delay+=vs_length(vs_sub(next,position))*state.group/VS_C;
            position=next;
        }
        if (!blocked || *tr==0) return true;
        const VsPrimitive *p=s->primitives+hit.primitive;
        const VsMaterial *m=s->materials+p->material;
        if (m->kind!=VS_NULL) { *tr=0; return true; }
        if (!cross_boundary(&media,p,m,hit.front,nm,&media,error)) return false;
        origin=vs_offset(p,hit.point,hit.normal,ray.direction);
    }
    return vs_fail(error,"shadow traversal exceeded transparent boundary limit");
}

static bool direct(const VsScene *s,Vertex v,VsRay ray,VsSample rng,uint32_t dim,VsPath *out,
                   const MediumStack *medium,const MediumStack *next,VsTrace *trace,VsError *error) {
    if (!s->light_count) return true;
    uint32_t index=(uint32_t)fmin(s->light_count-1,vs_sample(rng,dim)*s->light_count);
    double nm=out->wavelength.nm;
    Connection c=light_sample(s,v.point,ray.time,nm,index,vs_sample(rng,dim+1),vs_sample(rng,dim+2));
    if (!(c.pdf>0 && c.radiance>0)) return true;
    double f,pdf;
    const MediumStack *side=medium;
    if (v.volume) f=pdf=vs_phase(vs_dot(ray.direction,c.wi),v.g);
    else {
        double cosine=vs_dot(v.n,c.wi);
        if (cosine<0) side=next;
        VsBsdfValue b=vs_bsdf_eval(v.material,nm,v.n,v.wo,c.wi,current(medium).phase,current(next).phase);
        f=b.f*fabs(cosine); pdf=b.pdf;
        if (v.material->normal_map.rgb) f*=vs_shading_factor(v.n,v.shading,c.wi);
    }
    if (!(f>0)) return true;
    double tr,arrival;
    if (!shadow(s,v,c,ray.time,rng,nm,*side,out,&tr,&arrival,error)) return false;
    double weight=c.delta?1:mis(c.pdf,pdf);
    record(trace,VS_EVENT_SHADOW,v,current(side).medium,c.distance,c.pdf,tr*weight,c.delta);
    return contribute(out,v.beta*c.radiance*f*tr*weight/c.pdf,arrival,c.pulse,c.timed,error);
}

bool vs_render_valid(VsRender c,VsError *error) {
    return (c.width && c.height && c.width<=8192 && c.height<=8192 &&
            (uint64_t)c.width*c.height<=16777216u && c.max_depth<=VS_MAX_DEPTH &&
            (unsigned)c.sampler<=VS_HALTON && (unsigned)c.wavelength<=VS_WAVELENGTH_MIXTURE &&
            (unsigned)c.backend<=VS_BACKEND_VULKAN) || vs_fail(error,"invalid render configuration");
}

bool vs_trace_path(const VsScene *s,VsRender cfg,uint32_t pixel,uint32_t index,
                   VsPath *out,VsTrace *trace,VsError *error) {
    if (!s || !s->identity || !out || !vs_render_valid(cfg,error) ||
        cfg.backend!=VS_BACKEND_CPU || pixel>=cfg.width*cfg.height)
        return vs_fail(error,"trace requires a compiled scene, CPU configuration and valid pixel");
    *out=(VsPath){0};
    if (trace) *trace=(VsTrace){0};
    VsSample rng={pixel,index,cfg.seed,cfg.sampler};
    VsRay ray=vs_camera_ray(s->cameras+s->camera,cfg.width,cfg.height,rng);
    out->wavelength=vs_wavelength(vs_sample(rng,5),cfg.wavelength);
    double nm=out->wavelength.nm;
    Vertex v={.point=ray.origin,.previous=ray.origin,.beta=1/out->wavelength.pdf,
              .eta_balance=1,.primitive=UINT32_MAX,.previous_delta=true};
    MediumStack media={.state={{s->world_medium,0,1,1}},.size=1};
    record(trace,VS_EVENT_WAVELENGTH,v,s->world_medium,nm,out->wavelength.pdf,v.beta,false);
    for (v.iteration=0;v.iteration<128;++v.iteration) {
        VsHit hit;
        ++out->rays;
        Interior state=current(&media);
        bool surface=vs_bvh_hit(&s->bvh,s->primitives,ray,&hit);
        VsCollision collision;
        uint32_t dim=6+v.depth*12;
        uint32_t tracking=UINT32_C(0x40000000)+v.iteration*2u*VS_TRACK_LIMIT;
        if (!vs_collision(s->media+state.medium,ray,surface?hit.t:INFINITY,nm,rng,tracking,
                          &collision,error)) return false;
        out->nulls+=collision.nulls;
        if (!surface && !collision.collision) {
            if (s->environment!=UINT32_MAX) {
                const VsLight *l=s->lights+s->environment;
                double p=1/(4*VS_PI*s->light_count);
                double w=cfg.nee&&!v.previous_delta?mis(v.previous_pdf,p):1;
                if (!contribute(out,v.beta*w*vs_spectrum(l->power,nm),v.delay,l->pulse,false,error))
                    return false;
            }
            record(trace,VS_EVENT_END,v,state.medium,INFINITY,0,0,false);
            return true;
        }
        VsVec3 point=collision.collision?vs_add(ray.origin,vs_mul(ray.direction,collision.t)):hit.point;
        double distance=vs_length(vs_sub(point,v.point));
        v.delay+=distance*state.group/VS_C;
        v.point=point;
        v.wo=vs_mul(ray.direction,-1);
        v.volume=collision.collision;
        v.primitive=v.volume?UINT32_MAX:hit.primitive;
        MediumStack next=media;
        VsScatter scatter={0};
        if (v.volume) {
            v.beta*=collision.albedo;
            v.g=s->media[state.medium].anisotropy;
            record(trace,VS_EVENT_VOLUME,v,state.medium,distance,0,collision.albedo,false);
            if (v.depth>=cfg.max_depth || v.beta==0) break;
            if (cfg.nee && !direct(s,v,ray,rng,dim,out,&media,&next,trace,error)) return false;
            scatter.wi=vs_phase_sample(ray.direction,v.g,vs_sample(rng,dim+3),vs_sample(rng,dim+4));
            scatter.pdf=vs_phase(vs_dot(ray.direction,scatter.wi),v.g);
            scatter.weight=1;
        } else {
            const VsPrimitive *p=s->primitives+hit.primitive;
            v.material=s->materials+p->material;
            v.n=hit.front?hit.normal:vs_mul(hit.normal,-1);
            if (hit.front) {
                double le=vs_spectrum(v.material->emission,nm);
                if (le>0) {
                    double p_light=area_pdf(p,ray.direction,hit.normal,
                                            vs_length(vs_sub(v.point,v.previous)),s->light_count);
                    double w=cfg.nee&&!v.previous_delta?mis(v.previous_pdf,p_light):1;
                    record(trace,VS_EVENT_EMISSION,v,state.medium,distance,p_light,w,false);
                    if (!contribute(out,v.beta*le*w,v.delay,v.material->pulse,true,error)) return false;
                }
            }
            if (v.material->kind>=VS_DIELECTRIC &&
                !cross_boundary(&media,p,v.material,hit.front,nm,&next,error)) return false;
            if (v.material->kind==VS_NULL) {
                media=next;
                ray.origin=vs_offset(p,point,hit.normal,ray.direction);
                record(trace,VS_EVENT_BOUNDARY,v,current(&media).medium,distance,1,1,true);
                continue;
            }
            if (v.depth>=cfg.max_depth) break;
            if (v.material->texture.knots) v.beta*=vs_texture_factor(v.material,p,&hit,nm);
            if (v.material->normal_map.rgb) {
                v.shading=vs_shading_normal(v.material,p,&hit,v.n);
                v.beta*=vs_shading_factor(v.n,v.shading,v.wo);
            }
            if (v.beta==0) break;
            if (cfg.nee && !direct(s,v,ray,rng,dim,out,&media,&next,trace,error)) return false;
            scatter=vs_bsdf_sample(v.material,nm,v.n,v.wo,state.phase,current(&next).phase,
                                   vs_sample(rng,dim+3),vs_sample(rng,dim+4),vs_sample(rng,dim+5));
            if (v.material->normal_map.rgb) scatter.weight*=vs_shading_factor(v.n,v.shading,scatter.wi);
            if (scatter.transmit) {
                media=next;
                double r=current(&media).phase/state.phase;
                v.eta_balance*=r*r;
            }
        }
        record(trace,VS_EVENT_SCATTER,v,state.medium,distance,scatter.pdf,scatter.weight,scatter.delta);
        if (!(scatter.pdf>0 && scatter.weight>0)) break;
        v.beta*=scatter.weight;
        if (!isfinite(v.beta)) return vs_fail(error,"nonfinite path throughput");
        if (cfg.roulette && v.depth>=3) {
            double probability=fmin(.95,fmax(.05,v.beta*out->wavelength.pdf*v.eta_balance));
            bool alive=vs_sample(rng,dim+6)<probability;
            record(trace,VS_EVENT_ROULETTE,v,state.medium,0,probability,alive?1/probability:0,true);
            if (!alive) break;
            v.beta/=probability;
        }
        v.previous_delta=scatter.delta;
        v.previous_pdf=scatter.pdf;
        v.previous=point;
        ++v.depth;
        ray.origin=v.volume?point:vs_offset(s->primitives+v.primitive,point,v.n,scatter.wi);
        ray.direction=vs_unit(scatter.wi);
    }
    if (v.iteration>=128) return vs_fail(error,"path exceeded segment budget");
    record(trace,VS_EVENT_END,v,current(&media).medium,0,0,0,false);
    return true;
}
