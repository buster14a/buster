// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Ephemeral driver-validation adapter; not part of the implementation PR.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BUSTER_ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK_PROGRAM_OK 0
#define CheckTest void
static const char* destination;
static unsigned exported;
static int failed;
static void check_test_program_selection_run(CheckTest* test, const char* source, const char* name, uint32_t functions, uint32_t objects,
                                             int status, const char* clang, const char* symbols, const char* const* patches, bool model)
{
    (void)test;
    (void)functions;
    (void)objects;
    (void)status;
    (void)clang;
    (void)symbols;
    (void)patches;
    if (!model)
    {
        char path[1024];
        const int length = snprintf(path, sizeof(path), "%s/%s.c", destination, name);
        FILE* file = length > 0 && (unsigned)length < sizeof(path) ? fopen(path, "wb") : NULL;
        const char* harness = "\nextern int printf(const char*,...);int main(void){for(int i=-8;i<=8;i++)printf(\"%d\\n\",tested(i));return 0;}\n";
        int ok = file != NULL && fwrite(source, 1, strlen(source), file) == strlen(source) && fwrite(harness, 1, strlen(harness), file) == strlen(harness);
        if (file != NULL)
        {
            ok = fclose(file) == 0 && ok;
        }
        failed |= !ok;
        exported += ok ? 1 : 0;
    }
}
#include "src/buster/compiler/c_check_program_switch_test.c"
int main(int argc, char** argv)
{
    destination = argc == 2 ? argv[1] : NULL;
    if (destination != NULL)
    {
        check_test_program_folded_switches(NULL, "clang");
    }
    printf("Exported %u constant-switch driver fixtures\n", exported);
    return failed || exported != 29;
}
