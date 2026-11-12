// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Memoized type transitions (#1050): with CheckOptions.transitions (the driver's --check-transitions; docs/c-checker.md,
// "Memoized transitions"), the vector path of check_graph_run checks most nodes by an earlier node's answer rather than by
// its regions.
//
// Each completed node's value has a class: a 32-bit id of its type, flags, category, constant kind and payload (every field
// of a value but its object), interned exactly on its first sight (check_transitions_intern). An ordinary action is a
// function of a small key: its operator, member, requested type, extensions and floating environment, its operands' classes
// and which of them have an object and which share one (the objects' pattern), and a call's arguments' classes, each with
// whether it has an object. The key holds no object and no node: an action copies objects and reads them only for zero and
// equality (outside a constant evaluation, which isn't memoized), and reads a node only as a new fault's place, while an
// operand with a fault or an overload set has no class and an answer with one isn't stored. The first node of a key runs its
// action (a miss) and stores its answer, with its value's class; a later node of the key (a hit) writes that answer, its
// object taken from the operand the answer names, and its class, with no CheckAction, no CheckActionResult and no interning.
// Every type a hit's answer names was interned by its key's first node, so the misses, in node order, intern what the scalar
// path does in the order it does. An answer that read a tag's completion at its source position keeps the answer as a guard,
// which a hit asks again at its own position (CheckAction.guards); one that read a transparent union's activation, a constant
// evaluation's, a vector member's or an overload set's isn't memoized.
//
// The nodes go in node order (check_graph_transitions), after a pass that gives each node its static word: leaf, a node the
// table doesn't take, or its key's header (check_transition_statics). A node's key is four words built in registers from
// its header and its operands' classes, hashed and probed once, its entry's key compared, and a hit's answer written; the
// loop holds only leaves and those hits, and every other node goes out of line (check_transition_other), so that the loop
// keeps its values in registers. A run of consecutive pure misses that read none of each other goes as one batch of
// check_actions, whose integer folds go 8 at a time, as the regions' batches did; their classes wait as
// BUSTER_CHECK_CLASS_PENDING until it runs. The scalar path (--scalar, the node-by-node loop without transitions) is the
// answers' oracle, and CheckOptions.verify_transitions checks every hit against its action.
// Include from c_checker.c after check_graph_guarded_node.
#ifndef BUSTER_C_CHECK_TRANSITIONS_INCLUDED
#define BUSTER_C_CHECK_TRANSITIONS_INCLUDED

// The most arguments a memoized call has.
#define BUSTER_CHECK_TRANSITION_ARGUMENTS 64u
#define BUSTER_CHECK_TRANSITION_SMALLEST 16u
#define BUSTER_CHECK_TRANSITION_FALLBACK 1024u
// A value's class: 0 for an absent operand, 1 up to BUSTER_CHECK_CLASS_LIMIT for an interned value, BUSTER_CHECK_CLASS_UNKEYED
// for a value no key takes (a fault, an overload set, or one the classes had no room for), and BUSTER_CHECK_CLASS_PENDING
// for a node waiting in the batch.
#define BUSTER_CHECK_CLASS_LIMIT (1u << 30)
#define BUSTER_CHECK_CLASS_UNKEYED UINT32_MAX
#define BUSTER_CHECK_CLASS_PENDING (UINT32_MAX - 1)
// A class slot's shape for a value with a payload row: this bit and its template's index.
#define BUSTER_CHECK_CLASS_PAYLOAD (1u << 31)
// The key's first word: the operator, extensions << 8, the floating environment << 11, the objects' pattern << 12, the
// arguments' count << 18 and this bit, so that a header is neither static word's other value, with the member in its high
// half.
#define BUSTER_CHECK_KEY_VALID (1u << 31)

// How a node's key came out: memoized, a node the table doesn't take, an operand or argument without a class (a fold), or
// an operand waiting in the batch, which must run first.
typedef enum CheckTransitionKind
{
    CHECK_TRANSITION_KEYED,
    CHECK_TRANSITION_NONE,
    CHECK_TRANSITION_FOLD,
    CHECK_TRANSITION_WAIT,
} CheckTransitionKind;

// An interned class in its open-addressed slot: the value's type, flags, and shape (its category | constant kind << 8,
// or BUSTER_CHECK_CLASS_PAYLOAD | its template's index), and its id, 0 for an empty slot.
typedef struct CheckTransitionClassSlot CheckTransitionClassSlot;
struct CheckTransitionClassSlot
{
    TypeId type;
    uint32_t flags;
    uint32_t shape;
    uint32_t id;
};

// An entry: its key and what a hit reads first in one cache line, its conversions and member offset in the next. The answer
// is the node's value but its object, as its class, type, flags, a payload's template (1 + its index, or 0) and in
// `packed` its category and constant kind (16 bits), the operand whose object it takes (2 bits: 1 + its index, or 0 for
// none), its diagnostic (6 bits) and the diagnostic's operand (8 bits); and its warnings. A single guard is `guard`, the tag
// plus one with the answer in bit 31; `extra`, 1 + an index into the extras, holds a call's arguments and more guards.
typedef struct CheckTransitionEntry CheckTransitionEntry;
struct CheckTransitionEntry
{
    _Alignas(64) uint64_t key[4];
    uint32_t value_class;
    TypeId type;
    uint32_t flags;
    uint32_t packed;
    uint32_t warnings;
    uint32_t guard;
    uint32_t extra;
    uint32_t payload;
    CheckNodeConversions conversions;
    uint64_t member_offset;
};

_Static_assert(sizeof(CheckTransitionEntry) == 128, "an entry is two cache lines, its key's and its conversions'");

// What an answer has besides its entry: a call's arguments (from `arguments`), and two guards or more.
typedef struct CheckTransitionExtra CheckTransitionExtra;
struct CheckTransitionExtra
{
    uint32_t arguments;
    uint32_t guard_count;
    uint32_t guard_answers;
    uint32_t guard_tags[BUSTER_CHECK_ACTION_GUARDS];
};

// A memoized call's argument: its class with whether it has an object in bit 31, and its conversion.
typedef struct CheckTransitionArgument CheckTransitionArgument;
struct CheckTransitionArgument
{
    uint32_t word;
    CheckConversion conversion;
};

