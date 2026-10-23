// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #106 cursor/event tests, included after the independent coverage oracle and c_fragment_reader.c. Expected
// children come from ordinary-parser postorder and independently normalized parents, never a fragment decode.
// A reverse-pushed action worklist compares every event, including raw attachment boundaries and type/source
// identities, against that tree. This deliberately differs from the production prefix/arity continuation loop.
// Scalar/vector flattening and scalar/vector direct traversal must all match. These are syntax acceptance tests;
// they implement neither semantic actions nor instruction-identical IR qualification.

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

typedef struct CFragmentReaderTestStatistics CFragmentReaderTestStatistics;
struct CFragmentReaderTestStatistics
{
    uint64_t traversals;
    uint64_t events;
    uint64_t references;
    uint64_t source_peak;
    uint64_t temporary_peak;
    uint64_t borrowed_peak;
    uint64_t preparation_input_peak;
    uint64_t flat_peak;
    uint64_t flat_backing_peak;
    uint64_t frame_peak;
    uint64_t frame_backing_peak;
    uint64_t visits_backing_peak;
    uint64_t event_peak;
    uint64_t cursor_peak;
    uint64_t test_arena_peak;
};

typedef struct CFragmentReaderTestAction CFragmentReaderTestAction;
struct CFragmentReaderTestAction
{
    uint32_t token;
    uint32_t child;
    uint32_t ordinal;
    uint8_t phase;
};

typedef struct CFragmentReaderTestTree CFragmentReaderTestTree;
struct CFragmentReaderTestTree
{
    uint32_t* roots;
    uint32_t* boundaries;
    uint32_t* last;
    uint32_t* previous;
    uint32_t* children;
};

static void c_fragment_reader_test_peak(uint64_t* peak, uint64_t bytes)
{
    *peak = bytes > *peak ? bytes : *peak;
}

static bool c_fragment_reader_test_push(CFragmentReaderTestAction* work, uint32_t* depth, uint32_t capacity, uint8_t phase, uint32_t token, uint32_t child, uint32_t ordinal)
{
    const bool valid = *depth < capacity;
    if (valid)
    {
        work[(*depth)++] = (CFragmentReaderTestAction)
        {
            .token = token,
            .child = child,
            .ordinal = ordinal,
            .phase = phase,
        };
    }
    return valid;
}

// 255 is test-only EXPAND: it never occurs in the production event stream.
static bool c_fragment_reader_test_events(const Parser* parser, const CFragmentCoverage* coverage, const CFragmentReaderTestTree* tree, uint32_t root,
                                          const CFragmentReaderEvent* events, const CFragmentReaderResult* result, CFragmentReaderTestAction* work, uint32_t capacity)
{
    const uint32_t count = parser->tokens.count;
    uint32_t depth = 0;
    uint32_t at = 0;
    uint32_t nodes = 0;
    bool valid = result->valid && root < count;
    valid = valid && c_fragment_reader_test_push(work, &depth, capacity, C_FRAGMENT_READER_ATTACH, root, BUSTER_C_FRAGMENT_NONE, 0) &&
            c_fragment_reader_test_push(work, &depth, capacity, UINT8_MAX, root, BUSTER_C_FRAGMENT_NONE, 0);
    while (valid && depth != 0)
    {
        const CFragmentReaderTestAction action = work[--depth];
        const uint32_t token = action.token;
        valid = token < count;
        if (valid && action.phase == UINT8_MAX)
        {
            valid = nodes < count;
            nodes += 1;
            valid = valid && c_fragment_reader_test_push(work, &depth, capacity, C_FRAGMENT_READER_LEAVE, token, BUSTER_C_FRAGMENT_NONE, 0) &&
                    c_fragment_reader_test_push(work, &depth, capacity, C_FRAGMENT_READER_REDUCE, token, BUSTER_C_FRAGMENT_NONE, 0);
            uint32_t child = tree->last[token];
            uint32_t ordinal = tree->children[token];
            while (valid && ordinal != 0)
            {
                ordinal -= 1;
                valid = child < count && c_fragment_reader_test_push(work, &depth, capacity, C_FRAGMENT_READER_CHILD, token, child, ordinal) &&
                        c_fragment_reader_test_push(work, &depth, capacity, UINT8_MAX, child, BUSTER_C_FRAGMENT_NONE, 0);
                if (valid)
                {
                    child = tree->previous[child];
                }
            }
            valid = valid && child == BUSTER_C_FRAGMENT_NONE;
            if (valid && coverage->nodes[token].type_root != BUSTER_C_FRAGMENT_NONE)
            {
                valid = c_fragment_reader_test_push(work, &depth, capacity, C_FRAGMENT_READER_REFERENCE, token, BUSTER_C_FRAGMENT_NONE, 0);
            }
            valid = valid && c_fragment_reader_test_push(work, &depth, capacity, C_FRAGMENT_READER_ENTER, token, BUSTER_C_FRAGMENT_NONE, 0);
        }
        else if (valid)
        {
            valid = at < result->count;
            if (valid)
            {
                const CFragmentReaderEvent* actual = &events[at++];
                const CFragmentCoverageNode* expected = &coverage->nodes[token];
                const uint32_t auxiliary = expected->auxiliary < count ? parser->tokens.sources[expected->auxiliary] : BUSTER_C_FRAGMENT_NONE;
                valid = expected->source < count && actual->phase == action.phase && actual->token == token && actual->child == action.child && actual->ordinal == action.ordinal &&
                        actual->kind == expected->kind && actual->boundary == coverage->nodes[root].boundary && actual->reference == expected->type_root &&
                        actual->source == expected->source && actual->expanded == parser->tokens.sources[expected->source] && actual->auxiliary == expected->auxiliary && actual->expanded_auxiliary == auxiliary;
            }
        }
    }
    return valid && at == result->count && nodes == result->nodes;
}

static CFragmentReaderStatus c_fragment_reader_test_next(CFragmentReaderCursor* cursor, CFragmentReaderEvent* event, uint32_t mode)
{
    CFragmentReaderStatus status;
    if (mode < 2)
    {
        status = c_fragment_reader_next_flat(cursor, event);
    }
    else if (mode == 2)
    {
        status = c_fragment_reader_next_direct_scalar(cursor, event);
    }
    else
    {
        status = c_fragment_reader_next_direct_vector(cursor, event);
    }
    return status;
}

static bool c_fragment_reader_test_cleared(const CFragmentReaderEvent* event)
{
    const uint8_t* bytes = (const uint8_t*)event;
    bool valid = true;
    for (uint32_t i = 0; i < sizeof(*event); i += 1)
    {
        valid = valid && bytes[i] == 0;
    }
    return valid;
}

// Moving the paused cursor between two addresses after every event catches window/phase state kept outside
// the cursor. Only this test collector materializes a trace; its expected events still come from the raw tree.
static CFragmentReaderResult c_fragment_reader_test_stream(CFragmentDirectReader* reader, const uint8_t* kinds, const uint32_t* tokens, uint32_t positions,
                                                          CFragmentReaderVisits* visits, CFragmentReaderFrame* stack, uint32_t stack_capacity,
                                                          CFragmentReaderEvent* events, uint32_t event_capacity, uint32_t mode)
{
    CFragmentReaderCursor cursors[2];
    cursors[0] = c_fragment_reader_cursor(reader, kinds, tokens, positions, visits, stack, stack_capacity, mode < 2);
    uint32_t current = 0;
    uint32_t written = 0;
    bool valid = cursors[0].result.valid;
    bool finished = false;
    for (uint64_t turn = 0; valid && !finished && turn <= (uint64_t)event_capacity; turn += 1)
    {
        CFragmentReaderEvent event;
        memset(&event, 0xa5, sizeof(event));
        CFragmentReaderCursor* cursor = &cursors[current];
        const CFragmentReaderStatus status = c_fragment_reader_test_next(cursor, &event, mode);
        if (status == C_FRAGMENT_READER_EVENT)
        {
            valid = written < event_capacity && cursor->result.count == written + 1 && event.phase <= C_FRAGMENT_READER_ATTACH;
            if (valid)
            {
                events[written++] = event;
                cursors[current ^ 1] = *cursor;
                memset(cursor, 0xa5, sizeof(*cursor));
                current ^= 1;
            }
        }
        else
        {
            finished = status == C_FRAGMENT_READER_END;
            valid = finished && c_fragment_reader_test_cleared(&event) && cursor->result.count == written;
            memset(&event, 0xa5, sizeof(event));
            const CFragmentReaderStatus repeated = c_fragment_reader_test_next(cursor, &event, mode);
            valid = valid && repeated == status && c_fragment_reader_test_cleared(&event) && cursor->result.count == written;
        }
    }
    CFragmentReaderResult result = cursors[current].result;
    result.count = written;
    result.valid = valid && finished && result.valid;
    return result;
}

