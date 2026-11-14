// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Clang's generic vector builtins as Tier 0's machine operations (#136, #148): the rows (Tier0Operation) of
// __builtin_shufflevector, __builtin_convertvector, the __builtin_elementwise_* functions, the __builtin_reduce_*
// functions and __builtin_nontemporal_load and _store at their vectors' types, as clang -O0's code computes them, an
// instruction per element type where AVX-512 has one. A row's forms are EVEX ones of AVX-512F and AVX-512VL (at a target
// without them, their reviewed SSE or VEX forms, check_vector_baseline_row, and SSE's and AVX's shuffles,
// check_vector_baseline_shuffle_row: #775), and those of the extension each row names:
// AVX-512BW's for 8- and 16-bit elements, DQ's conversions of 64-bit integers, VBMI's byte permutes, BITALG's and
// VPOPCNTDQ's counts, CD's leading zero counts, VBMI2's funnel shifts, GFNI's bit reversal and AVX512-BMM's byte bit
// reversal; and only forms x86_semantics.c covers (tier0_operation_form_valid). Byte products reduce through widened
// words, keeping their final low byte. Floating elements take conversions between floats, doubles and integers of 32
// and 64 bits, from half-precision and bf16 elements to floats and from floats to half-precision ones, the square roots
// and fused multiply-adds of floats and doubles, vectors and scalars (check_vector_scalar_row), and the functions clang's
// code computes by instructions, of vectors and scalars (check_vector_floating_row, check_vector_floating_scalar_row:
// roundings, canonicalize, copysign, the maximum and minimum functions; check_vector_ldexp_row) and the floating
// reductions (check_vector_reduce_floating_rows); the C library's functions (sin, pow and the rest) are the lowering's
// calls (check_lower_vector_calls). What the rows don't take is refused by name: the other floating functions of
// half-precision and bf16 elements, 8-bit funnel shifts, saturating
// arithmetic of 32- and 64-bit elements and carry-less products. Included by c_check_program_lower.c after
// c_check_program_x86.c, whose record helpers it takes.

#include "c_check_program_vector_convert.c"

// The most elements a shuffle's index vector has (64 bytes of 1-byte elements), and an index of no element (clang's -1:
// the result's element is undefined).
#define BUSTER_CHECK_VECTOR_SHUFFLE_LANES 64u
#define BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED UINT32_MAX

// A shuffle's plan beside its row: whether the row reads its second vector, b, and whether it permutes, which takes
// the index vector `constant` (its bytes `constant_bytes`, 16, 32 or 64, the first element's first) as its last operand.
typedef struct CheckVectorShuffle CheckVectorShuffle;
struct CheckVectorShuffle
{
    uint64_t constant[8];
    uint32_t constant_bytes;
    // A narrow shuffle's second vector, a constant of 16 bytes (check_vector_narrow_shuffle_row).
    uint64_t second[8];
    bool reads_b;
    bool permutes;
    // Whether no row of the target's sequences takes it and it runs element by element instead (#1834).
    bool elementwise;
};

// The row of __builtin_shufflevector(a, b, indices...) of vectors of `source`'s type into one of `result`'s, its
// elements `indices` (`result`'s lanes of them): each an element of a (below a's lanes) or of b (from there to twice
// them), or BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED. Its operands are a, then b where the plan reads it or `b_kept` (b has
// effects to evaluate though its elements aren't read), then the index vector where it permutes.
//
// A shuffle whose every defined element is a's own at its place (a prefix, or a's elements past undefined ones: the
// intrinsics' casts) has no records: a's register holds the result, its bytes past a's zero (Tier 0 loads an operand
// so). Any other is one VPERMT2 at the wider of the two vectors' widths (F bytes, T elements): a is its first table and
// its destination, b its second (or a again where the shuffle doesn't read b), and the index vector selects element i
// of a as i and element i of b as T + i, an undefined element as 0.
static const char* check_vector_shuffle_row(const TypeTable* types, const X86Target* target, TypeId source, TypeId result, const uint32_t* indices, bool b_kept,
                                            Tier0Operation* row, CheckVectorShuffle* plan)
{
    const CheckVectorShape in = check_vector_builtin_shape(types, source);
    const CheckVectorShape out = check_vector_builtin_shape(types, result);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const char* reason = NULL;
    bool identity = true;
    bool reads_b = false;
    for (uint32_t i = 0; i < out.lanes && i < BUSTER_CHECK_VECTOR_SHUFFLE_LANES; i += 1)
    {
        const uint32_t k = indices[i];
        identity &= k == BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED || (k == i && k < in.lanes);
        reads_b |= k != BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED && k >= in.lanes;
    }
    memset(row, 0, sizeof(*row));
    *plan = (CheckVectorShuffle){ .reads_b = reads_b, .permutes = !identity };
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)out.bytes;
    row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    row->operand_bytes[0] = (uint8_t)in.bytes;
    uint32_t operands = 1;
    if (reads_b || b_kept)
    {
        row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[1] = (uint8_t)in.bytes;
        operands = 2;
    }
    if (in.element == 0 || out.lanes > BUSTER_CHECK_VECTOR_SHUFFLE_LANES || in.lanes == 0)
    {
        reason = "vector shuffles of elements of this size";
    }
    else if (!identity && (target->features & (BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL))) !=
                              (BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL)))
    {
        // Without VPERMT2, SSE's and AVX's shuffles (#775), with a constant operand where they take one.
        uint8_t constant[64] = { 0 };
        uint32_t constant_bytes = 0;
        reason = check_vector_baseline_shuffle_row(target, in.element, in.lanes, in.bytes, out.lanes, out.bytes, indices, BUSTER_TIER0_ROLE_VECTOR(operands), row,
                                                   constant, &constant_bytes, &plan->elementwise);
        plan->permutes = constant_bytes != 0;
        if (constant_bytes != 0)
        {
            memcpy(plan->constant, constant, sizeof(constant));
            plan->constant_bytes = constant_bytes;
            row->operand_classes[operands] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[operands] = (uint8_t)constant_bytes;
            operands += 1;
        }
    }
    else if (!identity)
    {
        // The wider vector's register: an XMM one for vectors of 4 and 8 bytes.
        const uint32_t wider = in.bytes > out.bytes ? in.bytes : out.bytes;
        const uint32_t bytes = wider > 16 ? wider : 16;
        const uint32_t table = bytes / in.element;
        uint8_t constant[64] = { 0 };
        for (uint32_t i = 0; i < out.lanes; i += 1)
        {
            const uint32_t k = indices[i];
            const uint64_t index = k == BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED ? 0 : k < in.lanes ? k : k - in.lanes + table;
            // Little-endian: an element's index in its low byte, the others' zero (VPERMT2 reads its low bits alone).
            constant[i * in.element] = (uint8_t)index;
        }
        memcpy(plan->constant, constant, sizeof(constant));
        plan->constant_bytes = bytes;
        static const CheckVectorForms permutes[4] = {
            { { X86_FORM_VPERMT2B_X_X_XM128, X86_FORM_VPERMT2B_Y_Y_YM256, X86_FORM_VPERMT2B_Z_Z_ZM512 } },
            { { X86_FORM_VPERMT2W_X_X_XM128, X86_FORM_VPERMT2W_Y_Y_YM256, X86_FORM_VPERMT2W_Z_Z_ZM512 } },
            { { X86_FORM_VPERMT2D_X_X_XM128, X86_FORM_VPERMT2D_Y_Y_YM256, X86_FORM_VPERMT2D_Z_Z_ZM512 } },
            { { X86_FORM_VPERMT2Q_X_X_XM128, X86_FORM_VPERMT2Q_Y_Y_YM256, X86_FORM_VPERMT2Q_Z_Z_ZM512 } },
        };
        const uint8_t index_role = BUSTER_TIER0_ROLE_VECTOR(operands);
        row->records[0] = check_vector_lower_record(permutes[check_vector_builtin_element(in.element)].forms[check_vector_builtin_width(bytes)], v0, index_role,
                                                    reads_b ? BUSTER_TIER0_ROLE_VECTOR(1) : v0);
        row->record_count = 1;
        row->operand_classes[operands] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[operands] = (uint8_t)bytes;
        operands += 1;
        const uint64_t needed = in.element == 1 ? BUSTER_X86_FEATURE(AVX512VBMI) : in.element == 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
        reason = (target->features & needed) != needed ? (in.element == 1 ? "vector shuffles of 8-bit elements without AVX-512 VBMI" :
                                                                  "vector shuffles of 16-bit elements without AVX-512BW") :
                                               NULL;
        // Without VPERMT2B or VPERMT2W, the lowering takes the shuffle element by element instead, as it takes the
        // baseline's (#1834): x86-64-v4's byte shuffles, the MMX unpacks' among them.
        plan->elementwise = reason != NULL;
    }
    row->operand_count = (uint8_t)operands;
    return reason;
}

// The row of a shuffle of a conversion into a vector Tier 0 has no class for (`narrow`, of 2 bytes) with a constant
// vector of its type (`b_bytes`, its bytes, or NULL where the shuffle reads none of its elements): the intrinsics'
// narrowing conversions, which widen the result back with zeros (_mm_cvtepi64_epi8's
// `__builtin_shufflevector(__builtin_convertvector(x, v2qi), (v2qi){0, 0}, 0, 1, 2, 3, 3, ...)`). It truncates
// x into its register's first bytes (check_vector_convert_row's VPMOV), which zeroes the rest: where every result
// element is the conversion's at its place or a zero of the constant's past the conversion's, that is the result. Else
// it permutes the two (VPERMT2, at the result's width or 16 bytes): the conversion's elements are the first table's, the
// constant's the second's. Its operands are x, then where it permutes the constant (`plan->second`, 16 bytes) and the
// index vector.
static const char* check_vector_narrow_shuffle_row(const TypeTable* types, const X86Target* target, TypeId converted, TypeId narrow, TypeId result,
                                                   const uint32_t* indices, const uint8_t* b_bytes, Tier0Operation* row, CheckVectorShuffle* plan)
{
    const CheckVectorShape in = check_vector_builtin_shape(types, converted);
    const CheckVectorShape middle = check_vector_builtin_shape(types, narrow);
    const CheckVectorShape out = check_vector_builtin_shape(types, result);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const char* reason = check_vector_convert_row(types, target, converted, in.bytes, narrow, row);
    bool in_place = true;
    bool reads_b = false;
    for (uint32_t i = 0; i < out.lanes && i < BUSTER_CHECK_VECTOR_SHUFFLE_LANES; i += 1)
    {
        const uint32_t k = indices[i];
        bool zero = k != BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED && k >= middle.lanes && b_bytes != NULL;
        for (uint32_t byte = 0; zero && byte < out.element; byte += 1)
        {
            zero = b_bytes[(k - middle.lanes) * out.element + byte] == 0;
        }
        in_place &= k == BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED || (i < middle.lanes ? k == i : zero);
        reads_b |= k != BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED && k >= middle.lanes;
    }
    *plan = (CheckVectorShuffle){ .reads_b = reads_b && !in_place, .permutes = !in_place };
    if (reason == NULL && (row->record_count != 1 || out.element != middle.element || out.lanes > BUSTER_CHECK_VECTOR_SHUFFLE_LANES ||
                           middle.bytes >= 16 || (reads_b && b_bytes == NULL)))
    {
        reason = "vector types of other than 4, 8, 16, 32 or 64 bytes";
    }
    else if (reason == NULL)
    {
        row->result_bytes = (uint8_t)out.bytes;
        if (!in_place)
        {
            const uint32_t bytes = out.bytes > 16 ? out.bytes : 16;
            const uint32_t table = bytes / out.element;
            uint8_t constant[64] = { 0 };
            for (uint32_t i = 0; i < out.lanes; i += 1)
            {
                const uint32_t k = indices[i];
                constant[i * out.element] = (uint8_t)(k == BUSTER_CHECK_VECTOR_SHUFFLE_UNDEFINED ? 0 : k < middle.lanes ? k : k - middle.lanes + table);
            }
            memcpy(plan->constant, constant, sizeof(constant));
            plan->constant_bytes = bytes;
            uint8_t second[64] = { 0 };
            if (b_bytes != NULL)
            {
                memcpy(second, b_bytes, middle.bytes);
            }
            memcpy(plan->second, second, sizeof(second));
            static const CheckVectorForms permutes[4] = {
                { { X86_FORM_VPERMT2B_X_X_XM128, X86_FORM_VPERMT2B_Y_Y_YM256, X86_FORM_VPERMT2B_Z_Z_ZM512 } },
                { { X86_FORM_VPERMT2W_X_X_XM128, X86_FORM_VPERMT2W_Y_Y_YM256, X86_FORM_VPERMT2W_Z_Z_ZM512 } },
                { { X86_FORM_VPERMT2D_X_X_XM128, X86_FORM_VPERMT2D_Y_Y_YM256, X86_FORM_VPERMT2D_Z_Z_ZM512 } },
                { { X86_FORM_VPERMT2Q_X_X_XM128, X86_FORM_VPERMT2Q_Y_Y_YM256, X86_FORM_VPERMT2Q_Z_Z_ZM512 } },
            };
            row->records[1] = check_vector_lower_record(permutes[check_vector_builtin_element(out.element)].forms[check_vector_builtin_width(bytes)], v0,
                                                        BUSTER_TIER0_ROLE_VECTOR(2), BUSTER_TIER0_ROLE_VECTOR(1));
            row->record_count = 2;
            row->operand_count = 3;
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = 16;
            row->operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[2] = (uint8_t)bytes;
            const uint64_t needed = out.element == 1 ? BUSTER_X86_FEATURE(AVX512VBMI) : out.element == 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
            reason = (target->features & needed) != needed ? (out.element == 1 ? "vector shuffles of 8-bit elements without AVX-512 VBMI" :
                                                                       "vector shuffles of 16-bit elements without AVX-512BW") :
                                                   NULL;
        }
    }
    return reason;
}

// A row's constant operands, past its arguments, whose loads stay outside the operation's eight records, as a shuffle's
// constant index vector's do: each a pattern of `bytes` bytes (1, 2, 4, 8 or 16, its first byte lowest) repeated through
// the operand's bytes, which are the vector's (CheckVectorConstants.vector bytes, at least 16 where `wide`), or a scalar
// row's floating constant, its class's bits (4 or 8 bytes). XOP's byte funnel shifts take 7, 0 and, for left shifts,
// -1; the floating functions their sign or magnitude masks, 1.0 and the largest float below 0.5.
#define BUSTER_CHECK_VECTOR_CONSTANTS 3u

typedef struct CheckVectorConstants CheckVectorConstants;
struct CheckVectorConstants
{
    uint8_t patterns[BUSTER_CHECK_VECTOR_CONSTANTS][16];
    uint8_t bytes[BUSTER_CHECK_VECTOR_CONSTANTS];
    uint32_t count;
};

// Adds a constant of a pattern of `bytes` bytes to a row's, as its next operand of `class` (VECTOR of `operand_bytes`,
// or F32 or F64).
static void check_vector_constant_bytes(Tier0Operation* row, CheckVectorConstants* constants, const uint8_t* pattern, uint32_t bytes, uint8_t class, uint32_t operand_bytes)
{
    const uint32_t c = constants->count;
    if (c < BUSTER_CHECK_VECTOR_CONSTANTS && row->operand_count < BUSTER_TIER0_OPERATION_OPERANDS && bytes <= 16)
    {
        memset(constants->patterns[c], 0, sizeof(constants->patterns[c]));
        memcpy(constants->patterns[c], pattern, bytes);
        constants->bytes[c] = (uint8_t)bytes;
        constants->count = c + 1;
        row->operand_classes[row->operand_count] = class;
        row->operand_bytes[row->operand_count] = class == BUSTER_TYPED_CLASS_VECTOR ? (uint8_t)operand_bytes : 0;
        row->operand_count += 1;
    }
}

// The same of a pattern of up to 8 bytes, its value's low ones.
static void check_vector_constant(Tier0Operation* row, CheckVectorConstants* constants, uint64_t value, uint32_t bytes, uint8_t class, uint32_t operand_bytes)
{
    uint8_t pattern[8];
    for (uint32_t b = 0; b < 8; b += 1)
    {
        pattern[b] = (uint8_t)(value >> (8 * b));
    }
    check_vector_constant_bytes(row, constants, pattern, bytes <= 8 ? bytes : 8, class, operand_bytes);
}

