// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Raw-IR dataflow qualification, included after ir_dataflow.c by ir_source_test.c. The independent oracle enumerates
// operands by semantic opcode, NOT ir_operand_table/ir_operand. It scans instructions backwards and places phi uses
// on the actual incoming edges. Small fixtures also use byte-per-original-value fixpoints, with numeric reverse block
// order rather than dataflow.c's postorder. Every source form gets scalar/vector identity and independent equation,
// dense-map, CFG and padding checks. No oracle is in a measured kernel.

#include <sys/mman.h>
#include <unistd.h>

_Static_assert(IR_OP_COUNT == 61, "audit the independent dataflow operand oracle for a new IR operation");

typedef struct IrDataflowTests IrDataflowTests;
struct IrDataflowTests
{
    uint64_t checks;
    uint64_t failures;
};

static void ir_dataflow_expect(IrDataflowTests* tests, bool condition, const char* reason)
{
    tests->checks += 1;
    if (!condition)
    {
        if (tests->failures < 20)
        {
            fprintf(stderr, "ir-dataflow: %s\n", reason);
        }
        tests->failures += 1;
    }
}

// Semantic direct operands only. Calls' argument lists and phis' edge uses are enumerated independently below.
static uint32_t ir_dataflow_test_direct(const IrFunction* f, uint32_t node, uint32_t values[3])
{
    uint32_t count = 0;
    switch (f->op[node])
    {
        case IR_COPY: case IR_NEG: case IR_NOT: case IR_FNEG:
        case IR_TRUNC: case IR_ZEXT: case IR_SEXT: case IR_FTRUNC: case IR_FEXT:
        case IR_SITOFP: case IR_UITOFP: case IR_FPTOSI: case IR_FPTOUI:
        case IR_INTTOPTR: case IR_PTRTOINT: case IR_BITCAST: case IR_LOAD:
        case IR_CBR: case IR_SWITCH: case IR_ALLOCA: case IR_STACK_RESTORE: case IR_BR_INDIRECT:
        {
            values[count++] = f->a[node];
        } break;
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_SDIV: case IR_UDIV: case IR_SREM: case IR_UREM:
        case IR_AND: case IR_OR: case IR_XOR: case IR_SHL: case IR_LSHR: case IR_ASHR:
        case IR_FADD: case IR_FSUB: case IR_FMUL: case IR_FDIV: case IR_ICMP: case IR_FCMP: case IR_STORE: case IR_VCMP:
        {
            values[count++] = f->a[node];
            values[count++] = f->b[node];
        } break;
        case IR_SELECT: case IR_VSELECT:
        {
            values[count++] = f->a[node];
            values[count++] = f->b[node];
            values[count++] = f->c[node];
        } break;
        case IR_ADDRESS:
        {
            values[count++] = f->a[node];
            if (f->b[node] != BUSTER_IR_NONE)
            {
                values[count++] = f->b[node];
            }
        } break;
        case IR_RET:
        {
            if (f->a[node] != BUSTER_IR_NONE)
            {
                values[count++] = f->a[node];
            }
        } break;
        case IR_CALL: case IR_CALL_INDIRECT:
        {
            if (f->op[node] == IR_CALL_INDIRECT)
            {
                values[count++] = f->a[node];
            }
            if (f->c[node] != BUSTER_IR_NONE)
            {
                values[count++] = f->c[node];
            }
        } break;
        default: break;
    }
    return count;
}

static void ir_dataflow_test_bit(uint64_t* row, uint32_t id)
{
    if (id != BUSTER_IR_NONE)
    {
        row[id >> 6] |= UINT64_C(1) << (id & 63);
    }
}

static bool ir_dataflow_test_bytes(const void* left, const void* right, uint64_t bytes)
{
    return bytes == 0 || memcmp(left, right, (size_t)bytes) == 0;
}

static bool ir_dataflow_test_equal(const IrDataflow* a, const IrDataflow* b)
{
    const uint64_t words = (uint64_t)a->flow.block_count * a->flow.row_words;
    const bool equal = a->instruction_count == b->instruction_count && a->flow.block_count == b->flow.block_count &&
        a->flow.value_count == b->flow.value_count && a->flow.edge_count == b->flow.edge_count && a->flow.row_words == b->flow.row_words &&
        a->phi_use_count == b->phi_use_count &&
        ir_dataflow_test_bytes(a->ids, b->ids, (uint64_t)a->instruction_count * 4) &&
        ir_dataflow_test_bytes(a->values, b->values, (uint64_t)a->flow.value_count * 4) &&
        ir_dataflow_test_bytes(a->phi_offsets, b->phi_offsets, ((uint64_t)a->flow.block_count + 1) * 4) &&
        ir_dataflow_test_bytes(a->phi_values, b->phi_values, (uint64_t)a->phi_use_count * 4) &&
        ir_dataflow_test_bytes(a->phi_targets, b->phi_targets, (uint64_t)a->phi_use_count * 4) &&
        ir_dataflow_test_bytes(a->flow.successor_offsets, b->flow.successor_offsets, ((uint64_t)a->flow.block_count + 1) * 4) &&
        ir_dataflow_test_bytes(a->flow.successors, b->flow.successors, (uint64_t)a->flow.edge_count * 4) &&
        ir_dataflow_test_bytes(a->flow.postorder, b->flow.postorder, (uint64_t)a->flow.block_count * 4) &&
        ir_dataflow_test_bytes(a->flow.use, b->flow.use, words * 8) && ir_dataflow_test_bytes(a->flow.def, b->flow.def, words * 8) &&
        ir_dataflow_test_bytes(a->flow.in, b->flow.in, words * 8) && ir_dataflow_test_bytes(a->flow.out, b->flow.out, words * 8);
    return equal;
}

