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
//     R_X86_64_DTPOFF32 or _64) is the writer's, by the link's symbol (ExecutableDebugSections' columns);
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
// own range, which the end compacts in their order (foreign_debug_finish). A relocation section's relocations go 8 at a
// time by the vector path (foreign_debug_relocate_vector), which leaves a batch with a rare relocation to the exact
// path (foreign_debug_relocate), its twin, which Foreign's exact path takes for all of them. On the lane path the lanes take each pass's
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

// A relocation's reference kind (EXECUTABLE_DEBUG_ABS64 and the others) and its field's width, or false for a type
// debug sections don't take.
static bool foreign_debug_type(uint32_t type, uint8_t* kind, uint32_t* width)
{
    const bool known = type == R_X86_64_64 || type == R_X86_64_32 || type == R_X86_64_DTPOFF32 || type == R_X86_64_DTPOFF64;
    *kind = type == R_X86_64_64 ? EXECUTABLE_DEBUG_ABS64 : type == R_X86_64_32 ? EXECUTABLE_DEBUG_ABS32 : type == R_X86_64_DTPOFF32 ? EXECUTABLE_DEBUG_DTPOFF32 :
            EXECUTABLE_DEBUG_DTPOFF64;
    *width = type == R_X86_64_64 || type == R_X86_64_DTPOFF64 ? 8 : 4;
    return known;
}

// The records of the references the writer resolves (ExecutableDebugSections' columns), writable, from a relocation
// section's first.
typedef struct ForeignDebugRecords ForeignDebugRecords;
struct ForeignDebugRecords
{
    uint32_t* offsets;
    int64_t* addends;
    SymbolId* targets;
    uint8_t* section_kinds;
    uint8_t* kinds;
};

// What each of an object's symbols means to its debug relocations, which the vector path reads by one gather where it
// would otherwise read the symbol, its section's place and the link's symbol: the class in the top two bits and a value
// in the low 48 (foreign_debug_resolve).
#define BUSTER_FOREIGN_DEBUG_TOMBSTONE 0ull
#define BUSTER_FOREIGN_DEBUG_SECTIONED 1ull
#define BUSTER_FOREIGN_DEBUG_LINKED 2ull
#define BUSTER_FOREIGN_DEBUG_LEFT 3ull
#define BUSTER_FOREIGN_DEBUG_VALUE_MASK ((1ull << 48) - 1)

// Each of `input`'s symbols, as its debug relocations take it, into `resolved`: SECTIONED with its place in its output
// section (`bases` of its section, plus its value) for a symbol in a kept debug section; LINKED with the link's symbol
// (`object`'s map) for another; TOMBSTONE for one the link has none of; and LEFT, which leaves a relocation of it to the
// exact path, for a symbol whose section is through SHN_XINDEX or whose place doesn't fit 48 bits. Symbol 0 is
// TOMBSTONE, but the relocations of no symbol don't read it.
static void foreign_debug_resolve(const ForeignObject* input, const ImportObject* object, const uint64_t* bases, uint64_t* resolved)
{
    resolved[0] = BUSTER_FOREIGN_DEBUG_TOMBSTONE << 62;
    for (uint32_t index = 1; index < input->symbols.count; index += 1)
    {
        const Elf64_Sym symbol = import_symbol(&input->symbols, index);
        const bool direct = symbol.st_shndx != SHN_XINDEX;
        const bool sectioned = direct && symbol.st_shndx < input->elf.section_count && bases[symbol.st_shndx] != UINT64_MAX;
        const uint64_t place = sectioned ? bases[symbol.st_shndx] + symbol.st_value : 0;
        const SymbolId target = index < object->symbol_count ? object->symbols[index] : 0;
        const uint64_t class = !direct || (sectioned && place > BUSTER_FOREIGN_DEBUG_VALUE_MASK) ? BUSTER_FOREIGN_DEBUG_LEFT :
                               sectioned                                                       ? BUSTER_FOREIGN_DEBUG_SECTIONED :
                               target != 0                                                     ? BUSTER_FOREIGN_DEBUG_LINKED :
                                                                                                 BUSTER_FOREIGN_DEBUG_TOMBSTONE;
        const uint64_t value = class == BUSTER_FOREIGN_DEBUG_SECTIONED ? place : class == BUSTER_FOREIGN_DEBUG_LINKED ? target : 0;
        resolved[index] = class << 62 | value;
    }
}

