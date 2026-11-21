// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Private expansion-history writers and diagnostic rendering. c_preprocessor_impl.c includes this after the same
// early preprocessor definitions as before the interface split; c_preprocessor_history.c supplies shared readers.

static void* pp_history_initial(Preprocessor* pp, uint32_t size, uint32_t* capacity)
{
    *capacity = BUSTER_PP_INITIAL_HISTORY_BYTES / size;
    return arena_push_array(&pp->history_initial, *capacity, size, 64);
}

// Grow once from the shared small slice to a table's full virtual reservation, whose first `small` bytes are base pages
// (pp_array_small). Origins are indices, not pointers, and no caller retains a record pointer across an append, so
// moving the initial records preserves every link.
static bool pp_history_grow(Preprocessor* pp, void** records, uint32_t* capacity, uint32_t needed, uint32_t maximum, uint32_t size, uint64_t small)
{
    bool result = needed <= *capacity;
    if (!result && needed <= maximum)
    {
        bool reserved = true;
        void* grown = pp_array_small(pp, &reserved, maximum, size, small);
        if (grown != 0)
        {
            memcpy(grown, *records, (uint64_t)*capacity * size);
            *records = grown;
            *capacity = maximum;
            result = true;
        }
    }
    if (!result)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, needed > maximum ? "expansion history capacity exceeded" : "out of memory for expansion history");
    }
    return result;
}

BUSTER_INLINE bool pp_history_room(Preprocessor* pp, void** records, uint32_t* capacity, uint32_t needed, uint32_t maximum, uint32_t size, uint64_t small)
{
    return needed <= *capacity || pp_history_grow(pp, records, capacity, needed, maximum, size, small);
}

static bool pp_history_origin_grow(Preprocessor* pp, PPTokens* tokens, uint32_t needed)
{
    bool result = needed <= tokens->origin_capacity;
    if (!result)
    {
        const uint64_t bytes = ((uint64_t)tokens->capacity + BUSTER_PP_TOKEN_SLACK) * sizeof(uint32_t);
        tokens->origin_arena = pp->fail_origin_growth ? (Arena){ 0 } : arena_reserve(bytes);
        pp->fail_origin_growth = false;
        uint32_t* grown = arena_push(&tokens->origin_arena, bytes, 64);
        if (grown != 0)
        {
            memcpy(grown, tokens->origins, (uint64_t)tokens->origin_capacity * sizeof(uint32_t));
            tokens->origins = grown;
            tokens->origin_capacity = tokens->capacity + BUSTER_PP_TOKEN_SLACK;
            result = true;
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "out of memory for temporary expansion origins");
        }
    }
    return result;
}

BUSTER_INLINE bool pp_history_origin_room(Preprocessor* pp, PPTokens* tokens, uint32_t needed)
{
    return needed <= tokens->origin_capacity || pp_history_origin_grow(pp, tokens, needed);
}

static uint32_t pp_history_expansion(Preprocessor* pp, PPMacroId macro, uint32_t location, uint32_t parent)
{
    uint32_t result = 0;
    if (!pp->options.no_history && !pp->fatal)
    {
        if (pp_history_room(pp, (void**)&pp->history, &pp->history_capacity, pp->history_count + 1, (BUSTER_PP_MAX_HISTORY >> pp->capacity_shift), sizeof(PPExpansion), BUSTER_ARENA_SMALL_BYTES))
        {
            const PPMacro* definition = &pp->macros[macro];
            result = ++pp->history_count;
            pp->history[result - 1] = (PPExpansion){
                .name = definition->name,
                .definition = definition->location,
                .begin = location,
                .end = location,
                .parent = parent,
                .end_origin = parent,
                .builtin = definition->builtin,
            };
        }
    }
    return result;
}

// A change at an output boundary. A trailing zero closes the last expansion, so plain file copies do no work here.
// Replacing that boundary with another expansion also coalesces equal neighbors into maximal runs.
static void pp_history_change(Preprocessor* pp, uint32_t token, uint32_t origin)
{
    uint32_t count = pp->history_run_count;
    count -= count != 0 && pp->history_runs[count - 1].token == token;
    const uint32_t previous = count != 0 ? pp->history_runs[count - 1].origin : 0;
    if (previous != origin)
    {
        if (pp_history_room(pp, (void**)&pp->history_runs, &pp->history_run_capacity, count + 1, (BUSTER_PP_MAX_OUTPUT_TOKENS >> pp->capacity_shift) + 1, sizeof(PPHistoryRun), BUSTER_ARENA_SMALL_BYTES))
        {
            pp->history_runs[count] = (PPHistoryRun)
            {
                .token = token,
                .origin = origin,
            };
            count += 1;
        }
    }
    pp->history_run_count = count;
}

static void pp_history_fill(Preprocessor* pp, PPTokens* target, uint32_t begin, uint32_t end, uint32_t origin)
{
    if (!pp->options.no_history && begin < end && !pp->fatal && (target == &pp->output || pp_history_origin_room(pp, target, end)))
    {
        if (target != &pp->output)
        {
            const uint32_t slot = target == &pp->arguments ? 0 : target == &pp->captures ? 1 : 2;
            pp->origin_peak[slot] = pp->origin_peak[slot] < end ? end : pp->origin_peak[slot];
        }
        if (target == &pp->output)
        {
            pp_history_change(pp, begin, origin);
            pp_history_change(pp, end, 0);
        }
        else if (end - begin == 1)
        {
            target->origins[begin] = origin;
        }
        else
        {
            const __m512i value = _mm512_set1_epi32((int)origin);
            for (uint32_t i = begin; i < end; i += 16)
            {
                const __mmask16 valid = (__mmask16)_bzhi_u32(~0u, end - i < 16 ? end - i : 16);
                _mm512_mask_storeu_epi32(target->origins + i, valid, value);
            }
        }
    }
}

// Copy history alongside a completed token copy, or compress it directly into the output run table. Each temporary
// buffer owns its origin column, so only a copy within the same PPTokens can overlap.
static void pp_history_copy(Preprocessor* pp, PPTokens* target, uint32_t at, const PPTokens* source, uint32_t begin, uint32_t end)
{
    if (!pp->options.no_history && begin < end && !pp->fatal && (target == &pp->output || pp_history_origin_room(pp, target, at + end - begin)))
    {
        if (target != &pp->output)
        {
            const uint32_t slot = target == &pp->arguments ? 0 : target == &pp->captures ? 1 : 2;
            pp->origin_peak[slot] = pp->origin_peak[slot] < at + end - begin ? at + end - begin : pp->origin_peak[slot];
            memmove(target->origins + at, source->origins + begin, (uint64_t)(end - begin) * sizeof(uint32_t));
        }
        else
        {
            uint32_t previous = source->origins[begin];
            pp_history_change(pp, at, previous);
            const __m256i lanes = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
            const __m256i shifted = _mm256_setr_epi32(0, 0, 1, 2, 3, 4, 5, 6);
            for (uint32_t i = begin + 1; i < end && !pp->fatal; i += 8)
            {
                const uint32_t count = end - i < 8 ? end - i : 8;
                const __mmask8 valid = (__mmask8)_bzhi_u32(~0u, count);
                const __m256i values = _mm256_maskz_loadu_epi32(valid, source->origins + i);
                const __m256i before = _mm256_mask_set1_epi32(_mm256_permutexvar_epi32(shifted, values), 1, (int)previous);
                const __mmask8 changes = _mm256_mask_cmpneq_epi32_mask(valid, values, before);
                const uint32_t added = (uint32_t)__builtin_popcount(changes);
                if (added != 0 && pp_history_room(pp, (void**)&pp->history_runs, &pp->history_run_capacity, pp->history_run_count + added, (BUSTER_PP_MAX_OUTPUT_TOKENS >> pp->capacity_shift) + 1, sizeof(PPHistoryRun), BUSTER_ARENA_SMALL_BYTES))
                {
                    const __m256i positions = _mm256_add_epi32(lanes, _mm256_set1_epi32((int)(at + i - begin)));
                    const __m512i records = _mm512_or_si512(_mm512_cvtepu32_epi64(positions), _mm512_slli_epi64(_mm512_cvtepu32_epi64(values), 32));
                    const __m512i packed = _mm512_maskz_compress_epi64(changes, records);
                    _mm512_mask_storeu_epi64(pp->history_runs + pp->history_run_count, (__mmask8)_bzhi_u32(~0u, added), packed);
                    pp->history_run_count += added;
                }
                previous = source->origins[i + count - 1];
            }
            pp_history_change(pp, at + end - begin, 0);
        }
    }
}

static uint32_t pp_history_substitution(Preprocessor* pp, uint32_t source, uint32_t parameter)
{
    uint32_t result = 0;
    if (!pp->options.no_history && !pp->fatal)
    {
        if (pp_history_room(pp, (void**)&pp->substitutions, &pp->substitution_capacity, pp->substitution_count + 1, (BUSTER_PP_MAX_HISTORY >> pp->capacity_shift), sizeof(PPSubstitution), BUSTER_PP_TABLE_SMALL_BYTES))
        {
            const uint32_t id = ++pp->substitution_count;
            pp->substitutions[id - 1] = (PPSubstitution)
            {
                .expansion = pp->history_current,
                .source = source,
                .parameter = parameter,
            };
            result = BUSTER_PP_ORIGIN_ARGUMENT | id;
        }
    }
    return result;
}

// One wrapper per contiguous origin run in an argument substitution, including repeated uses of the same parameter.
// Read the source directly: copying its origins first would only write values that this pass immediately replaces.
// A source in expansions is below its current count, so appending wrappers cannot overwrite unread source origins.
static void pp_history_argument(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t at, uint32_t parameter)
{
    PPTokens* out = &pp->expansions;
    for (uint32_t i = begin; !pp->options.no_history && i < end && !pp->fatal;)
    {
        const uint32_t source = tokens->origins[i];
        uint32_t stop = i + 1;
        while (stop < end && tokens->origins[stop] == source)
        {
            stop += 1;
        }
        pp_history_fill(pp, out, at + i - begin, at + stop - begin, pp_history_substitution(pp, source, parameter));
        i = stop;
    }
}

static uint32_t pp_history_generated(Preprocessor* pp, PPGeneratedKind kind, uint32_t location, uint32_t anchor, const PPSource* sources, uint32_t count)
{
    uint32_t result = 0;
    if (!pp->options.no_history && !pp->fatal)
    {
        if (pp_history_room(pp, (void**)&pp->generated, &pp->generated_capacity, pp->generated_count + 1, (BUSTER_PP_MAX_HISTORY >> pp->capacity_shift), sizeof(PPGenerated), BUSTER_PP_TABLE_SMALL_BYTES) &&
            pp_history_room(pp, (void**)&pp->history_sources, &pp->history_source_capacity, pp->history_source_count + count, (BUSTER_PP_MAX_HISTORY >> pp->capacity_shift), sizeof(PPSource), BUSTER_ARENA_SMALL_BYTES))
        {
            const uint32_t id = ++pp->generated_count;
            pp->generated[id - 1] = (PPGenerated){
                .expansion = pp->history_current,
                .location = location,
                .anchor = anchor,
                .first = pp->history_source_count,
                .count = count,
                .kind = kind,
            };
            if (count != 0)
            {
                memcpy(pp->history_sources + pp->history_source_count, sources, (uint64_t)count * sizeof(PPSource));
            }
            pp->history_source_count += count;
            result = BUSTER_PP_ORIGIN_GENERATED | id;
        }
    }
    return result;
}

