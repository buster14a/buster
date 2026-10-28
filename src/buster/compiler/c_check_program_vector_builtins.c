// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Clang's generic vector builtins as Tier 0's machine operations (#136, #148): the rows (Tier0Operation) of
// __builtin_shufflevector, __builtin_convertvector between integer vectors, the integer __builtin_elementwise_*
// functions and the integer __builtin_reduce_* functions at their vectors' types, as clang -O0's code computes them, an
// instruction per element type where AVX-512 has one. A row's forms are EVEX ones of AVX-512F and AVX-512VL, which the
// lowering requires of a function with vectors (check_lower_vector_target), and those of the extension each row names:
// AVX-512BW's for 8- and 16-bit elements, VBMI's byte permutes, BITALG's and VPOPCNTDQ's counts, CD's leading zero
// counts and VBMI2's funnel shifts. What the rows don't take is refused by name: floating elements' conversions,
// arithmetic and reductions, leading zero counts of 8- and 16-bit elements, 8-bit funnel shifts, saturating arithmetic
// of 32- and 64-bit elements and bit reversal. Included by c_check_program_lower.c after c_check_program_x86.c, whose
// record helpers it takes.

// A vector type as these rows read it: its lanes and bytes, its elements' bytes (1, 2, 4 or 8, or 0 for any other
// size), and whether they are integers (bool's aren't) and signed.
typedef struct CheckVectorShape CheckVectorShape;
struct CheckVectorShape
{
    uint32_t lanes;
    uint32_t bytes;
    uint8_t element;
    bool integer;
    bool is_signed;
};

static CheckVectorShape check_vector_builtin_shape(const TypeTable* types, TypeId vector)
{
    const TypeId plain = type_unqualified(type_strip_aligned(types, vector));
    const TypeId element = type_unqualified(type_strip_aligned(types, type_element(types, plain)));
    const uint64_t size = type_size(types, element);
    uint32_t bits = 0;
    bool sign = false;
    const bool integer = element != BUSTER_TYPE_BOOL && check_action_integer_type(types, element, &bits, &sign) && bits == size * 8;
    const CheckVectorShape result = {
        .lanes = check_builtin_vector_lanes(types, plain),
        .bytes = (uint32_t)type_size(types, plain),
        .element = size == 1 || size == 2 || size == 4 || size == 8 ? (uint8_t)size : 0,
        .integer = integer,
        .is_signed = sign,
    };
    return result;
}

// A width's place among a family's forms: 16, 32 and 64 bytes.
static uint32_t check_vector_builtin_width(uint32_t bytes)
{
    return bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
}

// An element's bytes' place in the four-entry tables below: 1, 2, 4 and 8.
static uint32_t check_vector_builtin_element(uint32_t bytes)
{
    return bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
}

// A one-operand family's forms (dest, source: reg, rm): the EVEX ones at 16 and 32 bytes where VEX has some too.
#define BUSTER_CHECK_VECTOR_UNARY_EVEX(name) BUSTER_CHECK_VECTOR_FORMS(name##_X_XM128_EVEX, name##_Y_YM256_EVEX, name##_Z_ZM512)
#define BUSTER_CHECK_VECTOR_UNARY_PACKED(name) BUSTER_CHECK_VECTOR_FORMS(name##_X_XM128, name##_Y_YM256, name##_Z_ZM512)

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
static const char* check_vector_shuffle_row(const TypeTable* types, uint64_t target, TypeId source, TypeId result, const uint32_t* indices, bool b_kept,
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
    else if (!identity)
    {
        const uint32_t bytes = in.bytes > out.bytes ? in.bytes : out.bytes;
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
        reason = (target & needed) != needed ? (in.element == 1 ? "vector shuffles of 8-bit elements without AVX-512 VBMI" :
                                                                  "vector shuffles of 16-bit elements without AVX-512BW") :
                                               NULL;
    }
    row->operand_count = (uint8_t)operands;
    return reason;
}

