#include "vesper/inverse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *names[]={"reflectance","emitter","extinction","product"};

static bool calibration(const char *path,VsCalibration *c,VsError *error) {
    FILE *fp=fopen(path,"r");
    if (!fp) return vs_fail(error,"cannot open calibration %s",path);
    char line[512],kind[48],other;
    bool ok=fgets(line,sizeof line,fp) && sscanf(line,"vesper-inverse 1 %47s %c",kind,&other)==1;
    c->kind=(VsInverseKind)4;
    if (ok) for (unsigned i=0;i<4;++i)
        if (!strcmp(kind,names[i])) c->kind=(VsInverseKind)i;
    ok=ok && c->kind<4;
    while (ok && fgets(line,sizeof line,fp)) {
        if (c->count==VS_MEASUREMENTS) { ok=false; break; }
        VsObservation *r=c->rows+c->count++;
        ok=sscanf(line,"%lf,%lf,%lf,%lf,%lf,%lf,%lf %c",&r->gain,&r->distance,
                  r->basis,r->basis+1,r->basis+2,&r->value,&r->sigma,&other)==7 &&
            (strchr(line,'\n') || feof(fp));
    }
    if (ferror(fp)) ok=false;
    fclose(fp);
    return ok || vs_fail(error,"invalid versioned calibration file %s",path);
}

static void report(const VsCalibration *c,const VsFit *f) {
    static const char *status[]={"identified","unidentifiable","model_mismatch","not_converged"};
    printf("{\"model\":\"%s\",\"status\":\"%s\",\"rank\":%u,\"chi2\":%.17g,\"iterations\":%u,\"parameters\":[",
           names[c->kind],status[f->status],f->rank,f->chi2,f->iterations);
    unsigned n=vs_parameter_count(c->kind);
    for (unsigned i=0;i<n;++i) printf("%s%.17g",i?",":"",f->parameter[i]);
    printf("],\"standard_error\":[");
    for (unsigned i=0;i<n;++i) {
        if (i) putchar(',');
        if (isfinite(f->uncertainty[i])) printf("%.17g",f->uncertainty[i]); else fputs("null",stdout);
    }
    puts("]}");
}

int main(int argc,char **argv) {
    if (argc!=2 && argc!=5) {
        fputs("usage: vesper-inverse CALIBRATION [INITIAL_X INITIAL_Y INITIAL_Z]\n",stderr);
        return 2;
    }
    double initial[3]={.4,.45,.5};
    if (argc==5) for (unsigned i=0;i<3;++i) {
        char *end;
        initial[i]=strtod(argv[i+2],&end);
        if (end==argv[i+2] || *end || !isfinite(initial[i])) return 2;
    }
    VsCalibration cal={0};
    VsFit result;
    VsError error={0};
    if (!calibration(argv[1],&cal,&error) || !vs_inverse_fit(&cal,initial,&result,&error)) {
        fprintf(stderr,"vesper-inverse: %s\n",error.message);
        return 1;
    }
    report(&cal,&result);
    return ferror(stdout)?1:0;
}
