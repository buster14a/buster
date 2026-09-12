// AArch64 machine selection and encoding. Included by machine.c in the
// backend-implementation-file pattern; not a standalone translation unit.
// The subset covers scalar integer functions — arguments, constants,
// casts, unary and binary arithmetic including divides and shifts,
// comparisons, direct locals and pointer dereference, member addresses,
// branches, and scalar returns — plus the AAPCS64 signature shapes, calls,
// symbol addresses, aggregate/array literal construction into frame slots,
// scalar float bodies (arithmetic, comparison, negation, and conversions
// over the bit-image model, riding V0/V1 internally), scalar stack
// arguments through a fixed outgoing area at the frame bottom, ELF
// variadic definitions using the public ELF AAPCS64 va_list
// (Darwin and Windows variadic conventions stay canonical),
// sixteen-byte short-vector arguments and results as slot-backed values
// touching the V file only at the ABI edges, indirect arguments — any
// aggregate or vector past sixteen bytes — behind a caller-side defensive
// copy, over-aligned locals as runtime-aligned pointers into padded raw
// slots, and rvalue array/vector INDEX bases through their storage
// snapshot. Wide division expands into scalar MIR blocks before placement.
// Sixteen-byte atomics use constrained pair rows, including readback on a
// failed compare-exchange so the returned old image is single-copy atomic.
// Everything else is an explicit unsupported result, never a
// silent misselection.
//
// Register conventions: values live zero-extended in X registers exactly
// like the x86-64 register model. X28 is the frame base (the canonical
// AArch64 path's convention), X29/X30 the frame-pointer pair, X16/X17 the
// encoder's address scratches (and the linker's veneer registers), X18 the
// platform register — none are allocatable. Frame slots sit at positive
// X28-relative offsets; a placement offset o addresses X28 + frame_size - o,
// which keeps the shared placement's grows-down offset convention intact.

#include <buster/lib/compiler/codegen/machine.h>
// For codegen_canonical_integer_aggregate_parts: the canonical emitter's
// named-parameter walk is the variadic model's defining simulation, and
// VA_START must run the exact same one.
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/codegen/codegen_internal.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/generated/aarch64-form-ids.generated.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

// Supported argument-list length: the register files plus the scalar
// stack tail the subset stages, matching the x86-64 selector's cap.
#define MACHINE_A64_MAX_ARGUMENTS 24

// Larger frames address the compact callee-save area from X29 through X16.
// The shared module accounts for the two extra address-setup words.
#define MACHINE_A64_DIRECT_SAVE_MAX (A64_IMM12_MAX * 8u)

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
    .unconditional_branch_opcode = MACHINE_A64_B,
    .switch_opcode = MACHINE_A64_SWITCH,
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

MachineTargetDescription const* machine_target_aarch64(void)
{
    return &machine_aarch64_description;
}

// How one AAPCS64 signature value travels: one integer part for the scalar
// subset, one float part for a float scalar, one or two integer parts for a
// register aggregate, up to four float parts for an HFA, one sixteen-byte
// V-register part for a short vector, or indirect — a large result through
// the X8 pointer, or a large argument (any aggregate or vector past
// sixteen bytes) through a pointer in the integer file, which the machine
// caller points at a defensive copy. A supported aligned integer pair follows
// the same register/stack simulation; other aggregate/HFA stack arguments stay
// outside the subset.
typedef struct MachineA64ValueShape MachineA64ValueShape;
struct MachineA64ValueShape
{
    u32 part_offsets[4];
    u8 part_is_float[4];
    u8 part_sizes[4];
    u32 part_count;
    u32 byte_size;
    // The unrounded size of an indirect result. byte_size is rounded up to
    // whole eightbytes for the value's own slot; the store through the
    // caller's X8 buffer must not write the rounding — the caller allocated
    // exactly the type.
    u32 exact_byte_size;
    bool aggregate;
    // Indirect result: returned through the caller's buffer named by X8.
    // Indirect argument: passed as a pointer riding the integer file.
    bool indirect;
    // One whole-value V-register part; the value itself stays slot-backed.
    bool vector;
    // AAPCS64 starts a sixteen-byte integer pair at an even X argument
    // register.  The bare integer-128 shape deliberately remains outside the
    // machine body subset, but wrapped integer pairs use this bit at the ABI
    // edge and therefore must share the canonical placement rule.
    bool even_integer_pair;
};

// One argument's placement: its shape's parts in consecutive per-class
// registers, integer parts from X0 and float parts from V0 — or, for a
// scalar past its register file or a supported aligned integer pair, stack
// eightbytes at the canonical caller's sequential offsets. Other aggregate
// and HFA stack arguments stay outside the subset; placement fails instead.
typedef struct MachineA64ArgumentPlacement MachineA64ArgumentPlacement;
struct MachineA64ArgumentPlacement
{
    u16 first_integer;
    u16 first_float;
    u16 first_stack_part;
    u8 on_stack;
    u8 reserved;
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

typedef struct MachineA64CallTarget MachineA64CallTarget;
struct MachineA64CallTarget
{
    IrSymbolId symbol;
    u8 reference;
    u8 reserved[3];
};

typedef struct MachineA64Selector MachineA64Selector;
struct MachineA64Selector
{
    Arena* arena;
    IrProgram* program;
    IrFunction* function;
    MachineFunctionBuilder builder;
    u64 reserved_selection_layout[MACHINE_SELECTION_RESERVED_LAYOUT_WORDS];
    Target target;
    MachineBuilderStream immediates;
    MachineBuilderStream stack_slots;
    MachineBuilderStream stack_slot_alignments;
    MachineBuilderStream call_targets;
    MachineBuilderStream va_args;
    MachineBuilderStream switch_cases;
    // Per IrValue: virtual register index, stack slot index, or UINT32_MAX.
    u32* value_virtual_registers;
    MachineCanonicalPair* value_pairs;
    u32* value_stack_slots;
    // Per IrValue: the padded raw slot behind an over-aligned local whose
    // virtual register holds a runtime-aligned pointer, or UINT32_MAX.
    u32* value_indirect_slots;
    MachineSelectionAddressCache address_cache;
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
    // Frame slot of the 64-byte X0-X7 variadic save area, or UINT32_MAX.
    u32 va_register_save_slot;
    // The fixed outgoing stack-argument area, appended at the first call
    // that needs one and pinned by placement to the frame bottom, so its
    // base is exactly the stack pointer a call sees.
    u32 outgoing_slot;
    u32 outgoing_bytes;
    // Definition point per virtual register, patched into the flattened
    // rows because builder chunks are write-once.
    u32* virtual_register_definitions;
    // Per IrValue: the fusion a BRANCH_IF on that condition value selects,
    // and whether the value is a chain member that selects into nothing.
    MachineA64BranchFusion* branch_fusions;
    u8* fused_dead;
    // Only allocated when a wide divide splits a canonical block. Canonical
    // targets enter its first machine block; outgoing edges leave its last.
    u32* block_entries;
    u32* block_exits;
    MachineBlock open_block;
    u32 virtual_register_count;
    IrOpcode failed_opcode;
    bool supported;
    bool returns_value;
    // A selected dynamic allocation moves the stack pointer below the
    // fixed outgoing argument area, whose base every call with stack
    // parts reads as its own stack pointer — the finalize check rejects
    // the pair whole.
    bool stack_allocate_selected;
};

BUSTER_GLOBAL_LOCAL bool machine_a64_type_is_scalar_register(IrType* type)
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
    // A bare 128-bit integer is the AAPCS64 even-aligned X pair — the same
    // two INTEGER parts a sixteen-byte wrapped pair builds — carried by the
    // aggregate machinery over the value's 16-byte slot, exactly like the
    // x86-64 selector's RAX:RDX shape. The pair bit comes from the shared
    // ABI helper so argument and result uses keep the canonical answer.
    if (type && type->layout.resolved && type->kind == IR_TYPE_INTEGER && type->bit_width == 128)
    {
        *shape = (MachineA64ValueShape){
            .part_offsets = {0, 8},
            .part_sizes = {8, 8},
            .part_count = 2,
            .byte_size = 16,
            .aggregate = true,
            .even_integer_pair = ir_abi_value_is_aarch64_even_integer_pair(program, type_id, ir_abi_convention_for_target(target), use),
        };
        return true;
    }
    if (!type || !type->layout.resolved ||
        (type->kind != IR_TYPE_STRUCT && type->kind != IR_TYPE_UNION && type->kind != IR_TYPE_SLICE && type->kind != IR_TYPE_VECTOR &&
         !(type->kind == IR_TYPE_VA_LIST && type->layout.size == 32 && ir_abi_convention_for_target(target) == IR_ABI_CONVENTION_AAPCS64)))
    {
        return false;
    }
    if (type->layout.size > UINT32_MAX - 7)
    {
        return false;
    }
    IrAbiValue abi = ir_type_abi_value(program, type_id, ir_abi_convention_for_target(target), use);
    if (type->kind == IR_TYPE_VECTOR && !abi.indirect && !abi.memory && abi.part_count == 1 && abi.parts[0].abi_class == IR_ABI_CLASS_VECTOR)
    {
        // A short vector rides one V register — the whole register for
        // the sixteen-byte form, the low bytes for anything smaller — and
        // the value itself is slot-backed with only the ABI edges
        // touching the vector file. Sizes without a sized FP transfer
        // encoding (the non-power-of-two lane counts) stay canonical.
        if (abi.parts[0].size != 1 && abi.parts[0].size != 2 && abi.parts[0].size != 4 && abi.parts[0].size != 8 && abi.parts[0].size != 16)
        {
            return false;
        }
        *shape = (MachineA64ValueShape){
            .part_is_float = {1},
            .part_sizes = {(u8)abi.parts[0].size},
            .part_count = 1,
            .byte_size = (abi.parts[0].size + 7u) & ~7u,
            .aggregate = true,
            .vector = true,
        };
        return true;
    }
    if (abi.indirect || abi.memory || !abi.part_count || abi.part_count > 4)
    {
        if (use == IR_ABI_USE_RESULT && abi.indirect)
        {
            // Large results return through the caller's X8-named buffer.
            *shape = (MachineA64ValueShape){
                .byte_size = (u32)((type->layout.size + 7) & ~(u64)7),
                .exact_byte_size = (u32)type->layout.size,
                .aggregate = true,
                .indirect = true,
            };
            return true;
        }
        if (use != IR_ABI_USE_RESULT && (abi.indirect || abi.memory))
        {
            // AArch64 passes every aggregate and vector past sixteen bytes
            // as a pointer riding the integer file. The machine caller
            // stages a defensive copy behind that pointer — AAPCS64 gives
            // the callee license to scribble on the pointed-at memory,
            // which the canonical caller's pass-the-value-slot shortcut
            // does not survive.
            *shape = (MachineA64ValueShape){
                .part_sizes = {8},
                .part_count = 1,
                .byte_size = (u32)((type->layout.size + 7) & ~(u64)7),
                .aggregate = true,
                .indirect = true,
            };
            return true;
        }
        return false;
    }
    MachineA64ValueShape built = {
        .part_count = abi.part_count,
        .byte_size = (u32)((type->layout.size + 7) & ~(u64)7),
        .aggregate = true,
        .even_integer_pair = ir_abi_value_is_aarch64_even_integer_pair(program, type_id, ir_abi_convention_for_target(target), use),
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

// One V-register frame transfer row for a vector shape's ABI edge: the q
// form for sixteen bytes, the sized form carrying its log2 in the payload
// otherwise. The shape gate has already restricted part sizes to
// 1/2/4/8/16.
BUSTER_GLOBAL_LOCAL MachineInstruction machine_a64_vector_transfer_row(MachineA64ValueShape* shape, u32 slot, u32 vector_register, bool store)
{
    u32 size = shape->part_sizes[0];
    MachineInstruction row = {
        .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
        .payload = vector_register << 24,
    };
    if (size == 16)
    {
        row.opcode = (u16)(store ? MACHINE_A64_VSTORE_FRAME : MACHINE_A64_VLOAD_FRAME);
    }
    else
    {
        u32 size_log2 = size == 1 ? 0 : size == 2 ? 1u : size == 4 ? 2u : 3u;
        row.payload |= size_log2 << 28;
        row.opcode = (u16)(store ? MACHINE_A64_VSTORE_FRAME_SIZED : MACHINE_A64_VLOAD_FRAME_SIZED);
    }
    return row;
}

// Consecutive-register assignment per class: integer parts take the next
// X argument register, float parts the next V register. Whatever no
// longer fits takes sequential eight-byte stack parts — a scalar or an
// indirect argument's pointer as one part, a supported aligned integer pair,
// or a register aggregate, HFA, or vector as its eightbyte images. The exact
// simulation the canonical
// caller and parameter capture both run, including both its quirks:
// integer overflow leaves the X file open for a later narrower argument,
// while an HFA or vector that overflows closes the V file behind it.
BUSTER_GLOBAL_LOCAL bool machine_a64_place_argument(MachineA64ValueShape* shape, u32* integer_count, u32* float_count, u32* stack_part_count,
                                                    MachineA64ArgumentPlacement* placement)
{
    bool placed = true;
    u32 integer_parts = 0;
    u32 float_parts = 0;
    for (u32 part_index = 0; part_index < shape->part_count; part_index += 1)
    {
        integer_parts += shape->part_is_float[part_index] == 0;
        float_parts += shape->part_is_float[part_index] != 0;
    }
    *placement = (MachineA64ArgumentPlacement){
        .first_integer = (u16)*integer_count,
        .first_float = (u16)*float_count,
    };
    if (shape->even_integer_pair && *integer_count < 8)
    {
        *integer_count = (*integer_count + 1u) & ~1u;
        placement->first_integer = (u16)*integer_count;
    }
    if (*integer_count + integer_parts <= 8 && *float_count + float_parts <= 8)
    {
        *integer_count += integer_parts;
        *float_count += float_parts;
    }
    else if (shape->part_count == 1 && (!shape->aggregate || shape->indirect))
    {
        // A scalar past its file, or an indirect argument's pointer past
        // the integer file, takes the next sequential eight-byte stack
        // part — the pointer is an ordinary integer part to the placement.
        placement->on_stack = 1;
        placement->first_stack_part = (u16)*stack_part_count;
        *stack_part_count += 1;
    }
    else if (float_parts == shape->part_count)
    {
        // An HFA or vector past the V file goes to the stack as its
        // eightbyte images — and the canonical caller closes the V file
        // behind it, so no later float argument may take a register the
        // skipped value did not. The integer file has no such rule.
        *float_count = 8;
        placement->on_stack = 1;
        placement->first_stack_part = (u16)*stack_part_count;
        *stack_part_count += shape->byte_size / 8;
    }
    else if (integer_parts == shape->part_count)
    {
        // A register aggregate past the X file: its eightbytes go to the
        // stack, and AAPCS64 C.12 closes the integer file behind it (NGRN
        // becomes 8), so no later scalar may take a register the skipped
        // composite did not — clang lays out `7 x u64, {u64,u64}, u64` with
        // both the pair and the trailing scalar on the stack.
        *integer_count = 8;
        placement->on_stack = 1;
        placement->first_stack_part = (u16)*stack_part_count;
        if (shape->even_integer_pair)
        {
            *stack_part_count = (*stack_part_count + 1u) & ~1u;
            placement->first_stack_part = (u16)*stack_part_count;
        }
        *stack_part_count += shape->byte_size / 8;
    }
    else
    {
        placed = false;
    }
    return placed;
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

BUSTER_GLOBAL_LOCAL u32 machine_a64_select_row(MachineA64Selector* selector, MachineInstruction instruction)
{
    machine_builder_classify_instruction_mutability(&selector->builder, &instruction);
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
    u32 result;
    switch (operation)
    {
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_POINTER_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL:
        result = MACHINE_A64_CONDITION_EQUAL;
        break;
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_POINTER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL:
        result = MACHINE_A64_CONDITION_NOT_EQUAL;
        break;
    case IR_BINARY_SIGNED_LESS:
        result = MACHINE_A64_CONDITION_LESS;
        break;
    case IR_BINARY_SIGNED_LESS_EQUAL:
        result = MACHINE_A64_CONDITION_LESS_EQUAL;
        break;
    case IR_BINARY_SIGNED_GREATER:
        result = MACHINE_A64_CONDITION_GREATER;
        break;
    case IR_BINARY_SIGNED_GREATER_EQUAL:
        result = MACHINE_A64_CONDITION_GREATER_EQUAL;
        break;
    case IR_BINARY_UNSIGNED_LESS:
        result = MACHINE_A64_CONDITION_BELOW;
        break;
    case IR_BINARY_UNSIGNED_LESS_EQUAL:
        result = MACHINE_A64_CONDITION_BELOW_EQUAL;
        break;
    case IR_BINARY_UNSIGNED_GREATER:
        result = MACHINE_A64_CONDITION_ABOVE;
        break;
    case IR_BINARY_UNSIGNED_GREATER_EQUAL:
        result = MACHINE_A64_CONDITION_ABOVE_EQUAL;
        break;
    default:
        result = UINT32_MAX;
        break;
    }

    return result;
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

BUSTER_GLOBAL_LOCAL u32 machine_a64_append_immediate(MachineA64Selector* selector, u64 value)
{
    u32 index = selector->immediates.total_count;
    u64* row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
    *row = value;
    return index;
}

// Mirrors the canonical emitter's indirect-place test: an over-aligned
// local's virtual register holds a runtime-aligned pointer, so the local
// reads and writes exactly like a GLOBAL's address, never like a direct
// frame slot and never like a promoted register value.
BUSTER_GLOBAL_LOCAL bool machine_a64_local_is_indirect(MachineA64Selector* selector, IrValueId value)
{
    return value.value < selector->function->value_count && selector->value_indirect_slots[value.value] != UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL MachineSelectionAddress machine_a64_address(MachineA64Selector* selector, IrValueId value)
{
    return machine_selection_address(selector->arena, selector->program, selector->function, &selector->address_cache, value);
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
    MachineSelectionAddress address = machine_a64_address(selector, base);
    // A zero-offset copy reuses the already materialized SSA address.
    // Recomputing it from the root would add encoding work at every use.
    if (byte_offset && address.index_value.value == IR_ID_UNDERLYING_INVALID && address.base_value.value != base.value &&
        address.displacement <= INT32_MAX && byte_offset <= (u32)INT32_MAX - address.displacement)
    {
        MachineSelectionAddress root = machine_a64_address(selector, address.base_value);
        bool promoted = root.opcode == IR_OPCODE_LOCAL && selector->value_virtual_registers[address.base_value.value] != UINT32_MAX &&
                        !machine_a64_local_is_indirect(selector, address.base_value);
        if (!promoted)
        {
            base = address.base_value;
            byte_offset += (u32)address.displacement;
            address = root;
        }
    }
    // The shared fact treats incoming block parameters as opaque pointer
    // values and retains the LOCAL distinction without chasing a definition.
    bool local = address.opcode == IR_OPCODE_LOCAL;
    u32 slot = selector->value_stack_slots[base.value];
    if (local && selector->value_virtual_registers[base.value] != UINT32_MAX &&
        !machine_a64_local_is_indirect(selector, base))
    {
        // A promoted local has no address. The promotability scan proved
        // no use needs one, so a request here is a selector hole — refuse
        // to the canonical fallback rather than hand a register's value
        // out as an address. An over-aligned local's register is the
        // aligned pointer itself and falls through to the pointer path.
        return false;
    }
    // An array or vector *value* is its storage, exactly like the
    // canonical INDEX base rule: its slot address is the base address —
    // the snapshot an rvalue base indexes into. Slices and struct values
    // stay on the loaded-pointer path below.
    bool storage_value = (address.flags & MACHINE_SELECTION_ADDRESS_STORAGE) != 0;
    if (storage_value && slot != UINT32_MAX)
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

BUSTER_GLOBAL_LOCAL bool machine_a64_select_local(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    // Direct locals produce no code: the stack slot recorded during
    // classification is the storage, exactly like the canonical path —
    // or, promoted, the virtual register is. An over-aligned local
    // computes its runtime-aligned pointer here, the same lea/add/and
    // the canonical emitter runs at its LOCAL instruction; the add folds
    // into the LEA's byte offset.
    bool selected;
    u32 indirect_slot = selector->value_indirect_slots[instruction->result.value];
    if (indirect_slot != UINT32_MAX)
    {
        u32 pointer_register = selector->value_virtual_registers[instruction->result.value];
        IrValue* local_value = function->values + instruction->result.value;
        IrType* local_type = ir_type_from_id(&program->types, local_value->canonical_type);
        u32 local_alignment = BUSTER_MAX(BUSTER_MAX(local_value->alignment, local_type ? local_type->layout.alignment : 0), 8u);
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                    machine_ref_make(MACHINE_REF_STACK_SLOT, indirect_slot)},
                                                       .payload = local_alignment - 1,
                                                       .opcode = MACHINE_A64_LEA_FRAME,
                                                   });
        machine_a64_define(selector, pointer_register, row);
        u32 mask_register = machine_a64_synthesize_register(selector);
        u32 mask_immediate = machine_a64_append_immediate(selector, 0 - (u64)local_alignment);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, mask_immediate)},
                                             .opcode = MACHINE_A64_MOV_RI,
                                         });
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register)},
                                             .opcode = MACHINE_A64_AND64,
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

BUSTER_GLOBAL_LOCAL bool machine_a64_select_stack_save(MachineA64Selector* selector, u32 result_register)
{
    // Save/restore pairs are exact SP copies; only a selected
    // STACK_ALLOCATE moves SP between them.
    bool selected = false;
    if (result_register != UINT32_MAX)
    {
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                       .opcode = MACHINE_A64_READ_SP,
                                                   });
        machine_a64_define(selector, result_register, row);
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_stack_restore(MachineA64Selector* selector, IrInstruction* instruction)
{
    bool selected = false;
    u32 saved_register;
    if (machine_a64_operand_register(selector, instruction->operands[0], &saved_register))
    {
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, saved_register)},
                                             .opcode = MACHINE_A64_WRITE_SP,
                                         });
        selected = true;
    }
    return selected;
}

// Dynamic stack allocation as one constrained row: the size travels in
// through X9 and the aligned pointer comes back in X10, with the
// canonical page-probed loop expanded whole in the encoder. Whether the
// function also stages outgoing stack arguments is only known once every
// call has selected, so the outgoing-area collision is rejected at
// finalize rather than here.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_stack_allocate(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    bool selected = false;
    u32 size_register;
    if (result_register != UINT32_MAX && instruction->immediate_count && instruction->immediates && instruction->operand_count >= 1)
    {
        u64 requested_alignment = instruction->immediates[0];
        // An out-of-range request lands on zero, which the power-of-two
        // test below rejects along with every other unusable alignment.
        u32 stack_alignment = requested_alignment <= UINT32_MAX ? BUSTER_MAX((u32)requested_alignment, 16u) : 0;
        if (stack_alignment && (stack_alignment & (stack_alignment - 1u)) == 0 &&
            machine_a64_operand_register(selector, instruction->operands[0], &size_register))
        {
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, size_register)},
                                                           .payload = stack_alignment,
                                                           .opcode = MACHINE_A64_STACK_ALLOCATE,
                                                       });
            machine_a64_define(selector, result_register, row);
            selector->stack_allocate_selected = true;
            selected = true;
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_argument(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    // The typed ARGUMENT can appear anywhere the frontend first used
    // the parameter; the value itself was captured by the entry rows
    // before any scratch register could clobber the incoming fixed
    // registers.
    bool selected = false;
    if (instruction->immediate_count && instruction->immediates)
    {
        u32 argument_index = (u32)instruction->immediates[0];
        selected = argument_index < MACHINE_A64_MAX_ARGUMENTS &&
                   (result_register != UINT32_MAX || selector->value_stack_slots[instruction->result.value] != UINT32_MAX) &&
                   selector->argument_values[argument_index] == instruction->result.value;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_constant(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

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
        u32 immediate_index = machine_a64_append_immediate(selector, immediate);
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                       .opcode = MACHINE_A64_MOV_RI,
                                                   });
        machine_a64_define(selector, result_register, row);
        selected = true;
    }
    else if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER && instruction->result.value != IR_ID_UNDERLYING_INVALID &&
             instruction->result.value < function->value_count && selector->value_stack_slots[instruction->result.value] != UINT32_MAX)
    {
        // A 128-bit integer constant is slot-backed like every i128 value.
        // The second eightbyte is the canonical emitters' selection: an
        // explicit second immediate when the frontend recorded one,
        // otherwise the negated low half's sign fill.
        IrType* constant_type = ir_type_from_id(&program->types, instruction->canonical_type);
        if (constant_type && constant_type->kind == IR_TYPE_INTEGER && constant_type->bit_width == 128)
        {
            u32 result_slot = selector->value_stack_slots[instruction->result.value];
            u64 low = instruction->immediate_is_negative ? 0 - instruction->immediates[0] : instruction->immediates[0];
            u64 high = instruction->immediate_count > 1 ? instruction->immediates[1] : instruction->immediate_is_negative ? UINT64_MAX : 0;
            u64 halves[2] = {low, high};
            for (u32 half_index = 0; half_index < 2; half_index += 1)
            {
                u32 half_register = machine_a64_synthesize_register(selector);
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, half_register),
                                                                  machine_ref_make(MACHINE_REF_IMMEDIATE,
                                                                                   machine_a64_append_immediate(selector, halves[half_index]))},
                                                     .opcode = MACHINE_A64_MOV_RI,
                                                 });
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, half_register)},
                                                     .payload = half_index * 8,
                                                     .opcode = MACHINE_A64_STORE_FRAME64,
                                                 });
            }
            selected = true;
        }
    }
    return selected;
}

// Integer-to-float mirrors the canonical emitter: a narrower signed source
// sign-extends first, an unsigned one is already zero-extended in the
// register model, and the 64-bit scvtf/ucvtf carries every case.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_cast_integer_to_float(MachineA64Selector* selector, IrInstruction* instruction, IrType* cast_target_type,
                                                                  u32 source_bits, u32 source_register, u32 result_register)
{
    bool selected = false;
    bool cast_signed = instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT;
    bool convertible = cast_target_type && cast_target_type->kind == IR_TYPE_FLOAT &&
                       (cast_target_type->bit_width == 32 || cast_target_type->bit_width == 64) && source_bits;
    if (convertible)
    {
        u32 extended_register = source_register;
        if (cast_signed && source_bits < 64)
        {
            u16 extend_opcode = (u16)(source_bits == 8 ? MACHINE_A64_SXTB : source_bits == 16 ? MACHINE_A64_SXTH : MACHINE_A64_SXTW);
            extended_register = machine_a64_synthesize_register(selector);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, extended_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                 .opcode = extend_opcode,
                                             });
        }
        u16 convert_opcode = (u16)(cast_signed ? (cast_target_type->bit_width == 64 ? MACHINE_A64_CVT_I64_TO_F64 : MACHINE_A64_CVT_I64_TO_F32)
                                               : (cast_target_type->bit_width == 64 ? MACHINE_A64_CVT_U64_TO_F64 : MACHINE_A64_CVT_U64_TO_F32));
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, extended_register)},
                                                       .opcode = convert_opcode,
                                                   });
        machine_a64_define(selector, result_register, row);
        selected = true;
    }
    return selected;
}

// Float-to-integer mirrors the canonical emitter: fcvtzs/fcvtzu always
// convert to the full 64-bit register, and narrower declared targets keep
// that image exactly like the canonical x9 store.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_cast_float_to_integer(MachineA64Selector* selector, IrInstruction* instruction, IrType* source_type,
                                                                  u32 source_register, u32 result_register)
{
    bool selected = false;
    bool to_unsigned = instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER;
    bool convertible = source_type && source_type->kind == IR_TYPE_FLOAT && (source_type->bit_width == 32 || source_type->bit_width == 64);
    if (convertible)
    {
        u16 convert_opcode = (u16)(to_unsigned ? (source_type->bit_width == 64 ? MACHINE_A64_CVT_F64_TO_U64 : MACHINE_A64_CVT_F32_TO_U64)
                                               : (source_type->bit_width == 64 ? MACHINE_A64_CVT_F64_TO_I64 : MACHINE_A64_CVT_F32_TO_I64));
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                       .opcode = convert_opcode,
                                                   });
        machine_a64_define(selector, result_register, row);
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_float_to_i128(MachineA64Selector* selector, IrInstruction* instruction,
                                                          IrType* source_type, IrType* target_type);

// i128 values use the same two-eightbyte frame representation as aggregate
// values, mirroring machine_x64_select_cast_i128: scalar integer extensions
// and truncations use the existing frame rows, a 128-bit reinterpret is a
// byte-preserving frame copy. Scalar float inputs use the magnitude split below.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_cast_i128(MachineA64Selector* selector, IrInstruction* instruction, IrType* source_type,
                                                      IrType* cast_target_type, u32 result_register)
{
    IrFunction* function = selector->function;

    bool selected = false;
    if (source_type && source_type->kind == IR_TYPE_FLOAT)
    {
        selected = machine_a64_select_float_to_i128(selector, instruction, source_type, cast_target_type);
    }
    else if (source_type && cast_target_type && source_type->kind == IR_TYPE_INTEGER && cast_target_type->kind == IR_TYPE_INTEGER)
    {
        bool source_integer128 = source_type->bit_width == 128;
        bool target_integer128 = cast_target_type->bit_width == 128;
        u32 source_bits = source_type->bit_width;
        u32 source_slot = instruction->operands[0].value < function->value_count ? selector->value_stack_slots[instruction->operands[0].value] : UINT32_MAX;
        u32 target_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
        bool reinterpret_i128 = source_integer128 && target_integer128 &&
                                (instruction->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET ||
                                 instruction->conversion_operation == IR_CONVERSION_IDENTITY);
        bool truncate_i128 = source_integer128 && !target_integer128 && cast_target_type->bit_width <= 64 &&
                             instruction->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE;
        bool extend_i128 = target_integer128 && !source_integer128 && source_bits >= 8 && source_bits <= 64 &&
                           (instruction->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND ||
                            instruction->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND);
        if (reinterpret_i128)
        {
            selected = source_slot != UINT32_MAX && target_slot != UINT32_MAX;
            if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, target_slot),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, source_slot)},
                                                     .payload = 16,
                                                     .opcode = MACHINE_A64_COPY_FRAME_FROM_FRAME,
                                                 });
            }
        }
        else if (truncate_i128)
        {
            selected = result_register != UINT32_MAX && source_slot != UINT32_MAX;
            if (selected)
            {
                u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_STACK_SLOT, source_slot)},
                                                               .opcode = MACHINE_A64_LOAD_FRAME,
                                                           });
                machine_a64_define(selector, result_register, row);
                if (cast_target_type->bit_width < 64)
                {
                    u16 narrow_opcode = cast_target_type->bit_width == 8    ? MACHINE_A64_UXTB
                                        : cast_target_type->bit_width == 16 ? MACHINE_A64_UXTH
                                                                            : MACHINE_A64_MOV32_RR;
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                         .opcode = narrow_opcode,
                                                     });
                }
            }
        }
        else if (extend_i128)
        {
            u32 source_register = UINT32_MAX;
            selected = result_register == UINT32_MAX && target_slot != UINT32_MAX &&
                       machine_a64_operand_register(selector, instruction->operands[0], &source_register);
            if (selected)
            {
                bool sign_extend = instruction->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND;
                // The register model keeps values zero-extended, so the zero
                // form reuses the source image; the signed form re-extends
                // into a fresh low so the shift below reads a full image.
                u32 low_register = source_register;
                if (sign_extend && source_bits < 64)
                {
                    u16 extend_opcode = (u16)(source_bits == 8 ? MACHINE_A64_SXTB : source_bits == 16 ? MACHINE_A64_SXTH : MACHINE_A64_SXTW);
                    low_register = machine_a64_synthesize_register(selector);
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                         .opcode = extend_opcode,
                                                     });
                }
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, target_slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low_register)},
                                                     .opcode = MACHINE_A64_STORE_FRAME64,
                                                 });
                u32 high_register;
                if (sign_extend)
                {
                    // The high half of a signed extension is the sign bit
                    // replicated: arithmetic-shift the low image by 63 so the
                    // runtime sign chooses all ones or all zeroes.
                    u32 shift_register = machine_a64_synthesize_register(selector);
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, shift_register),
                                                                      machine_ref_make(MACHINE_REF_IMMEDIATE, machine_a64_append_immediate(selector, 63))},
                                                         .opcode = MACHINE_A64_MOV_RI,
                                                     });
                    high_register = machine_a64_synthesize_register(selector);
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, shift_register)},
                                                         .opcode = MACHINE_A64_ASR64,
                                                     });
                }
                else
                {
                    high_register = machine_a64_synthesize_register(selector);
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high_register),
                                                                      machine_ref_make(MACHINE_REF_IMMEDIATE, machine_a64_append_immediate(selector, 0))},
                                                         .opcode = MACHINE_A64_MOV_RI,
                                                     });
                }
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, target_slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high_register)},
                                                     .payload = 8,
                                                     .opcode = MACHINE_A64_STORE_FRAME64,
                                                 });
            }
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_i128_to_float(MachineA64Selector* selector, IrInstruction* instruction,
                                                          IrType* source_type, IrType* target_type, u32 result_register);

BUSTER_GLOBAL_LOCAL bool machine_a64_select_cast(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 source_register;
    IrType* early_source_type = instruction->operands[0].value < function->value_count
                                    ? ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type)
                                    : 0;
    IrType* early_target_type = ir_type_from_id(&program->types, instruction->canonical_type);
    bool source_is_integer128 = early_source_type && early_source_type->kind == IR_TYPE_INTEGER && early_source_type->bit_width == 128;
    bool target_is_integer128 = early_target_type && early_target_type->kind == IR_TYPE_INTEGER && early_target_type->bit_width == 128;
    if (source_is_integer128 && early_target_type && early_target_type->kind == IR_TYPE_FLOAT)
    {
        selected = machine_a64_select_i128_to_float(selector, instruction, early_source_type, early_target_type, result_register);
    }
    else if (source_is_integer128 || target_is_integer128)
    {
        selected = machine_a64_select_cast_i128(selector, instruction, early_source_type, early_target_type, result_register);
    }
    else if (result_register != UINT32_MAX && machine_a64_operand_register(selector, instruction->operands[0], &source_register))
    {
        IrType* source_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        IrType* cast_target_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u32 source_bits = machine_a64_scalar_bit_width(source_type);
        if (instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND || instruction->conversion_operation == IR_CONVERSION_FLOAT_TRUNCATE)
        {
            bool extend = instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND;
            bool shaped = source_type && source_type->kind == IR_TYPE_FLOAT &&
                          source_type->bit_width == (extend ? 32u : 64u) && cast_target_type && cast_target_type->kind == IR_TYPE_FLOAT &&
                          cast_target_type->bit_width == (extend ? 64u : 32u);
            if (shaped)
            {
                u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                               .opcode = (u16)(extend ? MACHINE_A64_CVT_F32_TO_F64 : MACHINE_A64_CVT_F64_TO_F32),
                                                           });
                machine_a64_define(selector, result_register, row);
                selected = true;
            }
        }
        else if (instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ||
                 instruction->conversion_operation == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT)
        {
            selected = machine_a64_select_cast_integer_to_float(selector, instruction, cast_target_type, source_bits, source_register, result_register);
        }
        else if (instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ||
                 instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER)
        {
            selected = machine_a64_select_cast_float_to_integer(selector, instruction, source_type, source_register, result_register);
        }
        else
        {
            u16 opcode = 0;
            switch (instruction->conversion_operation)
            {
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
            if (opcode)
            {
                u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                               .opcode = opcode,
                                                           });
                machine_a64_define(selector, result_register, row);
                selected = true;
            }
        }
    }
    return selected;
}

// Trailing-zero count is RBIT followed by CLZ. Synthesize each temporary
// immediately before its defining row so verifier positions remain exact.
BUSTER_GLOBAL_LOCAL u32 machine_a64_select_zero_count(MachineA64Selector* selector, u32 source_register, u32 result_register, bool wide, bool leading)
{
    if (!leading)
    {
        u32 reversed = machine_a64_synthesize_register(selector);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, reversed),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                             .opcode = (u16)(wide ? MACHINE_A64_RBIT64 : MACHINE_A64_RBIT32),
                                         });
        source_register = reversed;
    }
    if (result_register == UINT32_MAX)
    {
        result_register = machine_a64_synthesize_register(selector);
    }
    u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                 .opcode = (u16)(wide ? MACHINE_A64_CLZ64 : MACHINE_A64_CLZ32),
                                             });
    machine_a64_define(selector, result_register, row);
    return result_register;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_unary(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 source_register;
    if (result_register != UINT32_MAX && machine_a64_operand_register(selector, instruction->operands[0], &source_register))
    {
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
            selected = true;
        }
        else if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS ||
                 instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS)
        {
            machine_a64_select_zero_count(selector, source_register, result_register, wide,
                                          instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS);
            selected = true;
        }
        else if (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
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
            selected = true;
        }
        else if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE)
        {
            // The canonical path negates the bit image on the integer side —
            // materialize the sign mask and XOR — so no float row exists to
            // port. The 32-bit EOR keeps the f32 image zero-extended.
            IrType* operand_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
            if (operand_type && operand_type->kind == IR_TYPE_FLOAT && (operand_type->bit_width == 32 || operand_type->bit_width == 64))
            {
                bool wide_float = operand_type->bit_width == 64;
                u32 mask_immediate = selector->immediates.total_count;
                u64* mask_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
                *mask_row = wide_float ? 0x8000000000000000ull : 0x80000000ull;
                u32 mask_register = machine_a64_synthesize_register(selector);
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register),
                                                                  machine_ref_make(MACHINE_REF_IMMEDIATE, mask_immediate)},
                                                     .opcode = MACHINE_A64_MOV_RI,
                                                 });
                u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register)},
                                                               .opcode = (u16)(wide_float ? MACHINE_A64_EOR64 : MACHINE_A64_EOR32),
                                                           });
                machine_a64_define(selector, result_register, row);
                selected = true;
            }
        }
    }
    return selected;
}

// AArch64 has no scalar remainder instruction. Expanding the old encoder
// macro-op before allocation is what makes quotient and product pressure,
// scheduling dependencies, and register lifetimes visible to the machine
// passes: the quotient and the product stay separate SSA values, and MSUB's
// equivalent `left - quotient * right` is spelled as a SUB here.
BUSTER_GLOBAL_LOCAL void machine_a64_select_remainder_expansion(MachineA64Selector* selector, u16 arithmetic, bool wide, u32 result_register,
                                                                u32 left_register, u32 right_register)
{
    u16 divide = arithmetic == MACHINE_A64_SREM32   ? MACHINE_A64_SDIV32
                 : arithmetic == MACHINE_A64_SREM64 ? MACHINE_A64_SDIV64
                 : arithmetic == MACHINE_A64_UREM32 ? MACHINE_A64_UDIV32
                                                    : MACHINE_A64_UDIV64;
    u16 multiply = wide ? MACHINE_A64_MUL64 : MACHINE_A64_MUL32;
    u16 subtract = wide ? MACHINE_A64_SUB64 : MACHINE_A64_SUB32;
    u32 quotient_register = machine_a64_synthesize_register(selector);
    u32 divide_row = machine_a64_select_row(selector, (MachineInstruction){
                                                          .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, quotient_register),
                                                                       machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                       machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                          .opcode = divide,
                                                      });
    machine_a64_define(selector, quotient_register, divide_row);
    u32 product_register = machine_a64_synthesize_register(selector);
    u32 multiply_row = machine_a64_select_row(selector, (MachineInstruction){
                                                            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, product_register),
                                                                         machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, quotient_register),
                                                                         machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                            .opcode = multiply,
                                                        });
    machine_a64_define(selector, product_register, multiply_row);
    u32 subtract_row = machine_a64_select_row(selector, (MachineInstruction){
                                                            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                         machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                         machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, product_register)},
                                                            .opcode = subtract,
                                                        });
    machine_a64_define(selector, result_register, subtract_row);
}

