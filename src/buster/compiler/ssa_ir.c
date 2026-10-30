// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Promote the shared IR's typed, private stack slots through ssa.c. Include after ir.c and ssa.c. All instruction
// operands remain visible to the IR verifier: the local-promotion view hides them only while constructing phis.
// The completed entry block must have no incoming edges; ir_lower.c supplies a separate parameter preheader.
// Untyped slots, volatile accesses and every observed escape stay in memory. A TEMPORARY flag alone is harmless.
// Calls, existing phis and switches receive independent extra slices, even when their input slices overlapped.
//
// Input and result descriptors are distinct; input columns are immutable. Output and scratch must be distinct,
// nonoverlapping arenas that own no input. On failure the output length and *result are unchanged; scratch is
// restored on every path. Both constructors remove unreachable blocks, compact promoted slots and validate the
// final IR with scalar and vector dominance checks. Existing phis are preserved after removing dead incoming edges;
// the SSA constructor's trivial-phi cleanup applies to its newly created phis.

_Static_assert(IR_VOID == BUSTER_SSA_TYPE_VOID && IR_I1 == BUSTER_SSA_TYPE_I1 && IR_I8 == BUSTER_SSA_TYPE_I8 && IR_I16 == BUSTER_SSA_TYPE_I16 && IR_I32 == BUSTER_SSA_TYPE_I32 && IR_I64 == BUSTER_SSA_TYPE_I64 && IR_I128 == BUSTER_SSA_TYPE_I128 && IR_F32 == BUSTER_SSA_TYPE_F32 && IR_F64 == BUSTER_SSA_TYPE_F64 && IR_F80 == BUSTER_SSA_TYPE_F80 && IR_PTR == BUSTER_SSA_TYPE_PTR, "SSA and IR scalar types must agree");

static __attribute__((unused)) IrCheck ssa_ir_promote(const IrFunction* input, Arena* output, Arena* scratch, IrFunction* result);
static __attribute__((unused)) IrCheck ssa_ir_promote_scalar(const IrFunction* input, Arena* output, Arena* scratch, IrFunction* result);
// The vector path's promotion (or the scalar twin's, without `vector`) with ssa.c's verifier run on the core's view of
// the function and its result before the IR is built (#134's tests): every use dominated by its definition, a phi
// operand per predecessor, no trivial phi, and its reaching-definition equations. `*verified` is its check, whose rule
// names what failed (SSA_OK with no rule if the promotion failed before it), and a failure fails the promotion as
// IR_ERROR_PHI (or as the memory's). The vector path's constructor runs here wherever its memory bound admits, past the
// cells where ssa_ir_promote takes the twin's (#787).
static __attribute__((unused)) IrCheck ssa_ir_promote_verified(const IrFunction* input, bool vector, Arena* output, Arena* scratch, IrFunction* result, SsaCheck* verified);

typedef struct SsaIrWork SsaIrWork;
struct SsaIrWork
{
    const IrFunction* input;
    Arena* scratch;
    IrCfg cfg;
    SsaInput source;
    SsaWork work;
    SsaResult ssa;
    uint8_t* kinds;
    uint32_t* locals;
    uint32_t* operands;
    uint8_t* local_types;
    uint8_t* local_flags;
    uint8_t* keep;
    uint32_t* blocks;
    uint32_t* values;
    uint32_t* slots;
    uint32_t* phi_offsets;
    uint32_t* phi_indices;
    IrSizes sizes;
};

static bool ssa_ir_overlap(const void* first, uint64_t first_bytes, const void* second, uint64_t second_bytes)
{
    const uintptr_t a = (uintptr_t)first;
    const uintptr_t b = (uintptr_t)second;
    return first_bytes != 0 && second_bytes != 0 && (a <= b ? b - a < first_bytes : a - b < second_bytes);
}

static bool ssa_ir_arena_valid(const Arena* arena)
{
    return arena != 0 && arena->length <= arena->capacity && (arena->capacity == 0 || arena->base != 0) && arena->capacity <= UINTPTR_MAX - (uintptr_t)arena->base;
}

static bool ssa_ir_owned(const Arena* arena, const void* data, uint64_t count, uint64_t bytes)
{
    return ssa_ir_overlap(arena->base, arena->capacity, data, count * bytes);
}

