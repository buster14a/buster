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

// The opening of the outermost statement expression a jump from `source` to `target` enters, which clang's jump scope
// checker notes ("jump enters a statement expression"), else BUSTER_CHECK_NONE: check_input_region_transfer's walk from the
// target, on to the regions' common ancestor, which reads the region that refuses the jump last. Only a refused jump's
// report asks it (#1942).
static uint32_t check_input_region_entered(const CheckInputReader* reader, uint32_t source, uint32_t target)
{
    const Parser* parser = reader->parser;
    uint32_t entered = BUSTER_CHECK_NONE;
    for (uint32_t ancestor = target, steps = 0; ancestor < parser->tokens.count && steps < parser->tokens.count; ancestor = parser->tree.parents[ancestor], steps += 1)
    {
        const uint32_t selected = reader->output->expression_regions[ancestor];
        const CheckInputRegion* region = selected != 0 ? &reader->output->regions[selected - 1] : NULL;
        entered = region != NULL && !(source > region->opening && source < region->closing) ? region->opening : entered;
    }
    return entered;
}

// A jump's report that enters a statement expression (#1942): clang's "cannot jump from this goto statement to its label",
// or "cannot jump from switch statement to this case label", at `token`, with its note at the expression's `(`.
static void check_input_report_jump(CheckInputReader* reader, uint32_t token, CheckInputMessage message, uint32_t source, uint32_t target)
{
    CheckInput* input = reader->output;
    const uint32_t before = input->report_count;
    check_input_report(reader, token, message);
    const uint32_t entered = check_input_region_entered(reader, source, target);
    if (input->report_count > before && entered != BUSTER_CHECK_NONE)
    {
        input->reports[before].note_kind = CHECK_INPUT_NOTE_JUMP;
        input->reports[before].note_token = entered;
    }
    check_input_jump_completion(reader, before);
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

// Whether the suppress named at `name`, an attribute list's item before `after`, has an argument that is no string literal
// (#2332, check_input_attribute_suppress_literal; an empty list has none): clang's parser gives its "expected string
// literal" and drops the attribute before Sema meets the label's.
static bool check_input_label_suppress_literal(const Parser* parser, uint32_t name, uint32_t after)
{
    const uint8_t* kinds = parser->tokens.kinds;
    const uint32_t close = name + 1 < after && kinds[name + 1] == PARSE_KIND_L_PAREN ? parser->tree.parents[name + 1] : 0;
    const bool listed = close > name + 2 && close < after;
    bool result = false;
    for (uint32_t argument = name + 2; listed && !result && argument <= close;)
    {
        uint32_t stop = argument;
        while (stop < close && kinds[stop] != PARSE_KIND_COMMA)
        {
            const uint32_t group = kinds[stop] == PARSE_KIND_L_PAREN ? parser->tree.parents[stop] : stop;
            stop = group > stop && group < close ? group + 1 : stop + 1;
        }
        result = check_input_attribute_suppress_literal(parser, argument, stop) != BUSTER_CHECK_NONE;
        argument = stop + 1;
    }
    return result;
}

// A label's attributes from `first` to `after` (#147): GNU ones right after a named label's colon, as clang's parser takes
// every GNU attribute there in C, whatever follows it (a null statement, another statement, a declaration, C23's
// attributes, which are the next item's), and a statement's before a label's name (`[[x]] l:`), which clang's labeled
// statement gives its label. Each is read as a declaration's are; clang's error is reported for the first that clang
// refuses on a label (check_input_label_refusal; musttail's by its reading, check_input_attribute_musttail), and clang
// takes the others, which mean nothing for a label here. Their tokens are then the label's, which no declaration or
// statement reads again.
static void check_input_label_attributes(CheckInputReader* reader, uint32_t first, uint32_t after)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    // A name clang refuses on a label is its error whatever the reading would make of the attributes (several of them,
    // minsize and nomerge among them, this reads nowhere else): each list item's name, after its opener or a comma.
    uint32_t refused = BUSTER_CHECK_NONE;
    uint32_t items = BUSTER_CHECK_NONE;
    for (uint32_t token = first; refused == BUSTER_CHECK_NONE && token < after; token += 1)
    {
        const uint8_t role = parser->tree.roles[token];
        const bool item = token > first && (parser->tree.roles[token - 1] == PARSE_ROLE_OPEN_ATTRIBUTE_LIST ||
                                            (parser->tokens.kinds[token - 1] == PARSE_KIND_COMMA && parser->tree.depths[token - 1] == items));
        items = role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST ? parser->tree.depths[token] + 1 : items;
        const bool named = item && parser->tokens.kinds[token] != PARSE_KIND_COMMA && role != PARSE_ROLE_CLOSE_ATTRIBUTE_LIST && parser->tokens.names[token] != 0;
        const String8 name = named ? check_input_attribute_name(reader, parser->tokens.names[token]) : (String8){ 0 };
        // (suppress's arguments are clang's parser's string literals first, whose error drops it before Sema meets the
        // label, #2332: the reading's error.)
        const bool dropped = named && check_input_attribute_suppress_name(name) && check_input_label_suppress_literal(parser, token, after);
        refused = named && !dropped && check_input_label_refusal(name) != NULL ? token : BUSTER_CHECK_NONE;
    }
    if (refused != BUSTER_CHECK_NONE)
    {
        check_input_report(reader, refused, CHECK_INPUT_LABEL_ATTRIBUTE);
    }
    uint32_t cursor = first;
    CheckInputAttributes* attributes = 0;
    bool valid = refused == BUSTER_CHECK_NONE && after > first && after < count && check_input_read_attributes(reader, &cursor, after, &attributes) && cursor == after;
    // A label's subject as clang's attributes name it (#147): the warning for those that apply elsewhere.
    // The label's name: before its colon, or after the attributes before it.
    const bool colon = first >= 2 && parser->tokens.kinds[first - 1] == PARSE_KIND_COLON;
    reader->attribute_declaration = colon ? first - 1 : after + 1;
    valid = valid && check_input_attribute_subjects(reader, attributes, BUSTER_CHECK_SUBJECT_LABEL, 0, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
    reader->attribute_declaration = 0;
    for (uint32_t i = 0; valid && attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        valid = attribute->keyword || check_input_label_refusal(check_input_attribute_name(reader, attribute->name)) == NULL;
        if (!valid)
        {
            check_input_report(reader, attribute->first, CHECK_INPUT_LABEL_ATTRIBUTE);
        }
    }
    // An unused (or maybe_unused) attribute marks the label used for -Wunused-label (#2264): recorded at its colon.
    bool unused = false;
    for (uint32_t i = 0; valid && attributes != 0 && i < attributes->count; i += 1)
    {
        unused |= attributes->records[i].kind == CHECK_ATTRIBUTE_UNUSED;
    }
    const uint32_t label = colon ? first - 1 : after + 1;
    if (unused && label < count && parser->tree.roles[label] == PARSE_ROLE_LABEL)
    {
        check_input_set_attributes_at(reader->output, label, attributes);
    }
    for (uint32_t token = first; token < after; token += 1)
    {
        reader->ignored[token] = true;
    }
}

// The GNU spellings clang 23's parser takes as statement attributes (c_parser.c's parse_scalar_statement_attribute): a
// block item whose GNU attributes are all of them, before an expression, is a statement, as the parser reads it.
static bool check_input_statement_attribute_name(String8 name)
{
    static const char* const names[] = {
        "always_inline", "atomic", "assume", "code_align", "fallthrough", "musttail", "noinline", "nomerge", "no_outline", "opencl_unroll_hint", "suppress",
    };
    bool result = false;
    for (uint32_t i = 0; !result && i < sizeof(names) / sizeof(names[0]); i += 1)
    {
        result = check_input_attribute_equal(name, names[i]);
    }
    return result;
}

// Whether the attributes from `first` to `after` are GNU ones of clang's statement attributes alone, by their names, and
// at least one (clang's HaveAttrs: empty lists, `__attribute__(())`, leave a declaration's): each item of a list starts
// right after its opener or after a comma among its items.
static bool check_input_statement_attribute_names(CheckInputReader* reader, uint32_t first, uint32_t after)
{
    const Parser* parser = reader->parser;
    bool result = true;
    bool named = false;
    uint32_t items = BUSTER_CHECK_NONE;
    for (uint32_t token = first; result && token < after; token += 1)
    {
        const uint8_t role = parser->tree.roles[token];
        const uint8_t kind = parser->tokens.kinds[token];
        const bool item = token > first && (parser->tree.roles[token - 1] == PARSE_ROLE_OPEN_ATTRIBUTE_LIST ||
                                            (parser->tokens.kinds[token - 1] == PARSE_KIND_COMMA && parser->tree.depths[token - 1] == items));
        items = role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST ? parser->tree.depths[token] + 1 : items;
        result = kind != PARSE_KIND_L_SQUARE && kind != PARSE_KIND_ALIGNAS && kind != PARSE_KIND_ASM;
        if (result && item && kind != PARSE_KIND_COMMA && role != PARSE_ROLE_CLOSE_ATTRIBUTE_LIST)
        {
            result = parser->tokens.names[token] != 0 && check_input_statement_attribute_name(check_input_attribute_name(reader, parser->tokens.names[token]));
            named = true;
        }
    }
    return result && named;
}

// The arm of the selection (a _Generic or a __builtin_choose_expr) at `keyword` whose `)` is `close` that its evaluation
// takes (#147), from *arm_first to *arm_end (past its last): the evaluator's record of it while an assumption's argument
// is evaluated (CheckInputReader.selection_arms), or, once the selections are read (`selected`), the arm that holds the
// root the reader selected (CheckInputReader.selected_tokens): a _Generic's association's expression after its colon, a
// __builtin_choose_expr's second or third operand. False where neither says.
static bool check_input_selected_arm(const CheckInputReader* reader, uint32_t keyword, uint32_t close, bool selected, uint32_t* arm_first, uint32_t* arm_end)
{
    const Parser* parser = reader->parser;
    const uint32_t* arms = reader->selection_arms;
    const uint32_t at = close - reader->selection_arm_base;
    const bool recorded = arms != NULL && close >= reader->selection_arm_base && close < reader->selection_arm_end && arms[2 * at + 1] != 0;
    const uint32_t chosen = !recorded && selected && reader->selected_tokens != NULL ? reader->selected_tokens[close] : BUSTER_CHECK_NONE;
    const uint32_t root = chosen != BUSTER_CHECK_NONE ? chosen & ~CHECK_INPUT_SOURCE_SELECTION : BUSTER_CHECK_NONE;
    bool result = recorded;
    *arm_first = recorded ? arms[2 * at] : 0;
    *arm_end = recorded ? arms[2 * at + 1] : 0;
    const bool generic = parser->tokens.names[keyword] == C_NAME_GENERIC;
    const uint32_t separator = root != BUSTER_CHECK_NONE ? check_constant_punctuation(parser, keyword + 2, close, PARSE_KIND_COMMA) : close;
    uint32_t item = separator + 1;
    while (!result && item < close)
    {
        const uint32_t colon = generic ? check_constant_punctuation(parser, item, close, PARSE_KIND_COLON) : item - 1;
        const uint32_t after = colon < close ? check_constant_punctuation(parser, colon + 1, close, PARSE_KIND_COMMA) : close;
        result = colon + 1 <= root && root < after;
        *arm_first = result ? colon + 1 : 0;
        *arm_end = result ? after : 0;
        item = after + 1;
    }
    return result;
}

// The tokens from `first` to `end` that an evaluation of them doesn't reach through their selections (#147): each
// _Generic's and __builtin_choose_expr's but its selected arm's (check_input_selected_arm), its controlling expression's
// among them, as clang's EvaluatedExprVisitor and HasSideEffects visit a selection's result alone. In `skipped`, a byte
// per token from `first`; a selection whose arm isn't known skips nothing.
static void check_input_unselected(const CheckInputReader* reader, uint32_t first, uint32_t end, bool selected, uint8_t* skipped)
{
    const Parser* parser = reader->parser;
    memset(skipped, 0, end - first);
    for (uint32_t token = first; token < end; token += 1)
    {
        const uint32_t name = parser->tokens.names[token];
        const bool selection = skipped[token - first] == 0 && (name == C_NAME_GENERIC || name == C_NAME_BUILTIN_CHOOSE_EXPR) && token + 1 < end &&
                               parser->tree.roles[token + 1] == PARSE_ROLE_OPEN_KEYWORD && parser->tree.parents[token + 1] < end;
        const uint32_t close = selection ? parser->tree.parents[token + 1] : end;
        uint32_t arm_first = 0;
        uint32_t arm_end = 0;
        if (selection && check_input_selected_arm(reader, token, close, selected, &arm_first, &arm_end))
        {
            for (uint32_t i = token; i <= close; i += 1)
            {
                skipped[i - first] |= i < arm_first || i >= arm_end;
            }
        }
    }
}

// Whether a statement's tokens, from `statement` to `end`, hold a _Generic or a __builtin_choose_expr.
static bool check_input_statement_selects(const CheckInputReader* reader, uint32_t statement, uint32_t end)
{
    const Parser* parser = reader->parser;
    bool result = false;
    for (uint32_t token = statement; !result && token + 1 < end; token += 1)
    {
        const uint32_t name = parser->tokens.names[token];
        result = (name == C_NAME_GENERIC || name == C_NAME_BUILTIN_CHOOSE_EXPR) && parser->tree.roles[token + 1] == PARSE_ROLE_OPEN_KEYWORD;
    }
    return result;
}

// The name a call's callee refers to (#147, as clang's getReferencedDeclOfCallee finds it): the identifier that the
// tokens before the call's `(` at `call`, after `first`, are, through the parentheses around it and the prefix *, & and +
// before it (`f()`, `(f)()`, `(*f)()`, `(&f)()`); BUSTER_CHECK_NONE where the callee is no such name.
static uint32_t check_input_callee_name(const CheckInputReader* reader, uint32_t first, uint32_t call)
{
    const Parser* parser = reader->parser;
    const uint32_t last = call > first ? call - 1 : BUSTER_CHECK_NONE;
    uint32_t begin = last != BUSTER_CHECK_NONE && parser->tokens.kinds[last] == PARSE_KIND_IDENTIFIER ? last : BUSTER_CHECK_NONE;
    uint32_t end = call;
    if (last != BUSTER_CHECK_NONE && parser->tokens.kinds[last] == PARSE_KIND_R_PAREN && parser->tree.roles[last] == PARSE_ROLE_CLOSE_GROUP)
    {
        // The group's `(`, back from its `)` (a `(`'s parent is its `)`).
        uint32_t open = last;
        while (open > first && !(parser->tokens.kinds[open - 1] == PARSE_KIND_L_PAREN && parser->tree.parents[open - 1] == last))
        {
            open -= 1;
        }
        begin = open > first ? open - 1 : BUSTER_CHECK_NONE;
    }
    bool unwrapping = begin != BUSTER_CHECK_NONE;
    while (unwrapping && begin + 1 < end)
    {
        const uint8_t kind = parser->tokens.kinds[begin];
        const bool group = kind == PARSE_KIND_L_PAREN && parser->tree.roles[begin] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[begin] == end - 1;
        const bool prefix = (kind == PARSE_KIND_STAR || kind == PARSE_KIND_AMP || kind == PARSE_KIND_PLUS) && parser->tree.roles[begin] == PARSE_ROLE_PREFIX;
        begin += group || prefix ? 1 : 0;
        end -= group ? 1 : 0;
        unwrapping = group || prefix;
    }
    const uint32_t result = begin != BUSTER_CHECK_NONE && begin + 1 == end && parser->tokens.kinds[begin] == PARSE_KIND_IDENTIFIER ? begin : BUSTER_CHECK_NONE;
    return result;
}

// Whether an assumption (from `first` to `end`, which the evaluator read) has clang's (potential) side effects (#147,
// HasSideEffects): a volatile object's read (`flags`, the evaluator's), an increment, a decrement or an assignment, or a
// call of other than a function declared const or pure (its declaration's attributes, through its callee's parentheses:
// check_input_callee_name); an operand of sizeof, _Alignof or typeof and a selection's unselected arms aren't evaluated
// (check_input_unselected, by the arms the argument's evaluation recorded).
static bool check_input_assume_effects(CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t flags)
{
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    uint8_t* skipped = end > first ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, end - first) : NULL;
    reader->output->resource |= end > first && skipped == NULL;
    if (skipped != NULL)
    {
        check_input_unselected(reader, first, end, false, skipped);
    }
    bool result = (flags & BUSTER_CHECK_VOLATILE_PART) != 0;
    uint32_t token = first;
    while (!result && skipped != NULL && token < end)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const uint8_t role = parser->tree.roles[token];
        const bool unevaluated = (kind == PARSE_KIND_SIZEOF || kind == PARSE_KIND_TYPEOF) && token + 1 < end && parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN &&
                                 parser->tree.parents[token + 1] > token + 1 && parser->tree.parents[token + 1] < end;
        const bool reached = skipped[token - first] == 0;
        const uint32_t callee = reached && role == PARSE_ROLE_OPEN_CALL ? check_input_callee_name(reader, first, token) : BUSTER_CHECK_NONE;
        const uint32_t declaration = callee != BUSTER_CHECK_NONE && reader->output->declarations != NULL ? reader->output->declarations[callee] : BUSTER_CHECK_NONE;
        const CheckInputAttributes* attributes = declaration < parser->tokens.count ? check_input_attributes_at(reader->output, declaration) : NULL;
        bool pure = false;
        for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
        {
            pure |= attributes->records[i].kind == CHECK_ATTRIBUTE_CONST || attributes->records[i].kind == CHECK_ATTRIBUTE_PURE;
        }
        // (A name of a volatile object, or one of a pointer to one that it dereferences, whose read the evaluator's
        // non-constant value doesn't mark.)
        const TypeId named = kind == PARSE_KIND_IDENTIFIER ? reader->declared_types[token] : 0;
        const TypeId bare = named != 0 ? type_strip_aligned(reader->types, named) : 0;
        const bool pointer = bare != 0 && type_kind(reader->types, bare) == BUSTER_TYPE_KIND_POINTER;
        const bool dereferenced = (token > first && parser->tokens.kinds[token - 1] == PARSE_KIND_STAR && parser->tree.roles[token - 1] == PARSE_ROLE_PREFIX) ||
                                  (token + 1 < end && (parser->tokens.kinds[token + 1] == PARSE_KIND_L_SQUARE || parser->tokens.kinds[token + 1] == PARSE_KIND_ARROW));
        const bool volatile_name = (named != 0 && (type_qualifiers(named) & BUSTER_TYPE_VOLATILE) != 0) ||
                                   (pointer && dereferenced && (type_qualifiers(reader->types->types.base[type_index(bare)]) & BUSTER_TYPE_VOLATILE) != 0);
        result = reached && (kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS || role == PARSE_ROLE_ASSIGN || (role == PARSE_ROLE_OPEN_CALL && !pure) || volatile_name);
        token = unevaluated ? parser->tree.parents[token + 1] + 1 : token + 1;
    }
    arena_pop_to(reader->scratch, mark);
    return result;
}

// A statement's next call from `token` up to its `end` (#147, as clang's CallExprFinder, an EvaluatedExprVisitor, finds
// them): a call's `(` (not a declarator's parameters') or an asm statement's, outside sizeof's, _Alignof's and typeof's
// operands, GNU attributes' arguments and the tokens `unselected` marks (if any), a byte per token from `base`, a
// selection's unselected arms (check_input_unselected); `end` for none.
static uint32_t check_input_statement_call(const CheckInputReader* reader, uint32_t token, uint32_t end, const uint8_t* unselected, uint32_t base)
{
    const Parser* parser = reader->parser;
    uint32_t result = end;
    uint32_t at = token;
    while (result == end && at < end)
    {
        const uint8_t kind = parser->tokens.kinds[at];
        const uint8_t role = parser->tree.roles[at];
        const bool grouped = at + 1 < end && parser->tokens.kinds[at + 1] == PARSE_KIND_L_PAREN && parser->tree.parents[at + 1] > at + 1 &&
                             parser->tree.parents[at + 1] < end;
        const bool skipped = kind == PARSE_KIND_SIZEOF || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATTRIBUTE;
        const bool declarator = at > 0 && reader->declaration_kinds[at - 1] != 0;
        const bool reached = unselected == NULL || unselected[at - base] == 0;
        result = reached && ((role == PARSE_ROLE_OPEN_CALL && !declarator) || role == PARSE_ROLE_OPEN_ASM) ? at : end;
        at = reached && skipped && grouped ? parser->tree.parents[at + 1] + 1 : reached && skipped && kind != PARSE_KIND_ATTRIBUTE ? reader->ends[at] + 1 : at + 1;
    }
    return result;
}

// The unselected arms' marks (check_input_unselected) of a statement's tokens, from `statement` to `end`, once the
// selections are read, in the scratch arena, which the caller pops; null where it can't hold them (CheckInput.resource).
static uint8_t* check_input_statement_unselected(CheckInputReader* reader, uint32_t statement, uint32_t end)
{
    uint8_t* result = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, (uint64_t)(end - statement) + 1);
    reader->output->resource |= result == NULL;
    if (result != NULL)
    {
        check_input_unselected(reader, statement, end, true, result);
    }
    return result;
}

// A statement attribute's check that waits for the selections (#147, CheckInputReader.deferred_statements): a copy of
// the attribute, the statement's first token, and the check (CheckInputDeferredCheck).
typedef enum CheckInputDeferredCheck
{
    CHECK_INPUT_DEFERRED_NOMERGE,
    CHECK_INPUT_DEFERRED_NOINLINE,
    CHECK_INPUT_DEFERRED_ALWAYS_INLINE,
} CheckInputDeferredCheck;
typedef struct CheckInputDeferredStatement CheckInputDeferredStatement;
struct CheckInputDeferredStatement
{
    CheckInputAttribute attribute;
    uint32_t statement;
    uint8_t check;
};

// Records a statement attribute's check for check_input_statement_deferred, in the reader's arena, which grows the list
// by doubling.
static void check_input_statement_defer(CheckInputReader* reader, const CheckInputAttribute* attribute, uint32_t statement, CheckInputDeferredCheck check)
{
    if (reader->deferred_statement_count == reader->deferred_statement_capacity)
    {
        const uint32_t capacity = reader->deferred_statement_capacity == 0 ? 8 :
                                  reader->deferred_statement_capacity <= UINT32_MAX / 2 ? reader->deferred_statement_capacity * 2 : 0;
        CheckInputDeferredStatement* grown = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputDeferredStatement, capacity) : NULL;
        reader->output->resource |= grown == NULL;
        if (grown != NULL)
        {
            if (reader->deferred_statement_count != 0)
            {
                memcpy(grown, reader->deferred_statements, (uint64_t)reader->deferred_statement_count * sizeof(*grown));
            }
            reader->deferred_statements = grown;
            reader->deferred_statement_capacity = capacity;
        }
    }
    if (reader->deferred_statement_count < reader->deferred_statement_capacity)
    {
        reader->deferred_statements[reader->deferred_statement_count++] = (CheckInputDeferredStatement)
        {
            .attribute = *attribute,
            .statement = statement,
            .check = (uint8_t)check,
        };
    }
}

// Whether the statement at `statement` evaluates a call or is or holds an asm statement (check_input_statement_call), its
// selections' selected arms' alone once they are read (`selected`).
static bool check_input_statement_calls(CheckInputReader* reader, uint32_t statement, bool selected)
{
    const uint64_t mark = reader->scratch->length;
    const uint32_t end = check_input_loop_statement_end(reader, statement);
    const uint8_t* unselected = selected ? check_input_statement_unselected(reader, statement, end) : NULL;
    // Without the marks (a resource failure), as if it had one: no warning.
    const bool result = (selected && unselected == NULL) || check_input_statement_call(reader, statement, end, unselected, statement) < end;
    arena_pop_to(reader->scratch, mark);
    return result;
}

