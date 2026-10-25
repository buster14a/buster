// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source rows for compiled C code's DWARF (#395): where each typed entry's code starts, after branch sizing, and the
// presumed line of its source token, which debug.c writes as each function's line program. Three passes, each a vector
// path with an exact twin, which --scalar takes:
//
// 1. Raw rows, in check_program_emit (check_debug_rows, twin check_debug_rows_scalar): entry e's first record's offset
//    (Tier0Code.entry_records, through branch sizing's offsets) and its token (CheckProgramLowered.entry_tokens), kept
//    where entry e + 1's offset differs: an entry fused into its consumer starts no code of its own, so the last entry at
//    an offset is the one whose code starts there. Before them a row at offset 0 with the declaration's token, the
//    prologue's, unless the first entry's code starts there; after them the epilogue's, with the closing brace's. 16
//    entries at a time: a gather of their offsets, the next entry's by a lane shift, a compare and a compress.
// 2. Lines, in the driver (check_debug_lines, twin check_debug_line_scalar): a row's token's presumed file and line as a
//    debugger shows its code: where its outermost macro expansion was invoked, or where it was written, after #line, as
//    the diagnostics place it (check_lazy_expansion_location, pp_line_of and the range's #line in force, which
//    pp_presumed scans for and check_debug_source indexes by range). A function's rows come from the tokens between its
//    declaration and its closing brace, whose places the vector path finds once (check_debug_span): a row whose output
//    token is between theirs and whose place is between theirs, in one file's range with no #line between them, takes
//    its history run, its macro chain and its line by searches 16 rows at a time over just the function's runs and
//    lines. The rest (a file the body includes, a #line inside it, a chain longer than BUSTER_CHECK_DEBUG_WALK) take the
//    exact path, row by row.
// 3. The rows kept (check_debug_keep, twin check_debug_keep_scalar): those in the function's file (debug.c's line
//    sequence has one) whose line differs from the kept row before's, and the row where the body starts, whatever its
//    line, which the unit marks (DebugFunction.prologue_end) for a debugger's breakpoint on the function.
//
// check_debug_build makes the driver's debug.c unit from them: a function per source body, named by its identifier, at
// its declaration's line, in a file table whose first file is the translation unit's, and the sections executable_write
// attaches (ExecutableOptions.debug), placing each function at its symbol's address. Every vector load is masked to its
// column, and every store whole into the rows' slack (BUSTER_CHECK_DEBUG_SLACK).

// Room past a pass's rows for the vector paths' whole stores.
#define BUSTER_CHECK_DEBUG_SLACK 16
// A row without a file's place: no file, line 0.
#define BUSTER_CHECK_DEBUG_NO_FILE UINT32_MAX
// A file key from a #line's name: the directive's index after this bit (a PPFile's index otherwise).
#define BUSTER_CHECK_DEBUG_NAMED (1u << 31)
// The macro chain links the vector path follows before it leaves a row to the exact path.
#define BUSTER_CHECK_DEBUG_WALK 8

// The vector path gathers the history's records by 32-bit dword indices.
_Static_assert(sizeof(PPExpansion) % 4 == 0 && sizeof(PPSubstitution) % 4 == 0 && sizeof(PPGenerated) % 4 == 0, "the history's records are dwords");
_Static_assert((uint64_t)BUSTER_PP_MAX_HISTORY * (sizeof(PPExpansion) / 4) < INT32_MAX && (uint64_t)BUSTER_PP_MAX_HISTORY * (sizeof(PPGenerated) / 4) < INT32_MAX,
               "the history's dword indices fit a gather's");

// check_program_emit's raw rows, when its caller asks for them: each source body's, in body order (body b's are first[b]
// to first[b] + counts[b]), their offsets ascending, each less than the function's code's bytes (`sizes`), and where its
// body's code starts, after its prologue (`bodies`). From the emission's output arena, with BUSTER_CHECK_DEBUG_SLACK
// rows' room past `count`.
typedef struct CheckProgramRows CheckProgramRows;
struct CheckProgramRows
{
    uint32_t* offsets;
    uint32_t* tokens;
    uint32_t* first;
    uint32_t* counts;
    uint32_t* sizes;
    uint32_t* bodies;
    uint32_t count;
    uint32_t capacity;
};

