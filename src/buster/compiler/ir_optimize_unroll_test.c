// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-ir-unroll (#2755): the loop unroller's fixtures. Include after ir_optimize_unroll.c, ir_optimize_test.c and
// ir_vectorize_loops_test.c, whose independent executors and fixture inputs it shares. Each scalar fixture is IR text
// that a small builder numbers (its lines name their values, `{name}` reads one), parsed by ir_parse. The original
// fixtures keep their frozen function/count digest and restore scratch; an unrolled one must verify with dominance, survive Tier 1's optimizer,
// and execute as the original did (return, status and stores) on parameters that give zero, partial and whole main
// iterations, unknown trip counts and bounds at each type's signed and unsigned limits. A refused one gives the input
// itself and counts its reason. The generated shapes cover the four loop forms (the test on the induction phi or on its
// increment, in the header or the latch, a one-block loop, a diamond inside), the four predicates with either operand
// order and either branch sense, steps, i8 to i64 inductions, each reduction kind and phis that only look like one,
// and addresses of the induction and its extension. Output arenas of every size up to a fixture's need check rollback.
// The composed fixtures feed the vectorizer's actual output into the unroller, with integer reductions, widened lanes,
// alias guards and masked or scalar tails. Their interpreter trials straddle both vector and unrolled-chunk boundaries.

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

#define BUSTER_IR_UNROLL_TEST_LINES 96
#define BUSTER_IR_UNROLL_TEST_TEXT 16384

typedef struct IrUnrollTestLine IrUnrollTestLine;
struct IrUnrollTestLine
{
    char name[16];
    char body[200];
    bool label;
};

typedef struct IrUnrollTestText IrUnrollTestText;
struct IrUnrollTestText
{
    IrUnrollTestLine lines[BUSTER_IR_UNROLL_TEST_LINES];
    uint32_t count;
    char header[160];
    bool overflow;
};

static void ir_unroll_test_label(IrUnrollTestText* text, uint32_t block)
{
    if (text->count < BUSTER_IR_UNROLL_TEST_LINES)
    {
        IrUnrollTestLine* line = &text->lines[text->count];
        *line = (IrUnrollTestLine){ .label = true };
        snprintf(line->body, sizeof(line->body), "b%" PRIu32 ":", block);
        text->count += 1;
    }
    else
    {
        text->overflow = true;
    }
}

// An instruction line, `name` its value's (or "" for none), its body after "%N = ".
static __attribute__((format(printf, 3, 4))) void ir_unroll_test_line(IrUnrollTestText* text, const char* name, const char* format, ...)
{
    if (text->count < BUSTER_IR_UNROLL_TEST_LINES)
    {
        IrUnrollTestLine* line = &text->lines[text->count];
        *line = (IrUnrollTestLine){ 0 };
        snprintf(line->name, sizeof(line->name), "%s", name);
        va_list arguments;
        va_start(arguments, format);
        const int written = vsnprintf(line->body, sizeof(line->body), format, arguments);
        va_end(arguments);
        text->overflow = text->overflow || written < 0 || (size_t)written >= sizeof(line->body);
        text->count += 1;
    }
    else
    {
        text->overflow = true;
    }
}

// The text: each instruction line numbered in order, each `{name}` its value's number.
static bool ir_unroll_test_render(const IrUnrollTestText* text, char* output, size_t capacity, size_t* length)
{
    size_t at = 0;
    bool ok = !text->overflow;
    int written = snprintf(output, capacity, "%s\n", text->header);
    ok = ok && written > 0 && (size_t)written < capacity;
    at = ok ? (size_t)written : 0;
    uint32_t number = 0;
    for (uint32_t k = 0; ok && k < text->count; k += 1)
    {
        const IrUnrollTestLine* line = &text->lines[k];
        if (line->label)
        {
            written = snprintf(output + at, capacity - at, "%s\n", line->body);
        }
        else
        {
            char body[256] = { 0 };
            size_t out = 0;
            for (const char* c = line->body; ok && *c != 0; c += 1)
            {
                if (*c == '{')
                {
                    const char* close = strchr(c, '}');
                    ok = close != NULL && (size_t)(close - c - 1) < 16;
                    char name[16] = { 0 };
                    if (ok)
                    {
                        memcpy(name, c + 1, (size_t)(close - c - 1));
                    }
                    uint32_t found = UINT32_MAX;
                    uint32_t index = 0;
                    for (uint32_t m = 0; ok && m < text->count; m += 1)
                    {
                        if (!text->lines[m].label)
                        {
                            found = strcmp(text->lines[m].name, name) == 0 ? index : found;
                            index += 1;
                        }
                    }
                    ok = ok && found != UINT32_MAX;
                    const int n = ok ? snprintf(body + out, sizeof(body) - out, "%%%" PRIu32, found) : -1;
                    ok = ok && n > 0 && (size_t)n < sizeof(body) - out;
                    out += ok ? (size_t)n : 0;
                    c = ok ? close : c;
                }
                else
                {
                    ok = out + 1 < sizeof(body);
                    body[out] = ok ? *c : 0;
                    out += ok ? 1 : 0;
                }
            }
            written = ok ? snprintf(output + at, capacity - at, "  %%%" PRIu32 " = %s\n", number, body) : -1;
            number += 1;
        }
        ok = ok && written > 0 && (size_t)written < capacity - at;
        at += ok ? (size_t)written : 0;
    }
    written = ok ? snprintf(output + at, capacity - at, "}\n") : -1;
    ok = ok && written > 0 && (size_t)written < capacity - at;
    at += ok ? (size_t)written : 0;
    *length = at;
    return ok;
}

static bool ir_unroll_test_parse(const IrUnrollTestText* text, Arena* output, Arena* scratch, IrFunction* function)
{
    char* source = malloc(BUSTER_IR_UNROLL_TEST_TEXT);
    size_t length = 0;
    bool ok = source != NULL && ir_unroll_test_render(text, source, BUSTER_IR_UNROLL_TEST_TEXT, &length);
    if (ok)
    {
        const String8 string = {
            .pointer = source,
            .length = length,
        };
        const IrParseResult parsed = ir_parse(string, output, scratch, function);
        ok = parsed.error == IR_PARSE_OK;
        if (!ok)
        {
            fprintf(stderr, "ir-unroll: fixture does not parse (error %d at byte %" PRIu64 "):\n%s", (int)parsed.error, (uint64_t)parsed.consumed, source);
        }
    }
    free(source);
    return ok;
}

// A constant's text of `type` and value, its bits masked to the type.
static void ir_unroll_test_constant(IrUnrollTestText* text, const char* name, const char* type, uint32_t bits, uint64_t value)
{
    const uint64_t mask = bits >= 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    ir_unroll_test_line(text, name, "const.%s 0x0000000000000000%016" PRIx64, type, value & mask);
}

typedef struct IrUnrollTestShape IrUnrollTestShape;
struct IrUnrollTestShape
{
    // The induction's type (8 to 64 bits), the continuing test (ult, ule, slt, sle), whether the induction is its right
    // operand, whether the loop continues on the branch's false edge, the form, the step, the start's and the bound's
    // constant offsets from the parameter, the reduction's kind and whether the body has a store of the induction's
    // address (or its extension's).
    uint32_t bits;
    uint32_t predicate;
    bool swapped;
    bool inverted;
    uint32_t form;
    uint32_t step;
    uint32_t start;
    uint32_t bound;
    uint32_t reduction;
    bool memory;
    // The sum form's constant: the header tests i + sum, apart from the increment.
    uint32_t sum;
};

#define BUSTER_IR_UNROLL_TEST_FORM_TOP 0u
#define BUSTER_IR_UNROLL_TEST_FORM_TOP_INCREMENT 1u
#define BUSTER_IR_UNROLL_TEST_FORM_SINGLE 2u
#define BUSTER_IR_UNROLL_TEST_FORM_LATCH 3u
#define BUSTER_IR_UNROLL_TEST_FORM_DIAMOND 4u
#define BUSTER_IR_UNROLL_TEST_FORM_SUM 5u
#define BUSTER_IR_UNROLL_TEST_FORMS 6u
#define BUSTER_IR_UNROLL_TEST_REDUCTIONS 14u

static const char* const ir_unroll_test_predicates[4] = { "ult", "ule", "lt", "le" };

static const char* ir_unroll_test_type(uint32_t bits)
{
    return bits == 8 ? "i8" : bits == 16 ? "i16" : bits == 32 ? "i32" : "i64";
}

// The predicate's text for the icmp as written: mirrored where the induction is its right operand, its complement where
// the loop continues on the false edge.
static const char* ir_unroll_test_predicate(const IrUnrollTestShape* shape)
{
    static const char* const names[IR_PREDICATE_COUNT] = { "eq", "ne", "lt", "le", "gt", "ge", "ult", "ule", "ugt", "uge" };
    static const uint32_t codes[4] = { IR_ULT, IR_ULE, IR_LT, IR_LE };
    uint32_t predicate = codes[shape->predicate];
    predicate = shape->inverted ? ir_unroll_inverse(predicate) : predicate;
    predicate = shape->swapped ? ir_unroll_mirror(predicate) : predicate;
    return names[predicate];
}

// The reduction's update of r by x into `name`, in the latch's part of the body.
static void ir_unroll_test_reduction(IrUnrollTestText* text, const IrUnrollTestShape* shape, const char* type)
{
    static const char* const operations[6] = { "add", "sub", "mul", "and", "or", "xor" };
    const uint32_t kind = shape->reduction;
    if (kind < 6)
    {
        ir_unroll_test_line(text, "rx", "or.%s {x}, {one}", type);
        ir_unroll_test_line(text, "r2", kind == 1 ? "%s.%s {r}, {rx}" : "%s.%s {rx}, {r}", operations[kind], type);
    }
    else if (kind == 6)
    {
        ir_unroll_test_line(text, "rc", "icmp.i1 {x}, {seven} ult");
        ir_unroll_test_line(text, "ru", "add.%s {r}, {x}", type);
        ir_unroll_test_line(text, "r2", "select.%s {rc}, {ru}, {r}", type);
    }
    else if (kind == 7)
    {
        ir_unroll_test_line(text, "rc", "icmp.i1 {x}, {seven} ult");
        ir_unroll_test_line(text, "ru", "xor.%s {r}, {x}", type);
        ir_unroll_test_line(text, "r2", "select.%s {rc}, {r}, {ru}", type);
    }
    else if (kind <= 11)
    {
        // smax, smin, umax and umin, each in a different order of the icmp's operands and the select's arms.
        static const char* const predicates[4] = { "lt", "lt", "ugt", "ult" };
        static const bool r_first[4] = { true, false, false, true };
        static const bool takes_x[4] = { true, true, true, false };
        const uint32_t k = kind - 8;
        ir_unroll_test_line(text, "rx", "mul.%s {x}, {seven}", type);
        ir_unroll_test_line(text, "rc", r_first[k] ? "icmp.i1 {r}, {rx} %s" : "icmp.i1 {rx}, {r} %s", predicates[k]);
        ir_unroll_test_line(text, "r2", takes_x[k] ? "select.%s {rc}, {rx}, {r}" : "select.%s {rc}, {r}, {rx}", type);
    }
    else if (kind == 12)
    {
        // r + r: no reduction, r read twice.
        ir_unroll_test_line(text, "r2", "add.%s {r}, {r}", type);
    }
    else
    {
        // r also read by the carried value: no reduction.
        ir_unroll_test_line(text, "r2", "add.%s {r}, {x}", type);
    }
}

