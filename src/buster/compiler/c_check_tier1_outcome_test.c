// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1054: test-check-lanes --tier1-outcomes (also in the ordinary lane suite). The complete classification table, real
// C through the real pipeline, stage handoffs that inject raw errors, a real top-level emission failure, forced nested
// emitter diagnostics, naked admission and the speculative preference's disposition are checked here. Both paths and
// 1/2/3/8 lanes must retain the original location, refuse internal/resource fallback, and restore output. Native
// mixed-tier output is checked separately: a successful executable alone is never evidence that a designated body
// compiled with Tier 1.

typedef struct CheckTier1FailureCase CheckTier1FailureCase;
struct CheckTier1FailureCase
{
    CheckTier1Fault fault;
    CheckTier1Category category;
};

static const CheckTier1FailureCase check_tier1_failure_cases[] = {
    { { 3, CHECK_TIER1_VALUE_NUMBERING, CHECK_TIER1_CAUSE_IR, IR_ERROR_UNSUPPORTED, 0x12345678, 0x87654321 }, CHECK_TIER1_UNSUPPORTED },
    { { 3, CHECK_TIER1_VALUE_NUMBERING, CHECK_TIER1_CAUSE_IR, IR_ERROR_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_VALUE_NUMBERING, CHECK_TIER1_CAUSE_IR, IR_ERROR_DOMINANCE, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_LOWERING, CHECK_TIER1_CAUSE_IR, IR_ERROR_UNSUPPORTED, 0x12345678, 0x87654321 }, CHECK_TIER1_UNSUPPORTED },
    { { 3, CHECK_TIER1_SELECTION, CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_UNSUPPORTED, 0x12345678, 0x87654321 }, CHECK_TIER1_UNSUPPORTED },
    { { 3, CHECK_TIER1_RESOURCE, CHECK_TIER1_CAUSE_ARENA, BUSTER_CHECK_TIER1_ARENA_RESERVE, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_LOWERING, CHECK_TIER1_CAUSE_IR, IR_ERROR_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_PROMOTION, CHECK_TIER1_CAUSE_IR, IR_ERROR_LIMIT, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_OPTIMIZATION, CHECK_TIER1_CAUSE_IR, IR_ERROR_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_ANALYSIS, CHECK_TIER1_CAUSE_IR, IR_ERROR_LIMIT, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_ANALYSIS, CHECK_TIER1_CAUSE_IR, IR_ERROR_METADATA, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_SELECTION, CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_PUBLICATION, CHECK_TIER1_CAUSE_ARENA, BUSTER_CHECK_TIER1_ARENA_OUTPUT, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_LOWERING, CHECK_TIER1_CAUSE_IR, IR_ERROR_STRUCTURE, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_PROMOTION, CHECK_TIER1_CAUSE_IR, IR_ERROR_PHI, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_OPTIMIZATION, CHECK_TIER1_CAUSE_IR, IR_ERROR_DOMINANCE, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_SELECTION, CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_INVALID, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_IR, IR_ERROR_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_LIVENESS, TIER1_LIVENESS_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_ALLOCATION, TIER1_ALLOCATION_CONSTRAINT, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_CFG_ALLOCATION, TIER1_CFG_INVALID, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_FRAME, TIER1_FRAME_REGISTER_CONFLICT, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_CERTIFICATE, TIER1_CERT_VALUE, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_CERTIFICATE, TIER1_CERT_CAPACITY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_INVALID, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_PUBLICATION, CHECK_TIER1_CAUSE_PUBLICATION, BUSTER_CHECK_TIER1_FRAME_PUBLICATION, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_SELECTION, CHECK_TIER1_CAUSE_SELECTION, 0xfedcba98, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
};

typedef struct CheckTier1ClassCase CheckTier1ClassCase;
struct CheckTier1ClassCase
{
    CheckTier1Cause cause;
    uint32_t error;
    CheckTier1Category category;
};

// Every CheckTier1Cause and every error value its classifier distinguishes. The stage only names where the failure
// happened; the classification table is independent of it.
static const CheckTier1ClassCase check_tier1_classification_cases[] = {
    { CHECK_TIER1_CAUSE_NONE, 0, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_ADMISSION, BUSTER_CHECK_TIER1_NAKED, CHECK_TIER1_UNSUPPORTED },
    { CHECK_TIER1_CAUSE_ADMISSION, 0, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_UNSUPPORTED, CHECK_TIER1_UNSUPPORTED },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_MEMORY, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_LIMIT, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_STRUCTURE, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_OPCODE, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_TYPE, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_OPERAND, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_METADATA, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_TERMINATOR, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_PHI, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_DOMINANCE, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_IR, IR_ERROR_IO, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_DOMINATORS, DOMINATOR_NO_MEMORY, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_DOMINATORS, DOMINATOR_MATRIX_LIMIT, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_DOMINATORS, DOMINATOR_INVALID, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_UNSUPPORTED, CHECK_TIER1_UNSUPPORTED },
    { CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_TARGET, CHECK_TIER1_UNSUPPORTED },
    { CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_MEMORY, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_LIMIT, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_ALLOCATION, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_INVALID, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_SELECTION, 0xfedcba98, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_UNSUPPORTED, CHECK_TIER1_UNSUPPORTED },
    { CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_TARGET, CHECK_TIER1_UNSUPPORTED },
    { CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_MEMORY, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_LIMIT, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_ALLOCATION, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_FRAME, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_INVALID, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_ARENA, BUSTER_CHECK_TIER1_ARENA_RESERVE, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_ARENA, BUSTER_CHECK_TIER1_ARENA_OUTPUT, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_ARENA, 0, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_LIVENESS, TIER1_LIVENESS_UNSUPPORTED, CHECK_TIER1_UNSUPPORTED },
    { CHECK_TIER1_CAUSE_LIVENESS, TIER1_LIVENESS_MEMORY, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_LIVENESS, TIER1_LIVENESS_LIMIT, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_LIVENESS, TIER1_LIVENESS_INVALID, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_ALLOCATION, TIER1_ALLOCATION_CAPACITY, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_ALLOCATION, TIER1_ALLOCATION_CONSTRAINT, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_ALLOCATION, TIER1_ALLOCATION_INVALID, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_CFG_ALLOCATION, TIER1_CFG_CAPACITY, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_CFG_ALLOCATION, TIER1_CFG_CONSTRAINT, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_CFG_ALLOCATION, TIER1_CFG_INVALID, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_FRAME, TIER1_FRAME_CAPACITY, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_FRAME, TIER1_FRAME_OVERFLOW, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_FRAME, TIER1_FRAME_INVALID, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_FRAME, TIER1_FRAME_REGISTER_CONFLICT, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_CERTIFICATE, TIER1_CERT_CAPACITY, CHECK_TIER1_RESOURCE_FAILURE },
    { CHECK_TIER1_CAUSE_CERTIFICATE, TIER1_CERT_INVALID, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_CERTIFICATE, TIER1_CERT_CORRESPONDENCE, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_CERTIFICATE, TIER1_CERT_VALUE, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_CERTIFICATE, TIER1_CERT_TRANSITION, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_CERTIFICATE, TIER1_CERT_ENTRY, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_CERTIFICATE, TIER1_CERT_EDGE, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_PUBLICATION, BUSTER_CHECK_TIER1_FRAME_PUBLICATION, CHECK_TIER1_INTERNAL },
    { CHECK_TIER1_CAUSE_PUBLICATION, 0, CHECK_TIER1_INTERNAL },
};

static void check_tier1_test_classification(CheckLanesTestTally* tally)
{
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_tier1_classification_cases); i += 1)
    {
        const CheckTier1ClassCase* test = check_tier1_classification_cases + i;
        const CheckTier1Outcome failure = check_tier1_failure(CHECK_TIER1_LOWERING, test->cause, test->error, 0x12345678, 0x87654321);
        const CheckTier1Outcome strict = check_tier1_policy(failure, true);
        const CheckTier1Fallback expected = test->category == CHECK_TIER1_UNSUPPORTED ? CHECK_TIER1_FALLBACK_TIER0 : CHECK_TIER1_FALLBACK_FORBIDDEN;
        char detail[96];
        snprintf(detail, sizeof(detail), "%s error %" PRIu32, check_tier1_cause_names[test->cause], test->error);
        check_lanes_test_expect(tally, failure.stage == CHECK_TIER1_LOWERING && failure.category == test->category && failure.cause == test->cause &&
                                failure.error == test->error && failure.instruction == 0x12345678 && failure.detail == 0x87654321 &&
                                failure.fallback == expected && strict.fallback == CHECK_TIER1_FALLBACK_FORBIDDEN,
                                "the classification table follows the documented policy", detail);
    }
    check_lanes_test_expect(tally, BUSTER_ARRAY_LENGTH(check_tier1_emission_causes) == TIER1_EMIT_CAUSE_CERTIFICATE + 1 &&
                            check_tier1_emission_causes[TIER1_EMIT_CAUSE_NONE] == CHECK_TIER1_CAUSE_EMISSION &&
                            check_tier1_emission_causes[TIER1_EMIT_CAUSE_IR] == CHECK_TIER1_CAUSE_IR &&
                            check_tier1_emission_causes[TIER1_EMIT_CAUSE_LIVENESS] == CHECK_TIER1_CAUSE_LIVENESS &&
                            check_tier1_emission_causes[TIER1_EMIT_CAUSE_ALLOCATION] == CHECK_TIER1_CAUSE_ALLOCATION &&
                            check_tier1_emission_causes[TIER1_EMIT_CAUSE_CFG_ALLOCATION] == CHECK_TIER1_CAUSE_CFG_ALLOCATION &&
                            check_tier1_emission_causes[TIER1_EMIT_CAUSE_FRAME] == CHECK_TIER1_CAUSE_FRAME &&
                            check_tier1_emission_causes[TIER1_EMIT_CAUSE_CERTIFICATE] == CHECK_TIER1_CAUSE_CERTIFICATE &&
                            check_tier1_fault_emission_cause(CHECK_TIER1_CAUSE_CERTIFICATE) == TIER1_EMIT_CAUSE_CERTIFICATE,
                            "every emitter cause has its outcome cause", NULL);
    check_lanes_test_expect(tally, !tier1_emit_cfg_preference_fatal(TIER1_CFG_OK) && !tier1_emit_cfg_preference_fatal(TIER1_CFG_CONSTRAINT) &&
                            !tier1_emit_cfg_preference_fatal(TIER1_CFG_CAPACITY) && tier1_emit_cfg_preference_fatal(TIER1_CFG_INVALID),
                            "the speculative callee-saved preference ignores constraint and capacity", NULL);
}

