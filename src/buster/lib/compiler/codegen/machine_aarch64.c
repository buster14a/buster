// AArch64 machine selection and encoding. Included by machine.c in the
// backend-implementation-file pattern; not a standalone translation unit.
// The initial subset covers scalar integer functions: arguments, constants,
// casts, unary and binary arithmetic including divides and shifts,
// comparisons, direct locals and pointer dereference, member addresses,
// branches, and scalar returns. Everything else is an explicit unsupported
// result, never a silent misselection.
//
// Register conventions: values live zero-extended in X registers exactly
// like the x86-64 register model. X28 is the frame base (the canonical
// AArch64 path's convention), X29/X30 the frame-pointer pair, X16/X17 the
// encoder's address scratches (and the linker's veneer registers), X18 the
// platform register — none are allocatable. Frame slots sit at positive
// X28-relative offsets; a placement offset o addresses X28 + frame_size - o,
// which keeps the shared placement's grows-down offset convention intact.

#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

// Supported argument-list length: eight integer register slots.
#define MACHINE_A64_MAX_ARGUMENTS 8

// Frames whose x28 save offset no longer fits the scaled 8-byte store stay
// on the canonical path; the exact prologue-cursor accounting in the module
// wiring depends on every prologue instruction being one word.
#define MACHINE_A64_MAX_FRAME_BYTES 32744u

BUSTER_CT_CHECK(MACHINE_A64_REGISTER_COUNT <= MACHINE_TARGET_REGISTER_LIMIT);

// The register file and special-opcode identities the shared allocators
// consume. The callee-saved members X19-X27 cost one prologue save per
// function that binds them, and their saves carry unwind actions; X28
// stays reserved as the frame base.
BUSTER_GLOBAL_LOCAL MachineTargetDescription const machine_aarch64_description = {
    .allocatable_mask = (1u << MACHINE_A64_X0) | (1u << MACHINE_A64_X1) | (1u << MACHINE_A64_X2) | (1u << MACHINE_A64_X3) | (1u << MACHINE_A64_X4) |
                        (1u << MACHINE_A64_X5) | (1u << MACHINE_A64_X6) | (1u << MACHINE_A64_X7) | (1u << MACHINE_A64_X8) | (1u << MACHINE_A64_X9) |
                        (1u << MACHINE_A64_X10) | (1u << MACHINE_A64_X11) | (1u << MACHINE_A64_X12) | (1u << MACHINE_A64_X13) | (1u << MACHINE_A64_X14) |
                        (1u << MACHINE_A64_X15) | (1u << MACHINE_A64_X19) | (1u << MACHINE_A64_X20) | (1u << MACHINE_A64_X21) | (1u << MACHINE_A64_X22) |
                        (1u << MACHINE_A64_X23) | (1u << MACHINE_A64_X24) | (1u << MACHINE_A64_X25) | (1u << MACHINE_A64_X26) | (1u << MACHINE_A64_X27),
    .callee_saved_mask = (1u << MACHINE_A64_X19) | (1u << MACHINE_A64_X20) | (1u << MACHINE_A64_X21) | (1u << MACHINE_A64_X22) | (1u << MACHINE_A64_X23) |
                         (1u << MACHINE_A64_X24) | (1u << MACHINE_A64_X25) | (1u << MACHINE_A64_X26) | (1u << MACHINE_A64_X27),
    .register_count = MACHINE_A64_REGISTER_COUNT,
    .slot_scratch = {MACHINE_A64_X9, MACHINE_A64_X10, MACHINE_A64_X11, MACHINE_A64_X12},
    .copy_opcode = MACHINE_A64_MOV_RR,
    .constant_opcode = MACHINE_A64_MOV_RI,
    .indirect_call_opcode = MACHINE_A64_CALL_INDIRECT,
    .float_bridge_opcode = MACHINE_A64_FMOV_TO_VEC,
    .indirect_call_register = MACHINE_A64_X16,
    .float_bridge_register = MACHINE_A64_X9,
    .quality_pin_registers = {MACHINE_A64_X26, MACHINE_A64_X27},
};

BUSTER_F_DECL MachineTargetDescription const* machine_target_aarch64(void)
{
    return &machine_aarch64_description;
}

typedef struct MachineA64Selector MachineA64Selector;
struct MachineA64Selector
{
    Arena* arena;
    IrProgram* program;
    IrFunction* function;
    MachineFunctionBuilder builder;
    Target target;
    MachineBuilderStream immediates;
    MachineBuilderStream stack_slots;
    MachineBuilderStream stack_slot_alignments;
    MachineBuilderStream call_targets;
    // Per IrValue: virtual register index, stack slot index, or UINT32_MAX.
    u32* value_virtual_registers;
    u32* value_stack_slots;
    // 1 when every use of the value is a direct-call callee; such FUNCTION
    // values materialize zero instead of a symbol address, exactly like the
    // canonical path, so a single-use reference to an undefined symbol
    // never becomes an absolute relocation the static link cannot satisfy.
    u8* direct_call_uses;
    // Result value per argument index, captured at entry before any scratch
    // register can clobber the incoming fixed registers.
    u32 argument_values[MACHINE_A64_MAX_ARGUMENTS];
    // Definition point per virtual register, patched into the flattened
    // rows because builder chunks are write-once.
    u32* virtual_register_definitions;
    u32 virtual_register_count;
    IrOpcode failed_opcode;
    bool supported;
    bool returns_value;
};

BUSTER_GLOBAL_LOCAL bool machine_a64_type_is_scalar_register(IrType* type)
{
    if (!type || !type->layout.resolved || type->layout.size > 8)
    {
        return false;
    }
    return type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_ENUM;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_type_is_64_bit(IrProgram* program, IrTypeId type_id)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    return type && (type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION || (type->kind == IR_TYPE_INTEGER && type->bit_width > 32));
}

