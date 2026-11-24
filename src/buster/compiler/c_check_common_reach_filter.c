// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The reach scan's filter (#1340), included after the candidates' classes. The scan (check_common_next_reach_op) lists
// its candidates by operator alone and check_common_own_reach tests each one: on sqlite3.c 78,304 nodes for 2,292 with a
// bit. This filter takes out of each block of 16 candidates the nodes whose bits can only be zero, before they are
// listed. It is no second implementation of a node's bits: check_common_own_reach stays the only verdict, asked of every
// node the filter keeps, and what the filter gives is a necessary condition of each family of that test, read off its
// code (the derivations are at check_common_reach_keep). A node it wrongly kept costs a visit; a node it wrongly took out
// would lose a warning, which the audit (CheckCommonReachAudit) and the candidates' tests look for.
//
// What it reads, each once a unit:
//   - a trait byte a node (check_common_reach_traits), 64 nodes at a time, from its operator and its constant kind;
//   - a class byte a type (check_common_reach_classes), 64 types at a time, from the type table's kinds;
//   - a count of the argument slots before each one that could matter (check_common_reach_arguments), 16 slots at a
//     time, so that a call is kept where its slice holds one.
// Then, a block of candidates at a time, the traits of their operands (gathers), the classes of the types a family
// reads, and, for the conversion edges alone, the rows of the nodes that have a constant, a conditional, a comma or a
// selection operand.

// A node's traits: what the exact tests ask of a node read as an operand, a source or an argument.
// Its constant kind is CHECK_CONSTANT_INTEGER.
#define BUSTER_CHECK_TRAIT_INTEGER 0x01u
// Its constant kind is CHECK_CONSTANT_FLOATING.
#define BUSTER_CHECK_TRAIT_FLOATING 0x02u
// A conditional or a comma (check_common_conversion_arms).
#define BUSTER_CHECK_TRAIT_ARMS 0x04u
// A `<<`, or an implicit conversion that check_common_bool_candidate, check_common_zero_divisor and
// check_common_null_dereference pass to reach their operand: what lies under it isn't read here.
#define BUSTER_CHECK_TRAIT_PEELED 0x08u
// A static selection.
#define BUSTER_CHECK_TRAIT_SELECT 0x10u
// An explicit cast, which check_common_zero_divisor and check_common_null_dereference pass.
#define BUSTER_CHECK_TRAIT_CAST 0x20u
// A leaf (check_common_self_assign_candidate).
#define BUSTER_CHECK_TRAIT_VALUE 0x40u
// Its constant kind is CHECK_CONSTANT_ADDRESS (check_common_zero_divisor's const object).
#define BUSTER_CHECK_TRAIT_ADDRESS 0x80u

static const uint8_t check_common_reach_operator_traits[128] =
{
    [CHECK_OP_VALUE] = BUSTER_CHECK_TRAIT_VALUE,
    [CHECK_OP_SHIFT_LEFT] = BUSTER_CHECK_TRAIT_PEELED,
    [CHECK_OP_CONDITIONAL] = BUSTER_CHECK_TRAIT_ARMS,
    [CHECK_OP_CAST] = BUSTER_CHECK_TRAIT_CAST,
    [CHECK_OP_COMMA] = BUSTER_CHECK_TRAIT_ARMS,
    [CHECK_OP_CONVERT] = BUSTER_CHECK_TRAIT_PEELED,
    [CHECK_OP_SELECT] = BUSTER_CHECK_TRAIT_SELECT,
};
_Static_assert(CHECK_OP_COUNT <= BUSTER_ARRAY_LENGTH(check_common_reach_operator_traits), "the operators' traits fit two byte-permute tables");

// A constant kind's traits, a 16-byte shuffle table in each 128-bit lane.
#define BUSTER_CHECK_REACH_CONSTANT_TRAITS \
    [CHECK_CONSTANT_INTEGER] = BUSTER_CHECK_TRAIT_INTEGER, [CHECK_CONSTANT_FLOATING] = BUSTER_CHECK_TRAIT_FLOATING, [CHECK_CONSTANT_ADDRESS] = BUSTER_CHECK_TRAIT_ADDRESS
static const uint8_t check_common_reach_constant_traits[16] = { BUSTER_CHECK_REACH_CONSTANT_TRAITS };
#undef BUSTER_CHECK_REACH_CONSTANT_TRAITS
_Static_assert(CHECK_CONSTANT_LABEL_DIFFERENCE < 16, "the constant kinds index one shuffle lane");

// A type's classes, by the kind its TypeId's index has: what a test may find past type_strip_aligned and
// type_unqualified. A wrapper (ALIGNED, QUALIFIED), whose kind hides its base's, and a kind this table doesn't know have
// every class.
#define BUSTER_CHECK_CLASS_FLOATING 0x01u
#define BUSTER_CHECK_CLASS_ENUM 0x02u
#define BUSTER_CHECK_CLASS_ARRAY 0x04u
#define BUSTER_CHECK_CLASS_NULLPTR 0x08u
#define BUSTER_CHECK_CLASS_ALL 0xffu

