// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The native-writer half of test-debug --cross-check. Include after executable_test.c. GNU as supplies the fixture's
// code and CFI; this imports them, attaches the caller's debug bytes through ExecutableOptions.debug, and leaves the
// native executable for the same GDB/perf consumer checks. Every lane/exact write must reproduce the complete file
// over poisoned output. The source debug sections remain untouched, including when a descriptor is refused.

static SymbolId debug_native_symbol(const Link* link, String8 name)
{
    SymbolId result = 0;
    for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
    {
        const NameId id = link->symbols.name[symbol];
        if (id != 0 && link->names.length[id] == name.length && memcmp(link->names.bytes[id], name.pointer, name.length) == 0)
        {
            result = symbol;
        }
    }
    return result;
}

static bool debug_native_write(Link* link, SymbolId entry, String8 path, const ExecutableDebug* debug, uint32_t lanes, bool exact, ExecutableResult* result)
{
    ExecutableTestWrite write = {
        .link = link,
        .options = {
            .path = path,
            .entry = entry,
            .debug = debug,
            .exact = exact,
        },
    };
    const bool ran = lane_run(lanes, executable_test_lane_main, &write, 0);
    *result = write.result;
    return ran && write.succeeded;
}

// An explicit symbol selection owns only .symtab/.strtab. Empty and singleton selections must leave all five
// already finalized DWARF sections unchanged even though their address-table function set is larger.
static bool debug_native_symbol_override(Link* link, SymbolId entry, String8 path, const ExecutableDebug* debug, const uint8_t* atoms,
                                         String8 reference, Arena* arena, ExecutableTestTally* tally)
{
    static const char* const names[] = { ".debug_info", ".debug_abbrev", ".debug_line", ".debug_addr", ".debug_rnglists" };
    const ExecutableTestImage original = executable_test_image(reference);
    const SymbolId one = debug->functions[0];
    bool valid = original.valid;
    for (uint32_t count = 0; valid && count <= 1; count += 1)
    {
        const ExecutableSymbols symbols = {
            .ids = count != 0 ? &one : 0,
            .count = count,
        };
        String8 first = { 0 };
        for (uint32_t exact = 0; valid && exact < 2; exact += 1)
        {
            for (uint32_t l = 0; valid && l < BUSTER_ARRAY_LENGTH(executable_test_lane_counts); l += 1)
            {
                const uint32_t lanes = executable_test_lane_counts[l];
                ExecutableTestWrite write = {
                    .link = link,
                    .options = {
                        .path = path,
                        .entry = entry,
                        .debug = debug,
                        .symbols = &symbols,
                        .exact = exact != 0,
                    },
                };
                const bool poisoned = executable_test_reset(link, path.pointer);
                executable_test_restore(link, atoms);
                const bool succeeded = poisoned && lane_run(lanes, executable_test_lane_main, &write, 0) && write.succeeded;
                const String8 file = succeeded ? executable_test_read(arena, path.pointer) : (String8){ 0 };
                const ExecutableTestImage image = executable_test_image(file);
                bool unchanged = image.valid;
                for (uint32_t section = 0; unchanged && section < BUSTER_ARRAY_LENGTH(names); section += 1)
                {
                    const Elf64_Shdr* before = executable_test_section(&original, names[section]);
                    const Elf64_Shdr* after = executable_test_section(&image, names[section]);
                    unchanged = before != 0 && after != 0 && before->sh_size == after->sh_size &&
                                before->sh_offset <= reference.length && before->sh_size <= reference.length - before->sh_offset &&
                                after->sh_offset <= file.length && after->sh_size <= file.length - after->sh_offset &&
                                memcmp(reference.pointer + before->sh_offset, file.pointer + after->sh_offset, after->sh_size) == 0;
                }
                const Elf64_Shdr* table = image.valid ? executable_test_section(&image, ".symtab") : 0;
                const Elf64_Shdr* strings = image.valid ? executable_test_section(&image, ".strtab") : 0;
                const NameId name = link->symbols.name[one];
                const uint64_t length = link->names.length[name];
                bool selected = table != 0 && strings != 0 && table->sh_size == (1ull + count) * sizeof(Elf64_Sym) &&
                                strings->sh_size == (count != 0 ? 2 + length : 1) && table->sh_offset <= file.length &&
                                table->sh_size <= file.length - table->sh_offset && strings->sh_offset <= file.length &&
                                strings->sh_size <= file.length - strings->sh_offset;
                if (selected && count != 0)
                {
                    Elf64_Sym symbol;
                    memcpy(&symbol, file.pointer + table->sh_offset + sizeof(symbol), sizeof(symbol));
                    selected = symbol.st_name == 1 && symbol.st_size == link->symbols.size[one] &&
                               symbol.st_value == BUSTER_EXECUTABLE_BASE + link->atoms.address[link->symbols.atom[one]] + link->symbols.value[one] &&
                               memcmp(file.pointer + strings->sh_offset + 1, link->names.bytes[name], length) == 0 &&
                               file.pointer[strings->sh_offset + 1 + length] == 0;
                }
                first = first.pointer == 0 ? file : first;
                const bool same = file.pointer != 0 && first.pointer != 0 && file.length == first.length && memcmp(file.pointer, first.pointer, file.length) == 0;
                valid = succeeded && unchanged && selected && same;
                executable_test_expect(tally, valid, "explicit empty/single symbol table preserves all DWARF slots", 100 * count + 10 * exact + lanes);
            }
        }
    }
    const bool reset = valid && executable_test_reset(link, path.pointer);
    executable_test_restore(link, atoms);
    ExecutableResult written = { 0 };
    valid = reset && debug_native_write(link, entry, path, debug, 3, false, &written) && executable_test_same_file(arena, path.pointer, reference);
    executable_test_expect(tally, valid, "restore debug function selection for external consumers", 0);
    return valid;
}

