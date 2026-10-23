// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original assembly -> foreign resolution -> Link conversion -> string merging -> native executable. The programs
// inspect the relocated bytes, interior aliases and requested alignment. Exact/vector producers and writers at
// 1/2/3/8 lanes must emit identical images. Section-PC and named-PC32/CALL references exercise unchanged unmerged
// fallback; named data-PC64 stays within one entry. GNU ld and lld execute the original programs with --cross-check.

static const char foreign_strings_output_entry[] =
    ".text\n.globl _start\n.type _start,@function\n_start:\n"
    "movabs $first_prefix,%rax\ncmpb $112,(%rax)\njne .Lfail\ncmpb $0,6(%rax)\njne .Lfail\n"
    "movabs $first_shared,%r8\ncmpb $115,(%r8)\njne .Lfail\ncmpb $0,6(%r8)\njne .Lfail\n"
    "movabs $first_alias,%rax\nlea 2(%r8),%rcx\ncmp %rcx,%rax\njne .Lfail\ncmpb $97,(%rax)\njne .Lfail\n"
    "movabs $first_shared_repeat,%rax\ncmpb $115,(%rax)\njne .Lfail\n"
    "mov first_section_pointer(%rip),%rax\ncmpb $97,(%rax)\njne .Lfail\n"
    "mov first_section32(%rip),%eax\ncmpb $116,(%rax)\njne .Lfail\n"
    "mov first_named_pointer(%rip),%rax\ncmpb $104,(%rax)\njne .Lfail\n"
    "lea first_pc64(%rip),%rax\nadd (%rax),%rax\ncmpb $97,(%rax)\njne .Lfail\n"
    "mov $first_shared+2,%rax\ncmpb $97,(%rax)\njne .Lfail\n"
    "mov first_shared@GOTPCREL(%rip),%rax\ncmpb $115,(%rax)\njne .Lfail\n"
    "cmpq $7,first_size(%rip)\njne .Lfail\n"
    "movabs $first_other,%rax\ncmpb $115,(%rax)\njne .Lfail\n"
    "movabs $first_wide,%rax\ncmpw $256,(%rax)\njne .Lfail\ncmpw $0,2(%rax)\njne .Lfail\n"
    "movabs $first_wide_alias,%rax\ncmpb $1,(%rax)\njne .Lfail\n"
    "mov first_wide_pointer(%rip),%rax\ncmpb $1,(%rax)\njne .Lfail\n"
    "movabs $first_wide4,%rax\ncmpl $65536,(%rax)\njne .Lfail\ncmpl $0,4(%rax)\njne .Lfail\n"
    "movabs $first_wide4_alias,%rax\ncmpb $1,(%rax)\njne .Lfail\n"
    "mov first_wide4_pointer(%rip),%rax\ncmpb $1,(%rax)\njne .Lfail\n"
    "call strings_check_second\ntest %eax,%eax\njne .Lfail\nmov $42,%edi\njmp .Lexit\n"
    ".Lfail:\nmov $101,%edi\n.Lexit:\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
    ".section .rodata.str1.1,\"aMS\",@progbits,1\n"
    ".globl first_prefix\n.type first_prefix,@object\nfirst_prefix:\n.asciz \"prefix\"\n.size first_prefix,7\n.globl first_shared\n.type first_shared,@object\nfirst_shared:\n.asciz \"shared\"\n.size first_shared,7\n"
    ".globl first_alias\n.type first_alias,@object\n.set first_alias,first_shared+2\n.size first_alias,3\n"
    ".asciz \"tail\"\n.globl first_shared_repeat\n.type first_shared_repeat,@object\nfirst_shared_repeat:\n.asciz \"shared\"\n.size first_shared_repeat,7\n.asciz \"shared\"\n"
    ".section .rodata.str.other,\"aMS\",@progbits,1\n.globl first_other\n.type first_other,@object\nfirst_other:\n.asciz \"shared\"\n.size first_other,7\n.asciz \"shared\"\n"
    ".section .rodata.str2.2,\"aMS\",@progbits,2\n.balign 2\n.globl first_wide\n.type first_wide,@object\nfirst_wide:\n.short 256,0\n.size first_wide,4\n.short 1,0,256,0\n"
    ".globl first_wide_alias\n.type first_wide_alias,@object\n.set first_wide_alias,first_wide+1\n.size first_wide_alias,1\n"
    ".section .rodata.str4.4,\"aMS\",@progbits,4\n.balign 4\n.globl first_wide4\n.type first_wide4,@object\nfirst_wide4:\n.long 65536,0\n.size first_wide4,8\n.long 65536,0\n"
    ".globl first_wide4_alias\n.type first_wide4_alias,@object\n.set first_wide4_alias,first_wide4+2\n.size first_wide4_alias,1\n"
    ".data\n.balign 8\nfirst_section_pointer:\n.quad .rodata.str1.1+9\nfirst_section32:\n.long .rodata.str1.1+14\n"
    ".balign 8\nfirst_named_pointer:\n.quad first_shared+1\nfirst_size:\n.quad first_shared@SIZE\n"
    "first_wide_pointer:\n.quad first_wide+1\nfirst_wide4_pointer:\n.quad first_wide4+2\n"
    "first_pc64:\n.quad first_shared+2-.\n"
    ".section .note.GNU-stack,\"\",@progbits\n";

