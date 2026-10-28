// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #pragma clang optimize on|off, as clang 23's PragmaOptimizeHandler reads it (#619: main.c's checker tests compile
// between `optimize off` and `on`, #681). The line takes one identifier, on or off, and nothing after it. An off makes
// the function definitions after it optnone and noinline until an on (Sema::AddRangeBasedOptnone), which changes nothing
// that -fsyntax-only reports or that a Tier 0 compile emits, so the reader only checks the line. Its errors, each at
// clang's token with clang's message: an argument other than on or off, and a token after it. clang reads both
// macro-expanded and the retained line has them as written, so a line with a name in either place that was a macro's
// anywhere in the unit is left to the caller's refusal (check_input_pragma_macro); so is a missing argument, an error at
// the line's end, whose place the retained line doesn't keep.

#define BUSTER_CHECK_OPTIMIZE_INPUT_IMPLEMENTATION 1

// An erroneous line's message and the output token clang reports it at (CheckInputOptimizePragmas).
typedef struct CheckInputOptimizeDiagnostic CheckInputOptimizeDiagnostic;
struct CheckInputOptimizeDiagnostic
{
    String8 text;
    uint32_t pragma;
    uint32_t source;
};

struct CheckInputOptimizePragmas
{
    CheckInputOptimizeDiagnostic* diagnostics;
    uint32_t diagnostic_count;
};

// Line `pragma`, from output token `source` (its '#') to `end` (its PRAGMA_END): 1 if it is read, 3 if it is an error at
// clang's place (into `diagnostic`, when it isn't NULL, its text in the arena), 0 if it is left to the caller's refusal.
static uint8_t check_input_optimize_line(CheckInputReader* reader, uint32_t pragma, uint32_t source, uint32_t end, uint64_t** macros,
                                         CheckInputOptimizeDiagnostic* diagnostic)
{
    const PPTokens* output = &reader->pp->output;
    const uint32_t argument = source + 4;
    const uint32_t extra = argument + 1;
    const IdentId argument_name = argument < end ? check_input_pragma_name(reader, argument) : 0;
    const bool identifier = argument < end && output->kinds[argument] == C_TOKEN_RAW_IDENTIFIER;
    const bool state = identifier && (pp_token_is_name(reader->pp, output, argument, end, "on") || pp_token_is_name(reader->pp, output, argument, end, "off"));
    const IdentId extra_name = state && extra < end ? check_input_pragma_name(reader, extra) : 0;
    uint8_t result = 1;
    uint32_t error = BUSTER_CHECK_NONE;
    if (argument == end || (identifier && check_input_pragma_macro(reader, macros, argument_name)))
    {
        result = 0;
    }
    else if (!state)
    {
        result = 3;
        error = argument;
    }
    else if (extra < end && extra_name != 0 && check_input_pragma_macro(reader, macros, extra_name))
    {
        result = 0;
    }
    else if (extra < end)
    {
        result = 3;
        error = extra;
    }
    if (result == 3 && diagnostic != NULL)
    {
        // The token's spelling, as clang's PP.getSpelling gives it, in clang's message.
        const uint64_t mark = reader->scratch->length;
        const uint32_t location = output->locations[error];
        const uint32_t value = output->values[error];
        const uint64_t room = value >= location ? (uint64_t)value - location + 24 : 24;
        uint8_t* buffer = arena_push(reader->scratch, room, 1);
        reader->output->resource |= buffer == 0;
        if (buffer != 0)
        {
            const uint8_t* spelling = buffer;
            const uint32_t length = pp_spelling(reader->pp, output->kinds[error], value, location, buffer, &spelling);
            const char* format = error == argument ? "unexpected argument '%.*s' to '#pragma clang optimize'; expected 'on' or 'off'" :
                                                     "unexpected extra argument '%.*s' to '#pragma clang optimize'";
            const int written = snprintf(NULL, 0, format, (int)length, (const char*)spelling);
            char* text = written > 0 ? arena_push(reader->arena, (uint64_t)written + 1, 1) : 0;
            reader->output->resource |= text == 0;
            if (text != 0)
            {
                snprintf(text, (uint64_t)written + 1, format, (int)length, (const char*)spelling);
                *diagnostic = (CheckInputOptimizeDiagnostic)
                {
                    .text = { .pointer = text, .length = (uint64_t)written },
                    .pragma = pragma,
                    .source = error,
                };
            }
        }
        reader->scratch->length = mark;
    }
    return result;
}

