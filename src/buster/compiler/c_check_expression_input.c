// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The coded walk (#1038): the vector path of the expressions' walk, which builds the expression graph from the tokens in
// postorder. The walk's action at a token (check_input_expression_act) tests the token's role, kind and columns through
// a chain of about 25 branches before it acts, and the scalar path takes it at every token, the oracle. The coded walk
// takes the class each token's visits byte holds (CheckInputExpressionCode, which check_input_expression_classes gives
// 64 tokens at a time from the role and kind tables), compacts the tokens the walk acts at into chunks in postorder with
// their classes, and acts at each by its class's own action: an identifier's leaf, a literal's, an operator's node, a list
// of values passed on, or nothing. The class's action tests what the class can't say (the values the children have, the
// columns the walk writes as it goes, the unit's rare features) and leaves every other case to the walk's visit
// (check_input_expression_visit), so that the graph, its leaves, the reports and the reader's columns are the walk's, node
// for node. The class actions number their nodes into a batch before the graph has any of them, and the batch's columns are
// written to the graph 16 nodes at a time (check_input_expression_flush, #1038's stages 2 and 4). Included by
// c_check_input.c after the walk's action.

// The postorder positions the coded walk compacts at a time: the tokens the walk acts at among them and their classes, in
// two arrays on the stack that stay in the first-level cache.
#define BUSTER_CHECK_EXPRESSION_CHUNK 1024u

// The tokens the walk acts at among postorder positions [first, end) into `tokens`, in postorder, and each one's visits
// byte into `classes`, but those whose byte is `skipped` (a class whose action is nothing, or a value no byte has);
// returns how many. `end - first` is at most BUSTER_CHECK_EXPRESSION_CHUNK, the arrays' room. The vector path gathers 16
// tokens' bytes from CheckInputReader.visits (four bytes at a token, which its padding allows) and compresses the tokens
// it keeps; the scalar path, its twin, takes a token at a time, writing each and counting the ones it keeps.
static uint32_t check_input_expression_compact(const CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t* tokens, uint32_t* classes,
                                               uint32_t skipped, bool scalar)
{
    const uint8_t* visits = reader->visits;
    const uint32_t* postorder = reader->postorder;
    // Gathers index by signed 32-bit offsets.
    const bool indexed = reader->parser->tokens.count <= (uint32_t)INT32_MAX;
    uint32_t result = 0;
    uint32_t position = first;
    for (; !BUSTER_SCALAR_PATH(scalar) && indexed && position < end; position += 16)
    {
        const uint32_t length = end - position < 16 ? end - position : 16;
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, length);
        const __m512i order = _mm512_maskz_loadu_epi32(lanes, postorder + position);
        const __m512i bytes = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, order, visits, 1);
        const __m512i visit = _mm512_and_si512(bytes, _mm512_set1_epi32(0xff));
        const __mmask16 acting = _mm512_mask_test_epi32_mask(lanes, visit, _mm512_set1_epi32(BUSTER_CHECK_EXPRESSION_ACTS)) &
                                 _mm512_cmpneq_epu32_mask(visit, _mm512_set1_epi32((int32_t)skipped));
        _mm512_mask_compressstoreu_epi32(tokens + result, acting, order);
        _mm512_mask_compressstoreu_epi32(classes + result, acting, visit);
        result += (uint32_t)__builtin_popcount(acting);
    }
    for (; position < end; position += 1)
    {
        const uint32_t token = postorder[position];
        const uint8_t visit = visits[token];
        // The compress store a token at a time, without a branch: every token is written at `result`, and the next one
        // writes over it unless counting keeps it. `result` never passes the positions read, so the room holds both.
        tokens[result] = token;
        classes[result] = visit;
        result += (visit & BUSTER_CHECK_EXPRESSION_ACTS) != 0 && visit != skipped;
    }
    return result;
}

typedef struct CheckInputValues CheckInputValues;
struct CheckInputValues
{
    uint32_t first;
    uint32_t last;
    uint32_t count;
};

// The values of `token`'s children, chained as the walk's action chains them: each child's chain after the one before
// it, through value_next, and their counts added (CheckInputReader.value_counts).
// Its branches stay: a branch-free splice (each child's last value and count taken by selects, the link stored into a
// local where there was no chain yet) made the walk's actions 0.37 ms slower in `-c sqlite3.c` on the 9700X (10.39 ms
// against 10.02 ms, `measure-compile --time-stages`), though those branches are 0.57% of the compile's misses (#529,
// #1646).
static inline CheckInputValues check_input_expression_values(CheckInputReader* reader, uint32_t token)
{
    CheckInputValues values = { .first = BUSTER_CHECK_NONE, .last = BUSTER_CHECK_NONE, .count = 0 };
    for (uint32_t child = reader->first_child[token]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
    {
        const uint32_t begin = reader->first_value[child];
        if (begin != BUSTER_CHECK_NONE)
        {
            if (values.last != BUSTER_CHECK_NONE)
            {
                reader->value_next[values.last] = begin;
            }
            else
            {
                values.first = begin;
            }
            values.last = reader->last_value[child];
            values.count += reader->value_counts[child];
        }
    }
    return values;
}

// The graph by passes (#1038's stages 2 and 4): the class actions number their nodes into a batch before any is written
// to the graph, and the batch's columns are written at once (check_input_expression_flush), 16 nodes at a time where the
// walk wrote each node's 17 columns as it made it. A class action appends its node's operator, token, operands and form
// to the batch, an array a column on the scratch that stays in the first- and second-level caches, and takes its id, the
// graph's count before the batch plus its place in it; the flush then writes the columns, the token columns every node
// reads (the type the token's query asked for, a static initializer's context, where its expression ends, its warnings'
// suppression and floating-point environment) gathered 16 tokens at a time, and records the nodes. The walk's visit, which
// reads and writes the graph itself, takes the batch's nodes into the graph first, and so does a token that the batch has
// no room for. A batch's node is the graph's node it will be: the actions read its token, its operator and its first
// operand through the batch (check_input_batch_token and the like), and write the columns their node takes after it is
// made (a member's name, a call's arguments, a list's value context) into the batch.
#define BUSTER_CHECK_EXPRESSION_BATCH 1024u

// How a batch's node takes its type and context columns: an operator's from its token's query and static initializer
// (check_input_expression_operator), a leaf's none (check_input_batch_add's VALUE), a discard's void type
// (check_input_expression_terminator), and an initializer's from its token's query, with a static initializer's value
// context (check_input_expression_initialize).
typedef enum CheckInputNodeForm
{
    CHECK_INPUT_FORM_OPERATOR,
    CHECK_INPUT_FORM_LEAF,
    CHECK_INPUT_FORM_DISCARD,
    CHECK_INPUT_FORM_INITIALIZER,
} CheckInputNodeForm;

typedef struct CheckInputBatch CheckInputBatch;
struct CheckInputBatch
{
    // The graph's count and leaf count before the batch: its node k is node base + k, and its leaf j leaf leaf_base + j.
    uint32_t base;
    uint32_t leaf_base;
    uint32_t count;
    uint32_t leaves;
    // The batch's nodes' columns, as check_graph_add and the actions write them; the flush gathers the rest.
    uint32_t ops[BUSTER_CHECK_EXPRESSION_BATCH];
    uint32_t tokens[BUSTER_CHECK_EXPRESSION_BATCH];
    uint32_t operand0[BUSTER_CHECK_EXPRESSION_BATCH];
    uint32_t operand1[BUSTER_CHECK_EXPRESSION_BATCH];
    uint32_t operand2[BUSTER_CHECK_EXPRESSION_BATCH];
    uint32_t leaf_slots[BUSTER_CHECK_EXPRESSION_BATCH];
    uint32_t members[BUSTER_CHECK_EXPRESSION_BATCH];
    uint32_t argument_offsets[BUSTER_CHECK_EXPRESSION_BATCH];
    uint32_t argument_counts[BUSTER_CHECK_EXPRESSION_BATCH];
    uint8_t forms[BUSTER_CHECK_EXPRESSION_BATCH];
    // The contexts the actions add to a node after it is made: a static list's element's BUSTER_CHECK_CONTEXT_VALUE.
    uint8_t contexts[BUSTER_CHECK_EXPRESSION_BATCH];
    // check_graph_classes' table, which the flush's vector path records its nodes by (check_input_batch_record), made once
    // a walk.
    _Alignas(64) uint8_t classes[BUSTER_CHECK_CLASS_OPERATORS];
};

_Static_assert(sizeof(CheckOp) == sizeof(uint32_t) && sizeof(TypeId) == sizeof(uint32_t), "the batch copies operators and types as 32-bit words");

// The batch's place of `node`, or BUSTER_CHECK_NONE for a node the graph already holds (or none).
static inline uint32_t check_input_batch_place(const CheckInputBatch* batch, uint32_t node)
{
    return node - batch->base < batch->count ? node - batch->base : BUSTER_CHECK_NONE;
}

// `node`'s token, operator and first operand, from the batch while it holds the node and the graph after.
static inline uint32_t check_input_batch_token(const CheckInputReader* reader, const CheckInputBatch* batch, uint32_t node)
{
    const uint32_t place = check_input_batch_place(batch, node);
    return place != BUSTER_CHECK_NONE ? batch->tokens[place] : reader->output->graph.tokens[node];
}

static inline CheckOp check_input_batch_op(const CheckInputReader* reader, const CheckInputBatch* batch, uint32_t node)
{
    const uint32_t place = check_input_batch_place(batch, node);
    return place != BUSTER_CHECK_NONE ? (CheckOp)batch->ops[place] : reader->output->graph.ops[node];
}

static inline uint32_t check_input_batch_operand0(const CheckInputReader* reader, const CheckInputBatch* batch, uint32_t node)
{
    const uint32_t place = check_input_batch_place(batch, node);
    return place != BUSTER_CHECK_NONE ? batch->operand0[place] : reader->output->graph.operand0[node];
}

// A node of `op` at `token` with operands a, b and c, numbered into the batch: check_input_add_node's node, whose
// columns the flush writes, but its value chain's end (CheckInputReader.value_next), which the batch's later actions
// extend. Where the graph has no room for it (check_graph_add's checks), none, and the unit is a resource failure, as
// check_input_add_node makes it. The caller made room in the batch (check_input_expression_room). A VALUE node takes the
// next leaf, which its maker writes whole: no leaf is cleared first.
// Always inlined (#880), as profile-guided builds do; hot callers: the class actions, once per node.
static inline __attribute__((always_inline)) uint32_t check_input_batch_add(CheckInputReader* reader, CheckInputBatch* batch, CheckOp op, uint32_t token, uint32_t a,
                                                                            uint32_t b, uint32_t c, CheckInputNodeForm form)
{
    const uint32_t place = batch->count;
    const uint32_t node = batch->base + place;
    uint32_t result = BUSTER_CHECK_NONE;
    if (node < reader->output->graph.capacity && op < CHECK_OP_COUNT && (a == BUSTER_CHECK_NONE || a < node) && (b == BUSTER_CHECK_NONE || b < node) &&
        (c == BUSTER_CHECK_NONE || c < node))
    {
        const bool leaf = op == CHECK_OP_VALUE;
        batch->ops[place] = op;
        batch->tokens[place] = token;
        batch->operand0[place] = a;
        batch->operand1[place] = b;
        batch->operand2[place] = c;
        batch->leaf_slots[place] = leaf ? batch->leaf_base + batch->leaves : BUSTER_CHECK_NONE;
        batch->members[place] = 0;
        batch->argument_offsets[place] = 0;
        batch->argument_counts[place] = 0;
        batch->forms[place] = (uint8_t)form;
        batch->contexts[place] = 0;
        batch->leaves += leaf;
        batch->count = place + 1;
        reader->value_next[node] = BUSTER_CHECK_NONE;
        if (reader->walk_ends != NULL)
        {
            reader->walk_ends[node] = BUSTER_CHECK_NONE;
        }
        result = node;
    }
    else
    {
        reader->output->resource = true;
    }
    return result;
}

// Writes the leaf of the batch's VALUE node `node`, which its maker writes whole, once (check_graph_set_leaf_at).
static inline __attribute__((always_inline)) void check_input_batch_set_leaf(CheckInputReader* reader, const CheckInputBatch* batch, uint32_t node, const CheckValue* value)
{
    check_graph_set_leaf_at(&reader->output->graph, batch->leaf_slots[node - batch->base], value);
}

static inline __m512i check_input_gather_bytes(__mmask16 lanes, __m512i indices, const uint8_t* column);

_Static_assert(sizeof(CheckGraphLeaf) == 32 && offsetof(CheckGraphLeaf, type) == 0 && offsetof(CheckGraphLeaf, overload_head) == 12,
               "a leaf's record is eight dwords, its type the first and its overload head the fourth");

// The builtin arithmetic types among 16 types: check_graph_builtin's test of each.
static inline __mmask16 check_input_batch_builtin(__mmask16 lanes, __m512i types)
{
    const __m512i code = _mm512_sub_epi32(_mm512_srli_epi32(types, BUSTER_TYPE_INDEX_SHIFT), _mm512_set1_epi32(BUSTER_TYPE_CODE_BOOL));
    return _mm512_mask_cmple_epu32_mask(lanes, code, _mm512_set1_epi32(BUSTER_TYPE_CODE_LONG_DOUBLE - BUSTER_TYPE_CODE_BOOL));
}

// The graph's nodes from `node` in `lanes`, the batch's, recorded as check_graph_record_nodes records them one at a time
// (#1671): each one's purity (check_graph_pure) into recorded_pure and the validation's counters, from the columns the flush
// has just written, every node before them recorded. As check_graph_validate_block computes purity: an operator's class
// from check_graph_classes' table (`classes`, in two vectors), its requested type builtin or void, a leaf's type and
// overload head gathered from its record, and its operands' purity, gathered from recorded_pure for those before the
// lanes, and taken in node order from the lanes' own for those among them. The graph holds its leaves as records. The
// scalar twin is check_graph_record_nodes, which the flush's scalar path and the walk's visits take.
// Always inlined (#1671); hot caller: check_input_expression_flush's vector path, once per 16 nodes.
static inline __attribute__((always_inline)) void check_input_batch_record(CheckGraph* graph, uint32_t node, __mmask16 lanes, __m512i ops, const __m512i operands[3],
                                                                           __m512i requested, __m512i counts, __m512i slots, __m512i classes_low,
                                                                           __m512i classes_high)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i none = _mm512_set1_epi32((int32_t)BUSTER_CHECK_NONE);
    const __m512i classes = _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutex2var_epi8(classes_low, _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(ops)), classes_high)));
    // check_graph_pure's three cases in its order: a leaf, then sizeof or _Alignof, then an operator a region may reorder.
    const __mmask16 leaves = _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_VALUE));
    const __mmask16 query = _mm512_mask_test_epi32_mask(lanes & (__mmask16)~leaves, classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_QUERY));
    const __mmask16 reorderable = _mm512_mask_test_epi32_mask(lanes & (__mmask16)~(leaves | query), classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_REORDERABLE));
    const __mmask16 typed = _mm512_test_epi32_mask(classes, _mm512_set1_epi32(BUSTER_CHECK_CLASS_TYPED));
    const __mmask16 type = check_input_batch_builtin(lanes, requested) | _mm512_cmpeq_epi32_mask(requested, _mm512_set1_epi32((int32_t)BUSTER_TYPE_VOID));
    const __m512i words = _mm512_slli_epi32(slots, 3);
    const __m512i leaf_types = _mm512_mask_i32gather_epi32(zero, leaves, words, graph->leaf_records, 4);
    const __m512i heads = _mm512_mask_i32gather_epi32(zero, leaves, _mm512_add_epi32(words, _mm512_set1_epi32(3)), graph->leaf_records, 4);
    const __mmask16 leaf_pure = check_input_batch_builtin(leaves, leaf_types) & _mm512_testn_epi32_mask(heads, heads);
    uint32_t pure = (uint32_t)(leaf_pure | query | (reorderable & ((__mmask16)~typed | type)));
    // The operands each case reads: sizeof's first, a reordered operator's three. An absent one is pure.
    const __mmask16 used[3] =
    {
        query | reorderable,
        reorderable,
        reorderable,
    };
    const __m512i first = _mm512_set1_epi32((int32_t)node);
    __mmask16 inside[3];
    for (uint32_t i = 0; i < 3; i += 1)
    {
        const __mmask16 present = _mm512_mask_cmpneq_epi32_mask(used[i], operands[i], none);
        const __mmask16 before = _mm512_mask_cmplt_epu32_mask(present, operands[i], first);
        const __m512i purity = check_input_gather_bytes(before, operands[i], graph->recorded_pure);
        pure &= (uint32_t)((__mmask16)~before | _mm512_test_epi32_mask(purity, purity));
        inside[i] = present & (__mmask16)~before;
    }
    // An operand among the lanes is an earlier lane, whose purity is final when its user's lane is taken in order.
    const uint32_t chained = (uint32_t)(inside[0] | inside[1] | inside[2]);
    if (chained != 0)
    {
        _Alignas(64) uint32_t edges[3][16];
        for (uint32_t i = 0; i < 3; i += 1)
        {
            _mm512_store_si512(edges[i], _mm512_sub_epi32(operands[i], first));
        }
        for (uint32_t remaining = chained; remaining != 0; remaining &= remaining - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(remaining);
            for (uint32_t i = 0; i < 3; i += 1)
            {
                const uint32_t bit = ((uint32_t)inside[i] >> lane) & 1u;
                pure &= ~(bit << lane) | (((pure >> (edges[i][lane] & 15u)) & 1u) << lane);
            }
        }
    }
    _mm_mask_storeu_epi8(graph->recorded_pure + node, lanes, _mm_maskz_mov_epi8((__mmask16)pure, _mm_set1_epi8(1)));
    // The counters: varargs, and every other node's arguments; the effects and builtins by operator.
    CheckGraphValidation* state = &graph->recorded;
    const __mmask16 varargs = _mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_VARARGS));
    const __m512i arguments = _mm512_maskz_mov_epi32(lanes & (__mmask16)~varargs, counts);
    const uint32_t widest = _mm512_reduce_max_epu32(arguments);
    state->varargs += (uint32_t)__builtin_popcount(varargs);
    state->maximum_arguments = widest > state->maximum_arguments ? widest : state->maximum_arguments;
    state->arguments += (uint32_t)_mm512_reduce_add_epi32(arguments);
    state->maximum_effects += (uint32_t)__builtin_popcount(_mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_OVERFLOW)));
    const CheckOp counted[6] =
    {
        CHECK_OP_BUILTIN_MISC,
        CHECK_OP_BUILTIN_FLOAT,
        CHECK_OP_BUILTIN_QUERY,
        CHECK_OP_BUILTIN_ATOMIC,
        CHECK_OP_BUILTIN_VECTOR,
        CHECK_OP_BUILTIN_BIT_CAST,
    };
    for (uint32_t i = 0; i < 6; i += 1)
    {
        state->builtins[i] += (uint32_t)__builtin_popcount(_mm512_mask_cmpeq_epi32_mask(lanes, ops, _mm512_set1_epi32(counted[i])));
    }
}

