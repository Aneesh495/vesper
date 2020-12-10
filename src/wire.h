#ifndef VS_WIRE_H
#define VS_WIRE_H

#include <stdint.h>
#include <string.h>

static inline void vs_store_u(unsigned char *p,uint64_t n,unsigned bytes) {
    for (unsigned i=0;i<bytes;++i) { p[i]=(unsigned char)n; n>>=8; }
}
static inline uint64_t vs_load_u(const unsigned char *p,unsigned bytes) {
    uint64_t n=0;
    while (bytes) n=(n<<8)|p[--bytes];
    return n;
}
static inline void vs_store_f32(unsigned char *p,float x) {
    uint32_t u;
    memcpy(&u,&x,4);
    vs_store_u(p,u,4);
}
static inline float vs_load_f32(const unsigned char *p) {
    uint32_t u=(uint32_t)vs_load_u(p,4);
    float x;
    memcpy(&x,&u,4);
    return x;
}
static inline void vs_store_f64(unsigned char *p,double x) {
    uint64_t u;
    memcpy(&u,&x,8);
    vs_store_u(p,u,8);
}
static inline double vs_load_f64(const unsigned char *p) {
    uint64_t u=vs_load_u(p,8);
    double x;
    memcpy(&x,&u,8);
    return x;
}

#endif
