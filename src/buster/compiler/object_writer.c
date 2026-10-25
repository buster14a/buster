// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The object writer: relocatable ELF objects (ET_REL, what `cc -c` writes) from the link representation (link.c), for
// GNU ld, lld, mold, wild and this linker (docs/object-writer-and-linker.md, "The object writer (-c)").
//
// An object holds one unit (atoms.unit, a translation unit), in sections in this order:
//   - a section per class its atoms have (object_class_sections), in class order, or with function sections one per
//     TEXT atom, .text.<the name of its symbol at offset 0>, each followed by its .rela section if its atoms have
//     fixups. In a section, each atom starts at the next multiple of its alignment, in id order, and the gaps are int3
//     in code and zero elsewhere;
//   - .note.GNU-stack, without which linkers assume the code needs an executable stack, and .comment;
//   - .symtab, then .symtab_shndx when a section's index reaches SHN_LORESERVE (65,280), .strtab and .shstrtab.
// The symbols are, in order: the null symbol; an STT_FILE with the source's name; the section symbols relocations refer
// to; the unit's local symbols, those without a name (a string literal's) named .LC0, .LC1 and so on, as GCC names
// them; then, from .symtab's sh_info, its global and weak symbols, and those its fixups refer to that it doesn't
// define, undefined, without a type but TLS's as GNU as writes them. A local symbol needs a name: wild 0.10 never
// finishes a link with a relocation against one without a name in a string pool. Past SHN_LORESERVE sections, e_shnum
// and e_shstrndx move into section 0 and a symbol's section index into .symtab_shndx, as the gABI has it.
//
// A relocation per fixup has the type its kind names (object_relocation_types: CALL_GOT's GOTPCRELX and LOAD_GOT's
// REX_GOTPCRELX only on the instructions the psABI lets a linker rewrite, which link_add_code_fixups enforces), the
// fixup's addend (relative to the field) and the target. A local target is replaced by its section's symbol, with the
// target's offset added to the addend, where GNU as and LLVM do so: for every kind but the GOT, TLS and size kinds,
// whose linkers need the symbol itself, not for an IFUNC, whose linkers resolve it through the symbol (a call or an
// address through its section's symbol would reach the resolver), and not into a string pool with a PC-relative kind or
// an addend, since linkers split pools by string and the section's symbol would then name the wrong string. The fields
// are already zero: link_add_code_fixups zeroes code's, and data's are zero, as GNU as writes them. An object that
// defines an IFUNC is ELFOSABI_GNU, as GNU as and LLVM mark it, and others ELFOSABI_SYSV.
//
// An absolute symbol (BUSTER_LINK_SOURCE_ABSOLUTE) is in no unit, so no object could define it as the link does:
// object_prepare refuses a link with one, rather than writing objects that leave it undefined. The direct path's
// programs have none: only an imported object's (the static runtime's) do, which no object is written for.
//
// Count, place, fill (docs/object-writer-and-linker.md, "Lanes", rule 3):
//   - object_prepare, once per link, for the units its caller says the link has (an empty translation unit, without
//     atoms, still gets its object): the atoms grouped by unit and class, the symbols defined in atoms by unit (locals
//     first) with what each one is to its unit's object, each symbol's name's bytes and each unit's, each TEXT atom's
//     name and each unit's cost, by counting sorts.
//   - object_build, per unit: counts the sections (their sizes, alignments and fixups), the symbols and the string
//     tables' bytes; places the section and symbol indices, the names and the file's ranges, with checked arithmetic;
//     then fills each range once into one buffer of exactly the object's size. Nothing is allocated per symbol or
//     relocation, and no buffer grows. One pass over the unit's fixups finds every symbol they refer to, the undefined
//     ones in the order the fixups first refer to them, and what each one is to the object (ObjectSymbols: a local, a
//     global the unit defines or an undefined one, and its place among them), which the fill then reads for each
//     relocation rather than working it out again.
//   - object_write_file: one write, over the old file in place or into a new one.
// The record writers stay scalar: the document measured the AVX-512 transpose of Elf64_Rela no faster, since writing
// the records is bound by stores.
//
// Lanes: an object is a few microseconds of work, too little to split across lanes, so object_write_units gives each
// lane whole objects, parts of the units by cost taken from the counter (rule 1). Each object is built in its lane's
// scratch arena with its lane's ObjectSymbols, and object_build writes only its own unit's entries of the shared
// per-atom columns.
//
// main.c includes this file after link.c, whose representation it reads, and lane.c, whose arenas and lanes it uses.
// Layout, in this order:
//   - API: ObjectError, ObjectOptions, ObjectUnits, the symbol codes (BUSTER_OBJECT_SYMBOL_*), ObjectSymbols, Object
//     and the entry points' prototypes.
//   - Formats: object_class_sections, object_relocation_types and the names of the other sections.
//   - Preparing: object_prepare and object_map_symbols.
//   - Building: ObjectSection, ObjectBuilder, object_count_sections,
//     object_count_symbols, object_place, the fill (object_fill_sections, object_fill_relocations,
//     object_fill_symbols, object_fill_headers) and object_build.
//   - Writing: object_write_file and object_write_units.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <elf.h>

// OS-dependent includes
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// CPU architecture dependent includes
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// API

typedef enum ObjectError
{
    OBJECT_ERROR_NONE,
    // An arena can't hold the object, or what building it needs.
    OBJECT_ERROR_FULL,
    // The unit doesn't exist, or the object would have more sections, symbols or string bytes than ELF64's 32-bit
    // fields hold, or more bytes than 64 bits count.
    OBJECT_ERROR_RANGE,
    // A system call failed writing the file; errno says why.
    OBJECT_ERROR_FILE,
} ObjectError;

// Optional producer-owned descriptors, indexed by AtomId. Each described atom is already a complete section.
// Names are counted slices, not NUL-terminated strings. A non-null table must cover every atom including id 0.
typedef struct ObjectAtomSection ObjectAtomSection;
struct ObjectAtomSection
{
    String8 name;
    uint32_t type;
    uint64_t flags;
    uint64_t entry_size;
};

typedef struct ObjectOptions ObjectOptions;
struct ObjectOptions
{
    // A section per TEXT atom, as -ffunction-sections, rather than one .text.
    bool function_sections;
    const ObjectAtomSection* atom_sections;
    uint32_t atom_section_count;
    // Optional bytes indexed by SymbolId: discard a marked local unless a relocation needs its own symbol.
    const uint8_t* temporary_symbols;
    uint32_t temporary_symbol_count;
};

// What object_prepare finds once for a link, which every unit's object reads.
typedef struct ObjectUnits ObjectUnits;
struct ObjectUnits
{
    // Units 0 to count - 1, as object_prepare was given them: every atoms.unit is one of them.
    uint32_t count;
    // The atoms by unit, then class, then id: unit u's atoms of class c are from atoms[atom_begin[u * BUSTER_LINK_CLASS_COUNT
    // + c]] up to the next entry's.
    AtomId* atoms;
    uint32_t* atom_begin;
    // The symbols defined in atoms, by unit, its local ones first, then by id: unit u's locals are from
    // symbols[symbol_begin[2 * u]], and its global and weak ones from symbols[symbol_begin[2 * u + 1]] up to
    // symbols[symbol_begin[2 * u + 2]].
    SymbolId* symbols;
    uint32_t* symbol_begin;
    // Each symbol's unit in the high 32 bits (UINT32_MAX without an atom), and in the low ones what it is to that unit's
    // object (BUSTER_OBJECT_SYMBOL_LOCAL or _GLOBAL, _IN_POOL, _IFUNC, and its place among the unit's local symbols or
    // its global and weak ones): what an object needs of a symbol it meets, in one load.
    uint64_t* symbol_info;
    // Each symbol's name's bytes in .strtab, with its NUL, or 0 without a name; and each unit's for its own symbols, the
    // names given to its local symbols without one among them.
    uint32_t* symbol_name_bytes;
    uint64_t* unit_name_bytes;
    // Each unit's object's EI_OSABI: ELFOSABI_GNU if it defines an IFUNC, otherwise ELFOSABI_SYSV.
    uint8_t* unit_abi;
    // A TEXT atom's name, for its own section with function sections: its first named symbol at offset 0's, or 0.
    NameId* atom_name;
    // The units' inclusive prefix sums of their costs (atoms' bytes, and fixups and symbols at a record's bytes each), by
    // which object_write_units cuts them into parts.
    uint64_t* cost_prefix;
    // lane_parts_by_cost's boundaries, which the lanes share: BUSTER_LANE_MAX_PARTS + 1 entries.
    uint64_t* part_boundaries;
    // Each atom's section in its object, among object_build's sections of atoms (numbered from 0 in the order it makes
    // them, which is their order in the object), and its offset there. object_build writes its own unit's.
    uint32_t* atom_section;
    uint64_t* atom_offset;
};

// What a symbol is to an object (ObjectUnits.symbol_info and ObjectSymbols.entries' low 32 bits): what kind of symbol
// (in the top two bits) and its place among the object's symbols of that kind (in the bits below
// BUSTER_OBJECT_SYMBOL_IFUNC). A local one in a string pool has BUSTER_OBJECT_SYMBOL_IN_POOL too, and a local IFUNC
// BUSTER_OBJECT_SYMBOL_IFUNC, which decide whether a relocation can refer to its section's symbol instead
// (object_uses_section_symbol).
#define BUSTER_OBJECT_SYMBOL_LOCAL 0x00000000u
#define BUSTER_OBJECT_SYMBOL_GLOBAL 0x40000000u
#define BUSTER_OBJECT_SYMBOL_UNDEFINED 0x80000000u
#define BUSTER_OBJECT_SYMBOL_KIND 0xc0000000u
#define BUSTER_OBJECT_SYMBOL_KIND_SHIFT 30
#define BUSTER_OBJECT_SYMBOL_IN_POOL 0x20000000u
#define BUSTER_OBJECT_SYMBOL_IFUNC 0x10000000u
#define BUSTER_OBJECT_SYMBOL_PLACE 0x0fffffffu

