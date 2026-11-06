// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Join cold preprocessing expansion events with the FP collector's source-ordered scope state. Captured #if uses
// have no C token. A pragma ordinal distinguishes adjacent directives at the same parser position; a captured
// _Pragma can precede a use before either has reached the final output. The reader never mutates PP history.

struct CheckInputFpEvalUse
{
    uint32_t token;
    uint32_t use;
    uint32_t publication;
};

// Consume uses before the next pragma or ordinary token, under the state that precedes that source boundary.
// Called before each pragma, then before the ordinary token after those pragmas, including the EOF boundary.
static __attribute__((unused)) void check_input_fp_eval_uses(CheckInputReader* reader, uint32_t* cursor, uint32_t position,
                                                             uint32_t pragmas, uint32_t token, bool restricted)
{
    const Preprocessor* pp = reader->pp;
    const uint32_t count = pp->eval_method_use_count;
    while (*cursor < count && !reader->output->resource)
    {
        const uint32_t use = *cursor;
        const PPEvalMethodUse* record = pp->eval_method_uses + use;
        if (record->position > position || record->pragmas > pragmas)
        {
            break;
        }
        if (restricted)
        {
            if (reader->fp_eval_uses == NULL)
            {
                reader->fp_eval_uses = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputFpEvalUse, count);
                reader->output->resource |= reader->fp_eval_uses == NULL;
            }
            if (!reader->output->resource)
            {
                uint32_t publication = record->position;
                if (record->pragmas != 0 && record->pragmas <= reader->parser->tokens.pragma_count)
                {
                    const uint32_t source = reader->parser->tokens.pragma_sources[record->pragmas - 1];
                    uint32_t end = source;
                    if (parse_pragma_at(&pp->output, source, &end) && publication <= end)
                    {
                        publication = end + 1;
                    }
                }
                reader->fp_eval_uses[reader->fp_eval_use_count++] = (CheckInputFpEvalUse)
                {
                    .token = token,
                    .use = use,
                    .publication = publication,
                };
            }
        }
        *cursor = use + 1;
    }
}

// A macro-use error is lexical: its numeric answer still lets the checker construct the ordinary graph. Delay
// report insertion until that graph is built, rather than cutting reading at an unrelated following C token.
// A directive-only unit can have more rejected uses than C tokens, so grow the cold report array on demand.
static void check_input_fp_eval_finish(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const uint32_t count = reader->fp_eval_use_count;
    const uint64_t needed = (uint64_t)input->report_count + count;
    if (!input->resource && needed > input->report_capacity)
    {
        CheckInputReport* reports = needed <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputReport, needed) : NULL;
        input->resource |= reports == NULL;
        if (reports != NULL)
        {
            memcpy(reports, input->reports, (uint64_t)input->report_count * sizeof(CheckInputReport));
            input->reports = reports;
            input->report_capacity = (uint32_t)needed;
        }
    }
    for (uint32_t i = 0; !input->resource && i < count; i += 1)
    {
        const CheckInputFpEvalUse* use = reader->fp_eval_uses + i;
        input->reports[input->report_count++] = (CheckInputReport)
        {
            .token = use->token,
            .message = CHECK_INPUT_FP_EVAL_USE,
            .subject = use->use + 1,
            .detail = use->publication,
            .repeated = true,
        };
    }
}
