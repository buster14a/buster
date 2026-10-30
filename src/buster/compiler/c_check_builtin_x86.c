// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's bounded x86 builtin constraint phase. Include after c_check_actions.c. The ordinary call owner has already
// checked the exact decoded prototype, made all parameter conversions and built typed_call. This phase preserves
// that result/effects/conversions and adds only generated-prototype ICE and handwritten immediate constraints.
// name_in_scope attests BOTH exact recognition in the frozen 1492-name x86 catalogue (or among its generic names that
// clang checks by their prototype alone, builtin_intrinsic_prototype_names, which no row here names) and successful ordinary
// prototype checking. A prefix, guessed suffix/prototype, or unresolved CustomTypeChecking T/t record is no proof.
// Exact name/name_bytes are immutable metadata spelling, not necessarily NUL-terminated. No input is evaluated.
//
// required_ice_arguments is the decoded prototype I-marker bitmap. ice_arguments is separately certified by the
// producer's actual getIntegerConstantExpr-equivalent source analysis. CheckValue's ICE flag alone is insufficient;
// source contexts/extensions can also prove an ICE not represented by that local flag. A witnessed operand must
// nevertheless have an integer type, exact normalized INTEGER bits, no fault and no effects. Contradictory witness
// metadata is INVALID; a valid integer format/payload outside this representation is explicitly UNSUPPORTED.
// Arguments are already converted, so each integer's signedness/width is its formal parameter representation.
// Exact canonical widths through 128 bits are supported without signed host arithmetic or narrowing to 64 bits.
//
// All prototype ICEs are checked first in argument order, then the hard RC/SAE or scale set, then the direct range. The two
// rndscale{pd,ps}_mask names keep BOTH their argument4 SAE rule and argument1 range. A source constant-evaluated
// context skips only direct ranges, never prototype ICEs or RC/SAE. Runtime diagnostic suppression and attached-call
// reachability are independent producer facts. Unused results, pure arguments or target features are no deadness proof.
// A range violation preserves semantic success and produces an event: default-error Warning class, group
// argument-outside-range. READY goes to the ordinary warning policy, DEFERRED waits for source CFG reachability,
// SUPPRESSED retains the audited fact without a diagnostic. No event is a hard action error. Missing CFG information
// must remain DEFERRED or later emit conservatively; it must not silently suppress the warning.
//
// The immutable 353 rows below are original factual metadata audited against the exact Clang 23.1.1 rules; no LLVM
// implementation was copied. They cover 351 exact names: 87 RC sets, 61 SAE sets, 6 scale sets and 199 deferred ranges
// (#126 added the build's 17 ranges and 6 scales, whose names joined the own-main scope, #619 ucmpd128_mask's range,
// #148 the 8 ranges of the SSE shuffles, shifts, extracts and inserts that stb_image, dr_libs and minimp3 call,
// #799 five additional 128/256-bit shuffle, blend and align names, and #148 the rest of clang's x86 builtins it
// implements, the supplementary scope's: c_builtin_supplementary_scope.inc). Names in the
// attested catalogue without a row have no additional SemaX86 rule. Reference source/case lines accompany each row:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaX86.cpp
// Generic ICE/range behavior: SemaChecking.cpp:2971-2989,6796-6843; runtime context/reachability: SemaExpr.cpp and
// AnalysisBasedWarnings.cpp; Warning+DefaultError: DiagnosticSemaKinds.td's warn_argument_invalid_range.
//
// This phase has no target-feature evaluator, vector/prototype conversion, constant folding or runtime emitter.
// It grants no catalog SOURCE/backend capability. Feature strings, unsupported custom type relations, source
// constexpr result evaluation and lowering remain separate obligations of their owners. Argument bound64 is explicit;
// required/witness bits outside argument_count are INVALID. Zero arguments permit NULL; positive counts require a
// readable argument slice. Action/result/name/argument storage must be disjoint and naturally aligned, with no tail
// padding. Batches preserve order, allocate no memory and permit null slices only for zero count.
#ifndef BUSTER_C_CHECK_BUILTIN_X86_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_X86_INCLUDED

#define BUSTER_CHECK_BUILTIN_X86_ARGUMENTS 64u
#define BUSTER_CHECK_BUILTIN_X86_EVENTS 2u
#define BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_SCOPE 0x01u
#define BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_INTEGER_WIDTH 0x02u
#define BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_INTEGER_NORMALIZATION 0x04u
#define BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_ARGUMENT_BOUND 0x08u

typedef enum CheckBuiltinX86Status
{
    CHECK_BUILTIN_X86_OK,
    CHECK_BUILTIN_X86_ERROR,
    CHECK_BUILTIN_X86_UNSUPPORTED,
    CHECK_BUILTIN_X86_INVALID,
} CheckBuiltinX86Status;

typedef enum CheckBuiltinX86Reachability
{
    CHECK_BUILTIN_X86_UNKNOWN,
    CHECK_BUILTIN_X86_REACHABLE,
    CHECK_BUILTIN_X86_UNREACHABLE,
} CheckBuiltinX86Reachability;

typedef enum CheckBuiltinX86EventDisposition
{
    CHECK_BUILTIN_X86_DEFERRED,
    CHECK_BUILTIN_X86_READY,
    CHECK_BUILTIN_X86_SUPPRESSED,
} CheckBuiltinX86EventDisposition;

