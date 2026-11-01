// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original fixtures for #139. The arithmetic oracle uses wide signed C arithmetic, separately from the optimizer's
// bit-pattern arithmetic. Guarded columns end at inaccessible pages and include empty and every partial batch.
// main.c includes this file after ir_optimize.c; run_ir_optimize_tests is the command entry point.

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct IrOptimizeTest IrOptimizeTest;
struct IrOptimizeTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
};

static void ir_optimize_test_expect(IrOptimizeTest* test, bool condition, const char* description)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 8)
        {
            fprintf(stderr, "ir-optimize: %s\n", description);
        }
        test->failures += 1;
    }
}

static uint64_t ir_optimize_test_random(IrOptimizeTest* test)
{
    uint64_t state = test->random;
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    test->random = state;
    return state;
}

static bool ir_optimize_test_fold_oracle(uint32_t operation, uint32_t width, uint64_t left, uint64_t right, uint64_t* output)
{
    bool folded = width >= 1 && width <= 64;
    unsigned __int128 value = 0;
    if (folded)
    {
        const unsigned __int128 modulus = (unsigned __int128)1 << width;
        const unsigned __int128 a = left % modulus;
        const unsigned __int128 b = operation >= IROPT_FOLD_SHL && operation <= IROPT_FOLD_ASHR ? right : right % modulus;
        const __int128 sa = a >= modulus / 2 ? (__int128)a - (__int128)modulus : (__int128)a;
        const __int128 sb = b >= modulus / 2 ? (__int128)b - (__int128)modulus : (__int128)b;
        switch (operation)
        {
            case IROPT_FOLD_ADD: value = a + b; break;
            case IROPT_FOLD_SUB: value = a + modulus - b; break;
            case IROPT_FOLD_MUL: value = a * b; break;
            case IROPT_FOLD_AND: value = a & b; break;
            case IROPT_FOLD_OR: value = a | b; break;
            case IROPT_FOLD_XOR: value = a ^ b; break;
            case IROPT_FOLD_EQ: value = a == b; break;
            case IROPT_FOLD_NE: value = a != b; break;
            case IROPT_FOLD_ULT: value = a < b; break;
            case IROPT_FOLD_ULE: value = a <= b; break;
            case IROPT_FOLD_SLT: value = sa < sb; break;
            case IROPT_FOLD_SLE: value = sa <= sb; break;
            case IROPT_FOLD_SHL:
            case IROPT_FOLD_LSHR:
            case IROPT_FOLD_ASHR:
            {
                folded = b < width;
                if (folded)
                {
                    const unsigned __int128 factor = (unsigned __int128)1 << b;
                    if (operation == IROPT_FOLD_SHL)
                    {
                        value = a * factor;
                    }
                    else if (operation == IROPT_FOLD_LSHR)
                    {
                        value = a / factor;
                    }
                    else
                    {
                        // Arithmetic shift rounds a negative quotient down rather than C division's toward-zero.
                        const __int128 adjusted = sa < 0 ? sa - (__int128)factor + 1 : sa;
                        value = (unsigned __int128)(adjusted / (__int128)factor);
                    }
                }
            } break;
            case IROPT_FOLD_UDIV:
            case IROPT_FOLD_UREM:
            {
                folded = b != 0;
                if (folded)
                {
                    value = operation == IROPT_FOLD_UDIV ? a / b : a % b;
                }
            } break;
            case IROPT_FOLD_SDIV:
            case IROPT_FOLD_SREM:
            {
                folded = b != 0 && !(sa == -(__int128)(modulus / 2) && sb == -1);
                if (folded)
                {
                    value = operation == IROPT_FOLD_SDIV ? (unsigned __int128)(sa / sb) : (unsigned __int128)(sa % sb);
                }
            } break;
            default: folded = false; break;
        }
        if (folded)
        {
            *output = (uint64_t)(value % modulus);
        }
    }
    return folded;
}

static void ir_optimize_test_folds(IrOptimizeTest* test)
{
    static const uint32_t widths[] = { 0, 1, 8, 16, 32, 64, 65 };
    static const uint64_t extremes[] = { 0, 1, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, UINT64_C(0x80000000), UINT64_C(0x8000000000000000), UINT64_MAX };
    for (uint32_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w += 1)
    {
        for (uint32_t operation = IROPT_FOLD_NONE; operation <= IROPT_FOLD_SLE; operation += 1)
        {
            for (uint32_t a = 0; a < sizeof(extremes) / sizeof(extremes[0]); a += 1)
            {
                for (uint32_t b = 0; b < sizeof(extremes) / sizeof(extremes[0]); b += 1)
                {
                    uint64_t expected = UINT64_C(0x1122334455667788);
                    uint64_t actual = expected;
                    const bool expected_fold = ir_optimize_test_fold_oracle(operation, widths[w], extremes[a], extremes[b], &expected);
                    const bool actual_fold = ir_optimize_fold_one(operation, widths[w], extremes[a], extremes[b], &actual);
                    ir_optimize_test_expect(test, expected_fold == actual_fold && actual == expected, "integer fold versus wide arithmetic");
                }
            }
        }
    }
}

