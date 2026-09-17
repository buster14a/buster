#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


# Canonical IR and LLVM bitcode mapping for atomicrmw nand.
replace_once(
    "src/buster/lib/compiler/ir/ir.h",
    """    IR_ATOMIC_BITWISE_XOR,
    IR_ATOMIC_EXCHANGE,
    IR_ATOMIC_OPERATION_COUNT,
""",
    """    IR_ATOMIC_BITWISE_XOR,
    IR_ATOMIC_EXCHANGE,
    IR_ATOMIC_BITWISE_NAND,
    IR_ATOMIC_OPERATION_COUNT,
""",
)

replace_once(
    "src/buster/lib/compiler/llvm/bitcode.c",
    """    case IR_ATOMIC_BITWISE_XOR:
        return 6;
    case IR_ATOMIC_OPERATION_COUNT:
""",
    """    case IR_ATOMIC_BITWISE_XOR:
        return 6;
    case IR_ATOMIC_BITWISE_NAND:
        return 4;
    case IR_ATOMIC_OPERATION_COUNT:
""",
)

cgen = "src/buster/lib/compiler/frontend/c/c_gen.c"

replace_once(
    cgen,
    """    C_IR_ATOMIC_BUILTIN_FETCH_XOR,
    C_IR_ATOMIC_BUILTIN_EXCHANGE,
""",
    """    C_IR_ATOMIC_BUILTIN_FETCH_XOR,
    C_IR_ATOMIC_BUILTIN_FETCH_NAND,
    C_IR_ATOMIC_BUILTIN_EXCHANGE,
""",
)

replace_once(
    cgen,
    """    bool builtin_atomic_new_value;
    bool builtin_identity;
""",
    """    bool builtin_atomic_new_value;
    // GNU's non-`_n` forms carry value/result pointers and operate on the
    // object's byte representation rather than on a scalar expression value.
    bool builtin_atomic_generic;
    // The legacy `__sync_*` family has sequentially-consistent ordering in
    // the spelling and therefore no explicit memory-order argument.
    bool builtin_atomic_sequential;
    bool builtin_identity;
""",
)

replace_once(
    cgen,
    """    C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_DESIRED,
    C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_VALUE,
    C_IR_PREPARED_CALL_CONTINUATION_UNARY,
""",
    """    C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_DESIRED,
    C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_VALUE,
    C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_RESULT_PLACE,
    C_IR_PREPARED_CALL_CONTINUATION_UNARY,
""",
)

replace_once(
    cgen,
    """    bool gnu;
    bool new_value;
    u8 reserved[2];
};
""",
    """    bool gnu;
    bool new_value;
    bool generic;
    bool sequential;
};
""",
)

replace_once(
    cgen,
    """        bool gnu;
        bool new_value;
    } mappings[] = {
""",
    """        bool gnu;
        bool new_value;
        bool generic;
        bool sequential;
    } mappings[] = {
""",
)

replace_once(
    cgen,
    """        {S8("__atomic_load_n"), C_IR_ATOMIC_BUILTIN_LOAD, true, false},
        {S8("__atomic_store_n"), C_IR_ATOMIC_BUILTIN_STORE, true, false},
        {S8("__atomic_exchange_n"), C_IR_ATOMIC_BUILTIN_EXCHANGE, true, false},
        {S8("__atomic_fetch_add"), C_IR_ATOMIC_BUILTIN_FETCH_ADD, true, false},
""",
    """        {S8("__atomic_load_n"), C_IR_ATOMIC_BUILTIN_LOAD, true, false},
        {S8("__atomic_store_n"), C_IR_ATOMIC_BUILTIN_STORE, true, false},
        {S8("__atomic_exchange_n"), C_IR_ATOMIC_BUILTIN_EXCHANGE, true, false},
        {S8("__atomic_load"), C_IR_ATOMIC_BUILTIN_LOAD, true, false, true, false},
        {S8("__atomic_store"), C_IR_ATOMIC_BUILTIN_STORE, true, false, true, false},
        {S8("__atomic_exchange"), C_IR_ATOMIC_BUILTIN_EXCHANGE, true, false, true, false},
        {S8("__atomic_fetch_add"), C_IR_ATOMIC_BUILTIN_FETCH_ADD, true, false},
""",
)

replace_once(
    cgen,
    """        {S8("__atomic_fetch_xor"), C_IR_ATOMIC_BUILTIN_FETCH_XOR, true, false},
        {S8("__atomic_add_fetch"), C_IR_ATOMIC_BUILTIN_FETCH_ADD, true, true},
""",
    """        {S8("__atomic_fetch_xor"), C_IR_ATOMIC_BUILTIN_FETCH_XOR, true, false},
        {S8("__atomic_fetch_nand"), C_IR_ATOMIC_BUILTIN_FETCH_NAND, true, false},
        {S8("__atomic_add_fetch"), C_IR_ATOMIC_BUILTIN_FETCH_ADD, true, true},
""",
)

