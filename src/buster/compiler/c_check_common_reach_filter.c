// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The reach scan's filter (#1340), included after the scan's selection (check_common_reach_selected). The scan
// (check_common_next_reach_op) lists its candidates by operator alone and check_common_own_reach tests each one: on
// sqlite3.c 78,304 nodes for 2,292 with a bit. This filter takes out the candidates whose bits can only be zero, before
// they are listed. It is no second implementation of a node's bits: check_common_own_reach stays the only verdict, asked of every
// node the filter keeps, and what the filter gives is a necessary condition of each family of that test, read off its
// code (the derivations are at check_common_reach_step). A node it wrongly kept costs a visit; a node it wrongly took out
// would lose a warning, which the audit (CheckCommonReachAudit) and the candidates' tests look for.
//
// What it makes, each once a unit, over the facts' tree (each node's operands and arguments have it for their parent):
//   - three bytes a node (check_common_reach_traits), 64 nodes at a time: its traits, from its operator and its constant
//     kind; its operator's families; and what its children are, which the few nodes with a trait store at their parent;
//   - a class byte a type (check_common_reach_type_classes), 64 types at a time, from the type table's kinds;
//   - a bit a call whose argument slots hold one that could matter (check_common_reach_arguments).
// Then 64 candidates at a time are three byte loads and a dozen byte tests (check_common_reach_step): most nodes are
// taken out there, where no child is anything a test asks for. What is left, about a node in ten on sqlite3.c, takes a
// few scalar loads each for what its family reads past its children's byte: a source's and its target's widths, a base's
// or an operand's type's class, its row of conversions.
//
// No gather: on Zen 5 a 16-lane vpgatherdd is 64 uops (docs/coding-style.md, "SIMD and Zen 5"). This filter's first
// form gathered each candidate's operands' traits, types and rows of conversions, 16 nodes at a time, and on the 9700X
// its blocks cost 10 M cycles of sqlite3.c's compile, as much as the scan it replaced; its second, 16 nodes at a time
// without the gathers, 6 M (the 9700X readings are with #1340's pull request). The rows of conversions are cold by now,
// a cache line each, so a chunk's are asked for together before any is read (check_common_reach_chunk).

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
// A real floating type: a conversion edge's target (a complex one's edge is none of check_common_edge_wanted's where
// only -Wimplicit-const-int-float-conversion may report).
#define BUSTER_CHECK_CLASS_FLOATING 0x01u
#define BUSTER_CHECK_CLASS_ENUM 0x02u
#define BUSTER_CHECK_CLASS_ARRAY 0x04u
#define BUSTER_CHECK_CLASS_NULLPTR 0x08u
#define BUSTER_CHECK_CLASS_ALL 0xffu

