// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The #105 experiment's scalar fragment store and reduction sink. Included by c_parser.c after ParseRole when
// BUSTER_PARSE_FRAGMENT_SINK is enabled. The normal parser build contains neither these arrays nor the hooks.
// The scalar parser transfers completed children here when its precedence stack resolves their parent. No finished
// ParseTree or postorder is read by the producer. c_fragments_test.c independently decodes and checks that output.
//
// Hot records have 16 header bytes plus 16, 32 or 48 packed operator bytes. Bit 1 denotes an operator; bit 0 denotes
// a leaf supplied by the cursor. A virtual singleton allocates nothing until mutation. Joins consume both handles,
// copy at most the right boundary record, and retain the old right tail when that side continues. Finished chains
// are immutable. All walks are iterative, with count bounds. Capacity failures invalidate the whole experiment.
//
// The token-identity column and per-token syntax metadata are deliberately separate cold oracle data; their memory
// is reported separately. They use parser-token indices. `sources` maps those indices to expanded-token identities,
// including an explicit many-to-one mapping where the parser normalizes one expanded token into two tokens.

#ifndef BUSTER_C_FRAGMENTS_INCLUDED
#define BUSTER_C_FRAGMENTS_INCLUDED

#include <assert.h>
#include "c_fragment_interleave.c"

#define BUSTER_C_FRAGMENT_NONE UINT32_MAX
#define BUSTER_C_FRAGMENT_POSITIONS 64u
#define BUSTER_C_FRAGMENT_LIST_ARITY 255u

// This namespace is unrelated to lexer kinds or their start-of-line bit. The table gives fixed arity and source
// order: 0 leaf, 1 prefix, 2 infix, 3 postfix, 4 ternary, 5 call/subscript, 6 compound literal. Lists end with END.
#define BUSTER_C_FRAGMENT_KINDS(X) \
    X(IDENTIFIER, 0, 0) \
    X(NUMBER, 0, 0) \
    X(CHARACTER, 0, 0) \
    X(STRING, 0, 0) \
    X(TYPE_NAME, 0, 0) \
    X(TYPE_DEFAULT, 0, 0) \
    X(DESIGNATOR, 0, 0) \
    X(STATEMENT_EXPRESSION, 0, 0) \
    X(POSITIVE, 1, 1) \
    X(NEGATIVE, 1, 1) \
    X(NOT, 1, 1) \
    X(COMPLEMENT, 1, 1) \
    X(DEREFERENCE, 1, 1) \
    X(ADDRESS, 1, 1) \
    X(PRE_INCREMENT, 1, 1) \
    X(PRE_DECREMENT, 1, 1) \
    X(SIZEOF, 1, 1) \
    X(ALIGNOF, 1, 1) \
    X(COUNTOF, 1, 1) \
    X(EXTENSION, 1, 1) \
    X(REAL, 1, 1) \
    X(IMAG, 1, 1) \
    X(LABEL_ADDRESS, 1, 1) \
    X(CAST, 1, 1) \
    X(POST_INCREMENT, 1, 3) \
    X(POST_DECREMENT, 1, 3) \
    X(MULTIPLY, 2, 2) \
    X(DIVIDE, 2, 2) \
    X(REMAINDER, 2, 2) \
    X(ADD, 2, 2) \
    X(SUBTRACT, 2, 2) \
    X(SHIFT_LEFT, 2, 2) \
    X(SHIFT_RIGHT, 2, 2) \
    X(LESS, 2, 2) \
    X(LESS_EQUAL, 2, 2) \
    X(GREATER, 2, 2) \
    X(GREATER_EQUAL, 2, 2) \
    X(EQUAL, 2, 2) \
    X(NOT_EQUAL, 2, 2) \
    X(BIT_AND, 2, 2) \
    X(BIT_XOR, 2, 2) \
    X(BIT_OR, 2, 2) \
    X(LOGICAL_AND, 2, 2) \
    X(LOGICAL_OR, 2, 2) \
    X(ASSIGN, 2, 2) \
    X(ADD_ASSIGN, 2, 2) \
    X(SUBTRACT_ASSIGN, 2, 2) \
    X(MULTIPLY_ASSIGN, 2, 2) \
    X(DIVIDE_ASSIGN, 2, 2) \
    X(REMAINDER_ASSIGN, 2, 2) \
    X(LEFT_ASSIGN, 2, 2) \
    X(RIGHT_ASSIGN, 2, 2) \
    X(AND_ASSIGN, 2, 2) \
    X(XOR_ASSIGN, 2, 2) \
    X(OR_ASSIGN, 2, 2) \
    X(MEMBER, 2, 2) \
    X(POINTER_MEMBER, 2, 2) \
    X(COMMA, 2, 2) \
    X(SEPARATOR, 2, 2) \
    X(TRAILING_SEPARATOR, 1, 3) \
    X(FIELD_DESIGNATOR, 1, 1) \
    X(INDEX_DESIGNATOR, 1, 1) \
    X(RANGE, 2, 2) \
    X(CONDITIONAL, 3, 4) \
    X(OMITTED_CONDITIONAL, 2, 4) \
    X(SUBSCRIPT, 2, 5) \
    X(CALL, BUSTER_C_FRAGMENT_LIST_ARITY, 5) \
    X(GENERIC, BUSTER_C_FRAGMENT_LIST_ARITY, 1) \
    X(BUILTIN, BUSTER_C_FRAGMENT_LIST_ARITY, 1) \
    X(ASSOCIATION, 2, 2) \
    X(COMPOUND_LITERAL, BUSTER_C_FRAGMENT_LIST_ARITY, 6) \
    X(INITIALIZER, BUSTER_C_FRAGMENT_LIST_ARITY, 1) \
    X(STRING_SEQUENCE, BUSTER_C_FRAGMENT_LIST_ARITY, 1) \
    X(END, 0, 0)