static const uint8_t check_common_reach_kind_classes[64] =
{
    [BUSTER_TYPE_KIND_FLOATING] = BUSTER_CHECK_CLASS_FLOATING,
    [BUSTER_TYPE_KIND_COMPLEX] = BUSTER_CHECK_CLASS_FLOATING,
    [BUSTER_TYPE_KIND_ENUM] = BUSTER_CHECK_CLASS_ENUM,
    [BUSTER_TYPE_KIND_ARRAY] = BUSTER_CHECK_CLASS_ARRAY,
    [BUSTER_TYPE_KIND_NULLPTR] = BUSTER_CHECK_CLASS_NULLPTR,
    [BUSTER_TYPE_KIND_ALIGNED] = BUSTER_CHECK_CLASS_ALL,
    [BUSTER_TYPE_KIND_QUALIFIED] = BUSTER_CHECK_CLASS_ALL,
};
_Static_assert(BUSTER_TYPE_KIND_COUNT <= BUSTER_ARRAY_LENGTH(check_common_reach_kind_classes), "the kinds' classes fit one byte-permute table");
_Static_assert(BUSTER_TYPE_KIND_WRAPPERS == BUSTER_TYPE_KIND_ALIGNED && BUSTER_TYPE_KIND_COUNT == BUSTER_TYPE_KIND_QUALIFIED + 1, "the wrappers are ALIGNED and QUALIFIED");

// An operator's families: which of check_common_own_reach's tests can give it a bit, apart from its conversion edges',
// which every operator has. No family at all (0) is an operator this filter derives nothing for and keeps: a shift
// (a bit wherever its count is a constant), a source comma, a conditional (its arms' edges), a selection, a member (asked
// only where -Watomic-access may report), the builtins, and anything else the scan may list.
// An assignment, whose source is its second operand.
#define BUSTER_CHECK_FAMILY_SOURCE1 0x01u
// An initialization or an implicit conversion, whose source is its first.
#define BUSTER_CHECK_FAMILY_SOURCE0 0x02u
#define BUSTER_CHECK_FAMILY_CALL 0x04u
#define BUSTER_CHECK_FAMILY_SUBSCRIPT 0x08u
// `!`, `&&`, `||` and a statement's condition.
#define BUSTER_CHECK_FAMILY_TRUTH 0x10u
#define BUSTER_CHECK_FAMILY_DIVISION 0x20u
#define BUSTER_CHECK_FAMILY_DEREFERENCE 0x40u
// No test but the conversion edges': a unary sign, `+`, `-`, `*`, a comparison, `+=`, `-=`, `*=`.
#define BUSTER_CHECK_FAMILY_EDGES 0x80u

static const uint8_t check_common_reach_families[128] =
{
    [CHECK_OP_POSITIVE] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_NEGATIVE] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_NOT] = BUSTER_CHECK_FAMILY_TRUTH,
    [CHECK_OP_ADD] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_SUBTRACT] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_MULTIPLY] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_DIVIDE] = BUSTER_CHECK_FAMILY_DIVISION,
    [CHECK_OP_REMAINDER] = BUSTER_CHECK_FAMILY_DIVISION,
    [CHECK_OP_LESS] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_LESS_EQUAL] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_GREATER] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_GREATER_EQUAL] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_EQUAL] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_NOT_EQUAL] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_LOGICAL_AND] = BUSTER_CHECK_FAMILY_TRUTH,
    [CHECK_OP_LOGICAL_OR] = BUSTER_CHECK_FAMILY_TRUTH,
    [CHECK_OP_DEREFERENCE] = BUSTER_CHECK_FAMILY_DEREFERENCE,
    [CHECK_OP_ASSIGN] = BUSTER_CHECK_FAMILY_SOURCE1,
    [CHECK_OP_SUBSCRIPT] = BUSTER_CHECK_FAMILY_SUBSCRIPT,
    [CHECK_OP_CALL] = BUSTER_CHECK_FAMILY_CALL,
    [CHECK_OP_INITIALIZE] = BUSTER_CHECK_FAMILY_SOURCE0,
    [CHECK_OP_CONVERT] = BUSTER_CHECK_FAMILY_SOURCE0,
    [CHECK_OP_CONDITION] = BUSTER_CHECK_FAMILY_TRUTH,
    [CHECK_OP_ADD_ASSIGN] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_SUBTRACT_ASSIGN] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_MULTIPLY_ASSIGN] = BUSTER_CHECK_FAMILY_EDGES,
    [CHECK_OP_DIVIDE_ASSIGN] = BUSTER_CHECK_FAMILY_DIVISION,
    [CHECK_OP_REMAINDER_ASSIGN] = BUSTER_CHECK_FAMILY_DIVISION,
};
_Static_assert(CHECK_OP_COUNT <= BUSTER_ARRAY_LENGTH(check_common_reach_families), "the operators' families fit two byte-permute tables");

