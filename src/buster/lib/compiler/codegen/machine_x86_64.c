// x86-64 machine selection, MIR_STACK placement, and encoding. Included by
// machine.c in the backend-implementation-file pattern; not a standalone
// translation unit. The stage-2 subset covers scalar integer functions:
// arguments/constants/casts/unary/binary arithmetic and comparisons, direct
// locals and pointer dereference, branches, and scalar returns. Everything
// else is an explicit unsupported result, never a silent misselection.

#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

// Supported argument-list length: six register slots plus stack parts.
#define MACHINE_X64_MAX_ARGUMENTS 16

// The register shape of one argument or result under the subset: scalar
// values occupy one integer register; small integer-class aggregates occupy
// one or two, with the part offsets taken from the IR-owned System V
// classification. Anything else (floats, memory or indirect classes) is
// outside the subset.
typedef struct MachineX64ValueShape MachineX64ValueShape;
struct MachineX64ValueShape
{
    u32 part_offsets[2];
    u8 part_is_float[2];
    u32 part_count;
    u32 byte_size;
    bool aggregate;
    // Memory-class argument: passed by value in outgoing stack eightbytes.
    bool force_stack;
    // Indirect result: returned through a hidden pointer in RDI.
    bool indirect;
    u8 reserved;
};

// One argument's placement after running register assignment: either its
// shape's parts in consecutive per-class registers, or the whole value in
// consecutive eightbytes of the outgoing stack area.
typedef struct MachineX64ArgumentPlacement MachineX64ArgumentPlacement;
struct MachineX64ArgumentPlacement
{
    u16 first_integer;
    u16 first_float;
    u16 first_stack_part;
    u16 stack_part_count;
    bool on_stack;
    u8 reserved[3];
};

typedef struct MachineX64Selector MachineX64Selector;
struct MachineX64Selector
{
    Arena* arena;
    IrProgram* program;
    IrFunction* function;
    MachineFunctionBuilder builder;
    MachineBuilderStream immediates;
    MachineBuilderStream stack_slots;
    MachineBuilderStream call_targets;
    MachineBuilderStream switch_cases;
    MachineBuilderStream stack_slot_alignments;
    // Per IrValue: virtual register index, stack slot index, or UINT32_MAX.
    u32* value_virtual_registers;
    u32* value_stack_slots;
    // Result value per argument index, captured at entry before any scratch
    // register can clobber the incoming fixed registers; IR_ID_UNDERLYING_INVALID
    // when the function has no such argument.
    u32 argument_values[MACHINE_X64_MAX_ARGUMENTS];
    // Register shape and placement per parameter, plus the return shape,
    // computed once from the IR-owned ABI classification.
    MachineX64ValueShape parameter_shapes[MACHINE_X64_MAX_ARGUMENTS];
    MachineX64ArgumentPlacement parameter_placements[MACHINE_X64_MAX_ARGUMENTS];
    MachineX64ValueShape return_shape;
    // Frame slot holding the incoming hidden result pointer, or UINT32_MAX.
    u32 hidden_return_slot;
    // Definition point per virtual register, patched into the flattened
    // rows because builder chunks are write-once.
    u32* virtual_register_definitions;
    u32 virtual_register_count;
    IrOpcode failed_opcode;
    bool supported;
};

BUSTER_GLOBAL_LOCAL u8 const machine_x64_system_v_arguments[6] = {
    MACHINE_X64_RDI, MACHINE_X64_RSI, MACHINE_X64_RDX, MACHINE_X64_RCX, MACHINE_X64_R8, MACHINE_X64_R9,
};

// The fixed scratch register per inline operand slot in MIR_STACK placement.
BUSTER_GLOBAL_LOCAL u8 const machine_x64_slot_scratch[4] = {
    MACHINE_X64_RAX, MACHINE_X64_RCX, MACHINE_X64_RDX, MACHINE_X64_RSI,
};

BUSTER_GLOBAL_LOCAL bool machine_x64_type_is_scalar_register(IrType* type)
{
    if (!type || !type->layout.resolved || type->layout.size > 8)
    {
        return false;
    }
    return type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_ENUM;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_value_shape(IrProgram* program, IrTypeId type_id, IrAbiUse use, MachineX64ValueShape* shape)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (machine_x64_type_is_scalar_register(type))
    {
        *shape = (MachineX64ValueShape){
            .part_count = 1,
            .byte_size = 8,
        };
        return true;
    }
    if (type && type->layout.resolved && type->kind == IR_TYPE_FLOAT && (type->bit_width == 32 || type->bit_width == 64))
    {
        *shape = (MachineX64ValueShape){
            .part_is_float = {1},
            .part_count = 1,
            .byte_size = 8,
        };
        return true;
    }
    if (!type || !type->layout.resolved || (type->kind != IR_TYPE_STRUCT && type->kind != IR_TYPE_UNION && type->kind != IR_TYPE_SLICE))
    {
        return false;
    }
    IrAbiValue abi = ir_type_abi_value(program, type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, use);
    if (abi.indirect || abi.memory || !abi.part_count || abi.part_count > 2)
    {
        if (type->layout.size > UINT32_MAX - 7)
        {
            return false;
        }
        if (use == IR_ABI_USE_RESULT && (abi.indirect || abi.memory))
        {
            // Large results return through a hidden pointer.
            *shape = (MachineX64ValueShape){
                .byte_size = (u32)((type->layout.size + 7) & ~(u64)7),
                .aggregate = true,
                .indirect = true,
            };
            return true;
        }
        if (use != IR_ABI_USE_RESULT && abi.memory && !abi.indirect)
        {
            // Memory-class arguments pass by value on the stack.
            *shape = (MachineX64ValueShape){
                .byte_size = (u32)((type->layout.size + 7) & ~(u64)7),
                .aggregate = true,
                .force_stack = true,
            };
            return true;
        }
        return false;
    }
    if (type->layout.size > UINT32_MAX - 7)
    {
        return false;
    }
    MachineX64ValueShape built = {
        .part_count = abi.part_count,
        .byte_size = (u32)((type->layout.size + 7) & ~(u64)7),
        .aggregate = true,
    };
    for (u32 part_index = 0; part_index < abi.part_count; part_index += 1)
    {
        bool part_float = abi.parts[part_index].abi_class == IR_ABI_CLASS_FLOAT;
        if ((abi.parts[part_index].abi_class != IR_ABI_CLASS_INTEGER && abi.parts[part_index].abi_class != IR_ABI_CLASS_POINTER && !part_float) ||
            abi.parts[part_index].size > 8)
        {
            return false;
        }
        built.part_offsets[part_index] = abi.parts[part_index].value_offset;
        built.part_is_float[part_index] = part_float ? 1 : 0;
    }
    *shape = built;
    return true;
}

// Consecutive-register assignment per class: integer parts take the next
// System V general register, float parts the next XMM register.
BUSTER_GLOBAL_LOCAL u32 machine_x64_shape_class_parts(MachineX64ValueShape* shape, bool floats)
{
    u32 count = 0;
    for (u32 part_index = 0; part_index < shape->part_count; part_index += 1)
    {
        count += (shape->part_is_float[part_index] != 0) == floats;
    }
    return count;
}

// Assigns one argument to registers when its whole shape fits the remaining
// per-class sequences, otherwise to consecutive outgoing stack eightbytes —
// the canonical all-or-nothing rule.
BUSTER_GLOBAL_LOCAL void machine_x64_place_argument(MachineX64ValueShape* shape, u32* integer_count, u32* float_count, u32* stack_part_count,
                                                    MachineX64ArgumentPlacement* placement)
{
    u32 integer_parts = machine_x64_shape_class_parts(shape, false);
    u32 float_parts = machine_x64_shape_class_parts(shape, true);
    if (shape->force_stack || *integer_count + integer_parts > BUSTER_ARRAY_LENGTH(machine_x64_system_v_arguments) || *float_count + float_parts > 8)
    {
        *placement = (MachineX64ArgumentPlacement){
            .first_stack_part = (u16)*stack_part_count,
            .stack_part_count = (u16)(shape->byte_size / 8),
            .on_stack = true,
        };
        *stack_part_count += shape->byte_size / 8;
        return;
    }
    *placement = (MachineX64ArgumentPlacement){
        .first_integer = (u16)*integer_count,
        .first_float = (u16)*float_count,
    };
    *integer_count += integer_parts;
    *float_count += float_parts;
}

// Mirrors codegen_canonical_register_is_64_bit for the integer subset.
BUSTER_GLOBAL_LOCAL bool machine_x64_type_is_64_bit(IrProgram* program, IrTypeId type_id)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    return type && (type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION || (type->kind == IR_TYPE_INTEGER && type->bit_width > 32));
}

BUSTER_GLOBAL_LOCAL u32 machine_x64_scalar_bit_width(IrType* type)
{
    if (!type)
    {
        return 0;
    }
    switch (type->kind)
    {
        break;
    case IR_TYPE_BOOLEAN:
        return 8;
        break;
    case IR_TYPE_INTEGER:
        return type->bit_width;
        break;
    case IR_TYPE_ENUM:
        return 32;
        break;
    case IR_TYPE_POINTER:
    case IR_TYPE_FUNCTION:
        return 64;
        break;
    default:
        return 0;
    }
}

// Address-producing place definitions: their machine vreg holds an 8-byte
// address regardless of the value's declared canonical type, exactly like
// the canonical path stores an address in the value's slot.
BUSTER_GLOBAL_LOCAL bool machine_x64_opcode_produces_address(IrOpcode opcode)
{
    return opcode == IR_OPCODE_GLOBAL || opcode == IR_OPCODE_INDEX || opcode == IR_OPCODE_FIELD || opcode == IR_OPCODE_DEREFERENCE ||
           opcode == IR_OPCODE_ADDRESS_OF || opcode == IR_OPCODE_FUNCTION;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_operand_register(MachineX64Selector* selector, IrValueId value, u32* register_out)
{
    if (value.value >= selector->function->value_count || selector->value_virtual_registers[value.value] == UINT32_MAX)
    {
        return false;
    }
    *register_out = selector->value_virtual_registers[value.value];
    return true;
}

BUSTER_GLOBAL_LOCAL u32 machine_x64_select_row(MachineX64Selector* selector, MachineInstruction instruction)
{
    return machine_builder_instruction(&selector->builder, instruction);
}

BUSTER_GLOBAL_LOCAL void machine_x64_define(MachineX64Selector* selector, u32 virtual_register, u32 machine_index)
{
    // Selection-synthesized vregs sit past the classification count and
    // carry their definition point from creation.
    if (virtual_register < selector->virtual_register_count && selector->virtual_register_definitions[virtual_register] == MACHINE_POINT_INVALID)
    {
        selector->virtual_register_definitions[virtual_register] = machine_point_make(machine_index, MACHINE_POINT_AFTER);
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_reject(MachineX64Selector* selector, IrOpcode opcode)
{
    selector->supported = false;
    selector->failed_opcode = opcode;
}

// Emits the row computing the address (or pointer value) of `base` into
// `destination_register` and defines it: a direct local's frame-slot
// address, or a copy of any vreg-held value — mirroring the canonical
// path's lea-or-slot-load base handling. Returns false outside the subset.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_place_address(MachineX64Selector* selector, IrValueId base, u32 destination_register)
{
    IrFunction* function = selector->function;
    if (base.value >= function->value_count)
    {
        return false;
    }
    IrValue* value = function->values + base.value;
    if (value->definition.value >= function->instruction_count)
    {
        return false;
    }
    IrInstruction* definition = function->instructions + value->definition.value;
    u32 slot = selector->value_stack_slots[base.value];
    if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
    {
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, destination_register),
                                                                    machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                       .opcode = MACHINE_X64_LEA_FRAME,
                                                   });
        machine_x64_define(selector, destination_register, row);
        return true;
    }
    u32 address_register;
    if (!machine_x64_operand_register(selector, base, &address_register))
    {
        return false;
    }
    u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                   .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, destination_register),
                                                                machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                   .opcode = MACHINE_X64_MOV_RR,
                                               });
    machine_x64_define(selector, destination_register, row);
    return true;
}

// A selection-synthesized temporary vreg, defined at the next row to be
// emitted so no post-pass definition patching is needed.
BUSTER_GLOBAL_LOCAL u32 machine_x64_append_slot(MachineX64Selector* selector, u32 size, u32 alignment)
{
    u32 slot_index = selector->stack_slots.total_count;
    u32* slot_size = (u32*)machine_stream_append(selector->arena, &selector->stack_slots);
    *slot_size = size;
    u32* slot_alignment = (u32*)machine_stream_append(selector->arena, &selector->stack_slot_alignments);
    *slot_alignment = alignment;
    return slot_index;
}

BUSTER_GLOBAL_LOCAL u32 machine_x64_synthesize_register(MachineX64Selector* selector)
{
    return machine_builder_virtual_register(&selector->builder, (MachineVirtualRegister){
                                                                    .definition_point =
                                                                        machine_point_make(selector->builder.instructions.total_count, MACHINE_POINT_AFTER),
                                                                    .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                    .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                                });
}