// Vector arithmetic over slot-backed values: the canonical emitter
// scalarizes lane by lane through V0, the machine rows run the NEON
// three-same word per sixteen-byte chunk through the fixed V0/V1 compute
// scratches — no vector register file is involved. Only the
// NEON-encodable set selects (add/sub/mul, the bitwise trio, and float
// add/sub/mul/div). Other operations expand into scalar MIR lanes.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_vector_binary(MachineA64Selector* selector, IrInstruction* instruction, IrType* vector)
{
    IrProgram* program = selector->program;

    bool selected = false;
    IrType* element = ir_type_from_id(&program->types, vector->element_type);
    u32 left_slot = selector->value_stack_slots[instruction->operands[0].value];
    u32 right_slot = selector->value_stack_slots[instruction->operands[1].value];
    u32 result_slot = instruction->result.value != IR_ID_UNDERLYING_INVALID && instruction->result.value < selector->function->value_count
                          ? selector->value_stack_slots[instruction->result.value]
                          : UINT32_MAX;
    bool shaped = element && (element->kind == IR_TYPE_INTEGER || element->kind == IR_TYPE_FLOAT) &&
                  (element->bit_width == 8 || element->bit_width == 16 || element->bit_width == 32 || element->bit_width == 64) &&
                  vector->layout.resolved && (u64)(element->bit_width / 8) * vector->element_count == vector->layout.size &&
                  vector->layout.size % 16 == 0 && vector->layout.size <= INT32_MAX && left_slot != UINT32_MAX && right_slot != UINT32_MAX &&
                  result_slot != UINT32_MAX;
    if (shaped)
    {
        u32 lane_log2 = element->bit_width == 8 ? 0 : element->bit_width == 16 ? 1 : element->bit_width == 32 ? 2 : 3;
        u32 operation = UINT32_MAX;
        if (element->kind == IR_TYPE_FLOAT && element->bit_width >= 32)
        {
            operation = instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_ADD        ? 6
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_SUBTRACT ? 7
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_MULTIPLY ? 8
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_DIVIDE   ? 9
                                                                                           : UINT32_MAX;
        }
        else if (element->kind == IR_TYPE_INTEGER)
        {
            // No 64-bit-lane NEON multiply exists; use scalar MIR lanes.
            operation = instruction->binary_operation == IR_BINARY_VECTOR_INTEGER_ADD           ? 0
                        : instruction->binary_operation == IR_BINARY_VECTOR_INTEGER_SUBTRACT    ? 1
                        : instruction->binary_operation == IR_BINARY_VECTOR_INTEGER_MULTIPLY    ? (lane_log2 == 3 ? UINT32_MAX : 2)
                        : instruction->binary_operation == IR_BINARY_VECTOR_INTEGER_BITWISE_AND ? 3
                        : instruction->binary_operation == IR_BINARY_VECTOR_INTEGER_BITWISE_OR  ? 4
                        : instruction->binary_operation == IR_BINARY_VECTOR_INTEGER_BITWISE_XOR ? 5
                                                                                                : UINT32_MAX;
        }
        if (operation != UINT32_MAX)
        {
            selected = true;
            for (u32 chunk_offset = 0; chunk_offset < (u32)vector->layout.size; chunk_offset += 16)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, left_slot)},
                                                     .payload = chunk_offset,
                                                     .opcode = MACHINE_A64_VLOAD_FRAME,
                                                 });
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, right_slot)},
                                                     .payload = chunk_offset | (1u << 24),
                                                     .opcode = MACHINE_A64_VLOAD_FRAME,
                                                 });
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .payload = (lane_log2 << 8) | operation,
                                                     .opcode = MACHINE_A64_VARITH,
                                                 });
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot)},
                                                     .payload = chunk_offset,
                                                     .opcode = MACHINE_A64_VSTORE_FRAME,
                                                 });
            }
        }
    }
    return selected;
}

// A synthesized register holding a 64-bit frame read at slot + offset.
BUSTER_GLOBAL_LOCAL u32 machine_a64_select_frame_load64(MachineA64Selector* selector, u32 slot, u32 byte_offset)
{
    u32 result = machine_a64_synthesize_register(selector);
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result),
                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                         .payload = byte_offset,
                                         .opcode = MACHINE_A64_LOAD_FRAME,
                                     });
    return result;
}

BUSTER_GLOBAL_LOCAL void machine_a64_select_frame_store64(MachineA64Selector* selector, u32 slot, u32 byte_offset, u32 value_register)
{
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                         .payload = byte_offset,
                                         .opcode = MACHINE_A64_STORE_FRAME64,
                                     });
}

BUSTER_GLOBAL_LOCAL u32 machine_a64_select_immediate_register(MachineA64Selector* selector, u64 value)
{
    u32 result = machine_a64_synthesize_register(selector);
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result),
                                                      machine_ref_make(MACHINE_REF_IMMEDIATE, machine_a64_append_immediate(selector, value))},
                                         .opcode = MACHINE_A64_MOV_RI,
                                     });
    return result;
}

// The frontend normally leaves a shift count as one integer constant,
// optionally wrapped in a short cast chain while applying the usual
// arithmetic conversions. Keep this walk explicitly bounded: an immediate
// machine lowering must never turn arbitrary IR into a compile-time search.
// Mirrors machine_x64_constant_shift_amount.
BUSTER_GLOBAL_LOCAL bool machine_a64_constant_shift_amount(MachineA64Selector* selector, IrValueId value, u32* amount_out)
{
    IrFunction* function = selector->function;

    bool result = false;
    bool done = false;
    for (u32 depth = 0; depth < 4 && !done; depth += 1)
    {
        IrInstruction* definition = 0;
        if (value.value < function->value_count && function->values[value.value].definition.value < function->instruction_count)
        {
            definition = function->instructions + function->values[value.value].definition.value;
        }
        if (!definition)
        {
            done = true;
        }
        else if (definition->opcode == IR_OPCODE_CONSTANT_INTEGER)
        {
            if (definition->immediate_count && definition->immediates && !definition->immediate_is_negative && definition->immediates[0] <= 127)
            {
                *amount_out = (u32)definition->immediates[0];
                result = true;
            }
            done = true;
        }
        else if (definition->opcode != IR_OPCODE_CAST || definition->operand_count < 1 || !definition->operands ||
                 definition->operands[0].value >= function->value_count)
        {
            done = true;
        }
        else
        {
            IrType* source_type = ir_type_from_id(&selector->program->types, function->values[definition->operands[0].value].canonical_type);
            IrType* target_type = ir_type_from_id(&selector->program->types, definition->canonical_type);
            bool cast_preserves_small_nonnegative =
                source_type && target_type && source_type->kind == IR_TYPE_INTEGER && target_type->kind == IR_TYPE_INTEGER &&
                target_type->bit_width >= 8 &&
                (definition->conversion_operation == IR_CONVERSION_IDENTITY || definition->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET ||
                 definition->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND ||
                 definition->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                 definition->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE);
            if (cast_preserves_small_nonnegative)
            {
                value = definition->operands[0];
            }
            else
            {
                done = true;
            }
        }
    }
    return result;
}

// One three-register row into a fresh synthesized register.
BUSTER_GLOBAL_LOCAL u32 machine_a64_select_arithmetic_row(MachineA64Selector* selector, u16 opcode, u32 left_register, u32 right_register)
{
    u32 result = machine_a64_synthesize_register(selector);
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result),
                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                         .opcode = opcode,
                                     });
    return result;
}

// A 128-bit shift whose amount is a compile-time constant, in all three
// directions. Both halves ride the ordinary 64-bit shift rows: below 64
// the cross bits transfer through the inverse shift, at 64 and above the
// surviving half shifts by amount-64 (the variable shift's mod-64 wrap
// makes exactly 64 an identity move of that half), and the signed form
// fills the vacated high half with the sign replicated by an arithmetic
// shift of 63. Variable amounts use the separate masked MIR expansion.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_i128_shift(MachineA64Selector* selector, IrInstruction* instruction, u32 amount)
{
    IrFunction* function = selector->function;

    bool selected = false;
    u32 source_slot = instruction->operand_count >= 2 && instruction->operands[0].value < function->value_count
                          ? selector->value_stack_slots[instruction->operands[0].value]
                          : UINT32_MAX;
    u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
    if (source_slot != UINT32_MAX && result_slot != UINT32_MAX && amount <= 127)
    {
        IrBinaryOperation operation = instruction->binary_operation;
        selected = true;
        if (amount == 0)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, source_slot)},
                                                 .payload = 16,
                                                 .opcode = MACHINE_A64_COPY_FRAME_FROM_FRAME,
                                             });
        }
        else if (amount < 64)
        {
            u32 low = machine_a64_select_frame_load64(selector, source_slot, 0);
            u32 high = machine_a64_select_frame_load64(selector, source_slot, 8);
            u32 count = machine_a64_select_immediate_register(selector, amount);
            u32 inverse_count = machine_a64_select_immediate_register(selector, 64 - amount);
            bool left = operation == IR_BINARY_SHIFT_LEFT;
            u32 shifted = machine_a64_select_arithmetic_row(selector, left ? MACHINE_A64_LSL64 : MACHINE_A64_LSR64,
                                                            left ? high : low, count);
            u32 cross = machine_a64_select_arithmetic_row(selector, left ? MACHINE_A64_LSR64 : MACHINE_A64_LSL64,
                                                          left ? low : high, inverse_count);
            u32 filled = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, shifted, cross);
            u16 kept_opcode = left ? MACHINE_A64_LSL64 : operation == IR_BINARY_SIGNED_SHIFT_RIGHT ? MACHINE_A64_ASR64 : MACHINE_A64_LSR64;
            u32 kept = machine_a64_select_arithmetic_row(selector, kept_opcode, left ? low : high, count);
            machine_a64_select_frame_store64(selector, result_slot, 0, left ? kept : filled);
            machine_a64_select_frame_store64(selector, result_slot, 8, left ? filled : kept);
        }
        else if (operation == IR_BINARY_SHIFT_LEFT)
        {
            u32 low = machine_a64_select_frame_load64(selector, source_slot, 0);
            u32 count = machine_a64_select_immediate_register(selector, amount - 64);
            u32 filled = machine_a64_synthesize_register(selector);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, filled),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, low),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, count)},
                                                 .opcode = MACHINE_A64_LSL64,
                                             });
            u32 zero = machine_a64_select_immediate_register(selector, 0);
            machine_a64_select_frame_store64(selector, result_slot, 0, zero);
            machine_a64_select_frame_store64(selector, result_slot, 8, filled);
        }
        else
        {
            u32 high = machine_a64_select_frame_load64(selector, source_slot, 8);
            u32 count = machine_a64_select_immediate_register(selector, amount - 64);
            u32 filled = machine_a64_synthesize_register(selector);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, filled),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, count)},
                                                 .opcode = (u16)(instruction->binary_operation == IR_BINARY_SIGNED_SHIFT_RIGHT ? MACHINE_A64_ASR64
                                                                                                                               : MACHINE_A64_LSR64),
                                             });
            u32 vacated;
            if (instruction->binary_operation == IR_BINARY_SIGNED_SHIFT_RIGHT)
            {
                u32 sixty_three = machine_a64_select_immediate_register(selector, 63);
                vacated = machine_a64_synthesize_register(selector);
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, vacated),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, sixty_three)},
                                                     .opcode = MACHINE_A64_ASR64,
                                                 });
            }
            else
            {
                vacated = machine_a64_select_immediate_register(selector, 0);
            }
            machine_a64_select_frame_store64(selector, result_slot, 0, filled);
            machine_a64_select_frame_store64(selector, result_slot, 8, vacated);
        }
    }
    return selected;
}

// A 128-bit integer shift in any direction: both operand and result are
// slot-backed pairs.
BUSTER_GLOBAL_LOCAL bool machine_a64_binary_is_i128_shift(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool matched = false;
    if (instruction->operand_count >= 2 && instruction->operands[0].value < function->value_count && instruction->result.value < function->value_count)
    {
        IrType* left_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        IrType* result_type = ir_type_from_id(&program->types, function->values[instruction->result.value].canonical_type);
        matched = left_type && result_type && left_type->kind == IR_TYPE_INTEGER && result_type->kind == IR_TYPE_INTEGER &&
                  left_type->bit_width == 128 && result_type->bit_width == 128 &&
                  (instruction->binary_operation == IR_BINARY_SHIFT_LEFT || instruction->binary_operation == IR_BINARY_SIGNED_SHIFT_RIGHT ||
                   instruction->binary_operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT);
    }
    return matched;
}

// Both 128-bit operands as slot-backed pairs: true only when the two value
// slots exist, filling the four half registers.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_i128_halves(MachineA64Selector* selector, IrInstruction* instruction, u32* left_low, u32* left_high,
                                                        u32* right_low, u32* right_high)
{
    IrFunction* function = selector->function;

    bool selected = false;
    u32 left_slot = instruction->operands[0].value < function->value_count ? selector->value_stack_slots[instruction->operands[0].value] : UINT32_MAX;
    u32 right_slot = instruction->operands[1].value < function->value_count ? selector->value_stack_slots[instruction->operands[1].value] : UINT32_MAX;
    if (left_slot != UINT32_MAX && right_slot != UINT32_MAX)
    {
        *left_low = machine_a64_select_frame_load64(selector, left_slot, 0);
        *left_high = machine_a64_select_frame_load64(selector, left_slot, 8);
        *right_low = machine_a64_select_frame_load64(selector, right_slot, 0);
        *right_high = machine_a64_select_frame_load64(selector, right_slot, 8);
        selected = true;
    }
    return selected;
}

// One CMP64 + CSET pair into a fresh synthesized register. The compare is
// re-issued per CSET so every flags read sits directly behind its producer,
// the shape the scheduler's flag chain already models.
BUSTER_GLOBAL_LOCAL u32 machine_a64_select_compare_set(MachineA64Selector* selector, u32 left_register, u32 right_register, u32 condition)
{
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                         .opcode = MACHINE_A64_CMP64,
                                     });
    u32 result = machine_a64_synthesize_register(selector);
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result)},
                                         .payload = condition,
                                         .opcode = MACHINE_A64_CSET,
                                     });
    return result;
}

// Scalar conversion rows carry IEEE bit images in the ordinary GPR model.
BUSTER_GLOBAL_LOCAL u32 machine_a64_select_float_to_pair_step(MachineA64Selector* selector, u16 opcode, u32 source, u32 other, u32 payload)
{
    u32 result = machine_a64_synthesize_register(selector);
    MachineInstruction row = {
        .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result), machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source)},
        .opcode = opcode,
        .payload = payload,
    };
    if (other != UINT32_MAX)
    {
        row.operands[2] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, other);
    }
    machine_a64_select_row(selector, row);
    return result;
}

// Match the direct oracle's 2^64 magnitude split using ordinary scalar MIR.
// Widen f32 exactly before splitting. For representable finite inputs the high
// limb converts back to f64 exactly, so subtraction retains the complete low
// limb; FCVTZU supplies truncation toward zero. Restore signed values with an
// integer borrow, including -2^127 and negative values that truncate to zero.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_float_to_i128(MachineA64Selector* selector, IrInstruction* instruction,
                                                          IrType* source_type, IrType* target_type)
{
    IrFunction* function = selector->function;
    u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
    bool signed_value = instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER;
    u32 source = UINT32_MAX;
    bool selected = source_type && source_type->kind == IR_TYPE_FLOAT && (source_type->bit_width == 32 || source_type->bit_width == 64) &&
                    target_type && target_type->kind == IR_TYPE_INTEGER && target_type->bit_width == 128 && result_slot != UINT32_MAX &&
                    (signed_value || instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER) &&
                    machine_a64_operand_register(selector, instruction->operands[0], &source);
    if (selected)
    {
        if (source_type->bit_width == 32)
        {
            source = machine_a64_select_float_to_pair_step(selector, MACHINE_A64_CVT_F32_TO_F64, source, UINT32_MAX, 0);
        }
        u32 sign = UINT32_MAX;
        if (signed_value)
        {
            u32 sign_position = machine_a64_select_immediate_register(selector, 63);
            sign = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ASR64, source, sign_position);
            u32 magnitude_mask = machine_a64_select_immediate_register(selector, UINT64_C(0x7fffffffffffffff));
            source = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, source, magnitude_mask);
        }
        // FARITH payload bit 8 selects f64; its low byte selects mul/sub.
        u32 inverse_limb_scale = machine_a64_select_immediate_register(selector, UINT64_C(0x3bf0000000000000)); // 2^-64
        u32 scaled = machine_a64_select_float_to_pair_step(selector, MACHINE_A64_FARITH, source, inverse_limb_scale, 0x102);
        u32 high = machine_a64_select_float_to_pair_step(selector, MACHINE_A64_CVT_F64_TO_U64, scaled, UINT32_MAX, 0);
        u32 high_float = machine_a64_select_float_to_pair_step(selector, MACHINE_A64_CVT_U64_TO_F64, high, UINT32_MAX, 0);
        u32 limb_scale = machine_a64_select_immediate_register(selector, UINT64_C(0x43f0000000000000)); // 2^64
        u32 high_contribution = machine_a64_select_float_to_pair_step(selector, MACHINE_A64_FARITH, high_float, limb_scale, 0x102);
        u32 residual = machine_a64_select_float_to_pair_step(selector, MACHINE_A64_FARITH, source, high_contribution, 0x101);
        u32 low = machine_a64_select_float_to_pair_step(selector, MACHINE_A64_CVT_F64_TO_U64, residual, UINT32_MAX, 0);
        if (signed_value)
        {
            u32 inverted_low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, low, sign);
            u32 inverted_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, high, sign);
            u32 borrow = machine_a64_select_compare_set(selector, inverted_low, sign, MACHINE_A64_CONDITION_BELOW);
            low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, inverted_low, sign);
            high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, inverted_high, sign);
            high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, high, borrow);
        }
        machine_a64_select_frame_store64(selector, result_slot, 0, low);
        machine_a64_select_frame_store64(selector, result_slot, 8, high);
    }
    return selected;
}

// Unary pair arithmetic stays in ordinary scalar rows. The low half's
// nonzero test supplies the borrow for two's-complement negation.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_i128_unary(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;
    u32 source_slot = instruction->operand_count == 1 && instruction->operands[0].value < function->value_count
                          ? selector->value_stack_slots[instruction->operands[0].value] : UINT32_MAX;
    u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
    bool negate = instruction->unary_operation == IR_UNARY_INTEGER_NEGATE;
    bool count = instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS ||
                 instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS;
    bool selected = source_slot != UINT32_MAX && result_slot != UINT32_MAX &&
                    (negate || count || instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT);
    if (selected)
    {
        u32 low = machine_a64_select_frame_load64(selector, source_slot, 0);
        u32 high = machine_a64_select_frame_load64(selector, source_slot, 8);
        u32 result_low;
        u32 result_high;
        if (count)
        {
            // A zero primary limb selects 64 + the other limb's count.
            // CLZ(0) is 64 on AArch64, so both-zero also matches the direct
            // oracle's 128, without evaluating an undefined bit scan.
            bool leading = instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS;
            u32 primary = leading ? high : low;
            u32 secondary = leading ? low : high;
            u32 first_count = machine_a64_select_zero_count(selector, primary, UINT32_MAX, true, leading);
            u32 second_count = machine_a64_select_zero_count(selector, secondary, UINT32_MAX, true, leading);
            u32 zero = machine_a64_select_immediate_register(selector, 0);
            u32 width = machine_a64_select_immediate_register(selector, 64);
            u32 crossed_count = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ADD64, second_count, width);
            u32 first_zero = machine_a64_select_compare_set(selector, primary, zero, MACHINE_A64_CONDITION_EQUAL);
            u32 mask = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, zero, first_zero);
            u32 difference = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, first_count, crossed_count);
            u32 selected_difference = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, difference, mask);
            result_low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, first_count, selected_difference);
            result_high = zero;
        }
        else
        {
            u32 constant = machine_a64_select_immediate_register(selector, negate ? 0 : UINT64_MAX);
            u16 opcode = negate ? MACHINE_A64_SUB64 : MACHINE_A64_EOR64;
            result_low = machine_a64_select_arithmetic_row(selector, opcode, constant, low);
            result_high = machine_a64_select_arithmetic_row(selector, opcode, constant, high);
            if (negate)
            {
                u32 borrow = machine_a64_select_compare_set(selector, low, constant, MACHINE_A64_CONDITION_NOT_EQUAL);
                result_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, result_high, borrow);
            }
        }
        machine_a64_select_frame_store64(selector, result_slot, 0, result_low);
        machine_a64_select_frame_store64(selector, result_slot, 8, result_high);
    }
    return selected;
}

// Variable shifts use disjoint masks for counts below/above 64. A separate
// nonzero mask suppresses the cross term at count zero, where the hardware
// shift would otherwise wrap 64 to zero. All defined C counts (0..127) use
// the same bounded straight-line MIR expansion.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_i128_variable_shift(MachineA64Selector* selector, IrInstruction* instruction)
{
    u32 source_slot = selector->value_stack_slots[instruction->operands[0].value];
    u32 result_slot = selector->value_stack_slots[instruction->result.value];
    u32 count_slot = selector->value_stack_slots[instruction->operands[1].value];
    u32 count = UINT32_MAX;
    bool selected = source_slot != UINT32_MAX && result_slot != UINT32_MAX;
    if (selected)
    {
        if (count_slot != UINT32_MAX)
        {
            count = machine_a64_select_frame_load64(selector, count_slot, 0);
        }
        else
        {
            selected = machine_a64_operand_register(selector, instruction->operands[1], &count);
        }
    }
    if (selected)
    {
        u32 low = machine_a64_select_frame_load64(selector, source_slot, 0);
        u32 high = machine_a64_select_frame_load64(selector, source_slot, 8);
        u32 zero = machine_a64_select_immediate_register(selector, 0);
        u32 width = machine_a64_select_immediate_register(selector, 64);
        u32 ones = machine_a64_select_immediate_register(selector, UINT64_MAX);
        u32 small = machine_a64_select_compare_set(selector, count, width, MACHINE_A64_CONDITION_BELOW);
        u32 small_mask = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, zero, small);
        u32 large_mask = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, small_mask, ones);
        u32 nonzero = machine_a64_select_compare_set(selector, count, zero, MACHINE_A64_CONDITION_NOT_EQUAL);
        u32 nonzero_mask = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, zero, nonzero);
        u32 inverse = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, width, count);
        bool left = instruction->binary_operation == IR_BINARY_SHIFT_LEFT;
        bool signed_right = instruction->binary_operation == IR_BINARY_SIGNED_SHIFT_RIGHT;
        u32 kept = machine_a64_select_arithmetic_row(selector, left ? MACHINE_A64_LSL64 : (signed_right ? MACHINE_A64_ASR64 : MACHINE_A64_LSR64),
                                                     left ? low : high, count);
        u32 shifted = machine_a64_select_arithmetic_row(selector, left ? MACHINE_A64_LSL64 : MACHINE_A64_LSR64, left ? high : low, count);
        u32 cross = machine_a64_select_arithmetic_row(selector, left ? MACHINE_A64_LSR64 : MACHINE_A64_LSL64, left ? low : high, inverse);
        cross = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, cross, nonzero_mask);
        u32 filled = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, shifted, cross);
        filled = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, filled, small_mask);
        u32 transferred = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, kept, large_mask);
        filled = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, filled, transferred);
        u32 surviving = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, kept, small_mask);
        if (signed_right)
        {
            u32 sign_count = machine_a64_select_immediate_register(selector, 63);
            u32 sign = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ASR64, high, sign_count);
            sign = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, sign, large_mask);
            surviving = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, surviving, sign);
        }
        machine_a64_select_frame_store64(selector, result_slot, 0, left ? surviving : filled);
        machine_a64_select_frame_store64(selector, result_slot, 8, left ? filled : surviving);
    }
    return selected;
}

// Normalize the unsigned magnitude as a pair, then round its top 24/53 bits
// once using round/sticky/parity. Converting limbs independently would lose
// the low limb's sticky bit and double-round halfway inputs. A zero high limb
// selects the ordinary u64 conversion; no runtime branch or mutable vreg is
// introduced. All shifts use the existing AArch64 modulo-64 scalar rows.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_i128_to_float(MachineA64Selector* selector, IrInstruction* instruction,
                                                          IrType* source_type, IrType* target_type, u32 result_register)
{
    IrFunction* function = selector->function;
    u32 source_slot = instruction->operands[0].value < function->value_count ? selector->value_stack_slots[instruction->operands[0].value] : UINT32_MAX;
    bool signed_value = instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT;
    bool selected = source_type && source_type->kind == IR_TYPE_INTEGER && source_type->bit_width == 128 && source_slot != UINT32_MAX &&
                    target_type && target_type->kind == IR_TYPE_FLOAT && (target_type->bit_width == 32 || target_type->bit_width == 64) &&
                    result_register != UINT32_MAX && (signed_value || instruction->conversion_operation == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT);
    if (selected)
    {
        bool wide = target_type->bit_width == 64;
        u32 precision = wide ? 53u : 24u;
        u32 low = machine_a64_select_frame_load64(selector, source_slot, 0);
        u32 high = machine_a64_select_frame_load64(selector, source_slot, 8);
        u32 zero = machine_a64_select_immediate_register(selector, 0);
        u32 one = machine_a64_select_immediate_register(selector, 1);
        u32 width = machine_a64_select_immediate_register(selector, 64);
        u32 sign = zero;
        if (signed_value)
        {
            u32 sign_position = machine_a64_select_immediate_register(selector, 63);
            sign = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ASR64, high, sign_position);
            u32 inverted_low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, low, sign);
            u32 inverted_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, high, sign);
            u32 borrow = machine_a64_select_compare_set(selector, inverted_low, sign, MACHINE_A64_CONDITION_BELOW);
            low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, inverted_low, sign);
            high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, inverted_high, sign);
            high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, high, borrow);
        }
        u32 leading = machine_a64_select_zero_count(selector, high, UINT32_MAX, true, true);
        u32 inverse = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, width, leading);
        u32 cross = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSR64, low, inverse);
        u32 shifted = machine_a64_select_compare_set(selector, leading, zero, MACHINE_A64_CONDITION_NOT_EQUAL);
        u32 shifted_mask = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, zero, shifted);
        // CLZ(high)==0 would otherwise wrap the cross shift of 64 to zero.
        cross = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, cross, shifted_mask);
        u32 normalized_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSL64, high, leading);
        normalized_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, normalized_high, cross);
        u32 normalized_low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSL64, low, leading);
        u32 significand_shift = machine_a64_select_immediate_register(selector, 64 - precision);
        u32 significand = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSR64, normalized_high, significand_shift);
        u32 round_shift = machine_a64_select_immediate_register(selector, 63 - precision);
        u32 round = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSR64, normalized_high, round_shift);
        round = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, round, one);
        u32 sticky_shift = machine_a64_select_immediate_register(selector, precision + 1);
        u32 sticky_bits = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSL64, normalized_high, sticky_shift);
        sticky_bits = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, sticky_bits, normalized_low);
        u32 sticky = machine_a64_select_compare_set(selector, sticky_bits, zero, MACHINE_A64_CONDITION_NOT_EQUAL);
        u32 parity = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, significand, one);
        u32 increment = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, sticky, parity);
        increment = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, increment, round);
        u32 rounded = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ADD64, significand, increment);
        u32 has_high = machine_a64_select_compare_set(selector, high, zero, MACHINE_A64_CONDITION_NOT_EQUAL);
        u32 high_mask = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, zero, has_high);
        u32 difference = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, rounded, low);
        difference = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, difference, high_mask);
        u32 conversion_input = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, low, difference);
        u32 discard_bias = machine_a64_select_immediate_register(selector, 128 - precision);
        u32 discarded = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, discard_bias, leading);
        discarded = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, discarded, high_mask);
        // The rounded significand is exactly representable, even if the
        // rounding increment carries to 2^precision. Scale in the destination
        // format, avoiding a second rounding through f64 for f32 results.
        u32 converted = machine_a64_synthesize_register(selector);
        machine_a64_select_row(selector, (MachineInstruction){
            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, converted), machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, conversion_input)},
            .opcode = (u16)(wide ? MACHINE_A64_CVT_U64_TO_F64 : MACHINE_A64_CVT_U64_TO_F32),
        });
        u32 exponent_shift = machine_a64_select_immediate_register(selector, precision - 1);
        u32 scale_exponent = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSL64, discarded, exponent_shift);
        u32 unit_bits = machine_a64_select_immediate_register(selector, wide ? UINT64_C(0x3ff0000000000000) : UINT64_C(0x3f800000));
        u32 scale = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ADD64, unit_bits, scale_exponent);
        u32 scaled = signed_value ? machine_a64_synthesize_register(selector) : result_register;
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled), machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, converted),
                         machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scale)},
            .opcode = MACHINE_A64_FARITH,
            .payload = (wide ? 0x100u : 0u) | 2u,
        });
        if (signed_value)
        {
            u32 sign_bit = machine_a64_select_immediate_register(selector, wide ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000));
            u32 float_sign = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, sign, sign_bit);
            row = machine_a64_select_row(selector, (MachineInstruction){
                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register), machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled),
                             machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, float_sign)},
                .opcode = MACHINE_A64_EOR64,
            });
        }
        machine_a64_define(selector, result_register, row);
    }
    return selected;
}

// The 128-bit binary subset over slot-backed pairs: the bitwise trio and
// add/subtract per half (the carry and borrow are CSET booleans off the
// low halves, so no flags-carrying add row is needed), equality through
// per-half XOR, and the ordered comparisons as the strict high-half
// relation blended with the unsigned low-half relation behind high-half
// equality. Multiply adds the cross products to UMULH of the low halves.
// The restoring loop has five SSA parameters: shifted dividend/quotient,
// partial remainder, and remaining bit count. All invariant values dominate
// the loop and all evolving values arrive through parallel edge copies.
#define MACHINE_A64_I128_DIVIDE_PARAMETER_COUNT 5u

BUSTER_GLOBAL_LOCAL bool machine_a64_instruction_is_i128_divide(IrProgram* program, IrInstruction* instruction)
{
    IrBinaryOperation operation = instruction->binary_operation;
    bool result = false;
    if (instruction->opcode == IR_OPCODE_BINARY &&
        (operation == IR_BINARY_SIGNED_DIVIDE || operation == IR_BINARY_UNSIGNED_DIVIDE ||
         operation == IR_BINARY_SIGNED_REMAINDER || operation == IR_BINARY_UNSIGNED_REMAINDER))
    {
        IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
        result = type && type->kind == IR_TYPE_INTEGER && type->bit_width == 128;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void machine_a64_select_i128_apply_sign(MachineA64Selector* selector, u32 sign, u32* low, u32* high)
{
    u32 inverted_low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, *low, sign);
    u32 inverted_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, *high, sign);
    u32 borrow = machine_a64_select_compare_set(selector, inverted_low, sign, MACHINE_A64_CONDITION_BELOW);
    *low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, inverted_low, sign);
    *high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, inverted_high, sign);
    *high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, *high, borrow);
}

BUSTER_GLOBAL_LOCAL void machine_a64_select_i128_divide_edge(MachineA64Selector* selector, u32 source, u32 destination, u32 const* values)
{
    u32 copy_offset = selector->builder.edge_copy_sources.total_count;
    for (u32 index = 0; index < MACHINE_A64_I128_DIVIDE_PARAMETER_COUNT; index += 1)
    {
        machine_builder_edge_copy_source(&selector->builder, machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, values[index]));
    }
    machine_builder_edge(&selector->builder, (MachineEdge){.source_block = source, .destination_block = destination,
                                                         .copy_offset = copy_offset, .copy_count = MACHINE_A64_I128_DIVIDE_PARAMETER_COUNT});
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_i128_divide(MachineA64Selector* selector, IrInstruction* instruction, u32 result_slot)
{
    u32 left_low;
    u32 left_high;
    u32 right_low;
    u32 right_high;
    bool selected = false;
    if (result_slot != UINT32_MAX && machine_a64_select_i128_halves(selector, instruction, &left_low, &left_high, &right_low, &right_high))
    {
        bool signed_value = instruction->binary_operation == IR_BINARY_SIGNED_DIVIDE || instruction->binary_operation == IR_BINARY_SIGNED_REMAINDER;
        bool remainder_result = instruction->binary_operation == IR_BINARY_SIGNED_REMAINDER || instruction->binary_operation == IR_BINARY_UNSIGNED_REMAINDER;
        u32 zero = machine_a64_select_immediate_register(selector, 0);
        u32 one = machine_a64_select_immediate_register(selector, 1);
        u32 top_bit = machine_a64_select_immediate_register(selector, 63);
        u32 count = machine_a64_select_immediate_register(selector, 128);
        u32 result_sign = zero;
        if (signed_value)
        {
            u32 left_sign = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ASR64, left_high, top_bit);
            u32 right_sign = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ASR64, right_high, top_bit);
            machine_a64_select_i128_apply_sign(selector, left_sign, &left_low, &left_high);
            machine_a64_select_i128_apply_sign(selector, right_sign, &right_low, &right_high);
            result_sign = remainder_result ? left_sign : machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, left_sign, right_sign);
        }
        u32 entry = selector->builder.open_block;
        u32 loop = entry + 1;
        u32 continuation = entry + 2;
        u32 initial[MACHINE_A64_I128_DIVIDE_PARAMETER_COUNT] = {left_low, left_high, zero, zero, count};
        machine_a64_select_i128_divide_edge(selector, entry, loop, initial);
        machine_a64_select_row(selector, (MachineInstruction){.operands = {machine_ref_make(MACHINE_REF_BLOCK, loop)}, .opcode = MACHINE_A64_B});
        machine_builder_block_end(&selector->builder, selector->open_block);
        machine_builder_block_begin(&selector->builder);
        selector->open_block = (MachineBlock){.parameter_offset = selector->builder.block_parameters.total_count,
                                             .parameter_count = MACHINE_A64_I128_DIVIDE_PARAMETER_COUNT};
        u32 state[MACHINE_A64_I128_DIVIDE_PARAMETER_COUNT];
        for (u32 index = 0; index < MACHINE_A64_I128_DIVIDE_PARAMETER_COUNT; index += 1)
        {
            state[index] = machine_builder_virtual_register(&selector->builder, (MachineVirtualRegister){.definition_point = MACHINE_POINT_INVALID,
                                                                                                       .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                                                       .typed_origin = IR_ID_UNDERLYING_INVALID});
            machine_builder_block_parameter(&selector->builder, (MachineBlockParameter){.virtual_register = state[index]});
        }
        // Shift one dividend bit into the partial remainder. Its prefix has
        // at most 128 bits, so this shift cannot lose a 129th remainder bit.
        u32 dividend_bit = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSR64, state[1], top_bit);
        u32 remainder_cross = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSR64, state[2], top_bit);
        u32 remainder_low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSL64, state[2], one);
        remainder_low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, remainder_low, dividend_bit);
        u32 remainder_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSL64, state[3], one);
        remainder_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, remainder_high, remainder_cross);
        u32 high_above = machine_a64_select_compare_set(selector, remainder_high, right_high, MACHINE_A64_CONDITION_ABOVE);
        u32 high_equal = machine_a64_select_compare_set(selector, remainder_high, right_high, MACHINE_A64_CONDITION_EQUAL);
        u32 low_enough = machine_a64_select_compare_set(selector, remainder_low, right_low, MACHINE_A64_CONDITION_ABOVE_EQUAL);
        u32 subtract = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, high_equal, low_enough);
        subtract = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, high_above, subtract);
        u32 mask = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, zero, subtract);
        u32 subtract_low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, right_low, mask);
        u32 subtract_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, right_high, mask);
        u32 borrow = machine_a64_select_compare_set(selector, remainder_low, subtract_low, MACHINE_A64_CONDITION_BELOW);
        u32 next[MACHINE_A64_I128_DIVIDE_PARAMETER_COUNT];
        next[2] = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, remainder_low, subtract_low);
        next[3] = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, remainder_high, subtract_high);
        next[3] = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, next[3], borrow);
        u32 quotient_cross = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSR64, state[0], top_bit);
        next[0] = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSL64, state[0], one);
        next[0] = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, next[0], subtract);
        next[1] = machine_a64_select_arithmetic_row(selector, MACHINE_A64_LSL64, state[1], one);
        next[1] = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, next[1], quotient_cross);
        next[4] = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, state[4], one);
        machine_a64_select_row(selector, (MachineInstruction){.operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, next[4])},
                                                            .opcode = MACHINE_A64_CMP_ZERO});
        machine_a64_select_row(selector, (MachineInstruction){.operands = {machine_ref_make(MACHINE_REF_BLOCK, loop),
                                                                         machine_ref_make(MACHINE_REF_BLOCK, continuation)},
                                                            .opcode = MACHINE_A64_BCC, .payload = MACHINE_A64_CONDITION_NOT_EQUAL});
        machine_a64_select_i128_divide_edge(selector, loop, loop, next);
        machine_builder_edge(&selector->builder, (MachineEdge){.source_block = loop, .destination_block = continuation});
        machine_builder_block_end(&selector->builder, selector->open_block);
        machine_builder_block_begin(&selector->builder);
        selector->open_block = (MachineBlock){0};
        u32 result_low = next[remainder_result ? 2 : 0];
        u32 result_high = next[remainder_result ? 3 : 1];
        if (signed_value)
        {
            machine_a64_select_i128_apply_sign(selector, result_sign, &result_low, &result_high);
        }
        machine_a64_select_frame_store64(selector, result_slot, 0, result_low);
        machine_a64_select_frame_store64(selector, result_slot, 8, result_high);
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_i128_binary(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrFunction* function = selector->function;

    bool selected = false;
    IrBinaryOperation operation = instruction->binary_operation;
    u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
    if (machine_a64_instruction_is_i128_divide(selector->program, instruction))
    {
        selected = machine_a64_select_i128_divide(selector, instruction, result_slot);
    }
    else if (machine_a64_binary_is_i128_shift(selector, instruction))
    {
        u32 amount;
        selected = machine_a64_constant_shift_amount(selector, instruction->operands[1], &amount)
                       ? machine_a64_select_i128_shift(selector, instruction, amount)
                       : machine_a64_select_i128_variable_shift(selector, instruction);
    }
    else if ((operation == IR_BINARY_INTEGER_BITWISE_AND || operation == IR_BINARY_INTEGER_BITWISE_OR || operation == IR_BINARY_INTEGER_BITWISE_XOR) &&
             result_slot != UINT32_MAX)
    {
        u32 left_low;
        u32 left_high;
        u32 right_low;
        u32 right_high;
        if (machine_a64_select_i128_halves(selector, instruction, &left_low, &left_high, &right_low, &right_high))
        {
            u16 opcode = operation == IR_BINARY_INTEGER_BITWISE_AND  ? MACHINE_A64_AND64
                         : operation == IR_BINARY_INTEGER_BITWISE_OR ? MACHINE_A64_ORR64
                                                                     : MACHINE_A64_EOR64;
            machine_a64_select_frame_store64(selector, result_slot, 0, machine_a64_select_arithmetic_row(selector, opcode, left_low, right_low));
            machine_a64_select_frame_store64(selector, result_slot, 8, machine_a64_select_arithmetic_row(selector, opcode, left_high, right_high));
            selected = true;
        }
    }
    else if ((operation == IR_BINARY_INTEGER_ADD || operation == IR_BINARY_INTEGER_SUBTRACT) && result_slot != UINT32_MAX)
    {
        u32 left_low;
        u32 left_high;
        u32 right_low;
        u32 right_high;
        if (machine_a64_select_i128_halves(selector, instruction, &left_low, &left_high, &right_low, &right_high))
        {
            if (operation == IR_BINARY_INTEGER_ADD)
            {
                // low' = low_a + low_b; the carry is low' <u low_a.
                u32 sum_low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ADD64, left_low, right_low);
                u32 carry = machine_a64_select_compare_set(selector, sum_low, left_low, MACHINE_A64_CONDITION_BELOW);
                u32 sum_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ADD64, left_high, right_high);
                machine_a64_select_frame_store64(selector, result_slot, 0, sum_low);
                machine_a64_select_frame_store64(selector, result_slot, 8, machine_a64_select_arithmetic_row(selector, MACHINE_A64_ADD64, sum_high, carry));
            }
            else
            {
                // The borrow is low_a <u low_b, read before the halves move.
                u32 borrow = machine_a64_select_compare_set(selector, left_low, right_low, MACHINE_A64_CONDITION_BELOW);
                u32 difference_low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, left_low, right_low);
                u32 difference_high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, left_high, right_high);
                machine_a64_select_frame_store64(selector, result_slot, 0, difference_low);
                machine_a64_select_frame_store64(selector, result_slot, 8,
                                                 machine_a64_select_arithmetic_row(selector, MACHINE_A64_SUB64, difference_high, borrow));
            }
            selected = true;
        }
    }
    else if (operation == IR_BINARY_INTEGER_MULTIPLY && result_slot != UINT32_MAX)
    {
        u32 left_low;
        u32 left_high;
        u32 right_low;
        u32 right_high;
        if (machine_a64_select_i128_halves(selector, instruction, &left_low, &left_high, &right_low, &right_high))
        {
            u32 low = machine_a64_select_arithmetic_row(selector, MACHINE_A64_MUL64, left_low, right_low);
            u32 high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_UMULH64, left_low, right_low);
            u32 cross_left = machine_a64_select_arithmetic_row(selector, MACHINE_A64_MUL64, left_low, right_high);
            u32 cross_right = machine_a64_select_arithmetic_row(selector, MACHINE_A64_MUL64, left_high, right_low);
            high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ADD64, high, cross_left);
            high = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ADD64, high, cross_right);
            machine_a64_select_frame_store64(selector, result_slot, 0, low);
            machine_a64_select_frame_store64(selector, result_slot, 8, high);
            selected = true;
        }
    }
    else if ((operation == IR_BINARY_INTEGER_EQUAL || operation == IR_BINARY_INTEGER_NOT_EQUAL) && result_register != UINT32_MAX)
    {
        u32 left_low;
        u32 left_high;
        u32 right_low;
        u32 right_high;
        if (machine_a64_select_i128_halves(selector, instruction, &left_low, &left_high, &right_low, &right_high))
        {
            u32 low_difference = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, left_low, right_low);
            u32 high_difference = machine_a64_select_arithmetic_row(selector, MACHINE_A64_EOR64, left_high, right_high);
            u32 any_difference = machine_a64_select_arithmetic_row(selector, MACHINE_A64_ORR64, low_difference, high_difference);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, any_difference)},
                                                 .opcode = MACHINE_A64_CMP_ZERO,
                                             });
            u32 row = machine_a64_select_row(
                selector, (MachineInstruction){
                              .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                              .payload = operation == IR_BINARY_INTEGER_EQUAL ? MACHINE_A64_CONDITION_EQUAL : MACHINE_A64_CONDITION_NOT_EQUAL,
                              .opcode = MACHINE_A64_CSET,
                          });
            machine_a64_define(selector, result_register, row);
            selected = true;
        }
    }
    else if ((operation == IR_BINARY_SIGNED_LESS || operation == IR_BINARY_SIGNED_LESS_EQUAL || operation == IR_BINARY_SIGNED_GREATER ||
              operation == IR_BINARY_SIGNED_GREATER_EQUAL || operation == IR_BINARY_UNSIGNED_LESS || operation == IR_BINARY_UNSIGNED_LESS_EQUAL ||
              operation == IR_BINARY_UNSIGNED_GREATER || operation == IR_BINARY_UNSIGNED_GREATER_EQUAL) &&
             result_register != UINT32_MAX)
    {
        u32 left_low;
        u32 left_high;
        u32 right_low;
        u32 right_high;
        if (machine_a64_select_i128_halves(selector, instruction, &left_low, &left_high, &right_low, &right_high))
        {
            // The high halves decide with the signed or unsigned strict
            // relation; only exactly equal high halves defer to the low
            // halves, which always compare unsigned.
            bool less_directed = operation == IR_BINARY_SIGNED_LESS || operation == IR_BINARY_SIGNED_LESS_EQUAL ||
                                 operation == IR_BINARY_UNSIGNED_LESS || operation == IR_BINARY_UNSIGNED_LESS_EQUAL;
            bool signed_compare = operation == IR_BINARY_SIGNED_LESS || operation == IR_BINARY_SIGNED_LESS_EQUAL ||
                                  operation == IR_BINARY_SIGNED_GREATER || operation == IR_BINARY_SIGNED_GREATER_EQUAL;
            bool or_equal = operation == IR_BINARY_SIGNED_LESS_EQUAL || operation == IR_BINARY_UNSIGNED_LESS_EQUAL ||
                            operation == IR_BINARY_SIGNED_GREATER_EQUAL || operation == IR_BINARY_UNSIGNED_GREATER_EQUAL;
            u32 high_condition = signed_compare ? (less_directed ? MACHINE_A64_CONDITION_LESS : MACHINE_A64_CONDITION_GREATER)
                                                : (less_directed ? MACHINE_A64_CONDITION_BELOW : MACHINE_A64_CONDITION_ABOVE);
            u32 low_condition = less_directed ? (or_equal ? MACHINE_A64_CONDITION_BELOW_EQUAL : MACHINE_A64_CONDITION_BELOW)
                                              : (or_equal ? MACHINE_A64_CONDITION_ABOVE_EQUAL : MACHINE_A64_CONDITION_ABOVE);
            u32 high_decides = machine_a64_select_compare_set(selector, left_high, right_high, high_condition);
            u32 high_equal = machine_a64_select_compare_set(selector, left_high, right_high, MACHINE_A64_CONDITION_EQUAL);
            u32 low_decides = machine_a64_select_compare_set(selector, left_low, right_low, low_condition);
            u32 deferred = machine_a64_select_arithmetic_row(selector, MACHINE_A64_AND64, high_equal, low_decides);
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, high_decides),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, deferred)},
                                                           .opcode = MACHINE_A64_ORR64,
                                                       });
            machine_a64_define(selector, result_register, row);
            selected = true;
        }
    }
    return selected;
}

