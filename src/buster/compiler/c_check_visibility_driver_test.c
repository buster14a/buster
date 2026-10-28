// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original pragma fixtures and an independent ELF st_other reader, checked against Clang's objects.

static bool check_test_visibility_symbol(const char* path, const char* name, uint16_t* flags)
{
    uint64_t length = 0;
    uint8_t* bytes = check_test_driver_read_aligned(path, &length);
    Elf64_Ehdr header = { 0 };
    bool valid = bytes != NULL && length >= sizeof(header);
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_shentsize == sizeof(Elf64_Shdr) &&
                header.e_shoff <= length && header.e_shnum <= (length - header.e_shoff) / sizeof(Elf64_Shdr);
    }
    bool found = false;
    for (uint32_t s = 0; valid && !found && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr table;
        memcpy(&table, bytes + header.e_shoff + (uint64_t)s * sizeof(table), sizeof(table));
        if (table.sh_type == SHT_SYMTAB)
        {
            valid = table.sh_link < header.e_shnum && table.sh_entsize == sizeof(Elf64_Sym) &&
                    table.sh_offset <= length && table.sh_size <= length - table.sh_offset && table.sh_size % sizeof(Elf64_Sym) == 0;
            Elf64_Shdr strings = { 0 };
            if (valid)
            {
                memcpy(&strings, bytes + header.e_shoff + (uint64_t)table.sh_link * sizeof(strings), sizeof(strings));
                valid = strings.sh_offset <= length && strings.sh_size <= length - strings.sh_offset;
            }
            for (uint64_t i = 0; valid && !found && i < table.sh_size / sizeof(Elf64_Sym); i += 1)
            {
                Elf64_Sym symbol;
                memcpy(&symbol, bytes + table.sh_offset + i * sizeof(symbol), sizeof(symbol));
                valid = symbol.st_name < strings.sh_size;
                if (valid)
                {
                    const char* spelling = (const char*)bytes + strings.sh_offset + symbol.st_name;
                    const char* end = memchr(spelling, 0, (size_t)(strings.sh_size - symbol.st_name));
                    valid = end != NULL;
                    found = valid && (size_t)(end - spelling) == strlen(name) && memcmp(spelling, name, strlen(name)) == 0;
                    if (found)
                    {
                        *flags = (uint16_t)((uint16_t)ELF64_ST_BIND(symbol.st_info) << 8 | (symbol.st_other & 3));
                    }
                }
            }
        }
    }
    free(bytes);
    return valid && found;
}

