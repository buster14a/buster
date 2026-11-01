// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The intrinsics' x86 builtins as Tier 0's machine operations (#136, #148): the row of an X86 entry (Tier0Operation) for a
// builtin's call, its arguments converted to its prototype's types already (c_check_builtin_x86.c) and its immediates
// constants, as clang -O0's code computes it. A builtin's row is its family's with the rule's form, from a table by
// name; the families are those this compiler's own main.c reaches first. Each form is a VEX or EVEX one that names its
// operands in its fields alone, of AVX-512F and AVX-512VL, which the lowering requires of a function with vectors, and
// of the features and extensions (AVX512-FP16's and XOP's: X86Extension) the rule names past them. A builtin the table
// doesn't name is refused by name. One whose instruction names fixed registers (rdtsc, xgetbv, monitorx and mwaitx) or has a
// legacy encoding (pause, tzcnt), which no row takes, is a call of a per-unit leaf instead (c_builtin_x86_helpers.c).
// Included by c_check_program_lower.c after c_check_program_vectors.c.

typedef enum CheckX86LowerFamily
{
    // (vector a, vector b, int predicate, mask m[, int rounding]): VPCMP's or VPCMPU's opmask of a and b under m, or the
    // form's (a float compare's, VCMPPS's: its predicate by `fixed`, CheckX86LowerPredicate).
    CHECK_X86_LOWER_COMPARE,
    // (mask m, vector w, vector a): w's elements where m's bits are set, a's elsewhere.
    CHECK_X86_LOWER_SELECT,
    // (mask m, vector a, vector w): w with its first element a's where m's first bit is set, and a's others (clang's
    // scalar select: the form, VMOVSS's or VMOVSD's of three registers, merges a's first element into w's under m).
    CHECK_X86_LOWER_SELECT_SCALAR,
    // (mask m): m, through an opmask register.
    CHECK_X86_LOWER_KMOV,
    // (const element* p, vector w, mask m): p's elements where m's bits are set, w's elsewhere; p needs no alignment.
    CHECK_X86_LOWER_LOAD,
    // (element* p, vector a, mask m): a's elements stored where m's bits are set, no others touched.
    CHECK_X86_LOWER_STORE,
    // (vector a, vector b): the form's dest, a, b (reg, vvvv, rm), or with `swap` dest, b, a.
    CHECK_X86_LOWER_BINARY,
    // (vector a, int count): a shifted by the count, moved into an XMM register zero-extended (the form's count operand,
    // whose low 64 bits it reads: a count past the elements' bits gives 0, or the sign, as clang's code does).
    CHECK_X86_LOWER_SHIFT,
    // (vector a, int index): the element at the index, an immediate, in a general register, or a float's in a floating one.
    CHECK_X86_LOWER_EXTRACT,
    // (vector a, int value, int index): a with the element at the index, an immediate, the value.
    CHECK_X86_LOWER_INSERT,
    // (vector a): the elements' top bits, in a general register.
    CHECK_X86_LOWER_MOVEMASK,
    // (integer a, integer b): BMI2's dest, a, b: PDEP's and PEXT's source in vvvv and mask in rm, or with `swap` BZHI's
    // source in rm and index in vvvv.
    CHECK_X86_LOWER_GENERAL,
    // (): an undefined vector, which a row gives as zeros.
    CHECK_X86_LOWER_UNDEFINED,
    // (mask a, mask b): the form's k1, k1, k2 of a in k1 and b in k2 (KAND, KOR, KXOR, KADD, KUNPCK: vvvv's a is the
    // high half), or with `zero_test` whether that is 0 (ktestz's, which the caller tests: a row can't take its flag).
    CHECK_X86_LOWER_KBINARY,
    // (mask a, int count): a shifted by the count, an immediate (KSHIFTL or KSHIFTR, which give 0 past the mask's bits).
    CHECK_X86_LOWER_KSHIFT,
    // (vector a, vector w, mask m): VPCOMPRESS's (or with `swap` VPEXPAND's) of a under m, merged into w.
    CHECK_X86_LOWER_COMPRESS,
    // (element* p, vector a, mask m): a's elements under m, stored one after another at p.
    CHECK_X86_LOWER_COMPRESS_STORE,
    // (vector a): VPMOVB2M's mask of a's elements' top bits.
    CHECK_X86_LOWER_VECTOR_MASK,
    // (mask m): VPMOVM2B's vector of -1 or 0 by m's bits.
    CHECK_X86_LOWER_MASK_VECTOR,
    // (vector a): the form's integer of a's low element, a truncating conversion (VCVTTSD2SI, VCVTTSS2SI).
    CHECK_X86_LOWER_CONVERT,
    // (vector a, T b, int rounding): b, an integer of `other` bytes (signed, or unsigned with `is_signed` false), converted
    // into a's low element by its rounding, a's others kept (VCVTSI2SD, VCVTUSI2SS).
    CHECK_X86_LOWER_INT_CONVERT,
    // (vector a, int index, vector w, mask m): the form's lane of a at the index, an immediate, merged into w under m
    // (VEXTRACTI32X4, VEXTRACTI64X4), of `other` bytes.
    CHECK_X86_LOWER_EXTRACT_LANE,
    // (vector a, vector b, int immediate): the form's dest, a, b, imm (VALIGN, VSHUFI, VINSERTI, VGF2P8AFFINEQB, and a
    // float compare's into a vector, VCMPPS's, its predicate by `fixed`, CheckX86LowerPredicate), b of `other` bytes
    // where it isn't a's.
    CHECK_X86_LOWER_BINARY_IMMEDIATE,
    // (vector a, int immediate): the form's dest, a, imm: with `swap`, the destination in vvvv (VPROLD, VPSLLDQ).
    CHECK_X86_LOWER_UNARY_IMMEDIATE,
    // (vector a): the form's dest, a (VPCONFLICTD).
    CHECK_X86_LOWER_UNARY,
    // (vector a, vector b, vector c[, int immediate][, mask m][, int rounding]): the form's a (the destination too), b,
    // c: VPERMT2's, VPDPBUSD's, and VPTERNLOG's and VFIXUPIMM's with their immediate, under m merged into a, or zeroed
    // (`fixed`, CHECK_X86_INTO_A_ZEROED), VFIXUPIMM's with the exceptions' suppression its rounding control asks for.
    CHECK_X86_LOWER_TERNARY,
    // (vector a, vector w, mask m): VPMOV's truncation of a's elements under m, merged into w, of `other` bytes.
    CHECK_X86_LOWER_TRUNCATE,
    // (vector w, const void* p, vector index, mask m, int scale): VPGATHER's elements at p + index * scale under m,
    // merged into w; the index of `other` bytes.
    CHECK_X86_LOWER_GATHER,
    // (void* p, mask m, vector index, vector a, int scale): VPSCATTER's of a's elements to p + index * scale under m; a
    // of `other` bytes.
    CHECK_X86_LOWER_SCATTER,
    // (vector a, vector b, vector m): VPBLENDVB's b's bytes where m's have their top bits, a's elsewhere.
    CHECK_X86_LOWER_BLENDV,
    // (vector a, vector b, mask m): VPSHUFBITQMB's mask under m; or (vector a, int immediate, mask m): VFPCLASS's of a.
    CHECK_X86_LOWER_MASK_BINARY,
    // (element* p, vector a, mask m): VPMOV's truncation of a's elements, each stored at its place at p where m's bit is
    // set (the form's memory destination); a's lanes are the mask's.
    CHECK_X86_LOWER_TRUNCATE_STORE,
    // (vector a[, int immediate], vector w, mask m): the form's operation of a (rm) into w (reg) under m, of `other` bytes
    // where they aren't a's (a conversion's); `element` is a's bytes per lane of the mask, which are a's bytes over it
    // (VRCP14PS, VGETEXPPS, VCVTPD2DQ, VGETMANTPS).
    CHECK_X86_LOWER_MASK_UNARY,
    // (vector a, vector b[, int immediate], vector w, mask m): the form's operation of a (vvvv) and b (rm) into w (reg)
    // under m, b of `other` bytes where they aren't a's (VSCALEFPS, VRCP14SS, VMAXSD, VCVTSD2SS, VRANGEPS); a scalar
    // one's immediate may follow m (VRANGESS's, VREDUCESS's, VRNDSCALESS's).
    CHECK_X86_LOWER_MASK_MERGE,
    // (vector a): the form's dest, a, a: a scalar operation of a's first element, the others a's (VRCPSS, VRSQRTSS).
    CHECK_X86_LOWER_UNARY_SCALAR,
    // (vector a, vector b): the form's flags of a and b (`swap`: of b and a: VCOMISD, VPTEST), then an int: 1 where the
    // condition `fixed` holds (SETcc's, CheckX86LowerCondition), and where `other` names a second one, its value where
    // that one holds: 0 (CHECK_X86_THEN_ZERO: comieq's unordered operands, by the parity) or 1 (CHECK_X86_THEN_ONE:
    // comineq's).
    CHECK_X86_LOWER_FLAGS,
    // (mask a, mask b): the same of masks of `bytes` bytes, a in k1 and b in k2 (KORTEST, KTEST).
    CHECK_X86_LOWER_KTEST,
    // (mask a): the form's k1, k1 of a in k1 (KNOT), of `bytes` bytes.
    CHECK_X86_LOWER_KUNARY,
    // The general registers' and memory's instructions of integers of `bytes` bytes, by `other`'s layout
    // (CheckX86LowerLayout).
    CHECK_X86_LOWER_SYSTEM,
    // (T* p): RDRAND's or RDSEED's value of `element` bytes stored at p, and whether it is one (CF), an int.
    CHECK_X86_LOWER_RANDOM,
    // (int carry, T a, T b, T* out): a + b + carry by ADC (or a - b - carry by SBB, the form), of `element` bytes, stored at
    // out, and the carry out, an int.
    CHECK_X86_LOWER_CARRY,
    // The vector loads and stores of their own (VMASKMOV's under a vector's signs, VLDDQU, MOVNTSD), by `other`'s layout
    // (CheckX86LowerLayout).
    CHECK_X86_LOWER_VECTOR_MEMORY,
    // (vector a, vector b, vector c, mask m): the form's FMA under m, by `fixed`: into a (213, merged), into a zeroed or
    // into c (231, merged), with a rounding control where the last argument is one.
    CHECK_X86_LOWER_MASK_TERNARY,
    // ([floating start,] vector a): a's elements reduced (`fixed`: add, mul, max or min, CheckX86LowerReduction) by
    // halvings in clang's order, with start the first operand of the last one where the reduction has one: two rows,
    // check_x86_lower_row_of's of a's 16, 32 or 64 bytes to 16 (to 8, of half-precision elements) and
    // check_x86_lower_reduce_row's of those to the element. A half-precision start and result are their bits in a
    // general register (U32), which the caller moves to and from the value's U16.
    CHECK_X86_LOWER_REDUCE,
    // (vector a, vector b, mask* p, mask* q): VP2INTERSECT's pair of masks, of a's elements found in b (k2, the pair's even
    // register) and of b's found in a (k3), stored at p and at q.
    CHECK_X86_LOWER_INTERSECT,
    // (vector a, vector b, int predicate[, int rounding]): the form's compare of a's and b's first elements by the
    // predicate (its imm8's low 5 bits) into k1, whose bit 0 is the int (VCMPSH's, VCMPSS's and VCMPSD's: clang's vcomi
    // intrinsics are an FSETCCM, not COMISH's flags).
    CHECK_X86_LOWER_SCALAR_COMPARE,
    // (vector a, vector b, int predicate): XOP's VPCOM of a and b by the predicate, the immediate's low 3 bits (less,
    // less or equal, greater, greater or equal, equal, not equal, false, true), as clang's code for AVX-512 has it: VPCMP's
    // or VPCMPU's opmask of a and b by the same predicate in its own numbering, then VPMOVM2's vector of -1 and 0 by it.
    CHECK_X86_LOWER_XOP_COMPARE,
} CheckX86LowerFamily;

