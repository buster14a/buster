// x86-64 machine selection, MIR_STACK placement, and encoding. Included by
// machine.c in the backend-implementation-file pattern; not a standalone
// translation unit. The stage-2 subset covers scalar integer functions:
// arguments/constants/casts/unary/binary arithmetic and comparisons, direct
// locals and pointer dereference, branches, and scalar returns. Everything
// else is an explicit unsupported result, never a silent misselection.

#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

// Supported argument-list length: six register slots plus stack parts.
#define MACHINE_X64_MAX_ARGUMENTS 24

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
    // One 64-byte vector part in one ZMM register. The part is float-class
    // for placement — System V draws vectors and scalar floats from the same
    // SSE register sequence — and every consumer checks `vector` before the
    // scalar-float staging paths.
    bool vector;
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

// A conditional branch whose condition chain folded into the branch: the
// terminator re-selects the innermost comparison as CMP (or the truthiness
// test as TEST) immediately before JCC, and every absorbed chain member
// selects into nothing. Indexed by the branch's condition value; a zero
// condition nibble means no fusion (the mapping never produces 0).
typedef struct MachineX64BranchFusion MachineX64BranchFusion;
struct MachineX64BranchFusion
{
    u32 left;  // value the CMP/TEST reads
    u32 right; // CMP's second value, or UINT32_MAX for the TEST form
    u8 condition;
    u8 wide;
    u8 reserved[2];
};

typedef struct MachineX64Selector MachineX64Selector;
struct MachineX64Selector
{
    Arena* arena;
    IrProgram* program;
    IrFunction* function;
    MachineFunctionBuilder builder;
    MachineSelectionPrepass selection_prepass;
    MachineSelectionCounters selection_counters;
    MachineBuilderStream immediates;
    MachineBuilderStream stack_slots;
    MachineBuilderStream call_targets;
    MachineBuilderStream switch_cases;
    MachineBuilderStream stack_slot_alignments;
    MachineBuilderStream va_args;
    // Per IrValue: virtual register index, stack slot index, or UINT32_MAX.
    u32* value_virtual_registers;
    u32* value_stack_slots;
    // Per IrValue: the padded raw-storage slot of an over-aligned local, or
    // UINT32_MAX. Such a local mirrors the canonical frame layout: its
    // virtual register holds a runtime-aligned pointer into this slot, the
    // slot itself stays out of value_stack_slots so no consumer can read it
    // as data, and every access dispatches down the same pointer paths a
    // GLOBAL's address takes.
    u32* value_indirect_slots;
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
    // SysV variadic save area.  The row is emitted before incoming argument
    // captures, so the six GP and eight XMM registers are preserved before
    // any selector scratch can overwrite them.
    u32 va_register_save_slot;
    // Definition point per virtual register, patched into the flattened
    // rows because builder chunks are write-once.
    u32* virtual_register_definitions;
    // Per IrValue: the fusion a BRANCH_IF on that condition value selects,
    // and whether the value is a chain member that selects into nothing.
    MachineX64BranchFusion* branch_fusions;
    // i128 CMPXCHG16B materializes its ZF result immediately, so later
    // aggregate expected-value copies never rely on flags surviving IR rows.
    u32* atomic_success_registers;
    u8* fused_dead;
    u32 virtual_register_count;
    // The compile target, for the AVX-512 feature gates on the vector rows.
    Target target;
    IrOpcode failed_opcode;
    bool supported;
};

BUSTER_GLOBAL_LOCAL u8 const machine_x64_system_v_arguments[6] = {
    MACHINE_X64_RDI, MACHINE_X64_RSI, MACHINE_X64_RDX, MACHINE_X64_RCX, MACHINE_X64_R8, MACHINE_X64_R9,
};

BUSTER_CT_CHECK(MACHINE_X64_REGISTER_COUNT <= MACHINE_TARGET_REGISTER_LIMIT);

// The register file and special-opcode identities the shared allocators
// consume; RSP and RBP are reserved. The callee-saved members cost one
// push/pop pair per function that binds them, and their pushes carry
// unwind actions.
BUSTER_GLOBAL_LOCAL MachineTargetDescription const machine_x86_64_description = {
    .allocatable_mask = (1u << MACHINE_X64_RAX) | (1u << MACHINE_X64_RCX) | (1u << MACHINE_X64_RDX) | (1u << MACHINE_X64_RSI) | (1u << MACHINE_X64_RDI) |
                        (1u << MACHINE_X64_R8) | (1u << MACHINE_X64_R9) | (1u << MACHINE_X64_R10) | (1u << MACHINE_X64_R11) | (1u << MACHINE_X64_RBX) |
                        (1u << MACHINE_X64_R12) | (1u << MACHINE_X64_R13) | (1u << MACHINE_X64_R14) | (1u << MACHINE_X64_R15),
    .callee_saved_mask =
        (1u << MACHINE_X64_RBX) | (1u << MACHINE_X64_R12) | (1u << MACHINE_X64_R13) | (1u << MACHINE_X64_R14) | (1u << MACHINE_X64_R15),
    .register_count = MACHINE_X64_REGISTER_COUNT,
    .slot_scratch = {MACHINE_X64_RAX, MACHINE_X64_RCX, MACHINE_X64_RDX, MACHINE_X64_RSI},
    .copy_opcode = MACHINE_X64_MOV_RR,
    .constant_opcode = MACHINE_X64_MOV_RI,
    .indirect_call_opcode = MACHINE_X64_CALL_INDIRECT,
    .switch_opcode = MACHINE_X64_SWITCH,
    .float_bridge_opcode = MACHINE_X64_MOVQ_TO_XMM,
    .indirect_call_register = MACHINE_X64_R10,
    .float_bridge_register = MACHINE_X64_RAX,
    // The whole callee-saved file, highest register first so the pins
    // collide last with the local scan's own callee-saved bindings, which
    // probe in register order from RBX up.
    .quality_pin_registers = {MACHINE_X64_R15, MACHINE_X64_R14, MACHINE_X64_R13, MACHINE_X64_R12, MACHINE_X64_RBX},
    .quality_pin_register_count = 5,
    // ZMM0-31 at unified indices 16-47; System V keeps every one
    // caller-saved, so the call flush spills the whole vector file. The
    // full file rides in the static description because it is reachable
    // only through vector virtual registers, which the selector produces
    // only under the AVX512F gate — the same gate that makes ZMM16-31
    // architectural.
    .vector_allocatable_mask = 0xffffffffull << MACHINE_X64_ZMM0,
    .vector_copy_opcode = MACHINE_X64_VMOV_RR,
    .vector_slot_scratch = {MACHINE_X64_ZMM0, MACHINE_X64_ZMM1, MACHINE_X64_ZMM2, MACHINE_X64_ZMM3},
};

BUSTER_F_DECL MachineTargetDescription const* machine_target_x86_64(void)
{
    return &machine_x86_64_description;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_type_is_scalar_register(IrType* type)
{
    if (!type || !type->layout.resolved || type->layout.size > 8)
    {
        return false;
    }
    return type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_ENUM;
}

// The vector subset is the target-fixed 512-bit vocabulary: 64-byte vector
// values travel in ZMM-class virtual registers. Narrower vector types stay
// outside the subset and keep the per-function canonical fallback.
BUSTER_GLOBAL_LOCAL bool machine_x64_type_is_vector_register(IrType* type)
{
    return type && type->layout.resolved && type->kind == IR_TYPE_VECTOR && type->layout.size == 64;
}

// Mirrors codegen_canonical_x64_simd_supported: F and BW carry the 512-bit
// byte lanes and mask compares, VBMI vpermt2b, VBMI2 the compress family.
BUSTER_GLOBAL_LOCAL bool machine_x64_simd_supported(Target target, IrSimdOperation operation)
{
    if (!target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512F) || !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512BW))
    {
        return false;
    }
    if (operation == IR_SIMD_PERMUTE2_BYTE)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512VBMI);
    }
    if (operation == IR_SIMD_COMPRESS_BYTE || operation == IR_SIMD_COMPRESS_STORE_BYTE)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512VBMI2);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_value_shape(IrProgram* program, IrTypeId type_id, IrAbiUse use, Target target, MachineX64ValueShape* shape)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (machine_x64_type_is_vector_register(type))
    {
        // The vector ABI needs the 512-bit vocabulary's register moves, so a
        // target without the features keeps the canonical fallback, whose
        // model-dependent register split this subset does not reproduce.
        if (!machine_x64_simd_supported(target, IR_SIMD_SPLAT_BYTE))
        {
            return false;
        }
        IrAbiValue vector_abi = ir_type_abi_value(program, type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, use);
        if (vector_abi.indirect || vector_abi.memory || vector_abi.part_count != 1 || vector_abi.parts[0].abi_class != IR_ABI_CLASS_VECTOR ||
            vector_abi.parts[0].size != 64)
        {
            return false;
        }
        *shape = (MachineX64ValueShape){
            .part_is_float = {1},
            .part_count = 1,
            .byte_size = 64,
            .vector = true,
        };
        return true;
    }
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
    // A 128-bit integer is the System V two-eightbyte INTEGER pair — RAX:RDX
    // as a result, two consecutive GPRs or whole-value stack eightbytes as an
    // argument — the same parts the canonical integer-aggregate rule builds,
    // carried by the aggregate machinery over the value's 16-byte slot.
    if (type && type->layout.resolved && type->kind == IR_TYPE_INTEGER && type->bit_width == 128)
    {
        *shape = (MachineX64ValueShape){
            .part_offsets = {0, 8},
            .part_count = 2,
            .byte_size = 16,
            .aggregate = true,
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
        // A 64-byte vector's stack home starts at a 64-aligned offset in
        // the argument area — the canonical layout pads the cursor with
        // eightbytes the callee never reads, and both sides must count them.
        if (shape->vector)
        {
            *stack_part_count = (*stack_part_count + 7u) & ~7u;
        }
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

// The integer ALU encodings are physically two-address, but their selected
// rows carry an explicit SSA destination, first source, and second source.
// Fixed-register shifts and divide/remainder remain on their legacy
// constrained shape until their slot constraints are fully target-metadata
// driven.
BUSTER_GLOBAL_LOCAL bool machine_x64_opcode_is_ssa_two_address(u16 opcode)
{
    switch (opcode)
    {
        break;
    case MACHINE_X64_ADD32:
    case MACHINE_X64_ADD64:
    case MACHINE_X64_SUB32:
    case MACHINE_X64_SUB64:
    case MACHINE_X64_AND32:
    case MACHINE_X64_AND64:
    case MACHINE_X64_OR32:
    case MACHINE_X64_OR64:
    case MACHINE_X64_XOR32:
    case MACHINE_X64_XOR64:
    case MACHINE_X64_IMUL32:
    case MACHINE_X64_IMUL64:
        return true;
    default:
        return false;
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

// The x86 condition nibble for an integer/pointer/boolean comparison, or 0
// (never a comparison's nibble) for everything else. The nibbles pair as
// exact complements, so negation is condition ^ 1.
BUSTER_GLOBAL_LOCAL u32 machine_x64_condition_from_comparison(IrBinaryOperation operation)
{
    switch (operation)
    {
        break;
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_POINTER_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL:
        return MACHINE_X64_CONDITION_EQUAL;
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_POINTER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL:
        return MACHINE_X64_CONDITION_NOT_EQUAL;
    case IR_BINARY_SIGNED_LESS:
        return MACHINE_X64_CONDITION_LESS;
    case IR_BINARY_SIGNED_LESS_EQUAL:
        return MACHINE_X64_CONDITION_LESS_EQUAL;
    case IR_BINARY_SIGNED_GREATER:
        return MACHINE_X64_CONDITION_GREATER;
    case IR_BINARY_SIGNED_GREATER_EQUAL:
        return MACHINE_X64_CONDITION_GREATER_EQUAL;
    case IR_BINARY_UNSIGNED_LESS:
        return MACHINE_X64_CONDITION_BELOW;
    case IR_BINARY_UNSIGNED_LESS_EQUAL:
        return MACHINE_X64_CONDITION_BELOW_EQUAL;
    case IR_BINARY_UNSIGNED_GREATER:
        return MACHINE_X64_CONDITION_ABOVE;
    case IR_BINARY_UNSIGNED_GREATER_EQUAL:
        return MACHINE_X64_CONDITION_ABOVE_EQUAL;
    default:
        return 0;
    }
}

BUSTER_GLOBAL_LOCAL u32 machine_x64_append_slot(MachineX64Selector* selector, u32 size, u32 alignment)
{
    u32 slot_index = selector->stack_slots.total_count;
    u32* slot_size = (u32*)machine_stream_append(selector->arena, &selector->stack_slots);
    *slot_size = size;
    u32* slot_alignment = (u32*)machine_stream_append(selector->arena, &selector->stack_slot_alignments);
    *slot_alignment = alignment;
    return slot_index;
}

// The SysV va_list register-save offsets are measured in bytes from the
// save-area pointer: six eightbyte GP slots followed by eight sixteen-byte
// XMM slots.  Return the fixed named-parameter cursors that VA_START seeds.
BUSTER_GLOBAL_LOCAL void machine_x64_va_named_cursors(MachineX64Selector* selector, u32* integer_count, u32* float_count, u32* stack_end)
{
    *integer_count = selector->return_shape.indirect ? 1u : 0u;
    *float_count = 0;
    *stack_end = 0;
    IrType* function_type = ir_type_from_id(&selector->program->types, selector->function->canonical_type);
    if (!function_type || function_type->kind != IR_TYPE_FUNCTION)
    {
        return;
    }
    for (u32 argument = 0; argument < function_type->parameter_count && argument < MACHINE_X64_MAX_ARGUMENTS; argument += 1)
    {
        MachineX64ValueShape* shape = selector->parameter_shapes + argument;
        MachineX64ArgumentPlacement* placement = selector->parameter_placements + argument;
        if (placement->on_stack)
        {
            *stack_end = BUSTER_MAX(*stack_end, ((u32)placement->first_stack_part + placement->stack_part_count) * 8u);
        }
        else
        {
            *integer_count = BUSTER_MAX(*integer_count, (u32)placement->first_integer + machine_x64_shape_class_parts(shape, false));
            *float_count = BUSTER_MAX(*float_count, (u32)placement->first_float + machine_x64_shape_class_parts(shape, true));
        }
    }
}

BUSTER_GLOBAL_LOCAL u32 machine_x64_va_append_immediate(MachineX64Selector* selector, u64 value)
{
    u32 index = selector->immediates.total_count;
    u64* row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
    *row = value;
    return index;
}

// Translate the IR-owned SysV variadic ABI classification into the compact
// side data consumed by MACHINE_X64_VA_ARG.  This deliberately handles only
// the scalar/two-eightbyte classes the machine subset can materialize; larger
// memory values still use one bounded overflow copy.
BUSTER_GLOBAL_LOCAL bool machine_x64_va_arg_metadata(MachineX64Selector* selector, IrType* type, u32 result_slot, bool result_is_frame,
                                                     MachineVaArg* metadata)
{
    if (!type || !type->layout.resolved || !type->layout.size || type->layout.size > UINT32_MAX || type->layout.alignment > 16)
    {
        return false;
    }
    IrTypeId type_id = {.value = (u32)(type - selector->program->types.types)};
    IrAbiValue abi = ir_type_abi_value(selector->program, type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_VARIADIC_ARGUMENT);
    if (!abi.part_count || abi.part_count > MACHINE_VA_ARG_PART_LIMIT)
    {
        return false;
    }
    MachineVaArg value = {
        .size = (u32)type->layout.size,
        .alignment = BUSTER_MAX(type->layout.alignment, 8u),
        .stack_size = ((u32)type->layout.size + 7u) & ~7u,
        .part_count = abi.part_count,
        .result_slot = result_slot,
        .result_is_frame = result_is_frame,
        .scalar_size = (u8)BUSTER_MIN(type->layout.size, 8u),
    };
    u32 gp_index = 0;
    u32 fp_index = 0;
    for (u32 part_index = 0; part_index < abi.part_count; part_index += 1)
    {
        IrAbiPart* part = abi.parts + part_index;
        MachineVaArgPart* output = value.parts + part_index;
        output->value_offset = part->value_offset;
        output->size = (u8)BUSTER_MIN(part->size, 8u);
        output->is_memory = part->abi_class == IR_ABI_CLASS_MEMORY;
        if (part->abi_class == IR_ABI_CLASS_FLOAT)
        {
            output->is_float = 1;
            // The va_list fp_offset already includes the 48-byte GP save
            // area.  Unlike the fixed-parameter placement index, each
            // variadic part adds this relative offset exactly once to the
            // live fp cursor.
            output->save_offset = fp_index * 16u;
            fp_index += 1;
        }
        else if (part->abi_class == IR_ABI_CLASS_INTEGER || part->abi_class == IR_ABI_CLASS_POINTER)
        {
            output->save_offset = 0;
            output->save_offset = gp_index * 8u;
            gp_index += 1;
        }
        else if (!output->is_memory)
        {
            return false;
        }
    }
    *metadata = value;
    return true;
}

// Mirrors the canonical emitter's indirect-place test: an over-aligned
// local's virtual register holds a runtime-aligned pointer, so the local
// reads and writes exactly like a GLOBAL's address, never like a direct
// frame slot and never like a promoted register value.
BUSTER_GLOBAL_LOCAL bool machine_x64_local_is_indirect(MachineX64Selector* selector, IrValueId value)
{
    return value.value < selector->function->value_count && selector->value_indirect_slots[value.value] != UINT32_MAX;
}

// Emits the row computing the address (or pointer value) of `base` into
// `destination_register` and defines it: a direct local's frame-slot
// address, or a copy of any vreg-held value — mirroring the canonical
// path's lea-or-slot-load base handling. Returns false outside the subset.
// Places the address of `base` plus a constant byte offset in one row:
// a direct local folds the offset into its frame displacement, and a
// pointer folds it into a lea. Only a zero offset on a pointer stays a
// plain copy, which the allocator can then coalesce away.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_place_address_offset(MachineX64Selector* selector, IrValueId base, u32 destination_register, u32 byte_offset)
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
    if (definition->opcode == IR_OPCODE_LOCAL && selector->value_virtual_registers[base.value] != UINT32_MAX &&
        !machine_x64_local_is_indirect(selector, base))
    {
        // A promoted local has no address. The promotability scan proved
        // no use needs one, so a request here is a selector hole — refuse
        // to the canonical fallback rather than hand a register's value
        // out as an address. An over-aligned local's register is the
        // aligned pointer itself and falls through to the pointer path.
        return false;
    }
    // An array or vector *value* is its storage, exactly like the
    // canonical INDEX base rule: its slot address is the base address.
    // Slices and struct values stay on the loaded-pointer path below,
    // matching the canonical emitter's per-opcode base handling.
    IrType* value_type = ir_type_from_id(&selector->program->types, value->canonical_type);
    bool storage_value = definition->opcode == IR_OPCODE_LOCAL ||
                         (value->category == IR_VALUE_VALUE && value_type &&
                          (value_type->kind == IR_TYPE_ARRAY || value_type->kind == IR_TYPE_VECTOR));
    if (storage_value && slot != UINT32_MAX)
    {
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, destination_register),
                                                                    machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                       .payload = byte_offset,
                                                       .opcode = MACHINE_X64_LEA_FRAME,
                                                   });
        machine_x64_define(selector, destination_register, row);
        return true;
    }
    // A vreg-held value is copied as a pointer below, which only means
    // anything for a general-class register. A 512-bit rvalue's register is
    // not an address — an INDEX into one, reachable now that vector-ABI
    // calls and signatures select, snapshots the immutable SSA value into a
    // dedicated 64-byte slot and hands out that slot's address instead.
    if (machine_x64_type_is_vector_register(ir_type_from_id(&selector->program->types, value->canonical_type)))
    {
        u32 vector_register;
        if (!machine_x64_operand_register(selector, base, &vector_register))
        {
            return false;
        }
        u32 snapshot_slot = machine_x64_append_slot(selector, 64, 16);
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, snapshot_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, vector_register)},
                                             .opcode = MACHINE_X64_VSTORE_FRAME,
                                         });
        u32 snapshot_row = machine_x64_select_row(selector, (MachineInstruction){
                                                                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, destination_register),
                                                                             machine_ref_make(MACHINE_REF_STACK_SLOT, snapshot_slot)},
                                                                .payload = byte_offset,
                                                                .opcode = MACHINE_X64_LEA_FRAME,
                                                            });
        machine_x64_define(selector, destination_register, snapshot_row);
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
                                                   .payload = byte_offset,
                                                   .opcode = (u16)(byte_offset ? MACHINE_X64_LEA_OFFSET : MACHINE_X64_MOV_RR),
                                               });
    machine_x64_define(selector, destination_register, row);
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_place_address(MachineX64Selector* selector, IrValueId base, u32 destination_register)
{
    return machine_x64_select_place_address_offset(selector, base, destination_register, 0);
}

// A selection-synthesized temporary vreg, defined at the next row to be
// emitted so no post-pass definition patching is needed.
BUSTER_GLOBAL_LOCAL u32 machine_x64_synthesize_register(MachineX64Selector* selector)
{
    return machine_builder_virtual_register(&selector->builder, (MachineVirtualRegister){
                                                                    .definition_point =
                                                                        machine_point_make(selector->builder.instructions.total_count, MACHINE_POINT_AFTER),
                                                                    .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                    .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                                });
}

BUSTER_GLOBAL_LOCAL u32 machine_x64_select_immediate_register(MachineX64Selector* selector, u64 value)
{
    u32 result = machine_x64_synthesize_register(selector);
    u32 immediate = selector->immediates.total_count;
    u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
    *immediate_row = value;
    machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, immediate)},
                                             .opcode = MACHINE_X64_MOV_RI,
                                         });
    return result;
}

BUSTER_GLOBAL_LOCAL u32 machine_x64_select_frame_load64(MachineX64Selector* selector, u32 slot, u32 payload)
{
    u32 result = machine_x64_synthesize_register(selector);
    machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result),
                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                             .payload = payload,
                                             .opcode = MACHINE_X64_LOAD_FRAME,
                                         });
    return result;
}

BUSTER_GLOBAL_LOCAL void machine_x64_select_frame_store64(MachineX64Selector* selector, u32 slot, u32 payload, u32 value)
{
    machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value)},
                                             .payload = payload,
                                             .opcode = MACHINE_X64_STORE_FRAME64,
                                         });
}