// A function's raw rows from its entries (`entry_count` of them, entry_records one more: the epilogue's first record)
// and branch sizing's offsets (by record, the record count's the function's length), into row_offsets and row_tokens
// (room for entry_count + 2, and the slack). Returns how many.
static uint32_t check_debug_rows_scalar(const uint32_t* entry_records, const uint32_t* entry_tokens, uint32_t entry_count, const uint32_t* offsets,
                                        uint32_t length, uint32_t declaration, uint32_t closing, uint32_t* row_offsets, uint32_t* row_tokens)
{
    row_offsets[0] = 0;
    row_tokens[0] = declaration;
    uint32_t count = offsets[entry_records[0]] != 0 ? 1 : 0;
    for (uint32_t e = 0; e < entry_count; e += 1)
    {
        const uint32_t offset = offsets[entry_records[e]];
        row_offsets[count] = offset;
        row_tokens[count] = entry_tokens[e];
        count += offset != offsets[entry_records[e + 1]] ? 1 : 0;
    }
    const uint32_t epilogue = offsets[entry_records[entry_count]];
    row_offsets[count] = epilogue;
    row_tokens[count] = closing;
    count += epilogue < length ? 1 : 0;
    return count;
}

// The same, 16 entries at a time: check_debug_rows_scalar's twin.
static uint32_t check_debug_rows(const uint32_t* entry_records, const uint32_t* entry_tokens, uint32_t entry_count, const uint32_t* offsets,
                                 uint32_t length, uint32_t declaration, uint32_t closing, uint32_t* row_offsets, uint32_t* row_tokens)
{
    row_offsets[0] = 0;
    row_tokens[0] = declaration;
    uint32_t count = offsets[entry_records[0]] != 0 ? 1 : 0;
    for (uint32_t e = 0; e < entry_count; e += 16)
    {
        const uint32_t take = entry_count - e < 16 ? entry_count - e : 16;
        const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, take);
        const __m512i records = _mm512_maskz_loadu_epi32(valid, entry_records + e);
        const __m512i here = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), valid, records, offsets, 4);
        // The next entry's offset: the next lane's, and the block's last entry's the entry after it's (the epilogue's
        // after the function's last).
        const __m512i after = _mm512_set1_epi32((int)offsets[entry_records[e + take]]);
        const __m512i next = _mm512_mask_mov_epi32(_mm512_alignr_epi32(after, here, 1), (__mmask16)(1u << (take - 1)), after);
        const __mmask16 keep = _mm512_mask_cmpneq_epu32_mask(valid, here, next);
        const __m512i tokens = _mm512_maskz_loadu_epi32(valid, entry_tokens + e);
        _mm512_storeu_si512(row_offsets + count, _mm512_maskz_compress_epi32(keep, here));
        _mm512_storeu_si512(row_tokens + count, _mm512_maskz_compress_epi32(keep, tokens));
        count += (uint32_t)__builtin_popcount(keep);
    }
    const uint32_t epilogue = offsets[entry_records[entry_count]];
    row_offsets[count] = epilogue;
    row_tokens[count] = closing;
    count += epilogue < length ? 1 : 0;
    return count;
}

// The history runs that start at or before output token `output`: pp_history_at's search, whose origin is the last one's.
static uint32_t check_debug_runs_at_most(const Preprocessor* pp, uint32_t output)
{
    uint32_t low = 0;
    uint32_t high = pp->history_run_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (pp->history_runs[middle].token <= output)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// The line of offset `offset` in a file whose line starts are `lines` (`count` of them, the first 0): the line starts at
// or before it, as pp_line_of counts them.
static uint32_t check_debug_line_at(const uint32_t* lines, uint32_t count, uint32_t offset)
{
    uint32_t low = 1;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (lines[middle] <= offset)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// What the lines are read from: the preprocessor and the parser, and each range's #lines in the order of their places
// (a range's notes are in that order, linked by next_in_range): range r's are directives[first[r]] to
// directives[first[r + 1]], which check_debug_directive searches, rather than walking a range's notes from its first.
typedef struct CheckDebugSource CheckDebugSource;
struct CheckDebugSource
{
    Preprocessor* pp;
    const Parser* parser;
    uint32_t* first;
    uint32_t* directives;
};

// The source of `pp` and `parser`, its index from `arena`; false when there's no room. The expansion history is recorded
// first if PPOptions.defer_history put it off (the checker's driver records it as the unit runs); a unit without one
// places its macros' code where their definitions spell it.
static bool check_debug_source(CheckDebugSource* source, Preprocessor* pp, const Parser* parser, Arena* arena)
{
    pp_history_ensure(pp);
    *source = (CheckDebugSource){
        .pp = pp,
        .parser = parser,
        .first = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)pp->range_count + 1),
        .directives = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)pp->line_directive_count + 1),
    };
    const bool valid = source->first != 0 && source->directives != 0;
    uint32_t count = 0;
    for (uint32_t range = 0; valid && range < pp->range_count; range += 1)
    {
        source->first[range] = count;
        uint32_t next = pp->ranges[range].first_line_directive;
        for (uint32_t steps = 0; next < pp->line_directive_count && count < pp->line_directive_count && steps < pp->line_directive_count; steps += 1)
        {
            source->directives[count] = next;
            count += 1;
            next = pp->line_directives[next].next_in_range;
        }
    }
    if (valid)
    {
        source->first[pp->range_count] = count;
    }
    return valid;
}

