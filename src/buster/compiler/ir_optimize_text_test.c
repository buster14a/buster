// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #175's first #139 pass tests in the IR's text form. Each fixture is a hand-written before/after pair under
// tests/ir/optimize: the before text is parsed by both parser paths, the pass runs by both optimizer twins, the
// vector result must be the after text byte for byte, the printed result is parsed again, and both sides execute
// with the same returns, statuses and stores (SROA, whose slot renumbering moves a store to another address in the
// executor's trace, compares the returns and statuses alone). `test-ir-optimize --print-text` prints the pairs'
// results for a reviewed regeneration. Included after ir_optimize_test.c, whose independent executor and structural
// comparison it shares; run_ir_optimize_tests calls ir_optimize_text_tests.

typedef enum IrOptimizeTextPass
{
    IR_OPTIMIZE_TEXT_SCCP,
    IR_OPTIMIZE_TEXT_DCE,
    IR_OPTIMIZE_TEXT_CFG,
    IR_OPTIMIZE_TEXT_COMBINED,
    IR_OPTIMIZE_TEXT_GVN,
    IR_OPTIMIZE_TEXT_LICM,
    IR_OPTIMIZE_TEXT_SROA,
} IrOptimizeTextPass;

typedef struct IrOptimizeTextFixture IrOptimizeTextFixture;
struct IrOptimizeTextFixture
{
    const char* name;
    IrOptimizeTextPass pass;
    // False where the pass renumbers stack slots (SROA's field splitting): the shared executor keys memory by slot
    // index, so a split slot's stores legitimately move to another address, and only the value and status are
    // compared there.
    bool effects;
};

static const IrOptimizeTextFixture ir_optimize_text_fixtures[] = {
    { .name = "sccp-constant-branch", .pass = IR_OPTIMIZE_TEXT_SCCP, .effects = true },
    { .name = "dce-roots", .pass = IR_OPTIMIZE_TEXT_DCE, .effects = true },
    { .name = "cfg-threading", .pass = IR_OPTIMIZE_TEXT_CFG, .effects = true },
    { .name = "combined-switch", .pass = IR_OPTIMIZE_TEXT_COMBINED, .effects = true },
    { .name = "gvn-congruence", .pass = IR_OPTIMIZE_TEXT_GVN, .effects = true },
    { .name = "licm-hoist", .pass = IR_OPTIMIZE_TEXT_LICM, .effects = true },
    { .name = "sroa-private-fields", .pass = IR_OPTIMIZE_TEXT_SROA, .effects = false },
};

// A fixture's diagnostic names the pair, so that a failure points at the file to read without a printout of the
// text itself.
static void ir_optimize_text_expect(IrOptimizeTest* test, bool condition, const char* name, const char* what)
{
    char message[192] = { 0 };
    snprintf(message, sizeof(message), "%s: %s", name, what);
    ir_optimize_test_expect(test, condition, message);
}

// The pair's files are read into the caller's source arena; the text keeps its file's bytes, so the canonical
// comparison below is byte for byte.
static bool ir_optimize_text_read(const char* path, Arena* source, String8* text)
{
    FILE* stream = fopen(path, "rb");
    bool ok = stream != 0;
    if (ok)
    {
        ok = fseek(stream, 0, SEEK_END) == 0;
        const long length = ok ? ftell(stream) : -1;
        ok = length > 0 && fseek(stream, 0, SEEK_SET) == 0;
        char* bytes = ok ? arena_push(source, (uint64_t)length, 1) : 0;
        ok = ok && bytes != 0;
        if (ok)
        {
            ok = fread(bytes, 1, (size_t)length, stream) == (size_t)length && ferror(stream) == 0;
            if (ok)
            {
                *text = (String8){
                    .pointer = bytes,
                    .length = (uint64_t)length,
                };
            }
        }
        fclose(stream);
    }
    return ok;
}

// One fixture file holds exactly one function and then whitespace only. Both parser paths must accept it, consume
// the same function and agree column for column. On failure both arenas are as they were, so the caller can report
// and continue.
static bool ir_optimize_text_parse(String8 text, Arena* output, Arena* twin, Arena* scratch, IrFunction* function, IrFunction* twin_function)
{
    const uint64_t output_mark = output->length;
    const uint64_t twin_mark = twin->length;
    const IrParseResult vector = ir_parse(text, output, scratch, function);
    const IrParseResult scalar = ir_parse_scalar(text, twin, scratch, twin_function);
    bool ok = vector.error == IR_PARSE_OK && scalar.error == IR_PARSE_OK && vector.consumed == scalar.consumed && vector.consumed != 0 && vector.consumed <= text.length &&
              ir_optimize_test_equal(function, twin_function);
    if (ok)
    {
        // Both paths ended at the same byte, so the vector path's read of the rest is the scalar path's too.
        const String8 rest = {
            .pointer = text.pointer + vector.consumed,
            .length = text.length - vector.consumed,
        };
        const uint64_t rest_mark = output->length;
        IrFunction extra = { 0 };
        const IrParseResult end = ir_parse(rest, output, scratch, &extra);
        ok = end.error == IR_PARSE_END && end.consumed == 0;
        arena_pop_to(output, rest_mark);
    }
    if (!ok)
    {
        arena_pop_to(output, output_mark);
        arena_pop_to(twin, twin_mark);
    }
    return ok;
}