// Scalar arithmetic is shared by ordinary values and the legal per-lane
// expansion of vector operations without a matching NEON form.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_scalar_binary(MachineA64Selector* selector, IrTypeId operand_type_id, u8 operation,
                                                          u32 left_register, u32 right_register, u32 result_register)
{
    IrProgram* program = selector->program;
    bool selected = false;
    IrType* operand_type = ir_type_from_id(&program->types, operand_type_id);
    if (machine_a64_type_is_scalar_register(operand_type))
    {
        // `wide` folds every width above 32 into the 64-bit rows, which
        // would wrap a 128-bit shift's amount modulo 64 and drop every
        // other operation's high half. The scalar-register guard above
        // (size <= 8) is what keeps 128-bit operands off this table:
        // i128 values are slot-backed with no operand register, so any
        // i128 binary outside the constant-shift path above refuses here
        // and the function falls back whole to the canonical emitter,
        // which owns the remaining 128-bit pair lowerings.
        bool wide = machine_a64_type_is_64_bit(program, operand_type_id);
        u16 arithmetic = 0;
        switch (operation)
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
            bool remainder = arithmetic == MACHINE_A64_SREM32 || arithmetic == MACHINE_A64_SREM64 || arithmetic == MACHINE_A64_UREM32 ||
                             arithmetic == MACHINE_A64_UREM64;
            if (remainder)
            {
                machine_a64_select_remainder_expansion(selector, arithmetic, wide, result_register, left_register, right_register);
            }
            else
            {
                u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                               .opcode = arithmetic,
                                                           });
                machine_a64_define(selector, result_register, row);
            }
            selected = true;
        }
        else
        {
            u32 condition = machine_a64_condition_from_comparison(operation);
            if (condition != UINT32_MAX)
            {
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
                selected = true;
            }
        }
    }
    else if (operand_type && operand_type->kind == IR_TYPE_FLOAT && (operand_type->bit_width == 32 || operand_type->bit_width == 64))
    {
        // Scalar float arithmetic and comparison over the bit-image
        // model. The compare conditions follow fcmp's flag encoding for
        // unordered-false C semantics — MI/LS for less/less-equal, GT/GE
        // for greater/greater-equal — exactly the canonical emitter's
        // table, so NaN needs no separate repair rows.
        u32 float_wide_bit = operand_type->bit_width == 64 ? 0x100u : 0;
        u32 arith_selector = UINT32_MAX;
        u32 compare_condition = UINT32_MAX;
        switch (operation)
        {
        case IR_BINARY_FLOAT_ADD:
            arith_selector = 0;
            break;
        case IR_BINARY_FLOAT_SUBTRACT:
            arith_selector = 1;
            break;
        case IR_BINARY_FLOAT_MULTIPLY:
            arith_selector = 2;
            break;
        case IR_BINARY_FLOAT_DIVIDE:
            arith_selector = 3;
            break;
        case IR_BINARY_FLOAT_EQUAL:
            compare_condition = 0x0;
            break;
        case IR_BINARY_FLOAT_NOT_EQUAL:
            compare_condition = 0x1;
            break;
        case IR_BINARY_FLOAT_LESS:
            compare_condition = 0x4;
            break;
        case IR_BINARY_FLOAT_LESS_EQUAL:
            compare_condition = 0x9;
            break;
        case IR_BINARY_FLOAT_GREATER:
            compare_condition = 0xc;
            break;
        case IR_BINARY_FLOAT_GREATER_EQUAL:
            compare_condition = 0xa;
            break;
        default:
            break;
        }
        if (arith_selector != UINT32_MAX || compare_condition != UINT32_MAX)
        {
            u32 row = machine_a64_select_row(
                selector, (MachineInstruction){
                              .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                           machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                           machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                              .payload = (arith_selector != UINT32_MAX ? arith_selector : compare_condition) | float_wide_bit,
                              .opcode = (u16)(arith_selector != UINT32_MAX ? MACHINE_A64_FARITH : MACHINE_A64_FCMP_SET),
                          });
            machine_a64_define(selector, result_register, row);
            selected = true;
        }
    }
    return selected;
}

// Exact-width lane loads never read past the vector's slot. Signed narrow
// lanes extend before division, remainder, comparisons, and arithmetic shifts.
BUSTER_GLOBAL_LOCAL u32 machine_a64_select_vector_lane_load(MachineA64Selector* selector, u32 slot, u32 offset, IrType* element)
{
    u32 address = machine_a64_synthesize_register(selector);
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address),
                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                         .payload = offset,
                                         .opcode = MACHINE_A64_LEA_FRAME,
                                     });
    u32 value = machine_a64_synthesize_register(selector);
    u16 load = element->bit_width == 8   ? MACHINE_A64_LOAD_PTR8
               : element->bit_width == 16 ? MACHINE_A64_LOAD_PTR16
               : element->bit_width == 32 ? MACHINE_A64_LOAD_PTR32
                                          : MACHINE_A64_LOAD_PTR64;
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value),
                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address)},
                                         .opcode = load,
                                     });
    if (element->kind == IR_TYPE_INTEGER && element->is_signed && element->bit_width < 32)
    {
        u32 extended = machine_a64_synthesize_register(selector);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, extended),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value)},
                                             .opcode = (u16)(element->bit_width == 8 ? MACHINE_A64_SXTB : MACHINE_A64_SXTH),
                                         });
        value = extended;
    }
    return value;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_vector_lanes(MachineA64Selector* selector, IrInstruction* instruction, IrType* vector)
{
    IrFunction* function = selector->function;
    IrType* element = ir_type_from_id(&selector->program->types, vector->element_type);
    bool unary = instruction->opcode == IR_OPCODE_UNARY;
    u32 left_slot = selector->value_stack_slots[instruction->operands[0].value];
    u32 right_slot = unary ? UINT32_MAX : selector->value_stack_slots[instruction->operands[1].value];
    u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
    bool selected = element && ((element->kind == IR_TYPE_INTEGER &&
                                 (element->bit_width == 8 || element->bit_width == 16 || element->bit_width == 32 || element->bit_width == 64)) ||
                                (element->kind == IR_TYPE_FLOAT && (element->bit_width == 32 || element->bit_width == 64))) &&
                    vector->layout.resolved && vector->layout.size <= INT32_MAX && vector->element_count &&
                    (u64)(element->bit_width / 8) * vector->element_count == vector->layout.size &&
                    left_slot != UINT32_MAX && (unary || right_slot != UINT32_MAX) && result_slot != UINT32_MAX;
    u8 operation = IR_BINARY_COUNT;
    if (selected && !unary)
    {
        static u8 const scalar_operations[] = {
            IR_BINARY_INTEGER_ADD, IR_BINARY_INTEGER_SUBTRACT, IR_BINARY_INTEGER_MULTIPLY,
            IR_BINARY_SIGNED_DIVIDE, IR_BINARY_UNSIGNED_DIVIDE,
            IR_BINARY_FLOAT_ADD, IR_BINARY_FLOAT_SUBTRACT, IR_BINARY_FLOAT_MULTIPLY, IR_BINARY_FLOAT_DIVIDE,
            IR_BINARY_SIGNED_REMAINDER, IR_BINARY_UNSIGNED_REMAINDER,
            IR_BINARY_SHIFT_LEFT, IR_BINARY_SIGNED_SHIFT_RIGHT, IR_BINARY_UNSIGNED_SHIFT_RIGHT,
            IR_BINARY_INTEGER_BITWISE_AND, IR_BINARY_INTEGER_BITWISE_OR, IR_BINARY_INTEGER_BITWISE_XOR,
            IR_BINARY_INTEGER_EQUAL, IR_BINARY_INTEGER_NOT_EQUAL,
            IR_BINARY_SIGNED_LESS, IR_BINARY_SIGNED_LESS_EQUAL, IR_BINARY_SIGNED_GREATER, IR_BINARY_SIGNED_GREATER_EQUAL,
            IR_BINARY_UNSIGNED_LESS, IR_BINARY_UNSIGNED_LESS_EQUAL, IR_BINARY_UNSIGNED_GREATER, IR_BINARY_UNSIGNED_GREATER_EQUAL,
            IR_BINARY_FLOAT_EQUAL, IR_BINARY_FLOAT_NOT_EQUAL, IR_BINARY_FLOAT_LESS, IR_BINARY_FLOAT_LESS_EQUAL,
            IR_BINARY_FLOAT_GREATER, IR_BINARY_FLOAT_GREATER_EQUAL,
        };
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(scalar_operations) == IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL - IR_BINARY_VECTOR_INTEGER_ADD + 1);
        u32 index = (u32)instruction->binary_operation - IR_BINARY_VECTOR_INTEGER_ADD;
        selected = index < BUSTER_ARRAY_LENGTH(scalar_operations);
        if (selected)
        {
            operation = scalar_operations[index];
        }
    }
    for (u32 lane = 0; selected && lane < vector->element_count; lane += 1)
    {
        u32 offset = lane * (element->bit_width / 8);
        u32 left = machine_a64_select_vector_lane_load(selector, left_slot, offset, element);
        u32 result_register = machine_a64_synthesize_register(selector);
        MachineVirtualRegister* result_record = selector->builder.virtual_register_cursor - 1;
        bool wide = element->bit_width == 64;
        if (unary)
        {
            bool negate = instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE;
            bool invert = instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT;
            bool float_negate = instruction->unary_operation == IR_UNARY_VECTOR_FLOAT_NEGATE;
            selected = ((negate || invert) && element->kind == IR_TYPE_INTEGER) || (float_negate && element->kind == IR_TYPE_FLOAT);
            if (selected && float_negate)
            {
                u32 sign = machine_a64_select_immediate_register(selector, wide ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000));
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, sign)},
                                                     .opcode = (u16)(wide ? MACHINE_A64_EOR64 : MACHINE_A64_EOR32),
                                                 });
            }
            else if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left)},
                                                     .opcode = (u16)(negate ? (wide ? MACHINE_A64_NEG64 : MACHINE_A64_NEG32)
                                                                            : (wide ? MACHINE_A64_NOT64 : MACHINE_A64_NOT32)),
                                                 });
            }
        }
        else
        {
            u32 right = machine_a64_select_vector_lane_load(selector, right_slot, offset, element);
            selected = machine_a64_select_scalar_binary(selector, vector->element_type, operation, left, right, result_register);
        }
        if (selected)
        {
            // Scalar helpers finish with the result's defining row; mask
            // constants, operand loads and remainder intermediates precede it.
            // Keep this arena-stable record so no stream search is needed.
            result_record->definition_point = machine_point_make(selector->builder.instructions.total_count - 1, MACHINE_POINT_AFTER);
        }
        if (selected && !unary && instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL)
        {
            // C vector comparisons return an all-ones lane for true.
            u32 mask = machine_a64_synthesize_register(selector);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                 .opcode = (u16)(wide ? MACHINE_A64_NEG64 : MACHINE_A64_NEG32),
                                             });
            result_register = mask;
        }
        if (selected)
        {
            u16 store = element->bit_width == 8   ? MACHINE_A64_STORE_FRAME8
                        : element->bit_width == 16 ? MACHINE_A64_STORE_FRAME16
                        : element->bit_width == 32 ? MACHINE_A64_STORE_FRAME32
                                                   : MACHINE_A64_STORE_FRAME64;
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                 .payload = offset,
                                                 .opcode = store,
                                             });
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_binary(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 left_register;
    u32 right_register;
    // Vector operands are slot-backed with no result register: they
    // dispatch before the register path, and the type read is safe only
    // behind its own id bound-checks.
    IrType* vector_operand_type = instruction->operand_count >= 2 && instruction->operands[0].value < function->value_count &&
                                          instruction->operands[1].value < function->value_count
                                      ? ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type)
                                      : 0;
    if (vector_operand_type && vector_operand_type->kind == IR_TYPE_VECTOR)
    {
        selected = machine_a64_select_vector_binary(selector, instruction, vector_operand_type);
        if (!selected)
        {
            selected = machine_a64_select_vector_lanes(selector, instruction, vector_operand_type);
        }
    }
    else if (vector_operand_type && vector_operand_type->kind == IR_TYPE_INTEGER && vector_operand_type->bit_width == 128)
    {
        selected = machine_a64_select_i128_binary(selector, instruction, result_register);
    }
    // The operand lookups bound the value ids, which is what makes the type
    // read below safe; nothing here may be hoisted above them.
    else if (result_register != UINT32_MAX && machine_a64_operand_register(selector, instruction->operands[0], &left_register) &&
        machine_a64_operand_register(selector, instruction->operands[1], &right_register))
    {
        IrTypeId operand_type_id = function->values[instruction->operands[0].value].canonical_type;
        selected = machine_a64_select_scalar_binary(selector, operand_type_id, instruction->binary_operation,
                                                    left_register, right_register, result_register);
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_dereference(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    bool selected = false;
    u32 source_register;
    if (result_register != UINT32_MAX && machine_a64_operand_register(selector, instruction->operands[0], &source_register))
    {
        // Aliased through the pointer chain: the dereference is a
        // name for the promoted local, not code.
        if (result_register != source_register)
        {
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                           .opcode = MACHINE_A64_MOV_RR,
                                                       });
            machine_a64_define(selector, result_register, row);
        }
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_address_of(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    return result_register != UINT32_MAX && machine_a64_select_place_address_offset(selector, instruction->operands[0], result_register, 0);
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_field(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    MachineSelectionAddress address = machine_a64_address(selector, instruction->result);
    bool selected = false;
    if (result_register != UINT32_MAX && (address.flags & MACHINE_SELECTION_ADDRESS_FIELD) && address.field_offset <= INT32_MAX)
    {
        selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], result_register, (u32)address.field_offset);
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL u32 machine_a64_call_target_append(MachineA64Selector* selector, IrSymbolId symbol, bool address)
{
    u32 target_index = selector->call_targets.total_count;
    bool page = address && (selector->target.os == OPERATING_SYSTEM_MACOS || selector->target.os == OPERATING_SYSTEM_IOS);
    MachineA64CallTarget* row = (MachineA64CallTarget*)machine_stream_append(selector->arena, &selector->call_targets);
    *row = (MachineA64CallTarget){.symbol = symbol, .reference = (u8)(page ? MACHINE_SYMBOL_REFERENCE_MACH_PAGE : MACHINE_SYMBOL_REFERENCE_DIRECT)};
    return target_index;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_global_address(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;

    bool selected = false;
    IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
    bool thread_local_global = symbol && instruction->opcode == IR_OPCODE_GLOBAL && symbol->is_thread_local;
    bool darwin = selector->target.os == OPERATING_SYSTEM_MACOS || selector->target.os == OPERATING_SYSTEM_IOS;
    bool windows = selector->target.os == OPERATING_SYSTEM_WINDOWS;
    bool thread_local_supported = darwin || windows ||
        selector->target.os == OPERATING_SYSTEM_LINUX || selector->target.os == OPERATING_SYSTEM_ANDROID;
    if (result_register != UINT32_MAX && symbol && thread_local_global && thread_local_supported)
    {
        u32 target_index = machine_a64_call_target_append(selector, instruction->symbol, false);
        u32 row;
        if (darwin)
        {
            machine_a64_select_row(selector, (MachineInstruction){.payload = target_index, .opcode = MACHINE_A64_TLS_DARWIN});
            row = machine_a64_select_row(selector, (MachineInstruction){
                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                             machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_A64_X0)},
                .opcode = MACHINE_A64_MOV_RR});
        }
        else
        {
            row = machine_a64_select_row(selector, (MachineInstruction){
                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                .payload = target_index, .opcode = (u16)(windows ? MACHINE_A64_TLS_WINDOWS : MACHINE_A64_LEA_TLS)});
        }
        machine_a64_define(selector, result_register, row);
        selected = true;
    }
    else if (result_register != UINT32_MAX && symbol && !thread_local_global)
    {
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
        }
        else
        {
            u32 target_index = machine_a64_call_target_append(selector, instruction->symbol, true);
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                           .payload = target_index,
                                                           .opcode = MACHINE_A64_LEA_SYMBOL,
                                                       });
            machine_a64_define(selector, result_register, row);
        }
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_index(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    MachineSelectionAddress address = machine_a64_address(selector, instruction->result);

    bool selected = false;
    u32 index_register;
    if (result_register != UINT32_MAX && instruction->operand_count >= 2 &&
        machine_a64_operand_register(selector, instruction->operands[1], &index_register))
    {
        bool typed = (address.flags & MACHINE_SELECTION_ADDRESS_INDEX) && address.scale <= INT32_MAX;
        // The scaled index is computed in a synthesized temporary; signed
        // narrow indexes sign-extend before scaling.  Resolving the extend
        // before the base address keeps an unsupported index width from
        // emitting rows the rejection would throw away anyway.
        u16 extend_opcode = MACHINE_A64_MOV_RR;
        if (typed && (address.flags & MACHINE_SELECTION_ADDRESS_INDEX_SIGNED) && address.index_bit_width < 64)
        {
            extend_opcode = (u16)(address.index_bit_width == 8    ? MACHINE_A64_SXTB
                                  : address.index_bit_width == 16 ? MACHINE_A64_SXTH
                                  : address.index_bit_width == 32 ? MACHINE_A64_SXTW
                                                                : 0);
        }
        if (typed && extend_opcode && machine_a64_select_place_address_offset(selector, instruction->operands[0], result_register, 0))
        {
            u32 scaled_register = machine_a64_synthesize_register(selector);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, index_register)},
                                                 .opcode = extend_opcode,
                                             });
            if (address.scale != 1)
            {
                u32 immediate_index = selector->immediates.total_count;
                u64* immediate_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
                *immediate_row = address.scale;
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
            selected = true;
        }
    }
    return selected;
}

// Everything CALL selection resolves before it emits a row: who is called,
// where each argument travels, and how the result comes back.
typedef struct MachineA64CallPlan MachineA64CallPlan;
struct MachineA64CallPlan
{
    MachineA64ValueShape argument_shapes[MACHINE_A64_MAX_ARGUMENTS];
    MachineA64ArgumentPlacement argument_placements[MACHINE_A64_MAX_ARGUMENTS];
    u32 argument_registers[MACHINE_A64_MAX_ARGUMENTS];
    u32 argument_slots[MACHINE_A64_MAX_ARGUMENTS];
    MachineA64ValueShape return_shape;
    u32 argument_count;
    u32 stack_part_count;
    u32 callee_register;
    u32 indirect_result_slot;
    bool direct_call;
    bool returns_value;
};

// Count named parameters in their independent AAPCS64 register files.
// A named composite that spills exhausts that register file, just as at calls.
BUSTER_GLOBAL_LOCAL void machine_a64_va_named_cursors(MachineA64Selector* selector, u32* gp_count, u32* fp_count, u32* stack_parts)
{
    *gp_count = 0;
    *fp_count = 0;
    *stack_parts = 0;
    IrType* function_type = ir_type_from_id(&selector->program->types, selector->function->canonical_type);
    if (!function_type || function_type->kind != IR_TYPE_FUNCTION)
    {
        return;
    }
    for (u32 parameter_index = 0; parameter_index < function_type->parameter_count; parameter_index += 1)
    {
        IrTypeId parameter_type_id = function_type->parameter_types[parameter_index];
        IrType* parameter = ir_type_from_id(&selector->program->types, parameter_type_id);
        IrAbiValue abi = ir_type_abi_value(selector->program, parameter_type_id, ir_abi_convention_for_target(selector->target), IR_ABI_USE_ARGUMENT);
        if (abi.part_count && abi.parts[0].abi_class == IR_ABI_CLASS_FLOAT)
        {
            if (*fp_count + abi.part_count <= 8)
            {
                *fp_count += abi.part_count;
            }
            else
            {
                *fp_count = 8;
                *stack_parts += (u32)((parameter->layout.size + 7) / 8);
            }
            continue;
        }
        u32 part_count = 1;
        bool aggregate = codegen_canonical_integer_aggregate_parts(selector->program, parameter_type_id, &part_count);
        if (aggregate && parameter && parameter->layout.size > 16)
        {
            part_count = 1;
        }
        bool even_integer_pair =
            ir_abi_value_is_aarch64_even_integer_pair(selector->program, parameter_type_id, ir_abi_convention_for_target(selector->target),
                                                      IR_ABI_USE_ARGUMENT);
        if (even_integer_pair && *gp_count < 8)
        {
            *gp_count = (*gp_count + 1u) & ~1u;
        }
        if (*gp_count + part_count <= 8)
        {
            *gp_count += part_count;
        }
        else
        {
            if (aggregate)
            {
                *gp_count = 8;
            }
            if (even_integer_pair)
            {
                *stack_parts = (*stack_parts + 1u) & ~1u;
            }
            *stack_parts += part_count;
        }
    }
}

// Materialize the public ELF AAPCS64 image. The register-save pointers name
// the ends of their areas; signed byte offsets independently walk towards zero.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_va_start(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;
    u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
    bool selected = false;
    if (result_slot != UINT32_MAX && selector->va_register_save_slot != UINT32_MAX)
    {
        u32 gp_count;
        u32 fp_count;
        u32 stack_parts;
        machine_a64_va_named_cursors(selector, &gp_count, &fp_count, &stack_parts);
        u32 overflow_register = machine_a64_synthesize_register(selector);
        machine_a64_select_row(selector, (MachineInstruction){
            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, overflow_register),
                         machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_A64_X29)},
            .payload = 16u + stack_parts * 8u,
            .opcode = MACHINE_A64_LEA_OFFSET,
        });
        machine_a64_select_row(selector, (MachineInstruction){
            .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                         machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, overflow_register)},
            .payload = MACHINE_A64_VA_STACK_OFFSET,
            .opcode = MACHINE_A64_STORE_FRAME64,
        });
        u32 save_register = machine_a64_synthesize_register(selector);
        machine_a64_select_row(selector, (MachineInstruction){
            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, save_register),
                         machine_ref_make(MACHINE_REF_STACK_SLOT, selector->va_register_save_slot)},
            .opcode = MACHINE_A64_LEA_FRAME,
        });
        for (u32 file = 0; file < 2; file += 1)
        {
            u32 top_register = machine_a64_synthesize_register(selector);
            machine_a64_select_row(selector, (MachineInstruction){
                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, top_register),
                             machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, save_register)},
                .payload = file ? MACHINE_A64_VA_SAVE_BYTES : MACHINE_A64_VA_GP_SAVE_BYTES,
                .opcode = MACHINE_A64_LEA_OFFSET,
            });
            machine_a64_select_row(selector, (MachineInstruction){
                .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                             machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, top_register)},
                .payload = file ? MACHINE_A64_VA_VR_TOP_OFFSET : MACHINE_A64_VA_GR_TOP_OFFSET,
                .opcode = MACHINE_A64_STORE_FRAME64,
            });
        }
        s32 gp_offset = (s32)gp_count * 8 - (s32)MACHINE_A64_VA_GP_SAVE_BYTES;
        s32 fp_offset = (s32)fp_count * 16 - (s32)MACHINE_A64_VA_FP_SAVE_BYTES;
        u64 offsets = (u64)(u32)gp_offset | ((u64)(u32)fp_offset << 32);
        u32 cursor_register = machine_a64_synthesize_register(selector);
        u32 cursor_immediate = machine_a64_append_immediate(selector, offsets);
        machine_a64_select_row(selector, (MachineInstruction){
            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, cursor_register),
                         machine_ref_make(MACHINE_REF_IMMEDIATE, cursor_immediate)},
            .opcode = MACHINE_A64_MOV_RI,
        });
        machine_a64_select_row(selector, (MachineInstruction){
            .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                         machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, cursor_register)},
            .payload = MACHINE_A64_VA_GR_OFFS_OFFSET,
            .opcode = MACHINE_A64_STORE_FRAME64,
        });
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_va_copy(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;

    u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
    u32 source_register;
    bool selected = false;
    if (result_slot != UINT32_MAX && instruction->operand_count >= 1 && machine_a64_operand_register(selector, instruction->operands[0], &source_register))
    {
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                             .payload = 32,
                                             .opcode = MACHINE_A64_COPY_FRAME_FROM_PTR,
                                         });
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_va_end(MachineA64Selector* selector, IrInstruction* instruction)
{
    // va_end has no runtime resource to release; preserve both list cursors.
    u32 source_register;
    bool selected = instruction->operand_count >= 1 && machine_a64_operand_register(selector, instruction->operands[0], &source_register);
    return selected;
}

// Translates the canonical VA_ARG gate into VA_ARG side data: scalars and
// integer-part aggregates of at most sixteen bytes. Scalar floats use
// the independent Q cursor; integer parts use the X cursor. The canonical emitter
// errors the module on anything else, so the machine path must fall back
// on exactly the same set rather than shape a value canonical cannot.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_va_arg(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 source_register;
    if (instruction->operand_count >= 1 && instruction->result.value < function->value_count &&
        machine_a64_operand_register(selector, instruction->operands[0], &source_register))
    {
        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u32 result_slot = selector->value_stack_slots[instruction->result.value];
        bool result_is_frame = result_register == UINT32_MAX && result_slot != UINT32_MAX;
        u32 part_count = 1;
        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &part_count);
        IrAbiValue abi = ir_type_abi_value(program, instruction->canonical_type, ir_abi_convention_for_target(selector->target), IR_ABI_USE_VARIADIC_ARGUMENT);
        bool floating = abi.part_count && abi.parts[0].abi_class == IR_ABI_CLASS_FLOAT;
        if (floating) { part_count = abi.part_count; }
        bool shaped = value_type && value_type->layout.resolved && value_type->layout.size && value_type->layout.size <= 16 &&
                      (aggregate || value_type->kind == IR_TYPE_INTEGER || value_type->kind == IR_TYPE_BOOLEAN ||
                       value_type->kind == IR_TYPE_POINTER || value_type->kind == IR_TYPE_FLOAT) &&
                      part_count && part_count <= MACHINE_VA_ARG_PART_LIMIT;
        if (shaped && result_is_frame == aggregate && (aggregate || part_count == 1))
        {
            u32 metadata_index = selector->va_args.total_count;
            MachineVaArg* metadata_row = (MachineVaArg*)machine_stream_append(selector->arena, &selector->va_args);
            *metadata_row = (MachineVaArg){
                .size = (u32)value_type->layout.size,
                .alignment = ir_abi_value_is_aarch64_even_integer_pair(program, instruction->canonical_type,
                    ir_abi_convention_for_target(selector->target), IR_ABI_USE_VARIADIC_ARGUMENT) ? 16u : 8u,
                .stack_size = (u32)((value_type->layout.size + 7) & ~(u64)7),
                .part_count = part_count,
                .result_slot = result_slot,
                .result_is_frame = result_is_frame,
                .scalar_size = 8,
            };
            for (u32 part_index = 0; part_index < part_count; part_index += 1)
            {
                metadata_row->parts[part_index] = (MachineVaArgPart){
                    .value_offset = floating ? abi.parts[part_index].value_offset : part_index * 8u,
                    .save_offset = part_index * (floating ? 16u : 8u),
                    .is_float = floating,
                    .size = floating ? (u8)abi.parts[part_index].size : 8,
                };
            }
            u32 row_index = machine_a64_select_row(selector, (MachineInstruction){
                                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register),
                                                                              result_is_frame
                                                                                  ? machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot)
                                                                                  : machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                                 .payload = metadata_index,
                                                                 .opcode = MACHINE_A64_VA_ARG,
                                                             });
            if (!result_is_frame)
            {
                machine_a64_define(selector, result_register, row_index);
            }
            selected = true;
        }
    }
    return selected;
}

// Resolves the call against the callee's signature. Nothing here emits a row,
// so a plan that fails leaves the row stream untouched.
BUSTER_GLOBAL_LOCAL bool machine_a64_plan_call(MachineA64Selector* selector, IrInstruction* instruction, MachineA64CallPlan* plan)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    *plan = (MachineA64CallPlan){.callee_register = UINT32_MAX, .indirect_result_slot = UINT32_MAX};
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
            planned = plan->direct_call || machine_a64_operand_register(selector, instruction->operands[0], &plan->callee_register);
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
        bool variadic_call = callee_type && callee_type->kind == IR_TYPE_FUNCTION && callee_type->is_variadic;
        // Darwin's anonymous stack arguments and Windows' integer-only
        // variadic register file need distinct placement. ELF variadic
        // scalars travel in the same registers as named ones.
        bool separate_variadic_abi = selector->target.os == OPERATING_SYSTEM_MACOS || selector->target.os == OPERATING_SYSTEM_IOS ||
                                     target_uses_pe_unwind(selector->target);
        planned = !(variadic_call && separate_variadic_abi) && callee_type && callee_type->kind == IR_TYPE_FUNCTION &&
                  (variadic_call ? plan->argument_count >= callee_type->parameter_count : callee_type->parameter_count == plan->argument_count) &&
                  plan->argument_count <= MACHINE_A64_MAX_ARGUMENTS;
    }
    if (planned)
    {
        IrType* callee_return_type = ir_type_from_id(&program->types, callee_type->return_type);
        plan->returns_value = callee_return_type && callee_return_type->kind != IR_TYPE_VOID;
        planned = !plan->returns_value ||
                  machine_a64_value_shape(program, callee_type->return_type, selector->target, IR_ABI_USE_RESULT, &plan->return_shape);
    }
    if (planned && plan->return_shape.indirect)
    {
        if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            plan->indirect_result_slot = selector->value_stack_slots[instruction->result.value];
            planned = plan->indirect_result_slot != UINT32_MAX;
        }
        else
        {
            // An unused indirect result still needs backing storage.
            plan->indirect_result_slot = machine_a64_append_slot(selector, plan->return_shape.byte_size, 8);
        }
    }
    if (planned)
    {
        u32 call_integer_count = 0;
        u32 call_float_count = 0;
        for (u32 argument_index = 0; argument_index < plan->argument_count && planned; argument_index += 1)
        {
            IrTypeId argument_type_id = argument_index < callee_type->parameter_count
                                            ? callee_type->parameter_types[argument_index]
                                            : function->values[instruction->operands[argument_index + 1].value].canonical_type;
            planned = machine_a64_value_shape(program, argument_type_id, selector->target, IR_ABI_USE_ARGUMENT, plan->argument_shapes + argument_index) &&
                      machine_a64_place_argument(plan->argument_shapes + argument_index, &call_integer_count, &call_float_count, &plan->stack_part_count,
                                                 plan->argument_placements + argument_index);
            if (planned)
            {
                plan->argument_registers[argument_index] = UINT32_MAX;
                plan->argument_slots[argument_index] = selector->value_stack_slots[instruction->operands[argument_index + 1].value];
                planned = plan->argument_shapes[argument_index].aggregate
                              ? plan->argument_slots[argument_index] != UINT32_MAX
                              : machine_a64_operand_register(selector, instruction->operands[argument_index + 1],
                                                             plan->argument_registers + argument_index);
            }
        }
    }
    return planned;
}