// Whether a statement's noinline or always_inline is clang's statement attribute (#147, isStmtNoInline and
// isClangAlwaysInline): clang::'s (msvc::noinline is a generic attribute's: check_input_statement_attribute_kinds); GNU's
// spellings (gnu:: among them) are a function's alone.
static bool check_input_statement_inline(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const bool scoped = attribute->standard && attribute->first != attribute->name_token;
    const String8 scope = scoped ? check_input_attribute_name(reader, reader->parser->tokens.names[attribute->first]) : (String8){ 0 };
    return check_input_attribute_equal(scope, "clang") || check_input_attribute_equal(scope, "_Clang");
}

// The function attribute a statement's noinline or always_inline overrides on a callee (#147, CheckStmtInlineAttr):
// `other` (always_inline for noinline, noinline for always_inline) or flatten on any of the function's declarations up
// to `declaration`, its visible one: 1 for `other`, 2 for flatten, 0 for neither. A scan of the unit's declarations
// before it, made for such a statement's calls alone.
static uint32_t check_input_inline_conflict(CheckInputReader* reader, uint32_t declaration, CheckInputAttributeKind other)
{
    const uint32_t key = reader->objects[declaration];
    uint32_t result = 0;
    for (uint32_t t = 0; key != 0 && result != 1 && t <= declaration; t += 1)
    {
        const CheckInputAttributes* attributes = reader->declaration_kinds[t] == C_BINDING_FUNCTION && reader->objects[t] == key ?
                                                 check_input_attributes_at(reader->output, t) : NULL;
        for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + i;
            result = attribute->ignored ? result : attribute->kind == other ? 1 : attribute->kind == CHECK_ATTRIBUTE_FLATTEN && result == 0 ? 2 : result;
        }
    }
    return result;
}

// clang's statement noinline (`noinline`) or always_inline (`attribute` at `statement`, #147, CheckStmtInlineAttr): with no call in the
// statement nomerge's warning; else, at each call of a function its callee's name finds where the statement starts
// (check_input_callee_name: through parentheses, `*` and `&`, as clang's getCalleeDecl), which
// check_input_inline_conflict gives an overridden attribute, "statement attribute 'X' has higher precedence than function
// attribute 'Y'" (-Wignored-attributes) at the statement, with a note at that declaration's first token. The calls are
// those the statement evaluates (check_input_statement_call), a selection's selected arm's alone.
static void check_input_statement_inline_calls(CheckInputReader* reader, const CheckInputAttribute* attribute, uint32_t statement, bool noinline, bool selected)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint64_t mark = reader->scratch->length;
    const uint32_t end = check_input_loop_statement_end(reader, statement);
    const uint8_t* unselected = selected ? check_input_statement_unselected(reader, statement, end) : NULL;
    const CheckInputAttributeKind other = noinline ? CHECK_ATTRIBUTE_ALWAYS_INLINE : CHECK_ATTRIBUTE_NOINLINE;
    const bool scanned = !selected || unselected != NULL;
    uint32_t call = scanned ? check_input_statement_call(reader, statement, end, unselected, statement) : end;
    if (scanned && call == end)
    {
        check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_NO_CALLS, statement,
                                             BUSTER_CHECK_WARNING_NO_SOURCE);
    }
    const uint32_t scope_name = attribute->standard && attribute->first != attribute->name_token ? parser->tokens.names[attribute->first] : 0;
    for (; call < end; call = check_input_statement_call(reader, call + 1, end, unselected, statement))
    {
        // The function the callee's name finds where the statement is: its binding as it is read, or, once the selections
        // are read, the declaration the bindings resolved its use to.
        const uint32_t callee = parser->tree.roles[call] == PARSE_ROLE_OPEN_CALL ? check_input_callee_name(reader, statement, call) : BUSTER_CHECK_NONE;
        const uint32_t handle = callee != BUSTER_CHECK_NONE && !selected ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[callee]) : 0;
        const uint32_t resolved = callee != BUSTER_CHECK_NONE && selected ? reader->output->declarations[callee] : BUSTER_CHECK_NONE;
        const uint32_t declaration = handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_FUNCTION ? reader->bindings.declaration[handle - 1] :
                                     resolved < count && reader->declaration_kinds[resolved] == C_BINDING_FUNCTION ? resolved : BUSTER_CHECK_NONE;
        const uint32_t conflict = declaration != BUSTER_CHECK_NONE ? check_input_inline_conflict(reader, declaration, other) : 0;
        if (conflict != 0)
        {
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                .kind = conflict == 2 ? CHECK_WARNING_EVENT_PRECEDENCE_FLATTEN : noinline ? CHECK_WARNING_EVENT_PRECEDENCE_ALWAYS_INLINE :
                                                                                       CHECK_WARNING_EVENT_PRECEDENCE_NOINLINE,
                .token = statement,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = statement,
                .payload = { .name = attribute->name, .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                             .data = scope_name | (attribute->standard ? BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE : 0) },
            };
            const CheckWarningNote note =
            {
                .kind = CHECK_WARNING_NOTE_CONFLICTING_ATTRIBUTE,
                .token = check_input_declaration_begin(parser, declaration),
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .name = 0, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            check_input_warning_event(reader, &event, &note, 1);
        }
    }
    arena_pop_to(reader->scratch, mark);
}

// The statement attributes' checks that waited for the selections (#147, check_input_statement_defer): each as at its
// statement, with its selections' selected arms' calls alone (clang's CallExprFinder visits a selection's result), and
// its callees' declarations as the bindings resolved them. In the order they were recorded, the statements' order.
static void check_input_statement_deferred(CheckInputReader* reader)
{
    for (uint32_t i = 0; i < reader->deferred_statement_count; i += 1)
    {
        const CheckInputDeferredStatement* item = reader->deferred_statements + i;
        if (item->check == CHECK_INPUT_DEFERRED_NOMERGE && !check_input_statement_calls(reader, item->statement, true))
        {
            check_input_attribute_named_event_at(reader, &item->attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_NO_CALLS, item->statement,
                                                 BUSTER_CHECK_WARNING_NO_SOURCE);
        }
        else if (item->check != CHECK_INPUT_DEFERRED_NOMERGE)
        {
            check_input_statement_inline_calls(reader, &item->attribute, item->statement, item->check == CHECK_INPUT_DEFERRED_NOINLINE, true);
        }
    }
    reader->deferred_statement_count = 0;
}

// A statement's attributes, as clang's Sema takes them, after their reading (check_input_read_attributes) took each: a null
// statement's fallthrough (`empty`), and a return's musttail (`returns`) are the statement's; GNU's noinline and
// always_inline clang ignores on a statement with -Wignored-attributes' warning at it (its first token, `statement`),
// clang's are checked (check_input_statement_inline_calls), and nomerge and suppress are ignored on any, a nomerge with
// the warning where it evaluates no call (check_input_statement_calls); musttail elsewhere, fallthrough on a statement that isn't empty, and every other
// attribute are clang's errors, the first one's reported. The musttail's first token, or BUSTER_CHECK_NONE, in
// `*musttail`. Whether they are valid.
static bool check_input_statement_attribute_kinds(CheckInputReader* reader, CheckInputAttributes* attributes, bool empty, bool returns, uint32_t statement,
                                                   uint32_t* musttail)
{
    const uint8_t next = statement < reader->parser->tokens.count ? reader->parser->tokens.kinds[statement] : PARSE_KIND_SEMI;
    bool valid = true;
    *musttail = BUSTER_CHECK_NONE;
    uint32_t aligned = BUSTER_CHECK_NONE;
    // The first clang::likely or clang::unlikely, which excludes the other (#2291).
    uint32_t likelihood = BUSTER_CHECK_NONE;
    bool likely = false;
    for (uint32_t i = 0; valid && attributes != 0 && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = attributes->records + i;
        const CheckInputAttributeKind kind = attribute->kind;
        const String8 generic = kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword ? check_input_attribute_name(reader, attribute->name) : (String8){ 0 };
        const bool code_align = check_input_attribute_equal(generic, "code_align");
        // msvc::noinline, clang's statement noinline's other spelling, and msvc::forceinline and forceinline_calls.
        const bool msvc = attribute->standard && attribute->first != attribute->name_token &&
                          check_input_attribute_equal(check_input_attribute_name(reader, reader->parser->tokens.names[attribute->first]), "msvc");
        const bool msvc_noinline = msvc && check_input_attribute_equal(generic, "noinline");
        const bool msvc_force = msvc && (check_input_attribute_equal(generic, "forceinline") || check_input_attribute_equal(generic, "forceinline_calls"));
        const bool unroll = check_input_attribute_equal(generic, "opencl_unroll_hint");
        // A nomerge of arguments, which the reader's count check made generic: clang's count error, "takes no arguments".
        const bool nomerge = !msvc && check_input_attribute_equal(generic, "nomerge");
        const bool loop = next == PARSE_KIND_FOR || next == PARSE_KIND_WHILE || next == PARSE_KIND_DO;
        const uint32_t reported = reader->output->report_count;
        if (code_align || unroll)
        {
            // Loops' hints (#147), which change nothing at -O0: clang's subject first, then the count, the constant and its
            // value, and a second code_align is a conflict.
            const uint32_t location = check_input_expression_location(reader->parser, attribute->argument_first, attribute->argument_end);
            CheckInputAttributes holder = { 0 };
            if (!loop)
            {
                check_input_attribute_argument_error(reader, &holder, attribute, attribute->first, CHECK_ARGUMENT_LOOP_ONLY, 0);
            }
            else if ((code_align && attribute->argument_count != 1) || (unroll && attribute->argument_count > 1))
            {
                check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT,
                                            (code_align ? BUSTER_CHECK_COUNT_EXACT : BUSTER_CHECK_COUNT_MOST) | 1u << 8);
            }
            else if (attribute->argument_count == 1 && !attribute->constant_argument)
            {
                check_input_attribute_argument_error(reader, &holder, attribute, location, CHECK_ARGUMENT_NOT_ICE, 0);
            }
            else if (code_align && (attribute->value < 1 || attribute->value > 4096 || (attribute->value & (attribute->value - 1)) != 0))
            {
                const uint32_t before = reader->output->report_count;
                check_input_attribute_argument_error(reader, &holder, attribute, attribute->first, CHECK_ARGUMENT_CODE_ALIGN, 0);
                if (reader->output->report_count > before)
                {
                    reader->output->reports[before].value = (uint64_t)attribute->value;
                }
            }
            else if (unroll && attribute->argument_count == 1 && attribute->value <= 0)
            {
                check_input_attribute_argument_error(reader, &holder, attribute, attribute->first, CHECK_ARGUMENT_POSITIVE, 0);
            }
            else if (code_align && aligned != BUSTER_CHECK_NONE)
            {
                const uint32_t before = reader->output->report_count;
                check_input_attribute_argument_error(reader, &holder, attribute, attribute->first, CHECK_ARGUMENT_LOOP_CONFLICT, 0);
                if (reader->output->report_count > before)
                {
                    reader->output->reports[before].note_token = aligned;
                    reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_PREVIOUS_ATTRIBUTE;
                }
            }
            valid = holder.error == CHECK_ATTRIBUTE_ERROR_NONE && reader->output->report_count == reported;
            aligned = code_align && aligned == BUSTER_CHECK_NONE ? attribute->first : aligned;
            attribute->ignored = true;
        }
        else if (check_input_attribute_equal(generic, "assume"))
        {
            // GNU's assumption (#147), which changes nothing at -O0: clang's subject (an empty statement), its count, its
            // argument a conditional expression (an assignment or a comma at its top needs parentheses), read as clang's Sema
            // reads it (check_input_evaluate), of a scalar, and its (potential) side effects warned of (-Wassume), which
            // drop it.
            CheckInputAttributes holder = { 0 };
            const Parser* parser = reader->parser;
            uint32_t bare = BUSTER_CHECK_NONE;
            for (uint32_t token = attribute->argument_first; empty && attribute->argument_count != 0 && bare == BUSTER_CHECK_NONE && token < attribute->argument_end;)
            {
                const uint8_t role = parser->tree.roles[token];
                bare = role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_COMMA || parser->tokens.kinds[token] == PARSE_KIND_COMMA ? token : BUSTER_CHECK_NONE;
                token = check_input_attribute_after_group(parser, token, attribute->argument_end);
            }
            if (!empty)
            {
                check_input_attribute_argument_error(reader, &holder, attribute, attribute->first, CHECK_ARGUMENT_EMPTY_ONLY, 0);
            }
            else if (attribute->argument_count == 0)
            {
                check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, BUSTER_CHECK_COUNT_EXACT | 1u << 8);
            }
            else if (bare != BUSTER_CHECK_NONE)
            {
                check_input_attribute_argument_error(reader, &holder, attribute, bare, CHECK_ARGUMENT_ASSUME_PARENTHESES, 0);
            }
            else
            {
                // (A name that names nothing is clang's parser's error first.)
                uint32_t undeclared = BUSTER_CHECK_NONE;
                for (uint32_t token = attribute->argument_first; undeclared == BUSTER_CHECK_NONE && token < attribute->argument_end; token += 1)
                {
                    const uint8_t previous = parser->tokens.kinds[token - 1];
                    const uint32_t name = parser->tokens.names[token];
                    const bool library = reader->library_declarations != NULL && reader->library_declarations[name] != 0;
                    const String8 spelled = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? check_input_attribute_name(reader, name) : (String8){ 0 };
                    const bool builtin = spelled.length > 10 && memcmp(spelled.pointer, "__builtin_", 10) == 0;
                    undeclared = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && previous != PARSE_KIND_PERIOD && previous != PARSE_KIND_ARROW &&
                                 previous != PARSE_KIND_STRUCT && previous != PARSE_KIND_ENUM && !library && !builtin &&
                                 c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name) == 0 ? token : BUSTER_CHECK_NONE;
                }
                CheckValue value = { 0 };
                // The arms its selections' evaluation takes, for its side effects' scan (check_input_assume_effects).
                const uint32_t span = attribute->argument_end > attribute->argument_first ? attribute->argument_end - attribute->argument_first : 0;
                reader->selection_arms = undeclared == BUSTER_CHECK_NONE && span != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, 2 * (uint64_t)span) : NULL;
                reader->output->resource |= undeclared == BUSTER_CHECK_NONE && span != 0 && reader->selection_arms == NULL;
                reader->selection_arm_base = attribute->argument_first;
                reader->selection_arm_end = reader->selection_arms != NULL ? attribute->argument_end : attribute->argument_first;
                if (reader->selection_arms != NULL)
                {
                    memset(reader->selection_arms, 0, 2 * (uint64_t)span * sizeof(uint32_t));
                }
                const bool evaluated = undeclared == BUSTER_CHECK_NONE && check_input_evaluate(reader, attribute->argument_first, attribute->argument_end, &value);
                const uint8_t value_kind = evaluated ? type_kind(reader->types, type_strip_aligned(reader->types, value.type)) : 0;
                if (undeclared != BUSTER_CHECK_NONE)
                {
                    check_input_report(reader, undeclared, CHECK_INPUT_UNDECLARED);
                }
                else if (!evaluated && reader->output->report_count == reported)
                {
                    check_input_report(reader, attribute->argument_first, CHECK_INPUT_UNSUPPORTED);
                }
                else if (value_kind == BUSTER_TYPE_KIND_STRUCT || value_kind == BUSTER_TYPE_KIND_UNION)
                {
                    const uint32_t before = reader->output->report_count;
                    check_input_attribute_argument_error(reader, &holder, attribute, attribute->argument_first, CHECK_ARGUMENT_NOT_BOOL, 0);
                    if (reader->output->report_count > before)
                    {
                        reader->output->reports[before].type = value.type;
                    }
                }
                else if (evaluated && check_input_assume_effects(reader, attribute->argument_first, attribute->argument_end, value.flags))
                {
                    check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_ASSUME, CHECK_WARNING_EVENT_ASSUME_SIDE_EFFECTS, attribute->argument_first,
                                                         BUSTER_CHECK_WARNING_NO_SOURCE);
                }
                reader->selection_arms = NULL;
            }
            valid = holder.error == CHECK_ATTRIBUTE_ERROR_NONE && reader->output->report_count == reported;
            attribute->ignored = true;
        }
        else if (check_input_attribute_equal(generic, "atomic"))
        {
            // A compound statement's atomic options (#147), which change nothing on x86-64: clang's subject first, then each
            // option (remote_memory, fine_grained_memory and ignore_denormal_mode, each with no_ or not).
            CheckInputAttributes holder = { 0 };
            static const char* const options[] = { "remote_memory", "fine_grained_memory", "ignore_denormal_mode" };
            if (next != PARSE_KIND_L_BRACE)
            {
                check_input_attribute_argument_error(reader, &holder, attribute, attribute->first, CHECK_ARGUMENT_COMPOUND_ONLY, 0);
            }
            else if (attribute->argument_count == 0)
            {
                check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, BUSTER_CHECK_COUNT_LEAST | 1u << 8);
            }
            for (uint32_t token = attribute->argument_first; holder.error == CHECK_ATTRIBUTE_ERROR_NONE && next == PARSE_KIND_L_BRACE && token < attribute->argument_end;
                 token += 1)
            {
                const uint8_t token_kind = reader->parser->tokens.kinds[token];
                String8 option = token_kind == PARSE_KIND_IDENTIFIER ? check_input_attribute_name(reader, reader->parser->tokens.names[token]) : (String8){ 0 };
                const bool negated = option.length > 3 && memcmp(option.pointer, "no_", 3) == 0;
                option = negated ? (String8){ option.pointer + 3, option.length - 3 } : option;
                bool known = token_kind == PARSE_KIND_COMMA;
                for (uint32_t o = 0; !known && o < BUSTER_ARRAY_LENGTH(options); o += 1)
                {
                    known = check_input_attribute_equal(option, options[o]);
                }
                if (!known)
                {
                    const uint32_t before = reader->output->report_count;
                    check_input_attribute_argument_error(reader, &holder, attribute, token, CHECK_ARGUMENT_ATOMIC_OPTION, 0);
                    if (reader->output->report_count > before)
                    {
                        reader->output->reports[before].value = token;
                    }
                }
            }
            valid = holder.error == CHECK_ATTRIBUTE_ERROR_NONE && reader->output->report_count == reported;
            attribute->ignored = true;
        }
        else if (kind == CHECK_ATTRIBUTE_MUSTTAIL)
        {
            *musttail = *musttail == BUSTER_CHECK_NONE ? attribute->first : *musttail;
            valid = returns;
            if (!valid)
            {
                check_input_report(reader, attribute->first, CHECK_INPUT_MUSTTAIL_STATEMENT);
            }
        }
        else if (kind == CHECK_ATTRIBUTE_FALLTHROUGH)
        {
            valid = empty;
            if (!valid)
            {
                check_input_report(reader, attribute->first, CHECK_INPUT_FALLTHROUGH_STATEMENT);
            }
        }
        else if ((kind == CHECK_ATTRIBUTE_NOINLINE || kind == CHECK_ATTRIBUTE_ALWAYS_INLINE) && !attribute->keyword && !check_input_statement_inline(reader, attribute))
        {
            // GNU's spellings, a function's alone, which clang ignores on a statement with its warning there.
            check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                                                 kind == CHECK_ATTRIBUTE_NOINLINE ? CHECK_WARNING_EVENT_NOINLINE_STATEMENT : CHECK_WARNING_EVENT_ALWAYS_INLINE_STATEMENT,
                                                 statement, BUSTER_CHECK_WARNING_NO_SOURCE);
            attribute->ignored = true;
        }
        else if ((kind == CHECK_ATTRIBUTE_NOINLINE || kind == CHECK_ATTRIBUTE_ALWAYS_INLINE) && !attribute->keyword && statement < reader->parser->tokens.count)
        {
            // clang's statement spellings, which change nothing at -O0 (check_input_statement_inline_calls), after the
            // selections where the statement has one.
            if (check_input_statement_selects(reader, statement, check_input_loop_statement_end(reader, statement)))
            {
                check_input_statement_defer(reader, attribute, statement, kind == CHECK_ATTRIBUTE_NOINLINE ? CHECK_INPUT_DEFERRED_NOINLINE : CHECK_INPUT_DEFERRED_ALWAYS_INLINE);
            }
            else
            {
                check_input_statement_inline_calls(reader, attribute, statement, kind == CHECK_ATTRIBUTE_NOINLINE, false);
            }
            attribute->ignored = true;
        }
        else if ((msvc_noinline || msvc_force || nomerge) && attribute->count_check != BUSTER_CHECK_COUNT_NONE)
        {
            check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, attribute->count_check | attribute->count_bound << 8);
            valid = false;
        }
        else if (msvc_noinline && statement < reader->parser->tokens.count)
        {
            if (check_input_statement_selects(reader, statement, check_input_loop_statement_end(reader, statement)))
            {
                check_input_statement_defer(reader, attribute, statement, CHECK_INPUT_DEFERRED_NOINLINE);
            }
            else
            {
                check_input_statement_inline_calls(reader, attribute, statement, true, false);
            }
            attribute->ignored = true;
        }
        else if (msvc_force && statement < reader->parser->tokens.count)
        {
            // MSVC's forceinline and forceinline_calls, which clang ignores off its ABI with its warning at the statement.
            check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED, statement,
                                                 BUSTER_CHECK_WARNING_NO_SOURCE);
            attribute->ignored = true;
        }
        else if (kind == CHECK_ATTRIBUTE_NOMERGE && statement < reader->parser->tokens.count &&
                 check_input_statement_selects(reader, statement, check_input_loop_statement_end(reader, statement)))
        {
            // Its calls are its selections' selected arms' alone, once they are read (check_input_statement_deferred).
            check_input_statement_defer(reader, attribute, statement, CHECK_INPUT_DEFERRED_NOMERGE);
            attribute->ignored = true;
        }
        else if (kind == CHECK_ATTRIBUTE_NOMERGE && statement < reader->parser->tokens.count && !check_input_statement_calls(reader, statement, false))
        {
            check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_NO_CALLS, statement,
                                                 BUSTER_CHECK_WARNING_NO_SOURCE);
            attribute->ignored = true;
        }
        else if (check_input_attribute_equal(generic, "likely") || check_input_attribute_equal(generic, "unlikely"))
        {
            // C23's clang::likely and clang::unlikely (#2291), branch hints that change nothing at -O0 and that clang takes on
            // any statement without a word, after their count's error.
            // The other one after one of them is clang's mutual exclusion error, with its note at the first.
            const bool this_likely = check_input_attribute_equal(generic, "likely");
            valid = attribute->count_check == BUSTER_CHECK_COUNT_NONE && (likelihood == BUSTER_CHECK_NONE || likely == this_likely);
            if (attribute->count_check != BUSTER_CHECK_COUNT_NONE)
            {
                check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, attribute->count_check | attribute->count_bound << 8);
            }
            else if (!valid)
            {
                check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_EXCLUSION, 0);
                if (reader->output->report_count > reported)
                {
                    reader->output->reports[reported].note_token = likelihood;
                    reader->output->reports[reported].note_kind = CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE;
                }
            }
            likely = likelihood == BUSTER_CHECK_NONE ? this_likely : likely;
            likelihood = likelihood == BUSTER_CHECK_NONE && valid ? attribute->first : likelihood;
            attribute->ignored = true;
        }
        else if (kind == CHECK_ATTRIBUTE_NOMERGE || kind == CHECK_ATTRIBUTE_SUPPRESS)
        {
            // clang's nomerge keeps the statement's calls from merging, which -O0's code never does, and suppress is the
            // static analyzer's.
            attribute->ignored = true;
        }
        else
        {
            // An Objective-C pointer type attribute (#1447): clang's parser's error at a later argument that names
            // nothing, then, as it has no subjects, its count's before the statement's.
            const bool objc = check_input_objc_kind(reader, attribute) != 0;
            const uint32_t undeclared = objc ? check_input_objc_undeclared(reader, attribute) : BUSTER_CHECK_NONE;
            valid = false;
            if (undeclared != BUSTER_CHECK_NONE)
            {
                check_input_report(reader, undeclared, CHECK_INPUT_UNDECLARED);
            }
            else if (objc && attribute->count_check != BUSTER_CHECK_COUNT_NONE)
            {
                check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, attribute->count_check | attribute->count_bound << 8);
            }
            else
            {
                check_input_report(reader, attribute->first, CHECK_INPUT_STATEMENT_ATTRIBUTE);
            }
        }
    }
    return valid;
}

