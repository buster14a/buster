// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The object writer's DWARF (ObjectOptions.debug, #395), for test-object-writer. main.c includes this file after
// debug.c and executable_test.c, whose units (debug_write) and ELF reading (executable_test_image) it uses;
// object_writer_test.c, which declares its entry points, calls object_test_debug, object_test_debug_compare (from
// object_test_unit) and, with --cross-check, object_test_debug_cross_check. The checks:
//   - Objects: each unit of test-link's sets and of the link made by hand, its ordinary functions in TEXT atoms, in an
//     order the seed shuffles, as a debug.c unit and as two (the second written at the first's sizes), each function
//     with up to four rows at random offsets inside it, of random lines in a file of two, finalized at random addresses
//     (so that the object must zero its address slots), with and without function sections: object_test_unit's checks
//     (every byte written, object_view_open's rules, object_test_compare with the DWARF's sections between the sections
//     of atoms and the trailing ones), and object_test_debug_compare's, which works the DWARF out again from the
//     descriptor and the object's own symbols: the five sections after the sections of atoms, unallocated, each
//     followed by its relocations where it has them (.debug_info's always, .debug_line's and .debug_addr's with
//     functions); the descriptor's bytes with the fields and address slots zeroed; each field an R_X86_64_32 against its
//     section's symbol with the value it held as its addend; and each function's address slot (in its unit's
//     .debug_addr contribution) and line address an R_X86_64_64 against the symbol of the section its definition is in,
//     with its st_value as the addend. A descriptor of one unit whose unit_functions say so is the same object.
//   - Refusals: 50 changes to a valid descriptor of two units (sections missing, empty, cut or too long; columns
//     missing; counts that don't add up or move a unit's .debug_addr header; headers, unit lengths, fields and field
//     values that aren't debug.c's; .debug_info's version, unit type and address size, unit 1's version too; each of
//     the four fields in its section's bounds but not at its unit's own contribution, one byte off and unit 1's at unit
//     0's, the address base without its header or past it twice; the line program's and the range lists' headers; an
//     abbreviation table cut short, and one whose zero code is made a declaration's, which runs on into the next
//     unit's; a field whose value is right in the wrong place (the line program's one byte early, where the bytes
//     before it read as its value, the DIE's three together, and unit 1's), the compile unit DIE's abbreviation code
//     and a string's end, which move its fields; line addresses that aren't operands, aren't ascending or end past
//     .debug_line; and functions that are no
//     symbol, another unit's, not functions, data and an IFUNC), each OBJECT_ERROR_DEBUG with nothing pushed on the
//     arena and the descriptor's bytes as they were; then an object whose entry is another unit's descriptor, and
//     entries one fewer than the units.
//   - Lanes: every unit's object of a program of 24 units and a 25th without atoms, each with its own unit's entry (one
//     debug unit for an even unit, two for an odd one, none for every fourth), checked as above and the same as without
//     DWARF exactly where its entry is empty; then written by object_write_units in groups of 1, 2, 3 and 8 lanes, in
//     both section modes, each file object_build's object; and with entries one fewer than the units, which every
//     unit's object refuses, by object_build and by object_write_units, which writes none.
//   - With --cross-check, which needs readelf, llvm-readobj, llvm-dwarfdump, ld.bfd, ld.lld and mold: the units' objects
//     of two of test-link's static sets, each with its DWARF (one debug unit for an even unit, two for an odd one),
//     with and without function sections, listed by readelf and llvm-readobj (object_test_listing), and linked by
//     each linker into a program that prints what it should, whose DWARF llvm-dwarfdump --verify finds no error in,
//     and whose five sections are the objects' contributions one after another: each object's bytes, each field
//     holding its target section's contribution's offset plus what it held, and each address slot and line address
//     its function's address in the program's .symtab.

// The splitmix64 step: the tests' random numbers, from the seed.
static uint64_t object_test_debug_random(uint64_t* state)
{
    *state += 0x9e3779b97f4a7c15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// One object's DWARF: its sections' bytes (one buffer each, the units' contributions one after another), the
// descriptor over them and the columns it points at, which live on the arena the descriptor was made on.
typedef struct ObjectTestDebug ObjectTestDebug;
struct ObjectTestDebug
{
    ObjectDebug descriptor;
    uint32_t unit_functions[2];
    uint32_t fields[2 * BUSTER_OBJECT_DEBUG_FIELDS];
};

// The DWARF of unit `unit`'s functions (the ordinary ones in TEXT atoms, among the symbols object_prepare grouped by
// unit, shuffled), as `unit_count` debug.c units (1 or 2, the second the functions after the first half, written at
// the first's sizes), finalized at random addresses; on `arena`. A unit without functions gets units of none. False if
// the arena has no room or debug.c refuses a unit.
static bool object_test_debug_make(ObjectTestDebug* debug, const Link* link, const ObjectUnits* units, uint32_t unit, uint32_t unit_count, uint64_t* random,
                                   Arena* arena)
{
    const LinkSymbols* symbols = &link->symbols;
    const uint32_t* begin = units->symbol_begin + 2 * (uint64_t)unit;
    const uint64_t defined = begin[2] - begin[0];
    SymbolId* functions = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, defined + 1);
    uint32_t* line_addresses = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, defined + 1);
    DebugFunction* entries = BUSTER_ARENA_PUSH_ARRAY(arena, DebugFunction, defined + 1);
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 4 * defined + 1);
    uint32_t* lines = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 4 * defined + 1);
    uint64_t* addresses = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, defined + 1);
    *debug = (ObjectTestDebug){ 0 };
    bool valid = functions != 0 && line_addresses != 0 && entries != 0 && offsets != 0 && lines != 0 && addresses != 0;
    uint32_t count = 0;
    for (uint32_t i = begin[0]; valid && i < begin[2]; i += 1)
    {
        const SymbolId symbol = units->symbols[i];
        if (link->atoms.class[symbols->atom[symbol]] == BUSTER_LINK_CLASS_TEXT && (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_FUNCTION)
        {
            functions[count] = symbol;
            count += 1;
        }
    }
    // Shuffled: debug.c's functions may be in any order of addresses, and the relocations follow the descriptor's.
    for (uint32_t i = count; i > 1; i -= 1)
    {
        const uint32_t j = (uint32_t)(object_test_debug_random(random) % i);
        const SymbolId swapped = functions[i - 1];
        functions[i - 1] = functions[j];
        functions[j] = swapped;
    }
    static const String8 files[] = { BUSTER_S8("object_debug.c"), BUSTER_S8("object_debug.h") };
    const uint32_t half = unit_count == 2 ? count / 2 : count;
    const uint32_t first[2] = { 0, half };
    const uint32_t end[2] = { half, count };
    uint32_t row_first[3] = { 0, 0, 0 };
    uint32_t rows = 0;
    for (uint32_t f = 0; valid && f < count; f += 1)
    {
        row_first[1] = f == half ? rows : row_first[1];
        const SymbolId symbol = functions[f];
        const AtomId atom = symbols->atom[symbol];
        const uint64_t room = link->atoms.size[atom] - symbols->value[symbol];
        const uint64_t size = symbols->size[symbol] != 0 ? symbols->size[symbol] : room != 0 ? room : 1;
        const NameId name = symbols->name[symbol];
        const uint32_t row_count = (uint32_t)(object_test_debug_random(random) % 5);
        // Offsets ascending inside the function (an insertion sort of a few), and lines anywhere, 0 among them.
        for (uint32_t r = 0; r < row_count; r += 1)
        {
            uint32_t offset = (uint32_t)(object_test_debug_random(random) % size);
            uint32_t at = rows + r;
            while (at > rows && offsets[at - 1] > offset)
            {
                offsets[at] = offsets[at - 1];
                at -= 1;
            }
            offsets[at] = offset;
            lines[rows + r] = (uint32_t)(object_test_debug_random(random) % 4000);
        }
        entries[f] = (DebugFunction){
            .name = { .pointer = name != 0 ? (char*)link->names.bytes[name] : 0, .length = name != 0 ? link->names.length[name] : 0 },
            .size = (uint32_t)size,
            .first_row = rows - row_first[f < half ? 0 : 1],
            .row_count = row_count,
            .file = (uint32_t)(object_test_debug_random(random) & 1),
            .line = (uint32_t)(object_test_debug_random(random) % 4000),
            .prologue_end = row_count != 0 ? (uint32_t)(object_test_debug_random(random) % (row_count + 1)) : 0,
        };
        addresses[f] = 0x400000 + (object_test_debug_random(random) & 0xffffff);
        rows += row_count;
    }
    row_first[1] = half == count ? rows : row_first[1];
    row_first[2] = rows;
    // Each unit measured, then the sections' buffers, then each unit written at its bases there and finalized.
    DebugUnit parts[2];
    DebugOutput outputs[2];
    uint64_t sizes[BUSTER_DEBUG_SECTIONS] = { 0 };
    for (uint32_t k = 0; valid && k < unit_count; k += 1)
    {
        parts[k] = (DebugUnit){
            .directory = BUSTER_S8("/object/debug"),
            .files = files,
            .file_count = BUSTER_ARRAY_LENGTH(files),
            .functions = entries + first[k],
            .function_count = end[k] - first[k],
            .offsets = offsets + row_first[k],
            .lines = lines + row_first[k],
            .row_count = row_first[k + 1] - row_first[k],
        };
        outputs[k] = (DebugOutput){
            .line_addresses = line_addresses + first[k],
            .address_capacity = end[k] - first[k],
        };
        memcpy(outputs[k].bases, sizes, sizeof(sizes));
        valid = debug_write(&parts[k], &outputs[k], false) == DEBUG_ERROR_CAPACITY;
        for (uint32_t s = 0; valid && s < BUSTER_DEBUG_SECTIONS; s += 1)
        {
            sizes[s] += outputs[k].sections[s].size;
        }
    }
    uint8_t* bytes[BUSTER_DEBUG_SECTIONS] = { 0 };
    for (uint32_t s = 0; valid && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        bytes[s] = arena_push(arena, sizes[s], 8);
        valid = bytes[s] != 0;
    }
    for (uint32_t k = 0; valid && k < unit_count; k += 1)
    {
        for (uint32_t s = 0; s < BUSTER_DEBUG_SECTIONS; s += 1)
        {
            outputs[k].sections[s].bytes = bytes[s] + outputs[k].bases[s];
            outputs[k].sections[s].capacity = outputs[k].sections[s].size;
        }
        valid = debug_write(&parts[k], &outputs[k], false) == DEBUG_OK && debug_finalize(&parts[k], &outputs[k], addresses + first[k]) == DEBUG_OK;
        for (uint32_t i = first[k]; valid && i < end[k]; i += 1)
        {
            line_addresses[i] += (uint32_t)outputs[k].bases[DEBUG_SECTION_LINE];
        }
        for (uint32_t field = 0; valid && field < BUSTER_OBJECT_DEBUG_FIELDS; field += 1)
        {
            debug->fields[BUSTER_OBJECT_DEBUG_FIELDS * k + field] = (uint32_t)outputs[k].bases[DEBUG_SECTION_INFO] + outputs[k].fields[DEBUG_SECTION_ABBREV + field];
        }
        debug->unit_functions[k] = end[k] - first[k];
    }
    debug->descriptor = (ObjectDebug){
        .functions = functions,
        .line_addresses = line_addresses,
        .function_count = count,
        .unit_functions = unit_count == 2 ? debug->unit_functions : 0,
        .unit_count = unit_count == 2 ? 2 : 0,
        .fields = debug->fields,
    };
    for (uint32_t s = 0; valid && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        debug->descriptor.sections[s] = (String8){ .pointer = (char*)bytes[s], .length = sizes[s] };
    }
    return valid;
}