// The row of __builtin_convertvector between integer vectors of the same lanes: the operand's first `result`'s lanes
// elements of `source`'s element type (all of a vector of `source`'s type, or a shuffle's first elements of its operand
// of `operand_bytes`, which the conversion takes in place of the shuffle's narrower vector), each to `result`'s element.
// A conversion between elements of one size keeps their bits (no records); to wider ones it extends them by the
// source's signedness (VPMOVSX and VPMOVZX, at the result's width), and to narrower ones it truncates them (VPMOV, at the
// source's width, which must then be the operand's).
static const char* check_vector_convert_row(const TypeTable* types, uint64_t target, TypeId source, uint32_t operand_bytes, TypeId result, Tier0Operation* row)
{
    const CheckVectorShape in = check_vector_builtin_shape(types, source);
    const CheckVectorShape out = check_vector_builtin_shape(types, result);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const char* reason = NULL;
    uint64_t needed = 0;
    memset(row, 0, sizeof(*row));
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)out.bytes;
    row->operand_count = 1;
    row->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    row->operand_bytes[0] = (uint8_t)operand_bytes;
    if (!in.integer || !out.integer)
    {
        reason = "vector conversions of floating elements";
    }
    else if (in.element == 0 || out.element == 0)
    {
        reason = "vector conversions of elements of this size";
    }
    else if (out.element > in.element)
    {
        // By the source's bytes and the result's: 1 to 2, 4 and 8, 2 to 4 and 8, 4 to 8; signed first.
        static const CheckVectorForms extend[2][6] = {
            {
                { { X86_FORM_VPMOVSXBW_X_XM64_EVEX, X86_FORM_VPMOVSXBW_Y_XM128_EVEX, X86_FORM_VPMOVSXBW_Z_YM256 } },
                { { X86_FORM_VPMOVSXBD_X_XM32_EVEX, X86_FORM_VPMOVSXBD_Y_XM64_EVEX, X86_FORM_VPMOVSXBD_Z_XM128 } },
                { { X86_FORM_VPMOVSXBQ_X_XM16_EVEX, X86_FORM_VPMOVSXBQ_Y_XM32_EVEX, X86_FORM_VPMOVSXBQ_Z_XM64 } },
                { { X86_FORM_VPMOVSXWD_X_XM64_EVEX, X86_FORM_VPMOVSXWD_Y_XM128_EVEX, X86_FORM_VPMOVSXWD_Z_YM256 } },
                { { X86_FORM_VPMOVSXWQ_X_XM32_EVEX, X86_FORM_VPMOVSXWQ_Y_XM64_EVEX, X86_FORM_VPMOVSXWQ_Z_XM128 } },
                { { X86_FORM_VPMOVSXDQ_X_XM64_EVEX, X86_FORM_VPMOVSXDQ_Y_XM128_EVEX, X86_FORM_VPMOVSXDQ_Z_YM256 } },
            },
            {
                { { X86_FORM_VPMOVZXBW_X_XM64_EVEX, X86_FORM_VPMOVZXBW_Y_XM128_EVEX, X86_FORM_VPMOVZXBW_Z_YM256 } },
                { { X86_FORM_VPMOVZXBD_X_XM32_EVEX, X86_FORM_VPMOVZXBD_Y_XM64_EVEX, X86_FORM_VPMOVZXBD_Z_XM128 } },
                { { X86_FORM_VPMOVZXBQ_X_XM16_EVEX, X86_FORM_VPMOVZXBQ_Y_XM32_EVEX, X86_FORM_VPMOVZXBQ_Z_XM64 } },
                { { X86_FORM_VPMOVZXWD_X_XM64_EVEX, X86_FORM_VPMOVZXWD_Y_XM128_EVEX, X86_FORM_VPMOVZXWD_Z_YM256 } },
                { { X86_FORM_VPMOVZXWQ_X_XM32_EVEX, X86_FORM_VPMOVZXWQ_Y_XM64_EVEX, X86_FORM_VPMOVZXWQ_Z_XM128 } },
                { { X86_FORM_VPMOVZXDQ_X_XM64_EVEX, X86_FORM_VPMOVZXDQ_Y_XM128_EVEX, X86_FORM_VPMOVZXDQ_Z_YM256 } },
            },
        };
        const uint32_t pair = in.element == 1 ? (out.element == 2 ? 0 : out.element == 4 ? 1 : 2) : in.element == 2 ? (out.element == 4 ? 3 : 4) : 5;
        row->records[0] = check_vector_lower_record(extend[in.is_signed ? 0 : 1][pair].forms[check_vector_builtin_width(out.bytes)], v0, 0, v0);
        row->record_count = 1;
        needed = pair == 0 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
    }
    else if (out.element < in.element && operand_bytes != in.bytes)
    {
        reason = "vector conversions of a shuffle's elements to narrower ones";
    }
    else if (out.element < in.element)
    {
        // By the source's bytes and the result's: 2 to 1, 4 to 1 and 2, 8 to 1, 2 and 4. The form's rm is its
        // destination.
        static const CheckVectorForms truncate[6] = {
            { { X86_FORM_VPMOVWB_XM64_X, X86_FORM_VPMOVWB_XM128_Y, X86_FORM_VPMOVWB_YM256_Z } },
            { { X86_FORM_VPMOVDB_XM32_X, X86_FORM_VPMOVDB_XM64_Y, X86_FORM_VPMOVDB_XM128_Z } },
            { { X86_FORM_VPMOVDW_XM64_X, X86_FORM_VPMOVDW_XM128_Y, X86_FORM_VPMOVDW_YM256_Z } },
            { { X86_FORM_VPMOVQB_XM16_X, X86_FORM_VPMOVQB_XM32_Y, X86_FORM_VPMOVQB_XM64_Z } },
            { { X86_FORM_VPMOVQW_XM32_X, X86_FORM_VPMOVQW_XM64_Y, X86_FORM_VPMOVQW_XM128_Z } },
            { { X86_FORM_VPMOVQD_XM64_X, X86_FORM_VPMOVQD_XM128_Y, X86_FORM_VPMOVQD_YM256_Z } },
        };
        const uint32_t pair = in.element == 2 ? 0 : in.element == 4 ? (out.element == 1 ? 1 : 2) : out.element == 1 ? 3 : out.element == 2 ? 4 : 5;
        row->records[0] = check_vector_lower_record(truncate[pair].forms[check_vector_builtin_width(in.bytes)], v0, 0, v0);
        row->record_count = 1;
        needed = pair == 0 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
    }
    if (reason == NULL && (target & needed) != needed)
    {
        reason = "vector conversions of 8-bit elements to 16-bit ones without AVX-512BW";
    }
    return reason;
}

