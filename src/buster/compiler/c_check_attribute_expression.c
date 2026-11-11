// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Attribute arguments are expressions before a handler asks whether they are constants or counts them. Reuse the
// declaration evaluator's iterative type/actions and retain only the sparse failure evidence needed by diagnostics.
// Kept outside c_check_attribute.c so the evaluator's own type-attribute reader remains acyclic.

// A constant reading's failure evidence (CheckConstantFailure) in clang's words (#147, #1447): the error's kind, its token,
// and the report's type, value and note, as CHECK_INPUT_ATTRIBUTE_ARGUMENT's and CHECK_INPUT_CONSTANT_EXPRESSION's are
// printed (check_driver_attribute_argument). `token` is the failure's own place; CHECK_ARGUMENT_EXPECTED_EXPRESSION where
// no message of clang's fits the evidence.
typedef struct CheckConstantError CheckConstantError;
struct CheckConstantError
{
    CheckAttributeArgumentError error;
    uint32_t token;
    uint32_t number;
    TypeId type;
    uint64_t value;
    uint32_t note_token;
    uint8_t note_kind;
    // The token before which the type's typedef spelling is looked for (check_input_typedef_spelling), or 0 for the
    // caller's: a sizeof's operand's, which clang names as written (`sizeof(Outer)`'s 'Outer' (aka 'struct T')).
    uint32_t spelling;
};

// The first token that declares the tag of `type` (a struct, union or enum, its qualifiers and alignment aside), or
// BUSTER_CHECK_NONE: clang's "forward declaration of" note's place. It is the type's own declaration, not the name's
// visible one, which an inner definition may shadow (`typedef struct T Outer;` read past a block's `struct T { ... }`,
// review on #1900), as check_semantic_forward_note's; an error's path alone scans for it.
static uint32_t check_constant_failure_tag(CheckInputReader* reader, TypeId type)
{
    const CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const TypeId bare = type != 0 ? type_unqualified(type_strip_aligned(reader->types, type)) : 0;
    const uint8_t kind = bare != 0 ? type_kind(reader->types, bare) : 0;
    const bool tagged = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
    const uint32_t name = tagged ? reader->types->tags.name[reader->types->types.index[type_index(bare)]] : 0;
    uint32_t declared = BUSTER_CHECK_NONE;
    for (uint32_t t = 0; name != 0 && declared == BUSTER_CHECK_NONE && t < parser->tokens.count; t += 1)
    {
        declared = parser->tokens.names[t] == name && input->declarations[t] == t && reader->declaration_kinds[t] == C_BINDING_TAG_KIND &&
                   type_unqualified(reader->declared_types[t]) == bare ? t : BUSTER_CHECK_NONE;
    }
    return declared;
}