static void check_test_driver_visibility(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source[] = "#pragma clang diagnostic ignored \"-Wimplicit-function-declaration\"\n"
        "int outside=1;\n#pragma GCC visibility push(hidden)\n"
        "int hidden=2;extern int external_hidden;_Thread_local int tls_hidden;extern _Thread_local int external_tls;static int local=3;"
        "int prior(void);int explicit_default __attribute__((visibility(\"default\")))=4;\n"
        "int implicit_caller(void){return missing_function(1)+external_tls;}\n"
        "#pragma GCC visibility push(protected)\nint protected_value=5;\n#pragma GCC visibility pop\n"
        "int hidden_again=6;\n#pragma GCC visibility push(bad)\nint ignored_bad=9;\n#pragma GCC visibility pop\nint restored=7;"
        "int prior(void){return outside+hidden+external_hidden+local+protected_value+hidden_again+restored+tls_hidden;}\n"
        "#pragma GCC visibility push(internal)\nint internal_value=8;\n#pragma GCC visibility pop\n"
        "int crossing(void){\n#pragma GCC visibility push(protected)\nreturn 0;}\n"
        "int after_body=10;\n#pragma GCC visibility pop\n";
    static const struct
    {
        const char* name;
        uint8_t visibility;
        uint8_t binding;
    } symbols[] = {
        { "outside", STV_DEFAULT, STB_GLOBAL },
        { "hidden", STV_HIDDEN, STB_GLOBAL },
        { "external_hidden", STV_HIDDEN, STB_GLOBAL },
        { "tls_hidden", STV_HIDDEN, STB_GLOBAL },
        { "external_tls", STV_HIDDEN, STB_GLOBAL },
        { "implicit_caller", STV_HIDDEN, STB_GLOBAL },
        { "missing_function", STV_HIDDEN, STB_GLOBAL },
        { "local", STV_DEFAULT, STB_LOCAL },
        { "prior", STV_HIDDEN, STB_GLOBAL },
        { "explicit_default", STV_DEFAULT, STB_GLOBAL },
        { "protected_value", STV_PROTECTED, STB_GLOBAL },
        { "hidden_again", STV_HIDDEN, STB_GLOBAL },
        { "ignored_bad", STV_HIDDEN, STB_GLOBAL },
        { "restored", STV_DEFAULT, STB_GLOBAL },
        { "internal_value", STV_HIDDEN, STB_GLOBAL },
        { "crossing", STV_DEFAULT, STB_GLOBAL },
        { "after_body", STV_PROTECTED, STB_GLOBAL },
    };
    char path[512];
    char objects[3][512] = { { 0 } };
    snprintf(path, sizeof(path), "%s/visibility.c", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1);
    check_test_expect(test, written, "visibility source fixture is written");
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        snprintf(objects[p], sizeof(objects[p]), "%s/visibility-%u.o", directory, p);
        char* diagnostics = NULL;
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", objects[p], p == 2 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &diagnostics) == 0;
        check_test_expect(test, compiled, "visibility pragmas compile through Clang and both checker paths");
        free(diagnostics);
        for (uint32_t s = 0; compiled && s < BUSTER_ARRAY_LENGTH(symbols); s += 1)
        {
            uint16_t actual = UINT16_MAX;
            const uint16_t expected = (uint16_t)((uint16_t)symbols[s].binding << 8 | symbols[s].visibility);
            char description[160];
            snprintf(description, sizeof(description), "%s visibility/binding independently matches ELF and Clang", symbols[s].name);
            check_test_expect(test, check_test_visibility_symbol(objects[p], symbols[s].name, &actual) && actual == expected, description);
        }
    }
    if (written)
    {
        check_test_expect(test, check_test_driver_same_files(objects[1], objects[2]), "visibility scalar/vector objects match byte for byte");
    }
    for (uint32_t p = 0; p < 3; p += 1)
    {
        unlink(objects[p]);
    }
    static const char* const malformed[] = {
        "#pragma GCC visibility pop\nint x;\n",
        "#pragma GCC visibility push(bad)\nint x;\n",
        "#pragma GCC visibility push\nint x;\n",
        "#pragma GCC visibility push()\nint x;\n",
        "#pragma GCC visibility push(hidden) extra\nint x;\n",
        "#pragma GCC visibility what\nint x;\n",
        "#pragma GCC visibility push(hidden\nint x;\n",
        "#pragma GCC visibility pop()\nint x;\n",
        "#pragma GCC visibility push(bad)\n#pragma clang diagnostic ignored \"-Wignored-attributes\"\nint x;\n",
        "#pragma clang diagnostic ignored \"-Wignored-attributes\"\n#pragma GCC visibility push(bad)\nint x;\n",
        "#pragma GCC visibility push\n#pragma clang diagnostic ignored \"-Wignored-pragmas\"\nint x;\n",
        "#pragma clang diagnostic error \"-Wignored-pragmas\"\n#pragma GCC visibility push\nint x;\n",
        "#pragma GCC visibility push(hidden)\n#pragma GCC visibility pop\n",
        "int\n#pragma GCC visibility push(hidden)\nx;\n",
        "int f(void){return\n#pragma GCC visibility push(hidden)\n0;}\n",
        "struct S {\n#pragma GCC visibility push(hidden)\nint x;};\n",
        "int x=\n#pragma GCC visibility push(hidden)\n1;\n",
        "int x=\n#pragma GCC visibility push(bad)\n1;\n",
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(malformed); c += 1)
    {
        const bool ready = parse_test_write(path, malformed[c], strlen(malformed[c]));
        for (uint32_t p = 0; ready && p < 2; p += 1)
        {
            char* expected = NULL;
            char* arguments[] = { (char*)clang, "-fsyntax-only", "-fno-caret-diagnostics", path, p != 0 ? "-w" : NULL, NULL, NULL };
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* actual = NULL;
                arguments[0] = (char*)executable;
                arguments[p != 0 ? 5 : 4] = scalar != 0 ? "--scalar" : NULL;
                const int status = check_test_driver_spawn(arguments, &actual);
                char description[160];
                snprintf(description, sizeof(description), "malformed visibility case %u %s %s has Clang's exact diagnostic", c,
                         scalar != 0 ? "scalar" : "vector", p != 0 ? "quiet" : "ordinary");
                check_test_expect(test, oracle >= 0 && status == oracle && actual != NULL && expected != NULL && strcmp(actual, expected) == 0, description);
                free(actual);
            }
            free(expected);
        }
    }
    unlink(path);
}