typedef enum CFragmentKind
{
#define BUSTER_C_FRAGMENT_ENUM(name, arity, order) C_FRAGMENT_##name,
    BUSTER_C_FRAGMENT_KINDS(BUSTER_C_FRAGMENT_ENUM)
#undef BUSTER_C_FRAGMENT_ENUM
    C_FRAGMENT_KIND_COUNT,
} CFragmentKind;

_Static_assert(C_FRAGMENT_KIND_COUNT < 128, "fragment kinds fit byte permutations");

static const uint8_t c_fragment_arities[] =
{
#define BUSTER_C_FRAGMENT_ARITY(name, arity, order) arity,
    BUSTER_C_FRAGMENT_KINDS(BUSTER_C_FRAGMENT_ARITY)
#undef BUSTER_C_FRAGMENT_ARITY
};

static const uint8_t c_fragment_orders[] =
{
#define BUSTER_C_FRAGMENT_ORDER(name, arity, order) order,
    BUSTER_C_FRAGMENT_KINDS(BUSTER_C_FRAGMENT_ORDER)
#undef BUSTER_C_FRAGMENT_ORDER
};

typedef struct CFragmentHeader CFragmentHeader;
struct CFragmentHeader
{
    uint64_t operator_mask;
    uint32_t next;
    uint8_t used;
    uint8_t operator_count;
    uint16_t reserved;
};

_Static_assert(sizeof(CFragmentHeader) == 16, "fragment header is sixteen bytes");

typedef struct CFragmentHandle CFragmentHandle;
struct CFragmentHandle
{
    uint32_t head;
    uint32_t tail;
    uint32_t positions;
    uint32_t leaf;
    bool frozen;
};

typedef struct CFragmentStatistics CFragmentStatistics;
struct CFragmentStatistics
{
    uint64_t reductions;
    uint64_t copies;
    uint64_t copied_bytes;
    uint64_t identity_bytes_copied;
    uint64_t links;
    uint64_t linked_bytes;
    uint64_t reused;
    uint64_t positions;
    uint64_t operators;
    uint64_t fragments;
    uint64_t position_occupancy[65];
    uint64_t operator_occupancy[49];
    uint32_t peak_live;
};

typedef struct CFragmentStore CFragmentStore;
struct CFragmentStore
{
    uint8_t* records;
    uint32_t* identities;
    uint32_t* free_next;
    uint8_t* live;
    const uint8_t* leaf_kinds;
    uint32_t leaf_count;
    uint32_t capacity;
    uint32_t allocated;
    uint32_t live_count;
    uint32_t free_head;
    uint32_t payload;
    uint32_t stride;
    bool valid;
    CFragmentStatistics statistics;
};

static CFragmentHandle c_fragment_empty(void)
{
    const CFragmentHandle result = {
        .head = BUSTER_C_FRAGMENT_NONE,
        .tail = BUSTER_C_FRAGMENT_NONE,
        .positions = 0,
        .leaf = BUSTER_C_FRAGMENT_NONE,
        .frozen = false,
    };
    return result;
}

static CFragmentHandle c_fragment_leaf(uint32_t token)
{
    CFragmentHandle result = c_fragment_empty();
    result.positions = 1;
    result.leaf = token;
    return result;
}

static CFragmentHeader* c_fragment_header(const CFragmentStore* store, uint32_t index)
{
    return (CFragmentHeader*)(store->records + (uint64_t)index * store->stride);
}

static uint8_t* c_fragment_operators(const CFragmentStore* store, uint32_t index)
{
    return store->records + (uint64_t)index * store->stride + sizeof(CFragmentHeader);
}

static uint32_t* c_fragment_identities(const CFragmentStore* store, uint32_t index)
{
    return store->identities + (uint64_t)index * BUSTER_C_FRAGMENT_POSITIONS;
}

static bool c_fragment_store_initialize(CFragmentStore* store, Arena* arena, uint32_t capacity, uint32_t payload, const uint8_t* kinds, uint32_t tokens)
{
    *store = (CFragmentStore){
        .leaf_kinds = kinds,
        .leaf_count = tokens,
        .capacity = capacity,
        .free_head = BUSTER_C_FRAGMENT_NONE,
        .payload = payload,
        .stride = payload + sizeof(CFragmentHeader),
        .valid = payload == 16 || payload == 32 || payload == 48,
    };
    if (store->valid)
    {
        store->records = arena_push_array(arena, capacity, store->stride, _Alignof(CFragmentHeader));
        store->identities = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)capacity * BUSTER_C_FRAGMENT_POSITIONS);
        store->free_next = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
        store->live = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, capacity);
        store->valid = store->records != 0 && store->identities != 0 && store->free_next != 0 && store->live != 0;
        if (store->valid)
        {
            memset(store->live, 0, capacity);
        }
    }
    return store->valid;
}

