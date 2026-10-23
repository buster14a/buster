// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Header transcript experiment for #111. One serial batch owns this cache;
// options, environment, source files and its FileCache stay immutable, while
// its intern table only grows. Read identities are exact file/directive pairs.
// Tokens, nested file entries, scratch spellings, macro effects and provenance
// replay in source order. Pragmas, repaired lexical views, unsupported builtins
// and optional cache exhaustion fall back to normal execution. This is a probe.

#define BUSTER_REPLAY_ITEMS 32768u
#define BUSTER_REPLAY_TRANSCRIPTS 4096u
#define BUSTER_REPLAY_VARIANTS 64u

typedef struct ReplayLocation ReplayLocation;
struct ReplayLocation
{
    uint32_t source;
    uint32_t offset;
};

typedef struct ReplayRead ReplayRead;
struct ReplayRead
{
    IdentId name;
    uint64_t key;
    uint32_t base;
    uint32_t first;
    uint32_t last;
};

typedef struct ReplayExpansion ReplayExpansion;
struct ReplayExpansion
{
    PPExpansion expansion;
    ReplayLocation definition;
    ReplayLocation begin;
    ReplayLocation end;
};

typedef struct ReplayStart ReplayStart;
struct ReplayStart
{
    uint32_t token;
    ReplayLocation location;
};

typedef struct ReplayWrite ReplayWrite;
struct ReplayWrite
{
    IdentId name;
    bool defined;
    PPMacro macro;
    uint32_t range;
};

typedef struct ReplayFile ReplayFile;
struct ReplayFile
{
    uint32_t file;
    uint32_t record;
    const char* path;
    uint32_t length;
    uint8_t system;
    bool included;
    bool guarded;
    bool initial_guarded;
    bool was_present;
    uint8_t initial_system;
};

typedef struct ReplayRange ReplayRange;
struct ReplayRange
{
    uint32_t file;
    uint32_t parent;
    uint32_t length;
    ReplayLocation include_location;
};

typedef struct ReplayScratch ReplayScratch;
struct ReplayScratch
{
    uint32_t location;
    uint32_t length;
    bool clean;
    const uint8_t* bytes;
};

typedef struct ReplayEvent ReplayEvent;
struct ReplayEvent
{
    uint32_t index;
    bool scratch;
};

typedef struct ReplayGenerated ReplayGenerated;
struct ReplayGenerated
{
    PPGenerated generated;
    ReplayLocation location;
    ReplayLocation anchor;
};

typedef struct ReplaySource ReplaySource;
struct ReplaySource
{
    PPSource source;
    ReplayLocation location;
};

typedef struct ReplaySubstitution ReplaySubstitution;
struct ReplaySubstitution
{
    PPSubstitution substitution;
    ReplayLocation parameter;
};

typedef struct ReplayTranscript ReplayTranscript;
struct ReplayTranscript
{
    uint32_t next;
    uint32_t record;
    uint32_t read_count;
    uint32_t write_count;
    ReplayRead* reads;
    ReplayWrite* writes;
    uint32_t old_base;
    bool guarded;
    uint32_t output_count;
    uint8_t* kinds;
    uint8_t* flags;
    uint32_t* values;
    ReplayLocation* locations;
    ReplayLocation* ends;
    PPHistoryRun* origins;
    uint32_t origin_count;
    uint32_t history_begin;
    uint32_t history_count;
    ReplayExpansion* history;
    uint32_t substitution_begin;
    uint32_t substitution_count;
    ReplaySubstitution* substitutions;
    uint32_t start_count;
    ReplayStart* starts;
    uint32_t range_count;
    ReplayRange* ranges;
    uint32_t file_count;
    ReplayFile* files;
    uint32_t search_index;
    uint32_t depth;
    uint32_t scratch_count;
    ReplayScratch* scratch;
    uint32_t event_count;
    ReplayEvent* events;
    uint32_t generated_begin;
    uint32_t generated_count;
    ReplayGenerated* generated;
    uint32_t source_begin;
    uint32_t source_count;
    ReplaySource* sources;
    bool expansion_changed;
    ReplayLocation expansion_location;
};

struct PPReplayCache
{
    Arena retained;
    Arena temporary;
    Arena seen_arena;
    Arena heads_arena;
    uint32_t* seen;
    uint32_t* heads;
    ReplayRead* reads;
    ReplayWrite* writes;
    ReplayTranscript* transcripts;
    uint32_t count;
    uint32_t generation;
    Preprocessor* owner;
    uint32_t context;
    uint32_t output;
    uint32_t errors;
    uint32_t warnings;
    uint32_t lines;
    uint32_t embeds;
    uint32_t read_count;
    uint32_t write_count;
    uint32_t history;
    uint32_t substitutions;
    uint32_t generated;
    uint32_t sources;
    uint32_t starts;
    uint32_t silent;
    uint32_t last_start;
    uint32_t expansion_location;
    uint32_t range_begin;
    ReplayFile* files;
    uint32_t file_count;
    uint32_t scratch_count;
    ReplayScratch* scratch;
    uint32_t event_count;
    ReplayEvent* events;
    uint32_t file_begin;
    uint8_t* file_system;
    uint64_t hits;
    uint64_t misses;
    uint64_t checks;
    uint64_t recorded_reads;
    uint64_t recorded_writes;
    uint64_t rejected;
    uint64_t why[12];
    bool scalar_copy;
};

