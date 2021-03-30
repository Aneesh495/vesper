#include "vesper/gpu.h"
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH=256, HEIGHT=192, TILE=16, SPP_LIMIT=1024 };

typedef struct {
    VsScene scene;
    VsFilm film;
    VsGpu *gpu;
    VsRender cfg;
    VsGate gate;
    VsRegion region;
    VsError error;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *image;
    uint32_t *rgba;
    const char *export_prefix;
    uint32_t bin,picked,sweep,x,y,selection,light;
    int anchor_x,anchor_y;
    double exposure;
    bool paused,quit,dragging,transient,material_plot;
} Workbench;

static bool reopen(Workbench *w) {
    if (!vs_scene_compile(&w->scene,&w->error)) return false;
    vs_gpu_close(w->gpu);
    w->gpu=NULL;
    uint64_t device=0;
    if (w->cfg.backend==VS_BACKEND_VULKAN) {
        if (!vs_gpu_open(&w->gpu,&w->scene,w->cfg,&w->error)) return false;
        device=vs_gpu_device(w->gpu)->identity;
    }
    if (!vs_film_init(&w->film,&w->scene,w->cfg,w->gate,device,&w->error)) return false;
    w->sweep=1;
    w->x=w->region.x; w->y=w->region.y;
    return true;
}

static bool update(Workbench *w) {
    if (w->paused || w->sweep>SPP_LIMIT) return true;
    VsRegion r=w->region;
    VsRegion tile={w->x,w->y,(uint32_t)fmin(TILE,r.x+r.width-w->x),
        (uint32_t)fmin(TILE,r.y+r.height-w->y)};
    bool ok=w->gpu?vs_gpu_render(w->gpu,&w->scene,&w->film,tile,w->sweep,NULL,&w->error):
        vs_render_cpu(&w->scene,&w->film,tile,w->sweep,NULL,&w->error);
    if (!ok) return false;
    w->x+=tile.width;
    if (w->x>=r.x+r.width) { w->x=r.x; w->y+=tile.height; }
    if (w->y>=r.y+r.height) { w->y=r.y; ++w->sweep; }
    return true;
}

static unsigned color(double x) { return (unsigned)lround(255*x); }

static void plot(Workbench *w,int x,int y,int width,int height,const double *a,unsigned n,
                 unsigned r,unsigned g,unsigned b) {
    SDL_Rect frame={x,y,width,height};
    SDL_SetRenderDrawColor(w->renderer,56,70,82,255);
    SDL_RenderDrawRect(w->renderer,&frame);
    double maximum=0;
    for (unsigned i=0;i<n;++i) maximum=fmax(maximum,a[i]);
    if (maximum==0) return;
    SDL_SetRenderDrawColor(w->renderer,(Uint8)r,(Uint8)g,(Uint8)b,255);
    for (unsigned i=0;i<n;++i) {
        int start=x+1+(int)((width-2)*i/n),end=x+1+(int)((width-2)*(i+1)/n);
        int h=(int)lround((height-4)*a[i]/maximum);
        SDL_Rect bar={start,y+height-2-h,end-start,h};
        SDL_RenderFillRect(w->renderer,&bar);
    }
}

