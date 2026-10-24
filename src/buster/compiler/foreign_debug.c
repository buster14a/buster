// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Foreign objects' DWARF (#395): the debug sections of each selected object, which the importer leaves out of the link
// (they aren't allocated), concatenated by name in input order into the sections the writer writes outside the loaded
// image (ExecutableDebugSections), each contribution at its alignment after the one before, as GNU ld and lld
// concatenate them without merging .debug_str's strings. Their relocations resolve here where the layout doesn't decide
// them, and are left to the writer where it does:
//   - a reference into a debug section (DW_FORM_sec_offset's and DW_FORM_strp's R_X86_64_32, R_X86_64_64 in 64-bit
//     DWARF) against a debug section's symbol, or a local in one, is that section's place in its output section, plus
//     the symbol's value and the addend: written now;
//   - one to code or data the link keeps (a low_pc's or a line program's R_X86_64_64, an ABS32 address, a thread-local's
//     R_X86_64_DTPOFF32 or _64) is the writer's, by the link's symbol (ExecutableDebugRelocation);
//   - one to a section that COMDAT selection discarded, or to a symbol the link has none of, gets lld's tombstone: 0,
//     or 1 in .debug_ranges and .debug_loc, whose lists 0 would end; one without a symbol is its addend.
// A debug section of a discarded group goes with its group. A compressed one (SHF_COMPRESSED, GCC's and clang's -gz) is
// refused, and so are relocation types DWARF doesn't use. The writer's patch and this pass make the bytes lld writes
// for the same inputs, but where lld merges .debug_str's and .debug_line_str's strings: their offsets differ, and the
// sections are larger.
//
// Two passes over the objects in input order: each debug section's place in its output section, then its bytes and
// relocations, which lane 0 takes on the lane path too: a program's debug sections are a few contributions per
// object, copied once. foreign_link.c includes this file.

// The debug section kind of a section's name (its ExecutableDebugSections index), or BUSTER_EXECUTABLE_DEBUG_KINDS for
// another name.
static uint32_t foreign_debug_kind(const ImportElf* elf, const Elf64_Shdr* header)
{
    uint32_t result = BUSTER_EXECUTABLE_DEBUG_KINDS;
    const bool named = header->sh_name < elf->section_names.length;
    const char* name = named ? elf->section_names.pointer + header->sh_name : "";
    const uint64_t room = named ? elf->section_names.length - header->sh_name : 0;
    for (uint32_t kind = 0; named && kind < BUSTER_EXECUTABLE_DEBUG_KINDS; kind += 1)
    {
        const char* wanted = executable_section_names[EXECUTABLE_SECTION_DEBUG_INFO + kind];
        const uint64_t length = strlen(wanted);
        result = length < room && memcmp(name, wanted, length + 1) == 0 ? kind : result;
    }
    return result;
}

// Whether section `section` of a selected object is one of its debug sections the output keeps: not allocated, of
// contents, of a known name, and not of a discarded group; with its kind.
static bool foreign_debug_section(const ForeignObject* input, uint32_t section, uint32_t* kind)
{
    const Elf64_Shdr header = import_section_header(&input->elf, section);
    const bool candidate = (header.sh_flags & SHF_ALLOC) == 0 && header.sh_type == SHT_PROGBITS && (input->discarded == 0 || input->discarded[section] == 0);
    *kind = candidate ? foreign_debug_kind(&input->elf, &header) : BUSTER_EXECUTABLE_DEBUG_KINDS;
    return *kind < BUSTER_EXECUTABLE_DEBUG_KINDS;
}

// A relocation's ExecutableDebugRelocation kind and its field's width, or false for a type debug sections don't take.
static bool foreign_debug_type(uint32_t type, uint8_t* kind, uint32_t* width)
{
    const bool known = type == R_X86_64_64 || type == R_X86_64_32 || type == R_X86_64_DTPOFF32 || type == R_X86_64_DTPOFF64;
    *kind = type == R_X86_64_64 ? EXECUTABLE_DEBUG_ABS64 : type == R_X86_64_32 ? EXECUTABLE_DEBUG_ABS32 : type == R_X86_64_DTPOFF32 ? EXECUTABLE_DEBUG_DTPOFF32 :
            EXECUTABLE_DEBUG_DTPOFF64;
    *width = type == R_X86_64_64 || type == R_X86_64_DTPOFF64 ? 8 : 4;
    return known;
}