replace_once(
    cgen,
    """        {S8("__atomic_xor_fetch"), C_IR_ATOMIC_BUILTIN_FETCH_XOR, true, true},
        // GNU's compare-exchange takes a `weak` flag the C11 name spells in
""",
    """        {S8("__atomic_xor_fetch"), C_IR_ATOMIC_BUILTIN_FETCH_XOR, true, true},
        {S8("__atomic_nand_fetch"), C_IR_ATOMIC_BUILTIN_FETCH_NAND, true, true},
        {S8("__sync_fetch_and_nand"), C_IR_ATOMIC_BUILTIN_FETCH_NAND, true, false, false, true},
        {S8("__sync_nand_and_fetch"), C_IR_ATOMIC_BUILTIN_FETCH_NAND, true, true, false, true},
        // GNU's compare-exchange takes a `weak` flag the C11 name spells in
""",
)

replace_once(
    cgen,
    """        {S8("__atomic_compare_exchange_n"), C_IR_ATOMIC_BUILTIN_COMPARE_EXCHANGE_STRONG, true, false},
        {S8("__atomic_thread_fence"), C_IR_ATOMIC_BUILTIN_THREAD_FENCE, true, false},
""",
    """        {S8("__atomic_compare_exchange_n"), C_IR_ATOMIC_BUILTIN_COMPARE_EXCHANGE_STRONG, true, false},
        {S8("__atomic_compare_exchange"), C_IR_ATOMIC_BUILTIN_COMPARE_EXCHANGE_STRONG, true, false, true, false},
        {S8("__atomic_thread_fence"), C_IR_ATOMIC_BUILTIN_THREAD_FENCE, true, false},
""",
)

replace_once(
    cgen,
    """                .gnu = mappings[index].gnu,
                .new_value = mappings[index].new_value,
            };
""",
    """                .gnu = mappings[index].gnu,
                .new_value = mappings[index].new_value,
                .generic = mappings[index].generic,
                .sequential = mappings[index].sequential,
            };
""",
)

replace_once(
    cgen,
    """            .builtin_atomic_gnu = atomic_spelling.gnu,
            .builtin_atomic_new_value = atomic_spelling.new_value,
            .builtin_strlen = builtin_strlen,
""",
    """            .builtin_atomic_gnu = atomic_spelling.gnu,
            .builtin_atomic_new_value = atomic_spelling.new_value,
            .builtin_atomic_generic = atomic_spelling.generic,
            .builtin_atomic_sequential = atomic_spelling.sequential,
            .builtin_strlen = builtin_strlen,
""",
)

replace_once(
    cgen,
    """    IrType* object = ir_type_from_id(&builder->program->types, object_type);
    bool valid = object && object->is_atomic && bits_type.value != IR_ID_UNDERLYING_INVALID;
    IrTypeId atomic_bits = valid ? c_ir_add_qualified_type(builder->program, bits_type, true, object->is_volatile) : IR_TYPE_ID_INVALID;
""",
    """    IrType* object = ir_type_from_id(&builder->program->types, object_type);
    IrType* bits = ir_type_from_id(&builder->program->types, bits_type);
    // GNU's generic forms name an ordinary object, while C11 aggregate
    // exchange/CAS name an atomic-qualified one. In both cases the integer
    // view must cover exactly the storage the caller supplied.
    bool valid = object && bits && object->layout.resolved && bits->layout.resolved &&
                 object->layout.size == bits->layout.size && bits_type.value != IR_ID_UNDERLYING_INVALID;
    IrTypeId atomic_bits = valid ? c_ir_add_qualified_type(builder->program, bits_type, true, object->is_volatile) : IR_TYPE_ID_INVALID;
""",
)

replace_once(
    cgen,
    """            // Where the GNU family's argument list differs from the C11 one for
            // the same operation: `__atomic_compare_exchange_n` carries the
            // `weak` flag the C11 name spells, and the lock-free predicates
            // take the object's address after its size. `__atomic_test_and_set`
            // and `__atomic_clear` have no C11 counterpart at all.
            if (selected->builtin_atomic_gnu)
            {
                expected_count = selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_COMPARE_EXCHANGE_STRONG   ? 6
                                 : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_IS_LOCK_FREE            ? 2
                                 : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_TEST_AND_SET ||
                                         selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_CLEAR
                                     ? 2
                                     : expected_count;
            }
""",
    """            // GNU's `_n`, generic pointer, and legacy `__sync_*` families
            // share operations but not argument shapes.
            if (selected->builtin_atomic_generic)
            {
                expected_count = selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_LOAD ? 3
                                 : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_EXCHANGE ? 4
                                 : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_COMPARE_EXCHANGE_STRONG ? 6
                                                                                                            : 3;
            }
            else if (selected->builtin_atomic_sequential)
            {
                expected_count = 2;
            }
            else if (selected->builtin_atomic_gnu)
            {
                expected_count = selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_COMPARE_EXCHANGE_STRONG   ? 6
                                 : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_IS_LOCK_FREE            ? 2
                                 : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_TEST_AND_SET ||
                                         selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_CLEAR
                                     ? 2
                                     : expected_count;
            }
""",
)

