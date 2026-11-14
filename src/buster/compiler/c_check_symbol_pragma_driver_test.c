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
static void check_test_driver_weak_alias_diagnostics(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    CheckTestFixtures fixtures;
    const bool loaded = check_test_fixtures_load(&fixtures, "checker/weak-alias");
    check_test_expect(test, loaded, loaded ? "weak alias diagnostic fixtures loaded" : fixtures.problem);
    char source[PATH_MAX];
    char object[PATH_MAX];
    char expected_lines[8192];
    char actual_lines[8192];
    snprintf(source, sizeof(source), "%s/weak-alias-diagnostic.c", directory);
    snprintf(object, sizeof(object), "%s/weak-alias-diagnostic.o", directory);
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    for (uint32_t f = 0; loaded && f < fixtures.count; f += 1)
    {
        const CheckSourceFixture* fixture = fixtures.cases + f;
        const bool written = parse_test_write(source, fixture->source, strlen(fixture->source));
        check_test_expect(test, written, "weak alias diagnostic source written");
        for (uint32_t standard = 0; written && standard < BUSTER_ARRAY_LENGTH(standards); standard += 1)
        {
            char* expected = NULL;
            char* oracle_arguments[] = { (char*)clang, (char*)standards[standard], "-fsyntax-only", "-fno-caret-diagnostics", source, NULL };
            const int oracle = check_test_driver_spawn(oracle_arguments, &expected);
            const bool lines = (oracle == 0 || oracle == 1) && expected != NULL && check_test_diagnostic_lines(expected, expected_lines, sizeof(expected_lines));
            check_test_expect(test, lines && oracle == (fixture->rejected ? 1 : 0), "weak alias oracle agrees with independent acceptance expectation");
            for (uint32_t mode = 0; lines && mode < 4; mode += 1)
            {
                const bool deferred = mode >= 2;
                char* actual = NULL;
                char* arguments[16] = { (char*)executable, (char*)standards[standard], deferred ? "-c" : "-fsyntax-only",
                                       "-fno-caret-diagnostics", source };
                uint32_t count = 5;
                if (mode == 1)
                {
                    arguments[count++] = "--scalar";
                }
                if (deferred)
                {
                    arguments[count++] = "-o";
                    arguments[count++] = object;
                    arguments[count++] = "--deferred-bodies";
                    arguments[count++] = "--lanes";
                    arguments[count++] = mode == 2 ? "1" : "3";
                }
                arguments[count] = NULL;
                unlink(object);
                const int status = check_test_driver_spawn(arguments, &actual);
                const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, sizeof(actual_lines)) &&
                                  strcmp(expected_lines, actual_lines) == 0;
                const bool correct = same && status == oracle && (!fixture->rejected || access(object, F_OK) != 0);
                char description[512];
                snprintf(description, sizeof(description), "%s, %s mode %u: status %d (clang %d), complete diagnostic output and publication", fixture->name,
                         standards[standard], mode, status, oracle);
                check_test_expect(test, correct, description);
                if (!correct)
                {
                    fprintf(stderr, "%s\nours:\n%s\nclang:\n%s\n", description, actual != NULL ? actual : "(no output)", expected);
                }
                free(actual);
                unlink(object);
            }
            free(expected);
        }
    }
    unlink(source);
    arena_release(&fixtures.storage);
}

// `#pragma weak A = T` (#1849): the weak aliases, each symbol's binding and visibility clang's by both paths, whose
// objects must be the same bytes, linked and run as clang's object: the shapes whose A the unit declares, and clang's
// clones of T's declaration (an A never declared, of a function or a variable, a static, a T defined later; an A declared
// before the pragma, never named, of another kind, type, thread-local storage or with a section); an A that is an alias
// already, which keeps its own; the shapes still refused by name, which clang's Sema errors at; and the warning for a T
// never declared, line for line clang's.
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
    static const char* const refused[] = {
        // A declared before the pragma and named after it: clang's "reference to 'a' is ambiguous".
        "int t(void) { return 1; }\nint a(void);\n#pragma weak a = t\nint u(void) { return a(); }\n",
        // T not defined: "alias must point to a defined variable or function".
        "int t(void);\nint a(void);\n#pragma weak a = t\n",
        // A defined after the pragma: "redefinition of 'a'".
        "int t = 1;\n#pragma weak a = t\nint a = 2;\n",
        // A declared after the pragma with another type: "redeclaration of 'a' with a different type".
        "long t = 1;\n#pragma weak a = t\nextern int a;\n",
    };
    char path[512];
    char objects[3][512];
    char programs[3][512];
    char captured[3][256] = { { 0 } };
    snprintf(path, sizeof(path), "%s/weak-aliases.c", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1);
    check_test_expect(test, written, "the weak alias pragmas' fixture is written");
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        snprintf(objects[p], sizeof(objects[p]), "%s/weak-aliases-%u.o", directory, p);
        snprintf(programs[p], sizeof(programs[p]), "%s/weak-aliases-%u", directory, p);
        char* text = NULL;
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", objects[p], p == 2 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        check_test_expect(test, compiled, "weak alias pragmas compile through clang and both checker paths");
        for (uint32_t n = 0; compiled && p != 0 && n < BUSTER_ARRAY_LENGTH(names); n += 1)
        {
            uint16_t ours = UINT16_MAX;
            uint16_t theirs = UINT16_MAX - 1;
            char description[160];
            snprintf(description, sizeof(description), "a weak alias pragma leaves %s's binding clang's, by the %s path", names[n], p == 1 ? "vector" : "scalar");
            check_test_expect(test, check_test_visibility_symbol(objects[p], names[n], &ours) && check_test_visibility_symbol(objects[0], names[n], &theirs) &&
                                   ours == theirs, description);
        }
        char* const link[] = { (char*)clang, objects[p], "-o", programs[p], NULL };
        const bool linked = compiled && check_test_driver_spawn(link, &text) == 0;
        free(text);
        check_test_expect(test, linked && check_test_driver_capture(programs[p], captured[p], sizeof(captured[p])), "weak alias pragmas' objects link and run");
    }
    check_test_expect(test, written && check_test_driver_same_files(objects[1], objects[2]), "weak alias pragmas' scalar and vector objects match byte for byte");
    check_test_expect(test, captured[0][0] != 0 && strcmp(captured[1], captured[0]) == 0 && strcmp(captured[2], captured[0]) == 0,
                      "weak alias pragmas' programs run as clang's");
    for (uint32_t p = 0; p < 3; p += 1)
    {
        unlink(objects[p]);
        unlink(programs[p]);
    }
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(refused); r += 1)
    {
        const bool shape = parse_test_write(path, refused[r], strlen(refused[r]));
        char* text = NULL;
        char* arguments[] = { (char*)executable, "-std=gnu17", "-O0", "-c", path, "-o", objects[1], NULL };
        const int status = shape ? check_test_driver_spawn(arguments, &text) : 0;
        check_test_expect(test, status != 0 && text != NULL && strstr(text, "'#pragma weak'") != NULL, "a weak alias pragma of a shape not modeled is refused by name");
        free(text);
        unlink(objects[1]);
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
}
