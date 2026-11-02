// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Memoized type transitions (#1050): the vector path of check_graph_run checks most nodes by an earlier node's answer.
// When no operand of a node holds a payload (check_stream_value_rest), its action is a function of a small key: its
// operator, member, requested type, extensions and floating environment, and each operand's (and a call's arguments') type,
// flags, category and constant kind, with which of them have an object and which share one. The key holds no object and no
// node: an action copies those, and never reads an object but for zero and equality, nor a node but for a fault's, and an
// answer with a fault isn't memoized. The first node of a key runs its action (a miss) and records its answer; a later node
// of the key (a hit) writes that answer, its object taken from the operand the answer names, with no CheckAction and no
// CheckActionResult. A hit interns no type: every type the answer names was interned by its first node, so the misses, in
// node order, intern what the scalar path does in the order it does. An answer that read a tag's completion at its
// source position keeps the answer as a guard, which a hit asks again at its own position (CheckAction.guards); one that
// read a transparent union's activation, a constant evaluation's, a vector member's or an overload set's isn't memoized.
//
// The nodes go in node order (check_graph_transitions): each node's key from its operands' columns, its hash, one probe,
// the key compared whole in one AVX-512 compare with its entry's (an entry is a key and its answer, two cache lines), and
// a hit's answer written. A run of consecutive pure misses and folds that read none of each other goes as one batch of
// check_actions, whose integer folds go 8 at a time, as the regions' batches did. The alternative,
// CheckOptions.transition_windows (check_graph_transition_windows), keys the ready nodes of 64-node windows 16 lanes at a
// time in AVX-512, round by round as their operands within the window complete. check_transition_key is the windows' keys'
// exact twin, and the scalar path (--scalar, the node-by-node loop without transitions) the answers' oracle.
// Include from c_checker.c after check_graph_guarded_node.
#ifndef BUSTER_C_CHECK_TRANSITIONS_INCLUDED
#define BUSTER_C_CHECK_TRANSITIONS_INCLUDED

#define BUSTER_CHECK_TRANSITION_WORDS 16u
#define BUSTER_CHECK_TRANSITION_LANES 16u
#define BUSTER_CHECK_TRANSITION_HORIZON 64u
// The most arguments a memoized call has, and each one's key words: its type, flags, and shape with whether it has an object.
#define BUSTER_CHECK_TRANSITION_ARGUMENTS 64u
#define BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS 3u
// A node's shape (check_transition_shape) when its value holds a payload row, and while it waits in a batch of check_actions.
#define BUSTER_CHECK_TRANSITION_PAYLOAD UINT32_MAX
#define BUSTER_CHECK_TRANSITION_PENDING (UINT32_MAX - 1)
#define BUSTER_CHECK_TRANSITION_INITIAL 1024u

// The key's words: the header (the operator, extensions << 8, the floating environment << 11, the objects' pattern << 12
// and the arguments' count << 18), the member, the requested type, the operands' types and flags, their shapes (operand
// 0's and 1's in one word, 2's in the next), the arguments' two hashes, and three zero words.
#define BUSTER_CHECK_KEY_HEADER 0u
#define BUSTER_CHECK_KEY_MEMBER 1u
#define BUSTER_CHECK_KEY_REQUESTED 2u
#define BUSTER_CHECK_KEY_TYPES 3u
#define BUSTER_CHECK_KEY_FLAGS 6u
#define BUSTER_CHECK_KEY_SHAPES 9u
#define BUSTER_CHECK_KEY_SHAPE2 10u
#define BUSTER_CHECK_KEY_ARGUMENTS 11u

// How a node's key came out: memoized, an operator or a node the table doesn't take, an operand or argument with a payload
// (a fold), or an operand waiting in a batch, which must run first.
typedef enum CheckTransitionClass
{
    CHECK_TRANSITION_KEYED,
    CHECK_TRANSITION_NONE,
    CHECK_TRANSITION_FOLD,
    CHECK_TRANSITION_WAIT,
} CheckTransitionClass;

// An entry: its key, and its answer in the next cache line. The answer is the node's value but its object and a payload,
// its conversions, a member's offset, its warnings, and in `packed` its shape (16 bits), the operand whose object it takes
// (2 bits: 1 + its index, or 0 for none), its diagnostic (6 bits) and the diagnostic's operand (8 bits). A single guard is
// `guard`, the tag plus one with the answer in bit 31; `extra`, 1 + an index into the extras, holds what else it has.
typedef struct CheckTransitionEntry CheckTransitionEntry;
struct CheckTransitionEntry
{
    uint32_t key[BUSTER_CHECK_TRANSITION_WORDS];
    CheckNodeConversions conversions;
    uint64_t member_offset;
    TypeId type;
    uint32_t flags;
    uint32_t warnings;
    uint32_t packed;
    uint32_t guard;
    uint32_t extra;
};

_Static_assert(sizeof(CheckTransitionEntry) == 128, "an entry is its key's cache line and its answer's");

// What an answer has besides its entry: its whole value as a template where it holds a payload row (1 + the template's
// index), a call's arguments (from `arguments`), and two guards or more.
typedef struct CheckTransitionExtra CheckTransitionExtra;
struct CheckTransitionExtra
{
    uint32_t payload;
    uint32_t arguments;
    uint32_t guard_count;
    uint32_t guard_answers;
    uint32_t guard_tags[BUSTER_CHECK_ACTION_GUARDS];
};

// A memoized call's argument: its key words, and its conversion.
typedef struct CheckTransitionArgument CheckTransitionArgument;
struct CheckTransitionArgument
{
    uint32_t words[BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS];
    CheckConversion conversion;
};

// A graph run's table, in its scratch arena: open-addressed slots, each an entry's index << 32 and its hash (whose low bit
// is set, so that 0 is an empty slot), and the entries in insertion order, with their hashes, extras, templates and calls'
// arguments. `full` stops insertion once an array can't grow within `limit`.
typedef struct CheckTransitions CheckTransitions;
struct CheckTransitions
{
    Arena* arena;
    uint64_t* slots;
    uint32_t mask;
    CheckTransitionEntry* entries;
    uint32_t* hashes;
    uint32_t count;
    uint32_t capacity;
    uint32_t hash_capacity;
    CheckTransitionExtra* extras;
    uint32_t extra_count;
    uint32_t extra_capacity;
    CheckValue* templates;
    uint32_t template_count;
    uint32_t template_capacity;
    CheckTransitionArgument* arguments;
    uint32_t argument_count;
    uint32_t argument_capacity;
    // The arena's length past which the table doesn't grow: a quarter of the room the run's scratch had left, so that the
    // table never takes what the run's other stages need.
    uint64_t limit;
    bool full;
};

// A miss's key, hash, operands' objects, the entry its failed guards leave, and its guards, kept for its insertion once its
// action has run.
typedef struct CheckTransitionMiss CheckTransitionMiss;
struct CheckTransitionMiss
{
    _Alignas(64) uint32_t key[BUSTER_CHECK_TRANSITION_WORDS];
    uint32_t hash;
    uint32_t objects[3];
    uint32_t existing;
    bool keyed;
    CheckActionGuards guards;
};

// The batch of consecutive pure misses and folds that read none of each other, which the node-order path runs as one
// call of check_actions once a node reads a member (check_transition_pending), a node that isn't pure misses, or the batch
// fills. Its members are in node order.
typedef struct CheckTransitionBatch CheckTransitionBatch;
struct CheckTransitionBatch
{
    CheckTransitionMiss misses[BUSTER_CHECK_BATCH];
    uint32_t nodes[BUSTER_CHECK_BATCH];
    uint32_t count;
};

// The operators whose actions the table memoizes: every ordinary action's, which the node's own operands and arguments
// decide. VALUE completes as a leaf; the builtins, varargs, the variable-length arrays' operators and a statement
// expression's value keep their own records or effects.
static bool check_transition_op(CheckOp op)
{
    return (op >= CHECK_OP_POSITIVE && op <= CHECK_OP_SELECT) || op == CHECK_OP_REAL || op == CHECK_OP_IMAGINARY;
}

