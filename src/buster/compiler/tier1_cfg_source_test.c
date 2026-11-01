// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier1-cfg-source (#140's corpus validity): every function -c compiles of the files, prepared, lowered and
// promoted as test-ir-source checks them, becomes tier1_cfg_ir.c's allocation problem, which both of tier1_cfg.c's
// paths allocate and tier1_cfg_verify checks. A function with a value the model has no class for is counted apart;
// any other refusal, a result the checker rejects, or paths that disagree on whether the problem allocates fail the
// command. Per path the report gives the intervals and the share the scan took, the values given a slot, the stores
// and reloads (fragment transitions, definitions into slots and edge moves, to and from slots), the
// rematerializations, the moves at edges and at splits, the split edges, and the time of each path's one run, which is
// no measurement (benchmark-tier1-cfg-source is one). Beside them, the frame's memory traffic: the IR's loads and
// stores of its slots, with each path's stores and reloads added. Tier 0's code for the same functions comes from its
// own tools (-c's objects, benchmark-backend-source): ir_source_prepare's emission initializes Tier 0's tables in the
// checker's unit, its home, and this command runs in the backend's, whose copies a call here would split from them.
// Include after ir_source_test.c and tier1_cfg_ir.c.
//
//   build/compiler test-tier1-cfg-source [--simd] [--print NAME] [preprocessor options] file...
//
// --print NAME prints that function's allocation problem's sizes and each path's counts. --simd allocates a function
// with vectors, which the SSA promoter refuses (test-ir-source --simd checks that it does), in its lowered form, which is
// SSA too (its locals in slots), so that #732's vector and mask values reach the vector and opmask classes (#179): the
// report then counts its masks, which must be some. A function whose only SIMD values are masks the promoter takes, and
// it is promoted and allocated as any other: refused, it fails the run. This is a test outside the compile path, with
// no vector path of its own.

#include <time.h>

typedef struct Tier1CfgSourcePath Tier1CfgSourcePath;
struct Tier1CfgSourcePath
{
    uint64_t allocated;
    uint64_t constraints;
    uint64_t failed;
    uint64_t rejected;
    uint64_t intervals;
    uint64_t vector_intervals;
    uint64_t scalar_intervals;
    uint64_t spilled;
    uint64_t stores;
    uint64_t reloads;
    uint64_t recomputed;
    uint64_t moves;
    uint64_t split_moves;
    uint64_t split_edges;
    uint64_t temp_slots;
    uint64_t nanoseconds;
    // The opmask class's: masks given a slot, and moves of masks.
    uint64_t mask_spilled;
    uint64_t mask_moves;
};

typedef struct Tier1CfgSourceTotals Tier1CfgSourceTotals;
struct Tier1CfgSourceTotals
{
    uint64_t files;
    uint64_t left_out;
    uint64_t failed_files;
    uint64_t functions;
    uint64_t unsupported;
    uint64_t unpromoted;
    uint64_t malformed;
    uint64_t disagreed;
    uint64_t instructions;
    uint64_t values;
    uint64_t blocks;
    uint64_t edges;
    uint64_t phis;
    // With --simd: the functions allocated in their lowered form, and their problems' mask values.
    uint64_t simd;
    uint64_t mask_values;
    // The promoted IR's loads and stores of its slots, beside each path's stores and reloads: the frame's traffic.
    uint64_t slot_accesses;
    Tier1CfgSourcePath paths[2];
};

// Whether an IR address is a slot's: a slot address, or an address computed from one.
static bool tier1_cfg_source_slot_address(const IrFunction* function, uint32_t value)
{
    const uint8_t op = function->op[value];
    return op == IR_STACK_ADDR || (op == IR_ADDRESS && function->op[function->a[value]] == IR_STACK_ADDR);
}

// The promoted function's loads and stores of its slots.
static uint64_t tier1_cfg_source_slot_accesses(const IrFunction* function)
{
    uint64_t accesses = 0;
    for (uint32_t i = 0; i < function->instruction_count; i += 1)
    {
        const uint8_t op = function->op[i];
        accesses += (op == IR_LOAD || op == IR_STORE) && tier1_cfg_source_slot_address(function, function->a[i]);
    }
    return accesses;
}


