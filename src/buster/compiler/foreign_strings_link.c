// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Consumer of foreign_strings_build's immutable offset map. atom_inputs maps original Link atoms to input index + 1
// (zero means untouched); section_symbols identifies original ELF STT_SECTION symbols, which Link's flags cannot
// distinguish from named locals. Call after reachability and before final executable layout, not before ET_REL
// writing: the pooled Link no longer represents separately linkable input sections. The caller retains the original section
// -> atom map alongside atom_inputs to translate subsequent diagnostic/source offsets; those original atom ids remain
// valid identifiers but no longer own storage after this pass. Symbol ids, names, fixup ids and export lists are stable.
//
// A shared STRINGS atom at unit zero owns the pool. This specific immutable literal pool is allowed as a local
// target across input units by link_check_target. The producer's arrays/pool must live until executable_write returns.
// Failure changes no Link column. Temporary translated symbol values and only the affected section-symbol addends
// are computed before the final stores. Named symbol addends, size expressions and GOT-relative expressions stay
// unchanged; named absolute addresses and data PC64 expressions must stay within their original string. Absolute
// section-symbol expressions map their original S+A. All PC32/CALL and section-symbol PC64 references are
// conservatively unsupported: instruction field-end bias is not retained in Link and can cross a string boundary. The caller may retain
// the unchanged Link on UNSUPPORTED, rather than guessing that bias from a relocation kind alone.

static __attribute__((unused)) ForeignStringsError foreign_strings_apply(Link* link, const ForeignStrings* strings,
                                                                         const uint32_t* atom_inputs, const uint8_t* section_symbols);

typedef struct ForeignStringsMerge ForeignStringsMerge;
struct ForeignStringsMerge
{
    ForeignStrings strings;
    // Original atom id -> offset-map input index + 1. Original per-object section maps remain identifiers into this
    // column after storage is pooled; use strings' offset map to translate original section offsets.
    uint32_t* atom_inputs;
    uint64_t input_bytes;
    bool applied;
};

static __attribute__((unused)) ForeignStringsError foreign_strings_merge(ForeignLink* converted, const Foreign* foreign,
                                                                         Arena* arena, ForeignStringsMerge* output);

static bool foreign_strings_direct(uint8_t kind)
{
    return kind == BUSTER_LINK_FIXUP_ABS64 || kind == BUSTER_LINK_FIXUP_ABS32 || kind == BUSTER_LINK_FIXUP_ABS32S;
}

