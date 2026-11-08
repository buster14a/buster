// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's vectors wider than the target's registers (#1624): a function's vectors of 32 or 64 bytes, at a target whose
// widest vector Tier 1 moves whole (ir_typed_vector_piece) is narrower (x86-64's and x86-64-v2's 16 bytes, an AVX
// target's 32), split into pieces of that width, so that selection, allocation and their checkers see vectors the target
// has registers for. The C driver's --tier1 runs it with ir_wide.c, after SSA promotion and before the optimizer
// (c_check_tier1.c), on what ir_lower_typed_target_pieces lowered. Include after ir.c.
//
// The typed stream carries such a vector only through its transports (tier0_validate_vector): Tier 0 refuses a row whose
// vectors are wider than its target's (Tier0Function.vector_bytes), and C's operators on vectors are rows, so a wide
// vector's IR is constants, loads and stores (of objects, through addresses, and of the expression stack's depth slots,
// which promotion makes phis), copies and phis. Each such instruction becomes its pieces' instructions, in its place, so
// that blocks keep their numbers, a block's phis stay first and each piece is defined where its value was:
// - a constant: each piece's own vconst, of its bytes of the payload; an undef: each piece's undef;
// - a copy: each piece's own, of the operand's pieces;
// - a load and a store: an access per piece, piece j's address j pieces' bytes on (its own address's offset plus them
//   where the pointer is an address), each at the access's alignment but at most a piece's, with its flags;
// - a phi: one per piece, with the same predecessors, each piece's list appended.
// Any other instruction that makes or reads a wide vector (an operation, a comparison, a select, which Tier 1 doesn't
// select for a vector of any width and the typed stream has none of, a bitcast, a call's argument or result, a row's
// operand), a wide vector among the function's or a call's types, and an atomic wide access refuse it
// (IR_ERROR_UNSUPPORTED), and the function falls back to Tier 0, as before.
//
// The rewrite is two passes of the same code, as ir_wide.c's: the first counts the output and places each value's
// pieces, the second writes the columns. Tables are copied whole, the new addresses and vector constants appended; each
// phi's predecessor list (each piece's) and each call's arguments are appended to the extra table, their values
// remapped. The counts are 64-bit, and an output past a u32's count is refused (IR_ERROR_LIMIT). SIMD: what pays. Every
// function --tier1 compiles at such a target is scanned for a wide vector instruction, 64 type bytes two compares on the
// vector path, one at a time on its scalar twin, which must agree; a function without one is returned as it is. The
// rewrite is scalar: it runs on the functions with wide vectors alone (the scalar host's emulations of AVX2's and
// AVX-512's intrinsics, host_scalar_vectors.h, among the compiler's own).

#ifndef BUSTER_IR_VECTOR_SPLIT_C
#define BUSTER_IR_VECTOR_SPLIT_C

// The most pieces a vector splits into: a 64-byte one into 16-byte ones.
#define BUSTER_IR_VECTOR_PIECES 4u

typedef struct IrVectorSplitResult IrVectorSplitResult;
struct IrVectorSplitResult
{
    // The split function, or the input itself (borrowed, its columns the input's) where it has no wide vector.
    IrFunction function;
    // The source's wide vector instructions, and whether the function was rewritten.
    uint32_t wide;
    bool split;
};

typedef struct IrVectorSplitWork IrVectorSplitWork;
struct IrVectorSplitWork
{
    const IrFunction* source;
    // The output's columns, or 0 while counting.
    IrFunction* output;
    // The pieces' type (IR_V4I32 or IR_V8I32) and bytes.
    uint32_t piece_type;
    uint32_t piece_bytes;
    // Each source value's pieces' output instructions, BUSTER_IR_VECTOR_PIECES apiece: a value no wider than a piece is
    // its first alone. The counting pass places them; the writing pass, which makes the same instructions, keeps them.
    uint32_t* pieces;
    // The output's counts so far, 64-bit as ir_wide.c's.
    uint64_t instruction;
    uint64_t address;
    uint64_t extra;
    uint64_t vector_constant;
    IrError error;
    uint32_t error_instruction;
};

