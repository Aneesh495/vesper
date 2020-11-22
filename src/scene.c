#include "vesper/scene.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool vs_fail(VsError *error, const char *format, ...) {
    if (error) {
        va_list args;
        va_start(args,format);
        vsnprintf(error->message,sizeof error->message,format,args);
        va_end(args);
    }
    return false;
}

static void *reserve(void *old, uint32_t n, uint32_t *cap, uint32_t limit, size_t element) {
    if (n>=limit) return NULL;
    if (n<*cap) return old;
    uint32_t target=*cap?*cap+*cap/2+1:8;
    if (target>limit) target=limit;
    void *next=realloc(old,target*element);
    if (next) *cap=target;
    return next;
}

bool vs_scene_init(VsScene *s, VsError *error) {
    *s=(VsScene){.environment=UINT32_MAX};
    VsMedium vacuum={.name="vacuum"};
    return vs_scene_add_medium(s,&vacuum,error);
}

void vs_scene_destroy(VsScene *s) {
    if (!s) return;
    free(s->primitives);
    free(s->materials);
    free(s->lights);
    for (uint32_t i=0;i<s->medium_count;++i) free(s->media[i].density);
    free(s->media);
    free(s->area_indices);
    vs_bvh_destroy(&s->bvh);
    *s=(VsScene){0};
}

static bool name_valid(const char name[VS_NAME]) {
    if (!name[0]) return false;
    for (unsigned i=0;i<VS_NAME;++i) {
        char c=name[i];
        if (!c) return true;
        if (!(c=='_' || c=='-' || (c>='A' && c<='Z') ||
              (c>='a' && c<='z') || (c>='0' && c<='9'))) return false;
    }
    return false;
}

static bool pulse_valid(VsPulse p) {
    return isfinite(p.start) && fabs(p.start)<=1 && p.width>=0 && p.width<=1;
}

static bool world_vector(VsVec3 p) {
    return fabs(p.x)<=VS_WORLD_LIMIT && fabs(p.y)<=VS_WORLD_LIMIT && fabs(p.z)<=VS_WORLD_LIMIT;
}

bool vs_scene_add_primitive(VsScene *s, VsPrimitive p, VsError *error) {
    if (!vs_primitive_valid(&p) || p.material>=s->material_count)
        return vs_fail(error,"invalid primitive or material index");
    VsBounds bounds=vs_primitive_bounds(&p);
    if (!world_vector(bounds.lo) || !world_vector(bounds.hi))
        return vs_fail(error,"geometry exceeds the metre coordinate limit");
    VsPrimitive *next=reserve(s->primitives,s->primitive_count,&s->primitive_cap,
                              VS_MAX_PRIMITIVES,sizeof p);
    if (!next) return vs_fail(error,"primitive allocation or count limit");
    s->primitives=next;
    s->primitives[s->primitive_count++]=p;
    s->identity=0;
    return true;
}

static bool material_valid(VsMaterial m, uint32_t media) {
    return name_valid(m.name) && (unsigned)m.kind<=VS_ROUGH_DIELECTRIC &&
        vs_spectrum_valid(m.albedo,1) && vs_spectrum_valid(m.emission,1e9) &&
        vs_spectrum_valid(m.eta,100) && vs_spectrum_valid(m.k,100) &&
        m.roughness>=0 && m.roughness<=1 && m.ior_a>=1 && m.ior_a<=4 &&
        m.ior_b>=0 && m.ior_b<=.2 && m.medium<media && pulse_valid(m.pulse) &&
        ((m.kind!=VS_CONDUCTOR && m.kind!=VS_ROUGH_DIELECTRIC) || m.roughness>=.01) &&
        (m.kind!=VS_CONDUCTOR || (m.eta.kind==VS_TABULATED && m.k.kind==VS_TABULATED));
}