// The unit's lines, by two scans of the retained pragmas (a count of the erroneous ones, then their diagnostics), into
// CheckInput.optimize_pragmas and the handled mask: 1 for a line read, 3 for an error at clang's place (`messages`
// CHECK_INPUT_OPTIMIZE), 0 for one left to the caller's refusal. Returns false for want of memory, with both arenas as
// they were.
static bool check_input_optimize_pragmas(CheckInputReader* reader, uint8_t* handled, uint8_t* messages)
{
    const uint64_t output_mark = reader->arena->length;
    const uint64_t scratch_mark = reader->scratch->length;
    const uint32_t pragmas = reader->parser->tokens.pragma_count;
    const PPTokens* output = &reader->pp->output;
    uint64_t* macros = 0;
    uint32_t errors = 0;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    for (uint32_t pragma = 0; pragma < pragmas; pragma += 1)
    {
        uint32_t source = 0;
        uint32_t end = 0;
        uint32_t token = 0;
        if (check_input_retained_pragma(reader, pragma, &previous_end, &previous_token, &source, &end, &token) && end - source >= 4 &&
            pp_token_is_name(reader->pp, output, source + 2, end, "clang") && pp_token_is_name(reader->pp, output, source + 3, end, "optimize"))
        {
            const uint8_t line = check_input_optimize_line(reader, pragma, source, end, &macros, NULL);
            handled[pragma] = line;
            errors += line == 3;
        }
    }
    bool valid = !reader->output->resource;
    if (valid && errors != 0)
    {
        CheckInputOptimizePragmas* lines = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputOptimizePragmas, 1);
        CheckInputOptimizeDiagnostic* diagnostics = lines != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputOptimizeDiagnostic, errors) : 0;
        valid = diagnostics != 0;
        if (valid)
        {
            *lines = (CheckInputOptimizePragmas){ 0 };
        }
        previous_end = 0;
        previous_token = 0;
        for (uint32_t pragma = 0; valid && pragma < pragmas; pragma += 1)
        {
            uint32_t source = 0;
            uint32_t end = 0;
            uint32_t token = 0;
            if (check_input_retained_pragma(reader, pragma, &previous_end, &previous_token, &source, &end, &token) && handled[pragma] == 3 &&
                end - source >= 4 && pp_token_is_name(reader->pp, output, source + 2, end, "clang") && pp_token_is_name(reader->pp, output, source + 3, end, "optimize"))
            {
                check_input_optimize_line(reader, pragma, source, end, &macros, diagnostics + lines->diagnostic_count);
                valid = !reader->output->resource;
                lines->diagnostic_count += valid;
                messages[pragma] = (uint8_t)CHECK_INPUT_OPTIMIZE;
            }
        }
        if (valid)
        {
            lines->diagnostics = diagnostics;
            reader->output->optimize_pragmas = lines;
        }
    }
    reader->output->resource |= !valid;
    if (!valid)
    {
        reader->arena->length = output_mark;
    }
    reader->scratch->length = scratch_mark;
    return valid;
}

// The diagnostic of an input report of an erroneous line (CHECK_INPUT_OPTIMIZE), or NULL.
static const CheckInputOptimizeDiagnostic* check_input_optimize_report(const CheckInput* input, const CheckInputReport* report)
{
    const CheckInputOptimizePragmas* lines = input->optimize_pragmas;
    const CheckInputOptimizeDiagnostic* result = NULL;
    for (uint32_t i = 0; report->message == CHECK_INPUT_OPTIMIZE && report->pragma != 0 && lines != NULL && i < lines->diagnostic_count; i += 1)
    {
        result = lines->diagnostics[i].pragma == report->pragma - 1 ? lines->diagnostics + i : result;
    }
    return result;
}
