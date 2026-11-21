// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #174's opt-in C-source qualification, included by ir_source_test.c after dominators_export.c, dominators_test.c
// and ir.c.
// Each actual IR CFG is analyzed twice with all optional outputs; the two results must be equal, and the first must
// match the independent deleted-node/forward-path definition oracle and the order and tree columns' contract. No
// generated graph replaces a source function, no matrix-limit refusal counts as a pass, and the caller reports the
// file, function and representation.
// The optional text stream receives each validated CFG as a buster-dominators-v1 record (dominators_export.c), which
// benchmark-dominators --cfg reads.
// A corpus is usable only when its caller writes the completion record after EVERY file and function has passed.
// All allocations borrow the caller's two distinct arenas and are rewound, including on a failed check or write.

typedef struct IrSourceDominatorCheck IrSourceDominatorCheck;
struct IrSourceDominatorCheck
{
    const char* reason;
    IrError cfg_error;
    DominatorStatus status;
    uint64_t checks;
};

static bool ir_source_dominator_check(const IrFunction* function, Arena* output, Arena* scratch, FILE* stream, const char* file, const uint8_t* name, uint32_t name_length, uint32_t form, IrSourceDominatorCheck* check)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    *check = (IrSourceDominatorCheck){
        .reason = "CFG construction failed",
        .status = DOMINATOR_INVALID,
    };
    IrCfg cfg = { 0 };
    check->cfg_error = ir_build_cfg(function, output, scratch, &cfg);
    bool valid = check->cfg_error == IR_OK && scratch->length == scratch_mark;
    if (valid)
    {
        const DominatorCfg graph = {
            .block_count = cfg.block_count,
            .edge_count = cfg.edge_count,
            .entry = cfg.block_count == 0 ? BUSTER_DOMINATOR_NONE : 0,
            .successor_offsets = cfg.successor_offsets,
            .successors = cfg.successors,
            .predecessor_offsets = cfg.predecessor_offsets,
            .predecessors = cfg.predecessors,
        };
        Dominators analysis = { 0 };
        Dominators repeated = { 0 };
        const DominatorOptions options = dominators_options();
        check->reason = "a complete analysis failed or retained scratch";
        check->status = dominators_analyze(output, scratch, &graph, options, &analysis);
        valid = check->status == DOMINATOR_OK && scratch->length == scratch_mark;
        if (valid)
        {
            check->status = dominators_analyze(output, scratch, &graph, options, &repeated);
            valid = check->status == DOMINATOR_OK && scratch->length == scratch_mark;
        }
        if (valid)
        {
            check->reason = "a repeated analysis differs";
            valid = dominator_test_equal(&analysis, &repeated);
        }
        if (valid)
        {
            check->reason = "the independent dominance/frontier/loop definition disagrees";
            DominatorTestTally tally = {
                .blocks = cfg.block_count,
                .shape = BUSTER_DOMINATOR_TEST_SHAPES,
            };
            // The definition oracle shares no algorithm with the analyzer. This work is never timed.
            dominator_test_definition(&tally, output, &graph, &analysis, true);
            dominator_test_columns(&tally, &analysis);
            check->checks = tally.checks;
            valid = tally.failures == 0;
        }
        if (valid && stream != 0)
        {
            check->reason = "writing the qualified CFG failed";
            valid = dominators_export_cfg(stream, &graph, (const uint8_t*)file, strlen(file), name, name_length, form);
        }
    }
    if (valid)
    {
        check->reason = "ok";
    }
    arena_pop_to(scratch, scratch_mark);
    arena_pop_to(output, output_mark);
    return valid;
}

// Exercise this handoff, not just its solvers: exact serialization, rejected CFGs, both arena exhaustion paths,
// actual stream failure and restoration of caller-owned prefixes. ir_test_simple is an original one-block IR.
static bool ir_source_dominator_tests(Arena* memory, Arena* output, Arena* scratch)
{
    const uint64_t memory_mark = memory->length;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrFunction function = { 0 };
    bool valid = ir_test_simple(memory, &function);
    char* text = 0;
    size_t length = 0;
    FILE* stream = valid ? open_memstream(&text, &length) : 0;
    valid = valid && stream != 0;
    IrSourceDominatorCheck check;
    if (valid)
    {
        valid = ir_source_dominator_check(&function, output, scratch, stream, "a b\n", (const uint8_t*)"f", 1, 0, &check) && check.checks != 0;
        valid = valid && output->length == output_mark && scratch->length == scratch_mark;
    }
    if (stream != 0)
    {
        const int closed = fclose(stream);
        static const char expected[] = "cfg 0 1 0 0\nfile 6120620a\nname 66\nend\n";
        valid = valid && closed == 0 && length == sizeof(expected) - 1 && memcmp(text, expected, sizeof(expected) - 1) == 0;
    }
    free(text);
    if (valid)
    {
        Arena full_output = *output;
        full_output.capacity = full_output.length;
        valid = !ir_source_dominator_check(&function, &full_output, scratch, 0, "", 0, 0, 0, &check) && check.cfg_error == IR_ERROR_MEMORY;
        valid = valid && full_output.length == output_mark && scratch->length == scratch_mark;
        Arena full_scratch = *scratch;
        full_scratch.capacity = full_scratch.length;
        valid = !ir_source_dominator_check(&function, output, &full_scratch, 0, "", 0, 0, 0, &check) && valid;
        valid = valid && output->length == output_mark && full_scratch.length == scratch_mark;
        // A branch to an absent block must fail before the analyzer or exporter touches its columns.
        const uint32_t old_target = function.a[2];
        function.op[2] = IR_BR;
        function.a[2] = function.block_count;
        valid = !ir_source_dominator_check(&function, output, scratch, 0, "", 0, 0, 0, &check) && check.cfg_error != IR_OK && valid;
        valid = valid && output->length == output_mark && scratch->length == scratch_mark;
        function.op[2] = IR_RET;
        function.a[2] = old_target;
    }
    FILE* full = valid ? fopen("/dev/full", "w") : 0;
    if (full != 0)
    {
        const bool unbuffered = setvbuf(full, 0, _IONBF, 0) == 0;
        const bool rejected = unbuffered && !ir_source_dominator_check(&function, output, scratch, full, "file", (const uint8_t*)"f", 1, 0, &check);
        valid = valid && rejected && ferror(full) != 0 && output->length == output_mark && scratch->length == scratch_mark;
        fclose(full);
    }
    else
    {
        valid = false;
    }
    arena_pop_to(scratch, scratch_mark);
    arena_pop_to(output, output_mark);
    arena_pop_to(memory, memory_mark);
    printf("ir-source: dominator handoff controls %s\n", valid ? "passed" : "FAILED");
    return valid;
}