// The row of a masked memory builtin (#148), as clang's code computes llvm.masked.* with AVX-512: the mask, a vector of
// bools, is an integer operand (its bits, which the lowering loads from its object and clears past its lanes where the
// instruction has more: `*kept` is the lanes' bits then, else 0), moved into k1 (KMOVW, AVX-512BW's KMOVD and KMOVQ past
// 16 lanes), and one instruction runs under k1:
//   - __builtin_masked_load (mask, p[, passthru]): VMOVDQU8, 16, 32 or 64 from p, of the elements k1 has, the others
//     zero or the passthru's;
//   - __builtin_masked_expand_load: VPEXPANDB, W, D or Q (8- and 16-bit elements' AVX-512 VBMI2's) from p the same way,
//     the elements k1 has taking p's consecutive ones;
//   - __builtin_masked_store (mask, v, p) and _compress_store: VMOVDQU or VPCOMPRESS of v's elements k1 has to p;
//   - __builtin_masked_gather (mask, index, p[, passthru]): VPGATHERDD, DQ, QD or QQ of p's elements at the index's
//     elements, sign-extended to 32 bits where narrower (VPMOVSX), as a GEP's indices are, times the element's bytes,
//     into zeros or the passthru;
//   - __builtin_masked_scatter (mask, index, v, p): VPSCATTER of v's elements there, times the index's element's bytes,
//     as clang 23's code has it (its GEP's element type is the index's).
// Gathers and scatters of 8- and 16-bit elements, which no instruction has, are refused by name. Its operands are the
// arguments, in their order; a loaded or gathered result is VECTOR(0)'s.
static const char* check_vector_masked_row(const TypeTable* types, const X86Target* target, CBuiltinId builtin, TypeId value, TypeId index, TypeId mask, bool passthru,
                                           Tier0Operation* row, uint64_t* kept)
{
    const bool gather = builtin == CBUILTIN_MASKED_GATHER;
    const bool scatter = builtin == CBUILTIN_MASKED_SCATTER;
    const bool addressed = gather || scatter;
    const bool expands = builtin == CBUILTIN_MASKED_EXPAND_LOAD || builtin == CBUILTIN_MASKED_COMPRESS_STORE;
    const bool loads = builtin == CBUILTIN_MASKED_LOAD || builtin == CBUILTIN_MASKED_EXPAND_LOAD || gather;
    const CheckVectorShape shape = check_vector_builtin_shape(types, value);
    const CheckVectorShape indices = addressed ? check_vector_builtin_shape(types, index) : shape;
    const uint32_t lanes = check_builtin_vector_lanes(types, mask);
    const uint64_t mask_bytes = type_size(types, mask);
    const uint32_t e = shape.element;
    // The index's elements as the instruction takes them (8- and 16-bit ones sign-extended to 32 bits), and its bytes.
    const uint32_t i = indices.element < 4 ? 4 : indices.element;
    const uint32_t data_bytes = shape.bytes < 16 ? 16 : shape.bytes;
    const uint32_t index_bytes = !addressed || lanes * i < 16 ? 16 : lanes * i;
    // The instruction's lanes: the data's, or a narrowing gather's or scatter's (quadword indices, doubleword elements)
    // its index's.
    const uint32_t instruction_lanes = e == 0 ? 0 : addressed && e == 4 && i == 8 ? index_bytes / 8 : data_bytes / e;
    const uint64_t needed = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | (e <= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0) |
                            (e <= 2 && expands ? BUSTER_X86_FEATURE(AVX512VBMI2) : 0);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    *kept = 0;
    if (e == 0 || (addressed && indices.element == 0))
    {
        reason = "masked vector memory builtins of elements of other than 1, 2, 4 or 8 bytes";
    }
    else if (addressed && e <= 2)
    {
        reason = "masked gathers and scatters of 8- and 16-bit elements, which no x86 instruction gathers or scatters";
    }
    else if (lanes == 0 || lanes > instruction_lanes || (mask_bytes != 1 && mask_bytes != 2 && mask_bytes != 4 && mask_bytes != 8) || index_bytes > 64)
    {
        reason = "vector types of other than 4, 8, 16, 32 or 64 bytes";
    }
    else if ((target->features & needed) != needed)
    {
        reason = (target->features & (BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL))) !=
                         (BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL)) ? "masked vector memory builtins without AVX-512F and AVX-512VL" :
                 (target->features & BUSTER_X86_FEATURE(AVX512BW)) == 0 ? "masked vector memory builtins of 8- and 16-bit elements without AVX-512BW" :
                                                                         "masked expanding loads and compressing stores of 8- and 16-bit elements without AVX-512 VBMI2";
    }
    else
    {
        static const CheckVectorForms expansions[4] = { BUSTER_CHECK_VECTOR_FORMS(VPEXPANDB_X_XM128, VPEXPANDB_Y_YM256, VPEXPANDB_Z_ZM512),
                                                        BUSTER_CHECK_VECTOR_FORMS(VPEXPANDW_X_XM128, VPEXPANDW_Y_YM256, VPEXPANDW_Z_ZM512),
                                                        BUSTER_CHECK_VECTOR_FORMS(VPEXPANDD_X_XM128, VPEXPANDD_Y_YM256, VPEXPANDD_Z_ZM512),
                                                        BUSTER_CHECK_VECTOR_FORMS(VPEXPANDQ_X_XM128, VPEXPANDQ_Y_YM256, VPEXPANDQ_Z_ZM512) };
        static const CheckVectorForms compressions[4] = { BUSTER_CHECK_VECTOR_FORMS(VPCOMPRESSB_XM128_X, VPCOMPRESSB_YM256_Y, VPCOMPRESSB_ZM512_Z),
                                                          BUSTER_CHECK_VECTOR_FORMS(VPCOMPRESSW_XM128_X, VPCOMPRESSW_YM256_Y, VPCOMPRESSW_ZM512_Z),
                                                          BUSTER_CHECK_VECTOR_FORMS(VPCOMPRESSD_XM128_X, VPCOMPRESSD_YM256_Y, VPCOMPRESSD_ZM512_Z),
                                                          BUSTER_CHECK_VECTOR_FORMS(VPCOMPRESSQ_XM128_X, VPCOMPRESSQ_YM256_Y, VPCOMPRESSQ_ZM512_Z) };
        // By the elements' bytes and the indices' (4 and 4, 8 and 4, 4 and 8, 8 and 8), at the data's width but a
        // narrowing one's, at its index's.
        static const CheckVectorForms gathers[4] = { BUSTER_CHECK_VECTOR_FORMS(VPGATHERDD_X_VM32X, VPGATHERDD_Y_VM32Y, VPGATHERDD_Z_VM32Z),
                                                     BUSTER_CHECK_VECTOR_FORMS(VPGATHERDQ_X_VM32X, VPGATHERDQ_Y_VM32X, VPGATHERDQ_Z_VM32Y),
                                                     BUSTER_CHECK_VECTOR_FORMS(VPGATHERQD_X_VM64X, VPGATHERQD_X_VM64Y, VPGATHERQD_Y_VM64Z),
                                                     BUSTER_CHECK_VECTOR_FORMS(VPGATHERQQ_X_VM64X, VPGATHERQQ_Y_VM64Y, VPGATHERQQ_Z_VM64Z) };
        static const CheckVectorForms scatters[4] = { BUSTER_CHECK_VECTOR_FORMS(VPSCATTERDD_VM32X_X, VPSCATTERDD_VM32Y_Y, VPSCATTERDD_VM32Z_Z),
                                                      BUSTER_CHECK_VECTOR_FORMS(VPSCATTERDQ_VM32X_X, VPSCATTERDQ_VM32X_Y, VPSCATTERDQ_VM32Y_Z),
                                                      BUSTER_CHECK_VECTOR_FORMS(VPSCATTERQD_VM64X_X, VPSCATTERQD_VM64Y_X, VPSCATTERQD_VM64Z_Y),
                                                      BUSTER_CHECK_VECTOR_FORMS(VPSCATTERQQ_VM64X_X, VPSCATTERQQ_VM64Y_Y, VPSCATTERQQ_VM64Z_Z) };
        static const CheckVectorForms extensions[2] = { BUSTER_CHECK_VECTOR_FORMS(VPMOVSXBD_X_XM32_EVEX, VPMOVSXBD_Y_XM64_EVEX, VPMOVSXBD_Z_XM128),
                                                        BUSTER_CHECK_VECTOR_FORMS(VPMOVSXWD_X_XM64_EVEX, VPMOVSXWD_Y_XM128_EVEX, VPMOVSXWD_Z_YM256) };
        static const CheckVectorForms zeros = BUSTER_CHECK_VECTOR_PACKED(VPXORD);
        const uint32_t size = e == 1 ? 0 : e == 2 ? 1 : e == 4 ? 2 : 3;
        const uint32_t width = check_vector_builtin_width(data_bytes);
        const uint32_t pair = (e == 8 ? 1u : 0u) | (i == 8 ? 2u : 0u);
        const uint32_t pair_width = pair == 2 ? check_vector_builtin_width(index_bytes) : width;
        const uint8_t mask_class = mask_bytes == 8 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
        // The pointer's operand, and the value's or the passthru's.
        const uint32_t pointer = builtin == CBUILTIN_MASKED_LOAD || builtin == CBUILTIN_MASKED_EXPAND_LOAD ? 1 : addressed && !scatter ? 2 : addressed ? 3 : 2;
        const uint8_t source = BUSTER_TIER0_ROLE_VECTOR(scatter ? 2 : 1);
        const uint8_t merged = BUSTER_TIER0_ROLE_VECTOR(gather ? 3 : 2);
        uint8_t indexed = BUSTER_TIER0_ROLE_VECTOR(1);
        uint32_t r = 0;
        *kept = lanes < instruction_lanes ? (UINT64_C(1) << lanes) - 1 : 0;
        row->records[r++] = check_x86_lower_record(mask_bytes == 8 ? X86_FORM_KMOVQ_K_R64 : check_x86_lower_mask_in(instruction_lanes <= 16 ? 16 : 32), 1, 0,
                                                   BUSTER_TIER0_ROLE_INTEGER(0));
        if (addressed && indices.element < 4)
        {
            indexed = BUSTER_TIER0_ROLE_VECTOR(4);
            row->records[r++] = check_vector_lower_record(extensions[indices.element == 1 ? 0 : 1].forms[check_vector_builtin_width(index_bytes)], indexed, 0,
                                                          BUSTER_TIER0_ROLE_VECTOR(1));
        }
        if (loads && passthru)
        {
            row->records[r++] = check_x86_lower_move_result(data_bytes, merged);
        }
        else if (gather)
        {
            row->records[r++] = check_vector_lower_record(zeros.forms[width], v0, v0, v0);
        }
        if (addressed)
        {
            const uint32_t scale = gather ? e : indices.element;
            const uint32_t form = gather ? gathers[pair].forms[pair_width] : scatters[pair].forms[pair_width];
            row->records[r++] = (X86Instruction){ .form = (uint16_t)form, .reg = gather ? v0 : source, .rm = BUSTER_TIER0_ROLE_INTEGER(pointer), .index = indexed,
                                                  .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(scale == 1 ? 0 : scale == 2 ? 1 : scale == 4 ? 2 : 3), .decorations = 1 };
        }
        else
        {
            const uint32_t form = loads ? (expands ? expansions[size].forms[width] : check_x86_lower_move(e, width, false)) :
                                          (expands ? compressions[size].forms[width] : check_x86_lower_move(e, width, true));
            row->records[r] = check_x86_lower_memory(form, loads ? v0 : source, BUSTER_TIER0_ROLE_INTEGER(pointer));
            row->records[r++].decorations = (uint8_t)(1 | (loads && !passthru ? BUSTER_X86_DECORATION_ZEROING : 0));
        }
        row->record_count = (uint8_t)r;
        row->operand_count = (uint8_t)(addressed ? 3 + (scatter || passthru ? 1 : 0) : 2 + (passthru || !loads ? 1 : 0));
        row->operand_classes[0] = mask_class;
        row->operand_classes[pointer] = BUSTER_TYPED_CLASS_U64;
        if (addressed)
        {
            row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[1] = (uint8_t)indices.bytes;
        }
        if (!loads)
        {
            row->operand_classes[scatter ? 2 : 1] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[scatter ? 2 : 1] = (uint8_t)shape.bytes;
        }
        else if (passthru)
        {
            row->operand_classes[gather ? 3 : 2] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[gather ? 3 : 2] = (uint8_t)shape.bytes;
        }
        row->result_class = loads ? BUSTER_TYPED_CLASS_VECTOR : BUSTER_TYPED_CLASS_VOID;
        row->result_bytes = loads ? (uint8_t)shape.bytes : 0;
    }
    return reason;
}

// Whether __builtin_elementwise_clmul of `vector`, a vector of integers, is check_vector_elementwise_row's: its elements
// zero-extended to 64 bits take 64 bytes at most, and the target has VPCLMULQDQ and AVX-512VL (EVEX's forms at every
// width). Else the lowering takes its elements one by one (check_lower_vector_lanes).
static bool check_vector_clmul_packed_shape(const X86Target* target, CheckVectorShape shape)
{
    const uint64_t needed = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL) | BUSTER_X86_FEATURE(VPCLMULQDQ);
    return shape.integer && shape.element != 0 && shape.bytes * (8 / shape.element) <= 64 && (target->features & needed) == needed;
}

static bool check_vector_clmul_packed(const TypeTable* types, const X86Target* target, TypeId vector)
{
    return check_vector_clmul_packed_shape(target, check_vector_builtin_shape(types, vector));
}

// Reverse each element's bits in VECTOR(0), appending the constants to the row's existing arguments. Each element's
// bytes are reversed within its 16-byte chunk, then GFNI reverses each byte's bits. AVX512-BMM has a byte form of its
// own. The constants have at least 16 bytes so the matrix's whole quadword is present for tiny and narrow vectors.
// Narrow leading-zero counts reuse this row before the trailing-zero algorithm below.
static const char* check_vector_bitreverse_shape_row(const X86Target* target, CheckVectorShape shape, Tier0Operation* row, CheckVectorConstants* constants)
{
    const uint32_t width = check_vector_builtin_width(shape.bytes);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const char* reason = NULL;
    if (shape.element == 1 && (target->extensions & BUSTER_X86_EXTENSION(AVX512BMM)) != 0)
    {
        row->records[0] = check_vector_lower_record(BUSTER_CHECK_VECTOR_UNARY_PACKED(VBITREVB).forms[width], v0, 0, v0);
        row->record_count = 1;
    }
    else
    {
        const uint32_t constant_bytes = shape.bytes < 16 ? 16 : shape.bytes;
        uint32_t r = 0;
        if (shape.element > 1)
        {
            uint8_t places[16];
            for (uint32_t b = 0; b < 16; b += 1)
            {
                places[b] = (uint8_t)(b / shape.element * shape.element + (shape.element - 1 - b % shape.element));
            }
            static const CheckVectorForms shuffles = BUSTER_CHECK_VECTOR_EVEX(VPSHUFB);
            check_vector_constant_bytes(row, constants, places, 16, BUSTER_TYPED_CLASS_VECTOR, constant_bytes);
            row->records[r++] = check_vector_lower_record(shuffles.forms[width], v0, v0, BUSTER_TIER0_ROLE_VECTOR(row->operand_count - 1));
        }
        static const CheckVectorForms affines = BUSTER_CHECK_VECTOR_FORMS(VGF2P8AFFINEQB_X_X_XM128_IMM8_EVEX, VGF2P8AFFINEQB_Y_Y_YM256_IMM8_EVEX,
                                                                          VGF2P8AFFINEQB_Z_Z_ZM512_IMM8);
        check_vector_constant(row, constants, UINT64_C(0x8040201008040201), 8, BUSTER_TYPED_CLASS_VECTOR, constant_bytes);
        row->records[r++] = check_vector_lower_record(affines.forms[width], v0, v0, BUSTER_TIER0_ROLE_VECTOR(row->operand_count - 1));
        row->record_count = (uint8_t)r;
        reason = (target->features & BUSTER_X86_FEATURE(GFNI)) == 0 ? "vector bit reversal without GFNI" :
                 shape.element > 1 && (target->features & BUSTER_X86_FEATURE(AVX512BW)) == 0 ? "vector bit reversal of elements wider than a byte without AVX-512BW" :
                                                                                            NULL;
    }
    return reason;
}

// Whether __builtin_elementwise_min or _max of qwords (`builtin`, of `shape`) is two rows without SSE4.2's PCMPGTQ (#1834),
// past a row's 8 records, and if so `rows`: the mask of a < b (of b < a for a maximum) from the compare's row
// (check_vector_baseline_compare_qwords), then of the mask, a and b, b ^ ((a ^ b) & mask). Both rows read a and b, which
// their callers keep in memory to load twice.
static bool check_vector_baseline_extreme_pair(const X86Target* target, CBuiltinId builtin, CheckVectorShape shape, Tier0Operation* rows)
{
    const bool maximum = builtin == CBUILTIN_ELEMENTWISE_MAX;
    const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    bool pair = (maximum || builtin == CBUILTIN_ELEMENTWISE_MIN) && shape.integer && shape.element == 8 && (shape.bytes == 16 || shape.bytes == 32) &&
                (target->features & evex) != evex && (target->features & BUSTER_X86_FEATURE(SSE42)) == 0;
    if (pair)
    {
        const CheckVectorElement element = { .bytes = 8, .is_signed = shape.is_signed };
        pair = check_vector_lower_baseline_element(target, maximum ? CHECK_OP_GREATER : CHECK_OP_LESS, element, shape.bytes, 0, rows) == NULL;
        Tier0Operation* blend = rows + 1;
        memset(blend, 0, sizeof(*blend));
        blend->result_class = BUSTER_TYPED_CLASS_VECTOR;
        blend->result_bytes = (uint8_t)shape.bytes;
        blend->operand_count = 3;
        for (uint32_t j = 0; j < 3; j += 1)
        {
            blend->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
            blend->operand_bytes[j] = (uint8_t)shape.bytes;
        }
        const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
        const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
        const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
        CheckVectorBaseline context = { blend, shape.bytes, (target->features & BUSTER_X86_FEATURE(AVX)) != 0, true };
        check_vector_baseline_binary(&context, BUSTER_CHECK_BASE_PACKED(&context, XORPS), v1, v1, v2);
        check_vector_baseline_binary(&context, BUSTER_CHECK_BASE_PACKED(&context, ANDPS), v0, v0, v1);
        check_vector_baseline_binary(&context, BUSTER_CHECK_BASE_PACKED(&context, XORPS), v0, v0, v2);
        Tier0Operation baseline;
        pair = pair && context.valid && check_vector_baseline_row(target, blend, &baseline);
    }
    return pair;
}

