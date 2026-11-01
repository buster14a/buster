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
        "#if __has_attribute(constructor) && __has_attribute(__constructor__) && __has_c_attribute(gnu::constructor)\n"
        "#define QUERY_CONSTRUCTOR __attribute__((constructor))\n#else\n#define QUERY_CONSTRUCTOR\n#endif\n"
        "#if __has_attribute(destructor) && __has_attribute(__destructor__) && __has_c_attribute(gnu::destructor)\n"
        "#define QUERY_DESTRUCTOR __attribute__((destructor))\n#else\n#define QUERY_DESTRUCTOR\n#endif\n"
        "#if __has_attribute(weak) && __has_attribute(__weak__) && __has_c_attribute(gnu::weak)\n"
        "#define QUERY_WEAK __attribute__((weak))\n#else\n#define QUERY_WEAK\n#endif\n"
        "static char order[32];\nstatic int placed;\nstatic void mark(char c) { order[placed++] = c; }\n"
        "static void QUERY_CONSTRUCTOR first_default(void) { mark('a'); }\n"
        "static void __attribute__((constructor(200))) at_200(void) { mark('b'); }\n"
        "static void __attribute__((constructor(101))) at_101(void) { mark('c'); }\n"
        "void __attribute__((constructor(65536))) past_limit(void) { mark('d'); }\n"
        "static void __attribute__((constructor(101))) also_101(void) { mark('e'); }\n"
        "__attribute__((constructor(150))) static void declared_first(void);\nstatic void declared_first(void) { mark('f'); }\n"
        "static void late(void) { mark('g'); }\nstatic void late(void) __attribute__((constructor));\n"
        "static void __attribute__((constructor(300), constructor(120))) first_of_two(void) { mark('h'); }\n"
        "__attribute__((constructor(260))) void redeclared(void);\nvoid __attribute__((constructor(140))) redeclared(void) { mark('i'); }\n"
        "static void __attribute__((constructor, destructor)) both(void) { mark('j'); putchar('j'); }\n"
        "static void QUERY_DESTRUCTOR last_default(void) { putchar('z'); }\n"
        "static void __attribute__((destructor(110))) d110(void) { putchar('x'); }\n"
        "static void __attribute__((destructor(220))) d220(void) { putchar('y'); }\n"
        "void __attribute__((destructor(-1))) wrapped(void) { putchar('w'); }\n"
        "int QUERY_WEAK weak_defined(void) { return 3; }\n"
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

// A template's symbols in -c's object (#812): its `.L` and numeric labels are the assembler's temporary symbols, which
// the object leaves out; an undefined `.globl` declaration and a `.set`/`.equ` absolute are the template's alone, which
// the object leaves out too (clang writes them, as GLOBAL UND and LOCAL ABS); and a `.hidden` label keeps its visibility.
// The source's labels are read from both objects' own symbol tables.
static bool check_test_elf_symbol_named(const CheckTestElf* elf, const char* spelling)
{
    bool found = false;
    for (uint64_t i = 1; !found && i < elf->symbol_count; i += 1)
    {
        const char* name = check_test_elf_string(elf->symbol_names, elf->symbol_names_size, elf->symbols[i].st_name);
        found = name != 0 && strcmp(name, spelling) == 0;
    }
    return found;
}

static bool check_test_elf_symbol_prefix(const CheckTestElf* elf, const char* prefix)
{
    bool found = false;
    const size_t length = strlen(prefix);
    for (uint64_t i = 1; !found && i < elf->symbol_count; i += 1)
    {
        const char* name = check_test_elf_string(elf->symbol_names, elf->symbol_names_size, elf->symbols[i].st_name);
        found = name != 0 && strncmp(name, prefix, length) == 0;
    }
    return found;
}

static bool check_test_elf_symbol_size(const CheckTestElf* elf, const char* spelling, uint64_t* size)
{
    bool found = false;
    for (uint64_t i = 1; !found && i < elf->symbol_count; i += 1)
    {
        const char* name = check_test_elf_string(elf->symbol_names, elf->symbol_names_size, elf->symbols[i].st_name);
        found = name != 0 && strcmp(name, spelling) == 0;
        if (found)
        {
            *size = elf->symbols[i].st_size;
        }
    }
    return found;
}