// The row of a shuffle of a conversion into a vector narrower than 16 bytes (`narrow`, which Tier 0 has no class for)
// with a constant vector of its type (`b_bytes`, its bytes, or NULL where the shuffle reads none of its elements): the
// intrinsics' narrowing conversions, which widen the result back with zeros
// (`__builtin_shufflevector(__builtin_convertvector(x, v4qi), (v4qi){0, 0, 0, 0}, 0, 1, ..., 7, 7, ...)`). It truncates
// x into its register's first bytes (check_vector_convert_row's VPMOV), which zeroes the rest: where every result
// element is the conversion's at its place or a zero of the constant's past the conversion's, that is the result. Else
// it permutes the two (VPERMT2, at the result's width or 16 bytes): the conversion's elements are the first table's, the
// constant's the second's. Its operands are x, then where it permutes the constant (`plan->second`, 16 bytes) and the
// index vector.
static const char* check_vector_narrow_shuffle_row(const TypeTable* types, uint64_t target, TypeId converted, TypeId narrow, TypeId result,
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
        reason = "vector types of other than 16, 32 or 64 bytes";
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
            reason = (target & needed) != needed ? (out.element == 1 ? "vector shuffles of 8-bit elements without AVX-512 VBMI" :
                                                                       "vector shuffles of 16-bit elements without AVX-512BW") :
                                                   NULL;
        }
    }
    return reason;
}