// The relocations of object `id`'s debug section `section`, whose bytes are at `bytes` in output kind `kind`'s section
// at `base`: each written now or recorded in `relocations` (from *count), as the file's comment says. `bases` holds
// each of the object's sections' place in its kind's output section (UINT64_MAX for one that isn't a kept debug
// section). Returns the first failure.
static ForeignLinkResult foreign_debug_relocate(const ForeignLink* output, const ForeignObject* input, uint32_t id, uint32_t relocation_section, uint32_t kind,
                                                uint64_t base, uint8_t* bytes, uint64_t size, const uint64_t* bases, ExecutableDebugRelocation* relocations, uint64_t* count)
{
    ForeignLinkResult result = { 0 };
    const Elf64_Shdr header = import_section_header(&input->elf, relocation_section);
    const String8 entries = import_section_bytes(&input->elf, &header);
    const uint32_t section_kind = EXECUTABLE_SECTION_DEBUG_INFO + kind;
    const uint64_t tombstone = section_kind == EXECUTABLE_SECTION_DEBUG_RANGES || section_kind == EXECUTABLE_SECTION_DEBUG_LOC ? 1 : 0;
    const ImportObject* object = &output->objects[id];
    for (uint64_t i = 0; result.error == FOREIGN_LINK_ERROR_NONE && i < entries.length / sizeof(Elf64_Rela); i += 1)
    {
        Elf64_Rela relocation;
        memcpy(&relocation, entries.pointer + i * sizeof(Elf64_Rela), sizeof(relocation));
        const uint32_t type = ELF64_R_TYPE(relocation.r_info);
        const uint32_t index = (uint32_t)ELF64_R_SYM(relocation.r_info);
        uint8_t relocation_kind = 0;
        uint32_t width = 0;
        const bool known = type == R_X86_64_NONE || foreign_debug_type(type, &relocation_kind, &width);
        const bool inside = type == R_X86_64_NONE || (relocation.r_offset <= size && width <= size - relocation.r_offset);
        const bool symbolized = index < input->symbols.count;
        Elf64_Sym symbol = { 0 };
        uint32_t target_section = SHN_UNDEF;
        bool indexed = symbolized;
        if (symbolized && index != 0)
        {
            symbol = import_symbol(&input->symbols, index);
            indexed = import_symbol_section(&input->symbols, index, &symbol, &target_section);
        }
        if (!known || !inside || !indexed)
        {
            result.error = !known ? FOREIGN_LINK_ERROR_UNSUPPORTED : FOREIGN_LINK_ERROR_IMPORT;
            result.object = id;
            result.import = import_fail(!known ? IMPORT_ERROR_UNSUPPORTED : IMPORT_ERROR_BOUNDS, header.sh_offset + i * sizeof(Elf64_Rela));
            result.import.section = relocation_section;
            result.import.relocation = (uint32_t)i;
            result.import.type = type;
        }
        else if (type != R_X86_64_NONE)
        {
            const bool module_offset = relocation_kind == EXECUTABLE_DEBUG_DTPOFF32 || relocation_kind == EXECUTABLE_DEBUG_DTPOFF64;
            const bool sectioned = index != 0 && target_section < input->elf.section_count && bases[target_section] != UINT64_MAX;
            const SymbolId target = index != 0 && index < object->symbol_count ? object->symbols[index] : 0;
            uint64_t value = tombstone;
            bool written = true;
            if (index == 0)
            {
                value = (uint64_t)relocation.r_addend;
            }
            else if (sectioned && !module_offset)
            {
                // A place in a debug section: its section's place in its output section, the symbol's and the addend.
                value = bases[target_section] + symbol.st_value + (uint64_t)relocation.r_addend;
            }
            else if (target != 0)
            {
                relocations[*count] = (ExecutableDebugRelocation){
                    .offset = base + relocation.r_offset,
                    .addend = relocation.r_addend,
                    .target = target,
                    .section = (uint8_t)kind,
                    .kind = relocation_kind,
                };
                *count += 1;
                written = false;
            }
            const bool fits = !written || width == 8 || value <= UINT32_MAX;
            if (written && fits)
            {
                memcpy(bytes + relocation.r_offset, &value, width);
            }
            if (!fits || (sectioned && module_offset))
            {
                result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
                result.object = id;
                result.import = import_fail(IMPORT_ERROR_UNSUPPORTED, header.sh_offset + i * sizeof(Elf64_Rela));
                result.import.section = relocation_section;
                result.import.relocation = (uint32_t)i;
                result.import.type = type;
            }
        }
    }
    return result;
}

