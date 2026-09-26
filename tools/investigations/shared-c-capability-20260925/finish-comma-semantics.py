#!/usr/bin/env python3
"""Exact-source continuation of #1354. No external input modification."""
import hashlib
import pathlib
import sys

path = pathlib.Path(sys.argv[1]) / 'src/buster/lib/compiler/frontend/c/c_gen.c'
if hashlib.sha256(path.read_bytes()).hexdigest() != '8de5e7afc0a813ddaa5c7e37ebc893729f45d836aa148fd851ab7a03977c663e':
    raise RuntimeError('unexpected production source')
text = path.read_text()

def replace_once(old, new):
    global text
    if text.count(old) != 1:
        raise RuntimeError('ambiguous or stale anchor: ' + old[:150])
    text = text.replace(old, new, 1)

helper = r'''// A comma yields its right operand's value, not that operand's lvalue.
// Keep dynamic array extents, but remove both the array-lvalue witness and
// recoverable scalar-load provenance. The existing canonical identity cast
// supplies a value boundary only when place recovery would otherwise be
// possible; constants and computed values need no extra instruction.
BUSTER_C_INTERNAL IrValueId c_ir_comma_result(CIntegerIrBuilder* builder, IrValueId value, IrSourceRange source)
{
    if (value.value >= builder->function->value_count)
    {
        return value;
    }
    value = c_ir_vla_pointer_rvalue(builder, value, source);
    value = c_ir_decay_array_value_if_needed(builder, value, source);
    if (value.value >= builder->function->value_count)
    {
        return IR_VALUE_ID_INVALID;
    }
    if (builder->function->values[value.value].category == IR_VALUE_PLACE)
    {
        IrTypeId type = builder->function->values[value.value].canonical_type;
        value = c_ir_emit_load_place(builder, value, type, source);
    }
    if (value.value >= builder->function->value_count)
    {
        return IR_VALUE_ID_INVALID;
    }
    IrValue* operand = builder->function->values + value.value;
    bool recoverable = c_ir_ssa_read_place(builder, value, false).value != IR_ID_UNDERLYING_INVALID;
    if (operand->definition.value < builder->function->instruction_count)
    {
        IrInstruction* definition = builder->function->instructions + operand->definition.value;
        IrType* type = ir_type_from_id(&builder->program->types, operand->canonical_type);
        IrType* element = type && type->kind == IR_TYPE_POINTER ? ir_type_from_id(&builder->program->types, type->element_type) : 0;
        recoverable = recoverable || definition->opcode == IR_OPCODE_LOAD || definition->opcode == IR_OPCODE_ATOMIC_LOAD ||
                      definition->opcode == IR_OPCODE_FUNCTION ||
                      (definition->opcode == IR_OPCODE_ADDRESS_OF && element && element->kind == IR_TYPE_VA_LIST);
    }
    if (recoverable)
    {
        CIrVlaValue shape = c_ir_vla_value(builder, value);
        value = c_ir_emit_cast_instruction(builder, value, operand->canonical_type, IR_CONVERSION_IDENTITY, source);
        if (shape.counts && value.value != IR_ID_UNDERLYING_INVALID)
        {
            c_ir_vla_value_set(builder, value, shape);
        }
    }
    return value;
}

'''
replace_once('BUSTER_C_INTERNAL void c_ir_vla_conditional_shape(CIntegerIrBuilder* builder, IrValueId place, IrValueId value)\n', helper + 'BUSTER_C_INTERNAL void c_ir_vla_conditional_shape(CIntegerIrBuilder* builder, IrValueId place, IrValueId value)\n')
replace_once('    C_IR_LOWER_FRAME_COMMA_COMPLETION,\n', '    C_IR_LOWER_FRAME_COMMA_COMPLETION,\n    C_IR_LOWER_FRAME_COMMA_RESULT,\n')
replace_once('        values[*value_count - 2] = c_ir_vla_pointer_rvalue(builder, values[*value_count - 1], source);', '        values[*value_count - 2] = c_ir_comma_result(builder, values[*value_count - 1], source);')
replace_once('            bool assignment_leaf = c_ir_has_assignment_anywhere(builder, leaf_start, leaf_end);\n',
             '            // A comma arm also owns sequencing, even without an assignment.\n'
             '            bool assignment_leaf = c_ir_has_assignment_anywhere(builder, leaf_start, leaf_end) ||\n'
             '                                   c_ir_has_root_comma(builder, leaf_start, leaf_end);\n')