static const uint8_t check_common_reach_kind_classes[64] =
{
    [BUSTER_TYPE_KIND_FLOATING] = BUSTER_CHECK_CLASS_FLOATING,
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
// In a unit's table (CheckCommonReachFilter.families), an operator the scan lists under none of the unit's options.
#define BUSTER_CHECK_FAMILY_UNLISTED 0xffu

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

// The traits each family's test asks of the operand it reads (check_common_reach_step).
// A source's traits but an integer constant's, whose conversion alone the widths decide (check_common_reach_step).
#define BUSTER_CHECK_REACH_ARM_TRAITS (BUSTER_CHECK_TRAIT_FLOATING | BUSTER_CHECK_TRAIT_ARMS | BUSTER_CHECK_TRAIT_PEELED | BUSTER_CHECK_TRAIT_SELECT)
#define BUSTER_CHECK_REACH_TRUTH_TRAITS (BUSTER_CHECK_TRAIT_FLOATING | BUSTER_CHECK_TRAIT_ARMS | BUSTER_CHECK_TRAIT_PEELED | BUSTER_CHECK_TRAIT_SELECT)
#define BUSTER_CHECK_REACH_EDGE_TRAITS (BUSTER_CHECK_TRAIT_INTEGER | BUSTER_CHECK_TRAIT_ARMS | BUSTER_CHECK_TRAIT_SELECT)
#define BUSTER_CHECK_REACH_DIVISOR_TRAITS (BUSTER_CHECK_TRAIT_INTEGER | BUSTER_CHECK_TRAIT_CAST | BUSTER_CHECK_TRAIT_PEELED | BUSTER_CHECK_TRAIT_ADDRESS)
#define BUSTER_CHECK_REACH_POINTER_TRAITS (BUSTER_CHECK_TRAIT_INTEGER | BUSTER_CHECK_TRAIT_CAST | BUSTER_CHECK_TRAIT_PEELED)

// The blocks of 16 nodes the scan's filter takes at a time: 512 nodes, eight steps, about thirty of whose rows of
// conversions the edges' test reads on sqlite3.c.
#define BUSTER_CHECK_REACH_CHUNK 32u
_Static_assert(BUSTER_CHECK_REACH_CHUNK % 4 == 0, "a chunk is whole steps of 64 nodes");

typedef struct CheckCommonReachFilter CheckCommonReachFilter;
struct CheckCommonReachFilter
{
    const CheckGraph* graph;
    const CheckStream* stream;
    // A trait byte a node. Null where the filter doesn't apply (check_common_reach_filter_build) and the scan lists every
    // candidate.
    const uint8_t* traits;
    // What each node's children are: the traits of its operands and arguments but a leaf's, and
    // BUSTER_CHECK_CHILD_BIT_FIELD.
    const uint8_t* children;
    // Each node's operator's families, from the unit's table below; no family for an operator outside the table.
    const uint8_t* node_families;
    // A class byte a type.
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
    // The operators' families under the unit's options (check_common_reach_filter_build), and
    // BUSTER_CHECK_FAMILY_UNLISTED for the operators the scan can't list under them.
    uint8_t families[128];
    // A builtin type's width as check_common_integer gives it, by its index, 0 where it isn't an integer type; and the
    // lesser of that and its default argument promotion's, 0 where either isn't one.
    uint8_t widths[BUSTER_TYPE_BUILTIN_COUNT];
    uint8_t promoted[BUSTER_TYPE_BUILTIN_COUNT];
    // The scan's chunk (check_common_reach_chunk): its first node, its blocks' candidates that may have a bit, and how
    // many of the blocks the scan has listed.
    uint32_t chunk_first;
    uint32_t chunk_blocks;
    uint32_t chunk_taken;
    uint16_t chunk_kept[BUSTER_CHECK_REACH_CHUNK];
};

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

// Every node's trait byte into `traits`, its operator's families (`families`, the unit's table) into `node_families`, and
// what its children are into `children` (`count` + 1 bytes, the last for the nodes without a parent), 64 nodes at a
// time: four operator vectors narrowed to bytes and looked up twice, as check_common_candidates_build has them, the
// nodes' constant kinds through a shuffle, and their bit-field flags. An
// operator or a constant kind outside its table has no trait, as no test names it. The nodes with a trait other than a
// leaf's, or a bit-field's flag, each store theirs at their parent, a node at a time: a constant, a conditional, a
// comma, a `<<`, a conversion, a selection, a cast.
static void check_common_reach_traits(const CheckGraph* graph, const CheckStream* stream, const uint32_t* parents, const uint8_t* families, uint8_t* traits,
                                      uint8_t* children, uint8_t* node_families)
{
    const uint32_t count = graph->count;
    const CheckOp* operators = graph->ops;
    const uint8_t* constants = stream->value_constants;
    const uint32_t* flags = stream->value_flags;
    const __m512i low = _mm512_loadu_si512(check_common_reach_operator_traits);
    const __m512i high = _mm512_loadu_si512(check_common_reach_operator_traits + 64);
    const __m512i kinds = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i*)check_common_reach_constant_traits));
    const __m512i family_low = _mm512_loadu_si512(families);
    const __m512i family_high = _mm512_loadu_si512(families + 64);
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
        _mm512_mask_storeu_epi8(node_families + first, lanes, _mm512_maskz_permutex2var_epi8(known & lanes, family_low, indices, family_high));
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

// Every type's class byte into `classes`, 64 types at a time: one byte permute of the kinds. A kind outside the table has
// every class.
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
}

// A TypeId's classes: its table's byte, and every class for one outside the table.
static inline __attribute__((always_inline)) uint32_t check_common_reach_class(const CheckCommonReachFilter* filter, TypeId type)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    return index < filter->type_count ? filter->classes[index] : BUSTER_CHECK_CLASS_ALL;
}

