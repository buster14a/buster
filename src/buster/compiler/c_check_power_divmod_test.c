// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original source and independently written shift/mask expectations for #1386.
// Included by c_check_program_test.c; its source harness compares both lowerings and native output with Clang.

static void check_test_power_divmod(CheckTest* test, const char* clang)
{
    // Negative narrow constants must be converted from their signed source width, not treated as masked powers.
    char* narrow_source = NULL;
    size_t narrow_length = 0;
    FILE* narrow = open_memstream(&narrow_source, &narrow_length);
    check_test_expect(test, narrow != NULL, "narrow-divisor source has an output stream");
    if (narrow != NULL)
    {
        static const char* operands[] = { "(signed char)-128", "(short)-32768", "(-2147483647-1)", "(_BitInt(9))-256", "(_BitInt(17))-65536", "(_BitInt(33))-4294967296ll" };
        static const uint64_t magnitudes[] = { 128, 32768, UINT64_C(2147483648), 256, 65536, UINT64_C(4294967296) };
        for (uint32_t width = 0; width < 2; width += 1)
        {
            const char* type = width == 0 ? "unsigned" : "unsigned long";
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(operands) - (width == 0); k += 1)
            {
                fprintf(narrow, "%s nq%u_%u(%s x){return x/%s;}%s nr%u_%u(%s x){return x%%%s;}"
                                "%s ncq%u_%u(%s x){x/=%s;return x;}%s ncr%u_%u(%s x){x%%=%s;return x;}",
                        type, width, k, type, operands[k], type, width, k, type, operands[k],
                        type, width, k, type, operands[k], type, width, k, type, operands[k]);
            }
        }
        fprintf(narrow, "int tested(int n){unsigned x=0xffffffffu-(unsigned)n;"
                        "unsigned long y=0xfffffffffffffffful-(unsigned)n;int bad=0;");
        for (uint32_t width = 0; width < 2; width += 1)
        {
            const char* value = width == 0 ? "x" : "y";
            const char* suffix = width == 0 ? "u" : "ul";
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(operands) - (width == 0); k += 1)
            {
                const uint64_t converted = width == 0 ? (uint32_t)(UINT64_C(0) - magnitudes[k]) : UINT64_C(0) - magnitudes[k];
                fprintf(narrow, "bad+=nq%u_%u(%s)!=%s/%llu%s;bad+=nr%u_%u(%s)!=%s%%%llu%s;"
                                "bad+=ncq%u_%u(%s)!=%s/%llu%s;bad+=ncr%u_%u(%s)!=%s%%%llu%s;",
                        width, k, value, value, (unsigned long long)converted, suffix,
                        width, k, value, value, (unsigned long long)converted, suffix,
                        width, k, value, value, (unsigned long long)converted, suffix,
                        width, k, value, value, (unsigned long long)converted, suffix);
            }
        }
        fprintf(narrow, "return bad;}");
        const bool written = fclose(narrow) == 0;
        check_test_expect(test, written, "narrow-divisor source closes successfully");
        if (written)
        {
            check_test_program_source(test, narrow_source, "unsigned-narrow-signed-divmod", clang);
        }
    }
    free(narrow_source);
    static const uint32_t shifts32[] = { 0, 1, 4, 16, 30, 31 };
    static const uint32_t shifts64[] = { 0, 1, 4, 31, 32, 33, 62, 63 };
    char* source = NULL;
    size_t length = 0;
    FILE* text = open_memstream(&source, &length);
    check_test_expect(test, text != NULL, "power-of-two source has an output stream");
    if (text != NULL)
    {
        for (uint32_t width = 0; width < 2; width += 1)
        {
            const uint32_t* shifts = width == 0 ? shifts32 : shifts64;
            const uint32_t count = width == 0 ? BUSTER_ARRAY_LENGTH(shifts32) : BUSTER_ARRAY_LENGTH(shifts64);
            const char* type = width == 0 ? "unsigned" : "unsigned long";
            const char* suffix = width == 0 ? "u" : "ul";
            for (uint32_t k = 0; k < count; k += 1)
            {
                const uint64_t divisor = UINT64_C(1) << shifts[k];
                fprintf(text, "%s q%u_%u(%s x){return x/%llu%s;}%s r%u_%u(%s x){return x%%%llu%s;}",
                        type, width, k, type, (unsigned long long)divisor, suffix, type, width, k, type, (unsigned long long)divisor, suffix);
            }
        }
        fprintf(text, "unsigned positive(unsigned x){return x/(short)16384;}"
                      "unsigned long positive64(unsigned long x){return x%(signed char)64;}"
                      "unsigned compound(unsigned x){x/=16u;x%%=64u;return x;}"
                      "unsigned normalized(unsigned x){return x/(-2147483647-1);}"
                      "int tested(int n){unsigned a[]={0u,1u,15u,16u,0x7fffffffu,0x80000000u,0xffffffffu,(unsigned)n};"
                      "unsigned long b[]={0ul,1ul,0xfffffffful,0x100000000ul,0x7ffffffffffffffful,0x8000000000000000ul,"
                      "0xfffffffffffffffful,(unsigned long)(unsigned)n*0x9e3779b97f4a7c15ul};int failed=0;"
                      "for(unsigned i=0;i<8u;i++){");
        for (uint32_t width = 0; width < 2; width += 1)
        {
            const uint32_t* shifts = width == 0 ? shifts32 : shifts64;
            const uint32_t count = width == 0 ? BUSTER_ARRAY_LENGTH(shifts32) : BUSTER_ARRAY_LENGTH(shifts64);
            const char* values = width == 0 ? "a[i]" : "b[i]";
            const char* suffix = width == 0 ? "u" : "ul";
            for (uint32_t k = 0; k < count; k += 1)
            {
                const uint64_t mask = (UINT64_C(1) << shifts[k]) - 1;
                fprintf(text, "failed+=q%u_%u(%s)!=(%s>>%u);failed+=r%u_%u(%s)!=(%s&%llu%s);",
                        width, k, values, values, shifts[k], width, k, values, values, (unsigned long long)mask, suffix);
            }
        }
        fprintf(text, "failed+=positive(a[i])!=(a[i]>>14);failed+=positive64(b[i])!=(b[i]&63ul);failed+=compound(a[i])!=((a[i]>>4)&63u);failed+=normalized(a[i])!=(a[i]>>31);}return failed;}");
        const bool written = fclose(text) == 0;
        check_test_expect(test, written, "power-of-two source closes successfully");
        if (written)
        {
            check_test_program_source(test, source, "unsigned-power-two-divmod", clang);
        }
    }
    free(source);
    check_test_program_source(test,
        "unsigned hits;unsigned touch(void){hits++;return 16u;}"
        "unsigned q(unsigned x,unsigned d){return x/d;}unsigned r(unsigned x,unsigned d){return x%d;}"
        "unsigned selected(unsigned x,int p){return x/(p?16u:32u);}"
        "int tested(int n){unsigned x=(unsigned)n;hits=0;volatile unsigned divisor=16u;"
        "unsigned a=x/(hits++,16u),b=x%touch(),c=x/divisor;"
        "int bad=hits!=2u;bad+=a!=(x>>4);bad+=b!=(x&15u);bad+=c!=(x>>4);"
        "bad+=q(x,10u)!=x/10u;bad+=r(x,10u)!=x%10u;bad+=selected(x,n&1)!=(x>>((n&1)?4:5));"
        "int s=n/16,t=n%16;bad+=s*16+t!=n;return bad;}",
        "power-two-signed-effects-variable-and-branch-controls", clang);
}