replace_once(
    cgen,
    """            if (continuation >= C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_PLACE &&
                continuation <= C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_VALUE)
""",
    """            if (continuation >= C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_PLACE &&
                continuation <= C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_RESULT_PLACE)
""",
)

replace_once(
    cgen,
    """                if (selected->builtin_atomic_gnu && pointer.value < builder->function->value_count)
""",
    """                if (selected->builtin_atomic_gnu && !selected->builtin_atomic_generic &&
                    pointer.value < builder->function->value_count)
""",
)

replace_once(
    cgen,
    """            if (selected->builtin_atomic_gnu && atomic && !atomic->is_atomic)
""",
    """            if (selected->builtin_atomic_gnu && !selected->builtin_atomic_generic &&
                atomic && !atomic->is_atomic)
""",
)

replace_once(
    cgen,
    """            bool aggregate_value = unqualified->kind == IR_TYPE_STRUCT || unqualified->kind == IR_TYPE_UNION;
            bool pointer_value = unqualified->kind == IR_TYPE_POINTER;
            u64 atomic_width = atomic->layout.resolved ? atomic->layout.size : 0;
            bool wide_atomic_runtime = builder->target.cpu_arch == CPU_ARCH_X86_64 && atomic_width == 16 &&
""",
    """            bool aggregate_value = unqualified->kind == IR_TYPE_STRUCT || unqualified->kind == IR_TYPE_UNION;
            bool pointer_value = unqualified->kind == IR_TYPE_POINTER;
            bool representation_value = aggregate_value || selected->builtin_atomic_generic;
            u64 atomic_width = selected->builtin_atomic_generic
                                   ? (unqualified->layout.resolved ? unqualified->layout.size : 0)
                                   : (atomic->layout.resolved ? atomic->layout.size : 0);
            IrTypeId generic_bits_type = IR_TYPE_ID_INVALID;
            if (selected->builtin_atomic_generic)
            {
                if (atomic_width != 1 && atomic_width != 2 && atomic_width != 4 && atomic_width != 8)
                {
                    builder->failure_token_index = selected->token_index;
                    builder->failure_message =
                        S8("GNU generic atomic builtins require an object whose size is 1, 2, 4, or 8 bytes");
                    return false;
                }
                generic_bits_type = c_ir_unsigned_type_of_size(builder, atomic_width);
                if (generic_bits_type.value == IR_ID_UNDERLYING_INVALID)
                {
                    return false;
                }
            }
            bool wide_atomic_runtime = builder->target.cpu_arch == CPU_ARCH_X86_64 && atomic_width == 16 &&
""",
)

replace_once(
    cgen,
    """            IrMemoryOrder order = IR_MEMORY_ORDER_RELAXED;
            if (selected->builtin_atomic != C_IR_ATOMIC_BUILTIN_INIT && !c_ir_atomic_memory_order(builder, starts[order_index], ends[order_index], &order))
            {
                return false;
            }
            if (selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_COMPARE_EXCHANGE_STRONG ||
""",
    """            IrMemoryOrder order = selected->builtin_atomic_sequential ? IR_MEMORY_ORDER_SEQUENTIAL : IR_MEMORY_ORDER_RELAXED;
            if (!selected->builtin_atomic_sequential && selected->builtin_atomic != C_IR_ATOMIC_BUILTIN_INIT &&
                !c_ir_atomic_memory_order(builder, starts[order_index], ends[order_index], &order))
            {
                return false;
            }
            IrValueId generic_load_result_place = IR_VALUE_ID_INVALID;
            if (selected->builtin_atomic_generic && selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_LOAD)
            {
                if (continuation == C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_RESULT_PLACE)
                {
                    if (!child_success)
                    {
                        return C_IR_PREPARED_CALL_STEP_FAILED;
                    }
                    generic_load_result_place = c_ir_emit_dereference_place(builder, child_value, source);
                    IrTypeId result_type = generic_load_result_place.value < builder->function->value_count
                                               ? builder->function->values[generic_load_result_place.value].canonical_type
                                               : IR_TYPE_ID_INVALID;
                    if (result_type.value != value_type_id.value)
                    {
                        return false;
                    }
                    frame->as.prepared_call.state->expected_place = generic_load_result_place;
                }
                else
                {
                    return c_ir_prepared_call_request_expression(builder, frame,
                                                                 C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_RESULT_PLACE,
                                                                 starts[1], ends[1], false);
                }
            }
            if (selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_COMPARE_EXCHANGE_STRONG ||
""",
)

