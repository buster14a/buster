// Target-neutral machine-IR implementation and the front door of the
// machine path (machine.h documents the representation). This file owns
// what every backend shares — MachineRef/MachinePoint encoding, the
// MachineOpcodeInfo metadata accessors, the chunked instruction stream and
// function builder, frequency-class stamping, the verifier, the baseline
// MIR_STACK placement, and replay serialization — and then includes the
// implementation files at the bottom in the backend-implementation-file
// pattern (selection facts, the x86-64 and AArch64 selectors/encoders,
// scheduling, and the FAST/QUALITY register allocators), so none of those
// are standalone translation units. machine_select_canonical_function at
// the end is the entry point codegen.c calls.

#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/codegen/machine_x86_64_emit_registry.h>

#include <buster/lib/os.h>
#include <buster/lib/string.h>

MachineRef machine_ref_make(MachineRefKind kind, u32 payload)
{
    BUSTER_CHECK(kind < MACHINE_REF_KIND_COUNT);
    BUSTER_CHECK(payload < MACHINE_REF_PAYLOAD_LIMIT);
    return ((u32)kind << MACHINE_REF_PAYLOAD_BITS) | payload;
}

MachineRefKind machine_ref_kind(MachineRef ref)
{
    return (MachineRefKind)(ref >> MACHINE_REF_PAYLOAD_BITS);
}

u32 machine_ref_payload(MachineRef ref)
{
    return ref & (MACHINE_REF_PAYLOAD_LIMIT - 1u);
}

MachinePoint machine_point_make(u32 instruction_index, MachinePointPhase phase)
{
    BUSTER_CHECK(instruction_index < MACHINE_POINT_INSTRUCTION_LIMIT);
    BUSTER_CHECK(phase < MACHINE_POINT_PHASE_COUNT);
    return (instruction_index << MACHINE_POINT_PHASE_BITS) | (u32)phase;
}

u32 machine_point_instruction(MachinePoint point)
{
    return point >> MACHINE_POINT_PHASE_BITS;
}

MachinePointPhase machine_point_phase(MachinePoint point)
{
    return (MachinePointPhase)(point & ((1u << MACHINE_POINT_PHASE_BITS) - 1u));
}

MachineEmitRecipeCategory machine_emit_recipe_category(MachineEmitRecipeId recipe)
{
    MachineEmitRecipeCategory result;
    if (recipe == MACHINE_EMIT_RECIPE_INVALID)
    {
        result = MACHINE_EMIT_RECIPE_CATEGORY_COUNT;
    }
    else
    {
        result = (MachineEmitRecipeCategory)(recipe >> MACHINE_EMIT_RECIPE_CATEGORY_SHIFT);
    }

    return result;
}

u16 machine_emit_recipe_index(MachineEmitRecipeId recipe)
{
    return (u16)(recipe & MACHINE_EMIT_RECIPE_INDEX_MASK);
}

bool machine_emit_recipe_is_valid(MachineEmitRecipeId recipe)
{
    MachineEmitRecipeCategory category = machine_emit_recipe_category(recipe);
    return category < MACHINE_EMIT_RECIPE_CATEGORY_COUNT &&
           (category != MACHINE_EMIT_RECIPE_CATEGORY_NONE || machine_emit_recipe_index(recipe) == 0);
}

#define MACHINE_OPERAND_USE_GENERAL ((u8)(MACHINE_OPERAND_ROLE_USE | (MACHINE_REGISTER_CLASS_GENERAL << MACHINE_OPERAND_CLASS_SHIFT)))
#define MACHINE_OPERAND_DEFINE_GENERAL ((u8)(MACHINE_OPERAND_ROLE_DEFINE | (MACHINE_REGISTER_CLASS_GENERAL << MACHINE_OPERAND_CLASS_SHIFT)))
#define MACHINE_OPERAND_USE_DEFINE_GENERAL ((u8)(MACHINE_OPERAND_ROLE_USE_DEFINE | (MACHINE_REGISTER_CLASS_GENERAL << MACHINE_OPERAND_CLASS_SHIFT)))
#define MACHINE_OPERAND_USE_VECTOR ((u8)(MACHINE_OPERAND_ROLE_USE | (MACHINE_REGISTER_CLASS_VECTOR << MACHINE_OPERAND_CLASS_SHIFT)))
#define MACHINE_OPERAND_DEFINE_VECTOR ((u8)(MACHINE_OPERAND_ROLE_DEFINE | (MACHINE_REGISTER_CLASS_VECTOR << MACHINE_OPERAND_CLASS_SHIFT)))
#define MACHINE_OPERAND_USE_DEFINE_VECTOR ((u8)(MACHINE_OPERAND_ROLE_USE_DEFINE | (MACHINE_REGISTER_CLASS_VECTOR << MACHINE_OPERAND_CLASS_SHIFT)))
// The scalar-float encoder sequences stage through XMM0/XMM1 (SSE forms) and
// the float-argument bridge writes xmm[payload] with payload at most seven;
// in the unified numbering those are vector registers 16-23, and declaring
// them keeps vector values clear of rows that scribble the low XMM file.
#define MACHINE_X64_FLOAT_SCRATCH_CLOBBER ((1u << MACHINE_X64_ZMM0) | (1u << MACHINE_X64_ZMM1))
#define MACHINE_X64_FLOAT_BRIDGE_CLOBBER (0xffu << MACHINE_X64_ZMM0)

// Shorthand rows for the x86-64 scalar subset: destination-and-source
// moves, read-modify-write arithmetic, flag producers/consumers, frame and
// pointer memory forms, and terminators.
#define MACHINE_INFO_MOVE(name_literal)                                                                                                                        \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},               \
        .form_set = MACHINE_OPCODE_FORM_SET_REGISTER, .expansion_recipe = MACHINE_OPCODE_EXPANSION_SINGLE,                               \
    }
#define MACHINE_INFO_READ_MODIFY(name_literal)                                                                                                                 \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},           \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,                                                                                                   \
    }
#define MACHINE_INFO_TWO_ADDRESS_SSA(name_literal, schedule)                                                                                                   \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 3,                                                                                              \
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                                            \
        .tied_pair = (u8)(1u | (2u << 4)),                                                                                                                     \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,                                                                                                   \
        .schedule_class = (schedule), .expansion_recipe = MACHINE_OPCODE_EXPANSION_SINGLE,                                                                    \
        .form_set = MACHINE_OPCODE_FORM_SET_REGISTER,                                                                                    \
    }
#define MACHINE_INFO_FINALIZE(flags, effect, memslot, schedule)                                                                                                \
    .attributes = (flags), .memory_effect = (effect), .memory_operand = (memslot), .schedule_class = (schedule),                                              \
    .expansion_recipe = MACHINE_OPCODE_EXPANSION_SINGLE
#define MACHINE_INFO_SHIFT(name_literal)                                                                                                                       \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},           \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,                                                            \
        .schedule_class = MACHINE_SCHEDULE_CLASS_SHIFT, .fixed_register_set = (1u << MACHINE_X64_RAX) | (1u << MACHINE_X64_RCX),                              \
        .fixed_register_mask = 0x3, .fixed_registers = {MACHINE_X64_RAX, MACHINE_X64_RCX},                                                                    \
    }
#define MACHINE_INFO_DIVIDE(name_literal)                                                                                                                      \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},           \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED, .clobber_mask = 1u << MACHINE_X64_RDX,                     \
        .schedule_class = MACHINE_SCHEDULE_CLASS_DIV, .fixed_register_set = (1u << MACHINE_X64_RAX) | (1u << MACHINE_X64_RCX),                               \
        .fixed_register_mask = 0x3, .fixed_registers = {MACHINE_X64_RAX, MACHINE_X64_RCX},                                                                    \
        .implicit_physical_defs = 1ull << MACHINE_X64_RDX,                                                                                                     \
    }
#define MACHINE_INFO_MOVE_CONSTRAINED(name_literal)                                                                                                            \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},               \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,                                                                                                    \
    }
#define MACHINE_INFO_MOVE_CONSTRAINED_CLOBBER(name_literal, clobbers)                                                                                          \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},               \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED, .clobber_mask = (clobbers), .implicit_physical_defs = (clobbers),                                  \
    }
#define MACHINE_INFO_THREE_ADDRESS(name_literal)                                                                                                               \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 3,                                                                                              \
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                                            \
        .form_set = MACHINE_OPCODE_FORM_SET_REGISTER, .expansion_recipe = MACHINE_OPCODE_EXPANSION_SINGLE,                               \
    }
#define MACHINE_INFO_A64_THREE_ADDRESS(name_literal, schedule)                                                                                                 \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 3,                                                                                              \
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                                            \
        .form_set = MACHINE_OPCODE_FORM_SET_REGISTER, .expansion_recipe = MACHINE_OPCODE_EXPANSION_SINGLE,                               \
        .schedule_class = (schedule),                                                                                                                          \
    }
#define MACHINE_INFO_THREE_ADDRESS_CONSTRAINED(name_literal)                                                                                                   \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 3,                                                                                              \
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                                            \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED | MACHINE_OPCODE_ATTRIBUTE_EXPANDS, .form_set = MACHINE_OPCODE_FORM_SET_PSEUDO,\
        .expansion_recipe = MACHINE_OPCODE_EXPANSION_PSEUDO, .schedule_class = MACHINE_SCHEDULE_CLASS_DIV,                                                    \
    }
#define MACHINE_INFO_A64_COMPARE(name_literal)                                                                                                                 \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                  \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE, .implicit_resource_defs = MACHINE_RESOURCE_NZCV_MASK,                                             \
        .form_set = MACHINE_OPCODE_FORM_SET_REGISTER, .expansion_recipe = MACHINE_OPCODE_EXPANSION_SINGLE,                               \
    }
#define MACHINE_INFO_UNARY_READ_MODIFY(name_literal)                                                                                                           \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 1, .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL},                                        \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,                                                                                                   \
    }
#define MACHINE_INFO_COMPARE(name_literal)                                                                                                                     \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                  \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE, .implicit_resource_defs = MACHINE_RESOURCE_FLAGS_MASK,                                             \
    }
#define MACHINE_INFO_STORE_FRAME(name_literal)                                                                                                                 \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {0, MACHINE_OPERAND_USE_GENERAL},                                            \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_MEMORY, .memory_effect = MACHINE_MEMORY_EFFECT_WRITE, .memory_operand = 1,                                      \
        .schedule_class = MACHINE_SCHEDULE_CLASS_STORE,                                                                                                         \
    }
#define MACHINE_INFO_MOVE_CLOBBER(name_literal, clobbers)                                                                                                      \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},               \
        .clobber_mask = (clobbers),                                                                                                                            \
    }
#define MACHINE_INFO_LOAD_POINTER(name_literal)                                                                                                                 \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},               \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_MEMORY, .memory_effect = MACHINE_MEMORY_EFFECT_READ, .memory_operand = 2,                                       \
        .schedule_class = MACHINE_SCHEDULE_CLASS_LOAD, .form_set = MACHINE_OPCODE_FORM_SET_REGISTER,                                     \
        .expansion_recipe = MACHINE_OPCODE_EXPANSION_SINGLE,                                                                                                   \
    }
#define MACHINE_INFO_STORE_POINTER(name_literal)                                                                                                               \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                  \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_MEMORY, .memory_effect = MACHINE_MEMORY_EFFECT_WRITE, .memory_operand = 1,                                      \
        .schedule_class = MACHINE_SCHEDULE_CLASS_STORE,                                                                                                         \
    }

