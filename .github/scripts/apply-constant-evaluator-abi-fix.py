from pathlib import Path


root = Path(__file__).resolve().parents[2]
path = root / "src/buster/lib/compiler/frontend/c/c_gen.c"
text = path.read_text(encoding="utf-8")

replacements = {
    "BUSTER_C_INTERNAL bool c_ir_constant_truth(CIntegerIrBuilder* builder, CIrConstantValue value);":
        "BUSTER_C_INTERNAL bool c_ir_constant_truth(CIntegerIrBuilder* builder, const CIrConstantValue* value);",
    "BUSTER_C_INTERNAL bool c_ir_constant_cast(CIntegerIrBuilder* builder, CIrConstantValue source, IrTypeId target_type, CIrConstantValue* result);":
        "BUSTER_C_INTERNAL bool c_ir_constant_cast(CIntegerIrBuilder* builder, const CIrConstantValue* source, IrTypeId target_type, CIrConstantValue* result);",
    "BUSTER_C_INTERNAL bool c_ir_constant_truth(CIntegerIrBuilder* builder, CIrConstantValue value)\n{":
        "BUSTER_C_INTERNAL bool c_ir_constant_truth(CIntegerIrBuilder* builder, const CIrConstantValue* value_input)\n{\n    CIrConstantValue value = *value_input;",
    "BUSTER_C_INTERNAL f64 c_ir_constant_integer_to_float(CIrConstantValue source, IrType* type, u32 precision)\n{":
        "BUSTER_C_INTERNAL f64 c_ir_constant_integer_to_float(const CIrConstantValue* source_input, IrType* type, u32 precision)\n{\n    CIrConstantValue source = *source_input;",
    "BUSTER_C_INTERNAL bool c_ir_constant_cast(CIntegerIrBuilder* builder, CIrConstantValue source, IrTypeId target_type, CIrConstantValue* result)\n{":
        "BUSTER_C_INTERNAL bool c_ir_constant_cast(CIntegerIrBuilder* builder, const CIrConstantValue* source_input, IrTypeId target_type, CIrConstantValue* result)\n{\n    CIrConstantValue source = *source_input;",
    "BUSTER_C_INTERNAL bool c_ir_constant_apply_binary(CIntegerIrBuilder* builder, CConditionalOperator operation, CIrConstantValue left,\n                                                     CIrConstantValue right, CIrConstantValue* result)\n{":
        "BUSTER_C_INTERNAL bool c_ir_constant_apply_binary(CIntegerIrBuilder* builder, CConditionalOperator operation, const CIrConstantValue* left_input,\n                                                     const CIrConstantValue* right_input, CIrConstantValue* result)\n{\n    CIrConstantValue left = *left_input;\n    CIrConstantValue right = *right_input;",

    "c_ir_constant_cast(builder, current->low_constant, switched_type_id, &converted)":
        "c_ir_constant_cast(builder, &current->low_constant, switched_type_id, &converted)",
    "c_ir_constant_cast(builder, current->high_constant, switched_type_id, &converted)":
        "c_ir_constant_cast(builder, &current->high_constant, switched_type_id, &converted)",
    "c_ir_constant_truth(builder, assertion)":
        "c_ir_constant_truth(builder, &assertion)",
    "c_ir_constant_cast(builder, evaluated, task.type, &converted)":
        "c_ir_constant_cast(builder, &evaluated, task.type, &converted)",
    "c_ir_constant_cast(builder, value, child_type, &converted)":
        "c_ir_constant_cast(builder, &value, child_type, &converted)",
    "c_ir_constant_truth(builder, source)":
        "c_ir_constant_truth(builder, &source)",
    "c_ir_constant_integer_to_float(source, source_type, target->bit_width == 32 ? 24 : 53)":
        "c_ir_constant_integer_to_float(&source, source_type, target->bit_width == 32 ? 24 : 53)",
    "c_ir_constant_cast(builder, *value, cast_type, &cast)":
        "c_ir_constant_cast(builder, value, cast_type, &cast)",
    "c_ir_constant_truth(builder, *value)":
        "c_ir_constant_truth(builder, value)",
    "c_ir_constant_cast(builder, *value, promoted, value)":
        "c_ir_constant_cast(builder, value, promoted, value)",
    "c_ir_constant_truth(builder, left)":
        "c_ir_constant_truth(builder, &left)",
    "c_ir_constant_truth(builder, right)":
        "c_ir_constant_truth(builder, &right)",
    "c_ir_constant_cast(builder, left, common, &left)":
        "c_ir_constant_cast(builder, &left, common, &left)",
    "c_ir_constant_cast(builder, right, promoted_right_type, &right)":
        "c_ir_constant_cast(builder, &right, promoted_right_type, &right)",
    "c_ir_constant_truth(builder, condition)":
        "c_ir_constant_truth(builder, &condition)",
    "c_ir_constant_apply_binary(builder, operation.operation, left, right, &result)":
        "c_ir_constant_apply_binary(builder, operation.operation, &left, &right, &result)",
    "c_ir_constant_cast(builder, body, literal_type, &converted_literal)":
        "c_ir_constant_cast(builder, &body, literal_type, &converted_literal)",
    "c_ir_constant_cast(builder, value, global->type, &converted)":
        "c_ir_constant_cast(builder, &value, global->type, &converted)",
    "c_ir_constant_cast(builder, value, builder->f64_type, &unrounded)":
        "c_ir_constant_cast(builder, &value, builder->f64_type, &unrounded)",
    "c_ir_constant_truth(&constant_builder, assertion)":
        "c_ir_constant_truth(&constant_builder, &assertion)",
}

for old, new in replacements.items():
    count = text.count(old)
    if count == 0:
        raise SystemExit(f"missing expected source fragment: {old!r}")
    text = text.replace(old, new)

path.write_text(text, encoding="utf-8")
