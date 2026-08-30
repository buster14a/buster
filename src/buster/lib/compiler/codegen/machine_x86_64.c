// x86-64 machine selection, MIR_STACK placement, and encoding. Included by
// machine.c in the backend-implementation-file pattern; not a standalone
// translation unit. The stage-2 subset covers scalar integer functions:
// arguments/constants/casts/unary/binary arithmetic and comparisons, direct
// locals and pointer dereference, branches, and scalar returns. Everything
// else is an explicit unsupported result, never a silent misselection.

#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>
#include <buster/lib/integer.h>

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
    // For a sub-32-bit scalar integer argument, the MOVSX/MOVZX opcode that
    // widens it into its argument register: the de-facto System V contract
    // clang's callers implement extends such arguments to 32 bits, and
    // callees exist that read the widened register directly. Zero when a
    // plain move suffices.
    u16 scalar_extend_opcode;
    // The unrounded size of an indirect result. byte_size is rounded up to
    // whole eightbytes for the value's own slot; the store through the
    // caller's hidden pointer must not write the rounding — the caller
    // allocated exactly the type, and clang puts its stack canary right
    // after.
    u32 exact_byte_size;
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
    // -fPIC: decided by the driver, read only where a thread-local symbol
    // reference picks its model.
    bool position_independent;
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
    // Win64 outgoing argument area: the slot every call writes its shadow
    // space and stack arguments into, sized to the widest call in the
    // function. Created on the first call selected and patched to its final
    // size once selection is done, because the size is only known then.
    u32 outgoing_slot;
    u32 outgoing_bytes;
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

MachineTargetDescription const* machine_target_x86_64(void)
{
    return &machine_x86_64_description;
}

// Win64's four positional argument registers. Slot i takes this register for
// an integer part or XMM i for a float part — never both files at once, which
// is the whole difference from the System V sequences above.
BUSTER_GLOBAL_LOCAL u8 const machine_x64_windows_arguments[4] = {
    MACHINE_X64_RCX,
    MACHINE_X64_RDX,
    MACHINE_X64_R8,
    MACHINE_X64_R9,
};

// The same allocatable file as System V; the ABI moves RSI and RDI into the
// callee-saved half, so a call preserves them and binding one costs a
// prologue push. The vector class keeps only the registers Win64 leaves
// volatile: XMM6-15's low halves are callee-saved and the allocator has no
// shape for saving them, so ZMM6-15 stay out of the file entirely.
// The scratch slots avoid RSI for the same reason — MIR_STACK writes them
// without recording a save.
BUSTER_GLOBAL_LOCAL MachineTargetDescription const machine_x86_64_windows_description = {
    .allocatable_mask = (1u << MACHINE_X64_RAX) | (1u << MACHINE_X64_RCX) | (1u << MACHINE_X64_RDX) | (1u << MACHINE_X64_RSI) | (1u << MACHINE_X64_RDI) |
                        (1u << MACHINE_X64_R8) | (1u << MACHINE_X64_R9) | (1u << MACHINE_X64_R10) | (1u << MACHINE_X64_R11) | (1u << MACHINE_X64_RBX) |
                        (1u << MACHINE_X64_R12) | (1u << MACHINE_X64_R13) | (1u << MACHINE_X64_R14) | (1u << MACHINE_X64_R15),
    .callee_saved_mask = (1u << MACHINE_X64_RBX) | (1u << MACHINE_X64_RSI) | (1u << MACHINE_X64_RDI) | (1u << MACHINE_X64_R12) | (1u << MACHINE_X64_R13) |
                         (1u << MACHINE_X64_R14) | (1u << MACHINE_X64_R15),
    .register_count = MACHINE_X64_REGISTER_COUNT,
    .slot_scratch = {MACHINE_X64_RAX, MACHINE_X64_RCX, MACHINE_X64_RDX, MACHINE_X64_R11},
    .copy_opcode = MACHINE_X64_MOV_RR,
    .constant_opcode = MACHINE_X64_MOV_RI,
    .indirect_call_opcode = MACHINE_X64_CALL_INDIRECT,
    .switch_opcode = MACHINE_X64_SWITCH,
    .float_bridge_opcode = MACHINE_X64_MOVQ_TO_XMM,
    .indirect_call_register = MACHINE_X64_R10,
    .float_bridge_register = MACHINE_X64_RAX,
    .quality_pin_registers = {MACHINE_X64_R15, MACHINE_X64_R14, MACHINE_X64_R13, MACHINE_X64_R12, MACHINE_X64_RDI, MACHINE_X64_RSI, MACHINE_X64_RBX},
    .quality_pin_register_count = 7,
    .vector_allocatable_mask = (0x3full << MACHINE_X64_ZMM0) | (0xffffull << MACHINE_X64_ZMM16),
    .vector_copy_opcode = MACHINE_X64_VMOV_RR,
    .saves_precede_frame_pointer = 1,
    .vector_slot_scratch = {MACHINE_X64_ZMM0, MACHINE_X64_ZMM1, MACHINE_X64_ZMM2, MACHINE_X64_ZMM3},
};

BUSTER_F_DECL MachineTargetDescription const* machine_target_x86_64_windows(void)
{
    return &machine_x86_64_windows_description;
}

// Win64 covers the Windows and UEFI targets, matching ir_target_abi_convention.
BUSTER_GLOBAL_LOCAL bool machine_x64_target_is_windows(Target target)
{
    return target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI;
}

BUSTER_GLOBAL_LOCAL IrAbiConvention machine_x64_abi_convention(Target target)
{
    return machine_x64_target_is_windows(target) ? IR_ABI_CONVENTION_WIN64_X86_64 : IR_ABI_CONVENTION_SYSTEMV_X86_64;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_type_is_scalar_register(IrType* type)
{
    bool result;
    if (!type || !type->layout.resolved || type->layout.size > 8)
    {
        result = false;
    }
    else
    {
        result = type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_ENUM;
    }

    return result;
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
    bool result;
    if (!target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512F) || !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512BW))
    {
        result = false;
    }
    else if (operation == IR_SIMD_PERMUTE2_BYTE)
    {
        result = target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512VBMI);
    }
    else if (operation == IR_SIMD_COMPRESS_BYTE || operation == IR_SIMD_COMPRESS_STORE_BYTE)
    {
        result = target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512VBMI2);
    }
    else
    {
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_value_shape(IrProgram* program, IrTypeId type_id, IrAbiUse use, Target target, MachineX64ValueShape* shape)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    IrAbiConvention convention = machine_x64_abi_convention(target);
    if (machine_x64_type_is_vector_register(type))
    {
        // The vector ABI needs the 512-bit vocabulary's register moves, so a
        // target without the features keeps the canonical fallback, whose
        // model-dependent register split this subset does not reproduce.
        // Win64 passes every 64-byte vector indirectly, a shape this subset
        // does not build, so those signatures stay canonical outright.
        if (!machine_x64_simd_supported(target, IR_SIMD_SPLAT_BYTE) || convention == IR_ABI_CONVENTION_WIN64_X86_64)
        {
            return false;
        }
        IrAbiValue vector_abi = ir_type_abi_value(program, type_id, convention, use);
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
        u16 extend_opcode = 0;
        if (type->kind == IR_TYPE_BOOLEAN || (type->kind == IR_TYPE_INTEGER && type->bit_width == 8))
        {
            extend_opcode = type->kind == IR_TYPE_INTEGER && type->is_signed ? MACHINE_X64_MOVSX8_RR : MACHINE_X64_MOVZX8_RR;
        }
        else if (type->kind == IR_TYPE_INTEGER && type->bit_width == 16)
        {
            extend_opcode = type->is_signed ? MACHINE_X64_MOVSX16_RR : MACHINE_X64_MOVZX16_RR;
        }
        *shape = (MachineX64ValueShape){
            .part_count = 1,
            .byte_size = 8,
            .scalar_extend_opcode = extend_opcode,
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
        // Win64 has no register pair: a 128-bit integer argument travels
        // indirectly and the result returns by value in XMM0 (clang's
        // de-facto ABI), neither of which this subset builds.
        if (convention == IR_ABI_CONVENTION_WIN64_X86_64)
        {
            return false;
        }
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
    IrAbiValue abi = ir_type_abi_value(program, type_id, convention, use);
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
                .exact_byte_size = (u32)type->layout.size,
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
BUSTER_GLOBAL_LOCAL void machine_x64_place_argument(MachineX64ValueShape* shape, bool windows, u32* integer_count, u32* float_count, u32* stack_part_count,
                                                    MachineX64ArgumentPlacement* placement)
{
    u32 integer_parts = machine_x64_shape_class_parts(shape, false);
    u32 float_parts = machine_x64_shape_class_parts(shape, true);
    if (windows)
    {
        // One positional slot per argument, whichever file it lands in, and
        // no split between the two: a Win64 shape is always a single part.
        // The two cursors advance together so the slot number is readable
        // from either one, which is what lets the staging paths below share
        // the System V code.
        u32 slot = BUSTER_MAX(*integer_count, *float_count);
        if (shape->force_stack || shape->part_count != 1 || slot >= BUSTER_ARRAY_LENGTH(machine_x64_windows_arguments))
        {
            *placement = (MachineX64ArgumentPlacement){
                .first_stack_part = (u16)*stack_part_count,
                .stack_part_count = (u16)(shape->byte_size / 8),
                .on_stack = true,
            };
            *stack_part_count += shape->byte_size / 8;
            *integer_count = slot + 1;
            *float_count = slot + 1;
            return;
        }
        *placement = (MachineX64ArgumentPlacement){
            .first_integer = (u16)slot,
            .first_float = (u16)slot,
        };
        *integer_count = slot + 1;
        *float_count = slot + 1;
        return;
    }
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

// The integer register an argument slot occupies under the running ABI.
// Placement above guarantees the index is inside its own file.
BUSTER_GLOBAL_LOCAL u32 machine_x64_argument_register(bool windows, u32 index)
{
    return windows ? machine_x64_windows_arguments[index] : machine_x64_system_v_arguments[index];
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
    bool result;
    switch (opcode)
    {
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
        result = true;
        break;
    default:
        result = false;
        break;
    }

    return result;
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
    bool result;
    if (value.value >= selector->function->value_count || selector->value_virtual_registers[value.value] == UINT32_MAX)
    {
        result = false;
    }
    else
    {
        *register_out = selector->value_virtual_registers[value.value];
        result = true;
    }

    return result;
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
    u32 result;
    switch (operation)
    {
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_POINTER_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL:
        result = MACHINE_X64_CONDITION_EQUAL;
        break;
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_POINTER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL:
        result = MACHINE_X64_CONDITION_NOT_EQUAL;
        break;
    case IR_BINARY_SIGNED_LESS:
        result = MACHINE_X64_CONDITION_LESS;
        break;
    case IR_BINARY_SIGNED_LESS_EQUAL:
        result = MACHINE_X64_CONDITION_LESS_EQUAL;
        break;
    case IR_BINARY_SIGNED_GREATER:
        result = MACHINE_X64_CONDITION_GREATER;
        break;
    case IR_BINARY_SIGNED_GREATER_EQUAL:
        result = MACHINE_X64_CONDITION_GREATER_EQUAL;
        break;
    case IR_BINARY_UNSIGNED_LESS:
        result = MACHINE_X64_CONDITION_BELOW;
        break;
    case IR_BINARY_UNSIGNED_LESS_EQUAL:
        result = MACHINE_X64_CONDITION_BELOW_EQUAL;
        break;
    case IR_BINARY_UNSIGNED_GREATER:
        result = MACHINE_X64_CONDITION_ABOVE;
        break;
    case IR_BINARY_UNSIGNED_GREATER_EQUAL:
        result = MACHINE_X64_CONDITION_ABOVE_EQUAL;
        break;
    default:
        result = 0;
        break;
    }

    return result;
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

BUSTER_GLOBAL_LOCAL bool machine_x64_select_va_arg(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 source_register;
    if (instruction->operand_count >= 1 && instruction->result.value < function->value_count &&
        machine_x64_operand_register(selector, instruction->operands[0], &source_register))
    {
        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u32 result_slot = selector->value_stack_slots[instruction->result.value];
        bool result_is_frame = result_register == UINT32_MAX && result_slot != UINT32_MAX;
        bool scalar = machine_x64_type_is_scalar_register(value_type) ||
                      (value_type && value_type->kind == IR_TYPE_FLOAT && (value_type->bit_width == 32 || value_type->bit_width == 64));
        bool aggregate = value_type && value_type->layout.resolved &&
                         (value_type->kind == IR_TYPE_STRUCT || value_type->kind == IR_TYPE_UNION || value_type->kind == IR_TYPE_SLICE ||
                          value_type->kind == IR_TYPE_ARRAY || (value_type->kind == IR_TYPE_INTEGER && value_type->bit_width == 128));
        MachineVaArg metadata;
        if ((scalar || aggregate) && result_is_frame == aggregate &&
            machine_x64_va_arg_metadata(selector, value_type, result_slot, result_is_frame, &metadata))
        {
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
            selected = true;
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_va_start(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;

    u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
    bool selected = false;
    if (result_slot != UINT32_MAX && selector->va_register_save_slot != UINT32_MAX)
    {
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
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_va_copy(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;

    u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
    u32 source_register;
    bool selected = false;
    if (result_slot != UINT32_MAX && instruction->operand_count >= 1 && machine_x64_operand_register(selector, instruction->operands[0], &source_register))
    {
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                             .payload = 32,
                                             .opcode = MACHINE_X64_COPY_FRAME_FROM_PTR,
                                         });
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_va_end(MachineX64Selector* selector, IrInstruction* instruction)
{
    u32 source_register;
    bool selected = false;
    if (instruction->operand_count >= 1 && machine_x64_operand_register(selector, instruction->operands[0], &source_register))
    {
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
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_local(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    // Direct locals produce no code: the stack slot recorded during
    // classification is the storage, exactly like the canonical path —
    // or, promoted, the virtual register is. An over-aligned local
    // computes its runtime-aligned pointer here, the same lea/add/and
    // the canonical emitter runs at its LOCAL instruction.
    bool selected;
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
        selected = true;
    }
    else
    {
        selected = selector->value_stack_slots[instruction->result.value] != UINT32_MAX ||
                   selector->value_virtual_registers[instruction->result.value] != UINT32_MAX;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_stack_save(MachineX64Selector* selector, u32 result_register)
{
    // RSP is a physical operand, so the scheduler and allocators keep
    // this checkpoint ordered with every stack-affecting row. The value
    // remains an ordinary virtual register and is spilled around calls
    // when its lifetime crosses one.
    bool selected = false;
    if (result_register != UINT32_MAX)
    {
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RSP)},
                                                       .opcode = MACHINE_X64_MOV_RR,
                                                   });
        machine_x64_define(selector, result_register, row);
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_stack_allocate(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    bool selected = false;
    u32 size_register;
    // A dynamic allocation moves the stack pointer away from the outgoing
    // argument area's base, which every Win64 call in the function reads
    // as its own stack pointer. The canonical path models that with a
    // frame base at the bottom of the frame; this subset does not, so
    // such functions stay canonical.
    if (result_register != UINT32_MAX && instruction->immediate_count && instruction->immediates &&
        !machine_x64_target_is_windows(selector->target))
    {
        u64 requested_alignment = instruction->immediates[0];
        // An out-of-range request lands on zero, which the power-of-two test
        // below rejects along with every other unusable alignment.
        u32 stack_alignment = requested_alignment <= UINT32_MAX ? BUSTER_MAX((u32)requested_alignment, 16u) : 0;
        if (stack_alignment && (stack_alignment & (stack_alignment - 1u)) == 0 &&
            machine_x64_operand_register(selector, instruction->operands[0], &size_register))
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, size_register)},
                                                           .payload = stack_alignment,
                                                           .opcode = MACHINE_X64_STACK_ALLOCATE,
                                                       });
            machine_x64_define(selector, result_register, row);
            selected = true;
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_stack_restore(MachineX64Selector* selector, IrInstruction* instruction)
{
    bool selected = false;
    u32 saved_register;
    if (!machine_x64_target_is_windows(selector->target) && machine_x64_operand_register(selector, instruction->operands[0], &saved_register))
    {
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RSP),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, saved_register)},
                                             .opcode = MACHINE_X64_MOV_RR,
                                         });
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_argument(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    // The typed ARGUMENT can appear anywhere the frontend first used the
    // parameter; the value itself was captured by the entry rows before
    // any scratch register could clobber the incoming fixed registers.
    bool selected = false;
    if (instruction->immediate_count && instruction->immediates)
    {
        u32 argument_index = (u32)instruction->immediates[0];
        selected = argument_index < MACHINE_X64_MAX_ARGUMENTS &&
                   (result_register != UINT32_MAX || selector->value_stack_slots[instruction->result.value] != UINT32_MAX) &&
                   selector->argument_values[argument_index] == instruction->result.value;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_constant(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    bool selected = false;
    if (result_register != UINT32_MAX)
    {
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
        selected = true;
    }
    return selected;
}

// i128 values use the same two-eightbyte frame representation as aggregate
// values. This path stays direct and bounded: scalar integer extensions and
// truncations use the existing frame rows, a 128-bit reinterpret is a
// byte-preserving frame copy, and other aggregate casts continue through the
// explicit unsupported fallback.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_cast_i128(MachineX64Selector* selector, IrInstruction* instruction, IrType* source_type,
                                                      IrType* cast_target_type, u32 source_bits, u32 result_register)
{
    IrFunction* function = selector->function;

    bool selected = false;
    if (source_type && cast_target_type && source_type->kind == IR_TYPE_INTEGER && cast_target_type->kind == IR_TYPE_INTEGER)
    {
        bool source_integer128 = source_type->bit_width == 128;
        bool target_integer128 = cast_target_type->bit_width == 128;
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
            selected = source_slot != UINT32_MAX && target_slot != UINT32_MAX;
            if (selected)
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, target_slot),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, source_slot)},
                                                     .payload = 16,
                                                     .opcode = MACHINE_X64_COPY_FRAME_FROM_FRAME,
                                                 });
            }
        }
        else if (truncate_i128)
        {
            selected = result_register != UINT32_MAX && source_slot != UINT32_MAX;
            if (selected)
            {
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
            }
        }
        else if (extend_i128)
        {
            // Only the rows below read this, and only where the operand lookup
            // that fills it succeeded.
            u32 source_register = UINT32_MAX;
            selected = result_register == UINT32_MAX && target_slot != UINT32_MAX && source_bits >= 8 && source_bits <= 64 &&
                       machine_x64_operand_register(selector, instruction->operands[0], &source_register);
            if (selected)
            {
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
            }
        }
    }
    return selected;
}

// The 64-bit convert instructions carry every narrower integer case after an
// extension, and the branchy unsigned-64 sequences stay outside the subset.
// The 64-bit convert instructions carry every narrower integer case after an
// extension; the branchy unsigned-64 sequences have rows of their own.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_cast_integer_to_float(MachineX64Selector* selector, IrInstruction* instruction, IrType* cast_target_type,
                                                                  u32 source_bits, u32 source_register, u32 result_register)
{
    bool selected = false;
    bool cast_signed = instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT;
    bool convertible = cast_target_type && cast_target_type->kind == IR_TYPE_FLOAT &&
                       (cast_target_type->bit_width == 32 || cast_target_type->bit_width == 64) && source_bits;
    if (convertible && !cast_signed && source_bits == 64)
    {
        u32 branchy_row = machine_x64_select_row(
            selector, (MachineInstruction){
                          .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                       machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                          .opcode = (u16)(cast_target_type->bit_width == 64 ? MACHINE_X64_CVT_U64_TO_F64 : MACHINE_X64_CVT_U64_TO_F32),
                      });
        machine_x64_define(selector, result_register, branchy_row);
        selected = true;
    }
    else if (convertible)
    {
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
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_cast_float_to_integer(MachineX64Selector* selector, IrInstruction* instruction, IrType* source_type,
                                                                  IrType* cast_target_type, u32 source_register, u32 result_register)
{
    bool selected = false;
    bool to_unsigned = instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER;
    bool convertible = source_type && source_type->kind == IR_TYPE_FLOAT && (source_type->bit_width == 32 || source_type->bit_width == 64);
    if (convertible && to_unsigned && cast_target_type && cast_target_type->kind == IR_TYPE_INTEGER && cast_target_type->bit_width == 64)
    {
        u32 branchy_row = machine_x64_select_row(
            selector, (MachineInstruction){
                          .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                       machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                          .opcode = (u16)(source_type->bit_width == 64 ? MACHINE_X64_CVT_F64_TO_U64 : MACHINE_X64_CVT_F32_TO_U64),
                      });
        machine_x64_define(selector, result_register, branchy_row);
        selected = true;
    }
    else if (convertible)
    {
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                       .opcode = (u16)(source_type->bit_width == 64 ? MACHINE_X64_CVT_F64_TO_I64
                                                                                                    : MACHINE_X64_CVT_F32_TO_I64),
                                                   });
        machine_x64_define(selector, result_register, row);
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_cast(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    if (instruction->operands[0].value < function->value_count)
    {
        IrType* source_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        IrType* cast_target_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u32 source_bits = machine_x64_scalar_bit_width(source_type);
        bool source_integer128 = source_type && source_type->kind == IR_TYPE_INTEGER && source_type->bit_width == 128;
        bool target_integer128 = cast_target_type && cast_target_type->kind == IR_TYPE_INTEGER && cast_target_type->bit_width == 128;
        // Only the register conversions below read this, and only where the
        // operand lookup that fills it succeeded.
        u32 source_register = UINT32_MAX;
        if (source_integer128 || target_integer128)
        {
            selected = machine_x64_select_cast_i128(selector, instruction, source_type, cast_target_type, source_bits, result_register);
        }
        // Float conversions mirror the canonical forms.
        else if (result_register != UINT32_MAX && machine_x64_operand_register(selector, instruction->operands[0], &source_register))
        {
            if (instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND || instruction->conversion_operation == IR_CONVERSION_FLOAT_TRUNCATE)
            {
                bool extend = instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND;
                u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                               .opcode = (u16)(extend ? MACHINE_X64_CVT_F32_TO_F64 : MACHINE_X64_CVT_F64_TO_F32),
                                                           });
                machine_x64_define(selector, result_register, row);
                selected = true;
            }
            else if (instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ||
                     instruction->conversion_operation == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT)
            {
                selected = machine_x64_select_cast_integer_to_float(selector, instruction, cast_target_type, source_bits, source_register, result_register);
            }
            else if (instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ||
                     instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER)
            {
                selected = machine_x64_select_cast_float_to_integer(selector, instruction, source_type, cast_target_type, source_register, result_register);
            }
            else
            {
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
                selected = opcode != 0;
                if (selected)
                {
                    u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                                   .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                                machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                                   .opcode = opcode,
                                                               });
                    machine_x64_define(selector, result_register, row);
                }
            }
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_unary(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 source_register;
    if (result_register != UINT32_MAX && machine_x64_operand_register(selector, instruction->operands[0], &source_register))
    {
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
            selected = true;
        }
        else if (instruction->unary_operation == IR_UNARY_INTEGER_POPULATION_COUNT)
        {
            selected = target_cpu_feature_has(selector->target, TARGET_CPU_FEATURE_X86_POPCNT);
            if (selected)
            {
                u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                               .opcode = (u16)(wide ? MACHINE_X64_POPCNT64 : MACHINE_X64_POPCNT32),
                                                           });
                machine_x64_define(selector, result_register, row);
            }
        }
        else if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS || instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS)
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
            selected = true;
        }
        else if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE)
        {
            IrType* float_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
            selected = float_type && float_type->kind == IR_TYPE_FLOAT && (float_type->bit_width == 32 || float_type->bit_width == 64);
            if (selected)
            {
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
            }
        }
        else if (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
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
            selected = true;
        }
    }
    return selected;
}

// Element-wise integer vector binary as one three-address EVEX row; the payload
// carries the 66 0F map opcode the canonical native path picks by operation and
// lane width, and bit 8 marks the 64-bit-lane forms whose EVEX encodings are W1
// (vpaddq/vpsubq — their W0 encodings #UD on real hardware).
BUSTER_GLOBAL_LOCAL bool machine_x64_select_vector_binary(MachineX64Selector* selector, IrInstruction* instruction, IrType* operand_type, u32 left_register,
                                                          u32 right_register, u32 result_register)
{
    IrProgram* program = selector->program;

    bool selected = false;
    IrType* element = ir_type_from_id(&program->types, operand_type->element_type);
    if (machine_x64_type_is_vector_register(operand_type) && machine_x64_simd_supported(selector->target, IR_SIMD_SPLAT_BYTE) && element &&
        element->kind == IR_TYPE_INTEGER)
    {
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
        if (vector_opcode)
        {
    u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                   .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                   .payload = vector_opcode,
                                                   .opcode = MACHINE_X64_VBINARY,
                                               });
    machine_x64_define(selector, result_register, row);
            selected = true;
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_float_binary(MachineX64Selector* selector, IrInstruction* instruction, IrType* operand_type, u32 left_register,
                                                         u32 right_register, u32 result_register)
{
    bool selected = false;
    if (operand_type->bit_width == 32 || operand_type->bit_width == 64)
    {
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
        selected = sse_opcode || compare_payload;
        if (selected)
        {
    u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                   .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                   .payload = (sse_opcode ? sse_opcode : compare_payload) | float_wide_bit,
                                                   .opcode = (u16)(sse_opcode ? MACHINE_X64_FARITH : MACHINE_X64_FCMP_SET),
                                               });
    machine_x64_define(selector, result_register, row);
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_scalar_binary(MachineX64Selector* selector, IrInstruction* instruction, IrTypeId operand_type_id,
                                                          u32 left_register, u32 right_register, u32 result_register)
{
    IrProgram* program = selector->program;

    bool selected = false;
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
        selected = true;
    }
    else
    {
        u32 condition = machine_x64_condition_from_comparison(instruction->binary_operation);
        if (condition)
        {
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
            selected = true;
        }
    }
    return selected;
}

// The CMPXCHG16B success boolean, when this comparison is the one that reads
// it: the atomic row materializes it immediately, and later aggregate copies
// consume that value rather than live flags across unrelated IR rows.
BUSTER_GLOBAL_LOCAL u32 machine_x64_atomic_success_source(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;

    u32 success_register = UINT32_MAX;
    if (instruction->operand_count >= 2 && instruction->binary_operation == IR_BINARY_INTEGER_EQUAL &&
        instruction->operands[0].value < function->value_count && instruction->operands[1].value < function->value_count)
    {
        IrValue* observed_value = function->values + instruction->operands[0].value;
        IrInstruction* atomic_instruction =
            observed_value->definition.value < function->instruction_count ? function->instructions + observed_value->definition.value : 0;
        bool is_atomic_expected_compare = atomic_instruction && atomic_instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE &&
                                          atomic_instruction->operand_count >= 2 &&
                                          atomic_instruction->operands[1].value == instruction->operands[1].value;
        if (is_atomic_expected_compare)
        {
            success_register = selector->atomic_success_registers[instruction->operands[0].value];
        }
    }
    return success_register;
}

// A 128-bit unsigned right shift, the one i128 binary the subset lowers.
BUSTER_GLOBAL_LOCAL bool machine_x64_binary_is_i128_shift(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool matched = false;
    if (instruction->operand_count >= 2 && instruction->operands[0].value < function->value_count && instruction->result.value < function->value_count)
    {
        IrType* left_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        IrType* result_type = ir_type_from_id(&program->types, function->values[instruction->result.value].canonical_type);
        matched = left_type && result_type && left_type->kind == IR_TYPE_INTEGER && result_type->kind == IR_TYPE_INTEGER &&
                  left_type->bit_width == 128 && result_type->bit_width == 128 && instruction->binary_operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT;
    }
    return matched;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_binary(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 success_register = machine_x64_atomic_success_source(selector, instruction);
    u32 left_register = UINT32_MAX;
    u32 right_register = UINT32_MAX;
    if (result_register != UINT32_MAX && success_register != UINT32_MAX)
    {
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, success_register)},
                                                       .opcode = MACHINE_X64_MOV_RR,
                                                   });
        machine_x64_define(selector, result_register, row);
        selected = true;
    }
    else if (machine_x64_binary_is_i128_shift(selector, instruction))
    {
        u32 amount;
        selected = machine_x64_constant_shift_amount(selector, instruction->operands[1], &amount) &&
                   machine_x64_select_i128_unsigned_shift_right(selector, instruction, amount);
    }
    else if (result_register != UINT32_MAX && machine_x64_operand_register(selector, instruction->operands[0], &left_register) &&
             machine_x64_operand_register(selector, instruction->operands[1], &right_register))
    {
        IrTypeId operand_type_id = function->values[instruction->operands[0].value].canonical_type;
        IrType* operand_type = ir_type_from_id(&program->types, operand_type_id);
        if (operand_type && operand_type->kind == IR_TYPE_VECTOR)
        {
            selected = machine_x64_select_vector_binary(selector, instruction, operand_type, left_register, right_register, result_register);
        }
        else if (operand_type && operand_type->kind == IR_TYPE_FLOAT)
        {
            selected = machine_x64_select_float_binary(selector, instruction, operand_type, left_register, right_register, result_register);
        }
        else if (machine_x64_type_is_scalar_register(operand_type))
        {
            selected = machine_x64_select_scalar_binary(selector, instruction, operand_type_id, left_register, right_register, result_register);
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_dereference(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    bool selected = false;
    u32 source_register;
    if (result_register != UINT32_MAX && machine_x64_operand_register(selector, instruction->operands[0], &source_register))
    {
        // Aliased through the pointer chain: the dereference is a
        // name for the promoted local, not code.
        if (result_register != source_register)
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                           .opcode = MACHINE_X64_MOV_RR,
                                                       });
            machine_x64_define(selector, result_register, row);
        }
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_global_address(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;

    bool selected = false;
    IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
    // Thread-local addresses select only where the ELF sequences apply;
    // other OSes keep the canonical fallback.
    bool thread_local_supported =
        symbol && (!symbol->is_thread_local || selector->target.os == OPERATING_SYSTEM_LINUX || selector->target.os == OPERATING_SYSTEM_ANDROID);
    if (result_register != UINT32_MAX && thread_local_supported)
    {
        u32 target_index = selector->call_targets.total_count;
        IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
        *target_row = instruction->symbol;
        CodegenThreadLocalModel model = symbol->is_thread_local
                                            ? codegen_thread_local_model(selector->position_independent, symbol->is_definition)
                                            : CODEGEN_THREAD_LOCAL_LOCAL_EXEC;
        if (model == CODEGEN_THREAD_LOCAL_GENERAL_DYNAMIC)
        {
            // The address comes back from __tls_get_addr in RAX, so this
            // reads exactly like a direct call: the call row itself defines
            // nothing the allocator names, and a move off the physical
            // result register is what the value is defined by.
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .payload = target_index,
                                                 .opcode = MACHINE_X64_TLS_GENERAL_DYNAMIC,
                                             });
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX)},
                                                           .opcode = MACHINE_X64_MOV_RR,
                                                       });
            machine_x64_define(selector, result_register, row);
        }
        else
        {
            u32 row = machine_x64_select_row(selector,
                                             (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                 .payload = target_index,
                                                 .opcode = (u16)(!symbol->is_thread_local              ? MACHINE_X64_LEA_SYMBOL
                                                                 : model == CODEGEN_THREAD_LOCAL_INITIAL_EXEC ? MACHINE_X64_LEA_TLS_INITIAL_EXEC
                                                                                                              : MACHINE_X64_LEA_TLS),
                                             });
            machine_x64_define(selector, result_register, row);
        }
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_address_of(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    return result_register != UINT32_MAX && machine_x64_select_place_address(selector, instruction->operands[0], result_register);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_field(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    if (result_register != UINT32_MAX && instruction->immediate_count && instruction->immediates)
    {
        IrType* aggregate = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        u64 field_index = instruction->immediates[0];
        if (aggregate && field_index < aggregate->field_count && aggregate->fields[field_index].offset <= INT32_MAX)
        {
            // The whole member address is one row: the offset rides in the
            // frame displacement of a local, or in the lea of a pointer.
            selected = machine_x64_select_place_address_offset(selector, instruction->operands[0], result_register,
                                                               (u32)aggregate->fields[field_index].offset);
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_debug_trap(MachineX64Selector* selector)
{
    machine_x64_select_row(selector, (MachineInstruction){.opcode = MACHINE_X64_INT3});
    return true;
}

// Bit-field members of one storage unit accumulate in a register — mask to
// width, shift to position via a power-of-two multiply, or together — and the
// unit stores once. The register starts from the unit's current bytes rather
// than from zero, because a unit is not the initializer's alone: packing
// slides it back over an ordinary member and can leave two units sharing a
// byte, and a whole-unit store of an accumulator seeded with zero overwrites
// whichever of those the loop reached first. Merging is sound because the slot
// is zero-filled before any member and every bit belongs to exactly one of
// them — the same read-modify-write the canonical emitter spells as OR into
// memory.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_bit_field_unit(MachineX64Selector* selector, IrInstruction* instruction, IrType* type, u32 slot, u32 first,
                                                           u64 field_offset, u64 field_size, u16 unit_store_opcode, u8* member_emitted)
{
    bool selected = true;
    // The caller admitted only the sizes the sized store covers, so the load
    // that pairs with it is total over the same four.
    u16 unit_load_opcode = field_size == 1   ? MACHINE_X64_LOAD_PTR8
                           : field_size == 2 ? MACHINE_X64_LOAD_PTR16
                           : field_size == 4 ? MACHINE_X64_LOAD_PTR32
                                             : MACHINE_X64_LOAD_PTR64;
    // The unit's address is one row: LEA_FRAME's payload is a byte offset into
    // the slot. The pointer loads carry no displacement of their own.
    u32 unit_address = machine_x64_synthesize_register(selector);
    machine_x64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_address),
                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                         .payload = (u32)field_offset,
                                         .opcode = MACHINE_X64_LEA_FRAME,
                                     });
    u32 unit_register = machine_x64_synthesize_register(selector);
    machine_x64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register),
                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_address)},
                                         .opcode = unit_load_opcode,
                                     });
    for (u32 sibling = first; sibling < instruction->operand_count && selected; sibling += 1)
    {
        u64 sibling_field = instruction->immediates[sibling];
        // Siblings share a unit only when they share both its start and its
        // width: packing can narrow one field's unit and not the next one's.
        if (!member_emitted[sibling] && sibling_field < type->field_count && type->fields[sibling_field].is_bit_field &&
            type->fields[sibling_field].offset == field_offset &&
            ir_field_access_size(&selector->program->types, type->fields + sibling_field) == field_size)
        {
            u32 bit_offset = type->fields[sibling_field].bit_offset;
            u32 bit_width = type->fields[sibling_field].bit_width;
            // Read only where the lookup that fills it succeeded.
            u32 value_register = UINT32_MAX;
            selected = bit_width && bit_offset + bit_width <= field_size * 8 &&
                       machine_x64_operand_register(selector, instruction->operands[sibling], &value_register);
            if (selected)
            {
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
        }
    }
    if (selected)
    {
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register)},
                                             .payload = (u32)field_offset,
                                             .opcode = unit_store_opcode,
                                         });
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_aggregate(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    // Field-by-field construction into the value's slot, mirroring the
    // canonical path: scalar members store sized at their offsets,
    // aggregate members copy from their own slots through the field's
    // address.
    bool selected = true;
    IrType* type = ir_type_from_id(&program->types, function->values[instruction->result.value].canonical_type);
    u32 slot = selector->value_stack_slots[instruction->result.value];
    bool shaped = type && (type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION) && slot != UINT32_MAX &&
                  instruction->immediate_count == instruction->operand_count && (!instruction->operand_count || instruction->immediates) &&
                  type->layout.resolved && type->layout.size <= INT32_MAX;
    if (!shaped)
    {
        selected = false;
    }
    else
    {
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
        for (u32 index = 0; index < instruction->operand_count && selected; index += 1)
        {
            u64 field_index = instruction->immediates[index];
            if (field_index >= type->field_count)
            {
                selected = false;
            }
            else if (!member_emitted[index])
            {
                u64 field_offset = type->fields[field_index].offset;
                // The unit a member is written through: a bit-field packing
                // left no room for a declared-type unit of carries a narrower
                // one, and every other field reads its own size back.
                u64 field_size = ir_field_access_size(&program->types, type->fields + field_index);
                u16 unit_store_opcode = field_size == 1   ? MACHINE_X64_STORE_FRAME8
                                        : field_size == 2 ? MACHINE_X64_STORE_FRAME16
                                        : field_size == 4 ? MACHINE_X64_STORE_FRAME32
                                        : field_size == 8 ? MACHINE_X64_STORE_FRAME64
                                                          : 0;
                if (!field_size || field_offset > INT32_MAX)
                {
                    selected = false;
                }
                else if (!type->fields[field_index].is_bit_field)
                {
                    selected = machine_x64_select_member_write(selector, slot, field_offset, field_size, instruction->operands[index]);
                    if (selected)
                    {
                        member_emitted[index] = 1;
                    }
                }
                else
                {
                    selected = unit_store_opcode && machine_x64_select_bit_field_unit(selector, instruction, type, slot, index, field_offset, field_size,
                                                                                      unit_store_opcode, member_emitted);
                }
            }
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_array(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    // Element-by-element construction into the value's slot at scaled
    // offsets; the frontend materializes every element including the
    // zero tail, so position times element size covers the object.
    bool selected = false;
    IrType* type = ir_type_from_id(&program->types, function->values[instruction->result.value].canonical_type);
    u32 slot = selector->value_stack_slots[instruction->result.value];
    if (type && type->kind == IR_TYPE_ARRAY && slot != UINT32_MAX)
    {
        IrType* element_type = ir_type_from_id(&program->types, type->element_type);
        u64 element_size = element_type && element_type->layout.resolved ? element_type->layout.size : 0;
        if (element_size && (u64)instruction->operand_count * element_size <= INT32_MAX)
        {
            selected = true;
            for (u32 index = 0; index < instruction->operand_count && selected; index += 1)
            {
                selected = machine_x64_select_member_write(selector, slot, (u64)index * element_size, element_size, instruction->operands[index]);
            }
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_index(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 index_register;
    if (result_register != UINT32_MAX && instruction->operand_count >= 2 &&
        machine_x64_operand_register(selector, instruction->operands[1], &index_register))
    {
        IrType* index_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
        IrType* element = ir_type_from_id(&program->types, instruction->canonical_type);
        bool typed = index_type && index_type->kind == IR_TYPE_INTEGER && element && element->layout.resolved && element->layout.size <= INT32_MAX;
        // The index extend resolves before the base address: an unsupported
        // index width then fails without emitting rows the rejection would
        // throw away anyway.
        u16 extend_opcode = MACHINE_X64_MOV_RR;
        if (typed && index_type->is_signed && index_type->bit_width < 64)
        {
                    extend_opcode = (u16)(index_type->bit_width == 8 ? MACHINE_X64_MOVSX8_RR : index_type->bit_width == 16 ? MACHINE_X64_MOVSX16_RR
                                          : index_type->bit_width == 32 ? MACHINE_X64_MOVSX32_RR : 0);
                }
                if (typed && extend_opcode && machine_x64_select_place_address(selector, instruction->operands[0], result_register))
                {
                // The scaled index is computed in a synthesized temporary so the
                // read-modify-write multiply never touches the index value's slot.
                u32 scaled_register = machine_x64_synthesize_register(selector);
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
            selected = true;
        }
    }
    return selected;
}

// A place whose address has to be materialized in a register: an explicit
// pointer expression, or a local the classifier gave indirect storage.
BUSTER_GLOBAL_LOCAL bool machine_x64_place_is_addressed(MachineX64Selector* selector, IrValueId place, IrInstruction* definition)
{
    return definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
           definition->opcode == IR_OPCODE_FIELD || machine_x64_local_is_indirect(selector, place);
}

// The virtual register a direct local was promoted into, or UINT32_MAX when the
// local lives in a frame slot instead.
BUSTER_GLOBAL_LOCAL u32 machine_x64_promoted_local_register(MachineX64Selector* selector, IrValueId place, IrInstruction* definition)
{
    u32 promoted = UINT32_MAX;
    if (definition->opcode == IR_OPCODE_LOCAL && !machine_x64_local_is_indirect(selector, place))
    {
        promoted = selector->value_virtual_registers[place.value];
    }
    return promoted;
}

// Aligned x86 loads are already atomic; the atomic form only excludes the
// aggregate and vector paths.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_load(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrFunction* function = selector->function;

    bool selected = false;

    IrValueId value_id = instruction->operands[0];
    IrIdUnderlying index = value_id.value;

    bool value_valid = (index < function->value_count) & (instruction->result.value != IR_ID_UNDERLYING_INVALID);
    if (value_valid)
    {
        IrValue* place = &function->values[index];

        if (place->definition.value < function->instruction_count)
        {
            IrProgram* program = selector->program;

            IrInstruction* definition = function->instructions + place->definition.value;
            u32 slot = selector->value_stack_slots[index];
            u32 result_slot = selector->value_stack_slots[instruction->result.value];
            IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);

            bool result_register_valid = result_register != UINT32_MAX;
            bool is_vector_load = result_register_valid & machine_x64_type_is_vector_register(loaded_type);
            bool is_scalar_load = !is_vector_load & result_register_valid;
            bool is_aggregate_load = !is_vector_load & !is_scalar_load & (result_slot != UINT32_MAX);
            bool splat_supported = machine_x64_simd_supported(selector->target, IR_SIMD_SPLAT_BYTE);

            bool register_load_supported = is_scalar_load |
                                           (is_vector_load & (instruction->opcode != IR_OPCODE_ATOMIC_LOAD) & splat_supported);
            if (register_load_supported)
            {
                // Whole-vector load into a ZMM-class register: a promoted vector local aliases
                // or copies, a slot-backed one loads from its frame home, and everything else
                // goes through an address vreg.

                // A promoted scalar local reads as a register, a direct local as a
                // frame load, and anything address-shaped as a sized pointer load.
                u32 definition_opcode = definition->opcode;
                u32 operand_register = selector->value_virtual_registers[index];
                bool operand_register_valid = operand_register != UINT32_MAX;
                bool direct_local = definition_opcode == IR_OPCODE_LOCAL;
                bool indirect_local = selector->value_indirect_slots[index] != UINT32_MAX;
                bool promoted_valid = direct_local & !indirect_local & operand_register_valid;
                bool local_slot_valid = direct_local & !indirect_local & !operand_register_valid & (slot != UINT32_MAX);
                u64 addressed_opcode_mask = ((u64)1 << IR_OPCODE_DEREFERENCE) | ((u64)1 << IR_OPCODE_GLOBAL) |
                                              ((u64)1 << IR_OPCODE_INDEX) | ((u64)1 << IR_OPCODE_FIELD);
                bool place_is_addressed = indirect_local | ((addressed_opcode_mask >> definition_opcode) & 1);
                bool pointer_valid = place_is_addressed & operand_register_valid;
                bool source_valid = promoted_valid | local_slot_valid | pointer_valid;
                bool select_and_define = source_valid & !(promoted_valid & (result_register == operand_register));

                selected = source_valid;

                if (select_and_define)
                {
                    BUSTER_CHECK(loaded_type);
                    BUSTER_CHECK(loaded_type->layout.resolved);
                    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(loaded_type->layout.size));

                    u64 size = loaded_type->layout.size;
                    u32 size_log2 = trailing_zeroes_u64(size);
                    u16 scalar_load_opcode = (u16)(MACHINE_X64_LOAD_PTR8 + size_log2);
                    BUSTER_CHECK(is_vector_load || (scalar_load_opcode >= MACHINE_X64_LOAD_PTR8 && scalar_load_opcode <= MACHINE_X64_LOAD_PTR64));
                    u16 pointer_opcode = is_vector_load ? MACHINE_X64_VLOAD_PTR : scalar_load_opcode;
                    u16 local_opcode = is_vector_load ? MACHINE_X64_VLOAD_FRAME : MACHINE_X64_LOAD_FRAME;
                    u16 promoted_opcode = is_vector_load ? MACHINE_X64_VMOV_RR : MACHINE_X64_MOV_RR;
                    u16 opcode = local_slot_valid ? local_opcode : pointer_opcode;
                    opcode = promoted_valid ? promoted_opcode : opcode;

                    MachineRef destination = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register);
                    u32 source_payload = local_slot_valid ? slot : operand_register;
                    MachineRefKind source_kind = local_slot_valid ? MACHINE_REF_STACK_SLOT : MACHINE_REF_VIRTUAL_REGISTER;
                    MachineRef source = machine_ref_make(source_kind, source_payload);
                    u32 row = machine_x64_select_row(selector, (MachineInstruction){ .operands = {destination, source}, .opcode = opcode });
                    machine_x64_define(selector, result_register, row);

                    // A frame load is deliberately a full eight-byte read so
                    // that every direct local uses one compact slot shape.
                    // Narrow objects (notably _Bool values written through a
                    // pointer) occupy only their declared byte width, though;
                    // reading the untouched upper bytes would let stale stack
                    // data escape as part of the scalar value.  Normalize the
                    // register after a direct frame load just as the pointer
                    // load forms do, preserving C's integer/boolean width.
                    u64 unsigned_narrow_opcodes = (u64)MACHINE_X64_MOVZX8_RR | ((u64)MACHINE_X64_MOVZX16_RR << 8) |
                                                  ((u64)MACHINE_X64_MOV32_RR << 16);
                    u64 signed_narrow_opcodes = (u64)MACHINE_X64_MOVSX8_RR | ((u64)MACHINE_X64_MOVSX16_RR << 8) |
                                                ((u64)MACHINE_X64_MOV32_RR << 16);
                    bool integer = loaded_type->kind == IR_TYPE_INTEGER;
                    bool narrow_scalar = integer | (loaded_type->kind == IR_TYPE_BOOLEAN);
                    u64 narrow_opcodes = (integer & loaded_type->is_signed) ? signed_narrow_opcodes : unsigned_narrow_opcodes;
                    narrow_opcodes &= 0 - (u64)narrow_scalar;
                    u16 narrow_opcode = (u16)((narrow_opcodes >> (size_log2 * 8)) & UINT8_MAX);
                    bool normalize_frame_load = local_slot_valid & (narrow_opcode != 0);
                    if (normalize_frame_load)
                    {
                        machine_x64_select_row(selector, (MachineInstruction){
                                                             .operands = {destination, destination},
                                                             .opcode = narrow_opcode,
                                                         });
                    }

                }
            }
            else if (is_aggregate_load)
            {
                // An aggregate load is an exact-size chunk copy into the result slot,
                // from a direct local's slot or through an address vreg.
                BUSTER_CHECK(loaded_type);
                bool aggregate_supported = (instruction->opcode != IR_OPCODE_ATOMIC_LOAD) & loaded_type->layout.resolved & (loaded_type->layout.size <= UINT32_MAX);
                if (aggregate_supported)
                {
                    bool local_slot_valid = (definition->opcode == IR_OPCODE_LOCAL) & (slot != UINT32_MAX);
                    bool place_is_addressed = machine_x64_place_is_addressed(selector, value_id, definition) & !local_slot_valid;

                    u32 address_register;
                    bool address_register_selected = machine_x64_operand_register(selector, value_id, &address_register) & place_is_addressed;
                    selected = local_slot_valid | address_register_selected;

                    if (selected)
                    {
                        MachineRef destination = machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot);
                        u32 source_payload = local_slot_valid ? slot : address_register;
                        MachineRefKind source_kind = local_slot_valid ? MACHINE_REF_STACK_SLOT : MACHINE_REF_VIRTUAL_REGISTER;
                        MachineRef source = machine_ref_make(source_kind, source_payload);
                        u16 opcode = local_slot_valid ? MACHINE_X64_COPY_FRAME_FROM_FRAME : MACHINE_X64_COPY_FRAME_FROM_PTR;

                        machine_x64_select_row(selector, (MachineInstruction){
                                                             .operands = {destination, source},
                                                             .payload = (u32)loaded_type->layout.size,
                                                             .opcode = opcode,
                                                         });
                    }
                }
            }
        }
    }

    return selected;
}

// Sequentially consistent stores exchange, exactly like the canonical path;
// weaker orders are plain x86 stores.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_sequential_atomic_store(MachineX64Selector* selector, IrInstruction* instruction, u64 size)
{
    bool selected = false;
    u32 atomic_value_register;
    if (machine_x64_operand_register(selector, instruction->operands[1], &atomic_value_register) &&
        (size == 1 || size == 2 || size == 4 || size == 8))
    {
        u32 address_register = machine_x64_synthesize_register(selector);
        selected = machine_x64_select_place_address(selector, instruction->operands[0], address_register);
        if (selected)
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, atomic_value_register)},
                                                 .payload = (u32)size,
                                                 .opcode = MACHINE_X64_ATOMIC_STORE_XCHG,
                                             });
        }
    }
    return selected;
}

// The aggregate form of a store: an exact-size chunk copy out of the value
// slot, into a direct local's slot or through an address vreg.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_aggregate_store(MachineX64Selector* selector, IrInstruction* instruction, IrInstruction* definition, u64 size,
                                                            u32 slot, u32 value_slot)
{
    bool selected = false;
    if (instruction->opcode != IR_OPCODE_ATOMIC_STORE && size && size <= UINT32_MAX)
    {
        if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                 .payload = (u32)size,
                                                 .opcode = MACHINE_X64_COPY_FRAME_FROM_FRAME,
                                             });
            selected = true;
        }
        else if (machine_x64_place_is_addressed(selector, instruction->operands[0], definition))
        {
            u32 address_register;
            selected = machine_x64_operand_register(selector, instruction->operands[0], &address_register);
            if (selected)
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = (u32)size,
                                                     .opcode = MACHINE_X64_COPY_PTR_FROM_FRAME,
                                                 });
            }
        }
    }
    return selected;
}

// Whole-vector store out of a ZMM-class register: a promoted vector local takes
// a full 512-bit register copy, a slot-backed one stores to its frame home, and
// everything else goes through an address vreg.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_vector_store(MachineX64Selector* selector, IrInstruction* instruction, IrInstruction* definition, u32 slot,
                                                         u32 value_register)
{
    bool selected = false;
    u32 place_register = machine_x64_promoted_local_register(selector, instruction->operands[0], definition);
    if (instruction->opcode != IR_OPCODE_ATOMIC_STORE && machine_x64_simd_supported(selector->target, IR_SIMD_SPLAT_BYTE))
    {
        if (place_register != UINT32_MAX)
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, place_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                           .opcode = MACHINE_X64_VMOV_RR,
                                                       });
            machine_x64_define(selector, place_register, row);
            selected = true;
        }
        else if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                 .opcode = MACHINE_X64_VSTORE_FRAME,
                                             });
            selected = true;
        }
        else if (machine_x64_place_is_addressed(selector, instruction->operands[0], definition))
        {
            u32 address_register;
            selected = machine_x64_operand_register(selector, instruction->operands[0], &address_register);
            if (selected)
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_X64_VSTORE_PTR,
                                                 });
            }
        }
    }
    return selected;
}

// The scalar form: a promoted local takes a register copy, a direct local a
// full-slot frame store, and anything address-shaped a sized pointer store.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_scalar_store(MachineX64Selector* selector, IrInstruction* instruction, IrInstruction* definition, u64 size,
                                                         u32 slot, u32 value_register)
{
    bool selected = false;
    u32 size_index = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : size == 8 ? 3 : UINT32_MAX;
    u32 place_register = machine_x64_promoted_local_register(selector, instruction->operands[0], definition);
    if (size_index != UINT32_MAX)
    {
        if (place_register != UINT32_MAX)
        {
            // Promoted local: the store is a full-width register copy —
            // the same 64-bit image a direct-slot store writes, since the
            // register model keeps every value zero-extended.
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, place_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                           .opcode = MACHINE_X64_MOV_RR,
                                                       });
            machine_x64_define(selector, place_register, row);
            selected = true;
        }
        else if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
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
            selected = true;
        }
        else if (machine_x64_place_is_addressed(selector, instruction->operands[0], definition))
        {
            u32 address_register;
            selected = machine_x64_operand_register(selector, instruction->operands[0], &address_register);
            if (selected)
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = (u16)(MACHINE_X64_STORE_PTR8 + size_index),
                                                 });
            }
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_store(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    if (instruction->operands[0].value < function->value_count && instruction->operands[1].value < function->value_count)
    {
        IrValue* place = function->values + instruction->operands[0].value;
        if (place->definition.value < function->instruction_count)
        {
            IrInstruction* definition = function->instructions + place->definition.value;
            IrType* stored_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
            u64 size = stored_type && stored_type->layout.resolved ? stored_type->layout.size : 0;
            u32 slot = selector->value_stack_slots[instruction->operands[0].value];
            u32 value_slot = selector->value_stack_slots[instruction->operands[1].value];
            if (instruction->opcode == IR_OPCODE_ATOMIC_STORE && instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL)
            {
                selected = machine_x64_select_sequential_atomic_store(selector, instruction, size);
            }
            else if (value_slot != UINT32_MAX && selector->value_virtual_registers[instruction->operands[1].value] == UINT32_MAX)
            {
                selected = machine_x64_select_aggregate_store(selector, instruction, definition, size, slot, value_slot);
            }
            else
            {
                u32 value_register;
                if (machine_x64_operand_register(selector, instruction->operands[1], &value_register))
                {
                    // Spelled as a branch rather than a conditional operand:
                    // both arms emit rows, and `ide` evaluates both arms of a
                    // conditional in some operand positions.
                    if (machine_x64_type_is_vector_register(stored_type))
                    {
                        selected = machine_x64_select_vector_store(selector, instruction, definition, slot, value_register);
                    }
                    else
                    {
                        selected = machine_x64_select_scalar_store(selector, instruction, definition, size, slot, value_register);
                    }
                }
            }
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_function(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    // A function reference is an ordinary rip-relative symbol address;
    // direct calls carry the symbol on the CALL row itself, so this lea
    // only matters when the value is used as data.
    bool selected = false;
    if (result_register != UINT32_MAX && instruction->symbol.value != IR_ID_UNDERLYING_INVALID)
    {
        u32 target_index = selector->call_targets.total_count;
        IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
        *target_row = instruction->symbol;
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                       .payload = target_index,
                                                       .opcode = MACHINE_X64_LEA_SYMBOL,
                                                   });
        machine_x64_define(selector, result_register, row);
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_simd(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    // The 512-bit vocabulary, one EVEX row per operation with values
    // register-resident — the canonical path's per-operand frame
    // round-trips are exactly what this subset removes. Masks arrive
    // and leave in general registers; rows that consume or produce one
    // stage through k1 inside the encoder.
    IrSimdOperation operation = (IrSimdOperation)instruction->simd_operation;
    IrSimdShape shape = ir_simd_operation_shape(operation);
    bool selected = machine_x64_simd_supported(selector->target, operation) && instruction->operand_count == shape.operand_count &&
                    instruction->immediate_count == shape.immediate_count && (!shape.immediate_count || instruction->immediates) &&
                    (!shape.has_result || result_register != UINT32_MAX);
    u32 operand_registers[4];
    for (u32 operand_index = 0; operand_index < instruction->operand_count && selected; operand_index += 1)
    {
        selected = machine_x64_operand_register(selector, instruction->operands[operand_index], operand_registers + operand_index);
    }
    if (selected)
    {
        // Read after the verdict: a shape that wants an immediate has already
        // been rejected here if the instruction carries no immediate array.
        u32 immediate = shape.immediate_count ? (u32)instruction->immediates[0] : 0;
        switch (operation)
        {
        case IR_SIMD_LOAD:
        case IR_SIMD_LOAD_MASKED:
        {
            bool masked = operation == IR_SIMD_LOAD_MASKED;
            // The mask ref is built before the row, not inside it: only the masked
            // shape has a second operand, so an unmasked load must not reach
            // operand_registers[1] at all.
            MachineRef mask_operand = masked ? machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1]) : 0;
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0]), mask_operand},
                                                           .opcode = masked ? MACHINE_X64_VLOAD_PTR_MASKED : MACHINE_X64_VLOAD_PTR,
                                                       });
            machine_x64_define(selector, result_register, row);
            break;
        }
        case IR_SIMD_STORE:
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0]),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1])},
                                                 .opcode = MACHINE_X64_VSTORE_PTR,
                                             });
            break;
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
            break;
        }
        case IR_SIMD_SPLAT_BYTE:
        case IR_SIMD_SPLAT_WORD:
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0])},
                                                           .opcode = operation == IR_SIMD_SPLAT_BYTE ? MACHINE_X64_VSPLATB : MACHINE_X64_VSPLATD,
                                                       });
            machine_x64_define(selector, result_register, row);
            break;
        }
        case IR_SIMD_COMPARE_EQUAL_BYTE:
        case IR_SIMD_COMPARE_LESS_BYTE:
        case IR_SIMD_TEST_MASK_BYTE:
        case IR_SIMD_COMPARE_EQUAL_WORD:
        case IR_SIMD_COMPARE_LESS_WORD:
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0]),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1])},
                                                           .payload = operation == IR_SIMD_COMPARE_EQUAL_BYTE  ? 0u
                                                                      : operation == IR_SIMD_COMPARE_LESS_BYTE  ? 1u
                                                                      : operation == IR_SIMD_TEST_MASK_BYTE     ? 2u
                                                                      : operation == IR_SIMD_COMPARE_EQUAL_WORD ? 3u
                                                                                                                : 4u,
                                                           .opcode = MACHINE_X64_VPCMP_MASK,
                                                       });
            machine_x64_define(selector, result_register, row);
            break;
        }
        case IR_SIMD_SIGN_MASK_BYTE:
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0])},
                                                           .opcode = MACHINE_X64_VPMOVB2M,
                                                       });
            machine_x64_define(selector, result_register, row);
            break;
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
            break;
        }
        case IR_SIMD_COMPRESS_BYTE:
        case IR_SIMD_COMPRESS_WORD:
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[0]),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_registers[1])},
                                                           .payload = operation == IR_SIMD_COMPRESS_BYTE ? 0u : 1u,
                                                           .opcode = MACHINE_X64_VCOMPRESSB,
                                                       });
            machine_x64_define(selector, result_register, row);
            break;
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
            break;
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
            break;
        }
        case IR_SIMD_COUNT:
            selected = false;
            break;
        }
    }
    return selected;
}

// Everything CALL selection resolves before it emits a row: who is called,
// where each argument travels, and how the result comes back.
typedef struct MachineX64CallPlan MachineX64CallPlan;
struct MachineX64CallPlan
{
    MachineX64ValueShape argument_shapes[MACHINE_X64_MAX_ARGUMENTS];
    MachineX64ArgumentPlacement argument_placements[MACHINE_X64_MAX_ARGUMENTS];
    u32 argument_registers[MACHINE_X64_MAX_ARGUMENTS];
    u32 argument_slots[MACHINE_X64_MAX_ARGUMENTS];
    MachineX64ValueShape return_shape;
    u32 argument_count;
    u32 callee_register;
    u32 indirect_result_slot;
    u32 integer_count;
    u32 float_count;
    u32 stack_part_count;
    bool stack_padding;
    bool windows_call;
    bool direct_call;
    bool variadic_call;
    bool returns_value;
};

// Resolves the call against the callee's signature and the ABI's argument
// placement. The only rows it emits are the backing store for an unused
// indirect result and the bounce slots the staging below reads.
BUSTER_GLOBAL_LOCAL bool machine_x64_plan_call(MachineX64Selector* selector, IrInstruction* instruction, MachineX64CallPlan* plan)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    *plan = (MachineX64CallPlan){.callee_register = UINT32_MAX, .indirect_result_slot = UINT32_MAX};
    IrType* callee_type = 0;
    bool planned = instruction->operand_count != 0 && instruction->operands[0].value < function->value_count;
    if (planned)
    {
        IrValue* callee = function->values + instruction->operands[0].value;
        planned = callee->definition.value < function->instruction_count;
        if (planned)
        {
            plan->direct_call = function->instructions[callee->definition.value].opcode == IR_OPCODE_FUNCTION &&
                                instruction->symbol.value != IR_ID_UNDERLYING_INVALID;
            planned = plan->direct_call || machine_x64_operand_register(selector, instruction->operands[0], &plan->callee_register);
            callee_type = ir_type_from_id(&program->types, callee->canonical_type);
            if (callee_type && callee_type->kind == IR_TYPE_POINTER)
            {
                callee_type = ir_type_from_id(&program->types, callee_type->element_type);
            }
        }
    }
    if (planned)
    {
        plan->argument_count = instruction->operand_count - 1;
        plan->variadic_call = callee_type && callee_type->kind == IR_TYPE_FUNCTION && callee_type->is_variadic;
        plan->windows_call = machine_x64_target_is_windows(selector->target);
        // Win64's variadic protocol duplicates float arguments into the
        // integer registers and has no AL count; the subset keeps those
        // calls canonical, matching the definition-side rule.
        planned = callee_type && callee_type->kind == IR_TYPE_FUNCTION &&
                  (plan->variadic_call ? plan->argument_count >= callee_type->parameter_count
                                       : callee_type->parameter_count == plan->argument_count) &&
                  plan->argument_count <= MACHINE_X64_MAX_ARGUMENTS && !(plan->windows_call && plan->variadic_call);
    }
    if (planned)
    {
        IrType* callee_return_type = ir_type_from_id(&program->types, callee_type->return_type);
        plan->returns_value = callee_return_type && callee_return_type->kind != IR_TYPE_VOID;
        planned = !plan->returns_value ||
                  machine_x64_value_shape(program, callee_type->return_type, IR_ABI_USE_RESULT, selector->target, &plan->return_shape);
    }
    if (planned)
    {
        plan->integer_count = plan->return_shape.indirect ? 1 : 0;
        plan->float_count = plan->return_shape.indirect && plan->windows_call ? 1 : 0;
        if (plan->return_shape.indirect)
        {
            if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
            {
                plan->indirect_result_slot = selector->value_stack_slots[instruction->result.value];
                planned = plan->indirect_result_slot != UINT32_MAX;
            }
            else
            {
                // An unused indirect result still needs backing storage.
                plan->indirect_result_slot = machine_x64_append_slot(selector, plan->return_shape.byte_size, 8);
            }
        }
    }
    // Every argument — fixed and variadic — must be integer-class under the
    // subset's shapes; a variadic call then always passes zero vector
    // registers in AL, matching the canonical convention. Aggregate variadic
    // tails stay outside the subset.
    for (u32 argument_index = 0; argument_index < plan->argument_count && planned; argument_index += 1)
    {
        IrTypeId argument_type_id = argument_index < callee_type->parameter_count
                                        ? callee_type->parameter_types[argument_index]
                                        : function->values[instruction->operands[argument_index + 1].value].canonical_type;
        // A variadic call's AL protocol would have to count vector registers.
        planned = machine_x64_value_shape(program, argument_type_id, IR_ABI_USE_ARGUMENT, selector->target, plan->argument_shapes + argument_index) &&
                  !(plan->variadic_call && plan->argument_shapes[argument_index].vector);
        if (planned)
        {
            u32 tight_stack_parts = plan->stack_part_count;
            machine_x64_place_argument(plan->argument_shapes + argument_index, plan->windows_call, &plan->integer_count, &plan->float_count,
                                       &plan->stack_part_count, plan->argument_placements + argument_index);
            // A stack vector argument whose 64-aligned offset opens a gap
            // needs padding eightbytes the push machinery cannot produce;
            // the canonical caller keeps that call.
            planned = !(plan->argument_placements[argument_index].on_stack && plan->argument_shapes[argument_index].vector &&
                        plan->argument_placements[argument_index].first_stack_part != tight_stack_parts);
        }
    }
    if (planned)
    {
        plan->stack_padding = (plan->stack_part_count & 1) != 0;
    }
    for (u32 argument_index = 0; argument_index < plan->argument_count && planned; argument_index += 1)
    {
        plan->argument_registers[argument_index] = UINT32_MAX;
        plan->argument_slots[argument_index] = selector->value_stack_slots[instruction->operands[argument_index + 1].value];
        planned = plan->argument_shapes[argument_index].aggregate
                      ? plan->argument_slots[argument_index] != UINT32_MAX
                      : machine_x64_operand_register(selector, instruction->operands[argument_index + 1], plan->argument_registers + argument_index);
    }
    return planned;
}

// Stages every argument into the register or stack slot its placement names,
// and reports the flags the call row carries: the variadic AL count, and
// whether a vector register is live across the call.
BUSTER_GLOBAL_LOCAL u16 machine_x64_stage_call_arguments(MachineX64Selector* selector, MachineX64CallPlan* plan)
{
    // A stack vector argument's register value bounces through a
    // dedicated 64-byte frame slot so the eightbyte pushes below have
    // memory to read; the store is frame-relative and free of the
    // push-moved stack pointer.
    for (u32 argument_index = 0; argument_index < plan->argument_count; argument_index += 1)
    {
        if (!plan->argument_placements[argument_index].on_stack || !plan->argument_shapes[argument_index].vector)
        {
            continue;
        }
        plan->argument_slots[argument_index] = machine_x64_append_slot(selector, 64, 16);
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, plan->argument_slots[argument_index]),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan->argument_registers[argument_index])},
                                             .opcode = MACHINE_X64_VSTORE_FRAME,
                                         });
    }
    // Win64 writes its outgoing stack arguments into the frame's own
    // area, above the four shadow eightbytes the callee owns, and leaves
    // the stack pointer alone: the area's base is where a call's stack
    // pointer already points. Every part travels through a register,
    // which the allocator is free to pick because nothing here is fixed.
    if (plan->windows_call)
    {
        u32 call_outgoing_bytes = (32u + plan->stack_part_count * 8u + 15u) & ~15u;
        if (selector->outgoing_slot == UINT32_MAX)
        {
            selector->outgoing_slot = machine_x64_append_slot(selector, call_outgoing_bytes, 16);
        }
        selector->outgoing_bytes = BUSTER_MAX(selector->outgoing_bytes, call_outgoing_bytes);
        for (u32 argument_index = 0; argument_index < plan->argument_count; argument_index += 1)
        {
            MachineX64ArgumentPlacement* argument_placement = plan->argument_placements + argument_index;
            if (!argument_placement->on_stack)
            {
                continue;
            }
            for (u32 part_index = 0; part_index < argument_placement->stack_part_count; part_index += 1)
            {
                u32 outgoing_offset = 32u + ((u32)argument_placement->first_stack_part + part_index) * 8u;
                u32 part_register = plan->argument_registers[argument_index];
                if (plan->argument_shapes[argument_index].aggregate)
                {
                    part_register = machine_x64_synthesize_register(selector);
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, part_register),
                                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, plan->argument_slots[argument_index])},
                                                         .payload = part_index * 8,
                                                         .opcode = MACHINE_X64_LOAD_FRAME,
                                                     });
                }
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, selector->outgoing_slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, part_register)},
                                                     .payload = outgoing_offset,
                                                     .opcode = MACHINE_X64_STORE_FRAME64,
                                                 });
            }
        }
    }
    // Outgoing stack parts push right to left before any register is
    // placed (the pushes scratch only RAX), with alignment padding for
    // an odd part count.
    if (!plan->windows_call && plan->stack_padding)
    {
        machine_x64_select_row(selector, (MachineInstruction){
                                             .payload = 8,
                                             .opcode = MACHINE_X64_SUB_RSP,
                                         });
    }
    for (u32 argument_reverse = plan->windows_call ? 0 : plan->argument_count; argument_reverse > 0; argument_reverse -= 1)
    {
        u32 argument_index = argument_reverse - 1;
        MachineX64ArgumentPlacement* argument_placement = plan->argument_placements + argument_index;
        if (!argument_placement->on_stack)
        {
            continue;
        }
        if (plan->argument_shapes[argument_index].aggregate || plan->argument_shapes[argument_index].vector)
        {
            for (u32 part_reverse = argument_placement->stack_part_count; part_reverse > 0; part_reverse -= 1)
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, plan->argument_slots[argument_index])},
                                                     .payload = (part_reverse - 1) * 8,
                                                     .opcode = MACHINE_X64_PUSH_FRAME,
                                                 });
            }
            continue;
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan->argument_registers[argument_index])},
                                             .opcode = MACHINE_X64_PUSH_REGISTER,
                                         });
    }
    // Explicit fixed-register argument copies, floats-first in two passes
    // like machine_x64_select_return: a float part bounces through RAX into
    // its XMM register via a freshly synthesized load whose free register
    // pick knows nothing about argument staging — crossing every float
    // before any integer argument register is written is what keeps that
    // pick from landing on (and destroying) an already placed argument.
    // Integer targets then load directly, never through a scratch. The
    // hidden result pointer takes the first integer register.
    bool call_vector_registers = false;
    for (u32 populate_pass = 0; populate_pass < 2; populate_pass += 1)
    {
        bool float_pass = populate_pass == 0;
        if (!float_pass && plan->return_shape.indirect)
        {
            u32 result_pointer_register = machine_x64_synthesize_register(selector);
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_pointer_register),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, plan->indirect_result_slot)},
                                                 .opcode = MACHINE_X64_LEA_FRAME,
                                             });
            machine_x64_select_row(selector,
                                   (MachineInstruction){
                                       .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, machine_x64_argument_register(plan->windows_call, 0)),
                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_pointer_register)},
                                       .opcode = MACHINE_X64_MOV_RR,
                                   });
        }
        for (u32 argument_index = 0; argument_index < plan->argument_count; argument_index += 1)
        {
            MachineX64ValueShape* shape = plan->argument_shapes + argument_index;
            if (plan->argument_placements[argument_index].on_stack)
            {
                continue;
            }
            u32 next_integer = plan->argument_placements[argument_index].first_integer;
            u32 next_float = plan->argument_placements[argument_index].first_float;
            if (shape->vector)
            {
                if (float_pass)
                {
                    // The whole 512-bit value takes its SSE-sequence register;
                    // the allocator relocates the source into that exact ZMM,
                    // so no later staging row can disturb an already placed one.
                    machine_x64_select_row(selector,
                                           (MachineInstruction){
                                               .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_ZMM0 + next_float),
                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan->argument_registers[argument_index])},
                                               .opcode = MACHINE_X64_VMOV_RR,
                                           });
                    call_vector_registers = true;
                }
                continue;
            }
            if (shape->aggregate)
            {
                for (u32 part_index = 0; part_index < shape->part_count; part_index += 1)
                {
                    bool part_float = shape->part_is_float[part_index] != 0;
                    if (part_float != float_pass)
                    {
                        next_integer += !part_float;
                        next_float += part_float;
                        continue;
                    }
                    if (part_float)
                    {
                        u32 bounce_register = machine_x64_synthesize_register(selector);
                        machine_x64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register),
                                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, plan->argument_slots[argument_index])},
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
                                                                                 machine_x64_argument_register(plan->windows_call, next_integer)),
                                                                machine_ref_make(MACHINE_REF_STACK_SLOT, plan->argument_slots[argument_index])},
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
                if (float_pass)
                {
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan->argument_registers[argument_index])},
                                                         .payload = next_float,
                                                         .opcode = MACHINE_X64_MOVQ_TO_XMM,
                                                     });
                }
                continue;
            }
            if (!float_pass)
            {
                u32 target_register = machine_x64_argument_register(plan->windows_call, next_integer);
                // The copy must stay the copy opcode: the allocators stage a
                // copy-into-physical's source in that same register, which is
                // the invariant keeping argument staging clobber-free. The
                // widening then runs in place, physical to physical, where
                // the allocator has nothing left to decide.
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, target_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan->argument_registers[argument_index])},
                                                     .opcode = MACHINE_X64_MOV_RR,
                                                 });
                if (shape->scalar_extend_opcode)
                {
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, target_register),
                                                                      machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, target_register)},
                                                         .opcode = shape->scalar_extend_opcode,
                                                     });
                }
            }
        }
    }
    return (u16)((plan->variadic_call ? (1u | (plan->float_count << 1)) : 0) |
                 (call_vector_registers ? MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE : 0));
}

// Collects the returned value: an indirect result is already in its slot, an
// aggregate comes back in parts, and a scalar arrives in RAX or XMM0.
BUSTER_GLOBAL_LOCAL bool machine_x64_receive_call_result(MachineX64Selector* selector, IrInstruction* instruction, MachineX64CallPlan* plan,
                                                         u32 result_register)
{
    bool received = true;
    if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
    {
        if (plan->return_shape.indirect)
        {
            // The callee already stored the value through the hidden
            // pointer into the result slot.
            received = true;
        }
        else if (plan->return_shape.aggregate)
        {
            u32 result_slot = selector->value_stack_slots[instruction->result.value];
            received = result_slot != UINT32_MAX;
            // Capture integer results before floating results: the
            // MOVQ_FROM_XMM bridge uses RAX as its fixed scratch, which
            // would otherwise destroy an integer-class return part that
            // still has to be copied from RAX.
            u32 return_integer_index = 0;
            u32 return_float_index = 0;
            for (u32 capture_pass = 0; capture_pass < 2 && received; capture_pass += 1)
            {
                bool float_pass = capture_pass == 1;
                for (u32 part_index = 0; part_index < plan->return_shape.part_count; part_index += 1)
                {
                    bool part_float = plan->return_shape.part_is_float[part_index] != 0;
                    if (part_float != float_pass)
                    {
                        continue;
                    }
                    if (!part_float)
                    {
                        machine_x64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                          machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER,
                                                                                           return_integer_index ? MACHINE_X64_RDX : MACHINE_X64_RAX)},
                                                             .payload = plan->return_shape.part_offsets[part_index],
                                                             .opcode = MACHINE_X64_STORE_FRAME64,
                                                         });
                        return_integer_index += 1;
                        continue;
                    }
                    u32 bounce_register = machine_x64_synthesize_register(selector);
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                         .payload = return_float_index,
                                                         .opcode = MACHINE_X64_MOVQ_FROM_XMM,
                                                     });
                    machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                         .payload = plan->return_shape.part_offsets[part_index],
                                                         .opcode = MACHINE_X64_STORE_FRAME64,
                                                     });
                    return_float_index += 1;
                }
            }
        }
        else if (result_register == UINT32_MAX)
        {
            received = false;
        }
        else
        {
            u32 row;
            if (plan->return_shape.vector)
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
            else if (plan->return_shape.part_is_float[0])
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
    }
    return received;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_call(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    MachineX64CallPlan plan;
    bool selected = machine_x64_plan_call(selector, instruction, &plan);
    if (selected)
    {
        u16 call_flags = machine_x64_stage_call_arguments(selector, &plan);
        if (plan.direct_call)
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
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan.callee_register)},
                                                 .opcode = MACHINE_X64_CALL_INDIRECT,
                                                 .flags = call_flags,
                                             });
        }
        u32 call_stack_release = plan.windows_call ? 0u : plan.stack_part_count * 8 + (plan.stack_padding ? 8u : 0);
        if (call_stack_release)
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .payload = call_stack_release,
                                                 .opcode = MACHINE_X64_ADD_RSP,
                                             });
        }
        selected = machine_x64_receive_call_result(selector, instruction, &plan, result_register);
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_branch(MachineX64Selector* selector, IrInstruction* instruction)
{
    machine_x64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value)},
                                         .opcode = MACHINE_X64_JMP,
                                     });
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_label_address(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    bool selected = result_register != UINT32_MAX && instruction->target_count == 1 && instruction->targets &&
                    instruction->targets[0].value < selector->function->block_count;
    if (selected)
    {
        u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                          .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                          .payload = instruction->targets[0].value,
                                                          .opcode = MACHINE_X64_LEA_BLOCK,
                                                      });
        machine_x64_define(selector, result_register, row);
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_indirect_branch(MachineX64Selector* selector, IrInstruction* instruction)
{
    bool selected = instruction->operand_count == 1 && instruction->target_count != 0 && instruction->targets;
    u32 target_register = UINT32_MAX;
    if (selected)
    {
        selected = machine_x64_operand_register(selector, instruction->operands[0], &target_register);
    }
    if (selected)
    {
        u32 first_target = selector->switch_cases.total_count;
        for (u32 target_index = 0; target_index < instruction->target_count; target_index += 1)
        {
            if (instruction->targets[target_index].value >= selector->function->block_count)
            {
                selected = false;
                break;
            }
            MachineSwitchCase* target_row = (MachineSwitchCase*)machine_stream_append(selector->arena, &selector->switch_cases);
            *target_row = (MachineSwitchCase){.target_block = instruction->targets[target_index].value};
        }
        if (selected)
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, target_register)},
                                                         .payload = first_target,
                                                         .flags = instruction->target_count,
                                                         .opcode = MACHINE_X64_INDIRECT_BRANCH,
                                                     });
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_selector_edge_exists(MachineFunctionBuilder* builder, u32 source_block, u32 destination_block)
{
    for (MachineBuilderChunk* chunk = builder->edges.first; chunk; chunk = chunk->next)
    {
        MachineEdge* edges = (MachineEdge*)(chunk + 1);
        for (u32 index = 0; index < chunk->count; index += 1)
        {
            if (edges[index].source_block == source_block && edges[index].destination_block == destination_block)
            {
                return true;
            }
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_indirect_edges(MachineX64Selector* selector)
{
    IrFunction* function = selector->function;
    bool valid = true;
    for (u32 source_block = 0; source_block < function->block_count && valid; source_block += 1)
    {
        IrBlock* source = function->blocks + source_block;
        if (source->last_instruction.value >= function->instruction_count)
        {
            continue;
        }
        IrInstruction* terminator = function->instructions + source->last_instruction.value;
        if (terminator->opcode != IR_OPCODE_INDIRECT_BRANCH)
        {
            continue;
        }
        for (u32 target_index = 0; target_index < terminator->target_count && valid; target_index += 1)
        {
            u32 destination_block = terminator->targets[target_index].value;
            if (destination_block >= function->block_count || machine_x64_selector_edge_exists(&selector->builder, source_block, destination_block))
            {
                continue;
            }
            IrBlock* destination = function->blocks + destination_block;
            u32 copy_offset = selector->builder.edge_copy_sources.total_count;
            for (IrBlockParameter* parameter = destination->first_parameter; parameter; parameter = parameter->next)
            {
                IrIncoming* incoming = parameter->first_incoming;
                while (incoming && incoming->predecessor.value != source_block)
                {
                    incoming = incoming->next;
                }
                if (!incoming || incoming->value.value >= function->value_count || selector->value_virtual_registers[incoming->value.value] == UINT32_MAX)
                {
                    valid = false;
                    break;
                }
                machine_builder_edge_copy_source(
                    &selector->builder, machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, selector->value_virtual_registers[incoming->value.value]));
            }
            if (valid)
            {
                machine_builder_edge(&selector->builder,
                                     (MachineEdge){.source_block = source_block,
                                                   .destination_block = destination_block,
                                                   .copy_offset = copy_offset,
                                                   .copy_count = (u16)destination->parameter_count});
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_atomic_read_modify_write(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;

    bool selected = false;
    u32 value_register;
    if (result_register != UINT32_MAX && instruction->operand_count >= 2 &&
        machine_x64_operand_register(selector, instruction->operands[1], &value_register) &&
        instruction->atomic_operation < IR_ATOMIC_OPERATION_COUNT)
    {
        IrType* atomic_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u64 size = atomic_type && atomic_type->layout.resolved ? atomic_type->layout.size : 0;
        if (size == 1 || size == 2 || size == 4 || size == 8)
        {
            u32 address_register = machine_x64_synthesize_register(selector);
            if (machine_x64_select_place_address(selector, instruction->operands[0], address_register))
            {
                u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                               .payload = (u32)size | ((u32)instruction->atomic_operation << 8),
                                                               .opcode = MACHINE_X64_ATOMIC_RMW,
                                                           });
                machine_x64_define(selector, result_register, row);
                selected = true;
            }
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_atomic_compare_exchange(MachineX64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    IrType* atomic_type = ir_type_from_id(&program->types, instruction->canonical_type);
    u64 size = atomic_type && atomic_type->layout.resolved ? atomic_type->layout.size : 0;
    u32 address_register = machine_x64_synthesize_register(selector);
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
        selected = target_cpu_feature_has(selector->target, TARGET_CPU_FEATURE_X86_CX16) && instruction->operand_count >= 3 &&
                   result_slot != UINT32_MAX && expected_slot != UINT32_MAX && desired_slot != UINT32_MAX &&
                   machine_x64_select_place_address(selector, instruction->operands[0], address_register);
        if (selected)
        {
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
            selector->atomic_success_registers[instruction->result.value] = success_register;
        }
    }
    else
    {
        // Both are read only where the lookups that fill them succeeded.
        u32 expected_register = UINT32_MAX;
        u32 desired_register = UINT32_MAX;
        selected = result_register != UINT32_MAX && instruction->operand_count >= 3 && (size == 1 || size == 2 || size == 4 || size == 8) &&
                   machine_x64_operand_register(selector, instruction->operands[1], &expected_register) &&
                   machine_x64_operand_register(selector, instruction->operands[2], &desired_register) &&
                   machine_x64_select_place_address(selector, instruction->operands[0], address_register);
        if (selected)
        {
            u32 row = machine_x64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, expected_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, desired_register)},
                                                           .payload = (u32)size,
                                                           .opcode = MACHINE_X64_ATOMIC_CMPXCHG,
                                                       });
            machine_x64_define(selector, result_register, row);
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_atomic_fence(MachineX64Selector* selector, IrInstruction* instruction)
{
    if (!instruction->atomic_signal_fence && instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL)
    {
        machine_x64_select_row(selector, (MachineInstruction){
                                             .opcode = MACHINE_X64_MFENCE,
                                         });
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_unreachable(MachineX64Selector* selector)
{
    // Control never reaches this terminator; ud2 keeps the block
    // verifier-well-formed, faults loudly if control ever arrives, and
    // matches the canonical bytes.
    machine_x64_select_row(selector, (MachineInstruction){
                                         .opcode = MACHINE_X64_UD2,
                                     });
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_switch(MachineX64Selector* selector, IrInstruction* instruction)
{
    u32 condition_register;
    bool selected = false;
    if (machine_x64_operand_register(selector, instruction->operands[0], &condition_register) && instruction->target_count &&
        instruction->target_count == instruction->immediate_count + 1 && instruction->immediates)
    {
        u16 compare_width = 64;
        if (instruction->operands[0].value < selector->function->value_count)
        {
            IrType* condition_type = ir_type_from_id(&selector->program->types,
                                                      selector->function->values[instruction->operands[0].value].canonical_type);
            if (condition_type && (condition_type->kind == IR_TYPE_BOOLEAN ||
                                   (condition_type->kind == IR_TYPE_INTEGER && condition_type->bit_width <= 32)))
            {
                compare_width = 32;
            }
        }
        u32 first_case = selector->switch_cases.total_count;
        for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
        {
            MachineSwitchCase* case_row = (MachineSwitchCase*)machine_stream_append(selector->arena, &selector->switch_cases);
            *case_row = (MachineSwitchCase){
                .value = instruction->immediates[case_index],
                .target_block = instruction->targets[case_index].value,
                .compare_width = compare_width,
            };
        }
        machine_x64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, condition_register),
                                                          machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[instruction->target_count - 1].value)},
                                             .payload = first_case,
                                             .opcode = MACHINE_X64_SWITCH,
                                             .flags = (u16)instruction->immediate_count,
                                         });
        selected = true;
    }
    return selected;
}

// A fused condition re-selects the chain's innermost comparison here,
// immediately before JCC: only allocator edits can land between the flags
// define and its use, and every edit form is a flag-preserving mov (frame
// load/store, reg copy, movabs remat).
BUSTER_GLOBAL_LOCAL bool machine_x64_select_fused_branch(MachineX64Selector* selector, IrInstruction* instruction, MachineX64BranchFusion* fusion)
{
    bool selected = false;
    u32 left_register;
    u32 right_register;
    if (machine_x64_operand_register(selector, (IrValueId){.value = fusion->left}, &left_register))
    {
        selected = true;
        if (fusion->right != UINT32_MAX)
        {
            selected = machine_x64_operand_register(selector, (IrValueId){.value = fusion->right}, &right_register);
            if (selected)
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                     .opcode = (u16)(fusion->wide ? MACHINE_X64_CMP64 : MACHINE_X64_CMP32),
                                                 });
            }
        }
        else
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register)},
                                                 .opcode = MACHINE_X64_TEST_RR,
                                             });
        }
        if (selected)
        {
            machine_x64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value),
                                                              machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[1].value)},
                                                 .payload = fusion->condition,
                                                 .opcode = MACHINE_X64_JCC,
                                             });
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_branch_if(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;

    bool selected = false;
    MachineX64BranchFusion* fusion =
        instruction->operands[0].value < function->value_count ? selector->branch_fusions + instruction->operands[0].value : 0;
    if (fusion && fusion->condition)
    {
        selected = machine_x64_select_fused_branch(selector, instruction, fusion);
    }
    else
    {
        u32 condition_register;
        if (machine_x64_operand_register(selector, instruction->operands[0], &condition_register))
        {
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
            selected = true;
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_select_return(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;

    bool selected = true;
    if (instruction->operand_count)
    {
        u32 value_slot = instruction->operands[0].value < function->value_count ? selector->value_stack_slots[instruction->operands[0].value] : UINT32_MAX;
        if (selector->return_shape.indirect)
        {
            selected = value_slot != UINT32_MAX && selector->hidden_return_slot != UINT32_MAX;
            if (selected)
            {
                u32 pointer_register = machine_x64_synthesize_register(selector);
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, selector->hidden_return_slot)},
                                                     .opcode = MACHINE_X64_LOAD_FRAME,
                                                 });
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = selector->return_shape.exact_byte_size ? selector->return_shape.exact_byte_size
                                                                                                       : selector->return_shape.byte_size,
                                                     .opcode = MACHINE_X64_COPY_PTR_FROM_FRAME,
                                                 });
                // The System V contract returns the hidden pointer in RAX.
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register)},
                                                     .opcode = MACHINE_X64_MOV_RR,
                                                 });
            }
        }
        else if (selector->return_shape.aggregate)
        {
            selected = value_slot != UINT32_MAX;
            if (selected)
            {
                // Populate floating return registers first: MOVQ_TO_XMM uses
                // RAX as its bridge, so integer-class parts must be loaded
                // into RAX/RDX only after every float part has crossed.
                u32 return_integer_index = 0;
                u32 return_float_index = 0;
                for (u32 populate_pass = 0; populate_pass < 2; populate_pass += 1)
                {
                    bool float_pass = populate_pass == 0;
                    for (u32 part_index = 0; part_index < selector->return_shape.part_count; part_index += 1)
                    {
                        bool part_float = selector->return_shape.part_is_float[part_index] != 0;
                        if (part_float != float_pass)
                        {
                            continue;
                        }
                        if (!part_float)
                        {
                            machine_x64_select_row(selector, (MachineInstruction){
                                                                 .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER,
                                                                                               return_integer_index ? MACHINE_X64_RDX : MACHINE_X64_RAX),
                                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                                 .payload = selector->return_shape.part_offsets[part_index],
                                                                 .opcode = MACHINE_X64_LOAD_FRAME,
                                                             });
                            return_integer_index += 1;
                            continue;
                        }
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
                    }
                }
            }
        }
        else
        {
            u32 value_register;
            selected = machine_x64_operand_register(selector, instruction->operands[0], &value_register);
            if (selected && selector->return_shape.vector)
            {
                // The 512-bit result leaves in ZMM0 and stays live through
                // the return row, whose vzeroupper the flag below suppresses.
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_ZMM0),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_X64_VMOV_RR,
                                                 });
            }
            else if (selected && selector->return_shape.part_is_float[0])
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_X64_MOVQ_TO_XMM,
                                                 });
            }
            else if (selected)
            {
                machine_x64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_X64_MOV_RR,
                                                 });
            }
        }
    }
    if (selected)
    {
        machine_x64_select_row(selector, (MachineInstruction){
                                             .opcode = MACHINE_X64_RET,
                                             .flags = (u16)(instruction->operand_count && selector->return_shape.vector
                                                                ? MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE
                                                                : 0),
                                         });
    }
    return selected;
}

// The rows the alias sweeps and the branch-fusion pass act on.  Everything
// else those passes visited only to fall through, so the prepass walk records
// where these sit and the two passes read that list instead of the rows.
#define MACHINE_X64_CANDIDATE_OPCODES                                                                                                  \
    (IR_OPCODE_BIT(IR_OPCODE_LOAD) | IR_OPCODE_BIT(IR_OPCODE_STORE) | IR_OPCODE_BIT(IR_OPCODE_DEREFERENCE) |                           \
     IR_OPCODE_BIT(IR_OPCODE_BRANCH_IF))

MachineSelectResult machine_select_canonical_function_x86_64(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                              bool position_independent, bool assume_validated)
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
    bool windows_abi = machine_x64_target_is_windows(target);
    u32 signature_integer_count = signature_return_shape.indirect ? 1 : 0;
    u32 signature_float_count = signature_return_shape.indirect && windows_abi ? 1 : 0;
    u32 signature_stack_count = 0;
    for (u32 parameter_index = 0; parameter_index < function_type->parameter_count; parameter_index += 1)
    {
        if (!machine_x64_value_shape(program, function_type->parameter_types[parameter_index], IR_ABI_USE_ARGUMENT, target,
                                     signature_parameter_shapes + parameter_index))
        {
            return result;
        }
        machine_x64_place_argument(signature_parameter_shapes + parameter_index, windows_abi, &signature_integer_count, &signature_float_count,
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
        .position_independent = position_independent,
        .supported = true,
        .failed_opcode = IR_OPCODE_COUNT,
    };
    if (!assume_validated && !machine_selection_prepass_build_minimal(arena, program, function).valid)
    {
        return result;
    }
    MachineSelectionValueFacts value_facts = machine_selection_value_facts_allocate(arena, function->value_count);
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
    selector.outgoing_slot = UINT32_MAX;
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
            value_facts.definition_blocks[parameter->value.value] = function->blocks[block_index].id.value;
        }
    }
    for (u32 argument_index = 0; argument_index < BUSTER_ARRAY_LENGTH(selector.argument_values); argument_index += 1)
    {
        selector.argument_values[argument_index] = IR_ID_UNDERLYING_INVALID;
    }
    // Local promotion, the mem2reg the machine path was built for: a
    // scalar local whose address never leaves a load or a store needs no
    // memory at all. Eligibility by type is derived from the stable
    // value-definition table while the target-order arrays below are
    // initialized; disqualification still happens on every use that is not
    // the place operand of a same-width scalar load or store: a field or
    // index selection, an address handed to a call, a mixed-width access,
    // and the atomic forms all keep the local in its slot. The byte size
    // is recorded so the width check needs no second type walk.
    u8* promotable_locals = arena_allocate(arena, u8, function->value_count ? function->value_count : 1);
    // Definition identity is already carried by IrValue. Accumulate its block
    // and the use facts in the target-order walk below so selection never
    // rereads the complete canonical row population solely for these facts.
    u32* value_last_use_ordinals = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* value_use_blocks = value_facts.use_blocks;
    u32* local_store_counts = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* value_use_counts = value_facts.use_counts;
    u32* value_def_blocks = value_facts.definition_blocks;
    u32* value_def_ordinals = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    // Cleared as four streams and then filled from the row side, because a
    // local is a property of its defining row and only a minority of rows
    // define one. Asking the question per value instead — read
    // `values[i].definition`, then follow it into `instructions[]` — is a
    // random 64-byte fetch per value for the two fields `opcode` and `result`,
    // and it was the single hottest line in the 2026-08-22T084855Z cache-miss
    // survey: 6,17% of the compile's DRAM fills there, 23,58% in this
    // function. The row scan reads the same bytes in address order, and the
    // walk immediately below wants them next.
    memset(promotable_locals, 0, sizeof(*promotable_locals) * function->value_count);
    memset(value_last_use_ordinals, 0, sizeof(*value_last_use_ordinals) * function->value_count);
    memset(local_store_counts, 0, sizeof(*local_store_counts) * function->value_count);
    memset(value_def_ordinals, 0, sizeof(*value_def_ordinals) * function->value_count);
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        // The value's own definition still has to agree: a popped row keeps
        // its opcode, so the round trip is what makes this the same set the
        // per-value form selected.
        if (instruction->opcode != IR_OPCODE_LOCAL || instruction->result.value >= function->value_count ||
            function->values[instruction->result.value].definition.value != instruction_index)
        {
            continue;
        }
        u32 value_index = instruction->result.value;
        IrType* local_type = ir_type_from_id(&program->types, function->values[value_index].canonical_type);
        if (machine_x64_type_is_scalar_register(local_type) && (local_type->layout.size == 4 || local_type->layout.size == 8))
        {
            promotable_locals[value_index] = (u8)local_type->layout.size;
        }
        // Vector locals promote under the same rule: a 64-byte value whose
        // address never leaves a same-width load or store lives in a ZMM-class
        // register and its accesses become vector copies, which is where the
        // kernels' named chunk variables stop round-tripping through the
        // frame.
        else if (machine_x64_type_is_vector_register(local_type) && machine_x64_simd_supported(selector.target, IR_SIMD_SPLAT_BYTE))
        {
            promotable_locals[value_index] = 64;
        }
    }
    // The one walk over the linked rows.  Besides the value facts below it
    // records how many rows each block owns and which of them carry an opcode
    // the alias sweeps and the branch-fusion pass act on, so those three
    // passes read counted arrays instead of chasing `next` again: the four
    // candidate opcodes are under a third of the rows, and the sweeps used to
    // walk all of them twice just to reach them.
    MachineSelectionRowLayout row_layout = {
        .block_row_counts = arena_allocate(arena, u32, function->block_count ? function->block_count : 1),
    };
    u32* block_candidate_counts = arena_allocate(arena, u32, function->block_count ? function->block_count : 1);
    u32* candidate_rows = arena_allocate(arena, u32, function->instruction_count ? function->instruction_count : 1);
    u32 candidate_count = 0;
    bool dense_rows = true;
    u32 walk_ordinal = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        u32 block_row_count = 0;
        u32 block_candidate_count = 0;
        for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = function->instructions[id.value].next)
        {
            IrInstruction* instruction = function->instructions + id.value;
            dense_rows &= id.value == block->first_instruction.value + block_row_count;
            if ((MACHINE_X64_CANDIDATE_OPCODES >> instruction->opcode) & 1)
            {
                candidate_rows[candidate_count] = block_row_count;
                candidate_count += 1;
                block_candidate_count += 1;
            }
            block_row_count += 1;
            walk_ordinal += 1;
            if (instruction->result.value != IR_ID_UNDERLYING_INVALID && instruction->result.value < function->value_count)
            {
                value_def_ordinals[instruction->result.value] = walk_ordinal;
                value_def_blocks[instruction->result.value] = block->id.value;
            }
            for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
            {
                u32 used = instruction->operands[operand_index].value;
                if (used >= function->value_count)
                {
                    continue;
                }
                value_use_counts[used] += 1;
                if (value_use_blocks[used] == MACHINE_SELECTION_INVALID_INDEX)
                {
                    value_use_blocks[used] = block->id.value;
                }
                else if (value_use_blocks[used] != block->id.value)
                {
                    value_use_blocks[used] = MACHINE_SELECTION_MULTIPLE_BLOCKS;
                }
                value_last_use_ordinals[used] = walk_ordinal;
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
        row_layout.block_row_counts[block_index] = block_row_count;
        block_candidate_counts[block_index] = block_candidate_count;
    }
    if (!dense_rows)
    {
        // Some block's rows are not its dense id range, so program order has
        // to be written down before the passes below can count through it.
        row_layout.rows = arena_allocate(arena, u32, function->instruction_count ? function->instruction_count : 1);
        u32 gathered_rows = 0;
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = function->instructions[id.value].next)
            {
                row_layout.rows[gathered_rows] = id.value;
                gathered_rows += 1;
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
    // The values the sweeps alias, in assignment order.  About an eighth of a
    // function's values end up aliased, so the pass that hands each one its
    // local's register reads this list — 128 K entries in a self-compile —
    // instead of asking all 1.01 M values whether they have an alias.
    u32* alias_values = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32 alias_count = 0;
    u32* next_store_ordinals = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
    u32* next_store_epochs = arena_allocate(arena, u32, function->value_count ? function->value_count : 1);
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
        u32 candidate_base = 0;
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            u32 epoch = alias_sweep * function->block_count + block_index + 1;
            u32 block_candidate_count = block_candidate_counts[block_index];
            // Only the candidate rows can move this sweep: every other opcode
            // leaves the root unrooted and falls straight through, and the
            // candidates are a subsequence, so reversing them is reversing
            // the block.
            for (u32 remaining = block_candidate_count; remaining > 0; remaining -= 1)
            {
                u32 row_offset = candidate_rows[candidate_base + remaining - 1];
                IrInstruction* instruction = function->instructions + machine_selection_row_id(&row_layout, block, walked_ordinals, row_offset);
                u32 instruction_ordinal = walked_ordinals + row_offset + 1;
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
                    // The second sweep recomputes the first's load aliases
                    // identically, so the transition is what keeps one value
                    // out of the list twice.
                    if (load_aliases[candidate] == UINT32_MAX)
                    {
                        alias_values[alias_count] = candidate;
                        alias_count += 1;
                    }
                    load_aliases[candidate] = root;
                }
            }
            walked_ordinals += row_layout.block_row_counts[block_index];
            candidate_base += block_candidate_count;
        }
    }
    // Classification pass: direct locals become stack slots, every other
    // scalar result becomes a virtual register, in stable value-id order.
    u32 classified_rows = 0;
    for (u32 block_index = 0; block_index < function->block_count && selector.supported; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        u32 block_row_count = row_layout.block_row_counts[block_index];
        for (u32 row_offset = 0; row_offset < block_row_count; row_offset += 1)
        {
            IrInstruction* instruction = function->instructions + machine_selection_row_id(&row_layout, block, classified_rows, row_offset);
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
        classified_rows += block_row_count;
    }
    // Aliased load results share their local's virtual register: every use
    // site then names the local directly and the load emits nothing. The
    // result's own classification vreg goes unused, which costs an id and
    // nothing else.
    for (u32 alias_index = 0; alias_index < alias_count; alias_index += 1)
    {
        u32 value_index = alias_values[alias_index];
        if (selector.value_virtual_registers[load_aliases[value_index]] != UINT32_MAX)
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
    u32* local_store_ordinals = local_store_counts;
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        selector.branch_fusions[value_index] = (MachineX64BranchFusion){0};
        selector.fused_dead[value_index] = 0;
        selector.atomic_success_registers[value_index] = UINT32_MAX;
        local_store_ordinals[value_index] = 0;
    }
    // The stores this stamps and the branches it fuses are both candidate
    // rows, so this pass reads the same compact list the sweeps did.  The
    // ordinals stay the walk's own: candidate `row_offset` in a block that
    // starts at `fused_rows` is the row the full walk numbered next.
    u32 fused_rows = 0;
    u32 fusion_candidate_base = 0;
    for (u32 block_index = 0; block_index < function->block_count && selector.supported; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        u32 block_candidate_count = block_candidate_counts[block_index];
        for (u32 candidate_index = 0; candidate_index < block_candidate_count; candidate_index += 1)
        {
            u32 row_offset = candidate_rows[fusion_candidate_base + candidate_index];
            IrInstruction* instruction = function->instructions + machine_selection_row_id(&row_layout, block, fused_rows, row_offset);
            u32 fusion_ordinal = fused_rows + row_offset + 1;
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
                    function->values[chain_value].definition.value == UINT32_MAX)
                {
                    break;
                }
                IrInstruction* member = function->instructions + function->values[chain_value].definition.value;
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
                            if (function->values[constant_value].definition.value == UINT32_MAX)
                            {
                                continue;
                            }
                            IrInstruction* side_definition = function->instructions + function->values[constant_value].definition.value;
                            if (side_definition->opcode == IR_OPCODE_CAST && side_definition->operand_count >= 1 &&
                                side_definition->operands[0].value < function->value_count &&
                                (side_definition->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                                 side_definition->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND ||
                                 side_definition->conversion_operation == IR_CONVERSION_IDENTITY))
                            {
                                through_cast = constant_value;
                                constant_value = side_definition->operands[0].value;
                                if (function->values[constant_value].definition.value == UINT32_MAX)
                                {
                                    continue;
                                }
                                side_definition = function->instructions + function->values[constant_value].definition.value;
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
        fused_rows += row_layout.block_row_counts[block_index];
        fusion_candidate_base += block_candidate_count;
    }
    selector.virtual_register_count = selector.builder.virtual_registers.total_count;
    selector.virtual_register_definitions = arena_allocate(arena, u32, selector.virtual_register_count);
    if (selector.virtual_register_count)
    {
        memset(selector.virtual_register_definitions, 0xff,
               sizeof(*selector.virtual_register_definitions) * selector.virtual_register_count);
    }
    u32 typed_instruction_count = 0;
    u32 simd_operation_count = 0;
    u32 selected_rows = 0;
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
            // Win64 stack arguments start above the caller-owned shadow
            // area, so every incoming read shifts by its four eightbytes;
            // System V starts at the saved frame and return address alone.
            u32 incoming_stack_base = windows_abi ? 32u : 0u;
            // Capture every incoming argument register at entry, before any
            // body row can use an argument register as an operand scratch.
            // Integer parts capture first because float captures scratch
            // general registers; XMM state survives that pass untouched.
            if (selector.return_shape.indirect)
            {
                machine_x64_select_row(&selector,
                                       (MachineInstruction){
                                           .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, selector.hidden_return_slot),
                                                        machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, machine_x64_argument_register(windows_abi, 0))},
                                           .opcode = MACHINE_X64_STORE_FRAME64,
                                       });
            }
            u32 normalize_registers[MACHINE_X64_MAX_ARGUMENTS] = {0};
            u32 normalize_values[MACHINE_X64_MAX_ARGUMENTS] = {0};
            u16 normalize_opcodes[MACHINE_X64_MAX_ARGUMENTS] = {0};
            u32 normalize_count = 0;
            for (u32 capture_pass = 0; capture_pass < 2 && selector.supported; capture_pass += 1)
            {
                bool float_pass = capture_pass == 1;
                for (u32 argument_index = 0; argument_index < function_type->parameter_count; argument_index += 1)
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
                                                                  .payload = 16 + incoming_stack_base + (u32)parameter_placement->first_stack_part * 8,
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
                                machine_x64_select_row(&selector,
                                                       (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                           .payload = incoming_stack_base + ((u32)parameter_placement->first_stack_part + part_index) * 8,
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
                                                                                 .payload = incoming_stack_base + (u32)parameter_placement->first_stack_part * 8,
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
                                                                                         machine_x64_argument_register(windows_abi, part_integer))},
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
                                                                                                  machine_x64_argument_register(windows_abi, next_integer))},
                                                                    .opcode = MACHINE_X64_MOV_RR,
                                                                });
                    }
                    machine_x64_define(&selector, argument_register, row);
                    // Both ABIs leave the bits above a narrow integer
                    // argument's declared width unspecified in the register
                    // it arrives in, so the captured value is normalized to
                    // that width here. Every later use reads the whole
                    // register, and a `uint32_t` parameter used as an index
                    // otherwise scales the caller's leftover high half.  The
                    // rows are recorded here and emitted after both capture
                    // passes: a row emitted between captures may take a
                    // scratch register the incoming arguments still occupy.
                    if (!scalar_float && normalize_count < BUSTER_ARRAY_LENGTH(normalize_registers))
                    {
                        IrType* parameter_type = ir_type_from_id(&program->types, function_type->parameter_types[argument_index]);
                        if (parameter_type && (parameter_type->kind == IR_TYPE_INTEGER || parameter_type->kind == IR_TYPE_BOOLEAN) &&
                            parameter_type->layout.size < 8)
                        {
                            normalize_registers[normalize_count] = argument_register;
                            normalize_values[normalize_count] = argument_value;
                            normalize_opcodes[normalize_count] = parameter_type->layout.size == 1   ? MACHINE_X64_MOVZX8_RR
                                                                 : parameter_type->layout.size == 2 ? MACHINE_X64_MOVZX16_RR
                                                                                                    : MACHINE_X64_MOV32_RR;
                            normalize_count += 1;
                        }
                    }
                }
            }
            for (u32 normalize_index = 0; normalize_index < normalize_count && selector.supported; normalize_index += 1)
            {
                // The normalized value takes a register of its own: a virtual
                // register is defined exactly once, and the argument's is
                // already defined by its capture row.
                u32 source_register = normalize_registers[normalize_index];
                u32 normalize_register = machine_x64_synthesize_register(&selector);
                u32 normalize_row = machine_x64_select_row(&selector, (MachineInstruction){
                                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, normalize_register),
                                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                                         .opcode = normalize_opcodes[normalize_index],
                                                                     });
                machine_x64_define(&selector, normalize_register, normalize_row);
                selector.value_virtual_registers[normalize_values[normalize_index]] = normalize_register;
            }
        }
        u32 block_row_count = row_layout.block_row_counts[block_index];
        for (u32 row_offset = 0; row_offset < block_row_count && selector.supported; row_offset += 1)
        {
            IrInstructionId id = {.value = machine_selection_row_id(&row_layout, block, selected_rows, row_offset)};
            IrInstruction* instruction = function->instructions + id.value;
            typed_instruction_count += 1;
            simd_operation_count += instruction->opcode == IR_OPCODE_SIMD;
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
            bool instruction_selected = false;
            bool fused_dead = false;
            u32 result_register = UINT32_MAX;
            if (instruction->result.value != IR_ID_UNDERLYING_INVALID && instruction->result.value < function->value_count)
            {
                // A branch-fusion chain member selects into nothing: the branch
                // re-selects the compare at its own row, and the member's only
                // consumer is the chain. Every marked member is pure.
                fused_dead = selector.fused_dead[instruction->result.value];
                result_register = selector.value_virtual_registers[instruction->result.value];
            }
            if (fused_dead)
            {
                instruction_selected = true;
            }
            else
            {
                switch (instruction->opcode)
                {
                case IR_OPCODE_VA_ARG:
                    instruction_selected = machine_x64_select_va_arg(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_VA_START:
                    instruction_selected = machine_x64_select_va_start(&selector, instruction);
                    break;
                case IR_OPCODE_VA_COPY:
                    instruction_selected = machine_x64_select_va_copy(&selector, instruction);
                    break;
                case IR_OPCODE_VA_END:
                    instruction_selected = machine_x64_select_va_end(&selector, instruction);
                    break;
                case IR_OPCODE_LOCAL:
                    instruction_selected = machine_x64_select_local(&selector, instruction);
                    break;
                case IR_OPCODE_STACK_SAVE:
                    instruction_selected = machine_x64_select_stack_save(&selector, result_register);
                    break;
                case IR_OPCODE_STACK_ALLOCATE:
                    instruction_selected = machine_x64_select_stack_allocate(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_STACK_RESTORE:
                    instruction_selected = machine_x64_select_stack_restore(&selector, instruction);
                    break;
                case IR_OPCODE_ARGUMENT:
                    instruction_selected = machine_x64_select_argument(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_CONSTANT_INTEGER:
                case IR_OPCODE_CONSTANT_FLOAT:
                    instruction_selected = machine_x64_select_constant(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_CAST:
                    instruction_selected = machine_x64_select_cast(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_UNARY:
                    instruction_selected = machine_x64_select_unary(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_BINARY:
                    instruction_selected = machine_x64_select_binary(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_DEREFERENCE:
                    instruction_selected = machine_x64_select_dereference(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_GLOBAL:
                    instruction_selected = machine_x64_select_global_address(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_ADDRESS_OF:
                    instruction_selected = machine_x64_select_address_of(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_FIELD:
                    instruction_selected = machine_x64_select_field(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_DEBUG_TRAP:
                    instruction_selected = machine_x64_select_debug_trap(&selector);
                    break;
                case IR_OPCODE_AGGREGATE:
                    instruction_selected = machine_x64_select_aggregate(&selector, instruction);
                    break;
                case IR_OPCODE_ARRAY:
                    instruction_selected = machine_x64_select_array(&selector, instruction);
                    break;
                case IR_OPCODE_INDEX:
                    instruction_selected = machine_x64_select_index(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_LOAD:
                case IR_OPCODE_ATOMIC_LOAD:
                    instruction_selected = machine_x64_select_load(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_STORE:
                case IR_OPCODE_ATOMIC_STORE:
                    instruction_selected = machine_x64_select_store(&selector, instruction);
                    break;
                case IR_OPCODE_FUNCTION:
                    instruction_selected = machine_x64_select_function(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_SIMD:
                    instruction_selected = machine_x64_select_simd(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_CALL:
                    instruction_selected = machine_x64_select_call(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_BRANCH:
                    instruction_selected = machine_x64_select_branch(&selector, instruction);
                    break;
                case IR_OPCODE_LABEL_ADDRESS:
                    instruction_selected = machine_x64_select_label_address(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_INDIRECT_BRANCH:
                    instruction_selected = machine_x64_select_indirect_branch(&selector, instruction);
                    break;
                case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE:
                    instruction_selected = machine_x64_select_atomic_read_modify_write(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_ATOMIC_COMPARE_EXCHANGE:
                    instruction_selected = machine_x64_select_atomic_compare_exchange(&selector, instruction, result_register);
                    break;
                case IR_OPCODE_ATOMIC_FENCE:
                    instruction_selected = machine_x64_select_atomic_fence(&selector, instruction);
                    break;
                case IR_OPCODE_UNREACHABLE:
                    instruction_selected = machine_x64_select_unreachable(&selector);
                    break;
                case IR_OPCODE_SWITCH:
                    instruction_selected = machine_x64_select_switch(&selector, instruction);
                    break;
                case IR_OPCODE_BRANCH_IF:
                    instruction_selected = machine_x64_select_branch_if(&selector, instruction);
                    break;
                case IR_OPCODE_RETURN:
                    instruction_selected = machine_x64_select_return(&selector, instruction);
                    break;
                default:
                    instruction_selected = false;
                    break;
                }
            }
            if (!instruction_selected)
            {
                machine_x64_reject(&selector, instruction->opcode);
                break;
            }
        }
        machine_builder_block_end(&selector.builder, (MachineBlock){.parameter_offset = parameter_offset, .parameter_count = (u16)block->parameter_count});
        selected_rows += block_row_count;
    }
    if (!selector.supported)
    {
        result.failed_opcode = selector.failed_opcode;
        return result;
    }
    for (u32 destination_block = 0; destination_block < function->block_count; destination_block += 1)
    {
        IrBlock* destination = function->blocks + destination_block;
        for (IrPredecessor* predecessor = destination->first_predecessor; predecessor; predecessor = predecessor->next)
        {
            u32 copy_offset = selector.builder.edge_copy_sources.total_count;
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
                machine_builder_edge_copy_source(
                    &selector.builder, machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, selector.value_virtual_registers[incoming->value.value]));
            }
            machine_builder_edge(&selector.builder,
                                 (MachineEdge){.source_block = predecessor->block.value,
                                               .destination_block = destination_block,
                                               .copy_offset = copy_offset,
                                               .copy_count = (u16)destination->parameter_count});
        }
    }
    if (!machine_x64_select_indirect_edges(&selector))
    {
        return (MachineSelectResult){.failed_opcode = IR_OPCODE_INDIRECT_BRANCH};
    }
    result.function = machine_function_builder_finish(arena, &selector.builder);
    result.function.target = windows_abi ? &machine_x86_64_windows_description : &machine_x86_64_description;
    result.function.immediates = arena_allocate(arena, u64, selector.immediates.total_count);
    result.function.immediate_count = selector.immediates.total_count;
    machine_stream_flatten(&selector.immediates, result.function.immediates);
    result.function.stack_slot_sizes = arena_allocate(arena, u32, selector.stack_slots.total_count);
    result.function.stack_slot_count = selector.stack_slots.total_count;
    machine_stream_flatten(&selector.stack_slots, result.function.stack_slot_sizes);
    result.function.stack_slot_alignments = arena_allocate(arena, u32, selector.stack_slot_alignments.total_count);
    machine_stream_flatten(&selector.stack_slot_alignments, result.function.stack_slot_alignments);
    // The outgoing area's final size is only known once every call has been
    // selected, so its slot is patched here rather than at creation.
    if (selector.outgoing_slot != UINT32_MAX)
    {
        result.function.outgoing_slot = selector.outgoing_slot;
        result.function.outgoing_bytes = selector.outgoing_bytes;
        result.function.stack_slot_sizes[selector.outgoing_slot] = selector.outgoing_bytes;
    }
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
    result.selector_certified = true;
    result.returns_value = returns_value;
    result.selected_typed_instructions = typed_instruction_count;
    result.machine_instructions = result.function.instruction_count;
    result.simd_operation_count = simd_operation_count;
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
    MACHINE_X64_MOV_IMMEDIATE_EXACT_FORM_ID = 10018u,
    MACHINE_X64_MOV_SIGNED_IMMEDIATE_EXACT_FORM_ID = 9533u,
    MACHINE_X64_LEA_EXACT_FORM_ID = 9849u,
    MACHINE_X64_ADD_IMMEDIATE8_EXACT_FORM_ID = 9316u,
    MACHINE_X64_ADD_IMMEDIATE32_EXACT_FORM_ID = 9270u,
    MACHINE_X64_IMUL_IMMEDIATE8_EXACT_FORM_ID = 9748u,
    MACHINE_X64_IMUL_IMMEDIATE32_EXACT_FORM_ID = 9745u,
    MACHINE_X64_MOV_MEMORY_EXACT_FORM_ID = 9845u,
    MACHINE_X64_MOV_MEMORY8_EXACT_FORM_ID = 9840u,
    MACHINE_X64_MOV_MEMORY_FULL_EXACT_FORM_ID = 9841u,
    MACHINE_X64_MOVZX_MEMORY8_EXACT_FORM_ID = 10289u,
    MACHINE_X64_MOVZX_MEMORY16_EXACT_FORM_ID = 10291u,
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
    MACHINE_X64_LOAD_INCOMING_EXACT_FORM_ID = 9845u,
    MACHINE_X64_PUSH_REGISTER_EXACT_FORM_ID = 9722u,
    MACHINE_X64_ADD_RSP_EXACT_FORM_ID = 9270u,
    MACHINE_X64_PUSH_FRAME_EXACT_FORM_ID = 9490u,
    MACHINE_X64_VMOV_RR_EXACT_FORM_ID = 5635u,
    MACHINE_X64_VLOAD_EXACT_FORM_ID = 5636u,
    MACHINE_X64_VSTORE_EXACT_FORM_ID = 5638u,
    MACHINE_X64_VPSLLD_EXACT_FORM_ID = 7702u,
    MACHINE_X64_VPTERNLOGD_EXACT_FORM_ID = 7740u,
    MACHINE_X64_VSPLATB_EXACT_FORM_ID = 5841u,
    MACHINE_X64_VSPLATD_EXACT_FORM_ID = 7534u,
    MACHINE_X64_XCHG_EXACT_FORM_ID = 9837u,
    MACHINE_X64_CMPXCHG_EXACT_FORM_ID = 10280u,
    MACHINE_X64_SUB_RSP_EXACT_FORM_ID = 9285u,
    MACHINE_X64_VPADDB_EXACT_FORM_ID = 5739u,
    MACHINE_X64_VPADDW_EXACT_FORM_ID = 5777u,
    MACHINE_X64_VPADDD_EXACT_FORM_ID = 7515u,
    MACHINE_X64_VPADDQ_EXACT_FORM_ID = 7517u,
    MACHINE_X64_VPSUBB_EXACT_FORM_ID = 6595u,
    MACHINE_X64_VPSUBW_EXACT_FORM_ID = 6633u,
    MACHINE_X64_VPSUBD_EXACT_FORM_ID = 7736u,
    MACHINE_X64_VPSUBQ_EXACT_FORM_ID = 7738u,
    MACHINE_X64_VPANDD_EXACT_FORM_ID = 7519u,
    MACHINE_X64_VPORD_EXACT_FORM_ID = 7674u,
    MACHINE_X64_VPXORD_EXACT_FORM_ID = 7760u,
};

// x86-64 requires SSE2, so the MFENCE exact row always receives the target's
// architectural baseline gate.  INT3 is BASE and deliberately carries no
// optional feature names.
BUSTER_GLOBAL_LOCAL String8 const machine_x64_sse2_features[] = {S8_INITIALIZER("sse2")};
BUSTER_GLOBAL_LOCAL String8 const machine_x64_sse_features[] = {S8_INITIALIZER("sse")};
BUSTER_GLOBAL_LOCAL String8 const machine_x64_avx_features[] = {S8_INITIALIZER("avx")};
BUSTER_GLOBAL_LOCAL String8 const machine_x64_popcnt_features[] = {S8_INITIALIZER("popcnt")};
BUSTER_GLOBAL_LOCAL String8 const machine_x64_avx512f_features[] = {S8_INITIALIZER("avx512f")};
BUSTER_GLOBAL_LOCAL String8 const machine_x64_avx512bw_features[] = {S8_INITIALIZER("avx512f"), S8_INITIALIZER("avx512bw")};
BUSTER_GLOBAL_LOCAL String8 const machine_x64_avx512vbmi_features[] = {
    S8_INITIALIZER("avx512f"), S8_INITIALIZER("avx512bw"), S8_INITIALIZER("avx512vbmi")};
BUSTER_GLOBAL_LOCAL String8 const machine_x64_avx512vbmi2_features[] = {
    S8_INITIALIZER("avx512f"), S8_INITIALIZER("avx512bw"), S8_INITIALIZER("avx512vbmi2")};
/* Expansion rows use this name for the same architectural AVX-512 baseline. */
BUSTER_GLOBAL_LOCAL String8 const machine_x64_avx512_features[] = {S8_INITIALIZER("avx512f"), S8_INITIALIZER("avx512bw")};
BUSTER_GLOBAL_LOCAL String8 const machine_x64_cx16_features[] = {S8_INITIALIZER("cx16")};

// Expansion/prologue instructions use a small, closed set of physical
// shapes.  Resolve each shape through the metadata selector once during the
// serial prewarm lane and publish only the resulting opaque machine token to
// workers.  The signature deliberately describes physical shape/value
// classes, not register numbers or byte templates; dynamic registers,
// displacements, and immediates are still validated by the metadata transform
// on every emission.
#define MACHINE_X64_METADATA_SHAPE_CACHE_CAPACITY 256u
#define MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY 512u
typedef struct MachineX64MetadataShapeCacheEntry MachineX64MetadataShapeCacheEntry;
struct MachineX64MetadataShapeCacheEntry
{
    u64 signature;
    u64 guard;
    BusterX86MetadataMachineExactToken token;
};
BUSTER_GLOBAL_LOCAL MachineX64MetadataShapeCacheEntry machine_x64_metadata_shape_cache[MACHINE_X64_METADATA_SHAPE_CACHE_CAPACITY];
BUSTER_GLOBAL_LOCAL u16 machine_x64_metadata_shape_cache_slots[MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY];
BUSTER_GLOBAL_LOCAL u32 machine_x64_metadata_shape_cache_count;
BUSTER_GLOBAL_LOCAL u32 machine_x64_metadata_shape_cache_invalid_count;
BUSTER_GLOBAL_LOCAL bool machine_x64_metadata_shape_cache_ready;
BUSTER_CT_CHECK((MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY & (MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY - 1u)) == 0);

BUSTER_GLOBAL_LOCAL void machine_x64_metadata_shape_cache_prewarm(void);

enum
{
    MACHINE_X64_EXACT_RECIPE_FLAG_SELF_COPY_NOOP = 1u << 0,
    MACHINE_X64_EXACT_RECIPE_FLAG_BRANCH_FIXUP = 1u << 1,
    MACHINE_X64_EXACT_RECIPE_FLAG_CALL_SITE = 1u << 2,
    MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32 = 1u << 3,
    MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_LOCK = 1u << 4,
    MACHINE_X64_EXACT_RECIPE_FLAG_PARITY_ONLY = 1u << 5,
    MACHINE_X64_EXACT_RECIPE_FLAG_PARITY_OR = 1u << 6,
};

typedef enum MachineX64ExactOperandProjection
{
    MACHINE_X64_EXACT_OPERAND_GPR,
    MACHINE_X64_EXACT_OPERAND_XMM_PAYLOAD,
    MACHINE_X64_EXACT_OPERAND_XMM_FIXED0,
    MACHINE_X64_EXACT_OPERAND_XMM_FIXED1,
    MACHINE_X64_EXACT_OPERAND_XMM_SLOT,
    MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX,
    MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RDX,
    MACHINE_X64_EXACT_OPERAND_GPR_PAYLOAD_SIZE,
    MACHINE_X64_EXACT_OPERAND_ZMM_SLOT,
    MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1,
    MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K0,
    MACHINE_X64_EXACT_OPERAND_FIXED_RSP,
    MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD,
    MACHINE_X64_EXACT_OPERAND_IMMEDIATE_CONSTANT,
    // The constant 1: the unsigned less-than predicate of the vpcmp*u* forms,
    // for rows whose payload is already spent on the variant index.
    MACHINE_X64_EXACT_OPERAND_IMMEDIATE_LESS,
    MACHINE_X64_EXACT_OPERAND_RELATIVE_ZERO,
    MACHINE_X64_EXACT_OPERAND_RIP_MEMORY_ZERO,
    MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO,
    MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD_SIZE,
    MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD,
    MACHINE_X64_EXACT_OPERAND_RBP_MEMORY_PAYLOAD,
    MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD,
    MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_SLOT,
    MACHINE_X64_EXACT_OPERAND_PROJECTION_COUNT,
} MachineX64ExactOperandProjection;

typedef enum MachineX64ExactVariantSelector
{
    MACHINE_X64_EXACT_VARIANT_FIXED,
    MACHINE_X64_EXACT_VARIANT_MOV_IMMEDIATE,
    MACHINE_X64_EXACT_VARIANT_SIGNED_IMMEDIATE,
    MACHINE_X64_EXACT_VARIANT_VBINARY,
    MACHINE_X64_EXACT_VARIANT_SELECTOR_COUNT,
} MachineX64ExactVariantSelector;

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
    MACHINE_X64_EXACT_PLAN_LOAD_INCOMING,
    MACHINE_X64_EXACT_PLAN_MOV_IMMEDIATE,
    MACHINE_X64_EXACT_PLAN_MOV_SIGNED_IMMEDIATE,
    MACHINE_X64_EXACT_PLAN_ADD_IMMEDIATE8,
    MACHINE_X64_EXACT_PLAN_IMUL_IMMEDIATE8,
    MACHINE_X64_EXACT_PLAN_IMUL_IMMEDIATE32,
    MACHINE_X64_EXACT_PLAN_STORE8,
    MACHINE_X64_EXACT_PLAN_STORE_FULL,
    MACHINE_X64_EXACT_PLAN_MOVZX8,
    MACHINE_X64_EXACT_PLAN_MOVZX16,
    MACHINE_X64_EXACT_PLAN_PUSH_FRAME,
    MACHINE_X64_EXACT_PLAN_VMOV,
    MACHINE_X64_EXACT_PLAN_VLOAD,
    MACHINE_X64_EXACT_PLAN_VSTORE,
    MACHINE_X64_EXACT_PLAN_VPSLLD,
    MACHINE_X64_EXACT_PLAN_VPADDB,
    MACHINE_X64_EXACT_PLAN_VPADDW,
    MACHINE_X64_EXACT_PLAN_VPADDD,
    MACHINE_X64_EXACT_PLAN_VPADDQ,
    MACHINE_X64_EXACT_PLAN_VPSUBB,
    MACHINE_X64_EXACT_PLAN_VPSUBW,
    MACHINE_X64_EXACT_PLAN_VPSUBD,
    MACHINE_X64_EXACT_PLAN_VPSUBQ,
    MACHINE_X64_EXACT_PLAN_VPANDD,
    MACHINE_X64_EXACT_PLAN_VPORD,
    MACHINE_X64_EXACT_PLAN_VPXORD,
    MACHINE_X64_EXACT_PLAN_VSPLATB,
    MACHINE_X64_EXACT_PLAN_VSPLATD,
    MACHINE_X64_EXACT_PLAN_VPTERNLOGD,
    MACHINE_X64_EXACT_PLAN_XCHG,
    MACHINE_X64_EXACT_PLAN_CMPXCHG,
    MACHINE_X64_EXACT_PLAN_SUB_RSP,
    MACHINE_X64_EXACT_PLAN_COUNT,
    MACHINE_X64_EXACT_PLAN_INVALID = UINT8_MAX,
} MachineX64ExactPlanId;

typedef struct MachineX64ExactRecipe MachineX64ExactRecipe;
typedef struct MachineX64ExactRecipeVariant MachineX64ExactRecipeVariant;
struct MachineX64ExactRecipeVariant
{
    X64ExactFormKey key;
    String8 const* features;
    u32 feature_count;
    u8 operand_count;
    u8 flags;
    u8 operand_slots[4];
    u8 operand_kinds[4];
    u16 operand_widths[4];
};

struct MachineX64ExactRecipe
{
    MachineEmitRecipeId recipe;
    X64ExactFormKey key;
    String8 const* features;
    u32 feature_count;
    u8 operand_count;
    u8 flags;
    u8 operand_slots[4];
    u8 operand_kinds[4];
    u16 operand_widths[4];
    // A family row may select one of a small number of durable forms (for
    // example MOV's zero-extending, sign-extending, and full-width immediate
    // encodings).  Variant zero is the row itself; the optional array holds
    // variants one and onward.  Direct rows keep variant_count at zero.
    u8 variant_count;
    u8 variant_selector;
    u8 reserved[2];
    MachineX64ExactRecipeVariant const* variants;
};

// Composite family rows are represented as a short, immutable sequence of
// metadata forms.  Steps carry the same projections as single-form recipes;
// staging (for example GPR -> k1) is therefore expressed by another exact
// token instead of a handwritten opcode helper.
#define MACHINE_X64_EXACT_SEQUENCE_MAX_VARIANTS 16u
#define MACHINE_X64_EXACT_SEQUENCE_MAX_STEPS 8u
typedef struct MachineX64ExactSequenceStep MachineX64ExactSequenceStep;
typedef struct MachineX64ExactSequenceVariant MachineX64ExactSequenceVariant;
typedef struct MachineX64ExactSequence MachineX64ExactSequence;
struct MachineX64ExactSequenceStep
{
    X64ExactFormKey key;
    String8 const* features;
    u32 feature_count;
    u8 operand_count;
    u8 flags;
    u8 operand_slots[4];
    u8 operand_kinds[4];
    u16 operand_widths[4];
    u8 mask_register_plus_one;
    bool zeroing;
};
struct MachineX64ExactSequenceVariant
{
    u8 step_count;
    MachineX64ExactSequenceStep const* steps;
};
struct MachineX64ExactSequence
{
    MachineEmitRecipeId recipe;
    u8 variant_count;
    u8 variant_selector;
    u8 reserved[2];
    MachineX64ExactSequenceVariant const* variants;
};

BUSTER_GLOBAL_LOCAL MachineX64ExactRecipeVariant machine_x64_exact_recipe_variant(MachineX64ExactRecipe const* recipe, u32 variant_index)
{
    MachineX64ExactRecipeVariant result = {
        .key = recipe->key,
        .features = recipe->features,
        .feature_count = recipe->feature_count,
        .operand_count = recipe->operand_count,
        .flags = recipe->flags,
        .operand_slots = {recipe->operand_slots[0], recipe->operand_slots[1], recipe->operand_slots[2], recipe->operand_slots[3]},
        .operand_kinds = {recipe->operand_kinds[0], recipe->operand_kinds[1], recipe->operand_kinds[2], recipe->operand_kinds[3]},
        .operand_widths = {recipe->operand_widths[0], recipe->operand_widths[1], recipe->operand_widths[2], recipe->operand_widths[3]},
    };
    if (variant_index && variant_index <= recipe->variant_count && recipe->variants)
    {
        result = recipe->variants[variant_index - 1];
    }
    return result;
}

// Flat DIRECT recipe projections.  Each descriptor is keyed by the stable
// MachineEmitRecipeId assigned by machine.c; the operand slots are the
// post-placement machine row slots, in metadata form order (RM/B before
// REG/R for the two-address ALU forms).
BUSTER_GLOBAL_LOCAL MachineX64ExactRecipe const machine_x64_exact_recipe_table[MACHINE_X86_64_EMIT_REGISTRY_DIRECT_COUNT] = {
    [0] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 0,
        .key = {MACHINE_X64_MOV_RR_EXACT_FORM_ID, UINT64_C(0x3ab69ab9d0d06329)},
        .operand_count = 2, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_SELF_COPY_NOOP,
        .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [1] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 1,
        .key = {MACHINE_X64_MOV_RR_EXACT_FORM_ID, UINT64_C(0x3ab69ab9d0d06329)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {32, 32},
    },
    [2] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 2,
        .key = {MACHINE_X64_MOVSX8_RR_EXACT_FORM_ID, UINT64_C(0x60113253679881a6)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 8},
    },
    [3] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 3,
        .key = {MACHINE_X64_MOVSX16_RR_EXACT_FORM_ID, UINT64_C(0x1ea1a0d40c380394)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 16},
    },
    [4] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 4,
        .key = {MACHINE_X64_MOVSX32_RR_EXACT_FORM_ID, UINT64_C(0xacab1d188d386b1e)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 32},
    },
    [5] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 5,
        .key = {MACHINE_X64_MOVZX8_RR_EXACT_FORM_ID, UINT64_C(0xa9d675ab86fb1641)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 8},
    },
    [6] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 6,
        .key = {MACHINE_X64_MOVZX16_RR_EXACT_FORM_ID, UINT64_C(0xb25b807d6d5747b3)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 16},
    },
    [7] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 7,
        .key = {MACHINE_X64_ADD_RR_EXACT_FORM_ID, UINT64_C(0xf0743d28f1fbad54)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [8] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 8,
        .key = {MACHINE_X64_ADD_RR_EXACT_FORM_ID, UINT64_C(0xf0743d28f1fbad54)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [9] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 9,
        .key = {MACHINE_X64_SUB_RR_EXACT_FORM_ID, UINT64_C(0x240ef104bd0b9756)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [10] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 10,
        .key = {MACHINE_X64_SUB_RR_EXACT_FORM_ID, UINT64_C(0x240ef104bd0b9756)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [11] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 11,
        .key = {MACHINE_X64_AND_RR_EXACT_FORM_ID, UINT64_C(0x65889d347cd743a9)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [12] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 12,
        .key = {MACHINE_X64_AND_RR_EXACT_FORM_ID, UINT64_C(0x65889d347cd743a9)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [13] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 13,
        .key = {MACHINE_X64_OR_RR_EXACT_FORM_ID, UINT64_C(0x7f76bb71727dbfaa)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [14] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 14,
        .key = {MACHINE_X64_OR_RR_EXACT_FORM_ID, UINT64_C(0x7f76bb71727dbfaa)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [15] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 15,
        .key = {MACHINE_X64_XOR_RR_EXACT_FORM_ID, UINT64_C(0x915fb23b51f8b7d1)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [16] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 16,
        .key = {MACHINE_X64_XOR_RR_EXACT_FORM_ID, UINT64_C(0x915fb23b51f8b7d1)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [17] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 17,
        .key = {MACHINE_X64_IMUL_RR_EXACT_FORM_ID, UINT64_C(0x8ad1b7f99185b8ea)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {32, 32},
    },
    [18] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 18,
        .key = {MACHINE_X64_IMUL_RR_EXACT_FORM_ID, UINT64_C(0x8ad1b7f99185b8ea)},
        .operand_count = 2, .operand_slots = {0, 2}, .operand_widths = {64, 64},
    },
    [19] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 19,
        .key = {MACHINE_X64_NEG_R_EXACT_FORM_ID, UINT64_C(0x10bbcf6d744b04f9)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {32},
    },
    [20] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 20,
        .key = {MACHINE_X64_NEG_R_EXACT_FORM_ID, UINT64_C(0x10bbcf6d744b04f9)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [21] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 21,
        .key = {MACHINE_X64_NOT_R_EXACT_FORM_ID, UINT64_C(0xbd007ad8742aadfc)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {32},
    },
    [22] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 22,
        .key = {MACHINE_X64_NOT_R_EXACT_FORM_ID, UINT64_C(0xbd007ad8742aadfc)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [23] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 23,
        .key = {MACHINE_X64_BSF_RR_EXACT_FORM_ID, UINT64_C(0x047fe78cb986f7d1)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {32, 32},
    },
    [24] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 24,
        .key = {MACHINE_X64_BSF_RR_EXACT_FORM_ID, UINT64_C(0x047fe78cb986f7d1)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [25] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 25,
        .key = {MACHINE_X64_BSR_RR_EXACT_FORM_ID, UINT64_C(0x7c13390dbcf0139e)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {32, 32},
    },
    [26] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 26,
        .key = {MACHINE_X64_BSR_RR_EXACT_FORM_ID, UINT64_C(0x7c13390dbcf0139e)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [27] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 27,
        .key = {MACHINE_X64_POPCNT_RR_EXACT_FORM_ID, UINT64_C(0xb971fc2e8a6cb1bc)},
        .features = machine_x64_popcnt_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_popcnt_features),
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {32, 32},
    },
    [28] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 28,
        .key = {MACHINE_X64_POPCNT_RR_EXACT_FORM_ID, UINT64_C(0xb971fc2e8a6cb1bc)},
        .features = machine_x64_popcnt_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_popcnt_features),
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [32] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 32,
        .key = {MACHINE_X64_JMP_EXACT_FORM_ID, UINT64_C(0xab9c4b53fce14f6e)},
        .operand_count = 1, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_BRANCH_FIXUP,
        .operand_slots = {0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_RELATIVE_ZERO}, .operand_widths = {32},
    },
    [33] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 33,
        .key = {MACHINE_X64_SHIFT_LEFT_R_EXACT_FORM_ID, UINT64_C(0xe73cf970ddf68ce2)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {32},
    },
    [34] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 34,
        .key = {MACHINE_X64_SHIFT_LEFT_R_EXACT_FORM_ID, UINT64_C(0xe73cf970ddf68ce2)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [35] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 35,
        .key = {MACHINE_X64_SHIFT_ARITHMETIC_R_EXACT_FORM_ID, UINT64_C(0xa64cc273e7d95e92)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {32},
    },
    [36] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 36,
        .key = {MACHINE_X64_SHIFT_ARITHMETIC_R_EXACT_FORM_ID, UINT64_C(0xa64cc273e7d95e92)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [37] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 37,
        .key = {MACHINE_X64_SHIFT_RIGHT_R_EXACT_FORM_ID, UINT64_C(0x1fb65789e301333f)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {32},
    },
    [38] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 38,
        .key = {MACHINE_X64_SHIFT_RIGHT_R_EXACT_FORM_ID, UINT64_C(0x1fb65789e301333f)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [39] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 39,
        .key = {MACHINE_X64_LEA_SYMBOL_EXACT_FORM_ID, UINT64_C(0x0b357f27b62f3409)},
        .operand_count = 2, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_CALL_SITE,
        .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_RIP_MEMORY_ZERO},
        .operand_widths = {64, 64},
    },
    [40] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 40,
        .key = {MACHINE_X64_MOVQ_TO_XMM_EXACT_FORM_ID, UINT64_C(0x7bd465046ab10c4f)},
        .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features),
        .operand_count = 2, .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_PAYLOAD, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {128, 64},
    },
    [41] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 41,
        .key = {MACHINE_X64_MOVQ_FROM_XMM_EXACT_FORM_ID, UINT64_C(0x3698d9bff62c4360)},
        .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features),
        .operand_count = 2, .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_XMM_PAYLOAD}, .operand_widths = {64, 128},
    },
    [42] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 42,
        .key = {MACHINE_X64_LOAD_INCOMING_EXACT_FORM_ID, UINT64_C(0xca30e68cfa1406bc)},
        .operand_count = 2, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32,
        .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_RBP_MEMORY_PAYLOAD},
        .operand_widths = {64, 64},
    },
    [43] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 43,
        .key = {MACHINE_X64_PUSH_REGISTER_EXACT_FORM_ID, UINT64_C(0x849cc7557c589605)},
        .operand_count = 1, .operand_slots = {0}, .operand_widths = {64},
    },
    [44] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 44,
        .key = {MACHINE_X64_ADD_RSP_EXACT_FORM_ID, UINT64_C(0xcebed63a599832c0)},
        .operand_count = 2, .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_FIXED_RSP, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD}, .operand_widths = {64, 32},
    },
    [29] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 29,
        .key = {MACHINE_X64_CMP_RR_EXACT_FORM_ID, UINT64_C(0xa381563e623950cd)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {32, 32},
    },
    [30] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 30,
        .key = {MACHINE_X64_CMP_RR_EXACT_FORM_ID, UINT64_C(0xa381563e623950cd)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [31] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 31,
        .key = {MACHINE_X64_TEST_RR_EXACT_FORM_ID, UINT64_C(0x29ceea139128c1a6)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_widths = {64, 64},
    },
    [45] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 45,
        .key = {MACHINE_X64_MFENCE_EXACT_FORM_ID, UINT64_C(0x8deb7f066b773767)},
        .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features),
    },
    [46] = {
        .recipe = MACHINE_EMIT_RECIPE_DIRECT_BASE + 46,
        .key = {MACHINE_X64_INT3_EXACT_FORM_ID, UINT64_C(0x11eeb10ba44771fd)},
    },
};

// FAMILY rows migrated to the metadata bridge.  The first cohort keeps the
// machine selector's original opcode identities while replacing each
// handwritten byte template with one or more durable form keys.  Immediate
// rows use variants to retain the old shortest-form choice; memory rows carry
// the exact operand width and frame-displacement policy in their descriptors.
BUSTER_GLOBAL_LOCAL MachineX64ExactRecipeVariant const machine_x64_mov_ri_variants[] = {
    {
        .key = {MACHINE_X64_MOV_SIGNED_IMMEDIATE_EXACT_FORM_ID, UINT64_C(0x2f91860fef63a638)},
        .operand_count = 2,
        .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD},
        .operand_widths = {64, 32},
    },
    {
        .key = {MACHINE_X64_MOV_IMMEDIATE_EXACT_FORM_ID, UINT64_C(0x2a2535c90ada7adc)},
        .operand_count = 2,
        .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD},
        .operand_widths = {64, 64},
    },
};

#define MACHINE_X64_VBINARY_VARIANT_COUNT 10u
BUSTER_GLOBAL_LOCAL MachineX64ExactRecipeVariant const machine_x64_vbinary_variants[MACHINE_X64_VBINARY_VARIANT_COUNT];

BUSTER_GLOBAL_LOCAL MachineX64ExactRecipeVariant const machine_x64_add_imm_variants[] = {
    {
        .key = {MACHINE_X64_ADD_IMMEDIATE32_EXACT_FORM_ID, UINT64_C(0xcebed63a599832c0)},
        .operand_count = 2,
        .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD},
        .operand_widths = {64, 32},
    },
};

BUSTER_GLOBAL_LOCAL MachineX64ExactRecipeVariant const machine_x64_imul_imm_variants[] = {
    {
        .key = {MACHINE_X64_IMUL_IMMEDIATE32_EXACT_FORM_ID, UINT64_C(0x0c283301d404723a)},
        .operand_count = 3,
        .operand_slots = {0, 1, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD},
        .operand_widths = {64, 64, 32},
    },
};

BUSTER_GLOBAL_LOCAL MachineX64ExactRecipeVariant const machine_x64_vbinary_variants[] = {
    {.key = {MACHINE_X64_VPADDW_EXACT_FORM_ID, UINT64_C(0xc226488b97c949be)}, .features = machine_x64_avx512bw_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features), .operand_count = 3, .operand_slots = {0, 1, 2},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT},
     .operand_widths = {512, 512, 512}},
    {.key = {MACHINE_X64_VPADDD_EXACT_FORM_ID, UINT64_C(0xa91a25d8b3eabcb3)}, .features = machine_x64_avx512f_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features), .operand_count = 3, .operand_slots = {0, 1, 2},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT},
     .operand_widths = {512, 512, 512}},
    {.key = {MACHINE_X64_VPADDQ_EXACT_FORM_ID, UINT64_C(0x0bd74fee16f80704)}, .features = machine_x64_avx512f_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features), .operand_count = 3, .operand_slots = {0, 1, 2},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT},
     .operand_widths = {512, 512, 512}},
    {.key = {MACHINE_X64_VPSUBB_EXACT_FORM_ID, UINT64_C(0x46bb97c7e362ab9c)}, .features = machine_x64_avx512bw_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features), .operand_count = 3, .operand_slots = {0, 1, 2},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT},
     .operand_widths = {512, 512, 512}},
    {.key = {MACHINE_X64_VPSUBW_EXACT_FORM_ID, UINT64_C(0xe58ae9721b6de3e6)}, .features = machine_x64_avx512bw_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features), .operand_count = 3, .operand_slots = {0, 1, 2},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT},
     .operand_widths = {512, 512, 512}},
    {.key = {MACHINE_X64_VPSUBD_EXACT_FORM_ID, UINT64_C(0x535e16651bc218c4)}, .features = machine_x64_avx512f_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features), .operand_count = 3, .operand_slots = {0, 1, 2},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT},
     .operand_widths = {512, 512, 512}},
    {.key = {MACHINE_X64_VPSUBQ_EXACT_FORM_ID, UINT64_C(0xd390b11ac7f6e1db)}, .features = machine_x64_avx512f_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features), .operand_count = 3, .operand_slots = {0, 1, 2},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT},
     .operand_widths = {512, 512, 512}},
    {.key = {MACHINE_X64_VPANDD_EXACT_FORM_ID, UINT64_C(0x4903e85df3a91181)}, .features = machine_x64_avx512f_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features), .operand_count = 3, .operand_slots = {0, 1, 2},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT},
     .operand_widths = {512, 512, 512}},
    {.key = {MACHINE_X64_VPORD_EXACT_FORM_ID, UINT64_C(0x72940a7ae71f5658)}, .features = machine_x64_avx512f_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features), .operand_count = 3, .operand_slots = {0, 1, 2},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT},
     .operand_widths = {512, 512, 512}},
    {.key = {MACHINE_X64_VPXORD_EXACT_FORM_ID, UINT64_C(0x1b33e7cdaa3916be)}, .features = machine_x64_avx512f_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features), .operand_count = 3, .operand_slots = {0, 1, 2},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT},
     .operand_widths = {512, 512, 512}},
};

BUSTER_GLOBAL_LOCAL MachineX64ExactRecipe const machine_x64_family_exact_recipe_table[MACHINE_X86_64_EMIT_REGISTRY_FAMILY_COUNT] = {
    [0] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 0,
        .key = {MACHINE_X64_MOV_IMMEDIATE_EXACT_FORM_ID, UINT64_C(0x2a2535c90ada7adc)},
        .operand_count = 2,
        .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD},
        .operand_widths = {32, 32},
        .variant_count = BUSTER_ARRAY_LENGTH(machine_x64_mov_ri_variants),
        .variant_selector = MACHINE_X64_EXACT_VARIANT_MOV_IMMEDIATE,
        .variants = machine_x64_mov_ri_variants,
    },
    [1] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 1,
        .key = {MACHINE_X64_LEA_EXACT_FORM_ID, UINT64_C(0x0b357f27b62f3409)},
        .operand_count = 2,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD},
        .operand_widths = {64, 64},
    },
    [2] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 2,
        .key = {MACHINE_X64_ADD_IMMEDIATE8_EXACT_FORM_ID, UINT64_C(0xc4d75f09ceeb4f69)},
        .operand_count = 2,
        .operand_slots = {0, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD},
        .operand_widths = {64, 8},
        .variant_count = BUSTER_ARRAY_LENGTH(machine_x64_add_imm_variants),
        .variant_selector = MACHINE_X64_EXACT_VARIANT_SIGNED_IMMEDIATE,
        .variants = machine_x64_add_imm_variants,
    },
    [3] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 3,
        .key = {MACHINE_X64_IMUL_IMMEDIATE8_EXACT_FORM_ID, UINT64_C(0xcc57ef111fd55ec5)},
        .operand_count = 3,
        .operand_slots = {0, 1, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD},
        .operand_widths = {64, 64, 8},
        .variant_count = BUSTER_ARRAY_LENGTH(machine_x64_imul_imm_variants),
        .variant_selector = MACHINE_X64_EXACT_VARIANT_SIGNED_IMMEDIATE,
        .variants = machine_x64_imul_imm_variants,
    },
    [4] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 4,
        .key = {MACHINE_X64_MOV_MEMORY_EXACT_FORM_ID, UINT64_C(0xca30e68cfa1406bc)},
        .operand_count = 2,
        .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD},
        .operand_widths = {64, 64},
    },
    [5] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 5,
        .key = {MACHINE_X64_MOV_MEMORY8_EXACT_FORM_ID, UINT64_C(0xe7a77cae08617d2d)},
        .operand_count = 2,
        .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD, MACHINE_X64_EXACT_OPERAND_GPR},
        .operand_widths = {8, 8},
    },
    [6] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 6,
        .key = {MACHINE_X64_MOV_MEMORY_FULL_EXACT_FORM_ID, UINT64_C(0xa4ef94df2e338694)},
        .operand_count = 2,
        .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD, MACHINE_X64_EXACT_OPERAND_GPR},
        .operand_widths = {16, 16},
    },
    [7] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 7,
        .key = {MACHINE_X64_MOV_MEMORY_FULL_EXACT_FORM_ID, UINT64_C(0xa4ef94df2e338694)},
        .operand_count = 2,
        .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD, MACHINE_X64_EXACT_OPERAND_GPR},
        .operand_widths = {32, 32},
    },
    [8] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 8,
        .key = {MACHINE_X64_MOV_MEMORY_FULL_EXACT_FORM_ID, UINT64_C(0xa4ef94df2e338694)},
        .operand_count = 2,
        .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD, MACHINE_X64_EXACT_OPERAND_GPR},
        .operand_widths = {64, 64},
    },
    [9] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 9,
        .key = {MACHINE_X64_MOVZX_MEMORY8_EXACT_FORM_ID, UINT64_C(0x6d04093431c32330)},
        .operand_count = 2,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO},
        .operand_widths = {64, 8},
    },
    [10] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 10,
        .key = {MACHINE_X64_MOVZX_MEMORY16_EXACT_FORM_ID, UINT64_C(0xa910655fd16f6729)},
        .operand_count = 2,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO},
        .operand_widths = {64, 16},
    },
    [11] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 11,
        .key = {MACHINE_X64_MOV_MEMORY_EXACT_FORM_ID, UINT64_C(0xca30e68cfa1406bc)},
        .operand_count = 2,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO},
        .operand_widths = {32, 32},
    },
    [12] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 12,
        .key = {MACHINE_X64_MOV_MEMORY_EXACT_FORM_ID, UINT64_C(0xca30e68cfa1406bc)},
        .operand_count = 2,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO},
        .operand_widths = {64, 64},
    },
    [13] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 13,
        .key = {MACHINE_X64_MOV_MEMORY8_EXACT_FORM_ID, UINT64_C(0xe7a77cae08617d2d)},
        .operand_count = 2,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO, MACHINE_X64_EXACT_OPERAND_GPR},
        .operand_widths = {8, 8},
    },
    [14] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 14,
        .key = {MACHINE_X64_MOV_MEMORY_FULL_EXACT_FORM_ID, UINT64_C(0xa4ef94df2e338694)},
        .operand_count = 2,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO, MACHINE_X64_EXACT_OPERAND_GPR},
        .operand_widths = {16, 16},
    },
    [15] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 15,
        .key = {MACHINE_X64_MOV_MEMORY_FULL_EXACT_FORM_ID, UINT64_C(0xa4ef94df2e338694)},
        .operand_count = 2,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO, MACHINE_X64_EXACT_OPERAND_GPR},
        .operand_widths = {32, 32},
    },
    [16] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 16,
        .key = {MACHINE_X64_MOV_MEMORY_FULL_EXACT_FORM_ID, UINT64_C(0xa4ef94df2e338694)},
        .operand_count = 2,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO, MACHINE_X64_EXACT_OPERAND_GPR},
        .operand_widths = {64, 64},
    },
    [17] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 17,
        .key = {MACHINE_X64_LEA_EXACT_FORM_ID, UINT64_C(0x0b357f27b62f3409)},
        .operand_count = 2,
        .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32,
        .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD},
        .operand_widths = {64, 64},
    },
    [18] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 18,
        .key = {MACHINE_X64_PUSH_FRAME_EXACT_FORM_ID, UINT64_C(0x18f2cf99c5297c27)},
        .operand_count = 1, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32,
        .operand_slots = {0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD}, .operand_widths = {64},
    },
    [19] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 19,
        .key = {MACHINE_X64_VMOV_RR_EXACT_FORM_ID, UINT64_C(0xea537c4b94111b09)},
        .operand_count = 2, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_SELF_COPY_NOOP, .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {512, 512},
        .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
    },
    [20] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 20,
        .key = {MACHINE_X64_VLOAD_EXACT_FORM_ID, UINT64_C(0x62bba0430900201e)},
        .operand_count = 2, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32, .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_SLOT}, .operand_widths = {512, 8},
        .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
    },
    [21] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 21,
        .key = {MACHINE_X64_VSTORE_EXACT_FORM_ID, UINT64_C(0xa3f8bbb35dcafc20)},
        .operand_count = 2, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32, .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {8, 512},
        .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
    },
    [22] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 22,
        .key = {MACHINE_X64_VLOAD_EXACT_FORM_ID, UINT64_C(0x62bba0430900201e)}, .operand_count = 2, .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO}, .operand_widths = {512, 8},
        .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
    },
    [23] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 23,
        .key = {MACHINE_X64_VSTORE_EXACT_FORM_ID, UINT64_C(0xa3f8bbb35dcafc20)}, .operand_count = 2, .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {8, 512},
        .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
    },
    [24] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 24,
        .key = {MACHINE_X64_VPSLLD_EXACT_FORM_ID, UINT64_C(0x47cd9f2fa72e4408)}, .operand_count = 3, .operand_slots = {0, 1, 0},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD}, .operand_widths = {512, 512, 8},
        .features = machine_x64_avx512f_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features),
    },
    [25] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 25,
        .key = {MACHINE_X64_VPADDB_EXACT_FORM_ID, UINT64_C(0x9fc171a572e6eeae)}, .operand_count = 3, .operand_slots = {0, 1, 2},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {512, 512, 512},
        .variant_count = MACHINE_X64_VBINARY_VARIANT_COUNT, .variant_selector = MACHINE_X64_EXACT_VARIANT_VBINARY, .variants = machine_x64_vbinary_variants,
        .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
    },
    [32] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 32, .key = {MACHINE_X64_VSPLATB_EXACT_FORM_ID, UINT64_C(0x5db141a5417ed3f6)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {512, 32},
        .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
    },
    [49] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 49, .key = {MACHINE_X64_VSPLATD_EXACT_FORM_ID, UINT64_C(0xf65793e9357098c7)},
        .operand_count = 2, .operand_slots = {0, 1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {512, 32},
        .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
    },
    [33] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 33, .key = {MACHINE_X64_VPTERNLOGD_EXACT_FORM_ID, UINT64_C(0x61fc34c1d8f6da45)},
        .operand_count = 4, .operand_slots = {0, 1, 2, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD}, .operand_widths = {512, 512, 512, 8},
        .features = machine_x64_avx512f_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features),
    },
    [34] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 34, .key = {MACHINE_X64_XCHG_EXACT_FORM_ID, UINT64_C(0x100612d3d9f27042)},
        .operand_count = 2, .operand_slots = {0, 1},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD_SIZE, MACHINE_X64_EXACT_OPERAND_GPR_PAYLOAD_SIZE},
    },
    [35] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 35, .key = {MACHINE_X64_CMPXCHG_EXACT_FORM_ID, UINT64_C(0x46a888e2c049f87a)},
        .operand_count = 2, .operand_slots = {1, 3}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD_SIZE, MACHINE_X64_EXACT_OPERAND_GPR_PAYLOAD_SIZE},
    },
    [46] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 46, .key = {MACHINE_X64_SUB_RSP_EXACT_FORM_ID, UINT64_C(0xb9527cc38accf111)},
        .operand_count = 2, .operand_slots = {0, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_FIXED_RSP, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD}, .operand_widths = {64, 32},
    },
};

// Composite descriptors are dense by family index.  Rows not yet carrying a
// validated metadata vocabulary remain zero and consequently fail closed in
// the worker lane rather than re-entering the handwritten switch.
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_cmpxchg_sequence_steps[] = {
    {
        .key = {MACHINE_X64_MOV_RR_EXACT_FORM_ID, UINT64_C(0x3ab69ab9d0d06329)},
        .operand_count = 2, .operand_slots = {0, 2},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX, MACHINE_X64_EXACT_OPERAND_GPR},
        .operand_widths = {64, 64},
    },
    {
        .key = {MACHINE_X64_CMPXCHG_EXACT_FORM_ID, UINT64_C(0x46a888e2c049f87a)},
        .operand_count = 2, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_LOCK,
        .operand_slots = {1, 3},
        .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD_SIZE, MACHINE_X64_EXACT_OPERAND_GPR_PAYLOAD_SIZE},
        .operand_widths = {0, 0},
    },
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_cmpxchg_sequence_variants[] = {
    {.step_count = 2, .steps = machine_x64_cmpxchg_sequence_steps},
};

// Scalar floating-point rows retain the canonical two-address scratch
// convention (XMM0 receives the integer bit pattern, the conversion reads
// and writes XMM0, and the result is copied back to the allocated GPR).  Each
// byte-producing step is a durable metadata form; the sequence merely stages
// the fixed scratch register through the existing exact MOVQ forms.
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_cvt_f32_f64_sequence_steps[] = {
    {.key = {MACHINE_X64_MOVQ_TO_XMM_EXACT_FORM_ID, UINT64_C(0x7bd465046ab10c4f)}, .features = machine_x64_sse2_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 1},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {128, 64}},
    {.key = {10509u, UINT64_C(0xd0ebceb2f65c639d)}, .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 0},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_XMM_FIXED0}, .operand_widths = {128, 128}},
    {.key = {MACHINE_X64_MOVQ_FROM_XMM_EXACT_FORM_ID, UINT64_C(0x3698d9bff62c4360)}, .features = machine_x64_sse2_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 0},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_XMM_FIXED0}, .operand_widths = {64, 128}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_cvt_f64_f32_sequence_steps[] = {
    {.key = {MACHINE_X64_MOVQ_TO_XMM_EXACT_FORM_ID, UINT64_C(0x7bd465046ab10c4f)}, .features = machine_x64_sse2_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 1},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {128, 64}},
    {.key = {10541u, UINT64_C(0x1d8915237e1a0dca)}, .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 0},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_XMM_FIXED0}, .operand_widths = {128, 128}},
    {.key = {MACHINE_X64_MOVQ_FROM_XMM_EXACT_FORM_ID, UINT64_C(0x3698d9bff62c4360)}, .features = machine_x64_sse2_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 0},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_XMM_FIXED0}, .operand_widths = {64, 128}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_cvt_i64_f32_sequence_steps[] = {
    {.key = {10436u, UINT64_C(0x8632f672b995b8ef)}, .features = machine_x64_sse_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse_features), .operand_count = 2, .operand_slots = {0, 1},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {128, 64}},
    {.key = {MACHINE_X64_MOVQ_FROM_XMM_EXACT_FORM_ID, UINT64_C(0x3698d9bff62c4360)}, .features = machine_x64_sse2_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 0},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_XMM_FIXED0}, .operand_widths = {64, 128}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_cvt_i64_f64_sequence_steps[] = {
    {.key = {10463u, UINT64_C(0x6b1b449f0ed1e10e)}, .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 1},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {128, 64}},
    {.key = {MACHINE_X64_MOVQ_FROM_XMM_EXACT_FORM_ID, UINT64_C(0x3698d9bff62c4360)}, .features = machine_x64_sse2_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 0},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_XMM_FIXED0}, .operand_widths = {64, 128}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_cvt_f32_i64_sequence_steps[] = {
    {.key = {MACHINE_X64_MOVQ_TO_XMM_EXACT_FORM_ID, UINT64_C(0x7bd465046ab10c4f)}, .features = machine_x64_sse2_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 1},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {128, 64}},
    {.key = {10440u, UINT64_C(0x443fee16922dd4d2)}, .features = machine_x64_sse_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse_features), .operand_count = 2, .operand_slots = {0, 0},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_XMM_FIXED0}, .operand_widths = {64, 128}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_cvt_f64_i64_sequence_steps[] = {
    {.key = {MACHINE_X64_MOVQ_TO_XMM_EXACT_FORM_ID, UINT64_C(0x7bd465046ab10c4f)}, .features = machine_x64_sse2_features,
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 1},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {128, 64}},
    {.key = {10467u, UINT64_C(0xdb7e2aa7c56853cf)}, .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 0},
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_XMM_FIXED0}, .operand_widths = {64, 128}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_cvt_f32_f64_sequence_variants[] = {{.step_count = 3, .steps = machine_x64_cvt_f32_f64_sequence_steps}};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_cvt_f64_f32_sequence_variants[] = {{.step_count = 3, .steps = machine_x64_cvt_f64_f32_sequence_steps}};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_cvt_i64_f32_sequence_variants[] = {{.step_count = 2, .steps = machine_x64_cvt_i64_f32_sequence_steps}};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_cvt_i64_f64_sequence_variants[] = {{.step_count = 2, .steps = machine_x64_cvt_i64_f64_sequence_steps}};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_cvt_f32_i64_sequence_variants[] = {{.step_count = 2, .steps = machine_x64_cvt_f32_i64_sequence_steps}};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_cvt_f64_i64_sequence_variants[] = {{.step_count = 2, .steps = machine_x64_cvt_f64_i64_sequence_steps}};

// Scalar floating arithmetic uses the same two-address XMM scratch convention
// as the legacy selector.  The arithmetic forms themselves are canonical
// metadata tokens; all integer/XMM staging remains in the exact sequence.
#define MACHINE_X64_FP_MOVQ_TO_STEP(slot_value) \
    {.key = {MACHINE_X64_MOVQ_TO_XMM_EXACT_FORM_ID, UINT64_C(0x7bd465046ab10c4f)}, .features = machine_x64_sse2_features, \
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, slot_value}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {128, 64}}
#define MACHINE_X64_FP_MOVQ_TO_XMM1_STEP(slot_value) \
    {.key = {MACHINE_X64_MOVQ_TO_XMM_EXACT_FORM_ID, UINT64_C(0x7bd465046ab10c4f)}, .features = machine_x64_sse2_features, \
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, slot_value}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED1, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {128, 64}}
#define MACHINE_X64_FP_ARITH_STEP(form_id, form_hash) \
    {.key = {form_id, form_hash}, .features = machine_x64_sse_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse_features), .operand_count = 2, .operand_slots = {0, 0}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_XMM_FIXED1}, .operand_widths = {128, 128}}
#define MACHINE_X64_FP_ARITH_SSE2_STEP(form_id, form_hash) \
    {.key = {form_id, form_hash}, .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 0}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_XMM_FIXED1}, .operand_widths = {128, 128}}
#define MACHINE_X64_FP_MOVQ_FROM_STEP \
    {.key = {MACHINE_X64_MOVQ_FROM_XMM_EXACT_FORM_ID, UINT64_C(0x3698d9bff62c4360)}, .features = machine_x64_sse2_features, \
     .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 0}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_XMM_FIXED0}, .operand_widths = {64, 128}}

BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_farith_sequence_steps[8][4] = {
    [0] = {MACHINE_X64_FP_MOVQ_TO_STEP(1), MACHINE_X64_FP_MOVQ_TO_XMM1_STEP(2), MACHINE_X64_FP_ARITH_STEP(10505u, 0xb7548ac43adc345e), MACHINE_X64_FP_MOVQ_FROM_STEP},
    [1] = {MACHINE_X64_FP_MOVQ_TO_STEP(1), MACHINE_X64_FP_MOVQ_TO_XMM1_STEP(2), MACHINE_X64_FP_ARITH_STEP(10513u, 0x9444e608b81120dc), MACHINE_X64_FP_MOVQ_FROM_STEP},
    [2] = {MACHINE_X64_FP_MOVQ_TO_STEP(1), MACHINE_X64_FP_MOVQ_TO_XMM1_STEP(2), MACHINE_X64_FP_ARITH_STEP(10507u, 0x5cc22ab1a19a6e33), MACHINE_X64_FP_MOVQ_FROM_STEP},
    [3] = {MACHINE_X64_FP_MOVQ_TO_STEP(1), MACHINE_X64_FP_MOVQ_TO_XMM1_STEP(2), MACHINE_X64_FP_ARITH_STEP(10517u, 0xa367cc47795a1141), MACHINE_X64_FP_MOVQ_FROM_STEP},
    [4] = {MACHINE_X64_FP_MOVQ_TO_STEP(1), MACHINE_X64_FP_MOVQ_TO_XMM1_STEP(2), MACHINE_X64_FP_ARITH_SSE2_STEP(10537u, 0x64c1f66df6f45674), MACHINE_X64_FP_MOVQ_FROM_STEP},
    [5] = {MACHINE_X64_FP_MOVQ_TO_STEP(1), MACHINE_X64_FP_MOVQ_TO_XMM1_STEP(2), MACHINE_X64_FP_ARITH_SSE2_STEP(10543u, 0x1b16db007692b16b), MACHINE_X64_FP_MOVQ_FROM_STEP},
    [6] = {MACHINE_X64_FP_MOVQ_TO_STEP(1), MACHINE_X64_FP_MOVQ_TO_XMM1_STEP(2), MACHINE_X64_FP_ARITH_SSE2_STEP(10539u, 0xa16076f1418ba607), MACHINE_X64_FP_MOVQ_FROM_STEP},
    [7] = {MACHINE_X64_FP_MOVQ_TO_STEP(1), MACHINE_X64_FP_MOVQ_TO_XMM1_STEP(2), MACHINE_X64_FP_ARITH_SSE2_STEP(10547u, 0x96856051e8170cbf), MACHINE_X64_FP_MOVQ_FROM_STEP},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_farith_sequence_variants[] = {
    [0] = {.step_count = 4, .steps = machine_x64_farith_sequence_steps[0]},
    [1] = {.step_count = 4, .steps = machine_x64_farith_sequence_steps[1]},
    [2] = {.step_count = 4, .steps = machine_x64_farith_sequence_steps[2]},
    [3] = {.step_count = 4, .steps = machine_x64_farith_sequence_steps[3]},
    [4] = {.step_count = 4, .steps = machine_x64_farith_sequence_steps[4]},
    [5] = {.step_count = 4, .steps = machine_x64_farith_sequence_steps[5]},
    [6] = {.step_count = 4, .steps = machine_x64_farith_sequence_steps[6]},
    [7] = {.step_count = 4, .steps = machine_x64_farith_sequence_steps[7]},
};

// FCMP_SET's parity repair is encoded with the exact SETNP/AND pair as the
// primary token.  For the OR-parity variants the worker swaps those two
// metadata tokens for SETP/OR; no byte template enters the sequence path.
#define MACHINE_X64_FCMP_MOVQ_TO_STEP(slot_value) MACHINE_X64_FP_MOVQ_TO_STEP(slot_value)
#define MACHINE_X64_FCMP_MOVQ_TO_XMM1_STEP(slot_value) MACHINE_X64_FP_MOVQ_TO_XMM1_STEP(slot_value)
#define MACHINE_X64_FCMP_COMI_STEP(form_id, form_hash) \
    {.key = {form_id, form_hash}, .features = machine_x64_sse2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features), .operand_count = 2, .operand_slots = {0, 0}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_FIXED0, MACHINE_X64_EXACT_OPERAND_XMM_FIXED1}, .operand_widths = {128, 128}}
#define MACHINE_X64_FCMP_SETCC_STEP(form_id, form_hash) \
    {.key = {form_id, form_hash}, .operand_count = 1, .operand_slots = {0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX}, \
     .operand_widths = {8}}
#define MACHINE_X64_FCMP_PARITY_SETNP_STEP \
    {.key = {10649u, UINT64_C(0xd15c6a2ed2b79fc2)}, .operand_count = 1, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_PARITY_ONLY, .operand_slots = {0}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RDX}, .operand_widths = {8}}
#define MACHINE_X64_FCMP_PARITY_AND_STEP \
    {.key = {9672u, UINT64_C(0xcbdfecbe7e216f54)}, .operand_count = 2, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_PARITY_ONLY | MACHINE_X64_EXACT_RECIPE_FLAG_PARITY_OR, .operand_slots = {0, 0}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX, MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RDX}, .operand_widths = {8, 8}}
#define MACHINE_X64_FCMP_MOVZX_STEP \
    {.key = {MACHINE_X64_MOVZX8_RR_EXACT_FORM_ID, UINT64_C(0xa9d675ab86fb1641)}, .operand_count = 2, .operand_slots = {0, 0}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX, MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX}, .operand_widths = {64, 8}}
#define MACHINE_X64_FCMP_STEPS(comi_id, comi_hash, set_id, set_hash) \
    {MACHINE_X64_FCMP_MOVQ_TO_STEP(1), MACHINE_X64_FCMP_MOVQ_TO_XMM1_STEP(2), MACHINE_X64_FCMP_COMI_STEP(comi_id, comi_hash), \
     MACHINE_X64_FCMP_SETCC_STEP(set_id, set_hash), MACHINE_X64_FCMP_PARITY_SETNP_STEP, MACHINE_X64_FCMP_PARITY_AND_STEP, MACHINE_X64_FCMP_MOVZX_STEP}

BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_fcmp_set_sequence_steps[12][7] = {
    [0] = MACHINE_X64_FCMP_STEPS(10432u, 0x513be0a63ce4f782, 10265u, 0x261b81212af08017),
    [1] = MACHINE_X64_FCMP_STEPS(10432u, 0x513be0a63ce4f782, 10267u, 0x99647caf50cf7fff),
    [2] = MACHINE_X64_FCMP_STEPS(10432u, 0x513be0a63ce4f782, 10261u, 0x0bc47fa18ee6a6de),
    [3] = MACHINE_X64_FCMP_STEPS(10432u, 0x513be0a63ce4f782, 10269u, 0x73419bd793371f04),
    [4] = MACHINE_X64_FCMP_STEPS(10432u, 0x513be0a63ce4f782, 10271u, 0x4c681fe5d1e14b1),
    [5] = MACHINE_X64_FCMP_STEPS(10432u, 0x513be0a63ce4f782, 10263u, 0x7022443cd4a81cf5),
    [6] = MACHINE_X64_FCMP_STEPS(10459u, 0x32db225c1a1533da, 10265u, 0x261b81212af08017),
    [7] = MACHINE_X64_FCMP_STEPS(10459u, 0x32db225c1a1533da, 10267u, 0x99647caf50cf7fff),
    [8] = MACHINE_X64_FCMP_STEPS(10459u, 0x32db225c1a1533da, 10261u, 0x0bc47fa18ee6a6de),
    [9] = MACHINE_X64_FCMP_STEPS(10459u, 0x32db225c1a1533da, 10269u, 0x73419bd793371f04),
    [10] = MACHINE_X64_FCMP_STEPS(10459u, 0x32db225c1a1533da, 10271u, 0x4c681fe5d1e14b1),
    [11] = MACHINE_X64_FCMP_STEPS(10459u, 0x32db225c1a1533da, 10263u, 0x7022443cd4a81cf5),
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_fcmp_set_sequence_variants[] = {
    [0] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[0]},
    [1] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[1]},
    [2] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[2]},
    [3] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[3]},
    [4] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[4]},
    [5] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[5]},
    [6] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[6]},
    [7] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[7]},
    [8] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[8]},
    [9] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[9]},
    [10] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[10]},
    [11] = {.step_count = 7, .steps = machine_x64_fcmp_set_sequence_steps[11]},
};

BUSTER_GLOBAL_LOCAL BusterX86MetadataMachineExactToken machine_x64_fcmp_setp_token;
BUSTER_GLOBAL_LOCAL BusterX86MetadataMachineExactToken machine_x64_fcmp_or_token;
BUSTER_GLOBAL_LOCAL bool machine_x64_fcmp_alternate_tokens_valid;

#define MACHINE_X64_SETCC_STEP(form_id, form_hash) \
    {.key = {form_id, form_hash}, .operand_count = 1, .operand_slots = {0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX}, .operand_widths = {8}}
#define MACHINE_X64_SETCC_MOVZX_STEP \
    {.key = {MACHINE_X64_MOVZX8_RR_EXACT_FORM_ID, UINT64_C(0xa9d675ab86fb1641)}, .operand_count = 2, .operand_slots = {0, 0}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX, MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX}, .operand_widths = {64, 8}}
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_setcc_sequence_steps[16][2] = {
    [0] = {MACHINE_X64_SETCC_STEP(10257u, 0x31ddc1ed865a5575), MACHINE_X64_SETCC_MOVZX_STEP},
    [1] = {MACHINE_X64_SETCC_STEP(10259u, 0x81dd033fae75c1c6), MACHINE_X64_SETCC_MOVZX_STEP},
    [2] = {MACHINE_X64_SETCC_STEP(10261u, 0x0bc47fa18ee6a6de), MACHINE_X64_SETCC_MOVZX_STEP},
    [3] = {MACHINE_X64_SETCC_STEP(10263u, 0x7022443cd4a81cf5), MACHINE_X64_SETCC_MOVZX_STEP},
    [4] = {MACHINE_X64_SETCC_STEP(10265u, 0x261b81212af08017), MACHINE_X64_SETCC_MOVZX_STEP},
    [5] = {MACHINE_X64_SETCC_STEP(10267u, 0x99647caf50cf7fff), MACHINE_X64_SETCC_MOVZX_STEP},
    [6] = {MACHINE_X64_SETCC_STEP(10269u, 0x73419bd793371f04), MACHINE_X64_SETCC_MOVZX_STEP},
    [7] = {MACHINE_X64_SETCC_STEP(10271u, 0x4c681fe5d1e14b1), MACHINE_X64_SETCC_MOVZX_STEP},
    [8] = {MACHINE_X64_SETCC_STEP(10643u, 0x0f501b348d5ad3ab), MACHINE_X64_SETCC_MOVZX_STEP},
    [9] = {MACHINE_X64_SETCC_STEP(10645u, 0xe718192d18926b3f), MACHINE_X64_SETCC_MOVZX_STEP},
    [10] = {MACHINE_X64_SETCC_STEP(10647u, 0x65dc8e342334f3cb), MACHINE_X64_SETCC_MOVZX_STEP},
    [11] = {MACHINE_X64_SETCC_STEP(10649u, 0xd15c6a2ed2b79fc2), MACHINE_X64_SETCC_MOVZX_STEP},
    [12] = {MACHINE_X64_SETCC_STEP(10651u, 0xb45e8ae6fd038751), MACHINE_X64_SETCC_MOVZX_STEP},
    [13] = {MACHINE_X64_SETCC_STEP(10653u, 0x4efeb1d47cbd56a0), MACHINE_X64_SETCC_MOVZX_STEP},
    [14] = {MACHINE_X64_SETCC_STEP(10655u, 0x6e81bfd37e941496), MACHINE_X64_SETCC_MOVZX_STEP},
    [15] = {MACHINE_X64_SETCC_STEP(10657u, 0xa2843c8dd1c8c547), MACHINE_X64_SETCC_MOVZX_STEP},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_setcc_sequence_variants[] = {
    [0] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[0]},
    [1] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[1]},
    [2] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[2]},
    [3] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[3]},
    [4] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[4]},
    [5] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[5]},
    [6] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[6]},
    [7] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[7]},
    [8] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[8]},
    [9] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[9]},
    [10] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[10]},
    [11] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[11]},
    [12] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[12]},
    [13] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[13]},
    [14] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[14]},
    [15] = {.step_count = 2, .steps = machine_x64_setcc_sequence_steps[15]},
};

#define MACHINE_X64_JCC_STEP(form_id, form_hash) \
    {.key = {form_id, form_hash}, .operand_count = 1, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_BRANCH_FIXUP, .operand_slots = {0}, \
     .operand_kinds = {MACHINE_X64_EXACT_OPERAND_RELATIVE_ZERO}, .operand_widths = {32}}
#define MACHINE_X64_JMP_STEP \
    {.key = {MACHINE_X64_JMP_EXACT_FORM_ID, UINT64_C(0xab9c4b53fce14f6e)}, .operand_count = 1, .flags = MACHINE_X64_EXACT_RECIPE_FLAG_BRANCH_FIXUP, \
     .operand_slots = {1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_RELATIVE_ZERO}, .operand_widths = {32}}
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_jcc_sequence_steps[16][2] = {
    [0] = {MACHINE_X64_JCC_STEP(10240u, 0x663680e7ff926f87), MACHINE_X64_JMP_STEP},
    [1] = {MACHINE_X64_JCC_STEP(10243u, 0x5b0b1a9540ee71fb), MACHINE_X64_JMP_STEP},
    [2] = {MACHINE_X64_JCC_STEP(10245u, 0x311d2176cc680771), MACHINE_X64_JMP_STEP},
    [3] = {MACHINE_X64_JCC_STEP(10247u, 0xb78eb0afc41232dd), MACHINE_X64_JMP_STEP},
    [4] = {MACHINE_X64_JCC_STEP(10249u, 0x5b6e3cd6eb63b76c), MACHINE_X64_JMP_STEP},
    [5] = {MACHINE_X64_JCC_STEP(10251u, 0x4d8e220c403f8696), MACHINE_X64_JMP_STEP},
    [6] = {MACHINE_X64_JCC_STEP(10253u, 0xd3e93216ca69f952), MACHINE_X64_JMP_STEP},
    [7] = {MACHINE_X64_JCC_STEP(10255u, 0x0da05f55f9ead40a), MACHINE_X64_JMP_STEP},
    [8] = {MACHINE_X64_JCC_STEP(10627u, 0x1b3720aa4829444c), MACHINE_X64_JMP_STEP},
    [9] = {MACHINE_X64_JCC_STEP(10629u, 0x744e31783e151c7b), MACHINE_X64_JMP_STEP},
    [10] = {MACHINE_X64_JCC_STEP(10631u, 0xf3d3864103433402), MACHINE_X64_JMP_STEP},
    [11] = {MACHINE_X64_JCC_STEP(10633u, 0xf6a6442e85dfa8e4), MACHINE_X64_JMP_STEP},
    [12] = {MACHINE_X64_JCC_STEP(10635u, 0xad1a1b6b4487b442), MACHINE_X64_JMP_STEP},
    [13] = {MACHINE_X64_JCC_STEP(10637u, 0xdc02d320fd463c30), MACHINE_X64_JMP_STEP},
    [14] = {MACHINE_X64_JCC_STEP(10639u, 0xbbb60554e5ce4380), MACHINE_X64_JMP_STEP},
    [15] = {MACHINE_X64_JCC_STEP(10641u, 0x6c77826ff61644d0), MACHINE_X64_JMP_STEP},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_jcc_sequence_variants[] = {
    [0] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[0]},
    [1] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[1]},
    [2] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[2]},
    [3] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[3]},
    [4] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[4]},
    [5] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[5]},
    [6] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[6]},
    [7] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[7]},
    [8] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[8]},
    [9] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[9]},
    [10] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[10]},
    [11] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[11]},
    [12] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[12]},
    [13] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[13]},
    [14] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[14]},
    [15] = {.step_count = 2, .steps = machine_x64_jcc_sequence_steps[15]},
};

#undef MACHINE_X64_JMP_STEP
#undef MACHINE_X64_JCC_STEP
#undef MACHINE_X64_SETCC_MOVZX_STEP
#undef MACHINE_X64_SETCC_STEP

#undef MACHINE_X64_FCMP_STEPS
#undef MACHINE_X64_FCMP_MOVZX_STEP
#undef MACHINE_X64_FCMP_PARITY_AND_STEP
#undef MACHINE_X64_FCMP_PARITY_SETNP_STEP
#undef MACHINE_X64_FCMP_SETCC_STEP
#undef MACHINE_X64_FCMP_COMI_STEP
#undef MACHINE_X64_FCMP_MOVQ_TO_STEP
#undef MACHINE_X64_FCMP_MOVQ_TO_XMM1_STEP
#undef MACHINE_X64_FP_MOVQ_FROM_STEP
#undef MACHINE_X64_FP_ARITH_STEP
#undef MACHINE_X64_FP_ARITH_SSE2_STEP
#undef MACHINE_X64_FP_MOVQ_TO_STEP
#undef MACHINE_X64_FP_MOVQ_TO_XMM1_STEP

// AVX-512 masked rows use k1 as the machine scratch mask.  KMOVQ is itself a
// metadata form, so staging the incoming general-register mask never falls
// back to a byte template.  The EVEX forms omit MASK1 from the physical list;
// the machine-only mask/zeroing attributes supply that decorator exactly.
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vload_ptr_masked_sequence_steps[] = {
    {.key = {6896u, UINT64_C(0x105806391b8c13c8)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 2}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {64, 64}},
    {.key = {MACHINE_X64_VLOAD_EXACT_FORM_ID, UINT64_C(0x62bba0430900201e)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO}, .operand_widths = {512, 8}, .mask_register_plus_one = 2, .zeroing = true},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vstore_ptr_masked_sequence_steps[] = {
    {.key = {6896u, UINT64_C(0x105806391b8c13c8)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {64, 64}},
    {.key = {MACHINE_X64_VSTORE_EXACT_FORM_ID, UINT64_C(0xa3f8bbb35dcafc20)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 2}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {8, 512}, .mask_register_plus_one = 2},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vcompress_store_ptr_sequence_steps[] = {
    {.key = {6896u, UINT64_C(0x105806391b8c13c8)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {64, 64}},
    {.key = {8910u, UINT64_C(0x68e38f515bab1a21)}, .features = machine_x64_avx512vbmi2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512vbmi2_features),
     .operand_count = 2, .operand_slots = {0, 2}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {8, 512}, .mask_register_plus_one = 2},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vpcmp_equal_sequence_steps[] = {
    {.key = {5883u, UINT64_C(0xb5f8bb7935287038)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 3, .operand_slots = {0, 1, 2}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {64, 512, 512}},
    {.key = {6897u, UINT64_C(0x12ab4073f19fd9ef)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1}, .operand_widths = {64, 64}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vpcmp_less_sequence_steps[] = {
    {.key = {5927u, UINT64_C(0x328655b53fe636a2)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 4, .operand_slots = {0, 1, 2, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_CONSTANT}, .operand_widths = {64, 512, 512, 8}},
    {.key = {6897u, UINT64_C(0x12ab4073f19fd9ef)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1}, .operand_widths = {64, 64}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vpcmp_test_sequence_steps[] = {
    {.key = {6647u, UINT64_C(0x1c172153a615826b)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 3, .operand_slots = {0, 1, 2}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {64, 512, 512}},
    {.key = {6897u, UINT64_C(0x12ab4073f19fd9ef)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1}, .operand_widths = {64, 64}},
};
// vpcmpeqd writes 16 mask bits and zeroes the rest of k1, so the shared
// 64-bit KMOVQ step delivers the exact Mask64 the dword compare defines.
// vpcmpud takes its predicate as an immediate, and the vpcmp family already
// spends the machine payload on the variant index, so the predicate cannot
// ride the payload the way vpcmpub's does (there payload 1 happens to be the
// unsigned-less predicate too). MACHINE_X64_EXACT_OPERAND_IMMEDIATE_LESS
// carries it instead.
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vpcmp_less_word_sequence_steps[] = {
    {.key = {7550u, UINT64_C(0xbd651626b11cf5bf)}, .features = machine_x64_avx512f_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features),
     .operand_count = 4, .operand_slots = {0, 1, 2, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_LESS}, .operand_widths = {64, 512, 512, 8}},
    {.key = {6897u, UINT64_C(0x12ab4073f19fd9ef)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1}, .operand_widths = {64, 64}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vpcmp_equal_word_sequence_steps[] = {
    {.key = {7540u, UINT64_C(0x2c3485903430167f)}, .features = machine_x64_avx512f_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features),
     .operand_count = 3, .operand_slots = {0, 1, 2}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {64, 512, 512}},
    {.key = {6897u, UINT64_C(0x12ab4073f19fd9ef)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1}, .operand_widths = {64, 64}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_vload_ptr_masked_sequence_variants[] = {{.step_count = 2, .steps = machine_x64_vload_ptr_masked_sequence_steps}};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_vstore_ptr_masked_sequence_variants[] = {{.step_count = 2, .steps = machine_x64_vstore_ptr_masked_sequence_steps}};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_vcompress_store_ptr_sequence_variants[] = {{.step_count = 2, .steps = machine_x64_vcompress_store_ptr_sequence_steps}};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_vpcmp_sequence_variants[] = {
    {.step_count = 2, .steps = machine_x64_vpcmp_equal_sequence_steps},
    {.step_count = 2, .steps = machine_x64_vpcmp_less_sequence_steps},
    {.step_count = 2, .steps = machine_x64_vpcmp_test_sequence_steps},
    {.step_count = 2, .steps = machine_x64_vpcmp_equal_word_sequence_steps},
    {.step_count = 2, .steps = machine_x64_vpcmp_less_word_sequence_steps},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vpmovb2m_sequence_steps[] = {
    {.key = {6183u, UINT64_C(0x845181de5363cb8d)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {64, 512}},
    {.key = {6897u, UINT64_C(0x12ab4073f19fd9ef)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_GPR, MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1}, .operand_widths = {64, 64}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vpermt2b_sequence_steps[] = {
    {.key = {6896u, UINT64_C(0x105806391b8c13c8)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {64, 64}},
    {.key = {7901u, UINT64_C(0xf776ce35d04b2826)}, .features = machine_x64_avx512vbmi_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512vbmi_features),
     .operand_count = 3, .operand_slots = {0, 2, 3}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {512, 512, 512}, .mask_register_plus_one = 2, .zeroing = true},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vcompressb_sequence_steps[] = {
    {.key = {6896u, UINT64_C(0x105806391b8c13c8)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {64, 64}},
    {.key = {8911u, UINT64_C(0xe4d1b4ecc7503fab)}, .features = machine_x64_avx512vbmi2_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512vbmi2_features),
     .operand_count = 2, .operand_slots = {0, 2}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {512, 512}, .mask_register_plus_one = 2, .zeroing = true},
};
// The dword compress shares the family: payload 1 selects it, and only the
// vector instruction differs — vpcompressd is plain AVX-512F where the byte
// form needs VBMI2, and it consumes 16 mask bits from the same 64-bit KMOVQ.
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vcompressd_sequence_steps[] = {
    {.key = {6896u, UINT64_C(0x105806391b8c13c8)}, .features = machine_x64_avx512bw_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512bw_features),
     .operand_count = 2, .operand_slots = {0, 1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1, MACHINE_X64_EXACT_OPERAND_GPR}, .operand_widths = {64, 64}},
    {.key = {7555u, UINT64_C(0xf39881ff9abad52e)}, .features = machine_x64_avx512f_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features),
     .operand_count = 2, .operand_slots = {0, 2}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT}, .operand_widths = {512, 512}, .mask_register_plus_one = 2, .zeroing = true},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_vpmovb2m_sequence_variants[] = {{.step_count = 2, .steps = machine_x64_vpmovb2m_sequence_steps}};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_vpermt2b_sequence_variants[] = {{.step_count = 2, .steps = machine_x64_vpermt2b_sequence_steps}};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_vcompressb_sequence_variants[] = {
    {.step_count = 2, .steps = machine_x64_vcompressb_sequence_steps},
    {.step_count = 2, .steps = machine_x64_vcompressd_sequence_steps},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vpmovzxbd_q0_steps[] = {
    {.key = {7658u, UINT64_C(0x1646d75ce384e74a)}, .features = machine_x64_avx512f_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features),
     .operand_count = 2, .operand_slots = {0, 1}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_XMM_SLOT}, .operand_widths = {512, 128}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceStep const machine_x64_vpmovzxbd_q_steps[] = {
    {.key = {7163u, UINT64_C(0x6d8890c0fa6f8622)}, .features = machine_x64_avx512f_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features),
     .operand_count = 3, .operand_slots = {0, 1, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_XMM_SLOT, MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_IMMEDIATE_CONSTANT}, .operand_widths = {128, 512, 8}},
    {.key = {7658u, UINT64_C(0x1646d75ce384e74a)}, .features = machine_x64_avx512f_features, .feature_count = BUSTER_ARRAY_LENGTH(machine_x64_avx512f_features),
     .operand_count = 2, .operand_slots = {0, 0}, .operand_kinds = {MACHINE_X64_EXACT_OPERAND_ZMM_SLOT, MACHINE_X64_EXACT_OPERAND_XMM_SLOT}, .operand_widths = {512, 128}},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequenceVariant const machine_x64_vpmovzxbd_sequence_variants[] = {
    {.step_count = 1, .steps = machine_x64_vpmovzxbd_q0_steps},
    {.step_count = 2, .steps = machine_x64_vpmovzxbd_q_steps},
    {.step_count = 2, .steps = machine_x64_vpmovzxbd_q_steps},
    {.step_count = 2, .steps = machine_x64_vpmovzxbd_q_steps},
};
BUSTER_GLOBAL_LOCAL MachineX64ExactSequence const machine_x64_exact_sequence_table[MACHINE_X86_64_EMIT_REGISTRY_FAMILY_COUNT] = {
    [26] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 26, .variant_count = 1, .variants = machine_x64_cvt_f32_f64_sequence_variants},
    [27] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 27, .variant_count = 1, .variants = machine_x64_cvt_f64_f32_sequence_variants},
    [28] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 28, .variant_count = 1, .variants = machine_x64_cvt_i64_f32_sequence_variants},
    [29] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 29, .variant_count = 1, .variants = machine_x64_cvt_i64_f64_sequence_variants},
    [30] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 30, .variant_count = 1, .variants = machine_x64_cvt_f32_i64_sequence_variants},
    [31] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 31, .variant_count = 1, .variants = machine_x64_cvt_f64_i64_sequence_variants},
    [36] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 36, .variant_count = 1, .variants = machine_x64_vload_ptr_masked_sequence_variants},
    [37] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 37, .variant_count = 1, .variants = machine_x64_vstore_ptr_masked_sequence_variants},
    [38] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 38, .variant_count = 1, .variants = machine_x64_vcompress_store_ptr_sequence_variants},
    [39] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 39, .variant_count = 5, .variant_selector = MACHINE_X64_EXACT_VARIANT_FIXED, .variants = machine_x64_vpcmp_sequence_variants},
    [40] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 40, .variant_count = 1, .variants = machine_x64_vpmovb2m_sequence_variants},
    [41] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 41, .variant_count = 1, .variants = machine_x64_vpermt2b_sequence_variants},
    [42] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 42, .variant_count = 2, .variant_selector = MACHINE_X64_EXACT_VARIANT_FIXED, .variants = machine_x64_vcompressb_sequence_variants},
    [43] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 43, .variant_count = 4, .variants = machine_x64_vpmovzxbd_sequence_variants},
    [35] = {
        .recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 35,
        .variant_count = 1, .variants = machine_x64_cmpxchg_sequence_variants,
    },
    [44] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 44, .variant_count = 8, .variants = machine_x64_farith_sequence_variants},
    [45] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 45, .variant_count = 12, .variants = machine_x64_fcmp_set_sequence_variants},
    [47] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 47, .variant_count = 16, .variants = machine_x64_setcc_sequence_variants},
    [48] = {.recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 48, .variant_count = 16, .variants = machine_x64_jcc_sequence_variants},
};

// DIRECT recipe index -> compact unique exact-plan identity.  Width variants
// and projection variants share a plan; LOAD_INCOMING has its own plan because
// its memory operand is a distinct metadata form.  The table is immutable;
// the plan values themselves are published only by
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
    [42] = MACHINE_X64_EXACT_PLAN_LOAD_INCOMING,
    [43] = MACHINE_X64_EXACT_PLAN_PUSH,
    [44] = MACHINE_X64_EXACT_PLAN_ADD_RSP,
    [45] = MACHINE_X64_EXACT_PLAN_MFENCE,
    [46] = MACHINE_X64_EXACT_PLAN_INT3,
};

BUSTER_GLOBAL_LOCAL u8 const machine_x64_family_exact_plan_id_by_recipe[MACHINE_X86_64_EMIT_REGISTRY_FAMILY_COUNT] = {
    [0] = MACHINE_X64_EXACT_PLAN_MOV_IMMEDIATE,
    [1] = MACHINE_X64_EXACT_PLAN_LEA_SYMBOL,
    [2] = MACHINE_X64_EXACT_PLAN_ADD_IMMEDIATE8,
    [3] = MACHINE_X64_EXACT_PLAN_IMUL_IMMEDIATE8,
    [4] = MACHINE_X64_EXACT_PLAN_LOAD_INCOMING,
    [5] = MACHINE_X64_EXACT_PLAN_STORE8,
    [6] = MACHINE_X64_EXACT_PLAN_STORE_FULL,
    [7] = MACHINE_X64_EXACT_PLAN_STORE_FULL,
    [8] = MACHINE_X64_EXACT_PLAN_STORE_FULL,
    [9] = MACHINE_X64_EXACT_PLAN_MOVZX8,
    [10] = MACHINE_X64_EXACT_PLAN_MOVZX16,
    [11] = MACHINE_X64_EXACT_PLAN_LOAD_INCOMING,
    [12] = MACHINE_X64_EXACT_PLAN_LOAD_INCOMING,
    [13] = MACHINE_X64_EXACT_PLAN_STORE8,
    [14] = MACHINE_X64_EXACT_PLAN_STORE_FULL,
    [15] = MACHINE_X64_EXACT_PLAN_STORE_FULL,
    [16] = MACHINE_X64_EXACT_PLAN_STORE_FULL,
    [17] = MACHINE_X64_EXACT_PLAN_LEA_SYMBOL,
    [18] = MACHINE_X64_EXACT_PLAN_PUSH_FRAME,
    [19] = MACHINE_X64_EXACT_PLAN_VMOV,
    [20] = MACHINE_X64_EXACT_PLAN_VLOAD,
    [21] = MACHINE_X64_EXACT_PLAN_VSTORE,
    [22] = MACHINE_X64_EXACT_PLAN_VLOAD,
    [23] = MACHINE_X64_EXACT_PLAN_VSTORE,
    [24] = MACHINE_X64_EXACT_PLAN_VPSLLD,
    [25] = MACHINE_X64_EXACT_PLAN_VPADDB,
    [32] = MACHINE_X64_EXACT_PLAN_VSPLATB,
    [33] = MACHINE_X64_EXACT_PLAN_VPTERNLOGD,
    [34] = MACHINE_X64_EXACT_PLAN_XCHG,
    [35] = MACHINE_X64_EXACT_PLAN_CMPXCHG,
    [46] = MACHINE_X64_EXACT_PLAN_SUB_RSP,
    [49] = MACHINE_X64_EXACT_PLAN_VSPLATD,
};

BUSTER_GLOBAL_LOCAL u8 machine_x64_exact_plan_id_for_recipe_variant(MachineEmitRecipeCategory category, u16 recipe_index, u32 variant_index)
{
    if (category == MACHINE_EMIT_RECIPE_CATEGORY_DIRECT)
    {
        return recipe_index < BUSTER_ARRAY_LENGTH(machine_x64_exact_plan_id_by_recipe) ? machine_x64_exact_plan_id_by_recipe[recipe_index]
                                                                                       : MACHINE_X64_EXACT_PLAN_INVALID;
    }
    if (category == MACHINE_EMIT_RECIPE_CATEGORY_FAMILY)
    {
        if (recipe_index < BUSTER_ARRAY_LENGTH(machine_x64_family_exact_plan_id_by_recipe))
        {
            if (recipe_index == 0 && variant_index == 1) return MACHINE_X64_EXACT_PLAN_MOV_SIGNED_IMMEDIATE;
            if (recipe_index == 2 && variant_index == 1) return MACHINE_X64_EXACT_PLAN_ADD_RSP;
            if (recipe_index == 3 && variant_index == 1) return MACHINE_X64_EXACT_PLAN_IMUL_IMMEDIATE32;
            if (recipe_index == 25 && variant_index <= 10) return (u8)(MACHINE_X64_EXACT_PLAN_VPADDB + variant_index);
            return machine_x64_family_exact_plan_id_by_recipe[recipe_index];
        }
    }
    return MACHINE_X64_EXACT_PLAN_INVALID;
}

// The encoder's hot row loop sees x86 opcodes as one contiguous ordinal span
// (MACHINE_X64_MOV_RI .. MACHINE_X64_VBINARY).  Keep the exact projection in
// that same dense namespace so workers do not re-enter the registry, decode a
// recipe category/index, or re-check a durable form key for every row.  The
// descriptor/status portion is a static projection of the source registry;
// the token and validity fields are filled and published by serial prewarm.
typedef struct MachineX64PreparedExactOpcode MachineX64PreparedExactOpcode;

// Register-indexed exact forms have a closed 16x16 physical population.
// Serial prewarm asks the metadata authority for every member once and
// publishes the result as a dense 16-byte record: fifteen architectural bytes
// plus its count.  Zero-displacement base-register memory joins the same closed
// population.  Forms with one trailing immediate or forced RBP displacement
// are encoded twice per member so publication also proves that only that patch
// field changes.  The worker then consumes the dense table and writes the
// dynamic field without reinterpreting the metadata form.  Tables whose
// complete population fits three bytes also place the count in byte three.
// Their first dword is a fixed scalar store and the compact encoding unit
// consumed by future homogeneous SIMD batches; longer tables keep the same
// uniform stride.
#define MACHINE_X64_GPR_ENCODING_TABLE_CAPACITY 64u
BUSTER_CT_CHECK(MACHINE_X64_GPR_ENCODING_TABLE_CAPACITY <= UINT8_MAX);
typedef struct MachineX64GprEncoding MachineX64GprEncoding;
typedef struct MachineX64GprEncodingTable MachineX64GprEncodingTable;
typedef struct MachineX64VariableMemoryEncodingTable MachineX64VariableMemoryEncodingTable;
struct MachineX64GprEncoding
{
    u8 bytes[15];
    u8 byte_count;
};
struct MachineX64GprEncodingTable
{
    MachineX64GprEncoding encodings[256];
    u8 operand_slots[2];
    u8 operand_count;
    u8 flags;
    u8 immediate_width;
    u8 memory_operand_slot;
};
#define MACHINE_X64_GPR_ENCODING_TABLE_COMPACT 0x1u
#define MACHINE_X64_GPR_ENCODING_TABLE_SELF_COPY_NOOP 0x2u
#define MACHINE_X64_GPR_ENCODING_TABLE_PATCH_IMMEDIATE 0x4u
#define MACHINE_X64_GPR_ENCODING_TABLE_MEMORY_BASE 0x8u
#define MACHINE_X64_GPR_ENCODING_TABLE_PATCH_DISPLACEMENT 0x10u
#define MACHINE_X64_GPR_ENCODING_TABLE_INCOMING_DISPLACEMENT 0x20u
#define MACHINE_X64_GPR_ENCODING_TABLE_IMMEDIATE_FROM_PAYLOAD 0x40u
BUSTER_CT_CHECK(sizeof(MachineX64GprEncoding) == 16);
BUSTER_GLOBAL_LOCAL MachineX64GprEncodingTable
    machine_x64_gpr_encoding_tables[MACHINE_X64_GPR_ENCODING_TABLE_CAPACITY];
BUSTER_GLOBAL_LOCAL u32 machine_x64_gpr_encoding_table_count;

// Variable base-register memory forms are another closed population.  Keep
// their zero, disp8, and disp32 encodings in separate homogeneous lanes so the
// worker classifies the displacement once and consumes a fixed-stride record.
#define MACHINE_X64_VARIABLE_MEMORY_TABLE_CAPACITY 16u
#define MACHINE_X64_MEMORY_DISPLACEMENT_CLASS_COUNT 3u
BUSTER_CT_CHECK(MACHINE_X64_VARIABLE_MEMORY_TABLE_CAPACITY <= UINT8_MAX);
struct MachineX64VariableMemoryEncodingTable
{
    MachineX64GprEncoding encodings[MACHINE_X64_MEMORY_DISPLACEMENT_CLASS_COUNT][256];
    u8 gpr_operand_slot;
    u8 memory_operand_slot;
};
BUSTER_GLOBAL_LOCAL MachineX64VariableMemoryEncodingTable
    machine_x64_variable_memory_encoding_tables[MACHINE_X64_VARIABLE_MEMORY_TABLE_CAPACITY];
BUSTER_GLOBAL_LOCAL u32 machine_x64_variable_memory_encoding_table_count;

struct MachineX64PreparedExactOpcode
{
    MachineX64ExactRecipe const* descriptor;
    MachineX64ExactSequence const* sequence;
    // Tokens are resolved beside each descriptor variant's feature policy
    // during serial prewarm and remain opaque to the machine layer.
    BusterX86MetadataMachineExactToken metadata_tokens[16];
    // One-based index into machine_x64_gpr_encoding_tables per form variant;
    // zero keeps the ordinary exact metadata transform.
    u8 gpr_encoding_tables[16];
    // One-based index into the displacement-class tables above.
    u8 variable_memory_encoding_tables[16];
    BusterX86MetadataMachineExactToken sequence_tokens[MACHINE_X64_EXACT_SEQUENCE_MAX_VARIANTS * MACHINE_X64_EXACT_SEQUENCE_MAX_STEPS];
    u8 exact_required;
    u8 sequence_required;
    u8 variant_count;
    // Direct single-variant GPR rows bypass the generic variant selector and
    // validity projection; zero retains the ordinary exact lane.
    u8 single_gpr_encoding_table;
    u16 variant_valid_mask;
    u8 plan_valid;
};

#define MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXACT_FORM(category, index) MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXACT_FORM_##category(index)
#define MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXACT_FORM_DIRECT(index) (&machine_x64_exact_recipe_table[(index)])
#define MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXACT_FORM_FAMILY(index) (&machine_x64_family_exact_recipe_table[(index)])
#define MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXACT_SEQUENCE(category, index) 0
#define MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_LEGACY_RAW(category, index) 0
#define MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXPANSION_POLICY(category, index) 0
#define MACHINE_X64_EXACT_OPCODE_REQUIRED_EXACT_FORM 1u
#define MACHINE_X64_EXACT_OPCODE_REQUIRED_EXACT_SEQUENCE 1u
#define MACHINE_X64_EXACT_OPCODE_REQUIRED_LEGACY_RAW 0u
#define MACHINE_X64_EXACT_OPCODE_REQUIRED_EXPANSION_POLICY 0u
#define MACHINE_X64_EXACT_OPCODE_SEQUENCE_EXACT_FORM 0u
#define MACHINE_X64_EXACT_OPCODE_SEQUENCE_EXACT_SEQUENCE 1u
#define MACHINE_X64_EXACT_OPCODE_SEQUENCE_LEGACY_RAW 0u
#define MACHINE_X64_EXACT_OPCODE_SEQUENCE_EXPANSION_POLICY 0u
#define MACHINE_X64_EXACT_OPCODE_MAP_ROW(opcode_value, category_value, index_value, status_value) \
    [opcode_value - MACHINE_X64_MOV_RI] = { \
        .descriptor = MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_##status_value(category_value, index_value), \
        .sequence_required = MACHINE_X64_EXACT_OPCODE_SEQUENCE_##status_value, \
        .exact_required = MACHINE_X64_EXACT_OPCODE_REQUIRED_##status_value, \
    },
BUSTER_GLOBAL_LOCAL MachineX64PreparedExactOpcode machine_x64_exact_opcode_map[MACHINE_X86_64_EMIT_REGISTRY_COUNT] = {
    MACHINE_X86_64_EMIT_REGISTRY(MACHINE_X64_EXACT_OPCODE_MAP_ROW)
};
#undef MACHINE_X64_EXACT_OPCODE_MAP_ROW
#undef MACHINE_X64_EXACT_OPCODE_REQUIRED_EXPANSION_POLICY
#undef MACHINE_X64_EXACT_OPCODE_REQUIRED_LEGACY_RAW
#undef MACHINE_X64_EXACT_OPCODE_REQUIRED_EXACT_FORM
#undef MACHINE_X64_EXACT_OPCODE_REQUIRED_EXACT_SEQUENCE
#undef MACHINE_X64_EXACT_OPCODE_SEQUENCE_EXPANSION_POLICY
#undef MACHINE_X64_EXACT_OPCODE_SEQUENCE_LEGACY_RAW
#undef MACHINE_X64_EXACT_OPCODE_SEQUENCE_EXACT_SEQUENCE
#undef MACHINE_X64_EXACT_OPCODE_SEQUENCE_EXACT_FORM
#undef MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXPANSION_POLICY
#undef MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_LEGACY_RAW
#undef MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXACT_FORM_FAMILY
#undef MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXACT_FORM_DIRECT
#undef MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXACT_FORM
#undef MACHINE_X64_EXACT_OPCODE_DESCRIPTOR_EXACT_SEQUENCE

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(machine_x64_exact_opcode_map) == MACHINE_X86_64_EMIT_REGISTRY_COUNT);
BUSTER_GLOBAL_LOCAL bool machine_x64_exact_opcode_map_ready;

// Keep the prewarm stages in separate functions.  The self-hosted C compiler
// accounts MIR capacity per function, and putting key collection, metadata
// preparation, and row publication in one body makes this otherwise bounded
// serial operation exceed that limit.  The key/plan helpers write caller-owned
// scratch storage, while the row helper stages the global map before the
// ready bit is published by the entry point.
BUSTER_GLOBAL_LOCAL void machine_x64_exact_collect_plan_keys(BusterX86MetadataFormKey* keys, bool* key_found)
{
    for (u32 recipe_index = 0; recipe_index < BUSTER_ARRAY_LENGTH(machine_x64_exact_recipe_table); recipe_index += 1)
    {
        MachineX64ExactRecipe const* recipe = machine_x64_exact_recipe_table + recipe_index;
        u32 variant_total = recipe->variant_count + 1u;
        for (u32 variant_index = 0; variant_index < variant_total; variant_index += 1)
        {
            MachineX64ExactRecipeVariant variant = machine_x64_exact_recipe_variant(recipe, variant_index);
            u8 plan_id = machine_x64_exact_plan_id_for_recipe_variant(MACHINE_EMIT_RECIPE_CATEGORY_DIRECT, (u16)recipe_index, variant_index);
            if (plan_id >= MACHINE_X64_EXACT_PLAN_COUNT || key_found[plan_id]) continue;
            keys[plan_id] = variant.key;
            key_found[plan_id] = true;
        }
    }
    for (u32 recipe_index = 0; recipe_index < BUSTER_ARRAY_LENGTH(machine_x64_family_exact_recipe_table); recipe_index += 1)
    {
        MachineX64ExactRecipe const* recipe = machine_x64_family_exact_recipe_table + recipe_index;
        if (!recipe->recipe || !recipe->key.stable_hash) continue;
        u32 variant_total = recipe->variant_count + 1u;
        for (u32 variant_index = 0; variant_index < variant_total; variant_index += 1)
        {
            MachineX64ExactRecipeVariant variant = machine_x64_exact_recipe_variant(recipe, variant_index);
            u8 plan_id = machine_x64_exact_plan_id_for_recipe_variant(MACHINE_EMIT_RECIPE_CATEGORY_FAMILY, (u16)recipe_index, variant_index);
            if (plan_id >= MACHINE_X64_EXACT_PLAN_COUNT || key_found[plan_id]) continue;
            keys[plan_id] = variant.key;
            key_found[plan_id] = true;
        }
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_exact_prepare_plans(BusterX86MetadataFormKey const* keys, bool const* key_found,
                                                          BusterX86MetadataExactPlan* prepared, bool* plan_valid)
{
    for (u32 plan_id = 0; plan_id < MACHINE_X64_EXACT_PLAN_COUNT; plan_id += 1)
    {
        plan_valid[plan_id] = key_found[plan_id] &&
                              buster_x86_metadata_exact_plan_prepare(keys[plan_id], &prepared[plan_id]) &&
                              prepared[plan_id].form_id == keys[plan_id].form_id &&
                              prepared[plan_id].stable_hash == keys[plan_id].stable_hash;
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_exact_prepare_fcmp_alternate_tokens(void)
{
    // FCMP_SET's OR-parity repair uses two alternate BASE forms.  Prepare
    // their opaque metadata tokens on the serial prewarm lane; workers only
    // select among these already validated tokens and never call metadata.
    machine_x64_fcmp_alternate_tokens_valid = false;
    BusterX86MetadataExactPlan setp_plan = {0};
    BusterX86MetadataExactPlan or_plan = {0};
    bool setp_prepared = buster_x86_metadata_exact_plan_prepare((BusterX86MetadataFormKey){10647u, UINT64_C(0x65dc8e342334f3cb)}, &setp_plan);
    bool or_prepared = buster_x86_metadata_exact_plan_prepare((BusterX86MetadataFormKey){9631u, UINT64_C(0x89a3abb502bbc55a)}, &or_plan);
    machine_x64_fcmp_alternate_tokens_valid = setp_prepared && or_prepared &&
                                               buster_x86_metadata_machine_exact_token_for_plan(setp_plan, (BusterX86MetadataFeatureInput){0}, &machine_x64_fcmp_setp_token) &&
                                               buster_x86_metadata_machine_exact_token_for_plan(or_plan, (BusterX86MetadataFeatureInput){0}, &machine_x64_fcmp_or_token);
}

BUSTER_GLOBAL_LOCAL void machine_x64_exact_prepare_sequence_entry(MachineX64PreparedExactOpcode* entry,
                                                                    MachineX64EmitRegistryEntry const* registry_entry)
{
    MachineEmitRecipeId sequence_recipe = registry_entry ? registry_entry->recipe : 0;
    u16 sequence_index = machine_emit_recipe_index(sequence_recipe);
    MachineX64ExactSequence const* sequence = sequence_index < BUSTER_ARRAY_LENGTH(machine_x64_exact_sequence_table)
                                                   ? machine_x64_exact_sequence_table[sequence_index].variants ? machine_x64_exact_sequence_table + sequence_index : 0
                                                   : 0;
    entry->sequence = sequence;
    bool sequence_valid = registry_entry && registry_entry->producer_status == MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_SEQUENCE &&
                          sequence && sequence->recipe == sequence_recipe && sequence->variant_count > 0 &&
                          sequence->variant_count <= MACHINE_X64_EXACT_SEQUENCE_MAX_VARIANTS;
    entry->variant_count = (u8)(sequence_valid ? sequence->variant_count : 0);
    entry->variant_valid_mask = 0;
    for (u32 variant_index = 0; sequence_valid && variant_index < sequence->variant_count; variant_index += 1)
    {
        MachineX64ExactSequenceVariant const* variant = sequence->variants + variant_index;
        if (!variant->steps || variant->step_count == 0 || variant->step_count > MACHINE_X64_EXACT_SEQUENCE_MAX_STEPS)
        {
            sequence_valid = false;
            break;
        }
        for (u32 step_index = 0; step_index < variant->step_count; step_index += 1)
        {
            MachineX64ExactSequenceStep const* step = variant->steps + step_index;
            BusterX86MetadataExactPlan step_plan = {0};
            BusterX86MetadataMachineExactToken* token = entry->sequence_tokens +
                variant_index * MACHINE_X64_EXACT_SEQUENCE_MAX_STEPS + step_index;
            bool prepared_step = buster_x86_metadata_exact_plan_prepare(step->key, &step_plan);
            bool identity_step = prepared_step && step_plan.form_id == step->key.form_id && step_plan.stable_hash == step->key.stable_hash;
            bool token_step = identity_step && buster_x86_metadata_machine_exact_token_for_plan(
                                                        step_plan,
                                                        (BusterX86MetadataFeatureInput){.names = step->features, .count = step->feature_count}, token);
            if (!token_step)
            {
                sequence_valid = false;
                break;
            }
        }
        if (sequence_valid) entry->variant_valid_mask |= (u16)(1u << variant_index);
    }
    entry->plan_valid = sequence_valid && entry->variant_valid_mask == (u16)((1u << entry->variant_count) - 1u);
    if (!entry->plan_valid)
    {
        entry->sequence = 0;
        entry->variant_count = 0;
        entry->variant_valid_mask = 0;
        for (u32 token_index = 0; token_index < BUSTER_ARRAY_LENGTH(entry->sequence_tokens); token_index += 1)
        {
            entry->sequence_tokens[token_index] = (BusterX86MetadataMachineExactToken){0};
        }
    }
}

BUSTER_GLOBAL_LOCAL bool machine_x64_exact_gpr_variant_slots(MachineX64ExactRecipeVariant const* variant,
                                                              u8 operand_slots[2], u8* operand_count,
                                                              u8* immediate_operand_index, u8* displacement_operand_index,
                                                              bool* has_memory_base)
{
    if (!variant || variant->operand_count == 0 ||
        (variant->flags & ~(MACHINE_X64_EXACT_RECIPE_FLAG_SELF_COPY_NOOP |
                            MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32)))
        return false;
    u8 count = 0;
    *immediate_operand_index = UINT8_MAX;
    *displacement_operand_index = UINT8_MAX;
    *has_memory_base = false;
    for (u32 operand_index = 0; operand_index < variant->operand_count; operand_index += 1)
    {
        MachineX64ExactOperandProjection kind = (MachineX64ExactOperandProjection)variant->operand_kinds[operand_index];
        if (kind == MACHINE_X64_EXACT_OPERAND_GPR)
        {
            if (count >= 2 || variant->operand_slots[operand_index] >= 4) return false;
            operand_slots[count++] = variant->operand_slots[operand_index];
        }
        else if (kind == MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO)
        {
            if (count >= 2 || variant->operand_slots[operand_index] >= 4) return false;
            operand_slots[count++] = variant->operand_slots[operand_index];
            *has_memory_base = true;
        }
        else if (kind == MACHINE_X64_EXACT_OPERAND_RBP_MEMORY_PAYLOAD ||
                 kind == MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD)
        {
            if (*displacement_operand_index != UINT8_MAX ||
                !(variant->flags & MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32) ||
                variant->operand_slots[operand_index] >= 4)
                return false;
            *displacement_operand_index = (u8)operand_index;
        }
        else if (kind == MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD)
        {
            if (*immediate_operand_index != UINT8_MAX) return false;
            *immediate_operand_index = (u8)operand_index;
        }
        else if (kind != MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX &&
                 kind != MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RDX && kind != MACHINE_X64_EXACT_OPERAND_FIXED_RSP)
        {
            return false;
        }
        u16 width = variant->operand_widths[operand_index];
        if (width != 8 && width != 16 && width != 32 && width != 64) return false;
    }
    if (!count && *immediate_operand_index == UINT8_MAX && *displacement_operand_index == UINT8_MAX) return false;
    *operand_count = count;
    return true;
}

BUSTER_GLOBAL_LOCAL u8 machine_x64_exact_prepare_gpr_encoding_table(
    MachineX64ExactRecipeVariant const* variant, BusterX86MetadataMachineExactToken token)
{
    if (machine_x64_gpr_encoding_table_count >= MACHINE_X64_GPR_ENCODING_TABLE_CAPACITY) return 0;
    u8 operand_slots[2] = {0};
    u8 register_operand_count = 0;
    u8 immediate_operand_index = UINT8_MAX;
    u8 displacement_operand_index = UINT8_MAX;
    bool has_memory_base = false;
    if (!machine_x64_exact_gpr_variant_slots(variant, operand_slots, &register_operand_count,
                                              &immediate_operand_index, &displacement_operand_index,
                                              &has_memory_base))
        return 0;
    if (immediate_operand_index != UINT8_MAX && displacement_operand_index != UINT8_MAX) return 0;
    u8 immediate_width = immediate_operand_index == UINT8_MAX ? 0 : (u8)(variant->operand_widths[immediate_operand_index] / 8u);
    if (immediate_operand_index != UINT8_MAX &&
        (immediate_width == 0 || immediate_width > sizeof(u64) || variant->operand_widths[immediate_operand_index] % 8u))
        return 0;

    MachineX64GprEncodingTable* table = machine_x64_gpr_encoding_tables + machine_x64_gpr_encoding_table_count;
    bool compact = true;
    for (u32 register_key = 0; register_key < BUSTER_ARRAY_LENGTH(table->encodings); register_key += 1)
    {
        u8 register_values[2] = {(u8)(register_key & 15u), (u8)(register_key >> 4)};
        u32 register_value_index = 0;
        BusterX86MetadataPhysicalOperand operands[4];
        for (u32 operand_index = 0; operand_index < variant->operand_count; operand_index += 1)
        {
            u32 reg = 0;
            switch ((MachineX64ExactOperandProjection)variant->operand_kinds[operand_index])
            {
            case MACHINE_X64_EXACT_OPERAND_GPR: reg = register_values[register_value_index++]; break;
            case MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO:
            {
                reg = register_values[register_value_index++];
                u16 width = variant->operand_widths[operand_index];
                operands[operand_index] = (BusterX86MetadataPhysicalOperand){
                    .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
                    .width = width,
                    .memory = {
                        .base = {.index = (u16)reg, .width = 64,
                                 .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
                        .address_size = 64,
                        .scale = 1,
                        .has_base = true,
                    },
                };
                continue;
            }
            case MACHINE_X64_EXACT_OPERAND_RBP_MEMORY_PAYLOAD:
            case MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD:
            {
                u16 width = variant->operand_widths[operand_index];
                operands[operand_index] = (BusterX86MetadataPhysicalOperand){
                    .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
                    .width = width,
                    .memory = {
                        .base = {.index = MACHINE_X64_RBP, .width = 64,
                                 .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
                        .address_size = 64,
                        .scale = 1,
                        .has_base = true,
                        .has_displacement = true,
                    },
                };
                continue;
            }
            case MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX: reg = MACHINE_X64_RAX; break;
            case MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RDX: reg = MACHINE_X64_RDX; break;
            case MACHINE_X64_EXACT_OPERAND_FIXED_RSP: reg = MACHINE_X64_RSP; break;
            case MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD:
            {
                u16 width = variant->operand_widths[operand_index];
                bool unsigned_immediate = variant->key.form_id == MACHINE_X64_MOV_IMMEDIATE_EXACT_FORM_ID;
                operands[operand_index] = (BusterX86MetadataPhysicalOperand){
                    .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
                    .width = width,
                    .has_value = !unsigned_immediate,
                    .has_unsigned_value = unsigned_immediate,
                    .unsigned_value = 0,
                };
                continue;
            }
            default: return 0;
            }
            u16 width = variant->operand_widths[operand_index];
            operands[operand_index] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                .width = width,
                .reg = {.index = (u16)reg, .width = width, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
            };
        }
        MachineX64GprEncoding* encoding = table->encodings + register_key;
        BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_exact_machine(token, (BusterX86MetadataMachineExactQuery){
            .operands = operands,
            .operand_count = variant->operand_count,
            .output = encoding->bytes,
            .output_capacity = BUSTER_ARRAY_LENGTH(encoding->bytes),
            .force_disp32 = displacement_operand_index != UINT8_MAX,
        });
        if (emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || emitted.relocation_count != 0 ||
            emitted.byte_count > BUSTER_ARRAY_LENGTH(encoding->bytes))
            return 0;
        encoding->byte_count = (u8)emitted.byte_count;
        if (immediate_operand_index != UINT8_MAX)
        {
            if (emitted.byte_count < immediate_width) return 0;
            u64 probe_value = immediate_width == 1 ? UINT64_C(0x5a) :
                              immediate_width == 2 ? UINT64_C(0x5aa5) :
                              immediate_width == 4 ? UINT64_C(0x5aa55aa5) : UINT64_C(0x5aa55aa55aa55aa5);
            BusterX86MetadataPhysicalOperand* immediate = operands + immediate_operand_index;
            if (immediate->has_unsigned_value) immediate->unsigned_value = probe_value;
            else immediate->value = (s64)probe_value;
            u8 probe_bytes[15];
            BusterX86MetadataEmitResult probe = buster_x86_metadata_emit_exact_machine(token, (BusterX86MetadataMachineExactQuery){
                .operands = operands,
                .operand_count = variant->operand_count,
                .output = probe_bytes,
                .output_capacity = BUSTER_ARRAY_LENGTH(probe_bytes),
            });
            if (probe.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || probe.relocation_count != 0 ||
                probe.byte_count != emitted.byte_count)
                return 0;
            u32 immediate_offset = emitted.byte_count - immediate_width;
            for (u32 byte_index = 0; byte_index < emitted.byte_count; byte_index += 1)
            {
                u8 expected = byte_index >= immediate_offset
                                  ? (u8)(probe_value >> ((byte_index - immediate_offset) * 8u))
                                  : encoding->bytes[byte_index];
                if (probe_bytes[byte_index] != expected) return 0;
            }
        }
        if (displacement_operand_index != UINT8_MAX)
        {
            if (emitted.byte_count < sizeof(u32)) return 0;
            u32 probe_value = UINT32_C(0x5aa55aa5);
            operands[displacement_operand_index].memory.displacement = (s32)probe_value;
            u8 probe_bytes[15];
            BusterX86MetadataEmitResult probe = buster_x86_metadata_emit_exact_machine(token, (BusterX86MetadataMachineExactQuery){
                .operands = operands,
                .operand_count = variant->operand_count,
                .output = probe_bytes,
                .output_capacity = BUSTER_ARRAY_LENGTH(probe_bytes),
                .force_disp32 = true,
            });
            if (probe.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || probe.relocation_count != 0 ||
                probe.byte_count != emitted.byte_count)
                return 0;
            u32 displacement_offset = emitted.byte_count - (u32)sizeof(u32);
            for (u32 byte_index = 0; byte_index < emitted.byte_count; byte_index += 1)
            {
                u8 expected = byte_index >= displacement_offset
                                  ? (u8)(probe_value >> ((byte_index - displacement_offset) * 8u))
                                  : encoding->bytes[byte_index];
                if (probe_bytes[byte_index] != expected) return 0;
            }
        }
        compact &= emitted.byte_count <= 3;
    }
    if (compact)
    {
        for (u32 register_key = 0; register_key < BUSTER_ARRAY_LENGTH(table->encodings); register_key += 1)
        {
            table->encodings[register_key].bytes[3] = table->encodings[register_key].byte_count;
        }
    }
    table->operand_slots[0] = operand_slots[0];
    table->operand_slots[1] = operand_slots[1];
    table->operand_count = register_operand_count;
    table->immediate_width = immediate_width;
    table->memory_operand_slot = displacement_operand_index == UINT8_MAX ? 0 : variant->operand_slots[displacement_operand_index];
    table->flags = (compact ? MACHINE_X64_GPR_ENCODING_TABLE_COMPACT : 0u) |
                   ((register_operand_count == 2 && (variant->flags & MACHINE_X64_EXACT_RECIPE_FLAG_SELF_COPY_NOOP))
                        ? MACHINE_X64_GPR_ENCODING_TABLE_SELF_COPY_NOOP
                        : 0u) |
                   (immediate_operand_index != UINT8_MAX ? MACHINE_X64_GPR_ENCODING_TABLE_PATCH_IMMEDIATE : 0u) |
                   (has_memory_base ? MACHINE_X64_GPR_ENCODING_TABLE_MEMORY_BASE : 0u) |
                   (displacement_operand_index != UINT8_MAX ? MACHINE_X64_GPR_ENCODING_TABLE_PATCH_DISPLACEMENT : 0u) |
                   ((displacement_operand_index != UINT8_MAX &&
                     variant->operand_kinds[displacement_operand_index] == MACHINE_X64_EXACT_OPERAND_RBP_MEMORY_PAYLOAD)
                        ? MACHINE_X64_GPR_ENCODING_TABLE_INCOMING_DISPLACEMENT
                        : 0u) |
                   ((immediate_operand_index != UINT8_MAX && variant->key.form_id == MACHINE_X64_SUB_RSP_EXACT_FORM_ID)
                        ? MACHINE_X64_GPR_ENCODING_TABLE_IMMEDIATE_FROM_PAYLOAD
                        : 0u);
    machine_x64_gpr_encoding_table_count += 1;
    return (u8)machine_x64_gpr_encoding_table_count;
}

BUSTER_GLOBAL_LOCAL u8 machine_x64_exact_prepare_variable_memory_encoding_table(
    MachineX64ExactRecipeVariant const* variant, BusterX86MetadataMachineExactToken token)
{
    if (!variant || variant->operand_count != 2 || variant->flags ||
        machine_x64_variable_memory_encoding_table_count >= MACHINE_X64_VARIABLE_MEMORY_TABLE_CAPACITY)
        return 0;

    u32 gpr_operand_index = UINT32_MAX;
    u32 memory_operand_index = UINT32_MAX;
    for (u32 operand_index = 0; operand_index < variant->operand_count; operand_index += 1)
    {
        MachineX64ExactOperandProjection kind = (MachineX64ExactOperandProjection)variant->operand_kinds[operand_index];
        if (kind == MACHINE_X64_EXACT_OPERAND_GPR && gpr_operand_index == UINT32_MAX)
        {
            gpr_operand_index = operand_index;
        }
        else if ((kind == MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO ||
                  kind == MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD) &&
                 memory_operand_index == UINT32_MAX)
        {
            memory_operand_index = operand_index;
        }
        else
        {
            return 0;
        }
        u16 width = variant->operand_widths[operand_index];
        if ((width != 8 && width != 16 && width != 32 && width != 64) ||
            variant->operand_slots[operand_index] >= 4)
            return 0;
    }
    if (gpr_operand_index == UINT32_MAX || memory_operand_index == UINT32_MAX) return 0;

    MachineX64VariableMemoryEncodingTable* table =
        machine_x64_variable_memory_encoding_tables + machine_x64_variable_memory_encoding_table_count;
    s32 const class_displacements[MACHINE_X64_MEMORY_DISPLACEMENT_CLASS_COUNT] = {
        0,
        INT32_C(0x5a),
        INT32_C(0x12345678),
    };
    s32 const probe_displacements[MACHINE_X64_MEMORY_DISPLACEMENT_CLASS_COUNT] = {
        0,
        -INT32_C(0x5b),
        INT32_C(0x5aa55aa5),
    };
    u8 const patch_widths[MACHINE_X64_MEMORY_DISPLACEMENT_CLASS_COUNT] = {0, 1, 4};

    for (u32 displacement_class = 0; displacement_class < MACHINE_X64_MEMORY_DISPLACEMENT_CLASS_COUNT; displacement_class += 1)
    {
        for (u32 register_key = 0; register_key < 256; register_key += 1)
        {
            u32 data_register = register_key & 15u;
            u32 base_register = register_key >> 4;
            BusterX86MetadataPhysicalOperand operands[2];
            operands[gpr_operand_index] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                .width = variant->operand_widths[gpr_operand_index],
                .reg = {
                    .index = (u16)data_register,
                    .width = variant->operand_widths[gpr_operand_index],
                    .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                },
            };
            operands[memory_operand_index] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
                .width = variant->operand_widths[memory_operand_index],
                .memory = {
                    .base = {
                        .index = (u16)base_register,
                        .width = 64,
                        .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                    },
                    .displacement = class_displacements[displacement_class],
                    .address_size = 64,
                    .scale = 1,
                    .has_base = true,
                    .has_displacement = displacement_class != 0,
                },
            };

            MachineX64GprEncoding* encoding = table->encodings[displacement_class] + register_key;
            BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_exact_machine(token, (BusterX86MetadataMachineExactQuery){
                .operands = operands,
                .operand_count = variant->operand_count,
                .output = encoding->bytes,
                .output_capacity = BUSTER_ARRAY_LENGTH(encoding->bytes),
            });
            if (emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || emitted.relocation_count != 0 ||
                emitted.byte_count > BUSTER_ARRAY_LENGTH(encoding->bytes))
                return 0;
            encoding->byte_count = (u8)emitted.byte_count;

            u32 patch_width = patch_widths[displacement_class];
            if (patch_width)
            {
                if (emitted.byte_count < patch_width) return 0;
                operands[memory_operand_index].memory.displacement = probe_displacements[displacement_class];
                u8 probe_bytes[15];
                BusterX86MetadataEmitResult probe = buster_x86_metadata_emit_exact_machine(token, (BusterX86MetadataMachineExactQuery){
                    .operands = operands,
                    .operand_count = variant->operand_count,
                    .output = probe_bytes,
                    .output_capacity = BUSTER_ARRAY_LENGTH(probe_bytes),
                });
                if (probe.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || probe.relocation_count != 0 ||
                    probe.byte_count != emitted.byte_count)
                    return 0;
                u32 patch_offset = emitted.byte_count - patch_width;
                u32 probe_value = (u32)probe_displacements[displacement_class];
                for (u32 byte_index = 0; byte_index < emitted.byte_count; byte_index += 1)
                {
                    u8 expected = byte_index >= patch_offset
                                      ? (u8)(probe_value >> ((byte_index - patch_offset) * 8u))
                                      : encoding->bytes[byte_index];
                    if (probe_bytes[byte_index] != expected) return 0;
                }
            }
        }
    }

    table->gpr_operand_slot = variant->operand_slots[gpr_operand_index];
    table->memory_operand_slot = variant->operand_slots[memory_operand_index];
    machine_x64_variable_memory_encoding_table_count += 1;
    return (u8)machine_x64_variable_memory_encoding_table_count;
}

BUSTER_GLOBAL_LOCAL void machine_x64_exact_prepare_form_entry(MachineX64PreparedExactOpcode* entry,
                                                               MachineX64EmitRegistryEntry const* registry_entry,
                                                               BusterX86MetadataExactPlan const* prepared, bool const* plan_valid)
{
    MachineX64ExactRecipe const* descriptor = entry->descriptor;
    MachineEmitRecipeCategory category = descriptor ? machine_emit_recipe_category(descriptor->recipe) : MACHINE_EMIT_RECIPE_CATEGORY_COUNT;
    bool descriptor_valid = registry_entry && registry_entry->producer_status == MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_FORM &&
                            descriptor && descriptor->key.stable_hash != 0 && descriptor->recipe == registry_entry->recipe &&
                            (category == MACHINE_EMIT_RECIPE_CATEGORY_DIRECT || category == MACHINE_EMIT_RECIPE_CATEGORY_FAMILY);
    u16 recipe_index = descriptor_valid ? machine_emit_recipe_index(descriptor->recipe) : 0;
    u32 variant_total = descriptor_valid ? descriptor->variant_count + 1u : 0;
    descriptor_valid &= variant_total > 0 && variant_total <= BUSTER_ARRAY_LENGTH(entry->metadata_tokens);
    entry->variant_count = (u8)(descriptor_valid ? variant_total : 0);
    entry->variant_valid_mask = 0;
    for (u32 variant_index = 0; descriptor_valid && variant_index < variant_total; variant_index += 1)
    {
        MachineX64ExactRecipeVariant variant = machine_x64_exact_recipe_variant(descriptor, variant_index);
        u8 plan_id = machine_x64_exact_plan_id_for_recipe_variant(category, recipe_index, variant_index);
        bool variant_valid = plan_id < MACHINE_X64_EXACT_PLAN_COUNT && plan_valid[plan_id] &&
                             prepared[plan_id].form_id == variant.key.form_id && prepared[plan_id].stable_hash == variant.key.stable_hash;
        if (variant_valid)
        {
            variant_valid = buster_x86_metadata_machine_exact_token_for_plan(
                prepared[plan_id],
                (BusterX86MetadataFeatureInput){.names = variant.features, .count = variant.feature_count},
                &entry->metadata_tokens[variant_index]);
        }
        entry->gpr_encoding_tables[variant_index] = variant_valid
            ? machine_x64_exact_prepare_gpr_encoding_table(&variant, entry->metadata_tokens[variant_index])
            : 0;
        entry->variable_memory_encoding_tables[variant_index] = variant_valid
            ? machine_x64_exact_prepare_variable_memory_encoding_table(&variant, entry->metadata_tokens[variant_index])
            : 0;
        if (variant_valid) entry->variant_valid_mask |= (u16)(1u << variant_index);
        else descriptor_valid = false;
    }
    if (descriptor_valid && entry->variant_valid_mask == (u16)((1u << variant_total) - 1u))
    {
        entry->descriptor = descriptor;
        entry->single_gpr_encoding_table = variant_total == 1 ? entry->gpr_encoding_tables[0] : 0;
        entry->plan_valid = true;
    }
    else
    {
        // Retain exact_required so an invalid exact row is counted and
        // rejected by the worker lane instead of using the old switch as an
        // accidental fallback.
        entry->descriptor = 0;
        for (u32 token_index = 0; token_index < BUSTER_ARRAY_LENGTH(entry->metadata_tokens); token_index += 1)
        {
            entry->metadata_tokens[token_index] = (BusterX86MetadataMachineExactToken){0};
        }
        entry->variant_count = 0;
        entry->single_gpr_encoding_table = 0;
        for (u32 table_index = 0; table_index < BUSTER_ARRAY_LENGTH(entry->variable_memory_encoding_tables); table_index += 1)
        {
            entry->variable_memory_encoding_tables[table_index] = 0;
        }
        entry->variant_valid_mask = 0;
        entry->plan_valid = false;
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_exact_prepare_opcode_map(BusterX86MetadataExactPlan const* prepared, bool const* plan_valid)
{
    for (u32 ordinal = 0; ordinal < MACHINE_X86_64_EMIT_REGISTRY_COUNT; ordinal += 1)
    {
        // The dense map is only observed after the ready bit is published, so
        // its static rows are safe serial staging storage.  Mutating each row
        // in place avoids a large aggregate scratch array (and its generated
        // zeroing/copying MIR) in the self-hosted prewarm function.
        MachineX64PreparedExactOpcode* entry = machine_x64_exact_opcode_map + ordinal;
        if (entry->exact_required)
        {
            MachineX64EmitRegistryEntry const* registry_entry = machine_x86_64_emit_registry_entry(ordinal);
            if (entry->sequence_required)
            {
                machine_x64_exact_prepare_sequence_entry(entry, registry_entry);
            }
            else
            {
                machine_x64_exact_prepare_form_entry(entry, registry_entry, prepared, plan_valid);
            }
        }
    }
}

// Prepare the immutable metadata plans once on the serial prewarm thread.
// Width variants and projection variants deliberately share a plan identity;
// workers only read the published value table after the ready bit is set.  The
// opcode map is published even when one key is stale: its exact-required bits
// remain set, while the affected descriptor/plan entries stay invalid so the
// worker lane fails closed rather than falling through to handwritten bytes.
void machine_x86_64_exact_prewarm(void)
{
    if (machine_x64_exact_opcode_map_ready) return;
    BUSTER_CHECK_SERIAL_INITIALIZATION();

    BusterX86MetadataFormKey keys[MACHINE_X64_EXACT_PLAN_COUNT] = {0};
    bool key_found[MACHINE_X64_EXACT_PLAN_COUNT] = {0};
    BusterX86MetadataExactPlan prepared[MACHINE_X64_EXACT_PLAN_COUNT] = {0};
    bool plan_valid[MACHINE_X64_EXACT_PLAN_COUNT] = {0};
    machine_x64_gpr_encoding_table_count = 0;
    machine_x64_variable_memory_encoding_table_count = 0;
    machine_x64_exact_collect_plan_keys(keys, key_found);
    machine_x64_exact_prepare_plans(keys, key_found, prepared, plan_valid);
    machine_x64_exact_prepare_fcmp_alternate_tokens();
    machine_x64_exact_prepare_opcode_map(prepared, plan_valid);
    machine_x64_metadata_shape_cache_prewarm();

    // Every row was staged serially above.  The ready bit is written last;
    // codegen_prewarm() completes before any worker lane can observe globals.
    machine_x64_exact_opcode_map_ready = true;
}

#if BUSTER_INCLUDE_TESTS
MachineX64ExactMapAudit machine_x86_64_exact_map_audit(void)
{
    machine_x86_64_exact_prewarm();
    MachineX64ExactMapAudit result = {.valid = machine_x64_exact_opcode_map_ready};
    result.registry_rows = MACHINE_X86_64_EMIT_REGISTRY_COUNT;
    for (u32 ordinal = 0; ordinal < result.registry_rows; ordinal += 1)
    {
        MachineX64PreparedExactOpcode const* entry = machine_x64_exact_opcode_map + ordinal;
        MachineX64EmitRegistryEntry const* registry_entry = machine_x86_64_emit_registry_entry(ordinal);
        bool row_valid = registry_entry != 0;
        if (!row_valid)
        {
            result.valid = false;
            continue;
        }
        MachineX64EmitProducerStatus status = (MachineX64EmitProducerStatus)registry_entry->producer_status;
        bool is_exact_form = status == MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_FORM;
        bool is_exact_sequence = status == MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_SEQUENCE;
        bool is_expansion = status == MACHINE_X64_EMIT_PRODUCER_STATUS_EXPANSION_POLICY;
        if (is_exact_form || is_exact_sequence)
        {
            result.exact_rows += 1;
            bool variants_in_range = entry->variant_count > 0 && entry->variant_count <= 16;
            u16 expected_mask = variants_in_range ? (u16)((1u << entry->variant_count) - 1u) : 0;
            bool variants_valid = variants_in_range && entry->variant_valid_mask == expected_mask;
            bool exact_row_valid = entry->exact_required && entry->plan_valid && variants_valid;
            if (is_exact_sequence)
            {
                result.sequence_rows += 1;
                bool sequence_valid = exact_row_valid && entry->sequence_required && entry->sequence && !entry->descriptor &&
                                       entry->sequence->variant_count == entry->variant_count;
                if (sequence_valid) result.sequence_variant_valid_rows += 1;
                exact_row_valid = sequence_valid;
            }
            else
            {
                exact_row_valid = exact_row_valid && !entry->sequence_required && entry->descriptor && !entry->sequence;
            }
            if (exact_row_valid) result.exact_plan_valid_rows += 1;
            result.valid &= exact_row_valid;
        }
        else if (is_expansion)
        {
            result.expansion_rows += 1;
            bool nonexact = !entry->exact_required && !entry->sequence_required && !entry->plan_valid && !entry->descriptor &&
                            !entry->sequence && entry->variant_count == 0 && entry->variant_valid_mask == 0;
            if (nonexact) result.expansion_nonexact_rows += 1;
            result.valid &= nonexact;
        }
        else
        {
            result.valid = false;
        }
    }
    result.valid &= result.exact_rows == MACHINE_X86_64_EMIT_REGISTRY_EXACT_COUNT;
    result.valid &= result.exact_plan_valid_rows == result.exact_rows;
    result.valid &= result.sequence_variant_valid_rows == result.sequence_rows;
    result.valid &= result.expansion_rows == MACHINE_X86_64_EMIT_REGISTRY_EXPANSION_COUNT;
    result.valid &= result.expansion_nonexact_rows == result.expansion_rows;
    result.dense_encoding_tables = machine_x64_gpr_encoding_table_count;
    result.variable_memory_encoding_tables = machine_x64_variable_memory_encoding_table_count;
    for (u32 table_index = 0; table_index < machine_x64_gpr_encoding_table_count; table_index += 1)
    {
        result.immediate_patch_tables +=
            (machine_x64_gpr_encoding_tables[table_index].flags & MACHINE_X64_GPR_ENCODING_TABLE_PATCH_IMMEDIATE) != 0;
        result.memory_base_tables +=
            (machine_x64_gpr_encoding_tables[table_index].flags & MACHINE_X64_GPR_ENCODING_TABLE_MEMORY_BASE) != 0;
        result.displacement_patch_tables +=
            (machine_x64_gpr_encoding_tables[table_index].flags & MACHINE_X64_GPR_ENCODING_TABLE_PATCH_DISPLACEMENT) != 0;
    }
    return result;
}
#endif


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

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_exact_unsigned_immediate_operand(u64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
        .width = width,
        .unsigned_value = value,
        .has_unsigned_value = true,
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

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_exact_memory_operand(u32 base, u16 width, s64 displacement, bool force_displacement)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = width,
        .memory = {
            .base = {
                .index = (u16)base,
                .width = 64,
                .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
            },
            .displacement = displacement,
            .address_size = 64,
            .scale = 1,
            .has_base = true,
            .has_displacement = force_displacement || displacement != 0,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_exact_rbp_memory_operand(s64 displacement, u16 width, bool force_displacement)
{
    return machine_x64_exact_memory_operand(MACHINE_X64_RBP, width, displacement, force_displacement);
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_exact_zmm_operand(u32 reg, u16 width);

BUSTER_GLOBAL_LOCAL u8 machine_x64_metadata_shape_immediate_class(BusterX86MetadataPhysicalOperand operand)
{
    u8 result;
    if (operand.has_unsigned_value)
    {
        result = 5;
    }
    else if (!operand.has_value)
    {
        result = 0;
    }
    else if (operand.value >= INT8_MIN && operand.value <= INT8_MAX)
    {
        result = operand.value < 0 ? 2 : 1;
    }
    else if (operand.value >= INT32_MIN && operand.value <= INT32_MAX)
    {
        result = operand.value < 0 ? 4 : 3;
    }
    else
    {
        result = operand.value < 0 ? 6 : 7;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u8 machine_x64_metadata_shape_displacement_class(BusterX86MetadataPhysicalOperand operand)
{
    u8 result;
    if (operand.kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY || !operand.memory.has_displacement)
    {
        result = 0;
    }
    else if (operand.memory.displacement >= INT8_MIN && operand.memory.displacement <= INT8_MAX)
    {
        result = 1;
    }
    else if (operand.memory.displacement >= INT32_MIN && operand.memory.displacement <= INT32_MAX)
    {
        result = 2;
    }
    else
    {
        result = 3;
    }

    return result;
}

// The mnemonics the shape signature distinguishes, and the small dense id each
// one folds to. Every row that is not listed hashes as 0, which only widens the
// shape key's collision domain and never changes what is emitted.
typedef struct MachineX64ShapeMnemonic MachineX64ShapeMnemonic;
struct MachineX64ShapeMnemonic
{
    String8 name;
    u8 id;
};

// Rows are grouped by length, ascending, and the span table below indexes
// straight into each group -- this stands in for the length test that opened
// every arm of the per-character compare ladder this replaced. Keep both in
// step when editing: adding a row means bumping every later span bound.
BUSTER_GLOBAL_LOCAL MachineX64ShapeMnemonic const machine_x64_shape_mnemonics[] = {
    {S8_INITIALIZER("JB"), 7},          {S8_INITIALIZER("JZ"), 11},         {S8_INITIALIZER("OR"), 14},
    {S8_INITIALIZER("ADD"), 1},         {S8_INITIALIZER("AND"), 2},         {S8_INITIALIZER("CDQ"), 3},
    {S8_INITIALIZER("CMP"), 4},         {S8_INITIALIZER("CQO"), 5},         {S8_INITIALIZER("DIV"), 6},
    {S8_INITIALIZER("JMP"), 8},         {S8_INITIALIZER("JNS"), 9},         {S8_INITIALIZER("JNZ"), 10},
    {S8_INITIALIZER("LEA"), 12},        {S8_INITIALIZER("MOV"), 13},        {S8_INITIALIZER("POP"), 15},
    {S8_INITIALIZER("RET"), 16},        {S8_INITIALIZER("SHR"), 17},        {S8_INITIALIZER("SUB"), 18},
    {S8_INITIALIZER("UD2"), 19},        {S8_INITIALIZER("XOR"), 20},        {S8_INITIALIZER("CALL"), 21},
    {S8_INITIALIZER("IDIV"), 23},       {S8_INITIALIZER("JNBE"), 24},       {S8_INITIALIZER("MOVQ"), 25},
    {S8_INITIALIZER("PUSH"), 26},       {S8_INITIALIZER("TEST"), 33},       {S8_INITIALIZER("ADDSD"), 27},
    {S8_INITIALIZER("ADDSS"), 28},      {S8_INITIALIZER("MOVSD"), 29},      {S8_INITIALIZER("MOVZX"), 30},
    {S8_INITIALIZER("SUBSD"), 31},      {S8_INITIALIZER("SUBSS"), 32},      {S8_INITIALIZER("CMPXCHG"), 34},
    {S8_INITIALIZER("UCOMISD"), 35},    {S8_INITIALIZER("UCOMISS"), 36},    {S8_INITIALIZER("CVTSI2SD"), 37},
    {S8_INITIALIZER("CVTSI2SS"), 38},   {S8_INITIALIZER("VMOVDQU8"), 39},   {S8_INITIALIZER("CVTTSD2SI"), 40},
    {S8_INITIALIZER("CVTTSS2SI"), 41},  {S8_INITIALIZER("CMPXCHG16B"), 42}, {S8_INITIALIZER("VZEROUPPER"), 43},
};

#define MACHINE_X64_SHAPE_MNEMONIC_MIN_LENGTH 2u
#define MACHINE_X64_SHAPE_MNEMONIC_MAX_LENGTH 10u

// First row of each length, indexed by length - 2, with a closing bound. There
// are no six-character rows, so that span is empty.
BUSTER_GLOBAL_LOCAL u8 const machine_x64_shape_mnemonic_spans[] = {0, 3, 20, 26, 32, 32, 35, 38, 40, 42};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(machine_x64_shape_mnemonics) == 42);
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(machine_x64_shape_mnemonic_spans) ==
                MACHINE_X64_SHAPE_MNEMONIC_MAX_LENGTH - MACHINE_X64_SHAPE_MNEMONIC_MIN_LENGTH + 2u);

BUSTER_GLOBAL_LOCAL u8 machine_x64_metadata_shape_mnemonic_id(String8 mnemonic)
{
    if (mnemonic.length >= MACHINE_X64_SHAPE_MNEMONIC_MIN_LENGTH && mnemonic.length <= MACHINE_X64_SHAPE_MNEMONIC_MAX_LENGTH)
    {
        u32 span = (u32)mnemonic.length - MACHINE_X64_SHAPE_MNEMONIC_MIN_LENGTH;
        // The first byte rejects all but a couple of rows in the widest span, so
        // the full compare runs about once per lookup rather than once per row.
        for (u32 row = machine_x64_shape_mnemonic_spans[span]; row < machine_x64_shape_mnemonic_spans[span + 1]; row += 1)
        {
            if (machine_x64_shape_mnemonics[row].name.pointer[0] == mnemonic.pointer[0] &&
                string_equal(machine_x64_shape_mnemonics[row].name, mnemonic))
            {
                return machine_x64_shape_mnemonics[row].id;
            }
        }
    }

    return 0;
}

BUSTER_GLOBAL_LOCAL u8 machine_x64_metadata_shape_feature_id(BusterX86MetadataFeatureInput features)
{
    u8 result;
    if (!features.count)
    {
        result = 0;
    }
    else if (features.names == machine_x64_sse_features)
    {
        result = 1;
    }
    else if (features.names == machine_x64_sse2_features)
    {
        result = 2;
    }
    else if (features.names == machine_x64_avx_features)
    {
        result = 3;
    }
    else if (features.names == machine_x64_avx512_features)
    {
        result = 4;
    }
    else if (features.names == machine_x64_cx16_features)
    {
        result = 5;
    }
    else if (features.names == machine_x64_avx512f_features)
    {
        result = 6;
    }
    else
    {
        result = (u8)(0x80u | BUSTER_MIN(features.count, 0x7fu));
    }

    return result;
}

typedef struct MachineX64MetadataShapeHashes MachineX64MetadataShapeHashes;
struct MachineX64MetadataShapeHashes
{
    u64 signature;
    u64 guard;
};

BUSTER_GLOBAL_LOCAL MachineX64MetadataShapeHashes machine_x64_metadata_shape_hashes(
    String8 mnemonic, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataFeatureInput features, BusterX86MetadataPhysicalAttributes attributes)
{
    u64 common = machine_x64_metadata_shape_mnemonic_id(mnemonic) ^ ((u64)operand_count << 8) ^
                 ((u64)machine_x64_metadata_shape_feature_id(features) << 16);
    MachineX64MetadataShapeHashes hashes = {
        .signature = UINT64_C(1469598103934665603) ^ common,
        .guard = UINT64_C(0x6a09e667f3bcc909) ^ common,
    };
    u64 attribute_bits = attributes.decorator_flags | ((u64)attributes.apx_flags << 8) | ((u64)attributes.amx_flags << 16) |
                         ((u64)attributes.mask_register << 24) | ((u64)attributes.broadcast_elements << 32) |
                         ((u64)attributes.rounding_mode << 40) | ((u64)attributes.has_mask_register << 44) |
                         ((u64)attributes.zeroing << 45) | ((u64)attributes.sae << 46) | ((u64)attributes.no_flags << 47) |
                         ((u64)attributes.lock << 48) | ((u64)attributes.rep << 49) | ((u64)attributes.repne << 50) |
                         ((u64)attributes.implicit_segment << 51) | ((u64)attributes.branch_hint << 55) |
                         ((u64)attributes.notrack << 58) | ((u64)attributes.dfv << 59) | ((u64)attributes.has_dfv << 60);
    hashes.signature ^= attribute_bits;
    hashes.guard ^= attribute_bits;
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = operands[operand_index];
        u64 descriptor = (u64)operand.kind | ((u64)operand.width << 4);
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER)
        {
            descriptor |= (u64)operand.reg.physical_class << 14;
            descriptor |= (u64)operand.reg.width << 18;
        }
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            descriptor |= (u64)operand.memory.base.physical_class << 14;
            descriptor |= (u64)operand.memory.index.physical_class << 18;
            descriptor |= (u64)operand.memory.address_size << 22;
            descriptor |= (u64)operand.memory.scale << 30;
            descriptor |= (u64)operand.memory.segment << 34;
            descriptor |= (u64)operand.memory.has_base << 38;
            descriptor |= (u64)operand.memory.has_index << 39;
            descriptor |= (u64)operand.memory.rip_relative << 40;
            descriptor |= (u64)operand.memory.has_segment << 41;
            descriptor |= (u64)operand.memory.vsib << 42;
            descriptor |= (u64)machine_x64_metadata_shape_displacement_class(operand) << 43;
        }
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE)
        {
            descriptor |= (u64)machine_x64_metadata_shape_immediate_class(operand) << 14;
        }
        hashes.signature ^= descriptor + UINT64_C(0x9e3779b97f4a7c15) + (hashes.signature << 6) + (hashes.signature >> 2);
        hashes.signature *= UINT64_C(0x9e3779b97f4a7c15);
        hashes.guard ^= descriptor + UINT64_C(0x9e3779b97f4a7c15) + (hashes.guard << 6) + (hashes.guard >> 2);
        hashes.guard *= UINT64_C(0x9e3779b97f4a7c15);
    }
    return hashes;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_metadata_shape_cache_add(String8 mnemonic,
                                                               BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
                                                               BusterX86MetadataFeatureInput features,
                                                               BusterX86MetadataPhysicalAttributes attributes)
{
    BusterX86MetadataPhysicalQuery query = {
        .mnemonic = mnemonic,
        .operands = operands,
        .operand_count = operand_count,
        .features = features,
        .attributes = attributes,
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .include_privileged = false,
        .include_not64 = false,
        .include_implicit = false,
        .source_semantics = false,
    };
    BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(query);
    if (selected.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || selected.form_id == UINT32_MAX || !selected.stable_hash)
    {
        machine_x64_metadata_shape_cache_invalid_count += 1;
        return false;
    }
    BusterX86MetadataFormKey key = {.form_id = selected.form_id, .stable_hash = selected.stable_hash};
    BusterX86MetadataExactPlan plan = {0};
    BusterX86MetadataMachineExactToken token = {0};
    if (!buster_x86_metadata_exact_plan_prepare(key, &plan) ||
        !buster_x86_metadata_machine_exact_token_for_plan(plan, features, &token))
    {
        machine_x64_metadata_shape_cache_invalid_count += 1;
        return false;
    }
    MachineX64MetadataShapeHashes hashes = machine_x64_metadata_shape_hashes(mnemonic, operands, operand_count, features, attributes);
    for (u32 entry_index = 0; entry_index < machine_x64_metadata_shape_cache_count; entry_index += 1)
    {
        MachineX64MetadataShapeCacheEntry* entry = machine_x64_metadata_shape_cache + entry_index;
        if (entry->signature != hashes.signature) continue;
        if (entry->guard != hashes.guard || entry->token.slot_plus_one != token.slot_plus_one ||
            entry->token.policy_flags != token.policy_flags ||
            entry->token.integrity != token.integrity)
        {
            machine_x64_metadata_shape_cache_invalid_count += 1;
            return false;
        }
        return true;
    }
    if (machine_x64_metadata_shape_cache_count >= MACHINE_X64_METADATA_SHAPE_CACHE_CAPACITY)
    {
        machine_x64_metadata_shape_cache_invalid_count += 1;
        return false;
    }
    machine_x64_metadata_shape_cache[machine_x64_metadata_shape_cache_count++] = (MachineX64MetadataShapeCacheEntry){
        .signature = hashes.signature,
        .guard = hashes.guard,
        .token = token,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataMachineExactToken const* machine_x64_metadata_shape_cache_find(
    String8 mnemonic, BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
    BusterX86MetadataFeatureInput features, BusterX86MetadataPhysicalAttributes attributes)
{
    MachineX64MetadataShapeHashes hashes = machine_x64_metadata_shape_hashes(mnemonic, operands, operand_count, features, attributes);
    u32 slot = (u32)hashes.signature & (MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY - 1u);
    for (u32 probe = 0; probe < MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY; probe += 1)
    {
        u16 entry_plus_one = machine_x64_metadata_shape_cache_slots[slot];
        if (!entry_plus_one) return 0;
        MachineX64MetadataShapeCacheEntry const* entry = machine_x64_metadata_shape_cache + (entry_plus_one - 1u);
        if (entry->signature == hashes.signature)
        {
            return entry->guard == hashes.guard ? &entry->token : 0;
        }
        slot = (slot + 1u) & (MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY - 1u);
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void machine_x64_metadata_shape_cache_publish_slots(void)
{
    for (u32 slot = 0; slot < MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY; slot += 1)
    {
        machine_x64_metadata_shape_cache_slots[slot] = 0;
    }
    for (u32 entry_index = 0; entry_index < machine_x64_metadata_shape_cache_count; entry_index += 1)
    {
        u32 slot = (u32)machine_x64_metadata_shape_cache[entry_index].signature &
                   (MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY - 1u);
        bool placed = false;
        for (u32 probe = 0; probe < MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY; probe += 1)
        {
            if (!machine_x64_metadata_shape_cache_slots[slot])
            {
                machine_x64_metadata_shape_cache_slots[slot] = (u16)(entry_index + 1u);
                placed = true;
                break;
            }
            slot = (slot + 1u) & (MACHINE_X64_METADATA_SHAPE_CACHE_SLOT_CAPACITY - 1u);
        }
        if (!placed) machine_x64_metadata_shape_cache_invalid_count += 1;
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_metadata_shape_cache_prepare_zero(void)
{
    BusterX86MetadataPhysicalAttributes attributes = {0};
    (void)machine_x64_metadata_shape_cache_add(S8("CDQ"), 0, 0, (BusterX86MetadataFeatureInput){0}, attributes);
    (void)machine_x64_metadata_shape_cache_add(S8("CQO"), 0, 0, (BusterX86MetadataFeatureInput){0}, attributes);
    (void)machine_x64_metadata_shape_cache_add(S8("RET"), 0, 0, (BusterX86MetadataFeatureInput){0}, attributes);
    (void)machine_x64_metadata_shape_cache_add(S8("UD2"), 0, 0, (BusterX86MetadataFeatureInput){0}, attributes);
    (void)machine_x64_metadata_shape_cache_add(S8("VZEROUPPER"), 0, 0,
                                               (BusterX86MetadataFeatureInput){.names = machine_x64_avx_features,
                                                                                .count = BUSTER_ARRAY_LENGTH(machine_x64_avx_features)},
                                               attributes);
}

BUSTER_GLOBAL_LOCAL void machine_x64_metadata_shape_cache_prepare_unary(void)
{
    BusterX86MetadataPhysicalAttributes attributes = {0};
    BusterX86MetadataPhysicalOperand operand = machine_x64_exact_gpr_operand(0, 64);
    (void)machine_x64_metadata_shape_cache_add(S8("PUSH"), &operand, 1, (BusterX86MetadataFeatureInput){0}, attributes);
    (void)machine_x64_metadata_shape_cache_add(S8("POP"), &operand, 1, (BusterX86MetadataFeatureInput){0}, attributes);
    (void)machine_x64_metadata_shape_cache_add(S8("CALL"), &operand, 1, (BusterX86MetadataFeatureInput){0}, attributes);
    operand.width = operand.reg.width = 32;
    (void)machine_x64_metadata_shape_cache_add(S8("DIV"), &operand, 1, (BusterX86MetadataFeatureInput){0}, attributes);
    (void)machine_x64_metadata_shape_cache_add(S8("IDIV"), &operand, 1, (BusterX86MetadataFeatureInput){0}, attributes);
    operand.width = operand.reg.width = 64;
    (void)machine_x64_metadata_shape_cache_add(S8("DIV"), &operand, 1, (BusterX86MetadataFeatureInput){0}, attributes);
    (void)machine_x64_metadata_shape_cache_add(S8("IDIV"), &operand, 1, (BusterX86MetadataFeatureInput){0}, attributes);
    for (u32 register_index = 0; register_index < 16; register_index += 1)
    {
        operand = machine_x64_exact_gpr_operand(register_index, 64);
        (void)machine_x64_metadata_shape_cache_add(S8("JMP"), &operand, 1, (BusterX86MetadataFeatureInput){0}, attributes);
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_metadata_shape_cache_prepare_registers(void)
{
    BusterX86MetadataPhysicalAttributes attributes = {0};
    String8 binary_names[] = {S8("MOV"), S8("XOR"), S8("ADD"), S8("SUB"), S8("AND"), S8("OR")};
    u16 binary_widths[] = {8, 16, 32, 64};
    for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(binary_names); name_index += 1)
    {
        for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(binary_widths); width_index += 1)
        {
            u16 width = binary_widths[width_index];
            BusterX86MetadataPhysicalOperand operands[2] = {
                machine_x64_exact_gpr_operand(0, width), machine_x64_exact_gpr_operand(1, width),
            };
            (void)machine_x64_metadata_shape_cache_add(binary_names[name_index], operands, 2,
                                                        (BusterX86MetadataFeatureInput){0}, attributes);
        }
    }
    u16 compare_widths[] = {32, 64};
    for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(compare_widths); width_index += 1)
    {
        u16 width = compare_widths[width_index];
        BusterX86MetadataPhysicalOperand operands[2] = {
            machine_x64_exact_gpr_operand(0, width), machine_x64_exact_gpr_operand(1, width),
        };
        (void)machine_x64_metadata_shape_cache_add(S8("CMP"), operands, 2, (BusterX86MetadataFeatureInput){0}, attributes);
        (void)machine_x64_metadata_shape_cache_add(S8("TEST"), operands, 2, (BusterX86MetadataFeatureInput){0}, attributes);
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_metadata_shape_cache_prepare_immediates(void)
{
    BusterX86MetadataPhysicalAttributes attributes = {0};
    BusterX86MetadataPhysicalOperand operands[2] = {0};
    String8 names[] = {S8("MOV"), S8("SUB"), S8("SHR"), S8("AND"), S8("CMP"), S8("ADD")};
    u16 register_widths[] = {32, 64};
    for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(names); name_index += 1)
    {
        for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(register_widths); width_index += 1)
        {
            u16 register_width = register_widths[width_index];
            if (string_equal(names[name_index], S8("MOV")) && register_width != 32) continue;
            if (string_equal(names[name_index], S8("SHR")) && register_width != 64) continue;
            u16 immediate_widths[] = {8, 32};
            for (u32 immediate_width_index = 0; immediate_width_index < BUSTER_ARRAY_LENGTH(immediate_widths); immediate_width_index += 1)
            {
                u16 immediate_width = immediate_widths[immediate_width_index];
                if (string_equal(names[name_index], S8("SHR")) && immediate_width != 8) continue;
                operands[0] = machine_x64_exact_gpr_operand(0, register_width);
                // Keep the explicit-immediate SHR form in the prepared
                // cache.  A representative value of one selects XED's
                // shorter implicit `shr reg, 1` row, whose exact token has
                // no visible immediate operand and therefore cannot be
                // replayed through the two-operand machine bridge.  Values
                // one and two share the same signed-int8 shape class, so
                // workers still find this token for the runtime shift-by-one
                // expansion while retaining its explicit immediate field.
                s64 representative_immediate = string_equal(names[name_index], S8("SHR")) && immediate_width == 8 ? 2 : (immediate_width == 8 ? 1 : 128);
                operands[1] = machine_x64_exact_immediate_operand(representative_immediate, immediate_width);
                (void)machine_x64_metadata_shape_cache_add(names[name_index], operands, 2,
                                                            (BusterX86MetadataFeatureInput){0}, attributes);
                // A width-32 immediate that fits the signed eight-bit form
                // and one that does not are distinct metadata rows.
                if (immediate_width == 32)
                {
                    operands[1] = machine_x64_exact_immediate_operand(1, immediate_width);
                    (void)machine_x64_metadata_shape_cache_add(names[name_index], operands, 2,
                                                                (BusterX86MetadataFeatureInput){0}, attributes);
                    operands[1] = machine_x64_exact_immediate_operand(-1, immediate_width);
                    (void)machine_x64_metadata_shape_cache_add(names[name_index], operands, 2,
                                                                (BusterX86MetadataFeatureInput){0}, attributes);
                    operands[1] = machine_x64_exact_immediate_operand(-129, immediate_width);
                    (void)machine_x64_metadata_shape_cache_add(names[name_index], operands, 2,
                                                                (BusterX86MetadataFeatureInput){0}, attributes);
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_metadata_shape_memory(u32 base, u16 width, s64 displacement,
                                                                                         bool force_displacement)
{
    return machine_x64_exact_memory_operand(base, width, displacement, force_displacement);
}

BUSTER_GLOBAL_LOCAL void machine_x64_metadata_shape_cache_prepare_memory(void)
{
    BusterX86MetadataPhysicalAttributes attributes = {0};
    u16 widths[] = {8, 16, 32, 64};
    u32 bases[] = {MACHINE_X64_RAX, MACHINE_X64_RBP};
    s64 displacements[] = {0, 8, 256};
    for (u32 base_index = 0; base_index < BUSTER_ARRAY_LENGTH(bases); base_index += 1)
    {
        for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(widths); width_index += 1)
        {
            u16 width = widths[width_index];
            for (u32 displacement_index = 0; displacement_index < BUSTER_ARRAY_LENGTH(displacements); displacement_index += 1)
            {
                s64 displacement = displacements[displacement_index];
                BusterX86MetadataPhysicalOperand operands[2] = {
                    machine_x64_exact_gpr_operand(0, 64), machine_x64_metadata_shape_memory(bases[base_index], width, displacement, displacement != 0),
                };
                if (width >= 32)
                {
                    (void)machine_x64_metadata_shape_cache_add(S8("MOV"), operands, 2, (BusterX86MetadataFeatureInput){0}, attributes);
                }
                (void)machine_x64_metadata_shape_cache_add(S8("LEA"), operands, 2, (BusterX86MetadataFeatureInput){0}, attributes);
                if (width == 32)
                {
                    operands[0] = machine_x64_exact_gpr_operand(0, 32);
                    (void)machine_x64_metadata_shape_cache_add(S8("MOV"), operands, 2,
                                                                (BusterX86MetadataFeatureInput){0}, attributes);
                    operands[0] = machine_x64_exact_gpr_operand(0, 64);
                }
                if (width == 8 || width == 16)
                {
                    (void)machine_x64_metadata_shape_cache_add(S8("MOVZX"), operands, 2, (BusterX86MetadataFeatureInput){0}, attributes);
                }
                operands[0] = operands[1];
                operands[1] = machine_x64_exact_gpr_operand(0, width);
                (void)machine_x64_metadata_shape_cache_add(S8("MOV"), operands, 2, (BusterX86MetadataFeatureInput){0}, attributes);
            }
        }
    }
    BusterX86MetadataPhysicalOperand test_operands[2] = {
        machine_x64_exact_memory_operand(MACHINE_X64_RSP, 8, 0, false), machine_x64_exact_immediate_operand(0, 8),
    };
    (void)machine_x64_metadata_shape_cache_add(S8("TEST"), test_operands, 2, (BusterX86MetadataFeatureInput){0}, attributes);
    BusterX86MetadataPhysicalOperand fs_operands[2] = {
        machine_x64_exact_gpr_operand(0, 64),
        {
            .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
            .width = 64,
            .memory = {.displacement = 0, .address_size = 64, .scale = 1, .has_displacement = true,
                       .segment = BUSTER_X86_METADATA_SEGMENT_FS, .has_segment = true},
        },
    };
    (void)machine_x64_metadata_shape_cache_add(S8("MOV"), fs_operands, 2, (BusterX86MetadataFeatureInput){0}, attributes);
    BusterX86MetadataPhysicalOperand rip_operands[2] = {
        machine_x64_exact_gpr_operand(0, 64), machine_x64_exact_rip_memory_operand(),
    };
    for (u32 register_index = 0; register_index < 16; register_index += 1)
    {
        rip_operands[0] = machine_x64_exact_gpr_operand(register_index, 64);
        (void)machine_x64_metadata_shape_cache_add(S8("LEA"), rip_operands, 2, (BusterX86MetadataFeatureInput){0}, attributes);
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_metadata_shape_cache_prepare_relative(void)
{
    BusterX86MetadataPhysicalAttributes attributes = {0};
    String8 names[] = {S8("CALL"), S8("JMP"), S8("JNS"), S8("JB"), S8("JNBE"), S8("JNZ"), S8("JZ")};
    u16 widths[] = {8, 32};
    for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(names); name_index += 1)
    {
        for (u32 width_index = 0; width_index < BUSTER_ARRAY_LENGTH(widths); width_index += 1)
        {
            if (string_equal(names[name_index], S8("CALL")) && widths[width_index] != 32) continue;
            if (string_equal(names[name_index], S8("JNS")) && widths[width_index] != 8) continue;
            if (string_equal(names[name_index], S8("JB")) && widths[width_index] != 8) continue;
            if (string_equal(names[name_index], S8("JNBE")) && widths[width_index] != 32) continue;
            if (string_equal(names[name_index], S8("JNZ")) && widths[width_index] != 32) continue;
            if (string_equal(names[name_index], S8("JZ")) && widths[width_index] != 32) continue;
            BusterX86MetadataPhysicalOperand operand = machine_x64_exact_relative_operand(0, widths[width_index]);
            (void)machine_x64_metadata_shape_cache_add(names[name_index], &operand, 1, (BusterX86MetadataFeatureInput){0}, attributes);
        }
    }
}

BUSTER_GLOBAL_LOCAL void machine_x64_metadata_shape_cache_prepare_float_vector_atomic(void)
{
    BusterX86MetadataPhysicalAttributes attributes = {0};
    BusterX86MetadataFeatureInput sse = {.names = machine_x64_sse_features, .count = BUSTER_ARRAY_LENGTH(machine_x64_sse_features)};
    BusterX86MetadataFeatureInput sse2 = {.names = machine_x64_sse2_features, .count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features)};
    BusterX86MetadataFeatureInput avx512 = {.names = machine_x64_avx512_features, .count = BUSTER_ARRAY_LENGTH(machine_x64_avx512_features)};
    BusterX86MetadataFeatureInput cx16 = {.names = machine_x64_cx16_features, .count = BUSTER_ARRAY_LENGTH(machine_x64_cx16_features)};
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_xmm_operand(0, 128), machine_x64_exact_gpr_operand(0, 64),
    };
    (void)machine_x64_metadata_shape_cache_add(S8("MOVQ"), operands, 2, sse2, attributes);
    operands[0] = machine_x64_exact_xmm_operand(0, 64);
    (void)machine_x64_metadata_shape_cache_add(S8("MOVQ"), operands, 2, sse2, attributes);
    operands[0] = machine_x64_exact_xmm_operand(0, 32);
    (void)machine_x64_metadata_shape_cache_add(S8("CVTSI2SS"), operands, 2, sse, attributes);
    operands[0] = machine_x64_exact_xmm_operand(0, 64);
    (void)machine_x64_metadata_shape_cache_add(S8("CVTSI2SD"), operands, 2, sse2, attributes);

    operands[0] = machine_x64_exact_gpr_operand(0, 64);
    operands[1] = machine_x64_exact_xmm_operand(0, 128);
    (void)machine_x64_metadata_shape_cache_add(S8("MOVQ"), operands, 2, sse2, attributes);
    operands[1] = machine_x64_exact_xmm_operand(0, 32);
    (void)machine_x64_metadata_shape_cache_add(S8("CVTTSS2SI"), operands, 2, sse, attributes);
    operands[1] = machine_x64_exact_xmm_operand(0, 64);
    (void)machine_x64_metadata_shape_cache_add(S8("CVTTSD2SI"), operands, 2, sse2, attributes);
    operands[1] = machine_x64_exact_xmm_operand(0, 128);
    (void)machine_x64_metadata_shape_cache_add(S8("CVTTSS2SI"), operands, 2, sse, attributes);
    (void)machine_x64_metadata_shape_cache_add(S8("CVTTSD2SI"), operands, 2, sse2, attributes);

    String8 scalar_names[] = {S8("ADDSS"), S8("ADDSD"), S8("SUBSS"), S8("SUBSD"), S8("UCOMISS"), S8("UCOMISD")};
    for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(scalar_names); name_index += 1)
    {
        bool double_width = string_ends_with_sequence(scalar_names[name_index], S8("SD"));
        BusterX86MetadataFeatureInput feature = double_width ? sse2 : sse;
        BusterX86MetadataPhysicalOperand scalar_operands[2] = {
            machine_x64_exact_xmm_operand(0, double_width ? 64 : 32), machine_x64_exact_xmm_operand(1, double_width ? 64 : 32),
        };
        (void)machine_x64_metadata_shape_cache_add(scalar_names[name_index], scalar_operands, 2, feature, attributes);
    }
    for (u32 displacement_index = 0; displacement_index < 3; displacement_index += 1)
    {
        s64 displacement = displacement_index == 0 ? 0 : displacement_index == 1 ? 8 : 256;
        BusterX86MetadataPhysicalOperand memory_operand = machine_x64_exact_memory_operand(MACHINE_X64_RBP, 64, displacement, displacement != 0);
        BusterX86MetadataPhysicalOperand xmm_operand = machine_x64_exact_xmm_operand(0, 64);
        BusterX86MetadataPhysicalOperand memory_operands[2] = {xmm_operand, memory_operand};
        (void)machine_x64_metadata_shape_cache_add(S8("MOVSD"), memory_operands, 2, sse2, attributes);
        memory_operands[0] = memory_operand;
        memory_operands[1] = xmm_operand;
        (void)machine_x64_metadata_shape_cache_add(S8("MOVSD"), memory_operands, 2, sse2, attributes);
        BusterX86MetadataPhysicalOperand zmm_operand = machine_x64_exact_zmm_operand(0, 512);
        memory_operand.width = 8;
        BusterX86MetadataPhysicalOperand vector_operands[2] = {zmm_operand, memory_operand};
        (void)machine_x64_metadata_shape_cache_add(S8("VMOVDQU8"), vector_operands, 2, avx512, attributes);
        vector_operands[0] = memory_operand;
        vector_operands[1] = zmm_operand;
        (void)machine_x64_metadata_shape_cache_add(S8("VMOVDQU8"), vector_operands, 2, avx512, attributes);
    }
    BusterX86MetadataPhysicalOperand vector_registers[2] = {
        machine_x64_exact_zmm_operand(0, 512), machine_x64_exact_zmm_operand(1, 512),
    };
    (void)machine_x64_metadata_shape_cache_add(S8("VMOVDQU8"), vector_registers, 2, avx512, attributes);

    attributes.lock = true;
    for (u32 width_index = 0; width_index < 4; width_index += 1)
    {
        u16 width = (u16)(8u << width_index);
        BusterX86MetadataPhysicalOperand atomic_operands[2] = {
            machine_x64_exact_memory_operand(MACHINE_X64_RCX, width, 0, false), machine_x64_exact_gpr_operand(0, width),
        };
        (void)machine_x64_metadata_shape_cache_add(S8("CMPXCHG"), atomic_operands, 2,
                                                    (BusterX86MetadataFeatureInput){0}, attributes);
    }
    BusterX86MetadataPhysicalOperand cmpxchg16_operand = machine_x64_exact_memory_operand(MACHINE_X64_RCX, 128, 0, false);
    (void)machine_x64_metadata_shape_cache_add(S8("CMPXCHG16B"), &cmpxchg16_operand, 1, cx16, attributes);
}

BUSTER_GLOBAL_LOCAL void machine_x64_metadata_shape_cache_prewarm(void)
{
    if (machine_x64_metadata_shape_cache_ready) return;
    machine_x64_metadata_shape_cache_count = 0;
    machine_x64_metadata_shape_cache_invalid_count = 0;
    machine_x64_metadata_shape_cache_prepare_zero();
    machine_x64_metadata_shape_cache_prepare_unary();
    machine_x64_metadata_shape_cache_prepare_registers();
    machine_x64_metadata_shape_cache_prepare_immediates();
    machine_x64_metadata_shape_cache_prepare_memory();
    machine_x64_metadata_shape_cache_prepare_relative();
    machine_x64_metadata_shape_cache_prepare_float_vector_atomic();
    machine_x64_metadata_shape_cache_publish_slots();
    machine_x64_metadata_shape_cache_ready = machine_x64_metadata_shape_cache_invalid_count == 0;
}

#if BUSTER_INCLUDE_TESTS
MachineX64MetadataShapeCacheAudit machine_x86_64_metadata_shape_cache_audit(void)
{
    machine_x86_64_exact_prewarm();
    return (MachineX64MetadataShapeCacheAudit){
        .prepared_rows = machine_x64_metadata_shape_cache_count,
        .invalid_rows = machine_x64_metadata_shape_cache_invalid_count,
        .valid = machine_x64_metadata_shape_cache_ready && machine_x64_metadata_shape_cache_count != 0 &&
                 machine_x64_metadata_shape_cache_invalid_count == 0,
    };
}
#endif

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_exact_form(MachineX64Encoder* encoder,
                                                     BusterX86MetadataMachineExactToken metadata_token,
                                                     BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
                                                     bool force_disp32,
                                                     bool force_lock, u8 mask_register_plus_one, bool zeroing,
                                                     MachineX64ExactEmitCounters* counters)
{
    if (counters) counters->attempts += 1;
    // The metadata bridge writes only after all dynamic validation and
    // capacity checks succeed.  Point it at the final encoder storage so the
    // machine path does not copy through a second 16-byte staging buffer.
    // Guard the subtraction and pointer formation first: an already-overflowed
    // encoder must fail closed without forming an out-of-range destination.
    if (encoder->count > encoder->capacity)
    {
        encoder->overflow = true;
        if (counters) counters->fallbacks += 1;
        return false;
    }
    u32 output_capacity = encoder->capacity - encoder->count;
    if (output_capacity > 16u) output_capacity = 16u;
    u8* output = encoder->bytes + encoder->count;
    BusterX86MetadataEmitResult emitted = buster_x86_metadata_emit_exact_machine(metadata_token, (BusterX86MetadataMachineExactQuery){
        .operands = operands,
        .operand_count = operand_count,
        .force_disp32 = force_disp32,
        .force_lock = force_lock,
        .mask_register_plus_one = mask_register_plus_one,
        .zeroing = zeroing,
        .output = output,
        .output_capacity = output_capacity,
        .relocations = 0,
        .relocation_capacity = 0,
    });
    if (emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || emitted.relocation_count != 0 ||
        emitted.byte_count > 16u || emitted.byte_count > output_capacity)
    {
        encoder->overflow = true;
        if (counters) counters->fallbacks += 1;
        return false;
    }
    encoder->count += emitted.byte_count;
    if (counters) counters->successes += 1;
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_exact_reject(MachineX64Encoder* encoder, MachineX64ExactEmitCounters* counters);

// Expansion rows are architectural sequences rather than one MIR opcode.
// Keep their individual instructions in the same metadata path as ordinary
// exact rows: select one durable form from the physical shape, then immediately
// re-emit that selected form through the exact-form transform.  The selection
// is deliberately local to the sequence helper (there is no callback or raw
// byte template hidden behind it); callers only provide the mnemonic and the
// already-typed physical operands.  Relative/TLS fields are passed as their
// neutral zero values and remain owned by the fixup/call-site stream.
BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_instruction(MachineX64Encoder* encoder, String8 mnemonic,
                                                              BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
                                                              BusterX86MetadataFeatureInput features,
                                                              BusterX86MetadataPhysicalAttributes attributes,
                                                              MachineX64ExactEmitCounters* counters)
{
    // Scalar SSE/SSE2 forms are real feature-gated metadata rows.  Callers
    // that use the generic mnemonic bridge intentionally omit a feature list;
    // derive the architectural requirement here so selection cannot silently
    // reject an otherwise valid exact instruction as a feature-mode miss.
    if (!features.count)
    {
        if (string_equal(mnemonic, S8("CVTSI2SS")) || string_equal(mnemonic, S8("ADDSS")) || string_equal(mnemonic, S8("UCOMISS")) ||
            string_equal(mnemonic, S8("SUBSS")) || string_equal(mnemonic, S8("CVTTSS2SI")))
        {
            features.names = machine_x64_sse_features;
            features.count = BUSTER_ARRAY_LENGTH(machine_x64_sse_features);
        }
        else if (string_equal(mnemonic, S8("CVTSI2SD")) || string_equal(mnemonic, S8("ADDSD")) || string_equal(mnemonic, S8("UCOMISD")) ||
                 string_equal(mnemonic, S8("SUBSD")) || string_equal(mnemonic, S8("CVTTSD2SI")) || string_equal(mnemonic, S8("MOVQ")) ||
                 string_equal(mnemonic, S8("MOVSD")))
        {
            features.names = machine_x64_sse2_features;
            features.count = BUSTER_ARRAY_LENGTH(machine_x64_sse2_features);
        }
    }
    // Workers never re-enter the generic selector.  The serial prewarm lane
    // populated one immutable token per finite physical shape; a stale or
    // unclassified shape fails closed instead of falling back to handwritten
    // bytes or a checked query lookup.
    if (!machine_x64_metadata_shape_cache_ready)
    {
        return machine_x64_exact_reject(encoder, counters);
    }
    BusterX86MetadataMachineExactToken const* token = machine_x64_metadata_shape_cache_find(mnemonic, operands, operand_count, features, attributes);
    bool result;
    if (!token)
    {
        result = machine_x64_exact_reject(encoder, counters);
    }
    else
    {
        result = machine_x64_emit_exact_form(encoder, *token, operands, operand_count, false, attributes.lock, 0, false, counters);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL MachineX64PreparedExactOpcode const* machine_x64_exact_opcode_for_opcode(u16 opcode);

BUSTER_GLOBAL_LOCAL bool machine_x64_exact_reject(MachineX64Encoder* encoder, MachineX64ExactEmitCounters* counters)
{
    // These exact wrappers are used by spill/reload and expansion paths whose
    // callers intentionally ignore the bool result.  Keep a stale or missing
    // prepared row from silently deleting an instruction.
    encoder->overflow = true;
    if (counters)
    {
        counters->attempts += 1;
        counters->fallbacks += 1;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_registers(MachineX64Encoder* encoder, String8 mnemonic, u32 destination, u32 source,
                                                             u16 width, MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_gpr_operand(destination, width),
        machine_x64_exact_gpr_operand(source, width),
    };
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_register(MachineX64Encoder* encoder, String8 mnemonic, u32 reg, u16 width,
                                                            MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operand = machine_x64_exact_gpr_operand(reg, width);
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, &operand, 1, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_register_immediate(MachineX64Encoder* encoder, String8 mnemonic, u32 destination,
                                                                      u64 immediate, u16 register_width, u16 immediate_width,
                                                                      MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_gpr_operand(destination, register_width),
        machine_x64_exact_immediate_operand((s64)immediate, immediate_width),
    };
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_register_memory(MachineX64Encoder* encoder, String8 mnemonic, u32 destination, u32 base,
                                                                   s64 displacement, u16 register_width, u16 memory_width,
                                                                   MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_gpr_operand(destination, register_width),
        machine_x64_exact_memory_operand(base, memory_width, displacement, displacement != 0),
    };
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_memory_register(MachineX64Encoder* encoder, String8 mnemonic, u32 base, s64 displacement,
                                                                   u32 source, u16 memory_width, u16 register_width,
                                                                   MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_memory_operand(base, memory_width, displacement, displacement != 0),
        machine_x64_exact_gpr_operand(source, register_width),
    };
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_atomic_memory_register(MachineX64Encoder* encoder, String8 mnemonic, u32 base, u32 source,
                                                                         u16 width, MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_memory_operand(base, width, 0, false),
        machine_x64_exact_gpr_operand(source, width),
    };
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){.lock = true}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_atomic_memory(MachineX64Encoder* encoder, String8 mnemonic, u32 base, u16 width,
                                                                 MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operand = machine_x64_exact_memory_operand(base, width, 0, false);
    BusterX86MetadataFeatureInput features = {0};
    if (string_equal(mnemonic, S8("CMPXCHG16B")))
    {
        features.names = machine_x64_cx16_features;
        features.count = BUSTER_ARRAY_LENGTH(machine_x64_cx16_features);
    }
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, &operand, 1, features,
                                                 (BusterX86MetadataPhysicalAttributes){.lock = true}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_relative(MachineX64Encoder* encoder, String8 mnemonic, s64 displacement, u16 width,
                                                            MachineX64ExactEmitCounters* counters)
{
    // The metadata snapshot uses one canonical spelling per condition-code
    // opcode.  JA/JNE are architectural aliases of JNBE/JNZ, respectively;
    // route those spellings through the canonical metadata rows while the
    // caller continues to express the branch semantics naturally.
    String8 metadata_mnemonic = mnemonic;
    if (string_equal(mnemonic, S8("JA"))) metadata_mnemonic = S8("JNBE");
    else if (string_equal(mnemonic, S8("JNE"))) metadata_mnemonic = S8("JNZ");
    BusterX86MetadataPhysicalOperand operand = machine_x64_exact_relative_operand(displacement, width);
    return machine_x64_emit_metadata_instruction(encoder, metadata_mnemonic, &operand, 1, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_memory_immediate(MachineX64Encoder* encoder, String8 mnemonic, u32 base, s64 displacement,
                                                                    u64 immediate, u16 memory_width, u16 immediate_width,
                                                                    MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_memory_operand(base, memory_width, displacement, displacement != 0),
        machine_x64_exact_immediate_operand((s64)immediate, immediate_width),
    };
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand machine_x64_exact_zmm_operand(u32 reg, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
        .width = width,
        .reg = {
            .index = (u16)reg,
            .width = width,
            .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM,
        },
    };
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_zmm_registers(MachineX64Encoder* encoder, String8 mnemonic, u32 destination, u32 source,
                                                                 u16 width, BusterX86MetadataFeatureInput features,
                                                                 MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_zmm_operand(destination, width),
        machine_x64_exact_zmm_operand(source, width),
    };
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, features,
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_zmm_memory(MachineX64Encoder* encoder, String8 mnemonic, u32 zmm, u32 base, s64 displacement,
                                                              bool store, u16 vector_width, u16 memory_width, BusterX86MetadataFeatureInput features,
                                                              MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {0};
    BusterX86MetadataPhysicalOperand memory = machine_x64_exact_memory_operand(base, memory_width, displacement, displacement != 0);
    BusterX86MetadataPhysicalOperand vector = machine_x64_exact_zmm_operand(zmm, vector_width);
    operands[0] = store ? memory : vector;
    operands[1] = store ? vector : memory;
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, features,
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_xmm_gpr(MachineX64Encoder* encoder, String8 mnemonic, u32 xmm, u32 gpr, u16 xmm_width,
                                                           u16 gpr_width, MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_xmm_operand(xmm, xmm_width),
        machine_x64_exact_gpr_operand(gpr, gpr_width),
    };
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_gpr_xmm(MachineX64Encoder* encoder, String8 mnemonic, u32 gpr, u32 xmm, u16 gpr_width,
                                                           u16 xmm_width, MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_gpr_operand(gpr, gpr_width),
        machine_x64_exact_xmm_operand(xmm, xmm_width),
    };
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_xmm_registers(MachineX64Encoder* encoder, String8 mnemonic, u32 destination, u32 source,
                                                                 u16 width, MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_xmm_operand(destination, width),
        machine_x64_exact_xmm_operand(source, width),
    };
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_xmm_memory(MachineX64Encoder* encoder, String8 mnemonic, u32 xmm, u32 base, s64 displacement,
                                                              bool store, u16 width, MachineX64ExactEmitCounters* counters)
{
    BusterX86MetadataPhysicalOperand operands[2] = {0};
    BusterX86MetadataPhysicalOperand memory = machine_x64_exact_memory_operand(base, width, displacement, displacement != 0);
    BusterX86MetadataPhysicalOperand vector = machine_x64_exact_xmm_operand(xmm, width);
    operands[0] = store ? memory : vector;
    operands[1] = store ? vector : memory;
    return machine_x64_emit_metadata_instruction(encoder, mnemonic, operands, 2, (BusterX86MetadataFeatureInput){0},
                                                 (BusterX86MetadataPhysicalAttributes){0}, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_variable_memory_encoding(
    MachineX64Encoder* encoder, u8 table_plus_one, u32 reg, u32 base, s32 displacement,
    bool force_disp32, MachineX64ExactEmitCounters* counters);

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_exact_frame_chunk(MachineX64Encoder* encoder, bool load, u32 reg, u32 offset, u32 chunk,
                                                             MachineX64ExactEmitCounters* counters)
{
    // Spill/reload and expansion chunks are the same MOV/MOVZX population as
    // pointer memory.  Preserve their canonical frame shape by selecting the
    // already-proven disp32 lane rather than rebuilding physical operands.
    u16 pointer_opcode = load ? (chunk == 1 ? MACHINE_X64_LOAD_PTR8 : chunk == 2 ? MACHINE_X64_LOAD_PTR16 : chunk == 4 ? MACHINE_X64_LOAD_PTR32
                                                                                                                       : MACHINE_X64_LOAD_PTR64)
                              : (chunk == 1 ? MACHINE_X64_STORE_PTR8 : chunk == 2 ? MACHINE_X64_STORE_PTR16
                                                            : chunk == 4       ? MACHINE_X64_STORE_PTR32
                                                                               : MACHINE_X64_STORE_PTR64);
    MachineX64PreparedExactOpcode const* pointer_entry = machine_x64_exact_opcode_for_opcode(pointer_opcode);
    if (pointer_entry && pointer_entry->plan_valid && pointer_entry->variant_count &&
        machine_x64_emit_variable_memory_encoding(
            encoder, pointer_entry->variable_memory_encoding_tables[0], reg, MACHINE_X64_RBP,
            (s32)(0u - offset), true, counters))
        return true;
    if (encoder->overflow) return false;

    u16 opcode = load ? (chunk == 1 ? MACHINE_X64_LOAD_PTR8 : chunk == 2 ? MACHINE_X64_LOAD_PTR16 : chunk == 4 ? MACHINE_X64_LOAD_PTR32
                                                                           : MACHINE_X64_LOAD_FRAME)
                      : (chunk == 1 ? MACHINE_X64_STORE_FRAME8 : chunk == 2 ? MACHINE_X64_STORE_FRAME16 : chunk == 4 ? MACHINE_X64_STORE_FRAME32
                                                                                                                         : MACHINE_X64_STORE_FRAME64);
    u16 width = (u16)(chunk * 8u);
    MachineX64PreparedExactOpcode const* entry = machine_x64_exact_opcode_for_opcode(opcode);
    if (!entry || !entry->descriptor || !entry->plan_valid || !entry->variant_count)
    {
        return machine_x64_exact_reject(encoder, counters);
    }
    BusterX86MetadataPhysicalOperand operands[2];
    if (load)
    {
        operands[0] = machine_x64_exact_gpr_operand(reg, chunk <= 2 ? 64 : width);
        operands[1] = machine_x64_exact_rbp_memory_operand(-(s64)(s32)offset, width, true);
    }
    else
    {
        operands[0] = machine_x64_exact_rbp_memory_operand(-(s64)(s32)offset, width, true);
        operands[1] = machine_x64_exact_gpr_operand(reg, width);
    }
    MachineX64ExactRecipeVariant variant = machine_x64_exact_recipe_variant(entry->descriptor, 0);
    return machine_x64_emit_exact_form(encoder, entry->metadata_tokens[0], operands, variant.operand_count, true, false, 0, false, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_exact_movabs(MachineX64Encoder* encoder, u32 reg, u64 value,
                                                       MachineX64ExactEmitCounters* counters)
{
    MachineX64PreparedExactOpcode const* entry = machine_x64_exact_opcode_for_opcode(MACHINE_X64_MOV_RI);
    if (!entry || !entry->descriptor || !entry->plan_valid || entry->variant_count < 3)
    {
        return machine_x64_exact_reject(encoder, counters);
    }
    MachineX64ExactRecipeVariant variant = machine_x64_exact_recipe_variant(entry->descriptor, 2);
    BusterX86MetadataPhysicalOperand operands[2] = {
        machine_x64_exact_gpr_operand(reg, 64),
        machine_x64_exact_unsigned_immediate_operand(value, 64),
    };
    return machine_x64_emit_exact_form(encoder, entry->metadata_tokens[2], operands, variant.operand_count, false, false, 0, false, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_variable_memory_encoding(
    MachineX64Encoder* encoder, u8 table_plus_one, u32 reg, u32 base, s32 displacement,
    bool force_disp32, MachineX64ExactEmitCounters* counters)
{
    if (!table_plus_one || table_plus_one > machine_x64_variable_memory_encoding_table_count || reg >= 16 || base >= 16)
        return false;

    u32 displacement_class = force_disp32 ? 2 : displacement == 0 ? 0 : displacement >= INT8_MIN && displacement <= INT8_MAX ? 1 : 2;
    MachineX64VariableMemoryEncodingTable const* table =
        machine_x64_variable_memory_encoding_tables + (table_plus_one - 1u);
    MachineX64GprEncoding const* encoding =
        table->encodings[displacement_class] + reg + (base << 4);
    u32 byte_count = encoding->byte_count;
    if (encoder->count > encoder->capacity || byte_count > encoder->capacity - encoder->count)
    {
        encoder->overflow = true;
        if (counters)
        {
            counters->attempts += 1;
            counters->fallbacks += 1;
        }
        return false;
    }

    memcpy(encoder->bytes + encoder->count, encoding->bytes, byte_count);
    if (displacement_class == 1)
    {
        encoder->bytes[encoder->count + byte_count - 1u] = (u8)displacement;
    }
    else if (displacement_class == 2)
    {
        u32 patch_offset = encoder->count + byte_count - (u32)sizeof(u32);
        u32 value = (u32)displacement;
        for (u32 byte_index = 0; byte_index < sizeof(u32); byte_index += 1)
        {
            encoder->bytes[patch_offset + byte_index] = (u8)(value >> (byte_index * 8u));
        }
    }
    encoder->count += byte_count;
    if (counters)
    {
        counters->attempts += 1;
        counters->successes += 1;
    }
    return true;
}

// A fixed instruction sequence copied in verbatim. The only caller is the
// general-dynamic thread-local pair, whose prefixes are part of the sequence a
// linker matches on rather than an encoding the metadata tables would choose;
// everything else goes through the encoder so its form stays the audited one.
BUSTER_GLOBAL_LOCAL bool machine_x64_emit_literal_bytes(MachineX64Encoder* encoder, u8 const* literal, u32 byte_count)
{
    if (encoder->count > encoder->capacity || byte_count > encoder->capacity - encoder->count)
    {
        encoder->overflow = true;
        return false;
    }
    memcpy(encoder->bytes + encoder->count, literal, byte_count);
    encoder->count += byte_count;
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_metadata_pointer_chunk(MachineX64Encoder* encoder, bool load, u32 reg, u32 base, u32 offset, u32 chunk,
                                                                  MachineX64ExactEmitCounters* counters)
{
    u16 opcode = load ? (chunk == 1 ? MACHINE_X64_LOAD_PTR8 : chunk == 2 ? MACHINE_X64_LOAD_PTR16 : chunk == 4 ? MACHINE_X64_LOAD_PTR32
                                                                                                               : MACHINE_X64_LOAD_PTR64)
                      : (chunk == 1 ? MACHINE_X64_STORE_PTR8 : chunk == 2 ? MACHINE_X64_STORE_PTR16 : chunk == 4 ? MACHINE_X64_STORE_PTR32
                                                                                                                 : MACHINE_X64_STORE_PTR64);
    MachineX64PreparedExactOpcode const* entry = machine_x64_exact_opcode_for_opcode(opcode);
    if (offset <= INT32_MAX && entry && entry->plan_valid && entry->variant_count &&
        machine_x64_emit_variable_memory_encoding(encoder, entry->variable_memory_encoding_tables[0], reg, base, (s32)offset, false, counters))
        return true;
    if (encoder->overflow) return false;

    u16 width = (u16)(chunk * 8u);
    if (load)
    {
        if (chunk == 1 || chunk == 2)
        {
            BusterX86MetadataPhysicalOperand operands[2] = {
                machine_x64_exact_gpr_operand(reg, 64),
                machine_x64_exact_memory_operand(base, width, offset, offset != 0),
            };
            bool result = machine_x64_emit_metadata_instruction(encoder, S8("MOVZX"), operands, 2, (BusterX86MetadataFeatureInput){0},
                                                                (BusterX86MetadataPhysicalAttributes){0}, counters);
            return result;
        }
        bool result = machine_x64_emit_metadata_register_memory(encoder, S8("MOV"), reg, base, offset, width, width, counters);
        return result;
    }
    bool result = machine_x64_emit_metadata_memory_register(encoder, S8("MOV"), base, offset, reg, width, width, counters);
    return result;
}

BUSTER_GLOBAL_LOCAL MachineX64PreparedExactOpcode const* machine_x64_exact_opcode_for_opcode(u16 opcode)
{
    MachineX64PreparedExactOpcode const* result;
    if (opcode < MACHINE_X64_MOV_RI || opcode > MACHINE_X64_VBINARY)
    {
        result = 0;
    }
    else
    {
        u32 ordinal = opcode - MACHINE_X64_MOV_RI;
        result = ordinal < BUSTER_ARRAY_LENGTH(machine_x64_exact_opcode_map) ? machine_x64_exact_opcode_map + ordinal : 0;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_exact_recipe(MachineX64Encoder* encoder, MachineX64PreparedExactOpcode const* entry,
                                                       MachineInstruction const* instruction, MachineStackPlacement const* placement,
                                                       u8 const* operand_registers, u32 payload, u64 immediate_value,
                                                       bool emit_self_copy, MachineX64ExactEmitCounters* counters)
{
    MachineX64ExactRecipe const* descriptor = entry ? entry->descriptor : 0;
    u32 variant_index = 0;
    u8 gpr_table_plus_one = entry ? entry->single_gpr_encoding_table : 0;
    if (!gpr_table_plus_one)
    {
        if (!descriptor || !entry->plan_valid)
        {
            if (counters) counters->attempts += 1;
            if (counters) counters->fallbacks += 1;
            return false;
        }
        if (descriptor->variant_selector == MACHINE_X64_EXACT_VARIANT_MOV_IMMEDIATE)
        {
            variant_index = immediate_value <= UINT32_MAX ? 0 : immediate_value >= UINT64_C(0xffffffff80000000) ? 1 : 2;
        }
        else if (descriptor->variant_selector == MACHINE_X64_EXACT_VARIANT_SIGNED_IMMEDIATE)
        {
            s64 signed_immediate = (s64)immediate_value;
            variant_index = signed_immediate >= INT8_MIN && signed_immediate <= INT8_MAX ? 0 : 1;
        }
        else if (descriptor->variant_selector == MACHINE_X64_EXACT_VARIANT_VBINARY)
        {
            switch (payload)
            {
            case 0xfcu: variant_index = 0; break;
            case 0xfdu: variant_index = 1; break;
            case 0xfeu: variant_index = 2; break;
            case 0x1d4u: variant_index = 3; break;
            case 0xf8u: variant_index = 4; break;
            case 0xf9u: variant_index = 5; break;
            case 0xfau: variant_index = 6; break;
            case 0x1fbu: variant_index = 7; break;
            case 0xdbu: variant_index = 8; break;
            case 0xebu: variant_index = 9; break;
            case 0xefu: variant_index = 10; break;
            default: variant_index = UINT32_MAX; break;
            }
        }
        if (variant_index >= entry->variant_count || !(entry->variant_valid_mask & (u16)(1u << variant_index)))
        {
            if (counters) counters->attempts += 1;
            if (counters) counters->fallbacks += 1;
            return false;
        }
        gpr_table_plus_one = entry->gpr_encoding_tables[variant_index];
    }
    if (gpr_table_plus_one && gpr_table_plus_one <= machine_x64_gpr_encoding_table_count)
    {
        MachineX64GprEncodingTable const* table = machine_x64_gpr_encoding_tables + (gpr_table_plus_one - 1u);
        u8 low_register = table->operand_count ? operand_registers[table->operand_slots[0]] : 0;
        u8 high_register = table->operand_count > 1 ? operand_registers[table->operand_slots[1]] : 0;
        if (low_register < 16 && high_register < 16)
        {
            if (!emit_self_copy && (table->flags & MACHINE_X64_GPR_ENCODING_TABLE_SELF_COPY_NOOP) && low_register == high_register)
            {
                if (counters)
                {
                    counters->attempts += 1;
                    counters->successes += 1;
                }
                return true;
            }
            MachineX64GprEncoding const* encoding = table->encodings + low_register + ((u32)high_register << 4);
            u32 byte_count = encoding->byte_count;
            s32 displacement = 0;
            if (table->flags & MACHINE_X64_GPR_ENCODING_TABLE_PATCH_DISPLACEMENT)
            {
                if (table->flags & MACHINE_X64_GPR_ENCODING_TABLE_INCOMING_DISPLACEMENT)
                {
                    displacement = (s32)(16u + (placement ? placement->incoming_base : 0u) + payload);
                }
                else
                {
                    u8 operand_slot = table->memory_operand_slot;
                    if (!instruction || !placement || operand_slot >= 4 ||
                        machine_ref_kind(instruction->operands[operand_slot]) != MACHINE_REF_STACK_SLOT)
                    {
                        if (counters)
                        {
                            counters->attempts += 1;
                            counters->fallbacks += 1;
                        }
                        return false;
                    }
                    u32 offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[operand_slot])] - payload;
                    displacement = -(s32)offset;
                }
            }
            if (counters) counters->attempts += 1;
            if (encoder->count > encoder->capacity || byte_count > encoder->capacity - encoder->count)
            {
                encoder->overflow = true;
                if (counters) counters->fallbacks += 1;
                return false;
            }
            if ((table->flags & MACHINE_X64_GPR_ENCODING_TABLE_COMPACT) &&
                encoder->capacity - encoder->count >= sizeof(u32))
            {
                memcpy(encoder->bytes + encoder->count, encoding->bytes, sizeof(u32));
            }
            else
            {
                memcpy(encoder->bytes + encoder->count, encoding->bytes, byte_count);
            }
            if (table->flags & MACHINE_X64_GPR_ENCODING_TABLE_PATCH_IMMEDIATE)
            {
                u32 immediate_width = table->immediate_width;
                if (!immediate_width || immediate_width > byte_count)
                {
                    if (counters) counters->fallbacks += 1;
                    return false;
                }
                u64 patch_value = table->flags & MACHINE_X64_GPR_ENCODING_TABLE_IMMEDIATE_FROM_PAYLOAD
                                      ? payload
                                      : immediate_value;
                u32 immediate_offset = encoder->count + byte_count - immediate_width;
                for (u32 byte_index = 0; byte_index < immediate_width; byte_index += 1)
                {
                    encoder->bytes[immediate_offset + byte_index] = (u8)(patch_value >> (byte_index * 8u));
                }
            }
            if (table->flags & MACHINE_X64_GPR_ENCODING_TABLE_PATCH_DISPLACEMENT)
            {
                u32 displacement_offset = encoder->count + byte_count - (u32)sizeof(u32);
                u32 value = (u32)displacement;
                for (u32 byte_index = 0; byte_index < sizeof(u32); byte_index += 1)
                {
                    encoder->bytes[displacement_offset + byte_index] = (u8)(value >> (byte_index * 8u));
                }
            }
            encoder->count += byte_count;
            if (counters) counters->successes += 1;
            return true;
        }
    }
    u8 variable_memory_table_plus_one = entry->variable_memory_encoding_tables[variant_index];
    if (variable_memory_table_plus_one &&
        variable_memory_table_plus_one <= machine_x64_variable_memory_encoding_table_count)
    {
        MachineX64VariableMemoryEncodingTable const* table =
            machine_x64_variable_memory_encoding_tables + (variable_memory_table_plus_one - 1u);
        if (machine_x64_emit_variable_memory_encoding(
                encoder, variable_memory_table_plus_one,
                operand_registers[table->gpr_operand_slot], operand_registers[table->memory_operand_slot],
                (s32)payload, false, counters))
            return true;
        if (encoder->overflow) return false;
    }
    MachineX64ExactRecipeVariant variant = machine_x64_exact_recipe_variant(descriptor, variant_index);
    if (!emit_self_copy && (variant.flags & MACHINE_X64_EXACT_RECIPE_FLAG_SELF_COPY_NOOP) &&
        operand_registers[variant.operand_slots[0]] == operand_registers[variant.operand_slots[1]])
    {
        if (counters)
        {
            counters->attempts += 1;
            counters->successes += 1;
        }
        return true;
    }
    // Every active descriptor slot is populated by the projection loop before
    // the metadata query; no inactive slot is consumed by the exact API.
    BusterX86MetadataPhysicalOperand operands[4];
    bool force_disp32 = (variant.flags & MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32) != 0;
    for (u32 operand_index = 0; operand_index < variant.operand_count; operand_index += 1)
    {
        u8 operand_slot = variant.operand_slots[operand_index];
        u16 width = variant.operand_widths[operand_index];
        switch ((MachineX64ExactOperandProjection)variant.operand_kinds[operand_index])
        {
        case MACHINE_X64_EXACT_OPERAND_GPR:
            operands[operand_index] = machine_x64_exact_gpr_operand(operand_registers[operand_slot], width);
            break;
        case MACHINE_X64_EXACT_OPERAND_XMM_PAYLOAD:
            operands[operand_index] = machine_x64_exact_xmm_operand(payload, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX:
            operands[operand_index] = machine_x64_exact_gpr_operand(MACHINE_X64_RAX, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RDX:
            operands[operand_index] = machine_x64_exact_gpr_operand(MACHINE_X64_RDX, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_GPR_PAYLOAD_SIZE:
        {
            u32 size = payload & 0xffu;
            if (size != 1 && size != 2 && size != 4 && size != 8)
            {
                if (counters) counters->fallbacks += 1;
                return false;
            }
            operands[operand_index] = machine_x64_exact_gpr_operand(operand_registers[operand_slot], (u16)(size * 8u));
        }
        break;
        case MACHINE_X64_EXACT_OPERAND_ZMM_SLOT:
            if (operand_registers[operand_slot] < MACHINE_X64_ZMM0 || operand_registers[operand_slot] >= MACHINE_X64_ZMM0 + 32)
            {
                if (counters) counters->fallbacks += 1;
                return false;
            }
            operands[operand_index] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                .width = width,
                .reg = {.index = (u16)(operand_registers[operand_slot] - MACHINE_X64_ZMM0), .width = width,
                        .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM},
            };
            break;
        case MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1:
            operands[operand_index] = (BusterX86MetadataPhysicalOperand){
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                .width = 64,
                .reg = {.index = 1, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK},
            };
            break;
        case MACHINE_X64_EXACT_OPERAND_FIXED_RSP:
            operands[operand_index] = machine_x64_exact_gpr_operand(MACHINE_X64_RSP, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD:
            // MOV's B8 forms are unsigned (UIMMv), while its C7 form and the
            // arithmetic immediate forms are signed.  Preserve the complete
            // u64 payload for the unsigned variants; truncating through s32
            // would reject values such as 0xffffffff and all movabs bits.
            if (variant.key.form_id == MACHINE_X64_MOV_IMMEDIATE_EXACT_FORM_ID)
            {
                operands[operand_index] = machine_x64_exact_unsigned_immediate_operand(immediate_value, width);
            }
            else if (variant.key.form_id == MACHINE_X64_VPSLLD_EXACT_FORM_ID || variant.key.form_id == MACHINE_X64_VPTERNLOGD_EXACT_FORM_ID ||
                     variant.key.form_id == MACHINE_X64_SUB_RSP_EXACT_FORM_ID)
            {
                // These rows carry their immediate directly in the machine
                // payload rather than through a MACHINE_REF_IMMEDIATE slot.
                operands[operand_index] = machine_x64_exact_immediate_operand((s64)(s32)payload, width);
            }
            else
            {
                operands[operand_index] = machine_x64_exact_immediate_operand((s64)(s32)immediate_value, width);
            }
            break;
        case MACHINE_X64_EXACT_OPERAND_IMMEDIATE_CONSTANT:
            operands[operand_index] = machine_x64_exact_unsigned_immediate_operand(payload, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_IMMEDIATE_LESS:
            operands[operand_index] = machine_x64_exact_unsigned_immediate_operand(1, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_RELATIVE_ZERO:
            operands[operand_index] = machine_x64_exact_relative_operand(0, width);
            break;
        case MACHINE_X64_EXACT_OPERAND_RIP_MEMORY_ZERO:
            operands[operand_index] = machine_x64_exact_rip_memory_operand();
            break;
        case MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO:
            operands[operand_index] = machine_x64_exact_memory_operand(operand_registers[operand_slot], width, 0, false);
            break;
        case MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD_SIZE:
        {
            u32 size = payload & 0xffu;
            if (size != 1 && size != 2 && size != 4 && size != 8)
            {
                if (counters) counters->fallbacks += 1;
                return false;
            }
            operands[operand_index] = machine_x64_exact_memory_operand(operand_registers[operand_slot], (u16)(size * 8u), 0, false);
        }
        break;
        case MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD:
            operands[operand_index] = machine_x64_exact_memory_operand(operand_registers[operand_slot], width, (s64)(s32)payload, false);
            break;
        case MACHINE_X64_EXACT_OPERAND_RBP_MEMORY_PAYLOAD:
            // Incoming arguments: past the saved frame pointer and return
            // address, plus whatever the prologue pushed below the frame
            // pointer before establishing it.
            operands[operand_index] =
                machine_x64_exact_rbp_memory_operand(16u + (placement ? placement->incoming_base : 0u) + payload, width, force_disp32);
            break;
        case MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_PAYLOAD:
            if (!instruction || !placement || operand_slot >= 4 || machine_ref_kind(instruction->operands[operand_slot]) != MACHINE_REF_STACK_SLOT)
            {
                if (counters) counters->attempts += 1;
                if (counters) counters->fallbacks += 1;
                return false;
            }
            {
                u32 offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[operand_slot])] - payload;
                operands[operand_index] = machine_x64_exact_rbp_memory_operand(-(s64)(s32)offset, width, force_disp32);
            }
            break;
        case MACHINE_X64_EXACT_OPERAND_RBP_FRAME_MEMORY_SLOT:
            if (!instruction || !placement || operand_slot >= 4 || machine_ref_kind(instruction->operands[operand_slot]) != MACHINE_REF_STACK_SLOT)
            {
                if (counters) counters->attempts += 1;
                if (counters) counters->fallbacks += 1;
                return false;
            }
            {
                u32 offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[operand_slot])];
                operands[operand_index] = machine_x64_exact_rbp_memory_operand(-(s64)(s32)offset, width, true);
            }
            break;
        default:
            if (counters) counters->attempts += 1;
            if (counters) counters->fallbacks += 1;
            return false;
        }
    }
    return machine_x64_emit_exact_form(encoder, entry->metadata_tokens[variant_index], operands, variant.operand_count, force_disp32, false, 0, false, counters);
}

// Allocator copies and rematerializations are not a separate encoding
// population.  Feed them through the same prepared recipe lane as ordinary
// machine rows so they consume its dense register tables and patch kernels.
BUSTER_GLOBAL_LOCAL bool machine_x64_emit_exact_register_copy(MachineX64Encoder* encoder, u32 destination, u32 source,
                                                              MachineX64ExactEmitCounters* counters)
{
    if (destination >= 16 || source >= 16) return machine_x64_exact_reject(encoder, counters);
    u8 operand_registers[4] = {(u8)destination, (u8)source};
    return machine_x64_emit_exact_recipe(
        encoder, machine_x64_exact_opcode_for_opcode(MACHINE_X64_MOV_RR), 0, 0,
        operand_registers, 0, 0, true, counters);
}

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_exact_immediate_value(MachineX64Encoder* encoder, u32 reg, u64 value,
                                                                MachineX64ExactEmitCounters* counters)
{
    if (reg >= 16) return machine_x64_exact_reject(encoder, counters);
    u8 operand_registers[4] = {(u8)reg};
    return machine_x64_emit_exact_recipe(
        encoder, machine_x64_exact_opcode_for_opcode(MACHINE_X64_MOV_RI), 0, 0,
        operand_registers, 0, value, false, counters);
}

typedef struct MachineX64BranchFixup MachineX64BranchFixup;
struct MachineX64BranchFixup
{
    u32 patch_offset;
    u32 block;
    bool label_address;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL bool machine_x64_emit_exact_sequence(MachineX64Encoder* encoder, MachineX64PreparedExactOpcode const* entry,
                                                         MachineInstruction const* instruction, MachineStackPlacement const* placement,
                                                         u8 const* operand_registers, u32 payload, u64 immediate_value,
                                                         MachineX64ExactEmitCounters* counters, Arena* arena, MachineBuilderStream* fixups)
{
    MachineX64ExactSequence const* sequence = entry ? entry->sequence : 0;
    if (!sequence || !entry->plan_valid || sequence->variant_count == 0 || entry->variant_count != sequence->variant_count ||
        !entry->variant_valid_mask)
    {
        if (counters) { counters->attempts += 1; counters->fallbacks += 1; }
        return false;
    }
    // Sequence variants are selected by the family payload where a row has
    // multiple canonical forms.  The first variant is the fixed/default path;
    // callers can extend this switch without changing MachineInstruction.
    u32 variant_index = 0;
    if (sequence->recipe == MACHINE_EMIT_RECIPE_FAMILY_BASE + 39 ||
        sequence->recipe == MACHINE_EMIT_RECIPE_FAMILY_BASE + 42)
    {
        variant_index = payload < sequence->variant_count ? payload : UINT32_MAX;
    }
    else if (sequence->recipe == MACHINE_EMIT_RECIPE_FAMILY_BASE + 44)
    {
        u32 operation = payload & 0xffu;
        u32 operation_index = operation == 0x58u ? 0 : operation == 0x5cu ? 1 : operation == 0x59u ? 2 : operation == 0x5eu ? 3 : UINT32_MAX;
        variant_index = operation_index == UINT32_MAX ? UINT32_MAX : operation_index + ((payload & 0x100u) ? 4u : 0u);
    }
    else if (sequence->recipe == MACHINE_EMIT_RECIPE_FAMILY_BASE + 45)
    {
        u32 condition = payload & 0xfu;
        u32 condition_index = condition == 4 ? 0 : condition == 5 ? 1 : condition == 2 ? 2 : condition == 6 ? 3 : condition == 7 ? 4 : condition == 3 ? 5 : UINT32_MAX;
        variant_index = condition_index == UINT32_MAX ? UINT32_MAX : condition_index + ((payload & 0x100u) ? 6u : 0u);
    }
    else if (sequence->recipe == MACHINE_EMIT_RECIPE_FAMILY_BASE + 43)
    {
        u32 quarter = payload & 0xffu;
        variant_index = quarter < sequence->variant_count ? quarter : UINT32_MAX;
    }
    else if (sequence->recipe == MACHINE_EMIT_RECIPE_FAMILY_BASE + 47 ||
             sequence->recipe == MACHINE_EMIT_RECIPE_FAMILY_BASE + 48)
    {
        variant_index = (payload & 0xfu) < sequence->variant_count ? payload & 0xfu : UINT32_MAX;
    }
    if (variant_index >= sequence->variant_count || !(entry->variant_valid_mask & (u16)(1u << variant_index)))
    {
        if (counters) { counters->attempts += 1; counters->fallbacks += 1; }
        return false;
    }
    MachineX64ExactSequenceVariant const* variant = sequence->variants + variant_index;
    u32 sequence_start = encoder->count;
    u32 parity_mode = (payload >> 9) & 0x3u;
    for (u32 step_index = 0; step_index < variant->step_count; step_index += 1)
    {
        MachineX64ExactSequenceStep const* step = variant->steps + step_index;
        if ((step->flags & MACHINE_X64_EXACT_RECIPE_FLAG_PARITY_ONLY) && parity_mode == 0)
        {
            continue;
        }
        BusterX86MetadataPhysicalOperand operands[4];
        bool force_disp32 = (step->flags & MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_DISP32) != 0;
        bool step_valid = true;
        for (u32 operand_index = 0; operand_index < step->operand_count; operand_index += 1)
        {
            u8 operand_slot = step->operand_slots[operand_index];
            u16 width = step->operand_widths[operand_index];
            switch ((MachineX64ExactOperandProjection)step->operand_kinds[operand_index])
            {
            case MACHINE_X64_EXACT_OPERAND_GPR:
                operands[operand_index] = machine_x64_exact_gpr_operand(operand_registers[operand_slot], width);
                break;
            case MACHINE_X64_EXACT_OPERAND_XMM_PAYLOAD:
                operands[operand_index] = machine_x64_exact_xmm_operand(payload, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_XMM_FIXED0:
                operands[operand_index] = machine_x64_exact_xmm_operand(0, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_XMM_FIXED1:
                operands[operand_index] = machine_x64_exact_xmm_operand(1, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_XMM_SLOT:
                step_valid = operand_registers[operand_slot] >= MACHINE_X64_ZMM0 && operand_registers[operand_slot] < MACHINE_X64_ZMM0 + 32;
                if (step_valid) operands[operand_index] = machine_x64_exact_xmm_operand(operand_registers[operand_slot] - MACHINE_X64_ZMM0, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RAX:
                operands[operand_index] = machine_x64_exact_gpr_operand(MACHINE_X64_RAX, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_GPR_FIXED_RDX:
                operands[operand_index] = machine_x64_exact_gpr_operand(MACHINE_X64_RDX, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_GPR_PAYLOAD_SIZE:
            {
                u32 size = payload & 0xffu;
                step_valid = size == 1 || size == 2 || size == 4 || size == 8;
                if (step_valid) operands[operand_index] = machine_x64_exact_gpr_operand(operand_registers[operand_slot], (u16)(size * 8u));
            }
            break;
            case MACHINE_X64_EXACT_OPERAND_ZMM_SLOT:
                step_valid = operand_registers[operand_slot] >= MACHINE_X64_ZMM0 && operand_registers[operand_slot] < MACHINE_X64_ZMM0 + 32;
                if (step_valid)
                {
                    operands[operand_index] = (BusterX86MetadataPhysicalOperand){
                        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = width,
                        .reg = {.index = (u16)(operand_registers[operand_slot] - MACHINE_X64_ZMM0), .width = width,
                                .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM}};
                }
                break;
            case MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K1:
                operands[operand_index] = (BusterX86MetadataPhysicalOperand){
                    .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 64,
                    .reg = {.index = 1, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK}};
                break;
            case MACHINE_X64_EXACT_OPERAND_MASK_FIXED_K0:
                operands[operand_index] = (BusterX86MetadataPhysicalOperand){
                    .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER, .width = 64,
                    .reg = {.index = 0, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK}};
                break;
            case MACHINE_X64_EXACT_OPERAND_FIXED_RSP:
                operands[operand_index] = machine_x64_exact_gpr_operand(MACHINE_X64_RSP, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_IMMEDIATE_PAYLOAD:
                operands[operand_index] = machine_x64_exact_immediate_operand((s64)(s32)payload, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_IMMEDIATE_CONSTANT:
                operands[operand_index] = machine_x64_exact_unsigned_immediate_operand(payload, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_IMMEDIATE_LESS:
                operands[operand_index] = machine_x64_exact_unsigned_immediate_operand(1, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_RELATIVE_ZERO:
                operands[operand_index] = machine_x64_exact_relative_operand(0, width);
                break;
            case MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_ZERO:
                operands[operand_index] = machine_x64_exact_memory_operand(operand_registers[operand_slot], width, 0, false);
                break;
            case MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD_SIZE:
            {
                u32 size = payload & 0xffu;
                step_valid = size == 1 || size == 2 || size == 4 || size == 8;
                if (step_valid) operands[operand_index] = machine_x64_exact_memory_operand(operand_registers[operand_slot], (u16)(size * 8u), 0, false);
            }
            break;
            case MACHINE_X64_EXACT_OPERAND_MEMORY_BASE_PAYLOAD:
                operands[operand_index] = machine_x64_exact_memory_operand(operand_registers[operand_slot], width, (s64)(s32)payload, false);
                break;
            default:
                step_valid = false;
                break;
            }
            if (!step_valid) break;
        }
        BusterX86MetadataMachineExactToken step_token =
            entry->sequence_tokens[variant_index * MACHINE_X64_EXACT_SEQUENCE_MAX_STEPS + step_index];
        if (sequence->recipe == MACHINE_EMIT_RECIPE_FAMILY_BASE + 45 &&
            (step->flags & MACHINE_X64_EXACT_RECIPE_FLAG_PARITY_ONLY) && parity_mode != 1)
        {
            step_valid = step_valid && machine_x64_fcmp_alternate_tokens_valid;
            if (step_valid)
            {
                step_token = (step->flags & MACHINE_X64_EXACT_RECIPE_FLAG_PARITY_OR) ? machine_x64_fcmp_or_token : machine_x64_fcmp_setp_token;
            }
        }
        u32 step_start = encoder->count;
        if (!step_valid || !machine_x64_emit_exact_form(
                               encoder,
                               step_token,
                               operands, step->operand_count, force_disp32,
                               (step->flags & MACHINE_X64_EXACT_RECIPE_FLAG_FORCE_LOCK) != 0,
                               step->mask_register_plus_one, step->zeroing, counters))
        {
            encoder->count = sequence_start;
            if (counters && step_valid == false) counters->fallbacks += 1;
            return false;
        }
        if (step->flags & MACHINE_X64_EXACT_RECIPE_FLAG_BRANCH_FIXUP)
        {
            u8 branch_slot = step->operand_slots[0];
            if (!instruction || branch_slot >= 4 || machine_ref_kind(instruction->operands[branch_slot]) != MACHINE_REF_BLOCK)
            {
                encoder->count = sequence_start;
                if (counters) counters->fallbacks += 1;
                return false;
            }
            MachineX64BranchFixup* fixup = (MachineX64BranchFixup*)machine_stream_append(arena, fixups);
            *fixup = (MachineX64BranchFixup){
                .patch_offset = step_start + (step->key.form_id == MACHINE_X64_JMP_EXACT_FORM_ID ? 1u : 2u),
                .block = machine_ref_payload(instruction->operands[branch_slot]),
            };
        }
    }
    (void)placement;
    (void)immediate_value;
    return true;
}

// Sized chunk moves shared by the aggregate copy loops, chunked 8/4/2/1
// exactly like the canonical copy code: narrow loads zero-extend through
// movzx, narrow stores write their exact width.
BUSTER_GLOBAL_LOCAL u32 machine_x64_copy_chunk(u64 remaining)
{
    return remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
}

// Allocation edits are already a compact point-sorted command stream. Keep
// their interpretation outside the machine-row dispatcher so the common row
// path does not duplicate spill/reload/copy/rematerialization policy around
// every instruction.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 machine_x64_emit_edit_run(MachineX64Encoder* encoder, MachineFunction* function,
                                                                MachineStackPlacement* placement, u32 edit_cursor,
                                                                MachinePoint point, bool default_store)
{
    while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == point)
    {
        MachineEdit* edit = placement->edits + edit_cursor;
        bool vector_location = edit->location >= MACHINE_X64_ZMM0;
        if (edit->kind == MACHINE_EDIT_COPY)
        {
            if (vector_location)
            {
                (void)machine_x64_emit_metadata_zmm_registers(
                    encoder, S8("VMOVDQU8"), edit->location - MACHINE_X64_ZMM0, edit->subject - MACHINE_X64_ZMM0, 512,
                    (BusterX86MetadataFeatureInput){.names = machine_x64_avx512_features,
                                                   .count = BUSTER_ARRAY_LENGTH(machine_x64_avx512_features)},
                    0);
            }
            else
            {
                (void)machine_x64_emit_exact_register_copy(encoder, edit->location, edit->subject, 0);
            }
        }
        else if (edit->kind == MACHINE_EDIT_REMATERIALIZE)
        {
            (void)machine_x64_emit_exact_immediate_value(encoder, edit->location, function->immediates[edit->subject], 0);
        }
        else
        {
            // Allocators emit only SPILL/RELOAD here. Preserve the previous
            // phase-specific fallback for malformed internal edit streams.
            bool store = edit->kind == MACHINE_EDIT_SPILL || (edit->kind != MACHINE_EDIT_RELOAD && default_store);
            if (vector_location)
            {
                (void)machine_x64_emit_metadata_zmm_memory(
                    encoder, S8("VMOVDQU8"), edit->location - MACHINE_X64_ZMM0, MACHINE_X64_RBP,
                    -(s64)(s32)placement->virtual_register_offsets[edit->subject], store, 512, 8,
                    (BusterX86MetadataFeatureInput){.names = machine_x64_avx512_features,
                                                   .count = BUSTER_ARRAY_LENGTH(machine_x64_avx512_features)},
                    0);
            }
            else
            {
                (void)machine_x64_emit_exact_frame_chunk(
                    encoder, !store, edit->location, placement->virtual_register_offsets[edit->subject], 8, 0);
            }
        }
        edit_cursor += 1;
    }
    return edit_cursor;
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
    // The fixed frame is reserved in the prologue, independently of any
    // MIR STACK_ALLOCATE row.  Each touched page emits a SUB plus a probe, so
    // account for that bounded prefix even for small functions with a large
    // spill frame; otherwise a valid placement can truncate before its first
    // machine row.
    u64 frame_probe_chunks = ((u64)placement->frame_size + 4095u) / 4096u;
    capacity64 += frame_probe_chunks * 24u;
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
    bool saves_first = function->target && function->target->saves_precede_frame_pointer;
    (void)machine_x64_emit_metadata_register(&encoder, S8("PUSH"), MACHINE_X64_RBP, 64, 0);
    if (!saves_first)
    {
        (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_RBP, MACHINE_X64_RSP, 64, 0);
    }
    // Ascending register order, which is RBX, R12-R15 under System V and
    // gains RSI and RDI under Win64; the unwind actions walk the same mask
    // the same way, so their offsets stay exact under either file.
    for (u32 push_register = 0; push_register < MACHINE_X64_ZMM0; push_register += 1)
    {
        if (placement->callee_saved_mask & (1ull << push_register))
        {
            (void)machine_x64_emit_metadata_register(&encoder, S8("PUSH"), push_register, 64, 0);
        }
    }
    if (saves_first)
    {
        // The frame pointer lands below the saves, so they sit at its
        // positive offsets and the epilogue recovers RSP with a plain move.
        (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_RBP, MACHINE_X64_RSP, 64, 0);
    }
    // The stack allocation mirrors the canonical chunked form: at most a
    // page per subtract with a probe touch after each, so a frame larger
    // than the guard page cannot skip it.
    u32 frame_remaining = placement->frame_size;
    while (frame_remaining)
    {
        u32 frame_chunk = BUSTER_MIN(frame_remaining, 4096u);
        (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("SUB"), MACHINE_X64_RSP, frame_chunk, 64,
                                                           frame_chunk <= INT8_MAX ? 8 : 32, 0);
        (void)machine_x64_emit_metadata_memory_immediate(&encoder, S8("TEST"), MACHINE_X64_RSP, 0, 0, 8, 8, 0);
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
            if (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == before)
            {
                edit_cursor = machine_x64_emit_edit_run(&encoder, function, placement, edit_cursor, before, false);
            }
            MachineX64PreparedExactOpcode const* exact_entry = machine_x64_exact_opcode_for_opcode(instruction->opcode);
            bool exact_required = exact_entry && exact_entry->exact_required;
            if (exact_required)
            {
                u32 exact_start = encoder.count;
                u64 exact_immediate = 0;
                bool exact_immediate_valid = true;
                if (instruction->opcode == MACHINE_X64_MOV_RI || instruction->opcode == MACHINE_X64_ADD64_IMM || instruction->opcode == MACHINE_X64_IMUL64_RRI)
                {
                    u32 immediate_operand = instruction->opcode == MACHINE_X64_IMUL64_RRI ? 2u : 1u;
                    if (machine_ref_kind(instruction->operands[immediate_operand]) == MACHINE_REF_IMMEDIATE &&
                        machine_ref_payload(instruction->operands[immediate_operand]) < function->immediate_count)
                    {
                        exact_immediate = function->immediates[machine_ref_payload(instruction->operands[immediate_operand])];
                    }
                    else
                    {
                        exact_immediate_valid = false;
                    }
                }
                else if (instruction->opcode == MACHINE_X64_ADD_RSP)
                {
                    // ADD_RSP carries its immediate directly in the row
                    // payload (there is no MACHINE_REF_IMMEDIATE operand).
                    exact_immediate = instruction->payload;
                }
                bool exact_emitted = false;
                if (!exact_immediate_valid)
                {
                    exact_counters.attempts += 1;
                    exact_counters.fallbacks += 1;
                }
                else
                {
                    exact_emitted = exact_entry->sequence_required
                                         ? machine_x64_emit_exact_sequence(&encoder, exact_entry, instruction, placement, operand_registers,
                                                                           instruction->payload, exact_immediate, &exact_counters, arena, &fixups)
                                         : machine_x64_emit_exact_recipe(&encoder, exact_entry, instruction, placement, operand_registers,
                                                                         instruction->payload, exact_immediate, false, &exact_counters);
                }
                if (!exact_entry)
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
                else if (exact_entry->descriptor)
                {
                    // Relative and symbolic forms deliberately query the
                    // metadata encoder with a neutral zero displacement. The
                    // existing machine fixup/call-site streams remain the
                    // owner of target resolution, preserving their canonical
                    // offsets and relocation semantics.
                    if (exact_entry->descriptor->flags & MACHINE_X64_EXACT_RECIPE_FLAG_BRANCH_FIXUP)
                    {
                        MachineX64BranchFixup* fixup = (MachineX64BranchFixup*)machine_stream_append(arena, &fixups);
                        *fixup = (MachineX64BranchFixup){
                            .patch_offset = exact_start + 1,
                            .block = machine_ref_payload(instruction->operands[0]),
                        };
                    }
                    if (exact_entry->descriptor->flags & MACHINE_X64_EXACT_RECIPE_FLAG_CALL_SITE)
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
                            (void)machine_x64_emit_metadata_instruction(&encoder, wide ? S8("CQO") : S8("CDQ"), 0, 0,
                                    (BusterX86MetadataFeatureInput){0}, (BusterX86MetadataPhysicalAttributes){0}, 0);
                        }
                        else
                        {
                            (void)machine_x64_emit_metadata_registers(&encoder, S8("XOR"), MACHINE_X64_RDX, MACHINE_X64_RDX, wide ? 64 : 32, 0);
                        }
                        (void)machine_x64_emit_metadata_register(&encoder, is_signed ? S8("IDIV") : S8("DIV"), MACHINE_X64_RCX, wide ? 64 : 32, 0);
                        if (remainder)
                        {
                            (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_RAX, MACHINE_X64_RDX, wide ? 64 : 32, 0);
                        }
                    }
                    break; case MACHINE_X64_RET:
                    {
                        // No vzeroupper over a live 512-bit return: it would zero
                        // bits 128+ of the ZMM0 the caller is about to read.
                        if (function_has_vector && !(instruction->flags & MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE))
                        {
                            (void)machine_x64_emit_metadata_instruction(&encoder, S8("VZEROUPPER"), 0, 0,
                                    (BusterX86MetadataFeatureInput){.names = machine_x64_avx_features,
                                    .count = BUSTER_ARRAY_LENGTH(machine_x64_avx_features)},
                                    (BusterX86MetadataPhysicalAttributes){0}, 0);
                        }
                        if (placement->callee_saved_mask)
                        {
                            // Point RSP at the pushed registers, restore them in
                            // reverse push order, then unwind the frame base. Where
                            // the saves precede the frame pointer they already sit
                            // at it, so a plain move is the whole adjustment.
                            u32 push_count = 0;
                            for (u32 push_register = 0; push_register < MACHINE_X64_REGISTER_COUNT; push_register += 1)
                            {
                                push_count += (placement->callee_saved_mask >> push_register) & 1u;
                            }
                            if (function->target && function->target->saves_precede_frame_pointer)
                            {
                                (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_RSP, MACHINE_X64_RBP, 64, 0);
                            }
                            else
                            {
                                (void)machine_x64_emit_metadata_register_memory(&encoder, S8("LEA"), MACHINE_X64_RSP, MACHINE_X64_RBP,
                                        -(s64)(8u * push_count), 64, 64, 0);
                            }
                            for (u32 pop_reverse = MACHINE_X64_ZMM0; pop_reverse > 0; pop_reverse -= 1)
                            {
                                u32 pop_register = pop_reverse - 1;
                                if (placement->callee_saved_mask & (1ull << pop_register))
                                {
                                    (void)machine_x64_emit_metadata_register(&encoder, S8("POP"), pop_register, 64, 0);
                                }
                            }
                        }
                        else
                        {
                            (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_RSP, MACHINE_X64_RBP, 64, 0);
                        }
                        (void)machine_x64_emit_metadata_register(&encoder, S8("POP"), MACHINE_X64_RBP, 64, 0);
                        (void)machine_x64_emit_metadata_instruction(&encoder, S8("RET"), 0, 0,
                                (BusterX86MetadataFeatureInput){0}, (BusterX86MetadataPhysicalAttributes){0}, 0);
                    }
                    break; case MACHINE_X64_CALL_DIRECT:
                    {
                        // No vzeroupper over staged ZMM arguments: the callee reads
                        // them whole, exactly like the canonical path, whose lazy
                        // vzeroupper fires before its call staging rather than after.
                        if (function_has_vector && !(instruction->flags & MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE))
                        {
                            (void)machine_x64_emit_metadata_instruction(&encoder, S8("VZEROUPPER"), 0, 0,
                                    (BusterX86MetadataFeatureInput){.names = machine_x64_avx_features,
                                    .count = BUSTER_ARRAY_LENGTH(machine_x64_avx_features)},
                                    (BusterX86MetadataPhysicalAttributes){0}, 0);
                        }
                        if (instruction->flags & 1)
                        {
                            // Variadic System V call: AL carries the count of XMM
                            // registers holding arguments.
                            (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("MOV"), MACHINE_X64_RAX,
                                    instruction->flags >> 1, 32, 32, 0);
                        }
                        u32 call_start = encoder.count;
                        (void)machine_x64_emit_metadata_relative(&encoder, S8("CALL"), 0, 32, 0);
                        MachineCallSite* site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                        *site = (MachineCallSite){
                            .code_offset = call_start + 1,
                            .target = instruction->payload,
                        };
                    }
                    break; case MACHINE_X64_CALL_INDIRECT:
                    {
                        if (function_has_vector && !(instruction->flags & MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE))
                        {
                            (void)machine_x64_emit_metadata_instruction(&encoder, S8("VZEROUPPER"), 0, 0,
                                    (BusterX86MetadataFeatureInput){.names = machine_x64_avx_features,
                                    .count = BUSTER_ARRAY_LENGTH(machine_x64_avx_features)},
                                    (BusterX86MetadataPhysicalAttributes){0}, 0);
                        }
                        if (instruction->flags & 1)
                        {
                            (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("MOV"), MACHINE_X64_RAX,
                                    instruction->flags >> 1, 32, 32, 0);
                        }
                        (void)machine_x64_emit_metadata_register(&encoder, S8("CALL"), operand_registers[0], 64, 0);
                    }
                    break; case MACHINE_X64_LEA_BLOCK:
                    {
                        u32 branch_start = encoder.count;
                        u32 destination = operand_registers[0];
                        BusterX86MetadataPhysicalOperand operands[2] = {
                            machine_x64_exact_gpr_operand(destination, 64), machine_x64_exact_rip_memory_operand(),
                        };
                        (void)machine_x64_emit_metadata_instruction(&encoder, S8("LEA"), operands, 2,
                                                                     (BusterX86MetadataFeatureInput){0},
                                                                     (BusterX86MetadataPhysicalAttributes){0}, 0);
                        MachineX64BranchFixup* fixup = (MachineX64BranchFixup*)machine_stream_append(arena, &fixups);
                        *fixup = (MachineX64BranchFixup){
                            .patch_offset = branch_start + 3,
                            .block = instruction->payload,
                            .label_address = true,
                        };
                    }
                    break; case MACHINE_X64_INDIRECT_BRANCH:
                    {
                        (void)machine_x64_emit_metadata_register(&encoder, S8("JMP"), operand_registers[0], 64, 0);
                    }
                    break; case MACHINE_X64_LEA_TLS:
                    {
                        // mov dest, fs:[0] — the thread pointer — then
                        // lea dest, [dest + tpoff] with the displacement patched
                        // thread-locally, byte-for-byte the canonical sequence.
                        u32 destination = operand_registers[0];
                        BusterX86MetadataPhysicalOperand fs_operands[2] = {
                            machine_x64_exact_gpr_operand(destination, 64),
                            {
                                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
                                .width = 64,
                                .memory = {
                                    .displacement = 0,
                                    .address_size = 64,
                                    .scale = 1,
                                    .segment = BUSTER_X86_METADATA_SEGMENT_FS,
                                    .has_displacement = true,
                                    .has_segment = true,
                                },
                            },
                        };
                        (void)machine_x64_emit_metadata_instruction(&encoder, S8("MOV"), fs_operands, 2,
                                (BusterX86MetadataFeatureInput){0}, (BusterX86MetadataPhysicalAttributes){0}, 0);
                        MachineX64PreparedExactOpcode const* lea_entry = machine_x64_exact_opcode_for_opcode(MACHINE_X64_LEA_FRAME);
                        BusterX86MetadataPhysicalOperand lea_operands[2] = {
                            machine_x64_exact_gpr_operand(destination, 64),
                            machine_x64_exact_memory_operand(destination, 64, 0, true),
                        };
                        if (!lea_entry || !lea_entry->descriptor || !lea_entry->plan_valid ||
                                !machine_x64_emit_exact_form(&encoder, lea_entry->metadata_tokens[0], lea_operands, 2, true, false, 0, false, 0))
                        {
                            encoder.overflow = true;
                        }
                        MachineCallSite* site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                        *site = (MachineCallSite){
                            .code_offset = encoder.count - 4,
                            .target = instruction->payload,
                            .is_thread_local = 1,
                            .thread_local_site = MACHINE_THREAD_LOCAL_SITE_LOCAL_EXEC,
                        };
                    }
                    break; case MACHINE_X64_LEA_TLS_INITIAL_EXEC:
                    {
                        // mov dest, fs:[0] — the thread pointer — then
                        // add dest, [rip + sym@GOTTPOFF], the word the loader
                        // wrote with this symbol's offset from it.
                        u32 destination = operand_registers[0];
                        BusterX86MetadataPhysicalOperand fs_operands[2] = {
                            machine_x64_exact_gpr_operand(destination, 64),
                            {
                                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
                                .width = 64,
                                .memory = {
                                    .displacement = 0,
                                    .address_size = 64,
                                    .scale = 1,
                                    .segment = BUSTER_X86_METADATA_SEGMENT_FS,
                                    .has_displacement = true,
                                    .has_segment = true,
                                },
                            },
                        };
                        (void)machine_x64_emit_metadata_instruction(&encoder, S8("MOV"), fs_operands, 2,
                                (BusterX86MetadataFeatureInput){0}, (BusterX86MetadataPhysicalAttributes){0}, 0);
                        BusterX86MetadataPhysicalOperand add_operands[2] = {
                            machine_x64_exact_gpr_operand(destination, 64),
                            machine_x64_exact_rip_memory_operand(),
                        };
                        (void)machine_x64_emit_metadata_instruction(&encoder, S8("ADD"), add_operands, 2,
                                (BusterX86MetadataFeatureInput){0}, (BusterX86MetadataPhysicalAttributes){0}, 0);
                        MachineCallSite* site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                        *site = (MachineCallSite){
                            .code_offset = encoder.count - 4,
                            .target = instruction->payload,
                            .is_thread_local = 1,
                            .thread_local_site = MACHINE_THREAD_LOCAL_SITE_INITIAL_EXEC,
                        };
                    }
                    break; case MACHINE_X64_TLS_GENERAL_DYNAMIC:
                    {
                        // Sixteen bytes whose prefixes are the sequence rather
                        // than an encoding choice: a linker relaxing
                        // general-dynamic to a cheaper model matches on
                        // exactly these bytes and overwrites all sixteen, so
                        // they are written directly instead of through the
                        // encoder, which would pick each instruction's
                        // shortest form.
                        //   66 48 8d 3d <r32>  data16 lea rdi, [rip + sym@TLSGD]
                        //   66 66 48 e8 <r32>  data16 data16 rex.W call __tls_get_addr
                        static u8 const general_dynamic_bytes[] = {0x66, 0x48, 0x8d, 0x3d, 0, 0, 0, 0, 0x66, 0x66, 0x48, 0xe8, 0, 0, 0, 0};
                        u32 sequence_offset = encoder.count;
                        (void)machine_x64_emit_literal_bytes(&encoder, general_dynamic_bytes, (u32)BUSTER_ARRAY_LENGTH(general_dynamic_bytes));
                        MachineCallSite* address_site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                        *address_site = (MachineCallSite){
                            .code_offset = sequence_offset + 4,
                            .target = instruction->payload,
                            .is_thread_local = 1,
                            .thread_local_site = MACHINE_THREAD_LOCAL_SITE_GENERAL_DYNAMIC,
                        };
                        MachineCallSite* helper_site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                        *helper_site = (MachineCallSite){
                            .code_offset = sequence_offset + 12,
                            .target = instruction->payload,
                            .thread_local_site = MACHINE_THREAD_LOCAL_SITE_TLS_GET_ADDR,
                        };
                    }
                    break; case MACHINE_X64_COPY_FRAME_FROM_FRAME:
                    {
                        u32 destination_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])];
                        u32 source_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])];
                        u32 copied = 0;
                        while (copied < instruction->payload)
                        {
                            u32 chunk = machine_x64_copy_chunk(instruction->payload - copied);
                            (void)machine_x64_emit_exact_frame_chunk(&encoder, true, MACHINE_X64_RAX, source_offset - copied, chunk, 0);
                            (void)machine_x64_emit_exact_frame_chunk(&encoder, false, MACHINE_X64_RAX, destination_offset - copied, chunk, 0);
                            copied += chunk;
                        }
                    }
                    break; case MACHINE_X64_COPY_FRAME_FROM_PTR:
                    {
                        u32 destination_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])];
                        u32 source_register = operand_registers[1];
                        u32 copied = 0;
                        while (copied < instruction->payload)
                        {
                            u32 chunk = machine_x64_copy_chunk(instruction->payload - copied);
                            (void)machine_x64_emit_metadata_pointer_chunk(&encoder, true, MACHINE_X64_RAX, source_register, copied, chunk, 0);
                            (void)machine_x64_emit_exact_frame_chunk(&encoder, false, MACHINE_X64_RAX, destination_offset - copied, chunk, 0);
                            copied += chunk;
                        }
                    }
                    break; case MACHINE_X64_COPY_PTR_FROM_FRAME:
                    {
                        u32 destination_register = operand_registers[0];
                        u32 source_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])];
                        u32 copied = 0;
                        while (copied < instruction->payload)
                        {
                            u32 chunk = machine_x64_copy_chunk(instruction->payload - copied);
                            (void)machine_x64_emit_exact_frame_chunk(&encoder, true, MACHINE_X64_RDX, source_offset - copied, chunk, 0);
                            (void)machine_x64_emit_metadata_pointer_chunk(&encoder, false, MACHINE_X64_RDX, destination_register, copied, chunk, 0);
                            copied += chunk;
                        }
                    }
                    break; case MACHINE_X64_CVT_U64_TO_F32: case MACHINE_X64_CVT_U64_TO_F64:
                    {
                        // Canonical branchy form: non-negative converts directly;
                        // a set sign bit halves with a sticky rounding bit, then
                        // doubles the float.  The selector leaves the two operands
                        // unconstrained; RCX is the row's declared integer scratch,
                        // while the destination carries the resulting float bits.
                        bool to_f64 = instruction->opcode == MACHINE_X64_CVT_U64_TO_F64;
                        u32 source = operand_registers[1];
                        u32 destination = operand_registers[0];
                        (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_RCX, source, 64, 0);
                        (void)machine_x64_emit_metadata_registers(&encoder, S8("TEST"), source, source, 64, 0);
                        u32 non_negative_branch = encoder.count;
                        (void)machine_x64_emit_metadata_relative(&encoder, S8("JNS"), 0, 8, 0);
                        (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), destination, source, 64, 0);
                        (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("SHR"), destination, 1, 64, 8, 0);
                        (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("AND"), MACHINE_X64_RCX, 1, 32, 8, 0);
                        (void)machine_x64_emit_metadata_registers(&encoder, S8("OR"), destination, MACHINE_X64_RCX, 64, 0);
                        (void)machine_x64_emit_metadata_xmm_gpr(&encoder, to_f64 ? S8("CVTSI2SD") : S8("CVTSI2SS"), 0, destination,
                                to_f64 ? 64 : 32, 64, 0);
                        (void)machine_x64_emit_metadata_xmm_registers(&encoder, to_f64 ? S8("ADDSD") : S8("ADDSS"), 0, 0,
                                to_f64 ? 64 : 32, 0);
                        (void)machine_x64_emit_metadata_gpr_xmm(&encoder, S8("MOVQ"), destination, 0, 64, 128, 0);
                        u32 end_branch = encoder.count;
                        (void)machine_x64_emit_metadata_relative(&encoder, S8("JMP"), 0, 8, 0);
                        u32 direct_conversion = encoder.count;
                        (void)machine_x64_emit_metadata_xmm_gpr(&encoder, to_f64 ? S8("CVTSI2SD") : S8("CVTSI2SS"), 0, source,
                                to_f64 ? 64 : 32, 64, 0);
                        (void)machine_x64_emit_metadata_gpr_xmm(&encoder, S8("MOVQ"), destination, 0, 64, 128, 0);
                        u32 conversion_end = encoder.count;
                        encoder.bytes[non_negative_branch + 1] = (u8)(direct_conversion - (non_negative_branch + 2));
                        encoder.bytes[end_branch + 1] = (u8)(conversion_end - (end_branch + 2));
                    }
                    break; case MACHINE_X64_CVT_F32_TO_U64: case MACHINE_X64_CVT_F64_TO_U64:
                    {
                        // Canonical threshold form: values below 2^63 convert
                        // directly, larger ones subtract the threshold first and
                        // set the top bit after.  The row's operands are free to use
                        // any allocatable GPR; RCX is reserved as the final scratch.
                        bool from_f64 = instruction->opcode == MACHINE_X64_CVT_F64_TO_U64;
                        u32 source = operand_registers[1];
                        u32 destination = operand_registers[0];
                        (void)machine_x64_emit_metadata_xmm_gpr(&encoder, S8("MOVQ"), 0, source, 128, 64, 0);
                        (void)machine_x64_emit_exact_immediate_value(&encoder, destination,
                                from_f64 ? UINT64_C(0x43e0000000000000) : UINT64_C(0x4f000000), 0);
                        (void)machine_x64_emit_metadata_xmm_gpr(&encoder, S8("MOVQ"), 1, destination, 128, 64, 0);
                        (void)machine_x64_emit_metadata_xmm_registers(&encoder, from_f64 ? S8("UCOMISD") : S8("UCOMISS"), 0, 1,
                                from_f64 ? 64 : 32, 0);
                        u32 direct_branch = encoder.count;
                        (void)machine_x64_emit_metadata_relative(&encoder, S8("JB"), 0, 8, 0);
                        (void)machine_x64_emit_metadata_xmm_registers(&encoder, from_f64 ? S8("SUBSD") : S8("SUBSS"), 0, 1,
                                from_f64 ? 64 : 32, 0);
                        (void)machine_x64_emit_metadata_gpr_xmm(&encoder, from_f64 ? S8("CVTTSD2SI") : S8("CVTTSS2SI"), destination, 0, 64,
                                from_f64 ? 64 : 32, 0);
                        // The threshold path already converted x-2^63 to a signed
                        // 64-bit result; restore the unsigned high bit, not merely
                        // bit 31 (the latter is only the f32 result width).
                        (void)machine_x64_emit_exact_immediate_value(&encoder, MACHINE_X64_RCX, UINT64_C(0x8000000000000000), 0);
                        (void)machine_x64_emit_metadata_registers(&encoder, S8("OR"), destination, MACHINE_X64_RCX, 64, 0);
                        u32 end_branch = encoder.count;
                        (void)machine_x64_emit_metadata_relative(&encoder, S8("JMP"), 0, 8, 0);
                        u32 direct_conversion = encoder.count;
                        (void)machine_x64_emit_metadata_gpr_xmm(&encoder, from_f64 ? S8("CVTTSD2SI") : S8("CVTTSS2SI"), destination, 0, 64,
                                from_f64 ? 64 : 32, 0);
                        u32 conversion_end = encoder.count;
                        encoder.bytes[direct_branch + 1] = (u8)(direct_conversion - (direct_branch + 2));
                        encoder.bytes[end_branch + 1] = (u8)(conversion_end - (end_branch + 2));
                    }
                    break; case MACHINE_X64_CVT_F32_TO_I64: case MACHINE_X64_CVT_F64_TO_I64:
                    {
                        bool from_f64 = instruction->opcode == MACHINE_X64_CVT_F64_TO_I64;
                        (void)machine_x64_emit_metadata_xmm_gpr(&encoder, S8("MOVQ"), 0, operand_registers[1], 64, 64, 0);
                        (void)machine_x64_emit_metadata_gpr_xmm(&encoder, from_f64 ? S8("CVTTSD2SI") : S8("CVTTSS2SI"), operand_registers[0], 0, 64, 128, 0);
                    }
                    break; case MACHINE_X64_VA_SAVE:
                    {
                        u32 save_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[0])];
                        static u8 const gp_registers[6] = {
                            MACHINE_X64_RDI, MACHINE_X64_RSI, MACHINE_X64_RDX, MACHINE_X64_RCX, MACHINE_X64_R8, MACHINE_X64_R9,
                        };
                        for (u32 gp_index = 0; gp_index < BUSTER_ARRAY_LENGTH(gp_registers); gp_index += 1)
                        {
                            u32 save_displacement = save_offset - gp_index * 8u;
                            (void)machine_x64_emit_exact_frame_chunk(&encoder, false, gp_registers[gp_index], save_displacement, 8, 0);
                        }
                        for (u32 float_index = 0; float_index < 8; float_index += 1)
                        {
                            u32 xmm = float_index;
                            u32 save_displacement = save_offset - 48u - float_index * 16u;
                            (void)machine_x64_emit_metadata_xmm_memory(&encoder, S8("MOVSD"), xmm, MACHINE_X64_RBP,
                                    -(s64)(s32)save_displacement, true, 64, 0);
                        }
                    }
                    break; case MACHINE_X64_VA_ARG:
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
                                (void)machine_x64_emit_metadata_pointer_chunk(&encoder, true, MACHINE_X64_RDX, list, 0, 4, 0);
                                (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("CMP"), MACHINE_X64_RDX,
                                        48u - integer_parts * 8u, 32, 32, 0);
                                u32 branch_start = encoder.count;
                                (void)machine_x64_emit_metadata_relative(&encoder, S8("JA"), 0, 32, 0);
                                if (overflow_patch_count < BUSTER_ARRAY_LENGTH(overflow_patches))
                                {
                                    overflow_patches[overflow_patch_count++] = branch_start + 2;
                                }
                            }
                            if (float_parts)
                            {
                                (void)machine_x64_emit_metadata_pointer_chunk(&encoder, true, MACHINE_X64_R9, list, 4, 4, 0);
                                (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("CMP"), MACHINE_X64_R9,
                                        176u - float_parts * 16u, 32, 32, 0);
                                u32 branch_start = encoder.count;
                                (void)machine_x64_emit_metadata_relative(&encoder, S8("JA"), 0, 32, 0);
                                if (overflow_patch_count < BUSTER_ARRAY_LENGTH(overflow_patches))
                                {
                                    overflow_patches[overflow_patch_count++] = branch_start + 2;
                                }
                            }
                            (void)machine_x64_emit_metadata_pointer_chunk(&encoder, true, MACHINE_X64_R8, list, 16, 8, 0);
                            for (u32 part_index = 0; part_index < metadata->part_count; part_index += 1)
                            {
                                MachineVaArgPart* part = metadata->parts + part_index;
                                u32 offset_register = part->is_float ? MACHINE_X64_R9 : MACHINE_X64_RDX;
                                // Keep R8 as the immutable reg_save_area base.  Build
                                // each part address in the scratch R11 register so
                                // mixed GP/FP values do not accumulate one class's
                                // cursor into the save-area base.
                                (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_R11, MACHINE_X64_R8, 64, 0);
                                (void)machine_x64_emit_metadata_registers(&encoder, S8("ADD"), MACHINE_X64_R11, offset_register, 64, 0);
                                (void)machine_x64_emit_metadata_pointer_chunk(&encoder, true, MACHINE_X64_R10, MACHINE_X64_R11, part->save_offset,
                                        part->size >= 8 ? 8 : part->size, 0);
                                if (metadata->result_is_frame)
                                {
                                    (void)machine_x64_emit_exact_frame_chunk(&encoder, false, MACHINE_X64_R10, result_offset - part->value_offset,
                                            part->size >= 8 ? 8 : part->size, 0);
                                }
                                else
                                {
                                    // Scalar VA_ARG values are one part and the
                                    // constrained result register is RCX.
                                    (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), result_register, MACHINE_X64_R10,
                                            part->size >= 8 ? 64 : 32, 0);
                                }
                            }
                            if (integer_parts)
                            {
                                u32 amount = integer_parts * 8u;
                                (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("ADD"), MACHINE_X64_RDX, amount, 32,
                                        amount <= INT8_MAX ? 8 : 32, 0);
                                (void)machine_x64_emit_metadata_pointer_chunk(&encoder, false, MACHINE_X64_RDX, list, 0, 4, 0);
                            }
                            if (float_parts)
                            {
                                u32 amount = float_parts * 16u;
                                (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("ADD"), MACHINE_X64_R9, amount, 32,
                                        amount <= INT8_MAX ? 8 : 32, 0);
                                (void)machine_x64_emit_metadata_pointer_chunk(&encoder, false, MACHINE_X64_R9, list, 4, 4, 0);
                            }
                            u32 end_branch_start = encoder.count;
                            (void)machine_x64_emit_metadata_relative(&encoder, S8("JMP"), 0, 32, 0);
                            end_patch = end_branch_start + 1;
                        }
                        u32 overflow_start = encoder.count;
                        (void)machine_x64_emit_metadata_pointer_chunk(&encoder, true, MACHINE_X64_RDX, list, 8, 8, 0);
                        if (metadata->alignment > 8)
                        {
                            (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("ADD"), MACHINE_X64_RDX, metadata->alignment - 1,
                                    64, (metadata->alignment - 1) <= INT8_MAX ? 8 : 32, 0);
                            (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("AND"), MACHINE_X64_RDX,
                                    (u64)(-(s64)metadata->alignment),
                                    64, 32, 0);
                        }
                        if (metadata->result_is_frame)
                        {
                            u32 copied = 0;
                            while (copied < metadata->size)
                            {
                                u32 chunk = machine_x64_copy_chunk(metadata->size - copied);
                                (void)machine_x64_emit_metadata_pointer_chunk(&encoder, true, MACHINE_X64_R10, MACHINE_X64_RDX, copied, chunk, 0);
                                (void)machine_x64_emit_exact_frame_chunk(&encoder, false, MACHINE_X64_R10, result_offset - copied, chunk, 0);
                                copied += chunk;
                            }
                        }
                        else
                        {
                            (void)machine_x64_emit_metadata_pointer_chunk(&encoder, true, result_register, MACHINE_X64_RDX, 0,
                                    metadata->scalar_size >= 8 ? 8 : metadata->scalar_size, 0);
                        }
                        (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("ADD"), MACHINE_X64_RDX, metadata->stack_size,
                                64, metadata->stack_size <= INT8_MAX ? 8 : 32, 0);
                        (void)machine_x64_emit_metadata_pointer_chunk(&encoder, false, MACHINE_X64_RDX, list, 8, 8, 0);
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
                    break; case MACHINE_X64_STACK_ALLOCATE:
                    {
                        // The constrained row places the runtime byte count in RCX
                        // and receives the resulting RSP in RAX. Keep the count
                        // unsigned, round it exactly as the canonical emitter does,
                        // and touch every page before the final residual subtract so
                        // guard-page stacks cannot be skipped.
                        u32 stack_alignment = instruction->payload;
                        (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("ADD"), MACHINE_X64_RCX, stack_alignment - 1, 64, 32, 0);
                        (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("AND"), MACHINE_X64_RCX,
                                (u64)(-(s64)stack_alignment), 64, 32, 0);

                        u32 compare_offset = encoder.count;
                        (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("CMP"), MACHINE_X64_RCX, 4096, 64, 32, 0);
                        u32 short_exit = encoder.count;
                        (void)machine_x64_emit_metadata_relative(&encoder, S8("JB"), 0, 8, 0);
                        (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("SUB"), MACHINE_X64_RSP, 4096, 64, 32, 0);
                        (void)machine_x64_emit_metadata_memory_immediate(&encoder, S8("TEST"), MACHINE_X64_RSP, 0, 0, 8, 8, 0);
                        (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("SUB"), MACHINE_X64_RCX, 4096, 64, 32, 0);
                        u32 loop_back = encoder.count;
                        (void)machine_x64_emit_metadata_relative(&encoder, S8("JMP"), 0, 8, 0);
                        u32 residual = encoder.count;
                        (void)machine_x64_emit_metadata_registers(&encoder, S8("SUB"), MACHINE_X64_RSP, MACHINE_X64_RCX, 64, 0);
                        (void)machine_x64_emit_metadata_memory_immediate(&encoder, S8("TEST"), MACHINE_X64_RSP, 0, 0, 8, 8, 0);
                        (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_RAX, MACHINE_X64_RSP, 64, 0);
                        encoder.bytes[short_exit + 1] = (u8)(residual - (short_exit + 2));
                        encoder.bytes[loop_back + 1] = (u8)(compare_offset - (loop_back + 2));
                    }
                    break; case MACHINE_X64_ATOMIC_RMW:
                    {
                        // Sized load, then a lock cmpxchg retry loop with R8 holding
                        // the proposed value, mirroring the canonical sequence.
                        u32 size = instruction->payload & 0xff;
                        u32 atomic_operation = instruction->payload >> 8;
                        (void)machine_x64_emit_metadata_pointer_chunk(&encoder, true, MACHINE_X64_RAX, MACHINE_X64_RCX, 0, size, 0);
                        u32 retry_offset = encoder.count;
                        (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_R8, MACHINE_X64_RAX, (u16)(size * 8u), 0);
                        if (atomic_operation == IR_ATOMIC_EXCHANGE)
                        {
                            (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_R8, MACHINE_X64_RDX, (u16)(size * 8u), 0);
                        }
                        else
                        {
                            String8 operation = atomic_operation == IR_ATOMIC_ADD           ? S8("ADD")
                                : atomic_operation == IR_ATOMIC_SUBTRACT    ? S8("SUB")
                                : atomic_operation == IR_ATOMIC_BITWISE_AND ? S8("AND")
                                : atomic_operation == IR_ATOMIC_BITWISE_OR  ? S8("OR")
                                : S8("XOR");
                            (void)machine_x64_emit_metadata_registers(&encoder, operation, MACHINE_X64_R8, MACHINE_X64_RDX, (u16)(size * 8u), 0);
                        }
                        (void)machine_x64_emit_metadata_atomic_memory_register(&encoder, S8("CMPXCHG"), MACHINE_X64_RCX, MACHINE_X64_R8,
                                (u16)(size * 8u), 0);
                        u32 retry_branch = encoder.count;
                        (void)machine_x64_emit_metadata_relative(&encoder, S8("JNE"), 0, 32, 0);
                        u32 displacement = retry_offset - (retry_branch + 6);
                        for (u32 byte_index = 0; byte_index < 4; byte_index += 1)
                        {
                            encoder.bytes[retry_branch + 2 + byte_index] = (u8)(displacement >> (byte_index * 8));
                        }
                    }
                    break; case MACHINE_X64_ATOMIC_CMPXCHG:
                    {
                        u32 size = instruction->payload & 0xff;
                        (void)machine_x64_emit_metadata_registers(&encoder, S8("MOV"), MACHINE_X64_RAX, MACHINE_X64_RDX, 64, 0);
                        (void)machine_x64_emit_metadata_atomic_memory_register(&encoder, S8("CMPXCHG"), MACHINE_X64_RCX, MACHINE_X64_RSI,
                                (u16)(size * 8u), 0);
                    }
                    break; case MACHINE_X64_ATOMIC_CMPXCHG16:
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
                        (void)machine_x64_emit_exact_frame_chunk(&encoder, true, MACHINE_X64_RAX, expected_offset, 8, 0);
                        // Frame slots grow toward lower addresses: aggregate byte
                        // offset 8 is represented by the slot offset minus eight.
                        (void)machine_x64_emit_exact_frame_chunk(&encoder, true, MACHINE_X64_RDX, expected_offset - 8, 8, 0);
                        (void)machine_x64_emit_exact_frame_chunk(&encoder, true, MACHINE_X64_RBX, desired_offset, 8, 0);
                        (void)machine_x64_emit_exact_frame_chunk(&encoder, true, MACHINE_X64_RCX, desired_offset - 8, 8, 0);
                        (void)machine_x64_emit_metadata_atomic_memory(&encoder, S8("CMPXCHG16B"), address, 128, 0);
                        (void)machine_x64_emit_exact_frame_chunk(&encoder, false, MACHINE_X64_RAX, result_offset, 8, 0);
                        (void)machine_x64_emit_exact_frame_chunk(&encoder, false, MACHINE_X64_RDX, result_offset - 8, 8, 0);
                    }
                    break; case MACHINE_X64_UD2: (void)machine_x64_emit_metadata_instruction(&encoder, S8("UD2"), 0, 0, (BusterX86MetadataFeatureInput){0}, (BusterX86MetadataPhysicalAttributes){0}, 0);
                    break; case MACHINE_X64_SWITCH:
                    {
                        // The canonical compare chain: the condition sits in the
                        // operand scratch, each case constant is materialized in
                        // RCX, equality jumps to the case block, and the tail jump
                        // takes the default edge.
                        u32 condition = operand_registers[0];
                        // A switch row has no explicit scratch operand.  RCX is the
                        // normal constant carrier, but the allocator may place the
                        // condition there; use RAX in that case so materializing a
                        // case value never destroys the value being compared.
                        u32 compare_register = condition == MACHINE_X64_RCX ? MACHINE_X64_RAX : MACHINE_X64_RCX;
                        for (u32 case_index = 0; case_index < instruction->flags; case_index += 1)
                        {
                            MachineSwitchCase* case_row = function->switch_cases + instruction->payload + case_index;
                            u16 compare_width = case_row->compare_width ? case_row->compare_width : 64;
                            if (compare_width == 32)
                            {
                                (void)machine_x64_emit_metadata_register_immediate(&encoder, S8("MOV"), compare_register,
                                                                                   case_row->value, 32, 32, 0);
                                (void)machine_x64_emit_metadata_registers(&encoder, S8("CMP"), compare_register, condition, 32, 0);
                            }
                            else
                            {
                                (void)machine_x64_emit_exact_movabs(&encoder, compare_register, case_row->value, 0);
                                (void)machine_x64_emit_metadata_registers(&encoder, S8("CMP"), compare_register, condition, 64, 0);
                            }
                            u32 branch_start = encoder.count;
                            (void)machine_x64_emit_metadata_relative(&encoder, S8("JZ"), 0, 32, 0);
                            MachineX64BranchFixup* case_fixup = (MachineX64BranchFixup*)machine_stream_append(arena, &fixups);
                            *case_fixup = (MachineX64BranchFixup){
                                .patch_offset = branch_start + 2,
                                .block = case_row->target_block,
                            };
                        }
                        u32 default_branch_start = encoder.count;
                        (void)machine_x64_emit_metadata_relative(&encoder, S8("JMP"), 0, 32, 0);
                        MachineX64BranchFixup* default_fixup = (MachineX64BranchFixup*)machine_stream_append(arena, &fixups);
                        *default_fixup = (MachineX64BranchFixup){
                            .patch_offset = default_branch_start + 1,
                            .block = machine_ref_payload(instruction->operands[1]),
                        };
                    }
                    break; default: machine_x64_exact_counters_assign(&result, exact_counters); return result;
                }
            }
            MachinePoint after = machine_point_make(instruction_index, MACHINE_POINT_AFTER);
            if (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == after)
            {
                edit_cursor = machine_x64_emit_edit_run(&encoder, function, placement, edit_cursor, after, true);
            }
        }
    }
    if (encoder.overflow)
    {
        machine_x64_exact_counters_assign(&result, exact_counters);
        return result;
    }
    bool fixups_valid = true;
    for (MachineBuilderChunk* chunk = fixups.first; chunk; chunk = chunk->next)
    {
        MachineX64BranchFixup* rows = (MachineX64BranchFixup*)(chunk + 1);
        for (u32 row_index = 0; row_index < chunk->count; row_index += 1)
        {
            MachineX64BranchFixup* fixup = rows + row_index;
            if (fixup->label_address)
            {
                s64 displacement = (s64)result.block_offsets[fixup->block] - (s64)(fixup->patch_offset + 4u);
                if (displacement < INT32_MIN || displacement > INT32_MAX)
                {
                    fixups_valid = false;
                    continue;
                }
                s32 encoded_displacement = (s32)displacement;
                memcpy(encoder.bytes + fixup->patch_offset, &encoded_displacement, sizeof(encoded_displacement));
            }
            else
            {
                u32 displacement = result.block_offsets[fixup->block] - (fixup->patch_offset + 4);
                memcpy(encoder.bytes + fixup->patch_offset, &displacement, sizeof(displacement));
            }
        }
    }
    if (!fixups_valid)
    {
        machine_x64_exact_counters_assign(&result, exact_counters);
        return result;
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
