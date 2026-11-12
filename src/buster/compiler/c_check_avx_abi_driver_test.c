// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's AVX calling-convention diagnostics (#2367, X86_64TargetCodeGenInfo::checkFunctionCallABI and checkFunctionABI):
// original fixtures compiled with -c by clang and by both paths, whose every line about the fixture (errors, -Wpsabi's
// warnings, their order and locations) and exit status must be clang's, for a unit without AVX and one with AVX2 but not
// AVX-512F, and with -Werror=psabi and -Wno-psabi. Include after c_check_fp_eval_driver_test.c's line reader.

static void check_test_driver_avx_abi(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
    } fixtures[] =
    {
        { "an always_inline intrinsic's wide result and argument after its feature errors",
          "#include <immintrin.h>\n"
          "int v(int x) { __m256i z = _mm256_set1_epi32(x); return _mm256_extract_epi32(_mm256_add_epi32(z, z), 0); }\n" },
        // Calls of 256- and 512-bit vectors, a variadic one's own type, a callee's target attribute that has the
        // feature (an error), a caller's (an error), and an internal callee's warning that clang leaves out.
        { "direct calls' wide arguments and results",
          "typedef long long v4 __attribute__((vector_size(32)));\ntypedef int v16 __attribute__((vector_size(64)));\n"
          "typedef float v8f __attribute__((ext_vector_type(8)));\n"
          "v4 f(v4 a);\nv16 g(v16 a);\nv8f h(int n, ...);\n"
          "__attribute__((target(\"avx\"))) v4 t(v4 a);\n__attribute__((target(\"avx512f\"))) v16 u(v16 a);\n"
          "static v4 k(v4 x) { return x; }\n"
          "v4 caller(v4 x) { return f(x); }\nv16 caller2(v16 x) { return g(x); }\nv8f caller3(v8f y) { return h(1, y, 2); }\n"
          "int caller4(v4 x) { v4 r = t(x); return (int)r[0]; }\n"
          "__attribute__((target(\"avx\"))) int caller5(v4 x) { v4 r = f(x); return (int)r[0]; }\n"
          "__attribute__((target(\"avx2\"))) int caller6(v16 x) { v16 r = u(x); return r[0]; }\n"
          "__attribute__((target(\"avx512f\"))) int caller7(v16 x) { v16 r = u(x); return r[0]; }\n"
          "int caller8(int n) { v4 z = {n}; return (int)k(z)[0]; }\n" },
        // A definition's own: its result's, then its first diagnosed parameter's, an unnamed one's at its end; none for an
        // internal or an inline one clang doesn't emit; an external one hidden or extern inline.
        { "definitions' wide parameters and results",
          "typedef long long v4 __attribute__((vector_size(32)));\n"
          "static v4 k(v4 x) { return x; }\nv4 (*use)(v4) = k;\nstatic v4 unused(v4 x) { return x; }\n"
          "const v4 cq(v4, v4 y) { return y; }\n__attribute__((noinline)) v4\nm(int a, v4 b, v4 c) { return b + c; }\n"
          "inline v4 inl(v4 a) { return a; }\n__attribute__((visibility(\"hidden\"))) v4 hid(v4 x) { return x; }\n"
          "extern inline v4 ei(v4 x) { return x; }\nv4 ret_only(int n) { v4 z = {n}; return z; }\nint arg_only(v4 x) { return (int)x[0]; }\n" },
        // Errors of the calling convention don't end the emission; a target feature's do, deferred functions too.
        { "the calling convention's errors and a target feature's",
          "typedef long long v4 __attribute__((vector_size(32)));\n"
          "__attribute__((target(\"avx\"), always_inline)) static inline int need(int x) { return x; }\n"
          "v4 ext(v4);\n__attribute__((target(\"avx\"))) v4 exta(v4);\n"
          "__attribute__((target(\"avx\"))) static v4 ka(v4 x) { return x; }\n"
          "int c0(int n) { v4 z = {n}; v4 r = exta(z); return (int)r[0]; }\nint c1(int n) { v4 z = {n}; return (int)ka(z)[0]; }\n"
          "static int c5(int n) { v4 z = {n}; v4 r = ext(z); return (int)r[0]; }\nint c6(int n) { return c5(n); }\n"
          "int c7(int n) { return need(n); }\nint c8(int n) { v4 z = {n}; v4 r = exta(z); return (int)r[0]; }\n" },
        // A target_clones body's calls, by its first version's features, once for each copy clang emits.
        { "target_clones bodies' calls",
          "typedef long long v4 __attribute__((vector_size(32)));\nv4 ext(v4);\n"
          "__attribute__((target_clones(\"avx2\", \"default\"))) int tc(int n) { v4 z = {n}; v4 r = ext(z); return (int)r[0]; }\n"
          "__attribute__((target_clones(\"default\", \"avx2\"))) int td(int n) { v4 z = {n}; v4 r = ext(z); return (int)r[0]; }\n" },
        { "a diagnostic pragma's -Wpsabi",
          "typedef long long v4 __attribute__((vector_size(32)));\nv4 ext(v4);\n#pragma clang diagnostic ignored \"-Wpsabi\"\n"
          "int a(int n) { v4 z = {n}; return (int)ext(z)[0]; }\n#pragma clang diagnostic warning \"-Wpsabi\"\n"
          "int b(int n) { v4 z = {n}; return (int)ext(z)[0]; }\n" },
    };
    static const char* const arches[] = { "-march=x86-64", "-march=x86-64-v3" };
    static const char* const options[] = { "-Wpsabi", "-Werror=psabi", "-Wno-psabi" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char source[192];
        char object[192];
        snprintf(source, sizeof(source), "%s/avx-abi-%u.c", directory, i);
        snprintf(object, sizeof(object), "%s/avx-abi-%u.o", directory, i);
        const bool written = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source));
        check_test_expect(test, written, "AVX calling-convention fixture written");
        for (uint32_t a = 0; written && a < BUSTER_ARRAY_LENGTH(arches); a += 1)
        {
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
            {
                char* const theirs_arguments[] = { (char*)clang, "-std=gnu17", (char*)arches[a], (char*)options[o], "-fno-caret-diagnostics", "-fno-color-diagnostics",
                                                   "-c", "-o", "/dev/null", source, NULL };
                char* theirs_text = NULL;
                const int theirs_status = check_test_driver_spawn(theirs_arguments, &theirs_text);
                static char theirs[16384];
                static char ours[16384];
                static char description[2 * 16384 + 512];
                const bool theirs_whole = check_test_driver_fp_source_lines(theirs_text, source, theirs, sizeof(theirs));
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    char* const vector_arguments[] = { (char*)executable, "-std=gnu17", (char*)arches[a], (char*)options[o], "-c", "-o", object, source, NULL };
                    char* const scalar_arguments[] = { (char*)executable, "--scalar", "-std=gnu17", (char*)arches[a], (char*)options[o], "-c", "-o", object, source,
                                                       NULL };
                    char* ours_text = NULL;
                    const int ours_status = check_test_driver_spawn(path == 0 ? vector_arguments : scalar_arguments, &ours_text);
                    const bool ours_whole = check_test_driver_fp_source_lines(ours_text, source, ours, sizeof(ours));
                    snprintf(description, sizeof(description), "%s (%s %s, %s path): -c's lines are clang -c's (status %d and %d)\n-c's:\n%sclang's:\n%s",
                             fixtures[i].name, arches[a], options[o], path == 0 ? "vector" : "scalar", ours_status, theirs_status, ours, theirs);
                    check_test_expect(test, theirs_whole && ours_whole && theirs_status == ours_status && strcmp(ours, theirs) == 0, description);
                    free(ours_text);
                    unlink(object);
                }
                free(theirs_text);
            }
        }
        unlink(source);
    }
}