// Sixteen independent binary searches. Record loads are scalar on Zen 5; bounds/decisions run in vectors.
static __attribute__((unused)) void pp_history_lookup(const Preprocessor* pp, const uint32_t* tokens, uint32_t* origins, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 16)
    {
        const uint32_t take = count - i < 16 ? count - i : 16;
        const __mmask16 valid = (__mmask16)_bzhi_u32(~0u, take);
        const __m512i positions = _mm512_maskz_loadu_epi32(valid, tokens + i);
        __m512i low = _mm512_setzero_si512();
        __m512i high = _mm512_set1_epi32((int)pp->history_run_count);
        __mmask16 active = valid & _mm512_cmplt_epu32_mask(low, high);
        while (active != 0)
        {
            const __m512i middle = _mm512_add_epi32(low, _mm512_srli_epi32(_mm512_sub_epi32(high, low), 1));
            uint32_t indices[16] __attribute__((aligned(64)));
            uint32_t starts[16] __attribute__((aligned(64))) =
            {
                0,
            };
            _mm512_store_si512(indices, middle);
            // Clang otherwise turns these explicit scalar loads back into Zen 5's expensive gathers.
#pragma clang loop vectorize(disable) interleave(disable)
            for (uint32_t k = 0; k < take; k += 1)
            {
                if ((active >> k) & 1)
                {
                    starts[k] = pp->history_runs[indices[k]].token;
                }
            }
            const __mmask16 after = active & _mm512_cmple_epu32_mask(_mm512_load_si512(starts), positions);
            low = _mm512_mask_add_epi32(low, after, middle, _mm512_set1_epi32(1));
            high = _mm512_mask_mov_epi32(high, active & ~after, middle);
            active = valid & _mm512_cmplt_epu32_mask(low, high);
        }
        uint32_t indices[16] __attribute__((aligned(64)));
        _mm512_store_si512(indices, low);
#pragma clang loop vectorize(disable) interleave(disable)
        for (uint32_t k = 0; k < take; k += 1)
        {
            origins[i + k] = tokens[i + k] < pp->output.count && indices[k] != 0 ? pp->history_runs[indices[k] - 1].origin : 0;
        }
    }
}

static void pp_diagnostic_flush(PPDiagnostic* diagnostic)
{
    if (diagnostic->count != 0)
    {
        fwrite(diagnostic->bytes, 1, diagnostic->count, diagnostic->stream);
        diagnostic->count = 0;
    }
}

static void pp_diagnostic_format(PPDiagnostic* diagnostic, const char* format, va_list arguments)
{
    va_list copy;
    va_copy(copy, arguments);
    const int length = diagnostic->direct ? -1 : vsnprintf(diagnostic->bytes + diagnostic->count, diagnostic->capacity - diagnostic->count, format, copy);
    va_end(copy);
    if (!diagnostic->direct && length >= 0 && (uint64_t)length < diagnostic->capacity - diagnostic->count)
    {
        diagnostic->count += (uint32_t)length;
    }
    else
    {
        uint64_t needed = 0;
        const bool sized = length >= 0 && !__builtin_add_overflow(diagnostic->count, (uint64_t)(uint32_t)length + 1, &needed);
        const uint64_t doubled = diagnostic->capacity <= UINT64_MAX / 2 ? diagnostic->capacity * 2 : 0;
        const uint64_t capacity = needed > doubled ? needed : doubled;
        char* grown = !diagnostic->direct && sized && diagnostic->arena != 0 ? arena_push(diagnostic->arena, capacity, 1) : 0;
        if (grown != 0)
        {
            memcpy(grown, diagnostic->bytes, diagnostic->count);
            diagnostic->bytes = grown;
            diagnostic->capacity = capacity;
            va_copy(copy, arguments);
            const int written = vsnprintf(grown + diagnostic->count, capacity - diagnostic->count, format, copy);
            va_end(copy);
            if (written == length)
            {
                diagnostic->count += (uint32_t)written;
            }
            else
            {
                diagnostic->direct = true;
            }
        }
        else
        {
            diagnostic->direct = true;
        }
        if (diagnostic->direct)
        {
            pp_diagnostic_flush(diagnostic);
            vfprintf(diagnostic->stream, format, arguments);
        }
    }
}

// A fatal error's flag as clang's printer gives it (#1447): "-Werror," names a warning made an error, so a fatal one (a
// warning -Werror promoted and -Wfatal-errors made fatal, or a pragma's fatal) has its group alone, `[-Wgroup]`, and one of
// no group no flag. Removes "-Werror," from the trailing flag of the message written from byte `start`, or a trailing
// " [-Werror]" whole.
static void pp_diagnostic_fatal_flag(PPDiagnostic* diagnostic, uint64_t start)
{
    static const char tag[] = " [-Werror,";
    static const char bare[] = " [-Werror]";
    const uint64_t size = sizeof(tag) - 1;
    const bool closed = !diagnostic->direct && diagnostic->count > start && diagnostic->bytes[diagnostic->count - 1] == ']';
    uint64_t open = closed ? diagnostic->count - 1 : start;
    while (open > start && diagnostic->bytes[open] != '[')
    {
        open -= 1;
    }
    const bool flagged = closed && open > start && diagnostic->count - (open - 1) >= size;
    const bool tagged = flagged && memcmp(diagnostic->bytes + open - 1, tag, size) == 0;
    const bool alone = flagged && diagnostic->count - (open - 1) == size && memcmp(diagnostic->bytes + open - 1, bare, size) == 0;
    if (tagged)
    {
        const uint64_t removed = size - 2;
        memmove(diagnostic->bytes + open + 1, diagnostic->bytes + open + 1 + removed, diagnostic->count - (open + 1 + removed));
        diagnostic->count -= removed;
    }
    else if (alone)
    {
        diagnostic->count = open - 1;
    }
}

static __attribute__((format(printf, 2, 3))) void pp_diagnostic_append(PPDiagnostic* diagnostic, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_diagnostic_format(diagnostic, format, arguments);
    va_end(arguments);
}

static void pp_diagnostic_place(Preprocessor* pp, PPDiagnostic* diagnostic, uint32_t location)
{
    const uint32_t range = location != 0 ? pp_find_range(pp, location) : BUSTER_PP_NONE;
    if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
    {
        pp_diagnostic_lines(pp, range);
        uint32_t column;
        pp_line_of(pp, range, location, &column);
        const char* name;
        uint32_t length;
        const uint32_t line = pp_presumed(pp, location, &name, &length);
        pp_diagnostic_append(diagnostic, "%.*s:%u:%u: ", (int)length, name, line, column);
    }
    else if (location != 0)
    {
        pp_diagnostic_append(diagnostic, "<scratch space>:1:1: ");
    }
}

// Clang's caret display (#2376): TextDiagnostic::emitSnippetAndCaret's source line, gutter and caret, which
// pp_render_diagnostic writes under each located line it prints, the primary's and its notes'. Clang 23's limits: no
// source line for a caret past BUSTER_PP_SNIPPET_BYTES bytes into its line, or a line longer than that
// (MaxLineLengthToPrint); tabs to stops BUSTER_PP_SNIPPET_TAB_STOP columns apart (DefaultTabStop), counted in bytes from
// the previous tab, as clang counts them; and a gutter as wide as the line number BUSTER_PP_SNIPPET_LINES lines on
// (DefaultSnippetLineLimit, or -fcaret-diagnostics-max-lines=N), and at least 4 digits.
#define BUSTER_PP_SNIPPET_BYTES 4096
#define BUSTER_PP_SNIPPET_TAB_STOP 8
#define BUSTER_PP_SNIPPET_LINES 16
#define BUSTER_PP_SNIPPET_GUTTER 4

// The decimal digits of `value` (getNumDisplayWidth).
static uint32_t pp_snippet_digits(uint32_t value)
{
    uint32_t digits = 1;
    for (uint64_t power = 10; power <= value; power *= 10)
    {
        digits += 1;
    }
    return digits;
}

// The code point of the UTF-8 sequence `bytes`, `size` bytes (1 to 6, the count its first byte says) and all of them in
// the line, where LLVM's isLegalUTF8 takes it: at most four bytes, continuation bytes 0x80 to 0xBF, no overlong form,
// surrogate or code point past U+10FFFF. Else UINT32_MAX.
static uint32_t pp_snippet_code_point(const uint8_t* bytes, uint32_t size)
{
    const uint8_t first = bytes[0];
    bool legal = size <= 4 && first <= 0xF4 && (first < 0x80 || first >= 0xC2);
    for (uint32_t i = 1; legal && i < size; i += 1)
    {
        legal = bytes[i] >= 0x80 && bytes[i] <= 0xBF;
    }
    const uint8_t second = size > 1 ? bytes[1] : 0x80;
    legal = legal && (first != 0xE0 || second >= 0xA0) && (first != 0xED || second <= 0x9F) && (first != 0xF0 || second >= 0x90) && (first != 0xF4 || second <= 0x8F);
    uint32_t code_point = size == 1 ? first : size == 2 ? first & 0x1Fu : size == 3 ? first & 0x0Fu : first & 0x07u;
    for (uint32_t i = 1; legal && i < size; i += 1)
    {
        code_point = (code_point << 6) | (bytes[i] & 0x3Fu);
    }
    return legal ? code_point : UINT32_MAX;
}

