// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original expression diagnostics for #1394. Include in c_check_common_warnings.c after its context, spelling and
// candidate helpers. Structural identity is deliberately narrower than equal folded values: calls, arithmetic,
// explicit casts and indirection are not self-comparison operands. No expression walk uses the C call stack.

typedef struct CheckExpressionWarnings CheckExpressionWarnings;
struct CheckExpressionWarnings
{
    const Parser* parser;
    Preprocessor* pp;
    const CheckInput* input;
    const CheckStream* stream;
    TypeTable* types;
    const CheckCommonFacts* facts;
    Arena* scratch;
    uint32_t* modifications;
    bool modifications_read;
};

// A named expression's source-visible declaration. Its canonical object is used only for identity, never to read
// attributes from a later declaration. Member declarations have their own source token.
static uint32_t check_expression_declaration(const CheckExpressionWarnings* warnings, uint32_t node)
{
    const CheckGraph* graph = &warnings->input->graph;
    const uint32_t token = node < graph->count ? graph->tokens[node] : BUSTER_CHECK_NONE;
    uint32_t result = BUSTER_CHECK_NONE;
    if (token < warnings->parser->tokens.count && graph->ops[node] == CHECK_OP_VALUE &&
        warnings->parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER)
    {
        result = warnings->input->declarations[token];
    }
    else if (node < graph->count && (graph->ops[node] == CHECK_OP_MEMBER || graph->ops[node] == CHECK_OP_ARROW))
    {
        const uint32_t member = check_semantic_member_of(warnings->parser, warnings->input, warnings->stream, warnings->types, node);
        result = member != 0 ? check_semantic_member_token(warnings->parser, warnings->input, warnings->types, member) : BUSTER_CHECK_NONE;
    }
    return result;
}

static bool check_expression_grouped(const Parser* parser, const CheckGraph* graph, uint32_t node)
{
    const uint32_t token = node < graph->count ? graph->tokens[node] : BUSTER_CHECK_NONE;
    const uint32_t parent = token < parser->tokens.count ? parser->tree.parents[token] : BUSTER_CHECK_NONE;
    return parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_GROUP;
}

// The source begin includes parentheses: a substituted parenthesized macro operand remains a macro operand even
// when its name itself was written at the invocation. A member's subtree ends at its operator, before its name.
static uint32_t check_expression_begin(const Parser* parser, const CheckGraph* graph, uint32_t node)
{
    uint32_t token = graph->tokens[node];
    for (uint32_t steps = 0; token < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        const uint32_t parent = parser->tree.parents[token];
        if (parent >= parser->tokens.count || parser->tree.roles[parent] != PARSE_ROLE_CLOSE_GROUP)
        {
            break;
        }
        token = parent;
    }
    return token < parser->tokens.count ? check_common_subtree_start(parser, token) : token;
}

// Any enclosing body expansion suppresses the pointer warning, including a body reached through a substituted
// argument. An ordinary macro argument alone does not. The preprocessor's origin chain is bounded and acyclic.
static bool check_expression_macro_body(const Parser* parser, Preprocessor* pp, uint32_t token)
{
    const uint32_t source = token < parser->tokens.count ? parse_source_index(&parser->tokens, token) : BUSTER_PP_NONE;
    uint32_t origin = source < pp->output.count && check_common_from_macro(parser, pp, token) && pp_history_ensure(pp) ? pp_history_at(pp, source) : 0;
    bool result = false;
    while (origin != 0 && !result)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        result = kind == 0;
        origin = kind == BUSTER_PP_ORIGIN_ARGUMENT ? pp->substitutions[index].source : 0;
    }
    return result;
}

typedef struct CheckExpressionPair CheckExpressionPair;
struct CheckExpressionPair
{
    uint32_t left;
    uint32_t right;
};

