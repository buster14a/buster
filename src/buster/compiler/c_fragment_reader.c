// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #106 reader experiment, included after c_fragments.c and Parser. c_fragment_reader_source builds an explicitly
// costed source-token index from the recognizer's classifications and ordinary raw parents. It does not replace
// that syntax storage. Leaves advance through source tokens, never through fragment identity slots; only packed
// operators use those slots for locations. B materializes prefix bytes; C keeps one bounded window. The shared
// iterative scheduler emits syntax transitions, not C semantics or IR. REFERENCE and ATTACH deliberately retain
// unresolved type/block and transparent-alias work for the future checker-owned adapter.
//
// Output and scratch arenas must differ. Source metadata and parser/sink inputs outlive all cursors. Every loop
// is count-bounded. Source columns retain 25 bytes/token including spans; temporary columns cost 13 bytes/token,
// before arena alignment. Construction also reads ordinary parents/postorder and recognizer kinds. Syntax and
// expanded-source columns remain borrowed for events. Actual arena deltas, flat bytes/operator locations, visit
// state, pending frames and event output are accounted separately by tests. This is not the final metadata layout.

#ifndef BUSTER_C_FRAGMENT_READER_INCLUDED
#define BUSTER_C_FRAGMENT_READER_INCLUDED

typedef struct CFragmentReaderSpan CFragmentReaderSpan;
struct CFragmentReaderSpan
{
    uint32_t begin;
    uint32_t end;
};

typedef struct CFragmentReaderSource CFragmentReaderSource;
struct CFragmentReaderSource
{
    const Parser* parser;
    const CFragmentSyntax* syntax;
    uint8_t* kinds;
    uint32_t* owners;
    uint32_t* next;
    uint32_t* first;
    uint32_t* boundaries;
    CFragmentReaderSpan* spans;
    uint32_t count;
    uint64_t retained_bytes;
    uint64_t temporary_bytes;
    uint64_t borrowed_bytes;
    uint64_t preparation_input_bytes;
    bool valid;
};

