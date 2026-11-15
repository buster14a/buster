// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1474 program: decoded-equivalent literals, named arrays, live occurrences after a dead first one,
// static pointer relocations and their addends, full embedded-NUL objects, and all character widths. Both tiers and
// scalar/vector readers run through the direct linker and through an ELF object linked by the system toolchain.
// Clang supplies an external comparison; zero errors is also stated independently.

// A selected object's .rodata must have only the selected function's initializer image. The full checked control
// table also contains the unused function's initializer; --lower-all deliberately keeps that function and its image.
static void check_test_driver_selected_initializer_pool(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[] = "tests/driver/selected_initializer_pool.c";
    char reference[256], object[256];
    snprintf(reference, sizeof(reference), "%s/selected-init-pool-clang.o", directory);
    snprintf(object, sizeof(object), "%s/selected-init-pool.o", directory);
    char* text = NULL;
    char* reference_arguments[] = { (char*)clang, "-std=gnu17", "-O0", "-c", "-o", reference, source, NULL };
    const int reference_status = check_test_driver_spawn(reference_arguments, &text);
    if (reference_status != 0 && text != NULL)
    {
        fprintf(stderr, "%s", text);
    }
    free(text);
    Arena reference_arena = arena_reserve(UINT64_C(1) << 22);
    CheckTestElf reference_elf = { 0 };
    const bool reference_read = reference_status == 0 && reference_arena.base != NULL &&
                                check_test_elf_open(&reference_elf, object_test_read_file(&reference_arena, reference));
    const Elf64_Shdr* reference_rodata = NULL;
    for (uint32_t s = 0; reference_read && s < reference_elf.section_count; s += 1)
    {
        const char* name = check_test_elf_section_name(&reference_elf, s);
        if (name != NULL && strcmp(name, ".rodata") == 0)
        {
            reference_rodata = reference_elf.sections + s;
        }
    }
    const bool reference_valid = reference_rodata != NULL && reference_rodata->sh_size == 256 &&
                                 check_test_elf_range(reference_elf.file, reference_rodata->sh_offset, reference_rodata->sh_size) &&
                                 check_test_elf_defined(&reference_elf, "used") != 0 && check_test_elf_defined(&reference_elf, "dead") == 0;
    check_test_expect(test, reference_valid, "Clang emits the live initializer image and omits the dead function");
    for (uint32_t mode = 0; mode < 4; mode += 1)
    {
        const bool scalar = (mode & 1) != 0;
        const bool all = (mode & 2) != 0;
        char* arguments[12] = { (char*)executable };
        uint32_t count = 1;
        if (scalar)
        {
            arguments[count++] = "--scalar";
        }
        if (all)
        {
            arguments[count++] = "--lower-all";
        }
        arguments[count++] = "-std=gnu17";
        arguments[count++] = "-c";
        arguments[count++] = "-o";
        arguments[count++] = object;
        arguments[count++] = source;
        arguments[count] = NULL;
        text = NULL;
        const int status = check_test_driver_spawn(arguments, &text);
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        Arena arena = arena_reserve(UINT64_C(1) << 22);
        CheckTestElf elf = { 0 };
        const bool read = status == 0 && arena.base != NULL && check_test_elf_open(&elf, object_test_read_file(&arena, object));
        const Elf64_Shdr* rodata = NULL;
        for (uint32_t s = 0; read && s < elf.section_count; s += 1)
        {
            const char* name = check_test_elf_section_name(&elf, s);
            if (name != NULL && strcmp(name, ".rodata") == 0)
            {
                rodata = elf.sections + s;
            }
        }
        const bool shape = read && rodata != NULL && rodata->sh_size == (all ? 512 : 256) &&
                           check_test_elf_range(elf.file, rodata->sh_offset, rodata->sh_size) &&
                           check_test_elf_defined(&elf, "used") != 0 && (check_test_elf_defined(&elf, "dead") != 0) == all;
        const bool bytes = shape && (all || (reference_valid && memcmp(elf.file.pointer + rodata->sh_offset,
                                  reference_elf.file.pointer + reference_rodata->sh_offset, 256) == 0));
        char description[160];
        snprintf(description, sizeof(description), "selected initializer pools match object selection (%s, %s)",
                 scalar ? "scalar" : "vector", all ? "all bodies" : "selected bodies");
        check_test_expect(test, bytes, description);
        arena_release(&arena);
        unlink(object);
    }
    arena_release(&reference_arena);
    unlink(reference);
}

