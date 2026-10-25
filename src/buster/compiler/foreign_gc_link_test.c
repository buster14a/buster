// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Production section-GC handoff tests. Included by foreign_test.c after foreign_link_test.c and its helpers.
// Original assembly supplies real import maps, SHF_GNU_RETAIN, priorities and zR/zPLR unwind records. The exact and
// vector adapters must produce the same compact representation and executable; GNU ld and lld run the same inputs.
// Input arena snapshots include all Link columns and input maps, and the original object is checked byte for byte.

static const ForeignLinkTestSource foreign_gc_link_test_sources[] = {
    {
        .name = "fgc_plain",
        .text =
            ".section .text.entry,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
            "call fgc_live\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
            ".section .text.dead,\"ax\",@progbits\n.globl fgc_dead\n.type fgc_dead,@function\nfgc_dead:\n"
            "mov fgc_dead_data(%rip),%eax\nret\n.size fgc_dead,.-fgc_dead\n"
            ".section .text.live,\"ax\",@progbits\n.globl fgc_live\n.type fgc_live,@function\nfgc_live:\n"
            "mov fgc_pointer(%rip),%rdx\nmov (%rdx),%eax\nret\n.size fgc_live,.-fgc_live\n"
            ".section .data.dead,\"aw\",@progbits\n.globl fgc_dead_data\nfgc_dead_data:\n.long 99\n"
            ".section .data.pointer,\"aw\",@progbits\n.globl fgc_pointer\nfgc_pointer:\n.quad fgc_value\n"
            ".section .rodata.value,\"a\",@progbits\n.globl fgc_value\nfgc_value:\n.long 42\n"
            ".section .data.export,\"aw\",@progbits\n.globl fgc_export\nfgc_export:\n.quad 17\n"
            ".section .data.force,\"aw\",@progbits\n.globl fgc_force\nfgc_force:\n.quad 18\n"
            ".section .data.manual,\"aw\",@progbits\n.globl fgc_manual\nfgc_manual:\n.quad 19\n"
            ".section .data.command,\"aw\",@progbits\n.globl fgc_command_root\nfgc_command_root:\n.quad 21\n"
            ".section .data.retain,\"awR\",@progbits\n.globl fgc_retain\nfgc_retain:\n.quad fgc_retain_target\n"
            ".section .data.retain_target,\"aw\",@progbits\n.globl fgc_retain_target\nfgc_retain_target:\n.quad 20\n"
            ".section .text.ctor,\"ax\",@progbits\n.globl fgc_ctor\n.type fgc_ctor,@function\nfgc_ctor:\nret\n.size fgc_ctor,.-fgc_ctor\n"
            ".section .text.dtor,\"ax\",@progbits\n.globl fgc_dtor\n.type fgc_dtor,@function\nfgc_dtor:\nret\n.size fgc_dtor,.-fgc_dtor\n"
            ".section .init_array.123,\"aw\",@init_array\n.balign 8\n.quad fgc_ctor\n"
            ".section .fini_array.456,\"aw\",@fini_array\n.balign 8\n.quad fgc_dtor\n"
            ".section .text.dynamic_init,\"ax\",@progbits\n.globl fgc_dynamic_init\n.type fgc_dynamic_init,@function\n"
            "fgc_dynamic_init:\nret\n.size fgc_dynamic_init,.-fgc_dynamic_init\n"
            ".section .text.dynamic_fini,\"ax\",@progbits\n.globl fgc_dynamic_fini\n.type fgc_dynamic_fini,@function\n"
            "fgc_dynamic_fini:\nret\n.size fgc_dynamic_fini,.-fgc_dynamic_fini\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fgc_unwind",
        .text =
            ".section .text.entry,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
            "call fgc_live\nmov %eax,%ebx\ncall fgc_live_lsda\nadd %ebx,%eax\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
            ".section .text.dead,\"ax\",@progbits\n.globl fgc_dead\n.type fgc_dead,@function\nfgc_dead:\n"
            ".cfi_startproc\nmov $99,%eax\nret\n.cfi_endproc\n.size fgc_dead,.-fgc_dead\n"
            ".section .text.live,\"ax\",@progbits\n.globl fgc_live\n.type fgc_live,@function\nfgc_live:\n"
            ".cfi_startproc\nmov $17,%eax\nret\n.cfi_endproc\n.size fgc_live,.-fgc_live\n"
            ".section .text.dead_lsda,\"ax\",@progbits\n.globl fgc_dead_lsda\n.type fgc_dead_lsda,@function\nfgc_dead_lsda:\n"
            ".cfi_startproc\n.cfi_personality 0x1b,fgc_dead_personality\n.cfi_lsda 0x1b,fgc_dead_table\n"
            "mov $98,%eax\nret\n.cfi_endproc\n.size fgc_dead_lsda,.-fgc_dead_lsda\n"
            ".section .text.live_lsda,\"ax\",@progbits\n.globl fgc_live_lsda\n.type fgc_live_lsda,@function\nfgc_live_lsda:\n"
            ".cfi_startproc\n.cfi_personality 0x1b,fgc_personality\n.cfi_lsda 0x1b,fgc_table\n"
            "mov $25,%eax\nret\n.cfi_endproc\n.size fgc_live_lsda,.-fgc_live_lsda\n"
            ".section .text.personality,\"ax\",@progbits\n.globl fgc_personality\n.type fgc_personality,@function\n"
            "fgc_personality:\nret\n.size fgc_personality,.-fgc_personality\n"
            ".section .text.dead_personality,\"ax\",@progbits\n.globl fgc_dead_personality\n.type fgc_dead_personality,@function\n"
            "fgc_dead_personality:\nret\n.size fgc_dead_personality,.-fgc_dead_personality\n"
            ".section .gcc_except_table.live,\"a\",@progbits\n.globl fgc_table\nfgc_table:\n.quad fgc_auxiliary\n"
            ".section .gcc_except_table.dead,\"a\",@progbits\n.globl fgc_dead_table\nfgc_dead_table:\n.quad fgc_dead_auxiliary\n"
            ".section .rodata.auxiliary,\"a\",@progbits\n.globl fgc_auxiliary\nfgc_auxiliary:\n.quad 33\n"
            ".section .rodata.dead_auxiliary,\"a\",@progbits\n.globl fgc_dead_auxiliary\nfgc_dead_auxiliary:\n.quad 34\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fgc_undefined",
        .text =
            ".section .text.entry,\"ax\",@progbits\n.globl _start\n.type _start,@function\n_start:\n"
            "call fgc_defined\nmov %eax,%edi\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
            ".section .text.defined,\"ax\",@progbits\n.globl fgc_defined\n.type fgc_defined,@function\n"
            "fgc_defined:\nmov $42,%eax\nret\n.size fgc_defined,.-fgc_defined\n"
            ".section .text.missing_caller,\"ax\",@progbits\n.globl fgc_missing_caller\n.type fgc_missing_caller,@function\n"
            "fgc_missing_caller:\n.cfi_startproc\ncall fgc_missing_strong\nret\n.cfi_endproc\n.size fgc_missing_caller,.-fgc_missing_caller\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
};

static bool foreign_gc_link_test_compile(const char* directory, ForeignTestTally* tally)
{
    bool result = true;
    ForeignTestPath source;
    ForeignTestPath object;
    ForeignTestPath output;
    ForeignTestPath errors;
    snprintf(output, sizeof(output), "%s/fgc_compile.out", directory);
    snprintf(errors, sizeof(errors), "%s/fgc_compile.err", directory);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(foreign_gc_link_test_sources); i += 1)
    {
        const ForeignLinkTestSource* fixture = &foreign_gc_link_test_sources[i];
        snprintf(source, sizeof(source), "%s/%s.s", directory, fixture->name);
        snprintf(object, sizeof(object), "%s/%s.o", directory, fixture->name);
        char* arguments[] = {
            (char*)"cc",
            (char*)"-c",
            (char*)"-Wa,-L",
            source,
            (char*)"-o",
            object,
            0,
        };
        const bool written = foreign_test_write(source, fixture->text, strlen(fixture->text));
        const bool compiled = written && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, compiled, "compile section GC fixture", fixture->name);
        result = compiled && result;
    }
    return result;
}

