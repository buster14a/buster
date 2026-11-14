// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 0's inlining of always_inline functions (#1398, tier0_inline.c and c_check_tier0_inline.c), as the driver builds
// a program: included by c_checker_driver_test.c after c_check_tier1_driver_test.c. The fixture is #1282's (the x86
// intrinsics' wrappers, a struct helper whose result returns in memory, which keeps its call, and a noinline function)
// and the splice's shapes: nested calls of one callee in its own arguments (its levels), arguments of ?: and of a
// statement expression (their starts), a callee without arguments as another's argument (two insertions at one entry),
// parameters of four classes (bindings one after its argument), a callee with a loop, one with two returns (refused, a
// call), a cycle of two (its closing call a call), a callee through a pointer, a pointer parameter, long double, a struct
// argument and result in registers, a chain of wrappers (_mm256_mask_add_epi32's), and intrinsics of 16, 32 and 64
// bytes. Built at x86-64-v4 by the vector path, the scalar twin and --tier0-no-inline: each runs as clang -O0's build
// does where the host has x86-64-v4, the two paths' objects are the same bytes, and the control's differs.
//
// The lanes' fixture (#1398's review): a chain of 20 always_inline functions, past the expansion's depth bound, and a
// cycle of two, reached from 40 roots at every depth, which the lanes take in an order their timing decides. Which
// callees expand must be the graph's alone: its object by 4 lanes, three times, and by 1 lane are the same bytes, and
// it runs as clang -O0's build does.

