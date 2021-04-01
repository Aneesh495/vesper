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

static float *load_image(const char *path,uint32_t *width,uint32_t *height,bool spectral,bool srgb,
                         VsError *error) {
    FILE *file=fopen(path,"rb");
    if (!file) {
        vs_fail(error,"could not open texture %s",path);
        return NULL;
    }
    char word[32];
    bool ok=token(file,word)==1;
    bool binary=ok && !strcmp(word,"P6");
    uint32_t max=0;
    ok=ok && (binary || !strcmp(word,"P3")) && integer(file,width) && integer(file,height) &&
        integer(file,&max) && max>=1 && max<=255 && *width>=1 && *height>=1 &&
        *width<=VS_MAX_TEXTURE_EDGE && *height<=VS_MAX_TEXTURE_EDGE;
    uint64_t count=(uint64_t)(*width)*(*height);
    uint32_t stride=spectral?VS_SPECTRAL_KNOTS:3;
    ok=ok && count<=VS_TEXTURE_KNOT_LIMIT/stride;
    float *data=ok?malloc((size_t)count*stride*sizeof *data):NULL;
    ok=data!=NULL;
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
        if (!ok) break;
        if (spectral) {
            for (unsigned k=0;k<VS_SPECTRAL_KNOTS;++k) {
                double value=basis[0].values[k]*channels[0]+basis[1].values[k]*channels[1]+basis[2].values[k]*channels[2];
                data[i*stride+k]=(float)fmin(1,value);
            }
        } else {
            for (unsigned c=0;c<3;++c) data[i*stride+c]=(float)channels[c];
        }
    }
    if (ok) ok=(binary?fgetc(file)==EOF:token(file,word)==0) && !ferror(file);
    fclose(file);
    if (ok) return data;
    free(data);
    vs_fail(error,"invalid, truncated or oversized 8-bit PPM texture %s",path);
    return NULL;
}

bool vs_load_texture(VsScene *s,uint32_t material,const char *path,bool srgb,
                     VsVec2 scale,VsVec2 offset,VsError *error) {
    VsTexture texture={.srgb=srgb};
    texture.knots=load_image(path,&texture.width,&texture.height,true,srgb,error);
    if (!texture.knots) return false;
    bool ok=vs_scene_set_texture(s,material,&texture,scale,offset,error);
    free(texture.knots);
    return ok;
}

bool vs_load_normal_map(VsScene *s,uint32_t material,const char *path,
                        VsVec2 scale,VsVec2 offset,double strength,VsError *error) {
    VsNormalMap map={.scale=scale,.offset=offset,.strength=strength};
    map.rgb=load_image(path,&map.width,&map.height,false,false,error);
    if (!map.rgb) return false;
    bool ok=vs_scene_set_normal_map(s,material,&map,error);
    free(map.rgb);
    return ok;
}

static VsVec2 surface_uv(const VsPrimitive *p,const VsHit *hit) {
    double u=hit->u,v=hit->v;
    if (!p->has_uv) return (VsVec2){u,v};
    double w=1-u-v;
    return (VsVec2){w*p->uv[0].x+u*p->uv[1].x+v*p->uv[2].x,
                    w*p->uv[0].y+u*p->uv[1].y+v*p->uv[2].y};
}

static VsVec2 texel_position(VsVec2 uv,VsVec2 scale,VsVec2 offset,uint32_t w,uint32_t h) {
    double u=uv.x*scale.x+offset.x,v=uv.y*scale.y+offset.y;
    return (VsVec2){(u-floor(u))*w-.5,(v-floor(v))*h-.5};
}

static size_t repeat_index(uint32_t width,uint32_t height,int x,int y) {
    int w=(int)width,h=(int)height;
    return (size_t)((y+h)%h)*width+(unsigned)((x+w)%w);
}

static double knot_sample(const VsTexture *texture,int x,int y,unsigned k,double f) {
    size_t pixel=repeat_index(texture->width,texture->height,x,y);
    const float *p=texture->knots+pixel*VS_SPECTRAL_KNOTS+k;
    return (1-f)*p[0]+f*p[1];
}

double vs_texture_factor(const VsMaterial *m,const VsPrimitive *p,const VsHit *hit,double nm) {
    const VsTexture *texture=&m->texture;
    if (!texture->knots) return 1;
    VsVec2 pos=texel_position(surface_uv(p,hit),m->uv_scale,m->uv_offset,texture->width,texture->height);
    int i=(int)floor(pos.x),j=(int)floor(pos.y);
    double wave=fmin(7,fmax(0,(nm-VS_LAMBDA_MIN)*7/(VS_LAMBDA_MAX-VS_LAMBDA_MIN)));
    unsigned k=(unsigned)fmin(6,wave);
    double f=wave-k,dx=pos.x-i,dy=pos.y-j;
    double a=(1-dx)*knot_sample(texture,i,j,k,f)+dx*knot_sample(texture,i+1,j,k,f);
    double b=(1-dx)*knot_sample(texture,i,j+1,k,f)+dx*knot_sample(texture,i+1,j+1,k,f);
    return (1-dy)*a+dy*b;
}


