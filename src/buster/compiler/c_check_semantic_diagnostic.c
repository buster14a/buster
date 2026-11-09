// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Cold spelling of the common constraint failures (#1107). The driver and corpus use the same renderer;
// the typed graph and retained declaration/case facts decide the error, not a second semantic pass. A changed
// caret never changes the report's completion/publication key. Text belongs to the caller's scratch arena.

static const char* check_semantic_text(Preprocessor* pp, const char* text)
{
    // Do not quietly replace a failed contextual rendering with a successful generic diagnostic.
    pp->diagnostic_collection_failed |= text == NULL;
    return text != NULL ? text : "checker diagnostic capacity exhausted";
}

static String8 check_semantic_name(const Parser* parser, Preprocessor* pp, uint32_t token)
{
    return token < parser->tokens.count ? check_driver_warning_name(pp, parser->tokens.names[token]) : (String8){ 0 };
}

// Only failures need the enclosing function's name. The reader's body-to-declaration column is authoritative;
// walking this cold column avoids retaining another name at every return or redoing lexical lookup.
static uint32_t check_semantic_function(const Parser* parser, const CheckInput* input, uint32_t token)
{
    uint32_t result = BUSTER_CHECK_NONE;
    const uint32_t count = input->token_count;
    for (uint32_t body = 0; body < token && body < count; body += 1)
    {
        const uint32_t declaration = check_sparse_get(input->body_declarations, body);
        if (declaration < count && token < parser->tree.parents[body])
        {
            result = declaration;
        }
    }
    return result;
}

static const char* check_semantic_input(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                       const CheckInputReport* report, CheckWarningSpelling* spell, Arena* scratch)
{
    const uint32_t token = report->token;
    const uint32_t count = input->token_count;
    const String8 name = check_semantic_name(parser, pp, token);
    const char* result = check_driver_input_report_message(input, report);
    if (report->message == CHECK_INPUT_RETURN)
    {
        const uint32_t function = check_semantic_function(parser, input, token);
        const String8 function_name = check_semantic_name(parser, pp, function);
        if (function_name.length != 0)
        {
            result = check_warning_text(scratch, "non-void function '%.*s' should return a value [-Wreturn-mismatch]",
                                        (int)function_name.length, function_name.pointer);
        }
    }
    else if (report->message == CHECK_INPUT_ENUMERATOR_REDEFINITION && name.length != 0)
    {
        result = check_warning_text(scratch, "redefinition of enumerator '%.*s'", (int)name.length, name.pointer);
    }
    else if (report->message == CHECK_INPUT_ARRAY_LITERAL_MISMATCH)
    {
        // GNU's array initialized by a compound literal of another array type (#1447): both types as written.
        const String8 array = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
        const String8 literal = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, 0);
        result = array.length == 0 || literal.length == 0 ? NULL : check_warning_text(scratch, "cannot initialize array of type %.*s with array of type %.*s",
                                                                                     (int)array.length, array.pointer, (int)literal.length, literal.pointer);
    }
    else if ((report->message == CHECK_INPUT_REDECLARATION || report->message == CHECK_INPUT_CONFLICTING_TYPES) && token < count && name.length != 0)
    {
        const uint32_t previous = report->subject - 1;
        const uint8_t kind = input->declaration_kinds[token];
        if (report->detail == CHECK_REDECLARATION_OBJECT_TYPE || report->detail == CHECK_REDECLARATION_OBJECT_TYPE_EXTERN)
        {
            const String8 current = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
            // The prior type is the noted declaration's, spelled as that one spells it (review on #1453).
            const uint32_t noted = report->note_token < count ? report->note_token : previous;
            const uint32_t prior_spelling = noted < count ? check_input_typedef_spelling(input, parser, noted) : 0;
            const String8 prior = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, prior_spelling);
            result = current.length == 0 || prior.length == 0 ? NULL : check_warning_text(scratch,
                "%s of '%.*s' with a different type: %.*s vs %.*s",
                report->detail == CHECK_REDECLARATION_OBJECT_TYPE_EXTERN ? "redeclaration" : "redefinition", (int)name.length, name.pointer,
                (int)current.length, current.pointer, (int)prior.length, prior.pointer);
        }
        else if (report->detail == CHECK_REDECLARATION_TYPEDEF)
        {
            const String8 current = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
            const uint32_t noted = report->note_token < count ? report->note_token : previous;
            const uint32_t prior_spelling = noted < count ? check_input_typedef_spelling(input, parser, noted) : 0;
            const String8 prior = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, prior_spelling);
            result = current.length == 0 || prior.length == 0 ? NULL : check_warning_text(scratch, "typedef redefinition with different types (%.*s vs %.*s)",
                                                                                         (int)current.length, current.pointer, (int)prior.length, prior.pointer);
        }
        else if (report->detail == CHECK_REDECLARATION_KIND)
        {
            result = check_warning_text(scratch, "redefinition of '%.*s' as different kind of symbol", (int)name.length, name.pointer);
        }
        else if (report->detail == CHECK_REDECLARATION_THREAD_LOCAL || report->detail == CHECK_REDECLARATION_NON_THREAD_LOCAL)
        {
            const char* format = report->detail == CHECK_REDECLARATION_THREAD_LOCAL ?
                                 "thread-local declaration of '%.*s' follows non-thread-local declaration" :
                                 "non-thread-local declaration of '%.*s' follows thread-local declaration";
            result = check_warning_text(scratch, format, (int)name.length, name.pointer);
        }
        else if (report->detail > CHECK_REDECLARATION_GENERIC && report->detail <= CHECK_REDECLARATION_NON_STATIC)
        {
            const char* format = report->detail == CHECK_REDECLARATION_FUNCTION_TYPE ? "conflicting types for '%.*s'" :
                                 report->detail == CHECK_REDECLARATION_PARAMETER ? "redefinition of parameter '%.*s'" :
                                 report->detail == CHECK_REDECLARATION_STATIC ? "static declaration of '%.*s' follows non-static declaration" :
                                 report->detail == CHECK_REDECLARATION_NON_STATIC ? "non-static declaration of '%.*s' follows static declaration" :
                                 "redefinition of '%.*s'";
            result = check_warning_text(scratch, format, (int)name.length, name.pointer);
        }
        else if (kind == C_BINDING_ENUMERATOR && previous < count && input->declaration_kinds[previous] == kind)
        {
            result = check_warning_text(scratch, "redefinition of enumerator '%.*s'", (int)name.length, name.pointer);
        }
        else if (previous < count && input->declaration_kinds[previous] == kind)
        {
            const uint32_t linkage = check_input_linkage(input->declaration_bits, token);
            const uint32_t prior_linkage = check_input_linkage(input->declaration_bits, previous);
            if (linkage != 0 && prior_linkage != 0 && linkage != prior_linkage)
            {
                result = check_warning_text(scratch, "%s declaration of '%.*s' follows %s declaration",
                                            linkage == 1 ? "static" : "non-static", (int)name.length, name.pointer,
                                            prior_linkage == 1 ? "static" : "non-static");
            }
            else if (report->message == CHECK_INPUT_CONFLICTING_TYPES)
            {
                result = check_warning_text(scratch, "conflicting types for '%.*s'", (int)name.length, name.pointer);
            }
        }
    }
    else if (token < count && (report->message == CHECK_INPUT_INCOMPLETE_OBJECT || report->message == CHECK_INPUT_INCOMPLETE_TENTATIVE))
    {
        const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, input->declared_types[token], 0);
        result = type.length == 0 ? NULL : report->message == CHECK_INPUT_INCOMPLETE_TENTATIVE ?
                 check_warning_text(scratch, "tentative definition has type %.*s that is never completed", (int)type.length, type.pointer) :
                 check_warning_text(scratch, "variable has incomplete type %.*s", (int)type.length, type.pointer);
    }
    return check_semantic_text(pp, result);
}

