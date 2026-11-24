#include <immintrin.h>
#include <stdio.h>
#include <string.h>
static void show(const char* name, __m64 v) { long long x; memcpy(&x, &v, 8); printf("%s %016llx\n", name, x); }
static void show128(const char* name, __m128i v) { long long x[2]; memcpy(x, &v, 16); printf("%s %016llx %016llx\n", name, x[1], x[0]); }
static void showps(const char* name, __m128 v) { float x[4]; memcpy(x, &v, 16); printf("%s %g %g %g %g\n", name, x[0], x[1], x[2], x[3]); }
static void showpd(const char* name, __m128d v) { double x[2]; memcpy(x, &v, 16); printf("%s %g %g\n", name, x[0], x[1]); }
int main(int argc, char** argv)
{
    (void)argv;
    int n = argc + 6;
    __m64 a = _mm_set_pi8((char)n, 2, 3, 4, (char)(n * 7), 6, 7, (char)-n);
    __m64 b = _mm_set_pi16((short)(n * 1000), -3, 7, (short)n);
    __m128 f = _mm_setr_ps(1.5f, -2.25f, (float)n, 100.75f);
    __m128d d = _mm_setr_pd(-7.5, (double)n * 3.25);
    show("add8", _mm_add_pi8(a, b));
    show("add16", _mm_add_pi16(a, b));
    show("add32", _mm_add_pi32(a, b));
    show("sub8", _mm_sub_pi8(a, b));
    show("adds8", _mm_adds_pi8(a, b));
    show("addsu8", _mm_adds_pu8(a, b));
    show("subs16", _mm_subs_pi16(a, b));
    show("madd", _mm_madd_pi16(a, b));
    show("mulhi", _mm_mulhi_pi16(a, b));
    show("mullo", _mm_mullo_pi16(a, b));
    show("and", _mm_and_si64(a, b));
    show("andnot", _mm_andnot_si64(a, b));
    show("or", _mm_or_si64(a, b));
    show("xor", _mm_xor_si64(a, b));
    show("cmpeq8", _mm_cmpeq_pi8(a, a));
    show("cmpgt16", _mm_cmpgt_pi16(a, b));
    show("cmpgt32", _mm_cmpgt_pi32(b, a));
    show("packs16", _mm_packs_pi16(a, b));
    show("packs32", _mm_packs_pi32(a, b));
    show("packsu16", _mm_packs_pu16(a, b));
    show("unpackhi8", _mm_unpackhi_pi8(a, b));
    show("unpacklo8", _mm_unpacklo_pi8(a, b));
    show("unpackhi32", _mm_unpackhi_pi32(a, b));
    show("unpacklo16", _mm_unpacklo_pi16(a, b));
    show("sll16", _mm_sll_pi16(a, _mm_cvtsi32_si64(3)));
    show("slli32", _mm_slli_pi32(a, 5));
    show("sllsi64", _mm_sll_si64(a, _mm_cvtsi32_si64(9)));
    show("srai16", _mm_srai_pi16(b, 2));
    show("sra32", _mm_sra_pi32(b, _mm_cvtsi32_si64(4)));
    show("srli64", _mm_srli_si64(a, 12));
    show("srl16", _mm_srl_pi16(b, _mm_cvtsi32_si64(1)));
    show("cvt", _mm_cvtsi32_si64(n * 3));
    printf("to32 %d\n", _mm_cvtsi64_si32(a));
    printf("to64 %lld\n", _mm_cvtm64_si64(b));
    show("from64", _mm_cvtsi64_m64((long long)n << 40));
    show("set1", _mm_set1_pi16((short)n));
    show("set1b", _mm_set1_pi8((char)(n + 100)));
    show("setr", _mm_setr_pi32(n, -n));
    show("zero", _mm_setzero_si64());
    show("avg8", _mm_avg_pu8(a, b));
    show("avg16", _mm_avg_pu16(a, b));
    showps("cvtpi32ps", _mm_cvtpi32_ps(f, b));
    show("cvtps32", _mm_cvtps_pi32(f));
    show("cvttps32", _mm_cvttps_pi32(f));
    showps("cvtpi8", _mm_cvtpi8_ps(a));
    showps("cvtpu8", _mm_cvtpu8_ps(a));
    showps("cvtpi16", _mm_cvtpi16_ps(b));
    showps("cvtpu16", _mm_cvtpu16_ps(b));
    showps("cvtpi32x2", _mm_cvtpi32x2_ps(a, b));
    show("cvtps16", _mm_cvtps_pi16(f));
    show("cvtps8", _mm_cvtps_pi8(f));
    __m64 mem[2] = { a, b };
    showps("loadh", _mm_loadh_pi(f, &mem[0]));
    showps("loadl", _mm_loadl_pi(f, &mem[1]));
    _mm_storeh_pi(&mem[0], f);
    _mm_storel_pi(&mem[1], f);
    show("storeh", mem[0]);
    show("storel", mem[1]);
    show("max16", _mm_max_pi16(a, b));
    show("maxu8", _mm_max_pu8(a, b));
    show("min16", _mm_min_pi16(a, b));
    show("minu8", _mm_min_pu8(a, b));
    printf("movemask %d\n", _mm_movemask_pi8(a));
    show("mulhu16", _mm_mulhi_pu16(a, b));
    show("sad", _mm_sad_pu8(a, b));
    __m64 stream;
    _mm_stream_pi(&stream, a);
    show("stream", stream);
    char bytes[16] = { 0 };
    _mm_maskmove_si64(a, _mm_set_pi8(-1, 0, -1, 0, -1, 0, -1, -1), bytes + 3);
    printf("maskmove");
    for (int i = 0; i < 16; i++)
    {
        printf(" %d", bytes[i]);
    }
    printf("\n");
    show("shuffle16", _mm_shuffle_pi16(b, 0x1b));
    show("abs8", _mm_abs_pi8(a));
    show("abs16", _mm_abs_pi16(b));
    show("abs32", _mm_abs_pi32(b));
    show("hadd16", _mm_hadd_pi16(a, b));
    show("hadd32", _mm_hadd_pi32(a, b));
    show("hadds16", _mm_hadds_pi16(a, b));
    show("hsub16", _mm_hsub_pi16(a, b));
    show("hsub32", _mm_hsub_pi32(a, b));
    show("hsubs16", _mm_hsubs_pi16(a, b));
    show("maddubs", _mm_maddubs_pi16(a, b));
    show("mulhrs", _mm_mulhrs_pi16(a, b));
    show("shuffle8", _mm_shuffle_pi8(a, b));
    show("sign8", _mm_sign_pi8(a, b));
    show("sign16", _mm_sign_pi16(a, b));
    show("sign32", _mm_sign_pi32(a, b));
    show("addsi64", _mm_add_si64(a, b));
    show("subsi64", _mm_sub_si64(a, b));
    show("mulsu32", _mm_mul_su32(a, b));
    show("cvtpd32", _mm_cvtpd_pi32(d));
    show("cvttpd32", _mm_cvttpd_pi32(d));
    showpd("cvtpi32pd", _mm_cvtpi32_pd(b));
    show("movepi64", _mm_movepi64_pi64(_mm_set_epi64x(n, -n)));
    show128("movpi64", _mm_movpi64_epi64(a));
    show128("set1epi64", _mm_set1_epi64(b));
    show128("setepi64", _mm_set_epi64(a, b));
    show128("setrepi64", _mm_setr_epi64(a, b));
    _mm_empty();
    return 0;
}