static bool c_fragment_reader_source(CFragmentReaderSource* source, const Parser* parser, const CFragmentSink* sink, Arena* output, Arena* scratch)
{
    *source = (CFragmentReaderSource)
    {
        .parser = parser,
        .syntax = sink->syntax,
        .count = sink->token_count,
    };
    const uint64_t mark = output->length;
    const uint64_t temporary = scratch->length;
    const uint32_t count = source->count;
    bool valid = output != scratch && count == parser->tokens.count && count < UINT32_MAX - 2 && sink->store.valid && !sink->syntax_error && sink->unsupported == 0;
    valid = valid && (count == 0 || (parser->postorder != 0 && parser->tree.parents != 0 && parser->tokens.sources != 0 && sink->kinds != 0 && sink->syntax != 0));
    const uint64_t slots = (uint64_t)count + 1;
    source->kinds = valid ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, slots) : 0;
    source->owners = valid ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, slots) : 0;
    source->next = valid ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, slots) : 0;
    source->first = valid ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, slots) : 0;
    source->boundaries = valid ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, slots) : 0;
    source->spans = valid ? BUSTER_ARENA_PUSH_ARRAY(output, CFragmentReaderSpan, slots) : 0;
    uint32_t* parents = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots) : 0;
    uint32_t* envelopes = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots) : 0;
    uint32_t* work = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots) : 0;
    uint8_t* seen = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, slots) : 0;
    valid &= source->kinds != 0 && source->owners != 0 && source->next != 0 && source->first != 0 && source->boundaries != 0 && source->spans != 0 && parents != 0 && envelopes != 0 && work != 0 && seen != 0;
    if (valid)
    {
        if (count != 0)
        {
            memcpy(source->kinds, sink->kinds, count);
        }
        source->kinds[count] = C_FRAGMENT_KIND_COUNT;
        memset(source->owners, 0xff, slots * sizeof(uint32_t));
        memset(source->next, 0xff, slots * sizeof(uint32_t));
        memset(source->first, 0xff, slots * sizeof(uint32_t));
        memset(source->boundaries, 0xff, slots * sizeof(uint32_t));
        memset(parents, 0xff, slots * sizeof(uint32_t));
        memset(seen, 0, slots);
        for (uint32_t token = 0; token <= count; token += 1)
        {
            source->spans[token] = (CFragmentReaderSpan)
            {
                .begin = token,
                .end = token + 1,
            };
            envelopes[token] = token;
        }
        for (uint32_t order = 0; valid && order < count; order += 1)
        {
            const uint32_t token = parser->postorder[order];
            valid = token < count && !seen[token];
            if (valid)
            {
                seen[token] = 1;
                const uint32_t parent = parser->tree.parents[token];
                valid = parent >= count || !seen[parent];
                if (valid && parent < count)
                {
                    source->spans[parent].begin = source->spans[token].begin < source->spans[parent].begin ? source->spans[token].begin : source->spans[parent].begin;
                    source->spans[parent].end = source->spans[token].end > source->spans[parent].end ? source->spans[token].end : source->spans[parent].end;
                }
            }
        }
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            uint32_t parent = parser->tree.parents[token];
            uint32_t envelope = token;
            uint32_t depth = 0;
            while (valid && parent < count && (sink->syntax[parent].transparent || sink->syntax[parent].bridge))
            {
                // The index side stops before the bridge; its later value can include the bridge envelope.
                if (sink->syntax[parent].bridge && token < parent)
                {
                    parent = count;
                    break;
                }
                envelope = envelopes[parent];
                if (parents[parent] != BUSTER_C_FRAGMENT_NONE)
                {
                    parent = parents[parent];
                    break;
                }
                valid = depth < count;
                if (valid)
                {
                    work[depth++] = parent;
                    parent = parser->tree.parents[parent];
                }
            }
            if (parent < count)
            {
                const uint8_t kind = source->kinds[parent];
                if (kind >= C_FRAGMENT_KIND_COUNT || c_fragment_arities[kind] == 0)
                {
                    parent = count;
                }
            }
            else
            {
                parent = count;
            }
            for (uint32_t i = 0; i < depth; i += 1)
            {
                parents[work[i]] = parent;
                envelopes[work[i]] = envelope;
            }
            parents[token] = parent;
            envelopes[token] = envelope;
        }
        for (uint32_t order = count; valid && order != 0; order -= 1)
        {
            const uint32_t token = parser->postorder[order - 1];
            const uint8_t kind = source->kinds[token];
            if (kind < C_FRAGMENT_KIND_COUNT)
            {
                valid = kind != C_FRAGMENT_END && sink->syntax[token].emitted;
                const uint32_t parent = parents[token];
                const uint32_t root = parent < count ? source->owners[parent] : token;
                valid &= root < count;
                if (valid)
                {
                    source->owners[token] = root;
                    if (parent == count)
                    {
                        source->boundaries[token] = envelopes[token];
                    }
                }
            }
        }
        memset(work, 0xff, slots * sizeof(uint32_t));
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            const uint8_t kind = source->kinds[token];
            if (kind < C_FRAGMENT_KIND_COUNT && c_fragment_arities[kind] == 0)
            {
                const uint32_t root = source->owners[token];
                valid = root < count;
                if (valid)
                {
                    if (work[root] == BUSTER_C_FRAGMENT_NONE)
                    {
                        source->first[root] = token;
                    }
                    else
                    {
                        source->next[work[root]] = token;
                    }
                    work[root] = token;
                }
            }
        }
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            const uint32_t boundary = source->boundaries[token];
            if (boundary < count)
            {
                valid = source->owners[boundary] == BUSTER_C_FRAGMENT_NONE || source->owners[boundary] == token;
                source->owners[boundary] = token;
            }
        }
        source->retained_bytes = output->length - mark;
        source->temporary_bytes = scratch->length - temporary;
        source->borrowed_bytes = (uint64_t)count * (sizeof(CFragmentSyntax) + sizeof(uint32_t));
        source->preparation_input_bytes = (uint64_t)count * (2 * sizeof(uint32_t) + sizeof(uint8_t));
    }
    if (!valid)
    {
        arena_pop_to(output, mark);
    }
    arena_pop_to(scratch, temporary);
    source->valid = valid;
    return valid;
}

typedef struct CFragmentLeafCursor CFragmentLeafCursor;
struct CFragmentLeafCursor
{
    const CFragmentReaderSource* source;
    CFragmentReaderSpan span;
    uint32_t root;
    uint32_t next;
    uint32_t previous;
    uint32_t read;
    bool valid;
};

static CFragmentLeafCursor c_fragment_leaf_cursor(const CFragmentReaderSource* source, uint32_t boundary)
{
    CFragmentLeafCursor cursor =
    {
        .source = source,
        .root = BUSTER_C_FRAGMENT_NONE,
        .next = BUSTER_C_FRAGMENT_NONE,
        .previous = BUSTER_C_FRAGMENT_NONE,
    };
    cursor.valid = source != 0 && source->valid && boundary < source->count;
    if (cursor.valid)
    {
        cursor.root = source->owners[boundary];
        cursor.valid = cursor.root < source->count && source->boundaries[cursor.root] == boundary;
        if (cursor.valid)
        {
            cursor.next = source->first[cursor.root];
            cursor.span = source->spans[boundary];
        }
    }
    return cursor;
}