// A preprocessed token's spelling, including generated and macro-expanded tokens. The spelling API's buffer
// bound is the same one used by the source adapter; compact #embed tokens have no single-token spelling.
static String8 check_semantic_token(const Parser* parser, Preprocessor* pp, uint32_t token, Arena* scratch)
{
    String8 result = { 0 };
    const uint32_t source = token < parser->tokens.count ? parser->tokens.sources[token] : BUSTER_CHECK_NONE;
    // The output's columns, which the backend may have given back (pp_release_output).
    pp_output_ensure(pp);
    if (source < pp->output.count && pp->output.kinds[source] != BUSTER_PP_KIND_EMBED)
    {
        const uint8_t kind = pp->output.kinds[source];
        // Names carry intern handles rather than end locations, and pp_spelling borrows their interned bytes.
        const bool named = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT);
        const uint64_t capacity = named ? 0 : kind == BUSTER_PP_KIND_INTEGER ? 24 : (uint64_t)pp->output.values[source] - pp->output.locations[source] + 1;
        uint8_t* buffer = !named ? arena_push(scratch, capacity, 1) : NULL;
        const uint8_t* bytes = NULL;
        const uint32_t length = named || buffer != NULL ? pp_spelling(pp, kind, pp->output.values[source], pp->output.locations[source], buffer, &bytes) : 0;
        result = (String8){ .pointer = (char*)bytes, .length = length };
    }
    return result;
}

static bool check_semantic_binary(uint8_t role)
{
    return (role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_COMMA || role == PARSE_ROLE_OPEN_TERNARY || role == PARSE_ROLE_CLOSE_TERNARY;
}

// Spell a failed assertion's requirement with expression spacing, not the source's arbitrary whitespace. This
// is a bounded token pass, including macro expansions; it never walks the expression recursively.
static const char* check_semantic_requirement(const Parser* parser, Preprocessor* pp, uint32_t first, uint32_t end, Arena* scratch)
{
    const uint32_t count = end - first;
    String8* parts = BUSTER_ARENA_PUSH_ARRAY(scratch, String8, count);
    bool valid = parts != NULL;
    uint64_t length = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        parts[i] = check_semantic_token(parser, pp, first + i, scratch);
        valid = parts[i].length != 0 && parts[i].length < UINT64_MAX - length - 1;
        length += valid ? parts[i].length + 1 : 0;
    }
    char* result = valid && length < UINT64_MAX ? BUSTER_ARENA_PUSH_ARRAY(scratch, char, length + 1) : NULL;
    uint64_t at = 0;
    for (uint32_t i = 0; result != NULL && i < count; i += 1)
    {
        const uint8_t role = parser->tree.roles[first + i];
        const uint8_t previous = i != 0 ? parser->tree.roles[first + i - 1] : PARSE_ROLE_NONE;
        const bool word = i != 0 && pp_word_character((uint8_t)parts[i - 1].pointer[parts[i - 1].length - 1], pp_dollar_punctuation(&pp->options)) &&
                                   pp_word_character((uint8_t)parts[i].pointer[0], pp_dollar_punctuation(&pp->options));
        if (i != 0 && (check_semantic_binary(role) || check_semantic_binary(previous) || word))
        {
            result[at++] = ' ';
        }
        memcpy(result + at, parts[i].pointer, parts[i].length);
        at += parts[i].length;
    }
    if (result != NULL)
    {
        result[at] = 0;
    }
    return result;
}

static const char* check_semantic_assert(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckReport* report,
                                        uint32_t* location, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    uint32_t keyword = BUSTER_CHECK_NONE;
    uint32_t close = count;
    for (uint32_t token = 0; token < report->token && token + 1 < count; token += 1)
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_STATIC_ASSERT && parser->tree.parents[token + 1] > report->token)
        {
            keyword = token;
            close = parser->tree.parents[token + 1];
        }
    }
    const char* result = "static assertion failed";
    if (keyword < count && close < count)
    {
        uint32_t first = keyword + 2;
        uint32_t end = check_constant_punctuation(parser, first, close, PARSE_KIND_COMMA);
        const uint32_t strings = end + 1;
        while (first + 1 < end && parser->tree.roles[first] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[first] == end - 1)
        {
            first += 1;
            end -= 1;
        }
        *location = first;
        const bool literal = first + 1 == end && (parser->tokens.kinds[first] == PARSE_KIND_NUMBER || parser->tokens.kinds[first] == PARSE_KIND_CHARACTER);
        const char* requirement = literal ? NULL : check_semantic_requirement(parser, pp, first, end, scratch);
        const bool message = strings < close;
        CheckStringPart* parts = message ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckStringPart, close - strings) : NULL;
        bool valid = literal || requirement != NULL;
        valid &= !message || parts != NULL;
        for (uint32_t token = strings; valid && token < close; token += 1)
        {
            const String8 part = check_semantic_token(parser, pp, token, scratch);
            valid = part.length != 0 && part.length <= UINT32_MAX && parser->tokens.kinds[token] == PARSE_KIND_STRING;
            parts[token - strings] = (CheckStringPart){ .bytes = (const uint8_t*)part.pointer, .length = (uint32_t)part.length };
        }
        CheckStringLiteral decoded = valid && message ? check_literal_strings(parts, close - strings, parser->language.standard >= 2023, scratch) : (CheckStringLiteral){ 0 };
        // A message of escapes clang takes that the fast decoding refuses (#1447: a delimited one, a named universal
        // character name, an unknown one), read as clang reads them.
        if (valid && message && !decoded.valid)
        {
            decoded = check_literal_strings_extended(parts, close - strings, parser->language.standard >= 2023, scratch);
        }
        valid &= !message || (decoded.valid && decoded.byte_count != 0 && decoded.byte_count - 1 <= INT_MAX);
        result = valid ? check_warning_text(scratch, "static assertion failed%s%s%s%s%.*s",
                                            literal ? "" : " due to requirement '", literal ? "" : requirement, literal ? "" : "'",
                                            message ? ": " : "", message ? (int)(decoded.byte_count - 1) : 0, message ? (const char*)decoded.bytes : "") : NULL;
    }
    (void)input;
    return check_semantic_text(pp, result);
}

