// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Binding handoff (#336). Include after CheckInputReader and its resolver. Declaration/type elaboration needs
// immediate visibility in source order, so the checker's reader is the one producer of binding events: it records
// every declaration, use and scope restore in reader->bindings, which CheckEnvironment publishes (#113). A use
// event's `previous` is the head of its name when it was recorded: the activation event + 1 that the use sees,
// already in the resolvers' result form. The default path reads those retained answers directly, with no replay,
// sort or name lookup. --scalar resolves the same events with the independent stack replay (c_bindings_scalar),
// whose results must be the same. The batch sort and the parser's own grammar replay (#128's ParseBindings,
// mapped onto the reader's use events) remain selectable for measurement and as oracles.
//
// Every mode feeds one consumer, check_input_binding_answers, with an answer per event: each use's declaration
// identity, its type at that point and its reports. Raw attribute uses have reader events but no parser events;
// a use-specific type captured during ordered elaboration (before a later completion) is kept. Tags take their
// canonical declaration during elaboration; the parser modes also check them against the parser's tag column.
// The parser, tree and original scratch arena are immutable: a local Parser view borrows checker scratch only
// while the parser modes build their temporary columns. A failed resolution or an invalid answer publishes nothing
// and sets `resource`. No column has a padding contract.

typedef enum CheckInputBindingMode
{
    // The direct answers, or the stack replay for --scalar.
    CHECK_INPUT_BINDINGS_DEFAULT,
    CHECK_INPUT_BINDINGS_DIRECT,
    CHECK_INPUT_BINDINGS_STACKS,
    CHECK_INPUT_BINDINGS_BATCH,
    CHECK_INPUT_BINDINGS_PARSER_STACKS,
    CHECK_INPUT_BINDINGS_PARSER_BATCH,
    CHECK_INPUT_BINDING_MODES,
} CheckInputBindingMode;

static const char* const check_input_binding_mode_names[CHECK_INPUT_BINDING_MODES] =
{
    "default",
    "direct",
    "stacks",
    "batch",
    "parser-stacks",
    "parser-batch",
};

// The incomplete array type a use inside its declaration's initializer sees, or 0: the type is read first, which most
// declarations haven't, so that a use reads the initializer's end alone where there is one (#1489).
static TypeId check_input_binding_incomplete(const CheckInputReader* reader, uint32_t declaration, uint32_t token)
{
    const TypeId incomplete = reader->incomplete_array_types != 0 && declaration < reader->parser->tokens.count ? BUSTER_CHECK_RARE(reader, incomplete_array_types, declaration) : 0;
    return incomplete != 0 && token < check_sparse_get(reader->output->initializer_ends, declaration) ? incomplete : 0;
}

// Whether a declared type is const-qualified, through an array's element type as Clang's -Wunused-const-variable sees it.
static bool check_input_type_const(const TypeTable* types, TypeId type)
{
    bool result = false;
    TypeId current = type;
    for (uint32_t steps = 0; !result && current != 0 && steps < types->types.count; steps += 1)
    {
        const TypeId stripped = type_strip_aligned(types, current);
        result = (type_qualifiers(stripped) & BUSTER_TYPE_CONST) != 0;
        const uint8_t kind = type_kind(types, type_unqualified(stripped));
        if (!result && type_kind_is_array(kind))
        {
            current = type_element(types, stripped);
        }
        else
        {
            current = 0;
        }
    }
    return result;
}

// Whether a declaration is written in the main file, where Clang gives file-scope unused variables' warnings.
static bool check_input_in_main_file(const CheckInputReader* reader, uint32_t token)
{
    const uint32_t source = token < reader->parser->tokens.count ? reader->parser->tokens.sources[token] : BUSTER_CHECK_NONE;
    const uint32_t location = source < reader->pp->output.count && reader->pp->output.locations != NULL ? reader->pp->output.locations[source] : 0;
    const bool result = source < reader->pp->output.count && pp_location_in_main_file(reader->pp, location);
    return result;
}

// A declaration clang's unused warnings leave alone: one the source marks unused or used, a typedef or enumerator,
// a label or tag, or a name whose object is never emitted. Parameters are marked from their definitions' chains.
static bool check_input_unused_suppressed(const CheckInput* input, uint32_t token, uint32_t kind)
{
    const CheckInputAttributes* attributes = token < input->token_count ? check_input_attributes_at(input, token) : NULL;
    bool marked = false;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const uint32_t attribute = attributes->records[i].kind;
        // x86's interrupt that its subject takes is clang's implicit used too (#147, handleAnyX86InterruptAttr).
        const bool interrupt = attribute == CHECK_ATTRIBUTE_INTERRUPT && !attributes->records[i].ignored && !attributes->records[i].subject_ignored;
        // An applied cleanup takes its variable's address, so clang's ShouldDiagnoseUnusedDecl skips it (#1881); one
        // ignored at file scope still leaves the variable unused.
        const bool cleanup = attribute == CHECK_ATTRIBUTE_CLEANUP && !attributes->records[i].ignored;
        marked = marked || attribute == CHECK_ATTRIBUTE_UNUSED || attribute == CHECK_ATTRIBUTE_USED || interrupt || cleanup;
    }
    const bool result = kind != C_BINDING_OBJECT && kind != C_BINDING_FUNCTION && kind != C_BINDING_TYPEDEF ? true : marked;
    return result;
}

