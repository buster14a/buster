// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The node machine's flat front (#2524, docs/node-machine.md): the lean front's records made by passes over a chunk,
// without its stack and without its walk a token at a time. The lean front (c_check_node_machine_front.c) keeps the
// values not yet taken on a stack, in groups by consumer, and treats each token whole as it reaches it: a branch on the
// token's class, then about a hundred instructions, whatever the class. This front takes a chunk's tokens apart by
// what each step needs.
//
// Operands by the tree's shape. In postorder a token's children are the runs of positions just before it, and the
// reader has each run's length (CheckInputReader.subtree_sizes, by postorder position). A token that takes values hops
// back over its children by those lengths and reads one word a child from a ring by position: the place in the unit of
// the record that is the child's value, a mark for a value the front doesn't know, or nothing. The hop over a child is
// in a second ring, the steps: the child's length, or 1 for a token that passes its children's values on (a list, a
// separator that is no comma operator, a group's closing), whose children the next hops then visit as the token's own.
// Nothing is pushed, popped, grouped or matched, no parent is loaded, and the passing tokens are not visited at all:
// they are a quarter of sqlite3.c's acting tokens and over half of Lua's and musl's.
//
// Places before records. How many records a token makes is known from its class and its own columns, before any of
// its operands is read: an identifier with a declared type one, an operator one, a statement's terminator one a value.
// So a chunk's records' places are a running sum over its tokens, and each token's word can be stored before any
// record is made. The price is that a token the front leaves (its values are not what its operator takes, or one of
// them is unknown) keeps its place, as an empty record (BUSTER_NODE_FLAT_EMPTY for its operator): the lean front makes
// none there.
//
// The passes over a chunk (node_front_chunk_flat):
//   1. The compaction (node_flat_compact): the tokens the front visits, and every position's step, 16 positions a step.
//   2. The classes (node_flat_classify): each token's class, the records it makes and what it gives its consumer where
//      the class alone says so, and the tokens of each class listed, 16 tokens a step.
//   3. The leaves (node_flat_leaves): the identifiers together, then the literals, the strings and the braced
//      initializers: each one's loads, and whether it makes a record.
//   4. The places (node_flat_places): the running sum, each token's word and each record's first form (a leaf's whole
//      record, an operator's empty one). A terminator's values are counted here, and its discards made.
//   5. The records (node_flat_unaries and the like): a class's tokens together, each one's hops, distances and record,
//      in a loop with no branch on the class.
//   6. The empty records (node_flat_empties): each one's consumers made empty in turn, up its own chain.
//
// A record is 16 bytes (NodeFlatRecord): the token, the operator, the operand count, three distances back, the root and
// leaf bits and a payload (an identifier's declaration; a literal's or a string's place among the chunk's values; a
// call's place among the chunk's argument distances). Its consumer writes two bytes into it, the consumer's distance
// forward and the place, as the lean front's consumer does.
//
// The lean front is its twin (node_flat_compare): after a chunk both have made, every record both made is compared
// field for field, by token, an empty record being none, and the distances less the empty records between. The lean
// front's records are compared with the walk's nodes in the same run, so a flat record that equals the lean one is the
// walk's node. Where the two legitimately differ the report counts it: the stack keeps a value however far its consumer
// is and forgets everything when it is full; the rings reach BUSTER_NODE_FLAT_WINDOW positions back and never fill. In
// the shadow only (PPOptions.node_front's BUSTER_NODE_FRONT_FLAT): no compile takes it. Included by
// c_check_expression_input.c after the front.

// The positions the rings of words and steps hold, a power of two and a multiple of the compaction's 16: a child
// further back than this from its chunk's end is out of reach, and the token that would take its value is left.
#define BUSTER_NODE_FLAT_WINDOW 8192u
// The records the ring of records holds, a power of two: a chunk's, for the comparison, and those a consumer writes its
// two bytes into.
#define BUSTER_NODE_FLAT_RECORDS 4096u
// A position's word: no value (a token that gives none, one that passes values on, or one the walk doesn't act at), a
// value the front doesn't know, or else the value's record's place in the unit.
#define BUSTER_NODE_FLAT_NONE UINT32_MAX
#define BUSTER_NODE_FLAT_UNKNOWN (UINT32_MAX - 1u)
// What a token gives its consumer: its word is the complement of the first two, and its record's place for the third.
#define BUSTER_NODE_FLAT_GIVES_NOTHING 0u
#define BUSTER_NODE_FLAT_GIVES_UNKNOWN 1u
#define BUSTER_NODE_FLAT_GIVES_RECORD 2u
// A visited token's byte (NodeFlat.metas): whether it makes a record, what it gives (BUSTER_NODE_FLAT_GIVES_*, from the
// second bit), and whether it is a statement's terminator, whose values are counted at its place.
#define BUSTER_NODE_FLAT_META_RECORD 0x01u
#define BUSTER_NODE_FLAT_META_GIVES_SHIFT 1u
#define BUSTER_NODE_FLAT_META_GIVES 0x06u
#define BUSTER_NODE_FLAT_META_STATEMENT 0x08u
#define BUSTER_NODE_FLAT_META(record, gives) ((uint8_t)((record) | (gives) << BUSTER_NODE_FLAT_META_GIVES_SHIFT))
// An empty record's operator: the token the front left keeps its place.
#define BUSTER_NODE_FLAT_EMPTY CHECK_OP_COUNT
// A record's flags: a full expression's root, and a leaf's kind (BUSTER_NODE_FRONT_LEAF_*) from the second bit.
#define BUSTER_NODE_FLAT_ROOT 0x01u
#define BUSTER_NODE_FLAT_LEAF_SHIFT 1u
#define BUSTER_NODE_FLAT_LEAF_MASK 0x06u
// The argument distances a chunk's calls list.
#define BUSTER_NODE_FLAT_ARGUMENTS 4096u
// The hops a token's last two values are looked for in before its every value is (node_flat_values): its operands, a
// passing token or two, and the tokens beside them that give nothing (a group's opening, a member's name, a cast's type).
#define BUSTER_NODE_FLAT_HOPS 6u
// The pass's stages, for their ticks (NodeFrontShadow.flat_stage_ticks).
#define BUSTER_NODE_FLAT_STAGE_COMPACT 0u
#define BUSTER_NODE_FLAT_STAGE_CLASSIFY 1u
#define BUSTER_NODE_FLAT_STAGE_LEAVES 2u
#define BUSTER_NODE_FLAT_STAGE_PLACES 3u
#define BUSTER_NODE_FLAT_STAGE_RECORDS 4u
#define BUSTER_NODE_FLAT_STAGE_EMPTIES 5u
// The fields of a record that the comparison with the lean front names (NodeFrontShadow.flat_difference_fields), after
// the front's own (BUSTER_NODE_FRONT_FIELD_*).
#define BUSTER_NODE_FLAT_FIELD_COMPACTION 16u
#define BUSTER_NODE_FLAT_FIELD_OPERATOR 17u
#define BUSTER_NODE_FLAT_FIELD_COUNT 18u
#define BUSTER_NODE_FLAT_FIELD_DISTANCE 19u
#define BUSTER_NODE_FLAT_FIELD_ROOT 20u
#define BUSTER_NODE_FLAT_FIELD_CONSUMER 21u
#define BUSTER_NODE_FLAT_FIELD_LEAF 22u
#define BUSTER_NODE_FLAT_FIELD_ARGUMENTS 23u
#define BUSTER_NODE_FLAT_FIELD_OPERAND 24u
#define BUSTER_NODE_FLAT_FIELD_CLASSES 25u
#define BUSTER_NODE_FLAT_FIELD_WINDOW 26u

// A record: 16 bytes. `count` is the operand count (a call's callee and arguments; 255 for that or more), `distances`
// each of its first three operands' distance back in the unit's records (0 for none, BUSTER_NODE_FRONT_FAR for that or
// more), `up` and `place` what its consumer writes (the consumer's distance forward and the record's place among its
// operands, as NodeFront.ups and places).
typedef struct NodeFlatRecord NodeFlatRecord;
struct NodeFlatRecord
{
    uint32_t token;
    uint8_t op;
    uint8_t count;
    uint8_t distances[3];
    uint8_t up;
    uint8_t place;
    uint8_t flags;
    uint32_t payload;
};
_Static_assert(sizeof(NodeFlatRecord) == 16, "a flat record is 16 bytes");