// The fixture's text: a function of one i64 parameter p whose loop runs i from p + start while i (or i + step) is below
// p + bound, by step, with a carried value s = s * 3 + i, a reduction r of x = (i ^ p) & 15, and with `memory` a store at
// the induction's address in a 64-byte slot; it returns s ^ r ^ i, extended.
static void ir_unroll_test_shape(IrUnrollTestText* text, const IrUnrollTestShape* shape)
{
    const char* type = ir_unroll_test_type(shape->bits);
    const bool top = shape->form == BUSTER_IR_UNROLL_TEST_FORM_TOP || shape->form == BUSTER_IR_UNROLL_TEST_FORM_TOP_INCREMENT || shape->form == BUSTER_IR_UNROLL_TEST_FORM_DIAMOND ||
                     shape->form == BUSTER_IR_UNROLL_TEST_FORM_SUM;
    const bool increment_tested = shape->form != BUSTER_IR_UNROLL_TEST_FORM_TOP && shape->form != BUSTER_IR_UNROLL_TEST_FORM_DIAMOND && shape->form != BUSTER_IR_UNROLL_TEST_FORM_SUM;
    const char* predicate = ir_unroll_test_predicate(shape);
    const char* compared = shape->form == BUSTER_IR_UNROLL_TEST_FORM_SUM ? "{isum}" : increment_tested ? "{i2}" : "{i}";
    char test[96] = { 0 };
    snprintf(test, sizeof(test), shape->swapped ? "icmp.i1 {n}, %s %s" : "icmp.i1 %s, {n} %s", compared, predicate);
    // The header's and latch's blocks: b1 the header, the body's last block the latch, the exit after it.
    const uint32_t latch = shape->form == BUSTER_IR_UNROLL_TEST_FORM_SINGLE ? 1u : shape->form == BUSTER_IR_UNROLL_TEST_FORM_DIAMOND ? 5u : 2u;
    const uint32_t exit = latch + 1u;
    snprintf(text->header, sizeof(text->header), "function (i64) -> i64 {\n  slot $0 size=64 align=8 type=void flags=0x00");
    ir_unroll_test_label(text, 0);
    ir_unroll_test_line(text, "p", "param.i64 0");
    if (shape->bits == 64)
    {
        ir_unroll_test_line(text, "pt", "copy.i64 {p}");
    }
    else
    {
        ir_unroll_test_line(text, "pt", "trunc.%s {p}", type);
    }
    ir_unroll_test_constant(text, "c_start", type, shape->bits, shape->start);
    ir_unroll_test_line(text, "start", "add.%s {pt}, {c_start}", type);
    ir_unroll_test_constant(text, "c_bound", type, shape->bits, shape->bound);
    ir_unroll_test_line(text, "n", "add.%s {pt}, {c_bound}", type);
    ir_unroll_test_constant(text, "step", type, shape->bits, shape->step);
    ir_unroll_test_constant(text, "zero", type, shape->bits, 0);
    ir_unroll_test_constant(text, "one", type, shape->bits, 1);
    ir_unroll_test_constant(text, "three", type, shape->bits, 3);
    ir_unroll_test_constant(text, "seven", type, shape->bits, 7);
    ir_unroll_test_constant(text, "fifteen", type, shape->bits, 15);
    ir_unroll_test_constant(text, "r0", type, shape->bits, shape->reduction == 2 ? 1 : shape->reduction == 3 ? UINT64_MAX : 0);
    ir_unroll_test_constant(text, "c_sum", type, shape->bits, shape->sum);
    ir_unroll_test_line(text, "slot", "stack_addr.ptr $0");
    ir_unroll_test_line(text, "", "br.void b1");
    ir_unroll_test_label(text, 1);
    ir_unroll_test_line(text, "i", "phi.%s [{start}, b0], [{i2}, b%" PRIu32 "]", type, latch);
    ir_unroll_test_line(text, "s", "phi.%s [{zero}, b0], [{s2}, b%" PRIu32 "]", type, latch);
    ir_unroll_test_line(text, "r", "phi.%s [{r0}, b0], [{r2}, b%" PRIu32 "]", type, latch);
    if (shape->form == BUSTER_IR_UNROLL_TEST_FORM_TOP_INCREMENT)
    {
        ir_unroll_test_line(text, "i2", "add.%s {i}, {step}", type);
    }
    if (shape->form == BUSTER_IR_UNROLL_TEST_FORM_SUM)
    {
        ir_unroll_test_line(text, "isum", "add.%s {c_sum}, {i}", type);
    }
    if (top)
    {
        ir_unroll_test_line(text, "t", "%s", test);
        ir_unroll_test_line(text, "", shape->inverted ? "cbr.void {t}, b%" PRIu32 ", b2" : "cbr.void {t}, b2, b%" PRIu32, exit);
        ir_unroll_test_label(text, 2);
    }
    // The body: x, the carried value, the reduction, and with `memory` the store.
    ir_unroll_test_line(text, "ix", "xor.%s {i}, {pt}", type);
    ir_unroll_test_line(text, "x", "and.%s {ix}, {fifteen}", type);
    ir_unroll_test_line(text, "s1", "mul.%s {s}, {three}", type);
    if (shape->form == BUSTER_IR_UNROLL_TEST_FORM_DIAMOND)
    {
        ir_unroll_test_line(text, "dc", "icmp.i1 {x}, {seven} ult");
        ir_unroll_test_line(text, "", "cbr.void {dc}, b3, b4");
        ir_unroll_test_label(text, 3);
        ir_unroll_test_line(text, "da", "add.%s {s1}, {i}", type);
        ir_unroll_test_line(text, "", "br.void b5");
        ir_unroll_test_label(text, 4);
        ir_unroll_test_line(text, "db", "sub.%s {s1}, {i}", type);
        ir_unroll_test_line(text, "", "br.void b5");
        ir_unroll_test_label(text, 5);
        ir_unroll_test_line(text, "s2", "phi.%s [{da}, b3], [{db}, b4]", type);
    }
    else if (shape->reduction == 13)
    {
        ir_unroll_test_line(text, "s1r", "add.%s {s1}, {r}", type);
        ir_unroll_test_line(text, "s2", "add.%s {s1r}, {i}", type);
    }
    else
    {
        ir_unroll_test_line(text, "s2", "add.%s {s1}, {i}", type);
    }
    if (shape->form == BUSTER_IR_UNROLL_TEST_FORM_LATCH)
    {
        ir_unroll_test_line(text, "", "br.void b2");
        ir_unroll_test_label(text, 2);
    }
    ir_unroll_test_reduction(text, shape, type);
    if (shape->memory)
    {
        // The store at the slot's i & 15 (the induction itself where the shape's start and bound keep it there).
        const bool direct = shape->bits == 64 && shape->start == 0 && shape->bound <= 16 && !shape->inverted;
        if (direct)
        {
            ir_unroll_test_line(text, "mi", "and.i64 {p}, {zero}");
            ir_unroll_test_line(text, "ma", "address.ptr {slot} + {i} * 4 offset=0");
        }
        else
        {
            ir_unroll_test_line(text, "mi", "and.%s {i}, {fifteen}", type);
            ir_unroll_test_line(text, "me", shape->bits == 64 ? "copy.i64 {mi}" : shape->predicate >= 2 ? "sext.i64 {mi}" : "zext.i64 {mi}");
            ir_unroll_test_line(text, "ma", "address.ptr {slot} + {me} * 4 offset=0");
        }
        ir_unroll_test_line(text, "mv", shape->bits == 64 ? "trunc.i32 {s2}" : shape->bits == 32 ? "copy.i32 {s2}" : "zext.i32 {s2}");
        ir_unroll_test_line(text, "", "store.void {ma}, {mv} align=4");
    }
    if (shape->form != BUSTER_IR_UNROLL_TEST_FORM_TOP_INCREMENT)
    {
        ir_unroll_test_line(text, "i2", "add.%s {i}, {step}", type);
    }
    if (top)
    {
        ir_unroll_test_line(text, "", "br.void b1");
    }
    else
    {
        ir_unroll_test_line(text, "t", "%s", test);
        ir_unroll_test_line(text, "", shape->inverted ? "cbr.void {t}, b%" PRIu32 ", b1" : "cbr.void {t}, b1, b%" PRIu32, exit);
    }
    ir_unroll_test_label(text, exit);
    ir_unroll_test_line(text, "o1", "xor.%s {s}, {r}", type);
    ir_unroll_test_line(text, "o2", "xor.%s {o1}, {i}", type);
    ir_unroll_test_line(text, "o3", shape->bits == 64 ? "copy.i64 {o2}" : "zext.i64 {o2}");
    ir_unroll_test_line(text, "", "ret.void {o3}");
}

// The parameters a fixture runs on: small ones (zero, partial and whole main iterations), and windows at the type's
// signed and unsigned limits, where the bound's wrap is the guard's to catch.
static uint32_t ir_unroll_test_parameters(uint32_t bits, uint64_t* parameters, uint32_t capacity)
{
    uint32_t count = 0;
    const uint64_t mask = bits >= 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    const uint64_t sign = (mask >> 1) + 1;
    for (uint64_t p = 0; p < 26 && count < capacity; p += 1)
    {
        parameters[count++] = p;
    }
    for (uint64_t d = 0; d < 24 && count + 3 < capacity; d += 1)
    {
        parameters[count++] = mask - d;
        parameters[count++] = sign - 1 - d;
        parameters[count++] = sign + d;
    }
    return count;
}

// The fixture: its function and counts into the digest; `expected`'s outcome counted once; an unrolled one verified,
// optimized and executed against the original on every parameter.
static void ir_unroll_test_run(IrOptimizeTest* test, const IrFunction* f, IrUnrollOptions options, IrUnrollRefusal expected, uint32_t bits, const char* label)
{
    Arena vector = arena_reserve(UINT64_C(1) << 28);
    Arena scratch = arena_reserve(UINT64_C(1) << 30);
    Arena optimized = arena_reserve(UINT64_C(1) << 28);
    char message[256] = { 0 };
    IrUnrollResult a = { 0 };
    const IrCheck ac = ir_optimize_unroll(f, &vector, &scratch, options, &a);
    snprintf(message, sizeof(message), "%s: the unroller accepts the fixture and returns its scratch", label);
    ir_optimize_test_expect(test, ac.error == IR_OK && scratch.length == 0, message);
    if (ac.error == IR_OK)
    {
        ir_optimize_test_fold_function(&test->unroll_digest, &a.function);
        for (uint32_t k = 0; k < IR_UNROLL_OUTCOME_COUNT; k += 1)
        {
            ir_optimize_test_fold(&test->unroll_digest, a.outcomes[k]);
        }
        ir_optimize_test_fold(&test->unroll_digest, a.copies);
        ir_optimize_test_fold(&test->unroll_digest, a.reductions);
        ir_optimize_test_fold(&test->unroll_digest, a.added);
    }
    snprintf(message, sizeof(message), "%s: the loop's outcome is %s (unrolled %u)", label, ir_unroll_outcome_names[expected], a.outcomes[IR_UNROLL_UNROLLED]);
    ir_optimize_test_expect(test, a.outcomes[expected] == 1, message);
    if (ac.error == IR_OK && a.outcomes[IR_UNROLL_UNROLLED] == 0)
    {
        snprintf(message, sizeof(message), "%s: a refused loop leaves the input itself", label);
        ir_optimize_test_expect(test, a.function.op == f->op && vector.length == 0, message);
    }
    if (ac.error == IR_OK && a.outcomes[IR_UNROLL_UNROLLED] != 0)
    {
        const IrCheck verified = ir_optimize_verify_result(&a.function, &optimized, &scratch);
        arena_pop_to(&optimized, 0);
        snprintf(message, sizeof(message), "%s: the unrolled function verifies with dominance (error %d at %u)", label, (int)verified.error, verified.instruction);
        ir_optimize_test_expect(test, verified.error == IR_OK, message);
        IrOptimizeOptions cleanup = ir_optimize_options();
        cleanup.passes = BUSTER_IR_OPTIMIZE_SCCP | BUSTER_IR_OPTIMIZE_DCE | BUSTER_IR_OPTIMIZE_CFG | BUSTER_IR_OPTIMIZE_MERGE | BUSTER_IR_OPTIMIZE_IDENTITY;
        IrOptimizeResult clean = { 0 };
        const IrCheck cleaned = verified.error == IR_OK ? ir_optimize(&a.function, &optimized, &scratch, cleanup, &clean) : verified;
        snprintf(message, sizeof(message), "%s: Tier 1's optimizer takes the unrolled function (error %d)", label, (int)cleaned.error);
        ir_optimize_test_expect(test, cleaned.error == IR_OK, message);
        uint64_t parameters[128];
        const uint32_t count = ir_unroll_test_parameters(bits, parameters, 128);
        bool executed = true;
        uint64_t first_failure = 0;
        uint32_t returned = 0;
        for (uint32_t k = 0; k < count; k += 1)
        {
            const IrOptimizeExecution original = ir_optimize_test_execute(f, &scratch, parameters[k], 0);
            const IrOptimizeExecution unrolled = ir_optimize_test_execute(&a.function, &scratch, parameters[k], 0);
            const IrOptimizeExecution cleaned_run = cleaned.error == IR_OK ? ir_optimize_test_execute(&clean.function, &scratch, parameters[k], 0) : unrolled;
            // A loop the original never leaves (a non-strict test of the type's limit) runs past the step bound in both.
            const bool agree = original.status == 1 ? unrolled.status == 1 && original.value == unrolled.value && original.effects == unrolled.effects && cleaned_run.status == 1 &&
                                                          cleaned_run.value == original.value && cleaned_run.effects == original.effects :
                                                      unrolled.status == original.status;
            first_failure = executed && !agree ? parameters[k] : first_failure;
            executed = executed && agree;
            returned += original.status == 1 ? 1u : 0u;
        }
        // The small parameters' loops all return: the comparison is of values, not of two faults.
        snprintf(message, sizeof(message), "%s: the original returns on the small parameters (%u of %u runs)", label, returned, count);
        ir_optimize_test_expect(test, returned >= 26, message);
        snprintf(message, sizeof(message), "%s: the unrolled function executes as the original (first difference at parameter %" PRIu64 ")", label, first_failure);
        ir_optimize_test_expect(test, executed, message);
    }
    arena_release(&vector);
    arena_release(&scratch);
    arena_release(&optimized);
}