static bool c_fragment_leaf_next(CFragmentLeafCursor* cursor, uint8_t* kind, uint32_t* token)
{
    const CFragmentReaderSource* source = cursor->source;
    const uint32_t next = cursor->next;
    bool valid = cursor->valid && source != 0 && source->valid && cursor->root < source->count && next < source->count && cursor->read < source->count && next >= cursor->span.begin && next < cursor->span.end;
    valid = valid && (cursor->previous == BUSTER_C_FRAGMENT_NONE || cursor->previous < next) && source->owners[next] == cursor->root;
    if (valid)
    {
        const uint8_t atom = source->kinds[next];
        valid = atom < C_FRAGMENT_KIND_COUNT && atom != C_FRAGMENT_END && c_fragment_arities[atom] == 0;
        if (valid)
        {
            *kind = atom;
            *token = next;
            cursor->previous = next;
            cursor->next = source->next[next];
            cursor->read += 1;
        }
    }
    cursor->valid = valid;
    return valid;
}

// Explicit continuation for a caller that consumes a published expression inside a retained opaque subtree.
// The outer source position is restored only after the child exhausts its own leaves. This does no type lookup.
static bool c_fragment_leaf_enter(CFragmentLeafCursor* cursor, uint32_t subtree, uint32_t boundary, CFragmentLeafCursor* continuations, uint32_t* depth, uint32_t capacity)
{
    const CFragmentReaderSource* source = cursor->source;
    CFragmentLeafCursor child = c_fragment_leaf_cursor(source, boundary);
    bool valid = cursor->valid && child.valid && subtree < source->count && *depth < capacity && continuations != 0;
    if (valid)
    {
        const CFragmentReaderSpan span = source->spans[subtree];
        valid = span.begin >= cursor->span.begin && span.end <= cursor->span.end && child.span.begin >= span.begin && child.span.end <= span.end && child.root != cursor->root;
        if (valid)
        {
            continuations[*depth] = *cursor;
            *depth += 1;
            *cursor = child;
        }
    }
    return valid;
}

static bool c_fragment_leaf_leave(CFragmentLeafCursor* cursor, const CFragmentLeafCursor* continuations, uint32_t* depth, uint32_t capacity)
{
    const bool valid = cursor->valid && cursor->next == BUSTER_C_FRAGMENT_NONE && *depth != 0 && *depth <= capacity && continuations != 0;
    if (valid)
    {
        *depth -= 1;
        *cursor = continuations[*depth];
    }
    return valid;
}

// A raw syntax scan and an opaque-subtree continuation share this ledger. Both must claim before entry and
// finish exactly the active root. A later raw scan skips DONE roots; ACTIVE roots are cycles, not a second visit.
// This tracks syntax ownership only. It does not choose which query/type/body references a checker evaluates.
typedef enum CFragmentReaderVisitState
{
    C_FRAGMENT_READER_UNSEEN,
    C_FRAGMENT_READER_ACTIVE,
    C_FRAGMENT_READER_DONE,
} CFragmentReaderVisitState;

typedef struct CFragmentReaderVisits CFragmentReaderVisits;
struct CFragmentReaderVisits
{
    uint8_t* states;
    uint8_t* nodes;
    uint32_t* roots;
    uint32_t count;
    uint32_t depth;
    uint32_t capacity;
};

static bool c_fragment_reader_claim(CFragmentReaderVisits* visits, const CFragmentLeafCursor* cursor)
{
    const bool valid = cursor->valid && cursor->root < visits->count && visits->depth < visits->capacity && visits->states != 0 && visits->roots != 0 &&
                       visits->states[cursor->root] == C_FRAGMENT_READER_UNSEEN;
    if (valid)
    {
        visits->states[cursor->root] = C_FRAGMENT_READER_ACTIVE;
        visits->roots[visits->depth++] = cursor->root;
    }
    return valid;
}

static bool c_fragment_reader_complete(CFragmentReaderVisits* visits, const CFragmentLeafCursor* cursor)
{
    const bool valid = cursor->valid && cursor->next == BUSTER_C_FRAGMENT_NONE && cursor->root < visits->count && visits->depth != 0 && visits->depth <= visits->capacity && visits->states != 0 && visits->roots != 0 &&
                       visits->roots[visits->depth - 1] == cursor->root && visits->states[cursor->root] == C_FRAGMENT_READER_ACTIVE;
    if (valid)
    {
        visits->states[cursor->root] = C_FRAGMENT_READER_DONE;
        visits->depth -= 1;
    }
    return valid;
}

typedef struct CFragmentDirectReader CFragmentDirectReader;
struct CFragmentDirectReader
{
    const CFragmentStore* store;
    CFragmentLeafCursor leaves;
    uint32_t next;
    uint32_t remaining;
    uint32_t visited;
    uint32_t tail;
    uint32_t last;
    bool valid;
};