// Appends source line `line` (`length` bytes) as clang's emitSnippet prints it, and fills `columns` (length + 1 entries)
// with clang's SourceColumnMap: each byte's display column where a character starts there, UINT32_MAX for its other
// bytes, and the line's width at `length`, which it returns. Printable ASCII is itself, a column a byte; a tab, spaces
// to the next stop; a well-formed UTF-8 character that LLVM's tables call printable, itself, in unicode_column_width's
// columns; another well-formed one, <U+XXXX>; and a byte of no well-formed character, <XX>. The plain runs are found,
// copied and numbered 64 bytes at a time; the other bytes, a line's rare tabs, controls and non-ASCII characters, take
// a step each, since a character's size and width come from its own bytes and LLVM's range tables. Cold: it runs once
// a line a printed diagnostic shows.
static uint32_t pp_snippet_line(PPDiagnostic* diagnostic, const uint8_t* line, uint32_t length, uint32_t* columns)
{
    uint32_t column = 0;
    // The byte after the last tab, from which a tab's stop counts bytes (bytesSincePreviousTabOrLineBegin).
    uint32_t tab_end = 0;
    uint32_t at = 0;
    const __m512i steps = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    while (at < length)
    {
        const uint32_t remaining = length - at;
        const uint32_t loaded = remaining < 64 ? remaining : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, loaded);
        const __m512i bytes = _mm512_maskz_loadu_epi8(valid, line + at);
        const uint64_t special = (_mm512_cmplt_epu8_mask(bytes, _mm512_set1_epi8(0x20)) | _mm512_cmpge_epu8_mask(bytes, _mm512_set1_epi8(0x7f))) & valid;
        const uint32_t run = special != 0 ? (uint32_t)_tzcnt_u64(special) : loaded;
        pp_diagnostic_append(diagnostic, "%.*s", (int)run, (const char*)line + at);
        for (uint32_t i = 0; i < run; i += 16)
        {
            const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffffu, run - i);
            _mm512_mask_storeu_epi32(columns + at + i, lanes, _mm512_add_epi32(steps, _mm512_set1_epi32((int)(column + i))));
        }
        column += run;
        at += run;
        if (special != 0)
        {
            const uint8_t byte = line[at];
            // LLVM's getNumBytesForUTF8: a continuation byte counts 1, a lead byte its sequence's length.
            const uint32_t claimed = byte < 0xC0 ? 1 : byte < 0xE0 ? 2 : byte < 0xF0 ? 3 : byte < 0xF8 ? 4 : byte < 0xFC ? 5 : 6;
            const uint32_t code_point = byte != '\t' && claimed <= length - at ? pp_snippet_code_point(line + at, claimed) : UINT32_MAX;
            const int32_t width = code_point != UINT32_MAX && code_point >= 0x80 ? unicode_column_width(code_point) : -1;
            const uint32_t size = code_point != UINT32_MAX ? claimed : 1;
            uint32_t shown = 0;
            if (byte == '\t')
            {
                shown = BUSTER_PP_SNIPPET_TAB_STOP - (at - tab_end) % BUSTER_PP_SNIPPET_TAB_STOP;
                pp_diagnostic_append(diagnostic, "%*s", (int)shown, "");
                tab_end = at + 1;
            }
            else if (width >= 0)
            {
                pp_diagnostic_append(diagnostic, "%.*s", (int)size, (const char*)line + at);
                shown = (uint32_t)width;
            }
            else
            {
                char escaped[16];
                const int written = code_point != UINT32_MAX ? snprintf(escaped, sizeof(escaped), "<U+%04X>", code_point) : snprintf(escaped, sizeof(escaped), "<%02X>", byte);
                pp_diagnostic_append(diagnostic, "%s", escaped);
                shown = (uint32_t)written;
            }
            columns[at] = column;
            for (uint32_t i = 1; i < size; i += 1)
            {
                columns[at + i] = UINT32_MAX;
            }
            column += shown;
            at += size;
        }
    }
    columns[length] = column;
    pp_diagnostic_append(diagnostic, "\n");
    return column;
}

// The column that holds byte `byte` of a line `columns` maps (byteToContainingColumn): its character's first byte's.
static uint32_t pp_snippet_containing_column(const uint32_t* columns, uint32_t byte)
{
    uint32_t at = byte;
    while (at != 0 && columns[at] == UINT32_MAX)
    {
        at -= 1;
    }
    return columns[at];
}

// A range highlighted on one line of a snippet (clang's LineRange): bytes [start, end) of line `line`, its `end` past the
// line's bytes for a range that goes on.
typedef struct PPSnippetLineRange PPSnippetLineRange;
struct PPSnippetLineRange
{
    uint32_t line;
    uint32_t start;
    uint32_t end;
};

// Marks `range` with '~' in `marks` (`*width` columns written so far, room for the line's width): its bytes but the
// spaces and tabs at its ends, by their columns (highlightRange).
static void pp_snippet_highlight(const PPSnippetLineRange* range, const uint8_t* line, uint32_t length, const uint32_t* columns, char* marks, uint32_t* width)
{
    uint32_t start = range->start;
    while (start < length && (line[start] == ' ' || line[start] == '\t'))
    {
        // startOfNextColumn: the next byte a column starts at.
        start += 1;
        while (start < length && columns[start] == UINT32_MAX)
        {
            start += 1;
        }
    }
    uint32_t end = range->end < length ? range->end : length;
    while (end != 0 && (line[end - 1] == ' ' || line[end - 1] == '\t'))
    {
        // startOfPreviousColumn.
        end -= 1;
        while (end != 0 && columns[end] == UINT32_MAX)
        {
            end -= 1;
        }
    }
    if (start <= end)
    {
        const uint32_t first = pp_snippet_containing_column(columns, start);
        const uint32_t last = pp_snippet_containing_column(columns, end);
        for (uint32_t i = *width; i < last; i += 1)
        {
            marks[i] = ' ';
        }
        *width = last > *width ? last : *width;
        memset(marks + first, '~', last - first);
    }
}

// The lines a snippet shows, as clang's maybeAddRange grows them by a range's: [*first, *last] widened towards
// [begin, end], to at most `most` lines.
static void pp_snippet_add_lines(uint32_t* first, uint32_t* last, uint32_t begin, uint32_t end, uint32_t most)
{
    // Unsigned arithmetic, as clang's.
    uint32_t slack = most - (*last - *first + 1);
    const uint32_t low = begin < *first ? begin : *first;
    const uint32_t high = end > *last ? end : *last;
    if (slack != 0 && high - low + 1 <= most)
    {
        *first = low;
        *last = high;
    }
    else if (slack != 0 && !((begin > *first && begin - *first + 1 > most) || (end < *last && *last - end + 1 > most)))
    {
        *last = *last + (slack + 1) / 2 < high ? *last + (slack + 1) / 2 : high;
        slack = most - (*last - *first + 1);
        *first = (low + slack > *first ? low + slack : *first) - slack;
        *last = *first + most - 1 < high ? *first + most - 1 : high;
    }
}

// The source lines and caret under a diagnostic's line at `location`, its printed place (or a note's, with `note`), as
// clang's emitSnippetAndCaret shows them, with `ranges` (`range_count`, mapped into its file: pp_diagnostic_map_range)
// marked with '~': none where carets are off, at no place or a place in scratch space, or at the last diagnostic's
// place again when this one has no range (but after a note where this one is no note), as clang's LastLoc rule says;
// none past the file's end or the limits above. The caret's line and as many of its ranges' lines around it as the
// snippet's line limit allows, each its physical line, the gutter numbered from the presumed one of the first.
static void pp_diagnostic_snippet(Preprocessor* pp, PPDiagnostic* diagnostic, uint32_t location, bool note, const PPDiagnosticRange* ranges, uint32_t range_count)
{
    const bool repeated = location == pp->diagnostic_caret_location && range_count == 0 && (!pp->diagnostic_caret_note || note);
    const uint32_t range = pp->options.carets && location != 0 && !repeated ? pp_find_range(pp, location) : BUSTER_PP_NONE;
    const uint32_t file = range != BUSTER_PP_NONE ? pp->ranges[range].file : BUSTER_PP_NONE;
    PPFileData* data = file != BUSTER_PP_NONE ? &pp->datas[pp->files[file].data] : NULL;
    uint32_t caret_column = 0;
    const uint32_t caret_line = data != NULL ? pp_line_of(pp, range, location, &caret_column) : 0;
    if (data != NULL && caret_column <= BUSTER_PP_SNIPPET_BYTES)
    {
        const uint8_t* bytes = data->bytes;
        const uint32_t length = data->length;
        const uint32_t most = pp->options.caret_lines_given ? pp->options.caret_lines : BUSTER_PP_SNIPPET_LINES;
        const char* name;
        uint32_t name_length;
        uint32_t display = pp_presumed(pp, location, &name, &name_length);
        uint32_t first = caret_line;
        uint32_t last = caret_line;
        // Each range's lines and bytes in the caret's file (findLinesForRange, prepareAndFilterRanges): its first byte's,
        // and its last token's and the byte after it.
        uint32_t begin_lines[BUSTER_PP_DIAGNOSTIC_RANGES];
        uint32_t begin_bytes[BUSTER_PP_DIAGNOSTIC_RANGES];
        uint32_t end_lines[BUSTER_PP_DIAGNOSTIC_RANGES];
        uint32_t end_bytes[BUSTER_PP_DIAGNOSTIC_RANGES];
        bool kept[BUSTER_PP_DIAGNOSTIC_RANGES];
        const uint32_t count = range_count < BUSTER_PP_DIAGNOSTIC_RANGES ? range_count : BUSTER_PP_DIAGNOSTIC_RANGES;
        for (uint32_t r = 0; r < count; r += 1)
        {
            kept[r] = ranges[r].begin != 0 && ranges[r].end != 0 && pp_find_range(pp, ranges[r].begin) == range && pp_find_range(pp, ranges[r].end) == range;
            if (kept[r])
            {
                uint32_t column;
                begin_lines[r] = pp_line_of(pp, range, ranges[r].begin, &column);
                begin_bytes[r] = column - 1;
                end_lines[r] = pp_line_of(pp, range, ranges[r].end, &column);
                end_bytes[r] = column - 1 + ranges[r].end_length;
                pp_snippet_add_lines(&first, &last, begin_lines[r], end_lines[r], most);
                const uint32_t begun = pp_presumed(pp, ranges[r].begin, &name, &name_length);
                display = begun < display ? begun : display;
            }
        }
        // Unsigned arithmetic, as clang's: a large -fcaret-diagnostics-max-lines= wraps.
        const uint32_t widest = pp_snippet_digits(display + most);
        const uint32_t gutter = pp->options.no_line_numbers ? 0 : widest > BUSTER_PP_SNIPPET_GUTTER ? widest : BUSTER_PP_SNIPPET_GUTTER;
        // The line table pp_diagnostic_place built; without it (its allocation failed) the caret's line alone.
        const uint32_t* starts = data->diagnostic_lines;
        const uint32_t caret_start = location - pp->ranges[range].base - (caret_column - 1);
        first = starts != NULL ? first : caret_line;
        last = starts != NULL ? last : caret_line;
        bool going = true;
        for (uint32_t line = first; going && line <= last && line >= first; line += 1, display += 1)
        {
            const uint32_t start = starts != NULL && line - 1 < data->diagnostic_line_count ? starts[line - 1] : line == caret_line ? caret_start : length;
            // The line's end: its first '\n' or '\r', looked for 64 bytes at a time up to one byte past the longest one shown.
            const uint32_t limit = length - start < BUSTER_PP_SNIPPET_BYTES + 1 ? length - start : BUSTER_PP_SNIPPET_BYTES + 1;
            uint32_t line_length = limit;
            for (uint32_t at = 0; line_length == limit && at < limit; at += 64)
            {
                const uint32_t loaded = limit - at < 64 ? limit - at : 64;
                const __m512i block = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, loaded), bytes + start + at);
                const uint64_t ends = (_mm512_cmpeq_epi8_mask(block, _mm512_set1_epi8('\n')) | _mm512_cmpeq_epi8_mask(block, _mm512_set1_epi8('\r'))) & _bzhi_u64(~0ull, loaded);
                line_length = ends != 0 ? at + (uint32_t)_tzcnt_u64(ends) : line_length;
            }
            going = start < length && line_length <= BUSTER_PP_SNIPPET_BYTES;
            uint32_t* columns = going ? arena_push(diagnostic->arena, ((uint64_t)line_length + 1) * sizeof(uint32_t), 4) : NULL;
            going = columns != NULL;
            if (going)
            {
                // Clang drops the line's trailing NUL bytes, on the caret's line those past the caret's.
                while (line_length != 0 && bytes[start + line_length - 1] == 0 && (line != caret_line || line_length > caret_column))
                {
                    line_length -= 1;
                }
                if (gutter != 0)
                {
                    const uint32_t digits = pp_snippet_digits(display);
                    pp_diagnostic_append(diagnostic, "%*s%u | ", (int)(gutter + 1 > digits ? gutter + 1 - digits : 0), "", display);
                }
                const uint32_t width = pp_snippet_line(diagnostic, bytes + start, line_length, columns);
                char* marks = arena_push(diagnostic->arena, (uint64_t)width + 2, 1);
                uint32_t marked = 0;
                for (uint32_t r = 0; marks != NULL && r < count; r += 1)
                {
                    const bool on = kept[r] && begin_lines[r] <= line && end_lines[r] >= line && begin_lines[r] <= last && end_lines[r] >= first;
                    const PPSnippetLineRange part =
                    {
                        .line = line,
                        .start = begin_lines[r] == line ? begin_bytes[r] : 0,
                        .end = end_lines[r] == line ? end_bytes[r] : UINT32_MAX,
                    };
                    if (on)
                    {
                        pp_snippet_highlight(&part, bytes + start, line_length, columns, marks, &marked);
                    }
                }
                if (marks != NULL && line == caret_line)
                {
                    const uint32_t caret = pp_snippet_containing_column(columns, caret_column - 1 < line_length ? caret_column - 1 : line_length);
                    for (uint32_t i = marked; i < caret; i += 1)
                    {
                        marks[i] = ' ';
                    }
                    marks[caret] = '^';
                    marked = caret + 1 > marked ? caret + 1 : marked;
                }
                if (marked != 0)
                {
                    if (gutter != 0)
                    {
                        pp_diagnostic_append(diagnostic, "%*s| ", (int)(gutter + 2), "");
                    }
                    pp_diagnostic_append(diagnostic, "%.*s\n", (int)marked, marks);
                }
            }
        }
    }
}

