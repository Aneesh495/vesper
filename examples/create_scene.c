#include "vesper/film.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    VsScene scene={0};
    VsFilm film={0};
    VsError error={0};
    VsMaterial diffuse={.kind=VS_DIFFUSE,.albedo=vs_flat(.55),.ior_a=1};
    strcpy(diffuse.name,"ceramic");
    VsLight lamp={.kind=VS_POINT,.vector={-2,3,3},.power=vs_gaussian(30,565,60)};
    bool ok=vs_scene_init(&scene,&error) &&
        vs_scene_add_material(&scene,diffuse,&error) &&
        vs_scene_add_primitive(&scene,(VsPrimitive){.radius=1,.shape=VS_SPHERE},&error) &&
        vs_scene_add_light(&scene,lamp,&error);
    scene.camera_count=1;
    scene.cameras[0]=(VsCamera){.name="front",.position={0,0,5},.up={0,1,0},.focus=5,.vfov=35};
    VsRender render={64,64,102,4,VS_STRATIFIED,VS_WAVELENGTH_MIXTURE,VS_BACKEND_CPU,true,true};
    VsRegion whole={0,0,64,64};
    ok=ok && vs_scene_compile(&scene,&error) &&
        vs_film_init(&film,&scene,render,(VsGate){512,0,.125e-9},0,&error) &&
        vs_render_cpu(&scene,&film,whole,8,NULL,&error) &&
        vs_checkpoint_write(&film,"example.vfilm",&error) &&
        vs_checkpoint_read(&film,"example.vfilm",&scene,render,(VsGate){512,0,.125e-9},0,&error) &&
        vs_render_cpu(&scene,&film,whole,16,NULL,&error) &&
        vs_export_exr(&film,"example.exr",&error);
    if (!ok) fprintf(stderr,"%s\n",error.message);
    vs_film_destroy(&film);
    vs_scene_destroy(&scene);
    return ok?0:1;
}