// The batch's nodes written to the graph's columns, then recorded with the nodes a visit made before them
// (check_graph_record_nodes, in node order), and the batch emptied after them. The vector path writes 16 nodes at a time:
// the columns the batch holds as they are, and the types, contexts, expression ends, warnings' suppression and
// floating-point environments from the nodes' tokens' columns, gathered (a byte column through check_input_gather_bytes),
// and records them as it writes them (check_input_batch_record, #1671), after the nodes a visit made before them; the
// scalar path, its twin, a node at a time, as check_input_add_node and the actions write them, and records them after.
static void check_input_expression_flush(CheckInputReader* reader, CheckInputBatch* batch, bool scalar)
{
    CheckInput* input = reader->output;
    CheckGraph* graph = &input->graph;
    const uint32_t count = batch->count;
    if (count != 0)
    {
        const Parser* parser = reader->parser;
        const uint32_t base = batch->base;
        // Gathers index by signed 32-bit offsets: the tokens', and the leaves' records' dwords.
        const bool indexed = parser->tokens.count <= (uint32_t)INT32_MAX - 4;
        const bool vector = !BUSTER_SCALAR_PATH(scalar) && indexed;
        // The vector path records its nodes as it writes them (check_input_batch_record), where the graph holds its leaves
        // as records, after the nodes a visit made before the batch.
        const bool recording = vector && graph->recorded_pure != NULL && graph->leaf_records != NULL && graph->dense_leaves == NULL &&
                               batch->leaf_base + batch->leaves <= (uint32_t)INT32_MAX / 8 - 4;
        if (recording)
        {
            check_graph_record_nodes(graph);
        }
        const __m512i classes_low = _mm512_load_si512(batch->classes);
        const __m512i classes_high = _mm512_load_si512(batch->classes + 64);
        const uint8_t* suppressed = reader->warning_suppressed;
        uint32_t k = 0;
        for (; vector && k < count; k += 16)
        {
            const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, count - k < 16 ? count - k : 16);
            const uint32_t node = base + k;
            const __m512i zero = _mm512_setzero_si512();
            const __m512i token = _mm512_maskz_loadu_epi32(lanes, batch->tokens + k);
            const __m512i ops = _mm512_maskz_loadu_epi32(lanes, batch->ops + k);
            const __m512i operands[3] =
            {
                _mm512_maskz_loadu_epi32(lanes, batch->operand0 + k),
                _mm512_maskz_loadu_epi32(lanes, batch->operand1 + k),
                _mm512_maskz_loadu_epi32(lanes, batch->operand2 + k),
            };
            const __m512i slots = _mm512_maskz_loadu_epi32(lanes, batch->leaf_slots + k);
            const __m512i counts = _mm512_maskz_loadu_epi32(lanes, batch->argument_counts + k);
            _mm512_mask_storeu_epi32(graph->ops + node, lanes, ops);
            _mm512_mask_storeu_epi32(graph->tokens + node, lanes, token);
            _mm512_mask_storeu_epi32(graph->operand0 + node, lanes, operands[0]);
            _mm512_mask_storeu_epi32(graph->operand1 + node, lanes, operands[1]);
            _mm512_mask_storeu_epi32(graph->operand2 + node, lanes, operands[2]);
            _mm512_mask_storeu_epi32(graph->leaf_slots + node, lanes, slots);
            _mm512_mask_storeu_epi32(graph->members + node, lanes, _mm512_maskz_loadu_epi32(lanes, batch->members + k));
            _mm512_mask_storeu_epi32(graph->argument_offsets + node, lanes, _mm512_maskz_loadu_epi32(lanes, batch->argument_offsets + k));
            _mm512_mask_storeu_epi32(graph->argument_counts + node, lanes, counts);
            const __m512i forms = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, batch->forms + k));
            const __mmask16 operators = _mm512_mask_cmpeq_epi32_mask(lanes, forms, _mm512_set1_epi32(CHECK_INPUT_FORM_OPERATOR));
            const __mmask16 discards = _mm512_mask_cmpeq_epi32_mask(lanes, forms, _mm512_set1_epi32(CHECK_INPUT_FORM_DISCARD));
            const __mmask16 initializers = _mm512_mask_cmpeq_epi32_mask(lanes, forms, _mm512_set1_epi32(CHECK_INPUT_FORM_INITIALIZER));
            const __m512i queried = _mm512_mask_i32gather_epi32(zero, operators | initializers, token, reader->query_types, 4);
            const __m512i types = _mm512_mask_mov_epi32(queried, discards, _mm512_set1_epi32((int32_t)BUSTER_TYPE_VOID));
            _mm512_mask_storeu_epi32(graph->types + node, lanes, types);
            if (recording)
            {
                check_input_batch_record(graph, node, lanes, ops, operands, types, counts, slots, classes_low, classes_high);
            }
            const __m512i statics = check_input_gather_bytes(operators | initializers, token, reader->static_initializers);
            const __mmask16 initialized = _mm512_test_epi32_mask(statics, statics);
            const __m512i added = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, batch->contexts + k));
            const __m512i contexts = _mm512_or_si512(_mm512_or_si512(_mm512_maskz_mov_epi32(initialized & operators, _mm512_set1_epi32(BUSTER_CHECK_CONTEXT_STATIC)),
                                                                     _mm512_maskz_mov_epi32(initialized & initializers, _mm512_set1_epi32(BUSTER_CHECK_CONTEXT_VALUE))),
                                                     added);
            _mm_mask_storeu_epi8(graph->contexts + node, lanes, _mm512_cvtepi32_epi8(contexts));
            // check_input_expression_end's: a condition's or a case label's closing ends at the token after it, every
            // other token where its subtree does.
            const __m512i roles = check_input_gather_bytes(lanes, token, parser->tree.roles);
            const __mmask16 closing = _mm512_cmpeq_epi32_mask(roles, _mm512_set1_epi32(PARSE_ROLE_CLOSE_CONDITION)) |
                                      _mm512_cmpeq_epi32_mask(roles, _mm512_set1_epi32(PARSE_ROLE_CLOSE_CASE));
            const __m512i ends = _mm512_mask_i32gather_epi32(zero, lanes & (__mmask16)~closing, token, reader->ends, 4);
            _mm512_mask_storeu_epi32(graph->source_positions + node, lanes, _mm512_add_epi32(_mm512_mask_mov_epi32(ends, closing, token), _mm512_set1_epi32(1)));
            if (graph->warning_suppressed != NULL)
            {
                const __m512i warnings = suppressed != NULL ? check_input_gather_bytes(lanes, token, suppressed) : zero;
                _mm_mask_storeu_epi8(graph->warning_suppressed + node, lanes, _mm512_cvtepi32_epi8(warnings));
            }
            if (__builtin_expect(graph->fp_environments != NULL, 0))
            {
                const __m512i constant = check_input_gather_bytes(lanes, token, reader->constant_contexts);
                const __mmask16 runtime = _mm512_mask_testn_epi32_mask(lanes, constant, constant);
                const __m512i environments = check_input_gather_bytes(lanes, token, input->fp_environments);
                const __m512i access = _mm512_maskz_mov_epi32(runtime, _mm512_set1_epi32(BUSTER_CHECK_FP_ENV_ACCESS));
                const __m512i mask = _mm512_or_si512(_mm512_set1_epi32(BUSTER_CHECK_FP_EVAL_MASK | BUSTER_CHECK_FP_CONTRACT_MASK), access);
                _mm_mask_storeu_epi8(graph->fp_environments + node, lanes, _mm512_cvtepi32_epi8(_mm512_and_si512(environments, mask)));
            }
        }
        for (; k < count; k += 1)
        {
            const uint32_t node = base + k;
            const uint32_t token = batch->tokens[k];
            const CheckInputNodeForm form = (CheckInputNodeForm)batch->forms[k];
            const bool operator = form == CHECK_INPUT_FORM_OPERATOR;
            const bool initializer = form == CHECK_INPUT_FORM_INITIALIZER;
            graph->ops[node] = (CheckOp)batch->ops[k];
            graph->tokens[node] = token;
            graph->operand0[node] = batch->operand0[k];
            graph->operand1[node] = batch->operand1[k];
            graph->operand2[node] = batch->operand2[k];
            graph->leaf_slots[node] = batch->leaf_slots[k];
            graph->members[node] = batch->members[k];
            graph->argument_offsets[node] = batch->argument_offsets[k];
            graph->argument_counts[node] = batch->argument_counts[k];
            graph->types[node] = operator || initializer ? reader->query_types[token] : form == CHECK_INPUT_FORM_DISCARD ? BUSTER_TYPE_VOID : 0;
            const bool initialized = (operator || initializer) && reader->static_initializers[token];
            graph->contexts[node] = (uint8_t)((initialized && operator ? BUSTER_CHECK_CONTEXT_STATIC : 0) | (initialized && initializer ? BUSTER_CHECK_CONTEXT_VALUE : 0) |
                                              batch->contexts[k]);
            graph->source_positions[node] = check_input_expression_end(reader, token);
            if (graph->warning_suppressed != NULL)
            {
                graph->warning_suppressed[node] = suppressed != NULL ? suppressed[token] : 0;
            }
            if (graph->fp_environments != NULL)
            {
                const uint8_t mask = BUSTER_CHECK_FP_EVAL_MASK | BUSTER_CHECK_FP_CONTRACT_MASK | (reader->constant_contexts[token] == 0 ? BUSTER_CHECK_FP_ENV_ACCESS : 0);
                graph->fp_environments[node] = input->fp_environments[token] & mask;
            }
        }
        graph->count = base + count;
        graph->leaf_count = batch->leaf_base + batch->leaves;
        graph->recorded_ready = false;
        graph->recorded_nodes = recording ? graph->count : graph->recorded_nodes;
        check_graph_record_nodes(graph);
    }
    batch->base = graph->count;
    batch->leaf_base = graph->leaf_count;
    batch->count = 0;
    batch->leaves = 0;
}

