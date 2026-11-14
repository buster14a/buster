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
    // A comparison into a mask alone (#1623, ir_simd_typed_compare_mask): the mask of the lanes' vcmp ANDed with its
    // third operand, the writemask, which the lowering leaves out where the typed stream's writemask has every lane set.
    bool compare_mask;
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
    // A scalar's bit splat (#1450): the bytes repeated by the exact broadcast form, zero for every other row.
    // Integer rows may use a wider scalar register; lowering truncates it to these bytes before the splat.
    uint8_t splat_bytes;
    // A row the IR has no operation of its own for (#141, ir_simd_typed_machine_row): the row itself, which a row call
    // runs (IrFunction.rows).
    bool machine;
    // A row of no operand whose vector is zeros (#1840, ir_simd_typed_zero): an undefined vector's, which the IR's zero
    // constant is.
    bool zero;
    // A width change (#1840, ir_simd_typed_width): its IR intrinsic (BUSTER_IR_INTRINSIC_EXTRACT, _WIDEN or _INSERT) plus
    // one, 0 for any other row, and its part; and whether its result merges into its second operand under its third, the
    // writemask (a masked extract's), at `lane`'s width.
    uint8_t width_change;
    uint8_t part;
    bool merge_mask;
    // A permute (#1840, ir_simd_typed_permute): its IR intrinsic (BUSTER_IR_INTRINSIC_PERMUTE to _SHUFFLE_LANES) plus one, 0
    // for any other row, and its immediate, the IR's flags.
    uint8_t permute;
    uint8_t immediate;
    // A conversion (#1840, ir_simd_typed_convert): its IR intrinsic (BUSTER_IR_INTRINSIC_EXTEND or _TRUNCATE) plus one, 0
    // for any other row, its flags in `immediate`; a masked truncation's merge in `merge_mask` and `lane`.
    uint8_t convert;
    // A shift by a scalar count (#1840, ir_simd_typed_shift): whether the row is one, its flags in `immediate`.
    bool shift;
    // A mask to a vector or back (#1840, ir_simd_typed_mask_vector), at `lane`: a vselect of all ones where the mask's bit
    // is set and zeros elsewhere (vpmovm2b to vpmovm2q), or the mask of the lanes whose sign bit is set, a signed
    // comparison less than zero (vpmovb2m to vpmovq2m).
    bool mask_expand;
    bool sign_mask;
    // A lane operation (#1840, ir_simd_typed_lane_op): whether the row is one, its flags in `immediate`.
    bool lane_op;
    // A permute of two tables (#1840, ir_simd_typed_permute2): whether the row is one, its lanes' bytes' log2 in
    // `immediate`, and its indices' and its second table's operands (the first table is operand 0); a table narrower than
    // the result is widened.
    bool permute2;
    uint8_t permute2_indices;
    uint8_t permute2_table;
    // A bitwise function of three vectors (#1840, ir_simd_typed_ternlog): whether the row is one, its truth table in
    // `immediate`; under a writemask (its fourth operand) merged into its first operand at `lane` (`merge_mask`), or into
    // zeros (`ternlog_zero`: a zeroing writemask's).
    bool ternlog;
    bool ternlog_zero;
    // A compress or an expand of a vector's lanes (#1840, ir_simd_typed_compress): its IR intrinsic
    // (BUSTER_IR_INTRINSIC_COMPRESS or _EXPAND) plus one, 0 for any other row, its lanes' bytes' log2 in `immediate`.
    uint8_t compress;
    // An AES round (#1840, ir_simd_typed_aes): whether the row is one, its round (0, or 1 for the last) in `immediate`.
    bool aes;
    // A masked load or store (#1840, ir_simd_typed_masked_memory): 1 for a load, 2 for a store, 0 for any other row; its
    // lanes in `lane` and its alignment's log2 (an aligned form's, 0 for an unaligned one) in `immediate`. 3 for a
    // compressing store, its lanes in `lane`; 4 for a gather (ir_simd_typed_gather), its lanes in `lane` and its scale's
    // log2 in `immediate`; 5 for a truncating store, its truncation's flags in `immediate` and its stored lanes in `lane`.
    uint8_t masked_memory;
    // One lane of a vector or its lanes' sign bits (#1840, ir_simd_typed_lane): its IR intrinsic
    // (BUSTER_IR_INTRINSIC_LANE_EXTRACT, _LANE_INSERT or _MOVEMASK) plus one, 0 for any other row, its flags in `immediate`.
    uint8_t lane_move;
    // A reduction of a vector's lanes (#1840, ir_simd_typed_reduce): whether the row is one, its flags in `immediate`.
    bool reduce;
    // A unary operation of each lane (#1840, ir_simd_typed_lane_unary): whether the row is one, its flags in `immediate`;
    // and whether it is clang's leading zero count with a second operand, that operand's lane where the first's is zero.
    bool lane_unary;
    bool lane_unary_zero;
    // A floating operation at its own rounding (#1840, ir_simd_typed_fround): whether the row is one, its flags in
    // `immediate`; and a truncating conversion of lane 0 (ir_simd_typed_cvtt), the same.
    bool fround;
    bool cvtt;
    // A byte shuffle by an immediate (#1840, ir_simd_typed_shuffle_immediate): its kind (BUSTER_IR_SHUFFLE_IMMEDIATE_*)
    // plus one, 0 for any other row, its immediate in `immediate`: the IR's shuffle of the vector by that kind's indices.
    uint8_t shuffle_immediate;
    // A quadword operation (#1840, ir_simd_typed_quadword_op): whether the row is one, its flags in `immediate`.
    bool quadword_op;
    // A funnel shift (#1840, ir_simd_typed_funnel): whether the row is one, its flags in `immediate`; and whether it is a
    // rotate by an immediate (`rotate`, its count in `part`), the funnel of its vector twice by that constant count.
    bool funnel;
    bool rotate;
    // A bit shuffle into a mask (#1840, ir_simd_typed_bitshuffle): whether the row is one, its bytes' mask ANDed with its
    // third operand, the writemask, as a comparison's (`lane` the bytes'), left out where the typed stream's writemask
    // has every lane.
    bool bitshuffle;
};

// The conversions' and the scalar shifts' forms (#1840), shared with Tier 1's selection.
#include "buster/compiler/ir_simd_convert_forms.c"

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

// The widest vector Tier 1 moves whole at a target (#1624): 64 bytes with AVX512F, 32 with AVX, and else x86-64's 16. A
// wider vector the C driver's --tier1 lowers (ir_lower_typed_target_pieces) is split into this width's pieces after
// promotion (ir_vector_split.c).
static uint32_t ir_typed_vector_piece(uint64_t target_features)
{
    return (target_features & BUSTER_X86_FEATURE(AVX512F)) != 0 ? 64 : (target_features & BUSTER_X86_FEATURE(AVX)) != 0 ? 32 : 16;
}

// A comparison's opmask forms (#717's C comparisons, #1623's comparisons into a mask), by its lanes: signed and unsigned
// integers' vpcmp and vpcmpu of each width, floats' and doubles' vcmpps and vcmppd (unsigned_lanes 0).
static const uint16_t ir_simd_typed_comparisons[IR_SIMD_LANE_COUNT][2][3] = {
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

// A comparison's predicate from its immediate (ir_simd_typed_comparisons' forms): integers' as vpcmp's immediates (0 ==,
// 1 <, 2 <=, 4 !=, 5 >=, 6 >), unsigned lanes' as vpcmpu's, and floating lanes' as vcmpps' EQ_OQ, NEQ_UQ, LT_OS, LE_OS,
// GT_OS and GE_OS, C's ==, !=, <, <=, > and >=. False for any other immediate (vpcmp's false and true, vcmpps' other
// predicates), which no IR predicate is.
static bool ir_simd_typed_predicate(int64_t immediate, bool floating, bool unsigned_lanes, IrSimdPredicate* predicate)
{
    bool result = true;
    switch (floating ? immediate + 0x100 : immediate)
    {
        case 0: *predicate = IR_SIMD_EQ; break;
        case 4: *predicate = IR_SIMD_NE; break;
        case 1: *predicate = unsigned_lanes ? IR_SIMD_ULT : IR_SIMD_LT; break;
        case 2: *predicate = unsigned_lanes ? IR_SIMD_ULE : IR_SIMD_LE; break;
        case 6: *predicate = unsigned_lanes ? IR_SIMD_UGT : IR_SIMD_GT; break;
        case 5: *predicate = unsigned_lanes ? IR_SIMD_UGE : IR_SIMD_GE; break;
        case 0x100: *predicate = IR_SIMD_EQ; break;
        case 0x104: *predicate = IR_SIMD_NE; break;
        case 0x101: *predicate = IR_SIMD_LT; break;
        case 0x102: *predicate = IR_SIMD_LE; break;
        case 0x10e: *predicate = IR_SIMD_GT; break;
        case 0x10d: *predicate = IR_SIMD_GE; break;
        default: result = false; break;
    }
    return result;
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
                        expected = (X86Instruction){ .form = ir_simd_typed_comparisons[lane][unsigned_lanes][width], .reg = 1, .vvvv = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = record->immediate };
                        if (memcmp(record, &expected, sizeof(expected)) == 0)
                        {
                            const bool predicate = ir_simd_typed_predicate(record->immediate, floating, unsigned_lanes != 0, &candidate.predicate);
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

// A row of the bit builtins (#1374, check_lower_bits_row): one integer operand of 32 or 64 bits, read by a count of the
// form's width into the result's register (tzcnt, bsf, lzcnt, bsr or popcnt), an integer of 32 bits or of the operand's
// width; or moved there and its bytes swapped (bswap), of the operand's class. Each is its IR intrinsic of the operand's
// width (ir_typed_bits narrows a 64-bit count to the result's type), which needs the features its form does.
static IrSimdTypedStatus ir_simd_typed_bits(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const IrSimdTypedIntrinsic counts[] = {
        { { X86_FORM_TZCNT_R32_RM32, X86_FORM_TZCNT_R64_RM64 }, BUSTER_IR_INTRINSIC_TZCNT, false, BUSTER_X86_FEATURE(BMI) },
        { { X86_FORM_BSF_R32_RM32, X86_FORM_BSF_R64_RM64 }, BUSTER_IR_INTRINSIC_BSF, false, 0 },
        { { X86_FORM_LZCNT_R32_RM32, X86_FORM_LZCNT_R64_RM64 }, BUSTER_IR_INTRINSIC_LZCNT, false, BUSTER_X86_FEATURE(LZCNT) },
        { { X86_FORM_BSR_R32_RM32, X86_FORM_BSR_R64_RM64 }, BUSTER_IR_INTRINSIC_BSR, false, 0 },
        { { X86_FORM_POPCNT_R32_RM32, X86_FORM_POPCNT_R64_RM64 }, BUSTER_IR_INTRINSIC_POPCNT, false, BUSTER_X86_FEATURE(POPCNT) },
    };
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_UNSUPPORTED : IR_SIMD_TYPED_INVALID;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    const uint8_t operand = source != NULL ? source->operand_classes[0] : 0;
    const uint8_t class = source != NULL ? source->result_class : 0;
    const uint32_t bytes = tier0_class_bytes(operand);
    if (status != IR_SIMD_TYPED_INVALID && source->operand_count == 1 && tier0_class_integer(operand) && (bytes == 4 || bytes == 8) &&
        source->operand_bytes[0] == 0 && tier0_class_integer(class) && source->result_bytes == 0)
    {
        const bool wide = bytes == 8;
        const uint8_t r = BUSTER_TIER0_ROLE_INTEGER_RESULT;
        const uint8_t i0 = BUSTER_TIER0_ROLE_INTEGER(0);
        if (source->record_count == 1 && (tier0_class_bytes(class) == 4 || tier0_class_bytes(class) == bytes))
        {
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(counts); k += 1)
            {
                const X86Instruction expected = { .form = counts[k].forms[wide], .reg = r, .rm = i0, .mode = BUSTER_X86_REGISTER_DIRECT };
                if (memcmp(&source->records[0], &expected, sizeof(expected)) == 0)
                {
                    candidate.intrinsic = (uint8_t)(counts[k].intrinsic + 1);
                    candidate.required_features = counts[k].feature;
                }
            }
        }
        else if (source->record_count == 2 && class == operand)
        {
            const X86Instruction move = { .form = wide ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32, .reg = r, .rm = i0, .mode = BUSTER_X86_REGISTER_DIRECT };
            const X86Instruction swap = { .form = wide ? X86_FORM_BSWAP_R64 : X86_FORM_BSWAP_R32, .rm = r, .mode = BUSTER_X86_REGISTER_DIRECT };
            if (memcmp(&source->records[0], &move, sizeof(move)) == 0 && memcmp(&source->records[1], &swap, sizeof(swap)) == 0)
            {
                candidate.intrinsic = (uint8_t)(BUSTER_IR_INTRINSIC_BSWAP + 1);
            }
        }
        if (candidate.intrinsic != 0)
        {
            status = (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_TARGET;
        }
    }
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.operand_count = 1;
        candidate.operand_classes[0] = operand;
        candidate.intrinsic_operands[0] = 0;
        candidate.intrinsic_operands[1] = 0;
        *result = candidate;
    }
    return status;
}

// A row of a signed overflow builtin's flag (#1385, check_lower_overflow_row): two integer operands of one class, an int's
// or a long's, and an int result, the operation's overflow flag, setcc'd into the first scratch register and zero-extended
// into the result's: the second operand added to the first's register (add) or multiplied into it (imul), or the two
// compared (cmp, a difference). Each is its IR intrinsic of the operands' type (saddo, smulo, ssubo), narrowed to the
// result's; baseline x86-64's forms, which every target has.
static IrSimdTypedStatus ir_simd_typed_overflow(const Tier0Operation* source, IrSimdTypedOperation* result)
{
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_UNSUPPORTED : IR_SIMD_TYPED_INVALID;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    const uint8_t operand = source != NULL ? source->operand_classes[0] : 0;
    const bool wide = operand == BUSTER_TYPED_CLASS_I64;
    if (status != IR_SIMD_TYPED_INVALID && (operand == BUSTER_TYPED_CLASS_I32 || wide) && source->operand_count == 2 && source->operand_classes[1] == operand &&
        source->operand_bytes[0] == 0 && source->operand_bytes[1] == 0 && source->result_class == BUSTER_TYPED_CLASS_I32 && source->result_bytes == 0)
    {
        const uint8_t r = BUSTER_TIER0_ROLE_INTEGER_RESULT;
        const uint8_t i0 = BUSTER_TIER0_ROLE_INTEGER(0);
        const uint8_t i1 = BUSTER_TIER0_ROLE_INTEGER(1);
        const uint8_t s0 = BUSTER_TIER0_ROLE_SCRATCH(0);
        const uint8_t direct = BUSTER_X86_REGISTER_DIRECT;
        const X86Instruction set = { .form = X86_FORM_SETO_RM8, .rm = s0, .mode = direct };
        const X86Instruction extend = { .form = X86_FORM_MOVZX_R32_RM8, .reg = r, .rm = s0, .mode = direct };
        const X86Instruction add = { .form = wide ? X86_FORM_ADD_R64_RM64 : X86_FORM_ADD_R32_RM32, .reg = i0, .rm = i1, .mode = direct };
        const X86Instruction multiply = { .form = wide ? X86_FORM_IMUL_R64_RM64 : X86_FORM_IMUL_R32_RM32, .reg = i0, .rm = i1, .mode = direct };
        const X86Instruction compare = { .form = wide ? X86_FORM_CMP_R64_RM64 : X86_FORM_CMP_R32_RM32, .reg = i0, .rm = i1, .mode = direct };
        const X86Instruction* records = source->records;
        if (source->record_count == 3 && memcmp(&records[1], &set, sizeof(set)) == 0 && memcmp(&records[2], &extend, sizeof(extend)) == 0)
        {
            candidate.intrinsic = memcmp(&records[0], &add, sizeof(add)) == 0             ? (uint8_t)(BUSTER_IR_INTRINSIC_SADDO + 1)
                                  : memcmp(&records[0], &compare, sizeof(compare)) == 0   ? (uint8_t)(BUSTER_IR_INTRINSIC_SSUBO + 1)
                                  : memcmp(&records[0], &multiply, sizeof(multiply)) == 0 ? (uint8_t)(BUSTER_IR_INTRINSIC_SMULO + 1)
                                                                                          : 0;
        }
        status = candidate.intrinsic != 0 ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    }
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.operand_count = 2;
        candidate.operand_classes[0] = operand;
        candidate.operand_classes[1] = operand;
        candidate.intrinsic_operands[0] = 0;
        candidate.intrinsic_operands[1] = 1;
        *result = candidate;
    }
    return status;
}

// The producer's single-record EVEX broadcasts (#1450). No decorated, memory or fixed-register row is inferred as a
// splat. Floating forms copy bits without evaluating them; byte/word scalars are extended to a 32-bit register by the
// producer, and a half's bits use the integer word form and its U32 source. Lowering truncates those carriers explicitly.
static IrSimdTypedStatus ir_simd_typed_splat(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint16_t forms[6][3] = {
        { X86_FORM_VPBROADCASTB_X_R32, X86_FORM_VPBROADCASTB_Y_R32, X86_FORM_VPBROADCASTB_Z_R32 },
        { X86_FORM_VPBROADCASTW_X_R32, X86_FORM_VPBROADCASTW_Y_R32, X86_FORM_VPBROADCASTW_Z_R32 },
        { X86_FORM_VPBROADCASTD_X_R32, X86_FORM_VPBROADCASTD_Y_R32, X86_FORM_VPBROADCASTD_Z_R32 },
        { X86_FORM_VPBROADCASTQ_X_R64, X86_FORM_VPBROADCASTQ_Y_R64, X86_FORM_VPBROADCASTQ_Z_R64 },
        { X86_FORM_VBROADCASTSS_X_XM32_EVEX, X86_FORM_VBROADCASTSS_Y_XM32_EVEX, X86_FORM_VBROADCASTSS_Z_XM32 },
        { X86_FORM_VPBROADCASTQ_X_XM64_EVEX, X86_FORM_VBROADCASTSD_Y_XM64_EVEX, X86_FORM_VBROADCASTSD_Z_XM64 },
    };
    static const uint8_t bytes[6] = { 1, 2, 4, 8, 4, 8 };
    IrSimdTypedStatus status = source != NULL && result != NULL ? IR_SIMD_TYPED_UNSUPPORTED : IR_SIMD_TYPED_INVALID;
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR &&
        (source->result_bytes == 16 || source->result_bytes == 32 || source->result_bytes == 64) &&
        source->operand_count == 1 && source->record_count == 1 && source->operand_bytes[0] == 0)
    {
        const uint32_t width = source->result_bytes == 16 ? 0 : source->result_bytes == 32 ? 1 : 2;
        const uint8_t class = source->operand_classes[0];
        for (uint32_t form = 0; form < BUSTER_ARRAY_LENGTH(forms); form += 1)
        {
            const bool floating = form >= 4;
            const bool scalar = floating ? class == (form == 4 ? BUSTER_TYPED_CLASS_F32 : BUSTER_TYPED_CLASS_F64) :
                                          tier0_class_integer(class) && tier0_class_bytes(class) >= (bytes[form] == 8 ? 8u : 4u) && tier0_class_bytes(class) <= 8;
            const X86Instruction expected = {
                .form = forms[form][width],
                .reg = BUSTER_TIER0_ROLE_VECTOR(0),
                .rm = floating ? BUSTER_TIER0_ROLE_FLOATING(0) : BUSTER_TIER0_ROLE_INTEGER(0),
                .mode = BUSTER_X86_REGISTER_DIRECT,
            };
            if (scalar && memcmp(source->records, &expected, sizeof(expected)) == 0)
            {
                const uint64_t required = ir_typed_vector_moves(source->result_bytes, target_features) | (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN);
                status = (target_features & required) == required ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_TARGET;
                if (status == IR_SIMD_TYPED_OK)
                {
                    IrSimdTypedOperation candidate = {
                        .operation = IR_SIMD_OPERATION_COUNT,
                        .shape = (IrSimdShape)(IR_SIMD_V4I32 + width),
                        .required_features = required,
                        .operand_count = 1,
                        .splat_bytes = bytes[form],
                    };
                    candidate.operand_classes[0] = class;
                    *result = candidate;
                }
            }
        }
    }
    return status;
}

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