// ObjectOptions.debug for an object of unit `unit`: an entry per unit of `units`, `debug` the unit's and every other one
// empty; on `arena`, or null if it has no room.
static ObjectDebug* object_test_debug_entries(const ObjectDebug* debug, uint32_t unit, const ObjectUnits* units, Arena* arena)
{
    ObjectDebug* entries = BUSTER_ARENA_PUSH_ARRAY(arena, ObjectDebug, units->count);
    if (entries != 0)
    {
        memset(entries, 0, (uint64_t)units->count * sizeof(ObjectDebug));
        entries[unit] = *debug;
    }
    return entries;
}

// A copy of a descriptor's bytes, on `arena`, to check later that object_build left them alone (an executable may take
// the same unit after an object has).
static bool object_test_debug_snapshot(const ObjectDebug* debug, String8* copies, Arena* arena)
{
    bool result = true;
    for (uint32_t s = 0; result && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        copies[s] = (String8){ .pointer = arena_push(arena, debug->sections[s].length, 8), .length = debug->sections[s].length };
        result = copies[s].pointer != 0;
        if (result)
        {
            memcpy(copies[s].pointer, debug->sections[s].pointer, copies[s].length);
        }
    }
    return result;
}

static bool object_test_debug_unchanged(const ObjectDebug* debug, const String8* copies)
{
    bool result = true;
    for (uint32_t s = 0; result && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        result = debug->sections[s].length == copies[s].length && memcmp(debug->sections[s].pointer, copies[s].pointer, copies[s].length) == 0;
    }
    return result;
}

// A unit's function count in a descriptor (ObjectDebug's rule, written again here).
static uint32_t object_test_debug_unit_functions(const ObjectDebug* debug, uint32_t unit)
{
    return debug->unit_count != 0 ? debug->unit_functions[unit] : debug->function_count;
}

// The STT_SECTION symbol of a view's section `section`, or 0.
static uint64_t object_test_debug_section_symbol(const ObjectView* view, uint64_t section)
{
    uint64_t result = 0;
    for (uint64_t i = 1; i < view->symbol_count; i += 1)
    {
        result = ELF64_ST_TYPE(view->symbols[i].st_info) == STT_SECTION && object_view_symbol_section(view, i) == section ? i : result;
    }
    return result;
}