replace_once(
    cgen,
    """                    desired = child_value;
                }
                else
""",
    """                    desired = child_value;
                    if (selected->builtin_atomic_generic)
                    {
                        IrValueId desired_place = c_ir_emit_dereference_place(builder, desired, source);
                        IrTypeId desired_type = desired_place.value < builder->function->value_count
                                                    ? builder->function->values[desired_place.value].canonical_type
                                                    : IR_TYPE_ID_INVALID;
                        desired = desired_type.value == value_type_id.value
                                      ? c_ir_emit_load_place_raw(builder, desired_place, desired_type, source)
                                      : IR_VALUE_ID_INVALID;
                    }
                }
                else
""",
)

replace_once(
    cgen,
    """                if (aggregate_value)
                {
                    if (selected->builtin_atomic_gnu || !c_ir_atomic_aggregate_access_representable(builder, atomic_type))
                    {
                        builder->failure_message = S8("C IR lowering does not support this atomic aggregate compare-exchange width");
                        return false;
                    }
                    comparison_type = atomic_width == 16 ? c_ir_builder_scalar_type(builder, C_TYPE_UNSIGNED_INT128)
                                                         : c_ir_unsigned_type_of_size(builder, atomic_width);
""",
    """                if (representation_value)
                {
                    if (!selected->builtin_atomic_generic &&
                        (selected->builtin_atomic_gnu || !c_ir_atomic_aggregate_access_representable(builder, atomic_type)))
                    {
                        builder->failure_message = S8("C IR lowering does not support this atomic aggregate compare-exchange width");
                        return false;
                    }
                    comparison_type = selected->builtin_atomic_generic
                                          ? generic_bits_type
                                          : (atomic_width == 16 ? c_ir_builder_scalar_type(builder, C_TYPE_UNSIGNED_INT128)
                                                                : c_ir_unsigned_type_of_size(builder, atomic_width));
""",
)

# There are two observed-value conversion sites in compare-exchange.
p = Path(cgen)
text = p.read_text()
old = """                    IrValueId observed_value = aggregate_value
                                                   ? c_ir_atomic_aggregate_bits_value(builder, observed, value_type_id, comparison_type, false, source)
                                                   : c_ir_emit_cast(builder, observed, value_type_id, source);
"""
count = text.count(old)
if count != 1:
    raise SystemExit(f"{cgen}: expected one runtime observed conversion, found {count}")
text = text.replace(
    old,
    """                    IrValueId observed_value = representation_value
                                                   ? c_ir_atomic_aggregate_bits_value(builder, observed, value_type_id, comparison_type, false, source)
                                                   : c_ir_emit_cast(builder, observed, value_type_id, source);
""",
    1,
)
old = """                    IrValueId observed_value = aggregate_value
                                                   ? c_ir_atomic_aggregate_bits_value(builder, observed, value_type_id, comparison_type, false, source)
                                                   : observed;
"""
count = text.count(old)
if count != 1:
    raise SystemExit(f"{cgen}: expected one native observed conversion, found {count}")
text = text.replace(
    old,
    """                    IrValueId observed_value = representation_value
                                                   ? c_ir_atomic_aggregate_bits_value(builder, observed, value_type_id, comparison_type, false, source)
                                                   : observed;
""",
    1,
)
text = text.replace(
    """                    comparison.binary_operation = pointer_value ? IR_BINARY_POINTER_EQUAL : IR_BINARY_INTEGER_EQUAL;
""",
    """                    comparison.binary_operation =
                        pointer_value && !representation_value ? IR_BINARY_POINTER_EQUAL : IR_BINARY_INTEGER_EQUAL;
""",
    1,
)
p.write_text(text)