// Emits result-vreg definition rows for one typed instruction. Returns false
// when the construct falls outside the selected subset.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_instruction(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;
    u32 result_register = UINT32_MAX;
    if (instruction->result.value != IR_ID_UNDERLYING_INVALID && instruction->result.value < function->value_count)
    {
        result_register = selector->value_virtual_registers[instruction->result.value];
    }
    switch (instruction->opcode)
    {
        break;
    case IR_OPCODE_LOCAL:
    {
        // Direct locals produce no code: the stack slot recorded during
        // classification is the storage, exactly like the canonical path.
        return selector->value_stack_slots[instruction->result.value] != UINT32_MAX;
    }
    break;
    case IR_OPCODE_STACK_SAVE:
    {
        // With STACK_ALLOCATE outside the subset, RSP is constant after the
        // prologue, so save/restore pairs reduce to exact RSP copies.
        if (result_register == UINT32_MAX)
        {
            return false;
        }
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RSP)},
                                                       .opcode = MACHINE_X64_MOV_RR,
                                                   });
        machine_x64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_STACK_RESTORE:
    {
        u32 saved_register;
        if (!machine_x64_operand_register(selector, instruction->operands[0], &saved_register))
        {
            return false;
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RSP),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, saved_register)},
                                             .opcode = MACHINE_X64_MOV_RR,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_ARGUMENT:
    {
        // The typed ARGUMENT can appear anywhere the frontend first used the
        // parameter; the value itself was captured by the entry rows before
        // any scratch register could clobber the incoming fixed registers.
        if (!instruction->immediate_count || !instruction->immediates)
        {
            return false;
        }
        u32 argument_index = (u32)instruction->immediates[0];
        return argument_index < MACHINE_X64_MAX_ARGUMENTS &&
               (result_register != UINT32_MAX || selector->value_stack_slots[instruction->result.value] != UINT32_MAX) &&
               selector->argument_values[argument_index] == instruction->result.value;
    }
    break;
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_CONSTANT_FLOAT:
    {
        if (result_register == UINT32_MAX)
        {
            return false;
        }
        // Float constants carry their IEEE bit pattern in the immediate,
        // exactly like the canonical shared constant path.
        u64 immediate = instruction->immediates[0];
        if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER && instruction->immediate_is_negative)
        {
            immediate = 0 - immediate;
        }
        u32 immediate_index = selector->immediates.total_count;
        u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
        *immediate_row = immediate;
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                       .opcode = MACHINE_X64_MOV_RI,
                                                   });
        machine_x64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_CAST:
    {
        u32 source_register;
        if (result_register == UINT32_MAX || !machine_x64_operand_register(selector, instruction->operands[0], &source_register))
        {
            return false;
        }
        IrType* source_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        IrType* cast_target_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u32 source_bits = machine_x64_scalar_bit_width(source_type);
        // Float conversions mirror the canonical forms: the 64-bit convert
        // instructions carry every narrower case after an integer extension,
        // and the branchy unsigned-64 sequences stay outside the subset.
        if (instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND || instruction->conversion_operation == IR_CONVERSION_FLOAT_TRUNCATE)
        {
            bool extend = instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND;
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                           .opcode = (u16)(extend ? MACHINE_X64_CVT_F32_TO_F64 : MACHINE_X64_CVT_F64_TO_F32),
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        if (instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ||
            instruction->conversion_operation == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT)
        {
            bool cast_signed = instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT;
            if (!cast_target_type || cast_target_type->kind != IR_TYPE_FLOAT ||
                (cast_target_type->bit_width != 32 && cast_target_type->bit_width != 64) || !source_bits ||
                (!cast_signed && source_bits == 64))
            {
                return false;
            }
            u32 extended_register = source_register;
            if (source_bits < 64)
            {
                u16 extend_opcode = (u16)(cast_signed ? (source_bits == 8 ? MACHINE_X64_MOVSX8_RR : source_bits == 16 ? MACHINE_X64_MOVSX16_RR
                                                         : MACHINE_X64_MOVSX32_RR)
                                                      : (source_bits == 8 ? MACHINE_X64_MOVZX8_RR : source_bits == 16 ? MACHINE_X64_MOVZX16_RR
                                                         : MACHINE_X64_MOV32_RR));
                extended_register = machine_x64_synthesize_register(selector);
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, extended_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                     .opcode = extend_opcode,
                                                 });
            }
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, extended_register)},
                                                           .opcode = (u16)(cast_target_type->bit_width == 64 ? MACHINE_X64_CVT_I64_TO_F64
                                                                                                             : MACHINE_X64_CVT_I64_TO_F32),
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        if (instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ||
            instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER)
        {
            bool to_unsigned = instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER;
            if (!source_type || source_type->kind != IR_TYPE_FLOAT || (source_type->bit_width != 32 && source_type->bit_width != 64) ||
                (to_unsigned && cast_target_type && cast_target_type->kind == IR_TYPE_INTEGER && cast_target_type->bit_width == 64))
            {
                return false;
            }
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                           .opcode = (u16)(source_type->bit_width == 64 ? MACHINE_X64_CVT_F64_TO_I64
                                                                                                        : MACHINE_X64_CVT_F32_TO_I64),
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        u16 opcode = 0;
        switch (instruction->conversion_operation)
        {
            break;
        case IR_CONVERSION_IDENTITY:
        case IR_CONVERSION_INTEGER_TRUNCATE:
        case IR_CONVERSION_INTEGER_REINTERPRET:
        case IR_CONVERSION_POINTER_REINTERPRET:
        case IR_CONVERSION_POINTER_TO_INTEGER:
        case IR_CONVERSION_INTEGER_TO_POINTER:
            opcode = MACHINE_X64_MOV_RR;
            break;
        case IR_CONVERSION_INTEGER_SIGN_EXTEND:
            opcode = (u16)(source_bits == 8 ? MACHINE_X64_MOVSX8_RR : source_bits == 16 ? MACHINE_X64_MOVSX16_RR : source_bits == 32 ? MACHINE_X64_MOVSX32_RR
                           : source_bits == 64 ? MACHINE_X64_MOV_RR : 0);
            break;
        case IR_CONVERSION_INTEGER_ZERO_EXTEND:
            opcode = (u16)(source_bits == 8 ? MACHINE_X64_MOVZX8_RR : source_bits == 16 ? MACHINE_X64_MOVZX16_RR : source_bits == 32 ? MACHINE_X64_MOV32_RR
                           : source_bits == 64 ? MACHINE_X64_MOV_RR : 0);
            break;
        default:
            opcode = 0;
        }
        if (!opcode)
        {
            return false;
        }
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                       .opcode = opcode,
                                                   });
        machine_x64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_UNARY:
    {
        u32 source_register;
        if (result_register == UINT32_MAX || !machine_x64_operand_register(selector, instruction->operands[0], &source_register))
        {
            return false;
        }
        bool wide = machine_x64_type_is_64_bit(program, function->values[instruction->operands[0].value].canonical_type);
        if (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE || instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT)
        {
            bool negate = instruction->unary_operation == IR_UNARY_INTEGER_NEGATE;
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                           .opcode = MACHINE_X64_MOV_RR,
                                                       });
            machine_x64_define(selector, result_register, row);
            machine_x64_select_row(selector,
                                   (MachineInstruction){
                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                       .opcode = (u16)(negate ? (wide ? MACHINE_X64_NEG64 : MACHINE_X64_NEG32) : (wide ? MACHINE_X64_NOT64 : MACHINE_X64_NOT32)),
                                   });
            return true;
        }
        if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE)
        {
            IrType* float_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
            if (!float_type || float_type->kind != IR_TYPE_FLOAT || (float_type->bit_width != 32 && float_type->bit_width != 64))
            {
                return false;
            }
            // Sign-bit flip in the general-register domain is the exact
            // IEEE negation for every input including NaN.
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                           .opcode = MACHINE_X64_MOV_RR,
                                                       });
            machine_x64_define(selector, result_register, row);
            u32 mask_register = machine_x64_synthesize_register(selector);
            u32 immediate_index = selector->immediates.total_count;
            u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
            *immediate_row = float_type->bit_width == 64 ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000);
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register),
                                                              machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                 .opcode = MACHINE_X64_MOV_RI,
                                             });
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register)},
                                                 .opcode = (u16)(float_type->bit_width == 64 ? MACHINE_X64_XOR64 : MACHINE_X64_XOR32),
                                             });
            return true;
        }
        if (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                 .opcode = MACHINE_X64_TEST_RR,
                                             });
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                           .payload = MACHINE_X64_CONDITION_EQUAL,
                                                           .opcode = MACHINE_X64_SETCC,
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        return false;
    }
    break;
    case IR_OPCODE_BINARY:
    {
        u32 left_register;
        u32 right_register;
        if (result_register == UINT32_MAX || !machine_x64_operand_register(selector, instruction->operands[0], &left_register) ||
            !machine_x64_operand_register(selector, instruction->operands[1], &right_register))
        {
            return false;
        }
        IrTypeId operand_type_id = function->values[instruction->operands[0].value].canonical_type;
        IrType* operand_type = ir_type_from_id(&program->types, operand_type_id);
        if (operand_type && operand_type->kind == IR_TYPE_FLOAT)
        {
            if (operand_type->bit_width != 32 && operand_type->bit_width != 64)
            {
                return false;
            }
            u32 float_wide_bit = operand_type->bit_width == 64 ? 0x100u : 0;
            u32 sse_opcode = 0;
            u32 compare_payload = 0;
            switch (instruction->binary_operation)
            {
                break;
            case IR_BINARY_FLOAT_ADD:
                sse_opcode = 0x58;
                break;
            case IR_BINARY_FLOAT_SUBTRACT:
                sse_opcode = 0x5c;
                break;
            case IR_BINARY_FLOAT_MULTIPLY:
                sse_opcode = 0x59;
                break;
            case IR_BINARY_FLOAT_DIVIDE:
                sse_opcode = 0x5e;
                break;
            case IR_BINARY_FLOAT_EQUAL:
                compare_payload = 0x4u | (1u << 9);
                break;
            case IR_BINARY_FLOAT_NOT_EQUAL:
                compare_payload = 0x5u | (2u << 9);
                break;
            case IR_BINARY_FLOAT_LESS:
                compare_payload = 0x2u | (1u << 9);
                break;
            case IR_BINARY_FLOAT_LESS_EQUAL:
                compare_payload = 0x6u | (1u << 9);
                break;
            case IR_BINARY_FLOAT_GREATER:
                compare_payload = 0x7u;
                break;
            case IR_BINARY_FLOAT_GREATER_EQUAL:
                compare_payload = 0x3u;
                break;
            default:
                return false;
            }
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                           .payload = (sse_opcode ? sse_opcode : compare_payload) | float_wide_bit,
                                                           .opcode = (u16)(sse_opcode ? MACHINE_X64_FARITH : MACHINE_X64_FCMP_SET),
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        if (!machine_x64_type_is_scalar_register(operand_type))
        {
            return false;
        }
        bool wide = machine_x64_type_is_64_bit(program, operand_type_id);
        u16 arithmetic = 0;
        switch (instruction->binary_operation)
        {
            break;
        case IR_BINARY_INTEGER_ADD:
            arithmetic = (u16)(wide ? MACHINE_X64_ADD64 : MACHINE_X64_ADD32);
            break;
        case IR_BINARY_INTEGER_SUBTRACT:
            arithmetic = (u16)(wide ? MACHINE_X64_SUB64 : MACHINE_X64_SUB32);
            break;
        case IR_BINARY_INTEGER_MULTIPLY:
            arithmetic = (u16)(wide ? MACHINE_X64_IMUL64 : MACHINE_X64_IMUL32);
            break;
        case IR_BINARY_INTEGER_BITWISE_AND:
        case IR_BINARY_BOOLEAN_AND:
            arithmetic = (u16)(wide ? MACHINE_X64_AND64 : MACHINE_X64_AND32);
            break;
        case IR_BINARY_INTEGER_BITWISE_OR:
        case IR_BINARY_BOOLEAN_OR:
            arithmetic = (u16)(wide ? MACHINE_X64_OR64 : MACHINE_X64_OR32);
            break;
        case IR_BINARY_INTEGER_BITWISE_XOR:
            arithmetic = (u16)(wide ? MACHINE_X64_XOR64 : MACHINE_X64_XOR32);
            break;
        case IR_BINARY_SHIFT_LEFT:
            arithmetic = (u16)(wide ? MACHINE_X64_SHL64 : MACHINE_X64_SHL32);
            break;
        case IR_BINARY_SIGNED_SHIFT_RIGHT:
            arithmetic = (u16)(wide ? MACHINE_X64_SAR64 : MACHINE_X64_SAR32);
            break;
        case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
            arithmetic = (u16)(wide ? MACHINE_X64_SHR64 : MACHINE_X64_SHR32);
            break;
        case IR_BINARY_SIGNED_DIVIDE:
            arithmetic = (u16)(wide ? MACHINE_X64_SDIV64 : MACHINE_X64_SDIV32);
            break;
        case IR_BINARY_UNSIGNED_DIVIDE:
            arithmetic = (u16)(wide ? MACHINE_X64_UDIV64 : MACHINE_X64_UDIV32);
            break;
        case IR_BINARY_SIGNED_REMAINDER:
            arithmetic = (u16)(wide ? MACHINE_X64_SREM64 : MACHINE_X64_SREM32);
            break;
        case IR_BINARY_UNSIGNED_REMAINDER:
            arithmetic = (u16)(wide ? MACHINE_X64_UREM64 : MACHINE_X64_UREM32);
            break;
        default:
            arithmetic = 0;
        }
        if (arithmetic)
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register)},
                                                           .opcode = MACHINE_X64_MOV_RR,
                                                       });
            machine_x64_define(selector, result_register, row);
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                 .opcode = arithmetic,
                                             });
            return true;
        }
        u32 condition = 0;
        switch (instruction->binary_operation)
        {
            break;
        case IR_BINARY_INTEGER_EQUAL:
        case IR_BINARY_POINTER_EQUAL:
        case IR_BINARY_BOOLEAN_EQUAL:
            condition = MACHINE_X64_CONDITION_EQUAL;
            break;
        case IR_BINARY_INTEGER_NOT_EQUAL:
        case IR_BINARY_POINTER_NOT_EQUAL:
        case IR_BINARY_BOOLEAN_NOT_EQUAL:
            condition = MACHINE_X64_CONDITION_NOT_EQUAL;
            break;
        case IR_BINARY_SIGNED_LESS:
            condition = MACHINE_X64_CONDITION_LESS;
            break;
        case IR_BINARY_SIGNED_LESS_EQUAL:
            condition = MACHINE_X64_CONDITION_LESS_EQUAL;
            break;
        case IR_BINARY_SIGNED_GREATER:
            condition = MACHINE_X64_CONDITION_GREATER;
            break;
        case IR_BINARY_SIGNED_GREATER_EQUAL:
            condition = MACHINE_X64_CONDITION_GREATER_EQUAL;
            break;
        case IR_BINARY_UNSIGNED_LESS:
            condition = MACHINE_X64_CONDITION_BELOW;
            break;
        case IR_BINARY_UNSIGNED_LESS_EQUAL:
            condition = MACHINE_X64_CONDITION_BELOW_EQUAL;
            break;
        case IR_BINARY_UNSIGNED_GREATER:
            condition = MACHINE_X64_CONDITION_ABOVE;
            break;
        case IR_BINARY_UNSIGNED_GREATER_EQUAL:
            condition = MACHINE_X64_CONDITION_ABOVE_EQUAL;
            break;
        default:
            condition = 0;
        }
        if (!condition)
        {
            return false;
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                             .opcode = (u16)(wide ? MACHINE_X64_CMP64 : MACHINE_X64_CMP32),
                                         });
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                       .payload = condition,
                                                       .opcode = MACHINE_X64_SETCC,
                                                   });
        machine_x64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_DEREFERENCE:
    {
        u32 source_register;
        if (result_register == UINT32_MAX || !machine_x64_operand_register(selector, instruction->operands[0], &source_register))
        {
            return false;
        }
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                       .opcode = MACHINE_X64_MOV_RR,
                                                   });
        machine_x64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_GLOBAL:
    {
        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
        if (result_register == UINT32_MAX || !symbol || symbol->is_thread_local)
        {
            return false;
        }
        u32 target_index = selector->call_targets.total_count;
        IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
        *target_row = instruction->symbol;
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                       .payload = target_index,
                                                       .opcode = MACHINE_X64_LEA_SYMBOL,
                                                   });
        machine_x64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_ADDRESS_OF:
    {
        return result_register != UINT32_MAX && machine_x64_select_place_address(selector, instruction->operands[0], result_register);
    }
    break;
    case IR_OPCODE_FIELD:
    {
        if (result_register == UINT32_MAX || !instruction->immediate_count || !instruction->immediates)
        {
            return false;
        }
        IrType* aggregate = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        u64 field_index = instruction->immediates[0];
        if (!aggregate || field_index >= aggregate->field_count || aggregate->fields[field_index].offset > INT32_MAX)
        {
            return false;
        }
        if (!machine_x64_select_place_address(selector, instruction->operands[0], result_register))
        {
            return false;
        }
        u64 field_offset = aggregate->fields[field_index].offset;
        if (field_offset)
        {
            u32 offset_register = machine_x64_synthesize_register(selector);
            u32 immediate_index = selector->immediates.total_count;
            u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
            *immediate_row = field_offset;
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, offset_register),
                                                              machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                 .opcode = MACHINE_X64_MOV_RI,
                                             });
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, offset_register)},
                                                 .opcode = MACHINE_X64_ADD64,
                                             });
        }
        return true;
    }
    break;
    case IR_OPCODE_INDEX:
    {
        u32 index_register;
        if (result_register == UINT32_MAX || instruction->operand_count < 2 ||
            !machine_x64_operand_register(selector, instruction->operands[1], &index_register))
        {
            return false;
        }
        IrType* index_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
        IrType* element = ir_type_from_id(&program->types, instruction->canonical_type);
        if (!index_type || index_type->kind != IR_TYPE_INTEGER || !element || !element->layout.resolved || element->layout.size > INT32_MAX)
        {
            return false;
        }
        if (!machine_x64_select_place_address(selector, instruction->operands[0], result_register))
        {
            return false;
        }
        // The scaled index is computed in a synthesized temporary so the
        // read-modify-write multiply never touches the index value's slot.
        u32 scaled_register = machine_x64_synthesize_register(selector);
        u16 extend_opcode = MACHINE_X64_MOV_RR;
        if (index_type->is_signed && index_type->bit_width < 64)
        {
            extend_opcode = (u16)(index_type->bit_width == 8 ? MACHINE_X64_MOVSX8_RR : index_type->bit_width == 16 ? MACHINE_X64_MOVSX16_RR
                                  : index_type->bit_width == 32 ? MACHINE_X64_MOVSX32_RR : 0);
            if (!extend_opcode)
            {
                return false;
            }
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, index_register)},
                                             .opcode = extend_opcode,
                                         });
        if (element->layout.size != 1)
        {
            u32 size_register = machine_x64_synthesize_register(selector);
            u32 immediate_index = selector->immediates.total_count;
            u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
            *immediate_row = element->layout.size;
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, size_register),
                                                              machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                 .opcode = MACHINE_X64_MOV_RI,
                                             });
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, size_register)},
                                                 .opcode = MACHINE_X64_IMUL64,
                                             });
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register)},
                                             .opcode = MACHINE_X64_ADD64,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_LOAD:
    case IR_OPCODE_ATOMIC_LOAD:
    {
        // Aligned x86 loads are already atomic; the atomic form only
        // excludes the aggregate paths.
        if (instruction->operands[0].value >= function->value_count || instruction->result.value == IR_ID_UNDERLYING_INVALID)
        {
            return false;
        }
        IrValue* place = function->values + instruction->operands[0].value;
        if (place->definition.value >= function->instruction_count)
        {
            return false;
        }
        IrInstruction* definition = function->instructions + place->definition.value;
        u32 slot = selector->value_stack_slots[instruction->operands[0].value];
        u32 result_slot = selector->value_stack_slots[instruction->result.value];
        if (result_register == UINT32_MAX && result_slot != UINT32_MAX)
        {
            if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD)
            {
                return false;
            }
            // Aggregate load: exact-size chunk copy into the result slot,
            // from a direct local slot or through an address vreg.
            IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
            if (!loaded_type || !loaded_type->layout.resolved || loaded_type->layout.size > UINT32_MAX)
            {
                return false;
            }
            if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                     .payload = (u32)loaded_type->layout.size,
                                                     .opcode = MACHINE_X64_COPY_FRAME_FROM_FRAME,
                                                 });
                return true;
            }
            if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                definition->opcode == IR_OPCODE_FIELD)
            {
                u32 address_register;
                if (!machine_x64_operand_register(selector, instruction->operands[0], &address_register))
                {
                    return false;
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                     .payload = (u32)loaded_type->layout.size,
                                                     .opcode = MACHINE_X64_COPY_FRAME_FROM_PTR,
                                                 });
                return true;
            }
            return false;
        }
        if (result_register == UINT32_MAX)
        {
            return false;
        }
        if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                           .opcode = MACHINE_X64_LOAD_FRAME,
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
            definition->opcode == IR_OPCODE_FIELD)
        {
            u32 address_register;
            if (!machine_x64_operand_register(selector, instruction->operands[0], &address_register))
            {
                return false;
            }
            IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
            u64 size = loaded_type && loaded_type->layout.resolved ? loaded_type->layout.size : 0;
            u16 opcode = size == 1 ? MACHINE_X64_LOAD_PTR8 : size == 2 ? MACHINE_X64_LOAD_PTR16 : size == 4 ? MACHINE_X64_LOAD_PTR32
                         : size == 8 ? MACHINE_X64_LOAD_PTR64 : 0;
            if (!opcode)
            {
                return false;
            }
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                           .opcode = opcode,
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        return false;
    }
    break;
    case IR_OPCODE_STORE:
    case IR_OPCODE_ATOMIC_STORE:
    {
        if (instruction->operands[0].value >= function->value_count || instruction->operands[1].value >= function->value_count)
        {
            return false;
        }
        IrValue* place = function->values + instruction->operands[0].value;
        if (place->definition.value >= function->instruction_count)
        {
            return false;
        }
        IrInstruction* definition = function->instructions + place->definition.value;
        IrType* stored_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
        u64 size = stored_type && stored_type->layout.resolved ? stored_type->layout.size : 0;
        u32 slot = selector->value_stack_slots[instruction->operands[0].value];
        u32 value_slot = selector->value_stack_slots[instruction->operands[1].value];
        if (instruction->opcode == IR_OPCODE_ATOMIC_STORE && instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL)
        {
            // Sequentially consistent stores exchange, exactly like the
            // canonical path; weaker orders are plain x86 stores below.
            u32 atomic_value_register;
            if (!machine_x64_operand_register(selector, instruction->operands[1], &atomic_value_register) || (size != 1 && size != 2 && size != 4 && size != 8))
            {
                return false;
            }
            u32 address_register = machine_x64_synthesize_register(selector);
            if (!machine_x64_select_place_address(selector, instruction->operands[0], address_register))
            {
                return false;
            }
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, atomic_value_register)},
                                                 .payload = (u32)size,
                                                 .opcode = MACHINE_X64_ATOMIC_STORE_XCHG,
                                             });
            return true;
        }
        if (value_slot != UINT32_MAX && selector->value_virtual_registers[instruction->operands[1].value] == UINT32_MAX)
        {
            if (instruction->opcode == IR_OPCODE_ATOMIC_STORE)
            {
                return false;
            }
            // Aggregate store: exact-size chunk copy out of the value slot.
            if (!size || size > UINT32_MAX)
            {
                return false;
            }
            if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = (u32)size,
                                                     .opcode = MACHINE_X64_COPY_FRAME_FROM_FRAME,
                                                 });
                return true;
            }
            if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                definition->opcode == IR_OPCODE_FIELD)
            {
                u32 address_register;
                if (!machine_x64_operand_register(selector, instruction->operands[0], &address_register))
                {
                    return false;
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = (u32)size,
                                                     .opcode = MACHINE_X64_COPY_PTR_FROM_FRAME,
                                                 });
                return true;
            }
            return false;
        }
        u32 value_register;
        if (!machine_x64_operand_register(selector, instruction->operands[1], &value_register))
        {
            return false;
        }
        u32 size_index = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : size == 8 ? 3 : UINT32_MAX;
        if (size_index == UINT32_MAX)
        {
            return false;
        }
        if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
        {
            // Direct-slot stores always write the full eight-byte slot,
            // exactly like the canonical path: the slot is the value's
            // exclusive home, and narrower stores would leave stale upper
            // bytes for the sixty-four-bit slot loads and tests that
            // follow. The pointer stores below stay exactly sized.
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                 .opcode = MACHINE_X64_STORE_FRAME64,
                                             });
            return true;
        }
        if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
            definition->opcode == IR_OPCODE_FIELD)
        {
            u32 address_register;
            if (!machine_x64_operand_register(selector, instruction->operands[0], &address_register))
            {
                return false;
            }
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                 .opcode = (u16)(MACHINE_X64_STORE_PTR8 + size_index),
                                             });
            return true;
        }
        return false;
    }
    break;
    case IR_OPCODE_FUNCTION:
    {
        // A function reference is an ordinary rip-relative symbol address;
        // direct calls carry the symbol on the CALL row itself, so this lea
        // only matters when the value is used as data.
        if (result_register == UINT32_MAX || instruction->symbol.value == IR_ID_UNDERLYING_INVALID)
        {
            return false;
        }
        u32 target_index = selector->call_targets.total_count;
        IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
        *target_row = instruction->symbol;
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                       .payload = target_index,
                                                       .opcode = MACHINE_X64_LEA_SYMBOL,
                                                   });
        machine_x64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_CALL:
    {
        if (!instruction->operand_count || instruction->operands[0].value >= function->value_count)
        {
            return false;
        }
        IrValue* callee = function->values + instruction->operands[0].value;
        if (callee->definition.value >= function->instruction_count)
        {
            return false;
        }
        bool direct_call = function->instructions[callee->definition.value].opcode == IR_OPCODE_FUNCTION &&
                           instruction->symbol.value != IR_ID_UNDERLYING_INVALID;
        u32 callee_register = UINT32_MAX;
        if (!direct_call && !machine_x64_operand_register(selector, instruction->operands[0], &callee_register))
        {
            return false;
        }
        IrType* callee_type = ir_type_from_id(&program->types, callee->canonical_type);
        if (callee_type && callee_type->kind == IR_TYPE_POINTER)
        {
            callee_type = ir_type_from_id(&program->types, callee_type->element_type);
        }
        u32 call_argument_count = instruction->operand_count - 1;
        bool variadic_call = callee_type && callee_type->kind == IR_TYPE_FUNCTION && callee_type->is_variadic;
        if (!callee_type || callee_type->kind != IR_TYPE_FUNCTION ||
            (variadic_call ? call_argument_count < callee_type->parameter_count : callee_type->parameter_count != call_argument_count) ||
            call_argument_count > MACHINE_X64_MAX_ARGUMENTS)
        {
            return false;
        }
        IrType* callee_return_type = ir_type_from_id(&program->types, callee_type->return_type);
        bool callee_returns_value = callee_return_type && callee_return_type->kind != IR_TYPE_VOID;
        MachineX64ValueShape callee_return_shape = {0};
        if (callee_returns_value && !machine_x64_value_shape(program, callee_type->return_type, IR_ABI_USE_RESULT, &callee_return_shape))
        {
            return false;
        }
        // Every argument — fixed and variadic — must be integer-class under
        // the subset's shapes; a variadic call then always passes zero
        // vector registers in AL, matching the canonical convention.
        // Aggregate variadic tails stay outside the subset.
        MachineX64ValueShape argument_shapes[MACHINE_X64_MAX_ARGUMENTS];
        MachineX64ArgumentPlacement argument_placements[MACHINE_X64_MAX_ARGUMENTS];
        u32 call_integer_count = callee_return_shape.indirect ? 1 : 0;
        u32 call_float_count = 0;
        u32 call_stack_part_count = 0;
        u32 indirect_result_slot = UINT32_MAX;
        if (callee_return_shape.indirect)
        {
            if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
            {
                indirect_result_slot = selector->value_stack_slots[instruction->result.value];
                if (indirect_result_slot == UINT32_MAX)
                {
                    return false;
                }
            }
            else
            {
                // An unused indirect result still needs backing storage.
                indirect_result_slot = machine_x64_append_slot(selector, callee_return_shape.byte_size, 8);
            }
        }
        for (u32 argument_index = 0; argument_index < call_argument_count; argument_index += 1)
        {
            IrTypeId argument_type_id = argument_index < callee_type->parameter_count
                                            ? callee_type->parameter_types[argument_index]
                                            : function->values[instruction->operands[argument_index + 1].value].canonical_type;
            if (!machine_x64_value_shape(program, argument_type_id, IR_ABI_USE_ARGUMENT, argument_shapes + argument_index) ||
                (argument_shapes[argument_index].aggregate && argument_index >= callee_type->parameter_count))
            {
                return false;
            }
            machine_x64_place_argument(argument_shapes + argument_index, &call_integer_count, &call_float_count, &call_stack_part_count,
                                       argument_placements + argument_index);
        }
        bool call_stack_padding = (call_stack_part_count & 1) != 0;
        u32 argument_registers[MACHINE_X64_MAX_ARGUMENTS];
        u32 argument_slots[MACHINE_X64_MAX_ARGUMENTS];
        for (u32 argument_index = 0; argument_index < call_argument_count; argument_index += 1)
        {
            argument_registers[argument_index] = UINT32_MAX;
            argument_slots[argument_index] = selector->value_stack_slots[instruction->operands[argument_index + 1].value];
            if (argument_shapes[argument_index].aggregate)
            {
                if (argument_slots[argument_index] == UINT32_MAX)
                {
                    return false;
                }
                continue;
            }
            if (!machine_x64_operand_register(selector, instruction->operands[argument_index + 1], argument_registers + argument_index))
            {
                return false;
            }
        }
        // Outgoing stack parts push right to left before any register is
        // placed (the pushes scratch only RAX), with alignment padding for
        // an odd part count.
        if (call_stack_padding)
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .payload = 8,
                                                 .opcode = MACHINE_X64_SUB_RSP,
                                             });
        }
        for (u32 argument_reverse = call_argument_count; argument_reverse > 0; argument_reverse -= 1)
        {
            u32 argument_index = argument_reverse - 1;
            MachineX64ArgumentPlacement* argument_placement = argument_placements + argument_index;
            if (!argument_placement->on_stack)
            {
                continue;
            }
            if (argument_shapes[argument_index].aggregate)
            {
                for (u32 part_reverse = argument_placement->stack_part_count; part_reverse > 0; part_reverse -= 1)
                {
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, argument_slots[argument_index])},
                                                         .payload = (part_reverse - 1) * 8,
                                                         .opcode = MACHINE_X64_PUSH_FRAME,
                                                     });
                }
                continue;
            }
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_registers[argument_index])},
                                                 .opcode = MACHINE_X64_PUSH_REGISTER,
                                             });
        }
        // Explicit fixed-register argument copies; integer targets load
        // directly (never through a scratch that could disturb an already
        // placed argument), and float parts bounce through RAX into their
        // XMM registers, which no general-register write can touch. The
        // hidden result pointer takes the first integer register.
        if (callee_return_shape.indirect)
        {
            u32 result_pointer_register = machine_x64_synthesize_register(selector);
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_pointer_register),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, indirect_result_slot)},
                                                 .opcode = MACHINE_X64_LEA_FRAME,
                                             });
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RDI),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_pointer_register)},
                                                 .opcode = MACHINE_X64_MOV_RR,
                                             });
        }
        for (u32 argument_index = 0; argument_index < call_argument_count; argument_index += 1)
        {
            MachineX64ValueShape* shape = argument_shapes + argument_index;
            if (argument_placements[argument_index].on_stack)
            {
                continue;
            }
            u32 next_integer = argument_placements[argument_index].first_integer;
            u32 next_float = argument_placements[argument_index].first_float;
            if (shape->aggregate)
            {
                for (u32 part_index = 0; part_index < shape->part_count; part_index += 1)
                {
                    bool part_float = shape->part_is_float[part_index] != 0;
                    if (part_float)
                    {
                        u32 bounce_register = machine_x64_synthesize_register(selector);
                        machine_x64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register),
                                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, argument_slots[argument_index])},
                                                             .payload = shape->part_offsets[part_index],
                                                             .opcode = MACHINE_X64_LOAD_FRAME,
                                                         });
                        machine_x64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                             .payload = next_float,
                                                             .opcode = MACHINE_X64_MOVQ_TO_XMM,
                                                         });
                        next_float += 1;
                    }
                    else
                    {
                        machine_x64_select_row(selector,
                                               (MachineInstruction){
                                                   .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER,
                                                                                 machine_x64_system_v_arguments[next_integer]),
                                                                machine_ref_make(MACHINE_REF_STACK_SLOT, argument_slots[argument_index])},
                                                   .payload = shape->part_offsets[part_index],
                                                   .opcode = MACHINE_X64_LOAD_FRAME,
                                               });
                        next_integer += 1;
                    }
                }
                continue;
            }
            if (shape->part_is_float[0])
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_registers[argument_index])},
                                                     .payload = next_float,
                                                     .opcode = MACHINE_X64_MOVQ_TO_XMM,
                                                 });
                continue;
            }
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER,
                                                                               machine_x64_system_v_arguments[next_integer]),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_registers[argument_index])},
                                                 .opcode = MACHINE_X64_MOV_RR,
                                             });
        }
        if (direct_call)
        {
            u32 target_index = selector->call_targets.total_count;
            IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
            *target_row = instruction->symbol;
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .payload = target_index,
                                                 .opcode = MACHINE_X64_CALL_DIRECT,
                                                 .flags = (u16)(variadic_call ? (1u | (call_float_count << 1)) : 0),
                                             });
        }
        else
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, callee_register)},
                                                 .opcode = MACHINE_X64_CALL_INDIRECT,
                                                 .flags = (u16)(variadic_call ? (1u | (call_float_count << 1)) : 0),
                                             });
        }
        if (call_stack_part_count || call_stack_padding)
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .payload = call_stack_part_count * 8 + (call_stack_padding ? 8u : 0),
                                                 .opcode = MACHINE_X64_ADD_RSP,
                                             });
        }
        if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            if (callee_return_shape.indirect)
            {
                // The callee already stored the value through the hidden
                // pointer into the result slot.
                return true;
            }
            if (callee_return_shape.aggregate)
            {
                u32 result_slot = selector->value_stack_slots[instruction->result.value];
                if (result_slot == UINT32_MAX)
                {
                    return false;
                }
                u32 return_integer_index = 0;
                u32 return_float_index = 0;
                for (u32 part_index = 0; part_index < callee_return_shape.part_count; part_index += 1)
                {
                    if (callee_return_shape.part_is_float[part_index])
                    {
                        u32 bounce_register = machine_x64_synthesize_register(selector);
                        machine_x64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                             .payload = return_float_index,
                                                             .opcode = MACHINE_X64_MOVQ_FROM_XMM,
                                                         });
                        machine_x64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                             .payload = callee_return_shape.part_offsets[part_index],
                                                             .opcode = MACHINE_X64_STORE_FRAME64,
                                                         });
                        return_float_index += 1;
                        continue;
                    }
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                      machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER,
                                                                                       return_integer_index ? MACHINE_X64_RDX : MACHINE_X64_RAX)},
                                                         .payload = callee_return_shape.part_offsets[part_index],
                                                         .opcode = MACHINE_X64_STORE_FRAME64,
                                                     });
                    return_integer_index += 1;
                }
                return true;
            }
            if (result_register == UINT32_MAX)
            {
                return false;
            }
            u32 row;
            if (callee_return_shape.part_is_float[0])
            {
                row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                           .opcode = MACHINE_X64_MOVQ_FROM_XMM,
                                                       });
            }
            else
            {
                row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX)},
                                                           .opcode = MACHINE_X64_MOV_RR,
                                                       });
            }
            machine_x64_define(selector, result_register, row);
        }
        return true;
    }
    break;
    case IR_OPCODE_BRANCH:
    {
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value)},
                                             .opcode = MACHINE_X64_JMP,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE:
    {
        u32 value_register;
        if (result_register == UINT32_MAX || instruction->operand_count < 2 ||
            !machine_x64_operand_register(selector, instruction->operands[1], &value_register) ||
            instruction->atomic_operation >= IR_ATOMIC_OPERATION_COUNT)
        {
            return false;
        }
        IrType* atomic_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u64 size = atomic_type && atomic_type->layout.resolved ? atomic_type->layout.size : 0;
        if (size != 1 && size != 2 && size != 4 && size != 8)
        {
            return false;
        }
        u32 address_register = machine_x64_synthesize_register(selector);
        if (!machine_x64_select_place_address(selector, instruction->operands[0], address_register))
        {
            return false;
        }
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                       .payload = (u32)size | ((u32)instruction->atomic_operation << 8),
                                                       .opcode = MACHINE_X64_ATOMIC_RMW,
                                                   });
        machine_x64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_ATOMIC_COMPARE_EXCHANGE:
    {
        u32 expected_register;
        u32 desired_register;
        if (result_register == UINT32_MAX || instruction->operand_count < 3 ||
            !machine_x64_operand_register(selector, instruction->operands[1], &expected_register) ||
            !machine_x64_operand_register(selector, instruction->operands[2], &desired_register))
        {
            return false;
        }
        IrType* atomic_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u64 size = atomic_type && atomic_type->layout.resolved ? atomic_type->layout.size : 0;
        if (size != 1 && size != 2 && size != 4 && size != 8)
        {
            return false;
        }
        u32 address_register = machine_x64_synthesize_register(selector);
        if (!machine_x64_select_place_address(selector, instruction->operands[0], address_register))
        {
            return false;
        }
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, expected_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, desired_register)},
                                                       .payload = (u32)size,
                                                       .opcode = MACHINE_X64_ATOMIC_CMPXCHG,
                                                   });
        machine_x64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_ATOMIC_FENCE:
    {
        if (!instruction->atomic_signal_fence && instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL)
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .opcode = MACHINE_X64_MFENCE,
                                             });
        }
        return true;
    }
    break;
    case IR_OPCODE_UNREACHABLE:
    {
        // Control never reaches this terminator; a plain return keeps the
        // block verifier-well-formed with the cheapest legal bytes.
        machine_x64_select_row(selector, (MachineInstruction){
                                             .opcode = MACHINE_X64_RET,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_SWITCH:
    {
        u32 condition_register;
        if (!machine_x64_operand_register(selector, instruction->operands[0], &condition_register) || !instruction->target_count ||
            instruction->target_count != instruction->immediate_count + 1 || instruction->immediate_count > UINT16_MAX || !instruction->immediates)
        {
            return false;
        }
        u32 first_case = selector->switch_cases.total_count;
        for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
        {
            MachineSwitchCase* case_row = (MachineSwitchCase*)machine_stream_append(selector->arena, &selector->switch_cases);
            *case_row = (MachineSwitchCase){
                .value = instruction->immediates[case_index],
                .target_block = instruction->targets[case_index].value,
            };
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, condition_register),
                                                          machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[instruction->target_count - 1].value)},
                                             .payload = first_case,
                                             .opcode = MACHINE_X64_SWITCH,
                                             .flags = (u16)instruction->immediate_count,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_BRANCH_IF:
    {
        u32 condition_register;
        if (!machine_x64_operand_register(selector, instruction->operands[0], &condition_register))
        {
            return false;
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, condition_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, condition_register)},
                                             .opcode = MACHINE_X64_TEST_RR,
                                         });
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value),
                                                          machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[1].value)},
                                             .payload = MACHINE_X64_CONDITION_NOT_EQUAL,
                                             .opcode = MACHINE_X64_JCC,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_RETURN:
    {
        if (instruction->operand_count)
        {
            if (selector->return_shape.indirect)
            {
                u32 value_slot = instruction->operands[0].value < function->value_count
                                     ? selector->value_stack_slots[instruction->operands[0].value]
                                     : UINT32_MAX;
                if (value_slot == UINT32_MAX || selector->hidden_return_slot == UINT32_MAX)
                {
                    return false;
                }
                u32 pointer_register = machine_x64_synthesize_register(selector);
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, selector->hidden_return_slot)},
                                                     .opcode = MACHINE_X64_LOAD_FRAME,
                                                 });
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = selector->return_shape.byte_size,
                                                     .opcode = MACHINE_X64_COPY_PTR_FROM_FRAME,
                                                 });
                // The System V contract returns the hidden pointer in RAX.
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register)},
                                                     .opcode = MACHINE_X64_MOV_RR,
                                                 });
            }
            else if (selector->return_shape.aggregate)
            {
                u32 value_slot = instruction->operands[0].value < function->value_count
                                     ? selector->value_stack_slots[instruction->operands[0].value]
                                     : UINT32_MAX;
                if (value_slot == UINT32_MAX)
                {
                    return false;
                }
                u32 return_integer_index = 0;
                u32 return_float_index = 0;
                for (u32 part_index = 0; part_index < selector->return_shape.part_count; part_index += 1)
                {
                    if (selector->return_shape.part_is_float[part_index])
                    {
                        u32 bounce_register = machine_x64_synthesize_register(selector);
                        machine_x64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register),
                                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                             .payload = selector->return_shape.part_offsets[part_index],
                                                             .opcode = MACHINE_X64_LOAD_FRAME,
                                                         });
                        machine_x64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                             .payload = return_float_index,
                                                             .opcode = MACHINE_X64_MOVQ_TO_XMM,
                                                         });
                        return_float_index += 1;
                        continue;
                    }
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER,
                                                                                       return_integer_index ? MACHINE_X64_RDX : MACHINE_X64_RAX),
                                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                         .payload = selector->return_shape.part_offsets[part_index],
                                                         .opcode = MACHINE_X64_LOAD_FRAME,
                                                     });
                    return_integer_index += 1;
                }
            }
            else if (selector->return_shape.part_is_float[0])
            {
                u32 value_register;
                if (!machine_x64_operand_register(selector, instruction->operands[0], &value_register))
                {
                    return false;
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_X64_MOVQ_TO_XMM,
                                                 });
            }
            else
            {
                u32 value_register;
                if (!machine_x64_operand_register(selector, instruction->operands[0], &value_register))
                {
                    return false;
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_X64_MOV_RR,
                                                 });
            }
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .opcode = MACHINE_X64_RET,
                                         });
        return true;
    }
    break;
    default:
        return false;
    }
    return false;
}

