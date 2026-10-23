// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Warning policy retained from the preprocessor's canonical Clang/GCC diagnostic pragmas. The preprocessor owns
// syntax, warning-group recognition and their diagnostics: it emits only applied directives, joins ordinary
// strings, removes trailing push/pop tokens, and omits unmatched pops and other warning-only no-ops. These records
// describe requested warning policy; they never suppress a C constraint error or change a semantic action.

#define BUSTER_CHECK_PRAGMA_INPUT_IMPLEMENTATION 1

typedef enum CheckDiagnosticDialect
{
    CHECK_DIAGNOSTIC_CLANG,
    CHECK_DIAGNOSTIC_GCC,
} CheckDiagnosticDialect;

typedef enum CheckDiagnosticOperation
{
    CHECK_DIAGNOSTIC_UNKNOWN,
    CHECK_DIAGNOSTIC_PUSH,
    CHECK_DIAGNOSTIC_POP,
    CHECK_DIAGNOSTIC_IGNORED,
    CHECK_DIAGNOSTIC_WARNING,
    CHECK_DIAGNOSTIC_ERROR,
    CHECK_DIAGNOSTIC_FATAL,
} CheckDiagnosticOperation;

typedef struct CheckInputDiagnosticPragma CheckInputDiagnosticPragma;
struct CheckInputDiagnosticPragma
{
    String8 option;
    uint32_t pragma;
    uint32_t source_begin;
    uint32_t source_end;
    uint32_t token;
    uint32_t state_before;
    uint32_t state_after;
    uint32_t matching_push;
    uint8_t dialect;
    uint8_t operation;
};

typedef struct CheckInputDiagnosticPragmas CheckInputDiagnosticPragmas;
struct CheckInputDiagnosticPragmas
{
    CheckInputDiagnosticPragma* records;
    uint32_t count;
    uint32_t final_state;
    uint32_t maximum_depth;
    uint32_t unmatched_pushes;
    uint32_t unmatched_pops;
};

typedef struct CheckDiagnosticPush CheckDiagnosticPush;
struct CheckDiagnosticPush
{
    uint32_t state;
    uint32_t record;
};

static bool check_input_retained_pragma(const CheckInputReader* reader, uint32_t pragma, uint32_t* previous_end,
                                         uint32_t* previous_token, uint32_t* source, uint32_t* end, uint32_t* token)
{
    const ParseTokens* tokens = &reader->parser->tokens;
    const PPTokens* output = &reader->pp->output;
    *source = tokens->pragma_sources[pragma];
    *token = tokens->pragma_positions[pragma];
    *end = *source;
    bool valid = *source >= *previous_end && *source < output->count && *token >= *previous_token && *token <= tokens->count &&
                 output->kinds[*source] == C_TOKEN_HASH && (output->flags[*source] & BUSTER_PP_START_OF_LINE) != 0;
    valid = valid && parse_pragma_at(output, *source, end);
    if (valid)
    {
        *previous_end = *end + 1;
        *previous_token = *token;
    }
    valid = valid && pp_token_is_name(reader->pp, output, *source + 1, *end, "pragma");
    return valid;
}

