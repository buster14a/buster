// AArch64 machine selection and encoding. Included by machine.c in the
// backend-implementation-file pattern; not a standalone translation unit.
// The subset covers scalar integer functions — arguments, constants,
// casts, unary and binary arithmetic including divides and shifts,
// comparisons, direct locals and pointer dereference, member addresses,
// branches, and scalar returns — plus the AAPCS64 signature shapes, calls,
// symbol addresses, aggregate/array literal construction into frame slots,
// scalar float bodies (arithmetic, comparison, negation, and conversions
// over the bit-image model, riding V0/V1 internally), scalar stack
// arguments through a fixed outgoing area at the frame bottom, non-Darwin
// variadic definitions over the canonical four-word va_list model
// (Darwin's anonymous-arguments-on-stack convention stays canonical),
// sixteen-byte short-vector arguments and results as slot-backed values
// touching the V file only at the ABI edges, indirect arguments — any
// aggregate or vector past sixteen bytes — behind a caller-side defensive
// copy, over-aligned locals as runtime-aligned pointers into padded raw
// slots, and rvalue array/vector INDEX bases through their storage
// snapshot. Everything else is an explicit unsupported result, never a
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
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/generated/aarch64-form-ids.generated.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

// Supported argument-list length: the register files plus the scalar
// stack tail the subset stages, matching the x86-64 selector's cap.
#define MACHINE_A64_MAX_ARGUMENTS 24

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
// caller points at a defensive copy. Aggregate/HFA stack arguments stay
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
    u8 reserved[1];
};

// One argument's placement: its shape's parts in consecutive per-class
// registers, integer parts from X0 and float parts from V0 — or, for a
// scalar past its register file, one eight-byte stack part at the
// canonical caller's sequential offsets. Aggregate and HFA stack
// arguments stay outside the subset; placement fails instead.
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

typedef struct MachineA64Selector MachineA64Selector;
struct MachineA64Selector
{
    Arena* arena;
    IrProgram* program;
    IrFunction* function;
    MachineFunctionBuilder builder;
    MachineSelectionCounters selection_counters;
    Target target;
    MachineBuilderStream immediates;
    MachineBuilderStream stack_slots;
    MachineBuilderStream stack_slot_alignments;
    MachineBuilderStream call_targets;
    MachineBuilderStream va_args;
    MachineBuilderStream switch_cases;
    // Per IrValue: virtual register index, stack slot index, or UINT32_MAX.
    u32* value_virtual_registers;
    u32* value_stack_slots;
    // Per IrValue: the padded raw slot behind an over-aligned local whose
    // virtual register holds a runtime-aligned pointer, or UINT32_MAX.
    u32* value_indirect_slots;
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
    if (!type || !type->layout.resolved ||
        (type->kind != IR_TYPE_STRUCT && type->kind != IR_TYPE_UNION && type->kind != IR_TYPE_SLICE && type->kind != IR_TYPE_VECTOR))
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
        // A short vector rides one whole V register; the value itself is
        // slot-backed and only the ABI edges touch the vector file. The
        // eight-byte short-vector form stays canonical until the corpus
        // grows one.
        if (abi.parts[0].size != 16)
        {
            return false;
        }
        *shape = (MachineA64ValueShape){
            .part_is_float = {1},
            .part_sizes = {16},
            .part_count = 1,
            .byte_size = 16,
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
// X argument register, float parts the next V register. Whatever no
// longer fits takes sequential eight-byte stack parts — a scalar or an
// indirect argument's pointer as one part, a register aggregate, HFA, or
// vector as its eightbyte images. The exact simulation the canonical
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
    if (definition->opcode == IR_OPCODE_LOCAL && selector->value_virtual_registers[base.value] != UINT32_MAX &&
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
    IrType* value_type = ir_type_from_id(&selector->program->types, value->canonical_type);
    bool storage_value = definition->opcode == IR_OPCODE_LOCAL ||
                         (value->category == IR_VALUE_VALUE && value_type &&
                          (value_type->kind == IR_TYPE_ARRAY || value_type->kind == IR_TYPE_VECTOR));
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
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_IMMEDIATE, immediate_index)},
                                                       .opcode = MACHINE_A64_MOV_RI,
                                                   });
        machine_a64_define(selector, result_register, row);
        selected = true;
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

