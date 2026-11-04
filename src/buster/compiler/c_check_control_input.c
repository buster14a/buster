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
        refused = named && check_input_label_refusal(check_input_attribute_name(reader, parser->tokens.names[token])) != NULL ? token : BUSTER_CHECK_NONE;
    }
    if (refused != BUSTER_CHECK_NONE)
    {
        check_input_report(reader, refused, CHECK_INPUT_LABEL_ATTRIBUTE);
    }
    uint32_t cursor = first;
    CheckInputAttributes* attributes = 0;
    bool valid = refused == BUSTER_CHECK_NONE && after > first && after < count && check_input_read_attributes(reader, &cursor, after, &attributes) && cursor == after;
    // A label's subject as clang's attributes name it (#147): the warning for those that apply elsewhere.
    valid = valid && check_input_attribute_subjects(reader, attributes, BUSTER_CHECK_SUBJECT_LABEL, 0, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
    for (uint32_t i = 0; valid && attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        valid = attribute->keyword || check_input_label_refusal(check_input_attribute_name(reader, attribute->name)) == NULL;
        if (!valid)
        {
            check_input_report(reader, attribute->first, CHECK_INPUT_LABEL_ATTRIBUTE);
        }
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

// Whether the attributes from `first` to `after` are GNU ones of clang's statement attributes alone, by their names: each
// item of a list starts right after its opener or after a comma among its items.
static bool check_input_statement_attribute_names(CheckInputReader* reader, uint32_t first, uint32_t after)
{
    const Parser* parser = reader->parser;
    bool result = true;
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
        }
    }
    return result;
}

// A statement's attributes, as clang's Sema takes them, after their reading (check_input_read_attributes) took each: a null
// statement's fallthrough (`empty`), and a return's musttail (`returns`) are the statement's; GNU's noinline and
// always_inline clang ignores on a statement (with a warning, which isn't modeled), and nomerge and suppress on any; musttail elsewhere, fallthrough on a
// statement that isn't empty, and every other attribute are clang's errors, the first one's reported. The musttail's
// first token, or BUSTER_CHECK_NONE, in `*musttail`. Whether they are valid.
static bool check_input_statement_attribute_kinds(CheckInputReader* reader, CheckInputAttributes* attributes, bool empty, bool returns, uint8_t next,
                                                   uint32_t* musttail)
{
    bool valid = true;
    *musttail = BUSTER_CHECK_NONE;
    uint32_t aligned = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; valid && attributes != 0 && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = attributes->records + i;
        const CheckInputAttributeKind kind = attribute->kind;
        const String8 generic = kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword ? check_input_attribute_name(reader, attribute->name) : (String8){ 0 };
        const bool code_align = check_input_attribute_equal(generic, "code_align");
        const bool unroll = check_input_attribute_equal(generic, "opencl_unroll_hint");
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
        else if ((kind == CHECK_ATTRIBUTE_NOINLINE || kind == CHECK_ATTRIBUTE_ALWAYS_INLINE) && !attribute->standard && !attribute->keyword)
        {
            attribute->ignored = true;
        }
        else if (kind == CHECK_ATTRIBUTE_NOMERGE || kind == CHECK_ATTRIBUTE_SUPPRESS)
        {
            // clang's nomerge keeps the statement's calls from merging, which -O0's code never does, and suppress is the
            // static analyzer's (clang warns of a nomerge without a call, which isn't modeled).
            attribute->ignored = true;
        }
        else
        {
            valid = false;
            check_input_report(reader, attribute->first, CHECK_INPUT_STATEMENT_ATTRIBUTE);
        }
    }
    return valid;
}