// The baseline's row of __builtin_elementwise_min, _max or _abs of integers whose EVEX row has no SSE or AVX form (#1834)
// (check_vector_baseline_extremes, check_vector_baseline_absolute): its operands the EVEX row's, and an unsigned
// compare's bias of the elements' sign bits its next (`constants`), the records rewritten. NULL or the reason.
static const char* check_vector_baseline_elementwise(const X86Target* target, CBuiltinId builtin, CheckVectorShape shape, Tier0Operation* row,
                                                     CheckVectorConstants* constants)
{
    const bool maximum = builtin == CBUILTIN_ELEMENTWISE_MAX;
    const bool extreme = maximum || builtin == CBUILTIN_ELEMENTWISE_MIN;
    const uint32_t e = shape.element;
    const char* reason = NULL;
    row->record_count = 0;
    CheckVectorBaseline context = { row, shape.bytes, (target->features & BUSTER_X86_FEATURE(AVX)) != 0, true };
    if (!shape.integer || (!extreme && builtin != CBUILTIN_ELEMENTWISE_ABS) || (e != 1 && e != 2 && e != 4 && e != 8))
    {
        reason = "elementwise vector builtins unavailable at this target";
    }
    else if (extreme && e == 8 && (target->features & BUSTER_X86_FEATURE(SSE42)) == 0)
    {
        reason = "vector minimum and maximum of 64-bit elements without SSE4.2";
    }
    else if (extreme)
    {
        uint8_t bias = 0;
        if (!shape.is_signed && e != 2)
        {
            check_vector_constant(row, constants, 1ull << (8 * e - 1), e, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            context.valid = row->operand_count == 3;
            bias = BUSTER_TIER0_ROLE_VECTOR(2);
        }
        check_vector_baseline_extremes(&context, maximum, e, shape.is_signed, bias);
    }
    else
    {
        check_vector_baseline_absolute(&context, e);
    }
    Tier0Operation baseline;
    if (reason == NULL && !(context.valid && check_vector_baseline_row(target, row, &baseline)))
    {
        reason = extreme ? "vector minimum and maximum unavailable at this target" : "vector absolute values unavailable at this target";
    }
    return reason;
}

// The row of a __builtin_elementwise_* function at a vector type, of `arguments` operands (all of the type, which its
// result is too): of integers, max and min, abs, saturating addition and subtraction, population counts, leading and
// trailing zero counts (clzg's and ctzg's second operands the elements a zero element takes) and funnel shifts; of
// half-precision elements (with AVX512-FP16), floats and doubles, the square roots and the fused multiply-adds at the
// current rounding, as llvm.sqrt's and llvm.fma's are (VSQRTPH/PS/PD of a, VFMADD213PH/PS/PD of b, a and c: b = a * b +
// c, moved to VECTOR(0), as clang's code has b in ModRM.reg, so that of two NaNs b's is the result); of bf16 elements
// (with AVX10.2), VSQRTBF16 of a, and the fused multiply-add of b, a and c (b = a * b + c) the same way, whose operands `negated` has the bit of (the lowering
// takes their negations' operands: check_lower_brain_negations) negate the product (a's or b's alone) or the sum (c's) as
// the form does, as clang's instruction selection folds the negations: VFMADD213BF16, VFMSUB213BF16, VFNMADD213BF16 or
// VFNMSUB213BF16. `constants` gets any extra constant operands.
static const char* check_vector_elementwise_shape_row(const X86Target* target, CBuiltinId builtin, CheckVectorShape shape, uint32_t arguments, uint32_t negated,
                                                      Tier0Operation* row, CheckVectorConstants* constants)
{
    const uint32_t width = check_vector_builtin_width(shape.bytes);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const char* reason = NULL;
    uint64_t needed = 0;
    *constants = (CheckVectorConstants){ 0 };
    memset(row, 0, sizeof(*row));
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)shape.bytes;
    row->operand_count = (uint8_t)arguments;
    for (uint32_t j = 0; j < arguments && j < 3; j += 1)
    {
        row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[j] = (uint8_t)shape.bytes;
    }
    if (shape.floating && !shape.bfloat && shape.element >= 2 && (builtin == CBUILTIN_ELEMENTWISE_SQRT || builtin == CBUILTIN_ELEMENTWISE_FMA))
    {
        static const CheckVectorForms roots[3] = { BUSTER_CHECK_VECTOR_UNARY_PACKED(VSQRTPH), BUSTER_CHECK_VECTOR_UNARY_EVEX(VSQRTPS),
                                                   BUSTER_CHECK_VECTOR_UNARY_EVEX(VSQRTPD) };
        static const CheckVectorForms products[3] = { BUSTER_CHECK_VECTOR_PACKED(VFMADD213PH), BUSTER_CHECK_VECTOR_EVEX(VFMADD213PS),
                                                      BUSTER_CHECK_VECTOR_EVEX(VFMADD213PD) };
        const uint32_t d = shape.element == 2 ? 0 : shape.element == 4 ? 1 : 2;
        // The product's destination is b, as clang's code has it (so that of two NaNs, b's is the result's), then moved to
        // VECTOR(0).
        row->records[0] = builtin == CBUILTIN_ELEMENTWISE_SQRT ? check_vector_lower_record(roots[d].forms[width], v0, 0, v0) :
                                                                  check_vector_lower_record(products[d].forms[width], v1, v0, v2);
        row->records[1] = check_x86_lower_move_result(shape.bytes, v1);
        row->record_count = builtin == CBUILTIN_ELEMENTWISE_SQRT ? 1 : 2;
        reason = shape.element == 2 && (target->extensions & BUSTER_X86_EXTENSION(AVX512FP16)) == 0 ?
                     "elementwise vector builtins of half-precision elements without AVX512-FP16" :
                     NULL;
    }
    else if (shape.bfloat && (builtin == CBUILTIN_ELEMENTWISE_SQRT || builtin == CBUILTIN_ELEMENTWISE_FMA))
    {
        static const CheckVectorForms products[4] = { BUSTER_CHECK_VECTOR_PACKED(VFMADD213BF16), BUSTER_CHECK_VECTOR_PACKED(VFMSUB213BF16),
                                                      BUSTER_CHECK_VECTOR_PACKED(VFNMADD213BF16), BUSTER_CHECK_VECTOR_PACKED(VFNMSUB213BF16) };
        // 1 for a negated sum, 2 for a negated product.
        const uint32_t sign = (negated >> 2 & 1) | ((negated ^ negated >> 1) & 1) << 1;
        if (builtin == CBUILTIN_ELEMENTWISE_SQRT)
        {
            row->records[0] = check_vector_lower_record(BUSTER_CHECK_VECTOR_UNARY_PACKED(VSQRTBF16).forms[width], v0, 0, v0);
            row->record_count = 1;
        }
        else
        {
            row->records[0] = check_vector_lower_record(products[sign].forms[width], v1, v0, v2);
            row->records[1] = check_x86_lower_move_result(shape.bytes, v1);
            row->record_count = 2;
        }
        reason = (target->extensions & BUSTER_X86_EXTENSION(AVX10_2)) == 0 ? "elementwise vector builtins of bf16 elements without AVX10.2" : NULL;
    }
    else if (shape.bfloat)
    {
        reason = "elementwise vector builtins of bf16 elements";
    }
    else if (!shape.integer)
    {
        reason = "elementwise vector builtins of floating elements";
    }
    else if (shape.element == 0)
    {
        reason = "elementwise vector builtins of elements of this size";
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_BITREVERSE)
    {
        reason = check_vector_bitreverse_shape_row(target, shape, row, constants);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_CLMUL)
    {
        // Each element zero-extended to 64 bits (VPMOVZX, where it is narrower), the products of each 16 bytes' first
        // quadwords and of their second ones (VPCLMULQDQ by 0x00 and 0x11), their low quadwords put back into their
        // places (VSHUFPD by 0, which takes each 16 bytes' first quadword of each, as clang's VPUNPCKLQDQ does: EVEX's
        // VPUNPCKLQDQ is no form x86_semantics.c covers), then truncated to the elements (VPMOV): an element's
        // product's low bits are the product's of its operands' bits alone, as llvm.clmul's are, which clang's code
        // computes the same way for 64-bit elements. Wider extensions than 64 bytes are check_vector_clmul_packed's to
        // refuse: the lowering takes them element by element (check_lower_vector_lanes).
        static const CheckVectorForms extend[3] = { BUSTER_CHECK_VECTOR_FORMS(VPMOVZXBQ_X_XM16_EVEX, VPMOVZXBQ_Y_XM32_EVEX, VPMOVZXBQ_Z_XM64),
                                                    BUSTER_CHECK_VECTOR_FORMS(VPMOVZXWQ_X_XM32_EVEX, VPMOVZXWQ_Y_XM64_EVEX, VPMOVZXWQ_Z_XM128),
                                                    BUSTER_CHECK_VECTOR_FORMS(VPMOVZXDQ_X_XM64_EVEX, VPMOVZXDQ_Y_XM128_EVEX, VPMOVZXDQ_Z_YM256) };
        // The form's rm is its destination.
        static const CheckVectorForms truncate[3] = { BUSTER_CHECK_VECTOR_FORMS(VPMOVQB_XM16_X, VPMOVQB_XM32_Y, VPMOVQB_XM64_Z),
                                                      BUSTER_CHECK_VECTOR_FORMS(VPMOVQW_XM32_X, VPMOVQW_XM64_Y, VPMOVQW_XM128_Z),
                                                      BUSTER_CHECK_VECTOR_FORMS(VPMOVQD_XM64_X, VPMOVQD_XM128_Y, VPMOVQD_YM256_Z) };
        static const CheckVectorForms products = BUSTER_CHECK_VECTOR_FORMS(VPCLMULQDQ_X_X_XM128_IMM8_EVEX, VPCLMULQDQ_Y_Y_YM256_IMM8_EVEX,
                                                                           VPCLMULQDQ_Z_Z_ZM512_IMM8);
        static const CheckVectorForms interleaves = BUSTER_CHECK_VECTOR_FORMS(VSHUFPD_X_X_XM128_IMM8_EVEX, VSHUFPD_Y_Y_YM256_IMM8_EVEX, VSHUFPD_Z_Z_ZM512_IMM8);
        const uint32_t e = shape.element == 1 ? 0 : shape.element == 2 ? 1 : 2;
        const uint32_t wide = check_vector_builtin_width(shape.bytes * (8 / shape.element));
        uint32_t r = 0;
        if (shape.element < 8)
        {
            row->records[r++] = check_vector_lower_record(extend[e].forms[wide], v0, 0, v0);
            row->records[r++] = check_vector_lower_record(extend[e].forms[wide], v1, 0, v1);
        }
        row->records[r++] = check_vector_lower_record(products.forms[wide], v2, v0, v1);
        row->records[r] = check_vector_lower_record(products.forms[wide], v0, v0, v1);
        row->records[r++].immediate = 0x11;
        row->records[r++] = check_vector_lower_record(interleaves.forms[wide], v0, v2, v0);
        if (shape.element < 8)
        {
            row->records[r++] = check_vector_lower_record(truncate[e].forms[wide], v0, 0, v0);
        }
        row->record_count = (uint8_t)r;
        reason = check_vector_clmul_packed_shape(target, shape) ? NULL : "vector carry-less multiplication without AVX-512VL and VPCLMULQDQ";
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_CTZG || (builtin == CBUILTIN_ELEMENTWISE_CLZG && shape.element <= 2))
    {
        // popcount(~x & (x - 1)), as clang's code computes llvm.cttz: each element's trailing zeros, its width for a zero
        // one; with a second operand, its element where the first's is zero (k1, from VPTESTNM, before the count overwrites
        // the first operand), as clzg's select is.
        const bool leading = builtin == CBUILTIN_ELEMENTWISE_CLZG;
        const uint32_t e = check_vector_builtin_element(shape.element);
        // A leading count adds up to three constants past the optional fallback, so its scratch is VECTOR(5).
        const uint8_t temporary = leading ? BUSTER_TIER0_ROLE_VECTOR(5) : v2;
        if (leading)
        {
            reason = check_vector_bitreverse_shape_row(target, shape, row, constants);
        }
        static const CheckVectorForms adds[4] = { BUSTER_CHECK_VECTOR_EVEX(VPADDB), BUSTER_CHECK_VECTOR_EVEX(VPADDW), BUSTER_CHECK_VECTOR_EVEX(VPADDD),
                                                  BUSTER_CHECK_VECTOR_EVEX(VPADDQ) };
        static const CheckVectorForms counts[4] = { BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTB), BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTW),
                                                    BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTD), BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTQ) };
        // Zero elements into k1: of 32- and 64-bit ones, VPTESTNM's, then VPBLENDM's select; of 8- and 16-bit ones, VPCMPEQ
        // with zeros' (VPXORD) and a masked VMOVDQU8 or VMOVDQU16, forms x86_semantics.c covers.
        static const CheckVectorForms tests[4] = {
            BUSTER_CHECK_VECTOR_FORMS(VPCMPEQB_K_X_XM128, VPCMPEQB_K_Y_YM256, VPCMPEQB_K_Z_ZM512),
            BUSTER_CHECK_VECTOR_FORMS(VPCMPEQW_K_X_XM128, VPCMPEQW_K_Y_YM256, VPCMPEQW_K_Z_ZM512),
            BUSTER_CHECK_VECTOR_FORMS(VPTESTNMD_K_X_XM128, VPTESTNMD_K_Y_YM256, VPTESTNMD_K_Z_ZM512),
            BUSTER_CHECK_VECTOR_FORMS(VPTESTNMQ_K_X_XM128, VPTESTNMQ_K_Y_YM256, VPTESTNMQ_K_Z_ZM512),
        };
        static const CheckVectorForms blends[4] = { BUSTER_CHECK_VECTOR_UNARY_PACKED(VMOVDQU8), BUSTER_CHECK_VECTOR_UNARY_PACKED(VMOVDQU16),
                                                    BUSTER_CHECK_VECTOR_PACKED(VPBLENDMD), BUSTER_CHECK_VECTOR_PACKED(VPBLENDMQ) };
        static const CheckVectorForms ternary = BUSTER_CHECK_VECTOR_FORMS(VPTERNLOGD_X_X_XM128_IMM8, VPTERNLOGD_Y_Y_YM256_IMM8, VPTERNLOGD_Z_Z_ZM512_IMM8);
        static const CheckVectorForms zeros = BUSTER_CHECK_VECTOR_PACKED(VPXORD);
        uint32_t r = row->record_count;
        if (arguments == 2 && shape.element <= 2 && !leading)
        {
            row->records[r++] = check_vector_lower_record(zeros.forms[width], temporary, temporary, temporary);
            row->records[r++] = check_vector_lower_record(tests[e].forms[width], 1, v0, temporary);
        }
        else if (arguments == 2 && !leading)
        {
            row->records[r++] = check_vector_lower_record(tests[e].forms[width], 1, v0, v0);
        }
        // All ones (any inputs' every truth table entry 1), then x - 1, then that and not x (A & ~B, A the destination
        // and B x: 0x30), then its count into v0.
        row->records[r] = check_vector_lower_record(ternary.forms[width], temporary, temporary, temporary);
        row->records[r++].immediate = 0xff;
        row->records[r++] = check_vector_lower_record(adds[e].forms[width], temporary, v0, temporary);
        row->records[r] = check_vector_lower_record(ternary.forms[width], temporary, v0, v0);
        row->records[r++].immediate = 0x30;
        row->records[r++] = check_vector_lower_record(counts[e].forms[width], v0, 0, temporary);
        if (arguments == 2 && leading)
        {
            // The count equals the element's width only for a zero input. Comparing it with a constant saves the
            // separate zeroing instruction before the count, keeping a word's reversal and fallback in eight records.
            check_vector_constant(row, constants, shape.element * 8, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            row->records[r++] = check_vector_lower_record(tests[e].forms[width], 1, v0, BUSTER_TIER0_ROLE_VECTOR(row->operand_count - 1));
        }
        if (arguments == 2)
        {
            // VMOVDQU8's and VMOVDQU16's source is in rm, VPBLENDM's second in rm too (k1's elements from it).
            row->records[r] = shape.element <= 2 ? check_vector_lower_record(blends[e].forms[width], v0, 0, v1) :
                                                   check_vector_lower_record(blends[e].forms[width], v0, v0, v1);
            row->records[r++].decorations = 1;
        }
        row->record_count = (uint8_t)r;
        needed = shape.element <= 2 ? BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512BITALG) : BUSTER_X86_FEATURE(AVX512VPOPCNTDQ);
        if (reason == NULL && (target->features & needed) != needed)
        {
            reason = leading ? "vector leading zero counts of 8- and 16-bit elements without AVX-512 BITALG" :
                     shape.element <= 2 ? "vector trailing zero counts of 8- and 16-bit elements without AVX-512 BITALG" :
                                          "vector trailing zero counts of 32- and 64-bit elements without AVX-512 VPOPCNTDQ";
        }
    }
    else if (shape.element == 1 && (builtin == CBUILTIN_ELEMENTWISE_FSHL || builtin == CBUILTIN_ELEMENTWISE_FSHR))
    {
        if (shape.bytes != 16)
        {
            reason = "vector funnel shifts of 8-bit elements outside 16-byte XOP vectors";
        }
        else if ((target->extensions & BUSTER_X86_EXTENSION(XOP)) == 0)
        {
            reason = "vector funnel shifts of 8-bit elements without XOP";
        }
        else
        {
            const bool left = builtin == CBUILTIN_ELEMENTWISE_FSHL;
            const uint8_t mask = BUSTER_TIER0_ROLE_VECTOR(3);
            const uint8_t zero = BUSTER_TIER0_ROLE_VECTOR(4);
            const uint8_t ones = BUSTER_TIER0_ROLE_VECTOR(5);
            const uint8_t temporary = BUSTER_TIER0_ROLE_VECTOR(6);
            check_vector_constant(row, constants, 7, 1, BUSTER_TYPED_CLASS_VECTOR, 16);
            check_vector_constant(row, constants, 0, 1, BUSTER_TYPED_CLASS_VECTOR, 16);
            if (left)
            {
                check_vector_constant(row, constants, 255, 1, BUSTER_TYPED_CLASS_VECTOR, 16);
            }
            // VPSHLB uses a signed count: positive shifts left, negative shifts right. With s = c & 7, first
            // b >> 1 >> (7-s), then a << s for fshl; a << 1 << (7-s), then b >> s for fshr. Splitting the
            // complementary shift makes it eight at s == 0, which discards that half instead of rotating it.
            if (left)
            {
                row->records[0] = check_vector_lower_record(X86_FORM_VPSHLB_X_XM128_X, v1, ones, v1);
                row->records[1] = check_vector_lower_record(X86_FORM_VPANDN_X_X_XM128, temporary, v2, mask);
                row->records[2] = check_vector_lower_record(X86_FORM_VPSUBB_X_X_XM128, temporary, zero, temporary);
                row->records[3] = check_vector_lower_record(X86_FORM_VPSHLB_X_XM128_X, v1, temporary, v1);
                row->records[4] = check_vector_lower_record(X86_FORM_VPAND_X_X_XM128, v2, v2, mask);
                row->records[5] = check_vector_lower_record(X86_FORM_VPSHLB_X_XM128_X, v0, v2, v0);
            }
            else
            {
                row->records[0] = check_vector_lower_record(X86_FORM_VPANDN_X_X_XM128, temporary, v2, mask);
                row->records[1] = check_vector_lower_record(X86_FORM_VPADDB_X_X_XM128, v0, v0, v0);
                row->records[2] = check_vector_lower_record(X86_FORM_VPSHLB_X_XM128_X, v0, temporary, v0);
                row->records[3] = check_vector_lower_record(X86_FORM_VPAND_X_X_XM128, v2, v2, mask);
                row->records[4] = check_vector_lower_record(X86_FORM_VPSUBB_X_X_XM128, v2, zero, v2);
                row->records[5] = check_vector_lower_record(X86_FORM_VPSHLB_X_XM128_X, v1, v2, v1);
            }
            row->records[6] = check_vector_lower_record(X86_FORM_VPOR_X_X_XM128, v0, v0, v1);
            row->record_count = 7;
        }
    }
    else
    {
        const CheckVectorForms forms = check_vector_elementwise_forms(builtin, shape, &needed);
        const uint32_t form = forms.forms[width];
        if (form == 0)
        {
            reason = builtin == CBUILTIN_ELEMENTWISE_ADD_SAT || builtin == CBUILTIN_ELEMENTWISE_SUB_SAT ? "saturating vector arithmetic of 32- and 64-bit elements" :
                     builtin == CBUILTIN_ELEMENTWISE_CLZG ? "vector leading zero counts of 8- and 16-bit elements" :
                     builtin == CBUILTIN_ELEMENTWISE_FSHL || builtin == CBUILTIN_ELEMENTWISE_FSHR ? "vector funnel shifts of 8-bit elements" :
                                                                                                    "this elementwise vector builtin";
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_ABS || builtin == CBUILTIN_ELEMENTWISE_POPCOUNT || (builtin == CBUILTIN_ELEMENTWISE_CLZG && arguments == 1))
        {
            row->records[0] = check_vector_lower_record(form, v0, 0, v0);
            row->record_count = 1;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_CLZG)
        {
            // The count into v2, then the second operand's element where the first's is zero (k1, from VPTESTNM): clang's
            // select of ctlz's result by the element's compare with zero.
            const bool wide = shape.element == 8;
            const CheckVectorForms tests = wide ? BUSTER_CHECK_VECTOR_FORMS(VPTESTNMQ_K_X_XM128, VPTESTNMQ_K_Y_YM256, VPTESTNMQ_K_Z_ZM512) :
                                                  BUSTER_CHECK_VECTOR_FORMS(VPTESTNMD_K_X_XM128, VPTESTNMD_K_Y_YM256, VPTESTNMD_K_Z_ZM512);
            const CheckVectorForms blends = wide ? BUSTER_CHECK_VECTOR_FORMS(VPBLENDMQ_X_X_XM128, VPBLENDMQ_Y_Y_YM256, VPBLENDMQ_Z_Z_ZM512) :
                                                   BUSTER_CHECK_VECTOR_FORMS(VPBLENDMD_X_X_XM128, VPBLENDMD_Y_Y_YM256, VPBLENDMD_Z_Z_ZM512);
            row->records[0] = check_vector_lower_record(form, v2, 0, v0);
            row->records[1] = check_vector_lower_record(tests.forms[width], 1, v0, v0);
            row->records[2] = check_vector_lower_record(blends.forms[width], v0, v2, v1);
            row->records[2].decorations = 1;
            row->record_count = 3;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_FSHL)
        {
            // The high halves of a:b shifted left: VPSHLDV's destination is a, the high one.
            row->records[0] = check_vector_lower_record(form, v0, v1, v2);
            row->record_count = 1;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_FSHR)
        {
            // The low halves of a:b shifted right: VPSHRDV's destination is b, the low one, which then goes to v0.
            row->records[0] = check_vector_lower_record(form, v1, v0, v2);
            row->records[1] = check_x86_lower_move_result(shape.bytes, v1);
            row->record_count = 2;
        }
        else
        {
            row->records[0] = check_vector_lower_record(form, v0, v0, v1);
            row->record_count = 1;
        }
        const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
        Tier0Operation baseline;
        if (reason == NULL && (target->features & evex) != evex)
        {
            // Without AVX-512, SSE's or AVX's form of the operation (#775), which the lowering takes in the EVEX one's
            // place (check_lower_operation_index), or its refusal by name.
            const bool extreme = builtin == CBUILTIN_ELEMENTWISE_MAX || builtin == CBUILTIN_ELEMENTWISE_MIN;
            reason = check_vector_baseline_row(target, row, &baseline) ? NULL :
                     extreme || builtin == CBUILTIN_ELEMENTWISE_ABS ? check_vector_baseline_elementwise(target, builtin, shape, row, constants) :
                     (target->features & needed) != needed ? check_vector_builtin_feature_reason(builtin, needed, target->features) :
                                                             "elementwise vector builtins unavailable at this target";
        }
        else if (reason == NULL && (target->features & needed) != needed)
        {
            reason = check_vector_builtin_feature_reason(builtin, needed, target->features);
        }
    }
    return reason;
}