static void ir_unroll_test_shape_run(IrOptimizeTest* test, const IrUnrollTestShape* shape, IrUnrollRefusal expected)
{
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 24);
    IrUnrollTestText* text = calloc(1, sizeof(IrUnrollTestText));
    IrFunction f = { 0 };
    char label[192] = { 0 };
    snprintf(label, sizeof(label), "shape i%u %s%s%s form %u step %u start %u bound %u reduction %u sum %u%s", shape->bits, ir_unroll_test_predicates[shape->predicate],
             shape->swapped ? " swapped" : "", shape->inverted ? " inverted" : "", shape->form, shape->step, shape->start, shape->bound, shape->reduction, shape->sum,
             shape->memory ? " memory" : "");
    const bool built = text != NULL;
    if (built)
    {
        ir_unroll_test_shape(text, shape);
    }
    const bool parsed = built && ir_unroll_test_parse(text, &input, &scratch, &f);
    ir_optimize_test_expect(test, parsed, label);
    if (parsed)
    {
        ir_unroll_test_run(test, &f, ir_unroll_options(), expected, shape->bits, label);
    }
    free(text);
    arena_release(&input);
    arena_release(&scratch);
}

// Every form, predicate, order, sense and reduction kind once each at a fixed type and step, then random shapes.
static void ir_unroll_test_shapes(IrOptimizeTest* test, uint32_t random_shapes)
{
    static const uint32_t bits[4] = { 8, 16, 32, 64 };
    static const uint32_t steps[4] = { 1, 2, 3, 7 };
    for (uint32_t form = 0; form < BUSTER_IR_UNROLL_TEST_FORMS; form += 1)
    {
        for (uint32_t predicate = 0; predicate < 4; predicate += 1)
        {
            for (uint32_t order = 0; order < 4; order += 1)
            {
                const IrUnrollTestShape shape = {
                    .bits = 32,
                    .predicate = predicate,
                    .swapped = (order & 1) != 0,
                    .inverted = (order & 2) != 0,
                    .form = form,
                    .step = 1,
                    .start = 0,
                    .bound = 13,
                    .reduction = 0,
                    .sum = order + 1,
                };
                ir_unroll_test_shape_run(test, &shape, IR_UNROLL_UNROLLED);
            }
        }
    }
    for (uint32_t reduction = 0; reduction < BUSTER_IR_UNROLL_TEST_REDUCTIONS; reduction += 1)
    {
        for (uint32_t width = 0; width < 4; width += 1)
        {
            const IrUnrollTestShape shape = {
                .bits = bits[width],
                .predicate = reduction % 4,
                .form = reduction % BUSTER_IR_UNROLL_TEST_FORMS,
                .step = 1,
                .start = 1,
                .bound = 23,
                .reduction = reduction,
            };
            ir_unroll_test_shape_run(test, &shape, IR_UNROLL_UNROLLED);
        }
    }
    for (uint32_t k = 0; k < 4; k += 1)
    {
        // The induction's own address, and its extensions'.
        const IrUnrollTestShape shape = {
            .bits = bits[k],
            .predicate = k % 4,
            .form = k == 3 ? BUSTER_IR_UNROLL_TEST_FORM_TOP : k,
            .step = 1,
            .start = 0,
            .bound = 14,
            .reduction = 0,
            .memory = true,
        };
        ir_unroll_test_shape_run(test, &shape, IR_UNROLL_UNROLLED);
    }
    for (uint32_t k = 0; k < random_shapes; k += 1)
    {
        const uint64_t r = ir_optimize_test_random(test);
        const uint32_t start = (uint32_t)((r >> 30) % 6);
        const IrUnrollTestShape shape = {
            .bits = bits[r % 4],
            .predicate = (uint32_t)((r >> 2) % 4),
            .swapped = ((r >> 4) & 1) != 0,
            .inverted = ((r >> 5) & 1) != 0,
            .form = (uint32_t)((r >> 6) % BUSTER_IR_UNROLL_TEST_FORMS),
            .step = steps[(r >> 10) % 4],
            .start = start,
            .bound = start + (uint32_t)((r >> 14) % 40),
            .reduction = (uint32_t)((r >> 20) % BUSTER_IR_UNROLL_TEST_REDUCTIONS),
            .memory = ((r >> 26) & 1) != 0,
            .sum = (uint32_t)((r >> 36) % 6),
        };
        ir_unroll_test_shape_run(test, &shape, IR_UNROLL_UNROLLED);
    }
}

// The independent executor's store trace. The fixture writes i at byte addresses 65, 61, ..., 1; its 18th store faults.
static uint64_t ir_unroll_test_countdown_trace(uint32_t start, uint32_t writes)
{
    uint64_t trace = 0;
    for (uint32_t k = 0; k < writes && k < 17; k += 1)
    {
        const uint32_t address = 65u - 4u * k;
        trace = trace * 65537u + address * 17u + (start - k);
    }
    return trace;
}

static void ir_unroll_test_countdown_runs(IrOptimizeTest* test, IrFunction* unrolled, Arena* scratch, uint32_t factor)
{
    uint64_t parameters[128];
    const uint32_t count = ir_unroll_test_parameters(32, parameters, BUSTER_ARRAY_LENGTH(parameters));
    for (uint32_t probe = 0; probe < 2; probe += 1)
    {
        if (probe != 0)
        {
            // Return the induction at the original header. This observes the real chunk guards, including a start
            // in the unsigned upper half that an incorrect signed guard would send straight to the remainder.
            const uint32_t branch = unrolled->block_offsets[2] - 1u;
            unrolled->op[branch] = IR_RET;
            unrolled->a[branch] = unrolled->block_offsets[1];
            unrolled->b[branch] = BUSTER_IR_NONE;
            unrolled->c[branch] = BUSTER_IR_NONE;
        }
        for (uint32_t k = 0; k < count; k += 1)
        {
            const uint32_t n = (uint32_t)parameters[k];
            const uint32_t tail = probe != 0 ? n % factor : 0;
            const uint32_t writes = n - tail;
            const IrOptimizeExecution run = ir_optimize_test_execute(unrolled, scratch, n, 0);
            const bool stopped = writes > 17 ? run.status == 2 : run.status == 1 && run.value == tail;
            char label[128];
            snprintf(label, sizeof(label), "countdown from %#010" PRIx32 ", factor %u: ordered stores and %s", n, factor, probe != 0 ? "exact remainder" : "result");
            ir_optimize_test_expect(test, stopped && run.effects == ir_unroll_test_countdown_trace(n, writes), label);
        }
    }
}