static AtomId foreign_gc_link_test_atom(const Foreign* foreign, const ForeignLink* converted, const char* name)
{
    const SymbolId symbol = foreign_link_test_symbol(foreign, converted, name);
    return symbol != 0 ? converted->link.symbols.atom[symbol] : 0;
}

static void foreign_gc_link_test_names(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* converted, const char* const* names, uint32_t count, bool kept)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const SymbolId symbol = foreign_link_test_symbol(foreign, converted, names[i]);
        foreign_test_expect(tally, (symbol != 0) == kept, kept ? "live name maps to compact symbol" : "dead name map is zero", names[i]);
        if (symbol != 0)
        {
            foreign_test_expect(tally, symbol < converted->link.symbols.count && converted->link.symbols.atom[symbol] != 0,
                                "kept definition names a compact atom", names[i]);
        }
    }
}

static void foreign_gc_link_test_maps(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* input, const ForeignLink* output)
{
    bool valid = input->names != output->names && input->objects != output->objects;
    for (uint32_t id = 1; valid && id < foreign->object_count; id += 1)
    {
        const ImportObject* before = &input->objects[id];
        const ImportObject* after = &output->objects[id];
        valid = before->section_count == after->section_count && before->symbol_count == after->symbol_count &&
                before->section_atoms != after->section_atoms && before->symbols != after->symbols;
        for (uint32_t section = 0; valid && section < after->section_count; section += 1)
        {
            const AtomId atom = after->section_atoms[section];
            valid = atom < output->link.atoms.count && (before->section_atoms[section] != 0 || atom == 0);
        }
        for (uint32_t symbol = 0; valid && symbol < after->symbol_count; symbol += 1)
        {
            const SymbolId mapped = after->symbols[symbol];
            valid = mapped < output->link.symbols.count && (before->symbols[symbol] != 0 || mapped == 0);
        }
        const ForeignObject* original = &foreign->objects[id];
        for (uint32_t i = 0; valid && i < original->global_count; i += 1)
        {
            uint32_t at = 0;
            const ForeignSymbols* symbols = foreign_test_symbols(foreign, original->global_begin + i, &at);
            valid = after->symbols[original->first_global + i] == output->names[symbols->name_id[at]];
        }
    }
    foreign_test_expect(tally, valid, "fresh input-index maps contain only compact IDs and agree on canonical globals", 0);
    Link checked = output->link;
    foreign_test_expect(tally, link_check(&checked) == LINK_ERROR_NONE, "compacted representation satisfies link invariants", 0);
    foreign_test_expect(tally, output->link.atoms.count < input->link.atoms.count && output->link.symbols.count < input->link.symbols.count,
                        "GC removes dead atoms and symbols", 0);
}

