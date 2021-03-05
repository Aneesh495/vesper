#define _POSIX_C_SOURCE 200809L
#include "vesper/gpu.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    VsRender cfg;
    VsGate gate;
    VsRegion region;
    uint32_t spp,pixel,sample,bin;
    double exposure,adaptive_error;
    bool progress;
    const char *prefix,*checkpoint,*resume;
} Options;

static volatile sig_atomic_t stop;
static void interrupt(int sig) { (void)sig; stop=1; }

static void usage(void) {
    fputs("usage: vesper render SCENE [options]\n"
          "       vesper trace SCENE [options]\n"
          "       vesper gpu-probe SCENE [options]\n"
          "       vesper inspect SCENE\n"
          "  --width N --height N --spp N --seed N --depth N\n"
          "  --adaptive-rel X (independent CPU pilot, spp is the main-film cap)\n"
          "  --sampler random|stratified|halton --wavelength uniform|mixture\n"
          "  --bins N --start-ns X --step-ns X --bin N --exposure X\n"
          "  --prefix PATH --checkpoint PATH --resume PATH --pixel N --sample N\n"
          "  --backend cpu|vulkan --region X Y W H --progress --no-nee --no-roulette\n",stderr);
}

static bool uint_arg(const char *s,uint32_t *out) {
    char *end;
    errno=0;
    unsigned long n=strtoul(s,&end,10);
    if (!*s || *end || errno || *s=='-' || n>UINT32_MAX) return false;
    *out=(uint32_t)n;
    return true;
}

static bool real_arg(const char *s,double *out) {
    char *end;
    errno=0;
    double n=strtod(s,&end);
    if (!*s || *end || errno || !isfinite(n)) return false;
    *out=n;
    return true;
}

static bool arguments(Options *o,int argc,char **argv,VsError *error) {
    double ns_start=0,ns_step=.25;
    *o=(Options){.cfg={320,200,1789,8,VS_RANDOM,VS_WAVELENGTH_MIXTURE,VS_BACKEND_CPU,true,true},
                 .spp=32,.prefix="vesper",.bin=UINT32_MAX,.pixel=UINT32_MAX};
    for (int i=3;i<argc;++i) {
        const char *key=argv[i];
        if (!strcmp(key,"--progress")) { o->progress=true; continue; }
        if (!strcmp(key,"--no-nee")) { o->cfg.nee=false; continue; }
        if (!strcmp(key,"--no-roulette")) { o->cfg.roulette=false; continue; }
        if (i+1>=argc) return vs_fail(error,"missing value for %s",key);
        const char *value=argv[++i];
        uint32_t *number=NULL;
        double *real=NULL;
        if (!strcmp(key,"--width")) number=&o->cfg.width;
        else if (!strcmp(key,"--height")) number=&o->cfg.height;
        else if (!strcmp(key,"--depth")) number=&o->cfg.max_depth;
        else if (!strcmp(key,"--seed")) number=&o->cfg.seed;
        else if (!strcmp(key,"--spp")) number=&o->spp;
        else if (!strcmp(key,"--bins")) number=&o->gate.bins;
        else if (!strcmp(key,"--pixel")) number=&o->pixel;
        else if (!strcmp(key,"--sample")) number=&o->sample;
        else if (!strcmp(key,"--bin")) number=&o->bin;
        else if (!strcmp(key,"--start-ns")) real=&ns_start;
        else if (!strcmp(key,"--step-ns")) real=&ns_step;
        else if (!strcmp(key,"--exposure")) real=&o->exposure;
        else if (!strcmp(key,"--adaptive-rel")) real=&o->adaptive_error;
        else if (!strcmp(key,"--prefix")) o->prefix=value;
        else if (!strcmp(key,"--checkpoint")) o->checkpoint=value;
        else if (!strcmp(key,"--resume")) o->resume=value;
        else if (!strcmp(key,"--backend")) {
            if (!strcmp(value,"cpu")) o->cfg.backend=VS_BACKEND_CPU;
            else if (!strcmp(value,"vulkan")) o->cfg.backend=VS_BACKEND_VULKAN;
            else return vs_fail(error,"invalid backend %s",value);
        } else if (!strcmp(key,"--sampler")) {
            if (!strcmp(value,"random")) o->cfg.sampler=VS_RANDOM;
            else if (!strcmp(value,"stratified")) o->cfg.sampler=VS_STRATIFIED;
            else if (!strcmp(value,"halton")) o->cfg.sampler=VS_HALTON;
            else return vs_fail(error,"invalid sampler %s",value);
        } else if (!strcmp(key,"--wavelength")) {
            if (!strcmp(value,"uniform")) o->cfg.wavelength=VS_WAVELENGTH_UNIFORM;
            else if (!strcmp(value,"mixture")) o->cfg.wavelength=VS_WAVELENGTH_MIXTURE;
            else return vs_fail(error,"invalid wavelength sampler %s",value);
        } else if (!strcmp(key,"--region")) {
            if (i+3>=argc || !uint_arg(value,&o->region.x) || !uint_arg(argv[i+1],&o->region.y) ||
                !uint_arg(argv[i+2],&o->region.width) || !uint_arg(argv[i+3],&o->region.height) ||
                !o->region.width || !o->region.height) return vs_fail(error,"invalid region");
            i+=3;
        } else return vs_fail(error,"unknown argument %s",key);
        if ((number && !uint_arg(value,number)) || (real && !real_arg(value,real)))
            return vs_fail(error,"invalid value %s for %s",value,key);
    }
    if (!vs_render_valid(o->cfg,error) || o->spp<1 || o->spp>VS_MAX_SPP ||
        (o->bin!=UINT32_MAX && o->bin>=o->gate.bins) || fabs(o->exposure)>40)
        return vs_fail(error,"invalid render options");
    if (o->adaptive_error!=0 && (!(o->adaptive_error>=.001 && o->adaptive_error<=1) ||
        o->cfg.sampler!=VS_RANDOM || o->spp<VS_ADAPTIVE_PILOT_SPP || !strcmp(argv[1],"trace")))
        return vs_fail(error,"adaptive sampling requires a render, random sampling, at least 16 spp and relative error in [.001,1]");
    if (o->gate.bins) {
        o->gate.start=ns_start*1e-9;
        o->gate.width=ns_step*1e-9;
    }
    if (o->pixel==UINT32_MAX) o->pixel=o->cfg.width*(o->cfg.height/2)+o->cfg.width/2;
    if (o->pixel>=o->cfg.width*o->cfg.height) return vs_fail(error,"pixel index outside image");
    if (!o->region.width) o->region=(VsRegion){0,0,o->cfg.width,o->cfg.height};
    return true;
}