// The pass by the requested twin. `change` counts the pass's own transformations, as its result reports them, so a
// fixture that stops exercising its pass fails instead of passing on an unchanged function.
static IrCheck ir_optimize_text_transform(const IrOptimizeTextFixture* fixture, const IrFunction* input, Arena* output, Arena* scratch, bool scalar, IrFunction* function, uint64_t* change)
{
    IrCheck check;
    IrFunction result = { 0 };
    uint64_t changed = 0;
    if (fixture->pass == IR_OPTIMIZE_TEXT_GVN)
    {
        IrGvnOptions options = ir_gvn_options();
        options.scalar = scalar;
        IrGvnResult pass = { 0 };
        check = ir_optimize_gvn(input, output, scratch, options, &pass);
        result = pass.function;
        changed = pass.removed;
    }
    else if (fixture->pass == IR_OPTIMIZE_TEXT_LICM)
    {
        IrLicmOptions options = ir_licm_options();
        options.scalar = scalar;
        IrLicmResult pass = { 0 };
        check = ir_optimize_licm(input, output, scratch, options, &pass);
        result = pass.function;
        changed = pass.moved;
    }
    else if (fixture->pass == IR_OPTIMIZE_TEXT_SROA)
    {
        IrSroaOptions options = ir_sroa_options();
        options.scalar = scalar;
        IrSroaResult pass = { 0 };
        check = ir_optimize_sroa(input, output, scratch, options, &pass);
        result = pass.function;
        changed = pass.fields;
    }
    else
    {
        IrOptimizeOptions options = ir_optimize_options();
        options.passes = fixture->pass == IR_OPTIMIZE_TEXT_SCCP ? BUSTER_IR_OPTIMIZE_SCCP :
                         fixture->pass == IR_OPTIMIZE_TEXT_DCE ? BUSTER_IR_OPTIMIZE_DCE :
                         fixture->pass == IR_OPTIMIZE_TEXT_CFG ? BUSTER_IR_OPTIMIZE_CFG :
                                                                 BUSTER_IR_OPTIMIZE_SCCP | BUSTER_IR_OPTIMIZE_DCE | BUSTER_IR_OPTIMIZE_CFG;
        options.path = scalar ? BUSTER_IR_OPTIMIZE_SCALAR : BUSTER_IR_OPTIMIZE_VECTOR;
        IrOptimizeResult pass = { 0 };
        check = ir_optimize(input, output, scratch, options, &pass);
        result = pass.function;
        changed = (uint64_t)pass.folded + pass.removed_instructions + pass.removed_blocks;
    }
    *function = result;
    *change = changed;
    return check;
}

// The printer's bytes of a function, allocated with malloc for the caller to free; null when printing fails.
static char* ir_optimize_text_print(const IrFunction* function, Arena* scratch, size_t* length)
{
    char* text = 0;
    size_t bytes = 0;
    FILE* stream = open_memstream(&text, &bytes);
    bool ok = stream != 0;
    if (ok)
    {
        const uint64_t mark = scratch->length;
        ok = ir_print(function, scratch, stream) == IR_OK;
        arena_pop_to(scratch, mark);
        ok = fclose(stream) == 0 && ok;
    }
    if (!ok)
    {
        free(text);
        text = 0;
        bytes = 0;
    }
    *length = bytes;
    return text;
}

static bool ir_optimize_text_same_bytes(const char* text, size_t length, String8 expected)
{
    return length == expected.length && (length == 0 || memcmp(text, expected.pointer, length) == 0);
}

// Independent execution of both sides: every fixture returns, so a step-limit or fault status fails it.
static void ir_optimize_text_execute(IrOptimizeTest* test, const char* name, const IrFunction* before, const IrFunction* after, Arena* scratch, bool effects)
{
    bool same = true;
    for (uint32_t trial = 0; trial < 18; trial += 1)
    {
        const uint64_t parameter = trial == 17 ? UINT64_MAX : trial;
        const uint64_t unspecified = trial & 1;
        const IrOptimizeExecution original = ir_optimize_test_execute(before, scratch, parameter, unspecified);
        const IrOptimizeExecution transformed = ir_optimize_test_execute(after, scratch, parameter, unspecified);
        same = same && original.status == 1 && transformed.status == 1 && original.value == transformed.value &&
               (!effects || original.effects == transformed.effects);
    }
    ir_optimize_text_expect(test, same, name, "the pass changed an interpreted return, status or store");
}

