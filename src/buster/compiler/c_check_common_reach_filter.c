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
// What it makes, each once a unit, over the facts' tree (each node's operands and arguments have it for their parent):
//   - a trait byte a node (check_common_reach_traits), 64 nodes at a time, from its operator and its constant kind, and
//     in the same pass a byte of what each node's children are, which the few nodes with a trait store at their parent;
//   - a class byte a type (check_common_reach_type_classes), 64 types at a time, from the type table's kinds;
//   - a bit a call whose argument slots hold one that could matter (check_common_reach_arguments).
// Then a block of candidates is sequential loads and compares: its operators' families from a permute, its own and its
// children's traits, and the traits and type of the node before each one, which is its last operand in a graph built in
// evaluation order (where it isn't, the children's byte answers).
//
// No gather: on Zen 5 a 16-lane vpgatherdd is 64 uops (docs/coding-style.md, "SIMD and Zen 5"). This filter's first
// form gathered each candidate's operands' traits, types and rows of conversions, a block at a time, and on the 9700X
// its blocks cost 10 M cycles of sqlite3.c's compile, as much as the scan it replaced
// (buster-performance, reach-scan-1340-2026-11-24). What is left to scalar loads is a lane at a time for the few lanes
// that need a type's class past the builtin types or a row of conversions, and the argument slots' pass.

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
// In the byte of a node's children, which takes no leaf's trait: one of them is a bit-field's lvalue
// (check_common_bitfield_assignment).
#define BUSTER_CHECK_CHILD_BIT_FIELD BUSTER_CHECK_TRAIT_VALUE

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
    // A trait byte a node, with two zero bytes before the first, the traits of the nodes before node 0. Null where the
    // filter doesn't apply (check_common_reach_filter_build) and the scan lists every candidate.
    const uint8_t* traits;
    // What each node's children are: the traits of its operands and arguments but a leaf's, and
    // BUSTER_CHECK_CHILD_BIT_FIELD.
    const uint8_t* children;
    // A class byte a type; 64 at least, so that the builtin types' are one register.
    const uint8_t* classes;
    // A bit a node: a call with an argument slot that could matter (check_common_reach_arguments). Null without the
    // argument columns.
    const uint64_t* marked;
    uint32_t count;
    uint32_t type_count;
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
_Static_assert(BUSTER_TYPE_BUILTIN_COUNT == 64, "the builtin types' widths and classes fit one byte-permute table each");

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