// A relocation section's relocations as both paths take them: object `id`'s relocation section `relocation_section`,
// whose target's bytes are at `bytes` (`size` of them) in output kind `kind`'s section at `base`; `bases` holds each of
// the object's sections' place in its kind's output section (UINT64_MAX for one that isn't a kept debug section),
// `resolved` its symbols as foreign_debug_resolve gives them (the vector path's), and the records go to `records` from
// its start.
typedef struct ForeignDebugSection ForeignDebugSection;
struct ForeignDebugSection
{
    const ForeignLink* output;
    const ForeignObject* input;
    uint32_t id;
    uint32_t relocation_section;
    uint32_t kind;
    uint64_t base;
    uint8_t* bytes;
    uint64_t size;
    const uint64_t* bases;
    const uint64_t* resolved;
    ForeignDebugRecords records;
};

// A relocation's failure, at relocation `i` of the section.
static ForeignLinkResult foreign_debug_failure(const ForeignDebugSection* section, ForeignLinkError error, ImportError import, uint64_t i, uint32_t type)
{
    const Elf64_Shdr header = import_section_header(&section->input->elf, section->relocation_section);
    ForeignLinkResult result = { .error = error, .object = section->id };
    result.import = import_fail(import, header.sh_offset + i * sizeof(Elf64_Rela));
    result.import.section = section->relocation_section;
    result.import.relocation = (uint32_t)i;
    result.import.type = type;
    return result;
}

// The exact path of a relocation section's relocations, `first` to `last`: each written now or recorded from record
// *count, as the file's comment says. Returns the first failure. The vector path's twin, and its path for a batch it
// leaves (foreign_debug_relocate_vector).
static ForeignLinkResult foreign_debug_relocate(const ForeignDebugSection* section, uint64_t first, uint64_t last, uint64_t* count)
{
    ForeignLinkResult result = { 0 };
    const ForeignObject* input = section->input;
    const Elf64_Shdr header = import_section_header(&input->elf, section->relocation_section);
    const String8 entries = import_section_bytes(&input->elf, &header);
    const uint32_t section_kind = EXECUTABLE_SECTION_DEBUG_INFO + section->kind;
    const uint64_t tombstone = section_kind == EXECUTABLE_SECTION_DEBUG_RANGES || section_kind == EXECUTABLE_SECTION_DEBUG_LOC ? 1 : 0;
    const ImportObject* object = &section->output->objects[section->id];
    for (uint64_t i = first; result.error == FOREIGN_LINK_ERROR_NONE && i < last; i += 1)
    {
        Elf64_Rela relocation;
        memcpy(&relocation, entries.pointer + i * sizeof(Elf64_Rela), sizeof(relocation));
        const uint32_t type = ELF64_R_TYPE(relocation.r_info);
        const uint32_t index = (uint32_t)ELF64_R_SYM(relocation.r_info);
        uint8_t relocation_kind = 0;
        uint32_t width = 0;
        const bool known = type == R_X86_64_NONE || foreign_debug_type(type, &relocation_kind, &width);
        const bool inside = type == R_X86_64_NONE || (relocation.r_offset <= section->size && width <= section->size - relocation.r_offset);
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
            result = foreign_debug_failure(section, !known ? FOREIGN_LINK_ERROR_UNSUPPORTED : FOREIGN_LINK_ERROR_IMPORT, !known ? IMPORT_ERROR_UNSUPPORTED : IMPORT_ERROR_BOUNDS, i,
                                           type);
        }
        else if (type != R_X86_64_NONE)
        {
            const bool module_offset = relocation_kind == EXECUTABLE_DEBUG_DTPOFF32 || relocation_kind == EXECUTABLE_DEBUG_DTPOFF64;
            const bool sectioned = index != 0 && target_section < input->elf.section_count && section->bases[target_section] != UINT64_MAX;
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
                value = section->bases[target_section] + symbol.st_value + (uint64_t)relocation.r_addend;
            }
            else if (target != 0)
            {
                section->records.offsets[*count] = (uint32_t)(section->base + relocation.r_offset);
                section->records.addends[*count] = relocation.r_addend;
                section->records.targets[*count] = target;
                section->records.section_kinds[*count] = (uint8_t)section->kind;
                section->records.kinds[*count] = relocation_kind;
                *count += 1;
                written = false;
            }
            const bool fits = !written || width == 8 || value <= UINT32_MAX;
            if (written && fits)
            {
                memcpy(section->bytes + relocation.r_offset, &value, width);
            }
            if (!fits || (sectioned && module_offset))
            {
                result = foreign_debug_failure(section, FOREIGN_LINK_ERROR_UNSUPPORTED, IMPORT_ERROR_UNSUPPORTED, i, type);
            }
        }
    }
    return result;
}