static bool ssa_ir_conflict(const IrFunction* f, const Arena* arena)
{
    return ssa_ir_owned(arena, f, 1, sizeof(*f)) ||
        ssa_ir_owned(arena, f->op, f->instruction_count, 1) ||
        ssa_ir_owned(arena, f->type, f->instruction_count, 1) ||
        ssa_ir_owned(arena, f->flags, f->instruction_count, 1) ||
        ssa_ir_owned(arena, f->alignment_log2, f->instruction_count, 1) ||
        ssa_ir_owned(arena, f->a, f->instruction_count, 4) ||
        ssa_ir_owned(arena, f->b, f->instruction_count, 4) ||
        ssa_ir_owned(arena, f->c, f->instruction_count, 4) ||
        ssa_ir_owned(arena, f->block_offsets, (uint64_t)f->block_count + 1, 4) ||
        ssa_ir_owned(arena, f->extra, f->extra_count, 4) ||
        ssa_ir_owned(arena, f->constants, f->constant_count, sizeof(IrConstant)) ||
        ssa_ir_owned(arena, f->vector_constants, f->vector_constant_count, sizeof(IrSimdValue)) ||
        ssa_ir_owned(arena, f->slots, f->slot_count, sizeof(IrSlot)) ||
        ssa_ir_owned(arena, f->addresses, f->address_count, sizeof(IrAddress)) ||
        ssa_ir_owned(arena, f->calls, f->call_count, sizeof(IrCall)) ||
        ssa_ir_owned(arena, f->call_types, f->call_type_count, 1) ||
        ssa_ir_owned(arena, f->parameter_types, f->parameter_count, 1) ||
        ssa_ir_owned(arena, f->labels, f->label_count, 4) ||
        ssa_ir_owned(arena, f->abi_calls, f->abi_call_count, sizeof(IrAbiCall)) ||
        ssa_ir_owned(arena, f->passed, f->passed_count, sizeof(IrPassed));
}

static IrError ssa_ir_error(SsaError error)
{
    IrError result = IR_OK;
    if (error == SSA_ERROR_MEMORY)
    {
        result = IR_ERROR_MEMORY;
    }
    else if (error == SSA_ERROR_LIMIT)
    {
        result = IR_ERROR_LIMIT;
    }
    else if (error != SSA_OK)
    {
        result = IR_ERROR_STRUCTURE;
    }
    return result;
}

// A stack address may have several definitions, and a later use may escape an address first used by a load.
// Classify every use before exposing any READ/WRITE to SSA; an indirect or differently typed access excludes the
// whole slot. No alias inference is needed: an address used through COPY, ADDRESS, a phi or a call stays memory.
static IrError ssa_ir_classify(SsaIrWork* work)
{
    const IrFunction* f = work->input;
    const uint32_t local_count = f->block_count == 0 ? 0 : f->slot_count;
    IrError error = (uint64_t)f->instruction_count + local_count < BUSTER_SSA_NONE ? IR_OK : IR_ERROR_LIMIT;
    if (error == IR_OK)
    {
        work->kinds = ssa_allocate(work->scratch, f->instruction_count, 1);
        work->locals = ssa_allocate(work->scratch, f->instruction_count, 4);
        work->operands = ssa_allocate(work->scratch, f->instruction_count, 4);
        work->local_types = ssa_allocate(work->scratch, local_count, 1);
        work->local_flags = ssa_allocate(work->scratch, local_count, 1);
        if (work->kinds == 0 || work->locals == 0 || work->operands == 0 || work->local_types == 0 || work->local_flags == 0)
        {
            error = IR_ERROR_MEMORY;
        }
    }
    if (error == IR_OK)
    {
        memset(work->kinds, BUSTER_SSA_VALUE, f->instruction_count);
        memset(work->locals, 0xff, (uint64_t)f->instruction_count * 4);
        memset(work->operands, 0xff, (uint64_t)f->instruction_count * 4);
        for (uint32_t slot = 0; slot < local_count; slot += 1)
        {
            const IrSlot local = f->slots[slot];
            work->local_types[slot] = local.type == IR_VOID ? BUSTER_SSA_TYPE_AGGREGATE : local.type;
            work->local_flags[slot] = local.type == IR_VOID || (local.flags & (BUSTER_IR_SLOT_ADDRESS_TAKEN | BUSTER_IR_SLOT_VOLATILE)) != 0 ? BUSTER_SSA_LOCAL_ADDRESS_TAKEN : 0;
        }
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            const uint64_t count = ir_operand_count(f, i);
            for (uint32_t p = 0; p < count; p += 1)
            {
                const IrOperand operand = ir_operand(f, i, p, work->cfg.instruction_blocks[i]);
                if (f->op[operand.value] == IR_STACK_ADDR)
                {
                    const uint32_t slot = f->a[operand.value];
                    const bool load = f->op[i] == IR_LOAD && operand.position == 0 && f->type[i] == f->slots[slot].type;
                    const bool store = f->op[i] == IR_STORE && operand.position == 0 && f->type[f->b[i]] == f->slots[slot].type;
                    if ((!load && !store) || (f->flags[i] & BUSTER_IR_VOLATILE) != 0)
                    {
                        work->local_flags[slot] |= BUSTER_SSA_LOCAL_ADDRESS_TAKEN;
                    }
                }
            }
        }
        work->source = (SsaInput){
            .block_count = f->block_count,
            .instruction_count = f->instruction_count,
            .local_count = local_count,
            .edge_count = work->cfg.edge_count,
            .block_offsets = f->block_offsets,
            .successor_offsets = work->cfg.successor_offsets,
            .successors = work->cfg.successors,
            .kinds = work->kinds,
            .types = f->type,
            .locals = work->locals,
            .operands = work->operands,
            .local_types = work->local_types,
            .local_flags = work->local_flags,
        };
    }
    return error;
}

