// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// What inline assembly defines in -c's objects and the direct path's executables (#1848), on original fixtures, against
// clang 23's: a template's `.comm` (#1265's COMMON symbol), a template's label or `.comm` that defines a symbol the
// unit declares without defining, which C's references then bind to, and a `.comm` of a variable the unit defines, which
// makes it a common of C's size. Each fixture's -c object, by both checker paths
// (the same bytes), has clang's symbols: each named symbol's type, binding, visibility, section (or UND, COM, ABS), size
// and a common symbol's alignment, compared as sorted lines (check_test_asm_definition_symbols); and the relocations
// of its sections other than code, unwind tables and debug information, which the compilers' own code makes. Both
// objects, linked by clang, and the direct path's executables of both paths print what clang's build prints, and the
// direct path's has the program's own note sections, PT_NOTEs and notes that clang's build has. Include
// after the driver test's ELF reader (CheckTestElf). Asm labels that name the unit's own objects (#1979) are checked here
// too: they make one symbol of a name, which the comparison reads the same way. File-scope COMDAT groups (#1848) compare
// their `.group` sections too, and link across two units (check_test_driver_asm_comdat_units).

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
// assembler's temporary `.L` labels, a line per relocation of a section other than code, unwind tables and debug
// information, and a line per section group, sorted. The sizes of the C functions `functions` names (words separated by spaces) are left out: their
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
    // Each section group (#1848): its signature (a symbol's name, or a section's), its flags and its members' names in
    // their order.
    for (uint32_t s = 1; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* table = &elf.sections[s];
        if (table->sh_type != SHT_GROUP)
        {
            continue;
        }
        valid = table->sh_size >= sizeof(uint32_t) && table->sh_size % sizeof(uint32_t) == 0 && check_test_elf_range(elf.file, table->sh_offset, table->sh_size);
        const uint32_t* words = valid ? (const uint32_t*)(elf.file.pointer + table->sh_offset) : 0;
        char line[512];
        int length = valid ? snprintf(line, sizeof(line), "group %s flags %u:", check_test_asm_definition_target(&elf, table->sh_info), words[0]) : 0;
        for (uint64_t w = 1; valid && w < table->sh_size / sizeof(uint32_t) && length > 0 && (size_t)length < sizeof(line); w += 1)
        {
            const char* member = check_test_elf_section_name(&elf, words[w]);
            length += snprintf(line + length, sizeof(line) - (size_t)length, " %s", member != 0 ? member : "?");
        }
        valid = valid && length > 0 && (size_t)length + 1 < sizeof(line);
        if (valid)
        {
            line[length] = '\n';
            line[length + 1] = 0;
        }
        order[count] = unsorted + used;
        valid = valid && count < 1024 && check_test_asm_definition_append(unsorted, capacity, &used, line);
        used += valid;
        count += valid;
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

// Whether the object at `path` has a label named `.L...` (a symbol not a function's) defined in a code section, which an
// assembler never keeps (#1845), its name in `found`; false when it has none or doesn't read.
static bool check_test_asm_definition_code_temporary(const char* path, char* found, uint64_t capacity)
{
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    CheckTestElf elf = { 0 };
    bool kept = false;
    if (arena.base != 0 && check_test_elf_open(&elf, object_test_read_file(&arena, path)))
    {
        for (uint64_t i = 1; !kept && i < elf.symbol_count; i += 1)
        {
            const Elf64_Sym* symbol = &elf.symbols[i];
            const char* name = check_test_elf_string(elf.symbol_names, elf.symbol_names_size, symbol->st_name);
            const char* section = symbol->st_shndx != SHN_UNDEF && symbol->st_shndx < SHN_LORESERVE ? check_test_elf_section_name(&elf, symbol->st_shndx) : 0;
            kept = name != 0 && strncmp(name, ".L", 2) == 0 && ELF64_ST_TYPE(symbol->st_info) != STT_FUNC && section != 0 && strncmp(section, ".text", 5) == 0;
            if (kept)
            {
                snprintf(found, capacity, "%s", name);
            }
        }
    }
    if (arena.base != 0)
    {
        arena_release(&arena);
    }
    return kept;
}

// Whether a note section is the toolchain's (the C runtime's ABI tag, the linker's build ID, GNU properties), not the
// program's own.
static bool check_test_asm_definition_toolchain_note(const char* name)
{
    return strcmp(name, ".note.ABI-tag") == 0 || strncmp(name, ".note.gnu.", 10) == 0;
}

// The program's own notes in the executable at `path` (#1848), as readelf -lSn reads them, into `lines`: a line per
// SHT_NOTE section but the toolchain's (check_test_asm_definition_toolchain_note), with its type, flags, alignment and
// whether a PT_NOTE of its alignment covers it whole; then a line per note in those sections as the PT_NOTE segments
// parse them, each padded to its segment's alignment as readelf pads it (8 for an alignment of 8, else 4): its owner,
// type and description bytes in hex, in file order. False when it doesn't read or the lines don't fit.
static bool check_test_asm_definition_notes(const char* path, char* lines, uint64_t capacity)
{
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    CheckTestElf elf = { 0 };
    bool valid = arena.base != 0 && capacity != 0 && check_test_elf_open(&elf, object_test_read_file(&arena, path));
    const Elf64_Ehdr* header = valid ? (const Elf64_Ehdr*)elf.file.pointer : 0;
    valid = valid && header->e_phentsize == sizeof(Elf64_Phdr) && check_test_elf_range(elf.file, header->e_phoff, (uint64_t)header->e_phnum * sizeof(Elf64_Phdr));
    const Elf64_Phdr* segments = valid ? (const Elf64_Phdr*)(elf.file.pointer + header->e_phoff) : 0;
    const uint32_t segment_count = valid ? header->e_phnum : 0;
    uint64_t used = 0;
    if (capacity != 0)
    {
        lines[0] = 0;
    }
    for (uint32_t s = 1; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* section = &elf.sections[s];
        const char* name = check_test_elf_section_name(&elf, s);
        if (section->sh_type == SHT_NOTE && name != 0 && !check_test_asm_definition_toolchain_note(name))
        {
            bool covered = false;
            for (uint32_t p = 0; p < segment_count; p += 1)
            {
                const Elf64_Phdr* segment = &segments[p];
                covered = covered || (segment->p_type == PT_NOTE && segment->p_align == section->sh_addralign && segment->p_offset <= section->sh_offset &&
                                      section->sh_offset + section->sh_size <= segment->p_offset + segment->p_filesz);
            }
            char line[512];
            snprintf(line, sizeof(line), "section %s type %u flags %llu align %llu%s\n", name, section->sh_type, (unsigned long long)section->sh_flags,
                     (unsigned long long)section->sh_addralign, covered ? " in a PT_NOTE" : " in no PT_NOTE");
            valid = check_test_asm_definition_append(lines, capacity, &used, line);
        }
    }
    for (uint32_t p = 0; valid && p < segment_count; p += 1)
    {
        const Elf64_Phdr* segment = &segments[p];
        const uint64_t padding = segment->p_align == 8 ? 8 : 4;
        uint64_t at = segment->p_offset;
        const uint64_t end = segment->p_offset + segment->p_filesz;
        valid = segment->p_type != PT_NOTE || check_test_elf_range(elf.file, segment->p_offset, segment->p_filesz);
        while (valid && segment->p_type == PT_NOTE && end - at >= sizeof(Elf64_Nhdr))
        {
            Elf64_Nhdr note;
            memcpy(&note, elf.file.pointer + at, sizeof(note));
            const uint64_t name_at = at + sizeof(note);
            // The description and the next note each start at the padding's multiple from the note's start.
            const uint64_t description_at = at + ((sizeof(note) + note.n_namesz + padding - 1) & ~(padding - 1));
            const uint64_t next = at + ((description_at - at + note.n_descsz + padding - 1) & ~(padding - 1));
            valid = next <= end && note.n_namesz != 0 && elf.file.pointer[name_at + note.n_namesz - 1] == 0;
            // The section the note is in: one of the program's own, or the toolchain's, which the lines leave out.
            const char* owner = 0;
            for (uint32_t s = 1; valid && s < elf.section_count; s += 1)
            {
                const Elf64_Shdr* section = &elf.sections[s];
                const char* name = check_test_elf_section_name(&elf, s);
                const bool inside = section->sh_type == SHT_NOTE && section->sh_offset <= at && at < section->sh_offset + section->sh_size;
                owner = inside && name != 0 && !check_test_asm_definition_toolchain_note(name) ? name : owner;
            }
            if (valid && owner != 0)
            {
                char line[512];
                int length = snprintf(line, sizeof(line), "note in %s owner %s type %u description", owner, elf.file.pointer + name_at, note.n_type);
                for (uint32_t b = 0; b < note.n_descsz && length > 0 && (uint64_t)length + 4 < sizeof(line); b += 1)
                {
                    length += snprintf(line + length, sizeof(line) - (uint64_t)length, " %02x", (uint8_t)elf.file.pointer[description_at + b]);
                }
                valid = length > 0 && (uint64_t)length + 2 < sizeof(line);
                if (valid)
                {
                    line[length] = '\n';
                    line[length + 1] = 0;
                    valid = check_test_asm_definition_append(lines, capacity, &used, line);
                }
            }
            at = next;
        }
    }
    if (arena.base != 0)
    {
        arena_release(&arena);
    }
    return valid;
}

// Two units that define the same file-scope COMDAT groups (#1848), a variable's and a function's code and data, of
// different contents, so that the program prints which unit's group the link kept: the first in the command's order, as
// a linker keeps it. Each unit's -c object by both paths, linked by clang in both orders, runs as clang's objects do,
// and so does the direct path's executable of both sources, on both paths and on four lanes. A group whose signature is
// no global symbol it defines (`other_group`'s) is refused by name in a link of several units, which selects groups
// by their signatures' names.
static void check_test_driver_asm_comdat_units(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const sources[3] = {
        "#include <stdio.h>\n"
        "extern int shared_value;\n"
        "int f(void);\n"
        "int other(void);\n"
        "__asm__(\".section .data.shared_value,\\\"awG\\\",@progbits,shared_value,comdat\\n.globl shared_value\\n.weak shared_value\\nshared_value: .long 42\\n.text\");\n"
        "__asm__(\".section .text.f,\\\"axG\\\",@progbits,f,comdat\\n.weak f\\n.type f,@function\\nf: movl f_data(%rip), %eax\\n\\tret\\n.size f, .-f\\n"
        ".section .data.f_data,\\\"awG\\\",@progbits,f,comdat\\n.weak f_data\\nf_data: .long 5\\n.text\");\n"
        "int main(void) { printf(\"%d %d %d\\n\", shared_value, f(), other()); return 0; }\n",
        "extern int shared_value;\n"
        "int f(void);\n"
        "__asm__(\".section .data.shared_value,\\\"awG\\\",@progbits,shared_value,comdat\\n.globl shared_value\\n.weak shared_value\\nshared_value: .long 43\\n.text\");\n"
        "__asm__(\".section .text.f,\\\"axG\\\",@progbits,f,comdat\\n.weak f\\n.type f,@function\\nf: movl f_data(%rip), %eax\\n\\taddl $100, %eax\\n\\tret\\n"
        ".size f, .-f\\n.section .data.f_data,\\\"awG\\\",@progbits,f,comdat\\n.weak f_data\\nf_data: .long 6\\n.text\");\n"
        "int other(void) { return shared_value + f(); }\n",
        "extern int other_value;\n"
        "__asm__(\".section .data.other_value,\\\"awG\\\",@progbits,other_group,comdat\\n.globl other_value\\n.weak other_value\\nother_value: .long 7\\n.text\");\n"
        "int other(void) { return other_value; }\n",
    };
    char paths[3][512];
    bool written = true;
    for (uint32_t u = 0; u < 3; u += 1)
    {
        snprintf(paths[u], sizeof(paths[u]), "%s/asm-comdat-unit-%u.c", directory, u);
        written = written && parse_test_write(paths[u], sources[u], strlen(sources[u]));
    }
    check_test_expect(test, written, "COMDAT units: the fixtures are written");
    // -c's objects: clang's, the vector path's and the scalar path's, of the first two units.
    char objects[3][2][512];
    bool compiled = written;
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        for (uint32_t u = 0; u < 2; u += 1)
        {
            snprintf(objects[p][u], sizeof(objects[p][u]), "%s/asm-comdat-unit-%u-%u.o", directory, u, p);
            char* text = NULL;
            char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-w", "-c", paths[u], "-o", objects[p][u], p == 2 ? "--scalar" : NULL, NULL };
            const bool built = check_test_driver_spawn(arguments, &text) == 0;
            char description[2048];
            snprintf(description, sizeof(description), "COMDAT units: unit %u's -c by %s: %s", u, p == 0 ? "clang" : p == 1 ? "the vector path" : "the scalar path",
                     text != NULL ? text : "");
            free(text);
            check_test_expect(test, built, description);
            compiled = compiled && built;
        }
    }
    char program[512];
    snprintf(program, sizeof(program), "%s/asm-comdat-units", directory);
    for (uint32_t order = 0; compiled && order < 2; order += 1)
    {
        // clang's objects linked in this order are the reference.
        char expected[256] = { 0 };
        for (uint32_t p = 0; p < 3; p += 1)
        {
            char* text = NULL;
            char* const link[] = { (char*)clang, objects[p][order], objects[p][1 - order], "-o", program, NULL };
            const bool linked = check_test_driver_spawn(link, &text) == 0;
            free(text);
            char output[256] = { 0 };
            const bool ran = linked && check_test_driver_capture(program, output, sizeof(output));
            if (p == 0)
            {
                memcpy(expected, output, sizeof(expected));
            }
            char description[1024];
            snprintf(description, sizeof(description), "COMDAT units: the %s objects linked by clang in order %u run as clang's: %s against %s",
                     p == 0 ? "clang" : p == 1 ? "vector" : "scalar", order, output, expected);
            check_test_expect(test, ran && expected[0] != 0 && strcmp(output, expected) == 0, description);
            unlink(program);
        }
        for (uint32_t route = 0; expected[0] != 0 && route < 3; route += 1)
        {
            char* text = NULL;
            char* arguments[] = { (char*)executable, "-std=gnu17", "-w", paths[order], paths[1 - order], "-o", program, route == 1 ? "--scalar" : route == 2 ? "--lanes" : NULL,
                                  route == 2 ? "4" : NULL, NULL };
            const bool built = check_test_driver_spawn(arguments, &text) == 0;
            char output[256] = { 0 };
            const bool ran = built && check_test_driver_capture(program, output, sizeof(output));
            char description[2048];
            snprintf(description, sizeof(description), "COMDAT units: the direct path's %s executable of both sources in order %u runs as clang's build: %s against %s%s",
                     route == 0 ? "vector" : route == 1 ? "scalar" : "four lanes'", order, output, expected, text != NULL ? text : "");
            free(text);
            check_test_expect(test, ran && strcmp(output, expected) == 0, description);
            unlink(program);
        }
    }
    for (uint32_t route = 0; written && route < 2; route += 1)
    {
        char* text = NULL;
        char* arguments[] = { (char*)executable, "-std=gnu17", "-w", paths[0], paths[2], "-o", program, route == 1 ? "--scalar" : NULL, NULL };
        const int status = check_test_driver_spawn(arguments, &text);
        char description[2048];
        snprintf(description, sizeof(description), "COMDAT units: a group of a signature of its own is refused by name in a link of several units (%s): %s",
                 route == 1 ? "scalar" : "vector", text != NULL ? text : "");
        check_test_expect(test, status == 1 && text != NULL &&
                                    strstr(text, "unsupported: file-scope assembly's section groups whose signature isn't a global symbol defined in the group") != NULL,
                          description);
        free(text);
        unlink(program);
    }
    for (uint32_t p = 0; written && p < 3; p += 1)
    {
        for (uint32_t u = 0; u < 2; u += 1)
        {
            unlink(objects[p][u]);
        }
    }
    for (uint32_t u = 0; u < 3; u += 1)
    {
        unlink(paths[u]);
    }
}

