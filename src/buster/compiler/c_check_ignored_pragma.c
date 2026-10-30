// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Classify only retained directives, without changing their -E representation. Existing semantic readers keep
// ownership even of malformed spellings. Unknown/tooling pragmas carry lexical warning facts, not checker errors;
// known unsupported changes to layout, symbols, targets or execution remain hard refusals. This small cold table
// is shared by the scalar/vector readers and the original-source diagnostic recovery path.

typedef enum CheckInputPragmaDisposition
{
    CHECK_PRAGMA_OWNED,
    CHECK_PRAGMA_SEMANTIC,
    CHECK_PRAGMA_QUIET,
    CHECK_PRAGMA_UNKNOWN,
    CHECK_PRAGMA_STDC_UNKNOWN,
    CHECK_PRAGMA_IGNORED,
    CHECK_PRAGMA_OPENMP,
} CheckInputPragmaDisposition;

static CheckInputPragmaDisposition check_input_pragma_disposition(const CheckInputReader* reader, uint32_t pragma)
{
    const ParseTokens* tokens = &reader->parser->tokens;
    const PPTokens* output = &reader->pp->output;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    uint32_t source = 0;
    uint32_t end = 0;
    uint32_t token = 0;
    const bool valid = pragma < tokens->pragma_count &&
        check_input_retained_pragma(reader, pragma, &previous_end, &previous_token, &source, &end, &token);
    CheckInputPragmaDisposition result = CHECK_PRAGMA_OWNED;
    if (valid)
    {
        result = CHECK_PRAGMA_UNKNOWN;
        const bool gcc = pp_token_is_name(reader->pp, output, source + 2, end, "GCC");
        const bool clang = pp_token_is_name(reader->pp, output, source + 2, end, "clang");
        const bool stdc = pp_token_is_name(reader->pp, output, source + 2, end, "STDC");
        const uint32_t name = source + 2 + (gcc || clang || stdc);
        // Bit 0 selects an unqualified directive; the other bits select the actual vendor, not an arbitrary
        // matching suffix. For example, an unknown vendor's 'pack' is not our supported #pragma pack.
        const uint8_t dialect = gcc ? 2 : clang ? 4 : stdc ? 8 : 1;
        static const struct
        {
            const char* name;
            uint8_t dialects;
            uint8_t disposition;
        } rules[] = {
            { "pack", 1, CHECK_PRAGMA_OWNED },
            { "unroll", 1 | 2, CHECK_PRAGMA_OWNED },
            { "nounroll", 1, CHECK_PRAGMA_OWNED },
            { "unroll_and_jam", 1, CHECK_PRAGMA_OWNED },
            { "nounroll_and_jam", 1, CHECK_PRAGMA_OWNED },
            { "diagnostic", 2 | 4, CHECK_PRAGMA_OWNED },
            { "visibility", 2, CHECK_PRAGMA_OWNED },
            { "assume_nonnull", 4, CHECK_PRAGMA_OWNED },
            { "loop", 4, CHECK_PRAGMA_OWNED },
            { "section", 4, CHECK_PRAGMA_OWNED },
            { "optimize", 4, CHECK_PRAGMA_OWNED },
            { "FENV_ACCESS", 8, CHECK_PRAGMA_OWNED },
            { "CX_LIMITED_RANGE", 8, CHECK_PRAGMA_OWNED },
            { "FP_CONTRACT", 8, CHECK_PRAGMA_OWNED },
            { "message", 1, CHECK_PRAGMA_OWNED },
            { "warning", 2, CHECK_PRAGMA_OWNED },
            { "error", 2, CHECK_PRAGMA_OWNED },
            { "push_macro", 1, CHECK_PRAGMA_OWNED },
            { "pop_macro", 1, CHECK_PRAGMA_OWNED },
            { "redefine_extname", 1, CHECK_PRAGMA_SEMANTIC },
            { "scalar_storage_order", 1 | 2, CHECK_PRAGMA_SEMANTIC },
            { "target", 2, CHECK_PRAGMA_SEMANTIC },
            { "weak", 1, CHECK_PRAGMA_SEMANTIC },
            { "ms_struct", 1, CHECK_PRAGMA_SEMANTIC },
            { "section", 1, CHECK_PRAGMA_SEMANTIC },
            { "code_seg", 1, CHECK_PRAGMA_SEMANTIC },
            { "data_seg", 1, CHECK_PRAGMA_SEMANTIC },
            { "bss_seg", 1, CHECK_PRAGMA_SEMANTIC },
            { "const_seg", 1, CHECK_PRAGMA_SEMANTIC },
            { "init_seg", 1, CHECK_PRAGMA_SEMANTIC },
            { "float_control", 1, CHECK_PRAGMA_SEMANTIC },
            { "fenv_access", 1, CHECK_PRAGMA_SEMANTIC },
            { "fp", 4, CHECK_PRAGMA_SEMANTIC },
            { "attribute", 4, CHECK_PRAGMA_SEMANTIC },
            { "FENV_ROUND", 8, CHECK_PRAGMA_SEMANTIC },
            { "acc", 1, CHECK_PRAGMA_SEMANTIC },
        };
        if (stdc)
        {
            result = CHECK_PRAGMA_STDC_UNKNOWN;
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rules); i += 1)
        {
            if ((rules[i].dialects & dialect) != 0 && pp_token_is_name(reader->pp, output, name, end, rules[i].name))
            {
                result = (CheckInputPragmaDisposition)rules[i].disposition;
                break;
            }
        }
        if (dialect == 1 && pp_token_is_name(reader->pp, output, name, end, "omp"))
        {
            // Driver state, never the user-modifiable _OPENMP macro. Even a requested alternate runtime must
            // not silently turn an unsupported parallel program into a serial executable.
            result = reader->pp->options.predefines.openmp_enabled ? CHECK_PRAGMA_SEMANTIC : CHECK_PRAGMA_OPENMP;
        }
        else if (dialect == 1 && (pp_token_is_name(reader->pp, output, name, end, "align") ||
                                 pp_token_is_name(reader->pp, output, name, end, "options")))
        {
            const bool options = pp_token_is_name(reader->pp, output, name, end, "options");
            const uint32_t equal = name + 1 + options;
            // '#pragma align 4' is an ignored malformed hint, not '#pragma align=packed', which changes layout.
            result = (!options || pp_token_is_name(reader->pp, output, name + 1, end, "align")) &&
                     equal < end && output->kinds[equal] == C_TOKEN_EQUAL ? CHECK_PRAGMA_SEMANTIC : CHECK_PRAGMA_IGNORED;
        }
        else if (dialect == 1 && pp_token_is_name(reader->pp, output, name, end, "comment"))
        {
            // Linux/ELF tooling directives. No claim to implement COFF linker directives or automatic libraries.
            const bool shape = end == name + 6 && output->kinds[name + 1] == C_TOKEN_L_PAREN &&
                               output->kinds[name + 3] == C_TOKEN_COMMA && output->kinds[name + 4] == C_TOKEN_STRING_LITERAL &&
                               output->kinds[name + 5] == C_TOKEN_R_PAREN;
            const bool lib = shape && pp_token_is_name(reader->pp, output, name + 2, end, "lib");
            const bool ignored = shape && (pp_token_is_name(reader->pp, output, name + 2, end, "user") ||
                                          pp_token_is_name(reader->pp, output, name + 2, end, "compiler") ||
                                          pp_token_is_name(reader->pp, output, name + 2, end, "exestr"));
            result = lib ? CHECK_PRAGMA_QUIET : ignored ? CHECK_PRAGMA_IGNORED : CHECK_PRAGMA_SEMANTIC;
        }
    }
    return result;
}

