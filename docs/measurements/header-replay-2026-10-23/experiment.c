// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// One executable for the isolated #111 experiment and its independent checks.
#define BUSTER_PROBE_MAIN replay_benchmark_command
#include "probe.c"
#define BUSTER_VERIFY_MAIN replay_verify_command
#include "verify.c"
#define BUSTER_GUARD_MAIN replay_guard_command
#include "guard.c"
#define BUSTER_GENERATE_MAIN replay_generate_command
#include "generate.c"
#define BUSTER_SOURCE_VERIFY_MAIN replay_source_verify_command
#include "source-verify.c"

int main(int argc, char** argv)
{
    int result = 1;
    if (argc > 1 && strcmp(argv[1], "benchmark") == 0)
    {
        result = replay_benchmark_command(argc - 1, argv + 1);
    }
    else if (argc > 1 && strcmp(argv[1], "verify") == 0)
    {
        result = replay_verify_command(argc - 1, argv + 1);
    }
    else if (argc > 1 && strcmp(argv[1], "guard") == 0)
    {
        result = replay_guard_command();
    }
    else if (argc > 1 && strcmp(argv[1], "generate") == 0)
    {
        result = replay_generate_command(argc - 1, argv + 1);
    }
    else if (argc > 1 && strcmp(argv[1], "source") == 0)
    {
        result = replay_source_verify_command(argc - 1, argv + 1);
    }
    else
    {
        fprintf(stderr, "usage: %s benchmark|verify|guard|generate [arguments]\n", argv[0]);
    }
    return result;
}