// The narrow, side-effect-insensitive structural identity used by -Wtautological-compare. In particular volatile
// and atomic names still compare to themselves. Only subscript indices admit matching integer literals.
static bool check_expression_same(CheckExpressionWarnings* warnings, uint32_t left, uint32_t right)
{
    const CheckGraph* graph = &warnings->input->graph;
    const Parser* parser = warnings->parser;
    CheckExpressionPair local[32];
    CheckExpressionPair* work = local;
    uint32_t capacity = BUSTER_ARRAY_LENGTH(local);
    uint32_t count = 1;
    const uint64_t mark = warnings->scratch->length;
    bool same = true;
    work[0] = (CheckExpressionPair){ left, right };
    while (same && count != 0)
    {
        const CheckExpressionPair pair = work[--count];
        left = check_common_bool_expression(graph, pair.left);
        right = check_common_bool_expression(graph, pair.right);
        const CheckOp op = left < graph->count ? graph->ops[left] : CHECK_OP_COUNT;
        same = right < graph->count && op == graph->ops[right];
        if (same && op == CHECK_OP_VALUE)
        {
            const uint32_t ld = check_expression_declaration(warnings, left);
            const uint32_t rd = check_expression_declaration(warnings, right);
            const uint32_t lo = ld < warnings->input->token_count ? warnings->input->objects[ld] : 0;
            const uint32_t ro = rd < warnings->input->token_count ? warnings->input->objects[rd] : 0;
            same = ld < warnings->input->token_count && rd < warnings->input->token_count && (ld == rd || (lo != 0 && lo == ro));
        }
        else if (same && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW || op == CHECK_OP_SUBSCRIPT))
        {
            if (count + 2 > capacity)
            {
                const uint32_t grown = capacity <= UINT32_MAX / 2 ? capacity * 2 : 0;
                CheckExpressionPair* next = grown != 0 ? CHECK_COMMON_PUSH_ARRAY(warnings->scratch, CheckExpressionPair, grown) : NULL;
                if (next != NULL)
                {
                    memcpy(next, work, count * sizeof(*work));
                    work = next;
                    capacity = grown;
                }
                else
                {
                    check_common_exhausted(warnings->pp);
                    same = false;
                }
            }
            if (same && op == CHECK_OP_SUBSCRIPT)
            {
                const uint32_t li = check_common_bool_expression(graph, graph->operand1[left]);
                const uint32_t ri = check_common_bool_expression(graph, graph->operand1[right]);
                const uint32_t lt = li < graph->count ? graph->tokens[li] : BUSTER_CHECK_NONE;
                const uint32_t rt = ri < graph->count ? graph->tokens[ri] : BUSTER_CHECK_NONE;
                const bool literals = lt < parser->tokens.count && rt < parser->tokens.count && graph->ops[li] == CHECK_OP_VALUE &&
                                      graph->ops[ri] == CHECK_OP_VALUE && parser->tokens.kinds[lt] == PARSE_KIND_NUMBER &&
                                      parser->tokens.kinds[rt] == PARSE_KIND_NUMBER && !check_expression_grouped(parser, graph, li) &&
                                      !check_expression_grouped(parser, graph, ri) && check_common_integer_constant(warnings->stream, li) &&
                                      check_common_integer_constant(warnings->stream, ri);
                if (literals)
                {
                    const CheckValue lv = check_stream_value(warnings->stream, li);
                    const CheckValue rv = check_stream_value(warnings->stream, ri);
                    same = lv.integer == rv.integer;
                }
                else
                {
                    work[count++] = (CheckExpressionPair){ li, ri };
                }
            }
            else if (same)
            {
                const uint32_t lm = check_semantic_member_of(parser, warnings->input, warnings->stream, warnings->types, left);
                const uint32_t rm = check_semantic_member_of(parser, warnings->input, warnings->stream, warnings->types, right);
                const uint32_t lb = check_common_bool_expression(graph, graph->operand0[left]);
                const uint32_t rb = check_common_bool_expression(graph, graph->operand0[right]);
                const CheckOp lop = lb < graph->count ? graph->ops[lb] : CHECK_OP_COUNT;
                const CheckOp rop = rb < graph->count ? graph->ops[rb] : CHECK_OP_COUNT;
                same = lm != 0 && lm == rm && (lop == CHECK_OP_VALUE || lop == CHECK_OP_MEMBER || lop == CHECK_OP_ARROW) &&
                       (rop == CHECK_OP_VALUE || rop == CHECK_OP_MEMBER || rop == CHECK_OP_ARROW);
            }
            if (same)
            {
                work[count++] = (CheckExpressionPair){ graph->operand0[left], graph->operand0[right] };
            }
        }
        else
        {
            same = false;
        }
    }
    arena_pop_to(warnings->scratch, mark);
    return same;
}