static bool replay_initialize(PPReplayCache* cache)
{
    *cache = (PPReplayCache){ 0 };
    cache->retained = arena_reserve(256ull << 20);
    cache->temporary = arena_reserve(8ull << 20);
    cache->seen_arena = arena_reserve((uint64_t)BUSTER_INTERN_MAX_IDS * sizeof(uint32_t));
    cache->heads_arena = arena_reserve((uint64_t)BUSTER_PP_MAX_FILES * sizeof(uint32_t));
    cache->seen = arena_push_array(&cache->seen_arena, BUSTER_INTERN_MAX_IDS, sizeof(uint32_t), 64);
    cache->heads = arena_push_array(&cache->heads_arena, BUSTER_PP_MAX_FILES, sizeof(uint32_t), 64);
    cache->reads = arena_push_array(&cache->temporary, BUSTER_REPLAY_ITEMS, sizeof(ReplayRead), 64);
    cache->writes = arena_push_array(&cache->temporary, BUSTER_REPLAY_ITEMS, sizeof(ReplayWrite), 64);
    cache->transcripts = arena_push_array(&cache->retained, BUSTER_REPLAY_TRANSCRIPTS, sizeof(ReplayTranscript), 64);
    cache->files = arena_push_array(&cache->temporary, 4096, sizeof(ReplayFile), 64);
    cache->scratch = arena_push_array(&cache->temporary, BUSTER_REPLAY_ITEMS, sizeof(ReplayScratch), 64);
    cache->events = arena_push_array(&cache->temporary, BUSTER_REPLAY_ITEMS, sizeof(ReplayEvent), 64);
    cache->file_system = arena_push_array(&cache->temporary, BUSTER_PP_MAX_FILES, sizeof(uint8_t), 64);
    return cache->seen != 0 && cache->heads != 0 && cache->reads != 0 && cache->writes != 0 && cache->transcripts != 0 && cache->files != 0 && cache->scratch != 0 && cache->events != 0 && cache->file_system != 0;
}

static void replay_release(PPReplayCache* cache)
{
    if (cache->owner != 0)
    {
        cache->owner->replay_recording = false;
    }
    arena_release(&cache->retained);
    arena_release(&cache->temporary);
    arena_release(&cache->seen_arena);
    arena_release(&cache->heads_arena);
    *cache = (PPReplayCache){ 0 };
}

// Exact defining-file/directive identity, no hash. Virtual prelude files have
// fixed contents under this cache's immutable-options contract. UINT64_MAX
// refuses private repaired views and pragma-modified definitions.
static uint64_t replay_key(const Preprocessor* pp, IdentId name)
{
    const PPMacroId id = pp->macro_of[name];
    uint64_t result = 0;
    if (id != 0)
    {
        const PPMacro* macro = &pp->macros[id];
        if ((macro->flags & BUSTER_PP_MACRO_REDEFINABLE) != 0)
        {
            result = UINT64_MAX;
        }
        else if ((macro->flags & BUSTER_PP_MACRO_BUILTIN) != 0)
        {
            result = (0xffffffffull << 32) | macro->builtin;
        }
        else
        {
            const PPFileData* data = &pp->datas[macro->data];
            const uint32_t record = data->record;
            const uint32_t source = data->lexical_bytes != 0 ? 0 : record != BUSTER_PP_NONE ? record + 1 :
                                    macro->data == pp->predefines_data ? 0xfffffffeu : macro->data == pp->replay_command_data ? 0xfffffffdu : 0;
            result = source != 0 ? ((uint64_t)source << 32) | (macro->location - macro->base) : UINT64_MAX;
        }
    }
    return result;
}

static void pp_replay_forbid(Preprocessor* pp)
{
    PPReplayCache* cache = pp->options.replay;
    if (cache != 0 && cache->owner == pp)
    {
        pp->replay_recording = false;
        cache->owner = 0;
        cache->rejected += 1;
    }
}

static void pp_replay_enter(Preprocessor* pp, uint32_t file)
{
    PPReplayCache* cache = pp->options.replay;
    if (cache != 0 && cache->owner == pp)
    {
        const PPFile* path = &pp->files[file];
        const PPFileData* data = &pp->datas[path->data];
        uint32_t found = 0;
        while (found < cache->file_count && cache->files[found].file != file)
        {
            found += 1;
        }
        if (data->record == BUSTER_PP_NONE || data->once || cache->file_count == 4096)
        {
            pp_replay_forbid(pp);
        }
        else if (found == cache->file_count)
        {
            cache->files[cache->file_count++] = (ReplayFile){ .file = file, .record = data->record, .path = path->path, .length = path->path_length,
                                                          .system = path->system, .initial_guarded = data->guarded, .was_present = file < cache->file_begin,
                                                          .initial_system = file < cache->file_begin ? cache->file_system[file] : 0 };
        }
    }
}

static void pp_replay_scratch(Preprocessor* pp, uint32_t location, uint32_t length, bool clean)
{
    PPReplayCache* cache = pp->options.replay;
    if (cache != 0 && cache->owner == pp)
    {
        if (location != 0 && cache->scratch_count < BUSTER_REPLAY_ITEMS && cache->event_count < BUSTER_REPLAY_ITEMS)
        {
            cache->events[cache->event_count++] = (ReplayEvent){ .index = cache->scratch_count, .scratch = true };
            cache->scratch[cache->scratch_count++] = (ReplayScratch){ .location = location, .length = length, .clean = clean };
        }
        else
        {
            pp_replay_forbid(pp);
        }
    }
}

static void pp_replay_read(const Preprocessor* pp, IdentId name)
{
    PPReplayCache* cache = pp->options.replay;
    if (cache != 0 && cache->owner == pp && cache->seen[name] != cache->generation)
    {
        const uint64_t key = replay_key(pp, name);
        if (key != UINT64_MAX && cache->read_count < BUSTER_REPLAY_ITEMS)
        {
            cache->seen[name] = cache->generation;
            ReplayRead read = { .name = name, .key = key };
            const uint32_t macro = pp->macro_of[name];
            if (macro != 0 && !(pp->macros[macro].flags & BUSTER_PP_MACRO_BUILTIN))
            {
                const PPMacro* definition = &pp->macros[macro];
                const PPFileData* data = &pp->datas[definition->data];
                read.base = definition->base;
                read.first = definition->location - definition->base;
                read.last = definition->body_end > definition->body_begin ? pp_file_token_end(pp, data, definition->body_end - 1) : read.first;
            }
            cache->reads[cache->read_count++] = read;
        }
        else
        {
            cache->owner->replay_recording = false;
            cache->owner = 0;
            cache->rejected += 1;
        }
    }
}