BUSTER_GLOBAL_LOCAL bool machine_x64_constant_shift_amount(MachineX64Selector* selector, IrValueId value, u32* amount_out)
{
    IrFunction* function = selector->function;
    // The frontend normally leaves a shift count as one integer constant,
    // optionally wrapped in a short cast chain while applying the usual
    // arithmetic conversions.  Keep this walk explicitly bounded: an
    // immediate machine lowering must never turn arbitrary IR into a
    // compile-time search.
    for (u32 depth = 0; depth < 4; depth += 1)
    {
        if (value.value >= function->value_count)
        {
            return false;
        }
        IrValue* ir_value = function->values + value.value;
        if (ir_value->definition.value >= function->instruction_count)
        {
            return false;
        }
        IrInstruction* definition = function->instructions + ir_value->definition.value;
        if (definition->opcode == IR_OPCODE_CONSTANT_INTEGER)
        {
            if (!definition->immediate_count || !definition->immediates || definition->immediate_is_negative || definition->immediates[0] > 127)
            {
                return false;
            }
            *amount_out = (u32)definition->immediates[0];
            return true;
        }
        if (definition->opcode != IR_OPCODE_CAST || definition->operand_count < 1 || !definition->operands)
        {
            return false;
        }
        if (definition->operands[0].value >= function->value_count)
        {
            return false;
        }
        IrType* source_type = ir_type_from_id(&selector->program->types, function->values[definition->operands[0].value].canonical_type);
        IrType* target_type = ir_type_from_id(&selector->program->types, definition->canonical_type);
        if (!source_type || !target_type || source_type->kind != IR_TYPE_INTEGER ||
            target_type->kind != IR_TYPE_INTEGER || target_type->bit_width < 8)
        {
            return false;
        }
        bool cast_preserves_small_nonnegative = definition->conversion_operation == IR_CONVERSION_IDENTITY ||
                                                definition->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET ||
                                                definition->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND ||
                                                definition->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                                                (definition->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE && target_type->bit_width >= 8);
        if (!cast_preserves_small_nonnegative)
        {
            return false;
        }
        value = definition->operands[0];
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_i128_unsigned_shift_right(MachineX64Selector* selector, IrInstruction* instruction, u32 amount)
{
    IrFunction* function = selector->function;
    if (instruction->operand_count < 2 || instruction->result.value >= function->value_count || instruction->operands[0].value >= function->value_count || amount > 127)
    {
        return false;
    }
    u32 source_slot = selector->value_stack_slots[instruction->operands[0].value];
    u32 result_slot = selector->value_stack_slots[instruction->result.value];
    if (source_slot == UINT32_MAX || result_slot == UINT32_MAX)
    {
        return false;
    }
    if (amount == 0)
    {
        machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, source_slot)},
                                                 .payload = 16,
                                                 .opcode = MACHINE_X64_COPY_FRAME_FROM_FRAME,
                                             });
        return true;
    }
    if (amount < 64)
    {
        u32 low = machine_x64_select_frame_load64(selector, source_slot, 0);
        u32 high = machine_x64_select_frame_load64(selector, source_slot, 8);
        u32 cross = machine_x64_synthesize_register(selector);
        machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, cross),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high)},
                                                 .opcode = MACHINE_X64_MOV_RR,
                                             });
        u32 count = machine_x64_select_immediate_register(selector, amount);
        machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, count)},
                                                 .opcode = MACHINE_X64_SHR64,
                                             });
        machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, count)},
                                                 .opcode = MACHINE_X64_SHR64,
                                             });
        u32 cross_count = machine_x64_select_immediate_register(selector, 64 - amount);
        machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, cross),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, cross_count)},
                                                 .opcode = MACHINE_X64_SHL64,
                                             });
        machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, cross)},
                                                 .opcode = MACHINE_X64_OR64,
                                             });
        machine_x64_select_frame_store64(selector, result_slot, 0, low);
        machine_x64_select_frame_store64(selector, result_slot, 8, high);
        return true;
    }
    u32 low = machine_x64_select_frame_load64(selector, source_slot, 8);
    if (amount > 64)
    {
        u32 count = machine_x64_select_immediate_register(selector, amount - 64);
        machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, count)},
                                                 .opcode = MACHINE_X64_SHR64,
                                             });
    }
    machine_x64_select_frame_store64(selector, result_slot, 0, low);
    u32 zero = machine_x64_select_immediate_register(selector, 0);
    machine_x64_select_frame_store64(selector, result_slot, 8, zero);
    return true;
}

