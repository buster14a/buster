// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #175's C-source text gate, included by ir_source_test.c after ir_parse_test.c. It compares meanings independently of
// the printer: instruction/block/slot/label identities stay fixed, but sparse rows are compared through their uses.
// Text does not retain unused rows, sharing, symbol capacity, padding or an unindexed address's scale. Every other
// published field is checked. This is a scalar test oracle, not a compiler execution path or a performance kernel.
// ir_source_text_roundtrip checks both parsers and canonical reprinting; all temporary output and scratch is returned.
// ir_source_text_tests checks the oracle against fixtures, intentional changes, sparse normalization and exhaustion.

#include <stdlib.h>

_Static_assert(IR_OP_COUNT == 61, "audit the text round-trip oracle when adding an IR operation");

typedef struct IrSourceTextCheck IrSourceTextCheck;
struct IrSourceTextCheck
{
    const char* reason;
    IrParseResult parsed;
};

static bool ir_source_text_bytes(const void* a, const void* b, uint64_t bytes)
{
    return bytes == 0 || memcmp(a, b, (size_t)bytes) == 0;
}

// Do not even form a pointer from a null, empty side table.
static bool ir_source_text_range(const void* a, uint32_t first_a, const void* b, uint32_t first_b, uint64_t count, uint64_t width)
{
    bool equal = true;
    if (count != 0)
    {
        equal = ir_source_text_bytes((const uint8_t*)a + (uint64_t)first_a * width, (const uint8_t*)b + (uint64_t)first_b * width, count * width);
    }
    return equal;
}

static bool ir_source_text_return(const AbiReturn* a, const AbiReturn* b)
{
    return a->kind == b->kind && a->flags == b->flags && a->part_bytes == b->part_bytes &&
           ir_source_text_bytes(a->registers, b->registers, sizeof(a->registers));
}

static bool ir_source_text_abi(const AbiCall* a, const AbiCall* b)
{
    return a->stack_bytes == b->stack_bytes && a->stack_alignment_log2 == b->stack_alignment_log2 &&
           a->integer_registers == b->integer_registers && a->vector_registers == b->vector_registers &&
           ir_source_text_return(&a->result, &b->result);
}

static bool ir_source_text_passed(const IrFunction* a, uint32_t first_a, const IrFunction* b, uint32_t first_b, uint32_t count)
{
    bool equal = true;
    for (uint32_t i = 0; equal && i < count; i += 1)
    {
        const IrPassed* x = &a->passed[first_a + i];
        const IrPassed* y = &b->passed[first_b + i];
        equal = x->size == y->size && x->location.stack_offset == y->location.stack_offset &&
                x->location.kind == y->location.kind && x->location.flags == y->location.flags &&
                ir_source_text_bytes(x->location.registers, y->location.registers, sizeof(x->location.registers));
    }
    return equal;
}

static bool ir_source_text_constant(const IrFunction* a, uint32_t x, const IrFunction* b, uint32_t y)
{
    return a->constants[x].low == b->constants[y].low && a->constants[x].high == b->constants[y].high;
}

static bool ir_source_text_call(const IrFunction* a, uint32_t x, const IrFunction* b, uint32_t y)
{
    const IrCall* left = &a->calls[x];
    const IrCall* right = &b->calls[y];
    bool equal = left->argument_count == right->argument_count && left->parameter_count == right->parameter_count &&
                 left->result_type == right->result_type && left->variadic == right->variadic && (left->abi != 0) == (right->abi != 0);
    equal = equal && ir_source_text_range(a->extra, left->argument_first, b->extra, right->argument_first, left->argument_count, sizeof(uint32_t)) &&
            ir_source_text_range(a->call_types, left->type_first, b->call_types, right->type_first, left->parameter_count, sizeof(uint8_t));
    if (equal && left->abi != 0)
    {
        const IrAbiCall* left_abi = &a->abi_calls[left->abi - 1];
        const IrAbiCall* right_abi = &b->abi_calls[right->abi - 1];
        equal = left_abi->result_size == right_abi->result_size && ir_source_text_abi(&left_abi->call, &right_abi->call) &&
                ir_source_text_passed(a, left_abi->passed, b, right_abi->passed, left->argument_count);
    }
    return equal;
}

