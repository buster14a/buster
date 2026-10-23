// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-fragments: the scalar recognizer with and without its compile-time fragment sink, at all three physical
// capacities. The independent decoder reconstructs parents, sibling order and postorder from fixed arities and
// explicit list ends. It then compares token identities node for node with the parser's unchanged postorder tree.
// A second iterative walk emits source events by fixity and compares them with sorted original token identities.
// Grouping punctuation maps to its closer's canonical child; type syntax and statement blocks remain postorder
// subtrees referenced by their expression node. Neither their punctuation nor declarations become arithmetic nodes.
// Includes the parser test harness for preprocessing options and fixtures; corpus inputs are explicit regular files.

#include "c_fragments_store_test.c"
#include "c_fragments_coverage_test.c"

typedef struct CFragmentTestFrame CFragmentTestFrame;
struct CFragmentTestFrame
{
    uint32_t token;
    uint32_t remaining;
};

typedef struct CFragmentTestTree CFragmentTestTree;
struct CFragmentTestTree
{
    uint32_t* parents;
    uint32_t* first;
    uint32_t* last;
    uint32_t* next;
    uint32_t* previous;
    uint32_t* postorder;
    uint8_t* seen;
    uint32_t postorder_count;
};

typedef struct CFragmentTestTotals CFragmentTestTotals;
struct CFragmentTestTotals
{
    uint64_t files;
    uint64_t tokens;
    uint64_t expressions;
    uint64_t nodes;
    uint64_t source_events;
    uint64_t hot_peak;
    uint64_t identity_peak;
    uint64_t hot_backing_peak;
    uint64_t identity_backing_peak;
    uint64_t ownership_backing_peak;
    uint64_t syntax_backing_peak;
    uint64_t inclusive_arena_peak;
    CFragmentStatistics statistics;
};

