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
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
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
    // The whole callee-saved file, highest register first so the pins
    // collide last with the local scan's own callee-saved bindings, which
    // probe in register order from X19 up.
    .quality_pin_registers = {MACHINE_A64_X27, MACHINE_A64_X26, MACHINE_A64_X25, MACHINE_A64_X24, MACHINE_A64_X23, MACHINE_A64_X22, MACHINE_A64_X21,
                              MACHINE_A64_X20, MACHINE_A64_X19},
    .quality_pin_register_count = 9,
};

BUSTER_F_DECL MachineTargetDescription const* machine_target_aarch64(void)
{
    return &machine_aarch64_description;
}

// How one AAPCS64 signature value travels: one integer part for the scalar
// subset, one float part for a float scalar, one or two integer parts for a
// register aggregate, up to four float parts for an HFA, or indirect for a
// large result through the X8 pointer. Anything else (indirect arguments,
// stack arguments, vectors) is outside the subset.
typedef struct MachineA64ValueShape MachineA64ValueShape;
struct MachineA64ValueShape
{
    u32 part_offsets[4];
    u8 part_is_float[4];
    u8 part_sizes[4];
    u32 part_count;
    u32 byte_size;
    bool aggregate;
    // Indirect result: returned through the caller's buffer named by X8.
    bool indirect;
    u8 reserved[2];
};

// One argument's placement: its shape's parts in consecutive per-class
// registers, integer parts from X0 and float parts from V0. The subset
// carries no stack arguments — placement fails instead.
typedef struct MachineA64ArgumentPlacement MachineA64ArgumentPlacement;
struct MachineA64ArgumentPlacement
{
    u16 first_integer;
    u16 first_float;
};

// A conditional branch whose condition chain folded into the branch: the
// terminator re-selects the innermost comparison as CMP (or the truthiness
// test as CMP_ZERO) immediately before BCC, and every absorbed chain
// member selects into nothing. Indexed by the branch's condition value; a
// condition of 0xff means no fusion (0 is a valid a64 condition).
typedef struct MachineA64BranchFusion MachineA64BranchFusion;
struct MachineA64BranchFusion
{
    u32 left;  // value the CMP/CMP_ZERO reads
    u32 right; // CMP's second value, or UINT32_MAX for the CMP_ZERO form
    u8 condition;
    u8 wide;
    u8 reserved[2];
};

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
    // Register shape and placement per parameter, plus the return shape,
    // computed once from the IR-owned AAPCS64 classification.
    MachineA64ValueShape parameter_shapes[MACHINE_A64_MAX_ARGUMENTS];
    MachineA64ArgumentPlacement parameter_placements[MACHINE_A64_MAX_ARGUMENTS];
    MachineA64ValueShape return_shape;
    // Frame slot holding the incoming hidden result pointer, or UINT32_MAX.
    u32 hidden_return_slot;
    // Definition point per virtual register, patched into the flattened
    // rows because builder chunks are write-once.
    u32* virtual_register_definitions;
    // Per IrValue: the fusion a BRANCH_IF on that condition value selects,
    // and whether the value is a chain member that selects into nothing.
    MachineA64BranchFusion* branch_fusions;
    u8* fused_dead;
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

// Float scalars travel as bit images in general registers exactly like the
// x86-64 machine path, bridging into the vector file only at the ABI
// boundaries; the bits above a 32-bit image are unspecified, which AAPCS64
// permits for every float passing site.
BUSTER_GLOBAL_LOCAL bool machine_a64_type_is_float_scalar(IrType* type)
{
    return type && type->layout.resolved && type->kind == IR_TYPE_FLOAT && (type->bit_width == 32 || type->bit_width == 64);
}

BUSTER_GLOBAL_LOCAL bool machine_a64_value_shape(IrProgram* program, IrTypeId type_id, Target target, IrAbiUse use, MachineA64ValueShape* shape)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (machine_a64_type_is_scalar_register(type))
    {
        *shape = (MachineA64ValueShape){
            .part_sizes = {8},
            .part_count = 1,
            .byte_size = 8,
        };
        return true;
    }
    if (machine_a64_type_is_float_scalar(type))
    {
        *shape = (MachineA64ValueShape){
            .part_is_float = {1},
            .part_sizes = {(u8)(type->bit_width / 8)},
            .part_count = 1,
            .byte_size = 8,
        };
        return true;
    }
    if (!type || !type->layout.resolved || (type->kind != IR_TYPE_STRUCT && type->kind != IR_TYPE_UNION && type->kind != IR_TYPE_SLICE))
    {
        return false;
    }
    if (type->layout.size > UINT32_MAX - 7)
    {
        return false;
    }
    IrAbiValue abi = ir_type_abi_value(program, type_id, ir_abi_convention_for_target(target), use);
    if (abi.indirect || abi.memory || !abi.part_count || abi.part_count > 4)
    {
        if (use == IR_ABI_USE_RESULT && abi.indirect)
        {
            // Large results return through the caller's X8-named buffer.
            *shape = (MachineA64ValueShape){
                .byte_size = (u32)((type->layout.size + 7) & ~(u64)7),
                .aggregate = true,
                .indirect = true,
            };
            return true;
        }
        // Indirect arguments would need a caller-side defensive copy the
        // subset does not stage; they keep the canonical path.
        return false;
    }
    MachineA64ValueShape built = {
        .part_count = abi.part_count,
        .byte_size = (u32)((type->layout.size + 7) & ~(u64)7),
        .aggregate = true,
    };
    for (u32 part_index = 0; part_index < abi.part_count; part_index += 1)
    {
        bool part_float = abi.parts[part_index].abi_class == IR_ABI_CLASS_FLOAT;
        if ((abi.parts[part_index].abi_class != IR_ABI_CLASS_INTEGER && abi.parts[part_index].abi_class != IR_ABI_CLASS_POINTER && !part_float) ||
            abi.parts[part_index].size > 8 || (part_float && abi.parts[part_index].size != 4 && abi.parts[part_index].size != 8))
        {
            return false;
        }
        built.part_offsets[part_index] = abi.parts[part_index].value_offset;
        built.part_is_float[part_index] = part_float ? 1 : 0;
        built.part_sizes[part_index] = (u8)abi.parts[part_index].size;
    }
    *shape = built;
    return true;
}