// Mapping a highlighted range into the file a diagnostic's caret is in (#2376), as clang's mapDiagnosticRanges does,
// on the expansion history: a place is a location and its history (PPRangePlace), and the expansions clang's FileIDs
// tell apart are the history's own, an expansion's, an argument's substitution's and a generated token's, and a file
// range's for a place in none (pp_range_scope). A substitution is clang's macro argument expansion: its immediate
// spelling place is the argument's place in its caller, and its immediate expansion place its parameter's in the
// macro's expansion. An expansion's immediate expansion range is its invocation, from its name to its end (its ')').
// The walks are bounded: a range deeper than BUSTER_PP_RANGE_DEPTH, or than BUSTER_PP_RANGE_STEPS steps, is dropped.
#define BUSTER_PP_RANGE_DEPTH 256
#define BUSTER_PP_RANGE_STEPS 4096

typedef struct PPRangePlace PPRangePlace;
struct PPRangePlace
{
    uint32_t location;
    uint32_t origin;
};

// The expansion `place` is in, as clang's FileID tells them apart: its history's entry, or its file range's.
static uint64_t pp_range_scope(const Preprocessor* pp, PPRangePlace place)
{
    return place.origin != 0 ? place.origin : ((uint64_t)1 << 32) | pp_find_range(pp, place.location);
}

static bool pp_range_argument(PPRangePlace place)
{
    return (place.origin & ~BUSTER_PP_ORIGIN_INDEX) == BUSTER_PP_ORIGIN_ARGUMENT;
}

// The immediate expansion range's begin (or its end) of a place in an expansion (getImmediateExpansionRange).
static PPRangePlace pp_range_expansion(const Preprocessor* pp, PPRangePlace place, bool begin)
{
    const uint32_t index = (place.origin & BUSTER_PP_ORIGIN_INDEX) - 1;
    const uint32_t kind = place.origin & ~BUSTER_PP_ORIGIN_INDEX;
    PPRangePlace result;
    if (kind == BUSTER_PP_ORIGIN_ARGUMENT)
    {
        result = (PPRangePlace){ pp->substitutions[index].parameter, pp->substitutions[index].expansion };
    }
    else if (kind == BUSTER_PP_ORIGIN_GENERATED)
    {
        result = (PPRangePlace){ pp->generated[index].anchor, pp->generated[index].expansion };
    }
    else
    {
        const PPExpansion* expansion = &pp->history[index];
        result = begin ? (PPRangePlace){ expansion->begin, expansion->parent } : (PPRangePlace){ expansion->end, expansion->end_origin };
    }
    return result;
}

// The immediate spelling place of a place in an expansion (getImmediateSpellingLoc): an argument's in its caller, else
// its own location, which is its spelling's.
static PPRangePlace pp_range_spelling(const Preprocessor* pp, PPRangePlace place)
{
    const bool argument = pp_range_argument(place);
    return (PPRangePlace){ place.location, argument ? pp->substitutions[(place.origin & BUSTER_PP_ORIGIN_INDEX) - 1].source : 0 };
}

// The substitutions `place`'s walk to a file passes (getMacroArgExpansionFileIDs), into `scopes` (at most
// BUSTER_PP_RANGE_DEPTH); their count, or UINT32_MAX for a deeper walk.
static uint32_t pp_range_arguments(const Preprocessor* pp, PPRangePlace place, bool begin, uint64_t* scopes)
{
    uint32_t count = 0;
    uint32_t steps = 0;
    while (place.origin != 0 && count != UINT32_MAX && steps < BUSTER_PP_RANGE_STEPS)
    {
        const bool argument = pp_range_argument(place);
        if (argument && count < BUSTER_PP_RANGE_DEPTH)
        {
            scopes[count++] = place.origin;
        }
        else if (argument)
        {
            count = UINT32_MAX;
        }
        place = argument ? pp_range_spelling(pp, place) : pp_range_expansion(pp, place, begin);
        steps += 1;
    }
    return steps < BUSTER_PP_RANGE_STEPS ? count : UINT32_MAX;
}

// The place in the caret's expansion (`caret`) that `place` comes from, as clang's retrieveMacroLocation finds it, a
// begin's or an end's: up through an expansion's invocation, or an argument's caller where both ends come through it
// (`common`), first, then the other way where that reaches no place there. Its iterative search keeps the other ways
// on a stack. A location of 0 where none reaches it.
static PPRangePlace pp_range_retrieve(const Preprocessor* pp, PPRangePlace place, uint64_t caret, const uint64_t* common, uint32_t common_count, bool begin)
{
    PPRangePlace others[BUSTER_PP_RANGE_DEPTH];
    uint32_t other_count = 0;
    PPRangePlace result = { 0, 0 };
    bool done = false;
    for (uint32_t steps = 0; !done && steps < BUSTER_PP_RANGE_STEPS; steps += 1)
    {
        const uint64_t scope = pp_range_scope(pp, place);
        const bool reached = scope == caret;
        const bool failed = !reached && place.origin == 0;
        result = reached ? place : result;
        done = reached || (failed && other_count == 0);
        if (failed && other_count != 0)
        {
            other_count -= 1;
            place = others[other_count];
        }
        else if (!done)
        {
            const bool argument = pp_range_argument(place);
            bool shared = false;
            for (uint32_t i = 0; argument && i < common_count; i += 1)
            {
                shared |= common[i] == scope;
            }
            const PPRangePlace first = argument ? pp_range_spelling(pp, place) : pp_range_expansion(pp, place, begin);
            const PPRangePlace second = argument ? pp_range_expansion(pp, place, begin) : pp_range_spelling(pp, place);
            const bool tried = (!argument || shared) && first.location != 0;
            if (tried && other_count < BUSTER_PP_RANGE_DEPTH)
            {
                others[other_count++] = second;
            }
            place = tried ? first : second;
        }
    }
    return result;
}

// The bytes of the token at `location` that a range's mapped end moved to (MeasureTokenLength): an invocation's ')' or
// an object-like macro's name, which are the places an expansion ends at.
static uint32_t pp_range_measure(const Preprocessor* pp, uint32_t location)
{
    const uint32_t range = pp_find_range(pp, location);
    uint32_t result = 1;
    if (range != BUSTER_PP_NONE && location - pp->ranges[range].base < pp->ranges[range].length)
    {
        const uint8_t* bytes = pp->ranges[range].bytes;
        const uint32_t start = location - pp->ranges[range].base;
        const uint8_t first = bytes[start];
        const bool identifier = first == '_' || first == '$' || (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first >= 0x80;
        const uint32_t end = identifier ? pp_identifier_end(pp, bytes, pp->ranges[range].length, start) : start + 1;
        result = end > start ? end - start : 1;
    }
    return result;
}

// `range` mapped into the expansion `caret` (a caret's file range's scope) as its first and last places' spellings, its
// end measured again where it moved (clang's mapDiagnosticRanges); false where the range can't be: its ends in no
// common expansion, or in none that reaches the caret's.
static bool pp_diagnostic_map_range(const Preprocessor* pp, const PPDiagnosticRange* range, uint64_t caret, PPDiagnosticRange* mapped)
{
    PPRangePlace begin = { range->begin, range->begin_origin };
    PPRangePlace end = { range->end, range->end_origin };
    uint64_t begin_scope = pp_range_scope(pp, begin);
    uint64_t end_scope = pp_range_scope(pp, end);
    // The begin's expansions up to the end's, each one's place kept; then the end's up to one of them.
    PPRangePlace begins[BUSTER_PP_RANGE_DEPTH];
    uint64_t begin_scopes[BUSTER_PP_RANGE_DEPTH];
    uint32_t depth = 0;
    bool valid = range->begin != 0 && range->end != 0;
    while (valid && begin.origin != 0 && begin_scope != end_scope)
    {
        valid = depth < BUSTER_PP_RANGE_DEPTH;
        begins[depth % BUSTER_PP_RANGE_DEPTH] = begin;
        begin_scopes[depth % BUSTER_PP_RANGE_DEPTH] = begin_scope;
        depth += 1;
        begin = pp_range_expansion(pp, begin, true);
        begin_scope = pp_range_scope(pp, begin);
    }
    uint32_t found = UINT32_MAX;
    for (uint32_t steps = 0; valid && begin_scope != end_scope && found == UINT32_MAX && steps < BUSTER_PP_RANGE_STEPS; steps += 1)
    {
        for (uint32_t i = 0; i < depth && found == UINT32_MAX; i += 1)
        {
            found = begin_scopes[i] == end_scope ? i : found;
        }
        const bool moving = found == UINT32_MAX && end.origin != 0;
        end = moving ? pp_range_expansion(pp, end, false) : end;
        end_scope = moving ? pp_range_scope(pp, end) : end_scope;
        valid = moving || found != UINT32_MAX;
    }
    if (valid && found != UINT32_MAX)
    {
        begin = begins[found];
        begin_scope = end_scope;
    }
    valid = valid && begin_scope == end_scope && begin.location != 0 && end.location != 0;
    // The substitutions both ends' walks pass (computeCommonMacroArgExpansionFileIDs).
    uint64_t begin_arguments[BUSTER_PP_RANGE_DEPTH];
    uint64_t end_arguments[BUSTER_PP_RANGE_DEPTH];
    uint64_t common[BUSTER_PP_RANGE_DEPTH];
    const uint32_t begin_count = valid ? pp_range_arguments(pp, begin, true, begin_arguments) : 0;
    const uint32_t end_count = valid ? pp_range_arguments(pp, end, false, end_arguments) : 0;
    valid = valid && begin_count != UINT32_MAX && end_count != UINT32_MAX;
    uint32_t common_count = 0;
    for (uint32_t i = 0; valid && i < begin_count; i += 1)
    {
        bool shared = false;
        for (uint32_t j = 0; j < end_count; j += 1)
        {
            shared |= begin_arguments[i] == end_arguments[j];
        }
        common[common_count] = begin_arguments[i];
        common_count += shared ? 1 : 0;
    }
    const PPRangePlace first = valid ? pp_range_retrieve(pp, begin, caret, common, common_count, true) : (PPRangePlace){ 0, 0 };
    const PPRangePlace last = valid ? pp_range_retrieve(pp, end, caret, common, common_count, false) : (PPRangePlace){ 0, 0 };
    valid = valid && first.location != 0 && last.location != 0;
    if (valid)
    {
        *mapped = (PPDiagnosticRange)
        {
            .begin = first.location,
            .end = last.location,
            .end_length = last.location == range->end ? range->end_length : pp_range_measure(pp, last.location),
        };
    }
    return valid;
}

// The bytes output token `token`'s spelling takes in the source: an identifier's or a keyword's up to its end there,
// an integer's digits, another token's from its place to its end (its value).
static uint32_t pp_diagnostic_token_length(Preprocessor* pp, uint32_t token)
{
    const uint8_t kind = pp->output.kinds[token];
    const uint32_t value = pp->output.values[token];
    const uint32_t location = pp->output.locations[token];
    const bool identifier = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT);
    uint32_t result = 1;
    if (identifier)
    {
        result = pp_range_measure(pp, location);
    }
    else if (kind == BUSTER_PP_KIND_INTEGER)
    {
        result = pp_snippet_digits(value);
    }
    else if (kind != BUSTER_PP_KIND_EMBED && value > location)
    {
        result = value - location;
    }
    return result;
}