// What a lane's objects know of each of the link's symbols, one object after another: entry s is the stamp of the
// object being built when it first met symbol s (in the high 32 bits) and what s is to that object (a code,
// BUSTER_OBJECT_SYMBOL_*). Each object takes the next stamp, so that no object clears what the one before it set.
typedef struct ObjectSymbols ObjectSymbols;
struct ObjectSymbols
{
    uint64_t* entries;
    uint64_t count;
    uint32_t stamp;
};

// An object in memory, as object_build returns it.
typedef struct Object Object;
struct Object
{
    // Its bytes, on the arena object_build was given, and how many.
    uint8_t* bytes;
    uint64_t size;
    uint64_t section_count;
    uint64_t symbol_count;
    uint64_t relocation_count;
    ObjectError error;
};

// Groups the link's atoms and symbols by unit, with what object_build needs, from `arena`, for units 0 to count - 1:
// the caller's translation units, which an atomless one is among, since its object is still written. Returns false if
// an atom's unit isn't below count, the link has an absolute symbol (see the orientation comment), or the arena can't
// hold them (or the scratch arena the counting sorts' cursors).
static __attribute__((unused)) bool object_prepare(ObjectUnits* units, const Link* link, uint32_t count, Arena* arena);
// The calling lane's ObjectSymbols for the link's symbols, from `arena`. Returns false if the arena can't hold them.
static __attribute__((unused)) bool object_map_symbols(ObjectSymbols* symbols, const Link* link, Arena* arena);
// Builds unit `unit`'s object in memory, its STT_FILE named `source_name`: its bytes are pushed on `arena`, and what
// building needs comes from a scratch arena that isn't `arena`. The link must be one link_check accepts, `units`
// object_prepare's for it, and `symbols` the calling lane's object_map_symbols for it.
static __attribute__((unused)) Object object_build(const Link* link, ObjectUnits* units, ObjectSymbols* symbols, uint32_t unit, ObjectOptions options, String8 source_name,
                                                   Arena* arena);
// Writes `size` bytes to the file at `path` (see the function for how).
static __attribute__((unused)) ObjectError object_write_file(const char* path, const uint8_t* bytes, uint64_t size);
// Every lane of the group calls it with the same arguments: it writes each unit's object to paths[unit], its STT_FILE
// named source_names[unit], each lane building whole objects in its scratch arenas, and errors[unit] gets each one's
// error (OBJECT_ERROR_FULL for every unit if a lane can't hold what it needs to start). Returns whether every object was
// written, the same on every lane; like lane_sync, it waits for every lane.
static __attribute__((unused)) bool object_write_units(const Link* link, ObjectUnits* units, ObjectOptions options, const char* const* paths, const String8* source_names,
                                                       ObjectError* errors);

// ---------------------------------------------------------------------------------------------------------------------
// Formats

// A section of atoms: its name, type and flags, and the size of its entries (for the merged strings and the arrays of
// pointers), by the class of its atoms.
typedef struct ObjectSectionFormat ObjectSectionFormat;
struct ObjectSectionFormat
{
    const char* name;
    uint32_t type;
    uint64_t flags;
    uint64_t entry_size;
};

static const ObjectSectionFormat object_class_sections[BUSTER_LINK_CLASS_COUNT] = {
    [BUSTER_LINK_CLASS_NONE] = {
        .name = "",
        .type = SHT_NULL,
        .flags = 0,
        .entry_size = 0,
    },
    [BUSTER_LINK_CLASS_RODATA] = {
        .name = ".rodata",
        .type = SHT_PROGBITS,
        .flags = SHF_ALLOC,
        .entry_size = 0,
    },
    // Strings of one-byte characters, each ending with a NUL, which linkers merge.
    [BUSTER_LINK_CLASS_STRINGS] = {
        .name = ".rodata.str1.1",
        .type = SHT_PROGBITS,
        .flags = SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
        .entry_size = 1,
    },
    // Unwind tables, the type GNU as gives .eh_frame on x86-64.
    [BUSTER_LINK_CLASS_EH_FRAME] = {
        .name = ".eh_frame",
        .type = SHT_X86_64_UNWIND,
        .flags = SHF_ALLOC,
        .entry_size = 0,
    },
    [BUSTER_LINK_CLASS_TEXT] = {
        .name = ".text",
        .type = SHT_PROGBITS,
        .flags = SHF_ALLOC | SHF_EXECINSTR,
        .entry_size = 0,
    },
    [BUSTER_LINK_CLASS_INIT] = {
        .name = ".init",
        .type = SHT_PROGBITS,
        .flags = SHF_ALLOC | SHF_EXECINSTR,
        .entry_size = 0,
    },
    [BUSTER_LINK_CLASS_FINI] = {
        .name = ".fini",
        .type = SHT_PROGBITS,
        .flags = SHF_ALLOC | SHF_EXECINSTR,
        .entry_size = 0,
    },
    [BUSTER_LINK_CLASS_TDATA] = {
        .name = ".tdata",
        .type = SHT_PROGBITS,
        .flags = SHF_WRITE | SHF_ALLOC | SHF_TLS,
        .entry_size = 0,
    },
    [BUSTER_LINK_CLASS_TBSS] = {
        .name = ".tbss",
        .type = SHT_NOBITS,
        .flags = SHF_WRITE | SHF_ALLOC | SHF_TLS,
        .entry_size = 0,
    },
    [BUSTER_LINK_CLASS_INIT_ARRAY] = {
        .name = ".init_array",
        .type = SHT_INIT_ARRAY,
        .flags = SHF_WRITE | SHF_ALLOC,
        .entry_size = 8,
    },
    [BUSTER_LINK_CLASS_FINI_ARRAY] = {
        .name = ".fini_array",
        .type = SHT_FINI_ARRAY,
        .flags = SHF_WRITE | SHF_ALLOC,
        .entry_size = 8,
    },
    [BUSTER_LINK_CLASS_DATA_RELRO] = {
        .name = ".data.rel.ro",
        .type = SHT_PROGBITS,
        .flags = SHF_WRITE | SHF_ALLOC,
        .entry_size = 0,
    },
    [BUSTER_LINK_CLASS_DATA] = {
        .name = ".data",
        .type = SHT_PROGBITS,
        .flags = SHF_WRITE | SHF_ALLOC,
        .entry_size = 0,
    },
    [BUSTER_LINK_CLASS_BSS] = {
        .name = ".bss",
        .type = SHT_NOBITS,
        .flags = SHF_WRITE | SHF_ALLOC,
        .entry_size = 0,
    },
};

// The relocation each fixup kind names (the table in docs/object-writer-and-linker.md, "Representation").
static const uint32_t object_relocation_types[BUSTER_LINK_FIXUP_KINDS] = {
    [BUSTER_LINK_FIXUP_NONE] = R_X86_64_NONE,
    [BUSTER_LINK_FIXUP_CALL] = R_X86_64_PLT32,
    [BUSTER_LINK_FIXUP_CALL_GOT] = R_X86_64_GOTPCRELX,
    [BUSTER_LINK_FIXUP_LOAD_GOT] = R_X86_64_REX_GOTPCRELX,
    [BUSTER_LINK_FIXUP_GOTPCREL] = R_X86_64_GOTPCREL,
    [BUSTER_LINK_FIXUP_PC32] = R_X86_64_PC32,
    [BUSTER_LINK_FIXUP_TPOFF32] = R_X86_64_TPOFF32,
    [BUSTER_LINK_FIXUP_GOTTPOFF] = R_X86_64_GOTTPOFF,
    [BUSTER_LINK_FIXUP_ABS64] = R_X86_64_64,
    [BUSTER_LINK_FIXUP_PC64] = R_X86_64_PC64,
    [BUSTER_LINK_FIXUP_ABS32] = R_X86_64_32,
    [BUSTER_LINK_FIXUP_ABS32S] = R_X86_64_32S,
    [BUSTER_LINK_FIXUP_SIZE32] = R_X86_64_SIZE32,
    [BUSTER_LINK_FIXUP_SIZE64] = R_X86_64_SIZE64,
    [BUSTER_LINK_FIXUP_TLSGD] = R_X86_64_TLSGD,
    [BUSTER_LINK_FIXUP_TLSLD] = R_X86_64_TLSLD,
    [BUSTER_LINK_FIXUP_DTPOFF32] = R_X86_64_DTPOFF32,
};

// The prefix of a relocation section's name: ".rela" and its section's, which .shstrtab shares.
#define BUSTER_OBJECT_RELA_PREFIX ".rela"
#define BUSTER_OBJECT_RELA_PREFIX_BYTES (sizeof(BUSTER_OBJECT_RELA_PREFIX) - 1)
// The prefix of the names given to local symbols without one: .LC0, .LC1 and so on, in the unit's order.
#define BUSTER_OBJECT_LOCAL_LABEL ".LC"
#define BUSTER_OBJECT_LOCAL_LABEL_BYTES (sizeof(BUSTER_OBJECT_LOCAL_LABEL) - 1)
// What .comment says, after its first byte, a NUL as GNU as writes it.
#define BUSTER_OBJECT_COMMENT "buster-fast"
#define BUSTER_OBJECT_COMMENT_BYTES (1 + sizeof(BUSTER_OBJECT_COMMENT))
// The sections after the ones of atoms, in order, and the extended index table's, which only past SHN_LORESERVE
// sections follows .symtab.
static const char* const object_trailing_sections[] = {
    ".note.GNU-stack",
    ".comment",
    ".symtab",
    ".strtab",
    ".shstrtab",
};
#define BUSTER_OBJECT_EXTENDED_SECTION ".symtab_shndx"
// A function section's name: this, then its atom's name.
#define BUSTER_OBJECT_FUNCTION_SECTION_PREFIX ".text."
#define BUSTER_OBJECT_FUNCTION_SECTION_PREFIX_BYTES (sizeof(BUSTER_OBJECT_FUNCTION_SECTION_PREFIX) - 1)
// A section's contents start at a multiple of its alignment in the file, up to this: past it, the file would pay for the
// gap, and linkers copy contents from any offset.
#define BUSTER_OBJECT_MAX_FILE_ALIGNMENT 4096
#define BUSTER_OBJECT_INT3 0xcc
// What a fixup or a symbol costs a unit in object_write_units's parts: about its record's bytes.
#define BUSTER_OBJECT_RECORD_COST 24