// Whether a type is a vector wider than the pieces, `piece_type`: the vector types after it.
static bool ir_vector_split_wide(uint32_t type, uint32_t piece_type)
{
    return type > piece_type && type <= IR_V16I32;
}

// Whether a function has an instruction of a vector wider than `piece_type`: its type column 64 bytes two compares on
// the vector path, one at a time on its scalar twin, which must agree.
static bool ir_vector_split_present_path(const IrFunction* function, uint32_t piece_type, bool vector)
{
    (void)vector;

    bool found = false;
    if (BUSTER_VECTOR_PATH(vector))
    {
        for (uint32_t first = 0; !found && first < function->instruction_count; first += 64)
        {
            const uint32_t count = function->instruction_count - first < 64 ? function->instruction_count - first : 64;
            const __mmask64 active = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
            const __m512i types = _mm512_maskz_loadu_epi8(active, function->type + first);
            const __mmask64 above = _mm512_mask_cmpgt_epu8_mask(active, types, _mm512_set1_epi8((char)piece_type));
            found = _mm512_mask_cmplt_epu8_mask(above, types, _mm512_set1_epi8(IR_V16I32 + 1)) != 0;
        }
    }
    else
    {
        for (uint32_t i = 0; !found && i < function->instruction_count; i += 1)
        {
            found = ir_vector_split_wide(function->type[i], piece_type);
        }
    }
    return found;
}

// Whether the function's signature or a call's has a wide vector (a by-value vector is an object, a ptr to its bytes,
// so none should), which the split refuses.
static bool ir_vector_split_signature(const IrFunction* function, uint32_t piece_type)
{
    bool found = ir_vector_split_wide(function->result_type, piece_type);
    for (uint32_t p = 0; !found && p < function->parameter_count; p += 1)
    {
        found = ir_vector_split_wide(function->parameter_types[p], piece_type);
    }
    for (uint32_t p = 0; !found && p < function->call_type_count; p += 1)
    {
        found = ir_vector_split_wide(function->call_types[p], piece_type);
    }
    for (uint32_t k = 0; !found && k < function->call_count; k += 1)
    {
        found = ir_vector_split_wide(function->calls[k].result_type, piece_type);
    }
    return found;
}

static uint32_t ir_vector_split_emit(IrVectorSplitWork* work, uint32_t op, uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t id = (uint32_t)work->instruction;
    if (work->output != 0)
    {
        IrFunction* output = work->output;
        output->op[id] = (uint8_t)op;
        output->type[id] = (uint8_t)type;
        output->flags[id] = 0;
        output->alignment_log2[id] = 0;
        output->a[id] = a;
        output->b[id] = b;
        output->c[id] = c;
    }
    work->instruction += 1;
    return id;
}

// How many pieces a source value is: a wide vector's, or 1.
static uint32_t ir_vector_split_count(const IrVectorSplitWork* work, uint32_t value)
{
    const uint32_t type = work->source->type[value];
    return ir_vector_split_wide(type, work->piece_type) ? ir_type_bits[type] / ir_type_bits[work->piece_type] : 1;
}

// A value's piece j, as placed: the output instruction it is. Only the writing pass reads it.
static uint32_t ir_vector_split_piece(const IrVectorSplitWork* work, uint32_t value, uint32_t j)
{
    return work->pieces[(uint64_t)value * BUSTER_IR_VECTOR_PIECES + j];
}

// The counting pass's place of `value`'s piece j; the writing pass, which makes the same instructions, keeps it.
static void ir_vector_split_place(IrVectorSplitWork* work, uint32_t value, uint32_t j, uint32_t id)
{
    if (work->output == 0)
    {
        work->pieces[(uint64_t)value * BUSTER_IR_VECTOR_PIECES + j] = id;
    }
}

// A piece's memory access's flags and alignment: the access's, at most a piece's alignment.
static void ir_vector_split_memory(IrVectorSplitWork* work, uint32_t id, uint32_t source)
{
    if (work->output != 0)
    {
        const uint32_t most = (uint32_t)__builtin_ctz(work->piece_bytes);
        work->output->flags[id] = work->source->flags[source];
        work->output->alignment_log2[id] = work->source->alignment_log2[source] < most ? work->source->alignment_log2[source] : (uint8_t)most;
    }
}

