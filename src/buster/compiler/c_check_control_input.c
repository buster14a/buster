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
        // A labeled last statement is its statement, through its labels, as clang has it: `({ ...; out: r; })` is r.
        for (uint32_t steps = 0; final < parser->tokens.count && parser->tree.roles[final] == PARSE_ROLE_LABEL && steps < parser->tokens.count; steps += 1)
        {
            uint32_t statement = BUSTER_CHECK_NONE;
            for (uint32_t child = reader->first_child[final]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                statement = child > final ? child : statement;
            }
            final = statement;
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
        check_input_sparse_mark(reader, opening, true);
        check_input_sparse_mark(reader, closing, true);
        check_input_sparse_mark(reader, block, true);
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

// A musttail return statement's record (CheckInput.musttails), in source order.
static bool check_input_musttail_record(CheckInputReader* reader, CheckInputMusttail musttail)
{
    CheckInput* input = reader->output;
    bool valid = true;
    if (input->musttail_count == input->musttail_capacity)
    {
        const uint32_t capacity = input->musttail_capacity != 0 ? input->musttail_capacity * 2 : 16;
        CheckInputMusttail* grown = capacity > input->musttail_capacity ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputMusttail, capacity) : NULL;
        valid = grown != NULL;
        if (valid && input->musttail_count != 0)
        {
            memcpy(grown, input->musttails, (uint64_t)input->musttail_count * sizeof(CheckInputMusttail));
        }
        input->musttails = valid ? grown : input->musttails;
        input->musttail_capacity = valid ? capacity : input->musttail_capacity;
    }
    if (valid)
    {
        input->musttails[input->musttail_count++] = musttail;
    }
    input->resource |= !valid;
    return valid;
}

// Whether attributes from `first` to `after` name clang's musttail, by their names alone, without reading them: the one
// attribute a statement other than a null one or a return has here (check_input_statement_attributes).
static bool check_input_names_musttail(CheckInputReader* reader, uint32_t first, uint32_t after)
{
    const Parser* parser = reader->parser;
    bool found = false;
    for (uint32_t token = first; !found && token < after; token += 1)
    {
        found = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && check_input_attribute_equal(check_input_attribute_name(reader, parser->tokens.names[token]), "musttail");
    }
    return found;
}

// Whether the token after a block item's attributes starts a declaration, as the parser read it: a specifier's keyword,
// or a typedef's name (the parser's specifier role), rather than a statement's.
static bool check_input_declaration_after(const Parser* parser, uint32_t token)
{
    const uint8_t kind = parser->tokens.kinds[token];
    return kind == PARSE_KIND_TYPE || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_ATOMIC ||
           kind == PARSE_KIND_CALLING_CONVENTION || kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_TYPEDEF || kind == PARSE_KIND_ALIGNAS ||
           kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_STATIC_ASSERT || kind == PARSE_KIND_LABEL ||
           (kind == PARSE_KIND_IDENTIFIER && parser->tree.roles[token] == PARSE_ROLE_SPECIFIER);
}