// Emits result-vreg definition rows for one typed instruction. Returns false
// when the construct falls outside the selected subset.
// One member of an aggregate or array literal lands in the value's slot
// at `member_offset`: scalar members store sized, slot-backed members
// copy through the member's address.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_member_write(MachineX64Selector* selector, u32 slot, u64 member_offset, u64 member_size, IrValueId operand)
{
    u32 value_register = selector->value_virtual_registers[operand.value];
    u32 value_slot = selector->value_stack_slots[operand.value];
    if (value_register != UINT32_MAX || value_slot == UINT32_MAX)
    {
        u16 store_opcode = member_size == 1   ? MACHINE_X64_STORE_FRAME8
                           : member_size == 2 ? MACHINE_X64_STORE_FRAME16
                           : member_size == 4 ? MACHINE_X64_STORE_FRAME32
                           : member_size == 8 ? MACHINE_X64_STORE_FRAME64
                                              : 0;
        if (!store_opcode || !machine_x64_operand_register(selector, operand, &value_register))
        {
            return false;
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                             .payload = (u32)member_offset,
                                             .opcode = store_opcode,
                                         });
        return true;
    }
    u32 address_register = machine_x64_synthesize_register(selector);
    machine_x64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                         .opcode = MACHINE_X64_LEA_FRAME,
                                     });
    if (member_offset)
    {
        u32 offset_register = machine_x64_synthesize_register(selector);
        u32 immediate_index = selector->immediates.total_count;
        u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
        *immediate_row = member_offset;
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, offset_register),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                             .opcode = MACHINE_X64_MOV_RI,
                                         });
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, offset_register)},
                                             .opcode = MACHINE_X64_ADD64,
                                         });
    }
    machine_x64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                         .payload = (u32)member_size,
                                         .opcode = MACHINE_X64_COPY_PTR_FROM_FRAME,
                                     });
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_instruction(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;
    u32 instruction_index = (u32)(instruction - function->instructions);
    MachineSelectionRuleContext selection_context =
        machine_selection_rule_context(&selector->selection_prepass, (IrInstructionId){.value = instruction_index}, selector->target);
    MachineSelectionDecision selection_decision =
        machine_selection_rule_select(selection_context, MACHINE_SELECTION_MODE_FAST, &selector->selection_counters);
    BUSTER_CHECK(selection_decision.selected.rule != MACHINE_SELECTION_RULE_INVALID);
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
    case IR_OPCODE_VA_ARG:
    {
        if (instruction->operand_count < 1 || instruction->result.value >= function->value_count)
        {
            return false;
        }
        u32 source_register;
        if (!machine_x64_operand_register(selector, instruction->operands[0], &source_register))
        {
            return false;
        }
        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u32 result_slot = selector->value_stack_slots[instruction->result.value];
        bool result_is_frame = result_register == UINT32_MAX && result_slot != UINT32_MAX;
        bool scalar = machine_x64_type_is_scalar_register(value_type) ||
                      (value_type && value_type->kind == IR_TYPE_FLOAT && (value_type->bit_width == 32 || value_type->bit_width == 64));
        bool aggregate = value_type && value_type->layout.resolved &&
                         (value_type->kind == IR_TYPE_STRUCT || value_type->kind == IR_TYPE_UNION || value_type->kind == IR_TYPE_SLICE ||
                          value_type->kind == IR_TYPE_ARRAY || (value_type->kind == IR_TYPE_INTEGER && value_type->bit_width == 128));
        if ((!scalar && !aggregate) || (result_is_frame != aggregate))
        {
            return false;
        }
        MachineVaArg metadata;
        if (!machine_x64_va_arg_metadata(selector, value_type, result_slot, result_is_frame, &metadata))
        {
            return false;
        }
        u32 metadata_index = selector->va_args.total_count;
        MachineVaArg* metadata_row = (MachineVaArg*)machine_stream_append(selector->arena, &selector->va_args);
        *metadata_row = metadata;
        MachineInstruction row = {
            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register),
                         result_is_frame ? machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot)
                                          : machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
            .payload = metadata_index,
            .opcode = MACHINE_X64_VA_ARG,
        };
        u32 row_index = machine_x64_select_row(selector, row);
        if (!result_is_frame)
        {
            machine_x64_define(selector, result_register, row_index);
        }
        return true;
    }
    break;
    case IR_OPCODE_VA_START:
    {
        u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
        if (result_slot == UINT32_MAX || selector->va_register_save_slot == UINT32_MAX)
        {
            return false;
        }
        u32 integer_count;
        u32 float_count;
        u32 stack_end;
        machine_x64_va_named_cursors(selector, &integer_count, &float_count, &stack_end);
        u64 offsets = (u64)(integer_count * 8u) | ((u64)(48u + float_count * 16u) << 32);
        u32 packed_register = machine_x64_synthesize_register(selector);
        u32 packed_immediate = machine_x64_va_append_immediate(selector, offsets);
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, packed_register),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, packed_immediate)},
                                             .opcode = MACHINE_X64_MOV_RI,
                                         });
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, packed_register)},
                                             .payload = 0,
                                             .opcode = MACHINE_X64_STORE_FRAME64,
                                         });
        u32 overflow_register = machine_x64_synthesize_register(selector);
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, overflow_register),
                                                          machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RBP)},
                                             .payload = 16u + stack_end,
                                             .opcode = MACHINE_X64_LEA_OFFSET,
                                         });
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, overflow_register)},
                                             .payload = 8,
                                             .opcode = MACHINE_X64_STORE_FRAME64,
                                         });
        u32 save_register = machine_x64_synthesize_register(selector);
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, save_register),
                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, selector->va_register_save_slot)},
                                             .opcode = MACHINE_X64_LEA_FRAME,
                                         });
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, save_register)},
                                             .payload = 16,
                                             .opcode = MACHINE_X64_STORE_FRAME64,
                                         });
        u32 zero_register = machine_x64_synthesize_register(selector);
        u32 zero_immediate = machine_x64_va_append_immediate(selector, 0);
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, zero_register),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, zero_immediate)},
                                             .opcode = MACHINE_X64_MOV_RI,
                                         });
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, zero_register)},
                                             .payload = 24,
                                             .opcode = MACHINE_X64_STORE_FRAME64,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_VA_COPY:
    {
        u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
        u32 source_register;
        if (result_slot == UINT32_MAX || instruction->operand_count < 1 || !machine_x64_operand_register(selector, instruction->operands[0], &source_register))
        {
            return false;
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                             .payload = 32,
                                             .opcode = MACHINE_X64_COPY_FRAME_FROM_PTR,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_VA_END:
    {
        u32 source_register;
        if (instruction->operand_count < 1 || !machine_x64_operand_register(selector, instruction->operands[0], &source_register))
        {
            return false;
        }
        u32 value_register = machine_x64_synthesize_register(selector);
        u32 value_immediate = machine_x64_va_append_immediate(selector, 1);
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, value_immediate)},
                                             .opcode = MACHINE_X64_MOV_RI,
                                         });
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                             .payload = 24,
                                             .opcode = MACHINE_X64_STORE_PTR64,
                                         });
        return true;
    }
    break;
    case IR_OPCODE_LOCAL:
    {
        // Direct locals produce no code: the stack slot recorded during
        // classification is the storage, exactly like the canonical path —
        // or, promoted, the virtual register is. An over-aligned local
        // computes its runtime-aligned pointer here, the same lea/add/and
        // the canonical emitter runs at its LOCAL instruction.
        u32 indirect_slot = selector->value_indirect_slots[instruction->result.value];
        if (indirect_slot != UINT32_MAX)
        {
            u32 pointer_register = selector->value_virtual_registers[instruction->result.value];
            IrValue* local_value = function->values + instruction->result.value;
            IrType* local_type = ir_type_from_id(&program->types, local_value->canonical_type);
            u32 local_alignment = BUSTER_MAX(BUSTER_MAX(local_value->alignment, local_type ? local_type->layout.alignment : 0), 8u);
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                        machine_ref_make(MACHINE_REF_STACK_SLOT, indirect_slot)},
                                                           .payload = local_alignment - 1,
                                                           .opcode = MACHINE_X64_LEA_FRAME,
                                                       });
            machine_x64_define(selector, pointer_register, row);
            u32 mask_register = machine_x64_synthesize_register(selector);
            u32 immediate_index = selector->immediates.total_count;
            u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
            *immediate_row = 0 - (u64)local_alignment;
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register),
                                                              machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                 .opcode = MACHINE_X64_MOV_RI,
                                             });
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register)},
                                                 .opcode = MACHINE_X64_AND64,
                                             });
            return true;
        }
        return selector->value_stack_slots[instruction->result.value] != UINT32_MAX ||
               selector->value_virtual_registers[instruction->result.value] != UINT32_MAX;
    }
    break;
    case IR_OPCODE_STACK_SAVE:
    {
        // RSP is a physical operand, so the scheduler and allocators keep
        // this checkpoint ordered with every stack-affecting row. The value
        // remains an ordinary virtual register and is spilled around calls
        // when its lifetime crosses one.
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
    case IR_OPCODE_STACK_ALLOCATE:
    {
        if (result_register == UINT32_MAX || !instruction->immediate_count || !instruction->immediates)
        {
            return false;
        }
        u64 requested_alignment = instruction->immediates[0];
        if (requested_alignment > UINT32_MAX)
        {
            return false;
        }
        u32 stack_alignment = BUSTER_MAX((u32)requested_alignment, 16u);
        if (!stack_alignment || (stack_alignment & (stack_alignment - 1u)) != 0)
        {
            return false;
        }
        u32 size_register;
        if (!machine_x64_operand_register(selector, instruction->operands[0], &size_register))
        {
            return false;
        }
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, size_register)},
                                                       .payload = stack_alignment,
                                                       .opcode = MACHINE_X64_STACK_ALLOCATE,
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
        if (instruction->operands[0].value >= function->value_count)
        {
            return false;
        }
        IrType* source_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        IrType* cast_target_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u32 source_bits = machine_x64_scalar_bit_width(source_type);
        bool source_integer128 = source_type && source_type->kind == IR_TYPE_INTEGER && source_type->bit_width == 128;
        bool target_integer128 = cast_target_type && cast_target_type->kind == IR_TYPE_INTEGER && cast_target_type->bit_width == 128;
        // i128 values use the same two-eightbyte frame representation as
        // aggregate values.  Keep this path direct and bounded: scalar
        // integer extensions/truncations use the existing frame rows, while
        // a 128-bit reinterpret is a byte-preserving frame copy. Other
        // aggregate casts continue through the explicit unsupported fallback.
        if (source_integer128 || target_integer128)
        {
            if (!source_type || !cast_target_type || source_type->kind != IR_TYPE_INTEGER || cast_target_type->kind != IR_TYPE_INTEGER)
            {
                return false;
            }
            u32 source_slot = selector->value_stack_slots[instruction->operands[0].value];
            u32 target_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
            bool reinterpret_i128 = source_integer128 && target_integer128 &&
                                   (instruction->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET ||
                                    instruction->conversion_operation == IR_CONVERSION_IDENTITY);
            bool truncate_i128 = source_integer128 && !target_integer128 && cast_target_type->bit_width <= 64 &&
                                 instruction->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE;
            bool extend_i128 = target_integer128 && !source_integer128 && source_type->bit_width >= 8 && source_type->bit_width <= 64 &&
                               (instruction->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND ||
                                instruction->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND);
            if (reinterpret_i128)
            {
                if (source_slot == UINT32_MAX || target_slot == UINT32_MAX)
                {
                    return false;
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, target_slot),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, source_slot)},
                                                     .payload = 16,
                                                     .opcode = MACHINE_X64_COPY_FRAME_FROM_FRAME,
                                                 });
                return true;
            }
            if (truncate_i128)
            {
                if (result_register == UINT32_MAX || source_slot == UINT32_MAX)
                {
                    return false;
                }
                u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_STACK_SLOT, source_slot)},
                                                               .opcode = MACHINE_X64_LOAD_FRAME,
                                                           });
                machine_x64_define(selector, result_register, row);
                if (cast_target_type->bit_width < 64)
                {
                    u16 narrow_opcode = cast_target_type->bit_width == 8   ? MACHINE_X64_MOVZX8_RR
                                        : cast_target_type->bit_width == 16 ? MACHINE_X64_MOVZX16_RR
                                                                             : MACHINE_X64_MOV32_RR;
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                         .opcode = narrow_opcode,
                                                     });
                }
                return true;
            }
            if (extend_i128)
            {
                u32 source_register;
                if (result_register != UINT32_MAX || target_slot == UINT32_MAX || source_bits < 8 || source_bits > 64 ||
                    !machine_x64_operand_register(selector, instruction->operands[0], &source_register))
                {
                    return false;
                }
                u32 low_register = source_register;
                if (source_bits < 64)
                {
                    u16 extend_opcode = instruction->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND
                                            ? (source_bits == 8 ? MACHINE_X64_MOVSX8_RR
                                               : source_bits == 16 ? MACHINE_X64_MOVSX16_RR
                                                                   : MACHINE_X64_MOVSX32_RR)
                                            : (source_bits == 8 ? MACHINE_X64_MOVZX8_RR
                                               : source_bits == 16 ? MACHINE_X64_MOVZX16_RR
                                                                   : MACHINE_X64_MOV32_RR);
                    low_register = machine_x64_synthesize_register(selector);
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                         .opcode = extend_opcode,
                                                     });
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, target_slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low_register)},
                                                     .opcode = MACHINE_X64_STORE_FRAME64,
                                                 });
                u32 high_register = machine_x64_synthesize_register(selector);
                if (instruction->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND)
                {
                    // The high half of a signed 64-to-128 extension is the
                    // sign bit replicated across all 64 bits.  Copy the low
                    // half, then arithmetic-shift it by 63 so the runtime
                    // sign (rather than the static type) chooses all ones or
                    // all zeroes.
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low_register)},
                                                         .opcode = MACHINE_X64_MOV_RR,
                                                     });
                    u32 shift_register = machine_x64_synthesize_register(selector);
                    u32 immediate_index = selector->immediates.total_count;
                    u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
                    *immediate_row = 63;
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, shift_register),
                                                                      machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                         .opcode = MACHINE_X64_MOV_RI,
                                                     });
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, shift_register)},
                                                         .opcode = MACHINE_X64_SAR64,
                                                     });
                }
                else
                {
                    u32 immediate_index = selector->immediates.total_count;
                    u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
                    *immediate_row = 0;
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high_register),
                                                                      machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                         .opcode = MACHINE_X64_MOV_RI,
                                                     });
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, target_slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high_register)},
                                                     .payload = 8,
                                                     .opcode = MACHINE_X64_STORE_FRAME64,
                                                 });
                return true;
            }
            return false;
        }
        u32 source_register;
        if (result_register == UINT32_MAX || !machine_x64_operand_register(selector, instruction->operands[0], &source_register))
        {
            return false;
        }
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
                (cast_target_type->bit_width != 32 && cast_target_type->bit_width != 64) || !source_bits)
            {
                return false;
            }
            if (!cast_signed && source_bits == 64)
            {
                u32 branchy_row = machine_x64_select_row(
                    selector, (MachineInstruction){
                                  .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                               machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                  .opcode = (u16)(cast_target_type->bit_width == 64 ? MACHINE_X64_CVT_U64_TO_F64 : MACHINE_X64_CVT_U64_TO_F32),
                              });
                machine_x64_define(selector, result_register, branchy_row);
                return true;
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
            if (!source_type || source_type->kind != IR_TYPE_FLOAT || (source_type->bit_width != 32 && source_type->bit_width != 64))
            {
                return false;
            }
            if (to_unsigned && cast_target_type && cast_target_type->kind == IR_TYPE_INTEGER && cast_target_type->bit_width == 64)
            {
                u32 branchy_row = machine_x64_select_row(
                    selector, (MachineInstruction){
                                  .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                               machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                  .opcode = (u16)(source_type->bit_width == 64 ? MACHINE_X64_CVT_F64_TO_U64 : MACHINE_X64_CVT_F32_TO_U64),
                              });
                machine_x64_define(selector, result_register, branchy_row);
                return true;
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
        case IR_CONVERSION_INTEGER_REINTERPRET:
        case IR_CONVERSION_POINTER_REINTERPRET:
        case IR_CONVERSION_INTEGER_TO_POINTER:
            opcode = MACHINE_X64_MOV_RR;
            break;
        case IR_CONVERSION_INTEGER_TRUNCATE:
        case IR_CONVERSION_POINTER_TO_INTEGER:
        {
            // The register model keeps every value zero-extended to 64
            // bits, so a narrowing cast must actually clear the discarded
            // top: a plain 64-bit copy would smuggle the source's high
            // bits into 64-bit consumers like unsigned index scaling.
            u32 destination_bits = machine_x64_scalar_bit_width(cast_target_type);
            opcode = (u16)(destination_bits == 8    ? MACHINE_X64_MOVZX8_RR
                           : destination_bits == 16 ? MACHINE_X64_MOVZX16_RR
                           : destination_bits == 32 ? MACHINE_X64_MOV32_RR
                           : destination_bits == 64 ? MACHINE_X64_MOV_RR
                                                    : 0);
        }
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
        if (instruction->unary_operation == IR_UNARY_INTEGER_POPULATION_COUNT)
        {
            if (!target_cpu_feature_has(selector->target, TARGET_CPU_FEATURE_X86_POPCNT))
            {
                return false;
            }
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                           .opcode = (u16)(wide ? MACHINE_X64_POPCNT64 : MACHINE_X64_POPCNT32),
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS || instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS)
        {
            // Canonical form: bsf for trailing zeros, bsr xor width-1 for
            // leading — both undefined on zero input, exactly like the
            // builtins they lower.
            bool leading = instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS;
            u32 bit_scan_register = leading ? machine_x64_synthesize_register(selector) : result_register;
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bit_scan_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                           .opcode = (u16)(leading ? (wide ? MACHINE_X64_BSR64 : MACHINE_X64_BSR32)
                                                                                   : (wide ? MACHINE_X64_BSF64 : MACHINE_X64_BSF32)),
                                                       });
            machine_x64_define(selector, bit_scan_register, row);
            if (leading)
            {
                u32 flip_immediate = selector->immediates.total_count;
                u64* flip_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
                *flip_row = wide ? 63 : 31;
                u32 flip_register = machine_x64_synthesize_register(selector);
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, flip_register),
                                                                  machine_ref_make(MACHINE_REF_IMMEDIATE, flip_immediate)},
                                                     .opcode = MACHINE_X64_MOV_RI,
                                                 });
                u32 flip_row_index = machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bit_scan_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, flip_register)},
                                                                     .opcode = (u16)(wide ? MACHINE_X64_XOR64 : MACHINE_X64_XOR32),
                                                                 });
                machine_x64_define(selector, result_register, flip_row_index);
            }
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
            u32 mask_register = machine_x64_synthesize_register(selector);
            u32 immediate_index = selector->immediates.total_count;
            u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
            *immediate_row = float_type->bit_width == 64 ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000);
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register),
                                                              machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                 .opcode = MACHINE_X64_MOV_RI,
                                             });
            u32 negate_row = machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register)},
                                                 .opcode = (u16)(float_type->bit_width == 64 ? MACHINE_X64_XOR64 : MACHINE_X64_XOR32),
                                             });
            machine_x64_define(selector, result_register, negate_row);
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
        // CMPXCHG16B materializes its success boolean immediately after the
        // atomic row; later aggregate copies consume that value, never live
        // flags across unrelated IR rows.
        if (result_register != UINT32_MAX && instruction->operand_count >= 2 &&
            (instruction->binary_operation == IR_BINARY_INTEGER_EQUAL || instruction->binary_operation == IR_BINARY_INTEGER_NOT_EQUAL) &&
            instruction->operands[0].value < function->value_count && instruction->operands[1].value < function->value_count)
        {
            u32 success_register = selector->atomic_success_registers[instruction->operands[0].value];
            IrValue* observed_value = function->values + instruction->operands[0].value;
            IrInstruction* atomic_instruction = 0;
            if (observed_value->definition.value < function->instruction_count)
            {
                atomic_instruction = function->instructions + observed_value->definition.value;
            }
            bool is_atomic_expected_compare = atomic_instruction && atomic_instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE &&
                                               atomic_instruction->operand_count >= 2 &&
                                               atomic_instruction->operands[1].value == instruction->operands[1].value;
            if (success_register != UINT32_MAX && is_atomic_expected_compare && instruction->binary_operation == IR_BINARY_INTEGER_EQUAL)
            {
                u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, success_register)},
                                                               .opcode = MACHINE_X64_MOV_RR,
                                                           });
                machine_x64_define(selector, result_register, row);
                return true;
            }
        }
        if (instruction->operand_count >= 2 && instruction->operands[0].value < function->value_count && instruction->result.value < function->value_count)
        {
            IrType* left_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
            IrType* result_type = ir_type_from_id(&program->types, function->values[instruction->result.value].canonical_type);
            if (left_type && result_type && left_type->kind == IR_TYPE_INTEGER && result_type->kind == IR_TYPE_INTEGER &&
                left_type->bit_width == 128 && result_type->bit_width == 128 &&
                instruction->binary_operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT)
            {
                u32 amount;
                if (!machine_x64_constant_shift_amount(selector, instruction->operands[1], &amount))
                {
                    return false;
                }
                return machine_x64_select_i128_unsigned_shift_right(selector, instruction, amount);
            }
        }
        u32 left_register;
        u32 right_register;
        if (result_register == UINT32_MAX || !machine_x64_operand_register(selector, instruction->operands[0], &left_register) ||
            !machine_x64_operand_register(selector, instruction->operands[1], &right_register))
        {
            return false;
        }
        IrTypeId operand_type_id = function->values[instruction->operands[0].value].canonical_type;
        IrType* operand_type = ir_type_from_id(&program->types, operand_type_id);
        if (operand_type && operand_type->kind == IR_TYPE_VECTOR)
        {
            // Element-wise integer vector binary as one three-address EVEX
            // row; the payload carries the 66 0F map opcode the canonical
            // native path picks by operation and lane width, and bit 8
            // marks the 64-bit-lane forms whose EVEX encodings are W1
            // (vpaddq/vpsubq — their W0 encodings #UD on real hardware).
            if (!machine_x64_type_is_vector_register(operand_type) || !machine_x64_simd_supported(selector->target, IR_SIMD_SPLAT_BYTE))
            {
                return false;
            }
            IrType* element = ir_type_from_id(&program->types, operand_type->element_type);
            if (!element || element->kind != IR_TYPE_INTEGER)
            {
                return false;
            }
            u32 vector_opcode = 0;
            switch (instruction->binary_operation)
            {
                break;
            case IR_BINARY_VECTOR_INTEGER_ADD:
                vector_opcode = element->bit_width == 8    ? 0xfcu
                                : element->bit_width == 16 ? 0xfdu
                                : element->bit_width == 32 ? 0xfeu
                                : element->bit_width == 64 ? (0xd4u | 0x100u)
                                                           : 0;
                break;
            case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
                vector_opcode = element->bit_width == 8    ? 0xf8u
                                : element->bit_width == 16 ? 0xf9u
                                : element->bit_width == 32 ? 0xfau
                                : element->bit_width == 64 ? (0xfbu | 0x100u)
                                                           : 0;
                break;
            case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
                vector_opcode = 0xdbu;
                break;
            case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
                vector_opcode = 0xebu;
                break;
            case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
                vector_opcode = 0xefu;
                break;
            default:
                break;
            }
            if (!vector_opcode)
            {
                return false;
            }
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                           .payload = vector_opcode,
                                                           .opcode = MACHINE_X64_VBINARY,
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
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
            if (machine_x64_opcode_is_ssa_two_address(arithmetic))
            {
                // Keep the physical two-address operation in explicit SSA
                // form: operand 0 defines the result, operand 1 is the first
                // source (tied by opcode metadata), and operand 2 is the
                // second source. The old MOV-plus-RMW pair obscured this
                // relationship from the allocator and scheduler.
                u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                               .opcode = arithmetic,
                                                           });
                machine_x64_define(selector, result_register, row);
            }
            else
            {
                // Fixed-register shifts and divide/remainder retain their
                // legacy constrained two-row shape until their target
                // fixed-register contract is represented in metadata.
                u32 copy_row = machine_x64_select_row(selector, (MachineInstruction){
                                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register)},
                                                                     .opcode = MACHINE_X64_MOV_RR,
                                                                 });
                machine_x64_define(selector, result_register, copy_row);
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                     .opcode = arithmetic,
                                                 });
            }
            return true;
        }
        u32 condition = machine_x64_condition_from_comparison(instruction->binary_operation);
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
        if (result_register == source_register)
        {
            // Aliased through the pointer chain: the dereference is a
            // name for the promoted local, not code.
            return true;
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
        if (result_register == UINT32_MAX || !symbol)
        {
            return false;
        }
        // Thread-local addresses select only where the canonical emitter's
        // local-exec fs-base sequence applies; other OSes keep the fallback.
        if (symbol->is_thread_local && selector->target.os != OPERATING_SYSTEM_LINUX && selector->target.os != OPERATING_SYSTEM_ANDROID)
        {
            return false;
        }
        u32 target_index = selector->call_targets.total_count;
        IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
        *target_row = instruction->symbol;
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                       .payload = target_index,
                                                       .opcode = (u16)(symbol->is_thread_local ? MACHINE_X64_LEA_TLS : MACHINE_X64_LEA_SYMBOL),
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
        // The whole member address is one row: the offset rides in the
        // frame displacement of a local, or in the lea of a pointer.
        return machine_x64_select_place_address_offset(selector, instruction->operands[0], result_register,
                                                       (u32)aggregate->fields[field_index].offset);
    }
    break;
    case IR_OPCODE_DEBUG_TRAP:
    {
        machine_x64_select_row(selector, (MachineInstruction){.opcode = MACHINE_X64_INT3});
        return true;
    }
    break;
    case IR_OPCODE_AGGREGATE:
    {
        // Field-by-field construction into the value's slot, mirroring the
        // canonical path: scalar members store sized at their offsets,
        // aggregate members copy from their own slots through the field's
        // address.
        IrType* type = ir_type_from_id(&program->types, function->values[instruction->result.value].canonical_type);
        u32 slot = selector->value_stack_slots[instruction->result.value];
        if (!type || (type->kind != IR_TYPE_STRUCT && type->kind != IR_TYPE_UNION) || slot == UINT32_MAX ||
            instruction->immediate_count != instruction->operand_count || (instruction->operand_count && !instruction->immediates))
        {
            return false;
        }
        if (!type->layout.resolved || type->layout.size > INT32_MAX)
        {
            return false;
        }
        // The operands need not cover the object — a union initializes only
        // one member, and padding is never an operand — so the whole slot is
        // zero-filled first, exactly like the canonical path.
        u32 zero_register = machine_x64_synthesize_register(selector);
        u32 zero_fill_immediate = selector->immediates.total_count;
        u64* zero_fill_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
        *zero_fill_row = 0;
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, zero_register),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, zero_fill_immediate)},
                                             .opcode = MACHINE_X64_MOV_RI,
                                         });
        u64 zero_filled = 0;
        while (zero_filled < type->layout.size)
        {
            u64 zero_remaining = type->layout.size - zero_filled;
            u32 zero_chunk = zero_remaining >= 8 ? 8 : zero_remaining >= 4 ? 4 : zero_remaining >= 2 ? 2 : 1;
            u16 zero_store_opcode = zero_chunk == 1   ? MACHINE_X64_STORE_FRAME8
                                    : zero_chunk == 2 ? MACHINE_X64_STORE_FRAME16
                                    : zero_chunk == 4 ? MACHINE_X64_STORE_FRAME32
                                                      : MACHINE_X64_STORE_FRAME64;
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, zero_register)},
                                                 .payload = (u32)zero_filled,
                                                 .opcode = zero_store_opcode,
                                             });
            zero_filled += zero_chunk;
        }
        u8* member_emitted = arena_allocate(selector->arena, u8, instruction->operand_count ? instruction->operand_count : 1);
        for (u32 index = 0; index < instruction->operand_count; index += 1)
        {
            member_emitted[index] = 0;
        }
        for (u32 index = 0; index < instruction->operand_count; index += 1)
        {
            u64 field_index = instruction->immediates[index];
            if (member_emitted[index] || field_index >= type->field_count)
            {
                if (field_index >= type->field_count)
                {
                    return false;
                }
                continue;
            }
            u64 field_offset = type->fields[field_index].offset;
            IrType* field_type = ir_type_from_id(&program->types, type->fields[field_index].type);
            u64 field_size = field_type && field_type->layout.resolved ? field_type->layout.size : 0;
            if (!field_size || field_offset > INT32_MAX)
            {
                return false;
            }
            if (!type->fields[field_index].is_bit_field)
            {
                if (!machine_x64_select_member_write(selector, slot, field_offset, field_size, instruction->operands[index]))
                {
                    return false;
                }
                member_emitted[index] = 1;
                continue;
            }
            // Bit-field members of one storage unit accumulate in a zeroed
            // register — mask to width, shift to position via a
            // power-of-two multiply, or together — and the unit stores
            // once. Initializers materialize every member, so the
            // accumulated word is the whole unit.
            u16 unit_store_opcode = field_size == 1   ? MACHINE_X64_STORE_FRAME8
                                    : field_size == 2 ? MACHINE_X64_STORE_FRAME16
                                    : field_size == 4 ? MACHINE_X64_STORE_FRAME32
                                    : field_size == 8 ? MACHINE_X64_STORE_FRAME64
                                                      : 0;
            if (!unit_store_opcode)
            {
                return false;
            }
            u32 unit_register = machine_x64_synthesize_register(selector);
            u32 zero_immediate = selector->immediates.total_count;
            u64* zero_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
            *zero_row = 0;
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register),
                                                              machine_ref_make(MACHINE_REF_IMMEDIATE, zero_immediate)},
                                                 .opcode = MACHINE_X64_MOV_RI,
                                             });
            for (u32 sibling = index; sibling < instruction->operand_count; sibling += 1)
            {
                u64 sibling_field = instruction->immediates[sibling];
                if (member_emitted[sibling] || sibling_field >= type->field_count || !type->fields[sibling_field].is_bit_field ||
                    type->fields[sibling_field].offset != field_offset)
                {
                    continue;
                }
                u32 bit_offset = type->fields[sibling_field].bit_offset;
                u32 bit_width = type->fields[sibling_field].bit_width;
                if (!bit_width || bit_offset + bit_width > field_size * 8)
                {
                    return false;
                }
                u32 value_register;
                if (!machine_x64_operand_register(selector, instruction->operands[sibling], &value_register))
                {
                    return false;
                }
                u32 masked_register = machine_x64_synthesize_register(selector);
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_X64_MOV_RR,
                                                 });
                u32 mask_immediate = selector->immediates.total_count;
                u64* mask_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
                *mask_row = bit_width >= 64 ? UINT64_MAX : (((u64)1 << bit_width) - 1);
                u32 mask_register = machine_x64_synthesize_register(selector);
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register),
                                                                  machine_ref_make(MACHINE_REF_IMMEDIATE, mask_immediate)},
                                                     .opcode = MACHINE_X64_MOV_RI,
                                                 });
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register)},
                                                     .opcode = MACHINE_X64_AND64,
                                                 });
                if (bit_offset)
                {
                    u32 scale_immediate = selector->immediates.total_count;
                    u64* scale_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
                    *scale_row = (u64)1 << bit_offset;
                    u32 scale_register = machine_x64_synthesize_register(selector);
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scale_register),
                                                                      machine_ref_make(MACHINE_REF_IMMEDIATE, scale_immediate)},
                                                         .opcode = MACHINE_X64_MOV_RI,
                                                     });
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scale_register)},
                                                         .opcode = MACHINE_X64_IMUL64,
                                                     });
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register)},
                                                     .opcode = MACHINE_X64_OR64,
                                                 });
                member_emitted[sibling] = 1;
            }
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register)},
                                                 .payload = (u32)field_offset,
                                                 .opcode = unit_store_opcode,
                                             });
        }
        return true;
    }
    break;
    case IR_OPCODE_ARRAY:
    {
        // Element-by-element construction into the value's slot at scaled
        // offsets; the frontend materializes every element including the
        // zero tail, so position times element size covers the object.
        IrType* type = ir_type_from_id(&program->types, function->values[instruction->result.value].canonical_type);
        u32 slot = selector->value_stack_slots[instruction->result.value];
        if (!type || type->kind != IR_TYPE_ARRAY || slot == UINT32_MAX)
        {
            return false;
        }
        IrType* element_type = ir_type_from_id(&program->types, type->element_type);
        u64 element_size = element_type && element_type->layout.resolved ? element_type->layout.size : 0;
        if (!element_size || (u64)instruction->operand_count * element_size > INT32_MAX)
        {
            return false;
        }
        for (u32 index = 0; index < instruction->operand_count; index += 1)
        {
            if (!machine_x64_select_member_write(selector, slot, (u64)index * element_size, element_size, instruction->operands[index]))
            {
                return false;
            }
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
            // The element size folds into the multiply the same way.
            u32 immediate_index = selector->immediates.total_count;
            u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
            *immediate_row = element->layout.size;
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register),
                                                              machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                 .opcode = MACHINE_X64_IMUL64_RRI,
                                             });
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
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
                definition->opcode == IR_OPCODE_FIELD || machine_x64_local_is_indirect(selector, instruction->operands[0]))
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
        IrType* register_loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
        if (machine_x64_type_is_vector_register(register_loaded_type))
        {
            // Whole-vector load into a ZMM-class register: a promoted
            // vector local aliases or copies, a slot-backed one loads from
            // its frame home, and everything else goes through an address
            // vreg.
            if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD || !machine_x64_simd_supported(selector->target, IR_SIMD_SPLAT_BYTE))
            {
                return false;
            }
            if (definition->opcode == IR_OPCODE_LOCAL && selector->value_virtual_registers[instruction->operands[0].value] != UINT32_MAX &&
                !machine_x64_local_is_indirect(selector, instruction->operands[0]))
            {
                if (result_register == selector->value_virtual_registers[instruction->operands[0].value])
                {
                    // Aliased: the load is a name for the local, not code.
                    return true;
                }
                u32 row = machine_x64_select_row(
                    selector,
                    (MachineInstruction){
                        .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                     machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, selector->value_virtual_registers[instruction->operands[0].value])},
                        .opcode = MACHINE_X64_VMOV_RR,
                    });
                machine_x64_define(selector, result_register, row);
                return true;
            }
            if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
            {
                u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                               .opcode = MACHINE_X64_VLOAD_FRAME,
                                                           });
                machine_x64_define(selector, result_register, row);
                return true;
            }
            if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                definition->opcode == IR_OPCODE_FIELD || machine_x64_local_is_indirect(selector, instruction->operands[0]))
            {
                u32 address_register;
                if (!machine_x64_operand_register(selector, instruction->operands[0], &address_register))
                {
                    return false;
                }
                u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                               .opcode = MACHINE_X64_VLOAD_PTR,
                                                           });
                machine_x64_define(selector, result_register, row);
                return true;
            }
            return false;
        }
        if (definition->opcode == IR_OPCODE_LOCAL && selector->value_virtual_registers[instruction->operands[0].value] != UINT32_MAX &&
            !machine_x64_local_is_indirect(selector, instruction->operands[0]))
        {
            if (result_register == selector->value_virtual_registers[instruction->operands[0].value])
            {
                // Aliased: the load is a name for the local, not code.
                return true;
            }
            // Promoted but not aliasable here: the load is a register copy.
            u32 row = machine_x64_select_row(
                selector, (MachineInstruction){
                              .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                           machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, selector->value_virtual_registers[instruction->operands[0].value])},
                              .opcode = MACHINE_X64_MOV_RR,
                          });
            machine_x64_define(selector, result_register, row);
            return true;
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
            definition->opcode == IR_OPCODE_FIELD || machine_x64_local_is_indirect(selector, instruction->operands[0]))
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
                definition->opcode == IR_OPCODE_FIELD || machine_x64_local_is_indirect(selector, instruction->operands[0]))
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
        if (machine_x64_type_is_vector_register(stored_type))
        {
            // Whole-vector store out of a ZMM-class register: a promoted
            // vector local takes a full 512-bit register copy, a
            // slot-backed one stores to its frame home, and everything
            // else goes through an address vreg.
            if (instruction->opcode == IR_OPCODE_ATOMIC_STORE || !machine_x64_simd_supported(selector->target, IR_SIMD_SPLAT_BYTE))
            {
                return false;
            }
            if (definition->opcode == IR_OPCODE_LOCAL && selector->value_virtual_registers[instruction->operands[0].value] != UINT32_MAX &&
                !machine_x64_local_is_indirect(selector, instruction->operands[0]))
            {
                u32 place_register = selector->value_virtual_registers[instruction->operands[0].value];
                u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, place_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                               .opcode = MACHINE_X64_VMOV_RR,
                                                           });
                machine_x64_define(selector, place_register, row);
                return true;
            }
            if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_X64_VSTORE_FRAME,
                                                 });
                return true;
            }
            if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                definition->opcode == IR_OPCODE_FIELD || machine_x64_local_is_indirect(selector, instruction->operands[0]))
            {
                u32 address_register;
                if (!machine_x64_operand_register(selector, instruction->operands[0], &address_register))
                {
                    return false;
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_X64_VSTORE_PTR,
                                                 });
                return true;
            }
            return false;
        }
        u32 size_index = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : size == 8 ? 3 : UINT32_MAX;
        if (size_index == UINT32_MAX)
        {
            return false;
        }
        if (definition->opcode == IR_OPCODE_LOCAL && selector->value_virtual_registers[instruction->operands[0].value] != UINT32_MAX &&
            !machine_x64_local_is_indirect(selector, instruction->operands[0]))
        {
            // Promoted local: the store is a full-width register copy —
            // the same 64-bit image a direct-slot store writes, since the
            // register model keeps every value zero-extended.
            u32 place_register = selector->value_virtual_registers[instruction->operands[0].value];
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, place_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                           .opcode = MACHINE_X64_MOV_RR,
                                                       });
            machine_x64_define(selector, place_register, row);
            return true;
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
            definition->opcode == IR_OPCODE_FIELD || machine_x64_local_is_indirect(selector, instruction->operands[0]))
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
    case IR_OPCODE_SIMD:
    {
        // The 512-bit vocabulary, one EVEX row per operation with values
        // register-resident — the canonical path's per-operand frame
        // round-trips are exactly what this subset removes. Masks arrive
        // and leave in general registers; rows that consume or produce one
        // stage through k1 inside the encoder.
        IrSimdOperation operation = (IrSimdOperation)instruction->simd_operation;
        IrSimdShape shape = ir_simd_operation_shape(operation);
        if (!machine_x64_simd_supported(selector->target, operation) || instruction->operand_count != shape.operand_count ||
            instruction->immediate_count != shape.immediate_count || (shape.immediate_count && !instruction->immediates) ||
            (shape.has_result && result_register == UINT32_MAX))
        {
            return false;
        }
        u32 operand_registers[4];
        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
        {
            if (!machine_x64_operand_register(selector, instruction->operands[operand_index], operand_registers + operand_index))
            {
                return false;
            }
        }
        u32 immediate = shape.immediate_count ? (u32)instruction->immediates[0] : 0;
        switch (operation)
        {
            break;
        case IR_SIMD_LOAD:
        case IR_SIMD_LOAD_MASKED:
        {
            bool masked = operation == IR_SIMD_LOAD_MASKED;
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0]),
                                                                        masked ? machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1]) : 0},
                                                           .opcode = masked ? MACHINE_X64_VLOAD_PTR_MASKED : MACHINE_X64_VLOAD_PTR,
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        case IR_SIMD_STORE:
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0]),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1])},
                                                 .opcode = MACHINE_X64_VSTORE_PTR,
                                             });
            return true;
        }
        case IR_SIMD_STORE_MASKED:
        case IR_SIMD_COMPRESS_STORE_BYTE:
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0]),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1]),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[2])},
                                                 .opcode = operation == IR_SIMD_STORE_MASKED ? MACHINE_X64_VSTORE_PTR_MASKED : MACHINE_X64_VCOMPRESS_STORE_PTR,
                                             });
            return true;
        }
        case IR_SIMD_SPLAT_BYTE:
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0])},
                                                           .opcode = MACHINE_X64_VSPLATB,
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        case IR_SIMD_COMPARE_EQUAL_BYTE:
        case IR_SIMD_COMPARE_LESS_BYTE:
        case IR_SIMD_TEST_MASK_BYTE:
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0]),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1])},
                                                           .payload = operation == IR_SIMD_COMPARE_EQUAL_BYTE ? 0u : operation == IR_SIMD_COMPARE_LESS_BYTE ? 1u : 2u,
                                                           .opcode = MACHINE_X64_VPCMP_MASK,
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        case IR_SIMD_SIGN_MASK_BYTE:
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0])},
                                                           .opcode = MACHINE_X64_VPMOVB2M,
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        case IR_SIMD_PERMUTE2_BYTE:
        {
            // vpermt2b reads the low table from its destination register
            // and overwrites it, so the low table copies into the result
            // first; the copy coalesces whenever the low value dies here.
            u32 copy_row = machine_x64_select_row(selector, (MachineInstruction){
                                                                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                             machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1])},
                                                                .opcode = MACHINE_X64_VMOV_RR,
                                                            });
            machine_x64_define(selector, result_register, copy_row);
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0]),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[2]),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[3])},
                                                 .opcode = MACHINE_X64_VPERMT2B,
                                             });
            return true;
        }
        case IR_SIMD_COMPRESS_BYTE:
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0]),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1])},
                                                           .opcode = MACHINE_X64_VCOMPRESSB,
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        case IR_SIMD_WIDEN_BYTE_TO_WORD:
        case IR_SIMD_SHIFT_LEFT_WORD:
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0])},
                                                           .payload = immediate,
                                                           .opcode = operation == IR_SIMD_WIDEN_BYTE_TO_WORD ? MACHINE_X64_VPMOVZXBD : MACHINE_X64_VPSLLD_RI,
                                                       });
            machine_x64_define(selector, result_register, row);
            return true;
        }
        case IR_SIMD_TERNARY_WORD:
        {
            // vpternlogd reads its first source from the destination, the
            // same in-place shape as vpermt2b.
            u32 copy_row = machine_x64_select_row(selector, (MachineInstruction){
                                                                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                             machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0])},
                                                                .opcode = MACHINE_X64_VMOV_RR,
                                                            });
            machine_x64_define(selector, result_register, copy_row);
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1]),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[2])},
                                                 .payload = immediate,
                                                 .opcode = MACHINE_X64_VPTERNLOGD,
                                             });
            return true;
        }
        case IR_SIMD_COUNT:
            return false;
        }
        return false;
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
        if (callee_returns_value && !machine_x64_value_shape(program, callee_type->return_type, IR_ABI_USE_RESULT, selector->target, &callee_return_shape))
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
            if (!machine_x64_value_shape(program, argument_type_id, IR_ABI_USE_ARGUMENT, selector->target, argument_shapes + argument_index))
            {
                return false;
            }
            // A variadic call's AL protocol would have to count vector
            // registers; the subset keeps the zero-vector convention and
            // leaves vector-carrying variadic calls canonical.
            if (variadic_call && argument_shapes[argument_index].vector)
            {
                return false;
            }
            u32 tight_stack_parts = call_stack_part_count;
            machine_x64_place_argument(argument_shapes + argument_index, &call_integer_count, &call_float_count, &call_stack_part_count,
                                       argument_placements + argument_index);
            // A stack vector argument whose 64-aligned offset opens a gap
            // needs padding eightbytes the push machinery cannot produce;
            // the canonical caller keeps that call.
            if (argument_placements[argument_index].on_stack && argument_shapes[argument_index].vector &&
                argument_placements[argument_index].first_stack_part != tight_stack_parts)
            {
                return false;
            }
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
        // A stack vector argument's register value bounces through a
        // dedicated 64-byte frame slot so the eightbyte pushes below have
        // memory to read; the store is frame-relative and free of the
        // push-moved stack pointer.
        for (u32 argument_index = 0; argument_index < call_argument_count; argument_index += 1)
        {
            if (!argument_placements[argument_index].on_stack || !argument_shapes[argument_index].vector)
            {
                continue;
            }
            argument_slots[argument_index] = machine_x64_append_slot(selector, 64, 16);
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, argument_slots[argument_index]),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_registers[argument_index])},
                                                 .opcode = MACHINE_X64_VSTORE_FRAME,
                                             });
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
            if (argument_shapes[argument_index].aggregate || argument_shapes[argument_index].vector)
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
        bool call_vector_registers = false;
        for (u32 argument_index = 0; argument_index < call_argument_count; argument_index += 1)
        {
            MachineX64ValueShape* shape = argument_shapes + argument_index;
            if (argument_placements[argument_index].on_stack)
            {
                continue;
            }
            u32 next_integer = argument_placements[argument_index].first_integer;
            u32 next_float = argument_placements[argument_index].first_float;
            if (shape->vector)
            {
                // The whole 512-bit value takes its SSE-sequence register;
                // the allocator relocates the source into that exact ZMM,
                // so no later staging row can disturb an already placed one.
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_ZMM0 + next_float),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, argument_registers[argument_index])},
                                                     .opcode = MACHINE_X64_VMOV_RR,
                                                 });
                call_vector_registers = true;
                continue;
            }
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
        u16 call_flags = (u16)((variadic_call ? (1u | (call_float_count << 1)) : 0) |
                               (call_vector_registers ? MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE : 0));
        if (direct_call)
        {
            u32 target_index = selector->call_targets.total_count;
            IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
            *target_row = instruction->symbol;
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .payload = target_index,
                                                 .opcode = MACHINE_X64_CALL_DIRECT,
                                                 .flags = call_flags,
                                             });
        }
        else
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, callee_register)},
                                                 .opcode = MACHINE_X64_CALL_INDIRECT,
                                                 .flags = call_flags,
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
            if (callee_return_shape.vector)
            {
                // The 512-bit result arrives in ZMM0 — the call's own
                // vzeroupper fires before the call, never after — and the
                // capture binds in place exactly like a scalar RAX capture.
                row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_ZMM0)},
                                                           .opcode = MACHINE_X64_VMOV_RR,
                                                       });
            }
            else if (callee_return_shape.part_is_float[0])
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
        IrType* atomic_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u64 size = atomic_type && atomic_type->layout.resolved ? atomic_type->layout.size : 0;
        if (size == 16)
        {
            // CMPXCHG16B consumes and produces two eightbyte values. Keep
            // all three value images in frame slots; only the address is a
            // virtual register and is constrained to slot 3 (RSI), leaving
            // RAX/RDX/RBX/RCX wholly available to the instruction's implicit
            // protocol.
            u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
            u32 expected_slot = instruction->operand_count >= 2 && instruction->operands[1].value < function->value_count
                                    ? selector->value_stack_slots[instruction->operands[1].value]
                                    : UINT32_MAX;
            u32 desired_slot = instruction->operand_count >= 3 && instruction->operands[2].value < function->value_count
                                   ? selector->value_stack_slots[instruction->operands[2].value]
                                   : UINT32_MAX;
            if (!target_cpu_feature_has(selector->target, TARGET_CPU_FEATURE_X86_CX16) || instruction->operand_count < 3 ||
                result_slot == UINT32_MAX || expected_slot == UINT32_MAX || desired_slot == UINT32_MAX)
            {
                return false;
            }
            u32 address_register = machine_x64_synthesize_register(selector);
            if (!machine_x64_select_place_address(selector, instruction->operands[0], address_register))
            {
                return false;
            }
            machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                    machine_ref_make(MACHINE_REF_STACK_SLOT, expected_slot),
                                                                    machine_ref_make(MACHINE_REF_STACK_SLOT, desired_slot),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                       .opcode = MACHINE_X64_ATOMIC_CMPXCHG16,
                                                   });
            u32 success_register = machine_x64_synthesize_register(selector);
            u32 success_row = machine_x64_select_row(selector, (MachineInstruction){
                                                                  .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, success_register)},
                                                                  .payload = MACHINE_X64_CONDITION_EQUAL,
                                                                  .opcode = MACHINE_X64_SETCC,
                                                              });
            machine_x64_define(selector, success_register, success_row);
            selector->atomic_success_registers[instruction->result.value] = success_register;
            return true;
        }
        u32 expected_register;
        u32 desired_register;
        if (result_register == UINT32_MAX || instruction->operand_count < 3 ||
            !machine_x64_operand_register(selector, instruction->operands[1], &expected_register) ||
            !machine_x64_operand_register(selector, instruction->operands[2], &desired_register))
        {
            return false;
        }
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
        // Control never reaches this terminator; ud2 keeps the block
        // verifier-well-formed, faults loudly if control ever arrives, and
        // matches the canonical bytes.
        machine_x64_select_row(selector, (MachineInstruction){
                                             .opcode = MACHINE_X64_UD2,
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
        // A fused condition re-selects the chain's innermost comparison
        // here, immediately before JCC: only allocator edits can land
        // between the flags define and its use, and every edit form is a
        // flag-preserving mov (frame load/store, reg copy, movabs remat).
        MachineX64BranchFusion* fusion =
            instruction->operands[0].value < function->value_count ? selector->branch_fusions + instruction->operands[0].value : 0;
        if (fusion && fusion->condition)
        {
            u32 left_register;
            u32 right_register;
            if (!machine_x64_operand_register(selector, (IrValueId){.value = fusion->left}, &left_register))
            {
                return false;
            }
            if (fusion->right != UINT32_MAX)
            {
                if (!machine_x64_operand_register(selector, (IrValueId){.value = fusion->right}, &right_register))
                {
                    return false;
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                     .opcode = (u16)(fusion->wide ? MACHINE_X64_CMP64 : MACHINE_X64_CMP32),
                                                 });
            }
            else
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register)},
                                                     .opcode = MACHINE_X64_TEST_RR,
                                                 });
            }
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value),
                                                              machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[1].value)},
                                                 .payload = fusion->condition,
                                                 .opcode = MACHINE_X64_JCC,
                                             });
            return true;
        }
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
            else if (selector->return_shape.vector)
            {
                // The 512-bit result leaves in ZMM0 and stays live through
                // the return row, whose vzeroupper the flag below suppresses.
                u32 value_register;
                if (!machine_x64_operand_register(selector, instruction->operands[0], &value_register))
                {
                    return false;
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_ZMM0),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_X64_VMOV_RR,
                                                 });
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
                                             .flags = (u16)(instruction->operand_count && selector->return_shape.vector
                                                                ? MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE
                                                                : 0),
                                         });
        return true;
    }
    break;
    default:
        return false;
    }
    return false;
}