// The definition parameters, whose uses alone decide -Wunused-parameter: a prototype's names never warn. The vector path
// visits the bodies' openings alone (check_sparse_next), a few thousand of sqlite3.c's 716,158 tokens (#1489); the
// scalar path, its twin, every token.
static bool check_input_parameter_marks(const CheckInput* input, const Parser* parser, uint8_t* marks)
{
    bool valid = parser != NULL && marks != NULL && check_sparse_present(input->body_declarations) &&
                 check_sparse_present(input->parameters) && check_sparse_present(input->parameter_next);
    const uint32_t count = valid ? parser->tokens.count : 0;
    const bool vector = valid && BUSTER_VECTOR_PATH(!parser->options.scalar);
    for (uint32_t opening = check_sparse_next(input->body_declarations, 0, count, vector); valid && opening < count;
         opening = check_sparse_next(input->body_declarations, opening + 1, count, vector))
    {
        const uint32_t declaration = check_sparse_get(input->body_declarations, opening);
        valid = declaration == BUSTER_CHECK_NONE || declaration < parser->tokens.count;
        for (uint32_t parameter = declaration != BUSTER_CHECK_NONE ? check_sparse_get(input->parameters, opening) : BUSTER_CHECK_NONE, steps = 0;
             valid && parameter != BUSTER_CHECK_NONE && steps < parser->tokens.count; steps += 1)
        {
            valid = parameter < parser->tokens.count;
            marks[parameter] = 1;
            parameter = check_sparse_get(input->parameter_next, parameter);
        }
    }
    return valid;
}

// Nearest statement terminator, or scope's closing brace/for-header parenthesis, including `token` itself (#1378, #1784).
// A for-header's subtree ends with its body, where the scope completes. `parents` is a private copy
// of the parser's count entries, used for this predicate alone, or NULL for an uncached walk. With a copy, compress
// each walked path to its answer: a comma chain
// shared by many declarators is visited once, without a new retained token column. Missing parents and cycles compress
// to NONE too. The parser's original parents remain immutable, including when another consumer asks a different role.
// `work` counts uncached parent steps only. Callers copy once those steps reach the unit's count, so cheap queries
// don't pay a token-wide copy or allocation. Before that switch at most twice count steps have been walked.
static uint32_t check_input_cached_ancestor(const Parser* parser, uint32_t token, uint32_t* parents, bool scope, uint64_t* work)
{
    const uint32_t count = parser->tokens.count;
    const uint8_t* roles = parser->tree.roles;
    const uint32_t* links = parents != NULL ? parents : parser->tree.parents;
    uint32_t ancestor = token;
    uint32_t steps = 0;
    bool found = false;
    while (ancestor < count && steps < count)
    {
        const uint8_t role = roles[ancestor];
        found = scope ? role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_COMPOUND_BODY || role == PARSE_ROLE_CLOSE_FOR :
                        role == PARSE_ROLE_TERMINATOR;
        if (found)
        {
            break;
        }
        ancestor = links[ancestor];
        steps += 1;
    }
    const uint32_t result = found ? ancestor : BUSTER_CHECK_NONE;
    if (parents == NULL && work != NULL)
    {
        *work += steps;
    }
    for (uint32_t at = token, walked = 0; parents != NULL && at < count && at != result && walked < count; walked += 1)
    {
        const uint32_t next = parents[at];
        parents[at] = result;
        at = next;
    }
    return result;
}

// The events of `events` whose operation is `operation` (C_BINDING_DECLARE or C_BINDING_USE), in order, into `list`
// (room for every event), and how many: 64 events' operations a mask, their indices compressed 16 at a time. The
// binding answers' passes over declarations or uses visit these alone on the vector path (#529).
static uint32_t check_input_operation_events(const CBindings* events, uint8_t operation, uint32_t* list)
{
    const __m512i lanes16 = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    uint32_t found = 0;
    for (uint32_t first = 0; first < events->count; first += 64)
    {
        const uint32_t length = events->count - first < 64 ? events->count - first : 64;
        const __mmask64 lanes = length == 64 ? UINT64_MAX : _bzhi_u64(UINT64_MAX, length);
        const __m512i operations = _mm512_maskz_loadu_epi8(lanes, events->operation + first);
        const uint64_t matches = _mm512_mask_cmpeq_epi8_mask(lanes, operations, _mm512_set1_epi8((char)operation));
        for (uint32_t part = 0; part < length; part += 16)
        {
            const __mmask16 selected = (__mmask16)(matches >> part);
            _mm512_mask_compressstoreu_epi32(list + found, selected, _mm512_add_epi32(_mm512_set1_epi32((int32_t)(first + part)), lanes16));
            found += (uint32_t)__builtin_popcount(selected);
        }
    }
    return found;
}