static bool c_fragment_record_valid(const CFragmentStore* store, uint32_t index)
{
    bool result = index < store->allocated && store->live[index] != 0;
    if (result)
    {
        const CFragmentHeader* fragment = c_fragment_header(store, index);
        result = fragment->used != 0 && fragment->used <= 64 && fragment->operator_count <= store->payload &&
                 fragment->operator_count == (uint32_t)__builtin_popcountll(fragment->operator_mask) &&
                 (fragment->operator_mask & ~c_fragment_interleave_mask(fragment->used)) == 0;
    }
    return result;
}

static uint32_t c_fragment_allocate(CFragmentStore* store)
{
    uint32_t result = BUSTER_C_FRAGMENT_NONE;
    if (store->valid && store->free_head != BUSTER_C_FRAGMENT_NONE)
    {
        result = store->free_head;
        store->free_head = store->free_next[result];
        store->statistics.reused += 1;
    }
    else if (store->valid && store->allocated < store->capacity)
    {
        result = store->allocated;
        store->allocated += 1;
    }
    else
    {
        store->valid = false;
    }
    if (result != BUSTER_C_FRAGMENT_NONE)
    {
        *c_fragment_header(store, result) = (CFragmentHeader){ .next = BUSTER_C_FRAGMENT_NONE };
        store->statistics.linked_bytes += sizeof(uint32_t);
        store->live[result] = 1;
        store->live_count += 1;
        store->statistics.peak_live = store->live_count > store->statistics.peak_live ? store->live_count : store->statistics.peak_live;
    }
    return result;
}

static void c_fragment_free(CFragmentStore* store, uint32_t index)
{
    assert(index < store->allocated && store->live[index]);
    *c_fragment_header(store, index) = (CFragmentHeader){ .next = BUSTER_C_FRAGMENT_NONE };
    store->statistics.linked_bytes += sizeof(uint32_t);
    store->live[index] = 0;
    store->live_count -= 1;
    store->free_next[index] = store->free_head;
    store->free_head = index;
}

static bool c_fragment_materialize(CFragmentStore* store, CFragmentHandle* handle)
{
    if (store->valid && handle->positions != 0 && handle->head == BUSTER_C_FRAGMENT_NONE)
    {
        const uint32_t index = c_fragment_allocate(store);
        if (index != BUSTER_C_FRAGMENT_NONE)
        {
            assert(handle->positions == 1 && handle->leaf < store->leaf_count);
            c_fragment_header(store, index)->used = 1;
            c_fragment_identities(store, index)[0] = handle->leaf;
            handle->head = index;
            handle->tail = index;
            handle->leaf = BUSTER_C_FRAGMENT_NONE;
        }
    }
    return store->valid;
}

// Move-only handles. The caller must not retain a copy and later mutate it. A failed operation poisons the store;
// the entire arena is then discarded. Empty handles are identities, including when the other side is virtual.
static CFragmentHandle c_fragment_join(CFragmentStore* store, CFragmentHandle* left, CFragmentHandle* right)
{
    CFragmentHandle result = c_fragment_empty();
    if (left == right || left->frozen || right->frozen || (uint64_t)left->positions + right->positions > UINT32_MAX)
    {
        store->valid = false;
    }
    if (store->valid && (left->positions == 0 || right->positions == 0))
    {
        result = left->positions == 0 ? *right : *left;
    }
    else if (store->valid && c_fragment_materialize(store, left) && c_fragment_materialize(store, right))
    {
        assert(c_fragment_record_valid(store, left->tail) && c_fragment_record_valid(store, right->head));
        CFragmentHeader* a = c_fragment_header(store, left->tail);
        CFragmentHeader* b = c_fragment_header(store, right->head);
        result = *left;
        result.positions += right->positions;
        const uint32_t positions = (uint32_t)a->used + b->used;
        const uint32_t operators = (uint32_t)a->operator_count + b->operator_count;
        if (positions <= BUSTER_C_FRAGMENT_POSITIONS && operators <= store->payload)
        {
            // b is nonempty, so a->used < 64 here. The full-mask case never shifts by 64.
            assert(a->used < 64);
            memcpy(c_fragment_operators(store, left->tail) + a->operator_count, c_fragment_operators(store, right->head), b->operator_count);
            memcpy(c_fragment_identities(store, left->tail) + a->used, c_fragment_identities(store, right->head), (uint64_t)b->used * sizeof(uint32_t));
            store->statistics.identity_bytes_copied += (uint64_t)b->used * sizeof(uint32_t);
            a->operator_mask |= b->operator_mask << a->used;
            a->used = (uint8_t)positions;
            a->operator_count = (uint8_t)operators;
            a->next = b->next;
            store->statistics.linked_bytes += sizeof(a->next);
            result.tail = right->head == right->tail ? left->tail : right->tail;
            store->statistics.copies += 1;
            store->statistics.copied_bytes += b->operator_count;
            c_fragment_free(store, right->head);
        }
        else
        {
            a->next = right->head;
            result.tail = right->tail;
            store->statistics.links += 1;
            store->statistics.linked_bytes += sizeof(a->next);
        }
        assert(c_fragment_record_valid(store, result.tail));
    }
    *left = c_fragment_empty();
    *right = c_fragment_empty();
    return result;
}

