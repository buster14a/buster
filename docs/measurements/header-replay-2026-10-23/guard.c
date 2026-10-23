// Exact-sized, protected-page tails for the transcript location-copy kernel.
#define main unused_probe_main
#include "probe.c"
#undef main

#ifndef BUSTER_GUARD_MAIN
#define BUSTER_GUARD_MAIN main
#endif
int BUSTER_GUARD_MAIN(void)
{
    static InternTable table;
    static Preprocessor pp;
    PPOptions options = { .language = { .standard = 2017, .gnu = true } };
    bool ok = intern_initialize(&table) && pp_initialize(&pp, &options, &table) && pp_run_prelude(&pp);
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, (size_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ok = ok && mapping != MAP_FAILED && mprotect(mapping + page, (size_t)page, PROT_NONE) == 0;
    const uint32_t bases[5] = { 100, 200, 300, 400, 500 };
    ReplayRead reads[1] = { { .name = C_NAME_FILE_MACRO } };
    pp.macros[pp.macro_of[C_NAME_FILE_MACRO]].base = 600;
    ReplayTranscript transcript = { .range_count = 4, .scratch_count = 1, .read_count = 1, .reads = reads };
    uint64_t checked = 0;
    for (uint32_t shape = 0; ok && shape < 4; shape += 1)
    {
        for (uint32_t count = 1; ok && count <= 65; count += 1)
        {
            ReplayLocation* locations = (ReplayLocation*)(mapping + page - count * sizeof(ReplayLocation));
            const uint32_t sources[9] = { 0, 1, 2, 3, 0x40000001, 0x80000001, UINT32_MAX - 1, UINT32_MAX - 2, UINT32_MAX - 3 };
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t source = shape == 0 ? 0 : shape == 1 ? 0x40000001 : sources[(i + shape) % BUSTER_ARRAY_LENGTH(sources)];
                locations[i] = (ReplayLocation){ .source = source, .offset = source == UINT32_MAX - 1 ? 0 : i % 37 };
            }
            for (uint32_t at = 0; ok && at < count; at += 16)
            {
                const uint32_t take = count - at < 16 ? count - at : 16;
                const __m512i vector = replay_load_locations(&pp, &transcript, bases, locations + at, take);
                uint32_t actual[16];
                _mm512_storeu_si512(actual, vector);
                for (uint32_t lane = 0; lane < take; lane += 1)
                {
                    const uint32_t expected = replay_load_location(&pp, &transcript, bases, locations[at + lane]);
                    ok &= actual[lane] == expected;
                    checked += 1;
                }
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (size_t)page * 2);
    }
    pp_release(&pp);
    intern_release(&table);
    printf("guarded replay locations: ok=%u checks=%lu\n", ok, checked);
    return ok ? 0 : 1;
}
