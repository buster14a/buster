// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded independent execution of the initial shared-IR SIMD subset. Include after ir_validate.c. This oracle
// accepts one straight-line block, explicit vector/mask arguments, and pointers as offsets into one bounded memory
// slice. Unsupported instructions are rejected before execution. Wide storage is allocated only for SIMD results;
// ordinary scalar interpreter values are unchanged. Scratch is restored on every outcome. An access fault retains
// earlier completed memory effects, as execution does; allocation/admission failure performs no memory access.
// Function columns, argument views and the memory slice must be disjoint from scratch's unused remainder. The
// memory slice is also disjoint from the argument/value views. Alignment promises apply to logical slice offsets.

typedef enum IrSimdExecutionStatus
{
    IR_SIMD_EXECUTION_OK,
    IR_SIMD_EXECUTION_INVALID,
    IR_SIMD_EXECUTION_UNSUPPORTED,
    IR_SIMD_EXECUTION_RESOURCE,
    IR_SIMD_EXECUTION_ACCESS,
} IrSimdExecutionStatus;

typedef struct IrSimdArgument IrSimdArgument;
struct IrSimdArgument
{
    const IrSimdValue* value;
    uint64_t offset;
};

typedef struct IrSimdExecution IrSimdExecution;
struct IrSimdExecution
{
    IrSimdExecutionStatus status;
    uint32_t instruction;
    uint8_t type;
    IrSimdValue value;
};

static IrSimdOperation ir_simd_operation(uint8_t operation)
{
    IrSimdOperation result = IR_SIMD_OPERATION_COUNT;
    switch (operation)
    {
        case IR_CONST:
        case IR_VCONST: result = IR_SIMD_CONSTANT; break;
        case IR_COPY: result = IR_SIMD_COPY; break;
        case IR_ADD: result = IR_SIMD_ADD; break;
        case IR_SUB: result = IR_SIMD_SUB; break;
        case IR_MUL: result = IR_SIMD_MUL; break;
        case IR_AND: result = IR_SIMD_AND; break;
        case IR_OR: result = IR_SIMD_OR; break;
        case IR_XOR: result = IR_SIMD_XOR; break;
        case IR_NOT: result = IR_SIMD_NOT; break;
        case IR_VCMP: result = IR_SIMD_COMPARE; break;
        case IR_VSELECT: result = IR_SIMD_SELECT; break;
        case IR_LOAD: result = IR_SIMD_LOAD; break;
        case IR_STORE: result = IR_SIMD_STORE; break;
        default: break;
    }
    return result;
}