// Room in the batch for `need` more nodes: the batch flushed where it hasn't it. Returns whether it has it then.
static inline bool check_input_expression_room(CheckInputReader* reader, CheckInputBatch* batch, uint32_t need)
{
    if (batch->count + need > BUSTER_CHECK_EXPRESSION_BATCH)
    {
        check_input_expression_flush(reader, batch, false);
    }
    return need <= BUSTER_CHECK_EXPRESSION_BATCH;
}

// An operator's node, with what the walk's action gives every operator's: the type the token's query asked for, a static
// initializer's context (both the flush's, by the node's form), and in a unit with overload tables the token's overload
// head, which an address of an operand (ADDRESS) and __extension__ (SELECT) take from their operand's token and every
// other operator clears, so that a call finds its callee's set through them (the walk's action's last step).
// Always inlined (#880), as profile-guided builds do; hot caller: the expression reader, once per operator.
static inline __attribute__((always_inline)) uint32_t check_input_expression_operator(CheckInputReader* reader, CheckInputBatch* batch, CheckOp op, uint32_t token,
                                                                                      uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t node = check_input_batch_add(reader, batch, op, token, a, b, c, CHECK_INPUT_FORM_OPERATOR);
    CheckInput* input = reader->output;
    if (node != BUSTER_CHECK_NONE && input->overload_heads != NULL)
    {
        const uint32_t count = reader->parser->tokens.count;
        const uint32_t operand = (op == CHECK_OP_ADDRESS || op == CHECK_OP_SELECT) && a != BUSTER_CHECK_NONE ? check_input_batch_token(reader, batch, a) : count;
        input->overload_heads[token] = operand < count ? input->overload_heads[operand] : 0;
    }
    return node;
}

// The token's value is its node, where it made one.
static void check_input_expression_value(CheckInputReader* reader, uint32_t token, uint32_t node)
{
    if (node != BUSTER_CHECK_NONE)
    {
        reader->output->token_nodes[token] = node;
        reader->first_value[token] = node;
        reader->last_value[token] = node;
        reader->value_counts[token] = 1;
    }
}

// The token's values are its children's.
static void check_input_expression_list(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, CheckInputValues values)
{
    reader->output->token_nodes[token] = values.count == 1 ? values.first : BUSTER_CHECK_NONE;
    reader->first_value[token] = values.first;
    reader->last_value[token] = values.last;
    reader->value_counts[token] = values.count;
    if (BUSTER_CHECK_RARE(reader, static_initializers, token))
    {
        // A braced static initializer's elements are constant expressions, which initializer planning diagnoses:
        // the same constant evaluation its scalar form's INITIALIZE gets (check_input_expression_act's list).
        for (uint32_t value = values.first; value != BUSTER_CHECK_NONE; value = reader->value_next[value])
        {
            const uint32_t place = check_input_batch_place(batch, value);
            if (place != BUSTER_CHECK_NONE)
            {
                batch->contexts[place] |= BUSTER_CHECK_CONTEXT_VALUE;
            }
            else
            {
                reader->output->graph.contexts[value] |= BUSTER_CHECK_CONTEXT_VALUE;
            }
        }
    }
}