static const char foreign_strings_output_second_begin[] =
    ".text\n.globl strings_check_second\n.type strings_check_second,@function\nstrings_check_second:\n"
    "movabs $second_shared,%rax\ntest $15,%eax\njne .Lbad\ncmpb $115,(%rax)\njne .Lbad\ncmpb $0,6(%rax)\njne .Lbad\n"
    "movabs $second_alias,%rax\ncmpb $104,(%rax)\njne .Lbad\n";

static const char foreign_strings_output_second_end[] =
    "cmpb $115,(%rax)\njne .Lbad\n"
    "mov second_section_pointer(%rip),%rax\ncmpb $104,(%rax)\njne .Lbad\n"
    "mov second_section32(%rip),%eax\ncmpb $120,(%rax)\njne .Lbad\n"
    "mov second_named_pointer(%rip),%rax\ncmpb $114,(%rax)\njne .Lbad\n"
    "mov second_shared@GOTPCREL(%rip),%rax\ncmpb $115,(%rax)\njne .Lbad\n"
    "cmpq $7,second_size(%rip)\njne .Lbad\n"
    "movabs $second_other,%rax\ncmpb $115,(%rax)\njne .Lbad\n"
    "movabs $second_wide,%rax\ncmpw $256,(%rax)\njne .Lbad\ncmpw $0,2(%rax)\njne .Lbad\n"
    "movabs $second_wide4,%rax\ncmpl $65536,(%rax)\njne .Lbad\ncmpl $0,4(%rax)\njne .Lbad\nxor %eax,%eax\nret\n"
    ".Lbad:\nmov $101,%eax\nret\n.size strings_check_second,.-strings_check_second\n"
    ".section .rodata.str1.1,\"aMS\",@progbits,1\n.balign 16\n.globl second_shared\n.type second_shared,@object\nsecond_shared:\n.asciz \"shared\"\n.size second_shared,7\n"
    ".globl second_alias\n.type second_alias,@object\n.set second_alias,second_shared+1\n.size second_alias,2\n.asciz \"x\"\n"
    ".globl second_shared_repeat\n.type second_shared_repeat,@object\nsecond_shared_repeat:\n.asciz \"shared\"\n.size second_shared_repeat,7\n.asciz \"shared\"\n"
    ".section .rodata.str.other,\"aMS\",@progbits,1\n.globl second_other\n.type second_other,@object\nsecond_other:\n.asciz \"shared\"\n.size second_other,7\n"
    ".section .rodata.str2.2,\"aMS\",@progbits,2\n.balign 2\n.globl second_wide\n.type second_wide,@object\nsecond_wide:\n.short 256,0\n.size second_wide,4\n"
    ".section .rodata.str4.4,\"aMS\",@progbits,4\n.balign 4\n.globl second_wide4\n.type second_wide4,@object\nsecond_wide4:\n.long 65536,0\n.size second_wide4,8\n"
    ".data\n.balign 8\nsecond_section_pointer:\n.quad .rodata.str1.1+10\nsecond_section32:\n.long .rodata.str1.1+7\n"
    ".balign 8\nsecond_named_pointer:\n.quad second_shared+3\nsecond_size:\n.quad second_shared@SIZE\n"
    ".section .note.GNU-stack,\"\",@progbits\n";

