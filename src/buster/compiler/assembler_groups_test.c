// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original section-group fixtures (#1268). GNU as and the system linker are independent external oracles; no
// compiler-generated assembly or external fixture is incorporated here.

static const char asm_test_groups_fixture[] =
    ".section .debug_macro,\"G\",@progbits,macro_a,comdat\n.Lmacro_a: .long .Lmacro_b\n"
    ".section .debug_macro,\"G\",@progbits,macro_b,comdat\n.Lmacro_b: .long .Lmacro_a\n.byte 2\n"
    ".section .debug_macro,\"G\",@progbits,macro_a,comdat\n.byte 1\n"
    ".pushsection .debug_macro,\"\",@progbits\n.byte 3\n.popsection\n.byte 4\n"
    ".section .text.pick,\"axG\",@progbits,pick,comdat\n.weak pick\n.type pick,@function\npick: call external\nret\n.size pick,.-pick\n"
    ".section .data,\"awG\",@progbits,pick,comdat\n.quad pick\n"
    ".section .rodata.merge,\"aMSG\",@progbits,1,macro_a,comdat\n.asciz \"grouped\"\n"
    ".section .tbss.group,\"awTG\",@nobits,tls_group,comdat\n.local tls_value\ntls_value: .zero 8\n"
    ".section .empty,\"aG\",@progbits,empty_group,comdat\n"
    ".section .local,\"aG\",@progbits,.Lsignature,comdat\n.Lsignature: .byte 5\n"
    ".section .global,\"aG\",@progbits,global_signature,comdat\n.globl global_signature\n.byte 6\n"
    ".section .weak,\"aG\",@progbits,weak_signature,comdat\n.weak weak_signature\n.byte 7\n"
    ".section .plain,\"aG\",@progbits,plain_signature\n.long plain_signature\n"
    ".section .same,\"aG\",@progbits,macro_a,comdat\n.byte 8\n"
    ".section .same,\"awG\",@nobits,macro_b,comdat\n.zero 9\n";

// Ordinary mentions and even cancelled expression uses decide whether GNU retains an undefined signature.
static const char asm_test_group_signatures[] =
    ".section .x,\"G\",@progbits,g0,comdat\n"
    "\n"
    ".section .x,\"G\",@progbits,g1,comdat\n"
    ".local g1\n"
    ".section .x,\"G\",@progbits,g2,comdat\n"
    ".type g2,@function\n"
    ".section .x,\"G\",@progbits,g3,comdat\n"
    ".size g3,5\n"
    ".section .x,\"G\",@progbits,g4,comdat\n"
    ".hidden g4\n"
    ".section .x,\"G\",@progbits,g5,comdat\n"
    ".weak g5\n"
    ".section .x,\"G\",@progbits,g6,comdat\n"
    ".weak g6\n"
    ".type g6,@function\n"
    ".size g6,5\n"
    ".hidden g6\n"
    ".section .x,\"G\",@progbits,g7,comdat\n"
    ".long g7-g7\n"
    ".section .x,\"G\",@progbits,g8,comdat\n"
    ".weak g8\n"
    ".long g8-g8\n"
    ".section .x,\"G\",@progbits,g9,comdat\n"
    ".local g9\n"
    ".long g9-g9\n"
    ".section .x,\"G\",@progbits,g10,comdat\n"
    ".Lx: .byte 1\n"
    ".section .x,\"G\",@progbits,defined_signature,comdat\n"
    "defined_signature: .byte 11\n"
    ".section .x,\"G\",@progbits,\"quoted.signature\",comdat\n"
    ".byte 12\n";

