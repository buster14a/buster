// Bounded diagnostic reader for Buster bootstrap selected-MIR trace version 1.
// This exposes stored rows only; it does not run or modify compiler inputs.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct MirTraceReader MirTraceReader;
struct MirTraceReader
{
    FILE* file;
    uint64_t remaining;
    int valid;
};

static uint64_t mir_trace_u64(MirTraceReader* reader)
{
    uint64_t result = 0;
    unsigned char bytes[8];
    if (reader->valid && reader->remaining >= sizeof(bytes) && fread(bytes, 1, sizeof(bytes), reader->file) == sizeof(bytes))
    {
        reader->remaining -= sizeof(bytes);
        for (unsigned int index = 0; index < sizeof(bytes); ++index)
        {
            result |= (uint64_t)bytes[index] << (index * 8u);
        }
    }
    else
    {
        reader->valid = 0;
    }
    return result;
}

static void mir_trace_skip_words(MirTraceReader* reader, uint64_t count)
{
    if (reader->valid && count <= reader->remaining / 8u)
    {
        uint64_t bytes = count * 8u;
        // The complete input has already been bounded below LONG_MAX.
        if (fseek(reader->file, (long)bytes, SEEK_CUR) == 0)
        {
            reader->remaining -= bytes;
        }
        else
        {
            reader->valid = 0;
        }
    }
    else
    {
        reader->valid = 0;
    }
    return;
}

static void mir_trace_string(MirTraceReader* reader, uint64_t length, char* text, unsigned int capacity)
{
    if (reader->valid && capacity && length < capacity && length <= reader->remaining &&
        fread(text, 1, (size_t)length, reader->file) == length)
    {
        reader->remaining -= length;
        text[length] = 0;
    }
    else
    {
        reader->valid = 0;
    }
    return;
}

static void mir_trace_table(MirTraceReader* reader, unsigned int width, int print, char const* label)
{
    uint64_t count = mir_trace_u64(reader);
    if (reader->valid && width && count <= reader->remaining / 8u / width)
    {
        if (print)
        {
            printf("RAD_SDK_MIR_TABLE kind=%s count=%llu width=%u\n", label, (unsigned long long)count, width);
        }
        for (uint64_t index = 0; reader->valid && index < count; ++index)
        {
            if (print && index < 256u)
            {
                printf("RAD_SDK_MIR_ROW kind=%s index=%llu", label, (unsigned long long)index);
                for (unsigned int field = 0; field < width; ++field)
                {
                    uint64_t value = mir_trace_u64(reader);
                    printf(" f%u=%llu", field, (unsigned long long)value);
                }
                printf("\n");
            }
            else
            {
                mir_trace_skip_words(reader, width);
            }
        }
    }
    else
    {
        reader->valid = 0;
    }
    return;
}

int main(int argc, char** argv)
{
    int result = 2;
    FILE* file = 0;
    if (argc == 2)
    {
#if defined(_MSC_VER)
        if (fopen_s(&file, argv[1], "rb") != 0)
        {
            file = 0;
        }
#else
        file = fopen(argv[1], "rb");
#endif
    }
    if (file)
    {
        int sized = fseek(file, 0, SEEK_END) == 0;
        long length = sized ? ftell(file) : -1;
        MirTraceReader reader = {.file = file, .remaining = length > 0 ? (uint64_t)length : 0,
                                 .valid = length > 0 && length <= 64L * 1024L * 1024L && fseek(file, 0, SEEK_SET) == 0};
        char text[256] = {0};
        mir_trace_string(&reader, mir_trace_u64(&reader), text, sizeof(text));
        reader.valid = reader.valid && strcmp(text, "BUSTER bootstrap trace v1") == 0;
        mir_trace_string(&reader, mir_trace_u64(&reader), text, sizeof(text));
        reader.valid = reader.valid && strcmp(text, "selected MIR") == 0;
        uint64_t cpu = mir_trace_u64(&reader);
        uint64_t allocator = mir_trace_u64(&reader);
        uint64_t pic = mir_trace_u64(&reader);
        printf("RAD_SDK_MIR_HEADER cpu=%llu allocator=%llu pic=%llu\n", (unsigned long long)cpu,
               (unsigned long long)allocator, (unsigned long long)pic);
        unsigned int selected_count = 0;
        int inner_seen = 0;
        int outer_seen = 0;
        int found_end = 0;
        while (reader.valid && reader.remaining)
        {
            uint64_t name_length = mir_trace_u64(&reader);
            if (name_length == UINT64_C(0x31444e4552545342))
            {
                found_end = 1;
                reader.valid = reader.valid && reader.remaining == 0;
                break;
            }
            mir_trace_string(&reader, name_length, text, sizeof(text));
            int selected = strcmp(text, "debuggee_inner") == 0 || strcmp(text, "debuggee_outer") == 0;
            uint64_t function = mir_trace_u64(&reader);
            uint64_t supported = mir_trace_u64(&reader);
            uint64_t failed_opcode = mir_trace_u64(&reader);
            if (selected && reader.valid)
            {
                selected_count += 1u;
                if (strcmp(text, "debuggee_inner") == 0)
                {
                    reader.valid = !inner_seen;
                    inner_seen = 1;
                }
                else
                {
                    reader.valid = !outer_seen;
                    outer_seen = 1;
                }
                printf("RAD_SDK_MIR_FUNCTION name=%s id=%llu supported=%llu failed_opcode=%llu\n", text,
                       (unsigned long long)function, (unsigned long long)supported, (unsigned long long)failed_opcode);
            }
            if (supported)
            {
                uint64_t error = mir_trace_u64(&reader);
                mir_trace_skip_words(&reader, 4u);
                if (error == 0)
                {
                    mir_trace_skip_words(&reader, 2u);
                    mir_trace_table(&reader, 7u, selected, "instructions");
                    mir_trace_table(&reader, 6u, selected, "virtual-registers");
                    mir_trace_table(&reader, 9u, selected, "blocks");
                    mir_trace_table(&reader, 5u, 0, "edges");
                    mir_trace_table(&reader, 2u, selected, "block-parameters");
                    mir_trace_table(&reader, 3u, 0, "switch-cases");
                    mir_trace_table(&reader, 2u, selected, "line-marks");
                    mir_trace_table(&reader, 1u, 0, "edge-copy-sources");
                    mir_trace_table(&reader, 1u, selected, "immediates");
                    uint64_t stack_slots = mir_trace_u64(&reader);
                    if (stack_slots <= reader.remaining / 16u)
                    {
                        mir_trace_skip_words(&reader, stack_slots * 2u);
                    }
                    else
                    {
                        reader.valid = 0;
                    }
                    mir_trace_table(&reader, 2u, 0, "call-targets");
                    // Seven scalar fields plus four fixed five-field ABI parts.
                    mir_trace_table(&reader, 27u, 0, "va-args");
                }
                else if (selected)
                {
                    printf("RAD_SDK_MIR_VERIFY error=%llu\n", (unsigned long long)error);
                }
            }
        }
        result = reader.valid && found_end && inner_seen && outer_seen ? 0 : 2;
        printf("RAD_SDK_MIR_RESULT valid=%d ended=%d selected=%u inner=%d outer=%d\n",
               reader.valid, found_end, selected_count, inner_seen, outer_seen);
        if (fclose(file) != 0)
        {
            result = 2;
        }
    }
    else
    {
        fprintf(stderr, "usage: raddebugger_trace_dump TRACE.mir\n");
    }
    return result;
}