typedef struct ForeignStringsOutputFixture ForeignStringsOutputFixture;
struct ForeignStringsOutputFixture
{
    const char* name;
    const char* reference;
    uint32_t named_pc_mask;
    bool section_pc;
    bool call;
};

static const ForeignStringsOutputFixture foreign_strings_output_fixtures[] = {
    {
        .name = "fs_second",
        .reference = "movabs $second_shared_repeat,%rax\n",
    },
    {
        .name = "fs_fallback",
        .reference = "lea .rodata.str1.1+9(%rip),%rax\n",
        .section_pc = true,
    },
    {
        .name = "fs_named_negative",
        .reference = "lea second_shared_repeat(%rip),%rax\n",
        .named_pc_mask = 1,
    },
    {
        .name = "fs_named_zero",
        .reference = "lea second_shared_repeat+4(%rip),%rax\nsub $4,%rax\n",
        .named_pc_mask = 2,
    },
    {
        .name = "fs_named_positive",
        .reference = "lea second_shared_repeat+5(%rip),%rax\nsub $5,%rax\n",
        .named_pc_mask = 4,
    },
    {
        .name = "fs_named_call",
        // The original relocation must survive and cause fallback; its string bytes are never executed.
        .reference = "jmp .Lafter_literal_call\ncall second_shared_repeat@PLT\n.Lafter_literal_call:\nmovabs $second_shared_repeat,%rax\n",
        .call = true,
    },
};

static bool foreign_strings_output_compile(ForeignTestTally* tally, const char* directory)
{
    bool result = true;
    for (uint32_t source = 0; source <= BUSTER_ARRAY_LENGTH(foreign_strings_output_fixtures); source += 1)
    {
        char assembly[16384];
        const char* name = source == 0 ? "fs_entry" : foreign_strings_output_fixtures[source - 1].name;
        const char* reference = source == 0 ? "" : foreign_strings_output_fixtures[source - 1].reference;
        const int size = source == 0 ? snprintf(assembly, sizeof(assembly), "%s", foreign_strings_output_entry) :
                                      snprintf(assembly, sizeof(assembly), "%s%s%s", foreign_strings_output_second_begin, reference, foreign_strings_output_second_end);
        ForeignTestPath input;
        ForeignTestPath object;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(input, sizeof(input), "%s/%s.s", directory, name);
        snprintf(object, sizeof(object), "%s/%s.o", directory, name);
        snprintf(output, sizeof(output), "%s/%s.compile.out", directory, name);
        snprintf(errors, sizeof(errors), "%s/%s.compile.err", directory, name);
        char* arguments[] = {
            (char*)"cc",
            (char*)"-c",
            input,
            (char*)"-o",
            object,
            0,
        };
        const bool written = size > 0 && (uint64_t)size < sizeof(assembly) && foreign_test_write(input, assembly, (uint64_t)size);
        const bool compiled = written && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, compiled, "assemble original string consumer", name);
        result = compiled && result;
    }
    return result;
}