MachineSelectResult machine_select_canonical_function(Arena* arena, IrProgram* program, IrFunction* function, Target target)
{
    MachineSelectResult result = {
        .failed_opcode = IR_OPCODE_COUNT,
    };
    if (!arena || !program || !function || target.cpu_arch != CPU_ARCH_X86_64 || function->state != IR_FUNCTION_LOWERED || !function->block_count ||
        function->entry.value != 0)
    {
        return result;
    }
    IrType* function_type = ir_type_from_id(&program->types, function->canonical_type);
    if (!function_type || function_type->kind != IR_TYPE_FUNCTION || function_type->is_variadic ||
        function_type->parameter_count > MACHINE_X64_MAX_ARGUMENTS)
    {
        return result;
    }
    IrType* return_type = ir_type_from_id(&program->types, function_type->return_type);
    bool returns_value = return_type && return_type->kind != IR_TYPE_VOID;
    MachineX64ValueShape signature_return_shape = {0};
    if (returns_value && !machine_x64_value_shape(program, function_type->return_type, IR_ABI_USE_RESULT, &signature_return_shape))
    {
        return result;
    }
    MachineX64ValueShape signature_parameter_shapes[MACHINE_X64_MAX_ARGUMENTS] = {0};
    MachineX64ArgumentPlacement signature_parameter_placements[MACHINE_X64_MAX_ARGUMENTS] = {0};
    u32 signature_integer_count = signature_return_shape.indirect ? 1 : 0;
    u32 signature_float_count = 0;
    u32 signature_stack_count = 0;
    for (u32 parameter_index = 0; parameter_index < function_type->parameter_count; parameter_index += 1)
    {
        if (!machine_x64_value_shape(program, function_type->parameter_types[parameter_index], IR_ABI_USE_ARGUMENT,
                                     signature_parameter_shapes + parameter_index))
        {
            return result;
        }
        machine_x64_place_argument(signature_parameter_shapes + parameter_index, &signature_integer_count, &signature_float_count,
                                   &signature_stack_count, signature_parameter_placements + parameter_index);
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        if (function->blocks[block_index].parameter_count)
        {
            return result;
        }
    }
    MachineX64Selector selector = {
        .arena = arena,
        .program = program,
        .function = function,
        .builder = machine_function_builder_begin(arena),
        .value_virtual_registers = arena_allocate(arena, u32, function->value_count),
        .value_stack_slots = arena_allocate(arena, u32, function->value_count),
        .supported = true,
        .failed_opcode = IR_OPCODE_COUNT,
    };
    machine_stream_initialize(&selector.immediates, sizeof(u64));
    machine_stream_initialize(&selector.stack_slots, sizeof(u32));
    machine_stream_initialize(&selector.call_targets, sizeof(IrSymbolId));
    machine_stream_initialize(&selector.switch_cases, sizeof(MachineSwitchCase));
    machine_stream_initialize(&selector.stack_slot_alignments, sizeof(u32));
    selector.return_shape = signature_return_shape;
    selector.hidden_return_slot = UINT32_MAX;
    if (signature_return_shape.indirect)
    {
        selector.hidden_return_slot = machine_x64_append_slot(&selector, 8, 8);
    }
    for (u32 parameter_index = 0; parameter_index < BUSTER_ARRAY_LENGTH(selector.parameter_shapes); parameter_index += 1)
    {
        selector.parameter_shapes[parameter_index] = signature_parameter_shapes[parameter_index];
        selector.parameter_placements[parameter_index] = signature_parameter_placements[parameter_index];
    }
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        selector.value_virtual_registers[value_index] = UINT32_MAX;
        selector.value_stack_slots[value_index] = UINT32_MAX;
    }
    for (u32 argument_index = 0; argument_index < BUSTER_ARRAY_LENGTH(selector.argument_values); argument_index += 1)
    {
        selector.argument_values[argument_index] = IR_ID_UNDERLYING_INVALID;
    }
    // Classification pass: direct locals become stack slots, every other
    // scalar result becomes a virtual register, in stable value-id order.
    for (u32 block_index = 0; block_index < function->block_count && selector.supported; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = function->instructions[id.value].next)
        {
            IrInstruction* instruction = function->instructions + id.value;
            if (instruction->result.value == IR_ID_UNDERLYING_INVALID || instruction->result.value >= function->value_count)
            {
                continue;
            }
            IrValue* value = function->values + instruction->result.value;
            if (instruction->opcode == IR_OPCODE_ARGUMENT && instruction->immediate_count && instruction->immediates &&
                instruction->immediates[0] < BUSTER_ARRAY_LENGTH(selector.argument_values))
            {
                selector.argument_values[instruction->immediates[0]] = instruction->result.value;
            }
            if (instruction->opcode == IR_OPCODE_LOCAL)
            {
                IrType* local_type = ir_type_from_id(&program->types, value->canonical_type);
                u32 local_alignment = BUSTER_MAX(BUSTER_MAX(value->alignment, local_type ? local_type->layout.alignment : 0), 8u);
                if (!local_type || !local_type->layout.resolved || local_alignment > 16 || local_type->layout.size > UINT32_MAX - 7)
                {
                    machine_x64_reject(&selector, instruction->opcode);
                    break;
                }
                selector.value_stack_slots[instruction->result.value] =
                    machine_x64_append_slot(&selector, (u32)((local_type->layout.size + 7) & ~(u64)7), local_alignment);
                continue;
            }
            IrType* value_type = ir_type_from_id(&program->types, value->canonical_type);
            // Address producers hold an 8-byte address in their vreg no
            // matter what their declared canonical type is, exactly like
            // the canonical path stores an address in the value's slot.
            bool float_scalar = value_type && value_type->layout.resolved && value_type->kind == IR_TYPE_FLOAT &&
                                (value_type->bit_width == 32 || value_type->bit_width == 64);
            if (machine_x64_type_is_scalar_register(value_type) || float_scalar || machine_x64_opcode_produces_address(instruction->opcode))
            {
                u32 register_index = machine_builder_virtual_register(&selector.builder, (MachineVirtualRegister){
                                                                                             .definition_point = MACHINE_POINT_INVALID,
                                                                                             .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                                             .typed_origin = instruction->result.value,
                                                                                         });
                selector.value_virtual_registers[instruction->result.value] = register_index;
            }
            else if ((instruction->opcode == IR_OPCODE_ARGUMENT || instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_CALL) &&
                     value_type && value_type->layout.resolved && value_type->layout.size <= UINT32_MAX - 7 &&
                     (value_type->kind == IR_TYPE_STRUCT || value_type->kind == IR_TYPE_UNION || value_type->kind == IR_TYPE_SLICE))
            {
                // Aggregate values own a frame slot like the canonical
                // path's per-value storage; copies and ABI part transfers
                // address it directly.
                selector.value_stack_slots[instruction->result.value] =
                    machine_x64_append_slot(&selector, (u32)((value_type->layout.size + 7) & ~(u64)7), 8);
            }
        }
    }
    selector.virtual_register_count = selector.builder.virtual_registers.total_count;
    selector.virtual_register_definitions = arena_allocate(arena, u32, selector.virtual_register_count);
    for (u32 register_index = 0; register_index < selector.virtual_register_count; register_index += 1)
    {
        selector.virtual_register_definitions[register_index] = MACHINE_POINT_INVALID;
    }
    u32 typed_instruction_count = 0;
    for (u32 block_index = 0; block_index < function->block_count && selector.supported; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        machine_builder_block_begin(&selector.builder);
        if (block_index == 0)
        {
            // Capture every incoming argument register at entry, before any
            // body row can use an argument register as an operand scratch.
            // Integer parts capture first because float captures scratch
            // general registers; XMM state survives that pass untouched.
            if (selector.return_shape.indirect)
            {
                machine_x64_select_row(&selector, (MachineInstruction){
                                                      .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, selector.hidden_return_slot),
                                                                   machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RDI)},
                                                      .opcode = MACHINE_X64_STORE_FRAME64,
                                                  });
            }
            for (u32 capture_pass = 0; capture_pass < 2 && selector.supported; capture_pass += 1)
            {
                bool float_pass = capture_pass == 1;
                for (u32 argument_index = 0; argument_index < BUSTER_ARRAY_LENGTH(selector.argument_values); argument_index += 1)
                {
                    u32 argument_value = selector.argument_values[argument_index];
                    if (argument_value == IR_ID_UNDERLYING_INVALID)
                    {
                        continue;
                    }
                    MachineX64ValueShape* shape = selector.parameter_shapes + argument_index;
                    MachineX64ArgumentPlacement* parameter_placement = selector.parameter_placements + argument_index;
                    u32 next_integer = parameter_placement->first_integer;
                    u32 next_float = parameter_placement->first_float;
                    if (parameter_placement->on_stack)
                    {
                        // Stack parameters copy from the caller-pushed area
                        // in the float pass, where the general-register
                        // scratches are already free.
                        if (!float_pass)
                        {
                            continue;
                        }
                        if (shape->aggregate)
                        {
                            u32 slot = selector.value_stack_slots[argument_value];
                            if (slot == UINT32_MAX)
                            {
                                machine_x64_reject(&selector, IR_OPCODE_ARGUMENT);
                                break;
                            }
                            for (u32 part_index = 0; part_index < parameter_placement->stack_part_count; part_index += 1)
                            {
                                u32 bounce_register = machine_x64_synthesize_register(&selector);
                                machine_x64_select_row(&selector, (MachineInstruction){
                                                                      .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                                      .payload = ((u32)parameter_placement->first_stack_part + part_index) * 8,
                                                                      .opcode = MACHINE_X64_LOAD_INCOMING,
                                                                  });
                                machine_x64_select_row(&selector, (MachineInstruction){
                                                                      .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                                                   machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                                      .payload = part_index * 8,
                                                                      .opcode = MACHINE_X64_STORE_FRAME64,
                                                                  });
                            }
                            continue;
                        }
                        u32 stack_argument_register = selector.value_virtual_registers[argument_value];
                        if (stack_argument_register == UINT32_MAX)
                        {
                            machine_x64_reject(&selector, IR_OPCODE_ARGUMENT);
                            break;
                        }
                        u32 incoming_row = machine_x64_select_row(&selector, (MachineInstruction){
                                                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER,
                                                                                                               stack_argument_register)},
                                                                                 .payload = (u32)parameter_placement->first_stack_part * 8,
                                                                                 .opcode = MACHINE_X64_LOAD_INCOMING,
                                                                             });
                        machine_x64_define(&selector, stack_argument_register, incoming_row);
                        continue;
                    }
                    if (shape->aggregate)
                    {
                        u32 slot = selector.value_stack_slots[argument_value];
                        if (slot == UINT32_MAX)
                        {
                            machine_x64_reject(&selector, IR_OPCODE_ARGUMENT);
                            break;
                        }
                        for (u32 part_index = 0; part_index < shape->part_count; part_index += 1)
                        {
                            bool part_float = shape->part_is_float[part_index] != 0;
                            u32 part_integer = next_integer;
                            u32 part_float_register = next_float;
                            next_integer += !part_float;
                            next_float += part_float;
                            if (part_float != float_pass)
                            {
                                continue;
                            }
                            if (part_float)
                            {
                                u32 bounce_register = machine_x64_synthesize_register(&selector);
                                machine_x64_select_row(&selector, (MachineInstruction){
                                                                      .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                                      .payload = part_float_register,
                                                                      .opcode = MACHINE_X64_MOVQ_FROM_XMM,
                                                                  });
                                machine_x64_select_row(&selector, (MachineInstruction){
                                                                      .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                                                   machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                                      .payload = shape->part_offsets[part_index],
                                                                      .opcode = MACHINE_X64_STORE_FRAME64,
                                                                  });
                            }
                            else
                            {
                                machine_x64_select_row(&selector,
                                                       (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                                        machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER,
                                                                                         machine_x64_system_v_arguments[part_integer])},
                                                           .payload = shape->part_offsets[part_index],
                                                           .opcode = MACHINE_X64_STORE_FRAME64,
                                                       });
                            }
                        }
                        continue;
                    }
                    bool scalar_float = shape->part_is_float[0] != 0;
                    if (scalar_float != float_pass)
                    {
                        continue;
                    }
                    u32 argument_register = selector.value_virtual_registers[argument_value];
                    if (argument_register == UINT32_MAX)
                    {
                        machine_x64_reject(&selector, IR_OPCODE_ARGUMENT);
                        break;
                    }
                    u32 row;
                    if (scalar_float)
                    {
                        row = machine_x64_select_row(&selector, (MachineInstruction){
                                                                    .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_register)},
                                                                    .payload = next_float,
                                                                    .opcode = MACHINE_X64_MOVQ_FROM_XMM,
                                                                });
                    }
                    else
                    {
                        row = machine_x64_select_row(&selector, (MachineInstruction){
                                                                    .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_register),
                                                                                 machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER,
                                                                                                  machine_x64_system_v_arguments[next_integer])},
                                                                    .opcode = MACHINE_X64_MOV_RR,
                                                                });
                    }
                    machine_x64_define(&selector, argument_register, row);
                }
            }
        }
        for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID && selector.supported; id = function->instructions[id.value].next)
        {
            IrInstruction* instruction = function->instructions + id.value;
            typed_instruction_count += 1;
            if (!machine_x64_select_instruction(&selector, instruction))
            {
                machine_x64_reject(&selector, instruction->opcode);
                break;
            }
        }
        machine_builder_block_end(&selector.builder, (MachineBlock){0});
    }
    if (!selector.supported)
    {
        result.failed_opcode = selector.failed_opcode;
        return result;
    }
    result.function = machine_function_builder_finish(arena, &selector.builder);
    result.function.immediates = arena_allocate(arena, u64, selector.immediates.total_count);
    result.function.immediate_count = selector.immediates.total_count;
    machine_stream_flatten(&selector.immediates, result.function.immediates);
    result.function.stack_slot_sizes = arena_allocate(arena, u32, selector.stack_slots.total_count);
    result.function.stack_slot_count = selector.stack_slots.total_count;
    machine_stream_flatten(&selector.stack_slots, result.function.stack_slot_sizes);
    result.function.stack_slot_alignments = arena_allocate(arena, u32, selector.stack_slot_alignments.total_count);
    machine_stream_flatten(&selector.stack_slot_alignments, result.function.stack_slot_alignments);
    result.function.call_targets = arena_allocate(arena, IrSymbolId, selector.call_targets.total_count);
    result.function.call_target_count = selector.call_targets.total_count;
    machine_stream_flatten(&selector.call_targets, result.function.call_targets);
    result.function.switch_cases = arena_allocate(arena, MachineSwitchCase, selector.switch_cases.total_count);
    result.function.switch_case_count = selector.switch_cases.total_count;
    machine_stream_flatten(&selector.switch_cases, result.function.switch_cases);
    // Only classification vregs need their definition patched; synthesized
    // temporaries carried their points from creation.
    for (u32 register_index = 0; register_index < selector.virtual_register_count; register_index += 1)
    {
        result.function.virtual_registers[register_index].definition_point = selector.virtual_register_definitions[register_index];
    }
    result.supported = true;
    result.returns_value = returns_value;
    result.selected_typed_instructions = typed_instruction_count;
    result.machine_instructions = result.function.instruction_count;
    return result;
}