static const char* check_vector_elementwise_row(const TypeTable* types, const X86Target* target, CBuiltinId builtin, TypeId vector, uint32_t arguments, uint32_t negated,
                                                Tier0Operation* row, CheckVectorConstants* constants)
{
    return check_vector_elementwise_shape_row(target, builtin, check_vector_builtin_shape(types, vector), arguments, negated, row, constants);
}

// The row of __builtin_elementwise_sqrt or _fma of float or double scalars (`class`, F32 or F64), of `arguments`
// operands of the class: VSQRTSS/SD of the operand, or VFMADD213SS/SD of b, a and c (b = a * b + c, b's register the
// destination as clang's code has it, so that of two NaNs b's is the result), moved into the first operand's register,
// at the current rounding, as llvm.sqrt and llvm.fma compute them. Their EVEX forms are AVX-512F's,
// which a target may lack though the operands are no vectors.
static const char* check_vector_scalar_row(const X86Target* target, CBuiltinId builtin, uint8_t class, uint32_t arguments, Tier0Operation* row)
{
    const bool root = builtin == CBUILTIN_ELEMENTWISE_SQRT && arguments == 1;
    const bool product = builtin == CBUILTIN_ELEMENTWISE_FMA && arguments == 3;
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    if (!root && !product)
    {
        reason = "elementwise builtins of scalars";
    }
    else if ((target->features & BUSTER_X86_FEATURE(AVX512F)) == 0)
    {
        // Without AVX-512F, SQRTSS's or SQRTSD's (VSQRTSS's with AVX: #775), and FMA's VFMADD213SS or SD.
        const bool d = class == BUSTER_TYPED_CLASS_F64;
        const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
        const uint8_t f0 = BUSTER_TIER0_ROLE_FLOATING(0);
        const uint8_t result = BUSTER_TIER0_ROLE_FLOATING_RESULT;
        if (root)
        {
            row->records[0] = avx ? check_vector_lower_record(d ? X86_FORM_VSQRTSD_X_X_XM64 : X86_FORM_VSQRTSS_X_X_XM32, result, f0, f0) :
                                    check_vector_lower_record(d ? X86_FORM_SQRTSD_X_XM64 : X86_FORM_SQRTSS_X_XM32, result, 0, f0);
        }
        else
        {
            // b's register is the product's destination, as clang's code has it (of two NaNs, b's is the result's).
            row->records[0] = check_vector_lower_record(d ? X86_FORM_VFMADD213SD_X_X_XM64 : X86_FORM_VFMADD213SS_X_X_XM32, BUSTER_TIER0_ROLE_FLOATING(1),
                                                        BUSTER_TIER0_ROLE_FLOATING(0), BUSTER_TIER0_ROLE_FLOATING(2));
            row->records[1] = check_vector_lower_record(X86_FORM_VMOVAPS_X_XM128, result, 0, BUSTER_TIER0_ROLE_FLOATING(1));
            reason = (target->features & BUSTER_X86_FEATURE(FMA)) == 0 ? "fused multiply-adds of scalars without FMA or AVX-512F" : NULL;
        }
        row->record_count = root ? 1 : 2;
        row->operand_count = (uint8_t)arguments;
        for (uint32_t j = 0; j < arguments; j += 1)
        {
            row->operand_classes[j] = class;
        }
        row->result_class = class;
    }
    else
    {
        const bool d = class == BUSTER_TYPED_CLASS_F64;
        const uint8_t f0 = BUSTER_TIER0_ROLE_FLOATING(0);
        row->records[0] = root ? check_vector_lower_record(d ? X86_FORM_VSQRTSD_X_X_XM64_EVEX : X86_FORM_VSQRTSS_X_X_XM32_EVEX, BUSTER_TIER0_ROLE_FLOATING_RESULT,
                                                           f0, f0) :
                                 check_vector_lower_record(d ? X86_FORM_VFMADD213SD_X_X_XM64_EVEX : X86_FORM_VFMADD213SS_X_X_XM32_EVEX,
                                                           BUSTER_TIER0_ROLE_FLOATING(1), f0, BUSTER_TIER0_ROLE_FLOATING(2));
        row->records[1] = check_vector_lower_record(X86_FORM_VMOVAPS_X_XM128_EVEX, BUSTER_TIER0_ROLE_FLOATING_RESULT, 0, BUSTER_TIER0_ROLE_FLOATING(1));
        row->record_count = root ? 1 : 2;
        row->operand_count = (uint8_t)arguments;
        for (uint32_t j = 0; j < arguments; j += 1)
        {
            row->operand_classes[j] = class;
        }
        row->result_class = class;
    }
    return reason;
}

// A source expression's contracted product (#1239), keeping its operands in evaluation order: a, b, c for a * b + c,
// or c, a, b for c + a * b. The product's second operand stays the destructive destination in either order, as
// clang -O0's fmuladd does (and therefore keeps the same NaN priority). Negations are explicit typed operands; using
// fmsub or fnmadd instead would change the sign of a NaN. The result still replaces the first stack operand.
static const char* check_vector_contract_row(const X86Target* target, uint8_t class, bool product_right, Tier0Operation* row)
{
    const char* reason = check_vector_scalar_row(target, CBUILTIN_ELEMENTWISE_FMA, class, 3, row);
    if (reason == NULL && (target->features & BUSTER_X86_FEATURE(AVX512VL)) == 0)
    {
        // Scalar EVEX FMA needs AVX512F alone, but the 128-bit copy needs VL. Its VEX twin keeps this row legal when
        // the function's target removes VL; the mixed row's floating roles are placed below xmm16 by both tiers.
        row->records[1].form = X86_FORM_VMOVAPS_X_XM128;
    }
    if (reason == NULL && product_right)
    {
        row->records[0] = check_vector_lower_record(row->records[0].form, BUSTER_TIER0_ROLE_FLOATING(2), BUSTER_TIER0_ROLE_FLOATING(1), BUSTER_TIER0_ROLE_FLOATING(0));
        row->records[1] = check_vector_lower_record(row->records[1].form, BUSTER_TIER0_ROLE_FLOATING_RESULT, 0, BUSTER_TIER0_ROLE_FLOATING(2));
    }
    return reason;
}

// Whether a floating elementwise builtin is computed by instructions (check_vector_floating_row and its scalar twin),
// rather than by the C library's functions an element at a time (sin, pow and the rest: check_lower_vector_calls).
static bool check_vector_floating_instructions(CBuiltinId builtin)
{
    const bool result = (builtin >= CBUILTIN_ELEMENTWISE_CEIL && builtin <= CBUILTIN_ELEMENTWISE_CANONICALIZE) ||
                        (builtin >= CBUILTIN_ELEMENTWISE_COPYSIGN && builtin <= CBUILTIN_ELEMENTWISE_MINIMUMNUM);
    return result;
}

// The same, of a builtin of a value whose elements are `floating` (or which is a floating scalar): its abs too, which is
// llvm.fabs then (#1834), its value's magnitude by a mask, which the rows above compute before any rounding (its
// identifier precedes theirs).
static bool check_vector_floating_builtin(CBuiltinId builtin, bool floating)
{
    return check_vector_floating_instructions(builtin) || (floating && builtin == CBUILTIN_ELEMENTWISE_ABS);
}

// The builtin clang's code computes a floating elementwise builtin as: max's and min's of floating values are
// llvm.maxnum and minnum (#1834), as __builtin_elementwise_maxnum and minnum are; every other builtin is itself.
static CBuiltinId check_vector_floating_alias(CBuiltinId builtin, bool floating)
{
    return floating && builtin == CBUILTIN_ELEMENTWISE_MAX ? CBUILTIN_ELEMENTWISE_MAXNUM :
           floating && builtin == CBUILTIN_ELEMENTWISE_MIN ? CBUILTIN_ELEMENTWISE_MINNUM :
                                                             builtin;
}

// The rounding immediate of VRNDSCALE for a rounding builtin, as llvm.floor ... llvm.roundeven are: bits 1 and 0 the
// rounding mode (to nearest even, down, up, toward zero), bit 2 MXCSR's mode instead, bit 3 no precision exception:
// floor 9, ceil 10, rint 4, nearbyint 12, roundeven 8, and trunc 11, which round's truncation is too.
static uint8_t check_vector_rounding(CBuiltinId builtin)
{
    const uint8_t result = builtin == CBUILTIN_ELEMENTWISE_FLOOR ? 9 : builtin == CBUILTIN_ELEMENTWISE_CEIL ? 10 : builtin == CBUILTIN_ELEMENTWISE_RINT ? 4 :
                           builtin == CBUILTIN_ELEMENTWISE_NEARBYINT ? 12 : builtin == CBUILTIN_ELEMENTWISE_ROUNDEVEN ? 8 : 11;
    return result;
}

// The floating constants of the rows below, as a float's (`d` false) or a double's bits: the sign mask, the magnitude
// mask (all but the sign), the largest value below 0.5 (llvm.round's) and 1.0.
static uint64_t check_vector_floating_sign(bool d)
{
    return d ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000);
}

static uint64_t check_vector_floating_half(bool d)
{
    return d ? UINT64_C(0x3fdfffffffffffff) : UINT64_C(0x3effffff);
}

static uint64_t check_vector_floating_one(bool d)
{
    return d ? UINT64_C(0x3ff0000000000000) : UINT64_C(0x3f800000);
}