// The traits each family's test asks of the operand it reads (check_common_reach_keep).
// A source's traits but an integer constant's, whose conversion alone the widths decide (check_common_reach_keep).
#define BUSTER_CHECK_REACH_ARM_TRAITS (BUSTER_CHECK_TRAIT_FLOATING | BUSTER_CHECK_TRAIT_ARMS | BUSTER_CHECK_TRAIT_PEELED | BUSTER_CHECK_TRAIT_SELECT)
#define BUSTER_CHECK_REACH_TRUTH_TRAITS (BUSTER_CHECK_TRAIT_FLOATING | BUSTER_CHECK_TRAIT_ARMS | BUSTER_CHECK_TRAIT_PEELED | BUSTER_CHECK_TRAIT_SELECT)
#define BUSTER_CHECK_REACH_EDGE_TRAITS (BUSTER_CHECK_TRAIT_INTEGER | BUSTER_CHECK_TRAIT_ARMS | BUSTER_CHECK_TRAIT_SELECT)
#define BUSTER_CHECK_REACH_DIVISOR_TRAITS (BUSTER_CHECK_TRAIT_INTEGER | BUSTER_CHECK_TRAIT_CAST | BUSTER_CHECK_TRAIT_PEELED | BUSTER_CHECK_TRAIT_ADDRESS)
#define BUSTER_CHECK_REACH_POINTER_TRAITS (BUSTER_CHECK_TRAIT_INTEGER | BUSTER_CHECK_TRAIT_CAST | BUSTER_CHECK_TRAIT_PEELED)

typedef struct CheckCommonReachFilter CheckCommonReachFilter;
struct CheckCommonReachFilter
{
    const CheckGraph* graph;
    const CheckStream* stream;
    // A trait byte a node, and three zero bytes past the last, so that a gather's four bytes stay inside. Null where the
    // filter doesn't apply (check_common_reach_filter_build) and the scan lists every candidate.
    const uint8_t* traits;
    // A class byte a type, and three bytes past the last.
    const uint8_t* classes;
    // The argument slots before each one that could matter, and after the last: argument_count + 1 counts.
    const uint32_t* marked;
    uint32_t count;
    uint32_t type_count;
    uint32_t argument_count;
    // An implicit conversion between enumerations may report (CheckCommonEnabled.enum_cast).
    bool enum_cast;
    // -Wself-assign may report: the facts mark a plain `x = y` from the scan's list (check_common_self_assign_candidate).
    bool self_assign;
    // -Wimplicit-const-int-float-conversion may report, the conversion edges' only warning that the filter is built
    // under.
    bool edges;
    // The operators' families under the unit's options (check_common_reach_filter_build).
    uint8_t families[128];
    // A builtin type's width as check_common_integer gives it, by its index, 0 where it isn't an integer type; and the
    // lesser of that and its default argument promotion's, 0 where either isn't one.
    uint8_t widths[BUSTER_TYPE_BUILTIN_COUNT];
    uint8_t promoted[BUSTER_TYPE_BUILTIN_COUNT];
};
_Static_assert(BUSTER_TYPE_BUILTIN_COUNT == 64, "the builtin types' widths fit one byte-permute table");

// The reach scan's audit, a test's (BUSTER_TEST_COMMON_REACH_AUDIT, read once a unit by check_driver_common_warnings):
// check_common_facts then lists every candidate the operators' classes give, as it did before the filter, asks
// check_common_own_reach of each, and fails the unit where a node the filter took out has a bit or is a self-assignment
// candidate. Where the variable names a file, each unit appends its counts to it: the candidates, the nodes the filter
// kept, the nodes with a bit, and the nodes wrongly taken out.
typedef struct CheckCommonReachAudit CheckCommonReachAudit;
struct CheckCommonReachAudit
{
    const char* path;
    bool enabled;
};
static _Thread_local CheckCommonReachAudit check_common_reach_audit;