static void ir_optimize_test_fold_guards(IrOptimizeTest* test)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    void* mapping = page_size > 0 ? mmap(0, (uint64_t)page_size * 12, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ready = mapping != MAP_FAILED;
    for (uint32_t p = 1; ready && p < 12; p += 2)
    {
        ready = mprotect((char*)mapping + p * page_size, page_size, PROT_NONE) == 0;
    }
    ir_optimize_test_expect(test, ready, "guard page setup");
    for (uint32_t count = 0; ready && count <= BUSTER_IR_OPTIMIZE_BATCH; count += 1)
    {
        uint32_t* operation = (uint32_t*)((char*)mapping + page_size - count * sizeof(uint32_t));
        uint32_t* width = (uint32_t*)((char*)mapping + 3 * page_size - count * sizeof(uint32_t));
        uint64_t* left = (uint64_t*)((char*)mapping + 5 * page_size - count * sizeof(uint64_t));
        uint64_t* right = (uint64_t*)((char*)mapping + 7 * page_size - count * sizeof(uint64_t));
        uint64_t* scalar = (uint64_t*)((char*)mapping + 9 * page_size - count * sizeof(uint64_t));
        uint64_t* vector = (uint64_t*)((char*)mapping + 11 * page_size - count * sizeof(uint64_t));
        for (uint32_t iteration = 0; iteration < 512; iteration += 1)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                operation[i] = ir_optimize_test_random(test) % (IROPT_FOLD_SLE + 2);
                width[i] = ir_optimize_test_random(test) % 66;
                left[i] = ir_optimize_test_random(test);
                right[i] = ir_optimize_test_random(test);
                scalar[i] = UINT64_C(0xfedcba9876543210);
                vector[i] = scalar[i];
            }
            IrOptimizeFoldBatch batch = {
                .count = count,
                .operation = operation,
                .width = width,
                .left = left,
                .right = right,
                .result = scalar,
            };
            const uint16_t active = iteration == 0 ? UINT16_MAX : ir_optimize_test_random(test);
            const uint16_t scalar_folded = ir_optimize_fold_scalar(batch, active);
            batch.result = vector;
            const uint16_t vector_folded = ir_optimize_fold_vector(batch, active);
            ir_optimize_test_expect(test, scalar_folded == vector_folded, "guarded scalar/vector fold masks");
            for (uint32_t i = 0; i < count; i += 1)
            {
                uint64_t expected = UINT64_C(0xfedcba9876543210);
                bool expected_folded = false;
                if ((active & (UINT32_C(1) << i)) != 0)
                {
                    expected_folded = ir_optimize_test_fold_oracle(operation[i], width[i], left[i], right[i], &expected);
                }
                ir_optimize_test_expect(test, scalar[i] == vector[i] && vector[i] == expected && ((scalar_folded >> i) & 1) == expected_folded, "guarded folds and inactive stores");
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (uint64_t)page_size * 12);
    }
    const IrOptimizeFoldBatch invalid = { .count = BUSTER_IR_OPTIMIZE_BATCH + 1 };
    ir_optimize_test_expect(test, ir_optimize_fold_scalar(invalid, UINT16_MAX) == 0 && ir_optimize_fold_vector(invalid, UINT16_MAX) == 0, "invalid count without column access");
}

static void ir_optimize_test_instruction(IrFunction* f, uint32_t i, uint8_t op, uint8_t type, uint32_t a, uint32_t b, uint32_t c)
{
    f->op[i] = op;
    f->type[i] = type;
    f->a[i] = a;
    f->b[i] = b;
    f->c[i] = c;
}

static bool ir_optimize_test_equal(const IrFunction* a, const IrFunction* b)
{
    bool equal = a->instruction_count == b->instruction_count && a->block_count == b->block_count && a->constant_count == b->constant_count && a->extra_count == b->extra_count && a->slot_count == b->slot_count && a->address_count == b->address_count && a->call_count == b->call_count && a->call_type_count == b->call_type_count && a->parameter_count == b->parameter_count && a->result_type == b->result_type && a->symbol_count == b->symbol_count;
    if (equal)
    {
        const uint64_t n = a->instruction_count;
        equal = (n == 0 || (memcmp(a->op, b->op, n) == 0 && memcmp(a->type, b->type, n) == 0 && memcmp(a->flags, b->flags, n) == 0 && memcmp(a->alignment_log2, b->alignment_log2, n) == 0 && memcmp(a->a, b->a, n * 4) == 0 && memcmp(a->b, b->b, n * 4) == 0 && memcmp(a->c, b->c, n * 4) == 0));
        equal = equal && memcmp(a->block_offsets, b->block_offsets, ((uint64_t)a->block_count + 1) * 4) == 0;
        equal = equal && (a->extra_count == 0 || memcmp(a->extra, b->extra, (uint64_t)a->extra_count * 4) == 0);
        equal = equal && (a->constant_count == 0 || memcmp(a->constants, b->constants, (uint64_t)a->constant_count * sizeof(IrConstant)) == 0);
        equal = equal && (a->call_count == 0 || memcmp(a->calls, b->calls, (uint64_t)a->call_count * sizeof(IrCall)) == 0);
        equal = equal && a->label_count == b->label_count && (a->label_count == 0 || memcmp(a->labels, b->labels, (uint64_t)a->label_count * 4) == 0);
        equal = equal && a->abi_call_count == b->abi_call_count && (a->abi_call_count == 0 || memcmp(a->abi_calls, b->abi_calls, (uint64_t)a->abi_call_count * sizeof(IrAbiCall)) == 0);
        equal = equal && a->passed_count == b->passed_count && (a->passed_count == 0 || memcmp(a->passed, b->passed, (uint64_t)a->passed_count * sizeof(IrPassed)) == 0);
        equal = equal && a->parameter_passed == b->parameter_passed && a->result_size == b->result_size && memcmp(&a->result_abi, &b->result_abi, sizeof(AbiReturn)) == 0 && a->variadic == b->variadic && memcmp(&a->incoming, &b->incoming, sizeof(AbiCall)) == 0;
    }
    return equal;
}

typedef struct IrOptimizeExecution IrOptimizeExecution;
struct IrOptimizeExecution
{
    uint64_t value;
    uint64_t effects;
    uint32_t status;
};

// The fixtures' producer defines label k's symbol as BUSTER_IR_OPTIMIZE_TEST_LABEL + k (#131's labels).
#define BUSTER_IR_OPTIMIZE_TEST_LABEL 64