// Both views have verified logical extents and metadata references. Unlike comparing printed bytes, this checks the
// producer's operands and metadata even if both printer and parser accidentally agree to discard a meaningful field.
static bool ir_source_text_equal(const IrFunction* a, const IrFunction* b)
{
    bool equal = a->instruction_count == b->instruction_count && a->block_count == b->block_count &&
                 a->slot_count == b->slot_count && a->parameter_count == b->parameter_count && a->result_type == b->result_type &&
                 a->label_count == b->label_count && (a->parameter_passed != 0) == (b->parameter_passed != 0) && a->variadic == b->variadic;
    if (equal)
    {
        const uint64_t count = a->instruction_count;
        equal = ir_source_text_bytes(a->op, b->op, count) && ir_source_text_bytes(a->type, b->type, count) &&
                ir_source_text_bytes(a->flags, b->flags, count) && ir_source_text_bytes(a->alignment_log2, b->alignment_log2, count) &&
                ir_source_text_bytes(a->block_offsets, b->block_offsets, a->block_count != 0 ? ((uint64_t)a->block_count + 1) * sizeof(uint32_t) : 0) &&
                ir_source_text_bytes(a->parameter_types, b->parameter_types, a->parameter_count) &&
                ir_source_text_bytes(a->labels, b->labels, (uint64_t)a->label_count * sizeof(uint32_t));
    }
    if (equal && a->parameter_passed != 0)
    {
        equal = a->result_size == b->result_size && ir_source_text_return(&a->result_abi, &b->result_abi) &&
                ir_source_text_passed(a, a->parameter_passed - 1, b, b->parameter_passed - 1, a->parameter_count);
    }
    if (equal && a->variadic)
    {
        equal = ir_source_text_abi(&a->incoming, &b->incoming);
    }
    for (uint32_t s = 0; equal && s < a->slot_count; s += 1)
    {
        equal = a->slots[s].size == b->slots[s].size && a->slots[s].alignment == b->slots[s].alignment &&
                a->slots[s].type == b->slots[s].type && a->slots[s].flags == b->slots[s].flags;
    }
    for (uint32_t i = 0; equal && i < a->instruction_count; i += 1)
    {
        switch (a->op[i])
        {
            case IR_CONST:
            {
                equal = ir_source_text_constant(a, a->a[i], b, b->a[i]);
            } break;
            case IR_VCONST:
            {
                equal = ir_source_text_bytes(&a->vector_constants[a->a[i]], &b->vector_constants[b->a[i]], sizeof(IrSimdValue));
            } break;
            case IR_ADDRESS:
            {
                const IrAddress* x = &a->addresses[a->c[i]];
                const IrAddress* y = &b->addresses[b->c[i]];
                equal = a->a[i] == b->a[i] && a->b[i] == b->b[i] && x->offset == y->offset &&
                        (a->b[i] == BUSTER_IR_NONE || x->scale == y->scale);
            } break;
            case IR_CALL:
            case IR_CALL_INDIRECT:
            {
                equal = a->a[i] == b->a[i] && a->c[i] == b->c[i] && ir_source_text_call(a, a->b[i], b, b->b[i]);
            } break;
            case IR_PHI:
            {
                equal = a->b[i] == b->b[i] && ir_source_text_range(a->extra, a->a[i], b->extra, b->a[i], (uint64_t)a->b[i] * 2, sizeof(uint32_t));
            } break;
            case IR_SWITCH:
            {
                equal = a->a[i] == b->a[i] && a->c[i] == b->c[i] && a->extra[a->b[i]] == b->extra[b->b[i]];
                for (uint32_t k = 0; equal && k < a->c[i]; k += 1)
                {
                    const uint64_t x = (uint64_t)a->b[i] + 1 + (uint64_t)k * 2;
                    const uint64_t y = (uint64_t)b->b[i] + 1 + (uint64_t)k * 2;
                    equal = ir_source_text_constant(a, a->extra[x], b, b->extra[y]) && a->extra[x + 1] == b->extra[y + 1];
                }
            } break;
            case IR_BR_INDIRECT:
            {
                equal = a->a[i] == b->a[i] && a->c[i] == b->c[i] && (a->b[i] == BUSTER_IR_NONE) == (b->b[i] == BUSTER_IR_NONE);
                if (equal && a->b[i] != BUSTER_IR_NONE)
                {
                    equal = ir_source_text_range(a->extra, a->b[i], b->extra, b->b[i], a->c[i], sizeof(uint32_t));
                }
            } break;
            default:
            {
                equal = a->a[i] == b->a[i] && a->b[i] == b->b[i] && a->c[i] == b->c[i];
            } break;
        }
    }
    return equal;
}