static VsVec3 normal_texel(const VsNormalMap *map,int x,int y) {
    const float *rgb=map->rgb+3*repeat_index(map->width,map->height,x,y);
    return vs_v(rgb[0],rgb[1],rgb[2]);
}

static VsVec3 lerp(VsVec3 a,VsVec3 b,double t) {
    return vs_add(vs_mul(a,1-t),vs_mul(b,t));
}

static bool tangent_frame(const VsPrimitive *p,const VsHit *hit,VsVec3 n,VsVec3 *t,VsVec3 *b) {
    if (p->shape==VS_SPHERE) {
        *t=vs_v(-hit->normal.z,0,hit->normal.x);
        double norm=vs_length(*t);
        if (norm<=1e-6) return false;
        *t=vs_mul(*t,1/norm);
        *b=vs_cross(hit->normal,*t);
        return true;
    }
    static const VsVec2 canonical[]={{0,0},{1,0},{0,1}};
    const VsVec2 *uv=p->has_uv?p->uv:canonical;
    double u1=uv[1].x-uv[0].x,v1=uv[1].y-uv[0].y,u2=uv[2].x-uv[0].x,v2=uv[2].y-uv[0].y;
    double det=u1*v2-u2*v1;
    if (fabs(det)<=1e-10) return false;
    VsVec3 e1=vs_sub(p->b,p->a),e2=vs_sub(p->c,p->a);
    double sign=det<0?-1:1;
    VsVec3 raw=vs_mul(vs_sub(vs_mul(e1,v2),vs_mul(e2,v1)),sign);
    VsVec3 dv=vs_mul(vs_sub(vs_mul(e2,u1),vs_mul(e1,u2)),sign);
    double norm=vs_length(raw);
    if (!(norm>1e-20)) return false;
    *t=vs_mul(raw,1/norm);
    *b=vs_cross(n,*t);
    if (vs_dot(*b,dv)<0) *b=vs_mul(*b,-1);
    return true;
}

static VsVec3 map_offset(const VsNormalMap *map,const VsPrimitive *p,const VsHit *hit,VsVec3 n) {
    VsVec3 tangent,bitangent;
    if (!map->rgb || map->strength==0 || !tangent_frame(p,hit,n,&tangent,&bitangent)) return vs_v(0,0,0);
    if (map->scale.x<0) tangent=vs_mul(tangent,-1);
    if (map->scale.y<0) bitangent=vs_mul(bitangent,-1);
    VsVec2 pos=texel_position(surface_uv(p,hit),map->scale,map->offset,map->width,map->height);
    int i=(int)floor(pos.x),j=(int)floor(pos.y);
    double dx=pos.x-i,dy=pos.y-j;
    VsVec3 a=lerp(normal_texel(map,i,j),normal_texel(map,i+1,j),dx);
    VsVec3 b=lerp(normal_texel(map,i,j+1),normal_texel(map,i+1,j+1),dx);
    VsVec3 encoded=lerp(a,b,dy);
    double slope=map->strength/fmax(.25,2*encoded.z-1);
    return vs_add(vs_mul(tangent,(2*encoded.x-1)*slope),vs_mul(bitangent,(2*encoded.y-1)*slope));
}

VsVec3 vs_shading_normal(const VsMaterial *m,const VsPrimitive *p,const VsHit *hit,VsVec3 n) {
    VsVec3 offset=map_offset(&m->normal_map,p,hit,n);
    if (m->smooth_normals && p->has_normals) {
        double u=hit->u,v=hit->v;
        VsVec3 imported=vs_add(vs_mul(p->normals[0],1-u-v),
            vs_add(vs_mul(p->normals[1],u),vs_mul(p->normals[2],v)));
        if (!hit->front) imported=vs_mul(imported,-1);
        double z=vs_dot(imported,n);
        if (z>1e-6) offset=vs_add(offset,vs_sub(vs_mul(imported,1/z),n));
    }
    double length=vs_length(offset),bound=.5773502691896258;
    if (length==0) return n;
    if (length>bound) offset=vs_mul(offset,bound/length);
    return vs_unit(vs_add(n,offset));
}

double vs_shading_factor(VsVec3 geometric,VsVec3 shading,VsVec3 direction) {
    double cosine=vs_dot(geometric,direction);
    return cosine>0?fmin(1,fmax(0,vs_dot(shading,direction)/cosine)):0;
}
