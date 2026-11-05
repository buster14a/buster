// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-facing typed execution for external differential runners. Preparation retains each unit separately; their
// data and functions resolve by C linkage. The command returns main's status and writes only the program's output.

static int run_typed_source(int argc, char** argv)
{
    char** files = calloc((uint64_t)argc + 1, sizeof(char*));
    char** flags = calloc((uint64_t)argc + 1, sizeof(char*));
    uint32_t file_count = 0;
    uint32_t flag_count = 0;
    uint64_t timeout = BUSTER_SSA_SOURCE_TIMEOUT;
    InterpreterOptions options = interpreter_default_options();
    options.max_steps = BUSTER_SSA_SOURCE_STEPS;
    // Unflagged host library functions cannot report which bytes they initialized. Bounds remain checked.
    options.check_undefined = false;
    bool valid = files != 0 && flags != 0;
    bool operands = false;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (!operands && strcmp(argv[i], "--") == 0)
        {
            operands = true;
        }
        else if (!operands && (strcmp(argv[i], "--steps") == 0 || strcmp(argv[i], "--timeout") == 0))
        {
            const bool steps = strcmp(argv[i], "--steps") == 0;
            uint64_t number = 0;
            valid = i + 1 < argc && check_corpus_number(argv[++i], &number) && number != 0 && (steps || number <= 3600);
            if (steps)
            {
                options.max_steps = number;
            }
            else
            {
                timeout = number;
            }
        }
        else if (!operands && strcmp(argv[i], "--check-undefined") == 0)
        {
            options.check_undefined = true;
        }
        else if (operands || argv[i][0] != '-')
        {
            files[file_count++] = argv[i];
        }
        else if (strcmp(argv[i], "--no-default-config") != 0)
        {
            flags[flag_count++] = argv[i];
            if (pp_option_takes_value(argv[i]))
            {
                valid = i + 1 < argc;
                flags[flag_count++] = valid ? argv[++i] : 0;
            }
        }
    }
    valid = valid && file_count != 0;
    Arena lowered = arena_reserve(UINT64_C(1) << 36);
    Arena tables = arena_reserve(UINT64_C(1) << 34);
    Arena runtime = arena_reserve(UINT64_C(1) << 34);
    IrSourceFile* prepared = valid ? BUSTER_ARENA_PUSH_ARRAY(&tables, IrSourceFile, file_count) : 0;
    valid = valid && lowered.base != 0 && tables.base != 0 && runtime.base != 0 && prepared != 0;
    int status = 125;
    for (uint32_t u = 0; valid && u < file_count; u += 1)
    {
        ir_source_prepare(&prepared[u], files[u], flags, flag_count, &lowered, &tables, true, false);
        valid = prepared[u].status == IR_SOURCE_PREPARED;
        if (!valid)
        {
            fprintf(stderr, "interpret-c: %s: %s\n", files[u], prepared[u].reason);
        }
    }
    if (valid)
    {
        alarm((uint32_t)timeout);
        const InterpreterResult result = interpreter_source_run(prepared, file_count, &options, &runtime);
        alarm(0);
        status = result.status == INTERPRETER_OK ? (int)(result.value.low & 255) : 125;
    }
    else if (file_count == 0)
    {
        fprintf(stderr, "usage: compiler interpret-c [--steps N] [--timeout SECONDS] [--check-undefined] [preprocessor options] file...\n");
    }
    arena_release(&runtime);
    arena_release(&tables);
    arena_release(&lowered);
    free(flags);
    free(files);
    return status;
}
