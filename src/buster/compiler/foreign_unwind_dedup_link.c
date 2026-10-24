// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// ForeignLink consumer for terminal-executable CIE pooling. Include after foreign_gc_link.c and
// foreign_unwind_dedup.c. The result is a distinct wrapper: converted may flow only to terminal string pooling and
// executable writing, NEVER back to GC, original relocation import, or the relocatable-object writer. Historical
// converted.unwind plans retain pre-pooling byte coordinates; their code-owner AtomIds are remapped. sections is
// the authoritative original-ELF -> pooled offset map, including each original section's explicit endpoint.
//
// SymbolIds and NameIds are preserved. Every mutable Link column, emitted byte, object map, export/root array and
// unwind record map is fresh. Immutable library metadata and historical unwind bytes remain borrowed from input;
// Foreign, input, and both output arenas must outlive the result. A failure rolls both arenas back and leaves
// output/input untouched. Unsupported input retains the unchanged input; pinned atoms are a successful no-op.
// Shared-object output is outside this initial executable contract and returns UNSUPPORTED unchanged.
// This bounded opt-in implementation copies the whole link for atomicity; measure that cost before enabling it.

typedef struct ForeignUnwindDedupSection ForeignUnwindDedupSection;
struct ForeignUnwindDedupSection
{
    uint32_t object;
    uint32_t section;
    AtomId atom;
    uint64_t old_length;
    uint64_t endpoint;
    ForeignUnwindRecord* records;
    uint32_t record_count;
};

typedef struct ForeignUnwindDedupLink ForeignUnwindDedupLink;
struct ForeignUnwindDedupLink
{
    ForeignLink converted;
    ForeignUnwindDedup plan;
    ForeignGcRoots roots;
    ForeignUnwindDedupSection* sections;
    uint32_t section_count;
};

static bool foreign_unwind_dedup_section_offset(const ForeignUnwindDedupSection* section, uint64_t offset, uint64_t* mapped)
{
    const ForeignUnwind plan = {
        .bytes = {
            .length = section->endpoint,
        },
        .records = section->records,
        .record_count = section->record_count,
        .old_length = section->old_length,
    };
    return offset <= section->old_length && foreign_unwind_offset(&plan, offset, mapped);
}

static ForeignLinkResult foreign_unwind_dedup_link_pins(uint8_t* pins, const Foreign* foreign, const ForeignLink* input,
                                                      const ForeignGcRoots* roots, Arena* arena)
{
    ForeignGcLinkState automatic = { 0 };
    ForeignLinkResult result = foreign_gc_link_roots(&automatic, foreign, input, roots, arena);
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        memset(pins, 0, input->link.atoms.count);
        const SymbolId entry = automatic.roots.entry;
        if (entry >= input->link.symbols.count)
        {
            result.error = FOREIGN_LINK_ERROR_INPUT;
        }
        else if (entry != 0)
        {
            pins[input->link.symbols.atom[entry]] = 1;
        }
        for (uint32_t i = 0; result.error == FOREIGN_LINK_ERROR_NONE && i < automatic.roots.export_count; i += 1)
        {
            const SymbolId symbol = automatic.roots.exports[i];
            if (symbol == 0 || symbol >= input->link.symbols.count)
            {
                result.error = FOREIGN_LINK_ERROR_INPUT;
            }
            else
            {
                pins[input->link.symbols.atom[symbol]] = 1;
            }
        }
        for (uint32_t i = 0; result.error == FOREIGN_LINK_ERROR_NONE && i < automatic.roots.retained_count; i += 1)
        {
            const AtomId atom = automatic.roots.retained[i];
            if (atom == 0 || atom >= input->link.atoms.count)
            {
                result.error = FOREIGN_LINK_ERROR_INPUT;
            }
            else
            {
                pins[atom] = 1;
            }
        }
    }
    return result;
}

