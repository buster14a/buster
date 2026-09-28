#include <stdint.h>
#include <stdio.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
int main(void){
    uint64_t x=1, n=200000000; double best_add=1e30,best_imul=1e30,best_step=1e30;
    for(int r=0;r<5;r++){
        uint64_t a=x; double t0=now();
        for(uint64_t i=0;i<n;i++){ __asm__ volatile("add %1, %0\n\tadd %1, %0\n\tadd %1, %0\n\tadd %1, %0" : "+r"(a) : "r"(x)); }
        double t=(now()-t0)/(4.0*n); if(t<best_add)best_add=t; x+=a;
        uint64_t b=x|1, m=10; t0=now();
        for(uint64_t i=0;i<n;i++){ __asm__ volatile("imul %1, %0\n\timul %1, %0\n\timul %1, %0\n\timul %1, %0" : "+r"(b) : "r"(m)); }
        t=(now()-t0)/(4.0*n); if(t<best_imul)best_imul=t; x+=b;
        uint64_t c=x, d=7; t0=now();
        for(uint64_t i=0;i<n;i++){ __asm__ volatile("imul %1, %0\n\tadd %2, %0\n\timul %1, %0\n\tadd %2, %0" : "+r"(c) : "r"(m), "r"(d)); }
        t=(now()-t0)/(2.0*n); if(t<best_step)best_step=t; x+=c;
    }
    printf("ns per dependent add %.4f, imul %.4f, horner step (imul+add) %.4f\n",best_add,best_imul,best_step);
    printf("imul/add latency ratio %.2f, horner step/add %.2f\n",best_imul/best_add,best_step/best_add);
    return (int)(x&1);
}
