#include "assets.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool vs_asset_path(char out[4096],const char *scene,const char *asset,VsError *error) {
    if (!*asset || *asset=='/' || strchr(asset,'\\') || strchr(asset,':'))
        return vs_fail(error,"asset must be a relative path");
    for (const char *p=asset;*p;) {
        size_t n=strcspn(p,"/");
        if (!n || (n==2 && p[0]=='.' && p[1]=='.')) return vs_fail(error,"invalid asset path segment");
        p+=n;
        if (*p) ++p;
    }
    const char *last=strrchr(scene,'/');
    size_t dir=last?(size_t)(last-scene)+1:0, n=strlen(asset);
    if (dir+n>=4096) return vs_fail(error,"asset path length limit");
    memcpy(out,scene,dir);
    memcpy(out+dir,asset,n+1);
    return true;
}

bool vs_load_grid(VsScene *s,VsMedium m,const char *path,VsError *error) {
    size_t count=(size_t)m.nx*m.ny*m.nz;
    FILE *file=fopen(path,"rb");
    if (!file) return vs_fail(error,"could not open density grid %s",path);
    m.density=malloc(count*sizeof *m.density);
    bool ok=m.density!=NULL;
    char word[64];
    for (size_t i=0;ok && i<count;++i) {
        if (fscanf(file,"%63s",word)!=1) { ok=false; break; }
        char *end;
        errno=0;
        float value=strtof(word,&end);
        if (errno || *end || !(value>=0 && value<=1000)) { ok=false; break; }
        m.density[i]=value;
    }
    if (ok && (fscanf(file,"%63s",word)!=EOF || ferror(file))) ok=false;
    fclose(file);
    if (ok) ok=vs_scene_add_medium(s,&m,error);
    else vs_fail(error,"invalid or incomplete %zu-value grid %s",count,path);
    free(m.density);
    return ok;
}

static char *token(char **cursor) {
    const char *whitespace=" \t\r\n";
    char *p=*cursor+strspn(*cursor,whitespace),*end=p+strcspn(p,whitespace);
    *cursor=*end?end+1:end;
    *end='\0';
    return p;
}

static bool vertex_index(char *word,size_t count,size_t *id) {
    if (!*word) return false;
    char *end;
    errno=0;
    long v=strtol(word,&end,10);
    if (errno || *end || !v) return false;
    int64_t n=v>0?v-1:(int64_t)count+v;
    if (n<0 || (uint64_t)n>=count) return false;
    *id=(size_t)n;
    return true;
}

typedef struct { VsVec3 *values; size_t count,capacity; } ObjList;

static bool obj_value(ObjList *list,char **cursor,unsigned dimensions) {
    if (list->count>=VS_MAX_PRIMITIVES) return false;
    if (list->count==list->capacity) {
        size_t cap=list->capacity?list->capacity+list->capacity/2+1:64;
        if (cap>VS_MAX_PRIMITIVES) cap=VS_MAX_PRIMITIVES;
        VsVec3 *next=realloc(list->values,cap*sizeof *next);
        if (!next) return false;
        list->values=next;
        list->capacity=cap;
    }
    double a[3]={0};
    for (unsigned i=0;i<dimensions;++i) {
        char *w=token(cursor),*end;
        errno=0;
        a[i]=strtod(w,&end);
        if (errno || end==w || *end || !isfinite(a[i])) return false;
    }
    if (*token(cursor)) return false;
    list->values[list->count++]=vs_v(a[0],a[1],a[2]);
    return true;
}

typedef struct { size_t vertex,uv,normal; bool has_uv,has_normal; } ObjCorner;

static bool obj_corner(char *word,const ObjList *vertices,const ObjList *uvs,const ObjList *normals,
                       ObjCorner *corner) {
    *corner=(ObjCorner){0};
    char *uv=strchr(word,'/');
    if (uv) *uv++='\0';
    if (!vertex_index(word,vertices->count,&corner->vertex)) return false;
    if (!uv) return true;
    char *normal=strchr(uv,'/');
    if (normal) *normal++='\0';
    if (*uv) {
        if (!vertex_index(uv,uvs->count,&corner->uv)) return false;
        corner->has_uv=true;
    } else if (!normal) return false;
    corner->has_normal=normal!=NULL;
    return !normal || vertex_index(normal,normals->count,&corner->normal);
}

bool vs_load_obj(VsScene *s,VsPrimitive p,VsVec3 scale,VsVec3 translation,const char *path,VsError *error) {
    FILE *file=fopen(path,"rb");
    if (!file) return vs_fail(error,"could not open OBJ %s",path);
    bool ok=true;
    ObjList vertices={0},uvs={0},normals={0};
    uint32_t faces=0,line=0,original_count=s->primitive_count;
    uint64_t original_id=s->identity;
    char buffer[4096];
    while (ok && fgets(buffer,sizeof buffer,file)) {
        ++line;
        if (line>4u*VS_MAX_PRIMITIVES || (!strchr(buffer,'\n') && !feof(file))) { ok=false; break; }
        char *end=strchr(buffer,'#');
        if (end) *end='\0';
        char *cursor=buffer,*command=token(&cursor);
        if (!*command || !strcmp(command,"o") || !strcmp(command,"g") || !strcmp(command,"s")) continue;
        if (!strcmp(command,"v")) ok=obj_value(&vertices,&cursor,3);
        else if (!strcmp(command,"vt")) ok=obj_value(&uvs,&cursor,2);
        else if (!strcmp(command,"vn")) {
            ok=obj_value(&normals,&cursor,3);
            if (ok) {
                double length=vs_length(normals.values[normals.count-1]);
                ok=isfinite(length) && length>0;
            }
        } else if (!strcmp(command,"f")) {
            ObjCorner corner[3];
            for (unsigned i=0;i<3;++i)
                if (!obj_corner(token(&cursor),&vertices,&uvs,&normals,corner+i)) { ok=false; break; }
            if (!ok || *token(&cursor) || corner[0].has_uv!=corner[1].has_uv ||
                corner[0].has_uv!=corner[2].has_uv || corner[0].has_normal!=corner[1].has_normal ||
                corner[0].has_normal!=corner[2].has_normal) { ok=false; break; }
            VsVec3 v[3];
            p.has_uv=corner[0].has_uv;
            p.has_normals=corner[0].has_normal;
            for (unsigned i=0;i<3;++i) {
                VsVec3 point=vertices.values[corner[i].vertex];
                v[i]=vs_add(vs_v(point.x*scale.x,point.y*scale.y,point.z*scale.z),translation);
                if (p.has_uv) {
                    VsVec3 uv=uvs.values[corner[i].uv];
                    p.uv[i]=(VsVec2){uv.x,1-uv.y};
                }
                if (p.has_normals) {
                    VsVec3 normal=normals.values[corner[i].normal];
                    p.normals[i]=vs_unit(vs_v(normal.x/scale.x,normal.y/scale.y,normal.z/scale.z));
                }
            }
            p.a=v[0]; p.b=v[1]; p.c=v[2];
            if (!vs_scene_add_primitive(s,p,error)) { ok=false; break; }
            ++faces;
        } else ok=false;
    }
    if (ferror(file) || !faces) ok=false;
    fclose(file);
    free(vertices.values);
    free(uvs.values);
    free(normals.values);
    if (ok) return true;
    s->primitive_count=original_count;
    s->identity=original_id;
    return vs_fail(error,"invalid, empty or unsupported triangle OBJ %s:%u",path,line);
}