static bool object_class_is_zero_fill(uint8_t class)
{
    return object_class_sections[class].type == SHT_NOBITS;
}

// How many decimal digits n has.
static uint64_t object_decimal_digits(uint64_t n)
{
    uint64_t result = 1;
    while (n >= 10)
    {
        n /= 10;
        result += 1;
    }
    return result;
}

// Writes n in decimal at `output`, and returns how many digits it wrote.
static uint64_t object_write_decimal(uint8_t* output, uint64_t n)
{
    const uint64_t result = object_decimal_digits(n);
    for (uint64_t i = result; i > 0; i -= 1)
    {
        output[i - 1] = (uint8_t)('0' + n % 10);
        n /= 10;
    }
    return result;
}

// x rounded up to a multiple of `alignment` (a power of two), false if that overflows.
static bool object_align(uint64_t x, uint64_t alignment, uint64_t* aligned)
{
    const bool result = x <= UINT64_MAX - (alignment - 1);
    *aligned = (x + (alignment - 1)) & ~(alignment - 1);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Preparing

static bool object_prepare(ObjectUnits* units, const Link* link, uint32_t count, Arena* arena)
{
    const LinkAtoms* atoms = &link->atoms;
    const LinkSymbols* symbols = &link->symbols;
    const uint64_t length = arena->length;
    // 64 bits, for the sizes worked out from it.
    const uint64_t unit_count = count;
    bool prepared = true;
    for (AtomId atom = 1; prepared && atom < atoms->count; atom += 1)
    {
        prepared = atoms->unit[atom] < unit_count;
    }
    // The symbols' sources and types, 64 at a time: an absolute symbol, which no unit holds, refuses the link, and an
    // IFUNC makes its unit's object ELFOSABI_GNU (whose units are found below, since IFUNCs are rare).
    uint64_t absolute = 0;
    uint64_t ifunc = 0;
    for (SymbolId first = 1; first < symbols->count; first += 64)
    {
        const uint64_t left = symbols->count - first;
        const __mmask64 valid = left >= 64 ? ~0ull : (1ull << left) - 1;
        const __m512i sources = _mm512_maskz_loadu_epi8(valid, symbols->source + first);
        const __m512i types = _mm512_and_si512(_mm512_maskz_loadu_epi8(valid, symbols->flags + first), _mm512_set1_epi8(BUSTER_LINK_SYMBOL_TYPE));
        absolute |= _mm512_mask_cmpeq_epi8_mask(valid, sources, _mm512_set1_epi8(BUSTER_LINK_SOURCE_ABSOLUTE));
        ifunc |= _mm512_mask_cmpeq_epi8_mask(valid, types, _mm512_set1_epi8(BUSTER_LINK_SYMBOL_IFUNC));
    }
    prepared = prepared && absolute == 0;
    ObjectUnits result = {
        .count = count,
    };
    const uint64_t keys = unit_count * BUSTER_LINK_CLASS_COUNT;
    if (prepared)
    {
        result.atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, atoms->count);
        result.atom_begin = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, keys + 1);
        result.symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, symbols->count);
        result.symbol_begin = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * unit_count + 1);
        result.symbol_info = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, symbols->count);
        result.symbol_name_bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbols->count);
        result.unit_name_bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, unit_count);
        result.unit_abi = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, unit_count);
        result.atom_name = BUSTER_ARENA_PUSH_ARRAY(arena, NameId, atoms->count);
        result.cost_prefix = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, unit_count);
        result.part_boundaries = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, BUSTER_LANE_MAX_PARTS + 1);
        result.atom_section = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, atoms->count);
        result.atom_offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, atoms->count);
        prepared = result.atoms != 0 && result.atom_begin != 0 && result.symbols != 0 && result.symbol_begin != 0 && result.symbol_info != 0 &&
                   result.symbol_name_bytes != 0 && result.unit_name_bytes != 0 && result.unit_abi != 0 && result.atom_name != 0 && result.cost_prefix != 0 &&
                   result.part_boundaries != 0 &&
                   result.atom_section != 0 && result.atom_offset != 0;
    }
    Scratch scratch = scratch_begin(arena);
    uint32_t* cursors = prepared ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, keys > 2 * unit_count ? keys : 2 * unit_count) : 0;
    uint64_t* unnamed = prepared ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, unit_count) : 0;
    prepared = prepared && cursors != 0 && unnamed != 0;
    if (prepared)
    {
        // Atoms by unit and class: count each key's atoms after its entry, add up, then place them in id order.
        memset(result.atom_begin, 0, (keys + 1) * sizeof(uint32_t));
        for (AtomId atom = 1; atom < atoms->count; atom += 1)
        {
            result.atom_begin[(uint64_t)atoms->unit[atom] * BUSTER_LINK_CLASS_COUNT + atoms->class[atom] + 1] += 1;
        }
        for (uint64_t key = 0; key < keys; key += 1)
        {
            result.atom_begin[key + 1] += result.atom_begin[key];
        }
        memcpy(cursors, result.atom_begin, keys * sizeof(uint32_t));
        for (AtomId atom = 1; atom < atoms->count; atom += 1)
        {
            const uint64_t key = (uint64_t)atoms->unit[atom] * BUSTER_LINK_CLASS_COUNT + atoms->class[atom];
            result.atoms[cursors[key]] = atom;
            cursors[key] += 1;
            result.atom_name[atom] = 0;
        }

        // Symbols defined in atoms, by unit, local ones first: the same, and what each one is to its unit's object. A
        // symbol without an atom is in no unit, and every object that refers to it has it undefined.
        memset(result.symbol_begin, 0, (2 * unit_count + 1) * sizeof(uint32_t));
        memset(result.unit_name_bytes, 0, unit_count * sizeof(uint64_t));
        memset(result.unit_abi, ELFOSABI_SYSV, unit_count);
        memset(unnamed, 0, unit_count * sizeof(uint64_t));
        result.symbol_info[0] = (uint64_t)UINT32_MAX << 32;
        result.symbol_name_bytes[0] = 0;
        for (SymbolId symbol = 1; symbol < symbols->count; symbol += 1)
        {
            const AtomId atom = symbols->atom[symbol];
            const NameId name = symbols->name[symbol];
            result.symbol_name_bytes[symbol] = name != 0 ? link->names.length[name] + 1 : 0;
            result.symbol_info[symbol] = (uint64_t)UINT32_MAX << 32;
            if (atom != 0)
            {
                const bool local = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
                result.symbol_begin[2 * (uint64_t)atoms->unit[atom] + (local ? 0 : 1) + 1] += 1;
            }
        }
        for (uint64_t key = 0; key < 2 * unit_count; key += 1)
        {
            result.symbol_begin[key + 1] += result.symbol_begin[key];
        }
        memcpy(cursors, result.symbol_begin, 2 * unit_count * sizeof(uint32_t));
        for (SymbolId symbol = 1; symbol < symbols->count; symbol += 1)
        {
            const AtomId atom = symbols->atom[symbol];
            const NameId name = symbols->name[symbol];
            if (atom != 0)
            {
                const bool local = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
                const uint64_t key = 2 * (uint64_t)atoms->unit[atom] + (local ? 0 : 1);
                const uint32_t place = cursors[key] - result.symbol_begin[key];
                const bool in_pool = local && atoms->class[atom] == BUSTER_LINK_CLASS_STRINGS;
                const uint32_t code = (local ? BUSTER_OBJECT_SYMBOL_LOCAL : BUSTER_OBJECT_SYMBOL_GLOBAL) | (in_pool ? BUSTER_OBJECT_SYMBOL_IN_POOL : 0) | place;
                result.symbols[cursors[key]] = symbol;
                result.symbol_info[symbol] = (uint64_t)atoms->unit[atom] << 32 | code;
                cursors[key] += 1;
                // The unit's names: a local symbol without one gets .LC<n>, n counting the unit's.
                if (name != 0)
                {
                    result.unit_name_bytes[atoms->unit[atom]] += result.symbol_name_bytes[symbol];
                }
                else if (local)
                {
                    result.unit_name_bytes[atoms->unit[atom]] += BUSTER_OBJECT_LOCAL_LABEL_BYTES + object_decimal_digits(unnamed[atoms->unit[atom]]) + 1;
                    unnamed[atoms->unit[atom]] += 1;
                }
                if (atoms->class[atom] == BUSTER_LINK_CLASS_TEXT && symbols->value[symbol] == 0 && name != 0 && result.atom_name[atom] == 0)
                {
                    result.atom_name[atom] = name;
                }
            }
        }
        // The IFUNCs, if the link has one, 64 symbols' types at a time: each one in an atom makes its unit's object
        // ELFOSABI_GNU, and a local one's relocations name it (BUSTER_OBJECT_SYMBOL_IFUNC).
        for (SymbolId first = 1; ifunc != 0 && first < symbols->count; first += 64)
        {
            const uint64_t left = symbols->count - first;
            const __mmask64 valid = left >= 64 ? ~0ull : (1ull << left) - 1;
            const __m512i types = _mm512_and_si512(_mm512_maskz_loadu_epi8(valid, symbols->flags + first), _mm512_set1_epi8(BUSTER_LINK_SYMBOL_TYPE));
            for (uint64_t ifuncs = _mm512_mask_cmpeq_epi8_mask(valid, types, _mm512_set1_epi8(BUSTER_LINK_SYMBOL_IFUNC)); ifuncs != 0; ifuncs &= ifuncs - 1)
            {
                const SymbolId symbol = first + (SymbolId)__builtin_ctzll(ifuncs);
                const AtomId atom = symbols->atom[symbol];
                const bool local = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
                if (atom != 0)
                {
                    result.unit_abi[atoms->unit[atom]] = ELFOSABI_GNU;
                    result.symbol_info[symbol] |= local ? BUSTER_OBJECT_SYMBOL_IFUNC : 0;
                }
            }
        }

        // Each unit's cost: its atoms' bytes, and its fixups and symbols at a record each.
        uint64_t cost = 0;
        for (uint64_t unit = 0; unit < unit_count; unit += 1)
        {
            for (uint32_t i = result.atom_begin[unit * BUSTER_LINK_CLASS_COUNT]; i < result.atom_begin[(unit + 1) * BUSTER_LINK_CLASS_COUNT]; i += 1)
            {
                const AtomId atom = result.atoms[i];
                cost += atoms->size[atom] + (uint64_t)atoms->fixup_count[atom] * BUSTER_OBJECT_RECORD_COST;
            }
            cost += (uint64_t)(result.symbol_begin[2 * unit + 2] - result.symbol_begin[2 * unit]) * BUSTER_OBJECT_RECORD_COST;
            result.cost_prefix[unit] = cost;
        }
    }
    scratch_end(scratch);
    if (!prepared)
    {
        arena_pop_to(arena, length);
        result = (ObjectUnits){ 0 };
    }
    *units = result;
    return prepared;
}

