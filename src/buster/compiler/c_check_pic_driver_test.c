// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #811 and #2101: -fPIC's and -fpic's code, through the real driver. -c's object (and check -c's, and -S's) is
// position-independent code a shared library may take: a symbol another module may preempt through its GOT, a
// thread-local by general- or local-dynamic access (CheckProgramOptions.pic); -shared links one, through the mixed
// path's writer; a link's executable takes the options with the code it has without them, byte for byte. The library
// test builds clang 23's -fPIC library's twins three ways (buster's -shared, buster's -c and cc -shared, and their
// executables linked from the objects) and runs the same programs against each: a threaded one, one that preempts the
// library's definitions, and a dlopen harness. The option order's last PIC or PIE option decides, with clang's macros.

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

// A link's PIC and PIE options, at most two in order, and its executable's exit status, __PIC__ * 10 + __PIE__.
typedef struct CheckPicLevel CheckPicLevel;
struct CheckPicLevel
{
    const char* first;
    const char* second;
    int status;
};

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
// non-emitting actions; output actions refuse the effective setting before publishing even the first unit. The default
// visibility's options are taken (#3576, check_test_driver_default_visibility), the last of them deciding.
static void check_test_driver_object_options(CheckTest* test, const char* executable, const char* directory)
{
    static const char* const flags[][2] = {
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
        "-fvisibility=ms-compat",
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
    // The effective value decides, not an earlier malformed spelling that clang ignores (#3576).
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
        unlink(output);
        const int status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT, options, BUSTER_ARRAY_LENGTH(options), &text);
        snprintf(label, sizeof(label), "effective visibility %s after a malformed one is taken", effective[i][1]);
        check_test_expect(test, status == 0 && access(output, F_OK) == 0, label);
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
    check_test_expect(test, default_ms_status == 0 && access(output, F_OK) == 0, "an effective -fvisibility-ms-compat is taken (#3576)");
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
                      "-fcommon and -fno-common, which common emission takes (#147), do not cancel function sections");
    free(text);
    // The original two-unit failure, a duplicate-definition link error, is a link of the two units' commons (#147): the
    // executable runs.
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
    char* run[] = { output, NULL };
    char* run_text = NULL;
    const int run_status = units_status == 0 ? check_test_driver_spawn(run, &run_text) : -1;
    free(run_text);
    check_test_expect(test, units_written && units_status == 0 && run_status == 0, "multi-unit common emission links the units' commons, which run");
    free(text);
    unlink(header);
    unlink(source);
    unlink(output);
    unlink(saved);
}