static void c_fragment_prepend(CFragmentStore* store, CFragmentHandle* handle, uint8_t kind, uint32_t identity)
{
    if (handle->frozen || kind >= C_FRAGMENT_KIND_COUNT || handle->positions == UINT32_MAX)
    {
        store->valid = false;
    }
    if (store->valid && c_fragment_materialize(store, handle))
    {
        CFragmentHeader* first = handle->head != BUSTER_C_FRAGMENT_NONE ? c_fragment_header(store, handle->head) : 0;
        if (first != 0 && first->used < BUSTER_C_FRAGMENT_POSITIONS && first->operator_count < store->payload)
        {
            uint8_t* operators = c_fragment_operators(store, handle->head);
            uint32_t* identities = c_fragment_identities(store, handle->head);
            memmove(operators + 1, operators, first->operator_count);
            memmove(identities + 1, identities, (uint64_t)first->used * sizeof(uint32_t));
            store->statistics.identity_bytes_copied += (uint64_t)first->used * sizeof(uint32_t);
            operators[0] = kind;
            identities[0] = identity;
            store->statistics.copied_bytes += first->operator_count;
            store->statistics.copies += 1;
            first->operator_count += 1;
            first->used += 1;
            first->operator_mask = (first->operator_mask << 1) | 1;
        }
        else
        {
            const uint32_t index = c_fragment_allocate(store);
            if (index != BUSTER_C_FRAGMENT_NONE)
            {
                CFragmentHeader* fragment = c_fragment_header(store, index);
                fragment->operator_mask = 1;
                fragment->operator_count = 1;
                fragment->used = 1;
                fragment->next = handle->head;
                c_fragment_operators(store, index)[0] = kind;
                c_fragment_identities(store, index)[0] = identity;
                store->statistics.links += handle->head != BUSTER_C_FRAGMENT_NONE;
                store->statistics.linked_bytes += sizeof(fragment->next);
                handle->tail = handle->positions == 0 ? index : handle->tail;
                handle->head = index;
                handle->leaf = BUSTER_C_FRAGMENT_NONE;
            }
        }
        handle->positions += store->valid;
    }
}

static void c_fragment_finish(CFragmentStore* store, CFragmentHandle* handle)
{
    uint32_t walked = 0;
    uint32_t positions = 0;
    uint32_t last = BUSTER_C_FRAGMENT_NONE;
    uint32_t index = handle->head;
    while (store->valid && index != BUSTER_C_FRAGMENT_NONE)
    {
        store->valid = c_fragment_record_valid(store, index) && walked < store->allocated;
        if (store->valid)
        {
            const CFragmentHeader* fragment = c_fragment_header(store, index);
            positions += fragment->used;
            store->statistics.position_occupancy[fragment->used] += 1;
            store->statistics.operator_occupancy[fragment->operator_count] += 1;
            store->statistics.positions += fragment->used;
            store->statistics.operators += fragment->operator_count;
            store->statistics.fragments += 1;
            walked += 1;
            last = index;
            index = fragment->next;
        }
    }
    store->valid &= handle->head == BUSTER_C_FRAGMENT_NONE ? handle->tail == BUSTER_C_FRAGMENT_NONE &&
                    ((handle->positions == 0 && handle->leaf == BUSTER_C_FRAGMENT_NONE) || (handle->positions == 1 && handle->leaf < store->leaf_count)) :
                    positions == handle->positions && last == handle->tail;
    handle->frozen = store->valid;
}

typedef struct CFragmentCursor CFragmentCursor;
struct CFragmentCursor
{
    uint32_t next;
    uint32_t virtual_leaf;
    uint32_t remaining;
    uint32_t visited;
    bool valid;
};

static CFragmentCursor c_fragment_cursor(const CFragmentHandle* handle)
{
    const CFragmentCursor result = {
        .next = handle->head,
        .virtual_leaf = handle->leaf,
        .remaining = handle->positions,
        .valid = handle->frozen,
    };
    return result;
}

// Caller provides exactly 64 writable kind/identity slots; returned live count is 0..64. No readable padding is
// required on the packed operator stream. Only the returned slots are written. Synthetic END has identity NONE.
static bool c_fragment_window(const CFragmentStore* store, CFragmentCursor* cursor, uint8_t* kinds, uint32_t* identities, uint32_t* count, bool vector)
{
    bool result = store->valid && cursor->valid;
    *count = 0;
    if (result && cursor->remaining != 0 && cursor->next == BUSTER_C_FRAGMENT_NONE)
    {
        result = cursor->remaining == 1 && cursor->virtual_leaf < store->leaf_count;
        if (result)
        {
            kinds[0] = store->leaf_kinds[cursor->virtual_leaf];
            identities[0] = cursor->virtual_leaf;
            cursor->remaining = 0;
            *count = 1;
        }
    }
    else if (result && cursor->remaining != 0)
    {
        result = c_fragment_record_valid(store, cursor->next) && cursor->visited < store->allocated;
        if (result)
        {
            const CFragmentHeader* fragment = c_fragment_header(store, cursor->next);
            const uint32_t* ids = c_fragment_identities(store, cursor->next);
            uint8_t leaves[64];
            uint32_t leaf_count = 0;
            result = fragment->used <= cursor->remaining;
            for (uint32_t k = 0; result && k < fragment->used; k += 1)
            {
                if (((fragment->operator_mask >> k) & 1) == 0)
                {
                    result = ids[k] < store->leaf_count;
                    if (result)
                    {
                        leaves[leaf_count] = store->leaf_kinds[ids[k]];
                        leaf_count += 1;
                    }
                }
            }
            if (result)
            {
                result = vector ? c_fragment_interleave(c_fragment_operators(store, cursor->next), leaves, fragment->operator_mask, fragment->used, kinds) :
                                  c_fragment_interleave_scalar(c_fragment_operators(store, cursor->next), leaves, fragment->operator_mask, fragment->used, kinds);
                memcpy(identities, ids, (uint64_t)fragment->used * sizeof(uint32_t));
                *count = fragment->used;
                cursor->next = fragment->next;
                cursor->remaining -= fragment->used;
                cursor->visited += 1;
                result &= cursor->remaining != 0 || cursor->next == BUSTER_C_FRAGMENT_NONE;
            }
        }
    }
    return result;
}