// A graph run's table, in its scratch arena: open-addressed slots, each an entry's index << 32 and its hash's high half
// (whose low bit is set, so that 0 is an empty slot), the entries in insertion order, with their extras and calls'
// arguments; and the classes' open-addressed slots, with their payloads' templates. `full` stops insertion once an array
// can't grow within `limit`, and `classes_full` new classes.
typedef struct CheckTransitions CheckTransitions;
struct CheckTransitions
{
    Arena* arena;
    uint64_t* slots;
    uint32_t mask;
    CheckTransitionEntry* entries;
    uint32_t count;
    uint32_t capacity;
    CheckTransitionExtra* extras;
    uint32_t extra_count;
    uint32_t extra_capacity;
    CheckTransitionArgument* arguments;
    uint32_t argument_count;
    uint32_t argument_capacity;
    CheckTransitionClassSlot* class_slots;
    uint32_t class_mask;
    uint32_t class_count;
    CheckValue* templates;
    uint32_t template_count;
    uint32_t template_capacity;
    // The arena's length past which the table doesn't grow: a quarter of the room the run's scratch had left, so that the
    // table never takes what the run's other stages need.
    uint64_t limit;
    bool full;
    bool classes_full;
};

// A miss's key, hash, operands' objects and guards, kept for its insertion once its action has run.
typedef struct CheckTransitionMiss CheckTransitionMiss;
struct CheckTransitionMiss
{
    uint64_t key[4];
    uint64_t hash;
    uint32_t objects[3];
    bool keyed;
    CheckActionGuards guards;
};

// The batch of consecutive pure misses that read none of each other, which the node order runs as one call of check_actions
// once a node reads a member (whose class is BUSTER_CHECK_CLASS_PENDING), a node that isn't a pure miss comes, or the batch
// fills. Its members are in node order, and as pure nodes, none is a call.
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

// The node order's static words (check_transition_statics), one a node: BUSTER_CHECK_TRANSITION_OTHER for a node the table
// doesn't take, BUSTER_CHECK_TRANSITION_LEAF for a VALUE (a leaf, check_graph_is_leaf, unless it has an overload set), and
// otherwise the key's first word but its objects' pattern and member (BUSTER_CHECK_KEY_VALID among its bits).
#define BUSTER_CHECK_TRANSITION_OTHER 0u
#define BUSTER_CHECK_TRANSITION_LEAF 1u

_Static_assert(sizeof(CheckOp) == sizeof(uint32_t), "the static words' vector path loads 16 operators at a time");

// One node's static word: the table takes a node by its operator, without a constant evaluation, a plain library builtin's
// fold or more arguments than a key holds; its key's first word has its operator, extensions (with the system-header bit
// from its token), floating environment and a call's argument count. The vector path's exact twin.
static uint32_t check_transition_static(const CheckGraph* graph, const CheckStream* stream, uint32_t node)
{
    const CheckOp op = graph->ops[node];
    const bool call = op == CHECK_OP_CALL;
    const uint32_t count = call ? graph->argument_counts[node] : 0;
    const bool eligible = check_transition_op(op) && (stream->constant_evaluated == NULL || stream->constant_evaluated[node] == 0) &&
                          (!call || ((graph->members[node] & BUSTER_CHECK_LIBRARY_CALL) == 0 && count <= BUSTER_CHECK_TRANSITION_ARGUMENTS));
    const uint32_t token = graph->tokens != NULL ? graph->tokens[node] : BUSTER_CHECK_NONE;
    const uint32_t extensions = graph->extensions | (check_graph_relaxed_token(graph, token) ? BUSTER_CHECK_EXTENSION_SYSTEM_HEADER : 0);
    const uint32_t environment = graph->fp_environments != NULL ? graph->fp_environments[node] : 0;
    const uint32_t header = (uint32_t)op | extensions << 8 | environment << 11 | count << 18 | BUSTER_CHECK_KEY_VALID;
    return op == CHECK_OP_VALUE ? BUSTER_CHECK_TRANSITION_LEAF : eligible ? header : BUSTER_CHECK_TRANSITION_OTHER;
}

// Each node's static word before the node order, which no node's answer changes (check_transition_static), and a vector
// member's (CheckGraph.vector_members) BUSTER_CHECK_TRANSITION_OTHER. The vector path takes 16 nodes at a time: their
// operators, a call's argument count and member by masked loads, the system-header bit by a gather of the 4 bytes from each
// token's own, where they lie within the tokens' array (and one at a time where they don't); the scalar path (`scalar`) a
// node at a time, its twin.
static void check_transition_statics(const CheckGraph* graph, const CheckStream* stream, uint32_t* statics, bool scalar)
{
    (void)scalar;
    const uint32_t count = graph->count;
    uint32_t node = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && count - node >= 16; node += 16)
    {
        const __m512i op = _mm512_loadu_si512(graph->ops + node);
        const __mmask16 call = _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(CHECK_OP_CALL));
        __mmask16 eligible = (_mm512_cmpge_epu32_mask(op, _mm512_set1_epi32(CHECK_OP_POSITIVE)) & _mm512_cmple_epu32_mask(op, _mm512_set1_epi32(CHECK_OP_SELECT))) |
                             _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(CHECK_OP_REAL)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(CHECK_OP_IMAGINARY));
        if (stream->constant_evaluated != NULL)
        {
            eligible &= _mm_cmpeq_epi8_mask(_mm_loadu_si128((const __m128i*)(stream->constant_evaluated + node)), _mm_setzero_si128());
        }
        const __m512i arguments = _mm512_maskz_loadu_epi32(call, graph->argument_counts + node);
        const __m512i member = _mm512_maskz_loadu_epi32(call, graph->members + node);
        eligible &= (__mmask16)~(_mm512_test_epi32_mask(member, _mm512_set1_epi32((int)BUSTER_CHECK_LIBRARY_CALL)) |
                                 _mm512_cmpgt_epu32_mask(arguments, _mm512_set1_epi32(BUSTER_CHECK_TRANSITION_ARGUMENTS)));
        const uint32_t system_count = graph->system_tokens != NULL ? graph->system_token_count : 0;
        __mmask16 system = 0;
        if (graph->tokens != NULL && system_count != 0)
        {
            const __m512i token = _mm512_loadu_si512(graph->tokens + node);
            const __mmask16 inside = system_count >= 4 ? _mm512_cmplt_epu32_mask(token, _mm512_set1_epi32((int)(system_count - 3))) : 0;
            const __m512i bytes = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), inside, token, graph->system_tokens, 1);
            system = _mm512_mask_test_epi32_mask(inside, bytes, _mm512_set1_epi32(0xff));
            for (uint32_t lanes = (uint32_t)(__mmask16)~inside; lanes != 0; lanes &= lanes - 1)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(lanes);
                system |= (__mmask16)((uint32_t)check_graph_relaxed_token(graph, graph->tokens[node + lane]) << lane);
            }
        }
        const __m512i environment = graph->fp_environments != NULL ? _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(graph->fp_environments + node))) :
                                    _mm512_setzero_si512();
        const __m512i extensions = _mm512_mask_or_epi32(_mm512_set1_epi32((int)graph->extensions), system, _mm512_set1_epi32((int)graph->extensions),
                                                        _mm512_set1_epi32(BUSTER_CHECK_EXTENSION_SYSTEM_HEADER));
        const __m512i header = _mm512_or_si512(_mm512_or_si512(op, _mm512_slli_epi32(extensions, 8)),
                                               _mm512_or_si512(_mm512_or_si512(_mm512_slli_epi32(environment, 11), _mm512_slli_epi32(arguments, 18)),
                                                               _mm512_set1_epi32((int)BUSTER_CHECK_KEY_VALID)));
        const __m512i words = _mm512_mask_mov_epi32(_mm512_maskz_mov_epi32(eligible, header), _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(CHECK_OP_VALUE)),
                                                    _mm512_set1_epi32(BUSTER_CHECK_TRANSITION_LEAF));
        _mm512_storeu_si512(statics + node, words);
    }
    for (; node < count; node += 1)
    {
        statics[node] = check_transition_static(graph, stream, node);
    }
    for (uint32_t i = 0; i < graph->vector_member_count; i += 1)
    {
        const uint32_t member = graph->vector_members[i].node;
        if (member < count && statics[member] != BUSTER_CHECK_TRANSITION_LEAF)
        {
            statics[member] = BUSTER_CHECK_TRANSITION_OTHER;
        }
    }
}

