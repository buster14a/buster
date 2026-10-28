// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The CPU and feature tables are clang 23.1.3's answers, generated as docs/c-preprocessor.md describes; the options'
// rules and -march=native's detection follow clang's driver and LLVM's Host.cpp, read for their rules
// (THIRD_PARTY_NOTICES.md).
//
// The predefined macros that the target and the command-line options decide, as clang 23 gives them for
// x86_64-linux-gnu (`clang -dM -E` with the same options): the CPU's and its features' macros (-march, the -m
// feature flags, -march=native from CPUID), and those of -O, -f... (PIC, stack protector, floating point, char and
// wchar_t, unwind tables, exceptions, the GNU version, sanitizers, OpenMP, ...), -g, -pthread, -mcmodel and
// -mlong-double. c_preprocessor.c's pp_predefines adds them to its fixed part and the language mode's.
//
// The model, clang's: a CPU has a set of features; the -m flags, in command-line order with only each feature's last
// flag kept (the driver's unification), turn a feature on with everything it implies (its `implies` closure) or off
// with everything that implies it (`implied_by`); then four soft rules turn on popcnt and crc32 with sse4.2, mmx with
// sse and xsave with avx, unless the list turned them off. Each feature has its own macros, but two macros need several
// (__APX_F__ and __APX_INLINE_ASM_USE_GPR32__). The options are read as clang's driver reads them: most families' last
// option wins, and the floating-point ones run through clang's state machine (predefines_floating_point).
//
// This runs once per translation unit, on a few dozen options and two-word feature sets: it has no vector path.
// docs/c-preprocessor.md ("The predefined macros") has its cost.
//
// main.c, c_preprocessor_benchmark.c and c_files_benchmark.c include this file after c_intern.c (for CLanguage) and
// before c_preprocessor.c.
// Layout:
//   - PredefinesFeatures and the generated tables: predefines_features (each feature's flag, closures and macros),
//     predefines_cpus (each -march CPU's macro name and features) and predefines_tunes (the -mtune names).
//   - The host: predefines_host, -march=native's CPU and features from CPUID, as LLVM's Host.cpp finds them.
//   - The options: PredefinesOptions, predefines_options_initialize, the -m options that change no macro
//     (predefines_neutral, the flags and value options clang uses for x86-64 Linux, beside which pp_parse_options
//     refuses every other -m option, and predefines_finish_neutral, their values and combinations checked as clang
//     checks them), clang's -m aliases of other options (predefines_alias) and predefines_option (one argument).
//   - The sanitizers: clang's sanitizers and groups, predefines_sanitizers (clang's SanitizerArgs), predefines_finish
//     (what the whole command line decides, after every option: the sanitizers and the options clang's driver reads
//     only the last of) and predefines_sanitizer_feature (the sanitizer features of the oracle's mode).
//   - The macros: predefines_resolve (the final CPU and features), predefines_text and predefines_flt_eval_method.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cpuid.h>
#include "host_isa.h"
#include "buster/compiler/x86_target.c"

// ---------------------------------------------------------------------------------------------------------------------
// Features and the generated tables

#define BUSTER_PREDEFINES_FEATURE_WORDS 2

// A set of features, by index in predefines_features.
typedef struct PredefinesFeatures PredefinesFeatures;
struct PredefinesFeatures
{
    uint64_t words[BUSTER_PREDEFINES_FEATURE_WORDS];
};

// How the command line names a feature.
typedef enum PredefinesFlag
{
    PREDEFINES_FLAG_NONE, // No flag of its own: a CPU's, implied, or set by an option of its own (cmov, cx8, 64bit, inline-asm-use-gpr32).
    PREDEFINES_FLAG_PLAIN, // -mNAME and -mno-NAME.
    PREDEFINES_FLAG_APX, // A name -mapx-features= and -mno-apx-features= take.
} PredefinesFlag;

typedef struct PredefinesFeature PredefinesFeature;
struct PredefinesFeature
{
    const char* name;
    PredefinesFlag flag;
    // The features turning it on turns on: itself and what it implies, transitively.
    PredefinesFeatures implies;
    // The features turning it off turns off: itself and what implies it, transitively.
    PredefinesFeatures implied_by;
    // Its own macros, as #define lines.
    const char* macros;
};

typedef struct PredefinesCPU PredefinesCPU;
struct PredefinesCPU
{
    const char* name;
    // X in its macros __X, __X__ and __tune_X__, or null for none.
    const char* macro;
    PredefinesFeatures features;
};

// GENERATED: clang 23.1.3's x86-64 features and CPUs, by `./build/compiler test-preprocessor --generate-targets`
// (docs/c-preprocessor.md, "The predefined macros"). Each feature, sorted by name: how the command line names it, its
// `implies` and `implied_by` closures and its own macros. Each -march CPU of x86-64: its macros' name and its features.
static const PredefinesFeature predefines_features[] = {
    { "64bit", PREDEFINES_FLAG_NONE, { { 0x1ull, 0x0ull } }, { { 0x1ull, 0x0ull } }, "" },
    { "adx", PREDEFINES_FLAG_PLAIN, { { 0x2ull, 0x0ull } }, { { 0x2ull, 0x0ull } }, "#define __ADX__ 1\n" },
    { "aes", PREDEFINES_FLAG_PLAIN, { { 0x4ull, 0xc0000000ull } }, { { 0x4ull, 0x20000000000ull } }, "#define __AES__ 1\n" },
    { "amx-avx512", PREDEFINES_FLAG_PLAIN, { { 0xc00002ffdfc08ull, 0x17c0000000ull } }, { { 0x8ull, 0x0ull } }, "#define __AMX_AVX512__ 1\n" },
    { "amx-bf16", PREDEFINES_FLAG_PLAIN, { { 0x410ull, 0x0ull } }, { { 0x10ull, 0x0ull } }, "#define __AMX_BF16__ 1\n" },
    { "amx-complex", PREDEFINES_FLAG_PLAIN, { { 0x420ull, 0x0ull } }, { { 0x20ull, 0x0ull } }, "#define __AMX_COMPLEX__ 1\n" },
    { "amx-fp16", PREDEFINES_FLAG_PLAIN, { { 0x440ull, 0x0ull } }, { { 0x40ull, 0x0ull } }, "#define __AMX_FP16__ 1\n" },
    { "amx-fp8", PREDEFINES_FLAG_PLAIN, { { 0x480ull, 0x0ull } }, { { 0x80ull, 0x0ull } }, "#define __AMX_FP8__ 1\n" },
    { "amx-int8", PREDEFINES_FLAG_PLAIN, { { 0x500ull, 0x0ull } }, { { 0x100ull, 0x0ull } }, "#define __AMX_INT8__ 1\n" },
    { "amx-movrs", PREDEFINES_FLAG_PLAIN, { { 0x600ull, 0x0ull } }, { { 0x200ull, 0x0ull } }, "#define __AMX_MOVRS__ 1\n" },
    { "amx-tile", PREDEFINES_FLAG_PLAIN, { { 0x400ull, 0x0ull } }, { { 0x7f8ull, 0x0ull } }, "#define __AMX_TILE__ 1\n" },
    { "avx", PREDEFINES_FLAG_PLAIN, { { 0x800ull, 0x17c0000000ull } }, { { 0x1c0007fffff808ull, 0x860034000000ull } }, "#define __AVX__ 1\n" },
    { "avx10.1", PREDEFINES_FLAG_PLAIN, { { 0xc00002ffdd800ull, 0x17c0000000ull } }, { { 0x3008ull, 0x0ull } }, "#define __AVX10_1_512__ 1\n#define __AVX10_1__ 1\n" },
    { "avx10.2", PREDEFINES_FLAG_PLAIN, { { 0xc00002ffdf800ull, 0x17c0000000ull } }, { { 0x2008ull, 0x0ull } }, "#define __AVX10_2_512__ 1\n#define __AVX10_2__ 1\n" },
    { "avx2", PREDEFINES_FLAG_PLAIN, { { 0x4800ull, 0x17c0000000ull } }, { { 0x7fffff008ull, 0x20024000000ull } }, "#define __AVX2__ 1\n" },
    { "avx512bf16", PREDEFINES_FLAG_PLAIN, { { 0xc00000024c800ull, 0x17c0000000ull } }, { { 0xb008ull, 0x0ull } }, "#define __AVX512BF16__ 1\n" },
    { "avx512bitalg", PREDEFINES_FLAG_PLAIN, { { 0xc000000254800ull, 0x17c0000000ull } }, { { 0x13008ull, 0x0ull } }, "#define __AVX512BITALG__ 1\n" },
    { "avx512bmm", PREDEFINES_FLAG_PLAIN, { { 0xc000000264800ull, 0x17c0000000ull } }, { { 0x20000ull, 0x0ull } }, "#define __AVX512BMM__ 1\n" },
    { "avx512bw", PREDEFINES_FLAG_PLAIN, { { 0xc000000244800ull, 0x17c0000000ull } }, { { 0x347b008ull, 0x0ull } }, "#define __AVX512BW__ 1\n" },
    { "avx512cd", PREDEFINES_FLAG_PLAIN, { { 0xc000000284800ull, 0x17c0000000ull } }, { { 0x83008ull, 0x0ull } }, "#define __AVX512CD__ 1\n" },
    { "avx512dq", PREDEFINES_FLAG_PLAIN, { { 0xc000000304800ull, 0x17c0000000ull } }, { { 0x103008ull, 0x0ull } }, "#define __AVX512DQ__ 1\n" },
    { "avx512f", PREDEFINES_FLAG_PLAIN, { { 0xc000000204800ull, 0x17c0000000ull } }, { { 0x3fffb008ull, 0x0ull } }, "#define __AVX512F__ 1\n" },
    { "avx512fp16", PREDEFINES_FLAG_PLAIN, { { 0xc000000644800ull, 0x17c0000000ull } }, { { 0x403008ull, 0x0ull } }, "#define __AVX512FP16__ 1\n" },
    { "avx512ifma", PREDEFINES_FLAG_PLAIN, { { 0xc000000a04800ull, 0x17c0000000ull } }, { { 0x803008ull, 0x0ull } }, "#define __AVX512IFMA__ 1\n" },
    { "avx512vbmi", PREDEFINES_FLAG_PLAIN, { { 0xc000001244800ull, 0x17c0000000ull } }, { { 0x1003008ull, 0x0ull } }, "#define __AVX512VBMI__ 1\n" },
    { "avx512vbmi2", PREDEFINES_FLAG_PLAIN, { { 0xc000002244800ull, 0x17c0000000ull } }, { { 0x2003008ull, 0x0ull } }, "#define __AVX512VBMI2__ 1\n" },
    { "avx512vl", PREDEFINES_FLAG_PLAIN, { { 0xc000004204800ull, 0x17c0000000ull } }, { { 0x4003008ull, 0x0ull } }, "#define __AVX512VL__ 1\n" },
    { "avx512vnni", PREDEFINES_FLAG_PLAIN, { { 0xc000008204800ull, 0x17c0000000ull } }, { { 0x8003008ull, 0x0ull } }, "#define __AVX512VNNI__ 1\n" },
    { "avx512vp2intersect", PREDEFINES_FLAG_PLAIN, { { 0xc000010204800ull, 0x17c0000000ull } }, { { 0x10000000ull, 0x0ull } }, "#define __AVX512VP2INTERSECT__ 1\n" },
    { "avx512vpopcntdq", PREDEFINES_FLAG_PLAIN, { { 0xc000020204800ull, 0x17c0000000ull } }, { { 0x20003008ull, 0x0ull } }, "#define __AVX512VPOPCNTDQ__ 1\n" },
    { "avxifma", PREDEFINES_FLAG_PLAIN, { { 0x40004800ull, 0x17c0000000ull } }, { { 0x40000000ull, 0x0ull } }, "#define __AVXIFMA__ 1\n" },
    { "avxneconvert", PREDEFINES_FLAG_PLAIN, { { 0x80004800ull, 0x17c0000000ull } }, { { 0x80000000ull, 0x0ull } }, "#define __AVXNECONVERT__ 1\n" },
    { "avxvnni", PREDEFINES_FLAG_PLAIN, { { 0x100004800ull, 0x17c0000000ull } }, { { 0x100000000ull, 0x0ull } }, "#define __AVXVNNI__ 1\n" },
    { "avxvnniint16", PREDEFINES_FLAG_PLAIN, { { 0x200004800ull, 0x17c0000000ull } }, { { 0x200000000ull, 0x0ull } }, "#define __AVXVNNIINT16__ 1\n" },
    { "avxvnniint8", PREDEFINES_FLAG_PLAIN, { { 0x400004800ull, 0x17c0000000ull } }, { { 0x400000000ull, 0x0ull } }, "#define __AVXVNNIINT8__ 1\n" },
    { "bmi", PREDEFINES_FLAG_PLAIN, { { 0x800000000ull, 0x0ull } }, { { 0x800000000ull, 0x0ull } }, "#define __BMI__ 1\n" },
    { "bmi2", PREDEFINES_FLAG_PLAIN, { { 0x1000000000ull, 0x0ull } }, { { 0x1000000000ull, 0x0ull } }, "#define __BMI2__ 1\n" },
    { "ccmp", PREDEFINES_FLAG_APX, { { 0x2000000000ull, 0x0ull } }, { { 0x2000000000ull, 0x0ull } }, "#define __CCMP__ 1\n" },
    { "cf", PREDEFINES_FLAG_APX, { { 0x4000000000ull, 0x0ull } }, { { 0x4000000000ull, 0x0ull } }, "#define __CF__ 1\n" },
    { "cldemote", PREDEFINES_FLAG_PLAIN, { { 0x8000000000ull, 0x0ull } }, { { 0x8000000000ull, 0x0ull } }, "#define __CLDEMOTE__ 1\n" },
    { "clflushopt", PREDEFINES_FLAG_PLAIN, { { 0x10000000000ull, 0x0ull } }, { { 0x10000000000ull, 0x0ull } }, "#define __CLFLUSHOPT__ 1\n" },
    { "clwb", PREDEFINES_FLAG_PLAIN, { { 0x20000000000ull, 0x0ull } }, { { 0x20000000000ull, 0x0ull } }, "#define __CLWB__ 1\n" },
    { "clzero", PREDEFINES_FLAG_PLAIN, { { 0x40000000000ull, 0x0ull } }, { { 0x40000000000ull, 0x0ull } }, "#define __CLZERO__ 1\n" },
    { "cmov", PREDEFINES_FLAG_NONE, { { 0x80000000000ull, 0x0ull } }, { { 0x80000000000ull, 0x0ull } }, "" },
    { "cmpccxadd", PREDEFINES_FLAG_PLAIN, { { 0x100000000000ull, 0x0ull } }, { { 0x100000000000ull, 0x0ull } }, "#define __CMPCCXADD__ 1\n" },
    { "crc32", PREDEFINES_FLAG_PLAIN, { { 0x200000000000ull, 0x0ull } }, { { 0x200000000000ull, 0x0ull } }, "#define __CRC32__ 1\n" },
    { "cx16", PREDEFINES_FLAG_PLAIN, { { 0x400000000000ull, 0x0ull } }, { { 0x400000000000ull, 0x0ull } }, "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_16 1\n" },
    { "cx8", PREDEFINES_FLAG_NONE, { { 0x800000000000ull, 0x0ull } }, { { 0x800000000000ull, 0x0ull } }, "" },
    { "egpr", PREDEFINES_FLAG_APX, { { 0x1000000000000ull, 0x0ull } }, { { 0x1000000000000ull, 0x0ull } }, "#define __EGPR__ 1\n" },
    { "enqcmd", PREDEFINES_FLAG_PLAIN, { { 0x2000000000000ull, 0x0ull } }, { { 0x2000000000000ull, 0x0ull } }, "#define __ENQCMD__ 1\n" },
    { "f16c", PREDEFINES_FLAG_PLAIN, { { 0x4000000000800ull, 0x17c0000000ull } }, { { 0x400003fffb008ull, 0x0ull } }, "#define __F16C__ 1\n" },
    { "fma", PREDEFINES_FLAG_PLAIN, { { 0x8000000000800ull, 0x17c0000000ull } }, { { 0x800003fffb008ull, 0x0ull } }, "#define __FMA__ 1\n" },
    { "fma4", PREDEFINES_FLAG_PLAIN, { { 0x10000000000800ull, 0x1fc0000000ull } }, { { 0x10000000000000ull, 0x800000000000ull } }, "#define __FMA4__ 1\n" },
    { "fsgsbase", PREDEFINES_FLAG_PLAIN, { { 0x20000000000000ull, 0x0ull } }, { { 0x20000000000000ull, 0x0ull } }, "#define __FSGSBASE__ 1\n" },
    { "fxsr", PREDEFINES_FLAG_PLAIN, { { 0x40000000000000ull, 0x0ull } }, { { 0x40000000000000ull, 0x0ull } }, "#define __FXSR__ 1\n" },
    { "gfni", PREDEFINES_FLAG_PLAIN, { { 0x80000000000000ull, 0xc0000000ull } }, { { 0x80000000000000ull, 0x0ull } }, "#define __GFNI__ 1\n" },
    { "hreset", PREDEFINES_FLAG_PLAIN, { { 0x100000000000000ull, 0x0ull } }, { { 0x100000000000000ull, 0x0ull } }, "#define __HRESET__ 1\n" },
    { "inline-asm-use-gpr32", PREDEFINES_FLAG_NONE, { { 0x200000000000000ull, 0x0ull } }, { { 0x200000000000000ull, 0x0ull } }, "" },
    { "invpcid", PREDEFINES_FLAG_PLAIN, { { 0x400000000000000ull, 0x0ull } }, { { 0x400000000000000ull, 0x0ull } }, "#define __INVPCID__ 1\n" },
    { "jmpabs", PREDEFINES_FLAG_APX, { { 0x800000000000000ull, 0x0ull } }, { { 0x800000000000000ull, 0x0ull } }, "#define __JMPABS__ 1\n" },
    { "kl", PREDEFINES_FLAG_PLAIN, { { 0x1000000000000000ull, 0xc0000000ull } }, { { 0x1000000000000000ull, 0x200000000000ull } }, "#define __KL__ 1\n" },
    { "lwp", PREDEFINES_FLAG_PLAIN, { { 0x2000000000000000ull, 0x0ull } }, { { 0x2000000000000000ull, 0x0ull } }, "#define __LWP__ 1\n" },
    { "lzcnt", PREDEFINES_FLAG_PLAIN, { { 0x4000000000000000ull, 0x0ull } }, { { 0x4000000000000000ull, 0x0ull } }, "#define __LZCNT__ 1\n" },
    { "mmx", PREDEFINES_FLAG_PLAIN, { { 0x8000000000000000ull, 0x0ull } }, { { 0x8000000000000000ull, 0x0ull } }, "#define __MMX__ 1\n" },
    { "movbe", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x1ull } }, { { 0x0ull, 0x1ull } }, "#define __MOVBE__ 1\n" },
    { "movdir64b", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x2ull } }, { { 0x0ull, 0x2ull } }, "#define __MOVDIR64B__ 1\n" },
    { "movdiri", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x4ull } }, { { 0x0ull, 0x4ull } }, "#define __MOVDIRI__ 1\n" },
    { "movrs", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x8ull } }, { { 0x0ull, 0x8ull } }, "#define __MOVRS__ 1\n" },
    { "mwaitx", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x10ull } }, { { 0x0ull, 0x10ull } }, "#define __MWAITX__ 1\n" },
    { "ndd", PREDEFINES_FLAG_APX, { { 0x0ull, 0x20ull } }, { { 0x0ull, 0x20ull } }, "#define __NDD__ 1\n" },
    { "nf", PREDEFINES_FLAG_APX, { { 0x0ull, 0x40ull } }, { { 0x0ull, 0x40ull } }, "#define __NF__ 1\n" },
    { "pclmul", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0xc0000080ull } }, { { 0x0ull, 0x40000000080ull } }, "#define __PCLMUL__ 1\n" },
    { "pconfig", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x100ull } }, { { 0x0ull, 0x100ull } }, "#define __PCONFIG__ 1\n" },
    { "pku", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x200ull } }, { { 0x0ull, 0x200ull } }, "#define __PKU__ 1\n" },
    { "popcnt", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x400ull } }, { { 0x0ull, 0x400ull } }, "#define __POPCNT__ 1\n" },
    { "ppx", PREDEFINES_FLAG_APX, { { 0x0ull, 0x800ull } }, { { 0x0ull, 0x800ull } }, "#define __PPX__ 1\n" },
    { "prefetchi", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x1000ull } }, { { 0x0ull, 0x1000ull } }, "#define __PREFETCHI__ 1\n" },
    { "prfchw", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x2000ull } }, { { 0x0ull, 0x2000ull } }, "#define __PRFCHW__ 1\n" },
    { "ptwrite", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x4000ull } }, { { 0x0ull, 0x4000ull } }, "#define __PTWRITE__ 1\n" },
    { "push2pop2", PREDEFINES_FLAG_APX, { { 0x0ull, 0x8000ull } }, { { 0x0ull, 0x8000ull } }, "#define __PUSH2POP2__ 1\n" },
    { "raoint", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x10000ull } }, { { 0x0ull, 0x10000ull } }, "#define __RAOINT__ 1\n" },
    { "rdpid", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x20000ull } }, { { 0x0ull, 0x20000ull } }, "#define __RDPID__ 1\n" },
    { "rdpru", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x40000ull } }, { { 0x0ull, 0x40000ull } }, "#define __RDPRU__ 1\n" },
    { "rdrnd", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x80000ull } }, { { 0x0ull, 0x80000ull } }, "#define __RDRND__ 1\n" },
    { "rdseed", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x100000ull } }, { { 0x0ull, 0x100000ull } }, "#define __RDSEED__ 1\n" },
    { "rtm", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x200000ull } }, { { 0x0ull, 0x200000ull } }, "#define __RTM__ 1\n" },
    { "sahf", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x400000ull } }, { { 0x0ull, 0x400000ull } }, "#define __LAHF_SAHF__ 1\n" },
    { "serialize", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x800000ull } }, { { 0x0ull, 0x800000ull } }, "#define __SERIALIZE__ 1\n" },
    { "sgx", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x1000000ull } }, { { 0x0ull, 0x1000000ull } }, "#define __SGX__ 1\n" },
    { "sha", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0xc2000000ull } }, { { 0x0ull, 0x2000000ull } }, "#define __SHA__ 1\n" },
    { "sha512", PREDEFINES_FLAG_PLAIN, { { 0x4800ull, 0x17c4000000ull } }, { { 0x0ull, 0x4000000ull } }, "#define __SHA512__ 1\n" },
    { "shstk", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x8000000ull } }, { { 0x0ull, 0x8000000ull } }, "#define __SHSTK__ 1\n" },
    { "sm3", PREDEFINES_FLAG_PLAIN, { { 0x800ull, 0x17d0000000ull } }, { { 0x0ull, 0x10000000ull } }, "#define __SM3__ 1\n" },
    { "sm4", PREDEFINES_FLAG_PLAIN, { { 0x4800ull, 0x17e0000000ull } }, { { 0x0ull, 0x20000000ull } }, "#define __SM4__ 1\n" },
    { "sse", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x40000000ull } }, { { 0x109c0007fffff80cull, 0xa61ff6000080ull } }, "#define __SSE_MATH__ 1\n#define __SSE__ 1\n" },
    { "sse2", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0xc0000000ull } }, { { 0x109c0007fffff80cull, 0xa61fb6000080ull } }, "#define __FLT16_DECIMAL_DIG__ 5\n#define __FLT16_DENORM_MIN__ 5.9604644775390625e-8F16\n#define __FLT16_DIG__ 3\n#define __FLT16_EPSILON__ 9.765625e-4F16\n#define __FLT16_HAS_DENORM__ 1\n#define __FLT16_HAS_INFINITY__ 1\n#define __FLT16_HAS_QUIET_NAN__ 1\n#define __FLT16_MANT_DIG__ 11\n#define __FLT16_MAX_10_EXP__ 4\n#define __FLT16_MAX_EXP__ 16\n#define __FLT16_MAX__ 6.5504e+4F16\n#define __FLT16_MIN_10_EXP__ (-4)\n#define __FLT16_MIN_EXP__ (-13)\n#define __FLT16_MIN__ 6.103515625e-5F16\n#define __FLT16_NORM_MAX__ 6.5504e+4F16\n#define __SSE2_MATH__ 1\n#define __SSE2__ 1\n" },
    { "sse3", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x1c0000000ull } }, { { 0x1c0007fffff808ull, 0x861f34000000ull } }, "#define __SSE3__ 1\n" },
    { "sse4.1", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x13c0000000ull } }, { { 0x1c0007fffff808ull, 0x860634000000ull } }, "#define __SSE4_1__ 1\n" },
    { "sse4.2", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x17c0000000ull } }, { { 0x1c0007fffff808ull, 0x860434000000ull } }, "#define __SSE4_2__ 1\n" },
    { "sse4a", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x9c0000000ull } }, { { 0x10000000000000ull, 0x800800000000ull } }, "#define __SSE4A__ 1\n" },
    { "ssse3", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x11c0000000ull } }, { { 0x1c0007fffff808ull, 0x861634000000ull } }, "#define __SSSE3__ 1\n" },
    { "tbm", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x2000000000ull } }, { { 0x0ull, 0x2000000000ull } }, "#define __TBM__ 1\n" },
    { "tsxldtrk", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x4000000000ull } }, { { 0x0ull, 0x4000000000ull } }, "#define __TSXLDTRK__ 1\n" },
    { "uintr", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x8000000000ull } }, { { 0x0ull, 0x8000000000ull } }, "#define __UINTR__ 1\n" },
    { "usermsr", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x10000000000ull } }, { { 0x0ull, 0x10000000000ull } }, "#define __USERMSR__ 1\n" },
    { "vaes", PREDEFINES_FLAG_PLAIN, { { 0x4804ull, 0x217c0000000ull } }, { { 0x0ull, 0x20000000000ull } }, "#define __VAES__ 1\n" },
    { "vpclmulqdq", PREDEFINES_FLAG_PLAIN, { { 0x800ull, 0x417c0000080ull } }, { { 0x0ull, 0x40000000000ull } }, "#define __VPCLMULQDQ__ 1\n" },
    { "waitpkg", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x80000000000ull } }, { { 0x0ull, 0x80000000000ull } }, "#define __WAITPKG__ 1\n" },
    { "wbnoinvd", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x100000000000ull } }, { { 0x0ull, 0x100000000000ull } }, "#define __WBNOINVD__ 1\n" },
    { "widekl", PREDEFINES_FLAG_PLAIN, { { 0x1000000000000000ull, 0x2000c0000000ull } }, { { 0x0ull, 0x200000000000ull } }, "#define __WIDEKL__ 1\n" },
    { "x87", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x400000000000ull } }, { { 0x0ull, 0x400000000000ull } }, "" },
    { "xop", PREDEFINES_FLAG_PLAIN, { { 0x10000000000800ull, 0x801fc0000000ull } }, { { 0x0ull, 0x800000000000ull } }, "#define __XOP__ 1\n" },
    { "xsave", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x1000000000000ull } }, { { 0x0ull, 0xf000000000000ull } }, "#define __XSAVE__ 1\n" },
    { "xsavec", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x3000000000000ull } }, { { 0x0ull, 0x2000000000000ull } }, "#define __XSAVEC__ 1\n" },
    { "xsaveopt", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x5000000000000ull } }, { { 0x0ull, 0x4000000000000ull } }, "#define __XSAVEOPT__ 1\n" },
    { "xsaves", PREDEFINES_FLAG_PLAIN, { { 0x0ull, 0x9000000000000ull } }, { { 0x0ull, 0x8000000000000ull } }, "#define __XSAVES__ 1\n" },
    { "zu", PREDEFINES_FLAG_APX, { { 0x0ull, 0x10000000000000ull } }, { { 0x0ull, 0x10000000000000ull } }, "#define __ZU__ 1\n" },
};

