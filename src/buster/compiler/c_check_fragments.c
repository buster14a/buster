// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #106's fragment consumer: the checker's expression graph built from Salter's operator fragments (#104, c_fragments.c)
// instead of the tree's children. c_check_input.c includes it in the fragments' unit alone (BUSTER_PARSE_FRAGMENT_SINK),
// and check_input uses it only while the experiment's request is set (c_fragment_request).
//
// check_input_expressions walks the tree in postorder and builds each token's graph nodes from its operands, the values
// of its children in the tree. With this reader the same walk drives a cursor per published expression
// (c_fragment_reader.c), and an operator takes its operands from the fragments instead: ENTER pushes a pending frame,
// each CHILD appends the values of the child that completed, a cast's REFERENCE those of its type name, and REDUCE takes
// the frame, at the operator's own place in the walk. A child's values are those of its envelope, the tree's child of
// the operator that holds it (a group around it, the `?` before a middle operand), which the walk built just before.
// Everything else keeps the tree's operands: the leaves (only a reference's subtree has any), and the tokens the
// fragments leave out (openers, transparent groups, type names, statements and declarations). Both readers then build
// the same graph, node for node, with check_input_expressions' own code for each token. Variant B flattens an expression
// into a prefix array when its cursor starts (c_fragment_reader_flatten); variant C decodes the fragments' windows as it
// reads.
//
// The cursors nest as the expressions do in postorder (an array bound's expression inside a cast's type name), on one
// explicit stack, and the walk advances only the top one, as the visit ledger requires: an expression's cursor starts at
// its first fragment token in postorder and ends at its root's REDUCE. Every pending frame and reader frame is pushed
// and popped in the same nesting, so each lives in one array. The walk's skips (past the first report, in bodies other
// readers check) only cut off the end of the postorder, which leaves the last cursors unfinished: only a diagnostic
// graph does that, and only a unit without reports must finish every expression (check_fragments_end).
//
// Layout: CheckFragments and its statistics; check_fragments_begin; the frames (check_fragments_append and the
// envelope); the cursors (check_fragments_start, check_fragments_advance, check_fragments_finish, check_fragments_token);
// check_fragments_operands, which check_input_expressions calls, with the test's check of what it takes
// (check_fragments_verify); check_fragments_end.

#ifndef BUSTER_C_CHECK_FRAGMENTS_INCLUDED
#define BUSTER_C_CHECK_FRAGMENTS_INCLUDED

#include "c_fragment_reader.c"

// An operator's operands while its children complete: its token, and its values so far, as check_input_expressions
// gathers them (first, last, count).
typedef struct CheckFragmentFrame CheckFragmentFrame;
struct CheckFragmentFrame
{
    uint32_t token;
    uint32_t first;
    uint32_t last;
    uint32_t count;
    // The operator has a child in the tree that carries no fragment (CFragmentSyntax.raw_operands): the frame gathers
    // nothing, so that check_input_expressions gathers its children's values, unlinked, from the tree.
    bool raw;
};

// A published expression being read: its reader (which the cursor points into, so that cursors never move), its root,
// and where its reader frames and B's flat prefix start in the shared arrays.
typedef struct CheckFragmentCursor CheckFragmentCursor;
struct CheckFragmentCursor
{
    CFragmentDirectReader reader;
    CFragmentReaderCursor cursor;
    uint32_t root;
    uint32_t stack;
    uint32_t flat;
};

// What a unit's reading did, for test-fragments and benchmark-parser --expressions: the expressions, the cursor events,
// the operators whose operands came from the fragments and those that took them from the tree, the leaves, the prefix
// positions B flattened, the deepest nesting of cursors and of pending frames, and the bytes of the reader's index (kept
// for the walk, and temporary while it is made) and of the walk's own state.
typedef struct CheckFragmentStatistics CheckFragmentStatistics;
struct CheckFragmentStatistics
{
    uint64_t expressions;
    uint64_t events;
    uint64_t operators;
    uint64_t raw_operators;
    uint64_t leaves;
    uint64_t flat_positions;
    uint32_t cursor_peak;
    uint32_t frame_peak;
    uint64_t index_bytes;
    uint64_t index_temporary_bytes;
    uint64_t state_bytes;
};