// -Wunused-variable, -Wunused-function and -Wunused-parameter facts, from the resolved binding events: a declaration
// no use names. Its lexical scope decides completion; a for initializer's scope includes the entire loop statement.
// Its three passes act at declare events alone, and read the chains' columns only at them: the vector path visits the
// declare events (`list`, `visited` of them, check_input_operation_events), where it visited every event, most of them
// uses, in each pass, with a branch on each one's operation; the scalar path, its twin, visits every event (`list` null
// and `visited` the events' count, #529).
static void check_input_unused_events(CheckInputReader* reader, const uint8_t* used, const uint8_t* used_tokens, const uint8_t* parameters,
                                      uint8_t* reported, const uint32_t* list, uint32_t visited)
{
    CheckInput* output = reader->output;
    const Parser* parser = reader->parser;
    const CBindings* events = &reader->bindings;
    const uint32_t count = parser->tokens.count;
    uint32_t* scope_parents = NULL;
    uint64_t scope_work = 0;
    CheckWarningBuffer pending = { .arena = reader->scratch };
    bool pending_for = false;
    uint32_t* chain_first = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, events->count);
    uint32_t* chain_definition = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, events->count);
    // A declaration inherits an earlier declaration's unused/used mark, as clang's redeclarations do: an attribute on
    // a prototype covers the definition that follows it. Carried along the chain in one pass (#1351), not walked back
    // from each declaration. Only objects' and functions' marks are read: any other kind is suppressed by itself.
    uint8_t* chain_marked = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, events->count);
    // The events the passes visit (check_input_binding_answers'): the declare events on the vector path, every event on
    // the scalar path, which passes no list.
    const bool listed = list != NULL;
    bool valid = chain_first != NULL && chain_definition != NULL && chain_marked != NULL;
    if (valid)
    {
        for (uint32_t at = 0; at < visited; at += 1)
        {
            const uint32_t event = listed ? list[at] : at;
            chain_marked[event] = 0;
            chain_definition[event] = 0;
            if (events->operation[event] != C_BINDING_DECLARE)
            {
                continue;
            }
            const uint32_t previous = events->previous[event];
            const bool linked = previous != 0 && previous <= events->count && events->operation[previous - 1] == C_BINDING_DECLARE &&
                                events->key[previous - 1] == events->key[event] && events->depth[previous - 1] == events->depth[event];
            chain_first[event] = linked ? chain_first[previous - 1] : event;
            chain_marked[event] = linked && (chain_marked[previous - 1] != 0 || check_input_unused_suppressed(output, events->declaration[previous - 1], C_BINDING_OBJECT));
        }
        for (uint32_t at = 0; valid && at < visited; at += 1)
        {
            const uint32_t event = listed ? list[at] : at;
            const uint32_t token = events->declaration[event];
            if (events->operation[event] == C_BINDING_DECLARE && token < count && output->declaration_bits != NULL &&
                check_input_bit(output->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED))
            {
                chain_definition[chain_first[event]] = token;
            }
        }
    }
    for (uint32_t at = 0; valid && at < visited; at += 1)
    {
        const uint32_t event = listed ? list[at] : at;
        const uint32_t token = events->declaration[event];
        const uint32_t kind = events->kind[event];
        // Only an ordinary object's or function's declaration can be unused (check_input_unused_suppressed), once: the
        // facts below are read for those alone, not for every use and restore (#1489).
        // A block's typedef is unused too (#2264, -Wunused-local-typedef); a file-scope one never is.
        const bool local_typedef = kind == C_BINDING_TYPEDEF && events->depth[event] != 0;
        if (events->operation[event] != C_BINDING_DECLARE || events->key[event] / events->name_capacity != C_BINDING_ORDINARY || token >= count ||
            (kind != C_BINDING_OBJECT && kind != C_BINDING_FUNCTION && !local_typedef) || reported[token])
        {
            continue;
        }
        const bool local = token < count && check_sparse_get(output->declaration_bodies, token) < count;
        const uint32_t parameter = token < count && parameters != NULL && parameters[token] != 0;
        const bool implicit = token < count && output->function_flags != NULL &&
                              (output->function_flags[token] & (CHECK_INPUT_FUNCTION_IMPLICIT | CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY)) != 0;
        const bool inline_function = kind == C_BINDING_FUNCTION && token < count && output->function_flags != NULL &&
                                     (output->function_flags[token] & CHECK_INPUT_FUNCTION_INLINE) != 0;
        const bool file_scope = !local && kind == C_BINDING_OBJECT;
        const bool main_file = !file_scope || check_input_in_main_file(reader, token);
        const bool const_variable = file_scope && check_input_type_const(reader->types, reader->declared_types[token]);
        // An invalid declaration (#1447, a recovered parameter's) is never unused, as clang's ShouldDiagnoseUnusedDecl skips it.
        const bool invalid_declaration = output->invalid_declarations != NULL && output->invalid_declarations[token] != 0;
        bool declaration_used = invalid_declaration || (parameter || local || local_typedef ? used_tokens[token] != 0 : used[event] != 0);
        // A typedef's name in a declaration's specifiers is no use event: a later token of its scope that names it is a use.
        // Only a block's typedef no use event named asks, and only its scope's tokens are read.
        const uint32_t typedef_scope = local_typedef && !declaration_used ?
                                       check_input_cached_ancestor(parser, parser->tree.parents[token], NULL, true, &scope_work) : BUSTER_CHECK_NONE;
        for (uint32_t at = token + 1; !declaration_used && typedef_scope < count && at < typedef_scope; at += 1)
        {
            declaration_used = output->declarations[at] == token;
        }
        // A block's declaration with linkage (an extern object, any function) belongs to the translation unit, not the
        // function, in clang (its semantic context, which ShouldDiagnoseUnusedDecl reads), so it is never unused; a
        // local's is one without linkage (#1447).
        const bool selected = events->operation[event] == C_BINDING_DECLARE && events->key[event] / events->name_capacity == C_BINDING_ORDINARY &&
                              token < count && !implicit && !inline_function && !declaration_used && !check_input_unused_suppressed(output, token, kind) &&
                              chain_marked[event] == 0 && main_file &&
                              (parameter || local_typedef || check_input_linkage(output->declaration_bits, token) == (local ? 0u : 1u));
        const uint32_t first = events->operation[event] == C_BINDING_DECLARE ? chain_first[event] : event;
        const uint32_t chosen = chain_definition[first] != 0 ? chain_definition[first] : events->declaration[first];
        const uint32_t id = kind == C_BINDING_FUNCTION ? CHECK_WARNING_ID_UNUSED_FUNCTION :
                            local_typedef ? CHECK_WARNING_ID_UNUSED_LOCAL_TYPEDEF :
                            parameter ? CHECK_WARNING_ID_UNUSED_PARAMETER :
                            const_variable ? CHECK_WARNING_ID_UNUSED_CONST_VARIABLE : CHECK_WARNING_ID_UNUSED_VARIABLE;
        const uint32_t event_kind = kind == C_BINDING_FUNCTION ? CHECK_WARNING_EVENT_UNUSED_FUNCTION :
                                    local_typedef ? CHECK_WARNING_EVENT_UNUSED_LOCAL_TYPEDEF :
                                    parameter ? CHECK_WARNING_EVENT_UNUSED_PARAMETER : CHECK_WARNING_EVENT_UNUSED_VARIABLE;
        if (selected && token == chosen && !reported[token])
        {
            // Clang publishes a declaration's unused warning when its scope completes: the nearest enclosing block,
            // body or compound brace, or the end of a for statement whose initializer declares it. The for-header's
            // `)` anchors that scope; its subtree ends after the entire body, including unbraced/nested controls.
            // A `struct S { } s` declaration walks past its type's own braces. A file-scope entity — an unused
            // function or a `static` object — waits for the translation unit's end, where every later
            // declaration could still have used it; sorting there keeps declaration order.
            // A block's typedef waits for the unit's end too: clang's emitAndClearUnusedLocalTypedefWarnings, as a later
            // use could still name it.
            uint32_t closing = BUSTER_CHECK_NONE;
            const bool scoped = !file_scope && kind != C_BINDING_FUNCTION && !local_typedef;
            if (scoped && scope_parents == NULL && scope_work >= count)
            {
                scope_parents = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, count);
                valid = scope_parents != NULL;
                if (valid)
                {
                    memcpy(scope_parents, parser->tree.parents, (uint64_t)count * sizeof(uint32_t));
                }
            }
            if (scoped && valid)
            {
                // Strict ancestors here: a declaration never completes its own scope.
                const uint32_t parent = parser->tree.parents[token];
                closing = parent != token ? check_input_cached_ancestor(parser, parent, scope_parents, true, &scope_work) : BUSTER_CHECK_NONE;
            }
            const bool for_scope = closing < count && parser->tree.roles[closing] == PARSE_ROLE_CLOSE_FOR;
            const uint32_t completion = for_scope ? reader->ends[closing] : closing;
            const CheckWarningEvent warning = {
                .id = id,
                .kind = event_kind,
                .token = token,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .flags = BUSTER_CHECK_WARNING_DEFAULT_IGNORED,
                .completion_kind = completion < count ? CHECK_WARNING_COMPLETE_AFTER_DELIMITER : CHECK_WARNING_COMPLETE_TU_END,
                .completion = completion < count ? completion : BUSTER_CHECK_NONE,
                .payload = {
                    .name = parser->tokens.names[token],
                    .token = event_kind == CHECK_WARNING_EVENT_UNUSED_VARIABLE && for_scope ? closing : BUSTER_CHECK_WARNING_NO_SOURCE,
                },
            };
            // From the first for initializer, stage this pass's remaining unused events privately: nested scopes
            // can share a statement end and must publish inner first. Their declarations follow that initializer;
            // no earlier warning can share its completion. Already published views remain immutable.
            pending_for |= for_scope;
            valid = valid && (pending_for ? check_warning_append(&pending, &warning, NULL, 0) : check_input_warning_event(reader, &warning, NULL, 0));
            if (!valid)
            {
                output->resource = true;
            }
            else
            {
                reported[token] = 1;
            }
        }
    }
    const uint32_t warnings = pending.count;
    const CheckWarningEvent* const staged = pending.events;
    uint64_t* keys = valid && warnings > 1 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint64_t, (uint64_t)warnings * 2) : NULL;
    valid &= warnings <= 1 || keys != NULL;
    uint32_t varying = 0;
    for (uint32_t i = 0; valid && warnings > 1 && i < warnings; i += 1)
    {
        const CheckWarningEvent* event = staged + i;
        const uint32_t scope = event->payload.token != BUSTER_CHECK_WARNING_NO_SOURCE ? event->payload.token : event->completion;
        // Equal completions have nested scopes: the later anchor is the inner one. Other boundaries are sorted by
        // the diagnostic collector. Stable keys preserve a scope's declaration order and translation-unit order.
        const uint32_t order = scope < count ? count - scope : count;
        keys[i] = (uint64_t)order << 32 | i;
        varying |= order;
    }
    const uint64_t* ordered = valid && warnings > 1 ? c_bindings_sort(keys, keys + warnings, warnings, !BUSTER_SCALAR_PATH(parser->options.scalar), varying) : NULL;
    for (uint32_t i = 0; valid && i < warnings; i += 1)
    {
        valid = check_input_warning_event(reader, staged + (ordered != NULL ? (uint32_t)ordered[i] : i), NULL, 0);
    }
    output->resource |= !valid;
}