static bool check_tier1_test_cause(CheckTier1Outcome outcome, const CheckTier1FailureCase* test)
{
    const CheckTier1Fault* fault = &test->fault;
    return outcome.stage == fault->stage && outcome.cause == fault->cause && outcome.error == fault->error &&
           outcome.instruction == fault->instruction && outcome.detail == fault->detail && outcome.category == test->category &&
           (fault->stage != CHECK_TIER1_EMISSION || fault->cause == CHECK_TIER1_CAUSE_EMISSION || outcome.emission_error == TIER1_EMIT_MEMORY);
}

static void check_tier1_test_rollback(CheckLanesTestTally* tally, CheckLanesTestUnit* unit, bool scalar)
{
    Arena lowered_arena = arena_reserve(UINT64_C(1) << 30);
    Arena scratch = arena_reserve(UINT64_C(1) << 30);
    Arena output = arena_reserve(UINT64_C(1) << 24);
    CheckProgramLowered lowered = { 0 };
    const CheckProgram* program = &unit->selected.program;
    bool ready = lowered_arena.base != NULL && scratch.base != NULL && output.base != NULL &&
                 check_program_lower(&lowered, program, scalar, &lowered_arena, &scratch);
    // Lowering does not publish the symbol-access column; the normal emission does that before Tier 1.
    CheckProgramCode publication = { .lowered = lowered };
    uint8_t* defined_objects = NULL;
    ready = ready && check_program_access(&publication, program, &lowered_arena, &scratch, &defined_objects);
    check_lanes_test_expect(tally, ready, "Tier 1 rollback fixture lowers", NULL);
    CheckTier1Lane lane = { 0 };
    uint8_t* prefix = ready ? arena_push(&output, 32, 1) : NULL;
    if (prefix != NULL)
    {
        memset(prefix, 0xa5, 32);
        const uint64_t mark = output.length;
        const Tier0Function* function = &lowered.functions[0];
        const uint8_t result_class = check_program_result_class(function, program, 0, false);
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_tier1_failure_cases); i += 1)
        {
            Tier0Code code;
            CheckTier1Counts counts;
            memset(&code, 0xa5, sizeof(code));
            memset(&counts, 0xa5, sizeof(counts));
            const CheckTier1FailureCase* test = check_tier1_failure_cases + i;
            const CheckTier1Outcome outcome = check_tier1_compile(&lane, function, result_class, &program->target, scalar, true, test->fault.stage == CHECK_TIER1_VALUE_NUMBERING, true, &output, &code, &counts, &test->fault);
            bool empty = lane.scratch.length == 0;
            for (uint32_t step = 0; step < BUSTER_CHECK_TIER1_STEPS; step += 1)
            {
                empty &= lane.steps[step].length == 0;
            }
            const CheckTier1Counts zero = { 0 };
            check_lanes_test_expect(tally, check_tier1_test_cause(outcome, test) && output.length == mark && code.records == NULL && code.references == NULL &&
                                    code.count == 0 && memcmp(&counts, &zero, sizeof(zero)) == 0 && empty,
                                    "a refused stage preserves its full cause, publishes no code/counts and rewinds its arenas", check_tier1_stage_names[test->fault.stage]);
            for (uint32_t k = 0; k < 32; k += 1)
            {
                check_lanes_test_expect(tally, prefix[k] == 0xa5, "refusal leaves the output prefix untouched", NULL);
            }
            // Reuse the same lane after every failure, not merely a fresh lane whose state cannot expose leftovers.
            const CheckTier1Outcome success = check_tier1_compile(&lane, function, result_class, &program->target, scalar, true, false, true, &output, &code, &counts, NULL);
            check_lanes_test_expect(tally, success.category == CHECK_TIER1_SUCCESS && code.count != 0 && counts.records == code.count,
                                    "a refused lane can subsequently publish a genuine Tier 1 function", NULL);
            arena_pop_to(&output, mark);
        }
        const uint64_t capacity = output.capacity;
        output.capacity = mark;
        Tier0Code code = { 0 };
        CheckTier1Counts counts = { 0 };
        const CheckTier1Outcome full = check_tier1_compile(&lane, function, result_class, &program->target, scalar, true, false, false, &output, &code, &counts, NULL);
        check_lanes_test_expect(tally, full.stage == CHECK_TIER1_PUBLICATION && full.category == CHECK_TIER1_RESOURCE_FAILURE &&
                                full.error == BUSTER_CHECK_TIER1_ARENA_OUTPUT && output.length == mark && code.records == NULL && counts.records == 0,
                                "real publication exhaustion is a resource error, not Tier 0 coverage", NULL);
        output.capacity = capacity;
        // A real emitter failure: the CFG consumer's liveness/CFG pass needs the emission arena, and the outcome keeps
        // both the nested cause/error and the public emitter wrapper rather than a generic emission failure. The arena
        // is reused for promotion, so find the smallest capacity where promotion fits; the CFG pass needs more, and
        // must fail there with a nested cause.
        const Tier0Function* nested_function = function;
        const uint8_t nested_result_class = result_class;
        const uint64_t emitter_capacity = lane.steps[1].capacity;
        Tier0Code chain_code = { 0 };
        CheckTier1Counts chain_counts = { 0 };
        const CheckTier1Outcome chain_full = check_tier1_compile(&lane, nested_function, nested_result_class, &program->target, scalar, true, false, false, &output, &chain_code, &chain_counts, NULL);
        check_lanes_test_expect(tally, chain_full.category == CHECK_TIER1_SUCCESS && chain_code.count != 0 && chain_counts.records == chain_code.count,
                                "the chain body fits its full emission arena", NULL);
        const uint64_t peak = lane.steps[1].peak;
        arena_pop_to(&output, mark);
        uint64_t low = 0;
        uint64_t high = peak != 0 ? peak : emitter_capacity;
        Tier0Code probe_code = { 0 };
        CheckTier1Counts probe_counts = { 0 };
        lane.steps[1].capacity = high;
        CheckTier1Outcome probe = check_tier1_compile(&lane, nested_function, nested_result_class, &program->target, scalar, true, false, false, &output, &probe_code, &probe_counts, NULL);
        if (probe.category != CHECK_TIER1_SUCCESS && high != emitter_capacity)
        {
            high = emitter_capacity;
            lane.steps[1].capacity = high;
            probe = check_tier1_compile(&lane, nested_function, nested_result_class, &program->target, scalar, true, false, false, &output, &probe_code, &probe_counts, NULL);
        }
        check_lanes_test_expect(tally, probe.category == CHECK_TIER1_SUCCESS, "the chain body fits the binary search's high bound", NULL);
        arena_pop_to(&output, mark);
        while (low + 1 < high)
        {
            const uint64_t middle = low + (high - low) / 2;
            lane.steps[1].capacity = middle;
            probe = check_tier1_compile(&lane, nested_function, nested_result_class, &program->target, scalar, true, false, false, &output, &probe_code, &probe_counts, NULL);
            if (probe.stage == CHECK_TIER1_PROMOTION)
            {
                low = middle;
            }
            else
            {
                high = middle;
            }
            arena_pop_to(&output, mark);
        }
        lane.steps[1].capacity = high;
        CheckTier1Outcome nested = check_tier1_compile(&lane, nested_function, nested_result_class, &program->target, scalar, true, false, false, &output, &code, &counts, NULL);
        arena_pop_to(&output, mark);
        char nested_detail[160];
        snprintf(nested_detail, sizeof(nested_detail), "stage=%u category=%u cause=%u error=%" PRIu32 " emission=%" PRIu32 " capacity=%" PRIu64 " peak=%" PRIu64,
                 (uint32_t)nested.stage, (uint32_t)nested.category, (uint32_t)nested.cause, nested.error, nested.emission_error, high, peak);
        check_lanes_test_expect(tally, nested.stage == CHECK_TIER1_EMISSION && nested.category == CHECK_TIER1_RESOURCE_FAILURE &&
                                nested.cause == CHECK_TIER1_CAUSE_EMISSION && nested.error == TIER1_EMIT_MEMORY &&
                                nested.emission_error == TIER1_EMIT_MEMORY && output.length == mark && code.records == NULL && counts.records == 0,
                                "a real top-level emitter failure keeps its wrapper and clears its outputs", nested_detail);
        lane.steps[1].capacity = emitter_capacity;
    }
    check_tier1_lane_release(&lane);
    arena_release(&output);
    arena_release(&scratch);
    arena_release(&lowered_arena);
}

