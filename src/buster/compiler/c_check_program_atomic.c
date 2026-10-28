// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Atomic object expressions and the C11 forms that need a representation buffer. Source operands are evaluated once
// before this task runs. A failed compare-exchange refreshes the expected bytes; only the arithmetic is retried.
// Natural scalar widths use the existing GNU raw-bit leaves. Other layouts use libatomic's size-and-pointer ABI,
// retaining atomic storage padding separately from the underlying C value's bytes.

enum
{
    BUSTER_CHECK_ATOMIC_LOAD,
    BUSTER_CHECK_ATOMIC_STORE,
    BUSTER_CHECK_ATOMIC_EXCHANGE,
    BUSTER_CHECK_ATOMIC_COMPARE,
    BUSTER_CHECK_ATOMIC_UPDATE,
    BUSTER_CHECK_ATOMIC_LOCK_FREE,
};

static const CheckProgramHelper* check_lower_atomic_leaf(const CheckLowerWriter* writer, CBuiltinId id, uint64_t size)
{
    const CheckProgramHelpers* helpers = &writer->output->helpers;
    const CheckProgramHelper* found = NULL;
    for (uint32_t i = 0; i < helpers->count && found == NULL; i += 1)
    {
        const CheckProgramHelper* helper = helpers->items + i;
        if (helper->id == id && helper->bytes == size && (!helper->rmw || helper->rmw_format == CBUILTIN_ATOMIC_RMW_INTEGER_BITS))
        {
            found = helper;
        }
    }
    return found;
}

static void check_lower_atomic_buffer_value(CheckLowerWriter* writer, const CheckLowerAtomic* plan, uint32_t buffer)
{
    const bool aggregate = check_lower_aggregate(writer, plan->value_type);
    check_lower_emit(writer, aggregate ? BUSTER_TYPED_LOCAL_ADDRESS : BUSTER_TYPED_LOCAL,
                     aggregate ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, plan->value_type), buffer);
    if (plan->value_type == BUSTER_TYPE_BOOL)
    {
        check_lower_scalar_conversion(writer, BUSTER_TYPE_UCHAR, BUSTER_TYPE_BOOL);
    }
}

static void check_lower_atomic_order(CheckLowerWriter* writer, const CheckLowerAtomic* plan, bool failure)
{
    const bool init = plan->id == CBUILTIN_C11_ATOMIC_INIT;
    const uint32_t index = plan->operation == BUSTER_CHECK_ATOMIC_COMPARE ? failure ? 4 : 3
                           : plan->operation == BUSTER_CHECK_ATOMIC_LOAD  ? 1
                                                                          : 2;
    if (!plan->builtin || init || plan->operation == BUSTER_CHECK_ATOMIC_UPDATE)
    {
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, init ? 0 : 5);
    }
    else
    {
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, plan->arguments[index]);
    }
}