// The first clang::likely or clang::unlikely among the C23 attribute lists from `token` on, as clang's
// Stmt::getLikelihood reads a statement's attributes (#2291): its first token (its scope's), BUSTER_CHECK_NONE if none,
// and in *likely which one.
static uint32_t check_input_statement_likelihood(CheckInputReader* reader, uint32_t token, bool* likely)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t result = BUSTER_CHECK_NONE;
    *likely = false;
    while (result == BUSTER_CHECK_NONE && token + 1 < count && parser->tokens.kinds[token] == PARSE_KIND_L_SQUARE &&
           parser->tokens.kinds[token + 1] == PARSE_KIND_L_SQUARE && parser->tree.parents[token] < count)
    {
        const uint32_t close = parser->tree.parents[token + 1];
        for (uint32_t item = token + 2; result == BUSTER_CHECK_NONE && item + 3 < close && close < count; item += 1)
        {
            const bool scoped = parser->tokens.kinds[item + 1] == PARSE_KIND_COLON && parser->tokens.kinds[item + 2] == PARSE_KIND_COLON &&
                                parser->tokens.names[item] != 0 && parser->tokens.names[item + 3] != 0 &&
                                (item == token + 2 || parser->tokens.kinds[item - 1] == PARSE_KIND_COMMA);
            const String8 scope = scoped ? check_input_attribute_name(reader, parser->tokens.names[item]) : (String8){ 0 };
            const String8 name = scoped ? check_input_attribute_name(reader, parser->tokens.names[item + 3]) : (String8){ 0 };
            const bool clang = check_input_attribute_equal(scope, "clang") || check_input_attribute_equal(scope, "_Clang");
            const bool is_likely = clang && (check_input_attribute_equal(name, "likely") || check_input_attribute_equal(name, "__likely__"));
            const bool is_unlikely = clang && (check_input_attribute_equal(name, "unlikely") || check_input_attribute_equal(name, "__unlikely__"));
            result = is_likely || is_unlikely ? item : BUSTER_CHECK_NONE;
            *likely = is_likely;
        }
        token = parser->tree.parents[token] + 1;
    }
    return result;
}

// The C23 attributes of an asm statement, from `first` to `end` (#147): each item's "'X' attribute ignored", at its first
// token (its scope's), named as clang names it, unknown ones too.
static void check_input_asm_statement_attributes(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    uint32_t token = first;
    while (token + 1 < end)
    {
        const bool list = parser->tokens.kinds[token] == PARSE_KIND_L_SQUARE && parser->tokens.kinds[token + 1] == PARSE_KIND_L_SQUARE;
        const uint32_t close = list ? parser->tree.parents[token + 1] : token;
        uint32_t item = token + 2;
        while (list && item < close && close < end)
        {
            const bool scoped = item + 3 < close && parser->tokens.kinds[item + 1] == PARSE_KIND_COLON && parser->tokens.kinds[item + 2] == PARSE_KIND_COLON;
            const uint32_t name = scoped ? item + 3 : item;
            if (parser->tokens.kinds[item] != PARSE_KIND_COMMA && name < close)
            {
                const CheckInputAttribute attribute = { .first = item, .name_token = name, .name = parser->tokens.names[name], .standard = true };
                check_input_attribute_named_event_at(reader, &attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED, item,
                                                     BUSTER_CHECK_WARNING_NO_SOURCE);
            }
            // Past its name and arguments to the next item's comma.
            uint32_t next = parser->tokens.kinds[item] == PARSE_KIND_COMMA ? item : name + 1;
            next = next < close && parser->tokens.kinds[next] == PARSE_KIND_L_PAREN && parser->tree.parents[next] < close ? parser->tree.parents[next] + 1 : next;
            item = next + 1;
        }
        token = list ? close + 2 : token + 1;
    }
}

// A block item's attributes before a statement rather than a declaration, read as the statement's (their tokens then
// ignored, so that check_input's walk takes no declaration for them), with clang's rules
// (check_input_statement_attribute_kinds): before a null statement, a statement's keyword or brace, or an expression when
// they are C23's alone or GNU ones of clang's statement attributes alone, which the parser then reads as a statement's
// (check_input_statement_attribute_names). A null statement's are recorded at its `;` for fallthrough's consumer, and a
// return's musttail for c_check_musttail.c and check_program. GNU attributes right after a named label are the label's
// (check_input_label_attributes); C23's there are the statement's. At file scope an attribute-only null declaration
// remains for check_input_declaration, which ignores GNU lists and checks C23 subjects.
// Whether a block item's attributes, from `first` to `after`, are GNU ones alone, each a suppress whose arguments clang's
// parser drops it for (#1844): each argument a string literal's tokens, or one in parentheses any number deep, separated
// by commas, as clang's parser reads them; anything else is its "expected string literal" or "expected ')'" (the
// attribute's own error, the checker's or the parser's). With none of them left, clang's ParseStatementOrDeclaration
// parses the statement after them as a declaration. A name alone and `()` are fine, and an empty item is none.
static bool check_input_suppress_dropped(CheckInputReader* reader, uint32_t first, uint32_t after)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    bool all = true;
    uint32_t dropped = 0;
    uint32_t token = first;
    while (all && token < after)
    {
        const bool specifier = parser->tokens.kinds[token] == PARSE_KIND_ATTRIBUTE && token + 2 < after && parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN &&
                               parser->tokens.kinds[token + 2] == PARSE_KIND_L_PAREN;
        const uint32_t close = specifier ? parser->tree.parents[token + 1] : count;
        all = specifier && close < after && close > token + 3 && parser->tokens.kinds[close - 1] == PARSE_KIND_R_PAREN;
        uint32_t item = token + 3;
        while (all && item < close - 1)
        {
            uint32_t end = item;
            uint32_t depth = 0;
            while (end < close - 1 && !(depth == 0 && parser->tokens.kinds[end] == PARSE_KIND_COMMA))
            {
                const uint8_t kind = parser->tokens.kinds[end];
                depth = kind == PARSE_KIND_L_PAREN ? depth + 1 : kind == PARSE_KIND_R_PAREN && depth != 0 ? depth - 1 : depth;
                end += 1;
            }
            if (end != item)
            {
                const String8 name = check_input_attribute_name(reader, parser->tokens.names[item]);
                const bool suppress = check_input_attribute_equal(name, "suppress") || check_input_attribute_equal(name, "__suppress__");
                bool malformed = false;
                uint32_t at = item + 2;
                bool done = suppress && (end == item + 1 || (at < end && parser->tokens.kinds[at] == PARSE_KIND_R_PAREN));
                while (suppress && !done && !malformed && at < end)
                {
                    uint32_t level = 0;
                    while (at < end && parser->tokens.kinds[at] == PARSE_KIND_L_PAREN)
                    {
                        level += 1;
                        at += 1;
                    }
                    const bool string = at < end && parser->tokens.kinds[at] == PARSE_KIND_STRING;
                    while (at < end && parser->tokens.kinds[at] == PARSE_KIND_STRING)
                    {
                        at += 1;
                    }
                    while (string && level != 0 && at < end && parser->tokens.kinds[at] == PARSE_KIND_R_PAREN)
                    {
                        level -= 1;
                        at += 1;
                    }
                    const uint8_t next = at < end ? parser->tokens.kinds[at] : PARSE_KIND_END;
                    malformed = !string || level != 0 || (next != PARSE_KIND_COMMA && next != PARSE_KIND_R_PAREN);
                    done = next == PARSE_KIND_R_PAREN;
                    at += 1;
                }
                all = suppress && malformed && parser->tokens.kinds[item + 1] == PARSE_KIND_L_PAREN;
                dropped += all ? 1 : 0;
            }
            item = end + 1;
        }
        token = close + 1;
    }
    return all && dropped != 0;
}

static void check_input_statement_attributes(CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    // (An asm statement's keyword, which an asm label's attribute reading would take, ends them.)
    const uint32_t skipped = check_input_skip_attributes(parser, first, count);
    uint32_t after = first;
    while (after < skipped && parser->tokens.kinds[after] != PARSE_KIND_ASM)
    {
        after += 1;
    }
    const uint8_t next = after < count ? parser->tokens.kinds[after] : PARSE_KIND_SEMI;
    const bool label = first != 0 && parser->tree.roles[first - 1] == PARSE_ROLE_LABEL && parser->tokens.kinds[first] == PARSE_KIND_ATTRIBUTE;
    const bool body = reader->function_body < count;
    const bool returns = after < count && next == PARSE_KIND_RETURN;
    const bool keyword = after < count && (returns || next == PARSE_KIND_IF || next == PARSE_KIND_SWITCH || next == PARSE_KIND_WHILE || next == PARSE_KIND_DO ||
                                           next == PARSE_KIND_FOR || next == PARSE_KIND_GOTO || next == PARSE_KIND_CONTINUE || next == PARSE_KIND_BREAK ||
                                           next == PARSE_KIND_CASE || next == PARSE_KIND_DEFAULT || next == PARSE_KIND_L_BRACE);
    // A declaration specifier among them before the `;` makes it a declaration of no declarator, which clang's
    // ParsedFreeStandingDeclSpec takes whatever the rest are (#147, check_input_declaration): `_Alignas(8);`, a calling
    // convention's keyword, _Noreturn and the nullability keywords, at the list's own depth.
    bool declaration_specifier = false;
    for (uint32_t token = first; token < after; token += 1)
    {
        const uint8_t token_kind = parser->tokens.kinds[token];
        const uint32_t name = parser->tokens.names[token];
        declaration_specifier |= parser->tree.depths[token] == parser->tree.depths[first] &&
                                 (token_kind == PARSE_KIND_ALIGNAS || token_kind == PARSE_KIND_CALLING_CONVENTION || name == C_NAME_NORETURN ||
                                  name == C_NAME_NONNULL || name == C_NAME_NULLABLE || name == C_NAME_NULLABLE_RESULT || name == C_NAME_NULL_UNSPECIFIED);
    }
    const bool free_standing = after < count && next == PARSE_KIND_SEMI && declaration_specifier;
    const bool empty = after < count && next == PARSE_KIND_SEMI && !free_standing;
    // GNU attributes among them make it a declaration unless each is one of clang's statement attributes, or they follow a
    // control statement's head (an if's, a loop's, a switch's condition, else, do), where C allows no declaration, or come
    // before a label's name; C23's alone leave it to what follows (#147's `[[x]] y += 1;`), as the parser reads them
    // (parse_scalar_block_attribute_end, parse_scalar_attributed_statement).
    bool gnu = false;
    for (uint32_t token = first; token < after; token += 1)
    {
        gnu |= parser->tokens.kinds[token] == PARSE_KIND_ATTRIBUTE;
    }
    const uint8_t before = first != 0 ? parser->tree.roles[first - 1] : PARSE_ROLE_NONE;
    const bool head = before == PARSE_ROLE_CLOSE_CONDITION || before == PARSE_ROLE_CLOSE_FOR || before == PARSE_ROLE_ELSE || before == PARSE_ROLE_DO;
    const bool labelled = next == PARSE_KIND_IDENTIFIER && after + 1 < count && parser->tree.roles[after + 1] == PARSE_ROLE_LABEL;
    const bool expression = after < count && !empty && !free_standing && !keyword && !check_input_declaration_after(parser, after) &&
                            (!gnu || head || labelled || check_input_statement_attribute_names(reader, first, after));
    // Before a label's name, the statement's attributes are its label's.
    const bool named = expression && labelled;
    if (label)
    {
        uint32_t end = first;
        while (end + 1 < count && parser->tokens.kinds[end] == PARSE_KIND_ATTRIBUTE && parser->tree.roles[end + 1] == PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER)
        {
            const uint32_t close = parser->tree.parents[end + 1];
            end = close > end + 1 && close < count ? close + 1 : count;
        }
        check_input_label_attributes(reader, first, end);
    }
    else if (named && body)
    {
        check_input_label_attributes(reader, first, after);
    }
    else if (body && next == PARSE_KIND_ASM)
    {
        // An asm statement's C23 attributes (#147, clang's ParseStatementOrDeclarationAfterAttributes): each is ignored with
        // -Wignored-attributes' warning at its first token, read no further (the parser refuses GNU ones there).
        check_input_asm_statement_attributes(reader, first, after);
        for (uint32_t token = first; token < after; token += 1)
        {
            reader->ignored[token] = true;
        }
    }
    else if (body && (empty || keyword || expression))
    {
        uint32_t cursor = first;
        CheckInputAttributes* attributes = 0;
        reader->statement_attributes = true;
        bool valid = check_input_read_attributes(reader, &cursor, count, &attributes) && cursor == after;
        reader->statement_attributes = false;
        // GNU suppresses that clang's parser drops, all of them (#1844): it parses the statement as a declaration, whose
        // first token here, no `(` nor `*` that a declarator could start with, is its second error. (A `(` or a `*` makes
        // an implicit int's declarator, whose errors and Sema's that follow are left out.)
        if (expression && gnu && !head && next != PARSE_KIND_L_PAREN && next != PARSE_KIND_STAR && check_input_suppress_dropped(reader, first, after))
        {
            check_input_report(reader, after, CHECK_INPUT_DROPPED_SUPPRESS_DECLARATION);
        }
        uint32_t musttail = BUSTER_CHECK_NONE;
        valid = valid && check_input_statement_attribute_kinds(reader, attributes, empty, returns, after, &musttail);
        if (valid && before == PARSE_ROLE_ELSE && parser->tree.parents[first - 1] < count)
        {
            // An else branch as likely or unlikely as its if's then branch: clang's warning at the then branch's, with its
            // note at this one (#2291).
            bool else_likely = false;
            bool then_likely = false;
            const uint32_t otherwise = check_input_statement_likelihood(reader, first, &else_likely);
            const uint32_t then = otherwise != BUSTER_CHECK_NONE ? check_input_statement_likelihood(reader, parser->tree.parents[first - 1] + 1, &then_likely) :
                                                                   BUSTER_CHECK_NONE;
            if (then != BUSTER_CHECK_NONE && then_likely == else_likely)
            {
                const CheckWarningEvent event =
                {
                    .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                    .kind = CHECK_WARNING_EVENT_LIKELIHOOD_CONFLICT,
                    .token = then,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    // Once the if's statement is read, after its branches' own warnings, as clang's ActOnIfStmt gives it.
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = after < count ? check_input_loop_statement_end(reader, after) : then,
                    .payload = { .name = parser->tokens.names[then + 3], .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                                 .data = parser->tokens.names[then] | BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE },
                };
                const CheckWarningNote note =
                {
                    .kind = CHECK_WARNING_NOTE_CONFLICTING_ATTRIBUTE,
                    .token = otherwise,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .payload = { .name = 0, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
                check_input_warning_event(reader, &event, &note, 1);
            }
        }
        bool fallthrough = false;
        for (uint32_t i = 0; valid && attributes != 0 && i < attributes->count; i += 1)
        {
            fallthrough |= attributes->records[i].kind == CHECK_ATTRIBUTE_FALLTHROUGH;
        }
        if (valid && empty && fallthrough)
        {
            check_input_set_attributes_at(reader->output, after, attributes);
        }
        if (valid && returns && musttail != BUSTER_CHECK_NONE)
        {
            const CheckInputMusttail record = { .statement = after, .attributes = first, .name = musttail, .body = reader->function_body };
            valid = check_input_musttail_record(reader, record);
            // Its function's own type and its scopes' cleanups, which the check reads, are source order's.
            check_input_lane_refuse(reader, reader->lane, "a body has a musttail return");
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
    // Per token: 1 + a label definition's or use's scope, or 0 for its body; BUSTER_CHECK_NONE for an ambiguous use.
    uint32_t* scopes;
    // Per name, the labels of the function's own scope that clang's lookup made (check_input_label_lookup): their count,
    // 1 + the name token of the first where a reference that was no ambiguity made it (or 0), and 1 + the function body
    // they are of, whose change empties them.
    uint32_t* function_counts;
    uint32_t* function_firsts;
    uint32_t* function_bodies;
    // Per name, 1 + the reference (its label's or its goto's or address's token) that made the first of them.
    uint32_t* function_ones;
    uint32_t count;
};

// What a label's reference finds (check_input_label_lookup): whether it is ambiguous, and its candidates for clang's
// notes: the name token of the innermost list's entry in scope of its name (or BUSTER_CHECK_NONE); whether the function's
// own labels of its name are among them; and the first of those, where a reference that was no ambiguity made it.
typedef struct CheckLabelLookup CheckLabelLookup;
struct CheckLabelLookup
{
    uint32_t local;
    uint32_t first;
    // The reference that made the function's own label it finds, where it finds that one alone, or BUSTER_CHECK_NONE.
    uint32_t one;
    bool function;
    bool ambiguous;
};

// A goto's or an address's CheckLocalLabels.scopes where its reference was ambiguous, and where a later definition
// found the label it made, which is then defined (check_input_label_lookup).
#define BUSTER_CHECK_LABEL_AMBIGUOUS BUSTER_CHECK_NONE
#define BUSTER_CHECK_LABEL_AMBIGUOUS_DEFINED (BUSTER_CHECK_NONE - 1)

// A label definition's or use's scope (CheckLocalLabels).
static uint32_t check_input_label_scope(const CheckInputReader* reader, const CheckLocalLabels* locals, uint32_t token)
{
    return locals->scopes != NULL && locals->scopes[token] != 0 ? locals->scopes[token] - 1 : check_sparse_get(reader->output->declaration_bodies, token);
}

// The name of a label table entry: a label's colon's first child, or a reserved &&name's (its prefix's). After attributes
// (`[[x]] l:`, #147) the colon's first child is their closer, which the parser makes the name's parent, and the name is
// right before the colon.
static uint32_t check_input_label_name(const CheckInputReader* reader, uint32_t entry)
{
    const Parser* parser = reader->parser;
    const uint32_t child = reader->first_child[entry];
    const bool attributed = child < parser->tokens.count && parser->tree.roles[child] == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
    return attributed ? entry - 1 : child;
}

// A label table entry's mark that a goto or an address found it (#2264, -Wunused-label), above its token plus one.
#define BUSTER_CHECK_LABEL_USED 0x80000000u

// The label of `scope` spelled `name` in check_input_controls' table (`capacity` slots), or BUSTER_CHECK_NONE.
static uint32_t check_input_find_label(const CheckInputReader* reader, const CheckLocalLabels* locals, const uint32_t* labels, uint64_t capacity, uint32_t scope,
                                       uint32_t name)
{
    const Parser* parser = reader->parser;
    uint32_t result = BUSTER_CHECK_NONE;
    uint64_t slot = ((uint64_t)name * UINT64_C(11400714819323198485) + scope) & (capacity - 1);
    while (result == BUSTER_CHECK_NONE && labels[slot] != 0)
    {
        const uint32_t target = (labels[slot] & ~BUSTER_CHECK_LABEL_USED) - 1;
        const uint32_t target_name = check_input_label_name(reader, target);
        result = check_input_label_scope(reader, locals, target) == scope && parser->tokens.names[target_name] == name ? target : BUSTER_CHECK_NONE;
        slot = (slot + 1) & (capacity - 1);
    }
    return result;
}

// The slot of label `target` (its colon) in check_input_controls' table (`capacity` slots), or `capacity` if it has none.
static uint64_t check_input_label_slot(const CheckInputReader* reader, const CheckLocalLabels* locals, const uint32_t* labels, uint64_t capacity, uint32_t target)
{
    const Parser* parser = reader->parser;
    const uint32_t name = parser->tokens.names[check_input_label_name(reader, target)];
    const uint32_t scope = check_input_label_scope(reader, locals, target);
    uint64_t slot = ((uint64_t)name * UINT64_C(11400714819323198485) + scope) & (capacity - 1);
    uint64_t result = capacity;
    while (result == capacity && labels[slot] != 0)
    {
        result = (labels[slot] & ~BUSTER_CHECK_LABEL_USED) == target + 1 ? slot : capacity;
        slot = (slot + 1) & (capacity - 1);
    }
    return result;
}

// -Wunused-label (#2264, clang's DiagnoseUnusedDecl of a LabelDecl as its function's scope pops): each label of the table
// (`capacity` slots) no goto or address found (BUSTER_CHECK_LABEL_USED), from `begin` to `limit`, but one an unused or
// maybe_unused attribute marks. Its event is at its name, or a local label's at its `__label__` list's name, and completes
// at its function body's `}`, among that scope's unused variables by place, as clang sorts them.
static void check_input_unused_labels(CheckInputReader* reader, const CheckLocalLabels* locals, const uint32_t* labels, uint64_t capacity, uint32_t begin,
                                      uint32_t limit)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    for (uint64_t slot = 0; slot < capacity; slot += 1)
    {
        const uint32_t label = labels[slot] != 0 ? (labels[slot] & ~BUSTER_CHECK_LABEL_USED) - 1 : BUSTER_CHECK_NONE;
        const bool defined = label >= begin && label < limit && parser->tree.roles[label] == PARSE_ROLE_LABEL && (labels[slot] & BUSTER_CHECK_LABEL_USED) == 0;
        const uint32_t body = defined ? check_sparse_get(reader->output->declaration_bodies, label) : BUSTER_CHECK_NONE;
        const uint32_t close = body < count && parser->tree.roles[body] == PARSE_ROLE_OPEN_BODY ? parser->tree.parents[body] : BUSTER_CHECK_NONE;
        const CheckInputAttributes* attributes = close < count ? check_input_attributes_at(reader->output, label) : NULL;
        bool marked = false;
        for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
        {
            marked |= attributes->records[i].kind == CHECK_ATTRIBUTE_UNUSED;
        }
        if (close < count && !marked)
        {
            const uint32_t name = check_input_label_name(reader, label);
            const uint32_t scope = check_input_label_scope(reader, locals, label);
            const uint32_t at = scope != body && scope < count ? scope : name;
            const CheckWarningEvent warning = {
                .id = CHECK_WARNING_ID_UNUSED_LABEL,
                .kind = CHECK_WARNING_EVENT_UNUSED_LABEL,
                .token = at,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .flags = BUSTER_CHECK_WARNING_DEFAULT_IGNORED,
                .completion_kind = CHECK_WARNING_COMPLETE_AFTER_DELIMITER,
                .completion = close,
                .payload = { .name = parser->tokens.names[name], .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                .suborder = (uint64_t)at + 1,
            };
            (void)check_input_warning_event(reader, &warning, NULL, 0);
        }
    }
}

// Makes the local labels' columns (CheckLocalLabels) on a unit's first list: the heads and the function's labels per name,
// the entries and scopes per token, each of which takes at most one.
static void check_input_local_labels_make(CheckInputReader* reader, CheckLocalLabels* locals)
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
        locals->function_counts = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)names + 1);
        locals->function_firsts = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)names + 1);
        locals->function_bodies = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)names + 1);
        locals->function_ones = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)names + 1);
        const bool made = locals->heads != NULL && locals->names != NULL && locals->shadowed != NULL && locals->tokens != NULL && locals->ends != NULL &&
                          locals->scopes != NULL && locals->function_counts != NULL && locals->function_firsts != NULL && locals->function_bodies != NULL &&
                          locals->function_ones != NULL;
        reader->output->resource |= !made;
        if (made)
        {
            memset(locals->heads, 0, ((uint64_t)names + 1) * sizeof(uint32_t));
            memset(locals->scopes, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
            memset(locals->function_bodies, 0, ((uint64_t)names + 1) * sizeof(uint32_t));
        }
        else
        {
            locals->heads = NULL;
            locals->scopes = NULL;
        }
    }
}

