#include "vesper/inverse.h"
#include <string.h>

typedef struct {
    double info[VS_PARAMETERS][VS_PARAMETERS],gradient[VS_PARAMETERS],chi2;
} System;

unsigned vs_parameter_count(VsInverseKind kind) {
    return kind==VS_EXTINCTION?1:kind==VS_PRODUCT?2:3;
}

static double upper(VsInverseKind kind) {
    return kind==VS_REFLECTANCE?1:kind==VS_EXTINCTION?6:8;
}

double vs_forward(VsInverseKind kind,const VsObservation *r,const double x[VS_PARAMETERS],
                  double d[VS_PARAMETERS]) {
    double y=0;
    if (d) memset(d,0,VS_PARAMETERS*sizeof *d);
    if (kind==VS_EXTINCTION) {
        y=r->gain*exp(-r->distance*x[0]);
        if (d) d[0]=-r->distance*y;
    } else if (kind==VS_PRODUCT) {
        y=r->gain*x[0]*x[1];
        if (d) { d[0]=r->gain*x[1]; d[1]=r->gain*x[0]; }
    } else {
        for (unsigned i=0;i<VS_PARAMETERS;++i) {
            double a=r->gain*r->basis[i];
            y+=a*x[i];
            if (d) d[i]=a;
        }
    }
    return y;
}

static bool equations(const VsCalibration *c,const double x[VS_PARAMETERS],System *s) {
    *s=(System){0};
    unsigned n=vs_parameter_count(c->kind);
    for (uint32_t i=0;i<c->count;++i) {
        const VsObservation *r=c->rows+i;
        double d[VS_PARAMETERS],pred=vs_forward(c->kind,r,x,d);
        double residual=(pred-r->value)/r->sigma;
        s->chi2+=residual*residual;
        for (unsigned j=0;j<n;++j) {
            double a=d[j]/r->sigma;
            s->gradient[j]+=a*residual;
            for (unsigned k=0;k<n;++k) s->info[j][k]+=a*(d[k]/r->sigma);
        }
    }
    return isfinite(s->chi2);
}

/* Pivoted elimination is sufficient for the small damped systems. Rank uses the
   undamped information matrix and a relative pivot threshold. */
static unsigned solve(const System *s,const double rhs[VS_PARAMETERS],unsigned n,double damping,
                      double tolerance,double x[VS_PARAMETERS],double *condition) {
    double a[VS_PARAMETERS][VS_PARAMETERS+1],max_entry=0,min_pivot=INFINITY,max_pivot=0;
    for (unsigned j=0;j<n;++j) {
        for (unsigned k=0;k<n;++k) {
            a[j][k]=s->info[j][k]+(j==k?damping:0);
            max_entry=fmax(max_entry,fabs(a[j][k]));
        }
        a[j][n]=rhs[j];
    }
    unsigned rank=0;
    for (unsigned col=0;col<n;++col) {
        unsigned row=rank;
        for (unsigned i=rank+1;i<n;++i) if (fabs(a[i][col])>fabs(a[row][col])) row=i;
        double pivot=fabs(a[row][col]);
        if (pivot<=tolerance*max_entry) continue;
        min_pivot=fmin(min_pivot,pivot); max_pivot=fmax(max_pivot,pivot);
        for (unsigned k=0;k<=n;++k) {
            double t=a[rank][k]; a[rank][k]=a[row][k]; a[row][k]=t;
        }
        double divisor=a[rank][col];
        for (unsigned k=col;k<=n;++k) a[rank][k]/=divisor;
        for (unsigned i=0;i<n;++i) {
            if (i==rank) continue;
            double f=a[i][col];
            for (unsigned k=col;k<=n;++k) a[i][k]-=f*a[rank][k];
        }
        ++rank;
    }
    if (condition) *condition=rank==n?max_pivot/min_pivot:INFINITY;
    if (rank==n) for (unsigned i=0;i<n;++i) x[i]=a[i][n];
    return rank;
}