// The floating elementwise builtins clang -O0's code computes by instructions (check_vector_floating_instructions), of a
// vector of floats or doubles, as clang's sequences compute them, operand for operand, so that NaN payloads and zeros'
// signs are clang's: VRNDSCALE for the roundings (check_vector_rounding's immediates); llvm.round's x + copysign(0.5's
// predecessor, x), truncated (VPTERNLOGD 248 of the predecessor, x and the sign mask, VADDP, VRNDSCALE 11);
// canonicalize's x * 1.0 (VMULP); copysign's bits of x's magnitude and y's sign (VPTERNLOGD 226 by the magnitude mask);
// maxnum and minnum, y > x ? y : x (VMAXP or VMINP of y and x), y where x is a NaN (a masked move by VCMPP's unordered
// compare, made first, before the result overwrites x); maximum and minimum, x > y ? x : y with its sign bit cleared
// where x's is clear (maximum's VPTERNLOGD 224 with the magnitude mask) or set where x's is (minimum's VPTERNLOGD 248
// with the sign mask), and x where x is a NaN (the ordered compare's mask moving the rest); maximumnum and minimumnum,
// the same, but y where x is a NaN and x where y is. The constants are operands past the arguments (`constants`).
static const char* check_vector_floating_shape_row(CBuiltinId builtin, CheckVectorShape shape, Tier0Operation* row, CheckVectorConstants* constants)
{
    const uint32_t width = check_vector_builtin_width(shape.bytes);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
    const bool binary = builtin >= CBUILTIN_ELEMENTWISE_COPYSIGN;
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    *constants = (CheckVectorConstants){ 0 };
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)shape.bytes;
    row->operand_count = binary ? 2 : 1;
    for (uint32_t j = 0; j < row->operand_count; j += 1)
    {
        row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[j] = (uint8_t)shape.bytes;
    }
    if (!shape.floating || shape.bfloat || shape.element == 2)
    {
        reason = "elementwise vector builtins of half-precision or bf16 elements";
    }
    else
    {
        const bool d = shape.element == 8;
        const uint64_t sign = check_vector_floating_sign(d);
        static const CheckVectorForms roundings[2] = { BUSTER_CHECK_VECTOR_FORMS(VRNDSCALEPS_X_XM128_IMM8, VRNDSCALEPS_Y_YM256_IMM8, VRNDSCALEPS_Z_ZM512_IMM8),
                                                       BUSTER_CHECK_VECTOR_FORMS(VRNDSCALEPD_X_XM128_IMM8, VRNDSCALEPD_Y_YM256_IMM8, VRNDSCALEPD_Z_ZM512_IMM8) };
        static const CheckVectorForms maxima[2] = { BUSTER_CHECK_VECTOR_EVEX(VMAXPS), BUSTER_CHECK_VECTOR_EVEX(VMAXPD) };
        static const CheckVectorForms minima[2] = { BUSTER_CHECK_VECTOR_EVEX(VMINPS), BUSTER_CHECK_VECTOR_EVEX(VMINPD) };
        static const CheckVectorForms compares[2] = { BUSTER_CHECK_VECTOR_FORMS(VCMPPS_K_X_XM128_IMM8, VCMPPS_K_Y_YM256_IMM8, VCMPPS_K_Z_ZM512_IMM8),
                                                      BUSTER_CHECK_VECTOR_FORMS(VCMPPD_K_X_XM128_IMM8, VCMPPD_K_Y_YM256_IMM8, VCMPPD_K_Z_ZM512_IMM8) };
        static const CheckVectorForms moves[2] = { BUSTER_CHECK_VECTOR_UNARY_EVEX(VMOVAPS), BUSTER_CHECK_VECTOR_UNARY_EVEX(VMOVAPD) };
        static const CheckVectorForms adds[2] = { BUSTER_CHECK_VECTOR_EVEX(VADDPS), BUSTER_CHECK_VECTOR_EVEX(VADDPD) };
        static const CheckVectorForms products[2] = { BUSTER_CHECK_VECTOR_EVEX(VMULPS), BUSTER_CHECK_VECTOR_EVEX(VMULPD) };
        static const CheckVectorForms ternary = BUSTER_CHECK_VECTOR_FORMS(VPTERNLOGD_X_X_XM128_IMM8, VPTERNLOGD_Y_Y_YM256_IMM8, VPTERNLOGD_Z_Z_ZM512_IMM8);
        const uint32_t compare = compares[d].forms[width];
        const uint32_t move = moves[d].forms[width];
        uint32_t r = 0;
        if (builtin == CBUILTIN_ELEMENTWISE_ABS)
        {
            // llvm.fabs: x's magnitude, its sign bit cleared, NaNs' too: A & B (VPTERNLOGD 192), A x, B the magnitude mask.
            check_vector_constant(row, constants, sign - 1, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            row->records[r] = check_vector_lower_record(ternary.forms[width], v0, v1, v1);
            row->records[r++].immediate = 192;
        }
        else if (builtin <= CBUILTIN_ELEMENTWISE_ROUNDEVEN)
        {
            row->records[r] = check_vector_lower_record(roundings[d].forms[width], v0, 0, v0);
            row->records[r++].immediate = check_vector_rounding(builtin);
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_ROUND)
        {
            check_vector_constant(row, constants, check_vector_floating_half(d), shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            check_vector_constant(row, constants, sign, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            row->records[r] = check_vector_lower_record(ternary.forms[width], v1, v0, v2);
            row->records[r++].immediate = 248;
            row->records[r++] = check_vector_lower_record(adds[d].forms[width], v0, v0, v1);
            row->records[r] = check_vector_lower_record(roundings[d].forms[width], v0, 0, v0);
            row->records[r++].immediate = 11;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_CANONICALIZE)
        {
            check_vector_constant(row, constants, check_vector_floating_one(d), shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            row->records[r++] = check_vector_lower_record(products[d].forms[width], v0, v0, v1);
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_COPYSIGN)
        {
            // x where the mask is set, y elsewhere: (B ? A : C), A x, B the mask, C y.
            check_vector_constant(row, constants, sign - 1, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            row->records[r] = check_vector_lower_record(ternary.forms[width], v0, v2, v1);
            row->records[r++].immediate = 226;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MINNUM)
        {
            const uint32_t operation = (builtin == CBUILTIN_ELEMENTWISE_MAXNUM ? maxima : minima)[d].forms[width];
            row->records[r] = check_vector_lower_record(compare, 1, v0, v0);
            row->records[r++].immediate = 3;
            row->records[r++] = check_vector_lower_record(operation, v0, v1, v0);
            row->records[r] = check_vector_lower_record(move, v0, 0, v1);
            row->records[r++].decorations = 1;
        }
        else
        {
            const bool maximum = builtin == CBUILTIN_ELEMENTWISE_MAXIMUM || builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM;
            const bool number = builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM || builtin == CBUILTIN_ELEMENTWISE_MINIMUMNUM;
            check_vector_constant(row, constants, maximum ? sign - 1 : sign, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
            row->records[r++] = check_vector_lower_record((maximum ? maxima : minima)[d].forms[width], v3, v0, v1);
            row->records[r] = check_vector_lower_record(ternary.forms[width], v3, v0, v2);
            row->records[r++].immediate = maximum ? 224 : 248;
            if (number)
            {
                row->records[r] = check_vector_lower_record(compare, 1, v0, v0);
                row->records[r++].immediate = 3;
                row->records[r] = check_vector_lower_record(move, v3, 0, v1);
                row->records[r++].decorations = 1;
                row->records[r] = check_vector_lower_record(compare, 1, v1, v1);
                row->records[r++].immediate = 7;
            }
            else
            {
                row->records[r] = check_vector_lower_record(compare, 1, v0, v0);
                row->records[r++].immediate = 7;
            }
            row->records[r] = check_vector_lower_record(move, v0, 0, v3);
            row->records[r++].decorations = 1;
        }
        row->record_count = (uint8_t)r;
    }
    return reason;
}

// The rows of a floating elementwise builtin that check_vector_floating_rows gives: up to three.
#define BUSTER_CHECK_VECTOR_FLOATING_ROWS 3u

// The baseline's select row of check_vector_baseline_floating_rows' chain: of its operands, the previous row's result
// VECTOR(0) and the arguments x VECTOR(1) and y VECTOR(2), `with` where `tested` is a NaN, else the previous result,
// that result xor the two's xor under CMPUNORDP's mask (MOVAPS, CMPUNORDP, PXOR, PAND, PXOR). `tested` is copied first,
// so that `with` may be the same operand.
static void check_vector_baseline_select_row(CheckVectorShape shape, uint8_t tested, uint8_t with, Tier0Operation* row)
{
    const bool d = shape.element == 8;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
    uint32_t r = 0;
    memset(row, 0, sizeof(*row));
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)shape.bytes;
    row->operand_count = 3;
    for (uint32_t j = 0; j < 3; j += 1)
    {
        row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[j] = (uint8_t)shape.bytes;
    }
    row->records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, v3, 0, tested);
    row->records[r] = check_vector_lower_record(d ? X86_FORM_CMPPD_X_XM128_IMM8 : X86_FORM_CMPPS_X_XM128_IMM8, v3, 0, v3);
    row->records[r++].immediate = 3;
    row->records[r++] = check_vector_lower_record(X86_FORM_PXOR_X_XM128, with, 0, v0);
    row->records[r++] = check_vector_lower_record(X86_FORM_PAND_X_XM128, with, 0, v3);
    row->records[r++] = check_vector_lower_record(X86_FORM_PXOR_X_XM128, v0, 0, with);
    row->record_count = (uint8_t)r;
}

// check_vector_floating_shape_row's builtins of floats or doubles without AVX-512F and AVX-512VL (#1834), the same
// values by SSE's forms (AVX's VEX ones, of 32 bytes too), its operands and constants that row's: SSE4.1's ROUNDPS and
// ROUNDPD for the roundings (round's of the value plus the largest value below 0.5 of its sign: PAND, POR, ADDP),
// canonicalize's MULP by 1.0, copysign's x's magnitude and y's sign (PAND, PANDN and POR; with AVX ANDPS, then y's bits
// less its magnitude's by ANDPS and XORPS, ORPS), maxnum's and minnum's MAXP or MINP of y and x, y where x is a NaN
// (CMPUNORDP's mask: PAND, PANDN and POR; with AVX VBLENDVP). The IEEE maxima and minima are MAXP or MINP of x and y,
// its sign bit cleared where x's is clear (maximum's ANDPS by x ORed with the magnitude mask) or set where x's is
// (minimum's ORPS by x ANDed with the sign mask), x where x is a NaN, and for the _num forms y where x is one, then x where
// y is: with AVX one row (VBLENDVP by CMPUNORDP's masks), without it more records than a row has, so a chain of rows,
// each after the first of the previous one's result and x and y (check_vector_baseline_select_row's), `*count` of
// them. Without SSE4.1 the roundings are the C library's functions (check_lower_vector_library), as clang's code calls
// them.
static const char* check_vector_baseline_floating_rows(const X86Target* target, CBuiltinId builtin, CheckVectorShape shape, Tier0Operation* rows, uint32_t* count,
                                                       CheckVectorConstants* constants)
{
    const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
    const bool d = shape.element == 8;
    const uint32_t w = shape.bytes > 16 ? 1 : 0;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
    const bool binary = builtin >= CBUILTIN_ELEMENTWISE_COPYSIGN;
    const uint64_t sign = check_vector_floating_sign(d);
    Tier0Operation* row = rows;
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    *constants = (CheckVectorConstants){ 0 };
    *count = 1;
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)shape.bytes;
    row->operand_count = binary ? 2 : 1;
    for (uint32_t j = 0; j < row->operand_count; j += 1)
    {
        row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[j] = (uint8_t)shape.bytes;
    }
    // Each family's legacy form, then its VEX forms of 16 and 32 bytes; a legacy form's destination is its first source.
    static const uint16_t roundings[2][3] = { { X86_FORM_ROUNDPS_X_XM128_IMM8, X86_FORM_VROUNDPS_X_XM128_IMM8, X86_FORM_VROUNDPS_Y_YM256_IMM8 },
                                              { X86_FORM_ROUNDPD_X_XM128_IMM8, X86_FORM_VROUNDPD_X_XM128_IMM8, X86_FORM_VROUNDPD_Y_YM256_IMM8 } };
    static const uint16_t adds[2][3] = { { X86_FORM_ADDPS_X_XM128, X86_FORM_VADDPS_X_X_XM128, X86_FORM_VADDPS_Y_Y_YM256 },
                                         { X86_FORM_ADDPD_X_XM128, X86_FORM_VADDPD_X_X_XM128, X86_FORM_VADDPD_Y_Y_YM256 } };
    static const uint16_t products[2][3] = { { X86_FORM_MULPS_X_XM128, X86_FORM_VMULPS_X_X_XM128, X86_FORM_VMULPS_Y_Y_YM256 },
                                             { X86_FORM_MULPD_X_XM128, X86_FORM_VMULPD_X_X_XM128, X86_FORM_VMULPD_Y_Y_YM256 } };
    static const uint16_t maxima[2][3] = { { X86_FORM_MAXPS_X_XM128, X86_FORM_VMAXPS_X_X_XM128, X86_FORM_VMAXPS_Y_Y_YM256 },
                                           { X86_FORM_MAXPD_X_XM128, X86_FORM_VMAXPD_X_X_XM128, X86_FORM_VMAXPD_Y_Y_YM256 } };
    static const uint16_t minima[2][3] = { { X86_FORM_MINPS_X_XM128, X86_FORM_VMINPS_X_X_XM128, X86_FORM_VMINPS_Y_Y_YM256 },
                                           { X86_FORM_MINPD_X_XM128, X86_FORM_VMINPD_X_X_XM128, X86_FORM_VMINPD_Y_Y_YM256 } };
    static const uint16_t compares[2][3] = { { X86_FORM_CMPPS_X_XM128_IMM8, X86_FORM_VCMPPS_X_X_XM128_IMM8, X86_FORM_VCMPPS_Y_Y_YM256_IMM8 },
                                             { X86_FORM_CMPPD_X_XM128_IMM8, X86_FORM_VCMPPD_X_X_XM128_IMM8, X86_FORM_VCMPPD_Y_Y_YM256_IMM8 } };
    static const uint16_t blends[2][2] = { { X86_FORM_VBLENDVPS_X_X_XM128_X, X86_FORM_VBLENDVPS_Y_Y_YM256_Y },
                                           { X86_FORM_VBLENDVPD_X_X_XM128_X, X86_FORM_VBLENDVPD_Y_Y_YM256_Y } };
    static const uint16_t ands[3] = { X86_FORM_PAND_X_XM128, X86_FORM_VANDPS_X_X_XM128, X86_FORM_VANDPS_Y_Y_YM256 };
    static const uint16_t ors[3] = { X86_FORM_POR_X_XM128, X86_FORM_VORPS_X_X_XM128, X86_FORM_VORPS_Y_Y_YM256 };
    static const uint16_t xors[3] = { X86_FORM_PXOR_X_XM128, X86_FORM_VXORPS_X_X_XM128, X86_FORM_VXORPS_Y_Y_YM256 };
    const uint32_t f = avx ? 1 + w : 0;
    const uint32_t extreme = builtin == CBUILTIN_ELEMENTWISE_MAXIMUM || builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM ?
                                 maxima[d][f] :
                                 minima[d][f];
    uint32_t r = 0;
    if (!shape.floating || shape.bfloat || shape.element == 2)
    {
        reason = "elementwise vector builtins of half-precision or bf16 elements";
    }
    else if (shape.bytes > (avx ? 32u : 16u))
    {
        reason = "vector machine operation unavailable at this target";
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_ABS)
    {
        check_vector_constant(row, constants, sign - 1, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
        row->records[r++] = check_vector_lower_record(ands[f], v0, avx ? v0 : 0, v1);
    }
    else if (builtin <= CBUILTIN_ELEMENTWISE_ROUND && (target->features & BUSTER_X86_FEATURE(SSE41)) == 0)
    {
        reason = "vector roundings without SSE4.1";
    }
    else if (builtin <= CBUILTIN_ELEMENTWISE_ROUNDEVEN)
    {
        row->records[r] = check_vector_lower_record(roundings[d][f], v0, 0, v0);
        row->records[r++].immediate = check_vector_rounding(builtin);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_ROUND)
    {
        check_vector_constant(row, constants, check_vector_floating_half(d), shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
        check_vector_constant(row, constants, sign, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
        row->records[r++] = check_vector_lower_record(ands[f], v2, avx ? v2 : 0, v0);
        row->records[r++] = check_vector_lower_record(ors[f], v2, avx ? v2 : 0, v1);
        row->records[r++] = check_vector_lower_record(adds[d][f], v2, avx ? v2 : 0, v0);
        row->records[r] = check_vector_lower_record(roundings[d][f], v0, 0, v2);
        row->records[r++].immediate = 11;
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_CANONICALIZE)
    {
        check_vector_constant(row, constants, check_vector_floating_one(d), shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
        row->records[r++] = check_vector_lower_record(products[d][f], v0, avx ? v0 : 0, v1);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_COPYSIGN)
    {
        check_vector_constant(row, constants, sign - 1, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
        if (avx)
        {
            row->records[r++] = check_vector_lower_record(ands[f], v3, v1, v2);
            row->records[r++] = check_vector_lower_record(xors[f], v3, v3, v1);
            row->records[r++] = check_vector_lower_record(ands[f], v0, v0, v2);
            row->records[r++] = check_vector_lower_record(ors[f], v0, v0, v3);
        }
        else
        {
            row->records[r++] = check_vector_lower_record(X86_FORM_PAND_X_XM128, v0, 0, v2);
            row->records[r++] = check_vector_lower_record(X86_FORM_PANDN_X_XM128, v2, 0, v1);
            row->records[r++] = check_vector_lower_record(X86_FORM_POR_X_XM128, v0, 0, v2);
        }
    }
    else if ((builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MINNUM) && avx)
    {
        row->records[r++] = check_vector_lower_record(extreme, v3, v1, v0);
        row->records[r] = check_vector_lower_record(compares[d][f], v2, v0, v0);
        row->records[r++].immediate = 3;
        row->records[r] = check_vector_lower_record(blends[d][w], v0, v3, v1);
        row->records[r++].immediate = 2 << 4;
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MINNUM)
    {
        row->records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, v3, 0, v1);
        row->records[r++] = check_vector_lower_record(extreme, v3, 0, v0);
        row->records[r] = check_vector_lower_record(compares[d][0], v0, 0, v0);
        row->records[r++].immediate = 3;
        row->records[r++] = check_vector_lower_record(X86_FORM_PAND_X_XM128, v1, 0, v0);
        row->records[r++] = check_vector_lower_record(X86_FORM_PANDN_X_XM128, v0, 0, v3);
        row->records[r++] = check_vector_lower_record(X86_FORM_POR_X_XM128, v0, 0, v1);
    }
    else
    {
        const bool maximum = builtin == CBUILTIN_ELEMENTWISE_MAXIMUM || builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM;
        const bool number = builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM || builtin == CBUILTIN_ELEMENTWISE_MINIMUMNUM;
        check_vector_constant(row, constants, maximum ? sign - 1 : sign, shape.element, BUSTER_TYPED_CLASS_VECTOR, shape.bytes);
        if (avx)
        {
            row->records[r++] = check_vector_lower_record(extreme, v3, v0, v1);
            row->records[r++] = check_vector_lower_record(maximum ? ors[f] : ands[f], v2, v2, v0);
            row->records[r++] = check_vector_lower_record(maximum ? ands[f] : ors[f], v3, v3, v2);
            row->records[r] = check_vector_lower_record(compares[d][f], v2, v0, v0);
            row->records[r++].immediate = 3;
            if (number)
            {
                row->records[r] = check_vector_lower_record(blends[d][w], v3, v3, v1);
                row->records[r++].immediate = 2 << 4;
                row->records[r] = check_vector_lower_record(compares[d][f], v2, v1, v1);
                row->records[r++].immediate = 3;
            }
            row->records[r] = check_vector_lower_record(blends[d][w], v0, v3, v0);
            row->records[r++].immediate = 2 << 4;
        }
        else
        {
            row->records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, v3, 0, v0);
            row->records[r++] = check_vector_lower_record(extreme, v3, 0, v1);
            row->records[r++] = check_vector_lower_record(maximum ? X86_FORM_POR_X_XM128 : X86_FORM_PAND_X_XM128, v2, 0, v0);
            row->records[r++] = check_vector_lower_record(maximum ? X86_FORM_PAND_X_XM128 : X86_FORM_POR_X_XM128, v3, 0, v2);
            row->records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, v0, 0, v3);
            if (number)
            {
                check_vector_baseline_select_row(shape, v1, v2, rows + 1);
                check_vector_baseline_select_row(shape, v2, v1, rows + 2);
                *count = 3;
            }
            else
            {
                check_vector_baseline_select_row(shape, v1, v1, rows + 1);
                *count = 2;
            }
        }
    }
    row->record_count = (uint8_t)r;
    return reason;
}

// The rows of a floating elementwise builtin of a vector shape at a target: check_vector_floating_shape_row's one
// with AVX-512F and AVX-512VL, else check_vector_baseline_floating_rows'.
static const char* check_vector_floating_rows(const X86Target* target, CBuiltinId builtin, CheckVectorShape shape, Tier0Operation* rows, uint32_t* count,
                                              CheckVectorConstants* constants)
{
    const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    const char* reason = NULL;
    *count = 1;
    if ((target->features & evex) == evex)
    {
        reason = check_vector_floating_shape_row(builtin, shape, rows, constants);
    }
    else
    {
        reason = check_vector_baseline_floating_rows(target, builtin, shape, rows, count, constants);
    }
    return reason;
}

// The baseline's row of the scalar builtins check_vector_floating_scalar_row's AVX-512F row computes, but its roundings
// and ldexp, without AVX-512F (#1834): the same values by SSE's forms (AVX's VEX ones), its operands and constants that
// row's. canonicalize is MULSS or MULSD by 1.0; copysign x's magnitude (PAND) or y's sign (PANDN), POR; maxnum and minnum
// MAXSS or MINSS of y and x, or y where x is a NaN (CMPUNORDSS, then PAND, PANDN and POR). maximum and minimum, and
// their _num forms, take AVX's non-destructive forms and VBLENDVPS: MAXSS or MINSS of x and y, its sign bit then
// cleared by x's (maximum) or set (minimum), x where x is a NaN (for the _num forms, y where x is one, then x where y
// is); without AVX they take two rows (check_vector_floating_scalar_row's, #1834).
static const char* check_vector_baseline_scalar_row(const X86Target* target, CBuiltinId builtin, uint8_t class, Tier0Operation* row,
                                                    CheckVectorConstants* constants)
{
    const bool d = class == BUSTER_TYPED_CLASS_F64;
    const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
    const uint8_t f0 = BUSTER_TIER0_ROLE_FLOATING(0);
    const uint8_t f1 = BUSTER_TIER0_ROLE_FLOATING(1);
    const uint8_t f2 = BUSTER_TIER0_ROLE_FLOATING(2);
    const uint8_t fr = BUSTER_TIER0_ROLE_FLOATING_RESULT;
    const uint8_t t = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t m = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint64_t sign = check_vector_floating_sign(d);
    const uint32_t and = avx ? X86_FORM_VPAND_X_X_XM128 : X86_FORM_PAND_X_XM128;
    const uint32_t andn = avx ? X86_FORM_VPANDN_X_X_XM128 : X86_FORM_PANDN_X_XM128;
    const uint32_t or = avx ? X86_FORM_VPOR_X_X_XM128 : X86_FORM_POR_X_XM128;
    const uint32_t compare = avx ? (d ? X86_FORM_VCMPSD_X_X_XM64_IMM8 : X86_FORM_VCMPSS_X_X_XM32_IMM8) : (d ? X86_FORM_CMPSD_X_XM64_IMM8 : X86_FORM_CMPSS_X_XM32_IMM8);
    const bool maximum = builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MAXIMUM || builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM;
    const uint32_t extreme = maximum ? (avx ? (d ? X86_FORM_VMAXSD_X_X_XM64 : X86_FORM_VMAXSS_X_X_XM32) : (d ? X86_FORM_MAXSD_X_XM64 : X86_FORM_MAXSS_X_XM32)) :
                                       (avx ? (d ? X86_FORM_VMINSD_X_X_XM64 : X86_FORM_VMINSS_X_X_XM32) : (d ? X86_FORM_MINSD_X_XM64 : X86_FORM_MINSS_X_XM32));
    const uint32_t copy = avx ? X86_FORM_VMOVUPS_X_XM128 : X86_FORM_MOVUPS_X_XM;
    const char* reason = NULL;
    uint32_t r = 0;
    // A legacy form's destination is its first source (`avx ? first : 0` names VEX's vvvv).
    if (builtin == CBUILTIN_ELEMENTWISE_CANONICALIZE)
    {
        const uint32_t multiply = avx ? (d ? X86_FORM_VMULSD_X_X_XM64 : X86_FORM_VMULSS_X_X_XM32) : (d ? X86_FORM_MULSD_X_XM64 : X86_FORM_MULSS_X_XM32);
        check_vector_constant(row, constants, check_vector_floating_one(d), d ? 8 : 4, class, 0);
        row->records[r++] = check_vector_lower_record(multiply, fr, avx ? f0 : 0, f1);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_COPYSIGN)
    {
        check_vector_constant(row, constants, sign - 1, d ? 8 : 4, class, 0);
        row->records[r++] = check_vector_lower_record(and, fr, avx ? f0 : 0, f2);
        row->records[r++] = check_vector_lower_record(andn, f2, avx ? f2 : 0, f1);
        row->records[r++] = check_vector_lower_record(or, fr, avx ? fr : 0, f2);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MINNUM)
    {
        if (!avx)
        {
            row->records[r++] = check_vector_lower_record(copy, t, 0, f1);
        }
        row->records[r++] = check_vector_lower_record(extreme, t, avx ? f1 : 0, f0);
        row->records[r] = check_vector_lower_record(compare, fr, avx ? f0 : 0, f0);
        row->records[r++].immediate = 3;
        row->records[r++] = check_vector_lower_record(and, f1, avx ? f1 : 0, fr);
        row->records[r++] = check_vector_lower_record(andn, fr, avx ? fr : 0, t);
        row->records[r++] = check_vector_lower_record(or, fr, avx ? fr : 0, f1);
    }
    else if (!avx)
    {
        reason = "IEEE maximum and minimum of scalars without AVX or AVX-512F";
    }
    else
    {
        // check_vector_floating_scalar_row's AVX-512F row's twin, record by record (VPTERNLOGD's 224 and 248 as VPOR and
        // VPAND, its k1 blends as VBLENDVPS): a change to either's NaN handling changes both.
        const bool number = builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM || builtin == CBUILTIN_ELEMENTWISE_MINIMUMNUM;
        const uint32_t blend = d ? X86_FORM_VBLENDVPD_X_X_XM128_X : X86_FORM_VBLENDVPS_X_X_XM128_X;
        check_vector_constant(row, constants, maximum ? sign - 1 : sign, d ? 8 : 4, class, 0);
        row->records[r++] = check_vector_lower_record(extreme, t, f0, f1);
        row->records[r++] = check_vector_lower_record(maximum ? or : and, f2, f2, f0);
        row->records[r++] = check_vector_lower_record(maximum ? and : or, t, t, f2);
        row->records[r] = check_vector_lower_record(compare, m, f0, f0);
        row->records[r++].immediate = 3;
        if (number)
        {
            row->records[r] = check_vector_lower_record(blend, t, t, f1);
            row->records[r++].immediate = 1 << 4;
            row->records[r] = check_vector_lower_record(compare, m, f1, f1);
            row->records[r++].immediate = 3;
        }
        row->records[r] = check_vector_lower_record(blend, fr, t, f0);
        row->records[r++].immediate = 1 << 4;
    }
    row->record_count = (uint8_t)r;
    return reason;
}

// llvm.maxnum, minnum, maximumnum and minimumnum of float or double scalars (`class`) at a target without AVX-512F (#967),
// which C's fmax, fmin, fmaximum_num and fminimum_num compute too: x FLOATING(0), y FLOATING(1), maximumnum's and
// minimumnum's constant FLOATING(2) (the magnitude mask, or the sign mask), the result FLOATING_RESULT; VECTOR(0) and
// VECTOR(1) free ones. With AVX, clang -O0's own sequences: maxnum's VMAXSD of y and x (y > x ? y : x: x of two zeros)
// and VBLENDVPD of y where VCMPUNORDSD finds x a NaN; maximumnum's VMAXSD of x and y, its sign bit cleared where x's is
// clear (VPOR of x and the magnitude mask, then VPAND; minimumnum's set where x's is set: VPAND of x and the sign mask,
// then VPOR), then y where x is a NaN and x where y is (two VBLENDVPDs), so that a signaling NaN beside another NaN is
// itself, unquieted. Without AVX, SSE2's: maxnum's is clang's (MAXSD, CMPUNORDSD and the select's PAND, PANDN and POR,
// legacy forms tied to their destinations); maximumnum's, whose two selects past MAXSD and its sign's correction take
// more records than a row has, gives the same bits by two rows: `row` x > y ? x : y with the sign's mask all ones where
// x is a NaN (MAXSD's y then, a number or the NaN the second row replaces), and minimumnum's sign cleared there; and
// `tail`, of that, y and x (FLOATING(0) to FLOATING(2)), x where y is a NaN. Every other row leaves `tail` empty.
static void check_vector_extreme_scalar_rows(const X86Target* target, CBuiltinId builtin, uint8_t class, Tier0Operation* row, CheckVectorConstants* constants,
                                             Tier0Operation* tail)
{
    const bool d = class == BUSTER_TYPED_CLASS_F64;
    const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
    const bool maximum = builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM;
    const bool number = builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM || builtin == CBUILTIN_ELEMENTWISE_MINIMUMNUM;
    const uint8_t f0 = BUSTER_TIER0_ROLE_FLOATING(0);
    const uint8_t f1 = BUSTER_TIER0_ROLE_FLOATING(1);
    const uint8_t f2 = BUSTER_TIER0_ROLE_FLOATING(2);
    const uint8_t fr = BUSTER_TIER0_ROLE_FLOATING_RESULT;
    const uint8_t t0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t t1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint64_t sign = check_vector_floating_sign(d);
    const uint32_t operation = avx ? maximum ? (d ? X86_FORM_VMAXSD_X_X_XM64 : X86_FORM_VMAXSS_X_X_XM32) : (d ? X86_FORM_VMINSD_X_X_XM64 : X86_FORM_VMINSS_X_X_XM32)
                                   : maximum ? (d ? X86_FORM_MAXSD_X_XM64 : X86_FORM_MAXSS_X_XM32) : (d ? X86_FORM_MINSD_X_XM64 : X86_FORM_MINSS_X_XM32);
    const uint32_t compare = avx ? (d ? X86_FORM_VCMPSD_X_X_XM64_IMM8 : X86_FORM_VCMPSS_X_X_XM32_IMM8) : (d ? X86_FORM_CMPSD_X_XM64_IMM8 : X86_FORM_CMPSS_X_XM32_IMM8);
    const uint32_t blend = d ? X86_FORM_VBLENDVPD_X_X_XM128_X : X86_FORM_VBLENDVPS_X_X_XM128_X;
    uint32_t r = 0;
    if (number)
    {
        check_vector_constant(row, constants, maximum ? sign - 1 : sign, d ? 8 : 4, class, 0);
    }
    if (avx && !number)
    {
        row->records[r++] = check_vector_lower_record(operation, t0, f1, f0);
        row->records[r] = check_vector_lower_record(compare, t1, f0, f0);
        row->records[r++].immediate = 3;
        row->records[r] = check_vector_lower_record(blend, fr, t0, f1);
        row->records[r++].immediate = 1 << 4;
    }
    else if (avx)
    {
        row->records[r++] = check_vector_lower_record(maximum ? X86_FORM_VPOR_X_X_XM128 : X86_FORM_VPAND_X_X_XM128, t0, f0, f2);
        row->records[r++] = check_vector_lower_record(operation, t1, f0, f1);
        row->records[r++] = check_vector_lower_record(maximum ? X86_FORM_VPAND_X_X_XM128 : X86_FORM_VPOR_X_X_XM128, t0, t0, t1);
        row->records[r] = check_vector_lower_record(compare, t1, f0, f0);
        row->records[r++].immediate = 3;
        row->records[r] = check_vector_lower_record(blend, t0, t0, f1);
        row->records[r++].immediate = 1 << 4;
        row->records[r] = check_vector_lower_record(compare, t1, f1, f1);
        row->records[r++].immediate = 3;
        row->records[r] = check_vector_lower_record(blend, fr, t0, f0);
        row->records[r++].immediate = 1 << 4;
    }
    else if (!number)
    {
        row->records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, t0, 0, f1);
        row->records[r++] = check_vector_lower_record(operation, t0, 0, f0);
        row->records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, t1, 0, f0);
        row->records[r] = check_vector_lower_record(compare, t1, 0, t1);
        row->records[r++].immediate = 3;
        row->records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, fr, 0, t1);
        row->records[r++] = check_vector_lower_record(X86_FORM_PAND_X_XM128, t1, 0, f1);
        row->records[r++] = check_vector_lower_record(X86_FORM_PANDN_X_XM128, fr, 0, t0);
        row->records[r++] = check_vector_lower_record(X86_FORM_POR_X_XM128, fr, 0, t1);
    }
    else
    {
        // maximumnum's mask x | magnitude | unordered(x); minimumnum's x & sign & ~unordered(x).
        row->records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, t0, 0, f0);
        row->records[r] = check_vector_lower_record(compare, t0, 0, t0);
        row->records[r++].immediate = 3;
        row->records[r++] = check_vector_lower_record(maximum ? X86_FORM_POR_X_XM128 : X86_FORM_PANDN_X_XM128, t0, 0, f0);
        row->records[r++] = check_vector_lower_record(maximum ? X86_FORM_POR_X_XM128 : X86_FORM_PAND_X_XM128, t0, 0, f2);
        row->records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, fr, 0, f0);
        row->records[r++] = check_vector_lower_record(operation, fr, 0, f1);
        row->records[r++] = check_vector_lower_record(maximum ? X86_FORM_PAND_X_XM128 : X86_FORM_POR_X_XM128, fr, 0, t0);
        // The tail: a FLOATING(0), y FLOATING(1), x FLOATING(2); x where y is a NaN, else a.
        uint32_t t = 0;
        tail->result_class = class;
        tail->operand_count = 3;
        for (uint32_t j = 0; j < 3; j += 1)
        {
            tail->operand_classes[j] = class;
        }
        tail->records[t++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, t0, 0, f1);
        tail->records[t] = check_vector_lower_record(compare, t0, 0, t0);
        tail->records[t++].immediate = 3;
        tail->records[t++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, t1, 0, t0);
        tail->records[t++] = check_vector_lower_record(X86_FORM_PAND_X_XM128, t1, 0, f2);
        tail->records[t++] = check_vector_lower_record(X86_FORM_PANDN_X_XM128, t0, 0, f0);
        tail->records[t++] = check_vector_lower_record(X86_FORM_POR_X_XM128, t0, 0, t1);
        tail->records[t++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, fr, 0, t0);
        tail->record_count = (uint8_t)t;
    }
    row->record_count = (uint8_t)r;
}