// Adds output tokens [first, last] as a highlighted range of the next diagnostic (#2376), clang's token range: a caller
// sets them before it and clears them after it (pp_diagnostic_ranges_clear), as diagnostic_spans. Beyond
// BUSTER_PP_DIAGNOSTIC_RANGES ranges, nothing. They stay tokens until the diagnostic is accepted
// (pp_diagnostic_resolve_ranges).
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_range_tokens(Preprocessor* pp, uint32_t first, uint32_t last)
{
    if (pp->diagnostic_range_count < BUSTER_PP_DIAGNOSTIC_RANGES && first <= last)
    {
        pp->diagnostic_ranges[pp->diagnostic_range_count] = (PPDiagnosticRange)
        {
            .begin = first,
            .end = last,
            .pending = PP_RANGE_TOKENS,
        };
        pp->diagnostic_range_count += 1;
    }
}

// Adds `length` characters at `location` (in a file, through no expansion) as a highlighted range of the next diagnostic,
// clang's character range.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_range_characters(Preprocessor* pp, uint32_t location, uint32_t length)
{
    if (pp->diagnostic_range_count < BUSTER_PP_DIAGNOSTIC_RANGES && location != 0 && length != 0)
    {
        pp->diagnostic_ranges[pp->diagnostic_range_count] = (PPDiagnosticRange)
        {
            .begin = location,
            .end = location,
            .end_length = length,
        };
        pp->diagnostic_range_count += 1;
    }
}

static uint32_t pp_spelling_offset_location(Preprocessor* pp, uint32_t location, uint32_t offset);

// Adds `length` characters of output token `token`'s spelling from its `offset`th (after translation phases 1 and 2, as
// pp_diagnose_output_offset_at places a diagnostic) as a highlighted range of the next diagnostic, clang's character
// range inside a literal.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_range_offsets(Preprocessor* pp, uint32_t token, uint32_t offset, uint32_t length)
{
    if (pp->diagnostic_range_count < BUSTER_PP_DIAGNOSTIC_RANGES && length != 0)
    {
        pp->diagnostic_ranges[pp->diagnostic_range_count] = (PPDiagnosticRange)
        {
            .begin = token,
            .end = offset,
            .end_length = length,
            .pending = PP_RANGE_OFFSETS,
        };
        pp->diagnostic_range_count += 1;
    }
}

