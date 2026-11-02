// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Retain source-ordered floating environment and complex range settings. A setting at the beginning of a
// compound statement ends with that compound; file settings persist. The optional graph column carries runtime
// floating policy. Complex range is a permission to relax evaluation, so retaining full-range actions is valid.
// FP_CONTRACT is one too, to contract a * b + c into one fused operation, which nothing here does: its records keep
// its state (environment, 1 for ON and 2 for clang's fast) and change no column. float_control(precise, on) retains
// the already precise evaluation policy. Other float_control options need semantics this reader does not supply.

#define CHECK_FP_PRAGMA_FENV_ACCESS 0
#define CHECK_FP_PRAGMA_CX_LIMITED_RANGE 1
#define CHECK_FP_PRAGMA_FP_CONTRACT 2
#define CHECK_FP_PRAGMA_PRECISE 3
#define CHECK_FP_PRAGMA_UNKNOWN_STATE 0x01
#define CHECK_FP_PRAGMA_EXTRA_TOKENS 0x02
#define CHECK_FP_PRAGMA_INVALID_PLACEMENT 0x04

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
    bool valid;
};

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

// Only complete supported extension forms acquire a handled mark. In particular, a recognized prefix must not
// swallow another clang fp clause, a float_control stack operation, a macro argument or malformed punctuation.
// Unsupported forms remain with the semantic-pragma diagnostic, which retains their original spelling.
// clang enables fenv_access only under -fms-extensions or a strict floating model, and silently ignores a mid-block
// spelling. Buster refuses that mode, so #1135 recognizes these spellings unconditionally; a spelling at a placement
// this reader cannot apply stays unhandled, and the semantic-pragma diagnostic refuses it.
static bool check_input_fp_extension(CheckInputReader* reader, uint64_t** macros, uint32_t source, uint32_t* end, uint8_t* kind, uint8_t* state)
{
    const PPTokens* output = &reader->pp->output;
    uint32_t last = source;
    const bool candidate = source < output->count && output->count - source >= 6 &&
                           (check_input_fp_name(reader, source + 2, "fenv_access") || check_input_fp_name(reader, source + 2, "float_control") ||
                            (check_input_fp_name(reader, source + 2, "clang") && check_input_fp_name(reader, source + 3, "fp")));
    const bool directive = candidate && parse_pragma_at(output, source, &last);
    const uint32_t length = directive ? last - source : 0;
    bool supported = false;
    uint8_t selected = CHECK_FP_PRAGMA_FENV_ACCESS;
    uint8_t value = 0;
    if (length == 6 && pp_token_is_name(reader->pp, output, source + 2, last, "fenv_access") &&
        output->kinds[source + 3] == C_TOKEN_L_PAREN && output->kinds[source + 5] == C_TOKEN_R_PAREN)
    {
        const bool on = pp_token_is_name(reader->pp, output, source + 4, last, "on");
        const bool off = pp_token_is_name(reader->pp, output, source + 4, last, "off");
        supported = on || off;
        value = on;
    }
    else if (length == 8 && pp_token_is_name(reader->pp, output, source + 2, last, "clang") &&
             pp_token_is_name(reader->pp, output, source + 3, last, "fp") &&
             pp_token_is_name(reader->pp, output, source + 4, last, "contract") &&
             output->kinds[source + 5] == C_TOKEN_L_PAREN && output->kinds[source + 7] == C_TOKEN_R_PAREN)
    {
        const bool on = pp_token_is_name(reader->pp, output, source + 6, last, "on");
        const bool off = pp_token_is_name(reader->pp, output, source + 6, last, "off");
        const bool fast = pp_token_is_name(reader->pp, output, source + 6, last, "fast");
        supported = on || off || fast;
        selected = CHECK_FP_PRAGMA_FP_CONTRACT;
        value = fast ? 2 : (uint8_t)on;
    }
    else if (length == 8 && pp_token_is_name(reader->pp, output, source + 2, last, "float_control") &&
             output->kinds[source + 3] == C_TOKEN_L_PAREN && output->kinds[source + 5] == C_TOKEN_COMMA &&
             output->kinds[source + 7] == C_TOKEN_R_PAREN &&
             pp_token_is_name(reader->pp, output, source + 4, last, "precise") &&
             pp_token_is_name(reader->pp, output, source + 6, last, "on"))
    {
        supported = true;
        selected = CHECK_FP_PRAGMA_PRECISE;
        value = 1;
    }
    // These handlers read macro-expanded arguments, but the retained stream is unexpanded. Even a literal-looking
    // word such as 'on' can have been a macro. Use the existing conservative unit-history guard; pragma namespace
    // and handler names themselves are not expanded. The count pass prepares this scratch before publishing state.
    const uint32_t first = source + (selected == CHECK_FP_PRAGMA_FP_CONTRACT ? 4 : 3);
    for (uint32_t token = first; supported && token < last; token += 1)
    {
        const IdentId name = check_input_pragma_name(reader, token);
        supported = name == 0 || !check_input_pragma_macro(reader, macros, name);
    }
    *end = last;
    *kind = selected;
    *state = value;
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
        uint32_t end = source;
        uint8_t kind = 0;
        uint8_t state = 0;
        const bool standard = check_input_fp_name(reader, source + 2, "STDC") &&
                              (check_input_fp_name(reader, source + 3, "FENV_ACCESS") || check_input_fp_name(reader, source + 3, "CX_LIMITED_RANGE") ||
                               check_input_fp_name(reader, source + 3, "FP_CONTRACT"));
        count += standard || check_input_fp_extension(reader, &macros, source, &end, &kind, &state);
    }
    typedef struct CheckFpScope CheckFpScope;
    struct CheckFpScope
    {
        uint32_t opening;
        uint32_t closing;
        uint8_t environment;
        uint8_t complex_range;
        bool compound;
    };
    CheckFpScope* scopes = count != 0 ? arena_push_array(reader->scratch, (uint64_t)parser->tokens.count + 1, sizeof(CheckFpScope), _Alignof(CheckFpScope)) : 0;
    CheckInputFpPragma* records = count != 0 ? arena_push_array(reader->arena, count, sizeof(CheckInputFpPragma), _Alignof(CheckInputFpPragma)) : 0;
    uint8_t* environments = count != 0 ? arena_push_array(reader->arena, (uint64_t)parser->tokens.count + 1, sizeof(uint8_t), 1) : 0;
    uint8_t* complex_ranges = count != 0 ? arena_push_array(reader->arena, (uint64_t)parser->tokens.count + 1, sizeof(uint8_t), 1) : 0;
    uint8_t* nodes = count != 0 ? arena_push_array(reader->arena, input->graph.capacity, sizeof(uint8_t), 1) : 0;
    const bool valid = !input->resource && (count == 0 || (scopes != 0 && records != 0 && environments != 0 && complex_ranges != 0 && nodes != 0));
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
        uint8_t environment = 0;
        uint8_t complex_range = 0;
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
                    const bool accepted = directive && (!recognized || (end == source + 5 && placement));
                    if (accepted && recognized && complex)
                    {
                        complex_range = on;
                    }
                    else if (accepted && recognized && !contract)
                    {
                        environment = on ? BUSTER_CHECK_FP_ENV_ACCESS : 0;
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
                        .environment = complex ? complex_range : contract ? (uint8_t)on : environment,
                        .warnings = !recognized ? CHECK_FP_PRAGMA_UNKNOWN_STATE : end != source + 5 ? CHECK_FP_PRAGMA_EXTRA_TOKENS : 0,
                        .valid = accepted,
                    };
                    handled[pragma] = accepted ? 2 : 3;
                }
                else
                {
                    uint32_t end = source;
                    uint8_t kind = 0;
                    uint8_t state = 0;
                    if (placement && check_input_fp_extension(reader, &macros, source, &end, &kind, &state))
                    {
                        if (kind == CHECK_FP_PRAGMA_FENV_ACCESS)
                        {
                            environment = state != 0 ? BUSTER_CHECK_FP_ENV_ACCESS : 0;
                        }
                        input->fp_pragmas[input->fp_pragma_count++] = (CheckInputFpPragma)
                        {
                            .pragma = pragma,
                            .source_begin = source,
                            .source_end = end + 1,
                            .token = token,
                            .scope = depth != 0 ? scopes[depth - 1].opening : BUSTER_CHECK_NONE,
                            .scope_end = depth != 0 ? scopes[depth - 1].closing + 1 : parser->tokens.count,
                            .kind = kind,
                            .environment = kind == CHECK_FP_PRAGMA_FENV_ACCESS ? environment : state,
                            .valid = true,
                        };
                        handled[pragma] = 2;
                    }
                }
                pragma += 1;
            }
            input->fp_environments[token] = environment;
            input->cx_limited_ranges[token] = complex_range;
            if (token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_L_BRACE)
            {
                const uint8_t role = parser->tree.roles[token];
                scopes[depth++] = (CheckFpScope)
                {
                    .opening = token,
                    .closing = parser->tree.parents[token],
                    .environment = environment,
                    .complex_range = complex_range,
                    .compound = role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_BLOCK,
                };
            }
            else if (depth != 0 && token == scopes[depth - 1].closing)
            {
                depth -= 1;
                environment = scopes[depth].environment;
                complex_range = scopes[depth].complex_range;
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