static IrSimdExecution ir_simd_execute(const IrFunction* function, const IrSimdArgument* arguments, uint32_t argument_count,
                                      IrSimdMemory* memory, Arena* scratch)
{
    IrSimdExecution result = { .status = IR_SIMD_EXECUTION_INVALID, .instruction = BUSTER_IR_NONE };
    const uint64_t mark = scratch != NULL ? scratch->length : 0;
    IrCheck verified = { .error = IR_ERROR_STRUCTURE };
    if (function != NULL && scratch != NULL && (argument_count == 0 || arguments != NULL))
    {
        verified = ir_verify_complete(function, scratch, false);
    }
    if (verified.error == IR_ERROR_MEMORY)
    {
        result.status = IR_SIMD_EXECUTION_RESOURCE;
    }
    else if (verified.error == IR_OK && function->parameter_count == argument_count)
    {
        result.status = function->block_count == 1 && (function->result_type == IR_VOID || function->result_type >= IR_V4I32) ?
                        IR_SIMD_EXECUTION_OK : IR_SIMD_EXECUTION_UNSUPPORTED;
    }
    for (uint32_t argument = 0; result.status == IR_SIMD_EXECUTION_OK && argument < argument_count; argument += 1)
    {
        const uint8_t type = function->parameter_types[argument];
        if (type != IR_PTR && type < IR_V4I32)
        {
            result.status = IR_SIMD_EXECUTION_UNSUPPORTED;
        }
        else if (type != IR_PTR && !ir_simd_value_valid(arguments[argument].value, ir_simd_shape(type)))
        {
            result.status = IR_SIMD_EXECUTION_INVALID;
        }
    }
    uint32_t wide_count = 0;
    for (uint32_t node = 0; result.status == IR_SIMD_EXECUTION_OK && node < function->instruction_count; node += 1)
    {
        const uint8_t op = function->op[node];
        const uint8_t type = function->type[node];
        const bool wide = type >= IR_V4I32;
        wide_count += wide;
        bool supported = op == IR_RET || (op == IR_PARAM && (wide || type == IR_PTR)) || (op == IR_CONST && type == IR_PTR);
        if (wide && op != IR_PARAM)
        {
            supported = ir_simd_operation(op) != IR_SIMD_OPERATION_COUNT;
        }
        if (op == IR_STORE)
        {
            supported = function->type[function->b[node]] >= IR_V4I32;
        }
        if (!supported)
        {
            result.status = IR_SIMD_EXECUTION_UNSUPPORTED;
            result.instruction = node;
        }
    }
    const uint32_t count = result.status == IR_SIMD_EXECUTION_OK ? function->instruction_count : 0;
    uint32_t* indices = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    uint64_t* offsets = indices != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count) : NULL;
    IrSimdValue* values = offsets != NULL && wide_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, IrSimdValue, wide_count) : NULL;
    if (result.status == IR_SIMD_EXECUTION_OK && (indices == NULL || offsets == NULL || (wide_count != 0 && values == NULL)))
    {
        result.status = IR_SIMD_EXECUTION_RESOURCE;
    }
    if (result.status == IR_SIMD_EXECUTION_OK)
    {
        uint32_t next = 0;
        for (uint32_t node = 0; node < count; node += 1)
        {
            indices[node] = function->type[node] >= IR_V4I32 ? next++ : BUSTER_IR_NONE;
            offsets[node] = 0;
        }
    }
    for (uint32_t node = 0; result.status == IR_SIMD_EXECUTION_OK && node < count; node += 1)
    {
        const uint8_t op = function->op[node];
        const uint8_t type = function->type[node];
        const uint32_t a = function->a[node];
        const uint32_t b = function->b[node];
        const uint32_t c = function->c[node];
        result.instruction = node;
        if (op == IR_PARAM)
        {
            if (type == IR_PTR)
            {
                offsets[node] = arguments[a].offset;
            }
            else
            {
                values[indices[node]] = *arguments[a].value;
            }
        }
        else if (op == IR_CONST && type == IR_PTR)
        {
            offsets[node] = function->constants[a].low;
        }
        else if (op == IR_RET)
        {
            result.type = function->result_type;
            if (a != BUSTER_IR_NONE)
            {
                result.value = values[indices[a]];
            }
        }
        else
        {
            IrSimdAction action = { .operation = ir_simd_operation(op), .result = ir_simd_shape(type) };
            const IrSimdValue* operands[3] = { NULL, NULL, NULL };
            const uint32_t ids[3] = { a, b, c };
            const uint32_t mask = ir_value_mask(function, node);
            for (uint32_t operand = 0; operand < 3; operand += 1)
            {
                if ((mask & (1u << operand)) != 0 && function->type[ids[operand]] >= IR_V4I32)
                {
                    operands[operand] = values + indices[ids[operand]];
                    action.operands[operand] = ir_simd_shape(function->type[ids[operand]]);
                }
            }
            IrSimdValue scalar = { 0 };
            const IrSimdValue* constant = NULL;
            if (op == IR_CONST)
            {
                scalar.words[0] = function->constants[a].low;
                constant = &scalar;
            }
            else if (op == IR_VCONST)
            {
                constant = function->vector_constants + a;
            }
            action.predicate = op == IR_VCMP ? (IrSimdPredicate)c : IR_SIMD_EQ;
            action.volatile_memory = (function->flags[node] & BUSTER_IR_VOLATILE) != 0;
            if (op == IR_STORE)
            {
                operands[0] = operands[1];
                operands[1] = NULL;
                action.operands[0] = action.operands[1];
                action.operands[1] = IR_SIMD_NONE;
            }
            IrSimdValue staged = { 0 };
            const uint64_t offset = op == IR_LOAD || op == IR_STORE ? offsets[a] : 0;
            const uint64_t alignment = UINT64_C(1) << function->alignment_log2[node];
            const bool aligned = (offset & (alignment - 1)) == 0;
            const bool executed = aligned && ir_simd_evaluate(&action, operands, constant, memory, offset, &staged);
            if (!executed)
            {
                result.status = op == IR_LOAD || op == IR_STORE ? IR_SIMD_EXECUTION_ACCESS : IR_SIMD_EXECUTION_INVALID;
            }
            else if (indices[node] != BUSTER_IR_NONE)
            {
                values[indices[node]] = staged;
            }
        }
    }
    if (scratch != NULL)
    {
        arena_pop_to(scratch, mark);
    }
    return result;
}
