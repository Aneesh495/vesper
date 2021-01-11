#include "vesper/film.h"
#include "check.h"

static VsRender config(uint32_t depth,uint32_t seed) {
    return (VsRender){1,1,seed,depth,VS_RANDOM,VS_WAVELENGTH_MIXTURE,VS_BACKEND_CPU,true,false};
}

static void init(VsScene *s,VsError *e) {
    REQUIRE(vs_scene_init(s,e));
    s->camera_count=1;
    s->cameras[0]=(VsCamera){.name="axial",.position={0,0,4},.target={0,0,0},.up={0,1,0},
        .vfov=1e-6,.focus=4};
    VsMaterial m={.name="patch",.kind=VS_DIFFUSE,.albedo=vs_flat(.6),.ior_a=1};
    REQUIRE(vs_scene_add_material(s,m,e));
}

static void quad(VsScene *s,VsError *e,uint32_t material,double z,double size,bool up) {
    VsVec3 a=vs_v(-size,-size,z),b=vs_v(size,-size,z),c=vs_v(size,size,z),d=vs_v(-size,size,z);
    VsPrimitive p={.a=a,.b=up?b:c,.c=up?c:b,.shape=VS_TRIANGLE,.material=material};
    REQUIRE(vs_scene_add_primitive(s,p,e));
    p.b=up?c:d; p.c=up?d:c;
    REQUIRE(vs_scene_add_primitive(s,p,e));
}

static void direct_and_bounce(void) {
    const double width=.125e-9,tol=.5*width;
    VsError e={0};
    for (unsigned k=0;k<15;++k) {
        VsScene s={0};
        init(&s,&e);
        bool direct=k<6;
        double z=-.13*k;
        quad(&s,&e,0,z,3,true);
        double optical=4-z;
        VsPulse pulse={(k%3)*.21e-9,(k%4)*.18e-9};
        if (direct) {
            s.materials[0].emission=vs_flat(2.1);
            s.materials[0].pulse=pulse;
        } else {
            double x=.14*(k-4),height=1+.03*k;
            VsLight light={.kind=VS_POINT,.vector={x,0,z+height},.power=vs_flat(2.1),.pulse=pulse};
            REQUIRE(vs_scene_add_light(&s,light,&e));
            optical+=hypot(x,height);
        }
        s.response=(k%5)*.11e-9;
        REQUIRE(vs_scene_compile(&s,&e));
        VsRender cfg=config(direct?0:1,211+k);
        VsPath p;
        REQUIRE(vs_trace_path(&s,cfg,0,7,&p,NULL,&e) && p.count==1 && p.events[0].timed);
        REQUIRE(fabs(p.events[0].delay-optical/299792458.)<=tol);
        if (!direct) {
            double dist=optical-(4-z),height=1+.03*k;
            double expected=.6*2.1*height/(VS_PI*dist*dist*dist);
            REQUIRE(fabs(p.events[0].value*p.wavelength.pdf-expected)<1e-8);
        }
        VsFilm f={0};
        REQUIRE(vs_film_init(&f,&s,cfg,(VsGate){512,0,width},0,&e));
        REQUIRE(vs_film_add(&f,0,&p,s.response,&e));
        double energy=0,center=0;
        for (uint32_t bin=0;bin<f.gate.bins;++bin) {
            double y=vs_film_slice(&f,0,bin).y;
            energy+=y;
            center+=(bin+.5)*width*y;
        }
        REQUIRE(energy>0 && fabs(energy-f.pixels[0].mean.y)<=2e-6*f.pixels[0].mean.y);
        double expected=optical/299792458.+pulse.start+.5*(pulse.width+s.response);
        REQUIRE(fabs(center/energy-expected)<=tol);
        printf("{\"case\":\"%s-%u\",\"length_m\":%.17g,\"pulse_start_s\":%.17g,\"pulse_width_s\":%.17g,"
            "\"response_s\":%.17g,\"arrival_s\":%.17g,\"centroid_s\":%.17g,\"bins_y\":%.17g,"
            "\"steady_y\":%.17g,\"step_s\":%.17g}\n",direct?"direct":"bounce",k,optical,pulse.start,
            pulse.width,s.response,p.events[0].delay,center/energy,energy,f.pixels[0].mean.y,width);
        vs_film_destroy(&f);
        vs_scene_destroy(&s);
    }
}

