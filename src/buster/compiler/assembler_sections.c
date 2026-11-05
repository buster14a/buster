// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Assembly section identities survive layout as one atom each. The optional writer descriptors preserve their
// original names/flags/entry widths without changing the shared Link classes or the writer's default grouping.

static void asm_section_index(AsmParser* parser, uint32_t id)
{
    if (parser->section_count * 2 > parser->section_mask + 1)
    {
        const uint32_t count = (parser->section_mask + 1) * 2;
        uint32_t* slots = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, count);
        if (slots == 0)
        {
            asm_fail(parser, 0, "arena exhausted indexing sections");
        }
        else
        {
            memset(slots, 0, count * sizeof(uint32_t));
            for (uint32_t i = 1; i < parser->section_count; i += 1)
            {
                const String8 name = parser->sections[i].object.name;
                uint32_t slot = (link_hash((const uint8_t*)name.pointer, name.length) ^ ((uint64_t)parser->sections[i].group * UINT64_C(0x9e3779b97f4a7c15))) & (count - 1);
                while (slots[slot] != 0)
                {
                    slot = (slot + 1) & (count - 1);
                }
                slots[slot] = i;
            }
            parser->section_slots = slots;
            parser->section_mask = count - 1;
        }
    }
    else
    {
        const String8 name = parser->sections[id].object.name;
        uint32_t slot = (link_hash((const uint8_t*)name.pointer, name.length) ^ ((uint64_t)parser->sections[id].group * UINT64_C(0x9e3779b97f4a7c15))) & parser->section_mask;
        while (parser->section_slots[slot] != 0)
        {
            slot = (slot + 1) & parser->section_mask;
        }
        parser->section_slots[slot] = id;
    }
    // Grouped sections may repeat a name. Record the ambiguity for section-name expressions. Ordinary assembly
    // pays no extra table allocation.
    if (parser->result.error == 0 && parser->group_count != 0)
    {
        const bool rebuild = parser->section_name_slots == 0 || parser->section_name_mask != parser->section_mask;
        if (rebuild)
        {
            parser->section_name_mask = parser->section_mask;
            parser->section_name_slots = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, (uint64_t)parser->section_mask + 1);
            if (parser->section_name_slots == 0)
            {
                asm_fail(parser, 0, "arena exhausted indexing section names");
            }
            else
            {
                memset(parser->section_name_slots, 0, ((uint64_t)parser->section_mask + 1) * sizeof(uint32_t));
            }
        }
        const uint32_t begin = rebuild ? 1 : id;
        const uint32_t end = rebuild ? parser->section_count : id + 1;
        for (uint32_t i = begin; parser->result.error == 0 && i < end; i += 1)
        {
            const String8 name = parser->sections[i].object.name;
            uint32_t slot = link_hash((const uint8_t*)name.pointer, name.length) & parser->section_name_mask;
            while (parser->section_name_slots[slot] != 0)
            {
                const String8 old = parser->sections[parser->section_name_slots[slot]].object.name;
                if (old.length == name.length && memcmp(old.pointer, name.pointer, name.length) == 0)
                {
                    parser->sections[parser->section_name_slots[slot]].name_ambiguous = true;
                    parser->sections[i].name_ambiguous = true;
                    break;
                }
                slot = (slot + 1) & parser->section_name_mask;
            }
            parser->section_name_slots[slot] = i;
        }
    }
}

static uint32_t asm_find_latest_section(const AsmParser* parser, String8 name)
{
    uint32_t result = 0;
    if (parser->section_name_slots != 0)
    {
        uint32_t slot = link_hash((const uint8_t*)name.pointer, name.length) & parser->section_name_mask;
        result = parser->section_name_slots[slot];
        while (result != 0)
        {
            const String8 old = parser->sections[result].object.name;
            if (old.length == name.length && memcmp(old.pointer, name.pointer, name.length) == 0)
            {
                break;
            }
            slot = (slot + 1) & parser->section_name_mask;
            result = parser->section_name_slots[slot];
        }
    }
    return result;
}

