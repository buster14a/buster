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
    unlink(path);
    unlink(partner);
    unlink(object);
    unlink(program);
}