// Checks an object's DWARF, as `view` read it, against the descriptor it was built with (see the file's comment).
static bool object_test_debug_compare(ObjectTestTally* tally, const ObjectView* view, const Link* link, const ObjectDebug* debug)
{
    static const char* const names[BUSTER_DEBUG_SECTIONS] = { ".debug_info", ".debug_abbrev", ".debug_line", ".debug_addr", ".debug_rnglists" };
    const uint64_t failures = tally->failures;
    const uint32_t unit_count = debug->unit_count != 0 ? debug->unit_count : 1;
    const uint64_t relocation_counts[BUSTER_DEBUG_SECTIONS] = {
        [DEBUG_SECTION_INFO] = (uint64_t)BUSTER_OBJECT_DEBUG_FIELDS * unit_count,
        [DEBUG_SECTION_LINE] = debug->function_count,
        [DEBUG_SECTION_ADDR] = debug->function_count,
    };
    // The five sections in debug.c's order, each followed by its relocations where it has them, then .note.GNU-stack.
    uint64_t index = object_test_section_named(view, names[0], false);
    uint64_t sections[BUSTER_DEBUG_SECTIONS] = { 0 };
    uint64_t relocations[BUSTER_DEBUG_SECTIONS] = { 0 };
    bool placed = index != 0;
    for (uint32_t s = 0; placed && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        const Elf64_Shdr* header = index < view->section_count ? &view->sections[index] : 0;
        const char* name = header != 0 ? object_view_section_name(view, index) : 0;
        placed = name != 0 && strcmp(name, names[s]) == 0 && header->sh_type == SHT_PROGBITS && header->sh_flags == 0 && header->sh_addralign == 1 &&
                 header->sh_size == debug->sections[s].length && header->sh_link == 0 && header->sh_info == 0 && header->sh_entsize == 0;
        sections[s] = index;
        index += 1;
        if (placed && relocation_counts[s] != 0)
        {
            const Elf64_Shdr* rela = index < view->section_count ? &view->sections[index] : 0;
            const char* rela_name = rela != 0 ? object_view_section_name(view, index) : 0;
            placed = rela_name != 0 && strncmp(rela_name, ".rela", 5) == 0 && strcmp(rela_name + 5, names[s]) == 0 && rela->sh_type == SHT_RELA &&
                     rela->sh_info == sections[s] && rela->sh_link == view->symbol_table_index && rela->sh_size == relocation_counts[s] * sizeof(Elf64_Rela);
            relocations[s] = index;
            index += 1;
        }
    }
    const char* after = placed && index < view->section_count ? object_view_section_name(view, index) : 0;
    object_test_expect(tally, placed && after != 0 && strcmp(after, ".note.GNU-stack") == 0, "the DWARF's sections and their relocations, after the sections of atoms", 0);
    // Their bytes: the descriptor's, with each field, address slot and line address zero.
    const uint8_t* contents[BUSTER_DEBUG_SECTIONS] = { 0 };
    for (uint32_t s = 0; placed && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        contents[s] = view->file + view->sections[sections[s]].sh_offset;
    }
    bool zeroed = placed;
    uint8_t* zero_map[BUSTER_DEBUG_SECTIONS] = { 0 };
    for (uint32_t s = 0; zeroed && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        zero_map[s] = calloc(debug->sections[s].length + 1, 1);
        zeroed = zero_map[s] != 0;
    }
    for (uint64_t f = 0; zeroed && f < (uint64_t)BUSTER_OBJECT_DEBUG_FIELDS * unit_count; f += 1)
    {
        memset(zero_map[DEBUG_SECTION_INFO] + debug->fields[f], 1, 4);
    }
    uint64_t slot_base = 0;
    uint32_t function = 0;
    for (uint32_t unit = 0; zeroed && unit < unit_count; unit += 1)
    {
        const uint32_t count = object_test_debug_unit_functions(debug, unit);
        for (uint32_t i = 0; i < count; i += 1)
        {
            memset(zero_map[DEBUG_SECTION_ADDR] + slot_base + 8 + 8ull * i, 1, 8);
            memset(zero_map[DEBUG_SECTION_LINE] + debug->line_addresses[function], 1, 8);
            function += 1;
        }
        slot_base += 8 + 8ull * count;
    }
    for (uint32_t s = 0; zeroed && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        for (uint64_t at = 0; zeroed && at < debug->sections[s].length; at += 1)
        {
            zeroed = contents[s][at] == (zero_map[s][at] != 0 ? 0 : (uint8_t)debug->sections[s].pointer[at]);
        }
    }
    for (uint32_t s = 0; s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        free(zero_map[s]);
    }
    object_test_expect(tally, zeroed, "the DWARF's bytes, with the fields and address slots zero", 0);
    // The section symbols: the four sections the fields point into have one each, .debug_info none.
    uint64_t symbols[BUSTER_DEBUG_SECTIONS] = { 0 };
    for (uint32_t s = 0; placed && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        symbols[s] = object_test_debug_section_symbol(view, sections[s]);
    }
    object_test_expect(tally, placed && symbols[DEBUG_SECTION_INFO] == 0 && symbols[DEBUG_SECTION_ABBREV] != 0 && symbols[DEBUG_SECTION_LINE] != 0 &&
                                  symbols[DEBUG_SECTION_ADDR] != 0 && symbols[DEBUG_SECTION_RNGLISTS] != 0,
                       "the section symbols of the sections the fields point into", 0);
    // Each field: an R_X86_64_32 against its section's symbol, with the offset it held.
    const Elf64_Rela* fields = placed ? (const Elf64_Rela*)(view->file + view->sections[relocations[DEBUG_SECTION_INFO]].sh_offset) : 0;
    for (uint64_t f = 0; fields != 0 && f < (uint64_t)BUSTER_OBJECT_DEBUG_FIELDS * unit_count; f += 1)
    {
        uint32_t value = 0;
        memcpy(&value, debug->sections[DEBUG_SECTION_INFO].pointer + debug->fields[f], 4);
        const uint64_t target = symbols[DEBUG_SECTION_ABBREV + f % BUSTER_OBJECT_DEBUG_FIELDS];
        object_test_expect(tally, fields[f].r_offset == debug->fields[f] && ELF64_R_TYPE(fields[f].r_info) == R_X86_64_32 && ELF64_R_SYM(fields[f].r_info) == target &&
                                      fields[f].r_addend == (int64_t)value,
                           "a field's relocation, against its section's symbol", f);
    }
    // Each function's address slot and line address: an R_X86_64_64 against its section's symbol, at its st_value.
    const Elf64_Rela* lines = placed && debug->function_count != 0 ? (const Elf64_Rela*)(view->file + view->sections[relocations[DEBUG_SECTION_LINE]].sh_offset) : 0;
    const Elf64_Rela* slots = placed && debug->function_count != 0 ? (const Elf64_Rela*)(view->file + view->sections[relocations[DEBUG_SECTION_ADDR]].sh_offset) : 0;
    slot_base = 0;
    function = 0;
    for (uint32_t unit = 0; slots != 0 && unit < unit_count; unit += 1)
    {
        const uint32_t count = object_test_debug_unit_functions(debug, unit);
        for (uint32_t i = 0; i < count; i += 1)
        {
            const NameId name = link->symbols.name[debug->functions[function]];
            char text[256];
            snprintf(text, sizeof(text), "%.*s", (int)link->names.length[name], (const char*)link->names.bytes[name]);
            const uint64_t own = object_test_symbol(view, text, 0, 0);
            const Elf64_Sym* entry = own != 0 ? &view->symbols[own] : 0;
            const uint64_t symbol = entry != 0 ? object_test_debug_section_symbol(view, object_view_symbol_section(view, own)) : 0;
            const bool slot = symbol != 0 && slots[function].r_offset == slot_base + 8 + 8ull * i && ELF64_R_TYPE(slots[function].r_info) == R_X86_64_64 &&
                              ELF64_R_SYM(slots[function].r_info) == symbol && slots[function].r_addend == (int64_t)entry->st_value;
            const bool line = symbol != 0 && lines[function].r_offset == debug->line_addresses[function] && ELF64_R_TYPE(lines[function].r_info) == R_X86_64_64 &&
                              ELF64_R_SYM(lines[function].r_info) == symbol && lines[function].r_addend == (int64_t)entry->st_value;
            object_test_expect(tally, entry != 0 && ELF64_ST_TYPE(entry->st_info) == STT_FUNC && slot && line,
                               "a function's address slot and line address, against its definition's section", function);
            function += 1;
        }
        slot_base += 8 + 8ull * count;
    }
    return tally->failures == failures;
}

// A symbol of the link by name, or 0.
static SymbolId object_test_debug_named(const Link* link, const char* text)
{
    SymbolId result = 0;
    const uint64_t length = strlen(text);
    for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
    {
        const NameId name = link->symbols.name[symbol];
        result = name != 0 && link->names.length[name] == length && memcmp(link->names.bytes[name], text, length) == 0 ? symbol : result;
    }
    return result;
}