static const PredefinesCPU predefines_cpus[] = {
    { "nocona", "nocona", { { 0x8040c80000000000ull, 0x4001c0000000ull } } },
    { "core2", "core2", { { 0x8040c80000000000ull, 0x4011c0400000ull } } },
    { "penryn", "core2", { { 0x8040c80000000000ull, 0x4013c0400000ull } } },
    { "bonnell", "atom", { { 0x8040c80000000000ull, 0x4011c0400001ull } } },
    { "atom", "atom", { { 0x8040c80000000000ull, 0x4011c0400001ull } } },
    { "silvermont", "slm", { { 0x8040e80000000000ull, 0x4017c0482481ull } } },
    { "slm", "slm", { { 0x8040e80000000000ull, 0x4017c0482481ull } } },
    { "goldmont", "goldmont", { { 0x8060e90000000004ull, 0xf4017c2582481ull } } },
    { "goldmont-plus", "goldmont_plus", { { 0x8060e90000000004ull, 0xf4017c35a6481ull } } },
    { "tremont", "tremont", { { 0x80e0eb0000000004ull, 0xf4017c35a6481ull } } },
    { "nehalem", "corei7", { { 0x8040e80000000000ull, 0x4017c0400400ull } } },
    { "corei7", "corei7", { { 0x8040e80000000000ull, 0x4017c0400400ull } } },
    { "westmere", "corei7", { { 0x8040e80000000000ull, 0x4017c0400480ull } } },
    { "sandybridge", "corei7", { { 0x8040e80000000800ull, 0x54017c0400480ull } } },
    { "corei7-avx", "corei7", { { 0x8040e80000000800ull, 0x54017c0400480ull } } },
    { "ivybridge", "corei7", { { 0x8064e80000000800ull, 0x54017c0480480ull } } },
    { "core-avx-i", "corei7", { { 0x8064e80000000800ull, 0x54017c0480480ull } } },
    { "haswell", "corei7", { { 0xc46ce81800004800ull, 0x54017c0480481ull } } },
    { "core-avx2", "corei7", { { 0xc46ce81800004800ull, 0x54017c0480481ull } } },
    { "broadwell", "corei7", { { 0xc46ce81800004802ull, 0x54017c0582481ull } } },
    { "skylake", "corei7", { { 0xc46ce91800004806ull, 0xf4017c1582481ull } } },
    { "skylake-avx512", "corei7", { { 0xc46ceb18043c4806ull, 0xf4017c0582681ull } } },
    { "skx", "corei7", { { 0xc46ceb18043c4806ull, 0xf4017c0582681ull } } },
    { "cascadelake", "corei7", { { 0xc46ceb180c3c4806ull, 0xf4017c0582681ull } } },
    { "cooperlake", "corei7", { { 0xc46ceb180c3cc806ull, 0xf4017c0582681ull } } },
    { "cannonlake", "corei7", { { 0xc46ce91805bc4806ull, 0xf4017c3582681ull } } },
    { "icelake-client", "corei7", { { 0xc4ece9182fbd4806ull, 0xf4617c35a2681ull } } },
    { "rocketlake", "corei7", { { 0xc4ece9182fbd4806ull, 0xf4617c25a2681ull } } },
    { "icelake-server", "corei7", { { 0xc4eceb182fbd4806ull, 0xf5617c35a2781ull } } },
    { "tigerlake", "corei7", { { 0xd4eceb183fbd4806ull, 0xf6617cb5a2687ull } } },
    { "sapphirerapids", "corei7", { { 0xc4eeeb992ffdcd16ull, 0xf5ed7cbda6787ull } } },
    { "alderlake", "corei7", { { 0xd5eceb1900004806ull, 0xf6e17cbda6787ull } } },
    { "raptorlake", "corei7", { { 0xd5eceb1900004806ull, 0xf6e17cbda6787ull } } },
    { "meteorlake", "corei7", { { 0xd5eceb1900004806ull, 0xf6e17cbda6787ull } } },
    { "arrowlake", "corei7", { { 0xd5eefb1dc0004806ull, 0xf6e97cbda6787ull } } },
    { "arrowlake-s", "corei7", { { 0xd5eefb1fc0004806ull, 0xf6e97ffda6787ull } } },
    { "lunarlake", "corei7", { { 0xd5eefb1fc0004806ull, 0xf6e97ffda6787ull } } },
    { "gracemont", "corei7", { { 0xd5eceb1900004806ull, 0xf6e17cbda6787ull } } },
    { "pantherlake", "corei7", { { 0xc5eefb1fc0004806ull, 0xf4e97ffda6787ull } } },
    { "wildcatlake", "corei7", { { 0xc5eefb1fc0004806ull, 0xf4e97ffda6787ull } } },
    { "novalake", "corei7", { { 0xcdeffb3feffdf806ull, 0x1f4e97ffdaffefull } } },
    { "sierraforest", "corei7", { { 0xd5eefb9dc0004806ull, 0xf6e97cbda6787ull } } },
    { "grandridge", "corei7", { { 0xd5eefb9dc0004806ull, 0xf6e97cbda6787ull } } },
    { "graniterapids", "corei7", { { 0xc4eeeb992ffdcd56ull, 0xf5ed7cbda7787ull } } },
    { "graniterapids-d", "corei7", { { 0xc4eeeb992ffdcd76ull, 0xf5ed7cbda7787ull } } },
    { "emeraldrapids", "corei7", { { 0xc4eeeb992ffdcd16ull, 0xf5ed7cbda6787ull } } },
    { "clearwaterforest", "corei7", { { 0xc5eefb9fc0004806ull, 0xf4f97ffda7787ull } } },
    { "diamondrapids", "corei7", { { 0xcceffbbfeffdfffeull, 0x1f5ed7ffdaffefull } } },
    { "knl", "knl", { { 0xc46ce81800284806ull, 0x54017c0582481ull } } },
    { "knm", 0, { { 0xc46ce81820284806ull, 0x54017c0582481ull } } },
    { "k8", "k8", { { 0x8040800000000000ull, 0x4000c0002000ull } } },
    { "athlon64", "k8", { { 0x8040800000000000ull, 0x4000c0002000ull } } },
    { "athlon-fx", "k8", { { 0x8040800000000000ull, 0x4000c0002000ull } } },
    { "opteron", "k8", { { 0x8040800000000000ull, 0x4000c0002000ull } } },
    { "k8-sse3", "k8", { { 0x8040800000000000ull, 0x4001c0002000ull } } },
    { "athlon64-sse3", "k8", { { 0x8040800000000000ull, 0x4001c0002000ull } } },
    { "opteron-sse3", "k8", { { 0x8040800000000000ull, 0x4001c0002000ull } } },
    { "amdfam10", "amdfam10", { { 0xc040c00000000000ull, 0x4009c0402400ull } } },
    { "barcelona", "amdfam10", { { 0xc040c00000000000ull, 0x4009c0402400ull } } },
    { "btver1", "btver1", { { 0xc040c00000000000ull, 0x4019c0402400ull } } },
    { "btver2", "btver2", { { 0xc044e00800000804ull, 0x5401fc0402481ull } } },
    { "bdver1", "bdver1", { { 0xe050e00000000804ull, 0x1c01fc0402480ull } } },
    { "bdver2", "bdver2", { { 0xe05ce00800000804ull, 0x1c03fc0402480ull } } },
    { "bdver3", "bdver3", { { 0xe07ce00800000804ull, 0x5c03fc0402480ull } } },
    { "bdver4", "bdver4", { { 0xe07ce01800004804ull, 0x5c03fc0482491ull } } },
    { "znver1", "znver1", { { 0xc06ce51800004806ull, 0xf401fc2582491ull } } },
    { "znver2", "znver2", { { 0xc06ce71800004806ull, 0xf501fc25e2491ull } } },
    { "znver3", "znver3", { { 0xc46ce71800004806ull, 0xf561fc25e2691ull } } },
    { "znver4", "znver4", { { 0xc4ece7182fbdc806ull, 0xf561fca5e2691ull } } },
    { "znver5", "znver5", { { 0xc4ece7193fbdc806ull, 0xf561fca5e3697ull } } },
    { "znver6", "znver6", { { 0xc4ece71dffffc806ull, 0xf561fca5e3697ull } } },
    { "c86-4g-m4", "c86_4g_m4", { { 0xc06ce51800004806ull, 0xf401fc2582491ull } } },
    { "c86-4g-m6", "c86_4g_m6", { { 0xc06ce51800004806ull, 0xf401fc2582491ull } } },
    { "c86-4g-m7", "c86_4g_m7", { { 0xc0ecef182fbdc806ull, 0xf561fc2582491ull } } },
    { "c86-4g-m8", "c86_4g_m8", { { 0xc0ecef182fbdc806ull, 0xf561fca582491ull } } },
    { "x86-64", "k8", { { 0x8040880000000000ull, 0x4000c0000000ull } } },
    { "x86-64-v2", 0, { { 0x8040e80000000000ull, 0x4017c0400400ull } } },
    { "x86-64-v3", 0, { { 0xc04ce81800004800ull, 0x14017c0400401ull } } },
    { "x86-64-v4", 0, { { 0xc04ce818043c4800ull, 0x14017c0400401ull } } },
};

// The -mtune names (clang's for x86-64, 32-bit CPUs among them; "generic" and "native" too).
static const char* const predefines_tunes[] = {
    "i386", "i486", "winchip-c6", "winchip2", "c3", "i586", "pentium", "pentium-mmx", "pentiumpro", "i686", "pentium2", "pentium3", "pentium3m",
    "pentium-m", "c3-2", "yonah", "pentium4", "pentium4m", "prescott", "nocona", "core2", "penryn", "bonnell", "atom", "silvermont", "slm",
    "goldmont", "goldmont-plus", "tremont", "nehalem", "corei7", "westmere", "sandybridge", "corei7-avx", "ivybridge", "core-avx-i", "haswell",
    "core-avx2", "broadwell", "skylake", "skylake-avx512", "skx", "cascadelake", "cooperlake", "cannonlake", "icelake-client", "rocketlake",
    "icelake-server", "tigerlake", "sapphirerapids", "alderlake", "raptorlake", "meteorlake", "arrowlake", "arrowlake-s", "lunarlake", "gracemont",
    "pantherlake", "wildcatlake", "novalake", "sierraforest", "grandridge", "graniterapids", "graniterapids-d", "emeraldrapids", "clearwaterforest",
    "diamondrapids", "knl", "knm", "lakemont", "k6", "k6-2", "k6-3", "athlon", "athlon-tbird", "athlon-xp", "athlon-mp", "athlon-4", "k8", "athlon64",
    "athlon-fx", "opteron", "k8-sse3", "athlon64-sse3", "opteron-sse3", "amdfam10", "barcelona", "btver1", "btver2", "bdver1", "bdver2", "bdver3",
    "bdver4", "znver1", "znver2", "znver3", "znver4", "znver5", "znver6", "c86-4g-m4", "c86-4g-m6", "c86-4g-m7", "c86-4g-m8", "x86-64", "geode",
};
// END GENERATED

static bool predefines_has(PredefinesFeatures set, uint32_t feature)
{
    return (set.words[feature / 64] >> (feature % 64)) & 1;
}

static void predefines_add(PredefinesFeatures* set, PredefinesFeatures more)
{
    for (uint32_t w = 0; w < BUSTER_PREDEFINES_FEATURE_WORDS; w += 1)
    {
        set->words[w] |= more.words[w];
    }
}

static void predefines_remove(PredefinesFeatures* set, PredefinesFeatures less)
{
    for (uint32_t w = 0; w < BUSTER_PREDEFINES_FEATURE_WORDS; w += 1)
    {
        set->words[w] &= ~less.words[w];
    }
}

