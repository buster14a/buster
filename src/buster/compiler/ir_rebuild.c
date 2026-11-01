// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Structural test-tool rewrites of the public scalar IR. A plan selects/permutates existing blocks and rows;
// direct aliases replace a removed value only with a retained value of the same type. Rebuild every sparse slice
// by occurrence, compact the stack slots that addresses and lifetime markers use and remap phi predecessors. Symbols
// and the function ABI stay stable. Input, output and scratch are disjoint. Both verifier paths, including reachable
// SSA dominance, must approve before a new view is published. Unknown operations and new vector/mask types fail
// closed, and so does metadata the rebuild doesn't remap (labels, by-value aggregates' ABI records, a variadic
// definition's incoming frame).
#ifndef BUSTER_IR_REBUILD_INCLUDED
#define BUSTER_IR_REBUILD_INCLUDED

typedef struct IrRebuildPlan IrRebuildPlan;
struct IrRebuildPlan
{
    // NULL blocks/rows mean the identity plan. A selected row must remain in its selected original block.
    const uint32_t* blocks;
    uint32_t block_count;
    const uint32_t* rows;
    const uint32_t* offsets;
    uint32_t instruction_count;
    // One entry per original row: NONE/self, or a retained original value. Chained aliases are not admitted.
    const uint32_t* aliases;
};

static uint32_t ir_rebuild_values(uint32_t op)
{
    uint32_t mask = BUSTER_IR_NONE;
    switch (op)
    {
        case IR_CONST: case IR_UNDEF: case IR_PARAM: case IR_STACK_ADDR: case IR_SYMBOL_ADDR:
        case IR_CALL: case IR_PHI: case IR_BR: case IR_RET: case IR_UNREACHABLE: case IR_STACK_SAVE: case IR_LIFETIME: mask = 0; break;
        case IR_COPY: case IR_NEG: case IR_NOT: case IR_FNEG: case IR_TRUNC: case IR_ZEXT: case IR_SEXT:
        case IR_FTRUNC: case IR_FEXT: case IR_SITOFP: case IR_UITOFP: case IR_FPTOSI: case IR_FPTOUI:
        case IR_INTTOPTR: case IR_PTRTOINT: case IR_BITCAST: case IR_ADDRESS: case IR_LOAD:
        case IR_CALL_INDIRECT: case IR_CBR: case IR_SWITCH: case IR_ALLOCA: case IR_STACK_RESTORE: mask = 1; break;
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_SDIV: case IR_UDIV: case IR_SREM: case IR_UREM:
        case IR_AND: case IR_OR: case IR_XOR: case IR_SHL: case IR_LSHR: case IR_ASHR:
        case IR_FADD: case IR_FSUB: case IR_FMUL: case IR_FDIV: case IR_ICMP: case IR_FCMP: case IR_STORE: mask = 3; break;
        case IR_SELECT: mask = 7; break;
        default: break;
    }
    return mask;
}

static uint32_t ir_rebuild_value(const IrFunction* source, const IrRebuildPlan* plan, const uint32_t* values, uint32_t value)
{
    uint32_t result = BUSTER_IR_NONE;
    if (value < source->instruction_count)
    {
        uint32_t target = value;
        if (plan != NULL && plan->aliases != NULL && plan->aliases[value] != BUSTER_IR_NONE)
        {
            target = plan->aliases[value];
        }
        if (target < source->instruction_count && source->type[target] == source->type[value] &&
            (plan == NULL || plan->aliases == NULL || plan->aliases[target] == BUSTER_IR_NONE || plan->aliases[target] == target))
        {
            result = values[target];
        }
    }
    return result;
}

// The function-level metadata a rebuild carries over is its signature and symbol bound alone: a function with labels,
// ABI records or a variadic frame is refused (IR_ERROR_METADATA) rather than copied without them.
static bool ir_rebuild_plain(const IrFunction* source)
{
    bool plain = source->label_count == 0 && source->abi_call_count == 0 && source->passed_count == 0 &&
                 source->parameter_passed == 0 && source->result_size == 0 && !source->variadic;
    for (uint32_t i = 0; plain && i < source->call_count; i += 1)
    {
        plain = source->calls[i].abi == 0;
    }
    return plain;
}