// The same builtins (and __builtin_elementwise_ldexp) of float or double scalars (`class`, F32 or F64), its scalar
// instructions' sequence: x FLOATING(0), y FLOATING(1), then the constants, as scalars of the class, and the result
// FLOATING_RESULT, which is x's register; VECTOR(0) a free one. ldexp's exponent is a value of the class already (the
// lowering converts it), which VSCALEFSS or VSCALEFSD takes. Their EVEX forms and opmasks are AVX-512F's; without
// AVX-512F, maxnum's, minnum's, maximumnum's and minimumnum's are check_vector_extreme_scalar_rows', whose `tail` may be a
// second row (empty for every other: record_count 0).
static const char* check_vector_floating_scalar_row(const X86Target* target, CBuiltinId builtin, uint8_t class, Tier0Operation* row, CheckVectorConstants* constants,
                                                    Tier0Operation* tail)
{
    const bool d = class == BUSTER_TYPED_CLASS_F64;
    const uint8_t f0 = BUSTER_TIER0_ROLE_FLOATING(0);
    const uint8_t f1 = BUSTER_TIER0_ROLE_FLOATING(1);
    const uint8_t f2 = BUSTER_TIER0_ROLE_FLOATING(2);
    const uint8_t fr = BUSTER_TIER0_ROLE_FLOATING_RESULT;
    const uint8_t t = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint64_t sign = check_vector_floating_sign(d);
    const bool binary = builtin >= CBUILTIN_ELEMENTWISE_COPYSIGN;
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    *constants = (CheckVectorConstants){ 0 };
    row->result_class = class;
    row->operand_count = binary ? 2 : 1;
    for (uint32_t j = 0; j < row->operand_count; j += 1)
    {
        row->operand_classes[j] = class;
    }
    memset(tail, 0, sizeof(*tail));
    const bool extreme = builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MINNUM || builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM ||
                         builtin == CBUILTIN_ELEMENTWISE_MINIMUMNUM;
    const bool ieee = builtin == CBUILTIN_ELEMENTWISE_MAXIMUM || builtin == CBUILTIN_ELEMENTWISE_MINIMUM;
    if (builtin == CBUILTIN_ELEMENTWISE_ABS)
    {
        // llvm.fabs: x's magnitude, its sign bit cleared by the magnitude mask: AVX-512F's VPTERNLOGD 192 (A & B, A x, whose
        // register the result's is, and B the mask), AVX's VANDPS, or SSE's ANDPS (whose destination, the result's
        // register, is x's), as clang's code clears it.
        const bool evex = (target->features & BUSTER_X86_FEATURE(AVX512F)) != 0;
        const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
        check_vector_constant(row, constants, sign - 1, d ? 8 : 4, class, 0);
        row->records[0] = evex ? check_vector_lower_record(X86_FORM_VPTERNLOGD_X_X_XM128_IMM8, fr, f1, f1) :
                                 check_vector_lower_record(avx ? X86_FORM_VANDPS_X_X_XM128 : X86_FORM_ANDPS_X_XM128, fr, avx ? f0 : 0, f1);
        row->records[0].immediate = evex ? 192 : 0;
        row->record_count = 1;
    }
    else if ((target->features & BUSTER_X86_FEATURE(AVX512F)) == 0 && extreme)
    {
        check_vector_extreme_scalar_rows(target, builtin, class, row, constants, tail);
    }
    else if ((target->features & (BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX))) == 0 && ieee)
    {
        // IEEE maximum and minimum without AVX (#1834), clang's SSE2 sequence in two rows: `row` MAXSS or MINSS of x and y
        // (y where either is a NaN), its sign bit cleared where x's is clear (POR of x and the magnitude mask, then PAND)
        // or set where x's is set (PAND of x and the sign mask, then POR); `tail`, of that, y and x, x where x is a NaN
        // (CMPUNORDSS, then PAND, PANDN and POR), unquieted, as clang's is.
        const bool maximum = builtin == CBUILTIN_ELEMENTWISE_MAXIMUM;
        const uint8_t t1 = BUSTER_TIER0_ROLE_VECTOR(1);
        const uint32_t move = X86_FORM_MOVAPS_X_XM;
        uint32_t r = 0;
        check_vector_constant(row, constants, maximum ? sign - 1 : sign, d ? 8 : 4, class, 0);
        row->records[r++] = check_vector_lower_record(move, t, 0, f0);
        row->records[r++] = check_vector_lower_record(maximum ? (d ? X86_FORM_MAXSD_X_XM64 : X86_FORM_MAXSS_X_XM32) :
                                                                (d ? X86_FORM_MINSD_X_XM64 : X86_FORM_MINSS_X_XM32), t, 0, f1);
        row->records[r++] = check_vector_lower_record(maximum ? X86_FORM_POR_X_XM128 : X86_FORM_PAND_X_XM128, f2, 0, f0);
        row->records[r++] = check_vector_lower_record(maximum ? X86_FORM_PAND_X_XM128 : X86_FORM_POR_X_XM128, t, 0, f2);
        row->records[r++] = check_vector_lower_record(move, fr, 0, t);
        row->record_count = (uint8_t)r;
        tail->result_class = class;
        tail->operand_count = 3;
        for (uint32_t j = 0; j < 3; j += 1)
        {
            tail->operand_classes[j] = class;
        }
        uint32_t k = 0;
        tail->records[k++] = check_vector_lower_record(move, t, 0, f2);
        tail->records[k] = check_vector_lower_record(d ? X86_FORM_CMPSD_X_XM64_IMM8 : X86_FORM_CMPSS_X_XM32_IMM8, t, 0, t);
        tail->records[k++].immediate = 3;
        tail->records[k++] = check_vector_lower_record(move, t1, 0, t);
        tail->records[k++] = check_vector_lower_record(X86_FORM_PAND_X_XM128, t1, 0, f2);
        tail->records[k++] = check_vector_lower_record(X86_FORM_PANDN_X_XM128, t, 0, f0);
        tail->records[k++] = check_vector_lower_record(X86_FORM_POR_X_XM128, t, 0, t1);
        tail->records[k++] = check_vector_lower_record(move, fr, 0, t);
        tail->record_count = (uint8_t)k;
    }
    else if ((target->features & BUSTER_X86_FEATURE(AVX512F)) == 0 && builtin <= CBUILTIN_ELEMENTWISE_ROUND && (target->features & BUSTER_X86_FEATURE(SSE41)) != 0)
    {
        // Without AVX-512F, SSE4.1's ROUNDSS or ROUNDSD (AVX's VEX forms) of the value, as clang's code rounds it (#1834);
        // llvm.round's of the value plus the largest value below 0.5 of its sign (ANDPS of the sign, ORPS, ADDSS or ADDSD),
        // truncated. Without SSE4.1 the lowering calls the C library's function instead (check_lower_vector_floating).
        const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
        uint32_t r = 0;
        uint8_t source = f0;
        if (builtin == CBUILTIN_ELEMENTWISE_ROUND)
        {
            check_vector_constant(row, constants, check_vector_floating_half(d), d ? 8 : 4, class, 0);
            check_vector_constant(row, constants, sign, d ? 8 : 4, class, 0);
            const uint32_t add = avx ? (d ? X86_FORM_VADDSD_X_X_XM64 : X86_FORM_VADDSS_X_X_XM32) : (d ? X86_FORM_ADDSD_X_XM64 : X86_FORM_ADDSS_X_XM32);
            row->records[r++] = check_vector_lower_record(avx ? X86_FORM_VANDPS_X_X_XM128 : X86_FORM_ANDPS_X_XM128, f2, avx ? f2 : 0, f0);
            row->records[r++] = check_vector_lower_record(avx ? X86_FORM_VORPS_X_X_XM128 : X86_FORM_ORPS_X_XM128, f2, avx ? f2 : 0, f1);
            row->records[r++] = check_vector_lower_record(add, f2, avx ? f2 : 0, f0);
            source = f2;
        }
        const uint32_t rounding = avx ? (d ? X86_FORM_VROUNDSD_X_X_XM64_IMM8 : X86_FORM_VROUNDSS_X_X_XM32_IMM8) :
                                        (d ? X86_FORM_ROUNDSD_X_XM64_IMM8 : X86_FORM_ROUNDSS_X_XM32_IMM8);
        row->records[r] = check_vector_lower_record(rounding, fr, avx ? source : 0, source);
        row->records[r++].immediate = check_vector_rounding(builtin);
        row->record_count = (uint8_t)r;
    }
    else if ((target->features & BUSTER_X86_FEATURE(AVX512F)) == 0 && builtin != CBUILTIN_ELEMENTWISE_LDEXP)
    {
        reason = check_vector_baseline_scalar_row(target, builtin, class, row, constants);
    }
    else if ((target->features & BUSTER_X86_FEATURE(AVX512F)) == 0)
    {
        reason = "elementwise builtins of scalars without AVX-512F";
    }
    else
    {
        // A scalar's rounding is VRNDSCALEPS's or VRNDSCALEPD's of a register holding it alone, zeros past it (VPXORD, then
        // VMOVSS or VMOVSD), which round exactly: the scalar forms' result and exceptions (clang's VROUNDSS's), by forms
        // x86_semantics.c covers.
        const uint32_t rounding = d ? X86_FORM_VRNDSCALEPD_X_XM128_IMM8 : X86_FORM_VRNDSCALEPS_X_XM128_IMM8;
        const uint32_t compare = d ? X86_FORM_VCMPSD_K_X_XM64_IMM8 : X86_FORM_VCMPSS_K_X_XM32_IMM8;
        const uint32_t move = d ? X86_FORM_VMOVSD_X_X_X_EVEX : X86_FORM_VMOVSS_X_X_X_EVEX;
        const uint32_t ternary = X86_FORM_VPTERNLOGD_X_X_XM128_IMM8;
        uint32_t r = 0;
        if (builtin <= CBUILTIN_ELEMENTWISE_ROUND)
        {
            uint8_t source = f0;
            if (builtin == CBUILTIN_ELEMENTWISE_ROUND)
            {
                check_vector_constant(row, constants, check_vector_floating_half(d), d ? 8 : 4, class, 0);
                check_vector_constant(row, constants, sign, d ? 8 : 4, class, 0);
                row->records[r] = check_vector_lower_record(ternary, f1, f0, f2);
                row->records[r++].immediate = 248;
                row->records[r++] = check_vector_lower_record(d ? X86_FORM_VADDSD_X_X_XM64_EVEX : X86_FORM_VADDSS_X_X_XM32_EVEX, f1, f0, f1);
                source = f1;
            }
            row->records[r++] = check_vector_lower_record(X86_FORM_VPXORD_X_X_XM128, t, t, t);
            row->records[r++] = check_vector_lower_record(move, t, t, source);
            row->records[r] = check_vector_lower_record(rounding, fr, 0, t);
            row->records[r++].immediate = check_vector_rounding(builtin);
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_CANONICALIZE)
        {
            check_vector_constant(row, constants, check_vector_floating_one(d), d ? 8 : 4, class, 0);
            row->records[r++] = check_vector_lower_record(d ? X86_FORM_VMULSD_X_X_XM64_EVEX : X86_FORM_VMULSS_X_X_XM32_EVEX, fr, f0, f1);
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_COPYSIGN)
        {
            check_vector_constant(row, constants, sign - 1, d ? 8 : 4, class, 0);
            row->records[r] = check_vector_lower_record(ternary, fr, f2, f1);
            row->records[r++].immediate = 226;
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_LDEXP)
        {
            row->records[r++] = check_vector_lower_record(d ? X86_FORM_VSCALEFSD_X_X_XM64 : X86_FORM_VSCALEFSS_X_X_XM32, fr, f0, f1);
        }
        else if (builtin == CBUILTIN_ELEMENTWISE_MAXNUM || builtin == CBUILTIN_ELEMENTWISE_MINNUM)
        {
            const uint32_t operation = builtin == CBUILTIN_ELEMENTWISE_MAXNUM ? (d ? X86_FORM_VMAXSD_X_X_XM64_EVEX : X86_FORM_VMAXSS_X_X_XM32_EVEX) :
                                                                                (d ? X86_FORM_VMINSD_X_X_XM64_EVEX : X86_FORM_VMINSS_X_X_XM32_EVEX);
            row->records[r] = check_vector_lower_record(compare, 1, f0, f0);
            row->records[r++].immediate = 3;
            row->records[r++] = check_vector_lower_record(operation, fr, f1, f0);
            row->records[r] = check_vector_lower_record(move, fr, fr, f1);
            row->records[r++].decorations = 1;
        }
        else
        {
            const bool maximum = builtin == CBUILTIN_ELEMENTWISE_MAXIMUM || builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM;
            const bool number = builtin == CBUILTIN_ELEMENTWISE_MAXIMUMNUM || builtin == CBUILTIN_ELEMENTWISE_MINIMUMNUM;
            const uint32_t operation = maximum ? (d ? X86_FORM_VMAXSD_X_X_XM64_EVEX : X86_FORM_VMAXSS_X_X_XM32_EVEX) :
                                                 (d ? X86_FORM_VMINSD_X_X_XM64_EVEX : X86_FORM_VMINSS_X_X_XM32_EVEX);
            check_vector_constant(row, constants, maximum ? sign - 1 : sign, d ? 8 : 4, class, 0);
            row->records[r++] = check_vector_lower_record(operation, t, f0, f1);
            row->records[r] = check_vector_lower_record(ternary, t, f0, f2);
            row->records[r++].immediate = maximum ? 224 : 248;
            if (number)
            {
                row->records[r] = check_vector_lower_record(compare, 1, f0, f0);
                row->records[r++].immediate = 3;
                row->records[r] = check_vector_lower_record(move, t, t, f1);
                row->records[r++].decorations = 1;
                row->records[r] = check_vector_lower_record(compare, 1, f1, f1);
                row->records[r++].immediate = 7;
            }
            else
            {
                row->records[r] = check_vector_lower_record(compare, 1, f0, f0);
                row->records[r++].immediate = 7;
            }
            row->records[r] = check_vector_lower_record(move, fr, fr, t);
            row->records[r++].decorations = 1;
        }
        row->record_count = (uint8_t)r;
    }
    return reason;
}