static IrCheck ssa_ir_check_dominance(SsaIrWork* work)
{
    const IrFunction* f = work->input;
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    for (uint32_t i = 0; check.error == IR_OK && i < f->instruction_count; i += 1)
    {
        const uint32_t block = work->cfg.instruction_blocks[i];
        if (work->work.result.reachable[block] != 0)
        {
            const uint64_t count = ir_operand_count(f, i);
            for (uint32_t p = 0; check.error == IR_OK && p < count; p += 1)
            {
                const IrOperand operand = ir_operand(f, i, p, block);
                // A phi's dead incoming edges disappear with their predecessor blocks. Cross-block uses in dead
                // blocks have no dynamic meaning and need not satisfy reachable-entry dominance.
                if (work->work.result.reachable[operand.block] != 0)
                {
                    const uint32_t origin = work->cfg.instruction_blocks[operand.value];
                    if (!ssa_dominates(&work->work, origin, operand.block))
                    {
                        check = ir_check(IR_ERROR_DOMINANCE, i, operand.position);
                    }
                }
            }
        }
    }
    return check;
}

static void ssa_ir_accesses(SsaIrWork* work)
{
    const IrFunction* f = work->input;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        if ((f->op[i] == IR_LOAD || f->op[i] == IR_STORE) && f->op[f->a[i]] == IR_STACK_ADDR)
        {
            const uint32_t slot = f->a[f->a[i]];
            if (work->work.result.promoted[slot] != 0)
            {
                work->kinds[i] = f->op[i] == IR_LOAD ? BUSTER_SSA_READ : BUSTER_SSA_WRITE;
                work->locals[i] = slot;
                work->operands[i] = f->op[i] == IR_STORE ? f->b[i] : BUSTER_SSA_NONE;
            }
        }
    }
}

static void ssa_ir_need_undef(SsaIrWork* work, uint32_t value)
{
    if (value >= work->source.instruction_count && value < work->ssa.phi_base)
    {
        work->values[value] = 0;
    }
}

static uint32_t ssa_ir_value(const SsaIrWork* work, uint32_t value)
{
    const uint32_t replacement = value < work->source.instruction_count ? work->ssa.replacements[value] : value;
    return work->values[replacement];
}