// Independent bounded interpreter used only by the original fixtures. It does not use optimizer opcode tables,
// use enumeration, lattice, CFG, or fold code. Integer arithmetic goes through the independent wide-C oracle.
static IrOptimizeExecution ir_optimize_test_execute(const IrFunction* f, Arena* scratch, uint64_t parameter, uint64_t unspecified)
{
    const uint64_t mark = scratch->length;
    uint64_t* values = arena_push_array(scratch, f->instruction_count, sizeof(uint64_t), _Alignof(uint64_t));
    uint64_t* phis = arena_push_array(scratch, f->instruction_count, sizeof(uint64_t), _Alignof(uint64_t));
    IrOptimizeExecution execution = { 0 };
    uint8_t memory[256] = { 0 };
    uint32_t block = 0;
    uint32_t predecessor = BUSTER_IR_NONE;
    bool active = f->block_count != 0 && values != 0 && phis != 0;
    if (!active)
    {
        execution.status = f->block_count == 0 ? 1 : 4;
    }
    if (active)
    {
        memset(values, 0, (uint64_t)f->instruction_count * sizeof(uint64_t));
    }
    for (uint32_t step = 0; active && step < 4096; step += 1)
    {
        const uint32_t first = f->block_offsets[block];
        const uint32_t end = f->block_offsets[(uint64_t)block + 1];
        uint32_t phi_end = first;
        while (phi_end < end && f->op[phi_end] == IR_PHI)
        {
            bool found = false;
            for (uint32_t incoming = 0; incoming < f->b[phi_end]; incoming += 1)
            {
                if (f->extra[f->a[phi_end] + incoming * 2] == predecessor)
                {
                    phis[phi_end] = values[f->extra[f->a[phi_end] + incoming * 2 + 1]];
                    found = true;
                }
            }
            if (!found)
            {
                execution.status = 4;
                active = false;
            }
            phi_end += 1;
        }
        for (uint32_t i = first; i < phi_end; i += 1)
        {
            values[i] = phis[i];
        }
        uint32_t target = BUSTER_IR_NONE;
        for (uint32_t i = phi_end; active && i < end; i += 1)
        {
            const uint32_t a = f->a[i];
            const uint32_t b = f->b[i];
            const uint32_t op = f->op[i];
            uint64_t value = 0;
            uint32_t operation = IROPT_FOLD_NONE;
            bool swap = false;
            bool folded = true;
            switch (op)
            {
                case IR_CONST: value = f->constants[a].low; break;
                case IR_PARAM: value = f->type[i] == IR_I1 ? parameter & 1 : parameter + a; break;
                case IR_UNDEF: value = unspecified; break;
                case IR_COPY: value = values[a]; break;
                case IR_SELECT: value = values[a] != 0 ? values[b] : values[f->c[i]]; break;
                case IR_ADD: operation = IROPT_FOLD_ADD; break;
                case IR_SUB: operation = IROPT_FOLD_SUB; break;
                case IR_MUL: operation = IROPT_FOLD_MUL; break;
                case IR_AND: operation = IROPT_FOLD_AND; break;
                case IR_OR: operation = IROPT_FOLD_OR; break;
                case IR_XOR: operation = IROPT_FOLD_XOR; break;
                case IR_SHL: operation = IROPT_FOLD_SHL; break;
                case IR_LSHR: operation = IROPT_FOLD_LSHR; break;
                case IR_ASHR: operation = IROPT_FOLD_ASHR; break;
                case IR_SDIV: operation = IROPT_FOLD_SDIV; break;
                case IR_UDIV: operation = IROPT_FOLD_UDIV; break;
                case IR_SREM: operation = IROPT_FOLD_SREM; break;
                case IR_UREM: operation = IROPT_FOLD_UREM; break;
                case IR_NEG: value = -values[a]; break;
                case IR_NOT: value = ~values[a]; break;
                case IR_TRUNC:
                case IR_ZEXT:
                case IR_BITCAST:
                case IR_INTTOPTR:
                case IR_PTRTOINT: value = values[a]; break;
                case IR_SEXT:
                {
                    const uint32_t width = ir_type_bits[f->type[a]];
                    const __int128 modulus = (__int128)1 << width;
                    const __int128 signed_value = values[a] >= modulus / 2 ? (__int128)values[a] - modulus : (__int128)values[a];
                    value = (uint64_t)signed_value;
                } break;
                case IR_ICMP:
                {
                    switch (f->c[i])
                    {
                        case IR_EQ: operation = IROPT_FOLD_EQ; break;
                        case IR_NE: operation = IROPT_FOLD_NE; break;
                        case IR_LT: operation = IROPT_FOLD_SLT; break;
                        case IR_LE: operation = IROPT_FOLD_SLE; break;
                        case IR_GT: operation = IROPT_FOLD_SLT; swap = true; break;
                        case IR_GE: operation = IROPT_FOLD_SLE; swap = true; break;
                        case IR_ULT: operation = IROPT_FOLD_ULT; break;
                        case IR_ULE: operation = IROPT_FOLD_ULE; break;
                        case IR_UGT: operation = IROPT_FOLD_ULT; swap = true; break;
                        case IR_UGE: operation = IROPT_FOLD_ULE; swap = true; break;
                        default: folded = false; break;
                    }
                } break;
                case IR_STACK_ADDR: value = (uint64_t)a * 64 + 1; break;
                // A slot's lifetime marker reads and writes nothing (#177).
                case IR_LIFETIME: break;
                case IR_SYMBOL_ADDR: value = (uint64_t)a << 32; break;
                case IR_ADDRESS: value = values[a] + (b == BUSTER_IR_NONE ? 0 : values[b] * f->addresses[f->c[i]].scale) + f->addresses[f->c[i]].offset; break;
                case IR_LOAD:
                case IR_STORE:
                {
                    const uint64_t address = values[a];
                    const uint32_t bytes = (ir_type_bits[op == IR_LOAD ? f->type[i] : f->type[b]] + 7) / 8;
                    folded = address != 0 && address - 1 <= sizeof(memory) && bytes <= sizeof(memory) - (address - 1);
                    if (folded)
                    {
                        if (op == IR_STORE)
                        {
                            for (uint32_t byte = 0; byte < bytes; byte += 1)
                            {
                                memory[address - 1 + byte] = values[b] >> (byte * 8);
                            }
                            execution.effects = execution.effects * 65537 + address * 17 + values[b];
                        }
                        else
                        {
                            for (uint32_t byte = 0; byte < bytes; byte += 1)
                            {
                                value |= (uint64_t)memory[address - 1 + byte] << (byte * 8);
                            }
                            if ((f->flags[i] & BUSTER_IR_VOLATILE) != 0)
                            {
                                execution.effects = execution.effects * 65537 + address * 19 + value;
                            }
                        }
                    }
                } break;
                case IR_CALL:
                case IR_CALL_INDIRECT:
                {
                    // A call with ABI records (#131): an aggregate argument is the address of the bytes the callee reads,
                    // and an aggregate result's bytes go where c points.
                    const IrCall call = f->calls[b];
                    const IrAbiCall* abi = call.abi != 0 ? &f->abi_calls[call.abi - 1] : 0;
                    value = op == IR_CALL ? a : values[a];
                    for (uint32_t argument = 0; folded && argument < call.argument_count; argument += 1)
                    {
                        const uint64_t argument_value = values[f->extra[call.argument_first + argument]];
                        const uint32_t size = abi != 0 ? f->passed[abi->passed + argument].size : 0;
                        folded = size == 0 || (argument_value != 0 && argument_value - 1 <= sizeof(memory) && size <= sizeof(memory) - (argument_value - 1));
                        value = value * 31 + (size == 0 ? argument_value : 0);
                        for (uint32_t byte = 0; folded && byte < size; byte += 1)
                        {
                            value = value * 31 + memory[argument_value - 1 + byte];
                        }
                    }
                    const uint32_t bytes = abi != 0 ? abi->result_size : 0;
                    const uint64_t into = bytes != 0 ? values[f->c[i]] : 0;
                    folded = folded && (bytes == 0 || (into != 0 && into - 1 <= sizeof(memory) && bytes <= sizeof(memory) - (into - 1)));
                    for (uint32_t byte = 0; folded && byte < bytes; byte += 1)
                    {
                        memory[into - 1 + byte] = (uint8_t)(value >> (byte % 8 * 8));
                    }
                    execution.effects = execution.effects * 65537 + value;
                } break;
                case IR_BR: target = a; break;
                case IR_CBR: target = values[a] != 0 ? b : f->c[i]; break;
                case IR_SWITCH:
                {
                    target = f->extra[b];
                    for (uint32_t entry = 0; entry < f->c[i]; entry += 1)
                    {
                        if (values[a] == f->constants[f->extra[b + 1 + entry * 2]].low)
                        {
                            target = f->extra[b + 2 + entry * 2];
                        }
                    }
                } break;
                case IR_BR_INDIRECT:
                {
                    // To the label whose symbol's address the operand holds, or nowhere (status 4).
                    const uint64_t label = (values[a] >> 32) - BUSTER_IR_OPTIMIZE_TEST_LABEL;
                    target = (values[a] & UINT32_MAX) == 0 && label < f->label_count ? f->labels[label] : BUSTER_IR_NONE;
                } break;
                case IR_RET:
                {
                    execution.value = a == BUSTER_IR_NONE ? 0 : values[a];
                    execution.status = 1;
                    active = false;
                } break;
                case IR_UNREACHABLE: folded = false; break;
                default: execution.status = 4; folded = false; break;
            }
            if (operation != IROPT_FOLD_NONE)
            {
                folded = ir_optimize_test_fold_oracle(operation, ir_type_bits[f->type[a]], values[swap ? b : a], values[swap ? a : b], &value);
            }
            if (!folded)
            {
                execution.status = execution.status == 4 ? 4 : 2;
                active = false;
            }
            if (f->type[i] != IR_VOID && ir_type_bits[f->type[i]] <= 64)
            {
                values[i] = value & (UINT64_MAX >> (64 - ir_type_bits[f->type[i]]));
            }
        }
        if (active)
        {
            if (target >= f->block_count)
            {
                execution.status = 4;
                active = false;
            }
            else
            {
                predecessor = block;
                block = target;
            }
        }
    }
    if (active)
    {
        execution.status = 3;
    }
    arena_pop_to(scratch, mark);
    return execution;
}