// The refusals (see the file's comment), on the link made by hand: unit 0's two functions as two debug units, each
// change made to a copy of the descriptor, its columns and the bytes it changes, then unit 2's with its IFUNC.
static void object_test_debug_refusals(ObjectTestTally* tally, const Link* hand, ObjectUnits* units, ObjectSymbols* symbols, uint64_t* random, Arena* arena)
{
    const uint64_t length = arena->length;
    ObjectTestDebug debug;
    const bool made = object_test_debug_make(&debug, hand, units, 0, 2, random, arena) && debug.descriptor.function_count == 2;
    object_test_expect(tally, made, "a descriptor of unit 0's two functions, as two units", 0);
    const ObjectDebug valid = debug.descriptor;
    const SymbolId far = object_test_debug_named(hand, "far");
    const SymbolId inside = object_test_debug_named(hand, "inside");
    const SymbolId small = object_test_debug_named(hand, "small");
    const SymbolId chosen = object_test_debug_named(hand, "chosen");
    // The section bytes a change may edit, copied, and the columns.
    uint8_t* copies[BUSTER_DEBUG_SECTIONS] = { 0 };
    bool copied = made && far != 0 && inside != 0 && small != 0 && chosen != 0;
    for (uint32_t s = 0; copied && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        copies[s] = arena_push(arena, valid.sections[s].length, 8);
        copied = copies[s] != 0;
    }
    SymbolId functions[2];
    uint32_t lines[2];
    uint32_t counts[2];
    uint32_t fields[2 * BUSTER_OBJECT_DEBUG_FIELDS];
    const uint32_t changes = 50;
    for (uint32_t change = 0; copied && change < changes; change += 1)
    {
        ObjectDebug bad = valid;
        for (uint32_t s = 0; s < BUSTER_DEBUG_SECTIONS; s += 1)
        {
            memcpy(copies[s], valid.sections[s].pointer, valid.sections[s].length);
            bad.sections[s].pointer = (char*)copies[s];
        }
        memcpy(functions, valid.functions, sizeof(functions));
        memcpy(lines, valid.line_addresses, sizeof(lines));
        memcpy(counts, valid.unit_functions, sizeof(counts));
        memcpy(fields, valid.fields, sizeof(fields));
        bad.functions = functions;
        bad.line_addresses = lines;
        bad.unit_functions = counts;
        bad.fields = fields;
        uint32_t word = 0;
        uint32_t info_length = 0;
        memcpy(&info_length, valid.sections[DEBUG_SECTION_INFO].pointer, 4);
        switch (change)
        {
            case 0:
                bad.sections[DEBUG_SECTION_INFO].pointer = 0;
                break;
            case 1:
                bad.sections[DEBUG_SECTION_ABBREV].length = 0;
                break;
            case 2:
                bad.sections[DEBUG_SECTION_ADDR].length -= 1;
                break;
            case 3:
                bad.sections[DEBUG_SECTION_LINE].length = 0xfffffff0u;
                break;
            case 4:
                bad.functions = 0;
                break;
            case 5:
                bad.line_addresses = 0;
                break;
            case 6:
                bad.unit_functions = 0;
                break;
            case 7:
                bad.fields = 0;
                break;
            case 8:
                counts[0] += 1;
                break;
            case 9:
                counts[0] += 1;
                counts[1] -= 1;
                break;
            case 10:
                memcpy(&word, copies[DEBUG_SECTION_ADDR], 4);
                word += 8;
                memcpy(copies[DEBUG_SECTION_ADDR], &word, 4);
                break;
            case 11:
                copies[DEBUG_SECTION_ADDR][4] = 4;
                break;
            case 12:
                memcpy(&word, copies[DEBUG_SECTION_INFO], 4);
                word += 1;
                memcpy(copies[DEBUG_SECTION_INFO], &word, 4);
                break;
            case 13:
                fields[0] += 4;
                break;
            case 14:
                fields[1] = valid.fields[2];
                fields[2] = valid.fields[1];
                break;
            case 15:
                memcpy(&word, copies[DEBUG_SECTION_INFO], 4);
                fields[3] = word + 4 - 2;
                break;
            case 16:
                word = (uint32_t)valid.sections[DEBUG_SECTION_LINE].length + 1;
                memcpy(copies[DEBUG_SECTION_INFO] + fields[1], &word, 4);
                break;
            case 17:
                lines[0] += 1;
                break;
            case 18:
                lines[0] = valid.line_addresses[1];
                lines[1] = valid.line_addresses[0];
                break;
            case 19:
                lines[1] = (uint32_t)valid.sections[DEBUG_SECTION_LINE].length - 7;
                break;
            case 20:
                functions[0] = 0;
                break;
            case 21:
                functions[0] = hand->symbols.count;
                break;
            case 22:
                functions[0] = far;
                break;
            case 23:
                functions[0] = inside;
                break;
            case 24:
                functions[0] = small;
                break;
            // The headers debug.c writes: .debug_info's version, unit type and address size, unit 1's version too.
            case 25:
                copies[DEBUG_SECTION_INFO][4] = 4;
                break;
            case 26:
                copies[DEBUG_SECTION_INFO][6] = 2;
                break;
            case 27:
                copies[DEBUG_SECTION_INFO][7] = 4;
                break;
            case 28:
                copies[DEBUG_SECTION_INFO][4 + info_length + 4] = 4;
                break;
            // Each of the four fields in its section's bounds but not at its unit's own contribution: one byte off, unit
            // 1's at unit 0's, and the address base without its header's 8 bytes or past them twice.
            case 29:
                memcpy(&word, copies[DEBUG_SECTION_INFO] + valid.fields[0], 4);
                word += 1;
                memcpy(copies[DEBUG_SECTION_INFO] + valid.fields[0], &word, 4);
                break;
            case 30:
                memcpy(copies[DEBUG_SECTION_INFO] + valid.fields[4], copies[DEBUG_SECTION_INFO] + valid.fields[0], 4);
                break;
            case 31:
                memcpy(&word, copies[DEBUG_SECTION_INFO] + valid.fields[1], 4);
                word += 1;
                memcpy(copies[DEBUG_SECTION_INFO] + valid.fields[1], &word, 4);
                break;
            case 32:
                memcpy(copies[DEBUG_SECTION_INFO] + valid.fields[5], copies[DEBUG_SECTION_INFO] + valid.fields[1], 4);
                break;
            case 33:
                memcpy(&word, copies[DEBUG_SECTION_INFO] + valid.fields[2], 4);
                word += 8;
                memcpy(copies[DEBUG_SECTION_INFO] + valid.fields[2], &word, 4);
                break;
            case 34:
                memcpy(&word, copies[DEBUG_SECTION_INFO] + valid.fields[2], 4);
                word -= 8;
                memcpy(copies[DEBUG_SECTION_INFO] + valid.fields[2], &word, 4);
                break;
            case 35:
                memcpy(&word, copies[DEBUG_SECTION_INFO] + valid.fields[3], 4);
                word += 1;
                memcpy(copies[DEBUG_SECTION_INFO] + valid.fields[3], &word, 4);
                break;
            case 36:
                memcpy(copies[DEBUG_SECTION_INFO] + valid.fields[7], copies[DEBUG_SECTION_INFO] + valid.fields[3], 4);
                break;
            // The line program's and the range lists' headers, and an abbreviation table that doesn't end where the
            // next starts: cut short, and unit 0's with its zero code made a declaration's, which runs on into unit 1's.
            case 37:
                copies[DEBUG_SECTION_LINE][4] = 4;
                break;
            case 38:
                copies[DEBUG_SECTION_LINE][6] = 4;
                break;
            case 39:
                copies[DEBUG_SECTION_RNGLISTS][4] = 4;
                break;
            case 40:
                copies[DEBUG_SECTION_RNGLISTS][7] = 1;
                break;
            case 41:
                copies[DEBUG_SECTION_RNGLISTS][8] = 1;
                break;
            case 42:
                bad.sections[DEBUG_SECTION_ABBREV].length -= 1;
                break;
            case 43:
                memcpy(&word, valid.sections[DEBUG_SECTION_INFO].pointer + valid.fields[4], 4);
                copies[DEBUG_SECTION_ABBREV][word - 1] = 1;
                break;
            // A field whose value is right in the wrong place: the line program's one byte early, where the language's
            // high byte and three of the offset's zeros read as its unit's 0 (#474's fourth review), all three of the
            // DIE's one byte early, and unit 1's line program's; and the compile unit DIE's abbreviation code, and a
            // string's end, which move where its fields are.
            case 44:
                fields[1] -= 1;
                break;
            case 45:
                fields[1] -= 1;
                fields[2] -= 1;
                fields[3] -= 1;
                break;
            case 46:
                fields[5] -= 1;
                break;
            case 47:
                copies[DEBUG_SECTION_INFO][12] = 2;
                break;
            case 48:
                *(uint8_t*)memchr(copies[DEBUG_SECTION_INFO] + 13, 0, info_length) = 'x';
                break;
            default:
                bad.sections[DEBUG_SECTION_RNGLISTS].pointer = 0;
                break;
        }
        ObjectDebug* entries = object_test_debug_entries(&bad, 0, units, arena);
        const uint64_t before = arena->length;
        const ObjectOptions options = { .debug = entries, .debug_count = units->count };
        const Object refused = entries != 0 ? object_build(hand, units, symbols, 0, options, BUSTER_S8("u0.c"), arena) : (Object){ .error = OBJECT_ERROR_FULL };
        object_test_expect(tally, refused.error == OBJECT_ERROR_DEBUG && refused.bytes == 0 && arena->length == before, "a descriptor object_build refuses", change);
    }
    // The descriptor itself, unchanged by any of it, still builds.
    if (copied)
    {
        ObjectDebug* entries = object_test_debug_entries(&valid, 0, units, arena);
        const uint64_t before = arena->length;
        const ObjectOptions options = { .debug = entries, .debug_count = units->count };
        const Object built = entries != 0 ? object_build(hand, units, symbols, 0, options, BUSTER_S8("u0.c"), arena) : (Object){ .error = OBJECT_ERROR_FULL };
        object_test_expect(tally, built.error == OBJECT_ERROR_NONE, "the descriptor the changes started from", 0);
        arena_pop_to(arena, before);
    }
    // An IFUNC, unit 2's: refused too, and its descriptor without it taken.
    ObjectTestDebug ifunc;
    const bool ifunc_made = object_test_debug_make(&ifunc, hand, units, 2, 1, random, arena) && ifunc.descriptor.function_count == 1;
    object_test_expect(tally, ifunc_made, "a descriptor of unit 2's function", 0);
    ObjectDebug* entries = ifunc_made ? object_test_debug_entries(&ifunc.descriptor, 2, units, arena) : 0;
    if (entries != 0)
    {
        const uint64_t before = arena->length;
        const ObjectOptions options = { .debug = entries, .debug_count = units->count };
        const Object built = object_build(hand, units, symbols, 2, options, BUSTER_S8("u2.c"), arena);
        object_test_expect(tally, built.error == OBJECT_ERROR_NONE, "unit 2's descriptor", 0);
        arena_pop_to(arena, before);
        SymbolId ifunc_function = chosen;
        entries[2].functions = &ifunc_function;
        const Object refused = object_build(hand, units, symbols, 2, options, BUSTER_S8("u2.c"), arena);
        object_test_expect(tally, refused.error == OBJECT_ERROR_DEBUG && refused.bytes == 0 && arena->length == before, "a descriptor of an IFUNC", 0);
        // Another unit's object: unit 0's, with unit 2's descriptor as its entry.
        entries[2].functions = ifunc.descriptor.functions;
        entries[0] = entries[2];
        const Object other = object_build(hand, units, symbols, 0, options, BUSTER_S8("u0.c"), arena);
        object_test_expect(tally, other.error == OBJECT_ERROR_DEBUG && other.bytes == 0 && arena->length == before, "another unit's descriptor", 0);
        // Entries that aren't the units': one fewer, which unit 2's object refuses though its entry is valid.
        entries[0] = (ObjectDebug){ 0 };
        const ObjectOptions fewer = { .debug = entries, .debug_count = units->count - 1 };
        const Object short_entries = object_build(hand, units, symbols, 2, fewer, BUSTER_S8("u2.c"), arena);
        object_test_expect(tally, short_entries.error == OBJECT_ERROR_DEBUG && short_entries.bytes == 0 && arena->length == before, "entries one fewer than the units", 0);
    }
    arena_pop_to(arena, length);
}