bool vs_scene_add_material(VsScene *s, VsMaterial m, VsError *error) {
    if (!material_valid(m,s->medium_count)) return vs_fail(error,"invalid material");
    for (uint32_t i=0;i<s->material_count;++i)
        if (!strcmp(m.name,s->materials[i].name)) return vs_fail(error,"duplicate material %s",m.name);
    VsMaterial *next=reserve(s->materials,s->material_count,&s->material_cap,
                             VS_MAX_MATERIALS,sizeof m);
    if (!next) return vs_fail(error,"material allocation or count limit");
    s->materials=next;
    s->materials[s->material_count++]=m;
    s->identity=0;
    return true;
}

bool vs_scene_add_light(VsScene *s, VsLight l, VsError *error) {
    if ((l.kind!=VS_POINT && l.kind!=VS_DISTANT && l.kind!=VS_ENVIRONMENT) ||
        !world_vector(l.vector) || !vs_spectrum_valid(l.power,1e9) || !pulse_valid(l.pulse) ||
        (l.kind==VS_DISTANT && vs_length(l.vector)<1e-8))
        return vs_fail(error,"invalid light");
    VsLight *next=reserve(s->lights,s->explicit_lights,&s->light_cap,VS_MAX_LIGHTS,sizeof l);
    if (!next) return vs_fail(error,"light allocation or count limit");
    s->lights=next;
    s->lights[s->explicit_lights++]=l;
    s->light_count=s->explicit_lights;
    s->identity=0;
    return true;
}

static double spectrum_max(VsSpectrum s) {
    if (s.kind==VS_GAUSSIAN) return s.values[0];
    double v=0;
    for (unsigned i=0;i<VS_SPECTRAL_KNOTS;++i) v=fmax(v,s.values[i]);
    return v;
}

static size_t grid_count(const VsMedium *m) {
    return (size_t)m->nx*m->ny*m->nz;
}

static bool medium_valid(const VsMedium *m, VsError *error) {
    if (!name_valid(m->name) || !vs_spectrum_valid(m->absorption,1e4) ||
        !vs_spectrum_valid(m->scattering,1e4) ||
        !(fabs(m->anisotropy)<.99) || !(m->majorant>=0 && m->majorant<=1e6))
        return vs_fail(error,"invalid medium");
    double maximum=1;
    if (m->density) {
        if (m->nx<2 || m->ny<2 || m->nz<2 || m->nx>128 || m->ny>128 || m->nz>128 ||
            !world_vector(m->bounds.lo) || !world_vector(m->bounds.hi) ||
            m->bounds.lo.x>=m->bounds.hi.x || m->bounds.lo.y>=m->bounds.hi.y ||
            m->bounds.lo.z>=m->bounds.hi.z)
            return vs_fail(error,"invalid density grid dimensions or bounds");
        maximum=0;
        for (size_t i=0;i<grid_count(m);++i) {
            if (!(m->density[i]>=0 && m->density[i]<=1000)) return vs_fail(error,"invalid grid density");
            maximum=fmax(maximum,m->density[i]);
        }
    } else if (m->nx || m->ny || m->nz) {
        return vs_fail(error,"density grid is missing");
    }
    double bound=maximum*(spectrum_max(m->absorption)+spectrum_max(m->scattering));
    return m->majorant>=bound || vs_fail(error,"medium %s majorant %.9g is below bound %.9g",
                                        m->name,m->majorant,bound);
}

bool vs_scene_add_medium(VsScene *s, const VsMedium *m, VsError *error) {
    if (!medium_valid(m,error)) return false;
    for (uint32_t i=0;i<s->medium_count;++i)
        if (!strcmp(m->name,s->media[i].name)) return vs_fail(error,"duplicate medium %s",m->name);
    VsMedium copy=*m;
    if (m->density) {
        size_t bytes=grid_count(m)*sizeof(float);
        copy.density=malloc(bytes);
        if (!copy.density) return vs_fail(error,"density allocation failed");
        memcpy(copy.density,m->density,bytes);
    }
    VsMedium *next=reserve(s->media,s->medium_count,&s->medium_cap,VS_MAX_MEDIA,sizeof *m);
    if (!next) {
        free(copy.density);
        return vs_fail(error,"medium allocation or count limit");
    }
    s->media=next;
    s->media[s->medium_count++]=copy;
    s->identity=0;
    return true;
}

