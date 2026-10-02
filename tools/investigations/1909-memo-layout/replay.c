/*
 * Standalone research replay of CParseExpressionQuery publications and lookups.
 * Ownership: diagnostic only; no Buster source, IR, or admission-policy changes.
 * Entry: main (TSV replay or --self-test).
 * Map: body_begin owns physical layouts; query_run/write_run execute accesses;
 *      event_apply validates traces; self_test covers keys and local-end limits.
 * Counts model logical fields and independently 64-byte-aligned allocations,
 * not hardware cache misses, elapsed time, or accepted performance evidence.
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

enum
{
    FLAG_VALID = 1,
    FLAG_CHECKED = 2,
    FLAG_RUNTIME = 4,
    FLAG_CONSTANT = 8,
    FLAG_MASK = 15,
    CACHE_LINE = 64,
    BLOCK_SLOTS = 64,
    MAX_FIELDS = 8,
    LAYOUT_COUNT = 7,
};

typedef enum LayoutKind
{
    LAYOUT_CURRENT,
    LAYOUT_FLAGS_FIRST,
    LAYOUT_HOT_COLD,
    LAYOUT_SOA,
    LAYOUT_BLOCKED,
    LAYOUT_COMPACT,
    LAYOUT_NARROW,
} LayoutKind;

typedef struct Payload
{
    u32 end;
    u32 scope;
    u32 type;
} Payload;

typedef struct Current16
{
    u32 end;
    u32 scope;
    u32 type;
    u32 flags;
} Current16;

typedef struct FlagsFirst16
{
    u32 flags;
    u32 end;
    u32 scope;
    u32 type;
} FlagsFirst16;

typedef struct Compact16
{
    u32 end;
    u32 scope;
    u32 type;
    u8 flags;
} Compact16;

typedef struct Narrow12
{
    u8 flags;
    u8 padding;
    u16 end_delta;
    u32 scope;
    u32 type;
} Narrow12;

typedef struct Block64
{
    u8 flags[BLOCK_SLOTS];
    Payload payload[BLOCK_SLOTS];
} Block64;

_Static_assert(sizeof(Current16) == 16, "current16 size");
_Static_assert(sizeof(FlagsFirst16) == 16, "flags-first16 size");
_Static_assert(sizeof(Payload) == 12, "payload size");
_Static_assert(sizeof(Compact16) == 16, "compact16 size");
_Static_assert(sizeof(Narrow12) == 12, "narrow12 size");
_Static_assert(sizeof(Block64) == 832, "blocked64 size");

typedef struct Stats
{
    u64 bodies;
    u64 queries;
    u64 nested_queries;
    u64 root_queries;
    u64 hits;
    u64 writes;
    u64 overwrites;
    u64 mismatches;
    u64 storage_sum;
    u64 storage_peak;
    u64 aligned_storage_sum;
    u64 aligned_storage_peak;
    u64 init_bytes;
    u64 init_lines;
    u64 query_bytes;
    u64 query_flags_bytes;
    u64 query_payload_bytes;
    u64 query_payload_probes;
    u64 query_lines;
    u64 query_stages;
    u64 end_checks;
    u64 scope_checks;
    u64 flags_checks;
    u64 type_checks;
    u64 write_bytes;
    u64 write_lines;
    u64 narrow_bodies;
    u64 fallback_bodies;
} Stats;

typedef struct Layout
{
    LayoutKind kind;
    char const* name;
    void* allocations[4];
    u64 storage;
    bool narrow;
    Stats stats;
} Layout;

typedef struct Replay
{
    Layout layouts[LAYOUT_COUNT];
    bool active;
    bool failed;
    u32 token_count;
    u32 body_start;
    u32 scope_count;
    u32 initial_type_count;
    u64 events;
    u64 total_tokens;
    u64 max_body_tokens;
    u64 small_bodies[4];
    u64 max_scope_count;
    u64 max_type_count;
} Replay;

typedef struct Touches
{
    u64 lines[16];
    u32 count;
    u64 bytes;
    u64 flags_bytes;
    u64 payload_bytes;
    bool payload_probed;
} Touches;

typedef struct Field
{
    void* pointer;
    u64 offset;
    u32 stream;
    u32 width;
    bool flags;
} Field;

typedef struct Event
{
    char kind;
    u32 values[MAX_FIELDS - 1];
    u32 count;
} Event;

static u64 round_line(u64 value)
{
    u64 result = (value + CACHE_LINE - 1) / CACHE_LINE * CACHE_LINE;
    return result;
}

static bool allocate_poison(void** pointer, u64 bytes)
{
    bool result = bytes <= SIZE_MAX;
    *pointer = 0;
    if (result && bytes)
    {
        *pointer = malloc((size_t)bytes);
        result = *pointer != 0;
        if (result)
        {
            memset(*pointer, 0xa5, (size_t)bytes);
        }
    }
    return result;
}

static void body_end(Replay* replay)
{
    for (u32 index = 0; index < LAYOUT_COUNT; index += 1)
    {
        for (u32 stream = 0; stream < 4; stream += 1)
        {
            free(replay->layouts[index].allocations[stream]);
            replay->layouts[index].allocations[stream] = 0;
        }
    }
    replay->active = false;
}

static Field field_get(Layout* layout, u32 slot, u32 field)
{
    Field result = {.stream = 0, .width = 4, .flags = field == 3};
    u8* allocation = layout->allocations[0];
    if (layout->kind == LAYOUT_CURRENT)
    {
        result.offset = (u64)slot * sizeof(Current16) + field * 4;
    }
    else if (layout->kind == LAYOUT_FLAGS_FIRST || (layout->kind == LAYOUT_NARROW && !layout->narrow))
    {
        result.offset = (u64)slot * sizeof(FlagsFirst16) + (field == 3 ? 0 : (field + 1) * 4);
    }
    else if (layout->kind == LAYOUT_HOT_COLD)
    {
        result.stream = field == 3 ? 0 : 1;
        allocation = layout->allocations[result.stream];
        result.offset = field == 3 ? slot : (u64)slot * sizeof(Payload) + field * 4;
        result.width = field == 3 ? 1 : 4;
    }
    else if (layout->kind == LAYOUT_SOA)
    {
        result.stream = field == 3 ? 0 : field + 1;
        allocation = layout->allocations[result.stream];
        result.width = field == 3 ? 1 : 4;
        result.offset = (u64)slot * result.width;
    }
    else if (layout->kind == LAYOUT_BLOCKED)
    {
        u32 local_slot = slot % BLOCK_SLOTS;
        u64 block_offset = (u64)(slot / BLOCK_SLOTS) * sizeof(Block64);
        result.offset = block_offset + (field == 3 ? local_slot : BLOCK_SLOTS + (u64)local_slot * sizeof(Payload) + field * 4);
        result.width = field == 3 ? 1 : 4;
    }
    else if (layout->kind == LAYOUT_COMPACT)
    {
        result.offset = (u64)slot * sizeof(Compact16) + (field == 3 ? offsetof(Compact16, flags) : field * 4);
        result.width = field == 3 ? 1 : 4;
    }
    else
    {
        result.offset = (u64)slot * sizeof(Narrow12) + (field == 3 ? offsetof(Narrow12, flags) : (field == 0 ? offsetof(Narrow12, end_delta) : field * 4));
        result.width = field == 3 ? 1 : (field == 0 ? 2 : 4);
    }
    result.pointer = allocation + (size_t)result.offset;
    return result;
}

static void touch_field(Touches* touches, Field field)
{
    u64 first = field.offset / CACHE_LINE;
    u64 last = (field.offset + field.width - 1) / CACHE_LINE;
    for (u64 line = first; line <= last; line += 1)
    {
        u64 key = ((u64)field.stream << 56) | line;
        bool present = false;
        for (u32 index = 0; index < touches->count; index += 1)
        {
            present |= touches->lines[index] == key;
        }
        if (!present)
        {
            touches->lines[touches->count] = key;
            touches->count += 1;
        }
    }
    touches->bytes += field.width;
    if (field.flags)
    {
        touches->flags_bytes += field.width;
    }
    else
    {
        touches->payload_bytes += field.width;
        touches->payload_probed = true;
    }
}

static u32 field_load(Layout* layout, u32 slot, u32 field, Touches* touches)
{
    Field location = field_get(layout, slot, field);
    u32 result;
    if (location.width == 1)
    {
        u8 value;
        memcpy(&value, location.pointer, sizeof(value));
        result = value;
    }
    else if (location.width == 2)
    {
        u16 value;
        memcpy(&value, location.pointer, sizeof(value));
        result = value;
    }
    else
    {
        memcpy(&result, location.pointer, sizeof(result));
    }
    if (touches)
    {
        touch_field(touches, location);
    }
    return result;
}

static void field_store(Layout* layout, u32 slot, u32 field, u32 value, Touches* touches)
{
    Field location = field_get(layout, slot, field);
    if (location.width == 1)
    {
        u8 narrowed = (u8)value;
        memcpy(location.pointer, &narrowed, sizeof(narrowed));
    }
    else if (location.width == 2)
    {
        u16 narrowed = (u16)value;
        memcpy(location.pointer, &narrowed, sizeof(narrowed));
    }
    else
    {
        memcpy(location.pointer, &value, sizeof(value));
    }
    if (touches)
    {
        touch_field(touches, location);
    }
}

static bool body_begin(Replay* replay, Event const* event)
{
    u32 token_count = event->values[0];
    u32 body_start = event->values[1];
    bool result = !replay->active && (u64)token_count + body_start <= UINT32_MAX;
    if (result)
    {
        replay->active = true;
        replay->token_count = token_count;
        replay->body_start = body_start;
        replay->scope_count = event->values[2];
        replay->initial_type_count = event->values[3];
        replay->total_tokens += token_count;
        if (token_count > replay->max_body_tokens) replay->max_body_tokens = token_count;
        if (replay->scope_count > replay->max_scope_count) replay->max_scope_count = replay->scope_count;
        if (replay->initial_type_count > replay->max_type_count) replay->max_type_count = replay->initial_type_count;
        replay->small_bodies[0] += token_count == 0;
        replay->small_bodies[1] += token_count > 0 && token_count <= 4;
        replay->small_bodies[2] += token_count > 4 && token_count <= 64;
        replay->small_bodies[3] += token_count > 64;
        for (u32 index = 0; index < LAYOUT_COUNT && result; index += 1)
        {
            Layout* layout = &replay->layouts[index];
            u64 sizes[4] = {0};
            layout->narrow = layout->kind == LAYOUT_NARROW && token_count <= UINT16_MAX;
            if (layout->kind == LAYOUT_HOT_COLD)
            {
                sizes[0] = token_count;
                sizes[1] = (u64)token_count * sizeof(Payload);
            }
            else if (layout->kind == LAYOUT_SOA)
            {
                sizes[0] = token_count;
                sizes[1] = sizes[2] = sizes[3] = (u64)token_count * sizeof(u32);
            }
            else if (layout->kind == LAYOUT_BLOCKED)
            {
                sizes[0] = ((u64)token_count + BLOCK_SLOTS - 1) / BLOCK_SLOTS * sizeof(Block64);
            }
            else
            {
                sizes[0] = (u64)token_count * (layout->narrow ? sizeof(Narrow12) : sizeof(Current16));
            }
            layout->storage = 0;
            u64 aligned_storage = 0;
            for (u32 stream = 0; stream < 4 && result; stream += 1)
            {
                result = allocate_poison(&layout->allocations[stream], sizes[stream]);
                layout->storage += sizes[stream];
                aligned_storage += round_line(sizes[stream]);
            }
            if (result)
            {
                bool full_clear = layout->kind == LAYOUT_CURRENT || layout->kind == LAYOUT_FLAGS_FIRST ||
                    (layout->kind == LAYOUT_NARROW && !layout->narrow);
                u64 init_bytes;
                u64 init_lines;
                if (full_clear)
                {
                    if (sizes[0]) memset(layout->allocations[0], 0, (size_t)sizes[0]);
                    init_bytes = sizes[0];
                    init_lines = round_line(sizes[0]) / CACHE_LINE;
                }
                else if (layout->kind == LAYOUT_HOT_COLD || layout->kind == LAYOUT_SOA)
                {
                    if (sizes[0]) memset(layout->allocations[0], 0, (size_t)sizes[0]);
                    init_bytes = sizes[0];
                    init_lines = round_line(sizes[0]) / CACHE_LINE;
                }
                else if (layout->kind == LAYOUT_BLOCKED)
                {
                    u64 blocks = ((u64)token_count + BLOCK_SLOTS - 1) / BLOCK_SLOTS;
                    for (u64 block = 0; block < blocks; block += 1)
                    {
                        Block64* entry = (Block64*)layout->allocations[0] + (size_t)block;
                        memset(entry->flags, 0, sizeof(entry->flags));
                    }
                    init_bytes = blocks * BLOCK_SLOTS;
                    init_lines = blocks;
                }
                else
                {
                    for (u32 slot = 0; slot < token_count; slot += 1)
                    {
                        field_store(layout, slot, 3, 0, 0);
                    }
                    init_bytes = token_count;
                    u64 stride = layout->narrow ? sizeof(Narrow12) : sizeof(Compact16);
                    u64 flags_offset = layout->narrow ? offsetof(Narrow12, flags) : offsetof(Compact16, flags);
                    init_lines = token_count ? ((u64)(token_count - 1) * stride + flags_offset) / CACHE_LINE + 1 : 0;
                }
                Stats* stats = &layout->stats;
                stats->bodies += 1;
                stats->storage_sum += layout->storage;
                stats->aligned_storage_sum += aligned_storage;
                stats->init_bytes += init_bytes;
                stats->init_lines += init_lines;
                stats->narrow_bodies += layout->narrow;
                stats->fallback_bodies += layout->kind == LAYOUT_NARROW && !layout->narrow;
                if (layout->storage > stats->storage_peak) stats->storage_peak = layout->storage;
                if (aligned_storage > stats->aligned_storage_peak) stats->aligned_storage_peak = aligned_storage;
            }
        }
    }
    return result;
}

static bool flags_match(u32 stored, u32 requested)
{
    bool result = (stored & ~(u32)FLAG_CHECKED) == (requested & ~(u32)FLAG_CHECKED) && (stored & requested) == requested;
    return result;
}

static bool query_run(Replay* replay, Layout* layout, Event const* event)
{
    u32 slot = event->values[1];
    u32 end = event->values[2];
    u32 scope = event->values[3];
    u32 requested = event->values[4];
    u32 type_count = event->values[5];
    bool hit = true;
    Touches touches = {0};
    Stats* stats = &layout->stats;
    if (layout->kind != LAYOUT_CURRENT)
    {
        stats->flags_checks += 1;
        hit = flags_match(field_load(layout, slot, 3, &touches), requested);
    }
    if (hit)
    {
        stats->end_checks += 1;
        u32 stored_end = field_load(layout, slot, 0, &touches);
        if (layout->narrow)
        {
            stored_end = replay->body_start + slot + stored_end;
        }
        hit = stored_end == end;
    }
    if (hit)
    {
        stats->scope_checks += 1;
        hit = field_load(layout, slot, 1, &touches) == scope;
    }
    if (hit && layout->kind == LAYOUT_CURRENT)
    {
        stats->flags_checks += 1;
        hit = flags_match(field_load(layout, slot, 3, &touches), requested);
    }
    if (hit)
    {
        stats->type_checks += 1;
        hit = field_load(layout, slot, 2, &touches) < type_count;
    }
    stats->queries += 1;
    stats->nested_queries += event->values[0] == 0;
    stats->root_queries += event->values[0] == 1;
    stats->hits += hit;
    stats->mismatches += hit != (event->values[6] != 0);
    stats->query_bytes += touches.bytes;
    stats->query_flags_bytes += touches.flags_bytes;
    stats->query_payload_bytes += touches.payload_bytes;
    stats->query_payload_probes += touches.payload_probed;
    stats->query_lines += touches.count;
    stats->query_stages = stats->end_checks + stats->scope_checks + stats->flags_checks + stats->type_checks;
    return hit;
}

static void write_run(Replay* replay, Layout* layout, Event const* event)
{
    u32 slot = event->values[0];
    u32 end = event->values[1];
    u32 scope = event->values[2];
    u32 type = event->values[3];
    u32 flags = event->values[4];
    u32 previous = field_load(layout, slot, 3, 0);
    Touches touches = {0};
    if (layout->narrow)
    {
        end -= replay->body_start + slot;
    }
    field_store(layout, slot, 0, end, &touches);
    field_store(layout, slot, 1, scope, &touches);
    field_store(layout, slot, 2, type, &touches);
    /* Each replay is serial. Publishing flags last is the validity contract,
     * not a promise of synchronization or safe concurrent readers. */
    field_store(layout, slot, 3, flags, &touches);
    layout->stats.writes += 1;
    layout->stats.overwrites += (previous & FLAG_VALID) != 0;
    layout->stats.write_bytes += touches.bytes;
    layout->stats.write_lines += touches.count;
}