static uint32_t check_expression_uncast(const CheckGraph* graph, uint32_t node)
{
    uint32_t result = node;
    for (uint32_t steps = 0; result < graph->count && (graph->ops[result] == CHECK_OP_CAST || graph->ops[result] == CHECK_OP_CONVERT) &&
         steps < graph->count; steps += 1)
    {
        result = graph->operand0[result];
    }
    return result;
}

static bool check_expression_null(const CheckExpressionWarnings* warnings, uint32_t node)
{
    const CheckValue value = node < warnings->stream->count ? check_stream_value(warnings->stream, node) : (CheckValue){ 0 };
    return node < warnings->stream->count && check_action_null(warnings->types, &value, 0);
}

static bool check_expression_array(const CheckExpressionWarnings* warnings, uint32_t node)
{
    const CheckGraph* graph = &warnings->input->graph;
    const TypeId type = node < graph->count ? check_stream_type(warnings->stream, node) : 0;
    // (The name's declaration last: a member's is a search of its record's body, and a member is no candidate.)
    return node < graph->count && graph->ops[node] == CHECK_OP_VALUE && type_kind_is_array(check_action_kind(warnings->types, type)) &&
           (check_stream_flags(warnings->stream, node) & BUSTER_CHECK_NONNULL_ADDRESS) != 0 &&
           check_expression_declaration(warnings, node) < warnings->input->token_count;
}

static const CheckInputAttribute* check_expression_attribute(const CheckInput* input, uint32_t declaration, CheckInputAttributeKind kind,
                                                            uint32_t parameter)
{
    const CheckInputAttributes* attributes = declaration < input->token_count ? check_input_attributes_at(input, declaration) : NULL;
    const CheckInputAttribute* result = NULL;
    for (uint32_t i = 0; result == NULL && attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        bool matches = !attribute->ignored && !attribute->subject_ignored && attribute->kind == kind;
        if (matches && parameter != BUSTER_CHECK_NONE && attribute->argument_count != 0)
        {
            matches = false;
            for (uint32_t a = 0; a < attribute->argument_count; a += 1)
            {
                matches |= attribute->argument_values != NULL && attribute->argument_values[a] == (int64_t)parameter + 1;
            }
        }
        result = matches ? attribute : NULL;
    }
    return result;
}

// Clang records a nonnull parameter as modifiable when a bare name is assigned or its address is taken. This is
// source order, including dead/unevaluated code, and not a reachability proof. A truth conversion checks at the full
// expression's completion, so even a later assignment in that expression invalidates its earlier parameter use.
// Only a unit with a nonnull parameter candidate pays for this sparse candidate scan and first-modification column.
static bool check_expression_unmodified(CheckExpressionWarnings* warnings, uint32_t declaration, uint32_t completion)
{
    const CheckGraph* graph = &warnings->input->graph;
    if (!warnings->modifications_read)
    {
        warnings->modifications_read = true;
        warnings->modifications = CHECK_COMMON_PUSH_ARRAY(warnings->scratch, uint32_t, warnings->input->token_count);
        if (warnings->modifications == NULL)
        {
            check_common_exhausted(warnings->pp);
        }
        else
        {
            memset(warnings->modifications, 0xff, warnings->input->token_count * sizeof(*warnings->modifications));
            static const CheckOp operators[] = { CHECK_OP_ASSIGN, CHECK_OP_ADDRESS };
            const CheckCommonOps set = check_common_ops(operators, BUSTER_ARRAY_LENGTH(operators));
            CheckCommonScan scan = { 0 };
            for (uint32_t node = check_common_next_op(graph, &set, &scan); node < graph->count;
                 node = check_common_next_op(graph, &set, &scan))
            {
                const CheckOp op = graph->ops[node];
                const uint32_t source = op == CHECK_OP_ASSIGN || op == CHECK_OP_ADDRESS ? graph->operand0[node] : BUSTER_CHECK_NONE;
                const uint32_t parameter = source < graph->count && graph->ops[source] == CHECK_OP_VALUE &&
                                           !check_expression_grouped(warnings->parser, graph, source) ?
                                           check_expression_declaration(warnings, source) : BUSTER_CHECK_NONE;
                if (parameter < warnings->input->token_count && check_common_sparse_get(warnings->input->parameters, parameter) != BUSTER_CHECK_NONE)
                {
                    const uint32_t end = graph->source_positions != NULL ? graph->source_positions[node] : graph->tokens[node] + 1;
                    warnings->modifications[parameter] = end < warnings->modifications[parameter] ? end : warnings->modifications[parameter];
                }
            }
        }
    }
    return warnings->modifications != NULL && warnings->modifications[declaration] > completion;
}