static CFragmentDirectReader c_fragment_direct_reader(const CFragmentStore* store, const CFragmentHandle* handle, const CFragmentReaderSource* source, uint32_t boundary)
{
    CFragmentDirectReader reader =
    {
        .store = store,
        .leaves = c_fragment_leaf_cursor(source, boundary),
        .next = handle->head,
        .remaining = handle->positions,
        .tail = handle->tail,
        .last = BUSTER_C_FRAGMENT_NONE,
    };
    reader.valid = reader.leaves.valid && store->valid && handle->frozen && handle->positions != 0 && store->allocated <= store->capacity &&
                   (handle->head == BUSTER_C_FRAGMENT_NONE ? handle->tail == BUSTER_C_FRAGMENT_NONE && handle->positions == 1 : handle->head < store->allocated && handle->tail < store->allocated) &&
                   (store->payload == 16 || store->payload == 32 || store->payload == 48) && store->stride == sizeof(CFragmentHeader) + store->payload;
    return reader;
}

// No leaf identity, virtual-leaf identity or store.leaf_kinds access is permitted here. Operator identities remain
// an explicit experimental location stream. Leaves return the source-token payload handle for a future checker.
BUSTER_INLINE bool c_fragment_direct_window(CFragmentDirectReader* reader, uint8_t* kinds, uint32_t* tokens, uint32_t* count, bool vector)
{
    const CFragmentStore* store = reader->store;
    *count = 0;
    bool valid = reader->valid;
    if (valid && reader->remaining != 0 && reader->next == BUSTER_C_FRAGMENT_NONE)
    {
        valid = reader->visited == 0 && reader->remaining == 1 && c_fragment_leaf_next(&reader->leaves, kinds, tokens);
        if (valid)
        {
            reader->remaining = 0;
            *count = 1;
        }
    }
    else if (valid && reader->remaining != 0)
    {
        valid = store->records != 0 && store->live != 0 && reader->visited < store->allocated && c_fragment_record_valid(store, reader->next);
        if (valid)
        {
            const CFragmentHeader* header = c_fragment_header(store, reader->next);
            valid = header->used <= reader->remaining && (header->operator_count == 0 || store->identities != 0);
            uint8_t leaves[64];
            uint32_t leaf_count = 0;
            uint32_t operator_count = 0;
            const uint8_t* operators = c_fragment_operators(store, reader->next);
            for (uint32_t position = 0; valid && position < header->used; position += 1)
            {
                if ((header->operator_mask >> position) & 1)
                {
                    const uint8_t kind = operators[operator_count++];
                    const uint32_t token = c_fragment_identities(store, reader->next)[position];
                    valid = kind < C_FRAGMENT_KIND_COUNT;
                    if (valid && kind == C_FRAGMENT_END)
                    {
                        valid = token == BUSTER_C_FRAGMENT_NONE;
                    }
                    else if (valid)
                    {
                        const CFragmentReaderSource* source = reader->leaves.source;
                        valid = c_fragment_arities[kind] != 0 && token < source->count && source->kinds[token] == kind && source->owners[token] == reader->leaves.root;
                    }
                    if (valid)
                    {
                        tokens[position] = token;
                    }
                }
                else
                {
                    valid = c_fragment_leaf_next(&reader->leaves, leaves + leaf_count, tokens + position);
                    leaf_count += valid;
                }
            }
            if (valid)
            {
                valid = vector ? c_fragment_interleave(operators, leaves, header->operator_mask, header->used, kinds) :
                                 c_fragment_interleave_scalar(operators, leaves, header->operator_mask, header->used, kinds);
                reader->remaining -= header->used;
                reader->last = reader->next;
                reader->next = header->next;
                reader->visited += 1;
                *count = header->used;
            }
        }
    }
    if (valid && reader->remaining == 0)
    {
        valid = reader->next == BUSTER_C_FRAGMENT_NONE && reader->last == reader->tail && reader->leaves.next == BUSTER_C_FRAGMENT_NONE;
    }
    reader->valid = valid;
    if (!valid)
    {
        *count = 0;
    }
    return valid;
}

BUSTER_INLINE bool c_fragment_reader_flatten(CFragmentDirectReader* reader, uint8_t* kinds, uint32_t* tokens, uint32_t capacity, bool vector)
{
    bool valid = reader->valid && reader->remaining <= capacity;
    uint32_t written = 0;
    while (valid && reader->remaining != 0)
    {
        uint8_t window[64];
        uint32_t ids[64];
        uint32_t count = 0;
        valid = c_fragment_direct_window(reader, window, ids, &count, vector) && count != 0 && count <= capacity - written;
        if (valid)
        {
            memcpy(kinds + written, window, count);
            for (uint32_t i = 0; i < count; i += 1)
            {
                // B retains locations for operators only. A fresh leaf cursor supplies every leaf payload.
                tokens[written + i] = c_fragment_arities[window[i]] != 0 ? ids[i] : BUSTER_C_FRAGMENT_NONE;
            }
            written += count;
        }
    }
    return valid && reader->remaining == 0 && reader->next == BUSTER_C_FRAGMENT_NONE && reader->leaves.next == BUSTER_C_FRAGMENT_NONE;
}