static bool debug_native_unchanged(const DebugUnit* unit, const DebugOutput* output, const String8* saved, const uint32_t* positions)
{
    bool result = true;
    for (uint32_t section = 0; section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        result = result && output->sections[section].size == saved[section].length &&
                 memcmp(output->sections[section].bytes, saved[section].pointer, saved[section].length) == 0;
    }
    result = result && memcmp(output->line_addresses, positions, (uint64_t)unit->function_count * sizeof(uint32_t)) == 0;
    return result;
}

static bool debug_native_symbols(const Link* link, const DebugUnit* unit, const SymbolId* functions, const ExecutableTestImage* image, String8 file,
                                 const Elf64_Shdr* text, const uint64_t* addresses)
{
    const Elf64_Shdr* symbols = executable_test_section(image, ".symtab");
    const Elf64_Shdr* strings = executable_test_section(image, ".strtab");
    bool result = symbols != 0 && strings != 0 && symbols->sh_type == SHT_SYMTAB && strings->sh_type == SHT_STRTAB && symbols->sh_flags == 0 && strings->sh_flags == 0 &&
                  symbols->sh_addr == 0 && strings->sh_addr == 0 &&
                  symbols->sh_entsize == sizeof(Elf64_Sym) && symbols->sh_size % sizeof(Elf64_Sym) == 0 && symbols->sh_offset <= file.length &&
                  symbols->sh_size <= file.length - symbols->sh_offset && strings->sh_offset <= file.length && strings->sh_size <= file.length - strings->sh_offset &&
                  symbols->sh_link == (uint64_t)(strings - image->section_headers);
    for (uint32_t segment = 0; result && segment < image->header->e_phnum; segment += 1)
    {
        const Elf64_Phdr* program = &image->program_headers[segment];
        if (program->p_type == PT_LOAD)
        {
            result = program->p_offset <= symbols->sh_offset && program->p_filesz <= symbols->sh_offset - program->p_offset &&
                     program->p_offset <= strings->sh_offset && program->p_filesz <= strings->sh_offset - program->p_offset;
        }
    }
    uint32_t locals = 0;
    for (uint32_t function = 0; function < unit->function_count; function += 1)
    {
        locals += (link->symbols.flags[functions[function]] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
    }
    result = result && symbols->sh_size == ((uint64_t)unit->function_count + 1) * sizeof(Elf64_Sym) && symbols->sh_info == locals + 1;
    for (uint64_t index = 0; result && index < symbols->sh_size / sizeof(Elf64_Sym); index += 1)
    {
        Elf64_Sym symbol;
        const Elf64_Sym empty = { 0 };
        memcpy(&symbol, file.pointer + symbols->sh_offset + index * sizeof(symbol), sizeof(symbol));
        result = index == 0 ? memcmp(&symbol, &empty, sizeof(symbol)) == 0 :
                             ((index < symbols->sh_info) == (ELF64_ST_BIND(symbol.st_info) == STB_LOCAL));
    }
    for (uint32_t function = 0; result && function < unit->function_count; function += 1)
    {
        uint32_t found = 0;
        for (uint64_t index = 1; result && index < symbols->sh_size / sizeof(Elf64_Sym); index += 1)
        {
            Elf64_Sym symbol;
            memcpy(&symbol, file.pointer + symbols->sh_offset + index * sizeof(symbol), sizeof(symbol));
            result = symbol.st_name < strings->sh_size;
            const char* name = result ? file.pointer + strings->sh_offset + symbol.st_name : 0;
            const char* end = result ? memchr(name, 0, strings->sh_size - symbol.st_name) : 0;
            result = result && end != 0;
            const String8 wanted = unit->functions[function].name;
            if (result && (uint64_t)(end - name) == wanted.length && memcmp(name, wanted.pointer, wanted.length) == 0)
            {
                found += 1;
                const uint8_t binding = (link->symbols.flags[functions[function]] & BUSTER_LINK_SYMBOL_BINDING) >> 4;
                result = ELF64_ST_TYPE(symbol.st_info) == STT_FUNC && ELF64_ST_BIND(symbol.st_info) == binding &&
                         symbol.st_shndx == (uint64_t)(text - image->section_headers) && symbol.st_value == addresses[function] && symbol.st_size == unit->functions[function].size;
            }
        }
        result = result && found == 1;
    }
    return result;
}

// The fixture has exactly one TEXT atom. Its ELF .text address and the imported symbol offsets independently give
// each function's final address; do not take expected addresses from the debug section being checked.
static bool debug_native_image(const Link* link, const DebugUnit* unit, const SymbolId* functions, const DebugOutput* source, String8 file,
                               const ExecutableResult* written, Arena* arena)
{
    static const char* const names[] = { ".debug_info", ".debug_abbrev", ".debug_line", ".debug_addr", ".debug_rnglists" };
    const ExecutableTestImage image = executable_test_image(file);
    const Elf64_Shdr* text = executable_test_section(&image, ".text");
    bool result = image.valid && text != 0;
    AtomId text_atom = 0;
    uint32_t text_count = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        if (link->atoms.class[atom] == BUSTER_LINK_CLASS_TEXT && link->atoms.size[atom] != 0)
        {
            text_atom = atom;
            text_count += 1;
        }
    }
    result = result && text_count == 1 && text->sh_addr == BUSTER_EXECUTABLE_BASE + link->atoms.address[text_atom];
    uint64_t* addresses = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, unit->function_count);
    result = result && addresses != 0;
    for (uint32_t function = 0; result && function < unit->function_count; function += 1)
    {
        const SymbolId symbol = functions[function];
        result = link->symbols.atom[symbol] == text_atom && link->symbols.size[symbol] == unit->functions[function].size;
        addresses[function] = text->sh_addr + link->symbols.value[symbol];
    }
    DebugOutput expected = {
        .line_addresses = source->line_addresses,
        .address_capacity = source->address_capacity,
    };
    DebugOutput actual = expected;
    for (uint32_t section = 0; result && section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        const Elf64_Shdr* header = executable_test_section(&image, names[section]);
        const uint64_t size = source->sections[section].size;
        result = header != 0 && header->sh_type == SHT_PROGBITS && header->sh_flags == 0 && header->sh_addr == 0 && header->sh_size == size &&
                 header->sh_offset <= file.length && size <= file.length - header->sh_offset;
        for (uint32_t segment = 0; result && segment < image.header->e_phnum; segment += 1)
        {
            const Elf64_Phdr* program = &image.program_headers[segment];
            if (program->p_type == PT_LOAD)
            {
                result = program->p_vaddr >= BUSTER_EXECUTABLE_BASE && program->p_vaddr - BUSTER_EXECUTABLE_BASE == program->p_offset &&
                         program->p_offset <= header->sh_offset && program->p_filesz <= header->sh_offset - program->p_offset;
            }
        }
        uint8_t* bytes = result ? arena_push(arena, size, 8) : 0;
        result = result && bytes != 0;
        if (result)
        {
            memcpy(bytes, source->sections[section].bytes, size);
            expected.sections[section] = (DebugSection){
                .bytes = bytes,
                .capacity = size,
                .size = size,
            };
            actual.sections[section] = (DebugSection){
                .bytes = (uint8_t*)file.pointer + header->sh_offset,
                .capacity = size,
                .size = size,
            };
        }
    }
    result = result && debug_finalize(unit, &expected, addresses) == DEBUG_OK;
    for (uint32_t section = 0; result && section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        result = memcmp(expected.sections[section].bytes, actual.sections[section].bytes, expected.sections[section].size) == 0;
    }
    result = result && debug_test_decode(unit, &actual, addresses);
    result = result && debug_native_symbols(link, unit, functions, &image, file, text, addresses) && executable_test_load_gaps(file, BUSTER_EXECUTABLE_BASE);
    result = result && executable_test_unwind(link, file, BUSTER_EXECUTABLE_BASE, 1, written) == 0;
    return result;
}