// The forms of an integer elementwise operation at an element's bytes, two operands' (dest, first, second: reg, vvvv,
// rm) or one's (dest, source: reg, rm): forms 0 for none. `features` gets what they need past AVX-512F and AVX-512VL.
static CheckVectorForms check_vector_elementwise_forms(CBuiltinId builtin, CheckVectorShape shape, uint64_t* features)
{
    const uint32_t e = check_vector_builtin_element(shape.element);
    CheckVectorForms forms = { { 0, 0, 0 } };
    uint64_t needed = shape.element <= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
    if (builtin == CBUILTIN_ELEMENTWISE_MAX || builtin == CBUILTIN_ELEMENTWISE_MIN || builtin == CBUILTIN_REDUCE_MAX || builtin == CBUILTIN_REDUCE_MIN)
    {
        const bool max = builtin == CBUILTIN_ELEMENTWISE_MAX || builtin == CBUILTIN_REDUCE_MAX;
        static const CheckVectorForms signed_max[4] = { BUSTER_CHECK_VECTOR_EVEX(VPMAXSB), BUSTER_CHECK_VECTOR_EVEX(VPMAXSW), BUSTER_CHECK_VECTOR_EVEX(VPMAXSD),
                                                        BUSTER_CHECK_VECTOR_PACKED(VPMAXSQ) };
        static const CheckVectorForms unsigned_max[4] = { BUSTER_CHECK_VECTOR_EVEX(VPMAXUB), BUSTER_CHECK_VECTOR_EVEX(VPMAXUW), BUSTER_CHECK_VECTOR_EVEX(VPMAXUD),
                                                          BUSTER_CHECK_VECTOR_PACKED(VPMAXUQ) };
        static const CheckVectorForms signed_min[4] = { BUSTER_CHECK_VECTOR_EVEX(VPMINSB), BUSTER_CHECK_VECTOR_EVEX(VPMINSW), BUSTER_CHECK_VECTOR_EVEX(VPMINSD),
                                                        BUSTER_CHECK_VECTOR_PACKED(VPMINSQ) };
        static const CheckVectorForms unsigned_min[4] = { BUSTER_CHECK_VECTOR_EVEX(VPMINUB), BUSTER_CHECK_VECTOR_EVEX(VPMINUW), BUSTER_CHECK_VECTOR_EVEX(VPMINUD),
                                                          BUSTER_CHECK_VECTOR_PACKED(VPMINUQ) };
        forms = max ? (shape.is_signed ? signed_max[e] : unsigned_max[e]) : (shape.is_signed ? signed_min[e] : unsigned_min[e]);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_ADD_SAT || builtin == CBUILTIN_ELEMENTWISE_SUB_SAT)
    {
        // AVX-512 saturates 8- and 16-bit elements alone.
        const bool add = builtin == CBUILTIN_ELEMENTWISE_ADD_SAT;
        static const CheckVectorForms saturating[2][2][2] = {
            { { BUSTER_CHECK_VECTOR_EVEX(VPADDSB), BUSTER_CHECK_VECTOR_EVEX(VPADDSW) }, { BUSTER_CHECK_VECTOR_EVEX(VPADDUSB), BUSTER_CHECK_VECTOR_EVEX(VPADDUSW) } },
            { { BUSTER_CHECK_VECTOR_EVEX(VPSUBSB), BUSTER_CHECK_VECTOR_EVEX(VPSUBSW) }, { BUSTER_CHECK_VECTOR_EVEX(VPSUBUSB), BUSTER_CHECK_VECTOR_EVEX(VPSUBUSW) } },
        };
        forms = shape.element <= 2 ? saturating[add ? 0 : 1][shape.is_signed ? 0 : 1][e] : forms;
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_ABS)
    {
        static const CheckVectorForms abs[4] = { BUSTER_CHECK_VECTOR_UNARY_EVEX(VPABSB), BUSTER_CHECK_VECTOR_UNARY_EVEX(VPABSW), BUSTER_CHECK_VECTOR_UNARY_EVEX(VPABSD),
                                                 BUSTER_CHECK_VECTOR_UNARY_PACKED(VPABSQ) };
        forms = abs[e];
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_POPCOUNT)
    {
        static const CheckVectorForms counts[4] = { BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTB), BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTW),
                                                    BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTD), BUSTER_CHECK_VECTOR_UNARY_PACKED(VPOPCNTQ) };
        forms = counts[e];
        needed = shape.element <= 2 ? BUSTER_X86_FEATURE(AVX512BITALG) : BUSTER_X86_FEATURE(AVX512VPOPCNTDQ);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_CLZG)
    {
        // AVX-512CD counts 32- and 64-bit elements' leading zeros alone.
        forms = shape.element == 4 ? BUSTER_CHECK_VECTOR_UNARY_PACKED(VPLZCNTD) : shape.element == 8 ? BUSTER_CHECK_VECTOR_UNARY_PACKED(VPLZCNTQ) : forms;
        needed = BUSTER_X86_FEATURE(AVX512CD);
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_FSHL || builtin == CBUILTIN_ELEMENTWISE_FSHR)
    {
        // AVX-512 VBMI2 shifts pairs of 16-, 32- and 64-bit elements alone.
        const bool left = builtin == CBUILTIN_ELEMENTWISE_FSHL;
        static const CheckVectorForms shifts[2][3] = {
            { BUSTER_CHECK_VECTOR_PACKED(VPSHLDVW), BUSTER_CHECK_VECTOR_PACKED(VPSHLDVD), BUSTER_CHECK_VECTOR_PACKED(VPSHLDVQ) },
            { BUSTER_CHECK_VECTOR_PACKED(VPSHRDVW), BUSTER_CHECK_VECTOR_PACKED(VPSHRDVD), BUSTER_CHECK_VECTOR_PACKED(VPSHRDVQ) },
        };
        forms = shape.element >= 2 ? shifts[left ? 0 : 1][e - 1] : forms;
        needed = BUSTER_X86_FEATURE(AVX512VBMI2);
    }
    else if (builtin == CBUILTIN_REDUCE_ADD)
    {
        static const CheckVectorForms add[4] = { BUSTER_CHECK_VECTOR_EVEX(VPADDB), BUSTER_CHECK_VECTOR_EVEX(VPADDW), BUSTER_CHECK_VECTOR_EVEX(VPADDD),
                                                 BUSTER_CHECK_VECTOR_EVEX(VPADDQ) };
        forms = add[e];
    }
    else if (builtin == CBUILTIN_REDUCE_MUL)
    {
        // AVX-512 multiplies 16-, 32- and 64-bit elements alone, the last with AVX-512DQ.
        forms = shape.element == 2 ? BUSTER_CHECK_VECTOR_EVEX(VPMULLW) : shape.element == 4 ? BUSTER_CHECK_VECTOR_EVEX(VPMULLD) :
                shape.element == 8 ? BUSTER_CHECK_VECTOR_PACKED(VPMULLQ) : forms;
        needed = shape.element == 2 ? BUSTER_X86_FEATURE(AVX512BW) : shape.element == 8 ? BUSTER_X86_FEATURE(AVX512DQ) : 0;
    }
    else if (builtin == CBUILTIN_REDUCE_AND || builtin == CBUILTIN_REDUCE_OR)
    {
        // Every element's bits at once: the 64-bit forms whatever the elements.
        forms = builtin == CBUILTIN_REDUCE_AND ? BUSTER_CHECK_VECTOR_PACKED(VPANDQ) : BUSTER_CHECK_VECTOR_PACKED(VPORQ);
        needed = 0;
    }
    *features = needed;
    return forms;
}

