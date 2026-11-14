// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #139's acceptance on real C (test-ir-source --passes): every function -c compiles of a set of files, prepared and
// lowered as test-ir-source does them (ir_source_prepare), through each of #139's passes by both of its versions, whose
// results must agree: exactly, or for value numbering semantically, as the issue sets it. Include after
// ir_source_benchmark.c, with the passes, ir_lower_test.c's and ir_optimize_test.c's comparisons.
//
//   build/compiler test-ir-source --passes [preprocessor options] file...
//
// For each function, its lowered form L (both lowerings must agree, as test-ir-source requires) and L promoted, P:
// - SROA on L with Tier 1's options (#1282's simd and flagged), vector and scalar: the same function, or the same
//   refusal; a split function must promote.
// - #866's solver on L: the acyclic numbering against its complete-refinement oracle (IrGvnOptions.complete), which must
//   give the same function, and against the scalar available-expression twin, which must accept the function and remove
//   no more instructions than the partition (it doesn't see cyclic congruences): the semantic comparison, as the solver's
//   guide has it. The numbering's result must promote.
// - The optimizer on P with SCCP alone, dead-code removal alone, CFG cleanup alone, the three, and the three with Tier 1's
//   layout and identities (c_check_tier1.c's set), forced vector and forced scalar: the same function.
// - LICM on P, vector and scalar: the same function, or the same refusal.
// For each caller among them, the inliner on a unit of it and its direct callees among the file's functions, as the C
// driver's expansion takes one (every function of internal linkage and #2541's general splice on, so that every such call is
// a candidate), vector and scalar: the same decisions and the same caller, whose expansion must promote.
//
// A refusal both versions give alike (IR_ERROR_UNSUPPORTED: vectors for GVN and LICM, labels and the dynamic stack for
// some; and the inliner's IR_ERROR_TYPE for a unit with an unprototyped call of one of its functions) is counted, not failed; any other error, a disagreement, or a result that doesn't promote fails the command, and
// each failure prints a line naming the file, function, pass and errors. The totals line counts, per pass, the functions
// it ran on, those it changed, those both versions refused, and its total effect. This is a test outside the compile
// path, with no vector path of its own; it needs no hardware counter and runs on baseline hosts too.

typedef enum IrSourcePass
{
    IR_SOURCE_PASS_SROA,
    IR_SOURCE_PASS_GVN,
    IR_SOURCE_PASS_SCCP,
    IR_SOURCE_PASS_DCE,
    IR_SOURCE_PASS_CFG,
    IR_SOURCE_PASS_OPTIMIZER,
    IR_SOURCE_PASS_TIER1,
    IR_SOURCE_PASS_LICM,
    IR_SOURCE_PASS_INLINE,
    IR_SOURCE_PASS_COUNT,
} IrSourcePass;

static const char* const ir_source_pass_names[IR_SOURCE_PASS_COUNT] = {
    "sroa",
    "gvn",
    "sccp",
    "dce",
    "cfg",
    "optimizer",
    "tier1",
    "licm",
    "inline",
};

// The optimizer's pass bits of each of its rows (IR_SOURCE_PASS_SCCP to IR_SOURCE_PASS_TIER1).
static const uint32_t ir_source_pass_optimizer[] = {
    BUSTER_IR_OPTIMIZE_SCCP,
    BUSTER_IR_OPTIMIZE_DCE,
    BUSTER_IR_OPTIMIZE_CFG,
    BUSTER_IR_OPTIMIZE_SCCP | BUSTER_IR_OPTIMIZE_DCE | BUSTER_IR_OPTIMIZE_CFG,
    BUSTER_IR_OPTIMIZE_SCCP | BUSTER_IR_OPTIMIZE_DCE | BUSTER_IR_OPTIMIZE_CFG | BUSTER_IR_OPTIMIZE_LAYOUT | BUSTER_IR_OPTIMIZE_IDENTITY,
};
_Static_assert(BUSTER_ARRAY_LENGTH(ir_source_pass_optimizer) == IR_SOURCE_PASS_TIER1 - IR_SOURCE_PASS_SCCP + 1, "a mask for each optimizer row");