static bool c_fragment_reader_test_file(const Parser* parser, const CFragmentSink* sink, const CFragmentCoverage* coverage, const uint32_t* parents, Arena* arena, CFragmentReaderTestStatistics* total)
{
    CFragmentReaderTestStatistics pending = *total;
    CFragmentReaderTestStatistics* statistics = &pending;
    const uint32_t count = parser->tokens.count;
    const uint64_t mark = arena->length;
    const uint64_t slots = (uint64_t)count + 1;
    Arena temporary = arena_reserve(1ull << 36);
    CFragmentReaderSource source;
    bool valid = count < UINT32_MAX / 5 - 2 && temporary.capacity != 0 && c_fragment_reader_source(&source, parser, sink, arena, &temporary);
    const uint32_t positions_capacity = valid ? 2 * count + 4 : 0;
    const uint32_t events_capacity = valid ? 5 * count + 1 : 0;
    CFragmentReaderTestTree tree =
    {
        .roots = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : 0,
        .boundaries = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : 0,
        .last = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : 0,
        .previous = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : 0,
        .children = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : 0,
    };
    uint8_t* flat = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, positions_capacity) : 0;
    uint32_t* locations = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, positions_capacity) : 0;
    CFragmentReaderFrame* stack = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, CFragmentReaderFrame, slots) : 0;
    CFragmentReaderEvent* events = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, CFragmentReaderEvent, events_capacity) : 0;
    CFragmentReaderTestAction* work = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, CFragmentReaderTestAction, events_capacity) : 0;
    CFragmentReaderVisits visits =
    {
        .states = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots) : 0,
        .nodes = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots) : 0,
        .roots = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots) : 0,
        .count = count,
        .capacity = count + 1,
    };
    valid = valid && tree.roots != 0 && tree.boundaries != 0 && tree.last != 0 && tree.previous != 0 && tree.children != 0 && flat != 0 && locations != 0 &&
            stack != 0 && events != 0 && work != 0 && visits.states != 0 && visits.nodes != 0 && visits.roots != 0;
    if (valid)
    {
        memset(tree.roots, 0xff, slots * sizeof(uint32_t));
        memset(tree.boundaries, 0xff, slots * sizeof(uint32_t));
        memset(tree.last, 0xff, slots * sizeof(uint32_t));
        memset(tree.previous, 0xff, slots * sizeof(uint32_t));
        memset(tree.children, 0, slots * sizeof(uint32_t));
        for (uint32_t order = count; valid && order != 0; order -= 1)
        {
            const uint32_t token = parser->postorder[order - 1];
            const CFragmentCoverageNode* expected = &coverage->nodes[token];
            valid = source.kinds[token] == expected->kind;
            if (expected->kind < C_FRAGMENT_KIND_COUNT)
            {
                const uint32_t root = parents[token] < count ? tree.roots[parents[token]] : token;
                valid = valid && root < count && source.owners[token] == root;
                tree.roots[token] = root;
                if (valid && root == token)
                {
                    const uint32_t boundary = expected->boundary;
                    valid = boundary < count && tree.boundaries[boundary] == BUSTER_C_FRAGMENT_NONE && source.boundaries[root] == boundary && source.owners[boundary] == root;
                    if (valid)
                    {
                        tree.boundaries[boundary] = root;
                    }
                }
            }
        }
        for (uint32_t order = 0; valid && order < count; order += 1)
        {
            const uint32_t token = parser->postorder[order];
            const uint32_t parent = parents[token];
            if (coverage->nodes[token].kind < C_FRAGMENT_KIND_COUNT && parent < count)
            {
                tree.previous[token] = tree.last[parent];
                tree.last[parent] = token;
                tree.children[parent] += 1;
            }
        }
        // Verify the source leaf chain independently, including exact first/next handles and strict source order.
        memset(visits.roots, 0xff, slots * sizeof(uint32_t));
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            const uint8_t kind = coverage->nodes[token].kind;
            if (kind < C_FRAGMENT_KIND_COUNT && c_fragment_arities[kind] == 0)
            {
                const uint32_t root = tree.roots[token];
                const uint32_t previous = visits.roots[root];
                valid = previous == BUSTER_C_FRAGMENT_NONE ? source.first[root] == token : source.next[previous] == token;
                visits.roots[root] = token;
            }
        }
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            if (tree.roots[token] == token)
            {
                const uint32_t last = visits.roots[token];
                valid = last == BUSTER_C_FRAGMENT_NONE ? source.first[token] == BUSTER_C_FRAGMENT_NONE : source.next[last] == BUSTER_C_FRAGMENT_NONE;
            }
        }
    }
    for (uint32_t pass = 0; valid && pass < 8; pass += 1)
    {
        const uint32_t mode = pass & 3;
        const bool stream = pass >= 4;
        memset(visits.states, 0, slots);
        memset(visits.nodes, 0, slots);
        visits.depth = 0;
        // Reversing publication order also exercises inner opaque expressions before/after their outer root.
        for (uint32_t i = 0; valid && i < sink->expression_count; i += 1)
        {
            const uint32_t e = mode & 1 ? sink->expression_count - i - 1 : i;
            const CFragmentExpression* expression = &sink->expressions[e];
            const uint32_t boundary = expression->root;
            valid = boundary < count && tree.boundaries[boundary] < count;
            if (valid)
            {
                const uint32_t root = tree.boundaries[boundary];
                CFragmentHandle handle = expression->handle;
                handle.leaf = BUSTER_C_FRAGMENT_NONE;
                CFragmentStore store = sink->store;
                store.leaf_kinds = 0;
                CFragmentDirectReader reader = c_fragment_direct_reader(&store, &handle, &source, boundary);
                valid = reader.valid && c_fragment_reader_claim(&visits, &reader.leaves);
                CFragmentReaderResult result = { 0 };
                if (valid && mode < 2)
                {
                    CFragmentDirectReader flatten = reader;
                    valid = mode == 0 ? c_fragment_reader_flatten(&flatten, flat, locations, positions_capacity, false) :
                                        c_fragment_reader_flatten(&flatten, flat, locations, positions_capacity, true);
                    for (uint32_t position = 0; valid && position < handle.positions; position += 1)
                    {
                        if (flat[position] != C_FRAGMENT_END && c_fragment_arities[flat[position]] == 0)
                        {
                            locations[position] = UINT32_MAX - 1;
                        }
                    }
                    if (valid && !stream)
                    {
                        result = c_fragment_reader_flat(&reader, flat, locations, handle.positions, &visits, stack, count + 1, events, events_capacity);
                    }
                }
                else if (valid && !stream && mode == 2)
                {
                    result = c_fragment_reader_direct_scalar(&reader, handle.positions, &visits, stack, count + 1, events, events_capacity);
                }
                else if (valid && !stream)
                {
                    result = c_fragment_reader_direct_vector(&reader, handle.positions, &visits, stack, count + 1, events, events_capacity);
                }
                if (valid && stream)
                {
                    result = c_fragment_reader_test_stream(&reader, flat, locations, handle.positions, &visits, stack, count + 1, events, events_capacity, mode);
                }
                valid = valid && c_fragment_reader_test_events(parser, coverage, &tree, root, events, &result, work, events_capacity) &&
                        c_fragment_reader_complete(&visits, &reader.leaves) && !c_fragment_reader_claim(&visits, &reader.leaves);
                if (valid)
                {
                    statistics->traversals += 1;
                    statistics->events += result.count;
                    for (uint32_t k = 0; k < result.count; k += 1)
                    {
                        statistics->references += events[k].phase == C_FRAGMENT_READER_REFERENCE;
                    }
                    c_fragment_reader_test_peak(&statistics->flat_peak, (uint64_t)handle.positions * (sizeof(uint8_t) + sizeof(uint32_t)));
                    c_fragment_reader_test_peak(&statistics->frame_peak, (uint64_t)result.stack_peak * sizeof(CFragmentReaderFrame));
                    c_fragment_reader_test_peak(&statistics->event_peak, (uint64_t)result.count * sizeof(CFragmentReaderEvent));
                }
                else
                {
                    fprintf(stderr, "fragment reader: mode=%u stream=%u expression=%u boundary=%u root=%u payload=%u events=%u nodes=%u\n", mode, stream, e, boundary, root, store.payload, result.count, result.nodes);
                }
            }
        }
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            valid = tree.roots[token] != token || visits.states[token] == C_FRAGMENT_READER_DONE;
            valid = valid && visits.nodes[token] == (coverage->nodes[token].kind < C_FRAGMENT_KIND_COUNT ? C_FRAGMENT_READER_DONE : C_FRAGMENT_READER_UNSEEN);
        }
        valid = valid && visits.depth == 0;
    }
    if (valid)
    {
        c_fragment_reader_test_peak(&statistics->source_peak, source.retained_bytes);
        c_fragment_reader_test_peak(&statistics->temporary_peak, source.temporary_bytes);
        c_fragment_reader_test_peak(&statistics->borrowed_peak, source.borrowed_bytes);
        c_fragment_reader_test_peak(&statistics->preparation_input_peak, source.preparation_input_bytes);
        c_fragment_reader_test_peak(&statistics->flat_backing_peak, (uint64_t)positions_capacity * (sizeof(uint8_t) + sizeof(uint32_t)));
        c_fragment_reader_test_peak(&statistics->frame_backing_peak, slots * sizeof(CFragmentReaderFrame));
        c_fragment_reader_test_peak(&statistics->visits_backing_peak, slots * (2 * sizeof(uint8_t) + sizeof(uint32_t)));
        c_fragment_reader_test_peak(&statistics->cursor_peak, sizeof(CFragmentReaderCursor));
        c_fragment_reader_test_peak(&statistics->test_arena_peak, arena->length - mark);
        *total = pending;
    }
    else
    {
        fprintf(stderr, "fragment reader: source/tree/event comparison failed, tokens=%u payload=%u\n", count, sink->store.payload);
    }
    arena_pop_to(arena, mark);
    arena_release(&temporary);
    return valid;
}

#define BUSTER_C_FRAGMENT_READER_TEST_TOKENS 128u

