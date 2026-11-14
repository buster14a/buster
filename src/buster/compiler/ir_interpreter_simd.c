// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Sparse vector storage for the shared scalar interpreter, included after IrInterpreterFrame. No host SIMD or native
// ABI is used. The established action oracle defines lane arithmetic; CFG, phi snapshots and checked memory remain
// the interpreter's own. Vector ABI values refuse before the frame has any effects; aggregate byte pointers remain
// supported through the existing ABI records. A future native or first-class vector call boundary needs its own API.

static IrSimdOperation ir_interpreter_simd_opcode(uint8_t op)
{
    IrSimdOperation operation = IR_SIMD_OPERATION_COUNT;
    switch (op)
    {
        case IR_ADD: operation = IR_SIMD_ADD; break;
        case IR_SUB: operation = IR_SIMD_SUB; break;
        case IR_MUL: operation = IR_SIMD_MUL; break;
        case IR_AND: operation = IR_SIMD_AND; break;
        case IR_OR: operation = IR_SIMD_OR; break;
        case IR_XOR: operation = IR_SIMD_XOR; break;
        case IR_NOT: operation = IR_SIMD_NOT; break;
        case IR_SHL: operation = IR_SIMD_SHL; break;
        case IR_LSHR: operation = IR_SIMD_LSHR; break;
        case IR_ASHR: operation = IR_SIMD_ASHR; break;
        case IR_FADD: operation = IR_SIMD_FADD; break;
        case IR_FSUB: operation = IR_SIMD_FSUB; break;
        case IR_FMUL: operation = IR_SIMD_FMUL; break;
        case IR_FDIV: operation = IR_SIMD_FDIV; break;
        case IR_VCMP: operation = IR_SIMD_COMPARE; break;
        case IR_VSELECT: operation = IR_SIMD_SELECT; break;
        default: break;
    }
    return operation;
}

// A width change's, a permute's, a conversion's, a scalar shift's or a lane operation's model operation (#1840: extract
// to lane_op), or
// IR_SIMD_OPERATION_COUNT for any other intrinsic.
static IrSimdOperation ir_interpreter_simd_intrinsic(uint32_t kind)
{
    IrSimdOperation operation = IR_SIMD_OPERATION_COUNT;
    switch (kind)
    {
        case BUSTER_IR_INTRINSIC_EXTRACT: operation = IR_SIMD_EXTRACT; break;
        case BUSTER_IR_INTRINSIC_WIDEN: operation = IR_SIMD_WIDEN; break;
        case BUSTER_IR_INTRINSIC_INSERT: operation = IR_SIMD_INSERT; break;
        case BUSTER_IR_INTRINSIC_PERMUTE: operation = IR_SIMD_PERMUTE; break;
        case BUSTER_IR_INTRINSIC_SHUFFLE: operation = IR_SIMD_SHUFFLE; break;
        case BUSTER_IR_INTRINSIC_ALIGN: operation = IR_SIMD_ALIGN; break;
        case BUSTER_IR_INTRINSIC_SHUFFLE_LANES: operation = IR_SIMD_SHUFFLE_LANES; break;
        case BUSTER_IR_INTRINSIC_EXTEND: operation = IR_SIMD_EXTEND; break;
        case BUSTER_IR_INTRINSIC_TRUNCATE: operation = IR_SIMD_TRUNCATE; break;
        case BUSTER_IR_INTRINSIC_SHIFT: operation = IR_SIMD_SHIFT; break;
        case BUSTER_IR_INTRINSIC_LANE_OP: operation = IR_SIMD_LANE_OP; break;
        case BUSTER_IR_INTRINSIC_PERMUTE2: operation = IR_SIMD_PERMUTE2; break;
        case BUSTER_IR_INTRINSIC_TERNLOG: operation = IR_SIMD_TERNLOG; break;
        case BUSTER_IR_INTRINSIC_COMPRESS: operation = IR_SIMD_COMPRESS; break;
        case BUSTER_IR_INTRINSIC_EXPAND: operation = IR_SIMD_EXPAND; break;
        case BUSTER_IR_INTRINSIC_FUNNEL: operation = IR_SIMD_FUNNEL; break;
        case BUSTER_IR_INTRINSIC_AES: operation = IR_SIMD_AES; break;
        case BUSTER_IR_INTRINSIC_LANE_EXTRACT: operation = IR_SIMD_LANE_EXTRACT; break;
        case BUSTER_IR_INTRINSIC_LANE_INSERT: operation = IR_SIMD_LANE_INSERT; break;
        case BUSTER_IR_INTRINSIC_MOVEMASK: operation = IR_SIMD_MOVEMASK; break;
        case BUSTER_IR_INTRINSIC_REDUCE: operation = IR_SIMD_REDUCE; break;
        case BUSTER_IR_INTRINSIC_LANE_UNARY: operation = IR_SIMD_LANE_UNARY; break;
        case BUSTER_IR_INTRINSIC_FROUND: operation = IR_SIMD_FROUND; break;
        case BUSTER_IR_INTRINSIC_CVTT: operation = IR_SIMD_CVTT; break;
        case BUSTER_IR_INTRINSIC_BITSHUFFLE: operation = IR_SIMD_BITSHUFFLE; break;
        case BUSTER_IR_INTRINSIC_QUADWORD_OP: operation = IR_SIMD_QUADWORD_OP; break;
        default: break;
    }
    return operation;
}