// The index of the feature named [name, name + length), or -1.
static int32_t predefines_feature_index(const char* name, size_t length)
{
    int32_t result = -1;
    uint32_t low = 0;
    uint32_t high = BUSTER_ARRAY_LENGTH(predefines_features);
    while (low < high && result < 0)
    {
        const uint32_t middle = low + (high - low) / 2;
        const char* entry = predefines_features[middle].name;
        const size_t entry_length = strlen(entry);
        const int compared = memcmp(name, entry, length < entry_length ? length : entry_length);
        const int order = compared != 0 ? compared : length < entry_length ? -1 : length > entry_length ? 1 : 0;
        if (order == 0)
        {
            result = (int32_t)middle;
        }
        else if (order < 0)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    return result;
}

// The index of the feature `name` (which the tables have).
static uint32_t predefines_feature(const char* name)
{
    return (uint32_t)predefines_feature_index(name, strlen(name));
}

static int32_t predefines_cpu_index(const char* name)
{
    int32_t result = -1;
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(predefines_cpus) && result < 0; c += 1)
    {
        result = strcmp(predefines_cpus[c].name, name) == 0 ? (int32_t)c : -1;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The host

// The CPUID leaf, as LLVM's getX86CpuIDAndInfoEx: false if the leaf is past the CPU's maximum.
static bool predefines_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t registers[4])
{
    const bool result = __get_cpuid_count(leaf, subleaf, &registers[0], &registers[1], &registers[2], &registers[3]) != 0;
    if (!result)
    {
        memset(registers, 0, 4 * sizeof(uint32_t));
    }
    return result;
}

// Bit `bit` of a register.
BUSTER_INLINE bool predefines_bit(uint32_t value, uint32_t bit)
{
    return (value >> bit) & 1;
}

// One feature of the host's: `listed` gets it (-march=native gives it as +name or -name), and `detected` gets it if on.
static void predefines_host_feature(PredefinesFeatures* listed, PredefinesFeatures* detected, const char* name, bool on)
{
    const uint32_t feature = predefines_feature(name);
    listed->words[feature / 64] |= 1ull << (feature % 64);
    detected->words[feature / 64] |= (uint64_t)on << (feature % 64);
}

// -march=native's CPU name (as LLVM's getHostCPUName finds it), or "generic", and the host's features (as
// getHostCPUFeatures finds them): `listed` are the features it names, `detected` those it turns on.
static const char* predefines_host(PredefinesFeatures* listed, PredefinesFeatures* detected)
{
    *listed = (PredefinesFeatures){ 0 };
    *detected = (PredefinesFeatures){ 0 };
    uint32_t r[4];
    const bool basic = predefines_cpuid(0, 0, r);
    const uint32_t max_level = r[0];
    const bool intel = r[1] == 0x756e6547 && r[3] == 0x49656e69 && r[2] == 0x6c65746e;
    const bool amd = r[1] == 0x68747541 && r[3] == 0x69746e65 && r[2] == 0x444d4163;
    const bool hygon = r[1] == 0x6f677948 && r[3] == 0x6e65476e && r[2] == 0x656e6975;
    const char* result = "generic";
    if (basic && max_level >= 1)
    {
        predefines_cpuid(1, 0, r);
        const uint32_t signature = r[0];
        const uint32_t ecx1 = r[2];
        const uint32_t edx1 = r[3];
        predefines_host_feature(listed, detected, "cx8", predefines_bit(edx1, 8));
        predefines_host_feature(listed, detected, "cmov", predefines_bit(edx1, 15));
        predefines_host_feature(listed, detected, "mmx", predefines_bit(edx1, 23));
        predefines_host_feature(listed, detected, "fxsr", predefines_bit(edx1, 24));
        predefines_host_feature(listed, detected, "sse", predefines_bit(edx1, 25));
        predefines_host_feature(listed, detected, "sse2", predefines_bit(edx1, 26));
        predefines_host_feature(listed, detected, "sse3", predefines_bit(ecx1, 0));
        predefines_host_feature(listed, detected, "pclmul", predefines_bit(ecx1, 1));
        predefines_host_feature(listed, detected, "ssse3", predefines_bit(ecx1, 9));
        predefines_host_feature(listed, detected, "cx16", predefines_bit(ecx1, 13));
        predefines_host_feature(listed, detected, "sse4.1", predefines_bit(ecx1, 19));
        predefines_host_feature(listed, detected, "sse4.2", predefines_bit(ecx1, 20));
        predefines_host_feature(listed, detected, "crc32", predefines_bit(ecx1, 20));
        predefines_host_feature(listed, detected, "movbe", predefines_bit(ecx1, 22));
        predefines_host_feature(listed, detected, "popcnt", predefines_bit(ecx1, 23));
        predefines_host_feature(listed, detected, "aes", predefines_bit(ecx1, 25));
        predefines_host_feature(listed, detected, "rdrnd", predefines_bit(ecx1, 30));
        // The OS saves the registers' state: XCR0's bits for SSE and AVX, AVX-512's three, AMX's two and APX's.
        const bool xsave = predefines_bit(ecx1, 27);
        const uint64_t xcr0 = xsave ? _xgetbv(0) : 0;
        const bool avx_save = xsave && predefines_bit(ecx1, 28) && (xcr0 & 0x6) == 0x6;
        const bool avx512_save = avx_save && (xcr0 & 0xe0) == 0xe0;
        const bool amx_save = xsave && (xcr0 & 0x60000) == 0x60000;
        const bool apx_save = xsave && ((xcr0 >> 19) & 1) != 0;
        predefines_host_feature(listed, detected, "avx", avx_save);
        predefines_host_feature(listed, detected, "fma", predefines_bit(ecx1, 12) && avx_save);
        predefines_host_feature(listed, detected, "xsave", predefines_bit(ecx1, 26) && avx_save);
        predefines_host_feature(listed, detected, "f16c", predefines_bit(ecx1, 29) && avx_save);
        predefines_cpuid(0x80000000, 0, r);
        const uint32_t max_extended = r[0];
        const bool extended1 = max_extended >= 0x80000001 && predefines_cpuid(0x80000001, 0, r);
        const uint32_t ecx81 = extended1 ? r[2] : 0;
        const uint32_t edx81 = extended1 ? r[3] : 0;
        predefines_host_feature(listed, detected, "sahf", predefines_bit(ecx81, 0));
        predefines_host_feature(listed, detected, "lzcnt", predefines_bit(ecx81, 5));
        predefines_host_feature(listed, detected, "sse4a", predefines_bit(ecx81, 6));
        predefines_host_feature(listed, detected, "prfchw", predefines_bit(ecx81, 8));
        predefines_host_feature(listed, detected, "xop", predefines_bit(ecx81, 11) && avx_save);
        predefines_host_feature(listed, detected, "lwp", predefines_bit(ecx81, 15));
        predefines_host_feature(listed, detected, "fma4", predefines_bit(ecx81, 16) && avx_save);
        predefines_host_feature(listed, detected, "tbm", predefines_bit(ecx81, 21));
        predefines_host_feature(listed, detected, "mwaitx", predefines_bit(ecx81, 29));
        predefines_host_feature(listed, detected, "64bit", predefines_bit(edx81, 29));
        const bool extended8 = max_extended >= 0x80000008 && predefines_cpuid(0x80000008, 0, r);
        const uint32_t ebx88 = extended8 ? r[1] : 0;
        predefines_host_feature(listed, detected, "clzero", predefines_bit(ebx88, 0));
        predefines_host_feature(listed, detected, "rdpru", predefines_bit(ebx88, 4));
        predefines_host_feature(listed, detected, "wbnoinvd", predefines_bit(ebx88, 9));
        const bool extended21 = max_extended >= 0x80000021 && predefines_cpuid(0x80000021, 0, r);
        const uint32_t eax821 = extended21 ? r[0] : 0;
        // prefetchi is AMD's bit here, or Intel's in leaf 7's subleaf 1.
        bool prefetchi = predefines_bit(eax821, 20);
        predefines_host_feature(listed, detected, "avx512bmm", predefines_bit(eax821, 23) && avx512_save);
        const bool leaf7 = max_level >= 7 && predefines_cpuid(7, 0, r);
        const uint32_t eax7 = leaf7 ? r[0] : 0;
        const uint32_t ebx7 = leaf7 ? r[1] : 0;
        const uint32_t ecx7 = leaf7 ? r[2] : 0;
        const uint32_t edx7 = leaf7 ? r[3] : 0;
        predefines_host_feature(listed, detected, "fsgsbase", predefines_bit(ebx7, 0));
        predefines_host_feature(listed, detected, "sgx", predefines_bit(ebx7, 2));
        predefines_host_feature(listed, detected, "bmi", predefines_bit(ebx7, 3));
        predefines_host_feature(listed, detected, "avx2", predefines_bit(ebx7, 5) && avx_save);
        predefines_host_feature(listed, detected, "bmi2", predefines_bit(ebx7, 8));
        predefines_host_feature(listed, detected, "invpcid", predefines_bit(ebx7, 10));
        predefines_host_feature(listed, detected, "rtm", predefines_bit(ebx7, 11));
        predefines_host_feature(listed, detected, "avx512f", predefines_bit(ebx7, 16) && avx512_save);
        predefines_host_feature(listed, detected, "avx512dq", predefines_bit(ebx7, 17) && avx512_save);
        predefines_host_feature(listed, detected, "rdseed", predefines_bit(ebx7, 18));
        predefines_host_feature(listed, detected, "adx", predefines_bit(ebx7, 19));
        predefines_host_feature(listed, detected, "avx512ifma", predefines_bit(ebx7, 21) && avx512_save);
        predefines_host_feature(listed, detected, "clflushopt", predefines_bit(ebx7, 23));
        predefines_host_feature(listed, detected, "clwb", predefines_bit(ebx7, 24));
        predefines_host_feature(listed, detected, "avx512cd", predefines_bit(ebx7, 28) && avx512_save);
        predefines_host_feature(listed, detected, "sha", predefines_bit(ebx7, 29));
        predefines_host_feature(listed, detected, "avx512bw", predefines_bit(ebx7, 30) && avx512_save);
        predefines_host_feature(listed, detected, "avx512vl", predefines_bit(ebx7, 31) && avx512_save);
        predefines_host_feature(listed, detected, "avx512vbmi", predefines_bit(ecx7, 1) && avx512_save);
        predefines_host_feature(listed, detected, "pku", predefines_bit(ecx7, 4));
        predefines_host_feature(listed, detected, "waitpkg", predefines_bit(ecx7, 5));
        predefines_host_feature(listed, detected, "avx512vbmi2", predefines_bit(ecx7, 6) && avx512_save);
        predefines_host_feature(listed, detected, "shstk", predefines_bit(ecx7, 7));
        predefines_host_feature(listed, detected, "gfni", predefines_bit(ecx7, 8));
        predefines_host_feature(listed, detected, "vaes", predefines_bit(ecx7, 9) && avx_save);
        predefines_host_feature(listed, detected, "vpclmulqdq", predefines_bit(ecx7, 10) && avx_save);
        predefines_host_feature(listed, detected, "avx512vnni", predefines_bit(ecx7, 11) && avx512_save);
        predefines_host_feature(listed, detected, "avx512bitalg", predefines_bit(ecx7, 12) && avx512_save);
        predefines_host_feature(listed, detected, "avx512vpopcntdq", predefines_bit(ecx7, 14) && avx512_save);
        predefines_host_feature(listed, detected, "rdpid", predefines_bit(ecx7, 22));
        predefines_host_feature(listed, detected, "kl", predefines_bit(ecx7, 23));
        predefines_host_feature(listed, detected, "cldemote", predefines_bit(ecx7, 25));
        predefines_host_feature(listed, detected, "movdiri", predefines_bit(ecx7, 27));
        predefines_host_feature(listed, detected, "movdir64b", predefines_bit(ecx7, 28));
        predefines_host_feature(listed, detected, "enqcmd", predefines_bit(ecx7, 29));
        predefines_host_feature(listed, detected, "uintr", predefines_bit(edx7, 5));
        predefines_host_feature(listed, detected, "avx512vp2intersect", predefines_bit(edx7, 8) && avx512_save);
        predefines_host_feature(listed, detected, "serialize", predefines_bit(edx7, 14));
        predefines_host_feature(listed, detected, "tsxldtrk", predefines_bit(edx7, 16));
        predefines_host_feature(listed, detected, "pconfig", predefines_bit(edx7, 18));
        predefines_host_feature(listed, detected, "amx-bf16", predefines_bit(edx7, 22) && amx_save);
        predefines_host_feature(listed, detected, "avx512fp16", predefines_bit(edx7, 23) && avx512_save);
        predefines_host_feature(listed, detected, "amx-tile", predefines_bit(edx7, 24) && amx_save);
        predefines_host_feature(listed, detected, "amx-int8", predefines_bit(edx7, 25) && amx_save);
        // Subleaf 0's EAX is the last subleaf: some CPUs don't give zeros past it.
        const bool leaf7_1 = leaf7 && eax7 >= 1 && predefines_cpuid(7, 1, r);
        const uint32_t eax71 = leaf7_1 ? r[0] : 0;
        const uint32_t edx71 = leaf7_1 ? r[3] : 0;
        predefines_host_feature(listed, detected, "sha512", predefines_bit(eax71, 0));
        predefines_host_feature(listed, detected, "sm3", predefines_bit(eax71, 1));
        predefines_host_feature(listed, detected, "sm4", predefines_bit(eax71, 2));
        predefines_host_feature(listed, detected, "raoint", predefines_bit(eax71, 3));
        predefines_host_feature(listed, detected, "avxvnni", predefines_bit(eax71, 4) && avx_save);
        predefines_host_feature(listed, detected, "avx512bf16", predefines_bit(eax71, 5) && avx512_save);
        predefines_host_feature(listed, detected, "amx-fp16", predefines_bit(eax71, 21) && amx_save);
        predefines_host_feature(listed, detected, "cmpccxadd", predefines_bit(eax71, 7));
        predefines_host_feature(listed, detected, "hreset", predefines_bit(eax71, 22));
        predefines_host_feature(listed, detected, "avxifma", predefines_bit(eax71, 23) && avx_save);
        predefines_host_feature(listed, detected, "movrs", predefines_bit(eax71, 31));
        predefines_host_feature(listed, detected, "avxvnniint8", predefines_bit(edx71, 4) && avx_save);
        predefines_host_feature(listed, detected, "avxneconvert", predefines_bit(edx71, 5) && avx_save);
        predefines_host_feature(listed, detected, "amx-complex", predefines_bit(edx71, 8) && amx_save);
        predefines_host_feature(listed, detected, "avxvnniint16", predefines_bit(edx71, 10) && avx_save);
        prefetchi |= predefines_bit(edx71, 14);
        predefines_host_feature(listed, detected, "prefetchi", prefetchi);
        predefines_host_feature(listed, detected, "usermsr", predefines_bit(edx71, 15));
        const bool avx10 = predefines_bit(edx71, 19);
        const bool apx = predefines_bit(edx71, 21) && apx_save;
        static const char* const apx_features[] = { "egpr", "push2pop2", "ppx", "ndd", "ccmp", "nf", "cf", "zu", "jmpabs" };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(apx_features); i += 1)
        {
            predefines_host_feature(listed, detected, apx_features[i], apx);
        }
        const bool leaf_d = max_level >= 0xd && predefines_cpuid(0xd, 1, r);
        const uint32_t eaxd1 = leaf_d ? r[0] : 0;
        predefines_host_feature(listed, detected, "xsaveopt", predefines_bit(eaxd1, 0) && avx_save);
        predefines_host_feature(listed, detected, "xsavec", predefines_bit(eaxd1, 1) && avx_save);
        predefines_host_feature(listed, detected, "xsaves", predefines_bit(eaxd1, 3) && avx_save);
        const bool leaf14 = max_level >= 0x14 && predefines_cpuid(0x14, 0, r);
        predefines_host_feature(listed, detected, "ptwrite", leaf14 && predefines_bit(r[1], 4));
        const bool leaf19 = max_level >= 0x19 && predefines_cpuid(0x19, 0, r);
        predefines_host_feature(listed, detected, "widekl", leaf7 && leaf19 && predefines_bit(r[1], 2));
        const bool leaf1e = max_level >= 0x1e && predefines_cpuid(0x1e, 1, r);
        const uint32_t eax1e1 = leaf1e ? r[0] : 0;
        predefines_host_feature(listed, detected, "amx-fp8", predefines_bit(eax1e1, 4) && amx_save);
        predefines_host_feature(listed, detected, "amx-avx512", predefines_bit(eax1e1, 7) && amx_save);
        predefines_host_feature(listed, detected, "amx-movrs", predefines_bit(eax1e1, 8) && amx_save);
        const bool leaf24 = max_level >= 0x24 && predefines_cpuid(0x24, 0, r);
        const uint32_t avx10_version = leaf24 ? r[1] & 0xff : 0;
        predefines_host_feature(listed, detected, "avx10.1", avx10 && avx10_version >= 1);
        predefines_host_feature(listed, detected, "avx10.2", avx10 && avx10_version >= 2);
        // The CPU's name, by vendor, family and model (LLVM's get*ProcessorTypeAndSubtype), with the features the
        // fallbacks look at.
        uint32_t family = (signature >> 8) & 0xf;
        uint32_t model = (signature >> 4) & 0xf;
        if (family == 6 || family == 0xf)
        {
            family += family == 0xf ? (signature >> 20) & 0xff : 0;
            model += ((signature >> 16) & 0xf) << 4;
        }
        const PredefinesFeatures d = *detected;
#define BUSTER_PREDEFINES_HOST_HAS(name) predefines_has(d, predefines_feature(name))
        const char* cpu = 0;
        if (intel)
        {
            if (family == 6)
            {
                switch (model)
                {
                    case 0x0f: case 0x16: cpu = "core2"; break;
                    case 0x17: case 0x1d: cpu = "penryn"; break;
                    case 0x1a: case 0x1e: case 0x1f: case 0x2e: cpu = "nehalem"; break;
                    case 0x25: case 0x2c: case 0x2f: cpu = "westmere"; break;
                    case 0x2a: case 0x2d: cpu = "sandybridge"; break;
                    case 0x3a: case 0x3e: cpu = "ivybridge"; break;
                    case 0x3c: case 0x3f: case 0x45: case 0x46: cpu = "haswell"; break;
                    case 0x3d: case 0x47: case 0x4f: case 0x56: cpu = "broadwell"; break;
                    case 0x4e: case 0x5e: case 0x8e: case 0x9e: case 0xa5: case 0xa6: cpu = "skylake"; break;
                    case 0xa7: cpu = "rocketlake"; break;
                    case 0x55:
                        cpu = BUSTER_PREDEFINES_HOST_HAS("avx512bf16") ? "cooperlake" : BUSTER_PREDEFINES_HOST_HAS("avx512vnni") ? "cascadelake" : "skylake-avx512";
                        break;
                    case 0x66: cpu = "cannonlake"; break;
                    case 0x7d: case 0x7e: cpu = "icelake-client"; break;
                    case 0x8c: case 0x8d: cpu = "tigerlake"; break;
                    case 0x97: case 0x9a: cpu = "alderlake"; break;
                    case 0xbe: cpu = "gracemont"; break;
                    case 0xb7: case 0xba: case 0xbf: cpu = "raptorlake"; break;
                    case 0xaa: case 0xac: cpu = "meteorlake"; break;
                    case 0xc5: case 0xb5: cpu = "arrowlake"; break;
                    case 0xc6: cpu = "arrowlake-s"; break;
                    case 0xbd: cpu = "lunarlake"; break;
                    case 0xcc: cpu = "pantherlake"; break;
                    case 0xd5: cpu = "wildcatlake"; break;
                    case 0xad: cpu = "graniterapids"; break;
                    case 0xae: cpu = "graniterapids-d"; break;
                    case 0x6a: case 0x6c: cpu = "icelake-server"; break;
                    case 0xcf: cpu = "emeraldrapids"; break;
                    case 0x8f: cpu = "sapphirerapids"; break;
                    case 0x1c: case 0x26: case 0x27: case 0x35: case 0x36: cpu = "bonnell"; break;
                    case 0x37: case 0x4a: case 0x4d: case 0x5a: case 0x5d: case 0x4c: cpu = "silvermont"; break;
                    case 0x5c: case 0x5f: cpu = "goldmont"; break;
                    case 0x7a: cpu = "goldmont-plus"; break;
                    case 0x86: case 0x8a: case 0x96: case 0x9c: cpu = "tremont"; break;
                    case 0xaf: cpu = "sierraforest"; break;
                    case 0xb6: cpu = "grandridge"; break;
                    case 0xdd: cpu = "clearwaterforest"; break;
                    case 0x57: cpu = "knl"; break;
                    case 0x85: cpu = "knm"; break;
                    default:
                        // An unknown family 6 model: guessed from its features. The 32-bit names aren't -march values for
                        // x86-64, so -march=native fails there, as clang's does.
                        cpu = BUSTER_PREDEFINES_HOST_HAS("avx512vp2intersect") ? "tigerlake"
                              : BUSTER_PREDEFINES_HOST_HAS("avx512vbmi2")      ? "icelake-client"
                              : BUSTER_PREDEFINES_HOST_HAS("avx512vbmi")       ? "cannonlake"
                              : BUSTER_PREDEFINES_HOST_HAS("avx512bf16")       ? "cooperlake"
                              : BUSTER_PREDEFINES_HOST_HAS("avx512vnni")       ? "cascadelake"
                              : BUSTER_PREDEFINES_HOST_HAS("avx512vl")         ? "skylake-avx512"
                              : BUSTER_PREDEFINES_HOST_HAS("clflushopt")       ? (BUSTER_PREDEFINES_HOST_HAS("sha") ? "goldmont" : "skylake")
                              : BUSTER_PREDEFINES_HOST_HAS("adx")              ? "broadwell"
                              : BUSTER_PREDEFINES_HOST_HAS("avx2")             ? "haswell"
                              : BUSTER_PREDEFINES_HOST_HAS("avx")              ? "sandybridge"
                              : BUSTER_PREDEFINES_HOST_HAS("sse4.2")           ? (BUSTER_PREDEFINES_HOST_HAS("movbe") ? "silvermont" : "nehalem")
                              : BUSTER_PREDEFINES_HOST_HAS("sse4.1")           ? "penryn"
                              : BUSTER_PREDEFINES_HOST_HAS("ssse3")            ? (BUSTER_PREDEFINES_HOST_HAS("movbe") ? "bonnell" : "core2")
                              : BUSTER_PREDEFINES_HOST_HAS("64bit")            ? "core2"
                              : BUSTER_PREDEFINES_HOST_HAS("sse3")             ? "yonah"
                              : BUSTER_PREDEFINES_HOST_HAS("sse2")             ? "pentium-m"
                              : BUSTER_PREDEFINES_HOST_HAS("sse")              ? "pentium3"
                              : BUSTER_PREDEFINES_HOST_HAS("mmx")              ? "pentium2"
                                                                               : "pentiumpro";
                        break;
                }
            }
            else if (family == 0xf)
            {
                cpu = BUSTER_PREDEFINES_HOST_HAS("64bit") ? "nocona" : BUSTER_PREDEFINES_HOST_HAS("sse3") ? "prescott" : "pentium4";
            }
            else if (family == 0x13)
            {
                cpu = model == 0x01 ? "diamondrapids" : 0;
            }
            else if (family == 0x12)
            {
                cpu = model == 0x1 || model == 0x3 ? "novalake" : 0;
            }
            else
            {
                cpu = family == 3 ? "i386" : family == 4 ? "i486" : family == 5 ? (BUSTER_PREDEFINES_HOST_HAS("mmx") ? "pentium-mmx" : "pentium") : 0;
            }
        }
        else if (amd)
        {
            switch (family)
            {
                case 4: cpu = "i486"; break;
                case 5:
                    cpu = model == 6 || model == 7 ? "k6" : model == 8 ? "k6-2" : model == 9 || model == 13 ? "k6-3" : model == 10 ? "geode" : "pentium";
                    break;
                case 6: cpu = BUSTER_PREDEFINES_HOST_HAS("sse") ? "athlon-xp" : "athlon"; break;
                case 15: cpu = BUSTER_PREDEFINES_HOST_HAS("sse3") ? "k8-sse3" : "k8"; break;
                case 16: case 18: cpu = "amdfam10"; break;
                case 20: cpu = "btver1"; break;
                case 21:
                    cpu = model >= 0x60 && model <= 0x7f ? "bdver4" : model >= 0x30 && model <= 0x3f ? "bdver3" : (model >= 0x10 && model <= 0x1f) || model == 0x02 ? "bdver2" : "bdver1";
                    break;
                case 22: cpu = "btver2"; break;
                case 23:
                    cpu = (model >= 0x30 && model <= 0x3f) || model == 0x47 || (model >= 0x60 && model <= 0x7f) || (model >= 0x84 && model <= 0x87) ||
                                  (model >= 0x90 && model <= 0xaf)
                              ? "znver2"
                              : "znver1";
                    break;
                case 25:
                    cpu = (model >= 0x10 && model <= 0x1f) || (model >= 0x60 && model <= 0x7f) || (model >= 0xa0 && model <= 0xaf) ? "znver4" : "znver3";
                    break;
                case 26:
                    cpu = (model >= 0x50 && model <= 0x5f) || (model >= 0x80 && model <= 0xcf) || (model >= 0xd8 && model <= 0xe7) ? "znver6" : "znver5";
                    break;
                default: break;
            }
        }
        else if (hygon)
        {
            cpu = family == 24 && model == 4 ? "c86-4g-m4" : family == 24 && model == 6 ? "c86-4g-m6" : family == 24 && model == 7 ? "c86-4g-m7" : family == 24 && model == 8 ? "c86-4g-m8" : 0;
        }
#undef BUSTER_PREDEFINES_HOST_HAS
        result = cpu != 0 ? cpu : "generic";
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The options

// The last option of each family that clang's driver reads with getLastArg, by kind (0: none given).
#define BUSTER_PREDEFINES_PIC_BIG 1 // -fPIC
#define BUSTER_PREDEFINES_PIC_SMALL 2 // -fpic
#define BUSTER_PREDEFINES_PIE_BIG 3 // -fPIE
#define BUSTER_PREDEFINES_PIE_SMALL 4 // -fpie
#define BUSTER_PREDEFINES_PIC_OFF 5 // -fno-PIC, -fno-pic, -fno-PIE or -fno-pie

#define BUSTER_PREDEFINES_SSP_OFF 1 // -fno-stack-protector
#define BUSTER_PREDEFINES_SSP_ON 2 // -fstack-protector
#define BUSTER_PREDEFINES_SSP_STRONG 3 // -fstack-protector-strong
#define BUSTER_PREDEFINES_SSP_ALL 4 // -fstack-protector-all
// Clang distributions can enable stack protection by default. The build uses
// that Clang without an explicit stack-protector option, so its predefined
// macro records the driver's default. Explicit -fstack-protector takes the
// stronger of the driver's default and its own level.
#if defined(__SSP_ALL__)
#define BUSTER_PREDEFINES_SSP_DEFAULT BUSTER_PREDEFINES_SSP_ALL
#elif defined(__SSP_STRONG__)
#define BUSTER_PREDEFINES_SSP_DEFAULT BUSTER_PREDEFINES_SSP_STRONG
#elif defined(__SSP__)
#define BUSTER_PREDEFINES_SSP_DEFAULT BUSTER_PREDEFINES_SSP_ON
#else
#define BUSTER_PREDEFINES_SSP_DEFAULT BUSTER_PREDEFINES_SSP_OFF
#endif

#define BUSTER_PREDEFINES_ON 1 // A -fX / -fno-X pair's last: -fX.
#define BUSTER_PREDEFINES_OFF 2 // -fno-X.

#define BUSTER_PREDEFINES_INLINE_FUNCTIONS 1 // -finline-functions
#define BUSTER_PREDEFINES_INLINE_HINT_FUNCTIONS 2 // -finline-hint-functions
#define BUSTER_PREDEFINES_NO_INLINE_FUNCTIONS 3 // -fno-inline-functions

#define BUSTER_PREDEFINES_EXCEPTIONS_SJLJ 1 // -fsjlj-exceptions
#define BUSTER_PREDEFINES_EXCEPTIONS_SEH 2 // -fseh-exceptions
#define BUSTER_PREDEFINES_EXCEPTIONS_DWARF 3 // -fdwarf-exceptions
#define BUSTER_PREDEFINES_EXCEPTIONS_WASM 4 // -fwasm-exceptions

#define BUSTER_PREDEFINES_CODE_MODEL_SMALL 0
#define BUSTER_PREDEFINES_CODE_MODEL_KERNEL 1
#define BUSTER_PREDEFINES_CODE_MODEL_MEDIUM 2
#define BUSTER_PREDEFINES_CODE_MODEL_LARGE 3

#define BUSTER_PREDEFINES_LONG_DOUBLE_80 0
#define BUSTER_PREDEFINES_LONG_DOUBLE_64 1
#define BUSTER_PREDEFINES_LONG_DOUBLE_128 2

#define BUSTER_PREDEFINES_EVAL_DEFAULT 0 // From the target: 0 with SSE, 2 without.
#define BUSTER_PREDEFINES_EVAL_SOURCE 1
#define BUSTER_PREDEFINES_EVAL_DOUBLE 2
#define BUSTER_PREDEFINES_EVAL_EXTENDED 3

#define BUSTER_PREDEFINES_FPMATH_SSE 1
#define BUSTER_PREDEFINES_FPMATH_387 2

// The floating-point options clang's driver steps through in order (RenderFloatingPointOptions).
typedef enum PredefinesFloat
{
    PREDEFINES_FLOAT_MODEL_FAST,
    PREDEFINES_FLOAT_MODEL_AGGRESSIVE,
    PREDEFINES_FLOAT_MODEL_PRECISE,
    PREDEFINES_FLOAT_MODEL_STRICT,
    PREDEFINES_FLOAT_HONOR_INFINITIES,
    PREDEFINES_FLOAT_NO_HONOR_INFINITIES,
    PREDEFINES_FLOAT_HONOR_NANS,
    PREDEFINES_FLOAT_NO_HONOR_NANS,
    PREDEFINES_FLOAT_APPROX_FUNC,
    PREDEFINES_FLOAT_NO_APPROX_FUNC,
    PREDEFINES_FLOAT_MATH_ERRNO,
    PREDEFINES_FLOAT_NO_MATH_ERRNO,
    PREDEFINES_FLOAT_ASSOCIATIVE,
    PREDEFINES_FLOAT_NO_ASSOCIATIVE,
    PREDEFINES_FLOAT_RECIPROCAL,
    PREDEFINES_FLOAT_NO_RECIPROCAL,
    PREDEFINES_FLOAT_SIGNED_ZEROS,
    PREDEFINES_FLOAT_NO_SIGNED_ZEROS,
    PREDEFINES_FLOAT_TRAPPING,
    PREDEFINES_FLOAT_NO_TRAPPING,
    PREDEFINES_FLOAT_ROUNDING,
    PREDEFINES_FLOAT_NO_ROUNDING,
    PREDEFINES_FLOAT_EXCEPTIONS_IGNORE, // -ffp-exception-behavior=ignore or maytrap
    PREDEFINES_FLOAT_EXCEPTIONS_STRICT,
    PREDEFINES_FLOAT_FINITE,
    PREDEFINES_FLOAT_NO_FINITE,
    PREDEFINES_FLOAT_UNSAFE,
    PREDEFINES_FLOAT_NO_UNSAFE,
    PREDEFINES_FLOAT_OFAST, // -Ofast, which counts only when it is the last -O.
    PREDEFINES_FLOAT_FAST,
    PREDEFINES_FLOAT_NO_FAST,
} PredefinesFloat;

// The hardening flags clang's driver weighs against each other (predefines_finish_neutral), each with its -mno- form.
typedef enum PredefinesHardening
{
    PREDEFINES_HARDENING_RETPOLINE,
    PREDEFINES_HARDENING_SPECULATIVE_LOAD,
    PREDEFINES_HARDENING_EXTERNAL_THUNK,
    PREDEFINES_HARDENING_LVI,
    PREDEFINES_HARDENING_LVI_CFI,
    PREDEFINES_HARDENING_SESES,
    PREDEFINES_HARDENING_COUNT,
} PredefinesHardening;

// The -m options with a value that change no macro (predefines_neutral_values).
#define BUSTER_PREDEFINES_NEUTRAL_VALUES 19

// What the options say about the predefined macros, as parsed in command-line order. All zero is clang's defaults with
// no option, so a zeroed PPOptions has them.
typedef struct PredefinesOptions PredefinesOptions;
struct PredefinesOptions
{
    // -march's value (a predefines_cpus name or "native"), or null for x86-64; -mtune's (a predefines_tunes name), or
    // null for none.
    const char* march;
    const char* mtune;
    // The -m feature flags in order: (feature << 1) | on, with -mapx-inline-asm-use-gpr32's feature apart, as the
    // driver puts it after the others. `features` has room for BUSTER_PREDEFINES_FLAGS_PER_ARGUMENT per argument.
    uint16_t* features;
    uint32_t feature_count;
    uint32_t feature_capacity;
    bool inline_asm_use_gpr32;
    // -mfpmath (BUSTER_PREDEFINES_FPMATH_*, from the last -mfpmath= by predefines_finish), checked against the final
    // features.
    uint8_t fpmath;
    // The last -O: its level (0 to 3), size (1 for -Os, 2 for -Oz) and whether it is -Ofast; and whether it is one other
    // than -O0 itself, which is what clang's driver reads to keep -fsanitize=object-size (-O00 keeps it, as no -O doesn't).
    uint8_t optimization;
    uint8_t optimize_size;
    bool ofast;
    bool optimization_flag;
    // The last of -finline and -fno-inline, and of -finline-functions, -finline-hint-functions and
    // -fno-inline-functions.
    uint8_t inline_option;
    uint8_t inline_functions;
    // The floating-point options in order (PredefinesFloat); `floats` has room for one per argument.
    uint8_t* floats;
    uint32_t float_count;
    uint32_t float_capacity;
    uint8_t fp_eval_method;
    bool char_unsigned;
    bool short_wchar;
    uint8_t pic;
    uint8_t stack_protector;
    uint8_t dwarf2_cfi_asm;
    uint8_t asynchronous_unwind_tables;
    uint8_t unwind_tables;
    bool exceptions;
    uint8_t exception_model;
    // The last -g option of clang's g_Group turns debug information on (-g, -g1, -ggdb, -gdwarf-4, -gcodeview,
    // -gmodules, ...) rather than off (-g0, -ggdb0); and -gmodules, when -gno-modules doesn't follow it, turns it on
    // whatever that last option is.
    bool debug;
    bool debug_modules;
    // -fgnuc-version: whether given, and the version (0.0.0 turns the GNU macros off).
    bool gnuc_given;
    uint8_t gnuc_major;
    uint8_t gnuc_minor;
    uint8_t gnuc_patch;
    uint8_t gnu89_inline;
    bool pthread;
    // -fcf-protection: bit 0 branch, bit 1 return.
    uint8_t cf_protection;
    // The sanitizer options in order (-fsanitize=, -fno-sanitize=, -fsanitize-ignore-for-ubsan-feature=,
    // -fsanitize-coverage= and -fno-sanitize-coverage=), which predefines_finish resolves after every option, as clang's
    // driver does; `sanitize_arguments` has room for one per argument.
    const char** sanitize_arguments;
    uint32_t sanitize_count;
    uint32_t sanitize_capacity;
    // What the sanitizers' rules read besides: the last of -mexecute-only, -mno-execute-only and their aliases -mpure-code
    // and -mno-pure-code (null for none), the last -flto value (-flto is "full", null for none or -fno-lto), the last
    // -fvisibility= value when no -fvisibility-ms-compat follows it, whether any -fvisibility= was given, and the last
    // option of each -f/-fno- pair.
    const char* execute_only;
    const char* lto;
    const char* visibility;
    bool visibility_given;
    uint8_t rtti;
    uint8_t wrapv;
    uint8_t wrapv_pointer;
    uint8_t strict_overflow;
    uint8_t cfi_cross_dso;
    // predefines_finish's results: the sanitizers on (by PredefinesSanitizer), those -fsanitize-ignore-for-ubsan-feature=
    // keeps out of __has_feature(undefined_behavior_sanitizer), and whether coverage instrumentation is on.
    uint64_t sanitizers;
    uint64_t ubsan_feature_ignored;
    bool sanitize_coverage;
    // -target's or --target='s triple (predefines_target_valid), or null for clang's default, x86_64-pc-linux-gnu.
    const char* target;
    // The options clang's driver reads only the last of, which predefines_finish checks (-march's CPU and -mtune are
    // above): the last -mfpmath=, -mcmodel=, -mabi= and -fveclib= value (null for none); the last -O, -fgnuc-version=
    // and -fcf-protection= when its value is invalid (null otherwise); and OpenMP, on with the last of -fopenmp,
    // -fopenmp= and -fno-openmp unless it is -fno-openmp, with the last -fopenmp='s runtime (null for libomp).
    const char* fpmath_name;
    const char* code_model_name;
    const char* abi;
    const char* veclib;
    const char* optimization_invalid;
    const char* gnuc_invalid;
    const char* cf_protection_invalid;
    // The first -ffp-model= value clang doesn't know: an error only when the last -O isn't -Ofast, which ignores every
    // model but aggressive without reading its value.
    const char* fp_model_invalid;
    bool openmp_enabled;
    const char* openmp_runtime;
    bool blocks;
    // Whether clang defines _OPENMP (predefines_finish's): OpenMP on, with libomp's runtime or its old name, libiomp5's.
    bool openmp;
    // -fopenmp-version's value, or 0.
    uint32_t openmp_version;
    bool openacc;
    bool target_os_macros;
    bool pascal_strings;
    // clang's driver reads two families, -f[no-]constant-cfstrings and -m[no-]constant-cfstrings, each by its last
    // option: __CONSTANT_CFSTRINGS__ goes when either family's is the no- one.
    bool no_constant_cfstrings;
    bool no_constant_cfstrings_m;
    // -municode (or -municodeANYTHING, a joined option), which clang's driver turns into a -DUNICODE ahead of the
    // command line's own -D and -U: pp_run puts it first in <command line>.
    bool unicode;
    // The -m options that change no macro but whose values or combinations clang checks (predefines_finish_neutral): each
    // value option's last value (null for none), and the last of each hardening flag and its -mno- form, 1 for the flag
    // and 2 for the -mno- one (0 for neither), with whether -mno-lvi-cfi came at all.
    const char* neutral_values[BUSTER_PREDEFINES_NEUTRAL_VALUES];
    uint8_t hardening[PREDEFINES_HARDENING_COUNT];
    bool no_lvi_cfi;
    // The profile-generating options' families, each last-wins (-fprofile-generate, -fprofile-instr-generate,
    // -fcs-profile-generate and -fprofile-generate-cold-function-coverage).
    uint8_t profile_generate[4];
    // BUSTER_PREDEFINES_CODE_MODEL_*, from the last -mcmodel= by predefines_finish.
    uint8_t code_model;
    uint8_t long_double;
};

// The most feature flags one argument gives: -mapx-features= with all nine names.
#define BUSTER_PREDEFINES_FLAGS_PER_ARGUMENT 9

typedef enum PredefinesParse
{
    PREDEFINES_PARSE_UNKNOWN, // Not an option of these.
    PREDEFINES_PARSE_TAKEN, // The argument.
    PREDEFINES_PARSE_TAKEN_TWO, // The argument and the next one.
    PREDEFINES_PARSE_ERROR, // An error, reported on stderr.
} PredefinesParse;

// Zeroes `options` (clang's defaults) and gives them room for `argument_count` arguments' flags in `arena`. Returns false
// if the arena is full.
static __attribute__((unused)) bool predefines_options_initialize(PredefinesOptions* options, Arena* arena, uint32_t argument_count)
{
    *options = (PredefinesOptions){ 0 };
    options->feature_capacity = argument_count * BUSTER_PREDEFINES_FLAGS_PER_ARGUMENT + 1;
    options->features = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, options->feature_capacity);
    options->float_capacity = argument_count + 1;
    options->floats = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, options->float_capacity);
    options->sanitize_capacity = argument_count + 1;
    options->sanitize_arguments = BUSTER_ARENA_PUSH_ARRAY(arena, const char*, options->sanitize_capacity);
    return options->features != 0 && options->floats != 0 && options->sanitize_arguments != 0;
}

static bool predefines_starts(const char* argument, const char* prefix)
{
    return strncmp(argument, prefix, strlen(prefix)) == 0;
}

// Adds a feature flag; false (with an error) if `options` has no room, which only an options value without
// predefines_options_initialize's room can run into.
static bool predefines_push_feature(PredefinesOptions* options, uint32_t feature, bool on)
{
    const bool result = options->feature_count < options->feature_capacity;
    if (result)
    {
        options->features[options->feature_count] = (uint16_t)(feature << 1 | (on ? 1u : 0u));
        options->feature_count += 1;
    }
    else
    {
        fprintf(stderr, "error: too many feature flags\n");
    }
    return result;
}

static bool predefines_push_float(PredefinesOptions* options, PredefinesFloat event)
{
    const bool result = options->float_count < options->float_capacity;
    if (result)
    {
        options->floats[options->float_count] = (uint8_t)event;
        options->float_count += 1;
    }
    else
    {
        fprintf(stderr, "error: too many floating-point options\n");
    }
    return result;
}

static bool predefines_push_sanitize(PredefinesOptions* options, const char* argument)
{
    const bool result = options->sanitize_count < options->sanitize_capacity;
    if (result)
    {
        options->sanitize_arguments[options->sanitize_count] = argument;
        options->sanitize_count += 1;
    }
    else
    {
        fprintf(stderr, "error: too many sanitizer options\n");
    }
    return result;
}

static bool predefines_tune_valid(const char* name)
{
    bool result = strcmp(name, "generic") == 0 || strcmp(name, "native") == 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(predefines_tunes) && !result; i += 1)
    {
        result = strcmp(predefines_tunes[i], name) == 0;
    }
    return result;
}

// The comma-separated names of -mapx-features= (or -mno-apx-features=), each pushed as on or off. False after an error.
static bool predefines_apx_features(PredefinesOptions* options, const char* argument, const char* list, bool on)
{
    bool result = true;
    const char* name = list;
    bool more = true;
    while (result && more)
    {
        const char* end = strchr(name, ',');
        const size_t length = end != 0 ? (size_t)(end - name) : strlen(name);
        const int32_t feature = predefines_feature_index(name, length);
        result = feature >= 0 && predefines_features[feature].flag == PREDEFINES_FLAG_APX;
        if (!result)
        {
            const char* equals = strchr(argument, '=');
            const int option_length = equals != 0 ? (int)(equals + 1 - argument) : (int)strlen(argument);
            fprintf(stderr, "error: unsupported argument '%.*s' to option '%.*s'\n", (int)length, name, option_length, argument);
        }
        result = result && predefines_push_feature(options, (uint32_t)feature, on);
        more = end != 0;
        name = end != 0 ? end + 1 : name;
    }
    return result;
}