static bool object_map_symbols(ObjectSymbols* symbols, const Link* link, Arena* arena)
{
    *symbols = (ObjectSymbols){
        .entries = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, link->symbols.count),
        .count = link->symbols.count,
        .stamp = 0,
    };
    const bool result = symbols->entries != 0;
    if (result)
    {
        memset(symbols->entries, 0, symbols->count * sizeof(uint64_t));
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Building


// One of an object's sections of atoms: which atoms (a range of ObjectUnits.atoms) of which class, their size, alignment
// and fixups, and once placed, its index, its .rela section's (0 without fixups), its section symbol's (0 if no
// relocation refers to it; object_count_symbols sets it to 1 first) and its name's offset in .shstrtab.
typedef struct ObjectSection ObjectSection;
struct ObjectSection
{
    uint32_t atom_begin;
    uint32_t atom_end;
    uint8_t class;
    uint64_t size;
    uint64_t alignment;
    uint64_t fixup_count;
    uint32_t index;
    uint32_t relocation_index;
    uint32_t symbol;
    uint32_t name;
    const ObjectAtomSection* explicit_section;
};

// What object_build counts and places for one object.
typedef struct ObjectBuilder ObjectBuilder;
struct ObjectBuilder
{
    const Link* link;
    ObjectUnits* units;
    uint32_t unit;
    ObjectOptions options;
    String8 source_name;
    // The sections of atoms, in the object's order.
    ObjectSection* sections;
    uint32_t section_count;
    uint64_t fixup_count;
    // What the unit's fixups refer to: the lane's ObjectSymbols, this object's stamp there, and the undefined symbols in
    // the order the fixups first refer to them.
    ObjectSymbols* symbols;
    uint64_t stamp;
    SymbolId* undefined;
    uint64_t undefined_count;
    // The symbols: section symbols from index 2, then locals from local_base, the unit's global and weak ones from
    // global_base, and the undefined ones from undefined_base, symbol_count in all.
    uint64_t section_symbol_count;
    uint64_t local_count;
    // Original local place -> compact local place, UINT32_MAX for an elided temporary; absent on default writes.
    uint32_t* local_places;
    uint64_t global_count;
    uint64_t local_base;
    uint64_t global_base;
    uint64_t undefined_base;
    uint64_t symbol_count;
    // The string tables' sizes.
    uint64_t names_size;
    uint64_t section_names_size;
    // The object's section headers, all of them, and the indices of the ones after the sections of atoms
    // (extended_index is 0 without .symtab_shndx).
    Elf64_Shdr* headers;
    uint64_t header_count;
    uint32_t note_index;
    uint32_t comment_index;
    uint32_t symbol_table_index;
    uint32_t extended_index;
    uint32_t names_index;
    uint32_t section_names_index;
    uint64_t headers_offset;
    uint64_t size;
    ObjectError error;
};

// Whether a fixup of `kind` with `addend` refers to its local target (whose code is `code`) through the target's section
// symbol rather than itself (see the orientation comment): GNU as and LLVM keep the symbol for the GOT, TLS and size
// kinds and for an IFUNC, and in a string pool for a PC-relative kind or any addend.
static bool object_uses_section_symbol(uint8_t kind, uint32_t code, int64_t addend)
{
    const bool kept = kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL || kind == BUSTER_LINK_FIXUP_TPOFF32 ||
                      kind == BUSTER_LINK_FIXUP_GOTTPOFF || kind == BUSTER_LINK_FIXUP_SIZE32 || kind == BUSTER_LINK_FIXUP_SIZE64 || kind == BUSTER_LINK_FIXUP_TLSGD ||
                      kind == BUSTER_LINK_FIXUP_TLSLD || kind == BUSTER_LINK_FIXUP_DTPOFF32 || (code & BUSTER_OBJECT_SYMBOL_IFUNC) != 0;
    const bool pool = (code & BUSTER_OBJECT_SYMBOL_IN_POOL) != 0 && (link_fixup_kinds[kind].pc_relative || addend != 0);
    return !kept && !pool;
}

// The unit's sections of atoms: a section per class with atoms (with function sections, per TEXT atom), each atom at the
// next multiple of its alignment there, and each section's size, alignment and fixups.
static bool object_count_sections(ObjectBuilder* builder)
{
    const LinkAtoms* atoms = &builder->link->atoms;
    ObjectUnits* units = builder->units;
    const uint64_t key = (uint64_t)builder->unit * BUSTER_LINK_CLASS_COUNT;
    bool result = true;
    for (uint8_t class = 1; result && class < BUSTER_LINK_CLASS_COUNT; class += 1)
    {
        const uint32_t begin = units->atom_begin[key + class];
        const uint32_t end = units->atom_begin[key + class + 1];
        const bool apart = builder->options.atom_sections != 0 || (builder->options.function_sections && class == BUSTER_LINK_CLASS_TEXT);
        for (uint32_t first = begin; result && first < end; first = apart ? first + 1 : end)
        {
            ObjectSection* section = &builder->sections[builder->section_count];
            *section = (ObjectSection){
                .atom_begin = first,
                .atom_end = apart ? first + 1 : end,
                .class = class,
                .alignment = 1,
                .explicit_section = builder->options.atom_sections != 0 ? &builder->options.atom_sections[units->atoms[first]] : 0,
            };
            const ObjectAtomSection* explicit = section->explicit_section;
            if (explicit != 0)
            {
                result = explicit->name.pointer != 0 && explicit->name.length != 0 && explicit->name.length < UINT32_MAX &&
                         memchr(explicit->name.pointer, 0, explicit->name.length) == 0 &&
                         (explicit->type == object_class_sections[class].type || (!object_class_is_zero_fill(class) && explicit->type == SHT_PROGBITS)) &&
                         ((explicit->flags & SHF_MERGE) == 0 || explicit->entry_size != 0);
            }
            uint64_t offset = 0;
            for (uint32_t i = section->atom_begin; result && i < section->atom_end; i += 1)
            {
                const AtomId atom = units->atoms[i];
                const uint64_t alignment = 1ull << atoms->alignment_log2[atom];
                result = object_align(offset, alignment, &offset);
                units->atom_section[atom] = builder->section_count;
                units->atom_offset[atom] = offset;
                result = result && !__builtin_add_overflow(offset, atoms->size[atom], &offset);
                section->alignment = alignment > section->alignment ? alignment : section->alignment;
                section->fixup_count += atoms->fixup_count[atom];
            }
            section->size = offset;
            builder->fixup_count += section->fixup_count;
            builder->section_count += 1;
        }
    }
    if (!result)
    {
        builder->error = OBJECT_ERROR_RANGE;
    }
    return result;
}

// The symbols: the unit's own (object_prepare grouped them), the section symbols the fixups need and the undefined
// symbols they refer to, found by a pass over the unit's fixups in section order; and the string table's bytes.
static bool object_count_symbols(ObjectBuilder* builder, Arena* scratch)
{
    const Link* link = builder->link;
    const LinkSymbols* symbols = &link->symbols;
    const LinkFixups* fixups = &link->fixups;
    const LinkAtoms* atoms = &link->atoms;
    ObjectUnits* units = builder->units;
    const uint32_t* begin = units->symbol_begin + 2 * (uint64_t)builder->unit;
    builder->local_count = begin[1] - begin[0];
    builder->global_count = begin[2] - begin[1];
    if (builder->options.temporary_symbols != 0)
    {
        builder->local_places = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, builder->local_count);
        for (uint32_t i = begin[0]; builder->local_places != 0 && i < begin[1]; i += 1)
        {
            builder->local_places[i - begin[0]] = !builder->options.temporary_symbols[units->symbols[i]];
        }
    }

    // Room for every fixup to refer to a different undefined symbol; and the object's stamp, after a clear once the
    // stamps have wrapped around.
    builder->undefined = BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, builder->fixup_count);
    bool result = (builder->undefined != 0 || builder->fixup_count == 0) && builder->symbols->count == symbols->count &&
                  (builder->options.temporary_symbols == 0 || builder->local_count == 0 || builder->local_places != 0);
    builder->error = result ? OBJECT_ERROR_NONE : OBJECT_ERROR_FULL;
    builder->symbols->stamp += 1;
    if (builder->symbols->stamp == 0)
    {
        memset(builder->symbols->entries, 0, builder->symbols->count * sizeof(uint64_t));
        builder->symbols->stamp = 1;
    }
    builder->stamp = (uint64_t)builder->symbols->stamp << 32;
    result = result && builder->local_count <= BUSTER_OBJECT_SYMBOL_PLACE && builder->global_count <= BUSTER_OBJECT_SYMBOL_PLACE;

    // The string table: a NUL, the source's name, then the names of the unit's symbols (which object_prepare added up)
    // and of the undefined ones.
    uint64_t names = 1 + (builder->source_name.length != 0 ? builder->source_name.length + 1 : 0) + units->unit_name_bytes[builder->unit];
    // The columns the loop reads and writes, in locals: with -fno-strict-aliasing, every store would otherwise make the
    // compiler load each of them again.
    const AtomId* unit_atoms = units->atoms;
    const FixupId* fixup_begin = atoms->fixup_begin;
    const uint32_t* fixup_count = atoms->fixup_count;
    const SymbolId* targets = fixups->target;
    const uint8_t* kinds = fixups->kind;
    const int32_t* addends = fixups->addend;
    const int64_t* wide_addends = fixups->wide_addends;
    const uint64_t* symbol_info = units->symbol_info;
    const uint32_t* symbol_name_bytes = units->symbol_name_bytes;
    uint64_t* entries = builder->symbols->entries;
    SymbolId* undefined = builder->undefined;
    ObjectSection* sections = builder->sections;
    const uint64_t stamp = builder->stamp;
    const uint64_t unit = builder->unit;
    uint64_t undefined_count = 0;
    for (uint32_t s = 0; result && s < builder->section_count; s += 1)
    {
        const uint32_t atom_end = sections[s].atom_end;
        for (uint32_t i = sections[s].atom_begin; result && i < atom_end; i += 1)
        {
            const AtomId atom = unit_atoms[i];
            const FixupId first = fixup_begin[atom];
            const FixupId end = first + fixup_count[atom];
            for (FixupId fixup = first; result && fixup < end; fixup += 1)
            {
                // The first time the object meets a target, what it is: a symbol the unit defines (a local one, which
                // link_check keeps in its unit, or a global or weak one), as object_prepare found it, or an undefined
                // one, which gets the next place. (On the 9700X a version without the branch, which loads and stores
                // every time, was a third slower: the undefined symbols' count became a chain through the loop.)
                const SymbolId target = targets[fixup];
                uint64_t entry = entries[target];
                if ((entry & ~(uint64_t)UINT32_MAX) != stamp)
                {
                    const uint64_t info = symbol_info[target];
                    const bool defined = info >> 32 == unit;
                    entry = stamp | (defined ? (uint32_t)info : BUSTER_OBJECT_SYMBOL_UNDEFINED | (uint32_t)undefined_count);
                    if (defined && builder->options.atom_sections != 0 && ((uint32_t)entry & BUSTER_OBJECT_SYMBOL_KIND) == BUSTER_OBJECT_SYMBOL_LOCAL)
                    {
                        // Explicit mergeable sections include fixed-width constants, not only the default string
                        // class. Their PC-relative/addended relocations need the original local target as well.
                        const ObjectAtomSection* section = &builder->options.atom_sections[symbols->atom[target]];
                        entry &= ~(uint64_t)BUSTER_OBJECT_SYMBOL_IN_POOL;
                        entry |= (section->flags & SHF_MERGE) != 0 ? BUSTER_OBJECT_SYMBOL_IN_POOL : 0;
                    }
                    entries[target] = entry;
                    if (!defined)
                    {
                        undefined[undefined_count] = target;
                        undefined_count += 1;
                        names += symbol_name_bytes[target];
                    }
                }
                // A relocation that refers to a local target's section symbol needs that symbol, and the target's offset
                // in the addend, which has to stay within 64 bits.
                const uint32_t code = (uint32_t)entry;
                if ((code & BUSTER_OBJECT_SYMBOL_KIND) == BUSTER_OBJECT_SYMBOL_LOCAL)
                {
                    const uint8_t kind = kinds[fixup];
                    const int64_t addend = (kind & BUSTER_LINK_FIXUP_WIDE) != 0 ? wide_addends[(uint32_t)addends[fixup]] : addends[fixup];
                    if (object_uses_section_symbol(kind & BUSTER_LINK_FIXUP_KIND, code, addend))
                    {
                        const AtomId target_atom = symbols->atom[target];
                        const uint64_t place = units->atom_offset[target_atom] + symbols->value[target];
                        int64_t adjusted = 0;
                        result = place <= INT64_MAX && !__builtin_add_overflow(addend, (int64_t)place, &adjusted);
                        sections[units->atom_section[target_atom]].symbol = 1;
                    }
                    else if (builder->local_places != 0)
                    {
                        builder->local_places[code & BUSTER_OBJECT_SYMBOL_PLACE] = 1;
                    }
                }
            }
        }
    }
    builder->undefined_count = undefined_count;
    if (builder->local_places != 0)
    {
        // Charge only retained local names, renumber unnamed locals as object_fill_symbols does.
        names -= units->unit_name_bytes[builder->unit];
        uint32_t kept = 0;
        uint64_t unnamed = 0;
        for (uint32_t i = begin[0]; i < begin[2]; i += 1)
        {
            const bool local = i < begin[1];
            const bool keep = !local || builder->local_places[i - begin[0]] != 0;
            if (local)
            {
                builder->local_places[i - begin[0]] = keep ? kept++ : UINT32_MAX;
            }
            const uint32_t bytes = symbol_name_bytes[units->symbols[i]];
            if (keep && bytes != 0)
            {
                names += bytes;
            }
            else if (keep && local)
            {
                names += BUSTER_OBJECT_LOCAL_LABEL_BYTES + object_decimal_digits(unnamed++) + 1;
            }
        }
        builder->local_count = kept;
    }
    result = result && undefined_count <= BUSTER_OBJECT_SYMBOL_PLACE;
    if (!result && builder->error == OBJECT_ERROR_NONE)
    {
        builder->error = OBJECT_ERROR_RANGE;
    }
    builder->names_size = names;
    return result;
}