typedef struct CFragmentReaderUnit CFragmentReaderUnit;
struct CFragmentReaderUnit
{
    Parser parser;
    CFragmentReaderSource source;
    CFragmentSyntax syntax[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint8_t kinds[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint32_t owners[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint32_t next[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint32_t first[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint32_t boundaries[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint32_t expanded[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    CFragmentReaderSpan spans[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint64_t record_words[24];
    uint32_t locations[192];
    uint8_t live[3];
    CFragmentStore store;
    CFragmentHandle handle;
    uint32_t boundary;
    uint8_t visit_states[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint8_t visit_nodes[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint32_t visit_roots[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    CFragmentReaderVisits visits;
    CFragmentReaderFrame stack[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    CFragmentReaderEvent events[5 * BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
};

static void c_fragment_reader_unit_initialize(CFragmentReaderUnit* unit)
{
    memset(unit, 0, sizeof(*unit));
    memset(unit->kinds, C_FRAGMENT_KIND_COUNT, sizeof(unit->kinds));
    memset(unit->owners, 0xff, sizeof(unit->owners));
    memset(unit->next, 0xff, sizeof(unit->next));
    memset(unit->first, 0xff, sizeof(unit->first));
    memset(unit->boundaries, 0xff, sizeof(unit->boundaries));
    memset(unit->locations, 0xff, sizeof(unit->locations));
    unit->parser.tokens.count = BUSTER_C_FRAGMENT_READER_TEST_TOKENS;
    unit->parser.tokens.sources = unit->expanded;
    unit->source = (CFragmentReaderSource)
    {
        .parser = &unit->parser,
        .syntax = unit->syntax,
        .kinds = unit->kinds,
        .owners = unit->owners,
        .next = unit->next,
        .first = unit->first,
        .boundaries = unit->boundaries,
        .spans = unit->spans,
        .count = BUSTER_C_FRAGMENT_READER_TEST_TOKENS,
        .valid = true,
    };
    unit->store = (CFragmentStore)
    {
        .records = (uint8_t*)unit->record_words,
        .identities = unit->locations,
        .live = unit->live,
        .capacity = 3,
        .allocated = 1,
        .payload = 16,
        .stride = sizeof(CFragmentHeader) + 16,
        .valid = true,
    };
    unit->live[0] = 1;
    unit->handle = (CFragmentHandle)
    {
        .head = 0,
        .tail = 0,
        .leaf = BUSTER_C_FRAGMENT_NONE,
        .frozen = true,
    };
    for (uint32_t token = 0; token < BUSTER_C_FRAGMENT_READER_TEST_TOKENS; token += 1)
    {
        unit->expanded[token] = 1000 + token;
        unit->syntax[token].source = token;
        unit->syntax[token].auxiliary = BUSTER_C_FRAGMENT_NONE;
        unit->syntax[token].type_root = BUSTER_C_FRAGMENT_NONE;
        unit->spans[token] = (CFragmentReaderSpan)
        {
            .begin = token,
            .end = token + 1,
        };
    }
}

static CFragmentDirectReader c_fragment_reader_unit_begin(CFragmentReaderUnit* unit)
{
    memset(unit->visit_states, 0, sizeof(unit->visit_states));
    memset(unit->visit_nodes, 0, sizeof(unit->visit_nodes));
    unit->visits = (CFragmentReaderVisits)
    {
        .states = unit->visit_states,
        .nodes = unit->visit_nodes,
        .roots = unit->visit_roots,
        .count = BUSTER_C_FRAGMENT_READER_TEST_TOKENS,
        .capacity = BUSTER_C_FRAGMENT_READER_TEST_TOKENS,
    };
    CFragmentDirectReader reader = c_fragment_direct_reader(&unit->store, &unit->handle, &unit->source, unit->boundary);
    reader.valid = reader.valid && c_fragment_reader_claim(&unit->visits, &reader.leaves);
    return reader;
}

static CFragmentReaderResult c_fragment_reader_unit_flat(CFragmentReaderUnit* unit, const uint8_t* kinds, const uint32_t* locations, uint32_t positions, uint32_t frames, uint32_t events)
{
    CFragmentDirectReader reader = c_fragment_reader_unit_begin(unit);
    return c_fragment_reader_flat(&reader, kinds, locations, positions, &unit->visits, unit->stack, frames, unit->events, events);
}

static CFragmentReaderResult c_fragment_reader_unit_direct(CFragmentReaderUnit* unit, bool vector)
{
    CFragmentDirectReader reader = c_fragment_reader_unit_begin(unit);
    const CFragmentReaderResult result = vector ? c_fragment_reader_direct_vector(&reader, unit->handle.positions, &unit->visits, unit->stack, BUSTER_C_FRAGMENT_READER_TEST_TOKENS, unit->events, 5 * BUSTER_C_FRAGMENT_READER_TEST_TOKENS) :
                                               c_fragment_reader_direct_scalar(&reader, unit->handle.positions, &unit->visits, unit->stack, BUSTER_C_FRAGMENT_READER_TEST_TOKENS, unit->events, 5 * BUSTER_C_FRAGMENT_READER_TEST_TOKENS);
    return result;
}

static void c_fragment_reader_unit_binary(CFragmentReaderUnit* unit)
{
    c_fragment_reader_unit_initialize(unit);
    unit->kinds[0] = C_FRAGMENT_NUMBER;
    unit->kinds[1] = C_FRAGMENT_ADD;
    unit->kinds[2] = C_FRAGMENT_NUMBER;
    unit->owners[0] = 1;
    unit->owners[1] = 1;
    unit->owners[2] = 1;
    unit->first[1] = 0;
    unit->next[0] = 2;
    unit->boundaries[1] = 1;
    unit->spans[1].begin = 0;
    unit->spans[1].end = 3;
    unit->boundary = 1;
    unit->handle.positions = 3;
    *c_fragment_header(&unit->store, 0) = (CFragmentHeader)
    {
        .operator_mask = 1,
        .next = BUSTER_C_FRAGMENT_NONE,
        .used = 3,
        .operator_count = 1,
    };
    c_fragment_operators(&unit->store, 0)[0] = C_FRAGMENT_ADD;
    unit->locations[0] = 1;
    // All leaf locations and the legacy leaf-kind column remain absent/poisoned.
}

static void c_fragment_reader_unit_check(uint32_t* checks, uint32_t* failures, bool valid, const char* description)
{
    *checks += 1;
    *failures += !valid;
    if (!valid && *failures <= 32)
    {
        fprintf(stderr, "fragment-reader unit: %s\n", description);
    }
}

static CFragmentReaderCursor c_fragment_reader_unit_cursor(CFragmentReaderUnit* unit, CFragmentDirectReader* reader, uint8_t* kinds, uint32_t* tokens, uint32_t mode)
{
    *reader = c_fragment_reader_unit_begin(unit);
    if (mode < 2)
    {
        CFragmentDirectReader flatten = *reader;
        reader->valid = mode == 0 ? c_fragment_reader_flatten(&flatten, kinds, tokens, BUSTER_C_FRAGMENT_READER_TEST_TOKENS, false) :
                                   c_fragment_reader_flatten(&flatten, kinds, tokens, BUSTER_C_FRAGMENT_READER_TEST_TOKENS, true);
        for (uint32_t position = 0; reader->valid && position < unit->handle.positions; position += 1)
        {
            if (kinds[position] != C_FRAGMENT_END && c_fragment_arities[kinds[position]] == 0)
            {
                tokens[position] = UINT32_MAX - 1;
            }
        }
    }
    return c_fragment_reader_cursor(reader, kinds, tokens, unit->handle.positions, &unit->visits, unit->stack, BUSTER_C_FRAGMENT_READER_TEST_TOKENS, mode < 2);
}

static bool c_fragment_reader_unit_action(const CFragmentReaderEvent* event, const CFragmentReaderTestAction* expected, uint32_t expanded_base, uint32_t reference)
{
    const uint8_t kind = expected->token == 1 ? C_FRAGMENT_ADD : C_FRAGMENT_NUMBER;
    return event->phase == expected->phase && event->token == expected->token && event->child == expected->child && event->ordinal == expected->ordinal && event->kind == kind &&
           event->boundary == 1 && event->source == expected->token && event->expanded == expanded_base + expected->token && event->auxiliary == BUSTER_C_FRAGMENT_NONE &&
           event->expanded_auxiliary == BUSTER_C_FRAGMENT_NONE && event->reference == (expected->token == 1 ? reference : BUSTER_C_FRAGMENT_NONE);
}

static bool c_fragment_reader_unit_rejected(CFragmentReaderCursor* cursor, uint32_t mode)
{
    CFragmentReaderStatus status = C_FRAGMENT_READER_EVENT;
    CFragmentReaderEvent event;
    bool valid = true;
    for (uint32_t advance = 0; valid && status == C_FRAGMENT_READER_EVENT && advance < 5 * BUSTER_C_FRAGMENT_READER_TEST_TOKENS + 1; advance += 1)
    {
        memset(&event, 0xa5, sizeof(event));
        status = c_fragment_reader_test_next(cursor, &event, mode);
        valid = status != C_FRAGMENT_READER_EVENT || event.phase != C_FRAGMENT_READER_ATTACH;
    }
    valid = valid && status == C_FRAGMENT_READER_ERROR && !cursor->result.valid && c_fragment_reader_test_cleared(&event);
    const uint32_t consumed = cursor->consumed;
    const uint32_t count = cursor->result.count;
    const uint32_t depth = cursor->depth;
    for (uint32_t repeat = 0; repeat < 3; repeat += 1)
    {
        memset(&event, 0xa5, sizeof(event));
        status = c_fragment_reader_test_next(cursor, &event, mode);
        valid = valid && status == C_FRAGMENT_READER_ERROR && c_fragment_reader_test_cleared(&event) && cursor->consumed == consumed && cursor->result.count == count && cursor->depth == depth;
    }
    return valid;
}

static bool c_fragment_reader_unit_reject(CFragmentReaderUnit* unit, const uint8_t* kinds, const uint32_t* tokens, uint32_t positions, uint32_t frames, uint32_t mode)
{
    CFragmentDirectReader reader = c_fragment_reader_unit_begin(unit);
    CFragmentReaderCursor cursor = c_fragment_reader_cursor(&reader, kinds, tokens, positions, &unit->visits, unit->stack, frames, mode < 2);
    return c_fragment_reader_unit_rejected(&cursor, mode);
}

static bool c_fragment_reader_unit_step(CFragmentReaderCursor* cursor, uint32_t mode, uint8_t phase, uint32_t token, uint32_t child, uint32_t ordinal)
{
    CFragmentReaderEvent event;
    memset(&event, 0xa5, sizeof(event));
    return c_fragment_reader_test_next(cursor, &event, mode) == C_FRAGMENT_READER_EVENT && event.phase == phase && event.token == token && event.child == child && event.ordinal == ordinal &&
           event.source == token && event.expanded == 1000 + token && event.boundary == 0 && event.reference == BUSTER_C_FRAGMENT_NONE;
}

static void c_fragment_reader_cursor_tests(uint32_t* checks, uint32_t* failures, const CFragmentReaderTestAction* binary, uint32_t binary_count)
{
    static CFragmentReaderUnit units[2];
    uint8_t kinds[2][BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint32_t tokens[2][BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    CFragmentReaderEvent event;
    const CFragmentReaderTestAction reference =
    {
        .token = 1,
        .child = BUSTER_C_FRAGMENT_NONE,
        .phase = C_FRAGMENT_READER_REFERENCE,
    };
    for (uint32_t mode = 0; mode < 4; mode += 1)
    {
        // Separate sources and ledgers, alternating after every event, expose accidentally shared phase/window
        // state. The first stream has an extra REFERENCE and different expanded-source handles from the second.
        c_fragment_reader_unit_binary(&units[0]);
        c_fragment_reader_unit_binary(&units[1]);
        units[0].syntax[1].type_root = 3;
        for (uint32_t token = 0; token < BUSTER_C_FRAGMENT_READER_TEST_TOKENS; token += 1)
        {
            units[1].expanded[token] = 9000 + token;
        }
        CFragmentDirectReader readers[2];
        CFragmentReaderCursor cursors[2];
        cursors[0] = c_fragment_reader_unit_cursor(&units[0], &readers[0], kinds[0], tokens[0], mode);
        cursors[1] = c_fragment_reader_unit_cursor(&units[1], &readers[1], kinds[1], tokens[1], 3 - mode);
        bool valid = cursors[0].result.valid && cursors[1].result.valid;
        uint32_t phases = 0;
        for (uint32_t at = 0; valid && at < binary_count + 1; at += 1)
        {
            const CFragmentReaderTestAction* expected = at == 1 ? &reference : &binary[at - (at > 1)];
            memset(&event, 0xa5, sizeof(event));
            valid = c_fragment_reader_test_next(&cursors[0], &event, mode) == C_FRAGMENT_READER_EVENT &&
                    c_fragment_reader_unit_action(&event, expected, 1000, 3) && cursors[0].result.count == at + 1;
            if (valid)
            {
                phases |= 1u << event.phase;
                valid = units[0].visit_nodes[0] == (at < 2 ? C_FRAGMENT_READER_UNSEEN : at < 4 ? C_FRAGMENT_READER_ACTIVE : C_FRAGMENT_READER_DONE) &&
                        units[0].visit_nodes[2] == (at < 6 ? C_FRAGMENT_READER_UNSEEN : at < 8 ? C_FRAGMENT_READER_ACTIVE : C_FRAGMENT_READER_DONE) &&
                        units[0].visit_nodes[1] == (at < 11 ? C_FRAGMENT_READER_ACTIVE : C_FRAGMENT_READER_DONE) && cursors[0].result.nodes == (at < 2 ? 1 : at < 6 ? 2 : 3);
            }
            if (valid && mode >= 2 && at < 2)
            {
                valid = cursors[0].window_at == 1 && cursors[0].window_count == 3 && readers[0].remaining == 0 && readers[0].leaves.next == BUSTER_C_FRAGMENT_NONE;
            }
            if (valid && at < binary_count)
            {
                memset(&event, 0xa5, sizeof(event));
                valid = c_fragment_reader_test_next(&cursors[1], &event, 3 - mode) == C_FRAGMENT_READER_EVENT &&
                        c_fragment_reader_unit_action(&event, &binary[at], 9000, BUSTER_C_FRAGMENT_NONE) && cursors[1].result.count == at + 1;
            }
        }
        valid = valid && phases == (1u << (C_FRAGMENT_READER_ATTACH + 1)) - 1 && c_fragment_reader_complete(&units[0].visits, &readers[0].leaves) &&
                c_fragment_reader_complete(&units[1].visits, &readers[1].leaves);
        units[0].kinds[4] = C_FRAGMENT_NUMBER;
        units[0].owners[4] = 4;
        units[0].boundaries[4] = 4;
        units[0].first[4] = 4;
        CFragmentLeafCursor later = c_fragment_leaf_cursor(&units[0].source, 4);
        valid = valid && c_fragment_reader_claim(&units[0].visits, &later);
        for (uint32_t repeat = 0; repeat < 3; repeat += 1)
        {
            for (uint32_t index = 0; index < 2; index += 1)
            {
                memset(&event, 0xa5, sizeof(event));
                const uint32_t selected = index == 0 ? mode : 3 - mode;
                const CFragmentReaderStatus status = c_fragment_reader_test_next(&cursors[index], &event, selected);
                valid = valid && status == C_FRAGMENT_READER_END && c_fragment_reader_test_cleared(&event) && cursors[index].result.count == binary_count + (index == 0);
            }
        }
        valid = valid && units[0].visits.depth == 1 && units[0].visit_states[4] == C_FRAGMENT_READER_ACTIVE && units[0].visit_nodes[4] == C_FRAGMENT_READER_UNSEEN;
        c_fragment_reader_unit_check(checks, failures, valid, "interleaved event cursors lost a phase/window/source identity or repeated ATTACH after claim release");

        // A reference owns a raw opaque interval containing a separately published leaf expression. The child
        // has its own frame slice and cursor but shares root/node visitation. Direct read-ahead must survive it.
        for (uint32_t scenario = 0; scenario < 4; scenario += 1)
        {
            const bool cast = scenario >= 2;
            const bool violation = scenario & 1;
            const uint8_t root_kind = cast ? C_FRAGMENT_CAST : C_FRAGMENT_TYPE_NAME;
            CFragmentReaderUnit* unit = &units[0];
            c_fragment_reader_unit_initialize(unit);
            unit->boundary = 1;
            unit->kinds[1] = root_kind;
            unit->owners[1] = 1;
            unit->boundaries[1] = 1;
            unit->first[1] = cast ? 2 : 1;
            unit->handle.positions = cast ? 2 : 1;
            if (cast)
            {
                unit->kinds[2] = C_FRAGMENT_NUMBER;
                unit->owners[2] = 1;
                *c_fragment_header(&unit->store, 0) = (CFragmentHeader)
                {
                    .operator_mask = 1,
                    .next = BUSTER_C_FRAGMENT_NONE,
                    .used = 2,
                    .operator_count = 1,
                };
                c_fragment_operators(&unit->store, 0)[0] = C_FRAGMENT_CAST;
                unit->locations[0] = 1;
            }
            else
            {
                unit->handle.head = BUSTER_C_FRAGMENT_NONE;
                unit->handle.tail = BUSTER_C_FRAGMENT_NONE;
            }
            unit->syntax[1].type_root = 3;
            unit->spans[1].end = 6;
            unit->spans[3].end = 6;
            unit->kinds[4] = C_FRAGMENT_NUMBER;
            unit->owners[4] = 4;
            unit->first[4] = 4;
            unit->boundaries[4] = 4;
            CFragmentDirectReader outer;
            CFragmentReaderCursor outer_cursor = c_fragment_reader_unit_cursor(unit, &outer, kinds[0], tokens[0], mode);
            valid = c_fragment_reader_test_next(&outer_cursor, &event, mode) == C_FRAGMENT_READER_EVENT && event.phase == C_FRAGMENT_READER_ENTER && event.token == 1 && event.kind == root_kind &&
                    c_fragment_reader_test_next(&outer_cursor, &event, mode) == C_FRAGMENT_READER_EVENT && event.phase == C_FRAGMENT_READER_REFERENCE && event.token == 1 && event.reference == 3 &&
                    outer_cursor.depth == 1 && unit->visit_nodes[1] == C_FRAGMENT_READER_ACTIVE && unit->visit_nodes[2] == C_FRAGMENT_READER_UNSEEN;
            CFragmentLeafCursor nested = outer.leaves;
            CFragmentLeafCursor saved[1];
            uint32_t nested_depth = 0;
            valid = valid && c_fragment_leaf_enter(&nested, 3, 4, saved, &nested_depth, 1);
            const CFragmentHandle handle =
            {
                .head = BUSTER_C_FRAGMENT_NONE,
                .tail = BUSTER_C_FRAGMENT_NONE,
                .positions = 1,
                .leaf = BUSTER_C_FRAGMENT_NONE,
                .frozen = true,
            };
            CFragmentDirectReader inner = c_fragment_direct_reader(&unit->store, &handle, &unit->source, 4);
            valid = valid && inner.valid && nested.root == inner.leaves.root && c_fragment_reader_claim(&unit->visits, &inner.leaves);
            CFragmentReaderFrame inner_stack[1];
            const uint8_t inner_kind[1] = { C_FRAGMENT_NUMBER };
            const uint32_t inner_token[1] = { UINT32_MAX - 1 };
            CFragmentReaderCursor inner_cursor = c_fragment_reader_cursor(&inner, inner_kind, inner_token, 1, &unit->visits, inner_stack, 1, mode < 2);
            const uint32_t consumed = outer_cursor.consumed;
            const uint32_t window_at = outer_cursor.window_at;
            const uint32_t read = outer.leaves.read;
            if (violation != 0)
            {
                valid = valid && c_fragment_reader_unit_rejected(&outer_cursor, mode) && outer_cursor.consumed == consumed && outer_cursor.window_at == window_at && outer.leaves.read == read;
            }
            static const uint8_t child_phases[] =
            {
                C_FRAGMENT_READER_ENTER,
                C_FRAGMENT_READER_REDUCE,
                C_FRAGMENT_READER_LEAVE,
                C_FRAGMENT_READER_ATTACH,
            };
            for (uint32_t at = 0; valid && at < sizeof(child_phases); at += 1)
            {
                valid = c_fragment_reader_test_next(&inner_cursor, &event, mode) == C_FRAGMENT_READER_EVENT && event.phase == child_phases[at] && event.token == 4 &&
                        event.boundary == 4 && event.child == BUSTER_C_FRAGMENT_NONE && event.source == 4 && event.expanded == 1004;
            }
            valid = valid && c_fragment_reader_complete(&unit->visits, &inner.leaves) && !c_fragment_reader_claim(&unit->visits, &inner.leaves);
            nested = inner.leaves;
            valid = valid && c_fragment_leaf_leave(&nested, saved, &nested_depth, 1) && nested_depth == 0 && nested.root == outer.leaves.root &&
                    nested.next == outer.leaves.next && nested.previous == outer.leaves.previous && nested.read == outer.leaves.read;
            if (violation == 0)
            {
                static const CFragmentReaderTestAction after_reference[] =
                {
                    {
                        .token = 2,
                        .child = BUSTER_C_FRAGMENT_NONE,
                        .phase = C_FRAGMENT_READER_ENTER,
                    },
                    {
                        .token = 2,
                        .child = BUSTER_C_FRAGMENT_NONE,
                        .phase = C_FRAGMENT_READER_REDUCE,
                    },
                    {
                        .token = 2,
                        .child = BUSTER_C_FRAGMENT_NONE,
                        .phase = C_FRAGMENT_READER_LEAVE,
                    },
                    {
                        .token = 1,
                        .child = 2,
                        .phase = C_FRAGMENT_READER_CHILD,
                    },
                    {
                        .token = 1,
                        .child = BUSTER_C_FRAGMENT_NONE,
                        .phase = C_FRAGMENT_READER_REDUCE,
                    },
                    {
                        .token = 1,
                        .child = BUSTER_C_FRAGMENT_NONE,
                        .phase = C_FRAGMENT_READER_LEAVE,
                    },
                    {
                        .token = 1,
                        .child = BUSTER_C_FRAGMENT_NONE,
                        .phase = C_FRAGMENT_READER_ATTACH,
                    },
                };
                for (uint32_t at = cast ? 0 : 4; valid && at < sizeof(after_reference) / sizeof(after_reference[0]); at += 1)
                {
                    const CFragmentReaderTestAction* expected = &after_reference[at];
                    valid = c_fragment_reader_test_next(&outer_cursor, &event, mode) == C_FRAGMENT_READER_EVENT && event.phase == expected->phase && event.token == expected->token &&
                            event.child == expected->child && event.ordinal == 0 && event.kind == (event.token == 1 ? root_kind : C_FRAGMENT_NUMBER) &&
                            event.source == expected->token && event.expanded == 1000 + expected->token && event.reference == (event.token == 1 ? 3 : BUSTER_C_FRAGMENT_NONE);
                }
                valid = valid && c_fragment_reader_complete(&unit->visits, &outer.leaves) && unit->visits.depth == 0 && outer_cursor.result.count == (cast ? 9 : 5);
            }
            else
            {
                // Restoring the ledger cannot revive a cursor that attempted to resume under another root.
                valid = valid && c_fragment_reader_unit_rejected(&outer_cursor, mode);
            }
            memset(&event, 0xa5, sizeof(event));
            valid = valid && c_fragment_reader_test_next(&inner_cursor, &event, mode) == C_FRAGMENT_READER_END && c_fragment_reader_test_cleared(&event);
            c_fragment_reader_unit_check(checks, failures, valid, violation == 0 ? "opaque nested traversal damaged a suspended outer event cursor" : "outer resume bypassed the active nested root or recovered from ERROR");
        }

        // Fail each active phase separately; no resume may publish a stale event or make progress after a root
        // claim is removed. ATTACH is terminal already, and is covered by the stable END checks above.
        for (uint32_t pause = 0; pause < binary_count; pause += 1)
        {
            c_fragment_reader_unit_binary(&units[0]);
            units[0].syntax[1].type_root = 3;
            CFragmentDirectReader reader;
            CFragmentReaderCursor cursor = c_fragment_reader_unit_cursor(&units[0], &reader, kinds[0], tokens[0], mode);
            valid = cursor.result.valid;
            for (uint32_t at = 0; valid && at <= pause; at += 1)
            {
                valid = c_fragment_reader_test_next(&cursor, &event, mode) == C_FRAGMENT_READER_EVENT;
            }
            const uint32_t consumed = cursor.consumed;
            const uint32_t emitted = cursor.result.count;
            units[0].visits.depth = 0;
            valid = valid && c_fragment_reader_unit_rejected(&cursor, mode) && cursor.consumed == consumed && cursor.result.count == emitted;
            c_fragment_reader_unit_check(checks, failures, valid, "paused cursor advanced after its root claim disappeared");
        }

        // 64-position refill followed by a three-position tail: a list root plus 65 source-ordered leaves and END.
        CFragmentReaderUnit* unit = &units[0];
        c_fragment_reader_unit_initialize(unit);
        unit->kinds[0] = C_FRAGMENT_INITIALIZER;
        unit->owners[0] = 0;
        unit->first[0] = 1;
        unit->boundaries[0] = 0;
        unit->spans[0].end = 66;
        unit->store.allocated = 2;
        unit->live[1] = 1;
        unit->handle.tail = 1;
        unit->handle.positions = 67;
        for (uint32_t token = 1; token <= 65; token += 1)
        {
            unit->kinds[token] = C_FRAGMENT_NUMBER;
            unit->owners[token] = 0;
            unit->next[token] = token == 65 ? BUSTER_C_FRAGMENT_NONE : token + 1;
        }
        *c_fragment_header(&unit->store, 0) = (CFragmentHeader)
        {
            .operator_mask = 1,
            .next = 1,
            .used = 64,
            .operator_count = 1,
        };
        *c_fragment_header(&unit->store, 1) = (CFragmentHeader)
        {
            .operator_mask = 4,
            .next = BUSTER_C_FRAGMENT_NONE,
            .used = 3,
            .operator_count = 1,
        };
        c_fragment_operators(&unit->store, 0)[0] = C_FRAGMENT_INITIALIZER;
        c_fragment_operators(&unit->store, 1)[0] = C_FRAGMENT_END;
        unit->locations[0] = 0;
        CFragmentDirectReader reader;
        CFragmentReaderCursor cursor = c_fragment_reader_unit_cursor(unit, &reader, kinds[0], tokens[0], mode);
        valid = c_fragment_reader_test_next(&cursor, &event, mode) == C_FRAGMENT_READER_EVENT && event.phase == C_FRAGMENT_READER_ENTER && event.token == 0;
        if (valid && mode >= 2)
        {
            valid = reader.leaves.next == 64 && reader.remaining == 3 && cursor.window_count == 64 && cursor.window_at == 1;
        }
        for (uint32_t token = 1; valid && token <= 65; token += 1)
        {
            static const uint8_t leaf_phases[] =
            {
                C_FRAGMENT_READER_ENTER,
                C_FRAGMENT_READER_REDUCE,
                C_FRAGMENT_READER_LEAVE,
                C_FRAGMENT_READER_CHILD,
            };
            for (uint32_t at = 0; valid && at < sizeof(leaf_phases); at += 1)
            {
                valid = c_fragment_reader_test_next(&cursor, &event, mode) == C_FRAGMENT_READER_EVENT && event.phase == leaf_phases[at] &&
                        event.token == (at == 3 ? 0 : token) && event.child == (at == 3 ? token : BUSTER_C_FRAGMENT_NONE) && event.ordinal == (at == 3 ? token - 1 : 0);
            }
        }
        static const uint8_t root_phases[] =
        {
            C_FRAGMENT_READER_REDUCE,
            C_FRAGMENT_READER_LEAVE,
            C_FRAGMENT_READER_ATTACH,
        };
        for (uint32_t at = 0; valid && at < sizeof(root_phases); at += 1)
        {
            valid = c_fragment_reader_test_next(&cursor, &event, mode) == C_FRAGMENT_READER_EVENT && event.phase == root_phases[at] && event.token == 0 && event.kind == C_FRAGMENT_INITIALIZER;
        }
        valid = valid && cursor.result.count == 264 && cursor.result.nodes == 66 && cursor.result.stack_peak == 2 && c_fragment_reader_complete(&unit->visits, &reader.leaves);
        memset(&event, 0xa5, sizeof(event));
        valid = valid && c_fragment_reader_test_next(&cursor, &event, mode) == C_FRAGMENT_READER_END && c_fragment_reader_test_cleared(&event);
        c_fragment_reader_unit_check(checks, failures, valid, "event suspension lost source order or END framing across the full/tail window boundary");

        // Two 64+1 layouts place END alone in the tail, or at slot 63 just before the parent's final leaf.
        // Both describe explicit trees with 64 nodes: CALL(63 leaves), and ADD(CALL(61 leaves), final leaf).
        for (uint32_t layout = 0; layout < 2; layout += 1)
        {
            c_fragment_reader_unit_initialize(unit);
            const uint32_t call = layout;
            const uint32_t first = layout + 1;
            const uint32_t last = layout == 0 ? 63 : 62;
            unit->kinds[0] = layout == 0 ? C_FRAGMENT_CALL : C_FRAGMENT_ADD;
            unit->owners[0] = 0;
            unit->kinds[call] = C_FRAGMENT_CALL;
            unit->owners[call] = 0;
            unit->first[0] = first;
            unit->boundaries[0] = 0;
            unit->spans[0].end = 64;
            unit->store.allocated = 2;
            unit->live[1] = 1;
            unit->handle.tail = 1;
            unit->handle.positions = 65;
            for (uint32_t token = first; token <= 63; token += 1)
            {
                unit->kinds[token] = C_FRAGMENT_NUMBER;
                unit->owners[token] = 0;
                unit->next[token] = token == 63 ? BUSTER_C_FRAGMENT_NONE : token + 1;
            }
            *c_fragment_header(&unit->store, 0) = (CFragmentHeader)
            {
                .operator_mask = layout == 0 ? 1 : (UINT64_C(1) << 63) | 3,
                .next = 1,
                .used = 64,
                .operator_count = layout == 0 ? 1 : 3,
            };
            *c_fragment_header(&unit->store, 1) = (CFragmentHeader)
            {
                .operator_mask = layout == 0 ? 1 : 0,
                .next = BUSTER_C_FRAGMENT_NONE,
                .used = 1,
                .operator_count = layout == 0 ? 1 : 0,
            };
            c_fragment_operators(&unit->store, 0)[0] = unit->kinds[0];
            unit->locations[0] = 0;
            if (layout == 0)
            {
                c_fragment_operators(&unit->store, 1)[0] = C_FRAGMENT_END;
            }
            else
            {
                c_fragment_operators(&unit->store, 0)[1] = C_FRAGMENT_CALL;
                c_fragment_operators(&unit->store, 0)[2] = C_FRAGMENT_END;
                unit->locations[1] = 1;
            }
            cursor = c_fragment_reader_unit_cursor(unit, &reader, kinds[0], tokens[0], mode);
            valid = c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_ENTER, 0, BUSTER_C_FRAGMENT_NONE, 0);
            if (layout != 0)
            {
                valid = valid && c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_ENTER, call, BUSTER_C_FRAGMENT_NONE, 0);
            }
            for (uint32_t token = first; valid && token <= last; token += 1)
            {
                valid = c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_ENTER, token, BUSTER_C_FRAGMENT_NONE, 0) &&
                        c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_REDUCE, token, BUSTER_C_FRAGMENT_NONE, 0) &&
                        c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_LEAVE, token, BUSTER_C_FRAGMENT_NONE, 0) &&
                        c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_CHILD, call, token, token - first);
            }
            valid = valid && c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_REDUCE, call, BUSTER_C_FRAGMENT_NONE, 0) &&
                    c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_LEAVE, call, BUSTER_C_FRAGMENT_NONE, 0);
            if (layout != 0)
            {
                valid = valid && c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_CHILD, 0, call, 0) &&
                        c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_ENTER, 63, BUSTER_C_FRAGMENT_NONE, 0) &&
                        c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_REDUCE, 63, BUSTER_C_FRAGMENT_NONE, 0) &&
                        c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_LEAVE, 63, BUSTER_C_FRAGMENT_NONE, 0) &&
                        c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_CHILD, 0, 63, 1) &&
                        c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_REDUCE, 0, BUSTER_C_FRAGMENT_NONE, 0) &&
                        c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_LEAVE, 0, BUSTER_C_FRAGMENT_NONE, 0);
            }
            valid = valid && c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_ATTACH, 0, BUSTER_C_FRAGMENT_NONE, 0) && cursor.result.count == 256 && cursor.result.nodes == 64 &&
                    c_fragment_reader_complete(&unit->visits, &reader.leaves);
            memset(&event, 0xa5, sizeof(event));
            valid = valid && c_fragment_reader_test_next(&cursor, &event, mode) == C_FRAGMENT_READER_END && c_fragment_reader_test_cleared(&event);
            c_fragment_reader_unit_check(checks, failures, valid, "END-only/final-leaf tail changed call/parent event order or repeated framing");
        }
    }

    // Initialization and next have defined null/error results before any borrowed metadata can be inspected.
    CFragmentReaderCursor invalid = c_fragment_reader_cursor(0, 0, 0, 1, 0, 0, 0, false);
    bool valid = !invalid.result.valid && c_fragment_reader_unit_rejected(&invalid, 2);
    memset(&event, 0xa5, sizeof(event));
    valid = valid && c_fragment_reader_next_direct_vector(0, &event) == C_FRAGMENT_READER_ERROR && c_fragment_reader_test_cleared(&event);
    c_fragment_reader_unit_check(checks, failures, valid, "null initialization/cursor did not produce a cleared sticky error");
    c_fragment_reader_unit_binary(&units[0]);
    CFragmentDirectReader reader;
    CFragmentReaderCursor cursor = c_fragment_reader_unit_cursor(&units[0], &reader, kinds[0], tokens[0], 2);
    valid = c_fragment_reader_next_direct_scalar(&cursor, 0) == C_FRAGMENT_READER_ERROR && cursor.result.count == 0 && c_fragment_reader_unit_rejected(&cursor, 2);
    c_fragment_reader_unit_check(checks, failures, valid, "null event output advanced the cursor or failed to poison it");
    for (uint32_t mode = 0; mode < 4; mode += 1)
    {
        c_fragment_reader_unit_binary(&units[0]);
        cursor = c_fragment_reader_unit_cursor(&units[0], &reader, kinds[0], tokens[0], mode);
        units[0].visit_roots[0] = 2;
        valid = c_fragment_reader_unit_rejected(&cursor, mode) && cursor.result.count == 0;
        c_fragment_reader_unit_check(checks, failures, valid, "cursor entered under the wrong root claim");
        c_fragment_reader_unit_binary(&units[0]);
        cursor = c_fragment_reader_unit_cursor(&units[0], &reader, kinds[0], tokens[0], mode);
        cursor.stack_capacity = 1;
        valid = c_fragment_reader_unit_rejected(&cursor, mode) && cursor.result.count == 1;
        c_fragment_reader_unit_check(checks, failures, valid, "streaming cursor exceeded its pending-frame slice");
        c_fragment_reader_unit_binary(&units[0]);
        cursor = c_fragment_reader_unit_cursor(&units[0], &reader, kinds[0], tokens[0], mode);
        valid = c_fragment_reader_test_next(&cursor, &event, mode) == C_FRAGMENT_READER_EVENT;
        units[0].visits.capacity = 1;
        units[0].visits.depth = 2;
        cursor.claim_depth = 2;
        units[0].visit_roots[1] = reader.leaves.root;
        valid = valid && c_fragment_reader_unit_rejected(&cursor, mode) && cursor.result.count == 1;
        c_fragment_reader_unit_check(checks, failures, valid, "resumed cursor indexed malformed visit depth even with a matching root");
        c_fragment_reader_unit_binary(&units[0]);
        units[0].kinds[3] = C_FRAGMENT_ADD;
        units[0].owners[3] = 1;
        units[0].locations[0] = 3;
        cursor = c_fragment_reader_unit_cursor(&units[0], &reader, kinds[0], tokens[0], mode);
        valid = c_fragment_reader_unit_rejected(&cursor, mode) && cursor.result.count == 0;
        c_fragment_reader_unit_check(checks, failures, valid, "streaming cursor admitted another operator as the claimed root");
    }
    for (uint32_t omission = 0; omission < 7; omission += 1)
    {
        c_fragment_reader_unit_binary(&units[0]);
        reader = c_fragment_reader_unit_begin(&units[0]);
        if (omission == 4)
        {
            reader.leaves.source = 0;
        }
        else if (omission == 5)
        {
            units[0].source.parser = 0;
        }
        else if (omission == 6)
        {
            units[0].source.next = 0;
        }
        cursor = c_fragment_reader_cursor(&reader, omission == 2 ? 0 : kinds[0], omission == 3 ? 0 : tokens[0], 3,
                                          omission == 0 ? 0 : &units[0].visits, omission == 1 ? 0 : units[0].stack, 128, true);
        valid = !cursor.result.valid && c_fragment_reader_unit_rejected(&cursor, 0);
        c_fragment_reader_unit_check(checks, failures, valid, "invalid initialization dereferenced absent frame/visit/input/source metadata");
    }
}