replace_once(
    cgen,
    """            else if (selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_LOAD)
            {
                if (order == IR_MEMORY_ORDER_RELEASE || order == IR_MEMORY_ORDER_ACQUIRE_RELEASE)
                {
                    return false;
                }
                if (wide_atomic_runtime)
""",
    """            else if (selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_LOAD)
            {
                if (order == IR_MEMORY_ORDER_RELEASE || order == IR_MEMORY_ORDER_ACQUIRE_RELEASE)
                {
                    return false;
                }
                if (selected->builtin_atomic_generic)
                {
                    IrValueId operation_place =
                        c_ir_atomic_aggregate_bits_place(builder, place, atomic_type, generic_bits_type, source);
                    IrValueId loaded = operation_place.value != IR_ID_UNDERLYING_INVALID
                                           ? c_ir_add_result(builder, generic_bits_type)
                                           : IR_VALUE_ID_INVALID;
                    if (loaded.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return false;
                    }
                    IrInstruction instruction = c_ir_instruction_initialize(IR_OPCODE_ATOMIC_LOAD, generic_bits_type);
                    instruction.operands = arena_allocate(builder->arena, IrValueId, 1);
                    instruction.operands[0] = operation_place;
                    instruction.operand_count = 1;
                    instruction.memory_order = (u8)order;
                    instruction.result = loaded;
                    IrInstructionId id = c_ir_append_instruction(builder, instruction, source);
                    builder->function->values[loaded.value].definition = id;
                    IrValueId loaded_value =
                        c_ir_atomic_aggregate_bits_value(builder, loaded, value_type_id, generic_bits_type, false, source);
                    if (loaded_value.value == IR_ID_UNDERLYING_INVALID ||
                        !c_ir_emit_store_place(builder, generic_load_result_place, value_type_id, loaded_value, source))
                    {
                        return false;
                    }
                    selected->result = c_ir_emit_integer_value(builder, 0, false, token);
                }
                else if (wide_atomic_runtime)
""",
)

replace_once(
    cgen,
    """            else
            {
                IrValueId value = IR_VALUE_ID_INVALID;
                if (continuation == C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_VALUE)
                {
                    if (!child_success)
                    {
                        return C_IR_PREPARED_CALL_STEP_FAILED;
                    }
                    value = child_value;
                }
                else
                {
                    return c_ir_prepared_call_request_expression(builder, frame, C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_VALUE, starts[1], ends[1], false);
                }
""",
    """            else
            {
                IrValueId value = IR_VALUE_ID_INVALID;
                IrValueId generic_exchange_result_place = IR_VALUE_ID_INVALID;
                if (continuation == C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_RESULT_PLACE)
                {
                    if (!child_success)
                    {
                        return C_IR_PREPARED_CALL_STEP_FAILED;
                    }
                    value = frame->as.prepared_call.state->first;
                    generic_exchange_result_place = c_ir_emit_dereference_place(builder, child_value, source);
                    IrTypeId result_type = generic_exchange_result_place.value < builder->function->value_count
                                               ? builder->function->values[generic_exchange_result_place.value].canonical_type
                                               : IR_TYPE_ID_INVALID;
                    if (!selected->builtin_atomic_generic ||
                        selected->builtin_atomic != C_IR_ATOMIC_BUILTIN_EXCHANGE ||
                        result_type.value != value_type_id.value)
                    {
                        return false;
                    }
                }
                else if (continuation == C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_VALUE)
                {
                    if (!child_success)
                    {
                        return C_IR_PREPARED_CALL_STEP_FAILED;
                    }
                    value = child_value;
                    if (selected->builtin_atomic_generic)
                    {
                        IrValueId value_place = c_ir_emit_dereference_place(builder, value, source);
                        IrTypeId input_type = value_place.value < builder->function->value_count
                                                  ? builder->function->values[value_place.value].canonical_type
                                                  : IR_TYPE_ID_INVALID;
                        value = input_type.value == value_type_id.value
                                    ? c_ir_emit_load_place_raw(builder, value_place, input_type, source)
                                    : IR_VALUE_ID_INVALID;
                        if (value.value == IR_ID_UNDERLYING_INVALID)
                        {
                            return false;
                        }
                        if (selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_EXCHANGE)
                        {
                            frame->as.prepared_call.state->first = value;
                            return c_ir_prepared_call_request_expression(
                                builder, frame, C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_RESULT_PLACE,
                                starts[2], ends[2], false);
                        }
                    }
                }
                else
                {
                    return c_ir_prepared_call_request_expression(builder, frame, C_IR_PREPARED_CALL_CONTINUATION_ATOMIC_VALUE, starts[1], ends[1], false);
                }
""",
)

replace_once(
    cgen,
    """                else
                {
                    value = c_ir_emit_cast(builder, value, value_type_id, source);
                }
""",
    """                else if (!selected->builtin_atomic_generic)
                {
                    value = c_ir_emit_cast(builder, value, value_type_id, source);
                }
""",
)