static bool draw(Workbench *w) {
    for (uint32_t i=0;i<WIDTH*HEIGHT;++i) {
        VsVec3 xyz=w->transient?vs_film_slice(&w->film,i,w->bin):w->film.pixels[i].mean;
        VsVec3 c=vs_display(xyz,w->exposure);
        w->rgba[i]=UINT32_C(0xff000000)|(color(c.x)<<16)|(color(c.y)<<8)|color(c.z);
    }
    if (SDL_UpdateTexture(w->image,NULL,w->rgba,WIDTH*4))
        return vs_fail(&w->error,"SDL texture update: %s",SDL_GetError());
    SDL_SetRenderDrawColor(w->renderer,16,22,28,255);
    SDL_RenderClear(w->renderer);
    SDL_Rect image={0,0,WIDTH,HEIGHT};
    SDL_RenderCopy(w->renderer,w->image,NULL,&image);
    SDL_SetRenderDrawColor(w->renderer,52,228,211,255);
    SDL_Rect region={(int)w->region.x,(int)w->region.y,(int)w->region.width,(int)w->region.height};
    SDL_RenderDrawRect(w->renderer,&region);
    SDL_Rect pick={(int)(w->picked%WIDTH)-2,(int)(w->picked/WIDTH)-2,5,5};
    SDL_RenderDrawRect(w->renderer,&pick);
    const VsPixel *p=w->film.pixels+w->picked;
    double spectrum[VS_SPECTRAL_KNOTS],time[128]={0};
    for (unsigned i=0;i<VS_SPECTRAL_KNOTS;++i) {
        if (w->material_plot) {
            double nm=VS_LAMBDA_MIN+i*(VS_LAMBDA_MAX-VS_LAMBDA_MIN)/(VS_SPECTRAL_KNOTS-1);
            if (w->selection<w->scene.material_count)
                spectrum[i]=vs_spectrum(w->scene.materials[w->selection].albedo,nm);
            else spectrum[i]=w->scene.explicit_lights?vs_spectrum(w->scene.lights[w->light].power,nm):0;
        } else spectrum[i]=p->spectral[i];
    }
    for (unsigned i=0;i<128;++i)
        for (unsigned k=0;k<4;++k) time[i]+=vs_film_slice(&w->film,w->picked,4*i+k).y;
    plot(w,6,HEIGHT+8,76,34,spectrum,VS_SPECTRAL_KNOTS,246,162,96);
    plot(w,90,HEIGHT+8,160,34,time,128,105,211,226);
    int bx=91+(int)(157*w->bin/511);
    SDL_SetRenderDrawColor(w->renderer,241,238,228,255);
    SDL_RenderDrawLine(w->renderer,bx,HEIGHT+6,bx,HEIGHT+44);
    char status[256];
    snprintf(status,sizeof status,"Vesper %s %s | (%u,%u) %u spp | Y %.5g var %.4g | %s %.2f ns | EV %+.1f",
        w->gpu?"Vulkan":"CPU",w->paused?"paused":"progressive",w->picked%WIDTH,w->picked/WIDTH,
        p->samples,p->mean.y,vs_film_variance(p),w->transient?"gate":"steady",w->bin*.125,w->exposure);
    SDL_SetWindowTitle(w->window,status);
    return true;
}

static void pick(Workbench *w,int x,int y) {
    if (x<0 || x>=WIDTH || y<0 || y>=HEIGHT) return;
    w->picked=(uint32_t)(y*WIDTH+x);
    VsCamera camera=w->scene.cameras[w->scene.camera];
    VsSample sample={w->picked,0,w->cfg.seed,VS_RANDOM};
    VsHit hit;
    VsRay ray=vs_camera_ray(&camera,WIDTH,HEIGHT,sample);
    w->selection=vs_bvh_hit(&w->scene.bvh,w->scene.primitives,ray,&hit)?
        w->scene.primitives[hit.primitive].material:UINT32_MAX;
}

static bool trace(Workbench *w) {
    VsRender cfg=w->cfg;
    cfg.backend=VS_BACKEND_CPU;
    VsPath p;
    VsTrace tr;
    if (!vs_trace_path(&w->scene,cfg,w->picked,0,&p,&tr,&w->error)) return false;
    printf("CPU reference pixel=%u wavelength_nm=%.8g pdf_per_nm=%.8g (src/transport.c)\n",
           w->picked,p.wavelength.nm,p.wavelength.pdf);
    for (uint32_t i=0;i<tr.count;++i) {
        const VsTraceEvent *e=tr.events+i;
        printf("  event=%u depth=%u primitive=%u medium=%u time_ns=%.9g pdf=%.8g weight=%.8g\n",
            e->kind,e->depth,e->primitive,e->medium,e->delay*1e9,e->pdf,e->weight);
    }
    return true;
}

static bool camera_key(Workbench *w,SDL_Keycode key) {
    VsCamera *c=w->scene.cameras+w->scene.camera;
    VsVec3 axis=vs_sub(c->position,c->target),up=vs_unit(c->up);
    VsVec3 right=vs_unit(vs_cross(up,axis));
    if (key==SDLK_LEFT || key==SDLK_RIGHT) {
        double angle=key==SDLK_LEFT?-.08:.08;
        VsVec3 rotated=vs_add(vs_mul(axis,cos(angle)),vs_mul(vs_cross(up,axis),sin(angle)));
        rotated=vs_add(rotated,vs_mul(up,vs_dot(up,axis)*(1-cos(angle))));
        c->position=vs_add(c->target,rotated);
    } else {
        VsVec3 d=key==SDLK_w?vs_mul(vs_unit(axis),-.17):key==SDLK_s?vs_mul(vs_unit(axis),.17):
            key==SDLK_a?vs_mul(right,-.17):key==SDLK_d?vs_mul(right,.17):
            key==SDLK_q?vs_mul(up,-.17):vs_mul(up,.17);
        c->position=vs_add(c->position,d);
        c->target=vs_add(c->target,d);
    }
    return reopen(w);
}

