/* Original bounded native Perfetto sample query. Exact raw-nanosecond ranges
 * are half-open. Counts, source periods and cumulative state stay distinct.
 * Layout: wire lookups, sequence-scoped dictionaries, sample totals, JSON.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#define main buster_performance_library_cli
#include "main.c"
#undef main

#define BUSTER_TRACE_DICT_SLOTS 524288u
#define BUSTER_TRACE_SEQUENCE_LIMIT 256u

typedef struct TraceEntry TraceEntry;
struct TraceEntry
{
    uint64_t iid;
    uint32_t sequence;
    uint32_t epoch;
    unsigned int kind;
    PerfBytes bytes;
    uint64_t inclusive;
    uint64_t self;
};

typedef struct TraceSequence TraceSequence;
struct TraceSequence
{
    uint32_t id;
    uint32_t epoch;
    uint32_t clock;
    bool valid;
};

typedef struct TraceQuery TraceQuery;
struct TraceQuery
{
    TraceEntry *dictionary;
    size_t dictionary_count;
    TraceSequence sequences[BUSTER_TRACE_SEQUENCE_LIMIT];
    size_t sequence_count;
    uint64_t fields;
    uint64_t start;
    uint64_t end;
    uint64_t total;
    uint64_t selected;
    uint64_t period_ns;
    uint64_t unweighted;
    uint64_t lost;
    uint64_t skipped;
    uint64_t first;
    uint64_t last;
    uint32_t clock;
    bool valid;
};

static bool trace_next(TraceQuery *query, PerfProto *input)
{
    bool result = query->valid && perf_proto_next(input);
    if (result)
    {
        query->valid = ++query->fields <= UINT64_C(32000000);
        result = query->valid;
    }
    else
    {
        query->valid = query->valid && input->valid;
    }
    return result;
}

static PerfProto trace_field(TraceQuery *query, PerfBytes bytes, uint32_t field)
{
    PerfProto input = {.bytes = bytes, .valid = true};
    PerfProto result = {.valid = true};
    while (trace_next(query, &input))
    {
        if (input.field == field)
        {
            result = input;
        }
    }
    return result;
}

static uint64_t trace_integer(TraceQuery *query, PerfBytes bytes, uint32_t field, uint64_t fallback)
{
    PerfProto value = trace_field(query, bytes, field);
    query->valid = query->valid && (value.field == 0 || value.wire == 0);
    uint64_t result = value.field == 0 ? fallback : value.integer;
    return result;
}

static PerfBytes trace_message(TraceQuery *query, PerfBytes bytes, uint32_t field)
{
    PerfProto value = trace_field(query, bytes, field);
    query->valid = query->valid && (value.field == 0 || value.wire == 2);
    PerfBytes result = value.value;
    return result;
}

static TraceEntry *trace_dictionary(TraceQuery *query, const TraceSequence *sequence, unsigned int kind, uint64_t iid, PerfBytes create)
{
    uint64_t hash = iid * UINT64_C(11400714819323198485) ^ ((uint64_t)sequence->id << 32) ^ ((uint64_t)sequence->epoch << 16) ^ kind;
    size_t index = (size_t)(hash ^ (hash >> 32)) & (BUSTER_TRACE_DICT_SLOTS - 1);
    TraceEntry *result = NULL;
    for (size_t probe = 0; probe < BUSTER_TRACE_DICT_SLOTS && query->valid; ++probe)
    {
        TraceEntry *entry = &query->dictionary[index];
        if (entry->kind == 0)
        {
            if (create.data != NULL && iid != 0 && query->dictionary_count < BUSTER_TRACE_DICT_SLOTS / 2)
            {
                *entry = (TraceEntry){.iid = iid, .sequence = sequence->id, .epoch = sequence->epoch, .kind = kind, .bytes = create};
                ++query->dictionary_count;
                result = entry;
            }
            break;
        }
        if (entry->iid == iid && entry->sequence == sequence->id && entry->epoch == sequence->epoch && entry->kind == kind)
        {
            query->valid = create.data == NULL || perf_bytes_equal(entry->bytes, create);
            result = entry;
            break;
        }
        query->valid = ++query->fields <= UINT64_C(32000000);
        index = (index + 1) & (BUSTER_TRACE_DICT_SLOTS - 1);
    }
    query->valid = query->valid && result != NULL;
    return result;
}

static void trace_intern(TraceQuery *query, const TraceSequence *sequence, PerfBytes bytes)
{
    PerfProto input = {.bytes = bytes, .valid = true};
    while (trace_next(query, &input))
    {
        if (input.field == 5 || input.field == 6 || input.field == 7 || input.field == 50)
        {
            query->valid = input.wire == 2;
            uint64_t iid = trace_integer(query, input.value, 1, 0);
            trace_dictionary(query, sequence, input.field, iid, input.value);
        }
    }
}

static void trace_sample(TraceQuery *query, const TraceSequence *sequence, PerfBytes packet, PerfBytes sample, unsigned int kind)
{
    if (kind == 66)
    {
        uint64_t lost = trace_integer(query, sample, 17, 0);
        uint64_t skipped = trace_integer(query, sample, 18, 0);
        query->valid = query->valid && lost <= UINT64_MAX - query->lost && skipped <= UINT64_MAX - query->skipped;
        query->lost += query->valid ? lost : 0;
        query->skipped += query->valid ? skipped : 0;
    }
    uint64_t stack_iid = trace_integer(query, sample, kind == 66 ? 4 : 6, 0);
    query->valid = query->valid && (kind != 135 || trace_message(query, sample, 5).data == NULL);
    if (stack_iid != 0 && query->valid)
    {
        uint64_t time = trace_integer(query, packet, 8, UINT64_MAX);
        uint64_t clock = trace_integer(query, packet, 58, sequence->clock);
        query->valid = sequence->valid && time <= INT64_MAX && clock == query->clock && query->total < BUSTER_PERF_ROW_LIMIT;
        if (query->valid)
        {
            ++query->total;
            query->first = time < query->first ? time : query->first;
            query->last = time > query->last ? time : query->last;
        }
        TraceEntry *stack = query->valid ? trace_dictionary(query, sequence, 7, stack_iid, (PerfBytes){0}) : NULL;
        TraceEntry *frames[BUSTER_PERF_DEPTH_LIMIT];
        size_t count = 0;
        PerfProto input = {.bytes = stack != NULL ? stack->bytes : (PerfBytes){0}, .valid = true};
        while (trace_next(query, &input))
        {
            if (input.field == 2)
            {
                PerfProto packed = {.bytes = input.value, .valid = true};
                bool once = input.wire == 0;
                query->valid = input.wire == 0 || input.wire == 2;
                while (query->valid && (once || packed.cursor < packed.bytes.size))
                {
                    uint64_t iid = once ? input.integer : perf_proto_varint(&packed);
                    once = false;
                    query->valid = packed.valid && count < BUSTER_PERF_DEPTH_LIMIT;
                    if (query->valid)
                    {
                        frames[count++] = trace_dictionary(query, sequence, 6, iid, (PerfBytes){0});
                    }
                }
            }
        }
        query->valid = query->valid && count != 0;
        if (time >= query->start && time < query->end && query->valid)
        {
            ++query->selected;
            for (size_t i = 0; i < count; ++i)
            {
                bool earlier = false;
                for (size_t j = 0; j < i; ++j)
                {
                    query->valid = query->valid && ++query->fields <= UINT64_C(32000000);
                    earlier = earlier || frames[i] == frames[j];
                }
                frames[i]->inclusive += !earlier;
            }
            ++frames[count - 1]->self;
            uint64_t descriptor_iid = kind == 135 ? trace_integer(query, sample, 10, 0) : 0;
            PerfProto weight = trace_field(query, sample, 11);
            if (descriptor_iid != 0 && weight.field != 0 && kind == 135)
            {
                TraceEntry *descriptor = trace_dictionary(query, sequence, 50, descriptor_iid, (PerfBytes){0});
                PerfBytes data = descriptor != NULL ? descriptor->bytes : (PerfBytes){0};
                uint64_t unit = trace_integer(query, data, 3, 0);
                uint64_t multiplier = trace_integer(query, data, 5, 1);
                query->valid = query->valid && unit == 1 && multiplier == 1 && weight.wire == 0 && weight.integer <= INT64_MAX && weight.integer <= UINT64_MAX - query->period_ns;
                query->period_ns += query->valid ? weight.integer : 0;
            }
            else
            {
                ++query->unweighted;
            }
        }
    }
}

static void trace_query(TraceQuery *query, PerfBytes bytes)
{
    PerfProto trace = {.bytes = bytes, .valid = true};
    uint64_t packets = 0;
    while (trace_next(query, &trace))
    {
        if (trace.field != 1)
        {
            continue;
        }
        query->valid = trace.wire == 2 && ++packets <= BUSTER_PERF_ROW_LIMIT;
        PerfBytes packet = trace.value;
        uint64_t id = trace_integer(query, packet, 10, 0);
        query->valid = query->valid && id <= UINT32_MAX;
        TraceSequence *sequence = NULL;
        for (size_t i = 0; i < query->sequence_count; ++i)
        {
            if (query->sequences[i].id == id)
            {
                sequence = &query->sequences[i];
            }
        }
        if (sequence == NULL && query->valid && query->sequence_count < BUSTER_TRACE_SEQUENCE_LIMIT)
        {
            sequence = &query->sequences[query->sequence_count++];
            *sequence = (TraceSequence){.id = (uint32_t)id, .clock = 6, .valid = true};
        }
        query->valid = query->valid && sequence != NULL;
        if (!query->valid)
        {
            break;
        }
        if (trace_integer(query, packet, 42, 0) != 0)
        {
            sequence->valid = false;
        }
        uint64_t flags = trace_integer(query, packet, 13, 0);
        if ((flags & 1) != 0)
        {
            query->valid = sequence->epoch < UINT32_MAX;
            ++sequence->epoch;
            sequence->valid = true;
            sequence->clock = 6;
        }
        if ((flags & 2) != 0 && !sequence->valid)
        {
            ++query->skipped;
            continue;
        }
        PerfBytes defaults = trace_message(query, packet, 59);
        uint64_t clock = trace_integer(query, defaults, 58, sequence->clock);
        query->valid = query->valid && clock <= UINT32_MAX;
        sequence->clock = (uint32_t)clock;
        PerfBytes snapshot = trace_message(query, packet, 6);
        uint64_t primary = trace_integer(query, snapshot, 2, query->clock);
        query->valid = query->valid && primary <= UINT32_MAX && (query->total == 0 || primary == query->clock);
        query->clock = (uint32_t)primary;
        query->valid = query->valid && trace_field(query, packet, 50).field == 0 && trace_field(query, packet, 97).field == 0;
        trace_intern(query, sequence, trace_message(query, packet, 12));
        for (unsigned int kind = 66; kind <= 135; kind += 69)
        {
            PerfBytes sample = trace_message(query, packet, kind);
            if (sample.data != NULL && query->valid)
            {
                trace_sample(query, sequence, packet, sample, kind);
            }
        }
    }
    query->valid = query->valid && query->total != 0;
}

#ifndef BUSTER_TRACE_QUERY_NO_MAIN
int main(int argc, char **argv)
{
    TraceQuery query = {.start = 0, .end = UINT64_MAX, .first = UINT64_MAX, .clock = 6, .valid = argc == 2 || argc == 4};
    if (argc == 4)
    {
        query.valid = perf_uint64(perf_literal(argv[2]), &query.start) && perf_uint64(perf_literal(argv[3]), &query.end) && query.start < query.end;
    }
    PerfBuffer input = {0};
    query.dictionary = calloc(BUSTER_TRACE_DICT_SLOTS, sizeof(*query.dictionary));
    query.valid = query.valid && query.dictionary != NULL && perf_read_file(argv[1], &input);
    if (query.valid)
    {
        trace_query(&query, perf_buffer_bytes(&input));
    }
    PerfBuffer output = {0};
    if (query.valid)
    {
        char header[768];
        int size = snprintf(header, sizeof(header), "{\"first_party\":true,\"query\":\"native sampled stacks; count and period totals; raw half-open nanoseconds\",\"clock_id\":%u,\"total_samples\":\"%llu\",\"selected_samples\":\"%llu\",\"selected_period_ns\":\"%llu\",\"selected_unweighted_samples\":\"%llu\",\"kernel_records_lost\":\"%llu\",\"skipped\":\"%llu\",\"first_ns\":\"%llu\",\"last_ns\":\"%llu\",\"frames\":[", query.clock, (unsigned long long)query.total, (unsigned long long)query.selected, (unsigned long long)query.period_ns, (unsigned long long)query.unweighted, (unsigned long long)query.lost, (unsigned long long)query.skipped, (unsigned long long)query.first, (unsigned long long)query.last);
        query.valid = size > 0 && (size_t)size < sizeof(header) && perf_buffer_append(&output, (PerfBytes){.data = (const unsigned char *)header, .size = size > 0 ? (size_t)size : 0});
        bool first = true;
        for (size_t i = 0; i < BUSTER_TRACE_DICT_SLOTS && query.valid; ++i)
        {
            TraceEntry *entry = &query.dictionary[i];
            if (entry->kind != 6 || entry->inclusive == 0)
            {
                continue;
            }
            TraceSequence sequence = {.id = entry->sequence, .epoch = entry->epoch};
            TraceEntry *name = trace_dictionary(&query, &sequence, 5, trace_integer(&query, entry->bytes, 2, 0), (PerfBytes){0});
            PerfBytes label = name != NULL ? trace_message(&query, name->bytes, 2) : perf_literal("[unresolved]");
            query.valid = query.valid && (first || perf_buffer_byte(&output, ',')) && perf_buffer_append(&output, perf_literal("{\"name\":")) && perf_json_quote(&output, label);
            first = false;
            char counts[128];
            size = snprintf(counts, sizeof(counts), ",\"inclusive_samples\":\"%llu\",\"self_samples\":\"%llu\"}", (unsigned long long)entry->inclusive, (unsigned long long)entry->self);
            query.valid = query.valid && size > 0 && (size_t)size < sizeof(counts) && perf_buffer_append(&output, (PerfBytes){.data = (const unsigned char *)counts, .size = size > 0 ? (size_t)size : 0});
        }
        query.valid = query.valid && perf_buffer_append(&output, perf_literal("]}\n"));
    }
    if (query.valid)
    {
        query.valid = fwrite(output.data, 1, output.size, stdout) == output.size;
    }
    else
    {
        fputs("trace-query: unsupported, missing, corrupt, oversized or unaligned native samples; no totals accepted\nusage: trace-query TRACE [START_NS END_NS]\n", stderr);
    }
    free(output.data);
    free(input.data);
    free(query.dictionary);
    return query.valid ? 0 : 1;
}
#endif
