// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #811: PIC predefines must not advertise shared-library code generation that the backend does not implement.
// Exercise the real driver, including option ordering and rejection before output publication.

typedef enum CheckPicAction
{
    BUSTER_CHECK_PIC_OBJECT,
    BUSTER_CHECK_PIC_CHECK_OBJECT,
    BUSTER_CHECK_PIC_LINK,
    BUSTER_CHECK_PIC_SYNTAX,
    BUSTER_CHECK_PIC_CHECK,
    BUSTER_CHECK_PIC_PREPROCESS,
    BUSTER_CHECK_PIC_MACROS,
    BUSTER_CHECK_PIC_TYPED,
    BUSTER_CHECK_PIC_STATIC,
    BUSTER_CHECK_PIC_ACTION_COUNT,
} CheckPicAction;

static int check_test_driver_pic_command(const char* executable, const char* source, const char* output, bool scalar,
                                         CheckPicAction action, const char* const* options, uint32_t option_count, char** text)
{
    static const char* const actions[BUSTER_CHECK_PIC_ACTION_COUNT][2] = {
        { "-c", NULL },
        { "check", "-c" },
        { NULL, NULL },
        { "-fsyntax-only", NULL },
        { "check", NULL },
        { "-E", NULL },
        { "-E", "-dM" },
        { "--dump-typed", NULL },
        { "--dump-static", NULL },
    };
    assert(action < BUSTER_CHECK_PIC_ACTION_COUNT && option_count <= 8);
    char* arguments[24] = { 0 };
    uint32_t count = 0;
    arguments[count++] = (char*)executable;
    // Explicit subcommands must precede options, including --scalar.
    for (uint32_t i = 0; i < 2 && actions[action][i] != NULL; i += 1)
    {
        arguments[count++] = (char*)actions[action][i];
    }
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    arguments[count++] = "-std=gnu17";
    for (uint32_t i = 0; i < option_count; i += 1)
    {
        arguments[count++] = (char*)options[i];
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    return check_test_driver_spawn(arguments, text);
}

static void check_test_driver_pic(CheckTest* test, const char* executable, const char* directory)
{
    static const char* const programs[] = {
        "int exported = 5; static int local = 7;\n"
        "int helper(int x) { return x * 2; }\n"
        "int (*pointer)(int) = helper;\n"
        "int main(void) { exported += local; return pointer(exported) == 24 ? 0 : 1; }\n",
        "_Thread_local int tls_value = 3;\n"
        "int main(void) { tls_value += 4; return tls_value == 7 ? 0 : 1; }\n",
    };
    static const char* const flags[] = {
        "-fPIC",
        "-fpic",
    };
    static const char* const overrides[] = {
        "-fno-PIC",
        "-fno-pic",
        "-fno-PIE",
        "-fno-pie",
        "-fPIE",
        "-fpie",
    };
    static const char sentinel[] = "existing output must survive a rejected PIC request\n";
    static const char macro_program[] =
        "#if !defined(__PIC__) || !defined(__pic__)\n#error missing PIC macros\n#endif\n"
        "#if __PIC__ != EXPECTED_PIC || __pic__ != EXPECTED_PIC\n#error incorrect PIC level\n#endif\n"
        "#if defined(__PIE__) || defined(__pie__)\n#error PIC must not imply PIE\n#endif\n"
        "int pic_level = __PIC__;\n";
    char source[256];
    char output[256];
    char saved[256];
    char header[256];
    char label[256];
    snprintf(source, sizeof(source), "%s/pic-driver.c", directory);
    snprintf(output, sizeof(output), "%s/pic-driver.out", directory);
    snprintf(saved, sizeof(saved), "%s/pic-driver.saved", directory);
    const bool saved_written = parse_test_write(saved, sentinel, sizeof(sentinel) - 1);
    check_test_expect(test, saved_written, "PIC rejection sentinel is written");
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(programs); fixture += 1)
    {
        const bool written = parse_test_write(source, programs[fixture], strlen(programs[fixture]));
        check_test_expect(test, written, "PIC fixture is written");
        for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
        {
            for (uint32_t flag = 0; flag < BUSTER_ARRAY_LENGTH(flags); flag += 1)
            {
                for (uint32_t mode = BUSTER_CHECK_PIC_OBJECT; mode <= BUSTER_CHECK_PIC_LINK; mode += 1)
                {
                    for (uint32_t existing = 0; existing < 2; existing += 1)
                    {
                        unlink(output);
                        const bool prepared = existing == 0 || parse_test_write(output, sentinel, sizeof(sentinel) - 1);
                        const char* options[] = { flags[flag] };
                        char* text = NULL;
                        const int status = check_test_driver_pic_command(executable, source, output, scalar != 0, (CheckPicAction)mode,
                                                                        options, BUSTER_ARRAY_LENGTH(options), &text);
                        char expected[128];
                        snprintf(expected, sizeof(expected), "unsupported code generation option '%s'", flags[flag]);
                        snprintf(label, sizeof(label), "PIC rejection fixture=%u scalar=%u flag=%s mode=%u existing=%u", fixture, scalar, flags[flag], mode, existing);
                        check_test_expect(test, prepared && status == 1 && text != NULL && strstr(text, expected) != NULL &&
                                               (existing == 0 ? access(output, F_OK) != 0 : saved_written && check_test_driver_same_files(saved, output)), label);
                        free(text);
                    }
                }
            }
        }
    }
    // The complete PIC/PIE family shares one last-option state in the predefines parser. Check both orders.
    const bool ordinary = parse_test_write(source, programs[0], strlen(programs[0]));
    check_test_expect(test, ordinary, "PIC option-order fixture is written");
    for (uint32_t flag = 0; ordinary && flag < BUSTER_ARRAY_LENGTH(flags); flag += 1)
    {
        for (uint32_t override = 0; override < BUSTER_ARRAY_LENGTH(overrides); override += 1)
        {
            for (uint32_t reverse = 0; reverse < 2; reverse += 1)
            {
                unlink(output);
                const char* options[] = {
                    reverse != 0 ? overrides[override] : flags[flag],
                    reverse != 0 ? flags[flag] : overrides[override],
                };
                char* text = NULL;
                const int status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT,
                                                                options, BUSTER_ARRAY_LENGTH(options), &text);
                snprintf(label, sizeof(label), "PIC option order %s %s", options[0], options[1]);
                char expected[128];
                snprintf(expected, sizeof(expected), "unsupported code generation option '%s'", flags[flag]);
                check_test_expect(test, reverse != 0 ? status == 1 && text != NULL && strstr(text, expected) != NULL && access(output, F_OK) != 0 :
                                                      status == 0 && access(output, F_OK) == 0, label);
                free(text);
            }
        }
        const char* repeated[] = { flags[1 - flag], flags[flag] };
        char* text = NULL;
        unlink(output);
        const int status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT,
                                                        repeated, BUSTER_ARRAY_LENGTH(repeated), &text);
        char expected[128];
        snprintf(expected, sizeof(expected), "unsupported code generation option '%s'", flags[flag]);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, expected) != NULL && access(output, F_OK) != 0,
                          "PIC diagnostic names the final PIC spelling");
        free(text);
        // An option value that happens to spell a PIC flag is a filename, not another code generation request.
        snprintf(header, sizeof(header), "%s/%s", directory, flags[flag]);
        const bool header_written = parse_test_write(header, "\n", 1);
        const char* include[] = { "-I", directory, "-include", flags[flag] };
        text = NULL;
        unlink(output);
        const int included = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT,
                                                          include, BUSTER_ARRAY_LENGTH(include), &text);
        check_test_expect(test, header_written && included == 0 && access(output, F_OK) == 0, "a PIC spelling consumed as an option value is not rejected");
        free(text);
        unlink(header);
    }
    const bool macros_written = parse_test_write(source, macro_program, sizeof(macro_program) - 1);
    check_test_expect(test, macros_written, "PIC macro fixture is written");
    for (uint32_t scalar = 0; macros_written && scalar < 2; scalar += 1)
    {
        for (uint32_t flag = 0; flag < BUSTER_ARRAY_LENGTH(flags); flag += 1)
        {
            for (uint32_t mode = BUSTER_CHECK_PIC_SYNTAX; mode < BUSTER_CHECK_PIC_ACTION_COUNT; mode += 1)
            {
                const char* options[] = { flags[flag], flag == 0 ? "-DEXPECTED_PIC=2" : "-DEXPECTED_PIC=1" };
                char* text = NULL;
                unlink(output);
                const int status = check_test_driver_pic_command(executable, source, output, scalar != 0, (CheckPicAction)mode,
                                                                options, BUSTER_ARRAY_LENGTH(options), &text);
                snprintf(label, sizeof(label), "PIC non-emitting action scalar=%u flag=%s mode=%u", scalar, flags[flag], mode);
                check_test_expect(test, status == 0, label);
                free(text);
            }
        }
    }
    unlink(source);
    unlink(output);
    unlink(saved);
}
