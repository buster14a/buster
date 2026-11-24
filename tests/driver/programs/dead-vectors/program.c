#include <immintrin.h>
#include <stdio.h>
static volatile int go;
static volatile long sink;
__attribute__((noinline)) __mmask16 kand_trap(__mmask16 k) { return go ? __builtin_ia32_kandhi(k, (__builtin_trap(), k)) : (__mmask16)(k + 1); }
__attribute__((noinline)) __m512i select_trap(__m512i a, __m512i b, __mmask16 k) { return go ? (__m512i)__builtin_ia32_selectd_512(k, (__v16si)a, (__builtin_trap(), (__v16si)b)) : b; }
__attribute__((noinline)) __m512i add_trap(__m512i a, __m512i b) { return go ? (__m512i)((__v16si)a + (__builtin_trap(), (__v16si)b)) : a; }
__attribute__((noinline)) __m256i add_trap256(__m256i a, __m256i b) { return go ? (__m256i)((__v8si)a + (__builtin_trap(), (__v8si)b)) : b; }
__attribute__((noinline)) void eh_mask(long offset, void* h, __mmask16 k) { __builtin_unwind_init(); sink = go ? (long)__builtin_ia32_kandhi(k, (__builtin_eh_return(offset, h), k)) : (long)k; }
__attribute__((noinline)) void eh_vector(long offset, void* h, __m512i a) { __builtin_unwind_init(); sink = go ? ((__v16si)a + (__builtin_eh_return(offset, h), (__v16si)a))[0] : ((__v16si)a)[1]; }
int main(void)
{
    __m512i a = _mm512_set1_epi32(3), b = _mm512_set1_epi32(4);
    __m256i c = _mm256_set1_epi32(5);
    printf("%d %d %d %d\n", (int)kand_trap(6), _mm512_reduce_add_epi32(select_trap(a, b, 7)), _mm512_reduce_add_epi32(add_trap(a, b)), ((__v8si)add_trap256(c, c))[0]);
    eh_mask(0, 0, 9);
    long m = sink;
    eh_vector(0, 0, a);
    printf("%ld %ld\n", m, sink);
    return 0;
}