// The place of range `range`'s #lines at or before `location`: how many of them there are, among its directives
// (check_debug_source's); the #line in force is the last of them.
static uint32_t check_debug_directive(const CheckDebugSource* source, uint32_t range, uint32_t location)
{
    const uint32_t* directives = source->directives + source->first[range];
    uint32_t low = 0;
    uint32_t high = source->first[range + 1] - source->first[range];
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (source->pp->line_directives[directives[middle]].location <= location)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// A row's file key and line, the exact path: the presumed line of its token's place (where its outermost macro expansion
// was invoked, or where it was written) as pp_presumed gives it, and its file's key, the PPFile's index or
// BUSTER_CHECK_DEBUG_NAMED and the #line's whose name applies. A token without a file's place gets
// BUSTER_CHECK_DEBUG_NO_FILE and line 0.
static void check_debug_line_scalar(const CheckDebugSource* source, uint32_t token, uint32_t* key, uint32_t* line)
{
    Preprocessor* pp = source->pp;
    const Parser* parser = source->parser;
    const uint32_t output = token < parser->tokens.count ? parser->tokens.sources[token] : BUSTER_PP_NONE;
    const uint32_t location = output < pp->output.count ? check_lazy_expansion_location(pp, output, pp_history_at(pp, output)) : 0;
    const uint32_t range = location != 0 ? pp_find_range(pp, location) : BUSTER_PP_NONE;
    *key = BUSTER_CHECK_DEBUG_NO_FILE;
    *line = 0;
    if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
    {
        pp_diagnostic_lines(pp, range);
        uint32_t presumed = pp_line_of(pp, range, location, 0);
        uint32_t file = pp->ranges[range].file;
        // The range's last #line at or before the place.
        const uint32_t before = check_debug_directive(source, range, location);
        const uint32_t applies = before != 0 ? source->directives[source->first[range] + before - 1] : BUSTER_PP_NONE;
        if (applies != BUSTER_PP_NONE)
        {
            presumed = (uint32_t)((int64_t)presumed + pp->line_directives[applies].delta);
            file = pp->line_directives[applies].name != 0 ? BUSTER_CHECK_DEBUG_NAMED | applies : file;
        }
        *key = file;
        *line = presumed;
    }
}

// The vector path's bounds for one function's rows (check_debug_lines). Its rows' tokens are among those from its
// declaration to its closing brace, whose output tokens are `first_output` and `last_output` and whose places, `begin`
// and `end`, are in one file's range (`base`, its file's line starts `lines`), with no #line between them. A row whose
// output token and place are between those then has its history run in [first_run, end_run) (the runs before
// first_run start at or before first_output, and the one at end_run after last_output), its line in [first_line,
// end_line), and the #line's `delta` and the file `key` of every such row.
typedef struct CheckDebugSpan CheckDebugSpan;
struct CheckDebugSpan
{
    uint32_t first_output;
    uint32_t last_output;
    uint32_t first_run;
    uint32_t end_run;
    uint32_t begin;
    uint32_t end;
    uint32_t base;
    const uint32_t* lines;
    uint32_t first_line;
    uint32_t end_line;
    uint32_t delta;
    uint32_t key;
};

// A function's span, from its declaration's and closing brace's tokens, or false when they aren't in one file's range
// with no #line between them (its rows then all take the exact path).
static bool check_debug_span(const CheckDebugSource* source, uint32_t declaration, uint32_t closing, CheckDebugSpan* span)
{
    Preprocessor* pp = source->pp;
    const Parser* parser = source->parser;
    bool valid = declaration < parser->tokens.count && closing < parser->tokens.count;
    const uint32_t first = valid ? parser->tokens.sources[declaration] : 0;
    const uint32_t last = valid ? parser->tokens.sources[closing] : 0;
    valid = valid && first <= last && last < pp->output.count;
    const uint32_t begin = valid ? check_lazy_expansion_location(pp, first, pp_history_at(pp, first)) : 0;
    const uint32_t end = valid ? check_lazy_expansion_location(pp, last, pp_history_at(pp, last)) : 0;
    const uint32_t range = begin != 0 && end != 0 && begin <= end ? pp_find_range(pp, begin) : BUSTER_PP_NONE;
    valid = range != BUSTER_PP_NONE && pp_find_range(pp, end) == range && pp->ranges[range].file != BUSTER_PP_NONE;
    if (valid)
    {
        pp_diagnostic_lines(pp, range);
        const PPRange* record = &pp->ranges[range];
        const PPFileData* data = &pp->datas[pp->files[record->file].data];
        valid = data->diagnostic_lines != 0 && end - record->base < data->length;
        // The #line in force at the declaration, which must still be in force at the closing brace.
        const uint32_t before = check_debug_directive(source, range, begin);
        const uint32_t applies = before != 0 ? source->directives[source->first[range] + before - 1] : BUSTER_PP_NONE;
        valid = valid && check_debug_directive(source, range, end) == before;
        if (valid)
        {
            const bool named = applies != BUSTER_PP_NONE && pp->line_directives[applies].name != 0;
            *span = (CheckDebugSpan){
                .first_output = first,
                .last_output = last,
                .first_run = check_debug_runs_at_most(pp, first),
                .end_run = check_debug_runs_at_most(pp, last),
                .begin = begin,
                .end = end,
                .base = record->base,
                .lines = data->diagnostic_lines,
                .first_line = check_debug_line_at(data->diagnostic_lines, data->diagnostic_line_count, begin - record->base),
                .end_line = check_debug_line_at(data->diagnostic_lines, data->diagnostic_line_count, end - record->base),
                .delta = applies != BUSTER_PP_NONE ? (uint32_t)pp->line_directives[applies].delta : 0,
                .key = named ? BUSTER_CHECK_DEBUG_NAMED | applies : record->file,
            };
        }
    }
    return valid;
}

// The file keys and lines of `count` rows' tokens of the function whose declaration and closing brace are `declaration`
// and `closing`: the rows inside its span 16 at a time, the rest (and every row, `exact`) by check_debug_line_scalar.
static void check_debug_lines(const CheckDebugSource* source, uint32_t declaration, uint32_t closing, const uint32_t* tokens, uint32_t count, uint32_t* keys,
                              uint32_t* lines, bool exact)
{
    const Preprocessor* pp = source->pp;
    const Parser* parser = source->parser;
    CheckDebugSpan span = { 0 };
    const bool spanned = !exact && check_debug_span(source, declaration, closing, &span);
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi32(1);
    for (uint32_t i = 0; i < count; i += spanned ? 16 : 1)
    {
        if (!spanned)
        {
            check_debug_line_scalar(source, tokens[i], keys + i, lines + i);
        }
        else
        {
            const uint32_t take = count - i < 16 ? count - i : 16;
            const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, take);
            const __m512i token = _mm512_maskz_loadu_epi32(valid, tokens + i);
            const __mmask16 known = _mm512_mask_cmplt_epu32_mask(valid, token, _mm512_set1_epi32((int)parser->tokens.count));
            const __m512i output = _mm512_mask_i32gather_epi32(zero, known, token, parser->tokens.sources, 4);
            __mmask16 inside = _mm512_mask_cmpge_epu32_mask(known, output, _mm512_set1_epi32((int)span.first_output)) &
                               _mm512_cmple_epu32_mask(output, _mm512_set1_epi32((int)span.last_output));
            // The history run: the runs at or before the output token, within the function's.
            __m512i low = _mm512_set1_epi32((int)span.first_run);
            __m512i high = _mm512_set1_epi32((int)span.end_run);
            __mmask16 active = _mm512_mask_cmplt_epu32_mask(inside, low, high);
            while (active != 0)
            {
                const __m512i middle = _mm512_add_epi32(low, _mm512_srli_epi32(_mm512_sub_epi32(high, low), 1));
                const __m512i start = _mm512_mask_i32gather_epi32(zero, active, _mm512_slli_epi32(middle, 1), pp->history_runs, 4);
                const __mmask16 after = _mm512_mask_cmple_epu32_mask(active, start, output);
                low = _mm512_mask_add_epi32(low, after, middle, one);
                high = _mm512_mask_mov_epi32(high, active & ~after, middle);
                active = _mm512_mask_cmplt_epu32_mask(inside, low, high);
            }
            const __mmask16 run = _mm512_mask_cmpneq_epu32_mask(inside, low, zero);
            __m512i origin = _mm512_mask_i32gather_epi32(zero, run, _mm512_sub_epi32(_mm512_slli_epi32(low, 1), one), pp->history_runs, 4);
            __m512i location = _mm512_mask_i32gather_epi32(zero, inside, output, pp->output.locations, 4);
            // The macro chain to the outermost expansion's place, as check_lazy_expansion_location follows it: an
            // expansion's name's place, a generated token's anchor, an argument's substituted token.
            __mmask16 walking = _mm512_mask_cmpneq_epu32_mask(inside, origin, zero);
            for (uint32_t step = 0; walking != 0 && step < BUSTER_CHECK_DEBUG_WALK; step += 1)
            {
                const __m512i kind = _mm512_andnot_si512(_mm512_set1_epi32((int)BUSTER_PP_ORIGIN_INDEX), origin);
                const __m512i index = _mm512_sub_epi32(_mm512_and_si512(origin, _mm512_set1_epi32((int)BUSTER_PP_ORIGIN_INDEX)), one);
                const __mmask16 expansion = _mm512_mask_cmpeq_epu32_mask(walking, kind, zero) &
                                            _mm512_cmplt_epu32_mask(index, _mm512_set1_epi32((int)pp->history_count));
                const __mmask16 argument = _mm512_mask_cmpeq_epu32_mask(walking, kind, _mm512_set1_epi32((int)BUSTER_PP_ORIGIN_ARGUMENT)) &
                                           _mm512_cmplt_epu32_mask(index, _mm512_set1_epi32((int)pp->substitution_count));
                const __mmask16 generated = _mm512_mask_cmpeq_epu32_mask(walking, kind, _mm512_set1_epi32((int)BUSTER_PP_ORIGIN_GENERATED)) &
                                            _mm512_cmplt_epu32_mask(index, _mm512_set1_epi32((int)pp->generated_count));
                // A link out of its table's bounds leaves the row to the exact path, which refuses it the same way.
                inside &= ~(walking & ~(expansion | argument | generated));
                const __m512i expanded = _mm512_mullo_epi32(index, _mm512_set1_epi32(sizeof(PPExpansion) / 4));
                const __m512i substituted = _mm512_mullo_epi32(index, _mm512_set1_epi32(sizeof(PPSubstitution) / 4));
                const __m512i made = _mm512_mullo_epi32(index, _mm512_set1_epi32(sizeof(PPGenerated) / 4));
                location = _mm512_mask_i32gather_epi32(location, expansion, _mm512_add_epi32(expanded, _mm512_set1_epi32(offsetof(PPExpansion, begin) / 4)),
                                                       pp->history, 4);
                origin = _mm512_mask_i32gather_epi32(origin, expansion, _mm512_add_epi32(expanded, _mm512_set1_epi32(offsetof(PPExpansion, parent) / 4)),
                                                     pp->history, 4);
                origin = _mm512_mask_i32gather_epi32(origin, argument, _mm512_add_epi32(substituted, _mm512_set1_epi32(offsetof(PPSubstitution, source) / 4)),
                                                     pp->substitutions, 4);
                location = _mm512_mask_i32gather_epi32(location, generated, _mm512_add_epi32(made, _mm512_set1_epi32(offsetof(PPGenerated, anchor) / 4)),
                                                       pp->generated, 4);
                origin = _mm512_mask_i32gather_epi32(origin, generated, _mm512_add_epi32(made, _mm512_set1_epi32(offsetof(PPGenerated, expansion) / 4)),
                                                     pp->generated, 4);
                walking = _mm512_mask_cmpneq_epu32_mask(walking & inside, origin, zero);
            }
            const __mmask16 fast = (inside & ~walking) & _mm512_cmpge_epu32_mask(location, _mm512_set1_epi32((int)span.begin)) &
                                   _mm512_cmple_epu32_mask(location, _mm512_set1_epi32((int)span.end));
            // The line: the function's line starts at or before the place's offset in its file.
            const __m512i offset = _mm512_sub_epi32(location, _mm512_set1_epi32((int)span.base));
            low = _mm512_set1_epi32((int)span.first_line);
            high = _mm512_set1_epi32((int)span.end_line);
            active = _mm512_mask_cmplt_epu32_mask(fast, low, high);
            while (active != 0)
            {
                const __m512i middle = _mm512_add_epi32(low, _mm512_srli_epi32(_mm512_sub_epi32(high, low), 1));
                const __m512i start = _mm512_mask_i32gather_epi32(zero, active, middle, span.lines, 4);
                const __mmask16 after = _mm512_mask_cmple_epu32_mask(active, start, offset);
                low = _mm512_mask_add_epi32(low, after, middle, one);
                high = _mm512_mask_mov_epi32(high, active & ~after, middle);
                active = _mm512_mask_cmplt_epu32_mask(fast, low, high);
            }
            _mm512_mask_storeu_epi32(lines + i, fast, _mm512_add_epi32(low, _mm512_set1_epi32((int)span.delta)));
            _mm512_mask_storeu_epi32(keys + i, fast, _mm512_set1_epi32((int)span.key));
            for (uint32_t rest = valid & ~fast; rest != 0; rest &= rest - 1)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(rest);
                check_debug_line_scalar(source, tokens[i + lane], keys + i + lane, lines + i + lane);
            }
        }
    }
}

