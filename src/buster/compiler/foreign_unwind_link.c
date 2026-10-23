// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The foreign producer's COMDAT unwind handoff. Included by foreign_link.c after its section/symbol import helpers.
// Only objects with discarded COMDAT sections need filtering. Original Foreign bytes and headers stay unchanged;
// compact bytes and records live in the output arena. The ordinary importer consumes those records directly.
// foreign_check_tables already guarantees at most one RELA table per target section.
// Symbol import first validates original extents; this adapter then maps retained EH symbols into compact bytes.
// References with nonzero addends to a changed EH fragment are currently refused: their expression's architecture
// bias must be known before mapping an interior address. No stale byte offset is silently retained.

struct ForeignLinkUnwind
{
    ForeignLinkUnwind* next;
    uint32_t section;
    ForeignUnwind plan;
};

static ForeignLinkUnwind* foreign_link_unwind(const ForeignLink* output, uint32_t object, uint32_t section)
{
    ForeignLinkUnwind* found = output->unwind != 0 ? output->unwind[object] : 0;
    while (found != 0 && found->section != section)
    {
        found = found->next;
    }
    return found;
}

static ForeignLinkResult foreign_link_prepare_unwind(ForeignLink* output, const Foreign* foreign, Arena* arena)
{
    ForeignLinkResult result = { 0 };
    for (uint32_t id = 1; result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        const ForeignObject* input = &foreign->objects[id];
        ImportObject* object = &output->objects[id];
        if (input->library == 0 && input->discarded != 0)
        {
            for (uint32_t r = 0; result.error == FOREIGN_LINK_ERROR_NONE && r < input->relocation_count; r += 1)
            {
                const uint32_t section = input->relocations[r];
                const Elf64_Shdr header = import_section_header(&input->elf, section);
                const AtomId atom = object->section_atoms[header.sh_info];
                if (atom != 0 && output->link.atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME)
                {
                    if (output->unwind == 0)
                    {
                        output->unwind = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignLinkUnwind*, foreign->object_count);
                        if (output->unwind != 0)
                        {
                            memset(output->unwind, 0, (uint64_t)foreign->object_count * sizeof(ForeignLinkUnwind*));
                        }
                    }
                    ForeignLinkUnwind* frame = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignLinkUnwind, 1);
                    const String8 relocations = import_section_bytes(&input->elf, &header);
                    if (output->unwind == 0 || frame == 0)
                    {
                        result.error = FOREIGN_LINK_ERROR_MEMORY;
                    }
                    else
                    {
                        *frame = (ForeignLinkUnwind){ .section = header.sh_info };
                        const ForeignUnwindInput source = {
                            .bytes = {
                                .pointer = (char*)output->link.atoms.bytes[atom],
                                .length = output->link.atoms.size[atom],
                            },
                            .relocations = (const Elf64_Rela*)relocations.pointer,
                            .relocation_count = (uint32_t)(relocations.length / sizeof(Elf64_Rela)),
                            .symbols = &input->symbols,
                            .sections = object->section_atoms,
                            .section_count = object->section_count,
                        };
                        const ForeignUnwindResult pruned = foreign_unwind_prune(&source, foreign->path, arena, &frame->plan);
                        if (pruned.error != FOREIGN_UNWIND_ERROR_NONE)
                        {
                            result.error = pruned.error == FOREIGN_UNWIND_ERROR_MEMORY ? FOREIGN_LINK_ERROR_MEMORY : FOREIGN_LINK_ERROR_UNSUPPORTED;
                            result.import.section = pruned.relocation != 0 ? section : header.sh_info;
                            result.import.relocation = pruned.relocation != 0 ? pruned.relocation - 1 : 0;
                            result.import.offset = pruned.offset;
                        }
                        else
                        {
                            frame->next = output->unwind[id];
                            output->unwind[id] = frame;
                        }
                    }
                    result.object = result.error != FOREIGN_LINK_ERROR_NONE ? id : 0;
                }
            }
        }
    }
    return result;
}

static String8 foreign_link_relocation_bytes(const ForeignLink* output, const ForeignObject* input, uint32_t id, const Elf64_Shdr* header)
{
    const ForeignLinkUnwind* frame = foreign_link_unwind(output, id, header->sh_info);
    const String8 result = frame != 0 ? (String8){
        .pointer = (char*)frame->plan.relocations,
        .length = (uint64_t)frame->plan.relocation_count * sizeof(Elf64_Rela),
    } : import_section_bytes(&input->elf, header);
    return result;
}