static void asm_test_group_metadata(AsmTest* test)
{
    AsmResult assembled = asm_test_pair(test, (String8){.pointer = (char*)asm_test_groups_fixture, .length = sizeof(asm_test_groups_fixture) - 1}, 1, X86_ARCH_BASELINE);
    if (assembled.error == 0)
    {
        const Object object = asm_object(&assembled, BUSTER_S8("groups.s"), &test->vector);
        ObjectView view;
        char error[256] = {0};
        const bool opened = object.error == OBJECT_ERROR_NONE && object_view_open(&view, object.bytes, object.size, false, error, sizeof(error));
        asm_test_check(test, opened, "section group ELF invariants", error);
        uint32_t groups = 0;
        uint32_t macros = 0;
        uint32_t grouped_relocations = 0;
        for (uint64_t i = 1; opened && i < view.section_count; i += 1)
        {
            const Elf64_Shdr* section = &view.sections[i];
            const char* name = object_view_section_name(&view, i);
            const char* group = object_test_section_group(&view, i);
            if (section->sh_type == SHT_GROUP)
            {
                groups += 1;
                const Elf64_Sym* signature = &view.symbols[section->sh_info];
                const bool synthetic = strcmp(group, "macro_a") == 0 || strcmp(group, "macro_b") == 0 || strcmp(group, "tls_group") == 0 ||
                                       strcmp(group, "empty_group") == 0 || strcmp(group, "weak_signature") == 0 || strcmp(group, ".Lsignature") == 0;
                const uint32_t* words = (const uint32_t*)(view.file + section->sh_offset);
                asm_test_check(test, i == groups && words[0] == (strcmp(group, "plain_signature") == 0 ? 0 : GRP_COMDAT), "group prefix and selection flags", group);
                if (synthetic)
                {
                    asm_test_check(test, signature->st_info == ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE) && object_view_symbol_section(&view, section->sh_info) == i &&
                                   signature->st_value == 0 && signature->st_size == 0, "group-only local signature", group);
                }
            }
            if (strcmp(name, ".debug_macro") == 0)
            {
                macros += 1;
                const uint8_t* bytes = view.file + section->sh_offset;
                const bool first = group != 0 && strcmp(group, "macro_a") == 0;
                const bool second = group != 0 && strcmp(group, "macro_b") == 0;
                const bool correct = first ? section->sh_size == 6 && bytes[4] == 1 && bytes[5] == 4 :
                                     second ? section->sh_size == 5 && bytes[4] == 2 : group == 0 && section->sh_size == 1 && bytes[0] == 3;
                asm_test_check(test, correct, "same-name sections retain separate contents and return stack", group);
            }
            grouped_relocations += section->sh_type == SHT_RELA && group != 0;
        }
        asm_test_check(test, opened && groups == 9 && macros == 3 && grouped_relocations == 5, "group, duplicate-section and relocation inventory", error);

        // Malformed producer-owned metadata must be rejected before the writer indexes it.
        Link changed = assembled.link;
        ObjectUnits units;
        changed.groups.atom_count -= 1;
        asm_test_check(test, !object_prepare(&units, &changed, 1, &test->vector), "short group membership table is rejected", 0);
        changed = assembled.link;
        changed.groups.data = 0;
        asm_test_check(test, !object_prepare(&units, &changed, 1, &test->vector), "missing group descriptors are rejected", 0);
        changed = assembled.link;
        uint32_t* membership = BUSTER_ARENA_PUSH_ARRAY(&test->vector, uint32_t, changed.atoms.count);
        LinkGroup* records = BUSTER_ARENA_PUSH_ARRAY(&test->vector, LinkGroup, changed.groups.count);
        if (membership != 0 && records != 0)
        {
            memcpy(membership, changed.groups.atom_groups, changed.atoms.count * sizeof(uint32_t));
            membership[1] = changed.groups.count;
            changed.groups.atom_groups = membership;
            asm_test_check(test, !object_prepare(&units, &changed, 1, &test->vector), "out-of-range group is rejected", 0);
            changed = assembled.link;
            memcpy(records, changed.groups.data, changed.groups.count * sizeof(LinkGroup));
            records[1].unit = 1;
            changed.groups.data = records;
            asm_test_check(test, !object_prepare(&units, &changed, 2, &test->vector), "a group's members must share its unit", 0);
            records[1] = assembled.link.groups.data[1];
            records[1].flags = 2;
            asm_test_check(test, !object_prepare(&units, &changed, 1, &test->vector), "unsupported group flags are rejected", 0);
        }
        else
        {
            asm_test_check(test, false, "group mutation scratch", "arena exhausted");
        }
    }
}