static CheckConstantError check_constant_failure_error(CheckInputReader* reader, const CheckConstantFailure* failure, uint32_t token)
{
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    const uint32_t count = parser->tokens.count;
    const bool member = failure->op == CHECK_OP_MEMBER || failure->op == CHECK_OP_ARROW;
    TypeId type = failure->type;
    const uint8_t written = type != 0 ? type_kind(types, type_strip_aligned(types, type)) : 0;
    // `->` of no pointer is clang's own error, at the member's name, naming the operand's type.
    const bool arrow_pointer = member && failure->op == CHECK_OP_ARROW && type != 0 && written != BUSTER_TYPE_KIND_POINTER &&
                               (failure->diagnostic == CHECK_DIAG_POINTER_REQUIRED || failure->diagnostic == CHECK_DIAG_MEMBER);
    type = member && failure->op == CHECK_OP_ARROW && written == BUSTER_TYPE_KIND_POINTER ? type_element(types, type) : type;
    const uint8_t kind = type != 0 ? type_kind(types, type_strip_aligned(types, type)) : 0;
    const bool record = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
    const bool size = failure->diagnostic == CHECK_DIAG_INCOMPLETE && (failure->op == CHECK_OP_SIZEOF || failure->op == CHECK_OP_ALIGNOF);
    const bool call = failure->diagnostic == CHECK_DIAG_ARGUMENT_COUNT && failure->op == CHECK_OP_CALL;
    const bool binary = failure->second_type != 0 && (failure->diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED ||
                        failure->diagnostic == CHECK_DIAG_INTEGER_REQUIRED || failure->diagnostic == CHECK_DIAG_SCALAR_REQUIRED ||
                        failure->diagnostic == CHECK_DIAG_INCOMPATIBLE);
    // Pointer arithmetic on an incomplete pointee: the pointer operand's, the first's or the second's.
    const TypeId second = failure->second_type;
    const bool first_pointer = failure->type != 0 && check_action_kind(types, failure->type) == BUSTER_TYPE_KIND_POINTER;
    const bool second_pointer = second != 0 && check_action_kind(types, second) == BUSTER_TYPE_KIND_POINTER;
    const bool arithmetic = failure->diagnostic == CHECK_DIAG_INCOMPLETE && (failure->op == CHECK_OP_ADD || failure->op == CHECK_OP_SUBTRACT) &&
                            (first_pointer || second_pointer);
    const TypeId pointee = arithmetic ? type_element(types, type_strip_aligned(types, first_pointer ? failure->type : second)) : 0;
    CheckConstantError result =
    {
        .error = member && (failure->diagnostic == CHECK_DIAG_MEMBER || arrow_pointer) ?
                 (arrow_pointer ? CHECK_ARGUMENT_EXPRESSION_ARROW_POINTER : record ? CHECK_ARGUMENT_EXPRESSION_MEMBER_NAME : CHECK_ARGUMENT_EXPRESSION_MEMBER_BASE) :
                 size ? CHECK_ARGUMENT_EXPRESSION_SIZEOF : call ? CHECK_ARGUMENT_EXPRESSION_CALL_COUNT : binary ? CHECK_ARGUMENT_EXPRESSION_BINARY :
                 arithmetic ? CHECK_ARGUMENT_EXPRESSION_INCOMPLETE_ARITHMETIC : CHECK_ARGUMENT_EXPECTED_EXPRESSION,
        .token = token,
        .number = call ? failure->expected : 0,
        .type = arithmetic ? pointee : type,
        .value = call ? failure->actual | ((uint64_t)failure->variadic << 32) : member ? failure->name : binary ? failure->second_type : 0,
        .note_token = BUSTER_CHECK_NONE,
    };
    // A member's name follows its operator, where clang reports a missing member and `->` of no pointer.
    const bool named = result.error == CHECK_ARGUMENT_EXPRESSION_MEMBER_NAME || result.error == CHECK_ARGUMENT_EXPRESSION_ARROW_POINTER;
    result.token = named && token + 1 < count && parser->tokens.kinds[token + 1] == PARSE_KIND_IDENTIFIER ? token + 1 : token;
    const uint32_t operand = size && token + 1 < count && parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN ? parser->tree.parents[token + 1] : 0;
    result.spelling = operand > token && operand < count ? operand : 0;
    if ((size && (record || kind == BUSTER_TYPE_KIND_ENUM)) || arithmetic)
    {
        result.note_token = check_constant_failure_tag(reader, arithmetic ? pointee : type);
        result.note_kind = CHECK_INPUT_NOTE_FORWARD_DECLARATION;
    }
    else if (call && failure->callee < count)
    {
        const uint32_t declaration = reader->output->declarations[failure->callee];
        result.note_token = declaration < count ? declaration : BUSTER_CHECK_NONE;
        result.note_kind = CHECK_INPUT_NOTE_FUNCTION_DECLARATION;
        // A declaration that names its only parameter: clang's single-argument words (check_driver_call_count's), the
        // parameter's token (plus 1) above the value's count and variadic bit.
        uint32_t parameter = BUSTER_CHECK_NONE;
        const uint32_t list = declaration + 1;
        const bool listed = failure->expected == 1 && declaration < count && list < count && parser->tokens.kinds[list] == PARSE_KIND_L_PAREN &&
                            parser->tree.parents[list] > list && parser->tree.parents[list] < count;
        for (uint32_t at = list + 1; listed && at < parser->tree.parents[list] && parser->tokens.kinds[at] != PARSE_KIND_COMMA; at += 1)
        {
            const uint8_t kind_at = parser->tokens.kinds[at];
            parameter = kind_at == PARSE_KIND_IDENTIFIER && reader->output->declarations[at] == at && reader->declaration_kinds[at] == C_BINDING_OBJECT ? at : parameter;
            at = (kind_at == PARSE_KIND_L_PAREN || kind_at == PARSE_KIND_L_SQUARE) && parser->tree.parents[at] > at ? parser->tree.parents[at] : at;
        }
        result.value |= parameter < count && parameter < (UINT32_MAX >> 1) ? (uint64_t)(parameter + 1) << 33 : 0;
        // Too many arguments is clang's error at the first one past the parameters.
        const uint32_t open = failure->callee + 1;
        const bool opened = open < count && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] > open &&
                            parser->tree.parents[open] < count;
        const uint32_t close = opened ? parser->tree.parents[open] : open;
        uint32_t commas = 0;
        // With no parameters, the first argument is the first extra one (review on #1900).
        result.token = opened && failure->expected == 0 && failure->actual > 0 && open + 1 < close ? open + 1 : result.token;
        for (uint32_t at = open + 1; opened && failure->actual > failure->expected && at < close && commas < failure->expected; at += 1)
        {
            const uint8_t kind_at = parser->tokens.kinds[at];
            const bool nested = (kind_at == PARSE_KIND_L_PAREN || kind_at == PARSE_KIND_L_SQUARE || kind_at == PARSE_KIND_L_BRACE) &&
                                parser->tree.parents[at] > at && parser->tree.parents[at] < close;
            commas += kind_at == PARSE_KIND_COMMA ? 1u : 0u;
            result.token = kind_at == PARSE_KIND_COMMA && commas == failure->expected ? at + 1 : result.token;
            at = nested ? parser->tree.parents[at] : at;
        }
    }
    return result;
}