// Piece j's address: `pointer` itself for the first, else j pieces' bytes past it, whose own offset takes them where it
// is an address.
static uint32_t ir_vector_split_address(IrVectorSplitWork* work, uint32_t pointer, uint32_t j)
{
    const IrFunction* f = work->source;
    uint32_t result = ir_vector_split_piece(work, pointer, 0);
    if (j != 0)
    {
        const int64_t bytes = (int64_t)j * work->piece_bytes;
        uint32_t base = pointer;
        uint32_t index = BUSTER_IR_NONE;
        IrAddress address = { .offset = bytes, .scale = 1 };
        int64_t offset = 0;
        if (f->op[pointer] == IR_ADDRESS && !__builtin_add_overflow(f->addresses[f->c[pointer]].offset, bytes, &offset))
        {
            base = f->a[pointer];
            index = f->b[pointer];
            address = (IrAddress){ .offset = offset, .scale = f->addresses[f->c[pointer]].scale };
        }
        const uint32_t entry = (uint32_t)work->address;
        if (work->output != 0)
        {
            work->output->addresses[entry] = address;
        }
        work->address += 1;
        result = ir_vector_split_emit(work, IR_ADDRESS, IR_PTR, ir_vector_split_piece(work, base, 0), index == BUSTER_IR_NONE ? BUSTER_IR_NONE : ir_vector_split_piece(work, index, 0),
                                      entry);
    }
    return result;
}

// A wide vector instruction i, as its pieces' instructions. Returns false for one this doesn't split.
static bool ir_vector_split_value(IrVectorSplitWork* work, uint32_t i)
{
    const IrFunction* f = work->source;
    const uint32_t op = f->op[i];
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    const uint32_t pieces = ir_vector_split_count(work, i);
    const uint32_t type = work->piece_type;
    const uint32_t none = BUSTER_IR_NONE;
    bool supported = true;
    switch (op)
    {
        case IR_VCONST:
        {
            // Each piece's bytes of the payload, a constant of its own appended to the table.
            const uint32_t words = work->piece_bytes / 8;
            for (uint32_t j = 0; j < pieces; j += 1)
            {
                const uint32_t entry = (uint32_t)work->vector_constant;
                if (work->output != 0)
                {
                    IrSimdValue value = { 0 };
                    memcpy(value.words, f->vector_constants[a].words + j * words, (uint64_t)words * 8);
                    work->output->vector_constants[entry] = value;
                }
                work->vector_constant += 1;
                const uint32_t id = ir_vector_split_emit(work, IR_VCONST, type, entry, none, none);
                if (work->output != 0)
                {
                    work->output->flags[id] = f->flags[i];
                }
                ir_vector_split_place(work, i, j, id);
            }
        } break;
        case IR_UNDEF:
            for (uint32_t j = 0; j < pieces; j += 1)
            {
                ir_vector_split_place(work, i, j, ir_vector_split_emit(work, IR_UNDEF, type, none, none, none));
            }
            break;
        case IR_COPY:
            for (uint32_t j = 0; j < pieces; j += 1)
            {
                ir_vector_split_place(work, i, j, ir_vector_split_emit(work, IR_COPY, type, ir_vector_split_piece(work, a, j), none, none));
            }
            break;
        case IR_LOAD:
            supported = (f->flags[i] & BUSTER_IR_ATOMIC) == 0;
            for (uint32_t j = 0; supported && j < pieces; j += 1)
            {
                const uint32_t id = ir_vector_split_emit(work, IR_LOAD, type, ir_vector_split_address(work, a, j), none, none);
                ir_vector_split_memory(work, id, i);
                ir_vector_split_place(work, i, j, id);
            }
            break;
        case IR_PHI:
        {
            // Each piece's predecessor list is appended, its values remapped there (ir_wide.c's reason: the verifier
            // lets extra's slices overlap).
            const uint32_t first = (uint32_t)work->extra;
            for (uint32_t j = 0; j < pieces; j += 1)
            {
                const uint32_t list = (uint32_t)(first + (uint64_t)j * b * 2);
                ir_vector_split_place(work, i, j, ir_vector_split_emit(work, IR_PHI, type, list, b, none));
                for (uint32_t p = 0; work->output != 0 && p < b; p += 1)
                {
                    work->output->extra[list + p * 2] = f->extra[a + p * 2];
                    work->output->extra[list + p * 2 + 1] = ir_vector_split_piece(work, f->extra[a + p * 2 + 1], j);
                }
            }
            work->extra += (uint64_t)b * 2 * pieces;
        } break;
        default: supported = false; break;
    }
    return supported;
}

