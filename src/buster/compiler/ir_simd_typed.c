// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Decode only the initial SIMD slice of #717's actual Tier0Operation contract. Include after tier0.c, ir.c and
// x86_target.c. This is an abstract semantic admission module, not a native selector: output-target features are
// explicit inputs and no compiler-host capability authorizes a form. Exact record roles/decorations/address fields
// distinguish an admitted operation from an arbitrary X86 sequence. All unsupported forms fail transactionally.
// The scalar corpus adapter remains independently owned under #131; this module does not modify that adapter.

typedef enum IrSimdTypedStatus
{
    IR_SIMD_TYPED_OK,
    IR_SIMD_TYPED_UNSUPPORTED,
    IR_SIMD_TYPED_TARGET,
    IR_SIMD_TYPED_INVALID,
} IrSimdTypedStatus;

// A mask row's step (#179): its registers are indices, mask operand j's j, the result's the first mask operand's, which
// Tier 0 places MASK_RESULT in (tier0_operation_mask_plan), or BUSTER_IR_SIMD_MASK_RESULT in a row of no operand, and
// MASK_SCRATCH(k)'s BUSTER_IR_SIMD_MASK_SCRATCH + k. A step's kind is its record's instruction: and, or, xor, xnor
// (~(left ^ right)), andn (~left & right), not and move of `left`, add (modulo the mask's bits), shifts of `left` by
// `count` (0 from the mask's bits on, as kshiftl and kshiftr give), and unpack (kunpck: left's low half above right's).
#define BUSTER_IR_SIMD_MASK_RESULT 8
#define BUSTER_IR_SIMD_MASK_SCRATCH 9
#define BUSTER_IR_SIMD_MASK_REGISTERS (BUSTER_IR_SIMD_MASK_SCRATCH + BUSTER_TIER0_SCRATCH)

typedef enum IrSimdMaskStepKind
{
    IR_SIMD_MASK_AND,
    IR_SIMD_MASK_OR,
    IR_SIMD_MASK_XOR,
    IR_SIMD_MASK_XNOR,
    IR_SIMD_MASK_ANDN,
    IR_SIMD_MASK_ADD,
    IR_SIMD_MASK_UNPACK,
    IR_SIMD_MASK_NOT,
    IR_SIMD_MASK_MOVE,
    IR_SIMD_MASK_SHIFT_LEFT,
    IR_SIMD_MASK_SHIFT_RIGHT,
    IR_SIMD_MASK_KINDS,
    // A flags test's step (ir_simd_typed_mask_test), a row's only one: its int, 1 where the flag `count` names is set (0
    // ZF, 1 CF) and 0 where it isn't, into the result's register. ktest's ZF is set where left & right is 0, its CF where
    // ~left & right is; kortest's ZF where left | right is 0, its CF where it has every one of the mask's bits.
    IR_SIMD_MASK_TEST = IR_SIMD_MASK_KINDS,
    IR_SIMD_MASK_ORTEST,
} IrSimdMaskStepKind;

typedef struct IrSimdTypedMaskStep IrSimdTypedMaskStep;
struct IrSimdTypedMaskStep
{
    uint8_t kind;
    uint8_t destination;
    uint8_t left;
    uint8_t right;
    uint8_t count;
};

typedef struct IrSimdTypedOperation IrSimdTypedOperation;
struct IrSimdTypedOperation
{
    IrSimdOperation operation;
    IrSimdShape shape;
    IrSimdPredicate predicate;
    // The lanes it reads (#141's element types), its IR operation's flags; a comparison's expansion selects at them.
    IrSimdLane lane;
    uint64_t required_features;
    uint8_t operand_count;
    uint8_t operand_classes[BUSTER_TIER0_OPERATION_OPERANDS];
    uint8_t operand_bytes[BUSTER_TIER0_OPERATION_OPERANDS];
    // #717 represents a C comparison as a mask followed by -1/0 vector lanes, not the raw mask alone.
    bool expand_mask;
    // A row of opmask instructions alone (#179, ir_simd_typed_mask_row): its masks' bytes (0 for a vector operation), its
    // records' steps in order, and the register its result is in when they end.
    uint8_t mask_bytes;
    uint8_t mask_step_count;
    uint8_t mask_result;
    IrSimdTypedMaskStep mask_steps[BUSTER_TIER0_OPERATION_RECORDS];
    // A row of one scalar BMI instruction (#141, ir_simd_typed_intrinsic): its IR intrinsic plus one
    // (BUSTER_IR_INTRINSIC_* + 1, 0 for any other row), and the operands its a and b are, the source and the other.
    uint8_t intrinsic;
    uint8_t intrinsic_operands[2];
    // A row the IR has no operation of its own for (#141, ir_simd_typed_machine_row): the row itself, which a row call
    // runs (IrFunction.rows).
    bool machine;
};

typedef struct IrSimdTypedForm IrSimdTypedForm;
struct IrSimdTypedForm
{
    uint16_t forms[3];
    IrSimdOperation operation;
    // The lanes its form reads (#141's element types): I32 for the bitwise forms, whose bits are any lanes'.
    IrSimdLane lane;
};

// The features Tier 1's moves, spills and reloads of a vector of `bytes` take at a target of `target_features`
// (tier1_select_class_forms, tier1_select_vector_forms): AVX512F's of a zmm register; of an xmm or a ymm one AVX512F's and
// VL's EVEX forms where the target has both, else AVX's VEX ones (#1281), which name registers below 16 alone; and of an
// xmm one at a target without AVX (#141's SSE targets) legacy SSE's movups and movaps, x86-64's own (none past it). A
// 32-byte vector and an SSE carrier still need AVX: a legacy target has no ymm register, nor the carriers' zeroing moves
// (vmovq, vinsertps) Tier 1 takes.
static uint64_t ir_typed_vector_moves(uint32_t bytes, uint64_t target_features)
{
    const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    return bytes == 64                                                           ? BUSTER_X86_FEATURE(AVX512F) :
           (target_features & evex) == evex                                      ? evex :
           bytes == 16 && (target_features & BUSTER_X86_FEATURE(AVX)) == 0 ? 0 :
                                                                                   BUSTER_X86_FEATURE(AVX);
}