static void replay_initialize(Replay* replay)
{
    char const* names[LAYOUT_COUNT] = {"current16", "flags_first16", "hot_cold13", "soa13", "blocked64", "compact16", "narrow12_wide16"};
    memset(replay, 0, sizeof(*replay));
    for (u32 index = 0; index < LAYOUT_COUNT; index += 1)
    {
        replay->layouts[index].kind = (LayoutKind)index;
        replay->layouts[index].name = names[index];
    }
}

static bool event_apply(Replay* replay, Event const* event)
{
    bool result = true;
    if (event->kind == 'B')
    {
        result = event->count == 4 && body_begin(replay, event);
    }
    else if (event->kind == 'E')
    {
        result = event->count == 0 && replay->active;
        if (result) body_end(replay);
    }
    else if (event->kind == 'Q' || event->kind == 'W')
    {
        result = replay->active && event->count == (event->kind == 'Q' ? 7u : 5u);
        if (result)
        {
            u32 slot = event->values[event->kind == 'Q' ? 1 : 0];
            u32 end = event->values[event->kind == 'Q' ? 2 : 1];
            u32 flags = event->values[4];
            result = slot < replay->token_count && end > (u64)replay->body_start + slot &&
                end <= (u64)replay->body_start + replay->token_count && (flags & FLAG_VALID) != 0 && (flags & ~(u32)FLAG_MASK) == 0;
            if (event->kind == 'Q')
            {
                result &= event->values[0] <= 1 && event->values[6] <= 1;
                if (event->values[5] > replay->max_type_count) replay->max_type_count = event->values[5];
            }
        }
        if (result)
        {
            for (u32 index = 0; index < LAYOUT_COUNT; index += 1)
            {
                if (event->kind == 'Q')
                {
                    (void)query_run(replay, &replay->layouts[index], event);
                }
                else
                {
                    write_run(replay, &replay->layouts[index], event);
                }
            }
        }
    }
    else
    {
        result = false;
    }
    replay->events += 1;
    replay->failed |= !result;
    return result;
}

