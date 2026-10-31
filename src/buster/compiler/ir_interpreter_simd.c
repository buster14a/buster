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
        case IR_VCMP: operation = IR_SIMD_COMPARE; break;
        case IR_VSELECT: operation = IR_SIMD_SELECT; break;
        default: break;
    }
    return operation;
}

static bool ir_interpreter_simd_supported(const IrFunction* function)
{
    bool supported = !ir_vector_type(function->result_type);
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
                        op == IR_PHI || op == IR_LOAD || ir_interpreter_simd_opcode(op) != IR_SIMD_OPERATION_COUNT;
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
    const uint32_t ids[3] = { function->a[i], function->b[i], function->c[i] };
    const uint8_t mask = ir_operand_table[op];
    IrSimdAction action = {
        .operation = ir_interpreter_simd_opcode(op),
        .result = ir_simd_shape(function->type[i]),
        .predicate = op == IR_VCMP ? (IrSimdPredicate)function->c[i] : IR_SIMD_EQ,
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
    const bool executed = ir_simd_evaluate(&action, operands, 0, 0, 0, result);
    return executed ? INTERPRETER_OK : INTERPRETER_UNSUPPORTED;
}