// A TypeId's width from `table`, the filter's widths or its promoted ones; 0 for a type that isn't a builtin one, whose
// width isn't known here.
static inline __attribute__((always_inline)) uint32_t check_common_reach_width(const uint8_t* table, TypeId type)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    return index < BUSTER_TYPE_BUILTIN_COUNT ? table[index] : 0;
}

// The calls with an argument slot that could matter, a bit each in `marked` (zeroed): a slot at a time, its argument's
// traits, its conversion's target and the call that reads it, the argument's parent. Which slots could matter is
// check_common_reach_step's derivation for a call. Scalar: each slot's argument is anywhere before its call, so its
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
            const uint32_t source_width = check_common_reach_width(filter->widths, stream->value_types[argument]);
            const uint32_t target_width = check_common_reach_width(filter->promoted, target);
            const bool arms = (traits & BUSTER_CHECK_REACH_ARM_TRAITS) != 0;
            // (Its conversion edge needs no test of its own: the argument is then a conditional, a comma or a selection, or
            // an integer constant whose target, a floating type, has no integer's width.)
            const bool narrowing = (traits & BUSTER_CHECK_TRAIT_INTEGER) != 0 && !(source_width != 0 && target_width >= source_width);
            const bool enumeration = filter->enum_cast && (check_common_reach_class(filter, target) & BUSTER_CHECK_CLASS_ENUM) != 0;
            const uint32_t call = parents[argument];
            if ((arms || narrowing || enumeration) && call < count)
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
//     types and a call's arguments' (check_float_narrows), whatever their operands are.
// A graph or a stream without its argument columns marks no call: the exact test then reads no argument, and a call
// has its own operands' edges alone.
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
        const uint64_t words = ((uint64_t)count + 63) / 64;
        uint8_t* traits = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, count);
        uint8_t* children = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
        uint8_t* node_families = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, count);
        uint8_t* classes = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, types->types.count);
        uint64_t* marked = arguments ? CHECK_COMMON_PUSH_ARRAY(scratch, uint64_t, words) : NULL;
        result = traits != NULL && children != NULL && node_families != NULL && classes != NULL && (marked != NULL || !arguments);
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
            // The operators the scan can list under the unit's options (check_common_reach_selected): the reach and
            // comparison classes', a member where -Watomic-access asks, the compound assignments and the builtins where
            // -Wimplicit-float-conversion does, and the conversion edges' operators. Whether it lists a node is still its
            // own answer; an operator it never lists is none of the filter's.
            for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
            {
                const bool classed = (check_common_candidate_classes[op] & ((1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_COMPARE))) != 0;
                const bool member = enabled->atomic_access && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW);
                const bool precision = enabled->float_precision && ((op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_MULTIPLY_ASSIGN) || op >= CHECK_OP_BUILTIN_MISC);
                const bool edged = enabled->const_int_float && ((op >= CHECK_OP_POSITIVE && op <= CHECK_OP_NEGATIVE) || (op >= CHECK_OP_ADD && op <= CHECK_OP_DIVIDE) ||
                                                                (op >= CHECK_OP_ADD_ASSIGN && op <= CHECK_OP_DIVIDE_ASSIGN) || op >= CHECK_OP_SELECT);
                if (!classed && !member && !precision && !edged)
                {
                    filter->families[op] = BUSTER_CHECK_FAMILY_UNLISTED;
                }
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
            check_common_reach_traits(graph, stream, parents, filter->families, traits, children, node_families);
            check_common_reach_type_classes(types, classes);
            filter->traits = traits;
            filter->children = children;
            filter->node_families = node_families;
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

