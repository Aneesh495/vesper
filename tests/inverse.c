#include "vesper/inverse.h"
#include "check.h"

int main(void) {
    VsObservation row={.gain=1.7,.distance=2.8,.basis={.23,.39,.38},.sigma=.002};
    double p[]={.45,.65,.84};
    for (unsigned kind=0;kind<=VS_PRODUCT;++kind) {
        double d[VS_PARAMETERS];
        double y=vs_forward((VsInverseKind)kind,&row,p,d);
        REQUIRE(y>0 && isfinite(y));
        for (unsigned j=0;j<vs_parameter_count((VsInverseKind)kind);++j) {
            double x=p[j],h=1e-5;
            p[j]=x+h;
            double plus=vs_forward((VsInverseKind)kind,&row,p,NULL);
            p[j]=x-h;
            double minus=vs_forward((VsInverseKind)kind,&row,p,NULL);
            p[j]=x;
            REQUIRE(fabs((plus-minus)/(2*h)-d[j])<1e-8);
        }
    }
    VsCalibration malformed={.kind=VS_EXTINCTION,.count=2};
    VsFit fit;
    VsError error={0};
    REQUIRE(!vs_inverse_fit(&malformed,p,&fit,&error));
    return 0;
}
