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

// The operand following a paste or stringification operator in its definition. The retained token starts skip splices;
// this runs only while recording expansion history, when the definition's columns are still live.
static uint32_t pp_history_paste_end(const Preprocessor* pp, uint32_t location, uint32_t fallback)
{
    const uint32_t range = location != 0 ? pp_find_range(pp, location) : BUSTER_PP_NONE;
    const uint32_t file = range != BUSTER_PP_NONE ? pp->ranges[range].file : BUSTER_PP_NONE;
    const PPFileData* data = file != BUSTER_PP_NONE ? &pp->datas[pp->files[file].data] : NULL;
    uint32_t result = fallback;
    if (data != NULL && data->starts != NULL)
    {
        const uint32_t offset = location - pp->ranges[range].base;
        uint32_t low = 0;
        uint32_t high = data->token_count;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            const bool before = data->starts[middle] <= offset;
            low = before ? middle + 1 : low;
            high = before ? high : middle;
        }
        while (low < data->token_count && pp_comment_token(data->bytes, data->kinds[low], data->starts[low]))
        {
            low += 1;
        }
        result = low < data->token_count ? pp->ranges[range].base + data->starts[low] : fallback;
    }
    return result;
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
                .end = kind != PP_GENERATED_PRAGMA ? pp_history_paste_end(pp, kind == PP_GENERATED_PASTE ? pp->history_anchor : anchor, anchor) : anchor,
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

// Replacement text on one source line, at the original display columns. Sorting this bounded set also preserves
// producer order for inserts at the same place. A multiline replacement is not representable on one insertion line.
static void pp_snippet_fixits(PPDiagnostic* diagnostic, const PPDiagnosticFixIt* fixits, uint32_t fixit_count,
                             const bool* kept, const uint32_t* begin_lines, const uint32_t* begin_bytes,
                             uint32_t line, uint32_t length, const uint32_t* columns, uint32_t gutter)
{
    uint32_t order[BUSTER_PP_DIAGNOSTIC_FIXITS];
    uint32_t count = 0;
    for (uint32_t f = 0; f < fixit_count; f += 1)
    {
        const char* text = fixits[f].text;
        const bool shown = kept[f] && begin_lines[f] == line && begin_bytes[f] <= length && text != NULL && text[0] != 0 &&
                           strchr(text, '\n') == NULL && strchr(text, '\r') == NULL;
        if (shown)
        {
            uint32_t at = count;
            while (at != 0 && begin_bytes[order[at - 1]] > begin_bytes[f])
            {
                order[at] = order[at - 1];
                at -= 1;
            }
            order[at] = f;
            count += 1;
        }
    }
    uint32_t column = 0;
    if (count != 0 && gutter != 0)
    {
        pp_diagnostic_append(diagnostic, "%*s| ", (int)(gutter + 2), "");
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t f = order[i];
        const uint32_t target = pp_snippet_containing_column(columns, begin_bytes[f]);
        const uint32_t spaces = target > column ? target - column : 0;
        pp_diagnostic_append(diagnostic, "%*s%s", (int)spaces, "", fixits[f].text);
        column += spaces;
        const uint8_t* text = (const uint8_t*)fixits[f].text;
        const uint64_t size = strlen(fixits[f].text);
        for (uint64_t at = 0; at < size;)
        {
            const uint8_t byte = text[at];
            const uint32_t claimed = byte < 0xC0 ? 1 : byte < 0xE0 ? 2 : byte < 0xF0 ? 3 : 4;
            const uint32_t point = claimed <= size - at ? pp_snippet_code_point(text + at, claimed) : UINT32_MAX;
            const int32_t width = point != UINT32_MAX && point >= 0x80 ? unicode_column_width(point) : 1;
            column += width >= 0 ? (uint32_t)width : 1;
            at += point != UINT32_MAX ? claimed : 1;
        }
    }
    if (count != 0)
    {
        pp_diagnostic_append(diagnostic, "\n");
    }
}