static const char check_test_tier0_inline_source[] =
    "#include <immintrin.h>\n"
    "#include <stdint.h>\n"
    "#include <stdio.h>\n"
    "#define AI static inline __attribute__((always_inline))\n"
    "typedef struct { long long a; int b; short c; } Triple;\n"
    "typedef struct { int x, y; } Pair;\n"
    "AI Triple twist(Triple t, long long k) { Triple r = { t.a * 3 + k, t.b ^ (int)k, (short)(t.c + 1) }; return r; }\n"
    "AI Pair swap(Pair p, int k) { Pair r = { p.y + k, p.x - k }; return r; }\n"
    "AI int add3(int a, int b, int c) { return a * 3 + b - c; }\n"
    "AI double mixf(int a, double b, long c, float d) { return a + b * (double)c - d; }\n"
    "AI int seven(void) { return 7; }\n"
    "AI int sum_to(int n) { int s = 0; for (int i = 0; i < n; i += 1) s += i; return s; }\n"
    "AI int sgn(int x) { if (x < 0) return -1; return x > 0; }\n"
    "AI int pong(int n);\n"
    "AI int ping(int n) { return n <= 0 ? 0 : pong(n - 1) + 2; }\n"
    "AI int pong(int n) { return n <= 0 ? 0 : ping(n - 1) + 1; }\n"
    "AI void bump(int* p, int k) { *p += k; }\n"
    "AI long double ld(long double x, int k) { return x * k + 0.5L; }\n"
    "static __attribute__((noinline)) int kept(int x) { return x * 7; }\n"
    "__attribute__((noinline)) __m512i mix(__m512i a, __m512i b, __m512i c)\n"
    "{\n"
    "    __m512i x = _mm512_add_epi32(a, b);\n"
    "    x = _mm512_xor_si512(x, _mm512_min_epu8(x, c));\n"
    "    return _mm512_mask_sub_epi32(x, (__mmask16)0x5a5a, x, _mm512_set1_epi32(17));\n"
    "}\n"
    "__attribute__((noinline)) uint64_t count(const uint8_t* p, int n)\n"
    "{\n"
    "    __m512i acc = _mm512_setzero_si512();\n"
    "    for (int i = 0; i + 64 <= n; i += 64)\n"
    "    {\n"
    "        __m512i v = _mm512_loadu_si512(p + i);\n"
    "        __mmask64 m = _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8('a'));\n"
    "        acc = _mm512_add_epi64(acc, _mm512_set1_epi64((long long)__builtin_popcountll(m)));\n"
    "        acc = _mm512_mask_add_epi32(acc, (__mmask16)m, acc, _mm512_set1_epi32(3));\n"
    "    }\n"
    "    return (uint64_t)_mm512_reduce_add_epi64(acc);\n"
    "}\n"
    "__attribute__((noinline)) int narrow(int n, int shift)\n"
    "{\n"
    "    __m128i v = _mm_set_epi32(n, n + 1, n * 2, n - 3);\n"
    "    v = _mm_slli_epi32(v, shift);\n"
    "    __m256i w = _mm256_mask_add_epi32(_mm256_set1_epi32(n), (__mmask8)(n & 0xff), _mm256_set1_epi32(1), _mm256_slli_epi32(_mm256_set1_epi32(n), 3));\n"
    "    int lanes[8];\n"
    "    _mm256_storeu_si256((__m256i*)lanes, w);\n"
    "    return _mm_extract_epi32(v, 1) + _mm_extract_epi16(v, 3) + _mm_cvtsi128_si32(_mm_add_epi32(v, _mm_shuffle_epi32(v, 0x1b))) + lanes[0] - lanes[7];\n"
    "}\n"
    "__attribute__((noinline)) long long structs(int n)\n"
    "{\n"
    "    Triple t = { 5, 9, 2 };\n"
    "    Pair p = { 3, 4 };\n"
    "    for (int i = 0; i < n; i += 1)\n"
    "    {\n"
    "        t = twist(t, i);\n"
    "        p = swap(swap(p, i), i + 1);\n"
    "    }\n"
    "    return t.a + t.b + t.c + kept(n) + p.x * 10 + p.y;\n"
    "}\n"
    "__attribute__((noinline)) long long scalars(int n, int m)\n"
    "{\n"
    "    int x = add3(n, add3(m, add3(1, 2, 3), n), add3(m, 5, 6));\n"
    "    int y = add3(n > 3 ? n : -n, ({ int t = n * 2; t + 1; }), n && m);\n"
    "    int z = add3(seven(), seven(), sum_to(n));\n"
    "    double d = mixf(n, 1.5, (long)m * 3, 0.25f) + mixf(add3(n, m, 1), mixf(1, 2.0, 3, 4.0f), 2, 1.0f);\n"
    "    int s = sgn(n - m) + sgn(m - n) + sgn(0);\n"
    "    int r = ping(n % 7) + pong(m % 5);\n"
    "    int b = 0;\n"
    "    for (int i = 0; i < n; i += 1) bump(&b, i % 3 ? add3(i, 1, 2) : seven());\n"
    "    long double l = ld(ld(1.25L, n), m);\n"
    "    int (*fp)(int, int, int) = add3;\n"
    "    return (long long)x * 1000003 + y * 1009 + z * 17 + (long long)(d * 8) + s * 101 + r * 7 + b + (long long)(l * 4) + fp(n, m, 2);\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    uint8_t buffer[640];\n"
    "    for (int i = 0; i < 640; i += 1) buffer[i] = (uint8_t)(\"abcab\"[i % 5] + (i % 7 == 0));\n"
    "    __m512i a = _mm512_set1_epi32(7);\n"
    "    __m512i b = _mm512_set_epi32(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);\n"
    "    __m512i c = _mm512_set1_epi8(9);\n"
    "    long long out[8];\n"
    "    _mm512_storeu_si512(out, mix(a, b, c));\n"
    "    printf(\"%llx %llx %llu %lld\\n\", out[0], out[7], (unsigned long long)count(buffer, 640), structs(9));\n"
    "    printf(\"%d %d %lld %lld\\n\", narrow(5, 3), narrow(-77, 9), scalars(9, 4), scalars(2, 11));\n"
    "    return 0;\n"
    "}\n";