// Links the `unit_count` files at `units` with `kind` (-no-pie, -static, -static-pie, or null for the default PIE) and
// `flag` (-fPIC, -fpic, or null) into `output`, replacing what was there: whether the link succeeded and its executable
// ran with status 0.
static bool check_test_driver_pic_link_runs(const char* executable, const char* kind, const char* flag, const char* const* units, uint32_t unit_count,
                                            const char* output)
{
    assert(unit_count <= 4);
    char* arguments[16] = { 0 };
    uint32_t count = 0;
    arguments[count++] = (char*)executable;
    arguments[count++] = "-std=gnu17";
    if (kind != NULL)
    {
        arguments[count++] = (char*)kind;
    }
    if (flag != NULL)
    {
        arguments[count++] = (char*)flag;
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    for (uint32_t i = 0; i < unit_count; i += 1)
    {
        arguments[count++] = (char*)units[i];
    }
    unlink(output);
    char* text = NULL;
    const int status = check_test_driver_spawn(arguments, &text);
    free(text);
    char* run[] = { (char*)output, NULL };
    char* run_text = NULL;
    const int ran = status == 0 ? check_test_driver_spawn(run, &run_text) : -1;
    free(run_text);
    return status == 0 && ran == 0;
}

// #2101: code generation reads no PIC state, so a link's executable with -fPIC or -fpic is the one it links without
// them, byte for byte, whatever executable it links: the default PIE, -no-pie's, -static's and -static-pie's. The units
// are one with exported data and a function pointer, and three that share a thread-local variable, exported data and a
// function pointer: clang 23.1.4's -fPIC code reads that variable through __tls_get_addr, where its -fPIE code doesn't.
static void check_test_driver_pic_kinds(CheckTest* test, const char* executable, const char* directory, const char* ordinary)
{
    static const char* const texts[] = {
        "_Thread_local int shared_tls = 1;\nextern int shared_data;\nint first(void);\nextern int (*second_pointer)(void);\n"
        "int main(void) { shared_tls += shared_data; return first() + second_pointer() == 11 ? 0 : 1; }\n",
        "extern _Thread_local int shared_tls;\nint shared_data = 3;\nint first(void) { return shared_tls + 1; }\n",
        "extern _Thread_local int shared_tls;\nstatic int second(void) { return shared_tls + 2; }\nint (*second_pointer)(void) = second;\n",
    };
    static const char* const kinds[] = {
        NULL,
        "-no-pie",
        "-static",
        "-static-pie",
    };
    static const char* const flags[] = {
        "-fPIC",
        "-fpic",
    };
    char paths[BUSTER_ARRAY_LENGTH(texts)][256];
    bool written = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(texts); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/pic-kinds-%u.c", directory, i);
        written &= parse_test_write(paths[i], texts[i], strlen(texts[i]));
    }
    check_test_expect(test, written, "PIC link kinds' units are written");
    const char* const shared_units[] = {
        paths[0],
        paths[1],
        paths[2],
    };
    const char* const ordinary_units[] = { ordinary };
    char base[256];
    char output[256];
    char label[256];
    snprintf(base, sizeof(base), "%s/pic-kinds-base.out", directory);
    snprintf(output, sizeof(output), "%s/pic-kinds-flag.out", directory);
    for (uint32_t set = 0; written && set < 2; set += 1)
    {
        const char* const* units = set == 0 ? ordinary_units : shared_units;
        const uint32_t unit_count = set == 0 ? BUSTER_ARRAY_LENGTH(ordinary_units) : BUSTER_ARRAY_LENGTH(shared_units);
        for (uint32_t kind = 0; kind < BUSTER_ARRAY_LENGTH(kinds); kind += 1)
        {
            const char* const kind_name = kinds[kind] != NULL ? kinds[kind] : "the default PIE";
            const bool linked = check_test_driver_pic_link_runs(executable, kinds[kind], NULL, units, unit_count, base);
            snprintf(label, sizeof(label), "PIC link kinds: %s of %u units without PIC links and runs", kind_name, unit_count);
            check_test_expect(test, linked, label);
            for (uint32_t flag = 0; linked && flag < BUSTER_ARRAY_LENGTH(flags); flag += 1)
            {
                const bool ran = check_test_driver_pic_link_runs(executable, kinds[kind], flags[flag], units, unit_count, output);
                snprintf(label, sizeof(label), "PIC link kinds: %s of %u units with %s runs and is the executable without it, byte for byte", kind_name,
                         unit_count, flags[flag]);
                check_test_expect(test, ran && check_test_driver_same_files(base, output), label);
            }
        }
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(texts); i += 1)
    {
        unlink(paths[i]);
    }
    unlink(base);
    unlink(output);
}

// Runs `arguments` (a null-terminated list) and keeps its standard output in `*text` (the caller frees it): its status.
static int check_test_driver_pic_output(char* const* arguments, char** text)
{
    *text = NULL;
    return check_test_driver_spawn_stream(arguments, text, true);
}

