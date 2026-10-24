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
// A debug section of a discarded group goes with its group. A compressed one (SHF_COMPRESSED's ELFCOMPRESS_ZLIB, which
// GNU as writes for -g by default and GCC's and clang's -gz ask for) contributes its bytes decompressed (inflate.c), at
// the size and alignment of its compression header, which its relocations' offsets are into; one of another
// compression (-gz=zstd's) is refused, and so are relocation types DWARF doesn't use. The writer's patch and this pass make the bytes lld writes
// for the same inputs, but where lld merges .debug_str's and .debug_line_str's strings: their offsets differ, and the
// sections are larger.
//
// A plan, then two passes: the plan (foreign_debug_plan, on the calling thread or lane 0) places each debug section in
// its output section in input order, and lists the contributions and the relocation sections; the bytes pass copies or
// decompresses each contribution, and the relocations pass applies each relocation section's, each into its records'
// own range, which the end compacts in their order (foreign_debug_finish). On the lane path the lanes take each pass's
// items costliest first (a section of a program like SQLite takes milliseconds to decompress, and one object's
// sections are as many streams), with a sync between the passes; the calling thread takes them in order. Either way the
// same bytes and records come out, and the first failure in the items' order. foreign_link.c includes this file.

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

// A debug section's size and alignment as it contributes them, and its zlib stream when it is compressed: after an
// SHF_COMPRESSED section's Elf64_Chdr, of type ELFCOMPRESS_ZLIB. IMPORT_ERROR_UNSUPPORTED for another compression, and
// IMPORT_ERROR_FORMAT for a header the section doesn't hold.
static ImportError foreign_debug_extent(const ForeignObject* input, const Elf64_Shdr* header, uint64_t* size, uint64_t* alignment, String8* stream)
{
    const String8 contents = import_section_bytes(&input->elf, header);
    const bool compressed = (header->sh_flags & SHF_COMPRESSED) != 0;
    Elf64_Chdr compression = { 0 };
    const bool held = !compressed || contents.length >= sizeof(compression);
    if (compressed && held)
    {
        memcpy(&compression, contents.pointer, sizeof(compression));
    }
    const ImportError result = !held ? IMPORT_ERROR_FORMAT : compressed && compression.ch_type != ELFCOMPRESS_ZLIB ? IMPORT_ERROR_UNSUPPORTED : IMPORT_ERROR_NONE;
    const uint64_t given = compressed ? compression.ch_addralign : header->sh_addralign;
    *size = compressed ? compression.ch_size : header->sh_size;
    *alignment = given > 1 ? given : 1;
    *stream = compressed && result == IMPORT_ERROR_NONE ? (String8){ .pointer = contents.pointer + sizeof(compression), .length = contents.length - sizeof(compression) } :
                                                          (String8){ 0 };
    return result;
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

// The plan of the debug sections' build: each kept section's contribution, which the bytes pass copies or decompresses
// into its place, and each relocation section that relocates one, which the relocations pass applies into its records'
// own range of the relocations' room (as many as its entries), compacted in their order at the end; with the output
// sections and each item's cost (its bytes, or its entries), by which the lanes take the items, costliest first.
typedef struct ForeignDebugContribution ForeignDebugContribution;
struct ForeignDebugContribution
{
    uint32_t object;
    uint32_t section;
    uint32_t kind;
    uint64_t base;
    uint64_t size;
};

typedef struct ForeignDebugRelocations ForeignDebugRelocations;
struct ForeignDebugRelocations
{
    uint32_t object;
    uint32_t section;
    uint32_t target;
    uint32_t kind;
    uint64_t base;
    uint64_t size;
    uint64_t first;
    uint64_t count;
};

typedef struct ForeignDebugPlan ForeignDebugPlan;
struct ForeignDebugPlan
{
    ForeignDebugContribution* contributions;
    uint32_t contribution_count;
    ForeignDebugRelocations* relocations;
    uint32_t relocation_count;
    // The items of each pass, costliest first.
    uint32_t* contribution_order;
    uint32_t* relocation_order;
    // Each object's sections' places in their kinds' output sections (UINT64_MAX for one that isn't a kept debug
    // section), for the objects with debug sections.
    uint64_t** bases;
    uint8_t* bytes[BUSTER_EXECUTABLE_DEBUG_KINDS];
    uint64_t sizes[BUSTER_EXECUTABLE_DEBUG_KINDS];
    ExecutableDebugRelocation* records;
    // Each item's failure, which the first in the items' order is reported of.
    ForeignLinkResult* contribution_results;
    ForeignLinkResult* relocation_results;
};

// An item and its cost, which the passes' orders are sorted by: costliest first, then by the item.
typedef struct ForeignDebugCost ForeignDebugCost;
struct ForeignDebugCost
{
    uint64_t cost;
    uint32_t item;
};

static int foreign_debug_compare_costs(const void* a, const void* b)
{
    const ForeignDebugCost* left = a;
    const ForeignDebugCost* right = b;
    return left->cost != right->cost ? (left->cost < right->cost ? 1 : -1) : (left->item > right->item) - (left->item < right->item);
}

// An order of `count` items, costliest first, from their costs.
static void foreign_debug_order(uint32_t* order, ForeignDebugCost* costs, uint32_t count)
{
    qsort(costs, count, sizeof(ForeignDebugCost), foreign_debug_compare_costs);
    for (uint32_t i = 0; i < count; i += 1)
    {
        order[i] = costs[i].item;
    }
}

// The plan of the selected objects' debug sections (ForeignDebugPlan), in the order of `order` (input order), from
// `arena`: the sizing pass. Returns the first failure.
static ForeignLinkResult foreign_debug_plan(ForeignDebugPlan* plan, const Foreign* foreign, const uint32_t* order, Arena* arena)
{
    ForeignLinkResult result = { 0 };
    *plan = (ForeignDebugPlan){ 0 };
    uint64_t relocation_capacity = 0;
    uint32_t contribution_capacity = 0;
    uint32_t relocation_section_capacity = 0;
    plan->bases = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t*, foreign->object_count);
    result.error = plan->bases != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
    for (uint32_t at = 0; result.error == FOREIGN_LINK_ERROR_NONE && at < foreign->object_count; at += 1)
    {
        const uint32_t id = order[at];
        const ForeignObject* input = &foreign->objects[id];
        plan->bases[id] = 0;
        for (uint32_t section = 1; id != 0 && input->library == 0 && result.error == FOREIGN_LINK_ERROR_NONE && section < input->elf.section_count; section += 1)
        {
            uint32_t kind = 0;
            if (foreign_debug_section(input, section, &kind))
            {
                const Elf64_Shdr header = import_section_header(&input->elf, section);
                if (plan->bases[id] == 0)
                {
                    plan->bases[id] = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, input->elf.section_count);
                    result.error = plan->bases[id] != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
                    for (uint32_t s = 0; plan->bases[id] != 0 && s < input->elf.section_count; s += 1)
                    {
                        plan->bases[id][s] = UINT64_MAX;
                    }
                }
                uint64_t size = 0;
                uint64_t alignment = 0;
                String8 stream = { 0 };
                const ImportError readable = foreign_debug_extent(input, &header, &size, &alignment, &stream);
                const bool aligned = (alignment & (alignment - 1)) == 0 && alignment <= 4096;
                if (result.error == FOREIGN_LINK_ERROR_NONE && (readable != IMPORT_ERROR_NONE || !aligned))
                {
                    result.error = readable != IMPORT_ERROR_NONE ? FOREIGN_LINK_ERROR_COMPRESSION : FOREIGN_LINK_ERROR_UNSUPPORTED;
                    result.object = id;
                    result.import = import_fail(readable != IMPORT_ERROR_NONE ? readable : IMPORT_ERROR_UNSUPPORTED, input->elf.section_offset + (uint64_t)section * sizeof(Elf64_Shdr));
                    result.import.section = section;
                    result.import.type = header.sh_type;
                }
                else if (result.error == FOREIGN_LINK_ERROR_NONE)
                {
                    // Past DWARF32's reach, the output section's size check below refuses it.
                    const uint64_t place = (plan->sizes[kind] + alignment - 1) & ~(alignment - 1);
                    plan->bases[id][section] = place;
                    plan->sizes[kind] = size < 0xfffffff0u ? place + size : 0xfffffff0u;
                    contribution_capacity += 1;
                }
            }
        }
        for (uint32_t r = 0; plan->bases[id] != 0 && result.error == FOREIGN_LINK_ERROR_NONE && r < input->relocation_count; r += 1)
        {
            const Elf64_Shdr header = import_section_header(&input->elf, input->relocations[r]);
            const bool relocates = header.sh_info < input->elf.section_count && plan->bases[id][header.sh_info] != UINT64_MAX;
            relocation_capacity += relocates ? header.sh_size / sizeof(Elf64_Rela) : 0;
            relocation_section_capacity += relocates ? 1 : 0;
        }
    }
    for (uint32_t kind = 0; result.error == FOREIGN_LINK_ERROR_NONE && kind < BUSTER_EXECUTABLE_DEBUG_KINDS; kind += 1)
    {
        // DWARF32's offsets reach the first 4 GB of a section.
        result.error = plan->sizes[kind] >= 0xfffffff0u ? FOREIGN_LINK_ERROR_CAPACITY : FOREIGN_LINK_ERROR_NONE;
        plan->bytes[kind] = result.error == FOREIGN_LINK_ERROR_NONE && plan->sizes[kind] != 0 ? arena_push(arena, plan->sizes[kind], 8) : 0;
        result.error = result.error == FOREIGN_LINK_ERROR_NONE && plan->sizes[kind] != 0 && plan->bytes[kind] == 0 ? FOREIGN_LINK_ERROR_MEMORY : result.error;
        if (plan->bytes[kind] != 0)
        {
            // The padding between contributions.
            memset(plan->bytes[kind], 0, plan->sizes[kind]);
        }
    }
    const uint64_t items = (uint64_t)contribution_capacity + relocation_section_capacity + 1;
    plan->contributions = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, ForeignDebugContribution, (uint64_t)contribution_capacity + 1) : 0;
    plan->relocations = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, ForeignDebugRelocations, (uint64_t)relocation_section_capacity + 1) : 0;
    plan->contribution_order = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)contribution_capacity + 1) : 0;
    plan->relocation_order = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)relocation_section_capacity + 1) : 0;
    ForeignDebugCost* costs = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, ForeignDebugCost, items) : 0;
    plan->contribution_results = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, ForeignLinkResult, (uint64_t)contribution_capacity + 1) : 0;
    plan->relocation_results = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, ForeignLinkResult, (uint64_t)relocation_section_capacity + 1) : 0;
    plan->records = result.error == FOREIGN_LINK_ERROR_NONE && relocation_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableDebugRelocation, relocation_capacity) : 0;
    const bool allocated = plan->contributions != 0 && plan->relocations != 0 && plan->contribution_order != 0 && plan->relocation_order != 0 && costs != 0 &&
                           plan->contribution_results != 0 && plan->relocation_results != 0 && (relocation_capacity == 0 || plan->records != 0);
    result.error = result.error == FOREIGN_LINK_ERROR_NONE && !allocated ? FOREIGN_LINK_ERROR_MEMORY : result.error;
    // The items, in the objects' order.
    uint64_t first = 0;
    for (uint32_t at = 0; result.error == FOREIGN_LINK_ERROR_NONE && at < foreign->object_count; at += 1)
    {
        const uint32_t id = order[at];
        const ForeignObject* input = &foreign->objects[id];
        for (uint32_t section = 1; plan->bases[id] != 0 && section < input->elf.section_count; section += 1)
        {
            uint32_t kind = 0;
            if (plan->bases[id][section] != UINT64_MAX && foreign_debug_section(input, section, &kind))
            {
                const Elf64_Shdr header = import_section_header(&input->elf, section);
                uint64_t size = 0;
                uint64_t alignment = 0;
                String8 stream = { 0 };
                foreign_debug_extent(input, &header, &size, &alignment, &stream);
                const uint32_t item = plan->contribution_count;
                plan->contributions[item] = (ForeignDebugContribution){ .object = id, .section = section, .kind = kind, .base = plan->bases[id][section], .size = size };
                // Decompressing costs about four times copying.
                costs[item] = (ForeignDebugCost){ .cost = (header.sh_flags & SHF_COMPRESSED) != 0 ? 4 * size : size, .item = item };
                plan->contribution_results[item] = (ForeignLinkResult){ 0 };
                plan->contribution_count += 1;
            }
        }
        for (uint32_t r = 0; plan->bases[id] != 0 && r < input->relocation_count; r += 1)
        {
            const Elf64_Shdr header = import_section_header(&input->elf, input->relocations[r]);
            uint32_t kind = 0;
            if (header.sh_info < input->elf.section_count && plan->bases[id][header.sh_info] != UINT64_MAX && foreign_debug_section(input, header.sh_info, &kind))
            {
                const Elf64_Shdr target = import_section_header(&input->elf, header.sh_info);
                uint64_t size = 0;
                uint64_t alignment = 0;
                String8 stream = { 0 };
                foreign_debug_extent(input, &target, &size, &alignment, &stream);
                const uint32_t item = plan->relocation_count;
                const uint64_t entries = header.sh_size / sizeof(Elf64_Rela);
                plan->relocations[item] = (ForeignDebugRelocations){
                    .object = id,
                    .section = input->relocations[r],
                    .target = header.sh_info,
                    .kind = kind,
                    .base = plan->bases[id][header.sh_info],
                    .size = size,
                    .first = first,
                };
                costs[contribution_capacity + item] = (ForeignDebugCost){ .cost = entries, .item = item };
                plan->relocation_results[item] = (ForeignLinkResult){ 0 };
                plan->relocation_count += 1;
                first += entries;
            }
        }
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        foreign_debug_order(plan->contribution_order, costs, plan->contribution_count);
        foreign_debug_order(plan->relocation_order, costs + contribution_capacity, plan->relocation_count);
    }
    return result;
}