static uint64_t check_transition_mix(uint64_t hash)
{
    hash ^= hash >> 32;
    hash *= 0xd6e8feb86659fd93u;
    hash ^= hash >> 32;
    return hash;
}

// A key's hash: each word times its odd multiplier, summed, then mixed.
static uint64_t check_transition_hash(const uint64_t* key)
{
    return check_transition_mix(key[0] * 0x9e3779b97f4a7c15u + key[1] * 0xc2b2ae3d27d4eb4fu + key[2] * 0x165667b19e3779f9u + key[3] * 0x27d4eb2f165667c5u);
}

// A class's hash, from its type, flags and shape (a payload's: BUSTER_CHECK_CLASS_PAYLOAD) and a payload's own hash (or 0).
static uint64_t check_transition_class_hash(TypeId type, uint32_t flags, uint32_t shape, uint64_t payload)
{
    return check_transition_mix(((uint64_t)type | (uint64_t)flags << 32) * 0x9e3779b97f4a7c15u + ((uint64_t)shape ^ payload) * 0xc2b2ae3d27d4eb4fu);
}

// A payload's hash: every field a reader reads but its type, flags and object (a long double by its ten bytes).
static uint64_t check_transition_payload_hash(const CheckValue* value)
{
    return (uint64_t)value->integer * 0x165667b19e3779f9u + (uint64_t)(value->integer >> 64) * 0x27d4eb2f165667c5u +
           value->floating_storage[0] * 0x85ebca77c2b2ae63u + (value->floating_storage[1] & 0xffff) * 0xff51afd7ed558ccdu +
           value->imaginary_storage[0] * 0xc4ceb9fe1a85ec53u + (value->imaginary_storage[1] & 0xffff) * 0x9e3779b97f4a7c15u +
           (uint64_t)value->offset * 0xc2b2ae3d27d4eb4fu + ((uint64_t)value->bit_width | (uint64_t)value->object_bit_offset << 32) * 0xd6e8feb86659fd93u +
           ((uint64_t)value->category | (uint64_t)value->constant_kind << 32) * 0x94d049bb133111ebu;
}

// Whether two values without a fault or an overload set are of one class: every field a reader reads but the object.
static bool check_transition_template_equal(const CheckValue* a, const CheckValue* b)
{
    return a->type == b->type && a->category == b->category && a->flags == b->flags && a->constant_kind == b->constant_kind && a->integer == b->integer &&
           a->floating_storage[0] == b->floating_storage[0] && (a->floating_storage[1] & 0xffff) == (b->floating_storage[1] & 0xffff) &&
           a->imaginary_storage[0] == b->imaginary_storage[0] && (a->imaginary_storage[1] & 0xffff) == (b->imaginary_storage[1] & 0xffff) &&
           a->offset == b->offset && a->bit_width == b->bit_width && a->object_bit_offset == b->object_bit_offset;
}

// The objects' pattern of three operands' objects: two bits each, 0 for none, 1 + the first operand of the same object.
static uint32_t check_transition_pattern(const uint32_t* objects)
{
    const uint32_t p0 = objects[0] != 0;
    const uint32_t p1 = objects[1] == 0 ? 0 : objects[1] == objects[0] ? 1 : 2;
    const uint32_t p2 = objects[2] == 0 ? 0 : objects[2] == objects[0] ? 1 : objects[2] == objects[1] ? 2 : 3;
    return p0 | p1 << 2 | p2 << 4;
}

// Whether `bytes` more, and an alignment's padding, keep the table's arena within its limit.
static bool check_transitions_fit(const CheckTransitions* table, uint64_t bytes)
{
    return table->arena->length <= table->limit && bytes + 128 <= table->limit - table->arena->length;
}