// The length of a section of atoms' name, which a function section takes from its atom's.
static uint64_t object_section_name_bytes(const ObjectBuilder* builder, const ObjectSection* section)
{
    uint64_t result = strlen(object_class_sections[section->class].name);
    if (section->explicit_section != 0)
    {
        result = section->explicit_section->name.length;
    }
    else if (builder->options.function_sections && section->class == BUSTER_LINK_CLASS_TEXT)
    {
        const NameId name = builder->units->atom_name[builder->units->atoms[section->atom_begin]];
        result = name != 0 ? BUSTER_OBJECT_FUNCTION_SECTION_PREFIX_BYTES + builder->link->names.length[name] : result;
    }
    return result;
}

// Places what was counted: the section indices (each section of atoms followed by its .rela section), the section
// symbols and the other symbols' indices, the section names' offsets in .shstrtab (a .rela section's name holds its
// section's at BUSTER_OBJECT_RELA_PREFIX_BYTES), and each section's range of the file after the ELF header, in index
// order, then the section headers. Every sum is checked.
static bool object_place(ObjectBuilder* builder, Arena* scratch)
{
    bool result = true;
    uint64_t index = 1;
    uint64_t symbol = 2;
    uint64_t name = 1;
    for (uint32_t s = 0; s < builder->section_count; s += 1)
    {
        ObjectSection* section = &builder->sections[s];
        const uint64_t bytes = object_section_name_bytes(builder, section) + 1;
        section->index = (uint32_t)index;
        section->relocation_index = section->fixup_count != 0 ? (uint32_t)(index + 1) : 0;
        section->name = (uint32_t)(name + (section->fixup_count != 0 ? BUSTER_OBJECT_RELA_PREFIX_BYTES : 0));
        index += section->fixup_count != 0 ? 2 : 1;
        name += bytes + (section->fixup_count != 0 ? BUSTER_OBJECT_RELA_PREFIX_BYTES : 0);
        section->symbol = section->symbol != 0 ? (uint32_t)symbol : 0;
        symbol += section->symbol != 0 ? 1 : 0;
        result = result && index < UINT32_MAX && name < UINT32_MAX;
    }
    // .symtab_shndx only when a section's index needs it.
    const bool extended = index - 1 >= SHN_LORESERVE;
    builder->note_index = (uint32_t)index;
    builder->comment_index = (uint32_t)(index + 1);
    builder->symbol_table_index = (uint32_t)(index + 2);
    builder->extended_index = extended ? (uint32_t)(index + 3) : 0;
    builder->names_index = (uint32_t)(index + (extended ? 4 : 3));
    builder->section_names_index = builder->names_index + 1;
    builder->header_count = builder->section_names_index + 1;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(object_trailing_sections); i += 1)
    {
        name += strlen(object_trailing_sections[i]) + 1;
    }
    name += extended ? sizeof(BUSTER_OBJECT_EXTENDED_SECTION) : 0;
    builder->section_names_size = name;
    builder->section_symbol_count = symbol - 2;
    builder->local_base = symbol;
    builder->global_base = builder->local_base + builder->local_count;
    builder->undefined_base = builder->global_base + builder->global_count;
    builder->symbol_count = builder->undefined_base + builder->undefined_count;
    result = result && index + 5 < UINT32_MAX && name < UINT32_MAX && builder->names_size < UINT32_MAX && builder->symbol_count < UINT32_MAX;

    // The headers, with every field but the offsets.
    builder->headers = result ? BUSTER_ARENA_PUSH_ARRAY(scratch, Elf64_Shdr, builder->header_count) : 0;
    if (result && builder->headers == 0)
    {
        builder->error = OBJECT_ERROR_FULL;
        result = false;
    }
    if (result)
    {
        Elf64_Shdr* headers = builder->headers;
        headers[0] = (Elf64_Shdr){
            .sh_size = builder->header_count >= SHN_LORESERVE ? builder->header_count : 0,
            .sh_link = builder->section_names_index >= SHN_LORESERVE ? builder->section_names_index : 0,
        };
        for (uint32_t s = 0; s < builder->section_count; s += 1)
        {
            const ObjectSection* section = &builder->sections[s];
            const ObjectSectionFormat* format = &object_class_sections[section->class];
            const ObjectAtomSection* explicit = section->explicit_section;
            headers[section->index] = (Elf64_Shdr){
                .sh_name = section->name,
                .sh_type = explicit != 0 ? explicit->type : format->type,
                .sh_flags = explicit != 0 ? explicit->flags : format->flags,
                .sh_size = section->size,
                .sh_addralign = section->alignment,
                .sh_entsize = explicit != 0 ? explicit->entry_size : format->entry_size,
            };
            if (section->relocation_index != 0)
            {
                headers[section->relocation_index] = (Elf64_Shdr){
                    .sh_name = section->name - (uint32_t)BUSTER_OBJECT_RELA_PREFIX_BYTES,
                    .sh_type = SHT_RELA,
                    .sh_flags = SHF_INFO_LINK,
                    .sh_size = section->fixup_count * sizeof(Elf64_Rela),
                    .sh_link = builder->symbol_table_index,
                    .sh_info = section->index,
                    .sh_addralign = 8,
                    .sh_entsize = sizeof(Elf64_Rela),
                };
            }
        }
        uint64_t trailing_name = builder->section_names_size - (extended ? sizeof(BUSTER_OBJECT_EXTENDED_SECTION) : 0);
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(object_trailing_sections); i += 1)
        {
            trailing_name -= strlen(object_trailing_sections[i]) + 1;
        }
        headers[builder->note_index] = (Elf64_Shdr){
            .sh_name = (uint32_t)trailing_name,
            .sh_type = SHT_PROGBITS,
            .sh_addralign = 1,
        };
        trailing_name += strlen(object_trailing_sections[0]) + 1;
        headers[builder->comment_index] = (Elf64_Shdr){
            .sh_name = (uint32_t)trailing_name,
            .sh_type = SHT_PROGBITS,
            .sh_flags = SHF_MERGE | SHF_STRINGS,
            .sh_size = BUSTER_OBJECT_COMMENT_BYTES,
            .sh_addralign = 1,
            .sh_entsize = 1,
        };
        trailing_name += strlen(object_trailing_sections[1]) + 1;
        headers[builder->symbol_table_index] = (Elf64_Shdr){
            .sh_name = (uint32_t)trailing_name,
            .sh_type = SHT_SYMTAB,
            .sh_size = builder->symbol_count * sizeof(Elf64_Sym),
            .sh_link = builder->names_index,
            .sh_info = (uint32_t)builder->global_base,
            .sh_addralign = 8,
            .sh_entsize = sizeof(Elf64_Sym),
        };
        trailing_name += strlen(object_trailing_sections[2]) + 1;
        headers[builder->names_index] = (Elf64_Shdr){
            .sh_name = (uint32_t)trailing_name,
            .sh_type = SHT_STRTAB,
            .sh_size = builder->names_size,
            .sh_addralign = 1,
        };
        trailing_name += strlen(object_trailing_sections[3]) + 1;
        headers[builder->section_names_index] = (Elf64_Shdr){
            .sh_name = (uint32_t)trailing_name,
            .sh_type = SHT_STRTAB,
            .sh_size = builder->section_names_size,
            .sh_addralign = 1,
        };
        trailing_name += strlen(object_trailing_sections[4]) + 1;
        if (extended)
        {
            headers[builder->extended_index] = (Elf64_Shdr){
                .sh_name = (uint32_t)trailing_name,
                .sh_type = SHT_SYMTAB_SHNDX,
                .sh_size = builder->symbol_count * sizeof(uint32_t),
                .sh_link = builder->symbol_table_index,
                .sh_addralign = 4,
                .sh_entsize = sizeof(uint32_t),
            };
        }

        // The file's ranges, after the ELF header, in index order: each section's contents at the next multiple of its
        // alignment (up to BUSTER_OBJECT_MAX_FILE_ALIGNMENT), and a zero-fill one where its contents would start.
        uint64_t offset = sizeof(Elf64_Ehdr);
        for (uint64_t i = 1; result && i < builder->header_count; i += 1)
        {
            Elf64_Shdr* header = &headers[i];
            const uint64_t alignment = header->sh_addralign < BUSTER_OBJECT_MAX_FILE_ALIGNMENT ? header->sh_addralign : BUSTER_OBJECT_MAX_FILE_ALIGNMENT;
            result = object_align(offset, alignment, &offset);
            header->sh_offset = offset;
            result = result && (header->sh_type == SHT_NOBITS || !__builtin_add_overflow(offset, header->sh_size, &offset));
        }
        result = result && object_align(offset, 8, &builder->headers_offset) &&
                 !__builtin_add_overflow(builder->headers_offset, builder->header_count * sizeof(Elf64_Shdr), &builder->size);
    }
    if (!result && builder->error == OBJECT_ERROR_NONE)
    {
        builder->error = OBJECT_ERROR_RANGE;
    }
    return result;
}

