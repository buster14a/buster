#include <buster/lib/compiler/codegen/machine.h>

#include <buster/lib/os.h>
#include <buster/lib/string.h>

BUSTER_F_DECL MachineRef machine_ref_make(MachineRefKind kind, u32 payload)
{
    BUSTER_CHECK(kind < MACHINE_REF_KIND_COUNT);
    BUSTER_CHECK(payload < MACHINE_REF_PAYLOAD_LIMIT);
    return ((u32)kind << MACHINE_REF_PAYLOAD_BITS) | payload;
}

BUSTER_F_DECL MachineRefKind machine_ref_kind(MachineRef ref)
{
    return (MachineRefKind)(ref >> MACHINE_REF_PAYLOAD_BITS);
}

BUSTER_F_DECL u32 machine_ref_payload(MachineRef ref)
{
    return ref & (MACHINE_REF_PAYLOAD_LIMIT - 1u);
}

BUSTER_F_DECL MachinePoint machine_point_make(u32 instruction_index, MachinePointPhase phase)
{
    BUSTER_CHECK(instruction_index < MACHINE_POINT_INSTRUCTION_LIMIT);
    BUSTER_CHECK(phase < MACHINE_POINT_PHASE_COUNT);
    return (instruction_index << MACHINE_POINT_PHASE_BITS) | (u32)phase;
}

BUSTER_F_DECL u32 machine_point_instruction(MachinePoint point)
{
    return point >> MACHINE_POINT_PHASE_BITS;
}

BUSTER_F_DECL MachinePointPhase machine_point_phase(MachinePoint point)
{
    return (MachinePointPhase)(point & ((1u << MACHINE_POINT_PHASE_BITS) - 1u));
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
    }
#define MACHINE_INFO_READ_MODIFY(name_literal)                                                                                                                 \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},           \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,                                                                                                   \
    }
#define MACHINE_INFO_SHIFT(name_literal)                                                                                                                       \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},           \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,                                                            \
    }
#define MACHINE_INFO_DIVIDE(name_literal)                                                                                                                      \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},           \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE | MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED, .clobber_mask = 1u << MACHINE_X64_RDX,                     \
    }
#define MACHINE_INFO_MOVE_CONSTRAINED(name_literal)                                                                                                            \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},               \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,                                                                                                    \
    }
#define MACHINE_INFO_MOVE_CONSTRAINED_CLOBBER(name_literal, clobbers)                                                                                          \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},               \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED, .clobber_mask = (clobbers),                                                                        \
    }
#define MACHINE_INFO_THREE_ADDRESS(name_literal)                                                                                                               \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 3,                                                                                              \
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                                            \
    }
#define MACHINE_INFO_THREE_ADDRESS_CONSTRAINED(name_literal)                                                                                                   \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 3,                                                                                              \
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                                            \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED,                                                                                                    \
    }
#define MACHINE_INFO_UNARY_READ_MODIFY(name_literal)                                                                                                           \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 1, .operand_info = {MACHINE_OPERAND_USE_DEFINE_GENERAL},                                        \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,                                                                                                   \
    }
#define MACHINE_INFO_COMPARE(name_literal)                                                                                                                     \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                  \
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,                                                                                                   \
    }
#define MACHINE_INFO_STORE_FRAME(name_literal)                                                                                                                 \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {0, MACHINE_OPERAND_USE_GENERAL},                                            \
    }
#define MACHINE_INFO_MOVE_CLOBBER(name_literal, clobbers)                                                                                                      \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, MACHINE_OPERAND_USE_GENERAL},               \
        .clobber_mask = (clobbers),                                                                                                                            \
    }