// The pending ranges of an accepted diagnostic as places (PPDiagnosticRange.pending): tokens' and offsets' from the
// output and its history, which an output diagnostic's path (pp_diagnose_source_location) has ensured; `resolve` false
// keeps the places alone, as the lexical paths and the collection do, which mustn't ask for the history (its replay
// reports through them). A range of tokens not in the output is dropped.
static void pp_diagnostic_resolve_ranges(Preprocessor* pp, bool resolve)
{
    const uint32_t count = resolve ? pp->output.count : 0;
    uint32_t kept = 0;
    for (uint32_t r = 0; r < pp->diagnostic_range_count; r += 1)
    {
        PPDiagnosticRange range = pp->diagnostic_ranges[r];
        bool valid = true;
        if (range.pending == PP_RANGE_TOKENS)
        {
            valid = range.end < count;
            range = valid ? (PPDiagnosticRange)
                            {
                                .begin = pp->output.locations[range.begin],
                                .begin_origin = pp_history_at(pp, range.begin),
                                .end = pp->output.locations[range.end],
                                .end_origin = pp_history_at(pp, range.end),
                                .end_length = pp_diagnostic_token_length(pp, range.end),
                            } : range;
        }
        else if (range.pending == PP_RANGE_OFFSETS)
        {
            valid = range.begin < count;
            const uint32_t base = valid ? pp->output.locations[range.begin] : 0;
            const uint32_t origin = valid ? pp_history_at(pp, range.begin) : 0;
            range = valid ? (PPDiagnosticRange)
                            {
                                .begin = pp_spelling_offset_location(pp, base, range.end),
                                .begin_origin = origin,
                                .end = pp_spelling_offset_location(pp, base, range.end + range.end_length - 1),
                                .end_origin = origin,
                                .end_length = 1,
                            } : range;
        }
        pp->diagnostic_ranges[kept] = range;
        kept += valid ? 1 : 0;
    }
    pp->diagnostic_range_count = kept;
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_ranges_clear(Preprocessor* pp)
{
    pp->diagnostic_range_count = 0;
}

// The place clang's renderer compares the next diagnostic's with (pp_diagnostic_snippet): a diagnostic's, once it and
// its notes are printed, and an "expanded from" note's, once it is.
static void pp_diagnostic_snippet_done(Preprocessor* pp, uint32_t location, bool note)
{
    pp->diagnostic_caret_location = location;
    pp->diagnostic_caret_note = note;
}

// Where the file `location` is presumed in was included, as clang's PresumedLoc::getIncludeLoc gives it (#1954): a line
// marker's (PPLineDirective.include_location) in effect there, else its range's inclusion's; BUSTER_PP_NONE for none.
static uint32_t pp_presumed_include(const Preprocessor* pp, uint32_t location)
{
    const uint32_t range = location != BUSTER_PP_NONE ? pp_find_range(pp, location) : BUSTER_PP_NONE;
    const uint32_t note = range != BUSTER_PP_NONE && pp->ranges[range].first_line_directive != BUSTER_PP_NONE ? pp_line_note_at(pp, range, location) : BUSTER_PP_NONE;
    const uint32_t marked = note != BUSTER_PP_NONE ? pp->line_directives[note].include_location : BUSTER_PP_NONE;
    const bool included = range != BUSTER_PP_NONE && pp->ranges[range].parent != BUSTER_PP_NONE;
    return marked != BUSTER_PP_NONE ? marked : included ? pp->ranges[range].include_location : BUSTER_PP_NONE;
}

// The include stack before a diagnostic at `location`, printed when its include location differs from the last one's, as
// clang's DiagnosticRenderer::emitIncludeStack compares them (Preprocessor.diagnostic_include_range keeps the last one).
static void pp_diagnostic_includes(Preprocessor* pp, PPDiagnostic* diagnostic, uint32_t location)
{
    uint32_t chain[BUSTER_PP_MAX_INCLUDE_DEPTH + 1];
    uint32_t count = 0;
    uint32_t include = pp_presumed_include(pp, location);
    const bool changed = include != pp->diagnostic_include_range;
    pp->diagnostic_include_range = include;
    while (changed && include != BUSTER_PP_NONE && count < BUSTER_ARRAY_LENGTH(chain))
    {
        chain[count++] = include;
        include = pp_presumed_include(pp, include);
    }
    for (uint32_t i = count; i > 0; i -= 1)
    {
        const char* name;
        uint32_t length;
        const uint32_t line = pp_presumed(pp, chain[i - 1], &name, &length);
        pp_diagnostic_append(diagnostic, "In file included from %.*s:%u:\n", (int)length, name, line);
    }
}

static void pp_render_diagnostic(Preprocessor* pp, PPSeverity severity, uint32_t location, uint32_t origin, const PPDiagnosticNote* note, const char* format, va_list arguments)
{
    Scratch scratch = scratch_begin(0);
    char bytes[BUSTER_PP_DIAGNOSTIC_BYTES];
    PPDiagnostic diagnostic =
    {
        .stream = pp->options.diagnostics != 0 ? pp->options.diagnostics : stderr,
        .arena = scratch.arena,
        .bytes = bytes,
        .capacity = sizeof(bytes),
    };
    uint64_t capacity = 0;
    uint32_t cursor = origin;
    while (cursor != 0)
    {
        const uint32_t index = (cursor & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = cursor & ~BUSTER_PP_ORIGIN_INDEX;
        cursor = kind == BUSTER_PP_ORIGIN_ARGUMENT ? pp->substitutions[index].source : kind == BUSTER_PP_ORIGIN_GENERATED ? pp->generated[index].expansion : pp->history[index].parent;
        capacity += 1;
    }
    PPDiagnosticSource* notes = origin != 0 ? arena_push_array(scratch.arena, capacity, sizeof(PPDiagnosticSource), 8) : 0;
    uint32_t count = 0;
    for (uint64_t steps = 0; notes != 0 && origin != 0 && steps < capacity + pp->substitution_count; steps += 1)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        if (kind == BUSTER_PP_ORIGIN_ARGUMENT)
        {
            // Forwarding alone is silent. If the substituted token is itself subsequently expanded, Clang also
            // shows the parameter through which the macro name arrived (CALL(F), where CALL(f) expands to f()), and
            // so it does where a diagnostic's range spans more than the argument's one token (render_spans).
            const PPSubstitution* substitution = &pp->substitutions[index];
            if (count != 0 || pp->render_spans || pp->diagnostic_arguments)
            {
                notes[count++] = (PPDiagnosticSource)
                {
                    .location = substitution->parameter,
                    .origin = substitution->expansion,
                };
            }
            origin = substitution->source;
        }
        else if (kind == BUSTER_PP_ORIGIN_GENERATED)
        {
            const PPGenerated* generated = &pp->generated[index];
            const uint32_t start = generated->kind == PP_GENERATED_PRAGMA && generated->count == 1 ? pp->history_sources[generated->first].location : 0;
            notes[count++] = (PPDiagnosticSource)
            {
                .location = location,
                .origin = 0,
                .column = start != 0 && location >= start && location - start <= UINT32_MAX - 2 ? location - start + 2 : 0,
            };
            location = generated->anchor;
            origin = generated->expansion;
        }
        else
        {
            const PPExpansion* expansion = &pp->history[index];
            // Builtins already have their identity and invocation in this expansion record. Their spelling is the
            // token's location, so they need no duplicate PPGenerated record, just the scratch-space note here.
            notes[count++] = (PPDiagnosticSource)
            {
                .location = location,
                .origin = expansion->builtin == PP_BUILTIN_NONE ? origin : 0,
            };
            location = expansion->begin;
            origin = expansion->parent;
        }
    }
    if (severity == PP_SEVERITY_NOTE)
    {
        // Clang omits include stacks for attached notes, but their source still makes the next primary's
        // include context change. A note in another header therefore causes that next prefix to repeat.
        pp->diagnostic_include_range = pp_presumed_include(pp, location);
    }
    else
    {
        pp_diagnostic_includes(pp, &diagnostic, location);
    }
    pp_diagnostic_place(pp, &diagnostic, location);
    pp_diagnostic_append(&diagnostic, "%s: ", severity == PP_SEVERITY_WARNING ? "warning" : severity == PP_SEVERITY_ERROR ? "error" : severity == PP_SEVERITY_NOTE ? "note" : "fatal error");
    const uint64_t message = diagnostic.count;
    pp_diagnostic_format(&diagnostic, format, arguments);
    if (severity == PP_SEVERITY_FATAL)
    {
        pp_diagnostic_fatal_flag(&diagnostic, message);
    }
    pp_diagnostic_append(&diagnostic, "\n");
    // The ranges mapped into the caret's file (emitCaret's mapDiagnosticRanges).
    PPDiagnosticRange mapped[BUSTER_PP_DIAGNOSTIC_RANGES];
    uint32_t mapped_count = 0;
    const uint32_t caret_range = location != 0 ? pp_find_range(pp, location) : BUSTER_PP_NONE;
    for (uint32_t r = 0; caret_range != BUSTER_PP_NONE && pp->render_ranges != NULL && r < pp->render_range_count && r < BUSTER_PP_DIAGNOSTIC_RANGES; r += 1)
    {
        mapped_count += pp_diagnostic_map_range(pp, &pp->render_ranges[r], ((uint64_t)1 << 32) | caret_range, &mapped[mapped_count]) ? 1 : 0;
    }
    pp_diagnostic_snippet(pp, &diagnostic, location, severity == PP_SEVERITY_NOTE, mapped, mapped_count);
    for (uint32_t i = count; i > 0; i -= 1)
    {
        const PPDiagnosticSource note = notes[i - 1];
        if (note.column != 0)
        {
            pp_diagnostic_append(&diagnostic, "<scratch space>:1:%u: ", note.column);
        }
        else
        {
            pp_diagnostic_place(pp, &diagnostic, note.location);
        }
        if (note.origin != 0)
        {
            const IdentId name = pp->history[note.origin - 1].name;
            pp_diagnostic_append(&diagnostic, "note: expanded from macro '%.*s'\n", (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
        }
        else
        {
            pp_diagnostic_append(&diagnostic, "note: expanded from here\n");
        }
        // The ranges mapped into the note's expansion, as spellings there (emitSingleMacroExpansion). A generated pragma's
        // note is placed in scratch space, which shows no line here.
        PPDiagnosticRange note_ranges[BUSTER_PP_DIAGNOSTIC_RANGES];
        uint32_t note_range_count = 0;
        for (uint32_t r = 0; note.origin != 0 && pp->render_ranges != NULL && r < pp->render_range_count && r < BUSTER_PP_DIAGNOSTIC_RANGES; r += 1)
        {
            note_range_count += pp_diagnostic_map_range(pp, &pp->render_ranges[r], note.origin, &note_ranges[note_range_count]) ? 1 : 0;
        }
        pp_diagnostic_snippet(pp, &diagnostic, note.column != 0 ? 0 : note.location, true, note_ranges, note_range_count);
        pp_diagnostic_snippet_done(pp, note.location, true);
    }
    pp_diagnostic_snippet_done(pp, location, severity == PP_SEVERITY_NOTE);
    if (note != 0)
    {
        // An attached note's place is the include context the next primary compares with, as a separately rendered note's
        // (#1970): one in another header makes that primary repeat its include stack.
        pp->diagnostic_include_range = pp_presumed_include(pp, note->location);
        pp_diagnostic_place(pp, &diagnostic, note->location);
        if (note->macro != 0)
        {
            pp_diagnostic_append(&diagnostic, "note: macro '%.*s' defined here\n", (int)intern_length(pp->table, note->macro), intern_bytes(pp->table, note->macro));
        }
        else
        {
            pp_diagnostic_append(&diagnostic, "note: %s\n", note->message);
        }
        pp_diagnostic_snippet(pp, &diagnostic, note->location, true, NULL, 0);
        pp_diagnostic_snippet_done(pp, note->location, true);
    }
    pp_diagnostic_flush(&diagnostic);
    scratch_end(scratch);
}

// A cold source-command queue, allocated only for diagnostics. Messages and the optional explicit note are owned;
// origin indices keep the recording preprocessor alive until publication. No rendered include-stack state is saved:
// its suppression follows the final source order, not the order in which compiler stages discover reports.
static void pp_collect_diagnostic(Preprocessor* pp, PPSeverity severity, uint64_t position, uint32_t location, uint32_t origin, const PPDiagnosticNote* note, const PPOutputDiagnosticNote* source_notes, uint32_t source_note_count, const PPDiagnosticSourceNote* placed_notes, uint32_t placed_note_count, const char* format, va_list arguments)
{
    const bool failed_before = pp->diagnostic_collection_failed;
    va_list copy;
    va_copy(copy, arguments);
    const int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    const uint64_t note_bytes = note != NULL && note->message != NULL ? strlen(note->message) + 1u : 0;
    const uint32_t note_count = source_note_count + placed_note_count;
    uint64_t bytes = length >= 0 ? sizeof(PPDiagnosticRecord) + (uint64_t)(uint32_t)length + 1u + note_bytes + (uint64_t)note_count * sizeof(PPDiagnosticSourceNote) : 0;
    bool valid = length >= 0 && (source_note_count == 0 || source_notes != NULL) && (placed_note_count == 0 || placed_notes != NULL);
    for (uint32_t i = 0; valid && i < source_note_count; i += 1)
    {
        valid = source_notes[i].token < pp->output.count && source_notes[i].message != NULL;
        if (valid)
        {
            valid = !__builtin_add_overflow(bytes, (uint64_t)strlen(source_notes[i].message) + 1u, &bytes);
        }
    }
    for (uint32_t i = 0; valid && i < placed_note_count; i += 1)
    {
        valid = placed_notes[i].message != NULL && !__builtin_add_overflow(bytes, (uint64_t)strlen(placed_notes[i].message) + 1u, &bytes);
    }
    bytes = valid ? bytes : 0;
    pp_diagnostic_resolve_ranges(pp, false);
    PPDiagnosticRecord* record = bytes != 0 && !pp->diagnostic_collection_failed ? arena_push(&pp->arena, bytes, _Alignof(PPDiagnosticRecord)) : NULL;
    if (record != NULL)
    {
        PPDiagnosticSourceNote* owned_notes = (PPDiagnosticSourceNote*)(record + 1);
        char* message = (char*)(owned_notes + note_count);
        va_copy(copy, arguments);
        const int written = vsnprintf(message, (uint64_t)(uint32_t)length + 1u, format, copy);
        va_end(copy);
        pp->diagnostic_collection_failed = written != length;
        if (!pp->diagnostic_collection_failed)
        {
            *record = (PPDiagnosticRecord){
                .owner = pp,
                .position = position,
                .location = location,
                .origin = origin,
                .note_location = note != NULL ? note->location : 0,
                .note_macro = note != NULL ? note->macro : 0,
                .message = message,
                .note_message = note_bytes != 0 ? message + length + 1 : NULL,
                .source_notes = owned_notes,
                .source_note_count = note_count,
                .severity = (uint8_t)severity,
                .spans = pp->diagnostic_spans,
                .arguments = pp->diagnostic_arguments,
                .range_count = pp->diagnostic_range_count < BUSTER_PP_DIAGNOSTIC_RANGES ? pp->diagnostic_range_count : BUSTER_PP_DIAGNOSTIC_RANGES,
            };
            memcpy(record->ranges, pp->diagnostic_ranges, sizeof(record->ranges));
            if (note_bytes != 0)
            {
                memcpy((char*)record->note_message, note->message, note_bytes);
            }
            char* next = message + length + 1 + note_bytes;
            for (uint32_t i = 0; i < source_note_count; i += 1)
            {
                const uint32_t token = source_notes[i].token;
                const uint64_t size = strlen(source_notes[i].message) + 1u;
                owned_notes[i] = (PPDiagnosticSourceNote){ pp->output.locations[token], pp_history_at(pp, token), next };
                memcpy(next, source_notes[i].message, size);
                next += size;
            }
            for (uint32_t i = 0; i < placed_note_count; i += 1)
            {
                const uint64_t size = strlen(placed_notes[i].message) + 1u;
                owned_notes[source_note_count + i] = (PPDiagnosticSourceNote){ placed_notes[i].location, placed_notes[i].origin, next };
                memcpy(next, placed_notes[i].message, size);
                next += size;
            }
            if (pp->diagnostic_last != NULL)
            {
                pp->diagnostic_last->next = record;
            }
            else
            {
                pp->diagnostic_first = record;
            }
            pp->diagnostic_last = record;
        }
    }
    else
    {
        pp->diagnostic_collection_failed = true;
    }
    pp->fatal |= pp->diagnostic_collection_failed;
    pp->errors += pp->diagnostic_collection_failed && !failed_before ? 1u : 0u;
}

static void pp_diagnostic_take(Preprocessor* pp, Preprocessor* replay)
{
    if (replay->diagnostic_first != NULL)
    {
        if (pp->diagnostic_last != NULL)
        {
            pp->diagnostic_last->next = replay->diagnostic_first;
        }
        else
        {
            pp->diagnostic_first = replay->diagnostic_first;
        }
        pp->diagnostic_last = replay->diagnostic_last;
    }
    pp->diagnostic_collection_failed |= replay->diagnostic_collection_failed;
}

// Stable bottom-up merging needs no second allocation on the rejected-unit path. The order key is the expansion
// output boundary for preprocessing and the exact output token for parser/checker groups. A completed construct may
// refine that boundary by its own nonzero suborder; ordinary records follow it. Equal keys retain discovery order,
// including several directive reports before the next source token.
static PPDiagnosticRecord* pp_diagnostic_order(PPDiagnosticRecord* first)
{
    uint64_t width = 1;
    bool sorting = first != NULL;
    while (sorting)
    {
        PPDiagnosticRecord* cursor = first;
        PPDiagnosticRecord* last = NULL;
        first = NULL;
        uint64_t merges = 0;
        while (cursor != NULL)
        {
            merges += 1;
            PPDiagnosticRecord* left = cursor;
            uint64_t left_count = 0;
            for (; left_count < width && cursor != NULL; left_count += 1)
            {
                cursor = cursor->next;
            }
            PPDiagnosticRecord* right = cursor;
            uint64_t right_count = width;
            while (left_count != 0 || (right_count != 0 && right != NULL))
            {
                const uint64_t left_order = left != NULL && left->suborder != 0 ? left->suborder : UINT64_MAX;
                const uint64_t right_order = right != NULL && right->suborder != 0 ? right->suborder : UINT64_MAX;
                const bool take_left = left_count != 0 && (right_count == 0 || right == NULL || left->position < right->position ||
                                                         (left->position == right->position && left_order <= right_order));
                PPDiagnosticRecord* next = take_left ? left : right;
                left = take_left ? left->next : left;
                right = !take_left ? right->next : right;
                left_count -= take_left ? 1u : 0u;
                right_count -= !take_left ? 1u : 0u;
                if (last != NULL)
                {
                    last->next = next;
                }
                else
                {
                    first = next;
                }
                last = next;
            }
            cursor = right;
        }
        last->next = NULL;
        sorting = merges > 1;
        width *= 2;
    }
    return first;
}

static void pp_render_message(Preprocessor* pp, PPSeverity severity, uint32_t location, uint32_t origin, const PPDiagnosticNote* note, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_render_diagnostic(pp, severity, location, origin, note, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY void pp_diagnostic_publish(Preprocessor* pp)
{
    if (pp->options.collect_diagnostics)
    {
        pp->options.collect_diagnostics = false;
        pp->errors = 0;
        pp->warnings = 0;
        pp->fatal = false;
        pp->diagnostic_include_range = BUSTER_PP_NONE;
        pp_diagnostic_snippet_done(pp, 0, false);
        if (pp->diagnostic_collection_failed)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "diagnostic collection capacity exhausted");
        }
        else
        {
            pp->diagnostic_first = pp_diagnostic_order(pp->diagnostic_first);
            for (PPDiagnosticRecord* record = pp->diagnostic_first; record != NULL && !pp->fatal; record = record->next)
            {
                PPSeverity severity = (PPSeverity)record->severity;
                bool limited;
                const bool accepted = pp_diagnostic_begin(pp, &severity, &limited);
                if (accepted && !pp->options.quiet && limited)
                {
                    pp_diagnostic_limit(pp);
                }
                else if (accepted && !pp->options.quiet)
                {
                    Preprocessor* owner = record->owner;
                    const bool owner_fatal = owner->fatal;
                    owner->fatal = pp->fatal;
                    owner->diagnostic_include_range = pp->diagnostic_include_range;
                    owner->diagnostic_caret_location = pp->diagnostic_caret_location;
                    owner->diagnostic_caret_note = pp->diagnostic_caret_note;
                    const bool owner_arguments = owner->diagnostic_arguments;
                    owner->diagnostic_arguments = record->arguments;
                    const PPDiagnosticNote note = {
                        .location = record->note_location,
                        .message = record->note_message,
                        .macro = record->note_macro,
                    };
                    owner->render_spans = record->spans;
                    owner->render_ranges = record->ranges;
                    owner->render_range_count = record->range_count;
                    pp_render_message(owner, severity, record->location, record->origin,
                                      record->note_message != NULL || record->note_macro != 0 ? &note : NULL, "%s", record->message);
                    owner->render_spans = false;
                    owner->render_ranges = NULL;
                    owner->render_range_count = 0;
                    for (uint32_t i = 0; i < record->source_note_count; i += 1)
                    {
                        const PPDiagnosticSourceNote* source = &record->source_notes[i];
                        pp_render_message(owner, PP_SEVERITY_NOTE, source->location, source->origin, NULL, "%s", source->message);
                    }
                    pp->diagnostic_include_range = owner->diagnostic_include_range;
                    pp->diagnostic_caret_location = owner->diagnostic_caret_location;
                    pp->diagnostic_caret_note = owner->diagnostic_caret_note;
                    owner->diagnostic_arguments = owner_arguments;
                    owner->fatal = owner_fatal;
                }
            }
        }
        pp->diagnostic_first = NULL;
        pp->diagnostic_last = NULL;
    }
}

static __attribute__((format(printf, 5, 6))) void pp_diagnose_note(Preprocessor* pp, PPSeverity severity, uint32_t location, const PPDiagnosticNote* note, const char* format, ...)
{
    const bool always = severity == PP_SEVERITY_WARNING_ALWAYS;
    severity = always ? PP_SEVERITY_WARNING : severity;
    const PPSeverity requested = severity;
    const bool visible = severity != PP_SEVERITY_WARNING || always || pp_diagnostic_warning_visible(pp, location, location != 0 ? pp->diagnostic_origin : 0);
    bool limited = false;
    const bool accepted = visible && pp_diagnostic_begin(pp, &severity, &limited);
    if (accepted && !pp->options.quiet && limited)
    {
        pp_diagnostic_limit(pp);
    }
    else if (accepted && (!pp->options.quiet || (pp->options.collect_diagnostics && !pp->history_deferred)))
    {
        va_list arguments;
        va_start(arguments, format);
        const uint32_t position = pp->diagnostic_position != BUSTER_PP_NONE ? pp->diagnostic_position : pp->output.count;
        if (pp->options.collect_diagnostics)
        {
            pp_collect_diagnostic(pp, requested, (uint64_t)position * PP_DIAGNOSTIC_PHASE_COUNT + PP_DIAGNOSTIC_LEXICAL, location, location != 0 ? pp->diagnostic_origin : 0, note, NULL, 0, NULL, 0, format, arguments);
        }
        else
        {
            pp_diagnostic_resolve_ranges(pp, false);
            pp->render_ranges = pp->diagnostic_ranges;
            pp->render_range_count = pp->diagnostic_range_count;
            pp_render_diagnostic(pp, severity, location, location != 0 ? pp->diagnostic_origin : 0, note, format, arguments);
            pp->render_ranges = NULL;
            pp->render_range_count = 0;
        }
        va_end(arguments);
    }
}

static void pp_diagnose_output_arguments(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, PPDiagnosticPhase phase, const PPOutputDiagnosticNote* notes, uint32_t note_count, const char* format, va_list arguments)
{
    // The output's columns, which the backend may have given back (pp_release_output): the history this diagnostic records
    // anyway gives them back.
    pp_history_ensure(pp);
    pp_output_ensure(pp);
    pp_diagnose_output_location(pp, severity, token, token < pp->output.count ? pp->output.locations[token] : 0, position, phase, notes, note_count, format, arguments);
}

// Explicit source coordinates can name a consumed preprocessing token. The caller has ensured history, so both
// its origin and every output note's origin belong to the same replay. They publish as one diagnostic group.
static void pp_diagnose_source_location(Preprocessor* pp, PPSeverity severity, uint32_t location, uint32_t origin, uint32_t position, PPDiagnosticPhase phase,
                                        const PPOutputDiagnosticNote* notes, uint32_t note_count, const PPDiagnosticSourceNote* placed, uint32_t placed_count,
                                        const char* format, va_list arguments)
{
    // The notes' places are the output's (pp_release_output).
    pp_output_ensure(pp);
    const bool always = severity == PP_SEVERITY_WARNING_ALWAYS;
    // Its ranges' tokens are the output's too (#2376); its callers have ensured the history.
    pp_diagnostic_resolve_ranges(pp, true);
    severity = always ? PP_SEVERITY_WARNING : severity;
    const PPSeverity requested = severity;
    const bool visible = severity != PP_SEVERITY_WARNING || always || pp_diagnostic_warning_visible(pp, location, origin);
    bool limited = false;
    const bool accepted = visible && pp_diagnostic_begin(pp, &severity, &limited);
    if (accepted && !pp->options.quiet && limited)
    {
        pp_diagnostic_limit(pp);
    }
    else if (accepted && (!pp->options.quiet || (pp->options.collect_diagnostics && !pp->history_deferred)))
    {
        if (pp->options.collect_diagnostics)
        {
            pp_collect_diagnostic(pp, requested, (uint64_t)position * PP_DIAGNOSTIC_PHASE_COUNT + phase, location, origin, NULL, notes, note_count, placed, placed_count, format, arguments);
        }
        else
        {
            pp->render_spans = pp->diagnostic_spans;
            pp->render_ranges = pp->diagnostic_ranges;
            pp->render_range_count = pp->diagnostic_range_count;
            pp_render_diagnostic(pp, severity, location, origin, NULL, format, arguments);
            pp->render_spans = false;
            pp->render_ranges = NULL;
            pp->render_range_count = 0;
            for (uint32_t i = 0; notes != NULL && i < note_count; i += 1)
            {
                const uint32_t at = notes[i].token;
                pp_render_message(pp, PP_SEVERITY_NOTE, at < pp->output.count ? pp->output.locations[at] : 0, pp_history_at(pp, at), NULL, "%s", notes[i].message);
            }
            for (uint32_t i = 0; placed != NULL && i < placed_count; i += 1)
            {
                pp_render_message(pp, PP_SEVERITY_NOTE, placed[i].location, placed[i].origin, NULL, "%s", placed[i].message);
            }
        }
    }
}

// pp_diagnose_output_arguments at `location`, a place in the spelling of output token `token` (with its history).
static void pp_diagnose_output_location(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t location, uint32_t position, PPDiagnosticPhase phase,
                                        const PPOutputDiagnosticNote* notes, uint32_t note_count, const char* format, va_list arguments)
{
    pp_history_ensure(pp);
    pp_diagnose_source_location(pp, severity, location, pp_history_at(pp, token), position, phase, notes, note_count, NULL, 0, format, arguments);
}

static __attribute__((format(printf, 9, 10))) void pp_diagnose_source_notes_phase(Preprocessor* pp, PPSeverity severity, uint32_t location, uint32_t origin,
                                                                                uint32_t position, PPDiagnosticPhase phase, const PPOutputDiagnosticNote* notes,
                                                                                uint32_t note_count, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_source_location(pp, severity, location, origin, position, phase, notes, note_count, NULL, 0, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) bool pp_output_macro_place(Preprocessor* pp, uint32_t token, const char* name, uint32_t* location, uint32_t* origin)
{
    pp_history_ensure(pp);
    pp_output_ensure(pp);
    const uint32_t own_location = token < pp->output.count ? pp->output.locations[token] : 0;
    const uint32_t own_origin = token < pp->output.count ? pp_history_at(pp, token) : 0;
    bool lifted = false;
    uint32_t at = own_location;
    uint32_t cursor = own_origin;
    if (name == NULL && cursor != 0 && (cursor & ~BUSTER_PP_ORIGIN_INDEX) == 0)
    {
        const PPExpansion* expansion = &pp->history[(cursor & BUSTER_PP_ORIGIN_INDEX) - 1];
        at = expansion->begin;
        cursor = expansion->parent;
        lifted = true;
    }
    else if (name == NULL && cursor != 0 && (cursor & ~BUSTER_PP_ORIGIN_INDEX) == BUSTER_PP_ORIGIN_ARGUMENT)
    {
        // A macro argument's: its parameter in the macro's body, which that macro's expansion holds.
        const PPSubstitution* substitution = &pp->substitutions[(cursor & BUSTER_PP_ORIGIN_INDEX) - 1];
        at = substitution->parameter;
        cursor = substitution->expansion;
        lifted = substitution->expansion != 0;
    }
    else if (name != NULL)
    {
        // The outermost expansion's macro: the last expansion the chain passes, an argument's through its macro's expansion
        // (SourceManager's expansion location of an argument is its macro's).
        IdentId outer = 0;
        for (uint64_t steps = 0; cursor != 0 && steps < (uint64_t)pp->history_count + pp->substitution_count + pp->generated_count + 1; steps += 1)
        {
            const uint32_t index = (cursor & BUSTER_PP_ORIGIN_INDEX) - 1;
            const uint32_t kind = cursor & ~BUSTER_PP_ORIGIN_INDEX;
            if (kind == BUSTER_PP_ORIGIN_ARGUMENT)
            {
                const PPSubstitution* substitution = &pp->substitutions[index];
                at = substitution->parameter;
                cursor = substitution->expansion;
            }
            else if (kind == BUSTER_PP_ORIGIN_GENERATED)
            {
                at = pp->generated[index].anchor;
                cursor = pp->generated[index].expansion;
                outer = 0;
            }
            else
            {
                const PPExpansion* expansion = &pp->history[index];
                outer = expansion->builtin == PP_BUILTIN_NONE ? expansion->name : 0;
                at = expansion->begin;
                cursor = expansion->parent;
            }
        }
        const uint32_t length = (uint32_t)strlen(name);
        lifted = cursor == 0 && outer != 0 && intern_length(pp->table, outer) == length && memcmp(intern_bytes(pp->table, outer), name, length) == 0;
    }
    *location = lifted ? at : own_location;
    *origin = lifted ? cursor : own_origin;
    return lifted;
}

// pp_diagnose_output_notes_phase at an explicit place (pp_output_macro_place's).
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 9, 10))) void pp_diagnose_placed_notes_phase(Preprocessor* pp, PPSeverity severity, uint32_t location,
                                                                                                         uint32_t origin, uint32_t position, PPDiagnosticPhase phase,
                                                                                                         const PPOutputDiagnosticNote* notes, uint32_t note_count,
                                                                                                         const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_history_ensure(pp);
    pp_diagnose_source_location(pp, severity, location, origin, position, phase, notes, note_count, NULL, 0, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 6, 7))) void pp_diagnose_output_phase(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, PPDiagnosticPhase phase, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_output_arguments(pp, severity, token, position, phase, NULL, 0, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 5, 6))) void pp_diagnose_output_at(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_output_arguments(pp, severity, token, position, PP_DIAGNOSTIC_SEMANTIC, NULL, 0, format, arguments);
    va_end(arguments);
}

