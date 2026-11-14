// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C driver's Tier 1 outcome, kept by value past every arena rollback (#1054). A cause names the enum whose
// original error is retained, not a replacement for it. The instruction/detail pair is that API's location, not a
// source token; the emission separately retains the body's declaration for the source diagnostic. No pointers into
// a refused attempt escape. Zero is an unattempted body, never successful Tier 1 coverage.

#ifndef BUSTER_CHECK_TIER1_OUTCOME_C
#define BUSTER_CHECK_TIER1_OUTCOME_C

typedef enum CheckTier1Stage
{
    CHECK_TIER1_COMPILED,
    CHECK_TIER1_NAKED,
    CHECK_TIER1_LOWERING,
    CHECK_TIER1_PROMOTION,
    CHECK_TIER1_OPTIMIZATION,
    CHECK_TIER1_ANALYSIS,
    CHECK_TIER1_SELECTION,
    CHECK_TIER1_EMISSION,
    CHECK_TIER1_RESOURCE,
    CHECK_TIER1_PUBLICATION,
    CHECK_TIER1_VALUE_NUMBERING,
    // The 128-bit integers' split into halves (ir_wide.c, #141), between promotion and the optimizer.
    CHECK_TIER1_WIDE,
    CHECK_TIER1_STAGE_COUNT,
} CheckTier1Stage;

typedef enum CheckTier1Category
{
    CHECK_TIER1_UNATTEMPTED,
    CHECK_TIER1_SUCCESS,
    CHECK_TIER1_UNSUPPORTED,
    CHECK_TIER1_RESOURCE_FAILURE,
    CHECK_TIER1_INTERNAL,
    CHECK_TIER1_CATEGORY_COUNT,
} CheckTier1Category;

typedef enum CheckTier1Cause
{
    CHECK_TIER1_CAUSE_NONE,
    CHECK_TIER1_CAUSE_ADMISSION,
    CHECK_TIER1_CAUSE_IR,
    CHECK_TIER1_CAUSE_DOMINATORS,
    CHECK_TIER1_CAUSE_SELECTION,
    CHECK_TIER1_CAUSE_EMISSION,
    CHECK_TIER1_CAUSE_ARENA,
    CHECK_TIER1_CAUSE_PUBLICATION,
    CHECK_TIER1_CAUSE_LIVENESS,
    CHECK_TIER1_CAUSE_ALLOCATION,
    CHECK_TIER1_CAUSE_CFG_ALLOCATION,
    CHECK_TIER1_CAUSE_FRAME,
    // Tier1EmitOptions.certify's checker (#939): its Tier1CertError.
    CHECK_TIER1_CAUSE_CERTIFICATE,
    CHECK_TIER1_CAUSE_COUNT,
} CheckTier1Cause;

typedef enum CheckTier1Fallback
{
    CHECK_TIER1_FALLBACK_NONE,
    CHECK_TIER1_FALLBACK_TIER0,
    CHECK_TIER1_FALLBACK_FORBIDDEN,
} CheckTier1Fallback;

// What a call selection refused has (#1116), as bits of CheckTier1Outcome.call_kinds: the shapes of its arguments and
// result that the selector's call boundary doesn't take (tier1_select_call_supported's predicates), so that a corpus's
// refusals say which shape to take first. Indirect and variadic are kinds the boundary takes, which the counts still tell
// apart. A call has every kind it has, so the kinds' counts can sum past the calls'. Stack arguments are the arguments
// past the boundary's limit (BUSTER_TIER1_SELECT_MAX_ARGUMENTS), not every argument the ABI passes on the stack, which
// Tier 1 takes below it (#141); an ABI record is one the boundary refuses, the call's or an argument's. Aggregate
// staging past the red zone is frame storage (#1450). The boundary takes floating and long double arguments and results (#141),
// with or without a record, so no kind names them: one with a record it refuses is an ABI record's.
typedef enum CheckTier1CallKind
{
    CHECK_TIER1_CALL_INDIRECT,
    CHECK_TIER1_CALL_VARIADIC,
    CHECK_TIER1_CALL_ABI_RECORD,
    CHECK_TIER1_CALL_STACK_ARGUMENTS,
    CHECK_TIER1_CALL_NARROW_ARGUMENT,
    CHECK_TIER1_CALL_OTHER_ARGUMENT,
    CHECK_TIER1_CALL_OTHER_RESULT,
    CHECK_TIER1_CALL_ARGUMENT_COUNT,
    CHECK_TIER1_CALL_KIND_COUNT,
} CheckTier1CallKind;