static ForeignLinkResult foreign_unwind_dedup_link_copy(ForeignUnwindDedupLink* output, const ForeignLink* input, Arena* arena, Arena* name_arena)
{
    const Link* before = &input->link;
    const ForeignUnwindDedup* plan = &output->plan;
    Link* after = &output->converted.link;
    ForeignLinkResult result = { .error = link_initialize(after, arena, name_arena, plan->capacity) ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY };
    for (NameId name = 1; result.error == FOREIGN_LINK_ERROR_NONE && name < before->names.count; name += 1)
    {
        if (link_add_name_hashed(after, before->names.bytes[name], before->names.length[name], before->names.hash[name]) != name)
        {
            result.error = FOREIGN_LINK_ERROR_LINK;
        }
    }
    for (AtomId owner = 1; result.error == FOREIGN_LINK_ERROR_NONE && owner < before->atoms.count; owner += 1)
    {
        const ForeignUnwindDedupAtom* map = &plan->maps[owner];
        if (map->owner == owner)
        {
            uint8_t* bytes = 0;
            const bool zero_fill = link_class_is_zero_fill(before->atoms.class[owner]);
            if (!zero_fill)
            {
                bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, map->length);
                result.error = bytes != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
            }
            if (result.error == FOREIGN_LINK_ERROR_NONE && !zero_fill)
            {
                if (map->record_count == 0)
                {
                    if (map->length != 0)
                    {
                        memcpy(bytes, before->atoms.bytes[owner], map->length);
                    }
                }
                else
                {
                    for (AtomId atom = owner; result.error == FOREIGN_LINK_ERROR_NONE && atom != 0; atom = plan->maps[atom].next)
                    {
                        const ForeignUnwindDedupAtom* member = &plan->maps[atom];
                        for (uint32_t r = 0; result.error == FOREIGN_LINK_ERROR_NONE && r < member->record_count; r += 1)
                        {
                            const ForeignUnwindRecord* record = &member->records[r];
                            if (member->kept[r])
                            {
                                memcpy(bytes + record->new_offset, before->atoms.bytes[atom] + record->old_offset, record->length);
                                if (record->kind == FOREIGN_UNWIND_FDE)
                                {
                                    uint64_t cie = 0;
                                    const bool mapped = foreign_unwind_dedup_offset(member, before->atoms.size[atom], record->cie_offset, &cie);
                                    const bool fits = mapped && cie <= record->new_offset + 4 && record->new_offset + 4 - cie <= UINT32_MAX;
                                    if (!fits)
                                    {
                                        result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
                                    }
                                    else
                                    {
                                        const uint32_t distance = (uint32_t)(record->new_offset + 4 - cie);
                                        memcpy(bytes + record->new_offset + 4, &distance, sizeof(distance));
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // Pool source unit zero permits its validated outgoing relocations to original local code/LSDA.
            // No incoming-local target allowance is needed: such EH atoms were pinned before pool formation.
            const uint32_t unit = map->pooled ? 0 : before->atoms.unit[owner];
            if (result.error == FOREIGN_LINK_ERROR_NONE && link_add_atom(after, before->atoms.class[owner], map->alignment_log2,
                                                                        map->length, bytes, unit) != plan->atoms[owner])
            {
                result.error = FOREIGN_LINK_ERROR_LINK;
            }
        }
    }
    for (SymbolId symbol = 1; result.error == FOREIGN_LINK_ERROR_NONE && symbol < before->symbols.count; symbol += 1)
    {
        const AtomId atom = before->symbols.atom[symbol];
        uint64_t value = before->symbols.value[symbol];
        if (atom != 0 && !foreign_unwind_dedup_offset(&plan->maps[atom], before->atoms.size[atom], value, &value))
        {
            result.error = FOREIGN_LINK_ERROR_INPUT;
        }
        if (result.error == FOREIGN_LINK_ERROR_NONE)
        {
            const SymbolId added = link_add_symbol(after, before->symbols.name[symbol], plan->atoms[atom], value, before->symbols.size[symbol],
                                                  before->symbols.flags[symbol], before->symbols.source[symbol]);
            if (added != symbol)
            {
                result.error = FOREIGN_LINK_ERROR_LINK;
            }
            else
            {
                after->symbols.version[symbol] = before->symbols.version[symbol];
            }
        }
    }
    for (AtomId owner = 1; result.error == FOREIGN_LINK_ERROR_NONE && owner < before->atoms.count; owner += 1)
    {
        if (plan->maps[owner].owner == owner)
        {
            for (AtomId atom = owner; result.error == FOREIGN_LINK_ERROR_NONE && atom != 0; atom = plan->maps[atom].next)
            {
                const FixupId begin = before->atoms.fixup_begin[atom];
                for (uint32_t i = 0; result.error == FOREIGN_LINK_ERROR_NONE && i < before->atoms.fixup_count[atom]; i += 1)
                {
                    const FixupId fixup = begin + i;
                    if (plan->fixups[fixup] != 0)
                    {
                        uint64_t offset = before->fixups.offset[fixup];
                        if (!foreign_unwind_dedup_offset(&plan->maps[atom], before->atoms.size[atom], offset, &offset) || offset > UINT32_MAX)
                        {
                            result.error = FOREIGN_LINK_ERROR_INPUT;
                        }
                        else if (link_add_fixup(after, plan->atoms[atom], (uint32_t)offset, before->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND,
                                               before->fixups.target[fixup], link_fixup_addend(before, fixup)) != plan->fixups[fixup])
                        {
                            result.error = FOREIGN_LINK_ERROR_LINK;
                        }
                    }
                }
            }
        }
    }
    for (uint32_t i = 1; result.error == FOREIGN_LINK_ERROR_NONE && i < before->priorities.count; i += 1)
    {
        if (!link_add_priority(after, plan->atoms[before->priorities.atom[i]], before->priorities.priority[i]))
        {
            result.error = FOREIGN_LINK_ERROR_LINK;
        }
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && link_check(after) != LINK_ERROR_NONE)
    {
        result.error = FOREIGN_LINK_ERROR_LINK;
    }
    return result;
}

static ForeignLinkResult foreign_unwind_dedup_link_external(ForeignUnwindDedupLink* output, const Foreign* foreign, const ForeignLink* input,
                                                          const ForeignGcRoots* roots, const ForeignGcLinkState* frames, Arena* arena)
{
    ForeignLink* built = &output->converted;
    const ForeignUnwindDedup* plan = &output->plan;
    built->names = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, foreign->names.count);
    built->objects = BUSTER_ARENA_PUSH_ARRAY(arena, ImportObject, foreign->object_count);
    built->unwind = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignLinkUnwind*, foreign->object_count);
    SymbolId* exports = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, input->dynamic.export_count);
    SymbolId* root_exports = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, roots->export_count);
    AtomId* retained = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, roots->retained_count);
    ForeignLinkResult result = { .error = built->names != 0 && built->objects != 0 && built->unwind != 0 && exports != 0 && root_exports != 0 && retained != 0 ?
                                        FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY };
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        memset(built->unwind, 0, (uint64_t)foreign->object_count * sizeof(*built->unwind));
        for (uint32_t name = 0; name < foreign->names.count; name += 1)
        {
            built->names[name] = input->names[name];
        }
        built->dynamic = input->dynamic;
        built->dynamic.exports = exports;
        built->library_versions = input->library_versions;
        for (uint32_t i = 0; i < input->dynamic.export_count; i += 1)
        {
            exports[i] = input->dynamic.exports[i];
        }
        output->roots = *roots;
        output->roots.exports = root_exports;
        output->roots.retained = retained;
        for (uint32_t i = 0; i < roots->export_count; i += 1)
        {
            root_exports[i] = roots->exports[i];
        }
        for (uint32_t i = 0; i < roots->retained_count; i += 1)
        {
            retained[i] = plan->atoms[roots->retained[i]];
        }
    }
    for (uint32_t id = 0; result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        const ImportObject* original = &input->objects[id];
        ImportObject* object = &built->objects[id];
        *object = *original;
        object->section_atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, original->section_count);
        object->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, original->symbol_count);
        if (object->section_atoms == 0 || object->symbols == 0)
        {
            result.error = FOREIGN_LINK_ERROR_MEMORY;
        }
        else
        {
            for (uint32_t section = 0; section < original->section_count; section += 1)
            {
                object->section_atoms[section] = plan->atoms[original->section_atoms[section]];
            }
            for (uint32_t symbol = 0; symbol < original->symbol_count; symbol += 1)
            {
                object->symbols[symbol] = original->symbols[symbol];
            }
        }
        ForeignLinkUnwind** previous = &built->unwind[id];
        for (const ForeignLinkUnwind* source = input->unwind != 0 ? input->unwind[id] : 0;
             result.error == FOREIGN_LINK_ERROR_NONE && source != 0; source = source->next)
        {
            ForeignLinkUnwind* historical = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignLinkUnwind, 1);
            ForeignUnwindRecord* records = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindRecord, source->plan.record_count);
            if (historical == 0 || records == 0)
            {
                result.error = FOREIGN_LINK_ERROR_MEMORY;
            }
            else
            {
                *historical = *source;
                historical->next = 0;
                historical->plan.records = records;
                for (uint32_t r = 0; r < source->plan.record_count; r += 1)
                {
                    records[r] = source->plan.records[r];
                    if (records[r].atom >= plan->atom_count)
                    {
                        result.error = FOREIGN_LINK_ERROR_INPUT;
                    }
                    else
                    {
                        records[r].atom = plan->atoms[records[r].atom];
                    }
                }
                *previous = historical;
                previous = &historical->next;
            }
        }
    }
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < plan->atom_count; atom += 1)
    {
        output->section_count += frames->frames[atom] != 0 ? 1 : 0;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        output->sections = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindDedupSection, output->section_count);
        result.error = output->sections != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
    }
    uint32_t at = 0;
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < plan->atom_count; atom += 1)
    {
        const ForeignGcFrame* frame = frames->frames[atom];
        if (frame != 0)
        {
            const ForeignUnwind* original = frame->original;
            const bool pruned = foreign_link_unwind(input, frame->object, frame->section) != 0;
            ForeignUnwindDedupSection* section = &output->sections[at];
            *section = (ForeignUnwindDedupSection){
                .object = frame->object,
                .section = frame->section,
                .atom = plan->atoms[atom],
                .old_length = original->old_length,
                .endpoint = plan->maps[atom].endpoint,
                .records = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindRecord, original->record_count),
                .record_count = original->record_count,
            };
            result.error = section->records != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
            for (uint32_t r = 0; result.error == FOREIGN_LINK_ERROR_NONE && r < original->record_count; r += 1)
            {
                ForeignUnwindRecord record = original->records[r];
                const uint64_t current = pruned ? record.new_offset : record.old_offset;
                record.new_offset = UINT64_MAX;
                if (current != UINT64_MAX && !foreign_unwind_dedup_offset(&plan->maps[atom], input->link.atoms.size[atom], current, &record.new_offset))
                {
                    result.error = FOREIGN_LINK_ERROR_INPUT;
                }
                if (record.atom >= plan->atom_count)
                {
                    result.error = FOREIGN_LINK_ERROR_INPUT;
                }
                else
                {
                    record.atom = plan->atoms[record.atom];
                }
                section->records[r] = record;
            }
            at += 1;
        }
    }
    return result;
}