static bool c_fragment_read(const CFragmentStore* store, const CFragmentHandle* handle, uint8_t* kinds, uint32_t* identities, uint32_t capacity, bool vector)
{
    bool result = store->valid && handle->frozen && capacity >= handle->positions;
    CFragmentCursor cursor = c_fragment_cursor(handle);
    uint32_t written = 0;
    while (result && cursor.remaining != 0)
    {
        uint8_t window[64];
        uint32_t ids[64];
        uint32_t count = 0;
        result = c_fragment_window(store, &cursor, window, ids, &count, vector);
        if (result)
        {
            memcpy(kinds + written, window, count);
            memcpy(identities + written, ids, (uint64_t)count * sizeof(uint32_t));
            written += count;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Direct parser reduction sink. A grouping is transparent, type syntax is a postorder-subtree reference, and a
// statement-expression's block remains in postorder. A list has an explicit END operator after its children.

typedef struct CFragmentSyntax CFragmentSyntax;
struct CFragmentSyntax
{
    CFragmentHandle children;
    uint32_t child_count;
    uint32_t first;
    uint32_t last;
    uint32_t group;
    uint32_t source;
    uint32_t auxiliary;
    uint32_t type_root;
    uint32_t parent;
    uint32_t context;
    uint8_t role;
    uint8_t context_role;
    bool expression;
    bool transparent;
    bool emitted;
    bool reduced;
    bool type_group;
};

typedef struct CFragmentExpression CFragmentExpression;
struct CFragmentExpression
{
    CFragmentHandle handle;
    uint32_t root;
};

typedef struct CFragmentSink CFragmentSink;
struct CFragmentSink
{
    CFragmentStore store;
    CFragmentSyntax* syntax;
    uint8_t* kinds;
    const uint8_t* input_kinds;
    const uint32_t* input_names;
    const uint32_t* sources;
    CFragmentExpression* expressions;
    uint32_t token_count;
    uint32_t expression_count;
    uint32_t unsupported;
    uint32_t first_unsupported;
    bool syntax_error;
};

static bool c_fragment_sink_initialize(CFragmentSink* sink, Arena* arena, const uint8_t* input_kinds, const uint32_t* input_names, const uint32_t* sources, uint32_t count, uint32_t payload)
{
    *sink = (CFragmentSink){
        .input_kinds = input_kinds,
        .input_names = input_names,
        .sources = sources,
        .token_count = count,
        .first_unsupported = BUSTER_C_FRAGMENT_NONE,
    };
    sink->syntax = BUSTER_ARENA_PUSH_ARRAY(arena, CFragmentSyntax, (uint64_t)count + 1);
    sink->kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1);
    sink->expressions = BUSTER_ARENA_PUSH_ARRAY(arena, CFragmentExpression, (uint64_t)count + 1);
    bool result = sink->syntax != 0 && sink->kinds != 0 && sink->expressions != 0 && count < UINT32_MAX - 2;
    if (result)
    {
        for (uint32_t k = 0; k <= count; k += 1)
        {
            sink->syntax[k] = (CFragmentSyntax){
                .children = c_fragment_empty(),
                .first = k,
                .last = k,
                .group = BUSTER_C_FRAGMENT_NONE,
                .source = k,
                .auxiliary = BUSTER_C_FRAGMENT_NONE,
                .type_root = BUSTER_C_FRAGMENT_NONE,
                .parent = BUSTER_C_FRAGMENT_NONE,
            };
            sink->kinds[k] = C_FRAGMENT_KIND_COUNT;
        }
        result = c_fragment_store_initialize(&sink->store, arena, count + 2, payload, sink->kinds, count);
    }
    return result;
}

static uint8_t c_fragment_binary_kind(uint8_t kind)
{
    uint8_t result = C_FRAGMENT_KIND_COUNT;
    switch (kind)
    {
        case PARSE_KIND_STAR: result = C_FRAGMENT_MULTIPLY; break;
        case PARSE_KIND_SLASH: result = C_FRAGMENT_DIVIDE; break;
        case PARSE_KIND_PERCENT: result = C_FRAGMENT_REMAINDER; break;
        case PARSE_KIND_PLUS: result = C_FRAGMENT_ADD; break;
        case PARSE_KIND_MINUS: result = C_FRAGMENT_SUBTRACT; break;
        case PARSE_KIND_LESSLESS: result = C_FRAGMENT_SHIFT_LEFT; break;
        case PARSE_KIND_GREATERGREATER: result = C_FRAGMENT_SHIFT_RIGHT; break;
        case PARSE_KIND_LESS: result = C_FRAGMENT_LESS; break;
        case PARSE_KIND_LESSEQUAL: result = C_FRAGMENT_LESS_EQUAL; break;
        case PARSE_KIND_GREATER: result = C_FRAGMENT_GREATER; break;
        case PARSE_KIND_GREATEREQUAL: result = C_FRAGMENT_GREATER_EQUAL; break;
        case PARSE_KIND_EQUALEQUAL: result = C_FRAGMENT_EQUAL; break;
        case PARSE_KIND_EXCLAIMEQUAL: result = C_FRAGMENT_NOT_EQUAL; break;
        case PARSE_KIND_AMP: result = C_FRAGMENT_BIT_AND; break;
        case PARSE_KIND_CARET: result = C_FRAGMENT_BIT_XOR; break;
        case PARSE_KIND_PIPE: result = C_FRAGMENT_BIT_OR; break;
        case PARSE_KIND_AMPAMP: result = C_FRAGMENT_LOGICAL_AND; break;
        case PARSE_KIND_PIPEPIPE: result = C_FRAGMENT_LOGICAL_OR; break;
        case PARSE_KIND_EQUAL: result = C_FRAGMENT_ASSIGN; break;
        case PARSE_KIND_PLUSEQUAL: result = C_FRAGMENT_ADD_ASSIGN; break;
        case PARSE_KIND_MINUSEQUAL: result = C_FRAGMENT_SUBTRACT_ASSIGN; break;
        case PARSE_KIND_STAREQUAL: result = C_FRAGMENT_MULTIPLY_ASSIGN; break;
        case PARSE_KIND_SLASHEQUAL: result = C_FRAGMENT_DIVIDE_ASSIGN; break;
        case PARSE_KIND_PERCENTEQUAL: result = C_FRAGMENT_REMAINDER_ASSIGN; break;
        case PARSE_KIND_LESSLESSEQUAL: result = C_FRAGMENT_LEFT_ASSIGN; break;
        case PARSE_KIND_GREATERGREATEREQUAL: result = C_FRAGMENT_RIGHT_ASSIGN; break;
        case PARSE_KIND_AMPEQUAL: result = C_FRAGMENT_AND_ASSIGN; break;
        case PARSE_KIND_CARETEQUAL: result = C_FRAGMENT_XOR_ASSIGN; break;
        case PARSE_KIND_PIPEEQUAL: result = C_FRAGMENT_OR_ASSIGN; break;
        case PARSE_KIND_PERIOD: result = C_FRAGMENT_MEMBER; break;
        case PARSE_KIND_ARROW: result = C_FRAGMENT_POINTER_MEMBER; break;
        case PARSE_KIND_COMMA: result = C_FRAGMENT_COMMA; break;
        case PARSE_KIND_ELLIPSIS: result = C_FRAGMENT_RANGE; break;
        default: break;
    }
    return result;
}

static uint8_t c_fragment_prefix_kind(uint8_t kind)
{
    uint8_t result = C_FRAGMENT_KIND_COUNT;
    switch (kind)
    {
        case PARSE_KIND_PLUS: result = C_FRAGMENT_POSITIVE; break;
        case PARSE_KIND_MINUS: result = C_FRAGMENT_NEGATIVE; break;
        case PARSE_KIND_EXCLAIM: result = C_FRAGMENT_NOT; break;
        case PARSE_KIND_TILDE: result = C_FRAGMENT_COMPLEMENT; break;
        case PARSE_KIND_STAR: result = C_FRAGMENT_DEREFERENCE; break;
        case PARSE_KIND_AMP: result = C_FRAGMENT_ADDRESS; break;
        case PARSE_KIND_PLUSPLUS: result = C_FRAGMENT_PRE_INCREMENT; break;
        case PARSE_KIND_MINUSMINUS: result = C_FRAGMENT_PRE_DECREMENT; break;
        case PARSE_KIND_SIZEOF: result = C_FRAGMENT_SIZEOF; break;
        case PARSE_KIND_EXTENSION: result = C_FRAGMENT_EXTENSION; break;
        case PARSE_KIND_REAL_IMAG: result = C_FRAGMENT_REAL; break;
        case PARSE_KIND_AMPAMP: result = C_FRAGMENT_LABEL_ADDRESS; break;
        default: break;
    }
    return result;
}

static void c_fragment_sink_group(CFragmentSink* sink, uint32_t opener, uint32_t closer, uint8_t role, bool type)
{
    if (sink != 0 && !sink->syntax_error && closer < sink->token_count && opener < closer)
    {
        sink->syntax[closer].group = opener;
        sink->syntax[closer].type_group = type;
        sink->syntax[opener].role = role;
        sink->syntax[opener].parent = closer;
    }
}

static void c_fragment_sink_token(CFragmentSink* sink, uint32_t token, uint8_t role, bool expression, uint32_t context, uint8_t context_role)
{
    if (sink != 0 && !sink->syntax_error && sink->store.valid)
    {
        CFragmentSyntax* node = &sink->syntax[token];
        const uint8_t input = sink->input_kinds[token];
        const bool group = node->group != BUSTER_C_FRAGMENT_NONE;
        node->role = role;
        node->context = context;
        node->context_role = context_role;
        node->expression = group ? sink->syntax[node->group].expression : expression;
        uint8_t kind = C_FRAGMENT_KIND_COUNT;
        if (node->expression)
        {
            if (role == PARSE_ROLE_OPERAND || role == PARSE_ROLE_MEMBER || role == PARSE_ROLE_STRING_NEXT)
            {
                kind = input == PARSE_KIND_STRING ? C_FRAGMENT_STRING : input == PARSE_KIND_CHARACTER ? C_FRAGMENT_CHARACTER :
                       input == PARSE_KIND_NUMBER ? C_FRAGMENT_NUMBER : C_FRAGMENT_IDENTIFIER;
            }
            else if (role == PARSE_ROLE_PREFIX)
            {
                kind = c_fragment_prefix_kind(input);
                const uint32_t name = sink->input_names[token];
                if (input == PARSE_KIND_SIZEOF && (name == C_NAME_ALIGNOF || name == C_NAME_ALIGNOF_C11 || name == C_NAME_GNU_ALIGNOF || name == C_NAME_GNU_ALIGNOF2))
                {
                    kind = C_FRAGMENT_ALIGNOF;
                }
                else if (input == PARSE_KIND_SIZEOF && name == C_NAME_COUNTOF)
                {
                    kind = C_FRAGMENT_COUNTOF;
                }
                else if (input == PARSE_KIND_REAL_IMAG && (name == C_NAME_GNU_IMAG || name == C_NAME_GNU_IMAG2))
                {
                    kind = C_FRAGMENT_IMAG;
                }
            }
            else if (role == PARSE_ROLE_POSTFIX)
            {
                kind = input == PARSE_KIND_PLUSPLUS ? C_FRAGMENT_POST_INCREMENT : C_FRAGMENT_POST_DECREMENT;
            }
            else if (role >= PARSE_ROLE_MEMBER_OPERATOR && role <= PARSE_ROLE_COMMA)
            {
                kind = c_fragment_binary_kind(input);
            }
            else if (role == PARSE_ROLE_SEPARATOR)
            {
                kind = C_FRAGMENT_SEPARATOR;
            }
            else if (role == PARSE_ROLE_ASSOCIATION)
            {
                kind = C_FRAGMENT_ASSOCIATION;
            }
            else if (role == PARSE_ROLE_CLOSE_GROUP || (role == PARSE_ROLE_CLOSE_SIZEOF && !node->type_group))
            {
                node->transparent = true;
            }
            else if (role == PARSE_ROLE_CLOSE_CAST)
            {
                kind = C_FRAGMENT_CAST;
                node->source = node->group;
            }
            else if (role == PARSE_ROLE_CLOSE_COMPOUND || (role == PARSE_ROLE_CLOSE_SIZEOF && node->type_group))
            {
                kind = C_FRAGMENT_TYPE_NAME;
                node->type_root = token;
                node->source = node->group;
            }
            else if (role == PARSE_ROLE_CLOSE_CALL)
            {
                kind = C_FRAGMENT_CALL;
                node->source = node->group;
            }
            else if (role == PARSE_ROLE_CLOSE_SUBSCRIPT)
            {
                kind = C_FRAGMENT_SUBSCRIPT;
                node->source = node->group;
            }
            else if (role == PARSE_ROLE_CLOSE_TERNARY)
            {
                kind = C_FRAGMENT_CONDITIONAL;
                node->source = node->group;
                node->auxiliary = token;
            }
            else if (role == PARSE_ROLE_CLOSE_KEYWORD && group && node->group > 0)
            {
                const uint32_t keyword = node->group - 1;
                kind = sink->input_kinds[keyword] == PARSE_KIND_GENERIC ? C_FRAGMENT_GENERIC : C_FRAGMENT_BUILTIN;
                node->source = keyword;
            }
            else if (role == PARSE_ROLE_CLOSE_COMPOUND_BODY)
            {
                kind = C_FRAGMENT_COMPOUND_LITERAL;
                node->source = node->group;
            }
            else if (role == PARSE_ROLE_CLOSE_INITIALIZER)
            {
                kind = C_FRAGMENT_INITIALIZER;
                node->source = node->group;
            }
            else if (role == PARSE_ROLE_CLOSE_BLOCK)
            {
                kind = C_FRAGMENT_STATEMENT_EXPRESSION;
                node->type_root = token;
                node->source = node->group;
            }
        }
        sink->kinds[token] = kind;
    }
}

static void c_fragment_sink_publish(CFragmentSink* sink, uint32_t root, CFragmentHandle* handle)
{
    if (handle->positions != 0 && sink->store.valid)
    {
        if (sink->expression_count == sink->token_count)
        {
            sink->store.valid = false;
        }
        else
        {
            c_fragment_finish(&sink->store, handle);
            sink->expressions[sink->expression_count] = (CFragmentExpression){
                .handle = *handle,
                .root = root,
            };
            sink->expression_count += 1;
        }
        *handle = c_fragment_empty();
    }
}

// Both sides are contiguous source subtrees. The parser may resolve a call's arguments before its callee, so a
// transfer can extend either boundary. Interleaving ranges indicate a broken reduction contract, not a slow path.
static void c_fragment_sink_attach(CFragmentSink* sink, uint32_t child, uint32_t parent, CFragmentHandle* handle)
{
    CFragmentSyntax* target = &sink->syntax[parent];
    const CFragmentSyntax* node = &sink->syntax[child];
    if (target->reduced)
    {
        sink->store.valid = false;
    }
    else if (target->children.positions == 0)
    {
        target->children = *handle;
        *handle = c_fragment_empty();
        target->first = node->first;
        target->last = node->last;
    }
    else if (node->last < target->first)
    {
        target->children = c_fragment_join(&sink->store, handle, &target->children);
        target->first = node->first;
    }
    else if (node->first > target->last)
    {
        target->children = c_fragment_join(&sink->store, &target->children, handle);
        target->last = node->last;
    }
    else
    {
        sink->store.valid = false;
    }
    target->child_count += 1;
}

static void c_fragment_sink_reduce(CFragmentSink* sink, uint32_t token, uint32_t parent)
{
    if (sink != 0 && !sink->syntax_error && sink->store.valid)
    {
        CFragmentSyntax* node = &sink->syntax[token];
        uint8_t kind = sink->kinds[token];
        if (parent < sink->token_count && sink->kinds[parent] == C_FRAGMENT_CAST && kind == C_FRAGMENT_KIND_COUNT && node->role != PARSE_ROLE_LEAF_RIGHT &&
            !(node->role >= PARSE_ROLE_OPEN_GROUP && node->role <= PARSE_ROLE_OPEN_CASE))
        {
            // CLOSE_CAST also owns the operand to its right. Retain the actual type child's postorder subtree,
            // rather than mislabeling the closer's larger subtree as a type name.
            sink->syntax[parent].type_root = token;
        }
        // A keyword's type arguments and offsetof's designator stay postorder subtrees, explicitly named as leaves.
        // They are recognized when the complete subtree meets its list boundary, not by rescanning a finished tree.
        const bool keyword_context = node->context_role == PARSE_ROLE_OPEN_KEYWORD && parent < sink->token_count &&
                                     node->context == sink->syntax[parent].context && sink->syntax[parent].expression;
        const bool keyword_boundary = parent < sink->token_count && (sink->syntax[parent].role == PARSE_ROLE_SEPARATOR ||
                                      sink->syntax[parent].role == PARSE_ROLE_ASSOCIATION || sink->syntax[parent].role == PARSE_ROLE_CLOSE_KEYWORD);
        if (kind == C_FRAGMENT_KIND_COUNT && !node->transparent && keyword_context && keyword_boundary &&
            node->role != PARSE_ROLE_LEAF_RIGHT && node->role != PARSE_ROLE_OPEN_KEYWORD)
        {
            kind = sink->input_kinds[token] == PARSE_KIND_DEFAULT ? C_FRAGMENT_TYPE_DEFAULT :
                   node->role == PARSE_ROLE_OPERAND || node->role == PARSE_ROLE_MEMBER_OPERATOR || node->role == PARSE_ROLE_CLOSE_SUBSCRIPT ? C_FRAGMENT_DESIGNATOR : C_FRAGMENT_TYPE_NAME;
            node->type_root = token;
        }
        node->parent = parent;
        assert(!node->reduced);
        node->reduced = true;
        if (kind == C_FRAGMENT_STRING && node->child_count != 0)
        {
            kind = C_FRAGMENT_STRING_SEQUENCE;
        }
        if (kind == C_FRAGMENT_CONDITIONAL && node->group + 1 == token)
        {
            kind = C_FRAGMENT_OMITTED_CONDITIONAL;
        }
        if (kind == C_FRAGMENT_MEMBER && node->child_count == 1)
        {
            kind = C_FRAGMENT_FIELD_DESIGNATOR;
        }
        if (kind == C_FRAGMENT_SUBSCRIPT && node->child_count == 1)
        {
            kind = C_FRAGMENT_INDEX_DESIGNATOR;
        }
        if (kind == C_FRAGMENT_SEPARATOR && node->child_count == 1)
        {
            kind = C_FRAGMENT_TRAILING_SEPARATOR;
        }
        sink->kinds[token] = kind;
        CFragmentHandle handle = node->children;
        node->children = c_fragment_empty();
        if (kind < C_FRAGMENT_KIND_COUNT)
        {
            const uint8_t arity = c_fragment_arities[kind];
            if (arity != BUSTER_C_FRAGMENT_LIST_ARITY && arity != node->child_count)
            {
                sink->unsupported += 1;
                sink->first_unsupported = sink->first_unsupported == BUSTER_C_FRAGMENT_NONE ? token : sink->first_unsupported;
                sink->store.valid = false;
            }
            else if (arity == 0)
            {
                handle = c_fragment_leaf(token);
            }
            else
            {
                if (arity == BUSTER_C_FRAGMENT_LIST_ARITY)
                {
                    CFragmentHandle end = c_fragment_empty();
                    c_fragment_prepend(&sink->store, &end, C_FRAGMENT_END, BUSTER_C_FRAGMENT_NONE);
                    handle = c_fragment_join(&sink->store, &handle, &end);
                }
                c_fragment_prepend(&sink->store, &handle, kind, token);
            }
            node->emitted = true;
            node->first = node->first < token ? node->first : token;
            node->last = node->last > token ? node->last : token;
            sink->store.statistics.reductions += 1;
        }
        else if (node->transparent)
        {
            if (node->child_count != 1)
            {
                sink->unsupported += 1;
                sink->first_unsupported = sink->first_unsupported == BUSTER_C_FRAGMENT_NONE ? token : sink->first_unsupported;
                sink->store.valid = false;
            }
        }
        else if (handle.positions != 0)
        {
            // Non-expression syntax is a publication barrier. Usually children publish as soon as they meet it;
            // this branch catches a classification that changed after a closer identified its group.
            sink->store.valid = false;
        }
        if (handle.positions != 0 && sink->store.valid)
        {
            const bool receiver = parent < sink->token_count &&
                                  (sink->kinds[parent] < C_FRAGMENT_KIND_COUNT || sink->syntax[parent].transparent);
            if (receiver)
            {
                c_fragment_sink_attach(sink, token, parent, &handle);
            }
            else
            {
                c_fragment_sink_publish(sink, token, &handle);
            }
        }
    }
}

#endif