static ForeignLinkResult foreign_link_finish_unwind(ForeignLink* output, const Foreign* foreign)
{
    ForeignLinkResult result = { 0 };
    Link* link = &output->link;
    for (uint32_t id = 1; output->unwind != 0 && result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        const ForeignObject* input = &foreign->objects[id];
        ImportObject* object = &output->objects[id];
        for (ForeignLinkUnwind* frame = output->unwind[id]; result.error == FOREIGN_LINK_ERROR_NONE && frame != 0; frame = frame->next)
        {
            const AtomId atom = object->section_atoms[frame->section];
            for (uint32_t index = 1; result.error == FOREIGN_LINK_ERROR_NONE && index < object->symbol_count; index += 1)
            {
                const SymbolId target = object->symbols[index];
                if (target != 0 && link->symbols.atom[target] == atom)
                {
                    const Elf64_Sym symbol = import_symbol(&input->symbols, index);
                    uint32_t section = SHN_UNDEF;
                    const bool indexed = import_symbol_section(&input->symbols, index, &symbol, &section);
                    if (indexed && section == frame->section)
                    {
                        const bool whole_section = ELF64_ST_TYPE(symbol.st_info) == STT_SECTION;
                        uint64_t value = 0;
                        uint64_t end = 0;
                        uint64_t original_end = 0;
                        const bool fits = !__builtin_add_overflow(symbol.st_value, symbol.st_size, &original_end);
                        const bool mapped = whole_section || (fits && foreign_unwind_offset(&frame->plan, symbol.st_value, &value) &&
                                                             foreign_unwind_offset(&frame->plan, original_end - (symbol.st_size != 0 ? 1 : 0), &end) && end >= value);
                        if (mapped)
                        {
                            link->symbols.value[target] = value;
                            link->symbols.size[target] = whole_section ? 0 : end - value + (symbol.st_size != 0 ? 1 : 0);
                        }
                        else if (ELF64_ST_BIND(symbol.st_info) == STB_LOCAL)
                        {
                            // Retained assembler labels can name discarded FDEs. Remove the input mapping so an
                            // actual surviving reference is refused; leave an inert slot to preserve all other IDs.
                            object->symbols[index] = 0;
                            link->symbols.name[target] = 0;
                            link->symbols.atom[target] = 0;
                            link->symbols.value[target] = 0;
                            link->symbols.size[target] = 0;
                            link->symbols.flags[target] = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_NOTYPE;
                            link->symbols.source[target] = BUSTER_LINK_SOURCE_ABSOLUTE;
                        }
                        else
                        {
                            result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
                            result.import.symbol = index;
                            result.import.section = frame->section;
                        }
                    }
                }
            }
            link->atoms.bytes[atom] = (uint8_t*)frame->plan.bytes.pointer;
            link->atoms.size[atom] = frame->plan.bytes.length;
            result.object = result.error != FOREIGN_LINK_ERROR_NONE ? id : 0;
        }
    }
    return result;
}

static ImportResult foreign_link_unwind_references(const ForeignLink* output, const Foreign* foreign, uint32_t id, uint32_t section, String8 entries)
{
    ImportResult result = { 0 };
    const Link* link = &output->link;
    const ImportObject* object = &output->objects[id];
    for (uint64_t r = 0; output->unwind != 0 && result.error == IMPORT_ERROR_NONE && r < entries.length / sizeof(Elf64_Rela); r += 1)
    {
        Elf64_Rela relocation;
        memcpy(&relocation, entries.pointer + r * sizeof(Elf64_Rela), sizeof(relocation));
        const uint64_t index = ELF64_R_SYM(relocation.r_info);
        const SymbolId target = index < object->symbol_count ? object->symbols[index] : 0;
        const AtomId atom = link->symbols.atom[target];
        const uint32_t owner = link->atoms.unit[atom];
        bool changed = false;
        for (const ForeignLinkUnwind* frame = owner < foreign->object_count ? output->unwind[owner] : 0; frame != 0 && !changed; frame = frame->next)
        {
            changed = output->objects[owner].section_atoms[frame->section] == atom && frame->plan.bytes.length != frame->plan.old_length;
        }
        if (changed && relocation.r_addend != 0 && ELF64_R_TYPE(relocation.r_info) != R_X86_64_NONE)
        {
            result = import_fail(IMPORT_ERROR_UNSUPPORTED, r * sizeof(Elf64_Rela));
            result.section = section;
            result.relocation = (uint32_t)r;
            result.symbol = (uint32_t)index;
            result.type = (uint32_t)ELF64_R_TYPE(relocation.r_info);
        }
    }
    return result;
}
