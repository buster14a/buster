// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original #1135 object/link regressions. Include after the driver ELF and visibility readers.

static void check_test_driver_symbol_pragmas(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source[] =
        "#pragma weak fallback\n#pragma weak weak_data\n#pragma weak missing\n"
        "#pragma redefine_extname source_function exported_function\n"
        "#pragma redefine_extname source_data exported_data\n"
        "#pragma redefine_extname source_import imported_data\n"
        "#pragma weak renamed_weak\n#pragma redefine_extname renamed_weak exported_weak\n"
        "int fallback(void);int fallback(void){return 3;}\nint weak_data=4;\nextern int missing(void);\n"
        "int source_function(void){return 5;}\nint source_data=6;extern int source_import;\n"
        "int renamed_weak(void){return 7;}\n"
        "int exercise(void){return fallback()+weak_data+(missing?missing():0)+source_function()+source_data+source_import+renamed_weak();}\n";
    static const char normal[] = "int imported_data=8;int exercise(void);int exported_function(void);extern int exported_data;int main(void){return exercise()!=33||exported_function()!=5||exported_data!=6;}\n";
    static const char strong[] = "int imported_data=8;int fallback(void){return 30;}int weak_data=40;int missing(void){return 50;}int exported_weak(void){return 70;}int exercise(void);int main(void){return exercise()!=209;}\n";
    static const struct
    {
        const char* name;
        uint8_t binding;
    } symbols[] =
    {
        { "fallback", STB_WEAK }, { "weak_data", STB_WEAK }, { "missing", STB_WEAK },
        { "exported_function", STB_GLOBAL }, { "exported_data", STB_GLOBAL },
        { "imported_data", STB_GLOBAL }, { "exported_weak", STB_WEAK }, { "exercise", STB_GLOBAL },
    };
    char path[512];
    char partner[512];
    char object[512];
    char program[512];
    snprintf(path, sizeof(path), "%s/symbol-pragmas.c", directory);
    snprintf(partner, sizeof(partner), "%s/symbol-pragmas-partner.c", directory);
    snprintf(object, sizeof(object), "%s/symbol-pragmas.o", directory);
    snprintf(program, sizeof(program), "%s/symbol-pragmas", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1);
    check_test_expect(test, written, "symbol pragma link fixture is written");
    for (uint32_t pass = 0; written && pass < 5; pass += 1)
    {
        const bool scalar = pass == 2 || pass == 4;
        const bool lazy = pass >= 3;
        char* diagnostics = NULL;
        char* arguments[] = { (char*)(pass == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", object,
                              lazy ? "-flazy-system-headers" : "-fno-lazy-system-headers", scalar ? "--scalar" : NULL, NULL };
        // Clang has no lazy-system-headers driver option.
        if (pass == 0)
        {
            arguments[7] = NULL;
        }
        const bool compiled = check_test_driver_spawn(arguments, &diagnostics) == 0;
        check_test_expect(test, compiled, "symbol pragmas compile through Clang and scalar/vector eager/lazy paths");
        if (!compiled)
        {
            fprintf(stderr, "symbol pragma pass %u: %s\n", pass, diagnostics != NULL ? diagnostics : "");
        }
        free(diagnostics);
        for (uint32_t s = 0; compiled && s < BUSTER_ARRAY_LENGTH(symbols); s += 1)
        {
            uint16_t flags = UINT16_MAX;
            check_test_expect(test, check_test_visibility_symbol(object, symbols[s].name, &flags) && flags == (uint16_t)(symbols[s].binding << 8),
                              "symbol pragma ELF binding and emitted name match the independently stated contract");
        }
        uint16_t ignored = 0;
        check_test_expect(test, !compiled || (!check_test_visibility_symbol(object, "source_function", &ignored) &&
                                             !check_test_visibility_symbol(object, "source_data", &ignored) &&
                                             !check_test_visibility_symbol(object, "source_import", &ignored) &&
                                             !check_test_visibility_symbol(object, "renamed_weak", &ignored)),
                          "external-name directives emit no stale source-spelling symbols");
        for (uint32_t override = 0; compiled && override < 2; override += 1)
        {
            const char* other = override != 0 ? strong : normal;
            const bool prepared = parse_test_write(partner, other, strlen(other));
            diagnostics = NULL;
            char* link[] = { (char*)clang, "-no-pie", object, partner, "-o", program, NULL };
            const bool linked = prepared && check_test_driver_spawn(link, &diagnostics) == 0;
            check_test_expect(test, linked, "pragma object links against separately compiled C names");
            free(diagnostics);
            diagnostics = NULL;
            char* run[] = { program, NULL };
            check_test_expect(test, linked && check_test_driver_spawn(run, &diagnostics) == 0,
                              override != 0 ? "strong definitions override weak pragma functions/data and renamed weak symbols" :
                                              "undefined weak references are null and renamed function/data relocations execute");
            free(diagnostics);
        }
    }
    // A real unreferenced system-header body must be selected out in the lazy route. An unrelated skipped body
    // must not suppress file-scope pragma metadata; a skipped earlier spelling of the same name must fail closed.
    char header[512];
    snprintf(header, sizeof(header), "%s/symbol-lazy.h", directory);
    static const char unrelated_header[] = "#pragma GCC system_header\nstatic inline int unused_system(void){return 7;}\n";
    static const char unrelated_source[] = "#include \"symbol-lazy.h\"\n#pragma weak lazy_weak\n#pragma redefine_extname lazy_name lazy_export\nint lazy_weak(void){return 3;}\nint lazy_name=4;\n";
    bool prepared = parse_test_write(header, unrelated_header, sizeof(unrelated_header) - 1) &&
                    parse_test_write(path, unrelated_source, sizeof(unrelated_source) - 1);
    check_test_expect(test, prepared, "unrelated lazy symbol pragma fixture is written");
    for (uint32_t pass = 0; prepared && pass < 3; pass += 1)
    {
        unlink(object);
        char* diagnostics = NULL;
        char* arguments[] = { (char*)executable, "-std=gnu17", "-c", path, "-o", object,
                              pass == 0 ? "-fno-lazy-system-headers" : "-flazy-system-headers", pass == 2 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &diagnostics) == 0;
        uint16_t weak = UINT16_MAX;
        uint16_t renamed = UINT16_MAX;
        uint16_t stale = UINT16_MAX;
        check_test_expect(test, compiled && check_test_visibility_symbol(object, "lazy_weak", &weak) && weak == (uint16_t)(STB_WEAK << 8) &&
                                check_test_visibility_symbol(object, "lazy_export", &renamed) && renamed == (uint16_t)(STB_GLOBAL << 8) &&
                                !check_test_visibility_symbol(object, "lazy_name", &stale),
                          "symbol pragmas survive an unrelated deferred system-header body");
        if (!compiled)
        {
            fprintf(stderr, "symbol pragma lazy pass %u: %s\n", pass, diagnostics != NULL ? diagnostics : "");
        }
        free(diagnostics);
    }
    static const char hidden_header[] = "#pragma GCC system_header\nstatic inline int unused_system(void){return late_f;}\n";
    static const char hidden_source[] = "extern int late_f;\n#include \"symbol-lazy.h\"\n#pragma weak late_f\n";
    prepared = parse_test_write(header, hidden_header, sizeof(hidden_header) - 1) && parse_test_write(path, hidden_source, sizeof(hidden_source) - 1);
    for (uint32_t scalar = 0; prepared && scalar < 2; scalar += 1)
    {
        char* diagnostics = NULL;
        char* arguments[] = { (char*)executable, "-std=gnu17", "-c", path, "-o", object, "-flazy-system-headers", scalar ? "--scalar" : NULL, NULL };
        const int status = check_test_driver_spawn(arguments, &diagnostics);
        check_test_expect(test, status == 1 && diagnostics != NULL && strstr(diagnostics, "unsupported semantic") != NULL,
                          "a hidden earlier spelling of the pragma name fails closed in lazy mode");
        free(diagnostics);
    }
    check_test_expect(test, prepared, "lazy symbol pragma fixtures are written");
    unlink(header);
    unlink(path);
    unlink(partner);
    unlink(object);
    unlink(program);
}

// #2449: late alias targets remain visible after the alias's actual error. Sources and the case inventory are runtime
// data; the ordinary source-fixture loader owns their bytes and rejects missing, duplicate and orphaned cases.
// A declared but undefined target is accepted by Sema and rejected only by code generation. Each action therefore
// has its own oracle; the codegen catalogue's rejected flag applies only to emitting commands.
static void check_test_driver_weak_alias_diagnostic_suite(CheckTest* test, const char* clang, const char* executable, const char* directory, const char* suite, bool codegen)
{
    CheckTestFixtures fixtures;
    const bool loaded = check_test_fixtures_load(&fixtures, suite);
    check_test_expect(test, loaded, loaded ? "weak alias diagnostic fixtures loaded" : fixtures.problem);
    char source[PATH_MAX];
    char object[PATH_MAX];
    char oracle_object[PATH_MAX];
    char expected_lines[2][8192];
    char actual_lines[8192];
    snprintf(source, sizeof(source), "%s/weak-alias-diagnostic.c", directory);
    snprintf(object, sizeof(object), "%s/weak-alias-diagnostic.o", directory);
    snprintf(oracle_object, sizeof(oracle_object), "%s/weak-alias-diagnostic-clang.o", directory);
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    static const char* const policies[] = { "-ferror-limit=0", "-ferror-limit=1", "-w", "-Werror=ignored-attributes" };
    static const char sentinel[] = "preserve the existing weak alias object\n";
    for (uint32_t f = 0; loaded && f < fixtures.count; f += 1)
    {
        const CheckSourceFixture* fixture = fixtures.cases + f;
        const bool written = parse_test_write(source, fixture->source, strlen(fixture->source));
        check_test_expect(test, written, "weak alias diagnostic source written");
        for (uint32_t standard = 0; written && standard < BUSTER_ARRAY_LENGTH(standards); standard += 1)
        {
            for (uint32_t policy = 0; policy < BUSTER_ARRAY_LENGTH(policies); policy += 1)
            {
                char* expected[2] = { NULL, NULL };
                int oracle[2] = { -1, -1 };
                bool lines[2] = { false, false };
                for (uint32_t action = 0; action < 2; action += 1)
                {
                    char* oracle_arguments[] = { (char*)clang, (char*)standards[standard], action != 0 ? "-c" : "-fsyntax-only",
                                                "-fno-caret-diagnostics", source, (char*)policies[policy], "-o", oracle_object, NULL };
                    unlink(oracle_object);
                    oracle[action] = check_test_driver_spawn(oracle_arguments, expected + action);
                    if (expected[action] != NULL)
                    {
                        // Scratch-buffer line numbers are compiler-internal; retain the spelling column and all
                        // real source coordinates and macro backtraces.
                        pp_test_normalize_scratch(expected[action]);
                    }
                    lines[action] = (oracle[action] == 0 || oracle[action] == 1) && expected[action] != NULL &&
                                    check_test_diagnostic_lines(expected[action], expected_lines[action], sizeof(expected_lines[action]));
                    const bool rejected = fixture->rejected && (!codegen || action != 0);
                    char description[512];
                    snprintf(description, sizeof(description), "%s, %s %s %s: weak alias oracle agrees with independent acceptance expectation",
                             fixture->name, standards[standard], policies[policy], action != 0 ? "object" : "syntax");
                    check_test_expect(test, lines[action] && oracle[action] == (rejected ? 1 : 0), description);
                    if (!lines[action] || oracle[action] != (rejected ? 1 : 0))
                    {
                        fprintf(stderr, "%s\nclang:\n%s\n", description, expected[action] != NULL ? expected[action] : "(no output)");
                    }
                    unlink(oracle_object);
                }
                for (uint32_t mode = 0; lines[0] && lines[1] && mode < 6; mode += 1)
                {
                    const bool emitting = mode >= 2;
                    const bool deferred = mode >= 4;
                    const uint32_t action = emitting ? 1 : 0;
                    const bool rejected = fixture->rejected && (!codegen || emitting);
                    char* actual = NULL;
                    char* arguments[16] = { (char*)executable, (char*)standards[standard], emitting ? "-c" : "-fsyntax-only",
                                           "-fno-caret-diagnostics", source, (char*)policies[policy] };
                    uint32_t count = 6;
                    if (mode == 1 || mode == 3)
                    {
                        arguments[count++] = "--scalar";
                    }
                    if (emitting)
                    {
                        arguments[count++] = "-o";
                        arguments[count++] = object;
                    }
                    if (deferred)
                    {
                        arguments[count++] = "--deferred-bodies";
                        arguments[count++] = "--lanes";
                        arguments[count++] = mode == 4 ? "1" : "3";
                    }
                    arguments[count] = NULL;
                    unlink(object);
                    const bool existing = emitting && (mode & 1u) != 0;
                    bool published = !existing || parse_test_write(object, sentinel, sizeof(sentinel) - 1);
                    const int status = check_test_driver_spawn(arguments, &actual);
                    if (actual != NULL)
                    {
                        pp_test_normalize_scratch(actual);
                    }
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, sizeof(actual_lines)) &&
                                      strcmp(expected_lines[action], actual_lines) == 0;
                    if (emitting && (existing || !rejected))
                    {
                        const int descriptor = open(object, O_RDONLY);
                        char* contents = NULL;
                        struct stat information = { 0 };
                        published &= descriptor >= 0 && fstat(descriptor, &information) == 0 &&
                                     check_test_driver_read_file(descriptor, &contents) && contents != NULL;
                        if (descriptor >= 0)
                        {
                            close(descriptor);
                        }
                        published &= contents != NULL && (rejected ? information.st_size == (off_t)(sizeof(sentinel) - 1) &&
                                                                    memcmp(contents, sentinel, sizeof(sentinel) - 1) == 0 :
                                                                    information.st_size >= 4 && memcmp(contents, "\177ELF", 4) == 0);
                        free(contents);
                    }
                    else if (emitting)
                    {
                        published &= access(object, F_OK) != 0;
                    }
                    const bool correct = same && status == oracle[action] && published;
                    char description[512];
                    snprintf(description, sizeof(description), "%s, %s %s mode %u: status %d (clang %d), complete diagnostic output and publication", fixture->name,
                             standards[standard], policies[policy], mode, status, oracle[action]);
                    check_test_expect(test, correct, description);
                    if (!correct)
                    {
                        fprintf(stderr, "%s\nours:\n%s\nclang:\n%s\n", description, actual != NULL ? actual : "(no output)", expected[action]);
                    }
                    free(actual);
                    unlink(object);
                }
                free(expected[0]);
                free(expected[1]);
            }
        }
    }
    unlink(source);
    arena_release(&fixtures.storage);
}