static void asm_sections_initialize(AsmParser* parser)
{
    parser->section_count = BUSTER_LINK_CLASS_COUNT;
    // The remaining reserved capacity is untouched until a directive actually introduces a section.
    memset(parser->sections, 0, BUSTER_LINK_CLASS_COUNT * sizeof(AsmSection));
    parser->section_mask = 31;
    parser->section_slots = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->section_mask + 1);
    if (parser->section_slots == 0)
    {
        asm_fail(parser, 0, "arena exhausted indexing sections");
    }
    else
    {
        memset(parser->section_slots, 0, (parser->section_mask + 1) * sizeof(uint32_t));
    }
    for (uint32_t i = 1; i < BUSTER_LINK_CLASS_COUNT; i += 1)
    {
        const ObjectSectionFormat* format = &object_class_sections[i];
        parser->sections[i] = (AsmSection){
            .object = {
                .name = {.pointer = (char*)format->name, .length = strlen(format->name)},
                .type = i == BUSTER_LINK_CLASS_EH_FRAME ? SHT_PROGBITS : format->type,
                .flags = format->flags,
                .entry_size = format->entry_size,
            },
            .class = i,
        };
        if (parser->result.error == 0)
        {
            asm_section_index(parser, i);
        }
    }
}

static bool asm_section_prefix(String8 name, const char* prefix)
{
    const uint64_t length = strlen(prefix);
    return name.length >= length && memcmp(name.pointer, prefix, length) == 0;
}

static uint32_t asm_find_section(const AsmParser* parser, String8 name, uint32_t group)
{
    uint32_t slot = (link_hash((const uint8_t*)name.pointer, name.length) ^ ((uint64_t)group * UINT64_C(0x9e3779b97f4a7c15))) & parser->section_mask;
    uint32_t result = parser->section_slots[slot];
    while (result != 0)
    {
        const String8 candidate = parser->sections[result].object.name;
        if (parser->sections[result].group == group && candidate.length == name.length && memcmp(candidate.pointer, name.pointer, name.length) == 0)
        {
            break;
        }
        slot = (slot + 1) & parser->section_mask;
        result = parser->section_slots[slot];
    }
    return result;
}

