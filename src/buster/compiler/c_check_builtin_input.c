// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Builtin calls retain their actual source argument count and enclosing function. In particular va_arg's type
// operand is elaborated at its own source position; it never enters the ordinary expression binding stream.

static bool check_input_vararg_id(uint16_t builtin)
{
    return (builtin >= CBUILTIN_VA_START && builtin <= CBUILTIN_VA_END) || cbuiltin_varargs_foreign(builtin);
}

static bool check_input_builtin_type_prepare(CheckInputReader* reader, uint32_t keyword, uint16_t builtin)
{
    const Parser* parser = reader->parser;
    const uint32_t opening = keyword + 1;
    const uint32_t closing = opening < parser->tokens.count ? parser->tree.parents[opening] : parser->tokens.count;
    bool valid = opening < closing && closing < parser->tokens.count && parser->tree.roles[opening] == PARSE_ROLE_OPEN_KEYWORD;
    const uint32_t separator = valid ? check_constant_punctuation(parser, opening + 1, closing, PARSE_KIND_COMMA) : closing;
    valid &= opening + 1 < separator && separator + 1 < closing &&
             check_constant_punctuation(parser, separator + 1, closing, PARSE_KIND_COMMA) == closing;
    if (valid)
    {
        const bool first_type = builtin == CBUILTIN_BIT_CAST;
        const uint32_t type_first = first_type ? opening + 1 : separator + 1;
        const uint32_t type_end = first_type ? separator : closing;
        check_input_rare_claim(reader, &reader->builtins[closing]);
        reader->builtins[closing] = builtin;
        check_input_rare_claim(reader, &reader->builtin_type_ends[type_first]);
        reader->builtin_type_ends[type_first] = closing;
        check_input_visit(reader, type_first, BUSTER_CHECK_RESOLVE_QUERY);
        reader->ignored[keyword] = true;
        memset(reader->ignored + type_first, 1, type_end - type_first);
    }
    else
    {
        check_input_report(reader, keyword, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

static void check_input_builtin_type(CheckInputReader* reader, uint32_t first)
{
    const uint32_t closing = reader->builtin_type_ends[first];
    const uint32_t end = reader->builtins[closing] == CBUILTIN_BIT_CAST ?
                         check_constant_punctuation(reader->parser, first, closing, PARSE_KIND_COMMA) : closing;
    const TypeId type = check_input_constant_type(reader, first, end);
    reader->query_types[closing] = type;
    if (type == 0)
    {
        check_input_report(reader, first, CHECK_INPUT_DECLARATION);
    }
}

// The builtin an identifier names (check_constant_builtin_name): the catalog's, or one of C23's <stdbit.h> functions,
// which a call is only where it calls the unit's declaration (check_input_stdc_library_call).
static uint16_t check_input_builtin_id(CheckInputReader* reader, uint32_t token)
{
    return check_constant_builtin_name(reader, reader->parser->tokens.names[token]);
}

static bool check_input_vararg_prepare(CheckInputReader* reader, uint32_t keyword, uint16_t builtin)
{
    const Parser* parser = reader->parser;
    const uint32_t opening = keyword + 1;
    const uint32_t closing = opening < parser->tokens.count ? parser->tree.parents[opening] : parser->tokens.count;
    bool valid = opening < closing && closing < parser->tokens.count &&
                 (parser->tree.roles[opening] == PARSE_ROLE_OPEN_CALL || parser->tree.roles[opening] == PARSE_ROLE_OPEN_KEYWORD);
    uint32_t arguments = 0;
    uint32_t second = BUSTER_CHECK_NONE;
    if (valid)
    {
        for (uint32_t first = opening + 1; first < closing;)
        {
            const uint32_t separator = check_constant_punctuation(parser, first, closing, PARSE_KIND_COMMA);
            second = arguments == 1 ? first : second;
            arguments += 1;
            first = separator + 1;
        }
        valid = reader->vararg_count < reader->vararg_capacity;
        reader->output->resource |= !valid;
    }
    if (valid)
    {
        CheckVarargsContext context =
        {
            .va_list_type = check_input_predefined_type(reader, reader->predefined_names[4]),
            .last_parameter = BUSTER_CHECK_VARARGS_NO_PARAMETER,
            .standard = parser->language.standard,
        };
        const uint32_t body = reader->function_body;
        if (body < parser->tokens.count)
        {
            const uint32_t function = check_sparse_get(reader->output->body_declarations, body);
            context.function_type = reader->declared_types[function];
            context.variadic = (reader->types->types.flags[type_index(context.function_type)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
            for (uint32_t parameter = check_sparse_get(reader->parameters, body); parameter != BUSTER_CHECK_NONE; parameter = check_sparse_get(reader->parameter_next, parameter))
            {
                context.last_parameter = parameter;
                context.last_parameter_type = reader->declared_types[parameter];
                context.last_parameter_flags = check_input_bit(reader->declaration_bits, parameter, BUSTER_CHECK_BITS_REGISTER) ? BUSTER_CHECK_REGISTER : 0;
            }
        }
        const uint32_t index = reader->vararg_count++;
        reader->varargs[index] = (CheckInputVarargs)
        {
            .context = context,
            .closing = closing,
            .argument_count = arguments,
            .second_argument = second,
        };
        check_input_rare_claim(reader, &reader->builtins[closing]);
        reader->builtins[closing] = builtin;
        check_input_rare_claim(reader, &reader->vararg_calls[closing]);
        reader->vararg_calls[closing] = index + 1;
        check_input_sparse_mark(reader, closing, true);
        reader->ignored[keyword] = true;
        if (builtin == CBUILTIN_VA_ARG && second < closing)
        {
            check_input_rare_claim(reader, &reader->vararg_types[second]);
            reader->vararg_types[second] = index + 1;
            check_input_visit(reader, second, BUSTER_CHECK_RESOLVE_QUERY);
            memset(reader->ignored + second, 1, closing - second);
        }
    }
    if (!valid)
    {
        check_input_report(reader, keyword, CHECK_INPUT_UNSUPPORTED);
    }
    return valid;
}

static void check_input_vararg_type(CheckInputReader* reader, uint32_t token)
{
    CheckInputVarargs* call = &reader->varargs[reader->vararg_types[token] - 1];
    if (call->argument_count == 2)
    {
        call->requested_type = check_input_constant_type(reader, token, call->closing);
        reader->query_types[call->closing] = call->requested_type;
        if (call->requested_type == 0)
        {
            check_input_report(reader, token, CHECK_INPUT_DECLARATION);
        }
    }
}

// The miscellaneous builtins' evidence (cbuiltin_misc_recorded), the call's record made: an alignment builtin's
// alignment, its second argument from past `end` (its first's) to the call's closing parenthesis `token`, as clang's
// evaluator reads it (BuiltinAlignment's EvaluateAsInt), the source evaluator's integer read as a constant context's
// rvalue, which reads a const object's constant initializer wherever the expression holds one (`const int a = 3;`'s `a`,
// `a + 0`, `(int)d` of a const double), where it gives one without a fault: the graph's runtime view of the argument reads
// none, so the call's checks take this value instead (CheckBuiltinCall.alignment_evaluated). And expect_with_probability's
// probability, which clang evaluates as a constant expression (EvaluateAsConstantExpr), const objects anywhere in it
// read too (`one ? 0.5 : 0.25`): it never runs, so its subtree is checked as a constant context's
// (BUSTER_CHECK_CONTEXT_VALUE, which reports no constraint of its own). Out of line, so that check_input_expressions'
// walk keeps its frame and registers: inlined, the evaluator's state took the walk's frame from 376 to 904 bytes.
// clang's -WCFString-literal (#148, CheckObjCString): __builtin___CFStringMakeConstantString's ordinary literal that isn't UTF-8 as LLVM's
// strict conversion reads it (pp_utf8_sequence: an overlong form, a surrogate, past U+10FFFF, or cut short), at the
// literal, once the call is read (its closing parenthesis `closing`). A rare builtin's rare literal: scalar.
static void check_input_cfstring_literal(CheckInputReader* reader, const CheckBuiltinCall* evidence, uint32_t closing)
{
    uint32_t at = 0;
    uint32_t length = 1;
    while (at < evidence->nan_payload_length && length != 0)
    {
        uint32_t code_point = 0;
        length = pp_utf8_sequence(evidence->nan_payload + at, evidence->nan_payload_length - at, &code_point);
        at += length;
    }
    if (length == 0)
    {
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_CFSTRING_LITERAL,
            .kind = CHECK_WARNING_EVENT_CFSTRING_TRUNCATED,
            .token = evidence->nan_payload_token,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = closing,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_once(reader, &event);
    }
}

// clang's warning of __builtin_cpu_supports's literal of a feature it doesn't know (#1843, SemaX86's validateCpuSupports:
// "invalid cpu feature string for builtin", of no option), at the callee's first token `keyword` (the builtin's name, or
// its outermost parenthesis), once the call `closing` is read. A cast literal is the call's error instead. Rare: scalar.
static void check_input_cpu_supports_literal(CheckInputReader* reader, const CheckBuiltinCall* evidence, uint32_t keyword, uint32_t closing)
{
    const uint64_t test = check_builtin_cpu_lookup(check_builtin_cpu_features, BUSTER_ARRAY_LENGTH(check_builtin_cpu_features), evidence->nan_payload,
                                                   evidence->nan_payload_length);
    if (test == 0)
    {
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_INVALID_CPU_SUPPORTS,
            .kind = CHECK_WARNING_EVENT_INVALID_CPU_SUPPORTS,
            .token = keyword,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = closing,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_once(reader, &event);
    }
}

// Whether call `close` (its `)`) calls a builtin, as clang's getBuiltinCallee finds one through the callee's
// parentheses (#148): a catalog builtin or a header builtin's leaf, which the reader claims at the call, or the unit's
// declaration, with external linkage, of a library builtin's name (pp_library_builtin, `abs` or `strlen` declared).
static bool check_input_builtin_callee(const CheckInputReader* reader, uint32_t close)
{
    const Parser* parser = reader->parser;
    uint32_t callee = reader->first_child[close];
    for (uint32_t depth = 0; depth < 64 && callee < parser->tokens.count && parser->tree.roles[callee] == PARSE_ROLE_CLOSE_GROUP; depth += 1)
    {
        const uint32_t opener = reader->first_child[callee];
        callee = opener < parser->tokens.count ? reader->next_child[opener] : BUSTER_CHECK_NONE;
    }
    const bool named = callee < parser->tokens.count && parser->tokens.kinds[callee] == PARSE_KIND_IDENTIFIER;
    const uint32_t declaration = named ? reader->output->declarations[callee] : BUSTER_CHECK_NONE;
    const bool function = declaration < parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_FUNCTION &&
                          check_input_linkage(reader->declaration_bits, declaration) == 2;
    const IdentId name = named ? parser->tokens.names[callee] : 0;
    const bool library = function && name != 0 &&
                         pp_library_builtin(&reader->pp->options, intern_bytes(reader->pp->table, name), intern_length(reader->pp->table, name));
    const bool intrinsic = reader->output->intrinsic_ids != NULL && reader->output->intrinsic_ids[close] != 0;
    return reader->builtins[close] != CBUILTIN_NONE || intrinsic || library;
}

// clang's warning of an alignment builtin's alignment of 1 (#2221, SemaBuiltinAlignment's warn_alignment_builtin_useless):
// align_up's and align_down's "aligning a value to 1 byte is a no-op", is_aligned's "... is always true", at the alignment
// argument [first, last) as clang's getExprLoc places it (a binary operator's at its operator, any other's at its first
// token), once the call `closing` is read. Rare: scalar.
static void check_input_alignment_one(CheckInputReader* reader, uint16_t builtin, uint32_t first, uint32_t last, uint32_t closing)
{
    const Parser* parser = reader->parser;
    uint32_t root = first;
    for (uint32_t t = first; t < last; t += 1)
    {
        const uint32_t parent = parser->tree.parents[t];
        root = parent < first || parent >= last || parent == t ? t : root;
    }
    const uint8_t role = parser->tree.roles[root];
    const bool binary = (role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_ASSIGN) || role == PARSE_ROLE_COMMA;
    const CheckWarningEvent event =
    {
        .id = builtin == CBUILTIN_IS_ALIGNED ? CHECK_WARNING_ID_ALIGNMENT_ALWAYS_TRUE : CHECK_WARNING_ID_ALIGNMENT_NO_OP,
        .kind = CHECK_WARNING_EVENT_ALIGNMENT_ONE,
        .token = binary ? root : first,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
        .completion = closing,
        .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
    };
    check_input_warning_once(reader, &event);
}

static __attribute__((noinline)) void check_input_misc_evidence(CheckInputReader* reader, uint32_t node, uint32_t token, uint32_t count, uint32_t end,
                                                                CheckBuiltinCall* evidence)
{
    CheckGraph* graph = &reader->output->graph;
    const uint16_t builtin = reader->builtins[token];
    const bool align = cbuiltin_align(builtin) && count == 2 && end < token;
    const uint32_t last = align ? check_constant_punctuation(reader->parser, end + 1, token, PARSE_KIND_COMMA) : token;
    CheckValue value = { 0 };
    if (align && last == token && end + 1 < last && check_input_evaluate_cached(reader, end + 1, last, &value) && check_input_const_rvalue(reader, last, &value) &&
        value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE && (value.flags & BUSTER_CHECK_MODIFICATION) == 0)
    {
        evidence->alignment_evaluated = true;
        evidence->alignment_side_effects = (value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0;
        evidence->alignment_low = (uint64_t)value.integer;
        evidence->alignment_high = (uint64_t)(value.integer >> 64);
        evidence->alignment_limbs = value.floating_storage[0];
        // Not where the source's own type is refused first (a floating one, a record, an enumeration, a function pointer),
        // as far as the source evaluator reads it.
        const uint32_t keyword = reader->first_child[token];
        CheckValue source = { 0 };
        const bool typed = value.integer == 1 && value.floating_storage[0] == 0 && keyword + 2 < end &&
                           check_input_evaluate_cached(reader, keyword + 2, end, &source) && source.type != 0;
        const TypeId bare = typed ? type_unqualified(type_strip_aligned(reader->types, source.type)) : 0;
        const uint8_t kind = typed ? type_kind(reader->types, bare) : BUSTER_TYPE_KIND_NONE;
        const bool refused = typed && kind != BUSTER_TYPE_KIND_INTEGER && kind != BUSTER_TYPE_KIND_BIT_INT && !type_kind_is_array(kind) &&
                             !(kind == BUSTER_TYPE_KIND_POINTER && type_kind(reader->types, type_strip_aligned(reader->types, type_element(reader->types, bare))) !=
                                                                   BUSTER_TYPE_KIND_FUNCTION);
        if (value.integer == 1 && value.floating_storage[0] == 0 && !refused)
        {
            check_input_alignment_one(reader, builtin, end + 1, last, token);
        }
    }
    if (builtin == CBUILTIN_ANNOTATION && count == 2 && end < token)
    {
        check_input_annotation_evidence(reader, end, token, evidence);
    }
    if (builtin == CBUILTIN_CALL_WITH_STATIC_CHAIN && count == 2 && end < token && end > 0)
    {
        // clang's BuiltinCallWithStaticChain: a CallExpr as written, without parentheses around it (whose node is a
        // grouping's), the root of its argument, of no builtin.
        const Parser* parser = reader->parser;
        const uint32_t close = end - 1;
        const uint32_t keyword = reader->first_child[token];
        const uint32_t begin = keyword < token ? keyword + 2 : token;
        const uint32_t parent = parser->tree.parents[close];
        const bool call = close >= begin && parser->tree.roles[close] == PARSE_ROLE_CLOSE_CALL && (parent < begin || parent >= end);
        evidence->static_chain_call = call && !check_input_builtin_callee(reader, close);
    }
    if (builtin == CBUILTIN_OS_LOG_FORMAT_BUFFER_SIZE || builtin == CBUILTIN_OS_LOG_FORMAT)
    {
        const uint32_t keyword = reader->first_child[token];
        check_input_os_log_evidence(reader, keyword < token ? keyword + 2 : token, token, builtin == CBUILTIN_OS_LOG_FORMAT, count, evidence);
    }
    if (builtin == CBUILTIN_COUNTED_BY_REF)
    {
        check_input_counted_ref_context(reader, token);
    }
    if (builtin == CBUILTIN_AVAILABLE)
    {
        const uint32_t keyword = reader->first_child[token];
        bool silent = false;
        if (keyword < token)
        {
            check_input_availability_specs(reader, keyword, token, false, &silent);
        }
        evidence->availability_silent = silent;
    }
    if (builtin == CBUILTIN_INFER_ALLOC_TOKEN)
    {
        const uint32_t keyword = reader->first_child[token];
        check_input_alloc_token_evidence(reader, keyword < token ? keyword + 2 : token, token, NULL, 0, 0, evidence);
    }
    if (builtin == CBUILTIN_CFSTRING_MAKE_CONSTANT_STRING && count == 1 && evidence->nan_payload_known && !evidence->nan_payload_utf8)
    {
        check_input_cfstring_literal(reader, evidence, token);
    }
    if (builtin == CBUILTIN_CPU_SUPPORTS && count == 1 && evidence->nan_payload_known && !evidence->nan_payload_cast && reader->first_child[token] < token)
    {
        // At the callee's first token: a parenthesized name's outermost `(` (#1916's review), whose `)` is the call's child.
        const uint32_t callee = reader->first_child[token];
        const bool grouped = reader->parser->tree.roles[callee] == PARSE_ROLE_CLOSE_GROUP && reader->first_child[callee] < callee;
        check_input_cpu_supports_literal(reader, evidence, grouped ? reader->first_child[callee] : callee, token);
    }
    if (builtin == CBUILTIN_EXPECT_WITH_PROBABILITY && count == 3 && graph->argument_count >= 3 &&
        graph->argument_offsets[node] == graph->argument_count - 3)
    {
        graph->contexts[graph->argument_nodes[graph->argument_count - 1]] |= BUSTER_CHECK_CONTEXT_VALUE;
    }
}

static void check_input_shuffle_evidence(CheckInputReader* reader, uint32_t keyword, uint32_t closing, uint32_t argument_count, CheckBuiltinCall* evidence)
{
    const uint32_t count = argument_count > 2 ? argument_count - 2 : 0;
    if (count != 0)
    {
        uint8_t* indices = arena_push_array(reader->arena, count, sizeof(uint8_t), 1);
        reader->output->resource |= indices == 0;
        if (indices != 0)
        {
            memset(indices, CHECK_BUILTIN_VECTOR_INDEX_ICE_NONE, count);
            uint32_t first = keyword + 2;
            for (uint32_t argument = 0; first < closing && argument < argument_count; argument += 1)
            {
                const uint32_t end = check_constant_punctuation(reader->parser, first, closing, PARSE_KIND_COMMA);
                if (argument >= 2)
                {
                    CheckValue value = { 0 };
                    const bool evaluated = check_input_evaluate_cached(reader, first, end, &value);
                    if (evaluated && value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE &&
                        (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
                    {
                        indices[argument - 2] = (value.flags & BUSTER_CHECK_ICE) != 0 ? CHECK_BUILTIN_VECTOR_INDEX_ICE_STRICT :
                                                (value.flags & BUSTER_CHECK_GNU_ICE_FORM) != 0 ? CHECK_BUILTIN_VECTOR_INDEX_ICE_GNU :
                                                CHECK_BUILTIN_VECTOR_INDEX_ICE_NONE;
                    }
                }
                first = end + 1;
            }
            evidence->shuffle_index_ice = indices;
            evidence->shuffle_index_ice_count = count;
        }
    }
}