static void ir_unroll_test_countdown(IrOptimizeTest* test)
{
    static const struct
    {
        const char* comparison;
        const char* update;
        bool inverted;
        IrUnrollRefusal outcome;
    } cases[] = {
        { "icmp.i1 {i}, {zero} ne", "sub.i32 {i}, {one}", false, IR_UNROLL_UNROLLED },
        { "icmp.i1 {zero}, {i} eq", "add.i32 {minus}, {i}", true, IR_UNROLL_UNROLLED },
        { "icmp.i1 {i}, {one} ne", "sub.i32 {i}, {one}", false, IR_UNROLL_PREDICATE },
        { "icmp.i1 {i}, {zero} ne", "sub.i32 {i}, {two}", false, IR_UNROLL_INDUCTION_REFUSED },
        { "icmp.i1 {i}, {zero} ne", "sub.i32 {one}, {i}", false, IR_UNROLL_INDUCTION_REFUSED },
        { "icmp.i1 {i}, {zero} ne", "add.i32 {i}, {one}", false, IR_UNROLL_INDUCTION_REFUSED },
    };
    Arena input = arena_reserve(UINT64_C(1) << 20);
    Arena output = arena_reserve(UINT64_C(1) << 20);
    Arena scratch = arena_reserve(UINT64_C(1) << 20);
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(cases); k += 1)
    {
        IrUnrollTestText text = { .header = "function (i32) -> i32 {\n  slot $0 size=68 align=4 type=void flags=0x00" };
        ir_unroll_test_label(&text, 0);
        ir_unroll_test_line(&text, "n", "param.i32 0");
        ir_unroll_test_constant(&text, "zero", "i32", 32, 0);
        ir_unroll_test_constant(&text, "one", "i32", 32, 1);
        ir_unroll_test_constant(&text, "two", "i32", 32, 2);
        ir_unroll_test_constant(&text, "minus", "i32", 32, UINT32_MAX);
        ir_unroll_test_constant(&text, "z64", "i64", 64, 0);
        ir_unroll_test_line(&text, "slot", "stack_addr.ptr $0");
        ir_unroll_test_line(&text, "wide", "zext.i64 {n}");
        ir_unroll_test_line(&text, "bias", "sub.i64 {z64}, {wide}");
        ir_unroll_test_line(&text, "base", "address.ptr {slot} + {bias} * 4 offset=68");
        ir_unroll_test_line(&text, "", "br.void b1");
        ir_unroll_test_label(&text, 1);
        ir_unroll_test_line(&text, "i", "phi.i32 [{n}, b0], [{next}, b2]");
        ir_unroll_test_line(&text, "test", "%s", cases[k].comparison);
        ir_unroll_test_line(&text, "", cases[k].inverted ? "cbr.void {test}, b3, b2" : "cbr.void {test}, b2, b3");
        ir_unroll_test_label(&text, 2);
        ir_unroll_test_line(&text, "last", "sub.i32 {i}, {one}");
        ir_unroll_test_line(&text, "idx", "zext.i64 {last}");
        ir_unroll_test_line(&text, "elem", "address.ptr {base} + {idx} * 4 offset=0");
        ir_unroll_test_line(&text, "", "store.void {elem}, {i} align=4");
        ir_unroll_test_line(&text, "next", "%s", cases[k].update);
        ir_unroll_test_line(&text, "", "br.void b1");
        ir_unroll_test_label(&text, 3);
        ir_unroll_test_line(&text, "", "ret.void {i}");
        IrFunction source = { 0 };
        const bool parsed = ir_unroll_test_parse(&text, &input, &scratch, &source);
        ir_optimize_test_expect(test, parsed, "countdown fixture parses");
        IrUnrollOptions options = ir_unroll_options();
        for (uint32_t factor = 2; parsed && factor <= BUSTER_IR_UNROLL_FACTOR; factor *= 2)
        {
            options.factor = factor;
            IrUnrollResult result = { 0 };
            const IrCheck check = ir_optimize_unroll(&source, &output, &scratch, options, &result);
            const bool expected = check.error == IR_OK && result.outcomes[cases[k].outcome] == 1;
            ir_optimize_test_expect(test, expected, "countdown recognition requires the zero test and unit decrement");
            if (expected && cases[k].outcome == IR_UNROLL_UNROLLED)
            {
                const IrCheck verified = ir_optimize_verify_result(&result.function, &input, &scratch);
                ir_optimize_test_expect(test, verified.error == IR_OK && result.copies == factor, "countdown verifies with the requested factor");
                if (verified.error == IR_OK)
                {
                    ir_unroll_test_countdown_runs(test, &result.function, &scratch, factor);
                }
            }
            arena_pop_to(&output, 0);
        }
        arena_pop_to(&input, 0);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

// A refusal fixture's common start: p, the constants, n = p & 15, and the header's phis i (from 0 by one) and s (s + i),
// both taking their latch values from block `latch`.
static void ir_unroll_test_prefix(IrUnrollTestText* text, uint32_t latch, const char* induction_type)
{
    snprintf(text->header, sizeof(text->header), "function (i64) -> i64 {");
    ir_unroll_test_label(text, 0);
    ir_unroll_test_line(text, "p", "param.i64 0");
    ir_unroll_test_constant(text, "zero", "i64", 64, 0);
    ir_unroll_test_constant(text, "one", "i64", 64, 1);
    ir_unroll_test_constant(text, "seven", "i64", 64, 7);
    ir_unroll_test_constant(text, "fifteen", "i64", 64, 15);
    ir_unroll_test_line(text, "n", "and.i64 {p}, {fifteen}");
    ir_unroll_test_line(text, "", "br.void b1");
    ir_unroll_test_label(text, 1);
    ir_unroll_test_line(text, "i", "phi.%s [{zero}, b0], [{i2}, b%" PRIu32 "]", induction_type, latch);
    ir_unroll_test_line(text, "s", "phi.i64 [{zero}, b0], [{s2}, b%" PRIu32 "]", latch);
}

// The latch's end: s + i, i + 1, back to the header; and the exit block returning s.
static void ir_unroll_test_latch(IrUnrollTestText* text, uint32_t exit)
{
    ir_unroll_test_line(text, "s2", "add.i64 {s}, {i}");
    ir_unroll_test_line(text, "i2", "add.i64 {i}, {one}");
    ir_unroll_test_line(text, "", "br.void b1");
    ir_unroll_test_label(text, exit);
    ir_unroll_test_line(text, "", "ret.void {s}");
}

// Each refusal's loop, with one thing wrong; the nested loop's inner loop unrolls beside its refused outer loop.
static void ir_unroll_test_refusals(IrOptimizeTest* test)
{
    for (uint32_t k = 0; k < 16; k += 1)
    {
        IrUnrollTestText* text = calloc(1, sizeof(IrUnrollTestText));
        const char* label = "";
        IrUnrollRefusal expected = IR_UNROLL_UNROLLED;
        if (text == NULL)
        {
            ir_optimize_test_expect(test, false, "refusal fixture allocates");
        }
        else if (k == 0)
        {
            label = "call";
            expected = IR_UNROLL_CALL_REFUSED;
            ir_unroll_test_prefix(text, 2, "i64");
            ir_unroll_test_line(text, "t", "icmp.i1 {i}, {n} ult");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b3");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_line(text, "", "call.void @7() fixed=() variadic=0");
            ir_unroll_test_latch(text, 3);
        }
        else if (k == 1)
        {
            label = "switch";
            expected = IR_UNROLL_CONTROL_REFUSED;
            ir_unroll_test_prefix(text, 3, "i64");
            ir_unroll_test_line(text, "t", "icmp.i1 {i}, {n} ult");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b4");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_line(text, "", "switch.void {p}, default b3, [0x00000000000000000000000000000005, b3]");
            ir_unroll_test_label(text, 3);
            ir_unroll_test_latch(text, 4);
        }
        else if (k == 2 || k == 3 || k == 4)
        {
            // A break, a return and an unreachable inside the loop: exits other than the counted test.
            label = k == 2 ? "break" : k == 3 ? "return" : "unreachable";
            expected = IR_UNROLL_EXIT;
            ir_unroll_test_prefix(text, 3, "i64");
            ir_unroll_test_line(text, "t", "icmp.i1 {i}, {n} ult");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b4");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_line(text, "c", "icmp.i1 {i}, {seven} eq");
            ir_unroll_test_line(text, "", k == 2 ? "cbr.void {c}, b4, b3" : "cbr.void {c}, b5, b3");
            ir_unroll_test_label(text, 3);
            ir_unroll_test_latch(text, 4);
            if (k != 2)
            {
                ir_unroll_test_label(text, 5);
                ir_unroll_test_line(text, "", k == 3 ? "ret.void {i}" : "unreachable.void");
            }
        }
        else if (k == 5)
        {
            label = "alloca";
            expected = IR_UNROLL_STACK_REFUSED;
            ir_unroll_test_prefix(text, 2, "i64");
            ir_unroll_test_line(text, "t", "icmp.i1 {i}, {n} ult");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b3");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_line(text, "a", "alloca.ptr {one}");
            ir_unroll_test_latch(text, 3);
        }
        else if (k == 6 || k == 7)
        {
            // An `ne` test, and a test that continues while the induction is greater.
            label = k == 6 ? "ne test" : "ugt test";
            expected = IR_UNROLL_PREDICATE;
            ir_unroll_test_prefix(text, 2, "i64");
            ir_unroll_test_line(text, "t", k == 6 ? "icmp.i1 {i}, {n} ne" : "icmp.i1 {i}, {n} ugt");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b3");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_latch(text, 3);
        }
        else if (k == 8 || k == 9)
        {
            // A step of the parameter, and a step down.
            label = k == 8 ? "variable step" : "step down";
            expected = IR_UNROLL_INDUCTION_REFUSED;
            ir_unroll_test_prefix(text, 2, "i64");
            ir_unroll_test_line(text, "t", "icmp.i1 {i}, {n} ult");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b3");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_line(text, "s2", "add.i64 {s}, {i}");
            ir_unroll_test_line(text, "i2", k == 8 ? "add.i64 {i}, {p}" : "sub.i64 {i}, {one}");
            ir_unroll_test_line(text, "", "br.void b1");
            ir_unroll_test_label(text, 3);
            ir_unroll_test_line(text, "", "ret.void {s}");
        }
        else if (k == 15)
        {
            // i - 1 tested: a sum of a negative constant.
            label = "test of i - 1";
            expected = IR_UNROLL_INDUCTION_REFUSED;
            ir_unroll_test_prefix(text, 2, "i64");
            ir_unroll_test_constant(text, "minus", "i64", 64, UINT64_MAX);
            ir_unroll_test_line(text, "im", "add.i64 {i}, {minus}");
            ir_unroll_test_line(text, "t", "icmp.i1 {im}, {n} lt");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b3");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_latch(text, 3);
        }
        else if (k == 10)
        {
            label = "test of a non-induction";
            expected = IR_UNROLL_INDUCTION_REFUSED;
            ir_unroll_test_prefix(text, 2, "i64");
            ir_unroll_test_line(text, "t", "icmp.i1 {s}, {n} ult");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b3");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_line(text, "s2", "add.i64 {s}, {i}");
            ir_unroll_test_line(text, "i2", "add.i64 {i}, {one}");
            ir_unroll_test_line(text, "", "br.void b1");
            ir_unroll_test_label(text, 3);
            ir_unroll_test_line(text, "", "ret.void {s}");
        }
        else if (k == 11)
        {
            label = "bound computed in the loop";
            expected = IR_UNROLL_BOUND;
            ir_unroll_test_prefix(text, 2, "i64");
            ir_unroll_test_line(text, "m", "add.i64 {n}, {p}");
            ir_unroll_test_line(text, "t", "icmp.i1 {i}, {m} ult");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b3");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_latch(text, 3);
        }
        else if (k == 12)
        {
            label = "branch on a select";
            expected = IR_UNROLL_TEST;
            ir_unroll_test_prefix(text, 2, "i64");
            ir_unroll_test_line(text, "c", "icmp.i1 {i}, {n} ult");
            ir_unroll_test_line(text, "t", "select.i1 {c}, {c}, {c}");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b3");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_latch(text, 3);
        }
        else if (k == 13)
        {
            // Two back edges: a `continue` straight to the header.
            label = "two latches";
            expected = IR_UNROLL_LATCH;
            snprintf(text->header, sizeof(text->header), "function (i64) -> i64 {");
            ir_unroll_test_label(text, 0);
            ir_unroll_test_line(text, "p", "param.i64 0");
            ir_unroll_test_constant(text, "zero", "i64", 64, 0);
            ir_unroll_test_constant(text, "one", "i64", 64, 1);
            ir_unroll_test_constant(text, "seven", "i64", 64, 7);
            ir_unroll_test_constant(text, "fifteen", "i64", 64, 15);
            ir_unroll_test_line(text, "n", "and.i64 {p}, {fifteen}");
            ir_unroll_test_line(text, "", "br.void b1");
            ir_unroll_test_label(text, 1);
            ir_unroll_test_line(text, "i", "phi.i64 [{zero}, b0], [{i2}, b2], [{i2}, b3]");
            ir_unroll_test_line(text, "s", "phi.i64 [{zero}, b0], [{s}, b2], [{s2}, b3]");
            ir_unroll_test_line(text, "t", "icmp.i1 {i}, {n} ult");
            ir_unroll_test_line(text, "", "cbr.void {t}, b2, b4");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_line(text, "i2", "add.i64 {i}, {one}");
            ir_unroll_test_line(text, "c", "icmp.i1 {i}, {seven} eq");
            ir_unroll_test_line(text, "", "cbr.void {c}, b1, b3");
            ir_unroll_test_label(text, 3);
            ir_unroll_test_line(text, "s2", "add.i64 {s}, {i}");
            ir_unroll_test_line(text, "", "br.void b1");
            ir_unroll_test_label(text, 4);
            ir_unroll_test_line(text, "", "ret.void {s}");
        }
        else
        {
            // A header entered from two outside blocks.
            label = "two entries";
            expected = IR_UNROLL_PREHEADER;
            snprintf(text->header, sizeof(text->header), "function (i64) -> i64 {");
            ir_unroll_test_label(text, 0);
            ir_unroll_test_line(text, "p", "param.i64 0");
            ir_unroll_test_constant(text, "zero", "i64", 64, 0);
            ir_unroll_test_constant(text, "one", "i64", 64, 1);
            ir_unroll_test_constant(text, "fifteen", "i64", 64, 15);
            ir_unroll_test_line(text, "n", "and.i64 {p}, {fifteen}");
            ir_unroll_test_line(text, "b", "icmp.i1 {n}, {one} ult");
            ir_unroll_test_line(text, "", "cbr.void {b}, b1, b2");
            ir_unroll_test_label(text, 1);
            ir_unroll_test_line(text, "", "br.void b3");
            ir_unroll_test_label(text, 2);
            ir_unroll_test_line(text, "", "br.void b3");
            ir_unroll_test_label(text, 3);
            ir_unroll_test_line(text, "i", "phi.i64 [{zero}, b1], [{one}, b2], [{i2}, b4]");
            ir_unroll_test_line(text, "s", "phi.i64 [{zero}, b1], [{zero}, b2], [{s2}, b4]");
            ir_unroll_test_line(text, "t", "icmp.i1 {i}, {n} ult");
            ir_unroll_test_line(text, "", "cbr.void {t}, b4, b5");
            ir_unroll_test_label(text, 4);
            ir_unroll_test_line(text, "s2", "add.i64 {s}, {i}");
            ir_unroll_test_line(text, "i2", "add.i64 {i}, {one}");
            ir_unroll_test_line(text, "", "br.void b3");
            ir_unroll_test_label(text, 5);
            ir_unroll_test_line(text, "", "ret.void {s}");
        }
        Arena input = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        IrFunction f = { 0 };
        const bool parsed = text != NULL && ir_unroll_test_parse(text, &input, &scratch, &f);
        ir_optimize_test_expect(test, parsed, label);
        if (parsed)
        {
            ir_unroll_test_run(test, &f, ir_unroll_options(), expected, 64, label);
        }
        arena_release(&input);
        arena_release(&scratch);
        free(text);
    }
    // A loop around a counted one: the outer is refused, the inner unrolled.
    IrUnrollTestText* text = calloc(1, sizeof(IrUnrollTestText));
    if (text != NULL)
    {
        snprintf(text->header, sizeof(text->header), "function (i64) -> i64 {");
        ir_unroll_test_label(text, 0);
        ir_unroll_test_line(text, "p", "param.i64 0");
        ir_unroll_test_constant(text, "zero", "i64", 64, 0);
        ir_unroll_test_constant(text, "one", "i64", 64, 1);
        ir_unroll_test_constant(text, "fifteen", "i64", 64, 15);
        ir_unroll_test_line(text, "n", "and.i64 {p}, {fifteen}");
        ir_unroll_test_line(text, "", "br.void b1");
        ir_unroll_test_label(text, 1);
        ir_unroll_test_line(text, "i", "phi.i64 [{zero}, b0], [{i2}, b4]");
        ir_unroll_test_line(text, "s", "phi.i64 [{zero}, b0], [{j_s}, b4]");
        ir_unroll_test_line(text, "t", "icmp.i1 {i}, {n} ult");
        ir_unroll_test_line(text, "", "cbr.void {t}, b2, b5");
        ir_unroll_test_label(text, 2);
        ir_unroll_test_line(text, "", "br.void b3");
        ir_unroll_test_label(text, 3);
        ir_unroll_test_line(text, "j", "phi.i64 [{zero}, b2], [{j2}, b3]");
        ir_unroll_test_line(text, "js", "phi.i64 [{s}, b2], [{j_s}, b3]");
        ir_unroll_test_line(text, "jx", "mul.i64 {j}, {i}");
        ir_unroll_test_line(text, "j_s", "add.i64 {js}, {jx}");
        ir_unroll_test_line(text, "j2", "add.i64 {j}, {one}");
        ir_unroll_test_line(text, "jt", "icmp.i1 {j2}, {i} ule");
        ir_unroll_test_line(text, "", "cbr.void {jt}, b3, b4");
        ir_unroll_test_label(text, 4);
        ir_unroll_test_line(text, "i2", "add.i64 {i}, {one}");
        ir_unroll_test_line(text, "", "br.void b1");
        ir_unroll_test_label(text, 5);
        ir_unroll_test_line(text, "", "ret.void {s}");
        Arena input = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        IrFunction f = { 0 };
        const bool parsed = ir_unroll_test_parse(text, &input, &scratch, &f);
        ir_optimize_test_expect(test, parsed, "nested");
        if (parsed)
        {
            ir_unroll_test_run(test, &f, ir_unroll_options(), IR_UNROLL_OUTER, 64, "nested: the outer loop");
            ir_unroll_test_run(test, &f, ir_unroll_options(), IR_UNROLL_UNROLLED, 64, "nested: the inner loop");
        }
        arena_release(&input);
        arena_release(&scratch);
        free(text);
    }
}

static void ir_unroll_test_rollback(IrOptimizeTest* test)
{
    // The first shape's whole need, then every smaller output arena: each fails cleanly or succeeds alike.
    const IrUnrollTestShape shape = {
        .bits = 64,
        .predicate = 0,
        .form = BUSTER_IR_UNROLL_TEST_FORM_TOP,
        .step = 1,
        .bound = 30,
        .reduction = 0,
        .memory = true,
    };
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    IrUnrollTestText* text = calloc(1, sizeof(IrUnrollTestText));
    IrFunction f = { 0 };
    if (text != NULL)
    {
        ir_unroll_test_shape(text, &shape);
    }
    const bool parsed = text != NULL && ir_unroll_test_parse(text, &input, &scratch, &f);
    ir_optimize_test_expect(test, parsed, "rollback fixture parses");
    if (parsed)
    {
        Arena whole = arena_reserve(UINT64_C(1) << 24);
        IrUnrollResult reference = { 0 };
        const IrCheck full = ir_optimize_unroll(&f, &whole, &scratch, ir_unroll_options(), &reference);
        const uint64_t need = whole.length;
        ir_optimize_test_expect(test, full.error == IR_OK && reference.outcomes[IR_UNROLL_UNROLLED] == 1 && need != 0, "rollback fixture unrolls");
        bool clean = true;
        for (uint64_t capacity = 64; full.error == IR_OK && capacity < need; capacity += 64)
        {
            Arena small = arena_reserve_small(capacity, capacity);
            IrUnrollResult result = { 0 };
            const IrCheck check = ir_optimize_unroll(&f, &small, &scratch, ir_unroll_options(), &result);
            clean = clean && (check.error == IR_ERROR_MEMORY ? small.length == 0 : check.error == IR_OK) && scratch.length == 0;
            arena_release(&small);
        }
        ir_optimize_test_expect(test, clean, "every short output arena fails with nothing left in it, or unrolls");
        // The same arena for output and scratch is refused.
        IrUnrollResult same = { 0 };
        const IrCheck shared = ir_optimize_unroll(&f, &scratch, &scratch, ir_unroll_options(), &same);
        ir_optimize_test_expect(test, shared.error == IR_ERROR_STRUCTURE && scratch.length == 0, "one arena for output and scratch is refused");
        arena_release(&whole);
    }
    free(text);
    arena_release(&input);
    arena_release(&scratch);
}

// One operand batch crosses the signed gather boundary in each column. The
// holes are inaccessible so literals and absent optional values cannot be read.
static void ir_unroll_test_wide_remap(IrOptimizeTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t span = (UINT64_C(1) << 32) * sizeof(uint32_t);
    uint8_t* storage = mmap(NULL, span + 2 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    bool ready = storage != MAP_FAILED;
    if (ready)
    {
        uint8_t* start = storage + page;
        ready = mprotect(start, page, PROT_READ | PROT_WRITE) == 0 &&
                mprotect(start + span / 2 - page, 2 * page, PROT_READ | PROT_WRITE) == 0 &&
                mprotect(start + span - page, page, PROT_READ | PROT_WRITE) == 0;
    }
    ir_optimize_test_expect(test, ready, "wide remap reserves its sparse operand map");
    if (ready)
    {
        uint32_t* map = (uint32_t*)(storage + page);
        const uint32_t low = 3, before = INT32_MAX, after = UINT32_C(1) << 31, last = UINT32_MAX - 1;
        map[low] = 11;
        map[before] = 22;
        map[after] = 33;
        map[last] = 44;
        uint8_t op[] = { IR_FMA, IR_ADDRESS, IR_CALL, IR_RET, IR_INTRINSIC, IR_ADDRESS };
        uint8_t type[] = { IR_V4I32, IR_PTR, IR_VOID, IR_VOID, IR_I32, IR_PTR };
        uint8_t flags[] = { 1, 2, 3, 4, 5, 6 };
        uint8_t align[] = { 0, 1, 2, 3, 4, 5 };
        uint32_t a[] = { low, after, 123456, BUSTER_IR_NONE, last, before };
        uint32_t b[] = { before, BUSTER_IR_NONE, 234567, 345678, after, last };
        uint32_t c[] = { last, 987654, after, 876543, 765432, 654321 };
        const uint32_t expected_a[] = { 11, 33, 123456, BUSTER_IR_NONE, 44, 22 };
        const uint32_t expected_b[] = { 22, BUSTER_IR_NONE, 234567, 345678, 33, 44 };
        const uint32_t expected_c[] = { 44, 987654, 33, 876543, 765432, 654321 };
        IrFunction input = { .instruction_count = UINT32_MAX, .op = op, .type = type, .flags = flags, .alignment_log2 = align, .a = a, .b = b, .c = c };
        uint8_t result_bytes[4][8];
        uint32_t result_values[3][8];
        IrFunction output = { .op = result_bytes[0], .type = result_bytes[1], .flags = result_bytes[2], .alignment_log2 = result_bytes[3],
                              .a = result_values[0], .b = result_values[1], .c = result_values[2] };
        for (uint32_t count = 0; count <= 6; count += 1)
        {
            memset(result_bytes, 0xa5, sizeof(result_bytes));
            memset(result_values, 0xa5, sizeof(result_values));
            const bool extras = ir_unroll_remap(&input, 0, count, map, &output, 1);
            ir_optimize_test_expect(test, extras == (count >= 3), "only slices reaching the call or intrinsic can own extra operands");
            const bool values = memcmp(output.a + 1, expected_a, count * sizeof(uint32_t)) == 0 && memcmp(output.b + 1, expected_b, count * sizeof(uint32_t)) == 0 &&
                                memcmp(output.c + 1, expected_c, count * sizeof(uint32_t)) == 0;
            const bool columns = memcmp(output.op + 1, op, count) == 0 && memcmp(output.type + 1, type, count) == 0 &&
                                 memcmp(output.flags + 1, flags, count) == 0 && memcmp(output.alignment_log2 + 1, align, count) == 0;
            bool edges = true;
            for (uint32_t col = 0; col < 4; col += 1)
            {
                edges = edges && result_bytes[col][0] == 0xa5 && result_bytes[col][count + 1] == 0xa5;
            }
            for (uint32_t col = 0; col < 3; col += 1)
            {
                edges = edges && result_values[col][0] == UINT32_C(0xa5a5a5a5) && result_values[col][count + 1] == UINT32_C(0xa5a5a5a5);
            }
            ir_optimize_test_expect(test, values && columns && edges, "wide remap gathers values and preserves literals, absent values and padding");
        }
    }
    if (storage != MAP_FAILED)
    {
        munmap(storage, span + 2 * page);
    }
}

// The budgets: a factor of 1 refuses every loop as too large, a growth of 0 as past the function's, and a factor of 2
// gives 2 copies.
static void ir_unroll_test_limits(IrOptimizeTest* test)
{
    const IrUnrollTestShape shape = {
        .bits = 64,
        .predicate = 2,
        .form = BUSTER_IR_UNROLL_TEST_FORM_SINGLE,
        .step = 1,
        .bound = 30,
        .reduction = 5,
    };
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 24);
    IrUnrollTestText* text = calloc(1, sizeof(IrUnrollTestText));
    IrFunction f = { 0 };
    if (text != NULL)
    {
        ir_unroll_test_shape(text, &shape);
    }
    const bool parsed = text != NULL && ir_unroll_test_parse(text, &input, &scratch, &f);
    ir_optimize_test_expect(test, parsed, "limits fixture parses");
    if (parsed)
    {
        IrUnrollOptions options = ir_unroll_options();
        options.factor = 1;
        ir_unroll_test_run(test, &f, options, IR_UNROLL_SIZE, 64, "a factor of 1");
        options = ir_unroll_options();
        options.budget = 1;
        ir_unroll_test_run(test, &f, options, IR_UNROLL_SIZE, 64, "a budget below two copies");
        options = ir_unroll_options();
        options.growth = 0;
        ir_unroll_test_run(test, &f, options, IR_UNROLL_GROWTH_REFUSED, 64, "no growth");
        options = ir_unroll_options();
        options.factor = 2;
        Arena output = arena_reserve(UINT64_C(1) << 24);
        IrUnrollResult result = { 0 };
        const IrCheck check = ir_optimize_unroll(&f, &output, &scratch, options, &result);
        ir_optimize_test_expect(test, check.error == IR_OK && result.copies == 2 && result.reductions == 1, "a factor of 2 makes two copies and splits the reduction in two");
        ir_unroll_test_run(test, &f, options, IR_UNROLL_UNROLLED, 64, "a factor of 2");
        options.factor = 64;
        options.budget = 1u << 20;
        ir_unroll_test_run(test, &f, options, IR_UNROLL_UNROLLED, 64, "a factor of 64");
        options = ir_unroll_options();
        options.accumulators = 1;
        arena_pop_to(&output, 0);
        const IrCheck single = ir_optimize_unroll(&f, &output, &scratch, options, &result);
        ir_optimize_test_expect(test, single.error == IR_OK && result.outcomes[IR_UNROLL_UNROLLED] == 1 && result.reductions == 0, "one accumulator splits nothing");
        ir_unroll_test_run(test, &f, options, IR_UNROLL_UNROLLED, 64, "one accumulator");
        arena_release(&output);
    }
    free(text);
    arena_release(&input);
    arena_release(&scratch);
}

// #2924: the opcode classes, a range's union of them and the operand remap against their definitions, on random
// columns: every length to 200, every range of up to 130 rows at several places, and remaps of 1 to 40 rows.
static void ir_unroll_test_scans(IrOptimizeTest* test)
{
    IrOptimizeTestScan scan;
    uint8_t classes[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint8_t op[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint8_t type[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint8_t flags[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint8_t alignment[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint32_t a[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint32_t b[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint32_t c[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    uint32_t map[8];
    bool classified = true;
    bool unions = true;
    bool remapped = true;
    for (uint32_t round = 0; round < 32; round += 1)
    {
        ir_optimize_test_scan_fill(test, &scan);
        IrFunction f = scan.function;
        uint8_t alignments[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
        // A fixed operand is always a value; an optional one may be absent.
        uint32_t* const columns[3] = { scan.a, scan.b, scan.c };
        for (uint32_t i = 0; i < BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS; i += 1)
        {
            alignments[i] = (uint8_t)(scan.flags[i] % 7);
            for (uint32_t k = 0; k < 3; k += 1)
            {
                columns[k][i] = (ir_operand_table[scan.op[i]] >> k & 1) != 0 && columns[k][i] == BUSTER_IR_NONE ? k : columns[k][i];
            }
        }
        f.alignment_log2 = alignments;
        for (uint32_t length = round % 9; length <= BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS; length += 13)
        {
            f.instruction_count = length;
            memset(classes, 0xa5, sizeof(classes));
            const bool phis = ir_unroll_classify(&f, classes);
            bool expected_phis = false;
            bool same = true;
            for (uint32_t i = 0; i < length; i += 1)
            {
                same = same && classes[i] == ir_unroll_classes[f.op[i]];
                expected_phis = expected_phis || (ir_unroll_classes[f.op[i]] & BUSTER_IR_UNROLL_PHI) != 0;
            }
            for (uint32_t i = length; i < BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS; i += 1)
            {
                same = same && classes[i] == 0xa5;
            }
            classified &= same && phis == expected_phis;
        }
        f.instruction_count = BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS;
        for (uint32_t i = 0; i < BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS; i += 1)
        {
            classes[i] = ir_unroll_classes[f.op[i]];
        }
        for (uint32_t first = round % 5; first < BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS; first += 31)
        {
            for (uint32_t end = first; end <= BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS && end - first <= 130; end += 1)
            {
                uint32_t expected = 0;
                for (uint32_t i = first; i < end; i += 1)
                {
                    expected |= classes[i];
                }
                unions &= ir_unroll_block_classes(classes, first, end) == expected;
            }
        }
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(map); k += 1)
        {
            map[k] = 1000 + k * 7 + round;
        }
        for (uint32_t count = 1; count <= 40; count += 1)
        {
            const uint32_t first = (round * 11 + count) % (BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS - 40);
            const uint32_t out = (round + count) % 5;
            memset(op, 0xa5, sizeof(op));
            memset(type, 0xa5, sizeof(type));
            memset(flags, 0xa5, sizeof(flags));
            memset(alignment, 0xa5, sizeof(alignment));
            memset(a, 0xa5, sizeof(a));
            memset(b, 0xa5, sizeof(b));
            memset(c, 0xa5, sizeof(c));
            IrFunction view = {
                .op = op,
                .type = type,
                .flags = flags,
                .alignment_log2 = alignment,
                .a = a,
                .b = b,
                .c = c,
            };
            const bool extras = ir_unroll_remap(&f, first, count, map, &view, out);
            bool expected_extras = false;
            bool same = true;
            for (uint32_t k = 0; k < count; k += 1)
            {
                const uint32_t i = first + k;
                const uint32_t mask = ir_value_mask(&f, i);
                const IrOp code = (IrOp)f.op[i];
                expected_extras |= code == IR_PHI || code == IR_CALL || code == IR_CALL_INDIRECT || code == IR_SCATTER || code == IR_INTRINSIC;
                same = same && op[out + k] == f.op[i] && type[out + k] == f.type[i] && flags[out + k] == f.flags[i] && alignment[out + k] == f.alignment_log2[i];
                same = same && a[out + k] == ((mask & 1) != 0 ? map[f.a[i]] : f.a[i]);
                same = same && b[out + k] == ((mask & 2) != 0 ? map[f.b[i]] : f.b[i]);
                same = same && c[out + k] == ((mask & 4) != 0 ? map[f.c[i]] : f.c[i]);
            }
            same = same && (out == 0 || a[out - 1] == UINT32_C(0xa5a5a5a5)) && a[out + count] == UINT32_C(0xa5a5a5a5);
            remapped &= same && extras == expected_extras;
        }
    }
    ir_optimize_test_expect(test, classified, "the unroller's opcode classes, against their definition");
    ir_optimize_test_expect(test, unions, "the unroller's union of a range's classes, against its definition");
    ir_optimize_test_expect(test, remapped, "the unroller's operand remap, against its definition");
}

// A conditional entry's other edge still belongs to its original block, including the bypass phi's incoming edge.
// The recurrence is deliberately not a reduction, so copying its carried value in order matters too.
static void ir_unroll_test_conditional_entry(IrOptimizeTest* test)
{
    for (uint32_t inverted = 0; inverted < 2; inverted += 1)
    {
        IrUnrollTestText text = { 0 };
        snprintf(text.header, sizeof(text.header), "function (i64) -> i64 {");
        ir_unroll_test_label(&text, 0);
        ir_unroll_test_line(&text, "p", "param.i64 0");
        ir_unroll_test_constant(&text, "zero", "i64", 64, 0);
        ir_unroll_test_constant(&text, "one", "i64", 64, 1);
        ir_unroll_test_constant(&text, "three", "i64", 64, 3);
        ir_unroll_test_constant(&text, "mask", "i64", 64, 63);
        ir_unroll_test_constant(&text, "seed", "i64", 64, 7);
        ir_unroll_test_constant(&text, "bypass", "i64", 64, 0x5a5a);
        ir_unroll_test_line(&text, "n", "and.i64 {p}, {mask}");
        ir_unroll_test_line(&text, "low", "and.i64 {p}, {one}");
        ir_unroll_test_line(&text, "entry", inverted != 0 ? "icmp.i1 {low}, {zero} eq" : "icmp.i1 {low}, {zero} ne");
        ir_unroll_test_line(&text, "", inverted != 0 ? "cbr.void {entry}, b3, b1" : "cbr.void {entry}, b1, b3");
        ir_unroll_test_label(&text, 1);
        ir_unroll_test_line(&text, "i", "phi.i64 [{zero}, b0], [{next}, b2]");
        ir_unroll_test_line(&text, "r", "phi.i64 [{seed}, b0], [{sum}, b2]");
        ir_unroll_test_line(&text, "test", "icmp.i1 {i}, {n} ult");
        ir_unroll_test_line(&text, "", "cbr.void {test}, b2, b3");
        ir_unroll_test_label(&text, 2);
        ir_unroll_test_line(&text, "product", "mul.i64 {r}, {three}");
        ir_unroll_test_line(&text, "sum", "add.i64 {product}, {i}");
        ir_unroll_test_line(&text, "next", "add.i64 {i}, {one}");
        ir_unroll_test_line(&text, "", "br.void b1");
        ir_unroll_test_label(&text, 3);
        ir_unroll_test_line(&text, "result", "phi.i64 [{bypass}, b0], [{r}, b1]");
        ir_unroll_test_line(&text, "", "ret.void {result}");
        Arena input = arena_reserve(UINT64_C(1) << 24);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        IrFunction function = { 0 };
        const bool parsed = ir_unroll_test_parse(&text, &input, &scratch, &function);
        ir_optimize_test_expect(test, parsed, "a conditional entry with a bypass phi parses");
        if (parsed)
        {
            ir_unroll_test_run(test, &function, ir_unroll_options(), IR_UNROLL_UNROLLED, 64,
                inverted != 0 ? "conditional false-edge entry and bypass phi" : "conditional true-edge entry and bypass phi");
        }
        arena_release(&input);
        arena_release(&scratch);
    }
}

// Both conditional edges can enter an admitted loop. Splitting the second must keep the first one's new edge.
static void ir_unroll_test_shared_entry(IrOptimizeTest* test)
{
    IrUnrollTestText text = { 0 };
    snprintf(text.header, sizeof(text.header), "function (i64) -> i64 {");
    ir_unroll_test_label(&text, 0);
    ir_unroll_test_line(&text, "p", "param.i64 0");
    ir_unroll_test_constant(&text, "zero", "i64", 64, 0);
    ir_unroll_test_constant(&text, "one", "i64", 64, 1);
    ir_unroll_test_constant(&text, "three", "i64", 64, 3);
    ir_unroll_test_constant(&text, "mask", "i64", 64, 63);
    ir_unroll_test_constant(&text, "seed", "i64", 64, 7);
    ir_unroll_test_line(&text, "n", "and.i64 {p}, {mask}");
    ir_unroll_test_line(&text, "low", "and.i64 {p}, {one}");
    ir_unroll_test_line(&text, "entry", "icmp.i1 {low}, {zero} ne");
    ir_unroll_test_line(&text, "", "cbr.void {entry}, b1, b3");
    for (uint32_t side = 0; side < 2; side += 1)
    {
        const uint32_t header = 1 + side * 2;
        const uint32_t body = header + 1;
        char name[16];
        ir_unroll_test_label(&text, header);
        snprintf(name, sizeof(name), "i%u", side);
        ir_unroll_test_line(&text, name, "phi.i64 [{zero}, b0], [{next%u}, b%u]", side, body);
        snprintf(name, sizeof(name), "r%u", side);
        ir_unroll_test_line(&text, name, side == 0 ? "phi.i64 [{zero}, b0], [{sum%u}, b%u]" : "phi.i64 [{seed}, b0], [{sum%u}, b%u]", side, body);
        snprintf(name, sizeof(name), "test%u", side);
        ir_unroll_test_line(&text, name, "icmp.i1 {i%u}, {n} ult", side);
        ir_unroll_test_line(&text, "", "cbr.void {test%u}, b%u, b5", side, body);
        ir_unroll_test_label(&text, body);
        snprintf(name, sizeof(name), "product%u", side);
        ir_unroll_test_line(&text, name, "mul.i64 {r%u}, {three}", side);
        snprintf(name, sizeof(name), "sum%u", side);
        ir_unroll_test_line(&text, name, "add.i64 {product%u}, {i%u}", side, side);
        snprintf(name, sizeof(name), "next%u", side);
        ir_unroll_test_line(&text, name, "add.i64 {i%u}, {one}", side);
        ir_unroll_test_line(&text, "", "br.void b%u", header);
    }
    ir_unroll_test_label(&text, 5);
    ir_unroll_test_line(&text, "result", "phi.i64 [{r0}, b1], [{r1}, b3]");
    ir_unroll_test_line(&text, "", "ret.void {result}");
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena output = arena_reserve(UINT64_C(1) << 24);
    Arena cleanup = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    IrFunction function = { 0 };
    const bool parsed = ir_unroll_test_parse(&text, &input, &scratch, &function);
    ir_optimize_test_expect(test, parsed, "two loops with one conditional predecessor parse");
    IrUnrollResult result = { 0 };
    const IrCheck check = parsed ? ir_optimize_unroll(&function, &output, &scratch, ir_unroll_options(), &result) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
    const bool unrolled = check.error == IR_OK && result.outcomes[IR_UNROLL_UNROLLED] == 2;
    ir_optimize_test_expect(test, unrolled, "both loops sharing a conditional predecessor unroll");
    if (unrolled)
    {
        const IrCheck verified = ir_optimize_verify_result(&result.function, &cleanup, &scratch);
        arena_pop_to(&cleanup, 0);
        ir_optimize_test_expect(test, verified.error == IR_OK, "both rewritten conditional edges verify with dominance");
        IrOptimizeOptions options = ir_optimize_options();
        options.passes |= BUSTER_IR_OPTIMIZE_MERGE;
        IrOptimizeResult optimized = { 0 };
        const IrCheck cleaned = verified.error == IR_OK ? ir_optimize(&result.function, &cleanup, &scratch, options, &optimized) : verified;
        bool agree = cleaned.error == IR_OK;
        for (uint64_t p = 0; agree && p < 80; p += 1)
        {
            const IrOptimizeExecution before = ir_optimize_test_execute(&function, &scratch, p, 0);
            const IrOptimizeExecution after = ir_optimize_test_execute(&result.function, &scratch, p, 0);
            const IrOptimizeExecution clean = ir_optimize_test_execute(&optimized.function, &scratch, p, 0);
            agree = before.status == 1 && after.status == 1 && clean.status == 1 && before.value == after.value && before.value == clean.value;
        }
        ir_optimize_test_expect(test, agree, "both choices preserve their own recurrence and join value");
    }
    arena_release(&input);
    arena_release(&output);
    arena_release(&cleanup);
    arena_release(&scratch);
}

typedef struct IrUnrollVectorFixture IrUnrollVectorFixture;
struct IrUnrollVectorFixture
{
    const char* name;
    const char* roles;
    // The widest lane determines how many source iterations a vector executes, including widened reductions.
    uint32_t lane_bytes;
    // Bit t says that the fixture vectorizes at target t (64, 32, 16 bytes).
    uint32_t widths;
    uint32_t reductions;
    // Direct vector fixtures store their final accumulator even when no iteration ran.
    bool empty_store;
};

static const IrUnrollVectorFixture ir_unroll_vector_fixtures[] = {
    { .name = "fill-u32", .roles = "pkn", .lane_bytes = 4, .widths = 7 },
    { .name = "copy-u8", .roles = "ppn", .lane_bytes = 1, .widths = 7 },
    { .name = "add-i32", .roles = "pppn", .lane_bytes = 4, .widths = 7 },
    { .name = "sum-uniform-i64", .roles = "kn", .lane_bytes = 8, .widths = 7, .reductions = 1 },
    { .name = "reductions-i32", .roles = "pn", .lane_bytes = 4, .widths = 7, .reductions = 4 },
    { .name = "sum-i32", .roles = "pn", .lane_bytes = 8, .widths = 3, .reductions = 1 },
    { .name = "cond-sum-i32", .roles = "pnk", .lane_bytes = 8, .widths = 3, .reductions = 1 },
    { .name = "minmax-select", .roles = "pnk", .lane_bytes = 4, .widths = 7, .reductions = 1 },
    { .name = "start-offset", .roles = "ppsno", .lane_bytes = 2, .widths = 7 },
    { .name = "axpy-f32", .roles = "ppkn", .lane_bytes = 4, .widths = 7 },
};

// Compare the source with both forms of the composed result at boundaries of one vector and of an unrolled chunk.
// Overlapping pointers take the vectorizer's scalar fallback; disjoint ones reach its main loop and either tail.
// The final placement ends the memory binding at the last element, and an empty input has null pointers.
static bool ir_unroll_test_vector_agree(IrVectorizeTest* random, const IrUnrollVectorFixture* fixture, const IrFunction* before,
    const IrFunction* after, const IrFunction* optimized, uint32_t bytes, uint32_t factor, Arena* execution, uint8_t* buffers)
{
    const uint32_t lanes = bytes / fixture->lane_bytes;
    const uint32_t chunk = lanes * factor;
    const uint32_t lengths[] = {
        0,
        1,
        lanes - 1,
        lanes,
        lanes + 1,
        2 * lanes - 1,
        2 * lanes + 1,
        chunk - 1,
        chunk,
        chunk + 1,
        chunk + lanes,
        2 * chunk - 1,
        2 * chunk,
        2 * chunk + 1,
    };
    const int64_t deltas[] = {
        0,
        1,
        -1,
        (int64_t)fixture->lane_bytes,
        -(int64_t)fixture->lane_bytes,
        (int64_t)bytes,
        -(int64_t)bytes,
        4096,
        0,
    };
    uint8_t* initial = buffers;
    uint8_t* left = buffers + BUSTER_IR_VECTORIZE_TEST_BUFFER;
    uint8_t* right = buffers + 2 * BUSTER_IR_VECTORIZE_TEST_BUFFER;
    const uint32_t count = before->parameter_count;
    bool agree = count <= 16;
    for (uint32_t n = 0; agree && n < BUSTER_ARRAY_LENGTH(lengths); n += 1)
    {
        const uint32_t length = lengths[n];
        const uint32_t extent = length == 0 && fixture->empty_store ? 1 : length;
        for (uint32_t placement = 0; agree && placement < BUSTER_ARRAY_LENGTH(deltas); placement += 1)
        {
            for (uint32_t k = 0; k < BUSTER_IR_VECTORIZE_TEST_BUFFER; k += 8)
            {
                const uint64_t word = ir_vectorize_test_next(random);
                memcpy(initial + k, &word, 8);
            }
            InterpreterValue arguments[16] = { 0 };
            InterpreterValue moved[16] = { 0 };
            const uint64_t start = strchr(fixture->roles, 's') != NULL ? n % 7 : 0;
            const uint64_t offset = strchr(fixture->roles, 'o') != NULL ? placement % 5 : 0;
            uint32_t pointers = 0;
            for (uint32_t k = 0; k < count; k += 1)
            {
                const char role = fixture->roles[k];
                uint64_t value = 0;
                if (role == 'p')
                {
                    const uint64_t place = placement + 1 == BUSTER_ARRAY_LENGTH(deltas) ?
                        BUSTER_IR_VECTORIZE_TEST_BUFFER - (start + extent + offset) * fixture->lane_bytes :
                        (uint64_t)(INT64_C(2048) + (pointers == 0 ? 0 : deltas[placement]));
                    value = length == 0 && !fixture->empty_store ? 0 : (uint64_t)(uintptr_t)(left + place);
                    pointers += 1;
                }
                else if (role == 'n')
                {
                    value = start + length;
                }
                else if (role == 's')
                {
                    value = start;
                }
                else if (role == 'o')
                {
                    value = offset;
                }
                else
                {
                    value = ir_vectorize_test_next(random);
                }
                arguments[k].low = value & ir_vectorize_type_mask(before->parameter_types[k]);
                moved[k] = arguments[k];
                if (role == 'p' && value != 0)
                {
                    moved[k].low = value - (uint64_t)(uintptr_t)left + (uint64_t)(uintptr_t)right;
                }
            }
            memcpy(left, initial, BUSTER_IR_VECTORIZE_TEST_BUFFER);
            memcpy(right, initial, BUSTER_IR_VECTORIZE_TEST_BUFFER);
            const InterpreterResult original = ir_vectorize_test_run(before, left, BUSTER_IR_VECTORIZE_TEST_BUFFER, arguments, count, execution);
            const InterpreterResult unrolled = ir_vectorize_test_run(after, right, BUSTER_IR_VECTORIZE_TEST_BUFFER, moved, count, execution);
            agree = original.status == INTERPRETER_OK && unrolled.status == INTERPRETER_OK &&
                original.value.low == unrolled.value.low && original.value.high == unrolled.value.high &&
                memcmp(left, right, BUSTER_IR_VECTORIZE_TEST_BUFFER) == 0;
            memcpy(right, initial, BUSTER_IR_VECTORIZE_TEST_BUFFER);
            const InterpreterResult cleaned = ir_vectorize_test_run(optimized, right, BUSTER_IR_VECTORIZE_TEST_BUFFER, moved, count, execution);
            agree = agree && cleaned.status == INTERPRETER_OK && original.value.low == cleaned.value.low &&
                original.value.high == cleaned.value.high && memcmp(left, right, BUSTER_IR_VECTORIZE_TEST_BUFFER) == 0;
            if (!agree)
            {
                fprintf(stderr, "ir-unroll: vector %s, %u-byte target, factor %u, length %u, placement %u: interpreter statuses %u/%u/%u\n",
                    fixture->name, bytes, factor, length, placement, original.status, unrolled.status, cleaned.status);
            }
        }
    }
    return agree;
}

static void ir_unroll_test_vectors(IrOptimizeTest* test)
{
    Arena source = arena_reserve(UINT64_C(1) << 24);
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena vector = arena_reserve(UINT64_C(1) << 26);
    Arena output = arena_reserve(UINT64_C(1) << 26);
    Arena cleanup = arena_reserve(UINT64_C(1) << 26);
    Arena scratch = arena_reserve(UINT64_C(1) << 28);
    Arena execution = arena_reserve(UINT64_C(1) << 28);
    uint8_t* buffers = malloc(3 * BUSTER_IR_VECTORIZE_TEST_BUFFER);
    IrVectorizeTest random = { .random = test->random };
    ir_optimize_test_expect(test, buffers != NULL, "composed vector/unroll interpreter buffers allocate");
    for (uint32_t k = 0; buffers != NULL && k < BUSTER_ARRAY_LENGTH(ir_unroll_vector_fixtures); k += 1)
    {
        const IrUnrollVectorFixture* fixture = &ir_unroll_vector_fixtures[k];
        char path[256];
        snprintf(path, sizeof(path), "tests/ir/vectorize/%s.ir", fixture->name);
        String8 text = { 0 };
        IrFunction function = { 0 };
        const bool parsed = ir_optimize_text_read(path, &source, &text) && ir_optimize_text_parse(text, &input, &scratch, &function);
        ir_optimize_test_expect(test, parsed, "a composed vector/unroll source fixture parses");
        for (uint32_t t = 0; parsed && t < 4; t += 1)
        {
            const uint32_t width_bit = 1u << (t == 3 ? 0 : t);
            if ((fixture->widths & width_bit) != 0)
            {
                IrVectorizeTarget target = { 0 };
                ir_vectorize_test_target(t, &target);
                const IrVectorizeOptions vector_options = { .target = &target };
                IrVectorizeResult vectorized = { 0 };
                const IrCheck vc = ir_vectorize_loops(&function, &vector_options, &vector, &scratch, &vectorized);
                char message[256];
                const uint32_t bytes = ir_vectorize_test_bytes(t);
                snprintf(message, sizeof(message), "%s: the vectorizer creates one %u-byte loop%s", fixture->name, bytes, t == 3 ? " with a scalar tail" : "");
                ir_optimize_test_expect(test, vc.error == IR_OK && vectorized.vectorized == 1 && scratch.length == 0, message);
                // The production handoff still contains if-conversion's unreachable stubs. Validate their structure
                // and every reachable SSA use here; the optimizer below removes them before its strict final check.
                const IrCheck handoff = vc.error == IR_OK ? ir_verify_complete(&vectorized.function, &scratch) : vc;
                snprintf(message, sizeof(message), "%s: the raw vectorizer handoff verifies all reachable SSA uses", fixture->name);
                ir_optimize_test_expect(test, handoff.error == IR_OK && handoff.dominance_checked && scratch.length == 0, message);
                for (uint32_t factor = t == 0 ? 2 : 4; handoff.error == IR_OK && vectorized.vectorized == 1 && factor <= (t == 0 ? 8 : 4); factor *= 2)
                {
                    IrUnrollOptions options = ir_unroll_options();
                    options.factor = factor;
                    options.budget = 4096;
                    IrUnrollResult result = { 0 };
                    const IrCheck check = ir_optimize_unroll(&vectorized.function, &output, &scratch, options, &result);
                    snprintf(message, sizeof(message), "%s: %u-byte vector loop unrolls by %u while its scalar fallback stays (error %u, unrolled %u, copies %u, reductions %u)",
                        fixture->name, bytes, factor, check.error, result.outcomes[IR_UNROLL_UNROLLED], result.copies, result.reductions);
                    const bool unrolled = check.error == IR_OK && result.outcomes[IR_UNROLL_UNROLLED] == 1 && result.copies == factor &&
                        result.reductions == fixture->reductions && scratch.length == 0;
                    ir_optimize_test_expect(test, unrolled, message);
                    if (unrolled)
                    {
                        bool vector_body = false;
                        for (uint32_t i = result.function.block_offsets[vectorized.function.block_count]; i < result.function.instruction_count; i += 1)
                        {
                            const bool vector_store = result.function.op[i] == IR_STORE && ir_vector_type(result.function.type[result.function.b[i]]);
                            vector_body = vector_body || ir_vector_type(result.function.type[i]) || vector_store;
                        }
                        ir_optimize_test_expect(test, vector_body, "the new unrolled body contains vector values, rather than another copy of the scalar fallback");
                        const IrCheck verified = ir_verify_complete(&result.function, &scratch);
                        snprintf(message, sizeof(message), "%s: composed result verifies with reachable SSA dominance (error %u at %u)", fixture->name, verified.error, verified.instruction);
                        ir_optimize_test_expect(test, verified.error == IR_OK && verified.dominance_checked && scratch.length == 0, message);
                        IrOptimizeOptions optimize = ir_optimize_options();
                        optimize.passes |= BUSTER_IR_OPTIMIZE_MERGE;
                        IrOptimizeResult optimized = { 0 };
                        const IrCheck cleaned = verified.error == IR_OK ? ir_optimize(&result.function, &cleanup, &scratch, optimize, &optimized) : verified;
                        snprintf(message, sizeof(message), "%s: the optimizer takes the composed result (error %u at %u)", fixture->name, cleaned.error, cleaned.instruction);
                        ir_optimize_test_expect(test, cleaned.error == IR_OK, message);
                        if (cleaned.error == IR_OK)
                        {
                            snprintf(message, sizeof(message), "%s: %u-byte vectors unrolled by %u agree around both boundaries, on overlap and at the memory end", fixture->name, bytes, factor);
                            ir_optimize_test_expect(test, ir_unroll_test_vector_agree(&random, fixture, &function, &result.function, &optimized.function, bytes, factor, &execution, buffers), message);
                        }
                    }
                    arena_pop_to(&output, 0);
                    arena_pop_to(&cleanup, 0);
                }
                arena_pop_to(&vector, 0);
            }
        }
        arena_pop_to(&input, 0);
        arena_pop_to(&source, 0);
    }
    free(buffers);
    arena_release(&source);
    arena_release(&input);
    arena_release(&vector);
    arena_release(&output);
    arena_release(&cleanup);
    arena_release(&scratch);
    arena_release(&execution);
}

// The public IR also admits vector reductions whose lanes the scalar loop vectorizer does not produce. Keep full
// vectors observable through a store, including the initial value when n is zero, and check every lane's bytes.
static void ir_unroll_test_vector_reduction_text(IrUnrollTestText* text, uint32_t width, uint32_t size, uint32_t operation)
{
    const uint32_t bytes = 16u << width;
    const uint32_t bits = 8u << size;
    const char* type = ir_type_names[IR_V4I32 + width];
    const char* scalar = ir_type_names[IR_I8 + size];
    const uint32_t mask_type = ir_lane_mask(IR_V4I32 + width, operation == 8 ? IR_SIMD_LANE_I8 : IR_SIMD_LANE_I32);
    const char* mask = ir_type_names[mask_type];
    snprintf(text->header, sizeof(text->header), "function (ptr, ptr, i64) -> i64 {");
    ir_unroll_test_label(text, 0);
    ir_unroll_test_line(text, "out", "param.ptr 0");
    ir_unroll_test_line(text, "in", "param.ptr 1");
    ir_unroll_test_line(text, "n", "param.i64 2");
    ir_unroll_test_constant(text, "zero", "i64", 64, 0);
    ir_unroll_test_constant(text, "one", "i64", 64, 1);
    ir_unroll_test_constant(text, "bytes", "i64", 64, bytes);
    ir_unroll_test_constant(text, "seed", scalar, bits, operation == 7 ? UINT64_C(0x3f800000) : UINT64_C(0x6d2b79f581c35713));
    ir_unroll_test_line(text, "initial", "intrinsic.%s {seed} splat", type);
    ir_unroll_test_constant(text, "mask", mask, ir_type_bits[mask_type], UINT64_C(0xaaaaaaaaaaaaaaaa));
    ir_unroll_test_line(text, "", "br.void b1");
    ir_unroll_test_label(text, 1);
    ir_unroll_test_line(text, "i", "phi.i64 [{zero}, b0], [{next}, b2]");
    ir_unroll_test_line(text, "r", "phi.%s [{initial}, b0], [{update}, b2]", type);
    ir_unroll_test_line(text, "test", "icmp.i1 {i}, {n} ult");
    ir_unroll_test_line(text, "", "cbr.void {test}, b2, b3");
    ir_unroll_test_label(text, 2);
    ir_unroll_test_line(text, "offset", "mul.i64 {i}, {bytes}");
    ir_unroll_test_line(text, "address", "address.ptr {in} + {offset} * 1 offset=0");
    ir_unroll_test_line(text, "x", "load.%s {address} align=1", type);
    if (operation < 2)
    {
        ir_unroll_test_line(text, "update", "%s.v%ui%u {r}, {x}", operation == 0 ? "sub" : "mul", bytes * 8 / bits, bits);
    }
    else if (operation < 7)
    {
        const uint32_t lane_operation = operation == 6 ? 5 : operation - 2;
        ir_unroll_test_line(text, "update", "intrinsic.%s {r}, {x} lane_op %u", type, lane_operation * 4 + size);
    }
    else if (operation == 7)
    {
        ir_unroll_test_line(text, "update", "fadd.v%uf32 {r}, {x}", bytes / 4);
    }
    else
    {
        ir_unroll_test_line(text, "sum", "add.%s {r}, {x}", type);
        ir_unroll_test_line(text, "update", "vselect.v%ui8 {mask}, {sum}, {r}", bytes);
    }
    ir_unroll_test_line(text, "next", "add.i64 {i}, {one}");
    ir_unroll_test_line(text, "", "br.void b1");
    ir_unroll_test_label(text, 3);
    ir_unroll_test_line(text, "", "store.void {out}, {r} align=1");
    ir_unroll_test_line(text, "", "ret.void {i}");
}

static void ir_unroll_test_vector_reductions(IrOptimizeTest* test)
{
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena output = arena_reserve(UINT64_C(1) << 26);
    Arena cleanup = arena_reserve(UINT64_C(1) << 26);
    Arena scratch = arena_reserve(UINT64_C(1) << 28);
    Arena execution = arena_reserve(UINT64_C(1) << 28);
    uint8_t* buffers = malloc(3 * BUSTER_IR_VECTORIZE_TEST_BUFFER);
    IrVectorizeTest random = { .random = test->random };
    ir_optimize_test_expect(test, buffers != NULL, "direct vector reduction buffers allocate");
    for (uint32_t width = 0; buffers != NULL && width < 3; width += 1)
    {
        for (uint32_t operation = 0; operation < 9; operation += 1)
        {
            const uint32_t first_size = operation >= 7 ? 2 : 0;
            const uint32_t end_size = operation >= 7 ? 3 : operation == 6 ? 2 : 4;
            for (uint32_t size = first_size; size < end_size; size += 1)
            {
                IrUnrollTestText text = { 0 };
                ir_unroll_test_vector_reduction_text(&text, width, size, operation);
                IrFunction function = { 0 };
                const bool parsed = ir_unroll_test_parse(&text, &input, &scratch, &function);
                char name[128];
                snprintf(name, sizeof(name), "direct operation %u, %u-byte vector, %u-bit lanes", operation, 16u << width, 8u << size);
                ir_optimize_test_expect(test, parsed, name);
                IrUnrollOptions options = ir_unroll_options();
                options.factor = 4;
                IrUnrollResult result = { 0 };
                const IrCheck check = parsed ? ir_optimize_unroll(&function, &output, &scratch, options, &result) : ir_check(IR_ERROR_STRUCTURE, 0, 0);
                const bool split = operation < 6;
                const bool unrolled = check.error == IR_OK && result.outcomes[IR_UNROLL_UNROLLED] == 1 && result.copies == 4 &&
                    result.reductions == (split ? 1u : 0u) && result.function.instruction_count == function.instruction_count + result.added && scratch.length == 0;
                char message[256];
                snprintf(message, sizeof(message), "%s: unrolls with %u split reductions and exact growth (error %u, reductions %u)", name, split ? 1u : 0u, check.error, result.reductions);
                ir_optimize_test_expect(test, unrolled, message);
                if (unrolled)
                {
                    const IrCheck verified = ir_optimize_verify_result(&result.function, &cleanup, &scratch);
                    arena_pop_to(&cleanup, 0);
                    snprintf(message, sizeof(message), "%s: verifies with dominance (error %u at %u)", name, verified.error, verified.instruction);
                    ir_optimize_test_expect(test, verified.error == IR_OK, message);
                    IrOptimizeOptions optimize = ir_optimize_options();
                    optimize.passes |= BUSTER_IR_OPTIMIZE_MERGE;
                    IrOptimizeResult optimized = { 0 };
                    const IrCheck cleaned = verified.error == IR_OK ? ir_optimize(&result.function, &cleanup, &scratch, optimize, &optimized) : verified;
                    snprintf(message, sizeof(message), "%s: raw and cleaned vectors preserve every lane's value", name);
                    const IrUnrollVectorFixture fixture = { .name = name, .roles = "ppn", .lane_bytes = 16u << width, .empty_store = true };
                    ir_optimize_test_expect(test, cleaned.error == IR_OK && ir_unroll_test_vector_agree(&random, &fixture, &function, &result.function, &optimized.function, 16u << width, 4, &execution, buffers), message);
                    if (width == 2 && size == 0 && operation == 1)
                    {
                        const uint64_t need = output.length;
                        bool rolled_back = true;
                        for (uint64_t capacity = 64; capacity < need; capacity += 64)
                        {
                            Arena small = arena_reserve_small(capacity, capacity);
                            IrUnrollResult limited = { 0 };
                            const IrCheck short_check = ir_optimize_unroll(&function, &small, &scratch, options, &limited);
                            rolled_back = rolled_back && (short_check.error == IR_ERROR_MEMORY ? small.length == 0 : short_check.error == IR_OK) && scratch.length == 0;
                            arena_release(&small);
                        }
                        ir_optimize_test_expect(test, rolled_back, "short arenas roll back vector identities and conditional guards without retaining scratch");
                        arena_pop_to(&cleanup, 0);
                        options.growth = result.added - 1;
                        IrUnrollResult refused = { 0 };
                        const IrCheck too_large = ir_optimize_unroll(&function, &cleanup, &scratch, options, &refused);
                        ir_optimize_test_expect(test, too_large.error == IR_OK && refused.outcomes[IR_UNROLL_GROWTH_REFUSED] == 1 && refused.function.op == function.op && cleanup.length == 0,
                            "one row below the exact vector growth refuses and borrows the input");
                    }
                }
                arena_pop_to(&input, 0);
                arena_pop_to(&output, 0);
                arena_pop_to(&cleanup, 0);
            }
        }
    }
    free(buffers);
    arena_release(&input);
    arena_release(&output);
    arena_release(&cleanup);
    arena_release(&scratch);
    arena_release(&execution);
}

static __attribute__((unused)) int run_ir_unroll_tests(int argc, char** argv)
{
    IrOptimizeTest test = {
        .random = 2755,
        .unroll_digest = BUSTER_IR_OPTIMIZE_TEST_DIGEST_BASIS,
    };
    uint32_t shapes = 400;
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            test.random = strtoull(argv[++i], &end, 0);
            valid = errno == 0 && *end == 0 && test.random != 0;
        }
        else if (strcmp(argv[i], "--shapes") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            const unsigned long long value = strtoull(argv[++i], &end, 0);
            valid = errno == 0 && *end == 0 && value <= 1000000;
            shapes = (uint32_t)value;
        }
        else
        {
            valid = false;
        }
    }
    int result = 2;
    if (valid)
    {
        const bool frozen = test.random == 2755 && shapes == 400;
        ir_unroll_test_scans(&test);
        ir_unroll_test_shapes(&test, shapes);
        ir_unroll_test_refusals(&test);
        ir_unroll_test_limits(&test);
        ir_unroll_test_wide_remap(&test);
        ir_unroll_test_rollback(&test);
        ir_unroll_test_countdown(&test);
        // The digest's random shapes are the default seed's 400.
        if (frozen)
        {
            ir_optimize_test_expect(&test, test.unroll_digest == BUSTER_IR_OPTIMIZE_TEST_UNROLL_DIGEST, "every fixture's unrolled function and counts have their frozen digest");
            if (test.unroll_digest != BUSTER_IR_OPTIMIZE_TEST_UNROLL_DIGEST)
            {
                fprintf(stderr, "ir-unroll: digest %#018" PRIx64 "\n", test.unroll_digest);
            }
        }
        // New composition checks stay past the original scalar fixtures' frozen digest.
        ir_unroll_test_conditional_entry(&test);
        ir_unroll_test_shared_entry(&test);
        ir_unroll_test_vectors(&test);
        ir_unroll_test_vector_reductions(&test);
        printf("ir-unroll: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
        result = test.failures == 0 ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-unroll [--seed N] [--shapes N]\n");
    }
    return result;
}

#endif