// The table, with room for an entry per 16 of the graph's `nodes` (sqlite3.c's bodies took one per 12) and a class's slot
// per 32 (they took a class per 43), the slots at most half full, and for at least BUSTER_CHECK_TRANSITION_SMALLEST
// entries, so that it seldom grows, whose copies would take as much memory again: an array's pages are only touched as its
// entries fill them, and a small graph's table is small.
// Where that much doesn't fit within the limit, room for at most BUSTER_CHECK_TRANSITION_FALLBACK, then for
// BUSTER_CHECK_TRANSITION_SMALLEST; false where even that doesn't.
static bool check_transitions_initialize(CheckTransitions* table, Arena* arena, uint32_t nodes)
{
    *table = (CheckTransitions){
        .arena = arena,
        .limit = arena->length + (arena->capacity > arena->length ? (arena->capacity - arena->length) / 4 : 0),
    };
    uint32_t capacity = BUSTER_CHECK_TRANSITION_SMALLEST;
    while (capacity < nodes / 16 && capacity < (1u << 22))
    {
        capacity *= 2;
    }
    bool result = false;
    bool trying = true;
    while (trying)
    {
        const uint64_t slots = (uint64_t)capacity * 2;
        const uint64_t classes = capacity / 2 < 64 ? 64 : capacity / 2;
        const uint64_t bytes = slots * sizeof(uint64_t) + classes * sizeof(CheckTransitionClassSlot) + (uint64_t)capacity * sizeof(CheckTransitionEntry) + 256;
        result = check_transitions_fit(table, bytes);
        if (result)
        {
            table->slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, slots);
            table->class_slots = arena_push_array(arena, classes, sizeof(CheckTransitionClassSlot), 64);
            table->entries = arena_push_array(arena, capacity, sizeof(CheckTransitionEntry), 128);
            result = table->slots != NULL && table->class_slots != NULL && table->entries != NULL;
        }
        if (result)
        {
            memset(table->slots, 0, slots * sizeof(uint64_t));
            memset(table->class_slots, 0, classes * sizeof(CheckTransitionClassSlot));
            table->mask = (uint32_t)slots - 1;
            table->class_mask = (uint32_t)classes - 1;
            table->capacity = capacity;
        }
        trying = !result && capacity > BUSTER_CHECK_TRANSITION_SMALLEST;
        capacity = capacity > BUSTER_CHECK_TRANSITION_FALLBACK ? BUSTER_CHECK_TRANSITION_FALLBACK : BUSTER_CHECK_TRANSITION_SMALLEST;
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

// Room for one more entry, and the slots at most half full: rehashed from the entries' keys when they double.
static bool check_transitions_grow(CheckTransitions* table)
{
    bool result = check_transitions_room(table, (void**)&table->entries, &table->capacity, table->count + 1, sizeof(CheckTransitionEntry), 128);
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
                const uint64_t hash = check_transition_hash(table->entries[i].key);
                uint32_t slot = (uint32_t)hash & mask;
                while (slots[slot] != 0)
                {
                    slot = (slot + 1) & mask;
                }
                slots[slot] = (uint64_t)i << 32 | (uint32_t)(hash >> 32) | 1;
            }
            table->slots = slots;
            table->mask = mask;
        }
    }
    return result;
}

// A class slot's hash, for a rehash: its fields', or its template's.
static uint64_t check_transitions_class_slot_hash(const CheckTransitions* table, const CheckTransitionClassSlot* slot)
{
    const bool payload = (slot->shape & BUSTER_CHECK_CLASS_PAYLOAD) != 0;
    return check_transition_class_hash(slot->type, slot->flags, payload ? BUSTER_CHECK_CLASS_PAYLOAD : slot->shape,
                                       payload ? check_transition_payload_hash(table->templates + (slot->shape & ~BUSTER_CHECK_CLASS_PAYLOAD)) : 0);
}

// A new class of hash `hash` in an empty slot, its payload's template (or null) kept: its id, or BUSTER_CHECK_CLASS_UNKEYED
// where the classes can't grow within the table's limit, which stops new classes. The slots stay at most half full,
// rehashed when they double.
static uint32_t check_transitions_add_class(CheckTransitions* table, uint64_t hash, TypeId type, uint32_t flags, uint32_t shape, const CheckValue* payload,
                                            uint32_t* template)
{
    bool room = !table->classes_full && table->class_count + 1 < BUSTER_CHECK_CLASS_LIMIT;
    if (room && (uint64_t)(table->class_count + 1) * 2 > (uint64_t)table->class_mask + 1)
    {
        const uint64_t count = ((uint64_t)table->class_mask + 1) * 2;
        CheckTransitionClassSlot* slots = check_transitions_fit(table, count * sizeof(CheckTransitionClassSlot)) ?
                                          arena_push_array(table->arena, count, sizeof(CheckTransitionClassSlot), 64) : NULL;
        room = slots != NULL;
        if (room)
        {
            memset(slots, 0, count * sizeof(CheckTransitionClassSlot));
            const uint32_t mask = (uint32_t)count - 1;
            for (uint32_t i = 0; i <= table->class_mask; i += 1)
            {
                const CheckTransitionClassSlot* old = table->class_slots + i;
                uint32_t slot = old->id != 0 ? (uint32_t)check_transitions_class_slot_hash(table, old) & mask : 0;
                while (old->id != 0 && slots[slot].id != 0)
                {
                    slot = (slot + 1) & mask;
                }
                if (old->id != 0)
                {
                    slots[slot] = *old;
                }
            }
            table->class_slots = slots;
            table->class_mask = mask;
        }
    }
    room = room && (payload == NULL || check_transitions_room(table, (void**)&table->templates, &table->template_capacity, table->template_count + 1,
                                                              sizeof(CheckValue), _Alignof(CheckValue)));
    table->classes_full |= !room;
    uint32_t result = BUSTER_CHECK_CLASS_UNKEYED;
    if (room)
    {
        uint32_t slot = (uint32_t)hash & table->class_mask;
        while (table->class_slots[slot].id != 0)
        {
            slot = (slot + 1) & table->class_mask;
        }
        table->class_count += 1;
        result = table->class_count;
        if (payload != NULL)
        {
            CheckValue kept = *payload;
            kept.object = 0;
            table->templates[table->template_count] = kept;
            *template = table->template_count + 1;
            shape = BUSTER_CHECK_CLASS_PAYLOAD | table->template_count;
            table->template_count += 1;
        }
        table->class_slots[slot] = (CheckTransitionClassSlot){
            .type = type,
            .flags = flags,
            .shape = shape,
            .id = result,
        };
    }
    return result;
}