static bool object_test_debug(uint64_t seed, Arena* arena)
{
    ObjectTestTally tally = {
        .check = "debug",
    };
    uint64_t random = seed ^ 0x6f626a6563742d67ull;
    LinkProgramOptions options[BUSTER_LINK_TEST_ALL_PROGRAMS];
    const char* names[BUSTER_LINK_TEST_ALL_PROGRAMS];
    const char* directories[BUSTER_LINK_TEST_ALL_PROGRAMS];
    link_test_all_options(options, names, directories, seed, BUSTER_OBJECT_TEST_FUNCTIONS);
    uint64_t objects = 0;
    uint64_t functions = 0;
    for (uint32_t i = 0; i < BUSTER_LINK_TEST_ALL_PROGRAMS; i += 1)
    {
        LinkProgram program;
        const bool generated = link_program_generate(&program, options[i]);
        object_test_expect(&tally, generated, names[i], i);
        const uint64_t length = arena->length;
        ObjectUnits units;
        ObjectSymbols symbols;
        const bool prepared = generated && object_prepare(&units, &program.link, options[i].units, arena) && object_map_symbols(&symbols, &program.link, arena);
        object_test_expect(&tally, !generated || prepared, "object_prepare", i);
        for (uint32_t unit = 0; prepared && unit < units.count; unit += 1)
        {
            for (uint32_t parts = 1; parts <= 2; parts += 1)
            {
                const uint64_t before = arena->length;
                ObjectTestDebug debug;
                String8 copies[BUSTER_DEBUG_SECTIONS];
                const bool made = object_test_debug_make(&debug, &program.link, &units, unit, parts, &random, arena) &&
                                  object_test_debug_snapshot(&debug.descriptor, copies, arena);
                ObjectDebug* entries = made ? object_test_debug_entries(&debug.descriptor, unit, &units, arena) : 0;
                object_test_expect(&tally, entries != 0, "the unit's DWARF", unit);
                for (uint32_t sections = 0; entries != 0 && sections < 2; sections += 1)
                {
                    const ObjectOptions object_options = {
                        .function_sections = sections == 1,
                        .debug = entries,
                        .debug_count = units.count,
                    };
                    ObjectTestCounts counts = { 0 };
                    object_test_unit(&tally, &program.link, &units, &symbols, unit, object_options, &counts, arena);
                    objects += 1;
                    functions += debug.descriptor.function_count;
                }
                object_test_expect(&tally, !made || object_test_debug_unchanged(&debug.descriptor, copies), "the descriptor's bytes as they were", unit);
                arena_pop_to(arena, before);
            }
        }
        // One unit, said so by unit_functions: the same object as a descriptor without them.
        if (prepared)
        {
            const uint64_t before = arena->length;
            ObjectTestDebug debug;
            const bool made = object_test_debug_make(&debug, &program.link, &units, 0, 1, &random, arena);
            const uint32_t all = debug.descriptor.function_count;
            ObjectDebug counted = debug.descriptor;
            counted.unit_functions = &all;
            counted.unit_count = 1;
            ObjectDebug* plain_entries = made ? object_test_debug_entries(&debug.descriptor, 0, &units, arena) : 0;
            ObjectDebug* said_entries = made ? object_test_debug_entries(&counted, 0, &units, arena) : 0;
            const ObjectOptions plain = { .debug = plain_entries, .debug_count = units.count };
            const ObjectOptions said = { .debug = said_entries, .debug_count = units.count };
            const bool entered = plain_entries != 0 && said_entries != 0;
            const Object first = entered ? object_build(&program.link, &units, &symbols, 0, plain, BUSTER_S8("u0.c"), arena) : (Object){ 0 };
            const Object second = entered ? object_build(&program.link, &units, &symbols, 0, said, BUSTER_S8("u0.c"), arena) : (Object){ 0 };
            object_test_expect(&tally, entered && first.error == OBJECT_ERROR_NONE && second.error == OBJECT_ERROR_NONE && first.size == second.size &&
                                           memcmp(first.bytes, second.bytes, first.size) == 0,
                               "one unit, said so, is the same object", i);
            arena_pop_to(arena, before);
        }
        arena_pop_to(arena, length);
        link_program_release(&program);
    }
    // The link made by hand: every unit (1 and 3 without atoms, unit 2 with an IFUNC beside its function), then the
    // refusals.
    const uint64_t length = arena->length;
    Link hand;
    ObjectUnits units;
    ObjectSymbols symbols;
    const bool prepared = object_test_hand_link(&hand, arena) && object_prepare(&units, &hand, BUSTER_OBJECT_TEST_HAND_UNITS, arena) &&
                          object_map_symbols(&symbols, &hand, arena);
    object_test_expect(&tally, prepared, "the link made by hand", 0);
    for (uint32_t unit = 0; prepared && unit < units.count; unit += 1)
    {
        for (uint32_t parts = 1; parts <= 2; parts += 1)
        {
            const uint64_t before = arena->length;
            ObjectTestDebug debug;
            String8 copies[BUSTER_DEBUG_SECTIONS];
            const bool made = object_test_debug_make(&debug, &hand, &units, unit, parts, &random, arena) && object_test_debug_snapshot(&debug.descriptor, copies, arena);
            ObjectDebug* entries = made ? object_test_debug_entries(&debug.descriptor, unit, &units, arena) : 0;
            object_test_expect(&tally, entries != 0, "the hand link unit's DWARF", unit);
            for (uint32_t sections = 0; entries != 0 && sections < 2; sections += 1)
            {
                const ObjectOptions object_options = {
                    .function_sections = sections == 1,
                    .debug = entries,
                    .debug_count = units.count,
                };
                ObjectTestCounts counts = { 0 };
                object_test_unit(&tally, &hand, &units, &symbols, unit, object_options, &counts, arena);
                objects += 1;
                functions += debug.descriptor.function_count;
            }
            object_test_expect(&tally, !made || object_test_debug_unchanged(&debug.descriptor, copies), "the descriptor's bytes as they were", unit);
            arena_pop_to(arena, before);
        }
    }
    if (prepared)
    {
        object_test_debug_refusals(&tally, &hand, &units, &symbols, &random, arena);
    }
    arena_pop_to(arena, length);
    printf("  %llu objects with DWARF, of %llu functions\n", (unsigned long long)objects, (unsigned long long)functions);
    return object_test_report(&tally);
}