static bool ir_rebuild_overlap(const void* first, uint64_t first_bytes, const void* second, uint64_t second_bytes)
{
    const uintptr_t a = (uintptr_t)first;
    const uintptr_t b = (uintptr_t)second;
    const bool result = first_bytes != 0 && second_bytes != 0 &&
                        (a <= b ? b - a < first_bytes : a - b < second_bytes);
    return result;
}

static bool ir_rebuild_arena(const Arena* arena)
{
    const bool result = arena != NULL && arena->length <= arena->capacity &&
                        (arena->capacity == 0 || arena->base != NULL) &&
                        (uintptr_t)arena->base <= UINTPTR_MAX - arena->capacity;
    return result;
}

static bool ir_rebuild_borrow(const void* pointer, uint64_t bytes, const Arena* output, const Arena* scratch)
{
    const uintptr_t out = (uintptr_t)output->base + output->length;
    const uintptr_t temp = (uintptr_t)scratch->base + scratch->length;
    const bool result = !ir_rebuild_overlap(pointer, bytes, (void*)out, output->capacity - output->length) &&
                        !ir_rebuild_overlap(pointer, bytes, (void*)temp, scratch->capacity - scratch->length) &&
                        !ir_rebuild_overlap(pointer, bytes, output, sizeof(*output)) &&
                        !ir_rebuild_overlap(pointer, bytes, scratch, sizeof(*scratch));
    return result;
}

// Verifiers allocate, so preflight first. Scratch-prefix plans remain valid; writable tails must be disjoint.
static bool ir_rebuild_owners(const IrFunction* source, const IrRebuildPlan* plan, Arena* output, Arena* scratch,
                              void* result, uint64_t result_bytes)
{
    bool valid = source != NULL && result != NULL && output != scratch && ir_rebuild_arena(output) && ir_rebuild_arena(scratch);
    if (valid)
    {
        const void* pointers[] = { source, source->op, source->type, source->flags, source->alignment_log2,
            source->a, source->b, source->c, source->block_offsets, source->extra, source->constants, source->slots,
            source->addresses, source->calls, source->call_types, source->parameter_types, source->vector_constants };
        const uint64_t bytes[] = { sizeof(*source), source->instruction_count, source->instruction_count,
            source->instruction_count, source->instruction_count, (uint64_t)source->instruction_count * 4,
            (uint64_t)source->instruction_count * 4, (uint64_t)source->instruction_count * 4,
            ((uint64_t)source->block_count + 1) * 4, (uint64_t)source->extra_count * 4,
            (uint64_t)source->constant_count * sizeof(IrConstant), (uint64_t)source->slot_count * sizeof(IrSlot),
            (uint64_t)source->address_count * sizeof(IrAddress), (uint64_t)source->call_count * sizeof(IrCall),
            source->call_type_count, source->parameter_count, (uint64_t)source->vector_constant_count * sizeof(IrSimdValue) };
        const void* out_tail = (void*)((uintptr_t)output->base + output->length);
        const void* temp_tail = (void*)((uintptr_t)scratch->base + scratch->length);
        valid = !ir_rebuild_overlap(out_tail, output->capacity - output->length, scratch->base, scratch->capacity) &&
                !ir_rebuild_overlap(temp_tail, scratch->capacity - scratch->length, output->base, output->capacity) &&
                !ir_rebuild_overlap(output, sizeof(*output), scratch, sizeof(*scratch)) &&
                !ir_rebuild_overlap(output, sizeof(*output), out_tail, output->capacity - output->length) &&
                !ir_rebuild_overlap(output, sizeof(*output), temp_tail, scratch->capacity - scratch->length) &&
                !ir_rebuild_overlap(scratch, sizeof(*scratch), out_tail, output->capacity - output->length) &&
                !ir_rebuild_overlap(scratch, sizeof(*scratch), temp_tail, scratch->capacity - scratch->length) &&
                ir_rebuild_borrow(result, result_bytes, output, scratch);
        for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(pointers); i += 1)
        {
            valid = ir_rebuild_borrow(pointers[i], bytes[i], output, scratch) &&
                    !ir_rebuild_overlap(pointers[i], bytes[i], result, result_bytes);
        }
        if (valid && plan != NULL)
        {
            valid = (plan->blocks == NULL) == (plan->rows == NULL) &&
                    (plan->rows != NULL ? plan->offsets != NULL : plan->offsets == NULL);
            const void* plan_pointers[] = { plan, plan->blocks, plan->rows, plan->offsets, plan->aliases };
            const uint64_t plan_bytes[] = { sizeof(*plan), plan->blocks != NULL ? (uint64_t)plan->block_count * 4 : 0,
                plan->rows != NULL ? (uint64_t)plan->instruction_count * 4 : 0,
                plan->offsets != NULL ? ((uint64_t)plan->block_count + 1) * 4 : 0,
                plan->aliases != NULL ? (uint64_t)source->instruction_count * 4 : 0 };
            for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(plan_pointers); i += 1)
            {
                valid = ir_rebuild_borrow(plan_pointers[i], plan_bytes[i], output, scratch) &&
                        !ir_rebuild_overlap(plan_pointers[i], plan_bytes[i], result, result_bytes);
            }
        }
    }
    return valid;
}