// Two different implementations of one COMDAT function and its data/relocations. Both input orders must choose the
// first implementation and discard the other whole group, not just its code. A strong pointer in the selected group
// also checks that the duplicate symbol and relocation disappear together.
static void asm_test_group_link(AsmTest* test, const char* directory)
{
    char objects[2][PATH_MAX];
    char program[PATH_MAX];
    char source[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(source, sizeof(source), "%s/group-main.c", directory);
    snprintf(program, sizeof(program), "%s/group-program", directory);
    snprintf(output, sizeof(output), "%s/group-output", directory);
    snprintf(errors, sizeof(errors), "%s/group-errors", directory);
    static const char caller[] = "extern int pick(void); extern int (*pick_ptr)(void); int main(void) { return pick() != EXPECT || pick_ptr() != EXPECT; }\n";
    bool okay = object_write_file(source, (const uint8_t*)caller, sizeof(caller) - 1) == OBJECT_ERROR_NONE;
    for (uint32_t i = 0; okay && i < 2; i += 1)
    {
        snprintf(objects[i], sizeof(objects[i]), "%s/group-%u.o", directory, i);
        char assembly[512];
        const int length = snprintf(assembly, sizeof(assembly),
            ".section .text.pick,\"axG\",@progbits,pick,comdat\n.weak pick\n.type pick,@function\npick: movl $%u,%%eax\nret\n"
            ".section .data.pick,\"awG\",@progbits,pick,comdat\n.globl pick_ptr\n.type pick_ptr,@object\npick_ptr: .quad pick\n", i == 0 ? 42 : 99);
        AsmResult result = asm_test_pair(test, (String8){.pointer = assembly, .length = length}, 1, X86_ARCH_BASELINE);
        const Object object = asm_object(&result, BUSTER_S8("group.s"), &test->vector);
        okay = object.error == OBJECT_ERROR_NONE && object_write_file(objects[i], object.bytes, object.size) == OBJECT_ERROR_NONE;
    }
    for (uint32_t i = 0; okay && i < 2; i += 1)
    {
        char* arguments[] = {"cc", source, objects[i], objects[1 - i], i == 0 ? "-DEXPECT=42" : "-DEXPECT=99", "-o", program, 0};
        unlink(program);
        okay = asm_test_tool(arguments, program, output, errors) == 0;
        char* run[] = {program, 0};
        okay = okay && object_test_run(run, output, errors) == 0;
        asm_test_check(test, okay, "COMDAT code/data/relocation deduplication in both input orders", errors);
    }
    asm_test_check(test, okay, "COMDAT external link and execution", errors);
    unlink(source);
    unlink(program);
}

// Check names and declaration order independently. A reference may precede the first declaration, but GNU as
// versions disagree after another same-name section is created, so those bare references must be refused.
static void asm_test_group_section_names(AsmTest* test, const char* directory, bool cross_check)
{
    static const char* const names[] = {"plain_name", ".dotted_name"};
    static const char* const formats[] = {
        ".long %s\n.section %s,\"G\",@progbits,first,comdat\n.long %s\n.section %s,\"G\",@progbits,second,comdat\n.long %s\n.section %s,\"G\",@progbits,first,comdat\n.long %s\n",
        ".long %s\n.section %s,\"\",@progbits\n.long %s\n.section %s,\"G\",@progbits,first,comdat\n.long %s\n.section %s,\"\",@progbits\n.long %s\n",
        ".long %s\n.section %s,\"G\",@progbits,first,comdat\n.long %s\n.section %s,\"\",@progbits\n.long %s\n.section %s,\"G\",@progbits,first,comdat\n.long %s\n",
        ".section %s,\"\",@progbits\n.long %s\n.section marker,\"G\",@progbits,first,comdat\n.long %s\n.long %s\n.long %s\n.long %s\n.long %s\n",
    };
    for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(names); n += 1)
    {
        const char* name = names[n];
        for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(formats); f += 1)
        {
            char source[1024];
            const int length = snprintf(source, sizeof(source), formats[f], name, name, name, name, name, name, name);
            const bool fits = length >= 0 && (uint64_t)length < sizeof(source);
            asm_test_check(test, fits, "section-name matrix fits", 0);
            if (fits)
            {
                const bool accepted = f == 3;
                const AsmResult result = asm_test_pair(test, (String8){.pointer = source, .length = (uint64_t)length}, accepted, X86_ARCH_BASELINE);
                if (!accepted)
                {
                    asm_test_check(test, result.error != 0 && strcmp(result.error, "ambiguous section name in expression") == 0,
                                   "same-name section reference is diagnosed as ambiguous", result.error);
                }
                if (cross_check && accepted)
                {
                    asm_test_oracle(test, source, directory, X86_ARCH_BASELINE);
                }
            }
        }
    }
}