// The flat front's state for a unit, on the reader's scratch.
typedef struct NodeFlat NodeFlat;
struct NodeFlat
{
    NodeFrontShadow* shadow;
    struct NodeFlatWindow* window;
    CheckInputLiterals* window_literals;
    // The next chunk's first postorder position, the last chunk's, the records made in the unit so far, and the first
    // of the last chunk's.
    uint32_t position;
    uint32_t chunk_first;
    uint32_t ordinal;
    uint32_t chunk_ordinal;
    // The chunk's tokens the front visits, in postorder, each with its visits byte and its position: 16 more slots than a
    // chunk has, which the compaction's whole-register stores write past its count.
    uint32_t work;
    uint32_t tokens[BUSTER_CHECK_EXPRESSION_CHUNK + 16];
    uint32_t classes[BUSTER_CHECK_EXPRESSION_CHUNK + 16];
    uint32_t positions[BUSTER_CHECK_EXPRESSION_CHUNK + 16];
    // By visited token: its class, its byte (BUSTER_NODE_FLAT_META_*), its record's first form (a leaf's whole record
    // once its payload is in it, an operator's empty one), and the place of its first record.
    uint8_t codes[BUSTER_CHECK_EXPRESSION_CHUNK + 16];
    uint8_t metas[BUSTER_CHECK_EXPRESSION_CHUNK + 16];
    NodeFlatRecord bases[BUSTER_CHECK_EXPRESSION_CHUNK + 16];
    uint32_t places[BUSTER_CHECK_EXPRESSION_CHUNK + 16];
    // The chunk's visited tokens of each class, by their place among the visited ones, in order.
    uint32_t list_counts[BUSTER_CHECK_EXPRESSION_CODES];
    uint32_t lists[BUSTER_CHECK_EXPRESSION_CODES][BUSTER_CHECK_EXPRESSION_CHUNK + 16];
    // The chunk's literals' and strings' values, by a leaf's payload: its type and its integer or object. A literal's is
    // its place among the chunk's literals, a string's after them.
    uint32_t side_count;
    TypeId side_types[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint64_t side_integers[BUSTER_CHECK_EXPRESSION_CHUNK];
    // The chunk's calls' arguments, by a call's payload: each argument's distance back from its call, in a byte, first
    // argument first (what #2850's call rule reads from the call's side).
    uint32_t argument_count;
    uint8_t argument_distances[BUSTER_NODE_FLAT_ARGUMENTS];
    // The places of the chunk's records that a class's loop left empty: each one's consumers are made empty in turn.
    uint32_t empty_count;
    uint32_t empties[BUSTER_CHECK_EXPRESSION_CHUNK + 16];
    // A binary and a prefix operator's CheckOp by its token's kind (CHECK_OP_COUNT for none), made once a unit.
    uint8_t binary_ops[256];
    uint8_t prefix_ops[256];
    // The rings of words and of steps, by position, the ring of records, by place in the unit, and, by a record's place,
    // its consumer's place where that is BUSTER_NODE_FRONT_FAR records ahead or more. The rings are not a multiple of
    // 4 KB apart: a load whose address has a recent store's low twelve bits waits for that store.
    uint32_t words[BUSTER_NODE_FLAT_WINDOW];
    uint8_t ring_stagger[2048 + 64];
    uint32_t steps[BUSTER_NODE_FLAT_WINDOW];
    uint8_t record_stagger[1024 + 64];
    NodeFlatRecord records[BUSTER_NODE_FLAT_RECORDS];
    uint32_t consumers[BUSTER_NODE_FLAT_RECORDS];
};

// The classes whose tokens only pass their children's values on, a bit a class: the compaction leaves them out.
#define BUSTER_NODE_FLAT_PASSING (1u << CHECK_INPUT_CODE_LIST | 1u << CHECK_INPUT_CODE_SEPARATOR | 1u << CHECK_INPUT_CODE_GROUP)
// The classes whose tokens are listed (node_flat_classify), a bit a class: the leaves and every class with a loop of its
// own. A terminator is treated at its place, and a token of any other class is the walk's own.
#define BUSTER_NODE_FLAT_LISTED \
    (1u << CHECK_INPUT_CODE_IDENTIFIER | 1u << CHECK_INPUT_CODE_LITERAL | 1u << CHECK_INPUT_CODE_STRING | 1u << CHECK_INPUT_CODE_PREFIX | 1u << CHECK_INPUT_CODE_POSTFIX | \
     1u << CHECK_INPUT_CODE_BINARY | 1u << CHECK_INPUT_CODE_CAST | 1u << CHECK_INPUT_CODE_SUBSCRIPT | 1u << CHECK_INPUT_CODE_TERNARY | 1u << CHECK_INPUT_CODE_MEMBER | \
     1u << CHECK_INPUT_CODE_CALL | 1u << CHECK_INPUT_CODE_CONDITION | 1u << CHECK_INPUT_CODE_RETURN | 1u << CHECK_INPUT_CODE_INITIALIZE)

// A class's byte, where the class alone says it (node_flat_classify): an operator makes a record and gives its place; a
// condition and a return make one and give nothing; a terminator's are counted at its place; a leaf's are its own
// columns' (node_flat_leaves); any other class is the walk's own visit, no record and an unknown value.
static const uint8_t node_flat_class_metas[BUSTER_CHECK_EXPRESSION_CODES] __attribute__((aligned(64))) =
{
    [CHECK_INPUT_CODE_OTHER] = BUSTER_NODE_FLAT_META(0, BUSTER_NODE_FLAT_GIVES_UNKNOWN),
    [CHECK_INPUT_CODE_SCALAR] = BUSTER_NODE_FLAT_META(0, BUSTER_NODE_FLAT_GIVES_UNKNOWN),
    [CHECK_INPUT_CODE_LIST] = BUSTER_NODE_FLAT_META(0, BUSTER_NODE_FLAT_GIVES_UNKNOWN),
    [CHECK_INPUT_CODE_DROP] = BUSTER_NODE_FLAT_META(0, BUSTER_NODE_FLAT_GIVES_UNKNOWN),
    [CHECK_INPUT_CODE_SEPARATOR] = BUSTER_NODE_FLAT_META(0, BUSTER_NODE_FLAT_GIVES_UNKNOWN),
    [CHECK_INPUT_CODE_GROUP] = BUSTER_NODE_FLAT_META(0, BUSTER_NODE_FLAT_GIVES_UNKNOWN),
    [CHECK_INPUT_CODE_OPERAND] = BUSTER_NODE_FLAT_META(0, BUSTER_NODE_FLAT_GIVES_UNKNOWN),
    [CHECK_INPUT_CODE_PREFIX] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_RECORD),
    [CHECK_INPUT_CODE_BINARY] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_RECORD),
    [CHECK_INPUT_CODE_CAST] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_RECORD),
    [CHECK_INPUT_CODE_TERNARY] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_RECORD),
    [CHECK_INPUT_CODE_SUBSCRIPT] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_RECORD),
    [CHECK_INPUT_CODE_MEMBER] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_RECORD),
    [CHECK_INPUT_CODE_POSTFIX] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_RECORD),
    [CHECK_INPUT_CODE_CALL] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_RECORD),
    [CHECK_INPUT_CODE_INITIALIZE] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_RECORD),
    [CHECK_INPUT_CODE_CONDITION] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_NOTHING),
    [CHECK_INPUT_CODE_RETURN] = BUSTER_NODE_FLAT_META(1, BUSTER_NODE_FLAT_GIVES_NOTHING),
    [CHECK_INPUT_CODE_TERMINATOR] = BUSTER_NODE_FLAT_META_STATEMENT,
};

// A class's record's first form (node_flat_classify): a leaf's operator and kind, an operator's empty record, and the
// root bit of the classes whose record is a full expression's root.
static const uint8_t node_flat_class_ops[BUSTER_CHECK_EXPRESSION_CODES] __attribute__((aligned(64))) =
{
    [CHECK_INPUT_CODE_OTHER] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_SCALAR] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_LIST] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_DROP] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_IDENTIFIER] = CHECK_OP_VALUE,
    [CHECK_INPUT_CODE_LITERAL] = CHECK_OP_VALUE,
    [CHECK_INPUT_CODE_PREFIX] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_BINARY] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_SEPARATOR] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_GROUP] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_CAST] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_TERNARY] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_SUBSCRIPT] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_MEMBER] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_POSTFIX] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_TERMINATOR] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_CONDITION] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_CALL] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_STRING] = CHECK_OP_VALUE,
    [CHECK_INPUT_CODE_RETURN] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_INITIALIZE] = BUSTER_NODE_FLAT_EMPTY,
    [CHECK_INPUT_CODE_OPERAND] = BUSTER_NODE_FLAT_EMPTY,
};

static const uint8_t node_flat_class_flags[BUSTER_CHECK_EXPRESSION_CODES] __attribute__((aligned(64))) =
{
    [CHECK_INPUT_CODE_IDENTIFIER] = BUSTER_NODE_FRONT_LEAF_IDENTIFIER << BUSTER_NODE_FLAT_LEAF_SHIFT,
    [CHECK_INPUT_CODE_LITERAL] = BUSTER_NODE_FRONT_LEAF_LITERAL << BUSTER_NODE_FLAT_LEAF_SHIFT,
    [CHECK_INPUT_CODE_STRING] = BUSTER_NODE_FRONT_LEAF_STRING << BUSTER_NODE_FLAT_LEAF_SHIFT,
    [CHECK_INPUT_CODE_CONDITION] = BUSTER_NODE_FLAT_ROOT,
    [CHECK_INPUT_CODE_RETURN] = BUSTER_NODE_FLAT_ROOT,
    [CHECK_INPUT_CODE_INITIALIZE] = BUSTER_NODE_FLAT_ROOT,
};

// The flat front's compaction of postorder positions [first, end), at most a chunk: the tokens it visits into `tokens`
// with their visits bytes in `classes` and their positions in `positions`, in postorder; every position's word into the
// ring `words`, BUSTER_NODE_FLAT_NONE; and every position's step into the ring `steps`, 1 for a token of a passing class
// and else its subtree's length. Returns how many it visits. A token is visited where the walk acts at it
// (check_input_expression_compact's test, a DROP left out) and its class is no passing one. The vector path gathers 16
// tokens' bytes, stores their 16 words and steps and compresses the tokens it keeps in registers, each stored whole (the
// arrays have 16 slots more than a chunk); the scalar path, its twin, takes a token at a time. Out of line: a stage of
// its own, with its own ticks.
static __attribute__((noinline)) uint32_t node_flat_compact(const CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t* tokens, uint32_t* classes,
                                                            uint32_t* positions, uint32_t* words, uint32_t* steps, bool scalar)
{
    const uint8_t* visits = reader->visits;
    const uint32_t* postorder = reader->postorder;
    const uint32_t* sizes = reader->subtree_sizes;
    const uint32_t dropped = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_DROP << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    // Gathers index by signed 32-bit offsets.
    const bool indexed = reader->parser->tokens.count <= (uint32_t)INT32_MAX;
    uint32_t result = 0;
    uint32_t position = first;
    const __m512i lane_steps = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (; !BUSTER_SCALAR_PATH(scalar) && indexed && position < end; position += 16)
    {
        const uint32_t length = end - position < 16 ? end - position : 16;
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, length);
        const __m512i order = _mm512_maskz_loadu_epi32(lanes, postorder + position);
        const __m512i lengths = _mm512_maskz_loadu_epi32(lanes, sizes + position);
        const __m512i bytes = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, order, visits, 1);
        const __m512i visit = _mm512_and_si512(bytes, _mm512_set1_epi32(0xff));
        const __mmask16 acting = _mm512_mask_test_epi32_mask(lanes, visit, _mm512_set1_epi32(BUSTER_CHECK_EXPRESSION_ACTS)) &
                                 _mm512_cmpneq_epu32_mask(visit, _mm512_set1_epi32((int32_t)dropped));
        // A class's bit of the passing classes, shifted down by the class: a class is five bits of the byte.
        const __m512i codes = _mm512_srli_epi32(_mm512_and_si512(visit, _mm512_set1_epi32(BUSTER_CHECK_EXPRESSION_CODE_MASK)), BUSTER_CHECK_EXPRESSION_CODE_SHIFT);
        const __mmask16 passing = acting & _mm512_test_epi32_mask(_mm512_srlv_epi32(_mm512_set1_epi32((int32_t)BUSTER_NODE_FLAT_PASSING), codes), _mm512_set1_epi32(1));
        const __mmask16 visited = acting & (__mmask16)~passing;
        // A chunk's first position and the rings' length are multiples of 16: the 16 words and steps never wrap.
        _mm512_mask_storeu_epi32(words + (position & (BUSTER_NODE_FLAT_WINDOW - 1)), lanes, _mm512_set1_epi32((int32_t)BUSTER_NODE_FLAT_NONE));
        _mm512_mask_storeu_epi32(steps + (position & (BUSTER_NODE_FLAT_WINDOW - 1)), lanes, _mm512_mask_blend_epi32(passing, lengths, _mm512_set1_epi32(1)));
        _mm512_storeu_si512(tokens + result, _mm512_maskz_compress_epi32(visited, order));
        _mm512_storeu_si512(classes + result, _mm512_maskz_compress_epi32(visited, visit));
        _mm512_storeu_si512(positions + result, _mm512_maskz_compress_epi32(visited, _mm512_add_epi32(_mm512_set1_epi32((int32_t)position), lane_steps)));
        result += (uint32_t)__builtin_popcount(visited);
    }
    for (; position < end; position += 1)
    {
        const uint32_t token = postorder[position];
        const uint8_t visit = visits[token];
        const uint32_t code = (visit & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
        const bool acting = (visit & BUSTER_CHECK_EXPRESSION_ACTS) != 0 && visit != dropped;
        const bool passing = acting && (BUSTER_NODE_FLAT_PASSING >> code & 1) != 0;
        words[position & (BUSTER_NODE_FLAT_WINDOW - 1)] = BUSTER_NODE_FLAT_NONE;
        steps[position & (BUSTER_NODE_FLAT_WINDOW - 1)] = passing ? 1 : sizes[position];
        // The compress store a token at a time, as check_input_expression_compact's: each is written at `result`, and
        // the next writes over it unless counting keeps it.
        tokens[result] = token;
        classes[result] = visit;
        positions[result] = position;
        result += acting && !passing;
    }
    return result;
}