static void ir_optimize_text_fixture(IrOptimizeTest* test, const IrOptimizeTextFixture* fixture, Arena* source, Arena* output, Arena* twin, Arena* scratch)
{
    char before_path[128] = { 0 };
    char after_path[128] = { 0 };
    snprintf(before_path, sizeof(before_path), "tests/ir/optimize/%s-before.ir", fixture->name);
    snprintf(after_path, sizeof(after_path), "tests/ir/optimize/%s-after.ir", fixture->name);
    const uint64_t source_mark = source->length;
    String8 before_text = { 0 };
    String8 after_text = { 0 };
    const bool read = ir_optimize_text_read(before_path, source, &before_text) && ir_optimize_text_read(after_path, source, &after_text);
    ir_optimize_text_expect(test, read, fixture->name, "the pair reads from tests/ir/optimize");
    if (read)
    {
        ir_optimize_text_expect(test, !ir_optimize_text_same_bytes(before_text.pointer, before_text.length, after_text), fixture->name,
                                "the pair is identical, so it tests no transformation");
        const uint64_t output_mark = output->length;
        const uint64_t twin_mark = twin->length;
        IrFunction before = { 0 };
        IrFunction temporary = { 0 };
        const bool parsed = ir_optimize_text_parse(before_text, output, twin, scratch, &before, &temporary);
        ir_optimize_text_expect(test, parsed, fixture->name, "the before text is one function both parser paths accept and agree on");
        IrFunction expected = { 0 };
        const bool expected_parsed = parsed && ir_optimize_text_parse(after_text, output, twin, scratch, &expected, &temporary);
        ir_optimize_text_expect(test, expected_parsed, fixture->name, "the after text is one function both parser paths accept and agree on");
        arena_pop_to(twin, twin_mark);
        if (parsed && expected_parsed)
        {
            size_t before_length = 0;
            char* before_printed = ir_optimize_text_print(&before, scratch, &before_length);
            ir_optimize_text_expect(test, before_printed != 0 && ir_optimize_text_same_bytes(before_printed, before_length, before_text), fixture->name,
                                    "the before text is not the canonical print of its function");
            free(before_printed);
            size_t after_length = 0;
            char* after_printed = ir_optimize_text_print(&expected, scratch, &after_length);
            ir_optimize_text_expect(test, after_printed != 0 && ir_optimize_text_same_bytes(after_printed, after_length, after_text), fixture->name,
                                    "the after text is not the canonical print of its function");
            free(after_printed);
            IrFunction vector = { 0 };
            uint64_t vector_change = 0;
            const IrCheck vector_check = ir_optimize_text_transform(fixture, &before, output, scratch, false, &vector, &vector_change);
            ir_optimize_text_expect(test, vector_check.error == IR_OK, fixture->name, "the vector twin transforms the before text");
            ir_optimize_text_expect(test, vector_check.error != IR_OK || vector_change != 0, fixture->name,
                                    "the pass reports no transformation, so the fixture no longer exercises it");
            if (vector_check.error == IR_OK)
            {
                // The text form does not carry unused sparse rows (constant, slot, address and call-type tables keep
                // their original indices in the pass's function), so the canonical bytes are the structural contract:
                // the printed result is the after text, and parsing it again gives the after text's function.
                size_t length = 0;
                char* text = ir_optimize_text_print(&vector, scratch, &length);
                ir_optimize_text_expect(test, text != 0 && ir_optimize_text_same_bytes(text, length, after_text), fixture->name,
                                        "the canonical print of the result differs from the after text's bytes");
                if (text != 0)
                {
                    const String8 printed = {
                        .pointer = text,
                        .length = length,
                    };
                    const uint64_t reparse_mark = source->length;
                    IrFunction reparsed = { 0 };
                    IrFunction reparsed_twin = { 0 };
                    const bool reparsed_ok = ir_optimize_text_parse(printed, twin, source, scratch, &reparsed, &reparsed_twin);
                    ir_optimize_text_expect(test, reparsed_ok && ir_optimize_test_equal(&reparsed, &expected), fixture->name,
                                            "printing the result and parsing it again does not give the after text's function");
                    size_t again_length = 0;
                    char* again = reparsed_ok ? ir_optimize_text_print(&reparsed, scratch, &again_length) : 0;
                    ir_optimize_text_expect(test, again != 0 && ir_optimize_text_same_bytes(again, again_length, after_text), fixture->name,
                                            "the reparsed result does not print the after text again");
                    free(again);
                    arena_pop_to(twin, twin_mark);
                    arena_pop_to(source, reparse_mark);
                    free(text);
                    text = 0;
                }
            }
            arena_pop_to(twin, twin_mark);
            IrFunction scalar = { 0 };
            uint64_t scalar_change = 0;
            const IrCheck scalar_check = ir_optimize_text_transform(fixture, &before, twin, scratch, true, &scalar, &scalar_change);
            ir_optimize_text_expect(test, scalar_check.error == IR_OK, fixture->name, "the scalar twin transforms the before text");
            if (fixture->pass == IR_OPTIMIZE_TEXT_GVN)
            {
                // GVN's scalar twin is the available-expression oracle: it may find fewer congruences than the vector
                // partition refinement, and always finds no more of them.
                ir_optimize_text_expect(test, scalar_check.error != IR_OK || scalar_change <= vector_change, fixture->name,
                                        "the scalar GVN oracle removes more than the vector partition does");
            }
            else
            {
                ir_optimize_text_expect(test, scalar_check.error == IR_OK && ir_optimize_test_equal(&vector, &scalar), fixture->name,
                                        "the optimizer twins give different functions");
            }
            if (vector_check.error == IR_OK)
            {
                ir_optimize_text_execute(test, fixture->name, &before, &vector, scratch, fixture->effects);
            }
            if (scalar_check.error == IR_OK && !ir_optimize_test_equal(&vector, &scalar))
            {
                ir_optimize_text_execute(test, fixture->name, &before, &scalar, scratch, fixture->effects);
            }
        }
        arena_pop_to(twin, twin_mark);
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(source, source_mark);
}

static void ir_optimize_text_tests(IrOptimizeTest* test)
{
    Arena source = arena_reserve(1ull << 20);
    Arena output = arena_reserve(8ull << 20);
    Arena twin = arena_reserve(8ull << 20);
    Arena scratch = arena_reserve(8ull << 20);
    const bool ready = source.base != 0 && output.base != 0 && twin.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "text fixture arenas");
    for (uint64_t index = 0; ready && index < sizeof(ir_optimize_text_fixtures) / sizeof(ir_optimize_text_fixtures[0]); index += 1)
    {
        ir_optimize_text_fixture(test, &ir_optimize_text_fixtures[index], &source, &output, &twin, &scratch);
        ir_optimize_text_expect(test, scratch.length == 0, ir_optimize_text_fixtures[index].name, "the parser, printer or pass retained scratch memory");
        arena_pop_to(&output, 0);
        arena_pop_to(&twin, 0);
    }
    arena_release(&scratch);
    arena_release(&twin);
    arena_release(&output);
    arena_release(&source);
}