// FLAGS' conditions, SETcc's (and the x86 conditions' order), and its second condition's value.
typedef enum CheckX86LowerCondition
{
    CHECK_X86_CC_O,
    CHECK_X86_CC_NO,
    CHECK_X86_CC_B,
    CHECK_X86_CC_AE,
    CHECK_X86_CC_E,
    CHECK_X86_CC_NE,
    CHECK_X86_CC_BE,
    CHECK_X86_CC_A,
    CHECK_X86_CC_S,
    CHECK_X86_CC_NS,
    CHECK_X86_CC_P,
    CHECK_X86_CC_NP,
    CHECK_X86_CC_L,
    CHECK_X86_CC_GE,
    CHECK_X86_CC_LE,
    CHECK_X86_CC_G,
} CheckX86LowerCondition;

#define CHECK_X86_THEN_ZERO 0x10u
#define CHECK_X86_THEN_ONE 0x20u

// SYSTEM's and VECTOR_MEMORY's layouts of their operands (a, b) into the form's fields, a pointer's into memory.
typedef enum CheckX86LowerLayout
{
    // SYSTEM's: () the form (EMMS); () -> its register (RDFSBASE, RDPID); (a) its register a (WRFSBASE, INCSSP); (void* p)
    // memory at p (CLWB, FXSAVE); (void* p, a) a stored at p (MOVNTI, MOVDIRI); (a, void* p) the same (WRSS); (void* p,
    // const void* q) p's register, q's memory (MOVDIR64B); (unsigned a, void* p) a zero-extended into a 64-bit register, p's
    // memory (INVPCID); (a) -> a's register's (RDSSP, which keeps it without a shadow stack); (a, b) -> a's, then the
    // form's of it and b (CRC32); () -> the form's flags' condition (the rule's `fixed`), an int (XTEST, TESTUI); (void* p,
    // const void* q) the same of POINTERS' (ENQCMD); (const void* p) -> the integer of the rule's bytes at p, a narrower
    // one's zero-extended (MOVRS); (a) -> the form's register of a's (URDMSR); (a, b) a's register and b's (UWRMSR).
    CHECK_X86_LAYOUT_NONE,
    CHECK_X86_LAYOUT_READ,
    CHECK_X86_LAYOUT_WRITE,
    CHECK_X86_LAYOUT_MEMORY,
    CHECK_X86_LAYOUT_STORE,
    CHECK_X86_LAYOUT_STORE_VALUE_FIRST,
    CHECK_X86_LAYOUT_POINTERS,
    CHECK_X86_LAYOUT_WIDENED,
    CHECK_X86_LAYOUT_MODIFY,
    CHECK_X86_LAYOUT_ACCUMULATE,
    CHECK_X86_LAYOUT_CONDITION,
    CHECK_X86_LAYOUT_POINTERS_CONDITION,
    CHECK_X86_LAYOUT_LOAD_INTEGER,
    CHECK_X86_LAYOUT_FROM,
    CHECK_X86_LAYOUT_REGISTERS,
    // VECTOR_MEMORY's: (const void* p, vector m) -> the elements at p where m's are negative, zeros elsewhere (VMASKMOV's
    // load); (void* p, vector m, vector a) a's elements stored where m's are negative; (const void* p) -> the vector at p
    // (VLDDQU); (void* p, vector a) a's first element stored at p (MOVNTSD); (vector a, vector m, void* p) a's bytes
    // stored at p where m's are negative (MASKMOVDQU's, by VPMOVB2M's mask and VMOVDQU8 under it).
    CHECK_X86_LAYOUT_MASKED_LOAD,
    CHECK_X86_LAYOUT_MASKED_STORE,
    CHECK_X86_LAYOUT_LOAD,
    CHECK_X86_LAYOUT_STORE_VECTOR,
    CHECK_X86_LAYOUT_BYTES_STORE,
} CheckX86LowerLayout;

// MASK_TERNARY's destinations.
typedef enum CheckX86LowerInto
{
    CHECK_X86_INTO_A,
    CHECK_X86_INTO_A_ZEROED,
    CHECK_X86_INTO_C,
} CheckX86LowerInto;

// MASK_MERGE's results, by its rule's `fixed`: the form's into w under the mask (its destination is w's register); the
// same with w zeroed where the mask's bits are clear; that, then a's elements where the mask's bits are clear (the
// 512-bit complex FMAs, which clang selects into a); that, then a's first lane where the mask's first bit is clear and
// a's others (the scalar complex FMAs' round_mask, clang's select by the mask's bit 0); the form into a copy of w (c) in
// VECTOR(3), whose first lane then goes over c's (their round_mask3, clang's shuffle of the result and c); or the form's
// into w under the mask with w and the result of half a's bytes (AVX10.2's conversions of 32 and 64 bytes of halves to
// 8-bit floats with a bias).
typedef enum CheckX86LowerMerge
{
    CHECK_X86_MERGE_W,
    CHECK_X86_MERGE_W_ZEROED,
    CHECK_X86_MERGE_A,
    CHECK_X86_MERGE_A_SCALAR,
    CHECK_X86_MERGE_C_SCALAR,
    CHECK_X86_MERGE_W_HALF,
} CheckX86LowerMerge;

// A float compare's predicate, by its rule's `fixed` (BINARY_IMMEDIATE's and COMPARE's of a form): the immediate's whole
// byte, as clang's code passes a scalar one's; or a packed one's predicate, retained in a function with FENV_ACCESS
// and otherwise the fcmp of the immediate's low 4 bits, as LLVM selects it (check_x86_lower_float_predicate).
typedef enum CheckX86LowerPredicate
{
    CHECK_X86_PREDICATE_BYTE,
    CHECK_X86_PREDICATE_FCMP,
} CheckX86LowerPredicate;

// REDUCE's operations.
typedef enum CheckX86LowerReduction
{
    CHECK_X86_REDUCE_ADD,
    CHECK_X86_REDUCE_MUL,
    CHECK_X86_REDUCE_MAX,
    CHECK_X86_REDUCE_MIN,
} CheckX86LowerReduction;

// A rule's rounding control, the builtin's last argument (an immediate): none, the exceptions' suppression (SAE: 8 or 12,
// _MM_FROUND_NO_EXC, take the rule's round_form) or embedded rounding (ER: 8 to 11, _MM_FROUND_NO_EXC with a direction
// in the low bits, take round_form with that rounding); 4, _MM_FROUND_CUR_DIRECTION, takes the rule's form either way.
// clang refuses other values.
typedef enum CheckX86LowerRounding
{
    CHECK_X86_ROUND_NONE,
    CHECK_X86_ROUND_SAE,
    CHECK_X86_ROUND_ER,
} CheckX86LowerRounding;

#define BUSTER_CHECK_X86_NO_IMMEDIATE 0xffu

typedef struct CheckX86LowerRule CheckX86LowerRule;
struct CheckX86LowerRule
{
    const char* name;
    uint8_t name_length;
    uint8_t family;
    // The elements' bytes (1, 2, 4 or 8) and the vector's (16, 32 or 64), or the integers' (4 or 8) for GENERAL.
    uint8_t element;
    uint8_t bytes;
    bool is_signed;
    bool swap;
    // The immediate argument's index, or BUSTER_CHECK_X86_NO_IMMEDIATE.
    uint8_t immediate;
    // The family's form: but COMPARE's, SELECT's, KMOV's, LOAD's, STORE's and UNDEFINED's, which their elements pick.
    uint16_t form;
    // Another vector's bytes, where the family has one of other bytes than the first (see the families), or 0.
    uint8_t other;
    // KBINARY's: whether the result is that the form's is 0.
    bool zero_test;
    // Its features past AVX-512F and AVX-512VL.
    uint64_t features;
    // The form its rounding control picks (CheckX86LowerRounding), and that control's kind.
    uint16_t round_form;
    uint8_t rounding;
    // BINARY's immediate where its form has one and the builtin none: the SSE compares' predicates; and the other
    // families' variant where they have one (MASK_MERGE's CheckX86LowerMerge, TERNARY's CheckX86LowerInto, a float
    // compare's CheckX86LowerPredicate, FLAGS' condition).
    uint8_t fixed;
    // Its extensions (X86Target.extensions' bits: AVX512-FP16's), which the target must have too.
    uint32_t extensions;
};

_Static_assert(X86_EXTENSION_COUNT <= 32, "a rule's extensions are a 32-bit word's bits");

#define BUSTER_CHECK_X86_BW BUSTER_X86_FEATURE(AVX512BW)
#define BUSTER_CBUILTIN_X86_ROW(...) { __VA_ARGS__ },
static const CheckX86LowerRule check_x86_lower_rules[] = {
#include "c_builtin_x86_rows.inc"
};
#undef BUSTER_CBUILTIN_X86_ROW

static const CheckX86LowerRule* check_x86_lower_rule(const char* name, uint32_t length)
{
    const CheckX86LowerRule* rule = NULL;
    for (uint32_t i = 0; rule == NULL && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        if (check_x86_lower_rules[i].name_length == length && memcmp(check_x86_lower_rules[i].name, name, length) == 0)
        {
            rule = check_x86_lower_rules + i;
        }
    }
    return rule;
}