// An undeclared name's error, with the keyword or the type name its typo's search suggested (#1447,
// check_typo_suggestion): the report's detail (the keyword's index + 1) and subject (the typedef's token + 1).
static void check_input_binding_undeclared(CheckInputReader* reader, uint32_t token)
{
    CheckInput* output = reader->output;
    const uint32_t before = output->report_count;
    check_input_report(reader, token, CHECK_INPUT_UNDECLARED);
    uint32_t keyword = 0;
    uint32_t type = BUSTER_CHECK_NONE;
    check_typo_suggestion(reader, token, &keyword, &type);
    if (output->report_count == before + 1 && !output->resource)
    {
        output->reports[before].detail = keyword;
        output->reports[before].subject = type < reader->parser->tokens.count ? type + 1 : 0;
    }
}

// `resolved` has an answer for each of reader->bindings' events (only its uses' are read). Every answer is
// checked before any is published: a use names a token, and sees no event at or after itself.
// A use's retained answer. Declaration tokens carry immutable elaborated types; all writes belong to the use token.
// The deferred handoff groups every event for the same use token in one body part, including repeated query uses.
// A value's undeclared name that a typo corrects (#1447, c_check_typo.c): clang's error, recovered, says what it meant,
// and the use takes that declaration as if it were written. The declaration's handle (its declare event + 1), or 0.
static uint32_t check_input_binding_correct(CheckInputReader* reader, uint32_t token, uint32_t event)
{
    CheckInput* output = reader->output;
    const uint32_t corrected = check_typo_value(reader, token, event);
    uint32_t result = 0;
    if (corrected != BUSTER_CHECK_NONE && reader->typo->corrected_count < BUSTER_CHECK_TYPO_LIMIT)
    {
        const uint32_t before = output->report_count;
        check_input_report(reader, token, CHECK_INPUT_UNDECLARED_CORRECTED);
        if (output->report_count == before + 1 && !output->resource)
        {
            output->reports[before].note_token = reader->bindings.declaration[corrected];
        }
        const bool recovered = output->report_count == before + 1 && check_input_recover_reports(reader, before);
        result = recovered ? corrected + 1 : 0;
        if (recovered)
        {
            CheckTypo* typo = reader->typo;
            typo->corrected_events[typo->corrected_count] = event;
            typo->corrected_handles[typo->corrected_count] = result;
            typo->corrected_count += 1;
            output->corrected_tokens = output->corrected_tokens == NULL ? typo->corrected_tokens : output->corrected_tokens;
            output->corrected_tokens[output->corrected_count] = token;
            output->corrected_count += 1;
        }
    }
    return result;
}

