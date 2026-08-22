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
// like GCC's and clang's, bare returns stay in XMM0) and the Win64 narrow
// fix below, the gated matrix passes cross-compiler against clang in both
// directions, so pairings no longer need it; it remains available for
// targets whose vector ABI is still being brought up.
//
// Win64 needed its own answers for everything narrower than one vector
// register, because MSVC has no generic vector extension and clang's choices
// are therefore the de-facto ABI: a single-lane vector travels like its
// scalar element (integer lanes in the positional GPR, float lanes in the
// positional XMM register), and a multi-lane vector under eight bytes is an
// indirect argument with a direct XMM0 result. __int128 answers the same way
// -- indirect argument, XMM0 result. Buster passed all of these by reference
// in both directions before, which agreed with itself and with nothing else.

#ifndef C_ABI_FULL
// Non-power-of-two lane counts are rejected as function parameter and return
// types.
#define ZIG_NO_NON_POW2_VECTORS
// Vectors wider than 64 bytes cross the boundary on Win64 x86-64 only:
// that convention legalizes them the way clang and MSVC do (one indirect
// reference per register-sized piece for an argument, up to four direct
// registers or the hidden pointer for a result), while every other
// convention still rejects the signature types. Target-keyed like the
// F128/INT128_SHIFTS gates below, so both sides agree whichever compiler
// builds them.
#if !(defined(_WIN32) && defined(__x86_64__))
#define ZIG_NO_WIDE_VECTORS
#endif
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