// The typedef-specifier declaration an operand's type is spelled with (check_input_typedef_spelling), or 0: its object's
// declaration's, through parentheses, dereferences and subscripts to the named object. Clang's type keeps that sugar:
// `(*p)++` of a `v4si *p` prints 'v4si'. A member's own typedef isn't found (a record's members keep no declaration), so
// `x.f++` of a member `uu f` prints its type without it.
static uint32_t check_semantic_operand_spelling(const Parser* parser, const CheckInput* input, uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    uint32_t result = 0;
    uint32_t at = node;
    for (uint32_t steps = 0; result == 0 && at < graph->count && steps < graph->count; steps += 1)
    {
        const CheckOp op = graph->ops[at];
        const uint32_t token = graph->tokens[at];
        const uint32_t declaration = token < input->token_count && input->declarations != NULL && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ?
                                     input->declarations[token] : 0;
        result = declaration != 0 && declaration < input->token_count ? check_input_typedef_spelling(input, parser, declaration) : 0;
        const bool through = declaration == 0 && (op == CHECK_OP_VALUE || op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT);
        at = result == 0 && through ? graph->operand0[at] : BUSTER_CHECK_NONE;
    }
    return result;
}

// c_check_common_warnings.c's, included after this file.
static uint32_t check_common_subtree_start(const Parser* parser, uint32_t root);

// An operand's type as clang names it (#1447): an array's or a function's pointer (which the conversion made, so the
// table has it); an operator's operand's converted to a value (`value`), unqualified, without its extended qualifiers,
// where a conversion's source keeps its own.
static TypeId check_semantic_value_type(const TypeTable* types, TypeId type, bool value_only)
{
    TypeId value = type != 0 && value_only ? type_unqualified(type) : type;
    value = value != 0 && value_only && type_extended_qualifiers(types, value) != 0 ? type_unqualified(type_strip_aligned(types, value)) : value;
    const uint8_t kind = value != 0 ? type_kind(types, type_strip_aligned(types, value)) : BUSTER_TYPE_KIND_NONE;
    value = type_kind_is_array(kind) ? type_find_pointer(types, type_element(types, type_strip_aligned(types, value))) :
            kind == BUSTER_TYPE_KIND_FUNCTION ? type_find_pointer(types, value) : value;
    return value;
}

// clang's note at a called function's parameter (#1447, its "passing argument to parameter 'x' here"), where the callee is a
// function's name: at the `argument`-th parameter's name in the declaration the name finds, or just past an unnamed one,
// as clang places an abstract declarator's parameter.
static void check_semantic_parameter_note(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t callee, uint32_t argument,
                                          PPOutputDiagnosticNote* note, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t declaration = callee < input->token_count && parser->tokens.kinds[callee] == PARSE_KIND_IDENTIFIER ? input->declarations[callee] : BUSTER_CHECK_NONE;
    const bool function = declaration < input->token_count && input->declaration_kinds[declaration] == C_BINDING_FUNCTION && declaration + 1 < count &&
                          parser->tokens.kinds[declaration + 1] == PARSE_KIND_L_PAREN;
    const uint32_t close = function ? parser->tree.parents[declaration + 1] : 0;
    // The parameter's tokens: from after the `(` or the comma before it, to its comma or the `)`, groups skipped.
    uint32_t at = declaration + 2;
    uint32_t first = at;
    uint32_t end = at;
    bool found = false;
    for (uint32_t index = 0; function && !found && close < count && at <= close; index += 1)
    {
        first = at;
        while (at < close && parser->tokens.kinds[at] != PARSE_KIND_COMMA)
        {
            const uint8_t kind = parser->tokens.kinds[at];
            const bool open = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
            at = open && parser->tree.parents[at] > at && parser->tree.parents[at] < close ? parser->tree.parents[at] + 1 : at + 1;
        }
        end = at;
        found = index == argument && end > first;
        at += 1;
    }
    uint32_t name = BUSTER_CHECK_NONE;
    for (uint32_t t = first; found && name == BUSTER_CHECK_NONE && t < end; t += 1)
    {
        // An attribute's arguments are operands too (address_space's name is one): the parameter's is the first
        // operand identifier outside every attribute group.
        bool attribute = false;
        for (uint32_t at = t; !attribute && at < end;)
        {
            const uint32_t parent = parser->tree.parents[at];
            const uint8_t role = parent < parser->tokens.count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
            attribute = role == PARSE_ROLE_CLOSE_ATTRIBUTE || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ||
                        role == PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT || role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT ||
                        role == PARSE_ROLE_CLOSE_ATTRIBUTE_LIST;
            at = parent < parser->tokens.count && parent > at && parent < end ? parent : end;
        }
        name = !attribute && parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && parser->tree.roles[t] == PARSE_ROLE_OPERAND ? t : name;
    }
    if (found)
    {
        const String8 spelled = name != BUSTER_CHECK_NONE ? check_semantic_name(parser, pp, name) : (String8){ "", 0 };
        *note = (PPOutputDiagnosticNote){
            .token = parser->tokens.sources[name != BUSTER_CHECK_NONE ? name : end],
            .message = spelled.length != 0 ? check_warning_text(scratch, "passing argument to parameter '%.*s' here", (int)spelled.length, spelled.pointer) :
                                             "passing argument to parameter here",
        };
    }
}