// The options clang rejects or that change what clang compiles past what buster follows: rejected by name, so that
// a run never silently differs from clang's. Those that change clang's keywords (-fms-compatibility, -ffixed-point,
// -fdefer-ts, ...) are pp_take_family's, refused only where the last of their family turns them on.
static bool predefines_unsupported(const char* argument)
{
    static const char* const exact[] = {
        "-m32",
        "-mx32",
        "-m16",
        "-miamcu",
        "-mkernel",
        "-fapple-kext",
        "-foffload-via-llvm",
        "-fstack-protector-explicit",
        "-fprofile-use",
        "-fprofile-instr-use",
        "-fcs-profile-use",
    };
    static const char* const prefixes[] = {
        "-mcpu=",
        "-fms-compatibility-version=",
        "-fexperimental-pointer-field-protection",
        "-fprofile-use=",
        "-fprofile-instr-use=",
        "-fptrauth",
        "-fsycl",
        "-fcuda",
        "-fgpu",
        "-fhip",
        "-fopenmp-targets=",
    };
    bool result = false;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(exact) && !result; i += 1)
    {
        result = strcmp(argument, exact[i]) == 0;
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(prefixes) && !result; i += 1)
    {
        result = predefines_starts(argument, prefixes[i]);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The -m options that change no macro

// The -m options clang 23.1.3 uses for x86-64 Linux that change nothing `clang -E -dM` gives, beside those predefines_option
// reads for their macros: predefines_neutral takes them, and pp_parse_options refuses every other -m option, as clang
// refuses one it doesn't know (-mavx512) or doesn't support for the target (-mfpu=), rather than keep the baseline's
// macros. Found from `clang --autocomplete=-m`, each option tried alone with `clang -E -dM`: accepted, with the same
// macros and no warning. Another target's options, which clang takes on x86-64 with a warning that it ignores them
// (-mthumb, -moutline-atomics), are refused too. test-preprocessor --clang compares each with clang, alone and among the
// other options. The flags without a value (but the hardening ones below):
static const char* const predefines_neutral_flags[] = {
    "-m64",
    "-malign-double",
    "-mbranches-within-32B-boundaries",
    "-mcmse",
    "-mfentry",
    "-mimplicit-float",
    "-mincremental-linker-compatible",
    "-mindirect-branch-cs-prefix",
    "-mlittle-endian",
    "-mms-bitfields",
    "-mno-gather",
    "-mno-iamcu",
    "-mno-implicit-float",
    "-mno-incremental-linker-compatible",
    "-mno-ms-bitfields",
    "-mno-omit-leaf-frame-pointer",
    "-mno-outline",
    "-mno-red-zone",
    "-mno-relax-all",
    "-mno-reserve-frame-pointer-reg",
    "-mno-restrict-it",
    "-mno-rtd",
    "-mno-scatter",
    "-mno-skip-rax-setup",
    "-mno-soft-float",
    "-mno-stack-arg-probe",
    "-mno-stackrealign",
    "-mno-tls-direct-seg-refs",
    "-mno-vzeroupper",
    "-momit-leaf-frame-pointer",
    "-moutline",
    "-mred-zone",
    "-mrelax-all",
    "-mreserve-frame-pointer-reg",
    "-mrestrict-it",
    "-mskip-rax-setup",
    "-msoft-float",
    "-mstack-arg-probe",
    "-mstackrealign",
    "-mtls-direct-seg-refs",
    "-mvzeroupper",
};

// The hardening flags' names, each with its -mno- form, by PredefinesHardening.
static const char* const predefines_hardening_names[] = {
    "retpoline",
    "speculative-load-hardening",
    "retpoline-external-thunk",
    "lvi-hardening",
    "lvi-cfi",
    "seses",
};
_Static_assert(BUSTER_ARRAY_LENGTH(predefines_hardening_names) == PREDEFINES_HARDENING_COUNT, "a name per pair");

// How clang checks a value option's last value, as its driver reads it and then cc1, which reads an option's number in
// any radix (0x, 0b, 0o, or a leading 0 for octal).
typedef enum PredefinesRule
{
    PREDEFINES_RULE_ANY, // Any value: clang reads none on x86-64.
    PREDEFINES_RULE_NAME, // One of the names (separated by commas in `names`).
    PREDEFINES_RULE_NAMES, // Names separated by commas, empty ones dropped (clang's CommaJoined): any number of them.
    PREDEFINES_RULE_FUNCTION_RETURN, // One of the names, but thunk-extern under -mcmodel=large.
    PREDEFINES_RULE_UNSIGNED, // An unsigned 32-bit decimal number.
    PREDEFINES_RULE_BOUNDARY, // That, a power of two from 16.
    PREDEFINES_RULE_WIDTH, // "none", or an unsigned 32-bit decimal number.
    PREDEFINES_RULE_CC1_UNSIGNED, // cc1's unsigned 32-bit number.
    PREDEFINES_RULE_PROBE_SIZE, // Empty (0), or cc1's unsigned 32-bit number.
    PREDEFINES_RULE_THRESHOLD, // cc1's unsigned 32-bit number under -mcmodel=medium or large, and any value otherwise.
    PREDEFINES_RULE_ALIGNMENT, // 0 or a power of two, as a signed 64-bit decimal number, then cc1's unsigned 32-bit one.
    PREDEFINES_RULE_OFFSET, // A signed 32-bit decimal number, then cc1's signed 32-bit one.
    PREDEFINES_RULE_SYMBOL, // A symbol's name: letters, digits and '_', not starting with a digit.
    PREDEFINES_RULE_RECIP, // -mrecip='s estimates (predefines_recip).
} PredefinesRule;

typedef struct PredefinesNeutralValue PredefinesNeutralValue;
struct PredefinesNeutralValue
{
    const char* option;
    PredefinesRule rule;
    const char* names;
};

// The value options, each read by its last value (clang's driver reads only the last of each; -mrecip is -mrecip=
// without one), and their rules, from clang's driver (Clang.cpp, CommonArgs.cpp, X86.cpp and OptionUtils.cpp) and cc1
// (CompilerInvocation.cpp and Options.td's marshalling).
static const PredefinesNeutralValue predefines_neutral_values[] = {
    { "-malign-branch-boundary=", PREDEFINES_RULE_BOUNDARY, 0 },
    { "-malign-branch=", PREDEFINES_RULE_NAMES, "fused,jcc,jmp,call,ret,indirect" },
    { "-masm=", PREDEFINES_RULE_NAME, "att,intel" },
    { "-mfunction-return=", PREDEFINES_RULE_FUNCTION_RETURN, "keep,thunk-extern" },
    { "-mharden-sls=", PREDEFINES_RULE_NAME, "none,all,return,indirect-jmp" },
    { "-mlarge-data-threshold=", PREDEFINES_RULE_THRESHOLD, 0 },
    { "-mlinker-version=", PREDEFINES_RULE_ANY, 0 },
    { "-mpad-max-prefix-size=", PREDEFINES_RULE_UNSIGNED, 0 },
    { "-mprefer-vector-width=", PREDEFINES_RULE_WIDTH, 0 },
    { "-mprintf-kind=", PREDEFINES_RULE_ANY, 0 },
    { "-mrecip=", PREDEFINES_RULE_RECIP, 0 },
    { "-mregparm=", PREDEFINES_RULE_CC1_UNSIGNED, 0 },
    { "-mstack-alignment=", PREDEFINES_RULE_ALIGNMENT, 0 },
    { "-mstack-probe-size=", PREDEFINES_RULE_PROBE_SIZE, 0 },
    { "-mstack-protector-guard-offset=", PREDEFINES_RULE_OFFSET, 0 },
    { "-mstack-protector-guard-reg=", PREDEFINES_RULE_NAME, "fs,gs" },
    { "-mstack-protector-guard-symbol=", PREDEFINES_RULE_SYMBOL, 0 },
    { "-mstack-protector-guard=", PREDEFINES_RULE_NAME, "tls,global" },
    { "-mtls-dialect=", PREDEFINES_RULE_NAME, "gnu,gnu2" },
};
_Static_assert(BUSTER_ARRAY_LENGTH(predefines_neutral_values) == BUSTER_PREDEFINES_NEUTRAL_VALUES, "a slot per value option");

// clang's -m aliases of other options (Options.td's Alias, with its AliasArgs), each with the option it stands for:
// pp_parse_options reads an alias as that option, so that its handling is the option's and the last of either spelling
// wins. clang's messages name -mpure-code as written, and -msse4, -m80387 and -mapxf stand for values of their own:
// predefines_option reads those under their own spellings. The others clang 23.1.3 has are other targets' (-marm,
// -mfix-cortex-*, Darwin's version options, -mmfcrf, -mv5...: refused like their options) or -mllvm's (refused).
static const char* const predefines_aliases[][2] = {
    { "-mpascal-strings", "-fpascal-strings" },
    { "-mno-pascal-strings", "-fno-pascal-strings" },
    { "-mzvector", "-fzvector" },
    { "-mno-zvector", "-fno-zvector" },
    { "-munsafe-fp-atomics", "-fatomic-ignore-denormal-mode" },
    { "-mno-unsafe-fp-atomics", "-fno-atomic-ignore-denormal-mode" },
    { "-mno-fp-ret-in-387", "-mno-x87" },
    { "-mips1", "-march=mips1" },
    { "-mips2", "-march=mips2" },
    { "-mips3", "-march=mips3" },
    { "-mips4", "-march=mips4" },
    { "-mips5", "-march=mips5" },
    { "-mips32", "-march=mips32" },
    { "-mips32r2", "-march=mips32r2" },
    { "-mips32r3", "-march=mips32r3" },
    { "-mips32r5", "-march=mips32r5" },
    { "-mips32r6", "-march=mips32r6" },
    { "-mips64", "-march=mips64" },
    { "-mips64r2", "-march=mips64r2" },
    { "-mips64r3", "-march=mips64r3" },
    { "-mips64r5", "-march=mips64r5" },
    { "-mips64r6", "-march=mips64r6" },
};

// `argument`, or the option it is clang's alias of (predefines_aliases).
static __attribute__((unused)) const char* predefines_alias(const char* argument)
{
    const char* result = argument;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(predefines_aliases) && result == argument; i += 1)
    {
        result = strcmp(argument, predefines_aliases[i][0]) == 0 ? predefines_aliases[i][1] : result;
    }
    return result;
}

// predefines_option's -m options that change no macro: true where `argument` (an -m option) is one, keeping what
// predefines_finish_neutral checks.
static bool predefines_neutral(PredefinesOptions* options, const char* argument)
{
    bool result = false;
    for (uint32_t i = 0; !result && i < BUSTER_ARRAY_LENGTH(predefines_neutral_flags); i += 1)
    {
        result = strcmp(argument, predefines_neutral_flags[i]) == 0;
    }
    const bool no = predefines_starts(argument, "-mno-");
    for (uint32_t h = 0; !result && h < PREDEFINES_HARDENING_COUNT; h += 1)
    {
        result = strcmp(argument + (no ? 5 : 2), predefines_hardening_names[h]) == 0;
        options->hardening[h] = result ? (no ? 2 : 1) : options->hardening[h];
        options->no_lvi_cfi |= result && no && h == PREDEFINES_HARDENING_LVI_CFI;
    }
    // -mrecip is -mrecip= without a value: all estimates.
    const bool recip = !result && strcmp(argument, "-mrecip") == 0;
    for (uint32_t v = 0; !result && v < BUSTER_ARRAY_LENGTH(predefines_neutral_values); v += 1)
    {
        const char* option = predefines_neutral_values[v].option;
        const bool taken = recip ? strcmp(option, "-mrecip=") == 0 : predefines_starts(argument, option);
        options->neutral_values[v] = taken ? (recip ? "" : argument + strlen(option)) : options->neutral_values[v];
        result = taken;
    }
    return result;
}

// llvm::StringRef::getAsInteger's reading of a whole text as a number, as clang's driver reads one in `radix` 10 and
// cc1 in radix 0 (the radix from a prefix: 0x, 0b, 0o, or a 0 before a digit for octal), with a '-' first where
// `signed_number` allows one. *magnitude gets the number without its sign and *minus whether it had the '-'. Returns
// false where the text isn't such a number or its magnitude passes 64 bits.
static bool predefines_integer(const char* text, uint32_t radix, bool signed_number, uint64_t* magnitude, bool* minus)
{
    *minus = signed_number && text[0] == '-';
    const char* digits = text + *minus;
    const bool zero = digits[0] == '0';
    const bool hexadecimal = radix == 0 && zero && (digits[1] == 'x' || digits[1] == 'X');
    const bool binary = radix == 0 && zero && (digits[1] == 'b' || digits[1] == 'B');
    const bool octal = radix == 0 && zero && digits[1] == 'o';
    const bool leading = radix == 0 && zero && digits[1] >= '0' && digits[1] <= '9';
    const uint32_t base = radix != 0 ? radix : hexadecimal ? 16 : binary ? 2 : octal || leading ? 8 : 10;
    digits += hexadecimal || binary || octal ? 2 : leading ? 1 : 0;
    uint64_t value = 0;
    bool result = digits[0] != 0;
    for (const char* p = digits; result && *p != 0; p += 1)
    {
        const char c = *p;
        const uint32_t digit = c >= '0' && c <= '9'   ? (uint32_t)(c - '0')
                               : c >= 'a' && c <= 'z' ? (uint32_t)(c - 'a') + 10
                               : c >= 'A' && c <= 'Z' ? (uint32_t)(c - 'A') + 10
                                                      : 36;
        const uint64_t next = value * base + digit;
        result = digit < base && next / base >= value;
        value = next;
    }
    *magnitude = value;
    return result;
}

// An unsigned 32-bit number in `radix` (predefines_integer's), its value in *value.
static bool predefines_unsigned(const char* text, uint32_t radix, uint64_t* value)
{
    bool minus;
    return predefines_integer(text, radix, false, value, &minus) && *value <= UINT32_MAX;
}

// A signed 32-bit number in `radix`.
static bool predefines_signed(const char* text, uint32_t radix)
{
    uint64_t magnitude;
    bool minus;
    return predefines_integer(text, radix, true, &magnitude, &minus) && magnitude <= (minus ? 0x80000000ull : 0x7fffffffull);
}

// Whether `length` bytes at `name` are one of `names` (separated by commas).
static bool predefines_name_in(const char* name, size_t length, const char* names)
{
    bool result = false;
    for (const char* p = names; !result && *p != 0;)
    {
        const char* end = strchr(p, ',');
        end = end != 0 ? end : p + strlen(p);
        result = (size_t)(end - p) == length && memcmp(p, name, length) == 0;
        p = *end != 0 ? end + 1 : end;
    }
    return result;
}

// clang's parseMRecipOption (OptionUtils.cpp) on -mrecip='s value: items separated by commas, empty ones dropped. None
// means all; a single one may be all, none or default; and otherwise each names an estimate (div, vec-div, sqrt or
// vec-sqrt) after an optional '!', which turns it off, with its precision's letter (d, f or h; none for all three), each
// precision of an estimate once. Every item may end in a refinement step, ':' and one digit. Returns whether clang
// takes it.
static bool predefines_recip(const char* value)
{
    static const char* const estimates[] = { "div", "vec-div", "sqrt", "vec-sqrt" };
    // Each estimate's precisions named so far, a bit each: d, f and h.
    uint8_t named[4] = { 0 };
    uint32_t items = 0;
    for (const char* p = value; *p != 0; p += *p != 0)
    {
        const char* end = strchr(p, ',');
        end = end != 0 ? end : p + strlen(p);
        items += end > p;
        p = end;
    }
    bool result = true;
    bool decided = items == 0;
    for (const char* p = value; result && !decided && *p != 0; p += *p != 0)
    {
        const char* end = strchr(p, ',');
        end = end != 0 ? end : p + strlen(p);
        if (end > p)
        {
            const char* step = memchr(p, ':', (size_t)(end - p));
            const size_t base_length = step != 0 ? (size_t)(step - p) : (size_t)(end - p);
            const bool step_valid = step == 0 || (end - step == 2 && step[1] >= '0' && step[1] <= '9');
            decided = items == 1 && step_valid && predefines_name_in(p, base_length, "all,none,default");
            // An estimate: its name and precision after the '!'.
            const bool off = *p == '!';
            const char* name = p + off;
            const size_t length = base_length - off;
            int32_t estimate = -1;
            uint32_t precision = 0;
            for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(estimates); e += 1)
            {
                const size_t estimate_length = strlen(estimates[e]);
                const char letter = length == estimate_length + 1 ? name[estimate_length] : 0;
                const uint32_t bit = letter == 'd' ? 1 : letter == 'f' ? 2 : letter == 'h' ? 4 : 0;
                const bool same = length >= estimate_length && memcmp(name, estimates[e], estimate_length) == 0 && (length == estimate_length || bit != 0);
                estimate = same ? (int32_t)e : estimate;
                // No letter names all three, none of which may be named already.
                precision = same ? (bit != 0 ? bit : 7) : precision;
            }
            const uint8_t before = estimate >= 0 ? named[estimate] : 0;
            result = step_valid && (decided || (estimate >= 0 && (before & precision) == 0));
            if (estimate >= 0)
            {
                named[estimate] = (uint8_t)(before | precision);
            }
        }
        p = end;
    }
    return result;
}