MachineStackPlacement machine_stack_placement_build(Arena* arena, MachineFunction* function)
{
    MachineStackPlacement placement = {
        .virtual_register_offsets = arena_allocate(arena, u32, function->virtual_register_count),
        .stack_slot_offsets = arena_allocate(arena, u32, function->stack_slot_count),
        .operand_registers = arena_allocate(arena, u8, (u64)function->instruction_count * 4),
    };
    u32 running = 0;
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        running += 8;
        placement.virtual_register_offsets[register_index] = running;
    }
    for (u32 slot_index = 0; slot_index < function->stack_slot_count; slot_index += 1)
    {
        // The frame base is sixteen-aligned, so a slot whose start offset is
        // a multiple of its alignment is aligned in memory.
        u32 slot_alignment = function->stack_slot_alignments ? function->stack_slot_alignments[slot_index] : 8;
        running = (running + function->stack_slot_sizes[slot_index] + slot_alignment - 1) & ~(slot_alignment - 1);
        placement.stack_slot_offsets[slot_index] = running;
    }
    placement.frame_size = (running + 15u) & ~15u;
    // Stage-2 restriction: frames requiring guard-page probes fall back.
    if (placement.frame_size >= 4080)
    {
        return placement;
    }
    MachineBuilderStream edits;
    machine_stream_initialize(&edits, sizeof(MachineEdit));
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        MachineInstruction* instruction = function->instructions + instruction_index;
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        if (!info)
        {
            return placement;
        }
        for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(instruction->operands); slot += 1)
        {
            u8* operand_register = placement.operand_registers + (u64)instruction_index * 4 + slot;
            *operand_register = UINT8_MAX;
            if (slot >= info->operand_count)
            {
                continue;
            }
            u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
            MachineRef ref = instruction->operands[slot];
            MachineRefKind kind = machine_ref_kind(ref);
            if (kind == MACHINE_REF_PHYSICAL_REGISTER)
            {
                *operand_register = (u8)machine_ref_payload(ref);
                continue;
            }
            if (kind != MACHINE_REF_VIRTUAL_REGISTER || role == MACHINE_OPERAND_ROLE_NONE)
            {
                continue;
            }
            u32 virtual_register = machine_ref_payload(ref);
            *operand_register = machine_x64_slot_scratch[slot];
            // A copy into a fixed physical register reloads its source
            // directly into that register: argument sequences would
            // otherwise clobber already-placed argument registers through
            // the shared operand scratches.
            if (instruction->opcode == MACHINE_X64_MOV_RR && slot == 1 &&
                machine_ref_kind(instruction->operands[0]) == MACHINE_REF_PHYSICAL_REGISTER)
            {
                *operand_register = (u8)machine_ref_payload(instruction->operands[0]);
            }
            if (instruction->opcode == MACHINE_X64_CALL_INDIRECT)
            {
                // R10 survives the argument registers and the AL setup.
                *operand_register = MACHINE_X64_R10;
            }
            if (role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE)
            {
                MachineEdit* edit = (MachineEdit*)machine_stream_append(arena, &edits);
                *edit = (MachineEdit){
                    .point = machine_point_make(instruction_index, MACHINE_POINT_BEFORE),
                    .kind = MACHINE_EDIT_RELOAD,
                    .subject = virtual_register,
                    .location = *operand_register,
                };
                placement.reload_count += 1;
            }
        }
        for (u32 slot = 0; slot < info->operand_count; slot += 1)
        {
            u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
            MachineRef ref = instruction->operands[slot];
            if (machine_ref_kind(ref) != MACHINE_REF_VIRTUAL_REGISTER)
            {
                continue;
            }
            if (role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE)
            {
                MachineEdit* edit = (MachineEdit*)machine_stream_append(arena, &edits);
                *edit = (MachineEdit){
                    .point = machine_point_make(instruction_index, MACHINE_POINT_AFTER),
                    .kind = MACHINE_EDIT_SPILL,
                    .subject = machine_ref_payload(ref),
                    .location = machine_x64_slot_scratch[slot],
                };
                placement.spill_count += 1;
            }
        }
    }
    placement.edits = arena_allocate(arena, MachineEdit, edits.total_count);
    placement.edit_count = edits.total_count;
    machine_stream_flatten(&edits, placement.edits);
    placement.valid = true;
    return placement;
}