// A declarator's constant's failure (#1447): its evidence's error in clang's words (check_constant_failure_error), else the
// generic declaration error at `fallback`. `first` is the constant's first token, from which a typedef's spelling is read.
static void check_input_constant_failure_report(CheckInputReader* reader, const CheckConstantFailure* failure, uint32_t first, uint32_t fallback)
{
    const Parser* parser = reader->parser;
    const bool evidence = failure != NULL && failure->diagnostic != CHECK_DIAG_NONE && failure->token < parser->tokens.count;
    const CheckConstantError mapped = evidence ? check_constant_failure_error(reader, failure, failure->token) :
                                                 (CheckConstantError){ .error = CHECK_ARGUMENT_EXPECTED_EXPRESSION };
    const uint32_t before = reader->output->report_count;
    if (mapped.error == CHECK_ARGUMENT_EXPECTED_EXPRESSION)
    {
        check_input_report(reader, fallback, CHECK_INPUT_DECLARATION);
    }
    else
    {
        check_input_report_argument(reader, mapped.token, CHECK_INPUT_CONSTANT_EXPRESSION, (uint32_t)mapped.error | mapped.number << 8);
    }
    if (mapped.error != CHECK_ARGUMENT_EXPECTED_EXPRESSION && reader->output->report_count > before)
    {
        CheckInputReport* report = reader->output->reports + before;
        // (The token its message's attribute name would be read from, which these messages don't name.)
        report->attribute = mapped.token;
        report->type = mapped.type;
        report->value = mapped.value;
        report->type_spelling = check_input_typedef_spelling(reader->output, parser, mapped.spelling != 0 ? mapped.spelling : first);
        report->note_token = mapped.note_token;
        report->note_kind = mapped.note_kind;
    }
}

// What clang's isUnusedResultAWarning reports of a discarded value (check_input_attribute_unused_of): its token and its
// BUSTER_CHECK_UNUSED_* message, or BUSTER_CHECK_NONE where the value is quiet.
typedef struct CheckAttributeUnused CheckAttributeUnused;
struct CheckAttributeUnused
{
    uint32_t token;
    uint32_t message;
};

// The operands of the parse tree's `node` within [first, end), in token order, but the brackets' openers and a
// conditional's `?`: at most four, and their count.
static uint32_t check_input_attribute_operands(const Parser* parser, uint32_t node, uint32_t first, uint32_t end, uint32_t* operands, uint32_t* opener)
{
    uint32_t count = 0;
    *opener = BUSTER_CHECK_NONE;
    for (uint32_t t = first; t < end; t += 1)
    {
        const uint8_t role = parser->tree.roles[t];
        const bool open = role >= PARSE_ROLE_OPEN_GROUP && role <= PARSE_ROLE_OPEN_CASE;
        const bool child = t != node && parser->tree.parents[t] == node;
        *opener = child && open && *opener == BUSTER_CHECK_NONE ? t : *opener;
        if (child && !open && count < 4)
        {
            operands[count] = t;
        }
        count += child && !open ? 1u : 0u;
    }
    return count;
}