static void check_input_binding_answer(CheckInputReader* reader, const uint32_t* resolved, uint32_t event)
{
    CheckInput* output = reader->output;
    const Parser* parser = reader->parser;
    const CBindings* events = &reader->bindings;
    const uint32_t count = parser->tokens.count;
    if (events->operation[event] == C_BINDING_USE)
    {
        const uint32_t token = events->declaration[event];
        const bool value = events->key[event] / events->name_capacity == C_BINDING_ORDINARY;
        const uint32_t handle = resolved[event] != 0 || !value ? resolved[event] : check_input_binding_correct(reader, token, event);
        const uint32_t declaration = handle != 0 ? events->declaration[handle - 1] : BUSTER_CHECK_NONE;
        const uint32_t old = output->declarations[token];
        const TypeId captured = reader->declared_types[token];
        const TypeId incomplete = check_input_binding_incomplete(reader, declaration, token);
        output->declarations[token] = declaration;
        if (events->key[event] / events->name_capacity == C_BINDING_ORDINARY)
        {
            reader->declared_types[token] = incomplete != 0 ? incomplete : declaration < count ?
                (old == declaration && captured != 0 ? captured : reader->declared_types[declaration]) : 0;
            const bool typedef_use = handle != 0 && events->kind[handle - 1] == C_BINDING_TYPEDEF;
            if (declaration < count && (reader->declared_types[token] == 0 ||
                (captured != 0 && old != declaration) ||
                (typedef_use && parser->tree.roles[token] == PARSE_ROLE_OPERAND && !reader->ignored[token])))
            {
                check_input_report(reader, token, CHECK_INPUT_DECLARATION);
            }
        }
        if (declaration == BUSTER_CHECK_NONE)
        {
            check_input_binding_undeclared(reader, token);
        }
    }
}

static bool check_deferred_binding_answers(CheckDeferred* deferred, CheckInputReader* reader, const uint32_t* resolved);