static IrError ssa_ir_layout(SsaIrWork* work)
{
    const IrFunction* f = work->input;
    const SsaResult* ssa = &work->ssa;
    const uint64_t value_count = (uint64_t)ssa->phi_base + ssa->phi_count;
    work->keep = ssa_allocate(work->scratch, f->instruction_count, 1);
    work->blocks = ssa_allocate(work->scratch, f->block_count, 4);
    work->values = ssa_allocate(work->scratch, value_count, 4);
    work->slots = ssa_allocate(work->scratch, f->slot_count, 4);
    work->phi_offsets = ssa_allocate(work->scratch, (uint64_t)f->block_count + 1, 4);
    work->phi_indices = ssa_allocate(work->scratch, ssa->phi_count, 4);
    uint32_t* cursor = ssa_allocate(work->scratch, f->block_count, 4);
    IrError error = work->keep != 0 && work->blocks != 0 && work->values != 0 && work->slots != 0 && work->phi_offsets != 0 && work->phi_indices != 0 && cursor != 0 ? IR_OK : IR_ERROR_MEMORY;
    if (error == IR_OK)
    {
        memset(work->keep, 0, f->instruction_count);
        memset(work->blocks, 0xff, (uint64_t)f->block_count * 4);
        memset(work->values, 0xff, value_count * 4);
        memset(work->slots, 0xff, (uint64_t)f->slot_count * 4);
        memset(work->phi_offsets, 0, ((uint64_t)f->block_count + 1) * 4);
        work->sizes = (IrSizes){
            .constant_count = f->constant_count,
            .address_count = f->address_count,
            .call_type_count = f->call_type_count,
            .parameter_count = f->parameter_count,
            .label_count = f->label_count,
            .abi_call_count = f->abi_call_count,
            .passed_count = f->passed_count,
        };
        for (uint32_t slot = 0; slot < f->slot_count; slot += 1)
        {
            if (slot >= work->source.local_count || ssa->promoted[slot] == 0)
            {
                work->slots[slot] = (uint32_t)work->sizes.slot_count++;
            }
        }
        for (uint32_t phi = 0; phi < ssa->phi_count; phi += 1)
        {
            work->phi_offsets[ssa->phi_blocks[phi] + 1] += 1;
        }
        for (uint32_t block = 0; block < f->block_count; block += 1)
        {
            work->phi_offsets[block + 1] += work->phi_offsets[block];
            cursor[block] = work->phi_offsets[block];
        }
        for (uint32_t phi = 0; phi < ssa->phi_count; phi += 1)
        {
            work->phi_indices[cursor[ssa->phi_blocks[phi]]++] = phi;
            work->sizes.extra_count += (uint64_t)(ssa->phi_offsets[phi + 1] - ssa->phi_offsets[phi]) * 2;
        }
        for (uint32_t operand = 0; operand < ssa->operand_count; operand += 1)
        {
            ssa_ir_need_undef(work, ssa->phi_operands[operand]);
        }
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            const uint32_t block = work->cfg.instruction_blocks[i];
            const bool access = work->kinds[i] == BUSTER_SSA_READ || work->kinds[i] == BUSTER_SSA_WRITE;
            const bool address = f->op[i] == IR_STACK_ADDR && ssa->promoted[f->a[i]] != 0;
            if (ssa->reachable[block] != 0 && !access && !address)
            {
                work->keep[i] = 1;
                const uint64_t count = ir_operand_count(f, i);
                for (uint32_t p = 0; p < count; p += 1)
                {
                    const IrOperand operand = ir_operand(f, i, p, block);
                    if (ssa->reachable[operand.block] != 0)
                    {
                        ssa_ir_need_undef(work, ssa->replacements[operand.value]);
                        if (f->op[i] == IR_PHI)
                        {
                            work->sizes.extra_count += 2;
                        }
                    }
                }
                if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
                {
                    work->sizes.call_count += 1;
                    work->sizes.extra_count += f->calls[f->b[i]].argument_count;
                }
                else if (f->op[i] == IR_SWITCH)
                {
                    work->sizes.extra_count += (uint64_t)f->c[i] * 2 + 1;
                }
                else if (f->op[i] == IR_BR_INDIRECT && f->b[i] != BUSTER_IR_NONE)
                {
                    work->sizes.extra_count += f->c[i];
                }
            }
        }
        for (uint32_t block = 0; block < f->block_count; block += 1)
        {
            if (ssa->reachable[block] != 0)
            {
                work->blocks[block] = (uint32_t)work->sizes.block_count++;
                for (uint32_t p = work->phi_offsets[block]; p < work->phi_offsets[block + 1]; p += 1)
                {
                    const uint32_t phi = work->phi_indices[p];
                    work->values[ssa->phi_base + phi] = (uint32_t)work->sizes.instruction_count++;
                }
                if (block == 0)
                {
                    for (uint32_t slot = 0; slot < work->source.local_count; slot += 1)
                    {
                        const uint32_t undefined = f->instruction_count + slot;
                        if (work->values[undefined] != BUSTER_IR_NONE)
                        {
                            work->values[undefined] = (uint32_t)work->sizes.instruction_count++;
                        }
                    }
                }
                for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[block + 1]; i += 1)
                {
                    if (work->keep[i] != 0)
                    {
                        work->values[i] = (uint32_t)work->sizes.instruction_count++;
                    }
                }
            }
        }
        if (work->sizes.instruction_count > UINT32_MAX || work->sizes.extra_count > UINT32_MAX)
        {
            error = IR_ERROR_LIMIT;
        }
    }
    return error;
}