static void asm_section_directive(AsmParser* parser, uint32_t token, uint32_t end)
{
    const uint32_t location = parser->tokens[token].start;
    const bool named = asm_is(parser, token, ".section") || asm_is(parser, token, ".pushsection");
    const uint32_t name_token = named ? parser->at++ : token;
    const String8 name = asm_name_text(parser, name_token);
    uint32_t group = 0;
    uint32_t id = asm_find_section(parser, name, group);
    ObjectAtomSection object = id != 0 ? parser->sections[id].object : (ObjectAtomSection){.name = name, .type = SHT_PROGBITS};
    if (id == 0)
    {
        // GNU's conventional families have useful defaults even before an explicit flags argument.
        object.flags = asm_section_prefix(name, ".text.") ? SHF_ALLOC | SHF_EXECINSTR :
                       asm_section_prefix(name, ".rodata.") ? SHF_ALLOC :
                       asm_section_prefix(name, ".data.") || asm_section_prefix(name, ".bss.") ? SHF_ALLOC | SHF_WRITE : 0;
        object.type = asm_section_prefix(name, ".bss.") ? SHT_NOBITS : SHT_PROGBITS;
    }
    const bool attributes = named && asm_take(parser, ',');
    if (attributes)
    {
        if (parser->at >= end || parser->tokens[parser->at].kind != BUSTER_ASM_STRING)
        {
            asm_fail(parser, location, "expected section flags string");
        }
        else
        {
            const String8 flags = asm_name_text(parser, parser->at++);
            object.flags = 0;
            for (uint64_t i = 0; i < flags.length; i += 1)
            {
                switch (flags.pointer[i])
                {
                case 'a': object.flags |= SHF_ALLOC; break;
                case 'w': object.flags |= SHF_WRITE; break;
                case 'x': object.flags |= SHF_EXECINSTR; break;
                case 'M': object.flags |= SHF_MERGE; break;
                case 'S': object.flags |= SHF_STRINGS; break;
                case 'T': object.flags |= SHF_TLS; break;
                case 'G': object.flags |= SHF_GROUP; break;
                default: asm_fail(parser, location, "unsupported section flag"); break;
                }
            }
            // A grouped section is independent of an ungrouped namesake. Preserve the
            // built-in entry size for ordinary sections such as .init_array.
            if ((object.flags & SHF_GROUP) != 0)
            {
                object.entry_size = 0;
            }
            if (asm_take(parser, ','))
            {
                if (!asm_take(parser, '@') && !asm_take(parser, '%'))
                {
                    asm_fail(parser, location, "expected section type");
                }
                if (asm_is(parser, parser->at, "progbits"))
                {
                    object.type = SHT_PROGBITS;
                }
                else if (asm_is(parser, parser->at, "nobits"))
                {
                    object.type = SHT_NOBITS;
                }
                else
                {
                    asm_fail(parser, location, "unsupported section type");
                }
                parser->at += 1;
            }
            if ((object.flags & SHF_MERGE) != 0)
            {
                asm_expect(parser, ',', "mergeable section requires an entry size");
                const __int128 size = asm_absolute(parser, end);
                if (size <= 0 || size > UINT32_MAX)
                {
                    asm_fail(parser, location, "section entry size must be positive and fit 32 bits");
                }
                object.entry_size = (uint32_t)size;
            }
            if ((object.flags & SHF_GROUP) != 0)
            {
                asm_expect(parser, ',', "grouped section requires a signature");
                const uint32_t signature_token = parser->at;
                const String8 signature_name = parser->at < end ? asm_name_text(parser, parser->at) : (String8){0};
                const bool named_signature = parser->at < end && asm_name_token(parser, parser->at);
                if (!named_signature || signature_name.length == 0)
                {
                    asm_fail(parser, location, "expected section group signature");
                }
                else
                {
                    parser->at += 1;
                    uint32_t flags = 0;
                    if (asm_take(parser, ','))
                    {
                        if (!asm_is(parser, parser->at, "comdat"))
                        {
                            asm_fail(parser, location, "unsupported section group linkage");
                        }
                        flags = BUSTER_LINK_GROUP_COMDAT;
                        parser->at += 1;
                    }
                    if (parser->result.error == 0)
                    {
                        const uint32_t signature = asm_symbol_scoped(parser, signature_name, parser->tokens[signature_token].start, false);
                        AsmSymbol* symbol = &parser->symbols[signature];
                        if (symbol->group != 0 && symbol->group_flags != flags)
                        {
                            asm_fail(parser, location, "section group linkage changed after declaration");
                        }
                        else
                        {
                            if (symbol->group == 0)
                            {
                                symbol->group = ++parser->group_count;
                                symbol->group_flags = flags;
                            }
                            group = symbol->group;
                        }
                    }
                }
            }
        }
    }
    // A name alone selects the ungrouped section. Grouped instances, even conventional names such as .text, have
    // independent metadata and layout; never inherit a different group's attributes.
    id = asm_find_section(parser, name, group);
    const bool executable = (object.flags & SHF_EXECINSTR) != 0;
    const bool zero = object.type == SHT_NOBITS;
    const bool tls = (object.flags & SHF_TLS) != 0;
    // Merged strings take characters of 1, 2 or 4 bytes: GCC's wide literals are `.rodata.str4.4,"aMS",@progbits,4`
    // (#1115).
    const bool characters = object.entry_size == 1 || object.entry_size == 2 || object.entry_size == 4;
    if (name.length == 0 || (zero && (executable || (object.flags & SHF_MERGE) != 0)) ||
        ((object.flags & SHF_STRINGS) != 0 && ((object.flags & SHF_MERGE) == 0 || !characters)))
    {
        asm_fail(parser, location, "unsupported section attributes");
    }
    if (id != 0 && attributes && (parser->sections[id].declared || id < BUSTER_LINK_CLASS_COUNT))
    {
        const ObjectAtomSection* old = &parser->sections[id].object;
        if (old->type != object.type || old->flags != object.flags || old->entry_size != object.entry_size)
        {
            asm_fail(parser, location, "section attributes changed after declaration");
        }
    }
    if (id == 0 && parser->section_count >= parser->section_capacity)
    {
        asm_fail(parser, location, "section capacity exhausted");
    }
    if (parser->result.error == 0)
    {
        const bool added = id == 0;
        if (id == 0)
        {
            id = parser->section_count++;
            parser->sections[id] = (AsmSection){0};
        }
        AsmSection* section = &parser->sections[id];
        section->object = object;
        section->group = group;
        // The Link's string pools are of bytes: wider characters' strings are read-only data, as the importer classes
        // them, whose section keeps its merge flags and entry size (the object writer's descriptor).
        section->class = zero ? (tls ? BUSTER_LINK_CLASS_TBSS : BUSTER_LINK_CLASS_BSS) :
                         executable ? BUSTER_LINK_CLASS_TEXT : tls ? BUSTER_LINK_CLASS_TDATA :
                         (object.flags & SHF_WRITE) != 0 ? BUSTER_LINK_CLASS_DATA :
                         (object.flags & SHF_STRINGS) != 0 && object.entry_size == 1 ? BUSTER_LINK_CLASS_STRINGS : BUSTER_LINK_CLASS_RODATA;
        section->declared = true;
        if (added)
        {
            asm_bind_forward_section(parser, name, id);
            asm_section_index(parser, id);
        }
        parser->previous_section = parser->section;
        parser->section = id;
        asm_fragment(parser, ASM_LABEL, location);
    }
}