// benchmark-parser --expressions' split of check_input (TSC ticks, summed until it clears them): the reader's index and
// state, made at the start of the expressions' walk (none without a request), and the walk itself, either reader's.
typedef struct CheckFragmentTicks CheckFragmentTicks;
struct CheckFragmentTicks
{
    uint64_t index;
    uint64_t walk;
};

static CheckFragmentTicks check_fragments_ticks;

typedef struct CheckFragments CheckFragments;
struct CheckFragments
{
    const CFragmentRequest* request;
    CFragmentReaderSource source;
    CFragmentReaderVisits visits;
    // Each published expression's index plus one, at its boundary token (the raw subtree it attaches as), or 0.
    uint32_t* expressions;
    CheckFragmentCursor* cursors;
    uint32_t cursor_count;
    uint32_t cursor_capacity;
    CheckFragmentFrame* frames;
    uint32_t frame_count;
    uint32_t frame_capacity;
    CFragmentReaderFrame* stack;
    uint32_t stack_used;
    uint32_t stack_capacity;
    uint8_t* flat_kinds;
    uint32_t* flat_tokens;
    uint32_t flat_used;
    uint32_t flat_capacity;
    // The first token whose reading failed (a malformed cursor event, or one out of step with the walk), or NONE: the
    // reader then stops, and check_input_expressions gathers from the tree, so the caller must look.
    uint32_t failed;
    bool valid;
    CheckFragmentStatistics statistics;
};

// The reader for one unit, from the request's sink after the scalar parse filled it, in the request's arena; or null
// without a request (or for a unit whose bodies other readers check, which it doesn't support). A request whose sink
// can't be read gets a reader that is invalid from the start, for the caller to find.
static CheckFragments* check_fragments_begin(CFragmentRequest* request, const Parser* parser, const uint8_t* deferred_tokens)
{
    CheckFragments* fragments = 0;
    if (request != 0)
    {
        Arena* arena = request->arena;
        const uint64_t mark = arena->length;
        fragments = BUSTER_ARENA_PUSH_ARRAY(arena, CheckFragments, 1);
        if (fragments != 0)
        {
            *fragments = (CheckFragments)
            {
                .request = request,
                .failed = BUSTER_CHECK_NONE,
            };
            const uint32_t count = parser->tokens.count;
            const CFragmentSink* sink = &request->sink;
            bool valid = request->filled && deferred_tokens == 0 && count < UINT32_MAX / 4 &&
                         c_fragment_reader_source(&fragments->source, parser, sink, arena, request->temporary);
            const uint64_t state = arena->length;
            const uint64_t slots = (uint64_t)count + 1;
            fragments->cursor_capacity = valid ? sink->expression_count + 1 : 0;
            fragments->frame_capacity = valid ? count + 1 : 0;
            fragments->stack_capacity = valid ? 2 * count + 64 : 0;
            fragments->flat_capacity = request->flat ? fragments->stack_capacity : 0;
            fragments->expressions = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : 0;
            fragments->cursors = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, CheckFragmentCursor, fragments->cursor_capacity) : 0;
            fragments->frames = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, CheckFragmentFrame, fragments->frame_capacity) : 0;
            fragments->stack = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, CFragmentReaderFrame, fragments->stack_capacity) : 0;
            fragments->flat_kinds = valid && request->flat ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, fragments->flat_capacity) : 0;
            fragments->flat_tokens = valid && request->flat ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, fragments->flat_capacity) : 0;
            fragments->visits = (CFragmentReaderVisits)
            {
                .states = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots) : 0,
                .nodes = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots) : 0,
                .roots = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : 0,
                .count = count,
                .capacity = count + 1,
            };
            valid = valid && fragments->expressions != 0 && fragments->cursors != 0 && fragments->frames != 0 && fragments->stack != 0 &&
                    (!request->flat || (fragments->flat_kinds != 0 && fragments->flat_tokens != 0)) &&
                    fragments->visits.states != 0 && fragments->visits.nodes != 0 && fragments->visits.roots != 0;
            if (valid)
            {
                memset(fragments->expressions, 0, slots * sizeof(uint32_t));
                memset(fragments->visits.states, 0, slots);
                memset(fragments->visits.nodes, 0, slots);
                for (uint32_t e = 0; valid && e < sink->expression_count; e += 1)
                {
                    const uint32_t boundary = sink->expressions[e].root;
                    valid = boundary < count && fragments->expressions[boundary] == 0;
                    if (valid)
                    {
                        fragments->expressions[boundary] = e + 1;
                    }
                }
                fragments->statistics.index_bytes = fragments->source.retained_bytes;
                fragments->statistics.index_temporary_bytes = fragments->source.temporary_bytes;
                fragments->statistics.state_bytes = arena->length - state;
            }
            fragments->valid = valid;
        }
        else
        {
            arena_pop_to(arena, mark);
        }
    }
    return fragments;
}