static void foreign_gc_link_test_structure(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* input, const ForeignLink* output, uint32_t fixture)
{
    static const char* const plain_kept[] = {
        "_start",
        "fgc_live",
        "fgc_pointer",
        "fgc_value",
        "fgc_export",
        "fgc_force",
        "fgc_manual",
        "fgc_command_root",
        "fgc_retain",
        "fgc_retain_target",
        "fgc_ctor",
        "fgc_dtor",
        "fgc_dynamic_init",
        "fgc_dynamic_fini",
    };
    static const char* const plain_dead[] = {
        "fgc_dead",
        "fgc_dead_data",
    };
    static const char* const unwind_kept[] = {
        "_start",
        "fgc_live",
        "fgc_live_lsda",
        "fgc_personality",
        "fgc_table",
        "fgc_auxiliary",
    };
    static const char* const unwind_dead[] = {
        "fgc_dead",
        "fgc_dead_lsda",
        "fgc_dead_personality",
        "fgc_dead_table",
        "fgc_dead_auxiliary",
    };
    foreign_gc_link_test_maps(tally, foreign, input, output);
    if (fixture == 0)
    {
        foreign_gc_link_test_names(tally, foreign, output, plain_kept, BUSTER_ARRAY_LENGTH(plain_kept), true);
        foreign_gc_link_test_names(tally, foreign, output, plain_dead, BUSTER_ARRAY_LENGTH(plain_dead), false);
        const ExecutableDynamic* dynamic = &output->dynamic;
        foreign_test_expect(tally, dynamic->export_count == 1 && dynamic->exports != input->dynamic.exports && dynamic->exports != 0 &&
                                  dynamic->exports[0] == foreign_link_test_symbol(foreign, output, "fgc_export") &&
                                  dynamic->init == foreign_link_test_symbol(foreign, output, "fgc_dynamic_init") &&
                                  dynamic->fini == foreign_link_test_symbol(foreign, output, "fgc_dynamic_fini"),
                            "dynamic exports and init/fini are roots and remap into fresh output", 0);
        const ExecutableDynamic* original = &input->dynamic;
        foreign_test_expect(tally, dynamic->interpreter.pointer == original->interpreter.pointer && dynamic->interpreter.length == original->interpreter.length &&
                                  dynamic->libraries == original->libraries && dynamic->library_count == original->library_count &&
                                  dynamic->versions == original->versions && dynamic->version_count == original->version_count &&
                                  dynamic->note.pointer == original->note.pointer && dynamic->note.length == original->note.length &&
                                  dynamic->soname.pointer == original->soname.pointer && dynamic->soname.length == original->soname.length &&
                                  dynamic->relr == original->relr && dynamic->relr_version == original->relr_version && dynamic->shared == original->shared,
                            "non-ID dynamic metadata is preserved", 0);
        const LinkPriorities* priorities = &output->link.priorities;
        bool ordered = priorities->count == 3;
        for (uint32_t p = 1; ordered && p < priorities->count; p += 1)
        {
            const AtomId atom = priorities->atom[p];
            ordered = atom != 0 && atom < output->link.atoms.count && priorities->priority[p] == (p == 1 ? 123 : 456) &&
                      output->link.atoms.class[atom] == (p == 1 ? BUSTER_LINK_CLASS_INIT_ARRAY : BUSTER_LINK_CLASS_FINI_ARRAY);
        }
        foreign_test_expect(tally, ordered, "array priorities retain their order and compact atom IDs", 0);
    }
    else
    {
        foreign_gc_link_test_names(tally, foreign, output, unwind_kept, BUSTER_ARRAY_LENGTH(unwind_kept), true);
        foreign_gc_link_test_names(tally, foreign, output, unwind_dead, BUSTER_ARRAY_LENGTH(unwind_dead), false);
        uint32_t frames = 0;
        uint64_t before = 0;
        uint64_t after = 0;
        for (AtomId atom = 1; atom < input->link.atoms.count; atom += 1)
        {
            before += input->link.atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME ? input->link.atoms.size[atom] : 0;
        }
        for (AtomId atom = 1; atom < output->link.atoms.count; atom += 1)
        {
            if (output->link.atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME)
            {
                after += output->link.atoms.size[atom];
                frames += executable_eh_frame_walk(&output->link, atom, 0, 0, 0);
            }
        }
        foreign_test_expect(tally, frames == 2 && after != 0 && after < before, "dead zR/zPLR records disappear and two live FDEs remain", 0);
    }
}