// The row of __builtin_elementwise_ldexp(x, e) of vectors: e's elements to x's floating type by their bits' signed
// values, as llvm.ldexp takes them (VPMOVSXBD or VPMOVSXWD first for 8- and 16-bit ones, then VCVTDQ2PS or VCVTDQ2PD,
// or VCVTQQ2PS or VCVTQQ2PD with AVX-512DQ), then x times 2 to the floor of each (VSCALEFPS or VSCALEFPD), as clang -O0's
// code computes it. Its operands are x and e, of `vector`'s and `exponent`'s types.
static const char* check_vector_ldexp_shape_row(const X86Target* target, CheckVectorShape in, CheckVectorShape ex, Tier0Operation* row)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const char* reason = NULL;
    memset(row, 0, sizeof(*row));
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)in.bytes;
    row->operand_count = 2;
    row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    row->operand_bytes[0] = (uint8_t)in.bytes;
    row->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
    row->operand_bytes[1] = (uint8_t)ex.bytes;
    if (!in.floating || in.bfloat || in.element == 2)
    {
        reason = "elementwise vector builtins of half-precision or bf16 elements";
    }
    else if (!ex.integer || ex.element == 0 || ex.lanes != in.lanes)
    {
        reason = "vector ldexp of exponents of this type";
    }
    else
    {
        const bool d = in.element == 8;
        const uint32_t width = check_vector_builtin_width(in.bytes);
        const uint32_t dwords = check_vector_builtin_width(4 * in.lanes);
        uint32_t r = 0;
        if (ex.element <= 2)
        {
            static const CheckVectorForms extend[2] = { BUSTER_CHECK_VECTOR_FORMS(VPMOVSXBD_X_XM32_EVEX, VPMOVSXBD_Y_XM64_EVEX, VPMOVSXBD_Z_XM128),
                                                        BUSTER_CHECK_VECTOR_FORMS(VPMOVSXWD_X_XM64_EVEX, VPMOVSXWD_Y_XM128_EVEX, VPMOVSXWD_Z_YM256) };
            row->records[r++] = check_vector_lower_record(extend[ex.element == 2].forms[dwords], v1, 0, v1);
        }
        if (ex.element <= 4)
        {
            static const CheckVectorForms convert[2] = { BUSTER_CHECK_VECTOR_UNARY_EVEX(VCVTDQ2PS),
                                                         BUSTER_CHECK_VECTOR_FORMS(VCVTDQ2PD_X_XM64_EVEX, VCVTDQ2PD_Y_XM128_EVEX, VCVTDQ2PD_Z_YM256) };
            row->records[r++] = check_vector_lower_record(convert[d].forms[width], v1, 0, v1);
        }
        else
        {
            static const CheckVectorForms convert[2] = { BUSTER_CHECK_VECTOR_FORMS(VCVTQQ2PS_X_XM128, VCVTQQ2PS_X_YM256, VCVTQQ2PS_Y_ZM512),
                                                         BUSTER_CHECK_VECTOR_UNARY_PACKED(VCVTQQ2PD) };
            row->records[r++] = check_vector_lower_record(convert[d].forms[d ? width : check_vector_builtin_width(ex.bytes)], v1, 0, v1);
            reason = (target->features & BUSTER_X86_FEATURE(AVX512DQ)) == 0 ? "vector ldexp of 64-bit exponents without AVX-512DQ" : NULL;
        }
        static const CheckVectorForms scale[2] = { BUSTER_CHECK_VECTOR_PACKED(VSCALEFPS), BUSTER_CHECK_VECTOR_PACKED(VSCALEFPD) };
        row->records[r++] = check_vector_lower_record(scale[d].forms[width], v0, v0, v1);
        row->record_count = (uint8_t)r;
    }
    return reason;
}

static const char* check_vector_ldexp_row(const TypeTable* types, const X86Target* target, TypeId vector, TypeId exponent, Tier0Operation* row)
{
    return check_vector_ldexp_shape_row(target, check_vector_builtin_shape(types, vector), check_vector_builtin_shape(types, exponent), row);
}

// The most rows a floating reduction takes: a maximum's or minimum's halvings of 16 floats, a row each.
#define BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS 4u

// The rows of __builtin_reduce_maximum, _minimum or _assoc_fadd of a vector of floats or doubles into its element, as
// clang -O0's code reduces it: halving its bytes (the high 32 of 64 by VEXTRACTF64X4, the high 16 of 32 by
// VEXTRACTF32X4, then the high 8 of 16 by VPERMILPD 1 and of floats the high 4 of 8 by VMOVSHDUP, into v2), and combining
// the low part with the high one, the low one first: by llvm.maximum's or minimum's sequence (check_vector_floating_row's,
// its mask each row's second operand, a constant) or VADDP. A maximum's or minimum's row takes one halving, a sum's as
// many as its records hold. The last row's result is the element (FLOATING_RESULT); a sum's adds the tree's to the start
// value (VADDSS or VADDSD, the start's first, as clang adds them), its last operand (FLOATING), which the lowering gives
// -0.0 without one. `count` gets the rows' number.
static const char* check_vector_reduce_floating_shape_rows(CBuiltinId builtin, CheckVectorShape shape,
                                                           Tier0Operation rows[BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS], uint32_t* count)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
    const char* reason = NULL;
    memset(rows, 0, BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS * sizeof(*rows));
    *count = 0;
    if (!shape.floating || shape.bfloat || shape.element == 2)
    {
        reason = "vector reductions of half-precision or bf16 elements";
    }
    else if (builtin != CBUILTIN_REDUCE_ASSOC_FADD && shape.lanes * shape.element < shape.bytes)
    {
        // A vector its elements don't fill: LLVM reduces its maximum or minimum in an order its width decides (an element
        // at a time after halving to 16 bytes, and three floats' over the elements alone), which picks the operand a NaN
        // result takes its sign and payload from, where these halvings take another. Its sum halves as these do.
        reason = "floating maximum and minimum reductions of vectors their elements don't fill";
    }
    else
    {
        const bool d = shape.element == 8;
        const bool sum = builtin == CBUILTIN_REDUCE_ASSOC_FADD;
        const bool maximum = builtin == CBUILTIN_REDUCE_MAXIMUM;
        const uint8_t class = d ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
        static const CheckVectorForms maxima[2] = { BUSTER_CHECK_VECTOR_EVEX(VMAXPS), BUSTER_CHECK_VECTOR_EVEX(VMAXPD) };
        static const CheckVectorForms minima[2] = { BUSTER_CHECK_VECTOR_EVEX(VMINPS), BUSTER_CHECK_VECTOR_EVEX(VMINPD) };
        static const CheckVectorForms compares[2] = { BUSTER_CHECK_VECTOR_FORMS(VCMPPS_K_X_XM128_IMM8, VCMPPS_K_Y_YM256_IMM8, VCMPPS_K_Z_ZM512_IMM8),
                                                      BUSTER_CHECK_VECTOR_FORMS(VCMPPD_K_X_XM128_IMM8, VCMPPD_K_Y_YM256_IMM8, VCMPPD_K_Z_ZM512_IMM8) };
        static const CheckVectorForms moves[2] = { BUSTER_CHECK_VECTOR_UNARY_EVEX(VMOVAPS), BUSTER_CHECK_VECTOR_UNARY_EVEX(VMOVAPD) };
        static const CheckVectorForms adds[2] = { BUSTER_CHECK_VECTOR_EVEX(VADDPS), BUSTER_CHECK_VECTOR_EVEX(VADDPD) };
        static const CheckVectorForms ternary = BUSTER_CHECK_VECTOR_FORMS(VPTERNLOGD_X_X_XM128_IMM8, VPTERNLOGD_Y_Y_YM256_IMM8, VPTERNLOGD_Z_Z_ZM512_IMM8);
        uint32_t span = shape.bytes;
        uint32_t r = 0;
        uint32_t row = 0;
        // Each row's operands: the vector left, then a maximum's or minimum's mask.
        for (uint32_t i = 0; i < BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS; i += 1)
        {
            rows[i].operand_count = sum ? 1 : 2;
            rows[i].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            rows[i].operand_classes[1] = sum ? 0 : BUSTER_TYPED_CLASS_VECTOR;
        }
        rows[0].operand_bytes[0] = (uint8_t)shape.bytes;
        rows[0].operand_bytes[1] = sum ? 0 : (uint8_t)shape.bytes;
        while (span > shape.element && row < BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS)
        {
            const uint32_t half = span / 2;
            const uint32_t width = check_vector_builtin_width(half);
            X86Instruction halving[5];
            uint32_t h = 0;
            if (half >= 16)
            {
                halving[h] = check_vector_lower_record(half == 32 ? X86_FORM_VEXTRACTF64X4_YM256_Z_IMM8 : X86_FORM_VEXTRACTF32X4_XM128_Y_IMM8, v0, 0, v2);
                halving[h++].immediate = 1;
            }
            else if (half == 8)
            {
                halving[h] = check_vector_lower_record(X86_FORM_VPERMILPD_X_XM128_IMM8_EVEX, v2, 0, v0);
                halving[h++].immediate = 1;
            }
            else
            {
                halving[h++] = check_vector_lower_record(X86_FORM_VMOVSHDUP_X_XM128_EVEX, v2, 0, v0);
            }
            if (sum)
            {
                halving[h++] = check_vector_lower_record(adds[d].forms[width], v0, v0, v2);
            }
            else
            {
                halving[h++] = check_vector_lower_record((maximum ? maxima : minima)[d].forms[width], v3, v0, v2);
                halving[h] = check_vector_lower_record(ternary.forms[width], v3, v0, v1);
                halving[h++].immediate = maximum ? 224 : 248;
                halving[h] = check_vector_lower_record(compares[d].forms[width], 1, v0, v0);
                halving[h++].immediate = 7;
                halving[h] = check_vector_lower_record(moves[d].forms[width], v0, 0, v3);
                halving[h++].decorations = 1;
            }
            // A sum's last row keeps a record for the start's addition; a maximum's or minimum's, for the move out.
            const bool last = half == shape.element;
            if ((r + h + (last ? 1u : 0u) > BUSTER_TIER0_OPERATION_RECORDS || (!sum && r != 0)) && row + 1 < BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS)
            {
                // This row ends before the halving, its result the vector of the bytes left, the next row's operand.
                rows[row].record_count = (uint8_t)r;
                rows[row].result_class = BUSTER_TYPED_CLASS_VECTOR;
                rows[row].result_bytes = (uint8_t)span;
                row += 1;
                rows[row].operand_bytes[0] = (uint8_t)span;
                rows[row].operand_bytes[1] = sum ? 0 : (uint8_t)span;
                r = 0;
            }
            if (r + h + (last ? 1u : 0u) <= BUSTER_TIER0_OPERATION_RECORDS)
            {
                memcpy(rows[row].records + r, halving, h * sizeof(*halving));
            }
            else
            {
                // A vector has 16 elements at most: a maximum's four halvings take the four rows exactly.
                reason = "vector reductions of more elements than their rows hold";
            }
            r += h;
            span = half;
        }
        if (reason == NULL)
        {
            Tier0Operation* current = rows + row;
            if (sum)
            {
                // The start's addition: start + tree, the start the row's last operand.
                const uint32_t start = current->operand_count;
                current->operand_classes[start] = class;
                current->operand_count = (uint8_t)(start + 1);
                current->records[r++] = check_vector_lower_record(d ? X86_FORM_VADDSD_X_X_XM64_EVEX : X86_FORM_VADDSS_X_X_XM32_EVEX,
                                                                  BUSTER_TIER0_ROLE_FLOATING_RESULT, BUSTER_TIER0_ROLE_FLOATING(start), v0);
            }
            else
            {
                current->records[r++] = check_vector_lower_record(X86_FORM_VMOVAPS_X_XM128_EVEX, BUSTER_TIER0_ROLE_FLOATING_RESULT, 0, v0);
            }
            current->record_count = (uint8_t)r;
            current->result_class = class;
            *count = row + 1;
        }
    }
    return reason;
}

static const char* check_vector_reduce_floating_rows(const TypeTable* types, CBuiltinId builtin, TypeId vector,
                                                    Tier0Operation rows[BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS], uint32_t* count)
{
    return check_vector_reduce_floating_shape_rows(builtin, check_vector_builtin_shape(types, vector), rows, count);
}

