// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

static void stream_benchmark_test_cpus(StreamTestTally* tally)
{
    uint64_t allowed[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    StreamBenchmarkCpus placement = stream_benchmark_cpus(allowed, 8, 0);
    stream_test_expect(tally, placement.allowed_count == 0 && placement.lanes == 0 && !placement.writer_available, "no unknown CPU placement is measured", 0);
    allowed[1] = 1;
    placement = stream_benchmark_cpus(allowed, 8, 0);
    stream_test_expect(tally, placement.allowed_count == 1 && placement.lanes == 1 && placement.writer[0] == 64 && !placement.writer_available,
                       "one allowed CPU clamps lanes and omits the writer", 0);
    allowed[0] = UINT64_C(1) << 63;
    allowed[BUSTER_LANE_CPU_MASK_WORDS - 1] = UINT64_C(1) << 63;
    placement = stream_benchmark_cpus(allowed, 8, 0);
    stream_test_expect(tally, placement.allowed_count == 3 && placement.lanes == 3 && placement.writer[0] == 63 && placement.writer[1] == 64 && placement.writer_available,
                       "sparse CPU masks span words and bound the selected lanes", 0);
    const uint32_t pairs[][2] = { {64, 1023}, {63, 63}, {0, 64}, {64, 1024}, {UINT32_MAX, 64} };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(pairs); i += 1)
    {
        placement = stream_benchmark_cpus(allowed, 1, pairs[i]);
        stream_test_expect(tally, placement.lanes == 1 && placement.writer_available == (i == 0) && placement.writer[0] == pairs[i][0] && placement.writer[1] == pairs[i][1],
                           "explicit CPUs retain the pair but never widen inherited affinity", 0);
    }
    const char* const invalid[] = { "", "1", "1,", ",1", "1,2,3", "1,2x", "-1,2", "+1,2", "1, 2", "1024,2", "2,1024", "4294967296,2", "2,18446744073709551616" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        uint32_t parsed[2];
        stream_test_expect(tally, !stream_benchmark_parse_cpus(invalid[i], parsed), "malformed or overflowing CPU pair is rejected", invalid[i]);
    }
    uint32_t parsed[2] = { 0 };
    stream_test_expect(tally, stream_benchmark_parse_cpus("0,1023", parsed) && parsed[0] == 0 && parsed[1] == 1023, "CPU pair includes both mask boundaries", 0);
}