static bool event_parse(char* line, Event* event)
{
    char* fields[MAX_FIELDS + 1] = {0};
    u32 count = 0;
    char* next = strtok(line, " \t\r\n");
    while (next && count <= MAX_FIELDS)
    {
        fields[count] = next;
        count += 1;
        next = strtok(0, " \t\r\n");
    }
    bool result = count > 0 && count <= MAX_FIELDS && fields[0][0] && !fields[0][1];
    memset(event, 0, sizeof(*event));
    if (result)
    {
        event->kind = fields[0][0];
        event->count = count - 1;
        for (u32 index = 1; index < count && result; index += 1)
        {
            char* end;
            errno = 0;
            unsigned long long value = strtoull(fields[index], &end, 10);
            result = fields[index][0] >= '0' && fields[index][0] <= '9' && *end == 0 && !errno && value <= UINT32_MAX;
            if (result) event->values[index - 1] = (u32)value;
        }
    }
    return result;
}

static void stats_print(Replay const* replay)
{
    printf("# model: logical field accesses; separately 64B-aligned streams; no cache/timing claim\n");
    printf("# events=%" PRIu64 " total_tokens=%" PRIu64 " max_body_tokens=%" PRIu64 " bodies_0=%" PRIu64 " bodies_1_4=%" PRIu64 " bodies_5_64=%" PRIu64 " bodies_65_plus=%" PRIu64 " max_scope_count=%" PRIu64 " max_type_count=%" PRIu64 "\n",
        replay->events, replay->total_tokens, replay->max_body_tokens, replay->small_bodies[0], replay->small_bodies[1], replay->small_bodies[2], replay->small_bodies[3], replay->max_scope_count, replay->max_type_count);
    puts("layout\tbodies\tqueries\tnested_queries\troot_queries\thits\twrites\toverwrites\tmismatches\tstorage_sum\tstorage_peak\taligned_storage_sum\taligned_storage_peak\tinit_bytes\tinit_lines\tquery_bytes\tquery_flags_bytes\tquery_payload_bytes\tquery_payload_probes\tquery_lines\tquery_stages\tend_checks\tscope_checks\tflags_checks\ttype_checks\twrite_bytes\twrite_lines\tnarrow_bodies\tfallback_bodies");
    for (u32 index = 0; index < LAYOUT_COUNT; index += 1)
    {
        Layout const* layout = &replay->layouts[index];
        Stats const* s = &layout->stats;
        printf("%s", layout->name);
        u64 values[] = {s->bodies, s->queries, s->nested_queries, s->root_queries, s->hits, s->writes, s->overwrites, s->mismatches,
            s->storage_sum, s->storage_peak, s->aligned_storage_sum, s->aligned_storage_peak, s->init_bytes, s->init_lines,
            s->query_bytes, s->query_flags_bytes, s->query_payload_bytes, s->query_payload_probes, s->query_lines, s->query_stages,
            s->end_checks, s->scope_checks, s->flags_checks, s->type_checks, s->write_bytes, s->write_lines, s->narrow_bodies, s->fallback_bodies};
        for (u32 column = 0; column < sizeof(values) / sizeof(values[0]); column += 1)
        {
            printf("\t%" PRIu64, values[column]);
        }
        putchar('\n');
    }
}