// A block item's attributes before a statement rather than a declaration, read as the statement's (their tokens then
// ignored, so that check_input's walk takes no declaration for them), with clang's rules
// (check_input_statement_attribute_kinds): before a null statement, a statement's keyword or brace, or an expression when
// they are C23's alone or GNU ones of clang's statement attributes alone, which the parser then reads as a statement's
// (check_input_statement_attribute_names). A null statement's are recorded at its `;` for fallthrough's consumer, and a
// return's musttail for c_check_musttail.c and check_program. GNU attributes right after a named label are the label's
// (check_input_label_attributes); C23's there are the statement's. Outside a function a null statement's attributes are
// refused as a declaration's.
static void check_input_statement_attributes(CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t after = check_input_skip_attributes(parser, first, count);
    const uint8_t next = after < count ? parser->tokens.kinds[after] : PARSE_KIND_SEMI;
    const bool label = first != 0 && parser->tree.roles[first - 1] == PARSE_ROLE_LABEL && parser->tokens.kinds[first] == PARSE_KIND_ATTRIBUTE;
    const bool body = reader->function_body < count;
    const bool returns = after < count && next == PARSE_KIND_RETURN;
    const bool keyword = after < count && (returns || next == PARSE_KIND_IF || next == PARSE_KIND_SWITCH || next == PARSE_KIND_WHILE || next == PARSE_KIND_DO ||
                                           next == PARSE_KIND_FOR || next == PARSE_KIND_GOTO || next == PARSE_KIND_CONTINUE || next == PARSE_KIND_BREAK ||
                                           next == PARSE_KIND_CASE || next == PARSE_KIND_DEFAULT || next == PARSE_KIND_L_BRACE);
    const bool empty = after < count && next == PARSE_KIND_SEMI;
    // GNU attributes among them make it a declaration unless each is one of clang's statement attributes; C23's alone
    // leave it to what follows (#147's `[[x]] y += 1;`), as the parser reads them (parse_scalar_attributed_statement).
    bool gnu = false;
    for (uint32_t token = first; token < after; token += 1)
    {
        gnu |= parser->tokens.kinds[token] == PARSE_KIND_ATTRIBUTE;
    }
    const bool expression = after < count && !empty && !keyword && !check_input_declaration_after(parser, after) &&
                            (!gnu || check_input_statement_attribute_names(reader, first, after));
    // Before a label's name, the statement's attributes are its label's.
    const bool named = expression && next == PARSE_KIND_IDENTIFIER && after + 1 < count && parser->tree.roles[after + 1] == PARSE_ROLE_LABEL;
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
    else if (empty && !body)
    {
        check_input_report(reader, first, CHECK_INPUT_DECLARATION);
    }
    else if (body && (empty || keyword || expression))
    {
        uint32_t cursor = first;
        CheckInputAttributes* attributes = 0;
        reader->statement_attributes = true;
        bool valid = check_input_read_attributes(reader, &cursor, count, &attributes) && cursor == after;
        reader->statement_attributes = false;
        uint32_t musttail = BUSTER_CHECK_NONE;
        valid = valid && check_input_statement_attribute_kinds(reader, attributes, empty, returns, next, &musttail);
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
    // Per token: 1 + a label definition's or use's scope, or 0 for its body.
    uint32_t* scopes;
    uint32_t count;
};

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
        const uint32_t target_name = check_input_label_name(reader, target);
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
        uint32_t first = item;
        while (first > 0 && check_input_fallthrough_within(parser, first - 1, item))
        {
            first -= 1;
        }
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

// The first token of statement `item`'s subtree.
static uint32_t check_input_fallthrough_first(const Parser* parser, uint32_t item)
{
    uint32_t first = item;
    while (first > 0 && check_input_fallthrough_within(parser, first - 1, item))
    {
        first -= 1;
    }
    return first;
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

// A control statement's condition, tokens `first` to `end`, as clang's CFG builder takes it, its edges to a branch that
// can't run pruned (tryEvaluateBool, #1126's review): 1 for true, 0 for false, 2 for unknown (or no condition: `for (;;)`'s
// is true, which its caller takes). Its && and || operators fold as clang's have it, each operand past its parentheses:
// a known left operand that decides (`0 && x`, `1 || x`) is the value; otherwise both known operands' value, or a known
// right one that decides (`x && 0`, `x || 1`), even past the left operand's side effects; any other operand is
// check_input_fallthrough_truth's; a `!` before an operand negates its known value (`!(x && 0)`, which clang's evaluator
// folds past the side effects). Clang's tautology checks of comparisons (`x > 5 && x < 3`) aren't made: unknown here.
// A stack of 16 operators takes the operands: no recursion (the call graph's rule), and a deeper one is unknown.
static uint32_t check_input_fallthrough_condition(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t firsts[16];
    uint32_t ends[16];
    uint32_t operators[16];
    uint32_t lefts[16];
    uint8_t phases[16];
    bool negations[16];
    uint32_t depth = first < end ? 1 : 0;
    firsts[0] = first;
    ends[0] = end;
    phases[0] = 0;
    negations[0] = false;
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
                a += group || negation ? 1 : 0;
                b -= group ? 1 : 0;
                stripping = group || negation;
                root = stripping ? check_input_fallthrough_root(parser, a, b) : root;
            }
            const bool logical = root < count && (parser->tree.roles[root] == PARSE_ROLE_LOGICAL_AND || parser->tree.roles[root] == PARSE_ROLE_LOGICAL_OR);
            if (logical && depth < 16 && a < root && root + 1 < b)
            {
                ends[top] = b;
                operators[top] = root;
                phases[top] = 1;
                firsts[depth] = a;
                ends[depth] = root;
                phases[depth] = 0;
                negations[depth] = false;
                depth += 1;
            }
            else
            {
                const uint32_t value = logical ? 2 : check_input_fallthrough_truth(reader, a, b);
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
                depth += 1;
                returned = false;
            }
        }
        else
        {
            const uint32_t left = lefts[top];
            const uint32_t right = result;
            const uint32_t value = left != 2 && right != 2 ? (either ? (left | right) : (left & right)) : left == 2 && right != 2 && (right == 1) == either ? right : 2;
            result = value != 2 && negations[top] ? value ^ 1 : value;
            depth -= 1;
        }
    }
    return result;
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
    uint32_t token = body < count ? check_input_fallthrough_first(parser, body) : count;
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
                const uint32_t condition = open != BUSTER_CHECK_NONE ? check_input_fallthrough_condition(reader, open + 1, at) : 2;
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
// parentheses, `&` and `*` (`die`, `(die)`, `(&die)`), or BUSTER_CHECK_NONE.
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
        stripping = inner < count;
        callee = stripping ? inner : callee;
    }
    return callee < count && parser->tokens.kinds[callee] == PARSE_KIND_IDENTIFIER ? callee : BUSTER_CHECK_NONE;
}