MachineSelectResult machine_select_canonical_function_x86_64(Arena* arena, IrProgram* program, IrFunction* function, Target target)
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
    if (!function_type || function_type->kind != IR_TYPE_FUNCTION)
    {
        return result;
    }
    if (function_type->is_variadic)
    {
        // The machine selector models the System V register sequence.  A
        // Windows target has a different variadic definition ABI and stays
        // on the canonical path even when the body ignores its tail.  SysV
        // bodies continue through ordinary selection: the first va_* (or any
        // earlier unsupported operation) is then reported in true IR order.
        if (target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI)
        {
            return result;
        }
    }
    if (function_type->parameter_count > MACHINE_X64_MAX_ARGUMENTS)
    {
        return result;
    }
    IrType* return_type = ir_type_from_id(&program->types, function_type->return_type);
    bool returns_value = return_type && return_type->kind != IR_TYPE_VOID;
    MachineX64ValueShape signature_return_shape = {0};
    if (returns_value && !machine_x64_value_shape(program, function_type->return_type, IR_ABI_USE_RESULT, target, &signature_return_shape))
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
        if (!machine_x64_value_shape(program, function_type->parameter_types[parameter_index], IR_ABI_USE_ARGUMENT, target,
                                     signature_parameter_shapes + parameter_index))
        {
            return result;
        }
        machine_x64_place_argument(signature_parameter_shapes + parameter_index, &signature_integer_count, &signature_float_count,
                                   &signature_stack_count, signature_parameter_placements + parameter_index);
    }
    MachineX64Selector selector = {
        .arena = arena,
        .program = program,
        .function = function,
        .builder = machine_function_builder_begin(arena),
        .value_virtual_registers = arena_allocate(arena, u32, function->value_count),
        .value_stack_slots = arena_allocate(arena, u32, function->value_count),
        .value_indirect_slots = arena_allocate(arena, u32, function->value_count),
        .target = target,
        .supported = true,
        .failed_opcode = IR_OPCODE_COUNT,
    };
    selector.selection_prepass = machine_selection_prepass_build(arena, program, function);
    if (!selector.selection_prepass.valid)
    {
        return result;
    }
    machine_stream_initialize(&selector.immediates, sizeof(u64));
    machine_stream_initialize(&selector.stack_slots, sizeof(u32));
    machine_stream_initialize(&selector.call_targets, sizeof(IrSymbolId));
    machine_stream_initialize(&selector.switch_cases, sizeof(MachineSwitchCase));
    machine_stream_initialize(&selector.va_args, sizeof(MachineVaArg));
    MachineBuilderStream line_marks;
    machine_stream_initialize(&line_marks, sizeof(MachineLineMark));
    machine_stream_initialize(&selector.stack_slot_alignments, sizeof(u32));
    selector.return_shape = signature_return_shape;
    selector.hidden_return_slot = UINT32_MAX;
    selector.va_register_save_slot = UINT32_MAX;
    if (signature_return_shape.indirect)
    {
        selector.hidden_return_slot = machine_x64_append_slot(&selector, 8, 8);
    }
    if (function_type->is_variadic)
    {
        // SysV's va_list points at a fixed 176-byte register save area.  A
        // dedicated frame slot keeps its displacement in ordinary placement
        // data while VA_SAVE owns the prologue snapshot itself.
        selector.va_register_save_slot = machine_x64_append_slot(&selector, 176, 16);
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
        selector.value_indirect_slots[value_index] = UINT32_MAX;
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        for (IrBlockParameter* parameter = function->blocks[block_index].first_parameter; parameter; parameter = parameter->next)
        {
            IrType* parameter_type = ir_type_from_id(&program->types, parameter->canonical_type);
            bool float_scalar = parameter_type && parameter_type->kind == IR_TYPE_FLOAT &&
                                (parameter_type->bit_width == 32 || parameter_type->bit_width == 64);
            if (parameter->value.value >= function->value_count || (!machine_x64_type_is_scalar_register(parameter_type) && !float_scalar))
            {
                return result;
            }
            selector.value_virtual_registers[parameter->value.value] =
                machine_builder_virtual_register(&selector.builder, (MachineVirtualRegister){
                                                                         .definition_point = MACHINE_POINT_INVALID,
                                                                         .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                         .typed_origin = parameter->value.value,
                                                                     });
        }
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
    // and the atomic forms all keep the local in its slot. The byte size
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
            if (machine_x64_type_is_scalar_register(local_type) && (local_type->layout.size == 4 || local_type->layout.size == 8))
            {
                promotable_locals[instruction->result.value] = (u8)local_type->layout.size;
            }
            // Vector locals promote under the same rule: a 64-byte value
            // whose address never leaves a same-width load or store lives
            // in a ZMM-class register and its accesses become vector
            // copies, which is where the kernels' named chunk variables
            // stop round-tripping through the frame.
            else if (machine_x64_type_is_vector_register(local_type) && machine_x64_simd_supported(selector.target, IR_SIMD_SPLAT_BYTE))
            {
                promotable_locals[instruction->result.value] = 64;
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
                    place_use = !instruction->volatile_access &&
                                (machine_x64_type_is_scalar_register(access_type) || machine_x64_type_is_vector_register(access_type)) &&
                                access_type->layout.size == promotable_locals[used];
                }
                else if (operand_index == 0 && instruction->opcode == IR_OPCODE_STORE && instruction->operand_count >= 2 &&
                         instruction->operands[1].value < function->value_count)
                {
                    IrType* access_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
                    place_use = !instruction->volatile_access &&
                                (machine_x64_type_is_scalar_register(access_type) || machine_x64_type_is_vector_register(access_type)) &&
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
    // index base folds its offset into a real lea and stops the chain.
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
                // Vector locals clamp to the frame's sixteen-byte contract
                // exactly like the canonical frame layout: every access is
                // the unaligned vmovdqu8, so the declared 64-byte alignment
                // buys correctness nothing here.
                if (local_type && local_type->kind == IR_TYPE_VECTOR)
                {
                    local_alignment = BUSTER_MIN(local_alignment, 16u);
                }
                if (!local_type || !local_type->layout.resolved || local_type->layout.size > UINT32_MAX - 7)
                {
                    machine_x64_reject(&selector, instruction->opcode);
                    break;
                }
                if (promotable_locals[instruction->result.value])
                {
                    // Promoted: the local is a virtual register for its
                    // whole life and never owns a frame slot. Its loads
                    // and stores lower to copies, and its definition point
                    // is patched at the first store like any other
                    // classification vreg. Vector locals promote into the
                    // vector class. Promotion also covers over-aligned
                    // locals: the scan proved no use needs the address, so
                    // the declared alignment is unobservable.
                    selector.value_virtual_registers[instruction->result.value] =
                        machine_builder_virtual_register(&selector.builder, (MachineVirtualRegister){
                                                                                .definition_point = MACHINE_POINT_INVALID,
                                                                                .register_class = promotable_locals[instruction->result.value] == 64
                                                                                                      ? MACHINE_REGISTER_CLASS_VECTOR
                                                                                                      : MACHINE_REGISTER_CLASS_GENERAL,
                                                                                .typed_origin = instruction->result.value,
                                                                            });
                    continue;
                }
                if (local_alignment > 16)
                {
                    // Over-aligned local, the canonical frame layout's
                    // shape exactly: a padded raw slot and a pointer
                    // aligned into it at runtime by the LOCAL's own rows.
                    // The slot stays out of value_stack_slots so every
                    // consumer takes the pointer paths a GLOBAL takes.
                    if (local_type->layout.size > UINT32_MAX - 7 - local_alignment)
                    {
                        machine_x64_reject(&selector, instruction->opcode);
                        break;
                    }
                    selector.value_indirect_slots[instruction->result.value] =
                        machine_x64_append_slot(&selector, (u32)((local_type->layout.size + local_alignment - 1 + 7) & ~(u64)7), 8u);
                    selector.value_virtual_registers[instruction->result.value] =
                        machine_builder_virtual_register(&selector.builder, (MachineVirtualRegister){
                                                                                .definition_point = MACHINE_POINT_INVALID,
                                                                                .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                                .typed_origin = instruction->result.value,
                                                                            });
                    continue;
                }
                selector.value_stack_slots[instruction->result.value] =
                    machine_x64_append_slot(&selector, (u32)((local_type->layout.size + 7) & ~(u64)7), local_alignment);
                continue;
            }
            IrType* value_type = ir_type_from_id(&program->types, value->canonical_type);
            if ((instruction->opcode == IR_OPCODE_VA_START || instruction->opcode == IR_OPCODE_VA_COPY) && value_type &&
                value_type->kind == IR_TYPE_VA_LIST && value_type->layout.resolved && value_type->layout.size <= UINT32_MAX - 7)
            {
                // va_list is a four-word aggregate on SysV.  Keep the
                // temporary in a regular frame slot so STORE/LOAD and
                // VA_COPY can reuse the existing aggregate copy rows.
                selector.value_stack_slots[instruction->result.value] =
                    machine_x64_append_slot(&selector, (u32)((value_type->layout.size + 7) & ~(u64)7), 8);
                continue;
            }
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
            else if (machine_x64_type_is_vector_register(value_type))
            {
                // 64-byte vector values live in ZMM-class virtual
                // registers; the rows that cannot keep one there reject at
                // selection and the function falls back whole.
                selector.value_virtual_registers[instruction->result.value] =
                    machine_builder_virtual_register(&selector.builder, (MachineVirtualRegister){
                                                                            .definition_point = MACHINE_POINT_INVALID,
                                                                            .register_class = MACHINE_REGISTER_CLASS_VECTOR,
                                                                            .typed_origin = instruction->result.value,
                                                                        });
            }
            else if ((instruction->opcode == IR_OPCODE_ARGUMENT || instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_CALL ||
                      instruction->opcode == IR_OPCODE_VA_ARG ||
                      instruction->opcode == IR_OPCODE_CAST || instruction->opcode == IR_OPCODE_AGGREGATE || instruction->opcode == IR_OPCODE_ARRAY ||
                      instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE || instruction->opcode == IR_OPCODE_BINARY) &&
                     value_type && value_type->layout.resolved && value_type->layout.size <= UINT32_MAX - 7 &&
                     (value_type->kind == IR_TYPE_STRUCT || value_type->kind == IR_TYPE_UNION || value_type->kind == IR_TYPE_SLICE ||
                      (value_type->kind == IR_TYPE_INTEGER && value_type->bit_width == 128) ||
                      ((instruction->opcode == IR_OPCODE_ARRAY || instruction->opcode == IR_OPCODE_LOAD) && value_type->kind == IR_TYPE_ARRAY)))
            {
                // Aggregate values own a frame slot like the canonical
                // path's per-value storage; copies and ABI part transfers
                // address it directly.
                selector.value_stack_slots[instruction->result.value] =
                    machine_x64_append_slot(&selector, (u32)((value_type->layout.size + 7) & ~(u64)7), 8);
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
    // Compare/branch fusion. A C loop head lowers its controlling
    // expression as compare → widen → (!= 0) → BRANCH_IF, and each link
    // materializes: SETCC/MOVZX makes a bool, a movabs makes the zero, a
    // second compare makes a second flag, and the branch tests it — ~10
    // instructions where clang emits cmp+jcc. When every member of that
    // chain has exactly one use and sits in the branch's own block, the
    // branch re-selects the innermost comparison as CMP (or the residual
    // truthiness test as TEST) at the terminator and the members select
    // into nothing. Walking the chain keeps an invariant — the branch
    // outcome equals truthy(chain value) xor negate — through (!= 0) and
    // (== 0) against a literal zero, truthiness-preserving extensions,
    // and BOOLEAN_NOT; a comparison that is none of those terminates the
    // walk as the CMP, and anything else terminates it as TEST. Both
    // read their operands at the branch row instead of the member's, so
    // a promoted local stored between the deepest absorbed member and
    // the branch forecloses the fusion — the walk stamps every promoted
    // local's latest store ordinal and the commit compares. Types stay
    // integer-class scalars throughout: float compares keep their
    // FCMP_SET materialization and only feed the chain as its bool.
    selector.branch_fusions = arena_allocate(arena, MachineX64BranchFusion, function->value_count ? function->value_count : 1);
    selector.fused_dead = arena_allocate(arena, u8, function->value_count ? function->value_count : 1);
    selector.atomic_success_registers = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* local_store_ordinals = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        selector.branch_fusions[value_index] = (MachineX64BranchFusion){0};
        selector.fused_dead[value_index] = 0;
        selector.atomic_success_registers[value_index] = UINT32_MAX;
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
            u32 condition = 0;
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
                    u32 member_condition = machine_x64_condition_from_comparison(member->binary_operation);
                    IrTypeId member_operand_type_id = function->values[member->operands[0].value].canonical_type;
                    if (!member_condition || !machine_x64_type_is_scalar_register(ir_type_from_id(&program->types, member_operand_type_id)))
                    {
                        break;
                    }
                    if (member_condition == MACHINE_X64_CONDITION_EQUAL || member_condition == MACHINE_X64_CONDITION_NOT_EQUAL)
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
                            negate ^= member_condition == MACHINE_X64_CONDITION_EQUAL;
                            chain_value = member->operands[1 - zero_side].value;
                            continue;
                        }
                    }
                    absorbed_members[absorbed_count++] = chain_value;
                    innermost_ordinal = value_def_ordinals[chain_value];
                    read_left = member->operands[0].value;
                    read_right = member->operands[1].value;
                    condition = member_condition ^ negate;
                    wide = machine_x64_type_is_64_bit(program, member_operand_type_id);
                    break;
                }
                if (member->opcode == IR_OPCODE_CAST && member->operand_count >= 1 && member->operands[0].value < function->value_count &&
                    (member->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                     member->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND || member->conversion_operation == IR_CONVERSION_IDENTITY) &&
                    machine_x64_type_is_scalar_register(
                        ir_type_from_id(&program->types, function->values[member->operands[0].value].canonical_type)))
                {
                    absorbed_members[absorbed_count++] = chain_value;
                    innermost_ordinal = value_def_ordinals[chain_value];
                    chain_value = member->operands[0].value;
                    continue;
                }
                if (member->opcode == IR_OPCODE_UNARY && member->unary_operation == IR_UNARY_BOOLEAN_NOT && member->operand_count >= 1 &&
                    member->operands[0].value < function->value_count &&
                    machine_x64_type_is_scalar_register(
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
                // Truthiness terminal: TEST64 of the register content is
                // exact because sub-64-bit values sit extended with clean
                // upper bits, matching the CMP-against-zero it replaces.
                if (chain_value >= function->value_count ||
                    !machine_x64_type_is_scalar_register(ir_type_from_id(&program->types, function->values[chain_value].canonical_type)))
                {
                    continue;
                }
                read_left = chain_value;
                condition = MACHINE_X64_CONDITION_NOT_EQUAL ^ negate;
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
            selector.branch_fusions[instruction->operands[0].value] = (MachineX64BranchFusion){
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
        u32 parameter_offset = selector.builder.block_parameters.total_count;
        for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
        {
            machine_builder_block_parameter(&selector.builder,
                                            (MachineBlockParameter){.virtual_register = selector.value_virtual_registers[parameter->value.value]});
        }
        if (block_index == 0)
        {
            if (selector.va_register_save_slot != UINT32_MAX)
            {
                machine_x64_select_row(&selector, (MachineInstruction){
                                                          .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, selector.va_register_save_slot)},
                                                          .opcode = MACHINE_X64_VA_SAVE,
                                                      });
            }
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
                        if (shape->vector)
                        {
                            // A stack vector parameter reads its 64-aligned
                            // incoming eightbytes whole: the area address is
                            // rbp plus the saved-frame-and-return sixteen,
                            // and the load's free register pick is safe here
                            // because every register vector parameter was
                            // captured in the integer pass below.
                            u32 vector_parameter_register = selector.value_virtual_registers[argument_value];
                            if (vector_parameter_register == UINT32_MAX)
                            {
                                machine_x64_reject(&selector, IR_OPCODE_ARGUMENT);
                                break;
                            }
                            u32 area_register = machine_x64_synthesize_register(&selector);
                            machine_x64_select_row(&selector, (MachineInstruction){
                                                                  .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, area_register),
                                                                               machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RBP)},
                                                                  .payload = 16 + (u32)parameter_placement->first_stack_part * 8,
                                                                  .opcode = MACHINE_X64_LEA_OFFSET,
                                                              });
                            u32 vector_incoming_row =
                                machine_x64_select_row(&selector, (MachineInstruction){
                                                                      .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER,
                                                                                                    vector_parameter_register),
                                                                                   machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, area_register)},
                                                                      .opcode = MACHINE_X64_VLOAD_PTR,
                                                                  });
                            machine_x64_define(&selector, vector_parameter_register, vector_incoming_row);
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
                    if (shape->vector)
                    {
                        // Register vector parameters capture in the integer
                        // pass: the copy binds in place on its incoming ZMM,
                        // so no free pick exists before every argument
                        // register — either file — has been read.
                        if (float_pass)
                        {
                            continue;
                        }
                        u32 vector_argument_register = selector.value_virtual_registers[argument_value];
                        if (vector_argument_register == UINT32_MAX)
                        {
                            machine_x64_reject(&selector, IR_OPCODE_ARGUMENT);
                            break;
                        }
                        u32 vector_capture_row =
                            machine_x64_select_row(&selector, (MachineInstruction){
                                                                  .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, vector_argument_register),
                                                                               machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER,
                                                                                                MACHINE_X64_ZMM0 + next_float)},
                                                                  .opcode = MACHINE_X64_VMOV_RR,
                                                              });
                        machine_x64_define(&selector, vector_argument_register, vector_capture_row);
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
            if (!machine_x64_select_instruction(&selector, instruction))
            {
                machine_x64_reject(&selector, instruction->opcode);
                break;
            }
        }
        machine_builder_block_end(&selector.builder, (MachineBlock){.parameter_offset = parameter_offset, .parameter_count = (u16)block->parameter_count});
    }
    if (!selector.supported)
    {
        result.failed_opcode = selector.failed_opcode;
        return result;
    }
    result.function = machine_function_builder_finish(arena, &selector.builder);
    for (u32 destination_block = 0; destination_block < function->block_count; destination_block += 1)
    {
        IrBlock* destination = function->blocks + destination_block;
        for (IrPredecessor* predecessor = destination->first_predecessor; predecessor; predecessor = predecessor->next)
        {
            u32 copy_offset = result.function.edge_copy_source_count;
            for (IrBlockParameter* parameter = destination->first_parameter; parameter; parameter = parameter->next)
            {
                IrIncoming* incoming = parameter->first_incoming;
                while (incoming && incoming->predecessor.value != predecessor->block.value)
                {
                    incoming = incoming->next;
                }
                if (!incoming || incoming->value.value >= function->value_count || selector.value_virtual_registers[incoming->value.value] == UINT32_MAX)
                {
                    return (MachineSelectResult){.failed_opcode = IR_OPCODE_COUNT};
                }
                MachineRef* grown = arena_allocate(arena, MachineRef, result.function.edge_copy_source_count + 1u);
                if (result.function.edge_copy_source_count)
                {
                    memcpy(grown, result.function.edge_copy_sources, (u64)result.function.edge_copy_source_count * sizeof(MachineRef));
                }
                grown[result.function.edge_copy_source_count++] =
                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, selector.value_virtual_registers[incoming->value.value]);
                result.function.edge_copy_sources = grown;
            }
            MachineEdge* grown_edges = arena_allocate(arena, MachineEdge, result.function.edge_count + 1u);
            if (result.function.edge_count)
            {
                memcpy(grown_edges, result.function.edges, (u64)result.function.edge_count * sizeof(MachineEdge));
            }
            grown_edges[result.function.edge_count++] = (MachineEdge){.source_block = predecessor->block.value,
                                                                      .destination_block = destination_block,
                                                                      .copy_offset = copy_offset,
                                                                      .copy_count = (u16)destination->parameter_count};
            result.function.edges = grown_edges;
        }
    }
    result.function.target = &machine_x86_64_description;
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
    result.function.switch_cases = arena_allocate(arena, MachineSwitchCase, selector.switch_cases.total_count);
    result.function.switch_case_count = selector.switch_cases.total_count;
    machine_stream_flatten(&selector.switch_cases, result.function.switch_cases);
    result.function.va_args = arena_allocate(arena, MachineVaArg, selector.va_args.total_count);
    result.function.va_arg_count = selector.va_args.total_count;
    machine_stream_flatten(&selector.va_args, result.function.va_args);
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
    result.selection_counters = selector.selection_counters;
    return result;
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