// The address spaces' errors (#1447), worded as clang's: a conversion's "changes address space of pointer" (an assignment's,
// an initialization's, a return's at its expression, an argument's) or of "nested pointer" one level down, and a
// subtraction's or a conditional's pointers to spaces that don't overlap
// (err_typecheck_op_on_nonoverlapping_address_space_pointers, its two spaces as clang prints them). An argument's has
// clang's note at the called function's parameter (check_semantic_parameter_note).
static const char* check_semantic_address_space(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                                const CheckReport* report, CheckWarningSpelling* spell, uint32_t* location, PPOutputDiagnosticNote* note,
                                                Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t node = report->node;
    const uint32_t token = report->token;
    const CheckOp op = graph->ops[node];
    const char* result = NULL;
    uint32_t from_node = BUSTER_CHECK_NONE;
    TypeId from = 0;
    TypeId to = 0;
    const char* format = NULL;
    const char* nested = report->diagnostic == CHECK_DIAG_ADDRESS_SPACE_NESTED ? "nested " : "";
    if (report->diagnostic == CHECK_DIAG_ADDRESS_SPACE_OVERLAP)
    {
        const bool conditional = op == CHECK_OP_CONDITIONAL;
        const uint32_t first = conditional ? graph->operand1[node] : graph->operand0[node];
        const uint32_t second = conditional ? graph->operand2[node] : graph->operand1[node];
        const String8 left = first < stream->count ? check_driver_warning_type(parser, pp, input, types, spell, scratch,
                                                                                check_semantic_value_type(types, check_stream_type(stream, first), true),
                                                                                check_semantic_operand_spelling(parser, input, first)) : (String8){ "", 0 };
        const String8 right = second < stream->count ? check_driver_warning_type(parser, pp, input, types, spell, scratch,
                                                                                  check_semantic_value_type(types, check_stream_type(stream, second), true),
                                                                                  check_semantic_operand_spelling(parser, input, second)) : (String8){ "", 0 };
        result = left.length != 0 && right.length != 0 ?
                 check_warning_text(scratch, "%s operands of type  (%.*s and %.*s) which are pointers to non-overlapping address spaces",
                                    conditional ? "conditional operator with the second and third" : "arithmetic operation with", (int)left.length, left.pointer,
                                    (int)right.length, right.pointer) : NULL;
        // A conditional's at its `?`, whose parent is the node's `:`.
        uint32_t question = token;
        while (conditional && question != 0 && question < parser->tokens.count &&
               !(parser->tree.roles[question] == PARSE_ROLE_OPEN_TERNARY && parser->tree.parents[question] == token))
        {
            question -= 1;
        }
        *location = conditional && question < parser->tokens.count && parser->tree.roles[question] == PARSE_ROLE_OPEN_TERNARY ? question : token;
    }
    else if (op == CHECK_OP_ASSIGN)
    {
        from_node = graph->operand1[node];
        to = graph->operand0[node] < stream->count ? check_stream_type(stream, graph->operand0[node]) : 0;
        format = "assigning %.*s to %.*s changes address space of %spointer";
    }
    else if (op == CHECK_OP_INITIALIZE)
    {
        from_node = graph->operand0[node];
        to = graph->types[node];
        format = "initializing %.*s with an expression of type %.*s changes address space of %spointer";
        for (uint32_t declaration = 0; declaration < input->token_count; declaration += 1)
        {
            if (check_sparse_get(input->initializer_nodes, declaration) == node)
            {
                *location = declaration;
                break;
            }
        }
    }
    else if (op == CHECK_OP_CONVERT && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_RETURN)
    {
        from_node = graph->operand0[node];
        to = graph->types[node];
        format = "returning %.*s from a function with result type %.*s changes address space of %spointer";
        *location = from_node < graph->count ? check_common_subtree_start(parser, graph->tokens[from_node]) : token;
    }
    else if (op == CHECK_OP_CALL && graph->argument_tokens != NULL)
    {
        const uint32_t offset = graph->argument_offsets[node];
        uint32_t argument = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; argument == BUSTER_CHECK_NONE && i < graph->argument_counts[node]; i += 1)
        {
            argument = graph->argument_tokens[offset + i] == token ? i : argument;
        }
        const uint32_t callee = graph->operand0[node];
        TypeId function = callee < stream->count ? type_strip_aligned(types, check_stream_type(stream, callee)) : 0;
        function = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, function)) : function;
        uint32_t parameter_count = 0;
        const TypeId* parameters = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION ? type_parameters(types, function, &parameter_count) : NULL;
        from_node = argument != BUSTER_CHECK_NONE ? graph->argument_nodes[offset + argument] : BUSTER_CHECK_NONE;
        to = argument < parameter_count ? parameters[argument] : 0;
        format = "passing %.*s to parameter of type %.*s changes address space of %spointer";
        check_semantic_parameter_note(parser, pp, input, callee < graph->count ? graph->tokens[callee] : BUSTER_CHECK_NONE, argument, note, scratch);
    }
    if (format != NULL)
    {
        from = from_node < stream->count ? check_semantic_value_type(types, check_stream_type(stream, from_node), false) : 0;
        const String8 source = from != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, from, check_semantic_operand_spelling(parser, input, from_node)) :
                                           (String8){ "", 0 };
        const String8 target = to != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, to, 0) : (String8){ "", 0 };
        const bool initializing = op == CHECK_OP_INITIALIZE;
        const String8 first = initializing ? target : source;
        const String8 second = initializing ? source : target;
        result = source.length != 0 && target.length != 0 ?
                 check_warning_text(scratch, format, (int)first.length, first.pointer, (int)second.length, second.pointer, nested) : NULL;
    }
    return result != NULL ? result : check_driver_report_message(report);
}

// The switch keyword whose condition `close` closes, or BUSTER_CHECK_NONE: the one before the condition's `(`.
static uint32_t check_semantic_switch(const Parser* parser, uint32_t close)
{
    uint32_t open = close;
    while (open != 0 && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == close))
    {
        open -= 1;
    }
    const bool found = open != 0 && parser->tokens.kinds[open - 1] == PARSE_KIND_SWITCH && parser->tree.parents[open - 1] == close;
    return found ? open - 1 : BUSTER_CHECK_NONE;
}