// The stream owns its growable buffer until close, after which the caller frees it even on failure. No temporary
// disk file per function: corpus checks print thousands of functions and this is outside every timed compiler path.
static bool ir_source_text_print(const IrFunction* function, Arena* scratch, char** bytes, size_t* length)
{
    *bytes = 0;
    *length = 0;
    FILE* stream = open_memstream(bytes, length);
    bool valid = stream != 0;
    if (stream != 0)
    {
        valid = ir_print(function, scratch, stream) == IR_OK && ferror(stream) == 0;
        const int closed = fclose(stream);
        valid = valid && closed == 0;
    }
    return valid;
}

static bool ir_source_text_roundtrip(const IrFunction* source, Arena* output, Arena* scratch, IrSourceTextCheck* check)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    *check = (IrSourceTextCheck){ .reason = "printing the source failed" };
    char* text = 0;
    size_t length = 0;
    bool valid = ir_source_text_print(source, scratch, &text, &length);
    if (valid && scratch->length != scratch_mark)
    {
        check->reason = "printing retained scratch";
        valid = false;
    }
    const String8 input = {
        .pointer = text,
        .length = length,
    };
    IrFunction parsed[2] = { 0 };
    IrParseResult results[2] = { 0 };
    if (valid)
    {
        results[0] = ir_parse(input, output, scratch, &parsed[0]);
        const bool vector_scratch = scratch->length == scratch_mark;
        results[1] = ir_parse_scalar(input, output, scratch, &parsed[1]);
        const bool scalar_scratch = scratch->length == scratch_mark;
        valid = vector_scratch && scalar_scratch;
        check->reason = "parsing retained scratch";
    }
    for (uint32_t path = 0; valid && path < 2; path += 1)
    {
        check->parsed = results[path];
        valid = results[path].error == IR_PARSE_OK && results[path].consumed == input.length && results[path].check.dominance_checked;
        check->reason = path == 0 ? "vector parse did not verify and consume the complete function" : "scalar parse did not verify and consume the complete function";
        if (valid)
        {
            valid = ir_source_text_equal(source, &parsed[path]);
            check->reason = path == 0 ? "vector round trip changed structure" : "scalar round trip changed structure";
        }
        char* printed = 0;
        size_t printed_length = 0;
        if (valid)
        {
            valid = ir_source_text_print(&parsed[path], scratch, &printed, &printed_length) &&
                    printed_length == length && ir_source_text_bytes(text, printed, length);
            check->reason = path == 0 ? "vector round trip changed canonical bytes" : "scalar round trip changed canonical bytes";
        }
        free(printed);
    }
    if (valid)
    {
        valid = scratch->length == scratch_mark;
        check->reason = "reprinting retained scratch";
    }
    if (valid)
    {
        check->reason = 0;
    }
    free(text);
    arena_pop_to(output, output_mark);
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

typedef struct IrSourceTextTests IrSourceTextTests;
struct IrSourceTextTests
{
    uint64_t checks;
    uint64_t failures;
};

static void ir_source_text_expect(IrSourceTextTests* tests, bool condition, const char* reason)
{
    tests->checks += 1;
    if (!condition)
    {
        if (tests->failures < 20)
        {
            fprintf(stderr, "ir-source text oracle: %s\n", reason);
        }
        tests->failures += 1;
    }
}

