// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The vector draws' probe (#2033, #2924; docs/ci.md, "The vector row"): whether this CPU and its OS enable everything
// the explicit vector target uses. tools/ci/vector-draw.sh's `probe` builds it with that target's flags
// (cmake/BusterSettings.cmake's, by the image's cc: the hosted toolchain isn't installed yet) and runs it before a
// candidate does anything else. It asks for x86-64-v4's level and every feature host_isa.h's predicate names;
// __builtin_cpu_supports reads what the OS enables too, so a VM that masks AVX-512 fails it, whatever its model. Its #if
// is the predicate itself, so flags that don't select the vector path don't compile it. tools/ci/changes.c's self-test
// keeps the predicate, these lists and the build's flags in step. Prints a `FEATURE<TAB>yes|no` line each; exits 0 when
// every one is enabled.
#include <stdio.h>

#if !(defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__) && defined(__AVX512DQ__) && defined(__AVX512CD__) && \
    defined(__AVX512VBMI__) && defined(__AVX512VBMI2__) && defined(__AVX512VPOPCNTDQ__) && defined(__AVX512BITALG__) && \
    defined(__AVX512VNNI__) && defined(__GFNI__) && defined(__VAES__) && defined(__AES__) && defined(__BMI__) && \
    defined(__BMI2__) && defined(__LZCNT__) && defined(__POPCNT__) && defined(__AVX2__) && defined(__SSSE3__) && \
    defined(__SSE4_1__) && defined(__XSAVE__))
#error "the vector target's flags don't select host_isa.h's vector path"
#endif

// Prints the feature's line, and returns whether this CPU and its OS enable it.
static int probe_feature(const char* name, int enabled)
{
    printf("%s\t%s\n", name, enabled ? "yes" : "no");
    return enabled != 0;
}

int main(void)
{
    __builtin_cpu_init();
    int qualified = 1;
    qualified &= probe_feature("x86-64-v4", __builtin_cpu_supports("x86-64-v4"));
    qualified &= probe_feature("avx512f", __builtin_cpu_supports("avx512f"));
    qualified &= probe_feature("avx512bw", __builtin_cpu_supports("avx512bw"));
    qualified &= probe_feature("avx512vl", __builtin_cpu_supports("avx512vl"));
    qualified &= probe_feature("avx512dq", __builtin_cpu_supports("avx512dq"));
    qualified &= probe_feature("avx512cd", __builtin_cpu_supports("avx512cd"));
    qualified &= probe_feature("avx512vbmi", __builtin_cpu_supports("avx512vbmi"));
    qualified &= probe_feature("avx512vbmi2", __builtin_cpu_supports("avx512vbmi2"));
    qualified &= probe_feature("avx512vpopcntdq", __builtin_cpu_supports("avx512vpopcntdq"));
    qualified &= probe_feature("avx512bitalg", __builtin_cpu_supports("avx512bitalg"));
    qualified &= probe_feature("avx512vnni", __builtin_cpu_supports("avx512vnni"));
    qualified &= probe_feature("gfni", __builtin_cpu_supports("gfni"));
    qualified &= probe_feature("vaes", __builtin_cpu_supports("vaes"));
    qualified &= probe_feature("aes", __builtin_cpu_supports("aes"));
    qualified &= probe_feature("bmi", __builtin_cpu_supports("bmi"));
    qualified &= probe_feature("bmi2", __builtin_cpu_supports("bmi2"));
    qualified &= probe_feature("lzcnt", __builtin_cpu_supports("lzcnt"));
    qualified &= probe_feature("popcnt", __builtin_cpu_supports("popcnt"));
    qualified &= probe_feature("avx2", __builtin_cpu_supports("avx2"));
    qualified &= probe_feature("ssse3", __builtin_cpu_supports("ssse3"));
    qualified &= probe_feature("sse4.1", __builtin_cpu_supports("sse4.1"));
    qualified &= probe_feature("xsave", __builtin_cpu_supports("xsave"));
    return qualified ? 0 : 1;
}
