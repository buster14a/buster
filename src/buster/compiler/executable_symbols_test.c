// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Optional regular-symbol output: independent ELF expectations, definition addresses rather than IFUNC stubs,
// TLS module offsets, local ordering, exact/vector/lane identity and refusal before changing an existing output.
// Included by executable_test.c after its image, fixture and file helpers.

static SymbolId executable_symbols_test_add(Link* link, const char* name, AtomId atom, uint64_t value, uint64_t size, uint8_t flags, uint8_t source)
{
    const NameId id = link_add_name(link, (const uint8_t*)name, (uint32_t)strlen(name));
    return id != 0 ? link_add_symbol(link, id, atom, value, size, flags, source) : 0;
}

static bool executable_symbols_test_ids(ExecutableTestTally* tally)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* memory = mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0;
    executable_test_expect(tally, mapped, "guarded symbol ID input", 0);
    for (uint32_t count = 0; mapped && count <= 33; count += 1)
    {
        SymbolId* ids = (SymbolId*)(memory + page) - count;
        for (uint32_t changed = 0; changed < (count == 0 ? 1 : 4); changed += 1)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                ids[i] = i + 1;
            }
            if (changed != 0)
            {
                ids[count / 2] = changed == 1 ? 0 : changed == 2 ? count + 2 : count / 2;
            }
            const ExecutableSymbols selected = {
                .ids = ids,
                .count = count,
            };
            for (uint32_t begin = 0; begin <= count; begin += 1)
            {
                bool expected = true;
                for (uint32_t i = begin; i < count; i += 1)
                {
                    expected &= ids[i] != 0 && ids[i] < count + 1 && (i == 0 || ids[i] > ids[i - 1]);
                }
                const Range64 range = {
                    .begin = begin,
                    .end = count,
                };
                executable_test_expect(tally, executable_symbol_ids_valid(&selected, count + 1, range, true) == expected,
                                       "guarded vector IDs match independent scalar ordering", count * 1000 + changed * 100 + begin);
            }
        }
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, 2 * page);
    }
    return mapped;
}

static bool executable_symbols_test_table(String8 file, const ExecutableTestImage* image, const ExecutableResult* result)
{
    const Elf64_Shdr* table = image->valid ? executable_test_section(image, ".symtab") : 0;
    const Elf64_Shdr* strings = image->valid ? executable_test_section(image, ".strtab") : 0;
    const Elf64_Shdr* text = image->valid ? executable_test_section(image, ".text") : 0;
    const Elf64_Shdr* data = image->valid ? executable_test_section(image, ".data") : 0;
    const Elf64_Shdr* tdata = image->valid ? executable_test_section(image, ".tdata") : 0;
    const Elf64_Shdr* tbss = image->valid ? executable_test_section(image, ".tbss") : 0;
    const Elf64_Shdr* stubs = image->valid ? executable_test_section(image, ".iplt") : 0;
    bool valid = table != 0 && strings != 0 && text != 0 && data != 0 && tdata != 0 && tbss != 0 && stubs != 0;
    uint64_t base = UINT64_MAX;
    for (uint32_t i = 0; valid && i < image->header->e_phnum; i += 1)
    {
        const Elf64_Phdr* segment = &image->program_headers[i];
        if (segment->p_type == PT_LOAD)
        {
            const uint64_t mapped = segment->p_vaddr - segment->p_offset;
            base = mapped < base ? mapped : base;
            valid &= segment->p_offset + segment->p_filesz <= table->sh_offset && segment->p_offset + segment->p_filesz <= strings->sh_offset;
        }
    }
    if (valid)
    {
        valid = table->sh_type == SHT_SYMTAB && table->sh_size == 10 * sizeof(Elf64_Sym) && table->sh_entsize == sizeof(Elf64_Sym) &&
                table->sh_info == 2 && table->sh_link == (uint64_t)(strings - image->section_headers) && table->sh_flags == 0 && table->sh_addr == 0 &&
                strings->sh_type == SHT_STRTAB && strings->sh_flags == 0 && strings->sh_addr == 0 &&
                table->sh_offset <= file.length && table->sh_size <= file.length - table->sh_offset &&
                strings->sh_offset <= file.length && strings->sh_size <= file.length - strings->sh_offset;
    }
    const char* names[] = { "", "local_data", "_start", "tls_data", "tls_zero", "absolute", "weak_missing", "_end", "resolver", "protected_data" };
    const uint64_t sizes[] = { 0, 8, 9, 2, 4, 0, 0, 0, 3, 8 };
    const uint8_t info[] = { 0, STT_OBJECT, ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), ELF64_ST_INFO(STB_GLOBAL, STT_TLS),
                             ELF64_ST_INFO(STB_WEAK, STT_TLS), ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), ELF64_ST_INFO(STB_WEAK, STT_FUNC),
                             ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), ELF64_ST_INFO(STB_GLOBAL, STT_GNU_IFUNC), ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT) };
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, file.pointer + table->sh_offset + i * sizeof(symbol), sizeof(symbol));
        const uint64_t length = strlen(names[i]);
        valid = symbol.st_name < strings->sh_size && length < strings->sh_size - symbol.st_name &&
                memcmp(file.pointer + strings->sh_offset + symbol.st_name, names[i], length + 1) == 0 &&
                symbol.st_size == sizes[i] && symbol.st_info == info[i] && symbol.st_other == (i == 1 ? STV_HIDDEN : i == 9 ? STV_PROTECTED : STV_DEFAULT);
        if (valid && (i == 2 || i == 8))
        {
            valid = symbol.st_shndx == (uint64_t)(text - image->section_headers) && symbol.st_value == text->sh_addr + (i == 8 ? 16 : 0);
            valid &= i != 8 || symbol.st_value != stubs->sh_addr;
        }
        else if (valid && (i == 1 || i == 9))
        {
            valid = symbol.st_shndx == (uint64_t)(data - image->section_headers) && symbol.st_value == data->sh_addr + (i == 9 ? 8 : 0);
        }
        else if (valid && (i == 3 || i == 4))
        {
            const Elf64_Shdr* section = i == 3 ? tdata : tbss;
            valid = symbol.st_shndx == (uint64_t)(section - image->section_headers) &&
                    symbol.st_value == section->sh_addr - tdata->sh_addr + (i == 3 ? 2 : 4);
        }
        else if (valid && i == 5)
        {
            valid = symbol.st_shndx == SHN_ABS && symbol.st_value == 42;
        }
        else if (valid && (i == 0 || i == 6))
        {
            valid = symbol.st_shndx == SHN_UNDEF && symbol.st_value == 0;
        }
        else if (valid && i == 7)
        {
            valid = symbol.st_shndx != SHN_UNDEF && symbol.st_shndx < image->header->e_shnum && symbol.st_value == base + result->image_size &&
                    (image->section_headers[symbol.st_shndx].sh_flags & (SHF_ALLOC | SHF_TLS)) == SHF_ALLOC;
        }
    }
    return valid;
}