// Both parsed copies have the same canonical rows. Change one meaningful field at a time without changing any
// referenced slice extent, then restore it. A comparison that only reprints or compares top-level columns is not enough.
static void ir_source_text_mutations(IrSourceTextTests* tests, const IrFunction* original, IrFunction* changed)
{
    ir_source_text_expect(tests, ir_source_text_equal(original, changed), "identical functions disagree");
    const uint8_t result_type = changed->result_type;
    changed->result_type = result_type == IR_VOID ? IR_I64 : IR_VOID;
    ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "a changed result type was missed");
    changed->result_type = result_type;
    for (uint32_t i = 0; i < changed->instruction_count; i += 1)
    {
        changed->flags[i] ^= 1;
        ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed instruction flags were missed");
        changed->flags[i] ^= 1;
        changed->alignment_log2[i] ^= 1;
        ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed memory alignment was missed");
        changed->alignment_log2[i] ^= 1;
        switch (changed->op[i])
        {
            case IR_CONST:
            {
                IrConstant* constant = &changed->constants[changed->a[i]];
                constant->low ^= 1;
                ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed constant bits were missed");
                constant->low ^= 1;
            } break;
            case IR_VCONST:
            {
                uint8_t* bytes = (uint8_t*)&changed->vector_constants[changed->a[i]];
                bytes[0] ^= 1;
                ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed vector constant bits were missed");
                bytes[0] ^= 1;
            } break;
            case IR_ADDRESS:
            {
                IrAddress* address = &changed->addresses[changed->c[i]];
                address->offset ^= 1;
                ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed address displacement was missed");
                address->offset ^= 1;
                if (changed->b[i] != BUSTER_IR_NONE)
                {
                    const uint8_t scale = address->scale;
                    address->scale = scale == 1 ? 2 : 1;
                    ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed indexed address scale was missed");
                    address->scale = scale;
                }
            } break;
            case IR_CALL:
            case IR_CALL_INDIRECT:
            {
                IrCall* call = &changed->calls[changed->b[i]];
                call->variadic = !call->variadic;
                ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed call signature was missed");
                call->variadic = !call->variadic;
                if (call->argument_count != 0)
                {
                    changed->extra[call->argument_first] ^= 1;
                    ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed call argument was missed");
                    changed->extra[call->argument_first] ^= 1;
                }
                if (call->abi != 0)
                {
                    IrAbiCall* abi = &changed->abi_calls[call->abi - 1];
                    abi->call.stack_bytes ^= 8;
                    ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed call ABI was missed");
                    abi->call.stack_bytes ^= 8;
                    if (call->argument_count != 0)
                    {
                        changed->passed[abi->passed].size ^= 1;
                        ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed aggregate argument size was missed");
                        changed->passed[abi->passed].size ^= 1;
                    }
                }
            } break;
            case IR_PHI:
            {
                if (changed->b[i] != 0)
                {
                    changed->extra[changed->a[i] + 1] ^= 1;
                    ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed phi input was missed");
                    changed->extra[changed->a[i] + 1] ^= 1;
                }
            } break;
            case IR_SWITCH:
            {
                changed->extra[changed->b[i]] ^= 1;
                ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed switch default was missed");
                changed->extra[changed->b[i]] ^= 1;
                if (changed->c[i] != 0)
                {
                    IrConstant* constant = &changed->constants[changed->extra[changed->b[i] + 1]];
                    constant->low ^= 1;
                    ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed switch case was missed");
                    constant->low ^= 1;
                }
            } break;
            case IR_BR_INDIRECT:
            {
                if (changed->b[i] != BUSTER_IR_NONE && changed->c[i] != 0)
                {
                    changed->extra[changed->b[i]] ^= 1;
                    ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed indirect branch label was missed");
                    changed->extra[changed->b[i]] ^= 1;
                }
            } break;
            default:
            {
                changed->a[i] ^= 1;
                ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed ordinary operand was missed");
                changed->a[i] ^= 1;
            } break;
        }
    }
    for (uint32_t s = 0; s < changed->slot_count; s += 1)
    {
        changed->slots[s].flags ^= 1;
        ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed slot flags were missed");
        changed->slots[s].flags ^= 1;
    }
    if (changed->parameter_passed != 0)
    {
        changed->result_size ^= 1;
        ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed aggregate return size was missed");
        changed->result_size ^= 1;
        for (uint32_t p = 0; p < changed->parameter_count; p += 1)
        {
            IrPassed* passed = &changed->passed[changed->parameter_passed - 1 + p];
            passed->location.flags ^= 1;
            ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed aggregate parameter ABI was missed");
            passed->location.flags ^= 1;
        }
    }
    if (changed->variadic)
    {
        changed->incoming.stack_bytes ^= 8;
        ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed variadic incoming frame was missed");
        changed->incoming.stack_bytes ^= 8;
    }
    for (uint32_t l = 0; l < changed->label_count; l += 1)
    {
        changed->labels[l] ^= 1;
        ir_source_text_expect(tests, !ir_source_text_equal(original, changed), "changed label block was missed");
        changed->labels[l] ^= 1;
    }
    ir_source_text_expect(tests, ir_source_text_equal(original, changed), "mutation checks did not restore their input");
}