static ForeignStringsError foreign_strings_apply(Link* link, const ForeignStrings* strings,
                                                 const uint32_t* atom_inputs, const uint8_t* section_symbols)
{
    ForeignStringsError error = link == 0 || strings == 0 || atom_inputs == 0 || section_symbols == 0 ?
                                FOREIGN_STRINGS_ERROR_INPUT : FOREIGN_STRINGS_ERROR_NONE;
    AtomId pool = 0;
    uint8_t alignment = 0;
    Scratch scratch = scratch_begin(0);
    uint64_t* values = error == FOREIGN_STRINGS_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, link->symbols.count) : 0;
    if (error == FOREIGN_STRINGS_ERROR_NONE && values == 0)
    {
        error = FOREIGN_STRINGS_ERROR_MEMORY;
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE && strings->size > INT32_MAX)
    {
        error = FOREIGN_STRINGS_ERROR_CAPACITY;
    }
    for (AtomId atom = 1; error == FOREIGN_STRINGS_ERROR_NONE && atom < link->atoms.count; atom += 1)
    {
        const uint32_t input = atom_inputs[atom];
        if (input != 0)
        {
            if (input > strings->input_count || (link->atoms.class[atom] != BUSTER_LINK_CLASS_STRINGS && link->atoms.class[atom] != BUSTER_LINK_CLASS_RODATA) ||
                link->atoms.size[atom] == 0 || link->atoms.size[atom] > UINT32_MAX)
            {
                error = FOREIGN_STRINGS_ERROR_INPUT;
            }
            else if (link->atoms.fixup_count[atom] != 0)
            {
                error = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
            }
            else
            {
                const uint32_t begin = strings->section_begin[input - 1];
                const uint32_t end = strings->section_begin[input];
                const uint64_t size = end > begin ? (uint64_t)strings->input_offset[end - 1] + strings->lengths[end - 1] : 0;
                error = size == link->atoms.size[atom] ? FOREIGN_STRINGS_ERROR_NONE : FOREIGN_STRINGS_ERROR_INPUT;
                pool = pool == 0 ? atom : pool;
                alignment = link->atoms.alignment_log2[atom] > alignment ? link->atoms.alignment_log2[atom] : alignment;
            }
        }
    }
    for (SymbolId symbol = 0; error == FOREIGN_STRINGS_ERROR_NONE && symbol < link->symbols.count; symbol += 1)
    {
        const AtomId atom = link->symbols.atom[symbol];
        const uint32_t input = atom_inputs[atom];
        values[symbol] = link->symbols.value[symbol];
        if (input != 0)
        {
            uint32_t mapped = 0;
            const uint64_t value = values[symbol];
            const uint64_t size = link->symbols.size[symbol];
            const bool valid = foreign_strings_offset(strings, input - 1, value, &mapped);
            uint32_t entry = 0;
            // A named object may not span independently merged entries, including wide-code-unit terminators.
            const bool single = valid && foreign_strings_entry(strings, input - 1, value, &entry) &&
                                size <= (uint64_t)strings->input_offset[entry] + strings->lengths[entry] - value;
            if (!single)
            {
                error = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
            }
            else
            {
                values[symbol] = mapped;
            }
        }
    }
    uint32_t changed = 0;
    for (FixupId fixup = 1; error == FOREIGN_STRINGS_ERROR_NONE && fixup < link->fixups.count; fixup += 1)
    {
        const SymbolId symbol = link->fixups.target[fixup];
        const AtomId atom = link->symbols.atom[symbol];
        const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
        const bool pooled = atom_inputs[atom] != 0;
        const bool section = pooled && section_symbols[symbol] != 0;
        if (pooled && (kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_CALL ||
                       (section && kind == BUSTER_LINK_FIXUP_PC64)))
        {
            error = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
        }
        if (pooled && !section && (foreign_strings_direct(kind) || kind == BUSTER_LINK_FIXUP_PC64))
        {
            const int64_t value = (int64_t)link->symbols.value[symbol];
            const int64_t addend = link_fixup_addend(link, fixup);
            const bool bounded = addend >= -value && addend < (int64_t)link->atoms.size[atom] - value;
            const uint64_t target = bounded ? (uint64_t)(value + addend) : 0;
            uint32_t source_entry = 0;
            uint32_t target_entry = 0;
            // A named absolute or data-PC64 expression crossing a terminator depends on original adjacency.
            // Keeping its addend after pooling would silently change that address, even if each entry is valid.
            if (!bounded || !foreign_strings_entry(strings, atom_inputs[atom] - 1, (uint64_t)value, &source_entry) ||
                !foreign_strings_entry(strings, atom_inputs[atom] - 1, target, &target_entry) || source_entry != target_entry)
            {
                error = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
            }
        }
        changed += section && foreign_strings_direct(kind) ? 1 : 0;
    }
    FixupId* ids = error == FOREIGN_STRINGS_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, FixupId, (uint64_t)changed + 1) : 0;
    int32_t* addends = error == FOREIGN_STRINGS_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, int32_t, (uint64_t)changed + 1) : 0;
    if (error == FOREIGN_STRINGS_ERROR_NONE && (ids == 0 || addends == 0))
    {
        error = FOREIGN_STRINGS_ERROR_MEMORY;
    }
    uint32_t next = 0;
    for (FixupId fixup = 1; error == FOREIGN_STRINGS_ERROR_NONE && fixup < link->fixups.count; fixup += 1)
    {
        const SymbolId symbol = link->fixups.target[fixup];
        const AtomId atom = link->symbols.atom[symbol];
        const uint32_t input = atom_inputs[atom];
        const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
        if (input != 0 && section_symbols[symbol] != 0 && foreign_strings_direct(kind))
        {
            const int64_t value = (int64_t)link->symbols.value[symbol];
            const int64_t addend = link_fixup_addend(link, fixup);
            uint32_t mapped = 0;
            const bool bounded = addend >= -value && addend < (int64_t)link->atoms.size[atom] - value;
            if (!bounded || !foreign_strings_offset(strings, input - 1, (uint64_t)(value + addend), &mapped))
            {
                // A section-relative expression may legally name its endpoint or an address outside its bytes.
                // There is no pooled entry to translate, so retain the original link through normal fallback.
                error = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
            }
            else
            {
                ids[next] = fixup;
                addends[next] = (int32_t)((int64_t)mapped - (int64_t)values[symbol]);
                next += 1;
            }
        }
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE && pool != 0)
    {
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            if (atom_inputs[link->symbols.atom[symbol]] != 0)
            {
                link->symbols.atom[symbol] = pool;
                link->symbols.value[symbol] = values[symbol];
            }
        }
        for (uint32_t i = 0; i < next; i += 1)
        {
            link->fixups.addend[ids[i]] = addends[i];
            link->fixups.kind[ids[i]] &= BUSTER_LINK_FIXUP_KIND;
        }
        for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
        {
            if (atom_inputs[atom] != 0)
            {
                link->atoms.size[atom] = atom == pool ? strings->size : 0;
                link->atoms.bytes[atom] = atom == pool ? strings->bytes : 0;
                link->atoms.alignment_log2[atom] = atom == pool ? alignment : 0;
                link->atoms.class[atom] = BUSTER_LINK_CLASS_STRINGS;
                link->atoms.unit[atom] = 0;
            }
        }
    }
    scratch_end(scratch);
    return error;
}