static void pp_replay_write(Preprocessor* pp, IdentId name, PPMacroId id)
{
    PPReplayCache* cache = pp->options.replay;
    if (cache != 0 && cache->owner == pp)
    {
        pp_replay_read(pp, name);
        if (cache->owner == pp && cache->write_count < BUSTER_REPLAY_ITEMS &&
            (id == 0 || (pp->datas[pp->macros[id].data].record != BUSTER_PP_NONE && pp->datas[pp->macros[id].data].lexical_bytes == 0 &&
                         pp->macros[id].base >= pp->ranges[cache->range_begin].base)))
        {
            cache->writes[cache->write_count++] = (ReplayWrite){ .name = name, .defined = id != 0, .macro = pp->macros[id] };
        }
        else
        {
            pp_replay_forbid(pp);
        }
    }
}

static bool replay_context_ok(const Preprocessor* pp)
{
    return pp->context_count != 0 && pp->contexts[pp->context_count - 1].kind == BUSTER_PP_CONTEXT_FILE && pp->invocation_count == 0 &&
           pp->capture_count == 0 && pp->stream_count == 0 && pp->limit_count == 0 && pp->poison_count == 0 && !pp->include_limit_reached && !pp->options.no_history &&
           pp->region_locations[0] == 0 && pp->region_locations[1] == 0 && pp->region_locations[2] == 0;
}

static void pp_replay_begin(Preprocessor* pp, PPContext* context)
{
    PPReplayCache* cache = pp->options.replay;
    if (cache != 0 && cache->owner == pp)
    {
        for (uint32_t f = 0; f < cache->file_count; f += 1)
        {
            cache->files[f].included |= cache->files[f].file == context->file;
        }
        if (cache->event_count < BUSTER_REPLAY_ITEMS)
        {
            cache->events[cache->event_count++] = (ReplayEvent){ .index = context->range - cache->range_begin };
        }
        else
        {
            pp_replay_forbid(pp);
        }
    }
    if (cache != 0 && cache->owner == 0 && cache->count < BUSTER_REPLAY_TRANSCRIPTS && pp->ranges[context->range].parent != BUSTER_PP_NONE && replay_context_ok(pp) &&
        pp->datas[context->data].record != BUSTER_PP_NONE && cache->generation != UINT32_MAX && pp->history_current == 0)
    {
        cache->generation += 1;
        cache->owner = pp;
        pp->replay_recording = true;
        cache->context = pp->context_count;
        cache->output = pp->output.count;
        cache->errors = pp->errors;
        cache->warnings = pp->warnings;
        cache->lines = pp->line_directive_count;
        cache->embeds = pp->embed_count;
        cache->read_count = 0;
        cache->write_count = 0;
        cache->history = pp->history_count;
        cache->substitutions = pp->substitution_count;
        cache->generated = pp->generated_count;
        cache->sources = pp->history_source_count;
        cache->starts = pp->expansion_start_count;
        cache->last_start = cache->starts != 0 && pp->expansion_starts[cache->starts - 1].token == pp->output.count ? pp->expansion_starts[cache->starts - 1].location : 0;
        cache->expansion_location = pp->expansion_location;
        cache->silent = pp->silent_pragma_count;
        cache->range_begin = context->range;
        cache->file_begin = pp->file_count;
        for (uint32_t f = 0; f < pp->file_count; f += 1)
        {
            cache->file_system[f] = pp->files[f].system;
        }
        cache->file_count = 0;
        cache->scratch_count = 0;
        cache->event_count = 1;
        cache->events[0] = (ReplayEvent){ .index = 0 };
        pp_replay_enter(pp, context->file);
        if (cache->owner == pp && cache->file_count != 0)
        {
            cache->files[0].included = true;
        }
    }
}

static ReplayLocation replay_save_location(const Preprocessor* pp, const PPContext* context, uint32_t location, bool* valid)
{
    const PPReplayCache* cache = pp->options.replay;
    ReplayLocation result = { .source = UINT32_MAX, .offset = 0 };
    if (location == 0)
    {
        result.source = UINT32_MAX - 1;
    }
    else if (location == pp->zero_location || location == pp->zero_location + 1)
    {
        result.source = UINT32_MAX - 2;
        result.offset = location - pp->zero_location;
    }
    else if (location == pp->one_location || location == pp->one_location + 1)
    {
        result.source = UINT32_MAX - 3;
        result.offset = location - pp->one_location;
    }
    else
    {
        const uint32_t range = pp_find_range(pp, location);
        if (range != BUSTER_PP_NONE && range >= cache->range_begin && pp->ranges[range].file != BUSTER_PP_NONE)
        {
            result.source = range - cache->range_begin;
            result.offset = location - pp->ranges[range].base;
        }
        for (uint32_t i = 0; result.source == UINT32_MAX && i < cache->scratch_count; i += 1)
        {
            const ReplayScratch scratch = cache->scratch[i];
            if (location >= scratch.location && location - scratch.location <= scratch.length)
            {
                result.source = 0x40000000u | (i + 1);
                result.offset = location - scratch.location;
            }
        }
        for (uint32_t i = 0; result.source == UINT32_MAX && i < cache->read_count; i += 1)
        {
            const ReplayRead* read = &cache->reads[i];
            if (read->key != 0 && read->last >= read->first && location >= read->base && location - read->base >= read->first && location - read->base <= read->last)
            {
                result.source = 0x80000000u | (i + 1);
                result.offset = location - read->base;
            }
        }
    }
    if (result.source == UINT32_MAX)
    {
        pp->options.replay->why[0] += 1;
    }
    *valid &= result.source != UINT32_MAX;
    (void)context;
    return result;
}

static bool replay_closed_origin(uint32_t origin, const ReplayTranscript* transcript)
{
    const uint32_t index = origin & BUSTER_PP_ORIGIN_INDEX;
    const uint32_t tag = origin & ~BUSTER_PP_ORIGIN_INDEX;
    return origin == 0 || (tag == 0 && index > transcript->history_begin && index - transcript->history_begin <= transcript->history_count) ||
           (tag == BUSTER_PP_ORIGIN_ARGUMENT && index > transcript->substitution_begin && index - transcript->substitution_begin <= transcript->substitution_count) ||
           (tag == BUSTER_PP_ORIGIN_GENERATED && index > transcript->generated_begin && index - transcript->generated_begin <= transcript->generated_count);
}