BUSTER_GLOBAL_LOCAL u32 machine_a64_scalar_bit_width(IrType* type)
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
// address regardless of the value's declared canonical type.
BUSTER_GLOBAL_LOCAL bool machine_a64_opcode_produces_address(IrOpcode opcode)
{
    return opcode == IR_OPCODE_GLOBAL || opcode == IR_OPCODE_INDEX || opcode == IR_OPCODE_FIELD || opcode == IR_OPCODE_DEREFERENCE ||
           opcode == IR_OPCODE_ADDRESS_OF || opcode == IR_OPCODE_FUNCTION;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_operand_register(MachineA64Selector* selector, IrValueId value, u32* register_out)
{
    if (value.value >= selector->function->value_count || selector->value_virtual_registers[value.value] == UINT32_MAX)
    {
        return false;
    }
    *register_out = selector->value_virtual_registers[value.value];
    return true;
}

BUSTER_GLOBAL_LOCAL u32 machine_a64_select_row(MachineA64Selector* selector, MachineInstruction instruction)
{
    return machine_builder_instruction(&selector->builder, instruction);
}

BUSTER_GLOBAL_LOCAL void machine_a64_define(MachineA64Selector* selector, u32 virtual_register, u32 machine_index)
{
    // Selection-synthesized vregs sit past the classification count and
    // carry their definition point from creation.
    if (virtual_register < selector->virtual_register_count && selector->virtual_register_definitions[virtual_register] == MACHINE_POINT_INVALID)
    {
        selector->virtual_register_definitions[virtual_register] = machine_point_make(machine_index, MACHINE_POINT_AFTER);
    }
}

BUSTER_GLOBAL_LOCAL void machine_a64_reject(MachineA64Selector* selector, IrOpcode opcode)
{
    selector->supported = false;
    selector->failed_opcode = opcode;
}

// A selection-synthesized temporary vreg, defined at the next row to be
// emitted so no post-pass definition patching is needed.
BUSTER_GLOBAL_LOCAL u32 machine_a64_synthesize_register(MachineA64Selector* selector)
{
    return machine_builder_virtual_register(&selector->builder, (MachineVirtualRegister){
                                                                    .definition_point =
                                                                        machine_point_make(selector->builder.instructions.total_count, MACHINE_POINT_AFTER),
                                                                    .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                    .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                                });
}

BUSTER_GLOBAL_LOCAL u32 machine_a64_append_slot(MachineA64Selector* selector, u32 size, u32 alignment)
{
    u32 slot_index = selector->stack_slots.total_count;
    u32* slot_size = (u32*)machine_stream_append(selector->arena, &selector->stack_slots);
    *slot_size = size;
    u32* slot_alignment = (u32*)machine_stream_append(selector->arena, &selector->stack_slot_alignments);
    *slot_alignment = alignment;
    return slot_index;
}

// Places the address of `base` plus a constant byte offset in one row: a
// direct local folds the offset into its frame displacement, and a pointer
// folds it into an immediate add. Only a zero offset on a pointer stays a
// plain copy, which the allocator can then coalesce away.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_place_address_offset(MachineA64Selector* selector, IrValueId base, u32 destination_register, u32 byte_offset)
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
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, destination_register),
                                                                    machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                       .payload = byte_offset,
                                                       .opcode = MACHINE_A64_LEA_FRAME,
                                                   });
        machine_a64_define(selector, destination_register, row);
        return true;
    }
    u32 address_register;
    if (!machine_a64_operand_register(selector, base, &address_register))
    {
        return false;
    }
    u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                   .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, destination_register),
                                                                machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                   .payload = byte_offset,
                                                   .opcode = (u16)(byte_offset ? MACHINE_A64_LEA_OFFSET : MACHINE_A64_MOV_RR),
                                               });
    machine_a64_define(selector, destination_register, row);
    return true;
}