static bool trace(const VsScene *s,const Options *o,VsError *error) {
    VsPath p;
    VsTrace tr;
    if (!vs_trace_path(s,o->cfg,o->pixel,o->sample,&p,&tr,error)) return false;
    printf("kind,depth,medium,primitive,distance_m,delay_s,pdf,weight,throughput,delta\n");
    for (uint32_t i=0;i<tr.count;++i) {
        const VsTraceEvent *e=tr.events+i;
        printf("%u,%u,%u,%u,%.17g,%.17g,%.17g,%.17g,%.17g,%u\n",
            e->kind,e->depth,e->medium,e->primitive,e->distance,e->delay,e->pdf,
            e->weight,e->throughput,e->delta);
    }
    fprintf(stderr,"wavelength_nm=%.17g pdf_per_nm=%.17g rays=%u shadows=%u nulls=%u dropped=%u\n",
        p.wavelength.nm,p.wavelength.pdf,p.rays,p.shadow_rays,p.nulls,tr.dropped);
    for (uint32_t i=0;i<p.count;++i)
        fprintf(stderr,"contribution[%u] value=%.17g delay_s=%.17g pulse_start_s=%.17g pulse_width_s=%.17g timed=%u\n",
            i,p.events[i].value,p.events[i].delay,p.events[i].pulse.start,p.events[i].pulse.width,p.events[i].timed);
    return !ferror(stdout) || vs_fail(error,"trace output failed");
}

static bool output_path(char dest[4096],const char *prefix,const char *ext,VsError *error) {
    int n=snprintf(dest,4096,"%s.%s",prefix,ext);
    return (n>0 && n<4096) || vs_fail(error,"output path length limit");
}

static double monotonic(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec*1e-9;
}