static bool c_fragment_test_tree_initialize(CFragmentTestTree* tree, Arena* arena, uint32_t count)
{
    *tree = (CFragmentTestTree){
        .parents = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1),
        .first = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1),
        .last = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1),
        .next = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1),
        .previous = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1),
        .postorder = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1),
        .seen = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1),
    };
    const bool result = tree->parents != 0 && tree->first != 0 && tree->last != 0 && tree->next != 0 && tree->previous != 0 && tree->postorder != 0 && tree->seen != 0;
    if (result)
    {
        memset(tree->parents, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(tree->first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(tree->last, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(tree->next, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(tree->previous, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(tree->seen, 0, (uint64_t)count + 1);
    }
    return result;
}

static bool c_fragment_test_decode(const CFragmentSink* sink, const uint8_t* kinds, const uint32_t* ids, uint32_t count, CFragmentTestTree* tree,
                                   CFragmentTestFrame* stack, uint32_t* root)
{
    uint32_t depth = 0;
    bool result = true;
    *root = BUSTER_C_FRAGMENT_NONE;
    for (uint32_t k = 0; result && k < count; k += 1)
    {
        while (depth != 0 && stack[depth - 1].remaining == 0)
        {
            tree->postorder[tree->postorder_count] = stack[depth - 1].token;
            tree->postorder_count += 1;
            depth -= 1;
        }
        const uint8_t kind = kinds[k];
        const uint32_t token = ids[k];
        if (kind == C_FRAGMENT_END)
        {
            result = token == BUSTER_C_FRAGMENT_NONE && depth != 0 && stack[depth - 1].remaining == UINT32_MAX;
            if (result)
            {
                tree->postorder[tree->postorder_count] = stack[depth - 1].token;
                tree->postorder_count += 1;
                depth -= 1;
            }
        }
        else
        {
            result = kind < C_FRAGMENT_KIND_COUNT && token < sink->token_count && tree->seen[token] == 0 && sink->syntax[token].emitted && sink->kinds[token] == kind;
            if (result)
            {
                tree->seen[token] = 1;
                if (depth != 0)
                {
                    CFragmentTestFrame* parent = &stack[depth - 1];
                    tree->parents[token] = parent->token;
                    const uint32_t previous = tree->last[parent->token];
                    tree->previous[token] = previous;
                    if (previous == BUSTER_C_FRAGMENT_NONE)
                    {
                        tree->first[parent->token] = token;
                    }
                    else
                    {
                        tree->next[previous] = token;
                    }
                    tree->last[parent->token] = token;
                    parent->remaining -= parent->remaining != UINT32_MAX;
                }
                else
                {
                    result = *root == BUSTER_C_FRAGMENT_NONE;
                    *root = token;
                }
                const uint8_t arity = c_fragment_arities[kind];
                stack[depth] = (CFragmentTestFrame){ .token = token, .remaining = arity == BUSTER_C_FRAGMENT_LIST_ARITY ? UINT32_MAX : arity };
                depth += 1;
            }
        }
    }
    while (depth != 0 && stack[depth - 1].remaining == 0)
    {
        tree->postorder[tree->postorder_count] = stack[depth - 1].token;
        tree->postorder_count += 1;
        depth -= 1;
    }
    return result && depth == 0 && *root != BUSTER_C_FRAGMENT_NONE;
}

// A source event is the exact parser token identity plus its explicit expanded-token mapping. Sorting is used only
// to construct the independent source-order oracle, never to repair the decoded expression's traversal.
static void c_fragment_test_sort(uint32_t* values, uint32_t* scratch, uint32_t count)
{
    for (uint32_t shift = 0; shift < 32; shift += 8)
    {
        uint32_t offsets[256] = { 0 };
        for (uint32_t k = 0; k < count; k += 1)
        {
            offsets[(values[k] >> shift) & 255] += 1;
        }
        uint32_t before = 0;
        for (uint32_t k = 0; k < 256; k += 1)
        {
            const uint32_t amount = offsets[k];
            offsets[k] = before;
            before += amount;
        }
        for (uint32_t k = 0; k < count; k += 1)
        {
            const uint32_t bucket = (values[k] >> shift) & 255;
            scratch[offsets[bucket]] = values[k];
            offsets[bucket] += 1;
        }
        memcpy(values, scratch, (uint64_t)count * sizeof(uint32_t));
    }
}

static bool c_fragment_test_source(const CFragmentSink* sink, const CFragmentTestTree* tree, uint32_t root, const uint32_t* ids, uint32_t count,
                                   uint64_t* work, uint32_t* actual, uint32_t* expected, uint32_t* sorted, uint64_t* events)
{
    const uint64_t event_bit = 1ull << 63;
    uint32_t depth = 1;
    uint32_t actual_count = 0;
    uint32_t expected_count = 0;
    bool result = true;
    work[0] = root;
    for (uint32_t k = 0; k < count; k += 1)
    {
        if (ids[k] < sink->token_count)
        {
            const CFragmentSyntax* node = &sink->syntax[ids[k]];
            expected[expected_count] = node->source;
            expected_count += 1;
            if (node->auxiliary != BUSTER_C_FRAGMENT_NONE)
            {
                expected[expected_count] = node->auxiliary;
                expected_count += 1;
            }
        }
    }
    while (result && depth != 0)
    {
        depth -= 1;
        const uint64_t task = work[depth];
        const uint32_t token = (uint32_t)task;
        result = token < sink->token_count && actual_count <= expected_count;
        if (result && (task & event_bit))
        {
            actual[actual_count] = token;
            actual_count += 1;
        }
        else if (result)
        {
            const CFragmentSyntax* node = &sink->syntax[token];
            const uint8_t order = c_fragment_orders[sink->kinds[token]];
            const uint32_t first = tree->first[token];
            if (order == 2 || order == 3 || order == 4 || order == 5 || order == 6)
            {
                for (uint32_t child = tree->last[token]; child != BUSTER_C_FRAGMENT_NONE && child != first; child = tree->previous[child])
                {
                    work[depth] = child;
                    depth += 1;
                    if (order == 4 && (child == tree->last[token]))
                    {
                        work[depth] = event_bit | node->auxiliary;
                        depth += 1;
                    }
                }
                work[depth] = event_bit | node->source;
                depth += 1;
                if (first != BUSTER_C_FRAGMENT_NONE)
                {
                    work[depth] = first;
                    depth += 1;
                }
            }
            else
            {
                for (uint32_t child = tree->last[token]; child != BUSTER_C_FRAGMENT_NONE; child = tree->previous[child])
                {
                    work[depth] = child;
                    depth += 1;
                }
                work[depth] = event_bit | node->source;
                depth += 1;
            }
            result = depth <= 2 * count + 2;
        }
    }
    c_fragment_test_sort(expected, sorted, expected_count);
    result &= actual_count == expected_count;
    for (uint32_t k = 0; result && k < expected_count; k += 1)
    {
        result = actual[k] == expected[k] && sink->sources[actual[k]] == sink->sources[expected[k]];
    }
    *events += actual_count;
    return result;
}

static bool c_fragment_test_oracle(const Parser* parser, const CFragmentSink* sink, const CFragmentCoverage* coverage, Arena* arena, uint64_t* nodes, uint64_t* events)
{
    const uint32_t count = sink->token_count;
    const uint64_t slots = 2 * (uint64_t)count + 4;
    CFragmentTestTree tree;
    bool result = c_fragment_test_tree_initialize(&tree, arena, count);
    CFragmentTestFrame* stack = BUSTER_ARENA_PUSH_ARRAY(arena, CFragmentTestFrame, slots);
    uint8_t* scalar = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots);
    uint8_t* vector = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slots);
    uint32_t* scalar_ids = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    uint32_t* vector_ids = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    uint64_t* work = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, 2 * slots + 2);
    uint32_t* actual = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * slots);
    uint32_t* expected = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * slots);
    uint32_t* sorted = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * slots);
    uint32_t* normalized_parents = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1);
    result &= stack != 0 && scalar != 0 && vector != 0 && scalar_ids != 0 && vector_ids != 0 && work != 0 && actual != 0 && expected != 0 && sorted != 0 && normalized_parents != 0;
    result = result && sink->expression_count == coverage->roots && c_fragment_test_root_controls(coverage, count);
    for (uint32_t e = 0; result && e < sink->expression_count; e += 1)
    {
        const CFragmentHandle* handle = &sink->expressions[e].handle;
        result = c_fragment_read(&sink->store, handle, scalar, scalar_ids, (uint32_t)slots, false) &&
                 c_fragment_read(&sink->store, handle, vector, vector_ids, (uint32_t)slots, true);
        result = result && memcmp(scalar, vector, handle->positions) == 0 && memcmp(scalar_ids, vector_ids, (uint64_t)handle->positions * sizeof(uint32_t)) == 0;
        uint32_t root = BUSTER_C_FRAGMENT_NONE;
        result = result && c_fragment_test_decode(sink, scalar, scalar_ids, handle->positions, &tree, stack, &root);
        if (result && !c_fragment_coverage_root_matches(coverage, count, root, sink->expressions[e].root))
        {
            fprintf(stderr, "fragment publication: decoded root %u expected raw boundary %u actual %u\n",
                    root, root < count ? coverage->nodes[root].boundary : BUSTER_C_FRAGMENT_NONE, sink->expressions[e].root);
            result = false;
        }
        result = result && c_fragment_test_source(sink, &tree, root, scalar_ids, handle->positions, work, actual, expected, sorted, events);
        if (!result)
        {
            fprintf(stderr, "fragment oracle: expression %u, parser root %u, payload %u\n", e, sink->expressions[e].root, sink->store.payload);
        }
    }
    // Transparent parent chains are compressed in explicit work stacks, making deeply grouped trees linear.
    if (result)
    {
        memset(normalized_parents, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            uint32_t parent = parser->tree.parents[token];
            uint32_t depth = 0;
            while (parent < count && (coverage->nodes[parent].transparent || coverage->nodes[parent].bridge))
            {
                // An omitted-'=' designator's index is a separate expression. Only its later RHS child bypasses
                // the raw closer, which forwards that handle at the live reduction. This is source-side specific.
                if (coverage->nodes[parent].bridge && token < parent)
                {
                    parent = count;
                    break;
                }
                if (normalized_parents[parent] != BUSTER_C_FRAGMENT_NONE)
                {
                    parent = normalized_parents[parent];
                    break;
                }
                work[depth] = parent;
                depth += 1;
                parent = parser->tree.parents[parent];
            }
            if (parent < count && coverage->nodes[parent].kind != C_FRAGMENT_KIND_COUNT)
            {
                // An opaque zero-arity reference keeps its full raw subtree for the consumer. Expressions inside
                // that type/designator/block publish separately; they are not children of the reference leaf.
                const bool opaque = c_fragment_coverage_opaque(&coverage->nodes[parent]);
                parent = opaque ? count : parent;
            }
            else
            {
                parent = count;
            }
            for (uint32_t k = 0; k < depth; k += 1)
            {
                normalized_parents[(uint32_t)work[k]] = parent;
            }
            normalized_parents[token] = parent;
        }
        // Opaque references split the ordinary tree into expression roots. An expression inside a reference can
        // finish before an earlier child of the enclosing expression, so publication order is not raw postorder.
        // Reuse the completed source-check scratch to identify those roots and compare each original decoded
        // sequence with its ordinary-tree postorder. No decoded node or sibling sequence is sorted or rewritten.
        uint32_t* normalized_roots = actual;
        uint32_t* decoded_positions = expected;
        uint32_t* previous_positions = sorted;
        memset(normalized_roots, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(decoded_positions, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(previous_positions, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t k = 0; k < tree.postorder_count; k += 1)
        {
            decoded_positions[tree.postorder[k]] = k;
        }
        for (uint32_t k = count; result && k != 0; k -= 1)
        {
            const uint32_t token = parser->postorder[k - 1];
            if (coverage->nodes[token].kind != C_FRAGMENT_KIND_COUNT)
            {
                const uint32_t parent = normalized_parents[token];
                normalized_roots[token] = parent < count ? normalized_roots[parent] : token;
                result = normalized_roots[token] != BUSTER_C_FRAGMENT_NONE;
            }
        }
        uint32_t order = 0;
        for (uint32_t k = 0; result && k < count; k += 1)
        {
            const uint32_t token = parser->postorder[k];
            const bool emitted = coverage->nodes[token].kind != C_FRAGMENT_KIND_COUNT;
            result = tree.seen[token] == emitted;
            if (result && emitted)
            {
                const uint32_t parent = normalized_parents[token] < count ? normalized_parents[token] : BUSTER_C_FRAGMENT_NONE;
                const uint32_t root = normalized_roots[token];
                const uint32_t position = decoded_positions[token];
                const uint32_t previous = previous_positions[root];
                result = tree.parents[token] == parent && position != BUSTER_C_FRAGMENT_NONE && (previous == BUSTER_C_FRAGMENT_NONE || previous < position);
                previous_positions[root] = position;
                order += 1;
                if (!result)
                {
                    fprintf(stderr, "fragment structure: token %u expected parent %u actual %u, root %u postorder position %u previous %u\n",
                            token, parent, tree.parents[token], root, position, previous);
                }
            }
        }
        result &= order == tree.postorder_count;
        *nodes += order;
    }
    return result;
}

static void c_fragment_test_accumulate(CFragmentTestTotals* total, const CFragmentSink* sink, uint64_t inclusive_arena_bytes)
{
    const CFragmentStatistics* from = &sink->store.statistics;
    CFragmentStatistics* to = &total->statistics;
    total->files += 1;
    total->tokens += sink->token_count;
    total->expressions += sink->expression_count;
    const uint64_t hot = (uint64_t)from->peak_live * sink->store.stride;
    const uint64_t identity = (uint64_t)from->peak_live * BUSTER_C_FRAGMENT_POSITIONS * sizeof(uint32_t);
    total->hot_peak = hot > total->hot_peak ? hot : total->hot_peak;
    total->identity_peak = identity > total->identity_peak ? identity : total->identity_peak;
    const uint64_t hot_backing = (uint64_t)sink->store.capacity * sink->store.stride;
    const uint64_t identity_backing = (uint64_t)sink->store.capacity * BUSTER_C_FRAGMENT_POSITIONS * sizeof(uint32_t);
    const uint64_t ownership_backing = (uint64_t)sink->store.capacity * (sizeof(uint32_t) + sizeof(uint8_t));
    const uint64_t syntax_backing = ((uint64_t)sink->token_count + 1) * (sizeof(CFragmentSyntax) + sizeof(CFragmentExpression) + sizeof(uint8_t));
    total->hot_backing_peak = hot_backing > total->hot_backing_peak ? hot_backing : total->hot_backing_peak;
    total->identity_backing_peak = identity_backing > total->identity_backing_peak ? identity_backing : total->identity_backing_peak;
    total->ownership_backing_peak = ownership_backing > total->ownership_backing_peak ? ownership_backing : total->ownership_backing_peak;
    total->syntax_backing_peak = syntax_backing > total->syntax_backing_peak ? syntax_backing : total->syntax_backing_peak;
    total->inclusive_arena_peak = inclusive_arena_bytes > total->inclusive_arena_peak ? inclusive_arena_bytes : total->inclusive_arena_peak;
    to->reductions += from->reductions;
    to->copies += from->copies;
    to->copied_bytes += from->copied_bytes;
    to->identity_bytes_copied += from->identity_bytes_copied;
    to->links += from->links;
    to->linked_bytes += from->linked_bytes;
    to->reused += from->reused;
    to->positions += from->positions;
    to->operators += from->operators;
    to->fragments += from->fragments;
    for (uint32_t k = 0; k <= 64; k += 1)
    {
        to->position_occupancy[k] += from->position_occupancy[k];
    }
    for (uint32_t k = 0; k <= 48; k += 1)
    {
        to->operator_occupancy[k] += from->operator_occupancy[k];
    }
}

static void c_fragment_test_failure_context(const Parser* parser, Preprocessor* pp, uint32_t token)
{
    if (token < parser->tokens.count)
    {
        const uint32_t source = parser->tokens.sources[token];
        const uint32_t location = pp->output.locations[source];
        const uint32_t range = pp_find_range(pp, location);
        const char* name = "<unknown>";
        uint32_t name_length = 9;
        uint32_t column = 0;
        const uint32_t line = pp_presumed(pp, location, &name, &name_length);
        if (range != BUSTER_PP_NONE)
        {
            pp_line_of(pp, range, location, &column);
        }
        fprintf(stderr, "fragment source: %.*s:%u:%u parser_token=%u expanded_token=%u kind=%s role=%s parent=%u\n", (int)name_length, name, line, column,
                token, source, parse_kind_names[parser->tokens.kinds[token]], parse_role_names[parser->tree.roles[token]], parser->tree.parents[token]);
        parse_test_print_context(parser, pp, token);
    }
}

static uint32_t c_fragment_test_file(const char* path, const PPOptions* options, int valid_input, CFragmentTestTotals* totals, int fixture, const char* expected_error)
{
    uint32_t failures = 0;
    static InternTable table;
    static Preprocessor pp;
    static Parser parser;
    const ParseOptions parse_options = { .scalar = true };
    bool ready = intern_initialize(&table) && pp_initialize(&pp, options, &table) && parse_initialize(&parser, &parse_options, options->language);
    ready = ready && pp_run(&pp, path);
    if (ready)
    {
        const bool parsed = parse_translation_unit(&parser, &pp);
        const uint32_t count = parser.tokens.count;
        failures += valid_input >= 0 && parsed != (valid_input != 0);
        if (expected_error != 0)
        {
            const ParseTestOutcome outcome = parse_test_outcome(&parser, &pp);
            char actual[320] = "";
            if (outcome.errors != 0)
            {
                snprintf(actual, sizeof(actual), "%u:%u: %s", outcome.line, outcome.column, outcome.message);
            }
            if (!outcome.preprocessed || strcmp(actual, expected_error) != 0)
            {
                fprintf(stderr, "fragment parser expectation: %s expected \"%s\" got \"%s\"\n", path, expected_error, actual);
                failures += 1;
            }
        }
        const bool baseline_good = failures == 0;
        for (uint32_t candidate = 0; candidate < 3; candidate += 1)
        {
            Arena arena = arena_reserve(1ull << 36);
            CFragmentSink sink;
            bool good = baseline_good && arena.capacity != 0 && c_fragment_sink_initialize(&sink, &arena, parser.tokens.kinds, parser.tokens.names, parser.tokens.sources, count, 16 * (candidate + 1));
            const uint64_t slots = (uint64_t)count + 2 * BUSTER_PARSE_TOKEN_PADDING;
            uint8_t* roles = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, slots);
            uint32_t* depths = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, slots);
            uint32_t* parents = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, slots);
            ParseDiagnostic* diagnostics = BUSTER_ARENA_PUSH_ARRAY(&arena, ParseDiagnostic, (uint64_t)count + 16);
            good = good && roles != 0 && depths != 0 && parents != 0 && diagnostics != 0 && parse_prepare_meanings(&parser, &table);
            if (good)
            {
                memset(roles, 0, slots);
                ParseTree tree = { .roles = roles + BUSTER_PARSE_TOKEN_PADDING, .depths = depths + BUSTER_PARSE_TOKEN_PADDING, .parents = parents + BUSTER_PARSE_TOKEN_PADDING };
                uint32_t diagnostic_count = 0;
                const ParseScalarResult result = parse_scalar_fragments(parser.tokens.kinds, parser.tokens.names, parser.tokens.flags, count, parser.language, &parser.meanings,
                                                                       &tree, &parser.scratch, diagnostics, count + 16, &diagnostic_count, &sink);
                good = !result.out_of_memory && result.errors == parser.errors && diagnostic_count == parser.diagnostic_count &&
                       memcmp(tree.roles, parser.tree.roles, count) == 0 && memcmp(tree.depths, parser.tree.depths, (uint64_t)count * sizeof(uint32_t)) == 0 &&
                       memcmp(tree.parents, parser.tree.parents, (uint64_t)count * sizeof(uint32_t)) == 0;
                for (uint32_t d = 0; good && d < diagnostic_count; d += 1)
                {
                    const ParseDiagnostic* a = &diagnostics[d];
                    const ParseDiagnostic* b = &parser.diagnostics[d];
                    good = a->token == b->token && a->argument == b->argument && a->message == b->message && a->place == b->place && a->error == b->error;
                }
                if (parsed)
                {
                    uint64_t nodes = 0;
                    uint64_t events = 0;
                    CFragmentCoverage coverage = { 0 };
                    if (!sink.store.valid || sink.unsupported != 0)
                    {
                        const uint32_t token = sink.first_unsupported;
                        fprintf(stderr, "fragment sink: %s payload=%u unsupported=%u first=%u role=%u kind=%u children=%u\n", path, sink.store.payload, sink.unsupported, token,
                                token < count ? sink.syntax[token].role : 0, token < count ? sink.kinds[token] : 0, token < count ? sink.syntax[token].child_count : 0);
                        c_fragment_test_failure_context(&parser, &pp, token);
                    }
                    good = good && sink.store.valid && !sink.syntax_error && sink.unsupported == 0;
                    good = good && c_fragment_test_coverage(&parser, &sink, &arena, &coverage);
                    good = good && c_fragment_test_coverage_fixture(&parser, &sink, fixture);
                    good = good && c_fragment_test_oracle(&parser, &sink, &coverage, &arena, &nodes, &events);
                    if (good)
                    {
                        totals[candidate].nodes += nodes;
                        totals[candidate].source_events += events;
                        c_fragment_test_accumulate(&totals[candidate], &sink, arena.length);
                    }
                }
                else
                {
                    good = good && sink.syntax_error;
                }
            }
            if (!good)
            {
                fprintf(stderr, "test-fragments: failed %s payload=%u\n", path, 16 * (candidate + 1));
                failures += 1;
            }
            arena_release(&arena);
        }
    }
    else
    {
        fprintf(stderr, "test-fragments: cannot preprocess %s\n", path);
        failures += 1;
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&table);
    return failures;
}

