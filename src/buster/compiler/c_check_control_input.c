// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Labels use function scope and a separate namespace. Build one bounded table keyed by body and identifier,
// then resolve forward gotos. Case ownership follows parser parents; the typed program pass converts case values
// to the promoted selector type and checks overlapping cases/defaults after expression checking.

static void check_input_region_prepare(CheckInputReader* reader, uint32_t opening)
{
    const Parser* parser = reader->parser;
    const uint32_t closing = parser->tree.parents[opening];
    const uint32_t block = parser->tree.parents[opening + 1];
    bool valid = reader->function_body < parser->tokens.count && closing < parser->tokens.count &&
                 block + 1 == closing && parser->tree.roles[block] == PARSE_ROLE_CLOSE_BLOCK;
    uint32_t final = BUSTER_CHECK_NONE;
    if (valid)
    {
        for (uint32_t child = reader->first_child[block]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            if (parser->tree.roles[child] != PARSE_ROLE_OPEN_BLOCK)
            {
                final = child;
            }
        }
        bool expression = final < parser->tokens.count && parser->tree.roles[final] == PARSE_ROLE_TERMINATOR &&
                          reader->first_child[final] != BUSTER_CHECK_NONE;
        for (uint32_t child = expression ? reader->first_child[final] : BUSTER_CHECK_NONE; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            const uint8_t role = parser->tree.roles[child];
            const uint8_t kind = parser->tokens.kinds[child];
            expression &= role != PARSE_ROLE_SPECIFIER && role != PARSE_ROLE_RETURN && kind != PARSE_KIND_GOTO &&
                          kind != PARSE_KIND_STRUCT && kind != PARSE_KIND_ENUM && kind != PARSE_KIND_STATIC_ASSERT &&
                          !check_input_attribute_starts(parser, child, final);
        }
        final = expression ? final : BUSTER_CHECK_NONE;
        const uint32_t index = reader->output->region_count++;
        reader->output->regions[index] = (CheckInputRegion)
        {
            .opening = opening,
            .closing = closing,
            .block = block,
            .body = reader->function_body,
            .result = BUSTER_CHECK_NONE,
            .final_statement = final,
        };
        reader->output->expression_regions[opening] = index + 1;
        reader->output->expression_regions[closing] = index + 1;
        reader->output->expression_regions[block] = index + 1;
    }
    else
    {
        check_input_report(reader, opening, CHECK_INPUT_DECLARATION);
    }
}

static bool check_input_region_transfer(const CheckInputReader* reader, uint32_t source, uint32_t target)
{
    const Parser* parser = reader->parser;
    bool valid = true;
    // Most units have no GNU statement expression. Otherwise only regions below the source and target's common
    // ancestor can be entered: a region above it holds both. The reader's postorder intervals recognize that ancestor
    // without another token column or a walk from the source. Keep the full parent walk on the scalar path as oracle.
    const bool scalar = parser->options.scalar;
    const uint32_t source_position = !scalar && reader->output->region_count != 0 && source < parser->tokens.count ? reader->postorder_positions[source] : BUSTER_CHECK_NONE;
    for (uint32_t ancestor = target; valid && ancestor < parser->tokens.count && (scalar || reader->output->region_count != 0); ancestor = parser->tree.parents[ancestor])
    {
        const uint32_t selected = reader->output->expression_regions[ancestor];
        if (selected != 0)
        {
            const CheckInputRegion* region = &reader->output->regions[selected - 1];
            valid = source > region->opening && source < region->closing;
        }
        if (!scalar)
        {
            const uint32_t position = reader->postorder_positions[ancestor];
            const uint32_t size = reader->subtree_sizes[position];
            if (source_position <= position && position - source_position < size)
            {
                break;
            }
        }
    }
    return valid;
}

static void check_input_statement_attributes(CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    const uint32_t after = check_input_skip_attributes(parser, first, parser->tokens.count);
    if (after < parser->tokens.count && parser->tokens.kinds[after] == PARSE_KIND_SEMI)
    {
        uint32_t cursor = first;
        CheckInputAttributes* attributes = 0;
        bool valid = check_input_read_attributes(reader, &cursor, parser->tokens.count, &attributes) && cursor == after &&
                     attributes != 0 && reader->function_body < parser->tokens.count;
        for (uint32_t i = 0; valid && i < attributes->count; i += 1)
        {
            valid = attributes->records[i].kind == CHECK_ATTRIBUTE_FALLTHROUGH;
        }
        if (valid)
        {
            reader->output->attributes[after] = attributes->count != 0 ? attributes : 0;
        }
        else
        {
            check_input_report(reader, first, CHECK_INPUT_DECLARATION);
        }
    }
}

