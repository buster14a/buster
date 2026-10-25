/* Original bounded Protocol Buffers wire reader. Unknown fields are skipped
 * without guessing their message type. Native Trace/TracePacket envelopes are
 * checked here; supported profiling messages are decoded separately.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
typedef struct PerfProto PerfProto;
struct PerfProto
{
    PerfBytes bytes;
    size_t cursor;
    uint64_t integer;
    PerfBytes value;
    uint32_t field;
    unsigned int wire;
    bool valid;
};

static uint64_t perf_proto_varint(PerfProto *input)
{
    uint64_t result = 0;
    bool ended = false;
    for (unsigned int i = 0; i < 10 && input->valid && !ended; ++i)
    {
        input->valid = input->cursor < input->bytes.size;
        if (input->valid)
        {
            unsigned char byte = input->bytes.data[input->cursor++];
            input->valid = i != 9 || byte <= 1;
            result |= (uint64_t)(byte & 127) << (i * 7);
            ended = (byte & 128) == 0;
        }
    }
    input->valid = input->valid && ended;
    return result;
}

static bool perf_proto_next(PerfProto *input)
{
    bool result = input->valid && input->cursor < input->bytes.size;
    input->value = (PerfBytes){0};
    input->integer = 0;
    if (result)
    {
        uint64_t tag = perf_proto_varint(input);
        input->field = (uint32_t)(tag >> 3);
        input->wire = tag & 7;
        input->valid = input->valid && tag >> 3 <= 0x1fffffff && input->field != 0;
        if (input->valid && input->wire == 0)
        {
            input->integer = perf_proto_varint(input);
        }
        else if (input->valid && (input->wire == 1 || input->wire == 2 || input->wire == 5))
        {
            uint64_t size = input->wire == 2 ? perf_proto_varint(input) : input->wire == 1 ? 8 : 4;
            input->valid = input->valid && perf_range(input->bytes, input->cursor, size);
            if (input->valid)
            {
                input->value = (PerfBytes){.data = input->bytes.data + input->cursor, .size = (size_t)size};
                input->cursor += (size_t)size;
                if (input->wire != 2)
                {
                    PerfFlat scalar = {.bytes = input->value, .valid = true};
                    input->integer = perf_little(&scalar, 0, (unsigned int)size);
                }
            }
        }
        else
        {
            input->valid = false;
        }
        result = input->valid;
    }
    return result;
}

static bool perf_trace_envelope(PerfBytes bytes, uint64_t *packets)
{
    PerfProto trace = {.bytes = bytes, .valid = true};
    bool result = true;
    while (perf_proto_next(&trace) && result)
    {
        if (trace.field == 1)
        {
            result = trace.wire == 2 && *packets < BUSTER_PERF_ROW_LIMIT;
            PerfProto packet = {.bytes = trace.value, .valid = true};
            while (perf_proto_next(&packet))
            {
            }
            result = result && packet.valid;
            ++*packets;
        }
    }
    result = result && trace.valid && *packets != 0;
    return result;
}