static void change_power(VsSpectrum *s,double scale,double limit) {
    unsigned count=s->kind==VS_GAUSSIAN?1:VS_SPECTRAL_KNOTS;
    for (unsigned i=0;i<count;++i) s->values[i]=fmin(limit,s->values[i]*scale);
}

static bool export_frame(Workbench *w) {
    char exr[4096],film[4096],time[4096];
    if (strlen(w->export_prefix)>sizeof exr-sizeof ".time.csv")
        return vs_fail(&w->error,"workbench export path length limit");
    snprintf(exr,sizeof exr,"%s.exr",w->export_prefix);
    snprintf(film,sizeof film,"%s.vfilm",w->export_prefix);
    snprintf(time,sizeof time,"%s.time.csv",w->export_prefix);
    return vs_export_exr(&w->film,exr,&w->error) &&
        vs_checkpoint_write(&w->film,film,&w->error) &&
        vs_export_time_csv(&w->film,time,w->picked,&w->error);
}

static bool keypress(Workbench *w,SDL_Keycode key) {
    switch (key) {
        case SDLK_ESCAPE: w->quit=true; return true;
        case SDLK_SPACE: w->paused=!w->paused; return true;
        case SDLK_t: w->transient=!w->transient; return true;
        case SDLK_TAB: w->material_plot=!w->material_plot; return true;
        case SDLK_LEFTBRACKET: if (w->bin) --w->bin; return true;
        case SDLK_RIGHTBRACKET: if (w->bin<511) ++w->bin; return true;
        case SDLK_EQUALS: w->exposure=fmin(20,w->exposure+.5); return true;
        case SDLK_MINUS: w->exposure=fmax(-20,w->exposure-.5); return true;
        case SDLK_l:
            if (w->scene.explicit_lights) w->light=(w->light+1)%w->scene.explicit_lights;
            w->selection=UINT32_MAX;
            return true;
        case SDLK_p: return trace(w);
        case SDLK_x: return export_frame(w);
        case SDLK_r:
            w->region=(VsRegion){0,0,WIDTH,HEIGHT};
            w->x=0; w->y=0; w->sweep=1;
            return true;
        case SDLK_1: case SDLK_2:
            if (w->selection>=w->scene.material_count) return true;
            change_power(&w->scene.materials[w->selection].albedo,key==SDLK_1?.83:1.2,1);
            return reopen(w);
        case SDLK_3: case SDLK_4: {
            if (!w->scene.explicit_lights) return true;
            VsLight *light=w->scene.lights+w->light;
            double scale=key==SDLK_3?.83:1.2;
            change_power(&light->power,scale,1e9);
            light->cone_power=fmin(1e9,light->cone_power*scale);
            return reopen(w);
        }
        case SDLK_LEFT: case SDLK_RIGHT: case SDLK_w: case SDLK_a: case SDLK_s: case SDLK_d:
        case SDLK_q: case SDLK_e: return camera_key(w,key);
        default: return true;
    }
}

static bool event(Workbench *w,const SDL_Event *e) {
    if (e->type==SDL_QUIT) w->quit=true;
    if (e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)
        w->dragging=false;
    if (e->type==SDL_KEYDOWN && !e->key.repeat) return keypress(w,e->key.keysym.sym);
    if (e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP) {
        /* SDL_RenderSetLogicalSize already scales queued mouse events. */
        int x=e->button.x,y=e->button.y;
        if (e->button.button==SDL_BUTTON_LEFT && e->type==SDL_MOUSEBUTTONDOWN) {
            if (y>=HEIGHT+6 && y<HEIGHT+44 && x>=90 && x<250) {
                w->bin=(uint32_t)((x-90)*512/160);
                w->transient=true;
            } else pick(w,x,y);
        }
        if (e->button.button==SDL_BUTTON_RIGHT && e->type==SDL_MOUSEBUTTONDOWN) {
            w->anchor_x=x; w->anchor_y=y; w->dragging=true;
        }
        if (e->button.button==SDL_BUTTON_RIGHT && e->type==SDL_MOUSEBUTTONUP && w->dragging) {
            w->dragging=false;
            int lo_x=(int)fmax(0,fmin(x,w->anchor_x)),hi_x=(int)fmin(WIDTH-1,fmax(x,w->anchor_x));
            int lo_y=(int)fmax(0,fmin(y,w->anchor_y)),hi_y=(int)fmin(HEIGHT-1,fmax(y,w->anchor_y));
            if (lo_x<=hi_x && lo_y<=hi_y) {
                w->region=(VsRegion){(uint32_t)lo_x,(uint32_t)lo_y,(uint32_t)(hi_x-lo_x+1),(uint32_t)(hi_y-lo_y+1)};
                w->x=w->region.x; w->y=w->region.y; w->sweep=1;
            }
        }
    }
    return true;
}