// Every node's trait byte into `traits` and what its children are into `children` (`count` + 1 bytes, the last for the
// nodes without a parent), 64 nodes at a time: four operator vectors narrowed to bytes and looked up, as
// check_common_candidates_build has them, the nodes' constant kinds through a shuffle, and their bit-field flags. An
// operator or a constant kind outside its table has no trait, as no test names it. The nodes with a trait other than a
// leaf's, or a bit-field's flag, each store theirs at their parent, a node at a time: a constant, a conditional, a
// comma, a `<<`, a conversion, a selection, a cast.
static void check_common_reach_traits(const CheckGraph* graph, const CheckStream* stream, const uint32_t* parents, uint8_t* traits, uint8_t* children)
{
    const uint32_t count = graph->count;
    const CheckOp* operators = graph->ops;
    const uint8_t* constants = stream->value_constants;
    const uint32_t* flags = stream->value_flags;
    const __m512i low = _mm512_loadu_si512(check_common_reach_operator_traits);
    const __m512i high = _mm512_loadu_si512(check_common_reach_operator_traits + 64);
    const __m512i kinds = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i*)check_common_reach_constant_traits));
    const __m512i limit = _mm512_set1_epi32(CHECK_OP_COUNT);
    const __m512i field = _mm512_set1_epi32(BUSTER_CHECK_BIT_FIELD);
    memset(children, 0, (uint64_t)count + 1);
    for (uint32_t first = 0; first < count; first += 64)
    {
        const uint32_t live = count - first < 64 ? count - first : 64;
        const __mmask64 lanes = live == 64 ? (__mmask64)UINT64_MAX : (__mmask64)((UINT64_C(1) << live) - 1);
        const __m512i a = _mm512_maskz_loadu_epi32((__mmask16)lanes, operators + first);
        const __m512i b = _mm512_maskz_loadu_epi32((__mmask16)(lanes >> 16), operators + first + 16);
        const __m512i c = _mm512_maskz_loadu_epi32((__mmask16)(lanes >> 32), operators + first + 32);
        const __m512i d = _mm512_maskz_loadu_epi32((__mmask16)(lanes >> 48), operators + first + 48);
        const __mmask64 known = (__mmask64)_mm512_cmplt_epu32_mask(a, limit) | (__mmask64)_mm512_cmplt_epu32_mask(b, limit) << 16 |
                                (__mmask64)_mm512_cmplt_epu32_mask(c, limit) << 32 | (__mmask64)_mm512_cmplt_epu32_mask(d, limit) << 48;
        __m512i indices = _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(a));
        indices = _mm512_inserti32x4(indices, _mm512_cvtepi32_epi8(b), 1);
        indices = _mm512_inserti32x4(indices, _mm512_cvtepi32_epi8(c), 2);
        indices = _mm512_inserti32x4(indices, _mm512_cvtepi32_epi8(d), 3);
        const __m512i constant = _mm512_maskz_loadu_epi8(lanes, constants + first);
        const __m512i by_operator = _mm512_maskz_permutex2var_epi8(known & lanes, low, indices, high);
        const __m512i by_constant = _mm512_maskz_shuffle_epi8(_mm512_cmplt_epu8_mask(constant, _mm512_set1_epi8(16)) & lanes, kinds, constant);
        const __m512i own = _mm512_or_si512(by_operator, by_constant);
        _mm512_mask_storeu_epi8(traits + first, lanes, own);
        const __mmask64 fields = (__mmask64)_mm512_test_epi32_mask(_mm512_maskz_loadu_epi32((__mmask16)lanes, flags + first), field) |
                                 (__mmask64)_mm512_test_epi32_mask(_mm512_maskz_loadu_epi32((__mmask16)(lanes >> 16), flags + first + 16), field) << 16 |
                                 (__mmask64)_mm512_test_epi32_mask(_mm512_maskz_loadu_epi32((__mmask16)(lanes >> 32), flags + first + 32), field) << 32 |
                                 (__mmask64)_mm512_test_epi32_mask(_mm512_maskz_loadu_epi32((__mmask16)(lanes >> 48), flags + first + 48), field) << 48;
        const __m512i told = _mm512_or_si512(_mm512_andnot_si512(_mm512_set1_epi8(BUSTER_CHECK_TRAIT_VALUE), own),
                                             _mm512_maskz_set1_epi8(fields, BUSTER_CHECK_CHILD_BIT_FIELD));
        uint8_t bytes[64];
        _mm512_storeu_si512(bytes, told);
        for (uint64_t pending = _mm512_test_epi8_mask(told, told); pending != 0; pending &= pending - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(pending);
            const uint32_t parent = parents[first + lane];
            children[parent < count ? parent : count] |= bytes[lane];
        }
    }
}

// Every type's class byte into `classes` (64 at least, the table's count or more), 64 types at a time: one byte permute
// of the kinds. A kind outside the table has every class, and so do the entries past the table's count.
static void check_common_reach_type_classes(const TypeTable* types, uint8_t* classes, uint32_t entries)
{
    const uint32_t count = types->types.count;
    const uint8_t* kinds = types->types.kind;
    const __m512i table = _mm512_loadu_si512(check_common_reach_kind_classes);
    const __m512i every = _mm512_set1_epi8((char)BUSTER_CHECK_CLASS_ALL);
    memset(classes, BUSTER_CHECK_CLASS_ALL, entries);
    for (uint32_t first = 0; first < count; first += 64)
    {
        const uint32_t live = count - first < 64 ? count - first : 64;
        const __mmask64 lanes = live == 64 ? (__mmask64)UINT64_MAX : (__mmask64)((UINT64_C(1) << live) - 1);
        const __m512i kind = _mm512_maskz_loadu_epi8(lanes, kinds + first);
        const __mmask64 known = _mm512_cmplt_epu8_mask(kind, _mm512_set1_epi8(BUSTER_TYPE_KIND_COUNT));
        _mm512_mask_storeu_epi8(classes + first, lanes, _mm512_mask_permutexvar_epi8(every, known, kind, table));
    }
}

// A TypeId's classes: its table's byte, and every class for one outside the table.
static inline __attribute__((always_inline)) uint32_t check_common_reach_class(const CheckCommonReachFilter* filter, TypeId type)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    return index < filter->type_count ? filter->classes[index] : BUSTER_CHECK_CLASS_ALL;
}

