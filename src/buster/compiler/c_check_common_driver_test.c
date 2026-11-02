// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's common attribute (#147) by the driver against clang, on original fixtures. A tentative definition of external
// linkage with the attribute is a common symbol, which -c's object writes as SHN_COMMON with its alignment as its value,
// as clang's does; one that an initializer defines, a section or a weak attribute, thread-local storage or internal
// linkage keeps a definition of its own. Each named symbol of the unit must be clang's (common or not, its value where it
// is common, its size, type, binding and visibility), by both paths, whose objects are the same bytes. Then a program of
// two units whose commons differ in size, a common beside a strong definition and beside a weak common, links from -c's
// objects by cc and as the direct path's executable of both units (by default, on one lane and by the scalar path), and
// must run as clang's build does. -fcommon makes every tentative definition of external linkage a common one, but for
// nocommon's. Include after c_check_alias_driver_test.c.

// The symbol `name` of the object at `path`: its st_info, st_other, whether it is SHN_COMMON's, its value where it is,
// and its size, in `summary`. Whether the object could be read and has the symbol.
static bool check_test_common_symbol(const char* path, const char* name, char* summary, size_t capacity)
{
    FILE* file = fopen(path, "rb");
    uint8_t* bytes = NULL;
    long size = -1;
    if (file != NULL && fseek(file, 0, SEEK_END) == 0)
    {
        size = ftell(file);
        bytes = size > (long)sizeof(Elf64_Ehdr) && fseek(file, 0, SEEK_SET) == 0 ? malloc((size_t)size) : NULL;
        if (bytes != NULL && fread(bytes, 1, (size_t)size, file) != (size_t)size)
        {
            free(bytes);
            bytes = NULL;
        }
    }
    if (file != NULL)
    {
        fclose(file);
    }
    bool found = false;
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)bytes;
    const bool sections = bytes != NULL && header->e_shoff + (uint64_t)header->e_shnum * sizeof(Elf64_Shdr) <= (uint64_t)size;
    for (uint32_t s = 0; sections && !found && s < header->e_shnum; s += 1)
    {
        const Elf64_Shdr* table = (const Elf64_Shdr*)(bytes + header->e_shoff) + s;
        const Elf64_Shdr* strings = table->sh_link < header->e_shnum ? (const Elf64_Shdr*)(bytes + header->e_shoff) + table->sh_link : NULL;
        const bool symbols = table->sh_type == SHT_SYMTAB && strings != NULL && table->sh_offset + table->sh_size <= (uint64_t)size &&
                             strings->sh_offset + strings->sh_size <= (uint64_t)size;
        for (uint64_t k = 1; symbols && !found && k < table->sh_size / sizeof(Elf64_Sym); k += 1)
        {
            const Elf64_Sym* symbol = (const Elf64_Sym*)(bytes + table->sh_offset) + k;
            const char* spelled = symbol->st_name < strings->sh_size ? (const char*)bytes + strings->sh_offset + symbol->st_name : "";
            if (strnlen(spelled, strings->sh_size - symbol->st_name) < strings->sh_size - symbol->st_name && strcmp(spelled, name) == 0)
            {
                const bool common = symbol->st_shndx == SHN_COMMON;
                snprintf(summary, capacity, "%s: info %u other %u %s value %llu size %llu", name, symbol->st_info, symbol->st_other, common ? "common" : "defined",
                         common ? (unsigned long long)symbol->st_value : 0ull, (unsigned long long)symbol->st_size);
                found = true;
            }
        }
    }
    free(bytes);
    return found;
}