static void check_test_driver_string_pool(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_selected_initializer_pool(test, clang, executable, directory);
    static const char program[] =
        "int printf(const char*, ...);\n"
        "static const char *dead(void) { return \"survivor\"; }\n"
        "const char *first = \"pool\", *second = \"po\" \"ol\", *offset = \"pool\" + 2;\n"
        "const char *embedded = \"a\\0b\", *interior = \"a\\0b\" + 2;\n"
        "static const char named_first[] = \"pool\", named_second[] = \"pool\";\n"
        "static const char *live(void) { return \"survivor\"; }\n"
        "static const char *other_live(void) { return \"sur\" \"vivor\"; }\n"
        "static const char *name(void) { return __func__; }\n"
        "int main(int argc, char **argv)\n"
        "{\n"
        "    (void)argv; int errors = 0; int index = argc + 1;\n"
        "    const char *p = \"pool\", *q = \"\\160ool\";\n"
        "    errors += p != q; errors += p != first; errors += first != second;\n"
        "    errors += offset != p + 2; errors += offset[0] != 'o';\n"
        "    errors += (\"pool\" == \"pool\") != 1; errors += (\"a\" \"b\" == \"ab\") != 1;\n"
        "    errors += (\"\" == \"\") != 1; errors += (u8\"pool\" == \"pool\") != 1;\n"
        "    errors += named_first == named_second; errors += p == named_first; errors += p == named_second;\n"
        "    errors += named_first[index] != 'o'; errors += named_second[index] != 'o';\n"
        "    errors += live() != other_live(); errors += live() != \"survivor\";\n"
        "    const char *a = \"a\\0b\", *b = \"a\\0c\", *shorter = \"a\";\n"
        "    errors += a != embedded; errors += a == b; errors += a == shorter;\n"
        "    errors += a[index] != 'b'; errors += b[index] != 'c'; errors += a[3] != 0;\n"
        "    errors += interior != a + 2; errors += interior[0] != 'b';\n"
        "    const char *long_a = \"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-0123456789abcdefghijklmnopqrstuvwxyz\";\n"
        "    const char *long_b = \"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-\" \"0123456789abcdefghijklmnopqrstuvwxyz\";\n"
        "    errors += long_a != long_b; errors += long_a[65] != '2';\n"
        "    const unsigned short *u = u\"\\u1234x\", *v = u\"\\u1234\" u\"x\";\n"
        "    const unsigned int *x = U\"\\U0001f642x\", *y = U\"\\U0001f642x\";\n"
        "    const int *w = L\"\\u1234x\", *z = L\"\\u1234x\";\n"
        "    errors += u != v; errors += x != y; errors += w != z;\n"
        "    errors += u[0] != 0x1234; errors += x[0] != 0x1f642; errors += w[0] != 0x1234;\n"
        "    errors += u[1] != 'x' || x[1] != 'x' || w[1] != 'x';\n"
        "    errors += u[index] != 0 || x[index] != 0 || w[index] != 0;\n"
        "    errors += name()[0] != 'n' || name()[3] != 'e' || name()[4] != 0;\n"
        "    printf(\"%d\\n\", errors); return errors != 0;\n"
        "}\n";
    char source[256];
    char object[256];
    char output[256];
    char reference[256];
    snprintf(source, sizeof(source), "%s/string-pool.c", directory);
    snprintf(object, sizeof(object), "%s/string-pool.o", directory);
    snprintf(output, sizeof(output), "%s/string-pool", directory);
    snprintf(reference, sizeof(reference), "%s/string-pool-clang", directory);
    const bool written = parse_test_write(source, program, sizeof(program) - 1);
    check_test_expect(test, written, "literal pooling driver source written");
    char* text = NULL;
    char* reference_arguments[] = { (char*)clang, "-std=gnu17", "-O0", "-w", "-o", reference, source, NULL };
    const int referenced = written ? check_test_driver_spawn(reference_arguments, &text) : 1;
    if (referenced != 0 && text != NULL)
    {
        fprintf(stderr, "%s", text);
    }
    free(text);
    char expected[128] = "";
    check_test_expect(test, referenced == 0 && check_test_driver_capture(reference, expected, sizeof(expected)) && strcmp(expected, "0\n") == 0,
                      "Clang's literal identities, array contents and static pointer offsets match the independent expectation");
    for (uint32_t mode = 0; written && mode < 8; mode += 1)
    {
        const bool scalar = (mode & 1) != 0;
        const bool tier1 = (mode & 2) != 0;
        const bool relocatable = (mode & 4) != 0;
        char* arguments[16] = { (char*)executable };
        uint32_t count = 1;
        if (scalar)
        {
            arguments[count++] = "--scalar";
        }
        if (tier1)
        {
            arguments[count++] = "--tier1";
            arguments[count++] = "--verify";
        }
        if (relocatable)
        {
            arguments[count++] = "-c";
        }
        arguments[count++] = "-std=gnu17";
        arguments[count++] = "-w";
        arguments[count++] = "-o";
        arguments[count++] = relocatable ? object : output;
        arguments[count++] = source;
        arguments[count] = NULL;
        text = NULL;
        int status = check_test_driver_spawn(arguments, &text);
        if (status != 0 && text != NULL)
        {
            fprintf(stderr, "%s", text);
        }
        free(text);
        if (status == 0 && relocatable)
        {
            char* link_arguments[] = { (char*)clang, "-o", output, object, NULL };
            text = NULL;
            status = check_test_driver_spawn(link_arguments, &text);
            if (status != 0 && text != NULL)
            {
                fprintf(stderr, "%s", text);
            }
            free(text);
        }
        char captured[128] = "";
        const bool ran = status == 0 && check_test_driver_capture(output, captured, sizeof(captured));
        char description[256];
        snprintf(description, sizeof(description), "literal pooling preserves identities, full bytes and relocations (%s, Tier %u, %s)",
                 scalar ? "scalar" : "vector", tier1 ? 1 : 0, relocatable ? "ELF/system linker" : "direct linker");
        check_test_expect(test, ran && strcmp(captured, "0\n") == 0 && strcmp(captured, expected) == 0, description);
        if (status == 0 && (!ran || strcmp(captured, "0\n") != 0))
        {
            fprintf(stderr, "%s: %s", description, captured);
        }
        unlink(output);
        unlink(object);
    }
    unlink(reference);
    unlink(source);
}
