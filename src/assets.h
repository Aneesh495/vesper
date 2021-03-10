#ifndef VS_ASSETS_H
#define VS_ASSETS_H
#include "vesper/scene.h"

bool vs_asset_path(char out[4096],const char *scene,const char *asset,VsError *error);
bool vs_load_grid(VsScene *s,VsMedium m,const char *path,VsError *error);
bool vs_load_obj(VsScene *s,VsPrimitive p,VsVec3 scale,VsVec3 translation,const char *path,VsError *error);

bool vs_load_gltf(VsScene *s,VsPrimitive p,VsVec3 scale,VsVec3 translation,const char *path,VsError *error);

#endif
