// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original runtime mixed-label-attribute fixtures (#2332). The case table records BUSTER_CASE, expected status and
// whether the first error must be suppress's string-literal error. Compare every diagnostic and note with Clang,
// including under a one-error limit; rejected object compiles must preserve an existing output.

static void check_test_driver_label_order(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[PATH_MAX];
    char catalog[PATH_MAX];
    FILE* cases = check_test_fixture_path(catalog, sizeof(catalog), "driver/suppress_label_order.cases") ? fopen(catalog, "r") : NULL;
    const bool available = cases != NULL && check_test_fixture_path(source, sizeof(source), "driver/suppress_label_order.c") && access(source, R_OK) == 0;
    check_test_expect(test, available, "label attribute order: runtime source and case table are available");
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    static const char* const options[] = { "-ferror-limit=0", "-ferror-limit=1", "-Werror", "-w" };
    static const char sentinel[] = "preserve the existing label-order object\n";
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/label-order.o", directory);
    uint32_t case_count = 0;
    uint32_t case_id = 0;
    uint32_t rejected = 0;
    uint32_t string_first = 0;
    int fields = available ? fscanf(cases, "%u %u %u", &case_id, &rejected, &string_first) : EOF;
    while (fields == 3 && rejected <= 1 && string_first <= rejected)
    {
        case_count += 1;
        char define[64];
        snprintf(define, sizeof(define), "-DBUSTER_CASE=%u", case_id);
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(standards); s += 1)
        {
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
            {
                char* oracle_arguments[] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics",
                                             (char*)source, define, (char*)options[o], NULL };
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(oracle_arguments, &expected);
                char expected_lines[8192] = { 0 };
                const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, sizeof(expected_lines));
                check_test_expect(test, lines && oracle == (int)rejected, "label attribute order: oracle has the case table's status");
                for (uint32_t mode = 0; lines && mode < 6; mode += 1)
                {
                    const bool compile = mode >= 2;
                    const bool scalar = mode == 1 || mode == 3;
                    char* arguments[16] = { (char*)executable, (char*)standards[s], compile ? "-c" : "-fsyntax-only", "-fno-caret-diagnostics",
                                            (char*)source, define, (char*)options[o] };
                    uint32_t count = 7;
                    bool preserved = true;
                    if (compile)
                    {
                        arguments[count++] = "-o";
                        arguments[count++] = object;
                        preserved = parse_test_write(object, sentinel, sizeof(sentinel) - 1);
                    }
                    if (scalar)
                    {
                        arguments[count++] = "--scalar";
                    }
                    if (mode >= 4)
                    {
                        arguments[count++] = "--deferred-bodies";
                        arguments[count++] = "--lanes";
                        arguments[count++] = mode == 4 ? "1" : "3";
                    }
                    arguments[count] = NULL;
                    char* actual = NULL;
                    const int status = check_test_driver_spawn(arguments, &actual);
                    char actual_lines[8192] = { 0 };
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, sizeof(actual_lines)) &&
                                      strcmp(expected_lines, actual_lines) == 0;
                    const char* first_error = strstr(actual_lines, ": error:");
                    const char* literal_error = strstr(actual_lines, ": error: expected string literal as argument of '");
                    const bool first = string_first == 0 || (literal_error != NULL && first_error == literal_error);
                    const bool later = rejected == 0 || o == 1 || strstr(actual_lines, "use of undeclared identifier 'undeclared_after'") != NULL;
                    if (compile && rejected != 0)
                    {
                        char* contents = NULL;
                        const int descriptor = open(object, O_RDONLY);
                        preserved &= descriptor >= 0 && check_test_driver_read_file(descriptor, &contents) && contents != NULL && strcmp(contents, sentinel) == 0;
                        if (descriptor >= 0)
                        {
                            close(descriptor);
                        }
                        free(contents);
                    }
                    else if (compile)
                    {
                        unsigned char magic[4] = { 0 };
                        const int descriptor = open(object, O_RDONLY);
                        const ssize_t bytes = descriptor >= 0 ? read(descriptor, magic, sizeof(magic)) : -1;
                        preserved &= bytes == sizeof(magic) && memcmp(magic, "\177ELF", sizeof(magic)) == 0;
                        if (descriptor >= 0)
                        {
                            close(descriptor);
                        }
                    }
                    char description[4096];
                    snprintf(description, sizeof(description), "label attribute order %u, %s %s, mode %u: status %d (clang %d), lines\n%s(clang\n%s)",
                             case_id, standards[s], options[o], mode, status, oracle, actual_lines, expected_lines);
                    check_test_expect(test, same && status == oracle && first && later && preserved, description);
                    free(actual);
                }
                free(expected);
            }
        }
        fields = fscanf(cases, "%u %u %u", &case_id, &rejected, &string_first);
    }
    check_test_expect(test, available && case_count != 0 && fields == EOF && feof(cases) && !ferror(cases), "label attribute order: complete case table read");
    if (cases != NULL)
    {
        fclose(cases);
    }
    unlink(object);
}