#define MACHINE_INFO_LOAD_POINTER(name_literal) MACHINE_INFO_MOVE(name_literal)
#define MACHINE_INFO_STORE_POINTER(name_literal)                                                                                                               \
    {                                                                                                                                                          \
        .name = S8_INITIALIZER(name_literal), .operand_count = 2, .operand_info = {MACHINE_OPERAND_USE_GENERAL, MACHINE_OPERAND_USE_GENERAL},                  \
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
    [MACHINE_X64_ADD32] = MACHINE_INFO_READ_MODIFY("x64_add32"),
    [MACHINE_X64_ADD64] = MACHINE_INFO_READ_MODIFY("x64_add64"),
    [MACHINE_X64_SUB32] = MACHINE_INFO_READ_MODIFY("x64_sub32"),
    [MACHINE_X64_SUB64] = MACHINE_INFO_READ_MODIFY("x64_sub64"),
    [MACHINE_X64_AND32] = MACHINE_INFO_READ_MODIFY("x64_and32"),
    [MACHINE_X64_AND64] = MACHINE_INFO_READ_MODIFY("x64_and64"),
    [MACHINE_X64_OR32] = MACHINE_INFO_READ_MODIFY("x64_or32"),
    [MACHINE_X64_OR64] = MACHINE_INFO_READ_MODIFY("x64_or64"),
    [MACHINE_X64_XOR32] = MACHINE_INFO_READ_MODIFY("x64_xor32"),
    [MACHINE_X64_XOR64] = MACHINE_INFO_READ_MODIFY("x64_xor64"),
    [MACHINE_X64_IMUL32] = MACHINE_INFO_READ_MODIFY("x64_imul32"),
    [MACHINE_X64_IMUL64] = MACHINE_INFO_READ_MODIFY("x64_imul64"),
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
    },
    [MACHINE_X64_LOAD_FRAME] = {
        .name = S8_INITIALIZER("x64_load_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
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
    [MACHINE_A64_ADD32] = MACHINE_INFO_THREE_ADDRESS("a64_add32"),
    [MACHINE_A64_ADD64] = MACHINE_INFO_THREE_ADDRESS("a64_add64"),
    [MACHINE_A64_SUB32] = MACHINE_INFO_THREE_ADDRESS("a64_sub32"),
    [MACHINE_A64_SUB64] = MACHINE_INFO_THREE_ADDRESS("a64_sub64"),
    [MACHINE_A64_AND32] = MACHINE_INFO_THREE_ADDRESS("a64_and32"),
    [MACHINE_A64_AND64] = MACHINE_INFO_THREE_ADDRESS("a64_and64"),
    [MACHINE_A64_ORR32] = MACHINE_INFO_THREE_ADDRESS("a64_orr32"),
    [MACHINE_A64_ORR64] = MACHINE_INFO_THREE_ADDRESS("a64_orr64"),
    [MACHINE_A64_EOR32] = MACHINE_INFO_THREE_ADDRESS("a64_eor32"),
    [MACHINE_A64_EOR64] = MACHINE_INFO_THREE_ADDRESS("a64_eor64"),
    [MACHINE_A64_MUL32] = MACHINE_INFO_THREE_ADDRESS("a64_mul32"),
    [MACHINE_A64_MUL64] = MACHINE_INFO_THREE_ADDRESS("a64_mul64"),
    [MACHINE_A64_SDIV32] = MACHINE_INFO_THREE_ADDRESS("a64_sdiv32"),
    [MACHINE_A64_SDIV64] = MACHINE_INFO_THREE_ADDRESS("a64_sdiv64"),
    [MACHINE_A64_UDIV32] = MACHINE_INFO_THREE_ADDRESS("a64_udiv32"),
    [MACHINE_A64_UDIV64] = MACHINE_INFO_THREE_ADDRESS("a64_udiv64"),
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
    [MACHINE_A64_CMP32] = MACHINE_INFO_COMPARE("a64_cmp32"),
    [MACHINE_A64_CMP64] = MACHINE_INFO_COMPARE("a64_cmp64"),
    [MACHINE_A64_CMP_ZERO] = {
        .name = S8_INITIALIZER("a64_cmp_zero"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE,
    },
    [MACHINE_A64_CSET] = {
        .name = S8_INITIALIZER("a64_cset"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
        .attributes = MACHINE_OPCODE_ATTRIBUTE_FLAGS_USE,
    },
    [MACHINE_A64_LOAD_FRAME] = {
        .name = S8_INITIALIZER("a64_load_frame"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
    },
    [MACHINE_A64_LOAD_FRAME32] = {
        .name = S8_INITIALIZER("a64_load_frame32"),
        .operand_count = 2,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL, 0},
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
    },
    [MACHINE_A64_RET] = {
        .name = S8_INITIALIZER("a64_ret"),
        .attributes = MACHINE_OPCODE_ATTRIBUTE_TERMINATOR,
    },
    [MACHINE_A64_FMOV_TO_VEC] = {
        .name = S8_INITIALIZER("a64_fmov_to_vec"),
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_USE_GENERAL},
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
};

BUSTER_F_DECL MachineOpcodeInfo const* machine_opcode_info(u16 opcode)
{
    if (opcode >= MACHINE_OPCODE_COUNT)
    {
        return 0;
    }
    return machine_opcode_infos + opcode;
}

BUSTER_F_DECL void machine_stream_initialize(MachineBuilderStream* stream, u64 element_size)
{
    stream->first = 0;
    stream->last = 0;
    stream->total_count = 0;
    stream->element_size = (u32)element_size;
    stream->chunk_capacity = (u32)((MACHINE_BUILDER_CHUNK_BYTES - sizeof(MachineBuilderChunk)) / element_size);
    stream->reserved = 0;
}

BUSTER_F_DECL void* machine_stream_append(Arena* arena, MachineBuilderStream* stream)
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

BUSTER_F_DECL void machine_stream_flatten(MachineBuilderStream* stream, void* destination)
{
    u64 offset = 0;
    for (MachineBuilderChunk* chunk = stream->first; chunk; chunk = chunk->next)
    {
        u64 chunk_bytes = (u64)chunk->count * stream->element_size;
        memcpy((u8*)destination + offset, chunk + 1, chunk_bytes);
        offset += chunk_bytes;
    }
}

BUSTER_F_DECL MachineFunctionBuilder machine_function_builder_begin(Arena* arena)
{
    MachineFunctionBuilder builder = {
        .arena = arena,
        .open_block = UINT32_MAX,
    };
    machine_stream_initialize(&builder.instructions, sizeof(MachineInstruction));
    machine_stream_initialize(&builder.virtual_registers, sizeof(MachineVirtualRegister));
    machine_stream_initialize(&builder.blocks, sizeof(MachineBlock));
    return builder;
}

BUSTER_F_DECL u32 machine_builder_virtual_register(MachineFunctionBuilder* builder, MachineVirtualRegister virtual_register)
{
    u32 index = builder->virtual_registers.total_count;
    MachineVirtualRegister* row = (MachineVirtualRegister*)machine_stream_append(builder->arena, &builder->virtual_registers);
    *row = virtual_register;
    return index;
}

BUSTER_F_DECL u32 machine_builder_block_begin(MachineFunctionBuilder* builder)
{
    BUSTER_CHECK(!builder->block_is_open);
    builder->block_is_open = true;
    builder->open_block = builder->blocks.total_count;
    builder->open_block_first_instruction = builder->instructions.total_count;
    return builder->open_block;
}

BUSTER_F_DECL u32 machine_builder_instruction(MachineFunctionBuilder* builder, MachineInstruction instruction)
{
    BUSTER_CHECK(builder->block_is_open);
    u32 index = builder->instructions.total_count;
    if (index >= MACHINE_POINT_INSTRUCTION_LIMIT)
    {
        builder->point_capacity_exceeded = true;
    }
    MachineInstruction* row = (MachineInstruction*)machine_stream_append(builder->arena, &builder->instructions);
    *row = instruction;
    return index;
}

BUSTER_F_DECL void machine_builder_block_end(MachineFunctionBuilder* builder, MachineBlock block)
{
    BUSTER_CHECK(builder->block_is_open);
    block.first_instruction = builder->open_block_first_instruction;
    block.instruction_count = builder->instructions.total_count - builder->open_block_first_instruction;
    MachineBlock* row = (MachineBlock*)machine_stream_append(builder->arena, &builder->blocks);
    *row = block;
    builder->block_is_open = false;
    builder->open_block = UINT32_MAX;
}

BUSTER_F_DECL MachineFunction machine_function_builder_finish(Arena* arena, MachineFunctionBuilder* builder)
{
    BUSTER_CHECK(!builder->block_is_open);
    MachineFunction function = {
        .instructions = arena_allocate(arena, MachineInstruction, builder->instructions.total_count),
        .virtual_registers = arena_allocate(arena, MachineVirtualRegister, builder->virtual_registers.total_count),
        .blocks = arena_allocate(arena, MachineBlock, builder->blocks.total_count),
        .instruction_count = builder->instructions.total_count,
        .virtual_register_count = builder->virtual_registers.total_count,
        .block_count = builder->blocks.total_count,
    };
    machine_stream_flatten(&builder->instructions, function.instructions);
    machine_stream_flatten(&builder->virtual_registers, function.virtual_registers);
    machine_stream_flatten(&builder->blocks, function.blocks);
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

BUSTER_F_DECL void machine_function_stamp_frequency_classes(MachineFunction* function)
{
    u32 block_count = function->block_count;
    if (!block_count)
    {
        return;
    }
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

BUSTER_F_DECL MachineVerifyResult machine_verify_function(MachineFunction* function)
{
    MachineVerifyResult result = {0};
    if (function->instruction_count >= MACHINE_POINT_INSTRUCTION_LIMIT)
    {
        result.error = MACHINE_VERIFY_POINT_CAPACITY;
        return result;
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
    if (!target)
    {
        return placement;
    }
    u32 running = 0;
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
        // MIR_STACK has no register allocator to notice an implicit
        // callee-saved clobber. Account for one directly from opcode
        // metadata so CMPXCHG16B receives the same ABI save/restore as
        // FAST/QUALITY placement.
        placement.callee_saved_mask |= info->clobber_mask & target->callee_saved_mask;
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
            u32 operand_class = (info->operand_info[slot] >> MACHINE_OPERAND_CLASS_SHIFT) & 0x7u;
            *operand_register = operand_class == MACHINE_REGISTER_CLASS_VECTOR ? target->vector_slot_scratch[slot] : target->slot_scratch[slot];
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
                u32 defined_class = (info->operand_info[slot] >> MACHINE_OPERAND_CLASS_SHIFT) & 0x7u;
                MachineEdit* edit = (MachineEdit*)machine_stream_append(arena, &edits);
                *edit = (MachineEdit){
                    .point = machine_point_make(instruction_index, MACHINE_POINT_AFTER),
                    .kind = MACHINE_EDIT_SPILL,
                    .subject = machine_ref_payload(ref),
                    .location = defined_class == MACHINE_REGISTER_CLASS_VECTOR ? target->vector_slot_scratch[slot] : target->slot_scratch[slot],
                };
                placement.spill_count += 1;
            }
        }
    }
    // The x86 prologue pushes every register named by the final clobber
    // mask after establishing RBP. MIR_STACK has no allocator pass to fold
    // those pushes into its running size, so account for their parity now:
    // an odd number of pushes needs an eight-byte subtract to restore the
    // System V call boundary before any nested call.
    u32 push_count = 0;
    for (u32 physical_register = 0; physical_register < target->register_count; physical_register += 1)
    {
        push_count += (placement.callee_saved_mask >> physical_register) & 1u;
    }
    placement.frame_size = (running + 15u) & ~15u;
    if (push_count & 1u)
    {
        placement.frame_size += 8u;
    }
    placement.edits = arena_allocate(arena, MachineEdit, edits.total_count);
    placement.edit_count = edits.total_count;
    machine_stream_flatten(&edits, placement.edits);
    placement.valid = true;
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
    u32 reserved;
};
BUSTER_CT_CHECK(sizeof(MachineReplayHeader) == 24);

BUSTER_F_DECL ByteSlice machine_replay_serialize(Arena* arena, MachineFunction* function)
{
    u64 instruction_bytes = (u64)function->instruction_count * sizeof(MachineInstruction);
    u64 register_bytes = (u64)function->virtual_register_count * sizeof(MachineVirtualRegister);
    u64 block_bytes = (u64)function->block_count * sizeof(MachineBlock);
    u64 total = sizeof(MachineReplayHeader) + instruction_bytes + register_bytes + block_bytes;
    u8* bytes = arena_allocate(arena, u8, total);
    MachineReplayHeader header = {
        .magic = MACHINE_REPLAY_MAGIC,
        .version = MACHINE_REPLAY_VERSION,
        .instruction_count = function->instruction_count,
        .virtual_register_count = function->virtual_register_count,
        .block_count = function->block_count,
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
    // The length is what was actually written, which the running offset
    // already holds; `total` only had to size the allocation. Keeping the
    // final increment live also means a section appended after this one
    // cannot silently drop out of the length.
    return (ByteSlice){
        .pointer = bytes,
        .length = offset,
    };
}

BUSTER_F_DECL bool machine_replay_deserialize(Arena* arena, ByteSlice bytes, MachineFunction* function)
{
    if (bytes.length < sizeof(MachineReplayHeader))
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
    u64 total = sizeof(MachineReplayHeader) + instruction_bytes + register_bytes + block_bytes;
    if (bytes.length != total)
    {
        return false;
    }
    MachineFunction read = {
        .instructions = arena_allocate(arena, MachineInstruction, header.instruction_count),
        .virtual_registers = arena_allocate(arena, MachineVirtualRegister, header.virtual_register_count),
        .blocks = arena_allocate(arena, MachineBlock, header.block_count),
        .instruction_count = header.instruction_count,
        .virtual_register_count = header.virtual_register_count,
        .block_count = header.block_count,
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
    }
    *function = read;
    return true;
}

#include <buster/lib/compiler/codegen/machine_x86_64.c>
#include <buster/lib/compiler/codegen/machine_aarch64.c>
#include <buster/lib/compiler/codegen/machine_schedule.c>
#include <buster/lib/compiler/codegen/register_allocator_fast.c>
#include <buster/lib/compiler/codegen/register_allocator_quality.c>

BUSTER_F_DECL MachineSelectResult machine_select_canonical_function(Arena* arena, IrProgram* program, IrFunction* function, Target target)
{
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        return machine_select_canonical_function_aarch64(arena, program, function, target);
    }
    return machine_select_canonical_function_x86_64(arena, program, function, target);
}