// The code of the functions `names` names (words separated by spaces) in the object at `path`, into `lines`: each one's
// size and bytes, and its code's relocations by offset in it, type, target and addend (#1845: naked functions, whose
// code is the user's, clang's byte for byte). False when it doesn't read or fit.
static bool check_test_asm_definition_naked_code(const char* path, const char* names, char* lines, uint64_t capacity)
{
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    CheckTestElf elf = { 0 };
    bool valid = arena.base != 0 && capacity != 0 && check_test_elf_open(&elf, object_test_read_file(&arena, path));
    uint64_t used = 0;
    lines[0] = 0;
    for (uint64_t i = 1; valid && i < elf.symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &elf.symbols[i];
        const char* name = check_test_elf_string(elf.symbol_names, elf.symbol_names_size, symbol->st_name);
        const bool chosen = ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && name != 0 && check_test_asm_definition_named(names, name) &&
                            symbol->st_shndx < elf.section_count;
        const Elf64_Shdr* code = chosen ? &elf.sections[symbol->st_shndx] : 0;
        valid = !chosen || (check_test_elf_range(elf.file, code->sh_offset, code->sh_size) && symbol->st_value <= code->sh_size &&
                            symbol->st_size <= code->sh_size - symbol->st_value);
        char line[512];
        int at = chosen && valid ? snprintf(line, sizeof(line), "%s size %llu:", name, (unsigned long long)symbol->st_size) : 0;
        for (uint64_t b = 0; chosen && valid && b < symbol->st_size && at < (int)sizeof(line) - 4; b += 1)
        {
            at += snprintf(line + at, sizeof(line) - (size_t)at, " %02x", (unsigned)(uint8_t)elf.file.pointer[code->sh_offset + symbol->st_value + b]);
        }
        valid = !chosen || (valid && check_test_asm_definition_append(lines, capacity, &used, line) && check_test_asm_definition_append(lines, capacity, &used, "\n"));
        for (uint32_t s = 1; chosen && valid && s < elf.section_count; s += 1)
        {
            const Elf64_Shdr* table = &elf.sections[s];
            const bool applies = table->sh_type == SHT_RELA && table->sh_info == symbol->st_shndx;
            valid = !applies || (table->sh_entsize == sizeof(Elf64_Rela) && check_test_elf_range(elf.file, table->sh_offset, table->sh_size));
            const Elf64_Rela* relocations = applies && valid ? (const Elf64_Rela*)(elf.file.pointer + table->sh_offset) : 0;
            for (uint64_t r = 0; valid && relocations != 0 && r < table->sh_size / sizeof(Elf64_Rela); r += 1)
            {
                const uint64_t offset = relocations[r].r_offset;
                if (offset >= symbol->st_value && offset - symbol->st_value < symbol->st_size)
                {
                    snprintf(line, sizeof(line), "%s+%llu relocation %u of %s%+lld\n", name, (unsigned long long)(offset - symbol->st_value),
                             (unsigned)ELF64_R_TYPE(relocations[r].r_info), check_test_asm_definition_target(&elf, ELF64_R_SYM(relocations[r].r_info)),
                             (long long)relocations[r].r_addend);
                    valid = check_test_asm_definition_append(lines, capacity, &used, line);
                }
            }
        }
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
        // `.comm` without an alignment and `.lcomm` (#2304): clang's integrated assembler aligns both to 1, where GNU as
        // takes the size's power of two (up to 16 for `.comm`, 8 for `.lcomm`). Statement and file-scope templates, a
        // `.comm` of the unit's definition, and `.lcomm`s whose sizes would pad under GNU's rule.
        { "common symbols without an alignment", "declare main",
          "#include <stdio.h>\n"
          "extern int counter;\n"
          "extern char odd[3];\n"
          "int defined = 1;\n"
          "static void declare(void) { __asm__ volatile(\".comm counter,4\\n.comm odd,3\\n.lcomm scratch,4\\n.lcomm pad,1\"); }\n"
          "__asm__(\".comm defined,8\\n.comm wide,40\\n.lcomm local_pad,3\\n.lcomm local_word,8\");\n"
          "int main(void) { declare(); counter += 41; odd[2] = 2; printf(\"%d %d %d\\n\", counter, odd[2], defined); return 0; }\n" },
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
        // #2551: portable file-scope assembly's percent-prefixed ELF types, including types C never refers to.
        { "file-scope assembly's percent-prefixed symbol types", "main",
          "#include <stdio.h>\n"
          "int percent_entry(int);\nextern int percent_object;\n"
          "__asm__(\".text\\n.globl percent_entry\\n.type percent_entry,%function\\npercent_entry:\\n"
          "movl percent_object(%rip),%eax\\naddl %edi,%eax\\nret\\n.size percent_entry,.-percent_entry\\n"
          ".data\\n.globl percent_object\\n.type percent_object,%object\\npercent_object: .long 11\\n.size percent_object,4\\n"
          ".type percent_notype,%notype\\npercent_notype: .long 7\\n"
          ".pushsection .tdata,\\\"awT\\\",@progbits\\n.type percent_tls,%tls_object\\npercent_tls: .long 3\\n.size percent_tls,4\\n.popsection\\n.text\");\n"
          "int main(void) { printf(\"%d %d\\n\", percent_entry(31), percent_object); return 0; }\n" },
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
        // File-scope sections of other kinds (#1848): an init array's and a fini array's entries, which run C's
        // functions before and after main, a note (its own SHT_NOTE section and PT_NOTE in the executable), merged
        // strings and custom flags.
        { "file-scope assembly's sections of other kinds", "early late main",
          "#include <stdio.h>\n"
          "int order = 1;\n"
          "void early(void) { order = order * 10 + 2; }\n"
          "void late(void) { printf(\"late %d\\n\", order); }\n"
          "extern const char* const names[2];\n"
          "__asm__(\".section .init_array,\\\"aw\\\",@init_array\\n.p2align 3\\n.quad early\\n.section .fini_array,\\\"aw\\\"\\n.p2align 3\\n.quad late\\n.text\");\n"
          "__asm__(\".pushsection .note.buster,\\\"a\\\",@note\\n.p2align 2\\n.long 7, 4, 1\\n.asciz \\\"buster\\\"\\n.p2align 2\\n.long 42\\n.popsection\");\n"
          "__asm__(\".pushsection .rodata.asm.str1.1,\\\"aMS\\\",@progbits,1\\n.Lfirst: .asciz \\\"merged\\\"\\n.Lsecond: .asciz \\\"strings\\\"\\n.popsection\\n"
          ".pushsection .data.rel.ro.names,\\\"aw\\\"\\n.p2align 3\\n.globl names\\nnames: .quad .Lfirst, .Lsecond\\n.popsection\");\n"
          "__asm__(\".pushsection custom_table,\\\"a\\\"\\n.globl custom_entry\\ncustom_entry: .long 3\\n.popsection\");\n"
          "extern const int custom_entry;\n"
          "int main(void) { printf(\"%d %s %s %d\\n\", order, names[0], names[1], custom_entry); order = 5; return 0; }\n" },
        // File-scope CFI (#1848): a function of its own frame, which C calls and which calls C back, whose unwind entry
        // lets glibc's backtrace walk through it to main.
        { "file-scope assembly's CFI", "inner main",
          "#include <execinfo.h>\n"
          "#include <stdio.h>\n"
          "int trampoline(int);\n"
          "int inner(int n) { void* frames[16]; return n + backtrace(frames, 16); }\n"
          "__asm__(\".text\\n.globl trampoline\\n.type trampoline,@function\\ntrampoline:\\n.cfi_startproc\\n\\tpushq %rbx\\n"
          "\\t.cfi_def_cfa_offset 16\\n\\t.cfi_offset %rbx, -16\\n\\tmovl %edi, %ebx\\n\\tcall inner\\n\\taddl %ebx, %eax\\n\\tpopq %rbx\\n"
          "\\t.cfi_def_cfa_offset 8\\n\\tret\\n.cfi_endproc\\n.size trampoline, .-trampoline\");\n"
          "int main(void) { printf(\"%d\\n\", trampoline(100)); return 0; }\n" },
        // A naked function's CFI (#1848): its template's directives are the rows of its own frame description, as clang's
        // implicit `.cfi_startproc` makes them, so that glibc's backtrace from a function it calls walks through it.
        { "a naked function's CFI", "inner main",
          "#include <execinfo.h>\n"
          "#include <stdio.h>\n"
          "int inner(void) { void* frames[16]; return backtrace(frames, 16); }\n"
          "__attribute__((naked)) int through(void) {\n"
          "    __asm__(\"pushq %rbx\\n\\t.cfi_adjust_cfa_offset 8\\n\\t.cfi_rel_offset %rbx, 0\\n\\tsubq $16, %rsp\\n\\t.cfi_adjust_cfa_offset 16\\n\\t\"\n"
          "            \"call inner\\n\\taddq $16, %rsp\\n\\t.cfi_adjust_cfa_offset -16\\n\\tpopq %rbx\\n\\t.cfi_adjust_cfa_offset -8\\n\\t.cfi_restore %rbx\\n\\tret\");\n"
          "}\n"
          "int main(void) { int direct = inner(); printf(\"%d\\n\", through() - direct); return 0; }\n" },
        // A template's `.comm` of a variable the unit defines (#1848): clang's common symbol of the name, of C's size at
        // the `.comm`'s alignment, whichever comes first, an initializer's or a tentative definition's; the definition's
        // bytes stay in their section without a symbol, and C's references (a pointer's relocation among them) are the
        // common's, whose zeros the program reads and writes. A statement template's `.comm` and a file-scope one's.
        { "a template's .comm of the unit's definitions", "declare main",
          "#include <stdio.h>\n"
          "int x = 7;\n"
          "int y = 3;\n"
          "__asm__(\".comm x,8,8\");\n"
          "__asm__(\".comm buffer,64,16\");\n"
          "char buffer[32];\n"
          "long long wide = 1;\n"
          "_Alignas(32) int aligned;\n"
          "__asm__(\".comm aligned,4,1\");\n"
          "int* pointer = &x;\n"
          "short later = 5;\n"
          "void declare(void) { __asm__ volatile(\".comm wide,4,4\\n.comm later,2,2\"); }\n"
          "int main(void) {\n"
          "    declare();\n"
          "    printf(\"%d %d %d %lld %d %d %d\\n\", x, y, buffer[31], wide, aligned, *pointer, later);\n"
          "    x = 42; buffer[31] = 9; wide = 1LL << 40; aligned = 6; later = 11;\n"
          "    printf(\"%d %d %d %lld %d %d %d\\n\", x, y, buffer[31], wide, aligned, *pointer, later);\n"
          "    return 0;\n"
          "}\n" },
        // Asm labels that name the unit's own objects (#1979): a declaration labelled as a later definition, two
        // labelled declarations and an identifier of one name, variables labelled as a tentative and an initialized
        // definition, and a label naming a static function; one symbol per name, as clang's module has one global.
        { "asm labels naming the unit's objects", "main bar tally hidden",
          "#include <stdio.h>\n"
          "double foo(double, double) __asm__(\"bar\");\n"
          "int count(void) __asm__(\"tally\");\n"
          "int tally(void);\n"
          "int twin(void) __asm__(\"tally\");\n"
          "extern int alias_y __asm__(\"shared\");\n"
          "int shared;\n"
          "extern int later __asm__(\"value\");\n"
          "int value = 7;\n"
          "int* const pointer = &later;\n"
          "static int hidden(void) { return 5; }\n"
          "int call_hidden(void) __asm__(\"hidden\");\n"
          "int main(void) {\n"
          "    alias_y = 3;\n"
          "    printf(\"%g %d %d %d %d %d %d %d\\n\", foo(1, 2), count(), twin(), tally(), shared, later, *pointer, call_hidden() + hidden());\n"
          "    return 0;\n"
          "}\n"
          "double bar(double a, double b) { return a + b + 42; }\n"
          "int tally(void) { static int n; return ++n; }\n" },
        // File-scope COMDAT groups (#1848): a weak variable that is its group's signature; a group whose signature is a
        // name of its own, which the object makes a local symbol of; a group of a function's code and its data, with a
        // relocation in each, as C++'s inline functions are; one whose signature is C's variable; and an empty one.
        { "file-scope assembly's COMDAT groups", "main",
          "#include <stdio.h>\n"
          "int sig = 3;\n"
          "extern int shared_value;\n"
          "extern int other_value;\n"
          "int f(void);\n"
          "extern int f_data;\n"
          "extern int (*const f_pointer)(void);\n"
          "extern int signed_value;\n"
          "__asm__(\".section .data.shared_value,\\\"awG\\\",@progbits,shared_value,comdat\\n.globl shared_value\\n.weak shared_value\\n"
          "shared_value: .long 42\\n.text\");\n"
          "__asm__(\".section .data.other_value,\\\"awG\\\",@progbits,other_group,comdat\\n.globl other_value\\n.weak other_value\\nother_value: .long 7\\n.text\");\n"
          "__asm__(\".section .text.f,\\\"axG\\\",@progbits,f,comdat\\n.weak f\\n.type f,@function\\nf: movl f_data(%rip), %eax\\n\\tret\\n.size f, .-f\\n"
          ".section .data.f_data,\\\"awG\\\",@progbits,f,comdat\\n.weak f_data\\nf_data: .long 5\\n.p2align 3\\n.weak f_pointer\\nf_pointer: .quad f\\n.text\");\n"
          "__asm__(\".section .data.signed,\\\"awG\\\",@progbits,sig,comdat\\n.globl signed_value\\nsigned_value: .long 1\\n"
          ".section .data.empty,\\\"awG\\\",@progbits,empty_group,comdat\\n.text\");\n"
          "int main(void) { printf(\"%d %d %d %d %d %d %d\\n\", shared_value, other_value, f(), f_data, f_pointer(), sig, signed_value); return 0; }\n" },
        // Naked functions (#1845): an empty one, clang's function of no bytes with an FDE of no range, and operands that
        // name static objects, printed RIP-relative ("m"(g), an element's and a member's, an asm label's, a static's,
        // H's second word; "i"(&g) with a).
        { "naked functions' symbols and empty bodies", "main",
          "#include <stdio.h>\n"
          "int g = 5;\nint arr[4] = { 1, 2, 3, 4 };\nstruct S { int a; long b; } st = { 1, 2 };\nint lab __asm__(\"renamed\") = 9;\nstatic int sg = 3;\n"
          "__attribute__((naked)) void empty(void) { }\n"
          "__attribute__((naked)) int get_g(void) {\n"
          "    __asm__(\"movl %0, %%eax\\n\\taddl %1, %%eax\\n\\taddl %2, %%eax\\n\\taddl %3, %%eax\\n\\tret\" : : \"m\"(g), \"m\"(arr[3]), \"m\"(lab), \"m\"(sg));\n"
          "}\n"
          "__attribute__((naked)) long get_b(void) { __asm__(\"movq %H0, %%rax\\n\\tret\" : : \"m\"(st)); }\n"
          "__attribute__((naked)) int* address_g(void) { __asm__(\"leaq %a0, %%rax\\n\\tret\" : : \"i\"(&g)); }\n"
          "int main(void) { printf(\"%d %ld %d %d\\n\", get_g(), get_b(), *address_g(), sg); return 0; }\n" },
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
        // The program's own notes (#1848): the direct path's executable has clang's build's note sections, each in a
        // PT_NOTE, and the same notes in them. A fixture with a note must find one in clang's build.
        static char notes[2][4096];
        const bool clang_notes = written && check_test_asm_definition_notes(programs[0], notes[0], sizeof(notes[0]));
        const bool direct_notes = written && check_test_asm_definition_notes(direct[0], notes[1], sizeof(notes[1]));
        snprintf(description, sizeof(description), "%s: the direct path's executable's notes are clang's build's:\n%s\nagainst clang's\n%s", fixtures[f].name,
                 direct_notes ? notes[1] : "(unread)", clang_notes ? notes[0] : "(unread)");
        check_test_expect(test, clang_notes && direct_notes && strcmp(notes[0], notes[1]) == 0 && (strstr(fixtures[f].source, "@note") == NULL || notes[0][0] != 0),
                          description);
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
    check_test_driver_asm_comdat_units(test, clang, executable, directory);
    // A template's `.comm` of a definition clang's code doesn't make a plain common of (#1848) is refused by name, on
    // both paths, at -c and the direct path: a C common symbol's (-fcommon's tentative definition, which isn't one
    // without -fcommon, and the common attribute's), where clang's backend crashes for another size or alignment ("x
    // redeclared as different type") and keeps the one common for the same; a function's, whose code clang keeps without
    // its symbol; a static, weak or thread-local definition's, which clang reports or makes a local, weak or thread-local
    // common of; and a const one's, whose initializer clang's code reads where the program reads the common's zeros.
    static const struct
    {
        const char* source;
        // Refused only with -fcommon.
        bool fcommon;
        const char* reason;
    } common_definitions[] = {
        { "int x; void declare(void) { __asm__(\".comm x,16,16\"); } int main(void) { declare(); x = 42; return x != 42; }\n", true,
          "unsupported: inline assembly .comm of the unit's common symbol" },
        { "int x __attribute__((common)); __asm__(\".comm x,4,4\"); int main(void) { return x; }\n", false,
          "unsupported: inline assembly .comm of the unit's common symbol" },
        { "int f(void) { return 1; } __asm__(\".comm f,4,4\"); int main(void) { return f(); }\n", false, "unsupported: inline assembly .comm of the unit's function" },
        { "static int x = 1; __asm__(\".comm x,4,4\"); int main(void) { return x; }\n", false,
          "unsupported: inline assembly .comm of a static, weak, thread-local or aliased definition" },
        { "__attribute__((weak)) int x = 1; __asm__(\".comm x,4,4\"); int main(void) { return x; }\n", false,
          "unsupported: inline assembly .comm of a static, weak, thread-local or aliased definition" },
        { "__thread int x = 1; __asm__(\".comm x,4,4\"); int main(void) { return x; }\n", false,
          "unsupported: inline assembly .comm of a static, weak, thread-local or aliased definition" },
        { "const int x = 1; __asm__(\".comm x,4,4\"); int main(void) { return x; }\n", false, "unsupported: inline assembly .comm of a read-only definition" },
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(common_definitions); f += 1)
    {
        char path[512];
        char output[512];
        snprintf(path, sizeof(path), "%s/asm-common-definition-%u.c", directory, f);
        snprintf(output, sizeof(output), "%s/asm-common-definition-%u.o", directory, f);
        const bool written = parse_test_write(path, common_definitions[f].source, strlen(common_definitions[f].source));
        for (uint32_t route = 0; written && route < 8; route += 1)
        {
            // Both paths, -c and the direct path, with and without -fcommon.
            char* text = NULL;
            char* arguments[] = { (char*)executable, "-std=gnu17", (route & 4) != 0 ? "-fcommon" : "-fno-common", path, "-o", output, (route & 2) != 0 ? "-c" : NULL,
                                  NULL, NULL };
            arguments[(route & 2) != 0 ? 7 : 6] = (route & 1) != 0 ? "--scalar" : NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            const bool refused = !common_definitions[f].fcommon || (route & 4) != 0;
            char description[1024];
            snprintf(description, sizeof(description), "a template's .comm of the unit's definition %u is %s (%s, %s, %s): %s", f, refused ? "refused by name" : "taken",
                     (route & 1) != 0 ? "scalar" : "vector", (route & 2) != 0 ? "-c" : "direct", (route & 4) != 0 ? "-fcommon" : "-fno-common", text != NULL ? text : "");
            check_test_expect(test, refused ? status == 1 && text != NULL && strstr(text, common_definitions[f].reason) != NULL : status == 0, description);
            free(text);
        }
        unlink(path);
        unlink(output);
    }
    // A file-scope label of a definition's name (#1848) is clang's assembler error, without a source location, its whole
    // output on both paths, at -c and the direct path, with and without -fcommon, but not at -fsyntax-only: a variable's
    // twice, a function's and a common symbol's (-fcommon's tentative definition) once, whatever the label's section or
    // `.globl`, and beside a `.comm` that commons another definition. Labels clang reports otherwise are refused by name:
    // several definitions' (in its module's order), a statement template's, a static definition's, and an `.lcomm`'s or a
    // `.weak` label's (clang reports the binding too).
    static const struct
    {
        const char* source;
        const char* reason;
    } label_definitions[] = {
        { "int x = 1;\n__asm__(\".globl x\\nx: .long 2\");\nint main(void) { return x; }\n", NULL },
        { "int f(void) { return 1; }\n__asm__(\".globl f\\n.type f,@function\\nf: movl $2, %eax\\nret\");\nint main(void) { return f(); }\n", NULL },
        { "int x;\n__asm__(\".data\\n.globl x\\nx: .long 2\");\nint main(void) { return x; }\n", NULL },
        { "char text[8] = \"text\";\n__asm__(\".pushsection .rodata\\ntext: .asciz \\\"asm\\\"\\n.popsection\");\nint main(void) { return text[0]; }\n", NULL },
        { "int x = 1;\nint y = 2;\n__asm__(\"x: .long 2\\n.comm y,4,4\");\nint main(void) { return x + y; }\n", NULL },
        { "int x = 1;\nint y = 2;\n__asm__(\".data\\nx: .long 2\\ny: .long 3\");\nint main(void) { return x + y; }\n",
          "unsupported: inline assembly labels named as several of the unit's definitions" },
        { "int x = 1;\nvoid g(void) { __asm__(\".pushsection .data\\nx: .long 2\\n.popsection\"); }\nint main(void) { g(); return x; }\n",
          "unsupported: inline assembly labels named as the unit's symbols" },
        { "static int x = 1;\n__asm__(\".data\\nx: .long 2\");\nint main(void) { return x; }\n", "unsupported: inline assembly labels named as the unit's symbols" },
        { "int x;\n__asm__(\".lcomm x,8\");\nint main(void) { return x; }\n", "unsupported: inline assembly labels named as the unit's symbols" },
        { "int x = 1;\n__asm__(\".weak x\\n.data\\nx: .long 2\");\nint main(void) { return x; }\n", "unsupported: inline assembly labels named as the unit's symbols" },
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(label_definitions); f += 1)
    {
        char path[512];
        char output[512];
        snprintf(path, sizeof(path), "%s/asm-label-definition-%u.c", directory, f);
        snprintf(output, sizeof(output), "%s/asm-label-definition-%u.o", directory, f);
        const char* reason = label_definitions[f].reason;
        const bool written = parse_test_write(path, label_definitions[f].source, strlen(label_definitions[f].source));
        for (uint32_t common = 0; written && common < 2; common += 1)
        {
            char* expected = NULL;
            char* clang_arguments[] = { (char*)clang, "-std=gnu17", common != 0 ? "-fcommon" : "-fno-common", "-fno-caret-diagnostics", "-c", path, "-o", output, NULL };
            const int clang_status = check_test_driver_spawn(clang_arguments, &expected);
            char description[2048];
            snprintf(description, sizeof(description), "asm label of a definition %u: clang's -c fails (%s): %s", f, common != 0 ? "-fcommon" : "-fno-common",
                     expected != NULL ? expected : "");
            check_test_expect(test, clang_status != 0 && expected != NULL && strstr(expected, "is already defined") != NULL, description);
            for (uint32_t route = 0; route < 6; route += 1)
            {
                // Both paths, at -c, the direct path and -fsyntax-only.
                const uint32_t mode = route >> 1;
                char* text = NULL;
                char* arguments[] = { (char*)executable, "-std=gnu17", common != 0 ? "-fcommon" : "-fno-common", "-fno-caret-diagnostics", path,
                                      mode == 2 ? "-fsyntax-only" : "-o", mode == 2 ? NULL : output, mode == 0 ? "-c" : NULL, NULL, NULL };
                arguments[mode == 2 ? 6 : mode == 0 ? 8 : 7] = (route & 1) != 0 ? "--scalar" : NULL;
                const int status = check_test_driver_spawn(arguments, &text);
                const bool reported = mode == 2       ? status == 0 && (text == NULL || text[0] == 0) :
                                      reason == NULL ? status == 1 && text != NULL && expected != NULL && strcmp(text, expected) == 0 :
                                                       status == 1 && text != NULL && strstr(text, reason) != NULL;
                snprintf(description, sizeof(description), "asm label of a definition %u (%s, %s, %s): %s\nagainst clang's\n%s", f, (route & 1) != 0 ? "scalar" : "vector",
                         mode == 0 ? "-c" : mode == 1 ? "direct" : "-fsyntax-only", common != 0 ? "-fcommon" : "-fno-common", text != NULL ? text : "",
                         expected != NULL ? expected : "");
                check_test_expect(test, reported, description);
                free(text);
            }
            free(expected);
        }
        unlink(path);
        unlink(output);
    }
    // Naked functions' code (#1845): an empty one, and operands that name a static object or a function, by every
    // modifier clang prints them with, absolute ones included (which a PIE can't link): -c's objects, by both paths, have
    // clang's bytes and relocations in each.
    static const char naked[] =
        "int g = 5;\nint arr[4] = { 1, 2, 3, 4 };\nstruct S { int a; long b; } st = { 1, 2 };\nint lab __asm__(\"renamed\") = 9;\nstatic int sg = 3;\n"
        "void fn(void) { }\n"
        "__attribute__((naked)) void empty(void) { }\n"
        "__attribute__((naked)) void a1(void) { __asm__(\"movq %0, %%rax\\n\\tmovq %c0, %%rax\\n\\tmovq %P0, %%rax\\n\\tmovq %a0, %%rax\\n\\tret\" : : \"i\"(&g)); }\n"
        "__attribute__((naked)) void a2(void) { __asm__(\"movq %0, %%rax\\n\\tmovq %c1, %%rax\\n\\tret\" : : \"i\"(&arr[2]), \"i\"(&st.b)); }\n"
        "__attribute__((naked)) void a3(void) {\n"
        "    __asm__(\"movl %0, %%eax\\n\\tmovl %1, %%eax\\n\\tmovq %2, %%rax\\n\\tmovl %3, %%eax\\n\\tmovl %4, %%eax\\n\\tret\" : : \"m\"(g), \"m\"(arr[3]), \"m\"(st.b),\n"
        "            \"m\"(lab), \"m\"(sg));\n"
        "}\n"
        "__attribute__((naked)) void a4(void) { __asm__(\"call %P0\\n\\tjmp %c0\\n\\tleaq %a1, %%rax\\n\\tmovq %H2, %%rax\\n\\tret\" : : \"i\"(fn), \"i\"(&g), \"m\"(st)); }\n";
    char naked_path[512];
    char naked_objects[3][512];
    char naked_code[3][4096];
    snprintf(naked_path, sizeof(naked_path), "%s/asm-naked-code.c", directory);
    const bool naked_written = parse_test_write(naked_path, naked, sizeof(naked) - 1);
    for (uint32_t p = 0; naked_written && p < 3; p += 1)
    {
        snprintf(naked_objects[p], sizeof(naked_objects[p]), "%s/asm-naked-code-%u.o", directory, p);
        char* text = NULL;
        char* arguments[] = { (char*)(p == 0 ? clang : executable), "-std=gnu17", "-c", naked_path, "-o", naked_objects[p], p == 2 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        char description[1024];
        snprintf(description, sizeof(description), "naked functions' code compiles by %s: %s", p == 0 ? "clang" : p == 1 ? "the vector path" : "the scalar path",
                 text != NULL ? text : "");
        free(text);
        naked_code[p][0] = 0;
        check_test_expect(test, compiled && check_test_asm_definition_naked_code(naked_objects[p], "empty a1 a2 a3 a4", naked_code[p], sizeof(naked_code[p])),
                          description);
        snprintf(description, sizeof(description), "naked functions' code is clang's, by the %s path:\n%s\nclang:\n%s", p == 1 ? "vector" : "scalar", naked_code[p],
                 naked_code[0]);
        check_test_expect(test, p == 0 || (naked_code[0][0] != 0 && strcmp(naked_code[p], naked_code[0]) == 0), description);
        unlink(naked_objects[p]);
    }
    unlink(naked_path);
    // The `.L` labels of code (#1845): a per-statement helper template's, a naked function's and `&&label`'s are
    // temporaries, which -c's object leaves out as clang's assembler does, on both paths (the helpers' own function
    // symbols stay); the objects still link and run as clang's build.
    static const char temporaries[] =
        "#include <stdio.h>\n"
        "int test_zero(int x) { __asm__ volatile(\"test %0, %0\\n\\tjz .Lskip%=\\n\\tnop\\n.Lskip%=:\" : : \"r\"(x)); return x; }\n"
        "__attribute__((naked)) int naked_three(void) { __asm__(\"jmp .Lnaked\\n.Lnaked: movl $3, %eax\\n\\tret\"); }\n"
        "int pick(int x) { static void* table[] = { &&first, &&second }; goto *table[x]; first: return 1; second: return 2; }\n"
        "int main(void) { printf(\"%d %d %d %d\\n\", test_zero(4), naked_three(), pick(0), pick(1)); return 0; }\n";
    char temporary_path[512];
    char temporary_object[512];
    char temporary_program[512];
    snprintf(temporary_path, sizeof(temporary_path), "%s/asm-code-temporaries.c", directory);
    snprintf(temporary_object, sizeof(temporary_object), "%s/asm-code-temporaries.o", directory);
    snprintf(temporary_program, sizeof(temporary_program), "%s/asm-code-temporaries", directory);
    const bool temporary_written = parse_test_write(temporary_path, temporaries, sizeof(temporaries) - 1);
    for (uint32_t p = 0; temporary_written && p < 2; p += 1)
    {
        char* text = NULL;
        char* arguments[] = { (char*)executable, "-std=gnu17", "-c", temporary_path, "-o", temporary_object, p == 1 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        char found[256] = { 0 };
        char description[1024];
        const bool kept = compiled && check_test_asm_definition_code_temporary(temporary_object, found, sizeof(found));
        snprintf(description, sizeof(description), "code's .L labels are left out of the %s path's -c object: %s", p == 1 ? "scalar" : "vector",
                 compiled ? found : "it didn't compile");
        check_test_expect(test, compiled && !kept, description);
        char* const link[] = { (char*)clang, temporary_object, "-o", temporary_program, NULL };
        text = NULL;
        const bool linked = compiled && check_test_driver_spawn(link, &text) == 0;
        free(text);
        char output[256] = { 0 };
        const bool ran = linked && check_test_driver_capture(temporary_program, output, sizeof(output));
        snprintf(description, sizeof(description), "the %s path's object without code's .L labels runs: %s", p == 1 ? "scalar" : "vector", output);
        check_test_expect(test, ran && strcmp(output, "4 3 1 2\n") == 0, description);
        unlink(temporary_object);
        unlink(temporary_program);
    }
    unlink(temporary_path);
    // Two functions' definitions of one name through an asm label (#1979) are clang's code generation error, whole output
    // on both paths, at -c and the direct path, but not at -fsyntax-only; two variables' are refused by name, where clang
    // keeps the later one's storage alone.
    static const char* const label_conflicts[] = {
        "int foo(void) __asm__(\"bar\");\nint foo(void) { return 1; }\nint bar(void) { return 2; }\nint main(void) { return foo(); }\n",
        "int bar(void) { return 2; }\nint foo(void) __asm__(\"bar\");\nint foo(void) { return 1; }\nint main(void) { return bar(); }\n",
        "int y __asm__(\"z\") = 1;\nint z = 2;\nint main(void) { return z; }\n",
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(label_conflicts); f += 1)
    {
        char path[512];
        char output[512];
        snprintf(path, sizeof(path), "%s/asm-label-conflict-%u.c", directory, f);
        snprintf(output, sizeof(output), "%s/asm-label-conflict-%u.o", directory, f);
        const bool written = parse_test_write(path, label_conflicts[f], strlen(label_conflicts[f]));
        const bool function = f < 2;
        char* expected = NULL;
        char* clang_arguments[] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", "-c", path, "-o", output, NULL };
        const int clang_status = written ? check_test_driver_spawn(clang_arguments, &expected) : 0;
        char description[2048];
        snprintf(description, sizeof(description), "asm label conflict %u: clang's -c %s", f, function ? "fails" : "succeeds");
        check_test_expect(test, written && (clang_status != 0) == function, description);
        for (uint32_t route = 0; written && route < 6; route += 1)
        {
            // Both paths, at -c, the direct path and -fsyntax-only.
            const uint32_t mode = route >> 1;
            char* text = NULL;
            char* arguments[] = { (char*)executable, "-std=gnu17", "-fno-caret-diagnostics", path, mode == 2 ? "-fsyntax-only" : "-o", mode == 2 ? NULL : output,
                                  mode == 0 ? "-c" : NULL, NULL, NULL };
            arguments[mode == 2 ? 5 : mode == 0 ? 7 : 6] = (route & 1) != 0 ? "--scalar" : NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            const bool reported = mode == 2 ? status == 0 && (text == NULL || text[0] == 0) :
                                  function  ? status == 1 && text != NULL && expected != NULL && strcmp(text, expected) == 0 :
                                              status == 1 && text != NULL && strstr(text, "unsupported: a second definition of a variable's symbol name through an asm label") != NULL;
            snprintf(description, sizeof(description), "asm label conflict %u (%s, %s): %s\nagainst clang's\n%s", f, (route & 1) != 0 ? "scalar" : "vector",
                     mode == 0 ? "-c" : mode == 1 ? "direct" : "-fsyntax-only", text != NULL ? text : "", expected != NULL ? expected : "");
            check_test_expect(test, reported, description);
            free(text);
        }
        free(expected);
        unlink(path);
        unlink(output);
    }
}
