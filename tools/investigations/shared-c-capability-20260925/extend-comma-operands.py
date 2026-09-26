#!/usr/bin/env python3
"""Guarded follow-up on this investigation's exact source; no external edits."""
import hashlib
import pathlib
import sys

path = pathlib.Path(sys.argv[1]) / 'src/buster/lib/compiler/frontend/c/c_gen.c'
if hashlib.sha256(path.read_bytes()).hexdigest() != '967a4e298628a1b5235a7890f11db19dfbe4e2ece20121636a33207df5c8baee':
    raise RuntimeError('unexpected production source')
text = path.read_text()

def replace_once(old, new):
    global text
    if text.count(old) != 1:
        raise RuntimeError('ambiguous or stale anchor: ' + old)
    text = text.replace(old, new, 1)

if text.count('c_ir_condition_has_root_comma') != 2:
    raise RuntimeError('unexpected root-comma helper references')
text = text.replace('c_ir_condition_has_root_comma', 'c_ir_has_root_comma')
replace_once('BUSTER_C_INTERNAL bool c_ir_group_is_statement_expression(CIntegerIrBuilder* builder, u32 start, u32 end);',
             'BUSTER_C_INTERNAL bool c_ir_group_is_statement_expression(CIntegerIrBuilder* builder, u32 start, u32 end);\n\n'
             'BUSTER_C_INTERNAL bool c_ir_has_root_comma(CIntegerIrBuilder* builder, u32 start, u32 end);')
replace_once('            // A control/assignment expression can be nested as an operand\n',
             '            // A control/assignment/comma expression can be nested as an operand\n')
replace_once('            if (close < end && (c_ir_has_root_control_operator(builder, index + 1, close) ||\n'
             '                                c_ir_has_assignment_anywhere(builder, index + 1, close)))',
             '            if (close < end && (c_ir_has_root_control_operator(builder, index + 1, close) ||\n'
             '                                c_ir_has_assignment_anywhere(builder, index + 1, close) ||\n'
             '                                c_ir_has_root_comma(builder, index + 1, close)))')
replace_once('// A controlling expression still obeys comma precedence. Only commas outside\n',
             '// Conditions and nested operands still obey comma precedence. Only commas outside\n')
path.write_text(text)