// These exact-form keys are generated-snapshot identities, not architectural
// opcode numbers.  Keep the stable hash beside the row ID so a regenerated
// metadata snapshot cannot silently route a machine row through another form.
enum
{
    MACHINE_X64_MFENCE_EXACT_FORM_ID = 9610u,
    MACHINE_X64_INT3_EXACT_FORM_ID = 10027u,
    MACHINE_X64_MOV_RR_EXACT_FORM_ID = 9842u,
    MACHINE_X64_MOVSX8_RR_EXACT_FORM_ID = 10686u,
    MACHINE_X64_MOVSX16_RR_EXACT_FORM_ID = 10687u,
    MACHINE_X64_MOVSX32_RR_EXACT_FORM_ID = 9742u,
    MACHINE_X64_MOVZX8_RR_EXACT_FORM_ID = 10294u,
    MACHINE_X64_MOVZX16_RR_EXACT_FORM_ID = 10295u,
    MACHINE_X64_ADD_RR_EXACT_FORM_ID = 9620u,
    MACHINE_X64_SUB_RR_EXACT_FORM_ID = 9688u,
    MACHINE_X64_AND_RR_EXACT_FORM_ID = 9675u,
    MACHINE_X64_OR_RR_EXACT_FORM_ID = 9634u,
    MACHINE_X64_XOR_RR_EXACT_FORM_ID = 9701u,
    MACHINE_X64_IMUL_RR_EXACT_FORM_ID = 10673u,
    MACHINE_X64_NEG_R_EXACT_FORM_ID = 9462u,
    MACHINE_X64_NOT_R_EXACT_FORM_ID = 9459u,
    MACHINE_X64_BSF_RR_EXACT_FORM_ID = 8688u,
    MACHINE_X64_BSR_RR_EXACT_FORM_ID = 8105u,
    MACHINE_X64_CMP_RR_EXACT_FORM_ID = 9712u,
    MACHINE_X64_TEST_RR_EXACT_FORM_ID = 9832u,
    MACHINE_X64_POPCNT_RR_EXACT_FORM_ID = 10846u,
    MACHINE_X64_SHIFT_LEFT_R_EXACT_FORM_ID = 9428u,
    MACHINE_X64_SHIFT_ARITHMETIC_R_EXACT_FORM_ID = 9434u,
    MACHINE_X64_SHIFT_RIGHT_R_EXACT_FORM_ID = 9430u,
    MACHINE_X64_JMP_EXACT_FORM_ID = 10060u,
    MACHINE_X64_LEA_SYMBOL_EXACT_FORM_ID = 9849u,
    MACHINE_X64_MOVQ_TO_XMM_EXACT_FORM_ID = 10574u,
    MACHINE_X64_MOVQ_FROM_XMM_EXACT_FORM_ID = 10575u,
    MACHINE_X64_PUSH_REGISTER_EXACT_FORM_ID = 9722u,
    MACHINE_X64_ADD_RSP_EXACT_FORM_ID = 9270u,
};

// x86-64 requires SSE2, so the MFENCE exact row always receives the target's
// architectural baseline gate.  INT3 is BASE and deliberately carries no
// optional feature names.
BUSTER_GLOBAL_LOCAL String8 const machine_x64_sse2_features[] = {S8("sse2")};
BUSTER_GLOBAL_LOCAL String8 const machine_x64_popcnt_features[] = {S8("popcnt")};

BUSTER_GLOBAL_LOCAL void machine_x64_emit8(MachineX64Encoder* encoder, u8 byte);

enum
{
    MACHINE_X64_EXACT_RECIPE_FLAG_SELF_COPY_NOOP = 1u << 0,
    MACHINE_X64_EXACT_RECIPE_FLAG_BRANCH_FIXUP = 1u << 1,
    MACHINE_X64_EXACT_RECIPE_FLAG_CALL_SITE = 1u << 2,
};

typedef enum MachineX64ExactOperandProjection
{
    MACHINE_X64_EXACT_OPERAND_GPR,
    MACHINE_X64_EXACT_OPERAND_XMM_PAYLOAD,
    MACHINE_X64_EXACT_OPERAND_FIXED_RSP,
    MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD,
    MACHINE_X64_EXACT_OPERAND_RELATIVE_ZERO,
    MACHINE_X64_EXACT_OPERAND_RIP_MEMORY_ZERO,
    MACHINE_X64_EXACT_OPERAND_PROJECTION_COUNT,
} MachineX64ExactOperandProjection;

// The metadata plan cache is keyed by unique durable form keys rather than
// by DIRECT recipe rows: width variants and other projections share one
// immutable normalized form/pattern plan.  Keep the identity compact in the
// row-to-plan map below; no plan pointer or callback enters MachineInstruction.
typedef enum MachineX64ExactPlanId
{
    MACHINE_X64_EXACT_PLAN_MOV,
    MACHINE_X64_EXACT_PLAN_SX8,
    MACHINE_X64_EXACT_PLAN_SX16,
    MACHINE_X64_EXACT_PLAN_SX32,
    MACHINE_X64_EXACT_PLAN_ZX8,
    MACHINE_X64_EXACT_PLAN_ZX16,
    MACHINE_X64_EXACT_PLAN_ADD,
    MACHINE_X64_EXACT_PLAN_SUB,
    MACHINE_X64_EXACT_PLAN_AND,
    MACHINE_X64_EXACT_PLAN_OR,
    MACHINE_X64_EXACT_PLAN_XOR,
    MACHINE_X64_EXACT_PLAN_IMUL,
    MACHINE_X64_EXACT_PLAN_NEG,
    MACHINE_X64_EXACT_PLAN_NOT,
    MACHINE_X64_EXACT_PLAN_BSF,
    MACHINE_X64_EXACT_PLAN_BSR,
    MACHINE_X64_EXACT_PLAN_POPCNT,
    MACHINE_X64_EXACT_PLAN_JMP,
    MACHINE_X64_EXACT_PLAN_SHL,
    MACHINE_X64_EXACT_PLAN_SAR,
    MACHINE_X64_EXACT_PLAN_SHR,
    MACHINE_X64_EXACT_PLAN_LEA_SYMBOL,
    MACHINE_X64_EXACT_PLAN_MOVQ_TO_XMM,
    MACHINE_X64_EXACT_PLAN_MOVQ_FROM_XMM,
    MACHINE_X64_EXACT_PLAN_PUSH,
    MACHINE_X64_EXACT_PLAN_ADD_RSP,
    MACHINE_X64_EXACT_PLAN_CMP,
    MACHINE_X64_EXACT_PLAN_TEST,
    MACHINE_X64_EXACT_PLAN_MFENCE,
    MACHINE_X64_EXACT_PLAN_INT3,
    MACHINE_X64_EXACT_PLAN_COUNT,
    MACHINE_X64_EXACT_PLAN_INVALID = UINT8_MAX,
} MachineX64ExactPlanId;

typedef struct MachineX64ExactRecipe MachineX64ExactRecipe;
struct MachineX64ExactRecipe
{
    MachineEmitRecipeId recipe;
    X64ExactFormKey key;
    String8 const* features;
    u32 feature_count;
    u8 operand_count;
    u8 flags;
    u8 operand_slots[2];
    u8 operand_kinds[2];
    u16 operand_widths[2];
};

// Flat DIRECT recipe projections.  Each descriptor is keyed by the stable
// MachineEmitRecipeId assigned by machine.c; the operand slots are the
// post-placement machine row slots, in metadata form order (RM/B before
// REG/R for the two-address ALU forms).
BUSTER_GLOBAL_LOCAL MachineX64ExactRecipe const machine_x64_exact_recipe_table[MACHINE_X86_64_EMIT_REGISTRY_DIRECT_COUNT] = {
    [0] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 0),
        .key = {MACHINE_X64_MOV_RR_EXACT_FORM_ID, UINT64_C(0x3ab69ab9d0d06329)},
        .operand_count = 2, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_SELF_COPY_NOOP,
        .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [1] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 1),
        .key = {MACHINE_X64_MOV_RR_EXACT_FORM_ID, UINT64_C(0x3ab69ab9d0d06329)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {32, 32},
    },
    [2] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 2),
        .key = {MACHINE_X64_MOVSX8_RR_EXACT_FORM_ID, UINT64_C(0x60113253679881a6)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 8},
    },
    [3] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 3),
        .key = {MACHINE_X64_MOVSX16_RR_EXACT_FORM_ID, UINT64_C(0x1ea1a0d40c380394)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 16},
    },
    [4] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 4),
        .key = {MACHINE_X64_MOVSX32_RR_EXACT_FORM_ID, UINT64_C(0xacab1d188d386b1e)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 32},
    },
    [5] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 5),
        .key = {MACHINE_X64_MOVZX8_RR_EXACT_FORM_ID, UINT64_C(0xa9d675ab86fb1641)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 8},
    },
    [6] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 6),
        .key = {MACHINE_X64_MOVZX16_RR_EXACT_FORM_ID, UINT64_C(0xb25b807d6d5747b3)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 16},
    },
    [7] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 7),
        .key = {MACHINE_X64_ADD_RR_EXACT_FORM_ID, UINT64_C(0xf0743d28f1fbad54)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [8] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 8),
        .key = {MACHINE_X64_ADD_RR_EXACT_FORM_ID, UINT64_C(0xf0743d28f1fbad54)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [9] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 9),
        .key = {MACHINE_X64_SUB_RR_EXACT_FORM_ID, UINT64_C(0x240ef104bd0b9756)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [10] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 10),
        .key = {MACHINE_X64_SUB_RR_EXACT_FORM_ID, UINT64_C(0x240ef104bd0b9756)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [11] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 11),
        .key = {MACHINE_X64_AND_RR_EXACT_FORM_ID, UINT64_C(0x65889d347cd743a9)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [12] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 12),
        .key = {MACHINE_X64_AND_RR_EXACT_FORM_ID, UINT64_C(0x65889d347cd743a9)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [13] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 13),
        .key = {MACHINE_X64_OR_RR_EXACT_FORM_ID, UINT64_C(0x7f76bb71727dbfaa)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [14] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 14),
        .key = {MACHINE_X64_OR_RR_EXACT_FORM_ID, UINT64_C(0x7f76bb71727dbfaa)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [15] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 15),
        .key = {MACHINE_X64_XOR_RR_EXACT_FORM_ID, UINT64_C(0x915fb23b51f8b7d1)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [16] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 16),
        .key = {MACHINE_X64_XOR_RR_EXACT_FORM_ID, UINT64_C(0x915fb23b51f8b7d1)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [17] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 17),
        .key = {MACHINE_X64_IMUL_RR_EXACT_FORM_ID, UINT64_C(0x8ad1b7f99185b8ea)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [18] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 18),
        .key = {MACHINE_X64_IMUL_RR_EXACT_FORM_ID, UINT64_C(0x8ad1b7f99185b8ea)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [19] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 19),
        .key = {MACHINE_X64_NEG_R_EXACT_FORM_ID, UINT64_C(0x10bbcf6d744b04f9)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {32},
    },
    [20] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 20),
        .key = {MACHINE_X64_NEG_R_EXACT_FORM_ID, UINT64_C(0x10bbcf6d744b04f9)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [21] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 21),
        .key = {MACHINE_X64_NOT_R_EXACT_FORM_ID, UINT64_C(0xbd007ad8742aadfc)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {32},
    },
    [22] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 22),
        .key = {MACHINE_X64_NOT_R_EXACT_FORM_ID, UINT64_C(0xbd007ad8742aadfc)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [23] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 23),
        .key = {MACHINE_X64_BSF_RR_EXACT_FORM_ID, UINT64_C(0x047fe78cb986f7d1)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {32, 32},
    },
    [24] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 24),
        .key = {MACHINE_X64_BSF_RR_EXACT_FORM_ID, UINT64_C(0x047fe78cb986f7d1)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [25] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 25),
        .key = {MACHINE_X64_BSR_RR_EXACT_FORM_ID, UINT64_C(0x7c13390dbcf0139e)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {32, 32},
    },
    [26] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 26),
        .key = {MACHINE_X64_BSR_RR_EXACT_FORM_ID, UINT64_C(0x7c13390dbcf0139e)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [27] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 27),
        .key = {MACHINE_X64_POPCNT_RR_EXACT_FORM_ID, UINT64_C(0xb971fc2e8a6cb1bc)},
        .features = machine_x64_popcnt_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_popcnt_features),
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {32, 32},
    },
    [28] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 28),
        .key = {MACHINE_X64_POPCNT_RR_EXACT_FORM_ID, UINT64_C(0xb971fc2e8a6cb1bc)},
        .features = machine_x64_popcnt_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_popcnt_features),
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [32] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 32),
        .key = {MACHINE_X64_JMP_EXACT_FORM_ID, UINT64_C(0xab9c4b53fce14f6e)},
        .operand_count = 1, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_BRANCH_FIXUP,
        .operand_slots = {0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_RELATIVE_ZERO}, .operand_widths = {32},
    },
    [33] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 33),
        .key = {MACHINE_X64_SHIFT_LEFT_R_EXACT_FORM_ID, UINT64_C(0xe73cf970ddf68ce2)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {32},
    },
    [34] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 34),
        .key = {MACHINE_X64_SHIFT_LEFT_R_EXACT_FORM_ID, UINT64_C(0xe73cf970ddf68ce2)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [35] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 35),
        .key = {MACHINE_X64_SHIFT_ARITHMETIC_R_EXACT_FORM_ID, UINT64_C(0xa64cc273e7d95e92)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {32},
    },
    [36] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 36),
        .key = {MACHINE_X64_SHIFT_ARITHMETIC_R_EXACT_FORM_ID, UINT64_C(0xa64cc273e7d95e92)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [37] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 37),
        .key = {MACHINE_X64_SHIFT_RIGHT_R_EXACT_FORM_ID, UINT64_C(0x1fb65789e301333f)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {32},
    },
    [38] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 38),
        .key = {MACHINE_X64_SHIFT_RIGHT_R_EXACT_FORM_ID, UINT64_C(0x1fb65789e301333f)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [39] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 39),
        .key = {MACHINE_X64_LEA_SYMBOL_EXACT_FORM_ID, UINT64_C(0x0b357f27b62f3409)},
        .operand_count = 2, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_CALL_SITE,
        .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_RIP_MEMORY_ZERO},
        .operand_widths = {64, 64},
    },
    [40] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 40),
        .key = {MACHINE_X64_MOVQ_TO_XMM_EXACT_FORM_ID, UINT64_C(0x7bd465046ab10c4f)},
        .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features),
        .operand_count = 2, .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_PAYLOAD, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {128, 64},
    },
    [41] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 41),
        .key = {MACHINE_X64_MOVQ_FROM_XMM_EXACT_FORM_ID, UINT64_C(0x3698d9bff62c4360)},
        .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features),
        .operand_count = 2, .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_XMM_PAYLOAD}, .operand_widths = {64, 128},
    },
    [43] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 43),
        .key = {MACHINE_X64_PUSH_REGISTER_EXACT_FORM_ID, UINT64_C(0x849cc7557c589605)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [44] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 44),
        .key = {MACHINE_X64_ADD_RSP_EXACT_FORM_ID, UINT64_C(0xcebed63a599832c0)},
        .operand_count = 2, .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_FIXED_RSP, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD}, .operand_widths = {64, 32},
    },
    [29] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 29),
        .key = {MACHINE_X64_CMP_RR_EXACT_FORM_ID, UINT64_C(0xa381563e623950cd)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {32, 32},
    },
    [30] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 30),
        .key = {MACHINE_X64_CMP_RR_EXACT_FORM_ID, UINT64_C(0xa381563e623950cd)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [31] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 31),
        .key = {MACHINE_X64_TEST_RR_EXACT_FORM_ID, UINT64_C(0x29ceea139128c1a6)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [45] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 45),
        .key = {MACHINE_X64_MFENCE_EXACT_FORM_ID, UINT64_C(0x8deb7f066b773767)},
        .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features),
    },
    [46] = {
        .recipe = MACHINE_EMIT_RECIPE_MAKE(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, 46),
        .key = {MACHINE_X64_INT3_EXACT_FORM_ID, UINT64_C(0x11eeb10ba44771fd)},
    },
};

