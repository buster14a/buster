// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Retain source-ordered floating environment and complex range settings. A setting at the beginning of a
// compound statement ends with that compound; file settings persist. The optional graph column carries runtime
// floating policy. Complex range is a permission to relax evaluation, so retaining full-range actions is valid.
// FP_CONTRACT survives in the graph for source-scoped contraction; reassociation, reciprocal evaluation and
// float_control(precise, off) grant other independent permissions. Exception-sensitive scopes retain their
// operations and conversions. File-scope float_control push/pop snapshots the arithmetic settings, including STDC's.
// The last evaluation pragma's method and location persist across pop for lexical restrictions.
// Evaluation methods use the same lexical history but insert ordinary floating promotions, including in constants.

#define CHECK_FP_PRAGMA_FENV_ACCESS 0
#define CHECK_FP_PRAGMA_CX_LIMITED_RANGE 1
#define CHECK_FP_PRAGMA_FP_CONTRACT 2
#define CHECK_FP_PRAGMA_PRECISE 3
#define CHECK_FP_PRAGMA_EXCEPTIONS 4
#define CHECK_FP_PRAGMA_REASSOCIATE 5
#define CHECK_FP_PRAGMA_RECIPROCAL 6
#define CHECK_FP_PRAGMA_EVAL_METHOD 7
#define CHECK_FP_PRAGMA_STATE_COUNT 8
#define CHECK_FP_PRAGMA_PUSH 8
#define CHECK_FP_PRAGMA_POP 9
#define CHECK_FP_PRAGMA_OPTIONS 10
#define CHECK_FP_PRAGMA_UNKNOWN_STATE 0x01
#define CHECK_FP_PRAGMA_EXTRA_TOKENS 0x02
#define CHECK_FP_PRAGMA_INVALID_PLACEMENT 0x04
#define CHECK_FP_PRAGMA_EMPTY_STACK 0x08
#define CHECK_FP_ERROR_FENV_PRECISE 1
#define CHECK_FP_ERROR_EXCEPT_PRECISE 2
#define CHECK_FP_ERROR_PRECISE_EXCEPT 3
#define CHECK_FP_ERROR_PRECISE_FENV 4
#define CHECK_FP_ERROR_STACK_SCOPE 5
#define CHECK_FP_ERROR_EVAL_REASSOCIATE 6
#define CHECK_FP_ERROR_EVAL_RECIPROCAL 7
#define CHECK_FP_ERROR_EVAL_APPROX_OPTION 0x08
#define CHECK_FP_ERROR_EVAL_REASSOCIATE_OPTION 0x10
#define CHECK_FP_ERROR_EVAL_RECIPROCAL_OPTION 0x20
#define CHECK_FP_ERROR_EVAL_OPTIONS 0x38
#define CHECK_FP_EXCEPTIONS_DEFAULT 3
#define CHECK_FP_EVAL_SOURCE 1
#define CHECK_FP_EVAL_DOUBLE 2
#define CHECK_FP_EVAL_EXTENDED 3
#define CHECK_FP_EVAL_METHOD_MASK 0x03
#define CHECK_FP_EVAL_LAST_SHIFT 2
#define CHECK_FP_EVAL_LAST_MASK 0x0c
#define CHECK_FP_EVAL_SEEN 0x80

typedef struct CheckFpState CheckFpState;
struct CheckFpState
{
    uint8_t values[CHECK_FP_PRAGMA_STATE_COUNT];
    // The last eval_method pragma's output `fp` token, or zero before any pragma in this scope.
    uint32_t eval_pragma_source;
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
    uint32_t error_source;
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
    const uint8_t evaluation = state.values[CHECK_FP_PRAGMA_EVAL_METHOD];
    const uint8_t method = evaluation & CHECK_FP_EVAL_METHOD_MASK;
    const uint8_t last = (evaluation & CHECK_FP_EVAL_LAST_MASK) >> CHECK_FP_EVAL_LAST_SHIFT;
    const uint8_t contract = state.values[CHECK_FP_PRAGMA_FP_CONTRACT];
    return (state.values[CHECK_FP_PRAGMA_FENV_ACCESS] != 0 || (exceptions != 0 && exceptions != CHECK_FP_EXCEPTIONS_DEFAULT) ? BUSTER_CHECK_FP_ENV_ACCESS : 0) |
           (method == CHECK_FP_EVAL_DOUBLE ? BUSTER_CHECK_FP_EVAL_DOUBLE : method == CHECK_FP_EVAL_EXTENDED ? BUSTER_CHECK_FP_EVAL_EXTENDED : 0) |
           (last == CHECK_FP_EVAL_DOUBLE ? BUSTER_CHECK_FP_EVAL_LAST_DOUBLE : last == CHECK_FP_EVAL_EXTENDED ? BUSTER_CHECK_FP_EVAL_LAST_EXTENDED : 0) |
           ((evaluation & CHECK_FP_EVAL_SEEN) != 0 ? BUSTER_CHECK_FP_EVAL_PRAGMA : 0) |
           (contract == BUSTER_PREDEFINES_CONTRACT_ON ? BUSTER_CHECK_FP_CONTRACT_ON : contract == BUSTER_PREDEFINES_CONTRACT_FAST ? BUSTER_CHECK_FP_CONTRACT_FAST : 0);
}