// Whether the comma at `token` is a comma operator: the parser's COMMA, or in an attribute's argument, whose commas it reads
// as separators, one in a chain of commas a group's parentheses hold (a call's separate its arguments).
static bool check_input_attribute_comma(const Parser* parser, uint32_t token)
{
    uint32_t at = token;
    while (parser->tree.roles[at] == PARSE_ROLE_SEPARATOR && parser->tree.parents[at] != at && parser->tokens.kinds[parser->tree.parents[at]] == PARSE_KIND_COMMA)
    {
        at = parser->tree.parents[at];
    }
    const uint32_t parent = parser->tree.parents[at];
    return parser->tokens.kinds[token] == PARSE_KIND_COMMA &&
           (parser->tree.roles[token] == PARSE_ROLE_COMMA || (parser->tree.roles[at] == PARSE_ROLE_SEPARATOR && parent != at && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_GROUP));
}

// Whether `node` within [first, end) is, through parentheses, the integer literal 0.
static bool check_input_attribute_literal_zero(CheckInputReader* reader, uint32_t node, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    uint32_t at = node;
    uint32_t operands[4];
    uint32_t opener = BUSTER_CHECK_NONE;
    while (parser->tree.roles[at] == PARSE_ROLE_CLOSE_GROUP && check_input_attribute_operands(parser, at, first, end, operands, &opener) == 1)
    {
        at = operands[0];
    }
    CheckValue value = { 0 };
    TypeId unused = 0;
    const bool literal = parser->tokens.kinds[at] == PARSE_KIND_NUMBER &&
                         check_input_constant_read(reader, at, at + 1, false, false, false, &value, &unused, NULL, NULL);
    return literal && value.constant_kind == CHECK_CONSTANT_INTEGER && value.integer == 0;
}

#define BUSTER_CHECK_ATTRIBUTE_UNUSED_PENDING 64

// One step of check_input_attribute_unused_of at `node`: the operand its warning is (a group's, a comma's right
// operand's, a conditional's else arm's), or BUSTER_CHECK_NONE with `*leaf` its own; the operands that must warn as well
// (a conditional's then arm, a logical operator's operands) go on `pending`, and `*fits` turns false where they do not.
static uint32_t check_input_attribute_unused_step(CheckInputReader* reader, uint32_t node, uint32_t first, uint32_t end, uint32_t* pending, uint32_t* pending_count, bool* fits, CheckAttributeUnused* leaf)
{
    const Parser* parser = reader->parser;
    const uint8_t role = parser->tree.roles[node];
    const uint8_t kind = parser->tokens.kinds[node];
    uint32_t operands[4] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
    uint32_t opener = BUSTER_CHECK_NONE;
    const uint32_t count = check_input_attribute_operands(parser, node, first, end, operands, &opener);
    uint32_t next = BUSTER_CHECK_NONE;
    *leaf = (CheckAttributeUnused){ .token = node, .message = BUSTER_CHECK_UNUSED_COMMA };
    if (role == PARSE_ROLE_CLOSE_GROUP && count == 1)
    {
        next = operands[0];
    }
    else if (kind == PARSE_KIND_COMMA && count == 2 && check_input_attribute_comma(parser, node))
    {
        const bool zero = check_input_attribute_literal_zero(reader, operands[1], first, end);
        next = zero ? BUSTER_CHECK_NONE : operands[1];
        leaf->token = BUSTER_CHECK_NONE;
    }
    else if (role == PARSE_ROLE_CLOSE_TERNARY && (count == 2 || count == 3))
    {
        // A GNU `x ?: y`'s then arm is its condition.
        *fits = *fits && *pending_count < BUSTER_CHECK_ATTRIBUTE_UNUSED_PENDING;
        pending[*fits ? *pending_count : 0] = operands[count - 2];
        *pending_count += *fits ? 1u : 0u;
        next = operands[count - 1];
    }
    else if ((role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR) && count == 2)
    {
        *fits = *fits && *pending_count + 2 <= BUSTER_CHECK_ATTRIBUTE_UNUSED_PENDING;
        pending[*fits ? *pending_count : 0] = operands[0];
        pending[*fits ? *pending_count + 1 : 1] = operands[1];
        *pending_count += *fits ? 2u : 0u;
    }
    else if (role == PARSE_ROLE_CLOSE_CALL || role == PARSE_ROLE_ASSIGN || kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS)
    {
        leaf->token = BUSTER_CHECK_NONE;
    }
    else if (role == PARSE_ROLE_CLOSE_CAST && opener != BUSTER_CHECK_NONE)
    {
        const TypeId type = check_input_constant_type(reader, opener + 1, node);
        const TypeId plain = type != 0 ? type_unqualified(type_strip_aligned(reader->types, type)) : 0;
        const TypeId element = plain != 0 && type_kind(reader->types, plain) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(reader->types, type_element(reader->types, plain)) :
                                                                                                          0;
        const bool pointer = element != 0 && type_kind(reader->types, element) == BUSTER_TYPE_KIND_VOID && type_qualifiers(element) == 0;
        leaf->token = plain != 0 && type_kind(reader->types, plain) == BUSTER_TYPE_KIND_VOID ? BUSTER_CHECK_NONE : opener;
        leaf->message = pointer ? BUSTER_CHECK_UNUSED_VOID_POINTER : BUSTER_CHECK_UNUSED_COMMA;
    }
    else if (role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_RELATIONAL)
    {
        leaf->message = role == PARSE_ROLE_RELATIONAL ? BUSTER_CHECK_UNUSED_RELATIONAL :
                        kind == PARSE_KIND_EQUALEQUAL ? BUSTER_CHECK_UNUSED_EQUALITY : BUSTER_CHECK_UNUSED_INEQUALITY;
    }
    else if ((role == PARSE_ROLE_CLOSE_KEYWORD || role == PARSE_ROLE_CLOSE_SIZEOF) && opener != BUSTER_CHECK_NONE && opener > first)
    {
        leaf->token = opener - 1;
    }
    else if (role == PARSE_ROLE_MEMBER_OPERATOR && node + 1 < end)
    {
        leaf->token = node + 1;
    }
    return next;
}