BUSTER_GLOBAL_LOCAL MachineOpcodeInfo const machine_opcode_infos[MACHINE_OPCODE_COUNT] = {
    [MACHINE_OPCODE_INVALID] = {
        .name = S8_INITIALIZER("invalid"),
    },
    [MACHINE_OPCODE_SKELETON_NOP] = {
        .name = S8_INITIALIZER("skeleton_nop"),
    },
    [MACHINE_OPCODE_SKELETON_COPY] = {
        .name = S8_INITIALIZER("skeleton_copy"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
    },
    [MACHINE_OPCODE_SKELETON_RETURN] = {
        .name = S8_INITIALIZER("skeleton_return"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_X64_MOV_RI] = {
        .name = S8_INITIALIZER("x64_mov_ri"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    [MACHINE_X64_MOV_RR] = MACHINE_INFO_MOVE("x64_mov_rr"),
    [MACHINE_X64_MOV32_RR] = MACHINE_INFO_MOVE("x64_mov32_rr"),
    [MACHINE_X64_MOVSX8_RR] = MACHINE_INFO_MOVE("x64_movsx8_rr"),
    [MACHINE_X64_MOVSX16_RR] = MACHINE_INFO_MOVE("x64_movsx16_rr"),
    [MACHINE_X64_MOVSX32_RR] = MACHINE_INFO_MOVE("x64_movsx32_rr"),
    [MACHINE_X64_MOVZX8_RR] = MACHINE_INFO_MOVE("x64_movzx8_rr"),
    [MACHINE_X64_MOVZX16_RR] = MACHINE_INFO_MOVE("x64_movzx16_rr"),
    // The branchy conversion sequences reuse RCX as an internal scratch —
    // the sticky-bit halving on the unsigned-to-float side, the constant
    // threshold on the float-to-unsigned side — so the source staged there
    // is gone after the row. Latent until local promotion: before it, the
    // staged value always died at its row, so nothing read the leftover.
    [MACHINE_X64_CVT_U64_TO_F32] = MACHINE_INFO_MOVE_CONSTRAINED_CLOBBER("x64_cvt_u64_to_f32", (1u << MACHINE_X64_RCX) | MACHINE_X64_FLOAT_SCRATCH_CLOBBER),
    [MACHINE_X64_CVT_U64_TO_F64] = MACHINE_INFO_MOVE_CONSTRAINED_CLOBBER("x64_cvt_u64_to_f64", (1u << MACHINE_X64_RCX) | MACHINE_X64_FLOAT_SCRATCH_CLOBBER),
    [MACHINE_X64_CVT_F32_TO_U64] = MACHINE_INFO_MOVE_CONSTRAINED_CLOBBER("x64_cvt_f32_to_u64", (1u << MACHINE_X64_RCX) | MACHINE_X64_FLOAT_SCRATCH_CLOBBER),
    [MACHINE_X64_CVT_F64_TO_U64] = MACHINE_INFO_MOVE_CONSTRAINED_CLOBBER("x64_cvt_f64_to_u64", (1u << MACHINE_X64_RCX) | MACHINE_X64_FLOAT_SCRATCH_CLOBBER),
    [MACHINE_X64_LEA_OFFSET] = MACHINE_INFO_MOVE("x64_lea_offset"),
    [MACHINE_X64_ADD64_IMM] = {
        .name = S8_INITIALIZER("x64_add64_imm"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,
    },
    [MACHINE_X64_IMUL64_RRI] = {
        .name = S8_INITIALIZER("x64_imul64_rri"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,
    },
    [MACHINE_X64_BSF32] = MACHINE_INFO_MOVE("x64_bsf32"),
    [MACHINE_X64_BSF64] = MACHINE_INFO_MOVE("x64_bsf64"),
    [MACHINE_X64_BSR32] = MACHINE_INFO_MOVE("x64_bsr32"),
    [MACHINE_X64_BSR64] = MACHINE_INFO_MOVE("x64_bsr64"),
    [MACHINE_X64_POPCNT32] = MACHINE_INFO_MOVE("x64_popcnt32"),
    [MACHINE_X64_POPCNT64] = MACHINE_INFO_MOVE("x64_popcnt64"),
    [MACHINE_X64_ADD32] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_add32", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_X64_ADD64] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_add64", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_X64_SUB32] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_sub32", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_X64_SUB64] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_sub64", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_X64_AND32] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_and32", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_X64_AND64] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_and64", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_X64_OR32] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_or32", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_X64_OR64] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_or64", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_X64_XOR32] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_xor32", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_X64_XOR64] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_xor64", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_X64_IMUL32] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_imul32", MACHINE_SCHEDULE_CLASS_MUL),
    [MACHINE_X64_IMUL64] = MACHINE_INFO_TWO_ADDRESS_SSA("x64_imul64", MACHINE_SCHEDULE_CLASS_MUL),
    [MACHINE_X64_NEG32] = MACHINE_INFO_UNARY_READ_MODIFY("x64_neg32"),
    [MACHINE_X64_NEG64] = MACHINE_INFO_UNARY_READ_MODIFY("x64_neg64"),
    [MACHINE_X64_NOT32] = MACHINE_INFO_UNARY_READ_MODIFY("x64_not32"),
    [MACHINE_X64_NOT64] = MACHINE_INFO_UNARY_READ_MODIFY("x64_not64"),
    [MACHINE_X64_CMP32] = MACHINE_INFO_COMPARE("x64_cmp32"),
    [MACHINE_X64_CMP64] = MACHINE_INFO_COMPARE("x64_cmp64"),
    [MACHINE_X64_TEST_RR] = MACHINE_INFO_COMPARE("x64_test_rr"),
    [MACHINE_X64_SETCC] = {
        .name = S8_INITIALIZER("x64_setcc"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_USE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .implicit_resource_uses = MACHINE_RESOURCE_FLAGS_MASK,
    },
    [MACHINE_X64_LOAD_FRAME] = {
        .name = S8_INITIALIZER("x64_load_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_MEMORY, .memory_effect = MACHINE_MEMORY_EFFECT_READ, .memory_operand = 2,
        .schedule_class = MACHINE_SCHEDULE_CLASS_LOAD,
    },
    [MACHINE_X64_STORE_FRAME8] = MACHINE_INFO_STORE_FRAME("x64_store_frame8"),
    [MACHINE_X64_STORE_FRAME16] = MACHINE_INFO_STORE_FRAME("x64_store_frame16"),
    [MACHINE_X64_STORE_FRAME32] = MACHINE_INFO_STORE_FRAME("x64_store_frame32"),
    [MACHINE_X64_STORE_FRAME64] = MACHINE_INFO_STORE_FRAME("x64_store_frame64"),
    [MACHINE_X64_LOAD_PTR8] = MACHINE_INFO_LOAD_POINTER("x64_load_ptr8"),
    [MACHINE_X64_LOAD_PTR16] = MACHINE_INFO_LOAD_POINTER("x64_load_ptr16"),
    [MACHINE_X64_LOAD_PTR32] = MACHINE_INFO_LOAD_POINTER("x64_load_ptr32"),
    [MACHINE_X64_LOAD_PTR64] = MACHINE_INFO_LOAD_POINTER("x64_load_ptr64"),
    [MACHINE_X64_STORE_PTR8] = MACHINE_INFO_STORE_POINTER("x64_store_ptr8"),
    [MACHINE_X64_STORE_PTR16] = MACHINE_INFO_STORE_POINTER("x64_store_ptr16"),
    [MACHINE_X64_STORE_PTR32] = MACHINE_INFO_STORE_POINTER("x64_store_ptr32"),
    [MACHINE_X64_STORE_PTR64] = MACHINE_INFO_STORE_POINTER("x64_store_ptr64"),
    [MACHINE_X64_JMP] = {
        .name = S8_INITIALIZER("x64_jmp"),
        .operand_count = 1,
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_X64_JCC] = {
        .name = S8_INITIALIZER("x64_jcc"),
        .operand_count = 2,
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR | MACHINE_OPCODE_ATTRIBUTE_FLAGS_USE,
        .implicit_resource_uses = MACHINE_RESOURCE_FLAGS_MASK,
    },
    [MACHINE_X64_RET] = {
        .name = S8_INITIALIZER("x64_ret"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_X64_SHL32] = MACHINE_INFO_SHIFT("x64_shl32"),
    [MACHINE_X64_SHL64] = MACHINE_INFO_SHIFT("x64_shl64"),
    [MACHINE_X64_SAR32] = MACHINE_INFO_SHIFT("x64_sar32"),
    [MACHINE_X64_SAR64] = MACHINE_INFO_SHIFT("x64_sar64"),
    [MACHINE_X64_SHR32] = MACHINE_INFO_SHIFT("x64_shr32"),
    [MACHINE_X64_SHR64] = MACHINE_INFO_SHIFT("x64_shr64"),
    [MACHINE_X64_SDIV32] = MACHINE_INFO_DIVIDE("x64_sdiv32"),
    [MACHINE_X64_SDIV64] = MACHINE_INFO_DIVIDE("x64_sdiv64"),
    [MACHINE_X64_UDIV32] = MACHINE_INFO_DIVIDE("x64_udiv32"),
    [MACHINE_X64_UDIV64] = MACHINE_INFO_DIVIDE("x64_udiv64"),
    [MACHINE_X64_SREM32] = MACHINE_INFO_DIVIDE("x64_srem32"),
    [MACHINE_X64_SREM64] = MACHINE_INFO_DIVIDE("x64_srem64"),
    [MACHINE_X64_UREM32] = MACHINE_INFO_DIVIDE("x64_urem32"),
    [MACHINE_X64_UREM64] = MACHINE_INFO_DIVIDE("x64_urem64"),
    [MACHINE_X64_LEA_FRAME] = {
        .name = S8_INITIALIZER("x64_lea_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    [MACHINE_X64_LEA_SYMBOL] = {
        .name = S8_INITIALIZER("x64_lea_symbol"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    [MACHINE_X64_LEA_TLS] = {
        .name = S8_INITIALIZER("x64_lea_tls"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    [MACHINE_X64_SWITCH] = {
        .name = S8_INITIALIZER("x64_switch"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR | MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .clobber_mask = 1u << MACHINE_X64_RCX,
    },
    [MACHINE_X64_COPY_FRAME_FROM_FRAME] = {
        .name = S8_INITIALIZER("x64_copy_frame_from_frame"),
        .operand_count = 2,
        .clobber_mask = 1u << MACHINE_X64_RAX,
    },
    [MACHINE_X64_COPY_FRAME_FROM_PTR] = {
        .name = S8_INITIALIZER("x64_copy_frame_from_ptr"),
        .operand_count = 2,
        .operand_info = {0, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .clobber_mask = 1u << MACHINE_X64_RAX,
    },
    [MACHINE_X64_COPY_PTR_FROM_FRAME] = {
        .name = S8_INITIALIZER("x64_copy_ptr_from_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .clobber_mask = (1u << MACHINE_X64_RAX) | (1u << MACHINE_X64_RDX),
    },
    [MACHINE_X64_FARITH] = {
        .name = S8_INITIALIZER("x64_farith"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .clobber_mask = MACHINE_X64_FLOAT_SCRATCH_CLOBBER,
    },
    [MACHINE_X64_FCMP_SET] = {
        .name = S8_INITIALIZER("x64_fcmp_set"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .implicit_resource_defs = MACHINE_RESOURCE_FLAGS_MASK,
        .clobber_mask = (1u << MACHINE_X64_RDX) | MACHINE_X64_FLOAT_SCRATCH_CLOBBER,
    },
    [MACHINE_X64_CVT_F32_TO_F64] = MACHINE_INFO_MOVE_CLOBBER("x64_cvt_f32_to_f64", MACHINE_X64_FLOAT_SCRATCH_CLOBBER),
    [MACHINE_X64_CVT_F64_TO_F32] = MACHINE_INFO_MOVE_CLOBBER("x64_cvt_f64_to_f32", MACHINE_X64_FLOAT_SCRATCH_CLOBBER),
    [MACHINE_X64_CVT_I64_TO_F32] = MACHINE_INFO_MOVE_CLOBBER("x64_cvt_i64_to_f32", MACHINE_X64_FLOAT_SCRATCH_CLOBBER),
    [MACHINE_X64_CVT_I64_TO_F64] = MACHINE_INFO_MOVE_CLOBBER("x64_cvt_i64_to_f64", MACHINE_X64_FLOAT_SCRATCH_CLOBBER),
    [MACHINE_X64_CVT_F32_TO_I64] = MACHINE_INFO_MOVE_CLOBBER("x64_cvt_f32_to_i64", MACHINE_X64_FLOAT_SCRATCH_CLOBBER),
    [MACHINE_X64_CVT_F64_TO_I64] = MACHINE_INFO_MOVE_CLOBBER("x64_cvt_f64_to_i64", MACHINE_X64_FLOAT_SCRATCH_CLOBBER),
    [MACHINE_X64_MOVQ_TO_XMM] = {
        .name = S8_INITIALIZER("x64_movq_to_xmm"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL},
        .clobber_mask = MACHINE_X64_FLOAT_BRIDGE_CLOBBER,
    },
    [MACHINE_X64_MOVQ_FROM_XMM] = {
        .name = S8_INITIALIZER("x64_movq_from_xmm"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
    },
    [MACHINE_X64_LOAD_INCOMING] = {
        .name = S8_INITIALIZER("x64_load_incoming"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
    },
    [MACHINE_X64_VA_SAVE] = {
        .name = S8_INITIALIZER("x64_va_save"),
        .operand_count = 1,
        // The operand is a frame slot, so no register class is attached.
        .operand_info = {0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
    },
    [MACHINE_X64_VA_ARG] = {
        .name = S8_INITIALIZER("x64_va_arg"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_DEFINE_GENERAL},
        // The row has a fixed scratch contract: slot 0 is RAX (the list
        // pointer), slot 1 is RCX for scalar results.  RDX/R8-R11 are
        // internal offset/data scratches and must be kept clear of live
        // virtual registers across the row.
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .clobber_mask = (1u << MACHINE_X64_RDX) | (1u << MACHINE_X64_R8) | (1u << MACHINE_X64_R9) |
                        (1u << MACHINE_X64_R10) | (1u << MACHINE_X64_R11),
    },
    [MACHINE_X64_PUSH_FRAME] = {
        .name = S8_INITIALIZER("x64_push_frame"),
        .operand_count = 1,
    },
    [MACHINE_X64_PUSH_REGISTER] = {
        .name = S8_INITIALIZER("x64_push_register"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL},
    },
    [MACHINE_X64_SUB_RSP] = {
        .name = S8_INITIALIZER("x64_sub_rsp"),
    },
    [MACHINE_X64_ADD_RSP] = {
        .name = S8_INITIALIZER("x64_add_rsp"),
    },
    [MACHINE_X64_STACK_ALLOCATE] = {
        .name = S8_INITIALIZER("x64_stack_allocate"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        // The count is consumed in RCX by the probe loop. The result itself
        // is the constrained RAX definition, so only RCX is an implicit
        // clobber beyond the declared operands.
        .clobber_mask = 1u << MACHINE_X64_RCX,
    },
    [MACHINE_X64_ATOMIC_STORE_XCHG] = {
        .name = S8_INITIALIZER("x64_atomic_store_xchg"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        // xchg writes the swapped-out memory word back into the value
        // register, so the operand's scratch does not hold the value
        // after the row — the atomics differential caught this as a real
        // miscompile once promotion made staged values outlive their row.
        .clobber_mask = 1u << MACHINE_X64_RCX,
    },
    [MACHINE_X64_ATOMIC_RMW] = {
        .name = S8_INITIALIZER("x64_atomic_rmw"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .clobber_mask = 1u << MACHINE_X64_R8,
    },
    [MACHINE_X64_ATOMIC_CMPXCHG] = {
        .name = S8_INITIALIZER("x64_atomic_cmpxchg"),
        .operand_count = 4,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
    },
    [MACHINE_X64_ATOMIC_CMPXCHG16] = {
        .name = S8_INITIALIZER("x64_atomic_cmpxchg16"),
        .operand_count = 4,
        // result, expected, and desired are stack-slot references.  Only the
        // address is a virtual register and it occupies slot 3, whose x86
        // scratch is RSI (outside CMPXCHG16B's implicit register quartet).
        .operand_info = {0, 0, 0, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .clobber_mask = (1ull << MACHINE_X64_RAX) | (1ull << MACHINE_X64_RDX) | (1ull << MACHINE_X64_RBX) | (1ull << MACHINE_X64_RCX),
    },
    [MACHINE_X64_MFENCE] = {
        .name = S8_INITIALIZER("x64_mfence"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_X64_INT3] = {
        .name = S8_INITIALIZER("x64_int3"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_X64_UD2] = {
        .name = S8_INITIALIZER("x64_ud2"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_X64_VMOV_RR] = {
        .name = S8_INITIALIZER("x64_vmov_rr"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_VECTOR, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VLOAD_FRAME] = {
        .name = S8_INITIALIZER("x64_vload_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_VECTOR, 0},
    },
    [MACHINE_X64_VSTORE_FRAME] = {
        .name = S8_INITIALIZER("x64_vstore_frame"),
        .operand_count = 2,
        .operand_info = {0, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VLOAD_PTR] = {
        .name = S8_INITIALIZER("x64_vload_ptr"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_VECTOR, MACHINE_OPERAND_USE_GENERAL},
    },
    [MACHINE_X64_VSTORE_PTR] = {
        .name = S8_INITIALIZER("x64_vstore_ptr"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VLOAD_PTR_MASKED] = {
        .name = S8_INITIALIZER("x64_vload_ptr_masked"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_VECTOR, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
    },
    [MACHINE_X64_VSTORE_PTR_MASKED] = {
        .name = S8_INITIALIZER("x64_vstore_ptr_masked"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VCOMPRESS_STORE_PTR] = {
        .name = S8_INITIALIZER("x64_vcompress_store_ptr"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VSPLATB] = {
        .name = S8_INITIALIZER("x64_vsplatb"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_VECTOR, MACHINE_OPERAND_USE_GENERAL},
    },
    [MACHINE_X64_VSPLATD] = {
        .name = S8_INITIALIZER("x64_vsplatd"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_VECTOR, MACHINE_OPERAND_USE_GENERAL},
    },
    [MACHINE_X64_VPCMP_MASK] = {
        .name = S8_INITIALIZER("x64_vpcmp_mask"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_VECTOR, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VPMOVB2M] = {
        .name = S8_INITIALIZER("x64_vpmovb2m"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VPERMT2B] = {
        .name = S8_INITIALIZER("x64_vpermt2b"),
        .operand_count = 4,
        .operand_info = {MACHINE_OPERAND_USE_DEFINE_VECTOR, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_VECTOR, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VCOMPRESSB] = {
        .name = S8_INITIALIZER("x64_vcompressb"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_VECTOR, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VPMOVZXBD] = {
        .name = S8_INITIALIZER("x64_vpmovzxbd"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_VECTOR, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VPSLLD_RI] = {
        .name = S8_INITIALIZER("x64_vpslld_ri"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_VECTOR, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VPTERNLOGD] = {
        .name = S8_INITIALIZER("x64_vpternlogd"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_USE_DEFINE_VECTOR, MACHINE_OPERAND_USE_VECTOR, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_VBINARY] = {
        .name = S8_INITIALIZER("x64_vbinary"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_VECTOR, MACHINE_OPERAND_USE_VECTOR, MACHINE_OPERAND_USE_VECTOR},
    },
    [MACHINE_X64_CALL_INDIRECT] = {
        .name = S8_INITIALIZER("x64_call_indirect"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CALL | MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_X64_CALL_DIRECT] = {
        .name = S8_INITIALIZER("x64_call_direct"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CALL | MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_A64_MOV_RI] = {
        .name = S8_INITIALIZER("a64_mov_ri"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    [MACHINE_A64_MOV_RR] = MACHINE_INFO_MOVE("a64_mov_rr"),
    [MACHINE_A64_MOV32_RR] = MACHINE_INFO_MOVE("a64_mov32_rr"),
    [MACHINE_A64_SXTB] = MACHINE_INFO_MOVE("a64_sxtb"),
    [MACHINE_A64_SXTH] = MACHINE_INFO_MOVE("a64_sxth"),
    [MACHINE_A64_SXTW] = MACHINE_INFO_MOVE("a64_sxtw"),
    [MACHINE_A64_UXTB] = MACHINE_INFO_MOVE("a64_uxtb"),
    [MACHINE_A64_UXTH] = MACHINE_INFO_MOVE("a64_uxth"),
    [MACHINE_A64_ADD32] = MACHINE_INFO_A64_THREE_ADDRESS("a64_add32", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_A64_ADD64] = MACHINE_INFO_A64_THREE_ADDRESS("a64_add64", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_A64_SUB32] = MACHINE_INFO_A64_THREE_ADDRESS("a64_sub32", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_A64_SUB64] = MACHINE_INFO_A64_THREE_ADDRESS("a64_sub64", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_A64_AND32] = MACHINE_INFO_A64_THREE_ADDRESS("a64_and32", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_A64_AND64] = MACHINE_INFO_A64_THREE_ADDRESS("a64_and64", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_A64_ORR32] = MACHINE_INFO_A64_THREE_ADDRESS("a64_orr32", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_A64_ORR64] = MACHINE_INFO_A64_THREE_ADDRESS("a64_orr64", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_A64_EOR32] = MACHINE_INFO_A64_THREE_ADDRESS("a64_eor32", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_A64_EOR64] = MACHINE_INFO_A64_THREE_ADDRESS("a64_eor64", MACHINE_SCHEDULE_CLASS_ALU),
    [MACHINE_A64_MUL32] = MACHINE_INFO_A64_THREE_ADDRESS("a64_mul32", MACHINE_SCHEDULE_CLASS_MUL),
    [MACHINE_A64_MUL64] = MACHINE_INFO_A64_THREE_ADDRESS("a64_mul64", MACHINE_SCHEDULE_CLASS_MUL),
    [MACHINE_A64_SDIV32] = MACHINE_INFO_A64_THREE_ADDRESS("a64_sdiv32", MACHINE_SCHEDULE_CLASS_DIV),
    [MACHINE_A64_SDIV64] = MACHINE_INFO_A64_THREE_ADDRESS("a64_sdiv64", MACHINE_SCHEDULE_CLASS_DIV),
    [MACHINE_A64_UDIV32] = MACHINE_INFO_A64_THREE_ADDRESS("a64_udiv32", MACHINE_SCHEDULE_CLASS_DIV),
    [MACHINE_A64_UDIV64] = MACHINE_INFO_A64_THREE_ADDRESS("a64_udiv64", MACHINE_SCHEDULE_CLASS_DIV),
    [MACHINE_A64_SREM32] = MACHINE_INFO_THREE_ADDRESS_CONSTRAINED("a64_srem32"),
    [MACHINE_A64_SREM64] = MACHINE_INFO_THREE_ADDRESS_CONSTRAINED("a64_srem64"),
    [MACHINE_A64_UREM32] = MACHINE_INFO_THREE_ADDRESS_CONSTRAINED("a64_urem32"),
    [MACHINE_A64_UREM64] = MACHINE_INFO_THREE_ADDRESS_CONSTRAINED("a64_urem64"),
    [MACHINE_A64_LSL32] = MACHINE_INFO_THREE_ADDRESS("a64_lsl32"),
    [MACHINE_A64_LSL64] = MACHINE_INFO_THREE_ADDRESS("a64_lsl64"),
    [MACHINE_A64_ASR32] = MACHINE_INFO_THREE_ADDRESS("a64_asr32"),
    [MACHINE_A64_ASR64] = MACHINE_INFO_THREE_ADDRESS("a64_asr64"),
    [MACHINE_A64_LSR32] = MACHINE_INFO_THREE_ADDRESS("a64_lsr32"),
    [MACHINE_A64_LSR64] = MACHINE_INFO_THREE_ADDRESS("a64_lsr64"),
    [MACHINE_A64_NEG32] = MACHINE_INFO_MOVE("a64_neg32"),
    [MACHINE_A64_NEG64] = MACHINE_INFO_MOVE("a64_neg64"),
    [MACHINE_A64_NOT32] = MACHINE_INFO_MOVE("a64_not32"),
    [MACHINE_A64_NOT64] = MACHINE_INFO_MOVE("a64_not64"),
    [MACHINE_A64_CMP32] = MACHINE_INFO_A64_COMPARE("a64_cmp32"),
    [MACHINE_A64_CMP64] = MACHINE_INFO_A64_COMPARE("a64_cmp64"),
    [MACHINE_A64_CMP_ZERO] = {
        .name = S8_INITIALIZER("a64_cmp_zero"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,
        .implicit_resource_defs = MACHINE_RESOURCE_NZCV_MASK,
    },
    [MACHINE_A64_CSET] = {
        .name = S8_INITIALIZER("a64_cset"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_USE,
        .implicit_resource_uses = MACHINE_RESOURCE_NZCV_MASK,
    },
    [MACHINE_A64_LOAD_FRAME] = {
        .name = S8_INITIALIZER("a64_load_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_MEMORY, .memory_effect = MACHINE_MEMORY_EFFECT_READ, .memory_operand = 2,
        .schedule_class = MACHINE_SCHEDULE_CLASS_LOAD,
    },
    [MACHINE_A64_LOAD_FRAME32] = {
        .name = S8_INITIALIZER("a64_load_frame32"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_MEMORY, .memory_effect = MACHINE_MEMORY_EFFECT_READ, .memory_operand = 2,
        .schedule_class = MACHINE_SCHEDULE_CLASS_LOAD,
    },
    [MACHINE_A64_STORE_FRAME8] = MACHINE_INFO_STORE_FRAME("a64_store_frame8"),
    [MACHINE_A64_STORE_FRAME16] = MACHINE_INFO_STORE_FRAME("a64_store_frame16"),
    [MACHINE_A64_STORE_FRAME32] = MACHINE_INFO_STORE_FRAME("a64_store_frame32"),
    [MACHINE_A64_STORE_FRAME64] = MACHINE_INFO_STORE_FRAME("a64_store_frame64"),
    [MACHINE_A64_LOAD_PTR8] = MACHINE_INFO_LOAD_POINTER("a64_load_ptr8"),
    [MACHINE_A64_LOAD_PTR16] = MACHINE_INFO_LOAD_POINTER("a64_load_ptr16"),
    [MACHINE_A64_LOAD_PTR32] = MACHINE_INFO_LOAD_POINTER("a64_load_ptr32"),
    [MACHINE_A64_LOAD_PTR64] = MACHINE_INFO_LOAD_POINTER("a64_load_ptr64"),
    [MACHINE_A64_STORE_PTR8] = MACHINE_INFO_STORE_POINTER("a64_store_ptr8"),
    [MACHINE_A64_STORE_PTR16] = MACHINE_INFO_STORE_POINTER("a64_store_ptr16"),
    [MACHINE_A64_STORE_PTR32] = MACHINE_INFO_STORE_POINTER("a64_store_ptr32"),
    [MACHINE_A64_STORE_PTR64] = MACHINE_INFO_STORE_POINTER("a64_store_ptr64"),
    [MACHINE_A64_LEA_FRAME] = {
        .name = S8_INITIALIZER("a64_lea_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    [MACHINE_A64_LEA_OFFSET] = MACHINE_INFO_MOVE("a64_lea_offset"),
    // The copies run through the reserved X17 data scratch, so unlike their
    // x86-64 counterparts they clobber no allocatable register.
    [MACHINE_A64_COPY_FRAME_FROM_FRAME] = {
        .name = S8_INITIALIZER("a64_copy_frame_from_frame"),
        .operand_count = 2,
    },
    [MACHINE_A64_COPY_FRAME_FROM_PTR] = {
        .name = S8_INITIALIZER("a64_copy_frame_from_ptr"),
        .operand_count = 2,
        .operand_info = {0, MACHINE_OPERAND_USE_GENERAL},
    },
    [MACHINE_A64_COPY_PTR_FROM_FRAME] = {
        .name = S8_INITIALIZER("a64_copy_ptr_from_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, 0},
    },
    [MACHINE_A64_B] = {
        .name = S8_INITIALIZER("a64_b"),
        .operand_count = 1,
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_A64_BCC] = {
        .name = S8_INITIALIZER("a64_bcc"),
        .operand_count = 2,
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR | MACHINE_OPCODE_ATTRIBUTE_FLAGS_USE,
        .implicit_resource_uses = MACHINE_RESOURCE_NZCV_MASK,
    },
    [MACHINE_A64_RET] = {
        .name = S8_INITIALIZER("a64_ret"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_A64_FMOV_TO_VEC] = {
        .name = S8_INITIALIZER("a64_fmov_to_vec"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL},
        // The declared bridge register: FAST forces the source operand into
        // X9 on the general path, exactly like the x86-64 RAX bridge — and
        // the clobber is also what keeps this row out of the simple-row
        // lane, whose free-register picks know nothing about the argument
        // registers an ABI staging sequence has already placed. Without it
        // a rematerialized float image could land on a staged X register
        // between the integer staging rows and the call.
        .clobber_mask = 1u << MACHINE_A64_X9,
    },
    [MACHINE_A64_FMOV_FROM_VEC] = {
        .name = S8_INITIALIZER("a64_fmov_from_vec"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
    },
    [MACHINE_A64_BRK] = {
        .name = S8_INITIALIZER("a64_brk"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_A64_UDF] = {
        .name = S8_INITIALIZER("a64_udf"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_A64_READ_SP] = {
        .name = S8_INITIALIZER("a64_read_sp"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
    },
    [MACHINE_A64_WRITE_SP] = {
        .name = S8_INITIALIZER("a64_write_sp"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_A64_CALL_DIRECT] = {
        .name = S8_INITIALIZER("a64_call_direct"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CALL | MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_A64_CALL_INDIRECT] = {
        .name = S8_INITIALIZER("a64_call_indirect"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CALL | MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_A64_LEA_SYMBOL] = {
        .name = S8_INITIALIZER("a64_lea_symbol"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    // The float rows ride V0/V1, which no allocatable register file
    // contains, so they carry no clobber mask; their ordering against the
    // ABI's FMOV staging comes from the scheduler's float-state chain.
    [MACHINE_A64_FARITH] = {
        .name = S8_INITIALIZER("a64_farith"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
    },
    [MACHINE_A64_FCMP_SET] = {
        .name = S8_INITIALIZER("a64_fcmp_set"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,
        .implicit_resource_defs = MACHINE_RESOURCE_NZCV_MASK,
    },
    [MACHINE_A64_CVT_F32_TO_F64] = MACHINE_INFO_MOVE("a64_cvt_f32_to_f64"),
    [MACHINE_A64_CVT_F64_TO_F32] = MACHINE_INFO_MOVE("a64_cvt_f64_to_f32"),
    [MACHINE_A64_CVT_I64_TO_F32] = MACHINE_INFO_MOVE("a64_cvt_i64_to_f32"),
    [MACHINE_A64_CVT_I64_TO_F64] = MACHINE_INFO_MOVE("a64_cvt_i64_to_f64"),
    [MACHINE_A64_CVT_F32_TO_I64] = MACHINE_INFO_MOVE("a64_cvt_f32_to_i64"),
    [MACHINE_A64_CVT_F64_TO_I64] = MACHINE_INFO_MOVE("a64_cvt_f64_to_i64"),
    [MACHINE_A64_CVT_U64_TO_F32] = MACHINE_INFO_MOVE("a64_cvt_u64_to_f32"),
    [MACHINE_A64_CVT_U64_TO_F64] = MACHINE_INFO_MOVE("a64_cvt_u64_to_f64"),
    [MACHINE_A64_CVT_F32_TO_U64] = MACHINE_INFO_MOVE("a64_cvt_f32_to_u64"),
    [MACHINE_A64_CVT_F64_TO_U64] = MACHINE_INFO_MOVE("a64_cvt_f64_to_u64"),
    [MACHINE_A64_LOAD_INCOMING] = {
        .name = S8_INITIALIZER("a64_load_incoming"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
    },
    [MACHINE_A64_VA_SAVE] = {
        .name = S8_INITIALIZER("a64_va_save"),
        .operand_count = 1,
        // The operand is a frame slot, so no register class is attached.
        // The row reads the still-live incoming X0-X7 and sits first in the
        // entry block, before any capture row can disturb them.
        .operand_info = {0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
    },
    [MACHINE_A64_VA_ARG] = {
        .name = S8_INITIALIZER("a64_va_arg"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_DEFINE_GENERAL},
        // The encoder's bounded sequence mirrors the canonical emitter's
        // register contract: the fixed operands are X10 (list pointer) and
        // X13 (scalar result); X9 carries part data and X11/X12 the
        // cursor and part address, so live values must vacate them.
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .clobber_mask = (1u << MACHINE_A64_X9) | (1u << MACHINE_A64_X11) | (1u << MACHINE_A64_X12),
        .fixed_register_mask = 0x3, .fixed_registers = {MACHINE_A64_X10, MACHINE_A64_X13},
    },
    // The V register is a fixed payload identity outside every allocatable
    // file, exactly like the FMOV rows; the slot operand carries no
    // register class. Ordering against V-register compute and staging
    // comes from the scheduler's float-state chain.
    [MACHINE_A64_VLOAD_FRAME] = {
        .name = S8_INITIALIZER("a64_vload_frame"),
        .operand_count = 1,
        .operand_info = {0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_MEMORY, .memory_effect = MACHINE_MEMORY_EFFECT_READ, .memory_operand = 1,
        .schedule_class = MACHINE_SCHEDULE_CLASS_LOAD,
    },
    [MACHINE_A64_VSTORE_FRAME] = {
        .name = S8_INITIALIZER("a64_vstore_frame"),
        .operand_count = 1,
        .operand_info = {0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_MEMORY, .memory_effect = MACHINE_MEMORY_EFFECT_WRITE, .memory_operand = 1,
    },
    // Pure V0/V1 compute between its chunk loads and store: no register
    // operands, no clobber mask (no allocatable file contains V0/V1), and
    // ordering comes from the scheduler's float-state chain.
    [MACHINE_A64_VARITH] = {
        .name = S8_INITIALIZER("a64_varith"),
        .operand_count = 0,
        .operand_info = {0},
    },
    // The atomic rows follow the x86-64 precedent: SIDE_EFFECTS makes
    // every one a scheduler barrier, which is at least as strong as the
    // ordering the memory-order operand asks for.
    [MACHINE_A64_ATOMIC_LOAD] = {
        .name = S8_INITIALIZER("a64_atomic_load"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_A64_ATOMIC_STORE] = {
        .name = S8_INITIALIZER("a64_atomic_store"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    // The exclusive loops run on the canonical emitter's fixed register
    // palette: X9 old value, X10 address, X11 operand/desired, X12
    // scratch/expected, X13 status — the same registers VA_ARG already
    // reserves through its own row contract.
    [MACHINE_A64_ATOMIC_RMW] = {
        .name = S8_INITIALIZER("a64_atomic_rmw"),
        .operand_count = 3,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .clobber_mask = (1u << MACHINE_A64_X12) | (1u << MACHINE_A64_X13),
        .fixed_register_mask = 0x7, .fixed_registers = {MACHINE_A64_X9, MACHINE_A64_X10, MACHINE_A64_X11},
    },
    [MACHINE_A64_ATOMIC_CAS] = {
        .name = S8_INITIALIZER("a64_atomic_cas"),
        .operand_count = 4,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED | MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,
        .implicit_resource_defs = MACHINE_RESOURCE_NZCV_MASK,
        .clobber_mask = 1u << MACHINE_A64_X13,
        .fixed_register_mask = 0xf, .fixed_registers = {MACHINE_A64_X9, MACHINE_A64_X10, MACHINE_A64_X12, MACHINE_A64_X11},
    },
    [MACHINE_A64_ATOMIC_FENCE] = {
        .name = S8_INITIALIZER("a64_atomic_fence"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_A64_LEA_TLS] = {
        .name = S8_INITIALIZER("a64_lea_tls"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_REMATERIALIZABLE,
    },
    // The canonical page-probe loop runs on X9/X10; the size use in X9 is
    // destroyed by the loop's countdown, which the clobber declares — the
    // x86-64 xchg lesson.
    [MACHINE_A64_STACK_ALLOCATE] = {
        .name = S8_INITIALIZER("a64_stack_allocate"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .implicit_resource_defs = MACHINE_RESOURCE_NZCV_MASK,
        .clobber_mask = 1u << MACHINE_A64_X9,
        .fixed_register_mask = 0x3, .fixed_registers = {MACHINE_A64_X10, MACHINE_A64_X9},
    },
    // The compare chain's case scratch is reserved X17, outside every
    // allocatable file, so no clobber mask is needed.
    [MACHINE_A64_SWITCH] = {
        .name = S8_INITIALIZER("a64_switch"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL, 0},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR | MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,
        .implicit_resource_defs = MACHINE_RESOURCE_NZCV_MASK,
    },
};

// Every opcode receives a recipe identity independent of the metadata row's
// operand and hazard fields.  The low index is target-local; values are kept
// target-local within each category so a future generated projection can
// replace this audit table without changing MachineOpcode identities.
BUSTER_CT_CHECK(MACHINE_X64_VBINARY - MACHINE_X64_MOV_RI + 1 == MACHINE_X86_64_EMIT_REGISTRY_COUNT);

BUSTER_GLOBAL_LOCAL MachineX64EmitRegistryEntry const machine_x86_64_emit_registry[MACHINE_X86_64_EMIT_REGISTRY_COUNT] = {
#define MACHINE_X64_REGISTRY_ROW(opcode_value, category_value, index_value, status_value) \
    [opcode_value - MACHINE_X64_MOV_RI] = { \
        .opcode = opcode_value, \
        .recipe = (MachineEmitRecipeId)(MACHINE_EMIT_RECIPE_##category_value##_BASE + index_value), \
        .producer_ordinal = (u16)(opcode_value - MACHINE_X64_MOV_RI), \
        .producer_status = MACHINE_X64_EMIT_PRODUCER_STATUS_##status_value, \
    },
    MACHINE_X86_64_EMIT_REGISTRY(MACHINE_X64_REGISTRY_ROW)
#undef MACHINE_X64_REGISTRY_ROW
};

BUSTER_CT_CHECK(MACHINE_X86_64_CANONICAL_AUTHORITY_SITE_COUNT == 5u);
BUSTER_CT_CHECK(MACHINE_X86_64_NEUTRAL_PATCH_SITE_COUNT == 14u);

// Canonical x86 authority records.  These rows name only the metadata module
// entry points that own instruction bytes.  Codegen, assembly, JIT, and link
// callers are consumers of this authority, never independent authorities.
BUSTER_GLOBAL_LOCAL MachineX64CanonicalAuthoritySite const
    machine_x86_64_canonical_authority_sites[MACHINE_X86_64_CANONICAL_AUTHORITY_SITE_COUNT] = {
    {
        .authority_kind = MACHINE_X64_CANONICAL_AUTHORITY_METADATA_CHECKED,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/assembly/x86_64_metadata.c"),
        .owner_symbol = S8_INITIALIZER("buster_x86_metadata_encode"),
    },
    {
        .authority_kind = MACHINE_X64_CANONICAL_AUTHORITY_METADATA_CHECKED,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/assembly/x86_64_metadata.c"),
        .owner_symbol = S8_INITIALIZER("buster_x86_metadata_emit_form"),
    },
    {
        .authority_kind = MACHINE_X64_CANONICAL_AUTHORITY_METADATA_EXACT,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/assembly/x86_64_metadata.c"),
        .owner_symbol = S8_INITIALIZER("buster_x86_metadata_emit_form_exact"),
    },
    {
        .authority_kind = MACHINE_X64_CANONICAL_AUTHORITY_METADATA_EXACT,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/assembly/x86_64_metadata.c"),
        .owner_symbol = S8_INITIALIZER("buster_x86_metadata_emit_exact_query"),
    },
    {
        .authority_kind = MACHINE_X64_CANONICAL_AUTHORITY_METADATA_EXACT,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/assembly/x86_64_metadata.c"),
        .owner_symbol = S8_INITIALIZER("buster_x86_metadata_emit_exact_machine"),
    },
};

// Neutral patch records are intentionally not instruction authorities.  They
// may write relocation fields, displacements, target payloads, or object/data
// records after metadata has emitted the instruction bytes.  Keep these rows
// explicit so a new byte write cannot hide a second encoder.
BUSTER_GLOBAL_LOCAL MachineX64NeutralPatchSite const machine_x86_64_neutral_patch_sites[MACHINE_X86_64_NEUTRAL_PATCH_SITE_COUNT] = {
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_DATA,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/codegen/codegen.c"),
        .owner_symbol = S8_INITIALIZER("codegen_emit_global_assembly"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_DISPLACEMENT,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/codegen/codegen.c"),
        .owner_symbol = S8_INITIALIZER("codegen_generate_canonical_module_attempt"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_RELOCATION,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/assembly/assembly.c"),
        .owner_symbol = S8_INITIALIZER("assembly_x86_metadata_local_relocation"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_DISPLACEMENT,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/link/link.c"),
        .owner_symbol = S8_INITIALIZER("link_address_difference"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_DATA,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/link/link.c"),
        .owner_symbol = S8_INITIALIZER("link_write_u16"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_DATA,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/link/link.c"),
        .owner_symbol = S8_INITIALIZER("link_write_u32"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_DATA,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/link/link.c"),
        .owner_symbol = S8_INITIALIZER("link_write_u32_be"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_DATA,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/link/link.c"),
        .owner_symbol = S8_INITIALIZER("link_write_u64"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_DISPLACEMENT,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/link/link.c"),
        .owner_symbol = S8_INITIALIZER("link_native_executable_elf64_x86_64"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_DISPLACEMENT,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/link/link.c"),
        .owner_symbol = S8_INITIALIZER("link_native_executable_elf64_x86_64_dynamic"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_DISPLACEMENT,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/link/link.c"),
        .owner_symbol = S8_INITIALIZER("link_native_executable_mach_o64"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_TARGET_PAYLOAD,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/jit/jit.c"),
        .owner_symbol = S8_INITIALIZER("jit_emit_thunks"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_RELOCATION,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/jit/jit.c"),
        .owner_symbol = S8_INITIALIZER("jit_apply_relocations"),
    },
    {
        .patch_class = MACHINE_X64_NEUTRAL_PATCH_RELOCATION,
        .source_file = S8_INITIALIZER("src/buster/lib/compiler/jit/jit.c"),
        .owner_symbol = S8_INITIALIZER("jit_apply_aarch64_mach_page_relocation"),
    },
};

BUSTER_GLOBAL_LOCAL MachineEmitRecipeId const machine_opcode_emit_recipes[MACHINE_OPCODE_COUNT] = {
    [MACHINE_OPCODE_INVALID] = MACHINE_EMIT_RECIPE_NONE,
    [MACHINE_OPCODE_SKELETON_NOP] = MACHINE_EMIT_RECIPE_NONE,
    [MACHINE_OPCODE_SKELETON_COPY] = MACHINE_EMIT_RECIPE_NONE,
    [MACHINE_OPCODE_SKELETON_RETURN] = MACHINE_EMIT_RECIPE_NONE,
#define MACHINE_X64_RECIPE_ROW(opcode, category, index, status) [opcode] = MACHINE_EMIT_RECIPE_##category##_BASE + index,
    MACHINE_X86_64_EMIT_REGISTRY(MACHINE_X64_RECIPE_ROW)
#undef MACHINE_X64_RECIPE_ROW
    [MACHINE_A64_MOV_RR] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 0,
    [MACHINE_A64_MOV32_RR] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 1,
    [MACHINE_A64_SXTB] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 2,
    [MACHINE_A64_SXTH] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 3,
    [MACHINE_A64_SXTW] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 4,
    [MACHINE_A64_UXTB] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 5,
    [MACHINE_A64_UXTH] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 6,
    [MACHINE_A64_ADD32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 7,
    [MACHINE_A64_ADD64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 8,
    [MACHINE_A64_SUB32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 9,
    [MACHINE_A64_SUB64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 10,
    [MACHINE_A64_AND32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 11,
    [MACHINE_A64_AND64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 12,
    [MACHINE_A64_ORR32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 13,
    [MACHINE_A64_ORR64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 14,
    [MACHINE_A64_EOR32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 15,
    [MACHINE_A64_EOR64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 16,
    [MACHINE_A64_MUL32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 17,
    [MACHINE_A64_MUL64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 18,
    [MACHINE_A64_SDIV32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 19,
    [MACHINE_A64_SDIV64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 20,
    [MACHINE_A64_UDIV32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 21,
    [MACHINE_A64_UDIV64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 22,
    [MACHINE_A64_LSL32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 23,
    [MACHINE_A64_LSL64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 24,
    [MACHINE_A64_ASR32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 25,
    [MACHINE_A64_ASR64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 26,
    [MACHINE_A64_LSR32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 27,
    [MACHINE_A64_NEG32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 28,
    [MACHINE_A64_NEG64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 29,
    [MACHINE_A64_NOT32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 30,
    [MACHINE_A64_NOT64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 31,
    [MACHINE_A64_CMP32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 32,
    [MACHINE_A64_CMP64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 33,
    [MACHINE_A64_CMP_ZERO] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 34,
    [MACHINE_A64_CSET] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 35,
    [MACHINE_A64_LOAD_FRAME] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 36,
    [MACHINE_A64_LOAD_FRAME32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 37,
    [MACHINE_A64_STORE_FRAME8] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 38,
    [MACHINE_A64_STORE_FRAME16] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 39,
    [MACHINE_A64_STORE_FRAME32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 40,
    [MACHINE_A64_STORE_FRAME64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 41,
    [MACHINE_A64_LOAD_PTR8] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 42,
    [MACHINE_A64_LOAD_PTR16] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 43,
    [MACHINE_A64_LOAD_PTR32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 44,
    [MACHINE_A64_LOAD_PTR64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 45,
    [MACHINE_A64_STORE_PTR8] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 46,
    [MACHINE_A64_STORE_PTR16] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 47,
    [MACHINE_A64_STORE_PTR32] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 48,
    [MACHINE_A64_STORE_PTR64] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 49,
    [MACHINE_A64_B] = MACHINE_EMIT_RECIPE_DIRECT_BASE + 50,
    [MACHINE_A64_LEA_OFFSET] = MACHINE_EMIT_RECIPE_FAMILY_BASE + 0,
    [MACHINE_A64_READ_SP] = MACHINE_EMIT_RECIPE_FAMILY_BASE + 1,
    [MACHINE_A64_WRITE_SP] = MACHINE_EMIT_RECIPE_FAMILY_BASE + 2,
    [MACHINE_A64_MOV_RI] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 0,
    [MACHINE_A64_SREM32] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 1,
    [MACHINE_A64_SREM64] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 2,
    [MACHINE_A64_UREM32] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 3,
    [MACHINE_A64_UREM64] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 4,
    [MACHINE_A64_LEA_FRAME] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 5,
    [MACHINE_A64_COPY_FRAME_FROM_FRAME] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 6,
    [MACHINE_A64_COPY_FRAME_FROM_PTR] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 7,
    [MACHINE_A64_COPY_PTR_FROM_FRAME] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 8,
    [MACHINE_A64_BCC] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 9,
    [MACHINE_A64_RET] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 10,
    [MACHINE_A64_FMOV_TO_VEC] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 11,
    [MACHINE_A64_FMOV_FROM_VEC] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 12,
    [MACHINE_A64_BRK] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 13,
    [MACHINE_A64_UDF] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 14,
    [MACHINE_A64_CALL_DIRECT] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 15,
    [MACHINE_A64_CALL_INDIRECT] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 16,
    [MACHINE_A64_LEA_SYMBOL] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 17,
    [MACHINE_A64_LSR64] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 18,
    [MACHINE_A64_FARITH] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 19,
    [MACHINE_A64_FCMP_SET] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 20,
    [MACHINE_A64_CVT_F32_TO_F64] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 21,
    [MACHINE_A64_CVT_F64_TO_F32] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 22,
    [MACHINE_A64_CVT_I64_TO_F32] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 23,
    [MACHINE_A64_CVT_I64_TO_F64] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 24,
    [MACHINE_A64_CVT_F32_TO_I64] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 25,
    [MACHINE_A64_CVT_F64_TO_I64] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 26,
    [MACHINE_A64_CVT_U64_TO_F32] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 27,
    [MACHINE_A64_CVT_U64_TO_F64] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 28,
    [MACHINE_A64_CVT_F32_TO_U64] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 29,
    [MACHINE_A64_CVT_F64_TO_U64] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 30,
    [MACHINE_A64_LOAD_INCOMING] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 31,
    [MACHINE_A64_VA_SAVE] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 32,
    [MACHINE_A64_VA_ARG] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 33,
    [MACHINE_A64_VLOAD_FRAME] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 34,
    [MACHINE_A64_VSTORE_FRAME] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 35,
    [MACHINE_A64_VARITH] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 36,
    [MACHINE_A64_ATOMIC_LOAD] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 37,
    [MACHINE_A64_ATOMIC_STORE] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 38,
    [MACHINE_A64_ATOMIC_RMW] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 39,
    [MACHINE_A64_ATOMIC_CAS] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 40,
    [MACHINE_A64_ATOMIC_FENCE] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 41,
    [MACHINE_A64_LEA_TLS] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 42,
    [MACHINE_A64_STACK_ALLOCATE] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 43,
    [MACHINE_A64_SWITCH] = MACHINE_EMIT_RECIPE_EXPANSION_BASE + 44,
};

MachineOpcodeInfo const* machine_opcode_info(u16 opcode)
{
    MachineOpcodeInfo const* result;
    if (opcode >= MACHINE_OPCODE_COUNT)
    {
        result = 0;
    }
    else
    {
        result = machine_opcode_infos + opcode;
    }

    return result;
}

MachineEmitRecipeId machine_opcode_emit_recipe(u16 opcode)
{
    return opcode < MACHINE_OPCODE_COUNT ? machine_opcode_emit_recipes[opcode] : MACHINE_EMIT_RECIPE_INVALID;
}

u32 machine_x86_64_emit_registry_count(void)
{
    return MACHINE_X86_64_EMIT_REGISTRY_COUNT;
}

MachineX64EmitRegistryEntry const* machine_x86_64_emit_registry_entry(u32 ordinal)
{
    return ordinal < MACHINE_X86_64_EMIT_REGISTRY_COUNT ? machine_x86_64_emit_registry + ordinal : 0;
}

MachineX64EmitRegistryEntry const* machine_x86_64_emit_registry_find(MachineOpcode opcode)
{
    MachineX64EmitRegistryEntry const* result;
    if (opcode < MACHINE_X64_MOV_RI || opcode > MACHINE_X64_VBINARY)
    {
        result = 0;
    }
    else
    {
        MachineX64EmitRegistryEntry const* entry = machine_x86_64_emit_registry + (opcode - MACHINE_X64_MOV_RI);
        result = entry->opcode == opcode ? entry : 0;
    }

    return result;
}

u32 machine_x86_64_canonical_authority_site_count(void)
{
    return MACHINE_X86_64_CANONICAL_AUTHORITY_SITE_COUNT;
}

MachineX64CanonicalAuthoritySite const* machine_x86_64_canonical_authority_site(u32 ordinal)
{
    return ordinal < MACHINE_X86_64_CANONICAL_AUTHORITY_SITE_COUNT ? machine_x86_64_canonical_authority_sites + ordinal : 0;
}

u32 machine_x86_64_neutral_patch_site_count(void)
{
    return MACHINE_X86_64_NEUTRAL_PATCH_SITE_COUNT;
}

MachineX64NeutralPatchSite const* machine_x86_64_neutral_patch_site(u32 ordinal)
{
    return ordinal < MACHINE_X86_64_NEUTRAL_PATCH_SITE_COUNT ? machine_x86_64_neutral_patch_sites + ordinal : 0;
}

u16 machine_opcode_form_set(MachineOpcodeInfo const* info)
{
    u16 result;
    if (!info)
    {
        result = 0;
    }
    else
    {
        result = info->form_set;
    }

    return result;
}

MachineScheduleClass machine_opcode_schedule_class(MachineOpcodeInfo const* info)
{
    return info && info->schedule_class < MACHINE_SCHEDULE_CLASS_COUNT ? (MachineScheduleClass)info->schedule_class : MACHINE_SCHEDULE_CLASS_NONE;
}

MachineOpcodeExpansion machine_opcode_expansion(MachineOpcodeInfo const* info)
{
    return info && info->expansion_recipe < MACHINE_OPCODE_EXPANSION_COUNT ? (MachineOpcodeExpansion)info->expansion_recipe : MACHINE_OPCODE_EXPANSION_NONE;
}

MachineMemoryEffect machine_opcode_memory_effect(MachineOpcodeInfo const* info)
{
    return info && info->memory_effect < MACHINE_MEMORY_EFFECT_COUNT ? (MachineMemoryEffect)info->memory_effect : MACHINE_MEMORY_EFFECT_NONE;
}

MachineBundleKind machine_opcode_bundle(MachineOpcodeInfo const* info)
{
    return info && info->bundle < MACHINE_BUNDLE_COUNT ? (MachineBundleKind)info->bundle : MACHINE_BUNDLE_NONE;
}

bool machine_opcode_is_memory(MachineOpcodeInfo const* info)
{
    return machine_opcode_memory_effect(info) != MACHINE_MEMORY_EFFECT_NONE || (info && (info->attributes & MACHINE_OPCODE_ATTRIBUTE_MEMORY));
}

u32 machine_opcode_fixed_register(MachineOpcodeInfo const* info, u32 slot)
{
    u32 result;
    if (!info || slot >= BUSTER_ARRAY_LENGTH(info->fixed_registers) || !(info->fixed_register_mask & (1u << slot)))
    {
        result = UINT32_MAX;
    }
    else
    {
        result = info->fixed_registers[slot];
    }

    return result;
}

u32 machine_opcode_memory_operand(MachineOpcodeInfo const* info)
{
    u32 result;
    if (!info || !info->memory_operand)
    {
        result = UINT32_MAX;
    }
    else
    {
        u32 slot = (u32)info->memory_operand - 1u;
        result = slot < info->operand_count ? slot : UINT32_MAX;
    }

    return result;
}

bool machine_opcode_operand_is_tied(MachineOpcodeInfo const* info, u32 destination_slot, u32 source_slot)
{
    bool result;
    if (!info || destination_slot >= 4 || source_slot >= 4)
    {
        result = false;
    }
    else
    {
        u32 encoded_destination = info->tied_pair & 0x0fu;
        u32 encoded_source = (info->tied_pair >> 4) & 0x0fu;
        result = encoded_destination != 0 && encoded_source != 0 && encoded_destination - 1u == destination_slot && encoded_source - 1u == source_slot;
    }

    return result;
}

bool machine_opcode_operand_is_early_clobber(MachineOpcodeInfo const* info, u32 slot)
{
    return info && slot < 8 && (info->early_clobber_mask & (1u << slot)) != 0;
}

bool machine_opcode_has_constraints(MachineOpcodeInfo const* info)
{
    return info && (info->tied_pair || info->early_clobber_mask || info->fixed_register_set || info->fixed_register_mask ||
                    (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED));
}

void machine_stream_initialize(MachineBuilderStream* stream, u64 element_size)
{
    stream->first = 0;
    stream->last = 0;
    stream->total_count = 0;
    stream->element_size = (u32)element_size;
    stream->chunk_capacity = (u32)((MACHINE_BUILDER_CHUNK_BYTES - sizeof(MachineBuilderChunk)) / element_size);
    stream->reserved = 0;
}

void* machine_stream_append(Arena* arena, MachineBuilderStream* stream)
{
    MachineBuilderChunk* chunk = stream->last;
    if (!chunk || chunk->count == stream->chunk_capacity)
    {
        chunk = (MachineBuilderChunk*)arena_allocate_bytes(arena, sizeof(MachineBuilderChunk) + (u64)stream->chunk_capacity * stream->element_size,
                                                           BUSTER_ALIGN_OF(MachineBuilderChunk));
        chunk->next = 0;
        chunk->count = 0;
        chunk->reserved = 0;
        if (stream->last)
        {
            stream->last->next = chunk;
        }
        else
        {
            stream->first = chunk;
        }
        stream->last = chunk;
    }
    void* row = (u8*)(chunk + 1) + (u64)chunk->count * stream->element_size;
    chunk->count += 1;
    stream->total_count += 1;
    return row;
}

void machine_stream_flatten(MachineBuilderStream* stream, void* destination)
{
    u64 offset = 0;
    for (MachineBuilderChunk* chunk = stream->first; chunk; chunk = chunk->next)
    {
        u64 chunk_bytes = (u64)chunk->count * stream->element_size;
        memcpy((u8*)destination + offset, chunk + 1, chunk_bytes);
        offset += chunk_bytes;
    }
}

MachineFunctionBuilder machine_function_builder_begin(Arena* arena)
{
    MachineFunctionBuilder builder = {
        .arena = arena,
        .open_block = UINT32_MAX,
    };
    machine_stream_initialize(&builder.instructions, sizeof(MachineInstruction));
    machine_stream_initialize(&builder.virtual_registers, sizeof(MachineVirtualRegister));
    machine_stream_initialize(&builder.blocks, sizeof(MachineBlock));
    machine_stream_initialize(&builder.edges, sizeof(MachineEdge));
    machine_stream_initialize(&builder.block_parameters, sizeof(MachineBlockParameter));
    machine_stream_initialize(&builder.edge_copy_sources, sizeof(MachineRef));
    return builder;
}

u32 machine_builder_virtual_register(MachineFunctionBuilder* builder, MachineVirtualRegister virtual_register)
{
    u32 index = builder->virtual_registers.total_count;
    MachineVirtualRegister* row = (MachineVirtualRegister*)machine_stream_append(builder->arena, &builder->virtual_registers);
    *row = virtual_register;
    return index;
}

u32 machine_builder_block_begin(MachineFunctionBuilder* builder)
{
    BUSTER_CHECK(!builder->block_is_open);
    builder->block_is_open = true;
    builder->open_block = builder->blocks.total_count;
    builder->open_block_first_instruction = builder->instructions.total_count;
    return builder->open_block;
}

u32 machine_builder_instruction(MachineFunctionBuilder* builder, MachineInstruction instruction)
{
    BUSTER_CHECK(builder->block_is_open);
    u32 index = builder->instructions.total_count;
    if (index >= MACHINE_POINT_INSTRUCTION_LIMIT)
    {
        builder->point_capacity_exceeded = true;
    }
    MachineInstruction* row = builder->instruction_cursor;
    if (row == builder->instruction_end)
    {
        row = (MachineInstruction*)machine_stream_append(builder->arena, &builder->instructions);
        builder->instruction_cursor = row + 1;
        builder->instruction_end = (MachineInstruction*)(builder->instructions.last + 1) + builder->instructions.chunk_capacity;
    }
    else
    {
        builder->instruction_cursor = row + 1;
        builder->instructions.last->count += 1;
        builder->instructions.total_count += 1;
    }
    *row = instruction;
    return index;
}

void machine_builder_block_end(MachineFunctionBuilder* builder, MachineBlock block)
{
    BUSTER_CHECK(builder->block_is_open);
    block.first_instruction = builder->open_block_first_instruction;
    block.instruction_count = builder->instructions.total_count - builder->open_block_first_instruction;
    MachineBlock* row = (MachineBlock*)machine_stream_append(builder->arena, &builder->blocks);
    *row = block;
    builder->block_is_open = false;
    builder->open_block = UINT32_MAX;
}

u32 machine_builder_block_parameter(MachineFunctionBuilder* builder, MachineBlockParameter parameter)
{
    u32 index = builder->block_parameters.total_count;
    MachineBlockParameter* row = (MachineBlockParameter*)machine_stream_append(builder->arena, &builder->block_parameters);
    *row = parameter;
    return index;
}

u32 machine_builder_edge_copy_source(MachineFunctionBuilder* builder, MachineRef source)
{
    u32 index = builder->edge_copy_sources.total_count;
    MachineRef* row = (MachineRef*)machine_stream_append(builder->arena, &builder->edge_copy_sources);
    *row = source;
    return index;
}

u32 machine_builder_edge(MachineFunctionBuilder* builder, MachineEdge edge)
{
    u32 index = builder->edges.total_count;
    MachineEdge* row = (MachineEdge*)machine_stream_append(builder->arena, &builder->edges);
    *row = edge;
    return index;
}

MachineFunction machine_function_builder_finish(Arena* arena, MachineFunctionBuilder* builder)
{
    BUSTER_CHECK(!builder->block_is_open);
    BUSTER_CHECK(builder->edges.total_count == 0 || builder->edges.total_count <= MACHINE_REF_PAYLOAD_LIMIT);
    BUSTER_CHECK(builder->block_parameters.total_count == 0 || builder->block_parameters.total_count <= MACHINE_REF_PAYLOAD_LIMIT);
    BUSTER_CHECK(builder->edge_copy_sources.total_count == 0 || builder->edge_copy_sources.total_count <= MACHINE_REF_PAYLOAD_LIMIT);
    MachineFunction function = {
        .instructions = arena_allocate(arena, MachineInstruction, builder->instructions.total_count),
        .virtual_registers = arena_allocate(arena, MachineVirtualRegister, builder->virtual_registers.total_count),
        .blocks = arena_allocate(arena, MachineBlock, builder->blocks.total_count),
        .edges = arena_allocate(arena, MachineEdge, builder->edges.total_count),
        .block_parameters = arena_allocate(arena, MachineBlockParameter, builder->block_parameters.total_count),
        .edge_copy_sources = arena_allocate(arena, MachineRef, builder->edge_copy_sources.total_count),
        .instruction_count = builder->instructions.total_count,
        .virtual_register_count = builder->virtual_registers.total_count,
        .block_count = builder->blocks.total_count,
        .edge_count = builder->edges.total_count,
        .block_parameter_count = builder->block_parameters.total_count,
        .edge_copy_source_count = builder->edge_copy_sources.total_count,
    };
    machine_stream_flatten(&builder->instructions, function.instructions);
    machine_stream_flatten(&builder->virtual_registers, function.virtual_registers);
    machine_stream_flatten(&builder->blocks, function.blocks);
    machine_stream_flatten(&builder->edges, function.edges);
    machine_stream_flatten(&builder->block_parameters, function.block_parameters);
    machine_stream_flatten(&builder->edge_copy_sources, function.edge_copy_sources);
    return function;
}

// Loop-depth frequency classes from the finished block structure. A
// backward block reference — a block-ref operand or a switch-case target
// naming a block at or before its own — approximates a loop over the block
// range [target, source]. Per head only the widest span counts, so a loop
// with several latches (a continue beside the bottom branch) stays one
// loop, and a block's class is the number of spans covering it — the
// nesting depth for the reducible shapes structured lowering emits.
// Sibling spans sharing no blocks never stack, and the cap keeps a
// pathological nest inside the u16 the block row carries.
#define MACHINE_FREQUENCY_CLASS_LIMIT 8u

void machine_function_stamp_frequency_classes(MachineFunction* function)
{
    u32 block_count = function->block_count;
    if (block_count)
    {
        TemporalArena scratch = scratch_begin(0, 0);
        // Widest backward span per head block, or UINT32_MAX for no span.
        u32* head_ends = arena_allocate(scratch.arena, u32, block_count);
        for (u32 block_index = 0; block_index < block_count; block_index += 1)
        {
            head_ends[block_index] = UINT32_MAX;
        }
        for (u32 block_index = 0; block_index < block_count; block_index += 1)
        {
            MachineBlock* block = function->blocks + block_index;
            for (u32 offset = 0; offset < block->instruction_count; offset += 1)
            {
                MachineInstruction* instruction = function->instructions + block->first_instruction + offset;
                MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                if (!info)
                {
                    scratch_end(scratch);
                    return;
                }
                for (u32 slot = 0; slot < info->operand_count; slot += 1)
                {
                    if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_BLOCK ||
                        machine_ref_payload(instruction->operands[slot]) > block_index)
                    {
                        continue;
                    }
                    u32 head = machine_ref_payload(instruction->operands[slot]);
                    head_ends[head] = head_ends[head] == UINT32_MAX ? block_index : BUSTER_MAX(head_ends[head], block_index);
                }
                if (function->target && instruction->opcode == function->target->switch_opcode)
                {
                    for (u32 case_index = 0; case_index < instruction->flags; case_index += 1)
                    {
                        u32 case_target = function->switch_cases[instruction->payload + case_index].target_block;
                        if (case_target > block_index)
                        {
                            continue;
                        }
                        head_ends[case_target] = head_ends[case_target] == UINT32_MAX ? block_index : BUSTER_MAX(head_ends[case_target], block_index);
                    }
                }
            }
        }
        s32* depth_deltas = arena_allocate(scratch.arena, s32, block_count + 1);
        for (u32 block_index = 0; block_index <= block_count; block_index += 1)
        {
            depth_deltas[block_index] = 0;
        }
        for (u32 head = 0; head < block_count; head += 1)
        {
            if (head_ends[head] == UINT32_MAX)
            {
                continue;
            }
            depth_deltas[head] += 1;
            depth_deltas[head_ends[head] + 1] -= 1;
        }
        s32 depth = 0;
        for (u32 block_index = 0; block_index < block_count; block_index += 1)
        {
            depth += depth_deltas[block_index];
            function->blocks[block_index].frequency_class = (u16)BUSTER_MIN((u32)depth, MACHINE_FREQUENCY_CLASS_LIMIT);
        }
        scratch_end(scratch);
    }
}

BUSTER_GLOBAL_LOCAL bool machine_verify_reference(MachineFunction* function, MachineRef ref)
{
    MachineRefKind kind = machine_ref_kind(ref);
    u32 payload = machine_ref_payload(ref);
    switch (kind)
    {
        case MACHINE_REF_NONE:
            return payload == 0;
        case MACHINE_REF_VIRTUAL_REGISTER:
            return payload < function->virtual_register_count;
        case MACHINE_REF_BLOCK:
            return payload < function->block_count;
        case MACHINE_REF_IMMEDIATE:
            return payload < function->immediate_count;
        case MACHINE_REF_STACK_SLOT:
            return payload < function->stack_slot_count;
        case MACHINE_REF_PHYSICAL_REGISTER:
        case MACHINE_REF_ADDRESS:
        case MACHINE_REF_EXTRA:
            // The address and overflow side tables arrive with later stages;
            // physical-register payloads are validated against the target
            // register file once multiple targets exist.
            return true;
        case MACHINE_REF_KIND_COUNT:
            return false;
    }
    return false;
}

MachineVerifyResult machine_verify_function(MachineFunction* function)
{
    MachineVerifyResult result = {0};
    if (!function)
    {
        result.error = MACHINE_VERIFY_EDGE_RANGE;
        return result;
    }
    if ((function->instruction_count && !function->instructions) || (function->virtual_register_count && !function->virtual_registers) ||
        (function->block_count && !function->blocks) || (function->edge_count && !function->edges) ||
        (function->block_parameter_count && !function->block_parameters) ||
        (function->edge_copy_source_count && !function->edge_copy_sources))
    {
        result.error = MACHINE_VERIFY_EDGE_RANGE;
        return result;
    }
    if (function->instruction_count >= MACHINE_POINT_INSTRUCTION_LIMIT)
    {
        result.error = MACHINE_VERIFY_POINT_CAPACITY;
        return result;
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        result.block = block_index;
        if (block->predecessor_offset > function->edge_count || block->predecessor_count > function->edge_count - block->predecessor_offset ||
            block->successor_offset > function->edge_count || block->successor_count > function->edge_count - block->successor_offset ||
            block->parameter_offset > function->block_parameter_count || block->parameter_count > function->block_parameter_count - block->parameter_offset)
        {
            result.error = MACHINE_VERIFY_EDGE_RANGE;
            return result;
        }
        for (u32 parameter_index = 0; parameter_index < block->parameter_count; parameter_index += 1)
        {
            MachineBlockParameter* parameter = function->block_parameters + block->parameter_offset + parameter_index;
            if (parameter->virtual_register >= function->virtual_register_count)
            {
                result.operand = parameter_index;
                result.error = MACHINE_VERIFY_BLOCK_PARAMETER;
                return result;
            }
        }
    }
    for (u32 edge_index = 0; edge_index < function->edge_count; edge_index += 1)
    {
        MachineEdge* edge = function->edges + edge_index;
        result.block = edge->source_block;
        if (edge->source_block >= function->block_count || edge->destination_block >= function->block_count || edge->copy_offset > function->edge_copy_source_count ||
            edge->copy_count > function->edge_copy_source_count - edge->copy_offset)
        {
            result.error = MACHINE_VERIFY_EDGE_RANGE;
            return result;
        }
        MachineBlock* destination = function->blocks + edge->destination_block;
        if (edge->copy_count != destination->parameter_count)
        {
            result.error = MACHINE_VERIFY_EDGE_COPY;
            return result;
        }
        for (u32 copy_index = 0; copy_index < edge->copy_count; copy_index += 1)
        {
            MachineRef source = function->edge_copy_sources[edge->copy_offset + copy_index];
            if (!machine_verify_reference(function, source))
            {
                result.operand = copy_index;
                result.error = MACHINE_VERIFY_EDGE_COPY;
                return result;
            }
        }
    }
    u32 covered_instruction_count = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        result.block = block_index;
        // Blocks must partition the instruction stream contiguously in
        // machine program order.
        if (block->first_instruction != covered_instruction_count || block->instruction_count > function->instruction_count - covered_instruction_count)
        {
            result.error = MACHINE_VERIFY_BLOCK_RANGE;
            return result;
        }
        covered_instruction_count += block->instruction_count;
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            u32 instruction_index = block->first_instruction + offset;
            result.instruction = instruction_index;
            MachineInstruction* instruction = function->instructions + instruction_index;
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            if (!info || instruction->opcode == MACHINE_OPCODE_INVALID)
            {
                result.error = MACHINE_VERIFY_OPCODE;
                return result;
            }
            bool is_terminator = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR) != 0;
            bool is_last = offset == block->instruction_count - 1;
            if (is_terminator != is_last)
            {
                result.error = MACHINE_VERIFY_TERMINATOR;
                return result;
            }
            for (u32 operand_index = 0; operand_index < BUSTER_ARRAY_LENGTH(instruction->operands); operand_index += 1)
            {
                result.operand = operand_index;
                MachineRef ref = instruction->operands[operand_index];
                if (operand_index >= info->operand_count)
                {
                    // Unused inline slots must stay empty so scans can trust
                    // the static operand count.
                    if (ref != MACHINE_REF_NONE_VALUE)
                    {
                        result.error = MACHINE_VERIFY_OPERAND_SLOT;
                        return result;
                    }
                    continue;
                }
                if (!machine_verify_reference(function, ref))
                {
                    result.error = MACHINE_VERIFY_OPERAND_REFERENCE;
                    return result;
                }
                u32 fixed_register = machine_opcode_fixed_register(info, operand_index);
                if (fixed_register != UINT32_MAX && machine_ref_kind(ref) == MACHINE_REF_PHYSICAL_REGISTER && machine_ref_payload(ref) != fixed_register)
                {
                    result.error = MACHINE_VERIFY_CONSTRAINT;
                    return result;
                }
            }
            u32 tied_destination = info->tied_pair & 0x0fu;
            u32 tied_source = (info->tied_pair >> 4) & 0x0fu;
            if (tied_destination && tied_source && tied_destination <= info->operand_count && tied_source <= info->operand_count)
            {
                MachineRef destination_ref = instruction->operands[tied_destination - 1u];
                MachineRef source_ref = instruction->operands[tied_source - 1u];
                if (machine_ref_kind(destination_ref) == MACHINE_REF_PHYSICAL_REGISTER && machine_ref_kind(source_ref) == MACHINE_REF_PHYSICAL_REGISTER &&
                    machine_ref_payload(destination_ref) != machine_ref_payload(source_ref))
                {
                    result.error = MACHINE_VERIFY_CONSTRAINT;
                    return result;
                }
            }
            result.operand = 0;
        }
    }
    result.block = 0;
    result.instruction = 0;
    if (covered_instruction_count != function->instruction_count)
    {
        result.error = MACHINE_VERIFY_INSTRUCTION_COVERAGE;
        return result;
    }
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        MachineVirtualRegister* virtual_register = function->virtual_registers + register_index;
        if (virtual_register->definition_point != MACHINE_POINT_INVALID &&
            machine_point_instruction(virtual_register->definition_point) >= function->instruction_count)
        {
            result.operand = register_index;
            result.error = MACHINE_VERIFY_VIRTUAL_REGISTER_DEFINITION;
            return result;
        }
    }
    return result;
}

// MIR_STACK placement: every virtual register owns one 8-byte frame slot
// and every operand round-trips through the target's fixed per-slot scratch
// register. This is the selector/encoder verification mode, not an
// allocator, and it is target-independent: everything target-specific comes
// through the function's MachineTargetDescription.
MachineStackPlacement machine_stack_placement_build(Arena* arena, MachineFunction* function)
{
    MachineStackPlacement placement = {
        .virtual_register_offsets = arena_allocate(arena, u32, function->virtual_register_count),
        .stack_slot_offsets = arena_allocate(arena, u32, function->stack_slot_count),
        .operand_registers = arena_allocate(arena, u8, (u64)function->instruction_count * 4),
    };
    MachineTargetDescription const* target = function->target;
    if (target)
    {
        // The encoder saves every callee-saved register named by the final
        // placement mask immediately below RBP.  Discover implicit clobbers
        // before laying out homes so the first virtual/stack slot starts past
        // that save area; otherwise a metadata-only clobber such as
        // CMPXCHG16B's RBX write aliases the saved register and corrupts the
        // caller when the generated function returns.
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            MachineOpcodeInfo const* info = machine_opcode_info(function->instructions[instruction_index].opcode);
            if (!info)
            {
                return placement;
            }
            placement.callee_saved_mask |= info->clobber_mask & target->callee_saved_mask;
        }
        u32 push_count = 0;
        for (u32 physical_register = 0; physical_register < target->register_count; physical_register += 1)
        {
            push_count += (placement.callee_saved_mask >> physical_register) & 1u;
        }
        // The callee-saved save area sits below the frame pointer only when the
        // pushes follow it. Where they precede it — Win64 — the saves are at the
        // frame pointer's positive offsets and every byte below it is frame, so
        // reserving and then subtracting a save area would size the allocation
        // short by exactly that many bytes and leave the deepest slots under the
        // stack pointer, where the next call's shadow space overwrites them.
        u32 push_area = function->target && function->target->saves_precede_frame_pointer ? 0u : 8u * push_count;
        u32 running = push_area;
        for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
        {
            // Vector values own 64-byte homes; the sixteen-byte offset rounding
            // mirrors the canonical frame layout's vector clamp, and every
            // access is the unaligned vmovdqu8 either way.
            if (function->virtual_registers[register_index].register_class == MACHINE_REGISTER_CLASS_VECTOR)
            {
                running = ((running + 15u) & ~15u) + 64u;
            }
            else
            {
                running += 8;
            }
            placement.virtual_register_offsets[register_index] = running;
        }
        for (u32 slot_index = 0; slot_index < function->stack_slot_count; slot_index += 1)
        {
            // The outgoing argument area is placed at the bottom of the frame
            // below, where a call's stack pointer lands on its base.
            if (function->outgoing_bytes && slot_index == function->outgoing_slot)
            {
                continue;
            }
            // The frame base is sixteen-aligned, so a slot whose start offset is
            // a multiple of its alignment is aligned in memory.
            u32 slot_alignment = function->stack_slot_alignments ? function->stack_slot_alignments[slot_index] : 8;
            running = (running + function->stack_slot_sizes[slot_index] + slot_alignment - 1) & ~(slot_alignment - 1);
            placement.stack_slot_offsets[slot_index] = running;
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
            u8* row_operand_registers = placement.operand_registers + (u64)instruction_index * 4;
            for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(instruction->operands); slot += 1)
            {
                u8* operand_register = row_operand_registers + slot;
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
                if (instruction->opcode == MACHINE_X64_VA_ARG)
                {
                    // The encoder's bounded sequence reserves RAX for the
                    // va_list pointer and RCX for a scalar result.
                    *operand_register = (u8)(slot == 0 ? MACHINE_X64_RAX : MACHINE_X64_RCX);
                }
                else
                {
                    u32 operand_class = (info->operand_info[slot] >> MACHINE_OPERAND_CLASS_SHIFT) & 0x7u;
                    *operand_register = operand_class == MACHINE_REGISTER_CLASS_VECTOR ? target->vector_slot_scratch[slot] : target->slot_scratch[slot];
                    u32 fixed_register = machine_opcode_fixed_register(info, slot);
                    if (fixed_register != UINT32_MAX)
                    {
                        *operand_register = (u8)fixed_register;
                    }
                    u32 tied_destination = UINT32_MAX;
                    for (u32 destination_slot = 0; destination_slot < info->operand_count; destination_slot += 1)
                    {
                        if (machine_opcode_operand_is_tied(info, destination_slot, slot))
                        {
                            tied_destination = destination_slot;
                            break;
                        }
                    }
                    if (tied_destination != UINT32_MAX && row_operand_registers[tied_destination] != UINT8_MAX)
                    {
                        *operand_register = row_operand_registers[tied_destination];
                    }
                }
                // A copy into a fixed physical register reloads its source
                // directly into that register: argument sequences would
                // otherwise clobber already-placed argument registers through
                // the shared operand scratches. The vector copy stages the same
                // way, or its reload through the shared ZMM scratch would
                // destroy an already-staged vector argument register.
                if ((instruction->opcode == target->copy_opcode || instruction->opcode == target->vector_copy_opcode) && slot == 1 &&
                    machine_ref_kind(instruction->operands[0]) == MACHINE_REF_PHYSICAL_REGISTER)
                {
                    *operand_register = (u8)machine_ref_payload(instruction->operands[0]);
                }
                if (instruction->opcode == target->indirect_call_opcode)
                {
                    // The callee pointer's register survives the argument
                    // registers and any variadic setup.
                    *operand_register = target->indirect_call_register;
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
                    u8 destination_register = row_operand_registers[slot];
                    if (destination_register == UINT8_MAX)
                    {
                        return placement;
                    }
                    MachineEdit* edit = (MachineEdit*)machine_stream_append(arena, &edits);
                    *edit = (MachineEdit){
                        .point = machine_point_make(instruction_index, MACHINE_POINT_AFTER),
                        .kind = MACHINE_EDIT_SPILL,
                        .subject = machine_ref_payload(ref),
                        .location = destination_register,
                    };
                    placement.spill_count += 1;
                }
            }
        }
        // The x86 prologue pushes every register named by the final clobber mask
        // around establishing RBP. Where the pushes follow it, the save area was
        // included in `running` above and is subtracted back out when sizing the
        // post-save allocation; where they precede it, `push_area` is zero and
        // the whole run is frame. Either way, add eight bytes for odd push parity
        // to restore the call boundary before any nested call.
        placement.frame_size = ((running - push_area + 15u) & ~15u) + ((push_count & 1u) ? 8u : 0u) + function->outgoing_bytes;
        if (function->outgoing_bytes)
        {
            placement.stack_slot_offsets[function->outgoing_slot] = placement.frame_size;
        }
        // Every offset handed out above is a distance below the frame pointer, and
        // `running` is their maximum by construction. The allocation plus whatever
        // save area really sits below the frame pointer must cover it, or the
        // deepest values live under the stack pointer where the next call's shadow
        // space and return address overwrite them. Refuse the placement instead:
        // the function falls back to the canonical emitter rather than miscompile.
        if (running <= placement.frame_size + push_area)
        {
            // Pushes that precede the frame pointer sit between it and the caller's
            // frame, so every incoming stack argument is that much further up.
            placement.incoming_base = function->target && function->target->saves_precede_frame_pointer ? 8u * push_count : 0u;
            placement.edits = arena_allocate(arena, MachineEdit, edits.total_count);
            placement.edit_count = edits.total_count;
            machine_stream_flatten(&edits, placement.edits);
            placement.valid = true;
        }
    }

    return placement;
}

typedef struct MachineReplayHeader MachineReplayHeader;
struct MachineReplayHeader
{
    u32 magic;
    u32 version;
    u32 instruction_count;
    u32 virtual_register_count;
    u32 block_count;
    u32 edge_count;
    u32 block_parameter_count;
    u32 edge_copy_source_count;
};
BUSTER_CT_CHECK(sizeof(MachineReplayHeader) == 32);

ByteSlice machine_replay_serialize(Arena* arena, MachineFunction* function)
{
    if ((function->instruction_count && !function->instructions) || (function->virtual_register_count && !function->virtual_registers) ||
        (function->block_count && !function->blocks) || (function->edge_count && !function->edges) ||
        (function->block_parameter_count && !function->block_parameters) ||
        (function->edge_copy_source_count && !function->edge_copy_sources))
    {
        return (ByteSlice){0};
    }
    u64 instruction_bytes = (u64)function->instruction_count * sizeof(MachineInstruction);
    u64 register_bytes = (u64)function->virtual_register_count * sizeof(MachineVirtualRegister);
    u64 block_bytes = (u64)function->block_count * sizeof(MachineBlock);
    u64 edge_bytes = (u64)function->edge_count * sizeof(MachineEdge);
    u64 parameter_bytes = (u64)function->block_parameter_count * sizeof(MachineBlockParameter);
    u64 edge_source_bytes = (u64)function->edge_copy_source_count * sizeof(MachineRef);
    u64 total = sizeof(MachineReplayHeader) + instruction_bytes + register_bytes + block_bytes + edge_bytes + parameter_bytes + edge_source_bytes;
    u8* bytes = arena_allocate(arena, u8, total);
    MachineReplayHeader header = {
        .magic = MACHINE_REPLAY_MAGIC,
        .version = MACHINE_REPLAY_VERSION,
        .instruction_count = function->instruction_count,
        .virtual_register_count = function->virtual_register_count,
        .block_count = function->block_count,
        .edge_count = function->edge_count,
        .block_parameter_count = function->block_parameter_count,
        .edge_copy_source_count = function->edge_copy_source_count,
    };
    u64 offset = 0;
    memcpy(bytes + offset, &header, sizeof(header));
    offset += sizeof(header);
    if (instruction_bytes)
    {
        memcpy(bytes + offset, function->instructions, instruction_bytes);
        offset += instruction_bytes;
    }
    if (register_bytes)
    {
        memcpy(bytes + offset, function->virtual_registers, register_bytes);
        offset += register_bytes;
    }
    if (block_bytes)
    {
        memcpy(bytes + offset, function->blocks, block_bytes);
        offset += block_bytes;
    }
    if (edge_bytes)
    {
        memcpy(bytes + offset, function->edges, edge_bytes);
        offset += edge_bytes;
    }
    if (parameter_bytes)
    {
        memcpy(bytes + offset, function->block_parameters, parameter_bytes);
        offset += parameter_bytes;
    }
    if (edge_source_bytes)
    {
        memcpy(bytes + offset, function->edge_copy_sources, edge_source_bytes);
        offset += edge_source_bytes;
    }
    // The length is what was actually written, which the running offset
    // already holds; `total` only had to size the allocation. Keeping the
    // final increment live also means a section appended after this one
    // cannot silently drop out of the length.
    return (ByteSlice){
        .pointer = bytes,
        .length = offset,
    };
}

bool machine_replay_deserialize(Arena* arena, ByteSlice bytes, MachineFunction* function)
{
    if (!function || (!bytes.pointer && bytes.length) || bytes.length < sizeof(MachineReplayHeader))
    {
        return false;
    }
    MachineReplayHeader header;
    memcpy(&header, bytes.pointer, sizeof(header));
    if (header.magic != MACHINE_REPLAY_MAGIC || header.version != MACHINE_REPLAY_VERSION)
    {
        return false;
    }
    u64 instruction_bytes = (u64)header.instruction_count * sizeof(MachineInstruction);
    u64 register_bytes = (u64)header.virtual_register_count * sizeof(MachineVirtualRegister);
    u64 block_bytes = (u64)header.block_count * sizeof(MachineBlock);
    u64 edge_bytes = (u64)header.edge_count * sizeof(MachineEdge);
    u64 parameter_bytes = (u64)header.block_parameter_count * sizeof(MachineBlockParameter);
    u64 edge_source_bytes = (u64)header.edge_copy_source_count * sizeof(MachineRef);
    u64 total = sizeof(MachineReplayHeader) + instruction_bytes + register_bytes + block_bytes + edge_bytes + parameter_bytes + edge_source_bytes;
    if (bytes.length != total)
    {
        return false;
    }
    MachineFunction read = {
        .instructions = arena_allocate(arena, MachineInstruction, header.instruction_count),
        .virtual_registers = arena_allocate(arena, MachineVirtualRegister, header.virtual_register_count),
        .blocks = arena_allocate(arena, MachineBlock, header.block_count),
        .edges = arena_allocate(arena, MachineEdge, header.edge_count),
        .block_parameters = arena_allocate(arena, MachineBlockParameter, header.block_parameter_count),
        .edge_copy_sources = arena_allocate(arena, MachineRef, header.edge_copy_source_count),
        .instruction_count = header.instruction_count,
        .virtual_register_count = header.virtual_register_count,
        .block_count = header.block_count,
        .edge_count = header.edge_count,
        .block_parameter_count = header.block_parameter_count,
        .edge_copy_source_count = header.edge_copy_source_count,
    };
    u64 offset = sizeof(MachineReplayHeader);
    if (instruction_bytes)
    {
        memcpy(read.instructions, bytes.pointer + offset, instruction_bytes);
        offset += instruction_bytes;
    }
    if (register_bytes)
    {
        memcpy(read.virtual_registers, bytes.pointer + offset, register_bytes);
        offset += register_bytes;
    }
    if (block_bytes)
    {
        memcpy(read.blocks, bytes.pointer + offset, block_bytes);
        offset += block_bytes;
    }
    if (edge_bytes)
    {
        memcpy(read.edges, bytes.pointer + offset, edge_bytes);
        offset += edge_bytes;
    }
    if (parameter_bytes)
    {
        memcpy(read.block_parameters, bytes.pointer + offset, parameter_bytes);
        offset += parameter_bytes;
    }
    if (edge_source_bytes)
    {
        memcpy(read.edge_copy_sources, bytes.pointer + offset, edge_source_bytes);
    }
    *function = read;
    return true;
}

#include <buster/lib/compiler/codegen/machine_select.c>
#include <buster/lib/compiler/codegen/machine_x86_64.c>
#include <buster/lib/compiler/codegen/machine_aarch64.c>
#include <buster/lib/compiler/codegen/machine_schedule.c>
#include <buster/lib/compiler/codegen/register_allocator_fast.c>
#include <buster/lib/compiler/codegen/register_allocator_quality.c>

BUSTER_GLOBAL_LOCAL MachineSelectResult machine_select_canonical_function_internal(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                                                    bool assume_validated)
{
    MachineSelectResult result;

    switch (target.cpu_arch)
    {
        break; case CPU_ARCH_X86_64: result = machine_select_canonical_function_x86_64(arena, program, function, target, assume_validated);
        break; case CPU_ARCH_AARCH64: result = machine_select_canonical_function_aarch64(arena, program, function, target, assume_validated);
        break; default: BUSTER_TODO();
    }

    return result;
}

MachineSelectResult machine_select_canonical_function(Arena* arena, IrProgram* program, IrFunction* function, Target target)
{
    return machine_select_canonical_function_internal(arena, program, function, target, false);
}

MachineSelectResult machine_select_validated_canonical_function(Arena* arena, IrProgram* program, IrFunction* function, Target target)
{
    return machine_select_canonical_function_internal(arena, program, function, target, true);
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL void machine_fast_picker_test_state_reset(MachineFastState* state, MachineTargetDescription const* description,
                                                              MachineStackPlacement* placement, u32* locations, u32* last_use,
                                                              u8* escapes, u32* rematerialize_immediates)
{
    *state = (MachineFastState){0};
    state->description = description;
    state->placement = placement;
    state->virtual_register_locations = locations;
    state->last_use = last_use;
    state->escapes = escapes;
    state->rematerialize_immediates = rematerialize_immediates;
    state->active_register_count = description->register_count;
    state->current_point = machine_point_make(1, MACHINE_POINT_BEFORE);
    for (u32 register_index = 0; register_index < MACHINE_TARGET_REGISTER_LIMIT; register_index += 1)
    {
        state->owner[register_index] = UINT32_MAX;
        state->age[register_index] = register_index;
        state->dirty[register_index] = false;
        locations[register_index] = UINT32_MAX;
        last_use[register_index] = UINT32_MAX;
        escapes[register_index] = 0;
        rematerialize_immediates[register_index] = UINT32_MAX;
    }
}

BUSTER_GLOBAL_LOCAL void machine_fast_picker_test_occupy(MachineFastState* state, u64 mask)
{
    for (u32 register_index = 0; register_index < state->active_register_count; register_index += 1)
    {
        if (mask & (1ull << register_index))
        {
            state->owner[register_index] = register_index;
            state->virtual_register_locations[register_index] = register_index;
        }
    }
}

BUSTER_GLOBAL_LOCAL u64 machine_fast_picker_test_first_registers(u64 available, u32 count, u32* registers)
{
    u64 selected = 0;
    for (u32 index = 0; index < count && available; index += 1)
    {
        u32 physical_register = machine_fast_first_set(available);
        registers[index] = physical_register;
        selected |= 1ull << physical_register;
        available &= available - 1;
    }
    return selected;
}

BUSTER_F_DECL u32 machine_fast_picker_test_cases(void)
{
    u32 passed = 0;
    MachineTargetDescription const* targets[] = {machine_target_x86_64(), machine_target_aarch64()};
    bool preferred_free = true;
    bool lowest_other_free = true;
    bool dead_first = true;
    bool lru_order = true;
    bool ctz_guard = true;
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
    {
        MachineTargetDescription const* description = targets[target_index];
        MachineStackPlacement placement = {0};
        MachineFastState state;
        u32 locations[MACHINE_TARGET_REGISTER_LIMIT];
        u32 last_use[MACHINE_TARGET_REGISTER_LIMIT];
        u8 escapes[MACHINE_TARGET_REGISTER_LIMIT];
        u32 rematerialize_immediates[MACHINE_TARGET_REGISTER_LIMIT];
        u64 allocatable = description->allocatable_mask;
        u64 callee_saved = allocatable & description->callee_saved_mask;
        u64 caller_saved = allocatable & ~description->callee_saved_mask;
        u32 preferred_registers[1] = {UINT32_MAX};
        u32 caller_registers[3] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
        u64 preferred_mask = machine_fast_picker_test_first_registers(callee_saved, 1, preferred_registers);
        u64 caller_mask = machine_fast_picker_test_first_registers(caller_saved, 3, caller_registers);
        preferred_free &= preferred_mask && caller_mask;
        lowest_other_free &= caller_mask && caller_registers[1] != UINT32_MAX;
        dead_first &= caller_mask && caller_registers[2] != UINT32_MAX;
        lru_order &= caller_mask && caller_registers[2] != UINT32_MAX;
        ctz_guard &= description->register_count &&
                     machine_fast_first_set(1ull << (description->register_count - 1)) == description->register_count - 1;
        if (!preferred_mask || !caller_mask || caller_registers[1] == UINT32_MAX || caller_registers[2] == UINT32_MAX)
        {
            continue;
        }

        machine_fast_picker_test_state_reset(&state, description, &placement, locations, last_use, escapes, rematerialize_immediates);
        u32 selected = machine_fast_pick(&state, preferred_mask | (1ull << caller_registers[0]), 0, true);
        preferred_free &= selected == preferred_registers[0];

        machine_fast_picker_test_state_reset(&state, description, &placement, locations, last_use, escapes, rematerialize_immediates);
        selected = machine_fast_pick(&state, (1ull << caller_registers[0]) | (1ull << caller_registers[1]), 0, true);
        lowest_other_free &= selected == caller_registers[0];

        machine_fast_picker_test_state_reset(&state, description, &placement, locations, last_use, escapes, rematerialize_immediates);
        machine_fast_picker_test_occupy(&state, (1ull << caller_registers[0]) | (1ull << caller_registers[1]) | (1ull << caller_registers[2]));
        last_use[caller_registers[1]] = 0;
        selected = machine_fast_pick(&state,
                                     (1ull << caller_registers[0]) | (1ull << caller_registers[1]) | (1ull << caller_registers[2]), 0, true);
        dead_first &= selected == caller_registers[1];

        machine_fast_picker_test_state_reset(&state, description, &placement, locations, last_use, escapes, rematerialize_immediates);
        machine_fast_picker_test_occupy(&state, (1ull << caller_registers[0]) | (1ull << caller_registers[1]) | (1ull << caller_registers[2]));
        state.age[caller_registers[0]] = 10;
        state.age[caller_registers[1]] = 4;
        state.age[caller_registers[2]] = 4;
        selected = machine_fast_pick(&state,
                                     (1ull << caller_registers[0]) | (1ull << caller_registers[1]) | (1ull << caller_registers[2]), 0, true);
        lru_order &= selected == caller_registers[1];
    }
    if (preferred_free) passed |= MACHINE_FAST_PICK_TEST_PREFERRED_FREE;
    if (lowest_other_free) passed |= MACHINE_FAST_PICK_TEST_LOWEST_OTHER_FREE;
    if (dead_first) passed |= MACHINE_FAST_PICK_TEST_DEAD_FIRST;
    if (lru_order) passed |= MACHINE_FAST_PICK_TEST_LRU_ORDER;
    if (ctz_guard) passed |= MACHINE_FAST_PICK_TEST_CTZ_GUARD;
    return passed;
}
#endif