static void check_tier1_test_lanes(CheckLanesTestTally* tally, CheckLanesTestUnit* unit, const Parser* parser, Preprocessor* pp, const InternTable* identifiers, bool scalar, bool mixed)
{
    static const uint32_t lane_counts[] = { 1, 2, 3, 8 };
    const uint32_t bodies = unit->selected.program.body_count;
    CheckLanesTestEmission reference = { 0 };
    for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(lane_counts); n += 1)
    {
        CheckProgramLanes lanes = { .lanes = lane_counts[n], .every_lane = true, .tier1 = true, .tier1_strict = !mixed, .slow_lane = n == 3 ? 1 : 0, .slow_microseconds = 200 };
        CheckLanesTestEmission good = check_lanes_test_emit(unit, parser, pp, identifiers, &lanes, scalar, false, true);
        check_lanes_test_expect(tally, good.emitted && good.built && lanes.tier1_report.requested == bodies &&
                                lanes.tier1_report.categories[CHECK_TIER1_SUCCESS] == bodies - mixed &&
                                lanes.tier1_report.categories[CHECK_TIER1_UNSUPPORTED] == mixed && lanes.tier1_report.fallbacks == mixed,
                                "explicit coverage: designated functions really compile with Tier 1", mixed ? "mixed" : "strict");
        if (n == 0)
        {
            reference = good;
        }
        else
        {
            check_lanes_test_expect(tally, check_lanes_test_same(&reference, &good), "Tier 1 objects are byte-identical on 1/2/3/8 lanes", NULL);
            free(good.object);
        }
        if (!mixed)
        {
            for (uint32_t strict = 0; strict < 2; strict += 1)
            {
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_tier1_failure_cases); i += 1)
                {
                    const CheckTier1FailureCase* test = check_tier1_failure_cases + i;
                    lanes = (CheckProgramLanes){ .lanes = lane_counts[n], .every_lane = true, .tier1 = true, .tier1_strict = strict != 0, .tier1_gvn = test->fault.stage == CHECK_TIER1_VALUE_NUMBERING, .tier1_fault = &test->fault };
                    CheckLanesTestEmission failed = check_lanes_test_emit(unit, parser, pp, identifiers, &lanes, scalar, false, true);
                    const bool fallback = test->category == CHECK_TIER1_UNSUPPORTED && !strict;
                    const uint32_t compiled = fallback ? bodies - 1 : test->fault.body - 1;
                    check_lanes_test_expect(tally, failed.emitted == fallback && (fallback ? failed.built : failed.rolled_back && check_tier1_test_cause(failed.tier1_failure, test)) &&
                                            lanes.tier1_report.requested == bodies && lanes.tier1_report.categories[CHECK_TIER1_SUCCESS] == compiled &&
                                            lanes.tier1_report.categories[test->category] == 1 && lanes.tier1_report.fallbacks == fallback && failed.again == CHECK_PROGRAM_AGAIN_NONE,
                                            "stage failures retain source-order disposition and counts on every lane count", check_tier1_stage_names[test->fault.stage]);
                    if (!fallback)
                    {
                        char message[256];
                        check_tier1_diagnostic(message, sizeof(message), &failed.tier1_failure);
                        check_lanes_test_expect(tally, strstr(message, "instruction=305419896 detail=2271560481") != NULL &&
                                                failed.token == unit->selected.program.bodies[test->fault.body - 1].declaration,
                                                "source declaration and original intermediate location both survive rollback", message);
                    }
                    free(failed.object);
                }
            }
        }
        else
        {
            lanes = (CheckProgramLanes){ .lanes = lane_counts[n], .every_lane = true, .tier1 = true, .tier1_strict = true };
            CheckLanesTestEmission refused = check_lanes_test_emit(unit, parser, pp, identifiers, &lanes, scalar, false, true);
            check_lanes_test_expect(tally, refused.rolled_back && refused.tier1_failure.category == CHECK_TIER1_UNSUPPORTED && lanes.tier1_report.fallbacks == 0,
                                    "strict qualification rejects a real capability refusal", NULL);
            free(refused.object);
        }
    }
    free(reference.object);
}

