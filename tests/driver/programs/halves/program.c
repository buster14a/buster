#include <immintrin.h>
#include <stdio.h>
#include <string.h>
static void show(const char* name, const void* p, unsigned long n) { const unsigned short* x = p; printf("%s", name); for (unsigned long i = 0; i < n / 2; i++) printf(" %04x", x[i]); printf("\n"); }
static void showh(const char* name, _Float16 h) { show(name, &h, 2); }
int main(int argc, char** argv)
{
    (void)argv;
    int n = argc + 6;
    _Float16 a = (_Float16)n / (_Float16)3, b = (_Float16)-2.5, c = (_Float16)(n * 1000);
    __m128h v = _mm_set_ph(a, b, c, 2, 3, 4, 5, (_Float16)n);
    __m128h s = _mm_set_sh(b);
    show("set", &v, 16);
    __m128h r = _mm_setr_ph(a, b, c, 2, 3, 4, 5, (_Float16)n);
    show("setr", &r, 16);
    __m128h one = _mm_set1_ph(a);
    show("set1", &one, 16);
    __m128h zero = _mm_setzero_ph();
    show("zero", &zero, 16);
    show("set_sh", &s, 16);
    showh("cvtsh_h", _mm_cvtsh_h(v));
    __m128h t = _mm_add_sh(v, s);
    show("add_sh", &t, 16);
    t = _mm_sub_sh(v, s);
    show("sub_sh", &t, 16);
    t = _mm_mul_sh(v, s);
    show("mul_sh", &t, 16);
    t = _mm_div_sh(v, s);
    show("div_sh", &t, 16);
    t = _mm_cvti32_sh(v, -n * 77);
    show("cvti32_sh", &t, 16);
    t = _mm_cvti64_sh(v, (long long)n * 999999);
    show("cvti64_sh", &t, 16);
    t = _mm_cvtu32_sh(v, (unsigned)n * 3001u);
    show("cvtu32_sh", &t, 16);
    t = _mm_cvtu64_sh(v, (unsigned long long)n << 40);
    show("cvtu64_sh", &t, 16);
    _Float16 memory[2] = { c, a };
    t = _mm_load_sh(&memory[1]);
    show("load_sh", &t, 16);
    _mm_store_sh(&memory[0], v);
    show("store_sh", memory, 4);
    t = _mm_move_sh(v, s);
    show("move_sh", &t, 16);
    __m256h w = _mm256_set_ph(a, b, c, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, (_Float16)n);
    show("set256", &w, 32);
    w = _mm256_setr_ph(a, b, c, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, (_Float16)n);
    show("setr256", &w, 32);
    w = _mm256_set1_ph(b);
    show("set1_256", &w, 32);
    showh("cvtsh_h256", _mm256_cvtsh_h(_mm256_set1_ph(c)));
    w = _mm256_setzero_ph();
    show("zero256", &w, 32);
    __m512h z = _mm512_set1_ph(c);
    show("set1_512", &z, 64);
    z = _mm512_set_ph(a, b, c, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, (_Float16)n);
    show("set512", &z, 64);
    z = _mm512_setr_ph(a, b, c, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, (_Float16)n);
    show("setr512", &z, 64);
    showh("cvtsh_h512", _mm512_cvtsh_h(z));
    z = _mm512_setzero_ph();
    show("zero512", &z, 64);
    float f = _cvtsh_ss((unsigned short)(0x3c00 + n));
    show("cvtsh_ss", &f, 4);
    __bf16 bf = _mm_cvtness_sbh((float)n / 3.0f);
    show("cvtness_sbh", &bf, 2);
    float back = _mm_cvtsbh_ss(bf);
    show("cvtsbh_ss", &back, 4);
    __m512h u = _mm512_setr_ph(a, b, c, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, (_Float16)-n, (_Float16)n);
    showh("reduce_add_ph", _mm_reduce_add_ph(v));
    showh("reduce_mul_ph", _mm_reduce_mul_ph(v));
    showh("reduce_max_ph", _mm_reduce_max_ph(v));
    showh("reduce_min_ph", _mm_reduce_min_ph(v));
    showh("reduce_add_ph256", _mm256_reduce_add_ph(_mm512_castph512_ph256(u)));
    showh("reduce_mul_ph256", _mm256_reduce_mul_ph(_mm512_castph512_ph256(u)));
    showh("reduce_max_ph256", _mm256_reduce_max_ph(_mm512_castph512_ph256(u)));
    showh("reduce_min_ph256", _mm256_reduce_min_ph(_mm512_castph512_ph256(u)));
    showh("reduce_add_ph512", _mm512_reduce_add_ph(u));
    showh("reduce_mul_ph512", _mm512_reduce_mul_ph(u));
    showh("reduce_max_ph512", _mm512_reduce_max_ph(u));
    showh("reduce_min_ph512", _mm512_reduce_min_ph(u));
    return 0;
}