// The reason a row's features refuse it, by the builtin: what the target hasn't of `needed`.
static const char* check_vector_builtin_feature_reason(CBuiltinId builtin, uint64_t needed, uint64_t target)
{
    const uint64_t missing = needed & ~target;
    const char* reason = NULL;
    if ((missing & BUSTER_X86_FEATURE(AVX512BITALG)) != 0)
    {
        reason = "vector population counts of 8- and 16-bit elements without AVX-512 BITALG";
    }
    else if ((missing & BUSTER_X86_FEATURE(AVX512VPOPCNTDQ)) != 0)
    {
        reason = "vector population counts of 32- and 64-bit elements without AVX-512 VPOPCNTDQ";
    }
    else if ((missing & BUSTER_X86_FEATURE(AVX512CD)) != 0)
    {
        reason = "vector leading zero counts without AVX-512CD";
    }
    else if ((missing & BUSTER_X86_FEATURE(AVX512VBMI2)) != 0)
    {
        reason = "vector funnel shifts without AVX-512 VBMI2";
    }
    else if ((missing & BUSTER_X86_FEATURE(AVX512DQ)) != 0)
    {
        reason = "vector multiplication of 64-bit elements without AVX-512DQ";
    }
    else if (missing != 0)
    {
        reason = builtin >= CBUILTIN_REDUCE_ADD ? "vector reductions of 8- and 16-bit elements without AVX-512BW" :
                                                  "elementwise vector builtins of 8- and 16-bit elements without AVX-512BW";
    }
    return reason;
}

