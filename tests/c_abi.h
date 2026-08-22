// Shared prelude for the C ABI interop suite ported from Zig's test/c_abi
// (ziglang/zig @ 3c46da14db, test/c_abi/{main.zig,cfuncs.c}). The suite is a
// pair of translation units that pass every ABI-relevant shape across the
// boundary in both directions: c_abi_cfuncs.c keeps the upstream C side nearly
// verbatim, c_abi_main.c and c_abi_main_generated.c are the C translation of
// main.zig (the Zig side), so the upstream `zig_` symbol prefix survives to
// keep the pair diffable against upstream. The driver test compiles all three
// with the compiler under test and runs the result; compiling the two sides
// with different compilers (one of them clang) turns the same fixtures into a
// cross-compiler conformance harness.
//
// This header replaces upstream's hosted includes (<inttypes.h>, <stdbool.h>,
// <stdalign.h>, <complex.h>, ...) because the fixtures must also cross-compile
// for targets whose system headers are absent; everything the suite needs is
// declared freestanding below.
#ifndef C_ABI_H
#define C_ABI_H

// Capability gates. Upstream already keys optional families off ZIG_NO_*
// macros per target; the port extends the same scheme with the shapes the
// compiler under test cannot build yet. Both translation units include this
// header, so the two sides always agree on which families exist — that must
// hold even when the sides are compiled by different compilers, which is why
// the gates are unconditional rather than keyed off __BUSTER__. Compile with
// -DC_ABI_FULL (clang on both sides) to run the complete upstream suite.
// Bool vectors are excluded from the port itself, not just gated for the
// compiler under test: they need clang's ext_vector_type on both sides, and
// upstream only exercises them on a handful of targets anyway.
#define ZIG_NO_BOOL_VECTORS

// ZIG_NO_VECTORS strips the whole integer/float vector matrix. Since the
// System V single-lane-double fix (<1 x double> arguments are MEMORY class
// like GCC's and clang's, bare returns stay in XMM0) the gated matrix
// passes cross-compiler against clang in both directions, so pairings no
// longer need it; it remains available for targets whose vector ABI is
// still being brought up.

#ifndef C_ABI_FULL
// Non-power-of-two lane counts are rejected as function parameter and return
// types.
#define ZIG_NO_NON_POW2_VECTORS
// Vectors wider than 64 bytes are rejected (the open wide-argument
// legalization work).
#define ZIG_NO_WIDE_VECTORS
// long double supports transport only; comparisons count as the unsupported
// wide floating-point arithmetic.
#define ZIG_NO_LONG_DOUBLE
// __fp16 is not parsed (upstream's ZIG_NO_RAW_F16 only skips bare f16 on
// targets where the struct form still works).
#define ZIG_NO_F16
#define ZIG_NO_F80
#define ZIG_NO_F128
// _Complex is not parsed.
#define ZIG_NO_COMPLEX
// Calling-convention attributes (ms_abi, sysv_abi, preserve_none) are
// silently skipped, so testing them would test nothing and break the
// cross-compiler pairing.
#define ZIG_NO_CC_ATTRIBUTES
#endif

// Target-derived gates that upstream cfuncs.c sets in its own prelude;
// mirrored here so both translation units always agree on the family set.
#if defined(__APPLE__) || defined(_MSC_VER)
#ifndef ZIG_NO_F128
#define ZIG_NO_F128
#endif
#endif

// The compiler under test still lowers a 128-bit shift on AArch64 as a
// single 64-bit LSL (whose amount wraps modulo 64), so any shift that moves
// bits into or out of the high half corrupts — measured 2026-08-22 under
// qemu, both pipelines. The big packed struct family constructs its value
// with a runtime `<< 64` on both sides, so it sits behind this gate on
// AArch64 until the pair shift lands; 128-bit passing, returning, and
// comparing are unaffected and stay tested.
#if !defined(C_ABI_FULL) && defined(__aarch64__)
#define ZIG_NO_INT128_SHIFTS
#endif

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef long long intptr_t;
typedef unsigned long long uintptr_t;
typedef __SIZE_TYPE__ size_t;

#define UINT64_C(value) value##ULL

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
typedef _Bool bool;
#define true 1
#define false 0
#define alignas _Alignas
#endif

#ifndef ZIG_NO_COMPLEX
#include <complex.h>
#endif

// The name of the test currently running, for a debugger or the panic report;
// assigned by every test function on entry. zig_panic (the upstream name for
// the shared failure sink, defined in c_abi_main.c) reports it and faults so
// the harness sees an abnormal exit.
extern const char* c_abi_current_test;
void zig_panic(void);

// The generated half of the Zig side: vector and struct-shape families, run
// in upstream test order.
void c_abi_run_generated_tests(void);

#endif