// Pushes a `__label__` list's names (check_input_local_labels marked it), whose block ends at its statement's parent's
// parent: the block's closing brace, after the list's `;`, making the columns on a unit's first list
// (check_input_local_labels_make).
static void check_input_local_label_push(CheckInputReader* reader, CheckLocalLabels* locals, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t names = reader->pp->table->count;
    check_input_local_labels_make(reader, locals);
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

// clang's lookup of a label's name at reference `name_token` (a definition's, a goto's or an address's) in function body
// `body`, whose closing brace is `close` (#1844): LookupSingleName's declarations of the first one's scope, then
// LookupOrCreateLabel's. The innermost list in scope with the name decides where its block isn't the body: ambiguous
// where it names it twice. Otherwise its candidates are the body's list's entries (none, one, or two and more) and the
// labels of the function's own scope, which is the body's: each reference that found no label made one, and so does each
// ambiguous reference, wherever it is (a later reference that finds that one alone takes it). Two and more are ambiguous.
// Makes the label the reference makes. Without lists in the unit, a name is never ambiguous.
static CheckLabelLookup check_input_label_lookup(const CheckInputReader* reader, CheckLocalLabels* locals, uint32_t reference, uint32_t name_token, uint32_t body,
                                                 uint32_t close)
{
    const Parser* parser = reader->parser;
    const uint32_t name = parser->tokens.names[name_token];
    CheckLabelLookup result = { .local = BUSTER_CHECK_NONE, .first = BUSTER_CHECK_NONE, .one = BUSTER_CHECK_NONE };
    if (locals->heads != NULL && name < reader->pp->table->count && body != BUSTER_CHECK_NONE)
    {
        if (locals->function_bodies[name] != body + 1)
        {
            locals->function_bodies[name] = body + 1;
            locals->function_counts[name] = 0;
            locals->function_firsts[name] = 0;
            locals->function_ones[name] = 0;
        }
        const uint32_t local = check_input_local_label(reader, locals, name);
        const bool top = local == BUSTER_CHECK_NONE || locals->ends[local] == close;
        const uint32_t made = locals->function_counts[name];
        const uint32_t entries = local == BUSTER_CHECK_NONE ? 0 : check_input_local_label_ambiguous(locals, local) ? 2 : 1;
        result.local = local != BUSTER_CHECK_NONE ? locals->tokens[local] : BUSTER_CHECK_NONE;
        result.function = top && made != 0;
        result.first = top && locals->function_firsts[name] != 0 ? locals->function_firsts[name] - 1 : BUSTER_CHECK_NONE;
        result.ambiguous = top ? entries + made >= 2 : entries >= 2;
        const bool makes = result.ambiguous || (local == BUSTER_CHECK_NONE && made == 0);
        result.one = !result.ambiguous && local == BUSTER_CHECK_NONE && made == 1 ? locals->function_ones[name] - 1 : BUSTER_CHECK_NONE;
        locals->function_firsts[name] = !result.ambiguous && makes ? name_token + 1 : locals->function_firsts[name];
        locals->function_ones[name] = made == 0 && makes ? reference + 1 : locals->function_ones[name];
        locals->function_counts[name] = made + (makes ? 1 : 0);
    }
    return result;
}

// The lookups of the references in function body `body` (whose closing brace is `close`) before the unit's first list,
// at `keyword`, which made no columns for them when they met them (#1844): each name's first, a label's definition, a
// goto's or an address's, made the function's label a later ambiguity counts. No reference before a list is ambiguous.
static void check_input_label_lookups_before(CheckInputReader* reader, CheckLocalLabels* locals, uint32_t body, uint32_t close, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    for (uint32_t token = body + 1; token < keyword; token += 1)
    {
        const bool deferred = reader->output->deferred_tokens != 0 && reader->output->deferred_tokens[token] != 0;
        const uint8_t kind = parser->tokens.kinds[token];
        const bool definition = parser->tree.roles[token] == PARSE_ROLE_LABEL;
        const bool jump = kind == PARSE_KIND_GOTO && check_input_asm_id(reader->output, token) == 0 && !check_input_indirect_goto(parser, token);
        const bool address = kind == PARSE_KIND_AMPAMP && parser->tree.roles[token] == PARSE_ROLE_PREFIX;
        const uint32_t name = deferred ? count : definition ? check_input_label_name(reader, token) : jump || address ? reader->first_child[token] : count;
        if (name < count && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER)
        {
            (void)check_input_label_lookup(reader, locals, token, name, body, close);
        }
    }
}

// Reports clang's "reference to 'X' is ambiguous" at a label's name, with what its notes name (the driver's): the
// innermost list's entry of the name in the candidates' scope (`note_token`, whose list's earlier entries of the name the
// driver adds), and where the function's own labels are among them, `detail` 1 and the first of them that a reference
// that was no ambiguity made (`attribute`), before the earlier ambiguous references' (each of which made one).
static void check_input_report_label_ambiguous(CheckInputReader* reader, uint32_t name_token, const CheckLabelLookup* lookup)
{
    const uint32_t before = reader->output->report_count;
    check_input_report(reader, name_token, CHECK_INPUT_LABEL_AMBIGUOUS);
    if (reader->output->report_count > before)
    {
        CheckInputReport* report = reader->output->reports + before;
        report->note_token = lookup->local;
        report->detail = lookup->function ? 1 : 0;
        report->attribute = lookup->first;
    }
}

// clang's "use of undeclared label" at `token`, a label's name, given where clang gives it: as the scope that declares the
// label pops, at `close` (a block's closing brace for a local label's list, the function body's for a label of the
// function's own), after the errors before it. A function's label (`function`) is reported once: clang's function scope
// creates a name's label at its first reference, which the later ones find (CheckInputReport.detail marks it, and a later
// one of the same name at the same close is none), but an ambiguous reference's (`own`) is a label of its own, which
// clang's lookup made (check_input_label_lookup). An error's path alone.
static void check_input_report_label_undeclared(CheckInputReader* reader, uint32_t token, uint32_t close, bool function, bool own)
{
    const CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint32_t name = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? parser->tokens.names[token] : BUSTER_CHECK_NONE;
    bool reported = false;
    for (uint32_t r = input->report_count; !own && !reported && name != BUSTER_CHECK_NONE && close != 0 && r-- > 0;)
    {
        const CheckInputReport* report = input->reports + r;
        reported = function && report->message == CHECK_INPUT_LABEL_UNDECLARED && report->detail == 1 && report->sort == close && report->token < parser->tokens.count &&
                   parser->tokens.kinds[report->token] == PARSE_KIND_IDENTIFIER && parser->tokens.names[report->token] == name;
    }
    const uint32_t before = input->report_count;
    if (!reported)
    {
        check_input_report(reader, token, CHECK_INPUT_LABEL_UNDECLARED);
    }
    if (reader->output->report_count > before)
    {
        reader->output->reports[before].sort = close;
        reader->output->reports[before].repeated = true;
        reader->output->reports[before].detail = function ? 1 : 0;
    }
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
                check_input_report_label_undeclared(reader, name_token, end, false, false);
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
                const uint32_t label = check_input_label_address(parser, token) ? check_sparse_get(reader->output->label_targets, token) : BUSTER_CHECK_NONE;
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

static uint32_t check_input_control_next_range(const Parser* parser, CheckInputControlScan* scan, bool resolving, uint32_t count)
{
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

static uint32_t check_input_control_next(const Parser* parser, CheckInputControlScan* scan, bool resolving)
{
    return check_input_control_next_range(parser, scan, resolving, parser->tokens.count);
}

// Only definitions enter the full-unit table. Diagnostic prefixes can also insert an unresolved &&name
// in an unfinished function, so reserve for those address sites before prefix_count as well. Count a safe
// superset (including deferred/invalid/duplicate sites), keeping the table below half full without token-sized
// clearing. The normal path reads roles only; both columns are bounded, unpadded byte arrays.
static uint64_t check_input_label_capacity_range(const Parser* parser, uint32_t prefix_count, uint32_t begin, uint32_t count)
{
    uint64_t entries = 0;
    if (BUSTER_SCALAR_PATH(parser->options.scalar))
    {
        for (uint32_t token = begin; token < count; token += 1)
        {
            entries += parser->tree.roles[token] == PARSE_ROLE_LABEL || (token < prefix_count && check_input_label_address(parser, token));
        }
    }
    else
    {
        for (uint64_t first = begin; first < count; first += BUSTER_CHECK_CONTROL_TOKENS)
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

static uint64_t check_input_label_capacity(const Parser* parser, uint32_t prefix_count)
{
    return check_input_label_capacity_range(parser, prefix_count, 0, parser->tokens.count);
}

// Whether token `token` is within statement `root`'s subtree: its parents lead to `root`, each later than the one
// before (a statement's tokens' parents are within it).
static bool check_input_fallthrough_within(const Parser* parser, uint32_t token, uint32_t root)
{
    uint32_t at = token;
    while (at < root)
    {
        at = parser->tree.parents[at];
    }
    return at == root;
}

// The first token of statement `item`'s subtree. A subtree is a run of tokens, so its first is its last (the reader's
// `ends`) less its length (`subtree_sizes`, by postorder position) plus one: one load each (#1357), where stepping back
// a token at a time, each step a climb to `item`, cost a switch's fallthroughs time quadratic in its statements. A reader
// without the traversal's columns steps back.
static uint32_t check_input_fallthrough_first(const CheckInputReader* reader, uint32_t item)
{
    const Parser* parser = reader->parser;
    const bool columns = reader->ends != NULL && reader->subtree_sizes != NULL && reader->postorder_positions != NULL && item < parser->tokens.count;
    const uint32_t size = columns ? reader->subtree_sizes[reader->postorder_positions[item]] : 0;
    const uint32_t end = columns ? reader->ends[item] : 0;
    uint32_t first = item;
    if (size != 0 && size - 1 <= end && end + 1 - size <= item)
    {
        first = end + 1 - size;
    }
    else
    {
        while (first > 0 && check_input_fallthrough_within(parser, first - 1, item))
        {
            first -= 1;
        }
    }
    return first;
}

// What a statement that runs after a fallthrough is (check_input_fallthrough_item): a switch's case or default label
// (`PLACED`), one without a statement (`EMPTY`: a null statement, an empty compound, a plain label on one), or any other.
#define BUSTER_CHECK_FALLTHROUGH_OTHER 0u
#define BUSTER_CHECK_FALLTHROUGH_PLACED 1u
#define BUSTER_CHECK_FALLTHROUGH_EMPTY 2u

// What one statement (`item`, its top token) is on its own for check_input_fallthrough_item, as clang's CFG check
// (FallthroughMapper::checkFallThroughIntoBlock) looks back from a case's block: a block without executable statements is
// looked through to the ones before it (#1126's review). A null statement is EMPTY, and so is a declaration that adds
// nothing to clang's CFG; a case or default label is PLACED; a compound or a plain label is BUSTER_CHECK_FALLTHROUGH_DESCEND,
// its items or its statement deciding (`*inner`, the first to look at), but a plain label directly on a case label isn't
// placed (clang takes such a label's block as the case's own and doesn't look through it); any other statement is OTHER.
#define BUSTER_CHECK_FALLTHROUGH_DESCEND 3u
static uint32_t check_input_fallthrough_statement(const CheckInputReader* reader, uint32_t item, uint32_t* inner)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint8_t role = item < count ? parser->tree.roles[item] : PARSE_ROLE_NONE;
    uint32_t result = BUSTER_CHECK_FALLTHROUGH_OTHER;
    *inner = BUSTER_CHECK_NONE;
    if (item < count && parser->tokens.kinds[item] == PARSE_KIND_SEMI && (item == 0 || !check_input_fallthrough_within(parser, item - 1, item)))
    {
        result = BUSTER_CHECK_FALLTHROUGH_EMPTY;
    }
    else if (role == PARSE_ROLE_CLOSE_CASE)
    {
        result = BUSTER_CHECK_FALLTHROUGH_PLACED;
    }
    else if (role == PARSE_ROLE_OPEN_BLOCK)
    {
        // A compound's opening brace, its first child.
        result = BUSTER_CHECK_FALLTHROUGH_EMPTY;
    }
    else if (item < count && parser->tokens.kinds[item] == PARSE_KIND_SEMI)
    {
        // A declaration that declares no variable, which clang's CFG doesn't add (CFGBuilder::VisitDeclSubExpr, #1126's
        // review): a typedef but of a variably modified type, a tag's, a function's, an enumerator's, a static assertion
        // or a local label's (`__label__`). A variable's declaration, any statement's: other.
        const uint32_t first = check_input_fallthrough_first(reader, item);
        const uint8_t first_kind = parser->tokens.kinds[first];
        bool declares = first_kind == PARSE_KIND_STATIC_ASSERT || first_kind == PARSE_KIND_LABEL;
        bool runs = false;
        for (uint32_t token = first; token < item; token += 1)
        {
            const bool declared = reader->output->declarations[token] == token;
            const uint8_t binding = declared ? reader->declaration_kinds[token] : C_BINDING_KINDS;
            const TypeId type = binding == C_BINDING_TYPEDEF ? reader->declared_types[token] : 0;
            // A prototype's parameter is no variable of the block's: it is inside a function declarator's parentheses
            // (which the two parsers mark as a call's or a function's), after their `(`, where the declared name, the
            // declarator's callee, is before it.
            bool parameter = false;
            uint32_t below = token;
            uint32_t at = declared ? parser->tree.parents[token] : item;
            while (at < item && !parameter)
            {
                const uint8_t at_role = parser->tree.roles[at];
                uint32_t open = BUSTER_CHECK_NONE;
                for (uint32_t child = at_role == PARSE_ROLE_CLOSE_CALL || at_role == PARSE_ROLE_CLOSE_FUNCTION ? reader->first_child[at] : BUSTER_CHECK_NONE;
                     child != BUSTER_CHECK_NONE; child = reader->next_child[child])
                {
                    open = parser->tree.roles[child] == PARSE_ROLE_OPEN_CALL || parser->tree.roles[child] == PARSE_ROLE_OPEN_FUNCTION ? child : open;
                }
                parameter = open != BUSTER_CHECK_NONE && open < below;
                below = at;
                at = parser->tree.parents[at];
            }
            const bool variable = binding == C_BINDING_OBJECT && !parameter;
            const bool variably = type != 0 && (type_inherited_flags(reader->types, type) & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED) != 0;
            declares |= declared || parser->tree.roles[token] == PARSE_ROLE_TAG;
            runs |= variable || variably;
        }
        result = declares && !runs ? BUSTER_CHECK_FALLTHROUGH_EMPTY : BUSTER_CHECK_FALLTHROUGH_OTHER;
    }
    else if (role == PARSE_ROLE_CLOSE_BLOCK)
    {
        // The compound's items in order, its first that isn't empty deciding; an empty one is empty.
        *inner = reader->first_child[item];
        result = *inner != BUSTER_CHECK_NONE ? BUSTER_CHECK_FALLTHROUGH_DESCEND : BUSTER_CHECK_FALLTHROUGH_EMPTY;
    }
    else if (role == PARSE_ROLE_LABEL)
    {
        // A plain label's statement: its child after it (the name is before it).
        uint32_t statement = BUSTER_CHECK_NONE;
        for (uint32_t child = reader->first_child[item]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            statement = statement == BUSTER_CHECK_NONE && child > item ? child : statement;
        }
        const bool case_label = statement < count && parser->tree.roles[statement] == PARSE_ROLE_CLOSE_CASE;
        *inner = statement;
        result = case_label ? BUSTER_CHECK_FALLTHROUGH_OTHER : statement != BUSTER_CHECK_NONE ? BUSTER_CHECK_FALLTHROUGH_DESCEND : BUSTER_CHECK_FALLTHROUGH_EMPTY;
    }
    return result;
}

// Classifies statement `item` for check_input_fallthrough_placed: its first statement in source order that isn't EMPTY
// (check_input_fallthrough_statement), compounds' items and labels' statements looked into, decides; EMPTY if none
// is. The walk goes down to a statement's inner one, and from an empty one on to its next sibling, or up to its
// parent's, never above `item`: no recursion (the call graph's rule).
static uint32_t check_input_fallthrough_item(const CheckInputReader* reader, uint32_t item)
{
    const Parser* parser = reader->parser;
    uint32_t node = item;
    uint32_t result = BUSTER_CHECK_NONE;
    while (result == BUSTER_CHECK_NONE)
    {
        uint32_t inner = BUSTER_CHECK_NONE;
        const uint32_t kind = check_input_fallthrough_statement(reader, node, &inner);
        if (kind == BUSTER_CHECK_FALLTHROUGH_DESCEND)
        {
            node = inner;
        }
        else if (kind != BUSTER_CHECK_FALLTHROUGH_EMPTY)
        {
            result = kind;
        }
        else
        {
            // The next statement after an empty one: its next sibling, else its parent's, until `item` is done.
            bool climbing = true;
            while (result == BUSTER_CHECK_NONE && climbing)
            {
                if (node == item)
                {
                    result = BUSTER_CHECK_FALLTHROUGH_EMPTY;
                }
                else if (reader->next_child[node] != BUSTER_CHECK_NONE)
                {
                    node = reader->next_child[node];
                    climbing = false;
                }
                else
                {
                    node = parser->tree.parents[node];
                }
            }
        }
    }
    return result;
}

// A token's spelling into `buffer` (`capacity` bytes), without the reader's scratch arena: an identifier's or a keyword's
// interned name, a short literal's as the preprocessor spells it. 0 for none or a longer one.
static uint32_t check_input_fallthrough_spelling(const CheckInputReader* reader, uint32_t token, uint8_t* buffer, uint32_t capacity, const uint8_t** bytes)
{
    const Parser* parser = reader->parser;
    Preprocessor* pp = reader->pp;
    const uint32_t name = token < parser->tokens.count ? parser->tokens.names[token] : 0;
    const uint32_t source = pp != NULL && token < parser->tokens.count ? parser->tokens.sources[token] : BUSTER_PP_NONE;
    const bool spelled = pp != NULL && source < pp->output.count;
    const uint8_t kind = spelled ? pp->output.kinds[source] : 0;
    const uint64_t needed = spelled && kind != BUSTER_PP_KIND_EMBED ?
                            (kind == BUSTER_PP_KIND_INTEGER ? 24 : (uint64_t)pp->output.values[source] - pp->output.locations[source] + 1) : UINT64_MAX;
    uint32_t length = 0;
    if (pp != NULL && name != 0)
    {
        *bytes = intern_bytes(pp->table, name);
        length = intern_length(pp->table, name);
    }
    else if (needed <= capacity)
    {
        length = pp_spelling(pp, kind, pp->output.values[source], pp->output.locations[source], buffer, bytes);
    }
    return length;
}


// The statement a plain label or a case or default label (`item`, its colon) labels: its child after it, or none.
static uint32_t check_input_fallthrough_labeled(const CheckInputReader* reader, uint32_t item)
{
    uint32_t statement = BUSTER_CHECK_NONE;
    for (uint32_t child = reader->first_child[item]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
    {
        statement = statement == BUSTER_CHECK_NONE && child > item ? child : statement;
    }
    return statement;
}

// The root of tokens `first` to `end`, an expression's: the one token whose parent lies outside them, or
// BUSTER_CHECK_NONE for none or several.
static uint32_t check_input_fallthrough_root(const Parser* parser, uint32_t first, uint32_t end)
{
    uint32_t root = BUSTER_CHECK_NONE;
    uint32_t roots = 0;
    for (uint32_t token = first; token < end && roots < 2; token += 1)
    {
        const uint32_t parent = parser->tree.parents[token];
        const bool outside = parent < first || parent >= end;
        root = outside ? token : root;
        roots += outside ? 1 : 0;
    }
    return roots == 1 ? root : BUSTER_CHECK_NONE;
}

// An expression's truth, as clang's EvaluateAsBooleanCondition folds it: tokens `first` to `end`, evaluated as the
// source evaluator folds a constant (an integer constant expression, a const object's constant initializer, a comma's
// right operand past its left one); 1 for a nonzero integer or floating value (a NaN's too), 0 for a zero one or a null
// pointer, 2 for any other (a symbol's address, which a weak declaration may make null, among them). A reader without a
// preprocessor (a synthetic control walk's) folds `true` and `false` alone.
static uint32_t check_input_fallthrough_truth(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    uint32_t result = 2;
    if (reader->pp == NULL)
    {
        const uint32_t name = first + 1 == end ? parser->tokens.names[first] : 0;
        result = name == C_NAME_TRUE ? 1 : name == C_NAME_FALSE ? 0 : 2;
    }
    else
    {
        CheckValue value = { 0 };
        const bool known = first < end && check_input_evaluate_cached(reader, first, end, &value) && check_input_const_rvalue(reader, end, &value) &&
                           value.fault == CHECK_FAULT_NONE && (value.flags & BUSTER_CHECK_MODIFICATION) == 0;
        const CheckConstantKind kind = known ? value.constant_kind : CHECK_CONSTANT_NONE;
        if (kind == CHECK_CONSTANT_INTEGER)
        {
            result = value.integer != 0;
        }
        else if (kind == CHECK_CONSTANT_FLOATING)
        {
            result = value.floating != 0 || value.floating != value.floating;
        }
        else if (kind == CHECK_CONSTANT_ADDRESS && value.object == 0)
        {
            result = value.offset != 0;
        }
    }
    return result;
}

// A range `*first` to `*end` past the parentheses around it, as clang's IgnoreParens: its root token, or BUSTER_CHECK_NONE.
static uint32_t check_input_fallthrough_ungroup(const Parser* parser, uint32_t* first, uint32_t* end)
{
    const uint32_t count = parser->tokens.count;
    uint32_t root = check_input_fallthrough_root(parser, *first, *end);
    while (root < count && root + 1 == *end && *first < root && parser->tree.roles[root] == PARSE_ROLE_CLOSE_GROUP && parser->tokens.kinds[*first] == PARSE_KIND_L_PAREN &&
           parser->tree.roles[*first] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[*first] == root)
    {
        *first += 1;
        *end -= 1;
        root = check_input_fallthrough_root(parser, *first, *end);
    }
    return root;
}

// An integer literal's value and width (`first` to `end`, one number token), its bits below its width, as clang's
// IntegerLiteral has it.
static bool check_input_fallthrough_integer_literal(CheckInputReader* reader, uint32_t first, uint32_t end, uint64_t* bits, uint32_t* width, bool* is_signed)
{
    const Parser* parser = reader->parser;
    CheckValue value = { 0 };
    bool result = first + 1 == end && parser->tokens.kinds[first] == PARSE_KIND_NUMBER && reader->pp != NULL &&
                  check_input_evaluate_cached(reader, first, end, &value) && value.constant_kind == CHECK_CONSTANT_INTEGER &&
                  check_action_integer_type(reader->types, value.type, width, is_signed) && *width <= 64;
    *bits = result ? (uint64_t)value.integer & (*width == 64 ? UINT64_MAX : (UINT64_C(1) << *width) - 1) : 0;
    return result;
}

// clang's CFG builder's getIntegerLiteralSubexpressionValue: an integer literal past its parentheses, or one that `+`,
// `-`, `~` or `!` takes, its value computed in the literal's width (`!`'s in int's); its bits and width.
static bool check_input_fallthrough_literal_value(CheckInputReader* reader, uint32_t first, uint32_t end, uint64_t* bits, uint32_t* width)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t root = check_input_fallthrough_ungroup(parser, &first, &end);
    const uint8_t kind = root < count ? parser->tokens.kinds[root] : 0;
    const bool unary = root == first && root + 1 < end && parser->tree.roles[root] == PARSE_ROLE_PREFIX &&
                       (kind == PARSE_KIND_PLUS || kind == PARSE_KIND_MINUS || kind == PARSE_KIND_TILDE || kind == PARSE_KIND_EXCLAIM);
    uint32_t inner_first = unary ? first + 1 : first;
    uint32_t inner_end = end;
    if (unary)
    {
        (void)check_input_fallthrough_ungroup(parser, &inner_first, &inner_end);
    }
    bool is_signed = false;
    const bool result = check_input_fallthrough_integer_literal(reader, inner_first, inner_end, bits, width, &is_signed);
    const uint64_t mask = *width == 64 ? UINT64_MAX : (UINT64_C(1) << *width) - 1;
    if (result && unary && kind == PARSE_KIND_MINUS)
    {
        *bits = (0 - *bits) & mask;
    }
    else if (result && unary && kind == PARSE_KIND_TILDE)
    {
        *bits = ~*bits & mask;
    }
    else if (result && unary && kind == PARSE_KIND_EXCLAIM)
    {
        *bits = *bits == 0;
        *width = 32;
    }
    return result;
}

// A range's single identifier's declaration, past the range's parentheses, or BUSTER_CHECK_NONE.
static uint32_t check_input_fallthrough_named(const CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    (void)check_input_fallthrough_ungroup(parser, &first, &end);
    return first + 1 == end && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER ? reader->output->declarations[first] : BUSTER_CHECK_NONE;
}

// A member access's operator past the range's parentheses (`base.name` or `base->name`), or BUSTER_CHECK_NONE; its base's
// range into `*base_end` (from `*first`, which the parentheses move).
static uint32_t check_input_fallthrough_member(const Parser* parser, uint32_t* first, uint32_t* end)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t root = check_input_fallthrough_ungroup(parser, first, end);
    const bool member = root < count && *first < root && root + 2 == *end && parser->tree.roles[root] == PARSE_ROLE_MEMBER_OPERATOR &&
                        parser->tree.roles[root + 1] == PARSE_ROLE_MEMBER;
    return member ? root : BUSTER_CHECK_NONE;
}

// A subscript's `[` past the range's parentheses (`base[index]`, its `]` the range's last token), or BUSTER_CHECK_NONE.
static uint32_t check_input_fallthrough_subscript(const Parser* parser, uint32_t* first, uint32_t* end)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t root = check_input_fallthrough_ungroup(parser, first, end);
    uint32_t open = BUSTER_CHECK_NONE;
    if (root < count && *first < root && root + 1 == *end && parser->tree.roles[root] == PARSE_ROLE_CLOSE_SUBSCRIPT)
    {
        // The `[` that the closing `]` matches, past the brackets nested in the index.
        uint32_t depth = 0;
        uint32_t token = root;
        while (open == BUSTER_CHECK_NONE && token > *first)
        {
            token -= 1;
            const uint8_t kind = parser->tokens.kinds[token];
            open = kind == PARSE_KIND_L_SQUARE && depth == 0 ? token : open;
            depth = kind == PARSE_KIND_R_SQUARE ? depth + 1 : kind == PARSE_KIND_L_SQUARE && depth != 0 ? depth - 1 : depth;
        }
        open = open != BUSTER_CHECK_NONE && *first < open ? open : BUSTER_CHECK_NONE;
    }
    return open;
}

// An integer literal's value, a lone number token's (clang's IntegerLiteral, which a subscript's index is with no
// parentheses), into `*bits`.
static bool check_input_fallthrough_index_literal(CheckInputReader* reader, uint32_t first, uint32_t end, uint64_t* bits)
{
    const Parser* parser = reader->parser;
    CheckValue value = { 0 };
    const bool result = first + 1 == end && parser->tokens.kinds[first] == PARSE_KIND_NUMBER && reader->pp != NULL &&
                        check_input_evaluate_cached(reader, first, end, &value) && value.constant_kind == CHECK_CONSTANT_INTEGER;
    *bits = result ? (uint64_t)value.integer : 0;
    return result;
}

// Whether two operands are the same, as clang's Expr::isSameComparisonOperand takes them past the same implicit
// conversions (`converted` false where their conversions differ, which leaves names alone): past their parentheses, one
// identifier each that names one declaration; members of the same name (`s.m`, `p->m`, `s.a.b`) whose bases are the same
// name or the same members, through parentheses; or elements (`a[1]`, `s.a[i]`, `a[1][j]`) of the same bases whose
// indices are integer literals of one value, or else the same operands. A stack of 16 pairs takes them: no recursion (the
// call graph's rule), and a deeper one is not the same.
static bool check_input_fallthrough_same_operand_converted(CheckInputReader* reader, uint32_t first1, uint32_t end1, uint32_t first2, uint32_t end2,
                                                           bool converted)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t firsts[16][2];
    uint32_t ends[16][2];
    // A pair's operands: any of the three (0); a member chain's bases, names or members alone (1), as clang walks a
    // MemberExpr chain past its bases' implicit conversions; or names alone (2), past conversions that differ.
    uint8_t modes[16];
    uint32_t depth = 1;
    firsts[0][0] = first1;
    firsts[0][1] = first2;
    ends[0][0] = end1;
    ends[0][1] = end2;
    modes[0] = converted ? 0 : 2;
    bool same = true;
    while (same && depth != 0)
    {
        depth -= 1;
        const uint8_t mode = modes[depth];
        uint32_t member_first1 = firsts[depth][0];
        uint32_t member_end1 = ends[depth][0];
        uint32_t member_first2 = firsts[depth][1];
        uint32_t member_end2 = ends[depth][1];
        uint32_t element_first1 = member_first1;
        uint32_t element_end1 = member_end1;
        uint32_t element_first2 = member_first2;
        uint32_t element_end2 = member_end2;
        const uint32_t one = check_input_fallthrough_named(reader, member_first1, member_end1);
        const uint32_t two = check_input_fallthrough_named(reader, member_first2, member_end2);
        const uint32_t member1 = check_input_fallthrough_member(parser, &member_first1, &member_end1);
        const uint32_t member2 = check_input_fallthrough_member(parser, &member_first2, &member_end2);
        const uint32_t open1 = check_input_fallthrough_subscript(parser, &element_first1, &element_end1);
        const uint32_t open2 = check_input_fallthrough_subscript(parser, &element_first2, &element_end2);
        if (one < count || two < count)
        {
            same = one == two;
        }
        else if (mode != 2 && member1 < count && member2 < count && depth < 16)
        {
            same = parser->tokens.names[member1 + 1] == parser->tokens.names[member2 + 1];
            firsts[depth][0] = member_first1;
            ends[depth][0] = member1;
            firsts[depth][1] = member_first2;
            ends[depth][1] = member2;
            modes[depth] = 1;
            depth += 1;
        }
        else if (mode == 0 && open1 < count && open2 < count && depth + 2 <= 16)
        {
            // The bases, and the indices unless both are integer literals, which must have one value.
            uint64_t index1 = 0;
            uint64_t index2 = 0;
            const bool literals = check_input_fallthrough_index_literal(reader, open1 + 1, element_end1 - 1, &index1) &&
                                  check_input_fallthrough_index_literal(reader, open2 + 1, element_end2 - 1, &index2);
            same = !literals || index1 == index2;
            firsts[depth][0] = element_first1;
            ends[depth][0] = open1;
            firsts[depth][1] = element_first2;
            ends[depth][1] = open2;
            modes[depth] = 0;
            firsts[depth + 1][0] = open1 + 1;
            ends[depth + 1][0] = element_end1 - 1;
            firsts[depth + 1][1] = open2 + 1;
            ends[depth + 1][1] = element_end2 - 1;
            modes[depth + 1] = 0;
            depth += literals ? 1 : 2;
        }
        else
        {
            same = false;
        }
    }
    return same;
}

static bool check_input_fallthrough_same_operand(CheckInputReader* reader, uint32_t first1, uint32_t end1, uint32_t first2, uint32_t end2)
{
    return check_input_fallthrough_same_operand_converted(reader, first1, end1, first2, end2, true);
}

// Whether an expression has a boolean value, as clang's isKnownToHaveBooleanValue takes it here: past its parentheses, a
// `!`, a comparison or a logical operator.
static bool check_input_fallthrough_boolean(const Parser* parser, uint32_t first, uint32_t end)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t root = check_input_fallthrough_ungroup(parser, &first, &end);
    const uint8_t role = root < count ? parser->tree.roles[root] : PARSE_ROLE_NONE;
    return (root == first && root + 1 < end && role == PARSE_ROLE_PREFIX && parser->tokens.kinds[root] == PARSE_KIND_EXCLAIM) || role == PARSE_ROLE_RELATIONAL ||
           role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR;
}