// A chunk's identifiers' leaves, made before its actions (#1038). An identifier's leaf is a function of columns at its
// token (its declared type, declaration and overload head) and at its declaration (kind, storage, flags, object,
// enumerator's value) and of its type's kind, which no action writes: the reader's run wrote them all before the walk.
// So the chunk's identifiers' leaves are made at once, by gathers whose loads don't wait for the walk's serial order, and
// each identifier's action writes its leaf from them, rather than load at its declaration as the walk reaches it.
typedef struct CheckInputIdentifiers CheckInputIdentifiers;
struct CheckInputIdentifiers
{
    // Per identifier of the chunk, in its order: its leaf's fields in pairs, each pair a word as the value lays them out (type
    // and category; flags and constant kind; object and overload head), the declaration, whose enumerator's value an
    // enumeration's leaf holds, and the identifier's status (BUSTER_CHECK_IDENTIFIER_*).
    uint64_t types[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint64_t flags[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint64_t objects[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t declarations[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t status[BUSTER_CHECK_EXPRESSION_CHUNK];
};

// An identifier's status: its class's action takes it (not a K&R unit's, not a constexpr object's use), it makes a node
// (it has a declared type), and its declaration is an enumerator.
#define BUSTER_CHECK_IDENTIFIER_ACTED 0x01u
#define BUSTER_CHECK_IDENTIFIER_NODE 0x02u
#define BUSTER_CHECK_IDENTIFIER_ENUMERATOR 0x04u

_Static_assert(sizeof(CheckValue) == 96 && offsetof(CheckValue, category) == 4 && offsetof(CheckValue, flags) == 8 &&
               offsetof(CheckValue, constant_kind) == 12 && offsetof(CheckValue, integer) == 16 && offsetof(CheckValue, floating_storage) == 32 &&
               offsetof(CheckValue, imaginary_storage) == 48 && offsetof(CheckValue, object) == 64 && offsetof(CheckValue, overload_head) == 68 &&
               offsetof(CheckValue, offset) == 72 && offsetof(CheckValue, object_bit_offset) == 92 && sizeof(CheckCategory) == 4 &&
               sizeof(CheckConstantKind) == 4 && BUSTER_CHECK_ICE < 256 && BUSTER_CHECK_STATIC_OBJECT < 256 && BUSTER_CHECK_REGISTER < 256,
               "an identifier's entry holds its leaf's fields as three of the value's words, which vary but for an enumerator's value");

// The identifier `token`'s entry `at` in `identifiers`, as the walk's action makes its leaf: the scalar twin of
// check_input_expression_identifiers' vector path.
static void check_input_identifier_entry(const CheckInputReader* reader, uint32_t token, CheckInputIdentifiers* identifiers, uint32_t at)
{
    const CheckInput* input = reader->output;
    const TypeId type = reader->declared_types[token];
    const uint32_t declaration = type != 0 ? input->declarations[token] : 0;
    const bool acted = input->kr_parameter_count == 0 && (type == 0 || (reader->declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_CONSTEXPR) == 0);
    const bool enumeration = type != 0 && reader->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
    if (type != 0)
    {
        check_input_note_enum(reader, token, enumeration, type);
    }
    const uint64_t category = enumeration ? CHECK_RVALUE : type != 0 && type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
    const uint64_t flags = (enumeration ? BUSTER_CHECK_ICE : 0) |
                           (type != 0 && check_input_bit(reader->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) && !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL) ? BUSTER_CHECK_STATIC_OBJECT : 0) |
                           (type != 0 ? check_input_register_flags(reader, declaration, token) : 0) |
                           (type != 0 ? check_input_nonnull_flag(reader, declaration, token, enumeration, reader->objects[declaration]) : 0);
    const uint64_t kind = enumeration ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS;
    const uint64_t head = input->overload_heads != NULL ? input->overload_heads[token] : 0;
    identifiers->types[at] = (uint64_t)type | category << 32;
    identifiers->flags[at] = flags | kind << 32;
    identifiers->objects[at] = (type != 0 ? reader->objects[declaration] : 0) | head << 32;
    identifiers->declarations[at] = declaration;
    identifiers->status[at] = (acted ? BUSTER_CHECK_IDENTIFIER_ACTED : 0) | (acted && type != 0 ? BUSTER_CHECK_IDENTIFIER_NODE : 0) |
                              (enumeration ? BUSTER_CHECK_IDENTIFIER_ENUMERATOR : 0);
}

// The bytes of `column` at the 16 `indices` in `lanes`, a dword each: the aligned dword that holds each byte, which never
// crosses a page the column's bytes aren't on (the column's start rounded down to four bytes), shifted down to the byte.
static inline __m512i check_input_gather_bytes(__mmask16 lanes, __m512i indices, const uint8_t* column)
{
    const uintptr_t start = (uintptr_t)column & ~(uintptr_t)3;
    const __m512i offsets = _mm512_add_epi32(indices, _mm512_set1_epi32((int32_t)((uintptr_t)column - start)));
    const __m512i aligned = _mm512_andnot_si512(_mm512_set1_epi32(3), offsets);
    const __m512i shifts = _mm512_slli_epi32(_mm512_and_si512(offsets, _mm512_set1_epi32(3)), 3);
    const __m512i dwords = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, aligned, (const void*)start, 1);
    return _mm512_and_si512(_mm512_srlv_epi32(dwords, shifts), _mm512_set1_epi32(0xff));
}

// The identifiers at `tokens` that `enums` selects, enumerators and enumerations' variables, as they mark the unit
// (check_input_note_enum, their twin): rare, so out of line, that the identifiers' vector loop keeps its code.
static __attribute__((noinline, cold)) void check_input_note_enums(const CheckInputReader* reader, const uint32_t* tokens, __mmask16 enums)
{
    for (__mmask16 named = enums; named != 0 && !reader->output->enum_names; named &= (__mmask16)(named - 1))
    {
        reader->output->enum_names = check_input_enum_named(reader, tokens[__builtin_ctz(named)]);
    }
    for (__mmask16 operands = enums; operands != 0 && !reader->output->enum_operands; operands &= (__mmask16)(operands - 1))
    {
        reader->output->enum_operands = check_input_enum_operand(reader, tokens[__builtin_ctz(operands)]);
    }
}

// The entries of the chunk's identifiers, the `acting` tokens and classes whose class is CHECK_INPUT_CODE_IDENTIFIER, in
// their order into `identifiers`; returns how many. The vector path compresses the identifiers 16 tokens at a time, then
// gathers 16 identifiers' columns at once: their types and declarations, then their declarations' columns and their
// types' kinds; the scalar path, its twin, makes each entry by check_input_identifier_entry.
static uint32_t check_input_expression_identifiers(const CheckInputReader* reader, const uint32_t* tokens, const uint32_t* classes, uint32_t acting,
                                                   CheckInputIdentifiers* identifiers, bool scalar)
{
    const CheckInput* input = reader->output;
    const uint32_t identifier = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_IDENTIFIER << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    // Gathers index by signed 32-bit offsets: the tokens' and the types' (the compaction checked the tokens').
    const bool indexed = reader->parser->tokens.count <= (uint32_t)INT32_MAX - 4 && reader->types->types.count <= (uint32_t)INT32_MAX - 4;
    uint32_t found[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t count = 0;
    uint32_t position = 0;
    __mmask16 registers = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && indexed && position < acting; position += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, acting - position < 16 ? acting - position : 16);
        const __mmask16 matched = _mm512_mask_cmpeq_epi32_mask(lanes, _mm512_maskz_loadu_epi32(lanes, classes + position), _mm512_set1_epi32((int32_t)identifier));
        _mm512_mask_compressstoreu_epi32(found + count, matched, _mm512_maskz_loadu_epi32(lanes, tokens + position));
        count += (uint32_t)__builtin_popcount(matched);
    }
    for (uint32_t at = 0; at < count; at += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, count - at < 16 ? count - at : 16);
        const __m512i zero = _mm512_setzero_si512();
        const __m512i token = _mm512_maskz_loadu_epi32(lanes, found + at);
        const __m512i type = _mm512_mask_i32gather_epi32(zero, lanes, token, reader->declared_types, 4);
        const __mmask16 typed = _mm512_mask_test_epi32_mask(lanes, type, type);
        const __m512i declaration = _mm512_mask_i32gather_epi32(zero, typed, token, input->declarations, 4);
        const __m512i kinds = check_input_gather_bytes(typed, declaration, reader->declaration_kinds);
        const __m512i bits = check_input_gather_bytes(typed, declaration, reader->declaration_bits);
        const __m512i declared = check_input_gather_bytes(typed, declaration, reader->declaration_flags);
        const __m512i type_kinds = check_input_gather_bytes(typed, _mm512_srli_epi32(type, BUSTER_TYPE_INDEX_SHIFT), reader->types->types.kind);
        const __m512i objects = _mm512_mask_i32gather_epi32(zero, typed, declaration, reader->objects, 4);
        const __m512i heads = input->overload_heads != NULL ? _mm512_mask_i32gather_epi32(zero, lanes, token, input->overload_heads, 4) : zero;
        const __mmask16 enumeration = _mm512_mask_cmpeq_epi32_mask(typed, kinds, _mm512_set1_epi32(C_BINDING_ENUMERATOR));
        const __mmask16 function = _mm512_mask_cmpeq_epi32_mask(typed, type_kinds, _mm512_set1_epi32(BUSTER_TYPE_KIND_FUNCTION));
        const __mmask16 constexprs = _mm512_mask_test_epi32_mask(typed, declared, _mm512_set1_epi32(BUSTER_CHECK_DECLARATION_CONSTEXPR));
        const __mmask16 acted = input->kr_parameter_count == 0 ? (__mmask16)(lanes & ~constexprs) : 0;
        __m512i category = _mm512_mask_mov_epi32(_mm512_set1_epi32(CHECK_LVALUE), function, _mm512_set1_epi32(CHECK_FUNCTION));
        category = _mm512_mask_mov_epi32(category, enumeration, _mm512_set1_epi32(CHECK_RVALUE));
        const __mmask16 static_object = _mm512_mask_test_epi32_mask(typed, bits, _mm512_set1_epi32(BUSTER_CHECK_BITS_STATIC)) &
                                        _mm512_mask_testn_epi32_mask(typed, bits, _mm512_set1_epi32(BUSTER_CHECK_BITS_THREAD_LOCAL));
        const __mmask16 in_register = _mm512_mask_test_epi32_mask(typed, bits, _mm512_set1_epi32(BUSTER_CHECK_BITS_REGISTER));
        registers |= in_register;
        // An enumerator or an enumeration's variable marks the unit (check_input_note_enum, its twin).
        __mmask16 enums = enumeration | _mm512_mask_cmpeq_epi32_mask(typed, type_kinds, _mm512_set1_epi32(BUSTER_TYPE_KIND_ENUM)) |
                          _mm512_mask_cmpeq_epi32_mask(typed, type_kinds, _mm512_set1_epi32(BUSTER_TYPE_KIND_ALIGNED));
        if (__builtin_expect(enums != 0 && !(reader->output->enum_operands && reader->output->enum_names), 0))
        {
            check_input_note_enums(reader, found + at, enums);
        }
        const __mmask16 has_object = _mm512_mask_test_epi32_mask(typed, objects, objects);
        const __mmask16 nonnull = has_object & ~enumeration;
        const __m512i flags = _mm512_or_si512(_mm512_or_si512(_mm512_maskz_mov_epi32(enumeration, _mm512_set1_epi32(BUSTER_CHECK_ICE)),
                                                              _mm512_maskz_mov_epi32(static_object, _mm512_set1_epi32(BUSTER_CHECK_STATIC_OBJECT))),
                                              _mm512_or_si512(_mm512_maskz_mov_epi32(in_register, _mm512_set1_epi32(BUSTER_CHECK_REGISTER)),
                                                              _mm512_maskz_mov_epi32(nonnull, _mm512_set1_epi32(BUSTER_CHECK_NONNULL_ADDRESS))));
        const __m512i constant = _mm512_mask_mov_epi32(_mm512_set1_epi32(CHECK_CONSTANT_ADDRESS), enumeration, _mm512_set1_epi32(CHECK_CONSTANT_INTEGER));
        const __m512i status = _mm512_or_si512(_mm512_or_si512(_mm512_maskz_mov_epi32(acted, _mm512_set1_epi32(BUSTER_CHECK_IDENTIFIER_ACTED)),
                                                               _mm512_maskz_mov_epi32(acted & typed, _mm512_set1_epi32(BUSTER_CHECK_IDENTIFIER_NODE))),
                                               _mm512_maskz_mov_epi32(enumeration, _mm512_set1_epi32(BUSTER_CHECK_IDENTIFIER_ENUMERATOR)));
        // The 64-bit words, the low lanes' then the high lanes': each a low dword and a high one.
        const __m512i pairs[3][2] =
        {
            { type, category },
            { flags, constant },
            { objects, heads },
        };
        uint64_t* words[3] =
        {
            identifiers->types + at,
            identifiers->flags + at,
            identifiers->objects + at,
        };
        for (uint32_t w = 0; w < 3; w += 1)
        {
            const __m512i low = _mm512_or_si512(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(pairs[w][0])),
                                                _mm512_slli_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(pairs[w][1])), 32));
            const __m512i high = _mm512_or_si512(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(pairs[w][0], 1)),
                                                 _mm512_slli_epi64(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(pairs[w][1], 1)), 32));
            _mm512_mask_storeu_epi64(words[w], (__mmask8)lanes, low);
            _mm512_mask_storeu_epi64(words[w] + 8, (__mmask8)(lanes >> 8), high);
        }
        _mm512_mask_storeu_epi32(identifiers->declarations + at, lanes, declaration);
        _mm512_mask_storeu_epi32(identifiers->status + at, lanes, status);
    }
    // Register objects are rare: their unparenthesized names (check_input_register_flags) are marked here.
    for (uint32_t at = 0; registers != 0 && at < count; at += 1)
    {
        identifiers->flags[at] |= (identifiers->flags[at] & BUSTER_CHECK_REGISTER) != 0 ? check_input_register_flags(reader, identifiers->declarations[at], found[at]) : 0;
    }
    // Weak addresses are rare. The gather marks every object non-null; this clears a weak attribute or `#pragma weak`.
    if (reader->weak_name_count != 0 || (input->symbol_pragmas != NULL && input->symbol_pragmas->count != 0))
    {
        for (uint32_t at = 0; at < count; at += 1)
        {
            if (check_input_address_is_weak(reader, identifiers->declarations[at], found[at]))
            {
                identifiers->flags[at] &= ~(uint64_t)BUSTER_CHECK_NONNULL_ADDRESS;
            }
        }
    }
    for (; position < acting; position += 1)
    {
        if (classes[position] == identifier)
        {
            check_input_identifier_entry(reader, tokens[position], identifiers, count);
            count += 1;
        }
    }
    return count;
}

// A chunk's number literals' values, found before its actions (#1038). A literal whose spelling is a plain decimal, one
// to nine digits without a leading zero but for 0 itself, is an int of that value: what check_input_literal makes of it
// (check_literal_integer's first case), its spelling its source's bytes, among which no splice, trigraph, digit
// separator or suffix can be; and so is an integer the preprocessor made (BUSTER_PP_KIND_INTEGER: __LINE__'s, say) up to
// INT_MAX, spelled in decimal. The chunk's literals are read at once, their spellings' bytes by gathers of the aligned
// words that hold them, and each literal's action takes its value from here rather than spell and parse it as the walk
// reaches it. It parses every other literal (check_input_literal).
typedef struct CheckInputLiterals CheckInputLiterals;
struct CheckInputLiterals
{
    // Per literal of the chunk, in its order: its value, or BUSTER_CHECK_NONE for a literal of another form.
    uint32_t values[BUSTER_CHECK_EXPRESSION_CHUNK];
};

// The most digits a plain decimal has here: nine, whose largest value is an int.
#define BUSTER_CHECK_LITERAL_PLAIN_DIGITS 9u

// The range of `pp`'s source that holds `location`: `cursor`'s if it does, else the one pp_find_range finds. A chunk's
// literals, in source order but for macros' bodies, are mostly in the range of the one before.
static uint32_t check_input_literal_range(const Preprocessor* pp, uint32_t location, uint32_t cursor)
{
    const bool held = cursor < pp->range_count && location - pp->ranges[cursor].base < pp->ranges[cursor].length;
    return held ? cursor : pp_find_range(pp, location);
}

// The literal `token`'s value where its spelling is a plain decimal or the preprocessor's int (CheckInputLiterals), else
// BUSTER_CHECK_NONE, with `*cursor` the range of its spelling's source after: the scalar twin of
// check_input_expression_literals' vector path.
static uint32_t check_input_literal_plain(const CheckInputReader* reader, uint32_t token, uint32_t* cursor)
{
    const Parser* parser = reader->parser;
    const Preprocessor* pp = reader->pp;
    const uint32_t source = parser->tokens.sources[token];
    const uint8_t kind = pp->output.kinds[source];
    const uint32_t length = pp->output.values[source] - pp->output.locations[source];
    const bool unnamed = parser->tokens.names[token] == 0;
    const bool number = unnamed && kind == C_TOKEN_NUMERIC_CONSTANT && length - 1 < BUSTER_CHECK_LITERAL_PLAIN_DIGITS;
    const uint32_t range = number ? check_input_literal_range(pp, pp->output.locations[source], *cursor) : BUSTER_PP_NONE;
    *cursor = range < pp->range_count ? range : *cursor;
    const uint8_t* bytes = range < pp->range_count ? pp->ranges[range].bytes + (pp->output.locations[source] - pp->ranges[range].base) : NULL;
    bool plain = bytes != NULL && (bytes[0] != '0' || length == 1);
    uint32_t value = 0;
    for (uint32_t i = 0; plain && i < length; i += 1)
    {
        const uint32_t digit = (uint32_t)bytes[i] - '0';
        plain = digit < 10;
        value = value * 10 + digit;
    }
    const bool integer = unnamed && kind == BUSTER_PP_KIND_INTEGER && pp->output.values[source] <= (uint32_t)INT32_MAX;
    return integer ? pp->output.values[source] : plain ? value : BUSTER_CHECK_NONE;
}

// The spellings of four tokens of 1 to 9 digits, one a 128-bit lane: each lane's two words hold its token's bytes from
// its byte `offsets`, for `lengths` bytes (both the lane's in each of its dwords). Returns each spelling's value in its
// lane's first dword, and in `*bad` the bytes of the lanes whose spellings are no plain decimal: a byte no digit, or a
// leading zero of more than one digit. One shuffle right-aligns each spelling; its digits are multiplied out in pairs,
// then quads, then the lane's four quads (of which the first is a 9-digit spelling's zero).
static inline __m512i check_input_literal_digits(__m512i words, __m512i offsets, __m512i lengths, uint64_t* bad)
{
    const __m512i iota = _mm512_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
                                         15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i splat = _mm512_set1_epi32(0x01010101);
    // In every byte of its lane: where the spelling's first digit goes (16 - length), the shift from its bytes' place to
    // there (offset + length - 16, from -15 to 0), and its length.
    const __m512i first = _mm512_mullo_epi32(_mm512_sub_epi32(_mm512_set1_epi32(16), lengths), splat);
    const __m512i shift = _mm512_mullo_epi32(_mm512_and_si512(_mm512_sub_epi32(_mm512_add_epi32(offsets, lengths), _mm512_set1_epi32(16)), _mm512_set1_epi32(0xff)),
                                             splat);
    const __m512i length = _mm512_mullo_epi32(lengths, splat);
    const __mmask64 before = _mm512_cmplt_epu8_mask(iota, first);
    const __m512i control = _mm512_mask_mov_epi8(_mm512_add_epi8(iota, shift), before, _mm512_set1_epi8((char)0x80));
    const __m512i aligned = _mm512_shuffle_epi8(words, control);
    const __mmask64 spelled = ~before;
    const __m512i digits = _mm512_mask_sub_epi8(_mm512_setzero_si512(), spelled, aligned, _mm512_set1_epi8('0'));
    const __mmask64 leading = _mm512_cmpeq_epi8_mask(iota, first) & _mm512_cmpgt_epu8_mask(length, _mm512_set1_epi8(1));
    *bad = _mm512_mask_cmpgt_epu8_mask(spelled, digits, _mm512_set1_epi8(9)) | _mm512_mask_cmpeq_epi8_mask(leading, aligned, _mm512_set1_epi8('0'));
    const __m512i pairs = _mm512_maddubs_epi16(digits, _mm512_set1_epi16(0x010a));
    const __m512i quads = _mm512_madd_epi16(pairs, _mm512_set1_epi32(0x00010064));
    const __m512i scaled = _mm512_mullo_epi32(quads, _mm512_set_epi32(1, 10000, 100000000, 0, 1, 10000, 100000000, 0, 1, 10000, 100000000, 0, 1, 10000, 100000000, 0));
    const __m512i halves = _mm512_add_epi32(scaled, _mm512_shuffle_epi32(scaled, _MM_SHUFFLE(1, 0, 3, 2)));
    return _mm512_add_epi32(halves, _mm512_shuffle_epi32(halves, _MM_SHUFFLE(2, 3, 0, 1)));
}