static void ssa_ir_copy_metadata(const SsaIrWork* work, IrFunction* f)
{
    const IrFunction* input = work->input;
    f->result_type = input->result_type;
    f->symbol_count = input->symbol_count;
    if (input->constant_count != 0)
    {
        memcpy(f->constants, input->constants, (uint64_t)input->constant_count * sizeof(IrConstant));
    }
    if (input->address_count != 0)
    {
        memcpy(f->addresses, input->addresses, (uint64_t)input->address_count * sizeof(IrAddress));
    }
    if (input->call_type_count != 0)
    {
        memcpy(f->call_types, input->call_types, input->call_type_count);
    }
    if (input->parameter_count != 0)
    {
        memcpy(f->parameter_types, input->parameter_types, input->parameter_count);
    }
    // The ABI records and the incoming frame as they were: each call keeps its index into them. A label whose block
    // is removed (no reachable br_indirect can go there) keeps no block.
    if (input->abi_call_count != 0)
    {
        memcpy(f->abi_calls, input->abi_calls, (uint64_t)input->abi_call_count * sizeof(IrAbiCall));
    }
    if (input->passed_count != 0)
    {
        memcpy(f->passed, input->passed, (uint64_t)input->passed_count * sizeof(IrPassed));
    }
    for (uint32_t label = 0; label < input->label_count; label += 1)
    {
        f->labels[label] = input->labels[label] == BUSTER_IR_NONE ? BUSTER_IR_NONE : work->blocks[input->labels[label]];
    }
    f->parameter_passed = input->parameter_passed;
    f->result_size = input->result_size;
    f->result_abi = input->result_abi;
    f->variadic = input->variadic;
    f->incoming = input->incoming;
    for (uint32_t slot = 0; slot < input->slot_count; slot += 1)
    {
        if (work->slots[slot] != BUSTER_IR_NONE)
        {
            f->slots[work->slots[slot]] = input->slots[slot];
        }
    }
}

