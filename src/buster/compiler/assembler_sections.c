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
                uint32_t slot = link_hash((const uint8_t*)name.pointer, name.length) & (count - 1);
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
        uint32_t slot = link_hash((const uint8_t*)name.pointer, name.length) & parser->section_mask;
        while (parser->section_slots[slot] != 0)
        {
            slot = (slot + 1) & parser->section_mask;
        }
        parser->section_slots[slot] = id;
    }
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

static uint32_t asm_find_section(const AsmParser* parser, String8 name)
{
    uint32_t slot = link_hash((const uint8_t*)name.pointer, name.length) & parser->section_mask;
    uint32_t result = parser->section_slots[slot];
    while (result != 0)
    {
        const String8 candidate = parser->sections[result].object.name;
        if (candidate.length == name.length && memcmp(candidate.pointer, name.pointer, name.length) == 0)
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
    const bool named = asm_is(parser, token, ".section");
    const uint32_t name_token = named ? parser->at++ : token;
    const String8 name = asm_name_text(parser, name_token);
    uint32_t id = asm_find_section(parser, name);
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
                default: asm_fail(parser, location, "unsupported section flag"); break;
                }
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
        }
    }
    const bool executable = (object.flags & SHF_EXECINSTR) != 0;
    const bool zero = object.type == SHT_NOBITS;
    const bool tls = (object.flags & SHF_TLS) != 0;
    if (name.length == 0 || (zero && (executable || (object.flags & SHF_MERGE) != 0)) ||
        ((object.flags & SHF_STRINGS) != 0 && ((object.flags & SHF_MERGE) == 0 || object.entry_size != 1)))
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
        section->class = zero ? (tls ? BUSTER_LINK_CLASS_TBSS : BUSTER_LINK_CLASS_BSS) :
                         executable ? BUSTER_LINK_CLASS_TEXT : tls ? BUSTER_LINK_CLASS_TDATA :
                         (object.flags & SHF_WRITE) != 0 ? BUSTER_LINK_CLASS_DATA :
                         (object.flags & SHF_STRINGS) != 0 ? BUSTER_LINK_CLASS_STRINGS : BUSTER_LINK_CLASS_RODATA;
        section->declared = true;
        if (added)
        {
            asm_section_index(parser, id);
        }
        parser->section = id;
        asm_fragment(parser, ASM_LABEL, location);
    }
}

static void asm_object_options(AsmParser* parser)
{
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
                temporary[symbol->link] = symbol->internal || (symbol->name.length >= 2 && memcmp(symbol->name.pointer, ".L", 2) == 0);
            }
        }
        parser->result.object_options = (ObjectOptions){
            .atom_sections = sections,
            .atom_section_count = link->atoms.count,
            .temporary_symbols = temporary,
            .temporary_symbol_count = link->symbols.count,
        };
    }
}