// A section index as st_shndx holds it: SHN_XINDEX from SHN_LORESERVE on, with the index in .symtab_shndx.
static uint16_t object_symbol_section_index(uint32_t index)
{
    return index >= SHN_LORESERVE ? SHN_XINDEX : (uint16_t)index;
}

// The sections of atoms' contents: each atom's bytes at its offset, int3 in code's gaps and zero in the others'.
static void object_fill_sections(const ObjectBuilder* builder, uint8_t* bytes)
{
    const LinkAtoms* atoms = &builder->link->atoms;
    const ObjectUnits* units = builder->units;
    for (uint32_t s = 0; s < builder->section_count; s += 1)
    {
        const ObjectSection* section = &builder->sections[s];
        if (!object_class_is_zero_fill(section->class))
        {
            uint8_t* contents = bytes + builder->headers[section->index].sh_offset;
            const uint8_t gap = section->class == BUSTER_LINK_CLASS_TEXT ? BUSTER_OBJECT_INT3 : 0;
            uint64_t end = 0;
            for (uint32_t i = section->atom_begin; i < section->atom_end; i += 1)
            {
                const AtomId atom = units->atoms[i];
                const uint64_t offset = units->atom_offset[atom];
                memset(contents + end, gap, offset - end);
                if (atoms->size[atom] != 0)
                {
                    memcpy(contents + offset, atoms->bytes[atom], atoms->size[atom]);
                }
                end = offset + atoms->size[atom];
            }
        }
    }
}

// The relocations of each section of atoms, in order: its atoms', each atom's fixups in offset order. The columns the
// loops read are in locals: with -fno-strict-aliasing, each store would otherwise make the compiler load them again.
static void object_fill_relocations(const ObjectBuilder* builder, uint8_t* bytes)
{
    const Link* link = builder->link;
    const ObjectUnits* units = builder->units;
    const AtomId* unit_atoms = units->atoms;
    const uint64_t* atom_offset = units->atom_offset;
    const uint32_t* atom_section = units->atom_section;
    const FixupId* fixup_begin = link->atoms.fixup_begin;
    const uint32_t* fixup_count = link->atoms.fixup_count;
    const uint32_t* offsets = link->fixups.offset;
    const SymbolId* targets = link->fixups.target;
    const uint8_t* kinds = link->fixups.kind;
    const int32_t* addends = link->fixups.addend;
    const int64_t* wide_addends = link->fixups.wide_addends;
    const AtomId* symbol_atoms = link->symbols.atom;
    const uint64_t* symbol_values = link->symbols.value;
    const uint64_t* entries = builder->symbols->entries;
    const ObjectSection* sections = builder->sections;
    // Each kind of symbol's first index, by its code's top bits.
    const uint64_t bases[4] = {
        builder->local_base,
        builder->global_base,
        builder->undefined_base,
        0,
    };
    for (uint32_t s = 0; s < builder->section_count; s += 1)
    {
        const ObjectSection* section = &sections[s];
        if (section->relocation_index != 0)
        {
            Elf64_Rela* relocation = (Elf64_Rela*)(bytes + builder->headers[section->relocation_index].sh_offset);
            for (uint32_t i = section->atom_begin; i < section->atom_end; i += 1)
            {
                const AtomId atom = unit_atoms[i];
                const uint64_t base = atom_offset[atom];
                const FixupId first = fixup_begin[atom];
                const FixupId end = first + fixup_count[atom];
                for (FixupId fixup = first; fixup < end; fixup += 1)
                {
                    // What object_count_symbols found the target to be: its index is its kind's first plus its place,
                    // unless the relocation refers to a local target's section symbol.
                    const SymbolId target = targets[fixup];
                    const uint32_t code = (uint32_t)entries[target];
                    const uint8_t kind_bits = kinds[fixup];
                    const uint8_t kind = kind_bits & BUSTER_LINK_FIXUP_KIND;
                    int64_t addend = (kind_bits & BUSTER_LINK_FIXUP_WIDE) != 0 ? wide_addends[(uint32_t)addends[fixup]] : addends[fixup];
                    uint64_t index = bases[code >> BUSTER_OBJECT_SYMBOL_KIND_SHIFT] + (code & BUSTER_OBJECT_SYMBOL_PLACE);
                    if ((code & BUSTER_OBJECT_SYMBOL_KIND) == BUSTER_OBJECT_SYMBOL_LOCAL && builder->local_places != 0)
                    {
                        index = builder->local_base + builder->local_places[code & BUSTER_OBJECT_SYMBOL_PLACE];
                    }
                    if ((code & BUSTER_OBJECT_SYMBOL_KIND) == BUSTER_OBJECT_SYMBOL_LOCAL && object_uses_section_symbol(kind, code, addend))
                    {
                        const AtomId target_atom = symbol_atoms[target];
                        index = sections[atom_section[target_atom]].symbol;
                        // object_count_symbols checked that this stays within 64 bits.
                        addend += (int64_t)(atom_offset[target_atom] + symbol_values[target]);
                    }
                    *relocation = (Elf64_Rela){
                        .r_offset = base + offsets[fixup],
                        .r_info = ELF64_R_INFO(index, object_relocation_types[kind]),
                        .r_addend = addend,
                    };
                    relocation += 1;
                }
            }
        }
    }
}