static bool foreign_gc_link_test_same(const Foreign* foreign, const ForeignLink* left, const ForeignLink* right)
{
    bool same = link_test_same(&left->link, &right->link) && memcmp(left->names, right->names, (uint64_t)foreign->names.count * sizeof(SymbolId)) == 0 &&
                left->link.priorities.count == right->link.priorities.count;
    for (uint32_t p = 1; same && p < left->link.priorities.count; p += 1)
    {
        same = left->link.priorities.atom[p] == right->link.priorities.atom[p] && left->link.priorities.priority[p] == right->link.priorities.priority[p];
    }
    for (uint32_t id = 1; same && id < foreign->object_count; id += 1)
    {
        const ImportObject* a = &left->objects[id];
        const ImportObject* b = &right->objects[id];
        same = a->section_count == b->section_count && a->symbol_count == b->symbol_count &&
               memcmp(a->section_atoms, b->section_atoms, (uint64_t)a->section_count * sizeof(AtomId)) == 0 &&
               memcmp(a->symbols, b->symbols, (uint64_t)a->symbol_count * sizeof(SymbolId)) == 0;
    }
    return same;
}

static void foreign_gc_link_test_refused(ForeignTestTally* tally, const Foreign* foreign, ForeignLink* input, const ForeignGcRoots* roots)
{
    Arena arena = arena_reserve(1ull << 20);
    Arena names = arena_reserve(1ull << 20);
    foreign_test_expect(tally, arena.base != 0 && names.base != 0, "reserve GC refusal arenas", 0);
    if (arena.base != 0 && names.base != 0)
    {
        ForeignLinkResult exact[5] = { 0 };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t failure = 0; failure < BUSTER_ARRAY_LENGTH(exact); failure += 1)
            {
                if (failure == 0 || failure == 1)
                {
                    Arena* exhausted = failure == 0 ? &arena : &names;
                    foreign_test_expect(tally, arena_push(exhausted, exhausted->capacity, 1) != 0, "exhaust test arena", 0);
                }
                const uint64_t saved = arena.length;
                const uint64_t saved_names = names.length;
                ForeignGcRoots invalid = *roots;
                const AtomId virtual_atom = input->link.atoms.count;
                if (failure == 2)
                {
                    invalid.entry = input->link.symbols.count;
                }
                else if (failure == 3)
                {
                    invalid.retained = &virtual_atom;
                    invalid.retained_count = 1;
                }
                ForeignLink output;
                memset(&output, 0xa5, sizeof(output));
                const ForeignLink original_output = output;
                const ForeignLink original_input = *input;
                ForeignLink* destination = failure == 4 ? input : &output;
                const ForeignLinkResult result = foreign_gc_link(destination, foreign, input, &invalid, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, &arena, &names);
                foreign_test_expect(tally, result.error != FOREIGN_LINK_ERROR_NONE, "invalid, aliased or exhausted GC output is refused", 0);
                if (path == 0)
                {
                    exact[failure] = result;
                }
                else
                {
                    const ForeignLinkResult* previous = &exact[failure];
                    foreign_test_expect(tally, result.error == previous->error && result.object == previous->object && result.name == previous->name &&
                                              result.import.error == previous->import.error && result.import.section == previous->import.section &&
                                              result.import.symbol == previous->import.symbol && result.import.relocation == previous->import.relocation &&
                                              result.import.type == previous->import.type && result.import.offset == previous->import.offset,
                                        "exact/vector refusal diagnostics agree", 0);
                }
                foreign_test_expect(tally, arena.length == saved && names.length == saved_names && memcmp(&output, &original_output, sizeof(output)) == 0 &&
                                          memcmp(input, &original_input, sizeof(*input)) == 0,
                                    "failure rolls both arenas back and leaves input/output unpublished", 0);
                arena_pop_to(&arena, 0);
                arena_pop_to(&names, 0);
            }
        }
    }
    arena_release(&arena);
    arena_release(&names);
}

static void foreign_gc_link_test_whole_unwind(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* input, const ForeignGcRoots* roots)
{
    Arena arena = arena_reserve(1ull << 20);
    Arena names = arena_reserve(1ull << 20);
    AtomId frame = 0;
    for (AtomId atom = 1; atom < input->link.atoms.count; atom += 1)
    {
        frame = input->link.atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME ? atom : frame;
    }
    ForeignGcRoots retained = *roots;
    retained.retained = &frame;
    retained.retained_count = 1;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        ForeignLink output = { 0 };
        const ForeignLinkResult result = foreign_gc_link(&output, foreign, input, &retained, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, &arena, &names);
        foreign_test_expect(tally, frame != 0 && result.error == FOREIGN_LINK_ERROR_NONE, "explicitly retained EH fragment is supported", 0);
        if (result.error == FOREIGN_LINK_ERROR_NONE)
        {
            uint32_t frames = 0;
            for (AtomId atom = 1; atom < output.link.atoms.count; atom += 1)
            {
                frames += output.link.atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME ? executable_eh_frame_walk(&output.link, atom, 0, 0, 0) : 0;
            }
            foreign_test_expect(tally, frames == 4 && foreign_gc_link_test_atom(foreign, &output, "fgc_dead") != 0 &&
                                      foreign_gc_link_test_atom(foreign, &output, "fgc_dead_lsda") != 0 &&
                                      foreign_gc_link_test_atom(foreign, &output, "fgc_dead_personality") != 0 &&
                                      foreign_gc_link_test_atom(foreign, &output, "fgc_dead_auxiliary") != 0,
                                "whole EH root retains every record and transitive dependency", 0);
        }
        arena_pop_to(&arena, 0);
        arena_pop_to(&names, 0);
    }
    arena_release(&arena);
    arena_release(&names);
}