// A unit's flat front begun: nothing made, and the operators' tables by kind.
static void node_flat_begin(NodeFlat* flat, NodeFrontShadow* shadow)
{
    flat->shadow = shadow;
    flat->position = 0;
    flat->chunk_first = 0;
    flat->ordinal = 0;
    flat->chunk_ordinal = 0;
    flat->work = 0;
    flat->side_count = 0;
    flat->argument_count = 0;
    flat->empty_count = 0;
    for (uint32_t kind = 0; kind < 256; kind += 1)
    {
        CheckOp prefix = CHECK_OP_COUNT;
        switch (kind)
        {
            case PARSE_KIND_PLUS: prefix = CHECK_OP_POSITIVE; break;
            case PARSE_KIND_EXTENSION: prefix = CHECK_OP_SELECT; break;
            case PARSE_KIND_MINUS: prefix = CHECK_OP_NEGATIVE; break;
            case PARSE_KIND_EXCLAIM: prefix = CHECK_OP_NOT; break;
            case PARSE_KIND_TILDE: prefix = CHECK_OP_BIT_NOT; break;
            case PARSE_KIND_AMP: prefix = CHECK_OP_ADDRESS; break;
            case PARSE_KIND_STAR: prefix = CHECK_OP_DEREFERENCE; break;
            case PARSE_KIND_PLUSPLUS: prefix = CHECK_OP_PRE_INCREMENT; break;
            case PARSE_KIND_MINUSMINUS: prefix = CHECK_OP_PRE_DECREMENT; break;
            default: break;
        }
        flat->prefix_ops[kind] = (uint8_t)prefix;
        flat->binary_ops[kind] = (uint8_t)check_input_binary((uint8_t)kind);
    }
}

// The chunk's `work` visited tokens by class: each one's class, its byte where its class says it, its record's first
// form (its token, its class's operator and flags, no payload), and its place among the visited tokens appended to its
// class's list. The vector path takes 16 tokens a step: the class from the visits byte, the byte, the operator and the
// flags by lookups in registers, the 16 records put together from the four columns, and each class's tokens compressed
// onto its list; the scalar path, its twin, takes a token at a time.
static __attribute__((noinline)) void node_flat_classify(NodeFlat* flat, uint32_t work, bool scalar)
{
    uint32_t i = 0;
    memset(flat->list_counts, 0, sizeof(flat->list_counts));
    const __m512i lane_steps = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const __m512i metas_low = _mm512_cvtepu8_epi32(_mm_load_si128((const __m128i*)node_flat_class_metas));
    const __m512i metas_high = _mm512_cvtepu8_epi32(_mm_load_si128((const __m128i*)(node_flat_class_metas + 16)));
    const __m512i ops_low = _mm512_cvtepu8_epi32(_mm_load_si128((const __m128i*)node_flat_class_ops));
    const __m512i ops_high = _mm512_cvtepu8_epi32(_mm_load_si128((const __m128i*)(node_flat_class_ops + 16)));
    const __m512i flags_low = _mm512_slli_epi32(_mm512_cvtepu8_epi32(_mm_load_si128((const __m128i*)node_flat_class_flags)), 24);
    const __m512i flags_high = _mm512_slli_epi32(_mm512_cvtepu8_epi32(_mm_load_si128((const __m128i*)(node_flat_class_flags + 16))), 24);
    for (; !BUSTER_SCALAR_PATH(scalar) && i < work; i += 16)
    {
        const uint32_t length = work - i < 16 ? work - i : 16;
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, length);
        const __m512i visit = _mm512_loadu_si512(flat->classes + i);
        const __m512i codes = _mm512_srli_epi32(_mm512_and_si512(visit, _mm512_set1_epi32(BUSTER_CHECK_EXPRESSION_CODE_MASK)), BUSTER_CHECK_EXPRESSION_CODE_SHIFT);
        const __m512i indexes = _mm512_add_epi32(_mm512_set1_epi32((int32_t)i), lane_steps);
        _mm_storeu_si128((__m128i*)(flat->codes + i), _mm512_cvtepi32_epi8(codes));
        _mm_storeu_si128((__m128i*)(flat->metas + i), _mm512_cvtepi32_epi8(_mm512_permutex2var_epi32(metas_low, codes, metas_high)));
        // The 16 records: a record is four doublewords, its token, its operator in the second's low byte, its flags
        // in the third's high byte and its payload. The four columns' doublewords are unpacked by pairs within each
        // 128 bits, which leaves records 0, 4, 8 and 12 in one register and so on, and the registers' quarters are then
        // put in the records' order.
        const __m512i tokens = _mm512_loadu_si512(flat->tokens + i);
        const __m512i ops = _mm512_permutex2var_epi32(ops_low, codes, ops_high);
        const __m512i flags = _mm512_permutex2var_epi32(flags_low, codes, flags_high);
        const __m512i zero = _mm512_setzero_si512();
        const __m512i token_op_low = _mm512_unpacklo_epi32(tokens, ops);
        const __m512i token_op_high = _mm512_unpackhi_epi32(tokens, ops);
        const __m512i flag_payload_low = _mm512_unpacklo_epi32(flags, zero);
        const __m512i flag_payload_high = _mm512_unpackhi_epi32(flags, zero);
        const __m512i first = _mm512_unpacklo_epi64(token_op_low, flag_payload_low);
        const __m512i second = _mm512_unpackhi_epi64(token_op_low, flag_payload_low);
        const __m512i third = _mm512_unpacklo_epi64(token_op_high, flag_payload_high);
        const __m512i fourth = _mm512_unpackhi_epi64(token_op_high, flag_payload_high);
        const __m512i even_low = _mm512_shuffle_i32x4(first, second, 0x88);
        const __m512i even_high = _mm512_shuffle_i32x4(third, fourth, 0x88);
        const __m512i odd_low = _mm512_shuffle_i32x4(first, second, 0xdd);
        const __m512i odd_high = _mm512_shuffle_i32x4(third, fourth, 0xdd);
        _mm512_storeu_si512(flat->bases + i, _mm512_shuffle_i32x4(even_low, even_high, 0x88));
        _mm512_storeu_si512(flat->bases + i + 4, _mm512_shuffle_i32x4(odd_low, odd_high, 0x88));
        _mm512_storeu_si512(flat->bases + i + 8, _mm512_shuffle_i32x4(even_low, even_high, 0xdd));
        _mm512_storeu_si512(flat->bases + i + 12, _mm512_shuffle_i32x4(odd_low, odd_high, 0xdd));
        for (uint32_t listed = BUSTER_NODE_FLAT_LISTED; listed != 0; listed &= listed - 1)
        {
            const uint32_t code = (uint32_t)__builtin_ctz(listed);
            const __mmask16 members = _mm512_mask_cmpeq_epi32_mask(lanes, codes, _mm512_set1_epi32((int32_t)code));
            _mm512_storeu_si512(flat->lists[code] + flat->list_counts[code], _mm512_maskz_compress_epi32(members, indexes));
            flat->list_counts[code] += (uint32_t)__builtin_popcount(members);
        }
    }
    for (; i < work; i += 1)
    {
        const uint32_t code = (flat->classes[i] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
        flat->codes[i] = (uint8_t)code;
        flat->metas[i] = node_flat_class_metas[code];
        flat->bases[i] = (NodeFlatRecord){ .token = flat->tokens[i], .op = node_flat_class_ops[code], .flags = node_flat_class_flags[code] };
        // The compress store a token at a time: written at its class's count, and kept where the class is listed.
        flat->lists[code][flat->list_counts[code]] = i;
        flat->list_counts[code] += BUSTER_NODE_FLAT_LISTED >> code & 1;
    }
}

