/* Original Linux perf record -> native Perfetto StackSample adapter.
 * Native records/configuration remain authoritative source artifacts. Exact
 * sample periods become native primary_weight, never cumulative timebase_count.
 * Layout: raw records; verified ELF frames; native dictionaries and samples.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#define main buster_performance_library_cli
#include "main.c"
#undef main
#include <linux/perf_event.h>
#include "trace_write.c"
#include "elf.c"

#define BUSTER_PROFILE_SAMPLE_LIMIT 1000000u
#define BUSTER_PROFILE_FRAME_LIMIT 2000000u

typedef struct ProfileMap ProfileMap;
struct ProfileMap
{
    uint32_t pid;
    uint64_t start;
    uint64_t end;
    uint64_t offset;
    uint64_t time;
    uint64_t load_bias;
    PerfBytes path;
    bool verified_binary;
};

typedef struct ProfileFrameKey ProfileFrameKey;
struct ProfileFrameKey
{
    uint32_t mapping;
    uint64_t ip;
};

typedef struct ProfileSample ProfileSample;
struct ProfileSample
{
    uint64_t time;
    uint64_t period;
    uint32_t pid;
    uint32_t tid;
    uint32_t cpu;
    uint32_t first_frame;
    uint32_t frame_count;
    unsigned int mode;
};

typedef struct ProfileRecords ProfileRecords;
typedef struct ProfileSwitch ProfileSwitch;
struct ProfileSwitch
{
    uint64_t time;
    uint32_t pid;
    uint32_t tid;
    uint32_t cpu;
    bool out;
};

struct ProfileRecords
{
    ProfileMap *maps;
    size_t map_count;
    ProfileSample *samples;
    size_t sample_count;
    ProfileFrameKey *frames;
    size_t frame_count;
    uint64_t lost_records;
    uint64_t lost_samples;
    uint64_t throttles;
    uint64_t switches;
    ProfileSwitch *switch_events;
    size_t switch_count;
    uint64_t running_intervals;
    uint64_t running_ns;
    uint64_t unpaired_switches;
    uint64_t forks;
    uint64_t exits;
    uint64_t ip_only;
    uint64_t record_count;
    uint64_t mapping_probes;
    bool work_limit;
};

static bool profile_read_member(const char *directory, const char *name, PerfBuffer *bytes)
{
    bool result = perf_read_below(directory, perf_literal(name), bytes);
    return result;
}

static bool profile_write_file(const char *path, PerfBytes bytes)
{
    FILE *file = fopen(path, "wb");
    bool result = file != NULL;
    if (file != NULL)
    {
        result = fwrite(bytes.data, 1, bytes.size, file) == bytes.size;
        result = fclose(file) == 0 && result;
    }
    return result;
}

static uint32_t profile_mapping(ProfileRecords *records, uint32_t pid, uint64_t time, uint64_t ip)
{
    uint32_t result = UINT32_MAX;
    for (uint32_t i = 0; i < records->map_count; ++i)
    {
        if (++records->mapping_probes > UINT64_C(64000000))
        {
            records->work_limit = true;
            break;
        }
        const ProfileMap *map = &records->maps[i];
        if (map->pid == pid && map->time <= time && ip >= map->start && ip < map->end && (result == UINT32_MAX || map->time >= records->maps[result].time))
        {
            result = i;
        }
    }
    return result;
}

static bool profile_raw_records(PerfBytes bytes, ProfileRecords *records)
{
    records->maps = calloc(BUSTER_PERF_FIELD_LIMIT, sizeof(*records->maps));
    size_t sample_capacity = bytes.size / 56 + 1;
    size_t frame_capacity = bytes.size / 8 + 1;
    sample_capacity = sample_capacity < BUSTER_PROFILE_SAMPLE_LIMIT ? sample_capacity : BUSTER_PROFILE_SAMPLE_LIMIT;
    frame_capacity = frame_capacity < BUSTER_PROFILE_FRAME_LIMIT ? frame_capacity : BUSTER_PROFILE_FRAME_LIMIT;
    records->samples = calloc(sample_capacity, sizeof(*records->samples));
    records->frames = calloc(frame_capacity, sizeof(*records->frames));
    size_t switch_capacity = bytes.size / 32 + 1;
    switch_capacity = switch_capacity < BUSTER_PROFILE_SAMPLE_LIMIT ? switch_capacity : BUSTER_PROFILE_SAMPLE_LIMIT;
    records->switch_events = calloc(switch_capacity, sizeof(*records->switch_events));
    bool result = records->maps != NULL && records->samples != NULL && records->frames != NULL && records->switch_events != NULL;
    /* Map timestamps are resolved before samples. Inherited tasks can produce
     * records in an order that differs from timestamp order. */
    for (unsigned int pass = 0; pass < 2 && result; ++pass)
    {
        size_t cursor = 0;
        while (cursor < bytes.size && result)
        {
            PerfFlat raw = {.bytes = bytes, .valid = true};
            uint32_t type = (uint32_t)perf_little(&raw, cursor, 4);
            unsigned int misc = (unsigned int)perf_little(&raw, cursor + 4, 2);
            uint64_t size = perf_little(&raw, cursor + 6, 2);
            result = raw.valid && size >= 8 && perf_range(bytes, cursor, size);
            if (!result)
            {
                break;
            }
            PerfFlat record = {.bytes = {.data = bytes.data + cursor, .size = (size_t)size}, .valid = true};
            if (pass == 0 && type == PERF_RECORD_MMAP2)
            {
                result = size >= 72 + 24 + 1 && records->map_count < BUSTER_PERF_FIELD_LIMIT;
                if (result)
                {
                    ProfileMap *map = &records->maps[records->map_count++];
                    map->pid = (uint32_t)perf_little(&record, 8, 4);
                    map->start = perf_little(&record, 16, 8);
                    uint64_t length = perf_little(&record, 24, 8);
                    map->offset = perf_little(&record, 32, 8);
                    map->time = perf_little(&record, size - 16, 8);
                    result = record.valid && length <= UINT64_MAX - map->start;
                    map->end = map->start + length;
                    const unsigned char *name = record.bytes.data + 72;
                    const unsigned char *end = memchr(name, 0, (size_t)size - 72 - 24);
                    result = result && end != NULL;
                    if (result)
                    {
                        map->path = (PerfBytes){.data = name, .size = (size_t)(end - name)};
                        result = perf_utf8(map->path);
                    }
                }
            }
            if (pass == 1)
            {
                ++records->record_count;
                if (type == PERF_RECORD_LOST)
                {
                    uint64_t lost = perf_little(&record, 16, 8);
                    result = record.valid && lost <= UINT64_MAX - records->lost_records;
                    records->lost_records += result ? lost : 0;
                }
                else if (type == PERF_RECORD_LOST_SAMPLES)
                {
                    uint64_t lost = perf_little(&record, 8, 8);
                    result = record.valid && lost <= UINT64_MAX - records->lost_samples;
                    records->lost_samples += result ? lost : 0;
                }
                else if (type == PERF_RECORD_THROTTLE)
                {
                    ++records->throttles;
                }
                else if (type == PERF_RECORD_SWITCH || type == PERF_RECORD_SWITCH_CPU_WIDE)
                {
                    ++records->switches;
                    result = size >= 32 && records->switch_count < switch_capacity;
                    if (result)
                    {
                        ProfileSwitch *event = &records->switch_events[records->switch_count++];
                        event->pid = (uint32_t)perf_little(&record, size - 24, 4);
                        event->tid = (uint32_t)perf_little(&record, size - 20, 4);
                        event->time = perf_little(&record, size - 16, 8);
                        event->cpu = (uint32_t)perf_little(&record, size - 8, 4);
                        event->out = (misc & PERF_RECORD_MISC_SWITCH_OUT) != 0;
                    }
                }
                else if (type == PERF_RECORD_FORK)
                {
                    ++records->forks;
                }
                else if (type == PERF_RECORD_EXIT)
                {
                    ++records->exits;
                }
                else if (type == PERF_RECORD_SAMPLE)
                {
                    result = size >= 56 && records->sample_count < sample_capacity;
                    if (result)
                    {
                        ProfileSample *sample = &records->samples[records->sample_count++];
                        uint64_t ip = perf_little(&record, 8, 8);
                        sample->pid = (uint32_t)perf_little(&record, 16, 4);
                        sample->tid = (uint32_t)perf_little(&record, 20, 4);
                        sample->time = perf_little(&record, 24, 8);
                        sample->cpu = (uint32_t)perf_little(&record, 32, 4);
                        sample->period = perf_little(&record, 40, 8);
                        unsigned int source_mode = misc & PERF_RECORD_MISC_CPUMODE_MASK;
                        sample->mode = source_mode == PERF_RECORD_MISC_USER ? 1 : source_mode == PERF_RECORD_MISC_KERNEL ? 2 : source_mode == PERF_RECORD_MISC_HYPERVISOR ? 3 : source_mode == PERF_RECORD_MISC_GUEST_USER ? 4 : source_mode == PERF_RECORD_MISC_GUEST_KERNEL ? 5 : 0;
                        uint64_t count = perf_little(&record, 48, 8);
                        result = record.valid && count <= 4096 && count <= (size - 56) / 8 && count * 8 + 56 == size;
                        sample->first_frame = (uint32_t)records->frame_count;
                        for (uint64_t i = count; i-- > 0 && result;)
                        {
                            uint64_t address = perf_little(&record, 56 + i * 8, 8);
                            if (address >= (uint64_t)PERF_CONTEXT_MAX)
                            {
                                continue;
                            }
                            result = record.valid && records->frame_count < frame_capacity && sample->frame_count < BUSTER_PERF_DEPTH_LIMIT;
                            if (result)
                            {
                                records->frames[records->frame_count++] = (ProfileFrameKey){.mapping = profile_mapping(records, sample->pid, sample->time, address), .ip = address};
                                ++sample->frame_count;
                            }
                        }
                        if (result && sample->frame_count == 0)
                        {
                            result = records->frame_count < frame_capacity;
                            if (result)
                            {
                                records->frames[records->frame_count++] = (ProfileFrameKey){.mapping = profile_mapping(records, sample->pid, sample->time, ip), .ip = ip};
                                sample->frame_count = 1;
                            }
                        }
                        records->ip_only += sample->frame_count == 1;
                    }
                }
            }
            result = result && record.valid && !records->work_limit;
            cursor += (size_t)size;
        }
        result = result && cursor == bytes.size;
    }
    return result;
}