static void ir_source_text_normalization(IrSourceTextTests* tests, Arena* original_output, Arena* output, Arena* scratch)
{
    const uint64_t mark = original_output->length;
    const IrSizes sizes = {
        .instruction_count = 8,
        .block_count = 1,
        .extra_count = 4,
        .constant_count = 4,
        .address_count = 3,
        .call_count = 3,
        .call_type_count = 3,
    };
    IrFunction original = { 0 };
    const bool created = ir_create(original_output, &sizes, &original) == IR_OK;
    ir_source_text_expect(tests, created, "normalization fixture allocation failed");
    if (created)
    {
        original.result_type = IR_I64;
        original.symbol_count = 42;
        original.block_offsets[1] = 8;
        original.constants[3].low = 17;
        for (uint32_t a = 0; a < original.address_count; a += 1)
        {
            original.addresses[a].scale = 1;
        }
        original.addresses[2].offset = -7;
        original.addresses[2].scale = 8;
        original.call_types[0] = IR_I8;
        original.call_types[1] = IR_I64;
        original.call_types[2] = IR_I64;
        original.extra[1] = 0;
        original.extra[2] = 1;
        original.calls[2] = (IrCall){
            .argument_first = 1,
            .argument_count = 2,
            .type_first = 1,
            .parameter_count = 2,
            .result_type = IR_I64,
        };
        const uint8_t ops[] = {
            IR_CONST,
            IR_CONST,
            IR_SYMBOL_ADDR,
            IR_ADDRESS,
            IR_CALL,
            IR_CALL,
            IR_ADD,
            IR_RET,
        };
        const uint8_t types[] = {
            IR_I64,
            IR_I64,
            IR_PTR,
            IR_PTR,
            IR_I64,
            IR_I64,
            IR_I64,
            IR_VOID,
        };
        const uint32_t a[] = {
            3,
            3,
            1,
            2,
            1,
            1,
            4,
            6,
        };
        for (uint32_t i = 0; i < original.instruction_count; i += 1)
        {
            original.op[i] = ops[i];
            original.type[i] = types[i];
            original.a[i] = a[i];
        }
        original.c[3] = 2;
        original.b[4] = 2;
        original.b[5] = 2;
        original.b[6] = 5;
        const IrCheck verified = ir_verify(&original, scratch, 0);
        ir_source_text_expect(tests, verified.error == IR_OK, "normalization fixture is invalid");
        IrSourceTextCheck check = { 0 };
        ir_source_text_expect(tests, verified.error == IR_OK && ir_source_text_roundtrip(&original, output, scratch, &check),
                              "shared/unused sparse rows, symbol capacity or unindexed scale changed meaning");
        uint8_t prefix[64];
        memset(prefix, 0xa5, sizeof(prefix));
        Arena exhausted = {
            .base = prefix,
            .length = sizeof(prefix),
            .capacity = sizeof(prefix),
        };
        const uint64_t scratch_mark = scratch->length;
        const bool refused = !ir_source_text_roundtrip(&original, &exhausted, scratch, &check);
        bool preserved = exhausted.length == sizeof(prefix) && scratch->length == scratch_mark;
        for (uint32_t i = 0; i < sizeof(prefix); i += 1)
        {
            preserved = preserved && prefix[i] == 0xa5;
        }
        ir_source_text_expect(tests, refused && check.parsed.error == IR_PARSE_MEMORY && preserved,
                              "exhausted parsing passed, lost its memory diagnostic or changed an arena prefix");
    }
    arena_pop_to(original_output, mark);
}