BUSTER_GLOBAL_LOCAL bool machine_a64_select_cast(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 source_register;
    if (result_register != UINT32_MAX && machine_a64_operand_register(selector, instruction->operands[0], &source_register))
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
// add/sub/mul/div); divisions, remainders, shifts, and comparisons keep
// the canonical per-lane path.
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
            // No 64-bit-lane NEON multiply exists; those functions keep
            // the canonical path whole.
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
    }
    // The operand lookups bound the value ids, which is what makes the type
    // read below safe; nothing here may be hoisted above them.
    else if (result_register != UINT32_MAX && machine_a64_operand_register(selector, instruction->operands[0], &left_register) &&
        machine_a64_operand_register(selector, instruction->operands[1], &right_register))
    {
        IrTypeId operand_type_id = function->values[instruction->operands[0].value].canonical_type;
        IrType* operand_type = ir_type_from_id(&program->types, operand_type_id);
        if (machine_a64_type_is_scalar_register(operand_type))
        {
            // `wide` folds every width above 32 into the 64-bit rows, which
            // would wrap a 128-bit shift's amount modulo 64 and drop every
            // other operation's high half. The scalar-register guard above
            // (size <= 8) is what keeps 128-bit operands off this table, and
            // no 128-bit producer (argument, load, cast, constant, call)
            // selects either, so such functions fall back whole to the
            // canonical emitter, which owns the 128-bit pair lowerings.
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
                u32 condition = machine_a64_condition_from_comparison(instruction->binary_operation);
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
            switch (instruction->binary_operation)
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
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    if (result_register != UINT32_MAX && instruction->immediate_count && instruction->immediates)
    {
        IrType* aggregate = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
        u64 field_index = instruction->immediates[0];
        if (aggregate && field_index < aggregate->field_count && aggregate->fields[field_index].offset <= INT32_MAX)
        {
            selected = machine_a64_select_place_address_offset(selector, instruction->operands[0], result_register,
                                                               (u32)aggregate->fields[field_index].offset);
        }
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_a64_select_global_address(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;

    bool selected = false;
    IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
    bool thread_local_global = symbol && instruction->opcode == IR_OPCODE_GLOBAL && symbol->is_thread_local;
    // ELF local-exec thread locals resolve through tpidr_el0 plus the
    // TPREL add pair; Darwin's tlv-call model stays canonical.
    bool thread_local_supported =
        selector->target.os == OPERATING_SYSTEM_LINUX || selector->target.os == OPERATING_SYSTEM_ANDROID;
    if (result_register != UINT32_MAX && symbol && thread_local_global && thread_local_supported)
    {
        u32 target_index = selector->call_targets.total_count;
        IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
        *target_row = instruction->symbol;
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register)},
                                                       .payload = target_index,
                                                       .opcode = MACHINE_A64_LEA_TLS,
                                                   });
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
            u32 target_index = selector->call_targets.total_count;
            IrSymbolId* target_row = (IrSymbolId*)machine_stream_append(selector->arena, &selector->call_targets);
            *target_row = instruction->symbol;
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
    IrProgram* program = selector->program;
    IrFunction* function = selector->function;

    bool selected = false;
    u32 index_register;
    if (result_register != UINT32_MAX && instruction->operand_count >= 2 &&
        machine_a64_operand_register(selector, instruction->operands[1], &index_register))
    {
        IrType* index_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
        IrType* element = ir_type_from_id(&program->types, instruction->canonical_type);
        bool typed = index_type && index_type->kind == IR_TYPE_INTEGER && element && element->layout.resolved && element->layout.size <= INT32_MAX;
        // The scaled index is computed in a synthesized temporary; signed
        // narrow indexes sign-extend before scaling.  Resolving the extend
        // before the base address keeps an unsupported index width from
        // emitting rows the rejection would throw away anyway.
        u16 extend_opcode = MACHINE_A64_MOV_RR;
        if (typed && index_type->is_signed && index_type->bit_width < 64)
        {
            extend_opcode = (u16)(index_type->bit_width == 8    ? MACHINE_A64_SXTB
                                  : index_type->bit_width == 16 ? MACHINE_A64_SXTH
                                  : index_type->bit_width == 32 ? MACHINE_A64_SXTW
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

// The canonical VA_START simulation, verbatim: every named parameter's
// integer-aggregate parts count against the one eight-register file —
// float scalars and HFAs included, unlike the machine placement, whose
// V-file assignment the canonical va_list model deliberately ignores.
// Both this selector's callers and the canonical emitter run the same
// walk, so machine and canonical functions interoperate either way.
BUSTER_GLOBAL_LOCAL void machine_a64_va_named_cursors(MachineA64Selector* selector, u32* gp_count, u32* stack_parts)
{
    *gp_count = 0;
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
        u32 part_count = 1;
        bool aggregate = codegen_canonical_integer_aggregate_parts(selector->program, parameter_type_id, &part_count);
        if (aggregate && parameter && parameter->layout.size > 16)
        {
            part_count = 1;
        }
        if (*gp_count + part_count <= 8)
        {
            *gp_count += part_count;
        }
        else
        {
            *stack_parts += part_count;
        }
    }
}

// Builds the canonical four-word va_list image in the result's frame slot:
// the byte cursor into the X save area, the overflow pointer sixteen bytes
// past the frame-pointer pair plus the named stack parts, the save-area
// address, and a zero end flag.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_va_start(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrFunction* function = selector->function;

    u32 result_slot = instruction->result.value < function->value_count ? selector->value_stack_slots[instruction->result.value] : UINT32_MAX;
    bool selected = false;
    if (result_slot != UINT32_MAX && selector->va_register_save_slot != UINT32_MAX)
    {
        u32 gp_count;
        u32 stack_parts;
        machine_a64_va_named_cursors(selector, &gp_count, &stack_parts);
        u32 cursor_register = machine_a64_synthesize_register(selector);
        u32 cursor_immediate = machine_a64_append_immediate(selector, gp_count * 8u);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, cursor_register),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, cursor_immediate)},
                                             .opcode = MACHINE_A64_MOV_RI,
                                         });
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, cursor_register)},
                                             .payload = 0,
                                             .opcode = MACHINE_A64_STORE_FRAME64,
                                         });
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
                                             .payload = 8,
                                             .opcode = MACHINE_A64_STORE_FRAME64,
                                         });
        u32 save_register = machine_a64_synthesize_register(selector);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, save_register),
                                                          machine_ref_make(MACHINE_REF_STACK_SLOT, selector->va_register_save_slot)},
                                             .opcode = MACHINE_A64_LEA_FRAME,
                                         });
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, save_register)},
                                             .payload = 16,
                                             .opcode = MACHINE_A64_STORE_FRAME64,
                                         });
        u32 zero_register = machine_a64_synthesize_register(selector);
        u32 zero_immediate = machine_a64_append_immediate(selector, 0);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, zero_register),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, zero_immediate)},
                                             .opcode = MACHINE_A64_MOV_RI,
                                         });
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, zero_register)},
                                             .payload = 24,
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
    // The canonical model's end protocol writes one into the fourth word.
    // The pointer stores carry no displacement, so the flag address takes
    // its own add row.
    u32 source_register;
    bool selected = false;
    if (instruction->operand_count >= 1 && machine_a64_operand_register(selector, instruction->operands[0], &source_register))
    {
        u32 flag_register = machine_a64_synthesize_register(selector);
        u32 flag_immediate = machine_a64_append_immediate(selector, 1);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, flag_register),
                                                          machine_ref_make(MACHINE_REF_IMMEDIATE, flag_immediate)},
                                             .opcode = MACHINE_A64_MOV_RI,
                                         });
        u32 address_register = machine_a64_synthesize_register(selector);
        u32 address_row = machine_a64_select_row(selector, (MachineInstruction){
                                                               .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                                            machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source_register)},
                                                               .payload = 24,
                                                               .opcode = MACHINE_A64_LEA_OFFSET,
                                                           });
        machine_a64_define(selector, address_register, address_row);
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address_register),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, flag_register)},
                                             .opcode = MACHINE_A64_STORE_PTR64,
                                         });
        selected = true;
    }
    return selected;
}

