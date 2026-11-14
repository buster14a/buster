typedef int I16 __attribute__((vector_size(64)));
typedef long long L8 __attribute__((vector_size(64)));
typedef long long L4 __attribute__((vector_size(32)));
typedef long long L2 __attribute__((vector_size(16)));
typedef int I8 __attribute__((vector_size(32)));
typedef int I4 __attribute__((vector_size(16)));
typedef char C16 __attribute__((vector_size(16)));
typedef char C32 __attribute__((vector_size(32)));
typedef short S32 __attribute__((vector_size(64)));
unsigned long long f(I16 a, L8 q, L4 h, L2 t, I8 e, I4 i, C16 c, C32 w, S32 s, const void* p, void* o)
{
    unsigned long long r = __builtin_ia32_ucmpd512_mask(a, a, 7, 0xffff) + __builtin_ia32_ucmpd256_mask(e, e, 0, 0xff) +
                           __builtin_ia32_ucmpd128_mask(i, i, 1, 0xff) +
                           __builtin_ia32_ucmpb128_mask(c, c, 4, 0xffff) + __builtin_ia32_ucmpb256_mask(w, w, 4, ~0u) +
                           __builtin_ia32_ucmpw512_mask(s, s, 6, ~0u) + __builtin_ia32_kshiftlihi(1, 255) + __builtin_ia32_kshiftlidi(1, 0);
    I16 g = __builtin_ia32_gathersiv16si(a, p, a, 0xffff, 4) + __builtin_ia32_alignd512(a, a, 255) + __builtin_ia32_shuf_i32x4(a, a, 0) +
            __builtin_ia32_inserti32x4(a, i, 3) + __builtin_ia32_prold512(a, 31);
    L8 l = __builtin_ia32_gathersiv8di(q, p, e, 0xff, 8) + __builtin_ia32_gatherdiv8di(q, p, q, 0xff, 1) + __builtin_ia32_alignq512(q, q, 7);
    __builtin_ia32_scatterdiv16si(o, 0xff, q, e, 2);
    __builtin_ia32_scatterdiv8di(o, 0xff, q, q, 8);
    __builtin_ia32_scattersiv16si(o, 0xffff, a, a, 4);
    L4 x = __builtin_ia32_extracti64x4_mask(q, 1, h, 0xff) + __builtin_ia32_insert128i256(h, t, 1);
    I4 v = __builtin_ia32_vec_set_v4si(i, 5, 3);
    return r + (unsigned long long)g[0] + (unsigned long long)l[0] + (unsigned long long)x[0] + (unsigned long long)v[0] +
           (unsigned)__builtin_ia32_vec_ext_v4si(i, 3) + (unsigned char)__builtin_ia32_vec_ext_v16qi(c, 15);
}
