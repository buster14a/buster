// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Retain source-ordered floating environment and complex range settings. A setting at the beginning of a
// compound statement ends with that compound; file settings persist. The optional graph column carries runtime
// floating policy. Complex range is a permission to relax evaluation, so retaining full-range actions is valid.
// FP_CONTRACT, reassociation, reciprocal evaluation and float_control(precise, off) grant permissions that this
// compiler need not use. Exception-sensitive scopes use the existing runtime environment path, which retains their
// operations and conversions. File-scope float_control push/pop snapshots all settings, including STDC's.

#define CHECK_FP_PRAGMA_FENV_ACCESS 0
#define CHECK_FP_PRAGMA_CX_LIMITED_RANGE 1
#define CHECK_FP_PRAGMA_FP_CONTRACT 2
#define CHECK_FP_PRAGMA_PRECISE 3
#define CHECK_FP_PRAGMA_EXCEPTIONS 4
#define CHECK_FP_PRAGMA_REASSOCIATE 5
#define CHECK_FP_PRAGMA_RECIPROCAL 6
#define CHECK_FP_PRAGMA_STATE_COUNT 7
#define CHECK_FP_PRAGMA_PUSH 7
#define CHECK_FP_PRAGMA_POP 8
#define CHECK_FP_PRAGMA_OPTIONS 9
#define CHECK_FP_PRAGMA_UNKNOWN_STATE 0x01
#define CHECK_FP_PRAGMA_EXTRA_TOKENS 0x02
#define CHECK_FP_PRAGMA_INVALID_PLACEMENT 0x04
#define CHECK_FP_PRAGMA_EMPTY_STACK 0x08
#define CHECK_FP_ERROR_FENV_PRECISE 1
#define CHECK_FP_ERROR_EXCEPT_PRECISE 2
#define CHECK_FP_ERROR_PRECISE_EXCEPT 3
#define CHECK_FP_ERROR_PRECISE_FENV 4
#define CHECK_FP_ERROR_STACK_SCOPE 5
#define CHECK_FP_EXCEPTIONS_DEFAULT 3

typedef struct CheckFpState CheckFpState;
struct CheckFpState
{
    uint8_t values[CHECK_FP_PRAGMA_STATE_COUNT];
};

typedef struct CheckFpDirective CheckFpDirective;
struct CheckFpDirective
{
    CheckFpState state;
    uint32_t end;
    uint8_t mask;
    uint8_t kind;
    bool push;
    bool pop;
};

struct CheckInputFpPragma
{
    uint32_t pragma;
    uint32_t source_begin;
    uint32_t source_end;
    uint32_t token;
    uint32_t scope;
    uint32_t scope_end;
    uint8_t kind;
    uint8_t environment;
    uint8_t warnings;
    uint8_t warning_severity;
    uint8_t error;
    bool warning_quiet;
    bool valid;
};

static uint8_t check_input_fp_warning_policy(Preprocessor* pp, uint32_t source, bool* quiet);

static uint8_t check_input_fp_environment(CheckFpState state)
{
    const uint8_t exceptions = state.values[CHECK_FP_PRAGMA_EXCEPTIONS];
    return state.values[CHECK_FP_PRAGMA_FENV_ACCESS] != 0 || (exceptions != 0 && exceptions != CHECK_FP_EXCEPTIONS_DEFAULT) ? BUSTER_CHECK_FP_ENV_ACCESS : 0;
}

static const char* check_input_fp_error_message(uint8_t error)
{
    const char* result = NULL;
    switch (error)
    {
        case CHECK_FP_ERROR_FENV_PRECISE: result = "'#pragma STDC FENV_ACCESS ON' is illegal when precise is disabled"; break;
        case CHECK_FP_ERROR_EXCEPT_PRECISE: result = "'#pragma float_control(except, on)' is illegal when precise is disabled"; break;
        case CHECK_FP_ERROR_PRECISE_EXCEPT: result = "'#pragma float_control(precise, off)' is illegal when except is enabled"; break;
        case CHECK_FP_ERROR_PRECISE_FENV: result = "'#pragma float_control(precise, off)' is illegal when fenv_access is enabled"; break;
        case CHECK_FP_ERROR_STACK_SCOPE: result = "'#pragma float_control push/pop' can only appear at file or namespace scope or within a language linkage specification"; break;
        default: break;
    }
    return result;
}