static const char check_test_tier0_inline_lanes_source[] =
    "#include <stdio.h>\n"
    "#define AI static inline __attribute__((always_inline))\n"
    "AI int f20(int x) { return x * 3 + 20; }\n"
    "AI int f19(int x) { return f20(x + 19) ^ 19; }\n"
    "AI int f18(int x) { return f19(x + 18) ^ 18; }\n"
    "AI int f17(int x) { return f18(x + 17) ^ 17; }\n"
    "AI int f16(int x) { return f17(x + 16) ^ 16; }\n"
    "AI int f15(int x) { return f16(x + 15) ^ 15; }\n"
    "AI int f14(int x) { return f15(x + 14) ^ 14; }\n"
    "AI int f13(int x) { return f14(x + 13) ^ 13; }\n"
    "AI int f12(int x) { return f13(x + 12) ^ 12; }\n"
    "AI int f11(int x) { return f12(x + 11) ^ 11; }\n"
    "AI int f10(int x) { return f11(x + 10) ^ 10; }\n"
    "AI int f09(int x) { return f10(x + 9) ^ 9; }\n"
    "AI int f08(int x) { return f09(x + 8) ^ 8; }\n"
    "AI int f07(int x) { return f08(x + 7) ^ 7; }\n"
    "AI int f06(int x) { return f07(x + 6) ^ 6; }\n"
    "AI int f05(int x) { return f06(x + 5) ^ 5; }\n"
    "AI int f04(int x) { return f05(x + 4) ^ 4; }\n"
    "AI int f03(int x) { return f04(x + 3) ^ 3; }\n"
    "AI int f02(int x) { return f03(x + 2) ^ 2; }\n"
    "AI int f01(int x) { return f02(x + 1) ^ 1; }\n"
    "AI int pong(int n);\n"
    "AI int ping(int n) { return n <= 0 ? 0 : pong(n - 1) + 2; }\n"
    "AI int pong(int n) { return n <= 0 ? 0 : ping(n - 1) + 1; }\n"
    "__attribute__((noinline)) int root00(int x) { return f01(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root01(int x) { return f08(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root02(int x) { return f15(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root03(int x) { return f02(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root04(int x) { return f09(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root05(int x) { return f16(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root06(int x) { return f03(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root07(int x) { return f10(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root08(int x) { return f17(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root09(int x) { return f04(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root10(int x) { return f11(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root11(int x) { return f18(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root12(int x) { return f05(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root13(int x) { return f12(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root14(int x) { return f19(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root15(int x) { return f06(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root16(int x) { return f13(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root17(int x) { return f20(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root18(int x) { return f07(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root19(int x) { return f14(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root20(int x) { return f01(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root21(int x) { return f08(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root22(int x) { return f15(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root23(int x) { return f02(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root24(int x) { return f09(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root25(int x) { return f16(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root26(int x) { return f03(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root27(int x) { return f10(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root28(int x) { return f17(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root29(int x) { return f04(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root30(int x) { return f11(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root31(int x) { return f18(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root32(int x) { return f05(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root33(int x) { return f12(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root34(int x) { return f19(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root35(int x) { return f06(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root36(int x) { return f13(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root37(int x) { return f20(x) + ping(x & 7); }\n"
    "__attribute__((noinline)) int root38(int x) { return f07(x) + pong(x & 7); }\n"
    "__attribute__((noinline)) int root39(int x) { return f14(x) + ping(x & 7); }\n"
    "int main(void)\n"
    "{\n"
    "    long long s = 0;\n"
    "    s = s * 31 + root00(5);\n"
    "    s = s * 31 + root01(18);\n"
    "    s = s * 31 + root02(31);\n"
    "    s = s * 31 + root03(44);\n"
    "    s = s * 31 + root04(57);\n"
    "    s = s * 31 + root05(70);\n"
    "    s = s * 31 + root06(83);\n"
    "    s = s * 31 + root07(96);\n"
    "    s = s * 31 + root08(109);\n"
    "    s = s * 31 + root09(122);\n"
    "    s = s * 31 + root10(135);\n"
    "    s = s * 31 + root11(148);\n"
    "    s = s * 31 + root12(161);\n"
    "    s = s * 31 + root13(174);\n"
    "    s = s * 31 + root14(187);\n"
    "    s = s * 31 + root15(200);\n"
    "    s = s * 31 + root16(213);\n"
    "    s = s * 31 + root17(226);\n"
    "    s = s * 31 + root18(239);\n"
    "    s = s * 31 + root19(252);\n"
    "    s = s * 31 + root20(265);\n"
    "    s = s * 31 + root21(278);\n"
    "    s = s * 31 + root22(291);\n"
    "    s = s * 31 + root23(304);\n"
    "    s = s * 31 + root24(317);\n"
    "    s = s * 31 + root25(330);\n"
    "    s = s * 31 + root26(343);\n"
    "    s = s * 31 + root27(356);\n"
    "    s = s * 31 + root28(369);\n"
    "    s = s * 31 + root29(382);\n"
    "    s = s * 31 + root30(395);\n"
    "    s = s * 31 + root31(408);\n"
    "    s = s * 31 + root32(421);\n"
    "    s = s * 31 + root33(434);\n"
    "    s = s * 31 + root34(447);\n"
    "    s = s * 31 + root35(460);\n"
    "    s = s * 31 + root36(473);\n"
    "    s = s * 31 + root37(486);\n"
    "    s = s * 31 + root38(499);\n"
    "    s = s * 31 + root39(512);\n"
    "    printf(\"%lld\\n\", s);\n"
    "    return 0;\n"
    "}\n";