// Emits the load/store/exchange/CAS, from already materialized addresses and value bytes. A native leaf returns raw
// unsigned long bits, while generic CAS returns C's bool; normalize the latter result to I32 for its branch consumer.
static void check_lower_atomic_call(CheckLowerWriter* writer, const CheckLowerAtomic* plan, uint8_t operation)
{
    const bool compare = operation == BUSTER_CHECK_ATOMIC_COMPARE;
    const bool load = operation == BUSTER_CHECK_ATOMIC_LOAD;
    const bool store = operation == BUSTER_CHECK_ATOMIC_STORE;
    const CBuiltinId id = compare ? CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N
                          : load  ? CBUILTIN_ATOMIC_LOAD_N
                          : store ? CBUILTIN_ATOMIC_STORE_N
                                  : CBUILTIN_ATOMIC_EXCHANGE_N;
    const CheckProgramHelper* helper = plan->native ? check_lower_atomic_leaf(writer, id, plan->size) : NULL;
    if (plan->native && helper == NULL)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    else if (helper != NULL)
    {
        const uint32_t call =
            check_lower_native_call(writer, helper->symbol, helper->result_type, helper->parameter_types, helper->parameter_count);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, plan->address);
        if (compare)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, plan->expected);
        }
        if (!load)
        {
            const uint8_t bits = check_lower_piece_class((uint32_t)plan->size);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, bits, plan->desired);
            if (bits != BUSTER_TYPED_CLASS_U64)
            {
                check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            }
        }
        if (compare)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
        }
        check_lower_atomic_order(writer, plan, false);
        if (compare)
        {
            check_lower_atomic_order(writer, plan, true);
        }
        check_lower_emit(writer, BUSTER_TYPED_CALL, check_lower_class(writer, helper->result_type), call);
        if (compare)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
        }
        else if (!store)
        {
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, plan->result);
        }
    }
    else
    {
        const CheckLowerRuntime runtime = compare ? CHECK_LOWER_RUNTIME_ATOMIC_COMPARE_EXCHANGE
                                          : load  ? CHECK_LOWER_RUNTIME_ATOMIC_LOAD
                                          : store ? CHECK_LOWER_RUNTIME_ATOMIC_STORE
                                                  : CHECK_LOWER_RUNTIME_ATOMIC_EXCHANGE;
        const TypeId arguments[6] = {BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG,
                                     BUSTER_TYPE_ULONG, compare || (!load && !store) ? BUSTER_TYPE_ULONG : BUSTER_TYPE_INT,
                                     BUSTER_TYPE_INT,   BUSTER_TYPE_INT};
        const uint32_t count = compare ? 6 : load || store ? 4 : 5;
        const uint32_t call =
            check_lower_runtime_call(writer, runtime, compare ? BUSTER_TYPE_BOOL : BUSTER_TYPE_VOID, arguments, count, BUSTER_CHECK_NONE);
        check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, plan->size);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, plan->address);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64,
                         compare ? plan->expected
                         : load  ? plan->result
                                 : plan->desired);
        if (compare || (!load && !store))
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, compare ? plan->desired : plan->result);
        }
        check_lower_atomic_order(writer, plan, false);
        if (compare)
        {
            check_lower_atomic_order(writer, plan, true);
        }
        check_lower_emit(writer, BUSTER_TYPED_CALL, compare ? BUSTER_TYPED_CLASS_U8 : BUSTER_TYPED_CLASS_VOID, call);
        if (compare)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
        }
    }
}