static uint32_t c_fragment_test_fixtures(const char* directory, CFragmentTestTotals* totals)
{
    static const char* const sources[] = {
        "int f(int a,int b,int c) { return a+b*c-a/b%c; }\n",
        "int f(int a,int b,int c) { return a=b=c; }\n",
        "int f(int a,int b,int c) { return +a+-b+!c+~a+ ++b+c--; }\n",
        "int f(int a,int b,int c) { return (((a+a)+(a+a))) ? b+b : c+c; }\n",
        "int f(int a,int b) { return a ?: b; }\n",
        "int g(int,...); int f(int a,int b) { return g(a,b,a+b)+g(a); }\n",
        "struct S { int x; }; int f(struct S *p,int *q) { return p->x+q[2]; }\n",
        "int f(int a) { return (long)a + sizeof(int) + sizeof(a); }\n",
        "int f(int a) { return _Generic(a, int:a+1, default:a-1); }\n",
        "int f(int a) { return ((int[]){1,2,3})[a]; }\n",
        "int f(int a) { return ({ int b=a+1; b+b; }); }\n",
        "const char *f(void) { return \"a\" \"b\" \"c\"; }\n",
        "int f(int a) { return __builtin_choose_expr(1,a,a+1); }\n",
        "int f(int a) { return __builtin_types_compatible_p(int,long); }\n",
        "int a[3] = {1,2,3}; int f(void) { return a[0]; }\n",
        "int x=1,y=2; _Static_assert(1,\"message\");\n",
        "int a[2][1]={{1},{2}};\n",
        "struct S { int a,b; }; struct S s={.a=1,.b=2}; int t[2]={[0]=1,[1]=2,};\n",
        "int f(int a) { __asm__(\"\" : \"+r\"(a)); return a; }\n",
        "int f(int a) { int x[3]; return sizeof(a)+_Alignof(int)+_Countof(x)+__real__ a+__imag__ a; }\n",
        "int f(int a) { return __builtin_types_compatible_p(typeof(a),int); }\n",
        "struct S { int a[2]; }; int f(void) { return __builtin_offsetof(struct S,a[1]); }\n",
        "enum E { A=1, B, C=2 };\n",
        "void f(int n,int a[static const n]) { (void)a; }\n",
        "int f(int x,int y) { int a[]={ [0] x+y }; return a[0]; }\n",
        "int g(int); int f(int x) { int a[]={ [0] g(x), [1] x++ }; return a[0]; }\n",
        "int f(int x) { int a[]={ [0] -x, [1] (int)x }; return a[0]; }\n",
        "int f(int x) { int a[][2]={ [0] {x,2} }; int b[4]={ [1 ... 3] +x }; return a[0][0]+b[1]; }\n",
        "int f(int x) { int a[]={ [0] (int){x} }; return a[0]; }\n",
        "const char *a[]={ [0] \"a\" \"b\" \"c\" };\n",
        "int f(int x,int y) { int a[]={ [(((0)))] (((x+y))) }; return a[0]; }\n",
        "int f(int x) { return (int)(x); }\n",
        "int f(int x) { (x+1); (x+1); return x; }\n",
    };
    static const char* const invalid[] = {
        "int f(void) { return +; }\n",
        "int f(int a,int b) { return + a b; }\n",
        "int f(int a) { return ((a); }\n",
        "int f(int a) { return a+; }\n",
        "int f(int a) { return a ? : ; }\n",
        "int f(int a) { return a[; }\n",
    };
    PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    char path[4096];
    snprintf(path, sizeof(path), "%s/fragment.c", directory);
    uint32_t failures = 0;
    for (uint32_t k = 0; k < sizeof(sources) / sizeof(sources[0]); k += 1)
    {
        const bool written = parse_test_write(path, sources[k], strlen(sources[k]));
        const uint32_t failed = written ? c_fragment_test_file(path, &options, 1, totals, (int)k, 0) : 1;
        if (failed != 0)
        {
            fprintf(stderr, "fragment fixture %u: %s", k, sources[k]);
        }
        failures += failed;
    }
    for (uint32_t k = 0; k < sizeof(invalid) / sizeof(invalid[0]); k += 1)
    {
        const bool written = parse_test_write(path, invalid[k], strlen(invalid[k]));
        failures += written ? c_fragment_test_file(path, &options, 0, totals, -1, 0) : 1;
    }
    // Source depth, unary density, left association and right association are independent of fragment capacity.
    const uint32_t length = 100000;
    char* text = malloc((uint64_t)length * 4 + 128);
    if (text == 0)
    {
        failures += 1;
    }
    else
    {
        for (uint32_t mode = 0; mode < 4; mode += 1)
        {
            size_t at = (size_t)sprintf(text, "int f(int a) { return ");
            if (mode < 2)
            {
                for (uint32_t k = 0; k < length; k += 1)
                {
                    text[at] = mode == 0 ? '(' : '!';
                    at += 1;
                }
                text[at] = 'a';
                at += 1;
                if (mode == 0)
                {
                    memset(text + at, ')', length);
                    at += length;
                }
            }
            else
            {
                for (uint32_t k = 0; k < length; k += 1)
                {
                    text[at] = 'a';
                    text[at + 1] = mode == 2 ? '+' : '=';
                    at += 2;
                }
                text[at] = 'a';
                at += 1;
            }
            memcpy(text + at, "; }\n", 4);
            at += 4;
            failures += parse_test_write(path, text, at) ? c_fragment_test_file(path, &options, 1, totals, -1, 0) : 1;
        }
        free(text);
    }
    unlink(path);
    return failures;
}