// What a comparison's operand is to clang's CFG builder's tryTransformToLiteralConstant, which finds its tautology
// checks' constants (CHECK_FALLTHROUGH_*): none, a literal (an integer literal or a character or floating constant, or
// one that `-` takes, past parentheses), or an enumerator's name (past parentheses).
enum
{
    CHECK_FALLTHROUGH_NONE,
    CHECK_FALLTHROUGH_LITERAL,
    CHECK_FALLTHROUGH_ENUMERATOR,
};

// A comparison's operand as a constant (CHECK_FALLTHROUGH_*), its value evaluated in its own type; `*grouped` whether
// parentheses enclose it (a literal's implicit conversion then hides it from clang, its cast around the parentheses) and
// `*declaration` an enumerator's.
static uint8_t check_input_fallthrough_constant(CheckInputReader* reader, uint32_t first, uint32_t end, CheckValue* value, bool* grouped, uint32_t* declaration)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t outer = first;
    const uint32_t root = check_input_fallthrough_ungroup(parser, &first, &end);
    const bool negated = root == first && root + 2 == end && parser->tree.roles[root] == PARSE_ROLE_PREFIX && parser->tokens.kinds[root] == PARSE_KIND_MINUS;
    const uint32_t literal = negated ? first + 1 : first;
    const uint8_t kind = literal < count ? parser->tokens.kinds[literal] : 0;
    const uint32_t named = first + 1 == end && kind == PARSE_KIND_IDENTIFIER ? reader->output->declarations[first] : BUSTER_CHECK_NONE;
    const bool enumerator = named < count && reader->declaration_kinds[named] == C_BINDING_ENUMERATOR;
    const bool constant = literal + 1 == end && (kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER || enumerator) && reader->pp != NULL;
    *value = (CheckValue){ 0 };
    const bool evaluated = constant && check_input_evaluate_cached(reader, first, end, value) &&
                           (value->constant_kind == CHECK_CONSTANT_INTEGER || (value->constant_kind == CHECK_CONSTANT_FLOATING && !enumerator));
    *grouped = first != outer;
    *declaration = enumerator ? named : BUSTER_CHECK_NONE;
    return !evaluated ? CHECK_FALLTHROUGH_NONE : enumerator ? CHECK_FALLTHROUGH_ENUMERATOR : CHECK_FALLTHROUGH_LITERAL;
}

// Whether two enumerators are of one enumeration (clang's areExprTypesCompatible): their declarations in one list, no
// brace closing it between them, nor either inside a nested one.
static bool check_input_fallthrough_same_enum(const CheckInputReader* reader, uint32_t one, uint32_t two)
{
    const Parser* parser = reader->parser;
    const uint32_t low = one < two ? one : two;
    const uint32_t high = one < two ? two : one;
    int64_t depth = 0;
    bool same = true;
    for (uint32_t token = low; same && token < high; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        depth += kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_L_PAREN ? 1 : kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_R_PAREN ? -1 : 0;
        same = depth >= 0;
    }
    return same && depth == 0;
}

// The implicit conversions clang gives a comparison's operand of type `type` past its lvalue's read, to `common`, the
// comparison's type, as a code that two operands of one type share where their conversions' kinds are the same: an
// integer promotion's IntegralCast, then the conversion's IntegralCast (merged into the promotion's, as clang's
// ImpCastExprToType merges casts of one kind), IntegralToFloating or FloatingCast. Expr::isSameComparisonOperand
// compares two operands past conversions of different kinds as names alone.
static uint32_t check_input_fallthrough_casts(TypeTable* types, TypeId type, TypeId common)
{
    const TypeId operand = type_unqualified(type_strip_aligned(types, type));
    const TypeId promoted = type_promote(types, operand);
    const bool floating = type_kind(types, promoted) == BUSTER_TYPE_KIND_FLOATING;
    const uint32_t inner = promoted != operand ? 1 : 0;
    const uint32_t outer = common == promoted ? 0 : type_kind(types, common) != BUSTER_TYPE_KIND_FLOATING ? 1 : floating ? 3 : 2;
    return inner == outer ? outer : inner | outer << 2;
}

// A value rounded to _Float16 (to nearest, ties to even, as APFloat rounds; past the largest finite value an infinity).
static long double check_input_fallthrough_half(long double value)
{
    return check_action_half_value(CHECK_HALF_IEEE, check_action_half_bits(CHECK_HALF_IEEE, value));
}

// One comparison of a tautology's: its variable's tokens, its operator as `variable OP constant` (flipped where the
// constant is on the left: tryNormalizeBinaryOperator), and its constant as clang's evaluator gives NumExpr: a literal's
// converted to the comparison's type (its implicit IntegralCast or IntegralToFloating, which a literal inside parentheses
// can't take, nor any other conversion), an enumerator's in its own type; an integer's bits below its width, its width
// and signedness, or a floating one's value and type. False where it isn't one.
typedef struct CheckFallthroughComparison CheckFallthroughComparison;
struct CheckFallthroughComparison
{
    uint32_t variable_first;
    uint32_t variable_end;
    uint32_t enumerator;
    uint32_t casts;
    uint32_t width;
    TypeId type;
    uint64_t bits;
    long double real;
    uint8_t kind;
    uint8_t constant;
    bool floating;
    bool is_signed;
};

static bool check_input_fallthrough_comparison(CheckInputReader* reader, uint32_t first, uint32_t end, CheckFallthroughComparison* comparison)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const uint32_t count = parser->tokens.count;
    const uint32_t root = check_input_fallthrough_ungroup(parser, &first, &end);
    const uint8_t role = root < count ? parser->tree.roles[root] : PARSE_ROLE_NONE;
    const bool comparison_root = (role == PARSE_ROLE_RELATIONAL || role == PARSE_ROLE_EQUALITY) && first < root && root + 1 < end;
    CheckValue left_value = { 0 };
    CheckValue right_value = { 0 };
    const bool typed = comparison_root && check_input_evaluate_cached(reader, first, root, &left_value) && check_input_evaluate_cached(reader, root + 1, end, &right_value) &&
                       left_value.type != 0 && right_value.type != 0;
    // The usual arithmetic conversions' type (0 for an operand that isn't arithmetic: a pointer), real: a complex
    // comparison's constants are no integers or floating values to clang's analysis.
    const TypeId converted = typed ? type_usual_arithmetic(types, type_unqualified(type_strip_aligned(types, left_value.type)),
                                                           type_unqualified(type_strip_aligned(types, right_value.type))) :
                                     0;
    const TypeId common = converted != 0 && type_kind(types, converted) != BUSTER_TYPE_KIND_COMPLEX ? converted : 0;
    const bool floating = common != 0 && type_kind(types, common) == BUSTER_TYPE_KIND_FLOATING;
    // The right operand first, then the left one.
    CheckValue constant = { 0 };
    bool grouped = false;
    uint32_t enumerator = BUSTER_CHECK_NONE;
    bool right = false;
    bool left = false;
    uint8_t found = CHECK_FALLTHROUGH_NONE;
    for (uint32_t side = 0; common != 0 && side < 2 && found == CHECK_FALLTHROUGH_NONE; side += 1)
    {
        found = check_input_fallthrough_constant(reader, side == 0 ? root + 1 : first, side == 0 ? end : root, &constant, &grouped, &enumerator);
        const TypeId own = type_unqualified(type_strip_aligned(types, constant.type));
        const bool cast = found == CHECK_FALLTHROUGH_LITERAL && own != common;
        const bool castable = constant.constant_kind == CHECK_CONSTANT_INTEGER && !grouped;
        found = cast && !castable ? CHECK_FALLTHROUGH_NONE : found;
        right = side == 0 && found != CHECK_FALLTHROUGH_NONE;
        left = side == 1 && found != CHECK_FALLTHROUGH_NONE;
    }
    uint8_t kind = comparison_root ? parser->tokens.kinds[root] : 0;
    if (left)
    {
        kind = kind == PARSE_KIND_GREATER ? PARSE_KIND_LESS : kind == PARSE_KIND_GREATEREQUAL ? PARSE_KIND_LESSEQUAL :
               kind == PARSE_KIND_LESS ? PARSE_KIND_GREATER : kind == PARSE_KIND_LESSEQUAL ? PARSE_KIND_GREATEREQUAL : kind;
    }
    // The constant's value: an enumerator's in its type, a literal's converted to the comparison's.
    const TypeId valued = found == CHECK_FALLTHROUGH_ENUMERATOR ? constant.type : common;
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = found != CHECK_FALLTHROUGH_NONE && !(found == CHECK_FALLTHROUGH_LITERAL && floating) &&
                         check_action_integer_type(types, valued, &width, &is_signed) && width <= 64;
    long double real = constant.floating;
    if (found == CHECK_FALLTHROUGH_LITERAL && floating && constant.constant_kind == CHECK_CONSTANT_INTEGER)
    {
        uint32_t literal_width = 0;
        bool literal_signed = false;
        (void)check_action_integer_type(types, constant.type, &literal_width, &literal_signed);
        // The literal's bits below its width, sign-extended where it is signed.
        const uint32_t shift = literal_width != 0 && literal_width <= 64 ? 64 - literal_width : 0;
        const uint64_t bits = (uint64_t)constant.integer << shift;
        const long double wide = literal_signed ? (long double)((int64_t)bits >> shift) : (long double)(bits >> shift);
        real = common == BUSTER_TYPE_FLOAT16 ? check_input_fallthrough_half(wide) : common == BUSTER_TYPE_FLOAT ? (long double)(float)wide :
               common == BUSTER_TYPE_DOUBLE ? (long double)(double)wide : wide;
        found = literal_width <= 64 ? found : CHECK_FALLTHROUGH_NONE;
    }
    const uint32_t variable_first = right ? first : root + 1;
    const uint32_t variable_end = right ? root : end;
    *comparison = (CheckFallthroughComparison){
        .variable_first = variable_first,
        .variable_end = variable_end,
        .enumerator = enumerator,
        .casts = common != 0 ? check_input_fallthrough_casts(types, right ? left_value.type : right_value.type, common) : 0,
        .width = width,
        .type = common,
        .bits = integer ? (uint64_t)constant.integer & (width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1) : 0,
        .real = real,
        .kind = kind,
        .constant = found,
        .floating = found == CHECK_FALLTHROUGH_LITERAL && floating,
        .is_signed = is_signed,
    };
    return found != CHECK_FALLTHROUGH_NONE && (integer || (found == CHECK_FALLTHROUGH_LITERAL && floating));
}

// A floating comparison of a value with a constant, by a comparison's token kind (APFloat's operators: an unordered
// one is false but for !=).
static bool check_input_fallthrough_compare_real(uint8_t kind, long double left, long double right)
{
    return kind == PARSE_KIND_EQUALEQUAL ? left == right : kind == PARSE_KIND_EXCLAIMEQUAL ? left != right : kind == PARSE_KIND_LESS ? left < right :
           kind == PARSE_KIND_LESSEQUAL ? left <= right : kind == PARSE_KIND_GREATER ? left > right : left >= right;
}

// checkIncorrectLogicOperator's floating case (`d > 5.0 && d < 3.0`): both constants in the wider of their types
// (_Float16, float, double or long double; __bf16's and __float128's are unknown), and the value the same at the smallest negative subnormal, each constant,
// their mean (computed in that type) and the largest finite value, as clang's APFloat values are; 1, 0 or 2 for unknown.
static uint32_t check_input_fallthrough_real_tautology(bool either, const CheckFallthroughComparison* one, const CheckFallthroughComparison* two)
{
    const uint32_t rank_one = one->type == BUSTER_TYPE_FLOAT16 ? 1 : one->type == BUSTER_TYPE_FLOAT ? 2 : one->type == BUSTER_TYPE_DOUBLE ? 3 :
                              one->type == BUSTER_TYPE_LONG_DOUBLE ? 4 : 0;
    const uint32_t rank_two = two->type == BUSTER_TYPE_FLOAT16 ? 1 : two->type == BUSTER_TYPE_FLOAT ? 2 : two->type == BUSTER_TYPE_DOUBLE ? 3 :
                              two->type == BUSTER_TYPE_LONG_DOUBLE ? 4 : 0;
    const uint32_t rank = rank_one > rank_two ? rank_one : rank_two;
    uint32_t result = 2;
    if (rank_one != 0 && rank_two != 0)
    {
        const long double first = one->real;
        const long double second = two->real;
        long double mean = 0;
        long double smallest = 0;
        long double largest = 0;
        if (rank == 1)
        {
            // Two _Float16 values' sum and half of one are exact in long double, and each is rounded once.
            mean = check_input_fallthrough_half(check_input_fallthrough_half(first + second) / 2.0L);
            smallest = -0x1p-24L;
            largest = 65504.0L;
        }
        else if (rank == 2)
        {
            const float sum = (float)first + (float)second;
            mean = sum / 2.0f;
            smallest = -0x1p-149L;
            largest = 0x1.fffffep127L;
        }
        else if (rank == 3)
        {
            const double sum = (double)first + (double)second;
            mean = sum / 2.0;
            smallest = -0x1p-1074L;
            largest = 0x1.fffffffffffffp1023L;
        }
        else
        {
            mean = (first + second) / 2.0L;
            smallest = -0x1p-16445L;
            largest = 0x1.fffffffffffffffep16383L;
        }
        const long double values[5] = { smallest, first, mean, second, largest };
        bool always_true = true;
        bool always_false = true;
        for (uint32_t i = 0; i < 5; i += 1)
        {
            const bool first_holds = check_input_fallthrough_compare_real(one->kind, values[i], first);
            const bool second_holds = check_input_fallthrough_compare_real(two->kind, values[i], second);
            const bool combined = either ? first_holds || second_holds : first_holds && second_holds;
            always_true &= combined;
            always_false &= !combined;
        }
        result = always_true ? 1 : always_false ? 0 : 2;
    }
    return result;
}

// A comparison of two values of a width and signedness (their bits below the width), by a comparison's token kind.
static bool check_input_fallthrough_compare(uint8_t kind, uint64_t left, uint64_t right, uint32_t width, bool is_signed)
{
    const uint32_t shift = 64 - width;
    const int64_t signed_left = (int64_t)(left << shift) >> shift;
    const int64_t signed_right = (int64_t)(right << shift) >> shift;
    const bool less = is_signed ? signed_left < signed_right : left < right;
    const bool equal = left == right;
    return kind == PARSE_KIND_EQUALEQUAL ? equal : kind == PARSE_KIND_EXCLAIMEQUAL ? !equal : kind == PARSE_KIND_LESS ? less :
           kind == PARSE_KIND_LESSEQUAL ? less || equal : kind == PARSE_KIND_GREATER ? !less && !equal : !less;
}