// #2101: a shared library of -fPIC's code. Its source has what clang 23.1.4's -fPIC reaches otherwise than -fPIE's: an
// exported object (through its GOT slot), a function called by its PLT entry and whose address goes through its GOT
// slot, an exported thread-local (general-dynamic) and the unit's own (local-dynamic), the program's object it refers
// to, besides a hidden object, a static one and a static function, which stay direct; and its unwind tables, which
// refer to their functions through their section's symbol; then thread-locals of every shape (`shapes`), one of them a
// second unit's, which defines it and has no function. Three programs, linked by the driver against clang's
// library at DIRECTORY/libpic.so: `main` calls it from two threads, `preempt` defines the library's exported object,
// thread-local and function, which the library must then reach (its address of the function, as Tier 1, like clang
// -O1, may expand a call of it), and `dl` (clang's, with -rdynamic) loads it by dlopen. Each library takes clang's place
// in turn, and each program's output must be the one it gives with clang's: buster's -shared at Tier 0 with -fPIC and
// at Tier 1 with -fpic, and its -c object linked by cc -shared; then executables of the program and the object, linked
// by the driver and by cc.
static void check_test_driver_pic_library(CheckTest* test, const char* executable, const char* directory)
{
    static const char library_text[] =
        "int shared_counter = 5;\nstatic int local_counter = 7;\n__attribute__((visibility(\"hidden\"))) int hidden_value = 9;\n"
        "_Thread_local int tls_value = 3;\nstatic _Thread_local int tls_own = 4;\nextern int program_value;\n"
        "int helper(int x) { return x * 2; }\nstatic int private_helper(int x) { return x + 1; }\nconst char* message = \"pic\";\n"
        "int (*helper_pointer)(int) = helper;\nint (*helper_address(void))(int) { return helper; }\nint* tls_address(void) { return &tls_value; }\n"
        "long api(int x)\n{\n    shared_counter += x;\n    local_counter++;\n    tls_value += x;\n    tls_own++;\n"
        "    return ((((long)shared_counter * 100 + local_counter) * 100 + tls_value) * 100 + tls_own) * 10000 + helper(x) + program_value +\n"
        "           private_helper(x) + hidden_value + message[0];\n}\n"
        // Thread-locals' shapes, each access a call of an accessor and the access through its result: updates of
        // every class (long double's in memory), an array, a struct's members and bit-fields, a volatile one, an
        // atomic one, a block-scope one, one whose address is kept, and another unit's.
        "_Thread_local char tls_char = 3;\n_Thread_local _Bool tls_bool;\n_Thread_local float tls_float = 1.5f;\n_Thread_local double tls_double = 2.25;\n"
        "_Thread_local long double tls_long_double = 3.5L;\n_Thread_local int tls_array[4] = { 1, 2, 3, 4 };\n"
        "_Thread_local struct { int a; double b; unsigned bits : 5; unsigned more : 3; } tls_record = { 4, 5.5, 7, 2 };\n"
        "static _Thread_local volatile int tls_volatile = 9;\n_Thread_local _Atomic int tls_atomic = 10;\n_Thread_local int* tls_pointer;\n"
        "extern _Thread_local int tls_other;\n"
        "long shapes(int x)\n{\n    static _Thread_local int calls;\n    long sum = tls_own++ + ++tls_value + (calls += x);\n"
        "    tls_char += (char)x;\n    tls_bool = !tls_bool;\n    tls_float *= 2.0f;\n    tls_double += x;\n    tls_long_double = tls_long_double * 2 + x;\n"
        "    sum += tls_char + tls_bool + (long)tls_float + (long)(tls_double * 4) + (long)tls_long_double;\n"
        "    for (int i = 0; i < 4; i++) { tls_array[i] += i * x; sum += tls_array[i]; }\n"
        "    tls_record.a -= x;\n    tls_record.b *= 1.5;\n    tls_record.bits += 3;\n    tls_record.more ^= 1;\n"
        "    sum += tls_record.a + (long)tls_record.b + tls_record.bits + tls_record.more;\n    tls_volatile = tls_volatile + 1;\n"
        "    tls_atomic += x;\n    tls_pointer = &tls_value;\n    *tls_pointer += 100;\n    sum += tls_volatile + tls_atomic + *tls_pointer + tls_other;\n"
        "    tls_other = x;\n    return sum;\n}\n";
    // A unit with a thread-local and no function: no accessor, and no unwind table.
    static const char data_text[] = "_Thread_local int tls_other = 42;\n";
    static const char main_text[] =
        "#include <pthread.h>\n#include <stdio.h>\nlong api(int);\nextern int shared_counter;\nextern _Thread_local int tls_value;\n"
        "int* tls_address(void);\nlong shapes(int);\nint program_value = 100;\n"
        "static void* worker(void* argument)\n{\n    (void)argument;\n    tls_value = 50;\n    const long first = api(1);\n    const long second = shapes(5);\n"
        "    printf(\"%ld %d %ld\\n\", first, tls_address() == &tls_value, second);\n    return 0;\n}\n"
        // (Each call a statement of its own: the calls change what the others read, in any order of the arguments.)
        "int main(void)\n{\n    long first = api(2);\n    long second = shapes(1);\n    printf(\"%ld %ld\\n\", first, second);\n    tls_value += 10;\n"
        "    first = api(3);\n    second = shapes(2);\n    printf(\"%ld %d %ld\\n\", first, tls_address() == &tls_value, second);\n"
        "    pthread_t thread;\n    if (pthread_create(&thread, 0, worker, 0) != 0 || pthread_join(thread, 0) != 0) return 1;\n"
        "    printf(\"%d %d\\n\", shared_counter, tls_value);\n    return 0;\n}\n";
    static const char preempt_text[] =
        "#include <stdio.h>\nint program_value = 1000;\nint shared_counter = 40;\n_Thread_local int tls_value = 20;\nint helper(int x) { return -x; }\n"
        "long api(int);\nint (*helper_address(void))(int);\nextern int (*helper_pointer)(int);\nint* tls_address(void);\n"
        "int main(void)\n{\n    api(1);\n    printf(\"%d %d %d %d %d\\n\", shared_counter, tls_value, helper_address() == helper, helper_pointer == helper,\n"
        "           tls_address() == &tls_value);\n    return 0;\n}\n";
    static const char dl_text[] =
        "#include <dlfcn.h>\n#include <stdio.h>\nint program_value = 7;\n"
        "int main(int argc, char** argv)\n{\n    void* library = argc > 1 ? dlopen(argv[1], RTLD_NOW) : 0;\n    if (library == 0) return 2;\n"
        "    long (*api)(int) = (long (*)(int))dlsym(library, \"api\");\n    int* tls = (int*)dlsym(library, \"tls_value\");\n"
        "    if (api == 0 || tls == 0) return 3;\n    printf(\"%ld\\n\", api(5));\n    const long last = api(6);\n    printf(\"%ld %d\\n\", last, *tls);\n    return 0;\n}\n";
    char library_source[256];
    char data_source[256];
    char data_object[256];
    char main_source[256];
    char preempt_source[256];
    char dl_source[256];
    char library[256];
    char object[256];
    char programs[3][256];
    char direct[256];
    char rpath[300];
    char label[512];
    snprintf(library_source, sizeof(library_source), "%s/pic-library.c", directory);
    snprintf(data_source, sizeof(data_source), "%s/pic-library-data.c", directory);
    snprintf(data_object, sizeof(data_object), "%s/pic-library-data.o", directory);
    snprintf(main_source, sizeof(main_source), "%s/pic-main.c", directory);
    snprintf(preempt_source, sizeof(preempt_source), "%s/pic-preempt.c", directory);
    snprintf(dl_source, sizeof(dl_source), "%s/pic-dl.c", directory);
    snprintf(library, sizeof(library), "%s/libpic.so", directory);
    snprintf(object, sizeof(object), "%s/pic-library.o", directory);
    snprintf(programs[0], sizeof(programs[0]), "%s/pic-main", directory);
    snprintf(programs[1], sizeof(programs[1]), "%s/pic-preempt", directory);
    snprintf(programs[2], sizeof(programs[2]), "%s/pic-dl", directory);
    snprintf(direct, sizeof(direct), "%s/pic-direct", directory);
    snprintf(rpath, sizeof(rpath), "-Wl,-rpath,%s", directory);
    const bool written = parse_test_write(library_source, library_text, sizeof(library_text) - 1) && parse_test_write(data_source, data_text, sizeof(data_text) - 1) &&
                         parse_test_write(main_source, main_text, sizeof(main_text) - 1) &&
                         parse_test_write(preempt_source, preempt_text, sizeof(preempt_text) - 1) && parse_test_write(dl_source, dl_text, sizeof(dl_text) - 1);
    // clang's library, and the programs against it.
    char* text = NULL;
    char* clang_library[] = { "clang", "-shared", "-fPIC", "-O0", "-Wl,-soname,libpic.so", "-o", library, library_source, data_source, NULL };
    const bool reference = written && check_test_driver_spawn(clang_library, &text) == 0;
    free(text);
    char* link_main[] = { (char*)executable, "-pthread", "-o", programs[0], main_source, library, rpath, NULL };
    char* link_preempt[] = { (char*)executable, "-o", programs[1], preempt_source, library, rpath, NULL };
    char* link_dl[] = { "clang", "-rdynamic", "-o", programs[2], dl_source, NULL };
    char* const* links[3] = { link_main, link_preempt, link_dl };
    bool linked = reference;
    for (uint32_t p = 0; linked && p < 3; p += 1)
    {
        text = NULL;
        linked = check_test_driver_spawn(links[p], &text) == 0;
        free(text);
    }
    check_test_expect(test, linked, "PIC library: clang's library and the programs against it, linked by the driver");
    char* expected[3] = { NULL, NULL, NULL };
    char* run_main[] = { programs[0], NULL };
    char* run_preempt[] = { programs[1], NULL };
    char* run_dl[] = { programs[2], library, NULL };
    char* const* runs[3] = { run_main, run_preempt, run_dl };
    bool expectations = linked;
    for (uint32_t p = 0; expectations && p < 3; p += 1)
    {
        expectations = check_test_driver_pic_output(runs[p], &expected[p]) == 0 && expected[p] != NULL;
    }
    check_test_expect(test, expectations && strcmp(expected[1], "41 21 1 1 1\n") == 0, "PIC library: clang's library runs, preempted by the program");
    // Buster's libraries in its place.
    char* tier0[] = { (char*)executable, "-shared", "-fPIC", "-Wl,-soname,libpic.so", "-o", library, library_source, data_source, NULL };
    char* tier1[] = { (char*)executable, "-shared", "-fpic", "--tier1", "-Wl,-soname=libpic.so", "-o", library, library_source, data_source, NULL };
    char* compiled[] = { (char*)executable, "-c", "-fPIC", "-o", object, library_source, NULL };
    char* compiled_data[] = { (char*)executable, "-c", "-fPIC", "-o", data_object, data_source, NULL };
    char* system_link[] = { "cc", "-shared", "-Wl,-soname,libpic.so", "-o", library, object, data_object, NULL };
    static const char* const variants[] = {
        "-shared -fPIC",
        "-shared -fpic --tier1",
        "-c -fPIC and cc -shared",
    };
    for (uint32_t v = 0; expectations && v < BUSTER_ARRAY_LENGTH(variants); v += 1)
    {
        unlink(library);
        text = NULL;
        bool built = check_test_driver_spawn(v == 0 ? tier0 : v == 1 ? tier1 : compiled, &text) == 0;
        free(text);
        if (built && v == 2)
        {
            text = NULL;
            built = check_test_driver_spawn(compiled_data, &text) == 0;
            free(text);
            text = NULL;
            built = built && check_test_driver_spawn(system_link, &text) == 0;
            free(text);
        }
        snprintf(label, sizeof(label), "PIC library: buster's library by %s links", variants[v]);
        check_test_expect(test, built, label);
        for (uint32_t p = 0; built && p < 3; p += 1)
        {
            char* output = NULL;
            const int status = check_test_driver_pic_output(runs[p], &output);
            snprintf(label, sizeof(label), "PIC library: buster's library by %s runs %s as clang's does", variants[v], p == 0 ? "the threads" : p == 1 ? "the preempting program" : "dlopen's harness");
            check_test_expect(test, status == 0 && output != NULL && strcmp(output, expected[p]) == 0, label);
            free(output);
        }
    }
    // The object in an executable: the driver's link (the mixed path's writer) and cc's relax its general- and
    // local-dynamic accesses and its GOT loads.
    char* link_direct[] = { (char*)executable, "-pthread", "-o", direct, main_source, object, data_object, NULL };
    char* system_direct[] = { "cc", "-pthread", "-o", direct, main_source, object, data_object, NULL };
    char* run_direct[] = { direct, NULL };
    for (uint32_t l = 0; expectations && l < 2; l += 1)
    {
        unlink(direct);
        text = NULL;
        const bool built = access(object, F_OK) == 0 && access(data_object, F_OK) == 0 && check_test_driver_spawn(l == 0 ? link_direct : system_direct, &text) == 0;
        free(text);
        char* output = NULL;
        const int status = built ? check_test_driver_pic_output(run_direct, &output) : -1;
        snprintf(label, sizeof(label), "PIC library: an executable of the program and the -fPIC object, linked by %s, runs as clang's library does", l == 0 ? "the driver" : "cc");
        check_test_expect(test, status == 0 && output != NULL && strcmp(output, expected[0]) == 0, label);
        free(output);
    }
    for (uint32_t p = 0; p < 3; p += 1)
    {
        free(expected[p]);
        unlink(programs[p]);
    }
    const char* const files[] = { library_source, data_source, main_source, preempt_source, dl_source, library, object, data_object, direct };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        unlink(files[f]);
    }
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
                    // -c's and check -c's object, which a shared library may take (#2101), and a link's executable, which
                    // runs, each replacing what was at its path.
                    const bool linked = mode == BUSTER_CHECK_PIC_LINK;
                    for (uint32_t existing = 0; existing < 2; existing += 1)
                    {
                        unlink(output);
                        const bool prepared = existing == 0 || parse_test_write(output, sentinel, sizeof(sentinel) - 1);
                        const char* options[] = { flags[flag] };
                        char* text = NULL;
                        const int status = check_test_driver_pic_command(executable, source, output, scalar != 0, (CheckPicAction)mode,
                                                                        options, BUSTER_ARRAY_LENGTH(options), &text);
                        const bool written = status == 0 && access(output, F_OK) == 0 && !check_test_driver_same_files(saved, output);
                        char* run[] = { output, NULL };
                        char* run_text = NULL;
                        const int ran = linked && written ? check_test_driver_spawn(run, &run_text) : 0;
                        free(run_text);
                        snprintf(label, sizeof(label), "PIC %s fixture=%u scalar=%u flag=%s mode=%u existing=%u", linked ? "link" : "object", fixture, scalar,
                                 flags[flag], mode, existing);
                        check_test_expect(test, prepared && written && ran == 0, label);
                        free(text);
                    }
                }
            }
        }
    }
    // The complete PIC/PIE family shares one last-option state in the predefines parser. Check both orders: the last
    // decides between -fPIC's object and the one without it, which differ (the exported object through its GOT slot).
    const bool ordinary = parse_test_write(source, programs[0], strlen(programs[0]));
    check_test_expect(test, ordinary, "PIC option-order fixture is written");
    if (ordinary)
    {
        check_test_driver_pic_kinds(test, executable, directory, source);
        check_test_driver_pic_library(test, executable, directory);
    }
    char plain[256];
    char positioned[256];
    snprintf(plain, sizeof(plain), "%s/pic-driver-plain.o", directory);
    snprintf(positioned, sizeof(positioned), "%s/pic-driver-pic.o", directory);
    const char* pic_option[] = { "-fPIC" };
    char* reference_text = NULL;
    unlink(plain);
    unlink(positioned);
    const bool references = ordinary && check_test_driver_pic_command(executable, source, plain, false, BUSTER_CHECK_PIC_OBJECT, NULL, 0, &reference_text) == 0;
    free(reference_text);
    reference_text = NULL;
    const bool pic_reference = references && check_test_driver_pic_command(executable, source, positioned, false, BUSTER_CHECK_PIC_OBJECT, pic_option, 1, &reference_text) == 0;
    free(reference_text);
    check_test_expect(test, pic_reference && !check_test_driver_same_files(plain, positioned), "PIC option order: -fPIC's object isn't the one without it");
    for (uint32_t flag = 0; pic_reference && flag < BUSTER_ARRAY_LENGTH(flags); flag += 1)
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
                snprintf(label, sizeof(label), "PIC option order %s %s is %s object", options[0], options[1], reverse != 0 ? "-fPIC's" : "the plain");
                check_test_expect(test, status == 0 && check_test_driver_same_files(reverse != 0 ? positioned : plain, output), label);
                free(text);
            }
        }
        const char* repeated[] = { flags[1 - flag], flags[flag] };
        char* text = NULL;
        unlink(output);
        const int status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_OBJECT,
                                                        repeated, BUSTER_ARRAY_LENGTH(repeated), &text);
        check_test_expect(test, status == 0 && check_test_driver_same_files(positioned, output), "PIC option order: -fpic's code is -fPIC's");
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
    // A link's predefined macros are -E's, which are clang's for the same options (clang 23.1.4's -dM -E, #2101): the last
    // of the eight PIC and PIE options decides, and -fPIC and -fpic leave __PIE__ undefined. The executable's exit status
    // is __PIC__ * 10 + __PIE__, either 0 where it is undefined.
    static const char level_program[] =
        "#if defined(__pic__) != defined(__PIC__) || defined(__pie__) != defined(__PIE__) || __pic__ != __PIC__ || __pie__ != __PIE__\n"
        "#error the lower-case PIC macros differ\n#endif\n"
        "#ifdef __PIC__\n#define PIC_LEVEL __PIC__\n#else\n#define PIC_LEVEL 0\n#endif\n"
        "#ifdef __PIE__\n#define PIE_LEVEL __PIE__\n#else\n#define PIE_LEVEL 0\n#endif\n"
        "int main(void) { return PIC_LEVEL * 10 + PIE_LEVEL; }\n";
    static const CheckPicLevel levels[] = {
        {
            .first = NULL,
            .second = NULL,
            .status = 22,
        },
        {
            .first = "-fPIC",
            .second = NULL,
            .status = 20,
        },
        {
            .first = "-fpic",
            .second = NULL,
            .status = 10,
        },
        {
            .first = "-fPIC",
            .second = "-fPIE",
            .status = 22,
        },
        {
            .first = "-fPIE",
            .second = "-fPIC",
            .status = 20,
        },
        {
            .first = "-fpic",
            .second = "-fPIE",
            .status = 22,
        },
        {
            .first = "-fPIE",
            .second = "-fpic",
            .status = 10,
        },
        {
            .first = "-fPIC",
            .second = "-fpie",
            .status = 11,
        },
        {
            .first = "-fpie",
            .second = "-fPIC",
            .status = 20,
        },
        {
            .first = "-fPIC",
            .second = "-fpic",
            .status = 10,
        },
        {
            .first = "-fpic",
            .second = "-fPIC",
            .status = 20,
        },
        {
            .first = "-fPIC",
            .second = "-fno-PIC",
            .status = 0,
        },
        {
            .first = "-fno-PIC",
            .second = "-fPIC",
            .status = 20,
        },
        {
            .first = "-fpic",
            .second = "-fno-pic",
            .status = 0,
        },
        {
            .first = "-fno-pic",
            .second = "-fpic",
            .status = 10,
        },
        {
            .first = "-fPIC",
            .second = "-fno-pie",
            .status = 0,
        },
        {
            .first = "-fno-pie",
            .second = "-fPIC",
            .status = 20,
        },
    };
    const bool levels_written = parse_test_write(source, level_program, sizeof(level_program) - 1);
    check_test_expect(test, levels_written, "PIC link level fixture is written");
    for (uint32_t i = 0; levels_written && i < BUSTER_ARRAY_LENGTH(levels); i += 1)
    {
        char* text = NULL;
        unlink(output);
        const char* options[] = { levels[i].first, levels[i].second };
        const uint32_t count = levels[i].second != NULL ? 2u : levels[i].first != NULL ? 1u : 0u;
        const int status = check_test_driver_pic_command(executable, source, output, false, BUSTER_CHECK_PIC_LINK, options, count, &text);
        char* run[] = { output, NULL };
        char* run_text = NULL;
        const int ran = status == 0 ? check_test_driver_spawn(run, &run_text) : -1;
        free(run_text);
        snprintf(label, sizeof(label), "PIC link levels %s %s: status %d", count > 0 ? levels[i].first : "(none)", count > 1 ? levels[i].second : "", levels[i].status);
        check_test_expect(test, status == 0 && ran == levels[i].status, label);
        free(text);
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
    unlink(plain);
    unlink(positioned);
}