// A completed node's shape: its category and constant kind, or BUSTER_CHECK_TRANSITION_PAYLOAD when its value holds a
// payload row (whose category and constant kind may pass a byte).
static uint32_t check_transition_shape(const CheckStream* stream, uint32_t node)
{
    return stream->payload_rows[node] != 0 ? BUSTER_CHECK_TRANSITION_PAYLOAD :
           (uint32_t)stream->value_categories[node] | (uint32_t)stream->value_constants[node] << 8;
}

// Whether `node` waits in `batch` (or null): its action hasn't run.
static bool check_transition_pending(const CheckTransitionBatch* batch, uint32_t node)
{
    bool pending = false;
    if (batch != NULL && batch->count != 0 && node >= batch->nodes[0])
    {
        for (uint32_t i = 0; i < batch->count; i += 1)
        {
            pending |= batch->nodes[i] == node;
        }
    }
    return pending;
}

// A complete node's shape, or BUSTER_CHECK_TRANSITION_PENDING for one waiting in `batch`.
static uint32_t check_transition_operand_shape(const CheckStream* stream, const CheckTransitionBatch* batch, uint32_t node)
{
    return check_transition_pending(batch, node) ? BUSTER_CHECK_TRANSITION_PENDING : check_transition_shape(stream, node);
}

static const uint32_t check_transition_multipliers[BUSTER_CHECK_TRANSITION_WORDS] =
{
    0x9e3779b1u, 0x85ebca77u, 0xc2b2ae3du, 0x27d4eb2fu, 0x165667b1u, 0xd3a2646du, 0xfd7046c5u, 0xb55a4f09u,
    0x7feb352du, 0x846ca68bu, 0xed5ad4bbu, 0xac4c1b51u, 0x31848babu, 0x2c1b3c6du, 0x297a2d39u, 0x68e31da5u,
};

// A key's hash, its low bit set: each word times its odd multiplier, summed, then mixed. The windows' hashes
// (check_transition_batch) are its twin, which must give the same bits.
static uint32_t check_transition_hash(const uint32_t* key)
{
    uint32_t sum = 0;
    for (uint32_t w = 0; w < BUSTER_CHECK_TRANSITION_WORDS; w += 1)
    {
        sum += key[w] * check_transition_multipliers[w];
    }
    sum ^= sum >> 15;
    sum *= 0x2c1b3c6du;
    sum ^= sum >> 12;
    sum *= 0x297a2d39u;
    sum ^= sum >> 15;
    return sum | 1;
}

// A call's argument words (BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS each) and their two hashes (FNV-1a's from two seeds):
// CHECK_TRANSITION_FOLD where an argument holds a payload, CHECK_TRANSITION_WAIT where one waits in `batch` (or null).
static CheckTransitionClass check_transition_arguments(const CheckGraph* graph, const CheckStream* stream, const CheckTransitionBatch* batch, uint32_t node,
                                                       uint32_t* words, uint32_t* first, uint32_t* second)
{
    const uint32_t offset = graph->argument_offsets[node];
    const uint32_t count = graph->argument_counts[node];
    CheckTransitionClass result = CHECK_TRANSITION_KEYED;
    uint32_t a = 0x811c9dc5u;
    uint32_t b = 0x01000193u ^ count;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t argument = graph->argument_nodes[offset + i];
        const uint32_t shape = check_transition_operand_shape(stream, batch, argument);
        result = shape == BUSTER_CHECK_TRANSITION_PENDING ? CHECK_TRANSITION_WAIT :
                 shape == BUSTER_CHECK_TRANSITION_PAYLOAD && result == CHECK_TRANSITION_KEYED ? CHECK_TRANSITION_FOLD : result;
        uint32_t* word = words + i * BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS;
        word[0] = stream->value_types[argument];
        word[1] = stream->value_flags[argument];
        word[2] = shape | (uint32_t)(stream->value_objects[argument] != 0) << 16;
        for (uint32_t w = 0; w < BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS; w += 1)
        {
            a = (a ^ word[w]) * 0x01000193u;
            b = (b ^ word[w]) * 0x9e3779b1u;
        }
    }
    *first = result == CHECK_TRANSITION_KEYED ? a : 0;
    *second = result == CHECK_TRANSITION_KEYED ? b : 0;
    return result;
}

// Whether the node itself may be memoized, whatever its operands: its operator, no constant evaluation, no vector member
// (`vector_member`), no plain library builtin's fold, and no more arguments than a key holds.
static bool check_transition_node(const CheckGraph* graph, const CheckStream* stream, uint32_t node, bool vector_member)
{
    const CheckOp op = graph->ops[node];
    bool result = check_transition_op(op) && !vector_member && (stream->constant_evaluated == NULL || stream->constant_evaluated[node] == 0);
    if (result && op == CHECK_OP_CALL)
    {
        result = (graph->members[node] & BUSTER_CHECK_LIBRARY_CALL) == 0 && graph->argument_counts[node] <= BUSTER_CHECK_TRANSITION_ARGUMENTS;
    }
    return result;
}

// The objects' pattern of three operands' objects: two bits each, 0 for none, 1 + the first operand of the same object.
static uint32_t check_transition_pattern(const uint32_t* objects)
{
    const uint32_t p0 = objects[0] != 0;
    const uint32_t p1 = objects[1] == 0 ? 0 : objects[1] == objects[0] ? 1 : 2;
    const uint32_t p2 = objects[2] == 0 ? 0 : objects[2] == objects[0] ? 1 : objects[2] == objects[1] ? 2 : 3;
    return p0 | p1 << 2 | p2 << 4;
}

// A node's key, its operands' objects and a call's argument words, one node at a time, for a node the table takes or not
// (`eligible`, check_transition_node's answer); the windows' keys' exact twin. Its operands and arguments are complete but
// those waiting in `batch` (or null); a key that isn't CHECK_TRANSITION_KEYED is zero.
static CheckTransitionClass check_transition_key(const CheckGraph* graph, const CheckStream* stream, const CheckTransitionBatch* batch, uint32_t node, bool eligible,
                                                 uint32_t* key, uint32_t* objects, uint32_t* argument_words)
{
    // Every column's pointer and every word first, then the key's stores: a store through a uint32_t pointer could alias a
    // column's pointer (-fno-strict-aliasing), which the compiler would load again after each.
    const TypeId* value_types = stream->value_types;
    const uint32_t* value_flags = stream->value_flags;
    const uint32_t* value_objects = stream->value_objects;
    const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    CheckTransitionClass result = eligible ? CHECK_TRANSITION_KEYED : CHECK_TRANSITION_NONE;
    uint32_t words[BUSTER_CHECK_TRANSITION_WORDS] = { 0 };
    uint32_t operand_objects[3];
    uint32_t operand_shapes[3];
    for (uint32_t i = 0; i < 3; i += 1)
    {
        const bool present = operands[i] != BUSTER_CHECK_NONE;
        operand_objects[i] = present ? value_objects[operands[i]] : 0;
        operand_shapes[i] = present ? check_transition_operand_shape(stream, batch, operands[i]) : 0;
        words[BUSTER_CHECK_KEY_TYPES + i] = present ? value_types[operands[i]] : 0;
        words[BUSTER_CHECK_KEY_FLAGS + i] = present ? value_flags[operands[i]] : 0;
        result = operand_shapes[i] == BUSTER_CHECK_TRANSITION_PENDING ? CHECK_TRANSITION_WAIT :
                 operand_shapes[i] == BUSTER_CHECK_TRANSITION_PAYLOAD && result == CHECK_TRANSITION_KEYED ? CHECK_TRANSITION_FOLD : result;
    }
    const CheckOp op = graph->ops[node];
    const uint32_t token = graph->tokens != NULL ? graph->tokens[node] : BUSTER_CHECK_NONE;
    const uint32_t extensions = graph->extensions | (check_graph_system_token(graph, token) ? BUSTER_CHECK_EXTENSION_SYSTEM_HEADER : 0);
    const uint32_t environment = graph->fp_environments != NULL ? graph->fp_environments[node] : 0;
    const uint32_t count = op == CHECK_OP_CALL ? graph->argument_counts[node] : 0;
    words[BUSTER_CHECK_KEY_HEADER] = (uint32_t)op | extensions << 8 | environment << 11 | check_transition_pattern(operand_objects) << 12 | count << 18;
    words[BUSTER_CHECK_KEY_MEMBER] = graph->members[node];
    words[BUSTER_CHECK_KEY_REQUESTED] = graph->types[node];
    words[BUSTER_CHECK_KEY_SHAPES] = (operand_shapes[0] & 0xffff) | operand_shapes[1] << 16;
    words[BUSTER_CHECK_KEY_SHAPE2] = operand_shapes[2] & 0xffff;
    if (result != CHECK_TRANSITION_NONE && op == CHECK_OP_CALL)
    {
        const CheckTransitionClass arguments = check_transition_arguments(graph, stream, batch, node, argument_words, words + BUSTER_CHECK_KEY_ARGUMENTS,
                                                                          words + BUSTER_CHECK_KEY_ARGUMENTS + 1);
        result = arguments == CHECK_TRANSITION_WAIT || (arguments == CHECK_TRANSITION_FOLD && result == CHECK_TRANSITION_KEYED) ? arguments : result;
    }
    for (uint32_t w = 0; w < BUSTER_CHECK_TRANSITION_WORDS; w += 1)
    {
        key[w] = result == CHECK_TRANSITION_KEYED ? words[w] : 0;
    }
    for (uint32_t i = 0; i < 3; i += 1)
    {
        objects[i] = operand_objects[i];
    }
    return result;
}