// A function's kept rows: those in its file, `key`, whose line is at most INT32_MAX (debug.c's limit) and differs from
// the kept row before's or that is at `body`, the body's start, into kept_offsets and kept_lines (room for `count`, and
// the slack). Returns how many.
static uint32_t check_debug_keep_scalar(const uint32_t* offsets, const uint32_t* keys, const uint32_t* lines, uint32_t count, uint32_t key, uint32_t body,
                                        uint32_t* kept_offsets, uint32_t* kept_lines)
{
    uint32_t kept = 0;
    uint32_t previous = UINT32_MAX;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const bool keep = keys[i] == key && lines[i] <= INT32_MAX && (lines[i] != previous || offsets[i] == body);
        kept_offsets[kept] = offsets[i];
        kept_lines[kept] = lines[i];
        previous = keep ? lines[i] : previous;
        kept += keep ? 1 : 0;
    }
    return kept;
}

// The same, 16 rows at a time: the rows in the file compressed together, each kept where its line differs from the one
// before it there (the block before's last for the first) or it is at the body's start, then compressed again.
// check_debug_keep_scalar's twin.
static uint32_t check_debug_keep(const uint32_t* offsets, const uint32_t* keys, const uint32_t* lines, uint32_t count, uint32_t key, uint32_t body,
                                 uint32_t* kept_offsets, uint32_t* kept_lines)
{
    uint32_t kept = 0;
    __m512i previous = _mm512_set1_epi32(-1);
    for (uint32_t i = 0; i < count; i += 16)
    {
        const uint32_t take = count - i < 16 ? count - i : 16;
        const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, take);
        const __m512i line = _mm512_maskz_loadu_epi32(valid, lines + i);
        const __mmask16 eligible = _mm512_mask_cmpeq_epu32_mask(valid, _mm512_maskz_loadu_epi32(valid, keys + i), _mm512_set1_epi32((int)key)) &
                                   _mm512_cmple_epu32_mask(line, _mm512_set1_epi32(INT32_MAX));
        const uint32_t count_eligible = (uint32_t)__builtin_popcount(eligible);
        const __m512i packed_lines = _mm512_maskz_compress_epi32(eligible, line);
        const __m512i packed_offsets = _mm512_maskz_compress_epi32(eligible, _mm512_maskz_loadu_epi32(valid, offsets + i));
        // Each packed row's predecessor: the packed lane before, the first's the last eligible line before the block.
        const __m512i before = _mm512_alignr_epi32(packed_lines, previous, 15);
        const __mmask16 packed = (__mmask16)_bzhi_u32(0xffff, count_eligible);
        const __mmask16 keep = _mm512_mask_cmpneq_epu32_mask(packed, packed_lines, before) | _mm512_mask_cmpeq_epu32_mask(packed, packed_offsets, _mm512_set1_epi32((int)body));
        _mm512_storeu_si512(kept_offsets + kept, _mm512_maskz_compress_epi32(keep, packed_offsets));
        _mm512_storeu_si512(kept_lines + kept, _mm512_maskz_compress_epi32(keep, packed_lines));
        kept += (uint32_t)__builtin_popcount(keep);
        previous = count_eligible != 0 ? _mm512_permutexvar_epi32(_mm512_set1_epi32((int)count_eligible - 1), packed_lines) : previous;
    }
    return kept;
}