static const CheckInputAttribute* check_expression_nonnull(CheckExpressionWarnings* warnings, uint32_t node, uint32_t declaration,
                                                          bool* parameter)
{
    const CheckInput* input = warnings->input;
    const CheckGraph* graph = &input->graph;
    const CheckInputAttribute* result = NULL;
    *parameter = node < graph->count && graph->ops[node] == CHECK_OP_VALUE && declaration < input->token_count &&
                 check_common_sparse_get(input->parameters, declaration) != BUSTER_CHECK_NONE;
    if (*parameter)
    {
        result = check_expression_attribute(input, declaration, CHECK_ATTRIBUTE_NONNULL, BUSTER_CHECK_NONE);
        if (result == NULL)
        {
            const uint32_t opening = check_common_sparse_get(input->declaration_bodies, declaration);
            const uint32_t function = opening < input->token_count ? check_common_sparse_get(input->body_declarations, opening) : BUSTER_CHECK_NONE;
            result = check_expression_attribute(input, function, CHECK_ATTRIBUTE_NONNULL, check_common_sparse_get(input->parameters, declaration));
        }
    }
    else if (node < graph->count && graph->ops[node] == CHECK_OP_CALL)
    {
        // (A selected overload's declaration stands for the callee's, which then isn't looked up.)
        const uint32_t overload = check_program_builtin_index(warnings->stream->overloads, warnings->stream->overload_count, sizeof(CheckOverloadEffect), node);
        const uint32_t function = overload != BUSTER_CHECK_NONE ? warnings->stream->overloads[overload].selected.declaration :
                                  check_expression_declaration(warnings, check_common_callee_source(graph, graph->operand0[node], NULL));
        result = check_expression_attribute(input, function, CHECK_ATTRIBUTE_RETURNS_NONNULL, BUSTER_CHECK_NONE);
    }
    return result;
}

// An unsigned offset cannot wrap a pointer under the default C overflow contract. These four syntactic bounds
// checks therefore have one result. An explicit group around the addition, a signed offset, and pointer-wrap
// semantics all suppress the check. Equality and an arbitrary arithmetically equivalent expression are not proofs.
static int check_expression_bounds(CheckExpressionWarnings* warnings, uint32_t node)
{
    const CheckGraph* graph = &warnings->input->graph;
    const CheckOp op = graph->ops[node];
    const bool reversed = op == CHECK_OP_GREATER || op == CHECK_OP_LESS_EQUAL;
    const uint32_t sum = reversed ? graph->operand1[node] : graph->operand0[node];
    const uint32_t base = reversed ? graph->operand0[node] : graph->operand1[node];
    const PredefinesOptions* options = &warnings->pp->options.predefines;
    const bool wraps = options->wrapv_pointer != 0 ? options->wrapv_pointer == BUSTER_PREDEFINES_ON : options->strict_overflow == BUSTER_PREDEFINES_OFF;
    int result = -1;
    if (!wraps && op >= CHECK_OP_LESS && op <= CHECK_OP_GREATER_EQUAL && sum < graph->count && base < graph->count &&
        graph->ops[sum] == CHECK_OP_ADD && check_action_kind(warnings->types, check_stream_type(warnings->stream, sum)) == BUSTER_TYPE_KIND_POINTER &&
        !check_expression_grouped(warnings->parser, graph, sum))
    {
        uint32_t offset = BUSTER_CHECK_NONE;
        if (check_expression_same(warnings, graph->operand0[sum], base))
        {
            offset = graph->operand1[sum];
        }
        else if (check_expression_same(warnings, graph->operand1[sum], base))
        {
            offset = graph->operand0[sum];
        }
        uint32_t width = 0;
        bool signed_offset = false;
        if (offset < graph->count && check_common_integer(warnings->types, check_stream_type(warnings->stream, offset), &width, &signed_offset) && !signed_offset)
        {
            result = op == CHECK_OP_GREATER_EQUAL || op == CHECK_OP_LESS_EQUAL ? 1 : 0;
        }
    }
    return result;
}