// A completed node's class, interned on its value's first sight, and its payload's template (1 + its index, or 0) in
// `template`: BUSTER_CHECK_CLASS_UNKEYED for a value with a fault or an overload set, which no key takes, or a new value
// once the classes are full. A value without a payload row is its columns; one with a row is the row, compared whole.
// Always inlined, as the node order's other steps are; hot caller: check_graph_transitions, once per leaf and miss.
static inline __attribute__((always_inline)) uint32_t check_transitions_intern(CheckTransitions* table, const CheckStream* stream, uint32_t node, uint32_t* template)
{
    const uint32_t row = stream->payload_rows[node];
    const CheckValue* payload = row != 0 ? stream->payloads + row - 1 : NULL;
    const TypeId type = stream->value_types[node];
    const uint32_t flags = stream->value_flags[node];
    const uint32_t shape = payload != NULL ? BUSTER_CHECK_CLASS_PAYLOAD : (uint32_t)stream->value_categories[node] | (uint32_t)stream->value_constants[node] << 8;
    const bool keyed = payload == NULL || (payload->fault == CHECK_FAULT_NONE && payload->fault_node == 0 && payload->overload_head == 0);
    const uint64_t hash = check_transition_class_hash(type, flags, shape, payload != NULL ? check_transition_payload_hash(payload) : 0);
    const CheckTransitionClassSlot* slots = table->class_slots;
    const uint32_t mask = table->class_mask;
    uint32_t result = BUSTER_CHECK_CLASS_UNKEYED;
    uint32_t found = 0;
    uint32_t slot = (uint32_t)hash & mask;
    bool searching = keyed;
    while (searching)
    {
        const CheckTransitionClassSlot* candidate = slots + slot;
        searching = candidate->id != 0;
        bool equal = searching && candidate->type == type && candidate->flags == flags;
        if (equal)
        {
            const bool kept = (candidate->shape & BUSTER_CHECK_CLASS_PAYLOAD) != 0;
            const uint32_t index = candidate->shape & ~BUSTER_CHECK_CLASS_PAYLOAD;
            equal = payload == NULL ? candidate->shape == shape : kept && check_transition_template_equal(table->templates + index, payload);
            found = payload != NULL ? index + 1 : 0;
        }
        result = equal ? candidate->id : result;
        searching = searching && !equal;
        slot = (slot + 1) & mask;
    }
    *template = result != BUSTER_CHECK_CLASS_UNKEYED ? found : 0;
    if (keyed && result == BUSTER_CHECK_CLASS_UNKEYED)
    {
        result = check_transitions_add_class(table, hash, type, flags, shape, payload, template);
    }
    return result;
}

// An eligible node's key, hash, operands' objects and a call's argument words, from its operands' and arguments' classes:
// CHECK_TRANSITION_WAIT where one waits in the batch, CHECK_TRANSITION_FOLD where one has no class, otherwise
// CHECK_TRANSITION_KEYED. Always inlined, as the node order's other steps are; hot caller: check_graph_transitions, once per
// eligible node.
static inline __attribute__((always_inline)) CheckTransitionKind check_transition_key(const CheckGraph* graph, const CheckStream* stream, const uint32_t* classes,
                                                                                    uint32_t node, uint32_t header, CheckTransitionMiss* miss, uint32_t* argument_words)
{
    const uint32_t* value_objects = stream->value_objects;
    const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    uint32_t operand_classes[3];
    uint32_t operand_objects[3];
    bool wait = false;
    bool fold = false;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        const bool present = operands[i] != BUSTER_CHECK_NONE;
        operand_classes[i] = present ? classes[operands[i]] : 0;
        operand_objects[i] = present ? value_objects[operands[i]] : 0;
        wait |= operand_classes[i] == BUSTER_CHECK_CLASS_PENDING;
        fold |= operand_classes[i] == BUSTER_CHECK_CLASS_UNKEYED;
    }
    const uint32_t count = header >> 18 & 0x7f;
    // A call's arguments: each one's class with whether it has an object, and their hash (FNV-1a's, by words).
    uint64_t arguments = count != 0 ? 0xcbf29ce484222325u : 0;
    const uint32_t offset = count != 0 ? graph->argument_offsets[node] : 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t argument = graph->argument_nodes[offset + i];
        const uint32_t argument_class = classes[argument];
        wait |= argument_class == BUSTER_CHECK_CLASS_PENDING;
        fold |= argument_class == BUSTER_CHECK_CLASS_UNKEYED;
        const uint32_t word = argument_class | (uint32_t)(value_objects[argument] != 0) << 31;
        argument_words[i] = word;
        arguments = (arguments ^ word) * 0x100000001b3u;
    }
    miss->key[0] = (header | check_transition_pattern(operand_objects) << 12) | (uint64_t)graph->members[node] << 32;
    miss->key[1] = graph->types[node] | (uint64_t)operand_classes[0] << 32;
    miss->key[2] = operand_classes[1] | (uint64_t)operand_classes[2] << 32;
    miss->key[3] = arguments;
    miss->hash = check_transition_hash(miss->key);
    for (uint32_t i = 0; i < 3; i += 1)
    {
        miss->objects[i] = operand_objects[i];
    }
    return wait ? CHECK_TRANSITION_WAIT : fold ? CHECK_TRANSITION_FOLD : CHECK_TRANSITION_KEYED;
}

// Whether a node the table doesn't take reads an operand waiting in the batch. Only a pure node joins the batch, and a pure
// node reads its operands alone.
static bool check_transition_waits(const CheckGraph* graph, const uint32_t* classes, uint32_t node)
{
    const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    bool wait = false;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        wait |= operands[i] != BUSTER_CHECK_NONE && classes[operands[i]] == BUSTER_CHECK_CLASS_PENDING;
    }
    return wait;
}

// The entry of `key` (with a call's `argument_words`, `argument_count` arguments' of them), or BUSTER_CHECK_NONE: each slot
// of its hash's run until an empty one, an entry of its hash's high half compared whole.
// Always inlined, as the node order's other steps are; hot caller: check_graph_transitions, once per keyed node.
static inline __attribute__((always_inline)) uint32_t check_transitions_find(const CheckTransitions* table, const uint64_t* key, uint64_t hash,
                                                                            const uint32_t* argument_words, uint32_t argument_count)
{
    const uint64_t k0 = key[0];
    const uint64_t k1 = key[1];
    const uint64_t k2 = key[2];
    const uint64_t k3 = key[3];
    const uint32_t tag = (uint32_t)(hash >> 32) | 1;
    const uint64_t* slots = table->slots;
    const uint32_t mask = table->mask;
    uint32_t found = BUSTER_CHECK_NONE;
    uint32_t slot = (uint32_t)hash & mask;
    bool searching = true;
    while (searching)
    {
        const uint64_t entry = slots[slot];
        searching = entry != 0;
        if (searching && (uint32_t)entry == tag)
        {
            const uint32_t index = (uint32_t)(entry >> 32);
            const CheckTransitionEntry* candidate = table->entries + index;
            // A key holds its call's argument count (its header's), and an entry of arguments has their extra
            // (check_transitions_insert).
            bool equal = ((candidate->key[0] ^ k0) | (candidate->key[1] ^ k1) | (candidate->key[2] ^ k2) | (candidate->key[3] ^ k3)) == 0;
            const CheckTransitionArgument* arguments = equal && argument_count != 0 ? table->arguments + table->extras[candidate->extra - 1].arguments : NULL;
            for (uint32_t i = 0; equal && i < argument_count; i += 1)
            {
                equal = arguments[i].word == argument_words[i];
            }
            found = equal ? index : BUSTER_CHECK_NONE;
            searching = !equal;
        }
        slot = (slot + 1) & mask;
    }
    return found;
}