// `note`, where the message has one (its message NULL where not): a const variable's "declared const here" (#147).
// Clang's "forward declaration of 'struct S'" note of an incomplete tag (`bare`, unqualified), at the tag's declaring
// name: its first token that declares a tag of this type. None for a type of no such name.
static PPOutputDiagnosticNote check_semantic_forward_note(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                                          CheckWarningSpelling* spell, TypeId bare, Arena* scratch)
{
    PPOutputDiagnosticNote result = { 0 };
    const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_VOID;
    const bool tag = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
    const uint32_t name = tag ? types->tags.name[types->types.index[type_index(bare)]] : 0;
    uint32_t declared = BUSTER_CHECK_NONE;
    for (uint32_t t = 0; name != 0 && declared == BUSTER_CHECK_NONE && t < input->token_count; t += 1)
    {
        declared = parser->tokens.names[t] == name && input->declarations[t] == t && input->declaration_kinds[t] == C_BINDING_TAG_KIND &&
                   type_unqualified(input->declared_types[t]) == bare ? t : BUSTER_CHECK_NONE;
    }
    const String8 named = declared != BUSTER_CHECK_NONE ? check_driver_warning_type(parser, pp, input, types, spell, scratch, bare, 0) : (String8){ "", 0 };
    if (named.length != 0)
    {
        result = (PPOutputDiagnosticNote){
            .token = parser->tokens.sources[declared],
            .message = check_warning_text(scratch, "forward declaration of %.*s", (int)named.length, named.pointer),
        };
    }
    return result;
}

