// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original assembly and C++ exercise COMDAT -> GC -> terminal CIE pooling -> native output. Include after
// foreign_gc_link_test.c and foreign_unwind_runtime_test.c. Compare both complete plans before either writer patches
// them, then execute and independently walk their output. All compilation/execution belongs to test-foreign's host.

static const ForeignLinkTestSource foreign_unwind_dedup_link_sources[] = {
    {
        .name = "fwd_first",
        .text =
            ".section .text.fwd_shared,\"axG\",@progbits,fwd_shared,comdat\n.weak fwd_shared\n.type fwd_shared,@function\n"
            "fwd_shared:\n.cfi_startproc\nmov $23,%eax\nret\n.cfi_endproc\n.size fwd_shared,.-fwd_shared\n"
            ".section .text.fwd_live_a,\"ax\",@progbits\n.globl fwd_live_a\n.type fwd_live_a,@function\n"
            "fwd_live_a:\n.cfi_startproc\nmov $11,%eax\nret\n.cfi_endproc\n.size fwd_live_a,.-fwd_live_a\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fwd_second",
        .text =
            ".section .text.fwd_shared,\"axG\",@progbits,fwd_shared,comdat\n.weak fwd_shared\n.type fwd_shared,@function\n"
            "fwd_shared:\n.cfi_startproc\nmov $99,%eax\nret\n.cfi_endproc\n.size fwd_shared,.-fwd_shared\n"
            ".section .text.fwd_dead,\"ax\",@progbits\n.globl fwd_dead\n.type fwd_dead,@function\n"
            "fwd_dead:\n.cfi_startproc\nmov $101,%eax\nret\n.cfi_endproc\n.size fwd_dead,.-fwd_dead\n"
            ".section .text.fwd_live_b,\"ax\",@progbits\n.globl fwd_live_b\n.type fwd_live_b,@function\n"
            "fwd_live_b:\n.cfi_startproc\nmov $8,%eax\nret\n.cfi_endproc\n.size fwd_live_b,.-fwd_live_b\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fwd_entry",
        .text =
            ".text\n.globl _start\n.type _start,@function\n_start:\ncall fwd_shared\nmov %eax,%ebx\n"
            "call fwd_live_a\nadd %eax,%ebx\ncall fwd_live_b\nadd %eax,%ebx\ncmp $42,%ebx\njne .Lfail\n"
            "mov $42,%edi\njmp .Lexit\n.Lfail:\nmov $101,%edi\n.Lexit:\nmov $60,%eax\nsyscall\n.size _start,.-_start\n"
            ".section .data.fwd_manual,\"aw\",@progbits\n.globl fwd_manual\n.type fwd_manual,@object\nfwd_manual:\n.quad 17\n.size fwd_manual,8\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
};

typedef struct ForeignUnwindDedupTestSnapshot ForeignUnwindDedupTestSnapshot;
struct ForeignUnwindDedupTestSnapshot
{
    ForeignLink header;
    uint8_t* bytes;
    uint8_t* names;
    uint64_t length;
    uint64_t names_length;
};

static bool foreign_unwind_dedup_link_test_snapshot(ForeignUnwindDedupTestSnapshot* snapshot, const ForeignLink* input,
                                               const Arena* arena, const Arena* names, Arena* scratch)
{
    *snapshot = (ForeignUnwindDedupTestSnapshot){
        .header = *input,
        .bytes = arena_push(scratch, arena->length, 1),
        .names = arena_push(scratch, names->length, 1),
        .length = arena->length,
        .names_length = names->length,
    };
    const bool ready = snapshot->bytes != 0 && snapshot->names != 0;
    if (ready)
    {
        memcpy(&snapshot->header, input, sizeof(*input));
        memcpy(snapshot->bytes, arena->base, arena->length);
        memcpy(snapshot->names, names->base, names->length);
    }
    return ready;
}

static bool foreign_unwind_dedup_link_test_unchanged(const ForeignUnwindDedupTestSnapshot* snapshot, const ForeignLink* input,
                                                const Arena* arena, const Arena* names)
{
    return snapshot->bytes != 0 && snapshot->names != 0 && arena->length == snapshot->length && names->length == snapshot->names_length &&
           memcmp(&snapshot->header, input, sizeof(*input)) == 0 && memcmp(snapshot->bytes, arena->base, arena->length) == 0 &&
           memcmp(snapshot->names, names->base, names->length) == 0;
}

static bool foreign_unwind_dedup_link_test_record(const ForeignUnwindRecord* a, const ForeignUnwindRecord* b)
{
    return a->old_offset == b->old_offset && a->new_offset == b->new_offset && a->length == b->length && a->cie_offset == b->cie_offset &&
           a->kind == b->kind && a->section == b->section && a->atom == b->atom;
}

static bool foreign_unwind_dedup_link_test_same(const Foreign* foreign, const ForeignUnwindDedupLink* a, const ForeignUnwindDedupLink* b)
{
    bool same = foreign_gc_link_test_same(foreign, &a->converted, &b->converted) && a->section_count == b->section_count &&
                a->plan.atom_count == b->plan.atom_count && a->plan.fixup_count == b->plan.fixup_count &&
                a->plan.removed_cies == b->plan.removed_cies && a->plan.removed_atoms == b->plan.removed_atoms && a->plan.removed_bytes == b->plan.removed_bytes &&
                a->plan.capacity.names == b->plan.capacity.names && a->plan.capacity.atoms == b->plan.capacity.atoms && a->plan.capacity.symbols == b->plan.capacity.symbols &&
                a->plan.capacity.fixups == b->plan.capacity.fixups && a->plan.capacity.wide_addends == b->plan.capacity.wide_addends &&
                a->plan.capacity.priorities == b->plan.capacity.priorities &&
                a->roots.entry == b->roots.entry && a->roots.export_count == b->roots.export_count && a->roots.retained_count == b->roots.retained_count;
    for (SymbolId symbol = 1; same && symbol < a->converted.link.symbols.count; symbol += 1)
    {
        same = a->converted.link.symbols.version[symbol] == b->converted.link.symbols.version[symbol];
    }
    for (uint32_t i = 0; same && i < a->roots.export_count; i += 1)
    {
        same = a->roots.exports[i] == b->roots.exports[i];
    }
    for (uint32_t i = 0; same && i < a->roots.retained_count; i += 1)
    {
        same = a->roots.retained[i] == b->roots.retained[i];
    }
    for (AtomId atom = 0; same && atom < a->plan.atom_count; atom += 1)
    {
        const ForeignUnwindDedupAtom* x = &a->plan.maps[atom];
        const ForeignUnwindDedupAtom* y = &b->plan.maps[atom];
        same = a->plan.atoms[atom] == b->plan.atoms[atom] && x->record_count == y->record_count && x->owner == y->owner && x->next == y->next &&
               x->length == y->length && x->endpoint == y->endpoint && x->eligible == y->eligible && x->pooled == y->pooled && x->alignment_log2 == y->alignment_log2;
        for (uint32_t r = 0; same && r < x->record_count; r += 1)
        {
            same = x->kept[r] == y->kept[r] && foreign_unwind_dedup_link_test_record(&x->records[r], &y->records[r]);
        }
    }
    for (FixupId fixup = 0; same && fixup < a->plan.fixup_count; fixup += 1)
    {
        same = a->plan.fixups[fixup] == b->plan.fixups[fixup];
    }
    for (uint32_t s = 0; same && s < a->section_count; s += 1)
    {
        const ForeignUnwindDedupSection* x = &a->sections[s];
        const ForeignUnwindDedupSection* y = &b->sections[s];
        same = x->object == y->object && x->section == y->section && x->atom == y->atom && x->old_length == y->old_length &&
               x->endpoint == y->endpoint && x->record_count == y->record_count;
        for (uint32_t r = 0; same && r < x->record_count; r += 1)
        {
            same = foreign_unwind_dedup_link_test_record(&x->records[r], &y->records[r]);
        }
    }
    return same;
}

static uint32_t foreign_unwind_dedup_link_test_section(const ForeignObject* object, const char* wanted)
{
    uint32_t found = 0;
    for (uint32_t i = object->first_global; object->library == 0 && i < object->symbols.count; i += 1)
    {
        const Elf64_Sym symbol = import_symbol(&object->symbols, i);
        uint32_t section = 0;
        String8 name = { 0 };
        if (ELF64_ST_TYPE(symbol.st_info) == STT_FUNC && import_symbol_section(&object->symbols, i, &symbol, &section) && section != SHN_UNDEF &&
            section < object->elf.section_count && (section < SHN_LORESERVE || symbol.st_shndx == SHN_XINDEX) &&
            import_string(object->symbols.names, symbol.st_name, &name) && import_equal(name, wanted))
        {
            found = section;
        }
    }
    return found;
}

static void foreign_unwind_dedup_link_test_maps(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* before,
                                           const ForeignGcRoots* roots, const ForeignUnwindDedupLink* output)
{
    const ForeignLink* after = &output->converted;
    bool symbols = before->link.symbols.count == after->link.symbols.count && before->link.names.count == after->link.names.count &&
                   memcmp(before->names, after->names, (uint64_t)foreign->names.count * sizeof(SymbolId)) == 0;
    for (SymbolId symbol = 1; symbols && symbol < before->link.symbols.count; symbol += 1)
    {
        const AtomId atom = before->link.symbols.atom[symbol];
        symbols = after->link.symbols.atom[symbol] == output->plan.atoms[atom] && before->link.symbols.name[symbol] == after->link.symbols.name[symbol] &&
                  before->link.symbols.size[symbol] == after->link.symbols.size[symbol] && before->link.symbols.flags[symbol] == after->link.symbols.flags[symbol] &&
                  before->link.symbols.source[symbol] == after->link.symbols.source[symbol] && before->link.symbols.version[symbol] == after->link.symbols.version[symbol];
        if (atom == 0 || before->link.atoms.class[atom] != BUSTER_LINK_CLASS_EH_FRAME)
        {
            symbols = symbols && before->link.symbols.value[symbol] == after->link.symbols.value[symbol];
        }
    }
    bool maps = before->objects != after->objects && before->names != after->names;
    for (uint32_t id = 0; maps && id < foreign->object_count; id += 1)
    {
        const ImportObject* a = &before->objects[id];
        const ImportObject* b = &after->objects[id];
        maps = a->section_count == b->section_count && a->symbol_count == b->symbol_count &&
               (a->section_count == 0 || a->section_atoms != b->section_atoms) && (a->symbol_count == 0 || a->symbols != b->symbols);
        for (uint32_t s = 0; maps && s < a->section_count; s += 1)
        {
            maps = b->section_atoms[s] == output->plan.atoms[a->section_atoms[s]];
        }
        for (uint32_t s = 0; maps && s < a->symbol_count; s += 1)
        {
            maps = a->symbols[s] == b->symbols[s];
        }
    }
    bool rooted = output->roots.entry == roots->entry && output->roots.export_count == roots->export_count && output->roots.retained_count == roots->retained_count;
    for (uint32_t i = 0; rooted && i < roots->export_count; i += 1)
    {
        rooted = output->roots.exports != roots->exports && output->roots.exports[i] == roots->exports[i];
    }
    for (uint32_t i = 0; rooted && i < roots->retained_count; i += 1)
    {
        rooted = output->roots.retained != roots->retained && output->roots.retained[i] == output->plan.atoms[roots->retained[i]];
    }
    foreign_test_expect(tally, symbols && maps && rooted, "pooling preserves SymbolIds and remaps fresh object/root arrays", 0);
    const ExecutableDynamic* x = &before->dynamic;
    const ExecutableDynamic* y = &after->dynamic;
    bool dynamic = x->interpreter.pointer == y->interpreter.pointer && x->interpreter.length == y->interpreter.length &&
                   x->libraries == y->libraries && x->library_count == y->library_count && x->versions == y->versions && x->version_count == y->version_count &&
                   x->note.pointer == y->note.pointer && x->note.length == y->note.length && x->init == y->init && x->fini == y->fini &&
                   x->export_count == y->export_count && x->relr_version == y->relr_version && x->relr == y->relr && x->shared == y->shared &&
                   x->soname.pointer == y->soname.pointer && x->soname.length == y->soname.length && before->library_versions == after->library_versions;
    for (uint32_t e = 0; dynamic && e < x->export_count; e += 1)
    {
        dynamic = x->exports != y->exports && x->exports[e] == y->exports[e];
    }
    foreign_test_expect(tally, dynamic, "pooling preserves dynamic metadata with fresh export storage", 0);
    bool records = true;
    bool endpoint_inside = false;
    for (uint32_t s = 0; records && s < output->section_count; s += 1)
    {
        const ForeignUnwindDedupSection* section = &output->sections[s];
        const ForeignLinkUnwind* original = foreign_link_unwind(before, section->object, section->section);
        const ForeignLinkUnwind* historical = foreign_link_unwind(after, section->object, section->section);
        uint64_t endpoint = UINT64_MAX;
        records = original != 0 && historical != 0 && section->old_length == original->plan.old_length &&
                  section->record_count == original->plan.record_count && historical->plan.record_count == original->plan.record_count &&
                  section->atom == after->objects[section->object].section_atoms[section->section] && section->atom < after->link.atoms.count &&
                  foreign_unwind_dedup_section_offset(section, section->old_length, &endpoint) && endpoint == section->endpoint &&
                  endpoint <= after->link.atoms.size[section->atom] && !foreign_unwind_dedup_section_offset(section, section->old_length + 1, &endpoint);
        endpoint_inside = endpoint_inside || (records && section->endpoint < after->link.atoms.size[section->atom]);
        for (uint32_t r = 0; records && r < section->record_count; r += 1)
        {
            const ForeignUnwindRecord* a = &original->plan.records[r];
            const ForeignUnwindRecord* b = &section->records[r];
            const ForeignUnwindRecord* h = &historical->plan.records[r];
            const AtomId code = a->kind == FOREIGN_UNWIND_FDE ? after->objects[section->object].section_atoms[a->section] : 0;
            uint64_t offset = UINT64_MAX;
            const bool live = a->new_offset != UINT64_MAX;
            const bool mapped = foreign_unwind_dedup_section_offset(section, a->old_offset, &offset);
            ForeignUnwindRecord expected = *a;
            expected.atom = output->plan.atoms[a->atom];
            records = a->old_offset == b->old_offset && a->length == b->length && a->kind == b->kind && a->section == b->section &&
                      a->cie_offset == b->cie_offset && b->atom == code && foreign_unwind_dedup_link_test_record(&expected, h) && mapped == live &&
                      (live ? b->new_offset == offset && offset <= after->link.atoms.size[section->atom] &&
                              b->length <= after->link.atoms.size[section->atom] - offset : b->new_offset == UINT64_MAX);
        }
    }
    foreign_test_expect(tally, records, "original record maps preserve dead records, endpoints and remapped code owners", 0);
    foreign_test_expect(tally, output->plan.removed_atoms == 0 || endpoint_inside, "a pooled member endpoint is distinct from the whole output atom", 0);
    Link checked = after->link;
    foreign_test_expect(tally, link_check(&checked) == LINK_ERROR_NONE, "terminal pooled link satisfies binding and fixup ownership", 0);
}

static void foreign_unwind_dedup_link_test_dead(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* input,
                                           const ForeignLink* compact, const ForeignUnwindDedupLink* output)
{
    uint32_t matched = 0;
    uint32_t removed = 0;
    uint32_t retained = 0;
    bool correct = true;
    for (uint32_t id = 1; id < foreign->object_count; id += 1)
    {
        const ForeignObject* object = &foreign->objects[id];
        const uint32_t live = foreign_unwind_dedup_link_test_section(object, "fwd_live_b");
        if (live != 0)
        {
            const uint32_t shared = foreign_unwind_dedup_link_test_section(object, "fwd_shared");
            const uint32_t dead = foreign_unwind_dedup_link_test_section(object, "fwd_dead");
            matched += 1;
            correct = correct && shared != 0 && dead != 0 && object->discarded != 0 && object->discarded[shared] != 0 && object->discarded[dead] == 0 &&
                      input->objects[id].section_atoms[shared] == 0 && input->objects[id].section_atoms[dead] != 0 &&
                      compact->objects[id].section_atoms[shared] == 0 && compact->objects[id].section_atoms[dead] == 0 && compact->objects[id].section_atoms[live] != 0;
            for (uint32_t s = 0; s < output->section_count; s += 1)
            {
                const ForeignUnwindDedupSection* section = &output->sections[s];
                for (uint32_t r = 0; section->object == id && r < section->record_count; r += 1)
                {
                    const ForeignUnwindRecord* record = &section->records[r];
                    if (record->kind == FOREIGN_UNWIND_FDE && (record->section == shared || record->section == dead))
                    {
                        removed += 1;
                        correct = correct && record->new_offset == UINT64_MAX && record->atom == 0;
                    }
                    else if (record->kind == FOREIGN_UNWIND_FDE && record->section == live)
                    {
                        retained += 1;
                        correct = correct && record->new_offset != UINT64_MAX && record->atom == output->converted.objects[id].section_atoms[live];
                    }
                }
            }
        }
    }
    foreign_test_expect(tally, correct && matched == 1 && removed == 2 && retained == 1,
                        "COMDAT-losing and GC-dead FDEs stay dead while the losing object's live FDE survives", 0);
    foreign_test_expect(tally, output->section_count == 2 && output->plan.removed_cies == 1 && output->plan.removed_atoms == 1 &&
                              foreign_link_test_symbol(foreign, &output->converted, "fwd_dead") == 0,
                        "two surviving assembly EH atoms pool exactly one duplicate CIE", 0);
}

static void foreign_unwind_dedup_link_test_cpp(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* before, const ForeignUnwindDedupLink* output)
{
    bool pooled = false;
    for (uint32_t s = 0; s < output->section_count; s += 1)
    {
        const ForeignUnwindDedupSection* section = &output->sections[s];
        const uint32_t run = foreign_unwind_dedup_link_test_section(&foreign->objects[section->object], "run_b");
        const AtomId original = before->objects[section->object].section_atoms[section->section];
        const ForeignUnwindDedupAtom* map = &output->plan.maps[original];
        for (uint32_t r = 0; run != 0 && r < section->record_count; r += 1)
        {
            const ForeignUnwindRecord* record = &section->records[r];
            if (record->kind == FOREIGN_UNWIND_FDE && record->section == run)
            {
                pooled = record->new_offset != UINT64_MAX && record->atom != 0 && map->pooled &&
                         (map->owner != original || map->next != 0) && output->converted.link.atoms.unit[section->atom] == 0;
            }
        }
    }
    foreign_test_expect(tally, pooled && output->plan.removed_cies != 0,
                        "the run_b exception frame shares a pooled CIE across input EH atoms", 0);
}

static bool foreign_unwind_dedup_link_test_functions(const Foreign* foreign, const ForeignLink* output, String8 file, bool cpp, Arena* arena)
{
    const uint64_t saved = arena->length;
    const ExecutableTestImage image = executable_test_image_of(file, cpp ? ET_DYN : ET_EXEC);
    const Elf64_Shdr* frames = executable_test_section(&image, ".eh_frame");
    bool covered = frames != 0 && frames->sh_offset <= file.length && frames->sh_size <= file.length - frames->sh_offset;
    const uint64_t capacity = covered ? frames->sh_size / 8 + 1 : 0;
    uint64_t* cies = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, capacity);
    ExecutableTestFde* fdes = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableTestFde, capacity);
    covered = covered && cies != 0 && fdes != 0;
    const uint64_t count = covered ? executable_test_walk_frames((const uint8_t*)file.pointer + frames->sh_offset, frames->sh_size, frames->sh_addr,
                                                                 cies, fdes, capacity) : UINT64_MAX;
    covered = covered && count != UINT64_MAX;
    static const char* const assembly_names[] = {
        "fwd_shared",
        "fwd_live_a",
        "fwd_live_b",
    };
    static const char* const cpp_names[] = {
        "_Z12shared_throwILi7EEii",
        "run_b",
    };
    const char* const* names = cpp ? cpp_names : assembly_names;
    const uint32_t name_count = cpp ? BUSTER_ARRAY_LENGTH(cpp_names) : BUSTER_ARRAY_LENGTH(assembly_names);
    for (uint32_t i = 0; covered && i < name_count; i += 1)
    {
        const SymbolId symbol = foreign_link_test_symbol(foreign, output, names[i]);
        const AtomId atom = symbol != 0 ? output->link.symbols.atom[symbol] : 0;
        const uint64_t initial = (cpp ? 0 : BUSTER_EXECUTABLE_BASE) + output->link.atoms.address[atom] + output->link.symbols.value[symbol];
        bool found = false;
        for (uint64_t f = 0; symbol != 0 && atom != 0 && f < count; f += 1)
        {
            found = found || (fdes[f].initial == (int64_t)initial && fdes[f].range == output->link.symbols.size[symbol]);
        }
        covered = found;
    }
    arena_pop_to(arena, saved);
    return covered;
}