static void foreign_gc_link_test_oracles(ForeignTestTally* tally, const char* directory, const char* object, uint32_t fixture)
{
    static const char* const linkers[] = {
        "ld.bfd",
        "ld.lld",
    };
    for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(linkers); l += 1)
    {
        ForeignTestPath executable;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(executable, sizeof(executable), "%s/fgc_reference_%u_%u", directory, fixture, l);
        snprintf(output, sizeof(output), "%s/fgc_reference_%u_%u.out", directory, fixture, l);
        snprintf(errors, sizeof(errors), "%s/fgc_reference_%u_%u.err", directory, fixture, l);
        char* arguments[] = {
            (char*)linkers[l],
            (char*)"-static",
            (char*)"--gc-sections",
            (char*)"-e",
            (char*)"_start",
            (char*)"-o",
            executable,
            (char*)object,
            0,
        };
        const bool linked = object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, linked, "reference linker collects original assembly", linkers[l]);
        char* run[] = {
            executable,
            0,
        };
        foreign_test_expect(tally, linked && object_test_run(run, output, errors) == 42, "reference GC executable returns expected value", linkers[l]);
    }
}

static void foreign_gc_link_test_unwind_output(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* output, String8 bytes, const ExecutableResult* written)
{
    // These objects deliberately mix framed functions with entry/personality stubs without CFI. Passing zero
    // program units disables the generic helper's assumption that every text atom in a framed unit has an FDE.
    const char* problem = bytes.pointer != 0 ? executable_test_unwind(&output->link, bytes, BUSTER_EXECUTABLE_BASE, 0, written) : "no output";
    foreign_test_expect(tally, problem == 0, "independent output walk checks retained FDE index and ranges", problem);
    if (problem == 0)
    {
        Elf64_Ehdr elf;
        memcpy(&elf, bytes.pointer, sizeof(elf));
        Elf64_Shdr names;
        memcpy(&names, bytes.pointer + elf.e_shoff + (uint64_t)elf.e_shstrndx * sizeof(names), sizeof(names));
        uint32_t matched = 0;
        for (uint32_t i = 1; i < elf.e_shnum; i += 1)
        {
            Elf64_Shdr section;
            memcpy(&section, bytes.pointer + elf.e_shoff + (uint64_t)i * sizeof(section), sizeof(section));
            if (strcmp(bytes.pointer + names.sh_offset + section.sh_name, ".eh_frame") == 0)
            {
                uint64_t cies[8] = { 0 };
                ExecutableTestFde frames[8] = { 0 };
                const uint64_t count = executable_test_walk_frames((const uint8_t*)bytes.pointer + section.sh_offset, section.sh_size, section.sh_addr,
                                                                 cies, frames, BUSTER_ARRAY_LENGTH(frames));
                static const char* const live[] = {
                    "fgc_live",
                    "fgc_live_lsda",
                };
                for (uint32_t name = 0; count <= BUSTER_ARRAY_LENGTH(frames) && name < BUSTER_ARRAY_LENGTH(live); name += 1)
                {
                    const SymbolId symbol = foreign_link_test_symbol(foreign, output, live[name]);
                    const AtomId atom = symbol != 0 ? output->link.symbols.atom[symbol] : 0;
                    const uint64_t address = BUSTER_EXECUTABLE_BASE + output->link.atoms.address[atom] + output->link.symbols.value[symbol];
                    for (uint32_t fde = 0; fde < count; fde += 1)
                    {
                        matched += symbol != 0 && frames[fde].initial == (int64_t)address && frames[fde].range == output->link.symbols.size[symbol] ? 1 : 0;
                    }
                }
                foreign_test_expect(tally, count == 2, "emitted EH section has exactly the two live FDEs", 0);
            }
        }
        foreign_test_expect(tally, matched == 2, "each retained FDE exactly covers its surviving function", 0);
    }
}

