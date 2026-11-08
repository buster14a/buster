// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original ELF oracle for tier1_table_loop's four-entry relative jump table. Include after
// check_test_tier1_function. Resolve S + A - P independently of the emitter, accepting section symbols too.
// Adding the table base leaves S + A - slot_offset, not the raw relocation addend. The optional negative control
// redirects slot 1 to the LEA in owned memory, so omitting that nonzero slot offset cannot pass the control.

static bool check_test_tier1_table_targets(const char* path, const char* name, uint32_t address, uint32_t address_field, uint32_t* targets, bool reentry_control)
{
    const uint32_t entries = 4;
    const uint64_t table_bytes = (uint64_t)entries * sizeof(uint32_t);
    uint32_t resolved[4] = { 0 };
    uint64_t length = 0;
    uint8_t* bytes = path != NULL && name != NULL && targets != NULL ? check_test_driver_read_aligned(path, &length) : NULL;
    Elf64_Ehdr header = { 0 };
    bool valid = bytes != NULL && length >= sizeof(header);
    if (valid)
    {
        memcpy(&header, bytes, sizeof(header));
        valid = header.e_ident[EI_CLASS] == ELFCLASS64 && header.e_ident[EI_DATA] == ELFDATA2LSB &&
                header.e_type == ET_REL && header.e_machine == EM_X86_64 && header.e_version == EV_CURRENT &&
                header.e_shoff % _Alignof(Elf64_Shdr) == 0;
    }
    CheckTestElf elf = { 0 };
    valid = valid && check_test_elf_open(&elf, (String8){ .pointer = (char*)bytes, .length = length });
    uint32_t symbol_table = 0;
    uint32_t symbol_tables = 0;
    for (uint32_t s = 0; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* section = elf.sections + s;
        if (section->sh_type == SHT_SYMTAB)
        {
            symbol_table = s;
            symbol_tables += 1;
            valid = section->sh_offset % _Alignof(Elf64_Sym) == 0 && section->sh_size % sizeof(Elf64_Sym) == 0 &&
                    section->sh_link < elf.section_count && elf.sections[section->sh_link].sh_type == SHT_STRTAB;
        }
    }
    valid = valid && symbol_tables == 1;
    const Elf64_Sym* function = NULL;
    uint64_t functions = 0;
    for (uint64_t i = 1; valid && i < elf.symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = elf.symbols + i;
        const char* spelling = check_test_elf_string(elf.symbol_names, elf.symbol_names_size, symbol->st_name);
        if (spelling != NULL && strcmp(spelling, name) == 0 && ELF64_ST_TYPE(symbol->st_info) == STT_FUNC)
        {
            function = symbol;
            functions += 1;
        }
    }
    valid = valid && functions == 1 && function->st_shndx != SHN_UNDEF && function->st_shndx < SHN_LORESERVE && function->st_shndx < elf.section_count;
    const Elf64_Shdr* text = valid ? elf.sections + function->st_shndx : NULL;
    valid = valid && text->sh_type == SHT_PROGBITS && (text->sh_flags & SHF_EXECINSTR) != 0 &&
            check_test_elf_range(elf.file, text->sh_offset, text->sh_size) && function->st_value <= text->sh_size &&
            function->st_size != 0 && function->st_size <= text->sh_size - function->st_value && function->st_size <= UINT32_MAX &&
            address < address_field && address_field <= function->st_size && sizeof(uint32_t) <= function->st_size - address_field;
    Elf64_Rela reference = { 0 };
    uint64_t references = 0;
    for (uint32_t s = 0; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* section = elf.sections + s;
        if (section->sh_type == SHT_RELA && section->sh_info == function->st_shndx)
        {
            valid = section->sh_link == symbol_table && section->sh_entsize == sizeof(Elf64_Rela) &&
                    section->sh_size % sizeof(Elf64_Rela) == 0 && check_test_elf_range(elf.file, section->sh_offset, section->sh_size);
            for (uint64_t r = 0; valid && r < section->sh_size / sizeof(Elf64_Rela); r += 1)
            {
                Elf64_Rela relocation;
                memcpy(&relocation, bytes + section->sh_offset + r * sizeof(relocation), sizeof(relocation));
                if (relocation.r_offset >= function->st_value && relocation.r_offset - function->st_value < function->st_size)
                {
                    reference = relocation;
                    references += 1;
                }
            }
        }
    }
    valid = valid && references == 1 && reference.r_offset - function->st_value == address_field &&
            ELF64_R_TYPE(reference.r_info) == R_X86_64_PC32 && ELF64_R_SYM(reference.r_info) < elf.symbol_count;
    const Elf64_Sym* table = valid ? elf.symbols + ELF64_R_SYM(reference.r_info) : NULL;
    valid = valid && table->st_shndx != SHN_UNDEF && table->st_shndx < SHN_LORESERVE && table->st_shndx < elf.section_count;
    const Elf64_Shdr* data = valid ? elf.sections + table->st_shndx : NULL;
    // The LEA's displacement is its final four bytes, so its PC32 reference has the usual -4 addend correction.
    const __int128 table_position = valid ? (__int128)table->st_value + reference.r_addend + sizeof(uint32_t) : 0;
    valid = valid && data->sh_type == SHT_PROGBITS && (data->sh_flags & (SHF_EXECINSTR | SHF_WRITE)) == 0 && table->st_value <= data->sh_size &&
            check_test_elf_range(elf.file, data->sh_offset, data->sh_size) && table_position >= 0 && table_position <= data->sh_size &&
            table_bytes <= (__int128)data->sh_size - table_position;
    const uint64_t table_offset = valid ? (uint64_t)table_position : 0;
    uint32_t seen = 0;
    for (uint32_t s = 0; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* section = elf.sections + s;
        if (section->sh_type == SHT_RELA && section->sh_info == table->st_shndx)
        {
            valid = section->sh_link == symbol_table && section->sh_entsize == sizeof(Elf64_Rela) &&
                    section->sh_size % sizeof(Elf64_Rela) == 0 && check_test_elf_range(elf.file, section->sh_offset, section->sh_size);
            for (uint64_t r = 0; valid && r < section->sh_size / sizeof(Elf64_Rela); r += 1)
            {
                Elf64_Rela relocation;
                uint8_t* stored = bytes + section->sh_offset + r * sizeof(relocation);
                memcpy(&relocation, stored, sizeof(relocation));
                if (relocation.r_offset >= table_offset && relocation.r_offset - table_offset < table_bytes)
                {
                    const uint64_t slot_offset = relocation.r_offset - table_offset;
                    const uint32_t slot = (uint32_t)(slot_offset / sizeof(uint32_t));
                    valid = slot_offset % sizeof(uint32_t) == 0 && (seen & (1u << slot)) == 0 &&
                            ELF64_R_TYPE(relocation.r_info) == R_X86_64_PC32 && ELF64_R_SYM(relocation.r_info) < elf.symbol_count;
                    const Elf64_Sym* label = valid ? elf.symbols + ELF64_R_SYM(relocation.r_info) : NULL;
                    valid = valid && label->st_shndx == function->st_shndx && label->st_value <= text->sh_size;
                    if (valid && reentry_control && slot == 1)
                    {
                        const __int128 addend = (__int128)function->st_value + address + slot_offset - label->st_value;
                        valid = addend >= INT64_MIN && addend <= INT64_MAX;
                        if (valid)
                        {
                            relocation.r_addend = (int64_t)addend;
                            memcpy(stored, &relocation, sizeof(relocation));
                        }
                    }
                    const __int128 destination = valid ? (__int128)label->st_value + relocation.r_addend - slot_offset : 0;
                    valid = valid && destination >= function->st_value && destination - function->st_value < function->st_size;
                    if (valid)
                    {
                        resolved[slot] = (uint32_t)(destination - function->st_value);
                        seen |= 1u << slot;
                    }
                }
            }
        }
    }
    valid = valid && seen == (1u << entries) - 1;
    if (valid)
    {
        memcpy(targets, resolved, sizeof(resolved));
    }
    free(bytes);
    return valid;
}