// The vector path of a relocation section's relocations (foreign_debug_relocate's), 8 at a time: the records
// de-interleaved into offsets, infos and addends by permutes, each symbol's meaning gathered from the object's table
// (foreign_debug_resolve), each value computed, written by scatters and recorded by compresses, in order. A batch takes
// this path when every relocation in it is a common one: a type DWARF uses (not R_X86_64_NONE), its field within the
// section, its symbol in the table and not LEFT, a value that fits its field, no thread-local offset into a debug
// section, and fields one after another without overlapping, as assemblers emit them; any other batch, and the last
// relocations short of a batch, go to the exact path, which reports a failure where the batch has one. Either way the
// same bytes and records come out.
static ForeignLinkResult foreign_debug_relocate_vector(const ForeignDebugSection* section, uint64_t* count)
{
    ForeignLinkResult result = { 0 };
    const ForeignObject* input = section->input;
    const Elf64_Shdr header = import_section_header(&input->elf, section->relocation_section);
    const String8 entries = import_section_bytes(&input->elf, &header);
    const uint64_t total = entries.length / sizeof(Elf64_Rela);
    const uint32_t section_kind = EXECUTABLE_SECTION_DEBUG_INFO + section->kind;
    const uint64_t tombstone = section_kind == EXECUTABLE_SECTION_DEBUG_RANGES || section_kind == EXECUTABLE_SECTION_DEBUG_LOC ? 1 : 0;
    // Lane r of each permute's result is relocation r's offset, info or addend: quadwords 3r, 3r + 1 and 3r + 2 of the
    // three loads.
    const __m512i offsets_first = _mm512_set_epi64(0, 0, 15, 12, 9, 6, 3, 0);
    const __m512i offsets_last = _mm512_set_epi64(13, 10, 5, 4, 3, 2, 1, 0);
    const __m512i infos_first = _mm512_set_epi64(0, 0, 0, 13, 10, 7, 4, 1);
    const __m512i infos_last = _mm512_set_epi64(14, 11, 8, 4, 3, 2, 1, 0);
    const __m512i addends_first = _mm512_set_epi64(0, 0, 0, 14, 11, 8, 5, 2);
    const __m512i addends_last = _mm512_set_epi64(15, 12, 9, 4, 3, 2, 1, 0);
    const __m512i next_lane = _mm512_set_epi64(7, 7, 6, 5, 4, 3, 2, 1);
    const __m512i size = _mm512_set1_epi64((int64_t)section->size);
    const __m512i base = _mm512_set1_epi64((int64_t)section->base);
    const __m512i value_mask = _mm512_set1_epi64((int64_t)BUSTER_FOREIGN_DEBUG_VALUE_MASK);
    uint64_t i = 0;
    // A section without symbols has no batch the vector path takes.
    for (; result.error == FOREIGN_LINK_ERROR_NONE && input->symbols.count != 0 && i + 8 <= total; i += 8)
    {
        const uint8_t* at = (const uint8_t*)entries.pointer + i * sizeof(Elf64_Rela);
        const __m512i first = _mm512_loadu_si512(at);
        const __m512i middle = _mm512_loadu_si512(at + 64);
        const __m512i last = _mm512_loadu_si512(at + 128);
        const __m512i offsets = _mm512_permutex2var_epi64(_mm512_permutex2var_epi64(first, offsets_first, middle), offsets_last, last);
        const __m512i infos = _mm512_permutex2var_epi64(_mm512_permutex2var_epi64(first, infos_first, middle), infos_last, last);
        const __m512i addends = _mm512_permutex2var_epi64(_mm512_permutex2var_epi64(first, addends_first, middle), addends_last, last);
        const __m512i types = _mm512_and_si512(infos, _mm512_set1_epi64(0xffffffff));
        const __m512i indices = _mm512_srli_epi64(infos, 32);
        const __mmask8 absolute64 = _mm512_cmpeq_epi64_mask(types, _mm512_set1_epi64(R_X86_64_64));
        const __mmask8 absolute32 = _mm512_cmpeq_epi64_mask(types, _mm512_set1_epi64(R_X86_64_32));
        const __mmask8 module64 = _mm512_cmpeq_epi64_mask(types, _mm512_set1_epi64(R_X86_64_DTPOFF64));
        const __mmask8 module32 = _mm512_cmpeq_epi64_mask(types, _mm512_set1_epi64(R_X86_64_DTPOFF32));
        const __mmask8 wide = absolute64 | module64;
        const __m512i widths = _mm512_mask_blend_epi64(wide, _mm512_set1_epi64(4), _mm512_set1_epi64(8));
        const __mmask8 inside = _mm512_cmple_epu64_mask(offsets, size) & _mm512_cmple_epu64_mask(widths, _mm512_sub_epi64(size, offsets));
        const __mmask8 symbolized = _mm512_cmplt_epu64_mask(indices, _mm512_set1_epi64(input->symbols.count));
        const __mmask8 ordered = _mm512_cmple_epu64_mask(_mm512_add_epi64(offsets, widths), _mm512_permutexvar_epi64(next_lane, offsets)) | 0x80;
        const __mmask8 named = _mm512_test_epi64_mask(indices, indices);
        bool take = (uint8_t)(absolute64 | absolute32 | module64 | module32) == 0xff && inside == 0xff && symbolized == 0xff && ordered == 0xff;
        const __m512i meanings = take ? _mm512_mask_i64gather_epi64(_mm512_setzero_si512(), named, indices, section->resolved, 8) : _mm512_setzero_si512();
        const __m512i classes = _mm512_srli_epi64(meanings, 62);
        const __m512i values = _mm512_and_si512(meanings, value_mask);
        const __mmask8 module = module64 | module32;
        const __mmask8 sectioned = _mm512_mask_cmpeq_epi64_mask(named, classes, _mm512_set1_epi64(BUSTER_FOREIGN_DEBUG_SECTIONED));
        const __mmask8 recorded = _mm512_mask_cmpeq_epi64_mask(named, classes, _mm512_set1_epi64(BUSTER_FOREIGN_DEBUG_LINKED));
        take = take && _mm512_mask_cmpeq_epi64_mask(named, classes, _mm512_set1_epi64(BUSTER_FOREIGN_DEBUG_LEFT)) == 0 && (sectioned & module) == 0;
        __m512i written = _mm512_set1_epi64((int64_t)tombstone);
        written = _mm512_mask_mov_epi64(written, (uint8_t)~named, addends);
        written = _mm512_mask_add_epi64(written, sectioned, values, addends);
        const __mmask8 writes = (uint8_t)~recorded;
        take = take && _mm512_mask_cmpgt_epu64_mask(writes & (uint8_t)~wide, written, _mm512_set1_epi64(0xffffffff)) == 0;
        if (!take)
        {
            result = foreign_debug_relocate(section, i, i + 8, count);
        }
        else
        {
            _mm512_mask_i64scatter_epi64(section->bytes, writes & wide, offsets, written, 1);
            _mm512_mask_i64scatter_epi32(section->bytes, writes & (uint8_t)~wide, offsets, _mm512_cvtepi64_epi32(written), 1);
            const uint32_t taken = (uint32_t)__builtin_popcount(recorded);
            const __mmask8 room = (__mmask8)((1u << taken) - 1);
            const __m512i kinds = _mm512_mask_blend_epi64(absolute32, _mm512_mask_blend_epi64(module32, _mm512_mask_blend_epi64(module64, _mm512_set1_epi64(EXECUTABLE_DEBUG_ABS64),
                                                                                                                              _mm512_set1_epi64(EXECUTABLE_DEBUG_DTPOFF64)),
                                                                                              _mm512_set1_epi64(EXECUTABLE_DEBUG_DTPOFF32)),
                                                          _mm512_set1_epi64(EXECUTABLE_DEBUG_ABS32));
            _mm256_mask_storeu_epi32(section->records.offsets + *count, room, _mm512_cvtepi64_epi32(_mm512_maskz_compress_epi64(recorded, _mm512_add_epi64(base, offsets))));
            _mm512_mask_storeu_epi64(section->records.addends + *count, room, _mm512_maskz_compress_epi64(recorded, addends));
            _mm256_mask_storeu_epi32(section->records.targets + *count, room, _mm512_cvtepi64_epi32(_mm512_maskz_compress_epi64(recorded, values)));
            _mm_mask_storeu_epi8(section->records.section_kinds + *count, room, _mm_set1_epi8((char)section->kind));
            _mm_mask_storeu_epi8(section->records.kinds + *count, room, _mm512_cvtepi64_epi8(_mm512_maskz_compress_epi64(recorded, kinds)));
            *count += taken;
        }
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && i < total)
    {
        result = foreign_debug_relocate(section, i, total, count);
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
    // section), and its symbols as its debug relocations take them (foreign_debug_resolve), for the objects with debug
    // sections.
    uint64_t** bases;
    uint64_t** resolved;
    uint8_t* bytes[BUSTER_EXECUTABLE_DEBUG_KINDS];
    uint64_t sizes[BUSTER_EXECUTABLE_DEBUG_KINDS];
    ForeignDebugRecords records;
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
static ForeignLinkResult foreign_debug_plan(ForeignDebugPlan* plan, const ForeignLink* output, const Foreign* foreign, const uint32_t* order, Arena* arena)
{
    ForeignLinkResult result = { 0 };
    *plan = (ForeignDebugPlan){ 0 };
    uint64_t relocation_capacity = 0;
    uint32_t contribution_capacity = 0;
    uint32_t relocation_section_capacity = 0;
    plan->bases = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t*, foreign->object_count);
    plan->resolved = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t*, foreign->object_count);
    result.error = plan->bases != 0 && plan->resolved != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
    for (uint32_t at = 0; result.error == FOREIGN_LINK_ERROR_NONE && at < foreign->object_count; at += 1)
    {
        const uint32_t id = order[at];
        const ForeignObject* input = &foreign->objects[id];
        plan->bases[id] = 0;
        plan->resolved[id] = 0;
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
        // The vector path's table of the object's symbols; the exact path reads the symbols themselves.
        if (plan->bases[id] != 0 && result.error == FOREIGN_LINK_ERROR_NONE && input->symbols.count != 0 && foreign->path == FOREIGN_PATH_VECTOR)
        {
            plan->resolved[id] = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, input->symbols.count);
            result.error = plan->resolved[id] != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
            if (plan->resolved[id] != 0)
            {
                foreign_debug_resolve(input, &output->objects[id], plan->bases[id], plan->resolved[id]);
            }
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
    const bool records = result.error == FOREIGN_LINK_ERROR_NONE && relocation_capacity != 0;
    plan->records = (ForeignDebugRecords){
        .offsets = records ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, relocation_capacity) : 0,
        .addends = records ? BUSTER_ARENA_PUSH_ARRAY(arena, int64_t, relocation_capacity) : 0,
        .targets = records ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, relocation_capacity) : 0,
        .section_kinds = records ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, relocation_capacity) : 0,
        .kinds = records ? BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, relocation_capacity) : 0,
    };
    const bool recorded = relocation_capacity == 0 || (plan->records.offsets != 0 && plan->records.addends != 0 && plan->records.targets != 0 &&
                                                       plan->records.section_kinds != 0 && plan->records.kinds != 0);
    const bool allocated = plan->contributions != 0 && plan->relocations != 0 && plan->contribution_order != 0 && plan->relocation_order != 0 && costs != 0 &&
                           plan->contribution_results != 0 && plan->relocation_results != 0 && recorded;
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