// Whether instruction i reads a wide vector operand.
static bool ir_vector_split_reads(const IrVectorSplitWork* work, uint32_t i)
{
    const IrFunction* f = work->source;
    const uint32_t mask = ir_value_mask(f, i);
    const uint32_t piece_type = work->piece_type;
    bool wide = ((mask & 1) != 0 && ir_vector_split_wide(f->type[f->a[i]], piece_type)) || ((mask & 2) != 0 && ir_vector_split_wide(f->type[f->b[i]], piece_type)) ||
                ((mask & 4) != 0 && ir_vector_split_wide(f->type[f->c[i]], piece_type));
    if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
    {
        const IrCall call = f->calls[f->b[i]];
        for (uint32_t p = 0; !wide && p < call.argument_count; p += 1)
        {
            wide = ir_vector_split_wide(f->type[f->extra[call.argument_first + p]], piece_type);
        }
    }
    return wide;
}

// A narrower instruction i that reads a wide vector: a store of one, a store per piece. Returns false for any other.
static bool ir_vector_split_use(IrVectorSplitWork* work, uint32_t i)
{
    const IrFunction* f = work->source;
    const uint32_t a = f->a[i];
    const uint32_t b = f->b[i];
    const bool supported = f->op[i] == IR_STORE && !ir_vector_split_wide(f->type[a], work->piece_type) && (f->flags[i] & BUSTER_IR_ATOMIC) == 0;
    const uint32_t pieces = supported ? ir_vector_split_count(work, b) : 0;
    for (uint32_t j = 0; j < pieces; j += 1)
    {
        const uint32_t address = ir_vector_split_address(work, a, j);
        const uint32_t id = ir_vector_split_emit(work, IR_STORE, IR_VOID, address, ir_vector_split_piece(work, b, j), BUSTER_IR_NONE);
        ir_vector_split_memory(work, id, i);
        // The store's own value, its first piece's.
        if (j == 0)
        {
            ir_vector_split_place(work, i, 0, id);
        }
    }
    return supported;
}

// Any other instruction, its value operands remapped (ir_wide_copy's: a phi's list and a call's arguments into slices of
// their own, appended).
static void ir_vector_split_copy(IrVectorSplitWork* work, uint32_t i)
{
    const IrFunction* f = work->source;
    const uint32_t op = f->op[i];
    const bool phi = op == IR_PHI;
    const bool call = op == IR_CALL || op == IR_CALL_INDIRECT;
    const uint32_t list = (uint32_t)work->extra;
    work->extra += phi ? (uint64_t)f->b[i] * 2 : call ? f->calls[f->b[i]].argument_count : 0;
    const uint32_t mask = ir_value_mask(f, i);
    const uint32_t a = phi ? list : (mask & 1) != 0 ? ir_vector_split_piece(work, f->a[i], 0) : f->a[i];
    const uint32_t b = (mask & 2) != 0 ? ir_vector_split_piece(work, f->b[i], 0) : f->b[i];
    const uint32_t c = (mask & 4) != 0 ? ir_vector_split_piece(work, f->c[i], 0) : f->c[i];
    const uint32_t id = ir_vector_split_emit(work, op, f->type[i], a, b, c);
    if (work->output != 0)
    {
        IrFunction* output = work->output;
        output->flags[id] = f->flags[i];
        output->alignment_log2[id] = f->alignment_log2[i];
        if (phi)
        {
            for (uint32_t p = 0; p < f->b[i]; p += 1)
            {
                output->extra[list + p * 2] = f->extra[f->a[i] + p * 2];
                output->extra[list + p * 2 + 1] = ir_vector_split_piece(work, f->extra[f->a[i] + p * 2 + 1], 0);
            }
        }
        else if (call)
        {
            const IrCall source = f->calls[f->b[i]];
            output->calls[f->b[i]].argument_first = list;
            for (uint32_t p = 0; p < source.argument_count; p += 1)
            {
                output->extra[list + p] = ir_vector_split_piece(work, f->extra[source.argument_first + p], 0);
            }
        }
    }
    ir_vector_split_place(work, i, 0, id);
}