// Of 16 TypeIds, the lanes of `asked` whose type has one of the classes `classes`: the builtin types' by an in-register
// lookup of their index, the others' a load each.
static inline __attribute__((always_inline)) __mmask16 check_common_reach_classed(const CheckCommonReachFilter* filter, __mmask16 asked, __m512i types, uint32_t classes)
{
    const __m512i indices = _mm512_srli_epi32(types, BUSTER_TYPE_INDEX_SHIFT);
    const __mmask16 builtin = _mm512_mask_cmplt_epu32_mask(asked, indices, _mm512_set1_epi32(BUSTER_TYPE_BUILTIN_COUNT));
    const __m512i bytes = _mm512_permutexvar_epi8(_mm512_zextsi128_si512(_mm512_cvtepi32_epi8(indices)), _mm512_loadu_si512(filter->classes));
    __mmask16 result = _mm512_mask_test_epi32_mask(builtin, _mm512_cvtepu8_epi32(_mm512_castsi512_si128(bytes)), _mm512_set1_epi32((int)classes));
    TypeId each[16];
    _mm512_storeu_si512(each, types);
    for (uint32_t rest = asked & (__mmask16)~builtin; rest != 0; rest &= rest - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(rest);
        result |= (__mmask16)(((check_common_reach_class(filter, each[lane]) & classes) != 0 ? 1u : 0u) << lane);
    }
    return result;
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

// The calls with an argument slot that could matter, a bit each in `marked` (zeroed): a slot at a time, its argument's
// traits, its conversion's target and the call that reads it, the argument's parent. Which slots could matter is
// check_common_reach_keep's derivation for a call. Scalar: each slot's argument is anywhere before its call, so its
// trait is a load, and a 16-lane gather of them costs more than 16 loads do (the header).
static void check_common_reach_arguments(const CheckCommonReachFilter* filter, const uint32_t* parents, uint64_t* marked)
{
    const CheckGraph* graph = filter->graph;
    const CheckStream* stream = filter->stream;
    const uint32_t count = filter->count;
    for (uint32_t slot = 0; slot < graph->argument_count; slot += 1)
    {
        const uint32_t argument = graph->argument_nodes[slot];
        const uint32_t traits = argument < count ? filter->traits[argument] : 0;
        if (argument < count && (filter->enum_cast || (traits & (BUSTER_CHECK_TRAIT_INTEGER | BUSTER_CHECK_REACH_ARM_TRAITS)) != 0))
        {
            const TypeId target = stream->argument_conversions[slot].type;
            const uint32_t source_index = stream->value_types[argument] >> BUSTER_TYPE_INDEX_SHIFT;
            const uint32_t target_index = target >> BUSTER_TYPE_INDEX_SHIFT;
            const uint32_t source_width = source_index < BUSTER_TYPE_BUILTIN_COUNT ? filter->widths[source_index] : 0;
            const uint32_t target_width = target_index < BUSTER_TYPE_BUILTIN_COUNT ? filter->promoted[target_index] : 0;
            const uint32_t classes = check_common_reach_class(filter, target);
            const bool arms = (traits & BUSTER_CHECK_REACH_ARM_TRAITS) != 0;
            const bool narrowing = (traits & BUSTER_CHECK_TRAIT_INTEGER) != 0 && !(source_width != 0 && target_width >= source_width);
            const bool edge = filter->edges && (traits & BUSTER_CHECK_REACH_EDGE_TRAITS) != 0 && (classes & BUSTER_CHECK_CLASS_FLOATING) != 0;
            const bool enumeration = filter->enum_cast && (classes & BUSTER_CHECK_CLASS_ENUM) != 0;
            const uint32_t call = parents[argument];
            if ((arms || narrowing || edge || enumeration) && call < count)
            {
                marked[call >> 6] |= UINT64_C(1) << (call & 63);
            }
        }
    }
}

// The filter for a unit's reach scan into `filter`, over the facts' tree (`parents`, check_common_parents'); false where
// scratch can't hold its tables. It applies (its traits aren't null) to a stream of columns with its conversions' rows,
// as check_graph_run's is: a hand-built stream of whole values (CheckStream.dense_values) keeps the scan's every
// candidate. So does a unit where -Wdouble-promotion or -Wimplicit-int-float-conversion may report, whose edges any
// operand of a floating or an integer type may have. The other options take their operators' families away, and with
// them the filter's say:
//   - -Wsign-compare gives every comparison a bit;
//   - -Wimplicit-float-conversion reads an assignment's, an initialization's, a conversion's and a compound assignment's
//     types and a call's arguments' (check_float_narrows), whatever their operands are;
//   - a graph or a stream without its argument columns has no call's test here.
static bool check_common_reach_filter_build(const CheckGraph* graph, const CheckStream* stream, const TypeTable* types, const CheckCommonEnabled* enabled,
                                            const uint32_t* parents, Arena* scratch, CheckCommonReachFilter* filter)
{
    const uint32_t count = graph->count;
    const bool arguments = graph->argument_offsets != NULL && graph->argument_counts != NULL && graph->argument_nodes != NULL && stream->argument_conversions != NULL;
    const bool applies = count != 0 && stream->count == count && stream->dense_values == NULL && stream->value_types != NULL &&
                         stream->value_constants != NULL && stream->value_flags != NULL && stream->conversions != NULL && types->types.kind != NULL && types->types.count != 0 &&
                         !enabled->promotion && !enabled->int_float;
    bool result = true;
    *filter = (CheckCommonReachFilter)
    {
        .graph = graph,
        .stream = stream,
        .count = count,
        .type_count = types->types.count,
        .enum_cast = enabled->enum_cast,
        .self_assign = enabled->self_assign,
        .edges = enabled->const_int_float,
    };
    if (applies)
    {
        const uint32_t class_entries = types->types.count < BUSTER_TYPE_BUILTIN_COUNT ? BUSTER_TYPE_BUILTIN_COUNT : types->types.count;
        const uint64_t words = ((uint64_t)count + 63) / 64;
        uint8_t* traits = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 2);
        uint8_t* children = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
        uint8_t* classes = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, class_entries);
        uint64_t* marked = arguments ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, words) : NULL;
        result = traits != NULL && children != NULL && classes != NULL && (marked != NULL || !arguments);
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
            // they do: 64 types.
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
            traits[0] = 0;
            traits[1] = 0;
            check_common_reach_traits(graph, stream, parents, traits + 2, children);
            check_common_reach_type_classes(types, classes, class_entries);
            filter->traits = traits + 2;
            filter->children = children;
            filter->classes = classes;
            if (arguments)
            {
                memset(marked, 0, words * sizeof(uint64_t));
                check_common_reach_arguments(filter, parents, marked);
                filter->marked = marked;
            }
        }
    }
    return result;
}