// ---------------------------------------------------------------------------------------------------------------------
// The frames

// Appends the values of `token`, built already, to an operator's frame, as check_input_expressions appends a child's:
// but for a token before an operator that finishes specifiers, whose value isn't its operand.
static void check_fragments_append(CheckInputReader* reader, CheckFragmentFrame* frame, uint32_t token)
{
    if (!frame->raw && (reader->specifier_finishes[frame->token] == 0 || token > frame->token))
    {
        check_input_append_values(reader, reader->first_value[token], reader->last_value[token], &frame->first, &frame->last, &frame->count);
    }
}

// The envelope of fragment child `child` under operator `parent`: the tree's child of `parent` that holds it, which is
// the child itself but where the fragments left out a token between them (a group, a middle operand's `?`). NONE if the
// child isn't below the operator in the tree.
static uint32_t check_fragments_envelope(const Parser* parser, uint32_t child, uint32_t parent)
{
    const uint32_t count = parser->tokens.count;
    uint32_t envelope = child;
    for (uint32_t steps = 0; envelope < count && parser->tree.parents[envelope] != parent && steps < count; steps += 1)
    {
        envelope = parser->tree.parents[envelope];
    }
    return envelope < count && parser->tree.parents[envelope] == parent ? envelope : BUSTER_CHECK_NONE;
}

// ---------------------------------------------------------------------------------------------------------------------
// The cursors

// Starts the cursor of the expression whose root is `root`, on top of the others, as the walk reaches its first fragment
// token: its reader, its claim in the ledger, its reader frames, and for B its prefix array, flattened now.
static bool check_fragments_start(CheckFragments* fragments, uint32_t root, bool flat, bool vector)
{
    const CFragmentSink* sink = &fragments->request->sink;
    const CFragmentReaderSource* source = &fragments->source;
    bool valid = root < source->count && fragments->cursor_count < fragments->cursor_capacity;
    const uint32_t boundary = valid ? source->boundaries[root] : BUSTER_CHECK_NONE;
    valid = valid && boundary < source->count && fragments->expressions[boundary] != 0;
    if (valid)
    {
        const CFragmentExpression* expression = &sink->expressions[fragments->expressions[boundary] - 1];
        const uint32_t positions = expression->handle.positions;
        CheckFragmentCursor* top = &fragments->cursors[fragments->cursor_count];
        top->reader = c_fragment_direct_reader(&sink->store, &expression->handle, source, boundary);
        top->root = root;
        top->stack = fragments->stack_used;
        top->flat = fragments->flat_used;
        valid = top->reader.valid && positions <= fragments->stack_capacity - fragments->stack_used &&
                (!flat || positions <= fragments->flat_capacity - fragments->flat_used) && c_fragment_reader_claim(&fragments->visits, &top->reader.leaves);
        if (valid && flat)
        {
            // B: the whole expression's prefix kinds and operator tokens, then a cursor over them with a fresh leaf cursor.
            CFragmentDirectReader flatten = top->reader;
            valid = c_fragment_reader_flatten(&flatten, fragments->flat_kinds + top->flat, fragments->flat_tokens + top->flat, positions, vector);
            fragments->statistics.flat_positions += positions;
        }
        if (valid)
        {
            top->cursor = c_fragment_reader_cursor(&top->reader, flat ? fragments->flat_kinds + top->flat : 0, flat ? fragments->flat_tokens + top->flat : 0, positions,
                                                   &fragments->visits, fragments->stack + top->stack, positions, flat);
            valid = top->cursor.result.valid;
        }
        if (valid)
        {
            fragments->stack_used += positions;
            fragments->flat_used += flat ? positions : 0;
            fragments->cursor_count += 1;
            fragments->statistics.expressions += 1;
            fragments->statistics.cursor_peak = fragments->cursor_count > fragments->statistics.cursor_peak ? fragments->cursor_count : fragments->statistics.cursor_peak;
        }
    }
    return valid;
}