// One pass over the source's instructions, block by block: the counting one (work->output 0) or the writing one. A phi
// reads values its block's predecessors define, which a later block may: the counting pass places every value before
// the writing pass reads any, so a phi's operands' pieces are known there.
static void ir_vector_split_pass(IrVectorSplitWork* work)
{
    const IrFunction* f = work->source;
    work->instruction = 0;
    work->address = f->address_count;
    work->extra = f->extra_count;
    work->vector_constant = f->vector_constant_count;
    for (uint32_t block = 0; work->error == IR_OK && block < f->block_count; block += 1)
    {
        if (work->output != 0)
        {
            work->output->block_offsets[block] = (uint32_t)work->instruction;
        }
        for (uint32_t i = f->block_offsets[block]; work->error == IR_OK && i < f->block_offsets[block + 1]; i += 1)
        {
            const uint64_t first = work->instruction;
            bool supported = true;
            if (ir_vector_split_wide(f->type[i], work->piece_type))
            {
                supported = ir_vector_split_value(work, i);
            }
            else if (ir_vector_split_reads(work, i))
            {
                supported = ir_vector_split_use(work, i);
            }
            else
            {
                ir_vector_split_copy(work, i);
            }
            if (!supported)
            {
                work->error = IR_ERROR_UNSUPPORTED;
                work->error_instruction = i;
            }
            else if (work->instruction > UINT32_MAX || work->address > UINT32_MAX || work->extra > UINT32_MAX || work->vector_constant > UINT32_MAX)
            {
                work->error = IR_ERROR_LIMIT;
                work->error_instruction = i;
            }
            if (work->error == IR_OK && work->output != NULL && work->output->debug != NULL)
            {
                for (uint64_t node = first; node < work->instruction; node += 1)
                {
                    work->output->debug->origins[node] = f->debug->origins[i];
                }
            }
        }
    }
    if (work->output != 0 && work->error == IR_OK)
    {
        work->output->block_offsets[f->block_count] = (uint32_t)work->instruction;
    }
}

