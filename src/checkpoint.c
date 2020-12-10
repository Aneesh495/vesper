#define _POSIX_C_SOURCE 200809L
#include "vesper/film.h"
#include "wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

enum { HEADER=80, RECORD=84, TIME_BLOCK=1024 };

typedef struct { FILE *fp; uint64_t hash; } Stream;

static uint64_t digest(uint64_t h,const unsigned char *bytes,size_t n) {
    for (size_t i=0;i<n;++i) h=(h^bytes[i])*UINT64_C(1099511628211);
    return h;
}

static bool block(Stream *s,unsigned char *bytes,size_t n,bool read) {
    size_t size=read?fread(bytes,1,n,s->fp):fwrite(bytes,1,n,s->fp);
    if (size!=n) return false;
    s->hash=digest(s->hash,bytes,n);
    return true;
}

static void header(const VsFilm *f,unsigned char bytes[HEADER]) {
    memset(bytes,0,HEADER);
    memcpy(bytes,"VSPFILM\0",8);
    const VsRender c=f->render;
    const uint32_t fields[]={VS_FILM_ABI,c.width,c.height,c.seed,c.max_depth,c.sampler,c.wavelength,
        c.backend,(c.nee?1u:0u)|(c.roulette?2u:0u),f->gate.bins};
    for (unsigned i=0;i<sizeof fields/sizeof *fields;++i) vs_store_u(bytes+8+4*i,fields[i],4);
    vs_store_u(bytes+48,f->scene,8);
    vs_store_u(bytes+56,f->device,8);
    vs_store_f64(bytes+64,f->gate.start);
    vs_store_f64(bytes+72,f->gate.width);
}

static void encode_pixel(unsigned char bytes[RECORD],const VsPixel *p) {
    const double fields[]={p->mean.x,p->mean.y,p->mean.z,p->m2_y,p->timed_y,p->excluded_y};
    for (unsigned i=0;i<6;++i) vs_store_f64(bytes+8*i,fields[i]);
    for (unsigned i=0;i<VS_SPECTRAL_KNOTS;++i) vs_store_f32(bytes+48+4*i,p->spectral[i]);
    vs_store_u(bytes+80,p->samples,4);
}

static bool decode_pixel(const unsigned char bytes[RECORD],VsPixel *p) {
    double x[6];
    for (unsigned i=0;i<6;++i) {
        x[i]=vs_load_f64(bytes+8*i);
        if (!isfinite(x[i]) || x[i]<-1e-12) return false;
    }
    p->mean=vs_v(x[0],x[1],x[2]);
    p->m2_y=x[3]; p->timed_y=x[4]; p->excluded_y=x[5];
    p->samples=(uint32_t)vs_load_u(bytes+80,4);
    if (p->samples>VS_MAX_SPP || (p->samples<2 && p->m2_y!=0) ||
        p->timed_y>p->mean.y+1e-10*fmax(1,p->mean.y) ||
        p->excluded_y>p->mean.y+1e-10*fmax(1,p->mean.y)) return false;
    if (!p->samples)
        for (unsigned i=0;i<6;++i)
            if (x[i]!=0) return false;
    for (unsigned i=0;i<VS_SPECTRAL_KNOTS;++i) {
        p->spectral[i]=vs_load_f32(bytes+48+4*i);
        if (!(isfinite(p->spectral[i]) && p->spectral[i]>=0) || (!p->samples && p->spectral[i]!=0))
            return false;
    }
    return true;
}

static bool transfer(Stream *s,VsFilm *dst,const VsFilm *src) {
    bool read=dst!=NULL;
    const VsFilm *f=read?dst:src;
    size_t count=(size_t)f->render.width*f->render.height;
    unsigned char bytes[TIME_BLOCK*4];
    for (size_t i=0;i<count;++i) {
        if (!read) encode_pixel(bytes,src->pixels+i);
        if (!block(s,bytes,RECORD,read) || (read && !decode_pixel(bytes,dst->pixels+i))) return false;
    }
    size_t end=count*f->gate.bins*3;
    for (size_t i=0;i<end;) {
        size_t n=end-i<TIME_BLOCK?end-i:TIME_BLOCK;
        if (!read) for (size_t j=0;j<n;++j) vs_store_f32(bytes+4*j,src->time[i+j]);
        if (!block(s,bytes,n*4,read)) return false;
        if (read) for (size_t j=0;j<n;++j) {
            float x=vs_load_f32(bytes+4*j);
            if (!(isfinite(x) && x>=0) || (x && !dst->pixels[(i+j)/(f->gate.bins*3)].samples))
                return false;
            dst->time[i+j]=x;
        }
        i+=n;
    }
    if (!read) {
        vs_store_u(bytes,s->hash,8);
        return fwrite(bytes,1,8,s->fp)==8;
    }
    return fread(bytes,1,8,s->fp)==8 && vs_load_u(bytes,8)==s->hash &&
        fgetc(s->fp)==EOF && !ferror(s->fp);
}

bool vs_checkpoint_write(const VsFilm *f,const char *path,VsError *error) {
    if (!f->pixels) return vs_fail(error,"cannot checkpoint an empty film");
    char temp[4096];
    int n=snprintf(temp,sizeof temp,"%s.tmp.XXXXXX",path);
    if (n<0 || (size_t)n>=sizeof temp) return vs_fail(error,"checkpoint path length limit");
    int fd=mkstemp(temp);
    if (fd<0) return vs_fail(error,"could not create a temporary checkpoint for %s",path);
    FILE *fp=fdopen(fd,"wb");
    if (!fp) { close(fd); unlink(temp); return vs_fail(error,"could not open temporary checkpoint"); }
    Stream s={fp,UINT64_C(14695981039346656037)};
    unsigned char bytes[HEADER];
    header(f,bytes);
    bool ok=block(&s,bytes,HEADER,false) && transfer(&s,NULL,f) && !fflush(fp) && !fsync(fd);
    if (fclose(fp)) ok=false;
    if (ok && !rename(temp,path)) return true;
    unlink(temp);
    return vs_fail(error,"could not commit checkpoint %s",path);
}

bool vs_checkpoint_read(VsFilm *f,const char *path,const VsScene *scene,VsRender cfg,
                        VsGate gate,uint64_t device,VsError *error) {
    FILE *fp=fopen(path,"rb");
    if (!fp) return vs_fail(error,"could not open checkpoint %s",path);
    VsFilm next={.render=cfg,.gate=gate,.scene=scene->identity,.device=device};
    unsigned char actual[HEADER],expected[HEADER];
    header(&next,expected);
    Stream s={fp,UINT64_C(14695981039346656037)};
    bool ok=block(&s,actual,HEADER,true) && !memcmp(actual,expected,HEADER);
    if (!ok) vs_fail(error,"checkpoint scene, gate, device, integrator or sample contract mismatch");
    if (ok) ok=vs_film_init(&next,scene,cfg,gate,device,error);
    if (ok && !transfer(&s,&next,NULL)) ok=vs_fail(error,"invalid checkpoint payload or checksum");
    fclose(fp);
    if (!ok) { vs_film_destroy(&next); return false; }
    vs_film_destroy(f);
    *f=next;
    return true;
}