// Whether the object lists an undefined symbol though no relocation may refer to it (#1115), as GNU as lists every one
// the source names (`.globl x`, `.size x, 4`, `.hidden x`, an alias's target, `_GLOBAL_OFFSET_TABLE_`), but a weak
// one nothing uses (`.weak x` alone): one a relocation refers to is listed first, and once.
static bool asm_object_declares(const AsmSymbol* symbol)
{
    const bool weak = (symbol->flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    // A section's name (`.long .debug_abbrev`, as clang's DWARF writes it) is the section's own symbol, not undefined.
    return symbol->link != 0 && !symbol->defined && !symbol->section_start && !symbol->absolute && !symbol->alias && symbol->common == 0 && !symbol->internal &&
           (!weak || symbol->used);
}

static void asm_object_groups(AsmParser* parser)
{
    Link* link = &parser->result.link;
    if (parser->group_count != 0 && parser->result.error == 0 && link->error == LINK_ERROR_NONE)
    {
        // A section used only as a signature still needs an atom, including an untouched default section.
        for (uint32_t i = 1; i < parser->symbol_count && parser->result.error == 0; i += 1)
        {
            const AsmSymbol* symbol = &parser->symbols[i];
            if (symbol->group != 0)
            {
                const uint32_t section = parser->section_name_slots != 0 ? asm_find_latest_section(parser, symbol->name) : asm_find_section(parser, symbol->name, 0);
                if (section != 0 && parser->sections[section].atom == 0)
                {
                    AsmSection* target = &parser->sections[section];
                    target->atom = link_add_atom(link, target->class, target->alignment, 0, 0, 0);
                }
            }
        }
        const uint32_t count = parser->group_count + 1;
        LinkGroup* groups = BUSTER_ARENA_PUSH_ARRAY(parser->arena, LinkGroup, count);
        uint32_t* atoms = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, link->atoms.count);
        if (groups == 0 || atoms == 0)
        {
            asm_fail(parser, 0, "arena exhausted publishing section groups");
        }
        else
        {
            memset(groups, 0, count * sizeof(LinkGroup));
            memset(atoms, 0, link->atoms.count * sizeof(uint32_t));
            for (uint32_t i = 1; i < parser->symbol_count; i += 1)
            {
                const AsmSymbol* symbol = &parser->symbols[i];
                if (symbol->group != 0)
                {
                    if (symbol->absolute || symbol->alias || symbol->view != 0 || symbol->common != 0)
                    {
                        asm_fail(parser, symbol->location, "unsupported section group signature definition");
                    }
                    const uint32_t section = parser->section_name_slots != 0 ? asm_find_latest_section(parser, symbol->name) : asm_find_section(parser, symbol->name, 0);
                    const bool section_signature = section != 0;
                    if (section_signature && (parser->sections[section].name_ambiguous || symbol->explicit_binding || symbol->has_size || symbol->flags != 0))
                    {
                        asm_fail(parser, symbol->location, "ambiguous or attributed section group signature");
                    }
                    const NameId name = symbol->link != 0 ? link->symbols.name[symbol->link] :
                                        link_add_name(link, (const uint8_t*)symbol->name.pointer, symbol->name.length);
                    groups[symbol->group] = (LinkGroup){
                        .name = name,
                        .signature = section_signature ? 0 : symbol->link,
                        .signature_atom = section_signature ? parser->sections[section].atom : 0,
                        .flags = symbol->group_flags,
                    };
                }
            }
            for (uint32_t i = 1; i < parser->section_count; i += 1)
            {
                const AsmSection* section = &parser->sections[i];
                if (section->atom != 0)
                {
                    atoms[section->atom] = section->group;
                }
            }
            link->groups = (LinkGroups){.data = groups, .atom_groups = atoms, .count = count, .atom_count = link->atoms.count};
            if (link->error != LINK_ERROR_NONE)
            {
                asm_fail(parser, 0, "arena exhausted publishing group signatures");
            }
        }
    }
}

