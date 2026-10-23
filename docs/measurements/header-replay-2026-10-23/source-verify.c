// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Caller-supplied virtual main files cannot masquerade as the fixed command line.
#ifndef BUSTER_VERIFY_MAIN
#define BUSTER_VERIFY_MAIN unused_verify_main
#endif
#include "verify.c"

#ifndef BUSTER_SOURCE_VERIFY_MAIN
#define BUSTER_SOURCE_VERIFY_MAIN main
#endif
int BUSTER_SOURCE_VERIFY_MAIN(int argc, char** argv)
{
    static InternTable table;
    static FileCache files;
    static PPReplayCache replay;
    char path[4096];
    PPOptions options = { .language = { .standard = 2017, .gnu = true }, .expansion_starts = true, .quiet = true };
    bool ok = argc == 2 && realpath(argv[1], path) != 0 && intern_initialize(&table) &&
              file_cache_initialize(&files, &table, pp_prepare_options(&options)) && replay_initialize(&replay);
    options.files = &files;
    const char* bodies[] = { "1", "A", "\"xy\"", "(1 + 2)" };
    uint32_t checked = 0;
    for (uint32_t repeat = 0; ok && repeat < 3; repeat += 1)
    {
        for (uint32_t shape = 0; ok && shape < BUSTER_ARRAY_LENGTH(bodies); shape += 1)
        {
            char source[8192];
            const int length = snprintf(source, sizeof(source), "#define V %s\n#include \"%s\"\n", bodies[shape], path);
            ok = length > 0 && length < (int)sizeof(source);
            static Preprocessor runs[2];
            for (uint32_t v = 0; ok && v < 2; v += 1)
            {
                PPOptions with = options;
                with.replay = v != 0 ? &replay : 0;
                ok = pp_initialize(&runs[v], &with, &table) && pp_run_source(&runs[v], "<command line>", (const uint8_t*)source, (uint32_t)length);
            }
            Preprocessor* a = &runs[0];
            Preprocessor* b = &runs[1];
            ok = ok && a->output.count == b->output.count && verify_bytes("virtual kinds", a->output.kinds, b->output.kinds, a->output.count, 1) &&
                 verify_bytes("virtual values", a->output.values, b->output.values, a->output.count, 4) && verify_print(a, b, 0) && verify_print(a, b, 1) && verify_print(a, b, 2);
            pp_release(a);
            pp_release(b);
            checked += 1;
        }
    }
    printf("virtual source identities: ok=%u cases=%u hits=%lu\n", ok, checked, replay.hits);
    replay_release(&replay);
    file_cache_release(&files);
    intern_release(&table);
    return ok ? 0 : 1;
}