// Whether `bytes` more, and an alignment's padding, keep the table's arena within its limit.
static bool check_transitions_fit(const CheckTransitions* table, uint64_t bytes)
{
    return table->arena->length <= table->limit && bytes + 128 <= table->limit - table->arena->length;
}

// The table, with room for an entry per 16 of the graph's `nodes` (sqlite3.c's bodies took one per 19), so that it seldom
// grows, whose copies would take as much memory again: an array's pages are only touched as its entries fill them. Where
// that much doesn't fit within the limit, room for BUSTER_CHECK_TRANSITION_INITIAL; false where even that doesn't.
static bool check_transitions_initialize(CheckTransitions* table, Arena* arena, uint32_t nodes)
{
    *table = (CheckTransitions){
        .arena = arena,
        .limit = arena->length + (arena->capacity > arena->length ? (arena->capacity - arena->length) / 4 : 0),
    };
    uint32_t capacity = BUSTER_CHECK_TRANSITION_INITIAL;
    while (capacity < nodes / 16 && capacity < (1u << 22))
    {
        capacity *= 2;
    }
    bool result = false;
    for (uint32_t attempt = 0; !result && attempt < 2; attempt += 1)
    {
        capacity = attempt == 0 ? capacity : BUSTER_CHECK_TRANSITION_INITIAL;
        const uint64_t slots = (uint64_t)capacity * 2;
        const uint64_t bytes = slots * sizeof(uint64_t) + (uint64_t)capacity * (sizeof(CheckTransitionEntry) + sizeof(uint32_t)) + 256;
        result = check_transitions_fit(table, bytes);
        if (result)
        {
            table->slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, slots);
            table->entries = arena_push_array(arena, capacity, sizeof(CheckTransitionEntry), 128);
            table->hashes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
            result = table->slots != NULL && table->entries != NULL && table->hashes != NULL;
        }
        if (result)
        {
            memset(table->slots, 0, slots * sizeof(uint64_t));
            table->mask = (uint32_t)slots - 1;
            table->capacity = capacity;
            table->hash_capacity = capacity;
        }
    }
    return result;
}

// Room for `needed` elements in an array of the table's arena, moved to one twice as large as it grows; false, with the
// array as it was, where the arena has no room within the table's limit.
static bool check_transitions_room(const CheckTransitions* table, void** array, uint32_t* capacity, uint32_t needed, uint64_t size, uint64_t alignment)
{
    bool result = needed <= *capacity;
    if (!result)
    {
        uint64_t grown = *capacity < 64 ? 64 : (uint64_t)*capacity * 2;
        grown = grown < needed ? needed : grown;
        void* moved = grown <= UINT32_MAX && check_transitions_fit(table, grown * size) ? arena_push_array(table->arena, grown, size, alignment) : NULL;
        if (moved != NULL)
        {
            if (*capacity != 0)
            {
                memcpy(moved, *array, (uint64_t)*capacity * size);
            }
            *array = moved;
            *capacity = (uint32_t)grown;
            result = true;
        }
    }
    return result;
}

// Room for one more entry, and the slots at most half full: rehashed from the entries' hashes when they double.
static bool check_transitions_grow(CheckTransitions* table)
{
    bool result = check_transitions_room(table, (void**)&table->entries, &table->capacity, table->count + 1, sizeof(CheckTransitionEntry), 128) &&
                  check_transitions_room(table, (void**)&table->hashes, &table->hash_capacity, table->count + 1, sizeof(uint32_t), 4);
    if (result && (uint64_t)(table->count + 1) * 2 > (uint64_t)table->mask + 1)
    {
        const uint64_t count = ((uint64_t)table->mask + 1) * 2;
        uint64_t* slots = check_transitions_fit(table, count * sizeof(uint64_t)) ? BUSTER_ARENA_PUSH_ARRAY(table->arena, uint64_t, count) : NULL;
        result = slots != NULL;
        if (result)
        {
            memset(slots, 0, count * sizeof(uint64_t));
            const uint32_t mask = (uint32_t)count - 1;
            for (uint32_t i = 0; i < table->count; i += 1)
            {
                uint32_t slot = table->hashes[i] & mask;
                while (slots[slot] != 0)
                {
                    slot = (slot + 1) & mask;
                }
                slots[slot] = (uint64_t)i << 32 | table->hashes[i];
            }
            table->slots = slots;
            table->mask = mask;
        }
    }
    return result;
}

// The entry of `key` (whose 16 words are 64-byte aligned, with a call's `argument_words`, `argument_count` arguments' of
// them), or BUSTER_CHECK_NONE: each slot of its hash's run until an empty one, an entry of its hash compared whole.
static uint32_t check_transitions_find(const CheckTransitions* table, const uint32_t* key, uint32_t hash, const uint32_t* argument_words, uint32_t argument_count)
{
    const __m512i wanted = _mm512_load_si512(key);
    uint32_t found = BUSTER_CHECK_NONE;
    uint32_t slot = hash & table->mask;
    bool searching = true;
    while (searching)
    {
        const uint64_t entry = table->slots[slot];
        searching = entry != 0;
        if (searching && (uint32_t)entry == hash)
        {
            const uint32_t index = (uint32_t)(entry >> 32);
            const CheckTransitionEntry* candidate = table->entries + index;
            bool equal = _mm512_cmpneq_epi32_mask(_mm512_load_si512(candidate->key), wanted) == 0;
            const CheckTransitionArgument* arguments = equal && argument_count != 0 ? table->arguments + table->extras[candidate->extra - 1].arguments : NULL;
            for (uint32_t i = 0; equal && i < argument_count; i += 1)
            {
                for (uint32_t w = 0; w < BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS; w += 1)
                {
                    equal &= arguments[i].words[w] == argument_words[i * BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS + w];
                }
            }
            found = equal ? index : BUSTER_CHECK_NONE;
            searching = !equal;
        }
        slot = (slot + 1) & table->mask;
    }
    return found;
}