static uint32_t c_fragment_test_parser_snippets(const char* directory, CFragmentTestTotals* totals)
{
    uint32_t failures = 0;
    uint32_t accepted = 0;
    uint32_t rejected = 0;
    const uint32_t count = sizeof(parse_test_snippets) / sizeof(parse_test_snippets[0]);
    char path[4096];
    for (uint32_t k = 0; k < count; k += 1)
    {
        const ParseTestSnippet* snippet = &parse_test_snippets[k];
        const PPOptions options = parse_test_snippet_options(snippet);
        snprintf(path, sizeof(path), "%s/parser_snippet_%u.c", directory, k);
        const bool written = parse_test_write(path, snippet->source, strlen(snippet->source));
        const bool expected_acceptance = snippet->error[0] == 0;
        const uint32_t failed = written ? c_fragment_test_file(path, &options, expected_acceptance, totals, -1, snippet->error) : 1;
        if (failed != 0)
        {
            fprintf(stderr, "fragment parser snippet %u: %s", k, snippet->source);
        }
        else
        {
            accepted += expected_acceptance;
            rejected += !expected_acceptance;
        }
        failures += failed;
        unlink(path);
    }
    printf("fragment parser-snippets: requested=%u accepted=%u rejected=%u failures=%u\n", count, accepted, rejected, failures);
    return failures;
}

