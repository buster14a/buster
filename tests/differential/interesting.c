// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original C-Reduce/cvise interestingness entry point. Build it once with clang. The reducer runs it in its candidate
// directory; source bytes come from that directory, while the original replay configuration remains immutable.
// BUSTER_DIFFERENTIAL_COMPILER is the absolute path to build/compiler; BUSTER_DIFFERENTIAL_CASE is the original case;
// BUSTER_DIFFERENTIAL_OUTPUT is an existing parent directory for each isolated replay's evidence.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    char* compiler = getenv("BUSTER_DIFFERENTIAL_COMPILER");
    char* original = getenv("BUSTER_DIFFERENTIAL_CASE");
    char* output = getenv("BUSTER_DIFFERENTIAL_OUTPUT");
    if (compiler && compiler[0] == '/' && original && original[0] == '/' && output && output[0] == '/')
    {
        char* arguments[] =
        {
            compiler,
            "test-differential",
            "--interesting",
            original,
            "--source-directory",
            ".",
            "--output",
            output,
            0,
        };
        execv(compiler, arguments);
    }
    fputs("differential reducer: set absolute BUSTER_DIFFERENTIAL_COMPILER, BUSTER_DIFFERENTIAL_CASE and BUSTER_DIFFERENTIAL_OUTPUT\n", stderr);
    return 1;
}