static void foreign_gc_link_test_program(ForeignTestTally* tally, const char* directory, Arena* scratch, uint32_t fixture, bool cross_check)
{
    const uint64_t saved = scratch->length;
    ForeignTestPath object;
    snprintf(object, sizeof(object), "%s/%s.o", directory, foreign_gc_link_test_sources[fixture].name);
    char* arguments[] = {
        object,
        (char*)"-u",
        (char*)"fgc_command_root",
    };
    ForeignCommand command = { 0 };
    const bool command_ready = foreign_test_command(&command, arguments, fixture == 0 ? BUSTER_ARRAY_LENGTH(arguments) : 1, scratch);
    foreign_test_expect(tally, command_ready, "prepare GC command", 0);
    if (command_ready)
    {
        Foreign foreign;
        foreign_run(&foreign, &command, FOREIGN_PATH_VECTOR, 1, 0);
        Arena input_arena = arena_reserve(1ull << 24);
        Arena input_names = arena_reserve(1ull << 20);
        ForeignLink input = { 0 };
        const ForeignLinkResult built = foreign_link_build(&input, &foreign, &input_arena, &input_names);
        const bool ready = foreign.failure.error == FOREIGN_ERROR_NONE && built.error == FOREIGN_LINK_ERROR_NONE;
        foreign_test_expect(tally, ready, "resolve and import GC fixture", foreign_gc_link_test_sources[fixture].name);
        if (ready)
        {
            const SymbolId entry = foreign_link_test_symbol(&foreign, &input, "_start");
            const SymbolId exported = fixture == 0 ? foreign_link_test_symbol(&foreign, &input, "fgc_export") : 0;
            const SymbolId forced = fixture == 0 ? foreign_link_test_symbol(&foreign, &input, "fgc_force") : 0;
            const AtomId manual = fixture == 0 ? foreign_gc_link_test_atom(&foreign, &input, "fgc_manual") : 0;
            ForeignGcRoots roots = {
                .entry = entry,
                .exports = &forced,
                .export_count = fixture == 0 ? 1 : 0,
                .retained = &manual,
                .retained_count = fixture == 0 ? 1 : 0,
            };
            if (fixture == 0)
            {
                input.dynamic.exports = &exported;
                input.dynamic.export_count = 1;
                input.dynamic.init = foreign_link_test_symbol(&foreign, &input, "fgc_dynamic_init");
                input.dynamic.fini = foreign_link_test_symbol(&foreign, &input, "fgc_dynamic_fini");
                input.dynamic.soname = (String8){
                    .pointer = (char*)"gc-metadata.so",
                    .length = sizeof("gc-metadata.so") - 1,
                };
                input.dynamic.relr = true;
                input.dynamic.relr_version = 7;
            }
            const ForeignLink original_input = input;
            const Foreign original_foreign = foreign;
            uint8_t* saved_input = arena_push(scratch, input_arena.length, 1);
            uint8_t* saved_names = arena_push(scratch, input_names.length, 1);
            const String8 original_object = foreign_test_read(scratch, object);
            const bool snapshots = saved_input != 0 && saved_names != 0 && original_object.pointer != 0;
            foreign_test_expect(tally, snapshots, "snapshot original import storage", 0);
            if (snapshots)
            {
                memcpy(saved_input, input_arena.base, input_arena.length);
                memcpy(saved_names, input_names.base, input_names.length);
                Arena arenas[2] = {
                    arena_reserve(1ull << 24),
                    arena_reserve(1ull << 24),
                };
                Arena names[2] = {
                    arena_reserve(1ull << 20),
                    arena_reserve(1ull << 20),
                };
                ForeignLink compact[2] = { 0 };
                bool accepted[2] = { 0 };
                String8 bytes[2] = { 0 };
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    const ForeignLinkResult result = foreign_gc_link(&compact[path], &foreign, &input, &roots, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR,
                                                                    &arenas[path], &names[path]);
                    accepted[path] = result.error == FOREIGN_LINK_ERROR_NONE;
                    char detail[128];
                    snprintf(detail, sizeof(detail), "%s %s: error %u, object %u, section %u", foreign_gc_link_test_sources[fixture].name,
                             path == 0 ? "exact" : "vector", (unsigned)result.error, result.object, result.import.section);
                    foreign_test_expect(tally, accepted[path], "collect imported executable", detail);
                    if (accepted[path])
                    {
                        foreign_gc_link_test_structure(tally, &foreign, &input, &compact[path], fixture);
                        ForeignTestPath executable;
                        ForeignTestPath output;
                        ForeignTestPath errors;
                        snprintf(executable, sizeof(executable), "%s/fgc_executable_%u_%u", directory, fixture, path);
                        snprintf(output, sizeof(output), "%s/fgc_run.out", directory);
                        snprintf(errors, sizeof(errors), "%s/fgc_run.err", directory);
                        const String8 destination = {
                            .pointer = executable,
                            .length = strlen(executable),
                        };
                        const SymbolId compact_entry = foreign_link_test_symbol(&foreign, &compact[path], "_start");
                        ExecutableResult written = { 0 };
                        const bool emitted = path == 0 ? executable_test_write_exact(&compact[path].link, compact_entry, destination, 1, &written) :
                                                        executable_test_write(&compact[path].link, compact_entry, destination, 1, &written, 0, 0);
                        foreign_test_expect(tally, emitted, "write collected executable", detail);
                        char* run[] = {
                            executable,
                            0,
                        };
                        foreign_test_expect(tally, emitted && object_test_run(run, output, errors) == 42, "execute compacted code and data relocations", detail);
                        bytes[path] = emitted ? foreign_test_read(scratch, executable) : (String8){ 0 };
                        if (fixture == 1)
                        {
                            foreign_gc_link_test_unwind_output(tally, &foreign, &compact[path], bytes[path], &written);
                        }
                    }
                    foreign_test_expect(tally, memcmp(saved_input, input_arena.base, input_arena.length) == 0 &&
                                              memcmp(saved_names, input_names.base, input_names.length) == 0 &&
                                              memcmp(&input, &original_input, sizeof(input)) == 0 && memcmp(&foreign, &original_foreign, sizeof(foreign)) == 0 &&
                                              foreign.object_count == 2 && original_object.length == foreign.objects[1].bytes.length &&
                                              memcmp(original_object.pointer, foreign.objects[1].bytes.pointer, original_object.length) == 0,
                                        "GC and writing preserve original Foreign, Link, maps and bytes", detail);
                }
                if (accepted[0] && accepted[1])
                {
                    foreign_test_expect(tally, foreign_gc_link_test_same(&foreign, &compact[0], &compact[1]), "exact/vector compact links and maps agree", 0);
                    foreign_test_expect(tally, bytes[0].pointer != 0 && bytes[1].pointer != 0 && bytes[0].length == bytes[1].length &&
                                              memcmp(bytes[0].pointer, bytes[1].pointer, bytes[0].length) == 0,
                                        "exact/vector GC emits identical executable bytes", 0);
                }
                foreign_gc_link_test_refused(tally, &foreign, &input, &roots);
                if (fixture == 1)
                {
                    foreign_gc_link_test_whole_unwind(tally, &foreign, &input, &roots);
                }
                foreign_test_expect(tally, memcmp(saved_input, input_arena.base, input_arena.length) == 0 &&
                                          memcmp(saved_names, input_names.base, input_names.length) == 0 &&
                                          memcmp(&input, &original_input, sizeof(input)) == 0,
                                    "refusals and whole-EH roots preserve every input column and map", 0);
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    arena_release(&arenas[path]);
                    arena_release(&names[path]);
                }
            }
        }
        arena_release(&input_arena);
        arena_release(&input_names);
        foreign_release(&foreign);
        if (cross_check)
        {
            foreign_gc_link_test_oracles(tally, directory, object, fixture);
        }
    }
    arena_pop_to(scratch, saved);
}