// Whether a discarded `node` (a parse tree node within [first, end)) warns, as clang's Expr::isUnusedResultAWarning
// decides for the shapes an attribute's expression takes (#1447), and where: a group's is its operand's; a comma's its
// right operand's, but for a literal 0, which is quiet; a conditional's where both arms' do, at the else arm's; a logical
// operator's where both operands' do, at the operator; calls, assignments and increments are quiet; a cast is at its `(`,
// quiet to void and clang's question to an unqualified `void *`; a comparison is -Wunused-comparison's at its operator; a
// keyword's call (sizeof's, a builtin's keyword) at its keyword, a member's at its name, anything else at its own token.
// The first walk follows `node` to its warning; the operands it pushed must each warn too, walked the same way, and an
// expression too wide for the pending stack is quiet.
static CheckAttributeUnused check_input_attribute_unused_of(CheckInputReader* reader, uint32_t node, uint32_t first, uint32_t end)
{
    uint32_t pending[BUSTER_CHECK_ATTRIBUTE_UNUSED_PENDING];
    uint32_t pending_count = 1;
    pending[0] = node;
    bool fits = true;
    bool warns = true;
    bool primary = true;
    CheckAttributeUnused result = { .token = BUSTER_CHECK_NONE, .message = BUSTER_CHECK_UNUSED_COMMA };
    while (pending_count != 0 && warns && fits)
    {
        pending_count -= 1;
        uint32_t at = pending[pending_count];
        CheckAttributeUnused leaf = { .token = BUSTER_CHECK_NONE, .message = BUSTER_CHECK_UNUSED_COMMA };
        while (at != BUSTER_CHECK_NONE)
        {
            at = check_input_attribute_unused_step(reader, at, first, end, pending, &pending_count, &fits, &leaf);
        }
        result = primary ? leaf : result;
        warns = leaf.token != BUSTER_CHECK_NONE;
        primary = false;
    }
    result.token = warns && fits ? result.token : BUSTER_CHECK_NONE;
    return result;
}