// Sparse immutable warning facts keep exact PP coordinates, even when several pragmas share one parser token
// or precede EOF. A failed reservation publishes nothing. Lexical facts survive later syntax recovery; unlike
// semantic placement checks, they do not require a valid C prefix or stop at its first parse diagnostic.
static bool check_input_ignored_pragmas(CheckInputReader* reader, uint8_t* handled)
{
    const ParseTokens* tokens = &reader->parser->tokens;
    const uint64_t mark = reader->arena->length;
    const uint64_t temporary = reader->scratch->length;
    uint8_t* dispositions = reader->arena != reader->scratch && tokens->pragma_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, tokens->pragma_count) : NULL;
    bool valid = reader->arena != reader->scratch && (tokens->pragma_count == 0 || dispositions != NULL);
    uint32_t count = 0;
    for (uint32_t pragma = 0; valid && pragma < tokens->pragma_count; pragma += 1)
    {
        const CheckInputPragmaDisposition disposition = handled != NULL && handled[pragma] != 0 ? CHECK_PRAGMA_OWNED :
                                                        check_input_pragma_disposition(reader, pragma);
        dispositions[pragma] = (uint8_t)disposition;
        count += disposition >= CHECK_PRAGMA_UNKNOWN;
    }
    CheckWarningEvent* events = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckWarningEvent, count) : NULL;
    valid &= count == 0 || events != NULL;
    if (valid)
    {
        uint32_t next = 0;
        for (uint32_t pragma = 0; pragma < tokens->pragma_count; pragma += 1)
        {
            const CheckInputPragmaDisposition disposition = (CheckInputPragmaDisposition)dispositions[pragma];
            if (handled != NULL && disposition >= CHECK_PRAGMA_QUIET)
            {
                handled[pragma] = 1;
            }
            if (disposition >= CHECK_PRAGMA_UNKNOWN)
            {
                const uint32_t source = tokens->pragma_sources[pragma];
                const bool vendor = pp_token_is_name(reader->pp, &reader->pp->output, source + 2, reader->pp->output.count, "GCC") ||
                                    pp_token_is_name(reader->pp, &reader->pp->output, source + 2, reader->pp->output.count, "clang") ||
                                    pp_token_is_name(reader->pp, &reader->pp->output, source + 2, reader->pp->output.count, "STDC");
                uint32_t end = source;
                parse_pragma_at(&reader->pp->output, source, &end);
                const uint32_t located = end == source + 2 ? source + 1 : source + 2 + (vendor && source + 3 < end);
                events[next++] = (CheckWarningEvent)
                {
                    .id = disposition == CHECK_PRAGMA_OPENMP ? CHECK_WARNING_ID_SOURCE_OPENMP :
                          disposition == CHECK_PRAGMA_IGNORED ? CHECK_WARNING_ID_IGNORED_PRAGMA : CHECK_WARNING_ID_UNRECOGNIZED_PRAGMA,
                    .kind = CHECK_WARNING_EVENT_PRAGMA,
                    .token = tokens->pragma_positions[pragma],
                    .source = located,
                    .flags = disposition == CHECK_PRAGMA_UNKNOWN || disposition == CHECK_PRAGMA_OPENMP ? BUSTER_CHECK_WARNING_DEFAULT_IGNORED : 0,
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = tokens->pragma_positions[pragma],
                    .payload = { .token = source },
                };
            }
        }
        reader->output->pragma_warnings = (CheckWarningEvents){ .events = events, .count = count };
    }
    else
    {
        arena_pop_to(reader->arena, mark);
        reader->output->resource = true;
    }
    arena_pop_to(reader->scratch, temporary);
    return valid;
}