// Every node's trait byte into `traits` (`count` + 3 bytes, the last three zero), 64 nodes at a time: four operator
// vectors narrowed to bytes and looked up, as check_common_candidates_build has them, and the nodes' constant kinds through
// a shuffle. An operator or a constant kind outside its table has no trait, as no test names it.
static void check_common_reach_traits(const CheckGraph* graph, const CheckStream* stream, uint8_t* traits)
{
    const uint32_t count = graph->count;
    const CheckOp* operators = graph->ops;
    const uint8_t* constants = stream->value_constants;
    const __m512i low = _mm512_loadu_si512(check_common_reach_operator_traits);
    const __m512i high = _mm512_loadu_si512(check_common_reach_operator_traits + 64);
    const __m512i kinds = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i*)check_common_reach_constant_traits));
    const __m512i limit = _mm512_set1_epi32(CHECK_OP_COUNT);
    uint32_t first = 0;
    for (; count - first >= 64; first += 64)
    {
        const __m512i a = _mm512_loadu_si512(operators + first);
        const __m512i b = _mm512_loadu_si512(operators + first + 16);
        const __m512i c = _mm512_loadu_si512(operators + first + 32);
        const __m512i d = _mm512_loadu_si512(operators + first + 48);
        const __mmask64 known = (__mmask64)_mm512_cmplt_epu32_mask(a, limit) | (__mmask64)_mm512_cmplt_epu32_mask(b, limit) << 16 |
                                (__mmask64)_mm512_cmplt_epu32_mask(c, limit) << 32 | (__mmask64)_mm512_cmplt_epu32_mask(d, limit) << 48;
        __m512i indices = _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(a));
        indices = _mm512_inserti32x4(indices, _mm512_cvtepi32_epi8(b), 1);
        indices = _mm512_inserti32x4(indices, _mm512_cvtepi32_epi8(c), 2);
        indices = _mm512_inserti32x4(indices, _mm512_cvtepi32_epi8(d), 3);
        const __m512i constant = _mm512_loadu_si512(constants + first);
        const __m512i by_operator = _mm512_maskz_permutex2var_epi8(known, low, indices, high);
        const __m512i by_constant = _mm512_maskz_shuffle_epi8(_mm512_cmplt_epu8_mask(constant, _mm512_set1_epi8(16)), kinds, constant);
        _mm512_storeu_si512(traits + first, _mm512_or_si512(by_operator, by_constant));
    }
    for (; first < count; first += 16)
    {
        const uint32_t live = count - first < 16 ? count - first : 16;
        const __mmask16 lanes = (__mmask16)(live == 16 ? 0xffffu : (1u << live) - 1);
        const __m512i ops = _mm512_maskz_loadu_epi32(lanes, operators + first);
        const __mmask16 known = _mm512_mask_cmplt_epu32_mask(lanes, ops, limit);
        const __m128i constant = _mm_maskz_loadu_epi8(lanes, constants + first);
        const __m128i by_operator = _mm512_castsi512_si128(_mm512_maskz_permutex2var_epi8((__mmask64)known, low, _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(ops)), high));
        const __m128i by_constant = _mm_maskz_shuffle_epi8(_mm_cmplt_epu8_mask(constant, _mm_set1_epi8(16)), _mm512_castsi512_si128(kinds), constant);
        _mm_mask_storeu_epi8(traits + first, lanes, _mm_or_si128(by_operator, by_constant));
    }
    memset(traits + count, 0, 3);
}

// Every type's class byte into `classes` (the table's count + 3 bytes), 64 types at a time: one byte permute of the
// kinds. A kind outside the table has every class.
static void check_common_reach_type_classes(const TypeTable* types, uint8_t* classes)
{
    const uint32_t count = types->types.count;
    const uint8_t* kinds = types->types.kind;
    const __m512i table = _mm512_loadu_si512(check_common_reach_kind_classes);
    const __m512i every = _mm512_set1_epi8((char)BUSTER_CHECK_CLASS_ALL);
    for (uint32_t first = 0; first < count; first += 64)
    {
        const uint32_t live = count - first < 64 ? count - first : 64;
        const __mmask64 lanes = live == 64 ? (__mmask64)UINT64_MAX : (__mmask64)((UINT64_C(1) << live) - 1);
        const __m512i kind = _mm512_maskz_loadu_epi8(lanes, kinds + first);
        const __mmask64 known = _mm512_cmplt_epu8_mask(kind, _mm512_set1_epi8(BUSTER_TYPE_KIND_COUNT));
        _mm512_mask_storeu_epi8(classes + first, lanes, _mm512_mask_permutexvar_epi8(every, known, kind, table));
    }
    memset(classes + count, 0, 3);
}

// The classes of 16 TypeIds (`lanes` of them asked), in each lane's low byte: a gather of the types' class bytes. A type
// outside the table, and a lane not asked, has every class.
static inline __attribute__((always_inline)) __m512i check_common_reach_classes(const CheckCommonReachFilter* filter, __mmask16 lanes, __m512i types)
{
    const __m512i indices = _mm512_srli_epi32(types, BUSTER_TYPE_INDEX_SHIFT);
    const __mmask16 inside = _mm512_mask_cmplt_epu32_mask(lanes, indices, _mm512_set1_epi32((int)filter->type_count));
    return _mm512_mask_i32gather_epi32(_mm512_set1_epi32(BUSTER_CHECK_CLASS_ALL), inside, indices, filter->classes, 1);
}

// The traits of 16 nodes (`lanes` of them asked, each below the filter's count), in each lane's low byte; no trait for a
// lane not asked.
static inline __attribute__((always_inline)) __m512i check_common_reach_node_traits(const CheckCommonReachFilter* filter, __mmask16 lanes, __m512i nodes)
{
    return _mm512_and_si512(_mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, nodes, filter->traits, 1), _mm512_set1_epi32(0xff));
}

// The widths of 16 TypeIds (`lanes` of them asked) from `table`, the filter's widths or its promoted ones: an in-register
// lookup by the type's index. 0 for a type that isn't a builtin one, whose width isn't known here, and for a lane not asked.
static inline __attribute__((always_inline)) __m512i check_common_reach_widths(const uint8_t* table, __mmask16 lanes, __m512i types)
{
    const __m512i indices = _mm512_srli_epi32(types, BUSTER_TYPE_INDEX_SHIFT);
    const __mmask16 builtin = _mm512_mask_cmplt_epu32_mask(lanes, indices, _mm512_set1_epi32(BUSTER_TYPE_BUILTIN_COUNT));
    const __m512i bytes = _mm512_permutexvar_epi8(_mm512_zextsi128_si512(_mm512_cvtepi32_epi8(indices)), _mm512_loadu_si512(table));
    return _mm512_maskz_cvtepu8_epi32(builtin, _mm512_castsi512_si128(bytes));
}

