#define _POSIX_C_SOURCE 200809L
#include "vesper/gpu.h"
#include "check.h"
#include <time.h>

static double now(void) {
    struct timespec t;
    REQUIRE(clock_gettime(CLOCK_MONOTONIC,&t)==0);
    return t.tv_sec+t.tv_nsec*1e-9;
}

static void grid(VsScene *s,VsError *error) {
    const uint32_t nx=1000,ny=500;
    VsMaterial surface={.name="screen",.kind=VS_DIFFUSE,.albedo=vs_flat(.65),.ior_a=1};
    REQUIRE(vs_scene_init(s,error));
    REQUIRE(vs_scene_add_material(s,surface,error));
    REQUIRE(vs_scene_add_light(s,(VsLight){.kind=VS_DISTANT,.vector={1,1,2},.power=vs_flat(.8)},error));
    s->camera_count=1;
    s->cameras[0]=(VsCamera){.name="overhead",.position={0,0,7},.target={0,0,0},.up={0,1,0},
        .vfov=30,.focus=7};
    for (uint32_t y=0;y<ny;++y) for (uint32_t x=0;x<nx;++x) {
        double l=(double)x*.01-5, r=(double)(x+1)*.01-5;
        double b=(double)y*.01-2.5,t=(double)(y+1)*.01-2.5;
        VsVec3 a=vs_v(l,b,0), d=vs_v(r,t,0);
        REQUIRE(vs_scene_add_primitive(s,(VsPrimitive){.a=a,.b={r,b,0},.c=d,.shape=VS_TRIANGLE},error));
        REQUIRE(vs_scene_add_primitive(s,(VsPrimitive){.a=a,.b=d,.c={l,t,0},.shape=VS_TRIANGLE},error));
    }
    REQUIRE(vs_scene_compile(s,error));
    REQUIRE(s->primitive_count==1000000 && s->bvh.count==1000000);
}

int main(int argc,char **argv) {
    const uint32_t spp=argc==2?(uint32_t)strtoul(argv[1],NULL,10):16;
    REQUIRE(spp>0 && spp<=128);
    VsScene scene={0};
    VsError error={0};
    double build=now();
    grid(&scene,&error);
    build=now()-build;
    VsRender cfg={512,256,47,1,VS_RANDOM,VS_WAVELENGTH_UNIFORM,VS_BACKEND_VULKAN,true,false};
    VsGpu *gpu=NULL;
    double upload=now();
    if (!vs_gpu_open(&gpu,&scene,cfg,&error)) {
        fprintf(stderr,"%s\n",error.message);
        vs_scene_destroy(&scene);
        return 1;
    }
    upload=now()-upload;
    VsFilm f={0};
    const VsDevice *device=vs_gpu_device(gpu);
    REQUIRE(vs_film_init(&f,&scene,cfg,(VsGate){0},device->identity,&error));
    VsRegion full={0,0,cfg.width,cfg.height};
    VsStats warm={0},stats={0};
    REQUIRE(vs_gpu_render(gpu,&scene,&f,full,2,&warm,&error));
    vs_film_clear(&f);
    double seconds=now();
    if (!vs_gpu_render(gpu,&scene,&f,full,spp,&stats,&error)) {
        fprintf(stderr,"%s\n",error.message);
        return 1;
    }
    seconds=now()-seconds;
    REQUIRE(stats.samples==(uint64_t)cfg.width*cfg.height*spp && stats.rays>stats.samples && stats.shadows>0);
    printf("{\"backend\":\"vulkan\",\"triangles\":%u,\"nodes\":%u,\"width\":%u,\"height\":%u,"
        "\"spp\":%u,\"samples\":%llu,\"path_segments\":%llu,\"shadow_segments\":%llu,"
        "\"seconds\":%.9g,\"segments_per_s\":%.9g,\"warmup_samples\":%llu,"
        "\"bvh_build_seconds\":%.9g,\"device_setup_seconds\":%.9g,\"device_id\":\"%016llx\"}\n",
        scene.primitive_count,scene.bvh.node_count,cfg.width,cfg.height,spp,(unsigned long long)stats.samples,
        (unsigned long long)stats.rays,(unsigned long long)stats.shadows,seconds,(stats.rays+stats.shadows)/seconds,
        (unsigned long long)warm.samples,build,upload,(unsigned long long)device->identity);
    vs_gpu_close(gpu);
    vs_film_destroy(&f);
    cfg.backend=VS_BACKEND_CPU;
    REQUIRE(vs_film_init(&f,&scene,cfg,(VsGate){0},0,&error));
    stats=(VsStats){0};
    seconds=now();
    REQUIRE(vs_render_cpu(&scene,&f,full,spp,&stats,&error));
    seconds=now()-seconds;
    printf("{\"backend\":\"cpu\",\"triangles\":%u,\"samples\":%llu,\"path_segments\":%llu,"
        "\"shadow_segments\":%llu,\"seconds\":%.9g,\"segments_per_s\":%.9g}\n",scene.primitive_count,
        (unsigned long long)stats.samples,(unsigned long long)stats.rays,(unsigned long long)stats.shadows,
        seconds,(stats.rays+stats.shadows)/seconds);
    vs_film_destroy(&f);
    vs_scene_destroy(&scene);
    return 0;
}
