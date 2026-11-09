// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// What inline assembly defines in -c's objects and the direct path's executables (#1848), on original fixtures, against
// clang 23's: a template's `.comm` (#1265's COMMON symbol), and a template's label or `.comm` that defines a symbol the
// unit declares without defining, which C's references then bind to. Each fixture's -c object, by both checker paths
// (the same bytes), has clang's symbols: each named symbol's type, binding, visibility, section (or UND, COM, ABS), size
// and a common symbol's alignment, compared as sorted lines (check_test_asm_definition_symbols); and the relocations
// of its sections other than code, unwind tables and debug information, which the compilers' own code makes. Both
// objects, linked by clang, and the direct path's executables of both paths print what clang's build prints. Include
// after the driver test's ELF reader (CheckTestElf).

// Appends `text` to `lines` (`*used` of `capacity`): false when it doesn't fit.
static bool check_test_asm_definition_append(char* lines, uint64_t capacity, uint64_t* used, const char* text)
{
    const uint64_t length = strlen(text);
    const bool fits = length < capacity - *used;
    if (fits)
    {
        memcpy(lines + *used, text, length + 1);
        *used += length;
    }
    return fits;
}

static int check_test_asm_definition_order(const void* a, const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

// `elf`'s symbol `index` as a relocation names it: its name, or its section's for a section symbol.
static const char* check_test_asm_definition_target(const CheckTestElf* elf, uint64_t index)
{
    const Elf64_Sym* symbol = index < elf->symbol_count ? &elf->symbols[index] : 0;
    const char* name = symbol != 0 ? check_test_elf_string(elf->symbol_names, elf->symbol_names_size, symbol->st_name) : 0;
    return symbol == 0 ? "?" : ELF64_ST_TYPE(symbol->st_info) == STT_SECTION ? check_test_elf_section_name(elf, symbol->st_shndx) : name != 0 ? name : "?";
}

// Whether `name` is one of `names`, words separated by spaces.
static bool check_test_asm_definition_named(const char* names, const char* name)
{
    const size_t length = strlen(name);
    bool found = false;
    for (const char* at = names; !found && at != 0 && *at != 0; at = strchr(at, ' ') != 0 ? strchr(at, ' ') + 1 : 0)
    {
        found = strncmp(at, name, length) == 0 && (at[length] == ' ' || at[length] == 0);
    }
    return found;
}

// The object at `path` as the comparison reads it, into `lines`: a line per named symbol but files', sections' and the
// assembler's temporary `.L` labels, and a line per relocation of a section other than code, unwind tables and debug
// information, sorted. The sizes of the C functions `functions` names (words separated by spaces) are left out: their
// code is each compiler's own. False when it doesn't read or fit.
static bool check_test_asm_definition_symbols(const char* path, const char* functions, char* lines, uint64_t capacity)
{
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    CheckTestElf elf = { 0 };
    bool valid = arena.base != 0 && capacity != 0 && check_test_elf_open(&elf, object_test_read_file(&arena, path));
    char* unsorted = valid ? BUSTER_ARENA_PUSH_ARRAY(&arena, char, capacity) : 0;
    const char** order = valid ? BUSTER_ARENA_PUSH_ARRAY(&arena, const char*, 1024) : 0;
    valid = valid && unsorted != 0 && order != 0;
    uint64_t used = 0;
    uint32_t count = 0;
    for (uint64_t i = 1; valid && i < elf.symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &elf.symbols[i];
        const uint8_t type = ELF64_ST_TYPE(symbol->st_info);
        const char* name = check_test_elf_string(elf.symbol_names, elf.symbol_names_size, symbol->st_name);
        if (type == STT_FILE || type == STT_SECTION || name == 0 || name[0] == 0 || strncmp(name, ".L", 2) == 0)
        {
            continue;
        }
        const char* section = symbol->st_shndx == SHN_UNDEF ? "UND" : symbol->st_shndx == SHN_COMMON ? "COM" : symbol->st_shndx == SHN_ABS ? "ABS" :
                                                                                                            check_test_elf_section_name(&elf, symbol->st_shndx);
        char line[512];
        char size[64] = { 0 };
        if (!check_test_asm_definition_named(functions, name))
        {
            snprintf(size, sizeof(size), " size %llu", (unsigned long long)symbol->st_size);
        }
        if (symbol->st_shndx == SHN_COMMON)
        {
            snprintf(size + strlen(size), sizeof(size) - strlen(size), " aligned %llu", (unsigned long long)symbol->st_value);
        }
        snprintf(line, sizeof(line), "%s type %u bind %u visibility %u in %s%s\n", name, type, ELF64_ST_BIND(symbol->st_info),
                 ELF64_ST_VISIBILITY(symbol->st_other), section != 0 ? section : "?", size);
        order[count] = unsorted + used;
        valid = count < 1024 && check_test_asm_definition_append(unsorted, capacity, &used, line);
        used += valid;
        count += valid;
    }
    for (uint32_t s = 1; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* table = &elf.sections[s];
        const char* target = table->sh_type == SHT_RELA ? check_test_elf_section_name(&elf, table->sh_info) : 0;
        const bool compared = target != 0 && strncmp(target, ".text", 5) != 0 && strcmp(target, ".eh_frame") != 0 && strncmp(target, ".debug", 6) != 0 &&
                              strcmp(target, ".llvm_addrsig") != 0;
        valid = !compared || (table->sh_entsize == sizeof(Elf64_Rela) && check_test_elf_range(elf.file, table->sh_offset, table->sh_size));
        const Elf64_Rela* relocations = compared && valid ? (const Elf64_Rela*)(elf.file.pointer + table->sh_offset) : 0;
        for (uint64_t r = 0; valid && relocations != 0 && r < table->sh_size / sizeof(Elf64_Rela); r += 1)
        {
            char line[512];
            snprintf(line, sizeof(line), "%s+%llu relocation %u of %s%+lld\n", target, (unsigned long long)relocations[r].r_offset,
                     (unsigned)ELF64_R_TYPE(relocations[r].r_info), check_test_asm_definition_target(&elf, ELF64_R_SYM(relocations[r].r_info)),
                     (long long)relocations[r].r_addend);
            order[count] = unsorted + used;
            valid = count < 1024 && check_test_asm_definition_append(unsorted, capacity, &used, line);
            used += valid;
            count += valid;
        }
    }
    uint64_t at = 0;
    if (valid)
    {
        // Each line was stored with its terminator, which the sorted copy joins without.
        qsort(order, count, sizeof(order[0]), check_test_asm_definition_order);
        for (uint32_t k = 0; k < count; k += 1)
        {
            const uint64_t length = strlen(order[k]);
            memcpy(lines + at, order[k], length);
            at += length;
        }
    }
    if (capacity != 0)
    {
        lines[at] = 0;
    }
    arena_release(&arena);
    return valid;
}

static void check_test_driver_asm_definitions(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        // The functions C defines, whose sizes the comparison leaves out.
        const char* functions;
        const char* source;
    } fixtures[] = {
        // A template's `.comm`s: one the unit declares and C reads and writes, one of the template's alone, and one C
        // declares that a later template defines.
        { "a template's common symbols", "declare later main",
          "#include <stdio.h>\n"
          "extern long counter;\n"
          "extern int pair[2];\n"
          "static void declare(void) { __asm__ volatile(\".comm counter,8,8\\n.comm private_common,24,16\"); }\n"
          "static void later(void) { __asm__ volatile(\".comm pair,8,4\"); }\n"
          "int main(void) { declare(); later(); counter += 41; pair[1] = 2; printf(\"%ld %d %d\\n\", counter, pair[0], pair[1]); return 0; }\n" },
        // A template's label defines a function the unit declares, called from C; `.globl` and `.type` make it
        // clang's global function, and another local label stays local.
        { "a template's label defines a declared function", "jump main",
          "#include <stdio.h>\n"
          "int answer(void);\n"
          "int local_answer(void);\n"
          "static int jump(int n) {\n"
          "    __asm__ volatile(\"jmp 1f\\n.globl answer\\n.type answer,@function\\nanswer: movl $42, %eax\\nret\\n.size answer,.-answer\\n"
          "local_answer: movl $7, %eax\\nret\\n1:\");\n"
          "    return n;\n"
          "}\n"
          "int main(void) { printf(\"%d %d %d\\n\", answer(), local_answer(), jump(3)); return 0; }\n" },
        // A template's data label defines a declared variable, in the template's own section.
        { "a template's data label defines a declared variable", "data main",
          "#include <stdio.h>\n"
          "extern const char greeting[];\n"
          "extern int table[3];\n"
          "static void data(void) {\n"
          "    __asm__(\".pushsection .rodata.greeting,\\\"a\\\",@progbits\\n.globl greeting\\n.type greeting,@object\\ngreeting: .asciz \\\"hello\\\"\\n"
          ".size greeting, 6\\n.popsection\");\n"
          "    __asm__(\".pushsection .data\\n.p2align 2\\ntable: .long 1, 2, 3\\n.popsection\");\n"
          "}\n"
          "int main(void) { data(); printf(\"%s %d\\n\", greeting, table[0] + table[1] + table[2]); return 0; }\n" },
        // File-scope assembly (#619) that defines functions and data C declares and calls, refers to C's function and
        // variables (a static one that C reads too, so that clang emits it), with local, `.L` and numeric labels,
        // a `.pushsection` string, a common symbol and a weak reference.
        { "file-scope assembly's symbols", "bump main",
          "#include <stdio.h>\n"
          "int counter = 5;\n"
          "static int hidden = 9;\n"
          "int bump(int x) { return x + 1; }\n"
          "int twice_counter(void);\n"
          "int plus_hidden(int);\n"
          "extern long table[];\n"
          "extern char shared_buffer[64];\n"
          "extern const char* message(void);\n"
          "extern int maybe;\n"
          "__asm__(\".text\\n.globl twice_counter\\n.type twice_counter,@function\\ntwice_counter:\\n\\tmovl counter(%rip), %eax\\n\\taddl %eax, %eax\\n\\tret\\n"
          ".size twice_counter, .-twice_counter\");\n"
          "__asm__(\"plus_hidden:\\n\\tmovl hidden(%rip), %eax\\n\\ttestl %edi, %edi\\n\\tjz 1f\\n\\taddl %edi, %eax\\n1:\\tret\");\n"
          "__asm__(\".data\\n.globl table\\n.p2align 3\\ntable: .quad bump\\n.quad .Lafter\\n.Lafter: .long 7\\n.text\");\n"
          "__asm__(\".comm shared_buffer,64,32\\n.weak maybe\");\n"
          "__asm__(\".globl message\\nmessage:\\n\\tleaq .Lmessage(%rip), %rax\\n\\tret\\n.pushsection .rodata\\n.Lmessage: .asciz \\\"file\\\"\\n.popsection\");\n"
          "int main(void) {\n"
          "    int* volatile weak = &maybe;\n"
          "    shared_buffer[3] = 4;\n"
          "    printf(\"%d %d %d %d %d %s %d %d\\n\", twice_counter(), plus_hidden(2), plus_hidden(0) + hidden, ((int (*)(int))table[0])(4), *(int*)table[1], message(),\n"
          "           shared_buffer[3], weak != 0 ? *weak : -1);\n"
          "    return 0;\n"
          "}\n" },
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        char path[512];
        char objects[3][512];
        char programs[3][512];
        char direct[2][512];
        snprintf(path, sizeof(path), "%s/asm-definition-%u.c", directory, f);
        const bool written = parse_test_write(path, fixtures[f].source, strlen(fixtures[f].source));
        char description[8192];
        snprintf(description, sizeof(description), "%s: the fixture is written", fixtures[f].name);
        check_test_expect(test, written, description);
        char captured[3][256] = { { 0 } };
        static char listed[3][4096];
        for (uint32_t p = 0; written && p < 3; p += 1)
        {
            listed[p][0] = 0;
            snprintf(objects[p], sizeof(objects[p]), "%s/asm-definition-%u-%u.o", directory, f, p);
            snprintf(programs[p], sizeof(programs[p]), "%s/asm-definition-%u-%u", directory, f, p);
            char* text = NULL;
            char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", objects[p], p == 2 ? "--scalar" : NULL, NULL };
            const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
            snprintf(description, sizeof(description), "%s: -c by %s: %s", fixtures[f].name, p == 0 ? "clang" : p == 1 ? "the vector path" : "the scalar path",
                     text != NULL ? text : "");
            free(text);
            text = NULL;
            check_test_expect(test, compiled, description);
            const bool read = compiled && check_test_asm_definition_symbols(objects[p], fixtures[f].functions, listed[p], sizeof(listed[p]));
            snprintf(description, sizeof(description), "%s: the %s object's symbols read", fixtures[f].name, p == 0 ? "clang" : p == 1 ? "vector" : "scalar");
            check_test_expect(test, read, description);
            char* const link[] = { (char*)clang, objects[p], "-o", programs[p], NULL };
            const bool linked = compiled && check_test_driver_spawn(link, &text) == 0;
            free(text);
            snprintf(description, sizeof(description), "%s: the %s object links and runs", fixtures[f].name, p == 0 ? "clang" : p == 1 ? "vector" : "scalar");
            check_test_expect(test, linked && check_test_driver_capture(programs[p], captured[p], sizeof(captured[p])), description);
        }
        for (uint32_t p = 1; written && p < 3; p += 1)
        {
            snprintf(description, sizeof(description), "%s: the %s object's symbols and data relocations are clang's:\n%s\nagainst clang's\n%s", fixtures[f].name,
                     p == 1 ? "vector" : "scalar", listed[p], listed[0]);
            check_test_expect(test, strcmp(listed[p], listed[0]) == 0, description);
            snprintf(description, sizeof(description), "%s: the %s object runs as clang's: %s against %s", fixtures[f].name, p == 1 ? "vector" : "scalar",
                     captured[p], captured[0]);
            check_test_expect(test, captured[0][0] != 0 && strcmp(captured[p], captured[0]) == 0, description);
        }
        snprintf(description, sizeof(description), "%s: the scalar and vector objects match byte for byte", fixtures[f].name);
        check_test_expect(test, written && check_test_driver_same_files(objects[1], objects[2]), description);
        for (uint32_t p = 0; written && p < 2; p += 1)
        {
            snprintf(direct[p], sizeof(direct[p]), "%s/asm-definition-%u-direct-%u", directory, f, p);
            char* text = NULL;
            char* arguments[] = { (char*)executable, "-std=gnu17", "-O0", path, "-o", direct[p], p == 1 ? "--scalar" : NULL, NULL };
            const bool built = check_test_driver_spawn(arguments, &text) == 0;
            free(text);
            char output[256] = { 0 };
            const bool ran = built && check_test_driver_capture(direct[p], output, sizeof(output));
            snprintf(description, sizeof(description), "%s: the direct path's %s executable runs as clang's build: %s against %s", fixtures[f].name,
                     p == 0 ? "vector" : "scalar", output, captured[0]);
            check_test_expect(test, ran && strcmp(output, captured[0]) == 0, description);
        }
        snprintf(description, sizeof(description), "%s: the direct path's scalar and vector executables match byte for byte", fixtures[f].name);
        check_test_expect(test, written && check_test_driver_same_files(direct[0], direct[1]), description);
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
    // A template's `.comm` of a name the unit defines, a tentative definition's too, stays refused by name, whatever
    // -fcommon: clang 23 has no coherent object for it. Without -fcommon its object makes C's .bss definition a common of
    // C's size at the template's alignment (an initializer's bytes are lost); with -fcommon a `.comm` of C's size and
    // alignment is the one common, and another size or alignment is a backend crash ("x redeclared as different type").
    static const char* const common_definitions[] = {
        "int x; void declare(void) { __asm__(\".comm x,4,4\"); } int main(void) { declare(); x = 42; return x != 42; }\n",
        "int x; void declare(void) { __asm__(\".comm x,16,16\"); } int main(void) { declare(); x = 42; return x != 42; }\n",
        "int x = 1; void declare(void) { __asm__(\".comm x,4,4\"); } int main(void) { declare(); return x != 1; }\n",
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(common_definitions); f += 1)
    {
        char path[512];
        char output[512];
        snprintf(path, sizeof(path), "%s/asm-common-definition-%u.c", directory, f);
        snprintf(output, sizeof(output), "%s/asm-common-definition-%u.o", directory, f);
        const bool written = parse_test_write(path, common_definitions[f], strlen(common_definitions[f]));
        for (uint32_t route = 0; written && route < 8; route += 1)
        {
            // Both paths, -c and the direct path, with and without -fcommon.
            char* text = NULL;
            char* arguments[] = { (char*)executable, "-std=gnu17", (route & 4) != 0 ? "-fcommon" : "-fno-common", path, "-o", output, (route & 2) != 0 ? "-c" : NULL,
                                  NULL, NULL };
            arguments[(route & 2) != 0 ? 7 : 6] = (route & 1) != 0 ? "--scalar" : NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            char description[1024];
            snprintf(description, sizeof(description), "a template's .comm of the unit's definition %u is refused by name (%s, %s, %s): %s", f,
                     (route & 1) != 0 ? "scalar" : "vector", (route & 2) != 0 ? "-c" : "direct", (route & 4) != 0 ? "-fcommon" : "-fno-common", text != NULL ? text : "");
            check_test_expect(test, status == 1 && text != NULL && strstr(text, "unsupported: inline assembly labels named as the unit's symbols") != NULL,
                              description);
            free(text);
        }
        unlink(path);
        unlink(output);
    }
}