typedef struct IrSourcePassTotals IrSourcePassTotals;
struct IrSourcePassTotals
{
    uint64_t files;
    uint64_t left_out;
    uint64_t failed_files;
    uint64_t functions;
    // Functions whose lowerings disagree or that don't promote: test-ir-source's failures, which fail this too.
    uint64_t unprepared;
    // Per pass: functions it ran on, changed, refused by both versions, failed (an error, a disagreement or no promotion).
    uint64_t ran[IR_SOURCE_PASS_COUNT];
    uint64_t changed[IR_SOURCE_PASS_COUNT];
    uint64_t refused[IR_SOURCE_PASS_COUNT];
    uint64_t failed[IR_SOURCE_PASS_COUNT];
    // Per pass, its total effect: SROA's split slots, GVN's removed instructions (the partition's), the optimizer rows'
    // removed instructions, LICM's moved values and the inliner's inlined calls; and the scalar GVN twin's removals.
    uint64_t effect[IR_SOURCE_PASS_COUNT];
    uint64_t gvn_scalar_removed;
};

// Whether two functions are the same column for column, with every side table: the lowering's comparison and the
// optimizer's, which each read tables the other doesn't.
static bool ir_source_passes_equal(const IrFunction* a, const IrFunction* b)
{
    return ir_lower_test_equal(a, b) && ir_optimize_test_equal(a, b);
}

// One pass's two versions on a function: both refused alike (counted), or both succeeded with `same` results (whose
// promotion, where the pass runs before it, `promotes` says); else a failure, printed. Returns whether it failed.
static bool ir_source_passes_record(IrSourcePassTotals* totals, IrSourcePass pass, const char* file, const uint8_t* function, IrCheck vector, IrCheck scalar, bool same, bool promotes,
                                    bool changed, uint64_t effect)
{
    // The inliner refuses a unit whose direct call's signature isn't its callee's (IR_ERROR_TYPE: an unprototyped
    // call is variadic, its callee not), as the driver's expansion then keeps the calls: both versions alike, at the same
    // node, is agreement too.
    const bool alike = vector.error == scalar.error && vector.instruction == scalar.instruction && vector.detail == scalar.detail;
    const bool refused = (vector.error == IR_ERROR_UNSUPPORTED && scalar.error == IR_ERROR_UNSUPPORTED) || (pass == IR_SOURCE_PASS_INLINE && vector.error == IR_ERROR_TYPE && alike);
    const bool succeeded = vector.error == IR_OK && scalar.error == IR_OK;
    const bool failed = !refused && !(succeeded && same && promotes);
    totals->ran[pass] += 1;
    totals->refused[pass] += refused;
    totals->failed[pass] += failed;
    totals->changed[pass] += succeeded && changed;
    totals->effect[pass] += succeeded ? effect : 0;
    if (failed || (refused && pass == IR_SOURCE_PASS_INLINE))
    {
        printf("ir-source-passes: %s file=%s function=%s pass=%s vector_error=%s scalar_error=%s same=%u promotes=%u instruction=%u/%u\n", failed ? "failed" : "refused", file, (const char*)function,
               ir_source_pass_names[pass], ir_source_errors[vector.error <= IR_ERROR_UNSUPPORTED ? vector.error : IR_ERROR_IO],
               ir_source_errors[scalar.error <= IR_ERROR_UNSUPPORTED ? scalar.error : IR_ERROR_IO], same, promotes, vector.instruction, scalar.instruction);
    }
    return failed;
}

