// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source/option fixtures for implicit function declarations; Clang is an external diagnostic/ABI oracle.

static uint32_t check_test_implicit_count(const char* text, const char* part)
{
    uint32_t count = 0;
    for (const char* at = text; at != NULL && (at = strstr(at, part)) != NULL; at += strlen(part))
    {
        count += 1;
    }
    return count;
}

#include "c_check_implicit_library_test.c"

static void check_test_driver_implicit_system(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* standard;
        const char* first;
        const char* second;
        uint8_t pragma;
        bool user;
    } cases[] = {
        { "-std=gnu17", NULL, NULL, false, false },
        { "-std=gnu17", "-Wsystem-headers", NULL, false, false },
        { "-std=gnu89", "-Wimplicit-function-declaration", NULL, false, false },
        { "-std=gnu89", "-Wimplicit-function-declaration", "-Wsystem-headers", false, false },
        { "-std=gnu17", "-Wsystem-headers", "-Wno-implicit-function-declaration", false, false },
        { "-std=gnu17", "-Wsystem-headers", "-Wno-error=implicit", false, false },
        { "-std=gnu17", "-Wsystem-headers", "-Wno-system-headers", false, false },
        { "-std=gnu17", "-Wno-system-headers", "-Wsystem-headers", false, false },
        { "-std=gnu23", "-Wno-implicit-function-declaration", NULL, false, false },
        { "-std=gnu17", NULL, NULL, true, false },
        { "-std=gnu17", "-Wsystem-headers", NULL, true, false },
        { "-std=gnu17", NULL, NULL, false, true },
        { "-std=gnu89", "-Werror=implicit-function-declaration", "-w", false, true },
        { "-std=gnu89", "-w", NULL, true, true },
        { "-std=gnu17", "-w", NULL, true, true },
        { "-std=gnu17", "-Wsystem-headers", NULL, 2, false },
        { "-std=gnu17", "-Wsystem-headers", NULL, 3, false },
        { "-std=gnu89", "-Wsystem-headers", NULL, 4, false },
    };
    char header[512];
    char source[512];
    char objects[2][512];
    snprintf(header, sizeof(header), "%s/implicit-system.h", directory);
    snprintf(source, sizeof(source), "%s/implicit-system.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/implicit-system-scalar.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/implicit-system-vector.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        static const char* const pragmas[] = {
            "",
            "#pragma clang diagnostic error \"-Wimplicit-function-declaration\"\n",
            "#pragma clang diagnostic ignored \"-Wall\"\n",
            "#pragma clang diagnostic warning \"-Wall\"\n",
            "#pragma clang diagnostic error \"-Wall\"\n",
        };
        static const char system_only[] = "#include <implicit-system.h>\n";
        static const char user[] = "#include <implicit-system.h>\nint user_function(void){return user_missing(1);}\n";
        char body[256];
        const int body_length = snprintf(body, sizeof(body), "%sint system_function(void){return system_missing(1);}\n", pragmas[cases[c].pragma]);
        const char* unit = cases[c].user ? user : system_only;
        const bool written = body_length > 0 && (uint32_t)body_length < sizeof(body) &&
                             parse_test_write(header, body, (uint64_t)body_length) && parse_test_write(source, unit, strlen(unit));
        check_test_expect(test, written, "implicit system-header fixture is written");
        for (uint32_t mode = 0; written && mode < 2; mode += 1)
        {
            char* expected = NULL;
            char* arguments[18] = { (char*)clang, (char*)cases[c].standard, "-fno-caret-diagnostics", mode == 0 ? "-fsyntax-only" : "-c",
                                    source, "-o", objects[0], "-isystem", (char*)directory };
            uint32_t count = 9;
            if (cases[c].first != NULL)
            {
                arguments[count++] = (char*)cases[c].first;
            }
            if (cases[c].second != NULL)
            {
                arguments[count++] = (char*)cases[c].second;
            }
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* actual = NULL;
                arguments[0] = (char*)executable;
                arguments[6] = objects[scalar];
                arguments[count] = scalar == 0 ? "--scalar" : NULL;
                arguments[count + 1] = NULL;
                unlink(objects[scalar]);
                const int status = check_test_driver_spawn(arguments, &actual);
                const bool same = expected != NULL && actual != NULL &&
                    check_test_implicit_count(expected, ": error:") == check_test_implicit_count(actual, ": error:") &&
                    check_test_implicit_count(expected, ": warning:") == check_test_implicit_count(actual, ": warning:") &&
                    check_test_implicit_count(expected, "system_missing") == check_test_implicit_count(actual, "system_missing") &&
                    check_test_implicit_count(expected, "user_missing") == check_test_implicit_count(actual, "user_missing");
                char description[160];
                snprintf(description, sizeof(description), "implicit system-header case %u %s %s agrees with Clang", c,
                         scalar == 0 ? "scalar" : "vector", mode == 0 ? "syntax" : "object");
                check_test_expect(test, oracle >= 0 && status == oracle && same, description);
                check_test_expect(test, mode == 0 || (access(objects[scalar], F_OK) == 0) == (status == 0),
                                  "implicit system-header policy controls object publication");
                free(actual);
            }
            if (mode != 0 && oracle == 0)
            {
                check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]), "implicit system-header objects agree byte for byte");
            }
            free(expected);
            unlink(objects[0]);
            unlink(objects[1]);
        }
    }
    unlink(header);
    unlink(source);
}