replace_once(
    cgen,
    """                    if (wide_atomic_runtime)
                    {
                        if (!c_ir_emit_wide_atomic_runtime_store(builder, place, atomic_type, value, source, order))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        IrSourceRange instruction_source = source;
                        IrInstruction instruction = c_ir_instruction_initialize(IR_OPCODE_ATOMIC_STORE, builder->void_type);
                        instruction.operands = arena_allocate(builder->arena, IrValueId, 2);
                        instruction.operands[0] = place;
                        instruction.operands[1] = value;
                        instruction.operand_count = 2;
                        instruction.memory_order = (u8)order;
                        c_ir_append_instruction(builder, instruction, instruction_source);
                    }
""",
    """                    if (selected->builtin_atomic_generic)
                    {
                        IrValueId operation_place =
                            c_ir_atomic_aggregate_bits_place(builder, place, atomic_type, generic_bits_type, source);
                        IrValueId operation_value =
                            c_ir_atomic_aggregate_bits_value(builder, value, value_type_id, generic_bits_type, true, source);
                        if (operation_place.value == IR_ID_UNDERLYING_INVALID ||
                            operation_value.value == IR_ID_UNDERLYING_INVALID)
                        {
                            return false;
                        }
                        IrInstruction instruction = c_ir_instruction_initialize(IR_OPCODE_ATOMIC_STORE, builder->void_type);
                        instruction.operands = arena_allocate(builder->arena, IrValueId, 2);
                        instruction.operands[0] = operation_place;
                        instruction.operands[1] = operation_value;
                        instruction.operand_count = 2;
                        instruction.memory_order = (u8)order;
                        c_ir_append_instruction(builder, instruction, source);
                    }
                    else if (wide_atomic_runtime)
                    {
                        if (!c_ir_emit_wide_atomic_runtime_store(builder, place, atomic_type, value, source, order))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        IrSourceRange instruction_source = source;
                        IrInstruction instruction = c_ir_instruction_initialize(IR_OPCODE_ATOMIC_STORE, builder->void_type);
                        instruction.operands = arena_allocate(builder->arena, IrValueId, 2);
                        instruction.operands[0] = place;
                        instruction.operands[1] = value;
                        instruction.operand_count = 2;
                        instruction.memory_order = (u8)order;
                        c_ir_append_instruction(builder, instruction, instruction_source);
                    }
""",
)

replace_once(
    cgen,
    """                                                  : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_XOR      ? IR_ATOMIC_BITWISE_XOR
                                                                                                                   : IR_ATOMIC_EXCHANGE;
""",
    """                                                  : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_XOR      ? IR_ATOMIC_BITWISE_XOR
                                                  : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_NAND     ? IR_ATOMIC_BITWISE_NAND
                                                                                                                   : IR_ATOMIC_EXCHANGE;
""",
)

replace_once(
    cgen,
    """                    if (aggregate_value)
                    {
                        if (selected->builtin_atomic_gnu || operation != IR_ATOMIC_EXCHANGE ||
                            !c_ir_atomic_aggregate_access_representable(builder, atomic_type))
                        {
                            builder->failure_message = S8("C IR lowering does not support this atomic aggregate read-modify-write");
                            return false;
                        }
                        operation_type = atomic_width == 16 ? c_ir_builder_scalar_type(builder, C_TYPE_UNSIGNED_INT128)
                                                           : c_ir_unsigned_type_of_size(builder, atomic_width);
""",
    """                    if (representation_value)
                    {
                        if ((selected->builtin_atomic_generic && operation != IR_ATOMIC_EXCHANGE) ||
                            (!selected->builtin_atomic_generic &&
                             (selected->builtin_atomic_gnu || operation != IR_ATOMIC_EXCHANGE ||
                              !c_ir_atomic_aggregate_access_representable(builder, atomic_type))))
                        {
                            builder->failure_message = S8("C IR lowering does not support this atomic aggregate read-modify-write");
                            return false;
                        }
                        operation_type = selected->builtin_atomic_generic
                                             ? generic_bits_type
                                             : (atomic_width == 16 ? c_ir_builder_scalar_type(builder, C_TYPE_UNSIGNED_INT128)
                                                                   : c_ir_unsigned_type_of_size(builder, atomic_width));
""",
)

replace_once(
    cgen,
    """                                               : operation == IR_ATOMIC_BITWISE_XOR ? S8("__atomic_fetch_xor_16")
                                                                                   : S8("__atomic_exchange_16");
""",
    """                                               : operation == IR_ATOMIC_BITWISE_XOR ? S8("__atomic_fetch_xor_16")
                                               : operation == IR_ATOMIC_BITWISE_NAND ? S8("__atomic_fetch_nand_16")
                                                                                      : S8("__atomic_exchange_16");
""",
)