static void check_lower_atomic_compute(CheckLowerWriter* writer, const CheckLowerAtomic* plan)
{
    const CheckGraph* graph = &writer->program->input->graph;
    const CheckStream* stream = writer->program->stream;
    const CheckOp op = graph->ops[plan->node];
    const bool increment = !plan->builtin && op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    const bool subtract = plan->builtin ? plan->id == CBUILTIN_C11_ATOMIC_FETCH_SUB
                                        : op == CHECK_OP_PRE_DECREMENT || op == CHECK_OP_POST_DECREMENT || op == CHECK_OP_SUBTRACT_ASSIGN;
    const bool pointer = check_action_kind(writer->program->types, plan->value_type) == BUSTER_TYPE_KIND_POINTER;
    const TypeId computed =
        plan->builtin ? plan->value_type == BUSTER_TYPE_BOOL ? BUSTER_TYPE_UINT : type_promote(writer->program->types, plan->value_type)
                      : stream->conversions[plan->node].operands[0].type;
    const uint8_t class = check_lower_class(writer, computed);
    uint8_t operation = increment ? subtract ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD : check_lower_binary(op);
    if (plan->builtin)
    {
        operation = plan->id == CBUILTIN_C11_ATOMIC_FETCH_ADD    ? BUSTER_TYPED_ADD
                    : plan->id == CBUILTIN_C11_ATOMIC_FETCH_SUB  ? BUSTER_TYPED_SUB
                    : plan->id == CBUILTIN_C11_ATOMIC_FETCH_AND  ? BUSTER_TYPED_AND
                    : plan->id == CBUILTIN_C11_ATOMIC_FETCH_OR   ? BUSTER_TYPED_OR
                    : plan->id == CBUILTIN_C11_ATOMIC_FETCH_NAND ? BUSTER_TYPED_AND
                                                                 : BUSTER_TYPED_XOR;
    }
    const bool complex = check_lower_complex(writer, computed);
    const bool min_max = plan->id == CBUILTIN_C11_ATOMIC_FETCH_MIN || plan->id == CBUILTIN_C11_ATOMIC_FETCH_MAX;
    const bool wide_division = check_lower_wide_class(class) && (operation == BUSTER_TYPED_DIV || operation == BUSTER_TYPED_MOD);
    if (complex)
    {
        const bool stored_complex = check_lower_complex(writer, plan->value_type);
        const TypeId element = check_lower_complex_element(writer->program->types, computed);
        const uint8_t current_class = stored_complex ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, element);
        const uint32_t left = check_lower_temporary(writer, current_class);
        check_lower_atomic_buffer_value(writer, plan, plan->expected);
        if (stored_complex)
        {
            check_lower_complex_conversion(writer, plan->value_type, computed);
        }
        else
        {
            check_lower_scalar_conversion(writer, plan->value_type, element);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, current_class, left);
        const CheckLowerComplexOperand first = {.local = left, .complex = stored_complex};
        const CheckLowerComplexOperand second = {.local = plan->arguments[1], .complex = check_lower_complex(writer, plan->operand_type)};
        const CheckOp binary = op == CHECK_OP_ADD_ASSIGN        ? CHECK_OP_ADD
                               : op == CHECK_OP_SUBTRACT_ASSIGN ? CHECK_OP_SUBTRACT
                               : op == CHECK_OP_MULTIPLY_ASSIGN ? CHECK_OP_MULTIPLY
                                                                : CHECK_OP_DIVIDE;
        const uint32_t result = check_lower_complex_compute(writer, binary, computed, first, second);
        if (stored_complex)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, plan->desired);
        }
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, result);
        check_lower_complex_conversion(writer, computed, plan->value_type);
        if (stored_complex)
        {
            check_lower_copy(writer, type_size(writer->program->types, plan->value_type), false, false);
        }
        else
        {
            check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(writer, plan->value_type), plan->desired);
        }
    }
    else if (min_max)
    {
        const uint32_t old = check_lower_label(writer);
        const uint32_t end = check_lower_label(writer);
        const bool floating = class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64;
        if (floating)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, plan->arguments[1]);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, plan->arguments[1]);
            check_lower_emit(writer, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, old);
        }
        check_lower_atomic_buffer_value(writer, plan, plan->expected);
        check_lower_conversion(writer, plan->value_type, computed);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, check_lower_class(writer, plan->operand_type), plan->arguments[1]);
        check_lower_conversion(writer, plan->operand_type, computed);
        check_lower_emit(writer, plan->id == CBUILTIN_C11_ATOMIC_FETCH_MIN ? BUSTER_TYPED_LT : BUSTER_TYPED_GT, BUSTER_TYPED_CLASS_I32, 0);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, old);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, check_lower_class(writer, plan->operand_type), plan->arguments[1]);
        check_lower_conversion(writer, plan->operand_type, computed);
        check_lower_emit(writer, BUSTER_TYPED_JUMP_VALUE, class, end);
        check_lower_place(writer, old);
        check_lower_atomic_buffer_value(writer, plan, plan->expected);
        check_lower_conversion(writer, plan->value_type, computed);
        check_lower_place(writer, end);
        check_lower_conversion(writer, computed, plan->value_type);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(writer, plan->value_type), plan->desired);
    }
    else if (wide_division)
    {
        const uint32_t left = check_lower_temporary(writer, class);
        const uint32_t result = check_lower_temporary(writer, class);
        const bool signed_value = class == BUSTER_TYPED_CLASS_I128;
        const CheckLowerRuntime runtime = operation == BUSTER_TYPED_DIV
                                              ? signed_value ? CHECK_LOWER_RUNTIME_DIVTI3 : CHECK_LOWER_RUNTIME_UDIVTI3
                                          : signed_value ? CHECK_LOWER_RUNTIME_MODTI3
                                                         : CHECK_LOWER_RUNTIME_UMODTI3;
        const TypeId arguments[2] = {computed, computed};
        const uint32_t call = check_lower_runtime_call(writer, runtime, computed, arguments, 2, result);
        check_lower_atomic_buffer_value(writer, plan, plan->expected);
        check_lower_conversion(writer, plan->value_type, computed);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, class, left);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, left);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, plan->arguments[1]);
        check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, call);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, class, result);
        check_lower_conversion(writer, computed, plan->value_type);
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(writer, plan->value_type), plan->desired);
    }
    else
    {
        if (plan->builtin && plan->value_type == BUSTER_TYPE_BOOL)
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U8, plan->expected);
        }
        else
        {
            check_lower_atomic_buffer_value(writer, plan, plan->expected);
        }
        check_lower_conversion(writer, plan->value_type, computed);
        if (increment)
        {
            uint64_t one = class == BUSTER_TYPED_CLASS_F32   ? UINT32_C(0x3f800000)
                           : class == BUSTER_TYPED_CLASS_F64 ? UINT64_C(0x3ff0000000000000)
                           : class == BUSTER_TYPED_CLASS_F80 ? check_lower_x87_constant(writer, 1.0L)
                                                             : 1;
            check_lower_emit(writer, BUSTER_TYPED_CONST, pointer ? BUSTER_TYPED_CLASS_I64 : class, one);
        }
        else
        {
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, check_lower_class(writer, plan->operand_type), plan->arguments[1]);
            if (plan->builtin && !pointer)
            {
                check_lower_conversion(writer, plan->operand_type, computed);
            }
            if ((operation == BUSTER_TYPED_SHL || operation == BUSTER_TYPED_SHR) && check_lower_wide(writer, plan->operand_type))
            {
                check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            }
        }
        if (pointer)
        {
            const uint64_t stride = check_lower_stride(writer, plan->value_type);
            if (stride != 1)
            {
                if (stride == 0)
                {
                    check_lower_vla_size(writer, type_element(writer->program->types, plan->value_type), BUSTER_TYPED_CLASS_I64);
                }
                else
                {
                    check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, stride);
                }
                check_lower_emit(writer, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_I64, 0);
            }
            if (subtract)
            {
                check_lower_emit(writer, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
            }
            check_lower_emit(writer, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
        else
        {
            check_lower_emit(writer, operation, class, 0);
            if (plan->id == CBUILTIN_C11_ATOMIC_FETCH_NAND)
            {
                check_lower_emit(writer, BUSTER_TYPED_NOT, class, 0);
            }
        }
        if (plan->builtin && plan->value_type == BUSTER_TYPE_BOOL)
        {
            check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
        }
        else
        {
            check_lower_conversion(writer, computed, plan->value_type);
        }
        check_lower_emit(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(writer, plan->value_type), plan->desired);
    }
}