// clang's CFG builder's checkIncorrectLogicOperator, of a `&&` or `||` (`root`) whose operands' values are unknown: an
// operand's negation of the other (`x || !x`, true; `x && !x`, false), or two comparisons of the same variable with
// constants (`x > 5 && x < 3`, `s.m == A || s.m != B`, `d > 5.0 && d < 3.0`): literals or enumerators of one
// enumeration, both integers of one width and signedness, whose value is the same at the smallest value, each constant,
// the one past the smaller constant and the largest, or both floating (check_input_fallthrough_real_tautology); 1, 0 or 2
// for unknown.
static uint32_t check_input_fallthrough_logic_tautology(CheckInputReader* reader, uint32_t first, uint32_t root, uint32_t end)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool either = parser->tree.roles[root] == PARSE_ROLE_LOGICAL_OR;
    uint32_t left_first = first;
    uint32_t left_end = root;
    uint32_t right_first = root + 1;
    uint32_t right_end = end;
    const uint32_t left_root = check_input_fallthrough_ungroup(parser, &left_first, &left_end);
    const uint32_t right_root = check_input_fallthrough_ungroup(parser, &right_first, &right_end);
    const bool left_not = left_root == left_first && left_root + 1 < left_end && parser->tree.roles[left_root] == PARSE_ROLE_PREFIX &&
                          parser->tokens.kinds[left_root] == PARSE_KIND_EXCLAIM;
    const bool right_not = right_root == right_first && right_root + 1 < right_end && parser->tree.roles[right_root] == PARSE_ROLE_PREFIX &&
                           parser->tokens.kinds[right_root] == PARSE_KIND_EXCLAIM;
    uint32_t result = 2;
    CheckFallthroughComparison one = { 0 };
    CheckFallthroughComparison two = { 0 };
    // Two comparisons of the same operand with constants of one kind: literals, or enumerators of one enumeration.
    const bool compared = !left_not && !right_not && left_root < count && right_root < count &&
                          check_input_fallthrough_comparison(reader, left_first, left_end, &one) &&
                          check_input_fallthrough_comparison(reader, right_first, right_end, &two) &&
                          check_input_fallthrough_same_operand_converted(reader, one.variable_first, one.variable_end, two.variable_first, two.variable_end,
                                                                         one.casts == two.casts) &&
                          one.constant == two.constant &&
                          (one.constant != CHECK_FALLTHROUGH_ENUMERATOR || check_input_fallthrough_same_enum(reader, one.enumerator, two.enumerator));
    if ((left_not && check_input_fallthrough_same_operand(reader, left_first + 1, left_end, right_first, right_end)) ||
        (right_not && check_input_fallthrough_same_operand(reader, right_first + 1, right_end, left_first, left_end)))
    {
        result = either ? 1 : 0;
    }
    else if (compared && one.floating && two.floating)
    {
        result = check_input_fallthrough_real_tautology(either, &one, &two);
    }
    else if (compared && !one.floating && !two.floating && one.width == two.width && one.is_signed == two.is_signed)
    {
        const uint64_t literal_one = one.bits;
        const uint64_t literal_two = two.bits;
        const bool signed_one = one.is_signed;
        const uint32_t width = one.width;
        const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        const uint64_t smallest = signed_one ? (UINT64_C(1) << (width - 1)) & mask : 0;
        const uint64_t largest = signed_one ? mask >> 1 : mask;
        const uint64_t lower = check_input_fallthrough_compare(PARSE_KIND_LESS, literal_one, literal_two, width, signed_one) ? literal_one : literal_two;
        const uint64_t values[5] = { smallest, literal_one, (lower + 1) & mask, literal_two, largest };
        bool always_true = true;
        bool always_false = true;
        for (uint32_t i = 0; i < 5; i += 1)
        {
            const bool first_holds = check_input_fallthrough_compare(one.kind, values[i], literal_one, width, signed_one);
            const bool second_holds = check_input_fallthrough_compare(two.kind, values[i], literal_two, width, signed_one);
            const bool combined = either ? first_holds || second_holds : first_holds && second_holds;
            always_true &= combined;
            always_false &= !combined;
        }
        result = always_true ? 1 : always_false ? 0 : 2;
    }
    return result;
}

// clang's CFG builder's checks of one operand that isn't a logical operator, whose value its evaluator doesn't fold, past
// no parentheses (tryEvaluateBool's): `x & 0` and `x * 0` false; an equality of an integer literal (getIntegerLiteral-
// SubexpressionValue's) with a bitwise `&` or `|` of another that can't give it (`(x & 4) == 3`, checkIncorrectEquality-
// Operator), or with a boolean value it isn't (`(x < y) == 10`); a relational one of a boolean value and an integer
// literal other than 0 and 1 (checkIncorrectRelationalOperator); and a `|` of one literal constant that isn't 0
// (checkIncorrectBitwiseOrOperator). 1, 0 or 2 for unknown.
static uint32_t check_input_fallthrough_tautology(CheckInputReader* reader, uint32_t first, uint32_t root, uint32_t end)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint8_t role = root < count && first < root && root + 1 < end ? parser->tree.roles[root] : PARSE_ROLE_NONE;
    const uint8_t kind = role != PARSE_ROLE_NONE ? parser->tokens.kinds[root] : 0;
    uint32_t result = 2;
    if (role == PARSE_ROLE_BITWISE_AND || (role == PARSE_ROLE_MULTIPLICATIVE && kind == PARSE_KIND_STAR))
    {
        CheckValue value = { 0 };
        const bool left = check_input_evaluate_cached(reader, first, root, &value) && check_input_const_rvalue(reader, root, &value) &&
                          value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE && value.integer == 0;
        value = (CheckValue){ 0 };
        const bool right = !left && check_input_evaluate_cached(reader, root + 1, end, &value) && check_input_const_rvalue(reader, end, &value) &&
                           value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE && value.integer == 0;
        result = left || right ? 0 : 2;
    }
    else if (role == PARSE_ROLE_EQUALITY)
    {
        uint64_t literal = 0;
        uint32_t width = 0;
        const bool on_left = check_input_fallthrough_literal_value(reader, first, root, &literal, &width);
        const bool on_right = !on_left && check_input_fallthrough_literal_value(reader, root + 1, end, &literal, &width);
        uint32_t other_first = on_left ? root + 1 : first;
        uint32_t other_end = on_left ? end : root;
        const uint32_t bit_root = on_left || on_right ? check_input_fallthrough_ungroup(parser, &other_first, &other_end) : BUSTER_CHECK_NONE;
        const uint8_t bit_role = bit_root < count && other_first < bit_root && bit_root + 1 < other_end ? parser->tree.roles[bit_root] : PARSE_ROLE_NONE;
        if (bit_role == PARSE_ROLE_BITWISE_AND || bit_role == PARSE_ROLE_BITWISE_OR)
        {
            uint64_t mask = 0;
            uint32_t mask_width = 0;
            const bool known = check_input_fallthrough_literal_value(reader, other_first, bit_root, &mask, &mask_width) ||
                               check_input_fallthrough_literal_value(reader, bit_root + 1, other_end, &mask, &mask_width);
            const bool impossible = known && (bit_role == PARSE_ROLE_BITWISE_AND ? (mask & literal) != literal : (mask | literal) != literal);
            result = impossible ? kind != PARSE_KIND_EQUALEQUAL : 2;
        }
        else if ((on_left || on_right) && literal > 1 && check_input_fallthrough_boolean(parser, other_first, other_end))
        {
            result = kind != PARSE_KIND_EQUALEQUAL;
        }
    }
    else if (role == PARSE_ROLE_RELATIONAL)
    {
        uint32_t left_first = first;
        uint32_t left_end = root;
        uint32_t right_first = root + 1;
        uint32_t right_end = end;
        (void)check_input_fallthrough_ungroup(parser, &left_first, &left_end);
        (void)check_input_fallthrough_ungroup(parser, &right_first, &right_end);
        uint64_t literal = 0;
        uint32_t width = 0;
        bool is_signed = false;
        const bool literal_first = check_input_fallthrough_integer_literal(reader, left_first, left_end, &literal, &width, &is_signed);
        const bool literal_second = !literal_first && check_input_fallthrough_integer_literal(reader, right_first, right_end, &literal, &width, &is_signed);
        const bool boolean = literal_first ? check_input_fallthrough_boolean(parser, right_first, right_end) :
                             literal_second && check_input_fallthrough_boolean(parser, left_first, left_end);
        if (boolean && literal > 1)
        {
            // An integer literal is never negative (clang's IntLarger): 10 > bool and bool < 10 are true.
            const bool greater = kind == PARSE_KIND_GREATER || kind == PARSE_KIND_GREATEREQUAL;
            result = greater ? literal_first : !literal_first;
        }
    }
    else if (role == PARSE_ROLE_BITWISE_OR)
    {
        CheckValue value = { 0 };
        bool grouped = false;
        uint32_t enumerator = BUSTER_CHECK_NONE;
        const bool left = check_input_fallthrough_constant(reader, first, root, &value, &grouped, &enumerator) != CHECK_FALLTHROUGH_NONE;
        const bool right = check_input_fallthrough_constant(reader, root + 1, end, &value, &grouped, &enumerator) != CHECK_FALLTHROUGH_NONE;
        value = (CheckValue){ 0 };
        const bool known = left != right && check_input_evaluate_cached(reader, left ? first : root + 1, left ? root : end, &value) &&
                           value.constant_kind == CHECK_CONSTANT_INTEGER;
        result = known && value.integer != 0 ? 1 : 2;
    }
    return result;
}

// A control statement's condition, tokens `first` to `end`, as clang's CFG builder takes it, its edges to a branch that
// can't run pruned (tryEvaluateBool, #1126's review): 1 for true, 0 for false, 2 for unknown (or no condition: `for (;;)`'s
// is true, which its caller takes). Its && and || operators fold as clang's have it, each operand past its parentheses:
// a known left operand that decides (`0 && x`, `1 || x`) is the value; otherwise both known operands' value, or a known
// right one that decides (`x && 0`, `x || 1`), even past the left operand's side effects; any other operand is
// check_input_fallthrough_truth's; a `!` before an operand negates its known value (`!(x && 0)`, which clang's evaluator
// folds past the side effects). An operand its evaluator doesn't fold, inside no parentheses or `!` (where clang's
// tryEvaluateBool finds no binary operator, and its evaluator alone folds), takes clang's tautology checks of
// comparisons: a logical operator's of two unknown operands (`x > 5 && x < 3`, check_input_fallthrough_logic_tautology)
// and another's (`(x & 4) == 3`, check_input_fallthrough_tautology), where `tautologies` says so (tryEvaluateBool's;
// not __builtin_assume's, whose argument clang's evaluator alone folds). A do loop's condition is this; an if's, a
// while's and a for's are check_input_fallthrough_branches'.
// A stack of 16 operators takes the operands: no recursion (the call graph's rule), and a deeper one is unknown.
static uint32_t check_input_fallthrough_condition(CheckInputReader* reader, uint32_t first, uint32_t end, bool tautologies)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t firsts[16];
    uint32_t ends[16];
    uint32_t operators[16];
    uint32_t lefts[16];
    uint8_t phases[16];
    bool negations[16];
    // Whether a level's operand had no parentheses and no `!` around it, where clang's tautology checks apply.
    bool bares[16];
    uint32_t depth = first < end ? 1 : 0;
    firsts[0] = first;
    ends[0] = end;
    phases[0] = 0;
    negations[0] = false;
    bares[0] = tautologies;
    uint32_t result = 2;
    bool returned = false;
    while (depth != 0)
    {
        const uint32_t top = depth - 1;
        const bool either = phases[top] != 0 && parser->tree.roles[operators[top]] == PARSE_ROLE_LOGICAL_OR;
        if (!returned)
        {
            // The operand's range past its parentheses, then its operator or its value.
            // The operand's range past its parentheses and `!` operators, which negate its value.
            uint32_t a = firsts[top];
            uint32_t b = ends[top];
            uint32_t root = check_input_fallthrough_root(parser, a, b);
            bool stripping = true;
            while (stripping)
            {
                const bool group = root < count && root + 1 == b && parser->tree.roles[root] == PARSE_ROLE_CLOSE_GROUP && parser->tokens.kinds[a] == PARSE_KIND_L_PAREN &&
                                   parser->tree.roles[a] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[a] == root;
                const bool negation = root == a && root + 1 < b && parser->tokens.kinds[root] == PARSE_KIND_EXCLAIM && parser->tree.roles[root] == PARSE_ROLE_PREFIX;
                negations[top] ^= negation;
                bares[top] &= !group && !negation;
                a += group || negation ? 1 : 0;
                b -= group ? 1 : 0;
                stripping = group || negation;
                root = stripping ? check_input_fallthrough_root(parser, a, b) : root;
            }
            const bool logical = root < count && (parser->tree.roles[root] == PARSE_ROLE_LOGICAL_AND || parser->tree.roles[root] == PARSE_ROLE_LOGICAL_OR);
            if (logical && depth < 16 && a < root && root + 1 < b)
            {
                firsts[top] = a;
                ends[top] = b;
                operators[top] = root;
                phases[top] = 1;
                firsts[depth] = a;
                ends[depth] = root;
                phases[depth] = 0;
                negations[depth] = false;
                bares[depth] = bares[top];
                depth += 1;
            }
            else
            {
                const uint32_t truth = logical ? 2 : check_input_fallthrough_truth(reader, a, b);
                const uint32_t value = truth == 2 && !logical && bares[top] ? check_input_fallthrough_tautology(reader, a, root, b) : truth;
                result = value != 2 && negations[top] ? value ^ 1 : value;
                depth -= 1;
                returned = true;
            }
        }
        else if (phases[top] == 1)
        {
            // The left operand's value: it decides, or the right operand is taken.
            lefts[top] = result;
            if (result != 2 && (result == 1) == either)
            {
                result = negations[top] ? result ^ 1 : result;
                depth -= 1;
            }
            else
            {
                phases[top] = 2;
                firsts[depth] = operators[top] + 1;
                ends[depth] = ends[top];
                phases[depth] = 0;
                negations[depth] = false;
                bares[depth] = bares[top];
                depth += 1;
                returned = false;
            }
        }
        else
        {
            const uint32_t left = lefts[top];
            const uint32_t right = result;
            const uint32_t folded = left != 2 && right != 2 ? (either ? (left | right) : (left & right)) : left == 2 && right != 2 && (right == 1) == either ? right : 2;
            const uint32_t value = left == 2 && right == 2 && bares[top] ? check_input_fallthrough_logic_tautology(reader, firsts[top], operators[top], ends[top]) : folded;
            result = value != 2 && negations[top] ? value ^ 1 : value;
            depth -= 1;
        }
    }
    return result;
}

// Which branches of an if's, a while's or a for's condition can run, as clang's CFG builder builds a condition whose
// operator, past its parentheses, is && or || (VisitLogicalOperator): a block for each operand that isn't one itself,
// past its parentheses, whose edges tryEvaluateBool prunes (check_input_fallthrough_condition), a right operand's by the
// operator's own value where its own is unknown; any other condition is one block, by its value, its parentheses kept.
// 1 where only the true branch can run, 0 where only the false one can, 2 for both. A stack of 16 operators, as above;
// a deeper one is unknown.
static uint32_t check_input_fallthrough_branches(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t firsts[16];
    uint32_t ends[16];
    uint32_t operators[16];
    uint8_t phases[16];
    uint8_t lefts[16];
    uint32_t a = first;
    uint32_t b = end;
    const uint32_t root = first < end ? check_input_fallthrough_ungroup(parser, &a, &b) : BUSTER_CHECK_NONE;
    const bool logical = root < count && a < root && root + 1 < b &&
                         (parser->tree.roles[root] == PARSE_ROLE_LOGICAL_AND || parser->tree.roles[root] == PARSE_ROLE_LOGICAL_OR);
    // A branch set: bit 0 for true, bit 1 for false.
    uint32_t result = 3;
    if (!logical)
    {
        const uint32_t value = first < end ? check_input_fallthrough_condition(reader, first, end, true) : 2;
        result = value == 1 ? 1u : value == 0 ? 2u : 3u;
    }
    uint32_t depth = 0;
    if (logical)
    {
        firsts[0] = a;
        ends[0] = b;
        operators[0] = root;
        phases[0] = 0;
        depth = 1;
    }
    bool returned = false;
    while (depth != 0)
    {
        const uint32_t top = depth - 1;
        const bool either = parser->tree.roles[operators[top]] == PARSE_ROLE_LOGICAL_OR;
        if (returned)
        {
            // A child operator's branches, the left operand's or the right one's.
            returned = false;
        }
        else
        {
            // The next operand: past its parentheses, an operator of its own, or a block whose value prunes its edges.
            const bool right = phases[top] != 0;
            uint32_t c = right ? operators[top] + 1 : firsts[top];
            uint32_t d = right ? ends[top] : operators[top];
            const uint32_t inner = check_input_fallthrough_ungroup(parser, &c, &d);
            const bool nested = inner < count && c < inner && inner + 1 < d &&
                                (parser->tree.roles[inner] == PARSE_ROLE_LOGICAL_AND || parser->tree.roles[inner] == PARSE_ROLE_LOGICAL_OR);
            if (nested && depth < 16)
            {
                firsts[depth] = c;
                ends[depth] = d;
                operators[depth] = inner;
                phases[depth] = 0;
                depth += 1;
                continue;
            }
            uint32_t value = nested ? 2 : check_input_fallthrough_condition(reader, c, d, true);
            if (value == 2 && right && !nested)
            {
                value = check_input_fallthrough_condition(reader, firsts[top], ends[top], true);
            }
            result = value == 1 ? 1u : value == 0 ? 2u : 3u;
        }
        if (phases[top] == 0)
        {
            lefts[top] = (uint8_t)result;
            phases[top] = 1;
        }
        else
        {
            const bool left_true = (lefts[top] & 1) != 0;
            const bool left_false = (lefts[top] & 2) != 0;
            const bool right_true = (result & 1) != 0;
            const bool right_false = (result & 2) != 0;
            const bool can_true = either ? left_true || (left_false && right_true) : left_true && right_true;
            const bool can_false = either ? left_false && right_false : left_false || (left_true && right_false);
            result = (can_true ? 1u : 0u) | (can_false ? 2u : 0u);
            depth -= 1;
            returned = depth != 0;
        }
    }
    return result == 1 ? 1 : result == 2 ? 0 : 2;
}

// Whether `token` lies in the subtree of `root`, before it or after it (a branch after an if's parentheses).
static bool check_input_fallthrough_inside(const Parser* parser, uint32_t token, uint32_t root)
{
    uint32_t at = token;
    while (at < parser->tokens.count && at != root)
    {
        at = parser->tree.parents[at];
    }
    return at == root;
}

// Whether a loop's body (`body`, its top token) has a break of its own that can run: one not inside a nested loop or
// switch, nor in an if's branch that its condition, folded as clang's CFG builder folds it, never takes (`if (0) break;`,
// #1126's review), whose block clang's CFG prunes. The body's tokens run from its first to its subtree's last, past its
// top token where the body is an if.
static bool check_input_fallthrough_breaks(CheckInputReader* reader, uint32_t body)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    bool breaks = false;
    uint32_t token = body < count ? check_input_fallthrough_first(reader, body) : count;
    while (token < count && !breaks && (token <= body || check_input_fallthrough_inside(parser, token, body)))
    {
        bool own = parser->tokens.kinds[token] == PARSE_KIND_BREAK;
        bool live = own;
        bool above = false;
        uint32_t child = token;
        for (uint32_t at = parser->tree.parents[token]; own && live && !above && at < count; at = parser->tree.parents[at])
        {
            const uint32_t keyword = parser->tree.roles[at] == PARSE_ROLE_CLOSE_CONDITION || parser->tree.roles[at] == PARSE_ROLE_CLOSE_FOR ? reader->first_child[at] :
                                                                                                                                          BUSTER_CHECK_NONE;
            const uint8_t kind = keyword < count ? parser->tokens.kinds[keyword] : 0;
            own = kind != PARSE_KIND_WHILE && kind != PARSE_KIND_FOR && kind != PARSE_KIND_SWITCH && parser->tokens.kinds[at] != PARSE_KIND_DO;
            // An if's then-branch (the child after its parentheses) or else-branch (its `else` token's).
            const bool otherwise = kind == PARSE_KIND_IF && parser->tokens.kinds[child] == PARSE_KIND_ELSE;
            const bool branch = kind == PARSE_KIND_IF && child > at && !otherwise;
            if (branch || otherwise)
            {
                uint32_t open = BUSTER_CHECK_NONE;
                for (uint32_t item = reader->first_child[at]; item != BUSTER_CHECK_NONE; item = reader->next_child[item])
                {
                    open = parser->tree.roles[item] == PARSE_ROLE_OPEN_CONDITION ? item : open;
                }
                const uint32_t condition = open != BUSTER_CHECK_NONE ? check_input_fallthrough_branches(reader, open + 1, at) : 2;
                live = !(branch && condition == 0) && !(otherwise && condition == 1);
            }
            above = at == body;
            child = at;
        }
        breaks = own && live;
        token += 1;
    }
    return breaks;
}

// Whether a call statement's callee is __builtin_unreachable or __builtin_trap, after which nothing runs.
static bool check_input_fallthrough_trap(const CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    uint8_t buffer[32];
    const uint8_t* bytes = NULL;
    const bool identifier = first < parser->tokens.count && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER && reader->pp != NULL;
    const uint32_t length = identifier ? check_input_fallthrough_spelling(reader, first, buffer, sizeof(buffer), &bytes) : 0;
    return (length == 21 && memcmp(bytes, "__builtin_unreachable", 21) == 0) || (length == 14 && memcmp(bytes, "__builtin_trap", 14) == 0);
}

// A call's callee, the call `close` (its closing parenthesis): the identifier before its opening parenthesis, past
// parentheses, `&` and `*` (`die`, `(die)`, `(&die)`) and an element's subscript (`table[i]`, whose object's
// declaration spells its elements' type), or a cast's closing parenthesis (`((N)p)`, whose type name spells the
// callee's type), or BUSTER_CHECK_NONE.
static uint32_t check_input_fallthrough_callee(const CheckInputReader* reader, uint32_t close)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t callee = BUSTER_CHECK_NONE;
    uint32_t previous = BUSTER_CHECK_NONE;
    bool found = false;
    for (uint32_t child = reader->first_child[close]; child != BUSTER_CHECK_NONE && !found; child = reader->next_child[child])
    {
        found = parser->tree.roles[child] == PARSE_ROLE_OPEN_CALL;
        callee = found ? previous : callee;
        previous = child;
    }
    bool stripping = callee < count;
    while (stripping)
    {
        const uint8_t role = parser->tree.roles[callee];
        const uint8_t kind = parser->tokens.kinds[callee];
        uint32_t inner = BUSTER_CHECK_NONE;
        if (role == PARSE_ROLE_CLOSE_GROUP || (role == PARSE_ROLE_PREFIX && (kind == PARSE_KIND_AMP || kind == PARSE_KIND_STAR)))
        {
            // The group's expression, its child but its opening parenthesis, or the operator's operand.
            uint32_t children = 0;
            for (uint32_t child = reader->first_child[callee]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                const bool operand = parser->tree.roles[child] != PARSE_ROLE_OPEN_GROUP;
                inner = operand ? child : inner;
                children += operand ? 1 : 0;
            }
            inner = children == 1 ? inner : BUSTER_CHECK_NONE;
        }
        else if (role == PARSE_ROLE_CLOSE_SUBSCRIPT)
        {
            // The subscripted operand: its child before the `[`.
            uint32_t open = BUSTER_CHECK_NONE;
            for (uint32_t child = reader->first_child[callee]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                open = parser->tree.roles[child] == PARSE_ROLE_OPEN_SUBSCRIPT ? child : open;
            }
            for (uint32_t child = reader->first_child[callee]; open < count && child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                inner = child < open && (inner >= count || child > inner) ? child : inner;
            }
        }
        stripping = inner < count;
        callee = stripping ? inner : callee;
    }
    const bool named = callee < count && (parser->tokens.kinds[callee] == PARSE_KIND_IDENTIFIER || parser->tree.roles[callee] == PARSE_ROLE_CLOSE_CAST);
    return named ? callee : BUSTER_CHECK_NONE;
}

