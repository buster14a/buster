// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Cold expansion history. Included by c_preprocessor.c after its state and diagnostic entry point.
// Origins in temporary token buffers are discarded with those buffers. The output keeps only eight-byte runs,
// built outside the token-copy kernels with adjacent comparisons and compression. Records and generated inputs
// live for the translation unit, including after a definition is replaced or an argument buffer is reclaimed.
// All links point to older records; diagnostics walk them iteratively, never through recursive calls.

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
        if (pp_history_room(pp, (void**)&pp->history, &pp->history_capacity, pp->history_count + 1, BUSTER_PP_MAX_HISTORY, sizeof(PPExpansion), BUSTER_ARENA_SMALL_BYTES))
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
        if (pp_history_room(pp, (void**)&pp->history_runs, &pp->history_run_capacity, count + 1, BUSTER_PP_MAX_OUTPUT_TOKENS + 1, sizeof(PPHistoryRun), BUSTER_ARENA_SMALL_BYTES))
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
        else if (BUSTER_VECTOR_PATH(pp->vectorized))
        {
            const __m512i value = _mm512_set1_epi32((int)origin);
            for (uint32_t i = begin; i < end; i += 16)
            {
                const __mmask16 valid = (__mmask16)_bzhi_u32(~0u, end - i < 16 ? end - i : 16);
                _mm512_mask_storeu_epi32(target->origins + i, valid, value);
            }
        }
        else
        {
            for (uint32_t i = begin; i < end; i += 1)
            {
                target->origins[i] = origin;
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
            if (BUSTER_VECTOR_PATH(pp->vectorized))
            {
                memmove(target->origins + at, source->origins + begin, (uint64_t)(end - begin) * sizeof(uint32_t));
            }
            else if (target == source && at > begin && at < end)
            {
                for (uint32_t i = end - begin; i > 0; i -= 1)
                {
                    target->origins[at + i - 1] = source->origins[begin + i - 1];
                }
            }
            else
            {
                for (uint32_t i = 0; i < end - begin; i += 1)
                {
                    target->origins[at + i] = source->origins[begin + i];
                }
            }
        }
        else if (BUSTER_VECTOR_PATH(pp->vectorized))
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
                if (added != 0 && pp_history_room(pp, (void**)&pp->history_runs, &pp->history_run_capacity, pp->history_run_count + added, BUSTER_PP_MAX_OUTPUT_TOKENS + 1, sizeof(PPHistoryRun), BUSTER_ARENA_SMALL_BYTES))
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
        else
        {
            uint32_t previous = ~source->origins[begin];
            for (uint32_t i = begin; i < end; i += 1)
            {
                const uint32_t origin = source->origins[i];
                if (origin != previous)
                {
                    pp_history_change(pp, at + i - begin, origin);
                    previous = origin;
                }
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
        if (pp_history_room(pp, (void**)&pp->substitutions, &pp->substitution_capacity, pp->substitution_count + 1, BUSTER_PP_MAX_HISTORY, sizeof(PPSubstitution), BUSTER_PP_TABLE_SMALL_BYTES))
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
        if (pp_history_room(pp, (void**)&pp->generated, &pp->generated_capacity, pp->generated_count + 1, BUSTER_PP_MAX_HISTORY, sizeof(PPGenerated), BUSTER_PP_TABLE_SMALL_BYTES) &&
            pp_history_room(pp, (void**)&pp->history_sources, &pp->history_source_capacity, pp->history_source_count + count, BUSTER_PP_MAX_HISTORY, sizeof(PPSource), BUSTER_ARENA_SMALL_BYTES))
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

static uint32_t pp_history_at(const Preprocessor* pp, uint32_t token)
{
    uint32_t low = 0;
    uint32_t high = pp->history_run_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (pp->history_runs[middle].token <= token)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return token < pp->output.count && low != 0 ? pp->history_runs[low - 1].origin : 0;
}

// Sixteen independent binary searches. Record loads are scalar on Zen 5; bounds/decisions run in vectors.
static __attribute__((unused)) void pp_history_lookup(const Preprocessor* pp, const uint32_t* tokens, uint32_t* origins, uint32_t count, bool scalar)
{
    (void)scalar;

    for (uint32_t i = 0; i < count; i += BUSTER_SCALAR_PATH(scalar) ? 1 : 16)
    {
        if (BUSTER_SCALAR_PATH(scalar))
        {
            origins[i] = pp_history_at(pp, tokens[i]);
        }
        else
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
}

// One logical diagnostic, including its include stack and notes, is rendered before its single fwrite. The usual
// diagnostic fits in the caller's stack buffer; exceptional long messages grow in temporary scratch. If scratch
// cannot grow, flush the complete prefix and keep printing directly, so memory exhaustion never drops or truncates
// a diagnostic. No FILE buffering mode changes, deferred output, or per-diagnostic heap allocation is needed.
#define BUSTER_PP_DIAGNOSTIC_BYTES 2048

typedef struct PPDiagnostic PPDiagnostic;
struct PPDiagnostic
{
    FILE* stream;
    Arena* arena;
    char* bytes;
    uint64_t count;
    uint64_t capacity;
    bool direct;
};

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

static void pp_diagnostic_includes(Preprocessor* pp, PPDiagnostic* diagnostic, uint32_t location)
{
    uint32_t chain[BUSTER_PP_MAX_INCLUDE_DEPTH + 1];
    uint32_t count = 0;
    uint32_t range = pp_find_range(pp, location);
    const bool changed = range != pp->diagnostic_include_range;
    pp->diagnostic_include_range = range;
    while (changed && range != BUSTER_PP_NONE && pp->ranges[range].parent != BUSTER_PP_NONE && count < BUSTER_ARRAY_LENGTH(chain))
    {
        chain[count++] = pp->ranges[range].include_location;
        range = pp->ranges[range].parent;
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
    PPSource* notes = origin != 0 ? arena_push_array(scratch.arena, capacity, sizeof(PPSource), 8) : 0;
    uint32_t count = 0;
    for (uint64_t steps = 0; notes != 0 && origin != 0 && steps < capacity + pp->substitution_count; steps += 1)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        if (kind == BUSTER_PP_ORIGIN_ARGUMENT)
        {
            // Forwarding alone is silent. If the substituted token is itself subsequently expanded, Clang also
            // shows the parameter through which the macro name arrived (CALL(F), where CALL(f) expands to f()).
            const PPSubstitution* substitution = &pp->substitutions[index];
            if (count != 0)
            {
                notes[count++] = (PPSource)
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
            notes[count++] = (PPSource)
            {
                .location = location,
                .origin = 0,
            };
            location = generated->anchor;
            origin = generated->expansion;
        }
        else
        {
            const PPExpansion* expansion = &pp->history[index];
            // Builtins already have their identity and invocation in this expansion record. Their spelling is the
            // token's location, so they need no duplicate PPGenerated record, just the scratch-space note here.
            notes[count++] = (PPSource)
            {
                .location = location,
                .origin = expansion->builtin == PP_BUILTIN_NONE ? origin : 0,
            };
            location = expansion->begin;
            origin = expansion->parent;
        }
    }
    pp_diagnostic_includes(pp, &diagnostic, location);
    pp_diagnostic_place(pp, &diagnostic, location);
    pp_diagnostic_append(&diagnostic, "%s: ", severity == PP_SEVERITY_WARNING ? "warning" : severity == PP_SEVERITY_ERROR ? "error" : "fatal error");
    pp_diagnostic_format(&diagnostic, format, arguments);
    pp_diagnostic_append(&diagnostic, "\n");
    for (uint32_t i = count; i > 0; i -= 1)
    {
        const PPSource note = notes[i - 1];
        pp_diagnostic_place(pp, &diagnostic, note.location);
        if (note.origin != 0)
        {
            const IdentId name = pp->history[note.origin - 1].name;
            pp_diagnostic_append(&diagnostic, "note: expanded from macro '%.*s'\n", (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
        }
        else
        {
            pp_diagnostic_append(&diagnostic, "note: expanded from here\n");
        }
    }
    if (note != 0 && !pp->fatal)
    {
        pp_diagnostic_place(pp, &diagnostic, note->location);
        if (note->macro != 0)
        {
            pp_diagnostic_append(&diagnostic, "note: macro '%.*s' defined here\n", (int)intern_length(pp->table, note->macro), intern_bytes(pp->table, note->macro));
        }
        else
        {
            pp_diagnostic_append(&diagnostic, "note: %s\n", note->message);
        }
    }
    pp_diagnostic_flush(&diagnostic);
    scratch_end(scratch);
}

// A cold source-command queue, allocated only for diagnostics. Messages and the optional explicit note are owned;
// origin indices keep the recording preprocessor alive until publication. No rendered include-stack state is saved:
// its suppression follows the final source order, not the order in which compiler stages discover reports.
static void pp_collect_diagnostic(Preprocessor* pp, PPSeverity severity, uint64_t position, uint32_t location, uint32_t origin, const PPDiagnosticNote* note, const char* format, va_list arguments)
{
    const bool failed_before = pp->diagnostic_collection_failed;
    va_list copy;
    va_copy(copy, arguments);
    const int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    const uint64_t note_bytes = note != NULL && note->message != NULL ? strlen(note->message) + 1u : 0;
    const uint64_t bytes = length >= 0 ? sizeof(PPDiagnosticRecord) + (uint64_t)(uint32_t)length + 1u + note_bytes : 0;
    PPDiagnosticRecord* record = bytes != 0 && !pp->diagnostic_collection_failed ? arena_push(&pp->arena, bytes, _Alignof(PPDiagnosticRecord)) : NULL;
    if (record != NULL)
    {
        char* message = (char*)(record + 1);
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
                .severity = (uint8_t)severity,
            };
            if (note_bytes != 0)
            {
                memcpy((char*)record->note_message, note->message, note_bytes);
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
// output boundary for preprocessing and the exact output token for parser/checker groups. Equal keys retain the
// discovery order, including several directive reports before the next source token.
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
                const bool take_left = left_count != 0 && (right_count == 0 || right == NULL || left->position <= right->position);
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
                    const PPDiagnosticNote note = {
                        .location = record->note_location,
                        .message = record->note_message,
                        .macro = record->note_macro,
                    };
                    pp_render_message(owner, severity, record->location, record->origin,
                                      record->note_message != NULL || record->note_macro != 0 ? &note : NULL, "%s", record->message);
                    pp->diagnostic_include_range = owner->diagnostic_include_range;
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
    bool limited;
    const bool accepted = pp_diagnostic_begin(pp, &severity, &limited);
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
            pp_collect_diagnostic(pp, severity, (uint64_t)position * 2, location, location != 0 ? pp->diagnostic_origin : 0, note, format, arguments);
        }
        else
        {
            pp_render_diagnostic(pp, severity, location, location != 0 ? pp->diagnostic_origin : 0, note, format, arguments);
        }
        va_end(arguments);
    }
}

static void pp_diagnose_output_arguments(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, const char* format, va_list arguments)
{
    pp_history_ensure(pp);
    bool limited;
    const bool accepted = pp_diagnostic_begin(pp, &severity, &limited);
    if (accepted && !pp->options.quiet && limited)
    {
        pp_diagnostic_limit(pp);
    }
    else if (accepted && (!pp->options.quiet || (pp->options.collect_diagnostics && !pp->history_deferred)))
    {
        if (pp->options.collect_diagnostics)
        {
            pp_collect_diagnostic(pp, severity, (uint64_t)position * 2 + 1u, token < pp->output.count ? pp->output.locations[token] : 0,
                                  pp_history_at(pp, token), NULL, format, arguments);
        }
        else
        {
            pp_render_diagnostic(pp, severity, token < pp->output.count ? pp->output.locations[token] : 0, pp_history_at(pp, token), NULL, format, arguments);
        }
    }
}

// The diagnostic location and its publication boundary can differ. A runtime warning, for example, completes
// after its function's closing brace and the preprocessor lookahead before the next source token. A boundary
// one past output.count is reserved for translation-unit final checks, after ordinary EOF completion groups.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 5, 6))) void pp_diagnose_output_at(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t position, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_output_arguments(pp, severity, token, position, format, arguments);
    va_end(arguments);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused, format(printf, 4, 5))) void pp_diagnose_output(Preprocessor* pp, PPSeverity severity, uint32_t token, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_diagnose_output_arguments(pp, severity, token, token, format, arguments);
    va_end(arguments);
}