// Whether an entry's guards hold at the node's source position: each tag complete there exactly where it was.
static bool check_transition_guards_hold(const CheckGraph* graph, const CheckTransitions* table, const CheckTransitionEntry* entry, uint32_t node)
{
    const uint32_t position = graph->source_positions != NULL ? graph->source_positions[node] : BUSTER_CHECK_NONE;
    const uint32_t* completions = graph->tag_completions;
    const uint32_t tags = completions != NULL ? graph->tag_completion_count : 0;
    bool hold = true;
    if (entry->guard != 0)
    {
        const uint32_t tag = (entry->guard & 0x7fffffffu) - 1;
        hold = (tag < tags && completions[tag] <= position) == (entry->guard >> 31 != 0);
    }
    const CheckTransitionExtra* extra = entry->extra != 0 ? table->extras + entry->extra - 1 : NULL;
    for (uint32_t i = 0; extra != NULL && i < extra->guard_count; i += 1)
    {
        const uint32_t tag = extra->guard_tags[i];
        hold &= (tag < tags && completions[tag] <= position) == ((extra->guard_answers >> i & 1) != 0);
    }
    return hold;
}

// A hit: entry `index`'s answer written as node `node`'s, its object the operand's it names, and its shape in `shapes` (or
// null).
static void check_transition_write(CheckStream* stream, const CheckGraph* graph, const CheckTransitions* table, uint32_t index, uint32_t node, const uint32_t* objects,
                                   uint32_t* shapes)
{
    // The answer's fields and the columns' pointers first, then the stores (as check_transition_key's).
    const CheckTransitionEntry* entry = table->entries + index;
    const uint32_t packed = entry->packed;
    const uint32_t rule = packed >> 16 & 3;
    const uint32_t object = rule != 0 ? objects[rule - 1] : 0;
    const CheckTransitionExtra* extra = entry->extra != 0 ? table->extras + entry->extra - 1 : NULL;
    const bool payload = extra != NULL && extra->payload != 0;
    const CheckNodeConversions conversions = entry->conversions;
    const uint64_t member_offset = entry->member_offset;
    const TypeId type = entry->type;
    const uint32_t flags = entry->flags;
    const uint32_t warnings = entry->warnings;
    TypeId* const value_types = stream->value_types;
    uint32_t* const value_flags = stream->value_flags;
    uint32_t* const value_objects = stream->value_objects;
    uint8_t* const value_categories = stream->value_categories;
    uint8_t* const value_constants = stream->value_constants;
    CheckNodeConversions* const node_conversions = stream->conversions;
    CheckDiagnostic* const diagnostics = stream->diagnostics;
    uint32_t* const diagnostic_operands = stream->diagnostic_operands;
    uint32_t* const node_warnings = stream->warnings;
    uint64_t* const member_offsets = stream->member_offsets;
    if (payload)
    {
        CheckValue value = table->templates[extra->payload - 1];
        value.object = object;
        check_stream_set_value(stream, node, &value);
    }
    else
    {
        value_types[node] = type;
        value_flags[node] = flags;
        value_objects[node] = object;
        value_categories[node] = (uint8_t)packed;
        value_constants[node] = (uint8_t)(packed >> 8);
    }
    node_conversions[node] = conversions;
    diagnostics[node] = (CheckDiagnostic)(packed >> 18 & 0x3f);
    diagnostic_operands[node] = packed >> 24;
    node_warnings[node] = warnings;
    member_offsets[node] = member_offset;
    if (shapes != NULL)
    {
        shapes[node] = payload ? BUSTER_CHECK_TRANSITION_PAYLOAD : packed & 0xffff;
    }
    if (graph->ops[node] == CHECK_OP_CALL && graph->argument_counts[node] != 0)
    {
        const uint32_t offset = graph->argument_offsets[node];
        const uint32_t count = graph->argument_counts[node];
        const CheckTransitionArgument* arguments = table->arguments + extra->arguments;
        CheckConversion* const argument_conversions = stream->argument_conversions + offset;
        for (uint32_t i = 0; i < count; i += 1)
        {
            argument_conversions[i] = arguments[i].conversion;
        }
    }
}

// A miss's answer, from the stream its action wrote, inserted under its key (or written over entry `existing`, whose guards
// failed): unless the action read what no guard holds, ran out of room, gave a fault, an overload set, an object of none of
// its operands, or fields past what an entry packs. A table that can't grow takes no more.
static void check_transitions_insert(CheckTransitions* table, const CheckGraph* graph, const CheckStream* stream, uint32_t node, const uint32_t* key, uint32_t hash,
                                     const uint32_t* objects, const CheckActionGuards* guards, const uint32_t* argument_words, uint32_t existing,
                                     CheckStatistics* statistics)
{
    const CheckValue value = check_stream_value(stream, node);
    const uint32_t object = value.object == 0 ? 0 : value.object == objects[0] ? 1 : value.object == objects[1] ? 2 : value.object == objects[2] ? 3 : 4;
    const CheckDiagnostic diagnostic = stream->diagnostics[node];
    const uint32_t diagnostic_operand = stream->diagnostic_operands[node];
    const bool memoized = !guards->unknown && diagnostic != CHECK_DIAG_RESOURCE && (uint32_t)diagnostic < 64 && diagnostic_operand < 256 &&
                          value.fault == CHECK_FAULT_NONE && value.overload_head == 0 && object != 4;
    const uint32_t count = graph->ops[node] == CHECK_OP_CALL ? graph->argument_counts[node] : 0;
    const bool payload = stream->payload_rows[node] != 0;
    const bool extra = payload || count != 0 || guards->count > 1;
    bool insert = memoized && !table->full && (existing != BUSTER_CHECK_NONE || check_transitions_grow(table));
    insert = insert && (!extra || check_transitions_room(table, (void**)&table->extras, &table->extra_capacity, table->extra_count + 1, sizeof(CheckTransitionExtra),
                                                         _Alignof(CheckTransitionExtra)));
    insert = insert && (!payload || check_transitions_room(table, (void**)&table->templates, &table->template_capacity, table->template_count + 1, sizeof(CheckValue),
                                                           _Alignof(CheckValue)));
    insert = insert && (count == 0 || check_transitions_room(table, (void**)&table->arguments, &table->argument_capacity, table->argument_count + count,
                                                             sizeof(CheckTransitionArgument), _Alignof(CheckTransitionArgument)));
    table->full |= memoized && !insert;
    if (insert)
    {
        const uint32_t shape = (uint32_t)stream->value_categories[node] | (uint32_t)stream->value_constants[node] << 8;
        const uint32_t index = existing != BUSTER_CHECK_NONE ? existing : table->count;
        CheckTransitionEntry* entry = table->entries + index;
        if (existing == BUSTER_CHECK_NONE)
        {
            memcpy(entry->key, key, sizeof(entry->key));
        }
        entry->conversions = stream->conversions[node];
        entry->member_offset = stream->member_offsets[node];
        entry->type = value.type;
        entry->flags = value.flags;
        entry->warnings = stream->warnings[node];
        entry->packed = shape | object << 16 | (uint32_t)diagnostic << 18 | diagnostic_operand << 24;
        entry->guard = guards->count == 1 ? (guards->tags[0] + 1) | (guards->answers & 1) << 31 : 0;
        entry->extra = 0;
        if (extra)
        {
            CheckTransitionExtra* record = table->extras + table->extra_count;
            *record = (CheckTransitionExtra){ 0 };
            table->extra_count += 1;
            entry->extra = table->extra_count;
            if (payload)
            {
                CheckValue template = value;
                template.object = 0;
                table->templates[table->template_count] = template;
                table->template_count += 1;
                record->payload = table->template_count;
            }
            if (count != 0)
            {
                const uint32_t offset = graph->argument_offsets[node];
                record->arguments = table->argument_count;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    CheckTransitionArgument* argument = table->arguments + table->argument_count + i;
                    memcpy(argument->words, argument_words + i * BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS, sizeof(argument->words));
                    argument->conversion = stream->argument_conversions[offset + i];
                }
                table->argument_count += count;
            }
            if (guards->count > 1)
            {
                record->guard_count = guards->count;
                record->guard_answers = guards->answers;
                memcpy(record->guard_tags, guards->tags, sizeof(record->guard_tags));
            }
        }
        if (existing == BUSTER_CHECK_NONE)
        {
            table->hashes[index] = hash;
            table->count += 1;
            uint32_t slot = hash & table->mask;
            while (table->slots[slot] != 0)
            {
                slot = (slot + 1) & table->mask;
            }
            table->slots[slot] = (uint64_t)index << 32 | hash;
        }
        statistics->transition_inserts += 1;
    }
}