// Whether a function's declaration holds GNU's noreturn attribute (`__attribute__((noreturn))`, `[[gnu::noreturn]]`),
// which clang makes its type's: not the _Noreturn keyword, nor C23's `[[noreturn]]` or `[[_Noreturn]]`, its declaration's.
static bool check_input_fallthrough_noreturn_type(const CheckInputAttributes* attributes)
{
    bool noreturn = false;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = &attributes->records[i];
        noreturn |= attribute->kind == CHECK_ATTRIBUTE_NORETURN && !attribute->ignored && !attribute->keyword &&
                    (!attribute->standard || attribute->name_token != attribute->first);
    }
    return noreturn;
}

// Whether an object's (a variable's or a parameter's) function type is noreturn, as clang's CFG reads a call through it
// (CFGBuilder::VisitCallExpr: the callee's type's noreturn): GNU's noreturn attribute, a type's in clang, on its own
// declaration (`void (*p)(void) __attribute__((noreturn))`), or on the typedef its specifiers name, through the typedefs
// that typedef's name in turn (`typedef void (*N)(void) __attribute__((noreturn)); N p;`), or through a typeof of a name
// its specifiers spell (#1844): of a function its GNU attribute marks (`__typeof__(die) *p`, `__typeof__(&die) p`; C11's
// _Noreturn is its declaration's, not its type's), or of an object or a typedef, whose own walk goes on; a bounded walk.
static bool check_input_fallthrough_noreturn_object(const CheckInputReader* reader, uint32_t declaration)
{
    const CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    bool noreturn = false;
    uint32_t at = declaration;
    for (uint32_t steps = 0; !noreturn && at < count && at != 0 && steps < 16; steps += 1)
    {
        noreturn = check_input_noreturn_attribute(check_input_attributes_at(input, at));
        uint32_t next = check_input_typedef_spelling(input, parser, at);
        const uint32_t keyword = !noreturn && next == 0 ? check_input_typeof_spelling(input, parser, at) : 0;
        const uint32_t close = keyword != 0 && keyword + 1 < count ? parser->tree.parents[keyword + 1] : count;
        // The typeof's operand: a name, or `&` and a name.
        const uint32_t name = close < count && keyword + 3 == close ? keyword + 2 :
                              close < count && keyword + 4 == close && parser->tokens.kinds[keyword + 2] == PARSE_KIND_AMP ? keyword + 3 : count;
        const uint32_t named = name < count && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER && input->declarations != NULL ? input->declarations[name] : 0;
        const bool function = named != 0 && named < count && reader->declaration_kinds[named] == C_BINDING_FUNCTION;
        noreturn = noreturn || (function && check_input_fallthrough_noreturn_type(check_input_attributes_at(input, named)));
        next = next != 0 ? next : named != 0 && named < count && !function ? named : 0;
        at = next;
    }
    return noreturn;
}

// Whether call `close` never returns, as clang's CFG ends its block there (CFGBuilder::VisitCallExpr): its callee names a
// function declared noreturn (CHECK_INPUT_FUNCTION_NORETURN, which a redeclaration inherits), a builtin clang's table marks
// noreturn (`r`: __builtin_abort, __builtin_unreachable, __builtin_trap), or a library function it marks so (abort, exit,
// _Exit) declared with external linkage where the library's builtins are on, as clang's declaration merges the builtin's
// noreturn type; a call through an object whose function type is noreturn (check_input_fallthrough_noreturn_object); or
// it is __builtin_assume of a false condition (isBuiltinAssumeFalse).
static bool check_input_fallthrough_noreturn(CheckInputReader* reader, uint32_t close)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    // A function's declarator (`void die(void);` in a block) declares its name, which calls nothing.
    const uint32_t named = check_input_fallthrough_callee(reader, close);
    const uint32_t callee = named < count && reader->output->declarations[named] != named ? named : BUSTER_CHECK_NONE;
    const uint32_t declaration = callee < count ? reader->output->declarations[callee] : BUSTER_CHECK_NONE;
    const bool function = declaration < count && reader->declaration_kinds[declaration] == C_BINDING_FUNCTION;
    const bool declared = function && reader->output->function_flags != NULL && (reader->output->function_flags[declaration] & CHECK_INPUT_FUNCTION_NORETURN) != 0;
    const uint32_t name = callee < count && reader->pp != NULL ? parser->tokens.names[callee] : 0;
    const BuiltinIntrinsicInfo* info = name != 0 && !declared ? check_input_implicit_info(reader, name) : NULL;
    const bool marked = info != NULL && memchr(info->attributes, 'r', info->attributes_length) != NULL;
    const bool library = info != NULL && memchr(info->attributes, 'f', info->attributes_length) != NULL;
    const bool builtin = marked && (!library || (function && check_input_linkage(reader->declaration_bits, declaration) == 2 &&
                                                 check_input_library_enabled(info, &reader->pp->options)));
    // A cast's type name spells the callee's type: its typedef's walk (`((N)p)()`).
    const bool cast = callee < count && parser->tree.roles[callee] == PARSE_ROLE_CLOSE_CAST;
    const uint32_t cast_type = cast ? check_input_token_type_spelling(reader->output, parser, callee, true) : 0;
    bool noreturn = declared || builtin || check_input_fallthrough_trap(reader, callee) ||
                    (!cast && declaration < count && !function && check_input_fallthrough_noreturn_object(reader, declaration)) ||
                    (cast_type != 0 && check_input_fallthrough_noreturn_object(reader, cast_type));
    if (!noreturn && callee < count && declaration >= count && reader->pp != NULL)
    {
        uint8_t buffer[32];
        const uint8_t* bytes = NULL;
        const uint32_t length = check_input_fallthrough_spelling(reader, callee, buffer, sizeof(buffer), &bytes);
        uint32_t open = BUSTER_CHECK_NONE;
        for (uint32_t child = reader->first_child[close]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            open = parser->tree.roles[child] == PARSE_ROLE_OPEN_CALL ? child : open;
        }
        noreturn = length == 16 && memcmp(bytes, "__builtin_assume", 16) == 0 && open < close && check_input_fallthrough_condition(reader, open + 1, close, false) == 0;
    }
    return noreturn;
}

// Whether call `close` runs whenever statement `statement` (its `;`) does, as clang's CFG has it in the statement's block:
// nothing between them but parentheses, casts, unary operators that evaluate their operand, binary and assignment
// operators, commas, subscripts, member accesses, another call that isn't a builtin's but expect's (as its callee or an
// argument), the left operand of && or || and a conditional's condition, an initializer list's elements, and a
// declaration's specifiers over its initializers and array bounds. sizeof's, typeof's, a statement expression's, another
// builtin call's (__builtin_choose_expr's arms, __builtin_constant_p's operand) and a function declarator's parameters
// aren't taken.
static bool check_input_fallthrough_evaluated(const CheckInputReader* reader, uint32_t close, uint32_t statement)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t child = close;
    uint32_t at = parser->tree.parents[close];
    bool evaluated = true;
    while (evaluated && at != statement)
    {
        evaluated = at < count;
        const uint8_t role = evaluated ? parser->tree.roles[at] : PARSE_ROLE_NONE;
        const uint8_t kind = evaluated ? parser->tokens.kinds[at] : 0;
        if (role == PARSE_ROLE_CLOSE_TERNARY)
        {
            // The condition is the child before the `?`.
            uint32_t question = BUSTER_CHECK_NONE;
            for (uint32_t item = reader->first_child[at]; item != BUSTER_CHECK_NONE; item = reader->next_child[item])
            {
                question = parser->tree.roles[item] == PARSE_ROLE_OPEN_TERNARY ? item : question;
            }
            evaluated = question < count && child < question;
        }
        else if (role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR)
        {
            evaluated = child < at;
        }
        else if (role == PARSE_ROLE_PREFIX)
        {
            evaluated = kind == PARSE_KIND_AMP || kind == PARSE_KIND_STAR || kind == PARSE_KIND_MINUS || kind == PARSE_KIND_PLUS || kind == PARSE_KIND_TILDE ||
                        kind == PARSE_KIND_EXCLAIM || kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS;
        }
        else if (role == PARSE_ROLE_CLOSE_CALL)
        {
            // A call's, but a builtin's other than expect's (whose first operand runs), and a declarator's parameters.
            const uint16_t builtin = reader->builtins != NULL ? reader->builtins[at] : 0;
            const uint32_t named = check_input_fallthrough_callee(reader, at);
            evaluated = (builtin == 0 || builtin == CBUILTIN_EXPECT || builtin == CBUILTIN_EXPECT_WITH_PROBABILITY) &&
                        (named >= count || reader->output->declarations[named] != named);
        }
        else
        {
            evaluated = role == PARSE_ROLE_CLOSE_GROUP || role == PARSE_ROLE_CLOSE_CAST || role == PARSE_ROLE_MULTIPLICATIVE || role == PARSE_ROLE_ADDITIVE ||
                        role == PARSE_ROLE_SHIFT || role == PARSE_ROLE_RELATIONAL || role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_BITWISE_AND ||
                        role == PARSE_ROLE_BITWISE_XOR || role == PARSE_ROLE_BITWISE_OR || role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_COMMA ||
                        role == PARSE_ROLE_SEPARATOR || role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_MEMBER_OPERATOR || role == PARSE_ROLE_POSTFIX ||
                        role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_CLOSE_INITIALIZER;
        }
        child = at;
        at = evaluated ? parser->tree.parents[at] : at;
    }
    return evaluated;
}

// Whether statement `statement` (its `;`, from its first token `first`) calls a function that never returns
// (check_input_fallthrough_noreturn) wherever it runs (check_input_fallthrough_evaluated).
static bool check_input_fallthrough_stops(CheckInputReader* reader, uint32_t first, uint32_t statement)
{
    const Parser* parser = reader->parser;
    bool stops = false;
    for (uint32_t token = first; token < statement && !stops; token += 1)
    {
        stops = parser->tree.roles[token] == PARSE_ROLE_CLOSE_CALL && check_input_fallthrough_noreturn(reader, token) &&
                check_input_fallthrough_evaluated(reader, token, statement);
    }
    return stops;
}

// Whether statement `item` never completes, as clang's CFG has no edge out of it (#1126's review): a return, break,
// continue or goto; a statement that calls a function that never returns wherever it runs (check_input_fallthrough_stops);
// a while, do or for loop whose condition is true (check_input_fallthrough_condition, or a for's absent one) without a
// break of its own; an if, a switch, a while or a for whose condition (or a for's first clause) calls one that never
// returns, and a do whose condition does, without a break of its own; an if whose known condition's branch never completes, or one whose two branches don't; a block whose
// last statement doesn't (but a label's, which another jump may reach); and a labeled statement whose statement doesn't.
// A stack of pending statements takes the branches: no recursion (the call graph's rule).
static bool check_input_fallthrough_leaves(CheckInputReader* reader, uint32_t item)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t pending[16];
    uint32_t pending_count = 1;
    pending[0] = item;
    bool leaves = item < count;
    while (leaves && pending_count != 0)
    {
        const uint32_t node = pending[--pending_count];
        const uint8_t role = parser->tree.roles[node];
        const uint32_t first = check_input_fallthrough_first(reader, node);
        const uint8_t first_kind = parser->tokens.kinds[first];
        const uint32_t keyword = role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR ? reader->first_child[node] : BUSTER_CHECK_NONE;
        const uint8_t keyword_kind = keyword < count ? parser->tokens.kinds[keyword] : 0;
        const bool statement = parser->tokens.kinds[node] == PARSE_KIND_SEMI;
        if (statement && (first_kind == PARSE_KIND_RETURN || first_kind == PARSE_KIND_GOTO || first_kind == PARSE_KIND_BREAK || first_kind == PARSE_KIND_CONTINUE))
        {
            // It leaves.
        }
        else if (statement && first_kind == PARSE_KIND_DO)
        {
            // do body while (condition);: the condition's parentheses are the `;`'s child, the body the do's.
            uint32_t condition = BUSTER_CHECK_NONE;
            for (uint32_t child = reader->first_child[node]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                condition = parser->tree.roles[child] == PARSE_ROLE_CLOSE_CONDITION ? child : condition;
            }
            uint32_t open = BUSTER_CHECK_NONE;
            for (uint32_t child = condition != BUSTER_CHECK_NONE ? reader->first_child[condition] : BUSTER_CHECK_NONE; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                open = parser->tree.roles[child] == PARSE_ROLE_OPEN_CONDITION ? child : open;
            }
            const uint32_t body = check_input_fallthrough_labeled(reader, first);
            // A condition that is true, or that calls a function that never returns, which every iteration that doesn't
            // break reaches.
            leaves = open != BUSTER_CHECK_NONE && body != BUSTER_CHECK_NONE && !check_input_fallthrough_breaks(reader, body) &&
                     (check_input_fallthrough_stops(reader, open + 1, condition) || check_input_fallthrough_condition(reader, open + 1, condition, true) == 1);
        }
        else if (statement && check_input_fallthrough_stops(reader, first, node))
        {
            // It calls a function that never returns.
        }
        else if (role == PARSE_ROLE_LABEL || role == PARSE_ROLE_CLOSE_CASE)
        {
            const uint32_t labeled = check_input_fallthrough_labeled(reader, node);
            leaves = labeled != BUSTER_CHECK_NONE;
            pending[pending_count] = labeled;
            pending_count += leaves ? 1 : 0;
        }
        else if (role == PARSE_ROLE_CLOSE_BLOCK)
        {
            // Its last statement that isn't a null statement.
            uint32_t last = BUSTER_CHECK_NONE;
            for (uint32_t child = reader->first_child[node]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                const bool empty = parser->tokens.kinds[child] == PARSE_KIND_SEMI && (child == 0 || !check_input_fallthrough_within(parser, child - 1, child));
                last = parser->tree.roles[child] != PARSE_ROLE_OPEN_BLOCK && !empty ? child : last;
            }
            // A label at the end lets a jump reach the block's end.
            leaves = last != BUSTER_CHECK_NONE && parser->tree.roles[last] != PARSE_ROLE_LABEL && parser->tree.roles[last] != PARSE_ROLE_CLOSE_CASE;
            pending[pending_count] = last;
            pending_count += leaves ? 1 : 0;
        }
        else if (keyword_kind == PARSE_KIND_IF)
        {
            uint32_t open = BUSTER_CHECK_NONE;
            uint32_t branch = BUSTER_CHECK_NONE;
            uint32_t otherwise = BUSTER_CHECK_NONE;
            for (uint32_t child = reader->first_child[node]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                open = parser->tree.roles[child] == PARSE_ROLE_OPEN_CONDITION ? child : open;
                branch = branch == BUSTER_CHECK_NONE && child > node && parser->tokens.kinds[child] != PARSE_KIND_ELSE ? child : branch;
                otherwise = parser->tokens.kinds[child] == PARSE_KIND_ELSE ? check_input_fallthrough_labeled(reader, child) : otherwise;
            }
            // A condition that calls a function that never returns leaves before either branch.
            const bool stops = open != BUSTER_CHECK_NONE && check_input_fallthrough_stops(reader, open + 1, node);
            const uint32_t condition = open != BUSTER_CHECK_NONE && !stops ? check_input_fallthrough_branches(reader, open + 1, node) : 2;
            const bool both = otherwise != BUSTER_CHECK_NONE && condition == 2;
            const uint32_t taken = condition == 1 ? branch : condition == 0 ? otherwise : BUSTER_CHECK_NONE;
            leaves = stops || (branch != BUSTER_CHECK_NONE && (both || taken != BUSTER_CHECK_NONE) && pending_count + 2 <= 16);
            if (stops)
            {
                // Nothing more to take.
            }
            else if (leaves && both)
            {
                pending[pending_count++] = branch;
                pending[pending_count++] = otherwise;
            }
            else if (leaves)
            {
                pending[pending_count++] = taken;
            }
        }
        else if (keyword_kind == PARSE_KIND_WHILE || keyword_kind == PARSE_KIND_FOR)
        {
            // The loop's parentheses' children: its condition (a while's tokens inside them, a for's second clause), then
            // the body after them.
            uint32_t open = BUSTER_CHECK_NONE;
            uint32_t body = BUSTER_CHECK_NONE;
            uint32_t clauses = 0;
            uint32_t initial = BUSTER_CHECK_NONE;
            uint32_t second = BUSTER_CHECK_NONE;
            for (uint32_t child = reader->first_child[node]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                open = parser->tree.roles[child] == PARSE_ROLE_OPEN_CONDITION || parser->tree.roles[child] == PARSE_ROLE_OPEN_FOR ? child : open;
                const bool clause = child < node && parser->tokens.kinds[child] == PARSE_KIND_SEMI && keyword_kind == PARSE_KIND_FOR;
                clauses += clause ? 1 : 0;
                initial = clause && clauses == 1 ? child : initial;
                second = clause && clauses == 2 ? child : second;
                body = child > node ? child : body;
            }
            // A for's condition is its second clause, the tokens between its two `;`: absent (true) when that `;` has
            // nothing under it.
            const bool absent = second != BUSTER_CHECK_NONE && (second == 0 || !check_input_fallthrough_within(parser, second - 1, second));
            const bool forever = keyword_kind == PARSE_KIND_FOR ?
                                     absent || (initial < second && second != BUSTER_CHECK_NONE && check_input_fallthrough_branches(reader, initial + 1, second) == 1) :
                                     open != BUSTER_CHECK_NONE && check_input_fallthrough_branches(reader, open + 1, node) == 1;
            // A loop's first evaluation, its condition's (a for's first clause and its condition), may call a function
            // that never returns.
            const bool stops = keyword_kind == PARSE_KIND_FOR ?
                                   (open != BUSTER_CHECK_NONE && initial != BUSTER_CHECK_NONE && check_input_fallthrough_stops(reader, open + 1, initial)) ||
                                       (initial != BUSTER_CHECK_NONE && second != BUSTER_CHECK_NONE && check_input_fallthrough_stops(reader, initial + 1, second)) :
                                   open != BUSTER_CHECK_NONE && check_input_fallthrough_stops(reader, open + 1, node);
            leaves = stops || (forever && body != BUSTER_CHECK_NONE && !check_input_fallthrough_breaks(reader, body));
        }
        else if (keyword_kind == PARSE_KIND_SWITCH)
        {
            // A switch whose condition calls a function that never returns.
            uint32_t open = BUSTER_CHECK_NONE;
            for (uint32_t child = reader->first_child[node]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
            {
                open = parser->tree.roles[child] == PARSE_ROLE_OPEN_CONDITION ? child : open;
            }
            leaves = open != BUSTER_CHECK_NONE && check_input_fallthrough_stops(reader, open + 1, node);
        }
        else
        {
            leaves = false;
        }
    }
    return leaves;
}

// Whether fallthrough null statement `token` can't run, as clang's CFG has it (#1126's review): a statement before it in
// its block, or in the blocks around it, never completes (check_input_fallthrough_leaves) with no label or case label
// after it, which would let control in again. A statement at a case label, or one that starts an if's or a loop's
// body, a labeled statement's or the function's, may run.
static bool check_input_fallthrough_dead(CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t statement = token;
    uint32_t state = 2;
    while (state == 2)
    {
        const uint32_t parent = statement < count ? parser->tree.parents[statement] : count;
        if (parent < count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_BLOCK)
        {
            // The block's statements before this one, in order: each label lets control in (live), each statement that
            // never completes makes what follows dead. The pass's fallthroughs come in source order, so each block's scan
            // resumes where the last query of it stopped (#1357): a scan is a prefix of the block's statements, each
            // classified once, and any prefix that ends before `statement` (or is empty) resumes; a query before the
            // kept prefix's end scans from the start and leaves the longer prefix kept.
            const bool cached = reader->fallthrough_block_next != NULL && reader->fallthrough_block_next[parent] != 0;
            const uint32_t cached_last = cached ? reader->fallthrough_block_last[parent] : BUSTER_CHECK_NONE;
            const bool resumable = cached && (cached_last == BUSTER_CHECK_NONE || cached_last < statement);
            const uint32_t next = resumable ? reader->fallthrough_block_next[parent] : 0;
            uint32_t child = !resumable ? reader->first_child[parent] : next == UINT32_MAX ? BUSTER_CHECK_NONE : next - 1;
            uint32_t last = resumable ? reader->fallthrough_block_last[parent] : BUSTER_CHECK_NONE;
            uint8_t block_state = resumable ? reader->fallthrough_block_state[parent] : 2;
            for (; child != BUSTER_CHECK_NONE && child < statement; child = reader->next_child[child])
            {
                uint32_t inner = child;
                bool labeled = false;
                while (inner != BUSTER_CHECK_NONE && (parser->tree.roles[inner] == PARSE_ROLE_LABEL || parser->tree.roles[inner] == PARSE_ROLE_CLOSE_CASE))
                {
                    labeled = true;
                    inner = check_input_fallthrough_labeled(reader, inner);
                }
                const bool leaves = parser->tree.roles[child] != PARSE_ROLE_OPEN_BLOCK && check_input_fallthrough_leaves(reader, child);
                block_state = leaves ? 0 : labeled ? 1 : block_state;
                last = child;
            }
            if (reader->fallthrough_block_next != NULL && (!cached || resumable))
            {
                reader->fallthrough_block_next[parent] = child == BUSTER_CHECK_NONE ? UINT32_MAX : child + 1;
                reader->fallthrough_block_last[parent] = last;
                reader->fallthrough_block_state[parent] = block_state;
            }
            state = block_state != 2 ? block_state : state;
            statement = parent;
        }
        else
        {
            // A function's body, a case's or a label's statement, or a control statement's body: it may run.
            state = 1;
        }
    }
    return state == 0;
}

// Whether statement `item` (its top token) adds no CFG block's end of its own after a fallthrough that can't run, as clang
// builds the block (#1126's review): no conditional operator, `&&` or `||` (a label's address excepted), no jump, loop,
// if, switch, label or case label, no other fallthrough (the block's last is the one clang takes) and no call of a
// function that never returns (check_input_fallthrough_noreturn), anywhere in it; a block or a statement expression of
// such statements is one.
static bool check_input_fallthrough_straight(CheckInputReader* reader, uint32_t item)
{
    const Parser* parser = reader->parser;
    bool straight = item < parser->tokens.count && (parser->tokens.kinds[item] == PARSE_KIND_SEMI || parser->tree.roles[item] == PARSE_ROLE_CLOSE_BLOCK);
    for (uint32_t at = straight ? check_input_fallthrough_first(reader, item) : item; straight && at <= item; at += 1)
    {
        const uint8_t kind = parser->tokens.kinds[at];
        const uint8_t role = parser->tree.roles[at];
        straight = kind != PARSE_KIND_QUESTION && kind != PARSE_KIND_PIPEPIPE && (kind != PARSE_KIND_AMPAMP || role == PARSE_ROLE_PREFIX) &&
                   kind != PARSE_KIND_RETURN && kind != PARSE_KIND_GOTO && kind != PARSE_KIND_BREAK && kind != PARSE_KIND_CONTINUE &&
                   kind != PARSE_KIND_DO && kind != PARSE_KIND_IF && kind != PARSE_KIND_WHILE && kind != PARSE_KIND_FOR && kind != PARSE_KIND_SWITCH &&
                   role != PARSE_ROLE_LABEL && role != PARSE_ROLE_CLOSE_CASE && (kind != PARSE_KIND_SEMI || check_input_attributes_at(reader->output, at) == 0) &&
                   (role != PARSE_ROLE_CLOSE_CALL || !check_input_fallthrough_noreturn(reader, at));
    }
    return straight;
}

