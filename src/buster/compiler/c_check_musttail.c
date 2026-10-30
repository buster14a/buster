// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's musttail attribute (#147) on a return statement, once the expressions' types are known: the rules of clang 23's
// Sema::checkMustTailAttr, and its JumpScopeChecker's for the scopes a tail call can't leave. The reader records each
// such return (CheckInput.musttails, check_input_statement_attributes), whose attribute's other errors (its arguments,
// its place) it reports itself. check_musttail_run checks them in source order, as clang does each when its statement is
// complete, the first rule each breaks, in clang's order:
//   1. its value is a call: of its expression, parentheses, __extension__ and a selection's (_Generic's,
//      __builtin_choose_expr's) chosen operand looked through, as Expr::IgnoreParenImplicitAsWritten does, but not a cast
//      (`return (long)f(x)`) or any other operator; a call of a builtin's, which the checker computes with no call, is
//      refused as unsupported (clang checks it against the builtin's signature and computes it inline);
//   2. both its function and its callee have prototypes (a K&R definition with parameters has its parameters' promoted
//      types as one, as the checker types it, and clang does it as a caller's; a call through a K&R designator has none);
//   3. the same calling convention;
//   4. neither is variadic;
//   5. no callee declared [[noreturn]] or [[_Noreturn]] (the standard spellings alone: GNU's attribute and the _Noreturn
//      keyword don't count, as clang's CXX11NoReturnAttr is theirs alone);
//   6. the same signature: the results, the parameters' count and each parameter, as similar types (Context's
//      hasSimilarType: the same canonical type once each pointer level's qualifiers are gone).
// A return that keeps all six can't leave a cleanup variable's scope: an error at its attributes' start for each
// variable whose scope holds it, the innermost first (clang reports them as its function ends, which their completion
// says). Each error is a CheckMusttailDiagnostic; the driver spells clang's message (check_driver_musttail_message), and
// check_program lowers each musttail return to a tail call (CHECK_CONTROL_RETURN of BUSTER_CHECK_CONTROL_TAIL). A unit
// without a musttail return pays one test. Include after c_check_asm_input.c, in c_check_input.c.

typedef enum CheckMusttailError
{
    CHECK_MUSTTAIL_CALL,
    CHECK_MUSTTAIL_BUILTIN,
    CHECK_MUSTTAIL_PROTOTYPE,
    CHECK_MUSTTAIL_CONVENTION,
    CHECK_MUSTTAIL_VARIADIC,
    CHECK_MUSTTAIL_NORETURN,
    CHECK_MUSTTAIL_SIGNATURE,
    CHECK_MUSTTAIL_SCOPE,
} CheckMusttailError;

// An error: where its caret goes (`token`: the return, or for a scope's its attributes' start), the token its place in
// the diagnostics' order is (`position`) and the one it is published after (`completion`, the return's terminator, or for
// a scope's its function's closing brace), the musttail record (`musttail`, its spelling's), and the callee's name for
// the messages that name it (`callee`, an identifier's token, or BUSTER_CHECK_NONE for a callee no declaration names).
typedef struct CheckMusttailDiagnostic CheckMusttailDiagnostic;
struct CheckMusttailDiagnostic
{
    CheckMusttailError error;
    uint32_t token;
    uint32_t position;
    uint32_t completion;
    uint32_t musttail;
    uint32_t callee;
};

typedef struct CheckMusttails CheckMusttails;
struct CheckMusttails
{
    CheckMusttailDiagnostic* diagnostics;
    uint32_t count;
    bool resource;
};

// Whether two unqualified types are arrays clang's UnwrapSimilarArrayTypes unwraps in C: of the same constant bound, or
// both of none (a variable one never).
static bool check_musttail_same_bound(const TypeTable* types, TypeId a, TypeId b)
{
    const uint8_t left = type_kind(types, a);
    const uint8_t right = type_kind(types, b);
    return (left == BUSTER_TYPE_KIND_ARRAY && right == BUSTER_TYPE_KIND_ARRAY && types->types.extent[type_index(a)] == types->types.extent[type_index(b)]) ||
           (left == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY && right == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY);
}