// A block item's attributes before a statement rather than a declaration, read as the statement's, which check_input's
// walk then takes no declaration for (their tokens ignored):
//   - a null statement's: fallthrough's, which its consumer checks, and musttail's, clang's error;
//   - a return's: musttail's alone (#147), recorded for c_check_musttail.c and check_program, the return's other
//     attributes refused (clang's error, that they don't apply to a statement);
//   - another statement's with musttail among them: clang's error.
// GNU attributes right after a named label are the label's, as clang reads them, which their reading as a
// declaration's (check_input_declaration) refuses musttail on; C23's there are the statement's.
static void check_input_statement_attributes(CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t after = check_input_skip_attributes(parser, first, count);
    const uint8_t next = after < count ? parser->tokens.kinds[after] : PARSE_KIND_SEMI;
    const bool label = first != 0 && parser->tree.roles[first - 1] == PARSE_ROLE_LABEL && parser->tokens.kinds[first] == PARSE_KIND_ATTRIBUTE;
    const bool returns = after < count && next == PARSE_KIND_RETURN && !label;
    const bool named = after < count && !returns && !label && check_input_names_musttail(reader, first, after);
    const bool other = named && next != PARSE_KIND_SEMI && !check_input_declaration_after(parser, after);
    if (after < count && next == PARSE_KIND_SEMI)
    {
        uint32_t cursor = first;
        CheckInputAttributes* attributes = 0;
        reader->statement_attributes = true;
        bool valid = check_input_read_attributes(reader, &cursor, count, &attributes) && cursor == after && attributes != 0 &&
                     reader->function_body < count;
        reader->statement_attributes = false;
        uint32_t musttail = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; valid && i < attributes->count; i += 1)
        {
            musttail = attributes->records[i].kind == CHECK_ATTRIBUTE_MUSTTAIL ? attributes->records[i].first : BUSTER_CHECK_NONE;
            valid = attributes->records[i].kind == CHECK_ATTRIBUTE_FALLTHROUGH;
        }
        if (valid)
        {
            reader->output->attributes[after] = attributes->count != 0 ? attributes : 0;
        }
        else if (named)
        {
            // musttail's, which no declaration then reads again: clang's error at it, unless its reading reported one.
            if (musttail != BUSTER_CHECK_NONE)
            {
                check_input_report(reader, musttail, CHECK_INPUT_MUSTTAIL_STATEMENT);
            }
            for (uint32_t token = first; token < after; token += 1)
            {
                reader->ignored[token] = true;
            }
        }
        else
        {
            check_input_report(reader, first, CHECK_INPUT_DECLARATION);
        }
    }
    else if (returns || other)
    {
        uint32_t cursor = first;
        CheckInputAttributes* attributes = 0;
        reader->statement_attributes = true;
        bool valid = check_input_read_attributes(reader, &cursor, count, &attributes) && cursor == after && attributes != 0 &&
                     reader->function_body < count;
        reader->statement_attributes = false;
        uint32_t musttail = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; valid && i < attributes->count; i += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + i;
            musttail = musttail == BUSTER_CHECK_NONE && attribute->kind == CHECK_ATTRIBUTE_MUSTTAIL ? attribute->first : musttail;
            valid = attribute->kind == CHECK_ATTRIBUTE_MUSTTAIL;
            if (!valid)
            {
                check_input_report(reader, attribute->first, CHECK_INPUT_UNSUPPORTED);
            }
        }
        if (valid && returns)
        {
            const CheckInputMusttail record = { .statement = after, .attributes = first, .name = musttail, .body = reader->function_body };
            valid = check_input_musttail_record(reader, record);
            // Its function's own type and its scopes' cleanups, which the check reads, are source order's.
            check_input_lane_refuse(reader, reader->lane, "a body has a musttail return");
        }
        else if (valid)
        {
            check_input_report(reader, musttail, CHECK_INPUT_MUSTTAIL_STATEMENT);
        }
        for (uint32_t token = first; token < after; token += 1)
        {
            reader->ignored[token] = true;
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

// GNU C's local labels (#147): `__label__ a, b;` at a block's start, whose names are labels of that block
// (CheckLocalLabels) and declare nothing else. Its tokens are the reader's, which the walks then ignore.
static void check_input_local_labels(CheckInputReader* reader, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t end = parser->tree.parents[keyword];
    bool valid = reader->function_body < count && end < count && end > keyword && parser->tokens.kinds[end] == PARSE_KIND_SEMI;
    for (uint32_t token = keyword; valid && token <= end; token += 1)
    {
        // Names, a comma between each two, as the parser reads the list (PARSE_MODE_LOCAL_LABEL_NAME).
        const uint8_t expected = ((token - keyword) & 1) != 0 ? PARSE_KIND_IDENTIFIER : PARSE_KIND_COMMA;
        valid = token == keyword || token == end || parser->tokens.kinds[token] == expected;
        reader->ignored[token] = valid;
    }
    if (!valid)
    {
        check_input_report(reader, keyword, CHECK_INPUT_DECLARATION);
    }
}

// The labels in scope of the `__label__` lists of the blocks around the controls' scan, as clang's scopes have them: each
// name a list declares is a label of the list's block, whose definitions and uses inside the block (a nested block's too,
// but where a nested list declares the name again) are that label's, and a name no list in scope declares is the
// function's. The scan meets each list before its block's labels and uses (lists start blocks), pushes its names, and
// pops them past the block's closing brace. A label's definitions and uses take a scope: the name token in the innermost
// list that declares it, or the body, which the controls' table keys a label by with its name. Built on a unit's first
// list: a unit without one builds none of it, and its labels' scopes are their bodies (CheckInput.declaration_bodies).
typedef struct CheckLocalLabels CheckLocalLabels;
struct CheckLocalLabels
{
    // Per name: 1 + the entry of its innermost list in scope, or 0.
    uint32_t* heads;
    // Per entry: its name, the head it shadowed, its name token in the list, and its block's closing brace.
    uint32_t* names;
    uint32_t* shadowed;
    uint32_t* tokens;
    uint32_t* ends;
    // Per token: 1 + a label definition's or use's scope, or 0 for its body.
    uint32_t* scopes;
    uint32_t count;
};

// A label definition's or use's scope (CheckLocalLabels).
static uint32_t check_input_label_scope(const CheckInputReader* reader, const CheckLocalLabels* locals, uint32_t token)
{
    return locals->scopes != NULL && locals->scopes[token] != 0 ? locals->scopes[token] - 1 : reader->output->declaration_bodies[token];
}

// The label of `scope` spelled `name` in check_input_controls' table (`capacity` slots), or BUSTER_CHECK_NONE.
static uint32_t check_input_find_label(const CheckInputReader* reader, const CheckLocalLabels* locals, const uint32_t* labels, uint64_t capacity, uint32_t scope,
                                       uint32_t name)
{
    const Parser* parser = reader->parser;
    uint32_t result = BUSTER_CHECK_NONE;
    uint64_t slot = ((uint64_t)name * UINT64_C(11400714819323198485) + scope) & (capacity - 1);
    while (result == BUSTER_CHECK_NONE && labels[slot] != 0)
    {
        const uint32_t target = labels[slot] - 1;
        const uint32_t target_name = reader->first_child[target];
        result = check_input_label_scope(reader, locals, target) == scope && parser->tokens.names[target_name] == name ? target : BUSTER_CHECK_NONE;
        slot = (slot + 1) & (capacity - 1);
    }
    return result;
}

// Pushes a `__label__` list's names (check_input_local_labels marked it), whose block ends at its statement's parent's
// parent: the block's closing brace, after the list's `;`. Makes the columns on a unit's first list: the heads per name,
// the entries and scopes per token, each of which takes at most one.
static void check_input_local_label_push(CheckInputReader* reader, CheckLocalLabels* locals, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t names = reader->pp->table->count;
    if (locals->heads == NULL)
    {
        locals->heads = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)names + 1);
        locals->names = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
        locals->shadowed = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
        locals->tokens = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
        locals->ends = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
        locals->scopes = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
        const bool made = locals->heads != NULL && locals->names != NULL && locals->shadowed != NULL && locals->tokens != NULL && locals->ends != NULL &&
                          locals->scopes != NULL;
        reader->output->resource |= !made;
        if (made)
        {
            memset(locals->heads, 0, ((uint64_t)names + 1) * sizeof(uint32_t));
            memset(locals->scopes, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        }
        else
        {
            locals->heads = NULL;
            locals->scopes = NULL;
        }
    }
    const uint32_t statement = parser->tree.parents[keyword];
    const uint32_t end = statement < count ? parser->tree.parents[statement] : count;
    for (uint32_t token = keyword + 1; locals->heads != NULL && end < count && token < statement; token += 1)
    {
        const uint32_t name = parser->tokens.names[token];
        if (parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && name < names && locals->count < count)
        {
            const uint32_t entry = locals->count++;
            locals->names[entry] = name;
            locals->shadowed[entry] = locals->heads[name];
            locals->tokens[entry] = token;
            locals->ends[entry] = end;
            locals->heads[name] = entry + 1;
        }
    }
}