double vs_phase_index(const VsMaterial *m,double nm) {
    double um=nm*.001;
    return m->ior_a+m->ior_b/(um*um);
}

double vs_group_index(const VsMaterial *m,double nm) {
    double um=nm*.001;
    return m->ior_a+3*m->ior_b/(um*um);
}

double vs_area(const VsPrimitive *p) {
    if (p->shape==VS_SPHERE) return 4*VS_PI*p->radius*p->radius;
    return .5*vs_length(vs_cross(vs_sub(p->b,p->a),vs_sub(p->c,p->a)));
}

static uint64_t word(uint64_t hash, uint64_t value) {
    for (unsigned i=0;i<8;++i) {
        hash=(hash^(value&255u))*UINT64_C(1099511628211);
        value>>=8;
    }
    return hash;
}

static uint64_t real(uint64_t hash, double value) {
    uint64_t bits;
    memcpy(&bits,&value,sizeof bits);
    return word(hash,bits);
}

static uint64_t vec(uint64_t h, VsVec3 v) {
    return real(real(real(h,v.x),v.y),v.z);
}

static uint64_t curve(uint64_t h, VsSpectrum s) {
    h=word(h,s.kind);
    for (unsigned i=0;i<VS_SPECTRAL_KNOTS;++i) h=real(h,s.values[i]);
    return h;
}

static uint64_t scene_identity(const VsScene *s) {
    uint64_t h=UINT64_C(14695981039346656037);
    h=word(word(word(word(h,s->primitive_count),s->material_count),s->explicit_lights),s->medium_count);
    h=real(word(h,s->world_medium),s->response);
    for (uint32_t i=0;i<s->primitive_count;++i) {
        const VsPrimitive *p=s->primitives+i;
        h=word(word(word(h,p->shape),p->boundary),p->material);
        h=real(vec(vec(vec(vec(h,p->a),p->b),p->c),p->velocity),p->radius);
    }
    for (uint32_t i=0;i<s->material_count;++i) {
        const VsMaterial *m=s->materials+i;
        h=word(word(h,m->kind),m->medium);
        h=curve(curve(curve(curve(h,m->albedo),m->emission),m->eta),m->k);
        h=real(real(real(h,m->roughness),m->ior_a),m->ior_b);
        h=real(real(h,m->pulse.start),m->pulse.width);
    }
    for (uint32_t i=0;i<s->explicit_lights;++i) {
        const VsLight *l=s->lights+i;
        h=curve(vec(word(h,l->kind),l->vector),l->power);
        h=real(real(h,l->pulse.start),l->pulse.width);
    }
    for (uint32_t i=0;i<s->medium_count;++i) {
        const VsMedium *m=s->media+i;
        h=real(real(curve(curve(h,m->absorption),m->scattering),m->majorant),m->anisotropy);
        h=vec(vec(word(word(word(h,m->nx),m->ny),m->nz),m->bounds.lo),m->bounds.hi);
        for (size_t j=0;j<grid_count(m);++j) h=real(h,m->density[j]);
    }
    const VsCamera *c=s->cameras+s->camera;
    h=vec(vec(vec(h,c->position),c->target),c->up);
    h=real(real(real(h,c->vfov),c->aperture),c->focus);
    return real(real(h,c->shutter_open),c->shutter_close);
}