static const char* check_semantic_expression(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            const TypeTable* types, const CheckReport* report, CheckWarningSpelling* spell, uint32_t* location,
                                            PPOutputDiagnosticNote* note, Arena* scratch)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t node = report->node;
    const uint32_t count = graph->count;
    const uint32_t token = report->token;
    const CheckOp op = node < count ? graph->ops[node] : CHECK_OP_COUNT;
    const uint32_t operand = node < count ? graph->operand0[node] : BUSTER_CHECK_NONE;
    const TypeId operand_type = operand < stream->count ? check_stream_type(stream, operand) : 0;
    const char* result = check_driver_report_message(report);
    *location = token;
    *note = (PPOutputDiagnosticNote){ 0 };
    if (report->message == CHECK_MESSAGE_STATIC_ASSERT)
    {
        result = check_semantic_assert(parser, pp, input, report, location, scratch);
    }
    else if (report->message == CHECK_MESSAGE_STATIC_INITIALIZER && node < count && graph->tokens[node] == token)
    {
        // At clang's culprit (#1447): an initialization's value's, not the initializer's first token. (A report's token is
        // its node's; the reports' walk's model pairs others, which keep their token.)
        const bool initialize = op == CHECK_OP_INITIALIZE && operand < count;
        const uint32_t value = initialize ? operand : node;
        const TypeId target = initialize && node < stream->count ? check_stream_type(stream, node) : 0;
        *location = check_init_culprit_at(graph, stream, types, parser, value, token, target);
    }
    else if (report->message == CHECK_MESSAGE_ACTION && node < count)
    {
        if (report->diagnostic == CHECK_DIAG_MEMBER && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && token + 1 < parser->tokens.count)
        {
            const TypeId record = op == CHECK_OP_ARROW ? type_element(types, operand_type) : operand_type;
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, type_unqualified(record), 0);
            const String8 name = check_semantic_name(parser, pp, token + 1);
            *location = token + 1;
            result = type.length != 0 ? check_warning_text(scratch, "no member named '%.*s' in %.*s", (int)name.length, name.pointer, (int)type.length, type.pointer) : NULL;
        }
        else if (report->diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED && op == CHECK_OP_POSITIVE && operand_type != 0 && graph->tokens[node] < parser->tokens.count &&
                 parser->tree.roles[graph->tokens[node]] == PARSE_ROLE_CLOSE_CONDITION && graph->tokens[node] != 0 &&
                 check_semantic_switch(parser, graph->tokens[node]) != BUSTER_CHECK_NONE)
        {
            // A switch's condition of no arithmetic type, which its promotion (a unary plus) refuses: clang's
            // err_typecheck_statement_requires_integer at the switch (#1447), as check_semantic_constraint's.
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, operand_type, check_input_operand_type_spelling(input, parser, operand));
            *location = check_semantic_switch(parser, graph->tokens[node]);
            result = type.length != 0 ? check_warning_text(scratch, "statement requires expression of integer type (%.*s invalid)", (int)type.length, type.pointer) :
                                        NULL;
        }
        else if (report->diagnostic == CHECK_DIAG_ARITHMETIC_REQUIRED && op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT &&
                 operand_type != 0 && !check_action_steppable(types, operand_type))
        {
            // clang's err_typecheck_illegal_increment_decrement, the operand's type as written (its qualifiers kept),
            // an _Atomic one's value type.
            const TypeId stripped = type_strip_aligned(types, operand_type);
            const TypeId value = type_kind(types, stripped) == BUSTER_TYPE_KIND_ATOMIC ? types->types.base[type_index(stripped)] : operand_type;
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, value, check_semantic_operand_spelling(parser, input, operand));
            const bool up = op == CHECK_OP_PRE_INCREMENT || op == CHECK_OP_POST_INCREMENT;
            result = type.length != 0 ? check_warning_text(scratch, "cannot %s value of type %.*s", up ? "increment" : "decrement", (int)type.length, type.pointer) : NULL;
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPLETE && ((op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT) || op == CHECK_OP_ADD ||
                 op == CHECK_OP_SUBTRACT || op == CHECK_OP_ADD_ASSIGN || op == CHECK_OP_SUBTRACT_ASSIGN || op == CHECK_OP_SUBSCRIPT))
        {
            // clang's arithmetic on a pointer to an incomplete type (#147, err_typecheck_arithmetic_incomplete_or_sizeless_type,
            // a subscript's err_subscript_incomplete_or_sizeless_type at its base): the pointee as written, with clang's
            // "forward declaration" note at the tag's declaring name.
            const uint32_t second = node < count ? graph->operand1[node] : BUSTER_CHECK_NONE;
            const TypeId second_type = second < stream->count ? check_stream_type(stream, second) : 0;
            const bool first_pointer = operand_type != 0 && check_action_kind(types, operand_type) == BUSTER_TYPE_KIND_POINTER;
            const uint32_t pointer = first_pointer ? operand : second;
            const TypeId pointer_type = first_pointer ? operand_type : second_type;
            const TypeId pointee = pointer_type != 0 && check_action_kind(types, pointer_type) == BUSTER_TYPE_KIND_POINTER ?
                                   type_element(types, type_unqualified(type_strip_aligned(types, pointer_type))) : 0;
            const TypeId bare = pointee != 0 ? type_unqualified(type_strip_aligned(types, pointee)) : 0;
            const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_VOID;
            const bool tag = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
            const String8 type = tag ? check_driver_warning_type(parser, pp, input, types, spell, scratch, pointee, check_semantic_operand_spelling(parser, input, pointer)) :
                                       (String8){ "", 0 };
            if (type.length != 0)
            {
                result = check_warning_text(scratch, op == CHECK_OP_SUBSCRIPT ? "subscript of pointer to incomplete type %.*s" :
                                                     "arithmetic on a pointer to an incomplete type %.*s", (int)type.length, type.pointer);
                // (A subscript's at its `[`, the opener of its node's `]`.)
                uint32_t open = token;
                while (op == CHECK_OP_SUBSCRIPT && open != 0 && open < parser->tokens.count &&
                       !(parser->tokens.kinds[open] == PARSE_KIND_L_SQUARE && parser->tree.parents[open] == token))
                {
                    open -= 1;
                }
                *location = open;
                *note = check_semantic_forward_note(parser, pp, input, types, spell, bare, scratch);
            }
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPLETE && (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF) && token < parser->tokens.count &&
                 graph->tokens[node] == token)
        {
            // clang's err_sizeof_alignof_incomplete_or_sizeless_type, named by the keyword as clang spells it (`alignof` for
            // _Alignof and alignof, `__alignof` for GNU's): a type name's at the keyword, its typedef as written; an
            // expression's at the expression, its type as the operand's. The tag's "forward declaration" note.
            const bool expression = operand < stream->count;
            const TypeId named = expression ? operand_type : graph->types != NULL ? graph->types[node] : 0;
            uint32_t spelling = expression ? check_semantic_operand_spelling(parser, input, operand) : 0;
            const uint32_t close = !expression && token + 1 < parser->tokens.count && parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN ?
                                   parser->tree.parents[token + 1] : token;
            for (uint32_t type_name = token + 2; !expression && spelling == 0 && type_name < close && close < parser->tokens.count; type_name += 1)
            {
                const uint32_t resolved = parser->tokens.kinds[type_name] == PARSE_KIND_IDENTIFIER && input->declarations != NULL ? input->declarations[type_name] : 0;
                spelling = resolved != 0 && resolved < input->token_count && input->declaration_kinds[resolved] == C_BINDING_TYPEDEF ? resolved : 0;
            }
            const String8 type = named != 0 ? check_driver_warning_type(parser, pp, input, types, spell, scratch, named, spelling) : (String8){ "", 0 };
            const uint32_t keyword = parser->tokens.names[token];
            const char* word = op == CHECK_OP_SIZEOF ? "sizeof" : keyword == C_NAME_ALIGNOF_C11 || keyword == C_NAME_ALIGNOF ? "alignof" : "__alignof";
            if (type.length != 0)
            {
                result = check_warning_text(scratch, "invalid application of '%s' to an incomplete type %.*s", word, (int)type.length, type.pointer);
                *location = expression ? token + 1 : token;
                *note = check_semantic_forward_note(parser, pp, input, types, spell, type_unqualified(type_strip_aligned(types, named)), scratch);
            }
        }
        else if (report->diagnostic == CHECK_DIAG_CALL && op == CHECK_OP_CALL && operand_type != 0 && graph->tokens[node] == token)
        {
            // clang's err_typecheck_call_not_function at the call's `(`, the callee's type as written once loaded: without
            // its qualifiers and _Atomic, so that a typedef's name stays only where it is the loaded type (`const T`'s T,
            // not a `const int` typedef's; review on #1802).
            const TypeId plain = type_unqualified(operand_type);
            const TypeId loaded = type_kind(types, plain) == BUSTER_TYPE_KIND_ATOMIC ? type_unqualified(types->types.base[type_index(plain)]) : plain;
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, loaded, check_semantic_operand_spelling(parser, input, operand));
            uint32_t open = token;
            while (open != 0 && open < parser->tokens.count && !(parser->tokens.kinds[open] == PARSE_KIND_L_PAREN && parser->tree.parents[open] == token))
            {
                open -= 1;
            }
            if (type.length != 0)
            {
                result = check_warning_text(scratch, "called object type %.*s is not a function or function pointer", (int)type.length, type.pointer);
                *location = open;
            }
        }
        else if (report->diagnostic == CHECK_DIAG_LVALUE_REQUIRED &&
                 (op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)))
        {
            result = "expression is not assignable";
        }
        else if (report->diagnostic == CHECK_DIAG_MODIFIABLE_REQUIRED && operand_type != 0)
        {
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, operand_type, 0);
            const uint8_t kind = check_action_kind(types, operand_type);
            if (type_kind_is_array(kind))
            {
                result = type.length != 0 ? check_warning_text(scratch, "array type %.*s is not assignable", (int)type.length, type.pointer) : NULL;
            }
            else if ((type_qualifiers(operand_type) & BUSTER_TYPE_CONST) != 0)
            {
                // clang's DiagnoseConstAssignment: through an array's subscripts and a record's `.` members to the variable
                // they reach, named with its type as declared (an array's whole, its typedef kept), and its "declared const
                // here" note at its name. A member that is const itself clang names (its declaration this checker doesn't
                // keep), and any other lvalue's (an arrow's, a pointer's target) is the generic error.
                uint32_t named = operand;
                bool walked = true;
                bool through_arrow = false;
                while (walked && !through_arrow && named < count &&
                       (graph->ops[named] == CHECK_OP_SUBSCRIPT || graph->ops[named] == CHECK_OP_MEMBER || graph->ops[named] == CHECK_OP_ARROW))
                {
                    const uint32_t base = graph->operand0[named];
                    const TypeId base_type = base < stream->count ? check_stream_type(stream, base) : 0;
                    if (graph->ops[named] == CHECK_OP_SUBSCRIPT)
                    {
                        walked = base_type != 0 && type_kind_is_array(check_action_kind(types, base_type));
                    }
                    else
                    {
                        // The field among the record's own members, by the name after the `.` or `->`: not one that is const
                        // itself. An arrow's base is the pointer variable clang names (its pointee the const object), alone.
                        through_arrow = graph->ops[named] == CHECK_OP_ARROW;
                        const TypeId pointee = through_arrow && base_type != 0 && check_action_kind(types, base_type) == BUSTER_TYPE_KIND_POINTER ?
                                               type_element(types, type_unqualified(type_strip_aligned(types, base_type))) : 0;
                        const TypeId record = through_arrow ? (pointee != 0 ? type_unqualified(type_strip_aligned(types, pointee)) : 0) :
                                              base_type != 0 ? type_unqualified(type_strip_aligned(types, base_type)) : 0;
                        const uint8_t record_kind = record != 0 ? type_kind(types, record) : BUSTER_TYPE_KIND_VOID;
                        const uint32_t field_name = graph->tokens[named] + 1 < parser->tokens.count ? parser->tokens.names[graph->tokens[named] + 1] : 0;
                        bool plain = false;
                        if ((record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION) && field_name != 0)
                        {
                            const uint32_t tag = types->types.index[type_index(record)];
                            const uint32_t begin = types->tags.member_begin[tag];
                            for (uint32_t m = begin; m < begin + types->tags.member_count[tag]; m += 1)
                            {
                                // (An array member's constness is its innermost element's.)
                                TypeId field = types->members.type[m];
                                for (uint32_t depth = 0; depth < 64 && type_kind_is_array(check_action_kind(types, field)); depth += 1)
                                {
                                    field = type_element(types, type_strip_aligned(types, field));
                                }
                                plain |= types->members.name[m] == field_name && (type_qualifiers(field) & BUSTER_TYPE_CONST) == 0;
                            }
                        }
                        walked = plain;
                    }
                    named = walked ? base : named;
                }
                const uint32_t target = named < count ? graph->tokens[named] : BUSTER_CHECK_NONE;
                const uint32_t declaration = target < input->token_count ? input->declarations[target] : BUSTER_CHECK_NONE;
                const String8 name = target < parser->tokens.count ? check_semantic_name(parser, pp, target) : (String8){ "", 0 };
                const bool variable = walked && named < count && graph->ops[named] == CHECK_OP_VALUE && declaration < input->token_count && name.length != 0;
                const String8 declared = variable ? check_driver_warning_type(parser, pp, input, types, spell, scratch, check_stream_type(stream, named),
                                                                              check_semantic_operand_spelling(parser, input, named)) : (String8){ "", 0 };
                result = !variable ? "read-only variable is not assignable" :
                         declared.length != 0 ? check_warning_text(scratch, "cannot assign to variable '%.*s' with const-qualified type %.*s",
                                                                   (int)name.length, name.pointer, (int)declared.length, declared.pointer) : NULL;
                if (variable && result != NULL)
                {
                    *note = (PPOutputDiagnosticNote){
                        .token = parser->tokens.sources[declaration],
                        .message = check_warning_text(scratch, "variable '%.*s' declared const here", (int)name.length, name.pointer),
                    };
                }
            }
        }
        else if (report->diagnostic == CHECK_DIAG_ADDRESS_SPACE || report->diagnostic == CHECK_DIAG_ADDRESS_SPACE_OVERLAP ||
                 report->diagnostic == CHECK_DIAG_ADDRESS_SPACE_NESTED)
        {
            result = check_semantic_address_space(parser, pp, input, stream, types, report, spell, location, note, scratch);
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_CONVERT && token < parser->tokens.count &&
                 parser->tokens.kinds[token] == PARSE_KIND_RETURN && check_action_kind(types, graph->types[node]) == BUSTER_TYPE_KIND_VOID)
        {
            const String8 name = check_semantic_name(parser, pp, check_semantic_function(parser, input, token));
            if (name.length != 0)
            {
                result = check_warning_text(scratch, "void function '%.*s' should not return a value [-Wreturn-mismatch]", (int)name.length, name.pointer);
            }
        }
        else if (report->diagnostic == CHECK_DIAG_INCOMPATIBLE && op == CHECK_OP_INITIALIZE && operand_type != 0)
        {
            const TypeId target = graph->types[node];
            const uint8_t target_kind = check_action_kind(types, target);
            const uint8_t source_kind = check_action_kind(types, operand_type);
            const bool source_pointer = source_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(source_kind) || source_kind == BUSTER_TYPE_KIND_FUNCTION;
            const bool target_pointer = target_kind == BUSTER_TYPE_KIND_POINTER;
            const bool function_pointer = target_pointer && check_action_kind(types, type_element(types, target)) == BUSTER_TYPE_KIND_FUNCTION;
            uint32_t width = 0;
            bool sign = false;
            const bool integer = check_action_integer_type(types, target, &width, &sign);
            if ((integer && source_pointer) || (target_pointer && source_pointer))
            {
                const String8 to = check_driver_warning_type(parser, pp, input, types, spell, scratch, target, 0);
                const String8 from = check_driver_warning_type(parser, pp, input, types, spell, scratch, operand_type, 0);
                for (uint32_t declaration = 0; declaration < input->token_count; declaration += 1)
                {
                    if (check_sparse_get(input->initializer_nodes, declaration) == node)
                    {
                        *location = declaration;
                        break;
                    }
                }
                result = to.length != 0 && from.length != 0 ? check_warning_text(scratch,
                    "%s initializing %.*s with an expression of type %.*s [%s]",
                    integer ? "incompatible pointer to integer conversion" : function_pointer ? "incompatible function pointer types" : "incompatible pointer types",
                    (int)to.length, to.pointer, (int)from.length, from.pointer,
                    integer ? "-Wint-conversion" : function_pointer ? "-Wincompatible-function-pointer-types" : "-Wincompatible-pointer-types") : NULL;
            }
        }
    }
    return check_semantic_text(pp, result);
}