static void foreign_gc_link_test_undefined(ForeignTestTally* tally, const char* directory, Arena* scratch, bool cross_check)
{
    ForeignTestPath object;
    ForeignTestPath reference_path;
    snprintf(object, sizeof(object), "%s/fgc_undefined.o", directory);
    snprintf(reference_path, sizeof(reference_path), "%s/fgc_undefined_exact", directory);
    static const char* const descriptions[] = {
        "unreachable strong undefined closure",
        "retained strong undefined closure",
        "forced -u strong undefined root",
    };
    for (uint32_t mode = 0; mode < BUSTER_ARRAY_LENGTH(descriptions); mode += 1)
    {
        const uint64_t saved = scratch->length;
        String8 reference = { 0 };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const uint64_t iteration = scratch->length;
            char* arguments[] = {
                object,
                (char*)"-u",
                (char*)"fgc_missing_strong",
            };
            ForeignCommand command = { 0 };
            const bool prepared = foreign_test_command(&command, arguments, mode == 2 ? BUSTER_ARRAY_LENGTH(arguments) : 1, scratch);
            foreign_test_expect(tally, prepared, "prepare deferred undefined command", descriptions[mode]);
            bool emitted = false;
            if (prepared)
            {
                Foreign foreign;
                foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, 1, 0);
                Arena input_arena = arena_reserve(1ull << 24);
                Arena input_names = arena_reserve(1ull << 20);
                ForeignLink strict = { 0 };
                const ForeignLinkResult rejected = foreign_link_build(&strict, &foreign, &input_arena, &input_names);
                const uint32_t missing_name = foreign_test_name(&foreign, "fgc_missing_strong");
                foreign_test_expect(tally, foreign.failure.error == FOREIGN_ERROR_NONE && rejected.error == FOREIGN_LINK_ERROR_UNDEFINED &&
                                          missing_name != 0 && rejected.name == missing_name,
                                    "ordinary producer still diagnoses the original strong undefined", descriptions[mode]);
                arena_pop_to(&input_arena, 0);
                arena_pop_to(&input_names, 0);
                ForeignLink input = { 0 };
                const ForeignLinkResult built = foreign_link_build_for_gc(&input, &foreign, &input_arena, &input_names, RUNTIME_KIND_STATIC, (String8){ 0 }, (ForeignLinkOptions){ .entry = foreign_string("_start") });
                const bool ready = foreign.failure.error == FOREIGN_ERROR_NONE && built.error == FOREIGN_LINK_ERROR_NONE;
                foreign_test_expect(tally, ready, "GC producer defers strong undefined until reachability", descriptions[mode]);
                if (ready)
                {
                    const SymbolId missing = foreign_link_test_symbol(&foreign, &input, "fgc_missing_strong");
                    foreign_test_expect(tally, missing != 0 && input.link.symbols.source[missing] == BUSTER_LINK_SOURCE_NONE &&
                                              (input.link.symbols.flags[missing] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL,
                                        "deferred undefined retains canonical strong symbol", descriptions[mode]);
                    const AtomId retained = foreign_gc_link_test_atom(&foreign, &input, "fgc_missing_caller");
                    const ForeignGcRoots roots = {
                        .entry = foreign_link_test_symbol(&foreign, &input, "_start"),
                        .retained = &retained,
                        .retained_count = mode == 1 ? 1 : 0,
                    };
                    const ForeignLink before = input;
                    uint8_t* columns = arena_push(scratch, input_arena.length, 1);
                    uint8_t* name_bytes = arena_push(scratch, input_names.length, 1);
                    const String8 original = foreign_test_read(scratch, object);
                    const bool snapshots = columns != 0 && name_bytes != 0 && original.pointer != 0;
                    foreign_test_expect(tally, snapshots, "snapshot deferred undefined input", descriptions[mode]);
                    if (snapshots)
                    {
                        memcpy(columns, input_arena.base, input_arena.length);
                        memcpy(name_bytes, input_names.base, input_names.length);
                        Arena arena = arena_reserve(1ull << 24);
                        Arena names = arena_reserve(1ull << 20);
                        ForeignLink output;
                        memset(&output, 0xa5, sizeof(output));
                        const ForeignLink sentinel = output;
                        const uint64_t output_mark = arena.length;
                        const uint64_t name_mark = names.length;
                        const ForeignLinkResult result = foreign_gc_link(&output, &foreign, &input, &roots,
                                                                        path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, &arena, &names);
                        if (mode == 0)
                        {
                            foreign_test_expect(tally, result.error == FOREIGN_LINK_ERROR_NONE, "dead strong undefined does not prevent GC output", descriptions[mode]);
                            if (result.error == FOREIGN_LINK_ERROR_NONE)
                            {
                                foreign_test_expect(tally, foreign_link_test_symbol(&foreign, &output, "fgc_missing_strong") == 0 &&
                                                          foreign_link_test_symbol(&foreign, &output, "fgc_missing_caller") == 0,
                                                    "dead undefined target and its referring function map to zero", descriptions[mode]);
                                uint32_t frames = 0;
                                for (AtomId atom = 1; atom < output.link.atoms.count; atom += 1)
                                {
                                    frames += output.link.atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME ? executable_eh_frame_walk(&output.link, atom, 0, 0, 0) : 0;
                                }
                                foreign_test_expect(tally, frames == 0, "dead undefined function does not survive through its FDE", descriptions[mode]);
                                ForeignTestPath executable;
                                ForeignTestPath stdout_path;
                                ForeignTestPath stderr_path;
                                snprintf(executable, sizeof(executable), "%s/fgc_undefined_vector", directory);
                                snprintf(stdout_path, sizeof(stdout_path), "%s/fgc_undefined.out", directory);
                                snprintf(stderr_path, sizeof(stderr_path), "%s/fgc_undefined.err", directory);
                                const String8 destination = {
                                    .pointer = path == 0 ? reference_path : executable,
                                    .length = strlen(path == 0 ? reference_path : executable),
                                };
                                const SymbolId entry = foreign_link_test_symbol(&foreign, &output, "_start");
                                ExecutableResult written = { 0 };
                                emitted = path == 0 ? executable_test_write_exact(&output.link, entry, destination, 1, &written) :
                                                      executable_test_write(&output.link, entry, destination, 1, &written, 0, 0);
                                char* run[] = {
                                    destination.pointer,
                                    0,
                                };
                                foreign_test_expect(tally, emitted && object_test_run(run, stdout_path, stderr_path) == 42,
                                                    "executable runs after discarding a strong undefined closure", descriptions[mode]);
                                if (path != 0)
                                {
                                    const String8 bytes = emitted ? foreign_test_read(scratch, destination.pointer) : (String8){ 0 };
                                    foreign_test_expect(tally, reference.pointer != 0 && bytes.pointer != 0 && reference.length == bytes.length &&
                                                              memcmp(reference.pointer, bytes.pointer, bytes.length) == 0,
                                                        "exact/vector dead-undefined executables agree byte for byte", descriptions[mode]);
                                }
                            }
                        }
                        else
                        {
                            foreign_test_expect(tally, result.error == FOREIGN_LINK_ERROR_UNDEFINED && result.name == missing_name,
                                                "live or forced undefined reports its canonical name", descriptions[mode]);
                            foreign_test_expect(tally, arena.length == output_mark && names.length == name_mark && memcmp(&output, &sentinel, sizeof(output)) == 0,
                                                "undefined failure restores both output arenas and leaves output untouched", descriptions[mode]);
                        }
                        foreign_test_expect(tally, memcmp(&input, &before, sizeof(input)) == 0 && memcmp(columns, input_arena.base, input_arena.length) == 0 &&
                                                  memcmp(name_bytes, input_names.base, input_names.length) == 0 && foreign.object_count == 2 &&
                                                  original.length == foreign.objects[1].bytes.length &&
                                                  memcmp(original.pointer, foreign.objects[1].bytes.pointer, original.length) == 0,
                                            "deferred undefined success or refusal preserves all source storage", descriptions[mode]);
                        arena_release(&arena);
                        arena_release(&names);
                    }
                }
                arena_release(&input_arena);
                arena_release(&input_names);
                foreign_release(&foreign);
            }
            arena_pop_to(scratch, iteration);
            if (mode == 0 && path == 0 && emitted)
            {
                reference = foreign_test_read(scratch, reference_path);
                foreign_test_expect(tally, reference.pointer != 0, "retain dead-undefined exact executable", 0);
            }
        }
        arena_pop_to(scratch, saved);
    }
    if (cross_check)
    {
        foreign_gc_link_test_oracles(tally, directory, object, 2);
    }
}

static bool foreign_gc_link_test(const char* directory, Arena* arena, bool cross_check)
{
    ForeignTestTally tally = {
        .check = "foreign section GC handoff",
    };
    if (foreign_gc_link_test_compile(directory, &tally))
    {
        for (uint32_t fixture = 0; fixture < 2; fixture += 1)
        {
            foreign_gc_link_test_program(&tally, directory, arena, fixture, cross_check);
        }
        foreign_gc_link_test_undefined(&tally, directory, arena, cross_check);
    }
    return foreign_test_report(&tally);
}