// Check the input's actual relocation metadata, so assembly simplification cannot silently remove the expressions
// this fixture is meant to cover. Each fallback has its own unsupported form; the supported input has none.
static void foreign_strings_output_relocations(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* converted, uint32_t fixture)
{
    uint32_t section_absolute = 0;
    uint32_t section_pc = 0;
    uint32_t named_pc_mask = 0;
    uint32_t named_pc64 = 0;
    uint32_t calls = 0;
    uint32_t named_got = 0;
    for (uint32_t object = 1; object < foreign->object_count; object += 1)
    {
        const ForeignObject* original = &foreign->objects[object];
        const ImportObject* imported = &converted->objects[object];
        uint32_t wide_widths = 0;
        bool wide_rodata = true;
        for (uint32_t section = 1; section < imported->section_count; section += 1)
        {
            const Elf64_Shdr header = import_section_header(&original->elf, section);
            if (header.sh_type == SHT_PROGBITS && (header.sh_flags & (SHF_MERGE | SHF_STRINGS)) == (SHF_MERGE | SHF_STRINGS) &&
                (header.sh_entsize == 2 || header.sh_entsize == 4))
            {
                const AtomId atom = imported->section_atoms[section];
                wide_widths |= header.sh_entsize == 2 ? 1u : 2u;
                wide_rodata = wide_rodata && atom != 0 && atom < converted->link.atoms.count &&
                              converted->link.atoms.class[atom] == BUSTER_LINK_CLASS_RODATA && converted->link.atoms.size[atom] == header.sh_size;
            }
        }
        foreign_test_expect(tally, wide_rodata && wide_widths == 3, "generic import retains both wide string sections as RODATA before final-output merging",
                            object == 1 ? "first input" : "second input");
        for (uint32_t index = 1; index < imported->symbol_count; index += 1)
        {
            const SymbolId symbol = imported->symbols[index];
            const AtomId atom = symbol != 0 ? converted->link.symbols.atom[symbol] : 0;
            if (atom != 0 && converted->link.atoms.class[atom] == BUSTER_LINK_CLASS_STRINGS)
            {
                const Elf64_Sym input = import_symbol(&original->symbols, index);
                const bool section = ELF64_ST_TYPE(input.st_info) == STT_SECTION;
                for (FixupId fixup = 1; fixup < converted->link.fixups.count; fixup += 1)
                {
                    const uint8_t kind = converted->link.fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
                    if (converted->link.fixups.target[fixup] == symbol)
                    {
                        section_absolute += section && (kind == BUSTER_LINK_FIXUP_ABS32 || kind == BUSTER_LINK_FIXUP_ABS32S || kind == BUSTER_LINK_FIXUP_ABS64) ? 1 : 0;
                        section_pc += section && (kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_PC64) ? 1 : 0;
                        const int64_t addend = link_fixup_addend(&converted->link, fixup);
                        if (!section && kind == BUSTER_LINK_FIXUP_PC32)
                        {
                            named_pc_mask |= addend < 0 ? 1u : addend == 0 ? 2u : 4u;
                        }
                        named_pc64 += !section && kind == BUSTER_LINK_FIXUP_PC64 && addend == 2 ? 1 : 0;
                        calls += kind == BUSTER_LINK_FIXUP_CALL ? 1 : 0;
                        named_got += !section && (kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL) ? 1 : 0;
                    }
                }
            }
        }
    }
    const ForeignStringsOutputFixture* expected = &foreign_strings_output_fixtures[fixture];
    foreign_test_expect(tally, section_absolute >= 4 && named_pc64 != 0 && named_got != 0 && named_pc_mask == expected->named_pc_mask &&
                              (expected->section_pc ? section_pc != 0 : section_pc == 0) && (expected->call ? calls != 0 : calls == 0),
                        "ELF retains absolute/data-PC64/GOT forms and the precise unsupported PC32/CALL case", expected->name);
}