static bool ir_interpreter_simd_supported(const IrFunction* function)
{
    // A machine row (#141, IrFunction.rows) is x86 records, which the interpreter has no model of: Tier 1's native code
    // is their oracle, against Tier 0's of the same rows.
    bool supported = !ir_vector_type(function->result_type) && function->row_count == 0;
    for (uint32_t p = 0; supported && p < function->parameter_count; p += 1)
    {
        supported = !ir_vector_type(function->parameter_types[p]);
    }
    // The external native bridge and call staging do not define a mask-register ABI either. Mask entry arguments
    // and results are logical bits, not native registers; calls carrying them must refuse rather than guess.
    for (uint32_t p = 0; supported && p < function->call_type_count; p += 1)
    {
        supported = function->call_types[p] < IR_V4I32;
    }
    for (uint32_t p = 0; supported && p < function->call_count; p += 1)
    {
        supported = function->calls[p].result_type < IR_V4I32;
    }
    for (uint32_t i = 0; supported && i < function->instruction_count; i += 1)
    {
        if (ir_vector_type(function->type[i]))
        {
            const uint8_t op = function->op[i];
            supported = op == IR_VCONST || op == IR_UNDEF || op == IR_COPY || op == IR_BITCAST || op == IR_SELECT ||
                        op == IR_PHI || op == IR_LOAD || op == IR_MASKED_LOAD || (op == IR_INTRINSIC && function->c[i] == BUSTER_IR_INTRINSIC_SPLAT) ||
                        (op == IR_INTRINSIC && ir_interpreter_simd_intrinsic(ir_intrinsic_kind(function->c[i])) != IR_SIMD_OPERATION_COUNT) ||
                        ir_interpreter_simd_opcode(op) != IR_SIMD_OPERATION_COUNT;
        }
    }
    return supported;
}

static InterpreterStatus ir_interpreter_simd_storage(IrInterpreterFrame* frame, Arena* arena)
{
    const IrFunction* function = frame->function;
    uint32_t vectors = 0;
    uint32_t phis = 0;
    for (uint32_t i = 0; i < function->instruction_count; i += 1)
    {
        const bool vector = ir_vector_type(function->type[i]);
        vectors += vector;
        phis += vector && function->op[i] == IR_PHI;
    }
    InterpreterStatus status = INTERPRETER_OK;
    if (vectors != 0)
    {
        frame->vector_indices = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, function->instruction_count);
        frame->vector_values = BUSTER_ARENA_PUSH_ARRAY(arena, IrSimdValue, vectors);
        frame->vector_phis = phis != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, IrSimdValue, phis) : 0;
        if (frame->vector_indices == 0 || frame->vector_values == 0 || (phis != 0 && frame->vector_phis == 0))
        {
            status = INTERPRETER_RESOURCE;
        }
        else
        {
            memset(frame->vector_values, 0, (uint64_t)vectors * sizeof(IrSimdValue));
            uint32_t next = 0;
            for (uint32_t i = 0; i < function->instruction_count; i += 1)
            {
                frame->vector_indices[i] = ir_vector_type(function->type[i]) ? next++ : BUSTER_IR_NONE;
            }
        }
    }
    return status;
}

static IrSimdValue ir_interpreter_simd_value(const IrInterpreterFrame* frame, uint32_t id)
{
    IrSimdValue value = {0};
    if (ir_vector_type(frame->function->type[id]))
    {
        value = frame->vector_values[frame->vector_indices[id]];
    }
    else
    {
        value.words[0] = frame->values[id].low;
        value.words[1] = frame->values[id].high;
    }
    return value;
}

static InterpreterStatus ir_interpreter_simd_operation(const IrInterpreterFrame* frame, uint32_t i, IrSimdValue* result)
{
    const IrFunction* function = frame->function;
    const uint8_t op = function->op[i];
    bool executed = false;
    if (op == IR_INTRINSIC && function->c[i] == BUSTER_IR_INTRINSIC_SPLAT)
    {
        const uint32_t source = function->a[i];
        executed = ir_splat_value(function->type[i], function->type[source], frame->values[source].low, result);
    }
    else
    {
        // A ternary intrinsic's third operand (#1840) is in the extra array, its `c` its kind and that place.
        const bool ternary = ir_ternary_instruction(function, i);
        const uint32_t ids[3] = { function->a[i], function->b[i], ternary ? ir_intrinsic_third(function, i) : function->c[i] };
        const uint32_t mask = ir_value_mask(function, i) | (ternary ? 4u : 0u);
        const bool intrinsic = op == IR_INTRINSIC;
        IrSimdAction action = {
            .operation = intrinsic ? ir_interpreter_simd_intrinsic(ir_intrinsic_kind(function->c[i])) : ir_interpreter_simd_opcode(op),
            .result = ir_simd_shape(function->type[i]),
            .predicate = op == IR_VCMP ? (IrSimdPredicate)function->c[i] : IR_SIMD_EQ,
            // Its lanes (#141's element types): a vector operation's flags, which the verifier checked; an intrinsic's
            // flags are its part (#1840's width changes), and it reads no lanes.
            .lane = intrinsic ? IR_SIMD_LANE_I32 : (IrSimdLane)function->flags[i],
            .index = intrinsic ? function->flags[i] : 0,
        };
        IrSimdValue values[3] = {0};
        const IrSimdValue* operands[3] = {0};
        for (uint32_t p = 0; p < 3; p += 1)
        {
            if ((mask & (1u << p)) != 0)
            {
                values[p] = ir_interpreter_simd_value(frame, ids[p]);
                operands[p] = values + p;
                action.operands[p] = ir_simd_shape(function->type[ids[p]]);
            }
        }
        executed = ir_simd_evaluate(&action, operands, 0, 0, 0, result);
    }
    return executed ? INTERPRETER_OK : INTERPRETER_UNSUPPORTED;
}
