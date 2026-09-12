BUSTER_GLOBAL_LOCAL ProcessResult compiler_run_benchmarks(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
    String8 input = BYTE_SLICE_TO_STRING(8, file_read(arena, S8("tests/basic_c_operations.c"), (FileReadOptions){0}));
    BUSTER_CHECK(input.length);
    enum { COPIES = 64, SAMPLES = 31 };
    String8 source = {arena_allocate(arena, char8, input.length * COPIES), input.length * COPIES};
    for (u32 copy = 0; copy < COPIES; copy += 1)
    {
        memcpy(source.pointer + copy * input.length, input.pointer, input.length);
    }
    c_prewarm();
    for (u32 scalar = 0; scalar < 2; scalar += 1)
    {
        u64 durations[SAMPLES];
        u64 tokens = 0;
        for (u32 sample = 0; sample <= SAMPLES; sample += 1)
        {
            u64 position = arena->position;
            TimeDataType start = timestamp_take();
            CLexResult lex = scalar ? c_lex_reference(arena, source) : c_lex(arena, source);
            TimeDataType end = timestamp_take();
            BUSTER_CHECK(!lex.diagnostic_count);
            tokens = lex.token_count;
            if (sample) durations[sample - 1] = timestamp_ns_between(start, end);
            arena_set_position(arena, position);
        }
        compiler_sort_u64(durations, SAMPLES);
        string_print(S8("BENCH_UTF8_ASCII scalar={u32} bytes={u64} tokens={u64} min_ns={u64} median_ns={u64}\n"),
                     scalar, source.length, tokens, durations[0], durations[SAMPLES / 2]);
    }
    arena_destroy(arena, 1);
    return PROCESS_RESULT_SUCCESS;
}
