#include "assets.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *file;
    char *token[24];
    unsigned count,line;
    VsScene *scene;
    VsError detail;
} Parser;

static bool numbers(Parser *p, unsigned first, unsigned n, double *out) {
    if (p->count!=first+n) return vs_fail(&p->detail,"expected %u arguments",first+n-1);
    for (unsigned i=0;i<n;++i) {
        char *end;
        errno=0;
        const char *s=p->token[first+i];
        out[i]=strtod(s,&end);
        if (errno || s==end || *end || !isfinite(out[i]))
            return vs_fail(&p->detail,"invalid finite number %s",s);
    }
    return true;
}

static VsVec3 triple(const double *v) { return vs_v(v[0],v[1],v[2]); }

static bool name(Parser *p, char dest[VS_NAME]) {
    if (p->count<2 || strlen(p->token[1])>=VS_NAME)
        return vs_fail(&p->detail,"missing or oversized name");
    strcpy(dest,p->token[1]);
    return true;
}

static uint32_t lookup(Parser *p, const char *s, bool media) {
    uint32_t count=media?p->scene->medium_count:p->scene->material_count;
    for (uint32_t i=0;i<count;++i)
        if (!strcmp(s,media?p->scene->media[i].name:p->scene->materials[i].name)) return i;
    vs_fail(&p->detail,"unknown %s %s",media?"medium":"material",s);
    return UINT32_MAX;
}

static bool camera(Parser *p) {
    VsCamera c={0};
    double a[14];
    if (!name(p,c.name) || !numbers(p,2,14,a)) return false;
    if (p->scene->camera_count>=VS_MAX_CAMERAS) return vs_fail(&p->detail,"camera count limit");
    c.position=triple(a); c.target=triple(a+3); c.up=triple(a+6);
    c.vfov=a[9]; c.aperture=a[10]; c.focus=a[11];
    c.shutter_open=a[12]; c.shutter_close=a[13];
    p->scene->cameras[p->scene->camera_count++]=c;
    return true;
}

static bool add_material(Parser *p, VsMaterialKind kind) {
    VsMaterial m={.kind=kind,.ior_a=1};
    if (!name(p,m.name)) return false;
    double a[3];
    if (kind==VS_DIFFUSE || kind==VS_CONDUCTOR) {
        if (!numbers(p,2,3,a)) return false;
        if (kind==VS_DIFFUSE) {
            for (unsigned i=0;i<3;++i)
                if (a[i]<0 || a[i]>1) return vs_fail(&p->detail,"RGB model input must be in [0,1]");
            m.albedo=vs_model_rgb(triple(a));
        } else {
            m.roughness=a[0]; m.eta=vs_flat(a[1]); m.k=vs_flat(a[2]);
        }
    } else {
        if (p->count<3) return vs_fail(&p->detail,"expected medium name");
        m.medium=lookup(p,p->token[2],true);
        if (m.medium==UINT32_MAX || !numbers(p,3,kind==VS_NULL?0:kind==VS_DIELECTRIC?2:3,a))
            return false;
        if (kind!=VS_NULL) { m.ior_a=a[0]; m.ior_b=a[1]; }
        if (kind==VS_ROUGH_DIELECTRIC) m.roughness=a[2];
    }
    return vs_scene_add_material(p->scene,m,&p->detail);
}

static bool modify_material(Parser *p) {
    if (p->count<2) return vs_fail(&p->detail,"expected material name");
    uint32_t id=lookup(p,p->token[1],false);
    if (id==UINT32_MAX) return false;
    VsMaterial *m=p->scene->materials+id;
    const char *op=p->token[0];
    double a[VS_SPECTRAL_KNOTS];
    if (!strcmp(op,"reflectance") || !strcmp(op,"eta") || !strcmp(op,"k")) {
        if (!numbers(p,2,VS_SPECTRAL_KNOTS,a)) return false;
        VsSpectrum s={0};
        memcpy(s.values,a,sizeof a);
        VsSpectrum *target=*op=='r'?&m->albedo:*op=='e'?&m->eta:&m->k;
        if (!vs_spectrum_valid(s,*op=='r'?1:100)) return vs_fail(&p->detail,"spectral knot range");
        *target=s;
        return true;
    }
    bool line=!strcmp(op,"emission_line");
    if (!numbers(p,2,line?5:3,a)) return false;
    m->emission=line?vs_gaussian(a[0],a[1],a[2]):vs_flat(a[0]);
    unsigned t=line?3:1;
    m->pulse=(VsPulse){a[t]*1e-9,a[t+1]*1e-9};
    return true;
}

static bool fog(Parser *p) {
    VsMedium m={0};
    double a[4];
    if (!name(p,m.name) || !numbers(p,2,4,a)) return false;
    m.absorption=vs_flat(a[0]); m.scattering=vs_flat(a[1]);
    m.anisotropy=a[2]; m.majorant=a[3];
    return vs_scene_add_medium(p->scene,&m,&p->detail);
}