static void check_test_driver_tier0_inline_lanes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char unit[512];
    char reference[512];
    snprintf(unit, sizeof(unit), "%s/tier0-inline-lanes.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier0-inline-lanes-clang", directory);
    const bool written = parse_test_write(unit, check_test_tier0_inline_lanes_source, strlen(check_test_tier0_inline_lanes_source));
    char* reference_arguments[] = { (char*)clang, "-O0", "-o", reference, unit, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    char expected[256] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of Tier 0's lanes fixture runs");
    // Builds 0 to 2 by 4 lanes, build 3 by 1 lane, builds 4 and 5 by 1 lane's windows of 1 and 64 tokens (#2536: each
    // callee lowered before the bodies, which call it before and after its definition), each an object; then an
    // executable by 4 lanes.
    static const char* const windows[6] = { "--emit-window=0", "--emit-window=0", "--emit-window=0", "--emit-window=0", "--emit-window=1", "--emit-window=64" };
    char objects[6][512];
    bool made[6] = { false, false, false, false, false, false };
    for (uint32_t build = 0; ran && build < 6; build += 1)
    {
        snprintf(objects[build], sizeof(objects[build]), "%s/tier0-inline-lanes-%u.o", directory, build);
        char* arguments[] = { (char*)executable, "--lanes", build < 3 ? "4" : "1", (char*)windows[build], "-c", "-o", objects[build], unit, NULL };
        text = NULL;
        made[build] = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
    }
    bool same = made[0];
    for (uint32_t build = 1; build < 4; build += 1)
    {
        same = same && made[build] && check_test_driver_same_files(objects[0], objects[build]);
    }
    check_test_expect(test, ran && same, "Tier 0's lanes fixture is the same object by 4 lanes, three times, and by 1 lane");
    const bool windowed = made[3] && made[4] && made[5] && check_test_driver_same_files(objects[3], objects[4]) && check_test_driver_same_files(objects[3], objects[5]);
    check_test_expect(test, ran && windowed, "Tier 0's lanes fixture is the same object by 1 lane's windows of 1 and 64 tokens");
    char program[512];
    snprintf(program, sizeof(program), "%s/tier0-inline-lanes", directory);
    char* arguments[] = { (char*)executable, "--lanes", "4", "-o", program, unit, NULL };
    text = NULL;
    const bool built = ran && check_test_driver_spawn(arguments, &text) == 0;
    free(text);
    char actual[256] = { 0 };
    check_test_expect(test, built && check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                      "Tier 0's lanes fixture runs as clang -O0's build does");
    unlink(program);
    for (uint32_t build = 0; build < 6; build += 1)
    {
        if (made[build])
        {
            unlink(objects[build]);
        }
    }
    unlink(reference);
    unlink(unit);
}