static void foreign_strings_output_structure(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* converted, const ForeignStringsMerge* merged)
{
    const Link* link = &converted->link;
    const SymbolId first = foreign_link_test_symbol(foreign, converted, "first_shared");
    const SymbolId first_repeat = foreign_link_test_symbol(foreign, converted, "first_shared_repeat");
    const SymbolId second = foreign_link_test_symbol(foreign, converted, "second_shared");
    const SymbolId second_repeat = foreign_link_test_symbol(foreign, converted, "second_shared_repeat");
    const SymbolId other = foreign_link_test_symbol(foreign, converted, "first_other");
    const SymbolId second_other = foreign_link_test_symbol(foreign, converted, "second_other");
    const SymbolId alias = foreign_link_test_symbol(foreign, converted, "first_alias");
    const SymbolId wide = foreign_link_test_symbol(foreign, converted, "first_wide");
    const SymbolId wide_second = foreign_link_test_symbol(foreign, converted, "second_wide");
    const SymbolId wide_alias = foreign_link_test_symbol(foreign, converted, "first_wide_alias");
    const SymbolId wide4 = foreign_link_test_symbol(foreign, converted, "first_wide4");
    const SymbolId wide4_second = foreign_link_test_symbol(foreign, converted, "second_wide4");
    const SymbolId wide4_alias = foreign_link_test_symbol(foreign, converted, "first_wide4_alias");
    const bool found = first != 0 && first_repeat != 0 && second != 0 && second_repeat != 0 && other != 0 && second_other != 0 && alias != 0 &&
                       wide != 0 && wide_second != 0 && wide_alias != 0 && wide4 != 0 && wide4_second != 0 && wide4_alias != 0;
    bool valid = found && merged->applied && merged->strings.size < merged->input_bytes && link->symbols.atom[first] == link->symbols.atom[second] &&
                 link->symbols.value[first] == link->symbols.value[first_repeat] && link->symbols.value[first] == link->symbols.value[second_repeat] &&
                 link->symbols.value[first] != link->symbols.value[other] && link->symbols.value[other] == link->symbols.value[second_other] &&
                 link->symbols.value[alias] == link->symbols.value[first] + 2 && (link->symbols.value[second] & 15) == 0 &&
                 link->symbols.atom[wide] == link->symbols.atom[first] && link->symbols.atom[wide4] == link->symbols.atom[first] &&
                 link->symbols.atom[wide_second] == link->symbols.atom[first] && link->symbols.atom[wide4_second] == link->symbols.atom[first] &&
                 link->symbols.value[wide] == link->symbols.value[wide_second] && link->symbols.value[wide_alias] == link->symbols.value[wide] + 1 &&
                 link->symbols.value[wide4] == link->symbols.value[wide4_second] && link->symbols.value[wide4_alias] == link->symbols.value[wide4] + 2 &&
                 link->atoms.alignment_log2[link->symbols.atom[first]] == 4;
    uint32_t empty = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        empty += merged->atom_inputs[atom] != 0 && link->atoms.size[atom] == 0 ? 1 : 0;
    }
    valid = valid && empty >= 7;
    foreign_test_expect(tally, valid, "stable 1/2/4-byte duplicates, distinct groups, byte-interior aliases and stronger alignment", 0);
}

static void foreign_strings_output_oracles(ForeignTestTally* tally, const char* directory, char** inputs, uint32_t fixture)
{
    static const char* const linkers[] = {
        "ld.bfd",
        "ld.lld",
    };
    for (uint32_t linker = 0; linker < BUSTER_ARRAY_LENGTH(linkers); linker += 1)
    {
        ForeignTestPath executable;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(executable, sizeof(executable), "%s/fs_oracle_%u_%u", directory, fixture, linker);
        snprintf(output, sizeof(output), "%s/fs_oracle_%u_%u.out", directory, fixture, linker);
        snprintf(errors, sizeof(errors), "%s/fs_oracle_%u_%u.err", directory, fixture, linker);
        char* arguments[] = {
            (char*)linkers[linker],
            (char*)"-static",
            (char*)"-e",
            (char*)"_start",
            (char*)"-o",
            executable,
            inputs[0],
            inputs[1],
            0,
        };
        const int link_status = object_test_run(arguments, output, errors);
        char* run[] = {
            executable,
            0,
        };
        const int run_status = link_status == 0 ? object_test_run(run, output, errors) : -1;
        char detail[192];
        snprintf(detail, sizeof(detail), "%s / %s: link status %d, run status %d", linkers[linker], foreign_strings_output_fixtures[fixture].name, link_status, run_status);
        foreign_test_expect(tally, link_status == 0 && run_status == 42, "external linker executes the same original string references", detail);
    }
}