// GNU C's labels as values: whether a goto is a computed one, goto *expression (the `*` after it its child, whose child is
// the whole expression: the parser's PARSE_MODE_GOTO), and whether a token is an address of a label, &&name (a `&&`
// prefix, of the name after it).
static bool check_input_indirect_goto(const Parser* parser, uint32_t token)
{
    const uint32_t star = token + 1;
    return parser->tokens.kinds[token] == PARSE_KIND_GOTO && star < parser->tokens.count && parser->tree.parents[star] == token &&
           parser->tree.roles[star] == PARSE_ROLE_RETURN && parser->tokens.kinds[star] == PARSE_KIND_STAR;
}

static bool check_input_label_address(const Parser* parser, uint32_t token)
{
    return parser->tree.roles[token] == PARSE_ROLE_PREFIX && parser->tokens.kinds[token] == PARSE_KIND_AMPAMP;
}

// The label of `body` spelled `name` in check_input_controls' table (`capacity` slots), or BUSTER_CHECK_NONE.
static uint32_t check_input_find_label(const CheckInputReader* reader, const uint32_t* labels, uint64_t capacity, uint32_t body, uint32_t name)
{
    const Parser* parser = reader->parser;
    uint32_t result = BUSTER_CHECK_NONE;
    uint64_t slot = ((uint64_t)name * UINT64_C(11400714819323198485) + body) & (capacity - 1);
    while (result == BUSTER_CHECK_NONE && labels[slot] != 0)
    {
        const uint32_t target = labels[slot] - 1;
        const uint32_t target_name = reader->first_child[target];
        result = reader->output->declaration_bodies[target] == body && parser->tokens.names[target_name] == name ? target : BUSTER_CHECK_NONE;
        slot = (slot + 1) & (capacity - 1);
    }
    return result;
}

// The computed gotos of each function with one, as clang's JumpScopeChecker takes them: a function without an address of
// a label refuses its first, and every one must be inside each statement expression that holds a label whose address the
// function takes, since it may go to any of them. Those regions are nested intervals, so the labels' innermost ones
// intersect in one, (low, high), which each goto must be in. (check_input_vla_finish does the same for the scopes of
// variable-length arrays.)
static void check_input_indirect_gotos(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    for (uint32_t opening = 0; opening < count; opening += 1)
    {
        const bool deferred = reader->output->deferred_tokens != 0 && reader->output->deferred_tokens[opening] != 0;
        const uint32_t closing = parser->tree.parents[opening];
        if (!deferred && parser->tree.roles[opening] == PARSE_ROLE_OPEN_BODY && closing < count && closing < reader->fragment_end)
        {
            uint32_t low = 0;
            uint32_t high = UINT32_MAX;
            uint32_t first = BUSTER_CHECK_NONE;
            bool addressed = false;
            for (uint32_t token = opening + 1; token < closing; token += 1)
            {
                const uint32_t label = check_input_label_address(parser, token) ? reader->output->label_targets[token] : BUSTER_CHECK_NONE;
                addressed |= label != BUSTER_CHECK_NONE;
                for (uint32_t ancestor = label; ancestor < count; ancestor = parser->tree.parents[ancestor])
                {
                    const uint32_t selected = reader->output->expression_regions[ancestor];
                    if (selected != 0)
                    {
                        const CheckInputRegion* region = &reader->output->regions[selected - 1];
                        low = region->opening > low ? region->opening : low;
                        high = region->closing < high ? region->closing : high;
                    }
                }
                first = first == BUSTER_CHECK_NONE && check_input_indirect_goto(parser, token) && reader->output->asm_ids[token] == 0 ? token : first;
            }
            if (first != BUSTER_CHECK_NONE && !addressed)
            {
                check_input_report(reader, first, CHECK_INPUT_DECLARATION);
            }
            for (uint32_t token = first; addressed && token < closing; token += 1)
            {
                if (check_input_indirect_goto(parser, token) && reader->output->asm_ids[token] == 0 && (token <= low || token >= high))
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
            }
            opening = closing;
        }
    }
}

// These walks only read the parser's kind/role columns; resolving controls and reporting diagnostics write the
// reader's output instead. Keep a block's exact event mask, then visit its set bits in source order. The scalar
// reader retains the original full traversal, independently of this filter. count is at most one 64-byte block.
#define BUSTER_CHECK_CONTROL_TOKENS 64u

