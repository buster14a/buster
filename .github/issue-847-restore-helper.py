#!/usr/bin/env python3
from pathlib import Path

path = Path("src/buster/tests/compiler/codegen/codegen_test.c")
text = path.read_text()
marker = "BUSTER_GLOBAL_LOCAL u32 codegen_test_canonical_descriptor_stack_size(CodegenFunctionDescriptor* descriptor)"
helper = """BUSTER_GLOBAL_LOCAL u32 codegen_test_canonical_value_frame_size(IrProgram* program, IrFunction* function)
{
    u64 value_bytes = 0;
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        IrType* type = ir_type_from_id(&program->types, function->values[value_index].canonical_type);
        IrInstructionId definition = function->values[value_index].definition;
        bool global_place = definition.value < function->instruction_count && function->instructions[definition.value].opcode == IR_OPCODE_GLOBAL;
        u64 slot_size = global_place ? 8 : (type->layout.size + 7) & ~(u64)7;
        slot_size = BUSTER_MAX(slot_size, 8u);
        u64 alignment = global_place ? 8 : BUSTER_MAX(BUSTER_MAX(type->layout.alignment, function->values[value_index].alignment), 8u);
        value_bytes += slot_size;
        u64 remainder = value_bytes % alignment;
        if (remainder)
        {
            value_bytes += alignment - remainder;
        }
    }
    return (u32)((value_bytes + 15) & ~(u64)15);
}

"""

if helper in text:
    raise SystemExit("shared frame-size helper was not removed")
if text.count(marker) != 1:
    raise SystemExit(f"expected one descriptor helper marker, found {text.count(marker)}")
path.write_text(text.replace(marker, helper + marker, 1))