// Emits result-vreg definition rows for one typed instruction. Returns
// false when the construct falls outside the selected subset.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_instruction(MachineA64Selector* selector, IrInstruction* instruction)
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
        // With STACK_ALLOCATE outside the subset, SP is constant after the
        // prologue, so save/restore pairs reduce to exact SP copies.
        if (result_register == UINT32_MAX)
        {
            return false;
        }
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                       .opcode = MACHINE_A64_READ_SP,
                                                   });
        machine_a64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_STACK_RESTORE:
    {
        u32 saved_register;
        if (!machine_a64_operand_register(selector, instruction->operands[0], &saved_register))
        {
            return false;
        }
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, saved_register)},
                                             .opcode = MACHINE_A64_WRITE_SP,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_ARGUMENT:
    {
        // The typed ARGUMENT can appear anywhere the frontend first used
        // the parameter; the value itself was captured by the entry rows
        // before any scratch register could clobber the incoming fixed
        // registers.
        if (!instruction->immediate_count || !instruction->immediates)
        {
            return false;
        }
        u32 argument_index = (u32)instruction->immediates[0];
        return argument_index < MACHINE_A64_MAX_ARGUMENTS && result_register != UINT32_MAX &&
               selector->argument_values[argument_index] == instruction->result.value;
    }
    break;
    case IR_OPCODE_CONSTANT_INTEGER:
    {
        if (result_register == UINT32_MAX)
        {
            return false;
        }
        u64 immediate = instruction->immediates[0];
        if (instruction->immediate_is_negative)
        {
            immediate = 0 - immediate;
        }
        u32 immediate_index = selector->immediates.total_count;
        u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
        *immediate_row = immediate;
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                       .opcode = MACHINE_A64_MOV_RI,
                                                   });
        machine_a64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_CAST:
    {
        u32 source_register;
        if (result_register == UINT32_MAX || !machine_a64_operand_register(selector, instruction->operands[0], &source_register))
        {
            return false;
        }
        IrType* source_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        IrType* cast_target_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u32 source_bits = machine_a64_scalar_bit_width(source_type);
        u16 opcode = 0;
        switch (instruction->conversion_operation)
        {
            break;
        case IR_CONVERSION_IDENTITY:
        case IR_CONVERSION_INTEGER_REINTERPRET:
        case IR_CONVERSION_POINTER_REINTERPRET:
        case IR_CONVERSION_INTEGER_TO_POINTER:
            opcode = MACHINE_A64_MOV_RR;
            break;
        case IR_CONVERSION_INTEGER_TRUNCATE:
        case IR_CONVERSION_POINTER_TO_INTEGER:
        {
            // The register model keeps every value zero-extended to 64
            // bits, so a narrowing cast must actually clear the discarded
            // top bits.
            u32 destination_bits = machine_a64_scalar_bit_width(cast_target_type);
            opcode = (u16)(destination_bits == 8    ? MACHINE_A64_UXTB
                           : destination_bits == 16 ? MACHINE_A64_UXTH
                           : destination_bits == 32 ? MACHINE_A64_MOV32_RR
                           : destination_bits == 64 ? MACHINE_A64_MOV_RR
                                                    : 0);
        }
        break;
        case IR_CONVERSION_INTEGER_SIGN_EXTEND:
            opcode = (u16)(source_bits == 8    ? MACHINE_A64_SXTB
                           : source_bits == 16 ? MACHINE_A64_SXTH
                           : source_bits == 32 ? MACHINE_A64_SXTW
                           : source_bits == 64 ? MACHINE_A64_MOV_RR
                                               : 0);
            break;
        case IR_CONVERSION_INTEGER_ZERO_EXTEND:
            opcode = (u16)(source_bits == 8    ? MACHINE_A64_UXTB
                           : source_bits == 16 ? MACHINE_A64_UXTH
                           : source_bits == 32 ? MACHINE_A64_MOV32_RR
                           : source_bits == 64 ? MACHINE_A64_MOV_RR
                                               : 0);
            break;
        default:
            opcode = 0;
        }
        if (!opcode)
        {
            return false;
        }
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                       .opcode = opcode,
                                                   });
        machine_a64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_UNARY:
    {
        u32 source_register;
        if (result_register == UINT32_MAX || !machine_a64_operand_register(selector, instruction->operands[0], &source_register))
        {
            return false;
        }
        bool wide = machine_a64_type_is_64_bit(program, function->values[instruction->operands[0].value].canonical_type);
        if (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE || instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT)
        {
            bool negate = instruction->unary_operation == IR_UNARY_INTEGER_NEGATE;
            u32 row = machine_a64_select_row(
                selector, (MachineInstruction){
                              .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                           machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                              .opcode = (u16)(negate ? (wide ? MACHINE_A64_NEG64 : MACHINE_A64_NEG32) : (wide ? MACHINE_A64_NOT64 : MACHINE_A64_NOT32)),
                          });
            machine_a64_define(selector, result_register, row);
            return true;
        }
        if (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                 .opcode = MACHINE_A64_CMP_ZERO,
                                             });
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                           .payload = MACHINE_A64_CONDITION_EQUAL,
                                                           .opcode = MACHINE_A64_CSET,
                                                       });
            machine_a64_define(selector, result_register, row);
            return true;
        }
        return false;
    }
    break;
    case IR_OPCODE_BINARY:
    {
        u32 left_register;
        u32 right_register;
        if (result_register == UINT32_MAX || !machine_a64_operand_register(selector, instruction->operands[0], &left_register) ||
            !machine_a64_operand_register(selector, instruction->operands[1], &right_register))
        {
            return false;
        }
        IrTypeId operand_type_id = function->values[instruction->operands[0].value].canonical_type;
        IrType* operand_type = ir_type_from_id(&program->types, operand_type_id);
        if (!machine_a64_type_is_scalar_register(operand_type))
        {
            return false;
        }
        bool wide = machine_a64_type_is_64_bit(program, operand_type_id);
        u16 arithmetic = 0;
        switch (instruction->binary_operation)
        {
            break;
        case IR_BINARY_INTEGER_ADD:
            arithmetic = (u16)(wide ? MACHINE_A64_ADD64 : MACHINE_A64_ADD32);
            break;
        case IR_BINARY_INTEGER_SUBTRACT:
            arithmetic = (u16)(wide ? MACHINE_A64_SUB64 : MACHINE_A64_SUB32);
            break;
        case IR_BINARY_INTEGER_MULTIPLY:
            arithmetic = (u16)(wide ? MACHINE_A64_MUL64 : MACHINE_A64_MUL32);
            break;
        case IR_BINARY_INTEGER_BITWISE_AND:
        case IR_BINARY_BOOLEAN_AND:
            arithmetic = (u16)(wide ? MACHINE_A64_AND64 : MACHINE_A64_AND32);
            break;
        case IR_BINARY_INTEGER_BITWISE_OR:
        case IR_BINARY_BOOLEAN_OR:
            arithmetic = (u16)(wide ? MACHINE_A64_ORR64 : MACHINE_A64_ORR32);
            break;
        case IR_BINARY_INTEGER_BITWISE_XOR:
            arithmetic = (u16)(wide ? MACHINE_A64_EOR64 : MACHINE_A64_EOR32);
            break;
        case IR_BINARY_SHIFT_LEFT:
            arithmetic = (u16)(wide ? MACHINE_A64_LSL64 : MACHINE_A64_LSL32);
            break;
        case IR_BINARY_SIGNED_SHIFT_RIGHT:
            arithmetic = (u16)(wide ? MACHINE_A64_ASR64 : MACHINE_A64_ASR32);
            break;
        case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
            arithmetic = (u16)(wide ? MACHINE_A64_LSR64 : MACHINE_A64_LSR32);
            break;
        case IR_BINARY_SIGNED_DIVIDE:
            arithmetic = (u16)(wide ? MACHINE_A64_SDIV64 : MACHINE_A64_SDIV32);
            break;
        case IR_BINARY_UNSIGNED_DIVIDE:
            arithmetic = (u16)(wide ? MACHINE_A64_UDIV64 : MACHINE_A64_UDIV32);
            break;
        case IR_BINARY_SIGNED_REMAINDER:
            arithmetic = (u16)(wide ? MACHINE_A64_SREM64 : MACHINE_A64_SREM32);
            break;
        case IR_BINARY_UNSIGNED_REMAINDER:
            arithmetic = (u16)(wide ? MACHINE_A64_UREM64 : MACHINE_A64_UREM32);
            break;
        default:
            arithmetic = 0;
        }
        if (arithmetic)
        {
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                           .opcode = arithmetic,
                                                       });
            machine_a64_define(selector, result_register, row);
            return true;
        }
        u32 condition = UINT32_MAX;
        switch (instruction->binary_operation)
        {
            break;
        case IR_BINARY_INTEGER_EQUAL:
        case IR_BINARY_POINTER_EQUAL:
        case IR_BINARY_BOOLEAN_EQUAL:
            condition = MACHINE_A64_CONDITION_EQUAL;
            break;
        case IR_BINARY_INTEGER_NOT_EQUAL:
        case IR_BINARY_POINTER_NOT_EQUAL:
        case IR_BINARY_BOOLEAN_NOT_EQUAL:
            condition = MACHINE_A64_CONDITION_NOT_EQUAL;
            break;
        case IR_BINARY_SIGNED_LESS:
            condition = MACHINE_A64_CONDITION_LESS;
            break;
        case IR_BINARY_SIGNED_LESS_EQUAL:
            condition = MACHINE_A64_CONDITION_LESS_EQUAL;
            break;
        case IR_BINARY_SIGNED_GREATER:
            condition = MACHINE_A64_CONDITION_GREATER;
            break;
        case IR_BINARY_SIGNED_GREATER_EQUAL:
            condition = MACHINE_A64_CONDITION_GREATER_EQUAL;
            break;
        case IR_BINARY_UNSIGNED_LESS:
            condition = MACHINE_A64_CONDITION_BELOW;
            break;
        case IR_BINARY_UNSIGNED_LESS_EQUAL:
            condition = MACHINE_A64_CONDITION_BELOW_EQUAL;
            break;
        case IR_BINARY_UNSIGNED_GREATER:
            condition = MACHINE_A64_CONDITION_ABOVE;
            break;
        case IR_BINARY_UNSIGNED_GREATER_EQUAL:
            condition = MACHINE_A64_CONDITION_ABOVE_EQUAL;
            break;
        default:
            condition = UINT32_MAX;
        }
        if (condition == UINT32_MAX)
        {
            return false;
        }
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                             .opcode = (u16)(wide ? MACHINE_A64_CMP64 : MACHINE_A64_CMP32),
                                         });
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                       .payload = condition,
                                                       .opcode = MACHINE_A64_CSET,
                                                   });
        machine_a64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_DEREFERENCE:
    {
        u32 source_register;
        if (result_register == UINT32_MAX || !machine_a64_operand_register(selector, instruction->operands[0], &source_register))
        {
            return false;
        }
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                       .opcode = MACHINE_A64_MOV_RR,
                                                   });
        machine_a64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_ADDRESS_OF:
    {
        return result_register != UINT32_MAX && machine_a64_select_place_address_offset(selector, instruction->operands[0], result_register, 0);
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
        return machine_a64_select_place_address_offset(selector, instruction->operands[0], result_register,
                                                       (u32)aggregate->fields[field_index].offset);
    }
    break;
    case IR_OPCODE_GLOBAL:
    case IR_OPCODE_FUNCTION:
    {
        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
        if (result_register == UINT32_MAX || !symbol || (instruction->opcode == IR_OPCODE_GLOBAL && symbol->is_thread_local))
        {
            return false;
        }
        if (instruction->opcode == IR_OPCODE_FUNCTION && selector->direct_call_uses[instruction->result.value] == 1)
        {
            u32 zero_index = selector->immediates.total_count;
            u64* zero_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
            *zero_row = 0;
            u32 zero_machine_row = machine_a64_select_row(selector, (MachineInstruction){
                                                              .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                           machine_ref_make(MACHINE_REF_IMMEDIATE, zero_index)},
                                                              .opcode = MACHINE_A64_MOV_RI,
                                                          });
            machine_a64_define(selector, result_register, zero_machine_row);
            return true;
        }
        u32 target_index = selector->call_targets.total_count;
        IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
        *target_row = instruction->symbol;
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                       .payload = target_index,
                                                       .opcode = MACHINE_A64_LEA_SYMBOL,
                                                   });
        machine_a64_define(selector, result_register, row);
        return true;
    }
    break;
    case IR_OPCODE_INDEX:
    {
        u32 index_register;
        if (result_register == UINT32_MAX || instruction->operand_count < 2 ||
            !machine_a64_operand_register(selector, instruction->operands[1], &index_register))
        {
            return false;
        }
        IrType* index_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
        IrType* element = ir_type_from_id(&program->types, instruction->canonical_type);
        if (!index_type || index_type->kind != IR_TYPE_INTEGER || !element || !element->layout.resolved || element->layout.size > INT32_MAX)
        {
            return false;
        }
        if (!machine_a64_select_place_address_offset(selector, instruction->operands[0], result_register, 0))
        {
            return false;
        }
        // The scaled index is computed in a synthesized temporary; signed
        // narrow indexes sign-extend before scaling.
        u32 scaled_register = machine_a64_synthesize_register(selector);
        u16 extend_opcode = MACHINE_A64_MOV_RR;
        if (index_type->is_signed && index_type->bit_width < 64)
        {
            extend_opcode = (u16)(index_type->bit_width == 8    ? MACHINE_A64_SXTB
                                  : index_type->bit_width == 16 ? MACHINE_A64_SXTH
                                  : index_type->bit_width == 32 ? MACHINE_A64_SXTW
                                                                : 0);
            if (!extend_opcode)
            {
                return false;
            }
        }
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, index_register)},
                                             .opcode = extend_opcode,
                                         });
        if (element->layout.size != 1)
        {
            u32 immediate_index = selector->immediates.total_count;
            u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
            *immediate_row = element->layout.size;
            u32 size_register = machine_a64_synthesize_register(selector);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, size_register),
                                                              machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                 .opcode = MACHINE_A64_MOV_RI,
                                             });
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, size_register)},
                                                 .opcode = MACHINE_A64_MUL64,
                                             });
        }
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register)},
                                             .opcode = MACHINE_A64_ADD64,
                                         });
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
        if (!direct_call && !machine_a64_operand_register(selector, instruction->operands[0], &callee_register))
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
        // Darwin passes every anonymous argument on the stack, which the
        // subset does not stage yet; AAPCS64 variadic scalars travel in
        // the same registers as named ones.
        bool darwin = selector->target.os == OPERATING_SYSTEM_MACOS || selector->target.os == OPERATING_SYSTEM_IOS;
        if (variadic_call && darwin)
        {
            return false;
        }
        if (!callee_type || callee_type->kind != IR_TYPE_FUNCTION ||
            (variadic_call ? call_argument_count < callee_type->parameter_count : callee_type->parameter_count != call_argument_count) ||
            call_argument_count > MACHINE_A64_MAX_ARGUMENTS)
        {
            return false;
        }
        IrType* callee_return_type = ir_type_from_id(&program->types, callee_type->return_type);
        bool callee_returns_value = callee_return_type && callee_return_type->kind != IR_TYPE_VOID;
        if (callee_returns_value && !machine_a64_type_is_scalar_register(callee_return_type))
        {
            return false;
        }
        u32 argument_registers[MACHINE_A64_MAX_ARGUMENTS];
        for (u32 argument_index = 0; argument_index < call_argument_count; argument_index += 1)
        {
            IrTypeId argument_type_id = argument_index < callee_type->parameter_count
                                            ? callee_type->parameter_types[argument_index]
                                            : function->values[instruction->operands[argument_index + 1].value].canonical_type;
            IrType* argument_type = ir_type_from_id(&program->types, argument_type_id);
            if (!machine_a64_type_is_scalar_register(argument_type) ||
                !machine_a64_operand_register(selector, instruction->operands[argument_index + 1], argument_registers + argument_index))
            {
                return false;
            }
        }
        for (u32 argument_index = 0; argument_index < call_argument_count; argument_index += 1)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, argument_index),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_registers[argument_index])},
                                                 .opcode = MACHINE_A64_MOV_RR,
                                             });
        }
        if (direct_call)
        {
            u32 target_index = selector->call_targets.total_count;
            IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
            *target_row = instruction->symbol;
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .payload = target_index,
                                                 .opcode = MACHINE_A64_CALL_DIRECT,
                                             });
        }
        else
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, callee_register)},
                                                 .opcode = MACHINE_A64_CALL_INDIRECT,
                                             });
        }
        if (callee_returns_value && instruction->result.value != IR_ID_UNDERLYING_INVALID && result_register != UINT32_MAX)
        {
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_A64_X0)},
                                                           .opcode = MACHINE_A64_MOV_RR,
                                                       });
            machine_a64_define(selector, result_register, row);
        }
        return true;
    }
    break;
    case IR_OPCODE_DEBUG_TRAP:
    {
        machine_a64_select_row(selector, (MachineInstruction){.opcode = MACHINE_A64_BRK});
        return true;
    }
    break;
    case IR_OPCODE_LOAD:
    {
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
        if (result_register == UINT32_MAX)
        {
            return false;
        }
        if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
        {
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                           .opcode = MACHINE_A64_LOAD_FRAME,
                                                       });
            machine_a64_define(selector, result_register, row);
            return true;
        }
        if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
            definition->opcode == IR_OPCODE_FIELD)
        {
            u32 address_register;
            if (!machine_a64_operand_register(selector, instruction->operands[0], &address_register))
            {
                return false;
            }
            IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
            u64 size = loaded_type && loaded_type->layout.resolved ? loaded_type->layout.size : 0;
            u16 opcode = size == 1   ? MACHINE_A64_LOAD_PTR8
                         : size == 2 ? MACHINE_A64_LOAD_PTR16
                         : size == 4 ? MACHINE_A64_LOAD_PTR32
                         : size == 8 ? MACHINE_A64_LOAD_PTR64
                                     : 0;
            if (!opcode)
            {
                return false;
            }
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                           .opcode = opcode,
                                                       });
            machine_a64_define(selector, result_register, row);
            return true;
        }
        return false;
    }
    break;
    case IR_OPCODE_STORE:
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
        u32 value_register;
        if (!machine_a64_operand_register(selector, instruction->operands[1], &value_register))
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
            // exactly like the x86-64 machine path: the slot is the value's
            // exclusive home, and narrower stores would leave stale upper
            // bytes for the sixty-four-bit slot loads that follow.
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                 .opcode = MACHINE_A64_STORE_FRAME64,
                                             });
            return true;
        }
        if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
            definition->opcode == IR_OPCODE_FIELD)
        {
            u32 address_register;
            if (!machine_a64_operand_register(selector, instruction->operands[0], &address_register))
            {
                return false;
            }
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                 .opcode = (u16)(MACHINE_A64_STORE_PTR8 + size_index),
                                             });
            return true;
        }
        return false;
    }
    break;
    case IR_OPCODE_BRANCH:
    {
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value)},
                                             .opcode = MACHINE_A64_B,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_BRANCH_IF:
    {
        u32 condition_register;
        if (!machine_a64_operand_register(selector, instruction->operands[0], &condition_register))
        {
            return false;
        }
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, condition_register)},
                                             .opcode = MACHINE_A64_CMP_ZERO,
                                         });
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value),
                                                          machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[1].value)},
                                             .payload = MACHINE_A64_CONDITION_NOT_EQUAL,
                                             .opcode = MACHINE_A64_BCC,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_UNREACHABLE:
    {
        // Control never reaches this terminator; brk keeps the block
        // verifier-well-formed, faults loudly if control ever arrives, and
        // matches the canonical bytes.
        machine_a64_select_row(selector, (MachineInstruction){
                                             .opcode = MACHINE_A64_UDF,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_RETURN:
    {
        if (instruction->operand_count)
        {
            u32 value_register;
            if (!machine_a64_operand_register(selector, instruction->operands[0], &value_register))
            {
                return false;
            }
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_A64_X0),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                 .opcode = MACHINE_A64_MOV_RR,
                                             });
        }
        machine_a64_select_row(selector, (MachineInstruction){
                                             .opcode = MACHINE_A64_RET,
                                         });
        return true;
    }
    break;
    default:
        return false;
    }
    return false;
}