// Two types as clang's hasSimilarType has them in C: each level's qualifiers (and an aligned typedef's alignment)
// removed, an array's from its elements too (getUnqualifiedArrayType), stepping into two pointers' pointees and the
// elements of arrays of the same bound while the two differ (UnwrapSimilarTypes), then the same canonical type
// (type_same_canonical, which looks through the aligned typedefs inside). Comparing what is left once alone is clang's
// comparison at each level: two types the same at a level have the same parts beneath it. Each step goes into a type
// of its own, so the steps are no more than the table's types, whatever the depth (#936's review: 64 pointer levels).
static bool check_musttail_similar(const TypeTable* types, TypeId left, TypeId right, bool* resource)
{
    TypeId a = type_unqualified(type_strip_aligned(types, left));
    TypeId b = type_unqualified(type_strip_aligned(types, right));
    for (uint32_t step = 0; step < types->types.count && a != b &&
                            (check_musttail_same_bound(types, a, b) ||
                             (type_kind(types, a) == BUSTER_TYPE_KIND_POINTER && type_kind(types, b) == BUSTER_TYPE_KIND_POINTER));
         step += 1)
    {
        a = type_unqualified(type_strip_aligned(types, type_element(types, a)));
        b = type_unqualified(type_strip_aligned(types, type_element(types, b)));
    }
    const TypeAnswer answer = a == b ? TYPE_ANSWER_YES : type_same_canonical(types, a, b);
    *resource |= answer == TYPE_ANSWER_FULL;
    return answer == TYPE_ANSWER_YES;
}

// The function type a call's callee has: its value's, a function's or a pointer's to one; 0 for neither.
static TypeId check_musttail_function(const TypeTable* types, TypeId type)
{
    TypeId function = type_strip_aligned(types, type);
    function = type_kind(types, function) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, function)) : function;
    return type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION ? function : 0;
}

// The declaration a callee names, as clang's getReferencedDeclOfCallee finds it: its expression's selections, address-of,
// dereferences and unary pluses looked through, then a name's declaration (its use's token, whose spelling is the
// declaration's) or a member's (its name's token). BUSTER_CHECK_NONE for any other callee, which no message names.
static uint32_t check_musttail_callee_name(const CheckInput* input, uint32_t node)
{
    const CheckGraph* graph = &input->graph;
    uint32_t at = node;
    for (uint32_t steps = 0; at < graph->count && steps < graph->count &&
                             (graph->ops[at] == CHECK_OP_SELECT || graph->ops[at] == CHECK_OP_ADDRESS || graph->ops[at] == CHECK_OP_DEREFERENCE ||
                              graph->ops[at] == CHECK_OP_POSITIVE);
         steps += 1)
    {
        at = graph->operand0[at];
    }
    uint32_t name = BUSTER_CHECK_NONE;
    if (at < graph->count && graph->ops[at] == CHECK_OP_VALUE && input->declarations[graph->tokens[at]] != BUSTER_CHECK_NONE)
    {
        name = graph->tokens[at];
    }
    else if (at < graph->count && (graph->ops[at] == CHECK_OP_MEMBER || graph->ops[at] == CHECK_OP_ARROW))
    {
        name = graph->tokens[at] + 1;
    }
    return name;
}

// Whether a declaration's attribute is the standard noreturn clang's rule names (CXX11NoReturnAttr): [[noreturn]] or
// [[_Noreturn]], unscoped, not GNU's or the _Noreturn keyword, and accepted, not one clang drops after the entity's
// definition (`ignored`, check_input_attribute_late).
static bool check_musttail_standard_noreturn(const CheckInputAttribute* attribute)
{
    return attribute->kind == CHECK_ATTRIBUTE_NORETURN && attribute->standard && !attribute->keyword && attribute->first == attribute->name_token &&
           !attribute->ignored;
}

// Per object, the first declaration of its entity with an accepted standard noreturn (check_musttail_standard_noreturn),
// or BUSTER_CHECK_NONE: clang's declaration that a call names inherits the attribute from every earlier one
// (mergeDeclAttributes), so a callee has it where such a declaration precedes the call. One pass over the declarations'
// attributes, in a unit with musttail returns alone; null without room.
static uint32_t* check_musttail_noreturns(const CheckInput* input, Arena* scratch)
{
    const uint32_t count = input->token_count;
    uint32_t* first = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
    for (uint32_t i = 0; first != NULL && i <= count; i += 1)
    {
        first[i] = BUSTER_CHECK_NONE;
    }
    for (uint32_t d = 0; first != NULL && d < count; d += 1)
    {
        const CheckInputAttributes* attributes = input->attributes[d];
        const uint32_t object = attributes != NULL ? input->objects[d] : 0;
        bool noreturn = false;
        for (uint32_t i = 0; object != 0 && object <= count && i < attributes->count; i += 1)
        {
            noreturn |= check_musttail_standard_noreturn(attributes->records + i);
        }
        first[object] = noreturn && first[object] == BUSTER_CHECK_NONE ? d : first[object];
    }
    return first;
}