// Translates the canonical VA_ARG gate into VA_ARG side data: scalars and
// integer-part aggregates of at most sixteen bytes, every part an
// eight-byte image read against the one cursor. The canonical emitter
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
                .alignment = 8,
                .stack_size = part_count * 8u,
                .part_count = part_count,
                .result_slot = result_slot,
                .result_is_frame = result_is_frame,
                .scalar_size = 8,
            };
            for (u32 part_index = 0; part_index < part_count; part_index += 1)
            {
                metadata_row->parts[part_index] = (MachineVaArgPart){
                    .value_offset = part_index * 8u,
                    .save_offset = part_index * 8u,
                    .size = 8,
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
        // Darwin passes every anonymous argument on the stack, which the
        // subset does not stage yet; AAPCS64 variadic scalars travel in
        // the same registers as named ones.
        bool darwin = selector->target.os == OPERATING_SYSTEM_MACOS || selector->target.os == OPERATING_SYSTEM_IOS;
        planned = !(variadic_call && darwin) && callee_type && callee_type->kind == IR_TYPE_FUNCTION &&
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
            // The whole sixteen-byte image loads straight from the value's
            // slot into its V argument register; no general register moves.
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, plan->argument_slots[argument_index])},
                                                 .payload = next_float << 24,
                                                 .opcode = MACHINE_A64_VLOAD_FRAME,
                                             });
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
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot)},
                                                 .payload = 0,
                                                 .opcode = MACHINE_A64_VSTORE_FRAME,
                                             });
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
BUSTER_GLOBAL_LOCAL bool machine_a64_select_aggregate_load(MachineA64Selector* selector, IrInstruction* instruction, IrInstruction* definition, u32 slot,
                                                           u32 result_slot)
{
    IrProgram* program = selector->program;

    bool selected = false;
    IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
    if (loaded_type && loaded_type->layout.resolved && loaded_type->layout.size <= UINT32_MAX)
    {
        if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, result_slot),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                 .payload = (u32)loaded_type->layout.size,
                                                 .opcode = MACHINE_A64_COPY_FRAME_FROM_FRAME,
                                             });
            selected = true;
        }
        else if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                 definition->opcode == IR_OPCODE_FIELD || machine_a64_local_is_indirect(selector, instruction->operands[0]))
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
BUSTER_GLOBAL_LOCAL bool machine_a64_select_register_load(MachineA64Selector* selector, IrInstruction* instruction, IrInstruction* definition, u32 slot,
                                                           u32 result_register)
{
    IrProgram* program = selector->program;

    bool selected = false;
    u32 place_register = selector->value_virtual_registers[instruction->operands[0].value];
    if (definition->opcode == IR_OPCODE_LOCAL && place_register != UINT32_MAX && !machine_a64_local_is_indirect(selector, instruction->operands[0]))
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
    else if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
    {
        u32 row = machine_a64_select_row(selector, (MachineInstruction){
                                                       .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, result_register),
                                                                    machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                       .opcode = MACHINE_A64_LOAD_FRAME,
                                                   });
        machine_a64_define(selector, result_register, row);
        selected = true;
    }
    else if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
             definition->opcode == IR_OPCODE_FIELD || machine_a64_local_is_indirect(selector, instruction->operands[0]))
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
        IrValue* place = function->values + instruction->operands[0].value;
        if (place->definition.value < function->instruction_count)
        {
            IrInstruction* definition = function->instructions + place->definition.value;
            u32 slot = selector->value_stack_slots[instruction->operands[0].value];
            u32 result_slot = selector->value_stack_slots[instruction->result.value];
            if (result_register != UINT32_MAX)
            {
                selected = machine_a64_select_register_load(selector, instruction, definition, slot, result_register);
            }
            else if (result_slot != UINT32_MAX)
            {
                selected = machine_a64_select_aggregate_load(selector, instruction, definition, slot, result_slot);
            }
        }
    }
    return selected;
}