static bool replay_capture(Preprocessor* pp, const PPContext* context, ReplayTranscript* transcript)
{
    PPReplayCache* cache = pp->options.replay;
    Arena* arena = &cache->retained;
    const uint32_t count = pp->output.count - cache->output;
    transcript->output_count = count;
    transcript->kinds = arena_push_array(arena, count, sizeof(uint8_t), 8);
    transcript->flags = arena_push_array(arena, count, sizeof(uint8_t), 8);
    transcript->values = arena_push_array(arena, count, sizeof(uint32_t), 8);
    transcript->locations = arena_push_array(arena, count, sizeof(ReplayLocation), 8);
    transcript->ends = arena_push_array(arena, count, sizeof(ReplayLocation), 8);
    uint32_t origin_count = count != 0;
    for (uint32_t r = 0; r < pp->history_run_count; r += 1)
    {
        origin_count += pp->history_runs[r].token > cache->output && pp->history_runs[r].token < pp->output.count;
    }
    transcript->origins = arena_push_array(arena, origin_count, sizeof(PPHistoryRun), 8);
    transcript->origin_count = origin_count;
    transcript->history_begin = cache->history;
    transcript->history_count = pp->history_count - cache->history;
    transcript->history = arena_push_array(arena, transcript->history_count, sizeof(ReplayExpansion), 8);
    transcript->substitution_begin = cache->substitutions;
    transcript->substitution_count = pp->substitution_count - cache->substitutions;
    transcript->substitutions = arena_push_array(arena, transcript->substitution_count, sizeof(ReplaySubstitution), 8);
    const uint32_t start_begin = cache->starts - (cache->last_start != 0 && pp->expansion_starts[cache->starts - 1].location != cache->last_start);
    transcript->start_count = pp->expansion_start_count - start_begin;
    transcript->starts = arena_push_array(arena, transcript->start_count, sizeof(ReplayStart), 8);
    transcript->range_count = pp->range_count - cache->range_begin;
    transcript->ranges = arena_push_array(arena, transcript->range_count, sizeof(ReplayRange), 8);
    transcript->file_count = cache->file_count;
    transcript->files = arena_push_array(arena, transcript->file_count, sizeof(ReplayFile), 8);
    transcript->scratch_count = cache->scratch_count;
    transcript->scratch = arena_push_array(arena, transcript->scratch_count, sizeof(ReplayScratch), 8);
    transcript->event_count = cache->event_count;
    transcript->events = arena_push_array(arena, transcript->event_count, sizeof(ReplayEvent), 8);
    transcript->generated_begin = cache->generated;
    transcript->generated_count = pp->generated_count - cache->generated;
    transcript->generated = arena_push_array(arena, transcript->generated_count, sizeof(ReplayGenerated), 8);
    transcript->source_begin = cache->sources;
    transcript->source_count = pp->history_source_count - cache->sources;
    transcript->sources = arena_push_array(arena, transcript->source_count, sizeof(ReplaySource), 8);
    transcript->search_index = context->search_index;
    transcript->depth = 0;
    for (uint32_t range = context->range; pp->ranges[range].parent != BUSTER_PP_NONE; range = pp->ranges[range].parent)
    {
        transcript->depth += 1;
    }
    bool result = transcript->kinds != 0 && transcript->flags != 0 && transcript->values != 0 && transcript->locations != 0 && transcript->ends != 0 &&
                  transcript->origins != 0 && transcript->history != 0 && transcript->substitutions != 0 && transcript->starts != 0 && transcript->ranges != 0 && transcript->files != 0 &&
                  transcript->scratch != 0 && transcript->events != 0 && transcript->generated != 0 && transcript->sources != 0;
    transcript->expansion_changed = pp->expansion_location != cache->expansion_location;
    if (transcript->expansion_changed)
    {
        transcript->expansion_location = replay_save_location(pp, context, pp->expansion_location, &result);
    }
    if (result)
    {
        memcpy(transcript->events, cache->events, (uint64_t)transcript->event_count * sizeof(ReplayEvent));
    }
    for (uint32_t i = 0; result && i < transcript->scratch_count; i += 1)
    {
        ReplayScratch scratch = cache->scratch[i];
        const uint32_t range = pp_find_range(pp, scratch.location);
        uint8_t* bytes = arena_push(arena, scratch.length, 1);
        result = range != BUSTER_PP_NONE && bytes != 0 && scratch.location - pp->ranges[range].base <= pp->ranges[range].length &&
                 scratch.length <= pp->ranges[range].length - (scratch.location - pp->ranges[range].base);
        if (result)
        {
            memcpy(bytes, pp->ranges[range].bytes + (scratch.location - pp->ranges[range].base), scratch.length);
            scratch.bytes = bytes;
            transcript->scratch[i] = scratch;
        }
    }
    for (uint32_t i = 0; result && i < transcript->file_count; i += 1)
    {
        ReplayFile file = cache->files[i];
        char* path = arena_push(arena, (uint64_t)file.length + 1, 1);
        result = path != 0;
        if (result)
        {
            memcpy(path, file.path, (uint64_t)file.length + 1);
            const PPFileData* data = &pp->datas[pp->files[file.file].data];
            file.path = path;
            result = !data->once && data->record == file.record && pp->files[file.file].system == file.system;
            transcript->files[i] = file;
        }
    }
    for (uint32_t i = 0; result && i < transcript->range_count; i += 1)
    {
        const PPRange* range = &pp->ranges[cache->range_begin + i];
        if (range->file == BUSTER_PP_NONE)
        {
            transcript->ranges[i] = (ReplayRange){ .file = BUSTER_PP_NONE };
            continue;
        }
        uint32_t file = 0;
        while (file < transcript->file_count && transcript->files[file].file != range->file)
        {
            file += 1;
        }
        result = file < transcript->file_count && (i == 0 || range->parent >= cache->range_begin) && range->file != BUSTER_PP_NONE &&
                 range->first_line_directive == BUSTER_PP_NONE;
        if (result)
        {
            transcript->ranges[i] = (ReplayRange){ .file = file, .parent = i == 0 ? BUSTER_PP_NONE : range->parent - cache->range_begin,
                                                 .length = range->length - 1, .include_location = i == 0 ? (ReplayLocation){ 0 } : replay_save_location(pp, context, range->include_location, &result) };
        }
    }
    for (uint32_t i = 0; result && i < transcript->write_count; i += 1)
    {
        ReplayWrite* write = &transcript->writes[i];
        const uint32_t range = write->defined ? pp_find_range(pp, write->macro.base) : cache->range_begin;
        result = range != BUSTER_PP_NONE && range >= cache->range_begin;
        write->range = range - cache->range_begin;
    }
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        const uint32_t token = cache->output + i;
        const uint8_t kind = pp->output.kinds[token];
        transcript->kinds[i] = kind;
        transcript->flags[i] = pp->output.flags[token];
        transcript->values[i] = pp->output.values[token];
        transcript->locations[i] = replay_save_location(pp, context, pp->output.locations[token], &result);
        const bool location_value = kind != C_TOKEN_RAW_IDENTIFIER && kind != BUSTER_PP_KIND_INTEGER;
        transcript->ends[i] = replay_save_location(pp, context, location_value ? pp->output.values[token] : 0, &result);
    }
    if (result && count != 0)
    {
        transcript->origins[0] = (PPHistoryRun){ .token = 0, .origin = pp_history_at(pp, cache->output) };
        uint32_t at = 1;
        for (uint32_t r = 0; r < pp->history_run_count; r += 1)
        {
            const PPHistoryRun run = pp->history_runs[r];
            if (run.token > cache->output && run.token < pp->output.count)
            {
                transcript->origins[at++] = (PPHistoryRun){ .token = run.token - cache->output, .origin = run.origin };
            }
        }
        for (uint32_t r = 0; result && r < origin_count; r += 1)
        {
            result &= replay_closed_origin(transcript->origins[r].origin, transcript);
        }
    }
    for (uint32_t i = 0; result && i < transcript->history_count; i += 1)
    {
        const PPExpansion expansion = pp->history[cache->history + i];
        transcript->history[i] = (ReplayExpansion){
            .expansion = expansion,
            .definition = replay_save_location(pp, context, expansion.definition, &result),
            .begin = replay_save_location(pp, context, expansion.begin, &result),
            .end = replay_save_location(pp, context, expansion.end, &result),
        };
        result &= replay_closed_origin(expansion.parent, transcript) && replay_closed_origin(expansion.end_origin, transcript);
    }
    for (uint32_t i = 0; result && i < transcript->substitution_count; i += 1)
    {
        const PPSubstitution substitution = pp->substitutions[cache->substitutions + i];
        transcript->substitutions[i] = (ReplaySubstitution){ .substitution = substitution, .parameter = replay_save_location(pp, context, substitution.parameter, &result) };
        result &= replay_closed_origin(substitution.expansion, transcript) && replay_closed_origin(substitution.source, transcript);
    }
    for (uint32_t i = 0; result && i < transcript->start_count; i += 1)
    {
        const PPExpansionStart start = pp->expansion_starts[start_begin + i];
        result &= start.token >= cache->output && start.token <= pp->output.count;
        transcript->starts[i] = (ReplayStart){ .token = start.token - cache->output, .location = replay_save_location(pp, context, start.location, &result) };
    }
    for (uint32_t i = 0; result && i < transcript->generated_count; i += 1)
    {
        const PPGenerated generated = pp->generated[cache->generated + i];
        result &= generated.first >= cache->sources && generated.first - cache->sources <= transcript->source_count &&
                  generated.count <= transcript->source_count - (generated.first - cache->sources) && replay_closed_origin(generated.expansion, transcript);
        transcript->generated[i] = (ReplayGenerated){ .generated = generated,
                                                    .location = replay_save_location(pp, context, generated.location, &result),
                                                    .anchor = replay_save_location(pp, context, generated.anchor, &result) };
    }
    for (uint32_t i = 0; result && i < transcript->source_count; i += 1)
    {
        const PPSource source = pp->history_sources[cache->sources + i];
        result &= replay_closed_origin(source.origin, transcript);
        transcript->sources[i] = (ReplaySource){ .source = source, .location = replay_save_location(pp, context, source.location, &result) };
    }
    return result;
}