// A mask of `bits` bits: its class, and the opmask moves to and from a general register (KMOVW for 8 and 16 bits, which
// moves 16, the value's high bits zero either way; KMOVD and KMOVQ, AVX-512BW's, for 32 and 64).
static uint8_t check_x86_lower_mask_class(uint32_t bits)
{
    return bits <= 8 ? BUSTER_TYPED_CLASS_U8 : bits == 16 ? BUSTER_TYPED_CLASS_U16 : bits == 32 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
}

static uint16_t check_x86_lower_mask_in(uint32_t bits)
{
    return bits <= 16 ? X86_FORM_KMOVW_K_R32 : bits == 32 ? X86_FORM_KMOVD_K_R32 : X86_FORM_KMOVQ_K_R64;
}

static uint16_t check_x86_lower_mask_out(uint32_t bits)
{
    return bits <= 16 ? X86_FORM_KMOVW_R32_K : bits == 32 ? X86_FORM_KMOVD_R32_K : X86_FORM_KMOVQ_R64_K;
}

// The masked moves of an element's size at each width: VMOVDQU8, 16, 32 and 64, loads (to a register) or stores.
static uint16_t check_x86_lower_move(uint32_t element, uint32_t width, bool store)
{
    static const uint16_t loads[4][3] = {
        { X86_FORM_VMOVDQU8_X_XM128, X86_FORM_VMOVDQU8_Y_YM256, X86_FORM_VMOVDQU8_Z_ZM512 },
        { X86_FORM_VMOVDQU16_X_XM128, X86_FORM_VMOVDQU16_Y_YM256, X86_FORM_VMOVDQU16_Z_ZM512 },
        { X86_FORM_VMOVDQU32_X_XM128, X86_FORM_VMOVDQU32_Y_YM256, X86_FORM_VMOVDQU32_Z_ZM512 },
        { X86_FORM_VMOVDQU64_X_XM128, X86_FORM_VMOVDQU64_Y_YM256, X86_FORM_VMOVDQU64_Z_ZM512 },
    };
    static const uint16_t stores[4][3] = {
        { X86_FORM_VMOVDQU8_XM128_X, X86_FORM_VMOVDQU8_YM256_Y, X86_FORM_VMOVDQU8_ZM512_Z },
        { X86_FORM_VMOVDQU16_XM128_X, X86_FORM_VMOVDQU16_YM256_Y, X86_FORM_VMOVDQU16_ZM512_Z },
        { X86_FORM_VMOVDQU32_XM128_X, X86_FORM_VMOVDQU32_YM256_Y, X86_FORM_VMOVDQU32_ZM512_Z },
        { X86_FORM_VMOVDQU64_XM128_X, X86_FORM_VMOVDQU64_YM256_Y, X86_FORM_VMOVDQU64_ZM512_Z },
    };
    const uint32_t size = element == 1 ? 0 : element == 2 ? 1 : element == 4 ? 2 : 3;
    return store ? stores[size][width] : loads[size][width];
}

// VPCMP's and VPCMPU's forms of an element's size (0 to 3: bytes to quadwords) at each width (16, 32 and 64 bytes).
static uint16_t check_x86_lower_compare_form(bool is_signed, uint32_t size, uint32_t width)
{
    static const uint16_t signed_forms[4][3] = {
        { X86_FORM_VPCMPB_K_X_XM128_IMM8, X86_FORM_VPCMPB_K_Y_YM256_IMM8, X86_FORM_VPCMPB_K_Z_ZM512_IMM8 },
        { X86_FORM_VPCMPW_K_X_XM128_IMM8, X86_FORM_VPCMPW_K_Y_YM256_IMM8, X86_FORM_VPCMPW_K_Z_ZM512_IMM8 },
        { X86_FORM_VPCMPD_K_X_XM128_IMM8, X86_FORM_VPCMPD_K_Y_YM256_IMM8, X86_FORM_VPCMPD_K_Z_ZM512_IMM8 },
        { X86_FORM_VPCMPQ_K_X_XM128_IMM8, X86_FORM_VPCMPQ_K_Y_YM256_IMM8, X86_FORM_VPCMPQ_K_Z_ZM512_IMM8 },
    };
    static const uint16_t unsigned_forms[4][3] = {
        { X86_FORM_VPCMPUB_K_X_XM128_IMM8, X86_FORM_VPCMPUB_K_Y_YM256_IMM8, X86_FORM_VPCMPUB_K_Z_ZM512_IMM8 },
        { X86_FORM_VPCMPUW_K_X_XM128_IMM8, X86_FORM_VPCMPUW_K_Y_YM256_IMM8, X86_FORM_VPCMPUW_K_Z_ZM512_IMM8 },
        { X86_FORM_VPCMPUD_K_X_XM128_IMM8, X86_FORM_VPCMPUD_K_Y_YM256_IMM8, X86_FORM_VPCMPUD_K_Z_ZM512_IMM8 },
        { X86_FORM_VPCMPUQ_K_X_XM128_IMM8, X86_FORM_VPCMPUQ_K_Y_YM256_IMM8, X86_FORM_VPCMPUQ_K_Z_ZM512_IMM8 },
    };
    return is_signed ? signed_forms[size][width] : unsigned_forms[size][width];
}

static X86Instruction check_x86_lower_record(uint32_t form, uint8_t reg, uint8_t vvvv, uint8_t rm)
{
    return (X86Instruction){ .form = (uint16_t)form, .reg = reg, .vvvv = vvvv, .rm = rm, .mode = BUSTER_X86_REGISTER_DIRECT };
}

// The vector moves of a width (16, 32 or 64 bytes, or 16's for a vector of 4 or 8) into VECTOR(0): VMOVDQU64's, which a
// row ends with where its instruction leaves its result in another register.
static X86Instruction check_x86_lower_move_result(uint32_t bytes, uint8_t from)
{
    const uint32_t width = bytes <= 16 ? 0 : bytes == 32 ? 1 : 2;
    return check_x86_lower_record(check_x86_lower_move(8, width, false), BUSTER_TIER0_ROLE_VECTOR(0), 0, from);
}

// Whether a family moves a mask between a general register and an opmask register, which for 32 lanes and more takes
// AVX-512BW's KMOVD or KMOVQ.
static bool check_x86_lower_family_masks(uint8_t family)
{
    bool result = false;
    switch (family)
    {
    case CHECK_X86_LOWER_COMPARE:
    case CHECK_X86_LOWER_SELECT:
    case CHECK_X86_LOWER_SELECT_SCALAR:
    case CHECK_X86_LOWER_KMOV:
    case CHECK_X86_LOWER_LOAD:
    case CHECK_X86_LOWER_STORE:
    case CHECK_X86_LOWER_KBINARY:
    case CHECK_X86_LOWER_KSHIFT:
    case CHECK_X86_LOWER_COMPRESS:
    case CHECK_X86_LOWER_COMPRESS_STORE:
    case CHECK_X86_LOWER_VECTOR_MASK:
    case CHECK_X86_LOWER_MASK_VECTOR:
    case CHECK_X86_LOWER_EXTRACT_LANE:
    case CHECK_X86_LOWER_TERNARY:
    case CHECK_X86_LOWER_TRUNCATE:
    case CHECK_X86_LOWER_GATHER:
    case CHECK_X86_LOWER_SCATTER:
    case CHECK_X86_LOWER_MASK_BINARY:
    case CHECK_X86_LOWER_TRUNCATE_STORE:
    case CHECK_X86_LOWER_MASK_UNARY:
    case CHECK_X86_LOWER_MASK_MERGE:
    case CHECK_X86_LOWER_KTEST:
    case CHECK_X86_LOWER_KUNARY:
    case CHECK_X86_LOWER_MASK_TERNARY: result = true; break;
    default: break;
    }
    return result;
}

// A packed float compare's predicate (CHECK_X86_PREDICATE_FCMP): in a constrained function keep all 5 bits and the
// operands' order, including the signaling bit. Otherwise use clang's unconstrained fcmp of the low 4 bits, as LLVM
// selects it (translateX86FSETCC), with *swap for a greater-than's operands. FALSE and TRUE use their instruction's
// own predicates. The caller supplies clang's function-wide FENV_ACCESS decision, not just the call token's state.
static uint32_t check_x86_lower_float_predicate(uint64_t immediate, bool strict, bool* swap)
{
    static const uint8_t predicates[16] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x06, 0x05, 0x0b, 0x0c, 0x02, 0x01, 0x0f };
    const uint32_t index = (uint32_t)(immediate & 15);
    *swap = !strict && (index == 0x09 || index == 0x0a || index == 0x0d || index == 0x0e);
    return strict ? (uint32_t)(immediate & 31) : predicates[index];
}

// A record of a rule with a rounding control at that control's value, `round` (CheckX86LowerRounding): its form, or the
// rule's round_form with the direction in its rounding bits (BUSTER_X86_DECORATION_ROUNDING's, the low bits' order:
// nearest, down, up, zero).
static void check_x86_lower_round(const CheckX86LowerRule* rule, uint64_t round, X86Instruction* record)
{
    if (rule->rounding != CHECK_X86_ROUND_NONE && (round & 8) != 0)
    {
        record->form = rule->round_form;
        if (rule->rounding == CHECK_X86_ROUND_ER)
        {
            record->decorations = (uint8_t)(record->decorations | (round & 3) << 5);
        }
    }
}

// FLAGS', KTEST's, RANDOM's and CARRY's tail from record `r`, after the records whose flags it reads, as clang's code has
// it: SETcc of `condition` (CheckX86LowerCondition) into a scratch register's byte, then its zero extension into the
// integer result. Where `then` names a second condition, SETcc of it (CHECK_X86_THEN_ONE's) or of its negation
// (CHECK_X86_THEN_ZERO's) into another scratch byte comes first, and the first condition's byte is ORed (or ANDed) with
// it: comineq's setp, setne and or, comieq's setnp, sete and and. The count of records after it.
static uint32_t check_x86_lower_condition(Tier0Operation* row, uint32_t r, uint32_t condition, uint32_t then)
{
    static const uint16_t sets[16] = {
        X86_FORM_SETO_RM8, X86_FORM_SETNO_RM8, X86_FORM_SETB_RM8, X86_FORM_SETAE_RM8, X86_FORM_SETE_RM8, X86_FORM_SETNE_RM8,
        X86_FORM_SETBE_RM8, X86_FORM_SETA_RM8, X86_FORM_SETS_RM8, X86_FORM_SETNS_RM8, X86_FORM_SETP_RM8, X86_FORM_SETNP_RM8,
        X86_FORM_SETL_RM8, X86_FORM_SETGE_RM8, X86_FORM_SETLE_RM8, X86_FORM_SETG_RM8,
    };
    const bool zero = (then & CHECK_X86_THEN_ZERO) != 0;
    const uint8_t value = BUSTER_TIER0_ROLE_SCRATCH(then != 0 ? 1 : 0);
    if (then != 0)
    {
        row->records[r++] = check_x86_lower_record(sets[(then & 15) ^ (zero ? 1 : 0)], 0, 0, BUSTER_TIER0_ROLE_SCRATCH(0));
    }
    row->records[r++] = check_x86_lower_record(sets[condition & 15], 0, 0, value);
    if (then != 0)
    {
        row->records[r++] = check_x86_lower_record(zero ? X86_FORM_AND_RM8_R8 : X86_FORM_OR_RM8_R8, BUSTER_TIER0_ROLE_SCRATCH(0), 0, value);
    }
    row->records[r++] = check_x86_lower_record(X86_FORM_MOVZX_R32_RM8, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, value);
    return r;
}

