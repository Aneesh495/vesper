float texture_knot(uvec4 info,ivec2 position,uint knot,float fraction) {
    ivec2 size=ivec2(info.xy);
    ivec2 wrapped=(position+size)%size;
    uint first=info.z+8u*uint(wrapped.y*size.x+wrapped.x)+knot;
    return mix(texels[first],texels[first+1u],fraction);
}

vec2 mesh_uv(uint primitive,uint vertex) {
    uint base=6u*primitive+2u*vertex;
    return vec2(texels[base],texels[base+1u]);
}

vec2 surface_uv(Primitive primitive,Hit hit) {
    if (primitive.info.x==0u) {
        vec3 normal=hit.normal_t.xyz;
        float azimuth=(normal.x==0. && normal.z==0.)?0.:atan(normal.z,normal.x);
        return vec2((azimuth+PI)/(2.*PI),acos(clamp(normal.y,-1.,1.))/PI);
    }
    vec2 bary=uintBitsToFloat(hit.id.zw);
    return (1.-bary.x-bary.y)*mesh_uv(hit.id.x,0u)+
        bary.x*mesh_uv(hit.id.x,1u)+bary.y*mesh_uv(hit.id.x,2u);
}

float texture_factor(Material m,Primitive primitive,Hit hit,float nm) {
    vec2 uv=surface_uv(primitive,hit);
    vec2 st=fract(uv*m.uv_transform.xy+m.uv_transform.zw)*vec2(m.texture_info.xy)-.5;
    ivec2 p=ivec2(floor(st));
    vec2 f=fract(st);
    float wave=clamp((nm-360.)*7./470.,0.,7.);
    uint k=min(6u,uint(wave));
    float a=mix(texture_knot(m.texture_info,p,k,wave-float(k)),
                texture_knot(m.texture_info,p+ivec2(1,0),k,wave-float(k)),f.x);
    float b=mix(texture_knot(m.texture_info,p+ivec2(0,1),k,wave-float(k)),
                texture_knot(m.texture_info,p+ivec2(1,1),k,wave-float(k)),f.x);
    return mix(a,b,f.y);
}


vec3 normal_texel(uvec4 info,ivec2 p) {
    ivec2 size=ivec2(info.xy);
    ivec2 wrapped=(p+size)%size;
    uint first=info.z+3u*uint(wrapped.y*size.x+wrapped.x);
    return vec3(texels[first],texels[first+1u],texels[first+2u]);
}

vec3 shading_normal(Material m,Primitive primitive,Hit hit,vec3 n) {
    if (m.optics.w==0.) return n;
    vec3 tangent,bitangent;
    if (primitive.info.x==0u) {
        vec3 outward=hit.normal_t.xyz;
        tangent=vec3(-outward.z,0.,outward.x);
        if (length(tangent)<=1e-6) return n;
        tangent=normalize(tangent);
        bitangent=cross(outward,tangent);
    } else {
        vec2 uv0=mesh_uv(hit.id.x,0u);
        vec2 d1=mesh_uv(hit.id.x,1u)-uv0,d2=mesh_uv(hit.id.x,2u)-uv0;
        float det=d1.x*d2.y-d2.x*d1.y;
        if (abs(det)<=1e-10) return n;
        vec3 e1=primitive.b.xyz-primitive.a.xyz,e2=primitive.c.xyz-primitive.a.xyz;
        vec3 raw=sign(det)*(e1*d2.y-e2*d1.y);
        if (length(raw)<=1e-20) return n;
        tangent=normalize(raw);
        bitangent=cross(n,tangent);
        if (dot(bitangent,sign(det)*(e2*d1.x-e1*d2.x))<0.) bitangent=-bitangent;
    }
    if (m.normal_transform.x<0.) tangent=-tangent;
    if (m.normal_transform.y<0.) bitangent=-bitangent;
    vec2 st=fract(surface_uv(primitive,hit)*m.normal_transform.xy+m.normal_transform.zw)*
        vec2(m.normal_info.xy)-.5;
    ivec2 p=ivec2(floor(st));
    vec2 f=fract(st);
    vec3 a=mix(normal_texel(m.normal_info,p),normal_texel(m.normal_info,p+ivec2(1,0)),f.x);
    vec3 b=mix(normal_texel(m.normal_info,p+ivec2(0,1)),normal_texel(m.normal_info,p+ivec2(1,1)),f.x);
    vec3 encoded=2.*mix(a,b,f.y)-1.;
    vec2 slope=encoded.xy*(m.optics.w/max(.25,encoded.z));
    float span=length(slope);
    if (span>.5773502691896258) slope*=.5773502691896258/span;
    return normalize(n+tangent*slope.x+bitangent*slope.y);
}

float shading_factor(vec3 geometric,vec3 shading,vec3 direction) {
    float cosine=dot(geometric,direction);
    return cosine>0.?clamp(dot(shading,direction)/cosine,0.,1.):0.;
}