// O(instructions + operands + edges*row_words) space/work apart from visiting each successor's actual phi inputs.
// This checks the map independently before using dense IDs, so omission of a cross-block operand cannot make two
// compressed implementations agree accidentally. Duplicate raw successor edges are harmless unions, not CFG inputs.
static bool ir_dataflow_test_equations(const IrFunction* f, const IrDataflow* result, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint64_t words = result->flow.row_words;
    bool valid = true;
    uint32_t* blocks = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &valid);
    uint8_t* cross = ir_allocate(scratch, f->instruction_count, 1, &valid);
    uint64_t* live = ir_allocate(scratch, words, sizeof(uint64_t), &valid);
    uint64_t* use = ir_allocate(scratch, words, sizeof(uint64_t), &valid);
    uint64_t* def = ir_allocate(scratch, words, sizeof(uint64_t), &valid);
    for (uint32_t block = 0; valid && block < f->block_count; block += 1)
    {
        for (uint32_t node = f->block_offsets[block]; node < f->block_offsets[block + 1]; node += 1)
        {
            blocks[node] = block;
        }
    }
    for (uint32_t node = 0; valid && node < f->instruction_count; node += 1)
    {
        uint32_t values[3];
        const uint32_t count = ir_dataflow_test_direct(f, node, values);
        for (uint32_t i = 0; i < count; i += 1)
        {
            cross[values[i]] |= blocks[values[i]] != blocks[node];
        }
        if (f->op[node] == IR_CALL || f->op[node] == IR_CALL_INDIRECT)
        {
            const IrCall call = f->calls[f->b[node]];
            for (uint32_t i = 0; i < call.argument_count; i += 1)
            {
                const uint32_t value = f->extra[call.argument_first + i];
                cross[value] |= blocks[value] != blocks[node];
            }
        }
        else if (f->op[node] == IR_PHI)
        {
            for (uint32_t i = 0; i < f->b[node]; i += 1)
            {
                cross[f->extra[f->a[node] + i * 2 + 1]] = 1;
            }
        }
    }
    uint32_t dense = 0;
    for (uint32_t value = 0; valid && value < f->instruction_count; value += 1)
    {
        if (cross[value])
        {
            valid = dense < result->flow.value_count && result->ids[value] == dense && result->values[dense] == value;
            dense += 1;
        }
        else
        {
            valid = result->ids[value] == BUSTER_IR_NONE;
        }
    }
    valid = valid && dense == result->flow.value_count && words == ((uint64_t)dense + 511) / 512 * 8;
    for (uint32_t block = 0; valid && block < f->block_count; block += 1)
    {
        if (words != 0)
        {
            memset(live, 0, words * 8);
            memset(use, 0, words * 8);
            memset(def, 0, words * 8);
        }
        const uint32_t last = f->block_offsets[block + 1] - 1;
        const uint32_t successors = ir_successor_count(f, last);
        for (uint32_t e = 0; e < successors; e += 1)
        {
            const uint32_t target = ir_successor(f, last, e);
            for (uint64_t word = 0; word < words; word += 1)
            {
                live[word] |= result->flow.in[(uint64_t)target * words + word];
            }
            for (uint32_t phi = f->block_offsets[target]; phi < f->block_offsets[target + 1] && f->op[phi] == IR_PHI; phi += 1)
            {
                for (uint32_t p = 0; p < f->b[phi]; p += 1)
                {
                    const uint32_t predecessor = f->extra[f->a[phi] + p * 2];
                    const uint32_t value = f->extra[f->a[phi] + p * 2 + 1];
                    if (predecessor == block)
                    {
                        ir_dataflow_test_bit(live, result->ids[value]);
                        if (blocks[value] != block)
                        {
                            ir_dataflow_test_bit(use, result->ids[value]);
                        }
                    }
                }
            }
        }
        if (words != 0)
        {
            valid = ir_dataflow_test_bytes(live, result->flow.out + (uint64_t)block * words, words * 8);
        }
        for (uint32_t end = f->block_offsets[block + 1]; end > f->block_offsets[block];)
        {
            const uint32_t node = --end;
            const uint32_t id = result->ids[node];
            if (id != BUSTER_IR_NONE)
            {
                live[id >> 6] &= ~(UINT64_C(1) << (id & 63));
                ir_dataflow_test_bit(def, id);
            }
            uint32_t values[3];
            const uint32_t count = ir_dataflow_test_direct(f, node, values);
            for (uint32_t i = 0; i < count; i += 1)
            {
                ir_dataflow_test_bit(live, result->ids[values[i]]);
                if (blocks[values[i]] != block)
                {
                    ir_dataflow_test_bit(use, result->ids[values[i]]);
                }
            }
            if (f->op[node] == IR_CALL || f->op[node] == IR_CALL_INDIRECT)
            {
                const IrCall call = f->calls[f->b[node]];
                for (uint32_t i = 0; i < call.argument_count; i += 1)
                {
                    const uint32_t value = f->extra[call.argument_first + i];
                    ir_dataflow_test_bit(live, result->ids[value]);
                    if (blocks[value] != block)
                    {
                        ir_dataflow_test_bit(use, result->ids[value]);
                    }
                }
            }
        }
        if (words != 0)
        {
            valid = valid && ir_dataflow_test_bytes(live, result->flow.in + (uint64_t)block * words, words * 8) &&
                ir_dataflow_test_bytes(use, result->flow.use + (uint64_t)block * words, words * 8) &&
                ir_dataflow_test_bytes(def, result->flow.def + (uint64_t)block * words, words * 8);
            for (uint64_t word = 0; valid && word < words; word += 1)
            {
                const uint64_t first = word * 64;
                const uint64_t remaining = dense > first ? dense - first : 0;
                const uint64_t active = remaining >= 64 ? UINT64_MAX : (UINT64_C(1) << remaining) - 1;
                valid = ((result->flow.in[(uint64_t)block * words + word] | result->flow.out[(uint64_t)block * words + word]) & ~active) == 0;
            }
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// A second algorithm for fixtures: full original-ID BYTE rows, reverse instruction transfer, reverse numeric block
// order, and raw branch/phi semantics. It does not use the adapter's use/def, map, phi CSR, CFG CSR or postorder.
static bool ir_dataflow_test_byte_oracle(const IrFunction* f, const IrDataflow* result, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = f->instruction_count;
    bool valid = true;
    uint8_t* in = ir_allocate(scratch, (uint64_t)count * f->block_count, 1, &valid);
    uint8_t* out = ir_allocate(scratch, (uint64_t)count * f->block_count, 1, &valid);
    uint8_t* live = ir_allocate(scratch, count, 1, &valid);
    bool changed = valid && f->block_count != 0;
    while (changed)
    {
        changed = false;
        for (uint32_t end = f->block_count; end != 0;)
        {
            const uint32_t block = --end;
            memset(live, 0, count);
            const uint32_t last = f->block_offsets[block + 1] - 1;
            const uint32_t successors = ir_successor_count(f, last);
            for (uint32_t e = 0; e < successors; e += 1)
            {
                const uint32_t target = ir_successor(f, last, e);
                for (uint32_t value = 0; value < count; value += 1)
                {
                    live[value] |= in[(uint64_t)target * count + value];
                }
                for (uint32_t phi = f->block_offsets[target]; phi < f->block_offsets[target + 1] && f->op[phi] == IR_PHI; phi += 1)
                {
                    for (uint32_t p = 0; p < f->b[phi]; p += 1)
                    {
                        if (f->extra[f->a[phi] + p * 2] == block)
                        {
                            live[f->extra[f->a[phi] + p * 2 + 1]] = 1;
                        }
                    }
                }
            }
            memcpy(out + (uint64_t)block * count, live, count);
            for (uint32_t after = f->block_offsets[block + 1]; after > f->block_offsets[block];)
            {
                const uint32_t node = --after;
                live[node] = 0;
                uint32_t values[3];
                const uint32_t direct = ir_dataflow_test_direct(f, node, values);
                for (uint32_t i = 0; i < direct; i += 1)
                {
                    live[values[i]] = 1;
                }
                if (f->op[node] == IR_CALL || f->op[node] == IR_CALL_INDIRECT)
                {
                    const IrCall call = f->calls[f->b[node]];
                    for (uint32_t i = 0; i < call.argument_count; i += 1)
                    {
                        live[f->extra[call.argument_first + i]] = 1;
                    }
                }
            }
            changed |= memcmp(in + (uint64_t)block * count, live, count) != 0;
            memcpy(in + (uint64_t)block * count, live, count);
        }
    }
    for (uint32_t block = 0; valid && block < f->block_count; block += 1)
    {
        for (uint32_t value = 0; valid && value < count; value += 1)
        {
            const uint32_t id = result->ids[value];
            const uint64_t word = (uint64_t)block * result->flow.row_words + (id == BUSTER_IR_NONE ? 0 : id >> 6);
            const bool live_in = id != BUSTER_IR_NONE && ((result->flow.in[word] >> (id & 63)) & 1) != 0;
            const bool live_out = id != BUSTER_IR_NONE && ((result->flow.out[word] >> (id & 63)) & 1) != 0;
            valid = live_in == (in[(uint64_t)block * count + value] != 0) && live_out == (out[(uint64_t)block * count + value] != 0);
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}

static bool ir_dataflow_test_function(const IrFunction* f, Arena* output, Arena* scratch, bool oracle, IrDataflowTests* tests)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint64_t failures = tests->failures;
    IrDataflow vector = { 0 };
    IrDataflow scalar = { 0 };
    const IrCheck vector_check = ir_dataflow_prepare(f, output, scratch, &vector);
    const bool vector_scratch = scratch->length == scratch_mark;
    const IrCheck scalar_check = ir_dataflow_prepare_scalar(f, output, scratch, &scalar);
    const bool prepared = vector_check.error == IR_OK && scalar_check.error == IR_OK;
    ir_dataflow_expect(tests, prepared, "an accepted IR function could not be prepared");
    ir_dataflow_expect(tests, vector_scratch && scratch->length == scratch_mark, "preparation retained scratch");
    if (prepared)
    {
        const uint64_t vector_iterations = ir_dataflow_solve(&vector);
        const uint64_t scalar_iterations = ir_dataflow_solve_scalar(&scalar);
        ir_dataflow_expect(tests, vector_iterations == scalar_iterations, "scalar/vector sweep counts disagree");
        ir_dataflow_expect(tests, ir_dataflow_test_equal(&vector, &scalar), "scalar/vector maps, edge uses or matrices disagree");
        ir_dataflow_expect(tests, ir_dataflow_test_equations(f, &vector, scratch), "independent IR equations, dense IDs or padding disagree");
        const uint64_t bytes = (uint64_t)vector.flow.block_count * vector.flow.row_words * 8;
        ir_dataflow_expect(tests, bytes == 0 ? vector.flow.in == NULL && vector.flow.out == NULL && vector.flow.use == NULL && vector.flow.def == NULL :
            (((uintptr_t)vector.flow.in | (uintptr_t)vector.flow.out | (uintptr_t)vector.flow.use | (uintptr_t)vector.flow.def) & 63) == 0,
            "empty matrices or row alignment violate the contract");
        if (oracle)
        {
            ir_dataflow_expect(tests, ir_dataflow_test_byte_oracle(f, &vector, scratch), "original-ID byte oracle disagrees");
            if (bytes != 0)
            {
                memset(vector.flow.in, 0xff, bytes);
                memset(vector.flow.out, 0xff, bytes);
                memset(scalar.flow.in, 0xa5, bytes);
                memset(scalar.flow.out, 0x5a, bytes);
            }
            ir_dataflow_expect(tests, ir_dataflow_solve(&vector) == vector_iterations && ir_dataflow_solve_scalar(&scalar) == scalar_iterations &&
                ir_dataflow_test_equal(&vector, &scalar) && ir_dataflow_test_byte_oracle(f, &vector, scratch), "re-solving poisoned rows changed liveness");
        }
    }
    ir_dataflow_expect(tests, scratch->length == scratch_mark, "a dataflow check retained scratch");
    arena_pop_to(output, output_mark);
    arena_pop_to(scratch, scratch_mark);
    return tests->failures == failures;
}

static void ir_dataflow_test_node(IrFunction* f, uint32_t node, uint32_t op, IrType type, uint32_t a, uint32_t b, uint32_t c)
{
    f->op[node] = (uint8_t)op;
    f->type[node] = (uint8_t)type;
    f->a[node] = a;
    f->b[node] = b;
    f->c[node] = c;
}

// A critical edge 0->2 beside 0->1->2. The two phis deliberately reuse incoming values: an edge's phi operands are
// parallel uses, not instructions in the successor. Value 1 passes through block 1 without an ordinary use there.
static bool ir_dataflow_test_diamond(Arena* memory, IrFunction* f)
{
    const IrSizes sizes = { .instruction_count = 10, .block_count = 3, .extra_count = 8, .constant_count = 2 };
    const bool created = ir_create(memory, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->constants[0].low = 11;
        f->constants[1].low = 22;
        const uint32_t offsets[] = { 0, 4, 6, 10 };
        const uint32_t extra[] = { 0, 0, 1, 4, 0, 0, 1, 1 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        memcpy(f->extra, extra, sizeof(extra));
        ir_dataflow_test_node(f, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 1, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 2, IR_UNDEF, IR_I1, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 3, IR_CBR, IR_VOID, 2, 1, 2);
        ir_dataflow_test_node(f, 4, IR_COPY, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 5, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 6, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 7, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 8, IR_ADD, IR_I64, 6, 7, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 9, IR_RET, IR_VOID, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

static bool ir_dataflow_test_swap(Arena* memory, IrFunction* f)
{
    const IrSizes sizes = { .instruction_count = 9, .block_count = 3, .extra_count = 8, .constant_count = 2 };
    const bool created = ir_create(memory, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->constants[0].low = 11;
        f->constants[1].low = 22;
        const uint32_t offsets[] = { 0, 4, 8, 9 };
        const uint32_t extra[] = { 0, 0, 1, 5, 0, 1, 1, 4 };
        memcpy(f->block_offsets, offsets, sizeof(offsets));
        memcpy(f->extra, extra, sizeof(extra));
        ir_dataflow_test_node(f, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 1, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 2, IR_UNDEF, IR_I1, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 3, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 4, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 5, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 6, IR_UNDEF, IR_I1, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 7, IR_CBR, IR_VOID, 6, 1, 2);
        ir_dataflow_test_node(f, 8, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

static uint32_t ir_dataflow_test_random(uint32_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

// Five instructions/block, local arithmetic and a phi for every nonentry block with predecessors. Incoming values
// are the predecessor's arithmetic result, including self/back edges. A value from entry is used throughout the
// reachable region, requiring live-through propagation; dead components use their own constants. mode 0 is a loop chain, mode 1 nested loops,
// and mode 2 arbitrary edges (multiple-entry SCCs, duplicate edges and unreachable components are all possible).
static bool ir_dataflow_test_graph(Arena* memory, Arena* scratch, uint32_t blocks, uint32_t seed, uint32_t mode, IrFunction* f)
{
    const uint64_t mark = scratch->length;
    bool valid = true;
    uint32_t* edges = ir_allocate(scratch, (uint64_t)blocks * 2, sizeof(uint32_t), &valid);
    uint32_t* counts = ir_allocate(scratch, blocks, sizeof(uint32_t), &valid);
    uint8_t* reachable = ir_allocate(scratch, blocks, 1, &valid);
    uint64_t extra = 0;
    for (uint32_t b = 0; valid && b < blocks; b += 1)
    {
        edges[b * 2] = mode == 2 ? ir_dataflow_test_random(&seed) % blocks : (b + 1) % blocks;
        edges[b * 2 + 1] = mode == 2 ? ir_dataflow_test_random(&seed) % blocks : mode == 1 && b > 1 ? b / 2 : (b + blocks - 1) % blocks;
        for (uint32_t i = 0; i < 2; i += 1)
        {
            const uint32_t target = edges[b * 2 + i];
            if (target != 0 && (i == 0 || target != edges[b * 2]))
            {
                counts[target] += 1;
                extra += 2;
            }
        }
    }
    if (valid)
    {
        reachable[0] = 1;
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                if (reachable[b])
                {
                    for (uint32_t edge = 0; edge < 2; edge += 1)
                    {
                        const uint32_t target = edges[b * 2 + edge];
                        changed |= reachable[target] == 0;
                        reachable[target] = 1;
                    }
                }
            }
        }
    }
    const IrSizes sizes = { .instruction_count = (uint64_t)blocks * 5, .block_count = blocks, .extra_count = extra, .constant_count = 1 };
    valid = valid && ir_create(memory, &sizes, f) == IR_OK;
    if (valid)
    {
        f->constants[0].low = 1;
        uint32_t place = 0;
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            const uint32_t node = b * 5;
            f->block_offsets[b] = node;
            if (counts[b] != 0)
            {
                ir_dataflow_test_node(f, node, IR_PHI, IR_I64, place, counts[b], BUSTER_IR_NONE);
                for (uint32_t predecessor = 0; predecessor < blocks; predecessor += 1)
                {
                    if (edges[predecessor * 2] == b || edges[predecessor * 2 + 1] == b)
                    {
                        f->extra[place++] = predecessor;
                        f->extra[place++] = predecessor * 5 + 2;
                    }
                }
            }
            else
            {
                ir_dataflow_test_node(f, node, IR_UNDEF, IR_I64, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            ir_dataflow_test_node(f, node + 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_dataflow_test_node(f, node + 2, IR_ADD, IR_I64, node, reachable[b] ? 1 : node + 1, BUSTER_IR_NONE);
            ir_dataflow_test_node(f, node + 3, IR_UNDEF, IR_I1, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_dataflow_test_node(f, node + 4, IR_CBR, IR_VOID, node + 3, edges[b * 2], edges[b * 2 + 1]);
        }
        f->block_offsets[blocks] = blocks * 5;
        valid = place == extra;
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// N values are all used in a successor, in reverse order with duplicates, across the 64/512-bit boundaries. Wide
// call operands and an indirect callee are real uses; the large direct symbol and call-table ID are NOT value IDs.
static bool ir_dataflow_test_call(Arena* memory, uint32_t count, IrFunction* f)
{
    const uint32_t arguments = count * 2;
    const IrSizes sizes = { .instruction_count = count + 5, .block_count = 2, .extra_count = arguments,
        .call_count = 1, .call_type_count = arguments };
    const bool created = ir_create(memory, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        f->block_offsets[1] = count + 2;
        f->block_offsets[2] = count + 5;
        f->calls[0] = (IrCall){ .argument_count = arguments, .parameter_count = arguments, .result_type = IR_I64 };
        for (uint32_t i = 0; i < count; i += 1)
        {
            ir_dataflow_test_node(f, i, IR_UNDEF, IR_I64, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            f->extra[i * 2] = count - 1 - i;
            f->extra[i * 2 + 1] = count - 1 - i;
        }
        if (arguments != 0)
        {
            memset(f->call_types, IR_I64, arguments);
        }
        ir_dataflow_test_node(f, count, IR_UNDEF, IR_PTR, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, count + 1, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, count + 2, IR_CALL, IR_I64, INT32_MAX, 0, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, count + 3, IR_CALL_INDIRECT, IR_I64, count, 0, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, count + 4, IR_RET, IR_VOID, count + 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

// Long mixed-op blocks exercise every lane of the fixed-column extractor, including optional operands and metadata
// whose small integer happens also to name a value. All real inputs are defined in the preceding block.
static bool ir_dataflow_test_columns(Arena* memory, uint32_t count, IrFunction* f)
{
    const uint32_t entry = 68;
    const IrSizes sizes = { .instruction_count = entry + count + 1, .block_count = 2, .address_count = 1 };
    const bool created = ir_create(memory, &sizes, f) == IR_OK;
    if (created)
    {
        f->block_offsets[1] = entry;
        f->block_offsets[2] = entry + count + 1;
        f->addresses[0] = (IrAddress){ .offset = 40, .scale = 4 };
        for (uint32_t i = 0; i < 65; i += 1)
        {
            ir_dataflow_test_node(f, i, IR_UNDEF, IR_I64, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        ir_dataflow_test_node(f, 65, IR_UNDEF, IR_I1, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 66, IR_UNDEF, IR_PTR, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, 67, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t node = entry + i;
            const uint32_t a = i % 65;
            const uint32_t b = (i * 7) % 65;
            switch (i % 9)
            {
                case 0: ir_dataflow_test_node(f, node, IR_ADD, IR_I64, a, b, BUSTER_IR_NONE); break;
                case 1: ir_dataflow_test_node(f, node, IR_ICMP, IR_I1, a, b, IR_NE); break;
                case 2: ir_dataflow_test_node(f, node, IR_SELECT, IR_I64, 65, a, b); break;
                case 3: ir_dataflow_test_node(f, node, IR_ADDRESS, IR_PTR, 66, a, 0); break;
                case 4: ir_dataflow_test_node(f, node, IR_ADDRESS, IR_PTR, 66, BUSTER_IR_NONE, 0); break;
                case 5: ir_dataflow_test_node(f, node, IR_LOAD, IR_I64, 66, BUSTER_IR_NONE, BUSTER_IR_NONE); break;
                case 6: ir_dataflow_test_node(f, node, IR_STORE, IR_VOID, 66, a, BUSTER_IR_NONE); break;
                case 7: ir_dataflow_test_node(f, node, IR_COPY, IR_I64, b, BUSTER_IR_NONE, BUSTER_IR_NONE); break;
                default: ir_dataflow_test_node(f, node, IR_ZEXT, IR_I64, 65, BUSTER_IR_NONE, BUSTER_IR_NONE); break;
            }
        }
        ir_dataflow_test_node(f, entry + count, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

// A conditional chain with a common exit has exactly count distinct predecessors at that exit, even when the last
// branch's two destinations agree. Its phi pair slice ends at a guard page in the caller.
static bool ir_dataflow_test_fanin(Arena* memory, uint32_t count, IrFunction* f)
{
    const IrSizes sizes = { .instruction_count = (uint64_t)count * 3 + 2, .block_count = count + 1,
        .extra_count = count * 2, .constant_count = count };
    const bool created = ir_create(memory, &sizes, f) == IR_OK;
    if (created)
    {
        f->result_type = IR_I64;
        for (uint32_t b = 0; b < count; b += 1)
        {
            const uint32_t node = b * 3;
            f->block_offsets[b] = node;
            f->constants[b].low = b;
            f->extra[b * 2] = b;
            f->extra[b * 2 + 1] = node;
            ir_dataflow_test_node(f, node, IR_CONST, IR_I64, b, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_dataflow_test_node(f, node + 1, IR_UNDEF, IR_I1, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_dataflow_test_node(f, node + 2, IR_CBR, IR_VOID, node + 1, count, b + 1);
        }
        f->block_offsets[count] = count * 3;
        f->block_offsets[count + 1] = count * 3 + 2;
        ir_dataflow_test_node(f, count * 3, IR_PHI, IR_I64, 0, count, BUSTER_IR_NONE);
        ir_dataflow_test_node(f, count * 3 + 1, IR_RET, IR_VOID, count * 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    return created;
}

static void* ir_dataflow_test_guard(const void* source, uint64_t bytes, void** mappings, uint32_t* count, bool* valid)
{
    void* result = NULL;
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    if (*valid && bytes != 0)
    {
        *valid = bytes <= page;
        void* mapping = *valid ? mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
        *valid = mapping != MAP_FAILED;
        if (*valid)
        {
            mappings[(*count)++] = mapping;
            result = (uint8_t*)mapping + page - bytes;
            memcpy(result, source, (size_t)bytes);
            *valid = mprotect((uint8_t*)mapping + page, page, PROT_NONE) == 0 && mprotect(mapping, page, PROT_READ) == 0;
        }
    }
    return result;
}

static void ir_dataflow_test_guarded(const IrFunction* source, Arena* output, Arena* scratch, IrDataflowTests* tests)
{
    IrFunction f = *source;
    void* mappings[9];
    uint32_t count = 0;
    bool valid = true;
    const uint64_t nodes = f.instruction_count;
    f.op = ir_dataflow_test_guard(f.op, nodes, mappings, &count, &valid);
    f.type = ir_dataflow_test_guard(f.type, nodes, mappings, &count, &valid);
    f.flags = ir_dataflow_test_guard(f.flags, nodes, mappings, &count, &valid);
    f.alignment_log2 = ir_dataflow_test_guard(f.alignment_log2, nodes, mappings, &count, &valid);
    f.a = ir_dataflow_test_guard(f.a, nodes * 4, mappings, &count, &valid);
    f.b = ir_dataflow_test_guard(f.b, nodes * 4, mappings, &count, &valid);
    f.c = ir_dataflow_test_guard(f.c, nodes * 4, mappings, &count, &valid);
    f.block_offsets = ir_dataflow_test_guard(f.block_offsets, ((uint64_t)f.block_count + 1) * 4, mappings, &count, &valid);
    f.extra = ir_dataflow_test_guard(f.extra, (uint64_t)f.extra_count * 4, mappings, &count, &valid);
    ir_dataflow_expect(tests, valid, "guarded input mapping failed");
    if (valid)
    {
        ir_dataflow_test_function(&f, output, scratch, true, tests);
    }
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    for (uint32_t i = 0; i < count; i += 1)
    {
        munmap(mappings[i], page * 2);
    }
}

static void ir_dataflow_test_failures(IrFunction* function, Arena* output, Arena* scratch, IrDataflowTests* tests)
{
    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    IrDataflow result;
    memset(&result, 0xa5, sizeof(result));
    uint8_t before[sizeof(result)];
    memcpy(before, &result, sizeof(result));
    for (uint32_t path = 0; path < 2; path += 1)
    {
        Arena empty = *output;
        empty.capacity = empty.length;
        IrCheck check = ir_dataflow_prepare_path(function, &empty, scratch, &result, path == 0);
        ir_dataflow_expect(tests, check.error == IR_ERROR_MEMORY && empty.length == mark && scratch->length == temporary &&
            memcmp(before, &result, sizeof(result)) == 0, "output exhaustion was not transactional");
        empty = *scratch;
        empty.capacity = empty.length;
        check = ir_dataflow_prepare_path(function, output, &empty, &result, path == 0);
        ir_dataflow_expect(tests, check.error == IR_ERROR_MEMORY && output->length == mark && empty.length == temporary &&
            memcmp(before, &result, sizeof(result)) == 0, "scratch exhaustion was not transactional");
        check = ir_dataflow_prepare_path(function, output, output, &result, path == 0);
        ir_dataflow_expect(tests, check.error == IR_ERROR_STRUCTURE && output->length == mark &&
            memcmp(before, &result, sizeof(result)) == 0, "aliased arenas were not rejected transactionally");
        IrFunction invalid = *function;
        invalid.instruction_count = UINT32_MAX;
        check = ir_dataflow_prepare_path(&invalid, output, scratch, &result, path == 0);
        ir_dataflow_expect(tests, check.error == IR_ERROR_LIMIT && output->length == mark && scratch->length == temporary &&
            memcmp(before, &result, sizeof(result)) == 0, "signed-gather limit was not rejected before column access");
        const uint32_t value = function->extra[1];
        function->extra[1] = function->instruction_count;
        check = ir_dataflow_prepare_path(function, output, scratch, &result, path == 0);
        ir_dataflow_expect(tests, check.error == IR_ERROR_PHI && output->length == mark && scratch->length == temporary &&
            memcmp(before, &result, sizeof(result)) == 0, "malformed phi was not rejected transactionally");
        function->extra[1] = value;
        IrDataflow sizing = { 0 };
        check = ir_dataflow_prepare_path(function, output, scratch, &sizing, path == 0);
        const uint64_t needed = sizing.arena_bytes;
        ir_dataflow_expect(tests, check.error == IR_OK && needed != 0, "could not size the allocation-failure fixture");
        arena_pop_to(output, mark);
        // Fail inside the output's mapping, not only at its first push, including the final matrix allocation.
        for (uint64_t room = 1; room < needed; room += 31)
        {
            Arena bounded = *output;
            bounded.capacity = mark + room;
            check = ir_dataflow_prepare_path(function, &bounded, scratch, &result, path == 0);
            ir_dataflow_expect(tests, check.error == IR_ERROR_MEMORY && bounded.length == mark && scratch->length == temporary &&
                memcmp(before, &result, sizeof(result)) == 0, "intermediate output exhaustion was not transactional");
        }
    }
}

static bool ir_dataflow_tests(Arena* memory, Arena* output, Arena* scratch)
{
    const uint64_t mark = memory->length;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrDataflowTests tests = { 0 };
    IrFunction f = { 0 };
    const IrSizes empty = { 0 };
    bool created = ir_create(memory, &empty, &f) == IR_OK;
    ir_dataflow_expect(&tests, created, "empty fixture allocation failed");
    if (created)
    {
        ir_dataflow_test_function(&f, output, scratch, true, &tests);
    }
    arena_pop_to(memory, mark);
    created = ir_dataflow_test_diamond(memory, &f);
    ir_dataflow_expect(&tests, created, "critical-edge fixture allocation failed");
    if (created)
    {
        ir_dataflow_test_function(&f, output, scratch, true, &tests);
        ir_dataflow_test_guarded(&f, output, scratch, &tests);
        ir_dataflow_test_failures(&f, output, scratch, &tests);
    }
    arena_pop_to(memory, mark);
    created = ir_dataflow_test_swap(memory, &f);
    ir_dataflow_expect(&tests, created, "parallel-phi fixture allocation failed");
    if (created)
    {
        ir_dataflow_test_function(&f, output, scratch, true, &tests);
        ir_dataflow_test_guarded(&f, output, scratch, &tests);
    }
    arena_pop_to(memory, mark);
    const uint32_t counts[] = { 0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 511, 512, 513 };
    for (uint32_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i += 1)
    {
        created = ir_dataflow_test_call(memory, counts[i], &f);
        ir_dataflow_expect(&tests, created, "call-tail fixture allocation failed");
        if (created)
        {
            ir_dataflow_test_function(&f, output, scratch, true, &tests);
            if (counts[i] <= 127)
            {
                ir_dataflow_test_guarded(&f, output, scratch, &tests);
            }
        }
        arena_pop_to(memory, mark);
    }
    for (uint32_t count = 1; count <= 33; count += 1)
    {
        created = ir_dataflow_test_columns(memory, count, &f);
        ir_dataflow_expect(&tests, created, "mixed column fixture allocation failed");
        if (created)
        {
            ir_dataflow_test_guarded(&f, output, scratch, &tests);
        }
        arena_pop_to(memory, mark);
        created = ir_dataflow_test_fanin(memory, count, &f);
        ir_dataflow_expect(&tests, created, "phi-tail fixture allocation failed");
        if (created)
        {
            ir_dataflow_test_guarded(&f, output, scratch, &tests);
        }
        arena_pop_to(memory, mark);
    }
    const IrType types[] = { IR_I8, IR_I128, IR_F32, IR_F64, IR_F80, IR_V16I32, IR_M16 };
    for (uint32_t t = 0; t < sizeof(types) / sizeof(types[0]); t += 1)
    {
        created = ir_dataflow_test_call(memory, 17, &f);
        ir_dataflow_expect(&tests, created, "non-i64 value fixture allocation failed");
        if (created)
        {
            f.result_type = (uint8_t)types[t];
            f.calls[0].result_type = (uint8_t)types[t];
            memset(f.call_types, types[t], f.call_type_count);
            for (uint32_t node = 0; node < f.instruction_count; node += 1)
            {
                if (f.type[node] == IR_I64)
                {
                    f.type[node] = (uint8_t)types[t];
                }
            }
            ir_dataflow_test_function(&f, output, scratch, true, &tests);
        }
        arena_pop_to(memory, mark);
    }
    const uint32_t graph_sizes[] = { 1, 2, 3, 7, 16, 33, 65, 129, 513 };
    for (uint32_t size = 0; size < sizeof(graph_sizes) / sizeof(graph_sizes[0]); size += 1)
    {
        for (uint32_t mode = 0; mode < 3; mode += 1)
        {
            for (uint32_t seed = 1; seed <= 8; seed += 1)
            {
                created = ir_dataflow_test_graph(memory, scratch, graph_sizes[size], seed * 0x9e3779b9u, mode, &f);
                ir_dataflow_expect(&tests, created, "generated CFG allocation failed");
                if (created)
                {
                    ir_dataflow_test_function(&f, output, scratch, true, &tests);
                }
                arena_pop_to(memory, mark);
            }
        }
    }
    ir_dataflow_expect(&tests, output->length == output_mark && scratch->length == scratch_mark, "self-tests retained arena storage");
    arena_pop_to(memory, mark);
    printf("ir-dataflow: self-tests checks=%" PRIu64 " failures=%" PRIu64 " vector_host=%u\n", tests.checks, tests.failures, BUSTER_HOST_VECTOR);
    return tests.failures == 0;
}