// The two runtime diagnostic families are built while the comparison is parsed, before the full expression's
// implicit conversions. They survive unreachable code and static selection's other arm; a required constant,
// sizeof/typeof operand or array bound suppresses them.
static void check_expression_comparison(CheckExpressionWarnings* warnings, uint32_t node, bool self, bool string)
{
    const CheckGraph* graph = &warnings->input->graph;
    const Parser* parser = warnings->parser;
    const CheckOp op = graph->ops[node];
    uint32_t left = check_common_bool_expression(graph, graph->operand0[node]);
    uint32_t right = check_common_bool_expression(graph, graph->operand1[node]);
    const uint32_t contexts = graph->contexts != NULL ? graph->contexts[node] : 0;
    const TypeId type = left < graph->count ? check_stream_type(warnings->stream, left) : 0;
    const uint8_t kind = check_action_kind(warnings->types, type);
    const bool eligible = left < graph->count && right < graph->count && !check_common_floating_kind(warnings->types, type) &&
                          kind != BUSTER_TYPE_KIND_VECTOR && kind != BUSTER_TYPE_KIND_EXT_VECTOR &&
                          (contexts & (BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT)) == 0;
    const char* message = NULL;
    if (eligible && self)
    {
        const bool truth = op == CHECK_OP_EQUAL || op == CHECK_OP_LESS_EQUAL || op == CHECK_OP_GREATER_EQUAL;
        if (check_expression_same(warnings, left, right))
        {
            message = truth ? "self-comparison always evaluates to true" : "self-comparison always evaluates to false";
        }
        else if (check_expression_array(warnings, left) && check_expression_array(warnings, right))
        {
            message = op == CHECK_OP_EQUAL ? "array comparison always evaluates to false" : op == CHECK_OP_NOT_EQUAL ?
                      "array comparison always evaluates to true" : "array comparison always evaluates to a constant";
        }
        else
        {
            const int bounds = check_expression_bounds(warnings, node);
            message = bounds < 0 ? NULL : bounds != 0 ? "pointer comparison always evaluates to true" : "pointer comparison always evaluates to false";
        }
        // An operand a macro gave only takes the warning away, and where an operand begins is a search of the unit's
        // tokens (check_common_subtree_start): read them where a message stands, not for every comparison (#1340).
        if (message != NULL && !check_common_from_macro(parser, warnings->pp, check_expression_begin(parser, graph, left)) &&
            !check_common_from_macro(parser, warnings->pp, check_expression_begin(parser, graph, right)))
        {
            check_common_report_evaluated(parser, warnings->pp, warnings->input, graph->tokens[node], graph->tokens[node], PP_DIAGNOSTIC_SEMANTIC,
                                          CHECK_WARNING_ID_TAUTOLOGICAL_SELF_COMPARE, false, message, NULL, 0, warnings->scratch);
        }
    }
    left = check_expression_uncast(graph, left);
    right = check_expression_uncast(graph, right);
    const uint32_t lt = left < graph->count ? graph->tokens[left] : BUSTER_CHECK_NONE;
    const uint32_t rt = right < graph->count ? graph->tokens[right] : BUSTER_CHECK_NONE;
    const bool ls = lt < parser->tokens.count && graph->ops[left] == CHECK_OP_VALUE && parser->tokens.kinds[lt] == PARSE_KIND_STRING;
    const bool rs = rt < parser->tokens.count && graph->ops[right] == CHECK_OP_VALUE && parser->tokens.kinds[rt] == PARSE_KIND_STRING;
    if (eligible && string && ((ls && !check_expression_null(warnings, right)) || (rs && !check_expression_null(warnings, left))))
    {
        check_common_report_evaluated(parser, warnings->pp, warnings->input, graph->tokens[node], graph->tokens[node], PP_DIAGNOSTIC_SEMANTIC,
                                      CHECK_WARNING_ID_STRING_COMPARE, false,
                                      "result of comparison against a string literal is unspecified (use an explicit string comparison function instead)",
                                      NULL, 0, warnings->scratch);
    }
}