// `test-ir-optimize --print-text`: each before text's vector result, printed for review before it becomes an after
// fixture. It writes no file; the reviewed bytes are pasted into the pair.
static __attribute__((unused)) int ir_optimize_text_print_fixtures(void)
{
    Arena source = arena_reserve(1ull << 20);
    Arena output = arena_reserve(8ull << 20);
    Arena twin = arena_reserve(8ull << 20);
    Arena scratch = arena_reserve(8ull << 20);
    bool ok = source.base != 0 && output.base != 0 && twin.base != 0 && scratch.base != 0;
    for (uint64_t index = 0; ok && index < sizeof(ir_optimize_text_fixtures) / sizeof(ir_optimize_text_fixtures[0]); index += 1)
    {
        const IrOptimizeTextFixture* fixture = &ir_optimize_text_fixtures[index];
        char path[128] = { 0 };
        snprintf(path, sizeof(path), "tests/ir/optimize/%s-before.ir", fixture->name);
        String8 text = { 0 };
        ok = ir_optimize_text_read(path, &source, &text);
        IrFunction before = { 0 };
        IrFunction before_twin = { 0 };
        ok = ok && ir_optimize_text_parse(text, &output, &twin, &scratch, &before, &before_twin);
        arena_pop_to(&twin, 0);
        IrFunction result = { 0 };
        uint64_t change = 0;
        ok = ok && ir_optimize_text_transform(fixture, &before, &output, &scratch, false, &result, &change).error == IR_OK;
        size_t length = 0;
        char* printed = ok ? ir_optimize_text_print(&result, &scratch, &length) : 0;
        ok = ok && printed != 0;
        if (ok)
        {
            printf("== %s\n%.*s", fixture->name, (int)length, printed);
        }
        free(printed);
        if (!ok)
        {
            fprintf(stderr, "ir-optimize: %s: cannot print its result\n", fixture->name);
        }
        arena_pop_to(&output, 0);
        arena_pop_to(&source, 0);
    }
    arena_release(&scratch);
    arena_release(&twin);
    arena_release(&output);
    arena_release(&source);
    return ok ? 0 : 1;
}