static bool check_input_binding_answers(CheckInputReader* reader, const uint32_t* resolved)
{
    CheckInput* output = reader->output;
    const Parser* parser = reader->parser;
    const CBindings* events = &reader->bindings;
    const uint32_t count = parser->tokens.count;
    bool valid = !output->resource;
    uint64_t identifiers = 0;
    uint64_t declarations = 0;
    for (uint32_t event = 0; valid && event < events->count; event += 1)
    {
        const uint8_t operation = events->operation[event];
        const uint32_t handle = operation == C_BINDING_USE ? resolved[event] : 0;
        valid = operation != C_BINDING_USE ||
                (events->declaration[event] < count && handle <= event &&
                 (handle == 0 || (events->operation[handle - 1] == C_BINDING_DECLARE && events->declaration[handle - 1] < count)));
        identifiers += operation != C_BINDING_RESTORE;
        declarations += operation == C_BINDING_DECLARE;
    }
    output->resource |= !valid;
    if (valid)
    {
        output->binding_statistics.identifiers = identifiers;
        output->binding_statistics.declarations = declarations;
    }
    // The vector path's passes visit the uses, and the declarations, each listed once in order
    // (check_input_operation_events), where each visited every event with a branch on its operation; the scalar path, the
    // twin, visits every event (#529).
    const bool listed = !BUSTER_SCALAR_PATH(parser->options.scalar);
    const uint64_t list_mark = reader->scratch != NULL ? reader->scratch->length : 0;
    uint32_t* uses = valid && listed && reader->scratch != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, events->count) : NULL;
    const uint32_t use_count = uses != NULL ? check_input_operation_events(events, C_BINDING_USE, uses) : events->count;
    const bool deferred = valid && reader->deferred != NULL && check_deferred_binding_answers(reader->deferred, reader, resolved);
    for (uint32_t at = 0; valid && !deferred && at < use_count; at += 1)
    {
        check_input_binding_answer(reader, resolved, uses != NULL ? uses[at] : at);
    }
    // A use of an invalid declaration (#1447, check_input_invalid_declare) names none, as an undeclared one doesn't, and
    // its report is quiet; so does an ambiguous one of a name two C23 definitions of an enum declare
    // (check_tag_enum_ambiguous), whose report is clang's error. Only a unit with one pays for the pass.
    const bool invalid = reader->output->invalid_count != 0;
    for (uint32_t at = 0; valid && !deferred && (invalid || reader->ambiguous_count != 0) && at < use_count; at += 1)
    {
        const uint32_t event = uses != NULL ? uses[at] : at;
        const uint32_t token = events->declaration[event];
        const uint32_t declaration = events->operation[event] == C_BINDING_USE && token < count ? output->declarations[token] : BUSTER_CHECK_NONE;
        const uint32_t pair = declaration < count && reader->ambiguous_index != NULL ? reader->ambiguous_index[declaration] : 0;
        const bool ambiguous = pair != 0 && token < reader->ambiguous_ends[pair - 1];
        // (Not a version of a function multiversioned by target strings, whose use is its overload set's, #2362: clang's
        // resolution goes on with its other versions.)
        const bool quiet = invalid && declaration < count && reader->output->invalid_declarations[declaration] != 0 &&
                           !(reader->target_versions && check_target_versions_since(reader, reader->objects[declaration]) != BUSTER_CHECK_NONE);
        if (ambiguous || quiet)
        {
            output->declarations[token] = BUSTER_CHECK_NONE;
            reader->declared_types[token] = 0;
            const uint32_t before = output->report_count;
            check_input_report(reader, token, ambiguous ? CHECK_INPUT_AMBIGUOUS : CHECK_INPUT_INVALID_USE);
            if (ambiguous && output->report_count == before + 1 && !output->resource)
            {
                const uint32_t earliest = reader->ambiguous_partners[pair - 1];
                output->reports[before].note_token = declaration;
                output->reports[before].attribute = earliest;
                // The definitions between (#1826: a third's use lists all three, as clang's candidates do), up to two, the
                // later first, packed in `value` (each plus 1; 0 for none). Only a unit with a third definition has one.
                uint32_t middle[2] = { 0, 0 };
                for (uint32_t k = 0; k < reader->ambiguous_count; k += 1)
                {
                    const uint32_t other = reader->ambiguous_declarations[k];
                    const bool between = reader->ambiguous_partners[k] == earliest && other != declaration && other < declaration && other < token;
                    const uint32_t later = between && other > middle[0] ? other : middle[0];
                    middle[1] = between && other > middle[0] ? middle[0] : between && other > middle[1] ? other : middle[1];
                    middle[0] = later;
                }
                output->reports[before].value = (uint64_t)(middle[0] != 0 ? middle[0] + 1 : 0) | (uint64_t)(middle[1] != 0 ? middle[1] + 1 : 0) << 32;
            }
        }
    }
    valid &= !output->resource;
    const bool unused_columns = check_sparse_present(output->body_declarations) && check_sparse_present(output->parameters) &&
                                check_sparse_present(output->parameter_next) && check_sparse_present(output->declaration_bodies) &&
                                output->declaration_kinds != NULL && output->declaration_bits != NULL;
    // (A unit whose only reports are quiet uses of invalid declarations, #1447, has its unused declarations' warnings as a
    // valid one does: clang gives them, and its uses mark their declarations used.)
    bool quiet_only = true;
    for (uint32_t r = 0; quiet_only && r < output->report_count; r += 1)
    {
        quiet_only = output->reports[r].message == CHECK_INPUT_INVALID_USE;
    }
    if (valid && unused_columns && quiet_only && events->count != 0)
    {
        uint8_t* used = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, events->count);
        uint8_t* used_tokens = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
        uint8_t* parameters = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
        uint8_t* reported = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
        if (used != NULL && used_tokens != NULL && parameters != NULL && reported != NULL)
        {
            memset(used, 0, events->count);
            memset(used_tokens, 0, count);
            memset(parameters, 0, count);
            memset(reported, 0, count);
            uint32_t* declares = uses != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, events->count) : NULL;
            const uint32_t declare_count = declares != NULL ? check_input_operation_events(events, C_BINDING_DECLARE, declares) : events->count;
            for (uint32_t at_use = 0; at_use < use_count; at_use += 1)
            {
                const uint32_t event = uses != NULL ? uses[at_use] : at_use;
                if (events->operation[event] == C_BINDING_USE)
                {
                    const uint32_t handle = check_typo_handle(reader, resolved, event);
                    if (handle != 0 && handle <= events->count && events->operation[handle - 1] == C_BINDING_DECLARE)
                    {
                        // A use binds the whole declaration chain: a prototype and its definition are one entity,
                        // and a use of either makes every declaration in the chain used. A declaration that an earlier
                        // use's walk marked ends the walk: that walk went on from it to the chain's start, as every
                        // declaration of a chain has the key and the depth of the walk's first (#529). A block's
                        // declaration with linkage redeclares the outer one with linkage it hides, so the walk goes on
                        // out to it: `static int s; void f(void) { extern int s; (void)s; }` uses s, as clang marks the
                        // entity's every redeclaration (#1447).
                        // (Linkage is read only where the walk would cross a depth, which few chains do.)
                        uint32_t at = handle;
                        uint32_t depth = events->depth[handle - 1];
                        uint32_t last = BUSTER_CHECK_NONE;
                        for (uint32_t steps = 0; at != 0 && steps < events->count; steps += 1)
                        {
                            const uint32_t declaration = at - 1;
                            const uint32_t token = events->declaration[declaration];
                            const bool crossing = events->depth[declaration] != depth;
                            const bool outward = crossing && events->depth[declaration] < depth && last < count && token < count &&
                                                 check_input_linkage(output->declaration_bits, last) != 0 &&
                                                 check_input_linkage(output->declaration_bits, token) != 0;
                            if (events->operation[declaration] != C_BINDING_DECLARE || events->key[declaration] != events->key[handle - 1] ||
                                (crossing && !outward) || used[declaration] != 0)
                            {
                                break;
                            }
                            used[declaration] = 1;
                            if (token < count)
                            {
                                used_tokens[token] = 1;
                            }
                            depth = events->depth[declaration];
                            last = token;
                            at = events->previous[declaration];
                        }
                    }
                }
            }
            // A use that bound an earlier declaration also keeps a later definition of the same entity used.
            for (uint32_t at_declare = 0; at_declare < declare_count; at_declare += 1)
            {
                const uint32_t event = declares != NULL ? declares[at_declare] : at_declare;
                const uint32_t previous = events->previous[event];
                if (events->operation[event] == C_BINDING_DECLARE && previous != 0 && previous <= events->count &&
                    events->operation[previous - 1] == C_BINDING_DECLARE && events->key[previous - 1] == events->key[event] &&
                    events->depth[previous - 1] == events->depth[event] && used[previous - 1] != 0)
                {
                    used[event] = 1;
                }
            }
            if (check_input_parameter_marks(output, parser, parameters))
            {
                check_input_unused_events(reader, used, used_tokens, parameters, reported, declares, declare_count);
            }
        }
        else if (used == NULL || (count != 0 && (used_tokens == NULL || parameters == NULL || reported == NULL)))
        {
            // A nonempty stream's scratch failure is a resource error, not a quiet skip of the analysis: the
            // caller already published its resource verdict, and check_input_run tests the flag, not this return.
            output->resource = true;
            valid = false;
        }
    }
    if (reader->scratch != NULL)
    {
        arena_pop_to(reader->scratch, list_mark);
    }
    return valid && output->report_count == 0;
}