// Whether a goto (or a label's address) names plain label `label`, anywhere in the unit.
static bool check_input_fallthrough_targeted(const CheckInputReader* reader, uint32_t label)
{
    const Parser* parser = reader->parser;
    bool targeted = false;
    for (uint32_t token = 0; token < parser->tokens.count && !targeted; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        targeted = (kind == PARSE_KIND_GOTO || kind == PARSE_KIND_AMPAMP) && check_sparse_get(reader->output->label_targets, token) == label;
    }
    return targeted;
}

// Whether the code after a fallthrough that can't run (`token`, check_input_fallthrough_dead) reaches a case or default
// label in its CFG block, as clang's analysis then takes it, with only a warning (and instead of
// check_input_fallthrough_placed, which looks through plain labels that clang, for code that can't run, doesn't):
// statements that don't end a block (check_input_fallthrough_straight), then the label; an if
// without an else may come last, its false edge the label's. A jump, a loop, an if with an else, another fallthrough (the
// last in a block is the one clang takes), a statement expression, and a plain label that a goto doesn't name end it
// (an empty one that a goto names is looked through, as clang looks through a reachable block of no statements).
static bool check_input_fallthrough_flows(CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t statement = token;
    uint32_t result = 2;
    while (result == 2)
    {
        const uint32_t parent = statement < count ? parser->tree.parents[statement] : count;
        const uint8_t parent_role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
        if (parent_role == PARSE_ROLE_CLOSE_BLOCK)
        {
            // The block's statements after this one.
            uint32_t child = reader->next_child[statement];
            bool pending_if = false;
            while (result == 2 && child != BUSTER_CHECK_NONE)
            {
                const uint8_t role = parser->tree.roles[child];
                const uint32_t keyword = role == PARSE_ROLE_CLOSE_CONDITION ? reader->first_child[child] : BUSTER_CHECK_NONE;
                const bool straight = check_input_fallthrough_straight(reader, child);
                bool if_alone = keyword < count && parser->tokens.kinds[keyword] == PARSE_KIND_IF;
                for (uint32_t branch = if_alone ? reader->first_child[child] : BUSTER_CHECK_NONE; branch != BUSTER_CHECK_NONE; branch = reader->next_child[branch])
                {
                    if_alone = if_alone && parser->tokens.kinds[branch] != PARSE_KIND_ELSE;
                }
                const uint32_t labeled = role == PARSE_ROLE_LABEL ? check_input_fallthrough_labeled(reader, child) : BUSTER_CHECK_NONE;
                const bool empty_label = labeled != BUSTER_CHECK_NONE && parser->tokens.kinds[labeled] == PARSE_KIND_SEMI &&
                                         (labeled == 0 || !check_input_fallthrough_within(parser, labeled - 1, labeled)) &&
                                         check_input_fallthrough_targeted(reader, child);
                if (role == PARSE_ROLE_CLOSE_CASE)
                {
                    result = 1;
                }
                else if (pending_if)
                {
                    // An if without an else whose false edge isn't a case label's.
                    result = 0;
                }
                else if (straight || empty_label)
                {
                    child = reader->next_child[child];
                }
                else if (if_alone)
                {
                    pending_if = true;
                    child = reader->next_child[child];
                }
                else
                {
                    result = 0;
                }
            }
            // Out of the block, to the statements after it, but for an if's pending false edge.
            result = result == 2 && pending_if ? 0 : result;
            statement = parent;
        }
        else if (parent_role == PARSE_ROLE_CLOSE_CASE)
        {
            // A case label's statement: what follows the label.
            statement = parent;
        }
        else
        {
            // A function's, a switch's or a control statement's end, a plain label's statement.
            result = 0;
        }
    }
    return result == 1;
}

// Whether a fallthrough null statement (`token`, its `;`) directly precedes a switch label, as clang's analysis of the
// function's CFG has it: the statement that runs after it, past statements without executable ones
// (check_input_fallthrough_item: null statements, empty compounds, plain labels on them), out of compound statements, the
// branches of an `if` and a switch's end (the switch's own next statement), but never a loop's body, which goes back to
// its condition, is a case or default label (another switch's too), or begins with one. A plain label on a case label
// or on any other statement, any other statement or the function's end is not. The walk goes up one statement at a
// time, its parents later tokens.
static bool check_input_fallthrough_placed(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t statement = token;
    bool placed = false;
    bool done = false;
    while (!done)
    {
        const uint32_t parent = statement < count ? parser->tree.parents[statement] : count;
        const uint8_t role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
        const uint8_t kind = parent < count ? parser->tokens.kinds[parent] : 0;
        if (role == PARSE_ROLE_CLOSE_BLOCK)
        {
            // The compound's next item: the first later token whose parent is the compound's brace, past statements
            // without executable ones (check_input_fallthrough_item).
            uint32_t next = statement + 1;
            uint32_t kind_of = BUSTER_CHECK_FALLTHROUGH_EMPTY;
            while (kind_of == BUSTER_CHECK_FALLTHROUGH_EMPTY && next < parent)
            {
                kind_of = parser->tree.parents[next] == parent ? check_input_fallthrough_item(reader, next) : BUSTER_CHECK_FALLTHROUGH_EMPTY;
                next += kind_of == BUSTER_CHECK_FALLTHROUGH_EMPTY;
            }
            if (kind_of != BUSTER_CHECK_FALLTHROUGH_EMPTY)
            {
                placed = kind_of == BUSTER_CHECK_FALLTHROUGH_PLACED;
                done = true;
            }
            else
            {
                statement = parent;
            }
        }
        else if (role == PARSE_ROLE_CLOSE_CONDITION)
        {
            // An if's branch goes on after the if, and a switch's body after the switch; a loop's body goes back.
            const uint32_t keyword = reader->first_child[parent];
            const uint8_t keyword_kind = keyword < count ? parser->tokens.kinds[keyword] : 0;
            done = keyword_kind != PARSE_KIND_IF && keyword_kind != PARSE_KIND_SWITCH;
            statement = parent;
        }
        else if (role == PARSE_ROLE_CLOSE_CASE || role == PARSE_ROLE_LABEL || kind == PARSE_KIND_ELSE)
        {
            // A labeled statement's, or an else branch's (whose parent is its if's condition), goes on after it.
            statement = parent;
        }
        else
        {
            // A function's end, a loop's (do's) body, or a place no statement has: no label follows.
            done = true;
        }
    }
    return placed;
}


// clang's analysis of fallthrough null statement `token` (its `;`): an error at the statement's first token where it
// doesn't precede a case label in its CFG block (check_input_fallthrough_placed), or, where it can't run
// (check_input_fallthrough_dead), where its block doesn't reach one (check_input_fallthrough_flows).
static void check_input_fallthrough_misplaced(CheckInputReader* reader, uint32_t token)
{
    if (!(check_input_fallthrough_dead(reader, token) ? check_input_fallthrough_flows(reader, token) : check_input_fallthrough_placed(reader, token)))
    {
        // clang's analysis of the function's CFG reports it at the statement's first token.
        const uint32_t first = check_input_fallthrough_first(reader, token);
        check_input_report(reader, first, CHECK_INPUT_FALLTHROUGH_PLACEMENT);
    }
}

// Records fallthrough null statement `token` for check_input_fallthrough_placements, in the reader's arena, which grows
// the list by doubling.
static void check_input_fallthrough_record(CheckInputReader* reader, uint32_t token)
{
    if (reader->fallthrough_count == reader->fallthrough_capacity)
    {
        const uint32_t capacity = reader->fallthrough_capacity == 0 ? 16 : reader->fallthrough_capacity <= UINT32_MAX / 2 ? reader->fallthrough_capacity * 2 : 0;
        uint32_t* tokens = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity) : NULL;
        reader->output->resource |= tokens == NULL;
        if (tokens != NULL)
        {
            if (reader->fallthrough_count != 0)
            {
                memcpy(tokens, reader->fallthrough_tokens, (uint64_t)reader->fallthrough_count * sizeof(uint32_t));
            }
            reader->fallthrough_tokens = tokens;
            reader->fallthrough_capacity = capacity;
        }
    }
    if (reader->fallthrough_count < reader->fallthrough_capacity)
    {
        reader->fallthrough_tokens[reader->fallthrough_count++] = token;
    }
}

// The recorded fallthroughs' analysis (check_input_fallthrough_misplaced), once the bindings' replay has resolved the uses
// it reads, in token order. Clang's analysis runs at a function's end only where no error came before it
// (hasUncompilableErrorOccurred): a function whose end a report's token precedes (an undeclared identifier's, a jump's,
// an earlier function's misplaced fallthrough) has none, though one function's analysis reports each of its own.
static void check_input_fallthrough_placements(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const CheckInput* input = reader->output;
    uint32_t first_error = count;
    for (uint32_t report = 0; report < input->report_count; report += 1)
    {
        first_error = input->reports[report].token < first_error ? input->reports[report].token : first_error;
    }
    const uint32_t recorded = reader->fallthrough_count;
    uint32_t function = BUSTER_CHECK_NONE;
    uint32_t reported = count;
    // The blocks' resumable scans (check_input_fallthrough_dead), for this pass alone; without room, each query scans.
    const uint64_t mark = reader->scratch->length;
    reader->fallthrough_block_next = recorded != 0 ? arena_push_array(reader->scratch, count, sizeof(uint32_t), 64) : NULL;
    reader->fallthrough_block_last = reader->fallthrough_block_next != NULL ? arena_push_array(reader->scratch, count, sizeof(uint32_t), 64) : NULL;
    reader->fallthrough_block_state = reader->fallthrough_block_last != NULL ? arena_push_array(reader->scratch, count, sizeof(uint8_t), 64) : NULL;
    if (reader->fallthrough_block_state == NULL)
    {
        arena_pop_to(reader->scratch, mark);
        reader->fallthrough_block_next = NULL;
        reader->fallthrough_block_last = NULL;
    }
    else
    {
        memset(reader->fallthrough_block_next, 0, (uint64_t)count * sizeof(uint32_t));
    }
    for (uint32_t i = 0; i < recorded; i += 1)
    {
        const uint32_t token = reader->fallthrough_tokens[i];
        uint32_t end = token;
        while (end < count && parser->tree.parents[end] < count)
        {
            end = parser->tree.parents[end];
        }
        // A later function's analysis sees the errors the earlier ones reported.
        first_error = end != function && reported < first_error ? reported : first_error;
        function = end;
        if (end < first_error)
        {
            const uint32_t before = input->report_count;
            check_input_fallthrough_misplaced(reader, token);
            reported = input->report_count != before && input->reports[before].token < reported ? input->reports[before].token : reported;
        }
    }
    reader->fallthrough_count = 0;
    reader->fallthrough_block_next = NULL;
    reader->fallthrough_block_last = NULL;
    reader->fallthrough_block_state = NULL;
    arena_pop_to(reader->scratch, mark);
}

// Whether clang's analysis of the function's CFG, which reports a misplaced fallthrough, runs where `token` is:
// AnalysisBasedWarnings::IssueWarnings returns at once when every warning is ignored (-w), and for a function in a
// system header unless -Wsystem-headers (the token's own level stands for the function's). A reader without a
// preprocessor (a synthetic control walk's) has neither option, so the analysis runs.
static bool check_input_fallthrough_analyzed(const CheckInputReader* reader, uint32_t token)
{
    const Preprocessor* pp = reader->pp;
    const CheckGraph* graph = &reader->output->graph;
    const bool system = graph->system_tokens != NULL && token < graph->system_token_count && graph->system_tokens[token] != 0;
    return pp == NULL || (!pp->options.suppress_warnings && (!system || pp->options.system_header_warnings));
}

// before == NONE is the unchanged full-unit path. The diagnostic prefix collects all function-scoped label names
// for forward references, but reports only sites before its completion and defers unfinished-function obligations.
static __attribute__((always_inline)) void check_input_controls_range(CheckInputReader* reader, uint32_t before, uint32_t begin, uint32_t end)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool scalar = BUSTER_SCALAR_PATH(parser->options.scalar);
    const bool prefix = before != BUSTER_CHECK_NONE;
    const uint32_t limit = prefix && before < end ? before : end;
    const uint64_t mark = reader->scratch->length;
    const uint64_t capacity = check_input_label_capacity_range(parser, prefix ? limit : 0, begin, end);
    uint32_t* labels = arena_push_array(reader->scratch, capacity, sizeof(uint32_t), 4);
    reader->output->resource |= labels == 0;
    CheckLocalLabels locals = { 0 };
    if (labels != 0)
    {
        memset(labels, 0, capacity * sizeof(uint32_t));
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t close = 0;
        CheckInputControlScan scan = { .after = begin };
        for (uint32_t token = begin; token < end; token += 1)
        {
            if (!scalar)
            {
                token = check_input_control_next_range(parser, &scan, false, end);
                if (token == end)
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
                // The unit's first list: the lookups before it, which no columns took, before its names are pushed.
                if (locals.heads == NULL)
                {
                    check_input_local_labels_make(reader, &locals);
                    if (locals.heads != NULL && body != BUSTER_CHECK_NONE)
                    {
                        check_input_label_lookups_before(reader, &locals, body, close, token);
                    }
                }
                check_input_local_label_push(reader, &locals, token);
            }
            else if (role == PARSE_ROLE_LABEL)
            {
                const uint32_t name_token = check_input_label_name(reader, token);
                if (body == BUSTER_CHECK_NONE || name_token >= count || parser->tokens.kinds[name_token] != PARSE_KIND_IDENTIFIER)
                {
                    if (!prefix || token < limit)
                    {
                        check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                    }
                }
                else
                {
                    check_sparse_set(reader->output->sparse_pool, reader->output->declaration_bodies, token, body);
                    const uint32_t name = parser->tokens.names[name_token];
                    // A local label's definition is its scope's (CheckLocalLabels); one of an ambiguous name defines none.
                    const uint32_t local = check_input_local_label(reader, &locals, name);
                    const CheckLabelLookup lookup = check_input_label_lookup(reader, &locals, token, name_token, body, close);
                    const bool ambiguous = lookup.ambiguous;
                    const uint32_t scope = local != BUSTER_CHECK_NONE && !ambiguous ? locals.tokens[local] : body;
                    // The label an ambiguous goto or address made, which this one alone of the function's finds, is defined.
                    if (lookup.one < count && parser->tree.roles[lookup.one] != PARSE_ROLE_LABEL && locals.scopes[lookup.one] == BUSTER_CHECK_LABEL_AMBIGUOUS)
                    {
                        locals.scopes[lookup.one] = BUSTER_CHECK_LABEL_AMBIGUOUS_DEFINED;
                    }
                    if (local != BUSTER_CHECK_NONE)
                    {
                        locals.scopes[token] = ambiguous ? 0 : scope + 1;
                    }
                    uint64_t slot = ((uint64_t)name * UINT64_C(11400714819323198485) + scope) & (capacity - 1);
                    // Whether a redefinition keeps the earlier label as the name's, for its gotos.
                    bool kept_earlier = false;
                    // An ambiguous definition defines the label of the function's own scope it made, which a later
                    // reference may find alone: it goes into the table, but never as a redefinition.
                    while (labels[slot] != 0)
                    {
                        const uint32_t old = (labels[slot] & ~BUSTER_CHECK_LABEL_USED) - 1;
                        const uint32_t old_name = check_input_label_name(reader, old);
                        if (!ambiguous && check_input_label_scope(reader, &locals, old) == scope && parser->tokens.names[old_name] == name)
                        {
                            // Clang defines a label as its statement completes (ActOnLabelStmt), so a label within the other's
                            // statement (`L: L: ;`, `L: { L: ; }`) is the one defined first, and the other is the redefinition,
                            // which clang drops, keeping its statement.
                            uint32_t outer = parser->tree.parents[token];
                            for (uint32_t steps = 0; outer < count && outer != old && parser->tree.parents[outer] != outer && steps < count; steps += 1)
                            {
                                outer = parser->tree.parents[outer];
                            }
                            const bool nested = outer == old;
                            kept_earlier = !nested;
                            if (!prefix || token < limit)
                            {
                                // With clang's note at the first definition's name, a local label's at its name in its list
                                // (its scope), which declares it. Recovered (#1447): the statement stays, and the unit is read
                                // on as a valid one, its controls' constraints among them.
                                const uint32_t before = reader->output->report_count;
                                check_input_report(reader, nested ? old_name : name_token, CHECK_INPUT_LABEL_REDEFINITION);
                                if (reader->output->report_count != before)
                                {
                                    reader->output->reports[before].note_token = local != BUSTER_CHECK_NONE ? scope : nested ? name_token : old_name;
                                    reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_PREVIOUS_DEFINITION;
                                    check_input_recover_reports(reader, before);
                                }
                            }
                            break;
                        }
                        slot = (slot + 1) & (capacity - 1);
                    }
                    if (ambiguous && (!prefix || token < limit))
                    {
                        check_input_report_label_ambiguous(reader, name_token, &lookup);
                    }
                    if (!kept_earlier)
                    {
                        labels[slot] = token + 1;
                    }
                }
            }
            else if ((kind == PARSE_KIND_GOTO && check_input_asm_id(reader->output, token) == 0) || (kind == PARSE_KIND_AMPAMP && role == PARSE_ROLE_PREFIX))
            {
                check_sparse_set(reader->output->sparse_pool, reader->output->declaration_bodies, token, body);
                // A use of a local label takes its scope here, where its lists are in scope: an ambiguous name's, at its name.
                const uint32_t operand = reader->first_child[token];
                const bool named = operand < count && parser->tokens.kinds[operand] == PARSE_KIND_IDENTIFIER && !check_input_indirect_goto(parser, token);
                const uint32_t local = named ? check_input_local_label(reader, &locals, parser->tokens.names[operand]) : BUSTER_CHECK_NONE;
                const CheckLabelLookup lookup = named ? check_input_label_lookup(reader, &locals, token, operand, body, close) : (CheckLabelLookup){ .local = BUSTER_CHECK_NONE };
                if (local != BUSTER_CHECK_NONE || lookup.ambiguous)
                {
                    locals.scopes[token] = lookup.ambiguous ? BUSTER_CHECK_LABEL_AMBIGUOUS : locals.tokens[local] + 1;
                    if (lookup.ambiguous && (!prefix || token < limit))
                    {
                        check_input_report_label_ambiguous(reader, operand, &lookup);
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
                if (owner >= count || end >= count)
                {
                    // (Outside any switch: clang's own words, #2406.)
                    const CheckInputMessage outside = parser->tokens.kinds[token] == PARSE_KIND_DEFAULT ? CHECK_INPUT_DEFAULT_OUTSIDE_SWITCH : CHECK_INPUT_CASE_OUTSIDE_SWITCH;
                    check_input_report(reader, token, end < count ? outside : CHECK_INPUT_DECLARATION);
                }
                else if (complete && !check_input_region_transfer(reader, owner, end))
                {
                    check_input_report_jump(reader, token, CHECK_INPUT_JUMP_SWITCH, owner, end);
                }
                else if (complete)
                {
                    check_sparse_set(reader->output->sparse_pool, reader->output->control_owners, token, owner);
                    check_sparse_set(reader->output->sparse_pool, reader->output->control_owners, end, owner);
                }
            }
        }
        check_input_local_label_pop(reader, &locals, labels, capacity, BUSTER_CHECK_NONE, limit);
        scan = (CheckInputControlScan){ .after = begin };
        for (uint32_t token = begin; token < limit; token += 1)
        {
            if (!scalar)
            {
                token = check_input_control_next_range(parser, &scan, true, end);
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
                const uint32_t body = check_sparse_get(reader->output->declaration_bodies, token);
                const uint32_t operand = reader->first_child[token];
                const bool local = locals.scopes != NULL && locals.scopes[token] != 0;
                const bool defined = locals.scopes != NULL && locals.scopes[token] == BUSTER_CHECK_LABEL_AMBIGUOUS_DEFINED;
                const bool ambiguous = locals.scopes != NULL && (locals.scopes[token] == BUSTER_CHECK_LABEL_AMBIGUOUS || defined);
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
                    const uint64_t slot = parser->tree.roles[target] == PARSE_ROLE_LABEL ? check_input_label_slot(reader, &locals, labels, capacity, target) : capacity;
                    if (slot < capacity)
                    {
                        labels[slot] |= BUSTER_CHECK_LABEL_USED;
                    }
                    check_sparse_set(reader->output->sparse_pool, reader->output->label_targets, token, target);
                    reader->output->label_address_count += address;
                    if (!address && (!prefix || !unfinished) && !check_input_region_transfer(reader, token, target))
                    {
                        check_input_report_jump(reader, token, CHECK_INPUT_JUMP_GOTO, token, target);
                    }
                }
                // A missing label of the function's, and an ambiguous local one's use, for which clang's lookup creates a
                // label of the function's that nothing defines, as the function ends (check_input_report_label_undeclared).
                if (target == BUSTER_CHECK_NONE && !unfinished && (!local || ambiguous) && !defined)
                {
                    const uint32_t close = body < count && parser->tree.roles[body] == PARSE_ROLE_OPEN_BODY ? parser->tree.parents[body] : 0;
                    check_input_report_label_undeclared(reader, operand < count && parser->tokens.kinds[operand] == PARSE_KIND_IDENTIFIER ? operand : token,
                                                        close < count ? close : 0, true, ambiguous);
                }
            }
            else if (kind == PARSE_KIND_SEMI && check_input_attributes_at(reader->output, token) != 0)
            {
                bool enclosed = false;
                for (uint32_t ancestor = parser->tree.parents[token]; ancestor < count && !enclosed; ancestor = parser->tree.parents[ancestor])
                {
                    const uint32_t keyword = reader->first_child[ancestor];
                    enclosed = parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_CONDITION && keyword < count &&
                               parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH;
                }
                // clang's error at the fallthrough's name (check_input_statement_attributes records a null statement's
                // attributes for it alone).
                const CheckInputAttributes* attributes = check_input_attributes_at(reader->output, token);
                uint32_t at = token;
                for (uint32_t i = attributes->count; i-- > 0;)
                {
                    at = attributes->records[i].kind == CHECK_ATTRIBUTE_FALLTHROUGH ? attributes->records[i].name_token : at;
                }
                if (!enclosed)
                {
                    check_input_report(reader, at, CHECK_INPUT_FALLTHROUGH_SWITCH);
                }
                else if (check_input_fallthrough_analyzed(reader, token) && reader->pp != NULL)
                {
                    check_input_fallthrough_record(reader, token);
                }
                else if (check_input_fallthrough_analyzed(reader, token))
                {
                    // A synthetic control walk's, which has no bindings' replay to wait for.
                    check_input_fallthrough_misplaced(reader, token);
                }
            }
        }
        if (reader->indirect_gotos != 0 && begin == 0 && end == count)
        {
            check_input_indirect_gotos(reader);
        }
        // (A synthetic control walk's, of no preprocessor, has no warnings.)
        if (!prefix && reader->pp != NULL)
        {
            check_input_unused_labels(reader, &locals, labels, capacity, begin, limit);
        }
    }
    arena_pop_to(reader->scratch, mark);
}

static bool check_deferred_controls(CheckDeferred* deferred, CheckInputReader* reader);

static void check_input_controls(CheckInputReader* reader)
{
    if (reader->deferred == NULL || !check_deferred_controls(reader->deferred, reader))
    {
        check_input_controls_range(reader, BUSTER_CHECK_NONE, 0, reader->parser->tokens.count);
    }
}

static __attribute__((noinline)) void check_input_controls_prefix(CheckInputReader* reader, uint32_t before)
{
    const uint32_t fragment_end = reader->fragment_end;
    reader->fragment_end = before < fragment_end ? before : fragment_end;
    check_input_controls_range(reader, before, 0, reader->parser->tokens.count);
    reader->fragment_end = fragment_end;
}
