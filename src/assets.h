#ifndef VS_ASSETS_H
#define VS_ASSETS_H
#include "vesper/scene.h"

bool vs_asset_path(char out[4096],const char *scene,const char *asset,VsError *error);
bool vs_load_grid(VsScene *s,VsMedium m,const char *path,VsError *error);
bool vs_load_obj(VsScene *s,VsPrimitive p,VsVec3 scale,VsVec3 translation,const char *path,VsError *error);

bool vs_load_gltf(VsScene *s,VsPrimitive p,VsVec3 scale,VsVec3 translation,const char *path,VsError *error);

bool vs_load_texture(VsScene *s,uint32_t material,const char *path,bool srgb,
                     VsVec2 scale,VsVec2 offset,VsError *error);

bool vs_load_normal_map(VsScene *s,uint32_t material,const char *path,
                        VsVec2 scale,VsVec2 offset,double strength,VsError *error);

#endif