// Resolve the reader's own events: DIRECT reads each use's retained answer, STACKS replays them independently and
// BATCH sorts them. The replay's and the sort's scratch is released before returning.
static bool check_input_binding_events(CheckInputReader* reader, CheckInputBindingMode mode)
{
    CheckInput* output = reader->output;
    const CBindings* bindings = &reader->bindings;
    const uint64_t mark = reader->scratch->length;
    output->binding_event_count = bindings->count;
    output->binding_statistics = (CBindingStatistics){ 0 };
    const bool direct = mode == CHECK_INPUT_BINDINGS_DIRECT;
    bool valid = bindings->valid && !output->resource && (mode == CHECK_INPUT_BINDINGS_DIRECT || mode == CHECK_INPUT_BINDINGS_STACKS || mode == CHECK_INPUT_BINDINGS_BATCH);
    valid = valid && (!direct || bindings->count == 0 || bindings->previous != 0);
    uint32_t* resolved = valid && !direct && bindings->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, bindings->count) : 0;
    valid = valid && (direct || bindings->count == 0 || resolved != 0);
    CBindingStatistics statistics = { 0 };
    if (valid && !direct && bindings->count != 0)
    {
        valid = mode == CHECK_INPUT_BINDINGS_STACKS ? c_bindings_scalar(bindings, resolved, reader->scratch) :
                                                      c_bindings_batch(bindings, resolved, reader->scratch, &statistics);
    }
    output->resource |= !valid;
    valid = valid && check_input_binding_answers(reader, direct ? bindings->previous : resolved);
    output->binding_statistics.walks = statistics.walks;
    output->binding_statistics.walked_uses = statistics.walked_uses;
    output->binding_statistics.fallback_uses = statistics.fallback_uses;
    arena_pop_to(reader->scratch, mark);
    return valid && !output->resource && output->report_count == 0;
}

