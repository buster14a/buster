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
    else if ((report->message == CHECK_INPUT_REDECLARATION || report->message == CHECK_INPUT_CONFLICTING_TYPES) && token < count && name.length != 0)
    {
        const uint32_t previous = report->subject - 1;
        const uint8_t kind = input->declaration_kinds[token];
        if (report->detail == CHECK_REDECLARATION_OBJECT_TYPE)
        {
            const String8 current = check_driver_warning_type(parser, pp, input, types, spell, scratch, report->type, report->type_spelling);
            const uint32_t prior_spelling = previous < count ? check_input_typedef_spelling(input, parser, previous) : 0;
            const String8 prior = check_driver_warning_type(parser, pp, input, types, spell, scratch, (TypeId)report->value, prior_spelling);
            result = current.length == 0 || prior.length == 0 ? NULL : check_warning_text(scratch,
                "redefinition of '%.*s' with a different type: %.*s vs %.*s", (int)name.length, name.pointer,
                (int)current.length, current.pointer, (int)prior.length, prior.pointer);
        }
        else if (report->detail > CHECK_REDECLARATION_GENERIC && report->detail <= CHECK_REDECLARATION_STATIC)
        {
            const char* format = report->detail == CHECK_REDECLARATION_FUNCTION_TYPE ? "conflicting types for '%.*s'" :
                                 report->detail == CHECK_REDECLARATION_PARAMETER ? "redefinition of parameter '%.*s'" :
                                 report->detail == CHECK_REDECLARATION_STATIC ? "static declaration of '%.*s' follows non-static declaration" :
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
        const CheckStringLiteral decoded = valid && message ? check_literal_strings(parts, close - strings, parser->language.standard >= 2023, scratch) : (CheckStringLiteral){ 0 };
        valid &= !message || (decoded.valid && decoded.byte_count != 0 && decoded.byte_count - 1 <= INT_MAX);
        result = valid ? check_warning_text(scratch, "static assertion failed%s%s%s%s%.*s",
                                            literal ? "" : " due to requirement '", literal ? "" : requirement, literal ? "" : "'",
                                            message ? ": " : "", message ? (int)(decoded.byte_count - 1) : 0, message ? (const char*)decoded.bytes : "") : NULL;
    }
    (void)input;
    return check_semantic_text(pp, result);
}

static const char* check_semantic_expression(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                            const TypeTable* types, const CheckReport* report, CheckWarningSpelling* spell, uint32_t* location, Arena* scratch)
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
    if (report->message == CHECK_MESSAGE_STATIC_ASSERT)
    {
        result = check_semantic_assert(parser, pp, input, report, location, scratch);
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
                const uint32_t target = graph->tokens[operand];
                const uint32_t declaration = target < input->token_count ? input->declarations[target] : BUSTER_CHECK_NONE;
                const String8 name = check_semantic_name(parser, pp, target);
                result = graph->ops[operand] == CHECK_OP_VALUE && declaration < input->token_count && name.length != 0 ?
                         type.length != 0 ? check_warning_text(scratch, "cannot assign to variable '%.*s' with const-qualified type %.*s",
                                                               (int)name.length, name.pointer, (int)type.length, type.pointer) : NULL :
                         "read-only variable is not assignable";
            }
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
