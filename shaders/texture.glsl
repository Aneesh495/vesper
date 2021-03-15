float texture_knot(uvec4 info,ivec2 position,uint knot,float fraction) {
    ivec2 size=ivec2(info.xy);
    ivec2 wrapped=(position+size)%size;
    uint first=info.z+8u*uint(wrapped.y*size.x+wrapped.x)+knot;
    return mix(texels[first],texels[first+1u],fraction);
}

float texture_factor(Material m,Primitive primitive,Hit hit,float nm) {
    vec2 uv;
    if (primitive.info.x==0u) {
        vec3 normal=hit.normal_t.xyz;
        float azimuth=(normal.x==0. && normal.z==0.)?0.:atan(normal.z,normal.x);
        uv=vec2((azimuth+PI)/(2.*PI),acos(clamp(normal.y,-1.,1.))/PI);
    } else {
        uint base=6u*hit.id.x;
        vec2 bary=uintBitsToFloat(hit.id.zw);
        uv=(1.-bary.x-bary.y)*vec2(texels[base],texels[base+1u])+
            bary.x*vec2(texels[base+2u],texels[base+3u])+bary.y*vec2(texels[base+4u],texels[base+5u]);
    }
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
