#include "assets.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int token(FILE *file,char word[32]) {
    int c;
    do {
        c=fgetc(file);
        if (c=='#') {
            do { c=fgetc(file); } while (c!=EOF && c!='\n');
        }
    } while (c!=EOF && isspace(c));
    if (c==EOF) return ferror(file)?-1:0;
    size_t size=0;
    do {
        if (size==31 || c=='#' || c<32 || c>126) return -1;
        word[size++]=(char)c;
        c=fgetc(file);
    } while (c!=EOF && !isspace(c));
    word[size]='\0';
    return 1;
}

static bool integer(FILE *file,uint32_t *number) {
    char word[32];
    if (token(file,word)!=1) return false;
    uint32_t result=0;
    for (const char *p=word;*p;++p) {
        if (*p<'0' || *p>'9' || result>(UINT32_MAX-(unsigned)(*p-'0'))/10u) return false;
        result=result*10u+(unsigned)(*p-'0');
    }
    *number=result;
    return true;
}

static double decode(uint32_t component,uint32_t max,bool srgb) {
    double x=component/(double)max;
    if (!srgb) return x;
    return x<=.04045?x/12.92:pow((x+.055)/1.055,2.4);
}

bool vs_load_texture(VsScene *s,uint32_t material,const char *path,bool srgb,
                     VsVec2 scale,VsVec2 offset,VsError *error) {
    FILE *file=fopen(path,"rb");
    if (!file) return vs_fail(error,"could not open texture %s",path);
    char word[32];
    bool ok=token(file,word)==1;
    bool binary=ok && !strcmp(word,"P6");
    VsTexture texture={.srgb=srgb};
    uint32_t max=0;
    ok=ok && (binary || !strcmp(word,"P3")) && integer(file,&texture.width) &&
        integer(file,&texture.height) && integer(file,&max) && max>=1 && max<=255 &&
        texture.width>=1 && texture.height>=1 && texture.width<=VS_MAX_TEXTURE_EDGE &&
        texture.height<=VS_MAX_TEXTURE_EDGE;
    uint64_t count=(uint64_t)texture.width*texture.height;
    ok=ok && count<=VS_TEXTURE_KNOT_LIMIT/VS_SPECTRAL_KNOTS;
    if (ok) {
        texture.knots=malloc((size_t)count*VS_SPECTRAL_KNOTS*sizeof *texture.knots);
        ok=texture.knots!=NULL;
    }
    const VsSpectrum basis[]={vs_model_rgb(vs_v(1,0,0)),vs_model_rgb(vs_v(0,1,0)),vs_model_rgb(vs_v(0,0,1))};
    for (size_t i=0;ok && i<count;++i) {
        double channels[3];
        for (unsigned c=0;c<3;++c) {
            uint32_t value;
            if (binary) {
                int byte=fgetc(file);
                if (byte==EOF) { ok=false; break; }
                value=(uint32_t)byte;
            } else if (!integer(file,&value)) { ok=false; break; }
            if (value>max) { ok=false; break; }
            channels[c]=decode(value,max,srgb);
        }
        if (ok) for (unsigned k=0;k<VS_SPECTRAL_KNOTS;++k) {
            double value=basis[0].values[k]*channels[0]+basis[1].values[k]*channels[1]+basis[2].values[k]*channels[2];
            texture.knots[i*VS_SPECTRAL_KNOTS+k]=(float)fmin(1,value);
        }
    }
    if (ok) ok=(binary?fgetc(file)==EOF:token(file,word)==0) && !ferror(file);
    fclose(file);
    if (ok) ok=vs_scene_set_texture(s,material,&texture,scale,offset,error);
    else vs_fail(error,"invalid, truncated or oversized 8-bit PPM texture %s",path);
    free(texture.knots);
    return ok;
}

static double knot_sample(const VsTexture *texture,int x,int y,unsigned k,double f) {
    int w=(int)texture->width,h=(int)texture->height;
    size_t pixel=(size_t)((y+h)%h)*texture->width+(unsigned)((x+w)%w);
    const float *p=texture->knots+pixel*VS_SPECTRAL_KNOTS+k;
    return (1-f)*p[0]+f*p[1];
}

double vs_texture_factor(const VsMaterial *m,const VsPrimitive *p,const VsHit *hit,double nm) {
    const VsTexture *texture=&m->texture;
    if (!texture->knots) return 1;
    double u=hit->u,v=hit->v;
    if (p->has_uv) {
        double w=1-u-v;
        u=w*p->uv[0].x+u*p->uv[1].x+v*p->uv[2].x;
        v=w*p->uv[0].y+hit->u*p->uv[1].y+v*p->uv[2].y;
    }
    u=u*m->uv_scale.x+m->uv_offset.x;
    v=v*m->uv_scale.y+m->uv_offset.y;
    double x=(u-floor(u))*texture->width-.5,y=(v-floor(v))*texture->height-.5;
    int i=(int)floor(x),j=(int)floor(y);
    double wave=fmin(7,fmax(0,(nm-VS_LAMBDA_MIN)*7/(VS_LAMBDA_MAX-VS_LAMBDA_MIN)));
    unsigned k=(unsigned)fmin(6,wave);
    double f=wave-k,dx=x-i,dy=y-j;
    double a=(1-dx)*knot_sample(texture,i,j,k,f)+dx*knot_sample(texture,i+1,j,k,f);
    double b=(1-dx)*knot_sample(texture,i,j+1,k,f)+dx*knot_sample(texture,i+1,j+1,k,f);
    return (1-dy)*a+dy*b;
}