typedef enum CFragmentReaderPhase
{
    C_FRAGMENT_READER_ENTER,
    C_FRAGMENT_READER_REFERENCE,
    C_FRAGMENT_READER_CHILD,
    C_FRAGMENT_READER_REDUCE,
    C_FRAGMENT_READER_LEAVE,
    C_FRAGMENT_READER_ATTACH,
} CFragmentReaderPhase;

typedef struct CFragmentReaderEvent CFragmentReaderEvent;
struct CFragmentReaderEvent
{
    uint32_t token;
    uint32_t child;
    uint32_t ordinal;
    uint32_t boundary;
    uint32_t source;
    uint32_t expanded;
    uint32_t auxiliary;
    uint32_t expanded_auxiliary;
    uint32_t reference;
    uint8_t kind;
    uint8_t phase;
};

typedef struct CFragmentReaderFrame CFragmentReaderFrame;
struct CFragmentReaderFrame
{
    uint32_t token;
    uint32_t remaining;
    uint32_t completed;
};

typedef struct CFragmentReaderResult CFragmentReaderResult;
struct CFragmentReaderResult
{
    uint32_t count;
    uint32_t nodes;
    uint32_t stack_peak;
    bool valid;
    bool overflow;
};

typedef enum CFragmentReaderStatus
{
    C_FRAGMENT_READER_ERROR,
    C_FRAGMENT_READER_EVENT,
    C_FRAGMENT_READER_END,
} CFragmentReaderStatus;

typedef enum CFragmentReaderStep
{
    C_FRAGMENT_READER_FAILED,
    C_FRAGMENT_READER_INPUT,
    C_FRAGMENT_READER_TYPE,
    C_FRAGMENT_READER_EXIT,
    C_FRAGMENT_READER_PARENT,
    C_FRAGMENT_READER_PUBLICATION,
    C_FRAGMENT_READER_FINISHED,
} CFragmentReaderStep;

typedef struct CFragmentReaderCursor CFragmentReaderCursor;
struct CFragmentReaderCursor
{
    CFragmentDirectReader* reader;
    const CFragmentReaderSource* source;
    const uint8_t* flat_kinds;
    const uint32_t* flat_tokens;
    CFragmentReaderVisits* visits;
    CFragmentReaderFrame* stack;
    uint32_t stack_capacity;
    uint32_t positions;
    uint32_t consumed;
    uint32_t depth;
    uint32_t claim_depth;
    uint32_t root;
    uint32_t boundary;
    uint32_t child;
    uint32_t window_count;
    uint32_t window_at;
    uint8_t kinds[64];
    uint32_t tokens[64];
    CFragmentReaderResult result;
    CFragmentReaderStep step;
    bool flat;
};

static bool c_fragment_reader_event(const CFragmentReaderSource* source, CFragmentReaderEvent* event, uint8_t phase, uint32_t token, uint32_t child, uint32_t ordinal, uint32_t boundary)
{
    bool valid = token < source->count;
    if (valid)
    {
        const CFragmentSyntax* syntax = &source->syntax[token];
        valid = syntax->source < source->count && (syntax->auxiliary == BUSTER_C_FRAGMENT_NONE || syntax->auxiliary < source->count) &&
                (syntax->type_root == BUSTER_C_FRAGMENT_NONE || syntax->type_root < source->count);
        if (valid)
        {
            *event = (CFragmentReaderEvent)
            {
                .token = token,
                .child = child,
                .ordinal = ordinal,
                .boundary = boundary,
                .source = syntax->source,
                .expanded = source->parser->tokens.sources[syntax->source],
                .auxiliary = syntax->auxiliary,
                .expanded_auxiliary = syntax->auxiliary < source->count ? source->parser->tokens.sources[syntax->auxiliary] : BUSTER_C_FRAGMENT_NONE,
                .reference = syntax->type_root,
                .kind = source->kinds[token],
                .phase = phase,
            };
        }
    }
    return valid;
}