// A record of a form whose operand is memory at the address in `base`'s register, the register operand (where it has one)
// in reg.
static X86Instruction check_x86_lower_memory(uint32_t form, uint8_t reg, uint8_t base)
{
    return (X86Instruction){ .form = (uint16_t)form, .reg = reg, .rm = base, .mode = BUSTER_X86_MEMORY_BASE };
}

// REDUCE's forms by its operation: the packed ones of 32 and 16 bytes and the scalar one, of floats' and doubles'.
static uint16_t check_x86_lower_reduction_form(uint32_t reduction, uint32_t element, uint32_t bytes)
{
    static const uint16_t forms[4][3][3] = {
        { { X86_FORM_VADDPS_Y_Y_YM256_EVEX, X86_FORM_VADDPS_X_X_XM128_EVEX, X86_FORM_VADDSS_X_X_XM32_EVEX },
          { X86_FORM_VADDPD_Y_Y_YM256_EVEX, X86_FORM_VADDPD_X_X_XM128_EVEX, X86_FORM_VADDSD_X_X_XM64_EVEX },
          { X86_FORM_VADDPH_Y_Y_YM256, X86_FORM_VADDPH_X_X_XM128, X86_FORM_VADDSH_X_X_XM16 } },
        { { X86_FORM_VMULPS_Y_Y_YM256_EVEX, X86_FORM_VMULPS_X_X_XM128_EVEX, X86_FORM_VMULSS_X_X_XM32_EVEX },
          { X86_FORM_VMULPD_Y_Y_YM256_EVEX, X86_FORM_VMULPD_X_X_XM128_EVEX, X86_FORM_VMULSD_X_X_XM64_EVEX },
          { X86_FORM_VMULPH_Y_Y_YM256, X86_FORM_VMULPH_X_X_XM128, X86_FORM_VMULSH_X_X_XM16 } },
        { { X86_FORM_VMAXPS_Y_Y_YM256_EVEX, X86_FORM_VMAXPS_X_X_XM128_EVEX, X86_FORM_VMAXSS_X_X_XM32_EVEX },
          { X86_FORM_VMAXPD_Y_Y_YM256_EVEX, X86_FORM_VMAXPD_X_X_XM128_EVEX, X86_FORM_VMAXSD_X_X_XM64_EVEX },
          { X86_FORM_VMAXPH_Y_Y_YM256, X86_FORM_VMAXPH_X_X_XM128, X86_FORM_VMAXSH_X_X_XM16 } },
        { { X86_FORM_VMINPS_Y_Y_YM256_EVEX, X86_FORM_VMINPS_X_X_XM128_EVEX, X86_FORM_VMINSS_X_X_XM32_EVEX },
          { X86_FORM_VMINPD_Y_Y_YM256_EVEX, X86_FORM_VMINPD_X_X_XM128_EVEX, X86_FORM_VMINSD_X_X_XM64_EVEX },
          { X86_FORM_VMINPH_Y_Y_YM256, X86_FORM_VMINPH_X_X_XM128, X86_FORM_VMINSH_X_X_XM16 } },
    };
    return forms[reduction & 3][element == 8 ? 1 : element == 2 ? 2 : 0][bytes == 32 ? 0 : bytes == 16 ? 1 : 2];
}