static void ssa_ir_fill(const SsaIrWork* work, IrFunction* f)
{
    const IrFunction* input = work->input;
    const SsaResult* ssa = &work->ssa;
    uint32_t extra = 0;
    uint32_t calls = 0;
    uint32_t instruction = 0;
    ssa_ir_copy_metadata(work, f);
    for (uint32_t block = 0; block < input->block_count; block += 1)
    {
        if (ssa->reachable[block] != 0)
        {
            f->block_offsets[work->blocks[block]] = instruction;
            for (uint32_t p = work->phi_offsets[block]; p < work->phi_offsets[block + 1]; p += 1)
            {
                const uint32_t phi = work->phi_indices[p];
                const uint32_t begin = ssa->phi_offsets[phi];
                const uint32_t count = ssa->phi_offsets[phi + 1] - begin;
                f->op[instruction] = IR_PHI;
                f->type[instruction] = work->local_types[ssa->phi_locals[phi]];
                f->a[instruction] = extra;
                f->b[instruction] = count;
                for (uint32_t incoming = 0; incoming < count; incoming += 1)
                {
                    const uint32_t predecessor = ssa->predecessors[ssa->predecessor_offsets[block] + incoming];
                    f->extra[extra++] = work->blocks[predecessor];
                    f->extra[extra++] = ssa_ir_value(work, ssa->phi_operands[begin + incoming]);
                }
                instruction += 1;
            }
            if (block == 0)
            {
                for (uint32_t slot = 0; slot < work->source.local_count; slot += 1)
                {
                    if (work->values[input->instruction_count + slot] != BUSTER_IR_NONE)
                    {
                        f->op[instruction] = IR_UNDEF;
                        f->type[instruction] = work->local_types[slot];
                        instruction += 1;
                    }
                }
            }
            for (uint32_t i = input->block_offsets[block]; i < input->block_offsets[block + 1]; i += 1)
            {
                if (work->keep[i] != 0)
                {
                    const uint32_t op = input->op[i];
                    f->op[instruction] = (uint8_t)op;
                    f->type[instruction] = input->type[i];
                    f->flags[instruction] = input->flags[i];
                    f->alignment_log2[instruction] = input->alignment_log2[i];
                    f->a[instruction] = input->a[i];
                    f->b[instruction] = input->b[i];
                    f->c[instruction] = input->c[i];
                    const uint32_t mask = ir_value_mask(input, i);
                    if ((mask & 1) != 0)
                    {
                        f->a[instruction] = ssa_ir_value(work, input->a[i]);
                    }
                    if ((mask & 2) != 0)
                    {
                        f->b[instruction] = ssa_ir_value(work, input->b[i]);
                    }
                    if ((mask & 4) != 0)
                    {
                        f->c[instruction] = ssa_ir_value(work, input->c[i]);
                    }
                    if (op == IR_STACK_ADDR)
                    {
                        f->a[instruction] = work->slots[input->a[i]];
                    }
                    else if (op == IR_CALL || op == IR_CALL_INDIRECT)
                    {
                        const IrCall call = input->calls[input->b[i]];
                        f->b[instruction] = calls;
                        f->calls[calls] = call;
                        f->calls[calls].argument_first = extra;
                        calls += 1;
                        for (uint32_t p = 0; p < call.argument_count; p += 1)
                        {
                            f->extra[extra++] = ssa_ir_value(work, input->extra[call.argument_first + p]);
                        }
                    }
                    else if (op == IR_PHI)
                    {
                        f->a[instruction] = extra;
                        f->b[instruction] = 0;
                        for (uint32_t p = 0; p < input->b[i]; p += 1)
                        {
                            const uint32_t predecessor = input->extra[input->a[i] + p * 2];
                            if (ssa->reachable[predecessor] != 0)
                            {
                                f->extra[extra++] = work->blocks[predecessor];
                                f->extra[extra++] = ssa_ir_value(work, input->extra[input->a[i] + p * 2 + 1]);
                                f->b[instruction] += 1;
                            }
                        }
                    }
                    else if (op == IR_BR)
                    {
                        f->a[instruction] = work->blocks[input->a[i]];
                    }
                    else if (op == IR_CBR)
                    {
                        f->b[instruction] = work->blocks[input->b[i]];
                        f->c[instruction] = work->blocks[input->c[i]];
                    }
                    else if (op == IR_SWITCH)
                    {
                        f->b[instruction] = extra;
                        f->extra[extra++] = work->blocks[input->extra[input->b[i]]];
                        for (uint32_t p = 0; p < input->c[i]; p += 1)
                        {
                            f->extra[extra++] = input->extra[input->b[i] + p * 2 + 1];
                            f->extra[extra++] = work->blocks[input->extra[input->b[i] + p * 2 + 2]];
                        }
                    }
                    else if (op == IR_BR_INDIRECT && input->b[i] != BUSTER_IR_NONE)
                    {
                        // Its labels' list, label indices, which promotion keeps.
                        f->b[instruction] = extra;
                        for (uint32_t p = 0; p < input->c[i]; p += 1)
                        {
                            f->extra[extra++] = input->extra[input->b[i] + p];
                        }
                    }
                    instruction += 1;
                }
            }
        }
    }
    f->block_offsets[f->block_count] = instruction;
}