// check_vector_reduce_floating_shape_rows' reductions without AVX-512F and AVX-512VL (#1834), of a vector of the
// target's registers' bytes at most (16, or 32 with AVX), by the same halvings, as clang's code reduces it: the high 16
// bytes of 32 by VEXTRACTF128, the high 8 of 16 by PSHUFD 0xee (VPERMILPD 1 with AVX) and a float's high 4 of 8 by PSHUFD
// 0x55 (VMOVSHDUP with AVX) into v2, then the low part combined with the high one, the low one first: a sum's ADDP (its
// start + the tree last, ADDSS or ADDSD into the start's copy), and a maximum's or minimum's llvm.maximum or minimum
// (check_vector_baseline_floating_rows'), its mask each row's second operand. With AVX that is a row a halving (VBLENDVP
// of the low part where it is a NaN); without, two: the first packs the low part with the high one, the high replaced by
// the low where the low is a NaN (CMPUNORDP's mask, PXOR, PAND, PXOR, then PUNPCKLQDQ or PUNPCKLDQ, so that the packed
// vector's high part is the second row's to extract), which MAXP or MINP then gives as the low NaN itself, and the second
// takes MAXP or MINP and the sign's correction (the low part's NaN unchanged by it). `count` gets the rows' number.
static const char* check_vector_baseline_reduce_floating_rows(const X86Target* target, CBuiltinId builtin, CheckVectorShape shape,
                                                              Tier0Operation rows[BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS], uint32_t* count)
{
    const bool avx = (target->features & BUSTER_X86_FEATURE(AVX)) != 0;
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
    const uint8_t fr = BUSTER_TIER0_ROLE_FLOATING_RESULT;
    const char* reason = NULL;
    memset(rows, 0, BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS * sizeof(*rows));
    *count = 0;
    if (!shape.floating || shape.bfloat || shape.element == 2)
    {
        reason = "vector reductions of half-precision or bf16 elements";
    }
    else if (shape.bytes > (avx ? 32u : 16u))
    {
        reason = "vector machine operation unavailable at this target";
    }
    else if (builtin != CBUILTIN_REDUCE_ASSOC_FADD && shape.lanes * shape.element < shape.bytes)
    {
        reason = "floating maximum and minimum reductions of vectors their elements don't fill";
    }
    else
    {
        const bool d = shape.element == 8;
        const bool sum = builtin == CBUILTIN_REDUCE_ASSOC_FADD;
        const bool maximum = builtin == CBUILTIN_REDUCE_MAXIMUM;
        const uint8_t class = d ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
        const uint32_t f = avx ? 1 : 0;
        static const uint16_t adds[2][2] = { { X86_FORM_ADDPS_X_XM128, X86_FORM_VADDPS_X_X_XM128 }, { X86_FORM_ADDPD_X_XM128, X86_FORM_VADDPD_X_X_XM128 } };
        static const uint16_t maxima[2][2] = { { X86_FORM_MAXPS_X_XM128, X86_FORM_VMAXPS_X_X_XM128 }, { X86_FORM_MAXPD_X_XM128, X86_FORM_VMAXPD_X_X_XM128 } };
        static const uint16_t minima[2][2] = { { X86_FORM_MINPS_X_XM128, X86_FORM_VMINPS_X_X_XM128 }, { X86_FORM_MINPD_X_XM128, X86_FORM_VMINPD_X_X_XM128 } };
        static const uint16_t compares[2][2] = { { X86_FORM_CMPPS_X_XM128_IMM8, X86_FORM_VCMPPS_X_X_XM128_IMM8 },
                                                 { X86_FORM_CMPPD_X_XM128_IMM8, X86_FORM_VCMPPD_X_X_XM128_IMM8 } };
        const uint32_t extreme = (maximum ? maxima : minima)[d][f];
        uint32_t span = shape.bytes;
        uint32_t row = 0;
        uint32_t r = 0;
        rows[0].operand_count = sum ? 1 : 2;
        rows[0].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        rows[0].operand_bytes[0] = (uint8_t)span;
        rows[0].operand_classes[1] = sum ? 0 : BUSTER_TYPED_CLASS_VECTOR;
        rows[0].operand_bytes[1] = sum ? 0 : (uint8_t)span;
        while (span > shape.element && reason == NULL)
        {
            const uint32_t half = span / 2;
            X86Instruction high;
            if (half == 16)
            {
                high = check_vector_lower_record(X86_FORM_VEXTRACTF128_XM128_Y_IMM8, v0, 0, v2);
                high.immediate = 1;
            }
            else if (avx)
            {
                high = check_vector_lower_record(half == 8 ? X86_FORM_VPERMILPD_X_XM128_IMM8 : X86_FORM_VMOVSHDUP_X_XM128, v2, 0, v0);
                high.immediate = half == 8 ? 1 : 0;
            }
            else
            {
                high = check_vector_lower_record(X86_FORM_PSHUFD_X_XM128_IMM8, v2, 0, v0);
                high.immediate = half == 8 ? 0xee : 0x55;
            }
            // Each maximum's or minimum's halving starts a row of its own (two without AVX); a sum's share one.
            if (!sum && r != 0)
            {
                if (row + 1 >= BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS)
                {
                    reason = "vector reductions of more elements than their rows hold";
                }
                else
                {
                    rows[row].record_count = (uint8_t)r;
                    rows[row].result_class = BUSTER_TYPED_CLASS_VECTOR;
                    rows[row].result_bytes = (uint8_t)span;
                    row += 1;
                    r = 0;
                    rows[row].operand_count = 2;
                    rows[row].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                    rows[row].operand_bytes[0] = (uint8_t)span;
                    rows[row].operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
                    rows[row].operand_bytes[1] = (uint8_t)span;
                }
            }
            if (reason == NULL && sum)
            {
                rows[row].records[r++] = high;
                rows[row].records[r++] = check_vector_lower_record(adds[d][f], v0, avx ? v0 : 0, v2);
            }
            else if (reason == NULL && avx)
            {
                rows[row].records[r++] = high;
                rows[row].records[r++] = check_vector_lower_record(extreme, v3, v0, v2);
                rows[row].records[r++] = check_vector_lower_record(maximum ? X86_FORM_VORPS_X_X_XM128 : X86_FORM_VANDPS_X_X_XM128, v1, v1, v0);
                rows[row].records[r++] = check_vector_lower_record(maximum ? X86_FORM_VANDPS_X_X_XM128 : X86_FORM_VORPS_X_X_XM128, v3, v3, v1);
                rows[row].records[r] = check_vector_lower_record(compares[d][1], v1, v0, v0);
                rows[row].records[r++].immediate = 3;
                rows[row].records[r] = check_vector_lower_record(d ? X86_FORM_VBLENDVPD_X_X_XM128_X : X86_FORM_VBLENDVPS_X_X_XM128_X, v0, v3, v0);
                rows[row].records[r++].immediate = 1 << 4;
            }
            else if (reason == NULL && row + 1 >= BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS)
            {
                reason = "vector reductions of more elements than their rows hold";
            }
            else if (reason == NULL)
            {
                // The packing row: its one operand the vector, its result the same bytes.
                rows[row].operand_count = 1;
                rows[row].operand_classes[1] = 0;
                rows[row].operand_bytes[1] = 0;
                rows[row].records[r++] = high;
                rows[row].records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, v3, 0, v0);
                rows[row].records[r] = check_vector_lower_record(compares[d][0], v3, 0, v3);
                rows[row].records[r++].immediate = 3;
                rows[row].records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, v1, 0, v0);
                rows[row].records[r++] = check_vector_lower_record(X86_FORM_PXOR_X_XM128, v1, 0, v2);
                rows[row].records[r++] = check_vector_lower_record(X86_FORM_PAND_X_XM128, v1, 0, v3);
                rows[row].records[r++] = check_vector_lower_record(X86_FORM_PXOR_X_XM128, v2, 0, v1);
                rows[row].records[r++] = check_vector_lower_record(half == 8 ? X86_FORM_PUNPCKLQDQ_X_XM128 : X86_FORM_PUNPCKLDQ_X_XM128, v0, 0, v2);
                rows[row].record_count = (uint8_t)r;
                rows[row].result_class = BUSTER_TYPED_CLASS_VECTOR;
                rows[row].result_bytes = (uint8_t)span;
                row += 1;
                r = 0;
                rows[row].operand_count = 2;
                rows[row].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                rows[row].operand_bytes[0] = (uint8_t)span;
                rows[row].operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
                rows[row].operand_bytes[1] = (uint8_t)span;
                rows[row].records[r++] = high;
                rows[row].records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, v3, 0, v0);
                rows[row].records[r++] = check_vector_lower_record(extreme, v3, 0, v2);
                rows[row].records[r++] = check_vector_lower_record(maximum ? X86_FORM_POR_X_XM128 : X86_FORM_PAND_X_XM128, v1, 0, v0);
                rows[row].records[r++] = check_vector_lower_record(maximum ? X86_FORM_PAND_X_XM128 : X86_FORM_POR_X_XM128, v3, 0, v1);
                rows[row].records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, v0, 0, v3);
            }
            span = half;
        }
        if (reason == NULL)
        {
            Tier0Operation* current = rows + row;
            if (sum)
            {
                // The start's addition: start + tree, into a copy of the start, the row's last operand.
                const uint32_t start = current->operand_count;
                current->operand_classes[start] = class;
                current->operand_count = (uint8_t)(start + 1);
                const uint32_t add = avx ? (d ? X86_FORM_VADDSD_X_X_XM64 : X86_FORM_VADDSS_X_X_XM32) : (d ? X86_FORM_ADDSD_X_XM64 : X86_FORM_ADDSS_X_XM32);
                if (avx)
                {
                    current->records[r++] = check_vector_lower_record(add, fr, BUSTER_TIER0_ROLE_FLOATING(start), v0);
                }
                else
                {
                    current->records[r++] = check_vector_lower_record(X86_FORM_MOVAPS_X_XM, fr, 0, BUSTER_TIER0_ROLE_FLOATING(start));
                    current->records[r++] = check_vector_lower_record(add, fr, 0, v0);
                }
            }
            else
            {
                current->records[r++] = check_vector_lower_record(avx ? X86_FORM_VMOVAPS_X_XM128 : X86_FORM_MOVAPS_X_XM, fr, 0, v0);
            }
            current->record_count = (uint8_t)r;
            current->result_class = class;
            current->result_bytes = 0;
            *count = row + 1;
        }
    }
    return reason;
}

// A floating reduction's rows at a target: check_vector_reduce_floating_shape_rows' with AVX-512F and AVX-512VL, else
// check_vector_baseline_reduce_floating_rows'.
static const char* check_vector_reduce_floating_target_rows(const X86Target* target, CBuiltinId builtin, CheckVectorShape shape,
                                                            Tier0Operation rows[BUSTER_CHECK_VECTOR_FLOATING_REDUCE_ROWS], uint32_t* count)
{
    const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    return (target->features & evex) == evex ? check_vector_reduce_floating_shape_rows(builtin, shape, rows, count) :
                                               check_vector_baseline_reduce_floating_rows(target, builtin, shape, rows, count);
}

// The widest piece of a nontemporal load or store (`store`) of `bytes` bytes that one row moves where the target hasn't
// AVX-512F and AVX-512VL (#1834): 16 bytes, 32 with AVX (a load's with AVX2: AVX's VMOVNTDQA is of 16), or `bytes`.
static uint32_t check_vector_nontemporal_width(const X86Target* target, bool store, uint32_t bytes)
{
    const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    const uint64_t wide = store ? BUSTER_X86_FEATURE(AVX) : BUSTER_X86_FEATURE(AVX2);
    const uint32_t width = (target->features & evex) == evex ? 64 : (target->features & wide) != 0 ? 32 : 16;
    return bytes < width ? bytes : width;
}

// The row of __builtin_nontemporal_store(value, p) or __builtin_nontemporal_load(p) of a vector of `vector`'s type at
// an address aligned to its bytes: VMOVNTPS, VMOVNTPD or VMOVNTDQ of the value by its elements, or VMOVNTDQA's load of
// any, as clang's code's are; of a vector of 4 or 8 bytes, its bits through a scratch register stored by MOVNTI
// (clang's for MMX's __m64, which takes SSE4A's MOVNTSD for floats where the target has it), and an ordinary load
// (clang's). A store's operands are the value and the address, a load's the address. Without SSE4.1 a load is an
// ordinary one, as clang's code's is (#1834); vectors wider than the target's nontemporal moves (16 bytes without AVX,
// a load's 32 without AVX2) are the lowering's pieces of `shape`'s bytes (check_vector_nontemporal_width).
static void check_vector_nontemporal_row(const X86Target* target, CheckVectorShape shape, bool store, Tier0Operation* row)
{
    const uint32_t width = check_vector_builtin_width(shape.bytes);
    const bool narrow = shape.bytes < 16;
    const bool wide = shape.bytes == 8;
    memset(row, 0, sizeof(*row));
    row->record_count = 1;
    if (store && narrow)
    {
        row->records[0] = check_vector_lower_record(wide ? X86_FORM_VMOVQ_RM64_X_EVEX : X86_FORM_VMOVD_RM32_X_EVEX, BUSTER_TIER0_ROLE_VECTOR(0), 0,
                                                    BUSTER_TIER0_ROLE_SCRATCH(0));
        row->records[1] = check_x86_lower_memory(wide ? X86_FORM_MOVNTI_M64_R64 : X86_FORM_MOVNTI_M32_R32, BUSTER_TIER0_ROLE_SCRATCH(0), BUSTER_TIER0_ROLE_INTEGER(1));
        row->record_count = 2;
        row->operand_count = 2;
        row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[0] = (uint8_t)shape.bytes;
        row->operand_classes[1] = BUSTER_TYPED_CLASS_U64;
        row->result_class = BUSTER_TYPED_CLASS_VOID;
    }
    else if (narrow)
    {
        row->records[0] = check_x86_lower_memory(wide ? X86_FORM_VMOVQ_X_XM64_EVEX : X86_FORM_VMOVD_X_RM32_EVEX, BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_INTEGER(0));
        row->operand_count = 1;
        row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
        row->result_class = BUSTER_TYPED_CLASS_VECTOR;
        row->result_bytes = (uint8_t)shape.bytes;
    }
    else if (store)
    {
        static const CheckVectorForms stores[3] = {
            BUSTER_CHECK_VECTOR_FORMS(VMOVNTPS_M128_X_EVEX, VMOVNTPS_M256_Y_EVEX, VMOVNTPS_M512_Z),
            BUSTER_CHECK_VECTOR_FORMS(VMOVNTPD_M128_X_EVEX, VMOVNTPD_M256_Y_EVEX, VMOVNTPD_M512_Z),
            BUSTER_CHECK_VECTOR_FORMS(VMOVNTDQ_M128_X_EVEX, VMOVNTDQ_M256_Y_EVEX, VMOVNTDQ_M512_Z),
        };
        const uint32_t kind = shape.floating && shape.element == 4 ? 0 : shape.floating && shape.element == 8 ? 1 : 2;
        row->records[0] = check_x86_lower_memory(stores[kind].forms[width], BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_INTEGER(1));
        row->operand_count = 2;
        row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[0] = (uint8_t)shape.bytes;
        row->operand_classes[1] = BUSTER_TYPED_CLASS_U64;
        row->result_class = BUSTER_TYPED_CLASS_VOID;
    }
    else
    {
        static const CheckVectorForms loads = BUSTER_CHECK_VECTOR_FORMS(VMOVNTDQA_X_M128_EVEX, VMOVNTDQA_Y_M256_EVEX, VMOVNTDQA_Z_M512);
        const bool ordinary = (target->features & BUSTER_X86_FEATURE(SSE41)) == 0;
        row->records[0] = check_x86_lower_memory(ordinary ? X86_FORM_VMOVDQU64_X_XM128 : loads.forms[width], BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_INTEGER(0));
        row->operand_count = 1;
        row->operand_classes[0] = BUSTER_TYPED_CLASS_U64;
        row->result_class = BUSTER_TYPED_CLASS_VECTOR;
        row->result_bytes = (uint8_t)shape.bytes;
    }
}

// check_vector_reduce_shape_rows' rows of a vector of `vector`'s type.
static const char* check_vector_reduce_rows(const TypeTable* types, const X86Target* target, CBuiltinId builtin, TypeId vector,
                                            Tier0Operation rows[BUSTER_CHECK_VECTOR_REDUCE_ROWS], uint32_t* count)
{
    return check_vector_reduce_shape_rows(target, builtin, check_vector_builtin_shape(types, vector), rows, count);
}

// Whether a reduction's vector of `vector`'s type has lanes past its elements (an ext_vector_type of 3 floats or of 6
// shorts, padded to a power of two), whose bytes may hold anything; then `row` gives the vector its operation's identity
// in them, as LLVM's widening of the vector does before clang's code reduces it, so that the reduction's halvings
// (check_vector_reduce_rows, check_vector_reduce_floating_rows) take the elements alone: (v & keep) | fill, VPTERNLOGD
// 0xEA of the vector and its constant operands `keep` (all ones in the elements' bytes) and `fill` (the identity past
// them): 0 for add, or, xor and an unsigned max; all ones for and and an unsigned min; 1 for mul; the largest value for
// a signed min and the smallest for a signed max; -inf for maximum, +inf for minimum and -0.0 for assoc_fadd.
static bool check_vector_reduce_fill_row(const TypeTable* types, CBuiltinId builtin, TypeId vector, Tier0Operation* row, uint64_t keep[8], uint64_t fill[8])
{
    const CheckVectorShape shape = check_vector_builtin_shape(types, vector);
    const uint32_t used = shape.lanes * shape.element;
    const bool padded = shape.element != 0 && used < shape.bytes && shape.bytes <= 64;
    memset(row, 0, sizeof(*row));
    memset(keep, 0, 8 * sizeof(*keep));
    memset(fill, 0, 8 * sizeof(*fill));
    if (padded)
    {
        const bool d = shape.element == 8;
        const uint64_t ones = d ? UINT64_MAX : (UINT64_C(1) << (8 * shape.element)) - 1;
        uint64_t identity = 0;
        if (builtin == CBUILTIN_REDUCE_AND || (builtin == CBUILTIN_REDUCE_MIN && !shape.is_signed))
        {
            identity = ones;
        }
        else if (builtin == CBUILTIN_REDUCE_MUL)
        {
            identity = 1;
        }
        else if (builtin == CBUILTIN_REDUCE_MIN)
        {
            identity = ones >> 1;
        }
        else if (builtin == CBUILTIN_REDUCE_MAX && shape.is_signed)
        {
            identity = (ones >> 1) + 1;
        }
        else if (builtin == CBUILTIN_REDUCE_MAXIMUM || builtin == CBUILTIN_REDUCE_MINIMUM)
        {
            // The infinity of the element's type, negative for maximum.
            identity = (d ? UINT64_C(0x7ff0000000000000) : UINT64_C(0x7f800000)) | (builtin == CBUILTIN_REDUCE_MAXIMUM ? check_vector_floating_sign(d) : 0);
        }
        else if (builtin == CBUILTIN_REDUCE_ASSOC_FADD)
        {
            identity = check_vector_floating_sign(d);
        }
        uint8_t* kept = (uint8_t*)keep;
        uint8_t* filled = (uint8_t*)fill;
        for (uint32_t b = 0; b < shape.bytes; b += 1)
        {
            kept[b] = b < used ? 0xff : 0;
            filled[b] = b < used ? 0 : (uint8_t)(identity >> (8 * (b % shape.element)));
        }
        static const CheckVectorForms ternary = BUSTER_CHECK_VECTOR_FORMS(VPTERNLOGD_X_X_XM128_IMM8, VPTERNLOGD_Y_Y_YM256_IMM8, VPTERNLOGD_Z_Z_ZM512_IMM8);
        row->records[0] = check_vector_lower_record(ternary.forms[check_vector_builtin_width(shape.bytes)], BUSTER_TIER0_ROLE_VECTOR(0),
                                                    BUSTER_TIER0_ROLE_VECTOR(1), BUSTER_TIER0_ROLE_VECTOR(2));
        row->records[0].immediate = 0xea;
        row->record_count = 1;
        row->operand_count = 3;
        for (uint32_t j = 0; j < 3; j += 1)
        {
            row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
            row->operand_bytes[j] = (uint8_t)shape.bytes;
        }
        row->result_class = BUSTER_TYPED_CLASS_VECTOR;
        row->result_bytes = (uint8_t)shape.bytes;
    }
    return padded;
}

#undef BUSTER_CHECK_BASE_PACKED
#undef BUSTER_CHECK_BASE_UNARY