// Relocation section `item`'s relocations, into its records' range: by the vector path, or by the exact path on
// Foreign's exact one (FOREIGN_PATH_EXACT).
static void foreign_debug_relocation_section(ForeignDebugPlan* plan, const ForeignLink* output, const Foreign* foreign, uint32_t item)
{
    ForeignDebugRelocations* relocations = &plan->relocations[item];
    const ForeignObject* input = &foreign->objects[relocations->object];
    const uint64_t first = relocations->first;
    const ForeignDebugSection section = {
        .output = output,
        .input = input,
        .id = relocations->object,
        .relocation_section = relocations->section,
        .kind = relocations->kind,
        .base = relocations->base,
        .bytes = plan->bytes[relocations->kind] + relocations->base,
        .size = relocations->size,
        .bases = plan->bases[relocations->object],
        .resolved = plan->resolved[relocations->object],
        .records = {
            .offsets = plan->records.offsets + first,
            .addends = plan->records.addends + first,
            .targets = plan->records.targets + first,
            .section_kinds = plan->records.section_kinds + first,
            .kinds = plan->records.kinds + first,
        },
    };
    const Elf64_Shdr header = import_section_header(&input->elf, relocations->section);
    uint64_t count = 0;
    plan->relocation_results[item] = foreign->path == FOREIGN_PATH_VECTOR ? foreign_debug_relocate_vector(&section, &count) :
                                                                            foreign_debug_relocate(&section, 0, header.sh_size / sizeof(Elf64_Rela), &count);
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
    const ForeignDebugRecords* records = &plan->records;
    for (uint32_t item = 0; item < plan->relocation_count; item += 1)
    {
        const ForeignDebugRelocations* relocations = &plan->relocations[item];
        const uint64_t first = relocations->first;
        const uint64_t taken = relocations->count;
        if (count != first && taken != 0)
        {
            memmove(records->offsets + count, records->offsets + first, taken * sizeof(*records->offsets));
            memmove(records->addends + count, records->addends + first, taken * sizeof(*records->addends));
            memmove(records->targets + count, records->targets + first, taken * sizeof(*records->targets));
            memmove(records->section_kinds + count, records->section_kinds + first, taken);
            memmove(records->kinds + count, records->kinds + first, taken);
        }
        count += taken;
    }
    for (uint32_t kind = 0; kind < BUSTER_EXECUTABLE_DEBUG_KINDS; kind += 1)
    {
        output->debug.sections[kind] = (String8){ .pointer = (char*)plan->bytes[kind], .length = plan->sizes[kind] };
    }
    output->debug.offsets = records->offsets;
    output->debug.addends = records->addends;
    output->debug.targets = records->targets;
    output->debug.section_kinds = records->section_kinds;
    output->debug.kinds = records->kinds;
    output->debug.relocation_count = count;
}

// The selected objects' debug sections into output->debug (see the file's comment), in the order of `order` (input
// order), from `arena`, on the calling thread. Returns the first failure.
static ForeignLinkResult foreign_debug_build(ForeignLink* output, const Foreign* foreign, const uint32_t* order, Arena* arena)
{
    ForeignDebugPlan plan;
    output->debug = (ExecutableDebugSections){ 0 };
    ForeignLinkResult result = foreign_debug_plan(&plan, output, foreign, order, arena);
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