// The split of `function` into pieces of `piece_bytes` (16 or 32; 64, a target with every width, splits nothing).
static IrCheck ir_vector_split_path(const IrFunction* function, uint32_t piece_bytes, Arena* output, Arena* scratch, bool vector, IrVectorSplitResult* result)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    *result = (IrVectorSplitResult){ .function = *function };
    const uint32_t piece_type = piece_bytes == 16 ? IR_V4I32 : piece_bytes == 32 ? IR_V8I32 : IR_V16I32;
    const bool present = output != scratch && piece_type != IR_V16I32 && ir_vector_split_present_path(function, piece_type, vector);
    if (output == scratch || (piece_bytes != 16 && piece_bytes != 32 && piece_bytes != 64))
    {
        check.error = IR_ERROR_STRUCTURE;
    }
    else if (present && ir_vector_split_signature(function, piece_type))
    {
        check.error = IR_ERROR_UNSUPPORTED;
    }
    if (check.error == IR_OK && present)
    {
        const uint32_t count = function->instruction_count;
        bool ok = true;
        IrVectorSplitWork work = {
            .source = function,
            .piece_type = piece_type,
            .piece_bytes = piece_bytes,
            .pieces = ir_allocate(scratch, (uint64_t)count * BUSTER_IR_VECTOR_PIECES, sizeof(uint32_t), &ok),
        };
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
        if (check.error == IR_OK)
        {
            ir_vector_split_pass(&work);
            check = ir_check(work.error, work.error_instruction, 0);
        }
        IrFunction split = { 0 };
        if (check.error == IR_OK)
        {
            const IrSizes sizes = {
                .instruction_count = work.instruction,
                .block_count = function->block_count,
                .extra_count = work.extra,
                .constant_count = function->constant_count,
                .slot_count = function->slot_count,
                .address_count = work.address,
                .call_count = function->call_count,
                .call_type_count = function->call_type_count,
                .parameter_count = function->parameter_count,
                .label_count = function->label_count,
                .abi_call_count = function->abi_call_count,
                .passed_count = function->passed_count,
                .vector_constant_count = work.vector_constant,
                .row_count = function->row_count,
            };
            check.error = ir_create(output, &sizes, &split);
        }
        if (check.error == IR_OK && !ir_debug_create(output, &split, function->debug))
        {
            check.error = IR_ERROR_MEMORY;
        }
        if (check.error == IR_OK)
        {
            if (split.debug != NULL && function->slot_count != 0)
            {
                memcpy(split.debug->locals, function->debug->locals, (uint64_t)function->slot_count * sizeof(uint32_t));
            }
            split.result_type = function->result_type;
            split.symbol_count = function->symbol_count;
            split.parameter_passed = function->parameter_passed;
            split.result_size = function->result_size;
            split.result_abi = function->result_abi;
            split.variadic = function->variadic;
            split.incoming = function->incoming;
            // The tables whole (memcpy of none would take ir_create's null column), the new addresses and vector
            // constants after.
            if (function->extra_count != 0)
            {
                memcpy(split.extra, function->extra, (uint64_t)function->extra_count * sizeof(uint32_t));
            }
            if (function->constant_count != 0)
            {
                memcpy(split.constants, function->constants, (uint64_t)function->constant_count * sizeof(IrConstant));
            }
            if (function->slot_count != 0)
            {
                memcpy(split.slots, function->slots, (uint64_t)function->slot_count * sizeof(IrSlot));
            }
            if (function->address_count != 0)
            {
                memcpy(split.addresses, function->addresses, (uint64_t)function->address_count * sizeof(IrAddress));
            }
            if (function->call_count != 0)
            {
                memcpy(split.calls, function->calls, (uint64_t)function->call_count * sizeof(IrCall));
            }
            if (function->call_type_count != 0)
            {
                memcpy(split.call_types, function->call_types, function->call_type_count);
            }
            if (function->parameter_count != 0)
            {
                memcpy(split.parameter_types, function->parameter_types, function->parameter_count);
            }
            if (function->label_count != 0)
            {
                memcpy(split.labels, function->labels, (uint64_t)function->label_count * sizeof(uint32_t));
            }
            if (function->abi_call_count != 0)
            {
                memcpy(split.abi_calls, function->abi_calls, (uint64_t)function->abi_call_count * sizeof(IrAbiCall));
            }
            if (function->passed_count != 0)
            {
                memcpy(split.passed, function->passed, (uint64_t)function->passed_count * sizeof(IrPassed));
            }
            if (function->vector_constant_count != 0)
            {
                memcpy(split.vector_constants, function->vector_constants, (uint64_t)function->vector_constant_count * sizeof(IrSimdValue));
            }
            if (function->row_count != 0)
            {
                memcpy(split.rows, function->rows, (uint64_t)function->row_count * sizeof(Tier0Operation));
            }
            const uint64_t instructions = work.instruction;
            work.output = &split;
            ir_vector_split_pass(&work);
            check = ir_check(work.instruction == instructions ? work.error : IR_ERROR_STRUCTURE, work.error_instruction, 0);
        }
        if (check.error == IR_OK)
        {
            uint32_t wide = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                wide += ir_vector_split_wide(function->type[i], piece_type);
            }
            *result = (IrVectorSplitResult){ .function = split, .wide = wide, .split = true };
        }
    }
    if (check.error != IR_OK)
    {
        *result = (IrVectorSplitResult){ 0 };
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return check;
}

static __attribute__((unused)) IrCheck ir_vector_split(const IrFunction* function, uint32_t piece_bytes, Arena* output, Arena* scratch, IrVectorSplitResult* result)
{
#if !BUSTER_HOST_VECTOR
    return ir_vector_split_path(function, piece_bytes, output, scratch, false, result);
#else
    return ir_vector_split_path(function, piece_bytes, output, scratch, true, result);
#endif
}

static __attribute__((unused)) IrCheck ir_vector_split_scalar(const IrFunction* function, uint32_t piece_bytes, Arena* output, Arena* scratch, IrVectorSplitResult* result)
{
    return ir_vector_split_path(function, piece_bytes, output, scratch, false, result);
}

#endif