// Whether an entry's guards hold at the node's source position: each tag complete there exactly where it was.
static bool check_transition_guards_hold(const CheckGraph* graph, const CheckTransitions* table, const CheckTransitionEntry* entry, uint32_t node)
{
    bool hold = true;
    if (entry->guard != 0 || entry->extra != 0)
    {
        const uint32_t position = graph->source_positions != NULL ? graph->source_positions[node] : BUSTER_CHECK_NONE;
        const uint32_t* completions = graph->tag_completions;
        const uint32_t tags = completions != NULL ? graph->tag_completion_count : 0;
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
    }
    return hold;
}

// A hit's value with a payload row: its template with the node's object, through the stream's setter. Out of line, as rare.
static __attribute__((noinline)) void check_transition_write_payload(CheckStream* stream, const CheckTransitions* table, uint32_t payload, uint32_t object, uint32_t node)
{
    CheckValue value = table->templates[payload - 1];
    value.object = object;
    check_stream_set_value(stream, node, &value);
}

// A hit's call's argument conversions. Out of line, as a call's.
static __attribute__((noinline)) void check_transition_write_arguments(CheckStream* stream, const CheckGraph* graph, const CheckTransitions* table, uint32_t extra,
                                                                      uint32_t node)
{
    const uint32_t offset = graph->argument_offsets[node];
    const uint32_t count = graph->argument_counts[node];
    const CheckTransitionArgument* arguments = table->arguments + table->extras[extra - 1].arguments;
    CheckConversion* const argument_conversions = stream->argument_conversions + offset;
    for (uint32_t i = 0; i < count; i += 1)
    {
        argument_conversions[i] = arguments[i].conversion;
    }
}