// The entries of the chunk's literals, the `acting` tokens and classes whose class is CHECK_INPUT_CODE_LITERAL, in their
// order into `literals`; returns how many. The vector path compresses the literals 16 tokens at a time, then reads them in
// stages, each over all of the chunk's literals 16 at a time, so that the loads of one stage, which mostly miss the
// cache (a chunk's literals are far apart in the preprocessor's columns and the source), wait together rather than in
// one chain a literal: their names and sources; the sources' kinds, locations and ends; the numbers' ranges, which are
// pp_find_range's (one of the two the last numbers were in, else found by a search of the ranges' bases that all lanes
// take in step), and the spellings' addresses where their ranges hold them; then the two aligned words that hold each spelling's bytes, four tokens a vector, and their digits
// (check_input_literal_digits). The preprocessor's ints take their values. The scalar path, the twin, takes
// check_input_literal_plain for every literal. An aligned word holds a byte of the spelling, so is on its page.
static uint32_t check_input_expression_literals(const CheckInputReader* reader, const uint32_t* tokens, const uint32_t* classes, uint32_t acting,
                                                CheckInputLiterals* literals, bool scalar)
{
    const Parser* parser = reader->parser;
    const Preprocessor* pp = reader->pp;
    const uint32_t literal = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_LITERAL << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    // Gathers index by signed 32-bit offsets: the tokens' (the compaction checked them), the preprocessor's tokens' and
    // the ranges' bytes.
    const bool indexed = parser->tokens.count <= (uint32_t)INT32_MAX - 4 && pp->output.count <= (uint32_t)INT32_MAX - 4 &&
                         (uint64_t)pp->range_count * sizeof(PPRange) <= (uint64_t)INT32_MAX;
    const uint8_t* table = (const uint8_t*)pp->ranges;
    // The stages' columns, a literal each: its token then its source, its spelling's location then its address, its
    // spelling's length, and its end (the preprocessor's int's value); and a group's numbers, the preprocessor's ints and
    // the numbers whose ranges hold them.
    uint32_t found[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t places[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t lengths[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t ends[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint64_t spellings[BUSTER_CHECK_EXPRESSION_CHUNK];
    __mmask16 numbers[BUSTER_CHECK_EXPRESSION_CHUNK / 16];
    __mmask16 integers[BUSTER_CHECK_EXPRESSION_CHUNK / 16];
    __mmask16 inside[BUSTER_CHECK_EXPRESSION_CHUNK / 16];
    uint32_t count = 0;
    uint32_t position = 0;
    uint32_t cursor = BUSTER_PP_NONE;
    for (; !BUSTER_SCALAR_PATH(scalar) && indexed && position < acting; position += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, acting - position < 16 ? acting - position : 16);
        const __mmask16 matched = _mm512_mask_cmpeq_epi32_mask(lanes, _mm512_maskz_loadu_epi32(lanes, classes + position), _mm512_set1_epi32((int32_t)literal));
        _mm512_mask_compressstoreu_epi32(found + count, matched, _mm512_maskz_loadu_epi32(lanes, tokens + position));
        count += (uint32_t)__builtin_popcount(matched);
    }
    const __m512i zero = _mm512_setzero_si512();
    // The names and the sources: an unnamed token's source in `found`, a named one's none of the numbers' or ints'.
    for (uint32_t at = 0; at < count; at += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, count - at < 16 ? count - at : 16);
        const __m512i token = _mm512_maskz_loadu_epi32(lanes, found + at);
        const __mmask16 unnamed = _mm512_mask_testn_epi32_mask(lanes, _mm512_mask_i32gather_epi32(zero, lanes, token, parser->tokens.names, 4),
                                                              _mm512_set1_epi32(-1));
        _mm512_mask_storeu_epi32(found + at, lanes, _mm512_mask_i32gather_epi32(zero, lanes, token, parser->tokens.sources, 4));
        numbers[at / 16] = unnamed;
    }
    // The sources' kinds, locations and ends: the numbers and the ints.
    for (uint32_t at = 0; at < count; at += 16)
    {
        const __mmask16 unnamed = numbers[at / 16];
        const __m512i source = _mm512_maskz_loadu_epi32(unnamed, found + at);
        const __m512i kinds = check_input_gather_bytes(unnamed, source, pp->output.kinds);
        const __m512i location = _mm512_mask_i32gather_epi32(zero, unnamed, source, pp->output.locations, 4);
        const __m512i end = _mm512_mask_i32gather_epi32(zero, unnamed, source, pp->output.values, 4);
        const __m512i length = _mm512_sub_epi32(end, location);
        numbers[at / 16] = _mm512_mask_cmpeq_epi32_mask(unnamed, kinds, _mm512_set1_epi32(C_TOKEN_NUMERIC_CONSTANT)) &
                           _mm512_cmplt_epu32_mask(_mm512_sub_epi32(length, _mm512_set1_epi32(1)), _mm512_set1_epi32((int32_t)BUSTER_CHECK_LITERAL_PLAIN_DIGITS));
        integers[at / 16] = _mm512_mask_cmpeq_epi32_mask(unnamed, kinds, _mm512_set1_epi32(BUSTER_PP_KIND_INTEGER)) &
                            _mm512_cmple_epu32_mask(end, _mm512_set1_epi32(INT32_MAX));
        _mm512_storeu_si512(places + at, location);
        _mm512_storeu_si512(lengths + at, length);
        _mm512_storeu_si512(ends + at, end);
    }
    // The numbers' ranges: one of the two the last numbers were in, where it holds the location (most of a chunk's numbers
    // are in one range or two), else the last whose base is at most the location, found by halving the ranges in step,
    // which is then the first of the two. Where the range holds the location, the spelling's address.
    const __m512i stride = _mm512_set1_epi32((int32_t)sizeof(PPRange));
    uint32_t cursors[2] = { BUSTER_PP_NONE, BUSTER_PP_NONE };
    for (uint32_t at = 0; at < count; at += 16)
    {
        const __mmask16 ranged = pp->range_count != 0 ? numbers[at / 16] : 0;
        const __m512i location = _mm512_loadu_si512(places + at);
        __m512i addresses[2] = { zero, zero };
        __mmask16 held = 0;
        for (uint32_t c = 0; c < 2; c += 1)
        {
            const PPRange* range = cursors[c] < pp->range_count ? &pp->ranges[cursors[c]] : NULL;
            const __m512i place = _mm512_sub_epi32(location, _mm512_set1_epi32(range != NULL ? (int32_t)range->base : 0));
            const __mmask16 in = range != NULL ? _mm512_mask_cmplt_epu32_mask(ranged & ~held, place, _mm512_set1_epi32((int32_t)range->length)) : 0;
            const __m512i bytes = _mm512_set1_epi64(range != NULL ? (int64_t)(uintptr_t)range->bytes : 0);
            addresses[0] = _mm512_mask_add_epi64(addresses[0], (__mmask8)in, bytes, _mm512_cvtepu32_epi64(_mm512_castsi512_si256(place)));
            addresses[1] = _mm512_mask_add_epi64(addresses[1], (__mmask8)(in >> 8), bytes, _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(place, 1)));
            held |= in;
        }
        const __mmask16 others = ranged & ~held;
        if (others != 0)
        {
            __m512i range = zero;
            for (uint32_t size = pp->range_count; size > 1; size -= size / 2)
            {
                const __m512i probe = _mm512_add_epi32(range, _mm512_set1_epi32((int32_t)(size / 2)));
                const __m512i bases = _mm512_mask_i32gather_epi32(zero, others, _mm512_mullo_epi32(probe, stride), table + offsetof(PPRange, base), 1);
                range = _mm512_mask_mov_epi32(range, _mm512_mask_cmple_epu32_mask(others, bases, location), probe);
            }
            const __m512i entry = _mm512_mullo_epi32(range, stride);
            const __m512i place = _mm512_sub_epi32(location, _mm512_mask_i32gather_epi32(zero, others, entry, table + offsetof(PPRange, base), 1));
            const __mmask16 found_in = _mm512_mask_cmplt_epu32_mask(others, place, _mm512_mask_i32gather_epi32(zero, others, entry, table + offsetof(PPRange, length), 1));
            addresses[0] = _mm512_mask_add_epi64(addresses[0], (__mmask8)found_in,
                                                 _mm512_mask_i32gather_epi64(zero, (__mmask8)found_in, _mm512_castsi512_si256(entry), table + offsetof(PPRange, bytes), 1),
                                                 _mm512_cvtepu32_epi64(_mm512_castsi512_si256(place)));
            addresses[1] = _mm512_mask_add_epi64(addresses[1], (__mmask8)(found_in >> 8),
                                                 _mm512_mask_i32gather_epi64(zero, (__mmask8)(found_in >> 8), _mm512_extracti64x4_epi64(entry, 1), table + offsetof(PPRange, bytes), 1),
                                                 _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(place, 1)));
            held |= found_in;
            uint32_t ranges[16];
            _mm512_storeu_si512(ranges, range);
            cursors[1] = found_in != 0 ? cursors[0] : cursors[1];
            cursors[0] = found_in != 0 ? ranges[__builtin_ctz(found_in)] : cursors[0];
        }
        inside[at / 16] = held;
        _mm512_storeu_si512(spellings + at, addresses[0]);
        _mm512_storeu_si512(spellings + at + 8, addresses[1]);
    }
    // The spellings' digits.
    for (uint32_t at = 0; at < count; at += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, count - at < 16 ? count - at : 16);
        const __mmask16 held = inside[at / 16];
        __m512i values = _mm512_mask_mov_epi32(_mm512_set1_epi32((int32_t)BUSTER_CHECK_NONE), integers[at / 16], _mm512_loadu_si512(ends + at));
        if (held != 0)
        {
            const __m512i addresses[2] = {
                _mm512_loadu_si512(spellings + at),
                _mm512_loadu_si512(spellings + at + 8),
            };
            const __m512i offsets = _mm512_and_si512(_mm512_inserti64x4(_mm512_castsi256_si512(_mm512_cvtepi64_epi32(addresses[0])), _mm512_cvtepi64_epi32(addresses[1]), 1),
                                                     _mm512_set1_epi32(7));
            const __m512i length = _mm512_mask_mov_epi32(_mm512_set1_epi32(1), held, _mm512_loadu_si512(lengths + at));
            // A token's second word holds its spelling's bytes past the first's eight.
            const __mmask16 seconds = _mm512_mask_cmpgt_epu32_mask(held, _mm512_add_epi32(offsets, length), _mm512_set1_epi32(8));
            __m512i groups[4];
            uint32_t bad = 0;
            for (uint32_t group = 0; group < 4; group += 1)
            {
                const int64_t b = (int64_t)(group & 1) * 4;
                const __m512i pick = _mm512_set_epi64(b + 3, b + 3, b + 2, b + 2, b + 1, b + 1, b, b);
                const __m512i words = _mm512_add_epi64(_mm512_andnot_si512(_mm512_set1_epi64(7), _mm512_permutexvar_epi64(pick, addresses[group >> 1])),
                                                       _mm512_set_epi64(8, 0, 8, 0, 8, 0, 8, 0));
                const __mmask8 loaded = (__mmask8)(_pdep_u32((held >> (group * 4)) & 0xf, 0x55) | _pdep_u32((seconds >> (group * 4)) & 0xf, 0xaa));
                const __m512i gathered = _mm512_mask_i64gather_epi64(zero, loaded, words, (const void*)0, 1);
                const int32_t g = (int32_t)group * 4;
                const __m512i lane = _mm512_set_epi32(g + 3, g + 3, g + 3, g + 3, g + 2, g + 2, g + 2, g + 2, g + 1, g + 1, g + 1, g + 1, g, g, g, g);
                uint64_t wrong = 0;
                groups[group] = check_input_literal_digits(gathered, _mm512_permutexvar_epi32(lane, offsets), _mm512_permutexvar_epi32(lane, length), &wrong);
                // A lane's bit where any byte of its 16 is wrong.
                const uint64_t nibbles = wrong | wrong >> 1 | wrong >> 2 | wrong >> 3;
                const uint64_t sixteens = nibbles | nibbles >> 4 | nibbles >> 8 | nibbles >> 12;
                bad |= (uint32_t)_pext_u64(sixteens, 0x0001000100010001ull) << (group * 4);
            }
            // Each lane's value from its group's first dwords.
            const __m512i firsts = _mm512_set_epi32(0, 0, 0, 0, 0, 0, 0, 0, 28, 24, 20, 16, 12, 8, 4, 0);
            const __m512i low = _mm512_permutex2var_epi32(groups[0], firsts, groups[1]);
            const __m512i high = _mm512_permutex2var_epi32(groups[2], firsts, groups[3]);
            const __m512i read = _mm512_permutex2var_epi32(low, _mm512_set_epi32(23, 22, 21, 20, 19, 18, 17, 16, 7, 6, 5, 4, 3, 2, 1, 0), high);
            values = _mm512_mask_mov_epi32(values, (__mmask16)(held & ~bad), read);
        }
        _mm512_mask_storeu_epi32(literals->values + at, lanes, values);
    }
    for (; position < acting; position += 1)
    {
        if (classes[position] == literal)
        {
            literals->values[count] = check_input_literal_plain(reader, tokens[position], &cursor);
            count += 1;
        }
    }
    return count;
}