// The chunk's leaves, a class at a time, as the lean front decides each (node_front_chunk_by): whether it makes a
// record and what it gives. An identifier is left in a K&R unit and for a constexpr object's use, and a value where
// it has a declared type; its payload is its declaration. A literal's value is the chunk's entry
// (check_input_expression_literals; its place among the chunk's literals is its place on its list), or, of another
// form, the decoding's, which may leave it. A string's is its object's. And a braced initializer's assignment passes
// its values on: its step becomes 1, as a passing class's, and it makes no record.
static __attribute__((noinline)) void node_flat_leaves(NodeFlat* flat, CheckInputReader* reader, const CheckInputLiterals* literals)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const TypeId* declared_types = reader->declared_types;
    const uint32_t* declarations = input->declarations;
    const uint8_t* declaration_flags = reader->declaration_flags;
    const bool kr = input->kr_parameter_count != 0;
    const uint32_t* identifiers = flat->lists[CHECK_INPUT_CODE_IDENTIFIER];
    const uint32_t identifier_count = flat->list_counts[CHECK_INPUT_CODE_IDENTIFIER];
    for (uint32_t j = 0; j < identifier_count; j += 1)
    {
        const uint32_t i = identifiers[j];
        const uint32_t token = flat->tokens[i];
        const TypeId type = declared_types[token];
        const uint32_t declaration = type != 0 ? declarations[token] : 0;
        const bool left = kr || (type != 0 && (declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_CONSTEXPR) != 0);
        const bool made = !left && type != 0;
        flat->metas[i] = BUSTER_NODE_FLAT_META(made, made ? BUSTER_NODE_FLAT_GIVES_RECORD : left ? BUSTER_NODE_FLAT_GIVES_UNKNOWN : BUSTER_NODE_FLAT_GIVES_NOTHING);
        flat->bases[i].payload = declaration;
    }
    const uint32_t* literal_list = flat->lists[CHECK_INPUT_CODE_LITERAL];
    const uint32_t literal_count = flat->list_counts[CHECK_INPUT_CODE_LITERAL];
    for (uint32_t j = 0; j < literal_count; j += 1)
    {
        const uint32_t i = literal_list[j];
        const uint32_t value = literals->values[j];
        bool left = false;
        TypeId type = BUSTER_TYPE_INT;
        uint64_t integer = value;
        if (__builtin_expect(value == BUSTER_CHECK_NONE, 0))
        {
            // Another form than a plain decimal, whose spelling a function's name's token can't be.
            const uint32_t token = flat->tokens[i];
            CheckLiteral decoded = { .type = BUSTER_TYPE_INT, .integer = value, .valid = true };
            left = check_input_function_name_token(parser, token) || !node_front_literal(reader, token, &decoded);
            type = decoded.type;
            integer = decoded.integer;
        }
        flat->side_types[j] = type;
        flat->side_integers[j] = integer;
        flat->metas[i] = BUSTER_NODE_FLAT_META(!left, left ? BUSTER_NODE_FLAT_GIVES_UNKNOWN : BUSTER_NODE_FLAT_GIVES_RECORD);
        flat->bases[i].payload = j;
    }
    const uint32_t* strings = flat->lists[CHECK_INPUT_CODE_STRING];
    const uint32_t string_count = flat->list_counts[CHECK_INPUT_CODE_STRING];
    for (uint32_t j = 0; j < string_count; j += 1)
    {
        const uint32_t i = strings[j];
        const CheckInitString* string = check_input_string(reader, flat->tokens[i]);
        const uint32_t side = literal_count + j;
        if (string != 0 && side < BUSTER_CHECK_EXPRESSION_CHUNK)
        {
            flat->side_types[side] = string->type;
            flat->side_integers[side] = string->object;
        }
        flat->metas[i] = BUSTER_NODE_FLAT_META(string != 0, string != 0 ? BUSTER_NODE_FLAT_GIVES_RECORD : BUSTER_NODE_FLAT_GIVES_NOTHING);
        flat->bases[i].payload = side;
    }
    flat->side_count = literal_count + string_count < BUSTER_CHECK_EXPRESSION_CHUNK ? literal_count + string_count : BUSTER_CHECK_EXPRESSION_CHUNK;
    const uint32_t* initializers = flat->lists[CHECK_INPUT_CODE_INITIALIZE];
    const uint32_t initializer_count = flat->list_counts[CHECK_INPUT_CODE_INITIALIZE];
    for (uint32_t j = 0; j < initializer_count; j += 1)
    {
        const uint32_t i = initializers[j];
        if (reader->initializers[flat->tokens[i]] == 2)
        {
            flat->steps[flat->positions[i] & (BUSTER_NODE_FLAT_WINDOW - 1)] = 1;
            flat->metas[i] = BUSTER_NODE_FLAT_META(0, BUSTER_NODE_FLAT_GIVES_NOTHING);
        }
    }
}

// The values of the token at `position`, last first, into `values` (BUSTER_NODE_FRONT_VALUES + 2 slots): how many, or
// BUSTER_NODE_FRONT_UNKNOWN where one is unknown, they are more than BUSTER_NODE_FRONT_VALUES, or a child is past the
// rings' reach (`reach`: the first position the rings still hold). From the token's last child back to its first by the
// children's steps: a child with a value gives it; a passing child's step is 1, so that its children are visited as the
// token's own (its last child is the position before it, and its first child's step leaves it where the passing child's
// length would); any other child gives nothing. `beyond` counts the tokens with a child out of reach.
static inline __attribute__((always_inline)) uint32_t node_flat_values(const uint32_t* words, const uint32_t* steps, uint32_t position, int64_t reach, uint32_t* values,
                                                                       uint64_t* beyond)
{
    const int64_t lower = (int64_t)position - steps[position & (BUSTER_NODE_FLAT_WINDOW - 1)] + 1;
    const int64_t bound = lower > reach ? lower : reach;
    int64_t child = (int64_t)position - 1;
    uint32_t count = 0;
    bool known = true;
    while (child >= bound && count <= BUSTER_NODE_FRONT_VALUES)
    {
        const uint32_t at = (uint32_t)child & (BUSTER_NODE_FLAT_WINDOW - 1);
        const uint32_t word = words[at];
        values[count] = word;
        count += word != BUSTER_NODE_FLAT_NONE;
        known = known && word != BUSTER_NODE_FLAT_UNKNOWN;
        child -= steps[at];
    }
    const bool out = child >= lower && count <= BUSTER_NODE_FRONT_VALUES;
    if (__builtin_expect(out, 0))
    {
        *beyond += 1;
    }
    return known && !out && count <= BUSTER_NODE_FRONT_VALUES ? count : BUSTER_NODE_FRONT_UNKNOWN;
}

// The same for a token that takes one or two values: `values[0]` its last and `values[1]` the one before, and the count
// as node_flat_values gives it but for more than two values, of which only the first two are looked at (a count no such
// token takes). At most BUSTER_NODE_FLAT_HOPS children are visited, a number the tree alone decides, so that the hops'
// positions and their count depend on nothing the front makes as it goes: each hop is its word's load and store and its
// step's load. A token with more children takes node_flat_values' loop.
static inline __attribute__((always_inline)) uint32_t node_flat_pair(const uint32_t* words, const uint32_t* steps, uint32_t position, int64_t reach, uint32_t* values,
                                                                     uint64_t* beyond)
{
    const int64_t lower = (int64_t)position - steps[position & (BUSTER_NODE_FLAT_WINDOW - 1)] + 1;
    const int64_t bound = lower > reach ? lower : reach;
    int64_t child = (int64_t)position - 1;
    uint32_t count = 0;
    for (uint32_t hop = 0; hop < BUSTER_NODE_FLAT_HOPS && child >= bound; hop += 1)
    {
        const uint32_t at = (uint32_t)child & (BUSTER_NODE_FLAT_WINDOW - 1);
        const uint32_t word = words[at];
        values[count] = word;
        count += word != BUSTER_NODE_FLAT_NONE;
        child -= steps[at];
    }
    uint32_t result = count;
    if (__builtin_expect(child >= lower, 0))
    {
        // More children than the hops, or one out of reach: the loop's answer.
        result = node_flat_values(words, steps, position, reach, values, beyond);
    }
    else if ((count >= 1 && values[0] == BUSTER_NODE_FLAT_UNKNOWN) || (count >= 2 && values[1] == BUSTER_NODE_FLAT_UNKNOWN))
    {
        result = BUSTER_NODE_FRONT_UNKNOWN;
    }
    return result;
}

// The record at the unit's place `ordinal`, stored whole: its consumer's bytes are 0 until one writes them.
static inline __attribute__((always_inline)) void node_flat_store(NodeFlatRecord* records, uint32_t ordinal, uint32_t token, uint32_t op, uint32_t count, uint32_t first,
                                                                  uint32_t second, uint32_t third, uint32_t flags, uint32_t payload)
{
    const uint64_t low = (uint64_t)token | (uint64_t)op << 32 | (uint64_t)count << 40 | (uint64_t)first << 48 | (uint64_t)second << 56;
    const uint64_t high = (uint64_t)third | (uint64_t)flags << 24 | (uint64_t)payload << 32;
    uint8_t* record = (uint8_t*)(records + (ordinal & (BUSTER_NODE_FLAT_RECORDS - 1)));
    memcpy(record, &low, sizeof(low));
    memcpy(record + sizeof(low), &high, sizeof(high));
}

// The first half of the record at `ordinal`, its token, operator, operand count and first two distances, stored over
// the first form its place gave it (node_flat_places): the consumer's bytes, in the second half, are another's to write.
static inline __attribute__((always_inline)) void node_flat_fill(NodeFlatRecord* records, uint32_t ordinal, uint32_t token, uint32_t op, uint32_t count, uint32_t first,
                                                                 uint32_t second)
{
    const uint64_t low = (uint64_t)token | (uint64_t)op << 32 | (uint64_t)count << 40 | (uint64_t)first << 48 | (uint64_t)second << 56;
    memcpy(records + (ordinal & (BUSTER_NODE_FLAT_RECORDS - 1)), &low, sizeof(low));
}

// An operand's distance back from the record at `ordinal`, in a byte.
static inline uint32_t node_flat_distance(uint32_t ordinal, uint32_t operand)
{
    const uint32_t distance = ordinal - operand;
    return distance < BUSTER_NODE_FRONT_FAR ? distance : BUSTER_NODE_FRONT_FAR;
}

// The record at `operand` taken by the one at `ordinal`, at `place`, `distance` back in a byte (node_flat_distance's):
// the consumer's distance forward and the place, both in one store, and the consumer's place itself where the distance
// doesn't say it. The ring holds the record: a taken value is a usable one (node_flat_usable).
static inline __attribute__((always_inline)) void node_flat_take(NodeFlat* flat, uint32_t ordinal, uint32_t operand, uint32_t distance, uint32_t place)
{
    const uint16_t bytes = (uint16_t)(distance | (place < 255 ? place : 255) << 8);
    memcpy(&flat->records[operand & (BUSTER_NODE_FLAT_RECORDS - 1)].up, &bytes, sizeof(bytes));
    if (__builtin_expect(distance == BUSTER_NODE_FRONT_FAR, 0))
    {
        flat->consumers[operand & (BUSTER_NODE_FLAT_RECORDS - 1)] = ordinal;
    }
}