static double projected_norm(const System *s,const double x[VS_PARAMETERS],unsigned n,double limit) {
    double norm=0;
    for (unsigned j=0;j<n;++j) {
        double g=s->gradient[j];
        if ((x[j]==0 && g>0) || (x[j]==limit && g<0)) continue;
        norm=fmax(norm,fabs(g)/sqrt(fmax(1,s->info[j][j])));
    }
    return norm;
}

bool vs_inverse_fit(const VsCalibration *c,const double initial[VS_PARAMETERS],VsFit *fit,VsError *error) {
    if ((unsigned)c->kind>VS_PRODUCT || c->count>VS_MEASUREMENTS ||
        c->count<=vs_parameter_count(c->kind)) return vs_fail(error,"invalid inverse experiment dimensions");
    unsigned n=vs_parameter_count(c->kind);
    for (uint32_t i=0;i<c->count;++i) {
        const VsObservation *r=c->rows+i;
        if (!(r->sigma>=1e-12 && r->sigma<=1e6 && r->gain>=0 && r->gain<=1e6 &&
              r->distance>=0 && r->distance<=1e6 && fabs(r->value)<=1e9))
            return vs_fail(error,"invalid calibration row %u",i);
        for (unsigned j=0;j<VS_PARAMETERS;++j)
            if (!(r->basis[j]>=0 && r->basis[j]<=1)) return vs_fail(error,"invalid basis row %u",i);
    }
    *fit=(VsFit){0};
    double limit=upper(c->kind),*x=fit->parameter;
    for (unsigned i=0;i<n;++i) {
        if (!isfinite(initial[i])) return vs_fail(error,"nonfinite inverse initialization");
        x[i]=fmin(limit,fmax(0,initial[i]));
    }
    System state;
    if (!equations(c,x,&state)) return vs_fail(error,"nonfinite inverse objective");
    bool converged=false;
    for (uint32_t step=0;step<128;++step) {
        fit->iterations=step;
        if (projected_norm(&state,x,n,limit)<1e-7) { converged=true; break; }
        double max_diagonal=1,rhs[VS_PARAMETERS]={0};
        for (unsigned i=0;i<n;++i) {
            rhs[i]=-state.gradient[i];
            max_diagonal=fmax(max_diagonal,state.info[i][i]);
        }
        bool accepted=false;
        for (unsigned trial=0;trial<16 && !accepted;++trial) {
            double direction[VS_PARAMETERS]={0};
            double damping=max_diagonal*1e-9*pow(10,trial/4);
            if (solve(&state,rhs,n,damping,1e-14,direction,NULL)!=n) continue;
            double next[VS_PARAMETERS]={0}, alpha=ldexp(1,-(int)(trial%4));
            for (unsigned i=0;i<n;++i) next[i]=fmin(limit,fmax(0,x[i]+alpha*direction[i]));
            System candidate;
            if (!equations(c,next,&candidate) || candidate.chi2>=state.chi2) continue;
            converged=state.chi2-candidate.chi2<=1e-12*fmax(1,state.chi2);
            state=candidate;
            memcpy(x,next,sizeof next);
            accepted=true;
        }
        if (!accepted || converged) break;
    }
    fit->chi2=state.chi2;
    const double zero[VS_PARAMETERS]={0};
    double ignored[VS_PARAMETERS];
    fit->rank=solve(&state,zero,n,0,1e-9,ignored,&fit->condition);
    for (unsigned i=0;i<n;++i) fit->uncertainty[i]=NAN;
    if (fit->rank<n) fit->status=VS_UNIDENTIFIABLE;
    else if (!converged) fit->status=VS_NOT_CONVERGED;
    else if (fit->chi2/(c->count-n)>8) fit->status=VS_MODEL_MISMATCH;
    else {
        fit->status=VS_IDENTIFIED;
        for (unsigned i=0;i<n;++i) {
            double e[VS_PARAMETERS]={0},col[VS_PARAMETERS];
            e[i]=1;
            if (solve(&state,e,n,0,1e-9,col,NULL)==n) fit->uncertainty[i]=sqrt(fmax(0,col[i]));
        }
    }
    return true;
}
