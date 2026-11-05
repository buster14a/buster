typedef char V16C __attribute__((vector_size(16)));
typedef long long V2I __attribute__((vector_size(16)));
typedef short V8H __attribute__((vector_size(16)));
typedef float V4F __attribute__((vector_size(16)));
unsigned rd(void) { return __builtin_ia32_readeflags_u32(); }
void wr(unsigned x) { __builtin_ia32_writeeflags_u32(x); }
int pcmp(V16C a,V16C b,int x) { return __builtin_ia32_pcmpistri128(a,b,x); }
int pcmp_low(V16C a,V16C b) { return __builtin_ia32_pcmpistri128(a,b,-1); }
int pcmp_high(V16C a,V16C b) { return __builtin_ia32_pcmpistri128(a,b,256); }
V2I extract1(V2I a,int n) { return __builtin_ia32_extrqi(a,n,3); }
V2I extract2(V2I a,int n) { return __builtin_ia32_extrqi(a,3,n); }
V2I extract3(V2I a) { return __builtin_ia32_extrqi(a,127,255); }
V8H narrow_hi(V4F a,V8H w) { return __builtin_ia32_vcvtps2ph_mask(a,256,w,15); }
V8H narrow_lo(V4F a,V8H w) { return __builtin_ia32_vcvtps2ph_mask(a,-1,w,15); }