// DIRECT recipe index -> compact unique exact-plan identity.  Index 42 is
// LOAD_INCOMING, intentionally still LEGACY_RAW, and therefore has no plan.
// The table is immutable; the plan values themselves are published only by
// machine_x86_64_exact_prewarm() after every selected key has prepared.
BUSTER_GLOBAL_LOCAL u8 const machine_x64_exact_plan_id_by_recipe[MACHINE_X86_64_EMIT_REGISTRY_DIRECT_COUNT] = {
    [0] = MACHINE_X64_EXACT_PLAN_MOV,
    [1] = MACHINE_X64_EXACT_PLAN_MOV,
    [2] = MACHINE_X64_EXACT_PLAN_SX8,
    [3] = MACHINE_X64_EXACT_PLAN_SX16,
    [4] = MACHINE_X64_EXACT_PLAN_SX32,
    [5] = MACHINE_X64_EXACT_PLAN_ZX8,
    [6] = MACHINE_X64_EXACT_PLAN_ZX16,
    [7] = MACHINE_X64_EXACT_PLAN_ADD,
    [8] = MACHINE_X64_EXACT_PLAN_ADD,
    [9] = MACHINE_X64_EXACT_PLAN_SUB,
    [10] = MACHINE_X64_EXACT_PLAN_SUB,
    [11] = MACHINE_X64_EXACT_PLAN_AND,
    [12] = MACHINE_X64_EXACT_PLAN_AND,
    [13] = MACHINE_X64_EXACT_PLAN_OR,
    [14] = MACHINE_X64_EXACT_PLAN_OR,
    [15] = MACHINE_X64_EXACT_PLAN_XOR,
    [16] = MACHINE_X64_EXACT_PLAN_XOR,
    [17] = MACHINE_X64_EXACT_PLAN_IMUL,
    [18] = MACHINE_X64_EXACT_PLAN_IMUL,
    [19] = MACHINE_X64_EXACT_PLAN_NEG,
    [20] = MACHINE_X64_EXACT_PLAN_NEG,
    [21] = MACHINE_X64_EXACT_PLAN_NOT,
    [22] = MACHINE_X64_EXACT_PLAN_NOT,
    [23] = MACHINE_X64_EXACT_PLAN_BSF,
    [24] = MACHINE_X64_EXACT_PLAN_BSF,
    [25] = MACHINE_X64_EXACT_PLAN_BSR,
    [26] = MACHINE_X64_EXACT_PLAN_BSR,
    [27] = MACHINE_X64_EXACT_PLAN_POPCNT,
    [28] = MACHINE_X64_EXACT_PLAN_POPCNT,
    [29] = MACHINE_X64_EXACT_PLAN_CMP,
    [30] = MACHINE_X64_EXACT_PLAN_CMP,
    [31] = MACHINE_X64_EXACT_PLAN_TEST,
    [32] = MACHINE_X64_EXACT_PLAN_JMP,
    [33] = MACHINE_X64_EXACT_PLAN_SHL,
    [34] = MACHINE_X64_EXACT_PLAN_SHL,
    [35] = MACHINE_X64_EXACT_PLAN_SAR,
    [36] = MACHINE_X64_EXACT_PLAN_SAR,
    [37] = MACHINE_X64_EXACT_PLAN_SHR,
    [38] = MACHINE_X64_EXACT_PLAN_SHR,
    [39] = MACHINE_X64_EXACT_PLAN_LEA_SYMBOL,
    [40] = MACHINE_X64_EXACT_PLAN_MOVQ_TO_XMM,
    [41] = MACHINE_X64_EXACT_PLAN_MOVQ_FROM_XMM,
    [42] = MACHINE_X64_EXACT_PLAN_INVALID,
    [43] = MACHINE_X64_EXACT_PLAN_PUSH,
    [44] = MACHINE_X64_EXACT_PLAN_ADD_RSP,
    [45] = MACHINE_X64_EXACT_PLAN_MFENCE,
    [46] = MACHINE_X64_EXACT_PLAN_INT3,
};

BUSTER_GLOBAL_LOCAL BusterX86MetadataExactPlan machine_x64_exact_plans[MACHINE_X64_EXACT_PLAN_COUNT];
BUSTER_GLOBAL_LOCAL bool machine_x64_exact_plans_ready;

// Prepare the immutable metadata plans once on the serial prewarm thread.
// Width variants and projection variants deliberately share a plan identity;
// workers only read the published value table after the ready bit is set.
BUSTER_F_DECL void machine_x86_64_exact_prewarm(void)
{
    if (machine_x64_exact_plans_ready) return;
    BUSTER_CHECK_SERIAL_INITIALIZATION();

    BusterX86MetadataFormKey keys[MACHINE_X64_EXACT_PLAN_COUNT] = {0};
    bool key_found[MACHINE_X64_EXACT_PLAN_COUNT] = {0};
    BusterX86MetadataExactPlan prepared[MACHINE_X64_EXACT_PLAN_COUNT] = {0};
    for (u32 recipe_index = 0; recipe_index < BUSTER_ARRAY_LENGTH(machine_x64_exact_recipe_table); recipe_index += 1)
    {
        u8 plan_id = machine_x64_exact_plan_id_by_recipe[recipe_index];
        if (plan_id >= MACHINE_X64_EXACT_PLAN_COUNT || key_found[plan_id]) continue;
        keys[plan_id] = machine_x64_exact_recipe_table[recipe_index].key;
        key_found[plan_id] = true;
    }
    for (u32 plan_id = 0; plan_id < MACHINE_X64_EXACT_PLAN_COUNT; plan_id += 1)
    {
        if (!key_found[plan_id] ||
            !buster_x86_metadata_exact_plan_prepare(keys[plan_id], &prepared[plan_id]) ||
            prepared[plan_id].form_id != keys[plan_id].form_id ||
            prepared[plan_id].stable_hash != keys[plan_id].stable_hash)
        {
            // Keep the table unpublished on any stale/missing key.  Exact
            // rows then fail closed in the worker lane rather than silently
            // re-entering the handwritten switch.
            return;
        }
    }
    for (u32 plan_id = 0; plan_id < MACHINE_X64_EXACT_PLAN_COUNT; plan_id += 1)
    {
        machine_x64_exact_plans[plan_id] = prepared[plan_id];
    }
    machine_x64_exact_plans_ready = true;
}


typedef struct MachineX64ExactEmitCounters MachineX64ExactEmitCounters;
struct MachineX64ExactEmitCounters
{
    u32 attempts;
    u32 successes;
    u32 fallbacks;
};