static uint64_t tier1_cfg_source_clock(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

// One path's counts of a successful allocation.
static void tier1_cfg_source_count(Tier1CfgSourcePath* path, const Tier1CfgInput* input, const Tier1CfgAllocation* result)
{
    const Tier1Allocation* allocation = &result->allocation;
    path->intervals += result->intervals.count;
    path->vector_intervals += allocation->vector_intervals;
    path->scalar_intervals += allocation->scalar_intervals;
    path->spilled += allocation->spill_count;
    path->moves += result->move_count;
    path->split_moves += result->move_count - result->edge_move_offsets[input->edge_count];
    path->split_edges += result->split_edges;
    path->temp_slots += result->temp_slots != 0;
    for (uint32_t i = 0; i < result->intervals.count; i += 1)
    {
        const uint32_t first = allocation->fragment_offsets[i];
        const uint32_t value = result->intervals.values[i];
        // A definition into a slot is made in the scratch register and stored.
        path->stores += allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_SPILL && result->intervals.starts[i] == input->definitions[value];
        for (uint32_t f = first + 1; f < allocation->fragment_offsets[i + 1]; f += 1)
        {
            path->stores += allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_SPILL;
            path->reloads += allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && allocation->kinds[f - 1] == BUSTER_TIER1_ALLOCATE_SPILL;
            path->recomputed += allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && allocation->kinds[f - 1] == BUSTER_TIER1_ALLOCATE_REMATERIALIZE;
        }
    }
    for (uint32_t m = 0; m < result->move_count; m += 1)
    {
        const uint32_t from = result->move_sources[m];
        const uint32_t to = result->move_destinations[m];
        path->recomputed += from == BUSTER_TIER1_CFG_REMATERIALIZE;
        path->stores += from != BUSTER_TIER1_CFG_REMATERIALIZE && to >= BUSTER_TIER1_CFG_SPILL;
        path->reloads += from != BUSTER_TIER1_CFG_REMATERIALIZE && from >= BUSTER_TIER1_CFG_SPILL;
        path->mask_moves += input->classes[result->move_values[m]] == BUSTER_TIER1_ALLOCATE_MASK;
    }
    for (uint32_t k = 0; k < allocation->spill_count; k += 1)
    {
        path->mask_spilled += input->classes[allocation->spill_values[k]] == BUSTER_TIER1_ALLOCATE_MASK;
    }
}

static void tier1_cfg_source_check(Tier1CfgSourceTotals* totals, const IrSourceFile* file, const char* name, const char* print, bool simd, Arena* lowered_output, Arena* promoted_output, Arena* problem_arena, Arena* output, Arena* scratch)
{
    uint32_t failures = 0;
    for (uint32_t f = 0; f < file->count; f += 1)
    {
        const IrSourceFunction* source = &file->functions[f];
        const uint64_t lowered_mark = lowered_output->length;
        const uint64_t promoted_mark = promoted_output->length;
        const uint64_t problem_mark = problem_arena->length;
        IrFunction lowered = { 0 };
        IrFunction promoted = { 0 };
        const IrCheck lowering = ir_lower_typed_target(source->function, source->result_type, source->symbol_count, source->target_features, lowered_output, scratch, &lowered);
        const IrCheck refused = lowering.error == IR_OK ? ssa_ir_promote(&lowered, promoted_output, scratch, &promoted) : lowering;
        // With --simd, a function the promoter refuses for its vectors is allocated as lowered; one of masks alone, which
        // the promoter takes (#179), must promote.
        const bool lowered_form = simd && lowering.error == IR_OK && refused.error == IR_ERROR_UNSUPPORTED && ir_function_has_vectors(&lowered);
        const IrCheck promotion = lowered_form ? lowering : refused;
        const IrFunction* allocated = lowered_form ? &lowered : &promoted;
        totals->simd += lowered_form;
        Tier1CfgIrProblem problem = { 0 };
        const Tier1CfgIrStatus adapted = promotion.error == IR_OK ? tier1_cfg_from_ir(allocated, problem_arena, scratch, &problem) : TIER1_CFG_IR_INVALID;
        totals->unpromoted += promotion.error != IR_OK;
        totals->unsupported += adapted == TIER1_CFG_IR_UNSUPPORTED;
        totals->malformed += promotion.error == IR_OK && adapted != TIER1_CFG_IR_OK && adapted != TIER1_CFG_IR_UNSUPPORTED;
        bool failed = promotion.error != IR_OK || (adapted != TIER1_CFG_IR_OK && adapted != TIER1_CFG_IR_UNSUPPORTED);
        if (adapted == TIER1_CFG_IR_OK)
        {
            const Tier1CfgInput* input = &problem.input;
            totals->slot_accesses += tier1_cfg_source_slot_accesses(allocated);
            for (uint32_t v = 0; v < input->value_count; v += 1)
            {
                totals->mask_values += input->classes[v] == BUSTER_TIER1_ALLOCATE_MASK;
            }
            totals->instructions += problem.instructions;
            totals->values += input->value_count;
            totals->blocks += input->block_count;
            totals->edges += input->edge_count;
            totals->phis += input->phi_count;
            Tier1CfgError errors[2] = {
                TIER1_CFG_INVALID,
                TIER1_CFG_INVALID,
            };
            for (uint32_t p = 0; p < 2; p += 1)
            {
                Tier1CfgSourcePath* path = &totals->paths[p];
                const uint64_t output_mark = output->length;
                Tier1CfgAllocation result = { 0 };
                const uint64_t start = tier1_cfg_source_clock();
                errors[p] = p == 0 ? tier1_cfg_allocate(input, output, scratch, &result) : tier1_cfg_allocate_scalar(input, output, scratch, &result);
                path->nanoseconds += tier1_cfg_source_clock() - start;
                path->constraints += errors[p] == TIER1_CFG_CONSTRAINT;
                path->failed += errors[p] != TIER1_CFG_OK && errors[p] != TIER1_CFG_CONSTRAINT;
                Tier1CfgCheck check = { 0 };
                const bool verified = errors[p] == TIER1_CFG_OK && tier1_cfg_verify(input, &result, scratch, &check);
                path->rejected += errors[p] == TIER1_CFG_OK && !verified;
                path->allocated += verified;
                if (verified)
                {
                    tier1_cfg_source_count(path, input, &result);
                }
                if (errors[p] == TIER1_CFG_CONSTRAINT)
                {
                    printf("tier1-cfg-source: file=%s function=%s path=%s refused=constraint\n", name, source->name, p == 0 ? "vector" : "scalar");
                }
                else if (errors[p] != TIER1_CFG_OK)
                {
                    printf("tier1-cfg-source: file=%s function=%s path=%s error=%u\n", name, source->name, p == 0 ? "vector" : "scalar", (unsigned)errors[p]);
                }
                else if (errors[p] == TIER1_CFG_OK && !verified)
                {
                    printf("tier1-cfg-source: file=%s function=%s path=%s rejected=\"%s\" block=%u value=%u position=%u\n", name, source->name, p == 0 ? "vector" : "scalar", check.reason, check.block, check.value, check.position);
                }
                if (print != 0 && strcmp(print, (const char*)source->name) == 0)
                {
                    printf("tier1-cfg-source: %s path=%s blocks=%u edges=%u values=%u phis=%u positions=%u error=%u intervals=%u spills=%u moves=%u split_edges=%u\n", source->name, p == 0 ? "vector" : "scalar", input->block_count, input->edge_count, input->value_count, input->phi_count, input->position_count, (unsigned)errors[p], result.intervals.count, result.allocation.spill_count, result.move_count, result.split_edges);
                }
                failed = failed || (errors[p] != TIER1_CFG_OK && errors[p] != TIER1_CFG_CONSTRAINT) || (errors[p] == TIER1_CFG_OK && !verified);
                arena_pop_to(output, output_mark);
            }
            totals->disagreed += (errors[0] == TIER1_CFG_OK) != (errors[1] == TIER1_CFG_OK);
            failed = failed || (errors[0] == TIER1_CFG_OK) != (errors[1] == TIER1_CFG_OK);
        }
        if (promotion.error != IR_OK || (adapted != TIER1_CFG_IR_OK && adapted != TIER1_CFG_IR_UNSUPPORTED))
        {
            printf("tier1-cfg-source: file=%s function=%s %s ir_error=%u adapter=%u\n", name, source->name, promotion.error != IR_OK ? "unpromoted" : "malformed", (unsigned)promotion.error, (unsigned)adapted);
        }
        failures += failed;
        arena_pop_to(problem_arena, problem_mark);
        arena_pop_to(promoted_output, promoted_mark);
        arena_pop_to(lowered_output, lowered_mark);
    }
    printf("tier1-cfg-source: file=%s functions=%u failures=%u\n", name, file->count, failures);
    totals->files += 1;
    totals->functions += file->count;
}

static void tier1_cfg_source_print_path(const char* label, const Tier1CfgSourcePath* path, uint64_t instructions)
{
    printf("tier1-cfg-source: path=%s allocated=%" PRIu64 " constraint_refusals=%" PRIu64 " failed=%" PRIu64 " rejected=%" PRIu64 " intervals=%" PRIu64 " scan_share=%.4f spilled_values=%" PRIu64 " stores=%" PRIu64 " reloads=%" PRIu64 " recomputed=%" PRIu64 " moves=%" PRIu64 " split_moves=%" PRIu64 " split_edges=%" PRIu64 " temp_slot_functions=%" PRIu64 " spilled_masks=%" PRIu64 " mask_moves=%" PRIu64 " one_run_ns_per_instruction=%.2f\n",
           label, path->allocated, path->constraints, path->failed, path->rejected, path->intervals, path->intervals != 0 ? (double)path->scalar_intervals / (double)path->intervals : 0.0, path->spilled, path->stores, path->reloads, path->recomputed, path->moves, path->split_moves, path->split_edges, path->temp_slots, path->mask_spilled, path->mask_moves, instructions != 0 ? (double)path->nanoseconds / (double)instructions : 0.0);
}

static __attribute__((unused)) int run_tier1_cfg_source_tests(int count, char** arguments)
{
    const char* print = 0;
    bool simd = false;
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    bool valid = options != 0 && files != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        if (strcmp(arguments[i], "--print") == 0)
        {
            valid = i + 1 < count;
            print = valid ? arguments[i + 1] : 0;
            i += 1;
        }
        else if (strcmp(arguments[i], "--simd") == 0)
        {
            simd = true;
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count] = arguments[i];
            file_count += 1;
        }
        else if (strcmp(arguments[i], "--no-default-config") != 0)
        {
            // An option and its value, as test-ir-source takes them.
            options[option_count] = arguments[i];
            option_count += 1;
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count] = valid ? arguments[i + 1] : 0;
                option_count += 1;
                i += 1;
            }
        }
    }
    valid = valid && file_count != 0;
    Arena lowered = arena_reserve(UINT64_C(1) << 36);
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    Arena lowered_output = arena_reserve(UINT64_C(1) << 34);
    Arena promoted_output = arena_reserve(UINT64_C(1) << 34);
    Arena problem_arena = arena_reserve(UINT64_C(1) << 34);
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena scratch = arena_reserve(UINT64_C(1) << 34);
    valid = valid && lowered.base != 0 && arena.base != 0 && lowered_output.base != 0 && promoted_output.base != 0 && problem_arena.base != 0 && output.base != 0 && scratch.base != 0;
    Tier1CfgSourceTotals totals = { 0 };
    for (uint32_t i = 0; valid && i < file_count; i += 1)
    {
        IrSourceFile file;
        ir_source_prepare(&file, files[i], options, option_count, &lowered, &arena, false);
        if (file.status == IR_SOURCE_PREPARED)
        {
            tier1_cfg_source_check(&totals, &file, files[i], print, simd, &lowered_output, &promoted_output, &problem_arena, &output, &scratch);
        }
        else
        {
            totals.left_out += file.status == IR_SOURCE_LEFT_OUT;
            totals.failed_files += file.status == IR_SOURCE_FAILED;
            printf("tier1-cfg-source: file=%s %s=\"%s\"\n", files[i], file.status == IR_SOURCE_LEFT_OUT ? "left_out" : "failed", file.reason);
        }
        fflush(stdout);
        arena_pop_to(&lowered, 0);
        arena_pop_to(&arena, 0);
    }
    const uint64_t refused = totals.paths[0].failed + totals.paths[0].rejected + totals.paths[1].failed + totals.paths[1].rejected;
    if (valid)
    {
        printf("tier1-cfg-source: totals files=%" PRIu64 " left_out=%" PRIu64 " failed=%" PRIu64 " functions=%" PRIu64 " unsupported=%" PRIu64 " unpromoted=%" PRIu64 " malformed=%" PRIu64 " disagreed=%" PRIu64 " instructions=%" PRIu64 " values=%" PRIu64 " blocks=%" PRIu64 " edges=%" PRIu64 " phis=%" PRIu64 " simd_lowered=%" PRIu64 " masks=%" PRIu64 "\n",
               totals.files, totals.left_out, totals.failed_files, totals.functions, totals.unsupported, totals.unpromoted, totals.malformed, totals.disagreed, totals.instructions, totals.values, totals.blocks, totals.edges, totals.phis, totals.simd, totals.mask_values);
        tier1_cfg_source_print_path("vector", &totals.paths[0], totals.instructions);
        tier1_cfg_source_print_path("scalar", &totals.paths[1], totals.instructions);
        printf("tier1-cfg-source: frame traffic: slot_accesses=%" PRIu64 " vector=%" PRIu64 " scalar=%" PRIu64 "\n", totals.slot_accesses, totals.slot_accesses + totals.paths[0].stores + totals.paths[0].reloads,
               totals.slot_accesses + totals.paths[1].stores + totals.paths[1].reloads);
    }
    else
    {
        fprintf(stderr, "usage: compiler test-tier1-cfg-source [--simd] [--print NAME] [preprocessor options] file...\n");
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&problem_arena);
    arena_release(&promoted_output);
    arena_release(&lowered_output);
    arena_release(&arena);
    arena_release(&lowered);
    free(files);
    free(options);
    return valid && totals.failed_files == 0 && totals.unpromoted == 0 && totals.malformed == 0 && totals.disagreed == 0 && refused == 0 && totals.functions != 0 && (!simd || totals.mask_values != 0) ? 0 : 1;
}