// Whether a callee's entity (a name's, `name` its use) has an accepted standard noreturn declared before the call
// (`noreturns`, check_musttail_noreturns).
static bool check_musttail_noreturn(const CheckInput* input, const uint32_t* noreturns, uint32_t name)
{
    const uint32_t declaration = name != BUSTER_CHECK_NONE && input->graph.count != 0 ? input->declarations[name] : BUSTER_CHECK_NONE;
    const uint32_t object = declaration < input->token_count ? input->objects[declaration] : 0;
    return object != 0 && object <= input->token_count && noreturns[object] < name;
}

// The first of the six rules that the musttail return `musttail` breaks (the file's comment), or the count for none, with
// the callee's name for the messages that name it.
static uint32_t check_musttail_rule(const CheckInput* input, const CheckStream* stream, const TypeTable* types, const uint32_t* noreturns,
                                    const CheckInputMusttail* musttail, uint32_t* callee_name, bool* resource)
{
    const CheckGraph* graph = &input->graph;
    const uint32_t returned = input->token_nodes[musttail->statement];
    uint32_t value = returned < graph->count ? graph->operand0[returned] : BUSTER_CHECK_NONE;
    for (uint32_t steps = 0; value < graph->count && steps < graph->count && graph->ops[value] == CHECK_OP_SELECT; steps += 1)
    {
        value = graph->operand0[value];
    }
    const CheckOp op = value < graph->count ? graph->ops[value] : CHECK_OP_COUNT;
    uint32_t rule = CHECK_MUSTTAIL_SCOPE;
    *callee_name = BUSTER_CHECK_NONE;
    if (op != CHECK_OP_CALL)
    {
        const bool builtin = op == CHECK_OP_BUILTIN_UNARY || op == CHECK_OP_BUILTIN_OVERFLOW || op == CHECK_OP_BUILTIN_MISC || op == CHECK_OP_BUILTIN_FLOAT ||
                             op == CHECK_OP_BUILTIN_QUERY || op == CHECK_OP_BUILTIN_ATOMIC || op == CHECK_OP_BUILTIN_VECTOR || op == CHECK_OP_BUILTIN_X86 ||
                             op == CHECK_OP_BUILTIN_BIT_CAST || op == CHECK_OP_VARARGS;
        rule = builtin ? CHECK_MUSTTAIL_BUILTIN : CHECK_MUSTTAIL_CALL;
    }
    else
    {
        const uint32_t callee = graph->operand0[value];
        *callee_name = check_musttail_callee_name(input, callee);
        const uint32_t declaration = input->body_declarations[musttail->body];
        const TypeId caller = declaration != BUSTER_CHECK_NONE ? check_musttail_function(types, input->declared_types[declaration]) : 0;
        const TypeId target = callee < graph->count ? check_musttail_function(types, stream->values[callee].type) : 0;
        const uint8_t caller_flags = caller != 0 ? types->types.flags[type_index(caller)] : 0;
        const uint8_t target_flags = target != 0 ? types->types.flags[type_index(target)] : 0;
        uint32_t caller_count = 0;
        uint32_t target_count = 0;
        const TypeId* caller_parameters = caller != 0 ? type_parameters(types, caller, &caller_count) : NULL;
        const TypeId* target_parameters = target != 0 ? type_parameters(types, target, &target_count) : NULL;
        if (caller == 0 || target == 0 || (caller_flags & BUSTER_TYPE_FLAG_PROTOTYPE) == 0 || (target_flags & BUSTER_TYPE_FLAG_PROTOTYPE) == 0)
        {
            rule = CHECK_MUSTTAIL_PROTOTYPE;
        }
        else if (type_function_abi_kind(types, caller) != type_function_abi_kind(types, target))
        {
            rule = CHECK_MUSTTAIL_CONVENTION;
        }
        else if ((caller_flags & BUSTER_TYPE_FLAG_VARIADIC) != 0 || (target_flags & BUSTER_TYPE_FLAG_VARIADIC) != 0)
        {
            rule = CHECK_MUSTTAIL_VARIADIC;
        }
        else if (check_musttail_noreturn(input, noreturns, *callee_name))
        {
            rule = CHECK_MUSTTAIL_NORETURN;
        }
        else
        {
            bool similar = check_musttail_similar(types, type_return(types, caller), type_return(types, target), resource) && caller_count == target_count;
            for (uint32_t p = 0; similar && p < caller_count; p += 1)
            {
                similar = check_musttail_similar(types, target_parameters[p], caller_parameters[p], resource);
            }
            rule = similar ? CHECK_MUSTTAIL_SCOPE : CHECK_MUSTTAIL_SIGNATURE;
        }
    }
    // A rule kept, the scopes' check: CHECK_MUSTTAIL_SCOPE stands for none broken here.
    return rule;
}