static void pp_replay_finish(Preprocessor* pp, PPContext* context)
{
    PPReplayCache* cache = pp->options.replay;
    if (cache != 0 && cache->owner == pp)
    {
        for (uint32_t f = 0; f < cache->file_count; f += 1)
        {
            if (cache->files[f].file == context->file)
            {
                cache->files[f].guarded |= context->guard_taken;
            }
        }
    }
    if (cache != 0 && cache->owner == pp && cache->context == pp->context_count)
    {
        pp->replay_recording = false;
        cache->owner = 0;
        const uint32_t record = pp->datas[context->data].record;
        uint32_t variants = 0;
        for (uint32_t at = record != BUSTER_PP_NONE ? cache->heads[record] : 0; at != 0; at = cache->transcripts[at - 1].next)
        {
            variants += 1;
        }
        const bool safe = pp->errors == cache->errors && pp->warnings == cache->warnings && !pp->fatal && pp->silent_pragma_count == cache->silent &&
                          pp->line_directive_count == cache->lines && pp->embed_count == cache->embeds && record != BUSTER_PP_NONE && variants < BUSTER_REPLAY_VARIANTS &&
                          context->data == pp->files[context->file].data && cache->count < BUSTER_REPLAY_TRANSCRIPTS;
        cache->why[1] += pp->generated_count != cache->generated;
        cache->why[2] += variants >= BUSTER_REPLAY_VARIANTS;
        cache->why[3] += pp->errors != cache->errors || pp->warnings != cache->warnings;
        const uint64_t mark = cache->retained.length;
        ReplayRead* reads = safe ? arena_push_array(&cache->retained, cache->read_count, sizeof(ReplayRead), 8) : 0;
        ReplayWrite* writes = safe ? arena_push_array(&cache->retained, cache->write_count, sizeof(ReplayWrite), 8) : 0;
        if (safe && reads != 0 && writes != 0)
        {
            memcpy(reads, cache->reads, (uint64_t)cache->read_count * sizeof(ReplayRead));
            memcpy(writes, cache->writes, (uint64_t)cache->write_count * sizeof(ReplayWrite));
            cache->transcripts[cache->count] = (ReplayTranscript){
                .next = cache->heads[record],
                .record = record,
                .read_count = cache->read_count,
                .write_count = cache->write_count,
                .reads = reads,
                .writes = writes,
                .old_base = pp->ranges[context->range].base,
                .guarded = context->guard_taken,
            };
            if (replay_capture(pp, context, &cache->transcripts[cache->count]))
            {
                cache->count += 1;
                cache->heads[record] = cache->count;
                cache->recorded_reads += cache->read_count;
                cache->recorded_writes += cache->write_count;
            }
            else
            {
                arena_pop_to(&cache->retained, mark);
                cache->rejected += 1;
            }
        }
        else
        {
            arena_pop_to(&cache->retained, mark);
            cache->rejected += 1;
        }
    }
}