// The passes on one function of `file` whose lowering is `lowered` and promotion `promoted`; every pass's outputs go to
// `output` and `twin` from their marks, which this restores.
static void ir_source_passes_function(IrSourcePassTotals* totals, const char* file, const IrSourceFunction* source, const IrFunction* lowered, const IrFunction* promoted, Arena* output,
                                      Arena* twin, Arena* promotion, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t twin_mark = twin->length;
    const uint64_t promotion_mark = promotion->length;
    const uint8_t* name = source->name;
    // SROA, with --tier1's options, before promotion.
    {
        IrSroaOptions options = ir_sroa_options();
        options.simd = true;
        options.flagged = true;
        IrSroaResult vector = { 0 };
        IrSroaResult scalar = { 0 };
        const IrCheck vector_check = ir_optimize_sroa(lowered, output, scratch, options, &vector);
        options.scalar = true;
        const IrCheck scalar_check = ir_optimize_sroa(lowered, twin, scratch, options, &scalar);
        const bool succeeded = vector_check.error == IR_OK && scalar_check.error == IR_OK;
        const bool same = succeeded && vector.split_slots == scalar.split_slots && vector.fields == scalar.fields && ir_source_passes_equal(&vector.function, &scalar.function);
        IrFunction split = { 0 };
        const bool promotes = !same || ssa_ir_promote(&vector.function, promotion, scratch, &split).error == IR_OK;
        ir_source_passes_record(totals, IR_SOURCE_PASS_SROA, file, name, vector_check, scalar_check, same, promotes, vector.split_slots != 0, vector.split_slots);
        arena_pop_to(output, output_mark);
        arena_pop_to(twin, twin_mark);
        arena_pop_to(promotion, promotion_mark);
    }
    // #866's solver, before promotion: the acyclic numbering, its exact oracle and the scalar semantic twin.
    {
        IrGvnOptions options = ir_gvn_options();
        IrGvnResult vector = { 0 };
        IrGvnResult complete = { 0 };
        IrGvnResult scalar = { 0 };
        const IrCheck vector_check = ir_optimize_gvn(lowered, output, scratch, options, &vector);
        options.complete = true;
        const IrCheck complete_check = ir_optimize_gvn(lowered, twin, scratch, options, &complete);
        const bool exact = vector_check.error == IR_OK && complete_check.error == IR_OK && ir_source_passes_equal(&vector.function, &complete.function);
        arena_pop_to(twin, twin_mark);
        options.complete = false;
        options.scalar = true;
        const IrCheck scalar_check = ir_optimize_gvn(lowered, twin, scratch, options, &scalar);
        const bool semantic = exact && scalar_check.error == IR_OK && scalar.removed <= vector.removed;
        IrFunction numbered = { 0 };
        const bool promotes = !semantic || ssa_ir_promote(&vector.function, promotion, scratch, &numbered).error == IR_OK;
        // The complete oracle must refuse what the numbering refuses, and take what it takes: where they differ, its
        // check stands for the twin's, so that the record fails.
        const IrCheck oracle = complete_check.error == vector_check.error ? scalar_check : complete_check;
        ir_source_passes_record(totals, IR_SOURCE_PASS_GVN, file, name, vector_check, oracle, semantic, promotes, vector.removed != 0, vector.removed);
        totals->gvn_scalar_removed += vector_check.error == IR_OK && scalar_check.error == IR_OK ? scalar.removed : 0;
        arena_pop_to(output, output_mark);
        arena_pop_to(twin, twin_mark);
        arena_pop_to(promotion, promotion_mark);
    }
    // The optimizer's rows on the promoted function.
    for (uint32_t row = 0; row < BUSTER_ARRAY_LENGTH(ir_source_pass_optimizer); row += 1)
    {
        IrOptimizeOptions options = ir_optimize_options();
        options.passes = ir_source_pass_optimizer[row];
        options.path = BUSTER_IR_OPTIMIZE_VECTOR;
        IrOptimizeResult vector = { 0 };
        IrOptimizeResult scalar = { 0 };
        const IrCheck vector_check = ir_optimize(promoted, output, scratch, options, &vector);
        options.path = BUSTER_IR_OPTIMIZE_SCALAR;
        const IrCheck scalar_check = ir_optimize(promoted, twin, scratch, options, &scalar);
        const bool same = vector_check.error == IR_OK && scalar_check.error == IR_OK && vector.folded == scalar.folded &&
                          vector.removed_instructions == scalar.removed_instructions && vector.removed_blocks == scalar.removed_blocks &&
                          ir_source_passes_equal(&vector.function, &scalar.function);
        const bool changed = vector.folded != 0 || vector.removed_instructions != 0 || vector.removed_blocks != 0;
        ir_source_passes_record(totals, (IrSourcePass)(IR_SOURCE_PASS_SCCP + row), file, name, vector_check, scalar_check, same, true, changed, vector.removed_instructions);
        arena_pop_to(output, output_mark);
        arena_pop_to(twin, twin_mark);
    }
    // LICM on the promoted function.
    {
        IrLicmOptions options = ir_licm_options();
        IrLicmResult vector = { 0 };
        IrLicmResult scalar = { 0 };
        const IrCheck vector_check = ir_optimize_licm(promoted, output, scratch, options, &vector);
        options.scalar = true;
        const IrCheck scalar_check = ir_optimize_licm(promoted, twin, scratch, options, &scalar);
        const bool same = vector_check.error == IR_OK && scalar_check.error == IR_OK && vector.moved == scalar.moved && ir_source_passes_equal(&vector.function, &scalar.function);
        ir_source_passes_record(totals, IR_SOURCE_PASS_LICM, file, name, vector_check, scalar_check, same, true, vector.moved != 0, vector.moved);
        arena_pop_to(output, output_mark);
        arena_pop_to(twin, twin_mark);
    }
}