replace_once('            if (task.kind == C_IR_LOWER_FRAME_COMMA_COMPLETION)\n            {\n                frame->as.expression.start = task.as.range.start;',
             '            if (task.kind == C_IR_LOWER_FRAME_COMMA_RESULT)\n'
             '            {\n'
             '                IrSourceRange source = c_ir_token_source_range(builder, builder->preprocess.tokens[task.as.range.start]);\n'
             '                IrValueId result = c_ir_comma_result(builder, value, source);\n'
             '                if (value.value != IR_ID_UNDERLYING_INVALID && result.value == IR_ID_UNDERLYING_INVALID)\n'
             '                {\n'
             '                    c_ir_lower_frame_finish(builder, false, IR_VALUE_ID_INVALID);\n'
             '                    return;\n'
             '                }\n'
             '                frame->as.expression.value = result;\n'
             '                continue;\n'
             '            }\n'
             '            if (task.kind == C_IR_LOWER_FRAME_COMMA_COMPLETION)\n'
             '            {\n'
             '                // Resume the right operand only after the left completes, then\n'
             '                // form the same non-lvalue result as the arithmetic comma path.\n'
             '                task.kind = C_IR_LOWER_FRAME_COMMA_RESULT;\n'
             '                if (!c_ir_expression_task_push(builder, frame, task, task.as.range.start))\n'
             '                {\n'
             '                    c_ir_lower_frame_finish(builder, false, IR_VALUE_ID_INVALID);\n'
             '                    return;\n'
             '                }\n'
             '                frame->as.expression.start = task.as.range.start;')
path.write_text(text)

path = pathlib.Path(sys.argv[1]) / 'src/buster/tests/compiler/frontend/c/c_test.c'
if hashlib.sha256(path.read_bytes()).hexdigest() != '13e90ed239dba69b712fc89d30251c91a7982f3767f0c2e30910bc98d9431eb0':
    raise RuntimeError('unexpected regression source')
text = path.read_text()
fixture = r'''// A comma is never an lvalue, even when its final operand was a place.
// Keep these in the registered suite alongside the VLA-row negative cases.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_comma_result_constraints(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 invalid_sources[] = {
        S8("int test(void) { int a = 0, b = 0; (a, b) = 1; return b; }"),
        S8("int test(void) { int a = 0, b = 0; return ((a, b) = 1); }"),
        S8("int test(void) { int a = 0, b = 0; (a, b) += 1; return b; }"),
        S8("int *test(int *p) { return &(0, *p); }"),
        S8("int test(int *p) { return (0, *p)++; }"),
        S8("int test(int *p) { return ++(0, *p); }"),
        S8("void *test(int n, char rows[][n]) { return &(0, rows[1]); }"),
        S8("int test(void) { int a[3]; return sizeof &(0, a); }"),
        S8("struct S { int x; }; struct S *test(struct S *p) { return &(0, *p); }"),
        S8("static void side(void) {} int test(void) { if (1, side()) return 1; return 0; }"),
        S8("struct S { int x; }; int test(void) { while (1, (struct S){0}) {} return 0; }"),
    };
    for (u32 form = 0; form < 2; form += 1)
    {
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_sources); index += 1)
        {
            Arena* conflicts[] = {arguments->arena};
            TemporalArena temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
            CPreprocessResult tokens = c_preprocess(temporary.arena, invalid_sources[index],
                (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native), .dialect = C_PREPROCESS_DIALECT_GNU17});
            CParseResult parsed = c_parse(temporary.arena, tokens);
            BUSTER_TEST(arguments, !tokens.diagnostic_count && !parsed.diagnostic_count);
            CIRLowerResult lowered = c_lower_to_ir_with_options(temporary.arena, S8("comma-constraint.c"), tokens, parsed, target_native,
                (CIRLowerOptions){.disable_direct_ssa = form != 0});
            BUSTER_TEST_RAW(arguments, lowered.diagnostic_count != 0, invalid_sources[index]);
            scratch_end(temporary);
        }
    }
    return result;
}

'''
replace_once('UnitTestResult c_frontend_tests(UnitTestArguments* arguments)\n', fixture + 'UnitTestResult c_frontend_tests(UnitTestArguments* arguments)\n')
replace_once('    BUSTER_TEST_FIXTURE(arguments, c_test_comma_condition_evaluation);', '    BUSTER_TEST_FIXTURE(arguments, c_test_comma_condition_evaluation);\n    BUSTER_TEST_FIXTURE(arguments, c_test_comma_result_constraints);')
path.write_text(text)