// Source/store/flat input remain immutable and outlive the cursor. The caller owns the reader, visit ledger and
// bounded frame slice, exclusively while this cursor is active; nested roots use different readers/frame slices
// and share the ledger. A cursor can move by value, but copying it does not duplicate ownership of those slices.
// next preserves its decoded window across every event, including source leaves prefetched by direct decoding.
// It allocates nothing. REFERENCE requests caller-owned work; the outer cursor resumes only after the nested
// claim is complete. This syntax stack is not the future unified tagged semantic/checker continuation stack.
static CFragmentReaderCursor c_fragment_reader_cursor(CFragmentDirectReader* reader, const uint8_t* flat_kinds, const uint32_t* flat_tokens, uint32_t positions,
                                                      CFragmentReaderVisits* visits, CFragmentReaderFrame* stack, uint32_t stack_capacity, bool flat)
{
    CFragmentReaderCursor cursor =
    {
        .reader = reader,
        .source = reader != 0 ? reader->leaves.source : 0,
        .flat_kinds = flat_kinds,
        .flat_tokens = flat_tokens,
        .visits = visits,
        .stack = stack,
        .stack_capacity = stack_capacity,
        .positions = positions,
        .root = BUSTER_C_FRAGMENT_NONE,
        .boundary = BUSTER_C_FRAGMENT_NONE,
        .child = BUSTER_C_FRAGMENT_NONE,
        .flat = flat,
    };
    const CFragmentReaderSource* source = cursor.source;
    bool valid = reader != 0 && reader->valid && reader->leaves.valid && positions != 0 && positions == reader->remaining && stack != 0 && stack_capacity != 0 && visits != 0;
    valid = valid && source != 0 && source->valid && source->parser != 0 && source->parser->tokens.count == source->count && source->parser->tokens.sources != 0 && source->syntax != 0 &&
            source->kinds != 0 && source->owners != 0 && source->next != 0 && source->boundaries != 0 && reader->leaves.root < source->count &&
            (flat ? flat_kinds != 0 && flat_tokens != 0 : reader->store != 0);
    valid = valid && visits->count == source->count && visits->nodes != 0 && visits->states != 0 && visits->roots != 0 && visits->depth != 0 && visits->depth <= visits->capacity &&
            visits->roots[visits->depth - 1] == reader->leaves.root && visits->states[reader->leaves.root] == C_FRAGMENT_READER_ACTIVE;
    if (valid)
    {
        cursor.root = reader->leaves.root;
        cursor.boundary = source->boundaries[cursor.root];
        cursor.claim_depth = visits->depth;
        valid = cursor.boundary < source->count && source->owners[cursor.boundary] == cursor.root;
    }
    cursor.step = valid ? C_FRAGMENT_READER_INPUT : C_FRAGMENT_READER_FAILED;
    cursor.result.valid = valid;
    return cursor;
}