static bool debug_test_native_write(const char* object_path, const char* executable_path, const DebugUnit* unit, const DebugOutput* output)
{
    _Static_assert(BUSTER_EXECUTABLE_DEBUG_SECTIONS == BUSTER_DEBUG_SECTIONS, "the writer and emitter carry the same sections");
    ExecutableTestTally tally = { .check = "debug native executable" };
    Arena arena = arena_reserve(64ull << 20);
    const String8 object = object_test_read_file(&arena, object_path);
    LinkCapacity capacity = { 0 };
    Link link = { 0 };
    ImportObject imported = { 0 };
    bool ready = object.pointer != 0 && unit->function_count != 0 && import_object_capacity(object, &capacity).error == IMPORT_ERROR_NONE;
    ready = ready && link_initialize(&link, &arena, &arena, capacity) &&
            import_object(&link, object, 0, BUSTER_LINK_SOURCE_PROGRAM, &arena, &imported).error == IMPORT_ERROR_NONE && link_check(&link) == LINK_ERROR_NONE;
    SymbolId* functions = BUSTER_ARENA_PUSH_ARRAY(&arena, SymbolId, unit->function_count);
    SymbolId* saved_functions = BUSTER_ARENA_PUSH_ARRAY(&arena, SymbolId, unit->function_count);
    SymbolId* before_functions = BUSTER_ARENA_PUSH_ARRAY(&arena, SymbolId, unit->function_count);
    uint32_t* positions = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, unit->function_count);
    uint32_t* bad_positions = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, unit->function_count);
    uint32_t* before_positions = BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, unit->function_count);
    ready = ready && functions != 0 && saved_functions != 0 && before_functions != 0 && positions != 0 && bad_positions != 0 && before_positions != 0;
    const SymbolId entry = ready ? debug_native_symbol(&link, BUSTER_S8("_start")) : 0;
    ready = ready && entry != 0;
    for (uint32_t function = 0; ready && function < unit->function_count; function += 1)
    {
        functions[function] = debug_native_symbol(&link, unit->functions[function].name);
        saved_functions[function] = functions[function];
        positions[function] = output->line_addresses[function];
        ready = functions[function] != 0;
    }
    const uint8_t* atoms = ready ? executable_test_save(&link, &arena) : 0;
    ready = ready && atoms != 0;
    ExecutableDebug debug = {
        .functions = functions,
        .line_addresses = output->line_addresses,
        .function_count = unit->function_count,
    };
    String8 saved[BUSTER_DEBUG_SECTIONS] = { 0 };
    for (uint32_t section = 0; ready && section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        const uint64_t size = output->sections[section].size;
        char* copy = arena_push(&arena, size, 8);
        ready = copy != 0;
        if (ready)
        {
            memcpy(copy, output->sections[section].bytes, size);
            saved[section] = (String8){
                .pointer = copy,
                .length = size,
            };
            debug.sections[section] = (String8){
                .pointer = (char*)output->sections[section].bytes,
                .length = size,
            };
        }
    }
    // A second, zero-address contribution guarantees that an omitted native patch cannot pass merely because the
    // native layout happened to match the fixed addresses used for the GNU-linked fixture.
    DebugOutput zeroed = *output;
    ExecutableDebug zero_debug = debug;
    String8 zero_saved[BUSTER_DEBUG_SECTIONS] = { 0 };
    uint64_t* zeros = BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, unit->function_count);
    ready = ready && zeros != 0;
    for (uint32_t function = 0; ready && function < unit->function_count; function += 1)
    {
        zeros[function] = 0;
    }
    for (uint32_t section = 0; ready && section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        const uint64_t size = output->sections[section].size;
        uint8_t* copy = arena_push(&arena, size, 8);
        ready = copy != 0;
        if (ready)
        {
            memcpy(copy, output->sections[section].bytes, size);
            zeroed.sections[section] = (DebugSection){
                .bytes = copy,
                .capacity = size,
                .size = size,
            };
            zero_debug.sections[section] = (String8){
                .pointer = (char*)copy,
                .length = size,
            };
        }
    }
    ready = ready && debug_finalize(unit, &zeroed, zeros) == DEBUG_OK;
    for (uint32_t section = 0; ready && section < BUSTER_DEBUG_SECTIONS; section += 1)
    {
        const uint64_t size = zeroed.sections[section].size;
        char* copy = arena_push(&arena, size, 8);
        ready = copy != 0;
        if (ready)
        {
            memcpy(copy, zeroed.sections[section].bytes, size);
            zero_saved[section] = (String8){
                .pointer = copy,
                .length = size,
            };
        }
    }
    executable_test_expect(&tally, ready, "import fixture and retain source sections", 0);
    const String8 path = {
        .pointer = (char*)executable_path,
        .length = strlen(executable_path),
    };
    String8 first = { 0 };
    const uint32_t vector_writes = BUSTER_ARRAY_LENGTH(executable_test_lane_counts);
    for (uint32_t write = 0; ready && write < vector_writes + 2; write += 1)
    {
        const bool exact = write >= vector_writes;
        const uint32_t lanes = exact ? (write == vector_writes ? 1 : 8) : executable_test_lane_counts[write];
        const bool poisoned = executable_test_reset(&link, executable_path);
        executable_test_restore(&link, atoms);
        ExecutableResult written = { 0 };
        const bool succeeded = poisoned && debug_native_write(&link, entry, path, (write & 1) != 0 ? &zero_debug : &debug, lanes, exact, &written);
        if (!succeeded)
        {
            executable_test_print_failure("native debug write", &written);
        }
        const String8 file = succeeded ? executable_test_read(&arena, executable_path) : (String8){ 0 };
        if (write == 0)
        {
            first = file;
        }
        const bool identical = file.pointer != 0 && first.pointer != 0 && file.length == first.length && memcmp(file.pointer, first.pointer, file.length) == 0;
        executable_test_expect(&tally, succeeded && identical, "lane/exact writes reproduce poisoned file", write);
        executable_test_expect(&tally, file.pointer != 0 && debug_native_image(&link, unit, functions, output, file, &written, &arena),
                               "native debug sections and resolved addresses", write);
        executable_test_expect(&tally, debug_native_unchanged(unit, output, saved, positions) && debug_native_unchanged(unit, &zeroed, zero_saved, positions) &&
                               memcmp(functions, saved_functions, (uint64_t)unit->function_count * sizeof(SymbolId)) == 0, "source bytes and address metadata unchanged", write);
        ready = succeeded && identical;
    }
    if (ready && unit->function_count > 1)
    {
        // The descriptor orders a global before this local. The ELF table must still place every local before
        // sh_info, preserve its binding, and leave the address-index order in the debug sections unchanged.
        const SymbolId local = functions[1];
        const uint8_t flags = link.symbols.flags[local];
        link.symbols.flags[local] = flags & ~BUSTER_LINK_SYMBOL_BINDING;
        const bool consistent = link_check(&link) == LINK_ERROR_NONE;
        const bool poisoned = executable_test_reset(&link, executable_path);
        executable_test_restore(&link, atoms);
        ExecutableResult written = { 0 };
        const bool succeeded = consistent && poisoned && debug_native_write(&link, entry, path, &zero_debug, 3, false, &written);
        const String8 file = succeeded ? executable_test_read(&arena, executable_path) : (String8){ 0 };
        executable_test_expect(&tally, succeeded && file.pointer != 0 && debug_native_image(&link, unit, functions, output, file, &written, &arena),
                               "local/global symbols retain binding and sh_info order", 0);
        executable_test_expect(&tally, debug_native_unchanged(unit, output, saved, positions) && debug_native_unchanged(unit, &zeroed, zero_saved, positions),
                               "local symbol write leaves caller bytes unchanged", 0);
        link.symbols.flags[local] = flags;
        const bool reset = executable_test_reset(&link, executable_path);
        executable_test_restore(&link, atoms);
        const bool restored = reset && debug_native_write(&link, entry, path, &debug, 8, true, &written);
        ready = restored && executable_test_same_file(&arena, executable_path, first);
        executable_test_expect(&tally, ready, "restore global consumer fixture byte for byte", 0);
    }
    ready = ready && debug_native_symbol_override(&link, entry, path, &debug, atoms, first, &arena, &tally);
    AtomId frame_atom = 0;
    for (AtomId atom = 1; ready && atom < link.atoms.count; atom += 1)
    {
        frame_atom = link.atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME ? atom : frame_atom;
    }
    const SymbolId last = ready ? saved_functions[unit->function_count - 1] : 0;
    const AtomId original_atom = ready ? link.symbols.atom[last] : 0;
    const uint64_t original_value = ready ? link.symbols.value[last] : 0;
    const uint64_t original_size = ready ? link.symbols.size[last] : 0;
    const uint8_t original_flags = ready ? link.symbols.flags[last] : 0;
    const uint8_t original_source = ready ? link.symbols.source[last] : 0;
    for (uint32_t change = 0; ready && change < 16; change += 1)
    {
        ExecutableDebug bad = debug;
        memcpy(bad_positions, positions, (uint64_t)unit->function_count * sizeof(uint32_t));
        memcpy(functions, saved_functions, (uint64_t)unit->function_count * sizeof(SymbolId));
        bad.line_addresses = bad_positions;
        switch (change)
        {
            case 0:
                bad.sections[DEBUG_SECTION_INFO].length = 0;
                break;
            case 1:
                bad.sections[DEBUG_SECTION_ABBREV].pointer = 0;
                break;
            case 2:
                bad.sections[DEBUG_SECTION_ADDR].length -= 1;
                break;
            case 3:
                bad.functions = 0;
                break;
            case 4:
                bad.line_addresses = 0;
                break;
            case 5:
                functions[unit->function_count - 1] = 0;
                break;
            case 6:
                functions[unit->function_count - 1] = link.symbols.count;
                break;
            case 7:
                bad_positions[0] = (uint32_t)bad.sections[DEBUG_SECTION_LINE].length;
                break;
            case 8:
                bad_positions[0] = 2;
                break;
            case 9:
                bad_positions[0] += 1;
                break;
            case 10:
                bad_positions[unit->function_count - 1] = unit->function_count > 1 ? bad_positions[0] : 0;
                break;
            case 11:
            {
                link.symbols.atom[last] = 0;
                link.symbols.source[last] = BUSTER_LINK_SOURCE_NONE;
                link.symbols.flags[last] = BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION;
            } break;
            case 12:
            {
                link.symbols.atom[last] = frame_atom;
                link.symbols.value[last] = 0;
                link.symbols.size[last] = 1;
            } break;
            case 13:
                link.symbols.flags[last] = (original_flags & ~BUSTER_LINK_SYMBOL_TYPE) | BUSTER_LINK_SYMBOL_OBJECT;
                break;
            case 14:
            {
                link.symbols.value[last] = link.atoms.size[original_atom];
                link.symbols.size[last] = 0;
            } break;
            case 15:
                link.symbols.flags[last] = (original_flags & ~BUSTER_LINK_SYMBOL_TYPE) | BUSTER_LINK_SYMBOL_IFUNC;
                break;
        }
        memcpy(before_functions, functions, (uint64_t)unit->function_count * sizeof(SymbolId));
        memcpy(before_positions, bad_positions, (uint64_t)unit->function_count * sizeof(uint32_t));
        const bool consistent = link_check(&link) == LINK_ERROR_NONE;
        executable_test_expect(&tally, consistent, "malformed descriptor keeps the Link contract", change);
        for (uint32_t way = 0; way < 2; way += 1)
        {
            executable_test_restore(&link, atoms);
            ExecutableResult refused = { 0 };
            const bool succeeded = consistent && debug_native_write(&link, entry, path, &bad, way == 0 ? 1 : 8, way != 0, &refused);
            const bool same = executable_test_same_file(&arena, executable_path, first);
            executable_test_expect(&tally, !succeeded && refused.error == EXECUTABLE_ERROR_UNSUPPORTED && same,
                                   "malformed descriptor rejected before publication", change * 2 + way);
            executable_test_expect(&tally, debug_native_unchanged(unit, output, saved, positions) &&
                                   memcmp(functions, before_functions, (uint64_t)unit->function_count * sizeof(SymbolId)) == 0 &&
                                   memcmp(bad_positions, before_positions, (uint64_t)unit->function_count * sizeof(uint32_t)) == 0,
                                   "refusal leaves source debug bytes and metadata unchanged", change * 2 + way);
        }
        link.symbols.atom[last] = original_atom;
        link.symbols.value[last] = original_value;
        link.symbols.size[last] = original_size;
        link.symbols.flags[last] = original_flags;
        link.symbols.source[last] = original_source;
    }
    executable_test_expect(&tally, ready && executable_test_runs(executable_path, (const uint8_t*)"", 0), "native fixture executes", 0);
    arena_release(&arena);
    return executable_test_report(&tally);
}
