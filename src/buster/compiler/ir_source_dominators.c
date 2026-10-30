// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #174's opt-in C-source qualification, included by ir_source_test.c after dominators_test.c and ir.c.
// Each actual IR CFG is checked by forced scalar, forced vector and AUTO analysis, including all optional outputs,
// then against the independent deleted-node/forward-path definition oracle. No generated graph replaces a source
// function, no matrix-limit refusal counts as a pass, and the caller reports the file, function and representation.
// The optional text stream contains the exact validated successor edge multiset for an external measurement reader.
// A corpus is usable only when its caller writes the completion record after EVERY file and function has passed.
// All allocations borrow the caller's two distinct arenas and are rewound, including on a failed check or write.

typedef struct IrSourceDominatorCheck IrSourceDominatorCheck;
struct IrSourceDominatorCheck
{
    const char* reason;
    IrError cfg_error;
    DominatorStatus status[3];
    uint64_t checks;
};

// Names are hex-encoded, not whitespace-delimited C strings: paths containing spaces or newlines remain unambiguous.
// String lengths have already been established by the producer, or by strlen at the command-line boundary.
static bool ir_source_dominator_hex(FILE* stream, const uint8_t* bytes, uint64_t length)
{
    static const char digits[] = "0123456789abcdef";
    bool valid = true;
    for (uint64_t i = 0; valid && i < length; i += 1)
    {
        const int high = fputc(digits[bytes[i] >> 4], stream);
        const int low = fputc(digits[bytes[i] & 15], stream);
        valid = high != EOF && low != EOF;
    }
    valid = fputc('\n', stream) != EOF && valid;
    return valid;
}

static bool ir_source_dominator_export(FILE* stream, const DominatorCfg* cfg, const char* file, const uint8_t* name, uint32_t name_length, uint32_t form)
{
    bool valid = fprintf(stream, "cfg %u %u %u %u\nfile ", form, cfg->block_count, cfg->edge_count, cfg->entry) >= 0;
    valid = valid && ir_source_dominator_hex(stream, (const uint8_t*)file, strlen(file));
    valid = valid && fputs("name ", stream) != EOF;
    valid = valid && ir_source_dominator_hex(stream, name, name_length);
    for (uint32_t source = 0; valid && source < cfg->block_count; source += 1)
    {
        for (uint32_t edge = cfg->successor_offsets[source]; valid && edge < cfg->successor_offsets[(uint64_t)source + 1]; edge += 1)
        {
            valid = fprintf(stream, "%u %u\n", source, cfg->successors[edge]) >= 0;
        }
    }
    valid = valid && fputs("end\n", stream) != EOF && ferror(stream) == 0;
    return valid;
}

static bool ir_source_dominator_check(const IrFunction* function, Arena* output, Arena* scratch, FILE* stream, const char* file, const uint8_t* name, uint32_t name_length, uint32_t form, IrSourceDominatorCheck* check)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    *check = (IrSourceDominatorCheck){
        .reason = "CFG construction failed",
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
        Dominators analyses[3] = { 0 };
        const DominatorPath paths[] = {
            DOMINATOR_SCALAR,
            DOMINATOR_VECTOR,
            DOMINATOR_AUTO,
        };
        DominatorOptions options = dominators_options();
        check->reason = "a complete analysis failed or retained scratch";
        for (uint32_t path = 0; valid && path < 3; path += 1)
        {
            options.path = paths[path];
            check->status[path] = dominators_analyze(output, scratch, &graph, options, &analyses[path]);
            valid = check->status[path] == DOMINATOR_OK && scratch->length == scratch_mark;
        }
        if (valid)
        {
            check->reason = "scalar, vector and AUTO disagree";
            valid = dominator_test_equal(&analyses[0], &analyses[1]) && dominator_test_equal(&analyses[0], &analyses[2]);
        }
        if (valid)
        {
            check->reason = "the independent dominance/frontier/loop definition disagrees";
            DominatorTestTally tally = {
                .blocks = cfg.block_count,
                .shape = BUSTER_DOMINATOR_TEST_SHAPES,
            };
            // The definition oracle uses neither solver's transfer equation. Complete equality above makes its
            // result apply to all three paths, not just the one whose output it reads. This work is never timed.
            dominator_test_definition(&tally, output, &graph, &analyses[0]);
            check->checks = tally.checks;
            valid = tally.failures == 0;
        }
        if (valid && stream != 0)
        {
            check->reason = "writing the qualified CFG failed";
            valid = ir_source_dominator_export(stream, &graph, file, name, name_length, form);
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