static bool check_input_binding_columns(CheckInputReader* reader, const ParseBindings* parsed)
{
    CheckInput* output = reader->output;
    const Parser* parser = reader->parser;
    const CBindings* events = &reader->bindings;
    const uint32_t count = parser->tokens.count;
    bool valid = parsed->valid && parsed->count == count && events->valid && !output->resource &&
                 parsed->token_events != 0 && parsed->declarations != 0 && parsed->namespaces != 0 && parsed->kinds != 0;
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        const uint32_t event = parsed->token_events[token];
        const uint32_t declaration = parsed->declarations[token];
        valid = event <= parsed->events.count && (declaration < count || declaration == BUSTER_PARSE_NONE);
    }
    for (uint32_t event = 0; valid && event < events->count; event += 1)
    {
        valid = events->operation[event] != C_BINDING_USE || events->declaration[event] < count;
    }
    output->resource |= !valid;
    for (uint32_t event = 0; valid && event < events->count; event += 1)
    {
        const uint8_t operation = events->operation[event];
        output->binding_statistics.identifiers += operation != C_BINDING_RESTORE;
        output->binding_statistics.declarations += operation == C_BINDING_DECLARE;
        if (operation == C_BINDING_USE)
        {
            const uint32_t token = events->declaration[event];
            const uint32_t old = output->declarations[token];
            const bool ordinary = events->key[event] / events->name_capacity == C_BINDING_ORDINARY;
            const bool recorded = parsed->token_events[token] != 0 && parsed->namespaces[token] == C_BINDING_ORDINARY;
            const uint32_t found = ordinary && recorded ? parsed->declarations[token] : old;
            const uint32_t corrected = found == BUSTER_CHECK_NONE && ordinary ? check_input_binding_correct(reader, token, event) : 0;
            const uint32_t declaration = corrected != 0 ? events->declaration[corrected - 1] : found;
            const TypeId captured = reader->declared_types[token];
            const TypeId incomplete = check_input_binding_incomplete(reader, declaration, token);
            output->declarations[token] = declaration;
            if (ordinary)
            {
                reader->declared_types[token] = incomplete != 0 ? incomplete : declaration < count ?
                    (old == declaration && captured != 0 ? captured : reader->declared_types[declaration]) : 0;
                const bool typedef_use = recorded ? parsed->kinds[token] == C_BINDING_TYPEDEF :
                                                    declaration < count && reader->declaration_kinds[declaration] == C_BINDING_TYPEDEF;
                if (declaration < count && (reader->declared_types[token] == 0 ||
                    (captured != 0 && old != declaration) ||
                    (typedef_use && parser->tree.roles[token] == PARSE_ROLE_OPERAND && !reader->ignored[token])))
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
            }
            if (declaration == BUSTER_CHECK_NONE)
            {
                check_input_binding_undeclared(reader, token);
            }
        }
    }
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        const uint32_t declaration = parsed->declarations[token];
        if (parsed->token_events[token] != 0 && parsed->namespaces[token] == C_BINDING_TAG &&
            declaration < count && output->declarations[token] != BUSTER_CHECK_NONE)
        {
            const uint32_t canonical = output->declarations[declaration];
            if (canonical < count && reader->declared_types[canonical] != 0)
            {
                output->declarations[token] = canonical;
            }
            else
            {
                check_input_report(reader, token, CHECK_INPUT_DECLARATION);
            }
        }
    }
    return valid && !output->resource && output->report_count == 0;
}

// The parser modes: the parser's own grammar replay (parse_bindings_build) and resolution, mapped onto the
// reader's use events. Kept for measurement and as an oracle; the reader's events already hold these answers.
static bool check_input_binding_parser(CheckInputReader* reader, bool scalar)
{
    CheckInput* output = reader->output;
    const uint64_t mark = reader->arena->length;
    Parser parser = *reader->parser;
    parser.scratch = *reader->scratch;
    ParseBindings parsed = { 0 };
    output->binding_event_count = reader->bindings.count;
    output->binding_statistics = (CBindingStatistics){ 0 };
    bool valid = reader->bindings.valid && !output->resource && reader->arena != reader->scratch;
    if (valid && parser.meanings.meanings == 0)
    {
        // Validated diagnostic-prefix views deliberately borrow no owning parser arenas. Reconstitute only
        // their initial identifier meanings in temporary storage; ParseBindings replays the retained grammar.
        parser.meanings.capacity = reader->pp->table->count;
        parser.meanings.meanings = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, parser.meanings.capacity);
        valid = parser.meanings.meanings != 0;
        if (valid)
        {
            memset(parser.meanings.meanings, PARSE_MEANING_ORDINARY, parser.meanings.capacity);
            for (uint32_t name = 0; name < BUSTER_PARSE_PREDECLARED_TYPEDEFS; name += 1)
            {
                valid &= parser.predeclared[name] < parser.meanings.capacity;
                if (valid)
                {
                    parser.meanings.meanings[parser.predeclared[name]] = PARSE_MEANING_TYPEDEF;
                }
            }
        }
    }
    valid = valid && parse_bindings_build(&parsed, &parser, reader->arena) && parse_bindings_resolve(&parsed, scalar, reader->scratch);
    output->resource |= !valid;
    if (valid)
    {
        output->parser_binding_event_count = parsed.events.count;
        output->parser_binding_statistics = parsed.statistics;
        valid = check_input_binding_columns(reader, &parsed);
    }
    arena_pop_to(reader->arena, mark);
    return valid && !output->resource && output->report_count == 0;
}

static bool check_input_bindings(CheckInputReader* reader, CheckInputBindingMode mode)
{
    const CheckInputBindingMode selected = mode != CHECK_INPUT_BINDINGS_DEFAULT ? mode :
                                           BUSTER_SCALAR_PATH(reader->parser->options.scalar) ? CHECK_INPUT_BINDINGS_STACKS : CHECK_INPUT_BINDINGS_DIRECT;
    const bool parser = selected == CHECK_INPUT_BINDINGS_PARSER_STACKS || selected == CHECK_INPUT_BINDINGS_PARSER_BATCH;
    return parser ? check_input_binding_parser(reader, selected == CHECK_INPUT_BINDINGS_PARSER_STACKS) : check_input_binding_events(reader, selected);
}