static IrCheck ssa_ir_verify_output(const SsaIrWork* work, const IrFunction* f)
{
    uint32_t* enter = ssa_allocate(work->scratch, f->block_count, 4);
    uint32_t* leave = ssa_allocate(work->scratch, f->block_count, 4);
    IrCheck check = ir_check(enter != 0 && leave != 0 ? IR_OK : IR_ERROR_MEMORY, BUSTER_IR_NONE, 0);
    if (check.error == IR_OK)
    {
        for (uint32_t block = 0; block < work->input->block_count; block += 1)
        {
            if (work->blocks[block] != BUSTER_IR_NONE)
            {
                enter[work->blocks[block]] = work->work.dominators.preorder[block];
                leave[work->blocks[block]] = work->work.dominators.subtree_end[block];
            }
        }
        const IrDominance dominance = {
            .enter = enter,
            .leave = leave,
            .block_count = f->block_count,
        };
        check = ir_verify(f, work->scratch, &dominance);
        if (check.error == IR_OK)
        {
            check = ir_verify_scalar(f, work->scratch, &dominance);
        }
    }
    return check;
}

// The most (block, slot) cells a function's promotion takes the vector path for (#787). Its phi placement, entry
// definitions and renaming each pass over every cell (ssa.c's ssa_place_phis, ssa_entry_definitions and
// ssa_rename_vector), where the scalar twin's work follows the instructions, so past this many the twin is faster. On
// the 9700X, each of the frozen sqlite3.c's 2,602 functions timed by both paths (benchmark-ir-source --functions): the
// vector path alone 40.15 ms, the twin alone 37.01, the vector path to 16,384 cells and the twin past them 35.96 (each
// function's faster path 35.32); every bound from 8,192 to 32,768 cells came within -0.28% to +0.26% of it.
#define BUSTER_SSA_IR_VECTOR_CELLS (UINT64_C(1) << 14)

static IrCheck ssa_ir_promote_path(const IrFunction* input, Arena* output, Arena* scratch, IrFunction* result, bool vector, SsaCheck* verified)
{
    (void)vector;

    const uint64_t output_length = output == 0 ? 0 : output->length;
    const uint64_t scratch_length = scratch == 0 ? 0 : scratch->length;
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    if (input == 0 || result == 0 || input == result || output == scratch || !ssa_ir_arena_valid(output) || !ssa_ir_arena_valid(scratch))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    if (check.error == IR_OK && (ssa_ir_overlap(output->base, output->capacity, scratch->base, scratch->capacity) || ssa_ir_conflict(input, output) || ssa_ir_conflict(input, scratch)))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    SsaIrWork work = {
        .input = input,
        .scratch = scratch,
    };
    IrFunction candidate = { 0 };
    if (check.error == IR_OK)
    {
        check = ir_verify_path(input, scratch, 0, BUSTER_VECTOR_PATH(vector));
    }
    if (check.error == IR_OK && ir_function_has_simd(input))
    {
        check.error = IR_ERROR_UNSUPPORTED;
    }
    if (check.error == IR_OK)
    {
        check.error = ir_cfg_validated(input, scratch, scratch, &work.cfg);
    }
    if (check.error == IR_OK && input->block_count != 0 && work.cfg.predecessor_offsets[1] != 0)
    {
        check = ir_check(IR_ERROR_STRUCTURE, input->block_offsets[0], 0);
    }
    if (check.error == IR_OK)
    {
        check.error = ssa_ir_classify(&work);
    }
    if (check.error == IR_OK)
    {
        // Match the builder's combined reserved-frontier/temporary-matrix cap, a memory bound; and past
        // BUSTER_SSA_IR_VECTOR_CELLS, the measured crossover, the vector path costs more than it saves (#787). Both
        // larger cases use the independent scalar constructor. A verified promotion, a test's, takes the vector
        // constructor past the crossover too, so that it is checked on every function its memory bound admits.
        const uint64_t cells = (uint64_t)input->block_count * work.source.local_count;
        vector = BUSTER_VECTOR_PATH(vector) && ssa_vector_fits(input->block_count) && (verified != 0 || cells <= BUSTER_SSA_IR_VECTOR_CELLS);
        // Eligibility is complete, but kinds are still VALUE: check every real use with this dominance tree before
        // exposing READ/WRITE. Their types and operand ordering were already checked by the full IR verifier.
        check.error = ssa_ir_error(ssa_prepare(&work.work, &work.source, scratch, BUSTER_VECTOR_PATH(vector)));
    }
    if (check.error == IR_OK)
    {
        check = ssa_ir_check_dominance(&work);
    }
    if (check.error == IR_OK)
    {
        ssa_ir_accesses(&work);
        check.error = ssa_ir_error(ssa_definitions(&work.work));
    }
    if (check.error == IR_OK)
    {
        check.error = ssa_ir_error(BUSTER_VECTOR_PATH(vector) ? ssa_construct_vector(&work.work) : ssa_construct_scalar(&work.work));
    }
    if (check.error == IR_OK)
    {
        check.error = ssa_ir_error(ssa_simplify(&work.work));
    }
    if (check.error == IR_OK)
    {
        // The private emitter only appends; its result may safely share scratch with completed construction work.
        // Nothing temporary is placed in the caller's output arena.
        check.error = ssa_ir_error(ssa_emit(&work.work, scratch, &work.ssa));
    }
    if (check.error == IR_OK && verified != 0)
    {
        // The core's result is the IR's phis and replacements one for one (ssa_ir_fill), so its checks are the output's.
        *verified = ssa_verify(&work.source, &work.ssa, scratch);
        if (verified->error != SSA_OK)
        {
            check = ir_check(verified->error == SSA_ERROR_MEMORY ? IR_ERROR_MEMORY : IR_ERROR_PHI, verified->instruction, 0);
        }
    }
    if (check.error == IR_OK)
    {
        check.error = ssa_ir_layout(&work);
    }
    if (check.error == IR_OK)
    {
        check.error = ir_create(output, &work.sizes, &candidate);
    }
    if (check.error == IR_OK)
    {
        ssa_ir_fill(&work, &candidate);
        check = ssa_ir_verify_output(&work, &candidate);
    }
    if (check.error == IR_OK)
    {
        *result = candidate;
    }
    else if (output != 0)
    {
        arena_pop_to(output, output_length);
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, scratch_length);
    }
    return check;
}