static const char* check_semantic_control(Preprocessor* pp, const TypeTable* types, CheckProgramStatus status, const CheckProgramFailure* failure, Arena* scratch)
{
    const char* result = status == CHECK_PROGRAM_RESOURCE ? "checker program capacity exhausted" :
                         status == CHECK_PROGRAM_UNSUPPORTED ? "unsupported C statement" : "invalid C statement or control flow";
    if (status == CHECK_PROGRAM_INVALID && failure->message == CHECK_PROGRAM_MESSAGE_DUPLICATE_DEFAULT)
    {
        result = "multiple default labels in one switch";
    }
    else if (status == CHECK_PROGRAM_INVALID && failure->message == CHECK_PROGRAM_MESSAGE_DUPLICATE_CASE)
    {
        uint32_t width = 0;
        bool sign = false;
        if (check_action_integer_type(types, failure->type, &width, &sign))
        {
            const unsigned __int128 mask = check_action_mask(width);
            const unsigned __int128 value = failure->value & mask;
            const bool negative = sign && (value >> (width - 1)) != 0;
            unsigned __int128 magnitude = negative ? (0 - value) & mask : value;
            // 128 bits need at most 39 decimal digits, plus sign and terminator.
            char text[41];
            uint32_t at = sizeof(text) - 1;
            text[at] = 0;
            do
            {
                text[--at] = '0' + magnitude % 10;
                magnitude /= 10;
            } while (magnitude != 0);
            if (negative)
            {
                text[--at] = '-';
            }
            result = check_warning_text(scratch, "duplicate case value '%s'", text + at);
        }
    }
    return check_semantic_text(pp, result);
}

