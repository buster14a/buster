from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
def replace(old, new):
    global s
    if s.count(old) != 1:
        raise RuntimeError('anchor count ' + str(s.count(old)) + ': ' + old[:120])
    s = s.replace(old, new)
replace('    C_IR_LOWER_STAGE_EXPRESSION_CORE_STATEMENT,\n', '    C_IR_LOWER_STAGE_EXPRESSION_CORE_STATEMENT,\n    C_IR_LOWER_STAGE_EXPRESSION_CORE_SIZEOF_VLA,\n')
helper = '''// This shortcut starts from a completed local VLA declaration, so an
// unmapped array suffix is variable-sized rather than an incomplete object.
// Inspect the operand's suffix, not the containing local: a[n][3] is a VLA,
// but a[index++] has fixed array type and sizeof must not evaluate its index.
BUSTER_C_INTERNAL bool c_ir_sizeof_vla_suffix_evaluated(CIntegerIrBuilder* builder, CEntityId entity, u32 suffix)
{
    CTypeId type = entity.value < builder->parse.entity_count ? builder->parse.entities[entity.value].type : C_TYPE_ID_INVALID;
    CType* current = c_type_from_id(&builder->parse, type);
    while (suffix && current && (current->kind == C_TYPE_ARRAY || current->kind == C_TYPE_POINTER))
    {
        type = current->element_type;
        current = c_type_from_id(&builder->parse, type);
        suffix -= 1;
    }
    bool result = !suffix && current && current->kind == C_TYPE_ARRAY &&
                  builder->c_type_ir_map[type.value].value == IR_ID_UNDERLYING_INVALID;
    return result;
}

'''
replace('BUSTER_C_INTERNAL void c_ir_lower_expression_core_step(CIntegerIrBuilder* builder)\n{', helper + 'BUSTER_C_INTERNAL void c_ir_lower_expression_core_step(CIntegerIrBuilder* builder)\n{')
replace('    u32 operation_count = 0;\n    u32 index;\n    bool expect_operand;', '    u32 operation_count = 0;\n    u32 index;\n    bool expect_operand;\n    bool yielded_sizeof = false;')
replace('    if (frame->stage == C_IR_LOWER_STAGE_EXPRESSION_CORE_STATEMENT)\n    {', '    if (frame->stage == C_IR_LOWER_STAGE_EXPRESSION_CORE_STATEMENT ||\n        frame->stage == C_IR_LOWER_STAGE_EXPRESSION_CORE_SIZEOF_VLA)\n    {')
replace('        index = state->index;\n        values[value_count++] = machine->child_result.value;\n        expect_operand = false;', '''        index = state->index;
        if (frame->stage == C_IR_LOWER_STAGE_EXPRESSION_CORE_STATEMENT)
        {
            values[value_count++] = machine->child_result.value;
        }
        // A VLA sizeof already saved its declaration-time size on the value
        // stack. Its child supplies required effects, not a replacement size.
        expect_operand = false;''')
replace('''            // A dereference of a pointer-to-VLA names the same array object
            // as subscript zero. Use its saved size, including through groups,
            // without reading the object or reevaluating declaration bounds.''', '''            // A dereference of a pointer-to-VLA names the same array object
            // as subscript zero. Retain its declaration-time size, including
            // through groups, but evaluate a variable-sized operand once.
            // Evaluation forms the row address; it does not read array data
            // or reevaluate declaration bounds.''')
replace('''                        values[value_count++] = runtime_size;
                        expect_operand = false;
                        index = consumed_index;
                        continue;''', '''                        values[value_count++] = runtime_size;
                        expect_operand = false;
                        if (c_ir_sizeof_vla_suffix_evaluated(builder, size_entity, suffix_index))
                        {
                            c_ir_expression_core_save(frame, values, operations, operation_sources, operation_cast_types,
                                                      value_count, operation_count, consumed_index + 1, false);
                            frame->stage = (u8)C_IR_LOWER_STAGE_EXPRESSION_CORE_SIZEOF_VLA;
                            if (!c_ir_lower_frame_push(builder, (CIrLowerFrame){
                                    .kind = C_IR_LOWER_FRAME_EXPRESSION,
                                    .as.expression = {.start = operand_start, .end = operand_end},
                                }))
                            {
                                c_ir_lower_frame_finish(builder, false, IR_VALUE_ID_INVALID);
                            }
                            yielded_sizeof = true;
                            break;
                        }
                        index = consumed_index;
                        continue;''')
old = '''    if (expect_operand)
    {
        c_ir_lower_frame_finish(builder, false, IR_VALUE_ID_INVALID);
        return;
    }
    while (operation_count)
    {
        operation_count -= 1;
        if (operations[operation_count] == C_CONDITIONAL_OPEN || operations[operation_count] == C_CONDITIONAL_INDEX_OPEN ||
            !c_ir_apply_operation(builder, operations[operation_count], values, &value_count, operation_sources[operation_count],
                                  operation_cast_types[operation_count]))
        {
            c_ir_lower_frame_finish(builder, false, IR_VALUE_ID_INVALID);
            return;
        }
    }
    if (value_count != 1)
    {
        c_ir_lower_frame_finish(builder, false, IR_VALUE_ID_INVALID);
        return;
    }
    c_ir_lower_frame_finish(builder, true, values[0]);
}'''
new = '''    if (!yielded_sizeof)
    {
        bool complete = !expect_operand;
        while (complete && operation_count)
        {
            operation_count -= 1;
            complete = operations[operation_count] != C_CONDITIONAL_OPEN && operations[operation_count] != C_CONDITIONAL_INDEX_OPEN &&
                       c_ir_apply_operation(builder, operations[operation_count], values, &value_count, operation_sources[operation_count],
                                            operation_cast_types[operation_count]);
        }
        complete = complete && value_count == 1;
        c_ir_lower_frame_finish(builder, complete, complete ? values[0] : IR_VALUE_ID_INVALID);
    }
}'''
replace(old, new)
p.write_text(s)