// The argument slots that could matter, counted: `marked[i]` is how many of the slots before `i` could give their call a
// bit (argument_count + 1 counts), 16 slots at a time, each block's marks summed across its lanes in four steps. Which
// slots could matter is check_common_reach_keep's derivation for a call.
static void check_common_reach_arguments(const CheckCommonReachFilter* filter, uint32_t* marked)
{
    const CheckGraph* graph = filter->graph;
    const CheckStream* stream = filter->stream;
    const uint32_t count = filter->argument_count;
    const __m512i limit = _mm512_set1_epi32((int)filter->count);
    const __m512i even = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
    const __m512i zero = _mm512_setzero_si512();
    _Static_assert(sizeof(CheckConversion) == 8 && offsetof(CheckConversion, type) == 0, "an argument's conversion is its type's dword and its flags'");
    uint32_t total = 0;
    marked[0] = 0;
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t live = count - first < 16 ? count - first : 16;
        const __mmask16 lanes = (__mmask16)(live == 16 ? 0xffffu : (1u << live) - 1);
        const __m512i arguments = _mm512_maskz_loadu_epi32(lanes, graph->argument_nodes + first);
        const __mmask16 inside = _mm512_mask_cmplt_epu32_mask(lanes, arguments, limit);
        const __m512i traits = check_common_reach_node_traits(filter, inside, arguments);
        __mmask16 marks = _mm512_mask_test_epi32_mask(inside, traits, _mm512_set1_epi32(BUSTER_CHECK_REACH_ARM_TRAITS));
        const __mmask16 constants = _mm512_mask_test_epi32_mask(inside, traits, _mm512_set1_epi32(BUSTER_CHECK_TRAIT_INTEGER)) & (__mmask16)~marks;
        const __mmask16 edges = filter->edges ? _mm512_mask_test_epi32_mask(inside, traits, _mm512_set1_epi32(BUSTER_CHECK_REACH_EDGE_TRAITS)) & (__mmask16)~marks : 0;
        const __mmask16 typed = constants | edges | (filter->enum_cast ? inside & (__mmask16)~marks : 0);
        if (typed != 0)
        {
            // The slots' targets: the even dwords of their conversions.
            const __mmask16 low_lanes = (__mmask16)(live >= 8 ? 0xffffu : (1u << (live * 2)) - 1);
            const __mmask16 high_lanes = (__mmask16)(live <= 8 ? 0u : live == 16 ? 0xffffu : (1u << ((live - 8) * 2)) - 1);
            const __m512i low = _mm512_maskz_loadu_epi32(low_lanes, stream->argument_conversions + first);
            const __m512i high = _mm512_maskz_loadu_epi32(high_lanes, stream->argument_conversions + first + 8);
            const __m512i targets = _mm512_permutex2var_epi32(low, even, high);
            if (constants != 0)
            {
                const __m512i source = check_common_reach_widths(filter->widths, constants, _mm512_mask_i32gather_epi32(zero, constants, arguments, stream->value_types, 4));
                const __m512i target = check_common_reach_widths(filter->promoted, constants, targets);
                const __mmask16 widening = _mm512_mask_cmpge_epu32_mask(_mm512_mask_cmpneq_epi32_mask(constants, source, zero), target, source);
                marks |= constants & (__mmask16)~widening;
            }
            const __mmask16 classed = (edges | (filter->enum_cast ? inside : 0)) & (__mmask16)~marks;
            if (classed != 0)
            {
                const __m512i classes = check_common_reach_classes(filter, classed, targets);
                marks |= _mm512_mask_test_epi32_mask(edges & classed, classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_FLOATING));
                if (filter->enum_cast)
                {
                    marks |= _mm512_mask_test_epi32_mask(classed, classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_ENUM));
                }
            }
        }
        // Each lane's count of the marks at and before it: a sum over 1, 2, 4 and 8 lanes back.
        __m512i sums = _mm512_maskz_set1_epi32(marks, 1);
        sums = _mm512_add_epi32(sums, _mm512_alignr_epi32(sums, zero, 15));
        sums = _mm512_add_epi32(sums, _mm512_alignr_epi32(sums, zero, 14));
        sums = _mm512_add_epi32(sums, _mm512_alignr_epi32(sums, zero, 12));
        sums = _mm512_add_epi32(sums, _mm512_alignr_epi32(sums, zero, 8));
        _mm512_mask_storeu_epi32(marked + first + 1, lanes, _mm512_add_epi32(sums, _mm512_set1_epi32((int)total)));
        total += (uint32_t)__builtin_popcount(marks);
    }
}