// The retained source range includes '#', 'pragma' and PRAGMA_END. All coordinates remain in the original PP and
// parser streams, including a directive at parser token_count after the last declaration. Rejecting a shape here
// leaves it unhandled for the caller's unsupported-pragma diagnostic; it is never an arbitrary pragma bypass.
static bool check_input_diagnostic_record(const CheckInputReader* reader, uint32_t pragma, uint32_t* previous_end,
                                          uint32_t* previous_token, CheckInputDiagnosticPragma* record)
{
    const PPTokens* output = &reader->pp->output;
    uint32_t source = 0;
    uint32_t end = 0;
    uint32_t token = 0;
    bool valid = check_input_retained_pragma(reader, pragma, previous_end, previous_token, &source, &end, &token);
    valid = valid && end - source >= 5 &&
            pp_token_is_name(reader->pp, output, source + 3, end, "diagnostic");
    const bool clang = valid && pp_token_is_name(reader->pp, output, source + 2, end, "clang");
    const bool gcc = valid && pp_token_is_name(reader->pp, output, source + 2, end, "GCC");
    valid &= clang || gcc;
    CheckDiagnosticOperation operation = CHECK_DIAGNOSTIC_UNKNOWN;
    static const char* const names[] = { "push", "pop", "ignored", "warning", "error", "fatal" };
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        if (pp_token_is_name(reader->pp, output, source + 4, end, names[i]))
        {
            operation = (CheckDiagnosticOperation)(CHECK_DIAGNOSTIC_PUSH + i);
        }
    }
    valid &= operation != CHECK_DIAGNOSTIC_UNKNOWN;
    if (valid)
    {
        const bool severity = operation >= CHECK_DIAGNOSTIC_IGNORED;
        valid = end - source == (severity ? 6 : 5) && (!severity || output->kinds[source + 5] == C_TOKEN_STRING_LITERAL);
    }
    if (valid)
    {
        *record = (CheckInputDiagnosticPragma)
        {
            .pragma = pragma,
            .source_begin = source,
            .source_end = end + 1,
            .token = token,
            .state_before = BUSTER_CHECK_NONE,
            .state_after = BUSTER_CHECK_NONE,
            .matching_push = BUSTER_CHECK_NONE,
            .dialect = clang ? CHECK_DIAGNOSTIC_CLANG : CHECK_DIAGNOSTIC_GCC,
            .operation = (uint8_t)operation,
        };
    }
    return valid;
}

typedef struct CheckInputNonnullPragma CheckInputNonnullPragma;
struct CheckInputNonnullPragma
{
    uint32_t pragma;
    uint32_t source_begin;
    uint32_t source_end;
    uint32_t token;
    uint32_t active_before;
    uint32_t active_after;
    uint32_t matching_begin;
    bool begin;
    bool invalid;
};

typedef struct CheckInputNonnullPragmas CheckInputNonnullPragmas;
struct CheckInputNonnullPragmas
{
    CheckInputNonnullPragma* records;
    uint32_t count;
    uint32_t active;
    uint32_t error_count;
};

static bool check_input_nonnull_record(const CheckInputReader* reader, uint32_t pragma, uint32_t* previous_end,
                                       uint32_t* previous_token, CheckInputNonnullPragma* record)
{
    const PPTokens* output = &reader->pp->output;
    uint32_t source = 0;
    uint32_t end = 0;
    uint32_t token = 0;
    bool valid = check_input_retained_pragma(reader, pragma, previous_end, previous_token, &source, &end, &token);
    valid = valid && end - source == 5 && pp_token_is_name(reader->pp, output, source + 2, end, "clang") &&
            pp_token_is_name(reader->pp, output, source + 3, end, "assume_nonnull");
    const bool begin = valid && pp_token_is_name(reader->pp, output, source + 4, end, "begin");
    valid = valid && (begin || pp_token_is_name(reader->pp, output, source + 4, end, "end"));
    if (valid)
    {
        *record = (CheckInputNonnullPragma)
        {
            .pragma = pragma,
            .source_begin = source,
            .source_end = end + 1,
            .token = token,
            .active_before = BUSTER_CHECK_NONE,
            .active_after = BUSTER_CHECK_NONE,
            .matching_begin = BUSTER_CHECK_NONE,
            .begin = begin,
        };
    }
    return valid;
}