// Consecutive-register assignment per class: integer parts take the next
// X argument register, float parts the next V register. AAPCS64 stack
// arguments stay outside the subset, so overflow fails the placement
// instead of spilling to the outgoing area.
BUSTER_GLOBAL_LOCAL bool machine_a64_place_argument(MachineA64ValueShape* shape, u32* integer_count, u32* float_count,
                                                    MachineA64ArgumentPlacement* placement)
{
    u32 integer_parts = 0;
    u32 float_parts = 0;
    for (u32 part_index = 0; part_index < shape->part_count; part_index += 1)
    {
        integer_parts += shape->part_is_float[part_index] == 0;
        float_parts += shape->part_is_float[part_index] != 0;
    }
    if (*integer_count + integer_parts > 8 || *float_count + float_parts > 8)
    {
        return false;
    }
    *placement = (MachineA64ArgumentPlacement){
        .first_integer = (u16)*integer_count,
        .first_float = (u16)*float_count,
    };
    *integer_count += integer_parts;
    *float_count += float_parts;
    return true;
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

// The a64 condition code for an integer/pointer/boolean comparison, or
// UINT32_MAX for everything else (0 is EQ). The codes pair as exact
// complements, so negation is condition ^ 1.
BUSTER_GLOBAL_LOCAL u32 machine_a64_condition_from_comparison(IrBinaryOperation operation)
{
    switch (operation)
    {
        break;
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_POINTER_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL:
        return MACHINE_A64_CONDITION_EQUAL;
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_POINTER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL:
        return MACHINE_A64_CONDITION_NOT_EQUAL;
    case IR_BINARY_SIGNED_LESS:
        return MACHINE_A64_CONDITION_LESS;
    case IR_BINARY_SIGNED_LESS_EQUAL:
        return MACHINE_A64_CONDITION_LESS_EQUAL;
    case IR_BINARY_SIGNED_GREATER:
        return MACHINE_A64_CONDITION_GREATER;
    case IR_BINARY_SIGNED_GREATER_EQUAL:
        return MACHINE_A64_CONDITION_GREATER_EQUAL;
    case IR_BINARY_UNSIGNED_LESS:
        return MACHINE_A64_CONDITION_BELOW;
    case IR_BINARY_UNSIGNED_LESS_EQUAL:
        return MACHINE_A64_CONDITION_BELOW_EQUAL;
    case IR_BINARY_UNSIGNED_GREATER:
        return MACHINE_A64_CONDITION_ABOVE;
    case IR_BINARY_UNSIGNED_GREATER_EQUAL:
        return MACHINE_A64_CONDITION_ABOVE_EQUAL;
    default:
        return UINT32_MAX;
    }
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
    if (definition->opcode == IR_OPCODE_LOCAL && selector->value_virtual_registers[base.value] != UINT32_MAX)
    {
        // A promoted local has no address. The promotability scan proved
        // no use needs one, so a request here is a selector hole — refuse
        // to the canonical fallback rather than hand a register's value
        // out as an address.
        return false;
    }
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
        // A branch-fusion chain member selects into nothing: the branch
        // re-selects the compare at its own row, and the member's only
        // consumer is the chain. Every marked member is pure.
        if (selector->fused_dead[instruction->result.value])
        {
            return true;
        }
        result_register = selector->value_virtual_registers[instruction->result.value];
    }
    switch (instruction->opcode)
    {
        break;
    case IR_OPCODE_LOCAL:
    {
        // Direct locals produce no code: the stack slot recorded during
        // classification is the storage, exactly like the canonical path —
        // or, promoted, the virtual register is.
        return selector->value_stack_slots[instruction->result.value] != UINT32_MAX ||
               selector->value_virtual_registers[instruction->result.value] != UINT32_MAX;
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
        return argument_index < MACHINE_A64_MAX_ARGUMENTS &&
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
        u32 condition = machine_a64_condition_from_comparison(instruction->binary_operation);
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
        if (result_register == source_register)
        {
            // Aliased through the pointer chain: the dereference is a
            // name for the promoted local, not code.
            return true;
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
        MachineA64ValueShape callee_return_shape = {0};
        if (callee_returns_value && !machine_a64_value_shape(program, callee_type->return_type, selector->target, IR_ABI_USE_RESULT, &callee_return_shape))
        {
            return false;
        }
        MachineA64ValueShape argument_shapes[MACHINE_A64_MAX_ARGUMENTS];
        MachineA64ArgumentPlacement argument_placements[MACHINE_A64_MAX_ARGUMENTS];
        u32 call_integer_count = 0;
        u32 call_float_count = 0;
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
                indirect_result_slot = machine_a64_append_slot(selector, callee_return_shape.byte_size, 8);
            }
        }
        u32 argument_registers[MACHINE_A64_MAX_ARGUMENTS];
        u32 argument_slots[MACHINE_A64_MAX_ARGUMENTS];
        for (u32 argument_index = 0; argument_index < call_argument_count; argument_index += 1)
        {
            IrTypeId argument_type_id = argument_index < callee_type->parameter_count
                                            ? callee_type->parameter_types[argument_index]
                                            : function->values[instruction->operands[argument_index + 1].value].canonical_type;
            if (!machine_a64_value_shape(program, argument_type_id, selector->target, IR_ABI_USE_ARGUMENT, argument_shapes + argument_index) ||
                !machine_a64_place_argument(argument_shapes + argument_index, &call_integer_count, &call_float_count,
                                            argument_placements + argument_index))
            {
                return false;
            }
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
            if (!machine_a64_operand_register(selector, instruction->operands[argument_index + 1], argument_registers + argument_index))
            {
                return false;
            }
        }
        // Explicit fixed-register argument staging: integer parts load
        // directly into their X registers (never through a scratch that
        // could disturb an already placed argument), and float parts
        // bridge into their V registers, which no general-register write
        // can touch. The hidden result pointer rides X8, outside the
        // argument file.
        if (callee_return_shape.indirect)
        {
            u32 result_pointer_register = machine_a64_synthesize_register(selector);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_pointer_register),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, indirect_result_slot)},
                                                 .opcode = MACHINE_A64_LEA_FRAME,
                                             });
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_A64_X8),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_pointer_register)},
                                                 .opcode = MACHINE_A64_MOV_RR,
                                             });
        }
        for (u32 argument_index = 0; argument_index < call_argument_count; argument_index += 1)
        {
            MachineA64ValueShape* shape = argument_shapes + argument_index;
            u32 next_integer = argument_placements[argument_index].first_integer;
            u32 next_float = argument_placements[argument_index].first_float;
            if (shape->aggregate)
            {
                for (u32 part_index = 0; part_index < shape->part_count; part_index += 1)
                {
                    bool part_float = shape->part_is_float[part_index] != 0;
                    u16 part_load_opcode =
                        (u16)(part_float && shape->part_sizes[part_index] == 4 ? MACHINE_A64_LOAD_FRAME32 : MACHINE_A64_LOAD_FRAME);
                    if (part_float)
                    {
                        u32 bounce_register = machine_a64_synthesize_register(selector);
                        machine_a64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register),
                                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, argument_slots[argument_index])},
                                                             .payload = shape->part_offsets[part_index],
                                                             .opcode = part_load_opcode,
                                                         });
                        machine_a64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                             .payload = next_float,
                                                             .opcode = MACHINE_A64_FMOV_TO_VEC,
                                                         });
                        next_float += 1;
                    }
                    else
                    {
                        machine_a64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, next_integer),
                                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, argument_slots[argument_index])},
                                                             .payload = shape->part_offsets[part_index],
                                                             .opcode = MACHINE_A64_LOAD_FRAME,
                                                         });
                        next_integer += 1;
                    }
                }
                continue;
            }
            if (shape->part_is_float[0])
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_registers[argument_index])},
                                                     .payload = next_float,
                                                     .opcode = MACHINE_A64_FMOV_TO_VEC,
                                                 });
                continue;
            }
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, next_integer),
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
        if (callee_returns_value && instruction->result.value != IR_ID_UNDERLYING_INVALID)
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
                        u32 bounce_register = machine_a64_synthesize_register(selector);
                        machine_a64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                             .payload = return_float_index,
                                                             .opcode = MACHINE_A64_FMOV_FROM_VEC,
                                                         });
                        machine_a64_select_row(
                            selector, (MachineInstruction){
                                          .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                       machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                          .payload = callee_return_shape.part_offsets[part_index],
                                          .opcode = (u16)(callee_return_shape.part_sizes[part_index] == 4 ? MACHINE_A64_STORE_FRAME32
                                                                                                          : MACHINE_A64_STORE_FRAME64),
                                      });
                        return_float_index += 1;
                        continue;
                    }
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                      machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, return_integer_index)},
                                                         .payload = callee_return_shape.part_offsets[part_index],
                                                         .opcode = MACHINE_A64_STORE_FRAME64,
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
                row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                           .opcode = MACHINE_A64_FMOV_FROM_VEC,
                                                       });
            }
            else
            {
                row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_A64_X0)},
                                                           .opcode = MACHINE_A64_MOV_RR,
                                                       });
            }
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
        u32 result_slot = selector->value_stack_slots[instruction->result.value];
        if (result_register == UINT32_MAX && result_slot != UINT32_MAX)
        {
            // Aggregate load: exact-size copy into the result slot, from a
            // direct local slot or through an address vreg.
            IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
            if (!loaded_type || !loaded_type->layout.resolved || loaded_type->layout.size > UINT32_MAX)
            {
                return false;
            }
            if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                     .payload = (u32)loaded_type->layout.size,
                                                     .opcode = MACHINE_A64_COPY_FRAME_FROM_FRAME,
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
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                     .payload = (u32)loaded_type->layout.size,
                                                     .opcode = MACHINE_A64_COPY_FRAME_FROM_PTR,
                                                 });
                return true;
            }
            return false;
        }
        if (result_register == UINT32_MAX)
        {
            return false;
        }
        if (definition->opcode == IR_OPCODE_LOCAL && selector->value_virtual_registers[instruction->operands[0].value] != UINT32_MAX)
        {
            if (result_register == selector->value_virtual_registers[instruction->operands[0].value])
            {
                // Aliased: the load is a name for the local, not code.
                return true;
            }
            // Promoted but not aliasable here: the load is a register copy.
            u32 row = machine_a64_select_row(
                selector, (MachineInstruction){
                              .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                           machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, selector->value_virtual_registers[instruction->operands[0].value])},
                              .opcode = MACHINE_A64_MOV_RR,
                          });
            machine_a64_define(selector, result_register, row);
            return true;
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
        u32 value_slot = selector->value_stack_slots[instruction->operands[1].value];
        if (value_slot != UINT32_MAX && selector->value_virtual_registers[instruction->operands[1].value] == UINT32_MAX)
        {
            // Aggregate store: exact-size copy out of the value slot.
            if (!size || size > UINT32_MAX)
            {
                return false;
            }
            if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = (u32)size,
                                                     .opcode = MACHINE_A64_COPY_FRAME_FROM_FRAME,
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
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = (u32)size,
                                                     .opcode = MACHINE_A64_COPY_PTR_FROM_FRAME,
                                                 });
                return true;
            }
            return false;
        }
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
        if (definition->opcode == IR_OPCODE_LOCAL && selector->value_virtual_registers[instruction->operands[0].value] != UINT32_MAX)
        {
            // Promoted local: the store is a full-width register copy —
            // the same 64-bit image a direct-slot store writes, since the
            // register model keeps every value zero-extended.
            u32 place_register = selector->value_virtual_registers[instruction->operands[0].value];
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, place_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                           .opcode = MACHINE_A64_MOV_RR,
                                                       });
            machine_a64_define(selector, place_register, row);
            return true;
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
        // A fused condition re-selects the chain's innermost comparison
        // here, immediately before BCC: only allocator edits can land
        // between the flags define and its use, and every edit form is a
        // flag-preserving instruction (ldr/str, orr copy, movz/movk).
        MachineA64BranchFusion* fusion =
            instruction->operands[0].value < function->value_count ? selector->branch_fusions + instruction->operands[0].value : 0;
        if (fusion && fusion->condition != 0xff)
        {
            u32 left_register;
            u32 right_register;
            if (!machine_a64_operand_register(selector, (IrValueId){.value = fusion->left}, &left_register))
            {
                return false;
            }
            if (fusion->right != UINT32_MAX)
            {
                if (!machine_a64_operand_register(selector, (IrValueId){.value = fusion->right}, &right_register))
                {
                    return false;
                }
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                     .opcode = (u16)(fusion->wide ? MACHINE_A64_CMP64 : MACHINE_A64_CMP32),
                                                 });
            }
            else
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register)},
                                                     .opcode = MACHINE_A64_CMP_ZERO,
                                                 });
            }
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value),
                                                              machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[1].value)},
                                                 .payload = fusion->condition,
                                                 .opcode = MACHINE_A64_BCC,
                                             });
            return true;
        }
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
            if (selector->return_shape.indirect)
            {
                // Copy the value through the caller's buffer, whose
                // address the entry saved from X8. AAPCS64 does not
                // return the pointer, so nothing writes X0.
                u32 value_slot = instruction->operands[0].value < function->value_count
                                     ? selector->value_stack_slots[instruction->operands[0].value]
                                     : UINT32_MAX;
                if (value_slot == UINT32_MAX || selector->hidden_return_slot == UINT32_MAX)
                {
                    return false;
                }
                u32 pointer_register = machine_a64_synthesize_register(selector);
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, selector->hidden_return_slot)},
                                                     .opcode = MACHINE_A64_LOAD_FRAME,
                                                 });
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = selector->return_shape.byte_size,
                                                     .opcode = MACHINE_A64_COPY_PTR_FROM_FRAME,
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
                        u32 bounce_register = machine_a64_synthesize_register(selector);
                        machine_a64_select_row(
                            selector, (MachineInstruction){
                                          .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register),
                                                       machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                          .payload = selector->return_shape.part_offsets[part_index],
                                          .opcode = (u16)(selector->return_shape.part_sizes[part_index] == 4 ? MACHINE_A64_LOAD_FRAME32
                                                                                                             : MACHINE_A64_LOAD_FRAME),
                                      });
                        machine_a64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                             .payload = return_float_index,
                                                             .opcode = MACHINE_A64_FMOV_TO_VEC,
                                                         });
                        return_float_index += 1;
                        continue;
                    }
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, return_integer_index),
                                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                         .payload = selector->return_shape.part_offsets[part_index],
                                                         .opcode = MACHINE_A64_LOAD_FRAME,
                                                     });
                    return_integer_index += 1;
                }
            }
            else if (selector->return_shape.part_is_float[0])
            {
                u32 value_register;
                if (!machine_a64_operand_register(selector, instruction->operands[0], &value_register))
                {
                    return false;
                }
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_A64_FMOV_TO_VEC,
                                                 });
            }
            else
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
    // The AAPCS64 shape gate: every parameter and the return value must
    // classify to register parts — integer scalars, float scalars,
    // one-or-two-part register aggregates, HFAs — or an indirect result.
    // Indirect and stack arguments stay canonical.
    IrType* return_type = ir_type_from_id(&program->types, function_type->return_type);
    bool returns_value = return_type && return_type->kind != IR_TYPE_VOID;
    MachineA64ValueShape signature_return_shape = {0};
    if (returns_value && !machine_a64_value_shape(program, function_type->return_type, target, IR_ABI_USE_RESULT, &signature_return_shape))
    {
        return result;
    }
    MachineA64ValueShape signature_parameter_shapes[MACHINE_A64_MAX_ARGUMENTS] = {0};
    MachineA64ArgumentPlacement signature_parameter_placements[MACHINE_A64_MAX_ARGUMENTS] = {0};
    u32 signature_integer_count = 0;
    u32 signature_float_count = 0;
    for (u32 parameter_index = 0; parameter_index < function_type->parameter_count; parameter_index += 1)
    {
        if (!machine_a64_value_shape(program, function_type->parameter_types[parameter_index], target, IR_ABI_USE_ARGUMENT,
                                     signature_parameter_shapes + parameter_index) ||
            !machine_a64_place_argument(signature_parameter_shapes + parameter_index, &signature_integer_count, &signature_float_count,
                                        signature_parameter_placements + parameter_index))
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
    selector.return_shape = signature_return_shape;
    selector.hidden_return_slot = UINT32_MAX;
    if (signature_return_shape.indirect)
    {
        selector.hidden_return_slot = machine_a64_append_slot(&selector, 8, 8);
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
    // Local promotion, the mem2reg the machine path was built for: a
    // scalar local whose address never leaves a load or a store needs no
    // memory at all. Two passes — eligibility by type (a 4- or 8-byte
    // scalar register type), then disqualification by any use that is not
    // the place operand of a same-width scalar load or store: a field or
    // index selection, an address handed to a call, a mixed-width access,
    // and the volatile forms all keep the local in its slot. The byte size
    // is recorded so the width check needs no second type walk.
    u8* promotable_locals = arena_allocate(arena, u8, function->value_count ? function->value_count : 1);
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        promotable_locals[value_index] = 0;
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = function->instructions[id.value].next)
        {
            IrInstruction* instruction = function->instructions + id.value;
            if (instruction->opcode != IR_OPCODE_LOCAL || instruction->result.value == IR_ID_UNDERLYING_INVALID ||
                instruction->result.value >= function->value_count)
            {
                continue;
            }
            IrType* local_type = ir_type_from_id(&program->types, function->values[instruction->result.value].canonical_type);
            if (machine_a64_type_is_scalar_register(local_type) && (local_type->layout.size == 4 || local_type->layout.size == 8))
            {
                promotable_locals[instruction->result.value] = (u8)local_type->layout.size;
            }
        }
    }
    u32* value_last_use_ordinals = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* value_use_blocks = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* local_store_counts = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* value_use_counts = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* value_def_rows = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* value_def_blocks = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* value_def_ordinals = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        value_last_use_ordinals[value_index] = 0;
        value_use_blocks[value_index] = UINT32_MAX;
        local_store_counts[value_index] = 0;
        value_use_counts[value_index] = 0;
        value_def_rows[value_index] = UINT32_MAX;
        value_def_blocks[value_index] = UINT32_MAX;
        value_def_ordinals[value_index] = 0;
    }
    u32 walk_ordinal = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = function->instructions[id.value].next)
        {
            IrInstruction* instruction = function->instructions + id.value;
            walk_ordinal += 1;
            if (instruction->result.value != IR_ID_UNDERLYING_INVALID && instruction->result.value < function->value_count)
            {
                value_def_rows[instruction->result.value] = id.value;
                value_def_blocks[instruction->result.value] = block_index;
                value_def_ordinals[instruction->result.value] = walk_ordinal;
            }
            for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
            {
                u32 used = instruction->operands[operand_index].value;
                if (used >= function->value_count)
                {
                    continue;
                }
                value_last_use_ordinals[used] = walk_ordinal;
                value_use_counts[used] += 1;
                value_use_blocks[used] = value_use_blocks[used] == UINT32_MAX || value_use_blocks[used] == block_index ? block_index : UINT32_MAX - 1;
                if (!promotable_locals[used])
                {
                    continue;
                }
                bool place_use = false;
                if (operand_index == 0 && instruction->opcode == IR_OPCODE_LOAD)
                {
                    IrType* access_type = ir_type_from_id(&program->types, instruction->canonical_type);
                    place_use = !instruction->volatile_access && machine_a64_type_is_scalar_register(access_type) &&
                                access_type->layout.size == promotable_locals[used];
                }
                else if (operand_index == 0 && instruction->opcode == IR_OPCODE_STORE && instruction->operand_count >= 2 &&
                         instruction->operands[1].value < function->value_count)
                {
                    IrType* access_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
                    place_use = !instruction->volatile_access && machine_a64_type_is_scalar_register(access_type) &&
                                access_type->layout.size == promotable_locals[used];
                    local_store_counts[used] += 1;
                }
                if (!place_use)
                {
                    promotable_locals[used] = 0;
                }
            }
        }
    }
    // Load aliasing over the promoted locals. The measured cost of
    // promotion is the copy every load lowers to: its source is the local,
    // which lives on, so the copy never coalesces — one extra register
    // move per read, and a second instruction whenever the local was not
    // resident. A load result whose every use sits in the load's own block
    // before the local's next store *is* the local: it shares the local's
    // virtual register and the load selects into nothing. The block-local
    // requirement is what makes the layout reasoning sound — a jump can
    // only re-enter at a block head, above the load, never between the
    // load and a use.
    u32* load_aliases = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* next_store_ordinals = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* next_store_epochs = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* block_row_ids = arena_allocate(arena, u32, function->instruction_count ? function->instruction_count : 1);
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        load_aliases[value_index] = UINT32_MAX;
        next_store_ordinals[value_index] = 0;
        next_store_epochs[value_index] = 0;
    }
    // Two identical reverse sweeps: the first aliases loads, the second
    // extends each alias through IR_OPCODE_DEREFERENCE, whose selection is
    // a plain pointer copy — an aliased pointer load feeding a dereference
    // used to coalesce because the load temporary died at the copy, and an
    // aliased value never dies, so the chain must collapse at selection or
    // the copy just moves from the load to the address staging. A field or
    // index base folds its offset into a real address add and stops the
    // chain.
    for (u32 alias_sweep = 0; alias_sweep < 2; alias_sweep += 1)
    {
        u32 walked_ordinals = 0;
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            u32 epoch = alias_sweep * function->block_count + block_index + 1;
            u32 row_count = 0;
            for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = function->instructions[id.value].next)
            {
                block_row_ids[row_count++] = id.value;
            }
            for (u32 remaining = row_count; remaining > 0; remaining -= 1)
            {
                IrInstruction* instruction = function->instructions + block_row_ids[remaining - 1];
                u32 instruction_ordinal = walked_ordinals + remaining;
                if (instruction->opcode == IR_OPCODE_STORE && instruction->operand_count >= 1 && instruction->operands[0].value < function->value_count &&
                    promotable_locals[instruction->operands[0].value])
                {
                    next_store_ordinals[instruction->operands[0].value] = instruction_ordinal;
                    next_store_epochs[instruction->operands[0].value] = epoch;
                }
                // The rooting local of a candidate: the load's own place,
                // or the alias the previous sweep gave a dereference's
                // pointer operand.
                u32 root = UINT32_MAX;
                if (instruction->opcode == IR_OPCODE_LOAD && instruction->operand_count >= 1 && instruction->operands[0].value < function->value_count &&
                    promotable_locals[instruction->operands[0].value])
                {
                    root = instruction->operands[0].value;
                }
                else if (instruction->opcode == IR_OPCODE_DEREFERENCE && instruction->operand_count >= 1 &&
                         instruction->operands[0].value < function->value_count)
                {
                    root = load_aliases[instruction->operands[0].value];
                }
                if (root == UINT32_MAX || instruction->result.value == IR_ID_UNDERLYING_INVALID || instruction->result.value >= function->value_count)
                {
                    continue;
                }
                u32 candidate = instruction->result.value;
                // A single-store local — a saved parameter, an init-once
                // configuration value — never changes after its one store,
                // so every read of it everywhere is the local, uses in
                // other blocks included. Otherwise the reverse walk means
                // next_store already names the nearest store strictly
                // below this row when its epoch stamp is current, and the
                // block-local containment is what keeps the layout
                // reasoning sound: a jump can only re-enter at a block
                // head, above the row, never between it and a use.
                if (local_store_counts[root] == 1 ||
                    (value_use_blocks[candidate] == block_index &&
                     (next_store_epochs[root] != epoch || next_store_ordinals[root] > value_last_use_ordinals[candidate])))
                {
                    load_aliases[candidate] = root;
                }
            }
            walked_ordinals += row_count;
        }
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
                if (promotable_locals[instruction->result.value])
                {
                    // Promoted: the local is a virtual register for its
                    // whole life and never owns a frame slot. Its loads
                    // and stores lower to copies, and its definition point
                    // is patched at the first store like any other
                    // classification vreg.
                    selector.value_virtual_registers[instruction->result.value] =
                        machine_builder_virtual_register(&selector.builder, (MachineVirtualRegister){
                                                                                .definition_point = MACHINE_POINT_INVALID,
                                                                                .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                                .typed_origin = instruction->result.value,
                                                                            });
                    continue;
                }
                selector.value_stack_slots[instruction->result.value] =
                    machine_a64_append_slot(&selector, (u32)((local_type->layout.size + 7) & ~(u64)7), local_alignment);
                continue;
            }
            IrType* value_type = ir_type_from_id(&program->types, value->canonical_type);
            // Float scalars hold their bit image in a general register, and
            // address producers hold an 8-byte address no matter what their
            // declared canonical type is.
            if (machine_a64_type_is_scalar_register(value_type) || machine_a64_type_is_float_scalar(value_type) ||
                machine_a64_opcode_produces_address(instruction->opcode))
            {
                u32 register_index = machine_builder_virtual_register(&selector.builder, (MachineVirtualRegister){
                                                                                             .definition_point = MACHINE_POINT_INVALID,
                                                                                             .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                                             .typed_origin = instruction->result.value,
                                                                                         });
                selector.value_virtual_registers[instruction->result.value] = register_index;
            }
            else if ((instruction->opcode == IR_OPCODE_ARGUMENT || instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_CALL ||
                      instruction->opcode == IR_OPCODE_AGGREGATE || instruction->opcode == IR_OPCODE_ARRAY) &&
                     value_type && value_type->layout.resolved && value_type->layout.size <= UINT32_MAX - 7 &&
                     (value_type->kind == IR_TYPE_STRUCT || value_type->kind == IR_TYPE_UNION || value_type->kind == IR_TYPE_SLICE ||
                      ((instruction->opcode == IR_OPCODE_ARRAY || instruction->opcode == IR_OPCODE_LOAD) && value_type->kind == IR_TYPE_ARRAY)))
            {
                // Aggregate values own a frame slot like the canonical
                // path's per-value storage; copies and ABI part transfers
                // address it directly.
                selector.value_stack_slots[instruction->result.value] =
                    machine_a64_append_slot(&selector, (u32)((value_type->layout.size + 7) & ~(u64)7), 8);
            }
        }
    }
    // Aliased load results share their local's virtual register: every use
    // site then names the local directly and the load emits nothing. The
    // result's own classification vreg goes unused, which costs an id and
    // nothing else.
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        if (load_aliases[value_index] != UINT32_MAX && selector.value_virtual_registers[load_aliases[value_index]] != UINT32_MAX)
        {
            selector.value_virtual_registers[value_index] = selector.value_virtual_registers[load_aliases[value_index]];
        }
    }
    // Compare/branch fusion, mirroring the x86-64 selector: a chain of
    // compare → widen → (!= 0) whose every member has exactly one use in
    // the branch's own block folds into CMP (or CMP_ZERO) + BCC at the
    // terminator, and the members select into nothing. The walk keeps the
    // invariant that the branch outcome equals truthy(chain value) xor
    // negate through (!= 0)/(== 0) against a literal zero (possibly
    // behind one widening cast), truthiness-preserving extensions, and
    // BOOLEAN_NOT. The fused compare reads its operands at the branch row
    // instead of the member's, and promoted locals are the one source of
    // multi-definition vregs: a store between the compare and the branch
    // would redefine what the sunk read sees, so the walk stamps every
    // promoted local's latest store ordinal and the commit compares.
    selector.branch_fusions = arena_allocate(arena, MachineA64BranchFusion, function->value_count ? function->value_count : 1);
    selector.fused_dead = arena_allocate(arena, u8, function->value_count ? function->value_count : 1);
    u32* local_store_ordinals = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        selector.branch_fusions[value_index] = (MachineA64BranchFusion){.condition = 0xff};
        selector.fused_dead[value_index] = 0;
        local_store_ordinals[value_index] = 0;
    }
    u32 fusion_ordinal = 0;
    for (u32 block_index = 0; block_index < function->block_count && selector.supported; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = function->instructions[id.value].next)
        {
            IrInstruction* instruction = function->instructions + id.value;
            fusion_ordinal += 1;
            if (instruction->opcode == IR_OPCODE_STORE && instruction->operand_count >= 1 && instruction->operands[0].value < function->value_count &&
                promotable_locals[instruction->operands[0].value])
            {
                local_store_ordinals[instruction->operands[0].value] = fusion_ordinal;
            }
            if (instruction->opcode != IR_OPCODE_BRANCH_IF || instruction->operand_count < 1 || instruction->operands[0].value >= function->value_count)
            {
                continue;
            }
            u32 chain_value = instruction->operands[0].value;
            u32 absorbed_members[16];
            u32 absorbed_count = 0;
            u32 dead_zeros[16];
            u32 dead_zero_count = 0;
            u32 negate = 0;
            u32 innermost_ordinal = 0;
            u32 read_left = UINT32_MAX;
            u32 read_right = UINT32_MAX;
            u32 condition = 0xff;
            bool wide = false;
            while (absorbed_count < BUSTER_ARRAY_LENGTH(absorbed_members))
            {
                if (chain_value >= function->value_count || value_use_counts[chain_value] != 1 || value_def_blocks[chain_value] != block_index ||
                    value_def_rows[chain_value] == UINT32_MAX)
                {
                    break;
                }
                IrInstruction* member = function->instructions + value_def_rows[chain_value];
                if (member->opcode == IR_OPCODE_BINARY && member->operand_count >= 2 && member->operands[0].value < function->value_count &&
                    member->operands[1].value < function->value_count)
                {
                    u32 member_condition = machine_a64_condition_from_comparison(member->binary_operation);
                    IrTypeId member_operand_type_id = function->values[member->operands[0].value].canonical_type;
                    if (member_condition == UINT32_MAX || !machine_a64_type_is_scalar_register(ir_type_from_id(&program->types, member_operand_type_id)))
                    {
                        break;
                    }
                    if (member_condition == MACHINE_A64_CONDITION_EQUAL || member_condition == MACHINE_A64_CONDITION_NOT_EQUAL)
                    {
                        // A literal zero side may sit behind one widening
                        // cast (extending zero is zero). The cast dies only
                        // when this compare is its one use, and the
                        // constant only once nothing still reads it.
                        u32 zero_side = UINT32_MAX;
                        u32 zero_cast = UINT32_MAX;
                        u32 zero_constant = UINT32_MAX;
                        for (u32 side = 0; side < 2 && zero_side == UINT32_MAX; side += 1)
                        {
                            u32 constant_value = member->operands[side].value;
                            u32 through_cast = UINT32_MAX;
                            if (value_def_rows[constant_value] == UINT32_MAX)
                            {
                                continue;
                            }
                            IrInstruction* side_definition = function->instructions + value_def_rows[constant_value];
                            if (side_definition->opcode == IR_OPCODE_CAST && side_definition->operand_count >= 1 &&
                                side_definition->operands[0].value < function->value_count &&
                                (side_definition->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                                 side_definition->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND ||
                                 side_definition->conversion_operation == IR_CONVERSION_IDENTITY))
                            {
                                through_cast = constant_value;
                                constant_value = side_definition->operands[0].value;
                                if (value_def_rows[constant_value] == UINT32_MAX)
                                {
                                    continue;
                                }
                                side_definition = function->instructions + value_def_rows[constant_value];
                            }
                            if (side_definition->opcode == IR_OPCODE_CONSTANT_INTEGER && side_definition->immediate_count && side_definition->immediates &&
                                side_definition->immediates[0] == 0)
                            {
                                zero_side = side;
                                zero_cast = through_cast;
                                zero_constant = constant_value;
                            }
                        }
                        if (zero_side != UINT32_MAX)
                        {
                            absorbed_members[absorbed_count++] = chain_value;
                            innermost_ordinal = value_def_ordinals[chain_value];
                            bool cast_dies = zero_cast == UINT32_MAX ||
                                             (value_use_counts[zero_cast] == 1 && dead_zero_count < BUSTER_ARRAY_LENGTH(dead_zeros));
                            if (zero_cast != UINT32_MAX && cast_dies)
                            {
                                dead_zeros[dead_zero_count++] = zero_cast;
                            }
                            if (cast_dies && value_use_counts[zero_constant] == 1 && dead_zero_count < BUSTER_ARRAY_LENGTH(dead_zeros))
                            {
                                dead_zeros[dead_zero_count++] = zero_constant;
                            }
                            negate ^= member_condition == MACHINE_A64_CONDITION_EQUAL;
                            chain_value = member->operands[1 - zero_side].value;
                            continue;
                        }
                    }
                    absorbed_members[absorbed_count++] = chain_value;
                    innermost_ordinal = value_def_ordinals[chain_value];
                    read_left = member->operands[0].value;
                    read_right = member->operands[1].value;
                    condition = member_condition ^ negate;
                    wide = machine_a64_type_is_64_bit(program, member_operand_type_id);
                    break;
                }
                if (member->opcode == IR_OPCODE_CAST && member->operand_count >= 1 && member->operands[0].value < function->value_count &&
                    (member->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                     member->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND || member->conversion_operation == IR_CONVERSION_IDENTITY) &&
                    machine_a64_type_is_scalar_register(
                        ir_type_from_id(&program->types, function->values[member->operands[0].value].canonical_type)))
                {
                    absorbed_members[absorbed_count++] = chain_value;
                    innermost_ordinal = value_def_ordinals[chain_value];
                    chain_value = member->operands[0].value;
                    continue;
                }
                if (member->opcode == IR_OPCODE_UNARY && member->unary_operation == IR_UNARY_BOOLEAN_NOT && member->operand_count >= 1 &&
                    member->operands[0].value < function->value_count &&
                    machine_a64_type_is_scalar_register(
                        ir_type_from_id(&program->types, function->values[member->operands[0].value].canonical_type)))
                {
                    absorbed_members[absorbed_count++] = chain_value;
                    innermost_ordinal = value_def_ordinals[chain_value];
                    negate ^= 1;
                    chain_value = member->operands[0].value;
                    continue;
                }
                break;
            }
            if (!absorbed_count)
            {
                continue;
            }
            if (read_left == UINT32_MAX)
            {
                // Truthiness terminal: CMP_ZERO's 64-bit compare is exact
                // because sub-64-bit values sit extended with clean upper
                // bits, matching the compare-against-zero it replaces.
                if (chain_value >= function->value_count ||
                    !machine_a64_type_is_scalar_register(ir_type_from_id(&program->types, function->values[chain_value].canonical_type)))
                {
                    continue;
                }
                read_left = chain_value;
                condition = MACHINE_A64_CONDITION_NOT_EQUAL ^ negate;
            }
            bool reads_safe = selector.value_virtual_registers[read_left] != UINT32_MAX &&
                              (read_right == UINT32_MAX || selector.value_virtual_registers[read_right] != UINT32_MAX);
            for (u32 read_side = 0; read_side < 2 && reads_safe; read_side += 1)
            {
                u32 read_value = read_side ? read_right : read_left;
                if (read_value == UINT32_MAX)
                {
                    continue;
                }
                u32 read_root = load_aliases[read_value];
                if (read_root != UINT32_MAX && local_store_ordinals[read_root] > innermost_ordinal)
                {
                    reads_safe = false;
                }
            }
            if (!reads_safe)
            {
                continue;
            }
            selector.branch_fusions[instruction->operands[0].value] = (MachineA64BranchFusion){
                .left = read_left,
                .right = read_right,
                .condition = (u8)condition,
                .wide = wide,
            };
            for (u32 member_index = 0; member_index < absorbed_count; member_index += 1)
            {
                selector.fused_dead[absorbed_members[member_index]] = 1;
            }
            for (u32 zero_index = 0; zero_index < dead_zero_count; zero_index += 1)
            {
                selector.fused_dead[dead_zeros[zero_index]] = 1;
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
            // general registers; the vector file survives that pass
            // untouched. The hidden result pointer arrives in X8, outside
            // the argument registers.
            if (selector.return_shape.indirect)
            {
                machine_a64_select_row(&selector, (MachineInstruction){
                                                      .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, selector.hidden_return_slot),
                                                                   machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_A64_X8)},
                                                      .opcode = MACHINE_A64_STORE_FRAME64,
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
                    MachineA64ValueShape* shape = selector.parameter_shapes + argument_index;
                    MachineA64ArgumentPlacement* parameter_placement = selector.parameter_placements + argument_index;
                    u32 next_integer = parameter_placement->first_integer;
                    u32 next_float = parameter_placement->first_float;
                    if (shape->aggregate)
                    {
                        u32 slot = selector.value_stack_slots[argument_value];
                        if (slot == UINT32_MAX)
                        {
                            machine_a64_reject(&selector, IR_OPCODE_ARGUMENT);
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
                                u32 bounce_register = machine_a64_synthesize_register(&selector);
                                machine_a64_select_row(&selector, (MachineInstruction){
                                                                      .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                                      .payload = part_float_register,
                                                                      .opcode = MACHINE_A64_FMOV_FROM_VEC,
                                                                  });
                                machine_a64_select_row(&selector,
                                                       (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                           .payload = shape->part_offsets[part_index],
                                                           .opcode = (u16)(shape->part_sizes[part_index] == 4 ? MACHINE_A64_STORE_FRAME32
                                                                                                             : MACHINE_A64_STORE_FRAME64),
                                                       });
                            }
                            else
                            {
                                machine_a64_select_row(&selector, (MachineInstruction){
                                                                      .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                                                   machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, part_integer)},
                                                                      .payload = shape->part_offsets[part_index],
                                                                      .opcode = MACHINE_A64_STORE_FRAME64,
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
                        machine_a64_reject(&selector, IR_OPCODE_ARGUMENT);
                        break;
                    }
                    u32 row;
                    if (scalar_float)
                    {
                        row = machine_a64_select_row(&selector, (MachineInstruction){
                                                                    .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_register)},
                                                                    .payload = next_float,
                                                                    .opcode = MACHINE_A64_FMOV_FROM_VEC,
                                                                });
                    }
                    else
                    {
                        row = machine_a64_select_row(&selector, (MachineInstruction){
                                                                    .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_register),
                                                                                 machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, next_integer)},
                                                                    .opcode = MACHINE_A64_MOV_RR,
                                                                });
                    }
                    machine_a64_define(&selector, argument_register, row);
                }
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
    A64Opcode opcode;
    u16 reserved;
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

