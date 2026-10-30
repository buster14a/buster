// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's alias, ifunc and weakref attributes in -c's objects and the direct path's executables (#147), on an original
// fixture, against clang's: each symbol's type, binding, visibility and whether it is defined, read from both objects;
// each alias and ifunc at its definition's section, value and size in both; both checker paths' objects the same bytes,
// linked by cc with a unit that defines a name only a weakref and an ordinary reference refer to, alone and with
// another object's strong definitions of a weak alias's name and of names only weakrefs refer to, and the direct path's
// executables of both paths (the same bytes), which must print what clang's build prints. Then the errors clang's code
// generation reports at -c (and Sema's at -fsyntax-only), each line of ours (place, message and notes) clang's. And GNU's
// retain attribute: each retained definition in clang's section (check_test_driver_same_sections: its name, type,
// SHF_GNU_RETAIN among its flags, and alignment), the object ELFOSABI_GNU as clang's is, what retain alone doesn't emit
// left out as clang leaves it, and the programs run alike. Include after c_check_symbol_driver_test.c and the driver
// test's ELF readers (check_test_driver_same_sections).

// A symbol of an object as its Elf64_Sym has it.
typedef struct CheckTestAliasSymbol CheckTestAliasSymbol;
struct CheckTestAliasSymbol
{
    uint8_t info;
    uint8_t other;
    uint16_t section;
    uint64_t value;
    uint64_t size;
};

// The symbol `name` of the object at `path`: whether it has one.
static bool check_test_alias_symbol(const char* path, const char* name, CheckTestAliasSymbol* result)
{
    uint64_t length = 0;
    uint8_t* bytes = check_test_driver_read_aligned(path, &length);
    Elf64_Ehdr header = { 0 };
    bool valid = bytes != NULL && length >= sizeof(header);
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_shentsize == sizeof(Elf64_Shdr) && header.e_shoff <= length &&
                header.e_shnum <= (length - header.e_shoff) / sizeof(Elf64_Shdr);
    }
    bool found = false;
    for (uint32_t s = 0; valid && !found && s < header.e_shnum; s += 1)
    {
        Elf64_Shdr table;
        memcpy(&table, bytes + header.e_shoff + (uint64_t)s * sizeof(table), sizeof(table));
        if (table.sh_type == SHT_SYMTAB)
        {
            valid = table.sh_link < header.e_shnum && table.sh_entsize == sizeof(Elf64_Sym) && table.sh_offset <= length &&
                    table.sh_size <= length - table.sh_offset && table.sh_size % sizeof(Elf64_Sym) == 0;
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
                const char* spelling = valid ? (const char*)bytes + strings.sh_offset + symbol.st_name : NULL;
                const char* end = valid ? memchr(spelling, 0, (size_t)(strings.sh_size - symbol.st_name)) : NULL;
                valid = valid && end != NULL;
                found = valid && (size_t)(end - spelling) == strlen(name) && memcmp(spelling, name, strlen(name)) == 0;
                if (found)
                {
                    *result = (CheckTestAliasSymbol){
                        .info = symbol.st_info,
                        .other = symbol.st_other,
                        .section = symbol.st_shndx,
                        .value = symbol.st_value,
                        .size = symbol.st_size,
                    };
                }
            }
        }
    }
    free(bytes);
    return valid && found;
}

// The EI_OSABI of the object at `path`, or 0xff where it can't be read.
static uint8_t check_test_alias_abi(const char* path)
{
    uint64_t length = 0;
    uint8_t* bytes = check_test_driver_read_aligned(path, &length);
    const uint8_t result = bytes != NULL && length >= EI_NIDENT && memcmp(bytes, ELFMAG, SELFMAG) == 0 ? bytes[EI_OSABI] : 0xff;
    free(bytes);
    return result;
}