// A naked body is an explicit admission refusal, not a lowering or emission failure: ordinary --tier1 uses its
// templates, while --tier1-strict reports the admission outcome and excludes it from `admitted`. Both a plain
// template and one that takes a directive (an assembled unit, merged from its own atoms) are checked.
static void check_tier1_test_naked(CheckLanesTestTally* tally, const char* directory, bool scalar)
{
    static const char* const templates[] = {
        "__asm__(\"mov $7, %rax\\n\\tret\")",
        "__asm__(\".p2align 4\\n\\tret\")",
    };
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(templates); t += 1)
    {
        char path[PATH_MAX];
        char source[512];
        snprintf(path, sizeof(path), "%s/tier1-naked-%s-%u.c", directory, scalar ? "scalar" : "vector", t);
        snprintf(source, sizeof(source), "__attribute__((naked)) long naked_value(void) { %s; }\n", templates[t]);
        const bool written = parse_test_write(path, source, strlen(source));
        CheckLanesTestUnit unit = { 0 };
        if (written)
        {
            check_lanes_test_compile(&unit, &pp, &parser, &identifiers, path, scalar);
        }
        check_lanes_test_expect(tally, written && unit.ready && unit.selected.program.body_count == 1, "the naked fixture selects its one body", path);
        if (written && unit.ready)
        {
            CheckProgramLanes lanes = { .lanes = 1, .tier1 = true };
            CheckLanesTestEmission ordinary = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, &lanes, scalar, false, false);
            check_lanes_test_expect(tally, ordinary.emitted && ordinary.built && lanes.tier1_report.requested == 1 && lanes.tier1_report.functions == 1 &&
                                    lanes.tier1_report.admitted == 0 && lanes.tier1_report.categories[CHECK_TIER1_SUCCESS] == 0 &&
                                    lanes.tier1_report.categories[CHECK_TIER1_UNSUPPORTED] == 1 && lanes.tier1_report.fallbacks == 1,
                                    "an ordinary naked body falls back and is not admitted", path);
            lanes = (CheckProgramLanes){ .lanes = 1, .tier1 = true, .tier1_strict = true };
            CheckLanesTestEmission strict = check_lanes_test_emit(&unit, &parser, &pp, &identifiers, &lanes, scalar, false, false);
            check_lanes_test_expect(tally, !strict.emitted && strict.rolled_back && strict.tier1_failure.stage == CHECK_TIER1_NAKED &&
                                    strict.tier1_failure.category == CHECK_TIER1_UNSUPPORTED && strict.tier1_failure.cause == CHECK_TIER1_CAUSE_ADMISSION &&
                                    strict.tier1_failure.error == BUSTER_CHECK_TIER1_NAKED && strict.tier1_failure.instruction == UINT32_MAX &&
                                    strict.tier1_failure.fallback == CHECK_TIER1_FALLBACK_FORBIDDEN && lanes.tier1_report.requested == 1 &&
                                    lanes.tier1_report.functions == 1 && lanes.tier1_report.admitted == 0 &&
                                    lanes.tier1_report.categories[CHECK_TIER1_UNSUPPORTED] == 1 && lanes.tier1_report.fallbacks == 0,
                                    "strict naked qualification reports the admission outcome", path);
            free(ordinary.object);
            free(strict.object);
        }
        check_lanes_test_close(&unit);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        unlink(path);
    }
}