// The row of an integer __builtin_elementwise_* function at a vector type, of `arguments` operands (all of the type,
// which its result is too): max and min, abs, saturating addition and subtraction, population counts, leading zero
// counts (clzg's second operand the elements a zero element takes) and funnel shifts.
static const char* check_vector_elementwise_row(const TypeTable* types, uint64_t target, CBuiltinId builtin, TypeId vector, uint32_t arguments, Tier0Operation* row)
{
    const CheckVectorShape shape = check_vector_builtin_shape(types, vector);
    const uint32_t width = check_vector_builtin_width(shape.bytes);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const char* reason = NULL;
    uint64_t needed = 0;
    memset(row, 0, sizeof(*row));
    row->result_class = BUSTER_TYPED_CLASS_VECTOR;
    row->result_bytes = (uint8_t)shape.bytes;
    row->operand_count = (uint8_t)arguments;
    for (uint32_t j = 0; j < arguments && j < 3; j += 1)
    {
        row->operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
        row->operand_bytes[j] = (uint8_t)shape.bytes;
    }
    if (!shape.integer)
    {
        reason = "elementwise vector builtins of floating elements";
    }
    else if (shape.element == 0)
    {
        reason = "elementwise vector builtins of elements of this size";
    }
    else if (builtin == CBUILTIN_ELEMENTWISE_BITREVERSE)
    {
        reason = "vector bit reversal";
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
        if (reason == NULL && (target & needed) != needed)
        {
            reason = check_vector_builtin_feature_reason(builtin, needed, target);
        }
    }
    return reason;
}

// The most rows a reduction takes: a row's 8 records hold three halvings and the element's move.
#define BUSTER_CHECK_VECTOR_REDUCE_ROWS 2u