// clang's warnings of the comma operators' left operands in an attribute's expression argument [first, end) (#1447,
// Sema's CheckCommaOperands), each once however often the argument is read: -Wunused-value's, or -Wunused-comparison's
// with its note at an equality, where check_input_attribute_unused_of says the operand's value is unused.
static void check_input_attribute_unused(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    for (uint32_t comma = first; comma < end && !reader->output->resource; comma += 1)
    {
        uint32_t operands[4] = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE };
        uint32_t opener = BUSTER_CHECK_NONE;
        const bool binary = parser->tokens.kinds[comma] == PARSE_KIND_COMMA && check_input_attribute_comma(parser, comma) &&
                            check_input_attribute_operands(parser, comma, first, end, operands, &opener) == 2;
        const CheckAttributeUnused unused = binary ? check_input_attribute_unused_of(reader, operands[0], first, end) :
                                                     (CheckAttributeUnused){ .token = BUSTER_CHECK_NONE };
        const uint64_t key = (uint64_t)CHECK_WARNING_EVENT_UNUSED_RESULT << 32 | ((uint64_t)unused.token + 1);
        if (unused.token != BUSTER_CHECK_NONE && !check_warning_set_contains(&reader->source_warning_keys, key))
        {
            const bool retained = check_warning_set_add(&reader->source_warning_keys, key);
            reader->output->resource |= !retained;
            const bool comparison = unused.message >= BUSTER_CHECK_UNUSED_EQUALITY;
            const CheckWarningEvent event =
            {
                .id = comparison ? CHECK_WARNING_ID_UNUSED_COMPARISON : CHECK_WARNING_ID_UNUSED_VALUE,
                .kind = CHECK_WARNING_EVENT_UNUSED_RESULT,
                .token = unused.token,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = unused.token + 1,
                .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = unused.message },
            };
            const CheckWarningNote note =
            {
                .kind = CHECK_WARNING_NOTE_COMPARISON_ASSIGNMENT,
                .token = unused.token,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = unused.message },
            };
            const bool noted = unused.message == BUSTER_CHECK_UNUSED_EQUALITY || unused.message == BUSTER_CHECK_UNUSED_INEQUALITY;
            if (retained)
            {
                check_input_warning_event(reader, &event, &note, noted ? 1u : 0u);
            }
        }
    }
}

static bool check_input_attribute_expression(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute,
                                              uint32_t first, uint32_t end, bool scoped, CheckValue* value, bool* call_failure)
{
    check_input_attribute_unused(reader, first, end);
    const Parser* parser = reader->parser;
    CheckConstantFailure failure = { .token = BUSTER_CHECK_NONE, .callee = BUSTER_CHECK_NONE };
    uint32_t failed = BUSTER_CHECK_NONE;
    TypeId unused = 0;
    const uint32_t before = reader->output->report_count;
    const bool valid = check_input_constant_read(reader, first, end, false, false, scoped, value, &unused, &failed, &failure);
    *call_failure = false;
    if (!valid && !reader->output->resource && reader->output->report_count == before)
    {
        const uint32_t token = failure.diagnostic != CHECK_DIAG_NONE && failure.token < parser->tokens.count ? failure.token :
                               failed != BUSTER_CHECK_NONE ? failed : first;
        uint32_t open = BUSTER_CHECK_NONE;
        for (uint32_t i = first; i < end && i < token; i += 1)
        {
            if (parser->tokens.kinds[i] == PARSE_KIND_L_PAREN && parser->tree.parents[i] >= token && parser->tree.parents[i] < end)
            {
                open = i;
                *call_failure |= i > first && parser->tokens.kinds[i - 1] == PARSE_KIND_IDENTIFIER;
            }
        }
        *call_failure |= failure.op == CHECK_OP_CALL && failure.diagnostic != CHECK_DIAG_NONE;
        const bool undeclared = failure.diagnostic == CHECK_DIAG_NONE && token < end && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
                                (!scoped || reader->declared_types[token] == 0) &&
                                c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token]) == 0;
        const bool type_tail = undeclared && open != BUSTER_CHECK_NONE && open + 1 < token && check_input_attribute_type_start(reader, open + 1);
        if (undeclared && !type_tail)
        {
            check_input_report(reader, token, CHECK_INPUT_UNDECLARED);
            attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
            attributes->error_first = token;
            attributes->error_end = token + 1;
        }
        else
        {
            const CheckConstantError mapped = check_constant_failure_error(reader, &failure, token);
            const CheckAttributeArgumentError error = type_tail ? CHECK_ARGUMENT_EXPECTED_PAREN : mapped.error;
            check_input_attribute_argument_error(reader, attributes, attribute, type_tail ? token : mapped.token, error, mapped.number);
            if (reader->output->report_count > before)
            {
                CheckInputReport* report = reader->output->reports + before;
                report->type = mapped.type;
                report->value = mapped.value;
                report->type_spelling = check_input_typedef_spelling(reader->output, parser, mapped.spelling != 0 ? mapped.spelling : first);
                report->note_token = type_tail ? open : mapped.note_token;
                report->note_kind = type_tail ? CHECK_INPUT_NOTE_MATCH_PAREN : mapped.note_kind;
            }
        }
    }
    return valid;
}