replace_once(
    cgen,
    """                    selected->result = aggregate_value
                                           ? c_ir_atomic_aggregate_bits_value(builder, previous, value_type_id, operation_type, false, source)
                                           : c_ir_emit_cast(builder, previous, value_type_id, source);
                    if (selected->result.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return false;
                    }
                    if (selected->builtin_atomic_new_value)
""",
    """                    selected->result = representation_value
                                           ? c_ir_atomic_aggregate_bits_value(builder, previous, value_type_id, operation_type, false, source)
                                           : c_ir_emit_cast(builder, previous, value_type_id, source);
                    if (selected->result.value == IR_ID_UNDERLYING_INVALID)
                    {
                        return false;
                    }
                    if (selected->builtin_atomic_generic)
                    {
                        if (!c_ir_emit_store_place(builder, generic_exchange_result_place, value_type_id,
                                                   selected->result, source))
                        {
                            return false;
                        }
                        selected->result = c_ir_emit_integer_value(builder, 0, false, token);
                    }
                    if (selected->builtin_atomic_new_value)
""",
)

replace_once(
    cgen,
    """                        CConditionalOperator recompute = selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_ADD        ? C_CONDITIONAL_ADD
                                                         : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_SUBTRACT ? C_CONDITIONAL_SUBTRACT
                                                         : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_AND      ? C_CONDITIONAL_BITWISE_AND
                                                         : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_OR       ? C_CONDITIONAL_BITWISE_OR
                                                                                                                          : C_CONDITIONAL_BITWISE_XOR;
                        IrTypeId recompute_type = pointer_value ? builder->ptrdiff_type : value_type_id;
                        IrValueId recompute_values[2] = {
                            pointer_value ? c_ir_emit_cast(builder, previous, recompute_type, source) : previous,
                            pointer_value ? c_ir_emit_cast(builder, operand_value, recompute_type, source) : operand_value,
                        };
                        u32 recompute_count = 2;
                        if (recompute_values[0].value == IR_ID_UNDERLYING_INVALID || recompute_values[1].value == IR_ID_UNDERLYING_INVALID ||
                            !c_ir_apply_operation(builder, recompute, recompute_values, &recompute_count, source, recompute_type) || recompute_count != 1)
                        {
                            return false;
                        }
                        selected->result = c_ir_emit_cast(builder, recompute_values[0], value_type_id, source);
""",
    """                        IrTypeId recompute_type = pointer_value ? builder->ptrdiff_type : value_type_id;
                        IrValueId recompute_values[2] = {
                            pointer_value ? c_ir_emit_cast(builder, previous, recompute_type, source) : previous,
                            pointer_value ? c_ir_emit_cast(builder, operand_value, recompute_type, source) : operand_value,
                        };
                        if (recompute_values[0].value == IR_ID_UNDERLYING_INVALID ||
                            recompute_values[1].value == IR_ID_UNDERLYING_INVALID)
                        {
                            return false;
                        }
                        if (selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_NAND)
                        {
                            IrValueId combined = c_ir_emit_binary_value(builder, recompute_values[0], recompute_values[1],
                                                                        recompute_type, IR_BINARY_INTEGER_BITWISE_AND, source);
                            IrValueId inverted = combined.value != IR_ID_UNDERLYING_INVALID
                                                     ? c_ir_add_result(builder, recompute_type)
                                                     : IR_VALUE_ID_INVALID;
                            if (inverted.value == IR_ID_UNDERLYING_INVALID)
                            {
                                return false;
                            }
                            IrInstruction instruction = c_ir_instruction_initialize(IR_OPCODE_UNARY, recompute_type);
                            instruction.operands = arena_allocate(builder->arena, IrValueId, 1);
                            instruction.operands[0] = combined;
                            instruction.operand_count = 1;
                            instruction.unary_operation = IR_UNARY_INTEGER_BITWISE_NOT;
                            instruction.result = inverted;
                            IrInstructionId id = c_ir_append_instruction(builder, instruction, source);
                            builder->function->values[inverted.value].definition = id;
                            recompute_values[0] = inverted;
                        }
                        else
                        {
                            CConditionalOperator recompute =
                                selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_ADD        ? C_CONDITIONAL_ADD
                                : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_SUBTRACT ? C_CONDITIONAL_SUBTRACT
                                : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_AND      ? C_CONDITIONAL_BITWISE_AND
                                : selected->builtin_atomic == C_IR_ATOMIC_BUILTIN_FETCH_OR       ? C_CONDITIONAL_BITWISE_OR
                                                                                                  : C_CONDITIONAL_BITWISE_XOR;
                            u32 recompute_count = 2;
                            if (!c_ir_apply_operation(builder, recompute, recompute_values, &recompute_count,
                                                      source, recompute_type) ||
                                recompute_count != 1)
                            {
                                return false;
                            }
                        }
                        selected->result = c_ir_emit_cast(builder, recompute_values[0], value_type_id, source);
""",
)