static bool shape(Parser *p, VsShape kind) {
    if (p->count<2) return vs_fail(&p->detail,"expected material name");
    uint32_t id=lookup(p,p->token[1],false);
    if (id==UINT32_MAX) return false;
    double a[13];
    unsigned n=kind==VS_SPHERE?8:13;
    if (!numbers(p,2,n,a)) return false;
    if (!(a[n-1]>=0 && a[n-1]<=UINT32_MAX && floor(a[n-1])==a[n-1]))
        return vs_fail(&p->detail,"boundary ID must be uint32");
    VsPrimitive q={.material=id,.shape=kind,.a=triple(a),.boundary=(uint32_t)a[n-1]};
    if (kind==VS_SPHERE) { q.radius=a[3]; q.velocity=triple(a+4); }
    else { q.b=triple(a+3); q.c=triple(a+6); q.velocity=triple(a+9); }
    if (p->scene->materials[id].kind>=VS_DIELECTRIC && !q.boundary)
        return vs_fail(&p->detail,"transmitting shapes require a nonzero boundary ID");
    return vs_scene_add_primitive(p->scene,q,&p->detail);
}

static bool light(Parser *p, VsLightKind kind, bool line) {
    double a[8];
    unsigned k=kind==VS_ENVIRONMENT?0:3;
    unsigned n=kind==VS_POINT?(line?5:3):1;
    if (!numbers(p,1,k+n,a)) return false;
    VsLight l={.kind=kind,.vector=k?triple(a):vs_v(0,0,0),
        .power=line?vs_gaussian(a[k],a[k+1],a[k+2]):vs_flat(a[k])};
    if (kind==VS_POINT) l.pulse=(VsPulse){a[k+n-2]*1e-9,a[k+n-1]*1e-9};
    return vs_scene_add_light(p->scene,l,&p->detail);
}

static bool grid(Parser *p) {
    VsMedium m={0};
    double a[13];
    char path[4096];
    if (!name(p,m.name) || !numbers(p,3,13,a) ||
        !vs_asset_path(path,p->file,p->token[2],&p->detail)) return false;
    for (unsigned i=4;i<7;++i)
        if (a[i]<2 || a[i]>128 || floor(a[i])!=a[i]) return vs_fail(&p->detail,"grid dimension range");
    m.absorption=vs_flat(a[0]); m.scattering=vs_flat(a[1]);
    m.anisotropy=a[2]; m.majorant=a[3];
    m.nx=(uint32_t)a[4]; m.ny=(uint32_t)a[5]; m.nz=(uint32_t)a[6];
    m.bounds=(VsBounds){triple(a+7),triple(a+10)};
    return vs_load_grid(p->scene,m,path,&p->detail);
}

static bool mesh(Parser *p) {
    char path[4096];
    double a[10];
    if (!numbers(p,3,10,a) || !vs_asset_path(path,p->file,p->token[2],&p->detail)) return false;
    uint32_t id=lookup(p,p->token[1],false);
    if (id==UINT32_MAX) return false;
    if (a[0]<=0 || a[1]<=0 || a[2]<=0 || a[9]<0 || a[9]>UINT32_MAX || floor(a[9])!=a[9])
        return vs_fail(&p->detail,"invalid mesh scale or boundary ID");
    VsPrimitive prim={.shape=VS_TRIANGLE,.material=id,.velocity=triple(a+6),.boundary=(uint32_t)a[9]};
    if (p->scene->materials[id].kind>=VS_DIELECTRIC && !prim.boundary)
        return vs_fail(&p->detail,"transmitting mesh requires a nonzero boundary ID");
    if (!strcmp(p->token[0],"gltf"))
        return vs_load_gltf(p->scene,prim,triple(a),triple(a+3),path,&p->detail);
    return vs_load_obj(p->scene,prim,triple(a),triple(a+3),path,&p->detail);
}

static bool texture(Parser *p) {
    char path[4096];
    double a[4];
    if (!numbers(p,4,4,a) || !vs_asset_path(path,p->file,p->token[2],&p->detail)) return false;
    bool srgb=!strcmp(p->token[3],"srgb");
    if (!srgb && strcmp(p->token[3],"linear")) return vs_fail(&p->detail,"texture color space must be srgb or linear");
    uint32_t id=lookup(p,p->token[1],false);
    return id!=UINT32_MAX && vs_load_texture(p->scene,id,path,srgb,(VsVec2){a[0],a[1]},
                                              (VsVec2){a[2],a[3]},&p->detail);
}

