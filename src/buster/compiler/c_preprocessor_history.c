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

// Grow once from the shared small slice to a table's full virtual reservation. Origins are indices, not pointers,
// and no caller retains a record pointer across an append, so moving the initial records preserves every link.
static bool pp_history_grow(Preprocessor* pp, void** records, uint32_t* capacity, uint32_t needed, uint32_t maximum, uint32_t size)
{
    bool result = needed <= *capacity;
    if (!result && needed <= maximum)
    {
        void* grown = pp_array(pp, maximum, size);
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

BUSTER_INLINE bool pp_history_room(Preprocessor* pp, void** records, uint32_t* capacity, uint32_t needed, uint32_t maximum, uint32_t size)
{
    return needed <= *capacity || pp_history_grow(pp, records, capacity, needed, maximum, size);
}

static bool pp_history_origin_grow(Preprocessor* pp, PPTokens* tokens, uint32_t needed)
{
    bool result = needed <= tokens->origin_capacity;
    if (!result)
    {
        const uint64_t bytes = ((uint64_t)tokens->capacity + BUSTER_PP_TOKEN_SLACK) * sizeof(uint32_t);
        tokens->origin_arena = arena_reserve(bytes);
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
        if (pp_history_room(pp, (void**)&pp->history, &pp->history_capacity, pp->history_count + 1, BUSTER_PP_MAX_HISTORY, sizeof(PPExpansion)))
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
        if (pp_history_room(pp, (void**)&pp->history_runs, &pp->history_run_capacity, count + 1, BUSTER_PP_MAX_OUTPUT_TOKENS + 1, sizeof(PPHistoryRun)))
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
        else if (pp->vectorized)
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

// Copy history alongside a completed token copy, or compress it directly into the output run table.
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
        else if (pp->vectorized)
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
                if (added != 0 && pp_history_room(pp, (void**)&pp->history_runs, &pp->history_run_capacity, pp->history_run_count + added, BUSTER_PP_MAX_OUTPUT_TOKENS + 1, sizeof(PPHistoryRun)))
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
        if (pp_history_room(pp, (void**)&pp->substitutions, &pp->substitution_capacity, pp->substitution_count + 1, BUSTER_PP_MAX_HISTORY, sizeof(PPSubstitution)))
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
        if (pp_history_room(pp, (void**)&pp->generated, &pp->generated_capacity, pp->generated_count + 1, BUSTER_PP_MAX_HISTORY, sizeof(PPGenerated)) &&
            pp_history_room(pp, (void**)&pp->history_sources, &pp->history_source_capacity, pp->history_source_count + count, BUSTER_PP_MAX_HISTORY, sizeof(PPSource)))
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
    for (uint32_t i = 0; i < count; i += scalar ? 1 : 16)
    {
        if (scalar)
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

static void pp_diagnostic_place(Preprocessor* pp, FILE* stream, uint32_t location)
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
        fprintf(stream, "%.*s:%u:%u: ", (int)length, name, line, column);
    }
    else if (location != 0)
    {
        fprintf(stream, "<scratch space>:1:1: ");
    }
}

static void pp_diagnostic_includes(Preprocessor* pp, FILE* stream, uint32_t location)
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
        fprintf(stream, "In file included from %.*s:%u:\n", (int)length, name, line);
    }
}

static void pp_render_diagnostic(Preprocessor* pp, PPSeverity severity, uint32_t location, uint32_t origin, const char* format, va_list arguments)
{
    FILE* stream = pp->options.diagnostics != 0 ? pp->options.diagnostics : stderr;
    Scratch scratch = scratch_begin(0);
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
    pp_diagnostic_includes(pp, stream, location);
    pp_diagnostic_place(pp, stream, location);
    fprintf(stream, "%s: ", severity == PP_SEVERITY_WARNING ? "warning" : severity == PP_SEVERITY_ERROR ? "error" : "fatal error");
    vfprintf(stream, format, arguments);
    fputc('\n', stream);
    for (uint32_t i = count; i > 0; i -= 1)
    {
        const PPSource note = notes[i - 1];
        pp_diagnostic_place(pp, stream, note.location);
        if (note.origin != 0)
        {
            const IdentId name = pp->history[note.origin - 1].name;
            fprintf(stream, "note: expanded from macro '%.*s'\n", (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
        }
        else
        {
            fprintf(stream, "note: expanded from here\n");
        }
    }
    scratch_end(scratch);
}

static __attribute__((unused, format(printf, 4, 5))) void pp_diagnose_output(Preprocessor* pp, PPSeverity severity, uint32_t token, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    pp_render_diagnostic(pp, severity, token < pp->output.count ? pp->output.locations[token] : 0, pp_history_at(pp, token), format, arguments);
    va_end(arguments);
    pp->warnings += severity == PP_SEVERITY_WARNING;
    pp->errors += severity != PP_SEVERITY_WARNING;
    pp->fatal |= severity == PP_SEVERITY_FATAL;
}