// A function's index and symbol, for the inliner's symbol-sorted unit.
typedef struct IrSourcePassMember IrSourcePassMember;
struct IrSourcePassMember
{
    uint32_t symbol;
    uint32_t index;
};

static int ir_source_passes_member_order(const void* left, const void* right)
{
    const IrSourcePassMember* a = left;
    const IrSourcePassMember* b = right;
    return a->symbol < b->symbol ? -1 : a->symbol > b->symbol ? 1 : a->index < b->index ? -1 : a->index > b->index;
}

// The inliner on each caller of `file`'s functions as the C driver's expansion takes it (c_check_tier1_inline.c): a unit
// of the caller and its direct callees among the file's functions (those of `prepared`), at most
// BUSTER_IR_SOURCE_PASSES_CALLEES of them, the caller alone rewritten (IrInlineUnit.only), every function of internal
// linkage and #2541's general splice on; vector and scalar: the same decisions and functions. A caller with no such
// callee isn't run. The units' tables go to `tables`, which the inliner requires apart from its output's tail and its
// scratch.
#define BUSTER_IR_SOURCE_PASSES_CALLEES 64u
static void ir_source_passes_unit(IrSourcePassTotals* totals, const char* file, const IrSourceFile* source, const IrFunction* functions, const bool* prepared, Arena* tables, Arena* output,
                                  Arena* twin, Arena* scratch)
{
    const uint64_t tables_mark = tables->length;
    // The file's prepared functions by symbol, for the callers' lookups.
    uint32_t count = 0;
    for (uint32_t f = 0; f < source->count; f += 1)
    {
        count += prepared[f];
    }
    IrSourcePassMember* members = count > 1 ? BUSTER_ARENA_PUSH_ARRAY(tables, IrSourcePassMember, count) : 0;
    IrSourcePassMember* unit_members = members != 0 ? BUSTER_ARENA_PUSH_ARRAY(tables, IrSourcePassMember, BUSTER_IR_SOURCE_PASSES_CALLEES + 1) : 0;
    IrFunction* unit_functions = unit_members != 0 ? BUSTER_ARENA_PUSH_ARRAY(tables, IrFunction, BUSTER_IR_SOURCE_PASSES_CALLEES + 1) : 0;
    uint32_t* symbols = unit_functions != 0 ? BUSTER_ARENA_PUSH_ARRAY(tables, uint32_t, BUSTER_IR_SOURCE_PASSES_CALLEES + 1) : 0;
    uint8_t* flags = symbols != 0 ? BUSTER_ARENA_PUSH_ARRAY(tables, uint8_t, BUSTER_IR_SOURCE_PASSES_CALLEES + 1) : 0;
    if (flags != 0)
    {
        uint32_t at = 0;
        for (uint32_t f = 0; f < source->count; f += 1)
        {
            if (prepared[f])
            {
                members[at] = (IrSourcePassMember){ .symbol = source->functions[f].symbol, .index = f };
                at += 1;
            }
        }
        qsort(members, count, sizeof(IrSourcePassMember), ir_source_passes_member_order);
    }
    for (uint32_t caller = 0; flags != 0 && caller < source->count; caller += 1)
    {
        const IrFunction* body = &functions[caller];
        uint32_t size = 1;
        bool fits = prepared[caller];
        unit_members[0] = (IrSourcePassMember){ .symbol = source->functions[caller].symbol, .index = caller };
        for (uint32_t node = 0; fits && node < body->instruction_count; node += 1)
        {
            // A direct call of another of the file's prepared functions, of one symbol (a unit's target versions aren't).
            const bool site = body->op[node] == IR_CALL && body->calls[body->b[node]].row == 0 && body->calls[body->b[node]].body == 0;
            const uint32_t symbol = site ? body->a[node] : UINT32_MAX;
            uint32_t low = 0;
            uint32_t high = count;
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = members[middle].symbol < symbol ? middle + 1 : low;
                high = members[middle].symbol < symbol ? high : middle;
            }
            const bool found = symbol != UINT32_MAX && low < count && members[low].symbol == symbol;
            const bool single = found && (low + 1 == count || members[low + 1].symbol != symbol);
            bool known = false;
            for (uint32_t k = 0; found && k < size; k += 1)
            {
                known = known || unit_members[k].symbol == symbol;
            }
            fits = !found || known || (single && size <= BUSTER_IR_SOURCE_PASSES_CALLEES);
            if (fits && found && !known)
            {
                unit_members[size] = members[low];
                size += 1;
            }
        }
        if (fits && size > 1)
        {
            qsort(unit_members, size, sizeof(IrSourcePassMember), ir_source_passes_member_order);
            uint32_t position = 0;
            uint32_t symbol_count = 0;
            for (uint32_t k = 0; k < size; k += 1)
            {
                const uint32_t index = unit_members[k].index;
                position = index == caller ? k : position;
                unit_functions[k] = functions[index];
                symbols[k] = unit_members[k].symbol;
                flags[k] = BUSTER_IR_INLINE_INTERNAL | (functions[index].variadic ? BUSTER_IR_INLINE_VARIADIC : 0);
                symbol_count = source->functions[index].symbol_count > symbol_count ? source->functions[index].symbol_count : symbol_count;
            }
            const IrInlineUnit unit = {
                .functions = unit_functions,
                .symbols = symbols,
                .flags = flags,
                .count = size,
                .symbol_count = symbol_count,
                .only = position + 1,
            };
            const uint64_t output_mark = output->length;
            const uint64_t twin_mark = twin->length;
            IrInlineOptions options = ir_inline_options();
            options.control_flow = true;
            IrInlineResult vector = { 0 };
            IrInlineResult scalar = { 0 };
            uint32_t vector_failed = BUSTER_IR_NONE;
            uint32_t scalar_failed = BUSTER_IR_NONE;
            const IrCheck vector_check = ir_optimize_inline(&unit, output, scratch, options, &vector, &vector_failed);
            options.scalar = true;
            const IrCheck scalar_check = ir_optimize_inline(&unit, twin, scratch, options, &scalar, &scalar_failed);
            bool same = vector_check.error == IR_OK && scalar_check.error == IR_OK && vector.function_count == scalar.function_count && vector.call_count == scalar.call_count &&
                        vector.inlined_calls == scalar.inlined_calls &&
                        (vector.call_count == 0 || (memcmp(vector.decisions, scalar.decisions, vector.call_count) == 0 &&
                                                    memcmp(vector.callees, scalar.callees, (uint64_t)vector.call_count * sizeof(uint32_t)) == 0 &&
                                                    memcmp(vector.costs, scalar.costs, (uint64_t)vector.call_count * sizeof(uint32_t)) == 0));
            same = same && ir_source_passes_equal(&vector.functions[position], &scalar.functions[position]);
            // An expansion promotes, as the driver's promotion takes it.
            IrFunction promoted = { 0 };
            const bool promotes = !same || vector.inlined_calls == 0 || ssa_ir_promote(&vector.functions[position], twin, scratch, &promoted).error == IR_OK;
            const IrCheck refusal = vector_failed == scalar_failed ? scalar_check : vector_check;
            ir_source_passes_record(totals, IR_SOURCE_PASS_INLINE, file, source->functions[caller].name, vector_check, refusal, same, promotes, vector.inlined_calls != 0,
                                    vector.inlined_calls);
            arena_pop_to(output, output_mark);
            arena_pop_to(twin, twin_mark);
        }
    }
    arena_pop_to(tables, tables_mark);
}