// Of the 16 nodes from `first`, the candidates `selected` that may have a bit of their own reach: the others'
// check_common_own_reach is zero, and they aren't check_common_self_assign_candidate's. A test's operand is read two
// ways. Where it is the node before its reader, as a last operand is in a graph built in evaluation order, its traits
// and its type are the ones before the reader's in their columns. Otherwise the reader's children answer for it: in the
// facts' tree every operand and argument that is a node has its reader for parent, so what no child is, no operand is;
// an operand that isn't a node gives no test a bit. Family by family, each a necessary condition read off the exact test:
//
//   - No family (the table's 0): kept.
//   - An assignment's, an initialization's, a conversion's source (check_common_candidate_bits' first case): the test
//     gives its bit through check_common_conversion_source (an integer or floating constant, a conditional or a comma),
//     through check_common_bool_candidate (a `<<` or a conditional, past conversions and selections), through
//     check_common_enum_cast_edge, whose target, the node's own type, has an enumeration's or an aligned typedef's kind,
//     or through check_float_narrows, which -Wimplicit-float-conversion alone asks (the family is gone then). So: its
//     source is a floating constant, a conditional, a comma, a `<<`, a conversion or a selection; or enumerations may
//     report and its own type has an enumeration's class; or its source is an integer constant, whose bit is
//     check_common_constant_changes' or check_common_bitfield_assignment's. The first is false of a conversion between
//     integer types to one at least as wide, which the builtin types' widths tell where the source is the node before
//     (a type they don't know keeps the node); the second needs a bit-field's lvalue among an assignment's operands.
//     And, where -Wself-assign may report, a plain `x = y` is kept for the facts' mark: an assignment is taken out only
//     where the node before it is its second operand and no leaf, or the node before that is its first and no leaf.
//   - A call (its second case): each argument's bit comes the same ways, its target the argument's conversion's type,
//     or, for an unprototyped call, that type's promotion: the narrower of the two answers for both. So: the call is
//     marked (check_common_reach_arguments).
//   - A subscript: an integer constant index (check_common_integer_constant) and a base whose type is an array's.
//   - `!`, `&&`, `||`, a condition (check_common_truth_candidate): one of the operands it reads, its own node where it is a
//     static assertion's, its first and its second, is check_common_bool_candidate's or a floating constant. Its
//     operands' conversion edges are none of check_common_edges_reach's (check_common_edge_slot), and it has no arguments.
//   - A division: its divisor is an integer constant, or check_common_zero_divisor finds one, starting from a cast, a
//     conversion or a const object's read, an address constant.
//   - A dereference (check_common_null_dereference): its operand is an integer constant, or a cast or a conversion (what
//     lies under them isn't read here), or its type is nullptr_t's.
//   - Any other operator's conversion edges (check_common_edges_reach, where only
//     -Wimplicit-const-int-float-conversion may report): check_common_edge_wanted takes an operand's or an argument's
//     edge only where it is an integer constant, a conditional, a comma or a selection, and the edge's target is a real
//     floating type's. So: a child that is one, and a floating type's class among the three targets of the node's row
//     of conversions, which only such nodes have read. A call's arguments' targets are their slots' (the call is marked).
static __mmask16 check_common_reach_keep(const CheckCommonReachFilter* filter, uint32_t first, __mmask16 selected)
{
    const CheckGraph* graph = filter->graph;
    const CheckStream* stream = filter->stream;
    const __m512i zero = _mm512_setzero_si512();
    const __m512i limit = _mm512_set1_epi32((int)filter->count);
    const __m512i before = _mm512_add_epi32(_mm512_set1_epi32((int)first), _mm512_setr_epi32(-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14));
    const __m512i ops = _mm512_maskz_loadu_epi32(selected, graph->ops + first);
    const __mmask16 known = _mm512_mask_cmplt_epu32_mask(selected, ops, _mm512_set1_epi32(BUSTER_ARRAY_LENGTH(filter->families)));
    const __m512i table = _mm512_permutex2var_epi8(_mm512_loadu_si512(filter->families), _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(ops)),
                                                   _mm512_loadu_si512(filter->families + 64));
    const __m512i families = _mm512_maskz_cvtepu8_epi32(known, _mm512_castsi512_si128(table));
    const __m512i operand0 = _mm512_maskz_loadu_epi32(selected, graph->operand0 + first);
    const __m512i operand1 = _mm512_maskz_loadu_epi32(selected, graph->operand1 + first);
    // An operand that is the node before its reader (and a node: before node 0 there is none).
    const __mmask16 adjacent0 = _mm512_mask_cmpeq_epi32_mask(_mm512_mask_cmplt_epu32_mask(selected, operand0, limit), operand0, before);
    const __mmask16 adjacent1 = _mm512_mask_cmpeq_epi32_mask(_mm512_mask_cmplt_epu32_mask(selected, operand1, limit), operand1, before);
    const __m512i own = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(selected, filter->traits + first));
    const __m512i previous = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(selected, filter->traits + first - 1));
    const __m512i children = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(selected, filter->children + first));
    // (A leaf's trait is no child's: the children's byte has a bit-field's flag in its place.)
    const __m512i any = _mm512_andnot_si512(_mm512_set1_epi32(BUSTER_CHECK_CHILD_BIT_FIELD), children);
    const __m512i traits0 = _mm512_mask_blend_epi32(adjacent0, any, previous);
    const __m512i traits1 = _mm512_mask_blend_epi32(adjacent1, any, previous);
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
        // The source's type is the one before the node's own where the source is the node before it.
        const __mmask16 typed = constants & ((source1 & adjacent1) | (source0 & adjacent0));
        const __m512i source = check_common_reach_widths(filter->widths, typed, _mm512_maskz_loadu_epi32(typed, stream->value_types + first - 1));
        const __m512i target = check_common_reach_widths(filter->widths, typed, _mm512_maskz_loadu_epi32(typed, stream->value_types + first));
        const __mmask16 widening = _mm512_mask_cmpge_epu32_mask(_mm512_mask_cmpneq_epi32_mask(typed, source, zero), target, source);
        kept |= constants & (__mmask16)~widening;
        kept |= _mm512_mask_test_epi32_mask(widening & source1, children, _mm512_set1_epi32(BUSTER_CHECK_CHILD_BIT_FIELD));
    }
    if (filter->self_assign)
    {
        // Taken out only where an operand is known and no leaf.
        const __m512i second = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(source1, filter->traits + first - 2));
        const __mmask16 first_known = _mm512_mask_cmpeq_epi32_mask(_mm512_mask_cmplt_epu32_mask(source1, operand0, limit), operand0,
                                                                   _mm512_sub_epi32(before, _mm512_set1_epi32(1)));
        const __mmask16 other = _mm512_mask_testn_epi32_mask(adjacent1 & source1, previous, _mm512_set1_epi32(BUSTER_CHECK_TRAIT_VALUE)) |
                                _mm512_mask_testn_epi32_mask(first_known, second, _mm512_set1_epi32(BUSTER_CHECK_TRAIT_VALUE));
        kept |= source1 & (__mmask16)~other;
    }
    if (filter->enum_cast && (sources & ~kept) != 0)
    {
        const __mmask16 asked = sources & (__mmask16)~kept;
        kept |= check_common_reach_classed(filter, asked, _mm512_maskz_loadu_epi32(asked, stream->value_types + first), BUSTER_CHECK_CLASS_ENUM);
    }
    // The truth contexts and the divisions.
    const __mmask16 truths = _mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_TRUTH));
    kept |= _mm512_mask_test_epi32_mask(truths, _mm512_or_si512(any, own), _mm512_set1_epi32(BUSTER_CHECK_REACH_TRUTH_TRAITS));
    kept |= _mm512_mask_test_epi32_mask(_mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_DIVISION)), traits1,
                                        _mm512_set1_epi32(BUSTER_CHECK_REACH_DIVISOR_TRAITS));
    // The subscripts with a constant index, by their bases' types, and the dereferences, by their operands': a lane at a
    // time, as each one's first operand is anywhere before it.
    const __mmask16 subscripts = _mm512_mask_test_epi32_mask(_mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_SUBSCRIPT)), traits1,
                                                             _mm512_set1_epi32(BUSTER_CHECK_TRAIT_INTEGER));
    const __mmask16 dereferences = _mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_DEREFERENCE));
    kept |= _mm512_mask_test_epi32_mask(dereferences, traits0, _mm512_set1_epi32(BUSTER_CHECK_REACH_POINTER_TRAITS));
    for (uint32_t typed = (subscripts | (dereferences & (__mmask16)~kept)) & _mm512_cmplt_epu32_mask(operand0, limit); typed != 0; typed &= typed - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(typed);
        const uint32_t classes = check_common_reach_class(filter, stream->value_types[graph->operand0[first + lane]]);
        const uint32_t asked = (subscripts >> lane & 1) != 0 ? BUSTER_CHECK_CLASS_ARRAY : BUSTER_CHECK_CLASS_NULLPTR;
        kept |= (__mmask16)(((classes & asked) != 0 ? 1u : 0u) << lane);
    }
    // The calls that are marked.
    const __mmask16 calls = _mm512_test_epi32_mask(families, _mm512_set1_epi32(BUSTER_CHECK_FAMILY_CALL));
    if (calls != 0)
    {
        // (`first` is a multiple of 16: the block's bits are in one word.)
        kept |= calls & (__mmask16)(filter->marked[first >> 6] >> (first & 63));
    }
    // The conversion edges of the nodes left, where a child could have one: a row at a time.
    if (filter->edges)
    {
        _Static_assert(sizeof(CheckNodeConversions) == 32 && BUSTER_ARRAY_LENGTH(((CheckNodeConversions*)0)->operands) == 3, "a node's row has three operands' targets");
        for (uint32_t rows = _mm512_mask_test_epi32_mask(selected & (__mmask16)~kept & (__mmask16)~truths, any, _mm512_set1_epi32(BUSTER_CHECK_REACH_EDGE_TRAITS)); rows != 0;
             rows &= rows - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(rows);
            const CheckNodeConversions* row = stream->conversions + first + lane;
            const uint32_t classes = check_common_reach_class(filter, row->operands[0].type) | check_common_reach_class(filter, row->operands[1].type) |
                                     check_common_reach_class(filter, row->operands[2].type);
            kept |= (__mmask16)(((classes & BUSTER_CHECK_CLASS_FLOATING) != 0 ? 1u : 0u) << lane);
        }
    }
    return kept;
}