// The filter for a unit's reach scan into `filter`; false where scratch can't hold its tables. It applies (its traits
// aren't null) to a stream of columns with its conversions' rows, as check_graph_run's is: a hand-built stream of whole
// values (CheckStream.dense_values) keeps the scan's every candidate. So does a unit where -Wdouble-promotion or
// -Wimplicit-int-float-conversion may report, whose edges any operand of a floating or an integer type may have.
// The other options take their operators' families away, and with them the filter's say:
//   - -Wsign-compare gives every comparison a bit;
//   - -Wimplicit-float-conversion reads an assignment's, an initialization's, a conversion's and a compound assignment's
//     types and a call's arguments' (check_float_narrows), whatever their operands are;
//   - a graph or a stream without its argument columns has no call's test here.
static bool check_common_reach_filter_build(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const CheckCommonEnabled* enabled, Arena* scratch,
                                            CheckCommonReachFilter* filter)
{
    const uint32_t count = graph->count;
    // A gather's index is a signed dword: a node's row of conversions is at eight dwords a node.
    const uint32_t most = 1u << 27;
    const bool arguments = graph->argument_offsets != NULL && graph->argument_counts != NULL && graph->argument_nodes != NULL && stream->argument_conversions != NULL &&
                           graph->argument_count < most;
    const bool applies = count != 0 && count < most && stream->count == count && stream->dense_values == NULL && stream->value_types != NULL &&
                         stream->value_constants != NULL && stream->value_flags != NULL && stream->conversions != NULL && types->types.kind != NULL && types->types.count != 0 &&
                         !enabled->promotion && !enabled->int_float;
    bool result = true;
    *filter = (CheckCommonReachFilter)
    {
        .graph = graph,
        .stream = stream,
        .count = count,
        .type_count = types->types.count,
        .argument_count = arguments ? graph->argument_count : 0,
        .enum_cast = enabled->enum_cast,
        .self_assign = enabled->self_assign,
        .edges = enabled->const_int_float,
    };
    if (applies)
    {
        uint8_t* traits = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 3);
        uint8_t* classes = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, (uint64_t)types->types.count + 3);
        uint32_t* marked = arguments ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, (uint64_t)graph->argument_count + 1) : NULL;
        result = traits != NULL && classes != NULL && (marked != NULL || !arguments);
        if (result)
        {
            memcpy(filter->families, check_common_reach_families, sizeof(filter->families));
            if (enabled->sign_compare)
            {
                memset(filter->families + CHECK_OP_LESS, 0, CHECK_OP_NOT_EQUAL - CHECK_OP_LESS + 1);
            }
            if (enabled->float_precision)
            {
                filter->families[CHECK_OP_ASSIGN] = 0;
                filter->families[CHECK_OP_INITIALIZE] = 0;
                filter->families[CHECK_OP_CONVERT] = 0;
                filter->families[CHECK_OP_CALL] = 0;
                memset(filter->families + CHECK_OP_ADD_ASSIGN, 0, CHECK_OP_DIVIDE_ASSIGN - CHECK_OP_ADD_ASSIGN + 1);
            }
            if (!arguments)
            {
                filter->families[CHECK_OP_CALL] = 0;
            }
            // The builtin types' widths, from the functions the exact test asks, so that the tables can't say other than
            // they do: 64 types, a unit's only scalar step here.
            for (uint32_t index = 0; index < BUSTER_TYPE_BUILTIN_COUNT && index < types->types.count; index += 1)
            {
                uint32_t width = 0;
                uint32_t promoted = 0;
                bool is_signed = false;
                const bool integer = check_common_integer(types, BUSTER_TYPE_BUILTIN(index), &width, &is_signed);
                const bool promotes = integer && check_common_integer(types, type_default_argument_promote(types, BUSTER_TYPE_BUILTIN(index)), &promoted, &is_signed);
                filter->widths[index] = integer ? (uint8_t)width : 0;
                filter->promoted[index] = promotes ? (uint8_t)(promoted < width ? promoted : width) : 0;
            }
            check_common_reach_traits(graph, stream, traits);
            check_common_reach_type_classes(types, classes);
            filter->traits = traits;
            filter->classes = classes;
            if (arguments)
            {
                check_common_reach_arguments(filter, marked);
                filter->marked = marked;
            }
        }
    }
    return result;
}