// Literal-mode next wrappers specialize both refill and input selection without callbacks. Each successful call
// yields exactly one event. END framing can consume one input position before REDUCE; it never becomes an event.
// ERROR is sticky until reinitialization and leaves partial traversal state unusable. END is sticky even after
// the caller completes/releases the root claim. Both statuses clear every output byte; only EVENT carries data.
BUSTER_INLINE CFragmentReaderStatus c_fragment_reader_next(CFragmentReaderCursor* cursor, CFragmentReaderEvent* event, bool flat, bool vector)
{
    CFragmentReaderStatus status = C_FRAGMENT_READER_ERROR;
    if (event != 0)
    {
        memset(event, 0, sizeof(*event));
    }
    bool valid = cursor != 0 && event != 0;
    if (valid && cursor->step == C_FRAGMENT_READER_FINISHED)
    {
        status = C_FRAGMENT_READER_END;
    }
    else if (valid && cursor->step != C_FRAGMENT_READER_FAILED)
    {
        CFragmentDirectReader* reader = cursor->reader;
        const CFragmentReaderSource* source = cursor->source;
        CFragmentReaderVisits* visits = cursor->visits;
        valid = cursor->result.valid && cursor->flat == flat && reader->valid && reader->leaves.valid && source->valid && reader->leaves.source == source && reader->leaves.root == cursor->root &&
                visits->count == source->count && visits->nodes != 0 && visits->states != 0 && visits->roots != 0 && visits->depth == cursor->claim_depth && visits->depth != 0 && visits->depth <= visits->capacity &&
                cursor->root < source->count && visits->roots[visits->depth - 1] == cursor->root && visits->states[cursor->root] == C_FRAGMENT_READER_ACTIVE &&
                cursor->depth <= cursor->stack_capacity && cursor->result.count != UINT32_MAX;
        if (valid && cursor->depth != 0)
        {
            const uint32_t token = cursor->stack[cursor->depth - 1].token;
            valid = token < source->count && source->owners[token] == cursor->root && visits->nodes[token] == C_FRAGMENT_READER_ACTIVE;
        }
        uint8_t phase = UINT8_MAX;
        uint32_t token = BUSTER_C_FRAGMENT_NONE;
        uint32_t child = BUSTER_C_FRAGMENT_NONE;
        uint32_t ordinal = 0;
        if (valid && cursor->step == C_FRAGMENT_READER_TYPE)
        {
            valid = cursor->depth != 0;
            if (valid)
            {
                token = cursor->stack[cursor->depth - 1].token;
                valid = source->syntax[token].type_root != BUSTER_C_FRAGMENT_NONE;
                phase = C_FRAGMENT_READER_REFERENCE;
                cursor->step = C_FRAGMENT_READER_INPUT;
            }
        }
        else if (valid && cursor->step == C_FRAGMENT_READER_EXIT)
        {
            valid = cursor->depth != 0 && cursor->stack[cursor->depth - 1].remaining == 0;
            if (valid)
            {
                token = cursor->stack[cursor->depth - 1].token;
                phase = C_FRAGMENT_READER_LEAVE;
                visits->nodes[token] = C_FRAGMENT_READER_DONE;
                cursor->depth -= 1;
                cursor->child = token;
                cursor->step = cursor->depth != 0 ? C_FRAGMENT_READER_PARENT : C_FRAGMENT_READER_PUBLICATION;
            }
        }
        else if (valid && cursor->step == C_FRAGMENT_READER_PARENT)
        {
            valid = cursor->depth != 0 && cursor->child < source->count && visits->nodes[cursor->child] == C_FRAGMENT_READER_DONE;
            if (valid)
            {
                CFragmentReaderFrame* parent = &cursor->stack[cursor->depth - 1];
                valid = parent->remaining != 0 && parent->completed < cursor->positions;
                token = parent->token;
                child = cursor->child;
                ordinal = parent->completed;
                phase = C_FRAGMENT_READER_CHILD;
                parent->completed += 1;
                parent->remaining -= parent->remaining != UINT32_MAX;
                cursor->step = C_FRAGMENT_READER_INPUT;
            }
        }
        else if (valid && cursor->step == C_FRAGMENT_READER_PUBLICATION)
        {
            valid = cursor->depth == 0 && cursor->consumed == cursor->positions && cursor->child == cursor->root && reader->leaves.next == BUSTER_C_FRAGMENT_NONE &&
                    visits->nodes[cursor->root] == C_FRAGMENT_READER_DONE && (flat || (reader->remaining == 0 && cursor->window_at == cursor->window_count));
            token = cursor->root;
            phase = C_FRAGMENT_READER_ATTACH;
            cursor->step = C_FRAGMENT_READER_FINISHED;
        }
        else if (valid)
        {
            valid = cursor->step == C_FRAGMENT_READER_INPUT;
            // At most two iterations: consume a node and emit ENTER, or consume END then emit REDUCE.
            for (uint32_t advance = 0; valid && phase == UINT8_MAX && advance < 2; advance += 1)
            {
                if (cursor->depth != 0 && cursor->stack[cursor->depth - 1].remaining == 0)
                {
                    token = cursor->stack[cursor->depth - 1].token;
                    phase = C_FRAGMENT_READER_REDUCE;
                    cursor->step = C_FRAGMENT_READER_EXIT;
                }
                else
                {
                    uint8_t kind = C_FRAGMENT_KIND_COUNT;
                    valid = cursor->consumed < cursor->positions;
                    if (valid && flat)
                    {
                        kind = cursor->flat_kinds[cursor->consumed];
                        if (kind < C_FRAGMENT_KIND_COUNT && kind != C_FRAGMENT_END && c_fragment_arities[kind] == 0)
                        {
                            uint8_t leaf = C_FRAGMENT_KIND_COUNT;
                            valid = c_fragment_leaf_next(&reader->leaves, &leaf, &token) && leaf == kind;
                        }
                        else
                        {
                            token = cursor->flat_tokens[cursor->consumed];
                        }
                    }
                    else if (valid)
                    {
                        valid = cursor->window_at <= cursor->window_count && cursor->window_count <= 64;
                        if (valid && cursor->window_at == cursor->window_count)
                        {
                            valid = c_fragment_direct_window(reader, cursor->kinds, cursor->tokens, &cursor->window_count, vector) && cursor->window_count != 0;
                            cursor->window_at = 0;
                        }
                        if (valid)
                        {
                            kind = cursor->kinds[cursor->window_at];
                            token = cursor->tokens[cursor->window_at++];
                        }
                    }
                    cursor->consumed += valid;
                    valid = valid && kind < C_FRAGMENT_KIND_COUNT;
                    if (valid && kind == C_FRAGMENT_END)
                    {
                        valid = token == BUSTER_C_FRAGMENT_NONE && cursor->depth != 0 && cursor->stack[cursor->depth - 1].remaining == UINT32_MAX;
                        if (valid)
                        {
                            cursor->stack[cursor->depth - 1].remaining = 0;
                        }
                    }
                    else if (valid)
                    {
                        valid = token < source->count && source->kinds[token] == kind && source->owners[token] == cursor->root && visits->nodes[token] == C_FRAGMENT_READER_UNSEEN &&
                                cursor->depth < cursor->stack_capacity && (cursor->depth != 0 || (cursor->result.nodes == 0 && token == cursor->root));
                        if (valid)
                        {
                            visits->nodes[token] = C_FRAGMENT_READER_ACTIVE;
                            const uint32_t arity = c_fragment_arities[kind];
                            cursor->stack[cursor->depth++] = (CFragmentReaderFrame)
                            {
                                .token = token,
                                .remaining = arity == BUSTER_C_FRAGMENT_LIST_ARITY ? UINT32_MAX : arity,
                            };
                            cursor->result.stack_peak = cursor->depth > cursor->result.stack_peak ? cursor->depth : cursor->result.stack_peak;
                            cursor->result.nodes += 1;
                            phase = C_FRAGMENT_READER_ENTER;
                            cursor->step = source->syntax[token].type_root != BUSTER_C_FRAGMENT_NONE ? C_FRAGMENT_READER_TYPE : C_FRAGMENT_READER_INPUT;
                        }
                    }
                }
            }
        }
        valid = valid && phase <= C_FRAGMENT_READER_ATTACH && c_fragment_reader_event(source, event, phase, token, child, ordinal, cursor->boundary);
        if (valid)
        {
            cursor->result.count += 1;
            status = C_FRAGMENT_READER_EVENT;
        }
    }
    if (status == C_FRAGMENT_READER_ERROR)
    {
        if (cursor != 0)
        {
            cursor->step = C_FRAGMENT_READER_FAILED;
            cursor->result.valid = false;
        }
        if (event != 0)
        {
            memset(event, 0, sizeof(*event));
        }
    }
    return status;
}