// Whether the value at place `operand` is one a token of this chunk can take: a record the ring holds, and not an
// earlier chunk's that is empty, whose consumers that chunk couldn't reach. A record of this chunk is taken as it is:
// one that is left empty makes its consumers empty afterwards (node_flat_empties).
static inline __attribute__((always_inline)) bool node_flat_usable(const NodeFlat* flat, uint32_t ordinal, uint32_t operand)
{
    bool usable = ordinal - operand < BUSTER_NODE_FLAT_RECORDS;
    if (__builtin_expect(operand < flat->chunk_ordinal, 0))
    {
        usable = usable && flat->records[operand & (BUSTER_NODE_FLAT_RECORDS - 1)].op != BUSTER_NODE_FLAT_EMPTY;
    }
    return usable;
}

// The chunk's places, in the tokens' order: each visited token's first record's place, its word (its place where it
// gives its record, else the mark of what it gives), and its records' first form: a leaf's whole record, and an
// operator's empty one, which its class's loop fills. The running sum is in a register and waits for no load of the
// pass: a token's count and what it gives are bytes the earlier passes made.
//
// A statement's terminator is treated whole here, where its values' words are already stored: its values counted by
// its hops, and each discarded, a CAST to void, or tested in a condition's context, in source order, as the lean
// front's terminator does. One with an unknown value, or with more values than its context takes, makes none.
static __attribute__((noinline)) void node_flat_places(NodeFlat* flat, CheckInputReader* reader, uint32_t work, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const uint32_t token_count = parser->tokens.count;
    const uint8_t* conditions = reader->conditions;
    const uint32_t region_count = input->region_count;
    uint32_t* words = flat->words;
    const uint32_t* steps = flat->steps;
    NodeFlatRecord* records = flat->records;
    uint32_t ordinal = flat->ordinal;
    uint32_t values[BUSTER_NODE_FRONT_VALUES + 2];
    for (uint32_t i = 0; i < work; i += 1)
    {
        const uint32_t position = flat->positions[i];
        const uint32_t meta = flat->metas[i];
        flat->places[i] = ordinal;
        if (__builtin_expect((meta & BUSTER_NODE_FLAT_META_STATEMENT) == 0, 1))
        {
            // The record is written whether or not the token makes one: the next record overwrites one that is none.
            const uint32_t gives = (meta & BUSTER_NODE_FLAT_META_GIVES) >> BUSTER_NODE_FLAT_META_GIVES_SHIFT;
            memcpy(records + (ordinal & (BUSTER_NODE_FLAT_RECORDS - 1)), flat->bases + i, sizeof(NodeFlatRecord));
            words[position & (BUSTER_NODE_FLAT_WINDOW - 1)] = gives == BUSTER_NODE_FLAT_GIVES_RECORD ? ordinal : ~gives;
            ordinal += meta & BUSTER_NODE_FLAT_META_RECORD;
        }
        else
        {
            // Most statements are one expression, the terminator's one child: its length is the terminator's less one.
            // Its word is then the statement's one value, or nothing (a declaration, a return, a jump), with no hop.
            const uint32_t token = flat->tokens[i];
            const uint32_t length = steps[position & (BUSTER_NODE_FLAT_WINDOW - 1)];
            const uint32_t child = (position - 1) & (BUSTER_NODE_FLAT_WINDOW - 1);
            const bool only = length >= 2 && steps[child] == length - 1 && (int64_t)position - 1 >= reach;
            uint32_t count = 0;
            if (__builtin_expect(only, 1))
            {
                const uint32_t word = words[child];
                values[0] = word;
                count = word == BUSTER_NODE_FLAT_NONE ? 0 : word == BUSTER_NODE_FLAT_UNKNOWN ? BUSTER_NODE_FRONT_UNKNOWN : 1;
            }
            else if (length >= 2)
            {
                count = node_flat_pair(words, steps, position, reach, values, beyond);
                if (__builtin_expect(count > 2 && count != BUSTER_NODE_FRONT_UNKNOWN, 0))
                {
                    count = node_flat_values(words, steps, position, reach, values, beyond);
                }
            }
            if (count != 0)
            {
                const bool condition = conditions[token] != 0;
                bool region = false;
                if (__builtin_expect(region_count != 0, 0))
                {
                    const uint32_t parent = parser->tree.parents[token];
                    uint32_t selected = parent < token_count ? input->expression_regions[parent] : 0;
                    if (selected == 0 && parent < token_count && parser->tree.roles[parent] == PARSE_ROLE_LABEL)
                    {
                        selected = check_input_labeled_region(reader, parent);
                    }
                    region = selected != 0 && input->regions[selected - 1].final_statement == token;
                }
                bool left = count == BUSTER_NODE_FRONT_UNKNOWN || (condition && count > 1) || (region && count != 1);
                for (uint32_t k = 0; !left && k < count; k += 1)
                {
                    left = !node_flat_usable(flat, ordinal, values[k]);
                }
                if (__builtin_expect(left, 0))
                {
                    *lefts += 1;
                }
                for (uint32_t k = 0; !left && k < count; k += 1)
                {
                    const uint32_t value = values[count - 1 - k];
                    const uint32_t distance = node_flat_distance(ordinal, value);
                    node_flat_store(records, ordinal, token, condition ? CHECK_OP_CONDITION : CHECK_OP_CAST, 1, distance, 0, 0, BUSTER_NODE_FLAT_ROOT, 0);
                    node_flat_take(flat, ordinal, value, distance, 0);
                    ordinal += 1;
                }
            }
        }
    }
    flat->ordinal = ordinal;
}

// A record the class's loop leaves empty, as its place gave it: listed, for its consumers (node_flat_empties).
static inline __attribute__((always_inline)) void node_flat_leave(NodeFlat* flat, uint32_t place, uint64_t* lefts)
{
    flat->empties[flat->empty_count] = place;
    flat->empty_count += 1;
    *lefts += 1;
}

// The records of the chunk's tokens of class `code`, each an operator of one value: its operator by its token's kind
// or its own column, as the lean front's case of the class has it, its value by its hops, and its record's first half
// filled where it has exactly that one value. `code` is a constant where this is inlined: each class has its own loop.
static inline __attribute__((always_inline)) void node_flat_unaries(NodeFlat* flat, CheckInputReader* reader, const uint32_t code, int64_t reach, uint64_t* beyond,
                                                                    uint64_t* lefts)
{
    const Parser* parser = reader->parser;
    const uint32_t token_count = parser->tokens.count;
    const uint8_t* kinds = parser->tokens.kinds;
    const uint32_t* words = flat->words;
    const uint32_t* steps = flat->steps;
    const uint32_t* list = flat->lists[code];
    const uint32_t count = flat->list_counts[code];
    uint32_t values[BUSTER_NODE_FRONT_VALUES + 2];
    for (uint32_t j = 0; j < count; j += 1)
    {
        const uint32_t i = list[j];
        const uint32_t token = flat->tokens[i];
        const uint32_t place = flat->places[i];
        // A braced initializer's assignment makes no record (node_flat_leaves).
        if (code != CHECK_INPUT_CODE_INITIALIZE || (flat->metas[i] & BUSTER_NODE_FLAT_META_RECORD) != 0)
        {
            const uint32_t taken = node_flat_pair(words, steps, flat->positions[i], reach, values, beyond);
            uint32_t op = CHECK_OP_COUNT;
            switch (code)
            {
                case CHECK_INPUT_CODE_PREFIX:
                    op = flat->prefix_ops[kinds[token]];
                    break;
                case CHECK_INPUT_CODE_POSTFIX:
                    op = kinds[token] == PARSE_KIND_PLUSPLUS ? CHECK_OP_POST_INCREMENT : kinds[token] == PARSE_KIND_MINUSMINUS ? CHECK_OP_POST_DECREMENT : CHECK_OP_COUNT;
                    break;
                case CHECK_INPUT_CODE_CAST:
                    op = reader->query_types[token] != 0 ? CHECK_OP_CAST : CHECK_OP_COUNT;
                    break;
                case CHECK_INPUT_CODE_MEMBER:
                    op = !(token + 1 < token_count && parser->tree.roles[token + 1] == PARSE_ROLE_MEMBER) ? CHECK_OP_COUNT :
                         kinds[token] == PARSE_KIND_ARROW ? CHECK_OP_ARROW : CHECK_OP_MEMBER;
                    break;
                case CHECK_INPUT_CODE_INITIALIZE:
                    op = CHECK_OP_INITIALIZE;
                    break;
                case CHECK_INPUT_CODE_CONDITION:
                {
                    const uint32_t keyword = reader->first_child[token];
                    op = keyword < token_count && kinds[keyword] == PARSE_KIND_SWITCH ? CHECK_OP_POSITIVE : CHECK_OP_CONDITION;
                    break;
                }
                default:
                    op = reader->return_types[token] != 0 ? CHECK_OP_CONVERT : CHECK_OP_COUNT;
                    break;
            }
            if (__builtin_expect(op != CHECK_OP_COUNT && taken == 1 && node_flat_usable(flat, place, values[0]), 1))
            {
                const uint32_t distance = node_flat_distance(place, values[0]);
                node_flat_fill(flat->records, place, token, op, 1, distance, 0);
                node_flat_take(flat, place, values[0], distance, 0);
            }
            else
            {
                node_flat_leave(flat, place, lefts);
            }
        }
    }
}

// The same for the classes of two values: a binary operator, by its token's kind, and a subscript.
static inline __attribute__((always_inline)) void node_flat_binaries(NodeFlat* flat, CheckInputReader* reader, const uint32_t code, int64_t reach, uint64_t* beyond,
                                                                     uint64_t* lefts)
{
    const uint8_t* kinds = reader->parser->tokens.kinds;
    const uint32_t* words = flat->words;
    const uint32_t* steps = flat->steps;
    const uint32_t* list = flat->lists[code];
    const uint32_t count = flat->list_counts[code];
    uint32_t values[BUSTER_NODE_FRONT_VALUES + 2];
    for (uint32_t j = 0; j < count; j += 1)
    {
        const uint32_t i = list[j];
        const uint32_t token = flat->tokens[i];
        const uint32_t place = flat->places[i];
        const uint32_t taken = node_flat_pair(words, steps, flat->positions[i], reach, values, beyond);
        const uint32_t op = code == CHECK_INPUT_CODE_BINARY ? flat->binary_ops[kinds[token]] : CHECK_OP_SUBSCRIPT;
        if (__builtin_expect(op != CHECK_OP_COUNT && taken == 2 && node_flat_usable(flat, place, values[0]) && node_flat_usable(flat, place, values[1]), 1))
        {
            // In source order: the one before, then the last.
            const uint32_t first = node_flat_distance(place, values[1]);
            const uint32_t second = node_flat_distance(place, values[0]);
            node_flat_fill(flat->records, place, token, op, 2, first, second);
            node_flat_take(flat, place, values[0], second, 1);
            node_flat_take(flat, place, values[1], first, 0);
        }
        else
        {
            node_flat_leave(flat, place, lefts);
        }
    }
}

