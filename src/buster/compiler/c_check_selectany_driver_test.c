// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original selectany fixtures for the C producer of LinkGroups. The independent ELF reader checks weak signatures,
// section membership and grouped relocations. Both external object linking and the direct units' merge must select
// the first definition, discard its competitors' undefined references and keep stronger ordinary definitions.
// Included by c_check_units_test.c after its subprocess and file helpers.

static bool check_units_test_selectany_object(const char* path, bool foreign)
{
    static const char* const names[] = { "choice", "extra", "zero", "constant", "tls", "tls_zero", "override" };
    uint64_t size = 0;
    char* bytes = check_units_test_read(path, &size);
    ObjectView view;
    char error[256] = { 0 };
    bool valid = bytes != NULL && object_view_open(&view, (const uint8_t*)bytes, size, foreign, error, sizeof(error));
    if (!valid)
    {
        fprintf(stderr, "  selectany object %s: %s\n", path, bytes != NULL ? error : "cannot read object");
    }
    uint32_t found = 0;
    uint32_t groups = 0;
    for (uint64_t s = 1; valid && s < view.section_count; s += 1)
    {
        const Elf64_Shdr* section = view.sections + s;
        if (section->sh_type != SHT_GROUP)
        {
            continue;
        }
        groups += 1;
        valid = section->sh_link == view.symbol_table_index && section->sh_info != 0 && section->sh_info < view.symbol_count &&
                section->sh_size >= 8 && section->sh_size % 4 == 0;
        if (valid)
        {
            const char* name = object_view_symbol_name(&view, section->sh_info);
            uint32_t which = BUSTER_ARRAY_LENGTH(names);
            for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(names); n += 1)
            {
                which = strcmp(names[n], name) == 0 ? n : which;
            }
            const uint32_t* members = (const uint32_t*)(view.file + section->sh_offset);
            const uint64_t count = section->sh_size / 4;
            valid = which < BUSTER_ARRAY_LENGTH(names) && (found & (1u << which)) == 0 && members[0] == GRP_COMDAT &&
                    ELF64_ST_BIND(view.symbols[section->sh_info].st_info) == STB_WEAK;
            if (valid)
            {
                found |= 1u << which;
                const uint64_t definition = object_view_symbol_section(&view, section->sh_info);
                bool defines = false;
                uint32_t relocations = 0;
                for (uint64_t m = 1; valid && m < count; m += 1)
                {
                    const uint32_t member = members[m];
                    valid = member > s && member < view.section_count && (view.sections[member].sh_flags & SHF_GROUP) != 0;
                    if (valid)
                    {
                        defines |= member == definition;
                        relocations += view.sections[member].sh_type == SHT_RELA;
                    }
                }
                valid = valid && defines && relocations == (which == 0 ? 1u : 0u) && count == (which == 0 ? 3u : 2u);
            }
            if (!valid)
            {
                fprintf(stderr, "  selectany group %s: binding %u, %llu member words, signature section %llu\n", name,
                        ELF64_ST_BIND(view.symbols[section->sh_info].st_info), (unsigned long long)count,
                        (unsigned long long)object_view_symbol_section(&view, section->sh_info));
            }
        }
    }
    valid = valid && groups == BUSTER_ARRAY_LENGTH(names) && found == (1u << BUSTER_ARRAY_LENGTH(names)) - 1;
    static const char* const aliases[] = { "selected_alias", "selected_group_alias" };
    static const char* const bases[] = { "aliased", "extra" };
    for (uint32_t a = 0; valid && a < BUSTER_ARRAY_LENGTH(aliases); a += 1)
    {
        uint64_t alias = 0;
        uint64_t base = 0;
        for (uint64_t s = 1; s < view.symbol_count; s += 1)
        {
            const char* name = object_view_symbol_name(&view, s);
            alias = strcmp(name, aliases[a]) == 0 ? s : alias;
            base = strcmp(name, bases[a]) == 0 ? s : base;
        }
        valid = alias != 0 && base != 0 && ELF64_ST_BIND(view.symbols[alias].st_info) == STB_WEAK &&
                object_view_symbol_section(&view, alias) == object_view_symbol_section(&view, base) &&
                view.symbols[alias].st_value == view.symbols[base].st_value && view.symbols[alias].st_size == view.symbols[base].st_size;
        if (!valid)
        {
            fprintf(stderr, "  selectany alias %s does not share %s's section, value and size with weak binding\n", aliases[a], bases[a]);
        }
    }
    free(bytes);
    return valid;
}

