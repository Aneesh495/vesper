#include "vesper/transport.h"
#include "check.h"

static void spectral(void) {
    const unsigned n=65536;
    VsVec3 expected=vs_v(0,0,0);
    for (unsigned k=360;k<830;++k)
        expected=vs_add(expected,vs_mul(vs_add(vs_observer(k),vs_observer(k+1)),.5));
    REQUIRE(fabs(expected.y-106.856914916767)<.0001);
    for (unsigned mode=0;mode<2;++mode) {
        double integral=0;
        for (unsigned i=0;i<4700;++i)
            integral+=.1*vs_wavelength_pdf(360+.1*(i+.5),(VsWavelengthMode)mode);
        REQUIRE(fabs(integral-1)<1e-12);
        for (unsigned seed=0;seed<3;++seed) {
            VsVec3 estimate=vs_v(0,0,0);
            double total=0;
            for (unsigned i=0;i<n;++i) {
                VsSample r={72,i,seed+101,VS_STRATIFIED};
                VsWavelength w=vs_wavelength(vs_sample(r,5),(VsWavelengthMode)mode);
                REQUIRE(w.nm>=360 && w.nm<=830 && w.pdf>0);
                estimate=vs_add(estimate,vs_mul(vs_observer(w.nm),1./(n*w.pdf)));
                total+=1./(n*w.pdf);
            }
            REQUIRE(fabs(total-470)<3.5);
            REQUIRE(vs_length(vs_sub(estimate,expected))<2.0);
            printf("spectrum,%u,%u,%u,%.12g,%.12g,%.12g\n",mode,seed,n,estimate.x,estimate.y,estimate.z);
        }
    }
    unsigned bins[16]={0},cells[8][8]={0};
    for (unsigned i=0;i<4096;++i) {
        VsSample r={3,i,127,VS_HALTON};
        unsigned x=(unsigned)(8*vs_sample(r,0)),y=(unsigned)(8*vs_sample(r,1));
        REQUIRE(x<8 && y<8);
        ++cells[x][y];
        r.mode=VS_STRATIFIED;
        ++bins[(unsigned)(16*vs_sample(r,7))];
    }
    for (unsigned i=0;i<16;++i) REQUIRE(bins[i]==256);
    for (unsigned i=0;i<8;++i) for (unsigned j=0;j<8;++j) REQUIRE(cells[i][j]>=59 && cells[i][j]<=69);
}