// The sections of a linked program, `file`, by debug.c's kinds, or false if one is missing or outside the file.
static bool object_test_debug_linked(String8 file, String8* sections)
{
    static const char* const names[BUSTER_DEBUG_SECTIONS] = { ".debug_info", ".debug_abbrev", ".debug_line", ".debug_addr", ".debug_rnglists" };
    const ExecutableTestImage image = executable_test_image(file);
    bool result = image.valid;
    for (uint32_t s = 0; result && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        const Elf64_Shdr* header = executable_test_section(&image, names[s]);
        result = header != 0 && header->sh_offset <= file.length && header->sh_size <= file.length - header->sh_offset;
        sections[s] = result ? (String8){ .pointer = file.pointer + header->sh_offset, .length = header->sh_size } : (String8){ 0 };
    }
    return result;
}

// A linked program's symbol named `name`'s value: its .symtab's, which a static link writes; 0 if it has none.
static uint64_t object_test_debug_address(String8 file, const String8 name)
{
    const ExecutableTestImage image = executable_test_image(file);
    const Elf64_Shdr* table = image.valid ? executable_test_section(&image, ".symtab") : 0;
    const Elf64_Shdr* strings = table != 0 && table->sh_link < image.header->e_shnum ? &image.section_headers[table->sh_link] : 0;
    uint64_t result = 0;
    for (uint64_t i = 1; strings != 0 && result == 0 && i < table->sh_size / sizeof(Elf64_Sym); i += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, file.pointer + table->sh_offset + i * sizeof(symbol), sizeof(symbol));
        const char* text = symbol.st_name < strings->sh_size ? file.pointer + strings->sh_offset + symbol.st_name : 0;
        result = text != 0 && ELF64_ST_TYPE(symbol.st_info) == STT_FUNC && strlen(text) == name.length && memcmp(text, name.pointer, name.length) == 0 ? symbol.st_value : 0;
    }
    return result;
}

// Whether a linked program's DWARF is its objects' (see the file's comment), `debugs[unit]` unit `unit`'s object's.
static bool object_test_debug_program(String8 file, const Link* link, const ObjectTestDebug* debugs, uint32_t unit_count)
{
    String8 sections[BUSTER_DEBUG_SECTIONS];
    bool result = object_test_debug_linked(file, sections);
    uint64_t bases[BUSTER_DEBUG_SECTIONS] = { 0 };
    for (uint32_t unit = 0; unit < unit_count; unit += 1)
    {
        for (uint32_t s = 0; s < BUSTER_DEBUG_SECTIONS; s += 1)
        {
            bases[s] += debugs[unit].descriptor.sections[s].length;
        }
    }
    for (uint32_t s = 0; result && s < BUSTER_DEBUG_SECTIONS; s += 1)
    {
        result = sections[s].length == bases[s];
    }
    memset(bases, 0, sizeof(bases));
    for (uint32_t unit = 0; result && unit < unit_count; unit += 1)
    {
        const ObjectDebug* debug = &debugs[unit].descriptor;
        uint8_t* expected[BUSTER_DEBUG_SECTIONS] = { 0 };
        for (uint32_t s = 0; result && s < BUSTER_DEBUG_SECTIONS; s += 1)
        {
            expected[s] = malloc(debug->sections[s].length + 1);
            result = expected[s] != 0;
            if (result)
            {
                memcpy(expected[s], debug->sections[s].pointer, debug->sections[s].length);
            }
        }
        const uint32_t units = debug->unit_count != 0 ? debug->unit_count : 1;
        for (uint64_t f = 0; result && f < (uint64_t)BUSTER_OBJECT_DEBUG_FIELDS * units; f += 1)
        {
            uint32_t value = 0;
            memcpy(&value, expected[DEBUG_SECTION_INFO] + debug->fields[f], 4);
            value += (uint32_t)bases[DEBUG_SECTION_ABBREV + f % BUSTER_OBJECT_DEBUG_FIELDS];
            memcpy(expected[DEBUG_SECTION_INFO] + debug->fields[f], &value, 4);
        }
        uint64_t slot_base = 0;
        uint32_t function = 0;
        for (uint32_t k = 0; result && k < units; k += 1)
        {
            const uint32_t count = object_test_debug_unit_functions(debug, k);
            for (uint32_t i = 0; result && i < count; i += 1)
            {
                const NameId name = link->symbols.name[debug->functions[function]];
                const uint64_t address = object_test_debug_address(file, (String8){ .pointer = (char*)link->names.bytes[name], .length = link->names.length[name] });
                result = address != 0;
                memcpy(expected[DEBUG_SECTION_ADDR] + slot_base + 8 + 8ull * i, &address, 8);
                memcpy(expected[DEBUG_SECTION_LINE] + debug->line_addresses[function], &address, 8);
                function += 1;
            }
            slot_base += 8 + 8ull * count;
        }
        for (uint32_t s = 0; result && s < BUSTER_DEBUG_SECTIONS; s += 1)
        {
            result = memcmp(sections[s].pointer + bases[s], expected[s], debug->sections[s].length) == 0;
            bases[s] += debug->sections[s].length;
        }
        for (uint32_t s = 0; s < BUSTER_DEBUG_SECTIONS; s += 1)
        {
            free(expected[s]);
        }
    }
    return result;
}