static void check_test_driver_weak_alias_diagnostics(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_weak_alias_diagnostic_suite(test, clang, executable, directory, "checker/weak-alias", false);
    check_test_driver_weak_alias_diagnostic_suite(test, clang, executable, directory, "checker/weak-alias-codegen", true);
}

// A non-common tentative definition and its alias are definitions in the same ordinary ELF section. Checking the
// section as well as binding prevents an accepted control from keeping SHN_COMMON or an undefined alias unnoticed.
static bool check_test_weak_alias_common_symbols(const char* path, uint8_t binding)
{
    Arena storage = arena_reserve(UINT64_C(1) << 20);
    CheckTestElf elf = { 0 };
    bool valid = storage.base != NULL && check_test_elf_open(&elf, object_test_read_file(&storage, path));
    const uint32_t target = valid ? check_test_elf_defined(&elf, "common_target") : 0;
    const uint32_t alias = valid ? check_test_elf_defined(&elf, "common_alias") : 0;
    uint16_t target_flags = UINT16_MAX;
    uint16_t alias_flags = UINT16_MAX;
    valid &= target != 0 && alias == target && check_test_visibility_symbol(path, "common_target", &target_flags) &&
             check_test_visibility_symbol(path, "common_alias", &alias_flags) && target_flags == (uint16_t)(STB_GLOBAL << 8) &&
             alias_flags == (uint16_t)(binding << 8);
    arena_release(&storage);
    return valid;
}