// The selected objects' debug sections into output->debug (see the file's comment), in the order of `order` (input
// order), from `arena`. Returns the first failure.
static ForeignLinkResult foreign_debug_build(ForeignLink* output, const Foreign* foreign, const uint32_t* order, Arena* arena)
{
    ForeignLinkResult result = { 0 };
    output->debug = (ExecutableDebugSections){ 0 };
    uint64_t sizes[BUSTER_EXECUTABLE_DEBUG_KINDS] = { 0 };
    uint64_t relocation_capacity = 0;
    // Each object's sections' places, for the objects with debug sections: counted in the first pass, which also sizes
    // the output sections and the relocations' room.
    uint64_t** bases = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t*, foreign->object_count);
    result.error = bases != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
    for (uint32_t at = 0; result.error == FOREIGN_LINK_ERROR_NONE && at < foreign->object_count; at += 1)
    {
        const uint32_t id = order[at];
        const ForeignObject* input = &foreign->objects[id];
        bases[id] = 0;
        for (uint32_t section = 1; id != 0 && input->library == 0 && result.error == FOREIGN_LINK_ERROR_NONE && section < input->elf.section_count; section += 1)
        {
            uint32_t kind = 0;
            if (foreign_debug_section(input, section, &kind))
            {
                const Elf64_Shdr header = import_section_header(&input->elf, section);
                if (bases[id] == 0)
                {
                    bases[id] = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, input->elf.section_count);
                    result.error = bases[id] != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
                    for (uint32_t s = 0; bases[id] != 0 && s < input->elf.section_count; s += 1)
                    {
                        bases[id][s] = UINT64_MAX;
                    }
                }
                const uint64_t alignment = header.sh_addralign > 1 ? header.sh_addralign : 1;
                const bool aligned = (alignment & (alignment - 1)) == 0 && alignment <= 4096;
                if (result.error == FOREIGN_LINK_ERROR_NONE && ((header.sh_flags & SHF_COMPRESSED) != 0 || !aligned))
                {
                    result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
                    result.object = id;
                    result.import = import_fail(IMPORT_ERROR_UNSUPPORTED, input->elf.section_offset + (uint64_t)section * sizeof(Elf64_Shdr));
                    result.import.section = section;
                    result.import.type = header.sh_type;
                }
                else if (result.error == FOREIGN_LINK_ERROR_NONE)
                {
                    const uint64_t place = (sizes[kind] + alignment - 1) & ~(alignment - 1);
                    bases[id][section] = place;
                    sizes[kind] = place + header.sh_size;
                }
            }
        }
        for (uint32_t r = 0; bases[id] != 0 && result.error == FOREIGN_LINK_ERROR_NONE && r < input->relocation_count; r += 1)
        {
            const Elf64_Shdr header = import_section_header(&input->elf, input->relocations[r]);
            relocation_capacity += header.sh_info < input->elf.section_count && bases[id][header.sh_info] != UINT64_MAX ? header.sh_size / sizeof(Elf64_Rela) : 0;
        }
    }
    uint8_t* bytes[BUSTER_EXECUTABLE_DEBUG_KINDS] = { 0 };
    for (uint32_t kind = 0; result.error == FOREIGN_LINK_ERROR_NONE && kind < BUSTER_EXECUTABLE_DEBUG_KINDS; kind += 1)
    {
        // DWARF32's offsets reach the first 4 GB of a section.
        result.error = sizes[kind] >= 0xfffffff0u ? FOREIGN_LINK_ERROR_CAPACITY : FOREIGN_LINK_ERROR_NONE;
        bytes[kind] = result.error == FOREIGN_LINK_ERROR_NONE && sizes[kind] != 0 ? arena_push(arena, sizes[kind], 8) : 0;
        result.error = result.error == FOREIGN_LINK_ERROR_NONE && sizes[kind] != 0 && bytes[kind] == 0 ? FOREIGN_LINK_ERROR_MEMORY : result.error;
        if (bytes[kind] != 0)
        {
            // The padding between contributions.
            memset(bytes[kind], 0, sizes[kind]);
        }
    }
    ExecutableDebugRelocation* relocations = result.error == FOREIGN_LINK_ERROR_NONE && relocation_capacity != 0 ?
                                                 BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableDebugRelocation, relocation_capacity) : 0;
    result.error = result.error == FOREIGN_LINK_ERROR_NONE && relocation_capacity != 0 && relocations == 0 ? FOREIGN_LINK_ERROR_MEMORY : result.error;
    uint64_t count = 0;
    for (uint32_t at = 0; result.error == FOREIGN_LINK_ERROR_NONE && at < foreign->object_count; at += 1)
    {
        const uint32_t id = order[at];
        const ForeignObject* input = &foreign->objects[id];
        for (uint32_t section = 1; bases[id] != 0 && section < input->elf.section_count; section += 1)
        {
            uint32_t kind = 0;
            if (bases[id][section] != UINT64_MAX && foreign_debug_section(input, section, &kind))
            {
                const Elf64_Shdr header = import_section_header(&input->elf, section);
                const String8 contents = import_section_bytes(&input->elf, &header);
                memcpy(bytes[kind] + bases[id][section], contents.pointer, contents.length);
            }
        }
        for (uint32_t r = 0; bases[id] != 0 && result.error == FOREIGN_LINK_ERROR_NONE && r < input->relocation_count; r += 1)
        {
            const Elf64_Shdr header = import_section_header(&input->elf, input->relocations[r]);
            uint32_t kind = 0;
            if (header.sh_info < input->elf.section_count && bases[id][header.sh_info] != UINT64_MAX && foreign_debug_section(input, header.sh_info, &kind))
            {
                const Elf64_Shdr target = import_section_header(&input->elf, header.sh_info);
                const uint64_t base = bases[id][header.sh_info];
                result = foreign_debug_relocate(output, input, id, input->relocations[r], kind, base, bytes[kind] + base, target.sh_size, bases[id], relocations, &count);
            }
        }
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        for (uint32_t kind = 0; kind < BUSTER_EXECUTABLE_DEBUG_KINDS; kind += 1)
        {
            output->debug.sections[kind] = (String8){ .pointer = (char*)bytes[kind], .length = sizes[kind] };
        }
        output->debug.relocations = relocations;
        output->debug.relocation_count = count;
    }
    return result;
}