typedef struct MachineX64Encoder MachineX64Encoder;
struct MachineX64Encoder
{
    u8* bytes;
    u32 count;
    u32 capacity;
    bool overflow;
    u8 reserved[3];
};

typedef struct MachineX64BranchFixup MachineX64BranchFixup;
struct MachineX64BranchFixup
{
    u32 patch_offset;
    u32 block;
};

BUSTER_GLOBAL_LOCAL void machine_x64_emit8(MachineX64Encoder* encoder, u8 byte)
{
    if (encoder->count >= encoder->capacity)
    {
        encoder->overflow = true;
        return;
    }
    encoder->bytes[encoder->count] = byte;
    encoder->count += 1;
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit32(MachineX64Encoder* encoder, u32 value)
{
    for (u32 byte_index = 0; byte_index < 4; byte_index += 1)
    {
        machine_x64_emit8(encoder, (u8)(value >> (byte_index * 8)));
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit64(MachineX64Encoder* encoder, u64 value)
{
    for (u32 byte_index = 0; byte_index < 8; byte_index += 1)
    {
        machine_x64_emit8(encoder, (u8)(value >> (byte_index * 8)));
    }
}

BUSTER_GLOBAL_LOCAL u8 machine_x64_modrm_register(u32 reg, u32 rm)
{
    return (u8)(0xc0 | ((reg & 7) << 3) | (rm & 7));
}

// [rbp + disp32] addressing for frame slots; `offset` is the positive
// distance below the frame base.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_frame_modrm(MachineX64Encoder* encoder, u32 reg, u32 offset)
{
    machine_x64_emit8(encoder, (u8)(0x85 | ((reg & 7) << 3)));
    machine_x64_emit32(encoder, (u32)(0 - (s32)offset));
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_frame_load(MachineX64Encoder* encoder, u32 reg, u32 offset)
{
    machine_x64_emit8(encoder, (u8)(0x48 | (reg >= 8 ? 0x04 : 0)));
    machine_x64_emit8(encoder, 0x8b);
    machine_x64_emit_frame_modrm(encoder, reg, offset);
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_frame_store(MachineX64Encoder* encoder, u32 reg, u32 offset)
{
    machine_x64_emit8(encoder, (u8)(0x48 | (reg >= 8 ? 0x04 : 0)));
    machine_x64_emit8(encoder, 0x89);
    machine_x64_emit_frame_modrm(encoder, reg, offset);
}

// [base + disp] addressing for the copy loops; the bases are operand
// scratches, never RSP/RBP/R12/R13, so the plain ModRM forms suffice.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_memory_modrm(MachineX64Encoder* encoder, u32 reg, u32 base, u32 displacement)
{
    if (!displacement)
    {
        machine_x64_emit8(encoder, (u8)(((reg & 7) << 3) | (base & 7)));
    }
    else if (displacement <= INT8_MAX)
    {
        machine_x64_emit8(encoder, (u8)(0x40 | ((reg & 7) << 3) | (base & 7)));
        machine_x64_emit8(encoder, (u8)displacement);
    }
    else
    {
        machine_x64_emit8(encoder, (u8)(0x80 | ((reg & 7) << 3) | (base & 7)));
        machine_x64_emit32(encoder, displacement);
    }
}

// Sized chunk moves shared by the aggregate copy loops, chunked 8/4/2/1
// exactly like the canonical copy code: narrow loads zero-extend through
// movzx, narrow stores write their exact width.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_chunk_load_prefix(MachineX64Encoder* encoder, u32 chunk)
{
    if (chunk == 8)
    {
        machine_x64_emit8(encoder, 0x48);
    }
    if (chunk <= 2)
    {
        machine_x64_emit8(encoder, 0x0f);
        machine_x64_emit8(encoder, chunk == 1 ? 0xb6 : 0xb7);
    }
    else
    {
        machine_x64_emit8(encoder, 0x8b);
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_chunk_store_prefix(MachineX64Encoder* encoder, u32 chunk)
{
    if (chunk == 8)
    {
        machine_x64_emit8(encoder, 0x48);
    }
    else if (chunk == 2)
    {
        machine_x64_emit8(encoder, 0x66);
    }
    machine_x64_emit8(encoder, chunk == 1 ? 0x88 : 0x89);
}

BUSTER_GLOBAL_LOCAL u32 machine_x64_copy_chunk(u64 remaining)
{
    return remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
}

// Bit-exact 64-bit moves between the low XMM registers and general
// registers, used by the float rows whose values travel as bit patterns.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_movq_to_xmm(MachineX64Encoder* encoder, u32 xmm, u32 general)
{
    machine_x64_emit8(encoder, 0x66);
    machine_x64_emit8(encoder, (u8)(0x48 | (general >= 8 ? 0x01 : 0)));
    machine_x64_emit8(encoder, 0x0f);
    machine_x64_emit8(encoder, 0x6e);
    machine_x64_emit8(encoder, machine_x64_modrm_register(xmm, general));
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_movq_from_xmm(MachineX64Encoder* encoder, u32 xmm, u32 general)
{
    machine_x64_emit8(encoder, 0x66);
    machine_x64_emit8(encoder, (u8)(0x48 | (general >= 8 ? 0x01 : 0)));
    machine_x64_emit8(encoder, 0x0f);
    machine_x64_emit8(encoder, 0x7e);
    machine_x64_emit8(encoder, machine_x64_modrm_register(xmm, general));
}

// Two-operand register form with an explicit REX policy: `wide` forces
// REX.W, and extended registers add REX.R/REX.B as required.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_rr(MachineX64Encoder* encoder, bool wide, bool two_byte, u8 opcode, u32 reg, u32 rm)
{
    u8 rex = (u8)((wide ? 0x48 : 0x40) | (reg >= 8 ? 0x04 : 0) | (rm >= 8 ? 0x01 : 0));
    if (rex != 0x40)
    {
        machine_x64_emit8(encoder, rex);
    }
    if (two_byte)
    {
        machine_x64_emit8(encoder, 0x0f);
    }
    machine_x64_emit8(encoder, opcode);
    machine_x64_emit8(encoder, machine_x64_modrm_register(reg, rm));
}

MachineEncodeResult machine_encode_x86_64(Arena* arena, MachineFunction* function, MachineStackPlacement* placement)
{
    MachineEncodeResult result = {0};
    if (!placement->valid)
    {
        return result;
    }
    // Per-row worst-case byte budget: switches and aggregate copies expand
    // with their side data, everything else fits the flat row budget.
    u64 capacity64 = 64;
    for (u32 capacity_index = 0; capacity_index < function->instruction_count; capacity_index += 1)
    {
        MachineInstruction* capacity_row = function->instructions + capacity_index;
        switch (capacity_row->opcode)
        {
            break;
        case MACHINE_X64_SWITCH:
            capacity64 += (u64)capacity_row->flags * 24 + 8;
            break;
        case MACHINE_X64_COPY_FRAME_FROM_FRAME:
        case MACHINE_X64_COPY_FRAME_FROM_PTR:
        case MACHINE_X64_COPY_PTR_FROM_FRAME:
            capacity64 += ((u64)capacity_row->payload / 8 + 4) * 24;
            break;
        case MACHINE_X64_FCMP_SET:
            capacity64 += 40;
            break;
        case MACHINE_X64_ATOMIC_RMW:
            capacity64 += 48;
            break;
        default:
            capacity64 += 24;
        }
    }
    capacity64 += (u64)placement->edit_count * 8;
    if (capacity64 > UINT32_MAX)
    {
        return result;
    }
    MachineX64Encoder encoder = {
        .bytes = arena_allocate(arena, u8, capacity64),
        .capacity = (u32)capacity64,
    };
    MachineBuilderStream fixups;
    machine_stream_initialize(&fixups, sizeof(MachineX64BranchFixup));
    MachineBuilderStream call_sites;
    machine_stream_initialize(&call_sites, sizeof(MachineCallSite));
    result.block_offsets = arena_allocate(arena, u32, function->block_count);
    // Prologue: the frame base is RBP, matching the canonical path, and
    // the placement's callee-saved registers push right after it in fixed
    // RBX, R14, R15 order so the unwind actions can name exact offsets.
    machine_x64_emit8(&encoder, 0x55);
    machine_x64_emit8(&encoder, 0x48);
    machine_x64_emit8(&encoder, 0x89);
    machine_x64_emit8(&encoder, 0xe5);
    if (placement->callee_saved_mask & (1u << MACHINE_X64_RBX))
    {
        machine_x64_emit8(&encoder, 0x53);
    }
    if (placement->callee_saved_mask & (1u << MACHINE_X64_R14))
    {
        machine_x64_emit8(&encoder, 0x41);
        machine_x64_emit8(&encoder, 0x56);
    }
    if (placement->callee_saved_mask & (1u << MACHINE_X64_R15))
    {
        machine_x64_emit8(&encoder, 0x41);
        machine_x64_emit8(&encoder, 0x57);
    }
    if (placement->frame_size)
    {
        machine_x64_emit8(&encoder, 0x48);
        machine_x64_emit8(&encoder, 0x81);
        machine_x64_emit8(&encoder, 0xec);
        machine_x64_emit32(&encoder, placement->frame_size);
    }
    u32 edit_cursor = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        result.block_offsets[block_index] = encoder.count;
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            u32 instruction_index = block->first_instruction + offset;
            MachineInstruction* instruction = function->instructions + instruction_index;
            u8 const* operand_registers = placement->operand_registers + (u64)instruction_index * 4;
            MachinePoint before = machine_point_make(instruction_index, MACHINE_POINT_BEFORE);
            while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == before)
            {
                MachineEdit* edit = placement->edits + edit_cursor;
                if (edit->kind == MACHINE_EDIT_SPILL)
                {
                    machine_x64_emit_frame_store(&encoder, edit->location, placement->virtual_register_offsets[edit->subject]);
                }
                else if (edit->kind == MACHINE_EDIT_COPY)
                {
                    machine_x64_emit_rr(&encoder, true, false, 0x89, edit->subject, edit->location);
                }
                else
                {
                    machine_x64_emit_frame_load(&encoder, edit->location, placement->virtual_register_offsets[edit->subject]);
                }
                edit_cursor += 1;
            }
            switch (instruction->opcode)
            {
                break;
            case MACHINE_X64_MOV_RI:
            {
                u32 reg = operand_registers[0];
                machine_x64_emit8(&encoder, (u8)(0x48 | (reg >= 8 ? 0x01 : 0)));
                machine_x64_emit8(&encoder, (u8)(0xb8 | (reg & 7)));
                machine_x64_emit64(&encoder, function->immediates[machine_ref_payload(instruction->operands[1])]);
            }
            break;
            case MACHINE_X64_MOV_RR:
                machine_x64_emit_rr(&encoder, true, false, 0x89, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_MOV32_RR:
                machine_x64_emit_rr(&encoder, false, false, 0x89, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_MOVSX8_RR:
                machine_x64_emit_rr(&encoder, true, true, 0xbe, operand_registers[0], operand_registers[1]);
                break;
            case MACHINE_X64_MOVSX16_RR:
                machine_x64_emit_rr(&encoder, true, true, 0xbf, operand_registers[0], operand_registers[1]);
                break;
            case MACHINE_X64_MOVSX32_RR:
                machine_x64_emit_rr(&encoder, true, false, 0x63, operand_registers[0], operand_registers[1]);
                break;
            case MACHINE_X64_MOVZX8_RR:
                machine_x64_emit_rr(&encoder, true, true, 0xb6, operand_registers[0], operand_registers[1]);
                break;
            case MACHINE_X64_MOVZX16_RR:
                machine_x64_emit_rr(&encoder, true, true, 0xb7, operand_registers[0], operand_registers[1]);
                break;
            case MACHINE_X64_ADD32:
                machine_x64_emit_rr(&encoder, false, false, 0x01, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_ADD64:
                machine_x64_emit_rr(&encoder, true, false, 0x01, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_SUB32:
                machine_x64_emit_rr(&encoder, false, false, 0x29, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_SUB64:
                machine_x64_emit_rr(&encoder, true, false, 0x29, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_AND32:
                machine_x64_emit_rr(&encoder, false, false, 0x21, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_AND64:
                machine_x64_emit_rr(&encoder, true, false, 0x21, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_OR32:
                machine_x64_emit_rr(&encoder, false, false, 0x09, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_OR64:
                machine_x64_emit_rr(&encoder, true, false, 0x09, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_XOR32:
                machine_x64_emit_rr(&encoder, false, false, 0x31, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_XOR64:
                machine_x64_emit_rr(&encoder, true, false, 0x31, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_IMUL32:
                machine_x64_emit_rr(&encoder, false, true, 0xaf, operand_registers[0], operand_registers[1]);
                break;
            case MACHINE_X64_IMUL64:
                machine_x64_emit_rr(&encoder, true, true, 0xaf, operand_registers[0], operand_registers[1]);
                break;
            case MACHINE_X64_NEG32:
                machine_x64_emit_rr(&encoder, false, false, 0xf7, 3, operand_registers[0]);
                break;
            case MACHINE_X64_NEG64:
                machine_x64_emit_rr(&encoder, true, false, 0xf7, 3, operand_registers[0]);
                break;
            case MACHINE_X64_NOT32:
                machine_x64_emit_rr(&encoder, false, false, 0xf7, 2, operand_registers[0]);
                break;
            case MACHINE_X64_NOT64:
                machine_x64_emit_rr(&encoder, true, false, 0xf7, 2, operand_registers[0]);
                break;
            case MACHINE_X64_CMP32:
                machine_x64_emit_rr(&encoder, false, false, 0x39, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_CMP64:
                machine_x64_emit_rr(&encoder, true, false, 0x39, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_TEST_RR:
                machine_x64_emit_rr(&encoder, true, false, 0x85, operand_registers[1], operand_registers[0]);
                break;
            case MACHINE_X64_SHL32:
            case MACHINE_X64_SHL64:
            case MACHINE_X64_SAR32:
            case MACHINE_X64_SAR64:
            case MACHINE_X64_SHR32:
            case MACHINE_X64_SHR64:
            {
                // The count operand must sit in CL; the stage-2 placement
                // pins operand slot 1 to RCX by construction.
                bool wide = instruction->opcode == MACHINE_X64_SHL64 || instruction->opcode == MACHINE_X64_SAR64 || instruction->opcode == MACHINE_X64_SHR64;
                u8 form = instruction->opcode == MACHINE_X64_SHL32 || instruction->opcode == MACHINE_X64_SHL64   ? 4
                          : instruction->opcode == MACHINE_X64_SAR32 || instruction->opcode == MACHINE_X64_SAR64 ? 7
                                                                                                                 : 5;
                machine_x64_emit_rr(&encoder, wide, false, 0xd3, form, operand_registers[0]);
            }
            break;
            case MACHINE_X64_SDIV32:
            case MACHINE_X64_SDIV64:
            case MACHINE_X64_SREM32:
            case MACHINE_X64_SREM64:
            case MACHINE_X64_UDIV32:
            case MACHINE_X64_UDIV64:
            case MACHINE_X64_UREM32:
            case MACHINE_X64_UREM64:
            {
                // Dividend and result live in RAX (operand slot 0); the
                // divisor is in RCX (slot 1); RDX is clobbered, exactly like
                // the canonical divide sequences.
                bool wide = instruction->opcode == MACHINE_X64_SDIV64 || instruction->opcode == MACHINE_X64_SREM64 ||
                            instruction->opcode == MACHINE_X64_UDIV64 || instruction->opcode == MACHINE_X64_UREM64;
                bool is_signed = instruction->opcode == MACHINE_X64_SDIV32 || instruction->opcode == MACHINE_X64_SDIV64 ||
                                 instruction->opcode == MACHINE_X64_SREM32 || instruction->opcode == MACHINE_X64_SREM64;
                bool remainder = instruction->opcode == MACHINE_X64_SREM32 || instruction->opcode == MACHINE_X64_SREM64 ||
                                 instruction->opcode == MACHINE_X64_UREM32 || instruction->opcode == MACHINE_X64_UREM64;
                if (is_signed)
                {
                    if (wide)
                    {
                        machine_x64_emit8(&encoder, 0x48);
                    }
                    machine_x64_emit8(&encoder, 0x99);
                }
                else
                {
                    if (wide)
                    {
                        machine_x64_emit8(&encoder, 0x48);
                    }
                    machine_x64_emit8(&encoder, 0x31);
                    machine_x64_emit8(&encoder, 0xd2);
                }
                if (wide)
                {
                    machine_x64_emit8(&encoder, 0x48);
                }
                machine_x64_emit8(&encoder, 0xf7);
                machine_x64_emit8(&encoder, (u8)(is_signed ? 0xf9 : 0xf1));
                if (remainder)
                {
                    if (wide)
                    {
                        machine_x64_emit8(&encoder, 0x48);
                    }
                    machine_x64_emit8(&encoder, 0x89);
                    machine_x64_emit8(&encoder, 0xd0);
                }
            }
            break;
            case MACHINE_X64_SETCC:
            {
                // The stage-2 placement pins SETCC's destination to RAX so
                // the low-byte encoding never touches the legacy AH family.
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, (u8)(0x90 | (instruction->payload & 0xf)));
                machine_x64_emit8(&encoder, 0xc0);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0xb6);
                machine_x64_emit8(&encoder, 0xc0);
            }
            break;
            case MACHINE_X64_LOAD_FRAME:
                machine_x64_emit_frame_load(&encoder, operand_registers[0],
                                            placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])] - instruction->payload);
                break;
            case MACHINE_X64_STORE_FRAME8:
            case MACHINE_X64_STORE_FRAME16:
            case MACHINE_X64_STORE_FRAME32:
            case MACHINE_X64_STORE_FRAME64:
            {
                u32 slot_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])] - instruction->payload;
                u32 value_register = operand_registers[1];
                if (instruction->opcode == MACHINE_X64_STORE_FRAME16)
                {
                    machine_x64_emit8(&encoder, 0x66);
                }
                u8 rex = (u8)(0x40 | (value_register >= 8 ? 0x04 : 0) | (instruction->opcode == MACHINE_X64_STORE_FRAME64 ? 0x08 : 0));
                if (rex != 0x40 || (instruction->opcode == MACHINE_X64_STORE_FRAME8 &&
                                    (value_register == MACHINE_X64_RSI || value_register == MACHINE_X64_RDI)))
                {
                    machine_x64_emit8(&encoder, rex);
                }
                machine_x64_emit8(&encoder, instruction->opcode == MACHINE_X64_STORE_FRAME8 ? 0x88 : 0x89);
                machine_x64_emit_frame_modrm(&encoder, value_register, slot_offset);
            }
            break;
            case MACHINE_X64_LOAD_PTR8:
            case MACHINE_X64_LOAD_PTR16:
            case MACHINE_X64_LOAD_PTR32:
            case MACHINE_X64_LOAD_PTR64:
            {
                u32 destination = operand_registers[0];
                u32 address = operand_registers[1];
                u8 rex = (u8)(0x40 | (destination >= 8 ? 0x04 : 0) | (address >= 8 ? 0x01 : 0));
                if (instruction->opcode == MACHINE_X64_LOAD_PTR8 || instruction->opcode == MACHINE_X64_LOAD_PTR16)
                {
                    machine_x64_emit8(&encoder, (u8)(rex | 0x08));
                    machine_x64_emit8(&encoder, 0x0f);
                    machine_x64_emit8(&encoder, instruction->opcode == MACHINE_X64_LOAD_PTR8 ? 0xb6 : 0xb7);
                }
                else
                {
                    if (instruction->opcode == MACHINE_X64_LOAD_PTR64)
                    {
                        rex |= 0x08;
                    }
                    if (rex != 0x40)
                    {
                        machine_x64_emit8(&encoder, rex);
                    }
                    machine_x64_emit8(&encoder, 0x8b);
                }
                machine_x64_emit8(&encoder, (u8)(((destination & 7) << 3) | (address & 7)));
            }
            break;
            case MACHINE_X64_STORE_PTR8:
            case MACHINE_X64_STORE_PTR16:
            case MACHINE_X64_STORE_PTR32:
            case MACHINE_X64_STORE_PTR64:
            {
                u32 address = operand_registers[0];
                u32 value_register = operand_registers[1];
                if (instruction->opcode == MACHINE_X64_STORE_PTR16)
                {
                    machine_x64_emit8(&encoder, 0x66);
                }
                u8 rex = (u8)(0x40 | (value_register >= 8 ? 0x04 : 0) | (address >= 8 ? 0x01 : 0));
                if (instruction->opcode == MACHINE_X64_STORE_PTR64)
                {
                    rex |= 0x08;
                }
                // Byte stores to SIL/DIL and every extended register need the
                // REX prefix even without W.
                if (rex != 0x40 || (instruction->opcode == MACHINE_X64_STORE_PTR8 && (value_register == MACHINE_X64_RSI || value_register == MACHINE_X64_RDI)))
                {
                    machine_x64_emit8(&encoder, rex);
                }
                machine_x64_emit8(&encoder, instruction->opcode == MACHINE_X64_STORE_PTR8 ? 0x88 : 0x89);
                machine_x64_emit8(&encoder, (u8)(((value_register & 7) << 3) | (address & 7)));
            }
            break;
            case MACHINE_X64_JMP:
            {
                machine_x64_emit8(&encoder, 0xe9);
                MachineX64BranchFixup* fixup = (MachineX64BranchFixup*)machine_stream_append(arena, &fixups);
                *fixup = (MachineX64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[0]),
                };
                machine_x64_emit32(&encoder, 0);
            }
            break;
            case MACHINE_X64_JCC:
            {
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, (u8)(0x80 | (instruction->payload & 0xf)));
                MachineX64BranchFixup* taken = (MachineX64BranchFixup*)machine_stream_append(arena, &fixups);
                *taken = (MachineX64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[0]),
                };
                machine_x64_emit32(&encoder, 0);
                machine_x64_emit8(&encoder, 0xe9);
                MachineX64BranchFixup* fallthrough = (MachineX64BranchFixup*)machine_stream_append(arena, &fixups);
                *fallthrough = (MachineX64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[1]),
                };
                machine_x64_emit32(&encoder, 0);
            }
            break;
            case MACHINE_X64_RET:
            {
                if (placement->callee_saved_mask)
                {
                    // Point RSP at the pushed registers, restore them in
                    // reverse push order, then unwind the frame base.
                    u32 push_count = 0;
                    for (u32 push_register = 0; push_register < MACHINE_X64_REGISTER_COUNT; push_register += 1)
                    {
                        push_count += (placement->callee_saved_mask >> push_register) & 1u;
                    }
                    machine_x64_emit8(&encoder, 0x48);
                    machine_x64_emit8(&encoder, 0x8d);
                    machine_x64_emit8(&encoder, 0x65);
                    machine_x64_emit8(&encoder, (u8)(0x100u - 8u * push_count));
                    if (placement->callee_saved_mask & (1u << MACHINE_X64_R15))
                    {
                        machine_x64_emit8(&encoder, 0x41);
                        machine_x64_emit8(&encoder, 0x5f);
                    }
                    if (placement->callee_saved_mask & (1u << MACHINE_X64_R14))
                    {
                        machine_x64_emit8(&encoder, 0x41);
                        machine_x64_emit8(&encoder, 0x5e);
                    }
                    if (placement->callee_saved_mask & (1u << MACHINE_X64_RBX))
                    {
                        machine_x64_emit8(&encoder, 0x5b);
                    }
                }
                else
                {
                    machine_x64_emit8(&encoder, 0x48);
                    machine_x64_emit8(&encoder, 0x89);
                    machine_x64_emit8(&encoder, 0xec);
                }
                machine_x64_emit8(&encoder, 0x5d);
                machine_x64_emit8(&encoder, 0xc3);
            }
            break;
            case MACHINE_X64_CALL_DIRECT:
            {
                if (instruction->flags & 1)
                {
                    // Variadic System V call: AL carries the count of XMM
                    // registers holding arguments.
                    machine_x64_emit8(&encoder, 0xb8);
                    machine_x64_emit32(&encoder, (u32)(instruction->flags >> 1));
                }
                machine_x64_emit8(&encoder, 0xe8);
                MachineCallSite* site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                *site = (MachineCallSite){
                    .code_offset = encoder.count,
                    .target = instruction->payload,
                };
                machine_x64_emit32(&encoder, 0);
            }
            break;
            case MACHINE_X64_CALL_INDIRECT:
            {
                if (instruction->flags & 1)
                {
                    machine_x64_emit8(&encoder, 0xb8);
                    machine_x64_emit32(&encoder, (u32)(instruction->flags >> 1));
                }
                if (operand_registers[0] >= 8)
                {
                    machine_x64_emit8(&encoder, 0x41);
                }
                machine_x64_emit8(&encoder, 0xff);
                machine_x64_emit8(&encoder, (u8)(0xd0 | (operand_registers[0] & 7)));
            }
            break;
            case MACHINE_X64_LEA_FRAME:
            {
                machine_x64_emit8(&encoder, (u8)(0x48 | (operand_registers[0] >= 8 ? 0x04 : 0)));
                machine_x64_emit8(&encoder, 0x8d);
                machine_x64_emit_frame_modrm(&encoder, operand_registers[0],
                                             placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])]);
            }
            break;
            case MACHINE_X64_LEA_SYMBOL:
            {
                machine_x64_emit8(&encoder, (u8)(0x48 | (operand_registers[0] >= 8 ? 0x04 : 0)));
                machine_x64_emit8(&encoder, 0x8d);
                machine_x64_emit8(&encoder, (u8)(0x05 | ((operand_registers[0] & 7) << 3)));
                MachineCallSite* site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                *site = (MachineCallSite){
                    .code_offset = encoder.count,
                    .target = instruction->payload,
                };
                machine_x64_emit32(&encoder, 0);
            }
            break;
            case MACHINE_X64_COPY_FRAME_FROM_FRAME:
            {
                u32 destination_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])];
                u32 source_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])];
                u32 copied = 0;
                while (copied < instruction->payload)
                {
                    u32 chunk = machine_x64_copy_chunk(instruction->payload - copied);
                    machine_x64_emit_chunk_load_prefix(&encoder, chunk);
                    machine_x64_emit_frame_modrm(&encoder, MACHINE_X64_RAX, source_offset - copied);
                    machine_x64_emit_chunk_store_prefix(&encoder, chunk);
                    machine_x64_emit_frame_modrm(&encoder, MACHINE_X64_RAX, destination_offset - copied);
                    copied += chunk;
                }
            }
            break;
            case MACHINE_X64_COPY_FRAME_FROM_PTR:
            {
                u32 destination_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])];
                u32 source_register = operand_registers[1];
                u32 copied = 0;
                while (copied < instruction->payload)
                {
                    u32 chunk = machine_x64_copy_chunk(instruction->payload - copied);
                    if (source_register >= 8)
                    {
                        machine_x64_emit8(&encoder, (u8)(chunk == 8 ? 0x49 : 0x41));
                        machine_x64_emit8(&encoder, chunk <= 2 ? 0x0f : 0x8b);
                        if (chunk <= 2)
                        {
                            machine_x64_emit8(&encoder, chunk == 1 ? 0xb6 : 0xb7);
                        }
                    }
                    else
                    {
                        machine_x64_emit_chunk_load_prefix(&encoder, chunk);
                    }
                    machine_x64_emit_memory_modrm(&encoder, MACHINE_X64_RAX, source_register, copied);
                    machine_x64_emit_chunk_store_prefix(&encoder, chunk);
                    machine_x64_emit_frame_modrm(&encoder, MACHINE_X64_RAX, destination_offset - copied);
                    copied += chunk;
                }
            }
            break;
            case MACHINE_X64_COPY_PTR_FROM_FRAME:
            {
                u32 destination_register = operand_registers[0];
                u32 source_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])];
                u32 copied = 0;
                while (copied < instruction->payload)
                {
                    u32 chunk = machine_x64_copy_chunk(instruction->payload - copied);
                    machine_x64_emit_chunk_load_prefix(&encoder, chunk);
                    machine_x64_emit_frame_modrm(&encoder, MACHINE_X64_RDX, source_offset - copied);
                    if (destination_register >= 8)
                    {
                        if (chunk == 2)
                        {
                            machine_x64_emit8(&encoder, 0x66);
                        }
                        machine_x64_emit8(&encoder, (u8)(chunk == 8 ? 0x49 : 0x41));
                        machine_x64_emit8(&encoder, chunk == 1 ? 0x88 : 0x89);
                    }
                    else
                    {
                        machine_x64_emit_chunk_store_prefix(&encoder, chunk);
                    }
                    machine_x64_emit_memory_modrm(&encoder, MACHINE_X64_RDX, destination_register, copied);
                    copied += chunk;
                }
            }
            break;
            case MACHINE_X64_FARITH:
            {
                machine_x64_emit_movq_to_xmm(&encoder, 0, operand_registers[1]);
                machine_x64_emit_movq_to_xmm(&encoder, 1, operand_registers[2]);
                machine_x64_emit8(&encoder, (instruction->payload & 0x100) ? 0xf2 : 0xf3);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, (u8)instruction->payload);
                machine_x64_emit8(&encoder, 0xc1);
                machine_x64_emit_movq_from_xmm(&encoder, 0, operand_registers[0]);
            }
            break;
            case MACHINE_X64_FCMP_SET:
            {
                // comis + setcc with the canonical NaN-parity fixups; the
                // destination is pinned to RAX so AL/DL stay legal.
                machine_x64_emit_movq_to_xmm(&encoder, 0, operand_registers[1]);
                machine_x64_emit_movq_to_xmm(&encoder, 1, operand_registers[2]);
                if (instruction->payload & 0x100)
                {
                    machine_x64_emit8(&encoder, 0x66);
                }
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x2e);
                machine_x64_emit8(&encoder, 0xc1);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, (u8)(0x90 | (instruction->payload & 0xf)));
                machine_x64_emit8(&encoder, 0xc0);
                u32 parity_mode = (instruction->payload >> 9) & 0x3;
                if (parity_mode)
                {
                    machine_x64_emit8(&encoder, 0x0f);
                    machine_x64_emit8(&encoder, parity_mode == 1 ? 0x9b : 0x9a);
                    machine_x64_emit8(&encoder, 0xc2);
                    machine_x64_emit8(&encoder, parity_mode == 1 ? 0x20 : 0x08);
                    machine_x64_emit8(&encoder, 0xd0);
                }
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0xb6);
                machine_x64_emit8(&encoder, 0xc0);
            }
            break;
            case MACHINE_X64_CVT_F32_TO_F64:
            case MACHINE_X64_CVT_F64_TO_F32:
            {
                machine_x64_emit_movq_to_xmm(&encoder, 0, operand_registers[1]);
                machine_x64_emit8(&encoder, instruction->opcode == MACHINE_X64_CVT_F32_TO_F64 ? 0xf3 : 0xf2);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x5a);
                machine_x64_emit8(&encoder, 0xc0);
                machine_x64_emit_movq_from_xmm(&encoder, 0, operand_registers[0]);
            }
            break;
            case MACHINE_X64_CVT_I64_TO_F32:
            case MACHINE_X64_CVT_I64_TO_F64:
            {
                machine_x64_emit8(&encoder, instruction->opcode == MACHINE_X64_CVT_I64_TO_F32 ? 0xf3 : 0xf2);
                machine_x64_emit8(&encoder, (u8)(0x48 | (operand_registers[1] >= 8 ? 0x01 : 0)));
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x2a);
                machine_x64_emit8(&encoder, machine_x64_modrm_register(0, operand_registers[1]));
                machine_x64_emit_movq_from_xmm(&encoder, 0, operand_registers[0]);
            }
            break;
            case MACHINE_X64_CVT_F32_TO_I64:
            case MACHINE_X64_CVT_F64_TO_I64:
            {
                machine_x64_emit_movq_to_xmm(&encoder, 0, operand_registers[1]);
                machine_x64_emit8(&encoder, instruction->opcode == MACHINE_X64_CVT_F32_TO_I64 ? 0xf3 : 0xf2);
                machine_x64_emit8(&encoder, (u8)(0x48 | (operand_registers[0] >= 8 ? 0x04 : 0)));
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x2c);
                machine_x64_emit8(&encoder, machine_x64_modrm_register(operand_registers[0], 0));
            }
            break;
            case MACHINE_X64_MOVQ_TO_XMM:
                machine_x64_emit_movq_to_xmm(&encoder, instruction->payload, operand_registers[0]);
                break;
            case MACHINE_X64_MOVQ_FROM_XMM:
                machine_x64_emit_movq_from_xmm(&encoder, instruction->payload, operand_registers[0]);
                break;
            case MACHINE_X64_LOAD_INCOMING:
            {
                // The incoming argument area sits past the saved frame base
                // and return address.
                u32 destination = operand_registers[0];
                machine_x64_emit8(&encoder, (u8)(0x48 | (destination >= 8 ? 0x04 : 0)));
                machine_x64_emit8(&encoder, 0x8b);
                machine_x64_emit8(&encoder, (u8)(0x85 | ((destination & 7) << 3)));
                machine_x64_emit32(&encoder, 16 + instruction->payload);
            }
            break;
            case MACHINE_X64_PUSH_FRAME:
            {
                machine_x64_emit8(&encoder, 0xff);
                machine_x64_emit_frame_modrm(&encoder, 6, placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])] -
                                                             instruction->payload);
            }
            break;
            case MACHINE_X64_PUSH_REGISTER:
            {
                u32 source = operand_registers[0];
                if (source >= 8)
                {
                    machine_x64_emit8(&encoder, 0x41);
                }
                machine_x64_emit8(&encoder, (u8)(0x50 | (source & 7)));
            }
            break;
            case MACHINE_X64_SUB_RSP:
            {
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x81);
                machine_x64_emit8(&encoder, 0xec);
                machine_x64_emit32(&encoder, instruction->payload);
            }
            break;
            case MACHINE_X64_ADD_RSP:
            {
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x81);
                machine_x64_emit8(&encoder, 0xc4);
                machine_x64_emit32(&encoder, instruction->payload);
            }
            break;
            case MACHINE_X64_ATOMIC_STORE_XCHG:
            {
                u32 size = instruction->payload & 0xff;
                if (size == 2)
                {
                    machine_x64_emit8(&encoder, 0x66);
                }
                if (size == 8)
                {
                    machine_x64_emit8(&encoder, 0x48);
                }
                machine_x64_emit8(&encoder, size == 1 ? 0x86 : 0x87);
                machine_x64_emit8(&encoder, 0x08);
            }
            break;
            case MACHINE_X64_ATOMIC_RMW:
            {
                // Sized load, then a lock cmpxchg retry loop with R8 holding
                // the proposed value, mirroring the canonical sequence.
                u32 size = instruction->payload & 0xff;
                u32 atomic_operation = instruction->payload >> 8;
                machine_x64_emit_chunk_load_prefix(&encoder, size);
                machine_x64_emit_memory_modrm(&encoder, MACHINE_X64_RAX, MACHINE_X64_RCX, 0);
                u32 retry_offset = encoder.count;
                if (size == 2)
                {
                    machine_x64_emit8(&encoder, 0x66);
                }
                machine_x64_emit8(&encoder, size == 8 ? 0x49 : 0x41);
                machine_x64_emit8(&encoder, size == 1 ? 0x88 : 0x89);
                machine_x64_emit8(&encoder, 0xc0);
                if (size == 2)
                {
                    machine_x64_emit8(&encoder, 0x66);
                }
                machine_x64_emit8(&encoder, size == 8 ? 0x49 : 0x41);
                if (atomic_operation == IR_ATOMIC_EXCHANGE)
                {
                    machine_x64_emit8(&encoder, size == 1 ? 0x88 : 0x89);
                }
                else
                {
                    u8 operation_opcode = atomic_operation == IR_ATOMIC_ADD           ? (size == 1 ? 0x00 : 0x01)
                                          : atomic_operation == IR_ATOMIC_SUBTRACT    ? (size == 1 ? 0x28 : 0x29)
                                          : atomic_operation == IR_ATOMIC_BITWISE_AND ? (size == 1 ? 0x20 : 0x21)
                                          : atomic_operation == IR_ATOMIC_BITWISE_OR  ? (size == 1 ? 0x08 : 0x09)
                                                                                      : (size == 1 ? 0x30 : 0x31);
                    machine_x64_emit8(&encoder, operation_opcode);
                }
                machine_x64_emit8(&encoder, 0xd0);
                if (size == 2)
                {
                    machine_x64_emit8(&encoder, 0x66);
                }
                machine_x64_emit8(&encoder, 0xf0);
                machine_x64_emit8(&encoder, size == 8 ? 0x4c : 0x44);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, size == 1 ? 0xb0 : 0xb1);
                machine_x64_emit8(&encoder, 0x01);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x85);
                machine_x64_emit32(&encoder, retry_offset - (encoder.count + 4));
            }
            break;
            case MACHINE_X64_ATOMIC_CMPXCHG:
            {
                u32 size = instruction->payload & 0xff;
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x89);
                machine_x64_emit8(&encoder, 0xd0);
                if (size == 2)
                {
                    machine_x64_emit8(&encoder, 0x66);
                }
                machine_x64_emit8(&encoder, 0xf0);
                if (size == 8)
                {
                    machine_x64_emit8(&encoder, 0x48);
                }
                else if (size == 1)
                {
                    // SIL needs a plain REX prefix.
                    machine_x64_emit8(&encoder, 0x40);
                }
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, size == 1 ? 0xb0 : 0xb1);
                machine_x64_emit8(&encoder, 0x31);
            }
            break;
            case MACHINE_X64_MFENCE:
            {
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0xae);
                machine_x64_emit8(&encoder, 0xf0);
            }
            break;
            case MACHINE_X64_SWITCH:
            {
                // The canonical compare chain: the condition sits in the
                // operand scratch, each case constant is materialized in
                // RCX, equality jumps to the case block, and the tail jump
                // takes the default edge.
                u32 condition = operand_registers[0];
                for (u32 case_index = 0; case_index < instruction->flags; case_index += 1)
                {
                    MachineSwitchCase* case_row = function->switch_cases + instruction->payload + case_index;
                    machine_x64_emit8(&encoder, 0x48);
                    machine_x64_emit8(&encoder, 0xb9);
                    machine_x64_emit64(&encoder, case_row->value);
                    machine_x64_emit_rr(&encoder, true, false, 0x39, MACHINE_X64_RCX, condition);
                    machine_x64_emit8(&encoder, 0x0f);
                    machine_x64_emit8(&encoder, 0x84);
                    MachineX64BranchFixup* case_fixup = (MachineX64BranchFixup*)machine_stream_append(arena, &fixups);
                    *case_fixup = (MachineX64BranchFixup){
                        .patch_offset = encoder.count,
                        .block = case_row->target_block,
                    };
                    machine_x64_emit32(&encoder, 0);
                }
                machine_x64_emit8(&encoder, 0xe9);
                MachineX64BranchFixup* default_fixup = (MachineX64BranchFixup*)machine_stream_append(arena, &fixups);
                *default_fixup = (MachineX64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[1]),
                };
                machine_x64_emit32(&encoder, 0);
            }
            break;
            default:
                return result;
            }
            MachinePoint after = machine_point_make(instruction_index, MACHINE_POINT_AFTER);
            while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == after)
            {
                MachineEdit* edit = placement->edits + edit_cursor;
                if (edit->kind == MACHINE_EDIT_RELOAD)
                {
                    machine_x64_emit_frame_load(&encoder, edit->location, placement->virtual_register_offsets[edit->subject]);
                }
                else if (edit->kind == MACHINE_EDIT_COPY)
                {
                    machine_x64_emit_rr(&encoder, true, false, 0x89, edit->subject, edit->location);
                }
                else
                {
                    machine_x64_emit_frame_store(&encoder, edit->location, placement->virtual_register_offsets[edit->subject]);
                }
                edit_cursor += 1;
            }
        }
    }
    if (encoder.overflow)
    {
        return result;
    }
    for (MachineBuilderChunk* chunk = fixups.first; chunk; chunk = chunk->next)
    {
        MachineX64BranchFixup* rows = (MachineX64BranchFixup*)(chunk + 1);
        for (u32 row_index = 0; row_index < chunk->count; row_index += 1)
        {
            MachineX64BranchFixup* fixup = rows + row_index;
            u32 displacement = result.block_offsets[fixup->block] - (fixup->patch_offset + 4);
            memcpy(encoder.bytes + fixup->patch_offset, &displacement, sizeof(displacement));
        }
    }
    result.call_sites = arena_allocate(arena, MachineCallSite, call_sites.total_count);
    result.call_site_count = call_sites.total_count;
    machine_stream_flatten(&call_sites, result.call_sites);
    result.bytes = encoder.bytes;
    result.byte_count = encoder.count;
    result.valid = true;
    return result;
}