// Section-symbol metadata cannot currently be represented faithfully in the shared writer. Exercise directives
// both before and after declaration, for ordinary and dotted names, and require the specific diagnostic.
static void asm_test_group_section_attributes(AsmTest* test)
{
    static const char* const names[] = {"section_name", ".section_name"};
    static const struct { const char* directive; const char* suffix; } attributes[] = {
        {".globl", ""}, {".global", ""}, {".local", ""}, {".weak", ""},
        {".hidden", ""}, {".protected", ""}, {".internal", ""}, {".type", ",@function"}, {".size", ",4"},
    };
    for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(names); n += 1)
    {
        for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(attributes); a += 1)
        {
            for (uint32_t after = 0; after < 2; after += 1)
            {
                char section[128];
                char directive[128];
                char source[256];
                const int sec = snprintf(section, sizeof(section), ".section %s,\"G\",@progbits,g,comdat\n", names[n]);
                const int dir = snprintf(directive, sizeof(directive), "%s %s%s\n", attributes[a].directive, names[n], attributes[a].suffix);
                const int length = sec > 0 && dir > 0 && (uint64_t)sec < sizeof(section) && (uint64_t)dir < sizeof(directive) ?
                                   snprintf(source, sizeof(source), "%s%s", after ? section : directive, after ? directive : section) : -1;
                const bool fits = length > 0 && (uint64_t)length < sizeof(source);
                asm_test_check(test, fits, "section attribute fixture fits", 0);
                if (fits)
                {
                    const AsmResult result = asm_test_pair(test, (String8){.pointer = source, .length = (uint64_t)length}, 0, X86_ARCH_BASELINE);
                    asm_test_check(test, result.error != 0 && strcmp(result.error, "section symbol attributes are not supported") == 0,
                                   "section attributes are diagnosed", result.error);
                }
            }
        }
    }
}