// REDUCE's second row, of the first's 16 bytes ([floating start,] vector x) to the element, as clang orders it: x and its
// halves swapped (VPERMILPD), of floats those and their odd elements (VMOVSHDUP), each operation's first operand the one
// before's result, and start the last's where the reduction has one (an add's or a mul's). The result is the floating
// result's register. Of half-precision elements, whose first row has swapped the halves, the odd 32-bit lanes
// (VMOVSHDUP), then the odd halves (VPSRLD by 16), the start's and the result's bits moved to and from a general
// register (VMOVD) for the caller's U16: max and min by VMAXSH and VMINSH, which give what clang's compare and VMOVSH
// give, as the packed ones do the second operand where either is a NaN or both are zeros.
static void check_x86_lower_reduce_row(const CheckX86LowerRule* rule, Tier0Operation* row)
{
    memset(row, 0, sizeof(*row));
    const bool start = rule->fixed == CHECK_X86_REDUCE_ADD || rule->fixed == CHECK_X86_REDUCE_MUL;
    const bool half = rule->element == 2;
    const uint8_t x = BUSTER_TIER0_ROLE_VECTOR(start ? 1 : 0);
    const uint8_t t = BUSTER_TIER0_ROLE_VECTOR(start ? 2 : 1);
    const uint16_t scalar = check_x86_lower_reduction_form(rule->fixed, rule->element, 4);
    uint32_t r = 0;
    if (half)
    {
        row->records[r++] = check_x86_lower_record(X86_FORM_VMOVSHDUP_X_XM128_EVEX, t, 0, x);
        row->records[r++] = check_x86_lower_record(check_x86_lower_reduction_form(rule->fixed, 2, 16), x, x, t);
        row->records[r] = check_x86_lower_record(X86_FORM_VPSRLD_X_XM128_IMM8, 0, t, x);
        row->records[r++].immediate = 16;
        row->records[r++] = check_x86_lower_record(scalar, x, x, t);
        if (start)
        {
            row->records[r++] = check_x86_lower_record(X86_FORM_VMOVD_X_RM32_EVEX, t, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[r++] = check_x86_lower_record(scalar, x, t, x);
        }
        row->records[r++] = check_x86_lower_record(X86_FORM_VMOVD_RM32_X_EVEX, x, 0, BUSTER_TIER0_ROLE_INTEGER_RESULT);
    }
    else
    {
        row->records[r] = check_x86_lower_record(X86_FORM_VPERMILPD_X_XM128_IMM8_EVEX, t, 0, x);
        row->records[r++].immediate = 1;
        if (rule->element == 4)
        {
            row->records[r++] = check_x86_lower_record(check_x86_lower_reduction_form(rule->fixed, 4, 16), x, x, t);
            row->records[r++] = check_x86_lower_record(X86_FORM_VMOVSHDUP_X_XM128_EVEX, t, 0, x);
        }
        if (start)
        {
            row->records[r++] = check_x86_lower_record(scalar, x, x, t);
            row->records[r++] = check_x86_lower_record(scalar, BUSTER_TIER0_ROLE_FLOATING_RESULT, BUSTER_TIER0_ROLE_FLOATING(0), x);
        }
        else
        {
            row->records[r++] = check_x86_lower_record(scalar, BUSTER_TIER0_ROLE_FLOATING_RESULT, x, t);
        }
    }
    row->record_count = (uint8_t)r;
    const uint8_t scalar_class = half ? BUSTER_TYPED_CLASS_U32 : rule->element == 8 ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
    row->operand_count = start ? 2 : 1;
    row->operand_classes[0] = start ? scalar_class : BUSTER_TYPED_CLASS_VECTOR;
    row->operand_bytes[0] = start ? 0 : 16;
    row->operand_classes[1] = start ? BUSTER_TYPED_CLASS_VECTOR : 0;
    row->operand_bytes[1] = start ? 16 : 0;
    row->result_class = scalar_class;
}

// The row of an x86 builtin's call on a target: its records, operands and result, or the reason it has none.
// `immediate` is its immediate argument's constant value (the rule's `immediate`'s), which a record takes and no operand
// is, and `round` its rounding control's (its last argument's, where the rule has one). `mask` is a MASK_UNARY's or
// MASK_MERGE's mask argument's value where it is a constant, 0 elsewhere: one with every lane's bit is the form unmasked,
// as clang's code has it (the headers' intrinsics without a mask pass -1), where the row merges into w. Operands are the
// other arguments
// in order, and a role's j is its operand's place among them all, so that a compare's mask is INTEGER(2) and a select's
// vectors VECTOR(1) and VECTOR(2). An integer operand's class here is its width's; the caller gives each its argument's
// own (check_lower_x86_builtin). A result is in the class the instruction extends it to, which the caller converts to
// the builtin's type. `strict` retains a packed floating compare's signaling and SAE in a function with FENV_ACCESS.
// The target's features and extensions must hold the rule's.
static const char* check_x86_lower_row_of(const X86Target* target, const CheckX86LowerRule* rule, uint64_t immediate, uint64_t round, uint64_t mask,
                                          bool strict, Tier0Operation* row)
{
    const char* reason = NULL;
    uint64_t needed = 0;
    memset(row, 0, sizeof(*row));
    if (rule == NULL)
    {
        reason = "this x86 builtin";
    }
    else
    {
        const uint32_t width = rule->bytes == 16 ? 0 : rule->bytes == 32 ? 1 : 2;
        const bool mask_bits = rule->family == CHECK_X86_LOWER_KMOV || rule->family == CHECK_X86_LOWER_KBINARY || rule->family == CHECK_X86_LOWER_KSHIFT ||
                               rule->family == CHECK_X86_LOWER_KTEST || rule->family == CHECK_X86_LOWER_KUNARY;
        const uint32_t lanes = mask_bits ? 8u * rule->bytes : rule->family == CHECK_X86_LOWER_EXTRACT_LANE ? (uint32_t)rule->other / rule->element :
                                                                                                          (uint32_t)rule->bytes / rule->element;
        const uint8_t mask_class = check_x86_lower_mask_class(lanes);
        const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
        const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
        const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
        const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
        const bool masked = rule->family <= CHECK_X86_LOWER_STORE;
        // Integer-only opmask rows never reach the VECTOR value-class gate. Their KMOV and mask instructions still
        // require AVX-512F; keep GENERAL's BMI2-only target independent of that requirement.
        needed = rule->features | (mask_bits ? BUSTER_X86_FEATURE(AVX512F) : 0) |
                 (masked && rule->element <= 2 && rule->family != CHECK_X86_LOWER_KMOV ? BUSTER_CHECK_X86_BW : 0) |
                 (check_x86_lower_family_masks(rule->family) && lanes >= 32 ? BUSTER_CHECK_X86_BW : 0);
        row->result_class = BUSTER_TYPED_CLASS_VECTOR;
        row->result_bytes = rule->bytes;
        if (rule->family == CHECK_X86_LOWER_COMPARE)
        {
            const uint32_t size = rule->element == 1 ? 0 : rule->element == 2 ? 1 : rule->element == 4 ? 2 : 3;
            // The predicate is the immediate's low 3 bits, as the instruction reads them; a float compare's (a rule with a
            // form: VCMPPS's) clang's code's (CheckX86LowerPredicate), with the operands swapped where that says, and the
            // exceptions' suppression its rounding control asks for, where the rule takes it.
            const bool floating = rule->form != X86_FORM_NONE;
            bool swap = false;
            const uint64_t predicate = !floating                                ? immediate & 7 :
                                       rule->fixed == CHECK_X86_PREDICATE_FCMP ? check_x86_lower_float_predicate(immediate, strict, &swap) :
                                                                                 immediate & 0xff;
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 2, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = check_x86_lower_record(floating ? rule->form : check_x86_lower_compare_form(rule->is_signed, size, width), 1, swap ? v1 : v0,
                                                     swap ? v0 : v1);
            row->records[1].immediate = (int64_t)predicate;
            row->records[1].decorations = 2;
            if (!floating || rule->fixed != CHECK_X86_PREDICATE_FCMP || strict)
            {
                check_x86_lower_round(rule, round, &row->records[1]);
            }
            row->records[2] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 1);
            row->record_count = 3;
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = mask_class;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_SELECT)
        {
            // A's elements, then W's merged in where the mask's bits are set.
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(check_x86_lower_move(8, width, false), v0, 0, v2);
            row->records[2] = check_x86_lower_record(check_x86_lower_move(rule->element, width, false), v0, 0, v1);
            row->records[2].decorations = 1;
            row->record_count = 3;
            row->operand_count = 3;
            row->operand_classes[0] = mask_class;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[2] = rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_SELECT_SCALAR && rule->element == 2 && rule->form == X86_FORM_NONE)
        {
            // bf16's, which has no scalar move (half-precision's has VMOVSH, the rule's form): A's elements, then W's first
            // merged in where the mask's first bit is clear, in k2, by KANDN of it and 1.
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(X86_FORM_MOV_R32_IMM32, 0, 0, BUSTER_TIER0_ROLE_SCRATCH(0));
            row->records[1].immediate = 1;
            row->records[2] = check_x86_lower_record(X86_FORM_KMOVW_K_R32, 2, 0, BUSTER_TIER0_ROLE_SCRATCH(0));
            row->records[3] = check_x86_lower_record(X86_FORM_KANDNW_K_K_K, 2, 1, 2);
            row->records[4] = check_x86_lower_record(check_x86_lower_move(8, width, false), v0, 0, v1);
            row->records[5] = check_x86_lower_record(check_x86_lower_move(2, width, false), v0, 0, v2);
            row->records[5].decorations = 2;
            row->record_count = 6;
            row->operand_count = 3;
            row->operand_classes[0] = mask_class;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[2] = rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_SELECT_SCALAR)
        {
            // W's elements, then A's first merged in where the mask's first bit is set and A's others over W's.
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(check_x86_lower_move(8, width, false), v0, 0, v2);
            row->records[2] = check_x86_lower_record(rule->form, v0, v1, v1);
            row->records[2].decorations = 1;
            row->record_count = 3;
            row->operand_count = 3;
            row->operand_classes[0] = mask_class;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[2] = rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_KMOV)
        {
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 1);
            row->record_count = 2;
            row->operand_count = 1;
            row->operand_classes[0] = mask_class;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_LOAD || rule->family == CHECK_X86_LOWER_STORE)
        {
            // The load merges into W's register and leaves it in VECTOR(0); the store writes A's elements alone. The move
            // is the rule's form where it names one (the aligned and floating moves, and the expanding loads), else the
            // element's VMOVDQU.
            const bool store = rule->family == CHECK_X86_LOWER_STORE;
            const uint16_t move = rule->form != X86_FORM_NONE ? rule->form : check_x86_lower_move(rule->element, width, store);
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = (X86Instruction){ .form = move, .reg = v1, .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE, .decorations = 1 };
            row->record_count = 2;
            if (!store)
            {
                row->records[2] = check_x86_lower_record(check_x86_lower_move(8, width, false), v0, 0, v1);
                row->record_count = 3;
            }
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = mask_class;
            row->result_class = store ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_VECTOR;
            row->result_bytes = store ? 0 : rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_BINARY)
        {
            // b is of `other` bytes where they aren't a's (a shift's count, an XMM register at every width). A form
            // without vvvv (SSE4A's, SHA's, SHA512's) has its reg a and the destination.
            const bool two = x86_form_description(rule->form)->vvvv == BUSTER_X86_OPERAND_NONE;
            row->records[0] = check_x86_lower_record(rule->form, v0, two ? 0 : rule->swap ? v1 : v0, rule->swap ? v0 : v1);
            row->records[0].immediate = rule->fixed;
            check_x86_lower_round(rule, round, &row->records[0]);
            row->record_count = 1;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->other != 0 ? rule->other : rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_SHIFT)
        {
            row->records[0] = check_x86_lower_record(X86_FORM_VMOVD_X_RM32_EVEX, v1, 0, BUSTER_TIER0_ROLE_INTEGER(1));
            row->records[1] = check_x86_lower_record(rule->form, v0, v0, v1);
            row->record_count = 2;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_I32;
        }
        else if (rule->family == CHECK_X86_LOWER_EXTRACT)
        {
            // The element at the index's low bits, zero-extended into the general register (but VPEXTRQ's, whole); of a
            // 256-bit vector, from its high lane extracted first where the index is in it. A float's (VPERMILPS, the rule's
            // form) is the floating result's first element instead, its bits a float's: the caller converts an integer
            // result by its value.
            const uint64_t count = (uint64_t)rule->bytes / rule->element;
            const uint64_t lane = 16u / rule->element;
            const uint64_t index = immediate & (count - 1);
            const bool floating = rule->form == X86_FORM_VPERMILPS_X_XM128_IMM8_EVEX;
            uint32_t r = 0;
            if (index >= lane)
            {
                row->records[r] = check_x86_lower_record(X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8, v0, 0, v1);
                row->records[r].immediate = 1;
                r += 1;
            }
            // VPEXTRW's form takes the general register in reg and the vector in rm; the others' the other way round.
            const uint8_t vector = index >= lane ? v1 : v0;
            row->records[r] = floating ? check_x86_lower_record(rule->form, BUSTER_TIER0_ROLE_FLOATING_RESULT, 0, vector) :
                              rule->form == X86_FORM_VPEXTRW_R32_X_IMM8_EVEX ? check_x86_lower_record(rule->form, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, vector) :
                                                                                 check_x86_lower_record(rule->form, vector, 0, BUSTER_TIER0_ROLE_INTEGER_RESULT);
            row->records[r].immediate = (int32_t)(index & (lane - 1));
            row->record_count = (uint8_t)(r + 1);
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_class = floating ? BUSTER_TYPED_CLASS_F32 : rule->element == 1 ? BUSTER_TYPED_CLASS_U8 : rule->element == 2 ? BUSTER_TYPED_CLASS_U16 :
                                rule->element == 4 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_I64;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_INSERT)
        {
            // The element at the index's low bits, in place, of a vector of 8 or 16 bytes; of a 256-bit vector, into a copy of
            // the lane that holds it in VECTOR(1) (its high lane extracted first, or its low one read where it is), which then
            // goes back into its lane (VINSERTI32X4): an insertion of 16 bytes zeroes its register's bytes past them.
            const uint64_t count = (uint64_t)rule->bytes / rule->element;
            const uint64_t lane = 16u / rule->element;
            const uint64_t index = immediate & (count - 1);
            const bool wide = rule->bytes == 32;
            const bool high = index >= lane;
            uint32_t r = 0;
            if (wide && high)
            {
                row->records[r] = check_x86_lower_record(X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8, v0, 0, v1);
                row->records[r].immediate = 1;
                r += 1;
            }
            row->records[r] = check_x86_lower_record(rule->form, wide ? v1 : v0, high ? v1 : v0, BUSTER_TIER0_ROLE_INTEGER(1));
            row->records[r].immediate = (int64_t)(index & (lane - 1));
            r += 1;
            if (wide)
            {
                row->records[r] = check_x86_lower_record(X86_FORM_VINSERTI32X4_Y_Y_XM128_IMM8, v0, v0, v1);
                row->records[r].immediate = high ? 1 : 0;
                r += 1;
            }
            row->record_count = (uint8_t)r;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = rule->element == 8 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32;
        }
        else if (rule->family == CHECK_X86_LOWER_MOVEMASK)
        {
            row->records[0] = check_x86_lower_record(rule->form, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, v0);
            row->record_count = 1;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_class = BUSTER_TYPED_CLASS_I32;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_GENERAL)
        {
            const uint8_t integer = rule->bytes == 8 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
            row->records[0] = check_x86_lower_record(rule->form, BUSTER_TIER0_ROLE_INTEGER_RESULT, BUSTER_TIER0_ROLE_INTEGER(rule->swap ? 1 : 0),
                                                     BUSTER_TIER0_ROLE_INTEGER(rule->swap ? 0 : 1));
            row->record_count = 1;
            row->operand_count = 2;
            row->operand_classes[0] = integer;
            row->operand_classes[1] = integer;
            row->result_class = integer;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_UNDEFINED)
        {
            row->records[0] = check_x86_lower_record(BUSTER_CHECK_VECTOR_PACKED(VPXORQ).forms[width], v0, v0, v0);
            row->record_count = 1;
        }
        else if (rule->family == CHECK_X86_LOWER_KBINARY || rule->family == CHECK_X86_LOWER_KSHIFT)
        {
            // a in k1 (and b in k2), the form into k1, and k1 back; a shift's count its immediate's low byte.
            const bool shift = rule->family == CHECK_X86_LOWER_KSHIFT;
            uint32_t r = 0;
            row->records[r++] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            if (shift)
            {
                row->records[r] = check_x86_lower_record(rule->form, 1, 0, 1);
                row->records[r++].immediate = (int32_t)(immediate & 0xff);
            }
            else
            {
                row->records[r++] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 2, 0, BUSTER_TIER0_ROLE_INTEGER(1));
                row->records[r++] = check_x86_lower_record(rule->form, 1, 1, 2);
            }
            row->records[r++] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 1);
            row->record_count = (uint8_t)r;
            row->operand_count = shift ? 1 : 2;
            row->operand_classes[0] = mask_class;
            row->operand_classes[1] = mask_class;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_COMPRESS)
        {
            // w in VECTOR(1) takes a's elements under the mask, compressed (the form's rm is its destination) or expanded
            // (its reg is), then goes to VECTOR(0).
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = rule->swap ? check_x86_lower_record(rule->form, v1, 0, v0) : check_x86_lower_record(rule->form, v0, 0, v1);
            row->records[1].decorations = 1;
            row->records[2] = check_x86_lower_move_result(rule->bytes, v1);
            row->record_count = 3;
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = mask_class;
        }
        else if (rule->family == CHECK_X86_LOWER_COMPRESS_STORE || rule->family == CHECK_X86_LOWER_TRUNCATE_STORE)
        {
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = (X86Instruction){ .form = rule->form, .reg = v1, .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE, .decorations = 1 };
            row->record_count = 2;
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = mask_class;
            row->result_class = BUSTER_TYPED_CLASS_VOID;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_VECTOR_MASK)
        {
            row->records[0] = check_x86_lower_record(rule->form, 1, 0, v0);
            row->records[1] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 1);
            row->record_count = 2;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_MASK_VECTOR)
        {
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(rule->form, v0, 0, 1);
            row->record_count = 2;
            row->operand_count = 1;
            row->operand_classes[0] = mask_class;
        }
        else if (rule->family == CHECK_X86_LOWER_CONVERT)
        {
            row->records[0] = check_x86_lower_record(rule->form, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, v0);
            check_x86_lower_round(rule, round, &row->records[0]);
            row->record_count = 1;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_class = rule->other == 8 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_INT_CONVERT)
        {
            row->records[0] = check_x86_lower_record(rule->form, v0, v0, BUSTER_TIER0_ROLE_INTEGER(1));
            check_x86_lower_round(rule, round, &row->records[0]);
            row->record_count = 1;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = rule->other == 8 ? (rule->is_signed ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U64) :
                                                         (rule->is_signed ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U32);
        }
        else if (rule->family == CHECK_X86_LOWER_EXTRACT_LANE || rule->family == CHECK_X86_LOWER_TRUNCATE)
        {
            // The lane at the index (its low bits), or the truncation, into w's register under the mask (the form's rm
            // is its destination), then to VECTOR(0) at w's bytes.
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[1] = check_x86_lower_record(rule->form, v0, 0, v1);
            row->records[1].decorations = 1;
            if (rule->family == CHECK_X86_LOWER_EXTRACT_LANE)
            {
                row->records[1].immediate = (int32_t)(immediate & ((uint64_t)rule->bytes / rule->other - 1));
            }
            row->records[2] = check_x86_lower_move_result(rule->other, v1);
            row->record_count = 3;
            row->operand_count = 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->other;
            row->operand_classes[2] = mask_class;
            row->result_bytes = rule->other;
        }
        else if (rule->family == CHECK_X86_LOWER_BINARY_IMMEDIATE)
        {
            // An insertion's lane index is its immediate's low bits (VINSERTI's), a packed float compare's predicate clang's
            // code's (`fixed` CHECK_X86_PREDICATE_FCMP, its operands swapped where that says), the others' the whole byte.
            const uint64_t lanes_in = rule->other != 0 ? (uint64_t)rule->bytes / rule->other : 256;
            const bool fcmp = rule->fixed == CHECK_X86_PREDICATE_FCMP;
            bool swap = false;
            const uint64_t value = fcmp ? check_x86_lower_float_predicate(immediate, strict, &swap) : immediate & (lanes_in - 1);
            row->records[0] = check_x86_lower_record(rule->form, v0, swap ? v1 : v0, swap ? v0 : v1);
            row->records[0].immediate = (int32_t)value;
            row->record_count = 1;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->other != 0 ? rule->other : rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_UNARY_IMMEDIATE || rule->family == CHECK_X86_LOWER_UNARY)
        {
            // The result is of `other` bytes where they aren't a's (a narrowing conversion's, VCVTPD2DQ's of a YMM).
            row->records[0] = rule->swap ? check_x86_lower_record(rule->form, 0, v0, v0) : check_x86_lower_record(rule->form, v0, 0, v0);
            row->records[0].immediate = rule->family == CHECK_X86_LOWER_UNARY_IMMEDIATE ? (int32_t)(immediate & 0xff) : 0;
            check_x86_lower_round(rule, round, &row->records[0]);
            row->record_count = 1;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_bytes = rule->other != 0 ? rule->other : rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_UNARY_SCALAR)
        {
            row->records[0] = check_x86_lower_record(rule->form, v0, v0, v0);
            row->record_count = 1;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_MASK_UNARY || rule->family == CHECK_X86_LOWER_MASK_MERGE)
        {
            // The operation into w's register under the mask, then w to VECTOR(0) at its bytes. A MASK_UNARY's w is
            // VECTOR(1) and its mask INTEGER(2); a MASK_MERGE's b VECTOR(1), w VECTOR(2) and its mask INTEGER(3), and its
            // result as its rule's merge says (CheckX86LowerMerge). C_SCALAR's operation goes into a copy of w in
            // VECTOR(3), free as the mask's operand isn't a vector.
            const bool merge = rule->family == CHECK_X86_LOWER_MASK_MERGE;
            const uint8_t merged = merge ? rule->fixed : CHECK_X86_MERGE_W;
            const uint8_t w = merge ? v2 : v1;
            const uint8_t into = merged == CHECK_X86_MERGE_C_SCALAR ? v3 : w;
            const uint32_t w_bytes = !merge && rule->other != 0 ? rule->other : merged == CHECK_X86_MERGE_W_HALF ? rule->bytes / 2 : rule->bytes;
            // A mask of every lane's bit leaves nothing of w: the form unmasked, with no opmask to load.
            const uint64_t every = lanes >= 64 ? UINT64_MAX : (UINT64_C(1) << lanes) - 1;
            const bool unmasked = (mask & every) == every &&
                                  (merged == CHECK_X86_MERGE_W || merged == CHECK_X86_MERGE_W_ZEROED || merged == CHECK_X86_MERGE_W_HALF);
            uint32_t r = 0;
            if (!unmasked)
            {
                row->records[r++] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(merge ? 3 : 2));
            }
            if (merged == CHECK_X86_MERGE_C_SCALAR)
            {
                row->records[r++] = check_x86_lower_record(check_x86_lower_move(8, width, false), v3, 0, w);
            }
            row->records[r] = check_x86_lower_record(rule->form, into, merge ? v0 : 0, merge ? v1 : v0);
            row->records[r].decorations = unmasked ? 0 : (uint8_t)(1 | (merged == CHECK_X86_MERGE_W_ZEROED ? BUSTER_X86_DECORATION_ZEROING : 0));
            row->records[r].immediate = rule->immediate != BUSTER_CHECK_X86_NO_IMMEDIATE ? (int32_t)(immediate & 0xff) : 0;
            check_x86_lower_round(rule, round, &row->records[r]);
            r += 1;
            if (merged == CHECK_X86_MERGE_A)
            {
                // a's elements where the mask's bits are clear, the form's (w's) where they are set.
                row->records[r] = check_x86_lower_record(check_x86_lower_move(rule->element, width, false), v0, 0, w);
                row->records[r++].decorations = 1;
            }
            else if (merged == CHECK_X86_MERGE_A_SCALAR)
            {
                row->records[r] = check_x86_lower_record(X86_FORM_VMOVSS_X_X_X_EVEX, v0, v0, w);
                row->records[r++].decorations = 1;
            }
            else if (merged == CHECK_X86_MERGE_C_SCALAR)
            {
                row->records[r++] = check_x86_lower_record(X86_FORM_VMOVSS_X_X_X_EVEX, w, w, v3);
                row->records[r++] = check_x86_lower_move_result(w_bytes, w);
            }
            else
            {
                row->records[r++] = check_x86_lower_move_result(w_bytes, w);
            }
            row->record_count = (uint8_t)r;
            row->operand_count = merge ? 4 : 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = merge && rule->other != 0 ? rule->other : merge ? rule->bytes : w_bytes;
            row->operand_classes[merge ? 3 : 2] = mask_class;
            if (merge)
            {
                row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[2] = w_bytes;
            }
            row->result_bytes = w_bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_TERNARY)
        {
            // VPTERNLOG's and VFIXUPIMM's (those with an immediate) under their mask, merged into a, or with `fixed`
            // INTO_A_ZEROED zeroed where the mask's bits are clear (the maskz builtins), VFIXUPIMM's with the exceptions'
            // suppression its rounding control asks for.
            const bool masked_immediate = rule->immediate != BUSTER_CHECK_X86_NO_IMMEDIATE;
            const uint8_t zeroing = rule->fixed == CHECK_X86_INTO_A_ZEROED ? BUSTER_X86_DECORATION_ZEROING : 0;
            uint32_t r = 0;
            if (masked_immediate)
            {
                row->records[r++] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(3));
            }
            row->records[r] = check_x86_lower_record(rule->form, v0, v1, v2);
            row->records[r].immediate = masked_immediate ? (int32_t)(immediate & 0xff) : 0;
            row->records[r].decorations = masked_immediate ? (uint8_t)(1 | zeroing) : 0;
            check_x86_lower_round(rule, round, &row->records[r]);
            row->record_count = (uint8_t)(r + 1);
            row->operand_count = masked_immediate ? 4 : 3;
            for (uint32_t j = 0; j < 3; j += 1)
            {
                row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[j] = j == 2 && rule->other != 0 ? rule->other : rule->bytes;
            }
            row->operand_classes[3] = mask_class;
        }
        else if (rule->family == CHECK_X86_LOWER_GATHER || rule->family == CHECK_X86_LOWER_SCATTER)
        {
            // The scale is the immediate: 1, 2, 4 or 8 (clang checks it), its log2 the address's.
            const uint32_t scale = immediate == 1 ? 0 : immediate == 2 ? 1 : immediate == 4 ? 2 : immediate == 8 ? 3 : 4;
            const bool gather = rule->family == CHECK_X86_LOWER_GATHER;
            reason = scale == 4 ? "x86 builtins' gathers and scatters of a scale other than 1, 2, 4 or 8" : reason;
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(gather ? 3 : 1));
            row->records[1] = (X86Instruction){ .form = rule->form, .reg = gather ? v0 : v3, .rm = BUSTER_TIER0_ROLE_INTEGER(gather ? 1 : 0), .index = v2,
                                                .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(scale & 3), .decorations = 1 };
            row->record_count = 2;
            row->operand_count = 4;
            if (gather)
            {
                row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[0] = rule->bytes;
                row->operand_classes[1] = BUSTER_TYPED_CLASS_U64;
                row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[2] = rule->other;
                row->operand_classes[3] = mask_class;
            }
            else
            {
                row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                row->operand_classes[1] = mask_class;
                row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[2] = rule->bytes;
                row->operand_classes[3] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[3] = rule->other;
                row->result_class = BUSTER_TYPED_CLASS_VOID;
                row->result_bytes = 0;
            }
        }
        else if (rule->family == CHECK_X86_LOWER_BLENDV)
        {
            // The mask's vector, VECTOR(2), in the immediate's bits 7 to 4.
            row->records[0] = check_x86_lower_record(rule->form, v0, v0, v1);
            row->records[0].immediate = 2 << 4;
            row->record_count = 1;
            row->operand_count = 3;
            for (uint32_t j = 0; j < 3; j += 1)
            {
                row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[j] = rule->bytes;
            }
        }
        else if (rule->family == CHECK_X86_LOWER_FLAGS || rule->family == CHECK_X86_LOWER_KTEST)
        {
            // The form's flags of a and b (VECTOR(0) and VECTOR(1), or the masks moved into k1 and k2), then the
            // condition's int, in the first operand's register: a vector's is free, a mask's read.
            const bool masks = rule->family == CHECK_X86_LOWER_KTEST;
            uint32_t r = 0;
            if (masks)
            {
                row->records[r++] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
                row->records[r++] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 2, 0, BUSTER_TIER0_ROLE_INTEGER(1));
            }
            const uint8_t a = masks ? 1 : v0;
            const uint8_t b = masks ? 2 : v1;
            row->records[r++] = check_x86_lower_record(rule->form, rule->swap ? b : a, 0, rule->swap ? a : b);
            r = check_x86_lower_condition(row, r, rule->fixed, rule->other);
            row->record_count = (uint8_t)r;
            row->operand_count = 2;
            for (uint32_t j = 0; j < 2; j += 1)
            {
                row->operand_classes[j] = masks ? mask_class : BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[j] = masks ? 0 : rule->bytes;
            }
            row->result_class = BUSTER_TYPED_CLASS_I32;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_KUNARY)
        {
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(rule->form, 1, 0, 1);
            row->records[2] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 1);
            row->record_count = 3;
            row->operand_count = 1;
            row->operand_classes[0] = mask_class;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_SYSTEM)
        {
            // The integers of the rule's bytes; a pointer's a U64, the address in its register.
            const uint8_t integer = rule->bytes == 8 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
            const uint16_t move = rule->bytes == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32;
            const uint8_t i0 = BUSTER_TIER0_ROLE_INTEGER(0);
            const uint8_t i1 = BUSTER_TIER0_ROLE_INTEGER(1);
            const uint8_t result = BUSTER_TIER0_ROLE_INTEGER_RESULT;
            // prefetchi's hint is its form's: PREFETCHIT0's 3, PREFETCHIT1's 2 (clang takes no other).
            const uint16_t form = rule->form == X86_FORM_PREFETCHIT0_M8 && (immediate & 3) == 2 ? (uint16_t)X86_FORM_PREFETCHIT1_M8 : rule->form;
            row->result_class = BUSTER_TYPED_CLASS_VOID;
            row->result_bytes = 0;
            switch (rule->other)
            {
            case CHECK_X86_LAYOUT_NONE:
                row->records[0] = (X86Instruction){ .form = form };
                row->record_count = 1;
                break;
            case CHECK_X86_LAYOUT_READ:
                row->records[0] = check_x86_lower_record(form, 0, 0, result);
                row->record_count = 1;
                row->result_class = integer;
                break;
            case CHECK_X86_LAYOUT_WRITE:
                row->records[0] = check_x86_lower_record(form, 0, 0, i0);
                row->record_count = 1;
                row->operand_count = 1;
                row->operand_classes[0] = integer;
                break;
            case CHECK_X86_LAYOUT_MEMORY:
                row->records[0] = check_x86_lower_memory(form, 0, i0);
                row->record_count = 1;
                row->operand_count = 1;
                row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                break;
            case CHECK_X86_LAYOUT_STORE:
            case CHECK_X86_LAYOUT_STORE_VALUE_FIRST:
            {
                const bool value_first = rule->other == CHECK_X86_LAYOUT_STORE_VALUE_FIRST;
                row->records[0] = check_x86_lower_memory(form, value_first ? i0 : i1, value_first ? i1 : i0);
                row->record_count = 1;
                row->operand_count = 2;
                row->operand_classes[value_first ? 1 : 0] = BUSTER_TYPED_CLASS_U64;
                row->operand_classes[value_first ? 0 : 1] = integer;
                break;
            }
            case CHECK_X86_LAYOUT_POINTERS:
                row->records[0] = check_x86_lower_memory(form, i0, i1);
                row->record_count = 1;
                row->operand_count = 2;
                row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                row->operand_classes[1] = BUSTER_TYPED_CLASS_U64;
                break;
            case CHECK_X86_LAYOUT_WIDENED:
                // The 32-bit type, zero-extended by its move into a 64-bit register.
                row->records[0] = check_x86_lower_record(X86_FORM_MOV_R32_RM32, BUSTER_TIER0_ROLE_SCRATCH(0), 0, i0);
                row->records[1] = check_x86_lower_memory(form, BUSTER_TIER0_ROLE_SCRATCH(0), i1);
                row->record_count = 2;
                row->operand_count = 2;
                row->operand_classes[0] = BUSTER_TYPED_CLASS_U32;
                row->operand_classes[1] = BUSTER_TYPED_CLASS_U64;
                break;
            case CHECK_X86_LAYOUT_MODIFY:
                // RDSSP leaves its register as it was without a shadow stack: a's.
                row->records[0] = check_x86_lower_record(move, result, 0, i0);
                row->records[1] = check_x86_lower_record(form, 0, 0, result);
                row->record_count = 2;
                row->operand_count = 1;
                row->operand_classes[0] = integer;
                row->result_class = integer;
                break;
            case CHECK_X86_LAYOUT_CONDITION:
            case CHECK_X86_LAYOUT_POINTERS_CONDITION:
            {
                const bool pointers = rule->other == CHECK_X86_LAYOUT_POINTERS_CONDITION;
                row->records[0] = pointers ? check_x86_lower_memory(form, i0, i1) : (X86Instruction){ .form = form };
                row->record_count = (uint8_t)check_x86_lower_condition(row, 1, rule->fixed, 0);
                row->operand_count = pointers ? 2 : 0;
                row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                row->operand_classes[1] = BUSTER_TYPED_CLASS_U64;
                row->result_class = BUSTER_TYPED_CLASS_I32;
                break;
            }
            case CHECK_X86_LAYOUT_LOAD_INTEGER:
                // A narrower integer through a scratch register's low bytes, the result's field 32 bits at least.
                row->records[0] = check_x86_lower_memory(form, rule->bytes >= 4 ? result : BUSTER_TIER0_ROLE_SCRATCH(0), i0);
                row->records[1] = check_x86_lower_record(rule->bytes == 1 ? X86_FORM_MOVZX_R32_RM8 : X86_FORM_MOVZX_R32_RM16, result, 0,
                                                         BUSTER_TIER0_ROLE_SCRATCH(0));
                row->record_count = rule->bytes >= 4 ? 1 : 2;
                row->operand_count = 1;
                row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                row->result_class = integer;
                break;
            case CHECK_X86_LAYOUT_FROM:
                row->records[0] = check_x86_lower_record(form, i0, 0, result);
                row->record_count = 1;
                row->operand_count = 1;
                row->operand_classes[0] = integer;
                row->result_class = integer;
                break;
            case CHECK_X86_LAYOUT_REGISTERS:
                row->records[0] = check_x86_lower_record(form, i0, 0, i1);
                row->record_count = 1;
                row->operand_count = 2;
                row->operand_classes[0] = integer;
                row->operand_classes[1] = integer;
                break;
            default:
                // ACCUMULATE: CRC32 of the value into the accumulator.
                row->records[0] = check_x86_lower_record(move, result, 0, i0);
                row->records[1] = check_x86_lower_record(form, result, 0, i1);
                row->record_count = 2;
                row->operand_count = 2;
                row->operand_classes[0] = integer;
                row->operand_classes[1] = integer;
                row->result_class = integer;
                break;
            }
        }
        else if (rule->family == CHECK_X86_LOWER_RANDOM)
        {
            static const uint16_t stores[3] = { X86_FORM_MOV_RM16_R16, X86_FORM_MOV_RM32_R32, X86_FORM_MOV_RM64_R64 };
            row->records[0] = check_x86_lower_record(rule->form, 0, 0, BUSTER_TIER0_ROLE_SCRATCH(0));
            row->records[1] = check_x86_lower_memory(stores[rule->element == 2 ? 0 : rule->element == 4 ? 1 : 2], BUSTER_TIER0_ROLE_SCRATCH(0),
                                                     BUSTER_TIER0_ROLE_INTEGER(0));
            row->record_count = (uint8_t)check_x86_lower_condition(row, 2, CHECK_X86_CC_B, 0);
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
            row->result_class = BUSTER_TYPED_CLASS_I32;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_CARRY)
        {
            // The carry in is CF, which NEG of it sets where it isn't 0.
            const bool wide = rule->element == 8;
            row->records[0] = check_x86_lower_record(X86_FORM_NEG_RM32, 0, 0, BUSTER_TIER0_ROLE_INTEGER(0));
            row->records[1] = check_x86_lower_record(wide ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32, BUSTER_TIER0_ROLE_SCRATCH(0), 0,
                                                     BUSTER_TIER0_ROLE_INTEGER(1));
            row->records[2] = check_x86_lower_record(rule->form, BUSTER_TIER0_ROLE_SCRATCH(0), 0, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[3] = check_x86_lower_memory(wide ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32, BUSTER_TIER0_ROLE_SCRATCH(0),
                                                     BUSTER_TIER0_ROLE_INTEGER(3));
            row->record_count = (uint8_t)check_x86_lower_condition(row, 4, CHECK_X86_CC_B, 0);
            row->operand_count = 4;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_U32;
            row->operand_classes[1] = wide ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
            row->operand_classes[2] = wide ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
            row->operand_classes[3] = BUSTER_TYPED_CLASS_U64;
            row->result_class = BUSTER_TYPED_CLASS_I32;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_VECTOR_MEMORY)
        {
            const uint8_t i0 = BUSTER_TIER0_ROLE_INTEGER(0);
            row->result_class = BUSTER_TYPED_CLASS_VOID;
            row->result_bytes = 0;
            row->record_count = 1;
            if (rule->other == CHECK_X86_LAYOUT_MASKED_LOAD || rule->other == CHECK_X86_LAYOUT_MASKED_STORE)
            {
                // The mask's vector in vvvv: the load's into VECTOR(0), the store's of a.
                const bool load = rule->other == CHECK_X86_LAYOUT_MASKED_LOAD;
                row->records[0] = (X86Instruction){ .form = rule->form, .reg = load ? v0 : v2, .vvvv = v1, .rm = i0, .mode = BUSTER_X86_MEMORY_BASE };
                row->operand_count = load ? 2 : 3;
                row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[1] = rule->bytes;
                row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[2] = rule->bytes;
                row->result_class = load ? BUSTER_TYPED_CLASS_VECTOR : BUSTER_TYPED_CLASS_VOID;
                row->result_bytes = load ? rule->bytes : 0;
            }
            else if (rule->other == CHECK_X86_LAYOUT_LOAD || rule->other == CHECK_X86_LAYOUT_STORE_VECTOR)
            {
                const bool load = rule->other == CHECK_X86_LAYOUT_LOAD;
                row->records[0] = check_x86_lower_memory(rule->form, load ? v0 : v1, i0);
                row->operand_count = load ? 1 : 2;
                row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
                row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[1] = rule->bytes;
                row->result_class = load ? BUSTER_TYPED_CLASS_VECTOR : BUSTER_TYPED_CLASS_VOID;
                row->result_bytes = load ? rule->bytes : 0;
            }
            else
            {
                // BYTES_STORE: d's bytes where n's are negative, by n's signs in k1 and the byte store under it.
                row->records[0] = check_x86_lower_record(X86_FORM_VPMOVB2M_K_X, 1, 0, v1);
                row->records[1] = check_x86_lower_memory(rule->form, v0, BUSTER_TIER0_ROLE_INTEGER(2));
                row->records[1].decorations = 1;
                row->record_count = 2;
                row->operand_count = 3;
                row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[0] = rule->bytes;
                row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[1] = rule->bytes;
                row->operand_classes[2] = BUSTER_TYPED_CLASS_U64;
            }
        }
        else if (rule->family == CHECK_X86_LOWER_MASK_TERNARY)
        {
            // Into a (213: a = b * a + c), merged or zeroed under the mask, or into c (231: c = a * b + c), then to
            // VECTOR(0).
            const bool into_c = rule->fixed == CHECK_X86_INTO_C;
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(3));
            row->records[1] = into_c ? check_x86_lower_record(rule->form, v2, v0, v1) : check_x86_lower_record(rule->form, v0, v1, v2);
            row->records[1].decorations = (uint8_t)(1 | (rule->fixed == CHECK_X86_INTO_A_ZEROED ? BUSTER_X86_DECORATION_ZEROING : 0));
            check_x86_lower_round(rule, round, &row->records[1]);
            row->record_count = 2;
            if (into_c)
            {
                row->records[2] = check_x86_lower_move_result(rule->bytes, v2);
                row->record_count = 3;
            }
            row->operand_count = 4;
            for (uint32_t j = 0; j < 3; j += 1)
            {
                row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
                row->operand_bytes[j] = rule->bytes;
            }
            row->operand_classes[3] = mask_class;
        }
        else if (rule->family == CHECK_X86_LOWER_REDUCE)
        {
            // The first row (check_x86_lower_reduce_row has the second): a's halves from 64 or 32 bytes to 16, then the
            // sum's (or product's, or greater's, or lesser's) halves, each operation's first operand the lower half; of
            // half-precision elements, which take a step more than the second row has room for, then the 16 bytes' halves
            // swapped (VPERMILPD) and the same of them.
            uint32_t r = 0;
            if (rule->bytes == 64)
            {
                row->records[r] = check_x86_lower_record(X86_FORM_VEXTRACTF64X4_YM256_Z_IMM8, v0, 0, v1);
                row->records[r++].immediate = 1;
                row->records[r++] = check_x86_lower_record(check_x86_lower_reduction_form(rule->fixed, rule->element, 32), v0, v0, v1);
            }
            if (rule->bytes >= 32)
            {
                row->records[r] = check_x86_lower_record(X86_FORM_VEXTRACTF32X4_XM128_Y_IMM8, v0, 0, v1);
                row->records[r++].immediate = 1;
                row->records[r++] = check_x86_lower_record(check_x86_lower_reduction_form(rule->fixed, rule->element, 16), v0, v0, v1);
            }
            if (rule->element == 2)
            {
                row->records[r] = check_x86_lower_record(X86_FORM_VPERMILPD_X_XM128_IMM8_EVEX, v1, 0, v0);
                row->records[r++].immediate = 1;
                row->records[r++] = check_x86_lower_record(check_x86_lower_reduction_form(rule->fixed, 2, 16), v0, v0, v1);
            }
            row->record_count = (uint8_t)r;
            row->operand_count = 1;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->result_bytes = 16;
        }
        else if (rule->family == CHECK_X86_LOWER_XOP_COMPARE)
        {
            // XOP's predicates (less, less or equal, greater, greater or equal, equal, not equal, false and true) in VPCMP's
            // numbering (equal 0, less 1, less or equal 2, false 3, not equal 4, not less 5 and not less or equal 6, true 7).
            static const uint8_t predicates[8] = { 1, 2, 6, 5, 0, 4, 3, 7 };
            static const uint16_t vectors[4] = { X86_FORM_VPMOVM2B_X_K, X86_FORM_VPMOVM2W_X_K, X86_FORM_VPMOVM2D_X_K, X86_FORM_VPMOVM2Q_X_K };
            const uint32_t size = rule->element == 1 ? 0 : rule->element == 2 ? 1 : rule->element == 4 ? 2 : 3;
            row->records[0] = check_x86_lower_record(check_x86_lower_compare_form(rule->is_signed, size, width), 1, v0, v1);
            row->records[0].immediate = predicates[immediate & 7];
            row->records[1] = check_x86_lower_record(vectors[size], v0, 0, 1);
            row->record_count = 2;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
        }
        else if (rule->family == CHECK_X86_LOWER_SCALAR_COMPARE)
        {
            row->records[0] = check_x86_lower_record(rule->form, 1, v0, v1);
            row->records[0].immediate = (int32_t)(immediate & 31);
            check_x86_lower_round(rule, round, &row->records[0]);
            row->records[1] = check_x86_lower_record(X86_FORM_KMOVW_R32_K, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 1);
            row->record_count = 2;
            row->operand_count = 2;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->result_class = BUSTER_TYPED_CLASS_I32;
            row->result_bytes = 0;
        }
        else if (rule->family == CHECK_X86_LOWER_INTERSECT)
        {
            // VP2INTERSECT zeroes its masks' bits past the lanes, so 8 bits hold a mask of 2, 4 or 8.
            const uint16_t store = lanes <= 8 ? X86_FORM_KMOVB_M8_K : X86_FORM_KMOVW_M16_K;
            row->records[0] = check_x86_lower_record(rule->form, 2, v0, v1);
            row->records[1] = check_x86_lower_memory(store, 2, BUSTER_TIER0_ROLE_INTEGER(2));
            row->records[2] = check_x86_lower_memory(store, 3, BUSTER_TIER0_ROLE_INTEGER(3));
            row->record_count = 3;
            row->operand_count = 4;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = rule->bytes;
            row->operand_classes[2] = BUSTER_TYPED_CLASS_U64;
            row->operand_classes[3] = BUSTER_TYPED_CLASS_U64;
            row->result_class = BUSTER_TYPED_CLASS_VOID;
            row->result_bytes = 0;
        }
        else
        {
            // MASK_BINARY: the form's mask into k2 under the mask in k1, of a and b, or with an immediate of a alone (VFPCLASS's
            // classes, the immediate's whole byte), whose mask is then its second operand.
            const bool classes = rule->immediate != BUSTER_CHECK_X86_NO_IMMEDIATE;
            row->records[0] = check_x86_lower_record(check_x86_lower_mask_in(lanes), 1, 0, BUSTER_TIER0_ROLE_INTEGER(classes ? 1 : 2));
            row->records[1] = check_x86_lower_record(rule->form, 2, classes ? 0 : v0, classes ? v0 : v1);
            row->records[1].immediate = classes ? (int32_t)(immediate & 0xff) : 0;
            row->records[1].decorations = 1;
            row->records[2] = check_x86_lower_record(check_x86_lower_mask_out(lanes), BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 2);
            row->record_count = 3;
            row->operand_count = classes ? 2 : 3;
            row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[0] = rule->bytes;
            row->operand_classes[1] = classes ? mask_class : BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = classes ? 0 : rule->bytes;
            row->operand_classes[2] = classes ? 0 : mask_class;
            row->result_class = mask_class;
            row->result_bytes = 0;
        }
    }
    const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    if (reason == NULL && (target->features & evex) != evex)
    {
        Tier0Operation baseline;
        const bool older = rule->family == CHECK_X86_LOWER_BINARY || rule->family == CHECK_X86_LOWER_SHIFT ||
                           rule->family == CHECK_X86_LOWER_EXTRACT || rule->family == CHECK_X86_LOWER_INSERT ||
                           rule->family == CHECK_X86_LOWER_BINARY_IMMEDIATE || rule->family == CHECK_X86_LOWER_UNARY_IMMEDIATE ||
                           rule->family == CHECK_X86_LOWER_UNARY;
        if (older && check_vector_baseline_row(target, row, &baseline))
        {
            *row = baseline;
            // These two were requirements of the EVEX implementation, not of the reviewed SSE/VEX operation.
            needed &= ~(BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ));
            if (rule->form == X86_FORM_VSHUFPD_X_X_XM128_IMM8)
            {
                needed &= ~BUSTER_X86_FEATURE(AVX);
            }
        }
        else if (older)
        {
            reason = "x86 builtins whose instructions the target hasn't";
        }
    }
    if (reason == NULL && ((target->features & needed) != needed || rule == NULL || (target->extensions & rule->extensions) != rule->extensions))
    {
        reason = "x86 builtins whose instructions the target hasn't";
    }
    return reason;
}