static void asm_object_options(AsmParser* parser)
{
    asm_object_groups(parser);
    Link* link = &parser->result.link;
    ObjectAtomSection* sections = BUSTER_ARENA_PUSH_ARRAY(parser->arena, ObjectAtomSection, link->atoms.count);
    uint8_t* temporary = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint8_t, link->symbols.count);
    if (sections == 0 || temporary == 0)
    {
        asm_fail(parser, 0, "arena exhausted publishing object metadata");
    }
    else
    {
        memset(sections, 0, link->atoms.count * sizeof(ObjectAtomSection));
        memset(temporary, 0, link->symbols.count);
        for (uint32_t i = 1; i < parser->section_count; i += 1)
        {
            if (parser->sections[i].atom != 0)
            {
                sections[parser->sections[i].atom] = parser->sections[i].object;
            }
        }
        for (uint32_t i = 1; i < parser->symbol_count; i += 1)
        {
            const AsmSymbol* symbol = &parser->symbols[i];
            if (symbol->link != 0 && (symbol->flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL)
            {
                // A section's name too: its references are the section symbol's, as GNU as writes no other (#1115).
                temporary[symbol->link] = symbol->internal || symbol->section_start || (symbol->name.length >= 2 && memcmp(symbol->name.pointer, ".L", 2) == 0);
            }
        }
        parser->result.object_options = (ObjectOptions){
            .atom_sections = sections,
            .atom_section_count = link->atoms.count,
            .temporary_symbols = temporary,
            .temporary_symbol_count = link->symbols.count,
            .undefined_types = true,
        };
    }
    // The undefined symbols GNU as lists without a relocation (#1115), every one the source names (asm_object_declares),
    // as the _GLOBAL_OFFSET_TABLE_ a GOT or TLS modifier declares. (A global .comm is a common symbol of its own atom,
    // which the object writer lists as SHN_COMMON's.)
    uint32_t declared_count = 0;
    for (uint32_t i = 1; i < parser->symbol_count; i += 1)
    {
        declared_count += asm_object_declares(&parser->symbols[i]);
    }
    if (parser->result.error == 0 && declared_count != 0)
    {
        SymbolId* declared = BUSTER_ARENA_PUSH_ARRAY(parser->arena, SymbolId, declared_count);
        if (declared == 0)
        {
            asm_fail(parser, 0, "arena exhausted publishing object metadata");
        }
        else
        {
            uint32_t count = 0;
            for (uint32_t i = 1; i < parser->symbol_count; i += 1)
            {
                if (asm_object_declares(&parser->symbols[i]))
                {
                    declared[count++] = parser->symbols[i].link;
                }
            }
            parser->result.object_options.declared_symbols = declared;
            parser->result.object_options.declared_symbol_count = count;
        }
    }
    // The object's st_size of each symbol whose size reaches past its section's end; the others keep the Link's.
    if (parser->result.error == 0 && parser->beyond_count != 0)
    {
        uint64_t* sizes = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint64_t, link->symbols.count);
        if (sizes == 0)
        {
            asm_fail(parser, 0, "arena exhausted publishing object metadata");
        }
        else
        {
            memcpy(sizes, link->symbols.size, link->symbols.count * sizeof(uint64_t));
            for (uint32_t i = 1; i < parser->symbol_count; i += 1)
            {
                const AsmSymbol* symbol = &parser->symbols[i];
                if (symbol->beyond != 0 && symbol->link != 0)
                {
                    sizes[symbol->link] = symbol->beyond;
                }
            }
            parser->result.object_options.symbol_sizes = sizes;
            parser->result.object_options.symbol_size_count = link->symbols.count;
        }
    }
}