// Whether `value` is the last value of `entry`'s option clang takes, reporting clang's error where it isn't.
static bool predefines_neutral_value(const PredefinesOptions* options, const PredefinesNeutralValue* entry, const char* value)
{
    uint64_t number = 0;
    uint64_t magnitude = 0;
    bool minus = false;
    const bool large = options->code_model == BUSTER_PREDEFINES_CODE_MODEL_LARGE;
    const bool medium = options->code_model == BUSTER_PREDEFINES_CODE_MODEL_MEDIUM;
    bool items = true;
    for (const char* p = value; entry->rule == PREDEFINES_RULE_NAMES && *p != 0; p += *p != 0)
    {
        const char* end = strchr(p, ',');
        end = end != 0 ? end : p + strlen(p);
        items &= end == p || predefines_name_in(p, (size_t)(end - p), entry->names);
        p = end;
    }
    bool symbol = value[0] != 0 && !(value[0] >= '0' && value[0] <= '9');
    for (const char* p = value; entry->rule == PREDEFINES_RULE_SYMBOL && *p != 0; p += 1)
    {
        const char c = *p;
        symbol &= (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    const PredefinesRule rule = entry->rule;
    const bool thunk_large = rule == PREDEFINES_RULE_FUNCTION_RETURN && strcmp(value, "thunk-extern") == 0 && large;
    const bool result =
        rule == PREDEFINES_RULE_ANY                ? true
        : rule == PREDEFINES_RULE_NAME             ? predefines_name_in(value, strlen(value), entry->names)
        : rule == PREDEFINES_RULE_NAMES            ? items
        : rule == PREDEFINES_RULE_FUNCTION_RETURN  ? predefines_name_in(value, strlen(value), entry->names) && !thunk_large
        : rule == PREDEFINES_RULE_UNSIGNED         ? predefines_unsigned(value, 10, &number)
        : rule == PREDEFINES_RULE_BOUNDARY         ? predefines_unsigned(value, 10, &number) && number >= 16 && (number & (number - 1)) == 0
        : rule == PREDEFINES_RULE_WIDTH            ? strcmp(value, "none") == 0 || predefines_unsigned(value, 10, &number)
        : rule == PREDEFINES_RULE_CC1_UNSIGNED     ? predefines_unsigned(value, 0, &number)
        : rule == PREDEFINES_RULE_PROBE_SIZE       ? value[0] == 0 || predefines_unsigned(value, 0, &number)
        : rule == PREDEFINES_RULE_THRESHOLD        ? !(medium || large) || predefines_unsigned(value, 0, &number)
        : rule == PREDEFINES_RULE_ALIGNMENT        ? predefines_integer(value, 10, true, &magnitude, &minus) && magnitude <= (minus ? 0ull : 0x7fffffffffffffffull) &&
                                                     (magnitude & (magnitude - 1)) == 0 && predefines_unsigned(value, 0, &number)
        : rule == PREDEFINES_RULE_OFFSET           ? predefines_signed(value, 10) && predefines_signed(value, 0)
        : rule == PREDEFINES_RULE_SYMBOL           ? symbol
                                                   : predefines_recip(value);
    if (thunk_large)
    {
        fprintf(stderr, "error: invalid argument '-mfunction-return=thunk-extern' not allowed with '-mcmodel=large'\n");
    }
    else if (!result)
    {
        fprintf(stderr, "error: invalid value '%s' in '%s%s'\n", value, entry->option, value);
    }
    return result;
}

// The -m options that change no macro, as clang checks them after every option: each value option's last value, and the
// hardening flags against each other as X86.cpp's getX86TargetFeatures weighs them: one of Spectre's (retpolines, then
// load hardening; the external thunk only without either's flags), and one of LVI's (its load hardening, then its CFI),
// not both, and SESES with neither but LVI's CFI, which it turns on unless -mno-lvi-cfi came at all. Reports clang's
// errors; false after one.
static bool predefines_finish_neutral(const PredefinesOptions* options)
{
    bool result = true;
    for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(predefines_neutral_values); v += 1)
    {
        const char* value = options->neutral_values[v];
        result = (value == 0 || predefines_neutral_value(options, &predefines_neutral_values[v], value)) && result;
    }
    const uint8_t* h = options->hardening;
    const bool spectre_flags = h[PREDEFINES_HARDENING_RETPOLINE] != 0 || h[PREDEFINES_HARDENING_SPECULATIVE_LOAD] != 0;
    const char* spectre = h[PREDEFINES_HARDENING_RETPOLINE] == 1                            ? "mretpoline"
                          : spectre_flags && h[PREDEFINES_HARDENING_SPECULATIVE_LOAD] == 1  ? "mspeculative-load-hardening"
                          : !spectre_flags && h[PREDEFINES_HARDENING_EXTERNAL_THUNK] == 1   ? "mretpoline-external-thunk"
                                                                                            : 0;
    const bool lvi_hardening = h[PREDEFINES_HARDENING_LVI] == 1;
    const char* lvi = lvi_hardening ? "mlvi-hardening" : h[PREDEFINES_HARDENING_LVI_CFI] == 1 ? "mlvi-cfi" : 0;
    const bool seses = h[PREDEFINES_HARDENING_SESES] == 1;
    if (seses && lvi_hardening)
    {
        fprintf(stderr, "error: invalid argument 'mlvi-hardening' not allowed with 'mseses'\n");
        result = false;
    }
    if (seses && spectre != 0)
    {
        fprintf(stderr, "error: invalid argument '%s' not allowed with 'mseses'\n", spectre);
        result = false;
    }
    lvi = seses && !options->no_lvi_cfi ? "mlvi-cfi" : lvi;
    if (spectre != 0 && lvi != 0)
    {
        fprintf(stderr, "error: invalid argument '%s' not allowed with '%s'\n", spectre, lvi);
        result = false;
    }
    return result;
}

// Whether a -target or --target triple is x86-64 Linux with glibc, the one target buster predefines for: x86_64 or
// amd64, any vendor (clang's predefines are the same for every one) or none, linux and gnu: ARCH-VENDOR-linux-gnu or
// ARCH-linux-gnu.
static bool predefines_target_valid(const char* triple)
{
    const char* rest = predefines_starts(triple, "x86_64-") ? triple + 7 : predefines_starts(triple, "amd64-") ? triple + 6 : 0;
    const char* vendor_end = rest != 0 ? strchr(rest, '-') : 0;
    return rest != 0 && (strcmp(rest, "linux-gnu") == 0 || (vendor_end != 0 && strcmp(vendor_end + 1, "linux-gnu") == 0));
}

// build.sh records the build Clang driver's default triple. Direct builds without that definition retain the
// historical default; an explicit target always overrides it.
#ifndef BUSTER_CLANG_DEFAULT_TARGET
#define BUSTER_CLANG_DEFAULT_TARGET "x86_64-pc-linux-gnu"
#endif

// The target triple's vendor name, as clang's __is_target_vendor reads it: the -target's ("unknown" when it names none)
// or the build Clang's default triple's. *length gets its length.
static const char* predefines_target_vendor(const PredefinesOptions* options, uint32_t* length)
{
    const char* target = options->target != 0 ? options->target : BUSTER_CLANG_DEFAULT_TARGET;
    const char* separator = strchr(target, '-');
    const char* rest = separator != 0 ? separator + 1 : 0;
    const char* vendor_end = rest != 0 ? strchr(rest, '-') : 0;
    const bool named = rest != 0 && vendor_end != 0 && strcmp(rest, "linux-gnu") != 0 && vendor_end != rest;
    *length = named ? (uint32_t)(vendor_end - rest) : 7;
    return named ? rest : "unknown";
}

// The target triple as clang's driver normalizes it (a missing or empty vendor is "unknown"), for its messages.
static void predefines_target_triple(const PredefinesOptions* options, char* buffer, size_t capacity)
{
    uint32_t vendor_length;
    const char* vendor = predefines_target_vendor(options, &vendor_length);
    const char* architecture = options->target != 0 ? options->target : "x86_64";
    const int architecture_length = options->target != 0 ? (int)(strchr(options->target, '-') - options->target) : 6;
    snprintf(buffer, capacity, "%.*s-%.*s-linux-gnu", architecture_length, architecture, (int)vendor_length, vendor);
}

// One argument (`next` is the one after it, or null). Reports an error on stderr, as clang's driver words it.
static __attribute__((unused)) PredefinesParse predefines_option(PredefinesOptions* options, const char* argument, const char* next)
{
    PredefinesParse result = PREDEFINES_PARSE_TAKEN;
    const bool m = argument[0] == '-' && argument[1] == 'm';
    const bool no = m && predefines_starts(argument, "-mno-");
    // A plain feature flag: -mNAME or -mno-NAME.
    const char* feature_name = no ? argument + 5 : m ? argument + 2 : 0;
    const int32_t feature = feature_name != 0 ? predefines_feature_index(feature_name, strlen(feature_name)) : -1;
    if (feature >= 0 && predefines_features[feature].flag == PREDEFINES_FLAG_PLAIN)
    {
        result = predefines_push_feature(options, (uint32_t)feature, !no) ? result : PREDEFINES_PARSE_ERROR;
    }
    else if (predefines_unsupported(argument))
    {
        fprintf(stderr, "error: unsupported option '%s' (it changes what clang predefines or compiles, which buster doesn't follow)\n", argument);
        result = PREDEFINES_PARSE_ERROR;
    }
    else if (predefines_starts(argument, "-march="))
    {
        // clang's driver reads only the last -march: predefines_resolve checks its CPU.
        options->march = argument + 7;
    }
    else if (predefines_starts(argument, "-mtune="))
    {
        // Only the last, which predefines_finish checks.
        options->mtune = argument + 7;
    }
    else if (strcmp(argument, "-msse4") == 0 || strcmp(argument, "-mno-sse4") == 0)
    {
        // The aliases: -msse4 is -msse4.2, and -mno-sse4 is -mno-sse4.1.
        const bool on = argument[2] != 'n';
        result = predefines_push_feature(options, predefines_feature(on ? "sse4.2" : "sse4.1"), on) ? result : PREDEFINES_PARSE_ERROR;
    }
    else if (strcmp(argument, "-m80387") == 0 || strcmp(argument, "-mno-80387") == 0)
    {
        result = predefines_push_feature(options, predefines_feature("x87"), argument[2] != 'n') ? result : PREDEFINES_PARSE_ERROR;
    }
    else if (strcmp(argument, "-mapxf") == 0 || strcmp(argument, "-mno-apxf") == 0)
    {
        // -mapxf is -mapx-features= with the APX features but cf.
        result = predefines_apx_features(options, argument, "egpr,push2pop2,ppx,ndd,ccmp,nf,zu,jmpabs", argument[2] != 'n') ? result : PREDEFINES_PARSE_ERROR;
    }
    else if (predefines_starts(argument, "-mapx-features=") || predefines_starts(argument, "-mno-apx-features="))
    {
        result = predefines_apx_features(options, argument, strchr(argument, '=') + 1, argument[2] != 'n') ? result : PREDEFINES_PARSE_ERROR;
    }
    else if (strcmp(argument, "-mgeneral-regs-only") == 0)
    {
        const bool pushed = predefines_push_feature(options, predefines_feature("x87"), false) && predefines_push_feature(options, predefines_feature("mmx"), false) &&
                            predefines_push_feature(options, predefines_feature("sse"), false);
        result = pushed ? result : PREDEFINES_PARSE_ERROR;
    }
    else if (strcmp(argument, "-mapx-inline-asm-use-gpr32") == 0)
    {
        options->inline_asm_use_gpr32 = true;
    }
    else if (strcmp(argument, "-mexecute-only") == 0 || strcmp(argument, "-mno-execute-only") == 0 || strcmp(argument, "-mpure-code") == 0 ||
             strcmp(argument, "-mno-pure-code") == 0)
    {
        // ARM's, which clang leaves unused on x86-64 but for the sanitizers' rules (predefines_sanitizers).
        options->execute_only = argument;
    }
    else if (predefines_starts(argument, "-mfpmath="))
    {
        // Only the last, which predefines_finish reads.
        options->fpmath_name = argument + 9;
    }
    else if (predefines_starts(argument, "-mcmodel="))
    {
        // Only the last, which predefines_finish reads.
        options->code_model_name = argument + 9;
    }
    else if (strcmp(argument, "-mlong-double-64") == 0 || strcmp(argument, "-mlong-double-80") == 0 || strcmp(argument, "-mlong-double-128") == 0)
    {
        options->long_double = argument[14] == '6' ? BUSTER_PREDEFINES_LONG_DOUBLE_64 : argument[14] == '8' ? BUSTER_PREDEFINES_LONG_DOUBLE_80 : BUSTER_PREDEFINES_LONG_DOUBLE_128;
    }
    else if (predefines_starts(argument, "-mabi="))
    {
        // Only the last, which predefines_finish checks.
        options->abi = argument + 6;
    }
    else if (m && predefines_neutral(options, argument))
    {
        // An option that changes no macro, which predefines_finish_neutral checks.
    }
    else if (strcmp(argument, "-target") == 0 || predefines_starts(argument, "--target="))
    {
        // Only the last, which predefines_finish checks.
        const char* triple = argument[1] == 't' ? next : argument + 9;
        result = triple != 0 ? (argument[1] == 't' ? PREDEFINES_PARSE_TAKEN_TWO : PREDEFINES_PARSE_TAKEN) : PREDEFINES_PARSE_ERROR;
        if (triple == 0)
        {
            fprintf(stderr, "error: argument to '-target' is missing (expected 1 value)\n");
        }
        options->target = triple;
    }
    else if (argument[0] == '-' && argument[1] == 'O')
    {
        const char* level = argument + 2;
        options->optimization_flag = strcmp(argument, "-O0") != 0;
        options->optimization_invalid = 0;
        if (strcmp(level, "s") == 0 || strcmp(level, "z") == 0)
        {
            options->optimization = 2;
            options->optimize_size = level[0] == 's' ? 1 : 2;
            options->ofast = false;
        }
        else if (strcmp(level, "fast") == 0)
        {
            options->optimization = 3;
            options->optimize_size = 0;
            options->ofast = true;
            result = predefines_push_float(options, PREDEFINES_FLOAT_OFAST) ? result : PREDEFINES_PARSE_ERROR;
        }
        else if (strcmp(level, "g") == 0 || level[0] == 0)
        {
            options->optimization = 1;
            options->optimize_size = 0;
            options->ofast = false;
        }
        else
        {
            uint32_t value = 0;
            bool digits = true;
            for (const char* p = level; *p != 0 && digits; p += 1)
            {
                digits = *p >= '0' && *p <= '9';
                value = value < 1000 ? value * 10 + (uint32_t)(*p - '0') : value;
            }
            // Only the last -O counts, so predefines_finish reports an invalid one.
            options->optimization_invalid = digits ? 0 : argument;
            // Past 3 is 3, with clang's warning.
            options->optimization = (uint8_t)(value > 3 ? 3 : value);
            options->optimize_size = 0;
            options->ofast = false;
        }
    }
    else if (strcmp(argument, "-finline") == 0 || strcmp(argument, "-fno-inline") == 0)
    {
        options->inline_option = argument[2] == 'n' && argument[3] == 'o' ? BUSTER_PREDEFINES_OFF : BUSTER_PREDEFINES_ON;
    }
    else if (strcmp(argument, "-finline-functions") == 0)
    {
        options->inline_functions = BUSTER_PREDEFINES_INLINE_FUNCTIONS;
    }
    else if (strcmp(argument, "-finline-hint-functions") == 0)
    {
        options->inline_functions = BUSTER_PREDEFINES_INLINE_HINT_FUNCTIONS;
    }
    else if (strcmp(argument, "-fno-inline-functions") == 0)
    {
        options->inline_functions = BUSTER_PREDEFINES_NO_INLINE_FUNCTIONS;
    }
    else if (strcmp(argument, "-fsigned-char") == 0 || strcmp(argument, "-fno-unsigned-char") == 0)
    {
        options->char_unsigned = false;
    }
    else if (strcmp(argument, "-funsigned-char") == 0 || strcmp(argument, "-fno-signed-char") == 0)
    {
        options->char_unsigned = true;
    }
    else if (strcmp(argument, "-fshort-wchar") == 0 || strcmp(argument, "-fno-short-wchar") == 0)
    {
        options->short_wchar = argument[2] == 's';
    }
    else if (strcmp(argument, "-fPIC") == 0 || strcmp(argument, "-fpic") == 0 || strcmp(argument, "-fPIE") == 0 || strcmp(argument, "-fpie") == 0)
    {
        options->pic = strcmp(argument, "-fPIC") == 0   ? BUSTER_PREDEFINES_PIC_BIG
                       : strcmp(argument, "-fpic") == 0 ? BUSTER_PREDEFINES_PIC_SMALL
                       : strcmp(argument, "-fPIE") == 0 ? BUSTER_PREDEFINES_PIE_BIG
                                                        : BUSTER_PREDEFINES_PIE_SMALL;
    }
    else if (strcmp(argument, "-fno-PIC") == 0 || strcmp(argument, "-fno-pic") == 0 || strcmp(argument, "-fno-PIE") == 0 || strcmp(argument, "-fno-pie") == 0)
    {
        options->pic = BUSTER_PREDEFINES_PIC_OFF;
    }
    else if (strcmp(argument, "-fno-stack-protector") == 0 || strcmp(argument, "-fstack-protector") == 0 || strcmp(argument, "-fstack-protector-strong") == 0 ||
             strcmp(argument, "-fstack-protector-all") == 0)
    {
        // "-fstack-protector" is 17 bytes, and its -strong and -all forms go on after it.
        options->stack_protector = argument[2] == 'n'    ? BUSTER_PREDEFINES_SSP_OFF
                                   : argument[17] == 0   ? BUSTER_PREDEFINES_SSP_ON
                                   : argument[18] == 's' ? BUSTER_PREDEFINES_SSP_STRONG
                                                         : BUSTER_PREDEFINES_SSP_ALL;
    }
    else if (strcmp(argument, "-fdwarf2-cfi-asm") == 0 || strcmp(argument, "-fno-dwarf2-cfi-asm") == 0)
    {
        options->dwarf2_cfi_asm = argument[2] == 'n' ? BUSTER_PREDEFINES_OFF : BUSTER_PREDEFINES_ON;
    }
    else if (strcmp(argument, "-fasynchronous-unwind-tables") == 0 || strcmp(argument, "-fno-asynchronous-unwind-tables") == 0)
    {
        options->asynchronous_unwind_tables = argument[2] == 'n' ? BUSTER_PREDEFINES_OFF : BUSTER_PREDEFINES_ON;
    }
    else if (strcmp(argument, "-funwind-tables") == 0 || strcmp(argument, "-fno-unwind-tables") == 0)
    {
        options->unwind_tables = argument[2] == 'n' ? BUSTER_PREDEFINES_OFF : BUSTER_PREDEFINES_ON;
    }
    else if (strcmp(argument, "-fexceptions") == 0 || strcmp(argument, "-fno-exceptions") == 0)
    {
        options->exceptions = argument[2] == 'e';
    }
    else if (strcmp(argument, "-fsjlj-exceptions") == 0 || strcmp(argument, "-fseh-exceptions") == 0 || strcmp(argument, "-fdwarf-exceptions") == 0 ||
             strcmp(argument, "-fwasm-exceptions") == 0)
    {
        options->exception_model = argument[2] == 's' && argument[3] == 'j' ? BUSTER_PREDEFINES_EXCEPTIONS_SJLJ
                                   : argument[2] == 's'                     ? BUSTER_PREDEFINES_EXCEPTIONS_SEH
                                   : argument[2] == 'd'                     ? BUSTER_PREDEFINES_EXCEPTIONS_DWARF
                                                                            : BUSTER_PREDEFINES_EXCEPTIONS_WASM;
    }
    else if (argument[0] == '-' && argument[1] == 'g')
    {
        // The -g options of clang's g_Group, which choose a debug information kind: the last one turns it on or off.
        // -gmodules is one, and turns it on whatever comes last unless -gno-modules follows it. The others (-gz,
        // -gsplit-dwarf, -gno-column-info, ...) change no macro, and clang refuses the old formats' options.
        static const char* const enabling[] = {
            "-g",
            "-g1",
            "-g2",
            "-g3",
            "-ggdb",
            "-ggdb1",
            "-ggdb2",
            "-ggdb3",
            "-glldb",
            "-gsce",
            "-gdbx",
            "-gline-tables-only",
            "-gline-directives-only",
            "-gmlt",
            "-gdwarf",
            "-gdwarf-2",
            "-gdwarf-3",
            "-gdwarf-4",
            "-gdwarf-5",
            "-gdwarf-6",
            "-gdwarf32",
            "-gdwarf64",
            "-gcodeview",
            "-gfull",
            "-gused",
            "-gmodules",
        };
        static const char* const refused[] = { "-gstabs", "-gcoff", "-gxcoff", "-gvms" };
        bool enables = false;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(enabling); i += 1)
        {
            enables |= strcmp(argument, enabling[i]) == 0;
        }
        const bool disables = strcmp(argument, "-g0") == 0 || strcmp(argument, "-ggdb0") == 0;
        options->debug = enables || (options->debug && !disables);
        options->debug_modules = strcmp(argument, "-gmodules") == 0 || (options->debug_modules && strcmp(argument, "-gno-modules") != 0);
        // The refused ones are prefixes (-gstabs+ is one too), and -gtoggle.
        bool unsupported = strcmp(argument, "-gtoggle") == 0;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused); i += 1)
        {
            unsupported |= predefines_starts(argument, refused[i]);
        }
        if (unsupported)
        {
            fprintf(stderr, "error: unsupported option '%s'\n", argument);
            result = PREDEFINES_PARSE_ERROR;
        }
    }
    else if (predefines_starts(argument, "-fgnuc-version="))
    {
        // major[.minor[.patch]], minor and patch below 100; empty is 0, which turns the GNU macros off.
        const char* p = argument + 15;
        uint32_t parts[3] = { 0, 0, 0 };
        uint32_t part = 0;
        bool valid = true;
        bool digit = false;
        for (; *p != 0 && valid; p += 1)
        {
            if (*p >= '0' && *p <= '9')
            {
                parts[part] = parts[part] * 10 + (uint32_t)(*p - '0');
                valid = parts[part] < 1000;
                digit = true;
            }
            else
            {
                valid = *p == '.' && digit && part < 2;
                part += 1;
                digit = false;
            }
        }
        valid &= (digit || argument[15] == 0) && parts[0] < 256 && parts[1] < 100 && parts[2] < 100;
        // Only the last counts, so predefines_finish reports an invalid one.
        options->gnuc_invalid = valid ? 0 : argument;
        options->gnuc_given = true;
        options->gnuc_major = (uint8_t)parts[0];
        options->gnuc_minor = (uint8_t)parts[1];
        options->gnuc_patch = (uint8_t)parts[2];
    }
    else if (strcmp(argument, "-fgnu89-inline") == 0 || strcmp(argument, "-fno-gnu89-inline") == 0)
    {
        options->gnu89_inline = argument[2] == 'g' ? BUSTER_PREDEFINES_ON : BUSTER_PREDEFINES_OFF;
    }
    else if (strcmp(argument, "-pthread") == 0)
    {
        options->pthread = true;
    }
    else if (strcmp(argument, "-no-pthread") == 0)
    {
        // clang ignores it in a compile (it warns that it is unused).
    }
    else if (strcmp(argument, "-fcf-protection") == 0 || predefines_starts(argument, "-fcf-protection="))
    {
        const char* value = argument[15] == '=' ? argument + 16 : "full";
        options->cf_protection = strcmp(value, "full") == 0 ? 3 : strcmp(value, "branch") == 0 ? 1 : strcmp(value, "return") == 0 ? 2 : strcmp(value, "none") == 0 ? 0 : 0xff;
        // Only the last counts, so predefines_finish reports an invalid one.
        options->cf_protection_invalid = options->cf_protection == 0xff ? argument : 0;
    }
    else if (predefines_starts(argument, "-fsanitize=") || predefines_starts(argument, "-fno-sanitize=") ||
             predefines_starts(argument, "-fsanitize-ignore-for-ubsan-feature=") || predefines_starts(argument, "-fsanitize-coverage=") ||
             predefines_starts(argument, "-fno-sanitize-coverage="))
    {
        // Resolved after every option (predefines_finish): clang's driver reads them from the last back.
        result = predefines_push_sanitize(options, argument) ? result : PREDEFINES_PARSE_ERROR;
    }
    else if (strcmp(argument, "-fsanitize-cfi-cross-dso") == 0 || strcmp(argument, "-fno-sanitize-cfi-cross-dso") == 0)
    {
        options->cfi_cross_dso = argument[2] == 'n' ? BUSTER_PREDEFINES_OFF : BUSTER_PREDEFINES_ON;
    }
    else if (strcmp(argument, "-frtti") == 0 || strcmp(argument, "-fno-rtti") == 0)
    {
        options->rtti = argument[2] == 'n' ? BUSTER_PREDEFINES_OFF : BUSTER_PREDEFINES_ON;
    }
    else if (strcmp(argument, "-fwrapv") == 0 || strcmp(argument, "-fno-wrapv") == 0)
    {
        options->wrapv = argument[2] == 'n' ? BUSTER_PREDEFINES_OFF : BUSTER_PREDEFINES_ON;
    }
    else if (strcmp(argument, "-fwrapv-pointer") == 0 || strcmp(argument, "-fno-wrapv-pointer") == 0)
    {
        options->wrapv_pointer = argument[2] == 'n' ? BUSTER_PREDEFINES_OFF : BUSTER_PREDEFINES_ON;
    }
    else if (strcmp(argument, "-fstrict-overflow") == 0 || strcmp(argument, "-fno-strict-overflow") == 0)
    {
        options->strict_overflow = argument[2] == 'n' ? BUSTER_PREDEFINES_OFF : BUSTER_PREDEFINES_ON;
    }
    else if (strcmp(argument, "-flto") == 0 || predefines_starts(argument, "-flto=") || strcmp(argument, "-fno-lto") == 0)
    {
        // -flto, -flto=jobserver and -flto=auto are -flto=full; predefines_finish checks the value.
        const bool full = strcmp(argument, "-flto") == 0 || strcmp(argument, "-flto=jobserver") == 0 || strcmp(argument, "-flto=auto") == 0;
        options->lto = argument[2] == 'n' ? 0 : full ? "full" : argument + 6;
    }
    else if (predefines_starts(argument, "-fvisibility=") || strcmp(argument, "-fvisibility-ms-compat") == 0)
    {
        // The last of them is checked (only -fvisibility= has a value); CFI's checks need any -fvisibility=.
        const bool value = argument[12] == '=';
        options->visibility = value ? argument + 13 : 0;
        options->visibility_given |= value;
    }
    else if (strcmp(argument, "-fblocks") == 0 || strcmp(argument, "-fno-blocks") == 0)
    {
        options->blocks = argument[2] == 'b';
    }
    else if (strcmp(argument, "-fopenmp") == 0 || strcmp(argument, "-fno-openmp") == 0 || predefines_starts(argument, "-fopenmp="))
    {
        // On unless -fno-openmp comes last; the runtime is the last -fopenmp='s (libomp without one), whichever comes
        // after it. predefines_finish reads them.
        options->openmp_enabled = argument[2] == 'o';
        options->openmp_runtime = argument[8] == '=' ? argument + 9 : options->openmp_runtime;
    }
    else if (predefines_starts(argument, "-fopenmp-version="))
    {
        options->openmp_version = (uint32_t)strtoul(argument + 17, 0, 10);
    }
    else if (strcmp(argument, "-fopenacc") == 0)
    {
        options->openacc = true;
    }
    else if (strcmp(argument, "-fdefine-target-os-macros") == 0 || strcmp(argument, "-fno-define-target-os-macros") == 0)
    {
        options->target_os_macros = argument[2] == 'd';
    }
    else if (strcmp(argument, "-fpascal-strings") == 0 || strcmp(argument, "-fno-pascal-strings") == 0)
    {
        options->pascal_strings = argument[2] == 'p';
    }
    else if (strcmp(argument, "-fconstant-cfstrings") == 0 || strcmp(argument, "-fno-constant-cfstrings") == 0)
    {
        options->no_constant_cfstrings = argument[2] == 'n';
    }
    else if (strcmp(argument, "-mconstant-cfstrings") == 0 || strcmp(argument, "-mno-constant-cfstrings") == 0)
    {
        options->no_constant_cfstrings_m = argument[2] == 'n';
    }
    else if (predefines_starts(argument, "-municode"))
    {
        options->unicode = true;
    }
    else if (predefines_starts(argument, "-fprofile-generate") || predefines_starts(argument, "-fprofile-instr-generate") ||
             predefines_starts(argument, "-fcs-profile-generate") || strcmp(argument, "-fno-profile-generate") == 0 ||
             strcmp(argument, "-fno-profile-instr-generate") == 0)
    {
        // Four families, each read by its last option: -fprofile-generate[=], -fprofile-instr-generate[=],
        // -fcs-profile-generate[=] and -fprofile-generate-cold-function-coverage[=]. -fno-profile-generate ends the first
        // and the third, as clang reads them.
        const bool on = argument[2] != 'n';
        const char* name = on ? argument + 2 : argument + 5;
        const uint32_t family = predefines_starts(name, "profile-generate-cold-function-coverage") ? 3
                                : predefines_starts(name, "profile-generate")                     ? 0
                                : predefines_starts(name, "profile-instr-generate")               ? 1
                                                                                                  : 2;
        options->profile_generate[family] = on ? BUSTER_PREDEFINES_ON : BUSTER_PREDEFINES_OFF;
        options->profile_generate[2] = !on && family == 0 ? BUSTER_PREDEFINES_OFF : options->profile_generate[2];
    }
    else if (predefines_starts(argument, "-ffp-model="))
    {
        const char* value = argument + 11;
        const int event = strcmp(value, "fast") == 0         ? PREDEFINES_FLOAT_MODEL_FAST
                          : strcmp(value, "aggressive") == 0 ? PREDEFINES_FLOAT_MODEL_AGGRESSIVE
                          : strcmp(value, "precise") == 0    ? PREDEFINES_FLOAT_MODEL_PRECISE
                          : strcmp(value, "strict") == 0     ? PREDEFINES_FLOAT_MODEL_STRICT
                                                             : -1;
        // An unknown value resets the state as precise does, then goes no further (predefines_finish_options).
        options->fp_model_invalid = event < 0 && options->fp_model_invalid == 0 ? value : options->fp_model_invalid;
        const PredefinesFloat model = event < 0 ? PREDEFINES_FLOAT_MODEL_PRECISE : (PredefinesFloat)event;
        result = predefines_push_float(options, model) ? result : PREDEFINES_PARSE_ERROR;
    }
    else if (predefines_starts(argument, "-ffp-exception-behavior="))
    {
        const char* value = argument + 24;
        const int event = strcmp(value, "ignore") == 0 || strcmp(value, "maytrap") == 0 ? PREDEFINES_FLOAT_EXCEPTIONS_IGNORE
                          : strcmp(value, "strict") == 0                                  ? PREDEFINES_FLOAT_EXCEPTIONS_STRICT
                                                                                          : -1;
        if (event < 0)
        {
            fprintf(stderr, "error: unsupported argument '%s' to option '-ffp-exception-behavior='\n", value);
        }
        result = event >= 0 && predefines_push_float(options, (PredefinesFloat)event) ? result : PREDEFINES_PARSE_ERROR;
    }
    else if (predefines_starts(argument, "-ffp-eval-method="))
    {
        const char* value = argument + 17;
        options->fp_eval_method = strcmp(value, "source") == 0     ? BUSTER_PREDEFINES_EVAL_SOURCE
                                  : strcmp(value, "double") == 0   ? BUSTER_PREDEFINES_EVAL_DOUBLE
                                  : strcmp(value, "extended") == 0 ? BUSTER_PREDEFINES_EVAL_EXTENDED
                                                                   : 0xff;
        if (options->fp_eval_method == 0xff)
        {
            fprintf(stderr, "error: unsupported argument '%s' to option '-ffp-eval-method='\n", value);
            result = PREDEFINES_PARSE_ERROR;
        }
    }
    else if (predefines_starts(argument, "-fveclib="))
    {
        // Of the vector libraries, ArmPL and SLEEF turn math errno off where they come among the floating-point options
        // (clang's state machine, whatever -fveclib= follows). predefines_finish checks the last one: clang refuses
        // both for x86-64.
        const char* value = argument + 9;
        options->veclib = value;
        const bool no_errno = strcmp(value, "SLEEF") == 0 || strcmp(value, "ArmPL") == 0;
        result = !no_errno || predefines_push_float(options, PREDEFINES_FLOAT_NO_MATH_ERRNO) ? result : PREDEFINES_PARSE_ERROR;
    }
    else
    {
        static const struct
        {
            const char* name;
            PredefinesFloat event;
        } floats[] = {
            { "-fhonor-infinities", PREDEFINES_FLOAT_HONOR_INFINITIES },
            { "-fno-honor-infinities", PREDEFINES_FLOAT_NO_HONOR_INFINITIES },
            { "-fhonor-nans", PREDEFINES_FLOAT_HONOR_NANS },
            { "-fno-honor-nans", PREDEFINES_FLOAT_NO_HONOR_NANS },
            { "-fapprox-func", PREDEFINES_FLOAT_APPROX_FUNC },
            { "-fno-approx-func", PREDEFINES_FLOAT_NO_APPROX_FUNC },
            { "-fmath-errno", PREDEFINES_FLOAT_MATH_ERRNO },
            { "-fno-math-errno", PREDEFINES_FLOAT_NO_MATH_ERRNO },
            { "-fassociative-math", PREDEFINES_FLOAT_ASSOCIATIVE },
            { "-fno-associative-math", PREDEFINES_FLOAT_NO_ASSOCIATIVE },
            { "-freciprocal-math", PREDEFINES_FLOAT_RECIPROCAL },
            { "-fno-reciprocal-math", PREDEFINES_FLOAT_NO_RECIPROCAL },
            { "-fsigned-zeros", PREDEFINES_FLOAT_SIGNED_ZEROS },
            { "-fno-signed-zeros", PREDEFINES_FLOAT_NO_SIGNED_ZEROS },
            { "-ftrapping-math", PREDEFINES_FLOAT_TRAPPING },
            { "-fno-trapping-math", PREDEFINES_FLOAT_NO_TRAPPING },
            { "-frounding-math", PREDEFINES_FLOAT_ROUNDING },
            { "-fno-rounding-math", PREDEFINES_FLOAT_NO_ROUNDING },
            { "-ffinite-math-only", PREDEFINES_FLOAT_FINITE },
            { "-fno-finite-math-only", PREDEFINES_FLOAT_NO_FINITE },
            { "-funsafe-math-optimizations", PREDEFINES_FLOAT_UNSAFE },
            { "-fno-unsafe-math-optimizations", PREDEFINES_FLOAT_NO_UNSAFE },
            { "-ffast-math", PREDEFINES_FLOAT_FAST },
            { "-fno-fast-math", PREDEFINES_FLOAT_NO_FAST },
        };
        int event = -1;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(floats) && event < 0; i += 1)
        {
            event = strcmp(argument, floats[i].name) == 0 ? (int)floats[i].event : -1;
        }
        result = event >= 0 ? (predefines_push_float(options, (PredefinesFloat)event) ? PREDEFINES_PARSE_TAKEN : PREDEFINES_PARSE_ERROR) : PREDEFINES_PARSE_UNKNOWN;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The sanitizers

// clang 23's sanitizers (Sanitizers.def), in its order: PREDEFINES_SANITIZER_<enumerator> numbers them, and bit i of a
// sanitizer mask is the i-th one's.
#define BUSTER_PREDEFINES_SANITIZERS(X) \
    X(ADDRESS, "address") \
    X(POINTER_COMPARE, "pointer-compare") \
    X(POINTER_SUBTRACT, "pointer-subtract") \
    X(KERNEL_ADDRESS, "kernel-address") \
    X(HWADDRESS, "hwaddress") \
    X(KERNEL_HWADDRESS, "kernel-hwaddress") \
    X(MEMTAG_STACK, "memtag-stack") \
    X(MEMTAG_HEAP, "memtag-heap") \
    X(MEMTAG_GLOBALS, "memtag-globals") \
    X(MEMORY, "memory") \
    X(KERNEL_MEMORY, "kernel-memory") \
    X(FUZZER, "fuzzer") \
    X(FUZZER_NO_LINK, "fuzzer-no-link") \
    X(TYPE, "type") \
    X(THREAD, "thread") \
    X(NUMERICAL, "numerical") \
    X(REALTIME, "realtime") \
    X(LEAK, "leak") \
    X(ALIGNMENT, "alignment") \
    X(ARRAY_BOUNDS, "array-bounds") \
    X(BOOL, "bool") \
    X(BUILTIN, "builtin") \
    X(ENUM, "enum") \
    X(FLOAT_CAST_OVERFLOW, "float-cast-overflow") \
    X(FLOAT_DIVIDE_BY_ZERO, "float-divide-by-zero") \
    X(FUNCTION, "function") \
    X(INTEGER_DIVIDE_BY_ZERO, "integer-divide-by-zero") \
    X(NONNULL_ATTRIBUTE, "nonnull-attribute") \
    X(NULL_POINTER, "null") \
    X(NULLABILITY_ARG, "nullability-arg") \
    X(NULLABILITY_ASSIGN, "nullability-assign") \
    X(NULLABILITY_RETURN, "nullability-return") \
    X(OBJECT_SIZE, "object-size") \
    X(POINTER_OVERFLOW, "pointer-overflow") \
    X(RETURN, "return") \
    X(RETURNS_NONNULL_ATTRIBUTE, "returns-nonnull-attribute") \
    X(SHIFT_BASE, "shift-base") \
    X(SHIFT_EXPONENT, "shift-exponent") \
    X(SIGNED_INTEGER_OVERFLOW, "signed-integer-overflow") \
    X(UNREACHABLE, "unreachable") \
    X(VLA_BOUND, "vla-bound") \
    X(VPTR, "vptr") \
    X(UNSIGNED_INTEGER_OVERFLOW, "unsigned-integer-overflow") \
    X(UNSIGNED_SHIFT_BASE, "unsigned-shift-base") \
    X(DATAFLOW, "dataflow") \
    X(CFI_CAST_STRICT, "cfi-cast-strict") \
    X(CFI_DERIVED_CAST, "cfi-derived-cast") \
    X(CFI_ICALL, "cfi-icall") \
    X(CFI_MFCALL, "cfi-mfcall") \
    X(CFI_UNRELATED_CAST, "cfi-unrelated-cast") \
    X(CFI_NVCALL, "cfi-nvcall") \
    X(CFI_VCALL, "cfi-vcall") \
    X(KCFI, "kcfi") \
    X(SAFE_STACK, "safe-stack") \
    X(SHADOW_CALL_STACK, "shadow-call-stack") \
    X(IMPLICIT_UNSIGNED_INTEGER_TRUNCATION, "implicit-unsigned-integer-truncation") \
    X(IMPLICIT_SIGNED_INTEGER_TRUNCATION, "implicit-signed-integer-truncation") \
    X(IMPLICIT_INTEGER_SIGN_CHANGE, "implicit-integer-sign-change") \
    X(IMPLICIT_BITFIELD_CONVERSION, "implicit-bitfield-conversion") \
    X(OBJC_CAST, "objc-cast") \
    X(LOCAL_BOUNDS, "local-bounds") \
    X(SCUDO, "scudo") \
    X(ALLOC_TOKEN, "alloc-token")

typedef enum PredefinesSanitizer
{
#define BUSTER_PREDEFINES_SANITIZER_ENUM(enumerator, name) PREDEFINES_SANITIZER_##enumerator,
    BUSTER_PREDEFINES_SANITIZERS(BUSTER_PREDEFINES_SANITIZER_ENUM)
#undef BUSTER_PREDEFINES_SANITIZER_ENUM
    PREDEFINES_SANITIZER_COUNT,
} PredefinesSanitizer;

static const char* const predefines_sanitizer_names[PREDEFINES_SANITIZER_COUNT] = {
#define BUSTER_PREDEFINES_SANITIZER_NAME(enumerator, name) name,
    BUSTER_PREDEFINES_SANITIZERS(BUSTER_PREDEFINES_SANITIZER_NAME)
#undef BUSTER_PREDEFINES_SANITIZER_NAME
};

#define BUSTER_PREDEFINES_SANITIZER(enumerator) (1ull << PREDEFINES_SANITIZER_##enumerator)

// The groups' members (Sanitizers.def's SANITIZER_GROUP), and the masks clang's driver reads.
#define BUSTER_PREDEFINES_SANITIZERS_ALL ((1ull << PREDEFINES_SANITIZER_COUNT) - 1)
#define BUSTER_PREDEFINES_SANITIZERS_MEMTAG \
    (BUSTER_PREDEFINES_SANITIZER(MEMTAG_STACK) | BUSTER_PREDEFINES_SANITIZER(MEMTAG_HEAP) | BUSTER_PREDEFINES_SANITIZER(MEMTAG_GLOBALS))
#define BUSTER_PREDEFINES_SANITIZERS_NULLABILITY \
    (BUSTER_PREDEFINES_SANITIZER(NULLABILITY_ARG) | BUSTER_PREDEFINES_SANITIZER(NULLABILITY_ASSIGN) | BUSTER_PREDEFINES_SANITIZER(NULLABILITY_RETURN))
#define BUSTER_PREDEFINES_SANITIZERS_SHIFT (BUSTER_PREDEFINES_SANITIZER(SHIFT_BASE) | BUSTER_PREDEFINES_SANITIZER(SHIFT_EXPONENT))
#define BUSTER_PREDEFINES_SANITIZERS_CFI \
    (BUSTER_PREDEFINES_SANITIZER(CFI_DERIVED_CAST) | BUSTER_PREDEFINES_SANITIZER(CFI_ICALL) | BUSTER_PREDEFINES_SANITIZER(CFI_MFCALL) | \
     BUSTER_PREDEFINES_SANITIZER(CFI_UNRELATED_CAST) | BUSTER_PREDEFINES_SANITIZER(CFI_NVCALL) | BUSTER_PREDEFINES_SANITIZER(CFI_VCALL))
#define BUSTER_PREDEFINES_SANITIZERS_UNDEFINED \
    (BUSTER_PREDEFINES_SANITIZER(ALIGNMENT) | BUSTER_PREDEFINES_SANITIZER(BOOL) | BUSTER_PREDEFINES_SANITIZER(BUILTIN) | \
     BUSTER_PREDEFINES_SANITIZER(ARRAY_BOUNDS) | BUSTER_PREDEFINES_SANITIZER(ENUM) | BUSTER_PREDEFINES_SANITIZER(FLOAT_CAST_OVERFLOW) | \
     BUSTER_PREDEFINES_SANITIZER(INTEGER_DIVIDE_BY_ZERO) | BUSTER_PREDEFINES_SANITIZER(NONNULL_ATTRIBUTE) | BUSTER_PREDEFINES_SANITIZER(NULL_POINTER) | \
     BUSTER_PREDEFINES_SANITIZER(OBJECT_SIZE) | BUSTER_PREDEFINES_SANITIZER(POINTER_OVERFLOW) | BUSTER_PREDEFINES_SANITIZER(RETURN) | \
     BUSTER_PREDEFINES_SANITIZER(RETURNS_NONNULL_ATTRIBUTE) | BUSTER_PREDEFINES_SANITIZERS_SHIFT | BUSTER_PREDEFINES_SANITIZER(SIGNED_INTEGER_OVERFLOW) | \
     BUSTER_PREDEFINES_SANITIZER(UNREACHABLE) | BUSTER_PREDEFINES_SANITIZER(VLA_BOUND) | BUSTER_PREDEFINES_SANITIZER(FUNCTION))