static void c_fragment_test_print_totals(const char* dataset, const CFragmentTestTotals* totals)
{
    for (uint32_t k = 0; k < 3; k += 1)
    {
        const CFragmentTestTotals* total = &totals[k];
        const CFragmentStatistics* stats = &total->statistics;
        printf("fragments dataset=%s payload=%u files=%llu tokens=%llu expressions=%llu nodes=%llu source_events=%llu fragments=%llu fragments/expression=%.6f positions=%.6f operators=%.6f "
               "reductions=%llu operator_bytes_copied=%llu identity_bytes_copied=%llu next_field_bytes_written=%llu operator_copy/reduction=%.6f next_write/reduction=%.6f "
               "peak_live_hot=%llu peak_live_identity=%llu peak_hot_backing=%llu peak_identity_backing=%llu peak_ownership_backing=%llu peak_syntax_backing=%llu "
               "peak_inclusive_experiment_arena=%llu reused=%llu\n",
               dataset, 16 * (k + 1), (unsigned long long)total->files, (unsigned long long)total->tokens, (unsigned long long)total->expressions, (unsigned long long)total->nodes,
               (unsigned long long)total->source_events, (unsigned long long)stats->fragments, total->expressions ? (double)stats->fragments / total->expressions : 0,
               stats->fragments ? (double)stats->positions / (64 * stats->fragments) : 0, stats->fragments ? (double)stats->operators / (16 * (k + 1) * stats->fragments) : 0,
               (unsigned long long)stats->reductions, (unsigned long long)stats->copied_bytes, (unsigned long long)stats->identity_bytes_copied, (unsigned long long)stats->linked_bytes,
               stats->reductions ? (double)stats->copied_bytes / stats->reductions : 0, stats->reductions ? (double)stats->linked_bytes / stats->reductions : 0,
               (unsigned long long)total->hot_peak, (unsigned long long)total->identity_peak, (unsigned long long)total->hot_backing_peak, (unsigned long long)total->identity_backing_peak,
               (unsigned long long)total->ownership_backing_peak, (unsigned long long)total->syntax_backing_peak, (unsigned long long)total->inclusive_arena_peak, (unsigned long long)stats->reused);
    }
}