static void check_lower_atomic_emit(CheckLowerWriter* writer, uint32_t index)
{
    const CheckLowerAtomic* plan = writer->atomics + index;
    const TypeTable* types = writer->program->types;
    if (plan->operation == BUSTER_CHECK_ATOMIC_LOCK_FREE)
    {
        const CheckGraph* graph = &writer->program->input->graph;
        const uint32_t argument = graph->argument_nodes[graph->argument_offsets[plan->node]];
        const CheckValue* source = writer->program->stream->values + argument;
        const bool small = source->constant_kind == CHECK_CONSTANT_INTEGER && source->fault == CHECK_FAULT_NONE && source->integer <= 8;
        if (small)
        {
            const uint64_t bytes = (uint64_t)source->integer;
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U8, bytes == 0 || (bytes & (bytes - 1)) == 0);
        }
        else
        {
            const TypeId arguments[2] = {BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG};
            const uint32_t call = check_lower_runtime_call(writer, CHECK_LOWER_RUNTIME_ATOMIC_IS_LOCK_FREE, BUSTER_TYPE_BOOL, arguments, 2,
                                                           BUSTER_CHECK_NONE);
            check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, plan->arguments[0]);
            check_lower_emit(writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
            check_lower_emit(writer, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_U8, call);
        }
    }
    else if (plan->operation == BUSTER_CHECK_ATOMIC_UPDATE)
    {
        const CheckInitPart clear = {.kind = CHECK_INIT_CLEAR, .byte_count = plan->size};
        check_lower_fill(writer, &clear, plan->desired, BUSTER_CHECK_NONE);
        CheckLowerAtomic initial = *plan;
        initial.result = plan->expected;
        initial.builtin = false;
        check_lower_atomic_call(writer, &initial, BUSTER_CHECK_ATOMIC_LOAD);
        const uint32_t retry = check_lower_label(writer);
        check_lower_place(writer, retry);
        check_lower_atomic_compute(writer, plan);
        check_lower_atomic_call(writer, plan, BUSTER_CHECK_ATOMIC_COMPARE);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, retry);
        check_lower_atomic_buffer_value(writer, plan, plan->postfix ? plan->expected : plan->desired);
    }
    else if (plan->operation == BUSTER_CHECK_ATOMIC_COMPARE)
    {
        const CheckInitPart clear = {.kind = CHECK_INIT_CLEAR, .byte_count = plan->size};
        check_lower_fill(writer, &clear, plan->expected, BUSTER_CHECK_NONE);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, plan->expected);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, plan->arguments[1]);
        check_lower_copy(writer, type_size(types, plan->value_type), false, false);
        check_lower_atomic_call(writer, plan, BUSTER_CHECK_ATOMIC_COMPARE);
        const uint32_t result = check_lower_temporary(writer, BUSTER_TYPED_CLASS_I32);
        const uint32_t end = check_lower_label(writer);
        check_lower_emit(writer, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_I32, result);
        check_lower_emit(writer, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, end);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, plan->arguments[1]);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, plan->expected);
        check_lower_copy(writer, type_size(types, plan->value_type), false, false);
        check_lower_place(writer, end);
        check_lower_emit(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, result);
        check_lower_emit(writer, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    }
    else
    {
        check_lower_atomic_call(writer, plan, plan->operation);
        if (plan->operation != BUSTER_CHECK_ATOMIC_STORE)
        {
            check_lower_atomic_buffer_value(writer, plan, plan->result);
        }
        else if (!plan->builtin && !plan->discard)
        {
            check_lower_atomic_buffer_value(writer, plan, plan->desired);
        }
    }
}