// Explicit fixed-register argument staging: integer parts load directly into
// their X registers (never through a scratch that could disturb an already
// placed argument), and float parts bridge into their V registers, which no
// general-register write can touch. The hidden result pointer rides X8,
// outside the argument file.
BUSTER_GLOBAL_LOCAL void machine_a64_stage_call_arguments(MachineA64Selector* selector, MachineA64CallPlan* plan)
{
    // Defensive copies and their addresses come first, before any register
    // staging: the LEA row is an unconstrained simple-lane candidate whose
    // free register pick knows nothing about staged argument registers, so
    // it must never sit between an ABI staging row and the call. The
    // pointer vreg then stages through the same copy-to-fixed-physical
    // path every scalar argument uses.
    for (u32 argument_index = 0; argument_index < plan->argument_count; argument_index += 1)
    {
        MachineA64ValueShape* shape = plan->argument_shapes + argument_index;
        if (!shape->indirect)
        {
            continue;
        }
        // The callee is licensed to scribble on the memory behind an
        // indirect argument, so the pointer names a fresh copy rather than
        // the value's own slot (the canonical caller's shortcut). The copy
        // itself rides the X17 data scratch.
        u32 copy_slot = machine_a64_append_slot(selector, shape->byte_size, 16);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, copy_slot),
                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, plan->argument_slots[argument_index])},
                                             .payload = shape->byte_size,
                                             .opcode = MACHINE_A64_COPY_FRAME_FROM_FRAME,
                                         });
        u32 pointer_register = machine_a64_synthesize_register(selector);
        u32 pointer_row = machine_a64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                            machine_ref_make(MACHINE_REF_STACK_SLOT, copy_slot)},
                                                               .opcode = MACHINE_A64_LEA_FRAME,
                                                           });
        machine_a64_define(selector, pointer_register, pointer_row);
        plan->argument_registers[argument_index] = pointer_register;
    }
    // Outgoing stack parts write into the frame's own area next — its base
    // is where a call's stack pointer already points, so the stores are
    // ordinary frame stores and the stack pointer never moves. Each part
    // is one eight-byte image at its sequential offset, exactly the
    // canonical caller's layout: a scalar's value or an indirect
    // argument's copy address.
    if (plan->stack_part_count)
    {
        u32 call_outgoing_bytes = (plan->stack_part_count * 8u + 15u) & ~15u;
        if (selector->outgoing_slot == UINT32_MAX)
        {
            selector->outgoing_slot = machine_a64_append_slot(selector, call_outgoing_bytes, 16);
        }
        selector->outgoing_bytes = BUSTER_MAX(selector->outgoing_bytes, call_outgoing_bytes);
        for (u32 argument_index = 0; argument_index < plan->argument_count; argument_index += 1)
        {
            MachineA64ArgumentPlacement* argument_placement = plan->argument_placements + argument_index;
            if (!argument_placement->on_stack)
            {
                continue;
            }
            MachineA64ValueShape* stack_shape = plan->argument_shapes + argument_index;
            if (stack_shape->aggregate && !stack_shape->indirect)
            {
                // An aggregate, HFA, or vector on the stack is its
                // eightbyte images at sequential offsets, bounced through
                // fresh vregs from the value's slot — the pre-pass runs
                // before any register staging, so the bounces are free to
                // land anywhere.
                for (u32 part_index = 0; part_index < stack_shape->byte_size / 8; part_index += 1)
                {
                    u32 bounce_register = machine_a64_synthesize_register(selector);
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register),
                                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, plan->argument_slots[argument_index])},
                                                         .payload = part_index * 8u,
                                                         .opcode = MACHINE_A64_LOAD_FRAME,
                                                     });
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, selector->outgoing_slot),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bounce_register)},
                                                         .payload = ((u32)argument_placement->first_stack_part + part_index) * 8u,
                                                         .opcode = MACHINE_A64_STORE_FRAME64,
                                                     });
                }
                continue;
            }
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, selector->outgoing_slot),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan->argument_registers[argument_index])},
                                                 .payload = (u32)argument_placement->first_stack_part * 8u,
                                                 .opcode = MACHINE_A64_STORE_FRAME64,
                                             });
        }
    }
    if (plan->return_shape.indirect)
    {
        u32 result_pointer_register = machine_a64_synthesize_register(selector);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_pointer_register),
                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, plan->indirect_result_slot)},
                                             .opcode = MACHINE_A64_LEA_FRAME,
                                         });
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_A64_X8),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_pointer_register)},
                                             .opcode = MACHINE_A64_MOV_RR,
                                         });
    }
    for (u32 argument_index = 0; argument_index < plan->argument_count; argument_index += 1)
    {
        MachineA64ValueShape* shape = plan->argument_shapes + argument_index;
        u32 next_integer = plan->argument_placements[argument_index].first_integer;
        u32 next_float = plan->argument_placements[argument_index].first_float;
        if (plan->argument_placements[argument_index].on_stack)
        {
            // Already written into the outgoing area above.
            continue;
        }
        if (shape->indirect)
        {
            // The defensive copy and its address were built by the
            // pre-pass; the pointer stages exactly like a scalar.
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, next_integer),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan->argument_registers[argument_index])},
                                                 .opcode = MACHINE_A64_MOV_RR,
                                             });
            continue;
        }
        if (shape->vector)
        {
            // The vector image loads straight from the value's slot into
            // its V argument register — the whole register or its low
            // bytes per the shape's size; no general register moves.
            machine_a64_select_row(selector,
                                   machine_a64_vector_transfer_row(shape, plan->argument_slots[argument_index], next_float, false));
            continue;
        }
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
                                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, plan->argument_slots[argument_index])},
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
                                                                      machine_ref_make(MACHINE_REF_STACK_SLOT, plan->argument_slots[argument_index])},
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
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan->argument_registers[argument_index])},
                                                 .payload = next_float,
                                                 .opcode = MACHINE_A64_FMOV_TO_VEC,
                                             });
            continue;
        }
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, next_integer),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan->argument_registers[argument_index])},
                                             .opcode = MACHINE_A64_MOV_RR,
                                         });
    }}

// Collects the returned value: indirect results are already in their slot,
// aggregates come back in parts, and a scalar arrives in X0 or V0.
BUSTER_GLOBAL_LOCAL bool machine_a64_receive_call_result(MachineA64Selector* selector, IrInstruction* instruction, MachineA64CallPlan* plan,
                                                         u32 result_register)
{
    bool received = true;
    if (plan->return_shape.indirect)
    {
        // The callee already stored the value through the hidden
        // pointer into the result slot.
        received = true;
    }
    else if (plan->return_shape.vector)
    {
        u32 result_slot = selector->value_stack_slots[instruction->result.value];
        received = result_slot != UINT32_MAX;
        if (received)
        {
            machine_a64_select_row(selector, machine_a64_vector_transfer_row(&plan->return_shape, result_slot, 0, true));
        }
    }
    else if (plan->return_shape.aggregate)
    {
        u32 result_slot = selector->value_stack_slots[instruction->result.value];
        received = result_slot != UINT32_MAX;
        if (received)
        {
        u32 return_integer_index = 0;
        u32 return_float_index = 0;
        for (u32 part_index = 0; part_index < plan->return_shape.part_count; part_index += 1)
        {
            if (plan->return_shape.part_is_float[part_index])
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
                                  .payload = plan->return_shape.part_offsets[part_index],
                                  .opcode = (u16)(plan->return_shape.part_sizes[part_index] == 4 ? MACHINE_A64_STORE_FRAME32
                                                                                                  : MACHINE_A64_STORE_FRAME64),
                              });
                return_float_index += 1;
                continue;
            }
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                              machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, return_integer_index)},
                                                 .payload = plan->return_shape.part_offsets[part_index],
                                                 .opcode = MACHINE_A64_STORE_FRAME64,
                                             });
            return_integer_index += 1;
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
        if (plan->return_shape.part_is_float[0])
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
    return received;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_call(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    MachineA64CallPlan plan;
    bool selected = machine_a64_plan_call(selector, instruction, &plan);
    if (selected)
    {
        machine_a64_stage_call_arguments(selector, &plan);
        if (plan.direct_call)
        {
            u32 target_index = machine_a64_call_target_append(selector, instruction->symbol, false);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .payload = target_index,
                                                 .opcode = MACHINE_A64_CALL_DIRECT,
                                             });
        }
        else
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, plan.callee_register)},
                                                 .opcode = MACHINE_A64_CALL_INDIRECT,
                                             });
        }
        if (plan.returns_value && instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            selected = machine_a64_receive_call_result(selector, instruction, &plan, result_register);
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_debug_trap(MachineA64Selector* selector)
{
    machine_a64_select_row(selector, (MachineInstruction){.opcode = MACHINE_A64_BRK});
    return true;
}

// The aggregate form of a load: an exact-size copy into the result slot,
// either from a direct local's slot or through an address vreg.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_aggregate_load(MachineA64Selector* selector, IrInstruction* instruction, u32 place_opcode, u32 slot,
                                                           u32 result_slot)
{
    IrProgram* program = selector->program;

    bool selected = false;
    IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
    if (loaded_type && loaded_type->layout.resolved && loaded_type->layout.size <= UINT32_MAX)
    {
        if (place_opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                 .payload = (u32)loaded_type->layout.size,
                                                 .opcode = MACHINE_A64_COPY_FRAME_FROM_FRAME,
                                             });
            selected = true;
        }
        else if (place_opcode == IR_OPCODE_DEREFERENCE || place_opcode == IR_OPCODE_GLOBAL || place_opcode == IR_OPCODE_INDEX ||
                 place_opcode == IR_OPCODE_FIELD || machine_a64_local_is_indirect(selector, instruction->operands[0]))
        {
            u32 address_register;
            selected = machine_a64_operand_register(selector, instruction->operands[0], &address_register);
            if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                     .payload = (u32)loaded_type->layout.size,
                                                     .opcode = MACHINE_A64_COPY_FRAME_FROM_PTR,
                                                 });
            }
        }
    }
    return selected;
}

// The scalar form: a promoted local reads as a register, a direct local as a
// frame load, and anything address-shaped as a sized pointer load.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_register_load(MachineA64Selector* selector, IrInstruction* instruction, u32 place_opcode, u32 slot,
                                                           u32 result_register)
{
    IrProgram* program = selector->program;

    bool selected = false;
    u32 place_register = selector->value_virtual_registers[instruction->operands[0].value];
    if (place_opcode == IR_OPCODE_LOCAL && place_register != UINT32_MAX && !machine_a64_local_is_indirect(selector, instruction->operands[0]))
    {
        // Aliased, the load is a name for the local and not code; promoted
        // but not aliasable here, it is a register copy.
        if (result_register != place_register)
        {
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, place_register)},
                                                           .opcode = MACHINE_A64_MOV_RR,
                                                       });
            machine_a64_define(selector, result_register, row);
        }
        selected = true;
    }
    else if (place_opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
    {
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                       .opcode = MACHINE_A64_LOAD_FRAME,
                                                   });
        machine_a64_define(selector, result_register, row);
        selected = true;
    }
    else if (place_opcode == IR_OPCODE_DEREFERENCE || place_opcode == IR_OPCODE_GLOBAL || place_opcode == IR_OPCODE_INDEX ||
             place_opcode == IR_OPCODE_FIELD || machine_a64_local_is_indirect(selector, instruction->operands[0]))
    {
        u32 address_register;
        if (machine_a64_operand_register(selector, instruction->operands[0], &address_register))
        {
            IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
            u64 size = loaded_type && loaded_type->layout.resolved ? loaded_type->layout.size : 0;
            u16 opcode = size == 1   ? MACHINE_A64_LOAD_PTR8
                         : size == 2 ? MACHINE_A64_LOAD_PTR16
                         : size == 4 ? MACHINE_A64_LOAD_PTR32
                         : size == 8 ? MACHINE_A64_LOAD_PTR64
                                     : 0;
            if (opcode)
            {
                u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                               .opcode = opcode,
                                                           });
                machine_a64_define(selector, result_register, row);
                selected = true;
            }
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_load(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrFunction* function = selector->function;

    bool selected = false;
    if (instruction->operands[0].value < function->value_count && instruction->result.value != IR_ID_UNDERLYING_INVALID)
    {
        MachineSelectionAddress address = machine_a64_address(selector, instruction->operands[0]);
        if (address.opcode != IR_OPCODE_COUNT)
        {
            u32 place_opcode = address.opcode;
            u32 slot = selector->value_stack_slots[instruction->operands[0].value];
            u32 result_slot = selector->value_stack_slots[instruction->result.value];
            if (result_register != UINT32_MAX)
            {
                selected = machine_a64_select_register_load(selector, instruction, place_opcode, slot, result_register);
            }
            else if (result_slot != UINT32_MAX)
            {
                selected = machine_a64_select_aggregate_load(selector, instruction, place_opcode, slot, result_slot);
            }
        }
    }
    return selected;
}

// The aggregate form of a store: an exact-size copy out of the value slot,
// into a direct local's slot or through an address vreg.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_aggregate_store(MachineA64Selector* selector, IrInstruction* instruction, u32 place_opcode, u64 size,
                                                            u32 slot, u32 value_slot)
{
    bool selected = false;
    if (size && size <= UINT32_MAX)
    {
        if (place_opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                 .payload = (u32)size,
                                                 .opcode = MACHINE_A64_COPY_FRAME_FROM_FRAME,
                                             });
            selected = true;
        }
        else if (place_opcode == IR_OPCODE_DEREFERENCE || place_opcode == IR_OPCODE_GLOBAL || place_opcode == IR_OPCODE_INDEX ||
                 place_opcode == IR_OPCODE_FIELD || machine_a64_local_is_indirect(selector, instruction->operands[0]))
        {
            u32 address_register;
            selected = machine_a64_operand_register(selector, instruction->operands[0], &address_register);
            if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = (u32)size,
                                                     .opcode = MACHINE_A64_COPY_PTR_FROM_FRAME,
                                                 });
            }
        }
    }
    return selected;
}

// The scalar form: a promoted local takes a register copy, a direct local a
// frame store, and anything address-shaped a sized pointer store.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_register_store(MachineA64Selector* selector, IrInstruction* instruction, u32 place_opcode, u64 size,
                                                           u32 slot)
{
    bool selected = false;
    u32 value_register;
    u32 size_index = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : size == 8 ? 3 : UINT32_MAX;
    u32 place_register = selector->value_virtual_registers[instruction->operands[0].value];
    if (size_index != UINT32_MAX && machine_a64_operand_register(selector, instruction->operands[1], &value_register))
    {
        if (place_opcode == IR_OPCODE_LOCAL && place_register != UINT32_MAX && !machine_a64_local_is_indirect(selector, instruction->operands[0]))
        {
            // Promoted local: the store is a full-width register copy —
            // the same 64-bit image a direct-slot store writes, since the
            // register model keeps every value zero-extended.
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, place_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                           .opcode = MACHINE_A64_MOV_RR,
                                                       });
            machine_a64_define(selector, place_register, row);
            selected = true;
        }
        else if (place_opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
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
            selected = true;
        }
        else if (place_opcode == IR_OPCODE_DEREFERENCE || place_opcode == IR_OPCODE_GLOBAL || place_opcode == IR_OPCODE_INDEX ||
                 place_opcode == IR_OPCODE_FIELD || machine_a64_local_is_indirect(selector, instruction->operands[0]))
        {
            u32 address_register;
            selected = machine_a64_operand_register(selector, instruction->operands[0], &address_register);
            if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = (u16)(MACHINE_A64_STORE_PTR8 + size_index),
                                                 });
            }
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_store(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    if (instruction->operands[0].value < function->value_count && instruction->operands[1].value < function->value_count)
    {
        MachineSelectionAddress address = machine_a64_address(selector, instruction->operands[0]);
        if (address.opcode != IR_OPCODE_COUNT)
        {
            u32 place_opcode = address.opcode;
            IrType* stored_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
            u64 size = stored_type && stored_type->layout.resolved ? stored_type->layout.size : 0;
            u32 slot = selector->value_stack_slots[instruction->operands[0].value];
            u32 value_slot = selector->value_stack_slots[instruction->operands[1].value];
            if (value_slot != UINT32_MAX && selector->value_virtual_registers[instruction->operands[1].value] == UINT32_MAX)
            {
                selected = machine_a64_select_aggregate_store(selector, instruction, place_opcode, size, slot, value_slot);
            }
            else
            {
                selected = machine_a64_select_register_store(selector, instruction, place_opcode, size, slot);
            }
        }
    }
    return selected;
}

// One literal member write into a literal value's slot, mirroring the
// x86-64 machine_x64_select_member_write: a scalar member stores sized at
// its offset, and a slot-backed aggregate member copies from its own slot
// through the member's address. LEA_FRAME folds the member offset into its
// payload, so the address needs no separate add row here.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_member_write(MachineA64Selector* selector, u32 slot, u64 member_offset, u64 member_size, IrValueId operand)
{
    bool selected;
    u32 value_register = selector->value_virtual_registers[operand.value];
    u32 value_slot = selector->value_stack_slots[operand.value];
    if (value_register != UINT32_MAX || value_slot == UINT32_MAX)
    {
        u16 store_opcode = member_size == 1   ? MACHINE_A64_STORE_FRAME8
                           : member_size == 2 ? MACHINE_A64_STORE_FRAME16
                           : member_size == 4 ? MACHINE_A64_STORE_FRAME32
                           : member_size == 8 ? MACHINE_A64_STORE_FRAME64
                                              : 0;
        if (!store_opcode || !machine_a64_operand_register(selector, operand, &value_register))
        {
            selected = false;
        }
        else
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                 .payload = (u32)member_offset,
                                                 .opcode = store_opcode,
                                             });
            selected = true;
        }
    }
    else
    {
        u32 address_register = machine_a64_synthesize_register(selector);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                             .payload = (u32)member_offset,
                                             .opcode = MACHINE_A64_LEA_FRAME,
                                         });
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                             .payload = (u32)member_size,
                                             .opcode = MACHINE_A64_COPY_PTR_FROM_FRAME,
                                         });
        selected = true;
    }
    return selected;
}

// Every bit-field member sharing one storage unit ORs into a single
// register image before the unit stores once, exactly like the x86-64
// form — but in three-address rows: each intermediate is a fresh vreg, so
// every row stays single-definition. The shift materializes as a
// multiply by a power of two, keeping the x86-64 structure. The image starts
// from the unit's current bytes for the reason the x86-64 form states: a slid
// or narrowed unit also covers an ordinary member or a second unit, and the
// slot is zero-filled first, so merging is what keeps both.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_bit_field_unit(MachineA64Selector* selector, IrInstruction* instruction, IrType* type, u32 slot, u32 first,
                                                           u64 field_offset, u64 field_size, u8* member_emitted)
{
    IrFieldAccessPiece pieces[IR_FIELD_ACCESS_PIECE_CAPACITY];
    u32 piece_count = ir_field_access_pieces(field_size, pieces);
    bool selected = piece_count != 0;
    // Packed units may span 3, 5, 6, 7, or 9 bytes. Each legal access merges
    // only its intersection with each sibling, preserving adjacent members.
    // Ordinary units still accumulate all siblings and store just once.
    for (u32 piece_index = 0; piece_index < piece_count && selected; piece_index += 1)
    {
        u32 piece_low = (u32)pieces[piece_index].offset * 8;
        u32 piece_high = piece_low + (u32)pieces[piece_index].size * 8;
        u32 piece_offset = (u32)(field_offset + pieces[piece_index].offset);
        u16 load_opcode = pieces[piece_index].size == 1   ? MACHINE_A64_LOAD_PTR8
                          : pieces[piece_index].size == 2 ? MACHINE_A64_LOAD_PTR16
                          : pieces[piece_index].size == 4 ? MACHINE_A64_LOAD_PTR32
                                                         : MACHINE_A64_LOAD_PTR64;
        u16 store_opcode = pieces[piece_index].size == 1   ? MACHINE_A64_STORE_FRAME8
                           : pieces[piece_index].size == 2 ? MACHINE_A64_STORE_FRAME16
                           : pieces[piece_index].size == 4 ? MACHINE_A64_STORE_FRAME32
                                                          : MACHINE_A64_STORE_FRAME64;
        u32 unit_address = machine_a64_synthesize_register(selector);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_address),
                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                             .payload = piece_offset,
                                             .opcode = MACHINE_A64_LEA_FRAME,
                                         });
        u32 unit_register = machine_a64_synthesize_register(selector);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_address)},
                                             .opcode = load_opcode,
                                         });
        for (u32 sibling = first; sibling < instruction->operand_count && selected; sibling += 1)
        {
            u64 sibling_field = instruction->immediates[sibling];
            if (!member_emitted[sibling] && sibling_field < type->field_count && type->fields[sibling_field].is_bit_field &&
                type->fields[sibling_field].bit_width && type->fields[sibling_field].offset == field_offset &&
                ir_field_access_size(&selector->program->types, type->fields + sibling_field) == field_size)
            {
                u32 bit_offset = type->fields[sibling_field].bit_offset;
                u32 bit_width = type->fields[sibling_field].bit_width;
                selected = bit_width <= 64 && (u64)bit_offset + bit_width <= field_size * 8;
                u32 low = BUSTER_MAX(piece_low, bit_offset);
                u32 high = BUSTER_MIN(piece_high, bit_offset + bit_width);
                if (selected && low < high)
                {
                    u32 value_register;
                    selected = machine_a64_operand_register(selector, instruction->operands[sibling], &value_register);
                    if (selected)
                    {
                        u32 masked_register = value_register;
                        if (low > bit_offset)
                        {
                            u32 count_register = machine_a64_select_immediate_register(selector, low - bit_offset);
                            u32 shifted_register = machine_a64_synthesize_register(selector);
                            machine_a64_select_row(selector, (MachineInstruction){
                                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, shifted_register),
                                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register),
                                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, count_register)},
                                                                 .opcode = MACHINE_A64_LSR64,
                                                             });
                            masked_register = shifted_register;
                        }
                        u32 mask_register = machine_a64_select_immediate_register(selector, high - low == 64 ? UINT64_MAX : (((u64)1 << (high - low)) - 1));
                        u32 updated_register = machine_a64_synthesize_register(selector);
                        machine_a64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, updated_register),
                                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register),
                                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register)},
                                                             .opcode = MACHINE_A64_AND64,
                                                         });
                        masked_register = updated_register;
                        if (low > piece_low)
                        {
                            u32 scale_register = machine_a64_select_immediate_register(selector, (u64)1 << (low - piece_low));
                            u32 scaled_register = machine_a64_synthesize_register(selector);
                            machine_a64_select_row(selector, (MachineInstruction){
                                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scaled_register),
                                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register),
                                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scale_register)},
                                                                 .opcode = MACHINE_A64_MUL64,
                                                             });
                            masked_register = scaled_register;
                        }
                        u32 merged_register = machine_a64_synthesize_register(selector);
                        machine_a64_select_row(selector, (MachineInstruction){
                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, merged_register),
                                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register),
                                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register)},
                                                             .opcode = MACHINE_A64_ORR64,
                                                         });
                        unit_register = merged_register;
                    }
                }
                // Keep the siblings available until every piece has consumed
                // them; the outer initializer loop must then skip them all.
                if (selected && piece_index + 1 == piece_count)
                {
                    member_emitted[sibling] = 1;
                }
            }
        }
        if (selected)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register)},
                                                 .payload = piece_offset,
                                                 .opcode = store_opcode,
                                             });
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_aggregate(MachineA64Selector* selector, IrInstruction* instruction)
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
        u32 zero_register = machine_a64_synthesize_register(selector);
        u32 zero_fill_immediate = selector->immediates.total_count;
        u64* zero_fill_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
        *zero_fill_row = 0;
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, zero_register),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, zero_fill_immediate)},
                                             .opcode = MACHINE_A64_MOV_RI,
                                         });
        u64 zero_filled = 0;
        while (zero_filled < type->layout.size)
        {
            u64 zero_remaining = type->layout.size - zero_filled;
            u32 zero_chunk = zero_remaining >= 8 ? 8 : zero_remaining >= 4 ? 4 : zero_remaining >= 2 ? 2 : 1;
            u16 zero_store_opcode = zero_chunk == 1   ? MACHINE_A64_STORE_FRAME8
                                    : zero_chunk == 2 ? MACHINE_A64_STORE_FRAME16
                                    : zero_chunk == 4 ? MACHINE_A64_STORE_FRAME32
                                                      : MACHINE_A64_STORE_FRAME64;
            machine_a64_select_row(selector, (MachineInstruction){
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
            else if (type->fields[field_index].is_bit_field && !type->fields[field_index].bit_width)
            {
                // Zero-width separators affect layout but store no bits.
                member_emitted[index] = 1;
            }
            else if (!member_emitted[index])
            {
                u64 field_offset = type->fields[field_index].offset;
                // The unit a member is written through: a bit-field packing
                // left no room for a declared-type unit of carries a narrower
                // one, and every other field reads its own size back.
                u64 field_size = ir_field_access_size(&program->types, type->fields + field_index);
                if (!field_size || field_offset > INT32_MAX)
                {
                    selected = false;
                }
                else if (!type->fields[field_index].is_bit_field)
                {
                    selected = machine_a64_select_member_write(selector, slot, field_offset, field_size, instruction->operands[index]);
                    if (selected)
                    {
                        member_emitted[index] = 1;
                    }
                }
                else
                {
                    selected = machine_a64_select_bit_field_unit(selector, instruction, type, slot, index, field_offset, field_size, member_emitted);
                }
            }
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_array(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    // Element-by-element construction into the value's slot at scaled
    // offsets; the frontend materializes every element including the
    // zero tail, so position times element size covers the object.
    // Vector literals share the shape exactly — the canonical emitter
    // lowers both kinds through the same per-element copy loop, and a
    // vector's lanes are always 1/2/4/8-byte scalars the member write
    // stores sized.
    bool selected = false;
    IrType* type = ir_type_from_id(&program->types, function->values[instruction->result.value].canonical_type);
    u32 slot = selector->value_stack_slots[instruction->result.value];
    if (type && (type->kind == IR_TYPE_ARRAY || type->kind == IR_TYPE_VECTOR) && slot != UINT32_MAX)
    {
        IrType* element_type = ir_type_from_id(&program->types, type->element_type);
        u64 element_size = element_type && element_type->layout.resolved ? element_type->layout.size : 0;
        if (element_size && (u64)instruction->operand_count * element_size <= INT32_MAX)
        {
            selected = true;
            for (u32 index = 0; index < instruction->operand_count && selected; index += 1)
            {
                selected = machine_a64_select_member_write(selector, slot, (u64)index * element_size, element_size, instruction->operands[index]);
            }
        }
    }
    return selected;
}

// The canonical emitter's C11 mapping: acquire folds in consume and both
// halves of acquire-release/sequential, release the other half.
BUSTER_GLOBAL_LOCAL bool machine_a64_memory_order_acquires(u8 memory_order)
{
    return memory_order == IR_MEMORY_ORDER_CONSUME || memory_order == IR_MEMORY_ORDER_ACQUIRE || memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE ||
           memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_memory_order_releases(u8 memory_order)
{
    return memory_order == IR_MEMORY_ORDER_RELEASE || memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE || memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
}

// Keeps the low `byte_count` bytes of a register and zeroes the rest, the
// canonical a64_emit_keep_low_bytes contract: an atomic aggregate store
// writes the place's promoted width, so the bytes between the value's real
// size and that width must be zero, not value-slot residue. A full
// eight-byte count needs no row at all.
BUSTER_GLOBAL_LOCAL void machine_a64_select_keep_low_bytes(MachineA64Selector* selector, u32 data_register, u64 byte_count)
{
    if (byte_count < 8)
    {
        u16 extend_opcode = byte_count == 1 ? MACHINE_A64_UXTB : byte_count == 2 ? MACHINE_A64_UXTH : byte_count == 4 ? MACHINE_A64_MOV32_RR : 0;
        if (extend_opcode)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, data_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, data_register)},
                                                 .opcode = extend_opcode,
                                             });
        }
        else
        {
            u32 mask_register = machine_a64_synthesize_register(selector);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register),
                                                              machine_ref_make(MACHINE_REF_IMMEDIATE,
                                                                               machine_a64_append_immediate(selector, ((u64)1 << (byte_count * 8)) - 1))},
                                                 .opcode = MACHINE_A64_MOV_RI,
                                             });
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, data_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, data_register),
                                                              machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register)},
                                                 .opcode = MACHINE_A64_AND64,
                                             });
        }
    }
}

// Scalar atomic load: the place's address into a fresh register, one
// ldar-family word into the result. Every memory order takes the acquire
// form — stronger than relaxed asks for, and what keeps the row count at
// one. An aggregate result is slot-backed instead: the access width is the
// place's promoted size (#731), not the loaded value's. Up to eight bytes,
// ldar zero-extends and the full-slot store leaves padding zero. At sixteen
// bytes, the pair row reads, writes back, and retries through LDXP/STXP so
// the observed image is single-copy atomic on baseline AArch64. Sequential
// loads use STLXP for the readback operation's release half.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_atomic_load(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
    u64 size = loaded_type && loaded_type->layout.resolved ? loaded_type->layout.size : 0;
    if (result_register != UINT32_MAX && (size == 1 || size == 2 || size == 4 || size == 8) && instruction->operand_count >= 1)
    {
        u32 address_register = machine_a64_synthesize_register(selector);
        selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], address_register, 0);
        if (selected)
        {
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                           .payload = (u32)size,
                                                           .opcode = MACHINE_A64_ATOMIC_LOAD,
                                                       });
            machine_a64_define(selector, result_register, row);
        }
    }
    else if (result_register == UINT32_MAX && instruction->operand_count >= 1 && instruction->result.value != IR_ID_UNDERLYING_INVALID &&
             instruction->result.value < function->value_count && instruction->operands[0].value < function->value_count && loaded_type)
    {
        u32 result_slot = selector->value_stack_slots[instruction->result.value];
        IrType* place_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        u64 atomic_width = place_type && place_type->layout.resolved ? place_type->layout.size : 0;
        bool aggregate_kind = loaded_type->kind == IR_TYPE_STRUCT || loaded_type->kind == IR_TYPE_UNION;
        bool pair_kind = aggregate_kind || (loaded_type->kind == IR_TYPE_INTEGER && loaded_type->bit_width == 128);
        if (result_slot != UINT32_MAX && atomic_width == 16 && size > 8 && size <= 16 && pair_kind)
        {
            u32 address_register = machine_a64_synthesize_register(selector);
            selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], address_register, 0);
            if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot)},
                                                     .payload = (u32)size |
                                                                (instruction->memory_order != IR_MEMORY_ORDER_RELAXED
                                                                     ? MACHINE_A64_ATOMIC_PAIR_ACQUIRE
                                                                     : 0) |
                                                                (instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL
                                                                     ? MACHINE_A64_ATOMIC_PAIR_RELEASE
                                                                     : 0),
                                                     .opcode = MACHINE_A64_ATOMIC_LOAD_PAIR,
                                                 });
            }
        }
        else if (result_slot != UINT32_MAX && aggregate_kind &&
                 (atomic_width == 1 || atomic_width == 2 || atomic_width == 4 || atomic_width == 8))
        {
            u32 address_register = machine_a64_synthesize_register(selector);
            selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], address_register, 0);
            if (selected)
            {
                u32 data_register = machine_a64_synthesize_register(selector);
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, data_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register)},
                                                     .payload = (u32)atomic_width,
                                                     .opcode = MACHINE_A64_ATOMIC_LOAD,
                                                 });
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, data_register)},
                                                     .opcode = MACHINE_A64_STORE_FRAME64,
                                                 });
            }
        }
    }
    return selected;
}

// Scalar atomic store mirrors the load: one stlr-family word at the
// stored scalar's width. An aggregate value is slot-backed instead: the
// integer image keeps only the value's real bytes (the slot's tail may be
// residue while promoted padding must store as zero). Up to eight bytes one
// stlr writes the promoted width; a sixteen-byte image takes the exclusive
// pair replacement loop used by the canonical emitter.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_atomic_store(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 value_register;
    if (instruction->operand_count >= 2 && instruction->operands[1].value < function->value_count &&
        machine_a64_operand_register(selector, instruction->operands[1], &value_register))
    {
        IrType* stored_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
        u64 size = stored_type && stored_type->layout.resolved ? stored_type->layout.size : 0;
        if (size == 1 || size == 2 || size == 4 || size == 8)
        {
            u32 address_register = machine_a64_synthesize_register(selector);
            selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], address_register, 0);
            if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .payload = (u32)size,
                                                     .opcode = MACHINE_A64_ATOMIC_STORE,
                                                 });
            }
        }
    }
    else if (instruction->operand_count >= 2 && instruction->operands[0].value < function->value_count &&
             instruction->operands[1].value < function->value_count && selector->value_stack_slots[instruction->operands[1].value] != UINT32_MAX)
    {
        IrType* stored_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
        IrType* place_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        u64 stored_size = stored_type && stored_type->layout.resolved ? stored_type->layout.size : 0;
        u64 atomic_width = place_type && place_type->layout.resolved ? place_type->layout.size : 0;
        bool aggregate_kind = stored_type && (stored_type->kind == IR_TYPE_STRUCT || stored_type->kind == IR_TYPE_UNION);
        bool pair_kind = stored_type && (aggregate_kind || (stored_type->kind == IR_TYPE_INTEGER && stored_type->bit_width == 128));
        if (pair_kind && atomic_width == 16 && stored_size > 8 && stored_size <= 16)
        {
            u32 value_slot = selector->value_stack_slots[instruction->operands[1].value];
            u32 address_register = machine_a64_synthesize_register(selector);
            selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], address_register, 0);
            if (selected)
            {
                bool acquire = instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                bool release = instruction->memory_order == IR_MEMORY_ORDER_RELEASE ||
                               instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE ||
                               instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = (u32)stored_size | (acquire ? MACHINE_A64_ATOMIC_PAIR_ACQUIRE : 0) |
                                                                (release ? MACHINE_A64_ATOMIC_PAIR_RELEASE : 0),
                                                     .opcode = MACHINE_A64_ATOMIC_STORE_PAIR,
                                                 });
            }
        }
        else if (aggregate_kind && stored_size && stored_size <= 8 &&
                 (atomic_width == 1 || atomic_width == 2 || atomic_width == 4 || atomic_width == 8))
        {
            u32 value_slot = selector->value_stack_slots[instruction->operands[1].value];
            u32 data_register = machine_a64_synthesize_register(selector);
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, data_register),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                 .opcode = MACHINE_A64_LOAD_FRAME,
                                             });
            if (stored_size < atomic_width)
            {
                machine_a64_select_keep_low_bytes(selector, data_register, stored_size);
            }
            u32 address_register = machine_a64_synthesize_register(selector);
            selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], address_register, 0);
            if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, data_register)},
                                                     .payload = (u32)atomic_width,
                                                     .opcode = MACHINE_A64_ATOMIC_STORE,
                                                 });
            }
        }
    }
    return selected;
}

// RMW and CAS consume complete integer representations. Aggregate C11
// operations already convert through these bits in canonical IR; do not
// reinterpret smaller record storage as a sixteen-byte input here.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_atomic_pair_update(MachineA64Selector* selector, IrInstruction* instruction, bool compare_exchange)
{
    IrFunction* function = selector->function;
    IrType* value_type = ir_type_from_id(&selector->program->types, instruction->canonical_type);
    u32 operand_count = compare_exchange ? 3u : 2u;
    bool selected = value_type && value_type->kind == IR_TYPE_INTEGER && value_type->bit_width == 128 &&
                    value_type->layout.resolved && value_type->layout.size == 16 && instruction->operand_count == operand_count &&
                    instruction->result.value < function->value_count && instruction->operands[0].value < function->value_count &&
                    (compare_exchange || instruction->atomic_operation < IR_ATOMIC_OPERATION_COUNT);
    MachineInstruction row = {
        .opcode = compare_exchange ? MACHINE_A64_ATOMIC_CAS_PAIR : MACHINE_A64_ATOMIC_RMW_PAIR,
    };
    if (selected)
    {
        u32 result_slot = selector->value_stack_slots[instruction->result.value];
        selected = result_slot != UINT32_MAX;
        if (selected)
        {
            row.operands[1] = machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot);
        }
    }
    for (u32 operand_index = 1; selected && operand_index < operand_count; operand_index += 1)
    {
        u32 value = instruction->operands[operand_index].value;
        selected = value < function->value_count && selector->value_stack_slots[value] != UINT32_MAX;
        if (selected)
        {
            row.operands[operand_index + 1] = machine_ref_make(MACHINE_REF_STACK_SLOT, selector->value_stack_slots[value]);
        }
    }
    if (selected)
    {
        u32 address_register = machine_a64_synthesize_register(selector);
        selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], address_register, 0);
        if (selected)
        {
            bool acquire = machine_a64_memory_order_acquires(instruction->memory_order) ||
                           (compare_exchange && machine_a64_memory_order_acquires(instruction->failure_memory_order));
            bool release = machine_a64_memory_order_releases(instruction->memory_order);
            row.operands[0] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register);
            row.payload = 16u | (acquire ? MACHINE_A64_ATOMIC_PAIR_ACQUIRE : 0) | (release ? MACHINE_A64_ATOMIC_PAIR_RELEASE : 0);
            if (!compare_exchange)
            {
                row.payload |= (u32)instruction->atomic_operation << MACHINE_A64_ATOMIC_PAIR_OPERATION_SHIFT;
            }
            machine_a64_select_row(selector, row);
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_atomic_read_modify_write(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;

    bool selected = false;
    IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
    u64 size = value_type && value_type->layout.resolved ? value_type->layout.size : 0;
    // The canonical shape gate: integers always, pointers for the additive
    // pair, booleans and pointers for exchange.
    bool pointer_arithmetic = value_type && value_type->kind == IR_TYPE_POINTER &&
                              (instruction->atomic_operation == IR_ATOMIC_ADD || instruction->atomic_operation == IR_ATOMIC_SUBTRACT);
    bool kind_supported = value_type && (value_type->kind == IR_TYPE_INTEGER || pointer_arithmetic ||
                                         (instruction->atomic_operation == IR_ATOMIC_EXCHANGE &&
                                          (value_type->kind == IR_TYPE_BOOLEAN || value_type->kind == IR_TYPE_POINTER)));
    u32 operand_register;
    if (result_register != UINT32_MAX && kind_supported && (size == 1 || size == 2 || size == 4 || size == 8) &&
        instruction->atomic_operation < IR_ATOMIC_OPERATION_COUNT && instruction->operand_count >= 2 &&
        machine_a64_operand_register(selector, instruction->operands[1], &operand_register))
    {
        u32 address_register = machine_a64_synthesize_register(selector);
        selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], address_register, 0);
        if (selected)
        {
            u32 payload = (u32)instruction->atomic_operation << 8 |
                          (machine_a64_memory_order_releases(instruction->memory_order) ? 0x20u : 0) |
                          (machine_a64_memory_order_acquires(instruction->memory_order) ? 0x10u : 0) | (u32)size;
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, operand_register)},
                                                           .payload = payload,
                                                           .opcode = MACHINE_A64_ATOMIC_RMW,
                                                       });
            machine_a64_define(selector, result_register, row);
        }
    }
    else if (result_register == UINT32_MAX)
    {
        selected = machine_a64_select_atomic_pair_update(selector, instruction, false);
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_atomic_compare_exchange(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;

    bool selected = false;
    IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
    u64 size = value_type && value_type->layout.resolved ? value_type->layout.size : 0;
    u32 expected_register;
    u32 desired_register;
    if (result_register != UINT32_MAX && value_type && (value_type->kind == IR_TYPE_INTEGER || value_type->kind == IR_TYPE_POINTER) &&
        (size == 1 || size == 2 || size == 4 || size == 8) && instruction->operand_count >= 3 &&
        machine_a64_operand_register(selector, instruction->operands[1], &expected_register) &&
        machine_a64_operand_register(selector, instruction->operands[2], &desired_register))
    {
        u32 address_register = machine_a64_synthesize_register(selector);
        selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], address_register, 0);
        if (selected)
        {
            // The canonical acquire fold takes the failure order into
            // account as well; consume and sequential count, a plain
            // acquire-release failure order cannot occur.
            bool acquire = machine_a64_memory_order_acquires(instruction->memory_order) ||
                           instruction->failure_memory_order == IR_MEMORY_ORDER_CONSUME ||
                           instruction->failure_memory_order == IR_MEMORY_ORDER_ACQUIRE ||
                           instruction->failure_memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
            u32 payload = (machine_a64_memory_order_releases(instruction->memory_order) ? 0x20u : 0) | (acquire ? 0x10u : 0) | (u32)size;
            u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                           .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, expected_register),
                                                                        machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, desired_register)},
                                                           .payload = payload,
                                                           .opcode = MACHINE_A64_ATOMIC_CAS,
                                                       });
            machine_a64_define(selector, result_register, row);
        }
    }
    else if (result_register == UINT32_MAX)
    {
        selected = machine_a64_select_atomic_pair_update(selector, instruction, true);
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_clear_instruction_cache(MachineA64Selector* selector, IrInstruction* instruction)
{
    u32 begin;
    u32 end;
    bool selected = false;
    if (instruction->operand_count == 2 && machine_a64_operand_register(selector, instruction->operands[0], &begin) &&
        machine_a64_operand_register(selector, instruction->operands[1], &end))
    {
        machine_a64_select_row(selector, (MachineInstruction){
            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, begin), machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, end)},
            .opcode = MACHINE_A64_CLEAR_INSTRUCTION_CACHE,
        });
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_atomic_fence(MachineA64Selector* selector, IrInstruction* instruction)
{
    // Signal fences and relaxed thread fences emit nothing, exactly like
    // the canonical path.
    if (!instruction->atomic_signal_fence && instruction->memory_order != IR_MEMORY_ORDER_RELAXED)
    {
        machine_a64_select_row(selector, (MachineInstruction){
                                             .opcode = MACHINE_A64_ATOMIC_FENCE,
                                         });
    }
    return true;
}