// The chunk's conditionals: three values, or two with the middle omitted (x ?: y), which takes its first value twice.
static __attribute__((noinline)) void node_flat_ternaries(NodeFlat* flat, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    const uint32_t* list = flat->lists[CHECK_INPUT_CODE_TERNARY];
    const uint32_t count = flat->list_counts[CHECK_INPUT_CODE_TERNARY];
    uint32_t values[BUSTER_NODE_FRONT_VALUES + 2];
    for (uint32_t j = 0; j < count; j += 1)
    {
        const uint32_t i = list[j];
        const uint32_t token = flat->tokens[i];
        const uint32_t place = flat->places[i];
        const uint32_t taken = node_flat_values(flat->words, flat->steps, flat->positions[i], reach, values, beyond);
        bool usable = taken == 2 || taken == 3;
        for (uint32_t k = 0; usable && k < taken; k += 1)
        {
            usable = node_flat_usable(flat, place, values[k]);
        }
        if (usable)
        {
            // In source order, last first in `values`. From the last operand to the first, as the lean front: an operand
            // taken twice ends at its first place.
            const uint32_t a = values[taken - 1];
            const uint32_t b = taken == 2 ? a : values[1];
            const uint32_t c = values[0];
            node_flat_fill(flat->records, place, token, CHECK_OP_CONDITIONAL, 3, node_flat_distance(place, a), node_flat_distance(place, b));
            flat->records[place & (BUSTER_NODE_FLAT_RECORDS - 1)].distances[2] = (uint8_t)node_flat_distance(place, c);
            node_flat_take(flat, place, c, node_flat_distance(place, c), 2);
            node_flat_take(flat, place, b, node_flat_distance(place, b), 1);
            node_flat_take(flat, place, a, node_flat_distance(place, a), 0);
        }
        else
        {
            node_flat_leave(flat, place, lefts);
        }
    }
}

// The chunk's calls: the callee and the arguments, the callee's distance in the record, and each argument's in the
// chunk's list where that has room, first argument first.
static __attribute__((noinline)) void node_flat_calls(NodeFlat* flat, CheckInputReader* reader, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    const CheckInput* input = reader->output;
    const bool refused = input->kr_parameter_count != 0;
    const uint32_t* list = flat->lists[CHECK_INPUT_CODE_CALL];
    const uint32_t count = flat->list_counts[CHECK_INPUT_CODE_CALL];
    uint32_t arguments = 0;
    uint32_t values[BUSTER_NODE_FRONT_VALUES + 2];
    for (uint32_t j = 0; j < count; j += 1)
    {
        const uint32_t i = list[j];
        const uint32_t token = flat->tokens[i];
        const uint32_t place = flat->places[i];
        const uint32_t taken = node_flat_values(flat->words, flat->steps, flat->positions[i], reach, values, beyond);
        bool usable = taken != BUSTER_NODE_FRONT_UNKNOWN && taken != 0 && !refused;
        for (uint32_t k = 0; usable && k < taken; k += 1)
        {
            usable = node_flat_usable(flat, place, values[k]);
        }
        if (__builtin_expect(usable, 1))
        {
            const uint32_t callee = values[taken - 1];
            const bool listed = arguments + taken - 1 <= BUSTER_NODE_FLAT_ARGUMENTS;
            node_flat_fill(flat->records, place, token, CHECK_OP_CALL, taken < 255 ? taken : 255, node_flat_distance(place, callee), 0);
            flat->records[place & (BUSTER_NODE_FLAT_RECORDS - 1)].payload = listed ? arguments : BUSTER_NODE_FLAT_NONE;
            node_flat_take(flat, place, callee, node_flat_distance(place, callee), 0);
            for (uint32_t k = 1; k < taken; k += 1)
            {
                const uint32_t argument = values[taken - 1 - k];
                const uint32_t distance = node_flat_distance(place, argument);
                node_flat_take(flat, place, argument, distance, k);
                if (listed)
                {
                    flat->argument_distances[arguments] = (uint8_t)distance;
                    arguments += 1;
                }
            }
        }
        else
        {
            node_flat_leave(flat, place, lefts);
        }
    }
    flat->argument_count = arguments;
}

// Each class's loop, out of line, so that a profile names it.
static __attribute__((noinline)) void node_flat_prefixes(NodeFlat* flat, CheckInputReader* reader, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    node_flat_unaries(flat, reader, CHECK_INPUT_CODE_PREFIX, reach, beyond, lefts);
}

static __attribute__((noinline)) void node_flat_postfixes(NodeFlat* flat, CheckInputReader* reader, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    node_flat_unaries(flat, reader, CHECK_INPUT_CODE_POSTFIX, reach, beyond, lefts);
}

static __attribute__((noinline)) void node_flat_casts(NodeFlat* flat, CheckInputReader* reader, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    node_flat_unaries(flat, reader, CHECK_INPUT_CODE_CAST, reach, beyond, lefts);
}

static __attribute__((noinline)) void node_flat_members(NodeFlat* flat, CheckInputReader* reader, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    node_flat_unaries(flat, reader, CHECK_INPUT_CODE_MEMBER, reach, beyond, lefts);
}

static __attribute__((noinline)) void node_flat_initializers(NodeFlat* flat, CheckInputReader* reader, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    node_flat_unaries(flat, reader, CHECK_INPUT_CODE_INITIALIZE, reach, beyond, lefts);
}

static __attribute__((noinline)) void node_flat_conditions(NodeFlat* flat, CheckInputReader* reader, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    node_flat_unaries(flat, reader, CHECK_INPUT_CODE_CONDITION, reach, beyond, lefts);
}

static __attribute__((noinline)) void node_flat_returns(NodeFlat* flat, CheckInputReader* reader, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    node_flat_unaries(flat, reader, CHECK_INPUT_CODE_RETURN, reach, beyond, lefts);
}

static __attribute__((noinline)) void node_flat_operators(NodeFlat* flat, CheckInputReader* reader, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    node_flat_binaries(flat, reader, CHECK_INPUT_CODE_BINARY, reach, beyond, lefts);
}

static __attribute__((noinline)) void node_flat_subscripts(NodeFlat* flat, CheckInputReader* reader, int64_t reach, uint64_t* beyond, uint64_t* lefts)
{
    node_flat_binaries(flat, reader, CHECK_INPUT_CODE_SUBSCRIPT, reach, beyond, lefts);
}

// The record at `place` made empty, where it isn't: its operator, and its operands released, their consumer's bytes
// cleared where the record's distances reach them (the lean front's left token gives its values no consumer). Returns
// whether it was a record.
static bool node_flat_empty(NodeFlat* flat, uint32_t place)
{
    NodeFlatRecord* record = &flat->records[place & (BUSTER_NODE_FLAT_RECORDS - 1)];
    const bool was = record->op != BUSTER_NODE_FLAT_EMPTY;
    if (was)
    {
        const uint16_t none = 0;
        for (uint32_t k = 0; k < 3; k += 1)
        {
            const uint32_t distance = record->distances[k];
            if (distance != 0 && distance != BUSTER_NODE_FRONT_FAR)
            {
                memcpy(&flat->records[(place - distance) & (BUSTER_NODE_FLAT_RECORDS - 1)].up, &none, sizeof(none));
            }
        }
        if (record->op == CHECK_OP_CALL && record->payload != BUSTER_NODE_FLAT_NONE)
        {
            for (uint32_t k = 1; k < record->count; k += 1)
            {
                const uint32_t distance = flat->argument_distances[record->payload + k - 1];
                if (distance != 0 && distance != BUSTER_NODE_FRONT_FAR)
                {
                    memcpy(&flat->records[(place - distance) & (BUSTER_NODE_FLAT_RECORDS - 1)].up, &none, sizeof(none));
                }
            }
        }
        record->op = BUSTER_NODE_FLAT_EMPTY;
        record->count = 0;
        record->distances[0] = 0;
        record->distances[1] = 0;
        record->distances[2] = 0;
    }
    return was;
}

// The chunk's empty records' consumers made empty in turn: from each record a class's loop left, up its consumer's
// distance forward (its place, where the distance doesn't say it), until a record that has no consumer or is empty
// already, whose own chain is walked from itself. A statement's discards are empty together, as the lean front's
// terminator makes none where one value is unknown: they are the records around it at the same token.
static __attribute__((noinline)) void node_flat_empties(NodeFlat* flat, uint64_t* lefts)
{
    for (uint32_t e = 0; e < flat->empty_count; e += 1)
    {
        uint32_t place = flat->empties[e];
        bool walking = true;
        while (walking)
        {
            const NodeFlatRecord* record = &flat->records[place & (BUSTER_NODE_FLAT_RECORDS - 1)];
            const uint32_t up = record->up;
            const uint32_t consumer = up == 0 ? BUSTER_NODE_FLAT_NONE : up == BUSTER_NODE_FRONT_FAR ? flat->consumers[place & (BUSTER_NODE_FLAT_RECORDS - 1)] : place + up;
            walking = consumer != BUSTER_NODE_FLAT_NONE && consumer - flat->chunk_ordinal < flat->ordinal - flat->chunk_ordinal && node_flat_empty(flat, consumer);
            if (walking)
            {
                const NodeFlatRecord* made = &flat->records[consumer & (BUSTER_NODE_FLAT_RECORDS - 1)];
                *lefts += 1;
                if ((made->flags & BUSTER_NODE_FLAT_ROOT) != 0 && made->up == 0)
                {
                    // A root: where it is a statement's discard, the statement's other discards, which have its token.
                    for (uint32_t other = consumer; other != flat->chunk_ordinal && flat->records[(other - 1) & (BUSTER_NODE_FLAT_RECORDS - 1)].token == made->token; other -= 1)
                    {
                        (void)node_flat_empty(flat, other - 1);
                    }
                    for (uint32_t other = consumer + 1; other != flat->ordinal && flat->records[other & (BUSTER_NODE_FLAT_RECORDS - 1)].token == made->token; other += 1)
                    {
                        (void)node_flat_empty(flat, other);
                    }
                }
                place = consumer;
            }
        }
    }
}