// Of the 16 nodes from `first`, the candidates `selected` that may have a bit of their own reach: the others'
// check_common_own_reach is zero, and they aren't check_common_self_assign_candidate's. Family by family, each a necessary
// condition read off the exact test (a node's operand that isn't a node, BUSTER_CHECK_NONE or past the graph, gives no
// test a bit, and has no trait here):
//
//   - No family (the table's 0): kept.
//   - An assignment's, an initialization's, a conversion's source (check_common_candidate_bits' first case): the test
//     gives its bit through check_common_conversion_source (an integer or floating constant, a conditional or a comma),
//     through check_common_bool_candidate (a `<<` or a conditional, past conversions and selections), through
//     check_common_enum_cast_edge, whose target, the node's own type, has an enumeration's or an aligned typedef's kind,
//     or through check_float_narrows, which -Wimplicit-float-conversion alone asks (the family is gone then). So: its
//     source has a source's trait, or enumerations may report and its own type has an enumeration's class. And, where
//     -Wself-assign may report, a plain `x = y`, two leaves, is kept for the facts' mark.
//   - A call (its second case): each argument's bit comes the same four ways, its target the argument's conversion's
//     type. So: a slot of its slice is marked (check_common_reach_arguments); a slice outside the columns is kept.
//   - A subscript: an integer constant index (check_common_integer_constant) and a base whose type is an array's.
//   - `!`, `&&`, `||`, a condition (check_common_truth_candidate): one of the operands it reads, its own node where it is a
//     static assertion's, its first and its second, is check_common_bool_candidate's or a floating constant.
//   - A division: its divisor is an integer constant, or check_common_zero_divisor finds one, starting from a cast, a
//     conversion or a const object's read, an address constant.
//   - A dereference (check_common_null_dereference): its operand is an integer constant, or a cast or a conversion (what
//     lies under them isn't read here), or its type is nullptr_t's.
//   - Any operator's conversion edges (check_common_edges_reach, where only -Wimplicit-const-int-float-conversion may
//     report): check_common_edge_wanted takes an operand's edge only where the operand is an integer constant, a
//     conditional, a comma or a selection, and the edge's target, in the node's row of conversions, is a real floating
//     type's. So: such an operand in a slot whose target has a floating type's class. The rows are read for the nodes
//     that have such an operand alone. A call's arguments' edges ask the same of the argument and its conversion's
//     type, and an argument with an edge's trait has a source's, so its slot is marked.
static __mmask16 check_common_reach_keep(const CheckCommonReachFilter* filter, uint32_t first, __mmask16 selected)
{
    const CheckGraph* graph = filter->graph;
    const CheckStream* stream = filter->stream;
    const __m512i zero = _mm512_setzero_si512();
    const __m512i limit = _mm512_set1_epi32((int)filter->count);
    const __m512i ops = _mm512_maskz_loadu_epi32(selected, graph->ops + first);
    const __mmask16 known = _mm512_mask_cmplt_epu32_mask(selected, ops, _mm512_set1_epi32(BUSTER_ARRAY_LENGTH(filter->families)));
    const __m512i table = _mm512_permutex2var_epi8(_mm512_loadu_si512(filter->families), _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(ops)),
                                                   _mm512_loadu_si512(filter->families + 64));
    const __m512i families = _mm512_maskz_cvtepu8_epi32(known, _mm512_castsi512_si128(table));
    const __m512i operand0 = _mm512_maskz_loadu_epi32(selected, graph->operand0 + first);
    const __m512i operand1 = _mm512_maskz_loadu_epi32(selected, graph->operand1 + first);
    const __mmask16 inside0 = _mm512_mask_cmplt_epu32_mask(selected, operand0, limit);
    const __mmask16 inside1 = _mm512_mask_cmplt_epu32_mask(selected, operand1, limit);
    const __m512i traits0 = check_common_reach_node_traits(filter, inside0, operand0);
    const __m512i traits1 = check_common_reach_node_traits(filter, inside1, operand1);
    __mmask16 kept = _mm512_mask_cmpeq_epi32_mask(selected, families, zero);
    // The sources.
    const __mmask16 source1 = _mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_SOURCE1));
    const __mmask16 source0 = _mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_SOURCE0));
    const __mmask16 sources = source0 | source1;
    kept |= _mm512_mask_test_epi32_mask(source1, traits1, _mm512_set1_epi32(BUSTER_CHECK_REACH_ARM_TRAITS)) |
            _mm512_mask_test_epi32_mask(source0, traits0, _mm512_set1_epi32(BUSTER_CHECK_REACH_ARM_TRAITS));
    const __mmask16 constants = (_mm512_mask_test_epi32_mask(source1, traits1, _mm512_set1_epi32(BUSTER_CHECK_TRAIT_INTEGER)) |
                                 _mm512_mask_test_epi32_mask(source0, traits0, _mm512_set1_epi32(BUSTER_CHECK_TRAIT_INTEGER))) & (__mmask16)~kept;
    if (constants != 0)
    {
        const __m512i source_types = _mm512_mask_i32gather_epi32(zero, constants, _mm512_mask_blend_epi32(source1, operand0, operand1), stream->value_types, 4);
        const __m512i source = check_common_reach_widths(filter->widths, constants, source_types);
        const __m512i target = check_common_reach_widths(filter->widths, constants, _mm512_maskz_loadu_epi32(constants, stream->value_types + first));
        const __mmask16 widening = _mm512_mask_cmpge_epu32_mask(_mm512_mask_cmpneq_epi32_mask(constants, source, zero), target, source);
        kept |= constants & (__mmask16)~widening;
        const __mmask16 fields = widening & source1 & inside0;
        if (fields != 0)
        {
            kept |= _mm512_mask_test_epi32_mask(fields, _mm512_mask_i32gather_epi32(zero, fields, operand0, stream->value_flags, 4), _mm512_set1_epi32(BUSTER_CHECK_BIT_FIELD));
        }
    }
    if (filter->self_assign)
    {
        kept |= _mm512_mask_test_epi32_mask(source1, _mm512_and_si512(traits0, traits1), _mm512_set1_epi32(BUSTER_CHECK_TRAIT_VALUE));
    }
    if (filter->enum_cast && (sources & ~kept) != 0)
    {
        const __mmask16 asked = sources & ~kept;
        const __m512i classes = check_common_reach_classes(filter, asked, _mm512_maskz_loadu_epi32(asked, stream->value_types + first));
        kept |= _mm512_mask_test_epi32_mask(asked, classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_ENUM));
    }
    // The truth contexts and the divisions.
    const __m512i own = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(selected, filter->traits + first));
    kept |= _mm512_mask_test_epi32_mask(_mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_TRUTH)),
                                        _mm512_or_si512(_mm512_or_si512(traits0, traits1), own), _mm512_set1_epi32(BUSTER_CHECK_REACH_TRUTH_TRAITS));
    kept |= _mm512_mask_test_epi32_mask(_mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_DIVISION)), traits1,
                                        _mm512_set1_epi32(BUSTER_CHECK_REACH_DIVISOR_TRAITS));
    // The subscripts with a constant index, by their bases' types, and the dereferences, by their operands'.
    const __mmask16 subscripts = _mm512_mask_test_epi32_mask(_mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_SUBSCRIPT)) & inside0, traits1,
                                                             _mm512_set1_epi32(BUSTER_CHECK_TRAIT_INTEGER));
    const __mmask16 dereferences = _mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_DEREFERENCE));
    kept |= _mm512_mask_test_epi32_mask(dereferences, traits0, _mm512_set1_epi32(BUSTER_CHECK_REACH_POINTER_TRAITS));
    const __mmask16 typed = subscripts | (dereferences & inside0 & ~kept);
    if (typed != 0)
    {
        const __m512i types = _mm512_mask_i32gather_epi32(zero, typed, operand0, stream->value_types, 4);
        const __m512i classes = check_common_reach_classes(filter, typed, types);
        kept |= _mm512_mask_test_epi32_mask(subscripts, classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_ARRAY)) |
                _mm512_mask_test_epi32_mask(dereferences & typed, classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_NULLPTR));
    }
    // The calls, by their slices' marked slots.
    const __mmask16 calls = _mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_CALL));
    if (calls != 0)
    {
        const __m512i total = _mm512_set1_epi32((int)filter->argument_count);
        const __m512i offsets = _mm512_maskz_loadu_epi32(calls, graph->argument_offsets + first);
        const __m512i counts = _mm512_maskz_loadu_epi32(calls, graph->argument_counts + first);
        const __mmask16 held = _mm512_mask_cmple_epu32_mask(_mm512_mask_cmple_epu32_mask(calls, offsets, total), counts, _mm512_sub_epi32(total, offsets));
        const __m512i before = _mm512_mask_i32gather_epi32(zero, held, offsets, filter->marked, 4);
        const __m512i after = _mm512_mask_i32gather_epi32(zero, held, _mm512_add_epi32(offsets, counts), filter->marked, 4);
        kept |= (calls & ~held) | _mm512_mask_cmpneq_epi32_mask(held, before, after);
    }
    // The conversion edges of the nodes left, where an operand could have one.
    const __mmask16 left = filter->edges ? selected & ~kept : 0;
    if (left != 0)
    {
        const __m512i operand2 = _mm512_maskz_loadu_epi32(left, graph->operand2 + first);
        const __m512i traits2 = check_common_reach_node_traits(filter, _mm512_mask_cmplt_epu32_mask(left, operand2, limit), operand2);
        const __m512i edge = _mm512_set1_epi32(BUSTER_CHECK_REACH_EDGE_TRAITS);
        const __mmask16 slot0 = _mm512_mask_test_epi32_mask(left, traits0, edge);
        const __mmask16 slot1 = _mm512_mask_test_epi32_mask(left, traits1, edge);
        const __mmask16 slot2 = _mm512_mask_test_epi32_mask(left, traits2, edge);
        if ((slot0 | slot1 | slot2) != 0)
        {
            _Static_assert(sizeof(CheckNodeConversions) == 32 && sizeof(CheckConversion) == 8 && offsetof(CheckConversion, type) == 0 &&
                           offsetof(CheckNodeConversions, operands) == 0, "a node's three operand targets are dwords 0, 2 and 4 of its eight");
            const __m512i rows = _mm512_slli_epi32(_mm512_add_epi32(_mm512_set1_epi32((int)first), _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)), 3);
            const __m512i floating = _mm512_set1_epi32(BUSTER_CHECK_CLASS_FLOATING);
            const __m512i target0 = _mm512_mask_i32gather_epi32(zero, slot0, rows, stream->conversions, 4);
            const __m512i target1 = _mm512_mask_i32gather_epi32(zero, slot1, _mm512_add_epi32(rows, _mm512_set1_epi32(2)), stream->conversions, 4);
            const __m512i target2 = _mm512_mask_i32gather_epi32(zero, slot2, _mm512_add_epi32(rows, _mm512_set1_epi32(4)), stream->conversions, 4);
            kept |= _mm512_mask_test_epi32_mask(slot0, check_common_reach_classes(filter, slot0, target0), floating) |
                    _mm512_mask_test_epi32_mask(slot1, check_common_reach_classes(filter, slot1, target1), floating) |
                    _mm512_mask_test_epi32_mask(slot2, check_common_reach_classes(filter, slot2, target2), floating);
        }
    }
    return kept;
}
