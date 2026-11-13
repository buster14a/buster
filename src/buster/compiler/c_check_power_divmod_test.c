// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original source and independently written shift/mask expectations for #1386 (signed and pointer differences too).
// Included by c_check_program_test.c; its source harness compares both lowerings and native output with Clang.

// Signed division and remainder by positive powers of two, and pointer differences by power-of-two element sizes
// (#1386): the signed functions' expectations come from unsigned shifts of the magnitude, the differences' from the
// indices, and every result is folded into the returned hash, which clang's build of the same source must match. The
// fixture divides nowhere (the harness checks its entries), so its controls, which keep their division, are another one.
static void check_test_power_divmod_signed(CheckTest* test, const char* clang)
{
    static const uint32_t shifts32[] = { 0, 1, 2, 4, 15, 16, 29, 30 };
    static const uint32_t shifts64[] = { 0, 1, 4, 31, 32, 33, 61, 62 };
    char* source = NULL;
    size_t length = 0;
    FILE* text = open_memstream(&source, &length);
    check_test_expect(test, text != NULL, "signed power-of-two source has an output stream");
    if (text != NULL)
    {
        for (uint32_t width = 0; width < 2; width += 1)
        {
            const uint32_t* shifts = width == 0 ? shifts32 : shifts64;
            const uint32_t count = width == 0 ? BUSTER_ARRAY_LENGTH(shifts32) : BUSTER_ARRAY_LENGTH(shifts64);
            const char* type = width == 0 ? "int" : "long";
            const char* suffix = width == 0 ? "" : "l";
            for (uint32_t k = 0; k < count; k += 1)
            {
                const unsigned long long divisor = UINT64_C(1) << shifts[k];
                fprintf(text, "%s q%u_%u(%s x){return x/%llu%s;}%s r%u_%u(%s x){return x%%%llu%s;}"
                              "%s cq%u_%u(%s x){x/=%llu%s;return x;}%s cr%u_%u(%s x){x%%=%llu%s;return x;}",
                        type, width, k, type, divisor, suffix, type, width, k, type, divisor, suffix,
                        type, width, k, type, divisor, suffix, type, width, k, type, divisor, suffix);
            }
        }
        fprintf(text, "int promoted(unsigned char c){return c/16+c%%16;}int halves(short s){return s/2+s%%2;}"
                      "int wide(unsigned short x){return x/4-x%%4;}short narrow(short x){x/=4;x%%=2;return x;}"
                      "signed char tiny(signed char x){x%%=8;return x;}"
                      "typedef struct{char b[16];}P16;typedef struct{char b[32];}P32;"
                      "long d1(char*a,char*b){return a-b;}long d2(short*a,short*b){return a-b;}long d4(int*a,int*b){return a-b;}"
                      "long d8(long*a,long*b){return a-b;}long d16(P16*a,P16*b){return a-b;}long d32(P32*a,P32*b){return a-b;}"
                      "static int eqi(int x,int k){unsigned u=(unsigned)x;return x<0?(int)(0u-((0u-u)>>k)):(int)(u>>k);}"
                      "static int eri(int x,int k){return (int)((unsigned)x-((unsigned)eqi(x,k)<<k));}"
                      "static long eql(long x,int k){unsigned long u=(unsigned long)x;return x<0?(long)(0ul-((0ul-u)>>k)):(long)(u>>k);}"
                      "static long erl(long x,int k){return (long)((unsigned long)x-((unsigned long)eql(x,k)<<k));}"
                      "static unsigned h;static int bad;static void see(long got,long expected){h=h*31u+(unsigned)got;bad+=got!=expected;}"
                      "char c1[64];short c2[64];int c4[64];long c8[64];P16 c16[64];P32 c32[64];"
                      "int tested(int n){h=0;bad=0;"
                      "int a[]={0,1,-1,2,-2,3,-3,15,-15,16,-16,17,-17,1073741823,-1073741823,1073741824,-1073741824,"
                      "2147483647,-2147483647,-2147483647-1,n,-n,(int)((unsigned)n*2654435761u)};"
                      "long b[]={0l,1l,-1l,15l,-15l,16l,-16l,2147483647l,-2147483648l,4294967295l,-4294967296l,4294967297l,"
                      "4611686018427387903l,-4611686018427387903l,4611686018427387904l,-4611686018427387904l,"
                      "9223372036854775807l,-9223372036854775807l,-9223372036854775807l-1,(long)n,-(long)n,(long)((unsigned long)n*0x9e3779b97f4a7c15ul)};"
                      "for(unsigned i=0;i<sizeof(a)/sizeof(a[0]);i++){int x=a[i];");
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(shifts32); k += 1)
        {
            fprintf(text, "see(q0_%u(x),eqi(x,%u));see(r0_%u(x),eri(x,%u));see(cq0_%u(x),eqi(x,%u));see(cr0_%u(x),eri(x,%u));",
                    k, shifts32[k], k, shifts32[k], k, shifts32[k], k, shifts32[k]);
        }
        fprintf(text, "see(promoted((unsigned char)x),eqi(x&255,4)+eri(x&255,4));see(halves((short)x),eqi((short)x,1)+eri((short)x,1));"
                      "see(wide((unsigned short)x),eqi(x&65535,2)-eri(x&65535,2));see(narrow((short)x),eri(eqi((short)x,2),1));"
                      "see(tiny((signed char)x),eri((signed char)x,3));}"
                      "for(unsigned i=0;i<sizeof(b)/sizeof(b[0]);i++){long x=b[i];");
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(shifts64); k += 1)
        {
            fprintf(text, "see(q1_%u(x),eql(x,%u));see(r1_%u(x),erl(x,%u));see(cq1_%u(x),eql(x,%u));see(cr1_%u(x),erl(x,%u));",
                    k, shifts64[k], k, shifts64[k], k, shifts64[k], k, shifts64[k]);
        }
        fprintf(text, "}int j=n&63;for(int i=0;i<64;i++){long e=(long)i-j;"
                      "see(d1(c1+i,c1+j),e);see(d2(c2+i,c2+j),e);see(d4(c4+i,c4+j),e);see(d8(c8+i,c8+j),e);"
                      "see(d16(c16+i,c16+j),e);see(d32(c32+i,c32+j),e);}"
                      "return bad?-bad:(int)(h&0x7fffffff);}");
        const bool written = fclose(text) == 0;
        check_test_expect(test, written, "signed power-of-two source closes successfully");
        if (written)
        {
            check_test_program_source(test, source, "signed-power-two-divmod", clang);
        }
    }
    free(source);
    // The controls keep their division: negative divisors, the sign bit's (no positive power of two), a _BitInt's, a
    // divisor that isn't a constant, and element sizes that aren't powers of two, a variable-length array's among them.
    check_test_program_source(test,
        "int n16(int x){return x/-16;}int nr16(int x){return x%-16;}int mq(int x){return x/(-2147483647-1);}int mr(int x){return x%(-2147483647-1);}"
        "long lq(long x){return x/(-9223372036854775807l-1);}long lr(long x){return x%(-9223372036854775807l-1);}"
        "int t3(int x){return x/3;}int r3(int x){return x%3;}_BitInt(33) bq(_BitInt(33) x){return x/16;}_BitInt(33) br(_BitInt(33) x){return x%16;}"
        "int hits;int sixteen(void){hits++;return 16;}int vq(int x,int d){return x/d;}"
        "typedef struct{char b[3];}P3;typedef struct{int b[3];}P12;long d3(P3*a,P3*b){return a-b;}long d12(P12*a,P12*b){return a-b;}"
        "long dv(int m,int i,int j){int a[8][m];return &a[i]-&a[j];}P3 c3[16];P12 c12[16];"
        "int tested(int n){unsigned h=0;int bad=0;hits=0;volatile int sixteen_v=16;"
        "int a[]={0,1,-1,15,-15,16,-16,17,-17,2147483647,-2147483647,-2147483647-1,n,-n};"
        "for(unsigned i=0;i<sizeof(a)/sizeof(a[0]);i++){int x=a[i];long y=(long)x*4294967311l;"
        "h=h*31u+(unsigned)n16(x);h=h*31u+(unsigned)nr16(x);h=h*31u+(unsigned)mq(x);h=h*31u+(unsigned)mr(x);"
        "h=h*31u+(unsigned)lq(y);h=h*31u+(unsigned)lr(y);h=h*31u+(unsigned)t3(x);h=h*31u+(unsigned)r3(x);"
        "h=h*31u+(unsigned)(long)bq((_BitInt(33))y);h=h*31u+(unsigned)(long)br((_BitInt(33))y);"
        "h=h*31u+(unsigned)(x/(hits++,16));h=h*31u+(unsigned)(x%sixteen());h=h*31u+(unsigned)(x/sixteen_v);h=h*31u+(unsigned)vq(x,16);}"
        "bad+=hits!=28;int j=n&15;for(int i=0;i<16;i++){bad+=d3(c3+i,c3+j)!=i-j;bad+=d12(c12+i,c12+j)!=i-j;"
        "bad+=dv(1,i&7,j&7)!=(i&7)-(j&7);bad+=dv(4,i&7,j&7)!=(i&7)-(j&7);bad+=dv(3,i&7,j&7)!=(i&7)-(j&7);}"
        "return bad?-bad:(int)(h&0x7fffffff);}",
        "signed-power-two-controls", clang);
}

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
                      "unsigned long positive64(unsigned long x){return x%%(signed char)64;}"
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
    check_test_power_divmod_signed(test, clang);
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