static void check_test_driver_tier0_inline(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
    const bool host = parsed && (x86_target_native().features & v4.features) == v4.features;
    char unit[512];
    char reference[512];
    snprintf(unit, sizeof(unit), "%s/tier0-inline.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier0-inline-clang", directory);
    const bool written = parse_test_write(unit, check_test_tier0_inline_source, strlen(check_test_tier0_inline_source));
    char* reference_arguments[] = { (char*)clang, "-O0", "-march=x86-64-v4", "-o", reference, unit, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    char expected[256] = { 0 };
    const bool ran = referenced && (!host || (check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0));
    check_test_expect(test, ran, "clang's build of Tier 0's inlining fixture runs");
    // Builds 0 and 1 inline, by the vector path and the scalar twin; build 2 keeps every call (--tier0-no-inline); builds
    // 3 and 4 are 0 and 1 by windows of one token (#2536, --emit-window=1: every body a window). Each makes an object and
    // an executable.
    char objects[5][512];
    bool made[5] = { false, false, false, false, false };
    for (uint32_t build = 0; ran && build < 5; build += 1)
    {
        const bool scalar = build == 1 || build == 4;
        const bool inlining = build != 2;
        const bool windowed = build >= 3;
        char program[512];
        char message[256];
        snprintf(program, sizeof(program), "%s/tier0-inline-%u", directory, build);
        snprintf(objects[build], sizeof(objects[build]), "%s/tier0-inline-%u.o", directory, build);
        for (uint32_t object = 0; object < 2; object += 1)
        {
            char* arguments[12] = { (char*)executable, "-march=x86-64-v4" };
            uint32_t count = 2;
            if (scalar)
            {
                arguments[count++] = "--scalar";
            }
            if (!inlining)
            {
                arguments[count++] = "--tier0-no-inline";
            }
            if (windowed)
            {
                arguments[count++] = "--emit-window=1";
            }
            if (object != 0)
            {
                arguments[count++] = "-c";
            }
            arguments[count++] = "-o";
            arguments[count++] = object != 0 ? objects[build] : program;
            arguments[count++] = unit;
            arguments[count] = NULL;
            text = NULL;
            const bool built = check_test_driver_spawn(arguments, &text) == 0;
            free(text);
            if (object == 0)
            {
                char actual[256] = { 0 };
                snprintf(message, sizeof(message), "Tier 0's inlining fixture runs as clang -O0's build does (build %u)", build);
                check_test_expect(test, built && (!host || (check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0)), message);
                unlink(program);
            }
            else
            {
                made[build] = built;
                snprintf(message, sizeof(message), "Tier 0's inlining fixture's object builds (build %u)", build);
                check_test_expect(test, built, message);
            }
        }
    }
    check_test_expect(test, made[0] && made[1] && check_test_driver_same_files(objects[0], objects[1]), "the splice's vector path and its scalar twin make the same object");
    check_test_expect(test, made[0] && made[2] && !check_test_driver_same_files(objects[0], objects[2]), "--tier0-no-inline keeps the calls the splice inlines");
    check_test_expect(test, made[0] && made[3] && made[4] && check_test_driver_same_files(objects[0], objects[3]) && check_test_driver_same_files(objects[0], objects[4]),
                      "windows of one token make the object of one window, by both paths");
    for (uint32_t build = 0; build < 5; build += 1)
    {
        if (made[build])
        {
            unlink(objects[build]);
        }
    }
    unlink(reference);
    unlink(unit);
}