// The foreign metadata adapter preserves the information lost in Link: section compatibility and STT_SECTION.
// It runs after COMDAT/GC has finalized converted->objects' section and symbol maps. A failure, including an
// UNSUPPORTED consumer expression, leaves converted unchanged and releases all newly allocated output storage.
static ForeignStringsError foreign_strings_merge(ForeignLink* converted, const Foreign* foreign, Arena* arena, ForeignStringsMerge* output)
{
    Link* link = &converted->link;
    const uint64_t saved = arena->length;
    ForeignStringsMerge merged = { 0 };
    uint32_t count = 0;
    ForeignStringsError error = FOREIGN_STRINGS_ERROR_NONE;
    Scratch scratch = scratch_begin(arena);
    uint8_t* units = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, link->atoms.count);
    if (units == 0)
    {
        error = FOREIGN_STRINGS_ERROR_MEMORY;
    }
    else
    {
        memset(units, 0, link->atoms.count);
    }
    // Generic import keeps wide strings as RODATA: Link's ordinary STRINGS class writes one-byte ET_REL entries.
    // This final-executable adapter still has original ELF metadata and can select wide entries without losing it
    // at the general importer/object-writer boundary. Zero means unselected; selected atoms carry unit_log2 + 1.
    for (uint32_t id = 1; error == FOREIGN_STRINGS_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        const ForeignObject* original = &foreign->objects[id];
        const ImportObject* imported = &converted->objects[id];
        for (uint32_t section = 1; original->library == 0 && section < imported->section_count; section += 1)
        {
            const AtomId atom = imported->section_atoms[section];
            const uint8_t class = link->atoms.class[atom];
            if (atom != 0 && link->atoms.size[atom] != 0 && (class == BUSTER_LINK_CLASS_STRINGS || class == BUSTER_LINK_CLASS_RODATA))
            {
                const Elf64_Shdr header = import_section_header(&original->elf, section);
                const bool strings = header.sh_type == SHT_PROGBITS && (header.sh_entsize == 1 || header.sh_entsize == 2 || header.sh_entsize == 4) &&
                                     (header.sh_flags & (SHF_MERGE | SHF_STRINGS)) == (SHF_MERGE | SHF_STRINGS);
                if (strings)
                {
                    count += units[atom] == 0 ? 1 : 0;
                    units[atom] = header.sh_entsize == 4 ? 3 : header.sh_entsize == 2 ? 2 : 1;
                }
            }
        }
    }
    ForeignStringInput* inputs = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ForeignStringInput, (uint64_t)count + 1);
    String8* names = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, String8, (uint64_t)count + 1);
    uint64_t* flags = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)count + 1);
    uint8_t* sections = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, link->symbols.count);
    merged.atom_inputs = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, link->atoms.count);
    uint64_t slots = 1;
    while (slots < 2 * (uint64_t)count)
    {
        slots *= 2;
    }
    uint32_t* groups = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, slots);
    if (inputs == 0 || names == 0 || flags == 0 || sections == 0 || merged.atom_inputs == 0 || groups == 0)
    {
        error = FOREIGN_STRINGS_ERROR_MEMORY;
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        memset(merged.atom_inputs, 0, link->atoms.count * sizeof(uint32_t));
        memset(names, 0, ((uint64_t)count + 1) * sizeof(String8));
        memset(sections, 0, link->symbols.count);
        memset(groups, 0, slots * sizeof(uint32_t));
    }
    uint32_t next = 0;
    for (AtomId atom = 1; error == FOREIGN_STRINGS_ERROR_NONE && atom < link->atoms.count; atom += 1)
    {
        if (units[atom] != 0)
        {
            if (link->atoms.size[atom] > UINT32_MAX)
            {
                error = FOREIGN_STRINGS_ERROR_CAPACITY;
            }
            else if (link->atoms.fixup_count[atom] != 0)
            {
                error = FOREIGN_STRINGS_ERROR_UNSUPPORTED;
            }
            else
            {
                inputs[next] = (ForeignStringInput){
                    .bytes = link->atoms.bytes[atom],
                    .size = (uint32_t)link->atoms.size[atom],
                    .alignment_log2 = link->atoms.alignment_log2[atom],
                    .unit_log2 = units[atom] - 1,
                };
                merged.input_bytes += link->atoms.size[atom];
                merged.atom_inputs[atom] = next + 1;
                next += 1;
            }
        }
    }
    for (uint32_t id = 1; error == FOREIGN_STRINGS_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        const ForeignObject* original = &foreign->objects[id];
        const ImportObject* imported = &converted->objects[id];
        for (uint32_t section = 1; original->library == 0 && section < imported->section_count; section += 1)
        {
            const AtomId atom = imported->section_atoms[section];
            const uint32_t index = merged.atom_inputs[atom];
            if (index != 0)
            {
                const Elf64_Shdr header = import_section_header(&original->elf, section);
                const bool valid = header.sh_type == SHT_PROGBITS && (header.sh_entsize == 1 || header.sh_entsize == 2 || header.sh_entsize == 4) &&
                                   (header.sh_flags & (SHF_MERGE | SHF_STRINGS)) == (SHF_MERGE | SHF_STRINGS) &&
                                   import_section_name(&original->elf, &header, &names[index - 1]);
                flags[index - 1] = header.sh_flags;
                inputs[index - 1].unit_log2 = header.sh_entsize == 4 ? 2 : header.sh_entsize == 2 ? 1 : 0;
                error = valid ? error : FOREIGN_STRINGS_ERROR_INPUT;
            }
        }
        for (uint32_t index = 1; original->library == 0 && index < imported->symbol_count; index += 1)
        {
            const SymbolId symbol = imported->symbols[index];
            if (symbol != 0)
            {
                const Elf64_Sym value = import_symbol(&original->symbols, index);
                sections[symbol] |= ELF64_ST_TYPE(value.st_info) == STT_SECTION ? 1 : 0;
            }
        }
    }
    for (uint32_t input = 0; error == FOREIGN_STRINGS_ERROR_NONE && input < count; input += 1)
    {
        if (names[input].pointer == 0)
        {
            error = FOREIGN_STRINGS_ERROR_INPUT;
        }
        else
        {
            const uint64_t hash = link_hash((const uint8_t*)names[input].pointer, names[input].length) ^ flags[input] ^ inputs[input].unit_log2;
            uint64_t slot = hash & (slots - 1);
            bool same = false;
            while (groups[slot] != 0 && !same)
            {
                const uint32_t prior = groups[slot] - 1;
                same = flags[prior] == flags[input] && inputs[prior].unit_log2 == inputs[input].unit_log2 && names[prior].length == names[input].length &&
                       memcmp(names[prior].pointer, names[input].pointer, names[input].length) == 0;
                if (!same)
                {
                    slot = (slot + 1) & (slots - 1);
                }
            }
            groups[slot] = same ? groups[slot] : input + 1;
            inputs[input].group = groups[slot];
        }
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        error = foreign_strings_build(inputs, count, foreign->path, arena, &merged.strings);
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE && merged.strings.size < merged.input_bytes)
    {
        error = foreign_strings_apply(link, &merged.strings, merged.atom_inputs, sections);
        merged.applied = error == FOREIGN_STRINGS_ERROR_NONE;
    }
    scratch_end(scratch);
    if (error != FOREIGN_STRINGS_ERROR_NONE || !merged.applied)
    {
        arena_pop_to(arena, saved);
        merged.strings = (ForeignStrings){ 0 };
        merged.atom_inputs = 0;
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        *output = merged;
    }
    return error;
}