static int profile_frame_compare(const void *left, const void *right)
{
    const ProfileFrameKey *a = left;
    const ProfileFrameKey *b = right;
    int result = a->mapping < b->mapping ? -1 : a->mapping > b->mapping ? 1 : a->ip < b->ip ? -1 : a->ip > b->ip ? 1 : 0;
    return result;
}

static int profile_switch_compare(const void *left, const void *right)
{
    const ProfileSwitch *a = left;
    const ProfileSwitch *b = right;
    int result = a->pid < b->pid ? -1 : a->pid > b->pid ? 1 : a->tid < b->tid ? -1 : a->tid > b->tid ? 1 : a->time < b->time ? -1 : a->time > b->time ? 1 : a->out == b->out ? 0 : a->out ? -1 : 1;
    return result;
}

static bool profile_running(ProfileRecords *records, PerfBuffer *trace)
{
    if (records->switch_count > 1)
    {
        qsort(records->switch_events, records->switch_count, sizeof(*records->switch_events), profile_switch_compare);
    }
    const ProfileSwitch *pending = NULL;
    uint64_t track = 0;
    bool result = true;
    for (size_t i = 0; i < records->switch_count && result; ++i)
    {
        const ProfileSwitch *event = &records->switch_events[i];
        bool new_task = i == 0 || event->pid != records->switch_events[i - 1].pid || event->tid != records->switch_events[i - 1].tid;
        if (new_task)
        {
            records->unpaired_switches += pending != NULL;
            pending = NULL;
            ++track;
            char name[160];
            snprintf(name, sizeof(name), "Observed running: PID %u TID %u (paired Linux perf switches)", event->pid, event->tid);
            PerfBuffer descriptor = {0};
            result = trace_write_number(&descriptor, 1, track) && trace_write_bytes(&descriptor, 2, perf_literal(name)) && trace_write_packet(trace, event->time, 3, 2, 0, 60, &descriptor);
            free(descriptor.data);
        }
        if (!event->out)
        {
            records->unpaired_switches += pending != NULL;
            pending = event;
        }
        else if (pending != NULL && pending->cpu == event->cpu && event->time > pending->time)
        {
            uint64_t duration = event->time - pending->time;
            result = duration <= UINT64_MAX - records->running_ns;
            char name[96];
            snprintf(name, sizeof(name), "Observed running on CPU %u; initial/final gaps unknown", event->cpu);
            PerfBuffer begin = {0};
            PerfBuffer end = {0};
            result = result && trace_write_number(&begin, 9, 1) && trace_write_number(&begin, 11, track) && trace_write_bytes(&begin, 23, perf_literal(name)) && trace_write_bytes(&begin, 22, perf_literal("Linux perf context switches")) && trace_write_packet(trace, pending->time, 3, 2, 0, 11, &begin) && trace_write_number(&end, 9, 2) && trace_write_number(&end, 11, track) && trace_write_packet(trace, event->time, 3, 2, 0, 11, &end);
            free(begin.data);
            free(end.data);
            records->running_ns += result ? duration : 0;
            ++records->running_intervals;
            pending = NULL;
        }
        else
        {
            records->unpaired_switches += 1 + (pending != NULL);
            pending = NULL;
        }
    }
    records->unpaired_switches += pending != NULL;
    return result;
}

