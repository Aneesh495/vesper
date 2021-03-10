#include "vesper/geometry.h"
#include <float.h>

VsBounds vs_bounds_empty(void) {
    return (VsBounds){vs_v(INFINITY,INFINITY,INFINITY),vs_v(-INFINITY,-INFINITY,-INFINITY)};
}

VsBounds vs_bounds_union(VsBounds a, VsBounds b) {
    return (VsBounds){
        vs_v(fmin(a.lo.x,b.lo.x),fmin(a.lo.y,b.lo.y),fmin(a.lo.z,b.lo.z)),
        vs_v(fmax(a.hi.x,b.hi.x),fmax(a.hi.y,b.hi.y),fmax(a.hi.z,b.hi.z))
    };
}

VsBounds vs_primitive_bounds(const VsPrimitive *p) {
    VsBounds box = {p->a,p->a};
    if (p->shape == VS_SPHERE) {
        VsVec3 radius = vs_v(p->radius,p->radius,p->radius);
        box = (VsBounds){vs_sub(p->a,radius),vs_add(p->a,radius)};
    } else {
        box = vs_bounds_union(box,(VsBounds){p->b,p->b});
        box = vs_bounds_union(box,(VsBounds){p->c,p->c});
    }
    return vs_bounds_union(box,(VsBounds){vs_add(box.lo,p->velocity),vs_add(box.hi,p->velocity)});
}

bool vs_bounds_hit(VsBounds bounds, VsRay ray, double *entry) {
    double lo = ray.min, hi = ray.max;
    for (unsigned axis=0;axis<3;++axis) {
        double o=vs_at(ray.origin,axis), d=vs_at(ray.direction,axis);
        double a=vs_at(bounds.lo,axis), b=vs_at(bounds.hi,axis);
        if (d==0) {
            if (o<a || o>b) return false;
            continue;
        }
        double ta=(a-o)/d, tb=(b-o)/d;
        lo=fmax(lo,nextafter(fmin(ta,tb),-INFINITY));
        hi=fmin(hi,nextafter(fmax(ta,tb),INFINITY));
        if (lo>hi) return false;
    }
    if (entry) *entry=lo;
    return true;
}

bool vs_primitive_valid(const VsPrimitive *p) {
    if (!p || !vs_finite(p->a) || !vs_finite(p->velocity)) return false;
    if (p->shape==VS_SPHERE) {
        if (!isfinite(p->radius) || p->radius<=0) return false;
    } else {
        if (p->shape!=VS_TRIANGLE || !vs_finite(p->b) || !vs_finite(p->c)) return false;
        double area=vs_length(vs_cross(vs_sub(p->b,p->a),vs_sub(p->c,p->a)));
        if (!isfinite(area) || area<=DBL_MIN) return false;
    }
    if (p->has_uv) {
        if (p->shape!=VS_TRIANGLE) return false;
        for (unsigned i=0;i<3;++i)
            if (!(fabs(p->uv[i].x)<=1e6 && fabs(p->uv[i].y)<=1e6)) return false;
    }
    VsBounds bounds=vs_primitive_bounds(p);
    return vs_finite(bounds.lo) && vs_finite(bounds.hi);
}

static bool sphere(const VsPrimitive *p, VsRay ray, VsHit *h) {
    VsVec3 delta=vs_sub(ray.origin,p->a);
    double a=vs_dot(ray.direction,ray.direction);
    double b=vs_dot(ray.direction,delta);
    double c=vs_dot(delta,delta)-p->radius*p->radius;
    VsVec3 closest=vs_sub(delta,vs_mul(ray.direction,b/a));
    double miss=vs_length(closest);
    double height=(p->radius-miss)*(p->radius+miss);
    if (!(height>=0) || a==0) return false;
    double q=-b-copysign(sqrt(a*height),b);
    double t0=q/a, t1=height==0?t0:c/q;
    h->t=fmin(t0,t1);
    if (h->t<ray.min || h->t>ray.max) h->t=fmax(t0,t1);
    if (!(h->t>=ray.min && h->t<=ray.max)) return false;
    h->normal=vs_unit(vs_add(delta,vs_mul(ray.direction,h->t)));
    h->u=(atan2(h->normal.z,h->normal.x)+VS_PI)/(2*VS_PI);
    h->v=acos(fmin(1,fmax(-1,h->normal.y)))/VS_PI;
    return true;
}

/* Shared edges use identical projected endpoints in either triangle. */
static bool triangle(const VsPrimitive *p, VsRay ray, VsHit *h) {
    VsVec3 v[]={vs_sub(p->a,ray.origin),vs_sub(p->b,ray.origin),vs_sub(p->c,ray.origin)};
    unsigned k=0;
    for (unsigned i=1;i<3;++i)
        if (fabs(vs_at(ray.direction,i))>fabs(vs_at(ray.direction,k))) k=i;
    unsigned i=(k+1)%3,j=(k+2)%3;
    double d=vs_at(ray.direction,k);
    if (d==0) return false;
    double sx=vs_at(ray.direction,i)/d, sy=vs_at(ray.direction,j)/d;
    double x[3],y[3],z[3];
    for (unsigned n=0;n<3;++n) {
        z[n]=vs_at(v[n],k);
        x[n]=vs_at(v[n],i)-sx*z[n];
        y[n]=vs_at(v[n],j)-sy*z[n];
    }
    double a=x[1]*y[2]-y[1]*x[2], b=x[2]*y[0]-y[2]*x[0], c=x[0]*y[1]-y[0]*x[1];
    if ((a<0 || b<0 || c<0) && (a>0 || b>0 || c>0)) return false;
    double det=a+b+c;
    if (det==0) return false;
    h->t=(a*z[0]+b*z[1]+c*z[2])/(d*det);
    if (!(h->t>=ray.min && h->t<=ray.max)) return false;
    h->u=b/det;
    h->v=c/det;
    h->normal=vs_unit(vs_cross(vs_sub(p->b,p->a),vs_sub(p->c,p->a)));
    return true;
}

bool vs_intersect(const VsPrimitive *p, VsRay ray, VsHit *hit) {
    VsRay local=ray;
    local.origin=vs_sub(ray.origin,vs_mul(p->velocity,ray.time));
    VsHit h={0};
    if (!(p->shape==VS_SPHERE?sphere(p,local,&h):triangle(p,local,&h))) return false;
    VsVec3 a=vs_add(p->a,vs_mul(p->velocity,ray.time));
    if (p->shape==VS_SPHERE) h.point=vs_add(a,vs_mul(h.normal,p->radius));
    else {
        h.point=vs_add(ray.origin,vs_mul(ray.direction,h.t));
        h.point=vs_add(h.point,vs_mul(h.normal,vs_dot(vs_sub(a,h.point),h.normal)));
    }
    h.front=vs_dot(ray.direction,h.normal)<0;
    *hit=h;
    return true;
}

VsVec3 vs_offset(const VsPrimitive *prim,VsVec3 p,VsVec3 normal,VsVec3 outgoing) {
    if (vs_dot(normal,outgoing)<0) normal=vs_mul(normal,-1);
    double scale=fmax(fabs(p.x),fmax(fabs(p.y),fabs(p.z)));
    double span=prim->shape==VS_SPHERE?prim->radius:
        fmax(vs_length(vs_sub(prim->b,prim->a)),vs_length(vs_sub(prim->c,prim->a)));
    return vs_add(p,vs_mul(normal,64*DBL_EPSILON*fmax(span,scale)));
}