#define BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_INTEGER_TRUNCATION \
    (BUSTER_PREDEFINES_SANITIZER(IMPLICIT_UNSIGNED_INTEGER_TRUNCATION) | BUSTER_PREDEFINES_SANITIZER(IMPLICIT_SIGNED_INTEGER_TRUNCATION))
#define BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_INTEGER_ARITHMETIC_VALUE_CHANGE \
    (BUSTER_PREDEFINES_SANITIZER(IMPLICIT_INTEGER_SIGN_CHANGE) | BUSTER_PREDEFINES_SANITIZER(IMPLICIT_SIGNED_INTEGER_TRUNCATION))
#define BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_INTEGER_CONVERSION \
    (BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_INTEGER_ARITHMETIC_VALUE_CHANGE | BUSTER_PREDEFINES_SANITIZER(IMPLICIT_UNSIGNED_INTEGER_TRUNCATION))
#define BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_CONVERSION \
    (BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_INTEGER_CONVERSION | BUSTER_PREDEFINES_SANITIZER(IMPLICIT_BITFIELD_CONVERSION))
#define BUSTER_PREDEFINES_SANITIZERS_INTEGER \
    (BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_INTEGER_CONVERSION | BUSTER_PREDEFINES_SANITIZER(INTEGER_DIVIDE_BY_ZERO) | BUSTER_PREDEFINES_SANITIZERS_SHIFT | \
     BUSTER_PREDEFINES_SANITIZER(SIGNED_INTEGER_OVERFLOW) | BUSTER_PREDEFINES_SANITIZER(UNSIGNED_INTEGER_OVERFLOW) | \
     BUSTER_PREDEFINES_SANITIZER(UNSIGNED_SHIFT_BASE))
#define BUSTER_PREDEFINES_SANITIZERS_BOUNDS (BUSTER_PREDEFINES_SANITIZER(ARRAY_BOUNDS) | BUSTER_PREDEFINES_SANITIZER(LOCAL_BOUNDS))
// Those clang supports for x86_64-linux-gnu (ToolChain's and Linux's getSupportedSanitizers): all but MTE's and objc-cast.
#define BUSTER_PREDEFINES_SANITIZERS_SUPPORTED \
    (BUSTER_PREDEFINES_SANITIZERS_ALL & ~BUSTER_PREDEFINES_SANITIZERS_MEMTAG & ~BUSTER_PREDEFINES_SANITIZER(OBJC_CAST))
// Those that turn coverage instrumentation off (the ones SanitizerArgs.cpp's SupportsCoverage leaves out).
#define BUSTER_PREDEFINES_SANITIZERS_WITHOUT_COVERAGE \
    (BUSTER_PREDEFINES_SANITIZER(POINTER_COMPARE) | BUSTER_PREDEFINES_SANITIZER(POINTER_SUBTRACT) | BUSTER_PREDEFINES_SANITIZER(REALTIME) | \
     BUSTER_PREDEFINES_SANITIZER(CFI_CAST_STRICT) | BUSTER_PREDEFINES_SANITIZER(SCUDO))
// The CFI checks of classes, which need -fvisibility= (CFIClasses).
#define BUSTER_PREDEFINES_SANITIZERS_CFI_CLASSES \
    (BUSTER_PREDEFINES_SANITIZER(CFI_VCALL) | BUSTER_PREDEFINES_SANITIZER(CFI_NVCALL) | BUSTER_PREDEFINES_SANITIZER(CFI_MFCALL) | \
     BUSTER_PREDEFINES_SANITIZER(CFI_DERIVED_CAST) | BUSTER_PREDEFINES_SANITIZER(CFI_UNRELATED_CAST))
// Those -mexecute-only refuses (NotAllowedWithExecuteOnly).
#define BUSTER_PREDEFINES_SANITIZERS_NOT_EXECUTE_ONLY (BUSTER_PREDEFINES_SANITIZER(FUNCTION) | BUSTER_PREDEFINES_SANITIZER(KCFI))

// The groups, in Sanitizers.def's order: bit g of a group mask is the g-th, the group named itself (clang keeps a
// group's own bit beside its members').
static const struct
{
    const char* name;
    uint64_t members;
} predefines_sanitizer_groups[] = {
    { "memtag", BUSTER_PREDEFINES_SANITIZERS_MEMTAG },
    { "nullability", BUSTER_PREDEFINES_SANITIZERS_NULLABILITY },
    { "shift", BUSTER_PREDEFINES_SANITIZERS_SHIFT },
    { "cfi", BUSTER_PREDEFINES_SANITIZERS_CFI },
    { "undefined", BUSTER_PREDEFINES_SANITIZERS_UNDEFINED },
    { "undefined-trap", BUSTER_PREDEFINES_SANITIZERS_UNDEFINED },
    { "implicit-integer-truncation", BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_INTEGER_TRUNCATION },
    { "implicit-integer-arithmetic-value-change", BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_INTEGER_ARITHMETIC_VALUE_CHANGE },
    { "implicit-integer-conversion", BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_INTEGER_CONVERSION },
    { "implicit-conversion", BUSTER_PREDEFINES_SANITIZERS_IMPLICIT_CONVERSION },
    { "integer", BUSTER_PREDEFINES_SANITIZERS_INTEGER },
    { "bounds", BUSTER_PREDEFINES_SANITIZERS_BOUNDS },
    { "all", BUSTER_PREDEFINES_SANITIZERS_ALL },
};

#define BUSTER_PREDEFINES_SANITIZER_GROUP_MEMTAG 0x0001
#define BUSTER_PREDEFINES_SANITIZER_GROUP_UNDEFINED 0x0010
#define BUSTER_PREDEFINES_SANITIZER_GROUP_ALL 0x1000
// Every group's own bit.
#define BUSTER_PREDEFINES_SANITIZER_GROUPS 0x1fff
// The groups with a member clang supports for x86_64-linux-gnu: all but memtag.
#define BUSTER_PREDEFINES_SANITIZER_GROUPS_SUPPORTED (BUSTER_PREDEFINES_SANITIZER_GROUPS & ~BUSTER_PREDEFINES_SANITIZER_GROUP_MEMTAG)
_Static_assert(BUSTER_PREDEFINES_SANITIZER_GROUPS == (1u << BUSTER_ARRAY_LENGTH(predefines_sanitizer_groups)) - 1, "a bit per group");

// clang's IncompatibleGroups for Linux, in its order: with `group` on, `incompatible`'s are errors, and dropped.
static const struct
{
    uint64_t group;
    uint64_t incompatible;
} predefines_sanitizer_conflicts[] = {
    { BUSTER_PREDEFINES_SANITIZER(ADDRESS), BUSTER_PREDEFINES_SANITIZER(THREAD) | BUSTER_PREDEFINES_SANITIZER(MEMORY) },
    {
        BUSTER_PREDEFINES_SANITIZER(TYPE),
        BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS) | BUSTER_PREDEFINES_SANITIZER(MEMORY) |
            BUSTER_PREDEFINES_SANITIZER(LEAK) | BUSTER_PREDEFINES_SANITIZER(THREAD),
    },
    { BUSTER_PREDEFINES_SANITIZER(THREAD), BUSTER_PREDEFINES_SANITIZER(MEMORY) },
    { BUSTER_PREDEFINES_SANITIZER(LEAK), BUSTER_PREDEFINES_SANITIZER(THREAD) | BUSTER_PREDEFINES_SANITIZER(MEMORY) },
    {
        BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS),
        BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(LEAK) | BUSTER_PREDEFINES_SANITIZER(THREAD) | BUSTER_PREDEFINES_SANITIZER(MEMORY),
    },
    {
        BUSTER_PREDEFINES_SANITIZER(HWADDRESS),
        BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(THREAD) | BUSTER_PREDEFINES_SANITIZER(MEMORY) |
            BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS),
    },
    {
        BUSTER_PREDEFINES_SANITIZER(SCUDO),
        BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(HWADDRESS) | BUSTER_PREDEFINES_SANITIZER(LEAK) |
            BUSTER_PREDEFINES_SANITIZER(THREAD) | BUSTER_PREDEFINES_SANITIZER(MEMORY) | BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS),
    },
    {
        BUSTER_PREDEFINES_SANITIZER(SAFE_STACK),
        BUSTER_PREDEFINES_SANITIZER(LEAK) | BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(HWADDRESS) |
            BUSTER_PREDEFINES_SANITIZER(THREAD) | BUSTER_PREDEFINES_SANITIZER(MEMORY) | BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS),
    },
    {
        BUSTER_PREDEFINES_SANITIZER(KERNEL_HWADDRESS),
        BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(HWADDRESS) | BUSTER_PREDEFINES_SANITIZER(LEAK) |
            BUSTER_PREDEFINES_SANITIZER(THREAD) | BUSTER_PREDEFINES_SANITIZER(MEMORY) | BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS) |
            BUSTER_PREDEFINES_SANITIZER(SAFE_STACK),
    },
    {
        BUSTER_PREDEFINES_SANITIZER(KERNEL_MEMORY),
        BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(HWADDRESS) | BUSTER_PREDEFINES_SANITIZER(LEAK) |
            BUSTER_PREDEFINES_SANITIZER(THREAD) | BUSTER_PREDEFINES_SANITIZER(MEMORY) | BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS) |
            BUSTER_PREDEFINES_SANITIZER(SCUDO) | BUSTER_PREDEFINES_SANITIZER(SAFE_STACK),
    },
    {
        BUSTER_PREDEFINES_SANITIZERS_MEMTAG,
        BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS) | BUSTER_PREDEFINES_SANITIZER(HWADDRESS) |
            BUSTER_PREDEFINES_SANITIZER(KERNEL_HWADDRESS),
    },
    { BUSTER_PREDEFINES_SANITIZER(KCFI), BUSTER_PREDEFINES_SANITIZER(FUNCTION) },
    {
        BUSTER_PREDEFINES_SANITIZER(REALTIME),
        BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(THREAD) | BUSTER_PREDEFINES_SANITIZERS_UNDEFINED |
            BUSTER_PREDEFINES_SANITIZER(MEMORY) | BUSTER_PREDEFINES_SANITIZER(TYPE),
    },
    {
        BUSTER_PREDEFINES_SANITIZER(ALLOC_TOKEN),
        BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(HWADDRESS) | BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS) |
            BUSTER_PREDEFINES_SANITIZER(KERNEL_HWADDRESS) | BUSTER_PREDEFINES_SANITIZER(MEMORY),
    },
};

// clang's -fsanitize-coverage= features (SanitizerArgs.cpp's CoverageFeature), in its order: bit i is the i-th.
static const char* const predefines_coverage_names[] = {
    "func",
    "bb",
    "edge",
    "indirect-calls",
    "trace-bb",
    "trace-cmp",
    "trace-div",
    "trace-gep",
    "8bit-counters",
    "trace-pc",
    "trace-pc-guard",
    "no-prune",
    "inline-8bit-counters",
    "pc-table",
    "stack-depth",
    "inline-bool-flag",
    "trace-loads",
    "trace-stores",
    "control-flow",
    "trace-pc-entry-exit",
};

#define BUSTER_PREDEFINES_COVERAGE_FUNC 0x00001
#define BUSTER_PREDEFINES_COVERAGE_BB 0x00002
#define BUSTER_PREDEFINES_COVERAGE_EDGE 0x00004
#define BUSTER_PREDEFINES_COVERAGE_INDIRECT_CALLS 0x00008
#define BUSTER_PREDEFINES_COVERAGE_TRACE_CMP 0x00020
#define BUSTER_PREDEFINES_COVERAGE_TRACE_PC 0x00200
#define BUSTER_PREDEFINES_COVERAGE_TRACE_PC_GUARD 0x00400
#define BUSTER_PREDEFINES_COVERAGE_INLINE_8BIT_COUNTERS 0x01000
#define BUSTER_PREDEFINES_COVERAGE_PC_TABLE 0x02000
#define BUSTER_PREDEFINES_COVERAGE_STACK_DEPTH 0x04000
#define BUSTER_PREDEFINES_COVERAGE_INLINE_BOOL_FLAG 0x08000
#define BUSTER_PREDEFINES_COVERAGE_TRACE_LOADS 0x10000
#define BUSTER_PREDEFINES_COVERAGE_TRACE_STORES 0x20000
#define BUSTER_PREDEFINES_COVERAGE_CONTROL_FLOW 0x40000
#define BUSTER_PREDEFINES_COVERAGE_TRACE_PC_ENTRY_EXIT 0x80000
// What -fsanitize=fuzzer turns on (on Linux, with stack-depth).
#define BUSTER_PREDEFINES_COVERAGE_FUZZER \
    (BUSTER_PREDEFINES_COVERAGE_INLINE_8BIT_COUNTERS | BUSTER_PREDEFINES_COVERAGE_INDIRECT_CALLS | BUSTER_PREDEFINES_COVERAGE_TRACE_CMP | \
     BUSTER_PREDEFINES_COVERAGE_PC_TABLE | BUSTER_PREDEFINES_COVERAGE_STACK_DEPTH)
// The features that imply edge coverage when none of func, bb and edge is given.
#define BUSTER_PREDEFINES_COVERAGE_IMPLY_EDGE \
    (BUSTER_PREDEFINES_COVERAGE_TRACE_PC | BUSTER_PREDEFINES_COVERAGE_TRACE_PC_ENTRY_EXIT | BUSTER_PREDEFINES_COVERAGE_TRACE_PC_GUARD | \
     BUSTER_PREDEFINES_COVERAGE_INLINE_8BIT_COUNTERS | BUSTER_PREDEFINES_COVERAGE_INLINE_BOOL_FLAG | BUSTER_PREDEFINES_COVERAGE_CONTROL_FLOW)
// The features that instrument: __has_feature(coverage_sanitizer) is on with one of them (CodeGenOptions's
// hasSanitizeCoverage).
#define BUSTER_PREDEFINES_COVERAGE_INSTRUMENTING \
    (BUSTER_PREDEFINES_COVERAGE_FUNC | BUSTER_PREDEFINES_COVERAGE_BB | BUSTER_PREDEFINES_COVERAGE_EDGE | BUSTER_PREDEFINES_COVERAGE_INDIRECT_CALLS | \
     BUSTER_PREDEFINES_COVERAGE_TRACE_CMP | BUSTER_PREDEFINES_COVERAGE_TRACE_LOADS | BUSTER_PREDEFINES_COVERAGE_TRACE_STORES | \
     BUSTER_PREDEFINES_COVERAGE_CONTROL_FLOW)

// Sanitizers and groups, as clang's SanitizerMask holds both.
typedef struct PredefinesSanitizers PredefinesSanitizers;
struct PredefinesSanitizers
{
    uint64_t kinds;
    uint16_t groups;
};

// One value of a sanitizer list: its sanitizer, its group's bit, or nothing for a name clang doesn't know.
static PredefinesSanitizers predefines_sanitizer_value(const char* name, size_t length)
{
    PredefinesSanitizers result = { 0 };
    for (uint32_t i = 0; i < PREDEFINES_SANITIZER_COUNT; i += 1)
    {
        const bool same = strlen(predefines_sanitizer_names[i]) == length && memcmp(predefines_sanitizer_names[i], name, length) == 0;
        result.kinds |= same ? 1ull << i : 0;
    }
    for (uint32_t g = 0; g < BUSTER_ARRAY_LENGTH(predefines_sanitizer_groups); g += 1)
    {
        const bool same = strlen(predefines_sanitizer_groups[g].name) == length && memcmp(predefines_sanitizer_groups[g].name, name, length) == 0;
        result.groups |= same ? (uint16_t)(1u << g) : 0;
    }
    return result;
}

// clang's expandSanitizerGroups: the groups' members added, the groups' own bits kept. "all" is every sanitizer and every
// group's own bit, as clang's All (~SanitizerMask()) is, so -fno-sanitize=all turns off an earlier group too.
static PredefinesSanitizers predefines_sanitizers_expand(PredefinesSanitizers set)
{
    PredefinesSanitizers result = set;
    for (uint32_t g = 0; g < BUSTER_ARRAY_LENGTH(predefines_sanitizer_groups); g += 1)
    {
        result.kinds |= (set.groups >> g) & 1 ? predefines_sanitizer_groups[g].members : 0;
    }
    result.groups |= (uint16_t)((set.groups & BUSTER_PREDEFINES_SANITIZER_GROUP_ALL) != 0 ? BUSTER_PREDEFINES_SANITIZER_GROUPS : 0);
    return result;
}

// The values of a sanitizer list (the option's argument after its '='; empty values skipped, as clang's option parser
// skips them), as clang's parseArgValues reads them. -fsanitize= (`add`) refuses "all", which only the others take. With
// `valid`, each value clang doesn't take is reported as an error of the option `spelling` and makes *valid false; without
// it (as clang's lastArgumentForMask reads a list), nothing is.
static PredefinesSanitizers predefines_sanitizer_list(const char* spelling, const char* list, bool add, bool* valid)
{
    PredefinesSanitizers result = { 0 };
    const char* name = list;
    bool more = true;
    while (more)
    {
        const char* end = strchr(name, ',');
        const size_t length = end != 0 ? (size_t)(end - name) : strlen(name);
        PredefinesSanitizers value = predefines_sanitizer_value(name, length);
        value.groups &= add ? (uint16_t)~BUSTER_PREDEFINES_SANITIZER_GROUP_ALL : (uint16_t)0xffff;
        const bool known = value.kinds != 0 || value.groups != 0;
        if (!known && length != 0 && valid != 0)
        {
            fprintf(stderr, "error: unsupported argument '%.*s' to option '%s'\n", (int)length, name, spelling);
            *valid = false;
        }
        result.kinds |= value.kinds;
        result.groups |= value.groups;
        more = end != 0;
        name = end != 0 ? end + 1 : name;
    }
    return result;
}

// Appends `length` bytes of `text` to `buffer`, which keeps a NUL and stops at its capacity. Returns the new length.
static size_t predefines_text_append(char* buffer, size_t used, size_t capacity, const char* text, size_t length)
{
    const size_t copied = used + length < capacity ? length : (used < capacity ? capacity - 1 - used : 0);
    memcpy(buffer + used, text, copied);
    buffer[used + copied] = 0;
    return used + copied;
}

// clang's describeSanitizeArg: "-fsanitize=" and the values of `argument`'s list whose sanitizers or groups meet `mask`,
// into `buffer`.
static const char* predefines_sanitizer_describe(const char* argument, PredefinesSanitizers mask, char* buffer, size_t capacity)
{
    size_t used = predefines_text_append(buffer, 0, capacity, "-fsanitize=", 11);
    bool first = true;
    const char* name = strchr(argument, '=') + 1;
    bool more = true;
    while (more)
    {
        const char* end = strchr(name, ',');
        const size_t length = end != 0 ? (size_t)(end - name) : strlen(name);
        const PredefinesSanitizers value = predefines_sanitizers_expand(predefines_sanitizer_value(name, length));
        if (length != 0 && ((value.kinds & mask.kinds) != 0 || (value.groups & mask.groups) != 0))
        {
            used = first ? used : predefines_text_append(buffer, used, capacity, ",", 1);
            used = predefines_text_append(buffer, used, capacity, name, length);
            first = false;
        }
        more = end != 0;
        name = end != 0 ? end + 1 : name;
    }
    return buffer;
}

// clang's lastArgumentForMask: the last -fsanitize= that turns on one of `mask`'s sanitizers (less those a later
// -fno-sanitize= turns off), described as predefines_sanitizer_describe does; empty when none does, as clang 23's release
// build prints the case its assertion is about.
static const char* predefines_sanitizer_last(const PredefinesOptions* options, uint64_t mask, char* buffer, size_t capacity)
{
    const char* result = "";
    uint64_t left = mask;
    for (uint32_t i = options->sanitize_count; i > 0 && result[0] == 0; i -= 1)
    {
        const char* argument = options->sanitize_arguments[i - 1];
        const char* list = strchr(argument, '=') + 1;
        if (predefines_starts(argument, "-fsanitize="))
        {
            const PredefinesSanitizers added = predefines_sanitizers_expand(predefines_sanitizer_list("-fsanitize=", list, true, 0));
            result = (added.kinds & left) != 0 ? predefines_sanitizer_describe(argument, (PredefinesSanitizers){ .kinds = left }, buffer, capacity) : result;
        }
        else if (predefines_starts(argument, "-fno-sanitize="))
        {
            left &= ~predefines_sanitizers_expand(predefines_sanitizer_list("-fno-sanitize=", list, false, 0)).kinds;
        }
    }
    return result;
}

// Whether a -fsanitize-coverage= list is the old form, one number (as LLVM's getAsInteger reads an int, its radix from
// its prefix: 0x, 0b, 0o or a leading 0, or decimal).
static bool predefines_coverage_number(const char* list)
{
    // The one value, without empty ones.
    const char* begin = list;
    while (*begin == ',')
    {
        begin += 1;
    }
    const char* end = begin;
    while (*end != 0 && *end != ',')
    {
        end += 1;
    }
    const char* rest = end;
    while (*rest == ',')
    {
        rest += 1;
    }
    const char* digits = begin + (*begin == '-');
    const bool hexadecimal = digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X');
    const bool binary = digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B');
    const bool octal_prefix = digits[0] == '0' && digits[1] == 'o';
    const bool octal = !hexadecimal && !binary && !octal_prefix && digits[0] == '0' && digits + 1 < end && digits[1] >= '0' && digits[1] <= '9';
    const uint32_t radix = hexadecimal ? 16 : binary ? 2 : octal_prefix || octal ? 8 : 10;
    const char* p = digits + (hexadecimal || binary || octal_prefix ? 2 : octal ? 1 : 0);
    uint64_t value = 0;
    bool number = p < end && *rest == 0;
    for (; number && p < end; p += 1)
    {
        const char c = *p;
        const uint32_t digit = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'a' && c <= 'z' ? (uint32_t)(c - 'a' + 10) : c >= 'A' && c <= 'Z' ? (uint32_t)(c - 'A' + 10) : 99;
        number = digit < radix;
        value = number && value <= (1ull << 40) ? value * radix + digit : value;
    }
    // It must fit an int.
    return number && value <= (*begin == '-' ? 0x80000000ull : 0x7fffffffull);
}

// The features of a -fsanitize-coverage= or -fno-sanitize-coverage= list (empty values skipped), each clang doesn't know
// reported as an error of the option `spelling`, which makes *valid false.
static uint32_t predefines_coverage_list(const char* spelling, const char* list, bool* valid)
{
    uint32_t result = 0;
    const char* name = list;
    bool more = true;
    while (more)
    {
        const char* end = strchr(name, ',');
        const size_t length = end != 0 ? (size_t)(end - name) : strlen(name);
        uint32_t feature = 0;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(predefines_coverage_names); i += 1)
        {
            const bool same = strlen(predefines_coverage_names[i]) == length && memcmp(predefines_coverage_names[i], name, length) == 0;
            feature |= same ? 1u << i : 0;
        }
        if (feature == 0 && length != 0)
        {
            fprintf(stderr, "error: unsupported argument '%.*s' to option '%s'\n", (int)length, name, spelling);
            *valid = false;
        }
        result |= feature;
        more = end != 0;
        name = end != 0 ? end + 1 : name;
    }
    return result;
}