// The C driver's DWARF for its executable: the debug.c unit, its sections and what executable_write takes.
typedef struct CheckDebug CheckDebug;
struct CheckDebug
{
    DebugUnit unit;
    DebugOutput output;
    ExecutableDebug attach;
    // The rows each pass made and kept, for the tests and the benchmark.
    uint32_t raw_rows;
};

// A file key's name: a PPFile's path, or a #line's.
static String8 check_debug_file_name(const Preprocessor* pp, uint32_t key)
{
    const PPLineDirective* named = (key & BUSTER_CHECK_DEBUG_NAMED) != 0 ? &pp->line_directives[key & ~BUSTER_CHECK_DEBUG_NAMED] : 0;
    const String8 result = named != 0 ? (String8){ .pointer = (char*)named->name, .length = named->name_length } :
                                        (String8){ .pointer = pp->files[key].path, .length = pp->files[key].path_length };
    return result;
}

// The unit's file of a key: the table's file of the same name, or a new one (`files`, room for the program's functions
// and one more). PPFile keys remember theirs (`by_file`).
static uint32_t check_debug_file(const Preprocessor* pp, uint32_t key, String8* files, uint32_t* file_count, uint32_t* by_file)
{
    uint32_t result = (key & BUSTER_CHECK_DEBUG_NAMED) == 0 ? by_file[key] : BUSTER_CHECK_DEBUG_NO_FILE;
    if (result == BUSTER_CHECK_DEBUG_NO_FILE)
    {
        const String8 name = check_debug_file_name(pp, key);
        for (uint32_t file = 0; result == BUSTER_CHECK_DEBUG_NO_FILE && file < *file_count; file += 1)
        {
            result = files[file].length == name.length && memcmp(files[file].pointer, name.pointer, name.length) == 0 ? file : BUSTER_CHECK_DEBUG_NO_FILE;
        }
        if (result == BUSTER_CHECK_DEBUG_NO_FILE)
        {
            result = *file_count;
            files[result] = name;
            *file_count += 1;
        }
        if ((key & BUSTER_CHECK_DEBUG_NAMED) == 0)
        {
            by_file[key] = result;
        }
    }
    return result;
}