static CFragmentReaderStatus c_fragment_reader_next_flat(CFragmentReaderCursor* cursor, CFragmentReaderEvent* event)
{
    return c_fragment_reader_next(cursor, event, true, false);
}

static CFragmentReaderStatus c_fragment_reader_next_direct_scalar(CFragmentReaderCursor* cursor, CFragmentReaderEvent* event)
{
    return c_fragment_reader_next(cursor, event, false, false);
}

static CFragmentReaderStatus c_fragment_reader_next_direct_vector(CFragmentReaderCursor* cursor, CFragmentReaderEvent* event)
{
    return c_fragment_reader_next(cursor, event, false, true);
}

// Compatibility collector. Output exhaustion is distinct from malformed input; both leave partial state unusable.
BUSTER_INLINE CFragmentReaderResult c_fragment_reader_schedule(CFragmentDirectReader* reader, const uint8_t* flat_kinds, const uint32_t* flat_tokens, uint32_t positions,
                                                               CFragmentReaderVisits* visits, CFragmentReaderFrame* stack, uint32_t stack_capacity, CFragmentReaderEvent* events, uint32_t event_capacity, bool flat, bool vector)
{
    CFragmentReaderCursor cursor = c_fragment_reader_cursor(reader, flat_kinds, flat_tokens, positions, visits, stack, stack_capacity, flat);
    uint32_t written = 0;
    bool finished = false;
    cursor.result.valid = cursor.result.valid && events != 0;
    while (cursor.result.valid && !finished)
    {
        CFragmentReaderEvent event;
        const CFragmentReaderStatus status = c_fragment_reader_next(&cursor, &event, flat, vector);
        if (status == C_FRAGMENT_READER_EVENT)
        {
            if (written < event_capacity)
            {
                events[written++] = event;
            }
            else
            {
                cursor.result.valid = false;
                cursor.result.overflow = true;
                cursor.step = C_FRAGMENT_READER_FAILED;
            }
        }
        else
        {
            finished = status == C_FRAGMENT_READER_END;
        }
    }
    CFragmentReaderResult result = cursor.result;
    result.count = written;
    result.valid = result.valid && finished;
    return result;
}

static CFragmentReaderResult c_fragment_reader_flat(CFragmentDirectReader* context, const uint8_t* kinds, const uint32_t* tokens, uint32_t positions,
                                                   CFragmentReaderVisits* visits, CFragmentReaderFrame* stack, uint32_t stack_capacity, CFragmentReaderEvent* events, uint32_t event_capacity)
{
    return c_fragment_reader_schedule(context, kinds, tokens, positions, visits, stack, stack_capacity, events, event_capacity, true, false);
}

static CFragmentReaderResult c_fragment_reader_direct_scalar(CFragmentDirectReader* reader, uint32_t positions, CFragmentReaderVisits* visits, CFragmentReaderFrame* stack, uint32_t stack_capacity, CFragmentReaderEvent* events, uint32_t event_capacity)
{
    return c_fragment_reader_schedule(reader, 0, 0, positions, visits, stack, stack_capacity, events, event_capacity, false, false);
}

static CFragmentReaderResult c_fragment_reader_direct_vector(CFragmentDirectReader* reader, uint32_t positions, CFragmentReaderVisits* visits, CFragmentReaderFrame* stack, uint32_t stack_capacity, CFragmentReaderEvent* events, uint32_t event_capacity)
{
    return c_fragment_reader_schedule(reader, 0, 0, positions, visits, stack, stack_capacity, events, event_capacity, false, true);
}

#endif