// A comparison into a mask alone (#1623): a comparison intrinsic's row as the opmask conversion leaves it
// (c_check_program_x86.c's COMPARE), two vectors of one width (16, 32 or 64 bytes) and a writemask of the result's width,
// one record of ir_simd_typed_comparisons' form of their lanes into MASK_RESULT, register-direct, MASK(2) its only
// decoration and an immediate an IR predicate takes (ir_simd_typed_predicate). Its operands may be in either order: the
// front end swaps a float compare's for clang's > and >= (check_x86_lower_float_predicate: GT_OS as LT_OS of the swapped
// pair), and the other order is the reversed predicate of the operands in theirs (a < b as b > a), exact for every lane
// and NaN, and the same instruction's exceptions. Its mask is the lanes' vcmp ANDed with the writemask (compare_mask),
// and the result's bytes must be the lanes' mask's (ir_lane_mask's: a byte for 8 lanes or fewer). It is taken only
// where the target has every form Tier 1 selects for it: the comparison's own, AVX512F and VL below 64 bytes, and its
// mask width's moves and kand (DQ for a byte's, BW for 32 and 64 lanes'); otherwise, and for an immediate no IR
// predicate is, a memory operand, a broadcast, rounding or any other decoration, it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_compare_mask(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint8_t reversed[IR_SIMD_PREDICATE_COUNT] = {
        [IR_SIMD_EQ] = IR_SIMD_EQ,
        [IR_SIMD_NE] = IR_SIMD_NE,
        [IR_SIMD_LT] = IR_SIMD_GT,
        [IR_SIMD_LE] = IR_SIMD_GE,
        [IR_SIMD_GT] = IR_SIMD_LT,
        [IR_SIMD_GE] = IR_SIMD_LE,
        [IR_SIMD_ULT] = IR_SIMD_UGT,
        [IR_SIMD_ULE] = IR_SIMD_UGE,
        [IR_SIMD_UGT] = IR_SIMD_ULT,
        [IR_SIMD_UGE] = IR_SIMD_ULE,
    };
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint32_t bytes = source->operand_bytes[0];
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_MASK && source->record_count == 1 && source->operand_count == 3 &&
                          source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR &&
                          source->operand_bytes[1] == bytes && (bytes == 16 || bytes == 32 || bytes == 64) &&
                          source->operand_classes[2] == BUSTER_TYPED_CLASS_MASK && source->operand_bytes[2] == source->result_bytes;
    const X86Instruction* record = &source->records[0];
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t lane = 0; admitted && lane < IR_SIMD_LANE_COUNT; lane += 1)
    {
        const bool floating = ir_simd_lane_floating((IrSimdLane)lane);
        const uint32_t lanes = 8u * bytes / ir_simd_lane_bits((IrSimdLane)lane);
        const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
        for (uint32_t form = 0; form < (floating ? 2u : 4u); form += 1)
        {
            const uint32_t unsigned_lanes = form >> 1;
            const bool swapped = (form & 1) != 0;
            const X86Instruction expected = {
                .form = ir_simd_typed_comparisons[lane][unsigned_lanes][width],
                .reg = BUSTER_TIER0_ROLE_MASK_RESULT,
                .vvvv = swapped ? v1 : v0,
                .rm = swapped ? v0 : v1,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .decorations = BUSTER_TIER0_DECORATION_MASK(2),
                .immediate = record->immediate,
            };
            IrSimdPredicate predicate = IR_SIMD_EQ;
            if (memcmp(record, &expected, sizeof(expected)) == 0 && mask_bytes == source->result_bytes &&
                ir_simd_typed_predicate(record->immediate, floating, unsigned_lanes != 0, &predicate))
            {
                candidate.operation = IR_SIMD_COMPARE;
                candidate.lane = (IrSimdLane)lane;
                candidate.predicate = swapped ? (IrSimdPredicate)reversed[predicate] : predicate;
                candidate.compare_mask = true;
            }
        }
    }
    const uint32_t mask = source->result_bytes;
    candidate.required_features = (x86_target_requirement(record->form) & ~BUSTER_X86_TARGET_KNOWN) | BUSTER_X86_FEATURE(AVX512F) |
                                  (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) | (mask == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) |
                                  (mask >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
    const IrSimdTypedStatus status = candidate.operation != IR_SIMD_OPERATION_COUNT && (target_features & candidate.required_features) == candidate.required_features ?
                                         IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The width changes (#1840): a row of no record of one vector operand (or two of one width, the second kept for its
// effects alone), whose result is the operand's register (c_check_program_vector_builtins.c's shuffles that move no
// element, the intrinsics' casts), of 16, 32 or 64 bytes but the operand's: the operand's low bytes where the result is
// narrower (extract, part 0), and the operand and zeros past it where it is wider (widen, Tier 0's and Tier 1's
// placements zero-extend an operand into its role's register); an insertion's row (BINARY_IMMEDIATE's vinsert forms),
// one record of its form, VECTOR(0) its destination and first source and VECTOR(1), of the form's narrower width, its
// second, its immediate the part (insert); and a lane's masked extraction (EXTRACT_LANE's vextract forms), its first
// record of its form from VECTOR(0) into VECTOR(1), of the form's narrower width, under MASK(2), its immediate the part,
// and its second VECTOR(1) moved whole into VECTOR(0) (extract, merged under the mask into the second operand at the
// form's elements' lanes, which Tier 1 selects as a vselect, or not where the typed stream's writemask has every lane).
// Each register-direct with no other field. Its features are the forms Tier 1 selects for it: AVX512F's and VL's
// vextracti32x4, vextracti64x4, vinserti32x4, vinserti64x4 and moves of each width, the row's own forms' and, merged, the
// mask's moves' (DQ for a byte's); otherwise, and for any other form, it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_width(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    // The inserts' and the extracts' forms, by their elements: 4-byte ones', then 8-byte ones'.
    static const uint16_t inserts[] = {
        X86_FORM_VINSERTI32X4_Y_Y_XM128_IMM8, X86_FORM_VINSERTI32X4_Z_Z_XM128_IMM8, X86_FORM_VINSERTF32X4_Y_Y_XM128_IMM8, X86_FORM_VINSERTF32X4_Z_Z_XM128_IMM8,
        X86_FORM_VINSERTI32X8_Z_Z_YM256_IMM8, X86_FORM_VINSERTF32X8_Z_Z_YM256_IMM8, X86_FORM_VINSERTI64X2_Y_Y_XM128_IMM8, X86_FORM_VINSERTI64X2_Z_Z_XM128_IMM8,
        X86_FORM_VINSERTF64X2_Y_Y_XM128_IMM8, X86_FORM_VINSERTF64X2_Z_Z_XM128_IMM8, X86_FORM_VINSERTI64X4_Z_Z_YM256_IMM8, X86_FORM_VINSERTF64X4_Z_Z_YM256_IMM8,
    };
    static const uint16_t extracts[2][6] = {
        { X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8, X86_FORM_VEXTRACTI32X4_XM128_Z_IMM8, X86_FORM_VEXTRACTF32X4_XM128_Y_IMM8, X86_FORM_VEXTRACTF32X4_XM128_Z_IMM8,
          X86_FORM_VEXTRACTI32X8_YM256_Z_IMM8, X86_FORM_VEXTRACTF32X8_YM256_Z_IMM8 },
        { X86_FORM_VEXTRACTI64X2_XM128_Y_IMM8, X86_FORM_VEXTRACTI64X2_XM128_Z_IMM8, X86_FORM_VEXTRACTF64X2_XM128_Y_IMM8, X86_FORM_VEXTRACTF64X2_XM128_Z_IMM8,
          X86_FORM_VEXTRACTI64X4_YM256_Z_IMM8, X86_FORM_VEXTRACTF64X4_YM256_Z_IMM8 },
    };
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint32_t bytes = source->result_bytes;
    const uint32_t first = source->operand_bytes[0];
    const bool vector = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->operand_count != 0 &&
                        source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && (first == 16 || first == 32 || first == 64);
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    uint64_t forms = 0;
    if (vector && source->record_count == 0 && first != bytes &&
        (source->operand_count == 1 || (source->operand_count == 2 && source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[1] == first)))
    {
        candidate.width_change = (uint8_t)(bytes < first ? BUSTER_IR_INTRINSIC_EXTRACT + 1 : BUSTER_IR_INTRINSIC_WIDEN + 1);
    }
    const X86Instruction* record = &source->records[0];
    for (uint32_t form = 0; vector && source->record_count == 1 && source->operand_count == 2 && form < BUSTER_ARRAY_LENGTH(inserts); form += 1)
    {
        const uint32_t narrow = source->operand_bytes[1];
        const X86Instruction expected = { .form = inserts[form], .reg = v0, .vvvv = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = record->immediate };
        const X86FormDescription* description = x86_form_description(inserts[form]);
        const uint32_t wide = 16u << description->vector_length;
        if (memcmp(record, &expected, sizeof(expected)) == 0 && first == bytes && wide == bytes && source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR &&
            (narrow == 16 || narrow == 32) && narrow < bytes && record->immediate >= 0 && (uint32_t)record->immediate < bytes / narrow &&
            (narrow == 32) == (form == 4 || form == 5 || form == 10 || form == 11))
        {
            candidate.width_change = BUSTER_IR_INTRINSIC_INSERT + 1;
            candidate.part = (uint8_t)record->immediate;
            forms = x86_target_requirement(record->form);
        }
    }
    for (uint32_t size = 0; vector && source->record_count == 2 && source->operand_count == 3 && size < 2; size += 1)
    {
        for (uint32_t form = 0; form < BUSTER_ARRAY_LENGTH(extracts[size]); form += 1)
        {
            const uint32_t narrow = form >= 4 ? 32 : 16;
            const uint32_t wide = form >= 4 || (form & 1) != 0 ? 64 : 32;
            const uint32_t lanes = narrow / (size == 0 ? 4 : 8);
            const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
            const X86Instruction expected = {
                .form = extracts[size][form],
                .reg = v0,
                .rm = v1,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .decorations = BUSTER_TIER0_DECORATION_MASK(2),
                .immediate = record->immediate,
            };
            const X86Instruction moved = {
                .form = narrow == 16 ? X86_FORM_VMOVDQU64_X_XM128 : X86_FORM_VMOVDQU64_Y_YM256,
                .reg = v0,
                .rm = v1,
                .mode = BUSTER_X86_REGISTER_DIRECT,
            };
            if (memcmp(record, &expected, sizeof(expected)) == 0 && memcmp(&source->records[1], &moved, sizeof(moved)) == 0 && first == wide && bytes == narrow &&
                source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[1] == narrow && source->operand_classes[2] == BUSTER_TYPED_CLASS_MASK &&
                source->operand_bytes[2] == mask_bytes && record->immediate >= 0 && (uint32_t)record->immediate < wide / narrow)
            {
                candidate.width_change = BUSTER_IR_INTRINSIC_EXTRACT + 1;
                candidate.part = (uint8_t)record->immediate;
                candidate.merge_mask = true;
                candidate.lane = size == 0 ? IR_SIMD_LANE_I32 : IR_SIMD_LANE_I64;
                forms = x86_target_requirement(record->form) | x86_target_requirement(moved.form) | (mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0);
            }
        }
    }
    candidate.required_features = ir_typed_vector_moves(bytes, target_features) | ir_typed_vector_moves(first, target_features) | BUSTER_X86_FEATURE(AVX512F) |
                                  BUSTER_X86_FEATURE(AVX512VL) | (forms & ~BUSTER_X86_TARGET_KNOWN);
    const IrSimdTypedStatus status = candidate.width_change != 0 && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK :
                                                                                                                                                    IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + (bytes == 16 ? 0 : bytes == 32 ? 1 : 2));
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The permutes (#1840): a row of two vectors of the result's width (16, 32 or 64 bytes) and one record of a permute's form
// at that width, register-direct with no decoration: vpermb, vpermw, vpermd and vpermq of VECTOR(1)'s indices (vvvv) into
// VECTOR(0)'s lanes (rm), the variable permutes' builtins as the front end swaps them (permute of VECTOR(0) by VECTOR(1),
// its flags its lanes' bytes' log2); vpshufb of VECTOR(0) (vvvv) by VECTOR(1) (rm) (shuffle); and valignd, valignq,
// vshufi32x4 and vshufi64x2 of VECTOR(0) (vvvv) and VECTOR(1) (rm) by the record's immediate (align, of its lanes' size
// and the immediate's count, its bits below the lanes, which the instruction reads; shuffle_lanes of the immediate, its
// low 2 bits at 32 bytes, whose 32x4 and 64x2 forms move the same 16-byte lanes). The destination is VECTOR(0)'s register (reg). Its features are the form's and its vectors' moves
// (AVX512F, VL below 64 bytes): Tier 1 selects the form at the same width, vshufi64x2 for both shuffles of lanes;
// otherwise, and for any other form or field, it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_permute(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    typedef struct IrSimdTypedPermuteForm IrSimdTypedPermuteForm;
    struct IrSimdTypedPermuteForm
    {
        uint16_t forms[3];
        uint8_t kind;
        uint8_t flags;
        bool indices_first;
    };
    static const IrSimdTypedPermuteForm forms[] = {
        { { X86_FORM_VPERMB_X_X_XM128, X86_FORM_VPERMB_Y_Y_YM256, X86_FORM_VPERMB_Z_Z_ZM512 }, BUSTER_IR_INTRINSIC_PERMUTE, 0, true },
        { { X86_FORM_VPERMW_X_X_XM128, X86_FORM_VPERMW_Y_Y_YM256, X86_FORM_VPERMW_Z_Z_ZM512 }, BUSTER_IR_INTRINSIC_PERMUTE, 1, true },
        { { X86_FORM_NONE, X86_FORM_VPERMD_Y_Y_YM256_EVEX, X86_FORM_VPERMD_Z_Z_ZM512 }, BUSTER_IR_INTRINSIC_PERMUTE, 2, true },
        { { X86_FORM_NONE, X86_FORM_NONE, X86_FORM_VPERMQ_Z_Z_ZM512 }, BUSTER_IR_INTRINSIC_PERMUTE, 3, true },
        { { X86_FORM_VPSHUFB_X_X_XM128_EVEX, X86_FORM_VPSHUFB_Y_Y_YM256_EVEX, X86_FORM_VPSHUFB_Z_Z_ZM512 }, BUSTER_IR_INTRINSIC_SHUFFLE, 0, false },
        { { X86_FORM_VALIGND_X_X_XM128_IMM8, X86_FORM_VALIGND_Y_Y_YM256_IMM8, X86_FORM_VALIGND_Z_Z_ZM512_IMM8 }, BUSTER_IR_INTRINSIC_ALIGN, 0x20, false },
        { { X86_FORM_VALIGNQ_X_X_XM128_IMM8, X86_FORM_VALIGNQ_Y_Y_YM256_IMM8, X86_FORM_VALIGNQ_Z_Z_ZM512_IMM8 }, BUSTER_IR_INTRINSIC_ALIGN, 0x30, false },
        { { X86_FORM_NONE, X86_FORM_VSHUFI32X4_Y_Y_YM256_IMM8, X86_FORM_VSHUFI32X4_Z_Z_ZM512_IMM8 }, BUSTER_IR_INTRINSIC_SHUFFLE_LANES, 0, false },
        { { X86_FORM_NONE, X86_FORM_VSHUFI64X2_Y_Y_YM256_IMM8, X86_FORM_VSHUFI64X2_Z_Z_ZM512_IMM8 }, BUSTER_IR_INTRINSIC_SHUFFLE_LANES, 0, false },
    };
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 1 &&
                          source->operand_count == 2 && source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[0] == bytes &&
                          source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[1] == bytes;
    const X86Instruction* record = &source->records[0];
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t form = 0; admitted && form < BUSTER_ARRAY_LENGTH(forms); form += 1)
    {
        const IrSimdTypedPermuteForm* entry = &forms[form];
        const bool immediate = entry->kind == BUSTER_IR_INTRINSIC_ALIGN || entry->kind == BUSTER_IR_INTRINSIC_SHUFFLE_LANES;
        const X86Instruction expected = {
            .form = entry->forms[width],
            .reg = v0,
            .vvvv = entry->indices_first ? v1 : v0,
            .rm = entry->indices_first ? v0 : v1,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .immediate = immediate ? record->immediate : 0,
        };
        // The bits of the immediate the instruction reads: an align's count below its lanes, a 32-byte shuffle of lanes' low 2.
        const uint32_t lanes = bytes >> (entry->flags >> 4);
        const uint32_t flags = entry->kind == BUSTER_IR_INTRINSIC_ALIGN                 ? entry->flags | ((uint32_t)record->immediate & (lanes - 1)) :
                               entry->kind == BUSTER_IR_INTRINSIC_SHUFFLE_LANES && width == 1 ? (uint32_t)record->immediate & 3 :
                               immediate                                                ? (uint32_t)record->immediate :
                                                                                          entry->flags;
        const bool fits = record->immediate >= 0 && record->immediate <= 255 && ir_permute_flags_valid(entry->kind, (uint32_t)(IR_V4I32 + width), flags);
        if (expected.form != X86_FORM_NONE && fits && memcmp(record, &expected, sizeof(expected)) == 0)
        {
            candidate.permute = (uint8_t)(entry->kind + 1);
            candidate.immediate = (uint8_t)flags;
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                          (x86_target_requirement(record->form) & ~BUSTER_X86_TARGET_KNOWN);
        }
    }
    const IrSimdTypedStatus status = candidate.permute != 0 && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK :
                                                                                                                                              IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 2;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The byte shuffles by an immediate (#1840): a row of one vector of 16, 32 or 64 bytes into a vector of its width, one
// record of an ir_simd_shuffle_immediate_forms form at that width (or the front end's 32-byte vpshufd, AVX2's VEX form),
// register-direct and undecorated, of VECTOR(0) (rm) into VECTOR(0)'s register (reg, or vvvv for vpslldq and vpsrldq):
// the builtins' __builtin_ia32_pshufd, pshufd256 and pshufd512, pshuflw and pshufhw, and pslldqi and psrldqi's byte
// shifts. An imm8 is its low 8 bits. It is the IR's shuffle of the vector by the kind's indices at that immediate in each
// 16-byte lane, a constant; Tier 1 matches it back to the EVEX form (tier1_select_shuffle_immediate). Its features are
// that form's (BW for the words' shuffles and the byte shifts), AVX512F's, VL's below 64 bytes and the vectors' moves;
// otherwise, and for any other form or field, it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_shuffle_immediate(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const X86Instruction* record = &source->records[0];
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 1 &&
                          source->operand_count == 1 && source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[0] == bytes &&
                          record->immediate >= -128 && record->immediate <= 255;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t kind = 0; admitted && kind < BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS; kind += 1)
    {
        const uint16_t form = ir_simd_shuffle_immediate_forms[kind][width];
        const bool shift = kind == BUSTER_IR_SHUFFLE_IMMEDIATE_LEFT_BYTES || kind == BUSTER_IR_SHUFFLE_IMMEDIATE_RIGHT_BYTES;
        const X86Instruction expected = {
            .form = form,
            .reg = shift ? 0 : v0,
            .vvvv = shift ? v0 : 0,
            .rm = v0,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .immediate = record->immediate,
        };
        X86Instruction vex = expected;
        vex.form = X86_FORM_VPSHUFD_Y_YM256_IMM8;
        const bool matched = memcmp(record, &expected, sizeof(expected)) == 0 ||
                             (kind == BUSTER_IR_SHUFFLE_IMMEDIATE_DWORDS && width == 1 && memcmp(record, &vex, sizeof(vex)) == 0);
        if (matched)
        {
            candidate.shuffle_immediate = (uint8_t)(kind + 1);
            candidate.immediate = (uint8_t)((uint32_t)record->immediate & 0xff);
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                          (x86_target_requirement(form) & ~BUSTER_X86_TARGET_KNOWN);
        }
    }
    const IrSimdTypedStatus status = candidate.shuffle_immediate != 0 && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK :
                                                                                                                                                         IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 1;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The permutes of two tables (#1840): a row of one record of a vpermt2b to vpermt2q form at the result's width (16, 32 or
// 64 bytes, ir_simd_permute2_forms), register-direct and undecorated, whose destination is VECTOR(0)'s register, the
// first table (reg), its indices a vector of the result's width (vvvv) and its second table another vector (rm): the
// permutex2var builtins' rows (the indices VECTOR(1), the table VECTOR(2)) and __builtin_shufflevector's of two vectors
// (the indices VECTOR(2), the table VECTOR(1)), or of one (the indices VECTOR(1), the table VECTOR(0) again). A table may
// be narrower than the result (16 or 32 bytes: a shuffle of narrower vectors into a wider one), its register's upper
// bytes then unspecified, which the indices never name where the result is defined: the IR's widen of it, zeros past it.
// The IR's permute2 of the first table, the indices and the second table, its flags its lanes' bytes' log2. Its features
// are its form's (VBMI for bytes, BW for words, VL below 64 bytes) and its vectors' moves; otherwise it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_permute2(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    // The operands' places: the indices', the second table's and the operand count.
    static const uint8_t shapes[3][3] = {
        { 1, 2, 3 },
        { 2, 1, 3 },
        { 1, 0, 2 },
    };
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 1;
    const X86Instruction* record = &source->records[0];
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t shape = 0; admitted && shape < 3; shape += 1)
    {
        const uint32_t indices = shapes[shape][0];
        const uint32_t table = shapes[shape][1];
        bool fits = source->operand_count == shapes[shape][2];
        uint64_t moves = 0;
        for (uint32_t operand = 0; fits && operand < source->operand_count; operand += 1)
        {
            const uint32_t operand_bytes = source->operand_bytes[operand];
            fits = source->operand_classes[operand] == BUSTER_TYPED_CLASS_VECTOR &&
                   (operand == indices ? operand_bytes == bytes : (operand_bytes == 16 || operand_bytes == 32 || operand_bytes == 64) && operand_bytes <= bytes);
            moves |= fits ? ir_typed_vector_moves(operand_bytes, target_features) : 0;
        }
        for (uint32_t lanes = 0; fits && lanes < 4; lanes += 1)
        {
            const X86Instruction expected = {
                .form = ir_simd_permute2_forms[lanes][width],
                .reg = BUSTER_TIER0_ROLE_VECTOR(0),
                .vvvv = (uint8_t)BUSTER_TIER0_ROLE_VECTOR(indices),
                .rm = (uint8_t)BUSTER_TIER0_ROLE_VECTOR(table),
                .mode = BUSTER_X86_REGISTER_DIRECT,
            };
            if (memcmp(record, &expected, sizeof(expected)) == 0)
            {
                candidate.permute2 = true;
                candidate.permute2_indices = (uint8_t)indices;
                candidate.permute2_table = (uint8_t)table;
                candidate.immediate = (uint8_t)lanes;
                candidate.required_features = ir_typed_vector_moves(bytes, target_features) | moves | (x86_target_requirement(record->form) & ~BUSTER_X86_TARGET_KNOWN);
            }
        }
    }
    const IrSimdTypedStatus status = candidate.permute2 && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The bitwise functions of three vectors (#1840): a row of one record of vpternlogd or vpternlogq (ir_simd_ternlog_forms)
// at the result's width (16, 32 or 64 bytes), register-direct, VECTOR(0) the destination and first operand (reg),
// VECTOR(1) the second (vvvv) and VECTOR(2) the third (rm), of three vectors of that width, its immediate the truth
// table, and under MASK(3)'s writemask with a bit per lane (the ternarylogic builtins', merged into the first operand,
// or zeroing, the maskz builtins') or without one: the IR's ternlog of the three, its flags the table, merged into the
// first operand, or into zeros, under the mask at the form's lanes where the typed stream's writemask hasn't every lane. Its features are its form's and its vectors' moves
// (AVX512F, VL below 64 bytes), and its mask's (DQ for a byte's, BW for 4 bytes'); otherwise it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_ternlog(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool masked = source->operand_count == 4;
    bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 1 && (source->operand_count == 3 || masked);
    for (uint32_t operand = 0; admitted && operand < 3; operand += 1)
    {
        admitted = source->operand_classes[operand] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[operand] == bytes;
    }
    const X86Instruction* record = &source->records[0];
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t form = 0; admitted && form < 4; form += 1)
    {
        const uint32_t lanes = form & 1;
        // Zeroing is a writemask's alone.
        const bool zeroing = form >= 2 && masked;
        const uint32_t count = bytes / (4u << lanes);
        const uint32_t mask_bytes = count <= 8 ? 1 : count / 8;
        const X86Instruction expected = {
            .form = ir_simd_ternlog_forms[lanes][width],
            .reg = BUSTER_TIER0_ROLE_VECTOR(0),
            .vvvv = BUSTER_TIER0_ROLE_VECTOR(1),
            .rm = BUSTER_TIER0_ROLE_VECTOR(2),
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .decorations = (uint8_t)(masked ? BUSTER_TIER0_DECORATION_MASK(3) | (zeroing ? BUSTER_X86_DECORATION_ZEROING : 0) : 0),
            .immediate = record->immediate,
        };
        const bool mask_fits = !masked || (source->operand_classes[3] == BUSTER_TYPED_CLASS_MASK && source->operand_bytes[3] == mask_bytes);
        if (record->immediate >= 0 && record->immediate <= 255 && mask_fits && memcmp(record, &expected, sizeof(expected)) == 0)
        {
            candidate.ternlog = true;
            candidate.ternlog_zero = zeroing;
            candidate.immediate = (uint8_t)record->immediate;
            candidate.merge_mask = masked;
            candidate.lane = lanes == 0 ? IR_SIMD_LANE_I32 : IR_SIMD_LANE_I64;
            const uint64_t mask = masked ? (mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (mask_bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0) : 0;
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | (x86_target_requirement(record->form) & ~BUSTER_X86_TARGET_KNOWN) | mask;
        }
    }
    const IrSimdTypedStatus status = candidate.ternlog && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The AES rounds (#1840): a row of one record of vaesenc or vaesenclast, EVEX at any width or VEX at 16 and 32 bytes
// (ir_simd_aes_forms, ir_simd_aes_vex_forms), register-direct and undecorated, VECTOR(0) the state (reg and vvvv) and
// VECTOR(1) the round key (rm), both of the result's width (the aesenc builtins' rows): the IR's aes of the two, its
// flags the round. Its features are its form's (VAES or AES) and its vectors' moves; otherwise it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_aes(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 1 && source->operand_count == 2 &&
                          source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[0] == bytes && source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR &&
                          source->operand_bytes[1] == bytes;
    const X86Instruction* record = &source->records[0];
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t form = 0; admitted && form < 4; form += 1)
    {
        const uint32_t last = form & 1;
        const X86Instruction expected = {
            .form = form < 2 ? ir_simd_aes_forms[last][width] : ir_simd_aes_vex_forms[last][width],
            .reg = BUSTER_TIER0_ROLE_VECTOR(0),
            .vvvv = BUSTER_TIER0_ROLE_VECTOR(0),
            .rm = BUSTER_TIER0_ROLE_VECTOR(1),
            .mode = BUSTER_X86_REGISTER_DIRECT,
        };
        if (expected.form != X86_FORM_NONE && memcmp(record, &expected, sizeof(expected)) == 0)
        {
            candidate.aes = true;
            candidate.immediate = (uint8_t)last;
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | (x86_target_requirement(record->form) & ~BUSTER_X86_TARGET_KNOWN);
        }
    }
    const IrSimdTypedStatus status = candidate.aes && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 2;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The compresses and expands into a register (#1840): a row of two records, VECTOR(0) the source and VECTOR(1) the
// passthrough, both of the result's width (16, 32 or 64 bytes), and MASK(2) a writemask with a bit per lane: a
// vpcompressb to vpcompressq of VECTOR(0) (reg) into VECTOR(1) (rm) or a vpexpandb to vpexpandq of VECTOR(0) (rm) into
// VECTOR(1) (reg), register-direct and merged under MASK(2) (ir_simd_compress_forms, ir_simd_expand_forms), then the
// whole of VECTOR(1) moved into VECTOR(0) (the compress and expand builtins' rows): the IR's compress or expand of the
// source under the mask, merged into the passthrough, its flags the lanes' bytes' log2. Its features are its form's (VBMI2
// for bytes and words, VL below 64 bytes), its vectors' moves and its mask's (DQ for a byte's, BW for 4 bytes'); otherwise
// it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_compress(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 2 && source->operand_count == 3 &&
                          source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[0] == bytes && source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR &&
                          source->operand_bytes[1] == bytes && source->operand_classes[2] == BUSTER_TYPED_CLASS_MASK;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const X86Instruction moved = { .form = ir_simd_whole_moves[width], .reg = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT };
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t form = 0; admitted && form < 8; form += 1)
    {
        const bool expand = form >= 4;
        const uint32_t size = form & 3;
        const uint32_t lanes = bytes >> size;
        const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
        const X86Instruction expected = {
            .form = expand ? ir_simd_expand_forms[size][width] : ir_simd_compress_forms[size][width],
            .reg = expand ? v1 : v0,
            .rm = expand ? v0 : v1,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .decorations = BUSTER_TIER0_DECORATION_MASK(2),
        };
        if (source->operand_bytes[2] == mask_bytes && memcmp(&source->records[0], &expected, sizeof(expected)) == 0 && memcmp(&source->records[1], &moved, sizeof(moved)) == 0)
        {
            candidate.compress = (uint8_t)((expand ? BUSTER_IR_INTRINSIC_EXPAND : BUSTER_IR_INTRINSIC_COMPRESS) + 1);
            candidate.immediate = (uint8_t)size;
            const uint64_t mask = (mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (mask_bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN) | mask;
        }
    }
    const IrSimdTypedStatus status = candidate.compress != 0 && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 3;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The conversions (#1840): a row of one record of an extension's or a truncation's form (ir_simd_typed_extensions,
// ir_simd_typed_truncations), register-direct and undecorated, of one vector of 16, 32 or 64 bytes into VECTOR(0)'s
// register (__builtin_convertvector's rows): an extension's reg VECTOR(0), its destination, and rm VECTOR(0), its
// source, at the result's width, its source an SSE carrier of 4 or 8 bytes too (an F32 or F64, whose bytes its form
// reads); a truncation's reg its source and rm its destination at the source's width. And a
// truncation's builtin's row (TRUNCATE's) whose lanes fill its result: its first record the form from VECTOR(0) into
// VECTOR(1)'s register under MASK(2), its second VECTOR(1) moved whole into VECTOR(0), its result merged under the
// writemask into its second operand at the result's lanes, which Tier 1 selects as a vselect, or not where the typed
// stream's writemask has every lane. Its flags are the pair's sizes and the sign or mode. Its features are its form's
// (BW for words' lanes) and AVX512F's and VL's; otherwise, and for a truncation under a mask whose lanes leave its
// result's upper bytes zero (whose merge isn't a vselect of the result's lanes), it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_convert(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint32_t bytes = source->result_bytes;
    const uint32_t first = source->operand_bytes[0];
    // An extension's source may be an SSE carrier (#1840, #1280: a vector of 4 or 8 bytes, an F32 or F64 in the IR), whose
    // register its form reads the low bytes of.
    const bool carrier = source->operand_count == 1 && source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && (first == 4 || first == 8);
    const bool vectors = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->operand_count != 0 &&
                         source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && (first == 16 || first == 32 || first == 64 || carrier);
    const uint32_t from_type = first == 4 ? IR_F32 : first == 8 ? IR_F64 : IR_V4I32 + (first == 16 ? 0 : first == 32 ? 1 : 2);
    const bool masked = vectors && !carrier && source->operand_count == 3 && source->record_count == 2 && source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR &&
                        source->operand_bytes[1] == bytes && source->operand_classes[2] == BUSTER_TYPED_CLASS_MASK;
    const bool plain = vectors && source->operand_count == 1 && source->record_count == 1;
    const X86Instruction* record = &source->records[0];
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t pair = 0; plain && pair < 6; pair += 1)
    {
        for (uint32_t sign = 0; sign < 2; sign += 1)
        {
            const uint32_t from = ir_simd_typed_extension_pairs[pair][0];
            const uint32_t to = ir_simd_typed_extension_pairs[pair][1];
            const X86Instruction expected = { .form = ir_simd_typed_extensions[sign][pair][bytes == 16 ? 0 : bytes == 32 ? 1 : 2], .reg = v0, .rm = v0, .mode = BUSTER_X86_REGISTER_DIRECT };
            const uint32_t flags = from | to << 2 | sign << 4;
            if (memcmp(record, &expected, sizeof(expected)) == 0 && ir_convert_flags_valid(BUSTER_IR_INTRINSIC_EXTEND, from_type, IR_V4I32 + (bytes == 16 ? 0 : bytes == 32 ? 1 : 2), flags))
            {
                candidate.convert = BUSTER_IR_INTRINSIC_EXTEND + 1;
                candidate.immediate = (uint8_t)flags;
            }
        }
    }
    for (uint32_t pair = 0; ((plain && !carrier) || masked) && pair < 6; pair += 1)
    {
        for (uint32_t mode = 0; mode < 3; mode += 1)
        {
            const uint32_t from = ir_simd_typed_truncation_pairs[pair][0];
            const uint32_t to = ir_simd_typed_truncation_pairs[pair][1];
            const X86Instruction expected = {
                .form = ir_simd_typed_truncations[mode][pair][first == 16 ? 0 : first == 32 ? 1 : 2],
                .reg = v0,
                .rm = masked ? v1 : v0,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .decorations = masked ? BUSTER_TIER0_DECORATION_MASK(2) : 0,
            };
            const X86Instruction moved = { .form = bytes == 16 ? X86_FORM_VMOVDQU64_X_XM128 : X86_FORM_VMOVDQU64_Y_YM256, .reg = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT };
            const uint32_t flags = from | to << 2 | mode << 4;
            const uint32_t lanes = first >> from;
            const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
            // Under a mask, its lanes fill its result (no upper bytes the instruction zeroes), and its writemask has a bit
            // for each.
            const bool full = !masked || (lanes << to == bytes && source->operand_bytes[2] == mask_bytes && memcmp(&source->records[1], &moved, sizeof(moved)) == 0);
            if (memcmp(record, &expected, sizeof(expected)) == 0 && full &&
                ir_convert_flags_valid(BUSTER_IR_INTRINSIC_TRUNCATE, IR_V4I32 + (first == 16 ? 0 : first == 32 ? 1 : 2), IR_V4I32 + (bytes == 16 ? 0 : bytes == 32 ? 1 : 2), flags))
            {
                candidate.convert = BUSTER_IR_INTRINSIC_TRUNCATE + 1;
                candidate.immediate = (uint8_t)flags;
                candidate.merge_mask = masked;
                candidate.lane = to == 0 ? IR_SIMD_LANE_I8 : to == 1 ? IR_SIMD_LANE_I16 : IR_SIMD_LANE_I32;
            }
        }
    }
    const uint64_t mask = candidate.merge_mask ? (source->operand_bytes[2] == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (source->operand_bytes[2] >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0) |
                                                     (candidate.lane == IR_SIMD_LANE_I8 || candidate.lane == IR_SIMD_LANE_I16 ? BUSTER_X86_FEATURE(AVX512BW) : 0) :
                                                 0;
    candidate.required_features = ir_typed_vector_moves(bytes, target_features) | ir_typed_vector_moves(first, target_features) | BUSTER_X86_FEATURE(AVX512F) |
                                  BUSTER_X86_FEATURE(AVX512VL) | (x86_target_requirement(record->form) & ~BUSTER_X86_TARGET_KNOWN) | mask;
    const IrSimdTypedStatus status = candidate.convert != 0 && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK :
                                                                                                                                              IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + (bytes == 16 ? 0 : bytes == 32 ? 1 : 2));
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The shifts by a scalar count (#1840): the x86 builtins' shifts by an int (c_check_program_x86.c's CHECK_X86_LOWER_SHIFT,
// `__builtin_ia32_psllwi512` to `__builtin_ia32_psraqi128`, whose header wrappers pass the count as a parameter), a row
// of a vector of 16, 32 or 64 bytes and an I32 in, the vector out: vmovd of INTEGER(1) into VECTOR(1)'s register, which
// zeroes its bytes past the count's 32 bits, then the shift's form of VECTOR(0) (vvvv) by VECTOR(1) (rm) into VECTOR(0)
// (reg), both register-direct with no decoration. It is the IR's scalar shift (BUSTER_IR_INTRINSIC_SHIFT) of the vector
// by the I32, read unsigned, as the xmm count's low quadword is: of words, doublewords and quadwords, left, logical
// right and arithmetic right. Its features are its forms' (BW for words) and its vectors' moves, those of the forms Tier 1
// selects (vmovd and the shift, or the shift by an immediate where the count is constant); otherwise it stays a machine
// row.
static IrSimdTypedStatus ir_simd_typed_shift(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const X86Instruction moved = { .form = X86_FORM_VMOVD_X_RM32_EVEX, .reg = v1, .rm = BUSTER_TIER0_ROLE_INTEGER(1), .mode = BUSTER_X86_REGISTER_DIRECT };
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 2 &&
                          source->operand_count == 2 && source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[0] == bytes &&
                          source->operand_classes[1] == BUSTER_TYPED_CLASS_I32 && memcmp(&source->records[0], &moved, sizeof(moved)) == 0;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t lanes = 1; admitted && lanes < 4; lanes += 1)
    {
        for (uint32_t kind = 0; kind < 3; kind += 1)
        {
            const X86Instruction expected = { .form = ir_simd_shift_forms[kind][lanes - 1][width], .reg = v0, .vvvv = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT };
            if (expected.form != X86_FORM_NONE && memcmp(&source->records[1], &expected, sizeof(expected)) == 0)
            {
                candidate.shift = true;
                candidate.immediate = (uint8_t)(lanes | kind << 2);
                candidate.required_features = ir_typed_vector_moves(bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                              ((x86_target_requirement(moved.form) | x86_target_requirement(expected.form) |
                                                x86_target_requirement(ir_simd_shift_immediate_forms[kind][lanes - 1][width])) &
                                               ~BUSTER_X86_TARGET_KNOWN);
            }
        }
    }
    const IrSimdTypedStatus status = candidate.shift && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 2;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The lane operations (#1840): clang's __builtin_elementwise_min, _max, _add_sat and _sub_sat of integer vectors, as
// c_check_program_vector_builtins.c makes their rows: two vectors of the result's width (16, 32 or 64 bytes) and one
// record of vpminub to vpmaxsq or vpaddusb to vpsubsw of VECTOR(0) (vvvv) and VECTOR(1) (rm) into VECTOR(0) (reg),
// register-direct and undecorated (ir_simd_lane_op_forms). It is the IR's lane_op of the two, its flags the lanes and the
// operation. Its features are the form's (BW for bytes and words), AVX512F's, VL's below 64 bytes and its vectors'
// moves; otherwise it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_lane_op(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 1 &&
                          source->operand_count == 2 && source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[0] == bytes &&
                          source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[1] == bytes;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t operation = 0; admitted && operation < 8; operation += 1)
    {
        for (uint32_t lanes = 0; lanes < 4; lanes += 1)
        {
            const X86Instruction expected = { .form = ir_simd_lane_op_forms[operation][lanes][width], .reg = v0, .vvvv = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT };
            if (expected.form != X86_FORM_NONE && memcmp(&source->records[0], &expected, sizeof(expected)) == 0)
            {
                candidate.lane_op = true;
                candidate.immediate = (uint8_t)(lanes | operation << 2);
                candidate.required_features = ir_typed_vector_moves(bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                              (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN);
            }
        }
    }
    const IrSimdTypedStatus status = candidate.lane_op && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 2;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The masks' conversions to vectors and back (#1840): the MASK_VECTOR rules' rows (`__builtin_ia32_cvtmask2b` to
// `_cvtmask2q`), one record of vpmovm2b to vpmovm2q of MASK(0) (rm) into VECTOR(0) (reg) at the result's width (16, 32 or
// 64 bytes), the mask of a bit per lane (a byte for 8 lanes or fewer); and the VECTOR_MASK rules' rows
// (`__builtin_ia32_cvtb2mask` to `_cvtq2mask`), one record of vpmovb2m to vpmovq2m of VECTOR(0) (rm) into MASK_RESULT
// (reg), the mask's bytes the lanes'. Both register-direct and undecorated, as the opmask conversion leaves them. The
// first is the IR's vselect of the mask, all ones and zeros at the lanes; the second its comparison of signed lanes
// less than zero, whose sign bits the instruction reads. Their features are the forms' (BW for bytes and words, DQ for
// doublewords and quadwords), AVX512F's, VL's below 64 bytes and the vector's and the mask's moves (DQ for a byte's, BW
// for 32 and 64 lanes'); Tier 1 selects each back as its instruction (tier1_select.c). Otherwise it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_mask_vector(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint16_t expansions[4][3] = {
        { X86_FORM_VPMOVM2B_X_K, X86_FORM_VPMOVM2B_Y_K, X86_FORM_VPMOVM2B_Z_K },
        { X86_FORM_VPMOVM2W_X_K, X86_FORM_VPMOVM2W_Y_K, X86_FORM_VPMOVM2W_Z_K },
        { X86_FORM_VPMOVM2D_X_K, X86_FORM_VPMOVM2D_Y_K, X86_FORM_VPMOVM2D_Z_K },
        { X86_FORM_VPMOVM2Q_X_K, X86_FORM_VPMOVM2Q_Y_K, X86_FORM_VPMOVM2Q_Z_K },
    };
    static const uint16_t signs[4][3] = {
        { X86_FORM_VPMOVB2M_K_X, X86_FORM_VPMOVB2M_K_Y, X86_FORM_VPMOVB2M_K_Z },
        { X86_FORM_VPMOVW2M_K_X, X86_FORM_VPMOVW2M_K_Y, X86_FORM_VPMOVW2M_K_Z },
        { X86_FORM_VPMOVD2M_K_X, X86_FORM_VPMOVD2M_K_Y, X86_FORM_VPMOVD2M_K_Z },
        { X86_FORM_VPMOVQ2M_K_X, X86_FORM_VPMOVQ2M_K_Y, X86_FORM_VPMOVQ2M_K_Z },
    };
    static const uint8_t lanes_of[4] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I32, IR_SIMD_LANE_I64 };
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const bool expand = source->result_class == BUSTER_TYPED_CLASS_VECTOR && source->operand_count == 1 && source->operand_classes[0] == BUSTER_TYPED_CLASS_MASK;
    const bool sign = source->result_class == BUSTER_TYPED_CLASS_MASK && source->operand_count == 1 && source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR;
    const uint32_t bytes = expand ? source->result_bytes : source->operand_bytes[0];
    const uint32_t mask_bytes = expand ? source->operand_bytes[0] : source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = (expand || sign) && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 1;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t size = 0; admitted && size < 4; size += 1)
    {
        const uint32_t lanes = bytes >> size;
        const X86Instruction expected = {
            .form = expand ? expansions[size][width] : signs[size][width],
            .reg = expand ? v0 : BUSTER_TIER0_ROLE_MASK_RESULT,
            .rm = expand ? BUSTER_TIER0_ROLE_MASK(0) : v0,
            .mode = BUSTER_X86_REGISTER_DIRECT,
        };
        if (memcmp(&source->records[0], &expected, sizeof(expected)) == 0 && mask_bytes == (lanes <= 8 ? 1 : lanes / 8))
        {
            candidate.mask_expand = expand;
            candidate.sign_mask = sign;
            candidate.lane = (IrSimdLane)lanes_of[size];
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                          (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN) | (mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) |
                                          (mask_bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
        }
    }
    const bool decoded = candidate.mask_expand || candidate.sign_mask;
    const IrSimdTypedStatus status = decoded && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 1;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A masked move's select (#1840): the SELECT rule's row as the opmask conversion leaves it (c_check_program_x86.c's
// CHECK_X86_LOWER_SELECT, `_mm512_mask_mov_epi8`, `_mm512_mask_blend_epi32` and the rest at each width): a mask and two
// vectors of one width (16, 32 or 64 bytes) in, VECTOR(2)'s register moved whole into VECTOR(0)'s (vmovdqu64), then
// VECTOR(1)'s moved into it under MASK(0) by the move of its elements (vmovdqu8 to vmovdqu64), register-direct with no
// other field or decoration. It is the IR's vselect of the mask, VECTOR(1) and VECTOR(2), its lanes the move's elements'
// integers, whose mask must be the lanes' (ir_lane_mask's bytes: a byte for 8 lanes or fewer). Its features are the
// forms Tier 1 selects for it: vpblendm at its lanes' width (BW for bytes and words) and its moves (AVX512F, VL below 64
// bytes; the mask's: DQ for a byte's, BW for 32 and 64 lanes'). Otherwise it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_select(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint16_t moves[4][3] = {
        { X86_FORM_VMOVDQU8_X_XM128, X86_FORM_VMOVDQU8_Y_YM256, X86_FORM_VMOVDQU8_Z_ZM512 },
        { X86_FORM_VMOVDQU16_X_XM128, X86_FORM_VMOVDQU16_Y_YM256, X86_FORM_VMOVDQU16_Z_ZM512 },
        { X86_FORM_VMOVDQU32_X_XM128, X86_FORM_VMOVDQU32_Y_YM256, X86_FORM_VMOVDQU32_Z_ZM512 },
        { X86_FORM_VMOVDQU64_X_XM128, X86_FORM_VMOVDQU64_Y_YM256, X86_FORM_VMOVDQU64_Z_ZM512 },
    };
    static const uint8_t lanes[4] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I32, IR_SIMD_LANE_I64 };
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 2 &&
                          source->operand_count == 3 && source->operand_classes[0] == BUSTER_TYPED_CLASS_MASK && source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR &&
                          source->operand_bytes[1] == bytes && source->operand_classes[2] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[2] == bytes;
    const X86Instruction whole = {
        .form = moves[3][width],
        .reg = BUSTER_TIER0_ROLE_VECTOR(0),
        .rm = BUSTER_TIER0_ROLE_VECTOR(2),
        .mode = BUSTER_X86_REGISTER_DIRECT,
    };
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t size = 0; admitted && size < 4; size += 1)
    {
        const X86Instruction merged = {
            .form = moves[size][width],
            .reg = BUSTER_TIER0_ROLE_VECTOR(0),
            .rm = BUSTER_TIER0_ROLE_VECTOR(1),
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .decorations = BUSTER_TIER0_DECORATION_MASK(0),
        };
        const uint32_t count = bytes >> size;
        const uint32_t mask_bytes = count <= 8 ? 1 : count / 8;
        if (memcmp(&source->records[0], &whole, sizeof(whole)) == 0 && memcmp(&source->records[1], &merged, sizeof(merged)) == 0 && source->operand_bytes[0] == mask_bytes)
        {
            candidate.operation = IR_SIMD_SELECT;
            candidate.lane = (IrSimdLane)lanes[size];
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) |
                                          ((x86_target_requirement(whole.form) | x86_target_requirement(merged.form)) & ~BUSTER_X86_TARGET_KNOWN) |
                                          (mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (mask_bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
        }
    }
    const IrSimdTypedStatus status = candidate.operation != IR_SIMD_OPERATION_COUNT && (target_features & candidate.required_features) == candidate.required_features ?
                                         IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// An undefined vector's row (#1840, c_check_program_x86.c's CHECK_X86_LOWER_UNDEFINED, `_mm512_undefined_epi32` and the
// rest): no operand, one record of vpxorq of VECTOR(0) with itself at the result's width, register-direct and undecorated.
// It is the IR's zero vector of that width, which the row gives (a refinement of an undefined value). Its features are
// the form's and the vector's moves; otherwise it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_zero(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint16_t forms[3] = { X86_FORM_VPXORQ_X_X_XM128, X86_FORM_VPXORQ_Y_Y_YM256, X86_FORM_VPXORQ_Z_Z_ZM512 };
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const X86Instruction expected = { .form = forms[width], .reg = v0, .vvvv = v0, .rm = v0, .mode = BUSTER_X86_REGISTER_DIRECT };
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 1 &&
                          source->operand_count == 0 && memcmp(&source->records[0], &expected, sizeof(expected)) == 0;
    const uint64_t required = ir_typed_vector_moves(bytes, target_features) | (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN);
    const IrSimdTypedStatus status = admitted && (target_features & required) == required ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        const IrSimdTypedOperation candidate = {
            .operation = IR_SIMD_OPERATION_COUNT,
            .shape = (IrSimdShape)(IR_SIMD_V4I32 + width),
            .required_features = required,
            .zero = true,
        };
        *result = candidate;
    }
    return status;
}

// The masked loads and stores (#1840): the masked load builtins' rows (CHECK_X86_LOWER_LOAD: loaddqu, loadups, loadupd,
// loadaps, loadapd, movdqa32load, movdqa64load, and the scalar loadss128 and loadsd128 of the whole vector) and the stores'
// (CHECK_X86_LOWER_STORE). Of a U64 address, a vector and its mask: a load's first record its move of the vector's width
// from [INTEGER(0)] into VECTOR(1), the passthrough, under MASK(2), its second VECTOR(1) moved whole into VECTOR(0); a
// store's one record the move of VECTOR(1) to [INTEGER(0)] under MASK(2). Its lanes are its move's element: vmovdqu8 to
// vmovdqu64's, vmovdqa32's and vmovups's and vmovaps's doublewords, vmovdqa64's, vmovupd's and vmovapd's quadwords; an aligned
// move's (vmovdqa, vmovaps, vmovapd) alignment the vector's bytes, which it faults without. Its features are its forms'
// (BW for bytes and words, VL below 64 bytes), the moves' and the mask's (DQ for a byte, BW for 4 bytes or more);
// otherwise it stays a machine row, as the expanding loads do.
static IrSimdTypedStatus ir_simd_typed_masked_memory(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    // Each kind's (ir_simd_masked_moves) lanes' bytes' log2 and whether it is aligned.
    static const uint8_t sizes[10] = { 0, 1, 2, 3, 2, 3, 2, 3, 2, 3 };
    static const bool aligned[10] = { false, false, false, false, true, true, false, false, true, true };
    static const uint8_t lanes[4] = { IR_SIMD_LANE_I8, IR_SIMD_LANE_I16, IR_SIMD_LANE_I32, IR_SIMD_LANE_I64 };
    static const uint16_t whole[3] = { X86_FORM_VMOVDQU64_X_XM128, X86_FORM_VMOVDQU64_Y_YM256, X86_FORM_VMOVDQU64_Z_ZM512 };
    const bool store = source->result_class == BUSTER_TYPED_CLASS_VOID;
    const uint32_t bytes = source->operand_bytes[1];
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = (store || (source->result_class == BUSTER_TYPED_CLASS_VECTOR && source->result_bytes == bytes)) && (bytes == 16 || bytes == 32 || bytes == 64) &&
                          source->record_count == (store ? 1 : 2) && source->operand_count == 3 && source->operand_classes[0] == BUSTER_TYPED_CLASS_U64 &&
                          source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR && source->operand_classes[2] == BUSTER_TYPED_CLASS_MASK;
    const X86Instruction moved = { .form = whole[width], .reg = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1), .mode = BUSTER_X86_REGISTER_DIRECT };
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t kind = 0; admitted && kind < 10; kind += 1)
    {
        const uint32_t size = sizes[kind];
        const uint32_t count = bytes >> size;
        const uint32_t mask_bytes = count <= 8 ? 1 : count / 8;
        const X86Instruction expected = {
            .form = ir_simd_masked_moves[kind][store][width],
            .reg = BUSTER_TIER0_ROLE_VECTOR(1),
            .rm = BUSTER_TIER0_ROLE_INTEGER(0),
            .mode = BUSTER_X86_MEMORY_BASE,
            .decorations = BUSTER_TIER0_DECORATION_MASK(2),
        };
        if (source->operand_bytes[2] == mask_bytes && memcmp(&source->records[0], &expected, sizeof(expected)) == 0 &&
            (store || memcmp(&source->records[1], &moved, sizeof(moved)) == 0))
        {
            candidate.masked_memory = (uint8_t)(store ? 2 : 1);
            candidate.lane = (IrSimdLane)lanes[size];
            candidate.immediate = (uint8_t)(aligned[kind] ? 4 + width : 0);
            const uint64_t mask = (mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (mask_bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) |
                                          ((x86_target_requirement(expected.form) | (store ? 0 : x86_target_requirement(moved.form))) & ~BUSTER_X86_TARGET_KNOWN) | mask;
        }
    }
    // A compressing store (#1840): vpcompressb to vpcompressq of VECTOR(1) (reg) to [INTEGER(0)] under MASK(2), the
    // compress-store builtins' rows; its features are its form's (VBMI2 for bytes and words, VL below 64 bytes).
    for (uint32_t size = 0; admitted && store && candidate.masked_memory == 0 && size < 4; size += 1)
    {
        const uint32_t count = bytes >> size;
        const uint32_t mask_bytes = count <= 8 ? 1 : count / 8;
        const X86Instruction expected = {
            .form = ir_simd_compress_forms[size][width],
            .reg = BUSTER_TIER0_ROLE_VECTOR(1),
            .rm = BUSTER_TIER0_ROLE_INTEGER(0),
            .mode = BUSTER_X86_MEMORY_BASE,
            .decorations = BUSTER_TIER0_DECORATION_MASK(2),
        };
        if (source->operand_bytes[2] == mask_bytes && memcmp(&source->records[0], &expected, sizeof(expected)) == 0)
        {
            candidate.masked_memory = 3;
            candidate.lane = (IrSimdLane)lanes[size];
            candidate.immediate = 0;
            const uint64_t mask = (mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (mask_bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) |
                                          (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN) | mask;
        }
    }
    // A truncating store under a mask (#1840): vpmovwb to vpmovqd, signed or unsigned saturating or not, of VECTOR(1) (reg)
    // to [INTEGER(0)] under MASK(2), a mask of the source's lanes, the truncating stores' rows. Its truncated lanes fill
    // 16 bytes or more (a truncation's result's, ir_convert_flags_valid), which a masked store of those lanes writes; its
    // features are its form's and the source's moves.
    for (uint32_t pair = 0; admitted && store && candidate.masked_memory == 0 && pair < 6; pair += 1)
    {
        for (uint32_t mode = 0; mode < 3; mode += 1)
        {
            const uint32_t from = ir_simd_typed_truncation_pairs[pair][0];
            const uint32_t to = ir_simd_typed_truncation_pairs[pair][1];
            const uint32_t count = bytes >> from;
            const uint32_t mask_bytes = count <= 8 ? 1 : count / 8;
            const X86Instruction expected = {
                .form = ir_simd_typed_truncations[mode][pair][width],
                .reg = BUSTER_TIER0_ROLE_VECTOR(1),
                .rm = BUSTER_TIER0_ROLE_INTEGER(0),
                .mode = BUSTER_X86_MEMORY_BASE,
                .decorations = BUSTER_TIER0_DECORATION_MASK(2),
            };
            if (source->operand_bytes[2] == mask_bytes && (count << to) >= 16 && memcmp(&source->records[0], &expected, sizeof(expected)) == 0)
            {
                candidate.masked_memory = 5;
                candidate.lane = (IrSimdLane)lanes[to];
                candidate.immediate = (uint8_t)(from | to << 2 | mode << 4);
                const uint64_t mask = (mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0) | (mask_bytes >= 4 ? BUSTER_X86_FEATURE(AVX512BW) : 0) |
                                      (to <= 1 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
                candidate.required_features = ir_typed_vector_moves(bytes, target_features) | ir_typed_vector_moves(count << to, target_features) |
                                              BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN) | mask;
            }
        }
    }
    const IrSimdTypedStatus status = candidate.masked_memory != 0 && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 3;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The gathers (#1840): a row of a vector passthrough VECTOR(0) of the result's 16, 32 or 64 bytes, a U64 base INTEGER(1),
// an index vector VECTOR(2) and a mask MASK(3) of the result's lanes, of one record: vpgatherdd to vpgatherqq, or
// vgatherdps to vgatherqpd (whose lanes' bits are the same), into VECTOR(0) under MASK(3) at [INTEGER(1) + VECTOR(2) *
// scale] (the gather builtins' rows). It is the IR's gather of the lanes, dwords or qwords, by the index's elements, one
// per lane, merged into the passthrough where the mask's bits are clear by a vselect: the shapes whose index has exactly
// those elements (vpgatherdq of 32 and 64 bytes, vpgatherqd of 16 and 32, and the others at each width). Its features
// are its form's (VL below 64 bytes), its vectors' moves and its mask's (DQ for a byte's); otherwise it stays a machine
// row, as the AVX2 gathers by a vector mask do.
static IrSimdTypedStatus ir_simd_typed_gather(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const uint32_t index_bytes = source->operand_bytes[2];
    const X86Instruction* record = &source->records[0];
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 1 && source->operand_count == 4 &&
                          source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[0] == bytes && source->operand_classes[1] == BUSTER_TYPED_CLASS_U64 &&
                          source->operand_classes[2] == BUSTER_TYPED_CLASS_VECTOR && (index_bytes == 16 || index_bytes == 32 || index_bytes == 64) &&
                          source->operand_classes[3] == BUSTER_TYPED_CLASS_MASK && record->reg == BUSTER_TIER0_ROLE_VECTOR(0) && record->vvvv == 0 &&
                          record->rm == BUSTER_TIER0_ROLE_INTEGER(1) && record->index == BUSTER_TIER0_ROLE_VECTOR(2) && record->displacement == 0 && record->immediate == 0 &&
                          (record->mode & ~3u) == (BUSTER_X86_MEMORY_BASE_INDEX(0) & ~3u) && record->decorations == BUSTER_TIER0_DECORATION_MASK(3);
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t kind = 0; admitted && kind < 8; kind += 1)
    {
        // The lanes' and the index's elements: 0 for dwords, 1 for qwords.
        const uint32_t lane = (kind >> 1) & 1;
        const uint32_t index = kind & 1;
        const uint32_t count = bytes >> (2 + lane);
        const uint32_t mask_bytes = count <= 8 ? 1 : count / 8;
        const uint16_t form = ir_simd_gather_forms[kind >> 2][lane][index][width];
        if (form != X86_FORM_NONE && record->form == form && index_bytes == count << (2 + index) && source->operand_bytes[3] == mask_bytes)
        {
            candidate.masked_memory = 4;
            candidate.lane = lane == 0 ? IR_SIMD_LANE_I32 : IR_SIMD_LANE_I64;
            candidate.immediate = (uint8_t)(record->mode & 3);
            const uint64_t mask = mask_bytes == 1 ? BUSTER_X86_FEATURE(AVX512DQ) : 0;
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | ir_typed_vector_moves(index_bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) |
                                          (x86_target_requirement(form) & ~BUSTER_X86_TARGET_KNOWN) | mask;
        }
    }
    const IrSimdTypedStatus status = candidate.masked_memory != 0 && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 4;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A cast into an SSE carrier (#1840, #1280: a vector of 4 or 8 bytes, an F32 or F64 in the IR): a row of no record of
// one vector of 16, 32 or 64 bytes into a vector of 4 or 8 bytes, whose result is the operand's register
// (c_check_program_vector_builtins.c's shuffles of a vector's first elements into a narrower one, as clang's headers'
// _mm256_cvtepu8_epi32 and the like take a vector's first bytes before their conversion). It is the IR's lane_extract of
// lane 0 of doublewords or quadwords at the carrier's type, which Tier 1 moves into the carrier's register zero-extended
// where that isn't the vector's. Its features are the vectors' moves, AVX512F's and VL's; otherwise it stays a machine
// row.
static IrSimdTypedStatus ir_simd_typed_carrier_cast(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint32_t bytes = source->result_bytes;
    const uint32_t first = source->operand_bytes[0];
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 4 || bytes == 8) && source->record_count == 0 && source->operand_count == 1 &&
                          source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && (first == 16 || first == 32 || first == 64);
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    candidate.required_features = ir_typed_vector_moves(bytes, target_features) | ir_typed_vector_moves(first, target_features) | BUSTER_X86_FEATURE(AVX512F) |
                                  BUSTER_X86_FEATURE(AVX512VL);
    const IrSimdTypedStatus status = admitted && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.lane_move = BUSTER_IR_INTRINSIC_LANE_EXTRACT + 1;
        candidate.immediate = (uint8_t)(bytes == 8 ? 3 : 2);
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + (first == 16 ? 0 : first == 32 ? 1 : 2));
        candidate.operand_count = 1;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// One lane of a vector and its lanes' sign bits (#1840): a row of one vector of 16, 32 or 64 bytes into an integer, its
// last record vmovd or vmovq of a lane 0 of 4 or 8 bytes, or vpextrb to vpextrq of a lane below 16 bytes, from VECTOR(0)
// or, after vextracti32x4 of VECTOR(0)'s part past the low one into VECTOR(1), from VECTOR(1), into INTEGER_RESULT (an
// integer of the lane's bytes, or of 4 for a narrower lane, which the instruction zero-extends into); a row of a 16-byte
// vector and an integer into a vector, vpinsrb to vpinsrq of INTEGER(1) into VECTOR(0)'s lane below 16 bytes (an integer
// of 8 bytes for a quadword, of at most 4 for a narrower lane); and a row of a 16- or 32-byte vector into an int,
// vpmovmskb, vmovmskps or vmovmskpd of VECTOR(0) into INTEGER_RESULT. Register-direct and undecorated; their features
// their records' and the vector's moves; otherwise they stay machine rows.
static IrSimdTypedStatus ir_simd_typed_lane(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t out = BUSTER_TIER0_ROLE_INTEGER_RESULT;
    const bool vector_in = source->operand_count >= 1 && source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR;
    const uint32_t bytes = vector_in ? source->operand_bytes[0] : 0;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool sized = bytes == 16 || bytes == 32 || bytes == 64;
    const bool scalar_out = tier0_class_integer(source->result_class);
    const uint32_t out_bytes = scalar_out ? tier0_class_bytes(source->result_class) : 0;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    uint64_t required = 0;
    // A lane's extraction: one record, or the part's extraction and one.
    const bool extract_shape = vector_in && sized && scalar_out && source->operand_count == 1 && (source->record_count == 1 || source->record_count == 2);
    const uint32_t part = extract_shape && source->record_count == 2 ? (uint32_t)source->records[0].immediate : 0;
    const X86Instruction part_record = {
        .form = width != 0 ? ir_simd_part_extract_forms[width == 2] : X86_FORM_NONE,
        .reg = v0,
        .rm = v1,
        .mode = BUSTER_X86_REGISTER_DIRECT,
        .immediate = (int64_t)part,
    };
    const bool parted = extract_shape && source->record_count == 2 && width != 0 && part != 0 && part < bytes / 16 &&
                        memcmp(&source->records[0], &part_record, sizeof(part_record)) == 0;
    const uint8_t from = parted ? v1 : v0;
    const X86Instruction* last = extract_shape ? &source->records[source->record_count - 1] : 0;
    for (uint32_t size = 0; (parted || (extract_shape && source->record_count == 1)) && size < 4; size += 1)
    {
        const uint32_t per = 16u >> size;
        const uint32_t sub = (uint32_t)last->immediate;
        const bool word = size == 1;
        const X86Instruction lane = {
            .form = ir_simd_lane_extract_forms[size],
            .reg = word ? out : from,
            .rm = word ? from : out,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .immediate = (int64_t)sub,
        };
        const X86Instruction whole = { .form = size == 2 ? X86_FORM_VMOVD_RM32_X_EVEX : X86_FORM_VMOVQ_RM64_X_EVEX, .reg = from, .rm = out, .mode = BUSTER_X86_REGISTER_DIRECT };
        const bool moved = size >= 2 && memcmp(last, &whole, sizeof(whole)) == 0;
        const bool extracted = sub < per && memcmp(last, &lane, sizeof(lane)) == 0;
        const bool fits = size == 3 ? out_bytes == 8 : out_bytes == 4 || out_bytes == (1u << size);
        if ((moved || extracted) && fits)
        {
            candidate.lane_move = (uint8_t)(BUSTER_IR_INTRINSIC_LANE_EXTRACT + 1);
            candidate.immediate = (uint8_t)(size | ((part * per + (moved ? 0 : sub)) << 2));
            required = x86_target_requirement(last->form) | (parted ? x86_target_requirement(part_record.form) : 0);
        }
    }
    // A lane's insertion into a 16-byte vector.
    const bool insert_shape = vector_in && bytes == 16 && source->result_class == BUSTER_TYPED_CLASS_VECTOR && source->result_bytes == 16 && source->operand_count == 2 &&
                              tier0_class_integer(source->operand_classes[1]) && source->record_count == 1;
    for (uint32_t size = 0; insert_shape && size < 4; size += 1)
    {
        const uint32_t sub = (uint32_t)source->records[0].immediate;
        const uint32_t in_bytes = tier0_class_bytes(source->operand_classes[1]);
        const X86Instruction insert = {
            .form = ir_simd_lane_insert_forms[size],
            .reg = v0,
            .vvvv = v0,
            .rm = BUSTER_TIER0_ROLE_INTEGER(1),
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .immediate = (int64_t)sub,
        };
        if (sub < (16u >> size) && (size == 3 ? in_bytes == 8 : in_bytes <= 4) && memcmp(&source->records[0], &insert, sizeof(insert)) == 0)
        {
            candidate.lane_move = (uint8_t)(BUSTER_IR_INTRINSIC_LANE_INSERT + 1);
            candidate.immediate = (uint8_t)(size | (sub << 2));
            required = x86_target_requirement(insert.form);
        }
    }
    // The lanes' sign bits.
    const bool mask_shape = vector_in && (bytes == 16 || bytes == 32) && scalar_out && out_bytes == 4 && source->operand_count == 1 && source->record_count == 1;
    for (uint32_t size = 0; mask_shape && size < 4; size += 1)
    {
        const X86Instruction movemask = { .form = ir_simd_movemask_forms[size][width], .reg = out, .rm = v0, .mode = BUSTER_X86_REGISTER_DIRECT };
        if (movemask.form != X86_FORM_NONE && memcmp(&source->records[0], &movemask, sizeof(movemask)) == 0)
        {
            candidate.lane_move = (uint8_t)(BUSTER_IR_INTRINSIC_MOVEMASK + 1);
            candidate.immediate = (uint8_t)size;
            required = x86_target_requirement(movemask.form);
        }
    }
    candidate.required_features = candidate.lane_move != 0 ? ir_typed_vector_moves(bytes, target_features) | (required & ~BUSTER_X86_TARGET_KNOWN) : 0;
    const IrSimdTypedStatus status = candidate.lane_move != 0 && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A reduction of a vector's doublewords or quadwords (#1840): a row of one vector of 16, 32 or 64 bytes, its records
// clang's __builtin_reduce_* code (c_check_program_vector_builtins.c's check_vector_reduce_rows): each halving the high
// half of VECTOR(0) into VECTOR(1) (vextracti64x4 of 64 bytes, vextracti32x4 of 32, then within 16 vpshufd 0x4e and, of
// doublewords, 0xb1), then the operation of both into VECTOR(0) at the half's width (ir_simd_reduce_form), every halving
// to the lane, then vmovd or vmovq of lane 0 into INTEGER_RESULT, an integer of the lane's bytes; or the same halvings
// alone into a 16-byte vector, every lane of which they leave the result (the first row of a reduction split in two,
// whose second is lane 0's move). Register-direct and undecorated; their features their records' and the vector's
// moves. It is the IR's reduce, followed by its lane_extract of lane 0 for a row into a general register.
static IrSimdTypedStatus ir_simd_typed_reduce(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint32_t bytes = source->operand_count == 1 && source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR ? source->operand_bytes[0] : 0;
    const bool sized = bytes == 16 || bytes == 32 || bytes == 64;
    const bool scalar_out = tier0_class_integer(source->result_class);
    const bool vector_out = source->result_class == BUSTER_TYPED_CLASS_VECTOR && source->result_bytes == 16;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    uint64_t required = 0;
    for (uint32_t size = 2; sized && (scalar_out || vector_out) && size < 4; size += 1)
    {
        const bool fits = !scalar_out || tier0_class_bytes(source->result_class) == (1u << size);
        for (uint32_t operation = 0; fits && operation < BUSTER_IR_REDUCE_OPERATIONS; operation += 1)
        {
            X86Instruction expected[BUSTER_TIER0_OPERATION_RECORDS] = { 0 };
            uint32_t count = 0;
            uint64_t features = 0;
            for (uint32_t span = bytes / 2; span >= (1u << size) && count + 2 <= BUSTER_TIER0_OPERATION_RECORDS; span /= 2)
            {
                const uint32_t width = span == 32 ? 1 : 0;
                const X86Instruction halve = span >= 16 ? (X86Instruction){ .form = span == 32 ? X86_FORM_VEXTRACTI64X4_YM256_Z_IMM8 : X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8,
                                                                            .reg = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = 1 }
                                                        : (X86Instruction){ .form = X86_FORM_VPSHUFD_X_XM128_IMM8_EVEX, .reg = v1, .rm = v0, .mode = BUSTER_X86_REGISTER_DIRECT,
                                                                            .immediate = span == 8 ? 0x4e : 0xb1 };
                const X86Instruction combine = { .form = ir_simd_reduce_form(operation, size, width), .reg = v0, .vvvv = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT };
                expected[count] = halve;
                expected[count + 1] = combine;
                features |= x86_target_requirement(halve.form) | x86_target_requirement(combine.form);
                count += 2;
            }
            const bool complete = (bytes >> (count / 2)) == (1u << size) && (!scalar_out || count < BUSTER_TIER0_OPERATION_RECORDS);
            if (complete && scalar_out)
            {
                expected[count] = (X86Instruction){ .form = size == 2 ? X86_FORM_VMOVD_RM32_X_EVEX : X86_FORM_VMOVQ_RM64_X_EVEX, .reg = v0, .rm = BUSTER_TIER0_ROLE_INTEGER_RESULT,
                                                    .mode = BUSTER_X86_REGISTER_DIRECT };
                features |= x86_target_requirement(expected[count].form);
                count += 1;
            }
            if (complete && count == source->record_count && memcmp(source->records, expected, count * sizeof(*expected)) == 0)
            {
                candidate.reduce = true;
                candidate.immediate = (uint8_t)(size | operation << 2);
                required = features;
            }
        }
    }
    candidate.required_features = candidate.reduce ? ir_typed_vector_moves(bytes, target_features) | (required & ~BUSTER_X86_TARGET_KNOWN) : 0;
    const IrSimdTypedStatus status = candidate.reduce && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = IR_SIMD_V4I32;
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A unary operation of each lane (#1840): a row of one vector of 16, 32 or 64 bytes into a vector of its type, one
// record of ir_simd_lane_unary_forms' (vpopcntb to vpopcntq, vplzcntd, vplzcntq, vpconflictd, vpconflictq) from
// VECTOR(0) into it, register-direct and undecorated: clang's __builtin_elementwise_popcount and _clzg of one operand, and
// the x86 builtins' _mm512_popcnt_*, _mm512_lzcnt_* and _mm512_conflict_*. And clang's __builtin_elementwise_clzg of a
// second operand of the first's type, its lane where the first's is zero: the count into VECTOR(2), vptestnmd or
// vptestnmq of VECTOR(0) with itself into k1, then vpblendmd or vpblendmq of VECTOR(2) and VECTOR(1) under k1 into
// VECTOR(0) (c_check_program_vector_builtins.c's three records). Their features their records' and the vectors' moves.
// It is the IR's lane_unary, and for the second form the vselect of the second operand where the first's lanes equal
// zero.
static IrSimdTypedStatus ir_simd_typed_lane_unary(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint16_t tests[2][3] = {
        { X86_FORM_VPTESTNMD_K_X_XM128, X86_FORM_VPTESTNMD_K_Y_YM256, X86_FORM_VPTESTNMD_K_Z_ZM512 },
        { X86_FORM_VPTESTNMQ_K_X_XM128, X86_FORM_VPTESTNMQ_K_Y_YM256, X86_FORM_VPTESTNMQ_K_Z_ZM512 },
    };
    static const uint16_t blends[2][3] = {
        { X86_FORM_VPBLENDMD_X_X_XM128, X86_FORM_VPBLENDMD_Y_Y_YM256, X86_FORM_VPBLENDMD_Z_Z_ZM512 },
        { X86_FORM_VPBLENDMQ_X_X_XM128, X86_FORM_VPBLENDMQ_Y_Y_YM256, X86_FORM_VPBLENDMQ_Z_Z_ZM512 },
    };
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint32_t bytes = source->result_class == BUSTER_TYPED_CLASS_VECTOR ? source->result_bytes : 0;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    bool shaped = (bytes == 16 || bytes == 32 || bytes == 64) && (source->operand_count == 1 || source->operand_count == 2);
    for (uint32_t j = 0; shaped && j < source->operand_count; j += 1)
    {
        shaped = source->operand_classes[j] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[j] == bytes;
    }
    const bool fallback = shaped && source->operand_count == 2 && source->record_count == 3;
    const bool plain = shaped && source->operand_count == 1 && source->record_count == 1;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    uint64_t required = 0;
    for (uint32_t operation = 0; (plain || fallback) && operation < 3; operation += 1)
    {
        for (uint32_t size = 0; size < 4; size += 1)
        {
            const uint16_t form = ir_simd_lane_unary_forms[operation][size][width];
            const X86Instruction count = { .form = form, .reg = fallback ? v2 : v0, .rm = v0, .mode = BUSTER_X86_REGISTER_DIRECT };
            bool matched = form != X86_FORM_NONE && (plain || (operation == BUSTER_IR_LANE_UNARY_LZCNT && size >= 2)) && memcmp(&source->records[0], &count, sizeof(count)) == 0;
            uint64_t features = matched ? x86_target_requirement(form) : 0;
            if (matched && fallback)
            {
                const X86Instruction test = { .form = tests[size - 2][width], .reg = 1, .vvvv = v0, .rm = v0, .mode = BUSTER_X86_REGISTER_DIRECT };
                const X86Instruction blend = { .form = blends[size - 2][width], .reg = v0, .vvvv = v2, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT, .decorations = 1 };
                matched = memcmp(&source->records[1], &test, sizeof(test)) == 0 && memcmp(&source->records[2], &blend, sizeof(blend)) == 0;
                features |= x86_target_requirement(test.form) | x86_target_requirement(blend.form);
            }
            if (matched)
            {
                candidate.lane_unary = true;
                candidate.lane_unary_zero = fallback;
                candidate.immediate = (uint8_t)(size | operation << 2);
                candidate.lane = (IrSimdLane)ir_intrinsic_lanes[size];
                required = features;
            }
        }
    }
    candidate.required_features = candidate.lane_unary ? ir_typed_vector_moves(bytes, target_features) | (required & ~BUSTER_X86_TARGET_KNOWN) : 0;
    const IrSimdTypedStatus status = candidate.lane_unary && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A floating operation at its own rounding (#1840): a row of two 64-byte vectors into one, one record of
// ir_simd_fround_forms' (vaddps to vdivpd with embedded rounding), VECTOR(0) in vvvv and VECTOR(1) in rm into VECTOR(0),
// register-direct, its decorations the rounding alone (the builtins' _mm512_add_round_ps to _mm512_div_round_pd with
// _MM_FROUND_NO_EXC and a direction). Its features its form's and the vectors' moves. It is the IR's fround.
static IrSimdTypedStatus ir_simd_typed_fround(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const bool shaped = source->operand_count == 2 && source->record_count == 1 && source->result_class == BUSTER_TYPED_CLASS_VECTOR && source->result_bytes == 64 &&
                        source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[0] == 64 &&
                        source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[1] == 64;
    const X86Instruction* record = &source->records[0];
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t operation = 0; shaped && operation < 4; operation += 1)
    {
        for (uint32_t lanes = 0; lanes < 2; lanes += 1)
        {
            const X86Instruction expected = {
                .form = ir_simd_fround_forms[operation][lanes],
                .reg = BUSTER_TIER0_ROLE_VECTOR(0),
                .vvvv = BUSTER_TIER0_ROLE_VECTOR(0),
                .rm = BUSTER_TIER0_ROLE_VECTOR(1),
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .decorations = (uint8_t)(record->decorations & BUSTER_X86_DECORATION_ROUNDING),
            };
            if (memcmp(record, &expected, sizeof(expected)) == 0)
            {
                candidate.fround = true;
                candidate.immediate = (uint8_t)(operation | lanes << 2 | (uint32_t)(record->decorations & BUSTER_X86_DECORATION_ROUNDING) >> 2);
                candidate.required_features = ir_typed_vector_moves(64, target_features) | (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN);
            }
        }
    }
    const IrSimdTypedStatus status = candidate.fround && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = IR_SIMD_V16I32;
        candidate.operand_count = 2;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A truncating conversion of lane 0 (#1840): a row of one 16-byte vector into an integer of 4 or 8 bytes, one record of
// ir_simd_cvtt_forms' (vcvttss2si, vcvttsd2si), INTEGER_RESULT in reg and VECTOR(0) in rm, register-direct and
// undecorated (the builtins' _mm_cvttss_si32 to _mm_cvttsd_si64 at _MM_FROUND_CUR_DIRECTION). Its features its form's
// and the vector's moves. It is the IR's cvtt.
static IrSimdTypedStatus ir_simd_typed_cvtt(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint32_t bytes = tier0_class_integer(source->result_class) ? tier0_class_bytes(source->result_class) : 0;
    const bool shaped = source->operand_count == 1 && source->record_count == 1 && source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[0] == 16 &&
                        (bytes == 4 || bytes == 8);
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t lane = 0; shaped && lane < 2; lane += 1)
    {
        const X86Instruction expected = {
            .form = ir_simd_cvtt_forms[lane][bytes == 8],
            .reg = BUSTER_TIER0_ROLE_INTEGER_RESULT,
            .rm = BUSTER_TIER0_ROLE_VECTOR(0),
            .mode = BUSTER_X86_REGISTER_DIRECT,
        };
        if (memcmp(&source->records[0], &expected, sizeof(expected)) == 0)
        {
            candidate.cvtt = true;
            candidate.immediate = (uint8_t)(lane | (bytes == 8 ? 2u : 0u));
            candidate.required_features = ir_typed_vector_moves(16, target_features) | (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN);
        }
    }
    const IrSimdTypedStatus status = candidate.cvtt && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = IR_SIMD_V4I32;
        candidate.operand_count = 1;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The funnel shifts (#1840): clang's __builtin_elementwise_fshl and fshr of vectors of words, doublewords or quadwords,
// three vectors of the result's width (16, 32 or 64 bytes), as c_check_program_vector_builtins.c writes them: fshl's one
// record of ir_simd_funnel_forms' left form, VECTOR(0) (reg, a, the high half and the destination), VECTOR(1) (vvvv, b,
// the low half) and VECTOR(2) (rm, the counts); fshr's the right form of VECTOR(1) (reg, b, the low half), VECTOR(0)
// (vvvv, a) and VECTOR(2), then VECTOR(1) moved whole into VECTOR(0). And the rotates by an immediate, the builtins'
// __builtin_ia32_prold, prolq, prord and prorq at each width: one vector of the result's width, one record of
// ir_simd_rotate_forms' form of VECTOR(0) (rm) into VECTOR(0) (vvvv, its reg the opcode's extension) by the immediate,
// whose low 8 bits are the instruction's. Each register-direct and undecorated. The IR's funnel of a, b and the counts
// (a rotate's of its vector twice and its count modulo the lanes' bits in each lane, a constant); its features its
// form's (VBMI2 for the funnels, BW for words), AVX512F's, VL's below 64 bytes and the vectors' moves.
static IrSimdTypedStatus ir_simd_typed_funnel(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    bool shaped = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && (source->operand_count == 1 || source->operand_count == 3);
    for (uint32_t j = 0; shaped && j < source->operand_count; j += 1)
    {
        shaped = source->operand_classes[j] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[j] == bytes;
    }
    const X86Instruction* record = &source->records[0];
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    uint16_t form = X86_FORM_NONE;
    for (uint32_t right = 0; shaped && source->operand_count == 3 && right < 2; right += 1)
    {
        for (uint32_t size = 0; size < 3; size += 1)
        {
            const X86Instruction expected = {
                .form = ir_simd_funnel_forms[right][size][width],
                .reg = right != 0 ? v1 : v0,
                .vvvv = right != 0 ? v0 : v1,
                .rm = v2,
                .mode = BUSTER_X86_REGISTER_DIRECT,
            };
            const X86Instruction moved = { .form = bytes == 16 ? X86_FORM_VMOVDQU64_X_XM128 : bytes == 32 ? X86_FORM_VMOVDQU64_Y_YM256 : X86_FORM_VMOVDQU64_Z_ZM512, .reg = v0, .rm = v1,
                                           .mode = BUSTER_X86_REGISTER_DIRECT };
            const bool matched = memcmp(record, &expected, sizeof(expected)) == 0 &&
                                 (right != 0 ? source->record_count == 2 && memcmp(&source->records[1], &moved, sizeof(moved)) == 0 : source->record_count == 1);
            if (matched)
            {
                candidate.funnel = true;
                candidate.immediate = (uint8_t)((size + 1) | right << 2);
                form = expected.form;
            }
        }
    }
    for (uint32_t right = 0; shaped && source->operand_count == 1 && source->record_count == 1 && right < 2; right += 1)
    {
        for (uint32_t size = 0; size < 2; size += 1)
        {
            const X86Instruction expected = {
                .form = ir_simd_rotate_forms[right][size][width],
                .vvvv = v0,
                .rm = v0,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .immediate = record->immediate,
            };
            if (memcmp(record, &expected, sizeof(expected)) == 0 && record->immediate >= -128 && record->immediate <= 255)
            {
                candidate.funnel = true;
                candidate.rotate = true;
                candidate.immediate = (uint8_t)((size + 2) | right << 2);
                candidate.part = (uint8_t)((uint32_t)record->immediate & ((32u << size) - 1));
                form = expected.form;
            }
        }
    }
    candidate.required_features = ir_typed_vector_moves(bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                  (x86_target_requirement(form) & ~BUSTER_X86_TARGET_KNOWN);
    const IrSimdTypedStatus status = candidate.funnel && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK : IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = source->operand_count;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// The quadword operations (#1840): a row of two vectors of the result's width (16, 32 or 64 bytes) and one record of an
// ir_simd_quadword_op_forms form at that width, register-direct and undecorated, of VECTOR(0) (vvvv) and VECTOR(1) (rm)
// into VECTOR(0) (reg): the builtins' __builtin_ia32_vpmultishiftqb128 to 512 (a's bytes the bits' positions, b the
// quadwords) and __builtin_ia32_psadbw128 to 512. It is the IR's quadword_op of its operation; its features are its form's
// (VBMI, BW), AVX512F's, VL's below 64 bytes and the vectors' moves; otherwise it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_quadword_op(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint32_t bytes = source->result_bytes;
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_VECTOR && (bytes == 16 || bytes == 32 || bytes == 64) && source->record_count == 1 &&
                          source->operand_count == 2 && source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[0] == bytes &&
                          source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR && source->operand_bytes[1] == bytes;
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    for (uint32_t operation = 0; admitted && operation < 2; operation += 1)
    {
        const X86Instruction expected = { .form = ir_simd_quadword_op_forms[operation][width], .reg = v0, .vvvv = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT };
        if (memcmp(&source->records[0], &expected, sizeof(expected)) == 0)
        {
            candidate.quadword_op = true;
            candidate.immediate = (uint8_t)operation;
            candidate.required_features = ir_typed_vector_moves(bytes, target_features) | BUSTER_X86_FEATURE(AVX512F) | (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) |
                                          (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN);
        }
    }
    const IrSimdTypedStatus status = candidate.quadword_op && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK :
                                                                                                                                             IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 2;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A bit shuffle into a mask (#1840): the vpshufbitqmb builtins' row as the opmask conversion leaves it
// (c_check_program_x86.c's MASK_BINARY), two vectors of one width (16, 32 or 64 bytes) and a writemask of their bytes'
// lanes, one record of its form at that width from VECTOR(0) (vvvv, the quadwords) and VECTOR(1) (rm, the byte indices)
// into MASK_RESULT, register-direct, MASK(2) its only decoration. It is the IR's bitshuffle ANDed with the writemask,
// left out where the typed stream's writemask has every lane. Its features are its form's (BITALG, BW), AVX512F's, VL's
// below 64 bytes and the mask width's moves and kand (BW for 32 and 64 lanes); otherwise it stays a machine row.
static IrSimdTypedStatus ir_simd_typed_bitshuffle(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    static const uint16_t forms[3] = { X86_FORM_VPSHUFBITQMB_K_X_XM128, X86_FORM_VPSHUFBITQMB_K_Y_YM256, X86_FORM_VPSHUFBITQMB_K_Z_ZM512 };
    const uint32_t bytes = source->operand_bytes[0];
    const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
    const bool admitted = source->result_class == BUSTER_TYPED_CLASS_MASK && source->result_bytes == bytes / 8 && source->record_count == 1 && source->operand_count == 3 &&
                          source->operand_classes[0] == BUSTER_TYPED_CLASS_VECTOR && source->operand_classes[1] == BUSTER_TYPED_CLASS_VECTOR &&
                          source->operand_bytes[1] == bytes && (bytes == 16 || bytes == 32 || bytes == 64) && source->operand_classes[2] == BUSTER_TYPED_CLASS_MASK &&
                          source->operand_bytes[2] == source->result_bytes;
    const X86Instruction expected = {
        .form = forms[width],
        .reg = BUSTER_TIER0_ROLE_MASK_RESULT,
        .vvvv = BUSTER_TIER0_ROLE_VECTOR(0),
        .rm = BUSTER_TIER0_ROLE_VECTOR(1),
        .mode = BUSTER_X86_REGISTER_DIRECT,
        .decorations = BUSTER_TIER0_DECORATION_MASK(2),
    };
    IrSimdTypedOperation candidate = { .operation = IR_SIMD_OPERATION_COUNT };
    candidate.bitshuffle = admitted && memcmp(&source->records[0], &expected, sizeof(expected)) == 0;
    candidate.required_features = (x86_target_requirement(expected.form) & ~BUSTER_X86_TARGET_KNOWN) | BUSTER_X86_FEATURE(AVX512F) |
                                  (width < 2 ? BUSTER_X86_FEATURE(AVX512VL) : 0) | (bytes >= 32 ? BUSTER_X86_FEATURE(AVX512BW) : 0);
    const IrSimdTypedStatus status = candidate.bitshuffle && (target_features & candidate.required_features) == candidate.required_features ? IR_SIMD_TYPED_OK :
                                                                                                                                           IR_SIMD_TYPED_UNSUPPORTED;
    if (status == IR_SIMD_TYPED_OK)
    {
        candidate.lane = IR_SIMD_LANE_I8;
        candidate.shape = (IrSimdShape)(IR_SIMD_V4I32 + width);
        candidate.operand_count = 3;
        memcpy(candidate.operand_classes, source->operand_classes, sizeof(candidate.operand_classes));
        memcpy(candidate.operand_bytes, source->operand_bytes, sizeof(candidate.operand_bytes));
        *result = candidate;
    }
    return status;
}

// A row's decoding: the bit builtins', the signed overflow builtins', masks, scalar intrinsics, lane operations,
// comparisons into a mask alone, masked moves' selects, undefined vectors, width changes, permutes, byte shuffles by an
// immediate, conversions and shifts by a scalar count, lane operations, masks to vectors and back, masked loads and
// stores, one lane's moves, reductions, the lanes' unary operations, floating operations at their own rounding and
// truncating conversions of lane 0, bit shuffles into a mask, quadword operations, funnel shifts (#1840), then exact
// scalar splats; other admitted forms stay machine rows. A semantic
// match whose target lacks a required feature stays refused, but a comparison into a mask alone, a select, an undefined
// vector, a width change, a permute, a byte shuffle by an immediate, a conversion or a shift, whose machine row a target
// may still run, stays one.
static IrSimdTypedStatus ir_simd_typed_row(const Tier0Operation* source, uint64_t target_features, IrSimdTypedOperation* result)
{
    const bool masks = source != NULL && source->operand_count != 0 && source->operand_classes[0] == BUSTER_TYPED_CLASS_MASK && tier0_class_integer(source->result_class);
    const bool scalar = source != NULL && (source->result_class == BUSTER_TYPED_CLASS_U32 || source->result_class == BUSTER_TYPED_CLASS_U64) && !masks;
    IrSimdTypedStatus status = ir_simd_typed_bits(source, target_features, result);
    if (status == IR_SIMD_TYPED_UNSUPPORTED)
    {
        status = ir_simd_typed_overflow(source, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED)
    {
        status = source->result_class == BUSTER_TYPED_CLASS_MASK ? ir_simd_typed_mask_row(source, target_features, result) :
                 masks                                           ? ir_simd_typed_mask_test(source, target_features, result) :
                 scalar                                          ? ir_simd_typed_intrinsic(source, target_features, result) :
                                                                   ir_simd_typed_operation(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_MASK)
    {
        status = ir_simd_typed_compare_mask(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_MASK)
    {
        status = ir_simd_typed_bitshuffle(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && (source->result_class == BUSTER_TYPED_CLASS_MASK || source->result_class == BUSTER_TYPED_CLASS_VECTOR))
    {
        status = ir_simd_typed_mask_vector(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_select(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_zero(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_width(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_permute(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_shuffle_immediate(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_convert(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_shift(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_lane_op(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_permute2(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_ternlog(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_compress(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_aes(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && (source->result_class == BUSTER_TYPED_CLASS_VECTOR || source->result_class == BUSTER_TYPED_CLASS_VOID))
    {
        status = ir_simd_typed_masked_memory(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_gather(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_carrier_cast(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && (source->result_class == BUSTER_TYPED_CLASS_VECTOR || tier0_class_integer(source->result_class)))
    {
        status = ir_simd_typed_lane(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && (source->result_class == BUSTER_TYPED_CLASS_VECTOR || tier0_class_integer(source->result_class)))
    {
        status = ir_simd_typed_reduce(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_lane_unary(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_fround(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_quadword_op(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && source->result_class == BUSTER_TYPED_CLASS_VECTOR)
    {
        status = ir_simd_typed_funnel(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED && tier0_class_integer(source->result_class))
    {
        status = ir_simd_typed_cvtt(source, target_features, result);
    }
    if (status == IR_SIMD_TYPED_UNSUPPORTED)
    {
        status = ir_simd_typed_splat(source, target_features, result);
    }
    // A row the IR's own operations don't take is its machine row (#141); one they take but the target refuses stays
    // refused.
    return status == IR_SIMD_TYPED_UNSUPPORTED ? ir_simd_typed_machine_row(source, target_features, result) : status;
}