static bool profile_native_trace(ProfileRecords *records, const ProfileElf *elf, uint64_t start, const uint64_t clocks[3], PerfBuffer *output, uint64_t *symbolized)
{
    ProfileFrameKey *unique = calloc(records->frame_count + 1, sizeof(*unique));
    bool result = unique != NULL;
    if (result && records->frame_count != 0)
    {
        memcpy(unique, records->frames, records->frame_count * sizeof(*unique));
        qsort(unique, records->frame_count, sizeof(*unique), profile_frame_compare);
    }
    size_t unique_count = 0;
    for (size_t i = 0; i < records->frame_count && result; ++i)
    {
        if (unique_count == 0 || profile_frame_compare(&unique[unique_count - 1], &unique[i]) != 0)
        {
            unique[unique_count++] = unique[i];
        }
    }
    PerfBuffer interned = {0};
    if (elf->build_id.size != 0)
    {
        PerfBuffer build = {0};
        result = result && trace_write_number(&build, 1, 1) && trace_write_bytes(&build, 2, elf->build_id) && trace_write_message(&interned, 16, &build);
        free(build.data);
    }
    for (size_t i = 0; i < records->map_count && result; ++i)
    {
        ProfileMap *map = &records->maps[i];
        map->verified_binary = perf_bytes_equal(map->path, perf_literal(elf->path));
        uint64_t ignored = 0;
        if (map->verified_binary)
        {
            /* The first actual frame in this segment establishes the ELF
             * address translation; page-prefix mapping offsets need not fall
             * inside the executable PT_LOAD's file bytes themselves. */
            for (size_t f = 0; f < unique_count; ++f)
            {
                result = result && ++records->mapping_probes <= UINT64_C(64000000);
                if (!result)
                {
                    break;
                }
                if (unique[f].mapping == i)
                {
                    uint64_t relative = unique[f].ip - map->start;
                    bool address_valid = relative <= UINT64_MAX - map->offset && profile_elf_pc(elf, map->offset + relative, &ignored, &map->load_bias);
                    map->verified_binary = map->verified_binary && address_valid;
                }
            }
        }
        PerfBuffer path = {0};
        PerfBuffer mapping = {0};
        result = result && trace_write_number(&path, 1, i + 1) && trace_write_bytes(&path, 2, map->path) && trace_write_message(&interned, 17, &path);
        result = result && trace_write_number(&mapping, 1, i + 1) && trace_write_number(&mapping, 3, 0) && trace_write_number(&mapping, 4, map->start) && trace_write_number(&mapping, 5, map->end) && trace_write_number(&mapping, 8, map->offset) && trace_write_number(&mapping, 7, i + 1);
        if (map->verified_binary)
        {
            result = result && trace_write_number(&mapping, 6, map->load_bias) && (elf->build_id.size == 0 || trace_write_number(&mapping, 2, 1));
        }
        result = result && trace_write_message(&interned, 19, &mapping);
        free(path.data);
        free(mapping.data);
    }
    for (size_t i = 0; i < unique_count && result; ++i)
    {
        const ProfileFrameKey *key = &unique[i];
        const ProfileMap *map = key->mapping < records->map_count ? &records->maps[key->mapping] : NULL;
        uint64_t virtual_pc = 0;
        uint64_t bias = 0;
        const ProfileElfSymbol *symbol = NULL;
        if (map != NULL && map->verified_binary && key->ip - map->start <= UINT64_MAX - map->offset && profile_elf_pc(elf, map->offset + key->ip - map->start, &virtual_pc, &bias))
        {
            symbol = profile_elf_symbol(elf, virtual_pc);
        }
        char address[64];
        snprintf(address, sizeof(address), "[unresolved 0x%llx]", (unsigned long long)key->ip);
        PerfBytes label = symbol == NULL ? perf_literal(address) : symbol->name;
        *symbolized += symbol != NULL;
        PerfBuffer name = {0};
        PerfBuffer frame = {0};
        result = trace_write_number(&name, 1, i + 1) && trace_write_bytes(&name, 2, label) && trace_write_message(&interned, 5, &name) && trace_write_number(&frame, 1, i + 1) && trace_write_number(&frame, 2, i + 1) && trace_write_number(&frame, 7, 1);
        if (map != NULL && map->verified_binary)
        {
            result = result && trace_write_number(&frame, 3, key->mapping + 1) && trace_write_number(&frame, 4, virtual_pc);
        }
        result = result && trace_write_message(&interned, 6, &frame);
        free(name.data);
        free(frame.data);
    }
    PerfBuffer descriptor = {0};
    result = result && trace_write_number(&descriptor, 1, 1) && trace_write_bytes(&descriptor, 2, perf_literal("CPU-clock sample-period weight")) && trace_write_number(&descriptor, 3, 1) && trace_write_number(&descriptor, 5, 1) && trace_write_bytes(&descriptor, 6, perf_literal("Kernel-reported per-sample CPU-clock periods on user-mode observations; statistical attribution, not invocation counts or instrumented durations.")) && trace_write_number(&descriptor, 7, 1) && trace_write_message(&interned, 50, &descriptor);
    free(descriptor.data);
    for (size_t s = 0; s < records->sample_count && result; ++s)
    {
        const ProfileSample *sample = &records->samples[s];
        PerfBuffer callstack = {0};
        result = trace_write_number(&callstack, 1, s + 1);
        for (uint32_t f = 0; f < sample->frame_count && result; ++f)
        {
            const ProfileFrameKey *key = &records->frames[sample->first_frame + f];
            const ProfileFrameKey *found = bsearch(key, unique, unique_count, sizeof(*unique), profile_frame_compare);
            result = found != NULL && trace_write_number(&callstack, 2, (uint64_t)(found - unique) + 1);
        }
        result = result && trace_write_message(&interned, 7, &callstack);
        free(callstack.data);
    }
    /* All source timestamps use CLOCK_MONOTONIC. Make that the native trace's
     * authoritative clock instead of inventing a BOOTTIME conversion. */
    PerfBuffer snapshot = {0};
    PerfBuffer clock_packet = {0};
    const uint32_t clock_ids[3] = {3, 6, 1};
    for (size_t i = 0; i < 3 && result; ++i)
    {
        PerfBuffer clock = {0};
        result = trace_write_number(&clock, 1, clock_ids[i]) && trace_write_number(&clock, 2, clocks[i]) && trace_write_message(&snapshot, 1, &clock);
        free(clock.data);
    }
    result = result && trace_write_number(&snapshot, 2, 3) && trace_write_message(&clock_packet, 6, &snapshot) && trace_write_message(output, 1, &clock_packet);
    free(snapshot.data);
    free(clock_packet.data);
    result = result && trace_write_packet(output, start, 3, 1, 1, 12, &interned);
    free(interned.data);
    for (size_t s = 0; s < records->sample_count && result; ++s)
    {
        const ProfileSample *sample = &records->samples[s];
        PerfBuffer task = {0};
        PerfBuffer execution = {0};
        PerfBuffer point = {0};
        result = trace_write_number(&task, 2, sample->pid) && trace_write_number(&task, 3, sample->tid) && trace_write_number(&execution, 2, sample->cpu) && trace_write_number(&execution, 3, sample->mode) && trace_write_message(&point, 1, &task) && trace_write_message(&point, 3, &execution) && trace_write_number(&point, 6, s + 1) && trace_write_number(&point, 10, 1) && trace_write_number(&point, 11, sample->period);
        if (sample->frame_count == 1)
        {
            result = result && trace_write_bytes(&point, 7, perf_literal("Only an instruction-pointer frame was retained; complete unwinding is not established."));
        }
        result = result && trace_write_packet(output, sample->time, 3, 1, 2, 135, &point);
        free(task.data);
        free(execution.data);
        free(point.data);
    }
    if (result && records->lost_records != 0)
    {
        PerfBuffer loss = {0};
        result = trace_write_number(&loss, 17, records->lost_records) && trace_write_packet(output, start, 3, 1, 2, 66, &loss);
        free(loss.data);
    }
    result = result && profile_running(records, output);
    free(unique);
    return result;
}