// Whether call `close` never returns, as clang's CFG ends its block there (CFGBuilder::VisitCallExpr): its callee names a
// function declared noreturn (CHECK_INPUT_FUNCTION_NORETURN, which a redeclaration inherits), a builtin clang's table marks
// noreturn (`r`: __builtin_abort, __builtin_unreachable, __builtin_trap), or a library function it marks so (abort, exit,
// _Exit) declared with external linkage where the library's builtins are on, as clang's declaration merges the builtin's
// noreturn type; or it is __builtin_assume of a false condition (isBuiltinAssumeFalse). A call through a pointer whose
// type is noreturn isn't known here.
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
    bool noreturn = declared || builtin || check_input_fallthrough_trap(reader, callee);
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
        noreturn = length == 16 && memcmp(bytes, "__builtin_assume", 16) == 0 && open < close && check_input_fallthrough_condition(reader, open + 1, close) == 0;
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
        const uint32_t first = check_input_fallthrough_first(parser, node);
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
                     (check_input_fallthrough_stops(reader, open + 1, condition) || check_input_fallthrough_condition(reader, open + 1, condition) == 1);
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
            const uint32_t condition = open != BUSTER_CHECK_NONE && !stops ? check_input_fallthrough_condition(reader, open + 1, node) : 2;
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
                                     absent || (initial < second && second != BUSTER_CHECK_NONE && check_input_fallthrough_condition(reader, initial + 1, second) == 1) :
                                     open != BUSTER_CHECK_NONE && check_input_fallthrough_condition(reader, open + 1, node) == 1;
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
            // never completes makes what follows dead.
            for (uint32_t child = reader->first_child[parent]; child != BUSTER_CHECK_NONE && child < statement; child = reader->next_child[child])
            {
                uint32_t inner = child;
                bool labeled = false;
                while (inner != BUSTER_CHECK_NONE && (parser->tree.roles[inner] == PARSE_ROLE_LABEL || parser->tree.roles[inner] == PARSE_ROLE_CLOSE_CASE))
                {
                    labeled = true;
                    inner = check_input_fallthrough_labeled(reader, inner);
                }
                const bool leaves = parser->tree.roles[child] != PARSE_ROLE_OPEN_BLOCK && check_input_fallthrough_leaves(reader, child);
                state = leaves ? 0 : labeled ? 1 : state;
            }
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
    for (uint32_t at = straight ? check_input_fallthrough_first(parser, item) : item; straight && at <= item; at += 1)
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
    const Parser* parser = reader->parser;
    if (!(check_input_fallthrough_dead(reader, token) ? check_input_fallthrough_flows(reader, token) : check_input_fallthrough_placed(reader, token)))
    {
        // clang's analysis of the function's CFG reports it at the statement's first token.
        uint32_t first = token;
        while (first > 0 && check_input_fallthrough_within(parser, first - 1, token))
        {
            first -= 1;
        }
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
                        const uint32_t old_name = check_input_label_name(reader, old);
                        if (check_input_label_scope(reader, &locals, old) == scope && parser->tokens.names[old_name] == name)
                        {
                            if (!prefix || token < limit)
                            {
                                // With clang's note at the first definition's name, a local label's at its name in its list
                                // (its scope), which declares it.
                                const uint32_t before = reader->output->report_count;
                                check_input_report(reader, name_token, CHECK_INPUT_LABEL_REDEFINITION);
                                if (reader->output->report_count != before)
                                {
                                    reader->output->reports[before].note_token = local != BUSTER_CHECK_NONE ? scope : old_name;
                                    reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_PREVIOUS_DEFINITION;
                                }
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
                check_sparse_set(reader->output->sparse_pool, reader->output->declaration_bodies, token, body);
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
                    check_sparse_set(reader->output->sparse_pool, reader->output->control_owners, token, owner);
                    check_sparse_set(reader->output->sparse_pool, reader->output->control_owners, end, owner);
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
                const uint32_t body = check_sparse_get(reader->output->declaration_bodies, token);
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
                    check_sparse_set(reader->output->sparse_pool, reader->output->label_targets, token, target);
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