// An identifier operand, the chunk's identifier `at`: a VALUE node of its declaration and its leaf, or nothing for one
// without a declared type. In a unit with overload tables the leaf holds the use's overload head, which the reader's run
// wrote at the token, and which the walk's action writes back there unchanged. A K&R unit's identifiers, whose types a
// K&R function's designator interns, and a constexpr object's, which reads its value, are the walk's.
// Always inlined (#1671): left to clang, the coded walk's action, once inlined into its loop, called it at each of sqlite3.c's
// 95,402 identifiers.
static inline __attribute__((always_inline)) bool check_input_expression_identifier(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token,
                                                                                  const CheckInputIdentifiers* identifiers, uint32_t at)
{
    const uint32_t status = identifiers->status[at];
    if ((status & BUSTER_CHECK_IDENTIFIER_NODE) != 0)
    {
        const uint32_t node = check_input_batch_add(reader, batch, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, CHECK_INPUT_FORM_LEAF);
        if (node != BUSTER_CHECK_NONE)
        {
            const unsigned __int128 integer = (status & BUSTER_CHECK_IDENTIFIER_ENUMERATOR) != 0 ? reader->enum_values[identifiers->declarations[at]] : 0;
            // Its three words and its enumerator's value, all else zero (no x87 store of the zero floating parts): its leaf's
            // record, and a row only for an enumerator's value past 64 bits.
            const CheckValue value =
            {
                .type = (TypeId)identifiers->types[at],
                .category = (CheckCategory)(identifiers->types[at] >> 32),
                .flags = (uint32_t)identifiers->flags[at],
                .constant_kind = (CheckConstantKind)(identifiers->flags[at] >> 32),
                .integer = integer,
                .object = (uint32_t)identifiers->objects[at],
                .overload_head = (uint32_t)(identifiers->objects[at] >> 32),
            };
            check_input_batch_set_leaf(reader, batch, node, &value);
            check_input_expression_value(reader, token, node);
        }
    }
    return (status & BUSTER_CHECK_IDENTIFIER_ACTED) != 0;
}

// A number or a character operand, the chunk's literal `at`: its literal's VALUE node, or the report of an invalid one; in
// a unit with overload tables, its token's head is its leaf's, none. A plain decimal's value the chunk's literals hold
// (CheckInputLiterals); every other literal is parsed here. A function's name is the walk's, as its tests take it after
// the literals.
// Always inlined (#1671), as the identifier's action is: called, at each of sqlite3.c's 44,184 literals.
static inline __attribute__((always_inline)) bool check_input_expression_literal(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token,
                                                                               const CheckInputLiterals* literals, uint32_t at)
{
    const bool acted = !check_input_function_name_token(reader->parser, token);
    if (acted)
    {
        CheckLiteral literal = { .type = BUSTER_TYPE_INT, .integer = literals->values[at], .valid = true };
        if (literals->values[at] == BUSTER_CHECK_NONE)
        {
            check_input_literal(reader, token, &literal);
        }
        if (!literal.valid)
        {
            check_input_report(reader, token, check_input_literal_report(&literal));
        }
        else
        {
            const uint32_t node = check_input_batch_add(reader, batch, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, CHECK_INPUT_FORM_LEAF);
            if (node != BUSTER_CHECK_NONE)
            {
                const CheckValue value = check_input_literal_value(reader, token, literal);
                check_input_batch_set_leaf(reader, batch, node, &value);
                if (reader->output->overload_heads != NULL)
                {
                    reader->output->overload_heads[token] = value.overload_head;
                }
                check_input_expression_value(reader, token, node);
            }
        }
    }
    return acted;
}

// A prefix operator of one operand, but the address of a label, sizeof and alignof, and __real__ and __imag__.
static bool check_input_expression_prefix(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, CheckInputValues values)
{
    CheckOp op = CHECK_OP_COUNT;
    switch (reader->parser->tokens.kinds[token])
    {
        case PARSE_KIND_PLUS: op = CHECK_OP_POSITIVE; break;
        case PARSE_KIND_EXTENSION: op = CHECK_OP_SELECT; break;
        case PARSE_KIND_MINUS: op = CHECK_OP_NEGATIVE; break;
        case PARSE_KIND_EXCLAIM: op = CHECK_OP_NOT; break;
        case PARSE_KIND_TILDE: op = CHECK_OP_BIT_NOT; break;
        case PARSE_KIND_AMP: op = CHECK_OP_ADDRESS; break;
        case PARSE_KIND_STAR: op = CHECK_OP_DEREFERENCE; break;
        case PARSE_KIND_PLUSPLUS: op = CHECK_OP_PRE_INCREMENT; break;
        case PARSE_KIND_MINUSMINUS: op = CHECK_OP_PRE_DECREMENT; break;
        default: break;
    }
    const bool acted = op != CHECK_OP_COUNT && values.count == 1;
    if (acted)
    {
        check_input_expression_value(reader, token, check_input_expression_operator(reader, batch, op, token, values.first, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE));
    }
    return acted;
}

// A binary operator, or a comma operator: two values. An initializer's assignment is a class of its own (INITIALIZE).
// Always inlined (#880), as profile-guided builds do; hot caller: check_input_expression_coded, once per binary
// operator.
static inline __attribute__((always_inline)) bool check_input_expression_binary(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, CheckInputValues values)
{
    const CheckOp op = check_input_binary(reader->parser->tokens.kinds[token]);
    const bool acted = op != CHECK_OP_COUNT && values.count == 2;
    if (acted)
    {
        const uint32_t second = reader->value_next[values.first];
        check_input_expression_value(reader, token, check_input_expression_operator(reader, batch, op, token, values.first, second, BUSTER_CHECK_NONE));
    }
    return acted;
}

// An initializer's assignment (CheckInputReader.initializers): the INITIALIZE of its one value, whose node is the declaration's
// initializer (CheckInput.initializer_nodes) and takes its token's query type and, in a static initializer, the value
// context, as the walk's action gives it; or a braced initializer's values, passed on as a list. An INITIALIZE of more
// values or none is the walk's, which refuses it.
static bool check_input_expression_initialize(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, CheckInputValues values)
{
    const bool listed = reader->initializers[token] == 2;
    const bool acted = listed || values.count == 1;
    if (listed)
    {
        check_input_expression_list(reader, batch, token, values);
    }
    else if (acted)
    {
        CheckInput* input = reader->output;
        const uint32_t node = check_input_batch_add(reader, batch, CHECK_OP_INITIALIZE, token, values.first, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE,
                                                    CHECK_INPUT_FORM_INITIALIZER);
        if (node != BUSTER_CHECK_NONE)
        {
            check_sparse_set(input->sparse_pool, input->initializer_nodes, reader->initializer_declarations[token], node);
            if (input->overload_heads != NULL)
            {
                input->overload_heads[token] = 0;
            }
        }
        check_input_expression_value(reader, token, node);
    }
    return acted;
}

// A conditional operator of three values, or of two with the middle one omitted (x ?: y), whose first is both.
static bool check_input_expression_ternary(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, CheckInputValues values)
{
    const bool acted = values.count == 3 || values.count == 2;
    if (acted)
    {
        const bool omitted = values.count == 2;
        const uint32_t second = reader->value_next[values.first];
        const uint32_t third = omitted ? BUSTER_CHECK_NONE : reader->value_next[second];
        check_input_expression_value(reader, token, check_input_expression_operator(reader, batch, CHECK_OP_CONDITIONAL, token, values.first, omitted ? values.first : second,
                                                                                    omitted ? second : third));
    }
    return acted;
}

// A member access: one value and the member's name after the operator, a vector's member for `.`.
static bool check_input_expression_member(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, CheckInputValues values)
{
    const Parser* parser = reader->parser;
    const bool acted = values.count == 1 && token + 1 < parser->tokens.count && parser->tree.roles[token + 1] == PARSE_ROLE_MEMBER;
    if (acted)
    {
        const bool arrow = parser->tokens.kinds[token] == PARSE_KIND_ARROW;
        const uint32_t node = check_input_expression_operator(reader, batch, arrow ? CHECK_OP_ARROW : CHECK_OP_MEMBER, token, values.first, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            batch->members[node - batch->base] = parser->tokens.names[token + 1];
            // `p->y` names an extended vector's lane only through a pointer to one, which only a unit with one has.
            if (!arrow || __builtin_expect(reader->types->ext_vectors, 0))
            {
                check_input_vector_member(reader, node, token + 1);
            }
        }
        check_input_expression_value(reader, token, node);
    }
    return acted;
}

// An operator of `values.count` values (one, or two for a subscript), where the class asks for `wanted`.
static bool check_input_expression_fixed(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, CheckInputValues values, CheckOp op, uint32_t wanted)
{
    const bool acted = values.count == wanted;
    if (acted)
    {
        const uint32_t second = wanted == 2 ? reader->value_next[values.first] : BUSTER_CHECK_NONE;
        check_input_expression_value(reader, token, check_input_expression_operator(reader, batch, op, token, values.first, second, BUSTER_CHECK_NONE));
    }
    return acted;
}

// A string literal operand: a VALUE node of its string's object (check_input_string, which makes the object the first time
// a string is met), or nothing where it has none; in a unit with overload tables, its token's head is its leaf's, none.
static bool check_input_expression_string(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token)
{
    const CheckInitString* string = check_input_string(reader, token);
    if (string != 0)
    {
        const uint32_t node = check_input_batch_add(reader, batch, CHECK_OP_VALUE, token, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, CHECK_INPUT_FORM_LEAF);
        if (node != BUSTER_CHECK_NONE)
        {
            const CheckValue value =
            {
                .type = string->type,
                .category = CHECK_LVALUE,
                .flags = BUSTER_CHECK_STATIC_OBJECT | (string->object != 0 ? BUSTER_CHECK_NONNULL_ADDRESS : 0),
                .constant_kind = CHECK_CONSTANT_ADDRESS,
                .object = string->object,
            };
            check_input_batch_set_leaf(reader, batch, node, &value);
            if (reader->output->overload_heads != NULL)
            {
                reader->output->overload_heads[token] = value.overload_head;
            }
            check_input_expression_value(reader, token, node);
        }
    }
    return true;
}

// A return statement: its one value converted to the function's return type (a CONVERT node, the token's node but no value
// of an operand's), or without one a report where the function's type isn't void, but in a system header, as clang
// warns (-Wreturn-mismatch). A return outside a function's body, or of more values, is the walk's.
static bool check_input_expression_return(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, CheckInputValues values)
{
    const TypeId type = reader->return_types[token];
    const bool acted = type != 0 && values.count <= 1;
    if (acted && values.count == 1)
    {
        reader->query_types[token] = type;
        const uint32_t node = check_input_expression_operator(reader, batch, CHECK_OP_CONVERT, token, values.first, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            reader->output->token_nodes[token] = node;
        }
    }
    else if (acted && check_action_kind(reader->types, type) != BUSTER_TYPE_KIND_VOID && !check_graph_relaxed_token(&reader->output->graph, token))
    {
        check_input_report(reader, token, CHECK_INPUT_RETURN);
    }
    return acted;
}