// clang's SanitizerArgs for x86_64-linux-gnu, after every option: options->sanitizers (the sanitizers on),
// options->ubsan_feature_ignored and options->sanitize_coverage, with clang's errors. The sanitizer options are read from
// the last back, each -fsanitize= turning on only what no later -fno-sanitize= turns off; the rules that need the whole
// set (the incompatible pairs, -flto for CFI, ...) come after, in clang's order. clang's rules that read
// -fsanitize-trap=, -fsanitize-minimal-runtime and -fsanitize-recover= aren't followed: they only refuse vptr and the
// recovery modes, and change no macro or feature. Returns false after an error.
static bool predefines_sanitizers(PredefinesOptions* options)
{
    bool result = true;
    char triple[256];
    char first[1024];
    char second[1024];
    const bool execute_only = options->execute_only != 0 && (strcmp(options->execute_only, "-mexecute-only") == 0 || strcmp(options->execute_only, "-mpure-code") == 0);
    const bool cross_dso = options->cfi_cross_dso == BUSTER_PREDEFINES_ON;
    const bool no_rtti = options->rtti == BUSTER_PREDEFINES_OFF;
    // -fno-strict-overflow is -fwrapv and -fwrapv-pointer unless they say otherwise.
    const bool no_strict_overflow = options->strict_overflow == BUSTER_PREDEFINES_OFF;
    const bool wrapv = options->wrapv != 0 ? options->wrapv == BUSTER_PREDEFINES_ON : no_strict_overflow;
    const bool wrapv_pointer = options->wrapv_pointer != 0 ? options->wrapv_pointer == BUSTER_PREDEFINES_ON : no_strict_overflow;
    const bool lto = options->lto != 0 && (strcmp(options->lto, "full") == 0 || strcmp(options->lto, "thin") == 0);
    // `removed`: what this option and the later ones turn off; `diagnosed`: what an error named already; `added`: every
    // sanitizer any -fsanitize= named (with its groups' members), turned off later or not.
    PredefinesSanitizers removed = { 0 };
    PredefinesSanitizers diagnosed = { 0 };
    uint64_t added = 0;
    uint64_t kinds = 0;
    uint64_t ignored = 0;
    uint32_t coverage = 0;
    for (uint32_t i = options->sanitize_count; i > 0; i -= 1)
    {
        const char* argument = options->sanitize_arguments[i - 1];
        const char* list = strchr(argument, '=') + 1;
        if (predefines_starts(argument, "-fsanitize="))
        {
            PredefinesSanitizers add = predefines_sanitizer_list("-fsanitize=", list, true, &result);
            // Without -O or with -O0, object-size is off (clang warns when it is named).
            removed.kinds |= options->optimization_flag ? 0 : BUSTER_PREDEFINES_SANITIZER(OBJECT_SIZE);
            added |= predefines_sanitizers_expand(add).kinds;
            add.kinds &= ~removed.kinds;
            add.groups &= (uint16_t)~removed.groups;
            // The rules for the sanitizers named themselves, not through a group.
            if (options->code_model != BUSTER_PREDEFINES_CODE_MODEL_SMALL && (add.kinds & BUSTER_PREDEFINES_SANITIZER(FUNCTION) & ~diagnosed.kinds) != 0)
            {
                fprintf(stderr, "error: invalid argument '-fsanitize=function' only allowed with '-mcmodel=small'\n");
                add.kinds &= ~BUSTER_PREDEFINES_SANITIZER(FUNCTION);
                diagnosed.kinds |= BUSTER_PREDEFINES_SANITIZER(FUNCTION);
                result = false;
            }
            const PredefinesSanitizers execute = { .kinds = execute_only ? add.kinds & BUSTER_PREDEFINES_SANITIZERS_NOT_EXECUTE_ONLY & ~diagnosed.kinds : 0 };
            if (execute.kinds != 0)
            {
                fprintf(stderr, "error: invalid argument '%s' not allowed with '%s'\n", predefines_sanitizer_describe(argument, execute, first, sizeof(first)),
                        options->execute_only);
                diagnosed.kinds |= execute.kinds;
                result = false;
            }
            add.kinds &= execute_only ? ~BUSTER_PREDEFINES_SANITIZERS_NOT_EXECUTE_ONLY : ~0ull;
            if (cross_dso && (add.kinds & BUSTER_PREDEFINES_SANITIZER(CFI_MFCALL) & ~diagnosed.kinds) != 0)
            {
                fprintf(stderr, "error: invalid argument '-fsanitize=cfi-mfcall' not allowed with '-fsanitize-cfi-cross-dso'\n");
                add.kinds &= ~BUSTER_PREDEFINES_SANITIZER(CFI_MFCALL);
                diagnosed.kinds |= BUSTER_PREDEFINES_SANITIZER(CFI_MFCALL);
                result = false;
            }
            const PredefinesSanitizers unsupported = {
                .kinds = add.kinds & ~BUSTER_PREDEFINES_SANITIZERS_SUPPORTED & ~diagnosed.kinds,
                .groups = (uint16_t)(add.groups & ~BUSTER_PREDEFINES_SANITIZER_GROUPS_SUPPORTED & ~diagnosed.groups),
            };
            if (unsupported.kinds != 0 || unsupported.groups != 0)
            {
                predefines_target_triple(options, triple, sizeof(triple));
                fprintf(stderr, "error: unsupported option '%s' for target '%s'\n", predefines_sanitizer_describe(argument, unsupported, first, sizeof(first)), triple);
                diagnosed.kinds |= unsupported.kinds;
                diagnosed.groups |= unsupported.groups;
                result = false;
            }
            add.kinds &= BUSTER_PREDEFINES_SANITIZERS_SUPPORTED;
            add.groups &= BUSTER_PREDEFINES_SANITIZER_GROUPS_SUPPORTED;
            if ((add.kinds & BUSTER_PREDEFINES_SANITIZER(VPTR)) != 0 && no_rtti)
            {
                fprintf(stderr, "error: invalid argument '-fsanitize=vptr' not allowed with '-fno-rtti'\n");
                removed.kinds |= BUSTER_PREDEFINES_SANITIZER(VPTR);
                result = false;
            }
            // Then with the groups' members, which the same rules drop silently. -fwrapv (or -fno-strict-overflow) keeps
            // -fsanitize=undefined from checking signed overflow, and -fwrapv-pointer pointer overflow.
            const bool undefined = (add.groups & BUSTER_PREDEFINES_SANITIZER_GROUP_UNDEFINED) != 0;
            uint64_t on = predefines_sanitizers_expand(add).kinds & ~removed.kinds & BUSTER_PREDEFINES_SANITIZERS_SUPPORTED;
            on &= execute_only ? ~BUSTER_PREDEFINES_SANITIZERS_NOT_EXECUTE_ONLY : ~0ull;
            on &= cross_dso ? ~BUSTER_PREDEFINES_SANITIZER(CFI_MFCALL) : ~0ull;
            on &= undefined && wrapv ? ~BUSTER_PREDEFINES_SANITIZER(SIGNED_INTEGER_OVERFLOW) : ~0ull;
            on &= undefined && wrapv_pointer ? ~BUSTER_PREDEFINES_SANITIZER(POINTER_OVERFLOW) : ~0ull;
            on |= (on & BUSTER_PREDEFINES_SANITIZER(FUZZER)) != 0 ? BUSTER_PREDEFINES_SANITIZER(FUZZER_NO_LINK) : 0;
            coverage |= (on & BUSTER_PREDEFINES_SANITIZER(FUZZER_NO_LINK)) != 0 ? BUSTER_PREDEFINES_COVERAGE_FUZZER : 0;
            kinds |= on;
        }
        else if (predefines_starts(argument, "-fno-sanitize="))
        {
            const PredefinesSanitizers remove = predefines_sanitizers_expand(predefines_sanitizer_list("-fno-sanitize=", list, false, &result));
            removed.kinds |= remove.kinds;
            removed.groups |= remove.groups;
        }
        else if (predefines_starts(argument, "-fsanitize-ignore-for-ubsan-feature="))
        {
            ignored |= predefines_sanitizers_expand(predefines_sanitizer_list("-fsanitize-ignore-for-ubsan-feature=", list, false, &result)).kinds;
        }
    }
    // Linux turns no sanitizer on by default. Without RTTI, vptr is off.
    kinds &= no_rtti ? ~BUSTER_PREDEFINES_SANITIZER(VPTR) : ~0ull;
    if ((kinds & BUSTER_PREDEFINES_SANITIZERS_CFI) != 0 && !lto)
    {
        fprintf(stderr, "error: invalid argument '%s' only allowed with '-flto'\n",
                predefines_sanitizer_last(options, kinds & BUSTER_PREDEFINES_SANITIZERS_CFI, first, sizeof(first)));
        result = false;
    }
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(predefines_sanitizer_conflicts); c += 1)
    {
        const uint64_t group = predefines_sanitizer_conflicts[c].group;
        const uint64_t incompatible = (kinds & group) != 0 ? kinds & predefines_sanitizer_conflicts[c].incompatible : 0;
        if (incompatible != 0)
        {
            fprintf(stderr, "error: invalid argument '%s' not allowed with '%s'\n", predefines_sanitizer_last(options, group, first, sizeof(first)),
                    predefines_sanitizer_last(options, incompatible, second, sizeof(second)));
            kinds &= ~incompatible;
            result = false;
        }
    }
    // kcfi and CFI ever named together, whatever turned them off.
    if ((added & BUSTER_PREDEFINES_SANITIZER(KCFI)) != 0 && (added & BUSTER_PREDEFINES_SANITIZERS_CFI) != 0)
    {
        fprintf(stderr, "error: invalid argument '-fsanitize=kcfi' not allowed with '%s'\n",
                predefines_sanitizer_last(options, BUSTER_PREDEFINES_SANITIZERS_CFI, first, sizeof(first)));
        result = false;
    }
    // Coverage, in command-line order: a number (the old form) turns it off, and so does a sanitizer that doesn't
    // support it, whenever a -fsanitize-coverage= list comes.
    for (uint32_t i = 0; i < options->sanitize_count; i += 1)
    {
        const char* argument = options->sanitize_arguments[i];
        const char* list = strchr(argument, '=') + 1;
        const bool on = predefines_starts(argument, "-fsanitize-coverage=");
        if (on && predefines_coverage_number(list))
        {
            coverage = 0;
        }
        else if (on)
        {
            coverage |= predefines_coverage_list("-fsanitize-coverage=", list, &result);
            coverage = (added & ~removed.kinds & BUSTER_PREDEFINES_SANITIZERS_WITHOUT_COVERAGE) != 0 ? 0 : coverage;
        }
        else if (predefines_starts(argument, "-fno-sanitize-coverage="))
        {
            coverage &= ~predefines_coverage_list("-fno-sanitize-coverage=", list, &result);
        }
    }
    static const struct
    {
        uint32_t features;
        const char* first;
        const char* second;
    } exclusive[] = {
        { BUSTER_PREDEFINES_COVERAGE_FUNC | BUSTER_PREDEFINES_COVERAGE_BB, "func", "bb" },
        { BUSTER_PREDEFINES_COVERAGE_FUNC | BUSTER_PREDEFINES_COVERAGE_EDGE, "func", "edge" },
        { BUSTER_PREDEFINES_COVERAGE_BB | BUSTER_PREDEFINES_COVERAGE_EDGE, "bb", "edge" },
    };
    for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(exclusive); e += 1)
    {
        if ((coverage & exclusive[e].features) == exclusive[e].features)
        {
            fprintf(stderr, "error: invalid argument '-fsanitize-coverage=%s' not allowed with '-fsanitize-coverage=%s'\n", exclusive[e].first, exclusive[e].second);
            result = false;
        }
    }
    // The tracing features without a kind of coverage imply edge coverage, and stack-depth function coverage.
    const bool kind = (coverage & (BUSTER_PREDEFINES_COVERAGE_FUNC | BUSTER_PREDEFINES_COVERAGE_BB | BUSTER_PREDEFINES_COVERAGE_EDGE)) != 0;
    coverage |= !kind && (coverage & BUSTER_PREDEFINES_COVERAGE_IMPLY_EDGE) != 0 ? BUSTER_PREDEFINES_COVERAGE_EDGE : 0;
    coverage |= !kind && (coverage & BUSTER_PREDEFINES_COVERAGE_STACK_DEPTH) != 0 ? BUSTER_PREDEFINES_COVERAGE_FUNC : 0;
    // pointer-compare and pointer-subtract need address, ever named.
    const uint64_t pointers = BUSTER_PREDEFINES_SANITIZER(POINTER_COMPARE) | BUSTER_PREDEFINES_SANITIZER(POINTER_SUBTRACT);
    if ((added & BUSTER_PREDEFINES_SANITIZER(ADDRESS)) == 0 && (added & pointers & ~removed.kinds) != 0)
    {
        fprintf(stderr, "error: invalid argument '%s' only allowed with '-fsanitize=address'\n", predefines_sanitizer_last(options, pointers, first, sizeof(first)));
        result = false;
    }
    // The CFI checks of classes need a -fvisibility=.
    if ((kinds & BUSTER_PREDEFINES_SANITIZERS_CFI_CLASSES) != 0 && !options->visibility_given)
    {
        fprintf(stderr, "error: invalid argument '%s' only allowed with '-fvisibility='\n",
                predefines_sanitizer_last(options, kinds & BUSTER_PREDEFINES_SANITIZERS_CFI_CLASSES, first, sizeof(first)));
        result = false;
    }
    options->sanitizers = kinds;
    options->ubsan_feature_ignored = ignored;
    options->sanitize_coverage = (coverage & BUSTER_PREDEFINES_COVERAGE_INSTRUMENTING) != 0;
    return result;
}

// The options clang's driver reads only the last of, after every option (an earlier invalid one doesn't count): -mtune,
// -mfpmath=, -mcmodel=, -mabi=, -fveclib=, -O, -fgnuc-version=, -fcf-protection=, -fopenmp='s runtime, -flto= and
// -fvisibility=. Sets the values they give (fpmath, code_model, openmp) and reports clang's errors; false after one.
static bool predefines_finish_options(PredefinesOptions* options)
{
    bool result = true;
    // The target triple, for the messages that name it.
    char triple[256];
    if (options->mtune != 0 && !predefines_tune_valid(options->mtune))
    {
        fprintf(stderr, "error: unknown target CPU '%s'\n", options->mtune);
        result = false;
    }
    const char* fpmath = options->fpmath_name;
    options->fpmath = fpmath == 0                 ? 0
                      : strcmp(fpmath, "sse") == 0 ? BUSTER_PREDEFINES_FPMATH_SSE
                      : strcmp(fpmath, "387") == 0 ? BUSTER_PREDEFINES_FPMATH_387
                                                   : 0xff;
    if (options->fpmath == 0xff)
    {
        fprintf(stderr, "error: unknown FP unit '%s'\n", fpmath);
        result = false;
    }
    const char* model = options->code_model_name;
    options->code_model = model == 0 || strcmp(model, "small") == 0 ? BUSTER_PREDEFINES_CODE_MODEL_SMALL
                          : strcmp(model, "kernel") == 0             ? BUSTER_PREDEFINES_CODE_MODEL_KERNEL
                          : strcmp(model, "medium") == 0             ? BUSTER_PREDEFINES_CODE_MODEL_MEDIUM
                          : strcmp(model, "large") == 0              ? BUSTER_PREDEFINES_CODE_MODEL_LARGE
                                                                     : 0xff;
    if (options->code_model == 0xff)
    {
        predefines_target_triple(options, triple, sizeof(triple));
        fprintf(stderr, "error: unsupported argument '%s' to option '-mcmodel=' for target '%s'\n", model, triple);
        result = false;
    }
    if (options->abi != 0 && strcmp(options->abi, "sysv") != 0)
    {
        predefines_target_triple(options, triple, sizeof(triple));
        fprintf(stderr, "error: unsupported option '-mabi=' for target '%s'\n", triple);
        result = false;
    }
    // The vector libraries: ArmPL and SLEEF are refused for x86-64 (naming the triple's architecture), and a name clang
    // doesn't know is an invalid value.
    static const char* const veclibs[] = { "Accelerate", "libmvec", "MASSV", "SVML", "SLEEF", "Darwin_libsystem_m", "ArmPL", "AMDLIBM", "none" };
    const char* veclib = options->veclib;
    bool known = veclib == 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(veclibs) && !known; i += 1)
    {
        known = strcmp(veclib, veclibs[i]) == 0;
    }
    if (veclib != 0 && (strcmp(veclib, "SLEEF") == 0 || strcmp(veclib, "ArmPL") == 0))
    {
        predefines_target_triple(options, triple, sizeof(triple));
        fprintf(stderr, "error: unsupported option '%s' for target '%.*s'\n", veclib, (int)(strchr(triple, '-') - triple), triple);
        result = false;
    }
    else if (!known)
    {
        fprintf(stderr, "error: invalid value '%s' in '-fveclib=%s'\n", veclib, veclib);
        result = false;
    }
    if (options->optimization_invalid != 0)
    {
        fprintf(stderr, "error: invalid integral value '%s' in '%s'\n", options->optimization_invalid + 2, options->optimization_invalid);
        result = false;
    }
    if (options->gnuc_invalid != 0)
    {
        fprintf(stderr, "error: invalid value '%s' in '%s'\n", options->gnuc_invalid + 15, options->gnuc_invalid);
        result = false;
    }
    if (options->fp_model_invalid != 0 && !options->ofast)
    {
        fprintf(stderr, "error: unsupported argument '%s' to option '-ffp-model='\n", options->fp_model_invalid);
        result = false;
    }
    if (options->cf_protection_invalid != 0)
    {
        fprintf(stderr, "error: invalid value '%s' in '%s'\n", options->cf_protection_invalid + 16, options->cf_protection_invalid);
        result = false;
    }
    // OpenMP's runtime is checked only when OpenMP is on. libgomp's gets no _OPENMP from clang: only libomp's (and its
    // old name, libiomp5's) do.
    const char* runtime = options->openmp_runtime != 0 ? options->openmp_runtime : "libomp";
    const bool openmp_runtime = strcmp(runtime, "libomp") == 0 || strcmp(runtime, "libiomp5") == 0;
    if (options->openmp_enabled && !openmp_runtime && strcmp(runtime, "libgomp") != 0)
    {
        fprintf(stderr, "error: unsupported argument '%s' to option '-fopenmp='\n", runtime);
        result = false;
    }
    options->openmp = options->openmp_enabled && openmp_runtime;
    const char* lto = options->lto;
    if (lto != 0 && strcmp(lto, "full") != 0 && strcmp(lto, "thin") != 0 && strcmp(lto, "none") != 0)
    {
        fprintf(stderr, "error: unsupported argument '%s' to option '-flto='\n", lto);
        result = false;
    }
    const char* visibility = options->visibility;
    if (visibility != 0 && strcmp(visibility, "default") != 0 && strcmp(visibility, "hidden") != 0 && strcmp(visibility, "internal") != 0 &&
        strcmp(visibility, "protected") != 0)
    {
        fprintf(stderr, "error: invalid value '%s' in '-fvisibility=%s'\n", visibility, visibility);
        result = false;
    }
    // After the code model, which some of them depend on.
    result = predefines_finish_neutral(options) && result;
    return result;
}

// What clang's driver decides from the whole command line, after every option: the target, the options it reads only the
// last of, the -flto mode, -fvisibility='s value and the sanitizers (predefines_sanitizers). Reports clang's errors and
// returns false after one.
static __attribute__((unused)) bool predefines_finish(PredefinesOptions* options)
{
    bool result = options->target == 0 || predefines_target_valid(options->target);
    if (!result)
    {
        fprintf(stderr, "error: unsupported target '%s': buster predefines for x86_64-linux-gnu only\n", options->target);
    }
    else
    {
        result = predefines_finish_options(options);
        result = predefines_sanitizers(options) && result;
    }
    return result;
}

// clang's sanitizer features (Features.def), which the oracle's mode answers from the sanitizers predefines_finish left
// on: 1 when `name` is one of them and on, and 0 otherwise.
static __attribute__((unused)) uint32_t predefines_sanitizer_feature(const PredefinesOptions* options, const uint8_t* name, uint32_t length)
{
    static const struct
    {
        const char* name;
        uint64_t kinds;
    } features[] = {
        { "address_sanitizer", BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS) },
        { "leak_sanitizer", BUSTER_PREDEFINES_SANITIZER(LEAK) },
        { "hwaddress_sanitizer", BUSTER_PREDEFINES_SANITIZER(HWADDRESS) | BUSTER_PREDEFINES_SANITIZER(KERNEL_HWADDRESS) },
        { "memtag_stack", BUSTER_PREDEFINES_SANITIZER(MEMTAG_STACK) },
        { "memtag_heap", BUSTER_PREDEFINES_SANITIZER(MEMTAG_HEAP) },
        { "memtag_globals", BUSTER_PREDEFINES_SANITIZER(MEMTAG_GLOBALS) },
        { "alignment_sanitizer", BUSTER_PREDEFINES_SANITIZER(ALIGNMENT) },
        { "bool_sanitizer", BUSTER_PREDEFINES_SANITIZER(BOOL) },
        { "builtin_sanitizer", BUSTER_PREDEFINES_SANITIZER(BUILTIN) },
        { "array_bounds_sanitizer", BUSTER_PREDEFINES_SANITIZER(ARRAY_BOUNDS) },
        { "enum_sanitizer", BUSTER_PREDEFINES_SANITIZER(ENUM) },
        { "float_cast_overflow_sanitizer", BUSTER_PREDEFINES_SANITIZER(FLOAT_CAST_OVERFLOW) },
        { "integer_divide_by_zero_sanitizer", BUSTER_PREDEFINES_SANITIZER(INTEGER_DIVIDE_BY_ZERO) },
        { "nonnull_attribute_sanitizer", BUSTER_PREDEFINES_SANITIZER(NONNULL_ATTRIBUTE) },
        { "null_sanitizer", BUSTER_PREDEFINES_SANITIZER(NULL_POINTER) },
        { "object_size_sanitizer", BUSTER_PREDEFINES_SANITIZER(OBJECT_SIZE) },
        { "pointer_overflow_sanitizer", BUSTER_PREDEFINES_SANITIZER(POINTER_OVERFLOW) },
        { "return_sanitizer", BUSTER_PREDEFINES_SANITIZER(RETURN) },
        { "returns_nonnull_attribute_sanitizer", BUSTER_PREDEFINES_SANITIZER(RETURNS_NONNULL_ATTRIBUTE) },
        { "shift_base_sanitizer", BUSTER_PREDEFINES_SANITIZER(SHIFT_BASE) },
        { "shift_exponent_sanitizer", BUSTER_PREDEFINES_SANITIZER(SHIFT_EXPONENT) },
        { "shift_sanitizer", BUSTER_PREDEFINES_SANITIZERS_SHIFT },
        { "signed_integer_overflow_sanitizer", BUSTER_PREDEFINES_SANITIZER(SIGNED_INTEGER_OVERFLOW) },
        { "unreachable_sanitizer", BUSTER_PREDEFINES_SANITIZER(UNREACHABLE) },
        { "vla_bound_sanitizer", BUSTER_PREDEFINES_SANITIZER(VLA_BOUND) },
        { "function_sanitizer", BUSTER_PREDEFINES_SANITIZER(FUNCTION) },
        { "realtime_sanitizer", BUSTER_PREDEFINES_SANITIZER(REALTIME) },
        { "numerical_stability_sanitizer", BUSTER_PREDEFINES_SANITIZER(NUMERICAL) },
        { "memory_sanitizer", BUSTER_PREDEFINES_SANITIZER(MEMORY) | BUSTER_PREDEFINES_SANITIZER(KERNEL_MEMORY) },
        { "type_sanitizer", BUSTER_PREDEFINES_SANITIZER(TYPE) },
        { "thread_sanitizer", BUSTER_PREDEFINES_SANITIZER(THREAD) },
        { "dataflow_sanitizer", BUSTER_PREDEFINES_SANITIZER(DATAFLOW) },
        { "scudo", BUSTER_PREDEFINES_SANITIZER(SCUDO) },
        { "cfi_sanitizer", BUSTER_PREDEFINES_SANITIZERS_CFI },
        { "cfi_cast_strict_sanitizer", BUSTER_PREDEFINES_SANITIZER(CFI_CAST_STRICT) },
        { "cfi_derived_cast_sanitizer", BUSTER_PREDEFINES_SANITIZER(CFI_DERIVED_CAST) },
        { "cfi_icall_sanitizer", BUSTER_PREDEFINES_SANITIZER(CFI_ICALL) },
        { "cfi_mfcall_sanitizer", BUSTER_PREDEFINES_SANITIZER(CFI_MFCALL) },
        { "cfi_unrelated_cast_sanitizer", BUSTER_PREDEFINES_SANITIZER(CFI_UNRELATED_CAST) },
        { "cfi_nvcall_sanitizer", BUSTER_PREDEFINES_SANITIZER(CFI_NVCALL) },
        { "cfi_vcall_sanitizer", BUSTER_PREDEFINES_SANITIZER(CFI_VCALL) },
        { "kcfi", BUSTER_PREDEFINES_SANITIZER(KCFI) },
        { "kcfi_arity", BUSTER_PREDEFINES_SANITIZER(KCFI) },
        { "safe_stack", BUSTER_PREDEFINES_SANITIZER(SAFE_STACK) },
        { "shadow_call_stack", BUSTER_PREDEFINES_SANITIZER(SHADOW_CALL_STACK) },
    };
    // undefined_behavior_sanitizer: one of the undefined behavior checks -fsanitize-ignore-for-ubsan-feature= doesn't
    // name; coverage_sanitizer: coverage instrumentation.
    const bool undefined = length == 28 && memcmp(name, "undefined_behavior_sanitizer", 28) == 0;
    const bool coverage = length == 18 && memcmp(name, "coverage_sanitizer", 18) == 0;
    uint32_t result = undefined ? (options->sanitizers & BUSTER_PREDEFINES_SANITIZERS_UNDEFINED & ~options->ubsan_feature_ignored) != 0
                      : coverage ? options->sanitize_coverage
                                 : 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(features); i += 1)
    {
        const bool same = strlen(features[i].name) == length && memcmp(features[i].name, name, length) == 0;
        result |= same && (options->sanitizers & features[i].kinds) != 0;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The macros

// The CPU and features the options give: -march's CPU (or x86-64), with -march=native's host features before the -m
// flags, the flags (each feature's last), and the soft rules. *cpu gets the CPU's index in predefines_cpus. Returns
// false after an error (an unknown host for -march=native, or -mfpmath against the features).
static bool predefines_resolve(const PredefinesOptions* options, PredefinesFeatures* features, int32_t* cpu)
{
    bool result = true;
    PredefinesFeatures listed = { 0 };
    PredefinesFeatures detected = { 0 };
    const bool native = options->march != 0 && strcmp(options->march, "native") == 0;
    const char* name = options->march != 0 ? options->march : "x86-64";
    if (native)
    {
        const char* host = predefines_host(&listed, &detected);
        // The driver falls back to its default CPU when the host is unknown.
        name = strcmp(host, "generic") != 0 ? host : "x86-64";
    }
    *cpu = predefines_cpu_index(name);
    if (*cpu < 0)
    {
        fprintf(stderr, "error: unknown target CPU '%s'\n", name);
        result = false;
    }
    // x86-64 always has sse2, then the CPU's features.
    *features = predefines_features[predefines_feature("sse2")].implies;
    if (*cpu >= 0)
    {
        predefines_add(features, predefines_cpus[*cpu].features);
    }
    // Each feature's last flag: the driver keeps only that one (a later flag for a feature drops the earlier ones,
    // native's among them). last[f] is the index of f's last flag, plus 1, or 0.
    uint32_t last[BUSTER_PREDEFINES_FEATURE_WORDS * 64] = { 0 };
    for (uint32_t i = 0; i < options->feature_count; i += 1)
    {
        last[options->features[i] >> 1] = i + 1;
    }
    PredefinesFeatures explicitly_off = { 0 };
    // Native's list first (in table order: the host's features are consistent, so the order doesn't matter), but for
    // the features a flag names.
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(predefines_features) && native; f += 1)
    {
        if (predefines_has(listed, f) && last[f] == 0)
        {
            if (predefines_has(detected, f))
            {
                predefines_add(features, predefines_features[f].implies);
            }
            else
            {
                predefines_remove(features, predefines_features[f].implied_by);
                explicitly_off.words[f / 64] |= 1ull << (f % 64);
            }
        }
    }
    // The flags, each feature's last, in order.
    for (uint32_t i = 0; i < options->feature_count; i += 1)
    {
        const uint32_t feature = options->features[i] >> 1;
        if (last[feature] == i + 1)
        {
            if ((options->features[i] & 1) != 0)
            {
                predefines_add(features, predefines_features[feature].implies);
            }
            else
            {
                predefines_remove(features, predefines_features[feature].implied_by);
                explicitly_off.words[feature / 64] |= 1ull << (feature % 64);
            }
        }
    }
    if (options->inline_asm_use_gpr32)
    {
        predefines_add(features, predefines_features[predefines_feature("inline-asm-use-gpr32")].implies);
    }
    // The soft rules: popcnt and crc32 come with sse4.2, mmx with sse and xsave with avx, unless turned off explicitly.
    static const char* const soft[4][2] = { { "sse4.2", "popcnt" }, { "sse", "mmx" }, { "avx", "xsave" }, { "sse4.2", "crc32" } };
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(soft); s += 1)
    {
        const uint32_t trigger = predefines_feature(soft[s][0]);
        const uint32_t added = predefines_feature(soft[s][1]);
        if (predefines_has(*features, trigger) && !predefines_has(explicitly_off, added))
        {
            features->words[added / 64] |= 1ull << (added % 64);
        }
    }
    // -mfpmath must agree with the features: 387 without SSE, sse with it.
    const bool sse = predefines_has(*features, predefines_feature("sse"));
    if ((options->fpmath == BUSTER_PREDEFINES_FPMATH_387 && sse) || (options->fpmath == BUSTER_PREDEFINES_FPMATH_SSE && !sse))
    {
        fprintf(stderr, "error: the '%s' unit is not supported with this instruction set\n", options->fpmath == BUSTER_PREDEFINES_FPMATH_387 ? "387" : "sse");
        result = false;
    }
    return result;
}

// The clang feature behind each of x86_target.c's features (X86Feature), by which predefines_target finds it among the
// resolved ones: none for RDTSCP and CLFLUSH, which clang has no feature for.
static const char* const predefines_x86_features[X86_FEATURE_COUNT] = {
    [X86_FEATURE_SSE3] = "sse3",
    [X86_FEATURE_SSSE3] = "ssse3",
    [X86_FEATURE_SSE41] = "sse4.1",
    [X86_FEATURE_SSE42] = "sse4.2",
    [X86_FEATURE_CRC32] = "crc32",
    [X86_FEATURE_POPCNT] = "popcnt",
    [X86_FEATURE_CX16] = "cx16",
    [X86_FEATURE_SAHF] = "sahf",
    [X86_FEATURE_AVX] = "avx",
    [X86_FEATURE_AVX2] = "avx2",
    [X86_FEATURE_BMI] = "bmi",
    [X86_FEATURE_BMI2] = "bmi2",
    [X86_FEATURE_F16C] = "f16c",
    [X86_FEATURE_FMA] = "fma",
    [X86_FEATURE_LZCNT] = "lzcnt",
    [X86_FEATURE_MOVBE] = "movbe",
    [X86_FEATURE_XSAVE] = "xsave",
    [X86_FEATURE_AVX512F] = "avx512f",
    [X86_FEATURE_AVX512BW] = "avx512bw",
    [X86_FEATURE_AVX512CD] = "avx512cd",
    [X86_FEATURE_AVX512DQ] = "avx512dq",
    [X86_FEATURE_AVX512VL] = "avx512vl",
    [X86_FEATURE_ADX] = "adx",
    [X86_FEATURE_AES] = "aes",
    [X86_FEATURE_AVX512BF16] = "avx512bf16",
    [X86_FEATURE_AVX512BITALG] = "avx512bitalg",
    [X86_FEATURE_AVX512IFMA] = "avx512ifma",
    [X86_FEATURE_AVX512VBMI] = "avx512vbmi",
    [X86_FEATURE_AVX512VBMI2] = "avx512vbmi2",
    [X86_FEATURE_AVX512VNNI] = "avx512vnni",
    [X86_FEATURE_AVX512VP2INTERSECT] = "avx512vp2intersect",
    [X86_FEATURE_AVX512VPOPCNTDQ] = "avx512vpopcntdq",
    [X86_FEATURE_AVXVNNI] = "avxvnni",
    [X86_FEATURE_CLFLUSHOPT] = "clflushopt",
    [X86_FEATURE_CLWB] = "clwb",
    [X86_FEATURE_CLZERO] = "clzero",
    [X86_FEATURE_FSGSBASE] = "fsgsbase",
    [X86_FEATURE_GFNI] = "gfni",
    [X86_FEATURE_INVPCID] = "invpcid",
    [X86_FEATURE_MOVDIR64B] = "movdir64b",
    [X86_FEATURE_MOVDIRI] = "movdiri",
    [X86_FEATURE_MWAITX] = "mwaitx",
    [X86_FEATURE_PCLMUL] = "pclmul",
    [X86_FEATURE_PKU] = "pku",
    [X86_FEATURE_PREFETCHI] = "prefetchi",
    [X86_FEATURE_PRFCHW] = "prfchw",
    [X86_FEATURE_RDPID] = "rdpid",
    [X86_FEATURE_RDPRU] = "rdpru",
    [X86_FEATURE_RDRND] = "rdrnd",
    [X86_FEATURE_RDSEED] = "rdseed",
    [X86_FEATURE_SHA] = "sha",
    [X86_FEATURE_SHSTK] = "shstk",
    [X86_FEATURE_SSE4A] = "sse4a",
    [X86_FEATURE_VAES] = "vaes",
    [X86_FEATURE_VPCLMULQDQ] = "vpclmulqdq",
    [X86_FEATURE_WBNOINVD] = "wbnoinvd",
    [X86_FEATURE_XSAVEC] = "xsavec",
    [X86_FEATURE_XSAVEOPT] = "xsaveopt",
    [X86_FEATURE_XSAVES] = "xsaves",
};