static const char* const check_tier1_call_kind_names[CHECK_TIER1_CALL_KIND_COUNT] = {
    "indirect", "variadic", "abi-record", "stack-arguments", "narrow-argument", "other-argument", "other-result", "argument-count",
};

// These two domains have no upstream error enum: the adapter's exact refusal/capacity reason.
#define BUSTER_CHECK_TIER1_NAKED 1u
// A caller of a function that returns twice (#1847, Tier0Function.calls_returns_twice), which Tier 1 leaves to Tier 0.
#define BUSTER_CHECK_TIER1_RETURNS_TWICE 2u
#define BUSTER_CHECK_TIER1_ARENA_RESERVE 1u
#define BUSTER_CHECK_TIER1_ARENA_OUTPUT 2u
#define BUSTER_CHECK_TIER1_FRAME_PUBLICATION 1u
// --tier1-schedule=post's order of a block that the scalar reference refused (--verify), or no room for its scratch.
#define BUSTER_CHECK_TIER1_SCHEDULE 2u
#define BUSTER_CHECK_TIER1_ARENA_SCHEDULE 3u

typedef struct CheckTier1Outcome CheckTier1Outcome;
struct CheckTier1Outcome
{
    CheckTier1Stage stage;
    CheckTier1Category category;
    CheckTier1Cause cause;
    CheckTier1Fallback fallback;
    uint32_t error;
    uint32_t instruction;
    uint32_t detail;
    uint32_t operation;
    // The emitter wrapper code too, when cause is a nested liveness/allocation/frame API.
    uint32_t emission_error;
    // The full IrCheck, including whether its dominance obligation was checked.
    bool dominance_checked;
    // Where selection refused a call, the call's kinds (1 << CheckTier1CallKind); zero for any other outcome.
    uint32_t call_kinds;
    // Whether the body made no code because it waits for the emission's last pass (#2541 step 2,
    // check_tier1_inline_held): its callers should expand every call of it. Otherwise unattempted.
    bool held;
};

// Caller-owned regression control, immutable during a run. After a stage succeeds, substitute this raw error at its
// handoff, then follow the production classification, cleanup and publication paths. No global/environment control.
// A body is one-based in CheckProgramLanes; zero disables the control. A real failure is never overwritten.
typedef struct CheckTier1Fault CheckTier1Fault;
struct CheckTier1Fault
{
    uint32_t body;
    CheckTier1Stage stage;
    CheckTier1Cause cause;
    uint32_t error;
    uint32_t instruction;
    uint32_t detail;
};

static const char* const check_tier1_stage_names[CHECK_TIER1_STAGE_COUNT] = { "tier1", "naked", "lowering", "promotion", "optimization", "analysis", "selection", "emission", "resource", "publication", "gvn", "wide" };
static const char* const check_tier1_category_names[CHECK_TIER1_CATEGORY_COUNT] = { "unattempted", "compiled", "unsupported", "resource", "internal" };
static const char* const check_tier1_cause_names[CHECK_TIER1_CAUSE_COUNT] = { "none", "admission", "ir", "dominators", "selection", "emission", "arena", "publication", "liveness", "allocation", "cfg-allocation", "frame", "certificate" };

static CheckTier1Outcome check_tier1_failure(CheckTier1Stage stage, CheckTier1Cause cause, uint32_t error, uint32_t instruction, uint32_t detail);

// Strict qualification changes disposition, never the original category or cause. Resource failure is fatal (there
// is no Tier 1 resource retry policy yet); internal failure is always fatal, including ordinary mixed-tier builds.
static CheckTier1Outcome check_tier1_policy(CheckTier1Outcome outcome, bool strict)
{
    outcome.fallback = outcome.category == CHECK_TIER1_SUCCESS ? CHECK_TIER1_FALLBACK_NONE :
                       outcome.category == CHECK_TIER1_UNSUPPORTED && !strict ? CHECK_TIER1_FALLBACK_TIER0 : CHECK_TIER1_FALLBACK_FORBIDDEN;
    return outcome;
}

static void check_tier1_diagnostic(char* message, uint64_t capacity, const CheckTier1Outcome* outcome)
{
    const char* prefix = outcome->category == CHECK_TIER1_INTERNAL ? "internal compiler error" :
                         outcome->category == CHECK_TIER1_RESOURCE_FAILURE ? "Tier 1 resource failure" : "strict Tier 1 qualification failed";
    snprintf(message, capacity, "%s: %s, cause=%s error=%" PRIu32 " instruction=%" PRIu32 " detail=%" PRIu32 " (Tier 0 fallback forbidden)", prefix,
             check_tier1_stage_names[outcome->stage], check_tier1_cause_names[outcome->cause], outcome->error, outcome->instruction, outcome->detail);
}

#endif