int main(int argc, char **argv)
{
    bool result = argc == 4;
    if (!result)
    {
        fputs("usage: profile CAPTURE_DIRECTORY MATCHING_BINARY OUTPUT.pftrace\n", stderr);
    }
    PerfBuffer attributes_bytes = {0};
    PerfBuffer raw = {0};
    PerfBuffer receipt_bytes = {0};
    PerfJson receipt = {0};
    PerfBuffer clock_bytes = {0};
    PerfJson clock = {0};
    uint64_t clocks[3] = {0};
    struct perf_event_attr attributes = {0};
    ProfileElf elf = {0};
    ProfileRecords records = {0};
    PerfBuffer trace = {0};
    if (result)
    {
        result = profile_read_member(argv[1], "event-attributes.bin", &attributes_bytes) && attributes_bytes.size >= 112 && attributes_bytes.size <= sizeof(attributes) && profile_read_member(argv[1], "perf-event-records.bin", &raw) && profile_read_member(argv[1], "execution.json", &receipt_bytes) && perf_json_parse(&receipt, perf_buffer_bytes(&receipt_bytes)) && profile_read_member(argv[1], "clock-snapshot.json", &clock_bytes) && perf_json_parse(&clock, perf_buffer_bytes(&clock_bytes));
        const char *clock_names[3] = {"monotonic_ns", "boottime_ns", "realtime_ns"};
        for (size_t i = 0; i < 3 && result; ++i)
        {
            result = perf_uint64(perf_json_text(&clock, perf_json_find(&clock, 0, clock_names[i])), &clocks[i]) && clocks[i] != 0;
        }
    }
    if (result)
    {
        memcpy(&attributes, attributes_bytes.data, attributes_bytes.size);
        uint64_t expected_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD | PERF_SAMPLE_CALLCHAIN;
        result = attributes.size == attributes_bytes.size && attributes.type == PERF_TYPE_SOFTWARE && attributes.config == PERF_COUNT_SW_CPU_CLOCK && attributes.sample_type == expected_type && attributes.sample_id_all && attributes.mmap2 && attributes.exclude_kernel && attributes.exclude_callchain_kernel && attributes.use_clockid && attributes.clockid == 1;
        result = result && profile_elf_load(argv[2], &elf);
        uint32_t binary = perf_json_find(&receipt, 0, "binary");
        result = result && perf_json_is(&receipt, perf_json_find(&receipt, binary, "sha256"), elf.sha256) && perf_json_is(&receipt, perf_json_find(&receipt, binary, "unchanged_after_execution"), "true") && perf_json_is(&receipt, perf_json_find(&receipt, 0, "collector_complete"), "true") && perf_json_is(&receipt, perf_json_find(&receipt, 0, "profiled"), "true") && perf_json_is(&receipt, perf_json_find(&receipt, 0, "exit_code"), "0");
    }
    uint64_t start = 0;
    uint64_t symbolized = 0;
    result = result && perf_uint64(perf_json_text(&receipt, perf_json_find(&receipt, 0, "start_ns")), &start) && profile_raw_records(perf_buffer_bytes(&raw), &records) && records.sample_count != 0 && profile_native_trace(&records, &elf, start, clocks, &trace, &symbolized);
    uint64_t packets = 0;
    result = result && perf_trace_envelope(perf_buffer_bytes(&trace), &packets) && profile_write_file(argv[3], perf_buffer_bytes(&trace));
    if (result)
    {
        char digest[65];
        result = perf_sha256(perf_buffer_bytes(&trace), digest);
        if (result)
        {
            printf("{\"first_party\":true,\"native_format\":\"Perfetto v58.2 StackSample\",\"samples\":\"%zu\",\"source_records\":\"%llu\",\"frame_occurrences\":\"%zu\",\"symbolized_unique_frames\":\"%llu\",\"ip_only_samples\":\"%llu\",\"lost_records\":\"%llu\",\"lost_samples\":\"%llu\",\"throttle_records\":\"%llu\",\"source_switch_records\":\"%llu\",\"switches_exported\":true,\"paired_running_intervals\":\"%llu\",\"paired_running_ns\":\"%llu\",\"unpaired_switch_edges\":\"%llu\",\"trace_sha256\":\"%s\",\"binary_sha256\":\"%s\"}\n", records.sample_count, (unsigned long long)records.record_count, records.frame_count, (unsigned long long)symbolized, (unsigned long long)records.ip_only, (unsigned long long)records.lost_records, (unsigned long long)records.lost_samples, (unsigned long long)records.throttles, (unsigned long long)records.switches, (unsigned long long)records.running_intervals, (unsigned long long)records.running_ns, (unsigned long long)records.unpaired_switches, digest, elf.sha256);
        }
    }
    else
    {
        fputs("profile: unsupported/corrupt source, unmatched binary, failed workload or no samples; no qualified native trace\n", stderr);
    }
    free(records.maps);
    free(records.samples);
    free(records.frames);
    free(records.switch_events);
    free(trace.data);
    profile_elf_free(&elf);
    perf_json_free(&receipt);
    perf_json_free(&clock);
    free(clock_bytes.data);
    free(receipt_bytes.data);
    free(attributes_bytes.data);
    free(raw.data);
    return result ? 0 : 1;
}