// Scalar loads feed contiguous columns; compares cover eight identities at a time.
static bool replay_matches(Preprocessor* pp, const ReplayTranscript* transcript)
{
    bool result = true;
    PPReplayCache* cache = pp->options.replay;
    for (uint32_t at = 0; result && at < transcript->read_count; at += 8)
    {
        uint64_t current[8] = { 0 };
        uint64_t expected[8] = { 0 };
        const uint32_t count = transcript->read_count - at < 8 ? transcript->read_count - at : 8;
        for (uint32_t lane = 0; lane < count; lane += 1)
        {
            const ReplayRead* read = &transcript->reads[at + lane];
            current[lane] = replay_key(pp, read->name);
            expected[lane] = read->key;
        }
        result = _mm512_cmpeq_epi64_mask(_mm512_loadu_si512(current), _mm512_loadu_si512(expected)) == 0xff;
        cache->checks += count;
    }
    return result;
}

static uint32_t replay_load_location(const Preprocessor* pp, const ReplayTranscript* transcript, const uint32_t* bases, ReplayLocation location)
{
    uint32_t result = 0;
    if (location.source < transcript->range_count)
    {
        result = bases[location.source] + location.offset;
    }
    else if (location.source == UINT32_MAX - 2)
    {
        result = pp->zero_location + location.offset;
    }
    else if (location.source == UINT32_MAX - 3)
    {
        result = pp->one_location + location.offset;
    }
    else if ((location.source & 0xc0000000u) == 0x40000000u)
    {
        result = bases[transcript->range_count + (location.source & 0x3fffffffu) - 1] + location.offset;
    }
    else if ((location.source & 0x80000000u) != 0 && (location.source & 0x7fffffffu) <= transcript->read_count)
    {
        const IdentId name = transcript->reads[(location.source & 0x7fffffffu) - 1].name;
        result = pp->macros[pp->macro_of[name]].base + location.offset;
    }
    return result;
}

static uint32_t replay_origin(const ReplayTranscript* transcript, uint32_t history, uint32_t substitutions, uint32_t generated, uint32_t origin)
{
    const uint32_t tag = origin & ~BUSTER_PP_ORIGIN_INDEX;
    const uint32_t index = origin & BUSTER_PP_ORIGIN_INDEX;
    return origin == 0 ? 0 : tag | (tag == BUSTER_PP_ORIGIN_ARGUMENT ? substitutions + index - transcript->substitution_begin :
                                  tag == BUSTER_PP_ORIGIN_GENERATED ? generated + index - transcript->generated_begin : history + index - transcript->history_begin);
}

// The producer keeps the source identity beside its byte offset. Two bounded
// loads give sixteen pairs; ordinary runs share one base and need one lookup.
// Exceptional mixed sources use scalar loads, avoiding Zen 5's gathers.
static __m512i replay_load_locations(const Preprocessor* pp, const ReplayTranscript* transcript, const uint32_t* bases, const ReplayLocation* locations, uint32_t count)
{
    const uint32_t first_count = count < 8 ? count : 8;
    const __m512i first = _mm512_maskz_loadu_epi64((__mmask8)_bzhi_u32(0xff, first_count), locations);
    __m512i second = _mm512_setzero_si512();
    if (count > 8)
    {
        second = _mm512_maskz_loadu_epi64((__mmask8)_bzhi_u32(0xff, count - 8), locations + 8);
    }
    const __m512i sources = _mm512_permutex2var_epi32(first, _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30), second);
    const __m512i offsets = _mm512_permutex2var_epi32(first, _mm512_setr_epi32(1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31), second);
    const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, count);
    const uint32_t source = locations[0].source;
    __m512i starts;
    if (_mm512_mask_cmpneq_epi32_mask(valid, sources, _mm512_set1_epi32((int)source)) == 0)
    {
        const uint32_t base = replay_load_location(pp, transcript, bases, (ReplayLocation){ .source = source });
        starts = _mm512_set1_epi32((int)base);
    }
    else
    {
        uint32_t local[16] = { 0 };
        for (uint32_t lane = 0; lane < count; lane += 1)
        {
            local[lane] = replay_load_location(pp, transcript, bases, (ReplayLocation){ .source = locations[lane].source });
        }
        starts = _mm512_loadu_si512(local);
    }
    return _mm512_add_epi32(starts, offsets);
}