// Whether two values are the same, every field a reader reads (a long double by its ten bytes).
static bool check_transition_value_equal(const CheckValue* a, const CheckValue* b)
{
    return a->type == b->type && a->category == b->category && a->flags == b->flags && a->constant_kind == b->constant_kind && a->integer == b->integer &&
           a->floating_storage[0] == b->floating_storage[0] && (a->floating_storage[1] & 0xffff) == (b->floating_storage[1] & 0xffff) &&
           a->imaginary_storage[0] == b->imaginary_storage[0] && (a->imaginary_storage[1] & 0xffff) == (b->imaginary_storage[1] & 0xffff) &&
           a->object == b->object && a->overload_head == b->overload_head && a->offset == b->offset && a->bit_width == b->bit_width &&
           a->fault == b->fault && a->fault_node == b->fault_node && a->object_bit_offset == b->object_bit_offset;
}

// CheckOptions.verify_transitions: a hit's node checked by its action too, whose answer the stream keeps, against the
// entry's; a mismatch, or a type the action interned, counts (CheckStatistics.transition_mismatches). `shapes` (or null)
// takes the node's shape.
static void check_transition_verify(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t node, CheckValue* arguments, Arena* scratch,
                                    uint32_t* shapes)
{
    const CheckValue value = check_stream_value(stream, node);
    const CheckNodeConversions conversions = stream->conversions[node];
    const CheckDiagnostic diagnostic = stream->diagnostics[node];
    const uint32_t diagnostic_operand = stream->diagnostic_operands[node];
    const uint32_t warnings = stream->warnings[node];
    const uint64_t member_offset = stream->member_offsets[node];
    const bool call = graph->ops[node] == CHECK_OP_CALL;
    const uint32_t offset = call ? graph->argument_offsets[node] : 0;
    const uint32_t count = call ? graph->argument_counts[node] : 0;
    CheckConversion argument_conversions[BUSTER_CHECK_TRANSITION_ARGUMENTS];
    for (uint32_t i = 0; i < count; i += 1)
    {
        argument_conversions[i] = stream->argument_conversions[offset + i];
    }
    const uint32_t interned = types->types.count;
    check_graph_guarded_node(types, graph, stream, node, arguments, scratch, false, NULL);
    stream->statistics.scalar_nodes -= 1;
    if (shapes != NULL)
    {
        shapes[node] = check_transition_shape(stream, node);
    }
    const CheckValue checked = check_stream_value(stream, node);
    bool equal = check_transition_value_equal(&value, &checked) && memcmp(&conversions, stream->conversions + node, sizeof(conversions)) == 0 &&
                 diagnostic == stream->diagnostics[node] && diagnostic_operand == stream->diagnostic_operands[node] && warnings == stream->warnings[node] &&
                 member_offset == stream->member_offsets[node] && interned == types->types.count;
    for (uint32_t i = 0; equal && i < count; i += 1)
    {
        equal = argument_conversions[i].type == stream->argument_conversions[offset + i].type &&
                argument_conversions[i].flags == stream->argument_conversions[offset + i].flags;
    }
    stream->statistics.transition_mismatches += !equal;
}


// After a node's action: its shape in `shapes` (or null), and a keyed miss's answer inserted.
static void check_transition_missed(CheckTransitions* table, const CheckGraph* graph, CheckStream* stream, uint32_t node, const CheckTransitionMiss* miss,
                                    const uint32_t* argument_words, uint32_t* shapes)
{
    if (shapes != NULL)
    {
        shapes[node] = check_transition_shape(stream, node);
    }
    if (miss->keyed)
    {
        check_transitions_insert(table, graph, stream, node, miss->key, miss->hash, miss->objects, &miss->guards, argument_words, miss->existing, &stream->statistics);
    }
}

// The batch's members run as one batch of check_actions (one alone as its node), in node order, and inserted. A pure node
// reads no argument slice, so a member's insertion needs no argument words.
static void check_transition_flush(TypeTable* types, const CheckGraph* graph, CheckStream* stream, CheckTransitions* table, CheckTransitionBatch* batch,
                                   CheckValue* arguments, Arena* scratch)
{
    if (batch->count == 1)
    {
        CheckTransitionMiss* miss = batch->misses;
        check_graph_guarded_node(types, graph, stream, batch->nodes[0], arguments, scratch, false, miss->keyed ? &miss->guards : NULL);
        check_transition_missed(table, graph, stream, batch->nodes[0], miss, NULL, NULL);
    }
    else if (batch->count != 0)
    {
        CheckAction actions[BUSTER_CHECK_BATCH];
        CheckActionResult results[BUSTER_CHECK_BATCH];
        for (uint32_t i = 0; i < batch->count; i += 1)
        {
            check_graph_action(graph, stream, batch->nodes[i], 0, &actions[i]);
            actions[i].guards = batch->misses[i].keyed ? &batch->misses[i].guards : NULL;
            const bool vector = check_action_vector_eligible(types, &actions[i]);
            stream->statistics.vector_nodes += (uint32_t)vector;
            stream->statistics.scalar_nodes += (uint32_t)!vector;
        }
        check_actions(types, actions, results, batch->count);
        for (uint32_t i = 0; i < batch->count; i += 1)
        {
            check_graph_store(stream, batch->nodes[i], &results[i]);
            check_transition_missed(table, graph, stream, batch->nodes[i], batch->misses + i, NULL, NULL);
        }
        stream->statistics.batches += 1;
    }
    batch->count = 0;
}

// check_graph_run's vector path with its memoized transitions, in node order (`marks`, the run's purity marks): a leaf
// without its action, a hit by its entry, a pure miss or fold into the batch, and any other node by its action, after the
// batch. False, with nothing written, where the scratch arena has no room for the table.
static bool check_graph_transitions(TypeTable* types, const CheckGraph* graph, CheckStream* stream, const uint8_t* marks, CheckValue* arguments, Arena* scratch,
                                    uint32_t batch_size, bool verify)
{
    CheckTransitions table;
    const uint32_t count = graph->count;
    const uint64_t start = scratch->length;
    uint32_t* words = arena_push_array(scratch, (uint64_t)BUSTER_CHECK_TRANSITION_ARGUMENTS * BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS, sizeof(uint32_t), 64);
    CheckTransitionBatch* batch = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckTransitionBatch, 1);
    const bool result = words != NULL && batch != NULL && check_transitions_initialize(&table, scratch, count);
    if (!result)
    {
        arena_pop_to(scratch, start);
    }
    else
    {
        batch->count = 0;
    }
    uint32_t vector_member = 0;
    // The counts, kept here rather than through `stream`, which the columns' stores could alias.
    uint32_t hits = 0;
    uint32_t misses = 0;
    uint32_t folds = 0;
    for (uint32_t node = 0; result && node < count; node += 1)
    {
        if (check_graph_is_leaf(graph, node))
        {
            check_graph_leaf(types, graph, stream, node);
        }
        else
        {
            const bool member = vector_member < graph->vector_member_count && graph->vector_members[vector_member].node == node;
            vector_member += member;
            const bool eligible = check_transition_node(graph, stream, node, member);
            CheckTransitionMiss miss;
            CheckTransitionClass class = check_transition_key(graph, stream, batch, node, eligible, miss.key, miss.objects, words);
            if (class == CHECK_TRANSITION_WAIT)
            {
                check_transition_flush(types, graph, stream, &table, batch, arguments, scratch);
                class = check_transition_key(graph, stream, batch, node, eligible, miss.key, miss.objects, words);
            }
            miss.keyed = class == CHECK_TRANSITION_KEYED;
            miss.hash = 0;
            miss.existing = BUSTER_CHECK_NONE;
            miss.guards = (CheckActionGuards){ 0 };
            bool hit = false;
            if (miss.keyed)
            {
                miss.hash = check_transition_hash(miss.key);
                miss.existing = check_transitions_find(&table, miss.key, miss.hash, words, miss.key[BUSTER_CHECK_KEY_HEADER] >> 18);
                hit = miss.existing != BUSTER_CHECK_NONE && check_transition_guards_hold(graph, &table, table.entries + miss.existing, node);
            }
            hits += hit;
            misses += miss.keyed && !hit;
            folds += class == CHECK_TRANSITION_FOLD;
            if (hit)
            {
                check_transition_write(stream, graph, &table, miss.existing, node, miss.objects, NULL);
                if (verify)
                {
                    check_transition_verify(types, graph, stream, node, arguments, scratch, NULL);
                }
            }
            else if ((marks[node] & BUSTER_CHECK_MARK_PURE) != 0 && batch_size > 1)
            {
                batch->nodes[batch->count] = node;
                batch->misses[batch->count] = miss;
                batch->count += 1;
                if (batch->count == batch_size)
                {
                    check_transition_flush(types, graph, stream, &table, batch, arguments, scratch);
                }
            }
            else
            {
                check_transition_flush(types, graph, stream, &table, batch, arguments, scratch);
                check_graph_guarded_node(types, graph, stream, node, arguments, scratch, false, miss.keyed ? &miss.guards : NULL);
                check_transition_missed(&table, graph, stream, node, &miss, words, NULL);
            }
        }
    }
    if (result)
    {
        check_transition_flush(types, graph, stream, &table, batch, arguments, scratch);
    }
    stream->statistics.transition_hits += hits;
    stream->statistics.transition_misses += misses;
    stream->statistics.transition_folds += folds;
    stream->statistics.vector_nodes += hits;
    stream->statistics.transition_entries = result ? table.count : 0;
    return result;
}