typedef enum CheckBuiltinX86Constraint
{
    CHECK_BUILTIN_X86_CONSTRAINT_NONE,
    CHECK_BUILTIN_X86_CONSTRAINT_ICE,
    CHECK_BUILTIN_X86_CONSTRAINT_RC,
    CHECK_BUILTIN_X86_CONSTRAINT_SAE,
    CHECK_BUILTIN_X86_CONSTRAINT_RANGE,
    // A gather's or scatter's scale: a hard set, 1, 2, 4 or 8 (SemaX86's CheckBuiltinGatherScatterScale).
    CHECK_BUILTIN_X86_CONSTRAINT_SCALE,
} CheckBuiltinX86Constraint;

typedef struct CheckBuiltinX86Action CheckBuiltinX86Action;
struct CheckBuiltinX86Action
{
    const char* name;
    uint32_t name_bytes;
    const CheckValue* arguments;
    uint32_t argument_count;
    uint64_t required_ice_arguments;
    uint64_t ice_arguments;
    uint32_t node;
    CheckActionResult typed_call;
    CheckBuiltinX86Reachability reachability;
    bool name_in_scope;
    bool constant_evaluated_context;
    bool runtime_diagnostic_suppressed;
};


typedef struct CheckBuiltinX86RangeEvent CheckBuiltinX86RangeEvent;
struct CheckBuiltinX86RangeEvent
{
    uint32_t node;
    uint32_t argument;
    uint32_t low;
    uint32_t high;
    uint32_t source_line;
    CheckValue value;
    CheckBuiltinX86EventDisposition disposition;
    bool default_error;
};

typedef struct CheckBuiltinX86Result CheckBuiltinX86Result;
struct CheckBuiltinX86Result
{
    CheckActionResult action;
    CheckBuiltinX86Status status;
    CheckBuiltinX86Constraint constraint;
    // Every ICE witness consulted by a prototype/custom constraint, including a failing one. The preliminary
    // metadata-consistency check does not add bits here.
    uint64_t checked_ice_arguments;
    uint32_t matched_rules;
    uint32_t unsupported;
    uint32_t range_event_count;
    CheckBuiltinX86RangeEvent range_events[BUSTER_CHECK_BUILTIN_X86_EVENTS];
};

typedef struct CheckBuiltinX86Rule CheckBuiltinX86Rule;
struct CheckBuiltinX86Rule
{
    const char* name;
    uint32_t name_bytes;
    CheckBuiltinX86Constraint kind;
    uint32_t argument;
    uint32_t low;
    uint32_t high;
    uint32_t accepted;
    uint32_t source_line;
};