// The top cursor's events up to `token`'s REDUCE, with the pending frames they push and fill; then `token`'s frame, off
// the frames, in `taken`. Whether `token` was the next REDUCE.
BUSTER_INLINE bool check_fragments_advance(CheckInputReader* reader, CheckFragments* fragments, uint32_t token, CheckFragmentFrame* taken, bool flat, bool vector)
{
    CheckFragmentCursor* top = &fragments->cursors[fragments->cursor_count - 1];
    bool valid = true;
    bool reduced = false;
    while (valid && !reduced)
    {
        CFragmentReaderEvent event;
        valid = c_fragment_reader_next(&top->cursor, &event, flat, vector) == C_FRAGMENT_READER_EVENT;
        fragments->statistics.events += valid;
        CheckFragmentFrame* frame = fragments->frame_count != 0 ? &fragments->frames[fragments->frame_count - 1] : 0;
        if (valid && event.phase == C_FRAGMENT_READER_ENTER)
        {
            valid = fragments->frame_count < fragments->frame_capacity;
            if (valid)
            {
                fragments->frames[fragments->frame_count] = (CheckFragmentFrame)
                {
                    .token = event.token,
                    .first = BUSTER_CHECK_NONE,
                    .last = BUSTER_CHECK_NONE,
                    .count = 0,
                    .raw = fragments->request->sink.syntax[event.token].raw_operands,
                };
                fragments->frame_count += 1;
                fragments->statistics.frame_peak = fragments->frame_count > fragments->statistics.frame_peak ? fragments->frame_count : fragments->statistics.frame_peak;
            }
        }
        else if (valid && event.phase == C_FRAGMENT_READER_REFERENCE)
        {
            // A cast's type name is its first operand in the tree. A leaf's reference is the leaf's own subtree, whose
            // values the leaf gathers from the tree.
            valid = frame != 0 && frame->token == event.token;
            if (valid && event.reference != event.token)
            {
                valid = event.reference < reader->parser->tokens.count && reader->parser->tree.parents[event.reference] == event.token;
                if (valid)
                {
                    check_fragments_append(reader, frame, event.reference);
                }
            }
        }
        else if (valid && event.phase == C_FRAGMENT_READER_CHILD)
        {
            const uint32_t envelope = check_fragments_envelope(reader->parser, event.child, event.token);
            valid = frame != 0 && frame->token == event.token && envelope != BUSTER_CHECK_NONE;
            if (valid)
            {
                check_fragments_append(reader, frame, envelope);
            }
        }
        else if (valid && event.phase == C_FRAGMENT_READER_REDUCE)
        {
            valid = event.token == token && frame != 0 && frame->token == token;
            if (valid)
            {
                *taken = *frame;
                fragments->frame_count -= 1;
                reduced = true;
            }
        }
        else if (valid)
        {
            // LEAVE: the frame went at REDUCE. ATTACH comes only after the root's REDUCE, which ends the advance.
            valid = event.phase == C_FRAGMENT_READER_LEAVE;
        }
    }
    return valid;
}

// The top cursor after its root's REDUCE: its LEAVE and ATTACH, then its end, its claim completed and its slices freed.
BUSTER_INLINE bool check_fragments_finish(CheckFragments* fragments, bool flat, bool vector)
{
    CheckFragmentCursor* top = &fragments->cursors[fragments->cursor_count - 1];
    CFragmentReaderEvent event;
    bool valid = c_fragment_reader_next(&top->cursor, &event, flat, vector) == C_FRAGMENT_READER_EVENT && event.phase == C_FRAGMENT_READER_LEAVE && event.token == top->root;
    valid = valid && c_fragment_reader_next(&top->cursor, &event, flat, vector) == C_FRAGMENT_READER_EVENT && event.phase == C_FRAGMENT_READER_ATTACH;
    valid = valid && c_fragment_reader_next(&top->cursor, &event, flat, vector) == C_FRAGMENT_READER_END;
    valid = valid && c_fragment_reader_complete(&fragments->visits, &top->reader.leaves);
    fragments->statistics.events += valid ? 2 : 0;
    if (valid)
    {
        fragments->stack_used = top->stack;
        fragments->flat_used = top->flat;
        fragments->cursor_count -= 1;
    }
    return valid;
}