// A strong named array or function and the address of a named object cannot be null. Parentheses around a bare
// comparison operand suppress this diagnostic, whereas a truth conversion strips them; parentheses immediately
// inside '&' suppress both. An explicitly written function address is the suggested, silent form.
static void check_expression_pointer(CheckExpressionWarnings* warnings, uint32_t source, uint32_t owner, uint32_t null, bool compare, bool equal)
{
    const CheckGraph* graph = &warnings->input->graph;
    const Parser* parser = warnings->parser;
    uint32_t node = check_common_bool_expression(graph, source);
    const bool address = node < graph->count && graph->ops[node] == CHECK_OP_ADDRESS;
    const bool grouped = node < graph->count && check_expression_grouped(parser, graph, node);
    const uint32_t original = node < graph->count ? graph->tokens[node] : BUSTER_CHECK_NONE;
    node = address ? graph->operand0[node] : node;
    const TypeId type = node < graph->count ? check_stream_type(warnings->stream, node) : 0;
    const uint8_t kind = check_action_kind(warnings->types, type);
    const bool function = kind == BUSTER_TYPE_KIND_FUNCTION;
    const bool array = type_kind_is_array(kind);
    // Only a strong form (an address, a function or an array) or a nonnull one (a parameter's name, or a call) can
    // warn: any other pointer, a member's or a subscript's, is done here (#1340).
    const bool named = node < graph->count && graph->ops[node] == CHECK_OP_VALUE;
    const bool call = node < graph->count && graph->ops[node] == CHECK_OP_CALL;
    if (!address && !function && !array && (kind != BUSTER_TYPE_KIND_POINTER || (!named && !call)))
    {
        return;
    }
    // A member's declarator is a search of its record's body (check_semantic_member_token), and only the strong forms
    // read it. The parameter's test reads a name's declaration alone.
    const uint32_t declaration = named || address || function || array ? check_expression_declaration(warnings, node) : BUSTER_CHECK_NONE;
    const uint32_t token = node < graph->count ? graph->tokens[node] : BUSTER_CHECK_NONE;
    const bool member = node < graph->count && (graph->ops[node] == CHECK_OP_MEMBER || graph->ops[node] == CHECK_OP_ARROW);
    uint32_t root = token;
    for (uint32_t steps = 0; compare && call && root < parser->tokens.count && steps < parser->tokens.count; steps += 1)
    {
        const uint32_t parent = parser->tree.parents[root];
        if (parent >= parser->tokens.count || parser->tree.roles[parent] != PARSE_ROLE_CLOSE_GROUP)
        {
            break;
        }
        root = parent;
    }
    // A call's location is its subtree's first token, a search of the unit's tokens (check_common_subtree_start) that
    // ends at or before its root, so the root answers whether there is one; a call that can warn takes the search.
    const uint32_t anchor = member ? token + 1 : call ? root : token;
    const uint32_t id = compare ? CHECK_WARNING_ID_TAUTOLOGICAL_POINTER_COMPARE : CHECK_WARNING_ID_POINTER_BOOL_CONVERSION;
    const bool form = anchor < parser->tokens.count && !(compare && grouped && !address && !call) &&
                      !(address && check_expression_grouped(parser, graph, node));
    bool parameter = false;
    const CheckInputAttribute* nonnull = form && !address && kind == BUSTER_TYPE_KIND_POINTER ?
                                         check_expression_nonnull(warnings, node, declaration, &parameter) : NULL;
    const bool strong = form && declaration < warnings->input->token_count && (address || function || array) && !(address && function) &&
                        (member || (check_stream_flags(warnings->stream, node) & BUSTER_CHECK_NONNULL_ADDRESS) != 0);
    const bool candidate = strong || nonnull != NULL;
    const uint32_t location = candidate && call ? check_common_subtree_start(parser, root) : anchor;
    if (candidate && check_common_severity(warnings->pp, parser, warnings->input, location, id, false) != CHECK_WARNING_IGNORE)
    {
        // Only a diagnostic candidate needs expansion history or its containing full expression.
        if (check_common_from_macro(parser, warnings->pp, original) && (check_expression_macro_body(parser, warnings->pp, original) ||
            check_expression_macro_body(parser, warnings->pp, graph->tokens[owner])))
        {
            return;
        }
        const uint32_t completion = compare ? (graph->source_positions != NULL ? graph->source_positions[owner] : graph->tokens[owner]) :
                                    check_common_full_expression_end(parser, graph, warnings->facts->parents, owner);
        if (nonnull != NULL && parameter && !check_expression_unmodified(warnings, declaration, completion))
        {
            return;
        }
        const uint32_t first = member ? check_common_subtree_start(parser, token) : location;
        const uint32_t end = call ? root + 1 : location + 1;
        const char* name = check_semantic_requirement(parser, warnings->pp, first, end, NULL, warnings->scratch);
        const char* message = name == NULL ? NULL : nonnull != NULL && compare ?
                              check_warning_text(warnings->scratch, "comparison of nonnull %s '%s' %sequal to a null pointer is '%s' on first encounter",
                                                 parameter ? "parameter" : "function call", name, equal ? "" : "not ", equal ? "false" : "true") :
                              nonnull != NULL ? check_warning_text(warnings->scratch, "nonnull %s '%s' will evaluate to 'true' on first encounter",
                                                                   parameter ? "parameter" : "function call", name) : compare ?
                              check_warning_text(warnings->scratch, "comparison of %s'%s' %sequal to a null pointer is always %s",
                                                 address ? "address of " : function ? "function " : "array ", name,
                                                 equal ? "" : "not ", equal ? "false" : "true") :
                              check_warning_text(warnings->scratch, "address of %s'%s' will always evaluate to 'true'",
                                                 address ? "" : function ? "function " : "array ", name);
        CheckCommonNote notes[2];
        uint32_t note_count = 0;
        if (nonnull != NULL)
        {
            notes[note_count++] = (CheckCommonNote){ nonnull->name_token, parameter ? "declared 'nonnull' here" : "declared 'returns_nonnull' here" };
        }
        else if (function)
        {
            notes[note_count++] = (CheckCommonNote){ location, "prefix with the address-of operator to silence this warning" };
            uint32_t parameters = 0;
            (void)type_parameters(warnings->types, type_unqualified(type_strip_aligned(warnings->types, type)), &parameters);
            const TypeId result = type_return(warnings->types, type_unqualified(type_strip_aligned(warnings->types, type)));
            const uint8_t returned = check_action_kind(warnings->types, result);
            const uint32_t uncast = compare ? check_expression_uncast(graph, null) : BUSTER_CHECK_NONE;
            const bool zero = uncast < graph->count && check_common_integer_constant(warnings->stream, uncast) && check_expression_null(warnings, uncast);
            uint32_t width = 0;
            bool signed_result = false;
            const bool callable = parameters == 0 && (compare ? returned == BUSTER_TYPE_KIND_POINTER ||
                                  (zero && check_common_integer(warnings->types, result, &width, &signed_result)) :
                                  check_common_bool_target(warnings->types, result));
            if (callable)
            {
                notes[note_count++] = (CheckCommonNote){ location, "suffix with parentheses to turn this into a function call" };
            }
        }
        if (message == NULL)
        {
            check_common_exhausted(warnings->pp);
        }
        else if (compare)
        {
            check_common_report_at(parser, warnings->pp, warnings->input, location, graph->tokens[owner], id, false, message, notes, note_count,
                                   warnings->scratch);
        }
        else
        {
            check_common_report_evaluated(parser, warnings->pp, warnings->input, location, completion, PP_DIAGNOSTIC_SEMANTIC, id, false,
                                          message, notes, note_count, warnings->scratch);
        }
    }
}