static bool object_test_debug_cross_check(uint64_t seed, const char* directory, bool keep, Arena* arena)
{
    ObjectTestTally tally = {
        .check = "debug cross-check",
    };
    static const char* const linkers[] = { "ld.bfd", "ld.lld", "mold" };
    uint64_t random = seed ^ 0x6c696e6b65642d67ull;
    LinkProgramOptions options[BUSTER_LINK_TEST_ALL_PROGRAMS];
    const char* names[BUSTER_LINK_TEST_ALL_PROGRAMS];
    const char* directories[BUSTER_LINK_TEST_ALL_PROGRAMS];
    link_test_all_options(options, names, directories, seed, BUSTER_OBJECT_TEST_FUNCTIONS);
    // The defaults and many units: static programs without libc.
    static const uint32_t sets[] = { 0, 2 };
    uint32_t linked = 0;
    for (uint32_t set = 0; set < BUSTER_ARRAY_LENGTH(sets); set += 1)
    {
        const uint32_t i = sets[set];
        LinkProgram program;
        const bool generated = link_program_generate(&program, options[i]);
        object_test_expect(&tally, generated, names[i], i);
        const uint64_t length = arena->length;
        ObjectUnits units;
        ObjectSymbols symbols;
        const bool prepared = generated && object_prepare(&units, &program.link, options[i].units, arena) && object_map_symbols(&symbols, &program.link, arena);
        const uint32_t count = prepared ? units.count : 0;
        ObjectTestDebug* debugs = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, ObjectTestDebug, count) : 0;
        // Every unit's DWARF as ObjectOptions.debug's entries: each object takes its unit's.
        ObjectDebug* entries = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, ObjectDebug, count) : 0;
        char* paths = prepared ? arena_push(arena, (uint64_t)count * BUSTER_OBJECT_TEST_PATH_BYTES, 1) : 0;
        char** arguments = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)count + 8) : 0;
        bool ready = debugs != 0 && entries != 0 && paths != 0 && arguments != 0;
        object_test_expect(&tally, ready, "the objects' DWARF and paths", i);
        for (uint32_t unit = 0; ready && unit < count; unit += 1)
        {
            ready = object_test_debug_make(&debugs[unit], &program.link, &units, unit, 1 + unit % 2, &random, arena);
            entries[unit] = debugs[unit].descriptor;
            object_test_expect(&tally, ready, "a unit's DWARF", unit);
        }
        char output_path[BUSTER_OBJECT_TEST_PATH_BYTES];
        char error_path[BUSTER_OBJECT_TEST_PATH_BYTES];
        char program_path[BUSTER_OBJECT_TEST_PATH_BYTES];
        snprintf(output_path, sizeof(output_path), "%s/debug-%s.out", directory, directories[i]);
        snprintf(error_path, sizeof(error_path), "%s/debug-%s.err", directory, directories[i]);
        snprintf(program_path, sizeof(program_path), "%s/debug-%s", directory, directories[i]);
        for (uint32_t sections = 0; ready && sections < 2; sections += 1)
        {
            // Each unit's object, with its DWARF, listed by both tools.
            for (uint32_t unit = 0; ready && unit < count; unit += 1)
            {
                char* path = paths + (uint64_t)unit * BUSTER_OBJECT_TEST_PATH_BYTES;
                snprintf(path, BUSTER_OBJECT_TEST_PATH_BYTES, "%s/debug-%s%s_u%u.o", directory, directories[i], sections == 1 ? "-sections" : "", unit);
                const ObjectOptions object_options = {
                    .function_sections = sections == 1,
                    .debug = entries,
                    .debug_count = count,
                };
                const uint64_t before = arena->length;
                char source[32];
                const Object object = object_build(&program.link, &units, &symbols, unit, object_options, object_test_source_name(source, sizeof(source), unit), arena);
                ready = object.error == OBJECT_ERROR_NONE && object_write_file(path, object.bytes, object.size) == OBJECT_ERROR_NONE;
                object_test_expect(&tally, ready, "writing an object with its DWARF", unit);
                ObjectView view;
                char error[512];
                if (ready && object_view_open(&view, object.bytes, object.size, false, error, sizeof(error)))
                {
                    object_test_listing(&tally, "readelf", &view, path, output_path, error_path, arena);
                    object_test_listing(&tally, "llvm-readobj", &view, path, output_path, error_path, arena);
                }
                arena_pop_to(arena, before);
            }
            // Each linker's program: it runs, its DWARF verifies, and it is the objects'.
            for (uint32_t l = 0; ready && l < BUSTER_ARRAY_LENGTH(linkers); l += 1)
            {
                uint32_t argument = 0;
                arguments[argument++] = (char*)linkers[l];
                arguments[argument++] = "-static";
                arguments[argument++] = "-e";
                arguments[argument++] = "_start";
                arguments[argument++] = "-o";
                arguments[argument++] = program_path;
                for (uint32_t unit = 0; unit < count; unit += 1)
                {
                    arguments[argument++] = paths + (uint64_t)unit * BUSTER_OBJECT_TEST_PATH_BYTES;
                }
                arguments[argument] = 0;
                unlink(program_path);
                const uint64_t before = arena->length;
                const int status = object_test_run(arguments, output_path, error_path);
                const String8 errors = object_test_read_file(arena, error_path);
                const bool clean = status == 0 && errors.pointer != 0 && errors.length == 0;
                if (!clean)
                {
                    printf("  %s with DWARF: %s exited with %d: %.*s\n", directories[i], linkers[l], status, errors.pointer != 0 ? (int)(errors.length < 500 ? errors.length : 500) : 0,
                           errors.pointer != 0 ? errors.pointer : "");
                }
                object_test_expect(&tally, clean, "a link of objects with DWARF without errors or warnings", l);
                char* run[] = { program_path, 0 };
                const int run_status = clean ? object_test_run(run, output_path, error_path) : -1;
                const String8 output = clean ? object_test_read_file(arena, output_path) : (String8){ 0 };
                object_test_expect(&tally, run_status == 0 && output.pointer != 0 && output.length == BUSTER_LINK_PROGRAM_OUTPUT_BYTES &&
                                               memcmp(output.pointer, program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES) == 0,
                                   "the program runs and prints what it should", l);
                char* verify[] = { "llvm-dwarfdump", "--verify", program_path, 0 };
                const int verified = clean ? object_test_run(verify, output_path, error_path) : -1;
                const String8 report = clean ? object_test_read_file(arena, output_path) : (String8){ 0 };
                const bool no_errors = verified == 0 && report.pointer != 0 && strstr(report.pointer, "No errors.") != 0;
                if (clean && !no_errors)
                {
                    printf("  %s linked by %s: llvm-dwarfdump --verify: %.*s\n", directories[i], linkers[l], report.pointer != 0 ? (int)(report.length < 600 ? report.length : 600) : 0,
                           report.pointer != 0 ? report.pointer : "");
                }
                object_test_expect(&tally, no_errors, "llvm-dwarfdump --verify finds no error", l);
                const String8 file = clean ? object_test_read_file(arena, program_path) : (String8){ 0 };
                object_test_expect(&tally, file.pointer != 0 && object_test_debug_program(file, &program.link, debugs, count),
                                   "the program's DWARF is its objects', relocated where the linker put them", l);
                linked += clean ? 1 : 0;
                arena_pop_to(arena, before);
            }
            for (uint32_t unit = 0; !keep && unit < count; unit += 1)
            {
                unlink(paths + (uint64_t)unit * BUSTER_OBJECT_TEST_PATH_BYTES);
            }
        }
        if (!keep)
        {
            unlink(program_path);
            unlink(output_path);
            unlink(error_path);
        }
        arena_pop_to(arena, length);
        link_program_release(&program);
    }
    printf("  objects with DWARF of %u sets linked %u times, by GNU ld, lld and mold\n", (uint32_t)BUSTER_ARRAY_LENGTH(sets), linked);
    return object_test_report(&tally);
}