static void asm_test_groups(AsmTest* test, const char* directory, bool cross_check)
{
    asm_test_group_section_attributes(test);
    asm_test_group_section_names(test, directory, cross_check);
    // One grouped section with a unique name is unambiguous, including a reference before its declaration.
    static const char unique_name[] = ".long plain\n.section plain,\"G\",@progbits,g,comdat\n.long plain\n";
    asm_test_pair(test, (String8){.pointer = (char*)unique_name, .length = sizeof(unique_name) - 1}, 1, X86_ARCH_BASELINE);
    if (cross_check)
    {
        asm_test_oracle(test, unique_name, directory, X86_ARCH_BASELINE);
    }
    // Explicit flags must retain the conventional array entry sizes. In particular,
    // the file-scope assembly path relies on accepting these before refusing their kind.
    static const char arrays[] = ".section .init_array,\"aw\"\n.quad 0\n.section .fini_array,\"aw\"\n.quad 0\n";
    test->scalar.length = 0;
    test->vector.length = 0;
    const String8 array_source = {.pointer = (char*)arrays, .length = sizeof(arrays) - 1};
    const AsmResult scalar_arrays = asm_assemble(array_source, test->index, (AsmOptions){0}, &test->scalar);
    const AsmResult vector_arrays = asm_assemble(array_source, test->index, (AsmOptions){.vectorized = true}, &test->vector);
    asm_test_check(test, scalar_arrays.error == 0 && vector_arrays.error == 0, "ordinary array section flags retain their entry size", vector_arrays.error);
    static const char* const section_signatures[] = {
        ".section foo,\"G\",@progbits,foo,comdat\n.long foo\n",
        ".section foo,\"\",@progbits\n.section bar,\"G\",@progbits,foo,comdat\n.byte 1\n",
        ".section bar,\"G\",@progbits,foo,comdat\n.byte 1\n.section foo,\"\",@progbits\n.byte 2\n",
        ".section bar,\"G\",@progbits,.data,comdat\n.byte 3\n",
        ".section foo,\"G\",@progbits,foo,comdat\n.byte 4\n",
        ".long foo\n.section foo,\"G\",@progbits,foo,comdat\n.byte 5\n",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(section_signatures); i += 1)
    {
        const char* source = section_signatures[i];
        AsmResult assembled = asm_test_pair(test, (String8){.pointer = (char*)source, .length = strlen(source)}, 1, X86_ARCH_BASELINE);
        if (assembled.error == 0)
        {
            const Object object = asm_object(&assembled, BUSTER_S8("section-signature.s"), &test->vector);
            ObjectView view;
            char error[256] = {0};
            const bool opened = object.error == OBJECT_ERROR_NONE && object_view_open(&view, object.bytes, object.size, false, error, sizeof(error));
            bool found = false;
            for (uint64_t j = 1; opened && j < view.section_count; j += 1)
            {
                const Elf64_Shdr* group = &view.sections[j];
                if (group->sh_type == SHT_GROUP)
                {
                    const Elf64_Sym* signature = &view.symbols[group->sh_info];
                    const char* name = object_view_symbol_name(&view, group->sh_info);
                    found = ELF64_ST_TYPE(signature->st_info) == STT_SECTION && name != 0 && strcmp(name, i == 3 ? ".data" : "foo") == 0;
                }
            }
            asm_test_check(test, opened && found, "group uses a named section symbol as signature", error);
            Link broken = assembled.link;
            LinkGroup* groups = BUSTER_ARENA_PUSH_ARRAY(&test->vector, LinkGroup, broken.groups.count);
            if (groups != 0)
            {
                memcpy(groups, broken.groups.data, broken.groups.count * sizeof(LinkGroup));
                broken.groups.data = groups;
                groups[1].signature_atom = broken.atoms.count;
                asm_test_check(test, !link_groups_valid(&broken), "section signature atom out of range is rejected", 0);
                if (broken.symbols.count > 1)
                {
                    groups[1] = assembled.link.groups.data[1];
                    groups[1].signature = 1;
                    asm_test_check(test, !link_groups_valid(&broken), "conflicting section and symbol signatures are rejected", 0);
                    groups[1] = assembled.link.groups.data[1];
                    if (broken.symbols.atom[1] == groups[1].signature_atom)
                    {
                        uint8_t* flags = BUSTER_ARENA_PUSH_ARRAY(&test->vector, uint8_t, broken.symbols.count);
                        if (flags != 0)
                        {
                            memcpy(flags, broken.symbols.flags, broken.symbols.count);
                            flags[1] = (flags[1] & ~BUSTER_LINK_SYMBOL_TYPE) | BUSTER_LINK_SYMBOL_COMMON;
                            broken.symbols.flags = flags;
                            ObjectUnits units;
                            asm_test_check(test, !object_prepare(&units, &broken, 1, &test->vector), "a COMMON atom cannot be a section signature", 0);
                        }
                        else
                        {
                            asm_test_check(test, false, "section signature common mutation scratch", "arena exhausted");
                        }
                    }
                }
            }
            else
            {
                asm_test_check(test, false, "section signature mutation scratch", "arena exhausted");
            }
        }
        if (cross_check)
        {
            asm_test_oracle(test, source, directory, X86_ARCH_BASELINE);
        }
    }
    asm_test_group_metadata(test);
    asm_test_pair(test, (String8){.pointer = (char*)asm_test_group_signatures, .length = sizeof(asm_test_group_signatures) - 1}, 1, X86_ARCH_BASELINE);
    static const char* const rejected[] = {
        ".section .x,\"G\",@progbits\n",
        ".section .x,\"G\",@progbits,\n",
        ".section .x,\"G\",@progbits,\"\",comdat\n",
        ".section .x,\"G\",@progbits,g,unknown\n",
        ".section .x,\"G\",@progbits,g,comdat,extra\n",
        ".section .x,\"G\",@progbits,g,comdat\n.section .y,\"G\",@progbits,g\n",
        ".section .x,\"aG\",@progbits,g,comdat\n.section .x,\"awG\",@progbits,g,comdat\n",
        ".section .x,\"aMG\",@progbits,0,g,comdat\n",
        ".section .x,\"G\",@progbits,g,comdat\n.set g,1\n",
        "foo: nop\n.section foo,\"G\",@progbits,g,comdat\n",
        ".comm foo,8,8\n.section foo,\"G\",@progbits,g,comdat\n",
        ".section foo,\"G\",@progbits,g,comdat\nfoo: nop\n",
        ".section foo,\"G\",@progbits,g,comdat\n.comm foo,8,8\n",
        ".section foo,\"G\",@progbits,g,comdat\n.lcomm foo,8\n",
        ".section foo,\"G\",@progbits,g,comdat\n.set foo,1\n",
        ".section foo,\"G\",@progbits,foo,comdat\n.weak foo\n",
        ".section foo,\"G\",@progbits,g,comdat\n.section foo,\"\",@progbits\n.section bar,\"G\",@progbits,foo,comdat\n",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rejected); i += 1)
    {
        asm_test_pair(test, (String8){.pointer = (char*)rejected[i], .length = strlen(rejected[i])}, 0, X86_ARCH_BASELINE);
    }
    // Enough same-name groups to grow and rehash the section index several times, then revisit each one.
    char many[16384];
    uint32_t length = 0;
    for (uint32_t pass = 0; pass < 2; pass += 1)
    {
        for (uint32_t i = 0; i < 96; i += 1)
        {
            length += snprintf(many + length, sizeof(many) - length, ".section .debug_macro,\"G\",@progbits,g%u,comdat\n.byte %u\n", i, i + pass);
        }
    }
    asm_test_pair(test, (String8){.pointer = many, .length = length}, 1, X86_ARCH_BASELINE);
    if (cross_check)
    {
        asm_test_oracle(test, asm_test_groups_fixture, directory, X86_ARCH_BASELINE);
        asm_test_oracle(test, many, directory, X86_ARCH_BASELINE);
        asm_test_oracle(test, asm_test_group_signatures, directory, X86_ARCH_BASELINE);
        asm_test_group_link(test, directory);
    }
}