MachineSelectResult machine_select_canonical_function_aarch64(Arena* arena, IrProgram* program, IrFunction* function, Target target)
{
    MachineSelectResult result = {
        .failed_opcode = IR_OPCODE_COUNT,
    };
    if (!arena || !program || !function || target.cpu_arch != CPU_ARCH_AARCH64 || function->state != IR_FUNCTION_LOWERED || !function->block_count ||
        function->entry.value != 0)
    {
        return result;
    }
    IrType* function_type = ir_type_from_id(&program->types, function->canonical_type);
    if (!function_type || function_type->kind != IR_TYPE_FUNCTION || function_type->is_variadic ||
        function_type->parameter_count > MACHINE_A64_MAX_ARGUMENTS)
    {
        return result;
    }
    // The scalar subset: every parameter and the return value must occupy
    // one integer register. Anything else stays canonical for now.
    IrType* return_type = ir_type_from_id(&program->types, function_type->return_type);
    bool returns_value = return_type && return_type->kind != IR_TYPE_VOID;
    if (returns_value && !machine_a64_type_is_scalar_register(return_type))
    {
        return result;
    }
    for (u32 parameter_index = 0; parameter_index < function_type->parameter_count; parameter_index += 1)
    {
        IrType* parameter_type = ir_type_from_id(&program->types, function_type->parameter_types[parameter_index]);
        if (!machine_a64_type_is_scalar_register(parameter_type))
        {
            return result;
        }
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        if (function->blocks[block_index].parameter_count)
        {
            return result;
        }
    }
    MachineA64Selector selector = {
        .arena = arena,
        .program = program,
        .function = function,
        .builder = machine_function_builder_begin(arena),
        .value_virtual_registers = arena_allocate(arena, u32, function->value_count),
        .value_stack_slots = arena_allocate(arena, u32, function->value_count),
        .supported = true,
        .failed_opcode = IR_OPCODE_COUNT,
    };
    selector.target = target;
    selector.direct_call_uses = arena_allocate(arena, u8, function->value_count ? function->value_count : 1);
    memset(selector.direct_call_uses, 0, function->value_count);
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* walk = function->instructions + instruction_index;
        for (u32 operand_index = 0; operand_index < walk->operand_count; operand_index += 1)
        {
            IrValueId operand = walk->operands[operand_index];
            if (operand.value >= function->value_count || selector.direct_call_uses[operand.value] == 2)
            {
                continue;
            }
            bool direct = walk->opcode == IR_OPCODE_CALL && operand_index == 0;
            if (direct)
            {
                IrInstructionId definition = function->values[operand.value].definition;
                IrInstruction* reference = definition.value < function->instruction_count ? function->instructions + definition.value : 0;
                direct = reference && reference->opcode == IR_OPCODE_FUNCTION && reference->symbol.value == walk->symbol.value;
            }
            selector.direct_call_uses[operand.value] = direct ? 1 : 2;
        }
    }
    machine_stream_initialize(&selector.immediates, sizeof(u64));
    machine_stream_initialize(&selector.stack_slots, sizeof(u32));
    machine_stream_initialize(&selector.stack_slot_alignments, sizeof(u32));
    machine_stream_initialize(&selector.call_targets, sizeof(IrSymbolId));
    MachineBuilderStream line_marks;
    machine_stream_initialize(&line_marks, sizeof(MachineLineMark));
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
                    machine_a64_reject(&selector, instruction->opcode);
                    break;
                }
                selector.value_stack_slots[instruction->result.value] =
                    machine_a64_append_slot(&selector, (u32)((local_type->layout.size + 7) & ~(u64)7), local_alignment);
                continue;
            }
            IrType* value_type = ir_type_from_id(&program->types, value->canonical_type);
            if (machine_a64_type_is_scalar_register(value_type) || machine_a64_opcode_produces_address(instruction->opcode))
            {
                u32 register_index = machine_builder_virtual_register(&selector.builder, (MachineVirtualRegister){
                                                                                             .definition_point = MACHINE_POINT_INVALID,
                                                                                             .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                                             .typed_origin = instruction->result.value,
                                                                                         });
                selector.value_virtual_registers[instruction->result.value] = register_index;
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
            for (u32 argument_index = 0; argument_index < BUSTER_ARRAY_LENGTH(selector.argument_values); argument_index += 1)
            {
                u32 argument_value = selector.argument_values[argument_index];
                if (argument_value == IR_ID_UNDERLYING_INVALID)
                {
                    continue;
                }
                u32 argument_register = selector.value_virtual_registers[argument_value];
                if (argument_register == UINT32_MAX)
                {
                    machine_a64_reject(&selector, IR_OPCODE_ARGUMENT);
                    break;
                }
                u32 row = machine_a64_select_row(&selector, (MachineInstruction){
                                                                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_register),
                                                                             machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, argument_index)},
                                                                .opcode = MACHINE_A64_MOV_RR,
                                                            });
                machine_a64_define(&selector, argument_register, row);
            }
        }
        for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID && selector.supported;
             id = function->instructions[id.value].next)
        {
            IrInstruction* instruction = function->instructions + id.value;
            typed_instruction_count += 1;
            IrSourceRange mark_source = ir_instruction_canonical_source(function, id);
            if (mark_source.source.value != IR_ID_UNDERLYING_INVALID)
            {
                MachineLineMark* mark = (MachineLineMark*)machine_stream_append(arena, &line_marks);
                *mark = (MachineLineMark){
                    .row = selector.builder.instructions.total_count,
                    .source = mark_source.source.value,
                    .offset = mark_source.offset,
                };
            }
            if (!machine_a64_select_instruction(&selector, instruction))
            {
                machine_a64_reject(&selector, instruction->opcode);
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
    result.function.target = &machine_aarch64_description;
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
    result.function.line_marks = arena_allocate(arena, MachineLineMark, line_marks.total_count);
    result.function.line_mark_count = line_marks.total_count;
    machine_stream_flatten(&line_marks, result.function.line_marks);
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

typedef struct MachineA64Encoder MachineA64Encoder;
struct MachineA64Encoder
{
    u8* bytes;
    u32 count;
    u32 capacity;
    bool overflow;
    // An operand or offset outside what the subset can encode; the caller
    // reports an encode fallback rather than emitting wrong bytes.
    bool error;
    u8 reserved[2];
};

typedef struct MachineA64BranchFixup MachineA64BranchFixup;
struct MachineA64BranchFixup
{
    u32 patch_offset;
    u32 block;
    // 0 = b (imm26), 1 = b.cond (imm19 << 5).
    u32 form;
};

BUSTER_GLOBAL_LOCAL void machine_a64_emit(MachineA64Encoder* encoder, u32 word)
{
    if (encoder->count + 4 > encoder->capacity)
    {
        encoder->overflow = true;
        return;
    }
    memcpy(encoder->bytes + encoder->count, &word, sizeof(word));
    encoder->count += 4;
}

// movz/movk materialization, mirroring the canonical a64_emit_constant.
BUSTER_GLOBAL_LOCAL void machine_a64_emit_immediate(MachineA64Encoder* encoder, u32 register_number, u64 value)
{
    machine_a64_emit(encoder, 0xd2800000 | ((u32)(value & 0xffff) << 5) | register_number);
    for (u32 shift = 16; shift < 64; shift += 16)
    {
        if ((value >> shift) & 0xffff)
        {
            machine_a64_emit(encoder, 0xf2800000 | ((shift / 16) << 21) | ((u32)((value >> shift) & 0xffff) << 5) | register_number);
        }
    }
}

// Frame-relative sized memory operation off the X28 frame base, mirroring
// the canonical codegen_canonical_a64_memory_operation_base: scaled
// unsigned offsets directly, larger offsets through the X16 scratch.
BUSTER_GLOBAL_LOCAL void machine_a64_emit_frame_memory(MachineA64Encoder* encoder, u32 register_number, u32 offset, u32 size, bool store)
{
    u32 scale = size;
    if ((size != 1 && size != 2 && size != 4 && size != 8) || offset % scale)
    {
        encoder->error = true;
        return;
    }
    u32 base_register = MACHINE_A64_X28;
    if (offset / scale > 4095)
    {
        machine_a64_emit_immediate(encoder, MACHINE_A64_X16, offset);
        machine_a64_emit(encoder, 0x8b000000 | (MACHINE_A64_X16 << 16) | (base_register << 5) | MACHINE_A64_X16);
        base_register = MACHINE_A64_X16;
        offset = 0;
    }
    u32 instruction;
    if (store)
    {
        instruction = size == 8 ? 0xf9000000 : size == 4 ? 0xb9000000 : size == 2 ? 0x79000000 : 0x39000000;
    }
    else
    {
        instruction = size == 8 ? 0xf9400000 : size == 4 ? 0xb9400000 : size == 2 ? 0x79400000 : 0x39400000;
    }
    machine_a64_emit(encoder, instruction | ((offset / scale) << 10) | (base_register << 5) | register_number);
}

BUSTER_GLOBAL_LOCAL void machine_a64_emit_frame_load(MachineA64Encoder* encoder, u32 register_number, u32 offset)
{
    machine_a64_emit_frame_memory(encoder, register_number, offset, 8, false);
}

BUSTER_GLOBAL_LOCAL void machine_a64_emit_frame_store(MachineA64Encoder* encoder, u32 register_number, u32 offset)
{
    machine_a64_emit_frame_memory(encoder, register_number, offset, 8, true);
}

// Register-to-register copy; SP never appears here, so the orr form's zero
// register reading of 31 can never be misinterpreted.
BUSTER_GLOBAL_LOCAL void machine_a64_emit_move(MachineA64Encoder* encoder, u32 destination, u32 source)
{
    machine_a64_emit(encoder, 0xaa0003e0 | (source << 16) | destination);
}

// Frame-slot placement offsets grow downward from the frame base; the
// X28-relative byte offset is their distance from the frame top.
BUSTER_GLOBAL_LOCAL u32 machine_a64_frame_offset(MachineStackPlacement* placement, u32 placement_offset)
{
    return placement->frame_size - placement_offset;
}

MachineEncodeResult machine_encode_aarch64(Arena* arena, MachineFunction* function, MachineStackPlacement* placement)
{
    MachineEncodeResult result = {0};
    if (!placement->valid || placement->callee_saved_mask || placement->frame_size > MACHINE_A64_MAX_FRAME_BYTES)
    {
        // Callee-saved allocation needs the prologue save area a later
        // stage adds; outsized frames need multi-word prologue stores the
        // module wiring's cursor accounting does not model yet.
        return result;
    }
    // Stack layout: [sp .. sp+frame_size) holds the placement's slots,
    // [sp+frame_size] saves the caller's x28, and eight padding bytes keep
    // the total sixteen-aligned. The placement frame is sixteen-aligned
    // already, so the total is too.
    u32 frame_total = placement->frame_size + 16;
    // Per-row worst-case byte budget: constants and remainders expand, the
    // epilogue carries the frame release, everything else is one word plus
    // slack for large-offset frame addressing.
    u32 frame_chunk_words = frame_total / 4080 + 1;
    u64 capacity64 = 64 + (u64)frame_chunk_words * 8;
    for (u32 capacity_index = 0; capacity_index < function->instruction_count; capacity_index += 1)
    {
        MachineInstruction* capacity_row = function->instructions + capacity_index;
        switch (capacity_row->opcode)
        {
            break;
        case MACHINE_A64_MOV_RI:
        case MACHINE_A64_LEA_SYMBOL:
            capacity64 += 16;
            break;
        case MACHINE_A64_RET:
            capacity64 += 20 + (u64)frame_chunk_words * 4;
            break;
        default:
            capacity64 += 12;
        }
    }
    capacity64 += (u64)placement->edit_count * 28;
    if (capacity64 > UINT32_MAX)
    {
        return result;
    }
    MachineA64Encoder encoder = {
        .bytes = arena_allocate(arena, u8, capacity64),
        .capacity = (u32)capacity64,
    };
    MachineBuilderStream fixups;
    machine_stream_initialize(&fixups, sizeof(MachineA64BranchFixup));
    MachineBuilderStream call_sites;
    machine_stream_initialize(&call_sites, sizeof(MachineCallSite));
    MachineBuilderStream epilogs;
    machine_stream_initialize(&epilogs, sizeof(u32));
    result.block_offsets = arena_allocate(arena, u32, function->block_count);
    result.row_offsets = arena_allocate(arena, u32, function->instruction_count ? function->instruction_count : 1);
    // Prologue, byte-for-byte the canonical AArch64 shape so the module
    // wiring's unwind actions keep their exact meaning: save the
    // frame-pointer pair, establish x29, allocate the frame in probed
    // chunks, save the caller's x28 and repoint it at the frame base.
    machine_a64_emit(&encoder, 0xa9bf7bfd);
    machine_a64_emit(&encoder, 0x910003fd);
    u32 frame_remaining = frame_total;
    while (frame_remaining)
    {
        u32 frame_chunk = BUSTER_MIN(frame_remaining, 4080u);
        machine_a64_emit(&encoder, 0xd10003ff | (frame_chunk << 10));
        machine_a64_emit(&encoder, 0xf90003ff);
        frame_remaining -= frame_chunk;
    }
    machine_a64_emit(&encoder, 0xf90003e0 | ((placement->frame_size / 8) << 10) | (MACHINE_A64_SP << 5) | MACHINE_A64_X28);
    machine_a64_emit(&encoder, 0x910003fc);
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
            result.row_offsets[instruction_index] = encoder.count;
            MachinePoint before = machine_point_make(instruction_index, MACHINE_POINT_BEFORE);
            while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == before)
            {
                MachineEdit* edit = placement->edits + edit_cursor;
                if (edit->kind == MACHINE_EDIT_SPILL)
                {
                    machine_a64_emit_frame_store(&encoder, edit->location, machine_a64_frame_offset(placement, placement->virtual_register_offsets[edit->subject]));
                }
                else if (edit->kind == MACHINE_EDIT_COPY)
                {
                    machine_a64_emit_move(&encoder, edit->location, edit->subject);
                }
                else if (edit->kind == MACHINE_EDIT_REMATERIALIZE)
                {
                    machine_a64_emit_immediate(&encoder, edit->location, function->immediates[edit->subject]);
                }
                else
                {
                    machine_a64_emit_frame_load(&encoder, edit->location, machine_a64_frame_offset(placement, placement->virtual_register_offsets[edit->subject]));
                }
                edit_cursor += 1;
            }
            switch (instruction->opcode)
            {
                break;
            case MACHINE_A64_MOV_RI:
                machine_a64_emit_immediate(&encoder, operand_registers[0], function->immediates[machine_ref_payload(instruction->operands[1])]);
                break;
            case MACHINE_A64_MOV_RR:
                // A full-width self-copy is the coalesced form of this row
                // and encodes to nothing. The narrower move below is not an
                // identity: it clears the upper bits.
                if (operand_registers[0] != operand_registers[1])
                {
                    machine_a64_emit_move(&encoder, operand_registers[0], operand_registers[1]);
                }
                break;
            case MACHINE_A64_MOV32_RR:
                machine_a64_emit(&encoder, 0x2a0003e0 | ((u32)operand_registers[1] << 16) | operand_registers[0]);
                break;
            case MACHINE_A64_SXTB:
                machine_a64_emit(&encoder, 0x93401c00 | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_SXTH:
                machine_a64_emit(&encoder, 0x93403c00 | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_SXTW:
                machine_a64_emit(&encoder, 0x93407c00 | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_UXTB:
                machine_a64_emit(&encoder, 0x53001c00 | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_UXTH:
                machine_a64_emit(&encoder, 0x53003c00 | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_ADD32:
            case MACHINE_A64_ADD64:
            case MACHINE_A64_SUB32:
            case MACHINE_A64_SUB64:
            case MACHINE_A64_AND32:
            case MACHINE_A64_AND64:
            case MACHINE_A64_ORR32:
            case MACHINE_A64_ORR64:
            case MACHINE_A64_EOR32:
            case MACHINE_A64_EOR64:
            {
                u32 word = instruction->opcode == MACHINE_A64_ADD32 || instruction->opcode == MACHINE_A64_ADD64   ? 0x0b000000
                           : instruction->opcode == MACHINE_A64_SUB32 || instruction->opcode == MACHINE_A64_SUB64 ? 0x4b000000
                           : instruction->opcode == MACHINE_A64_AND32 || instruction->opcode == MACHINE_A64_AND64 ? 0x0a000000
                           : instruction->opcode == MACHINE_A64_ORR32 || instruction->opcode == MACHINE_A64_ORR64 ? 0x2a000000
                                                                                                                  : 0x4a000000;
                bool wide = instruction->opcode == MACHINE_A64_ADD64 || instruction->opcode == MACHINE_A64_SUB64 ||
                            instruction->opcode == MACHINE_A64_AND64 || instruction->opcode == MACHINE_A64_ORR64 ||
                            instruction->opcode == MACHINE_A64_EOR64;
                machine_a64_emit(&encoder, word | (wide ? 0x80000000u : 0) | ((u32)operand_registers[2] << 16) | ((u32)operand_registers[1] << 5) |
                                               operand_registers[0]);
            }
            break;
            case MACHINE_A64_MUL32:
            case MACHINE_A64_MUL64:
                machine_a64_emit(&encoder, 0x1b007c00 | (instruction->opcode == MACHINE_A64_MUL64 ? 0x80000000u : 0) |
                                               ((u32)operand_registers[2] << 16) | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_SDIV32:
            case MACHINE_A64_SDIV64:
            case MACHINE_A64_UDIV32:
            case MACHINE_A64_UDIV64:
            {
                bool wide = instruction->opcode == MACHINE_A64_SDIV64 || instruction->opcode == MACHINE_A64_UDIV64;
                bool is_signed = instruction->opcode == MACHINE_A64_SDIV32 || instruction->opcode == MACHINE_A64_SDIV64;
                machine_a64_emit(&encoder, (is_signed ? 0x1ac00c00u : 0x1ac00800u) | (wide ? 0x80000000u : 0) | ((u32)operand_registers[2] << 16) |
                                               ((u32)operand_registers[1] << 5) | operand_registers[0]);
            }
            break;
            case MACHINE_A64_SREM32:
            case MACHINE_A64_SREM64:
            case MACHINE_A64_UREM32:
            case MACHINE_A64_UREM64:
            {
                // Divide into the destination, then multiply-subtract back:
                // d = n - (n / m) * m. The constrained slot layout keeps
                // the three registers distinct, which the sequence needs.
                bool wide = instruction->opcode == MACHINE_A64_SREM64 || instruction->opcode == MACHINE_A64_UREM64;
                bool is_signed = instruction->opcode == MACHINE_A64_SREM32 || instruction->opcode == MACHINE_A64_SREM64;
                u32 wide_mask = wide ? 0x80000000u : 0;
                machine_a64_emit(&encoder, (is_signed ? 0x1ac00c00u : 0x1ac00800u) | wide_mask | ((u32)operand_registers[2] << 16) |
                                               ((u32)operand_registers[1] << 5) | operand_registers[0]);
                machine_a64_emit(&encoder, 0x1b008000 | wide_mask | ((u32)operand_registers[2] << 16) | ((u32)operand_registers[1] << 10) |
                                               ((u32)operand_registers[0] << 5) | operand_registers[0]);
            }
            break;
            case MACHINE_A64_LSL32:
            case MACHINE_A64_LSL64:
            case MACHINE_A64_ASR32:
            case MACHINE_A64_ASR64:
            case MACHINE_A64_LSR32:
            case MACHINE_A64_LSR64:
            {
                bool wide = instruction->opcode == MACHINE_A64_LSL64 || instruction->opcode == MACHINE_A64_ASR64 || instruction->opcode == MACHINE_A64_LSR64;
                u32 word = instruction->opcode == MACHINE_A64_LSL32 || instruction->opcode == MACHINE_A64_LSL64   ? 0x1ac02000u
                           : instruction->opcode == MACHINE_A64_ASR32 || instruction->opcode == MACHINE_A64_ASR64 ? 0x1ac02800u
                                                                                                                  : 0x1ac02400u;
                machine_a64_emit(&encoder, word | (wide ? 0x80000000u : 0) | ((u32)operand_registers[2] << 16) | ((u32)operand_registers[1] << 5) |
                                               operand_registers[0]);
            }
            break;
            case MACHINE_A64_NEG32:
                machine_a64_emit(&encoder, 0x4b0003e0 | ((u32)operand_registers[1] << 16) | operand_registers[0]);
                break;
            case MACHINE_A64_NEG64:
                machine_a64_emit(&encoder, 0xcb0003e0 | ((u32)operand_registers[1] << 16) | operand_registers[0]);
                break;
            case MACHINE_A64_NOT32:
                machine_a64_emit(&encoder, 0x2a2003e0 | ((u32)operand_registers[1] << 16) | operand_registers[0]);
                break;
            case MACHINE_A64_NOT64:
                machine_a64_emit(&encoder, 0xaa2003e0 | ((u32)operand_registers[1] << 16) | operand_registers[0]);
                break;
            case MACHINE_A64_CMP32:
                machine_a64_emit(&encoder, 0x6b00001f | ((u32)operand_registers[1] << 16) | ((u32)operand_registers[0] << 5));
                break;
            case MACHINE_A64_CMP64:
                machine_a64_emit(&encoder, 0xeb00001f | ((u32)operand_registers[1] << 16) | ((u32)operand_registers[0] << 5));
                break;
            case MACHINE_A64_CMP_ZERO:
                machine_a64_emit(&encoder, 0xf100001f | ((u32)operand_registers[0] << 5));
                break;
            case MACHINE_A64_CSET:
                machine_a64_emit(&encoder, 0x1a9f07e0 | ((instruction->payload ^ 1u) << 12) | operand_registers[0]);
                break;
            case MACHINE_A64_LOAD_FRAME:
                machine_a64_emit_frame_load(
                    &encoder, operand_registers[0],
                    machine_a64_frame_offset(placement, placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])] - instruction->payload));
                break;
            case MACHINE_A64_STORE_FRAME8:
            case MACHINE_A64_STORE_FRAME16:
            case MACHINE_A64_STORE_FRAME32:
            case MACHINE_A64_STORE_FRAME64:
            {
                u32 size = instruction->opcode == MACHINE_A64_STORE_FRAME8    ? 1u
                           : instruction->opcode == MACHINE_A64_STORE_FRAME16 ? 2u
                           : instruction->opcode == MACHINE_A64_STORE_FRAME32 ? 4u
                                                                              : 8u;
                machine_a64_emit_frame_memory(
                    &encoder, operand_registers[1],
                    machine_a64_frame_offset(placement, placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])] - instruction->payload),
                    size, true);
            }
            break;
            case MACHINE_A64_LOAD_PTR8:
                machine_a64_emit(&encoder, 0x39400000 | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_LOAD_PTR16:
                machine_a64_emit(&encoder, 0x79400000 | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_LOAD_PTR32:
                machine_a64_emit(&encoder, 0xb9400000 | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_LOAD_PTR64:
                machine_a64_emit(&encoder, 0xf9400000 | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_STORE_PTR8:
                machine_a64_emit(&encoder, 0x39000000 | ((u32)operand_registers[0] << 5) | operand_registers[1]);
                break;
            case MACHINE_A64_STORE_PTR16:
                machine_a64_emit(&encoder, 0x79000000 | ((u32)operand_registers[0] << 5) | operand_registers[1]);
                break;
            case MACHINE_A64_STORE_PTR32:
                machine_a64_emit(&encoder, 0xb9000000 | ((u32)operand_registers[0] << 5) | operand_registers[1]);
                break;
            case MACHINE_A64_STORE_PTR64:
                machine_a64_emit(&encoder, 0xf9000000 | ((u32)operand_registers[0] << 5) | operand_registers[1]);
                break;
            case MACHINE_A64_LEA_FRAME:
            {
                // The payload is a byte offset into the slot; the whole
                // member address is one add when it fits an imm12, or a
                // materialized constant plus a register add when it does
                // not — mirroring the canonical base-address helper with
                // the destination as its own scratch.
                u32 frame_offset = machine_a64_frame_offset(
                    placement, placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])] - instruction->payload);
                if (frame_offset <= 4095)
                {
                    machine_a64_emit(&encoder, 0x91000000 | (frame_offset << 10) | ((u32)MACHINE_A64_X28 << 5) | operand_registers[0]);
                }
                else
                {
                    machine_a64_emit_immediate(&encoder, operand_registers[0], frame_offset);
                    machine_a64_emit(&encoder, 0x8b000000 | ((u32)operand_registers[0] << 16) | ((u32)MACHINE_A64_X28 << 5) | operand_registers[0]);
                }
            }
            break;
            case MACHINE_A64_LEA_OFFSET:
            {
                u32 displacement = instruction->payload;
                if (displacement <= 4095)
                {
                    machine_a64_emit(&encoder, 0x91000000 | (displacement << 10) | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                }
                else if (operand_registers[0] != operand_registers[1])
                {
                    machine_a64_emit_immediate(&encoder, operand_registers[0], displacement);
                    machine_a64_emit(&encoder, 0x8b000000 | ((u32)operand_registers[0] << 16) | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                }
                else
                {
                    machine_a64_emit_immediate(&encoder, MACHINE_A64_X16, displacement);
                    machine_a64_emit(&encoder, 0x8b000000 | ((u32)MACHINE_A64_X16 << 16) | ((u32)operand_registers[1] << 5) | operand_registers[0]);
                }
            }
            break;
            case MACHINE_A64_B:
            {
                MachineA64BranchFixup* fixup = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                *fixup = (MachineA64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[0]),
                };
                machine_a64_emit(&encoder, 0x14000000);
            }
            break;
            case MACHINE_A64_BCC:
            {
                MachineA64BranchFixup* taken = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                *taken = (MachineA64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[0]),
                    .form = 1,
                };
                machine_a64_emit(&encoder, 0x54000000 | (instruction->payload & 0xf));
                MachineA64BranchFixup* fallthrough = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                *fallthrough = (MachineA64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[1]),
                };
                machine_a64_emit(&encoder, 0x14000000);
            }
            break;
            case MACHINE_A64_RET:
            {
                // The canonical epilogue: restore the stack pointer from
                // the frame base, reload the caller's x28, release the
                // frame, restore the frame-pointer pair, return. The
                // epilogue start is recorded for the Windows unwind data.
                u32* epilog = (u32*)machine_stream_append(arena, &epilogs);
                *epilog = encoder.count;
                machine_a64_emit(&encoder, 0x9100039f);
                machine_a64_emit(&encoder, 0xf94003e0 | ((placement->frame_size / 8) << 10) | (MACHINE_A64_SP << 5) | MACHINE_A64_X28);
                u32 release_remaining = frame_total;
                while (release_remaining)
                {
                    u32 release_chunk = BUSTER_MIN(release_remaining, 4080u);
                    machine_a64_emit(&encoder, 0x910003ff | (release_chunk << 10));
                    release_remaining -= release_chunk;
                }
                machine_a64_emit(&encoder, 0xa8c17bfd);
                machine_a64_emit(&encoder, 0xd65f03c0);
            }
            break;
            case MACHINE_A64_BRK:
            case MACHINE_A64_UDF:
                machine_a64_emit(&encoder, 0xd4200000);
                break;
            case MACHINE_A64_READ_SP:
                machine_a64_emit(&encoder, 0x910003e0 | operand_registers[0]);
                break;
            case MACHINE_A64_WRITE_SP:
                machine_a64_emit(&encoder, 0x9100001f | ((u32)operand_registers[0] << 5));
                break;
            case MACHINE_A64_CALL_DIRECT:
            {
                MachineCallSite* site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                *site = (MachineCallSite){
                    .code_offset = encoder.count,
                    .target = instruction->payload,
                };
                machine_a64_emit(&encoder, 0x94000000);
            }
            break;
            case MACHINE_A64_CALL_INDIRECT:
                machine_a64_emit(&encoder, 0xd63f0000 | ((u32)operand_registers[0] << 5));
                break;
            case MACHINE_A64_LEA_SYMBOL:
            {
                // The canonical inline-literal form: load the eight-byte
                // literal two words ahead, branch over it, and let the
                // absolute relocation fill it.
                machine_a64_emit(&encoder, 0x58000040 | operand_registers[0]);
                machine_a64_emit(&encoder, 0x14000003);
                MachineCallSite* site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                *site = (MachineCallSite){
                    .code_offset = encoder.count,
                    .target = instruction->payload,
                    .absolute = 1,
                };
                machine_a64_emit(&encoder, 0);
                machine_a64_emit(&encoder, 0);
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
                    machine_a64_emit_frame_load(&encoder, edit->location, machine_a64_frame_offset(placement, placement->virtual_register_offsets[edit->subject]));
                }
                else if (edit->kind == MACHINE_EDIT_COPY)
                {
                    machine_a64_emit_move(&encoder, edit->location, edit->subject);
                }
                else if (edit->kind == MACHINE_EDIT_REMATERIALIZE)
                {
                    machine_a64_emit_immediate(&encoder, edit->location, function->immediates[edit->subject]);
                }
                else
                {
                    machine_a64_emit_frame_store(&encoder, edit->location, machine_a64_frame_offset(placement, placement->virtual_register_offsets[edit->subject]));
                }
                edit_cursor += 1;
            }
        }
    }
    if (encoder.overflow || encoder.error)
    {
        return result;
    }
    for (MachineBuilderChunk* chunk = fixups.first; chunk; chunk = chunk->next)
    {
        MachineA64BranchFixup* rows = (MachineA64BranchFixup*)(chunk + 1);
        for (u32 row_index = 0; row_index < chunk->count; row_index += 1)
        {
            MachineA64BranchFixup* fixup = rows + row_index;
            u32 word_delta = (result.block_offsets[fixup->block] - fixup->patch_offset) >> 2;
            u32 patched;
            memcpy(&patched, encoder.bytes + fixup->patch_offset, sizeof(patched));
            if (fixup->form)
            {
                patched |= (word_delta & 0x7ffffu) << 5;
            }
            else
            {
                patched |= word_delta & 0x03ffffffu;
            }
            memcpy(encoder.bytes + fixup->patch_offset, &patched, sizeof(patched));
        }
    }
    result.call_sites = arena_allocate(arena, MachineCallSite, call_sites.total_count);
    result.call_site_count = call_sites.total_count;
    machine_stream_flatten(&call_sites, result.call_sites);
    result.epilog_offsets = arena_allocate(arena, u32, epilogs.total_count ? epilogs.total_count : 1);
    result.epilog_count = epilogs.total_count;
    machine_stream_flatten(&epilogs, result.epilog_offsets);
    result.bytes = encoder.bytes;
    result.byte_count = encoder.count;
    result.valid = true;
    return result;
}