BUSTER_GLOBAL_LOCAL void machine_a64_emit_mc(MachineA64Encoder* encoder, A64MCInst instruction)
{
    u32 word = 0;
    if (!a64_mc_encode(&instruction, &word))
    {
        encoder->error = true;
        return;
    }
    machine_a64_emit(encoder, word);
}

// Seeded movz/movn materialization. The seed picks the fill — zeros or
// ones — matching more of the four halfwords, lands on the first
// halfword that differs from the fill (the top one when none does), and
// movk patches only the remaining differing halfwords, so a negative
// costs one movn instead of a movz and three movk. The canonical
// a64_emit_constant keeps its movz-first shape untouched.
BUSTER_GLOBAL_LOCAL void machine_a64_emit_immediate(MachineA64Encoder* encoder, u32 register_number, u64 value)
{
    u32 zero_halfwords = 0;
    u32 ones_halfwords = 0;
    for (u32 shift = 0; shift < 64; shift += 16)
    {
        zero_halfwords += ((value >> shift) & 0xffff) == 0;
        ones_halfwords += ((value >> shift) & 0xffff) == 0xffff;
    }
    u32 fill = ones_halfwords > zero_halfwords ? 0xffffu : 0u;
    u32 seed_shift = 0;
    while (seed_shift < 48 && ((value >> seed_shift) & 0xffff) == fill)
    {
        seed_shift += 16;
    }
    u32 seed_halfword = (u32)((value >> seed_shift) & 0xffff);
    u32 seed_opcode = fill ? 0x92800000 | ((~seed_halfword & 0xffffu) << 5) : 0xd2800000 | (seed_halfword << 5);
    machine_a64_emit(encoder, seed_opcode | ((seed_shift / 16) << 21) | register_number);
    for (u32 shift = seed_shift + 16; shift < 64; shift += 16)
    {
        u32 halfword = (u32)((value >> shift) & 0xffff);
        if (halfword != fill)
        {
            machine_a64_emit(encoder, 0xf2800000 | ((shift / 16) << 21) | (halfword << 5) | register_number);
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

// Sized memory operation through a pointer register with a scaled unsigned
// immediate offset, for the aggregate copy loops; an offset outside the
// imm12 form is an encode error, which falls the function back whole.
BUSTER_GLOBAL_LOCAL void machine_a64_emit_pointer_memory(MachineA64Encoder* encoder, u32 register_number, u32 base_register, u32 offset, u32 size, bool store)
{
    u32 scale = size;
    if ((size != 1 && size != 2 && size != 4 && size != 8) || offset % scale || offset / scale > 4095)
    {
        encoder->error = true;
        return;
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
// X28-relative byte offset is their distance from the top of the frame
// area. The area's first 8 * push_count bytes — the offsets the shared
// placement reserved for the x86-64 pushes — hold the callee-saved saves.
BUSTER_GLOBAL_LOCAL u32 machine_a64_frame_offset(u32 frame_area, u32 placement_offset)
{
    return frame_area - placement_offset;
}

MachineEncodeResult machine_encode_aarch64(Arena* arena, MachineFunction* function, MachineStackPlacement* placement)
{
    MachineEncodeResult result = {0};
    u32 push_count = 0;
    for (u32 saved_register = 0; saved_register < MACHINE_A64_REGISTER_COUNT; saved_register += 1)
    {
        push_count += (placement->callee_saved_mask >> saved_register) & 1u;
    }
    // Stack layout: [sp .. sp+frame_area) holds the placement's slots with
    // the callee-saved save area at the top (the offsets the shared
    // placement reserved for pushes), [sp+frame_area] saves the caller's
    // x28, and eight padding bytes keep the total sixteen-aligned. The
    // placement frame plus the save area is sixteen-aligned already, so
    // the total is too. Outsized frames need multi-word prologue stores
    // the module wiring's cursor accounting does not model, so they stay
    // canonical.
    u32 frame_area = placement->frame_size + 8 * push_count;
    if (!placement->valid || frame_area > MACHINE_A64_MAX_FRAME_BYTES)
    {
        return result;
    }
    u32 frame_total = frame_area + 16;
    // Per-row worst-case byte budget: constants and remainders expand, the
    // epilogue carries the frame release, everything else is one word plus
    // slack for large-offset frame addressing.
    u32 frame_chunk_words = frame_total / 4080 + 1;
    u64 capacity64 = 64 + (u64)frame_chunk_words * 8 + (u64)push_count * 4;
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
            capacity64 += 20 + (u64)frame_chunk_words * 4 + (u64)push_count * 4;
            break;
        case MACHINE_A64_COPY_FRAME_FROM_FRAME:
        case MACHINE_A64_COPY_FRAME_FROM_PTR:
        case MACHINE_A64_COPY_PTR_FROM_FRAME:
            // One load/store word pair per eight-byte chunk, plus sized
            // tail accesses that may each take the large-offset form.
            capacity64 += ((u64)capacity_row->payload / 8) * 8 + 48;
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
    u32 save_slot = 0;
    for (u32 saved_register = 0; saved_register < MACHINE_A64_REGISTER_COUNT; saved_register += 1)
    {
        if (!((placement->callee_saved_mask >> saved_register) & 1u))
        {
            continue;
        }
        save_slot += 1;
        machine_a64_emit(&encoder, 0xf90003e0 | (((frame_area - 8 * save_slot) / 8) << 10) | (MACHINE_A64_SP << 5) | saved_register);
    }
    machine_a64_emit(&encoder, 0xf90003e0 | ((frame_area / 8) << 10) | (MACHINE_A64_SP << 5) | MACHINE_A64_X28);
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
                    machine_a64_emit_frame_store(&encoder, edit->location, machine_a64_frame_offset(frame_area, placement->virtual_register_offsets[edit->subject]));
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
                    machine_a64_emit_frame_load(&encoder, edit->location, machine_a64_frame_offset(frame_area, placement->virtual_register_offsets[edit->subject]));
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
                    machine_a64_frame_offset(frame_area, placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])] - instruction->payload));
                break;
            case MACHINE_A64_LOAD_FRAME32:
                machine_a64_emit_frame_memory(
                    &encoder, operand_registers[0],
                    machine_a64_frame_offset(frame_area, placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])] - instruction->payload),
                    4, false);
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
                    machine_a64_frame_offset(frame_area, placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])] - instruction->payload),
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
                    frame_area, placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])] - instruction->payload);
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
            case MACHINE_A64_COPY_FRAME_FROM_FRAME:
            case MACHINE_A64_COPY_FRAME_FROM_PTR:
            case MACHINE_A64_COPY_PTR_FROM_FRAME:
            {
                // Chunked copy through the X17 data scratch — X16 stays the
                // large-offset address scratch inside the frame accesses.
                // Chunks descend in size, so every access stays aligned.
                u32 destination_slot_offset = 0;
                u32 source_slot_offset = 0;
                u32 pointer_register = 0;
                if (instruction->opcode == MACHINE_A64_COPY_FRAME_FROM_FRAME)
                {
                    destination_slot_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])];
                    source_slot_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])];
                }
                else if (instruction->opcode == MACHINE_A64_COPY_FRAME_FROM_PTR)
                {
                    destination_slot_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])];
                    pointer_register = operand_registers[1];
                }
                else
                {
                    pointer_register = operand_registers[0];
                    source_slot_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])];
                }
                u32 copied = 0;
                u32 remaining = instruction->payload;
                while (remaining && !encoder.overflow && !encoder.error)
                {
                    u32 chunk = remaining >= 8 ? 8u : remaining >= 4 ? 4u : remaining >= 2 ? 2u : 1u;
                    if (instruction->opcode == MACHINE_A64_COPY_PTR_FROM_FRAME)
                    {
                        machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X17, machine_a64_frame_offset(frame_area, source_slot_offset - copied), chunk,
                                                      false);
                        machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X17, pointer_register, copied, chunk, true);
                    }
                    else if (instruction->opcode == MACHINE_A64_COPY_FRAME_FROM_PTR)
                    {
                        machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X17, pointer_register, copied, chunk, false);
                        machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X17, machine_a64_frame_offset(frame_area, destination_slot_offset - copied),
                                                      chunk, true);
                    }
                    else
                    {
                        machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X17, machine_a64_frame_offset(frame_area, source_slot_offset - copied), chunk,
                                                      false);
                        machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X17, machine_a64_frame_offset(frame_area, destination_slot_offset - copied),
                                                      chunk, true);
                    }
                    copied += chunk;
                    remaining -= chunk;
                }
            }
            break;
            case MACHINE_A64_FMOV_TO_VEC:
                machine_a64_emit(&encoder, 0x9e670000 | ((u32)operand_registers[0] << 5) | (instruction->payload & 0x1f));
                break;
            case MACHINE_A64_FMOV_FROM_VEC:
                machine_a64_emit(&encoder, 0x9e660000 | ((instruction->payload & 0x1f) << 5) | operand_registers[0]);
                break;
            case MACHINE_A64_B:
            {
                MachineA64BranchFixup* fixup = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                *fixup = (MachineA64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[0]),
                    .opcode = A64_OPCODE_B,
                };
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {{.kind = A64_MC_OPERAND_PC_RELATIVE}},
                                                   .opcode = A64_OPCODE_B,
                                                   .operand_count = 1,
                                               });
            }
            break;
            case MACHINE_A64_BCC:
            {
                MachineA64BranchFixup* taken = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                *taken = (MachineA64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[0]),
                    .opcode = A64_OPCODE_B_COND,
                };
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {
                                                       {.kind = A64_MC_OPERAND_PC_RELATIVE},
                                                       {.value = instruction->payload & 15, .kind = A64_MC_OPERAND_IMMEDIATE},
                                                   },
                                                   .opcode = A64_OPCODE_B_COND,
                                                   .operand_count = 2,
                                               });
                MachineA64BranchFixup* fallthrough = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                *fallthrough = (MachineA64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[1]),
                    .opcode = A64_OPCODE_B,
                };
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {{.kind = A64_MC_OPERAND_PC_RELATIVE}},
                                                   .opcode = A64_OPCODE_B,
                                                   .operand_count = 1,
                                               });
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
                u32 restore_slot = 0;
                for (u32 saved_register = 0; saved_register < MACHINE_A64_REGISTER_COUNT; saved_register += 1)
                {
                    if (!((placement->callee_saved_mask >> saved_register) & 1u))
                    {
                        continue;
                    }
                    restore_slot += 1;
                    machine_a64_emit(&encoder, 0xf94003e0 | (((frame_area - 8 * restore_slot) / 8) << 10) | (MACHINE_A64_SP << 5) | saved_register);
                }
                machine_a64_emit(&encoder, 0xf94003e0 | ((frame_area / 8) << 10) | (MACHINE_A64_SP << 5) | MACHINE_A64_X28);
                u32 release_remaining = frame_total;
                while (release_remaining)
                {
                    u32 release_chunk = BUSTER_MIN(release_remaining, 4080u);
                    machine_a64_emit(&encoder, 0x910003ff | (release_chunk << 10));
                    release_remaining -= release_chunk;
                }
                machine_a64_emit(&encoder, 0xa8c17bfd);
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {{.value = 30, .kind = A64_MC_OPERAND_REGISTER}},
                                                   .opcode = A64_OPCODE_RET,
                                                   .operand_count = 1,
                                               });
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
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {{.kind = A64_MC_OPERAND_PC_RELATIVE}},
                                                   .opcode = A64_OPCODE_BL,
                                                   .operand_count = 1,
                                               });
            }
            break;
            case MACHINE_A64_CALL_INDIRECT:
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {{.value = operand_registers[0], .kind = A64_MC_OPERAND_REGISTER}},
                                                   .opcode = A64_OPCODE_BLR,
                                                   .operand_count = 1,
                                               });
                break;
            case MACHINE_A64_LEA_SYMBOL:
            {
                // The canonical inline-literal form: load the eight-byte
                // literal two words ahead, branch over it, and let the
                // absolute relocation fill it.
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {
                                                       {.value = operand_registers[0], .kind = A64_MC_OPERAND_REGISTER},
                                                       {.value = 8, .kind = A64_MC_OPERAND_PC_RELATIVE},
                                                   },
                                                   .opcode = A64_OPCODE_LDR_LITERAL_64,
                                                   .operand_count = 2,
                                               });
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {{.value = 12, .kind = A64_MC_OPERAND_PC_RELATIVE}},
                                                   .opcode = A64_OPCODE_B,
                                                   .operand_count = 1,
                                               });
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
                    machine_a64_emit_frame_load(&encoder, edit->location, machine_a64_frame_offset(frame_area, placement->virtual_register_offsets[edit->subject]));
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
                    machine_a64_emit_frame_store(&encoder, edit->location, machine_a64_frame_offset(frame_area, placement->virtual_register_offsets[edit->subject]));
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
            if (fixup->block >= function->block_count || encoder.count < 4 || fixup->patch_offset > encoder.count - 4)
            {
                return result;
            }
            s64 displacement = (s64)(u64)result.block_offsets[fixup->block] - (s64)(u64)fixup->patch_offset;
            u32 word = 0;
            u32 patched = 0;
            memcpy(&word, encoder.bytes + fixup->patch_offset, sizeof(word));
            if (!a64_pc_relative_patch(fixup->opcode, word, displacement, &patched))
            {
                return result;
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