static void check_tier1_outcome_tests(CheckLanesTestTally* tally, const char* directory, const char* executable)
{
    const bool ready = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && unwind_initialize() && x86_zen5_initialize();
    check_lanes_test_expect(tally, ready, "initialize Tier 1 outcome regression tables", NULL);
    check_tier1_test_classification(tally);
    char source[PATH_MAX];
    char object[PATH_MAX];
    char errors[PATH_MAX];
    char caller[PATH_MAX];
    char binary[PATH_MAX];
    snprintf(source, sizeof(source), "%s/tier1-outcomes.c", directory);
    snprintf(object, sizeof(object), "%s/tier1-outcomes.o", directory);
    snprintf(errors, sizeof(errors), "%s/tier1-outcomes.errors", directory);
    snprintf(caller, sizeof(caller), "%s/tier1-caller.c", directory);
    snprintf(binary, sizeof(binary), "%s/tier1-native", directory);
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    for (uint32_t mixed = 0; ready && mixed < 2; mixed += 1)
    {
        FILE* file = fopen(source, "w");
        bool written = file != NULL && fputs("long tier1_global=1;\nlong f0(long x){return x+tier1_global;}\n", file) >= 0;
        for (uint32_t f = 1; written && f < 7; f += 1)
        {
            written = fprintf(file, "long f%u(long x){return x+%u;}\n", f, f + 1) > 0;
        }
        written = written && fputs(mixed ? "double f7(double x){return x+1.5;}\n" : "long f7(long x){return x+8;}\n", file) >= 0;
        const bool closed = file != NULL && fclose(file) == 0;
        check_lanes_test_expect(tally, written && closed, "write original Tier 1 coverage fixture", source);
        for (uint32_t scalar = 0; written && closed && scalar < 2; scalar += 1)
        {
            CheckLanesTestUnit unit;
            check_lanes_test_compile(&unit, &pp, &parser, &identifiers, source, scalar != 0);
            check_lanes_test_expect(tally, unit.ready && unit.selected.program.body_count == 8, "both frontend paths select all eight designated bodies", source);
            if (unit.ready)
            {
                if (!mixed)
                {
                    check_tier1_test_rollback(tally, &unit, scalar != 0);
                }
                check_tier1_test_lanes(tally, &unit, &parser, &pp, &identifiers, scalar != 0, mixed != 0);
            }
            check_lanes_test_close(&unit);
            parse_release(&parser);
            pp_release(&pp);
            intern_release(&identifiers);
        }
        // Exercise the public command, including multi-unit option propagation, independently of the in-process
        // fault control. The native oracle is ordinary original C, compiled externally by Clang.
        const char* oracle = mixed ? "long f0(long);double f7(double);int main(void){return f0(41)!=42||f7(1.25)!=2.75;}\n" :
                                    "long f0(long);long f7(long);int main(void){return f0(41)!=42||f7(34)!=42;}\n";
        const bool wrote_caller = parse_test_write(caller, oracle, strlen(oracle));
        for (uint32_t scalar = 0; wrote_caller && scalar < 2; scalar += 1)
        {
            const char* path = scalar != 0 ? "--scalar" : "--verify";
            char* compile[] = { (char*)executable, "--tier1", "--tier1-report", "--tier1-refusals", (char*)path, "--lanes", "3", "-c", source, "-o", object, NULL };
            const int status = check_lanes_test_run(compile, errors);
            check_lanes_test_expect(tally, status == 0, "the public mixed-tier driver produces an object", source);
            char* link[] = { "clang", caller, object, "-o", binary, NULL };
            const int linked = status == 0 ? check_lanes_test_run(link, errors) : -1;
            char* run[] = { binary, NULL };
            check_lanes_test_expect(tally, linked == 0 && check_lanes_test_run(run, errors) == 0, "native results match the independent C oracle", source);
            unlink(binary);
            char* strict[] = { (char*)executable, "--tier1", "--tier1-strict", "--tier1-report", (char*)path, "--lanes", "3", source, caller, "-o", binary, NULL };
            const int strict_status = check_lanes_test_run(strict, errors);
            check_lanes_test_expect(tally, strict_status == (mixed ? 1 : 0) && (mixed ? access(binary, F_OK) != 0 : check_lanes_test_run(run, errors) == 0),
                                    "strict mode propagates to every source unit and publishes no failed executable", source);
            unlink(binary);
            char* strict_gvn[] = { (char*)executable, "--tier1", "--tier1-strict", "--tier1-gvn", (char*)path, "--lanes", "3", source, caller, "-o", binary, NULL };
            const int gvn_status = check_lanes_test_run(strict_gvn, errors);
            check_lanes_test_expect(tally, gvn_status == (mixed ? 1 : 0) && (mixed ? access(binary, F_OK) != 0 : check_lanes_test_run(run, errors) == 0),
                                    "strict qualification also covers the optional value-numbering pass in every source unit", source);
        }
    }
    for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
    {
        check_tier1_test_naked(tally, directory, scalar != 0);
    }
    char* missing[] = { (char*)executable, "--tier1-strict", "-c", source, "-o", object, NULL };
    check_lanes_test_expect(tally, check_lanes_test_run(missing, errors) == 1, "strict option requires an explicitly requested Tier 1 pipeline", NULL);
}