static bool ir_source_text_tests(Arena* sources, Arena* original_output, Arena* twin_output, Arena* output, Arena* scratch)
{
    static const char* const fixtures[] = {
        "tests/ir/arithmetic.ir",
        "tests/ir/conversions.ir",
        "tests/ir/control.ir",
        "tests/ir/memory-calls.ir",
        "tests/ir/dynamic-stack.ir",
        "tests/ir/labels.ir",
        "tests/ir/aggregates.ir",
        "tests/ir/vector-mask.ir",
        "tests/ir/empty.ir",
        "tests/ir/tier0-add.ir",
    };
    IrSourceTextTests tests = { 0 };
    for (uint32_t f = 0; f < sizeof(fixtures) / sizeof(fixtures[0]); f += 1)
    {
        const uint64_t source_mark = sources->length;
        String8 text = { 0 };
        const bool read = ir_parse_test_read(fixtures[f], sources, &text);
        ir_source_text_expect(&tests, read, fixtures[f]);
        uint64_t position = 0;
        bool valid = read;
        while (valid && position < text.length)
        {
            const uint64_t original_mark = original_output->length;
            const uint64_t twin_mark = twin_output->length;
            const uint64_t output_mark = output->length;
            const uint64_t scratch_mark = scratch->length;
            const String8 remaining = {
                .pointer = text.pointer + position,
                .length = text.length - position,
            };
            IrFunction original = { 0 };
            IrFunction twin = { 0 };
            const IrParseResult parsed = ir_parse(remaining, original_output, scratch, &original);
            const IrParseResult parsed_twin = ir_parse_scalar(remaining, twin_output, scratch, &twin);
            const bool ended = parsed.error == IR_PARSE_END && parsed_twin.error == IR_PARSE_END;
            valid = !ended && parsed.error == IR_PARSE_OK && parsed_twin.error == IR_PARSE_OK &&
                    parsed.consumed != 0 && parsed.consumed == parsed_twin.consumed && parsed.consumed <= remaining.length;
            ir_source_text_expect(&tests, valid || ended, "oracle fixture did not parse through both paths");
            if (valid)
            {
                IrSourceTextCheck check = { 0 };
                const bool roundtrip = ir_source_text_roundtrip(&original, output, scratch, &check);
                if (!roundtrip && tests.failures < 20)
                {
                    fprintf(stderr, "ir-source text oracle: fixture=%s offset=%" PRIu64 " reason=%s parse_error=%u line=%" PRIu64 " column=%" PRIu64 "\n",
                            fixtures[f], position, check.reason, (unsigned)check.parsed.error, (uint64_t)check.parsed.line, (uint64_t)check.parsed.column);
                }
                ir_source_text_expect(&tests, roundtrip, "oracle fixture did not round-trip");
                ir_source_text_expect(&tests, output->length == output_mark && scratch->length == scratch_mark, "round trip retained an arena allocation");
                ir_source_text_mutations(&tests, &original, &twin);
                position += parsed.consumed;
            }
            arena_pop_to(original_output, original_mark);
            arena_pop_to(twin_output, twin_mark);
            arena_pop_to(output, output_mark);
            arena_pop_to(scratch, scratch_mark);
        }
        arena_pop_to(sources, source_mark);
    }
    ir_source_text_normalization(&tests, original_output, output, scratch);
    printf("ir-source: text oracle checks=%" PRIu64 " failures=%" PRIu64 "\n", tests.checks, tests.failures);
    return tests.failures == 0;
}