static void check_test_driver_implicit(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* standard;
        const char* first;
        const char* second;
    } options[] = {
        { "-std=gnu17", NULL, NULL },
        { "-std=c99", NULL, NULL },
        { "-std=gnu17", "-Wno-error=implicit-function-declaration", NULL },
        { "-std=gnu17", "-Wno-implicit-function-declaration", NULL },
        { "-std=gnu17", "-Wno-implicit-function-declaration", "-Wno-error=implicit-function-declaration" },
        { "-std=gnu17", "-Wno-error=implicit-function-declaration", "-Werror" },
        { "-std=gnu17", "-Werror", "-Wno-error=implicit-function-declaration" },
        { "-std=gnu17", "-w", NULL },
        { "-std=gnu17", "-w", "-Wno-error=implicit-function-declaration" },
        { "-std=gnu17", "-Werror=implicit-function-declaration", NULL },
        { "-std=gnu89", NULL, NULL },
        { "-std=gnu89", "-Wimplicit-function-declaration", NULL },
        { "-std=gnu23", "-Wno-implicit-function-declaration", NULL },
        { "-std=gnu89", "-Wimplicit", NULL },
        { "-std=gnu89", "-Werror=implicit", NULL },
        { "-std=gnu17", "-Wno-implicit", NULL },
        { "-std=gnu17", "-Wno-error=implicit", NULL },
        { "-std=gnu17", "-Wno-implicit-function-declaration", "-Wimplicit-function-declaration" },
        { "-std=gnu17", "-Wno-implicit", "-Wimplicit-function-declaration" },
        { "-std=gnu17", "-Wimplicit-function-declaration", "-Wno-implicit" },
        { "-std=gnu17", "-Wimplicit", "-Wno-implicit-function-declaration" },
        { "-std=gnu17", "-Wno-implicit-function-declaration", "-Wimplicit" },
        { "-std=gnu17", "-Werror=implicit", "-Wno-error=implicit-function-declaration" },
        { "-std=gnu17", "-Wno-error=implicit-function-declaration", "-Werror=implicit" },
        { "-std=gnu17", "-Werror=implicit-function-declaration", "-Wno-error=implicit" },
        { "-std=gnu17", "-Wno-error=implicit", "-Werror=implicit-function-declaration" },
        { "-std=gnu17", "-Wno-all", NULL },
        { "-std=gnu17", "-Wall", "-Wno-all" },
        { "-std=gnu17", "-Wno-all", "-Wall" },
        { "-std=gnu17", "-Wno-everything", NULL },
        { "-std=gnu89", "-Weverything", NULL },
        { "-std=gnu17", "-Wno-implicit", "-Weverything" },
        { "-std=gnu89", "-Werror-implicit-function-declaration", NULL },
        { "-std=gnu17", "-Wno-error-implicit-function-declaration", NULL },
        { "-std=gnu17", "-Wno-error=all", NULL },
        { "-std=gnu89", "-Werror=all", NULL },
        { "-std=gnu89", "-Werror=implicit-function-declaration", "-w" },
        { "-std=gnu23", "-Wno-implicit", NULL },
        { "-std=gnu23", "-Wno-all", NULL },
    };
    static const char source[] = "int first(void);int second(void);\nint first(void){int x=missing(1);return x+missing(2);}\n"
        "int second(void){return missing(3);}\n";
    char path[512];
    char objects[2][512];
    snprintf(path, sizeof(path), "%s/implicit.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/implicit-scalar.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/implicit-vector.o", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1);
    check_test_expect(test, written, "implicit function source fixture is written");
    for (uint32_t c = 0; written && c < BUSTER_ARRAY_LENGTH(options); c += 1)
    {
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            char* expected = NULL;
            char* arguments[16] = { (char*)clang, (char*)options[c].standard, "-fno-caret-diagnostics", mode == 0 ? "-fsyntax-only" : "-c", path, "-o", objects[0] };
            uint32_t count = 7;
            if (options[c].first != NULL)
            {
                arguments[count++] = (char*)options[c].first;
            }
            if (options[c].second != NULL)
            {
                arguments[count++] = (char*)options[c].second;
            }
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* actual = NULL;
                arguments[0] = (char*)executable;
                arguments[6] = objects[scalar];
                arguments[count] = scalar == 0 ? "--scalar" : NULL;
                arguments[count + 1] = NULL;
                unlink(objects[scalar]);
                const int status = check_test_driver_spawn(arguments, &actual);
                const char* message = options[c].standard[8] == '2' ? "use of undeclared identifier 'missing'" :
                                      options[c].standard[8] == '8' ? "implicit declaration of function 'missing'" : "call to undeclared function 'missing'";
                const bool same = expected != NULL && actual != NULL &&
                    check_test_implicit_count(expected, ": error:") == check_test_implicit_count(actual, ": error:") &&
                    check_test_implicit_count(expected, ": warning:") == check_test_implicit_count(actual, ": warning:") &&
                    check_test_implicit_count(expected, message) == check_test_implicit_count(actual, message);
                char description[160];
                snprintf(description, sizeof(description), "implicit function option case %u %s %s agrees with Clang", c,
                         scalar == 0 ? "scalar" : "vector", mode == 0 ? "syntax" : "object");
                check_test_expect(test, oracle >= 0 && oracle == status && same, description);
                check_test_expect(test, mode == 0 || (access(objects[scalar], F_OK) == 0) == (status == 0),
                                  "implicit function diagnostic controls object publication");
                free(actual);
            }
            if (mode != 0 && oracle == 0)
            {
                check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]), "implicit function objects agree byte for byte");
            }
            free(expected);
            unlink(objects[0]);
            unlink(objects[1]);
        }
    }
    unlink(path);
    static const char* const ordinary[] = {
        "int f(void){return (missing)(1);}\n",
        "int f(void){return __builtin_missing(1);}\n",
        "int f(void){return missing;}\n",
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(ordinary); c += 1)
    {
        const bool ready = parse_test_write(path, ordinary[c], strlen(ordinary[c]));
        for (uint32_t scalar = 0; ready && scalar < 2; scalar += 1)
        {
            char* actual = NULL;
            char* arguments[] = { (char*)executable, "-Wno-implicit-function-declaration", "-fsyntax-only", path,
                                  scalar != 0 ? "--scalar" : NULL, NULL };
            const int status = check_test_driver_spawn(arguments, &actual);
            check_test_expect(test, status == 1 && actual != NULL && strstr(actual, "implicit function") == NULL &&
                                    strstr(actual, "implicit-function-declaration") == NULL,
                              "non-direct callees, builtin spellings and ordinary identifiers remain hard errors");
            free(actual);
        }
    }
    unlink(path);
    static const char native[] = "int before(int n){return convert((float)n,(unsigned char)9);}\n"
        "int convert(double d,int v){return (int)(d*7)+v;}\nint main(void){return before(4)==37?0:1;}\n";
    const bool native_written = parse_test_write(path, native, sizeof(native) - 1);
    for (uint32_t path_index = 0; native_written && path_index < 3; path_index += 1)
    {
        char* diagnostics = NULL;
        char* arguments[] = { (char*)(path_index == 0 ? clang : executable), "-std=gnu17", "-Wno-implicit-function-declaration", path,
                              "-o", objects[0], path_index == 2 ? "--scalar" : NULL, NULL };
        const int status = check_test_driver_spawn(arguments, &diagnostics);
        check_test_expect(test, status == 0, "implicit function caller/callee with default float and integer promotions links");
        free(diagnostics);
        if (status == 0)
        {
            char* run[] = { objects[0], NULL };
            diagnostics = NULL;
            check_test_expect(test, check_test_driver_spawn(run, &diagnostics) == 0, "implicit promoted arguments execute as Clang's build");
            free(diagnostics);
        }
        unlink(objects[0]);
    }
    unlink(path);
    check_test_driver_implicit_system(test, clang, executable, directory);
    check_test_driver_implicit_library(test, clang, executable, directory);
}