static void node_flat_differs(NodeFrontShadow* shadow, uint32_t token, uint32_t field);

// The flat front's passes over a chunk (this file's first comment), with the chunk's literals' entries
// (check_input_expression_literals). The compaction's ticks and the other passes' are apart, and each stage's.
static __attribute__((noinline)) void node_front_chunk_flat(NodeFlat* flat, CheckInputReader* reader, const CheckInputLiterals* literals)
{
    NodeFrontShadow* shadow = flat->shadow;
    const uint32_t token_count = reader->parser->tokens.count;
    const uint32_t first = flat->position;
    const uint32_t end = token_count - first < BUSTER_CHECK_EXPRESSION_CHUNK ? token_count : first + BUSTER_CHECK_EXPRESSION_CHUNK;
    // The first position the rings still hold, once the chunk's words and steps are in them.
    const int64_t reach = end > BUSTER_NODE_FLAT_WINDOW ? (int64_t)end - BUSTER_NODE_FLAT_WINDOW : 0;
    uint64_t beyond = 0;
    uint64_t lefts = 0;
    uint64_t ticks[BUSTER_NODE_FLAT_STAGE_EMPTIES + 2];
    ticks[0] = __rdtsc();
    const uint32_t work = node_flat_compact(reader, first, end, flat->tokens, flat->classes, flat->positions, flat->words, flat->steps, false);
    ticks[1] = __rdtsc();
    flat->chunk_ordinal = flat->ordinal;
    flat->chunk_first = first;
    flat->position = end;
    flat->work = work;
    flat->empty_count = 0;
    flat->argument_count = 0;
    node_flat_classify(flat, work, false);
    ticks[2] = __rdtsc();
    uint32_t own_literals = 0;
    if (flat->window_literals != NULL)
    {
        // The window producer has its own compacted token and class columns. Decode their literals before writing
        // its leaves. This time is included in the flat structural passes as well as reported separately.
        own_literals = check_input_expression_literals(reader, flat->tokens, flat->classes, work, flat->window_literals);
        shadow->flat_window_literal_ticks += __rdtsc() - ticks[2];
    }
    node_flat_leaves(flat, reader, flat->window_literals != NULL ? flat->window_literals : literals);
    ticks[3] = __rdtsc();
    node_flat_places(flat, reader, work, reach, &beyond, &lefts);
    ticks[4] = __rdtsc();
    node_flat_prefixes(flat, reader, reach, &beyond, &lefts);
    node_flat_postfixes(flat, reader, reach, &beyond, &lefts);
    node_flat_casts(flat, reader, reach, &beyond, &lefts);
    node_flat_members(flat, reader, reach, &beyond, &lefts);
    node_flat_initializers(flat, reader, reach, &beyond, &lefts);
    node_flat_conditions(flat, reader, reach, &beyond, &lefts);
    node_flat_returns(flat, reader, reach, &beyond, &lefts);
    node_flat_operators(flat, reader, reach, &beyond, &lefts);
    node_flat_subscripts(flat, reader, reach, &beyond, &lefts);
    node_flat_ternaries(flat, reach, &beyond, &lefts);
    node_flat_calls(flat, reader, reach, &beyond, &lefts);
    ticks[5] = __rdtsc();
    node_flat_empties(flat, &lefts);
    ticks[6] = __rdtsc();
    for (uint32_t stage = 0; stage <= BUSTER_NODE_FLAT_STAGE_EMPTIES; stage += 1)
    {
        shadow->flat_stage_ticks[stage] += ticks[stage + 1] - ticks[stage];
    }
    shadow->flat_ticks += ticks[6] - ticks[1];
    shadow->flat_compact_ticks += ticks[1] - ticks[0];
    shadow->flat_tokens += work;
    shadow->flat_beyond += beyond;
    shadow->flat_left += lefts;
    if (flat->window_literals != NULL)
    {
        const uint32_t count = flat->list_counts[CHECK_INPUT_CODE_LITERAL];
        if (count != own_literals || memcmp(literals->values, flat->window_literals->values, count * sizeof(uint32_t)) != 0)
        {
            node_flat_differs(shadow, work != 0 ? flat->tokens[0] : 0, BUSTER_NODE_FLAT_FIELD_LEAF);
        }
    }
}

// A difference between the flat front and the lean one, counted and, the first ones, listed.
static void node_flat_differs(NodeFrontShadow* shadow, uint32_t token, uint32_t field)
{
    if (shadow->flat_differences < BUSTER_NODE_FRONT_LISTED)
    {
        shadow->flat_difference_tokens[shadow->flat_differences] = token;
        shadow->flat_difference_fields[shadow->flat_differences] = field;
    }
    shadow->flat_differences += 1;
}

// The empty records among the places from `low` to `high`, neither counted.
static uint32_t node_flat_empties_between(const NodeFlat* flat, uint32_t low, uint32_t high)
{
    uint32_t empties = 0;
    for (uint32_t place = low + 1; place < high; place += 1)
    {
        empties += flat->records[place & (BUSTER_NODE_FLAT_RECORDS - 1)].op == BUSTER_NODE_FLAT_EMPTY;
    }
    return empties;
}