static void foreign_unwind_dedup_link_test_refused(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* input, const ForeignGcRoots* roots)
{
    Arena arena = arena_reserve(1ull << 20);
    Arena names = arena_reserve(1ull << 20);
    const bool room = arena.base != 0 && names.base != 0;
    foreign_test_expect(tally, room, "reserve CIE adapter refusal arenas", 0);
    static const char* const cases[] = {
        "exhausted output arena",
        "exhausted name arena",
        "invalid entry root",
        "invalid retained atom",
        "invalid export root",
        "shared output",
    };
    ForeignLinkError exact[BUSTER_ARRAY_LENGTH(cases)] = { 0 };
    for (uint32_t path = 0; room && path < 2; path += 1)
    {
        for (uint32_t failure = 0; failure < BUSTER_ARRAY_LENGTH(exact); failure += 1)
        {
            if (failure < 2)
            {
                Arena* exhausted = failure == 0 ? &arena : &names;
                foreign_test_expect(tally, arena_push(exhausted, exhausted->capacity, 1) != 0, "exhaust selected output arena", 0);
            }
            ForeignGcRoots invalid = *roots;
            const AtomId bad_atom = input->link.atoms.count;
            const SymbolId bad_symbol = input->link.symbols.count;
            invalid.entry = failure == 2 ? bad_symbol : invalid.entry;
            if (failure == 3)
            {
                invalid.retained = &bad_atom;
                invalid.retained_count = 1;
            }
            if (failure == 4)
            {
                invalid.exports = &bad_symbol;
                invalid.export_count = 1;
            }
            ForeignLink unsupported;
            memcpy(&unsupported, input, sizeof(unsupported));
            unsupported.dynamic.shared = failure == 5;
            const ForeignLink* tested = failure == 5 ? &unsupported : input;
            uint8_t original[sizeof(unsupported)];
            memcpy(original, tested, sizeof(original));
            ForeignUnwindDedupLink output;
            memset(&output, 0xa5, sizeof(output));
            uint8_t untouched[sizeof(output)];
            memcpy(untouched, &output, sizeof(output));
            const uint64_t saved = arena.length;
            const uint64_t saved_names = names.length;
            const ForeignLinkResult result = foreign_unwind_dedup_link(&output, foreign, tested, &invalid,
                                                                       path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, &arena, &names);
            // The planner allocates from the output arena directly. Name bytes are allocated by link_add_name_hashed,
            // whose failure the adapter reports as LINK, consistently with the GC adapter's copy path.
            const ForeignLinkError expected = failure == 0 ? FOREIGN_LINK_ERROR_MEMORY : failure == 1 ? FOREIGN_LINK_ERROR_LINK :
                                              failure == 5 ? FOREIGN_LINK_ERROR_UNSUPPORTED : FOREIGN_LINK_ERROR_INPUT;
            char context[160];
            snprintf(context, sizeof(context), "%s, %s, error %u expected %u", path == 0 ? "exact" : "vector", cases[failure],
                     (unsigned)result.error, (unsigned)expected);
            foreign_test_expect(tally, result.error == expected, "adapter refusal reports the expected error", context);
            foreign_test_expect(tally, arena.length == saved && names.length == saved_names && memcmp(untouched, &output, sizeof(output)) == 0 &&
                                      memcmp(original, tested, sizeof(original)) == 0,
                                "adapter refusal preserves arenas, output and input", context);
            if (path == 0)
            {
                exact[failure] = result.error;
            }
            else
            {
                foreign_test_expect(tally, exact[failure] == result.error, "exact/vector adapter refusal kinds agree", context);
            }
            arena_pop_to(&arena, 0);
            arena_pop_to(&names, 0);
        }
    }
    arena_release(&arena);
    arena_release(&names);
}