// The windows' state (CheckOptions.transition_windows): a window of up to 64 nodes, the positions done, pending, the table
// takes, probed and folds (an operand's payload), and each probed miss's key and the rest it keeps, by position.
typedef struct CheckTransitionWindow CheckTransitionWindow;
struct CheckTransitionWindow
{
    CheckTransitionMiss misses[BUSTER_CHECK_TRANSITION_HORIZON];
    // A call's argument words, BUSTER_CHECK_TRANSITION_ARGUMENTS of them per position.
    uint32_t* argument_words;
    uint32_t first;
    uint64_t done;
    uint64_t pending;
    uint64_t eligible;
    uint64_t probed;
    uint64_t folds;
};

static uint32_t* check_transition_words(const CheckTransitionWindow* window, uint32_t position)
{
    return window->argument_words + (uint64_t)position * BUSTER_CHECK_TRANSITION_ARGUMENTS * BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS;
}

// The positions of `candidates` whose operands, and a call's arguments, are complete: before the window, or done in it.
// Sixteen positions at a time, each operand's place in the window tested against `done`'s bits.
static uint64_t check_transition_ready(const CheckGraph* graph, const CheckTransitionWindow* window, uint64_t candidates)
{
    const uint32_t first = window->first;
    const __m512i base = _mm512_set1_epi32((int)first);
    const __m512i none = _mm512_set1_epi32((int)BUSTER_CHECK_NONE);
    const __m512i low = _mm512_set1_epi32((int)(uint32_t)window->done);
    const __m512i high = _mm512_set1_epi32((int)(uint32_t)(window->done >> 32));
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i thirty_two = _mm512_set1_epi32(32);
    const uint32_t* columns[3] = { graph->operand0, graph->operand1, graph->operand2 };
    uint64_t ready = 0;
    for (uint32_t chunk = 0; chunk < BUSTER_CHECK_TRANSITION_HORIZON / BUSTER_CHECK_TRANSITION_LANES; chunk += 1)
    {
        const __mmask16 active = (__mmask16)(candidates >> (chunk * BUSTER_CHECK_TRANSITION_LANES));
        __mmask16 blocked = 0;
        for (uint32_t i = 0; active != 0 && i < 3; i += 1)
        {
            const __m512i operand = _mm512_maskz_loadu_epi32(active, columns[i] + first + chunk * BUSTER_CHECK_TRANSITION_LANES);
            const __mmask16 inside = _mm512_mask_cmpge_epu32_mask(active, operand, base) & _mm512_cmpneq_epi32_mask(operand, none);
            const __m512i place = _mm512_sub_epi32(operand, base);
            const __m512i bits = _mm512_or_si512(_mm512_srlv_epi32(low, place), _mm512_srlv_epi32(high, _mm512_sub_epi32(place, thirty_two)));
            blocked |= _mm512_mask_testn_epi32_mask(inside, bits, one);
        }
        ready |= (uint64_t)(uint16_t)(active & (__mmask16)~blocked) << (chunk * BUSTER_CHECK_TRANSITION_LANES);
    }
    for (uint64_t calls = ready; calls != 0; calls &= calls - 1)
    {
        const uint32_t position = (uint32_t)__builtin_ctzll(calls);
        const uint32_t node = first + position;
        if (graph->ops[node] == CHECK_OP_CALL)
        {
            const uint32_t offset = graph->argument_offsets[node];
            bool complete = true;
            for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
            {
                const uint32_t argument = graph->argument_nodes[offset + i];
                complete &= argument < first || (window->done >> (argument - first) & 1) != 0;
            }
            ready &= complete ? UINT64_MAX : ~(UINT64_C(1) << position);
        }
    }
    return ready;
}