static bool render(const VsScene *s,const Options *o,bool probe,VsError *error) {
    VsFilm f={0};
    VsGpu *gpu=NULL;
    uint64_t device=0;
    if (o->cfg.backend==VS_BACKEND_VULKAN) {
        if (!vs_gpu_open(&gpu,s,o->cfg,error)) return false;
        const VsDevice *d=vs_gpu_device(gpu);
        device=d->identity;
        fprintf(stderr,"Vulkan device=%s vendor=%04x device_id=%08x api=%u driver=%s float64=%u validation=%u identity=%016llx\n",
                d->name,d->vendor_id,d->device_id,d->api_version,d->driver,d->float64,d->validation,
                (unsigned long long)device);
    }
    bool ok=o->resume?vs_checkpoint_read(&f,o->resume,s,o->cfg,o->gate,device,error):
        vs_film_init(&f,s,o->cfg,o->gate,device,error);
    if (!ok) { vs_gpu_close(gpu); return false; }
    VsRegion r=o->region;
    VsStats stats={0},pilot={0};
    double t0=monotonic();
    if (!vs_region_valid(&f,r)) ok=vs_fail(error,"render region outside image");
    uint32_t columns=gpu?f.render.width:16;
    uint32_t rows=gpu?8:16;
    uint32_t *targets=o->adaptive_error?malloc((size_t)columns*rows*sizeof *targets):NULL;
    if (o->adaptive_error && !targets) ok=vs_fail(error,"adaptive target allocation failed");
    for (uint32_t y=r.y;ok && !stop && y<r.y+r.height;y+=rows) {
        for (uint32_t x=r.x;ok && !stop && x<r.x+r.width;x+=columns) {
            VsRegion tile={x,y,(uint32_t)fmin(columns,r.x+r.width-x),(uint32_t)fmin(rows,r.y+r.height-y)};
            if (targets && !vs_adaptive_budget(s,o->cfg,tile,o->spp,o->adaptive_error,targets,&pilot,error)) {
                ok=false;
                break;
            }
            VsSampleBudget budget={tile,o->spp,targets};
            ok=gpu?vs_gpu_render_budget(gpu,s,&f,budget,&stats,error):
                vs_render_cpu_budget(s,&f,budget,&stats,error);
        }
        if (ok && !stop && o->progress) {
            fprintf(stderr,"progress rows=%u/%u samples=%llu\n",
                (uint32_t)fmin(y-r.y+rows,r.height),r.height,(unsigned long long)stats.samples);
            fflush(stderr);
        }
    }
    free(targets);
    double dt=monotonic()-t0;
    if (ok && o->checkpoint) ok=vs_checkpoint_write(&f,o->checkpoint,error);
    char path[4096];
    if (ok && !stop && !probe) {
        ok=output_path(path,o->prefix,"exr",error) && vs_export_exr(&f,path,error) &&
           output_path(path,o->prefix,"ppm",error) && vs_export_ppm(&f,path,o->exposure,o->bin,error);
        if (ok && f.gate.bins) ok=output_path(path,o->prefix,"time.csv",error) &&
            vs_export_time_csv(&f,path,o->pixel,error);
    }
    const VsPixel *picked=f.pixels+o->pixel;
    if (ok) printf("{\"scene\":\"%016llx\",\"backend\":\"%s\",\"samples\":%llu,\"rays\":%llu,"
        "\"shadow_rays\":%llu,\"nulls\":%llu,\"seconds\":%.9g,\"pixel\":%u,\"pixel_spp\":%u,"
        "\"pixel_y\":%.17g,\"pixel_variance_y\":%.17g,\"interrupted\":%s,"
        "\"adaptive_rel\":%.17g,\"pilot_samples\":%llu,\"pilot_segments\":%llu}\n",
        (unsigned long long)s->identity,gpu?"vulkan":"cpu",(unsigned long long)stats.samples,(unsigned long long)stats.rays,
        (unsigned long long)stats.shadows,(unsigned long long)stats.nulls,dt,o->pixel,picked->samples,
        picked->mean.y,vs_film_variance(picked),stop?"true":"false",o->adaptive_error,
        (unsigned long long)pilot.samples,(unsigned long long)(pilot.rays+pilot.shadows));
    vs_film_destroy(&f);
    vs_gpu_close(gpu);
    return ok;
}

static bool inspect(const VsScene *s) {
    printf("{\"scene\":\"%016llx\",\"primitives\":%u,\"nodes\":%u,\"lights\":%u,\"media\":[",
           (unsigned long long)s->identity,s->primitive_count,s->bvh.node_count,s->light_count);
    for (uint32_t i=0;i<s->medium_count;++i) {
        const VsMedium *m=s->media+i;
        float maximum=m->density?0:1;
        size_t count=(size_t)m->nx*m->ny*m->nz;
        for (size_t j=0;j<count;++j) maximum=fmaxf(maximum,m->density[j]);
        printf("%s{\"name\":\"%s\",\"dimensions\":[%u,%u,%u],\"density_max\":%.17g,"
               "\"majorant\":%.17g,\"absorption_550\":%.17g,\"scattering_550\":%.17g}",
               i?",":"",m->name,m->nx,m->ny,m->nz,(double)maximum,m->majorant,
               vs_spectrum(m->absorption,550),vs_spectrum(m->scattering,550));
    }
    puts("]}");
    return !ferror(stdout);
}

int main(int argc,char **argv) {
    if (argc==2 && !strcmp(argv[1],"--help")) { usage(); return 0; }
    if (argc<3 || (strcmp(argv[1],"render") && strcmp(argv[1],"trace") && strcmp(argv[1],"gpu-probe") &&
        strcmp(argv[1],"inspect"))) { usage(); return 2; }
    VsError error={0};
    Options o;
    VsScene s={0};
    bool inspection=!strcmp(argv[1],"inspect");
    if (inspection && argc!=3) { usage(); return 2; }
    if ((!inspection && !arguments(&o,argc,argv,&error)) || !vs_scene_load(&s,argv[2],&error)) {
        fprintf(stderr,"vesper: %s\n",error.message);
        return 2;
    }
    struct sigaction action={.sa_handler=interrupt};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL);
    sigaction(SIGTERM,&action,NULL);
    bool probe=!strcmp(argv[1],"gpu-probe");
    if (probe) o.cfg.backend=VS_BACKEND_VULKAN;
    bool ok=inspection?inspect(&s):!strcmp(argv[1],"trace")?trace(&s,&o,&error):render(&s,&o,probe,&error);
    vs_scene_destroy(&s);
    if (!ok) fprintf(stderr,"vesper: %s\n",error.message);
    return !ok?1:stop?130:0;
}