// Of the 64 nodes from `first` (a multiple of 64), the scan's candidates that may have a bit of their own reach, a mask
// each of four blocks of 16 into `kept_blocks`, but for those whose only way to one is a conversion edge, which go to
// `row_blocks` for check_common_reach_rows to decide: the others' check_common_own_reach is zero, and they aren't
// check_common_self_assign_candidate's. What an operand is, its reader's children tell: in the facts' tree every operand
// and argument that is a node has its reader for parent, so what no child is, no operand is; an operand that isn't a node
// gives no test a bit. Family by family, each a necessary condition read off the exact test:
//
//   - No family (the table's 0): kept.
//   - An assignment's, an initialization's, a conversion's source (check_common_candidate_bits' first case): the test
//     gives its bit through check_common_conversion_source (an integer or floating constant, a conditional or a comma),
//     through check_common_bool_candidate (a `<<` or a conditional, past conversions and selections), through
//     check_common_enum_cast_edge, whose target, the node's own type, has an enumeration's or an aligned typedef's kind,
//     or through check_float_narrows, which -Wimplicit-float-conversion alone asks (the family is gone then). So: a
//     child is a floating constant, a conditional, a comma, a `<<`, a conversion or a selection; or enumerations may
//     report and its own type has an enumeration's class; or its source is an integer constant, whose bit is
//     check_common_constant_changes' or check_common_bitfield_assignment's. The first is false of a conversion between
//     integer types to one at least as wide, which the builtin types' widths tell (a type they don't know keeps the
//     node); the second needs a bit-field's lvalue among an assignment's operands. And, where -Wself-assign may report,
//     an assignment that check_common_self_assign_candidate takes is kept for the facts' mark.
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
//     floating type's. So: a child that is one (`row_blocks`), and a floating type's class among the three targets of
//     the node's row of conversions, which only such nodes have read (check_common_reach_rows). A call's arguments'
//     targets are their slots' (the call is marked).
//
// Which nodes the scan lists is check_common_reach_selected's answer alone. A block's candidates of the reach and the
// comparison classes are its two masks'; the function is asked only of a block where a node of another operator may
// have a bit, as its other candidates read the block's operators, types and diagnostics.
static void check_common_reach_step(const CheckCommonReachFilter* filter, const CheckCommonCandidates* candidates, bool sign_compare, bool atomic_access, bool float_precision,
                                    bool edges, uint32_t first, uint16_t* kept_blocks, uint16_t* row_blocks)
{
    const CheckGraph* graph = filter->graph;
    const CheckStream* stream = filter->stream;
    const uint32_t count = filter->count;
    const uint32_t live = count - first < 64 ? count - first : 64;
    const __mmask64 lanes = live == 64 ? (__mmask64)UINT64_MAX : (__mmask64)((UINT64_C(1) << live) - 1);
    const __m512i raw = _mm512_maskz_loadu_epi8(lanes, filter->node_families + first);
    const __mmask64 listed = _mm512_mask_cmpneq_epi8_mask(lanes, raw, _mm512_set1_epi8((char)BUSTER_CHECK_FAMILY_UNLISTED));
    const __m512i families = _mm512_maskz_mov_epi8(listed, raw);
    const __m512i children = _mm512_maskz_loadu_epi8(listed, filter->children + first);
    const __m512i own = _mm512_maskz_loadu_epi8(listed, filter->traits + first);
    // (A leaf's trait is no child's: the children's byte has a bit-field's flag in its place.)
    const __m512i any = _mm512_andnot_si512(_mm512_set1_epi8(BUSTER_CHECK_CHILD_BIT_FIELD), children);
    const __mmask64 source1 = _mm512_test_epi8_mask(families, _mm512_set1_epi8(BUSTER_CHECK_FAMILY_SOURCE1));
    const __mmask64 sources = source1 | _mm512_test_epi8_mask(families, _mm512_set1_epi8(BUSTER_CHECK_FAMILY_SOURCE0));
    const __mmask64 truths = _mm512_test_epi8_mask(families, _mm512_set1_epi8(BUSTER_CHECK_FAMILY_TRUTH));
    const __mmask64 dereferences = _mm512_test_epi8_mask(families, _mm512_set1_epi8(BUSTER_CHECK_FAMILY_DEREFERENCE));
    __mmask64 kept = _mm512_mask_testn_epi8_mask(listed, families, families);
    kept |= _mm512_mask_test_epi8_mask(sources, any, _mm512_set1_epi8(BUSTER_CHECK_REACH_ARM_TRAITS));
    kept |= _mm512_mask_test_epi8_mask(truths, _mm512_or_si512(any, own), _mm512_set1_epi8(BUSTER_CHECK_REACH_TRUTH_TRAITS));
    kept |= _mm512_mask_test_epi8_mask(_mm512_test_epi8_mask(families, _mm512_set1_epi8(BUSTER_CHECK_FAMILY_DIVISION)), any,
                                       _mm512_set1_epi8((char)BUSTER_CHECK_REACH_DIVISOR_TRAITS));
    kept |= _mm512_mask_test_epi8_mask(dereferences, any, _mm512_set1_epi8(BUSTER_CHECK_REACH_POINTER_TRAITS));
    kept |= _mm512_test_epi8_mask(families, _mm512_set1_epi8(BUSTER_CHECK_FAMILY_CALL)) & (filter->marked != NULL ? filter->marked[first >> 6] : 0);
    // The nodes a lane's loads decide.
    __mmask64 constants = _mm512_mask_test_epi8_mask(sources & ~kept, any, _mm512_set1_epi8(BUSTER_CHECK_TRAIT_INTEGER));
    __mmask64 typed = _mm512_mask_test_epi8_mask(_mm512_test_epi8_mask(families, _mm512_set1_epi8(BUSTER_CHECK_FAMILY_SUBSCRIPT)), any,
                                                 _mm512_set1_epi8(BUSTER_CHECK_TRAIT_INTEGER)) | (dereferences & ~kept);
    __mmask64 selfs = filter->self_assign ? source1 & ~kept : 0;
    __mmask64 enumerations = filter->enum_cast ? sources & ~kept : 0;
    __mmask64 rows = filter->edges ? _mm512_mask_test_epi8_mask(listed & ~kept & ~truths, any, _mm512_set1_epi8(BUSTER_CHECK_REACH_EDGE_TRAITS)) : 0;
    // The scan's candidates among them.
    const __mmask64 interest = kept | constants | typed | selfs | enumerations | rows;
    const uint32_t blocks = candidates->blocks;
    const uint16_t* reach_class = candidates->masks + (uint64_t)CHECK_COMMON_CANDIDATE_REACH * blocks;
    const uint16_t* compare_class = candidates->masks + (uint64_t)CHECK_COMMON_CANDIDATE_COMPARE * blocks;
    uint64_t selected = 0;
    for (uint32_t block = 0; block < 4 && first + block * 16 < count; block += 1)
    {
        const uint32_t at = first / 16 + block;
        const uint32_t classed = reach_class[at] | (sign_compare || edges ? compare_class[at] : 0u);
        const uint32_t others = (uint32_t)(interest >> (block * 16)) & 0xffffu & ~classed;
        const uint32_t listing = others != 0 ? check_common_reach_selected(graph, stream, candidates, sign_compare, atomic_access, float_precision, edges, first + block * 16) :
                                 classed;
        selected |= (uint64_t)listing << (block * 16);
    }
    kept &= selected;
    constants &= selected;
    typed &= selected;
    selfs &= selected;
    enumerations &= selected;
    rows &= selected;
    // An integer constant's source, by the widths; where the constant is another operand's, no test reads it.
    for (uint64_t left = constants; left != 0; left &= left - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(left);
        const uint32_t node = first + lane;
        const bool assignment = (source1 >> lane & 1) != 0;
        const uint32_t source = assignment ? graph->operand1[node] : graph->operand0[node];
        const bool constant = source < count && (filter->traits[source] & BUSTER_CHECK_TRAIT_INTEGER) != 0;
        const uint32_t source_width = constant ? check_common_reach_width(filter->widths, stream->value_types[source]) : 0;
        const uint32_t target_width = check_common_reach_width(filter->widths, stream->value_types[node]);
        const bool widening = source_width != 0 && target_width >= source_width;
        const bool field = assignment && (filter->children[node] & BUSTER_CHECK_CHILD_BIT_FIELD) != 0;
        kept |= (uint64_t)(constant && (!widening || field) ? 1u : 0u) << lane;
    }
    for (uint64_t left = selfs & ~kept; left != 0; left &= left - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(left);
        kept |= (uint64_t)(check_common_self_assign_candidate(graph, first + lane) ? 1u : 0u) << lane;
    }
    for (uint64_t left = enumerations & ~kept; left != 0; left &= left - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(left);
        kept |= (uint64_t)((check_common_reach_class(filter, stream->value_types[first + lane]) & BUSTER_CHECK_CLASS_ENUM) != 0 ? 1u : 0u) << lane;
    }
    // A subscript's constant index and its base's type, and a dereference's operand's type.
    for (uint64_t left = typed & ~kept; left != 0; left &= left - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(left);
        const uint32_t node = first + lane;
        const uint32_t operand = graph->operand0[node];
        const uint32_t index = graph->operand1[node];
        const uint32_t classes = operand < count ? check_common_reach_class(filter, stream->value_types[operand]) : 0;
        const bool dereference = (dereferences >> lane & 1) != 0;
        const bool subscript = !dereference && index < count && (filter->traits[index] & BUSTER_CHECK_TRAIT_INTEGER) != 0 && (classes & BUSTER_CHECK_CLASS_ARRAY) != 0;
        kept |= (uint64_t)(subscript || (dereference && (classes & BUSTER_CHECK_CLASS_NULLPTR) != 0) ? 1u : 0u) << lane;
    }
    rows &= ~kept;
    memcpy(kept_blocks, &kept, sizeof(kept));
    memcpy(row_blocks, &rows, sizeof(rows));
}