// The lines of `text` that are errors or notes (": error: ", ": note: "), each followed by a newline, into `lines`.
static void check_test_alias_diagnostics(const char* text, char* lines, uint64_t capacity)
{
    uint64_t used = 0;
    const char* line = text;
    while (line != NULL && *line != 0)
    {
        const char* end = strchr(line, '\n');
        const uint64_t length = end != NULL ? (uint64_t)(end - line) : strlen(line);
        const bool kept = (memmem(line, length, ": error: ", 9) != NULL || memmem(line, length, ": note: ", 8) != NULL) && used + length + 2 < capacity;
        if (kept)
        {
            memcpy(lines + used, line, length);
            lines[used + length] = '\n';
            used += length + 1;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    if (capacity != 0)
    {
        lines[used] = 0;
    }
}

// `source`'s errors and notes by clang and by both checker paths, with -c and with -fsyntax-only, at `path`: ours must
// be clang's, line for line, each with `expected` among them (both modes' where `syntax` says Sema reports it, and at
// -fsyntax-only none otherwise, as clang's code generation reports it at -c alone).
static void check_test_alias_errors(CheckTest* test, const char* clang, const char* executable, const char* directory, const char* name, const char* source,
                                    const char* expected, bool syntax)
{
    char path[512];
    char object[512];
    snprintf(path, sizeof(path), "%s/alias-errors.c", directory);
    snprintf(object, sizeof(object), "%s/alias-errors.o", directory);
    const bool written = parse_test_write(path, source, strlen(source));
    for (uint32_t mode = 0; written && mode < 2; mode += 1)
    {
        char theirs[4096];
        char* text = NULL;
        char* const clang_arguments[] = { (char*)clang, "-std=gnu17", "-O0", mode == 0 ? "-c" : "-fsyntax-only", path, "-o", object, NULL };
        const int clang_status = check_test_driver_spawn(clang_arguments, &text);
        check_test_alias_diagnostics(text != NULL ? text : "", theirs, sizeof(theirs));
        free(text);
        for (uint32_t p = 0; p < 2; p += 1)
        {
            char ours[4096];
            char* const arguments[] = { (char*)executable, "-std=gnu17", "-O0", mode == 0 ? "-c" : "-fsyntax-only", path, "-o", object, p == 1 ? "--scalar" : NULL, NULL };
            const int status = check_test_driver_spawn(arguments, &text);
            check_test_alias_diagnostics(text != NULL ? text : "", ours, sizeof(ours));
            free(text);
            const bool reported = mode == 0 || syntax;
            char description[8192];
            snprintf(description, sizeof(description), "%s: the %s path's %s diagnostics are clang's:\n%s against\n%s", name, p == 0 ? "vector" : "scalar",
                     mode == 0 ? "-c" : "-fsyntax-only", ours, theirs);
            check_test_expect(test, (status != 0) == reported && (clang_status != 0) == reported && strcmp(ours, theirs) == 0 &&
                                   (strstr(theirs, expected) != NULL) == reported, description);
        }
    }
    unlink(object);
    unlink(path);
}

// Aliases of overloadable functions, whose names are their symbols' as the emission gives them: the mangled names of
// global and static ones, and an asm label's, which replaces the mangling. Each alias at its definition's section, value
// and size in clang's object and in both paths', and the program run as clang's build.
static void check_test_driver_alias_overloads(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source[] =
        "#include <stdio.h>\n"
        "int f(int x) __attribute__((overloadable));\n"
        "int f(int x) __attribute__((overloadable)) { return x + 1; }\n"
        "long f(long x) __attribute__((overloadable)) { return x + 2; }\n"
        "int g(int) __attribute__((alias(\"_Z1fi\")));\n"
        "long h(long) __attribute__((alias(\"_Z1fl\")));\n"
        "static int s(int x) __attribute__((overloadable)) { return x * 3; }\n"
        "static int s(char x) __attribute__((overloadable)) { return x; }\n"
        "int t(int) __attribute__((alias(\"_ZL1si\")));\n"
        "int k(int x) __asm__(\"k_label\") __attribute__((overloadable));\n"
        "int k(int x) __attribute__((overloadable)) { return x - 4; }\n"
        "int m(int) __attribute__((alias(\"k_label\")));\n"
        "int main(void) { printf(\"%d %ld %d %d %d %d %d\\n\", g(1), h(2), t(3), m(10), s((char)5), f(7), k(20)); return 0; }\n";
    static const char* const placed[][2] = { { "g", "_Z1fi" }, { "h", "_Z1fl" }, { "t", "_ZL1si" }, { "m", "k_label" } };
    char path[512];
    char objects[3][512];
    char programs[3][512];
    char captured[3][512] = { { 0 } };
    snprintf(path, sizeof(path), "%s/alias-overloads.c", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1);
    check_test_expect(test, written, "the overloadable aliases' fixture is written");
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        snprintf(objects[p], sizeof(objects[p]), "%s/alias-overloads-%u.o", directory, p);
        snprintf(programs[p], sizeof(programs[p]), "%s/alias-overloads-%u", directory, p);
        char* text = NULL;
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", objects[p], p == 2 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        if (!compiled && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        check_test_expect(test, compiled, "aliases of overloadable functions compile through clang and both checker paths");
        for (uint32_t a = 0; compiled && a < BUSTER_ARRAY_LENGTH(placed); a += 1)
        {
            CheckTestAliasSymbol alias = { 0 };
            CheckTestAliasSymbol base = { 0 };
            const bool read = check_test_alias_symbol(objects[p], placed[a][0], &alias) && check_test_alias_symbol(objects[p], placed[a][1], &base);
            char description[256];
            snprintf(description, sizeof(description), "%s is at %s's section, value and size in %s's object", placed[a][0], placed[a][1],
                     p == 0 ? "clang" : p == 1 ? "the vector path" : "the scalar path");
            check_test_expect(test, read && alias.section != SHN_UNDEF && alias.section == base.section && alias.value == base.value && alias.size == base.size,
                              description);
        }
        char* const link[] = { (char*)clang, objects[p], "-o", programs[p], NULL };
        const bool linked = compiled && check_test_driver_spawn(link, &text) == 0;
        free(text);
        check_test_expect(test, linked && check_test_driver_capture(programs[p], captured[p], sizeof(captured[p])), "the overloadable aliases' objects link and run");
        unlink(objects[p]);
        unlink(programs[p]);
    }
    for (uint32_t p = 1; written && p < 3; p += 1)
    {
        char description[1536];
        snprintf(description, sizeof(description), "the %s path's overloadable aliases run as clang's: %s against %s", p == 1 ? "vector" : "scalar", captured[p],
                 captured[0]);
        check_test_expect(test, captured[0][0] != 0 && strcmp(captured[p], captured[0]) == 0, description);
    }
    unlink(path);
}

static void check_test_driver_aliases(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_alias_overloads(test, clang, executable, directory);
    static const char source[] =
        "#include <stdio.h>\n"
        "#if __has_attribute(alias) && __has_attribute(__ifunc__) && __has_attribute(weakref) && __has_c_attribute(gnu::alias)\n"
        "#define QUERY_ALIAS(name) __attribute__((alias(#name)))\n#else\n#define QUERY_ALIAS(name)\n#endif\n"
        "static int implementation(int n) { return n * 3; }\n"
        "int global_target(int n) { return n + 100; }\n"
        "int exported_alias(int) QUERY_ALIAS(implementation);\n"
        "extern __typeof(global_target) weak_alias __attribute__((__weak__, __alias__(\"global_target\")));\n"
        "static int local_alias(int) __attribute__((alias(\"implementation\")));\n"
        "int chained(int) __attribute__((alias(\"exported_alias\")));\n"
        "int hidden_alias(int) __attribute__((alias(\"global_target\"), visibility(\"hidden\")));\n"
        "int declared_first(int);\nint declared_first(int) __attribute__((alias(\"implementation\")));\n"
        "int counter = 7;\nextern int counter_alias __attribute__((alias(\"counter\")));\n"
        "static int hidden_data = 11;\nextern int data_alias __attribute__((weak, alias(\"hidden_data\")));\n"
        "static int local_data_alias __attribute__((alias(\"hidden_data\")));\n"
        "static const char text[] = \"aliased\";\nextern const char text_alias[8] __attribute__((alias(\"text\")));\n"
        "_Thread_local int tls_value = 13;\nextern _Thread_local int tls_alias __attribute__((alias(\"tls_value\")));\n"
        "static int add_one(int n) { return n + 1; }\nstatic int (*resolve_add(void))(int) { return add_one; }\n"
        "int dispatched(int) __attribute__((ifunc(\"resolve_add\")));\n"
        "int dispatched_alias(int) __attribute__((alias(\"dispatched\")));\n"
        "static int twice(int n) { return n * 2; }\nstatic void* resolve_twice(void) { return (void*)twice; }\n"
        "int quiet_dispatch(int) __attribute__((ifunc(\"resolve_twice\"), weak));\n"
        "static int weak_puts(const char*) __attribute__((weakref(\"puts\")));\n"
        "static int weak_missing(void) __attribute__((weakref(\"alias_fixture_missing\")));\n"
        "static int weak_missing_again(void) __attribute__((weakref, alias(\"alias_fixture_missing\")));\n"
        "static int weak_defined(int) __attribute__((weakref(\"global_target\")));\n"
        "static int weak_strong(void) __attribute__((weakref(\"alias_fixture_strong\")));\nint alias_fixture_strong(void);\n"
        "static int weak_only(void) __attribute__((weakref(\"alias_fixture_weak\")));\n"
        "static int weak_counter __attribute__((weakref(\"counter\")));\n"
        "int (*const pointers[])(int) = { exported_alias, dispatched, weak_alias, weak_defined, local_alias };\n"
        "int* const data_pointers[] = { &local_data_alias, &counter_alias };\n"
        "int main(void)\n"
        "{\n"
        "    int (*pointer)(int) = dispatched;\n"
        "    printf(\"%d %d %d %d %d %d %d %d %s %d\\n\", exported_alias(1), weak_alias(2), local_alias(3), chained(4), hidden_alias(5), declared_first(6),\n"
        "           counter_alias, data_alias, text_alias, tls_alias);\n"
        "    counter_alias += 1;\n"
        "    tls_alias += 2;\n"
        "    printf(\"%d %d %d %d %d %d %d\\n\", counter, tls_value, dispatched(6), pointer(7), dispatched_alias(8), quiet_dispatch(9), pointer == dispatched);\n"
        "    printf(\"%d %d %d %d %d %d\\n\", weak_missing ? weak_missing() : -1, weak_missing_again ? weak_missing_again() : -2, weak_defined(9),\n"
        "           weak_strong() + alias_fixture_strong(), weak_only ? weak_only() : -3, weak_counter);\n"
        "    printf(\"%d %d %d %d %d %d %d\\n\", pointers[0](10), pointers[1](11), pointers[2](12), pointers[3](13), pointers[4](14), *data_pointers[0],\n"
        "           *data_pointers[1]);\n"
        "    return weak_puts(\"done\") < 0;\n"
        "}\n";
    static const char helper[] = "int alias_fixture_strong(void) { return 20; }\n";
    static const char strong[] =
        "int weak_alias(int n) { return n + 1000; }\nint alias_fixture_missing(void) { return 30; }\nint alias_fixture_weak(void) { return 40; }\n"
        "int data_alias = 50;\n";
    static const char* const names[] = {
        "implementation", "global_target", "exported_alias", "weak_alias", "local_alias", "chained", "hidden_alias", "declared_first", "counter",
        "counter_alias", "hidden_data", "data_alias", "local_data_alias", "text", "text_alias", "tls_value", "tls_alias", "resolve_add", "dispatched",
        "dispatched_alias", "resolve_twice", "quiet_dispatch", "puts", "alias_fixture_missing", "alias_fixture_strong", "alias_fixture_weak", "pointers",
        "data_pointers", "main",
    };
    // Each alias or ifunc, and the definition its symbol is at.
    static const char* const placed[][2] = {
        { "exported_alias", "implementation" }, { "weak_alias", "global_target" }, { "local_alias", "implementation" }, { "chained", "implementation" },
        { "hidden_alias", "global_target" }, { "declared_first", "implementation" }, { "counter_alias", "counter" }, { "data_alias", "hidden_data" },
        { "local_data_alias", "hidden_data" }, { "text_alias", "text" }, { "tls_alias", "tls_value" }, { "dispatched", "resolve_add" }, { "dispatched_alias", "resolve_add" },
        { "quiet_dispatch", "resolve_twice" },
    };
    char path[512] = { 0 };
    char helper_path[512] = { 0 };
    char strong_path[512] = { 0 };
    char helper_object[512] = { 0 };
    char strong_object[512] = { 0 };
    char objects[3][512] = { { 0 } };
    char programs[3][512] = { { 0 } };
    char strong_programs[3][512] = { { 0 } };
    char direct[2][512] = { { 0 } };
    snprintf(path, sizeof(path), "%s/aliases.c", directory);
    snprintf(helper_path, sizeof(helper_path), "%s/aliases-helper.c", directory);
    snprintf(strong_path, sizeof(strong_path), "%s/aliases-strong.c", directory);
    snprintf(helper_object, sizeof(helper_object), "%s/aliases-helper.o", directory);
    snprintf(strong_object, sizeof(strong_object), "%s/aliases-strong.o", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1) && parse_test_write(helper_path, helper, sizeof(helper) - 1) &&
                         parse_test_write(strong_path, strong, sizeof(strong) - 1);
    check_test_expect(test, written, "alias attributes' fixture is written");
    char* text = NULL;
    char* const helper_arguments[] = { (char*)clang, "-std=gnu17", "-O0", "-c", helper_path, "-o", helper_object, NULL };
    char* const strong_arguments[] = { (char*)clang, "-std=gnu17", "-O0", "-c", strong_path, "-o", strong_object, NULL };
    const bool helped = written && check_test_driver_spawn(helper_arguments, &text) == 0;
    free(text);
    const bool strengthened = written && check_test_driver_spawn(strong_arguments, &text) == 0;
    free(text);
    check_test_expect(test, helped && strengthened, "the alias fixture's other units compile through clang");
    char captured[3][512] = { { 0 } };
    char strong_captured[3][512] = { { 0 } };
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        snprintf(objects[p], sizeof(objects[p]), "%s/aliases-%u.o", directory, p);
        snprintf(programs[p], sizeof(programs[p]), "%s/aliases-%u", directory, p);
        snprintf(strong_programs[p], sizeof(strong_programs[p]), "%s/aliases-strong-%u", directory, p);
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", objects[p], p == 2 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        check_test_expect(test, compiled, "aliases, ifuncs and weakrefs compile through clang and both checker paths");
        if (!compiled && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        for (uint32_t n = 0; compiled && p != 0 && n < BUSTER_ARRAY_LENGTH(names); n += 1)
        {
            CheckTestAliasSymbol ours = { 0 };
            CheckTestAliasSymbol theirs = { 0 };
            const bool read = check_test_alias_symbol(objects[p], names[n], &ours) && check_test_alias_symbol(objects[0], names[n], &theirs);
            char description[256];
            snprintf(description, sizeof(description), "%s's type, binding, visibility and definedness are clang's, by the %s path: %02x %u %s against %02x %u %s", names[n],
                     p == 1 ? "vector" : "scalar", ours.info, ours.other & 3, ours.section == SHN_UNDEF ? "undefined" : "defined", theirs.info, theirs.other & 3,
                     theirs.section == SHN_UNDEF ? "undefined" : "defined");
            check_test_expect(test, read && ours.info == theirs.info && (ours.other & 3) == (theirs.other & 3) &&
                                   (ours.section == SHN_UNDEF) == (theirs.section == SHN_UNDEF), description);
        }
        for (uint32_t a = 0; compiled && a < BUSTER_ARRAY_LENGTH(placed); a += 1)
        {
            CheckTestAliasSymbol alias = { 0 };
            CheckTestAliasSymbol base = { 0 };
            const bool read = check_test_alias_symbol(objects[p], placed[a][0], &alias) && check_test_alias_symbol(objects[p], placed[a][1], &base);
            char description[256];
            snprintf(description, sizeof(description), "%s is at %s's section, value and size in %s's object", placed[a][0], placed[a][1],
                     p == 0 ? "clang" : p == 1 ? "the vector path" : "the scalar path");
            check_test_expect(test, read && alias.section != SHN_UNDEF && alias.section == base.section && alias.value == base.value && alias.size == base.size,
                              description);
        }
        char* const link[] = { (char*)clang, objects[p], helper_object, "-o", programs[p], NULL };
        char* const link_strong[] = { (char*)clang, objects[p], helper_object, strong_object, "-o", strong_programs[p], NULL };
        const bool linked = compiled && helped && check_test_driver_spawn(link, &text) == 0;
        free(text);
        const bool linked_strong = compiled && helped && strengthened && check_test_driver_spawn(link_strong, &text) == 0;
        free(text);
        check_test_expect(test, linked && check_test_driver_capture(programs[p], captured[p], sizeof(captured[p])), "the alias fixture's objects link and run");
        check_test_expect(test, linked_strong && check_test_driver_capture(strong_programs[p], strong_captured[p], sizeof(strong_captured[p])),
                          "the alias fixture's objects link with the strong definitions and run");
    }
    check_test_expect(test, written && check_test_driver_same_files(objects[1], objects[2]), "alias attributes' scalar and vector objects match byte for byte");
    for (uint32_t p = 1; written && p < 3; p += 1)
    {
        char description[1536];
        snprintf(description, sizeof(description), "the %s path's alias fixture runs as clang's: %s against %s", p == 1 ? "vector" : "scalar", captured[p], captured[0]);
        check_test_expect(test, captured[0][0] != 0 && strcmp(captured[p], captured[0]) == 0, description);
        snprintf(description, sizeof(description), "the %s path's weak alias and weakrefs give way to strong definitions as clang's: %s against %s",
                 p == 1 ? "vector" : "scalar", strong_captured[p], strong_captured[0]);
        check_test_expect(test, strong_captured[0][0] != 0 && strcmp(strong_captured[p], strong_captured[0]) == 0 && strcmp(strong_captured[p], captured[p]) != 0,
                          description);
    }
    // The direct path's executables, of both paths, with the helper as a unit of its own: the same bytes, run as clang's
    // build of the fixture (its ifuncs through the dynamic PIE's stubs and IRELATIVE relocations).
    char direct_captured[2][512] = { { 0 } };
    for (uint32_t p = 0; written && p < 2; p += 1)
    {
        snprintf(direct[p], sizeof(direct[p]), "%s/aliases-direct-%u", directory, p);
        char* arguments[] = { (char*)executable, "-std=gnu17", "-O0", path, helper_path, "-o", direct[p], p == 1 ? "--scalar" : NULL, NULL };
        const bool built = check_test_driver_spawn(arguments, &text) == 0;
        if (!built && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        char description[1536];
        const bool ran = built && check_test_driver_capture(direct[p], direct_captured[p], sizeof(direct_captured[p]));
        snprintf(description, sizeof(description), "the direct path's alias executable runs as clang's build, by the %s path: %s against %s",
                 p == 0 ? "vector" : "scalar", direct_captured[p], captured[0]);
        check_test_expect(test, ran && strcmp(direct_captured[p], captured[0]) == 0, description);
    }
    check_test_expect(test, written && check_test_driver_same_files(direct[0], direct[1]), "alias attributes' scalar and vector executables match byte for byte");
    for (uint32_t p = 0; p < 3; p += 1)
    {
        unlink(objects[p]);
        unlink(programs[p]);
        unlink(strong_programs[p]);
    }
    for (uint32_t p = 0; p < 2; p += 1)
    {
        unlink(direct[p]);
    }
    unlink(path);
    unlink(helper_path);
    unlink(strong_path);
    unlink(helper_object);
    unlink(strong_object);
    // Code generation's errors, at -c alone: an undefined name (with clang's note, and its alternative where the name is
    // a referenced declaration's), a cycle, a function's alias of a variable and a variable's of a function (with the
    // aliasee's definition noted), an inline function that isn't emitted, and an ifunc's undefined resolver, a variable as
    // its resolver and a resolver that returns no pointer; in the order clang emits them. And an alias and an ifunc of
    // itself, whose error stops clang's code generation, before and after others: the first one's error alone.
    check_test_alias_errors(test, clang, executable, directory, "undefined alias",
                            "int g(void) __attribute__((alias(\"nosuch\")));\nextern int f(void);\nint h(void) __attribute__((alias(\"f\")));\n"
                            "int use(void) { return f(); }\n",
                            "alias must point to a defined variable or function", false);
    check_test_alias_errors(test, clang, executable, directory, "alias cycles",
                            "int a(void) __attribute__((alias(\"b\")));\nint b(void) __attribute__((alias(\"a\")));\n", "alias definition is part of a cycle",
                            false);
    check_test_alias_errors(test, clang, executable, directory, "an alias of itself",
                            "int c(void) __attribute__((alias(\"c\")));\nint a(void) __attribute__((alias(\"b\")));\nint b(void) __attribute__((alias(\"a\")));\n"
                            "int g(void) __attribute__((alias(\"nosuch\")));\nint d(void) __attribute__((ifunc(\"d\")));\n",
                            "alias definition is part of a cycle", false);
    check_test_alias_errors(test, clang, executable, directory, "alias of another kind",
                            "extern int v;\nint v = 1;\nint g(void) __attribute__((alias(\"v\")));\nint f(void) { return 1; }\n"
                            "extern int w __attribute__((alias(\"f\")));\n",
                            "cannot alias a variable with a function", false);
    // An overloadable function's identifier, which names nothing: its symbol's name is its mangled one, which clang's
    // second note gives (check_driver_overload_name).
    check_test_alias_errors(test, clang, executable, directory, "an overloadable function's identifier",
                            "int f(int x) __attribute__((overloadable));\nint f(int x) __attribute__((overloadable)) { return x; }\n"
                            "int g(int) __attribute__((alias(\"f\")));\n",
                            "alias must point to a defined variable or function", false);
    check_test_alias_errors(test, clang, executable, directory, "alias of an inline function",
                            "inline int f(void) { return 1; }\nint g(void) __attribute__((alias(\"f\")));\n", "alias must point to a defined variable or function",
                            false);
    check_test_alias_errors(test, clang, executable, directory, "ifunc errors",
                            "int a(void) __attribute__((ifunc(\"nosuch\")));\nint v = 1;\nint b(void) __attribute__((ifunc(\"v\")));\n"
                            "static long r(void) { return 0; }\nint c(void) __attribute__((ifunc(\"r\")));\n",
                            "ifunc resolver function must return a pointer", false);
    check_test_alias_errors(test, clang, executable, directory, "an ifunc of itself",
                            "int a(void) __attribute__((ifunc(\"nosuch\")));\nint d(void) __attribute__((ifunc(\"d\")));\n"
                            "int e(void) __attribute__((alias(\"e\")));\n",
                            "ifunc definition is part of a cycle", false);
    // Sema's, at -fsyntax-only too: a definition that is an alias or an ifunc too, a weakref with external linkage, in a
    // block, without a target and on a typedef.
    check_test_alias_errors(test, clang, executable, directory, "definitions that are aliases", "int v = 1;\nint w __attribute__((alias(\"v\")));\n",
                            "definition 'w' cannot also be an alias", true);
    check_test_alias_errors(test, clang, executable, directory, "an initialized alias", "int v = 1;\nstatic int w __attribute__((alias(\"v\"))) = 3;\n",
                            "definition 'w' cannot also be an alias", true);
    check_test_alias_errors(test, clang, executable, directory, "two aliases", "int f(void) { return 1; }\nint g(void) __attribute__((alias(\"f\"), alias(\"f\")));\n",
                            "definition 'g' cannot also be an alias", true);
    check_test_alias_errors(test, clang, executable, directory, "an alias and an ifunc",
                            "static int (*r(void))(void) { return 0; }\nint h(void) __attribute__((alias(\"r\"), ifunc(\"r\")));\n",
                            "definition 'h' cannot also be an ifunc", true);
    check_test_alias_errors(test, clang, executable, directory, "external weakref", "int wf(void) __attribute__((weakref(\"target\")));\n",
                            "weakref declaration must have internal linkage", true);
    check_test_alias_errors(test, clang, executable, directory, "block weakref", "void f(void) { static int wl __attribute__((weakref(\"t\"))); }\n",
                            "weakref declaration of 'wl' must be in a global context", true);
    check_test_alias_errors(test, clang, executable, directory, "weakref without a target", "static int wf(void) __attribute__((weakref));\n",
                            "weakref declaration of 'wf' must also have an alias attribute", true);
    check_test_alias_errors(test, clang, executable, directory, "typedef weakref", "typedef int wt __attribute__((weakref(\"x\")));\n",
                            "'weakref' attribute only applies to variables and functions", true);
}

static void check_test_driver_retained(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source[] =
        "#include <stdio.h>\n"
        "__attribute__((retain, used)) static int kept = 5;\n"
        "__attribute__((retain)) int global = 6;\n"
        "__attribute__((retain)) int zeroed;\n"
        "__attribute__((retain)) static const int unreferenced_constant = 7;\n"
        "__attribute__((retain)) _Thread_local int thread_value = 8;\n"
        "__attribute__((retain)) _Thread_local int thread_zeroed;\n"
        "__attribute__((retain, section(\"retained_custom\"))) int placed = 9;\n"
        "__attribute__((retain)) static int local_function(void) { return 1; }\n"
        "__attribute__((retain)) int global_function(void) { return 2; }\n"
        "__attribute__((retain)) static int unreferenced_static = 3;\n"
        "__attribute__((retain)) static int unreferenced_function(void) { return 4; }\n"
        "static int plain = 10;\n__attribute__((used)) static int used_only = 11;\n"
        "int later = 12;\nint later __attribute__((retain));\n"
        "const char* const pointer __attribute__((retain)) = \"x\";\n"
        "static int table[3] __attribute__((retain, used)) = { 1, 2, 3 };\n"
        "static int counter(void) { static int local __attribute__((retain, used)) = 4; return local++; }\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%d %d %d %d %d %d %d %d %d %d %s %d %d\\n\", kept, global, zeroed, thread_value, thread_zeroed, placed, local_function(), global_function(),\n"
        "           plain, later, pointer, counter(), counter());\n"
        "    return 0;\n"
        "}\n";
    char path[512];
    char objects[3][512];
    char programs[3][512];
    char direct[2][512];
    snprintf(path, sizeof(path), "%s/retained.c", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1);
    char captured[3][256] = { { 0 } };
    char* text = NULL;
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        snprintf(objects[p], sizeof(objects[p]), "%s/retained-%u.o", directory, p);
        snprintf(programs[p], sizeof(programs[p]), "%s/retained-%u", directory, p);
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", objects[p], p == 2 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        check_test_expect(test, compiled, "retained definitions compile through clang and both checker paths");
        char difference[1024] = { 0 };
        const bool same = compiled && (p == 0 || check_test_driver_same_sections(objects[p], objects[0], ".data.kept", false, difference, sizeof(difference)));
        char description[1280];
        snprintf(description, sizeof(description), "each retained definition is in clang's section by the %s path (%s)", p == 1 ? "vector" : "scalar", difference);
        check_test_expect(test, p == 0 || same, description);
        // SHF_GNU_RETAIN makes the object ELFOSABI_GNU, as LLVM marks clang's.
        const uint8_t abi = compiled ? check_test_alias_abi(objects[p]) : 0xff;
        snprintf(description, sizeof(description), "the retained definitions' object is ELFOSABI_GNU by %s (EI_OSABI %u)",
                 p == 0 ? "clang" : p == 1 ? "the vector path" : "the scalar path", abi);
        check_test_expect(test, abi == ELFOSABI_GNU, description);
        char* const link[] = { (char*)clang, objects[p], "-o", programs[p], NULL };
        const bool linked = compiled && check_test_driver_spawn(link, &text) == 0;
        free(text);
        check_test_expect(test, linked && check_test_driver_capture(programs[p], captured[p], sizeof(captured[p])), "the retained definitions' objects link and run");
    }
    check_test_expect(test, written && check_test_driver_same_files(objects[1], objects[2]), "retained definitions' scalar and vector objects match byte for byte");
    for (uint32_t p = 1; written && p < 3; p += 1)
    {
        char description[640];
        snprintf(description, sizeof(description), "the %s path's retained definitions run as clang's: %s against %s", p == 1 ? "vector" : "scalar", captured[p],
                 captured[0]);
        check_test_expect(test, captured[0][0] != 0 && strcmp(captured[p], captured[0]) == 0, description);
    }
    for (uint32_t p = 0; written && p < 2; p += 1)
    {
        snprintf(direct[p], sizeof(direct[p]), "%s/retained-direct-%u", directory, p);
        char* arguments[] = { (char*)executable, "-std=gnu17", "-O0", path, "-o", direct[p], p == 1 ? "--scalar" : NULL, NULL };
        const bool built = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        char ran[256] = { 0 };
        char description[640];
        const bool running = built && check_test_driver_capture(direct[p], ran, sizeof(ran));
        snprintf(description, sizeof(description), "the direct path's retained definitions run as clang's build, by the %s path: %s against %s",
                 p == 0 ? "vector" : "scalar", ran, captured[0]);
        check_test_expect(test, running && strcmp(ran, captured[0]) == 0, description);
    }
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        unlink(objects[p]);
        unlink(programs[p]);
    }
    for (uint32_t p = 0; written && p < 2; p += 1)
    {
        unlink(direct[p]);
    }
    unlink(path);
}
