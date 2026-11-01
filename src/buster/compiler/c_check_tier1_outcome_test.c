// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1054: test-check-lanes --tier1-outcomes (also in the ordinary lane suite). Real C passes through the real pipeline;
// stage handoffs then inject raw errors, not categories or policy decisions. Both paths and 1/2/3/8 lanes must retain
// the original location, refuse internal/resource fallback, and restore output. Native mixed-tier output is checked
// separately: a successful executable alone is never evidence that a designated body compiled with Tier 1.

typedef struct CheckTier1FailureCase CheckTier1FailureCase;
struct CheckTier1FailureCase
{
    CheckTier1Fault fault;
    CheckTier1Category category;
};

static const CheckTier1FailureCase check_tier1_failure_cases[] = {
    { { 3, CHECK_TIER1_LOWERING, CHECK_TIER1_CAUSE_IR, IR_ERROR_UNSUPPORTED, 0x12345678, 0x87654321 }, CHECK_TIER1_UNSUPPORTED },
    { { 3, CHECK_TIER1_SELECTION, CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_UNSUPPORTED, 0x12345678, 0x87654321 }, CHECK_TIER1_UNSUPPORTED },
    { { 3, CHECK_TIER1_RESOURCE, CHECK_TIER1_CAUSE_ARENA, BUSTER_CHECK_TIER1_ARENA_RESERVE, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_LOWERING, CHECK_TIER1_CAUSE_IR, IR_ERROR_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_PROMOTION, CHECK_TIER1_CAUSE_IR, IR_ERROR_LIMIT, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_OPTIMIZATION, CHECK_TIER1_CAUSE_IR, IR_ERROR_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_ANALYSIS, CHECK_TIER1_CAUSE_IR, IR_ERROR_LIMIT, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_ANALYSIS, CHECK_TIER1_CAUSE_DOMINATORS, DOMINATOR_MATRIX_LIMIT, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_SELECTION, CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_MEMORY, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_PUBLICATION, CHECK_TIER1_CAUSE_ARENA, BUSTER_CHECK_TIER1_ARENA_OUTPUT, 0x12345678, 0x87654321 }, CHECK_TIER1_RESOURCE_FAILURE },
    { { 3, CHECK_TIER1_LOWERING, CHECK_TIER1_CAUSE_IR, IR_ERROR_STRUCTURE, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_PROMOTION, CHECK_TIER1_CAUSE_IR, IR_ERROR_PHI, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_OPTIMIZATION, CHECK_TIER1_CAUSE_IR, IR_ERROR_DOMINANCE, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_ANALYSIS, CHECK_TIER1_CAUSE_DOMINATORS, DOMINATOR_INVALID, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_SELECTION, CHECK_TIER1_CAUSE_SELECTION, TIER1_SELECT_ERROR_INVALID, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_ALLOCATION, TIER1_ALLOCATION_CONSTRAINT, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_CFG_ALLOCATION, TIER1_CFG_INVALID, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_FRAME, TIER1_FRAME_REGISTER_CONFLICT, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_EMISSION, CHECK_TIER1_CAUSE_EMISSION, TIER1_EMIT_INVALID, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_PUBLICATION, CHECK_TIER1_CAUSE_PUBLICATION, BUSTER_CHECK_TIER1_FRAME_PUBLICATION, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
    { { 3, CHECK_TIER1_SELECTION, CHECK_TIER1_CAUSE_SELECTION, 0xfedcba98, 0x12345678, 0x87654321 }, CHECK_TIER1_INTERNAL },
};

static bool check_tier1_test_cause(CheckTier1Outcome outcome, const CheckTier1FailureCase* test)
{
    const CheckTier1Fault* fault = &test->fault;
    return outcome.stage == fault->stage && outcome.cause == fault->cause && outcome.error == fault->error &&
           outcome.instruction == fault->instruction && outcome.detail == fault->detail && outcome.category == test->category;
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
            const CheckTier1Outcome outcome = check_tier1_compile(&lane, function, result_class, &program->target, scalar, true, &output, &code, &counts, &test->fault);
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
            const CheckTier1Outcome success = check_tier1_compile(&lane, function, result_class, &program->target, scalar, true, &output, &code, &counts, NULL);
            check_lanes_test_expect(tally, success.category == CHECK_TIER1_SUCCESS && code.count != 0 && counts.records == code.count,
                                    "a refused lane can subsequently publish a genuine Tier 1 function", NULL);
            arena_pop_to(&output, mark);
        }
        const uint64_t capacity = output.capacity;
        output.capacity = mark;
        Tier0Code code = { 0 };
        CheckTier1Counts counts = { 0 };
        const CheckTier1Outcome full = check_tier1_compile(&lane, function, result_class, &program->target, scalar, true, &output, &code, &counts, NULL);
        check_lanes_test_expect(tally, full.stage == CHECK_TIER1_PUBLICATION && full.category == CHECK_TIER1_RESOURCE_FAILURE &&
                                full.error == BUSTER_CHECK_TIER1_ARENA_OUTPUT && output.length == mark && code.records == NULL && counts.records == 0,
                                "real publication exhaustion is a resource error, not Tier 0 coverage", NULL);
        output.capacity = capacity;
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
                    lanes = (CheckProgramLanes){ .lanes = lane_counts[n], .every_lane = true, .tier1 = true, .tier1_strict = strict != 0, .tier1_fault = &test->fault };
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

static void check_tier1_outcome_tests(CheckLanesTestTally* tally, const char* directory, const char* executable)
{
    const bool ready = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize() && unwind_initialize() && x86_zen5_initialize();
    check_lanes_test_expect(tally, ready, "initialize Tier 1 outcome regression tables", NULL);
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
        bool written = file != NULL;
        for (uint32_t f = 0; written && f < 7; f += 1)
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
        }
    }
    char* missing[] = { (char*)executable, "--tier1-strict", "-c", source, "-o", object, NULL };
    check_lanes_test_expect(tally, check_lanes_test_run(missing, errors) == 1, "strict option requires an explicitly requested Tier 1 pipeline", NULL);
}