static void scattering(void) {
    REQUIRE(fabs(vs_fresnel(1,1,1.5)-.04)<1e-14);
    REQUIRE(vs_fresnel(.5,1.5,1)==1);
    REQUIRE(vs_fresnel(.01,1,1)==0);
    REQUIRE(fabs(vs_conductor_fresnel(1,.2,3)-
        ((.2-1)*(.2-1)+9)/((.2+1)*(.2+1)+9))<1e-14);
    VsVec3 n=vs_v(0,0,1),wo=vs_unit(vs_v(.5,0,1));
    const VsMaterial matte={.kind=VS_DIFFUSE,.albedo=vs_flat(.6)};
    VsMaterial metal={.kind=VS_CONDUCTOR,.eta=vs_flat(.21),.k=vs_flat(3.2),.roughness=.65};
    VsMaterial glass={.kind=VS_ROUGH_DIELECTRIC,.roughness=.52};
    const VsMaterial *m[]={&matte,&metal,&glass};
    for (unsigned j=0;j<3;++j) {
        double reflect=0,transmit=0,pdf=0;
        const unsigned N=192;
        for (unsigned y=0;y<2*N;++y) for (unsigned x=0;x<N;++x) {
            double c=(y+.5)/N-1,phi=(x+.5)*2*VS_PI/N;
            VsVec3 wi=vs_v(sqrt(1-c*c)*cos(phi),sqrt(1-c*c)*sin(phi),c);
            double nt=j==2?1.5:1;
            VsBsdfValue b=vs_bsdf_eval(m[j],530,n,wo,wi,1,nt);
            double mass=2*VS_PI/(N*N);
            if (c>0) {
                reflect+=b.f*c*mass;
                VsBsdfValue reverse=vs_bsdf_eval(m[j],530,n,wi,wo,1,nt);
                REQUIRE(fabs(b.f-reverse.f)<1e-10*fmax(1,b.f));
            } else {
                transmit-=b.f*c*mass;
                VsBsdfValue reverse=vs_bsdf_eval(m[j],530,vs_mul(n,-1),wi,wo,nt,1);
                REQUIRE(fabs(b.f*nt*nt-reverse.f)<1e-9*fmax(1,reverse.f));
            }
            pdf+=b.pdf*mass;
        }
        double mc=0,valid=0;
        unsigned n_samples=65536;
        for (unsigned i=0;i<n_samples;++i) {
            VsSample r={j,i,71,VS_HALTON};
            VsScatter b=vs_bsdf_sample(m[j],530,n,wo,1,j==2?1.5:1,
                                       vs_sample(r,0),vs_sample(r,1),vs_sample(r,2));
            REQUIRE(!b.delta);
            if (b.pdf>0) {
                REQUIRE(fabs(vs_length(b.wi)-1)<1e-12);
                mc+=b.weight/n_samples;
                valid+=1./n_samples;
            }
        }
        REQUIRE(fabs(pdf-valid)<.013);
        REQUIRE(fabs(mc-reflect-transmit)<.013);
        REQUIRE(reflect+transmit<=1.001 && reflect>=0 && transmit>=0);
        if (j==0) REQUIRE(fabs(reflect-.6)<1e-12);
        printf("bsdf,%u,%.12g,%.12g,%.12g,%.12g,%.12g\n",j,reflect,transmit,pdf,mc,valid);
    }
    for (int gstep=-3;gstep<=3;++gstep) {
        double g=gstep*.25,integral=0,expect=0;
        for (unsigned i=0;i<32768;++i) {
            double cosine=2*(i+.5)/32768-1;
            integral+=4*VS_PI*vs_phase(cosine,g)/32768;
            expect+=vs_phase_sample(n,g,(i+.5)/32768,.5).z/32768;
        }
        REQUIRE(fabs(integral-1)<2e-6 && fabs(expect-g)<1e-6);
    }
}

static void volume(void) {
    float grid[]={.25,.25,.25,.25,1.25,1.25,1.25,1.25};
    VsMedium m={.name="linear",.absorption=vs_flat(.32),.scattering=vs_flat(.48),
        .density=grid,.bounds={{0,0,0},{1,1,2}},.nx=2,.ny=2,.nz=2,.majorant=1.1};
    VsRay ray={vs_v(.5,.5,-1),vs_v(0,0,1),0,4,0};
    double expected=exp(-.8*1.5);
    VsError error={0};
    for (unsigned seed=0;seed<3;++seed) {
        double tr=0,pass=0;
        unsigned n=65536;
        for (unsigned i=0;i<n;++i) {
            VsSample rng={9,i,305+seed,VS_RANDOM};
            VsCollision c;
            double value;
            uint32_t nulls=0;
            REQUIRE(vs_collision(&m,ray,4,550,rng,64,&c,&error));
            REQUIRE(vs_transmittance(&m,ray,4,550,rng,40000,&value,&nulls,&error));
            tr+=value/n;
            pass+=!c.collision?1./n:0;
        }
        REQUIRE(fabs(tr-expected)<.007 && fabs(pass-expected)<.008);
        printf("volume,%u,%u,%.12g,%.12g,%.12g\n",seed,n,tr,pass,expected);
    }
    VsMedium homogeneous=m;
    homogeneous.density=NULL;
    double tr;
    uint32_t nulls=0;
    REQUIRE(vs_transmittance(&homogeneous,ray,4,550,(VsSample){0},1,&tr,&nulls,&error));
    REQUIRE(fabs(tr-exp(-3.2))<1e-14);
    VsScene scene={0};
    REQUIRE(vs_scene_init(&scene,&error));
    m.majorant=.9;
    REQUIRE(!vs_scene_add_medium(&scene,&m,&error));
    vs_scene_destroy(&scene);
}

int main(int argc,char **argv) {
    const char mode=argc==2?argv[1][0]:'a';
    if (mode=='s' || mode=='a') spectral();
    if (mode=='b' || mode=='a') scattering();
    if (mode=='v' || mode=='a') volume();
    return 0;
}