// A statement's terminator, or a for's closing parenthesis: each of its values discarded, a CAST to void, or a CONDITION
// in a condition's context, which may hold one value at most. In a unit with statement expressions, the last statement of
// a statement expression's region (CheckInputRegion.final_statement, found through its parent, or a label's) gives the
// region its one value as its result, and the discards are chained from the token for the region's walk
// (CheckInputReader.region_roots): what the walk's action does, in the same order. A region's last statement without
// exactly one value is the walk's, which refuses it.
// Always inlined (#1671), as the identifier's action is: called, at each of sqlite3.c's 54,508 terminators.
static inline __attribute__((always_inline)) bool check_input_expression_terminator(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token,
                                                                                  CheckInputValues values)
{
    CheckInput* input = reader->output;
    const bool condition = reader->conditions[token] != 0;
    CheckInputRegion* region = NULL;
    if (input->region_count != 0)
    {
        const Parser* parser = reader->parser;
        const uint32_t parent = parser->tree.parents[token];
        uint32_t selected = parent < parser->tokens.count ? input->expression_regions[parent] : 0;
        if (selected == 0 && parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_LABEL)
        {
            selected = check_input_labeled_region(reader, parent);
        }
        region = selected != 0 && input->regions[selected - 1].final_statement == token ? &input->regions[selected - 1] : NULL;
    }
    const bool acted = (!condition || values.count <= 1) && (region == NULL || values.count == 1);
    if (acted && region != NULL)
    {
        region->result = values.first;
    }
    uint32_t last = BUSTER_CHECK_NONE;
    for (uint32_t value = acted ? values.first : BUSTER_CHECK_NONE; value != BUSTER_CHECK_NONE; value = reader->value_next[value])
    {
        const uint32_t discard = check_input_batch_add(reader, batch, condition ? CHECK_OP_CONDITION : CHECK_OP_CAST, token, value, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE,
                                                       CHECK_INPUT_FORM_DISCARD);
        if (discard != BUSTER_CHECK_NONE)
        {
            if (reader->region_roots != NULL)
            {
                if (last == BUSTER_CHECK_NONE)
                {
                    reader->region_roots[token] = discard;
                }
                else
                {
                    reader->value_next[last] = discard;
                }
                last = discard;
            }
        }
    }
    return acted;
}

// An if's, a while's or a switch's condition: its one value tested (CONDITION), or a switch's promoted (POSITIVE). The
// condition's node is the token's, but no value of an operand's.
static bool check_input_expression_condition(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, CheckInputValues values)
{
    const Parser* parser = reader->parser;
    const bool acted = values.count == 1;
    if (acted)
    {
        const uint32_t keyword = reader->first_child[token];
        const bool switched = keyword < parser->tokens.count && parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH;
        const uint32_t second = switched ? reader->value_next[values.first] : BUSTER_CHECK_NONE;
        const uint32_t node = check_input_expression_operator(reader, batch, switched ? CHECK_OP_POSITIVE : CHECK_OP_CONDITION, token, values.first, second, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            reader->output->token_nodes[token] = node;
        }
    }
    return acted;
}