// Contribution `item`'s bytes into its place: copied, or decompressed.
static void foreign_debug_contribute(ForeignDebugPlan* plan, const Foreign* foreign, uint32_t item)
{
    const ForeignDebugContribution* contribution = &plan->contributions[item];
    const ForeignObject* input = &foreign->objects[contribution->object];
    const Elf64_Shdr header = import_section_header(&input->elf, contribution->section);
    uint64_t size = 0;
    uint64_t alignment = 0;
    String8 stream = { 0 };
    foreign_debug_extent(input, &header, &size, &alignment, &stream);
    uint8_t* place = plan->bytes[contribution->kind] + contribution->base;
    if ((header.sh_flags & SHF_COMPRESSED) == 0)
    {
        const String8 contents = import_section_bytes(&input->elf, &header);
        memcpy(place, contents.pointer, contents.length);
    }
    else if (inflate_zlib((const uint8_t*)stream.pointer, stream.length, place, size) != INFLATE_ERROR_NONE)
    {
        ForeignLinkResult* result = &plan->contribution_results[item];
        result->error = FOREIGN_LINK_ERROR_COMPRESSION;
        result->object = contribution->object;
        result->import = import_fail(IMPORT_ERROR_FORMAT, header.sh_offset);
        result->import.section = contribution->section;
        result->import.type = header.sh_type;
    }
}