static bool pump(Workbench *w) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) if (!event(w,&e)) return false;
    return true;
}

static bool snapshot(Workbench *w,const char *path) {
    int width,height;
    if (SDL_GetRendererOutputSize(w->renderer,&width,&height) ||
        width<=0 || height<=0 || width>4096 || height>4096)
        return vs_fail(&w->error,"invalid screenshot output size");
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
    if (!s) return vs_fail(&w->error,"SDL screenshot allocation: %s",SDL_GetError());
    bool ok=!SDL_RenderReadPixels(w->renderer,NULL,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch) &&
        !SDL_SaveBMP(s,path);
    SDL_FreeSurface(s);
    return ok || vs_fail(&w->error,"SDL screenshot: %s",SDL_GetError());
}

static bool send_event(Workbench *w,SDL_Event e) {
    if (SDL_PushEvent(&e)!=1) return vs_fail(&w->error,"could not queue SDL probe: %s",SDL_GetError());
    return pump(w);
}

static bool send_key(Workbench *w,SDL_Keycode key) {
    SDL_Event e={.type=SDL_KEYDOWN};
    e.key.windowID=SDL_GetWindowID(w->window);
    e.key.keysym.sym=key;
    return send_event(w,e);
}

static bool send_button(Workbench *w,Uint32 type,Uint8 button,int x,int y) {
    SDL_Event e={.type=type};
    e.button.windowID=SDL_GetWindowID(w->window);
    e.button.button=button;
    int px,py;
    SDL_RenderLogicalToWindow(w->renderer,x+.5f,y+.5f,&px,&py);
    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
        float lx,ly;
        SDL_RenderWindowToLogical(w->renderer,px+dx,py+dy,&lx,&ly);
        if ((int)floorf(lx)!=x || (int)floorf(ly)!=y) continue;
        e.button.x=px+dx;
        e.button.y=py+dy;
        return send_event(w,e);
    }
    return vs_fail(&w->error,"workbench probe could not map a logical pixel to the window");
}

static bool smoke(Workbench *w,const char *path,unsigned spp) {
    for (unsigned i=0;i<360;++i) if (!update(w)) return false;
    const uint32_t first_pick=35*WIDTH+27;
    if (!send_button(w,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT,27,35) || w->picked!=first_pick)
        return vs_fail(&w->error,"workbench native viewport picking failed (picked %u)",w->picked);
    uint32_t count=w->film.pixels[first_pick].samples;
    if (!count || !send_key(w,SDLK_SPACE) || !w->paused || !update(w) ||
        w->film.pixels[first_pick].samples!=count)
        return vs_fail(&w->error,"workbench pause probe failed");

    SDL_SetWindowSize(w->window,480,425);
    SDL_PumpEvents();
    if (!pump(w) || !send_button(w,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_LEFT,170,HEIGHT+20) ||
        !w->transient || w->bin!=256 || !send_key(w,SDLK_RIGHTBRACKET) || w->bin!=257 ||
        !draw(w) || w->film.pixels[first_pick].samples!=count)
        return vs_fail(&w->error,"workbench resize or transient scrub probe failed (bin %u)",w->bin);

    SDL_Event focus={.type=SDL_WINDOWEVENT};
    focus.window.windowID=SDL_GetWindowID(w->window);
    focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    if (!send_button(w,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT,16,16) || !w->dragging ||
        !send_event(w,focus) || w->dragging ||
        !send_button(w,SDL_MOUSEBUTTONUP,SDL_BUTTON_RIGHT,31,31) || w->region.width!=WIDTH)
        return vs_fail(&w->error,"workbench focus-loss drag cancellation failed");

    if (!send_button(w,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT,16,16) ||
        !send_button(w,SDL_MOUSEBUTTONUP,SDL_BUTTON_RIGHT,31,31) ||
        w->region.x!=16 || w->region.y!=16 || w->region.width!=16 || w->region.height!=16)
        return vs_fail(&w->error,"workbench resized region selection failed (%u,%u %ux%u)",
                       w->region.x,w->region.y,w->region.width,w->region.height);
    uint64_t old=w->scene.identity;
    if (!send_key(w,SDLK_RIGHT) || old==w->scene.identity || w->film.pixels[first_pick].samples ||
        !send_key(w,SDLK_SPACE) || w->paused || !update(w) ||
        w->film.pixels[16*WIDTH+16].samples!=1 || w->film.pixels[first_pick].samples)
        return vs_fail(&w->error,"workbench invalidation or bounded region rendering failed");

    if (w->scene.explicit_lights) {
        old=w->scene.identity;
        if (!send_key(w,SDLK_3) || old==w->scene.identity || w->film.pixels[16*WIDTH+16].samples)
            return vs_fail(&w->error,"workbench light edit kept stale samples");
    }
    if (!send_key(w,SDLK_r) || w->region.width!=WIDTH ||
        !send_key(w,SDLK_t) || w->transient)
        return vs_fail(&w->error,"workbench could not restore steady full-frame view");
    for (unsigned i=0;i<spp*(WIDTH/TILE)*(HEIGHT/TILE);++i) if (!update(w)) return false;
    if (!draw(w) || !snapshot(w,path) || !send_key(w,SDLK_x) ||
        !send_key(w,SDLK_ESCAPE) || !w->quit)
        return vs_fail(&w->error,"workbench capture, export or close probe failed");
    printf("{\"backend\":\"%s\",\"picked\":%u,\"samples\":%u,\"scene\":\"%016llx\","
           "\"window_width\":480,\"window_height\":425}\n",
           w->gpu?"vulkan":"cpu",w->picked,w->film.pixels[w->picked].samples,
           (unsigned long long)w->scene.identity);
    return true;
}