// One batched candidate scan for the four related expression families. Ordinary arithmetic and bare leaves do no
// declaration, attribute or spelling work.
static void check_common_expression_warnings(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                             TypeTable* types, const CheckCommonFacts* facts, Arena* scratch)
{
    CheckExpressionWarnings warnings = { .parser = parser, .pp = pp, .input = input, .stream = stream, .types = types, .facts = facts, .scratch = scratch };
    const CheckGraph* graph = &input->graph;
    const bool self = check_common_enabled(pp, input, CHECK_WARNING_ID_TAUTOLOGICAL_SELF_COMPARE, false);
    const bool string = check_common_enabled(pp, input, CHECK_WARNING_ID_STRING_COMPARE, false);
    const bool pointer = check_common_enabled(pp, input, CHECK_WARNING_ID_TAUTOLOGICAL_POINTER_COMPARE, false);
    const bool boolean = check_common_enabled(pp, input, CHECK_WARNING_ID_POINTER_BOOL_CONVERSION, false);
    static const CheckOp operators[] =
    {
        CHECK_OP_LESS, CHECK_OP_LESS_EQUAL, CHECK_OP_GREATER, CHECK_OP_GREATER_EQUAL, CHECK_OP_EQUAL, CHECK_OP_NOT_EQUAL,
        CHECK_OP_NOT, CHECK_OP_LOGICAL_AND, CHECK_OP_LOGICAL_OR, CHECK_OP_CONDITION, CHECK_OP_CONDITIONAL,
        CHECK_OP_INITIALIZE, CHECK_OP_ASSIGN, CHECK_OP_CONVERT, CHECK_OP_CALL,
    };
    const CheckCommonOps set = check_common_ops(operators, BUSTER_ARRAY_LENGTH(operators));
    CheckCommonScan scan = { 0 };
    for (uint32_t node = self || string || pointer || boolean ? check_common_next_op(graph, &set, &scan) : graph->count;
         node < graph->count && !pp->fatal; node = check_common_next_op(graph, &set, &scan))
    {
        const CheckOp op = graph->ops[node];
        if (node >= stream->count || stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            continue;
        }
        if (op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL)
        {
            check_expression_comparison(&warnings, node, self, string);
            if (pointer && (op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL))
            {
                const uint32_t left = graph->operand0[node];
                const uint32_t right = graph->operand1[node];
                const bool ln = check_expression_null(&warnings, left);
                const bool rn = check_expression_null(&warnings, right);
                if (ln != rn)
                {
                    check_expression_pointer(&warnings, ln ? right : left, node, ln ? left : right, true, op == CHECK_OP_EQUAL);
                }
            }
        }
        if (boolean)
        {
            uint32_t operands[3];
            uint32_t count = check_common_truth_operands(graph, node, operands);
            if (op == CHECK_OP_CONDITIONAL)
            {
                operands[count++] = graph->operand0[node];
            }
            for (uint32_t i = 0; i < count; i += 1)
            {
                check_expression_pointer(&warnings, operands[i], node, BUSTER_CHECK_NONE, false, false);
            }
            if (op == CHECK_OP_INITIALIZE || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONVERT)
            {
                // (The target's type first: few are _Bool, and the conversion's flags are another row to load.)
                const uint32_t slot = op == CHECK_OP_ASSIGN ? 1u : 0u;
                if (check_common_bool_target(types, check_stream_type(stream, node)))
                {
                    const uint32_t source = slot != 0 ? graph->operand1[node] : graph->operand0[node];
                    const uint32_t flags = stream->conversions[node].operands[slot].flags;
                    if ((flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (flags & (BUSTER_CHECK_CONVERT_CAST | BUSTER_CHECK_CONVERT_ATOMIC)) == 0)
                    {
                        check_expression_pointer(&warnings, source, node, BUSTER_CHECK_NONE, false, false);
                    }
                }
            }
            else if (op == CHECK_OP_CALL && graph->argument_offsets != NULL && graph->argument_counts != NULL && stream->argument_conversions != NULL)
            {
                const uint32_t offset = graph->argument_offsets[node];
                const uint32_t arguments = graph->argument_counts[node];
                for (uint32_t i = arguments; i != 0 && offset <= graph->argument_count && arguments <= graph->argument_count - offset;)
                {
                    i -= 1;
                    const CheckConversion edge = stream->argument_conversions[offset + i];
                    if ((edge.flags & BUSTER_CHECK_CONVERT_ASSIGNMENT) != 0 && (edge.flags & BUSTER_CHECK_CONVERT_CAST) == 0 && check_common_bool_target(types, edge.type))
                    {
                        check_expression_pointer(&warnings, graph->argument_nodes[offset + i], node, BUSTER_CHECK_NONE, false, false);
                    }
                }
            }
        }
    }
}