// A GNU compiler barrier has no architectural instruction. Its machine row
// exists solely so scheduling cannot move memory operations across it.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_compiler_barrier(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrInstructionExtra extra = ir_instruction_extra(selector->function, ir_instruction_self_id(selector->function, instruction));
    bool selected = !extra.literal.length && !instruction->operand_count && !instruction->target_count && extra.clobber_count == 1 &&
                    string_equal(extra.clobbers[0], S8("memory"));
    if (selected)
    {
        machine_a64_select_row(selector, (MachineInstruction){.opcode = MACHINE_A64_COMPILER_BARRIER});
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL u32 machine_a64_block_entry(MachineA64Selector* selector, u32 canonical_block)
{
    return selector->block_entries ? selector->block_entries[canonical_block] : canonical_block;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_branch(MachineA64Selector* selector, IrInstruction* instruction)
{
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_BLOCK, machine_a64_block_entry(selector, instruction->targets[0].value))},
                                         .opcode = MACHINE_A64_B,
                                     });
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_label_address(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    bool selected = result_register != UINT32_MAX && instruction->target_count == 1 && instruction->targets &&
                    instruction->targets[0].value < selector->function->block_count;
    if (selected)
    {
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                          .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                          .payload = machine_a64_block_entry(selector, instruction->targets[0].value),
                                                          .opcode = MACHINE_A64_LEA_BLOCK,
                                                      });
        machine_a64_define(selector, result_register, row);
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_indirect_branch(MachineA64Selector* selector, IrInstruction* instruction)
{
    bool selected = instruction->operand_count == 1 && instruction->target_count != 0 && instruction->targets;
    u32 target_register = UINT32_MAX;
    if (selected)
    {
        selected = machine_a64_operand_register(selector, instruction->operands[0], &target_register);
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
            *target_row = (MachineSwitchCase){.target_block = machine_a64_block_entry(selector, instruction->targets[target_index].value)};
        }
        if (selected)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, target_register)},
                                                         .payload = first_target,
                                                         .flags = instruction->target_count,
                                                         .opcode = MACHINE_A64_INDIRECT_BRANCH,
                                                     });
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_fused_branch(MachineA64Selector* selector, IrInstruction* instruction, MachineA64BranchFusion* fusion)
{
    bool selected = false;
    u32 left_register;
    u32 right_register;
    if (machine_a64_operand_register(selector, (IrValueId){.value = fusion->left}, &left_register))
    {
        selected = true;
        if (fusion->right != UINT32_MAX)
        {
            selected = machine_a64_operand_register(selector, (IrValueId){.value = fusion->right}, &right_register);
            if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, right_register)},
                                                     .opcode = (u16)(fusion->wide ? MACHINE_A64_CMP64 : MACHINE_A64_CMP32),
                                                 });
            }
        }
        else
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, left_register)},
                                                 .opcode = MACHINE_A64_CMP_ZERO,
                                             });
        }
        if (selected)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_BLOCK, machine_a64_block_entry(selector, instruction->targets[0].value)),
                                                              machine_ref_make(MACHINE_REF_BLOCK, machine_a64_block_entry(selector, instruction->targets[1].value))},
                                                 .payload = fusion->condition,
                                                 .opcode = MACHINE_A64_BCC,
                                             });
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_branch_if(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;

    bool selected = false;
    MachineA64BranchFusion* fusion =
        instruction->operands[0].value < function->value_count ? selector->branch_fusions + instruction->operands[0].value : 0;
    if (fusion && fusion->condition != 0xff)
    {
        selected = machine_a64_select_fused_branch(selector, instruction, fusion);
    }
    else
    {
        u32 condition_register;
        if (machine_a64_operand_register(selector, instruction->operands[0], &condition_register))
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, condition_register)},
                                                 .opcode = MACHINE_A64_CMP_ZERO,
                                             });
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_BLOCK, machine_a64_block_entry(selector, instruction->targets[0].value)),
                                                              machine_ref_make(MACHINE_REF_BLOCK, machine_a64_block_entry(selector, instruction->targets[1].value))},
                                                 .payload = MACHINE_A64_CONDITION_NOT_EQUAL,
                                                 .opcode = MACHINE_A64_BCC,
                                             });
            selected = true;
        }
    }
    return selected;
}

// The canonical compare chain over the shared switch-case side table,
// mirroring the x86-64 selector: one terminator row whose payload indexes
// the first case and whose flags carry the count; the encoder expands the
// chain with each branch fixed up like any block edge.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_switch(MachineA64Selector* selector, IrInstruction* instruction)
{
    u32 condition_register;
    bool selected = false;
    if (machine_a64_operand_register(selector, instruction->operands[0], &condition_register) && instruction->target_count &&
        instruction->target_count == instruction->immediate_count + 1 && instruction->immediates)
    {
        // A case immediate carries the switched type's own bits, while a
        // register may hold that value extended past them -- the cast a
        // narrow switch takes to its promoted type emits `sxtb x, w`, which
        // sign-extends to 64.  Comparing a 32-bit type at 64 bits therefore
        // measures the extension rather than the value and `case -1` never
        // matches.  Compare at the type's width, which is what the x86-64
        // selector does with the same field.
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
                .target_block = machine_a64_block_entry(selector, instruction->targets[case_index].value),
                .compare_width = compare_width,
            };
        }
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, condition_register),
                                                          machine_ref_make(MACHINE_REF_BLOCK, machine_a64_block_entry(selector, instruction->targets[instruction->target_count - 1].value))},
                                             .payload = first_case,
                                             .opcode = MACHINE_A64_SWITCH,
                                             .flags = (u16)instruction->immediate_count,
                                         });
        selected = true;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_unreachable(MachineA64Selector* selector)
{
    // Control never reaches this terminator; brk keeps the block
    // verifier-well-formed, faults loudly if control ever arrives, and
    // matches the canonical bytes.
    machine_a64_select_row(selector, (MachineInstruction){
                                         .opcode = MACHINE_A64_UDF,
                                     });
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_return(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;

    bool selected = true;
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
            selected = value_slot != UINT32_MAX && selector->hidden_return_slot != UINT32_MAX;
            if (selected)
            {
                u32 pointer_register = machine_a64_synthesize_register(selector);
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, selector->hidden_return_slot)},
                                                     .opcode = MACHINE_A64_LOAD_FRAME,
                                                 });
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                  machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = selector->return_shape.exact_byte_size ? selector->return_shape.exact_byte_size
                                                                                                       : selector->return_shape.byte_size,
                                                     .opcode = MACHINE_A64_COPY_PTR_FROM_FRAME,
                                                 });
            }
        }
        else if (selector->return_shape.vector)
        {
            // The vector image loads from the value's slot into V0 — the
            // whole register or its low bytes per the shape's size — the
            // vector analog of the scalar X0 move.
            u32 value_slot = instruction->operands[0].value < function->value_count
                                 ? selector->value_stack_slots[instruction->operands[0].value]
                                 : UINT32_MAX;
            selected = value_slot != UINT32_MAX;
            if (selected)
            {
                machine_a64_select_row(selector, machine_a64_vector_transfer_row(&selector->return_shape, value_slot, 0, false));
            }
        }
        else if (selector->return_shape.aggregate)
        {
            u32 value_slot = instruction->operands[0].value < function->value_count
                                 ? selector->value_stack_slots[instruction->operands[0].value]
                                 : UINT32_MAX;
            selected = value_slot != UINT32_MAX;
            if (selected)
            {
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
        }
        else
        {
            u32 value_register;
            selected = machine_a64_operand_register(selector, instruction->operands[0], &value_register);
            if (selected && selector->return_shape.part_is_float[0])
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_A64_FMOV_TO_VEC,
                                                 });
            }
            else if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_A64_X0),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register)},
                                                     .opcode = MACHINE_A64_MOV_RR,
                                                 });
            }
        }
    }
    if (selected)
    {
        machine_a64_select_row(selector, (MachineInstruction){
                                             .opcode = MACHINE_A64_RET,
                                         });
    }
    return selected;
}

// Emits result-vreg definition rows for one typed instruction. Returns
// false when the construct falls outside the selected subset.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_instruction(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;
    bool selected = false;
    bool fused_dead = false;
    u32 result_register = UINT32_MAX;
    if (instruction->result.value != IR_ID_UNDERLYING_INVALID && instruction->result.value < function->value_count)
    {
        // A branch-fusion chain member selects into nothing: the branch
        // re-selects the compare at its own row, and the member's only
        // consumer is the chain. Every marked member is pure.
        fused_dead = selector->fused_dead[instruction->result.value];
        result_register = selector->value_virtual_registers[instruction->result.value];
    }
    if (fused_dead)
    {
        selected = true;
    }
    else
    {
        switch (instruction->opcode)
        {
        case IR_OPCODE_LOCAL:
            selected = machine_a64_select_local(selector, instruction);
            break;
        case IR_OPCODE_STACK_SAVE:
            selected = machine_a64_select_stack_save(selector, result_register);
            break;
        case IR_OPCODE_STACK_RESTORE:
            selected = machine_a64_select_stack_restore(selector, instruction);
            break;
        case IR_OPCODE_STACK_ALLOCATE:
            selected = machine_a64_select_stack_allocate(selector, instruction, result_register);
            break;
        case IR_OPCODE_ARGUMENT:
            selected = machine_a64_select_argument(selector, instruction, result_register);
            break;
        case IR_OPCODE_CONSTANT_INTEGER:
        case IR_OPCODE_CONSTANT_FLOAT:
            selected = machine_a64_select_constant(selector, instruction, result_register);
            break;
        case IR_OPCODE_CAST:
            selected = machine_a64_select_cast(selector, instruction, result_register);
            break;
        case IR_OPCODE_UNARY:
        {
            IrType* unary_type = ir_type_from_id(&selector->program->types, instruction->canonical_type);
            selected = unary_type && unary_type->kind == IR_TYPE_VECTOR
                           ? machine_a64_select_vector_lanes(selector, instruction, unary_type)
                           : unary_type && unary_type->kind == IR_TYPE_INTEGER && unary_type->bit_width == 128
                                 ? machine_a64_select_i128_unary(selector, instruction)
                           : machine_a64_select_unary(selector, instruction, result_register);
        }
        break;
        case IR_OPCODE_BINARY:
            selected = machine_a64_select_binary(selector, instruction, result_register);
            break;
        case IR_OPCODE_DEREFERENCE:
            selected = machine_a64_select_dereference(selector, instruction, result_register);
            break;
        case IR_OPCODE_ADDRESS_OF:
            selected = machine_a64_select_address_of(selector, instruction, result_register);
            break;
        case IR_OPCODE_FIELD:
            selected = machine_a64_select_field(selector, instruction, result_register);
            break;
        case IR_OPCODE_GLOBAL:
        case IR_OPCODE_FUNCTION:
            selected = machine_a64_select_global_address(selector, instruction, result_register);
            break;
        case IR_OPCODE_INDEX:
            selected = machine_a64_select_index(selector, instruction, result_register);
            break;
        case IR_OPCODE_CALL:
            selected = machine_a64_select_call(selector, instruction, result_register);
            break;
        case IR_OPCODE_VA_START:
            selected = machine_a64_select_va_start(selector, instruction);
            break;
        case IR_OPCODE_VA_COPY:
            selected = machine_a64_select_va_copy(selector, instruction);
            break;
        case IR_OPCODE_VA_END:
            selected = machine_a64_select_va_end(selector, instruction);
            break;
        case IR_OPCODE_VA_ARG:
            selected = machine_a64_select_va_arg(selector, instruction, result_register);
            break;
        case IR_OPCODE_DEBUG_TRAP:
            selected = machine_a64_select_debug_trap(selector);
            break;
        case IR_OPCODE_AGGREGATE:
            selected = machine_a64_select_aggregate(selector, instruction);
            break;
        case IR_OPCODE_ARRAY:
            selected = machine_a64_select_array(selector, instruction);
            break;
        case IR_OPCODE_LOAD:
            selected = machine_a64_select_load(selector, instruction, result_register);
            break;
        case IR_OPCODE_STORE:
            selected = machine_a64_select_store(selector, instruction);
            break;
        case IR_OPCODE_ATOMIC_LOAD:
            selected = machine_a64_select_atomic_load(selector, instruction, result_register);
            break;
        case IR_OPCODE_ATOMIC_STORE:
            selected = machine_a64_select_atomic_store(selector, instruction);
            break;
        case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE:
            selected = machine_a64_select_atomic_read_modify_write(selector, instruction, result_register);
            break;
        case IR_OPCODE_ATOMIC_COMPARE_EXCHANGE:
            selected = machine_a64_select_atomic_compare_exchange(selector, instruction, result_register);
            break;
        case IR_OPCODE_CLEAR_INSTRUCTION_CACHE:
            selected = machine_a64_select_clear_instruction_cache(selector, instruction);
            break;
        case IR_OPCODE_ATOMIC_FENCE:
            selected = machine_a64_select_atomic_fence(selector, instruction);
            break;
        case IR_OPCODE_INLINE_ASSEMBLY:
            selected = machine_a64_select_compiler_barrier(selector, instruction);
            break;
        case IR_OPCODE_BRANCH:
            selected = machine_a64_select_branch(selector, instruction);
            break;
        case IR_OPCODE_LABEL_ADDRESS:
            selected = machine_a64_select_label_address(selector, instruction, result_register);
            break;
        case IR_OPCODE_INDIRECT_BRANCH:
            selected = machine_a64_select_indirect_branch(selector, instruction);
            break;
        case IR_OPCODE_BRANCH_IF:
            selected = machine_a64_select_branch_if(selector, instruction);
            break;
        case IR_OPCODE_SWITCH:
            selected = machine_a64_select_switch(selector, instruction);
            break;
        case IR_OPCODE_UNREACHABLE:
            selected = machine_a64_select_unreachable(selector);
            break;
        case IR_OPCODE_RETURN:
            selected = machine_a64_select_return(selector, instruction);
            break;
        default:
            selected = false;
            break;
        }
    }
    return selected;
}

// The alias sweeps and branch-fusion pass only mutate state for these rows;
// the authoritative target-order walk below records their compact offsets.
#define MACHINE_A64_CANDIDATE_OPCODES                                                                                                  \
    (IR_OPCODE_BIT(IR_OPCODE_LOAD) | IR_OPCODE_BIT(IR_OPCODE_STORE) | IR_OPCODE_BIT(IR_OPCODE_DEREFERENCE) |                           \
     IR_OPCODE_BIT(IR_OPCODE_BRANCH_IF))

MachineSelectResult machine_select_canonical_function_aarch64(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                               bool assume_validated)
{
    MachineSelectResult result = {
        .failed_opcode = IR_OPCODE_COUNT,
    };
    if (arena && program && function && target.cpu_arch == CPU_ARCH_AARCH64 && function->state == IR_FUNCTION_LOWERED && function->block_count &&
        function->entry.value == 0)
    {
        IrType* function_type = ir_type_from_id(&program->types, function->canonical_type);
        // Darwin and Windows variadic definition ABIs need distinct list
        // storage and stay canonical even when the body ignores its tail;
        // ELF AAPCS64 bodies continue through ordinary
        // selection, so the first va_* the subset cannot shape (or any
        // earlier unsupported operation) reports in true IR order.
        bool separate_variadic_abi = target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS || target_uses_pe_unwind(target);
        result.signature_rejected = function_type && function_type->kind == IR_TYPE_FUNCTION;
        if (!function_type || function_type->kind != IR_TYPE_FUNCTION || (function_type->is_variadic && separate_variadic_abi) ||
            function_type->parameter_count > MACHINE_A64_MAX_ARGUMENTS)
        {
            return result;
        }
        // The AAPCS64 shape gate: every parameter and the return value must
        // classify to register parts — integer scalars, float scalars,
        // one-or-two-part register aggregates, HFAs, sixteen-byte short
        // vectors — an indirect result or argument, or a scalar stack
        // argument. Aggregate/HFA/vector stack arguments stay canonical.
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
        u32 signature_stack_part_count = 0;
        for (u32 parameter_index = 0; parameter_index < function_type->parameter_count; parameter_index += 1)
        {
            if (!machine_a64_value_shape(program, function_type->parameter_types[parameter_index], target, IR_ABI_USE_ARGUMENT,
                                         signature_parameter_shapes + parameter_index) ||
                !machine_a64_place_argument(signature_parameter_shapes + parameter_index, &signature_integer_count, &signature_float_count,
                                            &signature_stack_part_count, signature_parameter_placements + parameter_index))
            {
                return result;
            }
        }
        result.signature_rejected = false;
        MachineA64Selector selector = {
            .arena = arena,
            .program = program,
            .function = function,
            .builder = machine_function_builder_begin(arena),
            .value_virtual_registers = arena_allocate(arena, u32, function->value_count),
            .value_stack_slots = arena_allocate(arena, u32, function->value_count),
            .value_indirect_slots = arena_allocate(arena, u32, function->value_count),
            .outgoing_slot = UINT32_MAX,
            .supported = true,
            .failed_opcode = IR_OPCODE_COUNT,
        };
        if (!assume_validated && machine_selection_validate_function(arena, program, function) != MACHINE_SELECTION_VALIDATION_NONE)
        {
            return result;
        }
        MachineSelectionValueFacts value_facts = machine_selection_value_facts_allocate(arena, function->value_count);
        selector.target = target;
        selector.direct_call_uses = arena_allocate(arena, u8, function->value_count ? function->value_count : 1);
        memset(selector.direct_call_uses, 0, function->value_count);
        machine_stream_initialize(&selector.immediates, sizeof(u64));
        machine_stream_initialize(&selector.stack_slots, sizeof(u32));
        machine_stream_initialize(&selector.stack_slot_alignments, sizeof(u32));
        machine_stream_initialize(&selector.call_targets, sizeof(MachineA64CallTarget));
        machine_stream_initialize(&selector.va_args, sizeof(MachineVaArg));
        machine_stream_initialize(&selector.switch_cases, sizeof(MachineSwitchCase));
        MachineBuilderStream line_marks;
        machine_stream_initialize(&line_marks, sizeof(MachineLineMark));
        selector.return_shape = signature_return_shape;
        selector.hidden_return_slot = UINT32_MAX;
        selector.va_register_save_slot = UINT32_MAX;
        if (signature_return_shape.indirect)
        {
            selector.hidden_return_slot = machine_a64_append_slot(&selector, 8, 8);
        }
        if (function_type->is_variadic)
        {
            // The canonical non-Darwin model saves X0-X7 and Q0-Q7 in a
            // 192-byte area; a dedicated frame slot keeps its displacement
            // in ordinary placement data while VA_SAVE owns the prologue
            // snapshot itself.
            selector.va_register_save_slot = machine_a64_append_slot(&selector, MACHINE_A64_VA_SAVE_BYTES, 16);
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
            u32 parameter_count = 0;
            IrCfgBlock const* published_block = function->published_cfg->blocks + block_index;
            for (u32 parameter_index = 0; parameter_index < published_block->parameter_count; parameter_index += 1)
            {
                IrCfgParameter const* parameter = function->published_cfg->parameters + published_block->parameter_offset + parameter_index;
                IrType* parameter_type = ir_type_from_id(&program->types, parameter->canonical_type);
                bool wide = parameter_type && parameter_type->kind == IR_TYPE_INTEGER && parameter_type->bit_width == 128;
                if (parameter->value.value >= function->value_count ||
                    (!wide && !machine_a64_type_is_scalar_register(parameter_type) && !machine_a64_type_is_float_scalar(parameter_type)))
                {
                    return result;
                }
                parameter_count += wide ? 2u : 1u;
                if (parameter_count > UINT16_MAX)
                {
                    return result;
                }
                if (wide)
                {
                    if (!machine_builder_canonical_pair_parameter(&selector.builder, function, published_block, parameter_index, &selector.value_pairs))
                    {
                        return result;
                    }
                    selector.value_stack_slots[parameter->value.value] = machine_a64_append_slot(&selector, 16, 16);
                }
                else
                {
                    selector.value_virtual_registers[parameter->value.value] =
                        machine_builder_virtual_register(&selector.builder, (MachineVirtualRegister){
                                                                                 .definition_point = MACHINE_POINT_INVALID,
                                                                                 .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                                 .typed_origin = parameter->value.value,
                                                                             });
                }
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
        // and the volatile forms all keep the local in its slot. The byte size
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
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            promotable_locals[value_index] = 0;
            value_last_use_ordinals[value_index] = 0;
            local_store_counts[value_index] = 0;
            value_def_ordinals[value_index] = 0;
            IrInstructionId definition = function->values[value_index].definition;
            if (definition.value < function->instruction_count)
            {
                IrInstruction* instruction = function->instructions + definition.value;
                if (!program->disable_target_local_promotion && instruction->opcode == IR_OPCODE_LOCAL && instruction->result.value == value_index)
                {
                    IrType* local_type = ir_type_from_id(&program->types, function->values[value_index].canonical_type);
                    if (machine_a64_type_is_scalar_register(local_type) && (local_type->layout.size == 4 || local_type->layout.size == 8))
                    {
                        promotable_locals[value_index] = (u8)local_type->layout.size;
                    }
                }
            }
        }
        // The target-order walk is authoritative for value facts.  Project the
        // row population here as well: aliasing and fusion only inspect these
        // four opcodes, so their later passes can consume compact candidate
        // offsets instead of chasing every linked row again.
        u32* block_candidate_counts = arena_allocate(arena, u32, function->block_count ? function->block_count : 1);
        u32* candidate_rows = arena_allocate(arena, u32, function->instruction_count ? function->instruction_count : 1);
        u32 candidate_count = 0;
        bool nonvolatile_memory = true;
        u32 walk_ordinal = 0;
        u32 expanded_blocks = 0;
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            u32 block_row_count = 0;
            u32 block_candidate_count = 0;
            u32 entry_block = expanded_blocks;
            if (expanded_blocks < MACHINE_REF_PAYLOAD_LIMIT)
            {
                expanded_blocks += 1;
            }
            else
            {
                machine_a64_reject(&selector, IR_OPCODE_BINARY);
            }
            for (IrInstructionId id = block->first_instruction; id.value <= block->last_instruction.value; id.value += 1)
            {
                IrInstruction* instruction = function->instructions + id.value;
                nonvolatile_memory &= !instruction->volatile_access;
                if (machine_a64_instruction_is_i128_divide(program, instruction))
                {
                    if (!selector.block_entries)
                    {
                        selector.block_entries = arena_allocate(arena, u32, function->block_count);
                        selector.block_exits = arena_allocate(arena, u32, function->block_count);
                        for (u32 previous = 0; previous < block_index; previous += 1)
                        {
                            selector.block_entries[previous] = previous;
                            selector.block_exits[previous] = previous;
                        }
                    }
                    if (expanded_blocks > MACHINE_REF_PAYLOAD_LIMIT - 2)
                    {
                        machine_a64_reject(&selector, IR_OPCODE_BINARY);
                    }
                    else
                    {
                        expanded_blocks += 2;
                    }
                }
                if ((MACHINE_A64_CANDIDATE_OPCODES >> instruction->opcode) & 1)
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
                    // Fold the existing direct-call projection into this operand
                    // walk. Other uses are absorbing, so linked/storage order
                    // cannot change the result. Do not skip the remaining facts.
                    if (selector.direct_call_uses[used] != 2)
                    {
                        bool direct = instruction->opcode == IR_OPCODE_CALL && operand_index == 0;
                        if (direct)
                        {
                            IrInstructionId definition = function->values[used].definition;
                            IrInstruction* reference = definition.value < function->instruction_count ? function->instructions + definition.value : 0;
                            direct = reference && reference->opcode == IR_OPCODE_FUNCTION && reference->symbol.value == instruction->symbol.value;
                        }
                        selector.direct_call_uses[used] = direct ? 1 : 2;
                    }
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
            if (selector.block_entries)
            {
                selector.block_entries[block_index] = entry_block;
                selector.block_exits[block_index] = expanded_blocks - 1;
            }
            block_candidate_counts[block_index] = block_candidate_count;
        }
        // Incoming block-parameter values are edge uses, not instruction
        // operands. Count them before aliasing/fusion so a value carried by
        // canonical promotion cannot be absorbed as an otherwise-dead branch
        // condition.
        IrPublishedCfg const* cfg = function->published_cfg;
        for (u32 index = 0; index < cfg->argument_count; index += 1)
        {
            u32 value = cfg->arguments[index].value;
            value_use_counts[value] += 1;
            value_use_blocks[value] = MACHINE_SELECTION_MULTIPLE_BLOCKS;
            value_last_use_ordinals[value] = walk_ordinal;
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
        // index base folds its offset into a real address add and stops the
        // chain.
        for (u32 alias_sweep = 0; alias_sweep < 2; alias_sweep += 1)
        {
            u32 walked_ordinals = 0;
            u32 candidate_base = 0;
            for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
            {
                IrBlock* block = function->blocks + block_index;
                u32 epoch = alias_sweep * function->block_count + block_index + 1;
                u32 block_candidate_count = block_candidate_counts[block_index];
                // Candidates are recorded in block order, so reversing this
                // compact subsequence is the same reverse walk as the linked
                // rows while skipping all unrelated opcodes.
                for (u32 remaining = block_candidate_count; remaining > 0; remaining -= 1)
                {
                    u32 row_offset = candidate_rows[candidate_base + remaining - 1];
                    IrInstruction* instruction = function->instructions +
                                                  (block->first_instruction.value + row_offset);
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
                        if (load_aliases[candidate] == UINT32_MAX)
                        {
                            alias_values[alias_count] = candidate;
                            alias_count += 1;
                        }
                        load_aliases[candidate] = root;
                    }
                }
                walked_ordinals += function->published_cfg->blocks[block_index].instruction_count;
                candidate_base += block_candidate_count;
            }
        }
        // Classification pass: direct locals become stack slots, every other
        // scalar result becomes a virtual register, in stable value-id order.
        for (u32 block_index = 0; block_index < function->block_count && selector.supported; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            u32 block_row_count = function->published_cfg->blocks[block_index].instruction_count;
            for (u32 row_offset = 0; row_offset < block_row_count; row_offset += 1)
            {
                IrInstruction* instruction = function->instructions + (block->first_instruction.value + row_offset);
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
                    if (!local_type || !local_type->layout.resolved || local_type->layout.size > UINT32_MAX - 7)
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
                                                                                    .flags = MACHINE_VIRTUAL_REGISTER_FLAG_MUTABLE,
                                                                                    .typed_origin = instruction->result.value,
                                                                                });
                        continue;
                    }
                    if (local_alignment > 16)
                    {
                        // Over-aligned local, the canonical frame layout's
                        // shape exactly: a padded raw slot and a pointer
                        // aligned into it at runtime by the LOCAL's own
                        // rows. The slot stays out of value_stack_slots so
                        // every consumer takes the pointer paths a GLOBAL
                        // takes.
                        if (local_type->layout.size > UINT32_MAX - 7 - local_alignment)
                        {
                            machine_a64_reject(&selector, instruction->opcode);
                            break;
                        }
                        selector.value_indirect_slots[instruction->result.value] =
                            machine_a64_append_slot(&selector, (u32)((local_type->layout.size + local_alignment - 1 + 7) & ~(u64)7), 8u);
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
                if ((instruction->opcode == IR_OPCODE_VA_START || instruction->opcode == IR_OPCODE_VA_COPY) && value_type &&
                    value_type->kind == IR_TYPE_VA_LIST && value_type->layout.resolved && value_type->layout.size <= UINT32_MAX - 7)
                {
                    // The public va_list occupies thirty-two bytes in the canonical
                    // model. Keep the temporary in a regular frame slot so
                    // STORE/LOAD and VA_COPY reuse the aggregate copy rows.
                    selector.value_stack_slots[instruction->result.value] =
                        machine_a64_append_slot(&selector, (u32)((value_type->layout.size + 7) & ~(u64)7), 8);
                    continue;
                }
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
                else if ((instruction->opcode == IR_OPCODE_ARGUMENT || instruction->opcode == IR_OPCODE_LOAD ||
                          instruction->opcode == IR_OPCODE_ATOMIC_LOAD || instruction->opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE ||
                          instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE || instruction->opcode == IR_OPCODE_CALL ||
                          instruction->opcode == IR_OPCODE_AGGREGATE || instruction->opcode == IR_OPCODE_ARRAY ||
                          instruction->opcode == IR_OPCODE_VA_ARG || instruction->opcode == IR_OPCODE_CAST ||
                          instruction->opcode == IR_OPCODE_CONSTANT_INTEGER ||
                          ((instruction->opcode == IR_OPCODE_BINARY || instruction->opcode == IR_OPCODE_UNARY) && value_type &&
                           (value_type->kind == IR_TYPE_VECTOR || (value_type->kind == IR_TYPE_INTEGER && value_type->bit_width == 128)))) &&
                         value_type && value_type->layout.resolved && value_type->layout.size <= UINT32_MAX - 7 &&
                         (value_type->kind == IR_TYPE_STRUCT || value_type->kind == IR_TYPE_UNION || value_type->kind == IR_TYPE_SLICE ||
                          value_type->kind == IR_TYPE_VECTOR || value_type->kind == IR_TYPE_VA_LIST ||
                          (value_type->kind == IR_TYPE_INTEGER && value_type->bit_width == 128) ||
                          ((instruction->opcode == IR_OPCODE_ARRAY || instruction->opcode == IR_OPCODE_LOAD) && value_type->kind == IR_TYPE_ARRAY)))
                {
                    // Aggregate and vector values own a frame slot like the
                    // canonical path's per-value storage; copies and ABI part
                    // transfers address it directly. A vector slot is
                    // sixteen-aligned so the V-register edge rows keep their
                    // scaled addressing form.
                    selector.value_stack_slots[instruction->result.value] = machine_a64_append_slot(
                        &selector, (u32)((value_type->layout.size + 7) & ~(u64)7), value_type->kind == IR_TYPE_VECTOR ? 16u : 8u);
                }
            }
        }
        // Aliased load results share their local's virtual register: every use
        // site then names the local directly and the load emits nothing. The
        // result's own classification vreg goes unused, which costs an id and
        // nothing else.
        for (u32 alias_index = 0; alias_index < alias_count; alias_index += 1)
        {
            u32 value_index = alias_values[alias_index];
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
        u32* local_store_ordinals = local_store_counts;
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            selector.branch_fusions[value_index] = (MachineA64BranchFusion){.condition = 0xff};
            selector.fused_dead[value_index] = 0;
            local_store_ordinals[value_index] = 0;
        }
        u32 fused_rows = 0;
        u32 fusion_candidate_base = 0;
        for (u32 block_index = 0; block_index < function->block_count && selector.supported; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            u32 block_candidate_count = block_candidate_counts[block_index];
            // Stores and BRANCH_IFs are the only rows that mutate fusion
            // state.  Iterate their stable candidate offsets and recover the
            // published row ID through its canonical block span.
            for (u32 candidate_index = 0; candidate_index < block_candidate_count; candidate_index += 1)
            {
                u32 row_offset = candidate_rows[fusion_candidate_base + candidate_index];
                IrInstruction* instruction = function->instructions + (block->first_instruction.value + row_offset);
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
                u32 condition = 0xff;
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
            fused_rows += function->published_cfg->blocks[block_index].instruction_count;
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
        for (u32 block_index = 0; block_index < function->block_count && selector.supported; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            machine_builder_block_begin(&selector.builder);
            selector.open_block = (MachineBlock){.parameter_offset = selector.builder.block_parameters.total_count};
            IrCfgBlock const* published_block = function->published_cfg->blocks + block_index;
            for (u32 parameter_index = 0; parameter_index < published_block->parameter_count; parameter_index += 1)
            {
                IrCfgParameter const* parameter = function->published_cfg->parameters + published_block->parameter_offset + parameter_index;
                u32 value = parameter->value.value;
                bool wide = selector.value_pairs && selector.value_pairs[value].registers[0] != UINT32_MAX;
                u32 count = wide ? 2u : 1u;
                u32 const* registers = wide ? selector.value_pairs[value].registers : selector.value_virtual_registers + value;
                for (u32 part = 0; part < count; part += 1)
                {
                    machine_builder_block_parameter(&selector.builder, (MachineBlockParameter){.virtual_register = registers[part]});
                    if (wide)
                    {
                        machine_a64_select_frame_store64(&selector, selector.value_stack_slots[value], part * 8u, registers[part]);
                    }
                }
            }
            selector.open_block.parameter_count = (u16)(selector.builder.block_parameters.total_count - selector.open_block.parameter_offset);
            if (block_index == 0)
            {
                // The variadic save snapshot reads the still-live incoming
                // X0-X7, so it precedes every capture row — the captures
                // also only read the argument registers, but a float
                // capture's bounce through a general scratch must never
                // land between the registers and their save.
                if (selector.va_register_save_slot != UINT32_MAX)
                {
                    machine_a64_select_row(&selector, (MachineInstruction){
                                                          .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, selector.va_register_save_slot)},
                                                          .opcode = MACHINE_A64_VA_SAVE,
                                                      });
                }
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
                u32 normalize_registers[MACHINE_A64_MAX_ARGUMENTS] = {0};
                u32 normalize_values[MACHINE_A64_MAX_ARGUMENTS] = {0};
                u16 normalize_opcodes[MACHINE_A64_MAX_ARGUMENTS] = {0};
                u32 normalize_count = 0;
                for (u32 capture_pass = 0; capture_pass < 3 && selector.supported; capture_pass += 1)
                {
                    bool float_pass = capture_pass == 1;
                    bool stack_pass = capture_pass == 2;
                    for (u32 argument_index = 0; argument_index < function_type->parameter_count; argument_index += 1)
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
                        // Stack read-backs run in a pass of their own, after
                        // every register capture. They read only the frame, so
                        // deferring them is always sound — and by then the
                        // incoming argument registers are dead, so the fresh
                        // vregs the read-backs define are free to land
                        // anywhere. Interleaved in argument order they were
                        // not: the integer file stays OPEN behind a
                        // stack-spilled vector or HFA, so a stack argument can
                        // precede a register one, and a bounce vreg placed on
                        // that register destroyed the argument before its own
                        // capture row ran (fast/quality wrong answer,
                        // mir-stack and canonical correct — the allocator-layer
                        // signature).
                        if (stack_pass != (parameter_placement->on_stack != 0))
                        {
                            continue;
                        }
                        if (parameter_placement->on_stack && shape->aggregate && !shape->indirect)
                        {
                            // A stack-passed aggregate, HFA, or vector: its
                            // eightbyte images read back from the caller's
                            // outgoing area into the value's slot.
                            u32 slot = selector.value_stack_slots[argument_value];
                            if (slot == UINT32_MAX)
                            {
                                machine_a64_reject(&selector, IR_OPCODE_ARGUMENT);
                                break;
                            }
                            for (u32 part_index = 0; part_index < shape->byte_size / 8; part_index += 1)
                            {
                                u32 incoming_register = machine_a64_synthesize_register(&selector);
                                u32 incoming_row = machine_a64_select_row(
                                    &selector, (MachineInstruction){
                                                   .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, incoming_register)},
                                                   .payload = 16u + ((u32)parameter_placement->first_stack_part + part_index) * 8u,
                                                   .opcode = MACHINE_A64_LOAD_INCOMING,
                                               });
                                machine_a64_define(&selector, incoming_register, incoming_row);
                                machine_a64_select_row(&selector, (MachineInstruction){
                                                                      .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                                                   machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, incoming_register)},
                                                                      .payload = part_index * 8u,
                                                                      .opcode = MACHINE_A64_STORE_FRAME64,
                                                                  });
                            }
                            continue;
                        }
                        if (shape->vector)
                        {
                            // The incoming V register image stores straight
                            // into the value's slot — whole or low bytes
                            // per the shape's size; no general scratch is
                            // touched, so the float pass carries it beside
                            // the FMOV captures.
                            if (float_pass)
                            {
                                u32 slot = selector.value_stack_slots[argument_value];
                                if (slot == UINT32_MAX)
                                {
                                    machine_a64_reject(&selector, IR_OPCODE_ARGUMENT);
                                    break;
                                }
                                machine_a64_select_row(&selector, machine_a64_vector_transfer_row(shape, slot, next_float, true));
                            }
                            continue;
                        }
                        if (shape->indirect)
                        {
                            // The pointer arrives in its integer register (or
                            // on the stack past the file); the callee owns a
                            // private copy, so the parameter behaves like any
                            // slot-backed aggregate from here on — the
                            // canonical callee reads through the pointer the
                            // same way, an eightbyte at a time.
                            if (!float_pass)
                            {
                                u32 slot = selector.value_stack_slots[argument_value];
                                if (slot == UINT32_MAX)
                                {
                                    machine_a64_reject(&selector, IR_OPCODE_ARGUMENT);
                                    break;
                                }
                                u32 pointer_register = machine_a64_synthesize_register(&selector);
                                u32 pointer_row;
                                if (parameter_placement->on_stack)
                                {
                                    pointer_row = machine_a64_select_row(
                                        &selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register)},
                                                       .payload = 16u + (u32)parameter_placement->first_stack_part * 8u,
                                                       .opcode = MACHINE_A64_LOAD_INCOMING,
                                                   });
                                }
                                else
                                {
                                    pointer_row = machine_a64_select_row(
                                        &selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register),
                                                                    machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, next_integer)},
                                                       .opcode = MACHINE_A64_MOV_RR,
                                                   });
                                }
                                machine_a64_define(&selector, pointer_register, pointer_row);
                                machine_a64_select_row(&selector, (MachineInstruction){
                                                                      .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                                                   machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, pointer_register)},
                                                                      .payload = shape->byte_size,
                                                                      .opcode = MACHINE_A64_COPY_FRAME_FROM_PTR,
                                                                  });
                            }
                            continue;
                        }
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
                        if (parameter_placement->on_stack)
                        {
                            // A scalar stack parameter reads its eight-byte
                            // image from the caller's outgoing area during
                            // the integer pass — the load touches no
                            // argument register of either class.
                            if (!float_pass)
                            {
                                u32 incoming_register = selector.value_virtual_registers[argument_value];
                                if (incoming_register == UINT32_MAX)
                                {
                                    machine_a64_reject(&selector, IR_OPCODE_ARGUMENT);
                                    break;
                                }
                                u32 incoming_row =
                                    machine_a64_select_row(&selector, (MachineInstruction){
                                                                          .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, incoming_register)},
                                                                          .payload = 16u + (u32)parameter_placement->first_stack_part * 8u,
                                                                          .opcode = MACHINE_A64_LOAD_INCOMING,
                                                                      });
                                machine_a64_define(&selector, incoming_register, incoming_row);
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
                        // AAPCS64 leaves the bits above a narrow integer
                        // argument's declared width unspecified in the
                        // register it arrives in, exactly as System V does on
                        // x86-64, so the captured value is normalized to that
                        // width.  The rows are recorded here and emitted
                        // after every capture pass: a row emitted between
                        // captures may take a scratch register the incoming
                        // arguments still occupy.
                        if (!scalar_float && normalize_count < BUSTER_ARRAY_LENGTH(normalize_registers))
                        {
                            IrType* parameter_type = ir_type_from_id(&program->types, function_type->parameter_types[argument_index]);
                            if (parameter_type && (parameter_type->kind == IR_TYPE_INTEGER || parameter_type->kind == IR_TYPE_BOOLEAN) &&
                                parameter_type->layout.size < 8)
                            {
                                normalize_registers[normalize_count] = argument_register;
                                normalize_values[normalize_count] = argument_value;
                                normalize_opcodes[normalize_count] = parameter_type->layout.size == 1   ? MACHINE_A64_UXTB
                                                                     : parameter_type->layout.size == 2 ? MACHINE_A64_UXTH
                                                                                                        : MACHINE_A64_MOV32_RR;
                                normalize_count += 1;
                            }
                        }
                    }
                }
                for (u32 normalize_index = 0; normalize_index < normalize_count && selector.supported; normalize_index += 1)
                {
                    // The normalized value takes a register of its own: a
                    // virtual register is defined exactly once, and the
                    // argument's is already defined by its capture row.
                    u32 source_register = normalize_registers[normalize_index];
                    u32 normalize_register = machine_a64_synthesize_register(&selector);
                    u32 normalize_row = machine_a64_select_row(&selector, (MachineInstruction){
                                                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, normalize_register),
                                                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                                             .opcode = normalize_opcodes[normalize_index],
                                                                         });
                    machine_a64_define(&selector, normalize_register, normalize_row);
                    selector.value_virtual_registers[normalize_values[normalize_index]] = normalize_register;
                }
            }
            u32 block_row_count = function->published_cfg->blocks[block_index].instruction_count;
            for (u32 row_offset = 0; row_offset < block_row_count && selector.supported; row_offset += 1)
            {
                IrInstructionId id = {.value = (block->first_instruction.value + row_offset)};
                IrInstruction* instruction = function->instructions + id.value;
                typed_instruction_count += 1;
                simd_operation_count += instruction->opcode == IR_OPCODE_SIMD;
                MachineLineMark* mark = (MachineLineMark*)machine_stream_append(arena, &line_marks);
                *mark = (MachineLineMark){.row = selector.builder.instructions.total_count, .instruction = id.value};
                if (!machine_a64_select_instruction(&selector, instruction))
                {
                    machine_a64_reject(&selector, instruction->opcode);
                    break;
                }
                u32 value = instruction->result.value;
                if (selector.value_pairs && value < function->value_count && selector.value_pairs[value].registers[0] != UINT32_MAX)
                {
                    u32 slot = selector.value_stack_slots[value];
                    if (slot == UINT32_MAX)
                    {
                        machine_a64_reject(&selector, instruction->opcode);
                        break;
                    }
                    for (u32 part = 0; part < 2; part += 1)
                    {
                        u32 reg = selector.value_pairs[value].registers[part];
                        u32 row = machine_a64_select_row(&selector, (MachineInstruction){
                            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, reg), machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                            .payload = part * 8u, .opcode = MACHINE_A64_LOAD_FRAME});
                        machine_a64_define(&selector, reg, row);
                    }
                }
            }
            machine_builder_block_end(&selector.builder, selector.open_block);
        }
        // A dynamic allocation moves the stack pointer below the fixed
        // outgoing argument area, whose base every call with stack parts
        // reads as its own stack pointer — the pair cannot coexist, and
        // which calls need the area is only known now.
        if (selector.supported && selector.stack_allocate_selected && selector.outgoing_slot != UINT32_MAX)
        {
            machine_a64_reject(&selector, IR_OPCODE_STACK_ALLOCATE);
        }
        if (!selector.supported)
        {
            result.failed_opcode = selector.failed_opcode;
            return result;
        }
        u32 canonical_edge_offset = selector.builder.edges.total_count;
        if (!machine_builder_canonical_edges(&selector.builder, function, selector.value_virtual_registers, selector.value_pairs))
        {
            return (MachineSelectResult){.failed_opcode = IR_OPCODE_COUNT};
        }
        result.function = machine_function_builder_finish(arena, &selector.builder);
        if (selector.block_entries)
        {
            for (u32 edge_index = canonical_edge_offset; edge_index < result.function.edge_count; edge_index += 1)
            {
                MachineEdge* edge = result.function.edges + edge_index;
                edge->source_block = selector.block_exits[edge->source_block];
                edge->destination_block = selector.block_entries[edge->destination_block];
            }
        }
        result.function.target = &machine_aarch64_description;
        result.function.windows_aarch64_frame = target_uses_pe_unwind(target);
        result.function.immediates = arena_allocate(arena, u64, selector.immediates.total_count);
        result.function.immediate_count = selector.immediates.total_count;
        machine_stream_flatten(&selector.immediates, result.function.immediates);
        result.function.stack_slot_sizes = arena_allocate(arena, u32, selector.stack_slots.total_count);
        result.function.stack_slot_count = selector.stack_slots.total_count;
        result.function.nonvolatile_memory_certified = nonvolatile_memory;
        machine_stream_flatten(&selector.stack_slots, result.function.stack_slot_sizes);
        result.function.stack_slot_alignments = arena_allocate(arena, u32, selector.stack_slot_alignments.total_count);
        machine_stream_flatten(&selector.stack_slot_alignments, result.function.stack_slot_alignments);
        result.function.switch_cases = arena_allocate(arena, MachineSwitchCase, selector.switch_cases.total_count ? selector.switch_cases.total_count : 1);
        result.function.switch_case_count = selector.switch_cases.total_count;
        machine_stream_flatten(&selector.switch_cases, result.function.switch_cases);
        if (selector.outgoing_slot != UINT32_MAX)
        {
            // The area was appended at the first call that needed one and
            // may have grown at a later call; publish the final size.
            result.function.outgoing_slot = selector.outgoing_slot;
            result.function.outgoing_bytes = selector.outgoing_bytes;
            result.function.stack_slot_sizes[selector.outgoing_slot] = selector.outgoing_bytes;
        }
        result.function.call_targets = arena_allocate(arena, IrSymbolId, selector.call_targets.total_count);
        result.function.call_target_references = arena_allocate(arena, u8, selector.call_targets.total_count);
        result.function.call_target_count = selector.call_targets.total_count;
        u32 split_target = 0;
        for (MachineBuilderChunk* chunk = selector.call_targets.first; chunk; chunk = chunk->next)
        {
            MachineA64CallTarget const* rows = (MachineA64CallTarget const*)(chunk + 1);
            for (u32 row = 0; row < chunk->count; row += 1)
            {
                result.function.call_targets[split_target] = rows[row].symbol;
                result.function.call_target_references[split_target] = rows[row].reference;
                split_target += 1;
            }
        }
        result.function.va_args = arena_allocate(arena, MachineVaArg, selector.va_args.total_count);
        result.function.va_arg_count = selector.va_args.total_count;
        machine_stream_flatten(&selector.va_args, result.function.va_args);
        result.function.line_marks = arena_allocate(arena, MachineLineMark, line_marks.total_count);
        result.function.line_mark_count = line_marks.total_count;
        machine_stream_flatten(&line_marks, result.function.line_marks);
        // Only classification vregs need their definition patched; synthesized
        // temporaries carried their points from creation.
        for (u32 register_index = 0; register_index < selector.virtual_register_count; register_index += 1)
        {
            result.function.virtual_registers[register_index].definition_point = selector.virtual_register_definitions[register_index];
        }
        if (!nonvolatile_memory)
        {
            machine_selection_certify_stack_memory(arena, &result.function, function);
        }
        if (!machine_function_split_parameter_edges(arena, &result.function))
        {
            return (MachineSelectResult){.failed_opcode = IR_OPCODE_COUNT};
        }
        result.mutable_virtual_register_count = machine_function_compact_virtual_registers(arena, &result.function);
        if (result.mutable_virtual_register_count == UINT32_MAX)
        {
            return (MachineSelectResult){.failed_opcode = IR_OPCODE_COUNT};
        }
        result.supported = true;
        result.selector_certified = true;
        result.returns_value = returns_value;
        result.selected_typed_instructions = typed_instruction_count;
        result.machine_instructions = result.function.instruction_count;
        result.simd_operation_count = simd_operation_count;
    }

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
    // Test-only sparse-layout mode: the planner mutates virtual offsets and
    // metadata without touching a giant byte buffer. Production encoders
    // leave this false and retain the ordinary memmove/emit behavior.
    bool sparse;
    u8 reserved[1];
};