// One batch of up to 16 of the window's ready positions (`positions`, `count` of them): their keys and hashes in vector
// lanes, their operands' columns gathered by node, and a call's arguments one call at a time (check_transition_arguments);
// then each lane's key probed and compared whole. A hit writes its answer and returns in the result's bits by position; a
// miss keeps its key for its turn; an operand's or argument's payload makes a fold. check_transition_key's twin.
static uint64_t check_transition_batch(TypeTable* types, const CheckGraph* graph, CheckStream* stream, const CheckTransitions* table, CheckTransitionWindow* window,
                                       const uint32_t* positions, uint32_t count, uint32_t* shapes, CheckValue* arguments, Arena* scratch, bool verify)
{
    const uint32_t first = window->first;
    const __mmask16 active = (__mmask16)((1u << count) - 1);
    const __m512i zero = _mm512_setzero_si512();
    const __m512i none = _mm512_set1_epi32((int)BUSTER_CHECK_NONE);
    const __m512i place = _mm512_maskz_loadu_epi32(active, positions);
    const __m512i nodes = _mm512_add_epi32(place, _mm512_set1_epi32((int)first));
    _Alignas(64) uint32_t keys[BUSTER_CHECK_TRANSITION_WORDS][BUSTER_CHECK_TRANSITION_LANES];
    _Alignas(64) uint32_t objects[3][BUSTER_CHECK_TRANSITION_LANES];
    _Alignas(64) uint32_t hashes[BUSTER_CHECK_TRANSITION_LANES];
    const uint32_t* columns[3] = { graph->operand0, graph->operand1, graph->operand2 };
    __m512i operand_shapes[3];
    __m512i operand_objects[3];
    __mmask16 folds = 0;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        const __m512i operand = _mm512_mask_i32gather_epi32(none, active, nodes, (const int*)columns[i], 4);
        const __mmask16 present = _mm512_mask_cmpneq_epi32_mask(active, operand, none);
        _mm512_store_si512(keys[BUSTER_CHECK_KEY_TYPES + i], _mm512_mask_i32gather_epi32(zero, present, operand, (const int*)stream->value_types, 4));
        _mm512_store_si512(keys[BUSTER_CHECK_KEY_FLAGS + i], _mm512_mask_i32gather_epi32(zero, present, operand, (const int*)stream->value_flags, 4));
        operand_objects[i] = _mm512_mask_i32gather_epi32(zero, present, operand, (const int*)stream->value_objects, 4);
        operand_shapes[i] = _mm512_mask_i32gather_epi32(zero, present, operand, (const int*)shapes, 4);
        folds |= _mm512_mask_cmpeq_epi32_mask(present, operand_shapes[i], none);
        _mm512_store_si512(objects[i], operand_objects[i]);
    }
    // The objects' pattern (check_transition_pattern).
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i two = _mm512_set1_epi32(2);
    const __m512i p0 = _mm512_maskz_mov_epi32(_mm512_test_epi32_mask(operand_objects[0], operand_objects[0]), one);
    __m512i p1 = _mm512_mask_mov_epi32(two, _mm512_cmpeq_epi32_mask(operand_objects[1], operand_objects[0]), one);
    p1 = _mm512_maskz_mov_epi32(_mm512_test_epi32_mask(operand_objects[1], operand_objects[1]), p1);
    __m512i p2 = _mm512_mask_mov_epi32(_mm512_set1_epi32(3), _mm512_cmpeq_epi32_mask(operand_objects[2], operand_objects[1]), two);
    p2 = _mm512_mask_mov_epi32(p2, _mm512_cmpeq_epi32_mask(operand_objects[2], operand_objects[0]), one);
    p2 = _mm512_maskz_mov_epi32(_mm512_test_epi32_mask(operand_objects[2], operand_objects[2]), p2);
    const __m512i pattern = _mm512_or_si512(p0, _mm512_or_si512(_mm512_slli_epi32(p1, 2), _mm512_slli_epi32(p2, 4)));
    const __m512i ops = _mm512_mask_i32gather_epi32(zero, active, nodes, (const int*)graph->ops, 4);
    const __mmask16 calls = _mm512_mask_cmpeq_epi32_mask(active, ops, _mm512_set1_epi32(CHECK_OP_CALL));
    const __m512i counts = _mm512_mask_i32gather_epi32(zero, calls, nodes, (const int*)graph->argument_counts, 4);
    // The floating environment, a byte per node: the window's bytes, each lane's picked by its position.
    __m512i environment = zero;
    if (graph->fp_environments != NULL)
    {
        const uint32_t span = graph->count - first < BUSTER_CHECK_TRANSITION_HORIZON ? graph->count - first : BUSTER_CHECK_TRANSITION_HORIZON;
        const __m512i bytes = _mm512_maskz_loadu_epi8(span == 64 ? UINT64_MAX : (UINT64_C(1) << span) - 1, graph->fp_environments + first);
        const __m512i picked = _mm512_permutexvar_epi8(_mm512_zextsi128_si512(_mm512_cvtepi32_epi8(place)), bytes);
        environment = _mm512_maskz_cvtepu8_epi32(active, _mm512_castsi512_si128(picked));
    }
    __m512i extensions = _mm512_set1_epi32((int)graph->extensions);
    for (uint32_t lanes = graph->system_tokens != NULL ? (uint32_t)active : 0; lanes != 0; lanes &= lanes - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(lanes);
        const uint32_t node = first + positions[lane];
        if (check_graph_system_token(graph, graph->tokens != NULL ? graph->tokens[node] : BUSTER_CHECK_NONE))
        {
            extensions = _mm512_mask_or_epi32(extensions, (__mmask16)(1u << lane), extensions, _mm512_set1_epi32((int)BUSTER_CHECK_EXTENSION_SYSTEM_HEADER));
        }
    }
    __m512i header = _mm512_or_si512(ops, _mm512_slli_epi32(extensions, 8));
    header = _mm512_or_si512(header, _mm512_slli_epi32(environment, 11));
    header = _mm512_or_si512(header, _mm512_slli_epi32(pattern, 12));
    header = _mm512_or_si512(header, _mm512_slli_epi32(counts, 18));
    _mm512_store_si512(keys[BUSTER_CHECK_KEY_HEADER], header);
    _mm512_store_si512(keys[BUSTER_CHECK_KEY_MEMBER], _mm512_mask_i32gather_epi32(zero, active, nodes, (const int*)graph->members, 4));
    _mm512_store_si512(keys[BUSTER_CHECK_KEY_REQUESTED], _mm512_mask_i32gather_epi32(zero, active, nodes, (const int*)graph->types, 4));
    const __m512i low = _mm512_set1_epi32(0xffff);
    _mm512_store_si512(keys[BUSTER_CHECK_KEY_SHAPES], _mm512_or_si512(_mm512_and_si512(operand_shapes[0], low), _mm512_slli_epi32(operand_shapes[1], 16)));
    _mm512_store_si512(keys[BUSTER_CHECK_KEY_SHAPE2], _mm512_and_si512(operand_shapes[2], low));
    for (uint32_t w = BUSTER_CHECK_KEY_ARGUMENTS; w < BUSTER_CHECK_TRANSITION_WORDS; w += 1)
    {
        _mm512_store_si512(keys[w], zero);
    }
    // A call's arguments, one call at a time.
    for (uint32_t lanes = (uint32_t)(calls & (__mmask16)~folds); lanes != 0; lanes &= lanes - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(lanes);
        if (check_transition_arguments(graph, stream, NULL, first + positions[lane], check_transition_words(window, positions[lane]),
                                       &keys[BUSTER_CHECK_KEY_ARGUMENTS][lane], &keys[BUSTER_CHECK_KEY_ARGUMENTS + 1][lane]) != CHECK_TRANSITION_KEYED)
        {
            folds |= (__mmask16)(1u << lane);
        }
    }
    // The hashes (check_transition_hash); a fold's key is zero.
    __m512i sum = zero;
    for (uint32_t w = 0; w < BUSTER_CHECK_TRANSITION_WORDS; w += 1)
    {
        const __m512i words = _mm512_maskz_mov_epi32((__mmask16)~folds, _mm512_load_si512(keys[w]));
        _mm512_store_si512(keys[w], words);
        sum = _mm512_add_epi32(sum, _mm512_mullo_epi32(words, _mm512_set1_epi32((int)check_transition_multipliers[w])));
    }
    sum = _mm512_xor_si512(sum, _mm512_srli_epi32(sum, 15));
    sum = _mm512_mullo_epi32(sum, _mm512_set1_epi32(0x2c1b3c6d));
    sum = _mm512_xor_si512(sum, _mm512_srli_epi32(sum, 12));
    sum = _mm512_mullo_epi32(sum, _mm512_set1_epi32(0x297a2d39));
    sum = _mm512_xor_si512(sum, _mm512_srli_epi32(sum, 15));
    _mm512_store_si512(hashes, _mm512_or_si512(sum, one));
    // Each keyed lane: its key whole (a column of `keys`), its entry, and a hit's answer or a miss's key kept.
    const __m512i column = _mm512_mullo_epi32(_mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), _mm512_set1_epi32(BUSTER_CHECK_TRANSITION_LANES));
    uint64_t hits = 0;
    for (uint32_t lanes = (uint32_t)(active & (__mmask16)~folds); lanes != 0; lanes &= lanes - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(lanes);
        const uint32_t position = positions[lane];
        const uint32_t node = first + position;
        CheckTransitionMiss* miss = window->misses + position;
        _mm512_store_si512(miss->key, _mm512_i32gather_epi32(_mm512_add_epi32(column, _mm512_set1_epi32((int)lane)), (const int*)keys, 4));
        miss->hash = hashes[lane];
        miss->objects[0] = objects[0][lane];
        miss->objects[1] = objects[1][lane];
        miss->objects[2] = objects[2][lane];
        miss->existing = check_transitions_find(table, miss->key, miss->hash, check_transition_words(window, position), keys[BUSTER_CHECK_KEY_HEADER][lane] >> 18);
        miss->keyed = true;
        miss->guards = (CheckActionGuards){ 0 };
        if (miss->existing != BUSTER_CHECK_NONE && check_transition_guards_hold(graph, table, table->entries + miss->existing, node))
        {
            check_transition_write(stream, graph, table, miss->existing, node, miss->objects, shapes);
            if (verify)
            {
                check_transition_verify(types, graph, stream, node, arguments, scratch, shapes);
            }
            hits |= UINT64_C(1) << position;
        }
    }
    for (uint32_t lanes = (uint32_t)(active & folds); lanes != 0; lanes &= lanes - 1)
    {
        const uint32_t position = positions[__builtin_ctz(lanes)];
        window->misses[position].keyed = false;
        window->folds |= UINT64_C(1) << position;
    }
    return hits;
}