BUSTER_GLOBAL_LOCAL void machine_x64_exact_counters_assign(MachineEncodeResult* result,
                                                            MachineX64ExactEmitCounters counters)
{
    result->exact_attempts = counters.attempts;
    result->exact_successes = counters.successes;
    result->exact_failures = counters.fallbacks;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_exact_gpr_operand(u32 reg, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
        .width = width,
        .reg = {
            .index = (u16)reg,
            .width = width,
            .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_exact_xmm_operand(u32 reg, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
        .width = width,
        .reg = {
            .index = (u16)reg,
            .width = width,
            .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_exact_immediate_operand(s64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
        .width = width,
        .value = value,
        .has_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_exact_relative_operand(s64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
        .width = width,
        .value = value,
        .has_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_exact_rip_memory_operand(void)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = 64,
        .memory = {
            .displacement = 0,
            .address_size = 64,
            .scale = 1,
            .has_displacement = true,
            .rip_relative = true,
        },
    };
}

BUSTER_GLOBAL_LOCAL bool machine_x64_exact_plan_for_recipe(MachineX64ExactRecipe const* descriptor,
                                                            BusterX86MetadataExactPlan* result)
{
    if (!descriptor || !result || !machine_x64_exact_plans_ready) return false;
    MachineEmitRecipeId recipe = descriptor->recipe;
    if (machine_emit_recipe_category(recipe) != MACHINE_EMIT_RECIPE_CATEGORY_DIRECT) return false;
    u16 recipe_index = machine_emit_recipe_index(recipe);
    if (recipe_index >= BUSTER_ARRAY_LENGTH(machine_x64_exact_recipe_table)) return false;
    u8 plan_id = machine_x64_exact_plan_id_by_recipe[recipe_index];
    if (plan_id >= MACHINE_X64_EXACT_PLAN_COUNT) return false;
    BusterX86MetadataExactPlan plan = machine_x64_exact_plans[plan_id];
    if (plan.form_id != descriptor->key.form_id || plan.stable_hash != descriptor->key.stable_hash) return false;
    *result = plan;
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_exact_form(MachineX64Encoder* encoder, X64ExactFormKey key,
                                                     BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
                                                     String8 const* features, u32 feature_count,
                                                     BusterX86MetadataExactPlan plan,
                                                     MachineX64ExactEmitCounters* counters)
{
    if (counters) counters->attempts += 1;
    if (plan.form_id != key.form_id || plan.stable_hash != key.stable_hash)
    {
        if (counters) counters->fallbacks += 1;
        return false;
    }
    u8 exact_bytes[16];
    BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_exact_prevalidated(plan, (BusterX86MetadataExactQuery){
        .key = key,
        .operands = operands,
        .operand_count = operand_count,
        .features = {.names = features, .count = feature_count},
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .include_implicit = false,
        .output = exact_bytes,
        .output_capacity = sizeof(exact_bytes),
        .relocations = 0,
        .relocation_capacity = 0,
    });
    if (emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || emitted.relocation_count != 0 ||
        emitted.byte_count > sizeof(exact_bytes) || encoder->count > encoder->capacity || emitted.byte_count > encoder->capacity - encoder->count)
    {
        if (counters) counters->fallbacks += 1;
        return false;
    }
    for (u32 byte_index = 0; byte_index < emitted.byte_count; byte_index += 1)
    {
        machine_x64_emit8(encoder, exact_bytes[byte_index]);
    }
    if (counters) counters->successes += 1;
    return true;
}

BUSTER_GLOBAL_LOCAL MachineX64ExactRecipe const* machine_x64_exact_recipe_for_opcode(u16 opcode, bool* exact_required)
{
    if (exact_required) *exact_required = false;
    MachineX64EmitRegistryEntry const* registry_entry = machine_x86_64_emit_registry_find((MachineOpcode)opcode);
    if (!registry_entry || registry_entry->producer_status != MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_FORM)
    {
        return 0;
    }
    if (exact_required) *exact_required = true;
    MachineEmitRecipeId recipe = registry_entry->recipe;
    if (machine_emit_recipe_category(recipe) != MACHINE_EMIT_RECIPE_CATEGORY_DIRECT)
    {
        return 0;
    }
    u16 index = machine_emit_recipe_index(recipe);
    if (index >= BUSTER_ARRAY_LENGTH(machine_x64_exact_recipe_table))
    {
        return 0;
    }
    MachineX64ExactRecipe const* descriptor = machine_x64_exact_recipe_table + index;
    return descriptor->recipe == recipe && descriptor->key.stable_hash != 0 ? descriptor : 0;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_exact_recipe(MachineX64Encoder* encoder, MachineX64ExactRecipe const* descriptor,
                                                       u8 const* operand_registers, u32 payload,
                                                       MachineX64ExactEmitCounters* counters)
{
    if ((descriptor->flags & MACHINE_X64_EXACT_RECIPE_FLAG_SELF_COPY_NOOP) &&
        operand_registers[descriptor->operand_slots[0]] == operand_registers[descriptor->operand_slots[1]])
    {
        return true;
    }
    BusterX86MetadataExactPlan plan = {0};
    if (!machine_x64_exact_plan_for_recipe(descriptor, &plan))
    {
        if (counters) counters->attempts += 1;
        if (counters) counters->fallbacks += 1;
        return false;
    }
    // Every active descriptor slot is populated by the projection loop before
    // the metadata query; no inactive slot is consumed by the exact API.
    BusterX86MetadataPhysicalOperand operands[2];
    for (u32 operand_index = 0; operand_index < descriptor->operand_count; operand_index += 1)
    {
        u16 width = descriptor->operand_widths[operand_index];
        switch ((MachineX64ExactOperandProjection)descriptor->operand_kinds[operand_index])
        {
            break;
        case MACHINE_X64_EXACT_OPERAND_GPR:
            operands[operand_index] = machine_x64_exact_gpr_operand(operand_registers[descriptor->operand_slots[operand_index]], width);
            break;
        case MACHINE_X64_EXACT_OPERAND_XMM_PAYLOAD:
            operands[operand_index] = machine_x64_exact_xmm_operand(payload, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_FIXED_RSP:
            operands[operand_index] = machine_x64_exact_gpr_operand(MACHINE_X64_RSP, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD:
            operands[operand_index] = machine_x64_exact_immediate_operand((s64)(s32)payload, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_RELATIVE_ZERO:
            operands[operand_index] = machine_x64_exact_relative_operand(0, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_RIP_MEMORY_ZERO:
            operands[operand_index] = machine_x64_exact_rip_memory_operand();
            break;
        default:
            if (counters) counters->attempts += 1;
            if (counters) counters->fallbacks += 1;
            return false;
        }
    }
    return machine_x64_emit_exact_form(encoder, descriptor->key, operands, descriptor->operand_count,
                                       descriptor->features, descriptor->feature_count, plan, counters);
}

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

// Materializes a constant through the shortest form that reproduces all
// sixty-four result bits: the zero-extending mov r32 when the value fits
// unsigned thirty-two, the sign-extended mov r64 when it fits signed
// thirty-two, the ten-byte movabs otherwise. Every form must leave flags
// alone — rematerializations land between arbitrary rows, including a
// compare and its branch — so zero takes the five-byte mov, never xor.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_immediate(MachineX64Encoder* encoder, u32 reg, u64 value)
{
    if (value <= UINT32_MAX)
    {
        if (reg >= 8)
        {
            machine_x64_emit8(encoder, 0x41);
        }
        machine_x64_emit8(encoder, (u8)(0xb8 | (reg & 7)));
        machine_x64_emit32(encoder, (u32)value);
    }
    else if (value >= UINT64_C(0xffffffff80000000))
    {
        machine_x64_emit8(encoder, (u8)(0x48 | (reg >= 8 ? 0x01 : 0)));
        machine_x64_emit8(encoder, 0xc7);
        machine_x64_emit8(encoder, (u8)(0xc0 | (reg & 7)));
        machine_x64_emit32(encoder, (u32)value);
    }
    else
    {
        machine_x64_emit8(encoder, (u8)(0x48 | (reg >= 8 ? 0x01 : 0)));
        machine_x64_emit8(encoder, (u8)(0xb8 | (reg & 7)));
        machine_x64_emit64(encoder, value);
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_frame_store(MachineX64Encoder* encoder, u32 reg, u32 offset)
{
    machine_x64_emit8(encoder, (u8)(0x48 | (reg >= 8 ? 0x04 : 0)));
    machine_x64_emit8(encoder, 0x89);
    machine_x64_emit_frame_modrm(encoder, reg, offset);
}

// EVEX prefix for the 512-bit vector rows. Unlike the canonical vocabulary's
// fixed low scratches, allocated rows reach the whole ZMM0-31 file, so every
// inverted extension bit is computed from the register numbers: R/R' carry
// the reg field's bits 3 and 4, B carries the rm-or-base bit 3, X doubles as
// the rm bit 4 in register-direct forms — memory bases are general registers
// and never reach 16, so the same expression leaves X high for them — and
// V' carries the vvvv bit 4. All forms are L'L=10; `wide` sets EVEX.W for
// the forms the SDM defines as W1 only (vpaddq/vpsubq), whose W0 encodings
// raise #UD on real hardware.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_evex(MachineX64Encoder* encoder, u8 map, u8 simd_prefix, u8 opcode, u32 reg, u32 vvvv, u32 mask, bool zeroing,
                                               bool wide, u32 rm_or_base)
{
    machine_x64_emit8(encoder, 0x62);
    machine_x64_emit8(encoder, (u8)(((reg & 8) ? 0 : 0x80) | ((rm_or_base & 16) ? 0 : 0x40) | ((rm_or_base & 8) ? 0 : 0x20) | ((reg & 16) ? 0 : 0x10) | map));
    machine_x64_emit8(encoder, (u8)((wide ? 0x80 : 0) | ((~vvvv & 0xf) << 3) | 0x04 | simd_prefix));
    machine_x64_emit8(encoder, (u8)((zeroing ? 0x80 : 0) | 0x40 | ((vvvv & 16) ? 0 : 0x08) | mask));
    machine_x64_emit8(encoder, opcode);
}

// Frame slots take mod=10/disp32: EVEX disp8 is compressed by the operand
// size, and mod=00 with an RBP base means RIP-relative.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_evex_frame(MachineX64Encoder* encoder, u8 map, u8 simd_prefix, u8 opcode, u32 reg, u32 mask, bool zeroing, u32 offset)
{
    machine_x64_emit_evex(encoder, map, simd_prefix, opcode, reg, 0, mask, zeroing, false, 0);
    machine_x64_emit8(encoder, (u8)(0x85 | ((reg & 7) << 3)));
    machine_x64_emit32(encoder, (u32)(0 - (s32)offset));
}

// [base] with the RSP/R12 SIB and RBP/R13 displacement detours; the
// displacement-carrying form is disp32 for the compression reason above.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_evex_indirect(MachineX64Encoder* encoder, u8 map, u8 simd_prefix, u8 opcode, u32 reg, u32 mask, bool zeroing,
                                                        u32 base)
{
    machine_x64_emit_evex(encoder, map, simd_prefix, opcode, reg, 0, mask, zeroing, false, base);
    u32 base_low = base & 7;
    if (base_low == 4)
    {
        machine_x64_emit8(encoder, (u8)(((reg & 7) << 3) | 4));
        machine_x64_emit8(encoder, 0x24);
    }
    else if (base_low == 5)
    {
        machine_x64_emit8(encoder, (u8)(0x80 | ((reg & 7) << 3) | 5));
        machine_x64_emit32(encoder, 0);
    }
    else
    {
        machine_x64_emit8(encoder, (u8)(((reg & 7) << 3) | base_low));
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_evex_register(MachineX64Encoder* encoder, u8 map, u8 simd_prefix, u8 opcode, u32 reg, u32 vvvv, u32 mask,
                                                        bool zeroing, bool wide, u32 rm)
{
    machine_x64_emit_evex(encoder, map, simd_prefix, opcode, reg, vvvv, mask, zeroing, wide, rm);
    machine_x64_emit8(encoder, (u8)(0xc0 | ((reg & 7) << 3) | (rm & 7)));
}

// KMOVQ between the k staging register and a general register:
// VEX.L0.F2.0F.W1 92 (from general) / 93 (to general). The k file is
// invisible to every allocator, so k1 is always free to stage through.
#define MACHINE_X64_STAGE_MASK 1u

BUSTER_GLOBAL_LOCAL void machine_x64_emit_kmovq_from_general(MachineX64Encoder* encoder, u32 mask_register, u32 general)
{
    machine_x64_emit8(encoder, 0xc4);
    machine_x64_emit8(encoder, (u8)(0xc1 | ((general & 8) ? 0 : 0x20)));
    machine_x64_emit8(encoder, 0xfb);
    machine_x64_emit8(encoder, 0x92);
    machine_x64_emit8(encoder, (u8)(0xc0 | ((mask_register & 7) << 3) | (general & 7)));
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_kmovq_to_general(MachineX64Encoder* encoder, u32 general, u32 mask_register)
{
    machine_x64_emit8(encoder, 0xc4);
    machine_x64_emit8(encoder, (u8)(0x61 | ((general & 8) ? 0 : 0x80)));
    machine_x64_emit8(encoder, 0xfb);
    machine_x64_emit8(encoder, 0x93);
    machine_x64_emit8(encoder, (u8)(0xc0 | ((general & 7) << 3) | (mask_register & 7)));
}

// The unaligned 64-byte moves the whole vector subset travels through,
// mirroring the canonical vmovdqu8 forms. `zmm` is the ZMM number, already
// rebased out of the unified register file.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_vector_frame(MachineX64Encoder* encoder, u32 zmm, bool store, u32 offset)
{
    machine_x64_emit_evex_frame(encoder, 1, 3, store ? 0x7f : 0x6f, zmm, 0, false, offset);
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_vector_copy(MachineX64Encoder* encoder, u32 destination_zmm, u32 source_zmm)
{
    machine_x64_emit_evex_register(encoder, 1, 3, 0x6f, destination_zmm, 0, 0, false, false, source_zmm);
}

// [base + disp] addressing for allocated bases. The RSP/R12 encodings
// demand a SIB byte and the RBP/R13 encodings have no displacement-free
// form, so those low codes take the SIB and disp8 detours.
BUSTER_GLOBAL_LOCAL void machine_x64_emit_memory_modrm(MachineX64Encoder* encoder, u32 reg, u32 base, u32 displacement)
{
    u32 base_low = base & 7;
    u32 mod = displacement                                    ? displacement <= INT8_MAX ? 0x40u : 0x80u
              : base_low == 5                                 ? 0x40u
                                                              : 0x00u;
    machine_x64_emit8(encoder, (u8)(mod | ((reg & 7) << 3) | base_low));
    if (base_low == 4)
    {
        machine_x64_emit8(encoder, 0x24);
    }
    if (mod == 0x40)
    {
        machine_x64_emit8(encoder, (u8)displacement);
    }
    else if (mod == 0x80)
    {
        machine_x64_emit32(encoder, displacement);
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_memory_load_sized(MachineX64Encoder* encoder, u32 destination, u32 base, u32 displacement, u32 size)
{
    u8 rex = (u8)(0x40 | (destination >= 8 ? 0x04 : 0) | (base >= 8 ? 0x01 : 0));
    if (size == 1 || size == 2)
    {
        if (size == 2)
        {
            machine_x64_emit8(encoder, 0x66);
        }
        if (rex != 0x40)
        {
            machine_x64_emit8(encoder, rex);
        }
        machine_x64_emit8(encoder, 0x0f);
        machine_x64_emit8(encoder, size == 1 ? 0xb6 : 0xb7);
    }
    else
    {
        if (size == 8)
        {
            rex |= 0x08;
        }
        if (rex != 0x40)
        {
            machine_x64_emit8(encoder, rex);
        }
        machine_x64_emit8(encoder, 0x8b);
    }
    machine_x64_emit_memory_modrm(encoder, destination, base, displacement);
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_memory_store_sized(MachineX64Encoder* encoder, u32 source, u32 base, u32 displacement, u32 size)
{
    u8 rex = (u8)(0x40 | (source >= 8 ? 0x04 : 0) | (base >= 8 ? 0x01 : 0) | (size == 8 ? 0x08 : 0));
    if (size == 2)
    {
        machine_x64_emit8(encoder, 0x66);
    }
    // AH/BH/CH/DH are not usable with a REX prefix; all constrained VA
    // scratch registers are the low-byte-safe RAX/RCX/RDX/R8+ family.
    if (rex != 0x40 || (size == 1 && (source == MACHINE_X64_RSI || source == MACHINE_X64_RDI)))
    {
        machine_x64_emit8(encoder, rex);
    }
    machine_x64_emit8(encoder, size == 1 ? 0x88 : 0x89);
    machine_x64_emit_memory_modrm(encoder, source, base, displacement);
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_frame_store_sized(MachineX64Encoder* encoder, u32 source, u32 offset, u32 size)
{
    u8 rex = (u8)(0x40 | (source >= 8 ? 0x04 : 0) | (size == 8 ? 0x08 : 0));
    if (size == 2)
    {
        machine_x64_emit8(encoder, 0x66);
    }
    if (rex != 0x40 || (size == 1 && (source == MACHINE_X64_RSI || source == MACHINE_X64_RDI)))
    {
        machine_x64_emit8(encoder, rex);
    }
    machine_x64_emit8(encoder, size == 1 ? 0x88 : 0x89);
    machine_x64_emit_frame_modrm(encoder, source, offset);
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_add_immediate_sized(MachineX64Encoder* encoder, u32 reg, u32 value, bool wide)
{
    u8 rex = (u8)(0x40 | (wide ? 0x08 : 0) | (reg >= 8 ? 0x01 : 0));
    machine_x64_emit8(encoder, rex);
    machine_x64_emit8(encoder, value <= INT8_MAX ? 0x83 : 0x81);
    machine_x64_emit8(encoder, (u8)(0xc0 | (reg & 7)));
    if (value <= INT8_MAX)
    {
        machine_x64_emit8(encoder, (u8)value);
    }
    else
    {
        machine_x64_emit32(encoder, value);
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_emit_compare_immediate32(MachineX64Encoder* encoder, u32 reg, u32 value)
{
    if (reg >= 8)
    {
        machine_x64_emit8(encoder, 0x41);
    }
    machine_x64_emit8(encoder, 0x81);
    machine_x64_emit8(encoder, (u8)(0xf8 | (reg & 7)));
    machine_x64_emit32(encoder, value);
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
        case MACHINE_X64_STACK_ALLOCATE:
            // Alignment, the page-probe loop, the final subtract/touch, and
            // the RSP result are substantially larger than a normal row.
            capacity64 += 64;
            break;
        case MACHINE_X64_VA_SAVE:
            // Six GP stores plus eight XMM stores, each with a disp32 frame
            // address and (for XMM) the legacy SSE prefix. Keep ample room
            // for the fixed prologue and allocator edits around the row.
            capacity64 += 320;
            break;
        case MACHINE_X64_VA_ARG:
            // Mixed/aggregate values may emit two bounds checks, one load
            // per eightbyte, frame stores, and the complete overflow copy.
            capacity64 += 640;
            break;
        case MACHINE_X64_FCMP_SET:
            capacity64 += 40;
            break;
        case MACHINE_X64_ATOMIC_RMW:
            capacity64 += 48;
            break;
        case MACHINE_X64_ATOMIC_CMPXCHG16:
            capacity64 += 96;
            break;
        default:
            capacity64 += 24;
        }
    }
    // Vector spill and reload edits are ten bytes (EVEX plus disp32).
    capacity64 += (u64)placement->edit_count * 12;
    if (capacity64 > UINT32_MAX)
    {
        return result;
    }
    MachineX64Encoder encoder = {
        .bytes = arena_allocate(arena, u8, capacity64),
        .capacity = (u32)capacity64,
    };
    MachineX64ExactEmitCounters exact_counters = {0};
    // Functions that touch the vector file end their AVX-512 regions with
    // vzeroupper at every call and return, matching the transition hygiene
    // the canonical vector paths keep; all vector values are dead at those
    // points — the scan flushed them — and the low XMM halves the float ABI
    // uses survive vzeroupper untouched. ZMM16-31 sit outside vzeroupper's
    // reach entirely (it clears bits 128+ of the VEX-visible ymm0-15 only,
    // and the high file has no legacy or VEX encodings to transition
    // against), so the policy is unchanged by the widened file — the high
    // registers still flush at calls through the caller-saved contract.
    bool function_has_vector = false;
    for (u32 vector_scan = 0; vector_scan < function->virtual_register_count; vector_scan += 1)
    {
        function_has_vector |= function->virtual_registers[vector_scan].register_class == MACHINE_REGISTER_CLASS_VECTOR;
    }
    MachineBuilderStream fixups;
    machine_stream_initialize(&fixups, sizeof(MachineX64BranchFixup));
    MachineBuilderStream call_sites;
    machine_stream_initialize(&call_sites, sizeof(MachineCallSite));
    result.block_offsets = arena_allocate(arena, u32, function->block_count);
    result.row_offsets = arena_allocate(arena, u32, function->instruction_count ? function->instruction_count : 1);
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
    if (placement->callee_saved_mask & (1u << MACHINE_X64_R12))
    {
        machine_x64_emit8(&encoder, 0x41);
        machine_x64_emit8(&encoder, 0x54);
    }
    if (placement->callee_saved_mask & (1u << MACHINE_X64_R13))
    {
        machine_x64_emit8(&encoder, 0x41);
        machine_x64_emit8(&encoder, 0x55);
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
    // The stack allocation mirrors the canonical chunked form: at most a
    // page per subtract with a probe touch after each, so a frame larger
    // than the guard page cannot skip it.
    u32 frame_remaining = placement->frame_size;
    while (frame_remaining)
    {
        u32 frame_chunk = BUSTER_MIN(frame_remaining, 4096u);
        machine_x64_emit8(&encoder, 0x48);
        machine_x64_emit8(&encoder, frame_chunk <= INT8_MAX ? 0x83 : 0x81);
        machine_x64_emit8(&encoder, 0xec);
        if (frame_chunk <= INT8_MAX)
        {
            machine_x64_emit8(&encoder, (u8)frame_chunk);
        }
        else
        {
            machine_x64_emit32(&encoder, frame_chunk);
        }
        machine_x64_emit8(&encoder, 0xf6);
        machine_x64_emit8(&encoder, 0x04);
        machine_x64_emit8(&encoder, 0x24);
        machine_x64_emit8(&encoder, 0);
        frame_remaining -= frame_chunk;
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
            result.row_offsets[instruction_index] = encoder.count;
            MachinePoint before = machine_point_make(instruction_index, MACHINE_POINT_BEFORE);
            while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == before)
            {
                MachineEdit* edit = placement->edits + edit_cursor;
                // Vector-file locations (the unified indices past the
                // general file) move through the 64-byte vmovdqu8 forms;
                // rematerializations never target them because vector
                // definitions are never constant materializations.
                if (edit->kind == MACHINE_EDIT_SPILL)
                {
                    if (edit->location >= MACHINE_X64_ZMM0)
                    {
                        machine_x64_emit_vector_frame(&encoder, edit->location - MACHINE_X64_ZMM0, true, placement->virtual_register_offsets[edit->subject]);
                    }
                    else
                    {
                        machine_x64_emit_frame_store(&encoder, edit->location, placement->virtual_register_offsets[edit->subject]);
                    }
                }
                else if (edit->kind == MACHINE_EDIT_COPY)
                {
                    if (edit->location >= MACHINE_X64_ZMM0)
                    {
                        machine_x64_emit_vector_copy(&encoder, edit->location - MACHINE_X64_ZMM0, edit->subject - MACHINE_X64_ZMM0);
                    }
                    else
                    {
                        machine_x64_emit_rr(&encoder, true, false, 0x89, edit->subject, edit->location);
                    }
                }
                else if (edit->kind == MACHINE_EDIT_REMATERIALIZE)
                {
                    machine_x64_emit_immediate(&encoder, edit->location, function->immediates[edit->subject]);
                }
                else if (edit->location >= MACHINE_X64_ZMM0)
                {
                    machine_x64_emit_vector_frame(&encoder, edit->location - MACHINE_X64_ZMM0, false, placement->virtual_register_offsets[edit->subject]);
                }
                else
                {
                    machine_x64_emit_frame_load(&encoder, edit->location, placement->virtual_register_offsets[edit->subject]);
                }
                edit_cursor += 1;
            }
            bool exact_required = false;
            MachineX64ExactRecipe const* exact_recipe = machine_x64_exact_recipe_for_opcode(instruction->opcode, &exact_required);
            if (exact_required)
            {
                u32 exact_start = encoder.count;
                bool exact_emitted = exact_recipe &&
                                     machine_x64_emit_exact_recipe(&encoder, exact_recipe, operand_registers, instruction->payload, &exact_counters);
                if (!exact_recipe)
                {
                    // A registry row marked EXACT_FORM is itself an exact
                    // attempt even when its descriptor projection is absent
                    // or stale. Count that fail-closed event so the
                    // per-function invariant remains attempts = successes +
                    // failures while refusing to re-enter the legacy switch.
                    exact_counters.attempts += 1;
                    exact_counters.fallbacks += 1;
                }
                if (!exact_emitted)
                {
                    // Migrated DIRECT rows have no handwritten byte fallback:
                    // an exact-form failure invalidates this result so the
                    // caller's existing codegen fallback accounting remains
                    // authoritative.
                    encoder.overflow = true;
                }
                else if (exact_recipe)
                {
                    // Relative and symbolic forms deliberately query the
                    // metadata encoder with a neutral zero displacement. The
                    // existing machine fixup/call-site streams remain the
                    // owner of target resolution, preserving their canonical
                    // offsets and relocation semantics.
                    if (exact_recipe->flags & MACHINE_X64_EXACT_RECIPE_FLAG_BRANCH_FIXUP)
                    {
                        MachineX64BranchFixup* fixup = (MachineX64BranchFixup*)machine_stream_append(arena, &fixups);
                        *fixup = (MachineX64BranchFixup){
                            .patch_offset = exact_start + 1,
                            .block = machine_ref_payload(instruction->operands[0]),
                        };
                    }
                    if (exact_recipe->flags & MACHINE_X64_EXACT_RECIPE_FLAG_CALL_SITE)
                    {
                        MachineCallSite* site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                        *site = (MachineCallSite){
                            .code_offset = exact_start + 3,
                            .target = instruction->payload,
                        };
                    }
                }
            }
            else
            {
                switch (instruction->opcode)
                {
                break;
            case MACHINE_X64_MOV_RI:
                machine_x64_emit_immediate(&encoder, operand_registers[0], function->immediates[machine_ref_payload(instruction->operands[1])]);
                break;
            case MACHINE_X64_ADD64_IMM:
            {
                u32 reg = operand_registers[0];
                u64 value = function->immediates[machine_ref_payload(instruction->operands[1])];
                machine_x64_emit8(&encoder, (u8)(0x48 | (reg >= 8 ? 0x01 : 0)));
                machine_x64_emit8(&encoder, value <= INT8_MAX ? 0x83 : 0x81);
                machine_x64_emit8(&encoder, (u8)(0xc0 | (reg & 7)));
                if (value <= INT8_MAX)
                {
                    machine_x64_emit8(&encoder, (u8)value);
                }
                else
                {
                    machine_x64_emit32(&encoder, (u32)value);
                }
            }
            break;
            case MACHINE_X64_IMUL64_RRI:
            {
                u32 destination = operand_registers[0];
                u32 source = operand_registers[1];
                u64 value = function->immediates[machine_ref_payload(instruction->operands[2])];
                machine_x64_emit8(&encoder, (u8)(0x48 | (destination >= 8 ? 0x04 : 0) | (source >= 8 ? 0x01 : 0)));
                machine_x64_emit8(&encoder, value <= INT8_MAX ? 0x6b : 0x69);
                machine_x64_emit8(&encoder, machine_x64_modrm_register(destination, source));
                if (value <= INT8_MAX)
                {
                    machine_x64_emit8(&encoder, (u8)value);
                }
                else
                {
                    machine_x64_emit32(&encoder, (u32)value);
                }
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
                machine_x64_emit_memory_modrm(&encoder, destination, address, 0);
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
                machine_x64_emit_memory_modrm(&encoder, value_register, address, 0);
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
                // No vzeroupper over a live 512-bit return: it would zero
                // bits 128+ of the ZMM0 the caller is about to read.
                if (function_has_vector && !(instruction->flags & MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE))
                {
                    machine_x64_emit8(&encoder, 0xc5);
                    machine_x64_emit8(&encoder, 0xf8);
                    machine_x64_emit8(&encoder, 0x77);
                }
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
                    if (placement->callee_saved_mask & (1u << MACHINE_X64_R13))
                    {
                        machine_x64_emit8(&encoder, 0x41);
                        machine_x64_emit8(&encoder, 0x5d);
                    }
                    if (placement->callee_saved_mask & (1u << MACHINE_X64_R12))
                    {
                        machine_x64_emit8(&encoder, 0x41);
                        machine_x64_emit8(&encoder, 0x5c);
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
                // No vzeroupper over staged ZMM arguments: the callee reads
                // them whole, exactly like the canonical path, whose lazy
                // vzeroupper fires before its call staging rather than after.
                if (function_has_vector && !(instruction->flags & MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE))
                {
                    machine_x64_emit8(&encoder, 0xc5);
                    machine_x64_emit8(&encoder, 0xf8);
                    machine_x64_emit8(&encoder, 0x77);
                }
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
                if (function_has_vector && !(instruction->flags & MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE))
                {
                    machine_x64_emit8(&encoder, 0xc5);
                    machine_x64_emit8(&encoder, 0xf8);
                    machine_x64_emit8(&encoder, 0x77);
                }
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
                // The payload is a byte offset into the slot, so a member
                // address needs no separate add.
                machine_x64_emit8(&encoder, (u8)(0x48 | (operand_registers[0] >= 8 ? 0x04 : 0)));
                machine_x64_emit8(&encoder, 0x8d);
                machine_x64_emit_frame_modrm(&encoder, operand_registers[0],
                                             placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])] - instruction->payload);
            }
            break;
            case MACHINE_X64_LEA_OFFSET:
            {
                u32 destination = operand_registers[0];
                u32 base = operand_registers[1];
                machine_x64_emit8(&encoder, (u8)(0x48 | (destination >= 8 ? 0x04 : 0) | (base >= 8 ? 0x01 : 0)));
                machine_x64_emit8(&encoder, 0x8d);
                machine_x64_emit_memory_modrm(&encoder, destination, base, instruction->payload);
            }
            break;
            case MACHINE_X64_LEA_TLS:
            {
                // mov dest, fs:[0] — the thread pointer — then
                // lea dest, [dest + tpoff] with the displacement patched
                // thread-locally, byte-for-byte the canonical sequence.
                u32 destination = operand_registers[0];
                machine_x64_emit8(&encoder, 0x64);
                machine_x64_emit8(&encoder, (u8)(0x48 | (destination >= 8 ? 0x04 : 0)));
                machine_x64_emit8(&encoder, 0x8b);
                machine_x64_emit8(&encoder, (u8)(0x04 | ((destination & 7) << 3)));
                machine_x64_emit8(&encoder, 0x25);
                machine_x64_emit32(&encoder, 0);
                machine_x64_emit8(&encoder, (u8)(0x48 | (destination >= 8 ? 0x05 : 0)));
                machine_x64_emit8(&encoder, 0x8d);
                machine_x64_emit8(&encoder, (u8)(0x80 | ((destination & 7) << 3) | (destination & 7)));
                if ((destination & 7) == 4)
                {
                    machine_x64_emit8(&encoder, 0x24);
                }
                MachineCallSite* site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                *site = (MachineCallSite){
                    .code_offset = encoder.count,
                    .target = instruction->payload,
                    .is_thread_local = 1,
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
            case MACHINE_X64_CVT_U64_TO_F32:
            case MACHINE_X64_CVT_U64_TO_F64:
            {
                // Canonical branchy form: non-negative converts directly;
                // a set sign bit halves with a sticky rounding bit, then
                // doubles the float. Value in RAX, RCX is the scratch, the
                // f32/f64 bit pattern lands back in RAX.
                bool to_f64 = instruction->opcode == MACHINE_X64_CVT_U64_TO_F64;
                u8 cvt_prefix = to_f64 ? 0xf2 : 0xf3;
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x89);
                machine_x64_emit8(&encoder, 0xc8);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x85);
                machine_x64_emit8(&encoder, 0xc0);
                machine_x64_emit8(&encoder, 0x79);
                machine_x64_emit8(&encoder, 0x17);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x89);
                machine_x64_emit8(&encoder, 0xc1);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0xd1);
                machine_x64_emit8(&encoder, 0xe8);
                machine_x64_emit8(&encoder, 0x83);
                machine_x64_emit8(&encoder, 0xe1);
                machine_x64_emit8(&encoder, 0x01);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x09);
                machine_x64_emit8(&encoder, 0xc8);
                machine_x64_emit8(&encoder, cvt_prefix);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x2a);
                machine_x64_emit8(&encoder, 0xc0);
                machine_x64_emit8(&encoder, cvt_prefix);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x58);
                machine_x64_emit8(&encoder, 0xc0);
                machine_x64_emit8(&encoder, 0xeb);
                machine_x64_emit8(&encoder, 0x05);
                machine_x64_emit8(&encoder, cvt_prefix);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x2a);
                machine_x64_emit8(&encoder, 0xc0);
                machine_x64_emit8(&encoder, 0x66);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x7e);
                machine_x64_emit8(&encoder, 0xc0);
            }
            break;
            case MACHINE_X64_CVT_F32_TO_U64:
            case MACHINE_X64_CVT_F64_TO_U64:
            {
                // Canonical threshold form: values below 2^63 convert
                // directly, larger ones subtract the threshold first and
                // set the top bit after. Pattern arrives in RCX, RCX is
                // also the constant scratch, the integer lands in RAX.
                bool from_f64 = instruction->opcode == MACHINE_X64_CVT_F64_TO_U64;
                u8 cvt_prefix = from_f64 ? 0xf2 : 0xf3;
                machine_x64_emit8(&encoder, 0x66);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x6e);
                machine_x64_emit8(&encoder, 0xc1);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0xb8);
                if (from_f64)
                {
                    machine_x64_emit32(&encoder, 0);
                    machine_x64_emit32(&encoder, 0x43e00000u);
                }
                else
                {
                    machine_x64_emit32(&encoder, 0x5f000000u);
                    machine_x64_emit32(&encoder, 0);
                }
                machine_x64_emit8(&encoder, 0x66);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x6e);
                machine_x64_emit8(&encoder, 0xc8);
                if (from_f64)
                {
                    machine_x64_emit8(&encoder, 0x66);
                }
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x2e);
                machine_x64_emit8(&encoder, 0xc1);
                machine_x64_emit8(&encoder, 0x72);
                machine_x64_emit8(&encoder, 0x18);
                machine_x64_emit8(&encoder, cvt_prefix);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x5c);
                machine_x64_emit8(&encoder, 0xc1);
                machine_x64_emit8(&encoder, cvt_prefix);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x2c);
                machine_x64_emit8(&encoder, 0xc0);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0xb9);
                machine_x64_emit32(&encoder, 0);
                machine_x64_emit32(&encoder, 0x80000000u);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x09);
                machine_x64_emit8(&encoder, 0xc8);
                machine_x64_emit8(&encoder, 0xeb);
                machine_x64_emit8(&encoder, 0x05);
                machine_x64_emit8(&encoder, cvt_prefix);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x2c);
                machine_x64_emit8(&encoder, 0xc0);
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
            case MACHINE_X64_VA_SAVE:
            {
                u32 save_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])];
                static u8 const gp_registers[6] = {
                    MACHINE_X64_RDI, MACHINE_X64_RSI, MACHINE_X64_RDX, MACHINE_X64_RCX, MACHINE_X64_R8, MACHINE_X64_R9,
                };
                for (u32 gp_index = 0; gp_index < BUSTER_ARRAY_LENGTH(gp_registers); gp_index += 1)
                {
                    u32 save_displacement = save_offset - gp_index * 8u;
                    machine_x64_emit8(&encoder, 0x48 | (gp_registers[gp_index] >= 8 ? 0x04 : 0));
                    machine_x64_emit8(&encoder, 0x89);
                    machine_x64_emit_frame_modrm(&encoder, gp_registers[gp_index], save_displacement);
                }
                for (u32 float_index = 0; float_index < 8; float_index += 1)
                {
                    u32 xmm = float_index;
                    u32 save_displacement = save_offset - 48u - float_index * 16u;
                    machine_x64_emit8(&encoder, 0xf2);
                    machine_x64_emit8(&encoder, (u8)(0x0f));
                    machine_x64_emit8(&encoder, 0x11);
                    machine_x64_emit_frame_modrm(&encoder, xmm, save_displacement);
                }
            }
            break;
            case MACHINE_X64_VA_ARG:
            {
                MachineVaArg* metadata = function->va_args + instruction->payload;
                u32 list = operand_registers[0];
                u32 result_register = operand_registers[1];
                u32 result_offset = metadata->result_is_frame ? placement->stack_slot_offsets[metadata->result_slot] : 0;
                // A mixed aggregate can exhaust either class independently.
                // Both class checks branch to the same overflow sequence, so
                // retain every displacement field rather than silently
                // leaving the second Jcc's zero displacement unpatched.
                u32 overflow_patches[2] = {UINT32_MAX, UINT32_MAX};
                u32 overflow_patch_count = 0;
                u32 end_patch = UINT32_MAX;
                bool register_path = metadata->part_count && !metadata->parts[0].is_memory;
                u32 integer_parts = 0;
                u32 float_parts = 0;
                for (u32 part_index = 0; part_index < metadata->part_count; part_index += 1)
                {
                    integer_parts += !metadata->parts[part_index].is_float && !metadata->parts[part_index].is_memory;
                    float_parts += metadata->parts[part_index].is_float && !metadata->parts[part_index].is_memory;
                    register_path &= !metadata->parts[part_index].is_memory;
                }
                if (register_path)
                {
                    if (integer_parts)
                    {
                        machine_x64_emit_memory_load_sized(&encoder, MACHINE_X64_RDX, list, 0, 4);
                        machine_x64_emit_compare_immediate32(&encoder, MACHINE_X64_RDX, 48u - integer_parts * 8u);
                        machine_x64_emit8(&encoder, 0x0f);
                        machine_x64_emit8(&encoder, 0x87); // ja overflow
                        if (overflow_patch_count < BUSTER_ARRAY_LENGTH(overflow_patches))
                        {
                            overflow_patches[overflow_patch_count++] = encoder.count;
                        }
                        machine_x64_emit32(&encoder, 0);
                    }
                    if (float_parts)
                    {
                        machine_x64_emit_memory_load_sized(&encoder, MACHINE_X64_R9, list, 4, 4);
                        machine_x64_emit_compare_immediate32(&encoder, MACHINE_X64_R9, 176u - float_parts * 16u);
                        machine_x64_emit8(&encoder, 0x0f);
                        machine_x64_emit8(&encoder, 0x87);
                        if (overflow_patch_count < BUSTER_ARRAY_LENGTH(overflow_patches))
                        {
                            overflow_patches[overflow_patch_count++] = encoder.count;
                        }
                        machine_x64_emit32(&encoder, 0);
                    }
                    machine_x64_emit_memory_load_sized(&encoder, MACHINE_X64_R8, list, 16, 8);
                    for (u32 part_index = 0; part_index < metadata->part_count; part_index += 1)
                    {
                        MachineVaArgPart* part = metadata->parts + part_index;
                        u32 offset_register = part->is_float ? MACHINE_X64_R9 : MACHINE_X64_RDX;
                        // Keep R8 as the immutable reg_save_area base.  The
                        // previous form accumulated each class cursor into
                        // R8, so a second eightbyte (especially a mixed
                        // GP/FP value) addressed from the first part's
                        // cursor instead of the save-area base.
                        machine_x64_emit_rr(&encoder, true, false, 0x89, MACHINE_X64_R8, MACHINE_X64_R11);
                        machine_x64_emit_rr(&encoder, true, false, 0x01, offset_register, MACHINE_X64_R11);
                        machine_x64_emit_memory_load_sized(&encoder, MACHINE_X64_R10, MACHINE_X64_R11, part->save_offset,
                                                           part->size >= 8 ? 8 : part->size);
                        if (metadata->result_is_frame)
                        {
                            machine_x64_emit_frame_store_sized(&encoder, MACHINE_X64_R10, result_offset - part->value_offset,
                                                               part->size >= 8 ? 8 : part->size);
                        }
                        else
                        {
                            // Scalar VA_ARG values are one part and the
                            // constrained result register is RCX.
                            machine_x64_emit_rr(&encoder, part->size >= 8, false, 0x89, MACHINE_X64_R10, result_register);
                        }
                    }
                    if (integer_parts)
                    {
                        machine_x64_emit_add_immediate_sized(&encoder, MACHINE_X64_RDX, integer_parts * 8u, false);
                        machine_x64_emit_memory_store_sized(&encoder, MACHINE_X64_RDX, list, 0, 4);
                    }
                    if (float_parts)
                    {
                        machine_x64_emit_add_immediate_sized(&encoder, MACHINE_X64_R9, float_parts * 16u, false);
                        machine_x64_emit_memory_store_sized(&encoder, MACHINE_X64_R9, list, 4, 4);
                    }
                    machine_x64_emit8(&encoder, 0xe9);
                    end_patch = encoder.count;
                    machine_x64_emit32(&encoder, 0);
                }
                u32 overflow_start = encoder.count;
                machine_x64_emit_memory_load_sized(&encoder, MACHINE_X64_RDX, list, 8, 8);
                if (metadata->alignment > 8)
                {
                    machine_x64_emit_add_immediate_sized(&encoder, MACHINE_X64_RDX, metadata->alignment - 1, true);
                    machine_x64_emit8(&encoder, 0x48 | (MACHINE_X64_RDX >= 8 ? 0x01 : 0));
                    machine_x64_emit8(&encoder, 0x81);
                    machine_x64_emit8(&encoder, 0xe2);
                    machine_x64_emit32(&encoder, 0 - metadata->alignment);
                }
                if (metadata->result_is_frame)
                {
                    u32 copied = 0;
                    while (copied < metadata->size)
                    {
                        u32 chunk = machine_x64_copy_chunk(metadata->size - copied);
                        machine_x64_emit_memory_load_sized(&encoder, MACHINE_X64_R10, MACHINE_X64_RDX, copied, chunk);
                        machine_x64_emit_frame_store_sized(&encoder, MACHINE_X64_R10, result_offset - copied, chunk);
                        copied += chunk;
                    }
                }
                else
                {
                    machine_x64_emit_memory_load_sized(&encoder, result_register, MACHINE_X64_RDX, 0, metadata->scalar_size >= 8 ? 8 : metadata->scalar_size);
                }
                machine_x64_emit_add_immediate_sized(&encoder, MACHINE_X64_RDX, metadata->stack_size, true);
                machine_x64_emit_memory_store_sized(&encoder, MACHINE_X64_RDX, list, 8, 8);
                u32 end = encoder.count;
                for (u32 patch_index = 0; patch_index < overflow_patch_count; patch_index += 1)
                {
                    u32 overflow_patch = overflow_patches[patch_index];
                    u32 displacement = overflow_start - (overflow_patch + 4);
                    for (u32 byte_index = 0; byte_index < 4; byte_index += 1)
                    {
                        encoder.bytes[overflow_patch + byte_index] = (u8)(displacement >> (byte_index * 8));
                    }
                }
                if (end_patch != UINT32_MAX)
                {
                    u32 displacement = end - (end_patch + 4);
                    for (u32 byte_index = 0; byte_index < 4; byte_index += 1)
                    {
                        encoder.bytes[end_patch + byte_index] = (u8)(displacement >> (byte_index * 8));
                    }
                }
            }
            break;
            case MACHINE_X64_PUSH_FRAME:
            {
                machine_x64_emit8(&encoder, 0xff);
                machine_x64_emit_frame_modrm(&encoder, 6, placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])] -
                                                             instruction->payload);
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
            case MACHINE_X64_STACK_ALLOCATE:
            {
                // The constrained row places the runtime byte count in RCX
                // and receives the resulting RSP in RAX. Keep the count
                // unsigned, round it exactly as the canonical emitter does,
                // and touch every page before the final residual subtract so
                // guard-page stacks cannot be skipped.
                u32 stack_alignment = instruction->payload;
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x81);
                machine_x64_emit8(&encoder, 0xc1); // add rcx, alignment - 1
                machine_x64_emit32(&encoder, stack_alignment - 1);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x81);
                machine_x64_emit8(&encoder, 0xe1); // and rcx, -alignment
                machine_x64_emit32(&encoder, 0 - stack_alignment);

                u32 compare_offset = encoder.count;
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x81);
                machine_x64_emit8(&encoder, 0xf9); // cmp rcx, 4096
                machine_x64_emit32(&encoder, 4096);
                u32 short_exit = encoder.count;
                machine_x64_emit8(&encoder, 0x72); // jb final residual subtract
                machine_x64_emit8(&encoder, 0);
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x81);
                machine_x64_emit8(&encoder, 0xec); // sub rsp, 4096
                machine_x64_emit32(&encoder, 4096);
                machine_x64_emit8(&encoder, 0xf6);
                machine_x64_emit8(&encoder, 0x04);
                machine_x64_emit8(&encoder, 0x24);
                machine_x64_emit8(&encoder, 0); // test byte [rsp], 0
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x81);
                machine_x64_emit8(&encoder, 0xe9); // sub rcx, 4096
                machine_x64_emit32(&encoder, 4096);
                u32 loop_back = encoder.count;
                machine_x64_emit8(&encoder, 0xeb);
                machine_x64_emit8(&encoder, 0);
                u32 residual = encoder.count;
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x29);
                machine_x64_emit8(&encoder, 0xcc); // sub rsp, rcx
                machine_x64_emit8(&encoder, 0xf6);
                machine_x64_emit8(&encoder, 0x04);
                machine_x64_emit8(&encoder, 0x24);
                machine_x64_emit8(&encoder, 0); // test byte [rsp], 0
                machine_x64_emit8(&encoder, 0x48);
                machine_x64_emit8(&encoder, 0x89);
                machine_x64_emit8(&encoder, 0xe0); // mov rax, rsp
                encoder.bytes[short_exit + 1] = (u8)(residual - (short_exit + 2));
                encoder.bytes[loop_back + 1] = (u8)(compare_offset - (loop_back + 2));
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
            case MACHINE_X64_ATOMIC_CMPXCHG16:
            {
                u32 result_slot = machine_ref_payload(instruction->operands[0]);
                u32 expected_slot = machine_ref_payload(instruction->operands[1]);
                u32 desired_slot = machine_ref_payload(instruction->operands[2]);
                u32 address = operand_registers[3];
                u32 result_offset = placement->stack_slot_offsets[result_slot];
                u32 expected_offset = placement->stack_slot_offsets[expected_slot];
                u32 desired_offset = placement->stack_slot_offsets[desired_slot];
                // CMPXCHG16B's implicit pairs are RAX:RDX (expected/result)
                // and RBX:RCX (desired). The result stores happen after the
                // instruction, preserving the old memory image on both the
                // success and failure paths.
                machine_x64_emit_frame_load(&encoder, MACHINE_X64_RAX, expected_offset);
                // Frame slots grow toward lower addresses: aggregate byte
                // offset 8 is represented by the slot offset minus eight.
                machine_x64_emit_frame_load(&encoder, MACHINE_X64_RDX, expected_offset - 8);
                machine_x64_emit_frame_load(&encoder, MACHINE_X64_RBX, desired_offset);
                machine_x64_emit_frame_load(&encoder, MACHINE_X64_RCX, desired_offset - 8);
                machine_x64_emit8(&encoder, 0xf0);
                machine_x64_emit8(&encoder, (u8)(0x48 | (address >= 8 ? 0x01 : 0)));
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0xc7);
                machine_x64_emit_memory_modrm(&encoder, 1, address, 0);
                machine_x64_emit_frame_store(&encoder, MACHINE_X64_RAX, result_offset);
                machine_x64_emit_frame_store(&encoder, MACHINE_X64_RDX, result_offset - 8);
            }
            break;
            case MACHINE_X64_UD2:
                machine_x64_emit8(&encoder, 0x0f);
                machine_x64_emit8(&encoder, 0x0b);
                break;
            case MACHINE_X64_VMOV_RR:
                // A full-width self-copy is the coalesced form and encodes
                // to nothing, exactly like the scalar MOV_RR.
                if (operand_registers[0] != operand_registers[1])
                {
                    machine_x64_emit_vector_copy(&encoder, operand_registers[0] - MACHINE_X64_ZMM0, operand_registers[1] - MACHINE_X64_ZMM0);
                }
                break;
            case MACHINE_X64_VLOAD_FRAME:
                machine_x64_emit_vector_frame(&encoder, operand_registers[0] - MACHINE_X64_ZMM0, false,
                                              placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])]);
                break;
            case MACHINE_X64_VSTORE_FRAME:
                machine_x64_emit_vector_frame(&encoder, operand_registers[1] - MACHINE_X64_ZMM0, true,
                                              placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])]);
                break;
            case MACHINE_X64_VLOAD_PTR:
                machine_x64_emit_evex_indirect(&encoder, 1, 3, 0x6f, operand_registers[0] - MACHINE_X64_ZMM0, 0, false, operand_registers[1]);
                break;
            case MACHINE_X64_VSTORE_PTR:
                machine_x64_emit_evex_indirect(&encoder, 1, 3, 0x7f, operand_registers[1] - MACHINE_X64_ZMM0, 0, false, operand_registers[0]);
                break;
            case MACHINE_X64_VLOAD_PTR_MASKED:
                machine_x64_emit_kmovq_from_general(&encoder, MACHINE_X64_STAGE_MASK, operand_registers[2]);
                machine_x64_emit_evex_indirect(&encoder, 1, 3, 0x6f, operand_registers[0] - MACHINE_X64_ZMM0, MACHINE_X64_STAGE_MASK, true,
                                               operand_registers[1]);
                break;
            case MACHINE_X64_VSTORE_PTR_MASKED:
                machine_x64_emit_kmovq_from_general(&encoder, MACHINE_X64_STAGE_MASK, operand_registers[1]);
                machine_x64_emit_evex_indirect(&encoder, 1, 3, 0x7f, operand_registers[2] - MACHINE_X64_ZMM0, MACHINE_X64_STAGE_MASK, false,
                                               operand_registers[0]);
                break;
            case MACHINE_X64_VCOMPRESS_STORE_PTR:
                // vpcompressb writes its destination through the rm operand,
                // so the compressing store shares the plain store's shape.
                machine_x64_emit_kmovq_from_general(&encoder, MACHINE_X64_STAGE_MASK, operand_registers[1]);
                machine_x64_emit_evex_indirect(&encoder, 2, 1, 0x63, operand_registers[2] - MACHINE_X64_ZMM0, MACHINE_X64_STAGE_MASK, false,
                                               operand_registers[0]);
                break;
            case MACHINE_X64_VSPLATB:
                // vpbroadcastb zmm, r32.
                machine_x64_emit_evex_register(&encoder, 2, 1, 0x7a, operand_registers[0] - MACHINE_X64_ZMM0, 0, 0, false, false, operand_registers[1]);
                break;
            case MACHINE_X64_VPCMP_MASK:
            {
                u32 compare_left = operand_registers[1] - MACHINE_X64_ZMM0;
                u32 compare_right = operand_registers[2] - MACHINE_X64_ZMM0;
                if (instruction->payload == 0)
                {
                    machine_x64_emit_evex_register(&encoder, 1, 1, 0x74, MACHINE_X64_STAGE_MASK, compare_left, 0, false, false, compare_right);
                }
                else if (instruction->payload == 1)
                {
                    // vpcmpub with predicate 1: unsigned less-than.
                    machine_x64_emit_evex_register(&encoder, 3, 1, 0x3e, MACHINE_X64_STAGE_MASK, compare_left, 0, false, false, compare_right);
                    machine_x64_emit8(&encoder, 1);
                }
                else
                {
                    machine_x64_emit_evex_register(&encoder, 2, 1, 0x26, MACHINE_X64_STAGE_MASK, compare_left, 0, false, false, compare_right);
                }
                machine_x64_emit_kmovq_to_general(&encoder, operand_registers[0], MACHINE_X64_STAGE_MASK);
            }
            break;
            case MACHINE_X64_VPMOVB2M:
                // vpmovb2m has no memory form and its source is the rm.
                machine_x64_emit_evex_register(&encoder, 2, 2, 0x29, MACHINE_X64_STAGE_MASK, 0, 0, false, false, operand_registers[1] - MACHINE_X64_ZMM0);
                machine_x64_emit_kmovq_to_general(&encoder, operand_registers[0], MACHINE_X64_STAGE_MASK);
                break;
            case MACHINE_X64_VPERMT2B:
                machine_x64_emit_kmovq_from_general(&encoder, MACHINE_X64_STAGE_MASK, operand_registers[1]);
                machine_x64_emit_evex_register(&encoder, 2, 1, 0x7d, operand_registers[0] - MACHINE_X64_ZMM0, operand_registers[2] - MACHINE_X64_ZMM0,
                                               MACHINE_X64_STAGE_MASK, true, false, operand_registers[3] - MACHINE_X64_ZMM0);
                break;
            case MACHINE_X64_VCOMPRESSB:
                // Register form: rm is the destination and reg the source.
                machine_x64_emit_kmovq_from_general(&encoder, MACHINE_X64_STAGE_MASK, operand_registers[1]);
                machine_x64_emit_evex_register(&encoder, 2, 1, 0x63, operand_registers[2] - MACHINE_X64_ZMM0, 0, MACHINE_X64_STAGE_MASK, true, false,
                                               operand_registers[0] - MACHINE_X64_ZMM0);
                break;
            case MACHINE_X64_VPMOVZXBD:
            {
                u32 widen_destination = operand_registers[0] - MACHINE_X64_ZMM0;
                u32 widen_source = operand_registers[1] - MACHINE_X64_ZMM0;
                if (instruction->payload)
                {
                    // vextracti32x4 xmm, zmm, quarter names its destination
                    // in rm, so the destination doubles as the scratch and
                    // no extra register is needed.
                    machine_x64_emit_evex_register(&encoder, 3, 1, 0x39, widen_source, 0, 0, false, false, widen_destination);
                    machine_x64_emit8(&encoder, (u8)instruction->payload);
                    widen_source = widen_destination;
                }
                machine_x64_emit_evex_register(&encoder, 2, 1, 0x31, widen_destination, 0, 0, false, false, widen_source);
            }
            break;
            case MACHINE_X64_VPSLLD_RI:
                // vpslld with an immediate: /6, destination in vvvv.
                machine_x64_emit_evex_register(&encoder, 1, 1, 0x72, 6, operand_registers[0] - MACHINE_X64_ZMM0, 0, false, false,
                                               operand_registers[1] - MACHINE_X64_ZMM0);
                machine_x64_emit8(&encoder, (u8)instruction->payload);
                break;
            case MACHINE_X64_VPTERNLOGD:
                machine_x64_emit_evex_register(&encoder, 3, 1, 0x25, operand_registers[0] - MACHINE_X64_ZMM0, operand_registers[1] - MACHINE_X64_ZMM0, 0,
                                               false, false, operand_registers[2] - MACHINE_X64_ZMM0);
                machine_x64_emit8(&encoder, (u8)instruction->payload);
                break;
            case MACHINE_X64_VBINARY:
                // Payload bit 8 marks the EVEX.W1 forms (vpaddq/vpsubq), the
                // same wide-bit convention FARITH uses for its double forms.
                machine_x64_emit_evex_register(&encoder, 1, 1, (u8)instruction->payload, operand_registers[0] - MACHINE_X64_ZMM0,
                                               operand_registers[1] - MACHINE_X64_ZMM0, 0, false, (instruction->payload & 0x100) != 0,
                                               operand_registers[2] - MACHINE_X64_ZMM0);
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
                machine_x64_exact_counters_assign(&result, exact_counters);
                return result;
                }
            }
            MachinePoint after = machine_point_make(instruction_index, MACHINE_POINT_AFTER);
            while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == after)
            {
                MachineEdit* edit = placement->edits + edit_cursor;
                if (edit->kind == MACHINE_EDIT_RELOAD)
                {
                    if (edit->location >= MACHINE_X64_ZMM0)
                    {
                        machine_x64_emit_vector_frame(&encoder, edit->location - MACHINE_X64_ZMM0, false, placement->virtual_register_offsets[edit->subject]);
                    }
                    else
                    {
                        machine_x64_emit_frame_load(&encoder, edit->location, placement->virtual_register_offsets[edit->subject]);
                    }
                }
                else if (edit->kind == MACHINE_EDIT_COPY)
                {
                    if (edit->location >= MACHINE_X64_ZMM0)
                    {
                        machine_x64_emit_vector_copy(&encoder, edit->location - MACHINE_X64_ZMM0, edit->subject - MACHINE_X64_ZMM0);
                    }
                    else
                    {
                        machine_x64_emit_rr(&encoder, true, false, 0x89, edit->subject, edit->location);
                    }
                }
                else if (edit->kind == MACHINE_EDIT_REMATERIALIZE)
                {
                    machine_x64_emit_immediate(&encoder, edit->location, function->immediates[edit->subject]);
                }
                else if (edit->location >= MACHINE_X64_ZMM0)
                {
                    machine_x64_emit_vector_frame(&encoder, edit->location - MACHINE_X64_ZMM0, true, placement->virtual_register_offsets[edit->subject]);
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
        machine_x64_exact_counters_assign(&result, exact_counters);
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
    machine_x64_exact_counters_assign(&result, exact_counters);
    return result;
}