static IrSimdTypedStatus ir_simd_typed_operation(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    // The typed stream's operators' EVEX forms (c_check_program_vectors.c), `op v0, v0, v1` at each width, by their lanes
    // (#141's element types): 8-, 16-, 32- and 64-bit integers' additions and subtractions, the 16-, 32- and 64-bit
    // ones' products and shifts by each lane's count, the floats' and doubles' arithmetic, and the bitwise operations of
    // any lanes. Each form's features are its own (x86_target_requirement: AVX512BW's bytes and words, DQ's vpmullq).
    static const IrSimdTypedForm binary[] = {
        { { X86_FORM_VPADDD_X_X_XM128_EVEX, X86_FORM_VPADDD_Y_Y_YM256_EVEX, X86_FORM_VPADDD_Z_Z_ZM512 }, IR_SIMD_ADD, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPSUBD_X_X_XM128_EVEX, X86_FORM_VPSUBD_Y_Y_YM256_EVEX, X86_FORM_VPSUBD_Z_Z_ZM512 }, IR_SIMD_SUB, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPMULLD_X_X_XM128_EVEX, X86_FORM_VPMULLD_Y_Y_YM256_EVEX, X86_FORM_VPMULLD_Z_Z_ZM512 }, IR_SIMD_MUL, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPANDQ_X_X_XM128, X86_FORM_VPANDQ_Y_Y_YM256, X86_FORM_VPANDQ_Z_Z_ZM512 }, IR_SIMD_AND, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPORQ_X_X_XM128, X86_FORM_VPORQ_Y_Y_YM256, X86_FORM_VPORQ_Z_Z_ZM512 }, IR_SIMD_OR, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPXORQ_X_X_XM128, X86_FORM_VPXORQ_Y_Y_YM256, X86_FORM_VPXORQ_Z_Z_ZM512 }, IR_SIMD_XOR, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPADDB_X_X_XM128_EVEX, X86_FORM_VPADDB_Y_Y_YM256_EVEX, X86_FORM_VPADDB_Z_Z_ZM512 }, IR_SIMD_ADD, IR_SIMD_LANE_I8 },
        { { X86_FORM_VPADDW_X_X_XM128_EVEX, X86_FORM_VPADDW_Y_Y_YM256_EVEX, X86_FORM_VPADDW_Z_Z_ZM512 }, IR_SIMD_ADD, IR_SIMD_LANE_I16 },
        { { X86_FORM_VPADDQ_X_X_XM128_EVEX, X86_FORM_VPADDQ_Y_Y_YM256_EVEX, X86_FORM_VPADDQ_Z_Z_ZM512 }, IR_SIMD_ADD, IR_SIMD_LANE_I64 },
        { { X86_FORM_VPSUBB_X_X_XM128_EVEX, X86_FORM_VPSUBB_Y_Y_YM256_EVEX, X86_FORM_VPSUBB_Z_Z_ZM512 }, IR_SIMD_SUB, IR_SIMD_LANE_I8 },
        { { X86_FORM_VPSUBW_X_X_XM128_EVEX, X86_FORM_VPSUBW_Y_Y_YM256_EVEX, X86_FORM_VPSUBW_Z_Z_ZM512 }, IR_SIMD_SUB, IR_SIMD_LANE_I16 },
        { { X86_FORM_VPSUBQ_X_X_XM128_EVEX, X86_FORM_VPSUBQ_Y_Y_YM256_EVEX, X86_FORM_VPSUBQ_Z_Z_ZM512 }, IR_SIMD_SUB, IR_SIMD_LANE_I64 },
        { { X86_FORM_VPMULLW_X_X_XM128_EVEX, X86_FORM_VPMULLW_Y_Y_YM256_EVEX, X86_FORM_VPMULLW_Z_Z_ZM512 }, IR_SIMD_MUL, IR_SIMD_LANE_I16 },
        { { X86_FORM_VPMULLQ_X_X_XM128, X86_FORM_VPMULLQ_Y_Y_YM256, X86_FORM_VPMULLQ_Z_Z_ZM512 }, IR_SIMD_MUL, IR_SIMD_LANE_I64 },
        { { X86_FORM_VPSLLVW_X_X_XM128, X86_FORM_VPSLLVW_Y_Y_YM256, X86_FORM_VPSLLVW_Z_Z_ZM512 }, IR_SIMD_SHL, IR_SIMD_LANE_I16 },
        { { X86_FORM_VPSLLVD_X_X_XM128_EVEX, X86_FORM_VPSLLVD_Y_Y_YM256_EVEX, X86_FORM_VPSLLVD_Z_Z_ZM512 }, IR_SIMD_SHL, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPSLLVQ_X_X_XM128_EVEX, X86_FORM_VPSLLVQ_Y_Y_YM256_EVEX, X86_FORM_VPSLLVQ_Z_Z_ZM512 }, IR_SIMD_SHL, IR_SIMD_LANE_I64 },
        { { X86_FORM_VPSRLVW_X_X_XM128, X86_FORM_VPSRLVW_Y_Y_YM256, X86_FORM_VPSRLVW_Z_Z_ZM512 }, IR_SIMD_LSHR, IR_SIMD_LANE_I16 },
        { { X86_FORM_VPSRLVD_X_X_XM128_EVEX, X86_FORM_VPSRLVD_Y_Y_YM256_EVEX, X86_FORM_VPSRLVD_Z_Z_ZM512 }, IR_SIMD_LSHR, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPSRLVQ_X_X_XM128_EVEX, X86_FORM_VPSRLVQ_Y_Y_YM256_EVEX, X86_FORM_VPSRLVQ_Z_Z_ZM512 }, IR_SIMD_LSHR, IR_SIMD_LANE_I64 },
        { { X86_FORM_VPSRAVW_X_X_XM128, X86_FORM_VPSRAVW_Y_Y_YM256, X86_FORM_VPSRAVW_Z_Z_ZM512 }, IR_SIMD_ASHR, IR_SIMD_LANE_I16 },
        { { X86_FORM_VPSRAVD_X_X_XM128_EVEX, X86_FORM_VPSRAVD_Y_Y_YM256_EVEX, X86_FORM_VPSRAVD_Z_Z_ZM512 }, IR_SIMD_ASHR, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPSRAVQ_X_X_XM128, X86_FORM_VPSRAVQ_Y_Y_YM256, X86_FORM_VPSRAVQ_Z_Z_ZM512 }, IR_SIMD_ASHR, IR_SIMD_LANE_I64 },
        { { X86_FORM_VADDPS_X_X_XM128_EVEX, X86_FORM_VADDPS_Y_Y_YM256_EVEX, X86_FORM_VADDPS_Z_Z_ZM512 }, IR_SIMD_FADD, IR_SIMD_LANE_F32 },
        { { X86_FORM_VADDPD_X_X_XM128_EVEX, X86_FORM_VADDPD_Y_Y_YM256_EVEX, X86_FORM_VADDPD_Z_Z_ZM512 }, IR_SIMD_FADD, IR_SIMD_LANE_F64 },
        { { X86_FORM_VSUBPS_X_X_XM128_EVEX, X86_FORM_VSUBPS_Y_Y_YM256_EVEX, X86_FORM_VSUBPS_Z_Z_ZM512 }, IR_SIMD_FSUB, IR_SIMD_LANE_F32 },
        { { X86_FORM_VSUBPD_X_X_XM128_EVEX, X86_FORM_VSUBPD_Y_Y_YM256_EVEX, X86_FORM_VSUBPD_Z_Z_ZM512 }, IR_SIMD_FSUB, IR_SIMD_LANE_F64 },
        { { X86_FORM_VMULPS_X_X_XM128_EVEX, X86_FORM_VMULPS_Y_Y_YM256_EVEX, X86_FORM_VMULPS_Z_Z_ZM512 }, IR_SIMD_FMUL, IR_SIMD_LANE_F32 },
        { { X86_FORM_VMULPD_X_X_XM128_EVEX, X86_FORM_VMULPD_Y_Y_YM256_EVEX, X86_FORM_VMULPD_Z_Z_ZM512 }, IR_SIMD_FMUL, IR_SIMD_LANE_F64 },
        { { X86_FORM_VDIVPS_X_X_XM128_EVEX, X86_FORM_VDIVPS_Y_Y_YM256_EVEX, X86_FORM_VDIVPS_Z_Z_ZM512 }, IR_SIMD_FDIV, IR_SIMD_LANE_F32 },
        { { X86_FORM_VDIVPD_X_X_XM128_EVEX, X86_FORM_VDIVPD_Y_Y_YM256_EVEX, X86_FORM_VDIVPD_Z_Z_ZM512 }, IR_SIMD_FDIV, IR_SIMD_LANE_F64 },
    };
    static const uint16_t complement[3] = { X86_FORM_VPTERNLOGQ_X_X_XM128_IMM8, X86_FORM_VPTERNLOGQ_Y_Y_YM256_IMM8, X86_FORM_VPTERNLOGQ_Z_Z_ZM512_IMM8 };
    // A comparison's opmask and its expansion to -1 and 0 lanes (#717's C comparisons), by its lanes: signed and unsigned
    // integers' vpcmp and vpcmpu of each width, floats' and doubles' vcmpps and vcmppd (unsigned_lanes 0), and the
    // expansion of the lanes' width, vpmovm2b to vpmovm2q (a float's lanes are doublewords, a double's quadwords).
    static const uint16_t comparisons[IR_SIMD_LANE_COUNT][2][3] = {
        [IR_SIMD_LANE_I32] = { { X86_FORM_VPCMPD_K_X_XM128_IMM8, X86_FORM_VPCMPD_K_Y_YM256_IMM8, X86_FORM_VPCMPD_K_Z_ZM512_IMM8 },
                               { X86_FORM_VPCMPUD_K_X_XM128_IMM8, X86_FORM_VPCMPUD_K_Y_YM256_IMM8, X86_FORM_VPCMPUD_K_Z_ZM512_IMM8 } },
        [IR_SIMD_LANE_I8] = { { X86_FORM_VPCMPB_K_X_XM128_IMM8, X86_FORM_VPCMPB_K_Y_YM256_IMM8, X86_FORM_VPCMPB_K_Z_ZM512_IMM8 },
                              { X86_FORM_VPCMPUB_K_X_XM128_IMM8, X86_FORM_VPCMPUB_K_Y_YM256_IMM8, X86_FORM_VPCMPUB_K_Z_ZM512_IMM8 } },
        [IR_SIMD_LANE_I16] = { { X86_FORM_VPCMPW_K_X_XM128_IMM8, X86_FORM_VPCMPW_K_Y_YM256_IMM8, X86_FORM_VPCMPW_K_Z_ZM512_IMM8 },
                               { X86_FORM_VPCMPUW_K_X_XM128_IMM8, X86_FORM_VPCMPUW_K_Y_YM256_IMM8, X86_FORM_VPCMPUW_K_Z_ZM512_IMM8 } },
        [IR_SIMD_LANE_I64] = { { X86_FORM_VPCMPQ_K_X_XM128_IMM8, X86_FORM_VPCMPQ_K_Y_YM256_IMM8, X86_FORM_VPCMPQ_K_Z_ZM512_IMM8 },
                               { X86_FORM_VPCMPUQ_K_X_XM128_IMM8, X86_FORM_VPCMPUQ_K_Y_YM256_IMM8, X86_FORM_VPCMPUQ_K_Z_ZM512_IMM8 } },
        [IR_SIMD_LANE_F32] = { { X86_FORM_VCMPPS_K_X_XM128_IMM8, X86_FORM_VCMPPS_K_Y_YM256_IMM8, X86_FORM_VCMPPS_K_Z_ZM512_IMM8 },
                               { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE } },
        [IR_SIMD_LANE_F64] = { { X86_FORM_VCMPPD_K_X_XM128_IMM8, X86_FORM_VCMPPD_K_Y_YM256_IMM8, X86_FORM_VCMPPD_K_Z_ZM512_IMM8 },
                               { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_NONE } },
    };
    static const uint16_t expansions[IR_SIMD_LANE_COUNT][3] = {
        [IR_SIMD_LANE_I32] = { X86_FORM_VPMOVM2D_X_K, X86_FORM_VPMOVM2D_Y_K, X86_FORM_VPMOVM2D_Z_K },
        [IR_SIMD_LANE_I8] = { X86_FORM_VPMOVM2B_X_K, X86_FORM_VPMOVM2B_Y_K, X86_FORM_VPMOVM2B_Z_K },
        [IR_SIMD_LANE_I16] = { X86_FORM_VPMOVM2W_X_K, X86_FORM_VPMOVM2W_Y_K, X86_FORM_VPMOVM2W_Z_K },
        [IR_SIMD_LANE_I64] = { X86_FORM_VPMOVM2Q_X_K, X86_FORM_VPMOVM2Q_Y_K, X86_FORM_VPMOVM2Q_Z_K },
        [IR_SIMD_LANE_F32] = { X86_FORM_VPMOVM2D_X_K, X86_FORM_VPMOVM2D_Y_K, X86_FORM_VPMOVM2D_Z_K },
        [IR_SIMD_LANE_F64] = { X86_FORM_VPMOVM2Q_X_K, X86_FORM_VPMOVM2Q_Y_K, X86_FORM_VPMOVM2Q_Z_K },
    };
    // The same operations' VEX forms at targets without AVX-512 (#1281), as the typed stream's rows have them there
    // (c_check_program_vectors_baseline.c and the builtins' rows): AVX's of an xmm register and AVX2's of a ymm one (AVX's
    // for the floating arithmetic), and for the bitwise ones the packed singles' forms as well as the integers', whose
    // bits are the same. Tier 1 computes them by its VEX forms (tier1_select_vector_forms).
    static const IrSimdTypedForm vex_binary[] = {
        { { X86_FORM_VPADDD_X_X_XM128, X86_FORM_VPADDD_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_ADD, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPSUBD_X_X_XM128, X86_FORM_VPSUBD_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_SUB, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPMULLD_X_X_XM128, X86_FORM_VPMULLD_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_MUL, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPAND_X_X_XM128, X86_FORM_VPAND_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_AND, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPOR_X_X_XM128, X86_FORM_VPOR_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_OR, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPXOR_X_X_XM128, X86_FORM_VPXOR_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_XOR, IR_SIMD_LANE_I32 },
        { { X86_FORM_VANDPS_X_X_XM128, X86_FORM_VANDPS_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_AND, IR_SIMD_LANE_I32 },
        { { X86_FORM_VORPS_X_X_XM128, X86_FORM_VORPS_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_OR, IR_SIMD_LANE_I32 },
        { { X86_FORM_VXORPS_X_X_XM128, X86_FORM_VXORPS_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_XOR, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPADDB_X_X_XM128, X86_FORM_VPADDB_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_ADD, IR_SIMD_LANE_I8 },
        { { X86_FORM_VPADDW_X_X_XM128, X86_FORM_VPADDW_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_ADD, IR_SIMD_LANE_I16 },
        { { X86_FORM_VPADDQ_X_X_XM128, X86_FORM_VPADDQ_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_ADD, IR_SIMD_LANE_I64 },
        { { X86_FORM_VPSUBB_X_X_XM128, X86_FORM_VPSUBB_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_SUB, IR_SIMD_LANE_I8 },
        { { X86_FORM_VPSUBW_X_X_XM128, X86_FORM_VPSUBW_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_SUB, IR_SIMD_LANE_I16 },
        { { X86_FORM_VPSUBQ_X_X_XM128, X86_FORM_VPSUBQ_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_SUB, IR_SIMD_LANE_I64 },
        { { X86_FORM_VPMULLW_X_X_XM128, X86_FORM_VPMULLW_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_MUL, IR_SIMD_LANE_I16 },
        { { X86_FORM_VPSLLVD_X_X_XM128, X86_FORM_VPSLLVD_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_SHL, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPSLLVQ_X_X_XM128, X86_FORM_VPSLLVQ_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_SHL, IR_SIMD_LANE_I64 },
        { { X86_FORM_VPSRLVD_X_X_XM128, X86_FORM_VPSRLVD_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_LSHR, IR_SIMD_LANE_I32 },
        { { X86_FORM_VPSRLVQ_X_X_XM128, X86_FORM_VPSRLVQ_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_LSHR, IR_SIMD_LANE_I64 },
        { { X86_FORM_VPSRAVD_X_X_XM128, X86_FORM_VPSRAVD_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_ASHR, IR_SIMD_LANE_I32 },
        { { X86_FORM_VADDPS_X_X_XM128, X86_FORM_VADDPS_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_FADD, IR_SIMD_LANE_F32 },
        { { X86_FORM_VADDPD_X_X_XM128, X86_FORM_VADDPD_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_FADD, IR_SIMD_LANE_F64 },
        { { X86_FORM_VSUBPS_X_X_XM128, X86_FORM_VSUBPS_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_FSUB, IR_SIMD_LANE_F32 },
        { { X86_FORM_VSUBPD_X_X_XM128, X86_FORM_VSUBPD_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_FSUB, IR_SIMD_LANE_F64 },
        { { X86_FORM_VMULPS_X_X_XM128, X86_FORM_VMULPS_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_FMUL, IR_SIMD_LANE_F32 },
        { { X86_FORM_VMULPD_X_X_XM128, X86_FORM_VMULPD_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_FMUL, IR_SIMD_LANE_F64 },
        { { X86_FORM_VDIVPS_X_X_XM128, X86_FORM_VDIVPS_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_FDIV, IR_SIMD_LANE_F32 },
        { { X86_FORM_VDIVPD_X_X_XM128, X86_FORM_VDIVPD_Y_Y_YM256, X86_FORM_NONE }, IR_SIMD_FDIV, IR_SIMD_LANE_F64 },
    };
    // The same operations' legacy SSE forms at targets without AVX (#141's SSE targets), as
    // c_check_program_vectors_baseline.c's rows have them: two operands, `op v0, v1`, the destination's the first, at 16
    // bytes alone; SSE2's but pmulld, SSE4.1's (its form's feature), and the bitwise operations' packed singles' forms
    // (andps, orps, xorps) as the rows take them, the same bits. No shift by each lane's count, which is AVX2's.
    static const IrSimdTypedForm sse_binary[] = {
        { { X86_FORM_PADDB_X_XM128 }, IR_SIMD_ADD, IR_SIMD_LANE_I8 },
        { { X86_FORM_PADDW_X_XM128 }, IR_SIMD_ADD, IR_SIMD_LANE_I16 },
        { { X86_FORM_PADDD_X_XM128 }, IR_SIMD_ADD, IR_SIMD_LANE_I32 },
        { { X86_FORM_PADDQ_X_XM128 }, IR_SIMD_ADD, IR_SIMD_LANE_I64 },
        { { X86_FORM_PSUBB_X_XM128 }, IR_SIMD_SUB, IR_SIMD_LANE_I8 },
        { { X86_FORM_PSUBW_X_XM128 }, IR_SIMD_SUB, IR_SIMD_LANE_I16 },
        { { X86_FORM_PSUBD_X_XM128 }, IR_SIMD_SUB, IR_SIMD_LANE_I32 },
        { { X86_FORM_PSUBQ_X_XM128 }, IR_SIMD_SUB, IR_SIMD_LANE_I64 },
        { { X86_FORM_PMULLW_X_XM128 }, IR_SIMD_MUL, IR_SIMD_LANE_I16 },
        { { X86_FORM_PMULLD_X_XM128 }, IR_SIMD_MUL, IR_SIMD_LANE_I32 },
        { { X86_FORM_PAND_X_XM128 }, IR_SIMD_AND, IR_SIMD_LANE_I32 },
        { { X86_FORM_POR_X_XM128 }, IR_SIMD_OR, IR_SIMD_LANE_I32 },
        { { X86_FORM_PXOR_X_XM128 }, IR_SIMD_XOR, IR_SIMD_LANE_I32 },
        { { X86_FORM_ANDPS_X_XM128 }, IR_SIMD_AND, IR_SIMD_LANE_I32 },
        { { X86_FORM_ORPS_X_XM128 }, IR_SIMD_OR, IR_SIMD_LANE_I32 },
        { { X86_FORM_XORPS_X_XM128 }, IR_SIMD_XOR, IR_SIMD_LANE_I32 },
        { { X86_FORM_ADDPS_X_XM128 }, IR_SIMD_FADD, IR_SIMD_LANE_F32 },
        { { X86_FORM_ADDPD_X_XM128 }, IR_SIMD_FADD, IR_SIMD_LANE_F64 },
        { { X86_FORM_SUBPS_X_XM128 }, IR_SIMD_FSUB, IR_SIMD_LANE_F32 },
        { { X86_FORM_SUBPD_X_XM128 }, IR_SIMD_FSUB, IR_SIMD_LANE_F64 },
        { { X86_FORM_MULPS_X_XM128 }, IR_SIMD_FMUL, IR_SIMD_LANE_F32 },
        { { X86_FORM_MULPD_X_XM128 }, IR_SIMD_FMUL, IR_SIMD_LANE_F64 },
        { { X86_FORM_DIVPS_X_XM128 }, IR_SIMD_FDIV, IR_SIMD_LANE_F32 },
        { { X86_FORM_DIVPD_X_XM128 }, IR_SIMD_FDIV, IR_SIMD_LANE_F64 },
    };
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_UNSUPPORTED : IR_SIMD_TYPED_INVALID;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    uint32_t width = 0;
    if (status != IR_SIMD_TYPED_INVALID && source->result_class == BUSTER_TYPED_CLASS_VECTOR &&
        (source->result_bytes == 16 || source->result_bytes == 32 || source->result_bytes == 64) &&
        (source->operand_count == 1 || source->operand_count == 2) && source->record_count != 0 && source->record_count <= 2)
    {
        width = source->result_bytes == 16 ? 0 : source->result_bytes == 32 ? 1 : 2;
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        // Every form here is EVEX's: AVX512F, and AVX512VL for its 128- and 256-bit forms alone; VPMOVM2D adds AVX512DQ
        // (below).
        candidate.required_features = BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0);
        bool operands = true;
        for (uint32_t operand = 0; operand < source->operand_count; operand += 1)
        {
            operands &= source->operand_classes[operand] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[operand] == source->result_bytes;
        }
        if (operands)
        {
            const X86Instruction* record = source->records;
            const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
            const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
            X86Instruction expected = { .form = record->form, .reg = v0, .vvvv = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT };
            // A legacy form's row: its destination its first operand, no vvvv.
            const X86Instruction legacy = { .form = record->form, .reg = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT };
            if (source->record_count == 1 && source->operand_count == 2 && width == 0 && memcmp(record, &legacy, sizeof(legacy)) == 0)
            {
                for (uint32_t form = 0; form < sizeof(sse_binary) / sizeof(*sse_binary); form += 1)
                {
                    if (record->form == sse_binary[form].forms[0])
                    {
                        candidate.operation = sse_binary[form].operation;
                        candidate.lane = sse_binary[form].lane;
                        candidate.required_features = ir_typed_vector_moves(16, target_features) | (x86_target_requirement(record->form) & ~BUSTER_X86_TARGET_KNOWN);
                    }
                }
            }
            else if (source->record_count == 1 && source->operand_count == 2 && memcmp(record, &expected, sizeof(expected)) == 0)
            {
                for (uint32_t form = 0; form < sizeof(binary) / sizeof(*binary); form += 1)
                {
                    if (record->form == binary[form].forms[width])
                    {
                        // Its form's features (AVX512BW for bytes and words, DQ for vpmullq), past its moves' (AVX512F,
                        // and VL for the xmm and ymm forms).
                        candidate.operation = binary[form].operation;
                        candidate.lane = binary[form].lane;
                        candidate.required_features |= x86_target_requirement(record->form) & ~BUSTER_X86_TARGET_KNOWN;
                    }
                }
                for (uint32_t form = 0; width < 2 && form < sizeof(vex_binary) / sizeof(*vex_binary); form += 1)
                {
                    if (record->form == vex_binary[form].forms[width])
                    {
                        // Its form's features (AVX, or AVX2 for a ymm register's integers), and its moves'.
                        candidate.operation = vex_binary[form].operation;
                        candidate.lane = vex_binary[form].lane;
                        candidate.required_features = ir_typed_vector_moves(source->result_bytes, target_features) | (x86_target_requirement(record->form) & ~BUSTER_X86_TARGET_KNOWN);
                    }
                }
            }
            else if (source->record_count == 1 && source->operand_count == 1)
            {
                expected = (X86Instruction){ .form = complement[width], .reg = v0, .vvvv = v0, .rm = v0, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = 0x55 };
                if (memcmp(record, &expected, sizeof(expected)) == 0)
                {
                    candidate.operation = IR_SIMD_NOT;
                }
            }
            else if (source->record_count == 2 && source->operand_count == 2)
            {
                // A comparison of any lanes (#141's element types): its form names them, and its expansion has their
                // width. Integer predicates as vpcmp's immediates (0 ==, 1 <, 2 <=, 4 !=, 5 >=, 6 >), floating ones as
                // the rows' vcmpps (EQ_OQ, NEQ_UQ, LT_OS, LE_OS, GT_OS and GE_OS: C's ==, !=, <, <=, > and >=).
                for (uint32_t lane = 0; lane < IR_SIMD_LANE_COUNT; lane += 1)
                {
                    expected = (X86Instruction){ .form = expansions[lane][width], .reg = v0, .rm = 1, .mode = BUSTER_X86_REGISTER_DIRECT };
                    const bool expansion = memcmp(record + 1, &expected, sizeof(expected)) == 0;
                    const bool floating = ir_simd_lane_floating((IrSimdLane)lane);
                    for (uint32_t unsigned_lanes = 0; expansion && unsigned_lanes < (floating ? 1u : 2u); unsigned_lanes += 1)
                    {
                        expected = (X86Instruction){ .form = comparisons[lane][unsigned_lanes][width], .reg = 1, .vvvv = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = record->immediate };
                        if (memcmp(record, &expected, sizeof(expected)) == 0)
                        {
                            bool predicate = true;
                            switch (floating ? record->immediate + 0x100 : record->immediate)
                            {
                                case 0: candidate.predicate = IR_SIMD_EQ; break;
                                case 4: candidate.predicate = IR_SIMD_NE; break;
                                case 1: candidate.predicate = unsigned_lanes != 0 ? IR_SIMD_ULT : IR_SIMD_LT; break;
                                case 2: candidate.predicate = unsigned_lanes != 0 ? IR_SIMD_ULE : IR_SIMD_LE; break;
                                case 6: candidate.predicate = unsigned_lanes != 0 ? IR_SIMD_UGT : IR_SIMD_GT; break;
                                case 5: candidate.predicate = unsigned_lanes != 0 ? IR_SIMD_UGE : IR_SIMD_GE; break;
                                case 0x100: candidate.predicate = IR_SIMD_EQ; break;
                                case 0x104: candidate.predicate = IR_SIMD_NE; break;
                                case 0x101: candidate.predicate = IR_SIMD_LT; break;
                                case 0x102: candidate.predicate = IR_SIMD_LE; break;
                                case 0x10e: candidate.predicate = IR_SIMD_GT; break;
                                case 0x10d: candidate.predicate = IR_SIMD_GE; break;
                                default: predicate = false; break;
                            }
                            if (predicate)
                            {
                                candidate.operation = IR_SIMD_COMPARE;
                                candidate.lane = (IrSimdLane)lane;
                                candidate.expand_mask = true;
                                // The compare's and the expansion's own features: AVX512BW's bytes and words, DQ's
                                // doublewords and quadwords (vpmovm2d and vpmovm2q).
                                candidate.required_features |= (x86_target_requirement(record[0].form) | x86_target_requirement(record[1].form)) & ~BUSTER_X86_TARGET_KNOWN;
                            }
                        }
                    }
                }
            }
        }
        if (candidate.operation != IR_SIMD_OPERATION_COUNT)
        {
            status = (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_TARGET;
        }
    }
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A mask row's register for a role (BUSTER_IR_SIMD_MASK_* indices), or BUSTER_IR_SIMD_MASK_REGISTERS for a field that
// isn't one of its roles: an opmask register by number among them, which a row with masks doesn't name.
static uint32_t ir_simd_typed_mask_register(const Tier0Operation* source, uint8_t role)
{
    uint32_t reg = BUSTER_IR_SIMD_MASK_REGISTERS;
    if (role >= BUSTER_TIER0_ROLE_MASK(0) && role < BUSTER_TIER0_ROLE_MASK(source->operand_count))
    {
        reg = role & 7;
    }
    else if (role == BUSTER_TIER0_ROLE_MASK_RESULT)
    {
        reg = source->operand_count != 0 ? 0 : BUSTER_IR_SIMD_MASK_RESULT;
    }
    else if (role >= BUSTER_TIER0_ROLE_MASK_SCRATCH(0) && role < BUSTER_TIER0_ROLE_MASK_SCRATCH(BUSTER_TIER0_SCRATCH))
    {
        reg = BUSTER_IR_SIMD_MASK_SCRATCH + (role & 3u);
    }
    return reg;
}

// A row of opmask instructions alone (#179): masks of one width w in and out (1, 2, 4 or 8 bytes), each record one of
// kand, kor, kxor, kxnor, kandn, kadd, kunpck, knot and kshiftl or kshiftr at w, or a kmov between registers at w or
// wider, which copies a mask whole (its bits past w are zero), its fields its roles alone, register-direct, without a
// decoration or another field, every register it reads written before. Its result is its first operand's register's, or
// MASK_RESULT's, at its end. These are the _k* builtins' rows (check_x86_lower_masks's): KBINARY's kandw MASK(0),
// MASK(0), MASK(1), KMOV's with no record. Its features are its forms': AVX-512F, DQ for the byte forms and kaddw, BW
// for the doubleword and quadword ones, kunpckwd and kunpckdq. Any other row, a vector's or a flags test's among them,
// is IR_SIMD_TYPED_UNSUPPORTED.
static IrSimdTypedStatus ir_simd_typed_mask_row(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint16_t forms[IR_SIMD_MASK_KINDS][4] = {
        [IR_SIMD_MASK_AND] = { X86_FORM_KANDB_K_K_K, X86_FORM_KANDW_K_K_K, X86_FORM_KANDD_K_K_K, X86_FORM_KANDQ_K_K_K },
        [IR_SIMD_MASK_OR] = { X86_FORM_KORB_K_K_K, X86_FORM_KORW_K_K_K, X86_FORM_KORD_K_K_K, X86_FORM_KORQ_K_K_K },
        [IR_SIMD_MASK_XOR] = { X86_FORM_KXORB_K_K_K, X86_FORM_KXORW_K_K_K, X86_FORM_KXORD_K_K_K, X86_FORM_KXORQ_K_K_K },
        [IR_SIMD_MASK_XNOR] = { X86_FORM_KXNORB_K_K_K, X86_FORM_KXNORW_K_K_K, X86_FORM_KXNORD_K_K_K, X86_FORM_KXNORQ_K_K_K },
        [IR_SIMD_MASK_ANDN] = { X86_FORM_KANDNB_K_K_K, X86_FORM_KANDNW_K_K_K, X86_FORM_KANDND_K_K_K, X86_FORM_KANDNQ_K_K_K },
        [IR_SIMD_MASK_ADD] = { X86_FORM_KADDB_K_K_K, X86_FORM_KADDW_K_K_K, X86_FORM_KADDD_K_K_K, X86_FORM_KADDQ_K_K_K },
        [IR_SIMD_MASK_UNPACK] = { X86_FORM_NONE, X86_FORM_KUNPCKBW_K_K_K, X86_FORM_KUNPCKWD_K_K_K, X86_FORM_KUNPCKDQ_K_K_K },
        [IR_SIMD_MASK_NOT] = { X86_FORM_KNOTB_K_K, X86_FORM_KNOTW_K_K, X86_FORM_KNOTD_K_K, X86_FORM_KNOTQ_K_K },
        [IR_SIMD_MASK_MOVE] = { X86_FORM_KMOVB_K_K, X86_FORM_KMOVW_K_K, X86_FORM_KMOVD_K_K, X86_FORM_KMOVQ_K_K },
        [IR_SIMD_MASK_SHIFT_LEFT] = { X86_FORM_KSHIFTLB_K_K_IMM8, X86_FORM_KSHIFTLW_K_K_IMM8, X86_FORM_KSHIFTLD_K_K_IMM8, X86_FORM_KSHIFTLQ_K_K_IMM8 },
        [IR_SIMD_MASK_SHIFT_RIGHT] = { X86_FORM_KSHIFTRB_K_K_IMM8, X86_FORM_KSHIFTRW_K_K_IMM8, X86_FORM_KSHIFTRD_K_K_IMM8, X86_FORM_KSHIFTRQ_K_K_IMM8 },
    };
    const uint32_t bytes = source != NULL ? source->result_bytes : 0;
    const uint32_t width = bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_INVALID;
    bool admitted = status == IR_SIMD_TYPED_OK && source->result_class == BUSTER_TYPED_CLASS_MASK && (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8) &&
                    source->operand_count <= BUSTER_TIER0_OPERATION_OPERANDS && source->record_count <= BUSTER_TIER0_OPERATION_RECORDS;
    for (uint32_t j = 0; admitted && j < source->operand_count; j += 1)
    {
        admitted = source->operand_classes[j] == BUSTER_TYPED_CLASS_MASK && source->operand_bytes[j] == bytes;
    }
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT, .mask_bytes = (uint8_t)bytes };
    candidate.required_features = BUSTER_X86_FEATURE(AVX512F);
    uint32_t defined = admitted ? (1u << source->operand_count) - 1u : 0;
    for (uint32_t r = 0; admitted && r < source->record_count; r += 1)
    {
        const X86Instruction* record = &source->records[r];
        uint32_t kind = IR_SIMD_MASK_KINDS;
        uint32_t form_width = 4;
        for (uint32_t k = 0; k < IR_SIMD_MASK_KINDS; k += 1)
        {
            for (uint32_t w = 0; w < 4; w += 1)
            {
                if (forms[k][w] != X86_FORM_NONE && record->form == forms[k][w])
                {
                    kind = k;
                    form_width = w;
                }
            }
        }
        const bool binary = kind <= IR_SIMD_MASK_UNPACK;
        const bool shift = kind == IR_SIMD_MASK_SHIFT_LEFT || kind == IR_SIMD_MASK_SHIFT_RIGHT;
        // A move copies a mask whole at its width or wider; every other record is the masks' width.
        admitted = kind != IR_SIMD_MASK_KINDS && (kind == IR_SIMD_MASK_MOVE ? form_width >= width : form_width == width);
        const X86Instruction expected = {
            .form = record->form,
            .reg = record->reg,
            .vvvv = binary ? record->vvvv : 0,
            .rm = record->rm,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .immediate = shift ? record->immediate : 0,
        };
        admitted = admitted && memcmp(record, &expected, sizeof(expected)) == 0 && (!shift || (record->immediate >= 0 && record->immediate <= 255));
        const uint32_t destination = ir_simd_typed_mask_register(source, record->reg);
        const uint32_t left = ir_simd_typed_mask_register(source, binary ? record->vvvv : record->rm);
        const uint32_t right = binary ? ir_simd_typed_mask_register(source, record->rm) : left;
        admitted = admitted && destination < BUSTER_IR_SIMD_MASK_REGISTERS && left < BUSTER_IR_SIMD_MASK_REGISTERS && right < BUSTER_IR_SIMD_MASK_REGISTERS &&
                   ((defined >> left) & (defined >> right) & 1u) != 0;
        if (admitted)
        {
            candidate.mask_steps[candidate.mask_step_count] = (IrSimdTypedMaskStep){
                .kind = (uint8_t)kind,
                .destination = (uint8_t)destination,
                .left = (uint8_t)left,
                .right = (uint8_t)right,
                .count = (uint8_t)(shift ? record->immediate : 0),
            };
            candidate.mask_step_count += 1;
            defined |= 1u << destination;
            candidate.required_features |= form_width == 0 || (kind == IR_SIMD_MASK_ADD && form_width == 1) ? BUSTER_X86_FEATURE(AVX512DQ) : 0;
            candidate.required_features |= form_width >= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
        }
    }
    const uint32_t returned = admitted && source->operand_count != 0 ? 0 : BUSTER_IR_SIMD_MASK_RESULT;
    admitted = admitted && ((defined >> returned) & 1u) != 0;
    if (status == IR_SIMD_TYPED_OK)
    {
        status = !admitted ? IR_SIMD_TYPED_UNSUPPORTED : (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_TARGET;
    }
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.mask_result = (uint8_t)returned;
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A row of two masks of one width w (1, 2, 4 or 8 bytes) and an int (#179): ktest or kortest at w of MASK(0) and MASK(1),
// in either order, register-direct, then sete or setb into SCRATCH(0)'s byte and its zero extension into INTEGER_RESULT,
// each record with no other field. These are the kortestz, kortestc, ktestz and ktestc builtins' rows
// (check_x86_lower_masks's of KTEST's): its step is IR_SIMD_MASK_TEST's or IR_SIMD_MASK_ORTEST's of its flag. Its
// features are its form's: AVX-512F, DQ for ktestb, ktestw and kortestb, BW for the doubleword and quadword forms. Any
// other row of mask operands and an integer result is IR_SIMD_TYPED_UNSUPPORTED.
static IrSimdTypedStatus ir_simd_typed_mask_test(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint16_t forms[2][4] = {
        { X86_FORM_KTESTB_K_K, X86_FORM_KTESTW_K_K, X86_FORM_KTESTD_K_K, X86_FORM_KTESTQ_K_K },
        { X86_FORM_KORTESTB_K_K, X86_FORM_KORTESTW_K_K, X86_FORM_KORTESTD_K_K, X86_FORM_KORTESTQ_K_K },
    };
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_INVALID;
    const uint32_t bytes = status == IR_SIMD_TYPED_OK ? source->operand_bytes[0] : 0;
    const uint32_t width = bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
    bool admitted = status == IR_SIMD_TYPED_OK && source->result_class == BUSTER_TYPED_CLASS_I32 && source->result_bytes == 0 && source->operand_count == 2 &&
                    source->record_count == 3 && (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8);
    for (uint32_t j = 0; admitted && j < 2; j += 1)
    {
        admitted = source->operand_classes[j] == BUSTER_TYPED_CLASS_MASK && source->operand_bytes[j] == bytes;
    }
    const X86Instruction* records = source != NULL ? source->records : NULL;
    const uint8_t m0 = BUSTER_TIER0_ROLE_MASK(0);
    const uint8_t m1 = BUSTER_TIER0_ROLE_MASK(1);
    const uint32_t kind = admitted && records[0].form == forms[0][width] ? 0 : admitted && records[0].form == forms[1][width] ? 1 : 2;
    const bool swapped = admitted && records[0].reg == m1;
    const X86Instruction test = { .form = records != NULL ? records[0].form : 0, .reg = swapped ? m1 : m0, .rm = swapped ? m0 : m1, .mode = BUSTER_X86_REGISTER_DIRECT };
    const uint16_t set = admitted ? records[1].form : 0;
    const X86Instruction condition = { .form = set, .rm = BUSTER_TIER0_ROLE_SCRATCH(0), .mode = BUSTER_X86_REGISTER_DIRECT };
    const X86Instruction extension = { .form = X86_FORM_MOVZX_R32_RM8, .reg = BUSTER_TIER0_ROLE_INTEGER_RESULT, .rm = BUSTER_TIER0_ROLE_SCRATCH(0), .mode = BUSTER_X86_REGISTER_DIRECT };
    admitted = admitted && kind < 2 && (set == X86_FORM_SETE_RM8 || set == X86_FORM_SETB_RM8) && memcmp(&records[0], &test, sizeof(test)) == 0 &&
               memcmp(&records[1], &condition, sizeof(condition)) == 0 && memcmp(&records[2], &extension, sizeof(extension)) == 0;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT, .mask_bytes = (uint8_t)bytes, .mask_step_count = 1, .mask_result = BUSTER_IR_SIMD_MASK_RESULT };
    candidate.mask_steps[0] = (IrSimdTypedMaskStep){
        .kind = (uint8_t)(kind == 0 ? IR_SIMD_MASK_TEST : IR_SIMD_MASK_ORTEST),
        .destination = BUSTER_IR_SIMD_MASK_RESULT,
        .left = swapped ? 1 : 0,
        .right = swapped ? 0 : 1,
        .count = set == X86_FORM_SETB_RM8 ? 1 : 0,
    };
    const bool dq = kind == 0 ? width <= 1 : width == 0;
    candidate.required_features = BUSTER_X86_FEATURE(AVX512F) | (dq ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (width >= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
    if (status == IR_SIMD_TYPED_OK)
    {
        status = !admitted ? IR_SIMD_TYPED_UNSUPPORTED : (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_TARGET;
    }
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

typedef struct IrSimdTypedIntrinsic IrSimdTypedIntrinsic;
struct IrSimdTypedIntrinsic
{
    uint16_t forms[2];
    uint8_t intrinsic;
    // Whether its source is vvvv's operand (pdep's and pext's, whose mask is rm's), or rm's (bzhi's and bextr's, whose
    // index or control is vvvv's).
    bool source_in_vvvv;
    uint64_t feature;
};

// A row of one BMI1 or BMI2 instruction of two operands of its result's class, an unsigned int's or long's (#141), on
// their registers: the x86 builtins' pdep, pext, bzhi and bextr (c_builtin_x86_rows.inc's GENERAL rows, whose `swap`
// puts bzhi's and bextr's source in rm) and the elementwise deposits' and extractions' (check_lower_vector_deposit), an
// IR intrinsic of the operands its record names in their places.
static IrSimdTypedStatus ir_simd_typed_intrinsic(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const IrSimdTypedIntrinsic intrinsics[] = {
        { { X86_FORM_PDEP_R32_R32_RM32, X86_FORM_PDEP_R64_R64_RM64 }, BUSTER_IR_INTRINSIC_PDEP, true, BUSTER_X86_FEATURE(BMI2) },
        { { X86_FORM_PEXT_R32_R32_RM32, X86_FORM_PEXT_R64_R64_RM64 }, BUSTER_IR_INTRINSIC_PEXT, true, BUSTER_X86_FEATURE(BMI2) },
        { { X86_FORM_BZHI_R32_RM32_R32, X86_FORM_BZHI_R64_RM64_R64 }, BUSTER_IR_INTRINSIC_BZHI, false, BUSTER_X86_FEATURE(BMI2) },
        { { X86_FORM_BEXTR_R32_RM32_R32, X86_FORM_BEXTR_R64_RM64_R64 }, BUSTER_IR_INTRINSIC_BEXTR, false, BUSTER_X86_FEATURE(BMI) },
    };
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_UNSUPPORTED : IR_SIMD_TYPED_INVALID;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    const uint8_t class = source != NULL ? source->result_class : 0;
    if (status != IR_SIMD_TYPED_INVALID && (class == BUSTER_TYPED_CLASS_U32 || class == BUSTER_TYPED_CLASS_U64) && source->result_bytes == 0 && source->record_count == 1 &&
        source->operand_count == 2 && source->operand_classes[0] == class && source->operand_classes[1] == class && source->operand_bytes[0] == 0 && source->operand_bytes[1] == 0)
    {
        const X86Instruction* record = source->records;
        const uint8_t i0 = BUSTER_TIER0_ROLE_INTEGER(0);
        const uint8_t i1 = BUSTER_TIER0_ROLE_INTEGER(1);
        const bool wide = class == BUSTER_TYPED_CLASS_U64;
        for (uint32_t k = 0; k < sizeof(intrinsics) / sizeof(*intrinsics); k += 1)
        {
            for (uint32_t order = 0; order < 2; order += 1)
            {
                const uint8_t first = order == 0 ? i0 : i1;
                const uint8_t second = order == 0 ? i1 : i0;
                const X86Instruction expected = {
                    .form = intrinsics[k].forms[wide],
                    .reg = BUSTER_TIER0_ROLE_INTEGER_RESULT,
                    .vvvv = intrinsics[k].source_in_vvvv ? first : second,
                    .rm = intrinsics[k].source_in_vvvv ? second : first,
                    .mode = BUSTER_X86_REGISTER_DIRECT,
                };
                if (memcmp(record, &expected, sizeof(expected)) == 0)
                {
                    candidate.intrinsic = (uint8_t)(intrinsics[k].intrinsic + 1);
                    candidate.intrinsic_operands[0] = (uint8_t)(first & 1);
                    candidate.intrinsic_operands[1] = (uint8_t)(second & 1);
                    candidate.required_features = intrinsics[k].feature;
                }
            }
        }
        if (candidate.intrinsic != 0)
        {
            status = (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_TARGET;
        }
    }
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A row's decoding: a mask result's by ir_simd_typed_mask_row, an int's of mask operands by ir_simd_typed_mask_test, an
// unsigned int's or long's of integers by ir_simd_typed_intrinsic, any other's by ir_simd_typed_operation.
// Any other X86 row (#141): one whose operands and result are of classes a row call takes (ir_row_type: integers of 1
// to 8 bytes, floats and doubles, vectors of 4, 8, 16, 32 or 64 bytes whatever their elements, masks and, for its
// result, void), IR_SIMD_TYPED_UNSUPPORTED otherwise, and whose records' forms the target has, IR_SIMD_TYPED_TARGET
// otherwise. It becomes the IR's machine row, which a row call runs as Tier 0 runs it: the vector builtins' rows and
// the vector operators' on elements other than 32-bit integers, which Tier 0 makes rows of, as x86 records.
static IrSimdTypedStatus ir_simd_typed_machine_row(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_INVALID;
    bool admitted = status == IR_SIMD_TYPED_OK && source->operand_count <= BUSTER_TIER0_OPERATION_OPERANDS && source->record_count <= BUSTER_TIER0_OPERATION_RECORDS &&
                    ir_row_type(source->result_class, source->result_bytes) != IR_TYPE_COUNT;
    // Its vectors' and masks' own moves, Tier 1's, as any vector value's (ir_typed_vector_moves): AVX512F's for a 64-byte
    // vector, and for a 16- or 32-byte one VL's EVEX ones where the target has them, else AVX's VEX ones (#1281); DQ's for a
    // byte mask and BW's for a doubleword's or a quadword's, as any mask's.
    uint64_t required = 0;
    for (uint32_t j = 0; admitted && j <= source->operand_count; j += 1)
    {
        const uint32_t class = j < source->operand_count ? source->operand_classes[j] : source->result_class;
        const uint32_t bytes = j < source->operand_count ? source->operand_bytes[j] : source->result_bytes;
        const uint32_t type = ir_row_type(class, bytes);
        admitted = type != IR_TYPE_COUNT && (type != IR_VOID || j == source->operand_count);
        // By its class: an SSE carrier's f32 or f64 (#1280) is a vector's too, staged as one (tier1_select_carrier_move).
        required |= class == BUSTER_TYPED_CLASS_VECTOR ? ir_typed_vector_moves(bytes, target_features) : 0;
        required |= ir_mask_type(type) ? BUSTER_X86_FEATURE(AVX512F) | (bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0) : 0;
    }
    // Each form's features, x86_target_requirement's, whose classified rows carry BUSTER_X86_TARGET_KNOWN, which every
    // target has (x86_target_forms).
    for (uint32_t r = 0; admitted && r < source->record_count; r += 1)
    {
        required |= x86_target_requirement(source->records[r].form) & ~BUSTER_X86_TARGET_KNOWN;
    }
    if (status == IR_SIMD_TYPED_OK)
    {
        status = !admitted ? IR_SIMD_TYPED_UNSUPPORTED : (target_features & required) == required ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_TARGET;
    }
    if (status == IR_SIMD_TYPED_OK)
    {
        IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT, .required_features = required, .machine = true };
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

static IrSimdTypedStatus ir_simd_typed_row(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const bool masks = source != NULL && source->operand_count != 0 && source->operand_classes[0] == BUSTER_TYPED_CLASS_MASK && tier0_class_integer(source->result_class);
    const bool scalar = source != NULL && (source->result_class == BUSTER_TYPED_CLASS_U32 || source->result_class == BUSTER_TYPED_CLASS_U64) && !masks;
    const IrSimdTypedStatus status = source != NULL && source->result_class == BUSTER_TYPED_CLASS_MASK ? ir_simd_typed_mask_row(source, target_features, result) :
                                     masks                                                             ? ir_simd_typed_mask_test(source, target_features, result) :
                                     scalar                                                            ? ir_simd_typed_intrinsic(source, target_features, result) :
                                                                                                         ir_simd_typed_operation(source, target_features, result);
    // A row the IR's own operations don't take is its machine row (#141); one they take but the target refuses stays
    // refused.
    return status == IR_SIMD_TYPED_UNSUPPORTED ? ir_simd_typed_machine_row(source, target_features, result) : status;
}
