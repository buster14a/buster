typedef char V16C __attribute__((vector_size(16)));
typedef long long V2I __attribute__((vector_size(16)));
typedef short V8H __attribute__((vector_size(16)));
typedef short V16H __attribute__((vector_size(32)));
typedef float V4F __attribute__((vector_size(16)));
typedef float V8F __attribute__((vector_size(32)));
typedef float V16F __attribute__((vector_size(64)));
int explicit_index(V16C a, int la, V16C b, int lb) { return __builtin_ia32_pcmpestri128(a,la,b,lb,255); }
int explicit_above(V16C a, int la, V16C b, int lb) { return __builtin_ia32_pcmpestria128(a,la,b,lb,3); }
int explicit_carry(V16C a, int la, V16C b, int lb) { return __builtin_ia32_pcmpestric128(a,la,b,lb,3); }
int explicit_overflow(V16C a, int la, V16C b, int lb) { return __builtin_ia32_pcmpestrio128(a,la,b,lb,3); }
int explicit_sign(V16C a, int la, V16C b, int lb) { return __builtin_ia32_pcmpestris128(a,la,b,lb,3); }
int explicit_zero(V16C a, int la, V16C b, int lb) { return __builtin_ia32_pcmpestriz128(a,la,b,lb,3); }
V16C explicit_mask(V16C a, int la, V16C b, int lb) { return __builtin_ia32_pcmpestrm128(a,la,b,lb,64); }
int implicit_index(V16C a, V16C b) { return __builtin_ia32_pcmpistri128(a,b,128); }
V16C implicit_mask(V16C a, V16C b) { return __builtin_ia32_pcmpistrm128(a,b,64); }
V2I extract(V2I a) { return __builtin_ia32_extrqi(a,5,11); }
V2I insert(V2I a,V2I b) { return __builtin_ia32_insertqi(a,b,5,11); }
V2I extract_high(V2I a) { return __builtin_ia32_extrqi(a,65,127); }
V8H narrow4(V4F a,V8H w,unsigned char k) { return __builtin_ia32_vcvtps2ph_mask(a,8,w,k); }
V8H narrow8(V8F a,V8H w,unsigned char k) { return __builtin_ia32_vcvtps2ph256_mask(a,12,w,k); }
V16H narrow16(V16F a,V16H w,unsigned short k) { return __builtin_ia32_vcvtps2ph512_mask(a,12,w,k); }
V16H narrow16_rc(V16F a,V16H w,unsigned short k) { return __builtin_ia32_vcvtps2ph512_mask(a,3,w,k); }
V16H narrow16_ff(V16F a,V16H w,unsigned short k) { return __builtin_ia32_vcvtps2ph512_mask(a,255,w,k); }