static bool executable_test_symbols(const ExecutableTestDirectory* directory)
{
    ExecutableTestTally tally = { .check = "regular symbol output" };
    ExecutableTestLink test = { 0 };
    bool valid = executable_symbols_test_ids(&tally) && executable_test_link_begin(&test, 4096);
    Link* link = &test.link;
    uint8_t code[32] = { 0x31, 0xff, 0xb8, 60, 0, 0, 0, 0x0f, 0x05 };
    code[16] = 0x31;
    code[17] = 0xc0;
    code[18] = 0xc3;
    uint8_t data_bytes[16] = { 0 };
    uint8_t tls_bytes[8] = { 0 };
    const AtomId text = valid ? link_add_atom(link, BUSTER_LINK_CLASS_TEXT, 4, sizeof(code), code, 0) : 0;
    const AtomId data = valid ? link_add_atom(link, BUSTER_LINK_CLASS_DATA, 3, sizeof(data_bytes), data_bytes, 0) : 0;
    const AtomId tdata = valid ? link_add_atom(link, BUSTER_LINK_CLASS_TDATA, 3, sizeof(tls_bytes), tls_bytes, 0) : 0;
    const AtomId tbss = valid ? link_add_atom(link, BUSTER_LINK_CLASS_TBSS, 3, 16, 0, 0) : 0;
    SymbolId ids[9] = { 0 };
    valid &= text != 0 && data != 0 && tdata != 0 && tbss != 0;
    if (valid)
    {
        ids[0] = executable_symbols_test_add(link, "_start", text, 0, 9, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM);
        ids[1] = executable_symbols_test_add(link, "local_data", data, 0, 8, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_HIDDEN, BUSTER_LINK_SOURCE_PROGRAM);
        ids[2] = executable_symbols_test_add(link, "tls_data", tdata, 2, 2, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_TLS, BUSTER_LINK_SOURCE_PROGRAM);
        ids[3] = executable_symbols_test_add(link, "tls_zero", tbss, 4, 4, BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_TLS, BUSTER_LINK_SOURCE_PROGRAM);
        ids[4] = executable_symbols_test_add(link, "absolute", 0, 42, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_ABSOLUTE);
        ids[5] = executable_symbols_test_add(link, "weak_missing", 0, 0, 0, BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_NONE);
        ids[6] = executable_symbols_test_add(link, "_end", 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_LINKER);
        ids[7] = executable_symbols_test_add(link, "resolver", text, 16, 3, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_IFUNC, BUSTER_LINK_SOURCE_PROGRAM);
        ids[8] = executable_symbols_test_add(link, "protected_data", data, 8, 8, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_PROTECTED, BUSTER_LINK_SOURCE_PROGRAM);
        valid = ids[8] != 0 && link_add_fixup(link, data, 8, BUSTER_LINK_FIXUP_ABS64, ids[7], 0) != 0 && link_check(link) == LINK_ERROR_NONE;
    }
    char path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/symbols", directory->path);
    const ExecutableRuntime runtime = { 0 };
    const ExecutableSymbols selected = { .ids = ids, .count = BUSTER_ARRAY_LENGTH(ids) };
    uint64_t boundaries[BUSTER_EXECUTABLE_TEST_MAX_BOUNDARIES];
    ExecutableTestWrite write = {
        .link = link,
        .options = {
            .path = { .pointer = path, .length = strlen(path) },
            .entry = ids[0],
            .runtime = &runtime,
            .symbols = &selected,
            .boundaries = boundaries,
            .boundary_capacity = BUSTER_ARRAY_LENGTH(boundaries),
        },
    };
    Arena saved = arena_reserve(8ull << 20);
    const uint8_t* original = valid && saved.base != 0 ? executable_test_save(link, &saved) : 0;
    String8 reference = { 0 };
    valid &= original != 0;
    executable_test_expect(&tally, valid, "construct independent fixture", 0);
    for (uint32_t exact = 0; valid && exact < 2; exact += 1)
    {
        for (uint32_t l = 0; valid && l < BUSTER_ARRAY_LENGTH(executable_test_lane_counts); l += 1)
        {
            executable_test_restore(link, original);
            const uint32_t lanes = executable_test_lane_counts[l];
            write.options.exact = exact != 0;
            write.succeeded = false;
            valid = executable_test_reset(link, path) && lane_run(lanes, executable_test_lane_main, &write, 0) && write.succeeded;
            const String8 file = valid ? object_test_read_file(&saved, path) : (String8){ 0 };
            const ExecutableTestImage image = executable_test_image(file);
            executable_test_expect(&tally, valid && executable_symbols_test_table(file, &image, &write.result), "ELF names, sizes, types and final definitions", 10 * exact + lanes);
            const bool same = file.pointer != 0 && (reference.pointer == 0 || (file.length == reference.length && memcmp(file.pointer, reference.pointer, file.length) == 0));
            executable_test_expect(&tally, same, "exact/vector/lane byte identity", 10 * exact + lanes);
            executable_test_expect(&tally, valid && executable_test_runs(path, (const uint8_t*)"", 0), "native program executes", 10 * exact + lanes);
            executable_test_expect(&tally, valid && executable_test_build_id(&saved, file, boundaries, write.result.chunks), "symbol bytes participate in build ID", 10 * exact + lanes);
            reference = reference.pointer == 0 ? file : reference;
        }
    }
    // A duplicate, a zero ID and an absent array must refuse before opening an already valid output.
    SymbolId duplicate[] = { ids[0], ids[0] };
    SymbolId zero[] = { 0 };
    const ExecutableSymbols invalid[] = { { .ids = duplicate, .count = 2 }, { .ids = zero, .count = 1 }, { .count = 1 } };
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        write.options.symbols = &invalid[i];
        write.succeeded = false;
        const bool ran = lane_run(3, executable_test_lane_main, &write, 0);
        const String8 file = object_test_read_file(&saved, path);
        executable_test_expect(&tally, ran && !write.succeeded && write.result.error == EXECUTABLE_ERROR_UNSUPPORTED && file.length == reference.length &&
                               file.pointer != 0 && memcmp(file.pointer, reference.pointer, file.length) == 0, "invalid descriptor preserves destination", i);
    }
    // Zero-sized TLS definitions are valid Link input. If their output section would be absent, refuse the optional
    // attachment rather than pairing a TLS-relative value with a non-TLS section index.
    for (uint32_t i = 0; valid && i < 2; i += 1)
    {
        const AtomId atom = i == 0 ? tdata : tbss;
        const SymbolId symbol = ids[2 + i];
        const uint64_t old_length = link->atoms.size[atom];
        const uint64_t old_value = link->symbols.value[symbol];
        const uint64_t old_size = link->symbols.size[symbol];
        link->atoms.size[atom] = 0;
        link->symbols.value[symbol] = 0;
        link->symbols.size[symbol] = 0;
        write.options.symbols = &selected;
        write.succeeded = false;
        const bool checked = link_check(link) == LINK_ERROR_NONE;
        const bool ran = checked && lane_run(3, executable_test_lane_main, &write, 0);
        const String8 file = object_test_read_file(&saved, path);
        executable_test_expect(&tally, ran && !write.succeeded && write.result.error == EXECUTABLE_ERROR_UNSUPPORTED && file.length == reference.length &&
                               file.pointer != 0 && memcmp(file.pointer, reference.pointer, file.length) == 0, "empty TLS section refusal preserves destination", i);
        link->atoms.size[atom] = old_length;
        link->symbols.value[symbol] = old_value;
        link->symbols.size[symbol] = old_size;
    }
    if (!directory->keep)
    {
        unlink(path);
    }
    arena_release(&saved);
    executable_test_link_end(&test);
    return executable_test_report(&tally);
}