// The flat front's chunk against the lean front's, after both made it (the lean front's records compared with the walk's
// nodes already). The compaction and the classes against their scalar twins. Then the two fronts' records of the chunk
// in postorder, a pair at a token (a terminator's discards in their order), an empty flat record being none. A pair must
// agree in every field: the operator, the operand count, the root bit, a leaf's kind and value, and the distances and
// the consumer's two bytes as they are now, the flat front's less the empty records between (the lean front has none
// there). A flat record's operands, by its distances, must carry its distance and their places, whichever chunk made
// them, and a call's list must name its arguments. A record only one front made is counted.
static void node_flat_compare(NodeFlat* flat, const NodeFront* front, const CheckInputReader* reader)
{
    NodeFrontShadow* shadow = flat->shadow;
    const uint32_t* placed = reader->postorder_positions;
    const uint32_t first = flat->chunk_first;
    const uint32_t end = flat->position;
    const uint32_t difference_token = first < reader->parser->tokens.count ? reader->postorder[first] : 0;
    // The compaction's twin: the same tokens, bytes and positions, and the same step at every position but a braced
    // initializer's assignment's (the leaves' own 1).
    const uint64_t scratch_mark = reader->scratch->length;
    uint32_t* twin = arena_push_array(reader->scratch, 3 * (uint64_t)(BUSTER_CHECK_EXPRESSION_CHUNK + 16) + 2 * (uint64_t)BUSTER_NODE_FLAT_WINDOW, sizeof(uint32_t), 64);
    NodeFlat* classified = arena_push_array(reader->scratch, 1, sizeof(NodeFlat), 64);
    if (twin != NULL && classified != NULL)
    {
        uint32_t* tokens = twin;
        uint32_t* classes = twin + BUSTER_CHECK_EXPRESSION_CHUNK + 16;
        uint32_t* positions = twin + 2 * (BUSTER_CHECK_EXPRESSION_CHUNK + 16);
        uint32_t* words = twin + 3 * (BUSTER_CHECK_EXPRESSION_CHUNK + 16);
        const uint32_t* twin_steps = words + BUSTER_NODE_FLAT_WINDOW;
        const uint32_t work = node_flat_compact(reader, first, end, tokens, classes, positions, words, words + BUSTER_NODE_FLAT_WINDOW, true);
        bool same = work == flat->work;
        for (uint32_t i = 0; same && i < work; i += 1)
        {
            same = tokens[i] == flat->tokens[i] && classes[i] == flat->classes[i] && positions[i] == flat->positions[i];
        }
        uint32_t visited = 0;
        for (uint32_t position = first; same && position < end; position += 1)
        {
            const uint32_t at = position & (BUSTER_NODE_FLAT_WINDOW - 1);
            const bool mine = visited < work && positions[visited] == position;
            const uint32_t code = mine ? (classes[visited] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT : 0;
            const bool braced = mine && code == CHECK_INPUT_CODE_INITIALIZE && reader->initializers[tokens[visited]] == 2;
            visited += mine;
            same = (mine || flat->words[at] == BUSTER_NODE_FLAT_NONE) && flat->steps[at] == (braced ? 1 : twin_steps[at]);
        }
        if (!same)
        {
            node_flat_differs(shadow, difference_token, BUSTER_NODE_FLAT_FIELD_COMPACTION);
        }
        // The classes' twin: each token's class, its record's first form and its place on its class's list, in order.
        // Its byte and its record's payload are compared where the classes alone say them: a leaf's and a braced
        // initializer's are its own.
        memcpy(classified->classes, flat->classes, sizeof(flat->classes));
        memcpy(classified->tokens, flat->tokens, sizeof(flat->tokens));
        node_flat_classify(classified, flat->work, true);
        same = memcmp(classified->list_counts, flat->list_counts, sizeof(flat->list_counts)) == 0;
        for (uint32_t i = 0; same && i < flat->work; i += 1)
        {
            const uint32_t code = classified->codes[i];
            const bool own = code == CHECK_INPUT_CODE_IDENTIFIER || code == CHECK_INPUT_CODE_LITERAL || code == CHECK_INPUT_CODE_STRING || code == CHECK_INPUT_CODE_INITIALIZE;
            const NodeFlatRecord* twin_base = classified->bases + i;
            const NodeFlatRecord* base = flat->bases + i;
            same = code == flat->codes[i] && (own || (classified->metas[i] == flat->metas[i] && twin_base->payload == base->payload)) && twin_base->token == base->token &&
                   twin_base->op == base->op && twin_base->flags == base->flags && base->count == 0 && base->distances[0] == 0 && base->distances[1] == 0 &&
                   base->distances[2] == 0 && base->up == 0 && base->place == 0;
        }
        for (uint32_t code = 0; same && code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
        {
            same = memcmp(classified->lists[code], flat->lists[code], (uint64_t)flat->list_counts[code] * sizeof(uint32_t)) == 0;
        }
        if (!same)
        {
            node_flat_differs(shadow, difference_token, BUSTER_NODE_FLAT_FIELD_CLASSES);
        }
    }
    arena_pop_to(reader->scratch, scratch_mark);
    // The children the chunk's tokens hopped over, counted here, outside the passes: each token that takes values, from
    // its last child back by the steps.
    for (uint32_t i = 0; i < flat->work; i += 1)
    {
        const uint32_t code = flat->codes[i];
        const bool leaf = code == CHECK_INPUT_CODE_IDENTIFIER || code == CHECK_INPUT_CODE_LITERAL || code == CHECK_INPUT_CODE_STRING ||
                          (node_flat_class_metas[code] & (BUSTER_NODE_FLAT_META_RECORD | BUSTER_NODE_FLAT_META_STATEMENT)) == 0;
        const bool braced = code == CHECK_INPUT_CODE_INITIALIZE && (flat->metas[i] & BUSTER_NODE_FLAT_META_RECORD) == 0;
        if (!leaf && !braced)
        {
            const int64_t position = flat->positions[i];
            const int64_t reach = end > BUSTER_NODE_FLAT_WINDOW ? (int64_t)end - BUSTER_NODE_FLAT_WINDOW : 0;
            const int64_t lower = position - flat->steps[position & (BUSTER_NODE_FLAT_WINDOW - 1)] + 1;
            for (int64_t child = position - 1; child >= lower && child >= reach; child -= flat->steps[child & (BUSTER_NODE_FLAT_WINDOW - 1)])
            {
                shadow->flat_hops += 1;
            }
        }
    }
    // The chunk's records only the flat front made, a bit a place, and the records whose consumer the lean front doesn't
    // have though the flat front does: such a consumer must be one only the flat front made (the lean front's stack
    // was full, and it left the token that takes the value).
    uint64_t only[BUSTER_NODE_FLAT_RECORDS / 64] = { 0 };
    uint32_t orphans[256];
    uint32_t orphan_count = 0;
    // Their mirror: the records only the lean front made, a bit a record of its own, and the records whose consumer
    // the flat front doesn't have though the lean front does. Such a consumer must be one only the lean front made:
    // the flat front left the token that takes the value, because another of its values is past the rings' reach,
    // where the lean front's stack keeps a value however far its consumer is (a declaration's `;` after a braced
    // initializer of long values, or after thousands of adjacent strings). A consumer further than the byte holds is
    // matched by count with the far operands of the records only the lean front made.
    uint64_t lean_only[BUSTER_NODE_FRONT_RECORDS / 64] = { 0 };
    uint32_t widows[256];
    uint32_t widow_count = 0;
    uint32_t far_takers = 0;
    uint32_t last_far_taker = 0;
    uint32_t theirs = front->made;
    uint32_t mine = flat->chunk_ordinal;
    while (theirs < front->count || mine != flat->ordinal)
    {
        const NodeFlatRecord* record = mine != flat->ordinal ? &flat->records[mine & (BUSTER_NODE_FLAT_RECORDS - 1)] : NULL;
        const uint32_t their_position = theirs < front->count ? placed[front->tokens[theirs]] : UINT32_MAX;
        const uint32_t my_position = record != NULL ? placed[record->token] : UINT32_MAX;
        if (record != NULL && flat->ordinal - mine >= BUSTER_NODE_FLAT_RECORDS)
        {
            // More records than the ring holds in one chunk: the first are gone (the slot after the last record may
            // hold a record's first form that was none).
            shadow->flat_unread += 1;
            mine += 1;
        }
        else if (record != NULL && record->op == BUSTER_NODE_FLAT_EMPTY)
        {
            shadow->flat_empties += 1;
            mine += 1;
        }
        else if (their_position < my_position)
        {
            lean_only[theirs >> 6 & (BUSTER_NODE_FRONT_RECORDS / 64 - 1)] |= UINT64_C(1) << (theirs & 63);
            for (uint32_t k = 0; k < 3; k += 1)
            {
                const bool far = front->distances[k][theirs] == BUSTER_NODE_FRONT_FAR;
                far_takers += far;
                last_far_taker = far ? theirs : last_far_taker;
            }
            shadow->flat_lean_only += 1;
            theirs += 1;
        }
        else if (my_position < their_position)
        {
            const uint32_t place = mine - flat->chunk_ordinal;
            only[place >> 6 & (BUSTER_NODE_FLAT_RECORDS / 64 - 1)] |= UINT64_C(1) << (place & 63);
            shadow->flat_only += 1;
            shadow->flat_records += 1;
            mine += 1;
        }
        else
        {
            const uint32_t token = record->token;
            const uint32_t kind = (record->flags & BUSTER_NODE_FLAT_LEAF_MASK) >> BUSTER_NODE_FLAT_LEAF_SHIFT;
            const bool leaf = record->op == CHECK_OP_VALUE;
            uint32_t field = 0;
            if (record->op != front->ops[theirs])
            {
                field = BUSTER_NODE_FLAT_FIELD_OPERATOR;
            }
            else if (record->count != front->counts[theirs])
            {
                field = BUSTER_NODE_FLAT_FIELD_COUNT;
            }
            else if (((record->flags & BUSTER_NODE_FLAT_ROOT) != 0) != (front->roots[theirs] != 0))
            {
                field = BUSTER_NODE_FLAT_FIELD_ROOT;
            }
            else if (leaf)
            {
                const bool same = kind != front->leaf_categories[theirs] ? false :
                                  kind == BUSTER_NODE_FRONT_LEAF_IDENTIFIER ? record->payload == front->leaf_integers[theirs] :
                                  record->payload < flat->side_count && flat->side_types[record->payload] == front->leaf_types[theirs] &&
                                  flat->side_integers[record->payload] == front->leaf_integers[theirs];
                field = same ? 0 : BUSTER_NODE_FLAT_FIELD_LEAF;
            }
            // Its distances back: each less the empty records between is the lean front's. One the byte doesn't hold
            // is the lean front's own where no empty record is counted in it, and is only counted otherwise.
            for (uint32_t k = 0; field == 0 && k < 3; k += 1)
            {
                const uint32_t distance = record->distances[k];
                const uint32_t lean = front->distances[k][theirs];
                if (distance == BUSTER_NODE_FRONT_FAR)
                {
                    shadow->flat_far += lean != BUSTER_NODE_FRONT_FAR;
                }
                else
                {
                    const uint32_t empties = distance != 0 ? node_flat_empties_between(flat, mine - distance, mine) : 0;
                    field = distance - empties == lean ? 0 : BUSTER_NODE_FLAT_FIELD_DISTANCE;
                }
            }
            // Its consumer's bytes as they are now: the distance forward less the empty records between, and the place.
            if (field == 0)
            {
                const uint32_t up = record->up;
                const uint32_t lean = front->ups[theirs];
                if (up == BUSTER_NODE_FRONT_FAR)
                {
                    shadow->flat_far += lean != BUSTER_NODE_FRONT_FAR;
                    field = record->place == front->places[theirs] ? 0 : BUSTER_NODE_FLAT_FIELD_CONSUMER;
                }
                else if (up != 0 && lean == 0 && front->places[theirs] == 0 && orphan_count < BUSTER_ARRAY_LENGTH(orphans))
                {
                    // The lean front has no consumer for it: checked below, once the chunk's records are all read.
                    orphans[orphan_count] = mine;
                    orphan_count += 1;
                }
                else if (up == 0 && record->place == 0 && lean != 0 && widow_count < BUSTER_ARRAY_LENGTH(widows))
                {
                    // The flat front has no consumer for it: checked below too.
                    widows[widow_count] = theirs;
                    widow_count += 1;
                }
                else
                {
                    const uint32_t empties = up != 0 ? node_flat_empties_between(flat, mine, mine + up) : 0;
                    field = up - empties == lean && record->place == front->places[theirs] ? 0 : BUSTER_NODE_FLAT_FIELD_CONSUMER;
                }
            }
            // Its operands from its own side: each within the rings' reach carries this record's distance and its place
            // (an operand taken twice its first), and a call's list names each argument's.
            for (uint32_t k = 0; field == 0 && k < 3; k += 1)
            {
                const uint32_t distance = record->distances[k];
                if (distance != 0 && distance != BUSTER_NODE_FRONT_FAR)
                {
                    const NodeFlatRecord* operand = &flat->records[(mine - distance) & (BUSTER_NODE_FLAT_RECORDS - 1)];
                    const uint32_t place = k == 1 && record->distances[1] == record->distances[0] ? 0 : k;
                    field = operand->up == distance && operand->place == place ? 0 : BUSTER_NODE_FLAT_FIELD_OPERAND;
                }
            }
            if (field == 0 && record->op == CHECK_OP_CALL && record->payload != BUSTER_NODE_FLAT_NONE)
            {
                bool same = record->payload + record->count - 1 <= flat->argument_count;
                for (uint32_t k = 1; same && k < record->count; k += 1)
                {
                    const uint32_t distance = flat->argument_distances[record->payload + k - 1];
                    const NodeFlatRecord* argument = &flat->records[(mine - distance) & (BUSTER_NODE_FLAT_RECORDS - 1)];
                    same = distance == BUSTER_NODE_FRONT_FAR || (distance != 0 && argument->up == distance && argument->place == k);
                }
                field = same ? 0 : BUSTER_NODE_FLAT_FIELD_ARGUMENTS;
            }
            if (field != 0)
            {
                node_flat_differs(shadow, token, field);
            }
            shadow->flat_compared += 1;
            shadow->flat_records += 1;
            theirs += 1;
            mine += 1;
        }
    }
    for (uint32_t k = 0; k < orphan_count; k += 1)
    {
        const NodeFlatRecord* record = &flat->records[orphans[k] & (BUSTER_NODE_FLAT_RECORDS - 1)];
        const uint32_t place = orphans[k] + record->up - flat->chunk_ordinal;
        const bool flat_only = record->up != BUSTER_NODE_FRONT_FAR && place < flat->ordinal - flat->chunk_ordinal && place < BUSTER_NODE_FLAT_RECORDS &&
                               (only[place >> 6] >> (place & 63) & 1) != 0;
        if (!flat_only)
        {
            node_flat_differs(shadow, record->token, BUSTER_NODE_FLAT_FIELD_CONSUMER);
        }
    }
    uint32_t far_widows = 0;
    for (uint32_t k = 0; k < widow_count; k += 1)
    {
        const uint32_t operand = widows[k];
        const uint32_t up = front->ups[operand];
        const uint32_t consumer = operand + up;
        const bool far = up == BUSTER_NODE_FRONT_FAR;
        far_widows += far;
        const bool left = far ? far_widows <= far_takers && last_far_taker >= operand + BUSTER_NODE_FRONT_FAR :
                                consumer < front->count && (lean_only[consumer >> 6 & (BUSTER_NODE_FRONT_RECORDS / 64 - 1)] >> (consumer & 63) & 1) != 0;
        if (!left)
        {
            node_flat_differs(shadow, front->tokens[operand], BUSTER_NODE_FLAT_FIELD_CONSUMER);
        }
    }
}