// The entry of a name's innermost local label in scope, or BUSTER_CHECK_NONE: the function's label.
static uint32_t check_input_local_label(const CheckInputReader* reader, const CheckLocalLabels* locals, uint32_t name)
{
    return locals->heads != NULL && name < reader->pp->table->count && locals->heads[name] != 0 ? locals->heads[name] - 1 : BUSTER_CHECK_NONE;
}

// Whether a local label's name is declared twice in its block (clang's "reference to 'a' is ambiguous" where its block
// defines or uses it): the entry it shadowed is its block's too.
static bool check_input_local_label_ambiguous(const CheckLocalLabels* locals, uint32_t entry)
{
    const uint32_t shadowed = locals->shadowed[entry];
    return shadowed != 0 && locals->ends[shadowed - 1] == locals->ends[entry];
}

// Pops the local labels whose blocks end before `token`: every label of a block, a run of entries at the stack's top,
// together, and each one its block doesn't define (an ambiguous one never) reported at its name, in its block's lists'
// order, as clang reports them where it leaves the block. In a prefix's pass, only a block that ends before the limit.
static void check_input_local_label_pop(CheckInputReader* reader, CheckLocalLabels* locals, const uint32_t* labels, uint64_t capacity, uint32_t token,
                                        uint32_t limit)
{
    while (locals->count != 0 && locals->ends[locals->count - 1] < token)
    {
        const uint32_t end = locals->ends[locals->count - 1];
        uint32_t first = locals->count - 1;
        while (first != 0 && locals->ends[first - 1] == end)
        {
            first -= 1;
        }
        for (uint32_t entry = first; entry < locals->count && end < limit; entry += 1)
        {
            const uint32_t name_token = locals->tokens[entry];
            if (check_input_find_label(reader, locals, labels, capacity, name_token, locals->names[entry]) == BUSTER_CHECK_NONE)
            {
                check_input_report(reader, name_token, CHECK_INPUT_LABEL_UNDECLARED);
            }
        }
        for (uint32_t entry = locals->count; entry-- > first;)
        {
            locals->heads[locals->names[entry]] = locals->shadowed[entry];
        }
        locals->count = first;
    }
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
                first = first == BUSTER_CHECK_NONE && check_input_indirect_goto(parser, token) && check_input_asm_id(reader->output, token) == 0 ? token : first;
            }
            if (first != BUSTER_CHECK_NONE && !addressed)
            {
                check_input_report(reader, first, CHECK_INPUT_DECLARATION);
            }
            for (uint32_t token = first; addressed && token < closing; token += 1)
            {
                if (check_input_indirect_goto(parser, token) && check_input_asm_id(reader->output, token) == 0 && (token <= low || token >= high))
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
                    _mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(PARSE_ROLE_OPEN_CASE)) |
                    _mm512_cmpeq_epi8_mask(kind, _mm512_set1_epi8(PARSE_KIND_LABEL));
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