static bool foreign_unwind_dedup_link_test_compile(ForeignTestTally* tally, const char* directory, ForeignTestLine* line, Arena* arena, bool cpp)
{
    bool ready = true;
    const uint32_t count = cpp ? BUSTER_ARRAY_LENGTH(foreign_unwind_runtime_sources) : BUSTER_ARRAY_LENGTH(foreign_unwind_dedup_link_sources);
    for (uint32_t i = 0; ready && i < count; i += 1)
    {
        ForeignTestPath source;
        ForeignTestPath object;
        ForeignTestPath output;
        ForeignTestPath errors;
        char name[64];
        snprintf(name, sizeof(name), cpp ? "fwd_cpp_%u" : "fwd_asm_%u", i);
        snprintf(source, sizeof(source), "%s/%s.%s", directory, name, cpp ? "cpp" : "s");
        snprintf(object, sizeof(object), "%s/%s.o", directory, name);
        snprintf(output, sizeof(output), "%s/%s.compile.out", directory, name);
        snprintf(errors, sizeof(errors), "%s/%s.compile.err", directory, name);
        char* arguments[] = {
            (char*)(cpp ? "g++" : "cc"),
            (char*)"-c",
            (char*)"-O1",
            (char*)"-fPIC",
            (char*)"-ffunction-sections",
            (char*)"-fdata-sections",
            source,
            (char*)"-o",
            object,
            0,
        };
        const char* text = cpp ? foreign_unwind_runtime_sources[i] : foreign_unwind_dedup_link_sources[i].text;
        ready = foreign_test_write(source, text, strlen(text)) && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, ready, "compile original CIE composition fixture", name);
        char basename[80];
        snprintf(basename, sizeof(basename), "%s.o", name);
        char* path = foreign_test_path(directory, basename, arena);
        ready = ready && path != 0;
        foreign_test_expect(tally, path != 0, "retain composition object path", name);
        if (ready)
        {
            foreign_test_add(line, path, true, true);
        }
    }
    return ready;
}

