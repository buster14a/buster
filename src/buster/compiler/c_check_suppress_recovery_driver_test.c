// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Remaining suppress recovery cases (#2332), read from original runtime programs. Compare complete ordered
// diagnostics and status with Clang, including macro notes, warning policy and error limits. Invalid sources must
// preserve an old output or leave an absent one absent; executable controls must retain their statement effects.
// Runtime catalogue columns are path, decimal case count, hexadecimal accepted mask and hexadecimal executable mask.

static void check_test_driver_suppress_recovery(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    FILE* catalog = fopen("tests/driver/suppress_recovery.cases", "r");
    check_test_expect(test, catalog != NULL, "suppress recovery runtime catalogue is available");
    static const char* const standards[] =
    {
        "-std=gnu17",
        "-std=gnu23",
    };
    static const char* const policies[] =
    {
        "-ferror-limit=0",
        "-ferror-limit=1",
        "-w",
        "-Werror",
        "-Wno-ignored-attributes",
    };
    static const char sentinel[] = "preserve the existing suppress-recovery object\n";
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    const bool buffers = expected_lines != NULL && actual_lines != NULL;
    check_test_expect(test, buffers, "suppress recovery diagnostic buffers initialize");
    char object[PATH_MAX];
    char binary[PATH_MAX];
    char saved[PATH_MAX];
    snprintf(object, sizeof(object), "%s/suppress-recovery.o", directory);
    snprintf(binary, sizeof(binary), "%s/suppress-recovery", directory);
    snprintf(saved, sizeof(saved), "%s/suppress-recovery.saved", directory);
    const bool saved_output = parse_test_write(saved, sentinel, sizeof(sentinel) - 1);
    check_test_expect(test, saved_output, "suppress recovery output sentinel initializes");
    char source[256];
    uint32_t cases = 0;
    unsigned long long accepted_cases = 0;
    unsigned long long executable_cases = 0;
    uint32_t rows = 0;
    int fields = catalog != NULL && buffers && saved_output ? fscanf(catalog, "%255s %u %llx %llx", source, &cases, &accepted_cases, &executable_cases) : EOF;
    while (fields == 4 && cases != 0 && cases <= 64 && (cases == 64 || (accepted_cases >> cases) == 0) && (executable_cases & ~accepted_cases) == 0)
    {
        rows += 1;
        const bool available = access(source, R_OK) == 0;
        check_test_expect(test, available, "suppress recovery runtime fixture is available and bounded");
        for (uint32_t c = 0; available && c < cases; c += 1)
        {
            const bool accepted = ((accepted_cases >> c) & 1) != 0;
            const bool runs = ((executable_cases >> c) & 1) != 0;
            char define[64];
            snprintf(define, sizeof(define), "-DBUSTER_CASE=%u", c);
            for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(standards); s += 1)
            {
                for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(policies); p += 1)
                {
                    char* arguments[24] =
                    {
                        (char*)clang,
                        (char*)standards[s],
                        "-fsyntax-only",
                        "-fno-caret-diagnostics",
                        "-ferror-limit=0",
                        source,
                        define,
                        (char*)policies[p],
                        NULL,
                    };
                    char* expected = NULL;
                    const int oracle = check_test_driver_spawn(arguments, &expected);
                    expected_lines[0] = 0;
                    const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                    check_test_expect(test, lines && oracle == (accepted ? 0 : 1), "suppress recovery oracle status and diagnostic lines");
                    for (uint32_t mode = 0; lines && mode < 6; mode += 1)
                    {
                        const bool compile = mode >= 2;
                        const bool absent = compile && mode == 2 && !accepted;
                        arguments[0] = (char*)executable;
                        arguments[2] = compile ? "-c" : "-fsyntax-only";
                        uint32_t at = 8;
                        if (mode == 1 || mode == 3)
                        {
                            arguments[at++] = "--scalar";
                        }
                        if (mode >= 4)
                        {
                            arguments[at++] = "--deferred-bodies";
                            arguments[at++] = "--lanes";
                            arguments[at++] = mode == 4 ? "1" : "3";
                        }
                        bool prepared = true;
                        if (compile)
                        {
                            arguments[at++] = "-o";
                            arguments[at++] = object;
                            unlink(object);
                            prepared = absent || parse_test_write(object, sentinel, sizeof(sentinel) - 1);
                        }
                        arguments[at] = NULL;
                        char* actual = NULL;
                        const int status = prepared ? check_test_driver_spawn(arguments, &actual) : -1;
                        actual_lines[0] = 0;
                        const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                        bool output = !compile || (absent ? access(object, F_OK) != 0 : check_test_driver_same_files(saved, object));
                        if (compile && accepted)
                        {
                            const int descriptor = open(object, O_RDONLY);
                            unsigned char magic[4] = { 0 };
                            output = descriptor >= 0 && read(descriptor, magic, sizeof(magic)) == sizeof(magic) && memcmp(magic, "\177ELF", sizeof(magic)) == 0;
                            if (descriptor >= 0)
                            {
                                close(descriptor);
                            }
                        }
                        char description[12288];
                        snprintf(description, sizeof(description), "suppress recovery %s case %u, %s %s, mode %u: status %d (clang %d), output %u\n%s(clang\n%s)",
                                 source, c, standards[s], policies[p], mode, status, oracle, output, actual_lines, expected_lines);
                        check_test_expect(test, same && status == oracle && output, description);
                        free(actual);
                        if (runs && accepted && compile && p == 0 && status == 0 && output)
                        {
                            char* linked = NULL;
                            char* link[] = { (char*)clang, object, "-o", binary, NULL };
                            const int link_status = check_test_driver_spawn(link, &linked);
                            free(linked);
                            char* ran = NULL;
                            char* run[] = { binary, NULL };
                            const int run_status = link_status == 0 ? check_test_driver_spawn(run, &ran) : -1;
                            free(ran);
                            check_test_expect(test, link_status == 0 && run_status == 0, "mixed suppress statement control links and preserves its effects");
                        }
                    }
                    free(expected);
                }
            }
        }
        fields = fscanf(catalog, "%255s %u %llx %llx", source, &cases, &accepted_cases, &executable_cases);
    }
    check_test_expect(test, rows != 0 && fields == EOF && catalog != NULL && !ferror(catalog), "suppress recovery catalogue is complete and well-formed");
    if (catalog != NULL)
    {
        fclose(catalog);
    }
    unlink(binary);
    unlink(object);
    unlink(saved);
    free(actual_lines);
    free(expected_lines);
}