static bool normal_map(Parser *p) {
    char path[4096];
    double a[5];
    if (!numbers(p,3,5,a) || !vs_asset_path(path,p->file,p->token[2],&p->detail)) return false;
    uint32_t id=lookup(p,p->token[1],false);
    return id!=UINT32_MAX && vs_load_normal_map(p->scene,id,path,(VsVec2){a[0],a[1]},
                                                (VsVec2){a[2],a[3]},a[4],&p->detail);
}

static bool command(Parser *p) {
    const char *op=p->token[0];
    if (!strcmp(op,"camera")) return camera(p);
    if (!strcmp(op,"use")) {
        if (p->count!=2) return vs_fail(&p->detail,"expected camera name");
        for (uint32_t i=0;i<p->scene->camera_count;++i) {
            if (strcmp(p->token[1],p->scene->cameras[i].name)) continue;
            p->scene->camera=i;
            return true;
        }
        return vs_fail(&p->detail,"unknown camera %s",p->token[1]);
    }
    if (!strcmp(op,"diffuse")) return add_material(p,VS_DIFFUSE);
    if (!strcmp(op,"conductor")) return add_material(p,VS_CONDUCTOR);
    if (!strcmp(op,"glass")) return add_material(p,VS_DIELECTRIC);
    if (!strcmp(op,"rough_glass")) return add_material(p,VS_ROUGH_DIELECTRIC);
    if (!strcmp(op,"boundary")) return add_material(p,VS_NULL);
    if (!strcmp(op,"reflectance") || !strcmp(op,"eta") || !strcmp(op,"k") ||
        !strcmp(op,"emission") || !strcmp(op,"emission_line")) return modify_material(p);
    if (!strcmp(op,"sphere")) return shape(p,VS_SPHERE);
    if (!strcmp(op,"triangle")) return shape(p,VS_TRIANGLE);
    if (!strcmp(op,"point")) return light(p,VS_POINT,false);
    if (!strcmp(op,"point_line")) return light(p,VS_POINT,true);
    if (!strcmp(op,"distant")) return light(p,VS_DISTANT,false);
    if (!strcmp(op,"environment")) return light(p,VS_ENVIRONMENT,false);
    if (!strcmp(op,"medium")) return fog(p);
    if (!strcmp(op,"grid")) return grid(p);
    if (!strcmp(op,"texture")) return texture(p);
    if (!strcmp(op,"normal_map")) return normal_map(p);
    if (!strcmp(op,"obj") || !strcmp(op,"gltf")) return mesh(p);
    if (!strcmp(op,"world")) {
        if (p->count!=2) return vs_fail(&p->detail,"expected medium name");
        uint32_t id=lookup(p,p->token[1],true);
        if (id==UINT32_MAX) return false;
        p->scene->world_medium=id;
        return true;
    }
    if (!strcmp(op,"response")) {
        double ns;
        if (!numbers(p,1,1,&ns)) return false;
        p->scene->response=ns*1e-9;
        return true;
    }
    return vs_fail(&p->detail,"unknown command %s",op);
}

static bool tokenize(Parser *p, char *line) {
    char *comment=strchr(line,'#');
    if (comment) *comment='\0';
    p->count=0;
    const char *space=" \t\r\n";
    char *t=line+strspn(line,space);
    while (*t) {
        if (p->count==sizeof p->token/sizeof *p->token)
            return vs_fail(&p->detail,"too many tokens");
        p->token[p->count++]=t;
        t+=strcspn(t,space);
        if (*t) {
            *t++='\0';
            t+=strspn(t,space);
        }
    }
    return true;
}

bool vs_scene_load(VsScene *s, const char *path, VsError *error) {
    FILE *file=fopen(path,"rb");
    if (!file) return vs_fail(error,"could not open %s",path);
    VsScene next={0};
    Parser p={.scene=&next,.file=path};
    bool ok=vs_scene_init(&next,&p.detail),header=false;
    char line[4096];
    while (ok && fgets(line,sizeof line,file)) {
        ++p.line;
        if ((!strchr(line,'\n') && !feof(file)) || p.line>VS_MAX_PRIMITIVES+100000u) {
            ok=vs_fail(&p.detail,"line length or scene line count limit");
            break;
        }
        ok=tokenize(&p,line);
        if (!ok || !p.count) continue;
        if (!header) {
            header=p.count==3 && !strcmp(p.token[0],"vesper") &&
                !strcmp(p.token[1],"1") && !strcmp(p.token[2],"metres");
            if (!header) ok=vs_fail(&p.detail,"expected header 'vesper 1 metres'");
        } else ok=command(&p);
    }
    if (ok && (ferror(file) || !header)) ok=vs_fail(&p.detail,"read failure or missing header");
    fclose(file);
    if (ok) ok=vs_scene_compile(&next,&p.detail);
    if (!ok) {
        vs_scene_destroy(&next);
        return vs_fail(error,"%s:%u: %s",path,p.line,p.detail.message);
    }
    vs_scene_destroy(s);
    *s=next;
    return true;
}