// Copies a name of `bytes` bytes, its NUL among them: one masked 64-byte load and store for a name of up to 64 bytes,
// as most C names are, rather than a call to memcpy, and memcpy for a longer one. A masked load and store touch nothing
// past `bytes`, so they are safe at the end of a page.
static void object_copy_name(uint8_t* destination, const uint8_t* source, uint64_t bytes)
{
    if (bytes <= 64)
    {
        const __mmask64 mask = _bzhi_u64(UINT64_MAX, (uint32_t)bytes);
        _mm512_mask_storeu_epi8(destination, mask, _mm512_maskz_loadu_epi8(mask, source));
    }
    else
    {
        memcpy(destination, source, bytes);
    }
}

// .symtab, .symtab_shndx and .strtab: the symbols in their order (object_place), and their names in the same order. The
// columns the loops read are in locals, as in object_fill_relocations.
static void object_fill_symbols(const ObjectBuilder* builder, uint8_t* bytes)
{
    const Link* link = builder->link;
    const ObjectUnits* units = builder->units;
    const NameId* symbol_names = link->symbols.name;
    const AtomId* symbol_atoms = link->symbols.atom;
    const uint64_t* symbol_values = link->symbols.value;
    const uint64_t* symbol_sizes = link->symbols.size;
    const uint8_t* symbol_flags = link->symbols.flags;
    const uint8_t* const* name_bytes = link->names.bytes;
    const uint32_t* symbol_name_bytes = units->symbol_name_bytes;
    const uint32_t* atom_section = units->atom_section;
    const uint64_t* atom_offset = units->atom_offset;
    const SymbolId* unit_symbols = units->symbols;
    const ObjectSection* sections = builder->sections;
    Elf64_Sym* entries = (Elf64_Sym*)(bytes + builder->headers[builder->symbol_table_index].sh_offset);
    uint32_t* extended = builder->extended_index != 0 ? (uint32_t*)(bytes + builder->headers[builder->extended_index].sh_offset) : 0;
    uint8_t* names = bytes + builder->headers[builder->names_index].sh_offset;
    uint64_t name = 1;
    names[0] = 0;
    entries[0] = (Elf64_Sym){ 0 };
    entries[1] = (Elf64_Sym){
        .st_name = builder->source_name.length != 0 ? 1 : 0,
        .st_info = ELF64_ST_INFO(STB_LOCAL, STT_FILE),
        .st_shndx = SHN_ABS,
    };
    if (builder->source_name.length != 0)
    {
        memcpy(names + name, builder->source_name.pointer, builder->source_name.length);
        names[name + builder->source_name.length] = 0;
        name += builder->source_name.length + 1;
    }
    if (extended != 0)
    {
        memset(extended, 0, builder->symbol_count * sizeof(uint32_t));
    }
    for (uint32_t s = 0; s < builder->section_count; s += 1)
    {
        const ObjectSection* section = &sections[s];
        if (section->symbol != 0)
        {
            entries[section->symbol] = (Elf64_Sym){
                .st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION),
                .st_shndx = object_symbol_section_index(section->index),
            };
            if (extended != 0 && section->index >= SHN_LORESERVE)
            {
                extended[section->symbol] = section->index;
            }
        }
    }
    // The unit's symbols, locals first, as object_prepare grouped them: each at its atom's offset in its section, and
    // the locals without a name named .LC<n>.
    const uint32_t* begin = units->symbol_begin + 2 * (uint64_t)builder->unit;
    Elf64_Sym* entry = entries + builder->local_base;
    uint64_t unnamed = 0;
    for (uint32_t i = begin[0]; i < begin[2]; i += 1)
    {
        if (i < begin[1] && builder->local_places != 0 && builder->local_places[i - begin[0]] == UINT32_MAX)
        {
            continue;
        }
        const SymbolId symbol = unit_symbols[i];
        const uint32_t name_bytes_with_nul = symbol_name_bytes[symbol];
        const bool labelled = name_bytes_with_nul == 0 && i < begin[1];
        const AtomId atom = symbol_atoms[symbol];
        const uint32_t section = sections[atom_section[atom]].index;
        const uint8_t flags = symbol_flags[symbol];
        *entry = (Elf64_Sym){
            .st_name = name_bytes_with_nul != 0 || labelled ? (uint32_t)name : 0,
            .st_info = link_symbol_elf_info(flags),
            .st_other = flags >> BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT,
            .st_shndx = object_symbol_section_index(section),
            .st_value = atom_offset[atom] + symbol_values[symbol],
            .st_size = symbol_sizes[symbol],
        };
        if (extended != 0 && section >= SHN_LORESERVE)
        {
            extended[entry - entries] = section;
        }
        if (name_bytes_with_nul != 0)
        {
            object_copy_name(names + name, name_bytes[symbol_names[symbol]], name_bytes_with_nul);
            name += name_bytes_with_nul;
        }
        else if (labelled)
        {
            memcpy(names + name, BUSTER_OBJECT_LOCAL_LABEL, BUSTER_OBJECT_LOCAL_LABEL_BYTES);
            name += BUSTER_OBJECT_LOCAL_LABEL_BYTES;
            name += object_write_decimal(names + name, unnamed);
            names[name] = 0;
            name += 1;
            unnamed += 1;
        }
        entry += 1;
    }
    // The undefined symbols: no type but TLS, as GNU as writes them, with their binding and visibility.
    const SymbolId* undefined = builder->undefined;
    for (uint64_t i = 0; i < builder->undefined_count; i += 1)
    {
        const SymbolId symbol = undefined[i];
        const uint32_t name_bytes_with_nul = symbol_name_bytes[symbol];
        const uint8_t flags = symbol_flags[symbol];
        const bool tls = (flags & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
        *entry = (Elf64_Sym){
            .st_name = name_bytes_with_nul != 0 ? (uint32_t)name : 0,
            .st_info = (flags & BUSTER_LINK_SYMBOL_BINDING) | (tls ? STT_TLS : STT_NOTYPE),
            .st_other = flags >> BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT,
            .st_shndx = SHN_UNDEF,
        };
        if (name_bytes_with_nul != 0)
        {
            object_copy_name(names + name, name_bytes[symbol_names[symbol]], name_bytes_with_nul);
            name += name_bytes_with_nul;
        }
        entry += 1;
    }
}

// The ELF header, .note.GNU-stack's nothing, .comment, .shstrtab and the section headers.
static void object_fill_headers(const ObjectBuilder* builder, uint8_t* bytes)
{
    *(Elf64_Ehdr*)bytes = (Elf64_Ehdr){
        .e_ident = {
            ELFMAG0,
            ELFMAG1,
            ELFMAG2,
            ELFMAG3,
            ELFCLASS64,
            ELFDATA2LSB,
            EV_CURRENT,
            builder->units->unit_abi[builder->unit],
        },
        .e_type = ET_REL,
        .e_machine = EM_X86_64,
        .e_version = EV_CURRENT,
        .e_shoff = builder->headers_offset,
        .e_ehsize = sizeof(Elf64_Ehdr),
        .e_shentsize = sizeof(Elf64_Shdr),
        .e_shnum = builder->header_count < SHN_LORESERVE ? (uint16_t)builder->header_count : 0,
        .e_shstrndx = builder->section_names_index < SHN_LORESERVE ? (uint16_t)builder->section_names_index : SHN_XINDEX,
    };
    uint8_t* comment = bytes + builder->headers[builder->comment_index].sh_offset;
    comment[0] = 0;
    memcpy(comment + 1, BUSTER_OBJECT_COMMENT, sizeof(BUSTER_OBJECT_COMMENT));

    // .shstrtab: a NUL, each section of atoms' name (after ".rela" if it has relocations), then the trailing sections'.
    uint8_t* names = bytes + builder->headers[builder->section_names_index].sh_offset;
    uint64_t name = 1;
    names[0] = 0;
    for (uint32_t s = 0; s < builder->section_count; s += 1)
    {
        const ObjectSection* section = &builder->sections[s];
        if (section->relocation_index != 0)
        {
            memcpy(names + name, BUSTER_OBJECT_RELA_PREFIX, BUSTER_OBJECT_RELA_PREFIX_BYTES);
            name += BUSTER_OBJECT_RELA_PREFIX_BYTES;
        }
        const NameId atom_name = builder->units->atom_name[builder->units->atoms[section->atom_begin]];
        if (section->explicit_section != 0)
        {
            const String8 text = section->explicit_section->name;
            memcpy(names + name, text.pointer, text.length);
            names[name + text.length] = 0;
            name += text.length + 1;
        }
        else if (builder->options.function_sections && section->class == BUSTER_LINK_CLASS_TEXT && atom_name != 0)
        {
            memcpy(names + name, BUSTER_OBJECT_FUNCTION_SECTION_PREFIX, BUSTER_OBJECT_FUNCTION_SECTION_PREFIX_BYTES);
            memcpy(names + name + BUSTER_OBJECT_FUNCTION_SECTION_PREFIX_BYTES, builder->link->names.bytes[atom_name], (uint64_t)builder->link->names.length[atom_name] + 1);
            name += BUSTER_OBJECT_FUNCTION_SECTION_PREFIX_BYTES + (uint64_t)builder->link->names.length[atom_name] + 1;
        }
        else
        {
            const uint64_t bytes_with_nul = strlen(object_class_sections[section->class].name) + 1;
            memcpy(names + name, object_class_sections[section->class].name, bytes_with_nul);
            name += bytes_with_nul;
        }
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(object_trailing_sections); i += 1)
    {
        const uint64_t bytes_with_nul = strlen(object_trailing_sections[i]) + 1;
        memcpy(names + name, object_trailing_sections[i], bytes_with_nul);
        name += bytes_with_nul;
    }
    if (builder->extended_index != 0)
    {
        memcpy(names + name, BUSTER_OBJECT_EXTENDED_SECTION, sizeof(BUSTER_OBJECT_EXTENDED_SECTION));
    }
    memcpy(bytes + builder->headers_offset, builder->headers, builder->header_count * sizeof(Elf64_Shdr));
}