# Runtime coverage for all generic widths, aggregate byte representations, and
# the four NAND spellings.
test_path = Path("tests/basic_c_gnu_atomic_builtins.c")
test = test_path.read_text()
needle = """    return 0;
}
"""
if test.count(needle) != 1:
    raise SystemExit("unexpected basic_c_gnu_atomic_builtins.c tail")
coverage = r"""    {
        unsigned char one = 1;
        unsigned char desired = 7;
        unsigned char observed = 0;
        __atomic_store(&one, &desired, __ATOMIC_RELEASE);
        __atomic_load(&one, &observed, __ATOMIC_ACQUIRE);
        if (observed != 7)
        {
            return 27;
        }
        desired = 9;
        __atomic_exchange(&one, &desired, &observed, __ATOMIC_SEQ_CST);
        if (observed != 7 || one != 9)
        {
            return 28;
        }
        desired = 11;
        observed = 9;
        if (!__atomic_compare_exchange(&one, &observed, &desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED) || one != 11)
        {
            return 29;
        }
    }
    {
        unsigned short two = 0;
        unsigned short input = 0x1234;
        unsigned short output = 0;
        __atomic_store(&two, &input, __ATOMIC_SEQ_CST);
        __atomic_load(&two, &output, __ATOMIC_SEQ_CST);
        if (output != input)
        {
            return 30;
        }
    }
    {
        struct four_bytes
        {
            unsigned char bytes[4];
        } object = {{1, 2, 3, 4}}, desired = {{5, 6, 7, 8}}, observed = {{0, 0, 0, 0}};
        __atomic_exchange(&object, &desired, &observed, __ATOMIC_SEQ_CST);
        if (observed.bytes[0] != 1 || observed.bytes[3] != 4 ||
            object.bytes[0] != 5 || object.bytes[3] != 8)
        {
            return 31;
        }
        struct four_bytes expected = {{5, 6, 7, 8}};
        struct four_bytes replacement = {{9, 10, 11, 12}};
        if (!__atomic_compare_exchange(&object, &expected, &replacement, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_RELAXED) ||
            object.bytes[0] != 9 || object.bytes[3] != 12)
        {
            return 32;
        }
    }
    {
        unsigned long long eight = 0;
        unsigned long long input = 0x1122334455667788ull;
        unsigned long long output = 0;
        __atomic_store(&eight, &input, __ATOMIC_SEQ_CST);
        __atomic_load(&eight, &output, __ATOMIC_SEQ_CST);
        if (output != input)
        {
            return 33;
        }
    }
    {
        unsigned int bits = 0xf0u;
        if (__atomic_fetch_nand(&bits, 0x3cu, __ATOMIC_SEQ_CST) != 0xf0u ||
            bits != ~(0xf0u & 0x3cu))
        {
            return 34;
        }
        bits = 0xf0u;
        if (__atomic_nand_fetch(&bits, 0x3cu, __ATOMIC_SEQ_CST) != ~(0xf0u & 0x3cu))
        {
            return 35;
        }
        bits = 0xf0u;
        if (__sync_fetch_and_nand(&bits, 0x3cu) != 0xf0u ||
            bits != ~(0xf0u & 0x3cu))
        {
            return 36;
        }
        bits = 0xf0u;
        if (__sync_nand_and_fetch(&bits, 0x3cu) != ~(0xf0u & 0x3cu))
        {
            return 37;
        }
    }
    return 0;
}
"""
test_path.write_text(test.replace(needle, coverage, 1))

# Keep the frontend ownership notes synchronized with the implementation.
docs = Path("docs/agents/frontend/atomics.md")
doc = docs.read_text()
anchor = """- **GNU scalar atomic builtins need an atomic pointer view in canonical IR.**
"""
if doc.count(anchor) != 1:
    raise SystemExit("atomics.md anchor changed")
addition = """- **GNU generic atomics use an integer view of the caller's exact object
  representation.** `__atomic_load`, `__atomic_store`, `__atomic_exchange`
  and `__atomic_compare_exchange` lower 1-, 2-, 4- and 8-byte objects through
  atomic unsigned-integer places; their value and result arguments are
  pointers and are each evaluated once through prepared-call continuations.
  Unlike `_Atomic(T)`, the view never promotes an odd-sized aggregate: widths
  other than 1/2/4/8 are diagnosed at the builtin. Compare-exchange writes the
  observed representation back through `expected` on failure. GNU NAND is one
  canonical `IR_ATOMIC_BITWISE_NAND` operation, serialized as LLVM
  `atomicrmw nand`; the `*_nand_fetch` spellings recompute `~(old & value)`
  from the returned old value, and the legacy `__sync_*` spellings carry
  sequential consistency in their names.
"""
docs.write_text(doc.replace(anchor, addition + anchor, 1))