static bool check_units_test_selectany_bytes(const char* bytes, uint64_t size, uint64_t address, void* result, uint64_t length)
{
    bool found = false;
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)bytes;
    const bool valid = size >= sizeof(*header) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 &&
                       header->e_phentsize == sizeof(Elf64_Phdr) && header->e_phoff <= size &&
                       header->e_phnum <= (size - header->e_phoff) / sizeof(Elf64_Phdr);
    for (uint32_t p = 0; valid && !found && p < header->e_phnum; p += 1)
    {
        Elf64_Phdr segment;
        memcpy(&segment, bytes + header->e_phoff + (uint64_t)p * sizeof(segment), sizeof(segment));
        const uint64_t offset = address >= segment.p_vaddr ? address - segment.p_vaddr : UINT64_MAX;
        found = segment.p_type == PT_LOAD && offset <= segment.p_filesz && length <= segment.p_filesz - offset &&
                segment.p_offset <= size && offset <= size - segment.p_offset && length <= size - segment.p_offset - offset;
        if (found)
        {
            memcpy(result, bytes + segment.p_offset + offset, length);
        }
    }
    return found;
}

// Ask binutils to decode the executable independently: the retained `choice` must point to the fixture's actual
// Row and pointee, while the second unit's discarded definition must not describe an unrelated live object.
static bool check_units_test_selectany_debug(const char* directory, const char* name, const char* errors)
{
    char dwarf_path[PATH_MAX];
    char image_path[PATH_MAX];
    snprintf(dwarf_path, sizeof(dwarf_path), "%s/debug-dump.txt", directory);
    snprintf(image_path, sizeof(image_path), "%s/%s", directory, name);
    char* dwarf_args[] = { (char*)"readelf", (char*)"--wide", (char*)"--debug-dump=info", (char*)name, NULL };
    bool valid = check_units_test_run(directory, dwarf_args, dwarf_path, errors, false) == 0;
    uint64_t size = 0;
    char* dwarf = valid ? check_units_test_read(dwarf_path, &size) : NULL;
    char* image = valid ? check_units_test_read(image_path, &size) : NULL;
    valid = dwarf != NULL && image != NULL;
    uint32_t definitions = 0;
    uint32_t aliases = 0;
    const char* at = dwarf;
    while (valid && at != NULL && (at = strstr(at, "DW_AT_name")) != NULL)
    {
        const char* end = strchr(at, '\n');
        const char* colon = strchr(at, ':');
        if (end == NULL || colon == NULL || colon > end)
        {
            valid = false;
            break;
        }
        const char* value = colon + 1;
        while (value < end && (*value == ' ' || *value == '\t'))
        {
            value += 1;
        }
        if ((size_t)(end - value) >= 9 && memcmp(value, "(string) ", 9) == 0)
        {
            value += 9;
        }
        if ((size_t)(end - value) == 6 && memcmp(value, "choice", 6) == 0)
        {
            const char* next_die = strstr(end, "DW_TAG_");
            const char* location = strstr(end, "DW_OP_addr:");
            if (location != NULL && (next_die == NULL || location < next_die))
            {
                char* after = NULL;
                const unsigned long long address = strtoull(location + strlen("DW_OP_addr:"), &after, 16);
                uint64_t pointer = 0;
                uint32_t row = 0;
                uint32_t pointee = 0;
                const bool live = address != 0 && address <= UINT64_MAX - sizeof(pointer) && check_units_test_selectany_bytes(image, size, address, &pointer, sizeof(pointer)) &&
                                  check_units_test_selectany_bytes(image, size, address + sizeof(pointer), &row, sizeof(row)) &&
                                  check_units_test_selectany_bytes(image, size, pointer, &pointee, sizeof(pointee)) && row == 17 && pointee == 13;
                valid = after != location + strlen("DW_OP_addr:") && definitions < 2 && (definitions == 0 ? live : address == 0);
                definitions += 1;
            }
        }
        else if ((size_t)(end - value) == 12 && memcmp(value, "losing_alias", 12) == 0)
        {
            const char* next_die = strstr(end, "DW_TAG_");
            const char* location = strstr(end, "DW_OP_addr:");
            if (location != NULL && (next_die == NULL || location < next_die))
            {
                char* after = NULL;
                const unsigned long long address = strtoull(location + strlen("DW_OP_addr:"), &after, 16);
                valid = after != location + strlen("DW_OP_addr:") && address == 0;
                aliases += 1;
            }
        }
        at = end + 1;
    }
    free(dwarf);
    free(image);
    return valid && definitions == 2 && aliases == 1;
}