static void replay_apply_output(Preprocessor* pp, const ReplayTranscript* transcript, const uint32_t* base)
{
    const uint32_t count = transcript->output_count;
    const uint32_t output = pp->output.count;
    const uint32_t history = pp->history_count;
    const uint32_t substitutions = pp->substitution_count;
    const uint32_t generated = pp->generated_count;
    const uint32_t sources = pp->history_source_count;
    bool ready = pp_room(pp, &pp->output, count);
    ready = ready && pp_history_room(pp, (void**)&pp->history, &pp->history_capacity, history + transcript->history_count, BUSTER_PP_MAX_HISTORY, sizeof(PPExpansion));
    ready = ready && pp_history_room(pp, (void**)&pp->substitutions, &pp->substitution_capacity, substitutions + transcript->substitution_count, BUSTER_PP_MAX_HISTORY, sizeof(PPSubstitution));
    ready = ready && pp_history_room(pp, (void**)&pp->generated, &pp->generated_capacity, generated + transcript->generated_count, BUSTER_PP_MAX_HISTORY, sizeof(PPGenerated));
    ready = ready && pp_history_room(pp, (void**)&pp->history_sources, &pp->history_source_capacity, sources + transcript->source_count, BUSTER_PP_MAX_HISTORY, sizeof(PPSource));
    if (ready)
    {
        memcpy(pp->output.kinds + output, transcript->kinds, count);
        memcpy(pp->output.flags + output, transcript->flags, count);
        if (pp->vectorized && !pp->options.replay->scalar_copy)
        {
            for (uint32_t at = 0; at < count; at += 16)
            {
                const uint32_t take = count - at < 16 ? count - at : 16;
                const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, take);
                const __m512i locations = replay_load_locations(pp, transcript, base, transcript->locations + at, take);
                const __m512i ends = replay_load_locations(pp, transcript, base, transcript->ends + at, take);
                const __m128i kinds = _mm_maskz_loadu_epi8(valid, transcript->kinds + at);
                const __mmask16 values = valid & (_mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(C_TOKEN_RAW_IDENTIFIER)) |
                                                 _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8((char)BUSTER_PP_KIND_INTEGER)));
                const __m512i retained = _mm512_maskz_loadu_epi32(valid, transcript->values + at);
                _mm512_mask_storeu_epi32(pp->output.locations + output + at, valid, locations);
                _mm512_mask_storeu_epi32(pp->output.values + output + at, valid, _mm512_mask_mov_epi32(ends, values, retained));
            }
        }
        else for (uint32_t i = 0; !pp->fatal && i < count; i += 1)
        {
            pp->output.locations[output + i] = replay_load_location(pp, transcript, base, transcript->locations[i]);
            const uint8_t kind = transcript->kinds[i];
            pp->output.values[output + i] = kind != C_TOKEN_RAW_IDENTIFIER && kind != BUSTER_PP_KIND_INTEGER ? replay_load_location(pp, transcript, base, transcript->ends[i]) : transcript->values[i];
        }
        if (!pp->options.no_history && count != 0)
        {
            for (uint32_t r = 0; !pp->fatal && r < transcript->origin_count; r += 1)
            {
                const PPHistoryRun run = transcript->origins[r];
                pp_history_change(pp, output + run.token, replay_origin(transcript, history, substitutions, generated, run.origin));
            }
            pp_history_change(pp, output + count, 0);
        }
        for (uint32_t i = 0; i < transcript->history_count; i += 1)
        {
            const ReplayExpansion* saved = &transcript->history[i];
            PPExpansion expansion = saved->expansion;
            expansion.definition = replay_load_location(pp, transcript, base, saved->definition);
            expansion.begin = replay_load_location(pp, transcript, base, saved->begin);
            expansion.end = replay_load_location(pp, transcript, base, saved->end);
            expansion.parent = replay_origin(transcript, history, substitutions, generated, expansion.parent);
            expansion.end_origin = replay_origin(transcript, history, substitutions, generated, expansion.end_origin);
            pp->history[history + i] = expansion;
        }
        for (uint32_t i = 0; i < transcript->substitution_count; i += 1)
        {
            PPSubstitution substitution = transcript->substitutions[i].substitution;
            substitution.expansion = replay_origin(transcript, history, substitutions, generated, substitution.expansion);
            substitution.source = replay_origin(transcript, history, substitutions, generated, substitution.source);
            substitution.parameter = replay_load_location(pp, transcript, base, transcript->substitutions[i].parameter);
            pp->substitutions[substitutions + i] = substitution;
        }
        for (uint32_t i = 0; i < transcript->start_count; i += 1)
        {
            if (pp->expansion_start_count < BUSTER_PP_MAX_OUTPUT_TOKENS)
            {
                const ReplayStart* start = &transcript->starts[i];
                const uint32_t token = output + start->token;
                const uint32_t count = pp->expansion_start_count;
                const bool replace = count != 0 && pp->expansion_starts[count - 1].token == token;
                pp->expansion_starts[replace ? count - 1 : count] = (PPExpansionStart){ .token = token, .location = replay_load_location(pp, transcript, base, start->location) };
                pp->expansion_start_count += !replace;
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "expansion start capacity exceeded");
            }
        }
        pp->history_count += transcript->history_count;
        pp->substitution_count += transcript->substitution_count;
        for (uint32_t i = 0; i < transcript->generated_count; i += 1)
        {
            const ReplayGenerated* saved = &transcript->generated[i];
            PPGenerated item = saved->generated;
            item.expansion = replay_origin(transcript, history, substitutions, generated, item.expansion);
            item.location = replay_load_location(pp, transcript, base, saved->location);
            item.anchor = replay_load_location(pp, transcript, base, saved->anchor);
            item.first = sources + item.first - transcript->source_begin;
            pp->generated[generated + i] = item;
        }
        for (uint32_t i = 0; i < transcript->source_count; i += 1)
        {
            const ReplaySource* saved = &transcript->sources[i];
            pp->history_sources[sources + i] = (PPSource){ .location = replay_load_location(pp, transcript, base, saved->location),
                                                        .origin = replay_origin(transcript, history, substitutions, generated, saved->source.origin) };
        }
        pp->generated_count += transcript->generated_count;
        pp->history_source_count += transcript->source_count;
        pp->output.count += count;
        if (transcript->expansion_changed)
        {
            pp->expansion_location = replay_load_location(pp, transcript, base, transcript->expansion_location);
        }
    }
}