// Relocation section `item`'s relocations, into its records' range.
static void foreign_debug_relocation_section(ForeignDebugPlan* plan, const ForeignLink* output, const Foreign* foreign, uint32_t item)
{
    ForeignDebugRelocations* relocations = &plan->relocations[item];
    const ForeignObject* input = &foreign->objects[relocations->object];
    uint64_t count = 0;
    plan->relocation_results[item] = foreign_debug_relocate(output, input, relocations->object, relocations->section, relocations->kind, relocations->base,
                                                            plan->bytes[relocations->kind] + relocations->base, relocations->size, plan->bases[relocations->object],
                                                            plan->records + relocations->first, &count);
    relocations->count = count;
}

// The first of a pass's failures, in its items' order.
static ForeignLinkResult foreign_debug_first_failure(const ForeignLinkResult* results, uint32_t count)
{
    ForeignLinkResult result = { 0 };
    for (uint32_t i = 0; result.error == FOREIGN_LINK_ERROR_NONE && i < count; i += 1)
    {
        result = results[i];
    }
    return result;
}

// The build's output: the relocations' records compacted, each relocation section's after the one before, and the
// sections.
static void foreign_debug_finish(ForeignDebugPlan* plan, ForeignLink* output)
{
    uint64_t count = 0;
    for (uint32_t item = 0; item < plan->relocation_count; item += 1)
    {
        const ForeignDebugRelocations* relocations = &plan->relocations[item];
        if (count != relocations->first && relocations->count != 0)
        {
            memmove(plan->records + count, plan->records + relocations->first, relocations->count * sizeof(ExecutableDebugRelocation));
        }
        count += relocations->count;
    }
    for (uint32_t kind = 0; kind < BUSTER_EXECUTABLE_DEBUG_KINDS; kind += 1)
    {
        output->debug.sections[kind] = (String8){ .pointer = (char*)plan->bytes[kind], .length = plan->sizes[kind] };
    }
    output->debug.relocations = plan->records;
    output->debug.relocation_count = count;
}