// Only definitions enter the full-unit table. Diagnostic prefixes can also insert an unresolved &&name
// in an unfinished function, so reserve for those address sites before prefix_count as well. Count a safe
// superset (including deferred/invalid/duplicate sites), keeping the table below half full without token-sized
// clearing. The normal path reads roles only; both columns are bounded, unpadded byte arrays.
static uint64_t check_input_label_capacity(const Parser* parser, uint32_t prefix_count)
{
    const uint32_t count = parser->tokens.count;
    uint64_t entries = 0;
    if (BUSTER_SCALAR_PATH(parser->options.scalar))
    {
        for (uint32_t token = 0; token < count; token += 1)
        {
            entries += parser->tree.roles[token] == PARSE_ROLE_LABEL || (token < prefix_count && check_input_label_address(parser, token));
        }
    }
    else
    {
        for (uint64_t first = 0; first < count; first += BUSTER_CHECK_CONTROL_TOKENS)
        {
            const uint32_t remaining = count - first;
            const uint32_t length = remaining < BUSTER_CHECK_CONTROL_TOKENS ? remaining : BUSTER_CHECK_CONTROL_TOKENS;
            const uint64_t valid = length == BUSTER_CHECK_CONTROL_TOKENS ? UINT64_MAX : (UINT64_C(1) << length) - 1;
            const __m512i roles = _mm512_maskz_loadu_epi8(valid, parser->tree.roles + first);
            uint64_t selected = _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_LABEL));
            if (first < prefix_count)
            {
                const uint32_t prefix_remaining = prefix_count - first;
                const uint32_t prefix_length = prefix_remaining < length ? prefix_remaining : length;
                const uint64_t prefix = prefix_length == BUSTER_CHECK_CONTROL_TOKENS ? UINT64_MAX : (UINT64_C(1) << prefix_length) - 1;
                const __m512i kinds = _mm512_maskz_loadu_epi8(prefix, parser->tokens.kinds + first);
                selected |= prefix & _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_PREFIX)) &
                            _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_AMPAMP));
            }
            entries += (uint64_t)__builtin_popcountll(selected & valid);
        }
    }
    uint64_t capacity = 1;
    while (capacity < entries * 2 + 1)
    {
        capacity *= 2;
    }
    return capacity;
}