// Stable section ranges are shared by layout and emission. Reserve one extra order entry for post-layout CFI.
// Fragment identities remain source identities; grouping never changes the order within a section.
static void asm_partition_fragments(AsmParser* parser)
{
    parser->fragment_order = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->fragment_count + 1);
    parser->fragment_begin = BUSTER_ARENA_PUSH_ARRAY(parser->arena, uint32_t, parser->section_count + 1);
    Scratch scratch = scratch_begin(parser->arena);
    uint32_t* cursors = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, parser->section_count);
    if (parser->fragment_order == 0 || parser->fragment_begin == 0 || cursors == 0)
    {
        asm_fail(parser, 0, "arena exhausted partitioning section fragments");
    }
    else
    {
        uint32_t* begin = parser->fragment_begin;
        memset(begin, 0, (parser->section_count + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < parser->fragment_count; i += 1)
        {
            begin[parser->fragments[i].section + 1] += 1;
        }
        for (uint32_t i = 0; i < parser->section_count; i += 1)
        {
            begin[i + 1] += begin[i];
        }
        memcpy(cursors, begin, parser->section_count * sizeof(uint32_t));
        for (uint32_t i = 0; i < parser->fragment_count; i += 1)
        {
            parser->fragment_order[cursors[parser->fragments[i].section]++] = i;
        }
    }
    scratch_end(scratch);
}

static void asm_partition_append(AsmParser* parser, uint32_t id)
{
    if (parser->result.error == 0)
    {
        const uint32_t section = parser->fragments[id].section;
        const uint32_t position = parser->fragment_begin[section + 1];
        // This is the single CFI fragment appended after layout; the original order allocation has its spare slot.
        memmove(parser->fragment_order + position + 1, parser->fragment_order + position, (id - position) * sizeof(uint32_t));
        parser->fragment_order[position] = id;
        for (uint32_t i = section + 1; i <= parser->section_count; i += 1)
        {
            parser->fragment_begin[i] += 1;
        }
    }
}

// Whether a section holds bytes, as GNU as's seg_not_empty_p sees them: an instruction or data, not only labels, fill
// or alignment.
static bool asm_section_holds(const AsmParser* parser, uint32_t section)
{
    bool result = false;
    for (uint32_t i = 0; section != 0 && !result && i < parser->fragment_count; i += 1)
    {
        const AsmFragment* fragment = &parser->fragments[i];
        result = fragment->section == section &&
                 (fragment->kind == ASM_INSTRUCTION || fragment->kind == ASM_ULEB || fragment->kind == ASM_SLEB ||
                  ((fragment->kind == ASM_VALUE || fragment->kind == ASM_BYTES) && fragment->size != 0));
    }
    return result;
}

// A debug section the assembler writes, as GNU as makes it when the source has none: the line table's .debug_line, and
// CFI's .debug_frame (#1115), of no flags, and DWARF 5's .debug_line_str of merged strings.
static uint32_t asm_debug_section(AsmParser* parser, const char* name, bool strings)
{
    const String8 spelling = { .pointer = (char*)name, .length = strlen(name) };
    uint32_t id = asm_find_section(parser, spelling, 0);
    if (id == 0 && parser->section_count >= parser->section_capacity)
    {
        asm_fail(parser, 0, "section capacity exhausted");
    }
    else if (id == 0)
    {
        id = parser->section_count++;
        parser->sections[id] = (AsmSection){
            .object = {
                .name = spelling,
                .type = SHT_PROGBITS,
                .flags = strings ? SHF_MERGE | SHF_STRINGS : 0,
                .entry_size = strings ? 1 : 0,
            },
            .class = strings ? BUSTER_LINK_CLASS_STRINGS : BUSTER_LINK_CLASS_RODATA,
            .declared = true,
        };
        asm_section_index(parser, id);
    }
    return id;
}