// #2449's common-symbol boundary deliberately remains a named refusal: Clang's integrated assembler rejects the
// assignment, rather than Sema rejecting the declaration. Every compiler mode must preserve an existing output;
// -fno-common independently checks that the same source still emits the alias, including a pragma-only clone.
static void check_test_driver_weak_alias_common(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    CheckTestFixtures fixtures;
    const bool loaded = check_test_fixtures_load(&fixtures, "checker/weak-alias-common");
    check_test_expect(test, loaded, loaded ? "common-target alias fixtures loaded" : fixtures.problem);
    char source[PATH_MAX];
    char object[PATH_MAX];
    char oracle_object[PATH_MAX];
    snprintf(source, sizeof(source), "%s/weak-alias-common.c", directory);
    snprintf(object, sizeof(object), "%s/weak-alias-common.o", directory);
    snprintf(oracle_object, sizeof(oracle_object), "%s/weak-alias-common-clang.o", directory);
    static const char sentinel[] = "keep the existing common-target alias object\n";
    for (uint32_t f = 0; loaded && f < fixtures.count; f += 1)
    {
        const CheckSourceFixture* fixture = fixtures.cases + f;
        const bool written = parse_test_write(source, fixture->source, strlen(fixture->source));
        check_test_expect(test, written, "common-target alias source written");
        const uint8_t binding = strcmp(fixtures.files[f], "attribute.c") == 0 ? STB_GLOBAL : STB_WEAK;
        for (uint32_t common = 0; written && common < 2; common += 1)
        {
            const char* option = common != 0 ? "-fcommon" : "-fno-common";
            const bool rejected = common != 0 && fixture->rejected;
            char* expected = NULL;
            char* oracle_arguments[] = { (char*)clang, "-std=gnu17", "-O0", "-c", (char*)option, "-fno-caret-diagnostics", source,
                                        "-o", oracle_object, NULL };
            unlink(oracle_object);
            const int oracle = check_test_driver_spawn(oracle_arguments, &expected);
            const bool oracle_valid = expected != NULL && (rejected ? oracle == 1 && strstr(expected, "Common symbol") != NULL :
                                                                                 oracle == 0 && check_test_weak_alias_common_symbols(oracle_object, binding));
            char description[512];
            snprintf(description, sizeof(description), "%s %s: Clang rejects common aliases and defines the non-common control", fixture->name, option);
            check_test_expect(test, oracle_valid, description);
            if (!oracle_valid)
            {
                fprintf(stderr, "%s\nclang:\n%s\n", description, expected != NULL ? expected : "(no output)");
            }
            free(expected);
            unlink(oracle_object);
            for (uint32_t mode = 0; mode < 6; mode += 1)
            {
                const bool prior = parse_test_write(object, sentinel, sizeof(sentinel) - 1);
                char* actual = NULL;
                char* arguments[16] = { (char*)executable, "-std=gnu17", "-O0", "-c", (char*)option, "-fno-caret-diagnostics", source,
                                       "-o", object };
                uint32_t count = 9;
                if ((mode & 1u) != 0)
                {
                    arguments[count++] = "--scalar";
                }
                if (mode >= 2)
                {
                    arguments[count++] = "--deferred-bodies";
                    arguments[count++] = "--lanes";
                    arguments[count++] = mode < 4 ? "1" : "3";
                }
                arguments[count] = NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                bool correct = prior && actual != NULL && status == (rejected ? 1 : 0);
                if (rejected)
                {
                    const int descriptor = open(object, O_RDONLY);
                    struct stat information = { 0 };
                    char* contents = NULL;
                    const bool preserved = descriptor >= 0 && fstat(descriptor, &information) == 0 &&
                                           information.st_size == (off_t)(sizeof(sentinel) - 1) && check_test_driver_read_file(descriptor, &contents) &&
                                           contents != NULL && memcmp(contents, sentinel, sizeof(sentinel) - 1) == 0;
                    if (descriptor >= 0)
                    {
                        close(descriptor);
                    }
                    correct &= preserved && actual != NULL && strstr(actual, "common symbol") != NULL;
                    free(contents);
                }
                else
                {
                    correct &= check_test_weak_alias_common_symbols(object, binding);
                }
                snprintf(description, sizeof(description), "%s %s mode %u: common alias refusal preserves output, non-common alias retains its binding",
                         fixture->name, option, mode);
                check_test_expect(test, correct, description);
                if (!correct)
                {
                    fprintf(stderr, "%s\nstatus %d, ours:\n%s\n", description, status, actual != NULL ? actual : "(no output)");
                }
                free(actual);
                unlink(object);
                if (rejected)
                {
                    actual = NULL;
                    const int absent_status = check_test_driver_spawn(arguments, &actual);
                    const bool absent = absent_status == 1 && actual != NULL && strstr(actual, "common symbol") != NULL && access(object, F_OK) != 0;
                    snprintf(description, sizeof(description), "%s -fcommon mode %u: rejected common alias creates no output", fixture->name, mode);
                    check_test_expect(test, absent, description);
                    if (!absent)
                    {
                        fprintf(stderr, "%s\nstatus %d, ours:\n%s\n", description, absent_status, actual != NULL ? actual : "(no output)");
                    }
                    free(actual);
                    unlink(object);
                }
            }
        }
    }
    unlink(source);
    arena_release(&fixtures.storage);
}