// One of check_program_diagnose_all's constraint errors in clang's words: a switch condition's type spelled as clang
// spells it, with its typedef's name first (#1447).
static const char* check_semantic_constraint(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                             const CheckProgramConstraint* constraint, Arena* scratch)
{
    const char* result = NULL;
    if (constraint->failure.message == CHECK_PROGRAM_MESSAGE_CONDITION)
    {
        CheckWarningSpelling spell = { 0 };
        // The condition's own expression, through the implicit conversions its switch makes of it (its promotion, the
        // unary plus at its closing parenthesis).
        uint32_t node = constraint->node;
        for (uint32_t steps = 0; steps < 4 && node < input->graph.count &&
             (input->graph.ops[node] == CHECK_OP_CONVERT || input->graph.ops[node] == CHECK_OP_CONDITION ||
              (input->graph.ops[node] == CHECK_OP_POSITIVE && input->graph.tokens[node] < parser->tokens.count &&
               parser->tree.roles[input->graph.tokens[node]] == PARSE_ROLE_CLOSE_CONDITION)); steps += 1)
        {
            node = input->graph.operand0[node];
        }
        const uint32_t spelling = node < input->graph.count ? check_input_operand_type_spelling(input, parser, node) : 0;
        const String8 type = check_driver_warning_type(parser, pp, input, types, &spell, scratch, constraint->failure.type, spelling);
        result = type.length != 0 ? check_warning_text(scratch, "statement requires expression of integer type (%.*s invalid)", (int)type.length, type.pointer) :
                                    NULL;
        result = check_semantic_text(pp, result);
    }
    else
    {
        result = check_semantic_control(pp, types, CHECK_PROGRAM_INVALID, &constraint->failure, scratch);
    }
    return result;
}

// One of check_asms_diagnose_all's statement errors in clang's words (#1447), with an incomplete tag's note at its
// declaring name (`note`, whose message is null where there's none).
static const char* check_semantic_asm(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                      const CheckAsmFailure* failure, PPOutputDiagnosticNote* note, Arena* scratch)
{
    CheckWarningSpelling spell = { 0 };
    const CheckInputAsm* assembly = input->asm_statements + failure->statement;
    const CheckAsmOperand* operand = assembly->operands + failure->operand;
    const bool output = failure->operand < assembly->output_count;
    const int constraint_length = (int)operand->constraint.length;
    const char* constraint = (const char*)operand->constraint.bytes;
    const String8 type = failure->type != 0 ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, failure->type,
                                                                        check_input_operand_type_spelling(input, parser, operand->node)) : (String8){ 0 };
    const char* result = NULL;
    *note = (PPOutputDiagnosticNote){ 0 };
    switch (failure->error)
    {
        case CHECK_ASM_ERROR_GENERIC: result = "invalid assembly operand or constraint"; break;
        case CHECK_ASM_ERROR_OUTPUT_LVALUE: result = "invalid lvalue in asm output"; break;
        case CHECK_ASM_ERROR_CAST: result = "invalid use of a cast in an inline asm context requiring an lvalue [-Winvalid-gnu-asm-cast]"; break;
        case CHECK_ASM_ERROR_INPUT_LVALUE: result = check_warning_text(scratch, "invalid lvalue in asm input for constraint '%.*s'", constraint_length, constraint); break;
        case CHECK_ASM_ERROR_BIT_FIELD:
            result = check_warning_text(scratch, "reference to a bit-field in asm %s with a memory constraint '%.*s'", output ? "input" : "output", constraint_length,
                                        constraint);
            break;
        case CHECK_ASM_ERROR_BIT_INT:
            result = type.length != 0 ? check_warning_text(scratch, "invalid type %.*s in asm %s", (int)type.length, type.pointer, output ? "input" : "output") : NULL;
            break;
        case CHECK_ASM_ERROR_VOID:
            result = type.length != 0 ? check_warning_text(scratch, "invalid type %.*s in asm input for constraint '%.*s'", (int)type.length, type.pointer,
                                                           constraint_length, constraint) : NULL;
            break;
        case CHECK_ASM_ERROR_INCOMPLETE:
        {
            result = type.length != 0 ? check_warning_text(scratch, "dereference of pointer to incomplete type %.*s", (int)type.length, type.pointer) : NULL;
            // The tag's declaring name: its first token that declares a tag of this type.
            TypeId bare = type_unqualified(type_strip_aligned(types, failure->type));
            const uint8_t kind = type_kind(types, bare);
            const bool tag = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
            const uint32_t name = tag ? types->tags.name[types->types.index[type_index(bare)]] : 0;
            uint32_t declared = BUSTER_CHECK_NONE;
            for (uint32_t t = 0; name != 0 && declared == BUSTER_CHECK_NONE && t < input->token_count; t += 1)
            {
                declared = parser->tokens.names[t] == name && input->declarations[t] == t && input->declaration_kinds[t] == C_BINDING_TAG_KIND &&
                           type_unqualified(input->declared_types[t]) == bare ? t : BUSTER_CHECK_NONE;
            }
            const String8 named = declared != BUSTER_CHECK_NONE ? check_driver_warning_type(parser, pp, input, types, &spell, scratch, bare, 0) : (String8){ "", 0 };
            if (named.length != 0)
            {
                *note = (PPOutputDiagnosticNote){
                    .token = declared,
                    .message = check_warning_text(scratch, "forward declaration of %.*s", (int)named.length, named.pointer),
                };
            }
            break;
        }
        case CHECK_ASM_ERROR_TIED:
        {
            const uint32_t tied = operand->match < assembly->output_count ? assembly->operands[operand->match].node : BUSTER_CHECK_NONE;
            const String8 output_type = failure->output_type != 0 ?
                check_driver_warning_type(parser, pp, input, types, &spell, scratch, failure->output_type, check_input_operand_type_spelling(input, parser, tied)) :
                (String8){ 0 };
            result = type.length != 0 && output_type.length != 0 ?
                check_warning_text(scratch, "unsupported inline asm: input with type %.*s matching output with type %.*s", (int)type.length, type.pointer,
                                   (int)output_type.length, output_type.pointer) : NULL;
            break;
        }
    }
    return check_semantic_text(pp, result);
}