// The window's lowest pending positions whose turn has come: a miss, a fold or a node the table doesn't take, run by its
// action in node order. Consecutive pure ones that read none of each other go as one batch of check_actions; a node not
// yet probed ends the turn.
static void check_transition_front(TypeTable* types, const CheckGraph* graph, CheckStream* stream, CheckTransitions* table, CheckTransitionWindow* window,
                                   const uint8_t* marks, uint32_t* shapes, CheckValue* arguments, Arena* scratch, uint32_t batch_size)
{
    const uint64_t settled = window->probed | ~window->eligible;
    bool open = window->pending != 0;
    while (open)
    {
        const uint32_t position = (uint32_t)__builtin_ctzll(window->pending);
        const uint32_t node = window->first + position;
        open = (settled >> position & 1) != 0;
        uint32_t members[BUSTER_CHECK_BATCH];
        uint32_t count = 0;
        uint64_t batch = 0;
        if (open && (marks[node] & BUSTER_CHECK_MARK_PURE) != 0 && batch_size > 1)
        {
            uint64_t candidates = window->pending;
            bool joining = true;
            while (joining && candidates != 0 && count < batch_size)
            {
                const uint32_t next = (uint32_t)__builtin_ctzll(candidates);
                const uint32_t member = window->first + next;
                const uint32_t operands[3] = { graph->operand0[member], graph->operand1[member], graph->operand2[member] };
                joining = (marks[member] & BUSTER_CHECK_MARK_PURE) != 0 && (settled >> next & 1) != 0;
                for (uint32_t i = 0; joining && i < 3; i += 1)
                {
                    joining = operands[i] == BUSTER_CHECK_NONE || operands[i] < window->first || (batch >> (operands[i] - window->first) & 1) == 0;
                }
                if (joining)
                {
                    members[count++] = next;
                    batch |= UINT64_C(1) << next;
                    candidates &= candidates - 1;
                }
            }
        }
        if (count > 1)
        {
            CheckAction actions[BUSTER_CHECK_BATCH];
            CheckActionResult results[BUSTER_CHECK_BATCH];
            for (uint32_t i = 0; i < count; i += 1)
            {
                CheckTransitionMiss* miss = window->misses + members[i];
                check_graph_action(graph, stream, window->first + members[i], 0, &actions[i]);
                actions[i].guards = (window->probed >> members[i] & 1) != 0 && miss->keyed ? &miss->guards : NULL;
                const bool vector = check_action_vector_eligible(types, &actions[i]);
                stream->statistics.vector_nodes += (uint32_t)vector;
                stream->statistics.scalar_nodes += (uint32_t)!vector;
            }
            check_actions(types, actions, results, count);
            for (uint32_t i = 0; i < count; i += 1)
            {
                CheckTransitionMiss* miss = window->misses + members[i];
                miss->keyed &= (window->probed >> members[i] & 1) != 0;
                check_graph_store(stream, window->first + members[i], &results[i]);
                check_transition_missed(table, graph, stream, window->first + members[i], miss, NULL, shapes);
            }
            stream->statistics.batches += 1;
        }
        else if (open)
        {
            CheckTransitionMiss* miss = window->misses + position;
            miss->keyed &= (window->probed >> position & 1) != 0;
            check_graph_guarded_node(types, graph, stream, node, arguments, scratch, false, miss->keyed ? &miss->guards : NULL);
            check_transition_missed(table, graph, stream, node, miss, check_transition_words(window, position), shapes);
            batch = UINT64_C(1) << position;
        }
        window->pending &= ~batch;
        window->done |= batch;
        open = open && window->pending != 0;
    }
}

// The windows' path (CheckOptions.transition_windows), check_graph_transitions' alternative: every node in windows of 64
// (`marks`, the run's purity marks), a window's leaves first, then its other nodes in rounds, each probing its ready nodes in
// batches of 16 (check_transition_batch) and running the nodes whose turn has come (check_transition_front). Its batches
// gather their operands' shapes from a column of them (`shapes`, check_transition_shape's for each node done), where node
// order reads each from the node's own columns. False, with nothing written, where the scratch arena has no room for the
// table.
static bool check_graph_transition_windows(TypeTable* types, const CheckGraph* graph, CheckStream* stream, const uint8_t* marks, CheckValue* arguments, Arena* scratch,
                                           uint32_t batch_size, bool verify)
{
    CheckTransitions table;
    const uint32_t count = graph->count;
    const uint64_t start = scratch->length;
    uint32_t* shapes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    CheckTransitionWindow* window = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckTransitionWindow, 1);
    uint32_t* words = window != NULL ? arena_push_array(scratch, (uint64_t)BUSTER_CHECK_TRANSITION_HORIZON * BUSTER_CHECK_TRANSITION_ARGUMENTS *
                                                                  BUSTER_CHECK_TRANSITION_ARGUMENT_WORDS, sizeof(uint32_t), 64) : NULL;
    const bool result = shapes != NULL && words != NULL && check_transitions_initialize(&table, scratch, count);
    if (!result)
    {
        arena_pop_to(scratch, start);
    }
    uint32_t vector_member = 0;
    for (uint32_t first = 0; result && first < count; first += BUSTER_CHECK_TRANSITION_HORIZON)
    {
        const uint32_t span = count - first < BUSTER_CHECK_TRANSITION_HORIZON ? count - first : BUSTER_CHECK_TRANSITION_HORIZON;
        // The window's vector members (CheckGraph.vector_members, in node order), which the table doesn't take.
        uint64_t members = 0;
        while (vector_member < graph->vector_member_count && graph->vector_members[vector_member].node < first + span)
        {
            const uint32_t node = graph->vector_members[vector_member].node;
            members |= node >= first ? UINT64_C(1) << (node - first) : 0;
            vector_member += 1;
        }
        window->argument_words = words;
        window->first = first;
        window->done = 0;
        window->pending = 0;
        window->eligible = 0;
        window->probed = 0;
        window->folds = 0;
        for (uint32_t position = 0; position < span; position += 1)
        {
            const uint32_t node = first + position;
            window->misses[position].keyed = false;
            if (check_graph_is_leaf(graph, node))
            {
                check_graph_leaf(types, graph, stream, node);
                shapes[node] = check_transition_shape(stream, node);
                window->done |= UINT64_C(1) << position;
            }
            else
            {
                window->pending |= UINT64_C(1) << position;
                window->eligible |= (uint64_t)check_transition_node(graph, stream, node, (members >> position & 1) != 0) << position;
            }
        }
        while (window->pending != 0)
        {
            const uint64_t ready = check_transition_ready(graph, window, window->pending & window->eligible & ~window->probed);
            uint64_t remaining = ready;
            while (remaining != 0)
            {
                uint32_t positions[BUSTER_CHECK_TRANSITION_LANES];
                uint32_t lanes = 0;
                for (; remaining != 0 && lanes < BUSTER_CHECK_TRANSITION_LANES; remaining &= remaining - 1)
                {
                    positions[lanes++] = (uint32_t)__builtin_ctzll(remaining);
                }
                const uint64_t folded = window->folds;
                const uint64_t hits = check_transition_batch(types, graph, stream, &table, window, positions, lanes, shapes, arguments, scratch, verify);
                const uint32_t hit_count = (uint32_t)__builtin_popcountll(hits);
                const uint32_t fold_count = (uint32_t)__builtin_popcountll(window->folds & ~folded);
                stream->statistics.transition_hits += hit_count;
                stream->statistics.transition_folds += fold_count;
                stream->statistics.transition_misses += lanes - hit_count - fold_count;
                stream->statistics.vector_nodes += hit_count;
                window->pending &= ~hits;
                window->done |= hits;
            }
            window->probed |= ready;
            check_transition_front(types, graph, stream, &table, window, marks, shapes, arguments, scratch, batch_size);
        }
    }
    stream->statistics.transition_entries = result ? table.count : 0;
    return result;
}

#endif
