#ifndef VESPER_SCENE_H
#define VESPER_SCENE_H

#include "vesper/spectral.h"

#define VS_NAME 48u
#define VS_MAX_MATERIALS 1024u
#define VS_MAX_LIGHTS 65536u
#define VS_MAX_MEDIA 64u
#define VS_MAX_CAMERAS 16u
#define VS_MAX_TEXTURE_EDGE 4096u
#define VS_TEXTURE_KNOT_LIMIT (16u*1024u*1024u)
#define VS_WORLD_LIMIT 1000000.0
#define VS_C 299792458.0

typedef enum {
    VS_DIFFUSE, VS_CONDUCTOR, VS_DIELECTRIC, VS_NULL, VS_ROUGH_DIELECTRIC
} VsMaterialKind;

typedef struct { double start,width; } VsPulse;
typedef struct {
    uint32_t width,height;
    float *knots;
    bool srgb;
} VsTexture;
typedef struct {
    uint32_t width,height;
    float *rgb;
    VsVec2 scale,offset;
    double strength;
} VsNormalMap;
typedef struct {
    char name[VS_NAME];
    VsMaterialKind kind;
    VsSpectrum albedo, emission, eta, k;
    double roughness,ior_a,ior_b;
    uint32_t medium;
    VsPulse pulse;
    VsTexture texture;
    VsVec2 uv_scale,uv_offset;
    VsNormalMap normal_map;
    bool smooth_normals;
} VsMaterial;

typedef enum { VS_POINT, VS_DISTANT, VS_AREA, VS_ENVIRONMENT } VsLightKind;
typedef struct {
    VsLightKind kind;
    VsVec3 vector;
    VsSpectrum power;
    VsPulse pulse;
    uint32_t primitive;
    /* A directional environment cap adds flat spectral density to the base. */
    double cone_cos,cone_power;
} VsLight;

typedef struct {
    char name[VS_NAME];
    VsSpectrum absorption,scattering;
    double anisotropy,majorant;
    VsBounds bounds;
    uint32_t nx,ny,nz;
    float *density;
} VsMedium;

typedef struct {
    char name[VS_NAME];
    VsVec3 position,target,up;
    double vfov,aperture,focus,shutter_open,shutter_close;
} VsCamera;

typedef struct {
    VsPrimitive *primitives;
    VsMaterial *materials;
    VsLight *lights;
    VsMedium *media;
    VsCamera cameras[VS_MAX_CAMERAS];
    VsBvh bvh;
    uint32_t *area_indices;
    uint32_t primitive_count,primitive_cap,material_count,material_cap;
    uint32_t light_count,light_cap,explicit_lights,medium_count,medium_cap;
    uint32_t camera_count,camera,world_medium,environment;
    double response;
    uint64_t identity;
} VsScene;

typedef struct { char message[256]; } VsError;

bool vs_fail(VsError *error, const char *format, ...);
bool vs_scene_init(VsScene *s, VsError *error);
void vs_scene_destroy(VsScene *s);
bool vs_scene_add_primitive(VsScene *s, VsPrimitive p, VsError *error);
bool vs_scene_add_material(VsScene *s, VsMaterial m, VsError *error);
bool vs_scene_set_texture(VsScene *s,uint32_t material,const VsTexture *texture,
                          VsVec2 scale,VsVec2 offset,VsError *error);
bool vs_scene_set_normal_map(VsScene *s,uint32_t material,const VsNormalMap *map,VsError *error);
VsVec3 vs_shading_normal(const VsMaterial *m,const VsPrimitive *p,const VsHit *hit,VsVec3 geometric);
double vs_shading_factor(VsVec3 geometric,VsVec3 shading,VsVec3 direction);
double vs_texture_factor(const VsMaterial *m,const VsPrimitive *p,const VsHit *hit,double nm);
bool vs_scene_add_light(VsScene *s, VsLight l, VsError *error);
bool vs_scene_add_medium(VsScene *s, const VsMedium *m, VsError *error);
bool vs_scene_compile(VsScene *s, VsError *error);
bool vs_scene_load(VsScene *s, const char *path, VsError *error);
double vs_phase_index(const VsMaterial *m,double nm);
double vs_group_index(const VsMaterial *m,double nm);
double vs_area(const VsPrimitive *p);
VsRay vs_camera_ray(const VsCamera *camera, uint32_t width, uint32_t height, VsSample sample);

#endif