// A call of a function: the callee's value its operand, and the arguments' values its own slice of argument_nodes, each
// with its first token; a call of a plain library builtin's declaration that clang folds marked in its members
// (check_input_library_call, #148); and in a unit with overload tables, a call whose callee's token names an overload set
// recorded with the set's head (CheckGraph.overload_calls), in node order as the walk records them. A K&R unit's calls are
// the walk's, and so are a builtin's and an intrinsic's, by their class (check_input_expression_fold).
static bool check_input_expression_call(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, CheckInputValues values)
{
    CheckInput* input = reader->output;
    const bool acted = values.count != 0 && input->kr_parameter_count == 0;
    if (acted)
    {
        CheckGraph* graph = &input->graph;
        const uint32_t callee = values.first;
        const uint32_t first_argument = reader->value_next[callee];
        const uint32_t node = check_input_expression_operator(reader, batch, CHECK_OP_CALL, token, callee, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        if (node != BUSTER_CHECK_NONE)
        {
            const uint32_t place = node - batch->base;
            const CheckOp callee_op = check_input_batch_op(reader, batch, callee);
            if (callee_op == CHECK_OP_VALUE || callee_op == CHECK_OP_ADDRESS)
            {
                const uint32_t designator = callee_op == CHECK_OP_ADDRESS ? check_input_batch_operand0(reader, batch, callee) : callee;
                const uint32_t use = designator < node && check_input_batch_op(reader, batch, designator) == CHECK_OP_VALUE ? check_input_batch_token(reader, batch, designator) :
                                                                                                                             BUSTER_CHECK_NONE;
                uint32_t library = use != BUSTER_CHECK_NONE ? check_input_library_call(reader, use) : 0;
                // In a function whose no_builtin names it, the call is the declaration's (#1958).
                library = library != 0 && check_input_no_builtin(reader, reader->parser->tokens.names[use], use) ? 0 : library;
                batch->members[place] = library != 0 ? library : batch->members[place];
            }
            const uint32_t callee_token = input->overload_heads != NULL ? check_input_batch_token(reader, batch, callee) : reader->parser->tokens.count;
            const uint32_t head = callee_token < reader->parser->tokens.count ? input->overload_heads[callee_token] : 0;
            if (head != 0)
            {
                if (graph->overload_count < graph->overload_capacity)
                {
                    graph->overload_calls[graph->overload_count++] = (CheckOverloadCall){ .node = node, .head = head - 1 };
                }
                else
                {
                    input->resource = true;
                }
            }
            batch->argument_offsets[place] = graph->argument_count;
            batch->argument_counts[place] = values.count - 1;
            for (uint32_t argument = first_argument; argument != BUSTER_CHECK_NONE; argument = reader->value_next[argument])
            {
                if (graph->argument_count < graph->argument_capacity)
                {
                    graph->argument_tokens[graph->argument_count] = check_input_first_token(reader, check_input_batch_token(reader, batch, argument));
                    graph->argument_nodes[graph->argument_count++] = argument;
                }
                else
                {
                    input->resource = true;
                }
            }
        }
        check_input_expression_value(reader, token, node);
    }
    return acted;
}

// The coded walk's action at `token` of visits byte `classes`, by its class where `coded` (the unit has none of the
// features every class leaves to the walk), else and where the class's action doesn't take it, the walk's visit. A
// class's action that leaves the token has written only its first value, NONE, and its children's chains (value_next),
// both of which the walk's visit writes again as they were. The class's action numbers its nodes into `batch`, which has
// room for them first: a terminator's discards, one a value, and every other class's one node at most (a terminator of
// more values than the batch holds is the walk's). In a coded unit, the visit's ticks are added to `*visited`
// (BUSTER_CHECK_EXPRESSION_PART_VISITS) unless it is null; in another, the caller times its whole chunk as visits.
// Always inlined into the chunk's loop (#1671): called, each of sqlite3.c's 451,790 actions paid a call of four arguments
// on the stack after a vzeroupper, its prologue and epilogue and its spills, about 2% of the compile's cycles on the 9700X.
static inline __attribute__((always_inline)) void check_input_expression_coded(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, uint8_t classes,
                                                                               bool coded, const CheckInputIdentifiers* identifiers, uint32_t* identifier,
                                                                               const CheckInputLiterals* literals, uint32_t* literal, uint64_t* visited)
{
    const uint32_t code = (classes & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    bool acted = coded && code != CHECK_INPUT_CODE_SCALAR;
    if (acted)
    {
        reader->first_value[token] = BUSTER_CHECK_NONE;
        const CheckInputValues values = check_input_expression_values(reader, token);
        acted = check_input_expression_room(reader, batch, code == CHECK_INPUT_CODE_TERMINATOR ? values.count : 1);
        switch (acted ? code : CHECK_INPUT_CODE_SCALAR)
        {
            case CHECK_INPUT_CODE_LIST:
                check_input_expression_list(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_DROP:
                break;
            case CHECK_INPUT_CODE_IDENTIFIER:
                acted = check_input_expression_identifier(reader, batch, token, identifiers, *identifier);
                *identifier += 1;
                break;
            case CHECK_INPUT_CODE_LITERAL:
                acted = check_input_expression_literal(reader, batch, token, literals, *literal);
                *literal += 1;
                break;
            case CHECK_INPUT_CODE_PREFIX:
                acted = check_input_expression_prefix(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_BINARY:
                acted = check_input_expression_binary(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_SEPARATOR:
                check_input_expression_list(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_GROUP:
                acted = values.count == 1;
                if (acted)
                {
                    check_input_expression_list(reader, batch, token, values);
                }
                break;
            case CHECK_INPUT_CODE_CAST:
                acted = reader->query_types[token] != 0 && check_input_expression_fixed(reader, batch, token, values, CHECK_OP_CAST, 1);
                break;
            case CHECK_INPUT_CODE_TERNARY:
                acted = check_input_expression_ternary(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_SUBSCRIPT:
                acted = check_input_expression_fixed(reader, batch, token, values, CHECK_OP_SUBSCRIPT, 2);
                break;
            case CHECK_INPUT_CODE_MEMBER:
                acted = check_input_expression_member(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_TERMINATOR:
                acted = check_input_expression_terminator(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_CONDITION:
                acted = check_input_expression_condition(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_CALL:
                acted = check_input_expression_call(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_STRING:
                acted = check_input_expression_string(reader, batch, token);
                break;
            case CHECK_INPUT_CODE_RETURN:
                acted = check_input_expression_return(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_INITIALIZE:
                acted = check_input_expression_initialize(reader, batch, token, values);
                break;
            case CHECK_INPUT_CODE_POSTFIX:
            {
                const uint8_t kind = reader->parser->tokens.kinds[token];
                acted = (kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS) &&
                        check_input_expression_fixed(reader, batch, token, values, kind == PARSE_KIND_PLUSPLUS ? CHECK_OP_POST_INCREMENT : CHECK_OP_POST_DECREMENT, 1);
                break;
            }
            default:
                acted = false;
                break;
        }
    }
    // Left tokens are counted in units the coded walk takes alone: in another, the walk's visit takes every token. The
    // tokens acted at by class are each class's tokens less those it left (check_input_expressions_coded's count).
    // Not hinted, though 98% of sqlite3.c's tokens act: with the visit's counters and clock in this branch, a hint
    // that the visit is rare took 19 thousand more taken branches (#1175).
    if (!acted)
    {
        reader->output->expression_left[code] += coded;
        if (reader->expression_declined != NULL)
        {
            reader->expression_declined[token] = 1;
        }
        // The visit reads and writes the graph: the batch's nodes go to it first (the actions' time, not the visit's),
        // and the batch's next node is the next after the visit's.
        check_input_expression_flush(reader, batch, false);
        const bool clocked = coded && visited != NULL;
        const uint64_t start = clocked ? __rdtsc() : 0;
        check_input_expression_visit(reader, token, classes);
        batch->base = reader->output->graph.count;
        batch->leaf_base = reader->output->graph.leaf_count;
        if (clocked)
        {
            *visited += __rdtsc() - start;
        }
    }
}

// The action of check_input_expression_coded, called: the loop that times each action by its class
// (PPOptions.expression_timing) takes it, so that the action's body is inlined once, into the untimed loop.
static __attribute__((noinline)) void check_input_expression_coded_timed(CheckInputReader* reader, CheckInputBatch* batch, uint32_t token, uint8_t classes, bool coded,
                                                                         const CheckInputIdentifiers* identifiers, uint32_t* identifier,
                                                                         const CheckInputLiterals* literals, uint32_t* literal)
{
    check_input_expression_coded(reader, batch, token, classes, coded, identifiers, identifier, literals, literal, NULL);
}

// Whether `token` passes its children's values on to its parent in the graph by passes' numbering: a LIST, a GROUP, a
// SEPARATOR or a braced initializer's INITIALIZE the walk's visit didn't take.
static bool check_input_numbering_passes(const CheckInputReader* reader, uint32_t token)
{
    const uint32_t code = (reader->visits[token] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    const bool listed = code == CHECK_INPUT_CODE_INITIALIZE && reader->initializers[token] == 2;
    return (code == CHECK_INPUT_CODE_LIST || code == CHECK_INPUT_CODE_GROUP || code == CHECK_INPUT_CODE_SEPARATOR || listed) && reader->expression_declined[token] == 0;
}

// Appends node `value` to the list of `consumer`'s values in the graph by passes' numbering.
static void check_input_numbering_append(uint32_t* first, uint32_t* last, uint32_t* counts, uint32_t* next, uint32_t consumer, uint32_t value)
{
    if (counts[consumer] != 0)
    {
        next[last[consumer]] = value;
    }
    else
    {
        first[consumer] = value;
    }
    last[consumer] = value;
    next[value] = BUSTER_CHECK_NONE;
    counts[consumer] += 1;
}

// The graph by passes' numbering, measured (#1038's stages 2 and 4, under PPOptions.expression_numbering): after the coded
// walk, its acting tokens numbered again in postorder from `base` (the graph's count before the walk), as such a pass
// would number them before writing any node. Each value goes to its consumer's list: the nearest ancestor that doesn't pass
// values on (check_input_numbering_passes). A token the walk's visit took (the scalar class, and those their classes'
// actions left, CheckInputReader.expression_declined) is a barrier: it takes the walk's count of nodes at it and the walk's
// chain of its values. A terminator makes a discard a value of its list; an identifier, a literal or a string a node where
// the walk made one; a return one where it has a value; and every other class one, whose first operand is its list's
// first. A condition's and a return's nodes are no values. The numbering is timed as written plainly, from its columns'
// allocation and clearing and the walk's counts at each token to its last token, the chunks compacted again
// (CheckInput.expression_numbering_ticks). Then each node a class numbered is checked against the walk's: its token, and
// but for a VALUE its first operand; the barriers' nodes it only counts. The caller pops the scratch.
static void check_input_expression_numbering(CheckInputReader* reader, uint32_t base, uint32_t* tokens, uint32_t* classes)
{
    CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const uint32_t count = reader->parser->tokens.count;
    const uint32_t* parents = reader->parser->tree.parents;
    const uint32_t nodes = graph->count;
    const uint32_t dropped = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_DROP << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    Arena* scratch = reader->scratch;
    // The numbering's whole cost, from its columns' allocation and clearing to its last token; the check is outside it.
    const uint64_t start = __rdtsc();
    // Per token: its list's first and last values and their count, and the nodes the walk made at it. Per node: the next
    // value of its list, and the token and first operand the numbering gives it.
    uint32_t* first = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* last = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* counts = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* made = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* next = arena_push_array(scratch, (uint64_t)nodes + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* numbered = arena_push_array(scratch, (uint64_t)nodes + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* operands = arena_push_array(scratch, (uint64_t)nodes + 1, sizeof(uint32_t), _Alignof(uint32_t));
    if (first != NULL && last != NULL && counts != NULL && made != NULL && next != NULL && numbered != NULL && operands != NULL)
    {
        memset(counts, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(made, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(numbered, 0xff, ((uint64_t)nodes + 1) * sizeof(uint32_t));
        for (uint32_t n = base; n < nodes; n += 1)
        {
            made[graph->tokens[n] < count ? graph->tokens[n] : count] += 1;
        }
        uint32_t id = base;
        for (uint32_t chunk = 0; chunk < count; chunk += BUSTER_CHECK_EXPRESSION_CHUNK)
        {
            const uint32_t end = count - chunk < BUSTER_CHECK_EXPRESSION_CHUNK ? count : chunk + BUSTER_CHECK_EXPRESSION_CHUNK;
            const uint32_t acting = check_input_expression_compact(reader, chunk, end, tokens, classes, dropped, false);
            for (uint32_t i = 0; i < acting; i += 1)
            {
                const uint32_t token = tokens[i];
                const uint32_t code = (classes[i] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
                const bool barrier = code == CHECK_INPUT_CODE_SCALAR || reader->expression_declined[token] != 0;
                if (barrier || !check_input_numbering_passes(reader, token))
                {
                    uint32_t consumer = parents[token];
                    while (consumer < count && check_input_numbering_passes(reader, consumer))
                    {
                        consumer = parents[consumer];
                    }
                    uint32_t value = BUSTER_CHECK_NONE;
                    uint32_t chain = BUSTER_CHECK_NONE;
                    uint32_t chained = 0;
                    if (barrier)
                    {
                        id += made[token];
                        chain = reader->first_value[token];
                        chained = reader->value_counts[token];
                    }
                    else if (code == CHECK_INPUT_CODE_TERMINATOR)
                    {
                        uint32_t operand = first[token];
                        for (uint32_t k = 0; k < counts[token]; k += 1)
                        {
                            numbered[id < nodes ? id : nodes] = token;
                            operands[id < nodes ? id : nodes] = operand;
                            id += 1;
                            operand = next[operand];
                        }
                    }
                    else if (code == CHECK_INPUT_CODE_IDENTIFIER || code == CHECK_INPUT_CODE_LITERAL || code == CHECK_INPUT_CODE_STRING)
                    {
                        if (input->token_nodes[token] != BUSTER_CHECK_NONE)
                        {
                            numbered[id < nodes ? id : nodes] = token;
                            operands[id < nodes ? id : nodes] = BUSTER_CHECK_NONE;
                            value = id;
                            id += 1;
                        }
                    }
                    else if (code != CHECK_INPUT_CODE_RETURN || counts[token] != 0)
                    {
                        numbered[id < nodes ? id : nodes] = token;
                        operands[id < nodes ? id : nodes] = counts[token] != 0 ? first[token] : BUSTER_CHECK_NONE;
                        value = code == CHECK_INPUT_CODE_CONDITION || code == CHECK_INPUT_CODE_RETURN ? BUSTER_CHECK_NONE : id;
                        id += 1;
                    }
                    if (consumer < count && value < nodes)
                    {
                        check_input_numbering_append(first, last, counts, next, consumer, value);
                    }
                    for (uint32_t k = 0; consumer < count && k < chained && chain < nodes; k += 1)
                    {
                        const uint32_t after = reader->value_next[chain];
                        check_input_numbering_append(first, last, counts, next, consumer, chain);
                        chain = after;
                    }
                }
            }
        }
        input->expression_numbering_ticks = __rdtsc() - start;
        input->expression_numbering_nodes = id - base;
        uint32_t checked = 0;
        uint32_t mismatches = id != nodes;
        for (uint32_t n = base; n < nodes && n < id; n += 1)
        {
            if (numbered[n] != BUSTER_CHECK_NONE)
            {
                checked += 1;
                mismatches += graph->tokens[n] != numbered[n] || (graph->ops[n] != CHECK_OP_VALUE && graph->operand0[n] != operands[n]);
            }
        }
        input->expression_numbering_checked = checked;
        input->expression_numbering_mismatches = mismatches;
    }
}

// The coded walk: the postorder a chunk at a time, compacted to the tokens the walk acts at, each acted at by its class.
// A DROP's action is nothing but its children's chains joined, which no parent reads (its first value is NONE, as the
// reader's setup wrote it), so the compaction leaves its tokens out. A unit with expressions past its limit or deferred
// is acted at by the walk's visit throughout, though still compacted. A unit with overload tables (CheckInput.
// overload_heads: one that names the overloadable attribute in an attribute list, whose every function declaration is a
// candidate) is coded too: the class actions carry the heads through their nodes to their calls as the walk's does.
static void check_input_expressions_coded_range(CheckInputReader* reader, uint32_t begin, uint32_t stop)
{
    const CheckInput* input = reader->output;
    const uint32_t count = reader->parser->tokens.count;
    // Every token's expression ends by the count, so that within these limits none is past them.
    const bool coded = input->diagnostic_limits == NULL && input->deferred_tokens == NULL && reader->expression_limit >= count && input->token_count >= count;
    const uint32_t dropped = BUSTER_CHECK_EXPRESSION_ACTS | CHECK_INPUT_CODE_DROP << BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
    uint32_t tokens[BUSTER_CHECK_EXPRESSION_CHUNK];
    uint32_t classes[BUSTER_CHECK_EXPRESSION_CHUNK];
    CheckInputIdentifiers identifiers;
    CheckInputLiterals literals;
    // The graph's nodes before the walk, from which the numbering's check counts (PPOptions.expression_numbering), and the
    // tokens the classes' actions leave, which it takes as the walk's visits.
    const uint32_t base = reader->output->graph.count;
    const uint64_t scratch_mark = reader->scratch->length;
    reader->expression_declined = coded && begin == 0 && stop == count && reader->pp->options.expression_numbering ? arena_push_array(reader->scratch, (uint64_t)count + 1, 1, 1) : NULL;
    if (reader->expression_declined != NULL)
    {
        memset(reader->expression_declined, 0, (uint64_t)count + 1);
    }
    // The nodes the class actions number before the graph has them (check_input_expression_flush).
    CheckInputBatch* batch = arena_push_array(reader->scratch, 1, sizeof(CheckInputBatch), 64);
    reader->output->resource |= batch == NULL;
    if (batch != NULL)
    {
        batch->base = reader->output->graph.count;
        batch->leaf_base = reader->output->graph.leaf_count;
        batch->count = 0;
        batch->leaves = 0;
        check_graph_classes(batch->classes);
    }
    // The parts' ticks (CheckInput.expression_part_ticks): three TSC reads a chunk, and two a visit of a coded unit's,
    // which are the actions' less their visits'. And the tokens of each class the actions reached, in four histograms
    // that a chunk's run of one class doesn't make a chain of: those the classes' actions acted at are the rest of the
    // ones their actions left (CheckInput.expression_acted).
    uint64_t prepared = 0;
    uint64_t acted = 0;
    uint64_t visited = 0;
    // A coded unit's actions timed by class where the options ask (CheckInput.expression_class_ticks).
    const bool timed = coded && reader->pp->options.expression_timing;
    uint32_t reached[4][BUSTER_CHECK_EXPRESSION_CODES] = { 0 };
    for (uint32_t first = begin; first < stop && !reader->output->resource; first += BUSTER_CHECK_EXPRESSION_CHUNK)
    {
        const uint64_t start = __rdtsc();
        const uint32_t end = stop - first < BUSTER_CHECK_EXPRESSION_CHUNK ? stop : first + BUSTER_CHECK_EXPRESSION_CHUNK;
        const uint32_t acting = check_input_expression_compact(reader, first, end, tokens, classes, coded ? dropped : UINT32_MAX, false);
        if (coded)
        {
            check_input_expression_identifiers(reader, tokens, classes, acting, &identifiers, false);
            check_input_expression_literals(reader, tokens, classes, acting, &literals, false);
        }
        const uint64_t ready = __rdtsc();
        uint32_t identifier = 0;
        uint32_t literal = 0;
        uint32_t i = 0;
        if (!timed)
        {
            for (; i < acting && !reader->output->resource; i += 1)
            {
                check_input_expression_coded(reader, batch, tokens[i], (uint8_t)classes[i], coded, &identifiers, &identifier, &literals, &literal, &visited);
            }
        }
        else
        {
            // Each token's action timed by its class, and a pair of reads after it, whose ticks are the timing's own. The
            // visits aren't timed apart: their reads would be in their classes' ticks, which only these runs pay.
            for (; i < acting && !reader->output->resource; i += 1)
            {
                const uint64_t before = __rdtsc();
                check_input_expression_coded_timed(reader, batch, tokens[i], (uint8_t)classes[i], coded, &identifiers, &identifier, &literals, &literal);
                const uint64_t after = __rdtsc();
                reader->output->expression_class_ticks[(classes[i] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT] += after - before;
                reader->output->expression_class_overhead += __rdtsc() - after;
            }
        }
        for (uint32_t k = 0; coded && k < i; k += 1)
        {
            reached[k & 3][(classes[k] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT] += 1;
        }
        const uint64_t done = __rdtsc();
        prepared += ready - start;
        acted += done - ready;
    }
    // The last batch's nodes, written as the actions' (and its time theirs).
    if (batch != NULL)
    {
        const uint64_t start = __rdtsc();
        check_input_expression_flush(reader, batch, false);
        acted += __rdtsc() - start;
    }
    // A coded unit's visits lie within its chunks' actions; another's actions are all visits.
    visited = coded ? (visited < acted ? visited : acted) : acted;
    reader->output->expression_part_ticks[BUSTER_CHECK_EXPRESSION_PART_PREPARE] = prepared;
    reader->output->expression_part_ticks[BUSTER_CHECK_EXPRESSION_PART_ACTIONS] = acted - visited;
    reader->output->expression_part_ticks[BUSTER_CHECK_EXPRESSION_PART_VISITS] = visited;
    for (uint32_t code = 0; code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
    {
        const uint32_t tokens_reached = reached[0][code] + reached[1][code] + reached[2][code] + reached[3][code];
        reader->output->expression_acted[code] = tokens_reached - reader->output->expression_left[code];
    }
    if (reader->expression_declined != NULL && !reader->output->resource)
    {
        check_input_expression_numbering(reader, base, tokens, classes);
    }
    reader->expression_declined = NULL;
    arena_pop_to(reader->scratch, scratch_mark);
}

static void check_input_expressions_coded(CheckInputReader* reader)
{
    check_input_expressions_coded_range(reader, 0, reader->parser->tokens.count);
}

// The tokens the coded walk acted at by their classes' own actions: CheckInput.expression_acted's sum over the classes.
static __attribute__((unused)) uint32_t check_input_expression_classed(const CheckInput* input)
{
    uint32_t total = 0;
    for (uint32_t code = 0; code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
    {
        total += input->expression_acted[code];
    }
    return total;
}

// The tokens the coded walk's class actions declined and left to the walk's visit: CheckInput.expression_left's sum over
// the classes but CHECK_INPUT_CODE_SCALAR, whose tokens no class's action takes.
static __attribute__((unused)) uint32_t check_input_expression_declined(const CheckInput* input)
{
    uint32_t total = 0;
    for (uint32_t code = 0; code < BUSTER_CHECK_EXPRESSION_CODES; code += 1)
    {
        total += code != CHECK_INPUT_CODE_SCALAR ? input->expression_left[code] : 0;
    }
    return total;
}