static void check_lower_atomic_store_operand(CheckLowerWriter* writer, uint32_t node, uint32_t operand, TypeId type, uint32_t buffer,
                                             uint64_t bytes)
{
    const bool aggregate = check_lower_aggregate(writer, type);
    if (aggregate)
    {
        check_lower_push(writer, (CheckLowerTask){.kind = CHECK_LOWER_COPY, .value = type_size(writer->program->types, type)});
        check_lower_operand(writer, node, operand, false, type);
        check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, buffer);
    }
    else
    {
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(writer, type), buffer);
        check_lower_operand(writer, node, operand, false, type);
    }
    check_lower_push(writer, (CheckLowerTask){.kind = CHECK_LOWER_CLEAR, .type = (TypeId)bytes, .value = buffer});
}

static void check_lower_atomic_prepare(CheckLowerWriter* writer, uint32_t node, bool builtin, bool discard)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const uint32_t target = builtin ? BUSTER_CHECK_NONE
                            : op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)
                                ? graph->operand0[node]
                                : node;
    const uint32_t selected =
        builtin ? check_program_builtin_index(stream->atomics, stream->atomic_count, sizeof(CheckBuiltinAtomicEffect), node)
                : BUSTER_CHECK_NONE;
    const CheckBuiltinAtomicEffect* effect = selected != BUSTER_CHECK_NONE ? stream->atomics + selected : NULL;
    const CBuiltinId id = builtin ? (CBuiltinId)graph->members[node] : CBUILTIN_NONE;
    const uint8_t operation = builtin
                                  ? id == CBUILTIN_C11_ATOMIC_IS_LOCK_FREE                              ? BUSTER_CHECK_ATOMIC_LOCK_FREE
                                    : id == CBUILTIN_C11_ATOMIC_LOAD                                    ? BUSTER_CHECK_ATOMIC_LOAD
                                    : id == CBUILTIN_C11_ATOMIC_STORE || id == CBUILTIN_C11_ATOMIC_INIT ? BUSTER_CHECK_ATOMIC_STORE
                                    : id == CBUILTIN_C11_ATOMIC_EXCHANGE                                ? BUSTER_CHECK_ATOMIC_EXCHANGE
                                    : id == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_STRONG || id == CBUILTIN_C11_ATOMIC_COMPARE_EXCHANGE_WEAK
                                        ? BUSTER_CHECK_ATOMIC_COMPARE
                                        : BUSTER_CHECK_ATOMIC_UPDATE
                              : op == CHECK_OP_ASSIGN ? BUSTER_CHECK_ATOMIC_STORE
                              : target == node        ? BUSTER_CHECK_ATOMIC_LOAD
                                                      : BUSTER_CHECK_ATOMIC_UPDATE;
    const TypeId object = builtin ? effect != NULL ? effect->object_type : 0 : stream->values[target].type;
    const TypeId value = object != 0 ? type_conversion_operand(types, object) : BUSTER_TYPE_VOID;
    const uint64_t size = object != 0 ? type_size(types, object) : 0;
    const uint64_t bytes = size < 16 ? 16 : size;
    const uint32_t alignment = object != 0 ? type_alignment_log2(types, object) : 3;
    const uint32_t index = writer->atomic_count++;
    CheckLowerAtomic* plan = writer->atomics + index;
    *plan = (CheckLowerAtomic){
        .node = node,
        .value_type = value,
        .object_type = object,
        .size = size,
        .id = id,
        .operation = operation,
        .builtin = builtin,
        .postfix = builtin || op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT,
        .discard = discard,
        .address = check_lower_temporary(writer, BUSTER_TYPED_CLASS_U64),
        .desired = check_lower_object(writer, bytes, alignment > 3 ? alignment : 3),
        .expected = check_lower_object(writer, bytes, alignment > 3 ? alignment : 3),
        .result = check_lower_object(writer, bytes, alignment > 3 ? alignment : 3),
    };
    const uint32_t walked = !builtin ? check_graph_atomic_address_alignment(types, graph, stream, target, NULL, true, true) : UINT32_MAX;
    const bool aligned = builtin ? effect != NULL && effect->alignment_known && effect->minimum_alignment >= size
                                 : walked < 64 && (UINT64_C(1) << walked) >= size;
    plan->native = operation != BUSTER_CHECK_ATOMIC_LOCK_FREE &&
                   check_builtin_atomic_c11_native(types, CBUILTIN_C11_ATOMIC_LOAD, value, size, aligned) &&
                   check_lower_atomic_leaf(writer,
                                           operation == BUSTER_CHECK_ATOMIC_LOAD       ? CBUILTIN_ATOMIC_LOAD_N
                                           : operation == BUSTER_CHECK_ATOMIC_STORE    ? CBUILTIN_ATOMIC_STORE_N
                                           : operation == BUSTER_CHECK_ATOMIC_EXCHANGE ? CBUILTIN_ATOMIC_EXCHANGE_N
                                                                                       : CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N,
                                           size) != NULL;
    check_lower_push(writer, (CheckLowerTask){.kind = CHECK_LOWER_ATOMIC, .node = index});
    if (builtin)
    {
        const uint32_t count = graph->argument_counts[node];
        const uint32_t first = graph->argument_offsets[node];
        for (uint32_t a = count; a-- > 0;)
        {
            const TypeId converted = stream->argument_conversions[first + a].type;
            const bool desired = (operation == BUSTER_CHECK_ATOMIC_STORE || operation == BUSTER_CHECK_ATOMIC_EXCHANGE) ? a == 1
                                 : operation == BUSTER_CHECK_ATOMIC_COMPARE                                            ? a == 2
                                                                                                                       : false;
            if (desired)
            {
                check_lower_atomic_store_operand(writer, node, a + 3, converted, plan->desired, bytes);
            }
            else
            {
                const uint32_t local = a == 0 && operation != BUSTER_CHECK_ATOMIC_LOCK_FREE
                                           ? plan->address
                                           : check_lower_temporary(writer, check_lower_class(writer, converted));
                plan->arguments[a] = local;
                if (a == 1 && operation == BUSTER_CHECK_ATOMIC_UPDATE)
                {
                    plan->operand_type = converted;
                }
                check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, check_lower_class(writer, converted), local);
                check_lower_operand(writer, node, a + 3, false, converted);
            }
        }
    }
    else
    {
        if (operation == BUSTER_CHECK_ATOMIC_STORE)
        {
            check_lower_atomic_store_operand(writer, node, 1, value, plan->desired, bytes);
        }
        else if (operation == BUSTER_CHECK_ATOMIC_UPDATE && !(op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT))
        {
            const TypeId own = stream->values[graph->operand1[node]].type;
            const TypeId converted = stream->conversions[node].operands[1].type;
            plan->operand_type = check_action_kind(types, value) == BUSTER_TYPE_KIND_POINTER ? BUSTER_TYPE_LONG
                                 : check_lower_complex(writer, stream->conversions[node].operands[0].type)
                                     ? check_lower_complex_target(writer, own, converted)
                                     : converted;
            plan->arguments[1] = check_lower_temporary(writer, check_lower_value_class(writer, plan->operand_type));
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, check_lower_value_class(writer, plan->operand_type), plan->arguments[1]);
            if (check_lower_complex(writer, plan->operand_type))
            {
                const uint32_t snapshot = check_lower_object(writer, type_size(types, plan->operand_type), type_alignment_log2(types, plan->operand_type));
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, snapshot);
                check_lower_push(writer, (CheckLowerTask){ .kind = CHECK_LOWER_COPY, .value = type_size(types, plan->operand_type),
                                                        .volatile_copy = check_lower_copy_node_volatile(writer, graph->operand1[node], plan->operand_type) });
                check_lower_operand(writer, node, 1, false, plan->operand_type);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, snapshot);
            }
            else
            {
                check_lower_operand(writer, node, 1, false, plan->operand_type);
            }
        }
        check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, plan->address);
        check_lower_push(writer, (CheckLowerTask){.kind = CHECK_LOWER_EXPRESSION, .node = target, .address = true});
    }
}