static const char* check_input_fp_error_message(uint8_t error)
{
    const char* result = NULL;
    if ((error & CHECK_FP_ERROR_EVAL_OPTIONS) != 0)
    {
        return (error & CHECK_FP_ERROR_EVAL_APPROX_OPTION) != 0 ? "'#pragma clang fp eval_method' cannot be used with option 'fapprox-func'" :
               (error & CHECK_FP_ERROR_EVAL_REASSOCIATE_OPTION) != 0 ? "'#pragma clang fp eval_method' cannot be used with option 'mreassociate'" :
               "'#pragma clang fp eval_method' cannot be used with option 'freciprocal'";
    }
    switch (error)
    {
        case CHECK_FP_ERROR_FENV_PRECISE: result = "'#pragma STDC FENV_ACCESS ON' is illegal when precise is disabled"; break;
        case CHECK_FP_ERROR_EXCEPT_PRECISE: result = "'#pragma float_control(except, on)' is illegal when precise is disabled"; break;
        case CHECK_FP_ERROR_PRECISE_EXCEPT: result = "'#pragma float_control(precise, off)' is illegal when except is enabled"; break;
        case CHECK_FP_ERROR_PRECISE_FENV: result = "'#pragma float_control(precise, off)' is illegal when fenv_access is enabled"; break;
        case CHECK_FP_ERROR_STACK_SCOPE: result = "'#pragma float_control push/pop' can only appear at file or namespace scope or within a language linkage specification"; break;
        case CHECK_FP_ERROR_EVAL_REASSOCIATE: result = "'#pragma clang fp eval_method' cannot be used with '#pragma clang fp reassociate'"; break;
        case CHECK_FP_ERROR_EVAL_RECIPROCAL: result = "'#pragma clang fp eval_method' cannot be used with '#pragma clang fp reciprocal'"; break;
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
// unknown evaluation method or malformed punctuation.
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
            const bool evaluation = pp_token_is_name(reader->pp, output, at, last, "eval_method");
            const bool on = pp_token_is_name(reader->pp, output, at + 2, last, "on");
            const bool off = pp_token_is_name(reader->pp, output, at + 2, last, "off");
            const bool fast = pp_token_is_name(reader->pp, output, at + 2, last, "fast");
            const bool ignore = pp_token_is_name(reader->pp, output, at + 2, last, "ignore");
            const bool maytrap = pp_token_is_name(reader->pp, output, at + 2, last, "maytrap");
            const bool strict = pp_token_is_name(reader->pp, output, at + 2, last, "strict");
            const bool source_method = pp_token_is_name(reader->pp, output, at + 2, last, "source");
            // Semantic preprocessing classifies the expanded operand as a C keyword.
            const bool double_method = check_input_fp_name(reader, at + 2, "double");
            const bool extended_method = pp_token_is_name(reader->pp, output, at + 2, last, "extended");
            supported = output->kinds[at + 1] == C_TOKEN_L_PAREN && output->kinds[at + 3] == C_TOKEN_R_PAREN &&
                        ((contract && (on || off || fast)) || ((reassociate || reciprocal) && (on || off)) ||
                         (exceptions && (ignore || maytrap || strict)) || (evaluation && (source_method || double_method || extended_method)));
            const uint8_t kind = contract ? CHECK_FP_PRAGMA_FP_CONTRACT : reassociate ? CHECK_FP_PRAGMA_REASSOCIATE :
                                 reciprocal ? CHECK_FP_PRAGMA_RECIPROCAL : evaluation ? CHECK_FP_PRAGMA_EVAL_METHOD : CHECK_FP_PRAGMA_EXCEPTIONS;
            const uint8_t method = source_method ? CHECK_FP_EVAL_SOURCE : double_method ? CHECK_FP_EVAL_DOUBLE : CHECK_FP_EVAL_EXTENDED;
            selected.mask |= (uint8_t)(1u << kind);
            selected.state.values[kind] = evaluation ? CHECK_FP_EVAL_SEEN | method | (method << CHECK_FP_EVAL_LAST_SHIFT) :
                                          exceptions ? (strict ? 2 : (uint8_t)maytrap) : fast ? 2 : (uint8_t)on;
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
    bool evaluations = false;
    for (uint32_t pragma = 0; pragma < parser->tokens.pragma_count; pragma += 1)
    {
        const uint32_t source = parser->tokens.pragma_sources[pragma];
        CheckFpDirective directive = { 0 };
        const bool standard = check_input_fp_name(reader, source + 2, "STDC") &&
                              (check_input_fp_name(reader, source + 3, "FENV_ACCESS") || check_input_fp_name(reader, source + 3, "CX_LIMITED_RANGE") ||
                               check_input_fp_name(reader, source + 3, "FP_CONTRACT"));
        const bool extension = !standard && check_input_fp_extension(reader, &macros, source, &directive);
        count += standard || extension;
        evaluations |= extension && (directive.mask & (1u << CHECK_FP_PRAGMA_EVAL_METHOD)) != 0;
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
    const uint32_t eval_uses = evaluations ? reader->pp->eval_method_use_count : 0;
    CheckInputFpEvalUse* uses = eval_uses != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputFpEvalUse, eval_uses) : NULL;
    const bool valid = !input->resource && (count == 0 || (scopes != 0 && stack != NULL && records != 0 && environments != 0 && complex_ranges != 0 && nodes != 0)) &&
                       (eval_uses == 0 || uses != NULL);
    reader->output->resource |= !valid;
    if (valid && count != 0)
    {
        input->fp_pragmas = records;
        input->fp_environments = environments;
        input->cx_limited_ranges = complex_ranges;
        input->graph.fp_environments = nodes;
        reader->fp_eval_uses = uses;
        memset(input->graph.fp_environments, 0, input->graph.capacity);
        uint32_t depth = 0;
        uint32_t pragma = 0;
        uint32_t eval_use = 0;
        uint32_t pushed = 0;
        bool fast, finite, no_errno;
        const uint8_t relaxation = predefines_floating_point(&reader->pp->options.predefines, &fast, &finite, &no_errno);
        const uint8_t option_error = (relaxation & BUSTER_PREDEFINES_FP_APPROX_FUNC ? CHECK_FP_ERROR_EVAL_APPROX_OPTION : 0) |
                                     (relaxation & BUSTER_PREDEFINES_FP_REASSOCIATE ? CHECK_FP_ERROR_EVAL_REASSOCIATE_OPTION : 0) |
                                     (relaxation & BUSTER_PREDEFINES_FP_RECIPROCAL ? CHECK_FP_ERROR_EVAL_RECIPROCAL_OPTION : 0);
        const uint8_t default_contract = predefines_fp_contract(&reader->pp->options.predefines);
        CheckFpState state = { .values = { [CHECK_FP_PRAGMA_PRECISE] = 1, [CHECK_FP_PRAGMA_FP_CONTRACT] = default_contract,
                                          [CHECK_FP_PRAGMA_EXCEPTIONS] = CHECK_FP_EXCEPTIONS_DEFAULT } };
        for (uint32_t token = 0; token <= parser->tokens.count; token += 1)
        {
            while (pragma < parser->tokens.pragma_count && parser->tokens.pragma_positions[pragma] == token)
            {
                const uint32_t source = parser->tokens.pragma_sources[pragma];
                check_input_fp_eval_uses(reader, &eval_use, source, pragma, token, state.eval_pragma_source);
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
                    const bool restore = directive && check_input_fp_name(reader, source + 4, "DEFAULT");
                    const bool off = directive && (check_input_fp_name(reader, source + 4, "OFF") || restore);
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
                        state.values[contract ? CHECK_FP_PRAGMA_FP_CONTRACT : CHECK_FP_PRAGMA_FENV_ACCESS] = contract && restore ? default_contract : (uint8_t)on;
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
                        // Clang checks the previous directive's state. Options combined with the first eval_method
                        // are accepted, but a later separate reassociate/reciprocal(on) conflicts even after source.
                        const bool evaluation = (state.values[CHECK_FP_PRAGMA_EVAL_METHOD] & CHECK_FP_EVAL_SEEN) != 0;
                        const uint8_t error = (directive.push || directive.pop) && depth != 0 ? CHECK_FP_ERROR_STACK_SCOPE :
                                              disable_precise && strict ? CHECK_FP_ERROR_PRECISE_EXCEPT :
                                              disable_precise && fenv ? CHECK_FP_ERROR_PRECISE_FENV :
                                              float_control && directive.kind == CHECK_FP_PRAGMA_EXCEPTIONS && directive.state.values[CHECK_FP_PRAGMA_EXCEPTIONS] != 0 && !precise ?
                                                  CHECK_FP_ERROR_EXCEPT_PRECISE :
                                              evaluation && directive.state.values[CHECK_FP_PRAGMA_REASSOCIATE] != 0 ? CHECK_FP_ERROR_EVAL_REASSOCIATE :
                                              evaluation && directive.state.values[CHECK_FP_PRAGMA_RECIPROCAL] != 0 ? CHECK_FP_ERROR_EVAL_RECIPROCAL :
                                              (directive.mask & (1u << CHECK_FP_PRAGMA_EVAL_METHOD)) != 0 ? option_error : 0;
                        const bool accepted = error == 0;
                        const bool empty = accepted && directive.pop && pushed == 0;
                        // A conflict with a global option still records the new evaluation scope in Clang, including
                        // its ban on expanding __FLT_EVAL_METHOD__. Other invalid state transitions leave it alone.
                        if (accepted || (error & CHECK_FP_ERROR_EVAL_OPTIONS) != 0)
                        {
                            if (directive.push)
                            {
                                stack[pushed++] = state;
                            }
                            if (directive.pop && !empty)
                            {
                                const uint8_t last = state.values[CHECK_FP_PRAGMA_EVAL_METHOD] & (CHECK_FP_EVAL_LAST_MASK | CHECK_FP_EVAL_SEEN);
                                const uint32_t eval_pragma_source = state.eval_pragma_source;
                                state = stack[--pushed];
                                // Clang restores arithmetic precision here, but its preprocessor retains the last
                                // evaluation method and pragma location until the enclosing compound ends.
                                state.values[CHECK_FP_PRAGMA_EVAL_METHOD] = (state.values[CHECK_FP_PRAGMA_EVAL_METHOD] & CHECK_FP_EVAL_METHOD_MASK) | last;
                                state.eval_pragma_source = eval_pragma_source;
                            }
                            for (uint32_t kind = 0; kind < CHECK_FP_PRAGMA_STATE_COUNT; kind += 1)
                            {
                                if ((directive.mask & (1u << kind)) != 0)
                                {
                                    state.values[kind] = directive.state.values[kind];
                                }
                            }
                            if ((directive.mask & (1u << CHECK_FP_PRAGMA_EVAL_METHOD)) != 0)
                            {
                                state.eval_pragma_source = source + 3;
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
                            .environment = directive.kind == CHECK_FP_PRAGMA_EVAL_METHOD ? state.values[directive.kind] & CHECK_FP_EVAL_METHOD_MASK :
                                           directive.kind < CHECK_FP_PRAGMA_STATE_COUNT ? state.values[directive.kind] : check_input_fp_environment(state),
                            .warnings = empty ? CHECK_FP_PRAGMA_EMPTY_STACK : 0,
                            .error = error,
                            .valid = accepted,
                        };
                        if (error == CHECK_FP_ERROR_EVAL_REASSOCIATE || error == CHECK_FP_ERROR_EVAL_RECIPROCAL || (error & CHECK_FP_ERROR_EVAL_OPTIONS) != 0)
                        {
                            // Clang locates these conflicts at the retained 'fp' token, including combined options.
                            input->fp_pragmas[input->fp_pragma_count - 1].error_source = source + 3;
                        }
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
            check_input_fp_eval_uses(reader, &eval_use, token < parser->tokens.count ? parser->tokens.sources[token] : reader->pp->output.count, pragma, token,
                                     state.eval_pragma_source);
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

static void check_input_fp_mark_type(CheckInputReader* reader, uint8_t** marked, uint32_t declaration)
{
    const uint32_t count = reader->parser->tokens.count;
    if (*marked == NULL)
    {
        *marked = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
        reader->output->resource |= *marked == NULL;
        if (*marked != NULL)
        {
            memset(*marked, 0, count);
        }
    }
    if (*marked != NULL)
    {
        (*marked)[declaration] |= 1;
    }
}

// Clang checks available_only_in_default_eval_method only when the command line explicitly chooses a method.
// Resolve its typedef uses after the declaration and expression walks, including casts and sizeof/typeof operands
// that have no runtime node. Cached constant replays then cannot duplicate an error. Only a unit with both an
// explicit method and retained FP state scans the sparse attribute groups, and only a marked typedef needs scratch.
static void check_input_fp_types(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const uint8_t requested = reader->pp->options.predefines.fp_eval_method;
    const bool active = !reader->lane && requested != BUSTER_PREDEFINES_EVAL_DEFAULT && input->fp_environments != NULL;
    const uint64_t temporary = reader->scratch->length;
    const uint32_t count = reader->parser->tokens.count;
    const bool scalar = reader->parser->options.scalar;
    uint8_t* marked = NULL;
    const bool attributed = active && check_sparse_wide_present(input->attributes);
    for (uint32_t first = attributed ? check_input_attributed_next(input->attributes, 0, count, scalar) : count;
         first < count && !input->resource; first = check_input_attributed_next(input->attributes, first + 16, count, scalar))
    {
        const uint32_t lanes = check_input_attributed_group(input->attributes, first, count, scalar);
        for (uint32_t selected = lanes; selected != 0 && !input->resource; selected &= selected - 1)
        {
            const uint32_t declaration = first + (uint32_t)__builtin_ctz(selected);
            const CheckInputAttributes* attributes = check_input_attributes_at(input, declaration);
            bool restricted = false;
            for (uint32_t i = 0; reader->declaration_kinds[declaration] == C_BINDING_TYPEDEF && attributes != NULL &&
                                 i < attributes->count && !restricted; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                restricted = !attribute->ignored && !attribute->subject_ignored &&
                             check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "available_only_in_default_eval_method");
            }
            if (restricted)
            {
                check_input_fp_mark_type(reader, &marked, declaration);
            }
        }
    }
    // Clang also attaches the attribute implicitly to translation-unit float_t/double_t typedef declarations.
    // The binding events retain file scope, without reconstructing it from punctuation or adding a token column.
    const IdentId float_name = active ? intern_find(reader->pp->table, (const uint8_t*)"float_t", 7) : 0;
    const IdentId double_name = active ? intern_find(reader->pp->table, (const uint8_t*)"double_t", 8) : 0;
    const CBindings* bindings = &reader->bindings;
    for (uint32_t event = 0; !input->resource && (float_name != 0 || double_name != 0) && event < bindings->count; event += 1)
    {
        const uint32_t name = bindings->key[event];
        const uint32_t declaration = bindings->declaration[event];
        if (bindings->operation[event] == C_BINDING_DECLARE && bindings->kind[event] == C_BINDING_TYPEDEF && bindings->depth[event] == 0 &&
            name < bindings->name_capacity && declaration < count && (name == float_name || name == double_name))
        {
            check_input_fp_mark_type(reader, &marked, declaration);
        }
    }
    const uint8_t baseline = requested == BUSTER_PREDEFINES_EVAL_DOUBLE ? BUSTER_CHECK_FP_EVAL_LAST_DOUBLE :
                             requested == BUSTER_PREDEFINES_EVAL_EXTENDED ? BUSTER_CHECK_FP_EVAL_LAST_EXTENDED : 0;
    for (uint32_t token = 0; marked != NULL && !input->resource && token < count; token += 1)
    {
        const uint32_t declaration = input->declarations[token];
        const uint8_t environment = input->fp_environments[token];
        if (check_input_typedef_declared(reader, token))
        {
            const uint32_t key = reader->objects[token] - 1;
            // The written mark is bit 0, the canonical entity's mark so far bit 1, and this declaration's effective
            // mark bit 2. Propagate in source order so a later attributed redeclaration cannot affect earlier uses.
            if (key < count && ((marked[token] & 1) != 0 || (marked[key] & 2) != 0))
            {
                marked[key] |= 2;
                marked[token] |= 4;
            }
        }
        if (declaration < count && declaration != token && (marked[declaration] & 4) != 0 &&
            (input->deferred_tokens == NULL || input->deferred_tokens[token] == 0) &&
            (environment & BUSTER_CHECK_FP_EVAL_PRAGMA) != 0 && (environment & BUSTER_CHECK_FP_EVAL_LAST_MASK) != baseline)
        {
            const uint32_t before = input->report_count;
            check_input_report(reader, declaration, CHECK_INPUT_FP_EVAL_TYPE);
            if (input->report_count != before)
            {
                CheckInputReport* report = input->reports + before;
                report->completion = token + 1;
                report->sort = token;
            }
        }
    }
    arena_pop_to(reader->scratch, temporary);
}