static void glass(void) {
    VsError e={0};
    for (unsigned k=0;k<5;++k) {
        VsScene s={0};
        init(&s,&e);
        s.materials[0].emission=vs_flat(1.5);
        quad(&s,&e,0,-1,2,true);
        double a=1.25+.08*k,b=.002*k,r=.36+.04*k;
        VsMaterial m={.name="glass",.kind=VS_DIELECTRIC,.ior_a=a,.ior_b=b};
        REQUIRE(vs_scene_add_material(&s,m,&e));
        VsPrimitive p={.a={0,0,1},.shape=VS_SPHERE,.radius=r,.material=1,.boundary=1};
        REQUIRE(vs_scene_add_primitive(&s,p,&e));
        REQUIRE(vs_scene_compile(&s,&e));
        bool found=false;
        for (uint32_t i=0;i<32 && !found;++i) {
            VsPath path;
            REQUIRE(vs_trace_path(&s,config(2,203+k),0,i,&path,NULL,&e));
            if (path.count!=1) continue;
            double um=path.wavelength.nm/1000;
            double length=5+2*r*(a+3*b/(um*um)-1);
            double actual=path.events[0].delay;
            REQUIRE(fabs(actual-length/299792458.)<1e-14);
            REQUIRE(fabs(path.events[0].value*path.wavelength.pdf-1.5)<1e-12);
            printf("{\"case\":\"glass-%u\",\"length_m\":%.17g,\"arrival_s\":%.17g,"
                "\"wavelength_nm\":%.17g,\"sample\":%u,\"step_s\":1.25e-10}\n",k,length,actual,
                path.wavelength.nm,i);
            found=true;
        }
        REQUIRE(found);
        vs_scene_destroy(&s);
    }
}

static void clipped(void) {
    VsError e={0};
    VsScene s={0};
    init(&s,&e);
    REQUIRE(vs_scene_compile(&s,&e));
    VsFilm f={0};
    REQUIRE(vs_film_init(&f,&s,config(0,1),(VsGate){4,0,.5e-9},0,&e));
    VsPath p={.wavelength={555,1},.count=1,.events={{1,0,{0,2e-9},true}}};
    REQUIRE(vs_film_add(&f,0,&p,2e-9,&e));
    double sum=0;
    for (unsigned i=0;i<4;++i) sum+=vs_film_slice(&f,0,i).y;
    REQUIRE(fabs(sum-.5)<2e-7 && fabs(f.pixels[0].excluded_y-.5)<1e-12);
    printf("{\"case\":\"clipped\",\"bins_y\":%.17g,\"steady_y\":%.17g,\"excluded_y\":%.17g}\n",
           sum,f.pixels[0].mean.y,f.pixels[0].excluded_y);
    vs_film_destroy(&f);
    vs_scene_destroy(&s);
}

static void square_mis(void) {
    double expected=0;
    const unsigned cells=400;
    for (unsigned j=0;j<cells;++j) for (unsigned i=0;i<cells;++i) {
        double x=(i+.5)/cells-.5,y=(j+.5)/cells-.5;
        double d2=4+x*x+y*y;
        expected+=.6*2.1*4/(VS_PI*d2*d2*cells*cells);
    }
    VsError e={0};
    VsScene s={0};
    init(&s,&e);
    s.cameras[0].position.z=1;
    VsMaterial emitter={.name="panel",.kind=VS_DIFFUSE,.emission=vs_flat(2.1),.ior_a=1};
    REQUIRE(vs_scene_add_material(&s,emitter,&e));
    quad(&s,&e,0,0,8,true);
    quad(&s,&e,1,2,.5,false);
    REQUIRE(vs_scene_compile(&s,&e));
    const uint32_t seeds[]={11,19,47};
    for (unsigned mode=0;mode<2;++mode) for (unsigned j=0;j<3;++j) {
        VsRender cfg=config(1,seeds[j]);
        cfg.nee=mode!=0;
        double mean=0,m2=0;
        for (uint32_t i=1;i<=131072;++i) {
            VsPath path;
            REQUIRE(vs_trace_path(&s,cfg,0,i-1,&path,NULL,&e));
            double value=0;
            for (uint32_t k=0;k<path.count;++k) value+=path.events[k].value*path.wavelength.pdf;
            double delta=value-mean;
            mean+=delta/i;
            m2+=delta*(value-mean);
            if (i==4096 || i==131072) {
                double se=sqrt(m2/(i-1)/i);
                if (i==131072) REQUIRE(fabs(mean-expected)<=6*se+.0001);
                printf("{\"case\":\"square-mis\",\"nee\":%u,\"seed\":%u,\"samples\":%u,"
                    "\"estimate\":%.17g,\"m2\":%.17g,\"expected\":%.17g}\n",
                    mode,seeds[j],i,mean,m2,expected);
            }
        }
    }
    vs_scene_destroy(&s);
}

int main(void) {
    direct_and_bounce();
    glass();
    clipped();
    square_mis();
    return 0;
}