// The aggregate form of a store: an exact-size copy out of the value slot,
// into a direct local's slot or through an address vreg.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_aggregate_store(MachineA64Selector* selector, IrInstruction* instruction, IrInstruction* definition, u64 size,
                                                            u32 slot, u32 value_slot)
{
    bool selected = false;
    if (size && size <= UINT32_MAX)
    {
        if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
        {
            machine_a64_select_row(selector, (MachineInstruction){
                                                 .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                              machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                 .payload = (u32)size,
                                                 .opcode = MACHINE_A64_COPY_FRAME_FROM_FRAME,
                                             });
            selected = true;
        }
        else if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                 definition->opcode == IR_OPCODE_FIELD || machine_a64_local_is_indirect(selector, instruction->operands[0]))
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
BUSTER_GLOBAL_LOCAL bool machine_a64_select_register_store(MachineA64Selector* selector, IrInstruction* instruction, IrInstruction* definition, u64 size,
                                                           u32 slot)
{
    bool selected = false;
    u32 value_register;
    u32 size_index = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : size == 8 ? 3 : UINT32_MAX;
    u32 place_register = selector->value_virtual_registers[instruction->operands[0].value];
    if (size_index != UINT32_MAX && machine_a64_operand_register(selector, instruction->operands[1], &value_register))
    {
        if (definition->opcode == IR_OPCODE_LOCAL && place_register != UINT32_MAX && !machine_a64_local_is_indirect(selector, instruction->operands[0]))
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
        else if (definition->opcode == IR_OPCODE_LOCAL && slot != UINT32_MAX)
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
        else if (definition->opcode == IR_OPCODE_DEREFERENCE || definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                 definition->opcode == IR_OPCODE_FIELD || machine_a64_local_is_indirect(selector, instruction->operands[0]))
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
        IrValue* place = function->values + instruction->operands[0].value;
        if (place->definition.value < function->instruction_count)
        {
            IrInstruction* definition = function->instructions + place->definition.value;
            IrType* stored_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
            u64 size = stored_type && stored_type->layout.resolved ? stored_type->layout.size : 0;
            u32 slot = selector->value_stack_slots[instruction->operands[0].value];
            u32 value_slot = selector->value_stack_slots[instruction->operands[1].value];
            if (value_slot != UINT32_MAX && selector->value_virtual_registers[instruction->operands[1].value] == UINT32_MAX)
            {
                selected = machine_a64_select_aggregate_store(selector, instruction, definition, size, slot, value_slot);
            }
            else
            {
                selected = machine_a64_select_register_store(selector, instruction, definition, size, slot);
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
// multiply by a power of two, keeping the x86-64 structure.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_bit_field_unit(MachineA64Selector* selector, IrInstruction* instruction, IrType* type, u32 slot, u32 first,
                                                           u64 field_offset, u64 field_size, u16 unit_store_opcode, u8* member_emitted)
{
    bool selected = true;
    u32 unit_register = machine_a64_synthesize_register(selector);
    u32 zero_immediate = selector->immediates.total_count;
    u64* zero_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
    *zero_row = 0;
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register),
                                                      machine_ref_make(MACHINE_REF_IMMEDIATE, zero_immediate)},
                                         .opcode = MACHINE_A64_MOV_RI,
                                     });
    for (u32 sibling = first; sibling < instruction->operand_count && selected; sibling += 1)
    {
        u64 sibling_field = instruction->immediates[sibling];
        if (!member_emitted[sibling] && sibling_field < type->field_count && type->fields[sibling_field].is_bit_field &&
            type->fields[sibling_field].offset == field_offset)
        {
            u32 bit_offset = type->fields[sibling_field].bit_offset;
            u32 bit_width = type->fields[sibling_field].bit_width;
            // Read only where the lookup that fills it succeeded.
            u32 value_register = UINT32_MAX;
            selected = bit_width && bit_offset + bit_width <= field_size * 8 &&
                       machine_a64_operand_register(selector, instruction->operands[sibling], &value_register);
            if (selected)
            {
                u32 mask_immediate = selector->immediates.total_count;
                u64* mask_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
                *mask_row = bit_width >= 64 ? UINT64_MAX : (((u64)1 << bit_width) - 1);
                u32 mask_register = machine_a64_synthesize_register(selector);
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register),
                                                                  machine_ref_make(MACHINE_REF_IMMEDIATE, mask_immediate)},
                                                     .opcode = MACHINE_A64_MOV_RI,
                                                 });
                u32 masked_register = machine_a64_synthesize_register(selector);
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask_register)},
                                                     .opcode = MACHINE_A64_AND64,
                                                 });
                if (bit_offset)
                {
                    u32 scale_immediate = selector->immediates.total_count;
                    u64* scale_row = (u64*)machine_stream_append(selector->arena, &selector->immediates);
                    *scale_row = (u64)1 << bit_offset;
                    u32 scale_register = machine_a64_synthesize_register(selector);
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scale_register),
                                                                      machine_ref_make(MACHINE_REF_IMMEDIATE, scale_immediate)},
                                                         .opcode = MACHINE_A64_MOV_RI,
                                                     });
                    u32 shifted_register = machine_a64_synthesize_register(selector);
                    machine_a64_select_row(selector, (MachineInstruction){
                                                         .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, shifted_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register),
                                                                      machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, scale_register)},
                                                         .opcode = MACHINE_A64_MUL64,
                                                     });
                    masked_register = shifted_register;
                }
                u32 merged_register = machine_a64_synthesize_register(selector);
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, merged_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register),
                                                                  machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, masked_register)},
                                                     .opcode = MACHINE_A64_ORR64,
                                                 });
                unit_register = merged_register;
                member_emitted[sibling] = 1;
            }
        }
    }
    if (selected)
    {
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot),
                                                          machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, unit_register)},
                                             .payload = (u32)field_offset,
                                             .opcode = unit_store_opcode,
                                         });
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
            else if (!member_emitted[index])
            {
                u64 field_offset = type->fields[field_index].offset;
                IrType* field_type = ir_type_from_id(&program->types, type->fields[field_index].type);
                u64 field_size = field_type && field_type->layout.resolved ? field_type->layout.size : 0;
                u16 unit_store_opcode = field_size == 1   ? MACHINE_A64_STORE_FRAME8
                                        : field_size == 2 ? MACHINE_A64_STORE_FRAME16
                                        : field_size == 4 ? MACHINE_A64_STORE_FRAME32
                                        : field_size == 8 ? MACHINE_A64_STORE_FRAME64
                                                          : 0;
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
                    selected = unit_store_opcode && machine_a64_select_bit_field_unit(selector, instruction, type, slot, index, field_offset, field_size,
                                                                                      unit_store_opcode, member_emitted);
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

// Scalar atomic load: the place's address into a fresh register, one
// ldar-family word into the result. Every memory order takes the acquire
// form — stronger than relaxed asks for, and what keeps the row count at
// one. Aggregate results are slot-backed and never reach here (no result
// register), exactly the canonical rejection.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_atomic_load(MachineA64Selector* selector, IrInstruction* instruction, u32 result_register)
{
    IrProgram* program = selector->program;

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
    return selected;
}

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

BUSTER_GLOBAL_LOCAL bool machine_a64_select_branch(MachineA64Selector* selector, IrInstruction* instruction)
{
    machine_a64_select_row(selector, (MachineInstruction){
                                         .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value)},
                                         .opcode = MACHINE_A64_B,
                                     });
    return true;
}