static uint64_t check_input_control_mask(const uint8_t* kinds, const uint8_t* roles, uint32_t count, bool resolving)
{
    const uint64_t valid = count == BUSTER_CHECK_CONTROL_TOKENS ? UINT64_MAX : (UINT64_C(1) << count) - 1;
    const __m512i kind = _mm512_maskz_loadu_epi8(valid, kinds);
    const __m512i role = _mm512_maskz_loadu_epi8(valid, roles);
    uint64_t selected = _mm512_cmpeq_epi8_mask(kind, _mm512_set1_epi8(PARSE_KIND_GOTO)) |
                        (_mm512_cmpeq_epi8_mask(kind, _mm512_set1_epi8(PARSE_KIND_AMPAMP)) &
                         _mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(PARSE_ROLE_PREFIX)));
    if (resolving)
    {
        selected |= _mm512_cmpeq_epi8_mask(kind, _mm512_set1_epi8(PARSE_KIND_SEMI));
    }
    else
    {
        selected |= _mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(PARSE_ROLE_OPEN_BODY)) |
                    _mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(PARSE_ROLE_LABEL)) |
                    _mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(PARSE_ROLE_OPEN_CASE));
    }
    return selected & valid;
}

typedef struct CheckInputControlScan CheckInputControlScan;
struct CheckInputControlScan
{
    uint64_t selected;
    uint32_t first;
    uint32_t after;
};

static uint32_t check_input_control_next(const Parser* parser, CheckInputControlScan* scan, bool resolving)
{
    const uint32_t count = parser->tokens.count;
    while (scan->selected == 0 && scan->after < count)
    {
        const uint32_t remaining = count - scan->after;
        const uint32_t length = remaining < BUSTER_CHECK_CONTROL_TOKENS ? remaining : BUSTER_CHECK_CONTROL_TOKENS;
        scan->first = scan->after;
        scan->selected = check_input_control_mask(parser->tokens.kinds + scan->first, parser->tree.roles + scan->first, length, resolving);
        // length <= count - after: after reaches count exactly, even at the u32 limit.
        scan->after += length;
    }
    uint32_t token = count;
    if (scan->selected != 0)
    {
        token = scan->first + (uint32_t)__builtin_ctzll(scan->selected);
        scan->selected &= scan->selected - 1;
    }
    return token;
}