// The token that ends a musttail return's statement, its terminator: its `return`'s parent past its attributes' specifiers,
// whose closers the parser makes its parents (check_input_statement_attributes).
static uint32_t check_musttail_terminator(const Parser* parser, uint32_t statement)
{
    uint32_t at = parser->tree.parents[statement];
    for (uint32_t steps = 0; at < parser->tokens.count && steps < parser->tokens.count && parser->tree.roles[at] == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER; steps += 1)
    {
        at = parser->tree.parents[at];
    }
    return at < parser->tokens.count ? at : statement;
}

// The musttail returns' errors (the file's comment), or with `diagnostics` null their count alone: in source order, each
// return's rules' first, or its scopes'. A count past `limit` (UINT32_MAX, what the count holds, but in tests: a scope's
// error for each active cleanup at each return can pass it, #936's review) stops the count, a resource failure
// (`resource`).
static uint64_t check_musttail_collect(const CheckInput* input, const Parser* parser, const CheckStream* stream, const TypeTable* types,
                                       const uint32_t* noreturns, uint64_t limit, CheckMusttailDiagnostic* diagnostics, bool* resource)
{
    uint64_t count = 0;
    for (uint32_t m = 0; m < input->musttail_count && count <= limit; m += 1)
    {
        const CheckInputMusttail* musttail = input->musttails + m;
        uint32_t callee = BUSTER_CHECK_NONE;
        const uint32_t rule = check_musttail_rule(input, stream, types, noreturns, musttail, &callee, resource);
        const uint32_t end = check_musttail_terminator(parser, musttail->statement);
        if (rule != CHECK_MUSTTAIL_SCOPE)
        {
            if (diagnostics != NULL)
            {
                diagnostics[count] = (CheckMusttailDiagnostic){
                    .error = (CheckMusttailError)rule,
                    .token = musttail->statement,
                    .position = end,
                    .completion = end + 1,
                    .musttail = m,
                    .callee = callee,
                };
            }
            count += 1;
        }
        else
        {
            // The cleanup variables whose scopes hold the return, the innermost (the latest declared) first, as clang reports
            // them once its function ends.
            const uint32_t closing = parser->tree.parents[musttail->body];
            for (uint32_t c = input->cleanup_count; c > 0 && count <= limit; c -= 1)
            {
                const CheckInputCleanup* cleanup = input->cleanups + c - 1;
                if (cleanup->scope >= musttail->body && cleanup->declaration < musttail->statement && musttail->statement < cleanup->end)
                {
                    if (diagnostics != NULL)
                    {
                        diagnostics[count] = (CheckMusttailDiagnostic){
                            .error = CHECK_MUSTTAIL_SCOPE,
                            .token = musttail->attributes,
                            .position = closing,
                            .completion = closing + 1,
                            .musttail = m,
                            .callee = BUSTER_CHECK_NONE,
                        };
                    }
                    count += 1;
                }
            }
        }
    }
    *resource |= count > limit;
    return count;
}

// The musttail returns' errors (check_musttail_collect), into `output`. A stream with errors of its own, whose expressions
// clang wouldn't check further, is checked no further. False only where an arena, the errors' count or a type comparison
// runs out of room (`resource`), with none kept. The callees' noreturn attributes take `scratch` while it runs. At most
// `limit` errors (check_musttail_collect).
static bool check_musttail_run_limited(CheckMusttails* result, const CheckInput* input, const Parser* parser, const CheckStream* stream,
                                       const TypeTable* types, Arena* output, Arena* scratch, uint64_t limit)
{
    *result = (CheckMusttails){ 0 };
    const bool checked = input->musttail_count != 0 && stream->report_count == 0;
    const uint64_t mark = scratch->length;
    const uint32_t* noreturns = checked ? check_musttail_noreturns(input, scratch) : NULL;
    result->resource = checked && noreturns == NULL;
    const uint64_t count = checked && !result->resource ? check_musttail_collect(input, parser, stream, types, noreturns, limit, NULL, &result->resource) : 0;
    result->diagnostics = count != 0 && !result->resource ? BUSTER_ARENA_PUSH_ARRAY(output, CheckMusttailDiagnostic, count) : NULL;
    result->resource |= count != 0 && result->diagnostics == NULL;
    if (!result->resource && count != 0)
    {
        result->count = (uint32_t)check_musttail_collect(input, parser, stream, types, noreturns, limit, result->diagnostics, &result->resource);
    }
    arena_pop_to(scratch, mark);
    return !result->resource;
}

static bool check_musttail_run(CheckMusttails* result, const CheckInput* input, const Parser* parser, const CheckStream* stream, const TypeTable* types,
                               Arena* output, Arena* scratch)
{
    return check_musttail_run_limited(result, input, parser, stream, types, output, scratch, UINT32_MAX);
}