// The source lines and caret under a diagnostic's line at `location`, its printed place (or a note's, with `note`), as
// clang's emitSnippetAndCaret shows them, with `ranges` (`range_count`, mapped into its file: pp_diagnostic_map_range)
// marked with '~': none where carets are off, at no place or a place in scratch space, or at the last diagnostic's
// place again when this one has no range (but after a note where this one is no note), as clang's LastLoc rule says;
// none past the file's end or the limits above. The caret's line and as many of its ranges' lines around it as the
// snippet's line limit allows, each its physical line, the gutter numbered from the presumed one of the first.
static void pp_diagnostic_snippet(Preprocessor* pp, PPDiagnostic* diagnostic, uint32_t location, bool note, const PPDiagnosticRange* ranges, uint32_t range_count,
                                  const PPDiagnosticFixIt* fixits, uint32_t fixit_count)
{
    const bool repeated = location == pp->diagnostic_caret_location && range_count == 0 && fixit_count == 0 && (!pp->diagnostic_caret_note || note);
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
        uint32_t begin_lines[BUSTER_PP_DIAGNOSTIC_RANGES + BUSTER_PP_DIAGNOSTIC_FIXITS] = { 0 };
        uint32_t begin_bytes[BUSTER_PP_DIAGNOSTIC_RANGES + BUSTER_PP_DIAGNOSTIC_FIXITS] = { 0 };
        uint32_t end_lines[BUSTER_PP_DIAGNOSTIC_RANGES + BUSTER_PP_DIAGNOSTIC_FIXITS] = { 0 };
        uint32_t end_bytes[BUSTER_PP_DIAGNOSTIC_RANGES + BUSTER_PP_DIAGNOSTIC_FIXITS] = { 0 };
        bool kept[BUSTER_PP_DIAGNOSTIC_RANGES + BUSTER_PP_DIAGNOSTIC_FIXITS];
        const uint32_t highlights = range_count < BUSTER_PP_DIAGNOSTIC_RANGES ? range_count : BUSTER_PP_DIAGNOSTIC_RANGES;
        const uint32_t fixes = fixit_count < BUSTER_PP_DIAGNOSTIC_FIXITS ? fixit_count : BUSTER_PP_DIAGNOSTIC_FIXITS;
        const uint32_t count = highlights + fixes;
        for (uint32_t r = 0; r < count; r += 1)
        {
            const PPDiagnosticRange* span = r < highlights ? &ranges[r] : &fixits[r - highlights].range;
            kept[r] = span->begin != 0 && span->end != 0 && pp_find_range(pp, span->begin) == range && pp_find_range(pp, span->end) == range;
            if (kept[r])
            {
                uint32_t column;
                begin_lines[r] = pp_line_of(pp, range, span->begin, &column);
                begin_bytes[r] = column - 1;
                end_lines[r] = pp_line_of(pp, range, span->end, &column);
                end_bytes[r] = column - 1 + span->end_length;
                pp_snippet_add_lines(&first, &last, begin_lines[r], end_lines[r], most);
                const uint32_t begun = pp_presumed(pp, span->begin, &name, &name_length);
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
                pp_snippet_fixits(diagnostic, fixits, fixes, kept + highlights, begin_lines + highlights, begin_bytes + highlights,
                                  line, line_length, columns, gutter);
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
        result = (PPRangePlace){ begin ? pp->generated[index].anchor : pp->generated[index].end, pp->generated[index].expansion };
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
static bool pp_diagnostic_resolve_range(Preprocessor* pp, PPDiagnosticRange* range, bool resolve)
{
    const uint32_t count = resolve ? pp->output.count : 0;
    bool valid = true;
    if (range->pending == PP_RANGE_TOKENS || range->pending == PP_RANGE_INSERT_BEFORE || range->pending == PP_RANGE_INSERT_AFTER)
    {
        valid = range->begin <= range->end && range->end < count;
        if (valid)
        {
            const bool insertion = range->pending != PP_RANGE_TOKENS;
            const uint32_t offset = range->pending == PP_RANGE_INSERT_AFTER ? pp_diagnostic_token_length(pp, range->begin) : 0;
            *range = (PPDiagnosticRange)
            {
                .begin = pp->output.locations[range->begin] + offset,
                .begin_origin = pp_history_at(pp, range->begin),
                .end = pp->output.locations[range->end] + offset,
                .end_origin = pp_history_at(pp, range->end),
                .end_length = insertion ? 0 : pp_diagnostic_token_length(pp, range->end),
            };
        }
    }
    else if (range->pending == PP_RANGE_OFFSETS)
    {
        valid = range->begin < count && range->end_length != 0 && range->end <= UINT32_MAX - (range->end_length - 1);
        const uint32_t base = valid ? pp->output.locations[range->begin] : 0;
        const uint32_t origin = valid ? pp_history_at(pp, range->begin) : 0;
        *range = valid ? (PPDiagnosticRange)
        {
            .begin = pp_spelling_offset_location(pp, base, range->end),
            .begin_origin = origin,
            .end = pp_spelling_offset_location(pp, base, range->end + range->end_length - 1),
            .end_origin = origin,
            .end_length = 1,
        } : *range;
    }
    return valid;
}

static uint32_t pp_diagnostic_resolve_range_array(Preprocessor* pp, PPDiagnosticRange* result, const PPDiagnosticRange* ranges, uint32_t count, bool resolve)
{
    const uint32_t bounded = count < BUSTER_PP_DIAGNOSTIC_RANGES ? count : BUSTER_PP_DIAGNOSTIC_RANGES;
    uint32_t kept = 0;
    for (uint32_t r = 0; r < bounded; r += 1)
    {
        PPDiagnosticRange range = ranges[r];
        const bool valid = pp_diagnostic_resolve_range(pp, &range, resolve);
        result[kept] = range;
        kept += valid ? 1 : 0;
    }
    return kept;
}

static bool pp_fixit_origin_contains(const Preprocessor* pp, uint32_t origin, uint32_t wanted)
{
    uint32_t pending[BUSTER_PP_RANGE_DEPTH];
    uint32_t count = 0;
    bool found = false;
    bool truncated = false;
    for (uint32_t steps = 0; !found && (origin != 0 || count != 0) && steps < BUSTER_PP_RANGE_STEPS; steps += 1)
    {
        if (origin == 0)
        {
            origin = pending[--count];
        }
        found = origin == wanted;
        if (!found)
        {
            const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
            const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
            if (kind == BUSTER_PP_ORIGIN_ARGUMENT)
            {
                const PPSubstitution* substitution = &pp->substitutions[index];
                if (substitution->source != 0 && count < BUSTER_PP_RANGE_DEPTH)
                {
                    pending[count++] = substitution->source;
                }
                else if (substitution->source != 0)
                {
                    truncated = true;
                }
                origin = substitution->expansion;
            }
            else
            {
                origin = kind == BUSTER_PP_ORIGIN_GENERATED ? pp->generated[index].expansion : pp->history[index].parent;
            }
        }
    }
    return found || truncated || origin != 0 || count != 0;
}

// A complete expansion can be edited at its invocation's boundary. An interior token cannot: the adjacent output
// token still belongs to that expansion. Arguments instead retain the caller-written spelling, including forwarding.
static bool pp_fixit_file_endpoint(const Preprocessor* pp, PPRangePlace* place, uint32_t neighbour, bool end, uint32_t* end_length)
{
    bool valid = true;
    for (uint32_t depth = 0; valid && place->origin != 0 && depth < BUSTER_PP_RANGE_DEPTH; depth += 1)
    {
        if (pp_range_argument(*place))
        {
            const PPSubstitution* substitution = &pp->substitutions[(place->origin & BUSTER_PP_ORIGIN_INDEX) - 1];
            const bool boundary = substitution->expansion != 0 && !pp_fixit_origin_contains(pp, neighbour, substitution->expansion);
            *place = boundary ? pp_range_expansion(pp, *place, !end) : pp_range_spelling(pp, *place);
        }
        else
        {
            valid = !pp_fixit_origin_contains(pp, neighbour, place->origin);
            if (valid)
            {
                *place = pp_range_expansion(pp, *place, !end);
                *end_length = end ? pp_range_measure(pp, place->location) : *end_length;
            }
        }
    }
    return valid && place->origin == 0;
}

// Fix-its edit real file characters. Preserve argument spellings and complete expansion boundaries, and reject
// interior macro-body edits. An unusable after-token location creates no insertion; an otherwise formed hint that
// cannot map to a file rejects the diagnostic's hint group, as Clang does. This never replays preprocessing.
static uint32_t pp_diagnostic_resolve_fixits(Preprocessor* pp, PPDiagnosticFixIt* result, const PPDiagnosticFixIt* fixits, uint32_t count, bool resolve)
{
    const uint32_t bounded = count < BUSTER_PP_DIAGNOSTIC_FIXITS ? count : BUSTER_PP_DIAGNOSTIC_FIXITS;
    uint32_t kept = 0;
    bool applicable = true;
    for (uint32_t f = 0; f < bounded; f += 1)
    {
        PPDiagnosticFixIt fixit = fixits[f];
        const uint32_t kind = fixit.range.pending;
        const uint32_t first = fixit.range.begin;
        const uint32_t last = fixit.range.end;
        bool valid = fixit.text != NULL && pp_diagnostic_resolve_range(pp, &fixit.range, resolve);
        const bool insertion = kind == PP_RANGE_INSERT_BEFORE || kind == PP_RANGE_INSERT_AFTER;
        const bool tokens = kind == PP_RANGE_TOKENS || insertion;
        if (valid && tokens)
        {
            const uint32_t before = first != 0 ? pp_history_at(pp, first - 1) : 0;
            const uint32_t after = last + 1 < pp->output.count ? pp_history_at(pp, last + 1) : 0;
            PPRangePlace begin = { fixit.range.begin, fixit.range.begin_origin };
            PPRangePlace end = { fixit.range.end, fixit.range.end_origin };
            uint32_t ignored = 0;
            if (insertion)
            {
                const bool at_end = kind == PP_RANGE_INSERT_AFTER;
                uint32_t moved_length = 0;
                valid = pp_fixit_file_endpoint(pp, &begin, at_end ? after : before, at_end, &moved_length);
                begin.location += moved_length;
                end = begin;
            }
            else
            {
                valid = pp_fixit_file_endpoint(pp, &begin, before, false, &ignored) &&
                        pp_fixit_file_endpoint(pp, &end, after, true, &fixit.range.end_length);
            }
            fixit.range.begin = begin.location;
            fixit.range.end = end.location;
            fixit.range.begin_origin = begin.origin;
            fixit.range.end_origin = end.origin;
        }
        valid = valid && fixit.range.begin_origin == 0 && fixit.range.end_origin == 0;
        const uint32_t begin_range = valid && fixit.range.begin != 0 ? pp_find_range(pp, fixit.range.begin) : BUSTER_PP_NONE;
        const uint32_t end_range = valid && fixit.range.end != 0 ? pp_find_range(pp, fixit.range.end) : BUSTER_PP_NONE;
        valid = valid && begin_range != BUSTER_PP_NONE && begin_range == end_range && pp->ranges[begin_range].file != BUSTER_PP_NONE &&
                fixit.range.begin <= fixit.range.end && fixit.range.end - pp->ranges[end_range].base <= pp->ranges[end_range].length &&
                fixit.range.end_length <= pp->ranges[end_range].length - (fixit.range.end - pp->ranges[end_range].base);
        fixit.range.begin_origin = 0;
        fixit.range.end_origin = 0;
        result[kept] = fixit;
        kept += valid ? 1 : 0;
        applicable &= valid || kind == PP_RANGE_INSERT_AFTER;
    }
    return applicable ? kept : 0;
}

static void pp_diagnostic_resolve_ranges(Preprocessor* pp, bool resolve)
{
    pp->diagnostic_range_count = pp_diagnostic_resolve_range_array(pp, pp->diagnostic_ranges, pp->diagnostic_ranges, pp->diagnostic_range_count, resolve);
    pp->diagnostic_fixit_count = pp_diagnostic_resolve_fixits(pp, pp->diagnostic_fixits, pp->diagnostic_fixits, pp->diagnostic_fixit_count, resolve);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_ranges_clear(Preprocessor* pp)
{
    pp->diagnostic_range_count = 0;
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_fixit_tokens(Preprocessor* pp, uint32_t first, uint32_t last, const char* text)
{
    if (pp->diagnostic_fixit_count < BUSTER_PP_DIAGNOSTIC_FIXITS && first <= last && text != NULL)
    {
        pp->diagnostic_fixits[pp->diagnostic_fixit_count++] = (PPDiagnosticFixIt)
        {
            .range = { .begin = first, .end = last, .pending = PP_RANGE_TOKENS },
            .text = text,
        };
    }
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_fixit_insert(Preprocessor* pp, uint32_t token, bool after, const char* text)
{
    if (pp->diagnostic_fixit_count < BUSTER_PP_DIAGNOSTIC_FIXITS && text != NULL)
    {
        pp->diagnostic_fixits[pp->diagnostic_fixit_count++] = (PPDiagnosticFixIt)
        {
            .range = { .begin = token, .end = token, .pending = after ? PP_RANGE_INSERT_AFTER : PP_RANGE_INSERT_BEFORE },
            .text = text,
        };
    }
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_fixit_characters(Preprocessor* pp, uint32_t location, uint32_t length, const char* text)
{
    if (pp->diagnostic_fixit_count < BUSTER_PP_DIAGNOSTIC_FIXITS && location != 0 && text != NULL)
    {
        pp->diagnostic_fixits[pp->diagnostic_fixit_count++] = (PPDiagnosticFixIt)
        {
            .range = { .begin = location, .end = location, .end_length = length },
            .text = text,
        };
    }
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_fixits_clear(Preprocessor* pp)
{
    pp->diagnostic_fixit_count = 0;
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

typedef struct PPScratchSnippet PPScratchSnippet;
struct PPScratchSnippet
{
    const uint8_t* bytes;
    uint32_t location;
    uint32_t length;
    uint32_t line;
    uint32_t column;
    uint32_t padding;
};

// A generated spelling is NUL-terminated inside a scratch chunk. Isolate its caret's physical line: neighboring
// spellings in that chunk are unrelated source. Clang prepends one space before a destringized _Pragma's first line.
static PPScratchSnippet pp_diagnostic_scratch_view(const Preprocessor* pp, const PPDiagnosticSource* note)
{
    PPScratchSnippet result = { 0 };
    const uint32_t range = note->scratch != 0 ? pp_find_range(pp, note->scratch) : BUSTER_PP_NONE;
    const bool same = range != BUSTER_PP_NONE && pp->ranges[range].file == BUSTER_PP_NONE &&
                      note->location >= note->scratch && pp_find_range(pp, note->location) == range;
    if (same)
    {
        const PPRange* source = &pp->ranges[range];
        const uint32_t offset = note->scratch - source->base;
        const uint8_t* bytes = source->bytes + offset;
        const uint32_t available = source->length - offset;
        const uint8_t* terminator = memchr(bytes, 0, available);
        const uint32_t length = terminator != NULL ? (uint32_t)(terminator - bytes) : 0;
        const uint32_t caret = note->location - note->scratch;
        uint32_t start = 0;
        uint32_t line = 1;
        bool found = false;
        while (terminator != NULL && !found && start <= caret && start <= length)
        {
            const uint8_t* lf = memchr(bytes + start, '\n', length - start);
            const uint8_t* cr = memchr(bytes + start, '\r', length - start);
            const uint8_t* end = lf == NULL ? cr : cr == NULL || lf < cr ? lf : cr;
            const uint32_t stop = end != NULL ? (uint32_t)(end - bytes) : length;
            found = caret <= stop;
            if (found)
            {
                const uint32_t padding = note->pragma && start == 0 ? 1 : 0;
                result = (PPScratchSnippet)
                {
                    .bytes = bytes + start,
                    .location = note->scratch + start,
                    .length = stop - start,
                    .line = line,
                    .column = caret - start + padding + 1,
                    .padding = padding,
                };
            }
            else
            {
                const bool pair = stop + 1 < length && (bytes[stop + 1] == '\n' || bytes[stop + 1] == '\r') && bytes[stop + 1] != bytes[stop];
                start = stop + 1 + pair;
                line += 1;
            }
        }
    }
    return result;
}

// Scratch-space line numbers depend on allocation order, so they remain local to each generated spelling. Their
// excerpts, columns, character widths and range marks use the same renderer as real source lines.
static void pp_diagnostic_scratch_snippet(Preprocessor* pp, PPDiagnostic* diagnostic, const PPDiagnosticSource* note,
                                          const PPScratchSnippet* snippet, const PPDiagnosticRange* ranges, uint32_t range_count)
{
    const bool repeated = note->location == pp->diagnostic_caret_location && range_count == 0;
    const uint32_t length = snippet->length + snippet->padding;
    if (pp->options.carets && !repeated && snippet->bytes != NULL && length <= BUSTER_PP_SNIPPET_BYTES && snippet->column <= BUSTER_PP_SNIPPET_BYTES)
    {
        uint8_t* line = arena_push(diagnostic->arena, (uint64_t)length + 1, 1);
        uint32_t* columns = arena_push_array(diagnostic->arena, (uint64_t)length + 1, sizeof(uint32_t), _Alignof(uint32_t));
        if (line != NULL && columns != NULL)
        {
            memset(line, ' ', snippet->padding);
            memcpy(line + snippet->padding, snippet->bytes, snippet->length);
            const uint32_t most = pp->options.caret_lines_given ? pp->options.caret_lines : BUSTER_PP_SNIPPET_LINES;
            const uint32_t widest = pp_snippet_digits(snippet->line + most);
            const uint32_t gutter = pp->options.no_line_numbers ? 0 : widest > BUSTER_PP_SNIPPET_GUTTER ? widest : BUSTER_PP_SNIPPET_GUTTER;
            if (gutter != 0)
            {
                const uint32_t digits = pp_snippet_digits(snippet->line);
                pp_diagnostic_append(diagnostic, "%*s%u | ", (int)(gutter + 1 > digits ? gutter + 1 - digits : 0), "", snippet->line);
            }
            const uint32_t width = pp_snippet_line(diagnostic, line, length, columns);
            char* marks = arena_push(diagnostic->arena, (uint64_t)width + 2, 1);
            uint32_t marked = 0;
            for (uint32_t r = 0; marks != NULL && r < range_count; r += 1)
            {
                const PPDiagnosticRange* range = &ranges[r];
                const bool on = range->begin >= snippet->location && range->end >= range->begin &&
                                (uint64_t)range->end + range->end_length <= (uint64_t)snippet->location + snippet->length;
                if (on)
                {
                    const PPSnippetLineRange part =
                    {
                        .start = range->begin - snippet->location + snippet->padding,
                        .end = range->end - snippet->location + range->end_length + snippet->padding,
                    };
                    pp_snippet_highlight(&part, line, length, columns, marks, &marked);
                }
            }
            if (marks != NULL)
            {
                const uint32_t caret = pp_snippet_containing_column(columns, snippet->column - 1);
                memset(marks + marked, ' ', caret > marked ? caret - marked : 0);
                marks[caret] = '^';
                marked = caret + 1 > marked ? caret + 1 : marked;
                if (gutter != 0)
                {
                    pp_diagnostic_append(diagnostic, "%*s| ", (int)(gutter + 2), "");
                }
                pp_diagnostic_append(diagnostic, "%.*s\n", (int)marked, marks);
            }
        }
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
            // so it does where a diagnostic's range spans more than the argument's one token (render_spans), or an
            // edit gives the argument's context. Character ranges within one literal alone keep forwarding silent.
            const PPSubstitution* substitution = &pp->substitutions[index];
            if (count != 0 || pp->render_spans || pp->diagnostic_arguments || pp->render_fixit_count != 0)
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
                .scratch = generated->location,
                .pragma = generated->kind == PP_GENERATED_PRAGMA,
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
                .scratch = expansion->builtin != PP_BUILTIN_NONE ? location : 0,
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
    pp_diagnostic_snippet(pp, &diagnostic, location, severity == PP_SEVERITY_NOTE, mapped, mapped_count, pp->render_fixits, pp->render_fixit_count);
    for (uint32_t i = count; i > 0; i -= 1)
    {
        const PPDiagnosticSource note = notes[i - 1];
        const PPScratchSnippet snippet = pp_diagnostic_scratch_view(pp, &note);
        if (snippet.bytes != NULL)
        {
            pp_diagnostic_append(&diagnostic, "<scratch space>:%u:%u: ", snippet.line, snippet.column);
        }
        else if (note.column != 0)
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
        // The ranges mapped into the note's expansion or its generated spelling (emitSingleMacroExpansion).
        PPDiagnosticRange note_ranges[BUSTER_PP_DIAGNOSTIC_RANGES];
        uint32_t note_range_count = 0;
        const uint64_t scope = note.origin != 0 ? note.origin : snippet.bytes != NULL ? ((uint64_t)1 << 32) | pp_find_range(pp, note.location) : 0;
        for (uint32_t r = 0; scope != 0 && pp->render_ranges != NULL && r < pp->render_range_count && r < BUSTER_PP_DIAGNOSTIC_RANGES; r += 1)
        {
            note_range_count += pp_diagnostic_map_range(pp, &pp->render_ranges[r], scope, &note_ranges[note_range_count]) ? 1 : 0;
        }
        if (snippet.bytes != NULL)
        {
            pp_diagnostic_scratch_snippet(pp, &diagnostic, &note, &snippet, note_ranges, note_range_count);
        }
        else
        {
            pp_diagnostic_snippet(pp, &diagnostic, note.location, true, note_ranges, note_range_count, NULL, 0);
        }
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
        pp_diagnostic_snippet(pp, &diagnostic, note->location, true, note->ranges, note->range_count, note->fixits, note->fixit_count);
        pp_diagnostic_snippet_done(pp, note->location, true);
    }
    pp_diagnostic_flush(&diagnostic);
    scratch_end(scratch);
}

static bool pp_diagnostic_fixit_bytes(const PPDiagnosticFixIt* fixits, uint32_t count, uint64_t* bytes)
{
    bool valid = count <= BUSTER_PP_DIAGNOSTIC_FIXITS;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        valid = fixits[i].text != NULL && !__builtin_add_overflow(*bytes, (uint64_t)strlen(fixits[i].text) + 1u, bytes);
    }
    return valid;
}

// A queued diagnostic outlives its producer's scratch strings. Each hint owns its text in the same allocation as
// the message, including notes' independent replacements. Resolution never asks for history from this lexical path.
static uint32_t pp_diagnostic_own_fixits(Preprocessor* pp, PPDiagnosticFixIt* result, const PPDiagnosticFixIt* fixits, uint32_t count, char** next, bool resolve)
{
    const uint32_t kept = pp_diagnostic_resolve_fixits(pp, result, fixits, count, resolve);
    for (uint32_t f = 0; f < kept; f += 1)
    {
        const uint64_t size = strlen(result[f].text) + 1u;
        memcpy(*next, result[f].text, size);
        result[f].text = *next;
        *next += size;
    }
    return kept;
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
    bool valid = length >= 0 && note_count >= source_note_count && (source_note_count == 0 || source_notes != NULL) && (placed_note_count == 0 || placed_notes != NULL) &&
                 pp_diagnostic_fixit_bytes(pp->diagnostic_fixits, pp->diagnostic_fixit_count, &bytes) &&
                 (note == NULL || pp_diagnostic_fixit_bytes(note->fixits, note->fixit_count, &bytes));
    for (uint32_t i = 0; valid && i < source_note_count; i += 1)
    {
        valid = source_notes[i].token < pp->output.count && source_notes[i].message != NULL;
        if (valid)
        {
            valid = !__builtin_add_overflow(bytes, (uint64_t)strlen(source_notes[i].message) + 1u, &bytes) &&
                    pp_diagnostic_fixit_bytes(source_notes[i].fixits, source_notes[i].fixit_count, &bytes);
        }
    }
    for (uint32_t i = 0; valid && i < placed_note_count; i += 1)
    {
        valid = placed_notes[i].message != NULL && !__builtin_add_overflow(bytes, (uint64_t)strlen(placed_notes[i].message) + 1u, &bytes) &&
                pp_diagnostic_fixit_bytes(placed_notes[i].fixits, placed_notes[i].fixit_count, &bytes);
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
                .line_notes = pp->diagnostic_line_notes,
                .range_count = pp->diagnostic_range_count < BUSTER_PP_DIAGNOSTIC_RANGES ? pp->diagnostic_range_count : BUSTER_PP_DIAGNOSTIC_RANGES,
            };
            memcpy(record->ranges, pp->diagnostic_ranges, sizeof(record->ranges));
            if (note_bytes != 0)
            {
                memcpy((char*)record->note_message, note->message, note_bytes);
            }
            char* next = message + length + 1 + note_bytes;
            record->fixit_count = pp_diagnostic_own_fixits(pp, record->fixits, pp->diagnostic_fixits, pp->diagnostic_fixit_count, &next, false);
            if (note != NULL)
            {
                record->note_range_count = pp_diagnostic_resolve_range_array(pp, record->note_ranges, note->ranges, note->range_count, false);
                record->note_fixit_count = pp_diagnostic_own_fixits(pp, record->note_fixits, note->fixits, note->fixit_count, &next, false);
            }
            for (uint32_t i = 0; i < source_note_count; i += 1)
            {
                const uint32_t token = source_notes[i].token;
                const uint64_t size = strlen(source_notes[i].message) + 1u;
                const uint32_t location = pp->output.locations[token] + (source_notes[i].after_token ? pp_diagnostic_token_length(pp, token) : 0);
                owned_notes[i] = (PPDiagnosticSourceNote){ location, pp_history_at(pp, token), next };
                memcpy(next, source_notes[i].message, size);
                next += size;
                owned_notes[i].range_count = pp_diagnostic_resolve_range_array(pp, owned_notes[i].ranges, source_notes[i].ranges, source_notes[i].range_count, true);
                owned_notes[i].fixit_count = pp_diagnostic_own_fixits(pp, owned_notes[i].fixits, source_notes[i].fixits, source_notes[i].fixit_count, &next, true);
            }
            for (uint32_t i = 0; i < placed_note_count; i += 1)
            {
                const uint64_t size = strlen(placed_notes[i].message) + 1u;
                owned_notes[source_note_count + i] = (PPDiagnosticSourceNote){ placed_notes[i].location, placed_notes[i].origin, next };
                memcpy(next, placed_notes[i].message, size);
                next += size;
                owned_notes[source_note_count + i].range_count = pp_diagnostic_resolve_range_array(pp, owned_notes[source_note_count + i].ranges, placed_notes[i].ranges, placed_notes[i].range_count, false);
                owned_notes[source_note_count + i].fixit_count = pp_diagnostic_own_fixits(pp, owned_notes[source_note_count + i].fixits, placed_notes[i].fixits, placed_notes[i].fixit_count, &next, false);
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

// The sorted records with each source-ordered one (pp_diagnostic_source_order) moved among the others at its position,
// which keep their sorted order: before the first whose token, or where clang queues it (pp_diagnostic_queued), follows
// its own, else after them all (#2252). The sort put the source-ordered ones in token order (their suborders); a record
// of no token (zero) follows none. One pass over the records, which only a unit with such a record takes.
static PPDiagnosticRecord* pp_diagnostic_source_merge(PPDiagnosticRecord* first)
{
    PPDiagnosticRecord* result = NULL;
    PPDiagnosticRecord* last = NULL;
    PPDiagnosticRecord* cursor = first;
    while (cursor != NULL)
    {
        // A run of one position, split into its source-ordered records and the others, each in its sorted order.
        const uint64_t position = cursor->position;
        PPDiagnosticRecord* ordered = NULL;
        PPDiagnosticRecord* ordered_last = NULL;
        PPDiagnosticRecord* others = NULL;
        PPDiagnosticRecord* others_last = NULL;
        while (cursor != NULL && cursor->position == position)
        {
            PPDiagnosticRecord* next = cursor->next;
            cursor->next = NULL;
            if (cursor->source_ordered)
            {
                if (ordered_last != NULL)
                {
                    ordered_last->next = cursor;
                }
                else
                {
                    ordered = cursor;
                }
                ordered_last = cursor;
            }
            else
            {
                if (others_last != NULL)
                {
                    others_last->next = cursor;
                }
                else
                {
                    others = cursor;
                }
                others_last = cursor;
            }
            cursor = next;
        }
        while (ordered != NULL || others != NULL)
        {
            const bool take_ordered = ordered != NULL && (others == NULL || ordered->token < others->token);
            PPDiagnosticRecord* taken = take_ordered ? ordered : others;
            ordered = take_ordered ? ordered->next : ordered;
            others = !take_ordered ? others->next : others;
            if (last != NULL)
            {
                last->next = taken;
            }
            else
            {
                result = taken;
            }
            last = taken;
        }
    }
    if (last != NULL)
    {
        last->next = NULL;
    }
    return result;
}

static void pp_render_message(Preprocessor* pp, PPSeverity severity, uint32_t location, uint32_t origin, const PPDiagnosticNote* note, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_render_diagnostic(pp, severity, location, origin, note, format, arguments);
    va_end(arguments);
}

static void pp_render_attached_note(Preprocessor* pp, uint32_t location, uint32_t origin, const char* message,
                                   const PPDiagnosticRange* ranges, uint32_t range_count, const PPDiagnosticFixIt* fixits, uint32_t fixit_count, bool resolve)
{
    PPDiagnosticRange resolved_ranges[BUSTER_PP_DIAGNOSTIC_RANGES];
    PPDiagnosticFixIt resolved_fixits[BUSTER_PP_DIAGNOSTIC_FIXITS];
    pp->render_range_count = pp_diagnostic_resolve_range_array(pp, resolved_ranges, ranges, range_count, resolve);
    pp->render_fixit_count = pp_diagnostic_resolve_fixits(pp, resolved_fixits, fixits, fixit_count, resolve);
    pp->render_ranges = resolved_ranges;
    pp->render_fixits = resolved_fixits;
    pp_render_message(pp, PP_SEVERITY_NOTE, location, origin, NULL, "%s", message);
    pp->render_ranges = NULL;
    pp->render_range_count = 0;
    pp->render_fixits = NULL;
    pp->render_fixit_count = 0;
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
            if (pp->diagnostic_source_ordered)
            {
                pp->diagnostic_first = pp_diagnostic_source_merge(pp->diagnostic_first);
            }
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
                    PPDiagnosticNote note = {
                        .location = record->note_location,
                        .message = record->note_message,
                        .macro = record->note_macro,
                        .range_count = record->note_range_count,
                        .fixit_count = record->note_fixit_count,
                    };
                    memcpy(note.ranges, record->note_ranges, sizeof(note.ranges));
                    memcpy(note.fixits, record->note_fixits, sizeof(note.fixits));
                    owner->render_spans = record->spans;
                    owner->render_ranges = record->ranges;
                    owner->render_range_count = record->range_count;
                    owner->render_fixits = record->fixits;
                    owner->render_fixit_count = record->fixit_count;
                    owner->presumed_note_limit = record->line_notes;
                    pp_render_message(owner, severity, record->location, record->origin,
                                      record->note_message != NULL || record->note_macro != 0 ? &note : NULL, "%s", record->message);
                    owner->presumed_note_limit = 0;
                    owner->render_spans = false;
                    owner->render_ranges = NULL;
                    owner->render_range_count = 0;
                    owner->render_fixits = NULL;
                    owner->render_fixit_count = 0;
                    for (uint32_t i = 0; i < record->source_note_count; i += 1)
                    {
                        const PPDiagnosticSourceNote* source = &record->source_notes[i];
                        pp_render_attached_note(owner, source->location, source->origin, source->message, source->ranges, source->range_count, source->fixits, source->fixit_count, false);
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
        pp->diagnostic_source_ordered = false;
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
            pp->render_fixits = pp->diagnostic_fixits;
            pp->render_fixit_count = pp->diagnostic_fixit_count;
            pp_render_diagnostic(pp, severity, location, location != 0 ? pp->diagnostic_origin : 0, note, format, arguments);
            pp->render_ranges = NULL;
            pp->render_range_count = 0;
            pp->render_fixits = NULL;
            pp->render_fixit_count = 0;
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
            pp->render_fixits = pp->diagnostic_fixits;
            pp->render_fixit_count = pp->diagnostic_fixit_count;
            pp_render_diagnostic(pp, severity, location, origin, NULL, format, arguments);
            pp->render_spans = false;
            pp->render_ranges = NULL;
            pp->render_range_count = 0;
            pp->render_fixits = NULL;
            pp->render_fixit_count = 0;
            for (uint32_t i = 0; notes != NULL && i < note_count; i += 1)
            {
                const uint32_t at = notes[i].token;
                const uint32_t note_location = at < pp->output.count ? pp->output.locations[at] + (notes[i].after_token ? pp_diagnostic_token_length(pp, at) : 0) : 0;
                pp_render_attached_note(pp, note_location, pp_history_at(pp, at), notes[i].message,
                                        notes[i].ranges, notes[i].range_count, notes[i].fixits, notes[i].fixit_count, true);
            }
            for (uint32_t i = 0; placed != NULL && i < placed_count; i += 1)
            {
                pp_render_attached_note(pp, placed[i].location, placed[i].origin, placed[i].message,
                                        placed[i].ranges, placed[i].range_count, placed[i].fixits, placed[i].fixit_count, false);
            }
        }
    }
}

// pp_diagnose_output_arguments at `location`, a place in the spelling of output token `token` (with its history).
static void pp_diagnose_output_location(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t location, uint32_t position, PPDiagnosticPhase phase,
                                        const PPOutputDiagnosticNote* notes, uint32_t note_count, const char* format, va_list arguments)
{
    pp_history_ensure(pp);
    PPDiagnosticRecord* previous = pp->diagnostic_last;
    pp_diagnose_source_location(pp, severity, location, pp_history_at(pp, token), position, phase, notes, note_count, NULL, 0, format, arguments);
    // Its collected record's token, which orders a source-ordered record among its position's (pp_diagnostic_source_merge).
    if (pp->diagnostic_last != previous)
    {
        pp->diagnostic_last->token = token + 1;
    }
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
            pp->render_fixits = pp->diagnostic_fixits;
            pp->render_fixit_count = pp->diagnostic_fixit_count;
            pp_render_diagnostic(pp, severity, location, location != 0 ? pp->diagnostic_origin : 0, NULL, format, arguments);
            pp->render_ranges = NULL;
            pp->render_range_count = 0;
            pp->render_fixits = NULL;
            pp->render_fixit_count = 0;
            for (uint32_t i = 0; i < note_count; i += 1)
            {
                pp_render_attached_note(pp, notes[i].location, notes[i].origin, notes[i].message,
                                        notes[i].ranges, notes[i].range_count, notes[i].fixits, notes[i].fixit_count, false);
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
