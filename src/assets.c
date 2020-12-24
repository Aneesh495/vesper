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

bool vs_load_obj(VsScene *s,VsPrimitive p,VsVec3 scale,VsVec3 translation,const char *path,VsError *error) {
    FILE *file=fopen(path,"rb");
    if (!file) return vs_fail(error,"could not open OBJ %s",path);
    bool ok=true;
    VsVec3 *vertices=NULL;
    size_t count=0,cap=0;
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
        if (!strcmp(command,"v")) {
            if (count>=VS_MAX_PRIMITIVES) { ok=false; break; }
            if (count==cap) {
                cap=cap?cap+cap/2+1:64;
                if (cap>VS_MAX_PRIMITIVES) cap=VS_MAX_PRIMITIVES;
                VsVec3 *next=realloc(vertices,cap*sizeof *next);
                if (!next) { ok=false; break; }
                vertices=next;
            }
            double a[3];
            for (unsigned i=0;i<3;++i) {
                char *w=token(&cursor);
                errno=0;
                a[i]=strtod(w,&end);
                if (errno || end==w || *end || !isfinite(a[i])) { ok=false; break; }
            }
            if (!ok || *token(&cursor)) { ok=false; break; }
            vertices[count++]=vs_add(vs_v(a[0]*scale.x,a[1]*scale.y,a[2]*scale.z),translation);
        } else if (!strcmp(command,"f")) {
            size_t ids[3];
            for (unsigned i=0;i<3;++i)
                if (!vertex_index(token(&cursor),count,&ids[i])) { ok=false; break; }
            if (!ok || *token(&cursor)) { ok=false; break; }
            p.a=vertices[ids[0]]; p.b=vertices[ids[1]]; p.c=vertices[ids[2]];
            if (!vs_scene_add_primitive(s,p,error)) { ok=false; break; }
            ++faces;
        } else ok=false;
    }
    if (ferror(file) || !faces) ok=false;
    fclose(file);
    free(vertices);
    if (ok) return true;
    s->primitive_count=original_count;
    s->identity=original_id;
    return vs_fail(error,"invalid, empty or unsupported position-only triangle OBJ %s:%u",path,line);
}