static void ir_optimize_test_compare(IrOptimizeTest* test, const IrFunction* f, Arena* vector_arena, Arena* scalar_arena, Arena* scratch, uint32_t passes)
{
    const uint64_t vm = vector_arena->length;
    const uint64_t sm = scalar_arena->length;
    const uint64_t tm = scratch->length;
    IrOptimizeOptions options = ir_optimize_options();
    options.passes = passes;
    options.path = BUSTER_IR_OPTIMIZE_VECTOR;
    IrOptimizeResult vector = { 0 };
    IrOptimizeResult scalar = { 0 };
    const IrCheck vc = ir_optimize(f, vector_arena, scratch, options, &vector);
    options.path = BUSTER_IR_OPTIMIZE_SCALAR;
    const IrCheck sc = ir_optimize(f, scalar_arena, scratch, options, &scalar);
    if ((vc.error != IR_OK || sc.error != IR_OK) && test->failures < 8)
    {
        fprintf(stderr, "ir-optimize: vector error=%u at=%u detail=%u, scalar error=%u at=%u detail=%u passes=%u\n", vc.error, vc.instruction, vc.detail, sc.error, sc.instruction, sc.detail, passes);
    }
    ir_optimize_test_expect(test, vc.error == IR_OK && sc.error == IR_OK, "optimizers accept constructed SSA");
    ir_optimize_test_expect(test, scratch->length == tm, "optimization restores scratch");
    if (vc.error == IR_OK && sc.error == IR_OK)
    {
        ir_optimize_test_expect(test, ir_optimize_test_equal(&vector.function, &scalar.function), "exact vector/worklist rewritten IR agreement");
        for (uint32_t trial = 0; trial < 9; trial += 1)
        {
            const uint64_t parameter = trial == 8 ? UINT64_MAX : trial;
            const IrOptimizeExecution before = ir_optimize_test_execute(f, scratch, parameter, trial & 1);
            const IrOptimizeExecution after = ir_optimize_test_execute(&vector.function, scratch, parameter, trial & 1);
            ir_optimize_test_expect(test, before.status != 4 && before.status == after.status && before.effects == after.effects && (before.status != 1 || before.value == after.value), "independent execution, effects and faults preserved");
        }
        arena_pop_to(scalar_arena, sm);
        options.path = BUSTER_IR_OPTIMIZE_VECTOR;
        options.sweep_limit = 0;
        const IrCheck fallback = ir_optimize(f, scalar_arena, scratch, options, &scalar);
        ir_optimize_test_expect(test, fallback.error == IR_OK && ir_optimize_test_equal(&vector.function, &scalar.function), "forced worklist completion is exact");
    }
    arena_pop_to(vector_arena, vm);
    arena_pop_to(scalar_arena, sm);
}

static bool ir_optimize_test_diamond(Arena* arena, IrFunction* f, uint32_t kind)
{
    const IrSizes sizes = {
        .instruction_count = 11,
        .block_count = 4,
        .constant_count = 3,
        .extra_count = 4,
        .parameter_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->parameter_types[0] = IR_I1;
        f->result_type = IR_I64;
        f->constants[0].low = 7;
        f->constants[1].low = 9;
        f->constants[2].low = kind & 1;
        f->block_offsets[1] = 4;
        f->block_offsets[2] = 6;
        f->block_offsets[3] = 8;
        f->block_offsets[4] = 11;
        ir_optimize_test_instruction(f, 0, kind < 2 ? IR_CONST : kind == 2 ? IR_PARAM : IR_UNDEF, IR_I1, kind < 2 ? 2 : kind == 2 ? 0 : BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_CBR, IR_VOID, 0, 1, 2);
        ir_optimize_test_instruction(f, 4, IR_ADD, IR_I64, 1, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_MUL, IR_I64, 2, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 8, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, IR_ADD, IR_I64, 8, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10, IR_RET, IR_VOID, 9, BUSTER_IR_NONE, BUSTER_IR_NONE);
        f->extra[0] = 1;
        f->extra[1] = 4;
        f->extra[2] = 2;
        f->extra[3] = 6;
    }
    return created;
}