static void check_test_driver_asm_object_symbols(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source_text[] =
        "void target(void) { volatile int value = 1; value = value + 2; (void)value; }\n"
        "static void static_target(void) {}\n"
        "int global = 5;\n"
        "static int static_global = 5;\n"
        "int tested(int n)\n"
        "{\n"
        "    __asm__ volatile(\".pushsection .data\\n.Ldirective_local:\\n.byte 1\\n.popsection\");\n"
        "    __asm__ volatile(\".pushsection .data\\n.hidden directive_hidden\\ndirective_hidden: .byte 2\\n.popsection\");\n"
        "    __asm__ volatile(\".globl directive_declared\");\n"
        "    __asm__ volatile(\".globl directive_cross\");\n"
        "    __asm__ volatile(\"directive_cross: nop\");\n"
        "    __asm__ volatile(\".set directive_set, 7\");\n"
        "    __asm__ volatile(\".hidden directive_external\");\n"
        "    __asm__ volatile(\"call directive_external\");\n"
        "    __asm__ volatile(\".weak directive_weak_decl\");\n"
        "    __asm__ volatile(\".hidden directive_hidden_decl\");\n"
        "    __asm__ volatile(\".weak target\");\n"
        "    __asm__ volatile(\".weak static_global\");\n"
        "    __asm__ volatile(\".hidden target\");\n"
        "    __asm__ volatile(\".globl static_target\");\n"
        "    __asm__ volatile(\".size target, 7\");\n"
        "    __asm__ volatile(\".size global, 4\");\n"
        "    __asm__ volatile(\"jmp 1f\\n1: nop\");\n"
        "    return n;\n"
        "}\n"
        "int main(void) { static_target(); static_global++; return tested(0) + static_global - 6; }\n";
    char source[512];
    char ours_path[512];
    char theirs_path[512];
    snprintf(source, sizeof(source), "%s/asm-directive-symbols.c", directory);
    snprintf(ours_path, sizeof(ours_path), "%s/asm-directive-symbols.o", directory);
    snprintf(theirs_path, sizeof(theirs_path), "%s/asm-directive-symbols-clang.o", directory);
    const bool written = object_write_file(source, (const uint8_t*)source_text, sizeof(source_text) - 1) == OBJECT_ERROR_NONE;
    const char* arguments[] = { "-std=gnu17" };
    const CheckCorpusSymbols oracle_object = { .clang = clang, .arguments = arguments, .argument_count = BUSTER_ARRAY_LENGTH(arguments), .directory = directory };
    CheckCorpusOracle oracle = { 0 };
    const bool accepted = written && check_corpus_clang_file(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), source, &oracle);
    const bool compiled_theirs = accepted && !oracle.rejected && check_corpus_clang_object(&oracle_object, source, theirs_path);
    char* const our_arguments[] = { (char*)executable, "-std=gnu17", "-c", "-o", ours_path, source, NULL };
    char* text = NULL;
    const bool compiled_ours = accepted && !oracle.rejected && check_test_driver_spawn(our_arguments, &text) == 0;
    free(text);
    check_test_expect(test, compiled_ours && compiled_theirs, "a template's directive symbols make clang's object and ours");
    Arena ours_arena = arena_reserve(UINT64_C(1) << 22);
    Arena theirs_arena = arena_reserve(UINT64_C(1) << 22);
    CheckTestElf ours = { 0 };
    CheckTestElf theirs = { 0 };
    const bool loaded = compiled_ours && compiled_theirs && ours_arena.base != 0 && theirs_arena.base != 0 &&
                        check_test_elf_open(&ours, object_test_read_file(&ours_arena, ours_path)) &&
                        check_test_elf_open(&theirs, object_test_read_file(&theirs_arena, theirs_path));
    check_test_expect(test, loaded, "the template symbols' objects are readable");
    check_test_expect(test, loaded && !check_test_elf_symbol_named(&ours, ".Ldirective_local") && !check_test_elf_symbol_prefix(&ours, ".LC"),
                      "a template's local and numeric labels are -c's temporary symbols");
    check_test_expect(test, loaded && !check_test_elf_symbol_named(&ours, "directive_declared") && check_test_elf_symbol_named(&theirs, "directive_declared"),
                      "an undefined '.globl' declaration is left out (clang keeps it)");
    check_test_expect(test, loaded && !check_test_elf_symbol_named(&ours, "directive_set") && check_test_elf_symbol_named(&theirs, "directive_set"),
                      "a template's '.set' is its own (clang writes an absolute symbol)");
    uint16_t ours_visibility = 0;
    uint16_t theirs_visibility = 0;
    const bool our_hidden = loaded && check_test_visibility_symbol(ours_path, "directive_hidden", &ours_visibility);
    const bool their_hidden = loaded && check_test_visibility_symbol(theirs_path, "directive_hidden", &theirs_visibility);
    check_test_expect(test, our_hidden && their_hidden && (ours_visibility & 3) == STV_HIDDEN && ours_visibility == theirs_visibility,
                      "a template label's '.hidden' is in -c's object as clang's is");
    uint16_t our_external = 0;
    uint16_t their_external = 0;
    const bool our_reference = loaded && check_test_visibility_symbol(ours_path, "directive_external", &our_external);
    const bool their_reference = loaded && check_test_visibility_symbol(theirs_path, "directive_external", &their_external);
    check_test_expect(test, our_reference && their_reference && (our_external & 3) == STV_HIDDEN && our_external == their_external,
                      "a template's '.hidden' declaration is in -c's undefined symbol as clang's is");
    uint16_t our_target = 0;
    uint16_t their_target = 0;
    uint64_t our_size = 0;
    uint64_t their_size = 0;
    const bool our_target_symbol = loaded && check_test_visibility_symbol(ours_path, "target", &our_target);
    const bool their_target_symbol = loaded && check_test_visibility_symbol(theirs_path, "target", &their_target);
    const bool our_target_size = loaded && check_test_elf_symbol_size(&ours, "target", &our_size);
    const bool their_target_size = loaded && check_test_elf_symbol_size(&theirs, "target", &their_size);
    check_test_expect(test, our_target_symbol && their_target_symbol && (our_target & 0xff00) == (2 << 8) && (our_target & 3) == STV_HIDDEN &&
                                our_target == their_target && our_target_size && their_target_size && our_size == 7 && their_size == 7,
                      "a template's '.weak', '.hidden' and '.size' change the unit's C function as clang's object has it");
    uint64_t our_global_size = 0;
    uint64_t their_global_size = 0;
    const bool our_global = loaded && check_test_elf_symbol_size(&ours, "global", &our_global_size);
    const bool their_global = loaded && check_test_elf_symbol_size(&theirs, "global", &their_global_size);
    check_test_expect(test, our_global && their_global && our_global_size == 4 && their_global_size == 4,
                      "a template's '.size' sets a C variable's size after its data atom exists");
    uint16_t our_static_global = 0;
    uint16_t their_static_global = 0;
    const bool our_static_global_symbol = loaded && check_test_visibility_symbol(ours_path, "static_global", &our_static_global);
    const bool their_static_global_symbol = loaded && check_test_visibility_symbol(theirs_path, "static_global", &their_static_global);
    check_test_expect(test, our_static_global_symbol && their_static_global_symbol && (our_static_global & 0xff00) == (2 << 8) &&
                                our_static_global == their_static_global,
                      "a template's '.weak' applies to a static C variable as clang's object has it");
    uint16_t our_static = 0;
    uint16_t their_static = 0;
    const bool our_static_symbol = loaded && check_test_visibility_symbol(ours_path, "static_target", &our_static);
    const bool their_static_symbol = loaded && check_test_visibility_symbol(theirs_path, "static_target", &their_static);
    check_test_expect(test, our_static_symbol && their_static_symbol && (our_static & 0xff00) == 0x100 && our_static == their_static,
                      "a template's '.globl' makes a static C function global as clang's object has it");
    uint16_t our_cross = 0;
    uint16_t their_cross = 0;
    const bool our_cross_symbol = loaded && check_test_visibility_symbol(ours_path, "directive_cross", &our_cross);
    const bool their_cross_symbol = loaded && check_test_visibility_symbol(theirs_path, "directive_cross", &their_cross);
    check_test_expect(test, our_cross_symbol && their_cross_symbol && (our_cross & 0xff00) == 0x100 && our_cross == their_cross,
                      "a template's '.globl' upgrades a label another template defines, as clang's object has it");
    check_test_expect(test, loaded && !check_test_elf_symbol_named(&ours, "directive_weak_decl") && check_test_elf_symbol_named(&theirs, "directive_weak_decl") &&
                                !check_test_elf_symbol_named(&ours, "directive_hidden_decl") && check_test_elf_symbol_named(&theirs, "directive_hidden_decl"),
                      "-c leaves out an unreferenced undefined weak or hidden declaration (documented)");
    static const char oversized[] = "int tested(int n){__asm__ volatile(\".size tested, 1000\");return n;}\n";
    char oversized_source[512];
    char oversized_object[512];
    snprintf(oversized_source, sizeof(oversized_source), "%s/asm-size-past-atom.c", directory);
    snprintf(oversized_object, sizeof(oversized_object), "%s/asm-size-past-atom.o", directory);
    const bool oversized_written = object_write_file(oversized_source, (const uint8_t*)oversized, sizeof(oversized) - 1) == OBJECT_ERROR_NONE;
    char* const oversized_arguments[] = { (char*)executable, "-std=gnu17", "-c", "-o", oversized_object, oversized_source, NULL };
    char* oversized_text = NULL;
    const int oversized_status = oversized_written ? check_test_driver_spawn(oversized_arguments, &oversized_text) : -1;
    check_test_expect(test, oversized_status == 1 && oversized_text != NULL &&
                                strstr(oversized_text, "inline assembly .size on the unit's symbol exceeds its definition") != NULL,
                      "a template's '.size' past a C symbol's definition is refused by name");
    free(oversized_text);
    unlink(oversized_source);
    unlink(oversized_object);
    static const char weak_variable[] = "int global = 5;\nvoid f(void) { __asm__ volatile(\".weak global\"); }\nint main(void) { return global == 5 ? 0 : 1; }\n";
    char weak_variable_source[512];
    char weak_variable_object[512];
    snprintf(weak_variable_source, sizeof(weak_variable_source), "%s/asm-weak-variable.c", directory);
    snprintf(weak_variable_object, sizeof(weak_variable_object), "%s/asm-weak-variable.o", directory);
    const bool weak_variable_written = object_write_file(weak_variable_source, (const uint8_t*)weak_variable, sizeof(weak_variable) - 1) == OBJECT_ERROR_NONE;
    char* const weak_variable_arguments[] = { (char*)executable, "-std=gnu17", "-c", "-o", weak_variable_object, weak_variable_source, NULL };
    char* weak_variable_text = NULL;
    const int weak_variable_status = weak_variable_written ? check_test_driver_spawn(weak_variable_arguments, &weak_variable_text) : -1;
    check_test_expect(test, weak_variable_status == 1 && weak_variable_text != NULL && strstr(weak_variable_text, "inline assembly .weak on a defined variable") != NULL,
                      "a template's '.weak' on a defined C variable is refused, as clang refuses it");
    free(weak_variable_text);
    unlink(weak_variable_source);
    unlink(weak_variable_object);
    // A template's `.weak` makes the unit's own C definition weak, so the direct path takes another object's strong
    // definition, as clang's link does.
    static const char weak_source[] = "void target(void) { }\nvoid f(void) { __asm__ volatile(\".weak target\"); }\nint main(void) { f(); return 0; }\n";
    static const char strong_source[] = "void target(void) { __asm__ volatile(\"nop\"); }\n";
    char weak_path[512];
    char strong_path[512];
    char weak_program[512];
    snprintf(weak_path, sizeof(weak_path), "%s/asm-weak.c", directory);
    snprintf(strong_path, sizeof(strong_path), "%s/asm-strong.c", directory);
    snprintf(weak_program, sizeof(weak_program), "%s/asm-weak-program", directory);
    const bool weak_written = object_write_file(weak_path, (const uint8_t*)weak_source, sizeof(weak_source) - 1) == OBJECT_ERROR_NONE &&
                              object_write_file(strong_path, (const uint8_t*)strong_source, sizeof(strong_source) - 1) == OBJECT_ERROR_NONE;
    char* const weak_arguments[] = { (char*)executable, "-o", weak_program, weak_path, strong_path, NULL };
    char* weak_text = NULL;
    const int weak_linked = weak_written ? check_test_driver_spawn(weak_arguments, &weak_text) : -1;
    free(weak_text);
    char* const weak_run_arguments[] = { weak_program, NULL };
    char* weak_output = NULL;
    const int weak_status = weak_linked == 0 ? check_test_driver_spawn(weak_run_arguments, &weak_output) : -1;
    free(weak_output);
    check_test_expect(test, weak_linked == 0 && weak_status == 0, "a template's '.weak' lets another object's strong definition link");
    unlink(weak_path);
    unlink(strong_path);
    unlink(weak_program);
    arena_release(&ours_arena);
    arena_release(&theirs_arena);
    unlink(source);
    unlink(ours_path);
    unlink(theirs_path);
}