static void foreign_strings_output_program(ForeignTestTally* tally, const char* directory, Arena* arena, bool cross_check, uint32_t fixture)
{
    const uint64_t saved = arena->length;
    const bool fallback = fixture != 0;
    char object_name[64];
    snprintf(object_name, sizeof(object_name), "%s.o", foreign_strings_output_fixtures[fixture].name);
    char* inputs[] = {
        foreign_test_path(directory, "fs_entry.o", arena),
        foreign_test_path(directory, object_name, arena),
    };
    ForeignCommand command = { 0 };
    const bool prepared = inputs[0] != 0 && inputs[1] != 0 && foreign_test_command(&command, inputs, BUSTER_ARRAY_LENGTH(inputs), arena);
    foreign_test_expect(tally, prepared, "prepare string executable command", 0);
    command.partition_records = BUSTER_FOREIGN_TEST_PARTITION_RECORDS;
    ForeignTestPath reference_path;
    snprintf(reference_path, sizeof(reference_path), "%s/fs_reference_%u", directory, fixture);
    String8 reference = { 0 };
    for (uint32_t path = 0; prepared && path < 2; path += 1)
    {
        for (uint32_t lane = 0; lane < BUSTER_ARRAY_LENGTH(foreign_test_lanes); lane += 1)
        {
            const uint64_t iteration = arena->length;
            Foreign foreign;
            foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[lane], 0);
            Arena names = arena_reserve(1ull << 20);
            Arena snapshots = arena_reserve(1ull << 22);
            ForeignLink converted = { 0 };
            const ForeignLinkResult built = foreign_link_build(&converted, &foreign, arena, &names);
            char detail[128];
            snprintf(detail, sizeof(detail), "%s, %s, %u lanes: conversion %u", foreign_strings_output_fixtures[fixture].name, path == 0 ? "exact" : "vector",
                     foreign_test_lanes[lane], (unsigned)built.error);
            bool ready = foreign.failure.error == FOREIGN_ERROR_NONE && built.error == FOREIGN_LINK_ERROR_NONE && snapshots.base != 0 && foreign.object_count == 3;
            foreign_test_expect(tally, ready, "resolve and convert original objects", detail);
            if (ready)
            {
                foreign_strings_output_relocations(tally, &foreign, &converted, fixture);
                ForeignLink before;
                memcpy(&before, &converted, sizeof(before));
                const uint64_t before_length = arena->length;
                const uint64_t columns_size = before_length - iteration;
                const uint64_t names_length = names.length;
                uint8_t* columns = arena_push(&snapshots, columns_size, 1);
                uint8_t* name_bytes = arena_push(&snapshots, names_length, 1);
                String8 originals[3] = { 0 };
                ready = columns != 0 && name_bytes != 0;
                if (ready)
                {
                    memcpy(columns, arena->base + iteration, columns_size);
                    memcpy(name_bytes, names.base, names_length);
                    for (uint32_t object = 1; ready && object < foreign.object_count; object += 1)
                    {
                        originals[object].length = foreign.objects[object].bytes.length;
                        originals[object].pointer = arena_push(&snapshots, originals[object].length, 1);
                        ready = originals[object].pointer != 0;
                        if (ready)
                        {
                            memcpy(originals[object].pointer, foreign.objects[object].bytes.pointer, originals[object].length);
                        }
                    }
                }
                foreign_test_expect(tally, ready, "snapshot Link, import maps and original ELF bytes", detail);
                if (ready)
                {
                    ForeignStringsMerge merged;
                    ForeignStringsMerge sentinel;
                    memset(&merged, 0xa5, sizeof(merged));
                    memcpy(&sentinel, &merged, sizeof(merged));
                    const ForeignStringsError error = foreign_strings_merge(&converted, &foreign, arena, &merged);
                    const bool accepted = fallback ? error == FOREIGN_STRINGS_ERROR_UNSUPPORTED : error == FOREIGN_STRINGS_ERROR_NONE && merged.applied;
                    foreign_test_expect(tally, accepted, "merge supported references or refuse ambiguous PC32/CALL without mutation", detail);
                    if (fallback)
                    {
                        const bool unchanged = memcmp(&converted, &before, sizeof(before)) == 0 && arena->length == before_length &&
                                               memcmp(arena->base + iteration, columns, columns_size) == 0 && memcmp(&merged, &sentinel, sizeof(merged)) == 0;
                        foreign_test_expect(tally, unchanged, "unsupported merge preserves every Link/import column, arena mark and output descriptor", detail);
                    }
                    else if (accepted)
                    {
                        foreign_strings_output_structure(tally, &foreign, &converted, &merged);
                    }
                    bool source_same = names.length == names_length && memcmp(names.base, name_bytes, names_length) == 0;
                    for (uint32_t object = 1; object < foreign.object_count; object += 1)
                    {
                        source_same = source_same && originals[object].length == foreign.objects[object].bytes.length &&
                                      memcmp(originals[object].pointer, foreign.objects[object].bytes.pointer, originals[object].length) == 0;
                        const ImportObject* imported = &converted.objects[object];
                        const uint64_t sections_at = (uint8_t*)imported->section_atoms - (arena->base + iteration);
                        const uint64_t symbols_at = (uint8_t*)imported->symbols - (arena->base + iteration);
                        const uint64_t sections_size = (uint64_t)imported->section_count * sizeof(*imported->section_atoms);
                        const uint64_t symbols_size = (uint64_t)imported->symbol_count * sizeof(*imported->symbols);
                        source_same = source_same && sections_at <= columns_size && sections_size <= columns_size - sections_at &&
                                      symbols_at <= columns_size && symbols_size <= columns_size - symbols_at;
                        if (source_same)
                        {
                            source_same = memcmp(imported->section_atoms, columns + sections_at, sections_size) == 0 &&
                                          memcmp(imported->symbols, columns + symbols_at, symbols_size) == 0;
                        }
                    }
                    foreign_test_expect(tally, source_same, "ELF inputs, names and original section/symbol maps remain unchanged", detail);
                    ForeignTestPath executable;
                    ForeignTestPath output;
                    ForeignTestPath errors;
                    const bool first = path == 0 && lane == 0;
                    snprintf(executable, sizeof(executable), "%s/fs_variant_%u_%u_%u", directory, fixture, path, foreign_test_lanes[lane]);
                    snprintf(output, sizeof(output), "%s/fs_run_%u_%u_%u.out", directory, fixture, path, foreign_test_lanes[lane]);
                    snprintf(errors, sizeof(errors), "%s/fs_run_%u_%u_%u.err", directory, fixture, path, foreign_test_lanes[lane]);
                    char* destination = first ? reference_path : executable;
                    const String8 destination_name = {
                        .pointer = destination,
                        .length = strlen(destination),
                    };
                    const SymbolId entry = foreign_link_test_symbol(&foreign, &converted, "_start");
                    ExecutableResult written = { 0 };
                    const bool emitted = accepted && entry != 0 && (path == 0 ? executable_test_write_exact(&converted.link, entry, destination_name, foreign_test_lanes[lane], &written) :
                                                                                  executable_test_write(&converted.link, entry, destination_name, foreign_test_lanes[lane], &written, 0, 0));
                    foreign_test_expect(tally, emitted, "write merged or unchanged fallback executable", detail);
                    char* run[] = {
                        destination,
                        0,
                    };
                    foreign_test_expect(tally, emitted && object_test_run(run, output, errors) == 42, "execute relocated strings, aliases, alignment, sizes and GOT", detail);
                    if (!first)
                    {
                        const String8 bytes = emitted ? foreign_test_read(arena, destination) : (String8){ 0 };
                        const bool same = reference.pointer != 0 && bytes.pointer != 0 && reference.length == bytes.length && memcmp(reference.pointer, bytes.pointer, bytes.length) == 0;
                        foreign_test_expect(tally, same, "exact/vector 1/2/3/8-lane executable bytes agree", detail);
                    }
                }
            }
            arena_release(&snapshots);
            arena_release(&names);
            foreign_release(&foreign);
            arena_pop_to(arena, iteration);
            if (path == 0 && lane == 0)
            {
                reference = foreign_test_read(arena, reference_path);
                foreign_test_expect(tally, reference.pointer != 0, "retain first string executable for byte identity", 0);
            }
        }
    }
    if (prepared && cross_check)
    {
        foreign_strings_output_oracles(tally, directory, inputs, fixture);
    }
    arena_pop_to(arena, saved);
}

static bool foreign_strings_output_test(const char* directory, Arena* arena, bool cross_check)
{
    ForeignTestTally tally = {
        .check = "foreign string executable handoff",
    };
    if (foreign_strings_output_compile(&tally, directory))
    {
        for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(foreign_strings_output_fixtures); fixture += 1)
        {
            foreign_strings_output_program(&tally, directory, arena, cross_check, fixture);
        }
    }
    return foreign_test_report(&tally);
}
