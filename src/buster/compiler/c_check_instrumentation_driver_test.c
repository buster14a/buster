// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1112: only non-emitting actions may model unsupported instrumentation. Exercise the production command before
// any output is created or replaced, including the final option state, both backends and option-looking operands.

static void check_test_driver_instrumentation(CheckTest* test, const char* executable, const char* directory)
{
    static const char source_text[] =
        "#if defined(EXPECT_ADDRESS) && !defined(__SANITIZE_ADDRESS__)\n"
        "#error address predefine lost\n"
        "#endif\n"
        "#if defined(EXPECT_OPENMP) && !defined(_OPENMP)\n"
        "#error OpenMP predefine lost\n"
        "#endif\n"
        "#if defined(EXPECT_PLAIN) && (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || defined(_OPENMP))\n"
        "#error disabled instrumentation still advertised\n"
        "#endif\n"
        "int main(void) { return 0; }\n";
    static const char sentinel[] = "instrumentation output must remain unchanged\n";
    static const struct
    {
        const char* first;
        const char* second;
        const char* diagnostic;
    } refused[] = {
        { "-fsanitize=address", NULL, "-fsanitize=address" },
        { "-fsanitize=undefined", NULL, "-fsanitize=undefined" },
        { "-fsanitize=thread", NULL, "-fsanitize=thread" },
        { "-fsanitize=memory", NULL, "-fsanitize=memory" },
        { "-fsanitize=alignment", NULL, "-fsanitize=alignment" },
        { "-fsanitize=address,undefined", NULL, "-fsanitize=address,undefined" },
        { "-fsanitize=address,undefined", "-fno-sanitize=address", "-fsanitize=undefined" },
        { "-fno-sanitize=address", "-fsanitize=address", "-fsanitize=address" },
        { "-fsanitize=address", "-fno-sanitize=thread", "-fsanitize=address" },
        { "-fsanitize=fuzzer-no-link", NULL, "-fsanitize=fuzzer-no-link" },
        { "-fsanitize-coverage=trace-pc-guard", NULL, "-fsanitize-coverage" },
        { "-fopenmp", NULL, "-fopenmp" },
        { "-fopenmp=libomp", NULL, "-fopenmp=libomp" },
        { "-fopenmp=libgomp", NULL, "-fopenmp=libgomp" },
        { "-fopenmp=libiomp5", NULL, "-fopenmp=libiomp5" },
        { "-fno-openmp", "-fopenmp", "-fopenmp" },
    };
    char compiler[PATH_MAX];
    char source[256];
    char output[256];
    char saved[256];
    char named[256];
    snprintf(source, sizeof(source), "%s/instrumentation.c", directory);
    snprintf(output, sizeof(output), "%s/instrumentation.out", directory);
    snprintf(saved, sizeof(saved), "%s/instrumentation.saved", directory);
    snprintf(named, sizeof(named), "%s/-fsanitize=address", directory);
    const bool made = realpath(executable, compiler) != NULL && parse_test_write(source, source_text, sizeof(source_text) - 1) &&
                      parse_test_write(saved, sentinel, sizeof(sentinel) - 1);
    check_test_expect(test, made, "instrumentation fixtures written");
    // Ordinary objects, direct linking, explicit check -c, Tier 1 and the scalar path all use the same barrier.
    for (uint32_t mode = 0; made && mode < 5; mode += 1)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused); i += 1)
        {
            for (uint32_t existing = 0; existing < 2; existing += 1)
            {
                unlink(output);
                const bool ready = existing == 0 || parse_test_write(output, sentinel, sizeof(sentinel) - 1);
                char* arguments[16] = { compiler };
                uint32_t count = 1;
                if (mode >= 2)
                {
                    arguments[count++] = mode == 2 ? "check" : mode == 3 ? "--tier1" : "--scalar";
                }
                if (mode != 1)
                {
                    arguments[count++] = "-c";
                }
                arguments[count++] = "-o";
                arguments[count++] = output;
                arguments[count++] = source;
                arguments[count++] = (char*)refused[i].first;
                if (refused[i].second != NULL)
                {
                    arguments[count++] = (char*)refused[i].second;
                }
                arguments[count] = NULL;
                char* text = NULL;
                const int status = ready ? check_test_driver_spawn(arguments, &text) : -1;
                char expected[192];
                snprintf(expected, sizeof(expected), "unsupported code generation option '%s'", refused[i].diagnostic);
                const bool untouched = existing != 0 ? check_test_driver_same_files(output, saved) : access(output, F_OK) != 0 && errno == ENOENT;
                char label[256];
                snprintf(label, sizeof(label), "instrumentation mode %u, %s %s, %s output: named refusal", mode, refused[i].first,
                         refused[i].second != NULL ? refused[i].second : "", existing != 0 ? "existing" : "absent");
                check_test_expect(test, ready && status == 1 && text != NULL && strstr(text, expected) != NULL && untouched, label);
                free(text);
            }
        }
    }
    static const char* const disabled[][2] = {
        { "-fno-sanitize=all", NULL },
        { "-fsanitize=address", "-fno-sanitize=address" },
        { "-fsanitize=address,undefined", "-fno-sanitize=all" },
        { "-fsanitize=thread", "-fno-sanitize=thread" },
        { "-fsanitize-coverage=trace-pc-guard", "-fno-sanitize-coverage=trace-pc-guard" },
        { "-fopenmp", "-fno-openmp" },
        { "-fopenmp=libgomp", "-fno-openmp" },
        { "-fopenmp=invalid", "-fno-openmp" },
    };
    for (uint32_t mode = 0; made && mode < 2; mode += 1)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(disabled); i += 1)
        {
            unlink(output);
            char* arguments[16] = { compiler };
            uint32_t count = 1;
            if (mode == 0)
            {
                arguments[count++] = "-c";
            }
            arguments[count++] = "-DEXPECT_PLAIN";
            arguments[count++] = "-o";
            arguments[count++] = output;
            arguments[count++] = source;
            arguments[count++] = (char*)disabled[i][0];
            if (disabled[i][1] != NULL)
            {
                arguments[count++] = (char*)disabled[i][1];
            }
            arguments[count] = NULL;
            char* text = NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            FILE* file = fopen(output, "rb");
            unsigned char magic[4] = { 0 };
            const bool elf = file != NULL && fread(magic, 1, sizeof(magic), file) == sizeof(magic) && memcmp(magic, "\177ELF", 4) == 0;
            if (file != NULL)
            {
                fclose(file);
            }
            char label[256];
            snprintf(label, sizeof(label), "disabled instrumentation mode %u, %s %s produces ELF", mode, disabled[i][0],
                     disabled[i][1] != NULL ? disabled[i][1] : "");
            check_test_expect(test, status == 0 && elf, label);
            free(text);
        }
    }
    static const char* const modeled[][2] = {
        { "-fsanitize=address", "-DEXPECT_ADDRESS" },
        { "-fsanitize=undefined", NULL },
        { "-fsanitize=thread", NULL },
        { "-fsanitize-coverage=trace-pc-guard", NULL },
        { "-fopenmp", "-DEXPECT_OPENMP" },
        { "-fopenmp=libomp", "-DEXPECT_OPENMP" },
        { "-fopenmp=libgomp", "-DEXPECT_PLAIN" },
    };
    // -fsyntax-only dominates -c in either order; check without -c is also non-emitting.
    for (uint32_t mode = 0; made && mode < 5; mode += 1)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(modeled); i += 1)
        {
            const bool ready = parse_test_write(output, sentinel, sizeof(sentinel) - 1);
            char* arguments[16] = { compiler };
            uint32_t count = 1;
            arguments[count++] = mode == 0 ? "-E" : mode == 2 ? "check" : mode == 3 ? "-c" : "-fsyntax-only";
            if (mode >= 3)
            {
                arguments[count++] = mode == 3 ? "-fsyntax-only" : "-c";
            }
            if (mode != 0)
            {
                arguments[count++] = "-o";
                arguments[count++] = output;
            }
            arguments[count++] = (char*)modeled[i][0];
            if (modeled[i][1] != NULL)
            {
                arguments[count++] = (char*)modeled[i][1];
            }
            arguments[count++] = source;
            arguments[count] = NULL;
            char* text = NULL;
            const int status = ready ? check_test_driver_spawn_stream(arguments, &text, mode == 0) : -1;
            const bool preprocessed = mode != 0 || (text != NULL && strstr(text, "main") != NULL);
            char label[192];
            snprintf(label, sizeof(label), "non-emitting mode %u retains %s predefines and leaves output alone", mode, modeled[i][0]);
            check_test_expect(test, status == 0 && preprocessed && check_test_driver_same_files(output, saved), label);
            free(text);
        }
    }
    // The spelling is an operand, not a sanitizer request.
    char* const operand[] = { "env", "-C", (char*)directory, compiler, "-c", "-o", "-fsanitize=address", "instrumentation.c", NULL };
    char* text = NULL;
    const int operand_status = made ? check_test_driver_spawn(operand, &text) : -1;
    struct stat info;
    check_test_expect(test, operand_status == 0 && stat(named, &info) == 0 && info.st_size > 0, "an output named -fsanitize=address is not an option");
    free(text);
    // Refusal must precede source loading as well as output publication, even for a multi-unit command.
    unlink(source);
    const bool ready = made && parse_test_write(output, sentinel, sizeof(sentinel) - 1);
    char* const missing[] = { compiler, "-fsanitize=address", "-o", output, source, source, NULL };
    text = NULL;
    const int missing_status = ready ? check_test_driver_spawn(missing, &text) : -1;
    check_test_expect(test, missing_status == 1 && text != NULL && strstr(text, "unsupported code generation option '-fsanitize=address'") != NULL &&
                      check_test_driver_same_files(output, saved), "instrumentation refusal precedes reading missing translation units");
    free(text);
    unlink(output);
    unlink(saved);
    unlink(named);
}