static Object object_build(const Link* link, ObjectUnits* units, ObjectSymbols* symbols, uint32_t unit, ObjectOptions options, String8 source_name, Arena* arena)
{
    Object result = { 0 };
    Scratch scratch = scratch_begin(arena);
    ObjectBuilder builder = {
        .link = link,
        .units = units,
        .unit = unit,
        .options = options,
        .source_name = source_name,
        .symbols = symbols,
        .error = OBJECT_ERROR_NONE,
    };
    bool built = unit < units->count && (options.atom_sections == 0 || options.atom_section_count == link->atoms.count) &&
                 (options.temporary_symbols == 0 || options.temporary_symbol_count == link->symbols.count);
    if (!built)
    {
        builder.error = OBJECT_ERROR_RANGE;
    }
    if (built)
    {
        // A section per atom at most, and one per class.
        const uint64_t key = (uint64_t)unit * BUSTER_LINK_CLASS_COUNT;
        const uint64_t atom_count = units->atom_begin[key + BUSTER_LINK_CLASS_COUNT] - units->atom_begin[key];
        builder.sections = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ObjectSection, atom_count + BUSTER_LINK_CLASS_COUNT);
        built = builder.sections != 0;
        builder.error = built ? OBJECT_ERROR_NONE : OBJECT_ERROR_FULL;
    }
    built = built && object_count_sections(&builder);
    built = built && object_count_symbols(&builder, scratch.arena);
    built = built && object_place(&builder, scratch.arena);
    uint8_t* bytes = built ? arena_push(arena, builder.size, 64) : 0;
    if (built && bytes == 0)
    {
        builder.error = OBJECT_ERROR_FULL;
        built = false;
    }
    if (built)
    {
        // The gaps between the ranges are zero; then every range is filled once.
        uint64_t end = sizeof(Elf64_Ehdr);
        for (uint64_t i = 1; i < builder.header_count; i += 1)
        {
            const Elf64_Shdr* header = &builder.headers[i];
            if (header->sh_type != SHT_NOBITS)
            {
                memset(bytes + end, 0, header->sh_offset - end);
                end = header->sh_offset + header->sh_size;
            }
        }
        memset(bytes + end, 0, builder.headers_offset - end);
        object_fill_sections(&builder, bytes);
        object_fill_relocations(&builder, bytes);
        object_fill_symbols(&builder, bytes);
        object_fill_headers(&builder, bytes);
        result = (Object){
            .bytes = bytes,
            .size = builder.size,
            .section_count = builder.header_count,
            .symbol_count = builder.symbol_count,
            .relocation_count = builder.fixup_count,
        };
    }
    result.error = builder.error;
    scratch_end(scratch);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Writing

// The new file beside one with other links, in the same directory: mkstemp's template, under NAME_MAX whatever the
// destination's name is.
#define BUSTER_OBJECT_TEMPORARY_NAME ".buster-object-XXXXXX"

// One open, with O_CREAT but never O_TRUNC, which on btrfs costs as much as writing again; then by what fstat finds
// (docs/object-writer-and-linker.md, "The object writer (-c)"):
//   - a regular file with one link, or the empty one open just made, is written in place, and truncated only if it was
//     longer: in place is 1.6 to 2.2 times as fast as a new file on the 9700X, and ftruncate to the same size isn't free
//     either (a transaction on btrfs, about 1 µs), so a rebuild's object, which keeps its size or grows, skips it;
//   - a regular file with other links gets a new file beside it, with its mode, renamed over it, so that the other links
//     keep the old contents;
//   - anything else is written as it is, in order: write rather than pwrite, which a pipe or a terminal refuses.
// One write: an object is one buffer, and pwrite or write returns early only for more than 2 GB, on an error, or into a
// pipe, which the loop handles.
static ObjectError object_write_file(const char* path, const uint8_t* bytes, uint64_t size)
{
    ObjectError result = OBJECT_ERROR_NONE;
    char temporary[PATH_MAX];
    bool replace = false;
    bool truncate = false;
    bool sequential = false;
    struct stat stats;
    int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
    if (fd == -1 || fstat(fd, &stats) != 0)
    {
        result = OBJECT_ERROR_FILE;
    }
    else if (S_ISREG(stats.st_mode) && stats.st_nlink > 1)
    {
        close(fd);
        fd = -1;
        const char* slash = strrchr(path, '/');
        const int directory_bytes = slash != 0 ? (int)(slash - path) + 1 : 0;
        const int length = snprintf(temporary, sizeof(temporary), "%.*s%s", directory_bytes, path, BUSTER_OBJECT_TEMPORARY_NAME);
        replace = length > 0 && length < (int)sizeof(temporary);
        fd = replace ? mkstemp(temporary) : -1;
        if (fd == -1 || fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 || fchmod(fd, stats.st_mode & 07777) != 0)
        {
            result = OBJECT_ERROR_FILE;
        }
    }
    else
    {
        truncate = S_ISREG(stats.st_mode) && (uint64_t)stats.st_size > size;
        sequential = !S_ISREG(stats.st_mode);
    }
    uint64_t written = 0;
    while (result == OBJECT_ERROR_NONE && written < size)
    {
        const ssize_t count = sequential ? write(fd, bytes + written, size - written) : pwrite(fd, bytes + written, size - written, (off_t)written);
        if (count > 0)
        {
            written += (uint64_t)count;
        }
        else if (count == 0 || errno != EINTR)
        {
            result = OBJECT_ERROR_FILE;
        }
    }
    if (result == OBJECT_ERROR_NONE && truncate && ftruncate(fd, (off_t)size) != 0)
    {
        result = OBJECT_ERROR_FILE;
    }
    // A failed close loses nothing written to a local file, but report it: on NFS it is where errors show.
    if (fd != -1 && close(fd) != 0 && result == OBJECT_ERROR_NONE)
    {
        result = OBJECT_ERROR_FILE;
    }
    if (replace && fd != -1)
    {
        if (result != OBJECT_ERROR_NONE || rename(temporary, path) != 0)
        {
            const int error = errno;
            unlink(temporary);
            errno = error;
            result = OBJECT_ERROR_FILE;
        }
    }
    return result;
}

static bool object_write_units(const Link* link, ObjectUnits* units, ObjectOptions options, const char* const* paths, const String8* source_names, ObjectError* errors)
{
    Scratch scratch = scratch_begin(0);
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)BUSTER_LANE_PARTS_PER_LANE * lane_count());
    ObjectSymbols symbols;
    const bool ready = order != 0 && object_map_symbols(&symbols, link, scratch.arena);
    // Every lane needs its order to take parts, and its symbols, so a lane without them makes all of them give up,
    // together: each lane hears every lane's (and makes every broadcast, which waits for all of them).
    bool result = true;
    for (uint32_t lane = 0; lane < lane_count(); lane += 1)
    {
        result = lane_broadcast(ready ? 0 : 1, lane) == 0 && result;
    }
    if (result)
    {
        const uint64_t parts = lane_parts_by_cost(units->cost_prefix, units->count, units->part_boundaries, order);
        for (uint64_t taken = lane_take(parts); taken < parts; taken = lane_take(parts))
        {
            const uint32_t part = order[taken];
            for (uint64_t unit = units->part_boundaries[part]; unit < units->part_boundaries[part + 1]; unit += 1)
            {
                const uint64_t length = scratch.arena->length;
                const Object object = object_build(link, units, &symbols, (uint32_t)unit, options, source_names[unit], scratch.arena);
                errors[unit] = object.error == OBJECT_ERROR_NONE ? object_write_file(paths[unit], object.bytes, object.size) : object.error;
                arena_pop_to(scratch.arena, length);
            }
        }
        lane_sync();
        for (uint64_t unit = 0; unit < units->count; unit += 1)
        {
            result = result && errors[unit] == OBJECT_ERROR_NONE;
        }
    }
    else
    {
        // No object is written: every unit's error says so, each lane writing a range of them before any lane returns.
        const Range64 range = lane_range(units->count);
        for (uint64_t unit = range.begin; unit < range.end; unit += 1)
        {
            errors[unit] = OBJECT_ERROR_FULL;
        }
        lane_sync();
    }
    scratch_end(scratch);
    return result;
}