static bool ir_optimize_test_loop(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = {
        .instruction_count = 14,
        .block_count = 4,
        .constant_count = 3,
        .extra_count = 8,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->constants[0].low = 0;
        f->constants[1].low = 1;
        f->constants[2].low = 5;
        f->block_offsets[1] = 4;
        f->block_offsets[2] = 8;
        f->block_offsets[3] = 13;
        f->block_offsets[4] = 14;
        ir_optimize_test_instruction(f, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_ICMP, IR_I1, 4, 2, IR_ULT);
        ir_optimize_test_instruction(f, 7, IR_CBR, IR_VOID, 6, 2, 3);
        ir_optimize_test_instruction(f, 8, IR_ADD, IR_I64, 4, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 9, IR_MUL, IR_I64, 5, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 10, IR_ADD, IR_I64, 5, 5, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 11, IR_XOR, IR_I64, 10, 10, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 12, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 13, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t incoming[] = { 0, 0, 2, 8, 0, 2, 2, 9 };
        memcpy(f->extra, incoming, sizeof(incoming));
    }
    return created;
}

static bool ir_optimize_test_random_dag(Arena* arena, IrFunction* f, IrOptimizeTest* test, uint32_t length)
{
    const IrSizes sizes = {
        .instruction_count = length + 4,
        .block_count = 1,
        .constant_count = 2,
        .parameter_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->constants[0].low = ir_optimize_test_random(test);
        f->constants[1].low = ir_optimize_test_random(test);
        f->block_offsets[1] = f->instruction_count;
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        static const uint8_t operations[] = { IR_ADD, IR_SUB, IR_MUL, IR_AND, IR_OR, IR_XOR };
        for (uint32_t i = 3; i + 1 < f->instruction_count; i += 1)
        {
            const uint8_t op = operations[ir_optimize_test_random(test) % 6];
            const uint32_t a = ir_optimize_test_random(test) % i;
            const uint32_t b = ir_optimize_test_random(test) % i;
            ir_optimize_test_instruction(f, i, op, IR_I64, a, b, BUSTER_IR_NONE);
        }
        ir_optimize_test_instruction(f, f->instruction_count - 1, IR_RET, IR_VOID, f->instruction_count - 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

static bool ir_optimize_test_effects(Arena* arena, IrFunction* f, bool fault)
{
    const IrSizes sizes = {
        .instruction_count = 9,
        .block_count = 1,
        .constant_count = 2,
        .slot_count = 1,
        .call_count = 1,
        .call_type_count = 1,
        .extra_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->constants[0].low = fault ? 0 : 1;
        f->constants[1].low = 42;
        f->block_offsets[1] = 9;
        f->slots[0].size = 8;
        f->slots[0].alignment = 8;
        f->slots[0].type = IR_I64;
        f->calls[0].result_type = IR_I64;
        f->calls[0].argument_count = 1;
        f->calls[0].parameter_count = 1;
        f->call_types[0] = IR_I64;
        f->extra[0] = 1;
        ir_optimize_test_instruction(f, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_STORE, IR_VOID, 2, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_LOAD, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        f->flags[3] = BUSTER_IR_VOLATILE;
        f->flags[4] = BUSTER_IR_VOLATILE;
        ir_optimize_test_instruction(f, 5, IR_UDIV, IR_I64, 1, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_CALL, IR_I64, 1, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_ADD, IR_I64, 1, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 8, IR_RET, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

static bool ir_optimize_test_switch(Arena* arena, IrFunction* f, bool constant)
{
    const IrSizes sizes = {
        .instruction_count = 8,
        .block_count = 4,
        .constant_count = 4,
        .extra_count = 9,
        .parameter_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->constants[0].low = 0;
        f->constants[1].low = 1;
        f->constants[2].low = 13;
        f->constants[3].low = 15;
        for (uint32_t block = 0; block <= 4; block += 1)
        {
            f->block_offsets[block] = block * 2;
        }
        ir_optimize_test_instruction(f, 0, constant ? IR_CONST : IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_SWITCH, IR_VOID, 0, 0, 2);
        ir_optimize_test_instruction(f, 2, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_CONST, IR_I64, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 6, IR_PHI, IR_I64, 5, 2, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_RET, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t extra[] = { 2, 0, 1, 1, 2, 1, 2, 2, 4 };
        memcpy(f->extra, extra, sizeof(extra));
    }
    return created;
}

static bool ir_optimize_test_jumps(Arena* arena, IrFunction* f, uint32_t blocks, bool cycle)
{
    const IrSizes sizes = {
        .instruction_count = (uint64_t)blocks + 1,
        .block_count = blocks,
        .constant_count = 1,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = cycle ? IR_VOID : IR_I64;
        f->constants[0].low = 17;
        ir_optimize_test_instruction(f, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            f->block_offsets[(uint64_t)block + 1] = block + 2;
            const bool last = block + 1 == blocks;
            const uint32_t target = last ? blocks / 2 : block + 1;
            ir_optimize_test_instruction(f, block + 1, last && !cycle ? IR_RET : IR_BR, IR_VOID, last && !cycle ? 0 : target, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
    }
    return created;
}

static void ir_optimize_test_threading(IrOptimizeTest* test, Arena* scratch)
{
    uint32_t direct[129];
    uint32_t scalar[129];
    uint32_t vector[129];
    const uint64_t mark = scratch->length;
    for (uint32_t count = 0; count <= 129; count += 1)
    {
        for (uint32_t trial = 0; trial < 8; trial += 1)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                direct[i] = trial == 0 ? (i + 1) % count : trial == 1 ? count - 1 : ir_optimize_test_random(test) % count;
            }
            const bool a = ir_optimize_thread_targets(scratch, direct, count, scalar, true);
            const bool b = ir_optimize_thread_targets(scratch, direct, count, vector, false);
            ir_optimize_test_expect(test, a && b && scratch->length == mark && memcmp(scalar, vector, count * sizeof(uint32_t)) == 0, "functional CFG scalar/vector canonical cycles");
            for (uint32_t i = 0; i < count; i += 1)
            {
                uint32_t node = i;
                for (uint32_t hop = 0; hop < count; hop += 1)
                {
                    node = direct[node];
                }
                uint32_t minimum = node;
                for (uint32_t hop = 0; hop < count; hop += 1)
                {
                    node = direct[node];
                    minimum = node < minimum ? node : minimum;
                }
                ir_optimize_test_expect(test, vector[i] == minimum, "jump threading versus independent orbit traversal");
            }
        }
    }
}

static void ir_optimize_test_limits(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch)
{
    IrFunction f = { 0 };
    bool created = ir_optimize_test_diamond(input, &f, 2);
    ir_optimize_test_expect(test, created, "limit fixture allocation");
    if (created)
    {
        const uint64_t om = output->length;
        const uint64_t sm = scratch->length;
        IrOptimizeResult result = { .folded = 139 };
        const IrOptimizeResult saved = result;
        IrOptimizeOptions options = ir_optimize_options();
        f.a[4] = 6;
        const IrCheck bad_ssa = ir_optimize(&f, output, scratch, options, &result);
        ir_optimize_test_expect(test, bad_ssa.error == IR_ERROR_DOMINANCE && output->length == om && scratch->length == sm && memcmp(&result, &saved, sizeof(result)) == 0, "nondominating input rejected transactionally");
        f.a[4] = 1;
        Arena tiny = *output;
        tiny.capacity = tiny.length;
        const IrCheck no_memory = ir_optimize(&f, &tiny, scratch, options, &result);
        ir_optimize_test_expect(test, no_memory.error == IR_ERROR_MEMORY && tiny.length == om && scratch->length == sm && memcmp(&result, &saved, sizeof(result)) == 0, "allocation failure preserves output and result");
        options.passes = UINT32_MAX;
        const IrCheck invalid = ir_optimize(&f, output, scratch, options, &result);
        ir_optimize_test_expect(test, invalid.error == IR_ERROR_METADATA && output->length == om && scratch->length == sm && memcmp(&result, &saved, sizeof(result)) == 0, "invalid options rejected transactionally");
    }
    arena_pop_to(input, 0);
    const IrSizes sizes = {
        .instruction_count = 4,
        .block_count = 1,
        .constant_count = 2,
    };
    created = ir_create(input, &sizes, &f) == IR_OK;
    ir_optimize_test_expect(test, created, "wide constant fixture allocation");
    if (created)
    {
        f.result_type = IR_I128;
        f.block_offsets[1] = 4;
        f.constants[0].low = 1;
        f.constants[0].high = 1;
        f.constants[1] = f.constants[0];
        ir_optimize_test_instruction(&f, 0, IR_CONST, IR_I128, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 1, IR_CONST, IR_I128, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 2, IR_MUL, IR_I128, 0, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        IrOptimizeResult result = { 0 };
        const IrCheck check = ir_optimize(&f, output, scratch, ir_optimize_options(), &result);
        const bool folded = check.error == IR_OK && result.function.instruction_count == 2 && result.function.op[0] == IR_CONST;
        ir_optimize_test_expect(test, folded && result.function.constants[result.function.a[0]].low == 1 && result.function.constants[result.function.a[0]].high == 2, "i128 multiplication has independently known cross-limb result");
        arena_pop_to(output, 0);
        f.result_type = IR_I8;
        f.type[0] = IR_I8;
        f.type[1] = IR_I64;
        f.type[2] = IR_I8;
        f.op[2] = IR_SHL;
        f.constants[0] = (IrConstant){ .low = 128 };
        f.constants[1] = (IrConstant){ .low = 256 };
        Arena reference = arena_reserve(1ull << 20);
        if (reference.base != 0)
        {
            ir_optimize_test_compare(test, &f, output, &reference, scratch, 7);
        }
        else
        {
            ir_optimize_test_expect(test, false, "mixed-width fixture arena");
        }
        arena_release(&reference);
    }
    arena_pop_to(input, 0);
}

static void ir_optimize_test_entry_phi(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch)
{
    const IrSizes sizes = { .instruction_count = 4, .block_count = 2, .constant_count = 1, .extra_count = 2 };
    IrFunction f = { 0 };
    const bool created = ir_create(input, &sizes, &f) == IR_OK;
    ir_optimize_test_expect(test, created, "entry phi fixture allocation");
    if (created)
    {
        f.result_type = IR_I64;
        f.block_offsets[1] = 2;
        f.block_offsets[2] = 4;
        f.constants[0].low = 7;
        f.extra[0] = 1;
        f.extra[1] = 2;
        ir_optimize_test_instruction(&f, 0, IR_PHI, IR_I64, 0, 1, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 1, IR_RET, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&f, 3, IR_BR, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const IrCheck verified = ir_verify(&f, scratch, 0);
        ir_optimize_test_expect(test, verified.error == IR_ERROR_PHI && verified.instruction == 0, "shared IR rejects phi with unreachable entry predecessor");
        for (uint32_t passes = 0; passes < 8; passes += 1)
        {
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                const uint64_t om = output->length;
                const uint64_t sm = scratch->length;
                IrOptimizeOptions options = ir_optimize_options();
                options.passes = passes;
                options.path = scalar != 0 ? BUSTER_IR_OPTIMIZE_SCALAR : BUSTER_IR_OPTIMIZE_VECTOR;
                IrOptimizeResult result = { .folded = 139 };
                const IrOptimizeResult saved = result;
                const IrCheck check = ir_optimize(&f, output, scratch, options, &result);
                ir_optimize_test_expect(test, check.error == IR_ERROR_PHI && check.instruction == 0 && output->length == om && scratch->length == sm && memcmp(&result, &saved, sizeof(result)) == 0, "entry phi explicitly refused without output mutation for every pass mask");
                ir_optimize_test_expect(test, f.op[0] == IR_PHI && f.b[0] == 1 && f.extra[0] == 1 && f.extra[1] == 2, "entry phi input remains intact");
            }
        }
    }
    arena_pop_to(input, 0);
}

// #131's labels through the CFG cleanup (#783), each with a br_indirect that picks label 0 for an odd parameter: kind 0
// threads label 0's block, a jump alone, to its target (the label moves there), kind 1 removes a block that no label or
// edge reaches beside the live br_indirect (every label keeps its block), and kind 2 removes the function's only
// br_indirect, in a block a constant branch leaves dead, with its labels' blocks (the labels then have none).
static bool ir_optimize_test_labels(Arena* arena, IrFunction* f, uint32_t kind)
{
    const uint32_t blocks = kind == 2 ? 5 : 4;
    const IrSizes sizes = {
        .instruction_count = kind == 0 ? 10 : kind == 1 ? 11 : 13,
        .block_count = blocks,
        .constant_count = 4,
        .parameter_count = 1,
        .label_count = 2,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I1;
        f->symbol_count = 128;
        f->constants[0].low = 10;
        f->constants[1].low = 20;
        f->constants[2].low = 30;
        f->constants[3].low = 1;
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        // The selecting block: b0, or for kind 2 b2, behind b0's constant branch to b1.
        uint32_t first = 1;
        if (kind == 2)
        {
            ir_optimize_test_instruction(f, 1, IR_CONST, IR_I1, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 2, IR_CBR, IR_VOID, 1, 1, 2);
            ir_optimize_test_instruction(f, 3, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, 4, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            f->block_offsets[1] = 3;
            f->block_offsets[2] = 5;
            first = 5;
        }
        ir_optimize_test_instruction(f, first, IR_SYMBOL_ADDR, IR_PTR, BUSTER_IR_OPTIMIZE_TEST_LABEL, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, first + 1, IR_SYMBOL_ADDR, IR_PTR, BUSTER_IR_OPTIMIZE_TEST_LABEL + 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, first + 2, IR_SELECT, IR_PTR, 0, first, first + 1);
        ir_optimize_test_instruction(f, first + 3, IR_BR_INDIRECT, IR_VOID, first + 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t next = first + 4;
        uint32_t block = kind == 2 ? 3 : 2;
        if (kind != 2)
        {
            // b1: label 0's jump alone, to b3 (kind 0), or a block with no label and no edge (kind 1).
            f->block_offsets[1] = next;
            if (kind == 0)
            {
                ir_optimize_test_instruction(f, next, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
                next += 1;
            }
            else
            {
                ir_optimize_test_instruction(f, next, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_optimize_test_instruction(f, next + 1, IR_RET, IR_VOID, next, BUSTER_IR_NONE, BUSTER_IR_NONE);
                next += 2;
            }
        }
        // Two returning blocks: 20 then 10 (kind 0: label 1's, then label 0's jump's target), or 10 then 20 (label 0's,
        // then label 1's).
        for (uint32_t r = 0; r < 2; r += 1)
        {
            f->block_offsets[block + r] = next;
            ir_optimize_test_instruction(f, next, IR_CONST, IR_I64, kind == 0 ? 1 - r : r, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(f, next + 1, IR_RET, IR_VOID, next, BUSTER_IR_NONE, BUSTER_IR_NONE);
            next += 2;
        }
        f->block_offsets[blocks] = next;
        f->labels[0] = kind == 0 ? 1 : block;
        f->labels[1] = kind == 0 ? 2 : block + 1;
    }
    return created;
}

// An aggregate call (#131's ABI records) whose result's destination follows a dead instruction (#783): DCE renumbers the
// destination, which the call's c must follow, and its old number becomes the argument's slot. It passes 16 bytes on
// the stack and the parameter, and returns 16 bytes in registers.
static bool ir_optimize_test_aggregate_call(Arena* arena, IrFunction* f)
{
    const IrSizes sizes = {
        .instruction_count = 8,
        .block_count = 1,
        .extra_count = 2,
        .slot_count = 2,
        .call_count = 1,
        .call_type_count = 2,
        .parameter_count = 1,
        .abi_call_count = 1,
        .passed_count = 2,
    };
    const bool created = ir_create(arena, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->parameter_types[0] = IR_I64;
        f->symbol_count = 128;
        f->block_offsets[1] = 8;
        for (uint32_t slot = 0; slot < 2; slot += 1)
        {
            f->slots[slot] = (IrSlot){
                .size = 16,
                .alignment = 8,
                .type = IR_VOID,
                .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN,
            };
        }
        f->extra[0] = 3;
        f->extra[1] = 0;
        f->call_types[0] = IR_PTR;
        f->call_types[1] = IR_I64;
        f->calls[0] = (IrCall){
            .argument_count = 2,
            .parameter_count = 2,
            .result_type = IR_VOID,
            .abi = 1,
        };
        f->abi_calls[0] = (IrAbiCall){
            .call = {
                .stack_bytes = 16,
                .stack_alignment_log2 = 4,
                .integer_registers = 1,
                .result = {
                    .kind = BUSTER_ABI_RESULT_REGISTERS,
                    .part_bytes = 8,
                    .registers = {
                        BUSTER_ABI_RAX,
                        BUSTER_ABI_RDX,
                        BUSTER_ABI_REGISTER_NONE,
                        BUSTER_ABI_REGISTER_NONE,
                    },
                },
            },
            .result_size = 16,
        };
        f->passed[0] = (IrPassed){
            .location = {
                .registers = {
                    BUSTER_ABI_REGISTER_NONE,
                    BUSTER_ABI_REGISTER_NONE,
                },
                .kind = BUSTER_ABI_LOCATION_STACK,
            },
            .size = 16,
        };
        f->passed[1] = (IrPassed){
            .location = {
                .registers = {
                    BUSTER_ABI_RDI,
                    BUSTER_ABI_REGISTER_NONE,
                },
                .kind = BUSTER_ABI_LOCATION_REGISTERS,
            },
        };
        ir_optimize_test_instruction(f, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 1, IR_NEG, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 2, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 3, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 4, IR_STORE, IR_VOID, 3, 0, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 5, IR_CALL, IR_VOID, 50, 0, 2);
        ir_optimize_test_instruction(f, 6, IR_LOAD, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(f, 7, IR_RET, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

// #783: the labels' and the aggregate call's fixtures through every pass mask, then optimized by every pass on both
// paths, with each label's block and the call's destination as the test works them out.
static void ir_optimize_test_records(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* scalar, Arena* scratch)
{
    for (uint32_t kind = 0; kind < 4; kind += 1)
    {
        IrFunction f = { 0 };
        const bool created = kind < 3 ? ir_optimize_test_labels(input, &f, kind) : ir_optimize_test_aggregate_call(input, &f);
        ir_optimize_test_expect(test, created && ir_verify(&f, scratch, 0).error == IR_OK, "labels and ABI records fixtures verify");
        for (uint32_t passes = 0; created && passes < 8; passes += 1)
        {
            ir_optimize_test_compare(test, &f, vector, scalar, scratch, passes);
        }
        for (uint32_t path = 0; created && path < 2; path += 1)
        {
            const uint64_t mark = vector->length;
            IrOptimizeOptions options = ir_optimize_options();
            options.path = path != 0 ? BUSTER_IR_OPTIMIZE_SCALAR : BUSTER_IR_OPTIMIZE_VECTOR;
            IrOptimizeResult result = { 0 };
            const IrCheck check = ir_optimize(&f, vector, scratch, options, &result);
            const IrFunction* out = &result.function;
            bool expected = check.error == IR_OK;
            if (expected && kind == 0)
            {
                // b1 is threaded to b3: b0, b2 and b3 are blocks 0 to 2, and label 0 is b3's.
                expected = out->block_count == 3 && out->label_count == 2 && out->labels[0] == 2 && out->labels[1] == 1;
            }
            else if (expected && kind == 1)
            {
                expected = out->block_count == 3 && out->label_count == 2 && out->labels[0] == 1 && out->labels[1] == 2;
            }
            else if (expected && kind == 2)
            {
                bool indirect = false;
                for (uint32_t i = 0; i < out->instruction_count; i += 1)
                {
                    indirect |= out->op[i] == IR_BR_INDIRECT;
                }
                expected = !indirect && out->block_count == 2 && out->label_count == 2 && out->labels[0] == BUSTER_IR_NONE && out->labels[1] == BUSTER_IR_NONE;
            }
            else if (expected)
            {
                // The neg is gone: the call writes where the first stack_addr's new value points, with the input's
                // records.
                const uint32_t call = 4;
                expected = out->instruction_count == 7 && out->op[call] == IR_CALL && out->c[call] == 1 && out->op[1] == IR_STACK_ADDR && out->a[1] == 0 && out->calls[out->b[call]].abi == 1 && out->abi_call_count == 1 &&
                           memcmp(out->abi_calls, f.abi_calls, sizeof(IrAbiCall)) == 0 && out->passed_count == 2 && memcmp(out->passed, f.passed, 2 * sizeof(IrPassed)) == 0;
            }
            ir_optimize_test_expect(test, expected, kind == 0 ? "a threaded block's label moves to its target" : kind == 1 ? "labels keep their blocks beside a removed unreachable block" : kind == 2 ? "a dead br_indirect's labels have no block" : "an aggregate call's destination follows DCE's renumbering");
            arena_pop_to(vector, mark);
        }
        arena_pop_to(input, 0);
    }
}

static void ir_optimize_test_ir(IrOptimizeTest* test)
{
    Arena input = arena_reserve(16ull << 20);
    Arena vector = arena_reserve(16ull << 20);
    Arena scalar = arena_reserve(16ull << 20);
    Arena scratch = arena_reserve(32ull << 20);
    const bool ready = input.base != 0 && vector.base != 0 && scalar.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "IR fixture arenas");
    if (ready)
    {
        for (uint32_t kind = 0; kind < 4; kind += 1)
        {
            IrFunction function = { 0 };
            const bool created = ir_optimize_test_diamond(&input, &function, kind);
            ir_optimize_test_expect(test, created, "diamond fixture allocation");
            for (uint32_t passes = 0; created && passes < 8; passes += 1)
            {
                ir_optimize_test_compare(test, &function, &vector, &scalar, &scratch, passes);
            }
            arena_pop_to(&input, 0);
        }
        IrFunction loop = { 0 };
        const bool loop_created = ir_optimize_test_loop(&input, &loop);
        ir_optimize_test_expect(test, loop_created, "loop fixture allocation");
        for (uint32_t passes = 0; loop_created && passes < 8; passes += 1)
        {
            ir_optimize_test_compare(test, &loop, &vector, &scalar, &scratch, passes);
        }
        arena_pop_to(&input, 0);
        for (uint32_t kind = 0; kind < 2; kind += 1)
        {
            IrFunction effects = { 0 };
            const bool effects_created = ir_optimize_test_effects(&input, &effects, kind != 0);
            ir_optimize_test_expect(test, effects_created, "effects fixture allocation");
            for (uint32_t passes = 0; effects_created && passes < 8; passes += 1)
            {
                ir_optimize_test_compare(test, &effects, &vector, &scalar, &scratch, passes);
            }
            arena_pop_to(&input, 0);
            IrFunction switch_function = { 0 };
            const bool switch_created = ir_optimize_test_switch(&input, &switch_function, kind != 0);
            ir_optimize_test_expect(test, switch_created, "switch fixture allocation");
            for (uint32_t passes = 0; switch_created && passes < 8; passes += 1)
            {
                ir_optimize_test_compare(test, &switch_function, &vector, &scalar, &scratch, passes);
            }
            arena_pop_to(&input, 0);
            IrFunction jumps = { 0 };
            const bool jumps_created = ir_optimize_test_jumps(&input, &jumps, 67, kind != 0);
            ir_optimize_test_expect(test, jumps_created, "jump chain fixture allocation");
            if (jumps_created)
            {
                ir_optimize_test_compare(test, &jumps, &vector, &scalar, &scratch, 7);
            }
            arena_pop_to(&input, 0);
        }
        ir_optimize_test_threading(test, &scratch);
        ir_optimize_test_records(test, &input, &vector, &scalar, &scratch);
        ir_optimize_test_limits(test, &input, &vector, &scratch);
        ir_optimize_test_entry_phi(test, &input, &vector, &scratch);
        for (uint32_t length = 0; length < 130; length += 1)
        {
            IrFunction function = { 0 };
            const bool created = ir_optimize_test_random_dag(&input, &function, test, length);
            ir_optimize_test_expect(test, created, "random DAG fixture allocation");
            if (created)
            {
                ir_optimize_test_compare(test, &function, &vector, &scalar, &scratch, 7);
            }
            arena_pop_to(&input, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&scalar);
    arena_release(&vector);
    arena_release(&input);
}

// #175's text-form pass fixtures, defined after the per-pass implementations in ir_optimize_text_test.c.
static void ir_optimize_text_tests(IrOptimizeTest* test);
static int ir_optimize_text_print_fixtures(void);

static __attribute__((unused)) int run_ir_optimize_tests(int argc, char** argv)
{
    IrOptimizeTest test = { .random = 139 };
    bool arguments_valid = true;
    bool print_text = false;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            test.random = strtoull(argv[++i], &end, 0);
            arguments_valid = errno == 0 && *end == 0 && test.random != 0;
        }
        else if (strcmp(argv[i], "--print-text") == 0)
        {
            print_text = true;
        }
        else
        {
            arguments_valid = false;
        }
    }
    int result = 2;
    if (arguments_valid && print_text)
    {
        result = ir_optimize_text_print_fixtures();
    }
    else if (arguments_valid)
    {
        ir_optimize_test_folds(&test);
        ir_optimize_test_fold_guards(&test);
        ir_optimize_test_ir(&test);
        ir_optimize_text_tests(&test);
        printf("ir-optimize: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
        result = test.failures == 0 ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-optimize [--seed N] [--print-text]\n");
    }
    return result;
}