static bool pp_replay_try(Preprocessor* pp, uint32_t file, uint32_t parent, uint32_t include_location, uint32_t search_index)
{
    PPReplayCache* cache = pp->options.replay;
    bool result = false;
    const uint32_t data_index = pp->files[file].data;
    PPFileData* data = &pp->datas[data_index];
    const uint32_t record = data->record;
    if (cache != 0 && cache->owner == 0 && parent != BUSTER_PP_NONE && record != BUSTER_PP_NONE && replay_context_ok(pp))
    {
        uint32_t match = 0;
        for (uint32_t at = cache->heads[record]; match == 0 && at != 0; at = cache->transcripts[at - 1].next)
        {
            const ReplayTranscript* candidate = &cache->transcripts[at - 1];
            bool same = candidate->search_index == search_index && candidate->depth == pp->include_depth + 1 &&
                        candidate->files[0].length == pp->files[file].path_length && strcmp(candidate->files[0].path, pp->files[file].path) == 0;
            for (uint32_t f = 0; same && f < candidate->file_count; f += 1)
            {
                const uint32_t local = pp->record_data[candidate->files[f].record];
                same = (local == 0 || !pp->datas[local - 1].once) && (local != 0 && pp->datas[local - 1].guarded) == candidate->files[f].initial_guarded;
                if (same && candidate->files[f].was_present)
                {
                    const ReplayFile* expected = &candidate->files[f];
                    uint32_t path = 0;
                    while (path < pp->file_count && (pp->files[path].path_length != expected->length || strcmp(pp->files[path].path, expected->path) != 0))
                    {
                        path += 1;
                    }
                    same = path < pp->file_count && pp->files[path].system == expected->initial_system;
                }
            }
            match = same && replay_matches(pp, candidate) ? at : 0;
        }
        if (match != 0)
        {
            const ReplayTranscript* transcript = &cache->transcripts[match - 1];
            uint32_t* bases = arena_push_array(&pp->arena, (uint64_t)transcript->range_count + transcript->scratch_count, sizeof(uint32_t), 8);
            uint32_t* ranges = arena_push_array(&pp->arena, transcript->range_count, sizeof(uint32_t), 8);
            uint32_t* files = arena_push_array(&pp->arena, transcript->file_count, sizeof(uint32_t), 8);
            result = true;
            if (bases != 0 && ranges != 0 && files != 0)
            {
                for (uint32_t f = 0; !pp->fatal && f < transcript->file_count; f += 1)
                {
                    const ReplayFile* saved = &transcript->files[f];
                    files[f] = pp_lookup_path(pp, saved->path, saved->length, saved->system);
                    if (files[f] == BUSTER_PP_NONE || pp->files[files[f]].data == BUSTER_PP_NONE ||
                        (!pp->datas[pp->files[files[f]].data].prepared && !pp_prepare_data(pp, &pp->datas[pp->files[files[f]].data], saved->path)))
                    {
                        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't materialize cached inclusion file");
                    }
                }
                for (uint32_t e = 0; !pp->fatal && e < transcript->event_count; e += 1)
                {
                    const ReplayEvent event = transcript->events[e];
                    if (event.scratch)
                    {
                        const ReplayScratch scratch = transcript->scratch[event.index];
                        bases[transcript->range_count + event.index] = pp_scratch_in(pp, scratch.bytes, scratch.length, scratch.clean);
                        continue;
                    }
                    const uint32_t r = event.index;
                    const ReplayRange* saved = &transcript->ranges[r];
                    const uint32_t path = files[saved->file];
                    const PPFileData* source = &pp->datas[pp->files[path].data];
                    const uint32_t from = r == 0 ? parent : ranges[saved->parent];
                    const uint32_t location = r == 0 ? include_location : replay_load_location(pp, transcript, bases, saved->include_location);
                    ranges[r] = pp_add_range(pp, saved->length, path, source->bytes, from, location);
                    if (ranges[r] != BUSTER_PP_NONE)
                    {
                        bases[r] = pp->ranges[ranges[r]].base;
                        const uint32_t note = pp->ranges[from].last_line_directive;
                        const uint8_t system = note != BUSTER_PP_NONE ? pp->line_directives[note].system : pp->ranges[from].system;
                        pp->ranges[ranges[r]].system = pp->files[path].system > system ? pp->files[path].system : system;
                    }
                }
                if (!pp->fatal)
                {
                    replay_apply_output(pp, transcript, bases);
                }
                for (uint32_t i = 0; !pp->fatal && i < transcript->write_count; i += 1)
                {
                    const ReplayWrite* write = &transcript->writes[i];
                    const uint32_t id = write->defined ? pp_new_macro(pp, write->name) : 0;
                    if (write->defined && id != 0)
                    {
                        PPMacro macro = write->macro;
                        const uint32_t target_file = files[transcript->ranges[write->range].file];
                        const uint32_t original_base = macro.base;
                        macro.data = pp->files[target_file].data;
                        macro.base = bases[write->range];
                        macro.location = macro.base + (macro.location - original_base);
                        macro.flags &= ~(BUSTER_PP_MACRO_COMPILED | BUSTER_PP_MACRO_IN_ORDER);
                        macro.items_begin = 0;
                        macro.item_count = 0;
                        macro.parameters = 0;
                        pp->macros[id] = macro;
                    }
                    pp_set_macro(pp, write->name, id);
                }
                data->guarded |= transcript->guarded;
                for (uint32_t f = 0; !pp->fatal && f < transcript->file_count; f += 1)
                {
                    PPFileData* local = &pp->datas[pp->files[files[f]].data];
                    local->guarded |= transcript->files[f].guarded;
                    local->included |= transcript->files[f].included;
                }
                pp->file_change_token = pp->output.count;
                const PPContext* includer = &pp->contexts[pp->context_count - 1];
                pp->file_change_location = pp_directive_resume(pp, includer, includer->next_directive - 1);
                cache->hits += 1;
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't allocate inclusion replay mapping");
            }
        }
        else
        {
            cache->misses += 1;
        }
    }
    return result;
}