static __attribute__((unused)) IrCheck ir_rebuild(const IrFunction* source, const IrRebuildPlan* plan, Arena* output, Arena* scratch, IrFunction* result)
{
    IrCheck check = { .error = IR_ERROR_STRUCTURE, .instruction = BUSTER_IR_NONE };
    const bool owners = ir_rebuild_owners(source, plan, output, scratch, result, sizeof(*result));
    const uint64_t output_mark = owners ? output->length : 0;
    const uint64_t scratch_mark = owners ? scratch->length : 0;
    if (owners)
    {
        check = ir_verify_complete(source, scratch, true);
        const IrCheck scalar = check.error == IR_OK ? ir_verify_complete(source, scratch, false) : check;
        check = scalar.error == IR_OK ? check : scalar;
    }
    const uint32_t blocks = owners && plan != NULL && plan->blocks != NULL ? plan->block_count : owners ? source->block_count : 0;
    const uint32_t rows = owners && plan != NULL && plan->rows != NULL ? plan->instruction_count : owners ? source->instruction_count : 0;
    uint32_t* block_map = NULL;
    uint32_t* value_map = NULL;
    uint32_t* slot_map = NULL;
    IrSizes sizes = { .instruction_count = rows, .block_count = blocks };
    if (check.error == IR_OK)
    {
        const bool paired = plan == NULL || ((plan->blocks == NULL) == (plan->rows == NULL) &&
                            (plan->rows == NULL || plan->offsets != NULL));
        if (ir_function_has_simd(source))
        {
            check.error = IR_ERROR_TYPE;
        }
        else if (!paired || blocks > source->block_count || rows > source->instruction_count || source->result_type > IR_PTR)
        {
            check.error = IR_ERROR_STRUCTURE;
        }
        else if (!ir_rebuild_plain(source))
        {
            check.error = IR_ERROR_METADATA;
        }
        for (uint32_t i = 0; check.error == IR_OK && i < source->parameter_count; i += 1)
        {
            check.error = source->parameter_types[i] <= IR_PTR ? IR_OK : IR_ERROR_TYPE;
        }
        for (uint32_t i = 0; check.error == IR_OK && i < source->instruction_count; i += 1)
        {
            check.error = ir_rebuild_values(source->op[i]) != BUSTER_IR_NONE && source->type[i] <= IR_PTR ? IR_OK : IR_ERROR_OPCODE;
        }
        for (uint32_t i = 0; check.error == IR_OK && i < source->slot_count; i += 1)
        {
            check.error = source->slots[i].type <= IR_PTR ? IR_OK : IR_ERROR_TYPE;
        }
        for (uint32_t i = 0; check.error == IR_OK && i < source->call_type_count; i += 1)
        {
            check.error = source->call_types[i] <= IR_PTR ? IR_OK : IR_ERROR_TYPE;
        }
        for (uint32_t i = 0; check.error == IR_OK && i < source->call_count; i += 1)
        {
            check.error = source->calls[i].result_type <= IR_PTR ? IR_OK : IR_ERROR_TYPE;
        }
    }
    if (check.error == IR_OK)
    {
        block_map = source->block_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, source->block_count) : NULL;
        value_map = source->instruction_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, source->instruction_count) : NULL;
        slot_map = source->slot_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, source->slot_count) : NULL;
        if ((source->block_count != 0 && block_map == NULL) || (source->instruction_count != 0 && value_map == NULL) ||
            (source->slot_count != 0 && slot_map == NULL))
        {
            check.error = IR_ERROR_MEMORY;
        }
        else
        {
            if (source->block_count != 0)
            {
                memset(block_map, 0xff, (uint64_t)source->block_count * sizeof(*block_map));
            }
            if (source->instruction_count != 0)
            {
                memset(value_map, 0xff, (uint64_t)source->instruction_count * sizeof(*value_map));
            }
            if (source->slot_count != 0)
            {
                memset(slot_map, 0xff, (uint64_t)source->slot_count * sizeof(*slot_map));
            }
        }
    }
    for (uint32_t b = 0; check.error == IR_OK && b < blocks; b += 1)
    {
        const uint32_t old = plan != NULL && plan->blocks != NULL ? plan->blocks[b] : b;
        const uint32_t begin = plan != NULL && plan->offsets != NULL ? plan->offsets[b] : source->block_offsets[b];
        const uint32_t end = plan != NULL && plan->offsets != NULL ? plan->offsets[b + 1] : source->block_offsets[b + 1];
        if (old >= source->block_count || block_map[old] != BUSTER_IR_NONE || (b == 0 && old != 0) ||
            begin >= end || end > rows || (b == 0 && begin != 0) || (b + 1 == blocks && end != rows))
        {
            check.error = IR_ERROR_STRUCTURE;
        }
        else
        {
            block_map[old] = b;
            for (uint32_t i = begin; check.error == IR_OK && i < end; i += 1)
            {
                const uint32_t old_row = plan != NULL && plan->rows != NULL ? plan->rows[i] : i;
                if (old_row < source->block_offsets[old] || old_row >= source->block_offsets[old + 1] || value_map[old_row] != BUSTER_IR_NONE)
                {
                    check.error = IR_ERROR_STRUCTURE;
                }
                else
                {
                    value_map[old_row] = i;
                }
            }
        }
    }
    if (check.error == IR_OK && ((blocks == 0) != (rows == 0)))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    for (uint32_t i = 0; check.error == IR_OK && plan != NULL && plan->aliases != NULL && i < source->instruction_count; i += 1)
    {
        if (plan->aliases[i] != BUSTER_IR_NONE && plan->aliases[i] != i &&
            ir_rebuild_value(source, plan, value_map, i) == BUSTER_IR_NONE)
        {
            check.error = IR_ERROR_OPERAND;
        }
    }
    for (uint32_t i = 0; check.error == IR_OK && i < rows; i += 1)
    {
        const uint32_t old = plan != NULL && plan->rows != NULL ? plan->rows[i] : i;
        const uint32_t op = source->op[old];
        const uint32_t a = source->a[old];
        const uint32_t b = source->b[old];
        const uint32_t c = source->c[old];
        if (ir_rebuild_values(op) == BUSTER_IR_NONE || source->type[old] > IR_PTR)
        {
            check.error = IR_ERROR_OPCODE;
        }
        else if (op == IR_CONST)
        {
            sizes.constant_count += 1;
        }
        else if ((op == IR_STACK_ADDR || op == IR_LIFETIME) && slot_map[a] == BUSTER_IR_NONE)
        {
            if (source->slots[a].type > IR_PTR)
            {
                check.error = IR_ERROR_TYPE;
            }
            slot_map[a] = (uint32_t)sizes.slot_count++;
        }
        else if (op == IR_ADDRESS)
        {
            sizes.address_count += 1;
        }
        else if (op == IR_CALL || op == IR_CALL_INDIRECT)
        {
            const IrCall* call = source->calls + b;
            sizes.call_count += 1;
            sizes.extra_count += call->argument_count;
            sizes.call_type_count += call->parameter_count;
            for (uint32_t p = 0; check.error == IR_OK && p < call->parameter_count; p += 1)
            {
                check.error = source->call_types[call->type_first + p] <= IR_PTR ? IR_OK : IR_ERROR_TYPE;
            }
        }
        else if (op == IR_PHI)
        {
            for (uint32_t p = 0; p < b; p += 1)
            {
                sizes.extra_count += block_map[source->extra[a + p * 2]] != BUSTER_IR_NONE ? 2 : 0;
            }
        }
        else if (op == IR_SWITCH)
        {
            sizes.extra_count += (uint64_t)c * 2 + 1;
            sizes.constant_count += c;
        }
    }
    IrFunction candidate = { 0 };
    sizes.parameter_count = owners ? source->parameter_count : 0;
    if (check.error == IR_OK)
    {
        check.error = ir_create(output, &sizes, &candidate);
    }
    if (check.error == IR_OK)
    {
        candidate.result_type = source->result_type;
        candidate.symbol_count = source->symbol_count;
        if (source->parameter_count != 0)
        {
            memcpy(candidate.parameter_types, source->parameter_types, source->parameter_count);
        }
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            candidate.block_offsets[b] = plan != NULL && plan->offsets != NULL ? plan->offsets[b] : source->block_offsets[b];
        }
        candidate.block_offsets[blocks] = rows;
        for (uint32_t s = 0; s < source->slot_count; s += 1)
        {
            if (slot_map[s] != BUSTER_IR_NONE)
            {
                candidate.slots[slot_map[s]] = source->slots[s];
            }
        }
        uint32_t constant = 0;
        uint32_t address = 0;
        uint32_t call = 0;
        uint32_t extra = 0;
        uint32_t call_type = 0;
        for (uint32_t i = 0; check.error == IR_OK && i < rows; i += 1)
        {
            const uint32_t old = plan != NULL && plan->rows != NULL ? plan->rows[i] : i;
            const uint32_t op = source->op[old];
            const uint32_t a = source->a[old];
            const uint32_t b = source->b[old];
            const uint32_t c = source->c[old];
            candidate.op[i] = (uint8_t)op;
            candidate.type[i] = source->type[old];
            candidate.flags[i] = source->flags[old];
            candidate.alignment_log2[i] = source->alignment_log2[old];
            candidate.a[i] = a;
            candidate.b[i] = b;
            candidate.c[i] = c;
            uint32_t mask = ir_rebuild_values(op);
            mask |= op == IR_ADDRESS && b != BUSTER_IR_NONE ? 2u : 0u;
            mask |= op == IR_RET && a != BUSTER_IR_NONE ? 1u : 0u;
            const uint32_t old_values[] = { a, b, c };
            uint32_t* new_values[] = { candidate.a + i, candidate.b + i, candidate.c + i };
            for (uint32_t p = 0; check.error == IR_OK && p < 3; p += 1)
            {
                if ((mask & (1u << p)) != 0)
                {
                    const uint32_t value = ir_rebuild_value(source, plan, value_map, old_values[p]);
                    *new_values[p] = value;
                    check.error = value != BUSTER_IR_NONE ? IR_OK : IR_ERROR_OPERAND;
                }
            }
            if (op == IR_CONST)
            {
                candidate.a[i] = constant;
                candidate.constants[constant++] = source->constants[a];
            }
            else if (op == IR_STACK_ADDR || op == IR_LIFETIME)
            {
                candidate.a[i] = slot_map[a];
            }
            else if (op == IR_ADDRESS)
            {
                candidate.c[i] = address;
                candidate.addresses[address++] = source->addresses[c];
            }
            else if (op == IR_CALL || op == IR_CALL_INDIRECT)
            {
                const IrCall old_call = source->calls[b];
                candidate.b[i] = call;
                candidate.calls[call++] = (IrCall){ .argument_first = extra, .argument_count = old_call.argument_count,
                    .type_first = call_type, .parameter_count = old_call.parameter_count, .result_type = old_call.result_type,
                    .variadic = old_call.variadic };
                for (uint32_t p = 0; check.error == IR_OK && p < old_call.argument_count; p += 1)
                {
                    const uint32_t value = ir_rebuild_value(source, plan, value_map, source->extra[old_call.argument_first + p]);
                    candidate.extra[extra++] = value;
                    check.error = value != BUSTER_IR_NONE ? IR_OK : IR_ERROR_OPERAND;
                }
                for (uint32_t p = 0; p < old_call.parameter_count; p += 1)
                {
                    candidate.call_types[call_type++] = source->call_types[old_call.type_first + p];
                }
            }
            else if (op == IR_PHI)
            {
                const uint32_t first = extra;
                for (uint32_t p = 0; check.error == IR_OK && p < b; p += 1)
                {
                    const uint32_t predecessor = block_map[source->extra[a + p * 2]];
                    if (predecessor != BUSTER_IR_NONE)
                    {
                        const uint32_t value = ir_rebuild_value(source, plan, value_map, source->extra[a + p * 2 + 1]);
                        candidate.extra[extra++] = predecessor;
                        candidate.extra[extra++] = value;
                        check.error = value != BUSTER_IR_NONE ? IR_OK : IR_ERROR_OPERAND;
                    }
                }
                candidate.a[i] = first;
                candidate.b[i] = (extra - first) / 2;
            }
            else if (op == IR_BR || op == IR_CBR)
            {
                uint32_t* first = op == IR_BR ? candidate.a + i : candidate.b + i;
                *first = block_map[op == IR_BR ? a : b];
                if (op == IR_CBR)
                {
                    candidate.c[i] = block_map[c];
                }
                if (*first == BUSTER_IR_NONE || (op == IR_CBR && candidate.c[i] == BUSTER_IR_NONE))
                {
                    check.error = IR_ERROR_OPERAND;
                }
            }
            else if (op == IR_SWITCH)
            {
                candidate.b[i] = extra;
                candidate.extra[extra++] = block_map[source->extra[b]];
                check.error = candidate.extra[extra - 1] != BUSTER_IR_NONE ? check.error : IR_ERROR_OPERAND;
                for (uint32_t p = 0; check.error == IR_OK && p < c; p += 1)
                {
                    candidate.constants[constant] = source->constants[source->extra[b + 1 + p * 2]];
                    candidate.extra[extra++] = constant++;
                    candidate.extra[extra++] = block_map[source->extra[b + 2 + p * 2]];
                    check.error = candidate.extra[extra - 1] != BUSTER_IR_NONE ? IR_OK : IR_ERROR_OPERAND;
                }
            }
        }
    }
    if (check.error == IR_OK)
    {
        check = ir_verify_complete(&candidate, scratch, true);
        const IrCheck scalar = check.error == IR_OK ? ir_verify_complete(&candidate, scratch, false) : check;
        check = scalar.error == IR_OK ? check : scalar;
    }
    if (owners)
    {
        if (check.error == IR_OK)
        {
            *result = candidate;
        }
        else
        {
            arena_pop_to(output, output_mark);
        }
        arena_pop_to(scratch, scratch_mark);
    }
    return check;
}

#endif