bool vs_scene_compile(VsScene *s, VsError *error) {
    s->identity=0;
    if (!s->camera_count || s->camera_count>VS_MAX_CAMERAS || s->camera>=s->camera_count ||
        s->world_medium>=s->medium_count || !(s->response>=0 && s->response<=1))
        return vs_fail(error,"invalid active camera, world medium or temporal response");
    for (uint32_t i=0;i<s->camera_count;++i) {
        const VsCamera *c=s->cameras+i;
        if (!name_valid(c->name) || !world_vector(c->position) || !world_vector(c->target) ||
            !world_vector(c->up) ||
            vs_length(vs_cross(vs_sub(c->target,c->position),c->up))<1e-10 ||
            !(c->vfov>0 && c->vfov<179 && c->aperture>=0 && c->aperture<=100 &&
              c->focus>0 && c->focus<=VS_WORLD_LIMIT && c->shutter_open>=0 &&
              c->shutter_close>=c->shutter_open && c->shutter_close<=1))
            return vs_fail(error,"invalid camera");
        for (uint32_t j=0;j<i;++j)
            if (!strcmp(c->name,s->cameras[j].name)) return vs_fail(error,"duplicate camera %s",c->name);
    }
    for (uint32_t i=0;i<s->material_count;++i)
        if (!material_valid(s->materials[i],s->medium_count)) return vs_fail(error,"invalid compiled material");
    for (uint32_t i=0;i<s->medium_count;++i)
        if (!medium_valid(s->media+i,error)) return false;
    uint32_t *area=s->primitive_count?malloc(s->primitive_count*sizeof *area):NULL;
    if (s->primitive_count && !area) return vs_fail(error,"light lookup allocation failed");
    s->environment=UINT32_MAX;
    for (uint32_t i=0;i<s->explicit_lights;++i) {
        if (s->lights[i].kind!=VS_ENVIRONMENT) continue;
        if (s->environment!=UINT32_MAX) {
            free(area);
            return vs_fail(error,"only one environment is supported");
        }
        s->environment=i;
    }
    s->light_count=s->explicit_lights;
    for (uint32_t i=0;i<s->primitive_count;++i) {
        uint32_t material=s->primitives[i].material;
        if (material>=s->material_count) {
            free(area);
            return vs_fail(error,"primitive references missing material");
        }
        const VsMaterial *m=s->materials+material;
        area[i]=UINT32_MAX;
        if (spectrum_max(m->emission)==0) continue;
        VsLight l={.kind=VS_AREA,.power=m->emission,.pulse=m->pulse,.primitive=i};
        VsLight *next=reserve(s->lights,s->light_count,&s->light_cap,VS_MAX_LIGHTS,sizeof l);
        if (!next) {
            free(area);
            return vs_fail(error,"emissive geometry light allocation or count limit");
        }
        s->lights=next;
        area[i]=s->light_count;
        s->lights[s->light_count++]=l;
    }
    if (!vs_bvh_build(&s->bvh,s->primitives,s->primitive_count) ||
        !vs_bvh_validate(&s->bvh,s->primitives)) {
        free(area);
        return vs_fail(error,"BVH build or validation failed");
    }
    free(s->area_indices);
    s->area_indices=area;
    s->identity=scene_identity(s);
    return true;
}

VsRay vs_camera_ray(const VsCamera *c, uint32_t width, uint32_t height, VsSample sample) {
    VsVec3 forward=vs_unit(vs_sub(c->target,c->position));
    VsVec3 right=vs_unit(vs_cross(forward,c->up)), up=vs_cross(right,forward);
    double tan_half=tan(c->vfov*VS_PI/360);
    double x=(2*((sample.pixel%width+vs_sample(sample,0))/width)-1)*tan_half*width/height;
    double y=(1-2*((sample.pixel/width+vs_sample(sample,1))/height))*tan_half;
    VsVec3 axis=vs_add(forward,vs_add(vs_mul(right,x),vs_mul(up,y)));
    double r=c->aperture*sqrt(vs_sample(sample,2)), phi=2*VS_PI*vs_sample(sample,3);
    VsVec3 lens=vs_add(vs_mul(right,r*cos(phi)),vs_mul(up,r*sin(phi)));
    VsVec3 d=vs_unit(vs_sub(vs_mul(axis,c->focus),lens));
    double time=c->shutter_open+(c->shutter_close-c->shutter_open)*vs_sample(sample,4);
    return (VsRay){vs_add(c->position,lens),d,0,INFINITY,time};
}