// The `offset`th character of a spelling after translation phases 1 and 2 (pp_spelling), in its source range.
// Both #if's temporary tokens and the checker's output tokens use this mapping; neither counts a splice as a byte.
static uint32_t pp_spelling_offset_location(Preprocessor* pp, uint32_t location, uint32_t offset)
{
    const uint32_t range = pp_find_range(pp, location);
    if (range != BUSTER_PP_NONE)
    {
        // The range is immutable throughout this scan; no helper can append or replace its storage here.
        const PPRange* range_bytes = &pp->ranges[range];
        const uint8_t* bytes = range_bytes->bytes;
        const uint32_t length = range_bytes->length;
        const uint32_t base = range_bytes->base;
        uint32_t at = location - base;
        for (uint32_t left = offset; left != 0 && at < length; left -= 1)
        {
            // Past the character, then past any splices after it: a backslash, perhaps spaces, and a line break.
            at += 1;
            bool spliced = true;
            while (spliced)
            {
                uint32_t after = at;
                spliced = at < length && bytes[at] == '\\';
                after += spliced;
                while (spliced && after < length && (bytes[after] == ' ' || bytes[after] == '\t'))
                {
                    after += 1;
                }
                const uint32_t line = after < length && bytes[after] == '\r' ? 1 : 0;
                spliced &= after + line < length && bytes[after + line] == '\n';
                at = spliced ? after + line + 1 : at;
            }
        }
        location = base + (at < length ? at : length);
    }
    return location;
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 6, 7))) void pp_diagnose_output_offset_at(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t offset,
                                                                                                           uint32_t position, const char* format, ...)
{
    pp_history_ensure(pp);
    pp_output_ensure(pp);
    const uint32_t location = token < pp->output.count ? pp_spelling_offset_location(pp, pp->output.locations[token], offset) : 0;
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_output_location(pp, severity, token, location, position, PP_DIAGNOSTIC_SEMANTIC, NULL, 0, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 8, 9))) void pp_diagnose_output_offset_notes(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t offset,
                                                                                                              uint32_t position, const PPOffsetNote* notes,
                                                                                                              uint32_t note_count, const char* format, ...)
{
    pp_history_ensure(pp);
    pp_output_ensure(pp);
    const uint32_t base = token < pp->output.count ? pp->output.locations[token] : 0;
    const uint32_t origin = pp_history_at(pp, token);
    const uint32_t location = base != 0 ? pp_spelling_offset_location(pp, base, offset) : 0;
    PPDiagnosticSourceNote placed[BUSTER_PP_PLACED_NOTES];
    const uint32_t count = note_count < BUSTER_PP_PLACED_NOTES ? note_count : BUSTER_PP_PLACED_NOTES;
    for (uint32_t i = 0; i < count; i += 1)
    {
        placed[i] = (PPDiagnosticSourceNote){ base != 0 ? pp_spelling_offset_location(pp, base, notes[i].offset) : 0, origin, notes[i].message };
    }
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_source_location(pp, severity, location, origin, position, PP_DIAGNOSTIC_SEMANTIC, NULL, 0, placed, count, format, arguments);
    va_end(arguments);
}