// Corpus acceptance uses explicit files, so directory traversal cannot silently omit unreadable entries.
static bool c_fragment_test_add_file(const char* path, char** files, uint32_t* count, uint32_t capacity)
{
    struct stat status;
    bool result = *count < capacity && stat(path, &status) == 0 && S_ISREG(status.st_mode);
    char* copy = result ? strdup(path) : 0;
    result &= copy != 0;
    if (result)
    {
        files[*count] = copy;
        *count += 1;
    }
    else
    {
        fprintf(stderr, "test-fragments: expected an accessible regular file with available input capacity: %s\n", path);
    }
    return result;
}

static int run_fragment_tests(int argument_count, char** arguments)
{
    uint32_t failures = c_fragment_store_tests();
    CFragmentTestTotals totals[3] = { 0 };
    CFragmentTestTotals fixture_totals[3] = { 0 };
    CFragmentTestTotals snippet_totals[3] = { 0 };
    char** options_arguments = malloc(((uint64_t)argument_count + 2) * sizeof(char*));
    char** files = malloc((1u << 20) * sizeof(char*));
    uint32_t file_count = 0;
    int option_count = 0;
    bool fixtures = true;
    bool valid = options_arguments != 0 && files != 0;
    for (int k = 0; valid && k < argument_count; k += 1)
    {
        const char* argument = arguments[k];
        if (strcmp(argument, "--no-fixtures") == 0)
        {
            fixtures = false;
        }
        else if (strcmp(argument, "--files-from") == 0 && k + 1 < argument_count)
        {
            k += 1;
            FILE* list = fopen(arguments[k], "r");
            valid = list != 0;
            const uint32_t initial_count = file_count;
            char path[4096];
            while (valid && fgets(path, sizeof(path), list) != 0)
            {
                const size_t length = strcspn(path, "\r\n");
                valid = length < sizeof(path) - 1;
                path[length] = 0;
                if (valid && length != 0)
                {
                    valid = c_fragment_test_add_file(path, files, &file_count, 1u << 20);
                }
            }
            if (list != 0)
            {
                valid &= !ferror(list);
                fclose(list);
            }
            valid &= file_count > initial_count;
        }
        else if (argument[0] != '-')
        {
            valid = c_fragment_test_add_file(argument, files, &file_count, 1u << 20);
        }
        else
        {
            options_arguments[option_count] = arguments[k];
            option_count += 1;
            const bool valued = pp_own_option_with_value(argument) || strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 ||
                                strcmp(argument, "-isystem") == 0 || strcmp(argument, "-include") == 0 || strcmp(argument, "-iquote") == 0 ||
                                strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-x") == 0 || strcmp(argument, "-target") == 0;
            if (valued && k + 1 < argument_count)
            {
                k += 1;
                options_arguments[option_count] = arguments[k];
                option_count += 1;
            }
        }
    }
    Arena arena = arena_reserve(1ull << 26);
    valid &= fixtures || file_count != 0;
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    if (valid)
    {
        options_arguments[option_count] = "placeholder.c";
        valid = pp_parse_options(option_count + 1, options_arguments, &options, &arena, &input, &output, &markers);
    }
    char directory[4096];
    snprintf(directory, sizeof(directory), "%s/buster-test-fragments-XXXXXX", getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp");
    valid = valid && mkdtemp(directory) != 0;
    if (valid)
    {
        if (fixtures)
        {
            failures += c_fragment_test_fixtures(directory, fixture_totals);
            failures += c_fragment_test_parser_snippets(directory, snippet_totals);
        }
        for (uint32_t f = 0; f < file_count; f += 1)
        {
            failures += c_fragment_test_file(files[f], &options, 1, totals, -1, 0);
        }
        rmdir(directory);
    }
    else
    {
        fprintf(stderr, "usage: test-fragments [--no-fixtures] [--files-from FILE] [-E options] [file...]\n");
        failures += 1;
    }
    if (fixtures)
    {
        c_fragment_test_print_totals("fixtures", fixture_totals);
        c_fragment_test_print_totals("parser-snippets", snippet_totals);
    }
    if (file_count != 0)
    {
        c_fragment_test_print_totals("corpus", totals);
    }
    printf("test-fragments: %u failures\n", failures);
    for (uint32_t f = 0; f < file_count; f += 1)
    {
        free(files[f]);
    }
    free(files);
    free(options_arguments);
    arena_release(&arena);
    return failures == 0 ? 0 : 1;
}