// before == NONE is the unchanged full-unit path. The diagnostic prefix collects all function-scoped label names
// for forward references, but reports only sites before its completion and defers unfinished-function obligations.
static __attribute__((always_inline)) void check_input_controls_mode(CheckInputReader* reader, uint32_t before)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool scalar = parser->options.scalar;
    const bool prefix = before != BUSTER_CHECK_NONE;
    const uint32_t limit = prefix && before < count ? before : count;
    const uint64_t mark = reader->scratch->length;
    uint64_t capacity = 1;
    while (capacity < (uint64_t)count * 2 + 1)
    {
        capacity *= 2;
    }
    uint32_t* labels = arena_push_array(reader->scratch, capacity, sizeof(uint32_t), 4);
    reader->output->resource |= labels == 0;
    if (labels != 0)
    {
        memset(labels, 0, capacity * sizeof(uint32_t));
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t close = 0;
        CheckInputControlScan scan = { 0 };
        for (uint32_t token = 0; token < count; token += 1)
        {
            if (!scalar)
            {
                token = check_input_control_next(parser, &scan, false);
                if (token == count)
                {
                    break;
                }
            }
            if (reader->output->deferred_tokens != 0 && reader->output->deferred_tokens[token] != 0)
            {
                continue;
            }
            const uint8_t role = parser->tree.roles[token];
            const uint8_t kind = parser->tokens.kinds[token];
            if (token > close)
            {
                // No consumer observes body between control events, so its expiry may wait for the next event.
                body = BUSTER_CHECK_NONE;
            }
            if (role == PARSE_ROLE_OPEN_BODY)
            {
                body = token;
                close = parser->tree.parents[token];
            }
            if (role == PARSE_ROLE_LABEL)
            {
                const uint32_t name_token = reader->first_child[token];
                if (body == BUSTER_CHECK_NONE || name_token >= count || parser->tokens.kinds[name_token] != PARSE_KIND_IDENTIFIER)
                {
                    if (!prefix || token < limit)
                    {
                        check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                    }
                }
                else
                {
                    reader->output->declaration_bodies[token] = body;
                    const uint32_t name = parser->tokens.names[name_token];
                    uint64_t slot = ((uint64_t)name * UINT64_C(11400714819323198485) + body) & (capacity - 1);
                    while (labels[slot] != 0)
                    {
                        const uint32_t old = labels[slot] - 1;
                        const uint32_t old_name = reader->first_child[old];
                        if (reader->output->declaration_bodies[old] == body && parser->tokens.names[old_name] == name)
                        {
                            if (!prefix || token < limit)
                            {
                                check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
                            }
                            break;
                        }
                        slot = (slot + 1) & (capacity - 1);
                    }
                    labels[slot] = token + 1;
                }
            }
            else if ((kind == PARSE_KIND_GOTO && reader->output->asm_ids[token] == 0) || (kind == PARSE_KIND_AMPAMP && role == PARSE_ROLE_PREFIX))
            {
                reader->output->declaration_bodies[token] = body;
            }
            else if (role == PARSE_ROLE_OPEN_CASE && (!prefix || token < limit))
            {
                const uint32_t end = parser->tree.parents[token];
                uint32_t owner = end < count ? parser->tree.parents[end] : count;
                while (owner < count)
                {
                    const uint32_t keyword = reader->first_child[owner];
                    if (parser->tree.roles[owner] == PARSE_ROLE_CLOSE_CONDITION && keyword < count && parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH)
                    {
                        break;
                    }
                    owner = parser->tree.parents[owner];
                }
                const bool complete = !prefix || (owner < limit && end < limit);
                if (owner >= count || end >= count || (complete && !check_input_region_transfer(reader, owner, end)))
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
                else if (complete)
                {
                    reader->output->control_owners[token] = owner;
                    reader->output->control_owners[end] = owner;
                }
            }
        }
        scan = (CheckInputControlScan){ 0 };
        for (uint32_t token = 0; token < limit; token += 1)
        {
            if (!scalar)
            {
                token = check_input_control_next(parser, &scan, true);
                if (token >= limit)
                {
                    break;
                }
            }
            if (reader->output->deferred_tokens != 0 && reader->output->deferred_tokens[token] != 0)
            {
                continue;
            }
            const uint8_t kind = parser->tokens.kinds[token];
            const bool jump = kind == PARSE_KIND_GOTO && reader->output->asm_ids[token] == 0;
            const bool address = kind == PARSE_KIND_AMPAMP && parser->tree.roles[token] == PARSE_ROLE_PREFIX;
            if (jump && check_input_indirect_goto(parser, token))
            {
                // goto *expression: its labels are every one whose address its function takes (check_input_indirect_gotos).
                reader->indirect_gotos += 1;
            }
            else if (jump || address)
            {
                // A goto's label, or the label whose address &&name is: the same function's (clang names a missing one at
                // its name, and an address of a label outside a function too).
                const uint32_t body = reader->output->declaration_bodies[token];
                const uint32_t operand = reader->first_child[token];
                uint32_t target = body != BUSTER_CHECK_NONE && operand < count && parser->tokens.kinds[operand] == PARSE_KIND_IDENTIFIER ?
                                        check_input_find_label(reader, labels, capacity, body, parser->tokens.names[operand]) : BUSTER_CHECK_NONE;
                const bool unfinished = body < count && parser->tree.roles[body] == PARSE_ROLE_OPEN_BODY &&
                                        parser->tree.parents[body] >= reader->fragment_end;
                if (prefix && unfinished && address && target == BUSTER_CHECK_NONE && operand < count && parser->tokens.kinds[operand] == PARSE_KIND_IDENTIFIER)
                {
                    // &&name implicitly declares a function-scoped label before its definition. Its canonical first
                    // reference is enough to type/fold this diagnostic-only graph; a definition is required only at
                    // function completion. These reference identities never reach an accepted or emitted unit.
                    uint64_t slot = ((uint64_t)parser->tokens.names[operand] * UINT64_C(11400714819323198485) + body) & (capacity - 1);
                    while (labels[slot] != 0)
                    {
                        slot = (slot + 1) & (capacity - 1);
                    }
                    labels[slot] = token + 1;
                    target = token;
                }
                if (target != BUSTER_CHECK_NONE)
                {
                    reader->output->label_targets[token] = target;
                    reader->output->label_address_count += address;
                    if (!address && (!prefix || !unfinished) && !check_input_region_transfer(reader, token, target))
                    {
                        check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                    }
                }
                if (target == BUSTER_CHECK_NONE && !unfinished)
                {
                    check_input_report(reader, address && operand < count ? operand : token, CHECK_INPUT_UNDECLARED);
                }
            }
            else if (kind == PARSE_KIND_SEMI && reader->output->attributes[token] != 0)
            {
                bool enclosed = false;
                for (uint32_t ancestor = parser->tree.parents[token]; ancestor < count && !enclosed; ancestor = parser->tree.parents[ancestor])
                {
                    const uint32_t keyword = reader->first_child[ancestor];
                    enclosed = parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_CONDITION && keyword < count &&
                               parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH;
                }
                if (!enclosed)
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
            }
        }
        if (reader->indirect_gotos != 0)
        {
            check_input_indirect_gotos(reader);
        }
    }
    arena_pop_to(reader->scratch, mark);
}

static void check_input_controls(CheckInputReader* reader)
{
    check_input_controls_mode(reader, BUSTER_CHECK_NONE);
}

static __attribute__((noinline)) void check_input_controls_prefix(CheckInputReader* reader, uint32_t before)
{
    const uint32_t fragment_end = reader->fragment_end;
    reader->fragment_end = before < fragment_end ? before : fragment_end;
    check_input_controls_mode(reader, before);
    reader->fragment_end = fragment_end;
}
