#include "vesper/film.h"
#include "wire.h"

#include <stdio.h>
#include <stdlib.h>

static bool bytes(FILE *fp,const void *p,size_t count) { return fwrite(p,1,count,fp)==count; }

static bool integer(FILE *fp,uint64_t n,unsigned size) {
    unsigned char buf[8];
    vs_store_u(buf,n,size);
    return bytes(fp,buf,size);
}

static bool attribute(FILE *fp,const char *name,const char *type,const void *data,uint32_t size) {
    return bytes(fp,name,strlen(name)+1) && bytes(fp,type,strlen(type)+1) &&
        integer(fp,size,4) && bytes(fp,data,size);
}

static const char *channels[]={"ExcludedY","S0","S1","S2","S3","S4","S5","S6","S7",
                               "Samples","TimedY","VarianceY","X","Y","Z"};
enum { CH_COUNT=sizeof channels/sizeof *channels };

static bool exr_header(FILE *fp,const VsFilm *f) {
    unsigned char list[512]={0},box[16]={0},one[4],zero[8]={0};
    size_t pos=0;
    for (unsigned i=0;i<CH_COUNT;++i) {
        size_t n=strlen(channels[i])+1;
        memcpy(list+pos,channels[i],n);
        pos+=n;
        vs_store_u(list+pos,i==9?0:2,4);
        list[pos+4]=1;
        vs_store_u(list+pos+8,1,4);
        vs_store_u(list+pos+12,1,4);
        pos+=16;
    }
    vs_store_u(box+8,f->render.width-1,4);
    vs_store_u(box+12,f->render.height-1,4);
    vs_store_f32(one,1);
    char meta[256];
    int n=snprintf(meta,sizeof meta,
        "scene=%016llx;device=%016llx;backend=%u;seed=%u;sampler=%u;wavelength=%u;depth=%u;abi=%u;bins=%u;t0_s=%.17g;dt_s=%.17g",
        (unsigned long long)f->scene,(unsigned long long)f->device,f->render.backend,
        f->render.seed,f->render.sampler,f->render.wavelength,f->render.max_depth,
        VS_FILM_ABI,f->gate.bins,f->gate.start,f->gate.width);
    return n>0 && (size_t)n<sizeof meta &&
        integer(fp,20000630,4) && integer(fp,2,4) &&
        attribute(fp,"channels","chlist",list,(uint32_t)pos+1) &&
        attribute(fp,"compression","compression",zero,1) &&
        attribute(fp,"dataWindow","box2i",box,16) &&
        attribute(fp,"displayWindow","box2i",box,16) &&
        attribute(fp,"lineOrder","lineOrder",zero,1) &&
        attribute(fp,"pixelAspectRatio","float",one,4) &&
        attribute(fp,"screenWindowCenter","v2f",zero,8) &&
        attribute(fp,"screenWindowWidth","float",one,4) &&
        attribute(fp,"vesper","string",meta,(uint32_t)n) && integer(fp,0,1);
}

static double channel_value(const VsPixel *p,unsigned c) {
    if (!c) return p->excluded_y;
    if (c<=8) return p->samples?p->spectral[c-1]/(double)p->samples:0;
    if (c==10) return p->timed_y;
    if (c==11) return vs_film_variance(p);
    return vs_at(p->mean,c-12);
}

bool vs_export_exr(const VsFilm *f,const char *path,VsError *error) {
    if (!f->pixels) return vs_fail(error,"film is empty");
    FILE *fp=fopen(path,"wb");
    if (!fp) return vs_fail(error,"could not write %s",path);
    bool ok=exr_header(fp,f);
    long header=ftell(fp);
    if (header<0) ok=false;
    uint32_t w=f->render.width,h=f->render.height, size=w*4*CH_COUNT;
    unsigned char *row=malloc(size);
    if (!row) ok=false;
    for (uint32_t y=0;ok && y<h;++y)
        ok=integer(fp,(uint64_t)header+8u*h+(uint64_t)y*(size+8),8);
    for (uint32_t y=0;ok && y<h;++y) {
        for (unsigned c=0;c<CH_COUNT && ok;++c) for (uint32_t x=0;x<w;++x) {
            const VsPixel *p=f->pixels+(size_t)y*w+x;
            unsigned char *out=row+4*(c*w+x);
            if (c==9) vs_store_u(out,p->samples,4);
            else {
                float value=(float)channel_value(p,c);
                if (!isfinite(value)) { ok=false; break; }
                vs_store_f32(out,value);
            }
        }
        ok=ok && integer(fp,y,4) && integer(fp,size,4) && bytes(fp,row,size);
    }
    free(row);
    if (fclose(fp)) ok=false;
    if (!ok) { remove(path); return vs_fail(error,"OpenEXR write failed or exceeded numeric range"); }
    return true;
}

bool vs_export_ppm(const VsFilm *f,const char *path,double exposure,uint32_t bin,VsError *error) {
    if (!f->pixels || !isfinite(exposure) || (bin!=UINT32_MAX && bin>=f->gate.bins))
        return vs_fail(error,"invalid display export");
    FILE *fp=fopen(path,"wb");
    if (!fp) return vs_fail(error,"could not write %s",path);
    bool ok=fprintf(fp,"P6\n%u %u\n255\n",f->render.width,f->render.height)>0;
    uint32_t count=f->render.width*f->render.height;
    for (uint32_t i=0;ok && i<count;++i) {
        VsVec3 xyz=bin==UINT32_MAX?f->pixels[i].mean:vs_film_slice(f,i,bin);
        VsVec3 v=vs_display(xyz,exposure);
        unsigned char pixel[]={(unsigned char)lround(v.x*255),(unsigned char)lround(v.y*255),
            (unsigned char)lround(v.z*255)};
        ok=bytes(fp,pixel,sizeof pixel);
    }
    if (fclose(fp)) ok=false;
    if (!ok) { remove(path); return vs_fail(error,"display image write failed"); }
    return true;
}

bool vs_export_time_csv(const VsFilm *f,const char *path,uint32_t pixel,VsError *error) {
    if (!f->gate.bins || pixel>=f->render.width*f->render.height)
        return vs_fail(error,"invalid transient export pixel or gate");
    FILE *fp=fopen(path,"w");
    if (!fp) return vs_fail(error,"could not write %s",path);
    bool ok=fprintf(fp,"bin,start_s,end_s,X,Y,Z,samples\n")>0;
    for (uint32_t b=0;ok && b<f->gate.bins;++b) {
        VsVec3 v=vs_film_slice(f,pixel,b);
        ok=fprintf(fp,"%u,%.17g,%.17g,%.17g,%.17g,%.17g,%u\n",b,
                   f->gate.start+b*f->gate.width,f->gate.start+(b+1)*f->gate.width,
                   v.x,v.y,v.z,f->pixels[pixel].samples)>0;
    }
    if (fclose(fp)) ok=false;
    if (!ok) { remove(path); return vs_fail(error,"transient CSV write failed"); }
    return true;
}