// The phis of `f`, and in `*trivial` how many of them are trivial: of one value but themselves, however many times
// (Braun et al.'s), which a promotion leaves none of among those it makes (#134's tests and benchmark-ir-source's phi
// counts; ir_lower.c makes no phi, so every phi of a function promoted from its IR is the promotion's).
static __attribute__((unused)) uint32_t ssa_ir_phis(const IrFunction* f, uint32_t* trivial)
{
    uint32_t phis = 0;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        if (f->op[i] == IR_PHI)
        {
            uint32_t same = BUSTER_IR_NONE;
            bool distinct = false;
            for (uint32_t p = 0; p < f->b[i]; p += 1)
            {
                const uint32_t value = f->extra[f->a[i] + p * 2 + 1];
                if (value != i)
                {
                    distinct |= same != BUSTER_IR_NONE && same != value;
                    same = value;
                }
            }
            phis += 1;
            *trivial += distinct ? 0 : 1;
        }
    }
    return phis;
}

static IrCheck ssa_ir_promote_scalar(const IrFunction* input, Arena* output, Arena* scratch, IrFunction* result);

static IrCheck ssa_ir_promote(const IrFunction* input, Arena* output, Arena* scratch, IrFunction* result)
{
#if !BUSTER_HOST_VECTOR
    return ssa_ir_promote_scalar(input, output, scratch, result);
#else
    return ssa_ir_promote_path(input, output, scratch, result, true, 0);
#endif
}

static IrCheck ssa_ir_promote_scalar(const IrFunction* input, Arena* output, Arena* scratch, IrFunction* result)
{
    return ssa_ir_promote_path(input, output, scratch, result, false, 0);
}

static IrCheck ssa_ir_promote_verified(const IrFunction* input, bool vector, Arena* output, Arena* scratch, IrFunction* result, SsaCheck* verified)
{
    *verified = (SsaCheck){ .error = SSA_OK, .instruction = BUSTER_SSA_NONE };
    return ssa_ir_promote_path(input, output, scratch, result, BUSTER_VECTOR_PATH(vector), verified);
}