static bool check_input_attribute_expressions(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    const uint8_t schema = attribute->parse_arguments ? attribute->parsed_schema : (uint8_t)attribute->schema;
    uint32_t first = attribute->argument_first;
    uint32_t index = 0;
    bool valid = true;
    bool call_failure = false;
    while (valid && first < attribute->argument_end)
    {
        const uint32_t end = check_constant_punctuation(parser, first, attribute->argument_end, PARSE_KIND_COMMA);
        const bool identifier = schema == CHECK_ATTRIBUTE_IDENTIFIER_LIST || (index == 0 &&
                               (schema == CHECK_ATTRIBUTE_IDENTIFIER || schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS));
        const bool string = schema == CHECK_ATTRIBUTE_STRING_LIST || (index == 0 && schema == CHECK_ATTRIBUTE_STRING);
        if (string && (first == end || parser->tokens.kinds[first] != PARSE_KIND_STRING))
        {
            // The string slot precedes a trailing empty argument's expression error (section(,), for example).
            check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_STRING_LITERAL, 0);
            valid = false;
        }
        else if (!identifier && !string)
        {
            CheckValue value = { 0 };
            valid = check_input_attribute_expression(reader, attributes, attribute, first, end, false, &value, &call_failure);
        }
        first = end + 1;
        index += 1;
    }
    if (valid && attribute->argument_first < attribute->argument_end && parser->tokens.kinds[attribute->argument_end - 1] == PARSE_KIND_COMMA)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->argument_end, CHECK_ARGUMENT_EXPECTED_EXPRESSION, 0);
        valid = false;
    }
    if (!valid && call_failure && attribute->count_check != BUSTER_CHECK_COUNT_NONE)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, attribute->count_check | attribute->count_bound << 8);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].sort = attribute->argument_end + 1;
            reader->output->reports[before].completion = attribute->argument_end + 1;
        }
    }
    return valid;
}

// Annotate's extra arguments accept any constant expression, including floating values and addresses. Its parameter
// numbering starts with the first extra argument, after the annotation string. The main error names the expression's
// beginning; the note names the failing subexpression or arithmetic operator.
static bool check_input_attribute_annotate(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    uint32_t first = check_constant_punctuation(parser, attribute->argument_first, attribute->argument_end, PARSE_KIND_COMMA) + 1;
    bool valid = true;
    for (uint32_t index = 1; valid && first < attribute->argument_end; index += 1)
    {
        const uint32_t end = check_constant_punctuation(parser, first, attribute->argument_end, PARSE_KIND_COMMA);
        CheckValue value = { 0 };
        bool call_failure = false;
        valid = check_input_attribute_expression(reader, attributes, attribute, first, end, false, &value, &call_failure);
        const bool constant = valid && check_input_const_rvalue(reader, end, &value) && value.constant_kind != CHECK_CONSTANT_NONE &&
                              value.fault == CHECK_FAULT_NONE && (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_GNU_DIVISION)) == 0;
        if (valid && !constant)
        {
            uint32_t note = value.fault != CHECK_FAULT_NONE && value.fault_node < parser->tokens.count ? value.fault_node : first;
            if (value.fault == CHECK_FAULT_NONE)
            {
                for (uint32_t token = first; token < end; token += 1)
                {
                    const uint32_t declaration = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? reader->output->declarations[token] : BUSTER_CHECK_NONE;
                    const TypeId type = declaration < parser->tokens.count ? reader->declared_types[declaration] : 0;
                    if (type != 0 && reader->declaration_kinds[declaration] != C_BINDING_ENUMERATOR &&
                        ((type_qualifiers(type) & BUSTER_TYPE_CONST) == 0 || type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION))
                    {
                        note = token;
                        break;
                    }
                }
            }
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_ANNOTATE_CONSTANT, index);
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].note_token = note;
                reader->output->reports[before].note_kind = value.fault == CHECK_FAULT_DIVIDE_BY_ZERO ? CHECK_INPUT_NOTE_CONSTANT_DIVISION :
                                                                                                  CHECK_INPUT_NOTE_CONSTANT_SUBEXPRESSION;
            }
            valid = false;
        }
        first = end + 1;
    }
    return valid;
}