// The driver's unit from check_program_emit's raw rows: each source body a function, its rows' lines (check_debug_lines),
// the rows it keeps (check_debug_keep) and its declaration's line, in the file of its declaration; its sections and their
// attachment written into `arena`, which must last through executable_write. False when memory runs out or debug.c
// refuses the unit (a name or a file debug.c can't write).
static bool check_debug_build(CheckDebug* debug, const CheckProgram* program, const CheckProgramRows* rows, const Parser* parser, Preprocessor* pp,
                              bool exact, Arena* arena, Arena* scratch)
{
    *debug = (CheckDebug){ .raw_rows = rows->count };
    const uint64_t mark = arena->length;
    const uint64_t temporary = scratch->length;
    const uint32_t count = program->body_count;
    uint32_t* keys = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK);
    uint32_t* lines = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK);
    uint32_t* by_file = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)pp->file_count + 1);
    String8* files = BUSTER_ARENA_PUSH_ARRAY(arena, String8, (uint64_t)count + 1);
    DebugFunction* functions = BUSTER_ARENA_PUSH_ARRAY(arena, DebugFunction, (uint64_t)count + 1);
    SymbolId* symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, (uint64_t)count + 1);
    uint32_t* kept_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK);
    uint32_t* kept_lines = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)rows->count + BUSTER_CHECK_DEBUG_SLACK);
    char* directory = BUSTER_ARENA_PUSH_ARRAY(arena, char, PATH_MAX);
    CheckDebugSource source;
    bool valid = keys != 0 && lines != 0 && by_file != 0 && files != 0 && functions != 0 && symbols != 0 && kept_offsets != 0 && kept_lines != 0 &&
                 directory != 0 && getcwd(directory, PATH_MAX) != 0 && pp->main_file < pp->file_count && check_debug_source(&source, pp, parser, scratch);
    uint32_t file_count = 0;
    uint32_t kept = 0;
    if (valid)
    {
        memset(by_file, 0xff, ((uint64_t)pp->file_count + 1) * sizeof(uint32_t));
        // The translation unit's file first: debug.c names the unit by it.
        check_debug_file(pp, pp->main_file, files, &file_count, by_file);
    }
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        const CheckProgramBody* body = &program->bodies[b];
        const uint32_t first = rows->first[b];
        const uint32_t closing = parser->tree.parents[body->opening];
        check_debug_lines(&source, body->declaration, closing, rows->tokens + first, rows->counts[b], keys + first, lines + first, exact);
        uint32_t key = BUSTER_CHECK_DEBUG_NO_FILE;
        uint32_t line = 0;
        check_debug_line_scalar(&source, body->declaration, &key, &line);
        const uint32_t file = key != BUSTER_CHECK_DEBUG_NO_FILE ? check_debug_file(pp, key, files, &file_count, by_file) : 0;
        const uint32_t start = rows->bodies[b];
        const uint32_t row_count = exact ? check_debug_keep_scalar(rows->offsets + first, keys + first, lines + first, rows->counts[b], key, start, kept_offsets + kept, kept_lines + kept) :
                                           check_debug_keep(rows->offsets + first, keys + first, lines + first, rows->counts[b], key, start, kept_offsets + kept, kept_lines + kept);
        // The body's row: the first kept, or the second after the prologue's.
        uint32_t prologue_end = 0;
        for (uint32_t r = row_count < 2 ? row_count : 2; r > 0; r -= 1)
        {
            prologue_end = kept_offsets[kept + r - 1] == start ? r : prologue_end;
        }
        const uint32_t name = parser->tokens.names[body->declaration];
        functions[b] = (DebugFunction){
            .name = { .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) },
            .size = rows->sizes[b],
            .first_row = kept,
            .row_count = row_count,
            .file = file,
            .line = line <= INT32_MAX ? line : 0,
            .prologue_end = prologue_end,
        };
        symbols[b] = program->object_symbols[body->object];
        kept += row_count;
    }
    debug->unit = (DebugUnit){
        .directory = { .pointer = directory, .length = valid ? strlen(directory) : 0 },
        .files = files,
        .file_count = file_count,
        .functions = functions,
        .function_count = count,
        .offsets = kept_offsets,
        .lines = kept_lines,
        .row_count = kept,
    };
    uint32_t* addresses = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + 1) : 0;
    debug->output = (DebugOutput){ .line_addresses = addresses, .address_capacity = count };
    // Measured first (DEBUG_ERROR_CAPACITY gives each section's size), then written.
    valid = valid && addresses != 0 && debug_write(&debug->unit, &debug->output, exact) == DEBUG_ERROR_CAPACITY;
    for (uint32_t section = 0; valid && section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        DebugSection* written = &debug->output.sections[section];
        written->capacity = written->size;
        written->bytes = arena_push(arena, written->size, 16);
        valid = written->bytes != 0;
    }
    valid = valid && debug_write(&debug->unit, &debug->output, exact) == DEBUG_OK;
    if (valid)
    {
        debug->attach = (ExecutableDebug){
            .functions = symbols,
            .line_addresses = addresses,
            .function_count = count,
        };
        for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
        {
            debug->attach.sections[section] = (String8){ .pointer = (char*)debug->output.sections[section].bytes, .length = debug->output.sections[section].size };
        }
    }
    else
    {
        arena_pop_to(arena, mark);
        *debug = (CheckDebug){ .raw_rows = rows->count };
    }
    arena_pop_to(scratch, temporary);
    return valid;
}
