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

// #1124: object/linkage options must not silently change nothing. Like PIC, valid requests are harmless to
// non-emitting actions; output actions refuse the effective setting before publishing even the first unit.
static void check_test_driver_object_options(CheckTest* test, const char* executable, const char* directory)
{
    static const char* const flags[][2] = {
        {
            "-fcommon",
            "-fno-common",
        },
        {
            "-fvisibility=hidden",
            "-fvisibility=default",
        },
        {
            "-fvisibility=protected",
            "-fvisibility=default",
        },
        {
            "-fvisibility=internal",
            "-fvisibility=default",
        },
        {
            "-fvisibility-ms-compat",
            "-fvisibility=default",
        },
        {
            "-ffunction-sections",
            "-fno-function-sections",
        },
        {
            "-fdata-sections",
            "-fno-data-sections",
        },
    };
    static const char program[] =
        "int tentative; int initialized = 2;\n"
        "int exported_fn(void) { return 1; }\n"
        "int main(void) { return exported_fn() + initialized != 3; }\n";
    static const char sentinel[] = "existing output must survive a rejected object/linkage option\n";
    char source[256];
    char output[256];
    char saved[256];
    char header[256];
    char label[256];
    snprintf(source, sizeof(source), "%s/object-options.c", directory);
    snprintf(output, sizeof(output), "%s/object-options.out", directory);
    snprintf(saved, sizeof(saved), "%s/object-options.saved", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    const bool saved_written = parse_test_write(saved, sentinel, sizeof(sentinel) - 1);
    check_test_expect(test, written && saved_written, "object/linkage option fixtures are written");
    for (uint32_t flag = 0; written && saved_written && flag < BUSTER_ARRAY_LENGTH(flags); flag += 1)
    {
        char expected[160];
        snprintf(expected, sizeof(expected), "unsupported code generation option '%s'", flags[flag][0]);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            for (uint32_t tier = 0; tier < 2; tier += 1)
            {
                for (uint32_t mode = BUSTER_CHECK_PIC_OBJECT; mode <= BUSTER_CHECK_PIC_LINK; mode += 1)
                {
                    for (uint32_t existing = 0; existing < 2; existing += 1)
                    {
                        unlink(output);
                        const bool prepared = existing == 0 || parse_test_write(output, sentinel, sizeof(sentinel) - 1);
                        const char* options[] = {
                            flags[flag][0],
                            "--tier1",
                        };
                        char* text = NULL;
                        const int status = check_test_driver_pic_command(executable, source, output, scalar != 0, (CheckPicAction)mode, options, 1 + tier, &text);
                        snprintf(label, sizeof(label), "object option %s scalar=%u tier=%u mode=%u existing=%u", flags[flag][0], scalar, tier, mode, existing);
                        check_test_expect(test, prepared && status == 1 && text != NULL && strstr(text, expected) != NULL &&
                                               (existing == 0 ? access(output, F_OK) != 0 : check_test_driver_same_files(saved, output)), label);
                        free(text);
                    }
                }
            }
        }
        for (uint32_t reverse = 0; reverse < 2; reverse += 1)
        {
            const char* options[] = {
                flags[flag][reverse],
                flags[flag][1 - reverse],
            };
            char* text = NULL;
            unlink(output);
            const int status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, options, BUSTER_ARRAY_LENGTH(options), &text);
            snprintf(label, sizeof(label), "object option order %s %s", options[0], options[1]);
            check_test_expect(test, reverse != 0 ? status == 1 && text != NULL && strstr(text, expected) != NULL && access(output, F_OK) != 0 :
                                                  status == 0 && access(output, F_OK) == 0, label);
            free(text);
        }
        for (uint32_t mode = BUSTER_CHECK_PIC_SYNTAX; mode < BUSTER_CHECK_PIC_ACTION_COUNT; mode += 1)
        {
            const char* options[] = { flags[flag][0] };
            char* text = NULL;
            unlink(output);
            const int status = check_test_driver_pic_command(executable, source, output, false, (CheckPicAction)mode, options, BUSTER_ARRAY_LENGTH(options), &text);
            snprintf(label, sizeof(label), "object option %s non-emitting mode=%u", flags[flag][0], mode);
            check_test_expect(test, status == 0, label);
            free(text);
        }
        // A value consumed by -include is a filename, even when it spells one of these options.
        snprintf(header, sizeof(header), "%s/%s", directory, flags[flag][0]);
        const bool header_written = parse_test_write(header, "\n", 1);
        const char* include[] = {
            "-I",
            directory,
            "-include",
            flags[flag][0],
        };
        char* text = NULL;
        unlink(output);
        const int included = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, include, BUSTER_ARRAY_LENGTH(include), &text);
        check_test_expect(test, header_written && included == 0 && access(output, F_OK) == 0, "an object-option spelling consumed as a value is not rejected");
        free(text);
        unlink(header);
    }
    // -fvisibility= is clang's last-option family: an earlier malformed value is ignored when a later one decides,
    // while a malformed last value keeps clang's invalid-value diagnostic.
    static const char* const malformed[] = {
        "-fvisibility=",
        "-fvisibility=bogus",
        "-fvisibility=default-extra",
        "-fvisibility=hidden-extra",
    };
    for (uint32_t i = 0; written && i < BUSTER_ARRAY_LENGTH(malformed); i += 1)
    {
        const char* options[] = {
            malformed[i],
            "-fvisibility=default",
        };
        char* text = NULL;
        unlink(output);
        const int status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, options, BUSTER_ARRAY_LENGTH(options), &text);
        snprintf(label, sizeof(label), "visibility default cancels malformed %s", malformed[i]);
        check_test_expect(test, status == 0 && text != NULL && access(output, F_OK) == 0, label);
        free(text);
        const char* alone[] = { malformed[i] };
        text = NULL;
        unlink(output);
        const int alone_status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, alone, BUSTER_ARRAY_LENGTH(alone), &text);
        snprintf(label, sizeof(label), "a malformed last visibility %s is invalid", malformed[i]);
        check_test_expect(test, alone_status == 1 && text != NULL && strstr(text, "invalid value") != NULL && access(output, F_OK) != 0, label);
        free(text);
    }
    // The effective non-default value is named, not an earlier malformed spelling that clang ignores.
    static const char* const effective[][2] = {
        { "-fvisibility=bogus", "-fvisibility=hidden" },
        { "-fvisibility=", "-fvisibility=protected" },
        { "-fvisibility=default-extra", "-fvisibility=internal" },
        { "-fvisibility=hidden-extra", "-fvisibility-ms-compat" },
    };
    for (uint32_t i = 0; written && i < BUSTER_ARRAY_LENGTH(effective); i += 1)
    {
        const char* options[] = {
            effective[i][0],
            effective[i][1],
        };
        char* text = NULL;
        char expected[160];
        snprintf(expected, sizeof(expected), "unsupported code generation option '%s'", effective[i][1]);
        unlink(output);
        const int status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, options, BUSTER_ARRAY_LENGTH(options), &text);
        snprintf(label, sizeof(label), "effective visibility %s is refused by name", effective[i][1]);
        check_test_expect(test, status == 1 && text != NULL && strstr(text, expected) != NULL && access(output, F_OK) != 0, label);
        free(text);
    }
    // -fvisibility-ms-compat is clang's hidden default and participates in the same last-option state.
    const char* ms_compat_then_default[] = {
        "-fvisibility-ms-compat",
        "-fvisibility=default",
    };
    char* text = NULL;
    unlink(output);
    const int ms_default_status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, ms_compat_then_default,
                                                                BUSTER_ARRAY_LENGTH(ms_compat_then_default), &text);
    check_test_expect(test, ms_default_status == 0 && access(output, F_OK) == 0, "a later default cancels -fvisibility-ms-compat");
    free(text);
    const char* default_then_ms_compat[] = {
        "-fvisibility=default",
        "-fvisibility-ms-compat",
    };
    text = NULL;
    unlink(output);
    const int default_ms_status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, default_then_ms_compat,
                                                                BUSTER_ARRAY_LENGTH(default_then_ms_compat), &text);
    check_test_expect(test, default_ms_status == 1 && text != NULL && strstr(text, "unsupported code generation option '-fvisibility-ms-compat'") != NULL &&
                           access(output, F_OK) != 0, "an effective -fvisibility-ms-compat is refused by name");
    free(text);
    // Clang has no bare -fvisibility; ours refuses it, and a later default does not turn it into an option.
    const char* bare_visibility[] = { "-fvisibility" };
    text = NULL;
    unlink(output);
    const int bare_status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, bare_visibility,
                                                          BUSTER_ARRAY_LENGTH(bare_visibility), &text);
    check_test_expect(test, bare_status == 1 && text != NULL && strstr(text, "unsupported code generation option '-fvisibility'") != NULL &&
                           access(output, F_OK) != 0, "a bare -fvisibility is refused");
    free(text);
    const char* bare_then_default[] = {
        "-fvisibility",
        "-fvisibility=default",
    };
    text = NULL;
    unlink(output);
    const int bare_default_status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, bare_then_default,
                                                                  BUSTER_ARRAY_LENGTH(bare_then_default), &text);
    check_test_expect(test, bare_default_status == 1 && text != NULL && strstr(text, "unsupported code generation option '-fvisibility'") != NULL &&
                           access(output, F_OK) != 0, "a later default does not conceal a bare -fvisibility");
    free(text);
    const char* independent[] = {
        "-fcommon",
        "-ffunction-sections",
        "-fno-common",
    };
    text = NULL;
    unlink(output);
    const int independent_status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, independent, BUSTER_ARRAY_LENGTH(independent), &text);
    check_test_expect(test, written && independent_status == 1 && text != NULL &&
                           strstr(text, "unsupported code generation option '-ffunction-sections'") != NULL && access(output, F_OK) != 0,
                      "canceling common emission does not cancel function sections");
    free(text);
    // The original two-unit failure must be a named option refusal, not a duplicate-definition link error.
    static const char first[] = "int shared; int get(void); int main(void) { shared = 3; return get() != 3; }\n";
    static const char second[] = "int shared; int get(void) { return shared; }\n";
    snprintf(header, sizeof(header), "%s/object-options-other.c", directory);
    const bool units_written = parse_test_write(source, first, sizeof(first) - 1) && parse_test_write(header, second, sizeof(second) - 1);
    char* arguments[] = {
        (char*)executable,
        "-fcommon",
        "-o",
        output,
        source,
        header,
        NULL,
    };
    text = NULL;
    unlink(output);
    const int units_status = check_test_driver_spawn(arguments, &text);
    check_test_expect(test, units_written && units_status == 1 && text != NULL && strstr(text, "unsupported code generation option '-fcommon'") != NULL &&
                           access(output, F_OK) != 0, "multi-unit common emission is refused before linking");
    free(text);
    unlink(header);
    unlink(source);
    unlink(output);
    unlink(saved);
}

static void check_test_driver_pic(CheckTest* test, const char* executable, const char* directory)
{
    check_test_driver_object_options(test, executable, directory);
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