typedef struct MachineA64BranchFixup MachineA64BranchFixup;
struct MachineA64BranchFixup
{
    u32 patch_offset;
    u32 block;
    A64Opcode opcode;
    // A conditional fixup keeps its original condition here because a long
    // transfer inverts the condition and skips the scratch transfer.  The
    // direct path still patches the exact word emitted by the MC encoder.
    u8 condition;
    u8 expanded;
    bool label_address;
    u8 reserved;
};

BUSTER_GLOBAL_LOCAL void machine_a64_emit(MachineA64Encoder* encoder, u32 word)
{
    if (!encoder || encoder->count > encoder->capacity || encoder->capacity - encoder->count < 4)
    {
        if (encoder)
        {
            encoder->overflow = true;
        }
        return;
    }
    if (encoder->sparse)
    {
        encoder->count += 4;
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

// A long branch is deliberately a fixed-size, base-independent transfer:
// ADR x16,#0 obtains the address of the transfer itself, x17 receives the
// signed function-relative delta in four fixed MOVZ/MOVK words, and ADD/BR
// reach the final block.  X16/X17 are reserved from the allocator (the
// encoder's address scratches), so this sequence cannot clobber a live value;
// none of the instructions writes NZCV.  Keeping the materialization fixed
// also makes layout relaxation monotonic when a later insertion shifts a
// target across a halfword pattern boundary.
#define MACHINE_A64_LONG_BRANCH_WORDS 7u
#define MACHINE_A64_LONG_BRANCH_BYTES (MACHINE_A64_LONG_BRANCH_WORDS * 4u)
#define MACHINE_A64_LONG_CONDITIONAL_BYTES (4u + MACHINE_A64_LONG_BRANCH_BYTES)
// A block pointer uses the same PC-relative anchor as a long branch, but
// stops after the ADD (there is no BR).  Its six fixed words are patched after
// branch relaxation so insertions elsewhere cannot invalidate the address.
#define MACHINE_A64_BLOCK_ADDRESS_WORDS 6u
#define MACHINE_A64_BLOCK_ADDRESS_BYTES (MACHINE_A64_BLOCK_ADDRESS_WORDS * 4u)

BUSTER_GLOBAL_LOCAL bool machine_a64_emit_generated_form(MachineA64Encoder* encoder, u32 form_id, u32 const* field_values, u32 field_count);

BUSTER_GLOBAL_LOCAL bool machine_a64_emit_long_branch_bytes(u8* bytes, u32 capacity, s64 displacement, u32* byte_count)
{
    if (!bytes || capacity < MACHINE_A64_LONG_BRANCH_BYTES || !byte_count || (((u64)displacement) & 3u) != 0)
    {
        return false;
    }
    MachineA64Encoder encoder = {
        .bytes = bytes,
        .capacity = capacity,
    };
    // ADR x16,#0: the transfer's own PC is the only anchor required, so the
    // eventual absolute code address and page alignment never enter the
    // calculation.
    machine_a64_emit_mc(&encoder, (A64MCInst){
                                           .operands = {
                                               {.value = MACHINE_A64_X16, .kind = A64_MC_OPERAND_REGISTER},
                                               {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
                                           },
                                           .opcode = A64_OPCODE_ADR,
                                           .operand_count = 2,
                                       });
    // Always emit all four halfwords.  The fixed shape means expansion never
    // needs to grow or shrink after another branch has been relaxed.
    u64 encoded_displacement = (u64)displacement;
    machine_a64_emit(&encoder, UINT32_C(0xd2800000) | ((u32)(encoded_displacement & 0xffffu) << 5) | MACHINE_A64_X17);
    machine_a64_emit(&encoder, UINT32_C(0xf2800000) | (UINT32_C(1) << 21) | ((u32)((encoded_displacement >> 16) & 0xffffu) << 5) | MACHINE_A64_X17);
    machine_a64_emit(&encoder, UINT32_C(0xf2800000) | (UINT32_C(2) << 21) | ((u32)((encoded_displacement >> 32) & 0xffffu) << 5) | MACHINE_A64_X17);
    machine_a64_emit(&encoder, UINT32_C(0xf2800000) | (UINT32_C(3) << 21) | ((u32)((encoded_displacement >> 48) & 0xffffu) << 5) | MACHINE_A64_X17);
    {
        u32 fields[] = {MACHINE_A64_X16, MACHINE_A64_X16, 0, MACHINE_A64_X17};
        if (!machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRS, fields, BUSTER_ARRAY_LENGTH(fields)))
        {
            return false;
        }
        // Keep the scratch transfer's exact non-S ADD spelling pinned.  It
        // preserves NZCV just like ADR/MOVZ/MOVK/BR, and a metadata drift
        // must never silently turn this relaxation into an ADDS clobber.
        u32 add_word = 0;
        memcpy(&add_word, encoder.bytes + 20, sizeof(add_word));
        if (add_word != UINT32_C(0x8b110210))
        {
            return false;
        }
    }
    machine_a64_emit_mc(&encoder, (A64MCInst){
                                           .operands = {{.value = MACHINE_A64_X16, .kind = A64_MC_OPERAND_REGISTER}},
                                           .opcode = A64_OPCODE_BR,
                                           .operand_count = 1,
                                       });
    if (encoder.error || encoder.overflow || encoder.count != MACHINE_A64_LONG_BRANCH_BYTES)
    {
        return false;
    }
    *byte_count = encoder.count;
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_emit_generated_form(MachineA64Encoder* encoder, u32 form_id, u32 const* field_values,
                                                          u32 field_count)
{
    u32 word = 0;
    if (!encoder || !a64_generated_production_raw_encode(form_id, field_values, field_count, &word))
    {
        if (encoder)
        {
            encoder->error = true;
        }
        return false;
    }
    machine_a64_emit(encoder, word);
    return true;
}

// Keep the semantic field order for every generated scalar row in one place.
// The imported plans intentionally follow LLVM's source-variable order, not
// necessarily the spelling order of an assembly alias (for example SBFM's
// imms/immr fields).  Both the production switch and the unconditional byte
// oracles below call this helper so a swapped field cannot hide behind a
// generic raw-layout comparison.
BUSTER_GLOBAL_LOCAL bool machine_a64_emit_generated_opcode(MachineA64Encoder* encoder, u16 opcode, u32 operand0, u32 operand1, u32 operand2, u32 payload)
{
    if (!encoder)
    {
        return false;
    }
    u32 form_id = UINT32_MAX;
    u32 field_count = 0;
    u32 fields[4] = {0};
    switch (opcode)
    {
    case MACHINE_A64_MOV_RR:
        form_id = BUSTER_AARCH64_GENERATED_FORM_ORRXRS;
        fields[0] = operand0;
        fields[1] = MACHINE_A64_SP;
        fields[2] = 0;
        fields[3] = operand1;
        field_count = 4;
        break;
    case MACHINE_A64_MOV32_RR:
        form_id = BUSTER_AARCH64_GENERATED_FORM_ORRWRS;
        fields[0] = operand0;
        fields[1] = MACHINE_A64_SP;
        fields[2] = 0;
        fields[3] = operand1;
        field_count = 4;
        break;
    case MACHINE_A64_SXTB:
    case MACHINE_A64_SXTH:
    case MACHINE_A64_SXTW:
        form_id = BUSTER_AARCH64_GENERATED_FORM_SBFMXRI;
        fields[0] = operand0;
        fields[1] = operand1;
        fields[2] = opcode == MACHINE_A64_SXTB ? 7u : opcode == MACHINE_A64_SXTH ? 15u : 31u;
        fields[3] = 0;
        field_count = 4;
        break;
    case MACHINE_A64_UXTB:
    case MACHINE_A64_UXTH:
        form_id = BUSTER_AARCH64_GENERATED_FORM_UBFMWRI;
        fields[0] = operand0;
        fields[1] = operand1;
        fields[2] = opcode == MACHINE_A64_UXTB ? 7u : 15u;
        fields[3] = 0;
        field_count = 4;
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
        form_id = opcode == MACHINE_A64_ADD32 ? BUSTER_AARCH64_GENERATED_FORM_ADDWRS
                  : opcode == MACHINE_A64_ADD64 ? BUSTER_AARCH64_GENERATED_FORM_ADDXRS
                  : opcode == MACHINE_A64_SUB32 ? BUSTER_AARCH64_GENERATED_FORM_SUBWRS
                  : opcode == MACHINE_A64_SUB64 ? BUSTER_AARCH64_GENERATED_FORM_SUBXRS
                  : opcode == MACHINE_A64_AND32 ? BUSTER_AARCH64_GENERATED_FORM_ANDWRS
                  : opcode == MACHINE_A64_AND64 ? BUSTER_AARCH64_GENERATED_FORM_ANDXRS
                  : opcode == MACHINE_A64_ORR32 ? BUSTER_AARCH64_GENERATED_FORM_ORRWRS
                  : opcode == MACHINE_A64_ORR64 ? BUSTER_AARCH64_GENERATED_FORM_ORRXRS
                  : opcode == MACHINE_A64_EOR32 ? BUSTER_AARCH64_GENERATED_FORM_EORWRS
                                                : BUSTER_AARCH64_GENERATED_FORM_EORXRS;
        fields[0] = operand0;
        fields[1] = operand1;
        fields[2] = 0;
        fields[3] = operand2;
        field_count = 4;
        break;
    case MACHINE_A64_MUL32:
    case MACHINE_A64_MUL64:
        form_id = opcode == MACHINE_A64_MUL64 ? BUSTER_AARCH64_GENERATED_FORM_MADDXRRR : BUSTER_AARCH64_GENERATED_FORM_MADDWRRR;
        fields[0] = operand0;
        fields[1] = operand1;
        fields[2] = MACHINE_A64_SP;
        fields[3] = operand2;
        field_count = 4;
        break;
    case MACHINE_A64_UMULH64:
        form_id = BUSTER_AARCH64_GENERATED_FORM_UMULHRR;
        fields[0] = operand0;
        fields[1] = operand1;
        fields[2] = operand2;
        field_count = 3;
        break;
    case MACHINE_A64_SDIV32:
    case MACHINE_A64_SDIV64:
    case MACHINE_A64_UDIV32:
    case MACHINE_A64_UDIV64:
        form_id = opcode == MACHINE_A64_SDIV32 ? BUSTER_AARCH64_GENERATED_FORM_SDIVWR
                  : opcode == MACHINE_A64_SDIV64 ? BUSTER_AARCH64_GENERATED_FORM_SDIVXR
                  : opcode == MACHINE_A64_UDIV32 ? BUSTER_AARCH64_GENERATED_FORM_UDIVWR
                                                  : BUSTER_AARCH64_GENERATED_FORM_UDIVXR;
        fields[0] = operand0;
        fields[1] = operand1;
        fields[2] = operand2;
        field_count = 3;
        break;
    case MACHINE_A64_SREM32:
    case MACHINE_A64_SREM64:
    case MACHINE_A64_UREM32:
    case MACHINE_A64_UREM64:
    {
        u32 divide_form_id = opcode == MACHINE_A64_SREM32 ? BUSTER_AARCH64_GENERATED_FORM_SDIVWR
                              : opcode == MACHINE_A64_SREM64 ? BUSTER_AARCH64_GENERATED_FORM_SDIVXR
                              : opcode == MACHINE_A64_UREM32 ? BUSTER_AARCH64_GENERATED_FORM_UDIVWR
                                                              : BUSTER_AARCH64_GENERATED_FORM_UDIVXR;
        u32 subtract_form_id = opcode == MACHINE_A64_SREM32 ? BUSTER_AARCH64_GENERATED_FORM_MSUBWRRR
                               : opcode == MACHINE_A64_SREM64 ? BUSTER_AARCH64_GENERATED_FORM_MSUBXRRR
                               : opcode == MACHINE_A64_UREM32 ? BUSTER_AARCH64_GENERATED_FORM_MSUBWRRR
                                                              : BUSTER_AARCH64_GENERATED_FORM_MSUBXRRR;
        u32 divide_fields[] = {operand0, operand1, operand2};
        // Production fields are ordered Rd, Rn, Ra, Rm while the assembly
        // spelling is Rd, Rn, Rm, Ra.  DIV has already placed n / m in d,
        // so MSUB must spell `d, d, m, n`: n - (n / m) * m.
        u32 subtract_fields[] = {operand0, operand0, operand1, operand2};
        return machine_a64_emit_generated_form(encoder, divide_form_id, divide_fields, BUSTER_ARRAY_LENGTH(divide_fields)) &&
               machine_a64_emit_generated_form(encoder, subtract_form_id, subtract_fields, BUSTER_ARRAY_LENGTH(subtract_fields));
    }
    case MACHINE_A64_LSL32:
    case MACHINE_A64_LSL64:
    case MACHINE_A64_ASR32:
    case MACHINE_A64_ASR64:
    case MACHINE_A64_LSR32:
    case MACHINE_A64_LSR64:
        form_id = opcode == MACHINE_A64_LSL32 ? BUSTER_AARCH64_GENERATED_FORM_LSLVWR
                  : opcode == MACHINE_A64_LSL64 ? BUSTER_AARCH64_GENERATED_FORM_LSLVXR
                  : opcode == MACHINE_A64_ASR32 ? BUSTER_AARCH64_GENERATED_FORM_ASRVWR
                  : opcode == MACHINE_A64_ASR64 ? BUSTER_AARCH64_GENERATED_FORM_ASRVXR
                  : opcode == MACHINE_A64_LSR32 ? BUSTER_AARCH64_GENERATED_FORM_LSRVWR
                                                : BUSTER_AARCH64_GENERATED_FORM_LSRVXR;
        fields[0] = operand0;
        fields[1] = operand1;
        fields[2] = operand2;
        field_count = 3;
        break;
    case MACHINE_A64_NEG32:
    case MACHINE_A64_NEG64:
        form_id = opcode == MACHINE_A64_NEG32 ? BUSTER_AARCH64_GENERATED_FORM_SUBWRS : BUSTER_AARCH64_GENERATED_FORM_SUBXRS;
        fields[0] = operand0;
        fields[1] = MACHINE_A64_SP;
        fields[2] = 0;
        fields[3] = operand1;
        field_count = 4;
        break;
    case MACHINE_A64_NOT32:
    case MACHINE_A64_NOT64:
        form_id = opcode == MACHINE_A64_NOT32 ? BUSTER_AARCH64_GENERATED_FORM_ORNWRS : BUSTER_AARCH64_GENERATED_FORM_ORNXRS;
        fields[0] = operand0;
        fields[1] = MACHINE_A64_SP;
        fields[2] = 0;
        fields[3] = operand1;
        field_count = 4;
        break;
    case MACHINE_A64_CLZ32:
    case MACHINE_A64_CLZ64:
    case MACHINE_A64_RBIT32:
    case MACHINE_A64_RBIT64:
        form_id = opcode == MACHINE_A64_CLZ32 ? BUSTER_AARCH64_GENERATED_FORM_CLZWR
                  : opcode == MACHINE_A64_CLZ64 ? BUSTER_AARCH64_GENERATED_FORM_CLZXR
                  : opcode == MACHINE_A64_RBIT32 ? BUSTER_AARCH64_GENERATED_FORM_RBITWR
                                                  : BUSTER_AARCH64_GENERATED_FORM_RBITXR;
        fields[0] = operand0;
        fields[1] = operand1;
        field_count = 2;
        break;
    case MACHINE_A64_CMP32:
    case MACHINE_A64_CMP64:
        form_id = opcode == MACHINE_A64_CMP32 ? BUSTER_AARCH64_GENERATED_FORM_SUBSWRS : BUSTER_AARCH64_GENERATED_FORM_SUBSXRS;
        fields[0] = MACHINE_A64_SP;
        fields[1] = operand0;
        fields[2] = 0;
        fields[3] = operand1;
        field_count = 4;
        break;
    case MACHINE_A64_CMP_ZERO:
        form_id = BUSTER_AARCH64_GENERATED_FORM_SUBSXRI;
        fields[0] = MACHINE_A64_SP;
        fields[1] = operand0;
        fields[2] = 0;
        field_count = 3;
        break;
    case MACHINE_A64_CSET:
        form_id = BUSTER_AARCH64_GENERATED_FORM_CSINCWR;
        fields[0] = operand0;
        fields[1] = MACHINE_A64_SP;
        fields[2] = payload ^ 1u;
        fields[3] = MACHINE_A64_SP;
        field_count = 4;
        break;
    case MACHINE_A64_FMOV_TO_VEC:
        form_id = BUSTER_AARCH64_GENERATED_FORM_FMOVXDR;
        fields[0] = payload;
        fields[1] = operand0;
        field_count = 2;
        break;
    case MACHINE_A64_FMOV_FROM_VEC:
        form_id = BUSTER_AARCH64_GENERATED_FORM_FMOVDXR;
        fields[0] = operand0;
        fields[1] = payload;
        field_count = 2;
        break;
    case MACHINE_A64_READ_SP:
        form_id = BUSTER_AARCH64_GENERATED_FORM_ADDXRI;
        fields[0] = operand0;
        fields[1] = MACHINE_A64_SP;
        fields[2] = 0;
        field_count = 3;
        break;
    case MACHINE_A64_WRITE_SP:
        form_id = BUSTER_AARCH64_GENERATED_FORM_ADDXRI;
        fields[0] = MACHINE_A64_SP;
        fields[1] = operand0;
        fields[2] = 0;
        field_count = 3;
        break;
    case MACHINE_A64_RET:
        form_id = BUSTER_AARCH64_GENERATED_FORM_RET;
        fields[0] = MACHINE_A64_X30;
        field_count = 1;
        break;
    default:
        encoder->error = true;
        return false;
    }
    if (opcode == MACHINE_A64_READ_SP || opcode == MACHINE_A64_WRITE_SP)
    {
        fields[2] = payload;
    }
    return machine_a64_emit_generated_form(encoder, form_id, fields, field_count);
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

// The scalar unsigned load/store rows all share the same three source fields:
// Rt, Rn, and the already-scaled imm12.  Keep the form selection named and
// checked here; the generated raw encoder owns field masks and fixed bits, so
// this backend cannot drift from the imported LLVM grammar while retaining
// the existing machine-level size/alignment checks.
BUSTER_GLOBAL_LOCAL bool machine_a64_emit_generated_unsigned_memory(MachineA64Encoder* encoder, u32 register_number, u32 base_register,
                                                                    u32 offset, u32 size, bool store)
{
    if (!encoder || (size != 1 && size != 2 && size != 4 && size != 8) || offset % size || offset / size > A64_IMM12_MAX)
    {
        if (encoder)
        {
            encoder->error = true;
        }
        return false;
    }
    u32 form_id = UINT32_MAX;
    if (store)
    {
        form_id = size == 1   ? BUSTER_AARCH64_GENERATED_FORM_STRBBUI
                  : size == 2 ? BUSTER_AARCH64_GENERATED_FORM_STRHHUI
                  : size == 4 ? BUSTER_AARCH64_GENERATED_FORM_STRWUI
                              : BUSTER_AARCH64_GENERATED_FORM_STRXUI;
    }
    else
    {
        form_id = size == 1   ? BUSTER_AARCH64_GENERATED_FORM_LDRBBUI
                  : size == 2 ? BUSTER_AARCH64_GENERATED_FORM_LDRHHUI
                  : size == 4 ? BUSTER_AARCH64_GENERATED_FORM_LDRWUI
                              : BUSTER_AARCH64_GENERATED_FORM_LDRXUI;
    }
    u32 field_values[3] = {register_number, base_register, offset / size};
    u32 word = 0;
    if (!a64_generated_production_raw_encode(form_id, field_values, BUSTER_ARRAY_LENGTH(field_values), &word))
    {
        encoder->error = true;
        return false;
    }
    machine_a64_emit(encoder, word);
    return true;
}

// Frame-relative sized memory operation off the X28 frame base, mirroring
// the canonical codegen_canonical_a64_memory_operation_base: scaled
// unsigned offsets directly, larger offsets through the X16 scratch.
BUSTER_GLOBAL_LOCAL void machine_a64_emit_frame_memory(MachineA64Encoder* encoder, u32 register_number, u32 offset, u32 size, bool store)
{
    u32 scale = size;
    if (size != 1 && size != 2 && size != 4 && size != 8)
    {
        encoder->error = true;
    }
    else
    {
        u32 base_register = MACHINE_A64_X28;
        // The scaled unsigned form addresses a multiple of the access size, and
        // a member of an aggregate whose alignment was lowered has no reason to
        // sit at one: `typedef int lowered __attribute__((aligned(2)));
        // struct { char tag; lowered value; char trailer; }` puts a four-byte
        // member at offset two. Misalignment therefore takes the same X16
        // materialize-and-add path an out-of-range offset does -- which is
        // exactly what the vector sibling below already does, for the same
        // reason. Failing closed here put the whole enclosing function back on
        // the canonical emitter through an encode-stage fallback (#813); the
        // access itself is legal, since AArch64 permits an unaligned normal-
        // memory load or store and only the immediate form is scaled. X16 is
        // reserved from the allocator, so it can never be the register the
        // access itself names.
        if (offset % scale || offset / scale > A64_IMM12_MAX)
        {
            machine_a64_emit_immediate(encoder, MACHINE_A64_X16, offset);
            machine_a64_emit(encoder, 0x8b000000 | (MACHINE_A64_X16 << 16) | (base_register << 5) | MACHINE_A64_X16);
            base_register = MACHINE_A64_X16;
            offset = 0;
        }
        machine_a64_emit_generated_unsigned_memory(encoder, register_number, base_register, offset, size, store);
    }
}

BUSTER_GLOBAL_LOCAL void machine_a64_emit_frame_load(MachineA64Encoder* encoder, u32 register_number, u32 offset)
{
    machine_a64_emit_frame_memory(encoder, register_number, offset, 8, false);
}

// Sixteen-byte V-register frame transfer off the X28 frame base, the
// canonical float-memory helper's q form: the scaled unsigned offset when
// it fits — a frame area with an odd callee-saved count leaves offsets
// only eight-aligned, so misalignment takes the same X16 materialize-and-
// add path as an out-of-range offset.
BUSTER_GLOBAL_LOCAL void machine_a64_emit_vector_frame_memory(MachineA64Encoder* encoder, u32 vector_register, u32 offset, bool store)
{
    u32 base_register = MACHINE_A64_X28;
    if (offset % 16 || offset / 16 > A64_IMM12_MAX)
    {
        machine_a64_emit_immediate(encoder, MACHINE_A64_X16, offset);
        machine_a64_emit(encoder, 0x8b000000 | (MACHINE_A64_X16 << 16) | (base_register << 5) | MACHINE_A64_X16);
        base_register = MACHINE_A64_X16;
        offset = 0;
    }
    machine_a64_emit(encoder, (store ? 0x3d800000u : 0x3dc00000u) | ((offset / 16) << 10) | (base_register << 5) | vector_register);
}

// The sized sibling: a scalar FP transfer of 1/2/4/8 bytes through the V
// register's low bytes, with the same X16 fallback for offsets outside
// the scaled unsigned form.
BUSTER_GLOBAL_LOCAL void machine_a64_emit_vector_frame_memory_sized(MachineA64Encoder* encoder, u32 vector_register, u32 offset, u32 size_log2, bool store)
{
    u32 base_register = MACHINE_A64_X28;
    u32 scale = 1u << size_log2;
    if (offset % scale || offset / scale > A64_IMM12_MAX)
    {
        machine_a64_emit_immediate(encoder, MACHINE_A64_X16, offset);
        machine_a64_emit(encoder, 0x8b000000 | (MACHINE_A64_X16 << 16) | (base_register << 5) | MACHINE_A64_X16);
        base_register = MACHINE_A64_X16;
        offset = 0;
    }
    u32 word = size_log2 == 0   ? (store ? 0x3d000000u : 0x3d400000u)
               : size_log2 == 1 ? (store ? 0x7d000000u : 0x7d400000u)
               : size_log2 == 2 ? (store ? 0xbd000000u : 0xbd400000u)
                                : (store ? 0xfd000000u : 0xfd400000u);
    machine_a64_emit(encoder, word | ((offset / scale) << 10) | (base_register << 5) | vector_register);
}

// Sized memory operation through a pointer register with a scaled unsigned
// immediate offset, for the aggregate copy loops; an offset outside the
// imm12 form is an encode error, which falls the function back whole.
BUSTER_GLOBAL_LOCAL void machine_a64_emit_pointer_memory(MachineA64Encoder* encoder, u32 register_number, u32 base_register, u32 offset, u32 size, bool store)
{
    u32 scale = size;
    if ((size != 1 && size != 2 && size != 4 && size != 8) || offset % scale || offset / scale > A64_IMM12_MAX)
    {
        encoder->error = true;
        return;
    }
    machine_a64_emit_generated_unsigned_memory(encoder, register_number, base_register, offset, size, store);
}

BUSTER_GLOBAL_LOCAL void machine_a64_emit_frame_store(MachineA64Encoder* encoder, u32 register_number, u32 offset)
{
    machine_a64_emit_frame_memory(encoder, register_number, offset, 8, true);
}

BUSTER_GLOBAL_LOCAL bool machine_a64_relax_branches(MachineA64Encoder* encoder, u32* block_offsets, u32 block_count, u32* row_offsets,
                                                    u32 row_count, MachineBuilderStream* fixups, MachineBuilderStream* call_sites,
                                                    MachineBuilderStream* epilogs);

#if BUSTER_INCLUDE_TESTS
bool machine_a64_test_emit_unsigned_memory(u8* bytes, u32 capacity, u32 register_number, u32 base_register, u32 offset, u32 size,
                                           bool store, bool frame_relative, u32* byte_count, bool* error)
{
    MachineA64Encoder encoder = {
        .bytes = bytes,
        .capacity = capacity,
    };
    if (frame_relative)
    {
        machine_a64_emit_frame_memory(&encoder, register_number, offset, size, store);
    }
    else
    {
        machine_a64_emit_pointer_memory(&encoder, register_number, base_register, offset, size, store);
    }
    if (byte_count)
    {
        *byte_count = encoder.count;
    }
    if (error)
    {
        *error = encoder.error;
    }
    return !encoder.error && !encoder.overflow;
}

bool machine_a64_test_emit_generated_opcode(u8* bytes, u32 capacity, u16 opcode, u32 operand0, u32 operand1, u32 operand2,
                                            u32 payload, u32* byte_count, bool* error)
{
    MachineA64Encoder encoder = {
        .bytes = bytes,
        .capacity = capacity,
    };
    machine_a64_emit_generated_opcode(&encoder, opcode, operand0, operand1, operand2, payload);
    if (byte_count)
    {
        *byte_count = encoder.count;
    }
    if (error)
    {
        *error = encoder.error;
    }
    return !encoder.error && !encoder.overflow;
}

// Test-only seam for the fixed-size position-independent transfer.  Keeping
// this wrapper under BUSTER_INCLUDE_TESTS leaves the production API unchanged
// while allowing sparse-layout tests to exercise arbitrary signed deltas.
bool machine_a64_test_emit_long_branch(u8* bytes, u32 capacity, s64 displacement, u32* byte_count)
{
    return machine_a64_emit_long_branch_bytes(bytes, capacity, displacement, byte_count);
}

// Return the exact monotonic tier the production planner uses for one edge:
// 0 = original direct encoding, 1 = inverse-cond skip + direct B (BCC only),
// 2 = fixed ADR/MOV/ADD/BR transfer.  `displacement` is measured from the
// conditional word; the short B sits four bytes later.
u8 machine_a64_test_branch_relaxation_tier(u16 opcode_value, u32 condition, s64 displacement)
{
    A64Opcode opcode = (A64Opcode)opcode_value;
    if (!((u64)displacement & 3u))
    {
        u32 word = 0;
        u32 patched = 0;
        if (opcode == A64_OPCODE_B)
        {
            word = UINT32_C(0x14000000);
            return a64_pc_relative_patch(opcode, word, displacement, &patched) ? 0u : 2u;
        }
        if (opcode == A64_OPCODE_B_COND && condition < 16u)
        {
            if (!a64_mc_encode(&(A64MCInst){
                                   .operands = {
                                       {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
                                       {.value = condition, .kind = A64_MC_OPERAND_IMMEDIATE},
                                   },
                                   .opcode = A64_OPCODE_B_COND,
                                   .operand_count = 2,
                               },
                               &word))
            {
                return UINT8_MAX;
            }
            if (a64_pc_relative_patch(A64_OPCODE_B_COND, word, displacement, &patched))
            {
                return 0u;
            }
            u32 inverse = 0;
            if (a64_condition_invert(condition, &inverse))
            {
                if (displacement < INT64_MIN + 4 || displacement > INT64_MAX - 4)
                {
                    return 2u;
                }
                word = UINT32_C(0x14000000);
                return a64_pc_relative_patch(A64_OPCODE_B, word, displacement - 4, &patched) ? 1u : 2u;
            }
        }
    }

    return UINT8_MAX;
}

// Sparse multi-fixup seam: this constructs the real MachineA64BranchFixup,
// call-site, epilog, and row-offset streams, then invokes the production
// convergence/final-patch helper in virtual-count mode. No code bytes are
// allocated; the helper still performs every checked range decision and
// scratch-transfer validation on its bounded seven-word stack buffer.
bool machine_a64_test_relax_sparse(Arena* arena, u32 code_size, MachineA64TestSparseFixup* sparse_fixups, u32 fixup_count,
                                   u32* final_code_size)
{
    if (!arena || (!sparse_fixups && fixup_count) || !final_code_size || code_size < 4)
    {
        return false;
    }
    MachineBuilderStream fixups;
    machine_stream_initialize(&fixups, sizeof(MachineA64BranchFixup));
    MachineBuilderStream call_sites;
    machine_stream_initialize(&call_sites, sizeof(MachineCallSite));
    MachineBuilderStream epilogs;
    machine_stream_initialize(&epilogs, sizeof(u32));
    u32 array_count = fixup_count ? fixup_count : 1;
    u32* block_offsets = arena_allocate(arena, u32, array_count);
    u32* row_offsets = arena_allocate(arena, u32, array_count);
    for (u32 index = 0; index < fixup_count; index += 1)
    {
        MachineA64TestSparseFixup* sparse = sparse_fixups + index;
        if ((sparse->source_offset & 3u) || (sparse->target_offset & 3u) || sparse->source_offset > code_size - 4)
        {
            return false;
        }
        block_offsets[index] = sparse->target_offset;
        row_offsets[index] = sparse->row_offset;
        MachineA64BranchFixup* fixup = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
        *fixup = (MachineA64BranchFixup){
            .patch_offset = sparse->source_offset,
            .block = index,
            .opcode = (A64Opcode)sparse->opcode,
            .condition = sparse->condition,
        };
        MachineCallSite* call_site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
        *call_site = (MachineCallSite){.code_offset = sparse->call_offset};
        u32* epilog = (u32*)machine_stream_append(arena, &epilogs);
        *epilog = sparse->epilog_offset;
    }
    MachineA64Encoder encoder = {
        .bytes = 0,
        .count = code_size,
        .capacity = UINT32_MAX,
        .sparse = true,
    };
    bool valid = machine_a64_relax_branches(&encoder, block_offsets, fixup_count, row_offsets, fixup_count, &fixups, &call_sites, &epilogs);
    if (!valid)
    {
        return false;
    }
    MachineA64BranchFixup* flattened_fixups = arena_allocate(arena, MachineA64BranchFixup, array_count);
    machine_stream_flatten(&fixups, flattened_fixups);
    MachineCallSite* flattened_calls = arena_allocate(arena, MachineCallSite, array_count);
    machine_stream_flatten(&call_sites, flattened_calls);
    u32* flattened_epilogs = arena_allocate(arena, u32, array_count);
    machine_stream_flatten(&epilogs, flattened_epilogs);
    for (u32 index = 0; index < fixup_count; index += 1)
    {
        sparse_fixups[index].source_offset = flattened_fixups[index].patch_offset;
        sparse_fixups[index].target_offset = block_offsets[index];
        sparse_fixups[index].row_offset = row_offsets[index];
        sparse_fixups[index].call_offset = flattened_calls[index].code_offset;
        sparse_fixups[index].epilog_offset = flattened_epilogs[index];
        sparse_fixups[index].tier = flattened_fixups[index].expanded;
    }
    *final_code_size = encoder.count;
    return true;
}
#endif

// Register-to-register copy; SP never appears here, so the orr form's zero
// register reading of 31 can never be misinterpreted.
BUSTER_GLOBAL_LOCAL void machine_a64_emit_move(MachineA64Encoder* encoder, u32 destination, u32 source)
{
    machine_a64_emit_generated_opcode(encoder, MACHINE_A64_MOV_RR, destination, source, 0, 0);
}

// Frame-slot placement offsets grow downward from the frame base; the
// X28-relative byte offset is their distance from the top of the frame
// area. The area's first 8 * push_count bytes — the offsets the shared
// placement reserved for the x86-64 pushes — hold the callee-saved saves.
BUSTER_GLOBAL_LOCAL u32 machine_a64_frame_offset(u32 frame_area, u32 placement_offset)
{
    return frame_area - placement_offset;
}

// Insert a four-byte-aligned relaxation sequence without invalidating any
// function-relative metadata.  The insertion point is always immediately
// after the original branch word, so the source row itself stays at the same
// offset; every object at or after the point moves together.  Checked adds
// make capacity/offset overflow a clean encode failure rather than a wrapped
// relocation.
BUSTER_GLOBAL_LOCAL bool machine_a64_insert_relaxation_bytes(MachineA64Encoder* encoder, u32 insertion_offset, u32 insertion_bytes,
                                                             u32* block_offsets, u32 block_count, u32* row_offsets, u32 row_count,
                                                             MachineBuilderStream* fixups, MachineBuilderStream* call_sites,
                                                             MachineBuilderStream* epilogs)
{
    if (!encoder || !insertion_bytes || encoder->count > encoder->capacity || insertion_offset > encoder->count ||
        insertion_bytes > encoder->capacity - encoder->count)
    {
        return false;
    }
    u32 old_count = encoder->count;
    if (!encoder->sparse)
    {
        memmove(encoder->bytes + insertion_offset + insertion_bytes, encoder->bytes + insertion_offset, old_count - insertion_offset);
        memset(encoder->bytes + insertion_offset, 0, insertion_bytes);
    }
    encoder->count = old_count + insertion_bytes;
    for (u32 index = 0; index < block_count; index += 1)
    {
        if (block_offsets[index] >= insertion_offset)
        {
            if (UINT32_MAX - block_offsets[index] < insertion_bytes)
            {
                return false;
            }
            block_offsets[index] += insertion_bytes;
        }
    }
    for (u32 index = 0; index < row_count; index += 1)
    {
        if (row_offsets[index] >= insertion_offset)
        {
            if (UINT32_MAX - row_offsets[index] < insertion_bytes)
            {
                return false;
            }
            row_offsets[index] += insertion_bytes;
        }
    }
    for (MachineBuilderChunk* chunk = fixups ? fixups->first : 0; chunk; chunk = chunk->next)
    {
        MachineA64BranchFixup* rows = (MachineA64BranchFixup*)(chunk + 1);
        for (u32 row_index = 0; row_index < chunk->count; row_index += 1)
        {
            if (rows[row_index].patch_offset >= insertion_offset)
            {
                if (UINT32_MAX - rows[row_index].patch_offset < insertion_bytes)
                {
                    return false;
                }
                rows[row_index].patch_offset += insertion_bytes;
            }
        }
    }
    for (MachineBuilderChunk* chunk = call_sites ? call_sites->first : 0; chunk; chunk = chunk->next)
    {
        MachineCallSite* rows = (MachineCallSite*)(chunk + 1);
        for (u32 row_index = 0; row_index < chunk->count; row_index += 1)
        {
            if (rows[row_index].code_offset >= insertion_offset)
            {
                if (UINT32_MAX - rows[row_index].code_offset < insertion_bytes)
                {
                    return false;
                }
                rows[row_index].code_offset += insertion_bytes;
            }
        }
    }
    for (MachineBuilderChunk* chunk = epilogs ? epilogs->first : 0; chunk; chunk = chunk->next)
    {
        u32* rows = (u32*)(chunk + 1);
        for (u32 row_index = 0; row_index < chunk->count; row_index += 1)
        {
            if (rows[row_index] >= insertion_offset)
            {
                if (UINT32_MAX - rows[row_index] < insertion_bytes)
                {
                    return false;
                }
                rows[row_index] += insertion_bytes;
            }
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_relax_expand_fixup(MachineA64Encoder* encoder, MachineA64BranchFixup* fixup, u32* block_offsets,
                                                        u32 block_count, u32* row_offsets, u32 row_count, MachineBuilderStream* fixups,
                                                        MachineBuilderStream* call_sites, MachineBuilderStream* epilogs, u8 expansion_kind)
{
    if (!encoder || !fixup || fixup->label_address || !expansion_kind || expansion_kind > 2 || fixup->expanded >= expansion_kind || encoder->count < 4 ||
        fixup->patch_offset > encoder->count - 4 || fixup->opcode == A64_OPCODE_INVALID)
    {
        return false;
    }
    // A conditional branch first relaxes to inverse-cond-skip + direct B
    // whenever the target remains inside B's wider range.  If a later
    // insertion pushes that B out of range, the second tier grows the same
    // slot to the fixed seven-word ADR/MOV/ADD/BR transfer.
    u32 insertion_offset = fixup->patch_offset + 4u;
    u32 insertion_bytes = MACHINE_A64_LONG_BRANCH_BYTES - 4u;
    if (fixup->opcode == A64_OPCODE_B_COND && expansion_kind == 1)
    {
        insertion_bytes = 4u;
    }
    else if (fixup->opcode == A64_OPCODE_B_COND && fixup->expanded == 0 && expansion_kind == 2)
    {
        // The original fallthrough B is still live at P+4; unlike an
        // unconditional B, the conditional word at P cannot be reused by
        // the long transfer.  Make room for all seven transfer words before
        // that fallthrough edge (which therefore moves to P+32).
        insertion_bytes = MACHINE_A64_LONG_BRANCH_BYTES;
    }
    else if (fixup->opcode == A64_OPCODE_B_COND && fixup->expanded == 1 && expansion_kind == 2)
    {
        insertion_offset += 4u;
    }
    if (!machine_a64_insert_relaxation_bytes(encoder, insertion_offset, insertion_bytes, block_offsets, block_count, row_offsets, row_count, fixups,
                                             call_sites, epilogs))
    {
        return false;
    }
    // The inserted bytes are initialized to zero and repopulated by the
    // final repatch pass.  Writing a valid zero-delta transfer now keeps the
    // intermediate layout independently decodable while more fixups grow.
    u32 transfer_offset = fixup->opcode == A64_OPCODE_B_COND ? fixup->patch_offset + 4u : fixup->patch_offset;
    u32 transfer_count = 0;
    if (encoder->sparse)
    {
        // The sparse seam validates the exact transfer in the final pass on
        // a bounded scratch buffer; only offsets/tiers are needed here.
        fixup->expanded = expansion_kind;
        return true;
    }
    if (fixup->opcode == A64_OPCODE_B_COND && expansion_kind == 1)
    {
        u32 word = 0;
        if (!a64_mc_encode(&(A64MCInst){.operands = {{.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE}}, .opcode = A64_OPCODE_B, .operand_count = 1}, &word))
        {
            return false;
        }
        memcpy(encoder->bytes + transfer_offset, &word, sizeof(word));
        transfer_count = sizeof(word);
    }
    else if (!machine_a64_emit_long_branch_bytes(encoder->bytes + transfer_offset, MACHINE_A64_LONG_BRANCH_BYTES, 0, &transfer_count) ||
             transfer_count != MACHINE_A64_LONG_BRANCH_BYTES)
    {
        return false;
    }
    fixup->expanded = expansion_kind;
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_relax_word(MachineA64Encoder* encoder, MachineA64BranchFixup* fixup, u32 offset, u32* word)
{
    if (!encoder || !fixup || !word || offset > encoder->count - 4)
    {
        return false;
    }
    if (!encoder->sparse)
    {
        memcpy(word, encoder->bytes + offset, sizeof(*word));
        return true;
    }
    // Sparse mode has no bytes to read. Every word the convergence pass can
    // inspect is an original direct edge (the inserted transfer words are
    // never represented by a fixup), so reconstruct the same canonical MC
    // form that production emission placed at this offset.
    if (offset == fixup->patch_offset && fixup->opcode == A64_OPCODE_B_COND)
    {
        return a64_mc_encode(&(A64MCInst){
                                  .operands = {
                                      {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
                                      {.value = fixup->condition, .kind = A64_MC_OPERAND_IMMEDIATE},
                                  },
                                  .opcode = A64_OPCODE_B_COND,
                                  .operand_count = 2,
                              },
                              word);
    }
    *word = UINT32_C(0x14000000);
    return true;
}

// Shared monotonic planner/final patcher.  Production mode mutates the real
// byte buffer; sparse test mode runs this exact function with virtual count
// and offsets, skipping only byte movement while retaining every tier,
// convergence bound, and metadata update callback.
BUSTER_GLOBAL_LOCAL bool machine_a64_relax_branches(MachineA64Encoder* encoder, u32* block_offsets, u32 block_count, u32* row_offsets,
                                                    u32 row_count, MachineBuilderStream* fixups, MachineBuilderStream* call_sites,
                                                    MachineBuilderStream* epilogs)
{
    if (!encoder || !fixups || (block_count && !block_offsets) || (row_count && !row_offsets))
    {
        return false;
    }
    u64 relaxation_steps = 0;
    u64 relaxation_limit = (u64)fixups->total_count * 2u + 1u;
    for (;;)
    {
        bool changed = false;
        for (MachineBuilderChunk* chunk = fixups->first; chunk && !changed; chunk = chunk->next)
        {
            MachineA64BranchFixup* rows = (MachineA64BranchFixup*)(chunk + 1);
            for (u32 row_index = 0; row_index < chunk->count; row_index += 1)
            {
                MachineA64BranchFixup* fixup = rows + row_index;
                if (fixup->block >= block_count || encoder->count < 4 || fixup->patch_offset > encoder->count - 4)
                {
                    return false;
                }
                if (fixup->label_address)
                {
                    if (encoder->count < MACHINE_A64_BLOCK_ADDRESS_BYTES || fixup->patch_offset > encoder->count - MACHINE_A64_BLOCK_ADDRESS_BYTES)
                    {
                        return false;
                    }
                    continue;
                }
                if (fixup->opcode == A64_OPCODE_B && fixup->expanded)
                {
                    continue;
                }
                if (fixup->opcode == A64_OPCODE_B_COND && fixup->expanded == 2)
                {
                    continue;
                }
                u32 word = 0;
                if (!machine_a64_relax_word(encoder, fixup, fixup->patch_offset, &word))
                {
                    return false;
                }
                s64 displacement = (s64)(u64)block_offsets[fixup->block] - (s64)(u64)fixup->patch_offset;
                if (fixup->opcode == A64_OPCODE_B)
                {
                    u32 ignored = 0;
                    bool direct_fits = a64_pc_relative_patch(A64_OPCODE_B, word, displacement, &ignored);
                    if (!direct_fits)
                    {
                        if (++relaxation_steps > relaxation_limit ||
                            !machine_a64_relax_expand_fixup(encoder, fixup, block_offsets, block_count, row_offsets, row_count, fixups, call_sites,
                                                            epilogs, 2))
                        {
                            return false;
                        }
                        changed = true;
                    }
                }
                else if (fixup->opcode == A64_OPCODE_B_COND)
                {
                    u32 ignored = 0;
                    bool direct_fits = a64_pc_relative_patch(A64_OPCODE_B_COND, word, displacement, &ignored);
                    if (fixup->expanded == 0 && direct_fits)
                    {
                        continue;
                    }
                    u8 desired = 0;
                    if (fixup->expanded == 0)
                    {
                        u32 inverse = 0;
                        if (!a64_condition_invert(fixup->condition, &inverse) || encoder->count < 8 || fixup->patch_offset > encoder->count - 8)
                        {
                            return false;
                        }
                        u32 direct_word = 0;
                        if (!machine_a64_relax_word(encoder, fixup, fixup->patch_offset + 4u, &direct_word))
                        {
                            return false;
                        }
                        s64 direct_displacement = (s64)(u64)block_offsets[fixup->block] - (s64)(u64)(fixup->patch_offset + 4u);
                        desired = a64_pc_relative_patch(A64_OPCODE_B, direct_word, direct_displacement, &ignored) ? 1u : 2u;
                    }
                    else
                    {
                        if (encoder->count < 8 || fixup->patch_offset > encoder->count - 8)
                        {
                            return false;
                        }
                        u32 direct_word = 0;
                        if (!machine_a64_relax_word(encoder, fixup, fixup->patch_offset + 4u, &direct_word))
                        {
                            return false;
                        }
                        s64 direct_displacement = (s64)(u64)block_offsets[fixup->block] - (s64)(u64)(fixup->patch_offset + 4u);
                        desired = a64_pc_relative_patch(A64_OPCODE_B, direct_word, direct_displacement, &ignored) ? 0u : 2u;
                    }
                    if (desired > fixup->expanded)
                    {
                        if (++relaxation_steps > relaxation_limit ||
                            !machine_a64_relax_expand_fixup(encoder, fixup, block_offsets, block_count, row_offsets, row_count, fixups, call_sites,
                                                            epilogs, desired))
                        {
                            return false;
                        }
                        changed = true;
                    }
                }
                else
                {
                    return false;
                }
                if (changed)
                {
                    break;
                }
            }
        }
        if (!changed)
        {
            break;
        }
    }

    for (MachineBuilderChunk* chunk = fixups->first; chunk; chunk = chunk->next)
    {
        MachineA64BranchFixup* rows = (MachineA64BranchFixup*)(chunk + 1);
        for (u32 row_index = 0; row_index < chunk->count; row_index += 1)
        {
            MachineA64BranchFixup* fixup = rows + row_index;
            if (fixup->block >= block_count || encoder->count < 4 || fixup->patch_offset > encoder->count - 4)
            {
                return false;
            }
            if (fixup->label_address)
            {
                if (encoder->count < MACHINE_A64_BLOCK_ADDRESS_BYTES || fixup->patch_offset > encoder->count - MACHINE_A64_BLOCK_ADDRESS_BYTES)
                {
                    return false;
                }
                s64 displacement = (s64)(u64)block_offsets[fixup->block] - (s64)(u64)fixup->patch_offset;
                u64 encoded_displacement = (u64)displacement;
                if (!encoder->sparse)
                {
                    u32 words[4] = {
                        UINT32_C(0xd2800000) | ((u32)(encoded_displacement & 0xffffu) << 5) | MACHINE_A64_X17,
                        UINT32_C(0xf2800000) | (UINT32_C(1) << 21) | ((u32)((encoded_displacement >> 16) & 0xffffu) << 5) | MACHINE_A64_X17,
                        UINT32_C(0xf2800000) | (UINT32_C(2) << 21) | ((u32)((encoded_displacement >> 32) & 0xffffu) << 5) | MACHINE_A64_X17,
                        UINT32_C(0xf2800000) | (UINT32_C(3) << 21) | ((u32)((encoded_displacement >> 48) & 0xffffu) << 5) | MACHINE_A64_X17,
                    };
                    memcpy(encoder->bytes + fixup->patch_offset + 4, words, sizeof(words));
                }
                continue;
            }
            if (fixup->expanded == 0)
            {
                s64 displacement = (s64)(u64)block_offsets[fixup->block] - (s64)(u64)fixup->patch_offset;
                u32 word = 0;
                u32 patched = 0;
                if (!machine_a64_relax_word(encoder, fixup, fixup->patch_offset, &word) ||
                    !a64_pc_relative_patch(fixup->opcode, word, displacement, &patched))
                {
                    return false;
                }
                if (!encoder->sparse)
                {
                    memcpy(encoder->bytes + fixup->patch_offset, &patched, sizeof(patched));
                }
            }
            else if (fixup->opcode == A64_OPCODE_B)
            {
                s64 displacement = (s64)(u64)block_offsets[fixup->block] - (s64)(u64)fixup->patch_offset;
                u8 scratch[MACHINE_A64_LONG_BRANCH_BYTES];
                u8* destination = encoder->sparse ? scratch : encoder->bytes + fixup->patch_offset;
                u32 capacity = encoder->sparse ? sizeof(scratch) : encoder->capacity - fixup->patch_offset;
                u32 transfer_count = 0;
                if (!machine_a64_emit_long_branch_bytes(destination, capacity, displacement, &transfer_count) ||
                    transfer_count != MACHINE_A64_LONG_BRANCH_BYTES)
                {
                    return false;
                }
            }
            else if (fixup->opcode == A64_OPCODE_B_COND)
            {
                u32 inverse = 0;
                if (!a64_condition_invert(fixup->condition, &inverse) || encoder->count < 8 || fixup->patch_offset > encoder->count - 8)
                {
                    return false;
                }
                u32 condition_word = 0;
                if (!a64_mc_encode(&(A64MCInst){
                                       .operands = {
                                           {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
                                           {.value = inverse, .kind = A64_MC_OPERAND_IMMEDIATE},
                                       },
                                       .opcode = A64_OPCODE_B_COND,
                                       .operand_count = 2,
                                   },
                                   &condition_word))
                {
                    return false;
                }
                s64 skip_displacement = fixup->expanded == 1 ? 8 : MACHINE_A64_LONG_CONDITIONAL_BYTES;
                u32 patched_condition = 0;
                if (!a64_pc_relative_patch(A64_OPCODE_B_COND, condition_word, skip_displacement, &patched_condition))
                {
                    return false;
                }
                if (!encoder->sparse)
                {
                    memcpy(encoder->bytes + fixup->patch_offset, &patched_condition, sizeof(patched_condition));
                }
                u32 direct_offset = fixup->patch_offset + 4u;
                if (fixup->expanded == 1)
                {
                    u32 direct_word = 0;
                    u32 patched_direct = 0;
                    if (!machine_a64_relax_word(encoder, fixup, direct_offset, &direct_word))
                    {
                        return false;
                    }
                    s64 displacement = (s64)(u64)block_offsets[fixup->block] - (s64)(u64)direct_offset;
                    if (!a64_pc_relative_patch(A64_OPCODE_B, direct_word, displacement, &patched_direct))
                    {
                        return false;
                    }
                    if (!encoder->sparse)
                    {
                        memcpy(encoder->bytes + direct_offset, &patched_direct, sizeof(patched_direct));
                    }
                }
                else
                {
                    s64 displacement = (s64)(u64)block_offsets[fixup->block] - (s64)(u64)direct_offset;
                    u8 scratch[MACHINE_A64_LONG_BRANCH_BYTES];
                    u8* destination = encoder->sparse ? scratch : encoder->bytes + direct_offset;
                    u32 capacity = encoder->sparse ? sizeof(scratch) : encoder->capacity - direct_offset;
                    u32 transfer_count = 0;
                    if (!machine_a64_emit_long_branch_bytes(destination, capacity, displacement, &transfer_count) ||
                        transfer_count != MACHINE_A64_LONG_BRANCH_BYTES)
                    {
                        return false;
                    }
                }
            }
            else
            {
                return false;
            }
        }
    }
    return true;
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
    // the total is too. Use a checked sum before narrowing the frame size.
    u64 frame_area64 = (u64)placement->frame_size + 8u * push_count;
    if (!placement->valid || frame_area64 > UINT32_MAX - 16u)
    {
        return result;
    }
    u32 frame_area = (u32)frame_area64;
    bool windows_frame = function->windows_aarch64_frame;
    u32 windows_save_area = codegen_a64_windows_save_area_size(push_count);
    if (windows_frame && frame_area > UINT32_MAX - windows_save_area)
    {
        return result;
    }
    u32 frame_total = frame_area + 16;
    bool large_save_offset = frame_area > MACHINE_A64_DIRECT_SAVE_MAX;
    // Per-row worst-case byte budget: constants and remainders expand, the
    // epilogue carries the frame release, everything else is one word plus
    // slack for large-offset frame addressing.
    u32 frame_chunk_words = frame_total / A64_SP_ADJUST_CHUNK + 1;
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
            capacity64 += 20 + (large_save_offset ? 8u : 0u) + (u64)frame_chunk_words * 4 + (u64)push_count * 4;
            break;
        case MACHINE_A64_B:
            // A direct word may grow to the fixed seven-word transfer.
            capacity64 += MACHINE_A64_LONG_BRANCH_BYTES;
            break;
        case MACHINE_A64_BCC:
            // The conditional row already emits two direct words.  Its
            // taken edge may require inverse-cond + long transfer, and its
            // fallthrough edge may independently require a long transfer.
            capacity64 += MACHINE_A64_LONG_CONDITIONAL_BYTES + MACHINE_A64_LONG_BRANCH_BYTES;
            break;
        case MACHINE_A64_LEA_BLOCK:
            capacity64 += MACHINE_A64_BLOCK_ADDRESS_BYTES;
            break;
        case MACHINE_A64_INDIRECT_BRANCH:
            capacity64 += 4;
            break;
        case MACHINE_A64_COPY_FRAME_FROM_FRAME:
        case MACHINE_A64_COPY_FRAME_FROM_PTR:
        case MACHINE_A64_COPY_PTR_FROM_FRAME:
            // One load/store word pair per eight-byte chunk, plus sized
            // tail accesses that may each take the large-offset form.
            capacity64 += ((u64)capacity_row->payload / 8) * (large_save_offset ? 32u : 8u) + (large_save_offset ? 96u : 48u);
            break;
        case MACHINE_A64_VA_SAVE:
            // Eight X and eight Q stores, each possibly using a large offset.
            capacity64 += 16 * 24;
            break;
        case MACHINE_A64_VLOAD_FRAME:
        case MACHINE_A64_VSTORE_FRAME:
        case MACHINE_A64_VLOAD_FRAME_SIZED:
        case MACHINE_A64_VSTORE_FRAME_SIZED:
            // One V transfer, possibly behind the X16 materialize-and-add.
            capacity64 += 24;
            break;
        case MACHINE_A64_VA_ARG:
            // Two bounded part sequences around the cursor split, each
            // part possibly storing through the large-offset frame form.
            capacity64 += 128 + 2u * ((u64)MACHINE_VA_ARG_PART_LIMIT * 32 + 12);
            break;
        case MACHINE_A64_ATOMIC_LOAD_PAIR:
            // Exclusive pair read-back loop plus two result-slot stores.
            capacity64 += 12 + 2u * (large_save_offset ? 16u : 4u);
            break;
        case MACHINE_A64_ATOMIC_STORE_PAIR:
            // Two value-slot loads, a worst-case four-word high-half mask,
            // AND, and the exclusive pair replacement loop.
            capacity64 += 32 + 2u * (large_save_offset ? 16u : 4u);
            break;
        case MACHINE_A64_ATOMIC_RMW_PAIR:
            // Two input loads, a pair loop with at most two arithmetic
            // words, and two result stores. Every frame access may expand.
            capacity64 += 20 + 4u * (large_save_offset ? 16u : 4u);
            break;
        case MACHINE_A64_ATOMIC_CAS_PAIR:
            // Expected/desired loads, compare and conditional replacement,
            // pair loop, and result stores; no mismatch escape from LDXP.
            capacity64 += 28 + 6u * (large_save_offset ? 16u : 4u);
            break;
        case MACHINE_A64_ATOMIC_RMW:
            // ld(a)xr, operation, st(l)xr, cbnz.
            capacity64 += 16;
            break;
        case MACHINE_A64_CLEAR_INSTRUCTION_CACHE:
            // Two cache-maintenance loops, alignment, and barriers.
            capacity64 += 64;
            break;
        case MACHINE_A64_ATOMIC_CAS:
            // ld(a)xr, cmp, b.ne, st(l)xr, cbnz, clrex.
            capacity64 += 24;
            break;
        case MACHINE_A64_STACK_ALLOCATE:
            // Two mask materializations plus the eleven-word canonical
            // probe loop.
            capacity64 += 80;
            break;
        case MACHINE_A64_SWITCH:
            // Per case: the X17 materialization, the compare, and a
            // conditional edge that may relax long; then the default
            // transfer.
            capacity64 += (u64)capacity_row->flags * (20 + MACHINE_A64_LONG_CONDITIONAL_BYTES) + MACHINE_A64_LONG_BRANCH_BYTES;
            break;
        default:
            // A 32-bit frame offset needs at most two MOV halfwords, ADD,
            // and the memory instruction. Small frames keep their budget.
            capacity64 += large_save_offset ? 16u : 12u;
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
    // PE keeps saves beside the frame chain before establishing X29.
    // ELF/Mach-O retain the existing frame, with saves above the body slots.
    // Module unwind actions describe the instructions of the selected shape.
    if (windows_frame)
    {
        // Put every save within the unwind format's small SP-relative range.
        // X29 points at this frame chain throughout the body, including VLA
        // allocations. Its value also recovers SP directly in each epilogue.
        u32 chain_fields[] = {MACHINE_A64_X29, MACHINE_A64_SP, MACHINE_A64_X30, (0u - windows_save_area / 8u) & 0x7fu};
        machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_STPXPRE, chain_fields, BUSTER_ARRAY_LENGTH(chain_fields));
        u32 slot = 16;
        for (u32 saved_register = 0; saved_register < MACHINE_A64_REGISTER_COUNT; saved_register += 1)
        {
            if ((placement->callee_saved_mask >> saved_register) & 1u)
            {
                machine_a64_emit_generated_unsigned_memory(&encoder, saved_register, MACHINE_A64_SP, slot, 8, true);
                slot += 8;
            }
        }
        machine_a64_emit_generated_unsigned_memory(&encoder, MACHINE_A64_X28, MACHINE_A64_SP, slot, 8, true);
        u32 frame_fields[] = {MACHINE_A64_X29, MACHINE_A64_SP, 0};
        machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, frame_fields, BUSTER_ARRAY_LENGTH(frame_fields));
        CodegenBuffer probe = {.bytes = encoder.bytes, .count = encoder.count, .capacity = encoder.capacity};
        bool large_probe = codegen_a64_windows_large_stack_adjust(&probe, frame_area, true, 0, 0);
        encoder.count = (u32)probe.count;
        encoder.overflow |= probe.error != CODEGEN_ERROR_NONE;
        if (!large_probe && frame_area)
        {
            machine_a64_emit(&encoder, 0xd10003ffu | (frame_area << 10));
            machine_a64_emit_generated_unsigned_memory(&encoder, MACHINE_A64_SP, MACHINE_A64_SP, 0, 8, true);
        }
        u32 base_fields[] = {MACHINE_A64_X28, MACHINE_A64_SP, 0};
        machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, base_fields, BUSTER_ARRAY_LENGTH(base_fields));
    }
    else
    {
        machine_a64_emit(&encoder, 0xa9bf7bfd);
        {
            u32 fields[] = {MACHINE_A64_X29, MACHINE_A64_SP, 0};
            machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, fields, BUSTER_ARRAY_LENGTH(fields));
        }
        u32 frame_remaining = frame_total;
        while (frame_remaining)
        {
            u32 frame_chunk = BUSTER_MIN(frame_remaining, A64_SP_ADJUST_CHUNK);
            machine_a64_emit(&encoder, 0xd10003ff | (frame_chunk << 10));
            machine_a64_emit_generated_unsigned_memory(&encoder, MACHINE_A64_SP, MACHINE_A64_SP, 0, 8, true);
            frame_remaining -= frame_chunk;
        }
        u32 save_base = MACHINE_A64_SP;
        if (large_save_offset)
        {
            // X29 remains at the saved FP/LR pair. The save area is immediately
            // below it regardless of frame size; X16 is reserved from allocation.
            machine_a64_emit_immediate(&encoder, MACHINE_A64_X16, 16u + 8u * push_count);
            u32 fields[] = {MACHINE_A64_X16, MACHINE_A64_X29, 0, MACHINE_A64_X16};
            machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_SUBXRS, fields, BUSTER_ARRAY_LENGTH(fields));
            save_base = MACHINE_A64_X16;
        }
        u32 save_slot = 0;
        for (u32 saved_register = 0; saved_register < MACHINE_A64_REGISTER_COUNT; saved_register += 1)
        {
            if (!((placement->callee_saved_mask >> saved_register) & 1u))
            {
                continue;
            }
            save_slot += 1;
            machine_a64_emit_generated_unsigned_memory(&encoder, saved_register, save_base,
                                                        large_save_offset ? 8u * (push_count - save_slot) : frame_area - 8u * save_slot, 8, true);
        }
        machine_a64_emit_generated_unsigned_memory(&encoder, MACHINE_A64_X28, save_base, large_save_offset ? 8u * push_count : frame_area, 8, true);
        {
            u32 fields[] = {MACHINE_A64_X28, MACHINE_A64_SP, 0};
            machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, fields, BUSTER_ARRAY_LENGTH(fields));
        }
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
                if (edit->kind == MACHINE_EDIT_SPILL || edit->kind == MACHINE_EDIT_TEMP_SPILL)
                {
                    u32 edit_frame_offset = edit->kind == MACHINE_EDIT_TEMP_SPILL ? placement->edge_copy_temporary_offset + edit->subject
                                                                                 : placement->virtual_register_offsets[edit->subject];
                    machine_a64_emit_frame_store(&encoder, edit->location, machine_a64_frame_offset(frame_area, edit_frame_offset));
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
                    u32 edit_frame_offset = edit->kind == MACHINE_EDIT_TEMP_RELOAD ? placement->edge_copy_temporary_offset + edit->subject
                                                                                  : placement->virtual_register_offsets[edit->subject];
                    machine_a64_emit_frame_load(&encoder, edit->location, machine_a64_frame_offset(frame_area, edit_frame_offset));
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
                    machine_a64_emit_generated_opcode(&encoder, instruction->opcode, operand_registers[0], operand_registers[1], operand_registers[2],
                                                      instruction->payload);
                }
                break;
            case MACHINE_A64_MOV32_RR:
            case MACHINE_A64_SXTB:
            case MACHINE_A64_SXTH:
            case MACHINE_A64_SXTW:
            case MACHINE_A64_UXTB:
            case MACHINE_A64_UXTH:
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
            case MACHINE_A64_MUL32:
            case MACHINE_A64_MUL64:
            case MACHINE_A64_UMULH64:
            case MACHINE_A64_SDIV32:
            case MACHINE_A64_SDIV64:
            case MACHINE_A64_UDIV32:
            case MACHINE_A64_UDIV64:
            case MACHINE_A64_SREM32:
            case MACHINE_A64_SREM64:
            case MACHINE_A64_UREM32:
            case MACHINE_A64_UREM64:
            case MACHINE_A64_LSL32:
            case MACHINE_A64_LSL64:
            case MACHINE_A64_ASR32:
            case MACHINE_A64_ASR64:
            case MACHINE_A64_LSR32:
            case MACHINE_A64_LSR64:
            case MACHINE_A64_NEG32:
            case MACHINE_A64_NEG64:
            case MACHINE_A64_NOT32:
            case MACHINE_A64_NOT64:
            case MACHINE_A64_CLZ32:
            case MACHINE_A64_CLZ64:
            case MACHINE_A64_RBIT32:
            case MACHINE_A64_RBIT64:
            case MACHINE_A64_CMP32:
            case MACHINE_A64_CMP64:
            case MACHINE_A64_CMP_ZERO:
            case MACHINE_A64_CSET:
                machine_a64_emit_generated_opcode(&encoder, instruction->opcode, operand_registers[0], operand_registers[1], operand_registers[2],
                                                  instruction->payload);
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
                // The outgoing area's base is the stack pointer a call sees,
                // which is X28 itself: the shared placement pins its offset
                // at frame_size, but the a64 frame area extends past that by
                // the callee-saved save words, so the slot is addressed
                // directly rather than through the top-relative mapping.
                u32 store_slot = machine_ref_payload(instruction->operands[0]);
                u32 store_offset = function->outgoing_bytes && store_slot == function->outgoing_slot
                                       ? instruction->payload
                                       : machine_a64_frame_offset(frame_area, placement->stack_slot_offsets[store_slot] - instruction->payload);
                machine_a64_emit_frame_memory(&encoder, operand_registers[1], store_offset, size, true);
            }
            break;
            case MACHINE_A64_LOAD_PTR8:
                machine_a64_emit_generated_unsigned_memory(&encoder, operand_registers[0], operand_registers[1], 0, 1, false);
                break;
            case MACHINE_A64_LOAD_PTR16:
                machine_a64_emit_generated_unsigned_memory(&encoder, operand_registers[0], operand_registers[1], 0, 2, false);
                break;
            case MACHINE_A64_LOAD_PTR32:
                machine_a64_emit_generated_unsigned_memory(&encoder, operand_registers[0], operand_registers[1], 0, 4, false);
                break;
            case MACHINE_A64_LOAD_PTR64:
                machine_a64_emit_generated_unsigned_memory(&encoder, operand_registers[0], operand_registers[1], 0, 8, false);
                break;
            case MACHINE_A64_STORE_PTR8:
                machine_a64_emit_generated_unsigned_memory(&encoder, operand_registers[1], operand_registers[0], 0, 1, true);
                break;
            case MACHINE_A64_STORE_PTR16:
                machine_a64_emit_generated_unsigned_memory(&encoder, operand_registers[1], operand_registers[0], 0, 2, true);
                break;
            case MACHINE_A64_STORE_PTR32:
                machine_a64_emit_generated_unsigned_memory(&encoder, operand_registers[1], operand_registers[0], 0, 4, true);
                break;
            case MACHINE_A64_STORE_PTR64:
                machine_a64_emit_generated_unsigned_memory(&encoder, operand_registers[1], operand_registers[0], 0, 8, true);
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
                if (frame_offset <= A64_IMM12_MAX)
                {
                    u32 fields[] = {operand_registers[0], MACHINE_A64_X28, frame_offset};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, fields, BUSTER_ARRAY_LENGTH(fields));
                }
                else
                {
                    machine_a64_emit_immediate(&encoder, operand_registers[0], frame_offset);
                    u32 fields[] = {operand_registers[0], MACHINE_A64_X28, 0, operand_registers[0]};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRS, fields, BUSTER_ARRAY_LENGTH(fields));
                }
            }
            break;
            case MACHINE_A64_LEA_OFFSET:
            {
                u32 displacement = instruction->payload;
                if (displacement <= A64_IMM12_MAX)
                {
                    u32 fields[] = {operand_registers[0], operand_registers[1], displacement};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, fields, BUSTER_ARRAY_LENGTH(fields));
                }
                else if (operand_registers[0] != operand_registers[1])
                {
                    machine_a64_emit_immediate(&encoder, operand_registers[0], displacement);
                    u32 fields[] = {operand_registers[0], operand_registers[1], 0, operand_registers[0]};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRS, fields, BUSTER_ARRAY_LENGTH(fields));
                }
                else
                {
                    machine_a64_emit_immediate(&encoder, MACHINE_A64_X16, displacement);
                    u32 fields[] = {operand_registers[0], operand_registers[1], 0, MACHINE_A64_X16};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRS, fields, BUSTER_ARRAY_LENGTH(fields));
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
                machine_a64_emit_generated_opcode(&encoder, instruction->opcode, operand_registers[0], operand_registers[1], operand_registers[2],
                                                  instruction->payload);
                break;
            case MACHINE_A64_FMOV_FROM_VEC:
                machine_a64_emit_generated_opcode(&encoder, instruction->opcode, operand_registers[0], operand_registers[1], operand_registers[2],
                                                  instruction->payload);
                break;
            case MACHINE_A64_FARITH:
            {
                // Bridge both bit images into the canonical float scratches
                // V0/V1, run the arithmetic, bridge the result back out. The
                // fadd/fsub/fmul/fdiv base words are the canonical emitter's
                // (Rd=0, Rn=0, Rm=1); the wide bit selects the d-form, and a
                // scalar s-form write zeroes the upper vector bits, which is
                // what keeps the returned f32 image zero-extended.
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_FMOV_TO_VEC, operand_registers[1], 0, 0, 0);
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_FMOV_TO_VEC, operand_registers[2], 0, 0, 1);
                u32 arith_selector = instruction->payload & 0xffu;
                u32 arith_word = arith_selector == 0   ? 0x1e212800u
                                 : arith_selector == 1 ? 0x1e213800u
                                 : arith_selector == 2 ? 0x1e210800u
                                                       : 0x1e211800u;
                if (instruction->payload & 0x100u)
                {
                    arith_word |= 0x00400000u;
                }
                machine_a64_emit(&encoder, arith_word);
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_FMOV_FROM_VEC, operand_registers[0], 0, 0, 0);
            }
            break;
            case MACHINE_A64_FCMP_SET:
                // fcmp through V0/V1, then the same w-form cset the integer
                // compares use; the payload condition already encodes the
                // unordered-false C semantics.
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_FMOV_TO_VEC, operand_registers[1], 0, 0, 0);
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_FMOV_TO_VEC, operand_registers[2], 0, 0, 1);
                machine_a64_emit(&encoder, (instruction->payload & 0x100u) ? 0x1e612000u : 0x1e212000u);
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_CSET, operand_registers[0], 0, 0, instruction->payload & 0xfu);
                break;
            case MACHINE_A64_CVT_F32_TO_F64:
            case MACHINE_A64_CVT_F64_TO_F32:
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_FMOV_TO_VEC, operand_registers[1], 0, 0, 0);
                machine_a64_emit(&encoder, instruction->opcode == MACHINE_A64_CVT_F32_TO_F64 ? 0x1e22c000u : 0x1e624000u);
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_FMOV_FROM_VEC, operand_registers[0], 0, 0, 0);
                break;
            case MACHINE_A64_CVT_I64_TO_F32:
            case MACHINE_A64_CVT_I64_TO_F64:
            case MACHINE_A64_CVT_U64_TO_F32:
            case MACHINE_A64_CVT_U64_TO_F64:
            {
                // scvtf/ucvtf read the general source directly (Rn), land in
                // V0, and the result bridges back as a bit image.
                bool convert_signed = instruction->opcode == MACHINE_A64_CVT_I64_TO_F32 || instruction->opcode == MACHINE_A64_CVT_I64_TO_F64;
                bool to_f64 = instruction->opcode == MACHINE_A64_CVT_I64_TO_F64 || instruction->opcode == MACHINE_A64_CVT_U64_TO_F64;
                machine_a64_emit(&encoder, (convert_signed ? 0x9e220000u : 0x9e230000u) | (to_f64 ? 0x00400000u : 0u) | ((u32)operand_registers[1] << 5));
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_FMOV_FROM_VEC, operand_registers[0], 0, 0, 0);
            }
            break;
            case MACHINE_A64_CVT_F32_TO_I64:
            case MACHINE_A64_CVT_F64_TO_I64:
            case MACHINE_A64_CVT_F32_TO_U64:
            case MACHINE_A64_CVT_F64_TO_U64:
            {
                // fcvtzs/fcvtzu write the full 64-bit general destination
                // (Rd) from V0, exactly the canonical x9 image.
                bool to_unsigned = instruction->opcode == MACHINE_A64_CVT_F32_TO_U64 || instruction->opcode == MACHINE_A64_CVT_F64_TO_U64;
                bool from_f64 = instruction->opcode == MACHINE_A64_CVT_F64_TO_I64 || instruction->opcode == MACHINE_A64_CVT_F64_TO_U64;
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_FMOV_TO_VEC, operand_registers[1], 0, 0, 0);
                machine_a64_emit(&encoder, (to_unsigned ? 0x9e390000u : 0x9e380000u) | (from_f64 ? 0x00400000u : 0u) | (u32)operand_registers[0]);
            }
            break;
            case MACHINE_A64_LOAD_INCOMING:
                // The payload already carries the X29-relative byte offset,
                // sixteen bytes past the frame-pointer pair like the
                // canonical parameter capture.
                machine_a64_emit_generated_unsigned_memory(&encoder, operand_registers[0], MACHINE_A64_X29,
                                                            instruction->payload + (windows_frame ? windows_save_area - 16u : 0u), 8, false);
                break;
            case MACHINE_A64_VLOAD_FRAME:
            case MACHINE_A64_VSTORE_FRAME:
            {
                // The payload packs the fixed V register above the byte
                // offset into the slot.
                u32 vector_slot = machine_ref_payload(instruction->operands[0]);
                u32 vector_offset = machine_a64_frame_offset(
                    frame_area, placement->stack_slot_offsets[vector_slot] - (instruction->payload & 0x00ffffffu));
                machine_a64_emit_vector_frame_memory(&encoder, instruction->payload >> 24, vector_offset,
                                                     instruction->opcode == MACHINE_A64_VSTORE_FRAME);
            }
            break;
            case MACHINE_A64_VLOAD_FRAME_SIZED:
            case MACHINE_A64_VSTORE_FRAME_SIZED:
            {
                // The sized payload adds the transfer's size log2 above
                // the fixed V register.
                u32 sized_slot = machine_ref_payload(instruction->operands[0]);
                u32 sized_offset = machine_a64_frame_offset(
                    frame_area, placement->stack_slot_offsets[sized_slot] - (instruction->payload & 0x00ffffffu));
                machine_a64_emit_vector_frame_memory_sized(&encoder, (instruction->payload >> 24) & 0xfu, sized_offset, instruction->payload >> 28,
                                                           instruction->opcode == MACHINE_A64_VSTORE_FRAME_SIZED);
            }
            break;
            case MACHINE_A64_VARITH:
            {
                // NEON three-same v0 = v0 op v1, Q form; base words
                // verified against llvm-mc. Integer forms take the lane
                // size at bits 23:22; the float forms keep their fixed
                // size field and take the double bit at 22.
                u32 varith_lane_log2 = (instruction->payload >> 8) & 0x3u;
                u32 varith_operation = instruction->payload & 0xffu;
                u32 varith_double_bit = varith_lane_log2 == 3 ? 0x00400000u : 0;
                u32 varith_word = varith_operation == 0   ? 0x4e218400u | (varith_lane_log2 << 22)
                                  : varith_operation == 1 ? 0x6e218400u | (varith_lane_log2 << 22)
                                  : varith_operation == 2 ? 0x4e219c00u | (varith_lane_log2 << 22)
                                  : varith_operation == 3 ? 0x4e211c00u
                                  : varith_operation == 4 ? 0x4ea11c00u
                                  : varith_operation == 5 ? 0x6e211c00u
                                  : varith_operation == 6 ? 0x4e21d400u | varith_double_bit
                                  : varith_operation == 7 ? 0x4ea1d400u | varith_double_bit
                                  : varith_operation == 8 ? 0x6e21dc00u | varith_double_bit
                                                          : 0x6e21fc00u | varith_double_bit;
                machine_a64_emit(&encoder, varith_word);
            }
            break;
            case MACHINE_A64_ATOMIC_LOAD_PAIR:
            {
                u32 result_slot = machine_ref_payload(instruction->operands[1]);
                u32 result_offset = placement->stack_slot_offsets[result_slot];
                u32 atomic_address = operand_registers[0];
                // LDXP alone is not a single-copy atomic load on baseline
                // AArch64. Write the same pair back and retry until STXP
                // proves that the observed image was indivisible; SC uses
                // STLXP so the successful readback also has release order.
                machine_a64_emit(&encoder, ((instruction->payload & MACHINE_A64_ATOMIC_PAIR_ACQUIRE) ? 0xc87f8000u : 0xc87f0000u) |
                                               (MACHINE_A64_X14 << 10) | (atomic_address << 5) | MACHINE_A64_X9);
                machine_a64_emit(&encoder, ((instruction->payload & MACHINE_A64_ATOMIC_PAIR_RELEASE) ? 0xc8208000u : 0xc8200000u) |
                                               (MACHINE_A64_X13 << 16) | (MACHINE_A64_X14 << 10) | (atomic_address << 5) |
                                               MACHINE_A64_X9);
                machine_a64_emit(&encoder, 0x35000000u | (0x7fffeu << 5) | MACHINE_A64_X13);
                machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X9, machine_a64_frame_offset(frame_area, result_offset), 8, true);
                machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X14, machine_a64_frame_offset(frame_area, result_offset - 8), 8, true);
            }
            break;
            case MACHINE_A64_ATOMIC_STORE_PAIR:
            {
                u32 source_slot = machine_ref_payload(instruction->operands[1]);
                u32 source_offset = placement->stack_slot_offsets[source_slot];
                u32 atomic_address = operand_registers[0];
                u32 stored_size = instruction->payload & 0xffu;
                machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X11, machine_a64_frame_offset(frame_area, source_offset), 8, false);
                machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X12, machine_a64_frame_offset(frame_area, source_offset - 8), 8, false);
                if (stored_size < 16)
                {
                    machine_a64_emit_immediate(&encoder, MACHINE_A64_X13, ((u64)1 << ((stored_size - 8) * 8)) - 1);
                    machine_a64_emit(&encoder, 0x8a000000u | (MACHINE_A64_X13 << 16) | (MACHINE_A64_X12 << 5) | MACHINE_A64_X12);
                }
                // Arm the exclusive monitor with a discarded pair load,
                // then retry the staged replacement until it lands whole.
                machine_a64_emit(&encoder, ((instruction->payload & MACHINE_A64_ATOMIC_PAIR_ACQUIRE) ? 0xc87f8000u : 0xc87f0000u) |
                                               (MACHINE_A64_X14 << 10) | (atomic_address << 5) | MACHINE_A64_X9);
                machine_a64_emit(&encoder, ((instruction->payload & MACHINE_A64_ATOMIC_PAIR_RELEASE) ? 0xc8208000u : 0xc8200000u) |
                                               (MACHINE_A64_X13 << 16) | (MACHINE_A64_X12 << 10) | (atomic_address << 5) | MACHINE_A64_X11);
                machine_a64_emit(&encoder, 0x35000000u | (0x7fffeu << 5) | MACHINE_A64_X13);
            }
            break;
            case MACHINE_A64_ATOMIC_RMW_PAIR:
            case MACHINE_A64_ATOMIC_CAS_PAIR:
            {
                bool compare_exchange = instruction->opcode == MACHINE_A64_ATOMIC_CAS_PAIR;
                u32 result_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[1])];
                u32 input_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[2])];
                u32 atomic_address = operand_registers[0];
                u32 retry_offset = encoder.count;
                // Reload operands before LDXP, not inside the exclusive
                // window: ordinary memory accesses would forfeit the
                // architecture's forward-progress guarantee. Retrying also
                // restores X11/X12 after arithmetic or conditional selection.
                machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X11, machine_a64_frame_offset(frame_area, input_offset), 8, false);
                machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X12, machine_a64_frame_offset(frame_area, input_offset - 8), 8, false);
                if (compare_exchange)
                {
                    u32 desired_offset = placement->stack_slot_offsets[machine_ref_payload(instruction->operands[3])];
                    machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X15, machine_a64_frame_offset(frame_area, desired_offset), 8, false);
                    machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X17, machine_a64_frame_offset(frame_area, desired_offset - 8), 8, false);
                }
                machine_a64_emit(&encoder, ((instruction->payload & MACHINE_A64_ATOMIC_PAIR_ACQUIRE) ? UINT32_C(0xc87f8000)
                                                                                                  : UINT32_C(0xc87f0000)) |
                                               (MACHINE_A64_X14 << 10) | (atomic_address << 5) | MACHINE_A64_X9);
                if (compare_exchange)
                {
                    machine_a64_emit(&encoder, UINT32_C(0xeb0b013f)); // cmp x9, x11
                    machine_a64_emit(&encoder, UINT32_C(0xfa4c01c0)); // ccmp x14, x12, #0, eq
                    // A mismatch still needs a successful STXP to validate
                    // the pair read. Select the observed bits in that case.
                    machine_a64_emit(&encoder, UINT32_C(0x9a8901eb)); // csel x11, x15, x9, eq
                    machine_a64_emit(&encoder, UINT32_C(0x9a8e022c)); // csel x12, x17, x14, eq
                }
                else
                {
                    switch (instruction->payload >> MACHINE_A64_ATOMIC_PAIR_OPERATION_SHIFT)
                    {
                        case IR_ATOMIC_ADD:
                            machine_a64_emit(&encoder, UINT32_C(0xab0b012b)); // adds x11, x9, x11
                            machine_a64_emit(&encoder, UINT32_C(0x9a0c01cc)); // adc x12, x14, x12
                            break;
                        case IR_ATOMIC_SUBTRACT:
                            machine_a64_emit(&encoder, UINT32_C(0xeb0b012b)); // subs x11, x9, x11
                            machine_a64_emit(&encoder, UINT32_C(0xda0c01cc)); // sbc x12, x14, x12
                            break;
                        case IR_ATOMIC_BITWISE_AND:
                            machine_a64_emit(&encoder, UINT32_C(0x8a0b012b));
                            machine_a64_emit(&encoder, UINT32_C(0x8a0c01cc));
                            break;
                        case IR_ATOMIC_BITWISE_OR:
                            machine_a64_emit(&encoder, UINT32_C(0xaa0b012b));
                            machine_a64_emit(&encoder, UINT32_C(0xaa0c01cc));
                            break;
                        case IR_ATOMIC_BITWISE_XOR:
                            machine_a64_emit(&encoder, UINT32_C(0xca0b012b));
                            machine_a64_emit(&encoder, UINT32_C(0xca0c01cc));
                            break;
                        case IR_ATOMIC_EXCHANGE:
                            break;
                        default:
                            encoder.error = true;
                            break;
                    }
                }
                machine_a64_emit(&encoder, ((instruction->payload & MACHINE_A64_ATOMIC_PAIR_RELEASE) ? UINT32_C(0xc8208000)
                                                                                                  : UINT32_C(0xc8200000)) |
                                               (MACHINE_A64_X13 << 16) | (MACHINE_A64_X12 << 10) | (atomic_address << 5) | MACHINE_A64_X11);
                machine_a64_emit_mc(&encoder, (A64MCInst){
                    .operands = {{.value = MACHINE_A64_X13, .kind = A64_MC_OPERAND_REGISTER},
                                 {.value = (s64)retry_offset - (s64)encoder.count, .kind = A64_MC_OPERAND_PC_RELATIVE}},
                    .opcode = A64_OPCODE_CBNZ_W,
                    .operand_count = 2,
                });
                // No frame store occurs before the successful exclusive
                // store, and the returned old value includes both limbs.
                machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X9, machine_a64_frame_offset(frame_area, result_offset), 8, true);
                machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X14, machine_a64_frame_offset(frame_area, result_offset - 8), 8, true);
            }
            break;
            case MACHINE_A64_ATOMIC_LOAD:
            case MACHINE_A64_ATOMIC_STORE:
            {
                // One ldar/stlr-family word, the canonical
                // a64_emit_atomic_pointer encoding; the low payload byte
                // carries the access size.
                u32 atomic_size = instruction->payload & 0xffu;
                u32 atomic_size_bits = atomic_size == 2 ? 0x40000000u : atomic_size == 4 ? 0x80000000u : atomic_size == 8 ? 0xc0000000u : 0;
                bool atomic_is_store = instruction->opcode == MACHINE_A64_ATOMIC_STORE;
                u32 atomic_data = operand_registers[atomic_is_store ? 1 : 0];
                u32 atomic_address = operand_registers[atomic_is_store ? 0 : 1];
                machine_a64_emit(&encoder, (atomic_is_store ? 0x089ffc00u : 0x08dffc00u) | atomic_size_bits | (atomic_address << 5) | atomic_data);
            }
            break;
            case MACHINE_A64_ATOMIC_RMW:
            {
                // The canonical exclusive loop on its fixed palette:
                //   retry: ld(a)xr x9, [x10]
                //          <op>    x12, x9, x11
                //          st(l)xr w13, x12, [x10]
                //          cbnz    w13, retry
                // The operation words are the canonical emitter's, computing
                // x12 from x9 and x11 directly.
                u32 rmw_size = instruction->payload & 0xfu;
                u32 rmw_size_bits = rmw_size == 2 ? 0x40000000u : rmw_size == 4 ? 0x80000000u : rmw_size == 8 ? 0xc0000000u : 0;
                bool rmw_wide = rmw_size == 8;
                u32 rmw_operation = (instruction->payload >> 8) & 0xffu;
                machine_a64_emit(&encoder, ((instruction->payload & 0x10u) ? 0x085ffc00u : 0x085f7c00u) | rmw_size_bits | (MACHINE_A64_X10 << 5) |
                                               MACHINE_A64_X9);
                u32 rmw_word = rmw_operation == IR_ATOMIC_ADD           ? (rmw_wide ? 0x8b0b012cu : 0x0b0b012cu)
                               : rmw_operation == IR_ATOMIC_SUBTRACT    ? (rmw_wide ? 0xcb0b012cu : 0x4b0b012cu)
                               : rmw_operation == IR_ATOMIC_BITWISE_AND ? (rmw_wide ? 0x8a0b012cu : 0x0a0b012cu)
                               : rmw_operation == IR_ATOMIC_BITWISE_OR  ? (rmw_wide ? 0xaa0b012cu : 0x2a0b012cu)
                               : rmw_operation == IR_ATOMIC_BITWISE_XOR ? (rmw_wide ? 0xca0b012cu : 0x4a0b012cu)
                                                                        : (rmw_wide ? 0xaa0b03ecu : 0x2a0b03ecu);
                machine_a64_emit(&encoder, rmw_word);
                machine_a64_emit(&encoder, ((instruction->payload & 0x20u) ? 0x0800fc00u : 0x08007c00u) | rmw_size_bits | (MACHINE_A64_X13 << 16) |
                                               (MACHINE_A64_X10 << 5) | MACHINE_A64_X12);
                machine_a64_emit(&encoder, 0x35000000u | (0x7fffdu << 5) | MACHINE_A64_X13);
            }
            break;
            case MACHINE_A64_ATOMIC_CAS:
            {
                // The canonical compare-exchange loop:
                //   retry: ld(a)xr x9, [x10]
                //          cmp     x9, x12
                //          b.ne    done
                //          st(l)xr w13, x11, [x10]
                //          cbnz    w13, retry
                //   done:  clrex
                u32 cas_size = instruction->payload & 0xfu;
                u32 cas_size_bits = cas_size == 2 ? 0x40000000u : cas_size == 4 ? 0x80000000u : cas_size == 8 ? 0xc0000000u : 0;
                machine_a64_emit(&encoder, ((instruction->payload & 0x10u) ? 0x085ffc00u : 0x085f7c00u) | cas_size_bits | (MACHINE_A64_X10 << 5) |
                                               MACHINE_A64_X9);
                machine_a64_emit(&encoder, cas_size == 8 ? 0xeb0c013fu : 0x6b0c013fu);
                machine_a64_emit(&encoder, 0x54000061u);
                machine_a64_emit(&encoder, ((instruction->payload & 0x20u) ? 0x0800fc00u : 0x08007c00u) | cas_size_bits | (MACHINE_A64_X13 << 16) |
                                               (MACHINE_A64_X10 << 5) | MACHINE_A64_X11);
                machine_a64_emit(&encoder, 0x35000000u | (0x7fffcu << 5) | MACHINE_A64_X13);
                machine_a64_emit(&encoder, 0xd5033f5fu);
            }
            break;
            case MACHINE_A64_CLEAR_INSTRUCTION_CACHE:
                // Cache lines are at least four bytes. Round the initial
                // address down so a short unaligned range cannot miss its
                // final line. X11 keeps that start for the second walk.
                // Both loops compare unsigned addresses against exclusive X10.
                machine_a64_emit(&encoder, 0x927ef52bu); // and x11, x9, #-4
                machine_a64_emit(&encoder, 0xaa0b03e9u); // mov x9, x11
                machine_a64_emit(&encoder, 0xeb0a013fu); // cmp x9, x10
                machine_a64_emit(&encoder, 0x54000082u); // b.hs data_done
                machine_a64_emit(&encoder, 0xd50b7b29u); // dc cvau, x9
                machine_a64_emit(&encoder, 0x91001129u); // add x9, x9, #4
                machine_a64_emit(&encoder, 0x17fffffcu); // b data_loop
                machine_a64_emit(&encoder, 0xd5033b9fu); // dsb ish
                machine_a64_emit(&encoder, 0xaa0b03e9u); // mov x9, x11
                machine_a64_emit(&encoder, 0xeb0a013fu); // cmp x9, x10
                machine_a64_emit(&encoder, 0x54000082u); // b.hs instruction_done
                machine_a64_emit(&encoder, 0xd50b7529u); // ic ivau, x9
                machine_a64_emit(&encoder, 0x91001129u); // add x9, x9, #4
                machine_a64_emit(&encoder, 0x17fffffcu); // b instruction_loop
                machine_a64_emit(&encoder, 0xd5033b9fu); // dsb ish
                machine_a64_emit(&encoder, 0xd5033fdfu); // isb
                break;
            case MACHINE_A64_COMPILER_BARRIER:
                break;
            case MACHINE_A64_ATOMIC_FENCE:
                // dmb ish, the canonical thread-fence word.
                machine_a64_emit(&encoder, 0xd5033bbfu);
                break;
            case MACHINE_A64_SWITCH:
            {
                // The canonical compare chain: each case constant
                // materializes in reserved X17 (never allocatable, so the
                // condition cannot live there), equality branches to the
                // case block through the ordinary fixup machinery, and
                // the tail branch takes the default edge.
                u32 switch_condition = operand_registers[0];
                for (u32 case_index = 0; case_index < instruction->flags; case_index += 1)
                {
                    MachineSwitchCase* case_row = function->switch_cases + instruction->payload + case_index;
                    machine_a64_emit_immediate(&encoder, MACHINE_A64_X17, case_row->value);
                    // subs wzr/xzr, <condition>, w17/x17 -- the 32-bit form
                    // when the switched type is 32 bits or narrower, so the
                    // comparison ignores whatever the value's producer left
                    // above the type's own width.
                    u32 compare_word = (case_row->compare_width ? case_row->compare_width : 64) == 32 ? 0x6b11001fu : 0xeb11001fu;
                    machine_a64_emit(&encoder, compare_word | (switch_condition << 5));
                    MachineA64BranchFixup* case_fixup = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                    *case_fixup = (MachineA64BranchFixup){
                        .patch_offset = encoder.count,
                        .block = case_row->target_block,
                        .opcode = A64_OPCODE_B_COND,
                        .condition = 0,
                    };
                    machine_a64_emit_mc(&encoder, (A64MCInst){
                                                       .operands = {
                                                           {.kind = A64_MC_OPERAND_PC_RELATIVE},
                                                           {.value = 0, .kind = A64_MC_OPERAND_IMMEDIATE},
                                                       },
                                                       .opcode = A64_OPCODE_B_COND,
                                                       .operand_count = 2,
                                                   });
                }
                MachineA64BranchFixup* default_fixup = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                *default_fixup = (MachineA64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[1]),
                    .opcode = A64_OPCODE_B,
                    .condition = 0xff,
                };
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {{.kind = A64_MC_OPERAND_PC_RELATIVE}},
                                                   .opcode = A64_OPCODE_B,
                                                   .operand_count = 1,
                                               });
            }
            break;
            case MACHINE_A64_STACK_ALLOCATE:
            {
                // The canonical page-probed loop: X9 holds the entire
                // distance to the aligned target, including any padding
                // beyond the sixteen-aligned incoming stack pointer.
                u64 alloc_mask = (u64)instruction->payload - 1;
                if (instruction->payload > 16)
                {
                    machine_a64_emit(&encoder, 0xcb2963e9u); // sub x9, sp, x9
                }
                else
                {
                    machine_a64_emit_immediate(&encoder, MACHINE_A64_X10, alloc_mask);
                    machine_a64_emit(&encoder, 0x8b0a0129u);
                }
                machine_a64_emit_immediate(&encoder, MACHINE_A64_X10, ~alloc_mask);
                machine_a64_emit(&encoder, 0x8a0a0129u);
                if (instruction->payload > 16)
                {
                    machine_a64_emit(&encoder, 0xcb2963e9u); // sub x9, sp, x9
                }
                machine_a64_emit(&encoder, 0xf140053fu);
                machine_a64_emit(&encoder, 0x540000a3u);
                machine_a64_emit(&encoder, 0xd14007ffu);
                machine_a64_emit(&encoder, 0xf94003ffu);
                machine_a64_emit(&encoder, 0xd1400529u);
                machine_a64_emit(&encoder, 0x17fffffbu);
                machine_a64_emit(&encoder, 0xcb2963ffu);
                machine_a64_emit(&encoder, 0xf94003ffu);
                machine_a64_emit(&encoder, 0x910003eau);
            }
            break;
            case MACHINE_A64_VA_SAVE:
            {
                // The canonical prologue's X0-X7/Q0-Q7 snapshot, addressed like
                // any other frame slot: byte k of the slot lives at the
                // shared grows-down convention's offset minus k.
                u32 va_save_slot = machine_ref_payload(instruction->operands[0]);
                for (u32 register_index = 0; register_index < 8; register_index += 1)
                {
                    u32 save_offset = machine_a64_frame_offset(frame_area, placement->stack_slot_offsets[va_save_slot] - register_index * 8u);
                    machine_a64_emit_frame_memory(&encoder, register_index, save_offset, 8, true);
                    u32 float_offset = machine_a64_frame_offset(frame_area, placement->stack_slot_offsets[va_save_slot] - MACHINE_A64_VA_GP_SAVE_BYTES - register_index * 16u);
                    machine_a64_emit_vector_frame_memory(&encoder, register_index, float_offset, true);
                }
            }
            break;
            case MACHINE_A64_VA_ARG:
            {
                MachineVaArg* metadata = function->va_args + instruction->payload;
                u32 list = operand_registers[0];
                bool floating = metadata->parts[0].is_float != 0;
                u32 cursor_offset = floating ? MACHINE_A64_VA_VR_OFFS_OFFSET : MACHINE_A64_VA_GR_OFFS_OFFSET;
                u32 increment = metadata->part_count * (floating ? 16u : 8u);
                // Only touch this file's four-byte cursor: an eight-byte store
                // would also overwrite the independently live FP cursor.
                machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X11, list, cursor_offset, 4, false);
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_SXTW, MACHINE_A64_X11, MACHINE_A64_X11, 0, 0);
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_CMP_ZERO, MACHINE_A64_X11, 0, 0, 0);
                u32 empty_patch = encoder.count;
                machine_a64_emit_mc(&encoder, (A64MCInst){
                    .operands = {{.kind = A64_MC_OPERAND_PC_RELATIVE},
                                 {.value = 10, .kind = A64_MC_OPERAND_IMMEDIATE}}, // GE: already exhausted
                    .opcode = A64_OPCODE_B_COND,
                    .operand_count = 2,
                });
                if (!floating && metadata->alignment > 8)
                {
                    u32 align_fields[] = {MACHINE_A64_X11, MACHINE_A64_X11, 15};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, align_fields,
                                                    BUSTER_ARRAY_LENGTH(align_fields));
                    machine_a64_emit_immediate(&encoder, MACHINE_A64_X9, ~UINT64_C(15));
                    machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_AND64, MACHINE_A64_X11, MACHINE_A64_X11, MACHINE_A64_X9, 0);
                }
                u32 increment_fields[] = {MACHINE_A64_X9, MACHINE_A64_X11, increment};
                machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, increment_fields,
                                                BUSTER_ARRAY_LENGTH(increment_fields));
                machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X9, list, cursor_offset, 4, true);
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_CMP_ZERO, MACHINE_A64_X9, 0, 0, 0);
                u32 overflow_patch = encoder.count;
                machine_a64_emit_mc(&encoder, (A64MCInst){
                    .operands = {{.kind = A64_MC_OPERAND_PC_RELATIVE},
                                 {.value = 12, .kind = A64_MC_OPERAND_IMMEDIATE}}, // GT: this value does not fit
                    .opcode = A64_OPCODE_B_COND,
                    .operand_count = 2,
                });
                machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X12, list,
                    floating ? MACHINE_A64_VA_VR_TOP_OFFSET : MACHINE_A64_VA_GR_TOP_OFFSET, 8, false);
                machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_ADD64, MACHINE_A64_X12, MACHINE_A64_X12, MACHINE_A64_X11, 0);
                for (u32 part_index = 0; part_index < metadata->part_count; part_index += 1)
                {
                    if (metadata->result_is_frame)
                    {
                        machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X9, MACHINE_A64_X12, metadata->parts[part_index].save_offset, metadata->parts[part_index].size, false);
                        u32 part_offset = machine_a64_frame_offset(
                            frame_area, placement->stack_slot_offsets[metadata->result_slot] - metadata->parts[part_index].value_offset);
                        machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X9, part_offset, metadata->parts[part_index].size, true);
                    }
                    else
                    {
                        machine_a64_emit_pointer_memory(&encoder, operand_registers[1], MACHINE_A64_X12, 0, 8, false);
                    }
                }
                u32 end_patch = encoder.count;
                machine_a64_emit_mc(&encoder, (A64MCInst){
                    .operands = {{.kind = A64_MC_OPERAND_PC_RELATIVE}},
                    .opcode = A64_OPCODE_B,
                    .operand_count = 1,
                });
                u32 overflow_offset = encoder.count;
                machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X12, list, MACHINE_A64_VA_STACK_OFFSET, 8, false);
                if (metadata->alignment > 8)
                {
                    u32 align_fields[] = {MACHINE_A64_X12, MACHINE_A64_X12, 15};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, align_fields,
                                                    BUSTER_ARRAY_LENGTH(align_fields));
                    machine_a64_emit_immediate(&encoder, MACHINE_A64_X9, ~UINT64_C(15));
                    machine_a64_emit_generated_opcode(&encoder, MACHINE_A64_AND64, MACHINE_A64_X12, MACHINE_A64_X12, MACHINE_A64_X9, 0);
                }
                for (u32 part_index = 0; part_index < metadata->part_count; part_index += 1)
                {
                    if (metadata->result_is_frame)
                    {
                        machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X9, MACHINE_A64_X12, metadata->parts[part_index].value_offset, metadata->parts[part_index].size, false);
                        u32 part_offset = machine_a64_frame_offset(
                            frame_area, placement->stack_slot_offsets[metadata->result_slot] - metadata->parts[part_index].value_offset);
                        machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X9, part_offset, metadata->parts[part_index].size, true);
                    }
                    else
                    {
                        machine_a64_emit_pointer_memory(&encoder, operand_registers[1], MACHINE_A64_X12, 0, 8, false);
                    }
                }
                u32 overflow_fields[] = {MACHINE_A64_X12, MACHINE_A64_X12, metadata->stack_size};
                machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, overflow_fields,
                                                BUSTER_ARRAY_LENGTH(overflow_fields));
                machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X12, list, MACHINE_A64_VA_STACK_OFFSET, 8, true);
                u32 end_offset = encoder.count;
                if (!encoder.overflow && !encoder.error)
                {
                    u32 patch_offsets[] = {empty_patch, overflow_patch, end_patch};
                    u32 target_offsets[] = {overflow_offset, overflow_offset, end_offset};
                    for (u32 patch_index = 0; patch_index < BUSTER_ARRAY_LENGTH(patch_offsets); patch_index += 1)
                    {
                        u32 word;
                        memcpy(&word, encoder.bytes + patch_offsets[patch_index], sizeof(word));
                        u32 patched;
                        if (a64_pc_relative_patch(patch_index < 2 ? A64_OPCODE_B_COND : A64_OPCODE_B, word,
                            (s64)target_offsets[patch_index] - patch_offsets[patch_index], &patched))
                        {
                            memcpy(encoder.bytes + patch_offsets[patch_index], &patched, sizeof(patched));
                        }
                        else { encoder.error = true; }
                    }
                }
            }
            break;
            case MACHINE_A64_B:
            {
                MachineA64BranchFixup* fixup = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                *fixup = (MachineA64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[0]),
                    .opcode = A64_OPCODE_B,
                    .condition = 0xff,
                };
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {{.kind = A64_MC_OPERAND_PC_RELATIVE}},
                                                   .opcode = A64_OPCODE_B,
                                                   .operand_count = 1,
                                               });
            }
            break;
            case MACHINE_A64_LEA_BLOCK:
            {
                u32 address_offset = encoder.count;
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                       .operands = {
                                                           {.value = operand_registers[0], .kind = A64_MC_OPERAND_REGISTER},
                                                           {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE},
                                                       },
                                                       .opcode = A64_OPCODE_ADR,
                                                       .operand_count = 2,
                                                   });
                machine_a64_emit(&encoder, UINT32_C(0xd2800000) | MACHINE_A64_X17);
                machine_a64_emit(&encoder, UINT32_C(0xf2800000) | (UINT32_C(1) << 21) | MACHINE_A64_X17);
                machine_a64_emit(&encoder, UINT32_C(0xf2800000) | (UINT32_C(2) << 21) | MACHINE_A64_X17);
                machine_a64_emit(&encoder, UINT32_C(0xf2800000) | (UINT32_C(3) << 21) | MACHINE_A64_X17);
                u32 fields[] = {operand_registers[0], operand_registers[0], 0, MACHINE_A64_X17};
                machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRS, fields, BUSTER_ARRAY_LENGTH(fields));
                MachineA64BranchFixup* fixup = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                *fixup = (MachineA64BranchFixup){
                    .patch_offset = address_offset,
                    .block = instruction->payload,
                    .opcode = A64_OPCODE_INVALID,
                    .condition = 0xff,
                    .label_address = true,
                };
            }
            break;
            case MACHINE_A64_INDIRECT_BRANCH:
                machine_a64_emit_mc(&encoder, (A64MCInst){
                                                   .operands = {{.value = operand_registers[0], .kind = A64_MC_OPERAND_REGISTER}},
                                                   .opcode = A64_OPCODE_BR,
                                                   .operand_count = 1,
                                               });
                break;
            case MACHINE_A64_BCC:
            {
                MachineA64BranchFixup* taken = (MachineA64BranchFixup*)machine_stream_append(arena, &fixups);
                *taken = (MachineA64BranchFixup){
                    .patch_offset = encoder.count,
                    .block = machine_ref_payload(instruction->operands[0]),
                    .opcode = A64_OPCODE_B_COND,
                    .condition = instruction->payload & 15,
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
                    .condition = 0xff,
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
                if (windows_frame)
                {
                    u32 frame_fields[] = {MACHINE_A64_SP, MACHINE_A64_X29, 0};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, frame_fields, BUSTER_ARRAY_LENGTH(frame_fields));
                    u32 slot = 16u + 8u * push_count;
                    machine_a64_emit_generated_unsigned_memory(&encoder, MACHINE_A64_X28, MACHINE_A64_SP, slot, 8, false);
                    for (u32 saved_register = MACHINE_A64_REGISTER_COUNT; saved_register > 0; saved_register -= 1)
                    {
                        if ((placement->callee_saved_mask >> (saved_register - 1u)) & 1u)
                        {
                            slot -= 8;
                            machine_a64_emit_generated_unsigned_memory(&encoder, saved_register - 1u, MACHINE_A64_SP, slot, 8, false);
                        }
                    }
                    u32 chain_fields[] = {MACHINE_A64_X29, MACHINE_A64_SP, MACHINE_A64_X30, windows_save_area / 8u};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_LDPXPOST, chain_fields, BUSTER_ARRAY_LENGTH(chain_fields));
                }
                else
                {
                    {
                        u32 fields[] = {MACHINE_A64_SP, MACHINE_A64_X28, 0};
                        machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, fields, BUSTER_ARRAY_LENGTH(fields));
                    }
                    u32 restore_base = MACHINE_A64_SP;
                    if (large_save_offset)
                    {
                        machine_a64_emit_immediate(&encoder, MACHINE_A64_X16, 16u + 8u * push_count);
                        u32 fields[] = {MACHINE_A64_X16, MACHINE_A64_X29, 0, MACHINE_A64_X16};
                        machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_SUBXRS, fields, BUSTER_ARRAY_LENGTH(fields));
                        restore_base = MACHINE_A64_X16;
                    }
                    u32 restore_slot = 0;
                    for (u32 saved_register = 0; saved_register < MACHINE_A64_REGISTER_COUNT; saved_register += 1)
                    {
                        if (!((placement->callee_saved_mask >> saved_register) & 1u))
                        {
                            continue;
                        }
                        restore_slot += 1;
                        machine_a64_emit_generated_unsigned_memory(&encoder, saved_register, restore_base,
                                                                    large_save_offset ? 8u * (push_count - restore_slot) : frame_area - 8u * restore_slot, 8, false);
                    }
                    machine_a64_emit_generated_unsigned_memory(&encoder, MACHINE_A64_X28, restore_base,
                                                                large_save_offset ? 8u * push_count : frame_area, 8, false);
                    u32 release_remaining = frame_total;
                    while (release_remaining)
                    {
                        u32 release_chunk = BUSTER_MIN(release_remaining, A64_SP_ADJUST_CHUNK);
                        u32 fields[] = {MACHINE_A64_SP, MACHINE_A64_SP, release_chunk};
                        machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, fields, BUSTER_ARRAY_LENGTH(fields));
                        release_remaining -= release_chunk;
                    }
                    machine_a64_emit(&encoder, 0xa8c17bfd);
                }
                machine_a64_emit_generated_opcode(&encoder, instruction->opcode, operand_registers[0], operand_registers[1], operand_registers[2],
                                                  instruction->payload);
            }
            break;
            case MACHINE_A64_BRK:
            case MACHINE_A64_UDF:
                machine_a64_emit(&encoder, 0xd4200000);
                break;
            case MACHINE_A64_READ_SP:
                machine_a64_emit_generated_opcode(&encoder, instruction->opcode, operand_registers[0], operand_registers[1], operand_registers[2],
                                                  instruction->payload);
                break;
            case MACHINE_A64_WRITE_SP:
                machine_a64_emit_generated_opcode(&encoder, instruction->opcode, operand_registers[0], operand_registers[1], operand_registers[2],
                                                  instruction->payload);
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
                bool page = function->call_target_references &&
                            function->call_target_references[instruction->payload] == MACHINE_SYMBOL_REFERENCE_MACH_PAGE;
                if (page)
                {
                    MachineCallSite* high = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                    *high = (MachineCallSite){.code_offset = encoder.count, .target = instruction->payload, .page_relative = 1};
                    machine_a64_emit_mc(&encoder, (A64MCInst){
                        .operands = {{.value = operand_registers[0], .kind = A64_MC_OPERAND_REGISTER},
                                     {.kind = A64_MC_OPERAND_PC_RELATIVE}},
                        .opcode = A64_OPCODE_ADRP,
                        .operand_count = 2,
                    });
                    MachineCallSite* low = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                    *low = (MachineCallSite){.code_offset = encoder.count, .target = instruction->payload, .page_relative = 1, .page_low = 1};
                    u32 fields[] = {operand_registers[0], operand_registers[0], 0};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, fields, BUSTER_ARRAY_LENGTH(fields));
                }
                else
                {
                    // The other targets retain their canonical inline literal.
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
            }
            break;
            case MACHINE_A64_TLS_WINDOWS:
            case MACHINE_A64_TLS_DARWIN:
            {
                bool windows = instruction->opcode == MACHINE_A64_TLS_WINDOWS;
                u32 destination = windows ? MACHINE_A64_X9 : MACHINE_A64_X0;
                MachineThreadLocalSite site_kind = windows ? MACHINE_THREAD_LOCAL_SITE_WINDOWS_INDEX : MACHINE_THREAD_LOCAL_SITE_DARWIN_DESCRIPTOR;
                MachineCallSite* high = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                *high = (MachineCallSite){.code_offset = encoder.count, .target = instruction->payload,
                    .is_thread_local = 1, .thread_local_site = (u32)site_kind};
                machine_a64_emit_mc(&encoder, (A64MCInst){
                    .operands = {{.value = destination, .kind = A64_MC_OPERAND_REGISTER},
                                 {.value = 0, .kind = A64_MC_OPERAND_PC_RELATIVE}},
                    .opcode = A64_OPCODE_ADRP, .operand_count = 2});
                MachineCallSite* low = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                *low = (MachineCallSite){.code_offset = encoder.count, .target = instruction->payload,
                    .is_thread_local = 1, .thread_local_low = 1, .thread_local_site = (u32)site_kind};
                machine_a64_emit_generated_unsigned_memory(&encoder, destination, destination, 0, windows ? 4u : 8u, false);
                if (windows)
                {
                    // X18 is the Windows TEB; its TLS array pointer is at 0x58.
                    machine_a64_emit_generated_unsigned_memory(&encoder, MACHINE_A64_X10, 18, 0x58, 8, false);
                    u32 index_fields[] = {destination, MACHINE_A64_X10, 3, destination};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRS, index_fields, BUSTER_ARRAY_LENGTH(index_fields));
                    machine_a64_emit_generated_unsigned_memory(&encoder, destination, destination, 0, 8, false);
                    MachineCallSite* offset_site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                    *offset_site = (MachineCallSite){.code_offset = encoder.count, .target = instruction->payload,
                        .is_thread_local = 1, .thread_local_site = MACHINE_THREAD_LOCAL_SITE_WINDOWS_OFFSET};
                    u32 offset_fields[] = {destination, destination, 0};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, offset_fields, BUSTER_ARRAY_LENGTH(offset_fields));
                }
                else
                {
                    machine_a64_emit_generated_unsigned_memory(&encoder, MACHINE_A64_X8, destination, 0, 8, false);
                    machine_a64_emit_mc(&encoder, (A64MCInst){
                        .operands = {{.value = MACHINE_A64_X8, .kind = A64_MC_OPERAND_REGISTER}},
                        .opcode = A64_OPCODE_BLR, .operand_count = 1});
                }
            }
            break;
            case MACHINE_A64_LEA_TLS:
            {
                // The canonical ELF local-exec sequence with the destination
                // free: mrs tpidr_el0, then the TPREL_HI12/LO12 add pair,
                // each add carrying its own thread-local call site.
                u32 tls_destination = operand_registers[0];
                machine_a64_emit(&encoder, 0xd53bd040u | tls_destination);
                MachineCallSite* tls_high_site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                *tls_high_site = (MachineCallSite){
                    .code_offset = encoder.count,
                    .target = instruction->payload,
                    .is_thread_local = 1,
                };
                machine_a64_emit(&encoder, 0x91400000u | (tls_destination << 5) | tls_destination);
                MachineCallSite* tls_low_site = (MachineCallSite*)machine_stream_append(arena, &call_sites);
                *tls_low_site = (MachineCallSite){
                    .code_offset = encoder.count,
                    .target = instruction->payload,
                    .is_thread_local = 1,
                    .thread_local_low = 1,
                };
                machine_a64_emit(&encoder, 0x91000000u | (tls_destination << 5) | tls_destination);
            }
            break;
            default:
                return result;
            }
            MachinePoint after = machine_point_make(instruction_index, MACHINE_POINT_AFTER);
            while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == after)
            {
                MachineEdit* edit = placement->edits + edit_cursor;
                if (edit->kind == MACHINE_EDIT_RELOAD || edit->kind == MACHINE_EDIT_TEMP_RELOAD)
                {
                    u32 edit_frame_offset = edit->kind == MACHINE_EDIT_TEMP_RELOAD ? placement->edge_copy_temporary_offset + edit->subject
                                                                                  : placement->virtual_register_offsets[edit->subject];
                    machine_a64_emit_frame_load(&encoder, edit->location, machine_a64_frame_offset(frame_area, edit_frame_offset));
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
                    u32 edit_frame_offset = edit->kind == MACHINE_EDIT_TEMP_SPILL ? placement->edge_copy_temporary_offset + edit->subject
                                                                                 : placement->virtual_register_offsets[edit->subject];
                    machine_a64_emit_frame_store(&encoder, edit->location, machine_a64_frame_offset(frame_area, edit_frame_offset));
                }
                edit_cursor += 1;
            }
        }
    }
    if (!encoder.overflow && !encoder.error)
    {
        if (!machine_a64_relax_branches(&encoder, result.block_offsets, function->block_count, result.row_offsets, function->instruction_count, &fixups,
                                        &call_sites, &epilogs))
        {
            return result;
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
    }

    return result;
}