static void check_units_test_selectany(CheckUnitsTestTally* tally, const char* executable, const char* clang, const char* directory)
{
    static const char first[] =
        "struct Row { int *pointer; int value; };\n"
        "static int local = 13;\n"
        "struct Row choice __attribute__((selectany, section(\".shared\"))) = { &local, 17 };\n"
        "int extra __attribute__((selectany, section(\".shared\"))) = 29;\n"
        "int zero __attribute__((selectany));\n"
        "const int constant __attribute__((selectany)) = 19;\n"
        "_Thread_local int tls __attribute__((selectany)) = 23;\n"
        "_Thread_local int tls_zero __attribute__((selectany));\n"
        "int override __attribute__((selectany)) = 7;\n"
        "int aliased = 31;\n"
        "extern int selected_alias __attribute__((alias(\"aliased\"), selectany));\n"
        "extern int selected_group_alias __attribute__((alias(\"extra\"), selectany));\n";
    static const char second[] =
        "struct Row { int *pointer; int value; };\n"
        "extern int discarded_only;\n"
        "struct Row choice __attribute__((selectany, section(\".shared\"))) = { &discarded_only, 97 };\n"
        "struct Row *choice_reference = &choice;\n"
        "extern struct Row losing_alias __attribute__((alias(\"choice\"), selectany));\n"
        "int extra __attribute__((selectany, section(\".shared\"))) = 99;\n"
        "int zero __attribute__((selectany)) = 777;\n"
        "const int constant __attribute__((selectany)) = 777;\n"
        "_Thread_local int tls __attribute__((selectany)) = 777;\n"
        "_Thread_local int tls_zero __attribute__((selectany)) = 777;\n"
        "int override = 37;\n";
    static const char main_unit[] =
        "#include <stdio.h>\n"
        "struct Row { int *pointer; int value; };\n"
        "extern struct Row choice; extern struct Row *choice_reference; extern int extra, zero, override; extern const int constant;\n"
        "extern int selected_alias, selected_group_alias;\n"
        "extern _Thread_local int tls, tls_zero;\n"
        "int main(void) { printf(\"%d %d %d %d %d %d %d\\n\", *choice.pointer + choice.value, extra, zero, constant, tls, tls_zero, override); return selected_alias != 31 || selected_group_alias != 29 || choice_reference != &choice; }\n";
    static const char one[] = "int value __attribute__((selectany)) = 11; int main(void) { return value == 11 ? 0 : 1; }\n";
    static const char* const sources[] = { "first.c", "second.c", "main.c", "one.c" };
    const char* const texts[] = { first, second, main_unit, one };
    char own[PATH_MAX];
    char path[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    char expected_path[PATH_MAX];
    snprintf(own, sizeof(own), "%s/selectany", directory);
    snprintf(output, sizeof(output), "%s/output.txt", own);
    snprintf(errors, sizeof(errors), "%s/errors.txt", own);
    snprintf(expected_path, sizeof(expected_path), "%s/expected.txt", own);
    bool written = mkdir(own, 0755) == 0;
    for (uint32_t f = 0; written && f < BUSTER_ARRAY_LENGTH(sources); f += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", own, sources[f]);
        written = parse_test_write(path, texts[f], strlen(texts[f]));
    }
    check_units_test_expect(tally, written, "write selectany definitions and discarded-relocation fixtures", own);
    char* reference[] = { (char*)clang, "-std=gnu17", "-O0", "-fcommon", "-w", "first.c", "second.c", "main.c", "-o", "reference", NULL };
    const bool reference_built = written && check_units_test_run(own, reference, "/dev/null", errors, false) == 0;
    char* reference_run[] = { "./reference", NULL };
    const bool reference_ran = reference_built && check_units_test_run(own, reference_run, expected_path, errors, false) == 0;
    uint64_t expected_length = 0;
    char* expected = reference_ran ? check_units_test_read(expected_path, &expected_length) : NULL;
    check_units_test_expect(tally, expected != NULL && strcmp(expected, "30 29 0 19 23 0 37\n") == 0,
                            "Clang selects the first groups and the strong override", errors);
    static const char* const options[][7] = {
        { "--lanes", "1", NULL },
        { "--lanes", "2", NULL },
        { "--lanes", "8", "--poison-arenas", "--id-order", "1447", NULL },
        { "--scalar", "--lanes", "8", NULL },
        { "--lanes", "1", "-g", NULL },
        { "--lanes", "8", "-g", NULL },
        { "--tier1", "--lanes", "2", NULL },
    };
    char first_output[PATH_MAX];
    snprintf(first_output, sizeof(first_output), "%s/direct-0", own);
    for (uint32_t mode = 0; expected != NULL && mode < BUSTER_ARRAY_LENGTH(options); mode += 1)
    {
        char name[32];
        snprintf(name, sizeof(name), "direct-%u", mode);
        char* arguments[24] = { (char*)executable, "-std=gnu17", "-fcommon", "-w" };
        uint32_t count = 4;
        for (uint32_t o = 0; options[mode][o] != NULL; o += 1)
        {
            arguments[count++] = (char*)options[mode][o];
        }
        arguments[count++] = "first.c";
        arguments[count++] = "second.c";
        arguments[count++] = "main.c";
        arguments[count++] = "-o";
        arguments[count++] = name;
        arguments[count] = NULL;
        const int built = check_units_test_run(own, arguments, "/dev/null", errors, false);
        snprintf(path, sizeof(path), "%s/%s", own, name);
        char* run[] = { path, NULL };
        const int ran = built == 0 ? check_units_test_run(own, run, output, errors, false) : -1;
        uint64_t length = 0;
        char* actual = ran == 0 ? check_units_test_read(output, &length) : NULL;
        check_units_test_expect(tally, actual != NULL && length == expected_length && memcmp(actual, expected, length) == 0,
                                "direct selectany linking drops discarded relocations and runs like Clang", name);
        if (mode == 4 || mode == 5)
        {
            check_units_test_expect(tally, built == 0 && check_units_test_selectany_debug(own, name, errors),
                                    "selectany DWARF retains the live address and tombstones the discarded definition", name);
        }
        if (mode < 4)
        {
            check_units_test_expect(tally, built == 0 && check_test_driver_same_files(path, first_output),
                                    "selectany executables agree across scalar, lanes, ID order and poisoned arenas", name);
        }
        if (mode == 5)
        {
            char debug_output[PATH_MAX];
            snprintf(debug_output, sizeof(debug_output), "%s/direct-4", own);
            check_units_test_expect(tally, built == 0 && check_test_driver_same_files(path, debug_output),
                                    "selectany debug executables agree across scalar and parallel unit merges", name);
        }
        free(actual);
    }
    free(expected);
    for (uint32_t kernel = 0; written && kernel < 3; kernel += 1)
    {
        char* arguments[] = { (char*)(kernel == 0 ? clang : executable), "-std=gnu17", "-fcommon", "-w", "-c", "first.c", "second.c", "main.c",
                              kernel == 2 ? "--scalar" : NULL, NULL };
        const bool built = check_units_test_run(own, arguments, "/dev/null", errors, false) == 0;
        snprintf(path, sizeof(path), "%s/first.o", own);
        check_units_test_expect(tally, built && check_units_test_selectany_object(path, kernel == 0), "selectany objects have Clang's weak COMDAT signatures and relocation members", arguments[0]);
        char* link[] = { (char*)clang, "first.o", "second.o", "main.o", "-o", "objects", NULL };
        const bool linked = built && check_units_test_run(own, link, "/dev/null", errors, false) == 0;
        char* run[] = { "./objects", NULL };
        const int ran = linked ? check_units_test_run(own, run, output, errors, false) : -1;
        uint64_t length = 0;
        char* actual = ran == 0 ? check_units_test_read(output, &length) : NULL;
        check_units_test_expect(tally, actual != NULL && strcmp(actual, "30 29 0 19 23 0 37\n") == 0,
                                "external linking discards the same group relocations", arguments[0]);
        free(actual);
    }
    for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
    {
        char* one_build[] = { (char*)executable, "one.c", "-o", "one", scalar != 0 ? "--scalar" : NULL, NULL };
        const bool built = check_units_test_run(own, one_build, "/dev/null", errors, false) == 0;
        char* one_run[] = { "./one", NULL };
        check_units_test_expect(tally, built && check_units_test_run(own, one_run, "/dev/null", errors, false) == 0,
                                "single-unit selectany survives runtime binding", scalar != 0 ? "scalar" : "vector");
        char* reversed[] = { (char*)executable, "second.c", "first.c", "main.c", "-o", "reversed", scalar != 0 ? "--scalar" : NULL, NULL };
        const int status = check_units_test_run(own, reversed, "/dev/null", errors, false);
        uint64_t length = 0;
        char* text = check_units_test_read(errors, &length);
        check_units_test_expect(tally, status == 1 && text != NULL && strstr(text, "undefined reference to 'discarded_only'") != NULL,
                                "reversing the units selects the group's formerly discarded undefined reference", scalar != 0 ? "scalar" : "vector");
        free(text);
    }
    printf("  selectany: grouped objects, first-group selection, discarded relocations and strong overrides\n");
}