// The preprocessor diagnoses nesting, unmatched ends, forbidden includes and an open region at file end. Retain
// its canonical begin/end history without changing pointer compatibility or inventing a nonnull runtime fact.
// A parser prefix may intentionally stop inside an originally balanced region, so this consumer imposes no
// additional end-of-unit obligation. Unexpected retained balance is a hard directive error (handled == 3).
static bool check_input_nonnull_pragmas(CheckInputReader* reader, uint8_t* handled)
{
    const uint64_t mark = reader->arena->length;
    const uint32_t pragmas = reader->parser->tokens.pragma_count;
    uint32_t capacity = 0;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    for (uint32_t pragma = 0; pragma < pragmas; pragma += 1)
    {
        CheckInputNonnullPragma record;
        capacity += check_input_nonnull_record(reader, pragma, &previous_end, &previous_token, &record);
    }
    bool valid = true;
    CheckInputNonnullPragmas* history = 0;
    if (capacity != 0)
    {
        history = arena_push(reader->arena, sizeof(*history), _Alignof(CheckInputNonnullPragmas));
        CheckInputNonnullPragma* records = history != 0 ? arena_push_array(reader->arena, capacity, sizeof(*records), _Alignof(CheckInputNonnullPragma)) : 0;
        valid = history != 0 && records != 0;
        reader->output->resource |= !valid;
        if (valid)
        {
            *history = (CheckInputNonnullPragmas)
            {
                .records = records,
                .active = BUSTER_CHECK_NONE,
            };
            previous_end = 0;
            previous_token = 0;
            for (uint32_t pragma = 0; pragma < pragmas; pragma += 1)
            {
                CheckInputNonnullPragma record;
                if (check_input_nonnull_record(reader, pragma, &previous_end, &previous_token, &record))
                {
                    record.active_before = history->active;
                    record.invalid = record.begin ? history->active != BUSTER_CHECK_NONE : history->active == BUSTER_CHECK_NONE;
                    history->error_count += record.invalid;
                    if (!record.invalid)
                    {
                        record.matching_begin = record.begin ? history->count : history->active;
                        history->active = record.begin ? history->count : BUSTER_CHECK_NONE;
                    }
                    record.active_after = history->active;
                    records[history->count] = record;
                    history->count += 1;
                }
            }
        }
    }
    if (valid && history != 0)
    {
        reader->output->nonnull_pragmas = history;
        for (uint32_t record = 0; record < history->count; record += 1)
        {
            handled[history->records[record].pragma] = history->records[record].invalid ? 3 : 1;
        }
    }
    else if (!valid)
    {
        reader->arena->length = mark;
    }
    return valid;
}

static bool check_input_diagnostic_string(CheckInputReader* reader, uint32_t source, String8* option)
{
    const PPTokens* output = &reader->pp->output;
    const uint32_t location = output->locations[source];
    const uint32_t value = output->values[source];
    const uint64_t mark = reader->scratch->length;
    bool valid = value >= location;
    uint8_t* buffer = valid ? arena_push(reader->scratch, (uint64_t)value - location + 1, 1) : 0;
    reader->output->resource |= valid && buffer == 0;
    valid &= buffer != 0;
    if (valid)
    {
        CheckStringPart part = { 0 };
        part.length = pp_spelling(reader->pp, C_TOKEN_STRING_LITERAL, value, location, buffer, &part.bytes);
        const CheckStringLiteral decoded = check_literal_strings(&part, 1, reader->pp->options.language.standard >= 2023, reader->arena);
        reader->output->resource |= decoded.resource;
        valid = decoded.valid && decoded.byte_count > 3 && decoded.bytes[0] == '-' &&
                (decoded.bytes[1] == 'W' || decoded.bytes[1] == 'R');
        if (valid)
        {
            *option = (String8)
            {
                .pointer = (char*)decoded.bytes,
                .length = decoded.byte_count - 1,
            };
        }
    }
    reader->scratch->length = mark;
    return valid;
}