// Of the nodes `rows` of the block from `first`, those with a floating type's class among the three targets of their row
// of conversions (check_common_reach_step's last family): a row at a time.
static __mmask16 check_common_reach_rows(const CheckCommonReachFilter* filter, uint32_t first, __mmask16 rows)
{
    _Static_assert(sizeof(CheckNodeConversions) == 32 && BUSTER_ARRAY_LENGTH(((CheckNodeConversions*)0)->operands) == 3, "a node's row has three operands' targets");
    __mmask16 kept = 0;
    for (uint32_t left = rows; left != 0; left &= left - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(left);
        const CheckNodeConversions* row = filter->stream->conversions + first + lane;
        const uint32_t classes = check_common_reach_class(filter, row->operands[0].type) | check_common_reach_class(filter, row->operands[1].type) |
                                 check_common_reach_class(filter, row->operands[2].type);
        kept |= (__mmask16)(((classes & BUSTER_CHECK_CLASS_FLOATING) != 0 ? 1u : 0u) << lane);
    }
    return kept;
}

// The filter's next chunk of blocks, from node `first` (a multiple of 64): each block's candidates that may have a bit,
// in two steps. The first makes every block's mask (check_common_reach_step) and asks for the rows of conversions that
// the edges' test will read, a prefetch each; the second reads them (check_common_reach_rows). A row is a cache line of an
// array of 32 bytes a node, cold by now, and a test that waited for its own row had them one or two at a time; a chunk's
// worth are in flight together. The kept nodes' rows are asked for too: their visits read them.
static void check_common_reach_chunk(CheckCommonReachFilter* filter, const CheckCommonCandidates* candidates, bool sign_compare, bool atomic_access, bool float_precision,
                                     bool edges, uint32_t first)
{
    const CheckStream* stream = filter->stream;
    const uint32_t count = filter->count;
    uint16_t rows[BUSTER_CHECK_REACH_CHUNK] = { 0 };
    uint32_t blocks = 0;
    for (uint32_t node = first; node < count && blocks < BUSTER_CHECK_REACH_CHUNK; node += 64)
    {
        memset(filter->chunk_kept + blocks, 0, 4 * sizeof(uint16_t));
        check_common_reach_step(filter, candidates, sign_compare, atomic_access, float_precision, edges, node, filter->chunk_kept + blocks, rows + blocks);
        uint64_t asked = 0;
        memcpy(&asked, rows + blocks, sizeof(asked));
        uint64_t kept = 0;
        memcpy(&kept, filter->chunk_kept + blocks, sizeof(kept));
        for (uint64_t lanes = asked | kept; lanes != 0; lanes &= lanes - 1)
        {
            _mm_prefetch((const char*)(stream->conversions + node + (uint32_t)__builtin_ctzll(lanes)), _MM_HINT_T0);
        }
        blocks += 4;
    }
    for (uint32_t block = 0; block < blocks; block += 1)
    {
        if (rows[block] != 0)
        {
            filter->chunk_kept[block] |= check_common_reach_rows(filter, first + block * 16, rows[block]);
        }
    }
    filter->chunk_first = first;
    filter->chunk_blocks = blocks;
    filter->chunk_taken = 0;
}