// before == NONE is the unchanged full-unit path. The diagnostic prefix collects all function-scoped label names
// for forward references, but reports only sites before its completion and defers unfinished-function obligations.
static __attribute__((always_inline)) void check_input_controls_mode(CheckInputReader* reader, uint32_t before)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool scalar = BUSTER_SCALAR_PATH(parser->options.scalar);
    const bool prefix = before != BUSTER_CHECK_NONE;
    const uint32_t limit = prefix && before < count ? before : count;
    const uint64_t mark = reader->scratch->length;
    const uint64_t capacity = check_input_label_capacity(parser, prefix ? limit : 0);
    uint32_t* labels = arena_push_array(reader->scratch, capacity, sizeof(uint32_t), 4);
    reader->output->resource |= labels == 0;
    CheckLocalLabels locals = { 0 };
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
            if (locals.count != 0)
            {
                check_input_local_label_pop(reader, &locals, labels, capacity, token, limit);
            }
            if (role == PARSE_ROLE_OPEN_BODY)
            {
                body = token;
                close = parser->tree.parents[token];
            }
            if (kind == PARSE_KIND_LABEL && reader->ignored[token])
            {
                check_input_local_label_push(reader, &locals, token);
            }
            else if (role == PARSE_ROLE_LABEL)
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
                    // A local label's definition is its scope's (CheckLocalLabels); one of an ambiguous name defines none.
                    const uint32_t local = check_input_local_label(reader, &locals, name);
                    const bool ambiguous = local != BUSTER_CHECK_NONE && check_input_local_label_ambiguous(&locals, local);
                    const uint32_t scope = local != BUSTER_CHECK_NONE ? locals.tokens[local] : body;
                    if (local != BUSTER_CHECK_NONE)
                    {
                        locals.scopes[token] = ambiguous ? 0 : scope + 1;
                    }
                    uint64_t slot = ((uint64_t)name * UINT64_C(11400714819323198485) + scope) & (capacity - 1);
                    while (!ambiguous && labels[slot] != 0)
                    {
                        const uint32_t old = labels[slot] - 1;
                        const uint32_t old_name = reader->first_child[old];
                        if (check_input_label_scope(reader, &locals, old) == scope && parser->tokens.names[old_name] == name)
                        {
                            if (!prefix || token < limit)
                            {
                                check_input_report(reader, name_token, CHECK_INPUT_LABEL_REDEFINITION);
                            }
                            break;
                        }
                        slot = (slot + 1) & (capacity - 1);
                    }
                    if (ambiguous && (!prefix || token < limit))
                    {
                        check_input_report(reader, name_token, CHECK_INPUT_LABEL_AMBIGUOUS);
                    }
                    else if (!ambiguous)
                    {
                        labels[slot] = token + 1;
                    }
                }
            }
            else if ((kind == PARSE_KIND_GOTO && check_input_asm_id(reader->output, token) == 0) || (kind == PARSE_KIND_AMPAMP && role == PARSE_ROLE_PREFIX))
            {
                reader->output->declaration_bodies[token] = body;
                // A use of a local label takes its scope here, where its lists are in scope: an ambiguous name's, at its name.
                const uint32_t operand = reader->first_child[token];
                const uint32_t local = operand < count && parser->tokens.kinds[operand] == PARSE_KIND_IDENTIFIER && !check_input_indirect_goto(parser, token) ?
                                       check_input_local_label(reader, &locals, parser->tokens.names[operand]) : BUSTER_CHECK_NONE;
                if (local != BUSTER_CHECK_NONE)
                {
                    const bool ambiguous = check_input_local_label_ambiguous(&locals, local);
                    locals.scopes[token] = ambiguous ? BUSTER_CHECK_NONE : locals.tokens[local] + 1;
                    if (ambiguous && (!prefix || token < limit))
                    {
                        check_input_report(reader, operand, CHECK_INPUT_LABEL_AMBIGUOUS);
                    }
                }
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
        check_input_local_label_pop(reader, &locals, labels, capacity, BUSTER_CHECK_NONE, limit);
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
            const bool jump = kind == PARSE_KIND_GOTO && check_input_asm_id(reader->output, token) == 0;
            const bool address = kind == PARSE_KIND_AMPAMP && parser->tree.roles[token] == PARSE_ROLE_PREFIX;
            if (jump && check_input_indirect_goto(parser, token))
            {
                // goto *expression: its labels are every one whose address its function takes (check_input_indirect_gotos).
                reader->indirect_gotos += 1;
            }
            else if (jump || address)
            {
                // A goto's label, or the label whose address &&name is: the same function's (clang names a missing one at
                // its name, and an address of a label outside a function too), or a local label's in its scope, whose
                // missing definition (or ambiguous name) the first scan reported at the label instead.
                const uint32_t body = reader->output->declaration_bodies[token];
                const uint32_t operand = reader->first_child[token];
                const bool local = locals.scopes != NULL && locals.scopes[token] != 0;
                const bool ambiguous = locals.scopes != NULL && locals.scopes[token] == BUSTER_CHECK_NONE;
                const uint32_t scope = check_input_label_scope(reader, &locals, token);
                uint32_t target = body != BUSTER_CHECK_NONE && !ambiguous && operand < count && parser->tokens.kinds[operand] == PARSE_KIND_IDENTIFIER ?
                                        check_input_find_label(reader, &locals, labels, capacity, scope, parser->tokens.names[operand]) : BUSTER_CHECK_NONE;
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
                if (target == BUSTER_CHECK_NONE && !unfinished && !local)
                {
                    check_input_report(reader, operand < count && parser->tokens.kinds[operand] == PARSE_KIND_IDENTIFIER ? operand : token, CHECK_INPUT_LABEL_UNDECLARED);
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