// Each severity record is an immutable state node whose predecessor is state_before. A push saves that node; a
// pop restores it. Both dialects share the stack. Consumers can replay records in source order, or walk a state's
// predecessor chain for its most recent setting. Group membership and warning production remain their concern.
// The caller supplies zeroed handled[pragma_count]. Publish that mask and the container only after all allocations
// succeed. Allocation failure restores both arenas and leaves the existing mask/output untouched.
static bool check_input_diagnostic_pragmas(CheckInputReader* reader, uint8_t* handled)
{
    const uint64_t output_mark = reader->arena->length;
    const uint64_t scratch_mark = reader->scratch->length;
    const uint32_t pragmas = reader->parser->tokens.pragma_count;
    uint32_t capacity = 0;
    uint32_t pushes = 0;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    for (uint32_t pragma = 0; pragma < pragmas; pragma += 1)
    {
        CheckInputDiagnosticPragma record;
        if (check_input_diagnostic_record(reader, pragma, &previous_end, &previous_token, &record))
        {
            capacity += 1;
            pushes += record.operation == CHECK_DIAGNOSTIC_PUSH;
        }
    }
    bool valid = true;
    CheckInputDiagnosticPragmas* state = 0;
    if (capacity != 0)
    {
        state = arena_push(reader->arena, sizeof(*state), _Alignof(CheckInputDiagnosticPragmas));
        CheckInputDiagnosticPragma* records = state != 0 ? arena_push_array(reader->arena, capacity, sizeof(*records), _Alignof(CheckInputDiagnosticPragma)) : 0;
        CheckDiagnosticPush* stack = pushes != 0 && records != 0 ? arena_push_array(reader->scratch, pushes, sizeof(*stack), _Alignof(CheckDiagnosticPush)) : 0;
        valid = state != 0 && records != 0 && (pushes == 0 || stack != 0);
        reader->output->resource |= !valid;
        if (valid)
        {
            *state = (CheckInputDiagnosticPragmas)
            {
                .records = records,
                .final_state = BUSTER_CHECK_NONE,
            };
            previous_end = 0;
            previous_token = 0;
            for (uint32_t pragma = 0; valid && pragma < pragmas; pragma += 1)
            {
                CheckInputDiagnosticPragma record;
                if (check_input_diagnostic_record(reader, pragma, &previous_end, &previous_token, &record))
                {
                    const uint64_t string_mark = reader->arena->length;
                    bool recognized = record.operation < CHECK_DIAGNOSTIC_IGNORED ||
                                      check_input_diagnostic_string(reader, record.source_begin + 5, &record.option);
                    if (recognized)
                    {
                        record.state_before = state->final_state;
                        if (record.operation == CHECK_DIAGNOSTIC_PUSH)
                        {
                            stack[state->unmatched_pushes] = (CheckDiagnosticPush)
                            {
                                .state = state->final_state,
                                .record = state->count,
                            };
                            state->unmatched_pushes += 1;
                            state->maximum_depth = state->unmatched_pushes > state->maximum_depth ? state->unmatched_pushes : state->maximum_depth;
                        }
                        else if (record.operation == CHECK_DIAGNOSTIC_POP)
                        {
                            if (state->unmatched_pushes != 0)
                            {
                                state->unmatched_pushes -= 1;
                                const CheckDiagnosticPush saved = stack[state->unmatched_pushes];
                                state->final_state = saved.state;
                                record.matching_push = saved.record;
                            }
                            else
                            {
                                state->unmatched_pops += 1;
                            }
                        }
                        else
                        {
                            state->final_state = state->count;
                        }
                        record.state_after = state->final_state;
                        records[state->count] = record;
                        state->count += 1;
                    }
                    else
                    {
                        reader->arena->length = string_mark;
                    }
                    valid = !reader->output->resource;
                }
            }
        }
    }
    if (valid && state != 0)
    {
        reader->output->diagnostic_pragmas = state;
        for (uint32_t record = 0; record < state->count; record += 1)
        {
            handled[state->records[record].pragma] = 1;
        }
    }
    else if (!valid)
    {
        reader->arena->length = output_mark;
    }
    reader->scratch->length = scratch_mark;
    return valid;
}