// pp_diagnose with notes at places of their own (`notes`, in the source as `location` is), a lexical diagnostic's (#1447:
// a named universal character name's in a character constant #if reads).
static __attribute__((format(printf, 6, 7))) void pp_diagnose_notes(Preprocessor* pp, PPSeverity severity, uint32_t location, const PPDiagnosticSourceNote* notes, uint32_t note_count,
                                                                   const char* format, ...)
{
    const PPSeverity requested = severity;
    const bool visible = severity != PP_SEVERITY_WARNING || pp_diagnostic_warning_visible(pp, location, location != 0 ? pp->diagnostic_origin : 0);
    bool limited = false;
    const bool accepted = visible && pp_diagnostic_begin(pp, &severity, &limited);
    if (accepted && !pp->options.quiet && limited)
    {
        pp_diagnostic_limit(pp);
    }
    else if (accepted && (!pp->options.quiet || (pp->options.collect_diagnostics && !pp->history_deferred)))
    {
        va_list arguments;
        va_start(arguments, format);
        const uint32_t position = pp->diagnostic_position != BUSTER_PP_NONE ? pp->diagnostic_position : pp->output.count;
        if (pp->options.collect_diagnostics)
        {
            pp_collect_diagnostic(pp, requested, (uint64_t)position * PP_DIAGNOSTIC_PHASE_COUNT + PP_DIAGNOSTIC_LEXICAL, location, location != 0 ? pp->diagnostic_origin : 0, NULL, NULL, 0, notes,
                                  note_count, format, arguments);
        }
        else
        {
            pp_diagnostic_resolve_ranges(pp, false);
            pp->render_ranges = pp->diagnostic_ranges;
            pp->render_range_count = pp->diagnostic_range_count;
            pp_render_diagnostic(pp, severity, location, location != 0 ? pp->diagnostic_origin : 0, NULL, format, arguments);
            pp->render_ranges = NULL;
            pp->render_range_count = 0;
            for (uint32_t i = 0; i < note_count; i += 1)
            {
                pp_render_message(pp, PP_SEVERITY_NOTE, notes[i].location, notes[i].origin, NULL, "%s", notes[i].message);
            }
        }
        va_end(arguments);
    }
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 4, 5))) void pp_diagnose_output(Preprocessor* pp, PPSeverity severity, uint32_t token, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_output_arguments(pp, severity, token, token, PP_DIAGNOSTIC_SEMANTIC, NULL, 0, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 7, 8))) void pp_diagnose_output_location_at(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t location,
                                                                                                          uint32_t position, PPDiagnosticPhase phase, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_output_location(pp, severity, token, location, position, phase, NULL, 0, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 7, 8))) void pp_diagnose_output_notes_at(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, const PPOutputDiagnosticNote* notes, uint32_t note_count, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_output_arguments(pp, severity, token, position, PP_DIAGNOSTIC_SEMANTIC, notes, note_count, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 9, 10))) void pp_diagnose_placed_sources_phase(Preprocessor* pp, PPSeverity severity, uint32_t location, uint32_t origin,
                                                                                                          uint32_t position, PPDiagnosticPhase phase, const PPDiagnosticSourceNote* placed,
                                                                                                          uint32_t placed_count, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_history_ensure(pp);
    pp_diagnose_source_location(pp, severity, location, origin, position, phase, NULL, 0, placed, placed_count, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 6, 7))) void pp_diagnose_unlocated_notes(Preprocessor* pp, PPSeverity severity, uint32_t position,
                                                                                                     const PPOutputDiagnosticNote* notes, uint32_t note_count,
                                                                                                     const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_history_ensure(pp);
    pp_diagnose_source_location(pp, severity, 0, 0, position, PP_DIAGNOSTIC_SEMANTIC, notes, note_count, NULL, 0, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 8, 9))) void pp_diagnose_output_notes_phase(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, PPDiagnosticPhase phase,
                                                                                                        const PPOutputDiagnosticNote* notes, uint32_t note_count, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_output_arguments(pp, severity, token, position, phase, notes, note_count, format, arguments);
    va_end(arguments);
}