static bool replay_file(Replay* replay, FILE* input)
{
    char line[1024];
    u64 line_number = 0;
    bool result = true;
    while (result && fgets(line, sizeof(line), input))
    {
        line_number += 1;
        size_t length = strlen(line);
        if (length == sizeof(line) - 1 && line[length - 1] != '\n')
        {
            result = false;
        }
        else
        {
            char* start = line;
            while (*start == ' ' || *start == '\t' || *start == '\r') start += 1;
            if (*start && *start != '\n' && *start != '#')
            {
                Event event;
                result = event_parse(start, &event) && event_apply(replay, &event);
            }
        }
        if (!result) fprintf(stderr, "invalid or unallocatable event at line %" PRIu64 "\n", line_number);
    }
    result &= !ferror(input) && !replay->active;
    if (replay->active) fprintf(stderr, "unterminated body at EOF\n");
    return result;
}

static bool self_event(Replay* replay, char kind, u32 count, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g)
{
    Event event = {.kind = kind, .values = {a, b, c, d, e, f, g}, .count = count};
    bool result = event_apply(replay, &event);
    return result;
}

static bool self_test(void)
{
    Replay replay;
    replay_initialize(&replay);
    bool result = true;
    u32 sizes[] = {0, 1, 4, 65, UINT16_MAX, UINT16_MAX + 1u};
    for (u32 test = 0; test < sizeof(sizes) / sizeof(sizes[0]) && result; test += 1)
    {
        u32 tokens = sizes[test];
        u32 start = test == 4 ? UINT32_MAX - tokens : 100;
        result = self_event(&replay, 'B', 4, tokens, start, UINT32_MAX, UINT32_MAX, 0, 0, 0);
        if (tokens && result)
        {
            /* Empty slot, checked-superset hit, checked-required miss, scope
             * overwrite, end overwrite, stale type bound, mixed mode flags. */
            result &= self_event(&replay, 'Q', 7, 1, 0, start + tokens, 90000, 1, 100000, 0);
            result &= self_event(&replay, 'W', 5, 0, start + tokens, 90000, 80000, 3, 0, 0);
            result &= self_event(&replay, 'Q', 7, 0, 0, start + tokens, 90000, 1, 100000, 1);
            result &= self_event(&replay, 'Q', 7, 1, 0, start + tokens, 90000, 3, 100000, 1);
            result &= self_event(&replay, 'Q', 7, 0, 0, start + tokens, 90000, 3, 80000, 0);
            result &= self_event(&replay, 'W', 5, 0, start + tokens, 90001, 80001, 1, 0, 0);
            result &= self_event(&replay, 'Q', 7, 1, 0, start + tokens, 90000, 1, 100000, 0);
            result &= self_event(&replay, 'Q', 7, 1, 0, start + tokens, 90001, 3, 100000, 0);
            result &= self_event(&replay, 'Q', 7, 0, 0, start + tokens, 90001, 1, 100000, 1);
            u32 dense = tokens < BLOCK_SLOTS + 1u ? tokens : BLOCK_SLOTS + 1u;
            for (u32 slot = 0; slot < dense && result; slot += 1)
            {
                u32 flags = FLAG_VALID | ((slot & 7u) << 1);
                result &= self_event(&replay, 'W', 5, slot, start + slot + 1, 70000 + slot, 60000 + slot, flags, 0, 0);
                result &= self_event(&replay, 'Q', 7, slot & 1u, slot, start + slot + 1, 70000 + slot, flags, 100000, 1);
                result &= self_event(&replay, 'Q', 7, slot & 1u, slot, start + slot + 1, 70000 + slot, flags ^ FLAG_RUNTIME, 100000, 0);
            }
            if (tokens > 1)
            {
                result &= self_event(&replay, 'Q', 7, 0, 0, start + 2, 70000, 1, 100000, 0);
                result &= self_event(&replay, 'W', 5, tokens - 1, start + tokens, UINT32_MAX - 1, UINT32_MAX - 1, 15, 0, 0);
                result &= self_event(&replay, 'Q', 7, 1, tokens - 1, start + tokens, UINT32_MAX - 1, 15, UINT32_MAX, 1);
            }
        }
        result &= self_event(&replay, 'E', 0, 0, 0, 0, 0, 0, 0, 0);
    }
    for (u32 layout = 0; layout < LAYOUT_COUNT; layout += 1)
    {
        result &= replay.layouts[layout].stats.mismatches == 0;
    }
    /* Reject overflowing absolute-body bounds, oversized/local invalid ends,
     * missing validity, out-of-range slots, and event cardinality. */
    result &= !self_event(&replay, 'B', 4, 2, UINT32_MAX, 0, 0, 0, 0, 0);
    replay.failed = false;
    result &= self_event(&replay, 'B', 4, 1, 100, 1, 1, 0, 0, 0);
    result &= !self_event(&replay, 'W', 5, 0, 102, 0, 0, 1, 0, 0);
    result &= !self_event(&replay, 'W', 5, 0, 101, 0, 0, 0, 0, 0);
    result &= !self_event(&replay, 'Q', 7, 1, 1, 101, 0, 1, 1, 0);
    result &= !self_event(&replay, 'E', 1, 0, 0, 0, 0, 0, 0, 0);
    result &= self_event(&replay, 'E', 0, 0, 0, 0, 0, 0, 0, 0);
    body_end(&replay);
    printf("self-test: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int main(int argc, char** argv)
{
    int result;
    if (argc == 2 && !strcmp(argv[1], "--self-test"))
    {
        result = self_test() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    else if (argc == 2)
    {
        Replay replay;
        replay_initialize(&replay);
        FILE* input = !strcmp(argv[1], "-") ? stdin : fopen(argv[1], "rb");
        bool valid = input && replay_file(&replay, input);
        if (!input) fprintf(stderr, "could not open trace: %s\n", argv[1]);
        if (input && input != stdin) fclose(input);
        bool equivalent = true;
        for (u32 index = 0; index < LAYOUT_COUNT; index += 1)
        {
            equivalent &= replay.layouts[index].stats.mismatches == 0;
        }
        if (valid) stats_print(&replay);
        if (!equivalent) fprintf(stderr, "layout mismatch with observed query results\n");
        body_end(&replay);
        result = valid && equivalent && !replay.failed ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    else
    {
        fprintf(stderr, "usage: replay TRACE.tsv|-|--self-test\n");
        result = EXIT_FAILURE;
    }
    return result;
}
