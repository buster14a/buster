// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Emits original synthetic fuzz seeds, not LLVM captures.
#include <stdio.h>
#include "../../src/buster/compiler/tier1_problem.c"
#include "fixtures.h"

int main(int argc, char** argv)
{
    ProblemFixture fixture;
    uint8_t bytes[8192];
    size_t size = 0;
    int result = 1;
    const bool valid = argc == 2 && (strcmp(argv[1], "straight") == 0 || strcmp(argv[1], "cfg") == 0 || strcmp(argv[1], "form") == 0);
    if (valid)
    {
        problem_fixture(&fixture);
        if (strcmp(argv[1], "cfg") == 0)
        {
            problem_fixture_cfg(&fixture);
        }
        if (strcmp(argv[1], "form") == 0)
        {
            problem_fixture_form(&fixture);
        }
        if (tier1_problem_encode(&fixture.problem, bytes, sizeof(bytes), &size) == TIER1_PROBLEM_OK)
        {
            result = fwrite(bytes, 1, size, stdout) == size && fflush(stdout) == 0 ? 0 : 1;
        }
    }
    else
    {
        fprintf(stderr, "usage: tier1-problem-seed straight|cfg|form\n");
    }
    return result;
}