int main(int argc,char **argv) {
    bool gpu=argc>2 && !strcmp(argv[2],"vulkan");
    bool cpu=argc==2 || (argc>2 && !strcmp(argv[2],"cpu"));
    bool probe=(argc==5 || argc==6) && !strcmp(argv[3],"--smoke");
    unsigned spp=8;
    if (argc==6 && probe) {
        char *end;
        unsigned long value=strtoul(argv[5],&end,10);
        if (end==argv[5] || *end || !value || value>512) return 2;
        spp=(unsigned)value;
    }
    if (argc<2 || (!gpu && !cpu) || (argc>3 && !probe)) {
        fputs("usage: vesper-workbench SCENE [cpu|vulkan] [--smoke OUT.bmp [SPP]]\n",stderr);
        return 2;
    }
    Workbench w={.cfg={WIDTH,HEIGHT,1789,7,VS_STRATIFIED,VS_WAVELENGTH_MIXTURE,
        gpu?VS_BACKEND_VULKAN:VS_BACKEND_CPU,true,true},.gate={512,0,.125e-9},
        .export_prefix=probe?argv[4]:"vesper-workbench",
        .region={0,0,WIDTH,HEIGHT},.picked=WIDTH*(HEIGHT/2)+WIDTH/2,.selection=UINT32_MAX};
    bool ok=vs_scene_load(&w.scene,argv[1],&w.error) && reopen(&w);
    if (ok && SDL_Init(SDL_INIT_VIDEO)) ok=vs_fail(&w.error,"SDL initialization: %s",SDL_GetError());
    if (ok) w.window=SDL_CreateWindow("Vesper",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        800,700,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if (w.window) w.renderer=SDL_CreateRenderer(w.window,-1,0);
    if (w.renderer) w.image=SDL_CreateTexture(w.renderer,SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,WIDTH,HEIGHT);
    if (ok && !w.image) ok=vs_fail(&w.error,"SDL window/renderer/texture: %s",SDL_GetError());
    if (ok) {
        w.rgba=malloc((size_t)WIDTH*HEIGHT*sizeof *w.rgba);
        if (!w.rgba || SDL_RenderSetLogicalSize(w.renderer,WIDTH,HEIGHT+50))
            ok=vs_fail(&w.error,"workbench display allocation failed");
    }
    if (ok && probe) ok=smoke(&w,argv[4],spp);
    while (ok && !probe && !w.quit) {
        ok=pump(&w);
        for (unsigned i=0;i<6 && ok && !w.quit;++i) ok=update(&w);
        if (ok && !w.quit) { ok=draw(&w); SDL_RenderPresent(w.renderer); }
        SDL_Delay(12);
    }
    if (!ok) fprintf(stderr,"vesper-workbench: %s\n",w.error.message);
    free(w.rgba);
    SDL_DestroyTexture(w.image);
    SDL_DestroyRenderer(w.renderer);
    SDL_DestroyWindow(w.window);
    SDL_Quit();
    vs_film_destroy(&w.film);
    vs_gpu_close(w.gpu);
    vs_scene_destroy(&w.scene);
    return ok?0:1;
}