// The rows of an integer __builtin_reduce_* function of a vector into its element: halves combined by the operation
// down to one element (the high 32 bytes of 64 by VEXTRACTI64X4, the high 16 of 32 by VEXTRACTI32X4, then within 16
// bytes the high 8 by VPSHUFD 0x4e, the high 4 by VPSHUFD 0xb1, the high 2 by VPSRLD 16 and the high byte by VPSRLW 8,
// each beside the low part in v1), then that element in the general register (VMOVQ, VMOVD, VPEXTRW or VPEXTRB). A
// halving is two records: past three of them (elements under 8 bytes, or under 4 of a 32-byte vector), the first row
// takes four, its result the 16 bytes left, and the second the rest. `count` gets the rows' number.
static const char* check_vector_reduce_rows(const TypeTable* types, uint64_t target, CBuiltinId builtin, TypeId vector,
                                            Tier0Operation rows[BUSTER_CHECK_VECTOR_REDUCE_ROWS], uint32_t* count)
{
    const CheckVectorShape shape = check_vector_builtin_shape(types, vector);
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const char* reason = NULL;
    uint64_t needed = 0;
    memset(rows, 0, BUSTER_CHECK_VECTOR_REDUCE_ROWS * sizeof(*rows));
    *count = 0;
    if (!shape.integer)
    {
        reason = "vector reductions of floating elements";
    }
    else if (shape.element == 0)
    {
        reason = "vector reductions of elements of this size";
    }
    else
    {
        const CheckVectorForms forms = check_vector_elementwise_forms(builtin, shape, &needed);
        // The halvings' records, two each (64 bytes of 1-byte elements take six), then the element's move.
        X86Instruction records[2 * 6 + 1];
        uint32_t r = 0;
        uint32_t span = shape.bytes;
        while (span > shape.element && forms.forms[0] != 0)
        {
            span /= 2;
            const uint32_t at = span >= 16 ? span : 16;
            if (span >= 32)
            {
                records[r] = check_vector_lower_record(X86_FORM_VEXTRACTI64X4_YM256_Z_IMM8, v0, 0, v1);
            }
            else if (span == 16)
            {
                records[r] = check_vector_lower_record(X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8, v0, 0, v1);
            }
            else if (span >= 4)
            {
                records[r] = check_vector_lower_record(X86_FORM_VPSHUFD_X_XM128_IMM8_EVEX, v1, 0, v0);
            }
            else
            {
                records[r] = check_vector_lower_record(span == 2 ? X86_FORM_VPSRLD_X_XM128_IMM8 : X86_FORM_VPSRLW_X_XM128_IMM8, 0, v1, v0);
            }
            records[r].immediate = span >= 16 ? 1 : span == 8 ? 0x4e : span == 4 ? 0xb1 : span == 2 ? 16 : 8;
            records[r + 1] = check_vector_lower_record(forms.forms[check_vector_builtin_width(at)], v0, v0, v1);
            r += 2;
        }
        const uint8_t result = shape.element == 8 ? BUSTER_TYPED_CLASS_I64 : shape.element == 4 ? BUSTER_TYPED_CLASS_I32 :
                               shape.element == 2 ? BUSTER_TYPED_CLASS_U16 : BUSTER_TYPED_CLASS_U8;
        const uint32_t move = shape.element == 8 ? X86_FORM_VMOVQ_RM64_X_EVEX : shape.element == 4 ? X86_FORM_VMOVD_RM32_X_EVEX :
                              shape.element == 2 ? X86_FORM_VPEXTRW_R32_X_IMM8_EVEX : X86_FORM_VPEXTRB_RM32_X_IMM8_EVEX;
        // VPEXTRW's register form names the general register in reg, the others in rm.
        records[r] = shape.element == 2 ? check_vector_lower_record(move, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, v0) :
                                          check_vector_lower_record(move, v0, 0, BUSTER_TIER0_ROLE_INTEGER_RESULT);
        r += 1;
        needed |= shape.element <= 2 ? BUSTER_X86_FEATURE(AVX512BW) : 0;
        if (forms.forms[0] == 0)
        {
            reason = builtin == CBUILTIN_REDUCE_MUL ? "vector multiplication of 8-bit elements" : "this vector reduction";
        }
        else if ((target & needed) != needed)
        {
            reason = check_vector_builtin_feature_reason(builtin, needed, target);
        }
        else
        {
            // One row where the halvings fit with the move; else the first takes four halvings (8 records), its result
            // the 16 bytes left, and the second the rest.
            const bool single = r <= BUSTER_TIER0_OPERATION_RECORDS;
            const uint32_t taken = single ? r : 2 * 4;
            const uint32_t left = (shape.bytes >> 4) >= 16 ? shape.bytes >> 4 : 16;
            memcpy(rows[0].records, records, taken * sizeof(*records));
            rows[0].record_count = (uint8_t)taken;
            rows[0].operand_count = 1;
            rows[0].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            rows[0].operand_bytes[0] = (uint8_t)shape.bytes;
            rows[0].result_class = single ? result : BUSTER_TYPED_CLASS_VECTOR;
            rows[0].result_bytes = single ? 0 : (uint8_t)left;
            *count = 1;
            if (!single)
            {
                memcpy(rows[1].records, records + taken, (r - taken) * sizeof(*records));
                rows[1].record_count = (uint8_t)(r - taken);
                rows[1].operand_count = 1;
                rows[1].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                rows[1].operand_bytes[0] = (uint8_t)left;
                rows[1].result_class = result;
                *count = 2;
            }
        }
    }
    return reason;
}