// `#pragma weak A = T` (#1849): the weak aliases, each symbol's binding and visibility clang's by both paths, whose
// objects must be the same bytes, linked and run as clang's object: the shapes whose A the unit declares, and clang's
// clones of T's declaration (an A never declared, of a function or a variable, a static, a T defined later; an A declared
// before the pragma, never named, of another kind, type, thread-local storage or with a section); an A that is an alias
// already, which keeps its own; and the warning for a T never declared, line for line clang's. Deferred runs must retain
// each target and publish its alias exactly as the eager reader does.
static void check_test_driver_weak_aliases(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source[] =
        "#include <stdio.h>\n"
        "int t(void) { return 1; }\nlong w_storage = 5;\nstatic int s(void) { return 4; }\n"
        "int a(void);\nextern long v;\nint ps(void);\nint b(void);\nint c(void) { return 3; }\n"
        "long k1(void);\nextern int k2;\nextern long k3 __attribute__((section(\"sx\")));\n__thread int tls_t = 3;\nextern int k4;\n"
        "static int st = 6;\nlong other = 8;\nextern long k5 __attribute__((alias(\"other\")));\nlong get_k6(void);\n"
        "int main(void) { printf(\"%d %ld %d %d %d %ld %ld %d\\n\", a(), v, ps(), b(), c(), k5, get_k6(), tls_t); return 0; }\n"
        "#pragma weak a = t\n#pragma weak v = w_storage\n#pragma weak ps = s\n#pragma weak c = t\n#pragma weak b = later\n"
        "#pragma weak u1 = t\n#pragma weak u2 = w_storage\n#pragma weak k1 = w_storage\n#pragma weak k2 = w_storage\n#pragma weak k3 = w_storage\n"
        "#pragma weak k4 = tls_t\n#pragma weak u3 = st\n#pragma weak k5 = w_storage\n#pragma weak u4 = later2\n#pragma weak k6 = w_storage\n"
        "extern long k6;\nlong get_k6(void) { return k6; }\n"
        "int later(void) { return 9; }\nint later2(void) { return 11; }\n";
    static const char* const names[] = {
        "t", "w_storage", "s", "a", "v", "ps", "b", "c", "later", "main", "u1", "u2", "u3", "u4", "k1", "k2", "k3", "k4", "k5", "k6", "st", "other", "tls_t",
    };
    char path[512];
    char objects[5][512] = { { 0 } };
    char programs[5][512] = { { 0 } };
    char captured[5][256] = { { 0 } };
    snprintf(path, sizeof(path), "%s/weak-aliases.c", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1);
    check_test_expect(test, written, "the weak alias pragmas' fixture is written");
    for (uint32_t p = 0; written && p < 5; p += 1)
    {
        snprintf(objects[p], sizeof(objects[p]), "%s/weak-aliases-%u.o", directory, p);
        snprintf(programs[p], sizeof(programs[p]), "%s/weak-aliases-%u", directory, p);
        char* text = NULL;
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", objects[p],
                              p >= 3 ? "--deferred-bodies" : p == 2 ? "--scalar" : NULL,
                              p >= 3 ? "--lanes" : NULL, p == 3 ? "1" : p == 4 ? "3" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        check_test_expect(test, compiled, "weak alias pragmas compile through clang and eager/deferred checker paths");
        if (!compiled)
        {
            fprintf(stderr, "weak alias pragma object pass %u did not compile:\n%s\n", p, text != NULL ? text : "(no output)");
        }
        free(text);
        text = NULL;
        for (uint32_t n = 0; compiled && p != 0 && n < BUSTER_ARRAY_LENGTH(names); n += 1)
        {
            uint16_t ours = UINT16_MAX;
            uint16_t theirs = UINT16_MAX - 1;
            char description[160];
            snprintf(description, sizeof(description), "a weak alias pragma leaves %s's binding clang's, by path %u", names[n], p);
            check_test_expect(test, check_test_visibility_symbol(objects[p], names[n], &ours) && check_test_visibility_symbol(objects[0], names[n], &theirs) &&
                                   ours == theirs, description);
        }
        char* const link[] = { (char*)clang, objects[p], "-o", programs[p], NULL };
        const bool linked = compiled && check_test_driver_spawn(link, &text) == 0;
        free(text);
        check_test_expect(test, linked && check_test_driver_capture(programs[p], captured[p], sizeof(captured[p])), "weak alias pragmas' objects link and run");
    }
    check_test_expect(test, written && check_test_driver_same_files(objects[1], objects[2]), "weak alias pragmas' scalar and vector objects match byte for byte");
    for (uint32_t p = 1; p < 5; p += 1)
    {
        check_test_expect(test, captured[0][0] != 0 && strcmp(captured[p], captured[0]) == 0,
                          "weak alias pragmas' eager/deferred programs run as clang's");
    }
    for (uint32_t p = 0; p < 5; p += 1)
    {
        unlink(objects[p]);
        unlink(programs[p]);
    }
    // A target never declared: clang's warning at the alias's name, by both paths, at -fsyntax-only.
    static const char undeclared[] = "#pragma weak d = undeclared_target\nint x;\n";
    const bool shape = parse_test_write(path, undeclared, sizeof(undeclared) - 1);
    char* theirs = NULL;
    char* const clang_arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", path, NULL };
    const int clang_status = shape ? check_test_driver_spawn(clang_arguments, &theirs) : -1;
    for (uint32_t p = 0; shape && p < 2; p += 1)
    {
        char* ours = NULL;
        char* arguments[] = { (char*)executable, "-std=gnu17", "-fsyntax-only", path, p == 1 ? "--scalar" : NULL, NULL };
        const int status = check_test_driver_spawn(arguments, &ours);
        check_test_expect(test, status == clang_status && ours != NULL && theirs != NULL && strstr(ours, "warning: weak identifier 'undeclared_target' never declared") != NULL &&
                                strstr(theirs, "1:14: warning: weak identifier 'undeclared_target' never declared") != NULL && strstr(ours, "1:14:") != NULL,
                          "a weak alias pragma's target never declared warns as clang's");
        free(ours);
    }
    free(theirs);
    unlink(path);
    check_test_driver_weak_alias_diagnostics(test, clang, executable, directory);
    check_test_driver_weak_alias_common(test, clang, executable, directory);
}