// x86_target.c's architecture for a -march or -mtune name: one of its six profiles, or X86_ARCH_OTHER for clang's other
// CPUs. "generic" (-mtune's default) is the baseline's.
static X86Architecture predefines_x86_architecture(const char* name)
{
    static const char* const profiles[] = { "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native" };
    X86Architecture result = strcmp(name, "generic") == 0 ? X86_ARCH_BASELINE : X86_ARCH_OTHER;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(profiles); i += 1)
    {
        result = strcmp(name, profiles[i]) == 0 ? (X86Architecture)i : result;
    }
    return result;
}

// The generated code's instruction set for the options (#180): x86_target.c's features that the resolved ones have, as
// predefines_x86_features names them, with RDTSCP and CLFLUSH from x86_target_parse's profile where -march names one
// (native's from CPUID), and that profile's architecture (X86_ARCH_OTHER for clang's other CPUs). `tune` gets
// -mtune's architecture (the baseline's without one). Returns false if the options don't resolve (reported on stderr).
static bool predefines_target(const PredefinesOptions* options, X86Target* target, X86Architecture* tune)
{
    PredefinesFeatures features;
    int32_t cpu;
    const bool result = predefines_resolve(options, &features, &cpu);
    const char* march = options->march != 0 ? options->march : "x86-64";
    X86Target profile = { .architecture = predefines_x86_architecture(march) };
    if (profile.architecture != X86_ARCH_OTHER)
    {
        x86_target_parse(march, strlen(march), &profile);
    }
    *target = (X86Target){
        .features = profile.features & (BUSTER_X86_FEATURE(RDTSCP) | BUSTER_X86_FEATURE(CLFLUSH)),
        .architecture = profile.architecture,
        .native_znver5 = profile.native_znver5,
    };
    for (uint32_t f = 0; f < X86_FEATURE_COUNT; f += 1)
    {
        const char* name = predefines_x86_features[f];
        target->features |= (uint64_t)(name != 0 && predefines_has(features, predefines_feature(name))) << f;
    }
    *tune = options->mtune != 0 ? predefines_x86_architecture(options->mtune) : X86_ARCH_BASELINE;
    return result;
}

// The floating-point macros' inputs, stepped through the options in order as clang's driver does
// (RenderFloatingPointOptions): whether to define __FAST_MATH__, __FINITE_MATH_ONLY__ 1 and __NO_MATH_ERRNO__.
static void predefines_floating_point(const PredefinesOptions* options, bool* fast, bool* finite, bool* no_errno)
{
    bool honor_infinities = true;
    bool honor_nans = true;
    bool approx_func = false;
    // glibc's Linux: -fmath-errno is the default.
    bool math_errno = true;
    bool associative = false;
    bool reciprocal = false;
    bool signed_zeros = true;
    bool trapping = false;
    bool rounding = false;
    for (uint32_t i = 0; i < options->float_count; i += 1)
    {
        const PredefinesFloat event = (PredefinesFloat)options->floats[i];
        // -Ofast counts only when it is the last -O, and then it is -ffast-math.
        const bool fast_math = event == PREDEFINES_FLOAT_FAST || (event == PREDEFINES_FLOAT_OFAST && options->ofast);
        if (event >= PREDEFINES_FLOAT_MODEL_FAST && event <= PREDEFINES_FLOAT_MODEL_STRICT)
        {
            // -ffp-model= resets to -fno-fast-math's state first; with -Ofast, only aggressive goes on.
            honor_infinities = true;
            honor_nans = true;
            approx_func = false;
            math_errno = true;
            associative = false;
            reciprocal = false;
            signed_zeros = true;
            const bool ignored = options->ofast && event != PREDEFINES_FLOAT_MODEL_AGGRESSIVE;
            if (!ignored && (event == PREDEFINES_FLOAT_MODEL_FAST || event == PREDEFINES_FLOAT_MODEL_AGGRESSIVE))
            {
                const bool aggressive = event == PREDEFINES_FLOAT_MODEL_AGGRESSIVE;
                honor_infinities = !aggressive;
                honor_nans = !aggressive;
                math_errno = false;
                associative = true;
                reciprocal = true;
                approx_func = true;
                signed_zeros = false;
                trapping = false;
                rounding = false;
            }
            else if (!ignored && event == PREDEFINES_FLOAT_MODEL_STRICT)
            {
                trapping = true;
                rounding = true;
            }
        }
        else if (fast_math)
        {
            honor_infinities = false;
            honor_nans = false;
            math_errno = false;
            associative = true;
            reciprocal = true;
            approx_func = true;
            signed_zeros = false;
            trapping = false;
            rounding = false;
        }
        else
        {
            switch (event)
            {
                case PREDEFINES_FLOAT_HONOR_INFINITIES: honor_infinities = true; break;
                case PREDEFINES_FLOAT_NO_HONOR_INFINITIES: honor_infinities = false; break;
                case PREDEFINES_FLOAT_HONOR_NANS: honor_nans = true; break;
                case PREDEFINES_FLOAT_NO_HONOR_NANS: honor_nans = false; break;
                case PREDEFINES_FLOAT_APPROX_FUNC: approx_func = true; break;
                case PREDEFINES_FLOAT_NO_APPROX_FUNC: approx_func = false; break;
                case PREDEFINES_FLOAT_MATH_ERRNO: math_errno = true; break;
                case PREDEFINES_FLOAT_NO_MATH_ERRNO: math_errno = false; break;
                case PREDEFINES_FLOAT_ASSOCIATIVE: associative = true; break;
                case PREDEFINES_FLOAT_NO_ASSOCIATIVE: associative = false; break;
                case PREDEFINES_FLOAT_RECIPROCAL: reciprocal = true; break;
                case PREDEFINES_FLOAT_NO_RECIPROCAL: reciprocal = false; break;
                case PREDEFINES_FLOAT_SIGNED_ZEROS: signed_zeros = true; break;
                case PREDEFINES_FLOAT_NO_SIGNED_ZEROS: signed_zeros = false; break;
                case PREDEFINES_FLOAT_TRAPPING: trapping = true; break;
                case PREDEFINES_FLOAT_NO_TRAPPING: trapping = false; break;
                case PREDEFINES_FLOAT_ROUNDING: rounding = true; break;
                case PREDEFINES_FLOAT_NO_ROUNDING: rounding = false; break;
                case PREDEFINES_FLOAT_EXCEPTIONS_IGNORE: trapping = false; break;
                case PREDEFINES_FLOAT_EXCEPTIONS_STRICT: trapping = true; break;
                case PREDEFINES_FLOAT_FINITE:
                    honor_infinities = false;
                    honor_nans = false;
                    break;
                case PREDEFINES_FLOAT_NO_FINITE:
                    honor_infinities = true;
                    honor_nans = true;
                    break;
                case PREDEFINES_FLOAT_UNSAFE:
                    associative = true;
                    reciprocal = true;
                    signed_zeros = false;
                    approx_func = true;
                    trapping = false;
                    break;
                case PREDEFINES_FLOAT_NO_UNSAFE:
                    associative = false;
                    reciprocal = false;
                    signed_zeros = true;
                    approx_func = false;
                    break;
                case PREDEFINES_FLOAT_NO_FAST:
                    honor_infinities = true;
                    honor_nans = true;
                    math_errno = true;
                    associative = false;
                    reciprocal = false;
                    approx_func = false;
                    signed_zeros = true;
                    break;
                default: break;
            }
        }
    }
    *fast = !honor_infinities && !honor_nans && !math_errno && associative && approx_func && reciprocal && !signed_zeros && !trapping && !rounding;
    *finite = *fast || (!honor_infinities && !honor_nans);
    *no_errno = !math_errno;
}

// __FLT_EVAL_METHOD__'s value where predefines_text doesn't run (--predefines): -ffp-eval-method's, else 0 with SSE
// and 2 (x87) without.
static __attribute__((unused)) uint32_t predefines_eval_method(const PredefinesOptions* options)
{
    uint32_t result = 0;
    if (options->fp_eval_method != BUSTER_PREDEFINES_EVAL_DEFAULT)
    {
        result = (uint32_t)(options->fp_eval_method - BUSTER_PREDEFINES_EVAL_SOURCE);
    }
    else
    {
        PredefinesFeatures features;
        int32_t cpu;
        predefines_resolve(options, &features, &cpu);
        result = predefines_has(features, predefines_feature("sse")) ? 0 : 2;
    }
    return result;
}

static uint32_t predefines_append(char* buffer, uint32_t length, uint32_t capacity, const char* text)
{
    const uint32_t size = (uint32_t)strlen(text);
    const uint32_t result = length + size <= capacity ? length + size : capacity + 1;
    if (result <= capacity)
    {
        memcpy(buffer + length, text, size);
    }
    return result;
}

// The most predefines_text writes.
#define BUSTER_PREDEFINES_TEXT_BYTES (16u << 10)

// long double's macros as -mlong-double-80 (x87's format), 64 (double's) and 128 (IEEE quad) give them.
static const char* const predefines_long_double[3] = {
    "#define __LDBL_DECIMAL_DIG__ 21\n#define __LDBL_DENORM_MIN__ 3.64519953188247460253e-4951L\n#define __LDBL_DIG__ 18\n"
    "#define __LDBL_EPSILON__ 1.08420217248550443401e-19L\n#define __LDBL_HAS_DENORM__ 1\n#define __LDBL_HAS_INFINITY__ 1\n"
    "#define __LDBL_HAS_QUIET_NAN__ 1\n#define __LDBL_MANT_DIG__ 64\n#define __LDBL_MAX_10_EXP__ 4932\n"
    "#define __LDBL_MAX__ 1.18973149535723176502e+4932L\n#define __LDBL_MAX_EXP__ 16384\n#define __LDBL_MIN_10_EXP__ (-4931)\n"
    "#define __LDBL_MIN__ 3.36210314311209350626e-4932L\n#define __LDBL_MIN_EXP__ (-16381)\n"
    "#define __LDBL_NORM_MAX__ 1.18973149535723176502e+4932L\n#define __SIZEOF_LONG_DOUBLE__ 16\n",
    "#define __LDBL_DECIMAL_DIG__ 17\n#define __LDBL_DENORM_MIN__ 4.9406564584124654e-324L\n#define __LDBL_DIG__ 15\n"
    "#define __LDBL_EPSILON__ 2.2204460492503131e-16L\n#define __LDBL_HAS_DENORM__ 1\n#define __LDBL_HAS_INFINITY__ 1\n"
    "#define __LDBL_HAS_QUIET_NAN__ 1\n#define __LDBL_MANT_DIG__ 53\n#define __LDBL_MAX_10_EXP__ 308\n"
    "#define __LDBL_MAX__ 1.7976931348623157e+308L\n#define __LDBL_MAX_EXP__ 1024\n#define __LDBL_MIN_10_EXP__ (-307)\n"
    "#define __LDBL_MIN__ 2.2250738585072014e-308L\n#define __LDBL_MIN_EXP__ (-1021)\n#define __LDBL_NORM_MAX__ 1.7976931348623157e+308L\n"
    "#define __SIZEOF_LONG_DOUBLE__ 8\n",
    "#define __LDBL_DECIMAL_DIG__ 36\n#define __LDBL_DENORM_MIN__ 6.47517511943802511092443895822764655e-4966L\n#define __LDBL_DIG__ 33\n"
    "#define __LDBL_EPSILON__ 1.92592994438723585305597794258492732e-34L\n#define __LDBL_HAS_DENORM__ 1\n#define __LDBL_HAS_INFINITY__ 1\n"
    "#define __LDBL_HAS_QUIET_NAN__ 1\n#define __LDBL_MANT_DIG__ 113\n#define __LDBL_MAX_10_EXP__ 4932\n"
    "#define __LDBL_MAX__ 1.18973149535723176508575932662800702e+4932L\n#define __LDBL_MAX_EXP__ 16384\n#define __LDBL_MIN_10_EXP__ (-4931)\n"
    "#define __LDBL_MIN__ 3.36210314311209350626267781732175260e-4932L\n#define __LDBL_MIN_EXP__ (-16381)\n"
    "#define __LDBL_NORM_MAX__ 1.18973149535723176508575932662800702e+4932L\n#define __SIZEOF_LONG_DOUBLE__ 16\n",
};

// -fdefine-target-os-macros's, for Linux.
static const char predefines_target_os[] =
    "#define TARGET_IPHONE_SIMULATOR 0\n#define TARGET_OS_DRIVERKIT 0\n#define TARGET_OS_EMBEDDED 0\n#define TARGET_OS_FIRMWARE 0\n"
    "#define TARGET_OS_IOS 0\n#define TARGET_OS_IPHONE 0\n#define TARGET_OS_LINUX 1\n#define TARGET_OS_MAC 0\n#define TARGET_OS_MACCATALYST 0\n"
    "#define TARGET_OS_NANO 0\n#define TARGET_OS_OSX 0\n#define TARGET_OS_SIMULATOR 0\n#define TARGET_OS_TV 0\n#define TARGET_OS_UEFI 0\n"
    "#define TARGET_OS_UIKITFORMAC 0\n#define TARGET_OS_UNIX 0\n#define TARGET_OS_VISION 0\n#define TARGET_OS_WATCH 0\n#define TARGET_OS_WIN32 0\n"
    "#define TARGET_OS_WINDOWS 0\n";

// Whether the compiler emits call frame information, as clang's driver decides it: with unwind tables (-funwind-tables,
// or asynchronous ones, the default unless freestanding, -f[no-]asynchronous-unwind-tables's last saying otherwise),
// exceptions or debug information. __GCC_HAVE_DWARF2_CFI_ASM says it does (but under -fno-dwarf2-cfi-asm, which
// changes only the form clang's assembly gives it), and the C driver writes its functions' unwind tables then (#395).
static __attribute__((unused)) bool predefines_unwind_tables(const PredefinesOptions* options, bool freestanding)
{
    const bool asynchronous = options->asynchronous_unwind_tables != 0 ? options->asynchronous_unwind_tables == BUSTER_PREDEFINES_ON : !freestanding;
    const bool unwind = options->unwind_tables == BUSTER_PREDEFINES_ON;
    return options->exceptions || unwind || asynchronous || options->debug || options->debug_modules;
}

// The macros the target and the options decide, as #define lines, into `buffer` (`capacity` bytes, at least
// BUSTER_PREDEFINES_TEXT_BYTES): all of them but __STDC_HOSTED__, which pp_predefines writes with the language mode's.
// `language` decides the GNU inline semantics and C23's char8_t, and `freestanding` the unwind tables' default.
// *flt_eval_method gets the builtin __FLT_EVAL_METHOD__'s value: -ffp-eval-method's, else 0 with SSE and 2 (x87)
// without. Returns the length, or BUSTER_PREDEFINES_TEXT_BYTES + 1 after an error, reported on stderr.
static __attribute__((unused)) uint32_t predefines_text(const PredefinesOptions* options, CLanguage language, bool freestanding, char* buffer, uint32_t capacity,
                                                        uint32_t* flt_eval_method)
{
    PredefinesFeatures features;
    int32_t cpu;
    const bool resolved = predefines_resolve(options, &features, &cpu);
    const bool sse = predefines_has(features, predefines_feature("sse"));
    *flt_eval_method = options->fp_eval_method != BUSTER_PREDEFINES_EVAL_DEFAULT ? (uint32_t)(options->fp_eval_method - BUSTER_PREDEFINES_EVAL_SOURCE) : sse ? 0 : 2;
    uint32_t length = 0;
    char line[256];
    if (resolved && predefines_cpus[cpu].macro != 0)
    {
        const char* macro = predefines_cpus[cpu].macro;
        snprintf(line, sizeof(line), "#define __%s 1\n#define __%s__ 1\n#define __tune_%s__ 1\n", macro, macro, macro);
        length = predefines_append(buffer, length, capacity, line);
    }
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(predefines_features) && resolved; f += 1)
    {
        if (predefines_has(features, f))
        {
            length = predefines_append(buffer, length, capacity, predefines_features[f].macros);
        }
    }
    // The two macros that need several features.
    static const char* const apx[] = { "egpr", "push2pop2", "ppx", "ndd", "ccmp", "nf", "zu", "jmpabs" };
    bool apx_f = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(apx); i += 1)
    {
        apx_f &= predefines_has(features, predefines_feature(apx[i]));
    }
    length = resolved && apx_f ? predefines_append(buffer, length, capacity, "#define __APX_F__ 1\n") : length;
    const bool gpr32 = predefines_has(features, predefines_feature("egpr")) && predefines_has(features, predefines_feature("inline-asm-use-gpr32"));
    length = resolved && gpr32 ? predefines_append(buffer, length, capacity, "#define __APX_INLINE_ASM_USE_GPR32__ 1\n") : length;
    // The code model.
    static const char* const code_models[4] = { "small", "kernel", "medium", "large" };
    snprintf(line, sizeof(line), "#define __code_model_%s__ 1\n", code_models[options->code_model]);
    length = predefines_append(buffer, length, capacity, line);
    // PIC and PIE: level 2 PIE by default (clang's on Linux), and the last of the eight options.
    const uint8_t pic = options->pic;
    const uint32_t pic_level = pic == BUSTER_PREDEFINES_PIC_OFF ? 0 : pic == BUSTER_PREDEFINES_PIC_SMALL || pic == BUSTER_PREDEFINES_PIE_SMALL ? 1 : 2;
    const bool pie = pic == 0 || pic == BUSTER_PREDEFINES_PIE_BIG || pic == BUSTER_PREDEFINES_PIE_SMALL;
    if (pic_level != 0)
    {
        snprintf(line, sizeof(line), "#define __pic__ %u\n#define __PIC__ %u\n", pic_level, pic_level);
        length = predefines_append(buffer, length, capacity, line);
        if (pie)
        {
            snprintf(line, sizeof(line), "#define __pie__ %u\n#define __PIE__ %u\n", pic_level, pic_level);
            length = predefines_append(buffer, length, capacity, line);
        }
    }
    // The stack protector.
    const uint8_t requested = options->stack_protector;
    const uint8_t ssp = requested == 0 ? BUSTER_PREDEFINES_SSP_DEFAULT
                        : requested == BUSTER_PREDEFINES_SSP_ON && BUSTER_PREDEFINES_SSP_DEFAULT > BUSTER_PREDEFINES_SSP_ON ? BUSTER_PREDEFINES_SSP_DEFAULT
                                                                                                                              : requested;
    length = ssp == BUSTER_PREDEFINES_SSP_ON       ? predefines_append(buffer, length, capacity, "#define __SSP__ 1\n")
             : ssp == BUSTER_PREDEFINES_SSP_STRONG ? predefines_append(buffer, length, capacity, "#define __SSP_STRONG__ 2\n")
             : ssp == BUSTER_PREDEFINES_SSP_ALL    ? predefines_append(buffer, length, capacity, "#define __SSP_ALL__ 3\n")
                                                   : length;
    // Floating point.
    bool fast;
    bool finite;
    bool no_errno;
    predefines_floating_point(options, &fast, &finite, &no_errno);
    length = fast ? predefines_append(buffer, length, capacity, "#define __FAST_MATH__ 1\n") : length;
    length = predefines_append(buffer, length, capacity, finite ? "#define __FINITE_MATH_ONLY__ 1\n" : "#define __FINITE_MATH_ONLY__ 0\n");
    length = no_errno ? predefines_append(buffer, length, capacity, "#define __NO_MATH_ERRNO__ 1\n") : length;
    // Optimization, and inlining: -O0 only inlines always_inline functions, as do -fno-inline and -fno-inline-functions.
    length = options->optimization != 0 ? predefines_append(buffer, length, capacity, "#define __OPTIMIZE__ 1\n") : length;
    length = options->optimize_size != 0 ? predefines_append(buffer, length, capacity, "#define __OPTIMIZE_SIZE__ 1\n") : length;
    const bool only_always = options->optimization == 0 || options->inline_option == BUSTER_PREDEFINES_OFF ||
                             (options->inline_option == 0 && options->inline_functions == BUSTER_PREDEFINES_NO_INLINE_FUNCTIONS);
    length = only_always ? predefines_append(buffer, length, capacity, "#define __NO_INLINE__ 1\n") : length;
    // The GNU version (4.2.1 unless -fgnuc-version says otherwise; 0 turns these off), the inline semantics and GCC's
    // atomics.
    const uint32_t major = options->gnuc_given ? options->gnuc_major : 4;
    const uint32_t minor = options->gnuc_given ? options->gnuc_minor : 2;
    const uint32_t patch = options->gnuc_given ? options->gnuc_patch : 1;
    if (major != 0 || minor != 0 || patch != 0)
    {
        snprintf(line, sizeof(line), "#define __GNUC__ %u\n#define __GNUC_MINOR__ %u\n#define __GNUC_PATCHLEVEL__ %u\n#define __GXX_ABI_VERSION 1002\n", major, minor, patch);
        length = predefines_append(buffer, length, capacity, line);
        const bool gnu_inline = language.standard < 1999 || options->gnu89_inline == BUSTER_PREDEFINES_ON;
        length = predefines_append(buffer, length, capacity, gnu_inline ? "#define __GNUC_GNU_INLINE__ 1\n" : "#define __GNUC_STDC_INLINE__ 1\n");
        length = predefines_append(buffer, length, capacity,
                                   "#define __GCC_ATOMIC_TEST_AND_SET_TRUEVAL 1\n#define __GCC_ATOMIC_BOOL_LOCK_FREE 2\n#define __GCC_ATOMIC_CHAR_LOCK_FREE 2\n"
                                   "#define __GCC_ATOMIC_CHAR16_T_LOCK_FREE 2\n#define __GCC_ATOMIC_CHAR32_T_LOCK_FREE 2\n#define __GCC_ATOMIC_WCHAR_T_LOCK_FREE 2\n"
                                   "#define __GCC_ATOMIC_SHORT_LOCK_FREE 2\n#define __GCC_ATOMIC_INT_LOCK_FREE 2\n#define __GCC_ATOMIC_LONG_LOCK_FREE 2\n"
                                   "#define __GCC_ATOMIC_LLONG_LOCK_FREE 2\n#define __GCC_ATOMIC_POINTER_LOCK_FREE 2\n");
        length = language.standard >= 2023 ? predefines_append(buffer, length, capacity, "#define __GCC_ATOMIC_CHAR8_T_LOCK_FREE 2\n") : length;
    }
    // char and wchar_t.
    length = options->char_unsigned ? predefines_append(buffer, length, capacity, "#define __CHAR_UNSIGNED__ 1\n") : length;
    length = predefines_append(buffer, length, capacity,
                               options->short_wchar ? "#define __WCHAR_MAX__ 65535\n#define __WCHAR_MIN__ 0\n#define __WCHAR_TYPE__ unsigned short\n#define __WCHAR_UNSIGNED__ 1\n"
                                                      "#define __WCHAR_WIDTH__ 16\n#define __SIZEOF_WCHAR_T__ 2\n#define __clang_wide_literal_encoding__ \"UTF-16\"\n"
                                                    : "#define __WCHAR_MAX__ 2147483647\n#define __WCHAR_MIN__ (-__WCHAR_MAX__ - 1)\n#define __WCHAR_TYPE__ int\n"
                                                      "#define __WCHAR_WIDTH__ 32\n#define __SIZEOF_WCHAR_T__ 4\n#define __clang_wide_literal_encoding__ \"UTF-32\"\n");
    length = predefines_append(buffer, length, capacity, predefines_long_double[options->long_double]);
    length = !options->no_constant_cfstrings && !options->no_constant_cfstrings_m ? predefines_append(buffer, length, capacity, "#define __CONSTANT_CFSTRINGS__ 1\n") : length;
    // The CFI directives: by default when there are unwind tables (asynchronous ones unless freestanding),
    // exceptions or debug information.
    const bool cfi_default = predefines_unwind_tables(options, freestanding);
    const bool cfi = options->dwarf2_cfi_asm != 0 ? options->dwarf2_cfi_asm == BUSTER_PREDEFINES_ON : cfi_default;
    length = cfi ? predefines_append(buffer, length, capacity, "#define __GCC_HAVE_DWARF2_CFI_ASM 1\n") : length;
    // Exceptions.
    length = options->exceptions ? predefines_append(buffer, length, capacity, "#define __EXCEPTIONS 1\n") : length;
    length = options->exception_model == BUSTER_PREDEFINES_EXCEPTIONS_SJLJ ? predefines_append(buffer, length, capacity, "#define __USING_SJLJ_EXCEPTIONS__ 1\n")
             : options->exception_model == BUSTER_PREDEFINES_EXCEPTIONS_SEH ? predefines_append(buffer, length, capacity, "#define __SEH__ 1\n")
                                                                            : length;
    // The rest, one option each.
    length = options->pthread ? predefines_append(buffer, length, capacity, "#define _REENTRANT 1\n") : length;
    if (options->cf_protection != 0)
    {
        snprintf(line, sizeof(line), "#define __CET__ %u\n", options->cf_protection);
        length = predefines_append(buffer, length, capacity, line);
    }
    // The sanitizers' macros: a kernel's sanitizer and its user space one share theirs.
    const uint64_t sanitizers = options->sanitizers;
    const uint64_t address = BUSTER_PREDEFINES_SANITIZER(ADDRESS) | BUSTER_PREDEFINES_SANITIZER(KERNEL_ADDRESS);
    const uint64_t hwaddress = BUSTER_PREDEFINES_SANITIZER(HWADDRESS) | BUSTER_PREDEFINES_SANITIZER(KERNEL_HWADDRESS);
    length = (sanitizers & address) != 0 ? predefines_append(buffer, length, capacity, "#define __SANITIZE_ADDRESS__ 1\n") : length;
    length = (sanitizers & hwaddress) != 0 ? predefines_append(buffer, length, capacity, "#define __SANITIZE_HWADDRESS__ 1\n") : length;
    length = (sanitizers & BUSTER_PREDEFINES_SANITIZER(THREAD)) != 0 ? predefines_append(buffer, length, capacity, "#define __SANITIZE_THREAD__ 1\n") : length;
    length = (sanitizers & BUSTER_PREDEFINES_SANITIZER(ALLOC_TOKEN)) != 0 ? predefines_append(buffer, length, capacity, "#define __SANITIZE_ALLOC_TOKEN__ 1\n") : length;
    length = options->blocks ? predefines_append(buffer, length, capacity, "#define __block __attribute__((__blocks__(byref)))\n#define __BLOCKS__ 1\n") : length;
    if (options->openmp)
    {
        // The _OPENMP of -fopenmp-version's version, or of 5.1 (clang's default, also for versions it doesn't know).
        const uint32_t v = options->openmp_version;
        const char* date = v == 31 ? "201107" : v == 40 ? "201307" : v == 45 ? "201511" : v == 50 ? "201811" : v == 52 ? "202111" : v == 60 ? "202411" : "202011";
        snprintf(line, sizeof(line), "#define _OPENMP %s\n", date);
        length = predefines_append(buffer, length, capacity, line);
    }
    length = options->openacc ? predefines_append(buffer, length, capacity, "#define _OPENACC 202506\n") : length;
    length = options->target_os_macros ? predefines_append(buffer, length, capacity, predefines_target_os) : length;
    length = options->pascal_strings ? predefines_append(buffer, length, capacity, "#define __PASCAL_STRINGS__ 1\n") : length;
    bool profile = false;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(options->profile_generate); i += 1)
    {
        profile |= options->profile_generate[i] == BUSTER_PREDEFINES_ON;
    }
    length = profile ? predefines_append(buffer, length, capacity, "#define __LLVM_INSTR_PROFILE_GENERATE 1\n") : length;
    if (length > capacity)
    {
        fprintf(stderr, "error: the predefined macros don't fit\n");
    }
    return resolved && length <= capacity ? length : BUSTER_PREDEFINES_TEXT_BYTES + 1;
}