// A hit: entry `entry`'s answer written as node `node`'s, its object the operand's it names, and its class.
// Always inlined, as the node order's other steps are; hot caller: check_graph_transitions, once per hit.
static inline __attribute__((always_inline)) void check_transition_write(CheckStream* stream, const CheckGraph* graph, const CheckTransitions* table,
                                                                        const CheckTransitionEntry* entry, uint32_t node, const uint32_t* objects, uint32_t* classes)
{
    // The answer's fields and the columns' pointers first, then the stores: a store through a uint32_t pointer could alias a
    // column's pointer or the entry (-fno-strict-aliasing), which the compiler would load again after each.
    const uint32_t packed = entry->packed;
    const uint32_t rule = packed >> 16 & 3;
    const uint32_t object = rule != 0 ? objects[rule - 1] : 0;
    const uint32_t payload = entry->payload;
    const uint32_t value_class = entry->value_class;
    const CheckNodeConversions conversions = entry->conversions;
    const uint64_t member_offset = entry->member_offset;
    const TypeId type = entry->type;
    const uint32_t flags = entry->flags;
    const uint32_t warnings = entry->warnings;
    const uint32_t extra = entry->extra;
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
    if (payload != 0)
    {
        check_transition_write_payload(stream, table, payload, object, node);
    }
    else
    {
        // check_stream_set_value's stores for a value without a payload row, but its row's: check_graph_run's stream has its
        // columns (never a hand-built stream's dense values), clears `payload_rows` before the run and writes each node
        // once, so that the node's row is still zero, as the setter would leave it.
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
    classes[node] = value_class;
    if (extra != 0 && graph->ops[node] == CHECK_OP_CALL && graph->argument_counts[node] != 0)
    {
        check_transition_write_arguments(stream, graph, table, extra, node);
    }
}

// A miss's answer, from the stream its action wrote, with its value's class and template, stored under its key: written
// over the key's entry where it has one (whose guards failed, or a twin's earlier in the same batch), inserted otherwise.
// Not where the action read what no guard holds, ran out of room, gave a value without a class (a fault or an overload
// set), an object of none of its operands, or fields past what an entry packs. A table that can't grow takes no more.
static void check_transitions_insert(CheckTransitions* table, const CheckGraph* graph, const CheckStream* stream, uint32_t node, const CheckTransitionMiss* miss,
                                     const uint32_t* argument_words, uint32_t value_class, uint32_t template, CheckStatistics* statistics)
{
    const uint32_t* objects = miss->objects;
    const uint32_t value_object = stream->value_objects[node];
    const uint32_t object = value_object == 0 ? 0 : value_object == objects[0] ? 1 : value_object == objects[1] ? 2 : value_object == objects[2] ? 3 : 4;
    const CheckDiagnostic diagnostic = stream->diagnostics[node];
    const uint32_t diagnostic_operand = stream->diagnostic_operands[node];
    const CheckActionGuards* guards = &miss->guards;
    const bool memoized = value_class != BUSTER_CHECK_CLASS_UNKEYED && !guards->unknown && diagnostic != CHECK_DIAG_RESOURCE && (uint32_t)diagnostic < 64 &&
                          diagnostic_operand < 256 && object != 4;
    const uint32_t count = graph->ops[node] == CHECK_OP_CALL ? graph->argument_counts[node] : 0;
    const uint32_t existing = memoized ? check_transitions_find(table, miss->key, miss->hash, argument_words, count) : BUSTER_CHECK_NONE;
    const bool extra = count != 0 || guards->count > 1;
    bool insert = memoized && !table->full && (existing != BUSTER_CHECK_NONE || check_transitions_grow(table));
    insert = insert && (!extra || check_transitions_room(table, (void**)&table->extras, &table->extra_capacity, table->extra_count + 1, sizeof(CheckTransitionExtra),
                                                         _Alignof(CheckTransitionExtra)));
    insert = insert && (count == 0 || check_transitions_room(table, (void**)&table->arguments, &table->argument_capacity, table->argument_count + count,
                                                             sizeof(CheckTransitionArgument), _Alignof(CheckTransitionArgument)));
    table->full |= memoized && !insert;
    if (insert)
    {
        const uint32_t index = existing != BUSTER_CHECK_NONE ? existing : table->count;
        CheckTransitionEntry* entry = table->entries + index;
        const uint32_t shape = (uint32_t)stream->value_categories[node] | (uint32_t)stream->value_constants[node] << 8;
        memcpy(entry->key, miss->key, sizeof(entry->key));
        entry->value_class = value_class;
        entry->type = stream->value_types[node];
        entry->flags = stream->value_flags[node];
        entry->packed = shape | object << 16 | (uint32_t)diagnostic << 18 | diagnostic_operand << 24;
        entry->warnings = stream->warnings[node];
        entry->guard = guards->count == 1 ? (guards->tags[0] + 1) | (guards->answers & 1) << 31 : 0;
        entry->extra = 0;
        entry->payload = template;
        entry->conversions = stream->conversions[node];
        entry->member_offset = stream->member_offsets[node];
        if (extra)
        {
            CheckTransitionExtra* record = table->extras + table->extra_count;
            *record = (CheckTransitionExtra){ 0 };
            table->extra_count += 1;
            entry->extra = table->extra_count;
            if (count != 0)
            {
                const uint32_t offset = graph->argument_offsets[node];
                record->arguments = table->argument_count;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    table->arguments[table->argument_count + i] = (CheckTransitionArgument){
                        .word = argument_words[i],
                        .conversion = stream->argument_conversions[offset + i],
                    };
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
            table->count += 1;
            uint32_t slot = (uint32_t)miss->hash & table->mask;
            while (table->slots[slot] != 0)
            {
                slot = (slot + 1) & table->mask;
            }
            table->slots[slot] = (uint64_t)index << 32 | (uint32_t)(miss->hash >> 32) | 1;
        }
        statistics->transition_inserts += 1;
    }
}

// Whether two values are the same, every field a reader reads (a long double by its ten bytes).
static bool check_transition_value_equal(const CheckValue* a, const CheckValue* b)
{
    return check_transition_template_equal(a, b) && a->object == b->object && a->overload_head == b->overload_head && a->fault == b->fault &&
           a->fault_node == b->fault_node;
}

// CheckOptions.verify_transitions: a hit's node checked by its action too, whose answer the stream keeps, against the
// entry's; a mismatch, or a type the action interned, counts (CheckStatistics.transition_mismatches).
static __attribute__((noinline)) void check_transition_verify(TypeTable* types, const CheckGraph* graph, CheckStream* stream, uint32_t node, CheckValue* arguments, Arena* scratch)
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
    const CheckValue checked = check_stream_value(stream, node);
    bool equal = check_transition_value_equal(&value, &checked) && memcmp(&conversions, stream->conversions + node, sizeof(conversions)) == 0 &&
                 diagnostic == stream->diagnostics[node] && diagnostic_operand == stream->diagnostic_operands[node] && warnings == stream->warnings[node] &&
                 member_offset == stream->member_offsets[node] && interned == types->types.count;
    for (uint32_t i = 0; equal && i < count; i += 1)
    {
        equal = argument_conversions[i].type == stream->argument_conversions[offset + i].type &&
                argument_conversions[i].flags == stream->argument_conversions[offset + i].flags;
    }
    stream->statistics.transition_verified += 1;
    stream->statistics.transition_mismatches += !equal;
}

// After a node's action: its class interned, and a keyed miss's answer stored.
static void check_transition_missed(CheckTransitions* table, const CheckGraph* graph, CheckStream* stream, uint32_t* classes, uint32_t node,
                                    const CheckTransitionMiss* miss, const uint32_t* argument_words)
{
    uint32_t template = 0;
    const uint32_t value_class = check_transitions_intern(table, stream, node, &template);
    classes[node] = value_class;
    if (miss != NULL && miss->keyed)
    {
        check_transitions_insert(table, graph, stream, node, miss, argument_words, value_class, template, &stream->statistics);
    }
}

// The batch's members run as one batch of check_actions (one alone as its node), in node order, and stored. A pure node
// reads no argument slice, so a member's insertion needs no argument words.
static __attribute__((noinline)) void check_transition_flush(TypeTable* types, const CheckGraph* graph, CheckStream* stream, CheckTransitions* table, uint32_t* classes,
                                   CheckTransitionBatch* batch, CheckValue* arguments, Arena* scratch)
{
    if (batch->count == 1)
    {
        CheckTransitionMiss* miss = batch->misses;
        check_graph_guarded_node(types, graph, stream, batch->nodes[0], arguments, scratch, false, miss->keyed ? &miss->guards : NULL);
        check_transition_missed(table, graph, stream, classes, batch->nodes[0], miss, NULL);
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
            check_transition_missed(table, graph, stream, classes, batch->nodes[i], batch->misses + i, NULL);
        }
        stream->statistics.batches += 1;
    }
    batch->count = 0;
}

// A node-order run's state beside its table, which its out-of-line steps take.
typedef struct CheckTransitionRun CheckTransitionRun;
struct CheckTransitionRun
{
    TypeTable* types;
    const CheckGraph* graph;
    CheckStream* stream;
    const uint8_t* marks;
    uint32_t* classes;
    CheckValue* arguments;
    Arena* scratch;
    CheckTransitions* table;
    CheckTransitionBatch* batch;
    uint32_t* words;
    uint32_t batch_size;
    uint32_t hits;
    uint32_t misses;
    uint32_t folds;
    // CheckOptions.verify_transitions: every hit's action run too, the loop's first-probe hits' and these.
    bool verify;
};

