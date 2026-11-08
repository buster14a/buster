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
    // Builds 0 and 1 inline, by the vector path and the scalar twin; build 2 keeps every call (--tier0-no-inline). Each
    // makes an object and an executable.
    char objects[3][512];
    bool made[3] = { false, false, false };
    for (uint32_t build = 0; ran && build < 3; build += 1)
    {
        const bool scalar = build == 1;
        const bool inlining = build != 2;
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
    for (uint32_t build = 0; build < 3; build += 1)
    {
        if (made[build])
        {
            unlink(objects[build]);
        }
    }
    unlink(reference);
    unlink(unit);
}
