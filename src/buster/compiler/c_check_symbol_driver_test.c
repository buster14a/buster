// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's constructor, destructor and weak attributes in -c's objects and the direct path's executables (#147), on an
// original fixture, against clang's: each symbol's binding (check_test_visibility_symbol's reader of the objects), the
// programs' output (the constructors' order before main and the destructors' after it) from cc's links of clang's object
// and both checker paths' objects, which must be the same bytes, and from the direct path's executables of both paths;
// and the same links with a strong definition in another object, which the weak definitions must give way to; and the
// prioritized entries' sections in clang's order, by priority. Include after c_check_visibility_driver_test.c and the
// driver test's ELF reader (CheckTestElf).

// The names of an object's .init_array.N and .fini_array.N sections, in the file's order, each followed by a space.
static bool check_test_symbol_array_sections(const char* path, char* names, uint64_t capacity)
{
    Arena arena = arena_reserve(UINT64_C(1) << 26);
    CheckTestElf elf = { 0 };
    bool valid = arena.base != 0 && capacity != 0 && check_test_elf_open(&elf, object_test_read_file(&arena, path));
    uint64_t used = 0;
    for (uint32_t s = 1; valid && s < elf.section_count; s += 1)
    {
        const char* name = check_test_elf_section_name(&elf, s);
        const bool prioritized = name != 0 && (strncmp(name, ".init_array.", 12) == 0 || strncmp(name, ".fini_array.", 12) == 0);
        const uint64_t length = prioritized ? strlen(name) : 0;
        valid = !prioritized || length + 1 < capacity - used;
        if (valid && prioritized)
        {
            memcpy(names + used, name, length);
            names[used + length] = ' ';
            used += length + 1;
        }
    }
    if (capacity != 0)
    {
        names[used] = 0;
    }
    arena_release(&arena);
    return valid;
}

