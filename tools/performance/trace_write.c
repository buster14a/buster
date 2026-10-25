/* Original native Perfetto protobuf emission primitives. Fields follow the
 * published v58.2 wire contracts; no generated code or protobuf library.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
static bool trace_write_varint(PerfBuffer *output, uint64_t value)
{
    bool result = true;
    do
    {
        unsigned char byte = (unsigned char)((value & 127) | (value > 127 ? 128 : 0));
        result = perf_buffer_byte(output, byte);
        value >>= 7;
    } while (result && value != 0);
    return result;
}

static bool trace_write_number(PerfBuffer *output, uint32_t field, uint64_t value)
{
    bool result = field != 0 && field <= 0x1fffffff && trace_write_varint(output, (uint64_t)field << 3) && trace_write_varint(output, value);
    return result;
}

static bool trace_write_bytes(PerfBuffer *output, uint32_t field, PerfBytes bytes)
{
    bool result = field != 0 && field <= 0x1fffffff && trace_write_varint(output, ((uint64_t)field << 3) | 2) && trace_write_varint(output, bytes.size) && perf_buffer_append(output, bytes);
    return result;
}

static bool trace_write_message(PerfBuffer *output, uint32_t field, const PerfBuffer *message)
{
    bool result = trace_write_bytes(output, field, perf_buffer_bytes(message));
    return result;
}

static bool trace_write_packet(PerfBuffer *trace, uint64_t timestamp, uint32_t clock, uint32_t sequence, uint32_t flags, uint32_t field, const PerfBuffer *payload)
{
    PerfBuffer packet = {0};
    bool result = trace_write_number(&packet, 8, timestamp) && trace_write_number(&packet, 58, clock) && trace_write_number(&packet, 10, sequence) && trace_write_number(&packet, 13, flags) && trace_write_message(&packet, field, payload) && trace_write_message(trace, 1, &packet);
    free(packet.data);
    return result;
}