static int run_ir_source_passes_tests(int count, char** arguments)
{
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
        }
        else if (strcmp(arguments[i], "--no-default-config") != 0)
        {
            // An option and its value, which pp_parse_options reads with each file, as test-ir-source takes them.
            options[option_count++] = arguments[i];
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
            }
        }
    }
    valid = valid && file_count != 0;
    Arena lowered_arena = arena_reserve(UINT64_C(1) << 36);
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    Arena functions_arena = arena_reserve(UINT64_C(1) << 36);
    Arena twin_lowering = arena_reserve(UINT64_C(1) << 34);
    Arena promotion = arena_reserve(UINT64_C(1) << 34);
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena twin = arena_reserve(UINT64_C(1) << 34);
    Arena scratch = arena_reserve(UINT64_C(1) << 35);
    valid = valid && lowered_arena.base != 0 && arena.base != 0 && functions_arena.base != 0 && twin_lowering.base != 0 && promotion.base != 0 && output.base != 0 &&
            twin.base != 0 && scratch.base != 0;
    IrSourcePassTotals totals = { 0 };
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        IrSourceFile file;
        ir_source_prepare(&file, files[i], options, option_count, &lowered_arena, &arena, false, false);
        if (file.status == IR_SOURCE_PREPARED)
        {
            // Every function lowered by both paths, which must agree, the vector one's kept for the unit's inlining.
            IrFunction* functions = BUSTER_ARENA_PUSH_ARRAY(&arena, IrFunction, (uint64_t)file.count + 1);
            bool* prepared = BUSTER_ARENA_PUSH_ARRAY(&arena, bool, (uint64_t)file.count + 1);
            const uint64_t before = totals.functions;
            for (uint32_t f = 0; functions != 0 && prepared != 0 && f < file.count; f += 1)
            {
                const IrSourceFunction* source = &file.functions[f];
                IrFunction scalar = { 0 };
                IrFunction promoted = { 0 };
                const IrCheck vector_check = ir_lower_typed_target(source->function, source->result_type, source->symbol_count, source->target_features, &functions_arena, &scratch, &functions[f]);
                const IrCheck scalar_check = ir_lower_typed_target_scalar(source->function, source->result_type, source->symbol_count, source->target_features, &twin_lowering, &scratch, &scalar);
                const bool agree = vector_check.error == IR_OK && scalar_check.error == IR_OK && ir_lower_test_equal(&functions[f], &scalar);
                const bool promotes = agree && ssa_ir_promote(&functions[f], &promotion, &scratch, &promoted).error == IR_OK;
                prepared[f] = promotes;
                totals.unprepared += !promotes;
                if (!promotes)
                {
                    printf("ir-source-passes: file=%s function=%s unprepared (lowerings disagree, or no promotion)\n", files[i], (const char*)source->name);
                }
                else
                {
                    ir_source_passes_function(&totals, files[i], source, &functions[f], &promoted, &output, &twin, &promotion, &scratch);
                }
                totals.functions += 1;
                arena_pop_to(&twin_lowering, 0);
                arena_pop_to(&promotion, 0);
            }
            if (functions != 0 && prepared != 0)
            {
                ir_source_passes_unit(&totals, files[i], &file, functions, prepared, &arena, &output, &twin, &scratch);
            }
            else
            {
                totals.failed_files += 1;
                printf("ir-source-passes: file=%s failed=\"no room for its functions\"\n", files[i]);
            }
            totals.files += 1;
            printf("ir-source-passes: file=%s functions=%" PRIu64 "\n", files[i], totals.functions - before);
        }
        else
        {
            totals.left_out += file.status == IR_SOURCE_LEFT_OUT;
            totals.failed_files += file.status == IR_SOURCE_FAILED;
            printf("ir-source-passes: file=%s %s=\"%s\"\n", files[i], file.status == IR_SOURCE_LEFT_OUT ? "left_out" : "failed", file.reason);
        }
        fflush(stdout);
        arena_pop_to(&functions_arena, 0);
        arena_pop_to(&lowered_arena, 0);
        arena_pop_to(&arena, 0);
    }
    uint64_t failed = 0;
    if (valid)
    {
        printf("ir-source-passes: totals files=%" PRIu64 " left_out=%" PRIu64 " failed=%" PRIu64 " functions=%" PRIu64 " unprepared=%" PRIu64 "\n", totals.files,
               totals.left_out, totals.failed_files, totals.functions, totals.unprepared);
        for (uint32_t pass = 0; pass < IR_SOURCE_PASS_COUNT; pass += 1)
        {
            printf("ir-source-passes: pass=%s ran=%" PRIu64 " changed=%" PRIu64 " refused=%" PRIu64 " failed=%" PRIu64 " effect=%" PRIu64 "\n", ir_source_pass_names[pass],
                   totals.ran[pass], totals.changed[pass], totals.refused[pass], totals.failed[pass], totals.effect[pass]);
            failed += totals.failed[pass];
        }
        printf("ir-source-passes: gvn-scalar removed=%" PRIu64 " (the partition's: %" PRIu64 ")\n", totals.gvn_scalar_removed, totals.effect[IR_SOURCE_PASS_GVN]);
    }
    else
    {
        fprintf(stderr, "usage: compiler test-ir-source --passes [preprocessor options] file...\n");
    }
    const bool passed = valid && failed == 0 && totals.failed_files == 0 && totals.unprepared == 0 && totals.functions != 0;
    printf("ir-source-passes: %s\n", passed ? "passed" : "FAILED");
    arena_release(&scratch);
    arena_release(&twin);
    arena_release(&output);
    arena_release(&promotion);
    arena_release(&twin_lowering);
    arena_release(&functions_arena);
    arena_release(&arena);
    arena_release(&lowered_arena);
    free(files);
    free(options);
    return passed ? 0 : 1;
}
