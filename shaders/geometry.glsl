bool box_hit(Node box,Ray ray,out float enter) {
    enter=ray.tmin;
    float exit=ray.tmax;
    for (int axis=0;axis<3;++axis) {
        if (ray.d[axis]==0) {
            if (ray.o[axis]<box.lo[axis] || ray.o[axis]>box.hi[axis]) return false;
            continue;
        }
        float a=(box.lo[axis]-ray.o[axis])/ray.d[axis];
        float b=(box.hi[axis]-ray.o[axis])/ray.d[axis];
        enter=max(enter,min(a,b));
        exit=min(exit,max(a,b));
        if (enter>exit) return false;
    }
    return true;
}

bool primitive_hit(Primitive p,Ray ray,out vec4 result) {
    vec3 local=ray.o-p.velocity.xyz*ray.time;
    float t;
    vec3 normal;
    if (p.info.x==0u) {
        vec3 q=local-p.a.xyz;
        float b=dot(q,ray.d);
        vec3 nearest=q-b*ray.d;
        float h=p.a.w*p.a.w-dot(nearest,nearest);
        if (h<0) return false;
        float r=sqrt(h);
        float d=-b-(b>=0?r:-r);
        float other=d==0?d:(dot(q,q)-p.a.w*p.a.w)/d;
        t=min(d,other);
        if (t<ray.tmin || t>ray.tmax) t=max(d,other);
        normal=normalize(q+t*ray.d);
    } else {
        vec3 d=abs(ray.d);
        int k=d.x>d.y?(d.x>d.z?0:2):(d.y>d.z?1:2);
        int i=(k+1)%3,j=(k+2)%3;
        float di=ray.d[i]/ray.d[k], dj=ray.d[j]/ray.d[k];
        vec3 a=p.a.xyz-local,b=p.b.xyz-local,c=p.c.xyz-local;
        precise vec2 va=vec2(a[i]-di*a[k],a[j]-dj*a[k]);
        precise vec2 vb=vec2(b[i]-di*b[k],b[j]-dj*b[k]);
        precise vec2 vc=vec2(c[i]-di*c[k],c[j]-dj*c[k]);
        precise vec3 e=vec3(vb.x*vc.y-vb.y*vc.x,vc.x*va.y-vc.y*va.x,va.x*vb.y-va.y*vb.x);
        if (any(lessThan(e,vec3(0))) && any(greaterThan(e,vec3(0)))) return false;
        float det=e.x+e.y+e.z;
        if (det==0) return false;
        t=dot(e,vec3(a[k],b[k],c[k]))/(ray.d[k]*det);
        normal=normalize(cross(p.b.xyz-p.a.xyz,p.c.xyz-p.a.xyz));
    }
    if (!(t>=ray.tmin && t<=ray.tmax)) return false;
    result=vec4(normal,t);
    return true;
}

Hit traverse(Ray ray) {
    Hit hit=Hit(vec4(0,0,0,ray.tmax),uvec4(NONE,0,0,0));
    float enter;
    if (scene.counts.x==0 || !box_hit(nodes[0],ray,enter)) return hit;
    uint stack[64];
    float depth[64];
    uint size=1;
    stack[0]=0;
    depth[0]=enter;
    while (size>0) {
        --size;
        if (depth[size]>ray.tmax) continue;
        Node node=nodes[stack[size]];
        if (node.link.y>0) {
            for (uint i=0;i<node.link.y;++i) {
                uint id=node.link.x+i;
                vec4 n;
                if (!primitive_hit(primitives[id],ray,n)) continue;
                if (hit.id.x==NONE || n.w<ray.tmax ||
                    primitives[id].info.w<primitives[hit.id.x].info.w) {
                    ray.tmax=n.w;
                    hit=Hit(n,uvec4(id,0,0,0));
                }
            }
        } else {
            float a,b;
            bool left=box_hit(nodes[node.link.x],ray,a);
            bool right=box_hit(nodes[node.link.z],ray,b);
            if (size+uint(left)+uint(right)>64) { fail(); return hit; }
            if (left && right && a<b) {
                stack[size]=node.link.z; depth[size++]=b;
                stack[size]=node.link.x; depth[size++]=a;
            } else {
                if (left) { stack[size]=node.link.x; depth[size++]=a; }
                if (right) { stack[size]=node.link.z; depth[size++]=b; }
            }
        }
    }
    return hit;
}

vec3 offset_ray(vec3 p,vec3 n,vec3 d) {
    if (dot(n,d)<0) n=-n;
    return p+n*(8e-7*max(1e-4,max(abs(p.x),max(abs(p.y),abs(p.z)))));
}