static const CheckBuiltinX86Rule check_builtin_x86_rules[] =
{
    { "__builtin_ia32_alignd128", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 845 },
    { "__builtin_ia32_alignd256", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 846 },
    { "__builtin_ia32_alignd512", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 844 },
    { "__builtin_ia32_alignq128", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 847 },
    { "__builtin_ia32_alignq256", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 848 },
    { "__builtin_ia32_alignq512", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 843 },
    { "__builtin_ia32_blendpd", 22, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 615 },
    { "__builtin_ia32_blendpd256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 700 },
    { "__builtin_ia32_blendps", 22, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 699 },
    { "__builtin_ia32_blendps256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 838 },
    { "__builtin_ia32_cmpb128_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 640 },
    { "__builtin_ia32_cmpb256_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 644 },
    { "__builtin_ia32_cmpb512_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 648 },
    { "__builtin_ia32_cmpd128_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 642 },
    { "__builtin_ia32_cmpd256_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 646 },
    { "__builtin_ia32_cmpd512_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 650 },
    { "__builtin_ia32_cmpq128_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 643 },
    { "__builtin_ia32_cmpq256_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 647 },
    { "__builtin_ia32_cmpq512_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 651 },
    { "__builtin_ia32_cmpw128_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 641 },
    { "__builtin_ia32_cmpw256_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 645 },
    { "__builtin_ia32_cmpw512_mask", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 649 },
    { "__builtin_ia32_cvtpd2dq512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 202 },
    { "__builtin_ia32_cvtpd2ps512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 201 },
    { "__builtin_ia32_cvtpd2qq512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 203 },
    { "__builtin_ia32_cvtpd2udq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 204 },
    { "__builtin_ia32_cvtpd2uqq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 205 },
    { "__builtin_ia32_cvtps2dq512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 206 },
    { "__builtin_ia32_cvtps2qq512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 207 },
    { "__builtin_ia32_cvtps2udq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 208 },
    { "__builtin_ia32_cvtps2uqq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 209 },
    { "__builtin_ia32_cvtqq2ps512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 211 },
    { "__builtin_ia32_cvtsd2ss_round_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 251 },
    { "__builtin_ia32_cvtss2sd_round_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 104 },
    { "__builtin_ia32_cvttpd2dq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 70 },
    { "__builtin_ia32_cvttpd2qq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 71 },
    { "__builtin_ia32_cvttpd2udq512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 72 },
    { "__builtin_ia32_cvttpd2uqq512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 73 },
    { "__builtin_ia32_cvttps2dq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 74 },
    { "__builtin_ia32_cvttps2qq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 75 },
    { "__builtin_ia32_cvttps2udq512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 76 },
    { "__builtin_ia32_cvttps2uqq512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 77 },
    { "__builtin_ia32_cvtuqq2ps512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 213 },
    { "__builtin_ia32_dbpsadbw128", 26, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 859 },
    { "__builtin_ia32_dbpsadbw256", 26, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 860 },
    { "__builtin_ia32_dbpsadbw512", 26, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 861 },
    { "__builtin_ia32_extract128i256", 29, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 564 },
    { "__builtin_ia32_extractf32x4_256_mask", 36, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 571 },
    { "__builtin_ia32_extractf32x4_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 599 },
    { "__builtin_ia32_extractf32x8_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 567 },
    { "__builtin_ia32_extractf64x2_256_mask", 36, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 569 },
    { "__builtin_ia32_extractf64x2_512_mask", 36, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 601 },
    { "__builtin_ia32_extractf64x4_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 565 },
    { "__builtin_ia32_extracti32x4_256_mask", 36, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 572 },
    { "__builtin_ia32_extracti32x4_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 600 },
    { "__builtin_ia32_extracti32x8_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 568 },
    { "__builtin_ia32_extracti64x2_256_mask", 36, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 570 },
    { "__builtin_ia32_extracti64x2_512_mask", 36, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 602 },
    { "__builtin_ia32_extracti64x4_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 566 },
    { "__builtin_ia32_gatherdiv8di", 27, CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 0x0116, 391 },
    { "__builtin_ia32_gathersiv16si", 28, CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 0x0116, 390 },
    { "__builtin_ia32_gathersiv8di", 27, CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 0x0116, 389 },
    { "__builtin_ia32_getexppd512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 84 },
    { "__builtin_ia32_getexpph512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 86 },
    { "__builtin_ia32_getexpps512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 85 },
    { "__builtin_ia32_getexpsd128_round_mask", 37, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 105 },
    { "__builtin_ia32_getexpsh128_round_mask", 37, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 107 },
    { "__builtin_ia32_getexpss128_round_mask", 37, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 106 },
    { "__builtin_ia32_insert128i256", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 581 },
    { "__builtin_ia32_insertf32x4", 26, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 626 },
    { "__builtin_ia32_insertf32x4_256", 30, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 588 },
    { "__builtin_ia32_insertf32x8", 26, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 582 },
    { "__builtin_ia32_insertf64x2_256", 30, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 586 },
    { "__builtin_ia32_insertf64x2_512", 30, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 624 },
    { "__builtin_ia32_insertf64x4", 26, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 584 },
    { "__builtin_ia32_inserti32x4", 26, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 627 },
    { "__builtin_ia32_inserti32x4_256", 30, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 589 },
    { "__builtin_ia32_inserti32x8", 26, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 583 },
    { "__builtin_ia32_inserti64x2_256", 30, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 587 },
    { "__builtin_ia32_inserti64x2_512", 30, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 625 },
    { "__builtin_ia32_inserti64x4", 26, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 585 },
    { "__builtin_ia32_kshiftlidi", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 823 },
    { "__builtin_ia32_kshiftlihi", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 821 },
    { "__builtin_ia32_kshiftliqi", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 820 },
    { "__builtin_ia32_kshiftlisi", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 822 },
    { "__builtin_ia32_kshiftridi", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 827 },
    { "__builtin_ia32_kshiftrihi", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 825 },
    { "__builtin_ia32_kshiftriqi", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 824 },
    { "__builtin_ia32_kshiftrisi", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 826 },
    { "__builtin_ia32_maxpd512", 23, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 0x1110, 59 },
    { "__builtin_ia32_maxph512", 23, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 0x1110, 63 },
    { "__builtin_ia32_maxps512", 23, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 0x1110, 60 },
    { "__builtin_ia32_maxsd_round_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 111 },
    { "__builtin_ia32_maxsh_round_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 113 },
    { "__builtin_ia32_maxss_round_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 112 },
    { "__builtin_ia32_minpd512", 23, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 0x1110, 61 },
    { "__builtin_ia32_minph512", 23, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 0x1110, 64 },
    { "__builtin_ia32_minps512", 23, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 2, 0, 0, 0x1110, 62 },
    { "__builtin_ia32_minsd_round_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 114 },
    { "__builtin_ia32_minsh_round_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 116 },
    { "__builtin_ia32_minss_round_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 115 },
    { "__builtin_ia32_palignr128", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 840 },
    { "__builtin_ia32_palignr256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 841 },
    { "__builtin_ia32_palignr512", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 842 },
    { "__builtin_ia32_pblendd128", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 698 },
    { "__builtin_ia32_pblendd256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 839 },
    { "__builtin_ia32_pblendw128", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 836 },
    { "__builtin_ia32_pblendw256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 837 },
    { "__builtin_ia32_permdf256", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 743 },
    { "__builtin_ia32_permdf512", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 745 },
    { "__builtin_ia32_permdi256", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 744 },
    { "__builtin_ia32_permdi512", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 746 },
    { "__builtin_ia32_permti256", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 835 },
    { "__builtin_ia32_prefetchi", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 2, 3, 0, 948 },
    { "__builtin_ia32_prold128", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 789 },
    { "__builtin_ia32_prold256", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 790 },
    { "__builtin_ia32_prold512", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 787 },
    { "__builtin_ia32_prolq128", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 791 },
    { "__builtin_ia32_prolq256", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 792 },
    { "__builtin_ia32_prolq512", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 788 },
    { "__builtin_ia32_prord128", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 795 },
    { "__builtin_ia32_prord256", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 796 },
    { "__builtin_ia32_prord512", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 793 },
    { "__builtin_ia32_prorq128", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 797 },
    { "__builtin_ia32_prorq256", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 798 },
    { "__builtin_ia32_prorq512", 23, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 794 },
    { "__builtin_ia32_pshufd", 21, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 751 },
    { "__builtin_ia32_pshufd256", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 752 },
    { "__builtin_ia32_pshufd512", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 753 },
    { "__builtin_ia32_pshufhw", 22, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 754 },
    { "__builtin_ia32_pshufhw256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 755 },
    { "__builtin_ia32_pshufhw512", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 756 },
    { "__builtin_ia32_pshuflw", 22, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 757 },
    { "__builtin_ia32_pshuflw256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 758 },
    { "__builtin_ia32_pshuflw512", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 759 },
    { "__builtin_ia32_pslldqi128_byteshift", 35, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 814 },
    { "__builtin_ia32_pslldqi256_byteshift", 35, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 815 },
    { "__builtin_ia32_pslldqi512_byteshift", 35, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 816 },
    { "__builtin_ia32_psrldqi128_byteshift", 35, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 817 },
    { "__builtin_ia32_psrldqi256_byteshift", 35, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 818 },
    { "__builtin_ia32_psrldqi512_byteshift", 35, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 819 },
    { "__builtin_ia32_pternlogd128_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 919 },
    { "__builtin_ia32_pternlogd128_maskz", 33, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 920 },
    { "__builtin_ia32_pternlogd256_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 921 },
    { "__builtin_ia32_pternlogd256_maskz", 33, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 922 },
    { "__builtin_ia32_pternlogd512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 915 },
    { "__builtin_ia32_pternlogd512_maskz", 33, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 916 },
    { "__builtin_ia32_pternlogq128_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 923 },
    { "__builtin_ia32_pternlogq128_maskz", 33, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 924 },
    { "__builtin_ia32_pternlogq256_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 925 },
    { "__builtin_ia32_pternlogq256_maskz", 33, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 926 },
    { "__builtin_ia32_pternlogq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 917 },
    { "__builtin_ia32_pternlogq512_maskz", 33, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 3, 0, 255, 0, 918 },
    { "__builtin_ia32_rndscalepd_mask", 30, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 120 },
    { "__builtin_ia32_rndscalepd_mask", 30, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 770 },
    { "__builtin_ia32_rndscaleps_mask", 30, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 121 },
    { "__builtin_ia32_rndscaleps_mask", 30, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 769 },
    { "__builtin_ia32_scalefpd512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 246 },
    { "__builtin_ia32_scalefph512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 245 },
    { "__builtin_ia32_scalefps512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 247 },
    { "__builtin_ia32_scalefsd_round_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 248 },
    { "__builtin_ia32_scalefsh_round_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 250 },
    { "__builtin_ia32_scalefss_round_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 249 },
    { "__builtin_ia32_scatterdiv16si", 29, CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 0x0116, 416 },
    { "__builtin_ia32_scatterdiv8di", 28, CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 0x0116, 415 },
    { "__builtin_ia32_scattersiv16si", 29, CHECK_BUILTIN_X86_CONSTRAINT_SCALE, 4, 0, 0, 0x0116, 414 },
    { "__builtin_ia32_shuf_f32x4", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 851 },
    { "__builtin_ia32_shuf_f32x4_256", 29, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 620 },
    { "__builtin_ia32_shuf_f64x2", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 852 },
    { "__builtin_ia32_shuf_f64x2_256", 29, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 621 },
    { "__builtin_ia32_shuf_i32x4", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 853 },
    { "__builtin_ia32_shuf_i32x4_256", 29, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 622 },
    { "__builtin_ia32_shuf_i64x2", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 854 },
    { "__builtin_ia32_shuf_i64x2_256", 29, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 623 },
    { "__builtin_ia32_shufpd", 21, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 616 },
    { "__builtin_ia32_shufpd256", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 701 },
    { "__builtin_ia32_shufpd512", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 855 },
    { "__builtin_ia32_shufps", 21, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 856 },
    { "__builtin_ia32_shufps256", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 857 },
    { "__builtin_ia32_shufps512", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 858 },
    { "__builtin_ia32_sqrtsd_round_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 254 },
    { "__builtin_ia32_sqrtsh_round_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 256 },
    { "__builtin_ia32_sqrtss_round_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 255 },
    { "__builtin_ia32_ucmpb128_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 652 },
    { "__builtin_ia32_ucmpb256_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 656 },
    { "__builtin_ia32_ucmpb512_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 660 },
    { "__builtin_ia32_ucmpd128_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 654 },
    { "__builtin_ia32_ucmpd256_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 658 },
    { "__builtin_ia32_ucmpd512_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 662 },
    { "__builtin_ia32_ucmpq128_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 655 },
    { "__builtin_ia32_ucmpq256_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 659 },
    { "__builtin_ia32_ucmpq512_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 663 },
    { "__builtin_ia32_ucmpw128_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 653 },
    { "__builtin_ia32_ucmpw256_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 657 },
    { "__builtin_ia32_ucmpw512_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 661 },
    { "__builtin_ia32_vcomish", 22, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 89 },
    { "__builtin_ia32_vcvt2ps2phx512_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 306 },
    { "__builtin_ia32_vcvtdq2ph512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 214 },
    { "__builtin_ia32_vcvtpd2ph512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 199 },
    { "__builtin_ia32_vcvtph2dq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 220 },
    { "__builtin_ia32_vcvtph2pd512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 67 },
    { "__builtin_ia32_vcvtph2ps512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 90 },
    { "__builtin_ia32_vcvtph2psx512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 68 },
    { "__builtin_ia32_vcvtph2qq512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 222 },
    { "__builtin_ia32_vcvtph2udq512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 221 },
    { "__builtin_ia32_vcvtph2uqq512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 223 },
    { "__builtin_ia32_vcvtph2uw512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 219 },
    { "__builtin_ia32_vcvtph2w512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 218 },
    { "__builtin_ia32_vcvtps2phx512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 200 },
    { "__builtin_ia32_vcvtqq2ph512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 224 },
    { "__builtin_ia32_vcvtsd2sh_round_mask", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 253 },
    { "__builtin_ia32_vcvtsd2usi32", 27, CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 0x0f10, 156 },
    { "__builtin_ia32_vcvtsd2usi64", 27, CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 0x0f10, 157 },
    { "__builtin_ia32_vcvtsh2sd_round_mask", 35, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 102 },
    { "__builtin_ia32_vcvtsh2si32", 26, CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 0x0f10, 162 },
    { "__builtin_ia32_vcvtsh2si64", 26, CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 0x0f10, 163 },
    { "__builtin_ia32_vcvtsh2ss_round_mask", 35, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 4, 0, 0, 0x1110, 103 },
    { "__builtin_ia32_vcvtsh2usi32", 27, CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 0x0f10, 164 },
    { "__builtin_ia32_vcvtsh2usi64", 27, CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 0x0f10, 165 },
    { "__builtin_ia32_vcvtss2sh_round_mask", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 252 },
    { "__builtin_ia32_vcvtss2usi32", 27, CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 0x0f10, 160 },
    { "__builtin_ia32_vcvtss2usi64", 27, CHECK_BUILTIN_X86_CONSTRAINT_RC, 1, 0, 0, 0x0f10, 161 },
    { "__builtin_ia32_vcvttpd2dqs512_round_mask", 40, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 310 },
    { "__builtin_ia32_vcvttpd2qqs512_round_mask", 40, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 312 },
    { "__builtin_ia32_vcvttpd2udqs512_round_mask", 41, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 311 },
    { "__builtin_ia32_vcvttpd2uqqs512_round_mask", 41, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 313 },
    { "__builtin_ia32_vcvttph2dq512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 80 },
    { "__builtin_ia32_vcvttph2qq512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 82 },
    { "__builtin_ia32_vcvttph2udq512_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 81 },
    { "__builtin_ia32_vcvttph2uqq512_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 83 },
    { "__builtin_ia32_vcvttph2uw512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 79 },
    { "__builtin_ia32_vcvttph2w512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 78 },
    { "__builtin_ia32_vcvttps2dqs512_round_mask", 40, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 314 },
    { "__builtin_ia32_vcvttps2qqs512_round_mask", 40, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 316 },
    { "__builtin_ia32_vcvttps2udqs512_round_mask", 41, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 315 },
    { "__builtin_ia32_vcvttps2uqqs512_round_mask", 41, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 3, 0, 0, 0x1110, 317 },
    { "__builtin_ia32_vcvttsd2si32", 27, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 37 },
    { "__builtin_ia32_vcvttsd2si64", 27, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 38 },
    { "__builtin_ia32_vcvttsd2usi32", 28, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 39 },
    { "__builtin_ia32_vcvttsd2usi64", 28, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 40 },
    { "__builtin_ia32_vcvttsh2si32", 27, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 45 },
    { "__builtin_ia32_vcvttsh2si64", 27, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 46 },
    { "__builtin_ia32_vcvttsh2usi32", 28, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 47 },
    { "__builtin_ia32_vcvttsh2usi64", 28, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 48 },
    { "__builtin_ia32_vcvttss2si32", 27, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 41 },
    { "__builtin_ia32_vcvttss2si64", 27, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 42 },
    { "__builtin_ia32_vcvttss2usi32", 28, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 43 },
    { "__builtin_ia32_vcvttss2usi64", 28, CHECK_BUILTIN_X86_CONSTRAINT_SAE, 1, 0, 0, 0x1110, 44 },
    { "__builtin_ia32_vcvtudq2ph512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 215 },
    { "__builtin_ia32_vcvtuqq2ph512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 225 },
    { "__builtin_ia32_vcvtuw2ph512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 217 },
    { "__builtin_ia32_vcvtw2ph512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 3, 0, 0, 0x0f10, 216 },
    { "__builtin_ia32_vec_ext_v16hi", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 15, 0, 693 },
    { "__builtin_ia32_vec_ext_v16qi", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 15, 0, 692 },
    { "__builtin_ia32_vec_ext_v2di", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 560 },
    { "__builtin_ia32_vec_ext_v32qi", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 31, 0, 719 },
    { "__builtin_ia32_vec_ext_v4di", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 598 },
    { "__builtin_ia32_vec_ext_v4hi", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 595 },
    { "__builtin_ia32_vec_ext_v4sf", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 597 },
    { "__builtin_ia32_vec_ext_v4si", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 596 },
    { "__builtin_ia32_vec_ext_v8hi", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 7, 0, 608 },
    { "__builtin_ia32_vec_ext_v8si", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 7, 0, 609 },
    { "__builtin_ia32_vec_set_v16hi", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 714 },
    { "__builtin_ia32_vec_set_v16qi", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 15, 0, 713 },
    { "__builtin_ia32_vec_set_v2di", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 577 },
    { "__builtin_ia32_vec_set_v32qi", 28, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 31, 0, 738 },
    { "__builtin_ia32_vec_set_v4di", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 619 },
    { "__builtin_ia32_vec_set_v4hi", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 617 },
    { "__builtin_ia32_vec_set_v4si", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 3, 0, 618 },
    { "__builtin_ia32_vec_set_v8hi", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 672 },
    { "__builtin_ia32_vec_set_v8si", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 673 },
    { "__builtin_ia32_vextractf128_pd256", 33, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 561 },
    { "__builtin_ia32_vextractf128_ps256", 33, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 562 },
    { "__builtin_ia32_vextractf128_si256", 33, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 1, 0, 563 },
    { "__builtin_ia32_vfcmaddcph512_mask", 33, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 299 },
    { "__builtin_ia32_vfcmaddcph512_mask3", 34, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 301 },
    { "__builtin_ia32_vfcmaddcph512_maskz", 34, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 300 },
    { "__builtin_ia32_vfcmaddcsh_mask", 30, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 296 },
    { "__builtin_ia32_vfcmaddcsh_round_mask", 36, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 297 },
    { "__builtin_ia32_vfcmaddcsh_round_mask3", 37, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 298 },
    { "__builtin_ia32_vfcmulcph512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 305 },
    { "__builtin_ia32_vfcmulcsh_mask", 29, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 304 },
    { "__builtin_ia32_vfmaddcph512_mask", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 293 },
    { "__builtin_ia32_vfmaddcph512_mask3", 33, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 295 },
    { "__builtin_ia32_vfmaddcph512_maskz", 33, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 294 },
    { "__builtin_ia32_vfmaddcsh_mask", 29, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 290 },
    { "__builtin_ia32_vfmaddcsh_round_mask", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 291 },
    { "__builtin_ia32_vfmaddcsh_round_mask3", 36, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 292 },
    { "__builtin_ia32_vfmaddph512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 274 },
    { "__builtin_ia32_vfmaddph512_mask3", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 276 },
    { "__builtin_ia32_vfmaddph512_maskz", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 275 },
    { "__builtin_ia32_vfmaddsd3_mask", 29, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 257 },
    { "__builtin_ia32_vfmaddsd3_mask3", 30, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 259 },
    { "__builtin_ia32_vfmaddsd3_maskz", 30, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 258 },
    { "__builtin_ia32_vfmaddsh3_mask", 29, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 263 },
    { "__builtin_ia32_vfmaddsh3_mask3", 30, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 265 },
    { "__builtin_ia32_vfmaddsh3_maskz", 30, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 264 },
    { "__builtin_ia32_vfmaddss3_mask", 29, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 260 },
    { "__builtin_ia32_vfmaddss3_mask3", 30, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 262 },
    { "__builtin_ia32_vfmaddss3_maskz", 30, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 261 },
    { "__builtin_ia32_vfmaddsubpd512_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 278 },
    { "__builtin_ia32_vfmaddsubpd512_mask3", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 280 },
    { "__builtin_ia32_vfmaddsubpd512_maskz", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 279 },
    { "__builtin_ia32_vfmaddsubph512_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 286 },
    { "__builtin_ia32_vfmaddsubph512_mask3", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 288 },
    { "__builtin_ia32_vfmaddsubph512_maskz", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 287 },
    { "__builtin_ia32_vfmaddsubps512_mask", 34, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 282 },
    { "__builtin_ia32_vfmaddsubps512_mask3", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 284 },
    { "__builtin_ia32_vfmaddsubps512_maskz", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 283 },
    { "__builtin_ia32_vfmsubaddpd512_mask3", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 281 },
    { "__builtin_ia32_vfmsubaddph512_mask3", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 289 },
    { "__builtin_ia32_vfmsubaddps512_mask3", 35, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 285 },
    { "__builtin_ia32_vfmsubph512_mask3", 32, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 277 },
    { "__builtin_ia32_vfmulcph512_mask", 31, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 303 },
    { "__builtin_ia32_vfmulcsh_mask", 28, CHECK_BUILTIN_X86_CONSTRAINT_RC, 4, 0, 0, 0x0f10, 302 },
    { "__builtin_ia32_vinsertf128_pd256", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 578 },
    { "__builtin_ia32_vinsertf128_ps256", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 579 },
    { "__builtin_ia32_vinsertf128_si256", 32, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 1, 0, 580 },
    { "__builtin_ia32_vpcomb", 21, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 668 },
    { "__builtin_ia32_vpcomd", 21, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 670 },
    { "__builtin_ia32_vpcomq", 21, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 671 },
    { "__builtin_ia32_vpcomub", 22, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 664 },
    { "__builtin_ia32_vpcomud", 22, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 666 },
    { "__builtin_ia32_vpcomuq", 22, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 667 },
    { "__builtin_ia32_vpcomuw", 22, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 665 },
    { "__builtin_ia32_vpcomw", 21, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 7, 0, 669 },
    { "__builtin_ia32_vperm2f128_pd256", 31, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 832 },
    { "__builtin_ia32_vperm2f128_ps256", 31, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 833 },
    { "__builtin_ia32_vperm2f128_si256", 31, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 834 },
    { "__builtin_ia32_vpermilpd", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 3, 0, 594 },
    { "__builtin_ia32_vpermilpd256", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 15, 0, 678 },
    { "__builtin_ia32_vpermilpd512", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 749 },
    { "__builtin_ia32_vpermilps", 24, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 747 },
    { "__builtin_ia32_vpermilps256", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 748 },
    { "__builtin_ia32_vpermilps512", 27, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 1, 0, 255, 0, 750 },
    { "__builtin_ia32_vpshldd128", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 862 },
    { "__builtin_ia32_vpshldd256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 863 },
    { "__builtin_ia32_vpshldd512", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 864 },
    { "__builtin_ia32_vpshldq128", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 865 },
    { "__builtin_ia32_vpshldq256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 866 },
    { "__builtin_ia32_vpshldq512", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 867 },
    { "__builtin_ia32_vpshldw128", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 868 },
    { "__builtin_ia32_vpshldw256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 869 },
    { "__builtin_ia32_vpshldw512", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 870 },
    { "__builtin_ia32_vpshrdd128", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 871 },
    { "__builtin_ia32_vpshrdd256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 872 },
    { "__builtin_ia32_vpshrdd512", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 873 },
    { "__builtin_ia32_vpshrdq128", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 874 },
    { "__builtin_ia32_vpshrdq256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 875 },
    { "__builtin_ia32_vpshrdq512", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 876 },
    { "__builtin_ia32_vpshrdw128", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 877 },
    { "__builtin_ia32_vpshrdw256", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 878 },
    { "__builtin_ia32_vpshrdw512", 25, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, 2, 0, 255, 0, 879 },
};

_Static_assert(BUSTER_ARRAY_LENGTH(check_builtin_x86_rules) == 353, "the audited x86 rule manifest has 353 rows");

static int check_builtin_x86_compare(const char* name, uint32_t bytes, const CheckBuiltinX86Rule* rule)
{
    const uint32_t common = bytes < rule->name_bytes ? bytes : rule->name_bytes;
    int result = memcmp(name, rule->name, common);
    if (result == 0)
    {
        result = bytes < rule->name_bytes ? -1 : bytes > rule->name_bytes ? 1 : 0;
    }
    return result;
}

static void check_builtin_x86_fail(CheckBuiltinX86Result* output, CheckBuiltinX86Status status, CheckBuiltinX86Constraint constraint,
                                   CheckDiagnostic diagnostic, uint32_t argument, uint32_t unsupported)
{
    output->status = status;
    output->constraint = constraint;
    output->unsupported |= unsupported;
    check_action_diagnose(&output->action, diagnostic, argument);
}

static bool check_builtin_x86_witness(const TypeTable* table, const CheckValue* value)
{
    const bool result = type_is_valid(table, value->type) && check_action_is_integer(table, value->type) &&
        value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
    return result;
}

static bool check_builtin_x86_integer(const TypeTable* table, const CheckBuiltinX86Action* input, CheckBuiltinX86Result* output,
                                      uint32_t argument, unsigned __int128* bits, bool* negative)
{
    const uint64_t bit = UINT64_C(1) << argument;
    output->checked_ice_arguments |= bit;
    bool result = false;
    if ((input->ice_arguments & bit) == 0)
    {
        check_builtin_x86_fail(output, CHECK_BUILTIN_X86_ERROR, CHECK_BUILTIN_X86_CONSTRAINT_ICE, CHECK_DIAG_INCOMPATIBLE, argument, 0);
    }
    else
    {
        const CheckValue* value = input->arguments + argument;
        uint32_t width = 0;
        bool is_signed = false;
        if (!check_action_integer_type(table, value->type, &width, &is_signed) || width == 0 || width > 128)
        {
            check_builtin_x86_fail(output, CHECK_BUILTIN_X86_UNSUPPORTED, CHECK_BUILTIN_X86_CONSTRAINT_ICE, CHECK_DIAG_UNSUPPORTED, argument,
                                   BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_INTEGER_WIDTH);
        }
        else
        {
            const unsigned __int128 mask = check_action_mask(width);
            if ((value->integer & ~mask) != 0)
            {
                check_builtin_x86_fail(output, CHECK_BUILTIN_X86_UNSUPPORTED, CHECK_BUILTIN_X86_CONSTRAINT_ICE, CHECK_DIAG_UNSUPPORTED, argument,
                                       BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_INTEGER_NORMALIZATION);
            }
            else
            {
                *bits = value->integer;
                *negative = is_signed && ((value->integer >> (width - 1)) & 1) != 0;
                result = true;
            }
        }
    }
    return result;
}

static __attribute__((unused)) void check_builtin_x86_one(const TypeTable* table, const CheckBuiltinX86Action* input, CheckBuiltinX86Result* output)
{
    *output = (CheckBuiltinX86Result){ .action = input->typed_call };
    if (output->action.diagnostic != CHECK_DIAG_NONE)
    {
        output->status = CHECK_BUILTIN_X86_ERROR;
    }
    else if (!input->name_in_scope)
    {
        check_builtin_x86_fail(output, CHECK_BUILTIN_X86_UNSUPPORTED, CHECK_BUILTIN_X86_CONSTRAINT_NONE, CHECK_DIAG_UNSUPPORTED, 0,
                               BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_SCOPE);
    }
    else if (input->argument_count > BUSTER_CHECK_BUILTIN_X86_ARGUMENTS)
    {
        check_builtin_x86_fail(output, CHECK_BUILTIN_X86_UNSUPPORTED, CHECK_BUILTIN_X86_CONSTRAINT_NONE, CHECK_DIAG_UNSUPPORTED, 0,
                               BUSTER_CHECK_BUILTIN_X86_UNSUPPORTED_ARGUMENT_BOUND);
    }
    else if (input->name == NULL || input->name_bytes == 0 || (input->argument_count != 0 && input->arguments == NULL) ||
             (uint32_t)input->reachability > CHECK_BUILTIN_X86_UNREACHABLE)
    {
        check_builtin_x86_fail(output, CHECK_BUILTIN_X86_INVALID, CHECK_BUILTIN_X86_CONSTRAINT_NONE, CHECK_DIAG_INVALID_TYPE, 0, 0);
    }
    else
    {
        const uint64_t valid = input->argument_count == 64 ? UINT64_MAX : (UINT64_C(1) << input->argument_count) - 1;
        if (((input->required_ice_arguments | input->ice_arguments) & ~valid) != 0)
        {
            check_builtin_x86_fail(output, CHECK_BUILTIN_X86_INVALID, CHECK_BUILTIN_X86_CONSTRAINT_ICE, CHECK_DIAG_INVALID_TYPE, 0, 0);
        }
        uint32_t first = 0;
        uint32_t end = (uint32_t)BUSTER_ARRAY_LENGTH(check_builtin_x86_rules);
        while (output->status == CHECK_BUILTIN_X86_OK && first < end)
        {
            const uint32_t middle = first + (end - first) / 2;
            if (check_builtin_x86_compare(input->name, input->name_bytes, check_builtin_x86_rules + middle) > 0)
            {
                first = middle + 1;
            }
            else
            {
                end = middle;
            }
        }
        uint32_t last = first;
        while (output->status == CHECK_BUILTIN_X86_OK && last < BUSTER_ARRAY_LENGTH(check_builtin_x86_rules) &&
               check_builtin_x86_compare(input->name, input->name_bytes, check_builtin_x86_rules + last) == 0)
        {
            if (check_builtin_x86_rules[last].argument >= input->argument_count)
            {
                check_builtin_x86_fail(output, CHECK_BUILTIN_X86_INVALID, CHECK_BUILTIN_X86_CONSTRAINT_NONE, CHECK_DIAG_ARGUMENT_COUNT,
                                       check_builtin_x86_rules[last].argument, 0);
            }
            last += 1;
        }
        output->matched_rules = last - first;
        // Validate every supplied witness, even one not needed by this particular constraint list. Such metadata
        // cannot turn a floating value, runtime read, effect or potential evaluation fault into an integer ICE.
        for (uint32_t argument = 0; output->status == CHECK_BUILTIN_X86_OK && argument < input->argument_count; argument += 1)
        {
            if ((input->ice_arguments & (UINT64_C(1) << argument)) != 0 && !check_builtin_x86_witness(table, input->arguments + argument))
            {
                check_builtin_x86_fail(output, CHECK_BUILTIN_X86_INVALID, CHECK_BUILTIN_X86_CONSTRAINT_ICE, CHECK_DIAG_INVALID_TYPE, argument, 0);
            }
        }
        for (uint32_t argument = 0; output->status == CHECK_BUILTIN_X86_OK && argument < input->argument_count; argument += 1)
        {
            if ((input->required_ice_arguments & (UINT64_C(1) << argument)) != 0)
            {
                unsigned __int128 bits = 0;
                bool negative = false;
                check_builtin_x86_integer(table, input, output, argument, &bits, &negative);
            }
        }
        for (uint32_t i = first; output->status == CHECK_BUILTIN_X86_OK && i < last; i += 1)
        {
            const CheckBuiltinX86Rule* rule = check_builtin_x86_rules + i;
            if (rule->kind != CHECK_BUILTIN_X86_CONSTRAINT_RANGE || !input->constant_evaluated_context)
            {
                unsigned __int128 bits = 0;
                bool negative = false;
                if (check_builtin_x86_integer(table, input, output, rule->argument, &bits, &negative))
                {
                    if (rule->kind == CHECK_BUILTIN_X86_CONSTRAINT_RANGE)
                    {
                        if (negative || bits < rule->low || bits > rule->high)
                        {
                            if (output->range_event_count < BUSTER_CHECK_BUILTIN_X86_EVENTS)
                            {
                                CheckBuiltinX86RangeEvent* event = output->range_events + output->range_event_count;
                                *event = (CheckBuiltinX86RangeEvent)
                                {
                                    .node = input->node,
                                    .argument = rule->argument,
                                    .low = rule->low,
                                    .high = rule->high,
                                    .source_line = rule->source_line,
                                    .value = input->arguments[rule->argument],
                                    .disposition = input->runtime_diagnostic_suppressed || input->reachability == CHECK_BUILTIN_X86_UNREACHABLE ?
                                        CHECK_BUILTIN_X86_SUPPRESSED : input->reachability == CHECK_BUILTIN_X86_REACHABLE ? CHECK_BUILTIN_X86_READY : CHECK_BUILTIN_X86_DEFERRED,
                                    .default_error = true,
                                };
                                output->range_event_count += 1;
                                output->constraint = CHECK_BUILTIN_X86_CONSTRAINT_RANGE;
                            }
                            else
                            {
                                check_builtin_x86_fail(output, CHECK_BUILTIN_X86_UNSUPPORTED, CHECK_BUILTIN_X86_CONSTRAINT_RANGE, CHECK_DIAG_RESOURCE,
                                                       rule->argument, 0);
                            }
                        }
                    }
                    else if (negative || bits >= 16 || (rule->accepted & (UINT32_C(1) << (uint32_t)bits)) == 0)
                    {
                        check_builtin_x86_fail(output, CHECK_BUILTIN_X86_ERROR, rule->kind, CHECK_DIAG_INCOMPATIBLE, rule->argument, 0);
                    }
                }
            }
        }
    }
}

static __attribute__((unused)) void check_builtin_x86(const TypeTable* table, const CheckBuiltinX86Action* actions, CheckBuiltinX86Result* results, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        check_builtin_x86_one(table, actions + i, results + i);
    }
}

#endif