// A fused condition re-selects the chain's innermost comparison here,
// immediately before BCC: only allocator edits can land between the flags
// define and its use, and every edit form is a flag-preserving instruction
// (ldr/str, orr copy, movz/movk).
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
                                                 .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value),
                                                              machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[1].value)},
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
                                                 .operands = {machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[0].value),
                                                              machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[1].value)},
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
        u32 first_case = selector->switch_cases.total_count;
        for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
        {
            MachineSwitchCase* case_row = (MachineSwitchCase*)machine_stream_append(selector->arena, &selector->switch_cases);
            *case_row = (MachineSwitchCase){
                .value = instruction->immediates[case_index],
                .target_block = instruction->targets[case_index].value,
            };
        }
        machine_a64_select_row(selector, (MachineInstruction){
                                             .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, condition_register),
                                                          machine_ref_make(MACHINE_REF_BLOCK, instruction->targets[instruction->target_count - 1].value)},
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
            // The whole sixteen-byte image loads from the value's slot into
            // V0, the vector analog of the scalar X0 move.
            u32 value_slot = instruction->operands[0].value < function->value_count
                                 ? selector->value_stack_slots[instruction->operands[0].value]
                                 : UINT32_MAX;
            selected = value_slot != UINT32_MAX;
            if (selected)
            {
                machine_a64_select_row(selector, (MachineInstruction){
                                                     .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, value_slot)},
                                                     .payload = 0,
                                                     .opcode = MACHINE_A64_VLOAD_FRAME,
                                                 });
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
            selected = machine_a64_select_unary(selector, instruction, result_register);
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
        case IR_OPCODE_ATOMIC_FENCE:
            selected = machine_a64_select_atomic_fence(selector, instruction);
            break;
        case IR_OPCODE_BRANCH:
            selected = machine_a64_select_branch(selector, instruction);
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
        // Darwin's variadic definition ABI places every anonymous argument
        // on the stack and stays on the canonical path even when the body
        // ignores its tail; AAPCS64 bodies continue through ordinary
        // selection, so the first va_* the subset cannot shape (or any
        // earlier unsupported operation) reports in true IR order.
        bool variadic_darwin = target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS;
        if (!function_type || function_type->kind != IR_TYPE_FUNCTION || (function_type->is_variadic && variadic_darwin) ||
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
        if (!assume_validated && !machine_selection_prepass_build_minimal(arena, program, function).valid)
        {
            return result;
        }
        MachineSelectionValueFacts value_facts = machine_selection_value_facts_allocate(arena, function->value_count);
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
            // The canonical non-Darwin model saves X0-X7 into a fixed
            // 64-byte area; a dedicated frame slot keeps its displacement
            // in ordinary placement data while VA_SAVE owns the prologue
            // snapshot itself.
            selector.va_register_save_slot = machine_a64_append_slot(&selector, 64, 8);
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
                if (parameter->value.value >= function->value_count ||
                    (!machine_a64_type_is_scalar_register(parameter_type) && !machine_a64_type_is_float_scalar(parameter_type)))
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
                if (instruction->opcode == IR_OPCODE_LOCAL && instruction->result.value == value_index)
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
            row_layout.block_row_counts[block_index] = block_row_count;
            block_candidate_counts[block_index] = block_candidate_count;
        }
        if (!dense_rows)
        {
            // Relinked blocks do not have a dense id range.  Gather their
            // program order once so every consumer below preserves that order.
            row_layout.rows = arena_allocate(arena, u32, function->instruction_count ? function->instruction_count : 1);
            u32 gathered_rows = 0;
            for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
            {
                IrBlock* block = function->blocks + block_index;
                for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID;
                     id = function->instructions[id.value].next)
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
                                                  machine_selection_row_id(&row_layout, block, walked_ordinals, row_offset);
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
                    // va_list is a four-word aggregate in the canonical
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
                else if ((instruction->opcode == IR_OPCODE_ARGUMENT || instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_CALL ||
                          instruction->opcode == IR_OPCODE_AGGREGATE || instruction->opcode == IR_OPCODE_ARRAY ||
                          instruction->opcode == IR_OPCODE_VA_ARG ||
                          (instruction->opcode == IR_OPCODE_BINARY && value_type && value_type->kind == IR_TYPE_VECTOR)) &&
                         value_type && value_type->layout.resolved && value_type->layout.size <= UINT32_MAX - 7 &&
                         (value_type->kind == IR_TYPE_STRUCT || value_type->kind == IR_TYPE_UNION || value_type->kind == IR_TYPE_SLICE ||
                          value_type->kind == IR_TYPE_VECTOR ||
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
            classified_rows += block_row_count;
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
            // original row ID through the dense-or-gathered layout.
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
                        MachineA64ValueShape* shape = selector.parameter_shapes + argument_index;
                        MachineA64ArgumentPlacement* parameter_placement = selector.parameter_placements + argument_index;
                        u32 next_integer = parameter_placement->first_integer;
                        u32 next_float = parameter_placement->first_float;
                        if (parameter_placement->on_stack && shape->aggregate && !shape->indirect)
                        {
                            // A stack-passed aggregate, HFA, or vector: its
                            // eightbyte images read back from the caller's
                            // outgoing area into the value's slot during the
                            // integer pass — the loads touch no argument
                            // register of either class.
                            if (!float_pass)
                            {
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
                            }
                            continue;
                        }
                        if (shape->vector)
                        {
                            // The whole incoming V register stores straight
                            // into the value's slot; no general scratch is
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
                                machine_a64_select_row(&selector, (MachineInstruction){
                                                                      .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, slot)},
                                                                      .payload = next_float << 24,
                                                                      .opcode = MACHINE_A64_VSTORE_FRAME,
                                                                  });
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
                    }
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
                if (!machine_a64_select_instruction(&selector, instruction))
                {
                    machine_a64_reject(&selector, instruction->opcode);
                    break;
                }
            }
            selected_rows += block_row_count;
            machine_builder_block_end(&selector.builder, (MachineBlock){.parameter_offset = parameter_offset, .parameter_count = (u16)block->parameter_count});
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
        result.function.call_target_count = selector.call_targets.total_count;
        machine_stream_flatten(&selector.call_targets, result.function.call_targets);
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
        result.supported = true;
        result.selector_certified = true;
        result.returns_value = returns_value;
        result.selected_typed_instructions = typed_instruction_count;
        result.machine_instructions = result.function.instruction_count;
        result.simd_operation_count = simd_operation_count;
        result.selection_counters = selector.selection_counters;
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
    u16 reserved;
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
    if ((size != 1 && size != 2 && size != 4 && size != 8) || offset % scale)
    {
        encoder->error = true;
        return;
    }
    u32 base_register = MACHINE_A64_X28;
    if (offset / scale > A64_IMM12_MAX)
    {
        machine_a64_emit_immediate(encoder, MACHINE_A64_X16, offset);
        machine_a64_emit(encoder, 0x8b000000 | (MACHINE_A64_X16 << 16) | (base_register << 5) | MACHINE_A64_X16);
        base_register = MACHINE_A64_X16;
        offset = 0;
    }
    machine_a64_emit_generated_unsigned_memory(encoder, register_number, base_register, offset, size, store);
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
    if (!encoder || !fixup || !expansion_kind || expansion_kind > 2 || fixup->expanded >= expansion_kind || encoder->count < 4 ||
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
            capacity64 += 20 + (u64)frame_chunk_words * 4 + (u64)push_count * 4;
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
        case MACHINE_A64_COPY_FRAME_FROM_FRAME:
        case MACHINE_A64_COPY_FRAME_FROM_PTR:
        case MACHINE_A64_COPY_PTR_FROM_FRAME:
            // One load/store word pair per eight-byte chunk, plus sized
            // tail accesses that may each take the large-offset form.
            capacity64 += ((u64)capacity_row->payload / 8) * 8 + 48;
            break;
        case MACHINE_A64_VA_SAVE:
            // Eight stores, each possibly in the large-offset frame form.
            capacity64 += 8 * 24;
            break;
        case MACHINE_A64_VLOAD_FRAME:
        case MACHINE_A64_VSTORE_FRAME:
            // One q transfer, possibly behind the X16 materialize-and-add.
            capacity64 += 24;
            break;
        case MACHINE_A64_VA_ARG:
            // Two bounded part sequences around the cursor split, each
            // part possibly storing through the large-offset frame form.
            capacity64 += 48 + 2u * ((u64)MACHINE_VA_ARG_PART_LIMIT * 32 + 12);
            break;
        case MACHINE_A64_ATOMIC_RMW:
            // ld(a)xr, operation, st(l)xr, cbnz.
            capacity64 += 16;
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
    u32 save_slot = 0;
    for (u32 saved_register = 0; saved_register < MACHINE_A64_REGISTER_COUNT; saved_register += 1)
    {
        if (!((placement->callee_saved_mask >> saved_register) & 1u))
        {
            continue;
        }
        save_slot += 1;
        machine_a64_emit_generated_unsigned_memory(&encoder, saved_register, MACHINE_A64_SP, frame_area - 8 * save_slot, 8, true);
    }
    machine_a64_emit_generated_unsigned_memory(&encoder, MACHINE_A64_X28, MACHINE_A64_SP, frame_area, 8, true);
    {
        u32 fields[] = {MACHINE_A64_X28, MACHINE_A64_SP, 0};
        machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, fields, BUSTER_ARRAY_LENGTH(fields));
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
                machine_a64_emit_generated_unsigned_memory(&encoder, operand_registers[0], MACHINE_A64_X29, instruction->payload, 8, false);
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
                    machine_a64_emit(&encoder, 0xeb11001fu | (switch_condition << 5));
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
                // The canonical page-probed loop verbatim: align the X9
                // byte count up through the X10 mask, probe and drop SP a
                // page at a time, take the sub-page tail, and hand the
                // new stack pointer back in X10.
                u64 alloc_mask = (u64)instruction->payload - 1;
                machine_a64_emit_immediate(&encoder, MACHINE_A64_X10, alloc_mask);
                machine_a64_emit(&encoder, 0x8b0a0129u);
                machine_a64_emit_immediate(&encoder, MACHINE_A64_X10, ~alloc_mask);
                machine_a64_emit(&encoder, 0x8a0a0129u);
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
                // The canonical prologue's X0-X7 snapshot, addressed like
                // any other frame slot: byte k of the slot lives at the
                // shared grows-down convention's offset minus k.
                u32 va_save_slot = machine_ref_payload(instruction->operands[0]);
                for (u32 register_index = 0; register_index < 8; register_index += 1)
                {
                    u32 save_offset = machine_a64_frame_offset(frame_area, placement->stack_slot_offsets[va_save_slot] - register_index * 8u);
                    machine_a64_emit_frame_memory(&encoder, register_index, save_offset, 8, true);
                }
            }
            break;
            case MACHINE_A64_VA_ARG:
            {
                // The canonical register/overflow split, word for word:
                // X11 carries the byte cursor, X12 the part address, X9
                // the part data for frame results — a scalar result loads
                // its fixed destination directly instead.
                MachineVaArg* metadata = function->va_args + instruction->payload;
                u32 list = operand_registers[0];
                u32 increment = metadata->part_count * 8u;
                u32 limit = 64u - increment;
                machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X11, list, 0, 8, false);
                machine_a64_emit(&encoder, 0xf100001fu | ((u32)MACHINE_A64_X11 << 5) | (limit << 10));
                u32 overflow_patch = encoder.count;
                machine_a64_emit(&encoder, 0x54000008u);
                machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X12, list, 16, 8, false);
                machine_a64_emit(&encoder, 0x8b000000u | ((u32)MACHINE_A64_X11 << 16) | ((u32)MACHINE_A64_X12 << 5) | MACHINE_A64_X12);
                for (u32 part_index = 0; part_index < metadata->part_count; part_index += 1)
                {
                    if (metadata->result_is_frame)
                    {
                        machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X9, MACHINE_A64_X12, part_index * 8u, 8, false);
                        u32 part_offset = machine_a64_frame_offset(
                            frame_area, placement->stack_slot_offsets[metadata->result_slot] - part_index * 8u);
                        machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X9, part_offset, 8, true);
                    }
                    else
                    {
                        machine_a64_emit_pointer_memory(&encoder, operand_registers[1], MACHINE_A64_X12, 0, 8, false);
                    }
                }
                u32 increment_fields[] = {MACHINE_A64_X11, MACHINE_A64_X11, increment};
                machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, increment_fields,
                                                BUSTER_ARRAY_LENGTH(increment_fields));
                machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X11, list, 0, 8, true);
                u32 end_patch = encoder.count;
                machine_a64_emit(&encoder, 0x14000000u);
                u32 overflow_offset = encoder.count;
                machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X12, list, 8, 8, false);
                for (u32 part_index = 0; part_index < metadata->part_count; part_index += 1)
                {
                    if (metadata->result_is_frame)
                    {
                        machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X9, MACHINE_A64_X12, part_index * 8u, 8, false);
                        u32 part_offset = machine_a64_frame_offset(
                            frame_area, placement->stack_slot_offsets[metadata->result_slot] - part_index * 8u);
                        machine_a64_emit_frame_memory(&encoder, MACHINE_A64_X9, part_offset, 8, true);
                    }
                    else
                    {
                        machine_a64_emit_pointer_memory(&encoder, operand_registers[1], MACHINE_A64_X12, 0, 8, false);
                    }
                }
                u32 overflow_fields[] = {MACHINE_A64_X12, MACHINE_A64_X12, increment};
                machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, overflow_fields,
                                                BUSTER_ARRAY_LENGTH(overflow_fields));
                machine_a64_emit_pointer_memory(&encoder, MACHINE_A64_X12, list, 8, 8, true);
                u32 end_offset = encoder.count;
                if (!encoder.overflow && !encoder.error)
                {
                    u32 conditional = 0x54000008u | (((overflow_offset - overflow_patch) / 4) << 5);
                    memcpy(encoder.bytes + overflow_patch, &conditional, sizeof(conditional));
                    u32 branch = 0x14000000u | ((end_offset - end_patch) / 4);
                    memcpy(encoder.bytes + end_patch, &branch, sizeof(branch));
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
                {
                    u32 fields[] = {MACHINE_A64_SP, MACHINE_A64_X28, 0};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, fields, BUSTER_ARRAY_LENGTH(fields));
                }
                u32 restore_slot = 0;
                for (u32 saved_register = 0; saved_register < MACHINE_A64_REGISTER_COUNT; saved_register += 1)
                {
                    if (!((placement->callee_saved_mask >> saved_register) & 1u))
                    {
                        continue;
                    }
                    restore_slot += 1;
                    machine_a64_emit_generated_unsigned_memory(&encoder, saved_register, MACHINE_A64_SP, frame_area - 8 * restore_slot, 8, false);
                }
                machine_a64_emit_generated_unsigned_memory(&encoder, MACHINE_A64_X28, MACHINE_A64_SP, frame_area, 8, false);
                u32 release_remaining = frame_total;
                while (release_remaining)
                {
                    u32 release_chunk = BUSTER_MIN(release_remaining, A64_SP_ADJUST_CHUNK);
                    u32 fields[] = {MACHINE_A64_SP, MACHINE_A64_SP, release_chunk};
                    machine_a64_emit_generated_form(&encoder, BUSTER_AARCH64_GENERATED_FORM_ADDXRI, fields, BUSTER_ARRAY_LENGTH(fields));
                    release_remaining -= release_chunk;
                }
                machine_a64_emit(&encoder, 0xa8c17bfd);
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