// The selected objects' debug sections into output->debug (see the file's comment), in the order of `order` (input
// order), from `arena`, on the calling thread. Returns the first failure.
static ForeignLinkResult foreign_debug_build(ForeignLink* output, const Foreign* foreign, const uint32_t* order, Arena* arena)
{
    ForeignDebugPlan plan;
    output->debug = (ExecutableDebugSections){ 0 };
    ForeignLinkResult result = foreign_debug_plan(&plan, foreign, order, arena);
    for (uint32_t item = 0; result.error == FOREIGN_LINK_ERROR_NONE && item < plan.contribution_count; item += 1)
    {
        foreign_debug_contribute(&plan, foreign, item);
        result = plan.contribution_results[item];
    }
    for (uint32_t item = 0; result.error == FOREIGN_LINK_ERROR_NONE && item < plan.relocation_count; item += 1)
    {
        foreign_debug_relocation_section(&plan, output, foreign, item);
        result = plan.relocation_results[item];
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        foreign_debug_finish(&plan, output);
    }
    return result;
}

// A lane's part of the bytes pass: the contributions it takes, costliest first.
static void foreign_debug_contribute_lanes(ForeignDebugPlan* plan, const Foreign* foreign)
{
    for (uint64_t taken = lane_take(plan->contribution_count); taken < plan->contribution_count; taken = lane_take(plan->contribution_count))
    {
        foreign_debug_contribute(plan, foreign, plan->contribution_order[taken]);
    }
}

// A lane's part of the relocations pass.
static void foreign_debug_relocate_lanes(ForeignDebugPlan* plan, const ForeignLink* output, const Foreign* foreign)
{
    for (uint64_t taken = lane_take(plan->relocation_count); taken < plan->relocation_count; taken = lane_take(plan->relocation_count))
    {
        foreign_debug_relocation_section(plan, output, foreign, plan->relocation_order[taken]);
    }
}