// One token of check_input_expressions' walk in one mode: a fragment token advances its expression's cursor (starting
// it at the expression's first), and an operator then has its operands from its frame. Whether it has.
BUSTER_INLINE bool check_fragments_token(CheckInputReader* reader, CheckFragments* fragments, uint32_t token, uint8_t kind, CheckFragmentFrame* taken, bool flat, bool vector)
{
    const uint32_t root = fragments->source.owners[token];
    bool valid = fragments->cursor_count != 0 && fragments->cursors[fragments->cursor_count - 1].root == root;
    if (!valid)
    {
        valid = check_fragments_start(fragments, root, flat, vector);
    }
    valid = valid && check_fragments_advance(reader, fragments, token, taken, flat, vector);
    if (valid && token == root)
    {
        valid = check_fragments_finish(fragments, flat, vector);
    }
    const bool operator = c_fragment_arities[kind] != 0 && !taken->raw;
    fragments->statistics.operators += valid && operator;
    fragments->statistics.raw_operators += valid && c_fragment_arities[kind] != 0 && taken->raw;
    fragments->statistics.leaves += valid && c_fragment_arities[kind] == 0;
    fragments->valid = valid;
    fragments->failed = valid ? fragments->failed : token;
    return valid && operator;
}

// The test's check of an operator's operands (CFragmentRequest.verify): the first and last values of its children in the
// tree and their count, as check_input_expressions would gather them. The fragments linked the same lists already, so it
// counts each child's list from its first value to its last rather than to the end of the links.
static bool check_fragments_verify(const CheckInputReader* reader, uint32_t token, const CheckFragmentFrame* taken)
{
    uint32_t first = BUSTER_CHECK_NONE;
    uint32_t last = BUSTER_CHECK_NONE;
    uint32_t count = 0;
    bool valid = true;
    for (uint32_t child = reader->first_child[token]; valid && child != BUSTER_CHECK_NONE; child = reader->next_child[child])
    {
        const uint32_t begin = reader->first_value[child];
        if ((reader->specifier_finishes[token] == 0 || child > token) && begin != BUSTER_CHECK_NONE)
        {
            const uint32_t end = reader->last_value[child];
            first = first == BUSTER_CHECK_NONE ? begin : first;
            last = end;
            uint32_t n = begin;
            uint32_t steps = 0;
            while (n != end && n != BUSTER_CHECK_NONE && steps < reader->output->graph.count)
            {
                n = reader->value_next[n];
                steps += 1;
            }
            valid = n == end;
            count += steps + 1;
        }
    }
    return valid && first == taken->first && last == taken->last && count == taken->count;
}

// check_input_expressions' operands of `token` when the fragments give them: an operator of a published expression,
// whose values its children completed, into first, last and count. Whether they did. The mode is the request's: a
// static choice for each loop, with the interleave's vector path or its scalar twin.
static bool check_fragments_operands(CheckInputReader* reader, uint32_t token, uint32_t* first, uint32_t* last, uint32_t* count)
{
    CheckFragments* fragments = reader->fragments;
    const uint8_t kind = fragments->valid ? fragments->source.kinds[token] : C_FRAGMENT_KIND_COUNT;
    bool result = false;
    if (kind < C_FRAGMENT_KIND_COUNT)
    {
        CheckFragmentFrame taken = { 0 };
        const CFragmentRequest* request = fragments->request;
        if (request->flat)
        {
            result = check_fragments_token(reader, fragments, token, kind, &taken, true, false);
        }
        else if (request->vector)
        {
            result = check_fragments_token(reader, fragments, token, kind, &taken, false, true);
        }
        else
        {
            result = check_fragments_token(reader, fragments, token, kind, &taken, false, false);
        }
        if (result && request->verify && !check_fragments_verify(reader, token, &taken))
        {
            fragments->valid = false;
            fragments->failed = token;
        }
        if (result)
        {
            *first = taken.first;
            *last = taken.last;
            *count = taken.count;
        }
    }
    return result;
}

// After the walk: a unit without reports (`complete`) must have read every published expression to its end.
static void check_fragments_end(CheckFragments* fragments, bool complete)
{
    if (fragments != 0 && fragments->valid && complete)
    {
        fragments->valid = fragments->cursor_count == 0 && fragments->frame_count == 0 && fragments->visits.depth == 0 &&
                           fragments->statistics.expressions == fragments->request->sink.expression_count;
    }
}

#endif