static const CheckInputFpPragma* check_input_fp_report(const CheckInput* input, const CheckInputReport* report)
{
    const CheckInputFpPragma* result = NULL;
    for (uint32_t i = 0; result == NULL && report->pragma != 0 && i < input->fp_pragma_count; i += 1)
    {
        const CheckInputFpPragma* record = input->fp_pragmas + i;
        result = record->pragma + 1 == report->pragma && record->error != 0 ? record : NULL;
    }
    return result;
}

static bool check_input_fp_name(const CheckInputReader* reader, uint32_t source, const char* spelling)
{
    const PPTokens* tokens = &reader->pp->output;
    const uint8_t kind = source < tokens->count ? tokens->kinds[source] : 0;
    const uint32_t name = source < tokens->count ? tokens->values[source] : 0;
    const uint32_t length = (uint32_t)strlen(spelling);
    return (kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END)) &&
           name < reader->pp->table->count && intern_length(reader->pp->table, name) == length &&
           memcmp(intern_bytes(reader->pp->table, name), spelling, length) == 0;
}

// Only complete supported extension forms acquire a handled mark. A recognized prefix must not swallow an
// unsupported evaluation method or malformed punctuation.
// Unsupported forms remain with the semantic-pragma diagnostic, which retains their original spelling.
// The unqualified fenv_access handler is not registered in Clang's accepted Linux mode; it belongs to the unknown
// pragma policy. The standard FENV_ACCESS form below supplies the runtime environment semantics.
static bool check_input_fp_extension(CheckInputReader* reader, uint64_t** macros, uint32_t source, CheckFpDirective* result)
{
    const PPTokens* output = &reader->pp->output;
    uint32_t last = source;
    const bool candidate = source < output->count && output->count - source >= 6 &&
                           (check_input_fp_name(reader, source + 2, "float_control") ||
                            (check_input_fp_name(reader, source + 2, "clang") && check_input_fp_name(reader, source + 3, "fp")));
    const bool directive = candidate && parse_pragma_at(output, source, &last);
    const uint32_t length = directive ? last - source : 0;
    const bool clang = directive && pp_token_is_name(reader->pp, output, source + 2, last, "clang");
    CheckFpDirective selected = { .end = last, .kind = CHECK_FP_PRAGMA_PRECISE };
    bool supported = false;
    if (clang && length >= 8 && (length - 4) % 4 == 0)
    {
        supported = true;
        for (uint32_t at = source + 4; supported && at < last; at += 4)
        {
            const bool contract = pp_token_is_name(reader->pp, output, at, last, "contract");
            const bool reassociate = pp_token_is_name(reader->pp, output, at, last, "reassociate");
            const bool reciprocal = pp_token_is_name(reader->pp, output, at, last, "reciprocal");
            const bool exceptions = pp_token_is_name(reader->pp, output, at, last, "exceptions");
            const bool on = pp_token_is_name(reader->pp, output, at + 2, last, "on");
            const bool off = pp_token_is_name(reader->pp, output, at + 2, last, "off");
            const bool fast = pp_token_is_name(reader->pp, output, at + 2, last, "fast");
            const bool ignore = pp_token_is_name(reader->pp, output, at + 2, last, "ignore");
            const bool maytrap = pp_token_is_name(reader->pp, output, at + 2, last, "maytrap");
            const bool strict = pp_token_is_name(reader->pp, output, at + 2, last, "strict");
            supported = output->kinds[at + 1] == C_TOKEN_L_PAREN && output->kinds[at + 3] == C_TOKEN_R_PAREN &&
                        ((contract && (on || off || fast)) || ((reassociate || reciprocal) && (on || off)) ||
                         (exceptions && (ignore || maytrap || strict)));
            const uint8_t kind = contract ? CHECK_FP_PRAGMA_FP_CONTRACT : reassociate ? CHECK_FP_PRAGMA_REASSOCIATE :
                                 reciprocal ? CHECK_FP_PRAGMA_RECIPROCAL : CHECK_FP_PRAGMA_EXCEPTIONS;
            selected.mask |= (uint8_t)(1u << kind);
            selected.state.values[kind] = exceptions ? (strict ? 2 : (uint8_t)maytrap) : fast ? 2 : (uint8_t)on;
            selected.kind = length == 8 ? kind : CHECK_FP_PRAGMA_OPTIONS;
        }
    }
    else if (!clang && length >= 6 && output->kinds[source + 3] == C_TOKEN_L_PAREN && output->kinds[last - 1] == C_TOKEN_R_PAREN)
    {
        const bool precise = pp_token_is_name(reader->pp, output, source + 4, last, "precise");
        const bool exceptions = pp_token_is_name(reader->pp, output, source + 4, last, "except");
        const bool push = pp_token_is_name(reader->pp, output, source + 4, last, "push");
        const bool pop = pp_token_is_name(reader->pp, output, source + 4, last, "pop");
        if ((push || pop) && length == 6)
        {
            supported = true;
            selected.kind = push ? CHECK_FP_PRAGMA_PUSH : CHECK_FP_PRAGMA_POP;
            selected.push = push;
            selected.pop = pop;
        }
        else if (precise || exceptions)
        {
            const bool on = length == 6 || pp_token_is_name(reader->pp, output, source + 6, last, "on");
            const bool off = length >= 8 && pp_token_is_name(reader->pp, output, source + 6, last, "off");
            const bool pushing = length >= 8 && pp_token_is_name(reader->pp, output, source + 6, last, "push");
            const bool trailing_push = length == 10 && output->kinds[source + 7] == C_TOKEN_COMMA &&
                                       pp_token_is_name(reader->pp, output, source + 8, last, "push");
            supported = length == 6 || ((length == 8 || trailing_push) && output->kinds[source + 5] == C_TOKEN_COMMA && (on || off || pushing));
            selected.kind = precise ? CHECK_FP_PRAGMA_PRECISE : CHECK_FP_PRAGMA_EXCEPTIONS;
            selected.mask = (uint8_t)(1u << selected.kind);
            selected.state.values[selected.kind] = off ? 0 : exceptions ? 2 : 1;
            selected.push = pushing || trailing_push;
        }
    }
    // Semantic preprocessing expands these operands at their source point. A caller supplying the ordinary -E
    // stream has no such proof: even 'on' can have been a macro, so retain its conservative history guard. The
    // namespace and handler names themselves are never expanded.
    const uint32_t first = source + (clang ? 4 : 3);
    for (uint32_t token = first; !reader->pp->options.semantic_pragmas && supported && token < last; token += 1)
    {
        const IdentId name = check_input_pragma_name(reader, token);
        supported = name == 0 || !check_input_pragma_macro(reader, macros, name);
    }
    *result = selected;
    return supported;
}

