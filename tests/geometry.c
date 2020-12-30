#include "vesper/geometry.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t state=UINT64_C(0x10b67a2b9cf101d5);

static double uniform(void) {
    state=state*UINT64_C(6364136223846793005)+UINT64_C(1442695040888963407);
    return (state>>11)*0x1p-53;
}

static VsVec3 position(double radius) {
    double x=uniform(),y=uniform(),z=uniform();
    return vs_v((2*x-1)*radius,(2*y-1)*radius,(2*z-1)*radius);
}

/* Independent plane-edge and textbook long-double quadratic checks. */
static bool oracle(const VsPrimitive *p,VsRay ray,double *t) {
    VsVec3 a=vs_add(p->a,vs_mul(p->velocity,ray.time));
    if (p->shape==VS_SPHERE) {
        long double x=(long double)ray.origin.x-a.x, y=(long double)ray.origin.y-a.y;
        long double z=(long double)ray.origin.z-a.z;
        long double dx=ray.direction.x,dy=ray.direction.y,dz=ray.direction.z;
        long double q=dx*dx+dy*dy+dz*dz, b=x*dx+y*dy+z*dz;
        long double c=x*x+y*y+z*z-(long double)p->radius*p->radius;
        long double d=b*b-q*c;
        if (d<0) return false;
        d=sqrtl(d);
        long double lo=(-b-d)/q,hi=(-b+d)/q;
        if (lo<ray.min || lo>ray.max) lo=hi;
        if (lo<ray.min || lo>ray.max) return false;
        *t=(double)lo;
        return true;
    }
    VsVec3 b=vs_add(p->b,vs_mul(p->velocity,ray.time));
    VsVec3 c=vs_add(p->c,vs_mul(p->velocity,ray.time));
    VsVec3 n=vs_cross(vs_sub(b,a),vs_sub(c,a));
    double den=vs_dot(n,ray.direction);
    if (den==0) return false;
    double k=vs_dot(n,vs_sub(a,ray.origin))/den;
    if (!(k>=ray.min && k<=ray.max)) return false;
    VsVec3 pt=vs_add(ray.origin,vs_mul(ray.direction,k));
    if (vs_dot(n,vs_cross(vs_sub(b,a),vs_sub(pt,a))) < 0 ||
        vs_dot(n,vs_cross(vs_sub(c,b),vs_sub(pt,b))) < 0 ||
        vs_dot(n,vs_cross(vs_sub(a,c),vs_sub(pt,c))) < 0) return false;
    *t=k;
    return true;
}

static bool edges(void) {
    VsPrimitive p[2]={
        {.shape=VS_TRIANGLE,.a={0,0,0},.b={1,0,0},.c={0,1,0}},
        {.shape=VS_TRIANGLE,.a={1,1,0},.b={0,1,0},.c={1,0,0}}
    };
    VsBvh tree={0};
    if (!vs_bvh_build(&tree,p,2)) return false;
    bool ok=true;
    for (unsigned i=0;i<=1024;++i) {
        double x=i/1024.;
        VsRay ray={vs_v(x,1-x,1),vs_v(-0.,0,-1),1,1,0};
        VsHit h;
        if (!vs_bvh_hit(&tree,p,ray,&h) || h.t!=1) {
            fprintf(stderr,"shared edge sample %u failed\n",i);
            ok=false;
            break;
        }
    }
    vs_bvh_destroy(&tree);
    VsPrimitive sphere={.shape=VS_SPHERE,.a={1e6,0,0},.radius=.01};
    VsHit hit;
    VsRay ray={vs_v(0,.01,0),vs_v(1,0,0),0,1e7,0};
    bool tangent=vs_intersect(&sphere,ray,&hit);
    if (!tangent || hit.t!=1e6)
        fprintf(stderr,"far tangent hit=%d distance=%.17g\n",tangent,tangent?hit.t:-1);
    return ok && tangent && hit.t==1e6;
}

int main(int argc,char **argv) {
    unsigned n=argc==2?(unsigned)strtoul(argv[1],NULL,10):20000;
    if (!n || n>10000000 || !edges()) { fputs("adversarial geometry check failed\n",stderr); return 1; }
    enum { COUNT=32 };
    VsPrimitive shapes[COUNT];
    for (unsigned i=0;i<COUNT;++i) {
        VsVec3 a=position(5), b=position(1), c=position(1), motion=position(.3);
        shapes[i]=(VsPrimitive){.shape=i%2?VS_SPHERE:VS_TRIANGLE,
            .a=a,.b=vs_add(a,b),.c=vs_add(a,c),.velocity=motion,.radius=.2+uniform()};
    }
    VsBvh bvh={0};
    if (!vs_bvh_build(&bvh,shapes,COUNT) || !vs_bvh_validate(&bvh,shapes)) return 1;
    unsigned hits=0;
    double max_error=0;
    for (unsigned i=0;i<n;++i) {
        VsVec3 o=position(10),d=position(1);
        if (i%3) d=vs_sub(shapes[i%COUNT].a,o);
        VsRay ray={o,vs_unit(d),i%5==0?.1:0,i%7==0?3:40,uniform()};
        bool expect=false;
        uint32_t id=UINT32_MAX;
        VsRay bounded=ray;
        for (uint32_t j=0;j<COUNT;++j) {
            double distance;
            if (!oracle(shapes+j,bounded,&distance)) continue;
            bounded.max=distance; id=j; expect=true;
        }
        VsHit h;
        bool actual=vs_bvh_hit(&bvh,shapes,ray,&h);
        if (actual!=expect || (actual && (h.primitive!=id ||
            fabs(h.t-bounded.max)>2e-10*fmax(1,bounded.max)))) {
            fprintf(stderr,"ray %u actual=%d expected=%d id=%u expected_id=%u\n",i,actual,expect,
                    actual?h.primitive:UINT32_MAX,id);
            vs_bvh_destroy(&bvh);
            return 1;
        }
        if (actual) { ++hits; max_error=fmax(max_error,fabs(h.t-bounded.max)); }
    }
    printf("{\"rays\":%u,\"hits\":%u,\"edge_rays\":1025,\"max_distance_error_m\":%.17g,\"seed\":\"10b67a2b9cf101d5\"}\n",
           n,hits,max_error);
    vs_bvh_destroy(&bvh);
    return 0;
}