static void foreign_unwind_dedup_link_test_program(ForeignTestTally* tally, const char* directory, Arena* scratch,
                                              bool cross_check, ForeignTestSystem* system, bool cpp)
{
    const uint64_t saved = scratch->length;
    ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(scratch, ForeignTestLine, 1);
    Runtime runtime = { 0 };
    bool ready = line != 0 && (!cpp || (system->found && runtime_import(&runtime) == RUNTIME_ERROR_NONE));
    foreign_test_expect(tally, ready, "prepare composition command and runtime", cpp ? "C++" : "assembly");
    if (ready)
    {
        *line = (ForeignTestLine){ 0 };
        if (cpp)
        {
            foreign_test_pie_begin(line, system);
        }
        else
        {
            foreign_test_add(line, (char*)"-static", false, true);
            foreign_test_add(line, (char*)"-e", false, true);
            foreign_test_add(line, (char*)"_start", false, true);
        }
        foreign_test_add(line, (char*)"--eh-frame-hdr", false, true);
        foreign_test_add(line, (char*)"--gc-sections", false, true);
        ready = foreign_unwind_dedup_link_test_compile(tally, directory, line, scratch, cpp);
        if (cpp)
        {
            foreign_test_add(line, (char*)"-lstdc++", true, true);
            foreign_test_pie_end(line, system);
        }
    }
    ForeignCommand command = { 0 };
    ready = ready && foreign_test_command(&command, line->ours, line->our_count, scratch);
    foreign_test_expect(tally, ready, "prepare composition resolver arguments", cpp ? "C++" : "assembly");
    if (ready)
    {
        Foreign foreign;
        foreign_run(&foreign, &command, FOREIGN_PATH_VECTOR, 1, 0);
        Arena input_arena = arena_reserve(1ull << 26);
        Arena input_names = arena_reserve(1ull << 22);
        ForeignLink input = { 0 };
        const ForeignLinkResult imported = foreign_link_build_for_gc(&input, &foreign, &input_arena, &input_names,
                                                                     cpp ? RUNTIME_KIND_DYNAMIC : RUNTIME_KIND_STATIC, runtime.loader_path,
                                                                     (ForeignLinkOptions){ .entry = foreign_string("_start") });
        ready = foreign.failure.error == FOREIGN_ERROR_NONE && imported.error == FOREIGN_LINK_ERROR_NONE;
        foreign_test_expect(tally, ready, "resolve COMDAT inputs for GC and terminal CIE pooling", cpp ? "C++" : "assembly");
        if (ready)
        {
            Foreign original_foreign;
            memcpy(&original_foreign, &foreign, sizeof(foreign));
            ForeignUnwindDedupTestSnapshot original = { 0 };
            const bool original_saved = foreign_unwind_dedup_link_test_snapshot(&original, &input, &input_arena, &input_names, scratch);
            foreign_test_expect(tally, original_saved, "snapshot original import columns and bytes", 0);
            Arena compact_arenas[2] = {
                arena_reserve(1ull << 26),
                arena_reserve(1ull << 26),
            };
            Arena compact_names[2] = {
                arena_reserve(1ull << 22),
                arena_reserve(1ull << 22),
            };
            Arena pool_arenas[2] = {
                arena_reserve(1ull << 26),
                arena_reserve(1ull << 26),
            };
            Arena pool_names[2] = {
                arena_reserve(1ull << 22),
                arena_reserve(1ull << 22),
            };
            ForeignLink compact[2] = { 0 };
            ForeignUnwindDedupLink pooled[2] = { 0 };
            ForeignUnwindDedupTestSnapshot snapshots[2] = { 0 };
            bool accepted[2] = { 0 };
            const SymbolId exported = cpp ? 0 : foreign_link_test_symbol(&foreign, &input, "fwd_live_a");
            const AtomId manual = cpp ? 0 : foreign_gc_link_test_atom(&foreign, &input, "fwd_manual");
            const ForeignGcRoots roots = {
                .entry = foreign_link_test_symbol(&foreign, &input, "_start"),
                .exports = &exported,
                .export_count = cpp ? 0 : 1,
                .retained = &manual,
                .retained_count = cpp ? 0 : 1,
            };
            for (uint32_t path = 0; original_saved && path < 2; path += 1)
            {
                const ForeignPath mode = path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR;
                const ForeignLinkResult collected = foreign_gc_link(&compact[path], &foreign, &input, &roots, mode, &compact_arenas[path], &compact_names[path]);
                foreign_test_expect(tally, collected.error == FOREIGN_LINK_ERROR_NONE, "collect before terminal pooling", cpp ? "C++" : "assembly");
                if (collected.error == FOREIGN_LINK_ERROR_NONE)
                {
                    const SymbolId compact_export = cpp ? 0 : foreign_link_test_symbol(&foreign, &compact[path], "fwd_live_a");
                    const AtomId compact_manual = cpp ? 0 : foreign_gc_link_test_atom(&foreign, &compact[path], "fwd_manual");
                    const ForeignGcRoots compact_roots = {
                        .entry = foreign_link_test_symbol(&foreign, &compact[path], "_start"),
                        .exports = &compact_export,
                        .export_count = cpp ? 0 : 1,
                        .retained = &compact_manual,
                        .retained_count = cpp ? 0 : 1,
                    };
                    const bool snapshot = foreign_unwind_dedup_link_test_snapshot(&snapshots[path], &compact[path], &compact_arenas[path], &compact_names[path], scratch);
                    foreign_test_expect(tally, snapshot, "snapshot complete post-GC columns, maps, names and bytes", 0);
                    if (snapshot)
                    {
                        if (!cpp && path == 0)
                        {
                            foreign_unwind_dedup_link_test_refused(tally, &foreign, &compact[path], &compact_roots);
                            foreign_test_expect(tally, foreign_unwind_dedup_link_test_unchanged(&snapshots[path], &compact[path], &compact_arenas[path], &compact_names[path]),
                                                "refusals preserve every input byte and column", 0);
                        }
                        const ForeignLinkResult result = foreign_unwind_dedup_link(&pooled[path], &foreign, &compact[path], &compact_roots, mode,
                                                                                  &pool_arenas[path], &pool_names[path]);
                        accepted[path] = result.error == FOREIGN_LINK_ERROR_NONE;
                        char detail[128];
                        snprintf(detail, sizeof(detail), "%s %s error %u object %u section %u offset %llu", cpp ? "C++" : "assembly", path == 0 ? "exact" : "vector",
                                 (unsigned)result.error, result.object, result.import.section, (unsigned long long)result.import.offset);
                        foreign_test_expect(tally, accepted[path], "pool current post-GC CIEs", detail);
                        if (accepted[path])
                        {
                            foreign_unwind_dedup_link_test_maps(tally, &foreign, &compact[path], &compact_roots, &pooled[path]);
                            if (cpp)
                            {
                                foreign_unwind_runtime_ownership(tally, &foreign, &pooled[path].converted, detail);
                                foreign_unwind_dedup_link_test_cpp(tally, &foreign, &compact[path], &pooled[path]);
                            }
                            else
                            {
                                foreign_unwind_dedup_link_test_dead(tally, &foreign, &input, &compact[path], &pooled[path]);
                                foreign_test_expect(tally, pooled[path].roots.retained[0] != compact_manual,
                                                    "retained data root after the pooled EH atoms gets a new AtomId", 0);
                            }
                        }
                    }
                }
            }
            if (accepted[0] && accepted[1])
            {
                foreign_test_expect(tally, foreign_gc_link_test_same(&foreign, &compact[0], &compact[1]) &&
                                          foreign_unwind_dedup_link_test_same(&foreign, &pooled[0], &pooled[1]),
                                    "exact/vector GC, pooled bytes, fixups, roots and record maps agree before layout", 0);
            }
            String8 bytes[2] = { 0 };
            for (uint32_t path = 0; path < 2; path += 1)
            {
                if (accepted[path])
                {
                    ForeignTestPath executable;
                    snprintf(executable, sizeof(executable), "%s/fwd_output_%u_%u", directory, cpp ? 102u : 101u, path);
                    ExecutableTestWrite write = {
                        .link = &pooled[path].converted.link,
                        .options = {
                            .path = {
                                .pointer = executable,
                                .length = strlen(executable),
                            },
                            .entry = pooled[path].roots.entry,
                            .dynamic = cpp ? &pooled[path].converted.dynamic : 0,
                            .exact = path == 0,
                        },
                    };
                    const bool published = lane_run(path == 0 ? 1 : 3, executable_test_lane_main, &write, 0) && write.succeeded;
                    foreign_test_expect(tally, published, "write composed pooled executable", executable);
                    if (published)
                    {
                        foreign_test_expect(tally, foreign_dynamic_test_run(directory, executable, cpp ? 102 : 101, scratch),
                                            cpp ? "C++ throws across retained frames and runs all three destructors" : "pooled assembly executes the three surviving functions", executable);
                        bytes[path] = foreign_test_read(scratch, executable);
                        // Pooling moves EH ownership to unit 0. Skip the walk's old per-unit TEXT inference;
                        // it still probes every FDE's first/last PC, and the named functions are checked below.
                        const char* problem = bytes[path].pointer != 0 ? executable_test_unwind(&pooled[path].converted.link, bytes[path],
                                                cpp ? 0 : BUSTER_EXECUTABLE_BASE, 0, &write.result) : "no output";
                        foreign_test_expect(tally, problem == 0, "independent output walk validates pooled FDE index and ranges", problem);
                        foreign_test_expect(tally, problem == 0 && foreign_unwind_dedup_link_test_functions(&foreign, &pooled[path].converted, bytes[path], cpp, scratch),
                                            "emitted FDEs cover each named surviving or throwing function exactly", executable);
                        if (!cpp)
                        {
                            foreign_test_expect(tally, write.result.fdes == 3, "output contains exactly the three surviving assembly FDEs", 0);
                        }
                    }
                    else
                    {
                        executable_test_print_failure(executable, &write.result);
                    }
                    foreign_test_expect(tally, foreign_unwind_dedup_link_test_unchanged(&snapshots[path], &compact[path], &compact_arenas[path], &compact_names[path]),
                                        "pooling and writer patching preserve every post-GC input byte and column", executable);
                }
            }
            foreign_test_expect(tally, bytes[0].pointer != 0 && bytes[1].pointer != 0 && bytes[0].length == bytes[1].length &&
                                      memcmp(bytes[0].pointer, bytes[1].pointer, bytes[0].length) == 0, "exact/vector complete executable bytes agree", 0);
            foreign_test_expect(tally, original_saved && foreign_unwind_dedup_link_test_unchanged(&original, &input, &input_arena, &input_names) &&
                                      memcmp(&foreign, &original_foreign, sizeof(foreign)) == 0, "composition preserves original producer storage and Foreign metadata", 0);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                arena_release(&compact_arenas[path]);
                arena_release(&compact_names[path]);
                arena_release(&pool_arenas[path]);
                arena_release(&pool_names[path]);
            }
        }
        arena_release(&input_arena);
        arena_release(&input_names);
        foreign_release(&foreign);
        if (cross_check)
        {
            foreign_dynamic_test_oracles(tally, directory, line, cpp ? 102 : 101, scratch);
        }
    }
    runtime_release(&runtime);
    arena_pop_to(scratch, saved);
}

static bool foreign_unwind_dedup_link_test(const char* directory, Arena* arena, bool cross_check, ForeignTestSystem* system)
{
    ForeignTestTally tally = {
        .check = "foreign COMDAT/GC/CIE output composition",
    };
    foreign_unwind_dedup_link_test_program(&tally, directory, arena, cross_check, system, false);
    foreign_unwind_dedup_link_test_program(&tally, directory, arena, cross_check, system, true);
    return foreign_test_report(&tally);
}
