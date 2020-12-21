float density_at(Medium m,vec3 p) {
    if (m.grid.x==0u) return 1.;
    vec3 q=(p-m.lo.xyz)/(m.hi.xyz-m.lo.xyz);
    if (any(lessThan(q,vec3(0))) || any(greaterThan(q,vec3(1)))) return 0.;
    vec3 t=q*vec3(m.grid.xyz-1u);
    uvec3 i=min(uvec3(t),m.grid.xyz-2u);
    vec3 f=t-vec3(i);
    float result=0.;
    for (uint z=0u;z<2u;++z) for (uint y=0u;y<2u;++y) for (uint x=0u;x<2u;++x) {
        uvec3 a=i+uvec3(x,y,z);
        uint index=m.grid.w+(a.z*m.grid.y+a.y)*m.grid.x+a.x;
        vec3 w=mix(1.-f,f,bvec3(x!=0u,y!=0u,z!=0u));
        result+=w.x*w.y*w.z*density[index];
    }
    return result;
}

float medium_sigma(Medium m,float nm,out float scattering) {
    scattering=spectrum(m.scattering,m.kind.y,nm);
    return scattering+spectrum(m.absorption,m.kind.x,nm);
}

bool interval_at(Medium m,Ray ray,inout float start,inout float end) {
    if (m.grid.x==0u) return end>start;
    for (int a=0;a<3;++a) {
        if (ray.d[a]==0.) {
            if (ray.o[a]<m.lo[a] || ray.o[a]>m.hi[a]) return false;
        } else {
            float x=(m.lo[a]-ray.o[a])/ray.d[a],y=(m.hi[a]-ray.o[a])/ray.d[a];
            start=max(start,min(x,y));
            end=min(end,max(x,y));
        }
    }
    return end>start;
}

float extinction_ratio(Medium m,Ray ray,float t,float sigma) {
    float extinction=sigma*density_at(m,ray.o+t*ray.d);
    if (extinction>m.optics.y) { fault(9u); return 0.; }
    return extinction/m.optics.y;
}

const uint TRACK_LIMIT=16384u;

bool collision_at(Medium m,Ray ray,float end,float nm,uvec2 key,uint dim,out float t,out float albedo) {
    float scattering,sigma=medium_sigma(m,nm,scattering);
    t=0.;
    albedo=0.;
    if (sigma==0. || !interval_at(m,ray,t,end)) return false;
    albedo=scattering/sigma;
    if (m.grid.x==0u) { t=-log(1.-sample_at(key,dim))/sigma; return t<end; }
    for (uint k=0u;k<TRACK_LIMIT;++k) {
        t-=log(1.-sample_at(key,dim+2u*k))/m.optics.y;
        if (t>=end) { atomicAdd(counter[8],k); return false; }
        float r=extinction_ratio(m,ray,t,sigma);
        if (sample_at(key,dim+2u*k+1u)<r) { atomicAdd(counter[8],k); return true; }
    }
    fault(10u);
    return false;
}

float transmittance_at(Medium m,Ray ray,float end,float nm,uvec2 key,uint dim) {
    float scattering,sigma=medium_sigma(m,nm,scattering),t=0.;
    if (sigma==0. || !interval_at(m,ray,t,end)) return 1.;
    if (m.grid.x==0u) return exp(-sigma*end);
    float tr=1.;
    for (uint k=0u;k<TRACK_LIMIT;++k) {
        t-=log(1.-sample_at(key,dim+k))/m.optics.y;
        if (t>=end) { atomicAdd(counter[8],k); return tr; }
        tr*=1.-extinction_ratio(m,ray,t,sigma);
        if (tr==0.) { atomicAdd(counter[8],k+1u); return 0.; }
    }
    fault(11u);
    return 0.;
}

float phase_at(float cosine,float g) {
    float k=1.+g*g-2.*g*clamp(cosine,-1.,1.);
    return (1.-g*g)/(4.*PI*k*sqrt(k));
}

vec3 phase_direction(vec3 forward,float g,vec2 u) {
    float x=2.*u.x-1.;
    if (abs(g)>1e-6) {
        float r=(1.-g*g)/(1.+g*x);
        x=clamp((1.+g*g-r*r)/(2.*g),-1.,1.);
    }
    return direction_at(forward,x,2.*PI*u.y);
}