static void check_test_driver_symbols(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source[] =
        "#include <stdio.h>\n"
        "static char order[32];\nstatic int placed;\nstatic void mark(char c) { order[placed++] = c; }\n"
        "static void __attribute__((constructor)) first_default(void) { mark('a'); }\n"
        "static void __attribute__((constructor(200))) at_200(void) { mark('b'); }\n"
        "static void __attribute__((constructor(101))) at_101(void) { mark('c'); }\n"
        "void __attribute__((constructor(65536))) past_limit(void) { mark('d'); }\n"
        "static void __attribute__((constructor(101))) also_101(void) { mark('e'); }\n"
        "__attribute__((constructor(150))) static void declared_first(void);\nstatic void declared_first(void) { mark('f'); }\n"
        "static void late(void) { mark('g'); }\nstatic void late(void) __attribute__((constructor));\n"
        "static void __attribute__((constructor(300), constructor(120))) first_of_two(void) { mark('h'); }\n"
        "__attribute__((constructor(260))) void redeclared(void);\nvoid __attribute__((constructor(140))) redeclared(void) { mark('i'); }\n"
        "static void __attribute__((constructor, destructor)) both(void) { mark('j'); putchar('j'); }\n"
        "static void __attribute__((destructor)) last_default(void) { putchar('z'); }\n"
        "static void __attribute__((destructor(110))) d110(void) { putchar('x'); }\n"
        "static void __attribute__((destructor(220))) d220(void) { putchar('y'); }\n"
        "void __attribute__((destructor(-1))) wrapped(void) { putchar('w'); }\n"
        "int __attribute__((weak)) weak_defined(void) { return 3; }\n"
        "extern int __attribute__((weak)) weak_missing(void);\n"
        "int __attribute__((weak)) weak_data = 4;\n"
        "int __attribute__((weak)) weak_tentative;\n"
        "int strong_later = 5;\nint strong_later __attribute__((weak));\n"
        "__attribute__((weak)) int pair_tentative, pair_defined = 6;\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%s %d %d %d %d %d %d %d\\n\", order, weak_defined(), weak_missing ? weak_missing() : -1, weak_data, weak_tentative,\n"
        "           strong_later, pair_tentative, pair_defined);\n"
        "    return 0;\n"
        "}\n";
    static const char strong[] = "int weak_defined(void) { return 30; }\nint weak_missing(void) { return 40; }\nint weak_data = 50;\n";
    static const char* const names[] = { "weak_defined", "weak_missing", "weak_data", "weak_tentative", "strong_later", "pair_tentative", "pair_defined",
                                         "past_limit", "redeclared", "wrapped", "first_default", "main" };
    char path[512] = { 0 };
    char strong_path[512] = { 0 };
    char strong_object[512] = { 0 };
    char objects[3][512] = { { 0 } };
    char programs[3][512] = { { 0 } };
    char strong_programs[3][512] = { { 0 } };
    char direct[2][512] = { { 0 } };
    snprintf(path, sizeof(path), "%s/symbols.c", directory);
    snprintf(strong_path, sizeof(strong_path), "%s/strong.c", directory);
    snprintf(strong_object, sizeof(strong_object), "%s/strong.o", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1) && parse_test_write(strong_path, strong, sizeof(strong) - 1);
    check_test_expect(test, written, "symbol attributes' fixture is written");
    char* text = NULL;
    char* const strong_arguments[] = { (char*)clang, "-std=gnu17", "-O0", "-c", strong_path, "-o", strong_object, NULL };
    const bool strong_compiled = written && check_test_driver_spawn(strong_arguments, &text) == 0;
    free(text);
    check_test_expect(test, strong_compiled, "the strong definitions compile through clang");
    char captured[3][256] = { { 0 } };
    char strong_captured[3][256] = { { 0 } };
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        snprintf(objects[p], sizeof(objects[p]), "%s/symbols-%u.o", directory, p);
        snprintf(programs[p], sizeof(programs[p]), "%s/symbols-%u", directory, p);
        snprintf(strong_programs[p], sizeof(strong_programs[p]), "%s/symbols-strong-%u", directory, p);
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", objects[p], p == 2 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        check_test_expect(test, compiled, "constructors, destructors and weak symbols compile through clang and both checker paths");
        for (uint32_t n = 0; compiled && p != 0 && n < BUSTER_ARRAY_LENGTH(names); n += 1)
        {
            uint16_t ours = UINT16_MAX;
            uint16_t theirs = UINT16_MAX - 1;
            char description[160];
            snprintf(description, sizeof(description), "%s's binding is clang's, by the %s path", names[n], p == 1 ? "vector" : "scalar");
            check_test_expect(test, check_test_visibility_symbol(objects[p], names[n], &ours) && check_test_visibility_symbol(objects[0], names[n], &theirs) &&
                                   ours == theirs, description);
        }
        char* const link[] = { (char*)clang, objects[p], "-o", programs[p], NULL };
        char* const link_strong[] = { (char*)clang, objects[p], strong_object, "-o", strong_programs[p], NULL };
        const bool linked = compiled && check_test_driver_spawn(link, &text) == 0;
        free(text);
        const bool linked_strong = compiled && strong_compiled && check_test_driver_spawn(link_strong, &text) == 0;
        free(text);
        check_test_expect(test, linked && check_test_driver_capture(programs[p], captured[p], sizeof(captured[p])), "the objects link and run");
        check_test_expect(test, linked_strong && check_test_driver_capture(strong_programs[p], strong_captured[p], sizeof(strong_captured[p])),
                          "the objects link with the strong definitions and run");
    }
    check_test_expect(test, written && check_test_driver_same_files(objects[1], objects[2]), "symbol attributes' scalar and vector objects match byte for byte");
    char sections[2][512] = { { 0 } };
    const bool listed = written && check_test_symbol_array_sections(objects[0], sections[0], sizeof(sections[0])) &&
                        check_test_symbol_array_sections(objects[1], sections[1], sizeof(sections[1]));
    char order[1280];
    snprintf(order, sizeof(order), "the prioritized entries' sections are clang's, in its order: %s against %s", sections[1], sections[0]);
    check_test_expect(test, listed && sections[0][0] != 0 && strcmp(sections[0], sections[1]) == 0, order);
    for (uint32_t p = 1; written && p < 3; p += 1)
    {
        char description[512];
        snprintf(description, sizeof(description), "the %s path's object runs as clang's: %s against %s", p == 1 ? "vector" : "scalar", captured[p], captured[0]);
        check_test_expect(test, strcmp(captured[p], captured[0]) == 0, description);
        snprintf(description, sizeof(description), "the %s path's weak definitions give way to strong ones as clang's: %s against %s", p == 1 ? "vector" : "scalar",
                 strong_captured[p], strong_captured[0]);
        check_test_expect(test, strcmp(strong_captured[p], strong_captured[0]) == 0 && strcmp(strong_captured[p], captured[p]) != 0, description);
    }
    // The direct path's executables, of both paths: the same bytes, run as clang's build of the fixture.
    char direct_captured[2][256] = { { 0 } };
    for (uint32_t p = 0; written && p < 2; p += 1)
    {
        snprintf(direct[p], sizeof(direct[p]), "%s/symbols-direct-%u", directory, p);
        char* arguments[] = { (char*)executable, "-std=gnu17", "-O0", path, "-o", direct[p], p == 1 ? "--scalar" : NULL, NULL };
        const bool built = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        char description[512];
        const bool ran = built && check_test_driver_capture(direct[p], direct_captured[p], sizeof(direct_captured[p]));
        snprintf(description, sizeof(description), "the direct path's executable runs as clang's build, by the %s path: %s against %s",
                 p == 0 ? "vector" : "scalar", direct_captured[p], captured[0]);
        check_test_expect(test, ran && strcmp(direct_captured[p], captured[0]) == 0, description);
    }
    check_test_expect(test, written && check_test_driver_same_files(direct[0], direct[1]), "symbol attributes' scalar and vector executables match byte for byte");
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
    unlink(strong_path);
    unlink(strong_object);
}
