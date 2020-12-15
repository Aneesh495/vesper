uint hash32(uint n) {
    n^=n>>16; n*=0x7feb352du; n^=n>>15; n*=0x846ca68bu;
    return n^(n>>16);
}

float unit_hash(uint x) { return (float(x>>9)+.5)*(1./8388608.); }

float sample_at(uvec2 k,uint dim) {
    uint key=hash32(pc.seed^hash32(k.x)^hash32(dim+1u));
    float u=unit_hash(hash32(key^hash32(k.y)));
    uint mode=(pc.flags>>2)&3u;
    if (mode==1) {
        uint b=hash32(key^(k.y>>4));
        uint stratum=((k.y&15u)*(b|1u)+(b>>16))&15u;
        return (float(stratum)+u)/16.;
    }
    if (mode==2 && dim<16) {
        const uint bases[16]=uint[16](2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53);
        uint base=bases[dim],index=k.y;
        float step=1./float(base),r=0;
        while (index>0) {
            r+=float(index%base)*step;
            index/=base; step/=float(base);
        }
        return fract(r+unit_hash(key));
    }
    return u;
}

vec2 wavelength(float u) {
    if ((pc.flags&16u)==0) return vec2(360.+470.*u,1./470.);
    float nm=360.+940.*u;
    if (u>=.5) {
        float v=2.*u-1.;
        nm=v<.5?450.+100.*sqrt(2.*v):650.-100.*sqrt(2.*(1.-v));
    }
    return vec2(nm,.5*(1./470.+max(0.,1.-abs(nm-550.)/100.)/100.));
}

float spectrum(vec4 s[2],uint kind,float nm) {
    if (kind==1) {
        float z=(nm-s[0].y)/s[0].z;
        return s[0].x*exp(-.5*z*z);
    }
    float x=clamp((nm-360.)*7./470.,0.,7.);
    uint i=min(uint(x),6u),j=i+1u;
    return mix(s[i/4][i%4],s[j/4][j%4],x-float(i));
}

vec3 direction_at(vec3 axis,float c,float phi) {
    vec3 basis=abs(axis.z)<.9?vec3(0,0,1):vec3(0,1,0);
    vec3 x=normalize(cross(basis,axis)),y=cross(axis,x);
    return c*axis+sqrt(max(0.,1.-c*c))*(x*cos(phi)+y*sin(phi));
}

float fresnel(float c,float ni,float nt) {
    if (ni==nt) return 0;
    c=clamp(c,0.,1.);
    float r=ni/nt,s=r*r*(1.-c*c);
    if (s>=1.) return 1.;
    float t=sqrt(1.-s);
    vec2 a=vec2(ni,nt)*c,b=vec2(nt,ni)*t, f=(a-b)/(a+b);
    return .5*dot(f,f);
}

float metal_fresnel(float cosine,float eta,float k) {
    if (eta==1 && k==0) return 0;
    float c=clamp(cosine,0.,1.), c2=c*c,s2=1.-c2;
    float a=eta*eta-k*k-s2,b=length(vec2(a,2.*eta*k));
    float x=sqrt(.5*(b+a));
    float rs=(b+c2-2.*x*c)/(b+c2+2.*x*c);
    float rp=rs*(c2*b+s2*s2-2.*x*c*s2)/(c2*b+s2*s2+2.*x*c*s2);
    return clamp(.5*(rs+rp),0.,1.);
}

float masking(float c,float alpha) {
    return 2.*c/(c+sqrt(c*c+alpha*alpha*(1.-c*c)));
}

vec2 bsdf(Material m,float nm,vec3 n,vec3 wo,vec3 wi,float ni,float nt) {
    float co=dot(wo,n),ci=dot(wi,n);
    if (!(co>0.) || ci==0.) return vec2(0);
    if (m.info.x==0u)
        return ci>0.?vec2(spectrum(m.albedo,m.info.y,nm),ci)/PI:vec2(0);
    bool metal=m.info.x==1u,transmit=ci<0.;
    if ((!metal && m.info.x!=4u) || (metal && transmit) || (!metal && ni==nt)) return vec2(0);
    float eta=nt/ni;
    vec3 raw=wo+(transmit?eta:1.)*wi;
    if (dot(raw,raw)==0.) return vec2(0);
    vec3 h=normalize(raw);
    if (dot(h,n)<0.) h=-h;
    float ch=clamp(dot(h,n),0.,1.),oh=dot(wo,h),ih=dot(wi,h);
    if (!(ch>0. && oh>0.) || (transmit?ih>=0.:ih<=0.)) return vec2(0);
    float alpha=m.optics.x*m.optics.x,aa=alpha*alpha;
    float z=1.-ch*ch+aa*ch*ch,D=aa/(PI*z*z),G=masking(co,alpha)*masking(abs(ci),alpha);
    float F=metal?metal_fresnel(oh,spectrum(m.eta,0,nm)/ni,spectrum(m.k,0,nm)/ni):fresnel(oh,ni,nt);
    if (!transmit) return vec2(F*D*G/(4.*co*ci),D*ch/(4.*oh)*(metal?1.:F));
    float k=oh+eta*ih;
    if (k==0.) return vec2(0);
    float J=abs(ih)/(k*k);
    return (1.-F)*D*J*vec2(G*oh/(co*abs(ci)),ch*eta*eta);
}

struct Scatter { vec3 wi; float pdf,weight; bool delta,transmit; };

Scatter scatter_at(Material m,float nm,vec3 n,vec3 wo,float ni,float nt,vec3 u) {
    Scatter s=Scatter(vec3(0),0.,0.,false,false);
    if (m.info.x==2u || (m.info.x==4u && ni==nt)) {
        float f=fresnel(dot(wo,n),ni,nt);
        if (u.z<f) return Scatter(reflect(-wo,n),f,1.,true,false);
        float ratio=ni/nt;
        return Scatter(refract(-wo,n,ratio),1.-f,ratio*ratio,true,true);
    }
    if (m.info.x==0u) s.wi=direction_at(n,sqrt(u.x),2.*PI*u.y);
    else {
        float alpha=m.optics.x*m.optics.x;
        float ch=sqrt((1.-u.x)/(1.-u.x+alpha*alpha*u.x));
        vec3 h=direction_at(n,ch,2.*PI*u.y);
        float oh=dot(wo,h);
        if (oh<=0.) return s;
        s.transmit=m.info.x==4u && u.z>=fresnel(oh,ni,nt);
        s.wi=s.transmit?refract(-wo,h,ni/nt):reflect(-wo,h);
        if ((dot(n,s.wi)<0.)!=s.transmit) return Scatter(vec3(0),0.,0.,false,false);
    }
    vec2 b=bsdf(m,nm,n,wo,s.wi,ni,nt);
    s.pdf=b.y;
    if (s.pdf>0.) s.weight=b.x*abs(dot(n,s.wi))/s.pdf;
    return s;
}