// Every node but a leaf and a first-probe hit, out of the node order's loop so that the loop keeps its values in
// registers: `kind` and `miss` are the loop's key of an eligible node (`header`, check_transition_statics) and `found` its
// probe, CHECK_TRANSITION_NONE for any other node. A node reading a batch member runs the batch and is keyed again; a hit
// (then a flushed hit) is written and, with `verify`, checked by its action too, a pure miss or other pure node joins the
// batch, and any other node runs its action after the batch.
static __attribute__((noinline)) void check_transition_other(CheckTransitionRun* run, uint32_t node, uint32_t header, CheckTransitionMiss* miss,
                                                            CheckTransitionKind kind, uint32_t found)
{
    const CheckGraph* graph = run->graph;
    CheckStream* stream = run->stream;
    CheckTransitions* table = run->table;
    CheckTransitionBatch* batch = run->batch;
    const bool eligible = header > BUSTER_CHECK_TRANSITION_LEAF;
    kind = !eligible && check_transition_waits(graph, run->classes, node) ? CHECK_TRANSITION_WAIT : kind;
    if (kind == CHECK_TRANSITION_WAIT)
    {
        check_transition_flush(run->types, graph, stream, table, run->classes, batch, run->arguments, run->scratch);
        kind = eligible ? check_transition_key(graph, stream, run->classes, node, header, miss, run->words) : CHECK_TRANSITION_NONE;
        found = kind == CHECK_TRANSITION_KEYED ? check_transitions_find(table, miss->key, miss->hash, run->words, header >> 18 & 0x7f) : BUSTER_CHECK_NONE;
    }
    miss->keyed = kind == CHECK_TRANSITION_KEYED;
    miss->guards = (CheckActionGuards){ 0 };
    const bool hit = found != BUSTER_CHECK_NONE && check_transition_guards_hold(graph, table, table->entries + found, node);
    run->hits += hit;
    run->misses += miss->keyed && !hit;
    run->folds += kind == CHECK_TRANSITION_FOLD;
    if (hit)
    {
        check_transition_write(stream, graph, table, table->entries + found, node, miss->objects, run->classes);
        if (run->verify)
        {
            check_transition_verify(run->types, graph, stream, node, run->arguments, run->scratch);
        }
    }
    else if ((run->marks[node] & BUSTER_CHECK_MARK_PURE) != 0 && run->batch_size > 1)
    {
        batch->nodes[batch->count] = node;
        batch->misses[batch->count] = *miss;
        batch->count += 1;
        run->classes[node] = BUSTER_CHECK_CLASS_PENDING;
        if (batch->count == run->batch_size)
        {
            check_transition_flush(run->types, graph, stream, table, run->classes, batch, run->arguments, run->scratch);
        }
    }
    else
    {
        check_transition_flush(run->types, graph, stream, table, run->classes, batch, run->arguments, run->scratch);
        check_graph_guarded_node(run->types, graph, stream, node, run->arguments, run->scratch, false, miss->keyed ? &miss->guards : NULL);
        check_transition_missed(table, graph, stream, run->classes, node, miss, run->words);
    }
}

// check_graph_run's vector path with its memoized transitions, in node order (`marks`, the run's purity marks): a leaf
// without its action, a hit by its entry, a pure miss or pure node the table doesn't take into the batch, and any other
// node by its action, after the batch; every node's class in `classes` and its static word in `statics`, columns of the
// run's scratch that the run's later stages write before they read. False, with nothing written, where the scratch arena
// has no room for the table. Out of line, so that its loop's values have their own registers.
static __attribute__((noinline)) bool check_graph_transitions(TypeTable* types, const CheckGraph* graph, CheckStream* stream, const uint8_t* marks, uint32_t* classes,
                                                             uint32_t* statics, CheckValue* arguments, Arena* scratch, uint32_t batch_size, bool verify)
{
    CheckTransitions table;
    const uint32_t count = graph->count;
    const uint64_t start = scratch->length;
    uint32_t* words = arena_push_array(scratch, BUSTER_CHECK_TRANSITION_ARGUMENTS, sizeof(uint32_t), 64);
    CheckTransitionBatch* batch = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckTransitionBatch, 1);
    const bool result = words != NULL && classes != NULL && batch != NULL && check_transitions_initialize(&table, scratch, count);
    if (!result)
    {
        arena_pop_to(scratch, start);
    }
    else
    {
        batch->count = 0;
        check_transition_statics(graph, stream, statics, false);
    }
    CheckTransitionRun run = {
        .types = types,
        .graph = graph,
        .stream = stream,
        .marks = marks,
        .classes = classes,
        .arguments = arguments,
        .scratch = scratch,
        .table = &table,
        .batch = batch,
        .words = words,
        .batch_size = batch_size,
        .verify = verify,
    };
    // The loop's hits, kept here rather than through `run` or `stream`, which the columns' stores could alias.
    uint32_t hits = 0;
    for (uint32_t node = 0; result && node < count; node += 1)
    {
        const uint32_t header = statics[node];
        if (header == BUSTER_CHECK_TRANSITION_LEAF && check_graph_leaf_overload_head(graph, node) == 0)
        {
            check_graph_leaf(types, graph, stream, node);
            uint32_t template = 0;
            classes[node] = check_transitions_intern(&table, stream, node, &template);
        }
        else
        {
            CheckTransitionMiss miss;
            CheckTransitionKind kind = CHECK_TRANSITION_NONE;
            uint32_t found = BUSTER_CHECK_NONE;
            bool done = false;
            if (header > BUSTER_CHECK_TRANSITION_LEAF)
            {
                kind = check_transition_key(graph, stream, classes, node, header, &miss, words);
                found = kind == CHECK_TRANSITION_KEYED ? check_transitions_find(&table, miss.key, miss.hash, words, header >> 18 & 0x7f) : BUSTER_CHECK_NONE;
                done = found != BUSTER_CHECK_NONE && check_transition_guards_hold(graph, &table, table.entries + found, node);
            }
            if (done)
            {
                hits += 1;
                check_transition_write(stream, graph, &table, table.entries + found, node, miss.objects, classes);
                if (verify)
                {
                    check_transition_verify(types, graph, stream, node, arguments, scratch);
                }
            }
            else
            {
                check_transition_other(&run, node, header, &miss, kind, found);
            }
        }
    }
    if (result)
    {
        check_transition_flush(types, graph, stream, &table, classes, batch, arguments, scratch);
    }
    hits += run.hits;
    stream->statistics.transition_hits += hits;
    stream->statistics.transition_flushed_hits += run.hits;
    stream->statistics.transition_misses += run.misses;
    stream->statistics.transition_folds += run.folds;
    stream->statistics.vector_nodes += hits;
    stream->statistics.transition_entries = result ? table.count : 0;
    return result;
}

#endif