static void check_test_driver_commons(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char unit[] =
        "int plain __attribute__((common));\n"
        "char buffer[100] __attribute__((common));\n"
        "const int constant __attribute__((common));\n"
        "int open[] __attribute__((common));\n"
        "struct S { int n; char c[]; } flexible __attribute__((common));\n"
        "extern int declared __attribute__((common));\nint declared;\n"
        "int later __attribute__((common));\nint later = 5;\n"
        "int initialized __attribute__((common)) = 6;\n"
        "static int internal __attribute__((common));\n"
        "__attribute__((visibility(\"hidden\"))) int hidden __attribute__((common));\n"
        "int weak __attribute__((common, weak));\n"
        "int placed __attribute__((common, section(\"common_placed\")));\n"
        "_Thread_local int thread __attribute__((common));\n"
        "long aligned __attribute__((common, aligned(32)));\n"
        "int* pointer = &plain;\n"
        "int get(void) { return plain + buffer[3] + constant + open[0] + flexible.n + declared + later + initialized + internal + hidden + weak + placed + "
        "thread + (int)aligned; }\n";
    static const char* const names[] = { "plain", "buffer", "constant", "open", "flexible", "declared", "later", "initialized", "hidden", "weak", "placed",
                                          "thread", "aligned", "pointer" };
    char path[512];
    char objects[3][512];
    snprintf(path, sizeof(path), "%s/commons.c", directory);
    const bool written = parse_test_write(path, unit, sizeof(unit) - 1);
    char* text = NULL;
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        snprintf(objects[p], sizeof(objects[p]), "%s/commons-%u.o", directory, p);
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-w", "-c", path, "-o", objects[p], p == 2 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        check_test_expect(test, compiled, "commons: the unit compiles through clang and both checker paths");
        for (uint32_t n = 0; compiled && p != 0 && n < BUSTER_ARRAY_LENGTH(names); n += 1)
        {
            char expected[192] = { 0 };
            char actual[192] = { 0 };
            const bool both = check_test_common_symbol(objects[0], names[n], expected, sizeof(expected)) &&
                              check_test_common_symbol(objects[p], names[n], actual, sizeof(actual));
            char description[512];
            snprintf(description, sizeof(description), "commons, by the %s path: clang's %s, and ours %s", p == 1 ? "vector" : "scalar", expected, actual);
            check_test_expect(test, both && strcmp(expected, actual) == 0, description);
        }
    }
    check_test_expect(test, written && check_test_driver_same_files(objects[1], objects[2]), "commons: the scalar and vector objects match byte for byte");
    // -fcommon: every tentative definition of external linkage is a common one, but for nocommon's, and the attribute's
    // with nocommon beside it.
    static const char everything[] =
        "int tentative;\nint initialized = 1;\nint kept __attribute__((nocommon));\nint both __attribute__((common, nocommon));\n"
        "static int internal;\nextern int declared;\nint declared;\nchar array[40];\n_Thread_local int thread;\n"
        "int get(void) { return tentative + initialized + kept + both + internal + declared + array[1] + thread; }\n";
    static const char* const everything_names[] = { "tentative", "initialized", "kept", "both", "declared", "array", "thread" };
    char everything_path[512];
    char everything_objects[2][512];
    snprintf(everything_path, sizeof(everything_path), "%s/commons-everything.c", directory);
    const bool everything_written = parse_test_write(everything_path, everything, sizeof(everything) - 1);
    for (uint32_t p = 0; everything_written && p < 2; p += 1)
    {
        snprintf(everything_objects[p], sizeof(everything_objects[p]), "%s/commons-everything-%u.o", directory, p);
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-w", "-fcommon", "-c", everything_path, "-o", everything_objects[p], NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        check_test_expect(test, compiled, "commons: -fcommon's unit compiles through clang and the checker");
        for (uint32_t n = 0; compiled && p != 0 && n < BUSTER_ARRAY_LENGTH(everything_names); n += 1)
        {
            char expected[192] = { 0 };
            char actual[192] = { 0 };
            const bool both = check_test_common_symbol(everything_objects[0], everything_names[n], expected, sizeof(expected)) &&
                              check_test_common_symbol(everything_objects[p], everything_names[n], actual, sizeof(actual));
            char description[512];
            snprintf(description, sizeof(description), "commons, with -fcommon: clang's %s, and ours %s", expected, actual);
            check_test_expect(test, both && strcmp(expected, actual) == 0, description);
        }
    }
    for (uint32_t p = 0; everything_written && p < 2; p += 1)
    {
        unlink(everything_objects[p]);
    }
    unlink(everything_path);
    // Two units: commons of different sizes, whose largest the link keeps; a common beside a strong definition, which
    // takes its place; a common beside a weak common one, which is a common one's place too.
    static const char first[] =
        "int a __attribute__((common));\n"
        "char b[8] __attribute__((common));\n"
        "int c __attribute__((common));\n"
        "long d __attribute__((common));\n"
        "int first(void) { a += 1; b[7] = 'a'; return a + c + (int)d; }\n";
    static const char second[] =
        "#include <stdio.h>\n#include <stdint.h>\n"
        "int a __attribute__((common));\n"
        "char b[64] __attribute__((common, aligned(32)));\n"
        "int c = 40;\n"
        "long d __attribute__((common, weak));\n"
        "int first(void);\n"
        "int main(void) { a = 1; for (int i = 0; i < 64; i++) b[i] = (char)i; int r = first();\n"
        "printf(\"%d %d %d %ld %d %zu\\n\", r, a, b[7], d, (int)((uintptr_t)b % 32), sizeof b); return 0; }\n";
    char sources[2][512];
    snprintf(sources[0], sizeof(sources[0]), "%s/commons-first.c", directory);
    snprintf(sources[1], sizeof(sources[1]), "%s/commons-second.c", directory);
    const bool units = parse_test_write(sources[0], first, sizeof(first) - 1) && parse_test_write(sources[1], second, sizeof(second) - 1);
    char reference[512];
    snprintf(reference, sizeof(reference), "%s/commons-clang", directory);
    char* build[] = { (char*)clang, "-std=gnu17", "-O0", "-w", sources[0], sources[1], "-o", reference, NULL };
    const bool built = units && check_test_driver_spawn(build, &text) == 0;
    free(text);
    char expected[256] = { 0 };
    check_test_expect(test, built && check_test_driver_capture(reference, expected, sizeof(expected)), "commons: clang's build of the two units runs");
    // Ours: -c's objects linked by cc, then the direct path's executable by default, on one lane and by the scalar path.
    char unit_objects[2][512];
    char linked[512];
    snprintf(linked, sizeof(linked), "%s/commons-objects", directory);
    bool objects_built = built;
    for (uint32_t u = 0; objects_built && u < 2; u += 1)
    {
        snprintf(unit_objects[u], sizeof(unit_objects[u]), "%s/commons-unit-%u.o", directory, u);
        char* arguments[] = { (char*)executable, "-std=gnu17", "-w", "-c", sources[u], "-o", unit_objects[u], NULL };
        objects_built = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
    }
    char* link[] = { (char*)clang, unit_objects[0], unit_objects[1], "-o", linked, NULL };
    objects_built = objects_built && check_test_driver_spawn(link, &text) == 0;
    free(text);
    char ran[256] = { 0 };
    char description[768];
    snprintf(description, sizeof(description), "commons: -c's objects linked by cc run as clang's build: %s against %s",
             objects_built && check_test_driver_capture(linked, ran, sizeof(ran)) ? ran : "(nothing)", expected);
    check_test_expect(test, objects_built && strcmp(ran, expected) == 0, description);
    static const char* const ways[] = { NULL, "--lanes=1", "--scalar" };
    for (uint32_t w = 0; built && w < BUSTER_ARRAY_LENGTH(ways); w += 1)
    {
        char direct[512];
        snprintf(direct, sizeof(direct), "%s/commons-direct-%u", directory, w);
        char* arguments[] = { (char*)executable, "-std=gnu17", "-w", sources[0], sources[1], "-o", direct, (char*)ways[w], NULL };
        const bool made = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        char output[256] = { 0 };
        const bool running = made && check_test_driver_capture(direct, output, sizeof(output));
        snprintf(description, sizeof(description), "commons: the direct path's executable of both units runs as clang's build (%s): %s against %s",
                 ways[w] != NULL ? ways[w] : "by default", running ? output : "(nothing)", expected);
        check_test_expect(test, running && strcmp(output, expected) == 0, description);
        unlink(direct);
    }
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        unlink(objects[p]);
    }
    for (uint32_t u = 0; units && u < 2; u += 1)
    {
        unlink(unit_objects[u]);
        unlink(sources[u]);
    }
    unlink(reference);
    unlink(linked);
    unlink(path);
}
