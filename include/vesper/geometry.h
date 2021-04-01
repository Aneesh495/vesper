#ifndef VESPER_GEOMETRY_H
#define VESPER_GEOMETRY_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VS_PI 3.14159265358979323846264338327950288
#define VS_MAX_PRIMITIVES 2000000u
#define VS_BVH_STACK 64u

typedef struct { double x, y; } VsVec2;
typedef struct { double x, y, z; } VsVec3;

static inline VsVec3 vs_v(double x, double y, double z) { return (VsVec3){x,y,z}; }
static inline VsVec3 vs_add(VsVec3 a, VsVec3 b) { return vs_v(a.x+b.x,a.y+b.y,a.z+b.z); }
static inline VsVec3 vs_sub(VsVec3 a, VsVec3 b) { return vs_v(a.x-b.x,a.y-b.y,a.z-b.z); }
static inline VsVec3 vs_mul(VsVec3 a, double b) { return vs_v(a.x*b,a.y*b,a.z*b); }
static inline double vs_dot(VsVec3 a, VsVec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline VsVec3 vs_cross(VsVec3 a, VsVec3 b) {
    return vs_v(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);
}
static inline double vs_length(VsVec3 a) { return sqrt(vs_dot(a,a)); }
static inline VsVec3 vs_unit(VsVec3 a) { return vs_mul(a,1.0/vs_length(a)); }
static inline double vs_at(VsVec3 a, unsigned axis) { return axis==0?a.x:axis==1?a.y:a.z; }
static inline bool vs_finite(VsVec3 a) { return isfinite(a.x)&&isfinite(a.y)&&isfinite(a.z); }

typedef struct { VsVec3 origin, direction; double min, max, time; } VsRay;
typedef struct { VsVec3 lo, hi; } VsBounds;
typedef enum { VS_SPHERE, VS_TRIANGLE } VsShape;
typedef struct {
    VsVec3 a,b,c, velocity;
    double radius;
    uint32_t material, boundary;
    VsShape shape;
    bool has_uv,has_normals;
    VsVec2 uv[3];
    VsVec3 normals[3];
} VsPrimitive;

typedef struct {
    double t,u,v;
    VsVec3 point,normal;
    uint32_t primitive;
    bool front;
} VsHit;

typedef struct {
    VsBounds bounds;
    uint32_t first, count, right;
} VsNode;

typedef struct {
    VsNode *nodes;
    uint32_t *order;
    uint32_t count, node_count, depth;
} VsBvh;

VsBounds vs_bounds_empty(void);
VsBounds vs_bounds_union(VsBounds a, VsBounds b);
VsBounds vs_primitive_bounds(const VsPrimitive *p);
bool vs_bounds_hit(VsBounds bounds, VsRay ray, double *entry);
bool vs_primitive_valid(const VsPrimitive *p);
bool vs_intersect(const VsPrimitive *p, VsRay ray, VsHit *hit);
VsVec3 vs_offset(const VsPrimitive *primitive,VsVec3 p,VsVec3 normal,VsVec3 outgoing);
bool vs_bvh_build(VsBvh *bvh, const VsPrimitive *p, uint32_t count);
bool vs_bvh_validate(const VsBvh *bvh, const VsPrimitive *p);
void vs_bvh_destroy(VsBvh *bvh);
bool vs_bvh_hit(const VsBvh *bvh, const VsPrimitive *p, VsRay ray, VsHit *hit);

#endif