static uint32_t c_fragment_reader_tests(void)
{
    static CFragmentReaderUnit unit;
    uint32_t checks = 0;
    uint32_t failures = 0;
    uint8_t flat[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    uint32_t locations[BUSTER_C_FRAGMENT_READER_TEST_TOKENS];
    static const CFragmentReaderTestAction binary[] =
    {
        {
            .token = 1,
            .child = BUSTER_C_FRAGMENT_NONE,
            .phase = C_FRAGMENT_READER_ENTER,
        },
        {
            .token = 0,
            .child = BUSTER_C_FRAGMENT_NONE,
            .phase = C_FRAGMENT_READER_ENTER,
        },
        {
            .token = 0,
            .child = BUSTER_C_FRAGMENT_NONE,
            .phase = C_FRAGMENT_READER_REDUCE,
        },
        {
            .token = 0,
            .child = BUSTER_C_FRAGMENT_NONE,
            .phase = C_FRAGMENT_READER_LEAVE,
        },
        {
            .token = 1,
            .child = 0,
            .ordinal = 0,
            .phase = C_FRAGMENT_READER_CHILD,
        },
        {
            .token = 2,
            .child = BUSTER_C_FRAGMENT_NONE,
            .phase = C_FRAGMENT_READER_ENTER,
        },
        {
            .token = 2,
            .child = BUSTER_C_FRAGMENT_NONE,
            .phase = C_FRAGMENT_READER_REDUCE,
        },
        {
            .token = 2,
            .child = BUSTER_C_FRAGMENT_NONE,
            .phase = C_FRAGMENT_READER_LEAVE,
        },
        {
            .token = 1,
            .child = 2,
            .ordinal = 1,
            .phase = C_FRAGMENT_READER_CHILD,
        },
        {
            .token = 1,
            .child = BUSTER_C_FRAGMENT_NONE,
            .phase = C_FRAGMENT_READER_REDUCE,
        },
        {
            .token = 1,
            .child = BUSTER_C_FRAGMENT_NONE,
            .phase = C_FRAGMENT_READER_LEAVE,
        },
        {
            .token = 1,
            .child = BUSTER_C_FRAGMENT_NONE,
            .phase = C_FRAGMENT_READER_ATTACH,
        },
    };
    for (uint32_t mode = 0; mode < 4; mode += 1)
    {
        c_fragment_reader_unit_binary(&unit);
        CFragmentReaderResult result;
        if (mode < 2)
        {
            CFragmentDirectReader flatten = c_fragment_reader_unit_begin(&unit);
            const bool ready = mode == 0 ? c_fragment_reader_flatten(&flatten, flat, locations, BUSTER_C_FRAGMENT_READER_TEST_TOKENS, false) :
                                          c_fragment_reader_flatten(&flatten, flat, locations, BUSTER_C_FRAGMENT_READER_TEST_TOKENS, true);
            c_fragment_reader_unit_check(&checks, &failures, ready && locations[1] == BUSTER_C_FRAGMENT_NONE && locations[2] == BUSTER_C_FRAGMENT_NONE, "flatten copied leaf identities");
            locations[1] = UINT32_MAX - 1;
            locations[2] = UINT32_MAX - 1;
            result = c_fragment_reader_unit_flat(&unit, flat, locations, 3, BUSTER_C_FRAGMENT_READER_TEST_TOKENS, 5 * BUSTER_C_FRAGMENT_READER_TEST_TOKENS);
        }
        else
        {
            result = c_fragment_reader_unit_direct(&unit, mode == 3);
        }
        bool valid = result.valid && result.count == sizeof(binary) / sizeof(binary[0]) && result.nodes == 3 && result.stack_peak == 2;
        for (uint32_t i = 0; valid && i < result.count; i += 1)
        {
            const CFragmentReaderEvent* event = &unit.events[i];
            valid = event->phase == binary[i].phase && event->token == binary[i].token && event->child == binary[i].child && event->ordinal == binary[i].ordinal &&
                    event->boundary == 1 && event->source == event->token && event->expanded == 1000 + event->token && event->reference == BUSTER_C_FRAGMENT_NONE;
        }
        c_fragment_reader_unit_check(&checks, &failures, valid, "hand-specified binary event order/source identities differ");
    }
    c_fragment_reader_cursor_tests(&checks, &failures, binary, sizeof(binary) / sizeof(binary[0]));
    for (uint32_t mutation = 0; mutation < 15; mutation += 1)
    {
        c_fragment_reader_unit_binary(&unit);
        CFragmentHeader* header = c_fragment_header(&unit.store, 0);
        switch (mutation)
        {
            case 0:
                header->used = 0;
                break;
            case 1:
                header->used = 65;
                break;
            case 2:
                header->operator_mask = UINT64_C(1) << 63;
                break;
            case 3:
                header->operator_count = 2;
                break;
            case 4:
                header->operator_count = 17;
                break;
            case 5:
                header->next = 0;
                unit.handle.positions = 6;
                break;
            case 6:
                unit.handle.positions = 2;
                break;
            case 7:
                unit.handle.positions = 4;
                break;
            case 8:
                unit.store.allocated = 2;
                unit.live[1] = 1;
                unit.handle.tail = 1;
                break;
            case 9:
                header->next = 2;
                unit.handle.positions = 6;
                break;
            case 10:
                unit.next[0] = BUSTER_C_FRAGMENT_NONE;
                break;
            case 11:
                unit.kinds[3] = C_FRAGMENT_NUMBER;
                unit.owners[3] = 1;
                unit.next[2] = 3;
                unit.spans[1].end = 4;
                break;
            case 12:
                unit.first[1] = 2;
                unit.next[2] = 0;
                unit.next[0] = BUSTER_C_FRAGMENT_NONE;
                break;
            case 13:
                unit.next[0] = 0;
                break;
            case 14:
                unit.next[0] = BUSTER_C_FRAGMENT_READER_TEST_TOKENS;
                break;
        }
        CFragmentReaderResult scalar = c_fragment_reader_unit_direct(&unit, false);
        CFragmentReaderResult vector = c_fragment_reader_unit_direct(&unit, true);
        c_fragment_reader_unit_check(&checks, &failures, !scalar.valid && !vector.valid, "malformed chain/mask/count/tail/source leaf index was accepted");
        c_fragment_reader_unit_check(&checks, &failures,
                                     c_fragment_reader_unit_reject(&unit, 0, 0, unit.handle.positions, 128, 2) && c_fragment_reader_unit_reject(&unit, 0, 0, unit.handle.positions, 128, 3),
                                     "malformed direct cursor yielded ATTACH or failed to retain a cleared error");
        if (mutation >= 10)
        {
            flat[0] = C_FRAGMENT_ADD;
            flat[1] = C_FRAGMENT_NUMBER;
            flat[2] = C_FRAGMENT_NUMBER;
            locations[0] = 1;
            locations[1] = UINT32_MAX - 1;
            locations[2] = UINT32_MAX - 1;
            const CFragmentReaderResult flattened = c_fragment_reader_unit_flat(&unit, flat, locations, 3, 128, 64);
            c_fragment_reader_unit_check(&checks, &failures, !flattened.valid, "flat traversal accepted an exhausted/leftover/reordered/duplicate leaf index");
            c_fragment_reader_unit_check(&checks, &failures, c_fragment_reader_unit_reject(&unit, flat, locations, 3, 128, 0), "malformed flat source cursor did not retain a cleared error");
        }
    }
    c_fragment_reader_unit_binary(&unit);
    flat[0] = C_FRAGMENT_ADD;
    flat[1] = C_FRAGMENT_NUMBER;
    flat[2] = C_FRAGMENT_NUMBER;
    locations[0] = 1;
    locations[1] = BUSTER_C_FRAGMENT_NONE;
    locations[2] = BUSTER_C_FRAGMENT_NONE;
    CFragmentReaderResult result = c_fragment_reader_unit_flat(&unit, flat, locations, 3, 1, 64);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid && !result.overflow, "exhausted continuation stack was accepted or mislabeled as output overflow");
    memset(&unit.events[11], 0xa5, sizeof(unit.events[11]));
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 3, 128, 11);
    CFragmentReaderEvent untouched;
    memset(&untouched, 0xa5, sizeof(untouched));
    c_fragment_reader_unit_check(&checks, &failures, !result.valid && result.overflow && result.count == 11 && memcmp(&unit.events[11], &untouched, sizeof(untouched)) == 0,
                                 "collector failed to report output overflow or overwrote the tail");
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 3, 128, 0);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid && result.overflow && result.count == 0, "zero-capacity collector did not report output overflow");
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 3, 2, 12);
    c_fragment_reader_unit_check(&checks, &failures, result.valid && !result.overflow && result.count == 12 && result.stack_peak == 2, "exact frame/event capacities failed");
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 0, 128, 64);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid, "empty expression was accepted");
    flat[2] = C_FRAGMENT_END;
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 3, 128, 64);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid, "END closed a fixed-arity expression");
    c_fragment_reader_unit_check(&checks, &failures, c_fragment_reader_unit_reject(&unit, flat, locations, 3, 128, 0), "fixed-arity END failed to poison the streaming cursor");
    flat[0] = C_FRAGMENT_KIND_COUNT;
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 3, 128, 64);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid, "out-of-range kind was accepted");
    c_fragment_reader_unit_check(&checks, &failures, c_fragment_reader_unit_reject(&unit, flat, locations, 3, 128, 0), "out-of-range flat kind failed to poison the streaming cursor");
    CFragmentDirectReader invalid = { 0 };
    result = c_fragment_reader_direct_scalar(&invalid, 1, &unit.visits, 0, 0, 0, 0);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid && result.count == 0, "invalid context read metadata");
    CFragmentReaderSource empty = { .valid = true };
    invalid.leaves = c_fragment_leaf_cursor(&empty, 0);
    result = c_fragment_reader_direct_vector(&invalid, 1, &unit.visits, unit.stack, 128, unit.events, 64);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid && result.count == 0, "empty source read metadata");
    CFragmentDirectReader malformed = c_fragment_reader_unit_begin(&unit);
    unit.visits.capacity = 1;
    unit.visits.depth = 2;
    unit.visit_roots[1] = malformed.leaves.root;
    result = c_fragment_reader_direct_scalar(&malformed, 3, &unit.visits, unit.stack, 128, unit.events, 64);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid && result.count == 0, "malformed visit depth was indexed by the scheduler");
    malformed.leaves.next = BUSTER_C_FRAGMENT_NONE;
    c_fragment_reader_unit_check(&checks, &failures, !c_fragment_reader_complete(&unit.visits, &malformed.leaves), "malformed visit depth was indexed at completion");

    // Operator-only subtrees still need exact framing/counts; leaves cannot detect their omission.
    c_fragment_reader_unit_initialize(&unit);
    unit.kinds[0] = C_FRAGMENT_INITIALIZER;
    unit.kinds[1] = C_FRAGMENT_INITIALIZER;
    unit.owners[0] = 0;
    unit.owners[1] = 0;
    unit.boundaries[0] = 0;
    unit.spans[0].end = 2;
    unit.handle.positions = 4;
    flat[0] = C_FRAGMENT_INITIALIZER;
    flat[1] = C_FRAGMENT_INITIALIZER;
    flat[2] = C_FRAGMENT_END;
    flat[3] = C_FRAGMENT_END;
    locations[0] = 0;
    locations[1] = 1;
    locations[2] = BUSTER_C_FRAGMENT_NONE;
    locations[3] = BUSTER_C_FRAGMENT_NONE;
    *c_fragment_header(&unit.store, 0) = (CFragmentHeader)
    {
        .operator_mask = 15,
        .next = BUSTER_C_FRAGMENT_NONE,
        .used = 4,
        .operator_count = 4,
    };
    memcpy(c_fragment_operators(&unit.store, 0), flat, 4);
    memcpy(unit.locations, locations, 4 * sizeof(uint32_t));
    CFragmentReaderResult nested_scalar = c_fragment_reader_unit_direct(&unit, false);
    CFragmentReaderResult nested_vector = c_fragment_reader_unit_direct(&unit, true);
    c_fragment_reader_unit_check(&checks, &failures, nested_scalar.valid && nested_vector.valid && nested_scalar.count == 8 && nested_vector.count == 8, "zero-leaf list refill differs");
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 4, 128, 64);
    bool valid = result.valid && result.nodes == 2 && result.count == 8;
    for (uint32_t i = 0; valid && i < result.count; i += 1)
    {
        valid = unit.events[i].kind != C_FRAGMENT_END && unit.events[i].token < 2;
    }
    c_fragment_reader_unit_check(&checks, &failures, valid, "synthetic END became a semantic token event");
    for (uint32_t mode = 0; mode < 4; mode += 1)
    {
        CFragmentDirectReader reader;
        CFragmentReaderCursor cursor = c_fragment_reader_unit_cursor(&unit, &reader, flat, locations, mode);
        valid = c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_ENTER, 0, BUSTER_C_FRAGMENT_NONE, 0) &&
                c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_ENTER, 1, BUSTER_C_FRAGMENT_NONE, 0) &&
                c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_REDUCE, 1, BUSTER_C_FRAGMENT_NONE, 0) &&
                c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_LEAVE, 1, BUSTER_C_FRAGMENT_NONE, 0) &&
                c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_CHILD, 0, 1, 0) &&
                c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_REDUCE, 0, BUSTER_C_FRAGMENT_NONE, 0) &&
                c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_LEAVE, 0, BUSTER_C_FRAGMENT_NONE, 0) &&
                c_fragment_reader_unit_step(&cursor, mode, C_FRAGMENT_READER_ATTACH, 0, BUSTER_C_FRAGMENT_NONE, 0) && cursor.result.count == 8 &&
                c_fragment_reader_complete(&unit.visits, &reader.leaves);
        c_fragment_reader_unit_check(&checks, &failures, valid, "consecutive nested empty-list END positions skipped or repeated an event");
    }
    flat[1] = C_FRAGMENT_END;
    locations[1] = BUSTER_C_FRAGMENT_NONE;
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 2, 128, 64);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid, "shortened flat stream omitted an operator-only child");
    flat[1] = C_FRAGMENT_INITIALIZER;
    locations[1] = 0;
    unit.locations[1] = 0;
    nested_scalar = c_fragment_reader_unit_direct(&unit, false);
    nested_vector = c_fragment_reader_unit_direct(&unit, true);
    c_fragment_reader_unit_check(&checks, &failures, !nested_scalar.valid && !nested_vector.valid, "direct traversal duplicated an operator identity");
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 4, 128, 64);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid, "duplicate operator identity was accepted");
    c_fragment_reader_unit_check(&checks, &failures, c_fragment_reader_unit_reject(&unit, flat, locations, 4, 128, 0) &&
                                 c_fragment_reader_unit_reject(&unit, flat, locations, 4, 128, 2) && c_fragment_reader_unit_reject(&unit, flat, locations, 4, 128, 3),
                                 "duplicate operator identity did not poison flat/scalar/vector cursors");
    locations[1] = 1;
    unit.handle.positions = 3;
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 3, 128, 64);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid, "unterminated list was accepted");
    c_fragment_reader_unit_check(&checks, &failures, c_fragment_reader_unit_reject(&unit, flat, locations, 3, 128, 0), "unterminated flat list did not end with a cleared sticky error");
    unit.handle.positions = 1;
    flat[0] = C_FRAGMENT_END;
    locations[0] = BUSTER_C_FRAGMENT_NONE;
    result = c_fragment_reader_unit_flat(&unit, flat, locations, 1, 128, 64);
    c_fragment_reader_unit_check(&checks, &failures, !result.valid, "naked list END was accepted");
    c_fragment_reader_unit_check(&checks, &failures, c_fragment_reader_unit_reject(&unit, flat, locations, 1, 128, 0), "naked flat END did not end with a cleared sticky error");

    // A physical all-leaf window must not touch even a null identity column. Its leaves are not one expression;
    // this tests bounded refill independently of scheduling. 64+1 covers a full window and the shortest tail.
    c_fragment_reader_unit_initialize(&unit);
    unit.store.identities = 0;
    unit.store.allocated = 2;
    unit.live[1] = 1;
    unit.boundaries[0] = 0;
    unit.first[0] = 0;
    unit.spans[0].end = 65;
    unit.handle.tail = 1;
    unit.handle.positions = 65;
    for (uint32_t token = 0; token < 65; token += 1)
    {
        unit.kinds[token] = token & 1 ? C_FRAGMENT_IDENTIFIER : C_FRAGMENT_NUMBER;
        unit.owners[token] = 0;
        unit.next[token] = token == 64 ? BUSTER_C_FRAGMENT_NONE : token + 1;
    }
    *c_fragment_header(&unit.store, 0) = (CFragmentHeader)
    {
        .next = 1,
        .used = 64,
    };
    *c_fragment_header(&unit.store, 1) = (CFragmentHeader)
    {
        .next = BUSTER_C_FRAGMENT_NONE,
        .used = 1,
    };
    for (uint32_t mode = 0; mode < 2; mode += 1)
    {
        CFragmentDirectReader reader = c_fragment_reader_unit_begin(&unit);
        uint32_t written = 0;
        valid = true;
        while (valid && reader.remaining != 0)
        {
            uint32_t amount = 0;
            valid = mode == 0 ? c_fragment_direct_window(&reader, flat, locations, &amount, false) : c_fragment_direct_window(&reader, flat, locations, &amount, true);
            valid = valid && amount == (written == 0 ? 64 : 1);
            for (uint32_t i = 0; valid && i < amount; i += 1)
            {
                valid = locations[i] == written + i && flat[i] == unit.kinds[written + i];
            }
            written += amount;
        }
        c_fragment_reader_unit_check(&checks, &failures, valid && written == 65, "full/tail refill read the absent identity column or lost leaf order");
    }

    // Save an outer leaf position, consume a separately published root in its opaque interval, then restore.
    // The shared ledger rejects both recursive re-entry and a later raw scan scheduling the completed child.
    c_fragment_reader_unit_initialize(&unit);
    unit.kinds[0] = C_FRAGMENT_NUMBER;
    unit.kinds[4] = C_FRAGMENT_NUMBER;
    unit.kinds[5] = C_FRAGMENT_NUMBER;
    unit.kinds[9] = C_FRAGMENT_NUMBER;
    unit.owners[0] = 0;
    unit.owners[9] = 0;
    unit.owners[4] = 4;
    unit.owners[5] = 4;
    unit.boundaries[0] = 0;
    unit.boundaries[4] = 4;
    unit.first[0] = 0;
    unit.first[4] = 4;
    unit.next[0] = 9;
    unit.next[4] = 5;
    unit.spans[0].end = 10;
    unit.spans[3].end = 7;
    unit.spans[4].end = 6;
    unit.handle.head = BUSTER_C_FRAGMENT_NONE;
    unit.handle.tail = BUSTER_C_FRAGMENT_NONE;
    unit.handle.positions = 1;
    CFragmentDirectReader outer = c_fragment_reader_unit_begin(&unit);
    CFragmentLeafCursor cursor = outer.leaves;
    CFragmentLeafCursor continuations[2];
    uint32_t depth = 0;
    uint8_t kind = C_FRAGMENT_KIND_COUNT;
    uint32_t token = BUSTER_C_FRAGMENT_NONE;
    valid = outer.valid && c_fragment_leaf_next(&cursor, &kind, &token) && token == 0;
    c_fragment_reader_unit_check(&checks, &failures, valid && !c_fragment_leaf_enter(&cursor, 3, 4, continuations, &depth, 0), "zero-capacity opaque continuation was accepted");
    c_fragment_reader_unit_check(&checks, &failures, valid && !c_fragment_leaf_enter(&cursor, 2, 4, continuations, &depth, 2) && depth == 0 && cursor.root == 0 && cursor.next == 9,
                                 "opaque child outside the requested subtree was accepted");
    unit.spans[3].end = 11;
    c_fragment_reader_unit_check(&checks, &failures, valid && !c_fragment_leaf_enter(&cursor, 3, 4, continuations, &depth, 2) && depth == 0 && cursor.root == 0 && cursor.next == 9,
                                 "opaque subtree outside the suspended parent was accepted");
    unit.spans[3].end = 7;
    valid = valid && c_fragment_leaf_enter(&cursor, 3, 4, continuations, &depth, 2) && c_fragment_reader_claim(&unit.visits, &cursor);
    c_fragment_reader_unit_check(&checks, &failures, valid && !c_fragment_leaf_leave(&cursor, continuations, &depth, 2) && !c_fragment_reader_claim(&unit.visits, &cursor), "unfinished/active opaque child was accepted twice");
    valid = valid && c_fragment_leaf_next(&cursor, &kind, &token) && token == 4 && c_fragment_leaf_next(&cursor, &kind, &token) && token == 5 && c_fragment_reader_complete(&unit.visits, &cursor);
    c_fragment_reader_unit_check(&checks, &failures, valid && !c_fragment_reader_claim(&unit.visits, &cursor), "raw scan rescheduled a completed opaque child");
    valid = valid && c_fragment_leaf_leave(&cursor, continuations, &depth, 2) && cursor.previous == 0 && c_fragment_leaf_next(&cursor, &kind, &token) && token == 9 &&
            c_fragment_reader_complete(&unit.visits, &cursor) && depth == 0 && unit.visits.depth == 0;
    c_fragment_reader_unit_check(&checks, &failures, valid, "opaque interval failed to restore the forward outer cursor");

    // Preparation is charged and uses real ordinary-tree metadata. Invalid postorder fails before publication
    // and rolls back retained allocations; the same arena is not a valid scratch/output pair.
    c_fragment_reader_unit_binary(&unit);
    uint32_t raw_parents[] =
    {
        1,
        3,
        1,
        BUSTER_C_FRAGMENT_NONE,
    };
    uint32_t raw_postorder[] =
    {
        0,
        2,
        1,
        3,
    };
    unit.parser.tokens.count = 4;
    unit.parser.tree.parents = raw_parents;
    unit.parser.postorder = raw_postorder;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        unit.syntax[i].emitted = true;
    }
    unit.syntax[3].transparent = true;
    CFragmentSink sink =
    {
        .store = unit.store,
        .syntax = unit.syntax,
        .kinds = unit.kinds,
        .token_count = 4,
    };
    Arena output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    CFragmentReaderSource prepared;
    valid = output.capacity != 0 && scratch.capacity != 0 && c_fragment_reader_source(&prepared, &unit.parser, &sink, &output, &scratch);
    c_fragment_reader_unit_check(&checks, &failures, valid && prepared.first[1] == 0 && prepared.next[0] == 2 && prepared.boundaries[1] == 3 &&
                                 prepared.retained_bytes >= 5 * 25 && prepared.temporary_bytes >= 5 * 13 && scratch.length == 0, "source-index construction/accounting differs");
    // The infix node contains both leaves; its transparent parent additionally contains token 3. Leaf spans
    // remain singletons. These hand-specified intervals reject a constructor that widens everything to [0,4).
    c_fragment_reader_unit_check(&checks, &failures, valid && prepared.spans[0].begin == 0 && prepared.spans[0].end == 1 &&
                                 prepared.spans[1].begin == 0 && prepared.spans[1].end == 3 && prepared.spans[2].begin == 2 && prepared.spans[2].end == 3 &&
                                 prepared.spans[3].begin == 0 && prepared.spans[3].end == 4, "prepared raw subtree intervals differ from the hand-specified tree");
    const uint64_t retained = output.length;
    raw_postorder[1] = 0;
    valid = output.capacity != 0 && scratch.capacity != 0 && !c_fragment_reader_source(&prepared, &unit.parser, &sink, &output, &scratch);
    c_fragment_reader_unit_check(&checks, &failures, valid && output.length == retained && scratch.length == 0 && !prepared.valid, "malformed source index did not fail and roll back");
    raw_postorder[1] = 2;
    valid = !c_fragment_reader_source(&prepared, &unit.parser, &sink, &output, &output);
    c_fragment_reader_unit_check(&checks, &failures, valid && output.length == retained, "overlapping output/scratch arenas were accepted");
    arena_release(&scratch);
    arena_release(&output);