static bool check_input_fp_pragmas(CheckInputReader* reader, uint8_t* handled)
{
    const Parser* parser = reader->parser;
    CheckInput* input = reader->output;
    const uint64_t mark = reader->scratch->length;
    const uint64_t retained = reader->arena->length;
    uint64_t* macros = 0;
    uint32_t count = 0;
    for (uint32_t pragma = 0; pragma < parser->tokens.pragma_count; pragma += 1)
    {
        const uint32_t source = parser->tokens.pragma_sources[pragma];
        CheckFpDirective directive = { 0 };
        const bool standard = check_input_fp_name(reader, source + 2, "STDC") &&
                              (check_input_fp_name(reader, source + 3, "FENV_ACCESS") || check_input_fp_name(reader, source + 3, "CX_LIMITED_RANGE") ||
                               check_input_fp_name(reader, source + 3, "FP_CONTRACT"));
        count += standard || check_input_fp_extension(reader, &macros, source, &directive);
    }
    typedef struct CheckFpScope CheckFpScope;
    struct CheckFpScope
    {
        uint32_t opening;
        uint32_t closing;
        CheckFpState state;
        bool compound;
    };
    CheckFpScope* scopes = count != 0 ? arena_push_array(reader->scratch, (uint64_t)parser->tokens.count + 1, sizeof(CheckFpScope), _Alignof(CheckFpScope)) : 0;
    CheckFpState* stack = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckFpState, count) : NULL;
    CheckInputFpPragma* records = count != 0 ? arena_push_array(reader->arena, count, sizeof(CheckInputFpPragma), _Alignof(CheckInputFpPragma)) : 0;
    uint8_t* environments = count != 0 ? arena_push_array(reader->arena, (uint64_t)parser->tokens.count + 1, sizeof(uint8_t), 1) : 0;
    uint8_t* complex_ranges = count != 0 ? arena_push_array(reader->arena, (uint64_t)parser->tokens.count + 1, sizeof(uint8_t), 1) : 0;
    uint8_t* nodes = count != 0 ? arena_push_array(reader->arena, input->graph.capacity, sizeof(uint8_t), 1) : 0;
    const bool valid = !input->resource && (count == 0 || (scopes != 0 && stack != NULL && records != 0 && environments != 0 && complex_ranges != 0 && nodes != 0));
    reader->output->resource |= !valid;
    if (valid && count != 0)
    {
        input->fp_pragmas = records;
        input->fp_environments = environments;
        input->cx_limited_ranges = complex_ranges;
        input->graph.fp_environments = nodes;
        memset(input->graph.fp_environments, 0, input->graph.capacity);
        uint32_t depth = 0;
        uint32_t pragma = 0;
        uint32_t pushed = 0;
        CheckFpState state = { .values = { [CHECK_FP_PRAGMA_PRECISE] = 1, [CHECK_FP_PRAGMA_FP_CONTRACT] = 1,
                                          [CHECK_FP_PRAGMA_EXCEPTIONS] = CHECK_FP_EXCEPTIONS_DEFAULT } };
        for (uint32_t token = 0; token <= parser->tokens.count; token += 1)
        {
            while (pragma < parser->tokens.pragma_count && parser->tokens.pragma_positions[pragma] == token)
            {
                const uint32_t source = parser->tokens.pragma_sources[pragma];
                const bool complex = check_input_fp_name(reader, source + 3, "CX_LIMITED_RANGE");
                const bool contract = check_input_fp_name(reader, source + 3, "FP_CONTRACT");
                const bool file_boundary = token == 0 || parser->tree.roles[token - 1] == PARSE_ROLE_TERMINATOR ||
                                           parser->tree.roles[token - 1] == PARSE_ROLE_CLOSE_BODY;
                const bool placement = depth == 0 ? file_boundary : scopes[depth - 1].compound && token == scopes[depth - 1].opening + 1;
                if (check_input_fp_name(reader, source + 2, "STDC") && (complex || contract || check_input_fp_name(reader, source + 3, "FENV_ACCESS")))
                {
                    uint32_t end = source;
                    const bool directive = parse_pragma_at(&reader->pp->output, source, &end);
                    const bool on = directive && check_input_fp_name(reader, source + 4, "ON");
                    const bool off = directive && (check_input_fp_name(reader, source + 4, "OFF") || check_input_fp_name(reader, source + 4, "DEFAULT"));
                    const bool recognized = on || off;
                    const bool precise = state.values[CHECK_FP_PRAGMA_PRECISE] != 0 && state.values[CHECK_FP_PRAGMA_REASSOCIATE] == 0 &&
                                         state.values[CHECK_FP_PRAGMA_RECIPROCAL] == 0;
                    const bool accepted = directive && (!recognized || (end == source + 5 && placement && (complex || contract || !on || precise)));
                    if (accepted && recognized && complex)
                    {
                        state.values[CHECK_FP_PRAGMA_CX_LIMITED_RANGE] = on;
                    }
                    else if (accepted && recognized)
                    {
                        state.values[contract ? CHECK_FP_PRAGMA_FP_CONTRACT : CHECK_FP_PRAGMA_FENV_ACCESS] = on;
                    }
                    input->fp_pragmas[input->fp_pragma_count++] = (CheckInputFpPragma)
                    {
                        .pragma = pragma,
                        .source_begin = source,
                        .source_end = end + 1,
                        .token = token,
                        .scope = depth != 0 ? scopes[depth - 1].opening : BUSTER_CHECK_NONE,
                        .scope_end = depth != 0 ? scopes[depth - 1].closing + 1 : parser->tokens.count,
                        .kind = complex ? CHECK_FP_PRAGMA_CX_LIMITED_RANGE : contract ? CHECK_FP_PRAGMA_FP_CONTRACT : CHECK_FP_PRAGMA_FENV_ACCESS,
                        .environment = state.values[complex ? CHECK_FP_PRAGMA_CX_LIMITED_RANGE : contract ? CHECK_FP_PRAGMA_FP_CONTRACT : CHECK_FP_PRAGMA_FENV_ACCESS],
                        .warnings = !recognized ? CHECK_FP_PRAGMA_UNKNOWN_STATE : end != source + 5 ? CHECK_FP_PRAGMA_EXTRA_TOKENS : 0,
                        .error = directive && end == source + 5 && placement && !complex && !contract && on && !precise ? CHECK_FP_ERROR_FENV_PRECISE : 0,
                        .valid = accepted,
                    };
                    handled[pragma] = accepted ? 2 : 3;
                }
                else
                {
                    CheckFpDirective directive = { 0 };
                    if (placement && check_input_fp_extension(reader, &macros, source, &directive))
                    {
                        const bool precise = state.values[CHECK_FP_PRAGMA_PRECISE] != 0 && state.values[CHECK_FP_PRAGMA_REASSOCIATE] == 0 &&
                                             state.values[CHECK_FP_PRAGMA_RECIPROCAL] == 0;
                        const bool float_control = check_input_fp_name(reader, source + 2, "float_control");
                        const bool disable_precise = directive.kind == CHECK_FP_PRAGMA_PRECISE && directive.state.values[CHECK_FP_PRAGMA_PRECISE] == 0;
                        const bool fenv = state.values[CHECK_FP_PRAGMA_FENV_ACCESS] != 0;
                        const bool strict = state.values[CHECK_FP_PRAGMA_EXCEPTIONS] == 2 || (state.values[CHECK_FP_PRAGMA_EXCEPTIONS] == CHECK_FP_EXCEPTIONS_DEFAULT && fenv);
                        const uint8_t error = (directive.push || directive.pop) && depth != 0 ? CHECK_FP_ERROR_STACK_SCOPE :
                                              disable_precise && strict ? CHECK_FP_ERROR_PRECISE_EXCEPT :
                                              disable_precise && fenv ? CHECK_FP_ERROR_PRECISE_FENV :
                                              float_control && directive.kind == CHECK_FP_PRAGMA_EXCEPTIONS && directive.state.values[CHECK_FP_PRAGMA_EXCEPTIONS] != 0 && !precise ?
                                                  CHECK_FP_ERROR_EXCEPT_PRECISE : 0;
                        const bool accepted = error == 0;
                        const bool empty = accepted && directive.pop && pushed == 0;
                        if (accepted)
                        {
                            if (directive.push)
                            {
                                stack[pushed++] = state;
                            }
                            if (directive.pop && !empty)
                            {
                                state = stack[--pushed];
                            }
                            for (uint32_t kind = 0; kind < CHECK_FP_PRAGMA_STATE_COUNT; kind += 1)
                            {
                                if ((directive.mask & (1u << kind)) != 0)
                                {
                                    state.values[kind] = directive.state.values[kind];
                                }
                            }
                            if (directive.kind == CHECK_FP_PRAGMA_PRECISE)
                            {
                                const bool enabled = state.values[CHECK_FP_PRAGMA_PRECISE] != 0;
                                state.values[CHECK_FP_PRAGMA_REASSOCIATE] = !enabled;
                                state.values[CHECK_FP_PRAGMA_RECIPROCAL] = !enabled;
                                state.values[CHECK_FP_PRAGMA_FP_CONTRACT] = enabled ? 1 : 2;
                            }
                        }
                        input->fp_pragmas[input->fp_pragma_count++] = (CheckInputFpPragma)
                        {
                            .pragma = pragma,
                            .source_begin = source,
                            .source_end = directive.end + 1,
                            .token = token,
                            .scope = depth != 0 ? scopes[depth - 1].opening : BUSTER_CHECK_NONE,
                            .scope_end = depth != 0 ? scopes[depth - 1].closing + 1 : parser->tokens.count,
                            .kind = directive.kind,
                            .environment = directive.kind < CHECK_FP_PRAGMA_STATE_COUNT ? state.values[directive.kind] : check_input_fp_environment(state),
                            .warnings = empty ? CHECK_FP_PRAGMA_EMPTY_STACK : 0,
                            .error = error,
                            .valid = accepted,
                        };
                        if (empty)
                        {
                            CheckInputFpPragma* record = input->fp_pragmas + input->fp_pragma_count - 1;
                            record->warning_severity = check_input_fp_warning_policy(reader->pp, source + 2, &record->warning_quiet);
                        }
                        handled[pragma] = accepted ? 2 : 3;
                    }
                }
                pragma += 1;
            }
            input->fp_environments[token] = check_input_fp_environment(state);
            input->cx_limited_ranges[token] = state.values[CHECK_FP_PRAGMA_CX_LIMITED_RANGE];
            if (token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_L_BRACE)
            {
                const uint8_t role = parser->tree.roles[token];
                scopes[depth++] = (CheckFpScope)
                {
                    .opening = token,
                    .closing = parser->tree.parents[token],
                    .state = state,
                    .compound = role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_BLOCK,
                };
            }
            else if (depth != 0 && token == scopes[depth - 1].closing)
            {
                depth -= 1;
                state = scopes[depth].state;
            }
        }
    }
    else if (!valid)
    {
        arena_pop_to(reader->arena, retained);
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}