// The lanes (see the file's comment), in directories of their own under `directory`.
static bool object_test_debug_lanes(uint64_t seed, const char* directory, bool keep, Arena* arena)
{
    ObjectTestTally tally = {
        .check = "debug lanes",
    };
    static const uint32_t lane_counts[] = { 1, 2, 3, 8 };
    uint64_t random = seed ^ 0x6c616e65732d6762ull;
    LinkProgramOptions options = link_program_default_options();
    options.seed = seed;
    options.units = 24;
    options.functions = 600;
    options.globals = 96;
    LinkProgram program;
    const bool generated = link_program_generate(&program, options);
    object_test_expect(&tally, generated, "link_program_generate", 0);
    const uint64_t length = arena->length;
    ObjectUnits units;
    ObjectSymbols symbols;
    const bool prepared = generated && object_prepare(&units, &program.link, options.units + 1, arena) && object_map_symbols(&symbols, &program.link, arena);
    object_test_expect(&tally, !generated || prepared, "object_prepare", 0);
    const uint32_t count = prepared ? units.count : 0;
    ObjectTestDebug* debugs = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, ObjectTestDebug, count) : 0;
    ObjectDebug* entries = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, ObjectDebug, count) : 0;
    char** paths = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, count) : 0;
    char* path_bytes = prepared ? arena_push(arena, (uint64_t)count * BUSTER_OBJECT_TEST_PATH_BYTES, 1) : 0;
    String8* source_names = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, String8, count) : 0;
    char* source_bytes = prepared ? arena_push(arena, (uint64_t)count * 32, 1) : 0;
    ObjectError* errors = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, ObjectError, count) : 0;
    Object* baseline = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, Object, 2 * (uint64_t)count) : 0;
    bool ready = debugs != 0 && entries != 0 && paths != 0 && path_bytes != 0 && source_names != 0 && source_bytes != 0 && errors != 0 && baseline != 0;
    object_test_expect(&tally, !prepared || ready, "room for the entries and the paths", 0);
    uint32_t described = 0;
    for (uint32_t unit = 0; ready && unit < count; unit += 1)
    {
        entries[unit] = (ObjectDebug){ 0 };
        if (unit % 4 != 3)
        {
            ready = object_test_debug_make(&debugs[unit], &program.link, &units, unit, 1 + unit % 2, &random, arena);
            entries[unit] = debugs[unit].descriptor;
            described += 1;
            object_test_expect(&tally, ready, "a unit's DWARF", unit);
        }
        source_names[unit] = object_test_source_name(source_bytes + (uint64_t)unit * 32, 32, unit);
        paths[unit] = path_bytes + (uint64_t)unit * BUSTER_OBJECT_TEST_PATH_BYTES;
    }
    // Each unit's object with the entries, checked and kept for the lanes' files; and without DWARF, the same bytes
    // exactly where its entry is empty.
    for (uint32_t mode = 0; ready && mode < 2; mode += 1)
    {
        for (uint32_t unit = 0; ready && unit < count; unit += 1)
        {
            const ObjectOptions with = {
                .function_sections = mode != 0,
                .debug = entries,
                .debug_count = count,
            };
            ObjectTestCounts counts = { 0 };
            object_test_unit(&tally, &program.link, &units, &symbols, unit, with, &counts, arena);
            const Object built = object_build(&program.link, &units, &symbols, unit, with, source_names[unit], arena);
            baseline[mode * count + unit] = built;
            const uint64_t before = arena->length;
            const Object without = object_build(&program.link, &units, &symbols, unit, (ObjectOptions){ .function_sections = mode != 0 }, source_names[unit], arena);
            ready = built.error == OBJECT_ERROR_NONE && without.error == OBJECT_ERROR_NONE;
            const bool same = ready && built.size == without.size && memcmp(built.bytes, without.bytes, built.size) == 0;
            object_test_expect(&tally, ready && same == (unit % 4 == 3), "DWARF in the object exactly where its unit's entry has it", unit);
            arena_pop_to(arena, before);
        }
    }
    // Every lane count, in both modes: each file object_build's object.
    for (uint32_t c = 0; ready && c < 2 * BUSTER_ARRAY_LENGTH(lane_counts); c += 1)
    {
        const uint32_t lanes = lane_counts[c / 2];
        char lanes_directory[BUSTER_OBJECT_TEST_PATH_BYTES];
        snprintf(lanes_directory, sizeof(lanes_directory), "%s/debug-lanes%u-mode%u", directory, lanes, c % 2);
        object_test_expect(&tally, mkdir(lanes_directory, 0755) == 0, "mkdir", c);
        for (uint32_t unit = 0; unit < count; unit += 1)
        {
            snprintf(paths[unit], BUSTER_OBJECT_TEST_PATH_BYTES, "%s/u%u.o", lanes_directory, unit);
        }
        ObjectTestLanes test = {
            .link = &program.link,
            .units = &units,
            .options = {
                .function_sections = c % 2 == 1,
                .debug = entries,
                .debug_count = count,
            },
            .paths = (const char* const*)paths,
            .source_names = source_names,
            .errors = errors,
        };
        bool written = lane_run(lanes, object_test_lanes_main, &test, 0);
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            written = written && test.written[lane];
        }
        object_test_expect(&tally, written, "object_write_units with each unit's DWARF", lanes);
        for (uint32_t unit = 0; written && unit < count; unit += 1)
        {
            const uint64_t before = arena->length;
            const String8 file = object_test_read_file(arena, paths[unit]);
            const Object expected = baseline[(c % 2) * count + unit];
            object_test_expect(&tally, file.pointer != 0 && file.length == expected.size && memcmp(file.pointer, expected.bytes, expected.size) == 0,
                               "each unit's file object_build's object, with its own DWARF", (uint64_t)lanes * 1000 + unit);
            arena_pop_to(arena, before);
        }
        for (uint32_t unit = 0; !keep && unit < count; unit += 1)
        {
            unlink(paths[unit]);
        }
        if (!keep)
        {
            rmdir(lanes_directory);
        }
    }
    // Entries one fewer than the units: every unit's object refused, by object_build and by object_write_units.
    if (ready)
    {
        const ObjectOptions fewer = {
            .debug = entries,
            .debug_count = count - 1,
        };
        for (uint32_t unit = 0; unit < count; unit += 1)
        {
            const uint64_t before = arena->length;
            const Object refused = object_build(&program.link, &units, &symbols, unit, fewer, source_names[unit], arena);
            object_test_expect(&tally, refused.error == OBJECT_ERROR_DEBUG && refused.bytes == 0 && arena->length == before, "entries one fewer than the units", unit);
        }
        char lanes_directory[BUSTER_OBJECT_TEST_PATH_BYTES];
        snprintf(lanes_directory, sizeof(lanes_directory), "%s/debug-lanes-fewer", directory);
        object_test_expect(&tally, mkdir(lanes_directory, 0755) == 0, "mkdir", 0);
        for (uint32_t unit = 0; unit < count; unit += 1)
        {
            snprintf(paths[unit], BUSTER_OBJECT_TEST_PATH_BYTES, "%s/u%u.o", lanes_directory, unit);
            errors[unit] = OBJECT_ERROR_NONE;
        }
        ObjectTestLanes test = {
            .link = &program.link,
            .units = &units,
            .options = fewer,
            .paths = (const char* const*)paths,
            .source_names = source_names,
            .errors = errors,
        };
        bool refused = lane_run(3, object_test_lanes_main, &test, 0);
        for (uint32_t lane = 0; lane < 3; lane += 1)
        {
            refused = refused && !test.written[lane];
        }
        for (uint32_t unit = 0; unit < count; unit += 1)
        {
            refused = refused && errors[unit] == OBJECT_ERROR_DEBUG && access(paths[unit], F_OK) != 0;
        }
        object_test_expect(&tally, refused, "object_write_units with entries one fewer than the units writes none", 0);
        rmdir(lanes_directory);
    }
    printf("  %u units' objects, %u with DWARF of their own, by 1, 2, 3 and 8 lanes\n", count, described);
    arena_pop_to(arena, length);
    if (generated)
    {
        link_program_release(&program);
    }
    return object_test_report(&tally);
}
