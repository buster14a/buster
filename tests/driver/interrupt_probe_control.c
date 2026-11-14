// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #2226 fixture: a deterministic compile/link/run stand-in with an execution trace. No interrupt ISA needed.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv)
{
    int status = 64;
    if (argc == 4)
    {
        FILE* trace = fopen(argv[3], "a");
        bool traced = trace != NULL && fprintf(trace, "%s\n", argv[1]) > 0;
        if (trace != NULL)
        {
            traced = fclose(trace) == 0 && traced;
        }
        if (traced)
        {
            const bool run = strcmp(argv[1], "run") == 0;
            const bool failed = strcmp(argv[1], argv[2]) == 0;
            status = 0;
            if (failed)
            {
                fprintf(run ? stdout : stderr, "%s diagnostic\n", argv[1]);
                status = run ? 43 : strcmp(argv[1], "compile") == 0 ? 41 : 42;
            }
            else if (run && (strcmp(argv[2], "stderr") == 0 || strcmp(argv[2], "stderr-pass") == 0))
            {
                fputs("runtime diagnostic\n", stderr);
                status = strcmp(argv[2], "stderr") == 0 ? 44 : 0;
                if (status == 0)
                {
                    puts("ok");
                }
            }
            else if (run && strcmp(argv[2], "empty") != 0)
            {
                puts(strcmp(argv[2], "wrong") == 0 ? "wrong" : "ok");
            }
        }
    }
    return status;
}