#if defined(__linux__)
    const long page_size = sysconf(_SC_PAGESIZE);
    void* guard = page_size > 0 ? mmap(0, (size_t)page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    c_fragment_reader_unit_check(&checks, &failures, guard != MAP_FAILED, "guard page allocation failed");
    if (guard != MAP_FAILED)
    {
        c_fragment_reader_unit_binary(&unit);
        CFragmentDirectReader reader = c_fragment_reader_unit_begin(&unit);
        c_fragment_header(&unit.store, 0)->used = 65;
        unit.store.identities = guard;
        uint32_t amount = 99;
        valid = !c_fragment_direct_window(&reader, guard, guard, &amount, true) && amount == 0;
        c_fragment_reader_unit_check(&checks, &failures, valid, "invalid count touched guarded input/output slices");
        c_fragment_reader_unit_initialize(&unit);
        unit.kinds[0] = C_FRAGMENT_NUMBER;
        unit.owners[0] = 0;
        unit.boundaries[0] = 0;
        unit.first[0] = 0;
        unit.handle.head = BUSTER_C_FRAGMENT_NONE;
        unit.handle.tail = BUSTER_C_FRAGMENT_NONE;
        unit.handle.positions = 1;
        unit.store.identities = guard;
        unit.store.leaf_kinds = guard;
        reader = c_fragment_reader_unit_begin(&unit);
        valid = c_fragment_direct_window(&reader, flat, locations, &amount, true) && amount == 1 && locations[0] == 0;
        valid = valid && c_fragment_direct_window(&reader, guard, guard, &amount, true) && amount == 0;
        c_fragment_reader_unit_check(&checks, &failures, valid, "virtual leaf or empty refill touched guarded legacy columns/output");
        munmap(guard, (size_t)page_size);
    }
#endif
    printf("fragment-reader units: checks=%u failures=%u\n", checks, failures);
    return failures;
}