static __attribute__((unused)) ForeignLinkResult foreign_unwind_dedup_link(ForeignUnwindDedupLink* output, const Foreign* foreign, const ForeignLink* input,
                                                                         const ForeignGcRoots* roots, ForeignPath path, Arena* arena, Arena* name_arena)
{
    const uint64_t saved = arena != 0 ? arena->length : 0;
    const uint64_t saved_names = name_arena != 0 ? name_arena->length : 0;
    const bool arguments = output != 0 && foreign != 0 && input != 0 && roots != 0 && arena != 0 && name_arena != 0 &&
                           &output->converted != input && (path == FOREIGN_PATH_EXACT || path == FOREIGN_PATH_VECTOR);
    ForeignLinkResult result = { .error = arguments ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_INPUT };
    if (result.error == FOREIGN_LINK_ERROR_NONE && (!foreign->started || foreign->failure.error != FOREIGN_ERROR_NONE || input->names == 0 || input->objects == 0 ||
                                                  input->link.error != LINK_ERROR_NONE || input->link.atoms.count == 0 || input->link.symbols.count == 0 ||
                                                  input->link.names.count == 0 || input->link.fixups.count == 0 || input->link.priorities.count == 0))
    {
        result.error = FOREIGN_LINK_ERROR_INPUT;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        Link checked = input->link;
        result.error = link_check(&checked) == LINK_ERROR_NONE ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_INPUT;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && input->dynamic.shared)
    {
        result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
    }
    // Patching/layout consumes the representation. Reprocessing it would compare relocated CIE fields and names.
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < input->link.atoms.count; atom += 1)
    {
        if (input->link.atoms.address[atom] != 0)
        {
            result.error = FOREIGN_LINK_ERROR_INPUT;
        }
    }
    for (SymbolId symbol = 1; result.error == FOREIGN_LINK_ERROR_NONE && symbol < input->link.symbols.count; symbol += 1)
    {
        if (input->link.symbols.reference[symbol] != 0 || input->link.symbols.got_slot[symbol] != 0 || input->link.symbols.dynamic_index[symbol] != 0)
        {
            result.error = FOREIGN_LINK_ERROR_INPUT;
        }
    }
    Scratch scratch = result.error == FOREIGN_LINK_ERROR_NONE ? scratch_begin(arena) : (Scratch){ 0 };
    if (result.error == FOREIGN_LINK_ERROR_NONE && (scratch.arena == name_arena || scratch.arena->base == 0))
    {
        result.error = FOREIGN_LINK_ERROR_MEMORY;
    }
    ForeignGcLinkState frames = { 0 };
    ForeignUnwindDedupLink built = { 0 };
    const ForeignUnwind** current = 0;
    uint8_t* pins = 0;
    uint8_t* section_symbols = 0;
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        current = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, const ForeignUnwind*, input->link.atoms.count);
        pins = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, input->link.atoms.count);
        section_symbols = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, input->link.symbols.count);
        result.error = current != 0 && pins != 0 && section_symbols != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        memset(section_symbols, 0, input->link.symbols.count);
    }
    for (uint32_t id = 0; result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        const ImportObject* object = &input->objects[id];
        for (uint32_t section = 0; result.error == FOREIGN_LINK_ERROR_NONE && section < object->section_count; section += 1)
        {
            if (object->section_atoms == 0 || object->section_atoms[section] >= input->link.atoms.count)
            {
                result.error = FOREIGN_LINK_ERROR_INPUT;
            }
        }
        for (uint32_t i = 0; result.error == FOREIGN_LINK_ERROR_NONE && i < object->symbol_count; i += 1)
        {
            const SymbolId symbol = object->symbols != 0 ? object->symbols[i] : input->link.symbols.count;
            if (symbol >= input->link.symbols.count || i >= foreign->objects[id].symbols.count)
            {
                result.error = FOREIGN_LINK_ERROR_INPUT;
            }
            else if (symbol != 0)
            {
                const Elf64_Sym original = import_symbol(&foreign->objects[id].symbols, i);
                section_symbols[symbol] = ELF64_ST_TYPE(original.st_info) == STT_SECTION ? 1 : section_symbols[symbol];
            }
        }
    }
    for (uint32_t name = 0; result.error == FOREIGN_LINK_ERROR_NONE && name < foreign->names.count; name += 1)
    {
        if (input->names[name] >= input->link.symbols.count)
        {
            result.error = FOREIGN_LINK_ERROR_INPUT;
        }
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_unwind_dedup_link_pins(pins, foreign, input, roots, scratch.arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_gc_link_frames(&frames, foreign, input, path, scratch.arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        for (AtomId atom = 0; atom < input->link.atoms.count; atom += 1)
        {
            current[atom] = frames.frames[atom] != 0 ? &frames.frames[atom]->current : 0;
        }
        const ForeignUnwindResult planned = foreign_unwind_dedup_plan(&input->link, current, pins, section_symbols, path, arena, &built.plan);
        result.error = planned.error == FOREIGN_UNWIND_ERROR_NONE ? FOREIGN_LINK_ERROR_NONE : planned.error == FOREIGN_UNWIND_ERROR_MEMORY ?
                       FOREIGN_LINK_ERROR_MEMORY : planned.error == FOREIGN_UNWIND_ERROR_UNSUPPORTED ? FOREIGN_LINK_ERROR_UNSUPPORTED : FOREIGN_LINK_ERROR_INPUT;
        result.import.offset = planned.offset;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_unwind_dedup_link_copy(&built, input, arena, name_arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_unwind_dedup_link_external(&built, foreign, input, roots, &frames, arena);
    }
    if (scratch.arena != 0)
    {
        scratch_end(scratch);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        *output = built;
    }
    else
    {
        if (arena != 0)
        {
            arena_pop_to(arena, saved);
        }
        if (name_arena != 0 && name_arena != arena)
        {
            arena_pop_to(name_arena, saved_names);
        }
    }
    return result;
}
