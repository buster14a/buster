// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The import path's readers (docs/object-writer-and-linker.md, "Linking other compilers' objects" and "The runtime
// snapshot"): ELF relocatable objects into the link representation (link.c), archives' members and symbol index,
// shared libraries' dynamic symbols and version definitions, and the commands of the linker scripts that libc.so and
// GCC's _asneeded scripts are. Step 5 imports the C runtime's files with them (runtime.c); step 7 extends them to
// foreign inputs, with resolution, COMDAT groups, relaxations and the vector parse.
//
// Every reader takes a file's bytes as a slice and checks each offset, size, count and index it reads against the
// slice or the table it indexes, with checked arithmetic, before it reads there. A malformed file gets an ImportResult
// that says what is wrong and where (the section, symbol or relocation index, and the byte offset), never a read
// outside its bytes. Fields are read with memcpy, so nothing is assumed about the bytes' alignment.
//
// Objects (import_object), into the tables of a link with room for them (import_object_capacity says how much):
//   - Sections: each allocated section the representation has a class for becomes one atom of that class, the class lld
//     would give its output section, by type, flags and name (import_section_class). Its bytes point into the file's
//     bytes, which the caller keeps for as long as the link and lets the link write: an executable patches atoms in
//     place. Unallocated sections, notes and .sframe are dropped: GCC 16's crt objects have an .sframe, which lld copies
//     and GNU ld merges, and which the representation has no class for (libc.a's .gnu.warning.* and .note.stapsdt go
//     too). An init or fini array's .init_array.N or .fini_array.N gets priority N (link_add_priority). SHT_REL and
//     SHT_PREINIT_ARRAY sections, and .ctors and .dtors, which linkers would move into the arrays reversed, are
//     unsupported.
//   - COMDAT groups (libc.a's DW.ref.__gcc_personality_v0 and .stapsdt.base): each becomes an ImportGroup with its
//     sections, which are imported as any others; the caller keeps the first group of each signature and discards the
//     rest. A group without GRP_COMDAT, and a section with SHF_GROUP in no group or in two, are unsupported.
//   - Symbols: each one defined in an atom becomes a symbol of the given source there, with its name, value, size,
//     type (an IFUNC's among them), binding and visibility; each absolute one (SHN_ABS), a symbol of source ABSOLUTE
//     with its value and without an atom; each undefined one, a symbol of source NONE without an atom; and each section
//     symbol, an unnamed local at its atom's start, which relocations against the section refer to. FILE symbols, and
//     locals defined in dropped sections, become none. Common and GNU_UNIQUE symbols, and absolute TLS symbols and
//     IFUNCs, are unsupported.
//   - Relocations: each entry of an atom's .rela section becomes one of its fixups, of the kind its type names (the
//     inverse of the object writer's object_relocation_types), with its target and addend, in offset order: sorted
//     if the file's aren't. Other types are unsupported.
// Nothing is resolved: a reference to a name that another file defines stays a reference to this object's undefined
// symbol, which the caller binds.
//
// Archives (import_archive): GNU's format, with its symbol index (32- or 64-bit) and its table of long names. The
// members are slices of the archive's bytes, and the index maps each name to the member that defines it. A thin
// archive (!<thin>, ar's T) has the same index and long names, but its members' bytes are files of their own, named by
// paths relative to the archive's directory, and only their headers are in the archive: each member's bytes are then
// empty, at the end of its header, and ImportArchive.thin tells the caller to open the file its name gives.
//
// Shared libraries (import_library): the soname, the version definitions, and from .dynsym the names it defines, each
// with its default version (name@@VERSION, or unversioned), type and size, indexed by link_hash (ImportNameIndex), and
// the names it refers to without defining them, which decide what a program exports (docs/object-writer-and-linker.md,
// "Exports"), each with its .gnu.version index; and apart, the names it defines at versions other than their defaults
// (name@VERSION), which only a reference that requires that version binds to. An IFUNC is exported as a function, as
// linkers show it in a program's .dynsym.
//
// Each of the three has two paths (docs/object-writer-and-linker.md, "Step 5's phases, data-parallel"): a vector path
// (import_object_vector, import_archive_vector, import_library_vector), which reads eight records at a time in 64-bit
// lanes and gives up on anything out of the ordinary, and an exact path (import_object_scalar, import_archive_scalar,
// import_library_scalar), a record at a time, which reports what is wrong and where. The reader runs the vector path,
// and the exact one when the vector path gives up; the tests compare the two on every input they have.
//
// Linker scripts (import_script): OUTPUT_FORMAT, GROUP and INPUT, whose inputs are paths or -l names, each of which
// AS_NEEDED can mark, and comments. Any other command is an error.
//
// main.c includes this file after link.c, whose representation it fills, lane.c, whose arenas it uses, and
// object_writer.c, whose relocation types it reads the other way. import_object takes BUSTER_LINKER_ENTRY rather than
// static: main.c's units compile it in one unit alone (link.c). Layout, in this order:
//   - API: ImportError, ImportResult, the readers' outputs and the entry points' prototypes.
//   - Bytes: bounded reads, strings and the ELF header and section table (ImportElf).
//   - Objects: import_section_class, import_fixup_kind, import_object_capacity and import_object_scalar.
//   - Objects, the vector path: ImportMark, the vector helpers (import_load_records3, import_name_rows,
//     import_kind_table), the section, symbol and relocation passes, import_object_vector and import_object.
//   - Archives: import_archive_scalar, then import_archive_size, import_archive_vector and import_archive.
//   - Shared libraries: the names' index (import_exclusive_scan, import_index_build, import_index_bucket),
//     import_library_find, import_library_scalar, import_library_vector and import_library.
//   - Linker scripts: import_script.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <elf.h>

// SFrame's section type (binutils 2.40 and later), which older <elf.h> don't have.
#define BUSTER_IMPORT_SHT_GNU_SFRAME 0x6ffffff4

// ---------------------------------------------------------------------------------------------------------------------
// API

typedef enum ImportError
{
    IMPORT_ERROR_NONE,
    // Not a file of the kind asked for: not ELF, not 64-bit little-endian x86-64, not of the type asked for, or not an
    // archive.
    IMPORT_ERROR_FORMAT,
    // An offset, size, count or index outside the file or the table it indexes, or a string without its NUL.
    IMPORT_ERROR_BOUNDS,
    // Something valid that this step doesn't import: a section, symbol or relocation type, a symbol in an absolute or
    // common section, a section group, a BSD archive's member.
    IMPORT_ERROR_UNSUPPORTED,
    // The representation refused a record (link->error says why): a table full, relocations that overlap, a symbol
    // outside its section.
    IMPORT_ERROR_LINK,
    // An arena couldn't hold what the reader allocates.
    IMPORT_ERROR_MEMORY,
    // A linker script's command or token outside the subset, or one that doesn't parse.
    IMPORT_ERROR_SCRIPT,
} ImportError;

// A reader's outcome, and when it failed, where: the indices of the section, symbol and relocation (in its section)
// it is about, the byte offset in the file of the header, entry or token, and the type (section, symbol or relocation)
// that isn't supported. Each is 0 when the error isn't about one.
typedef struct ImportResult ImportResult;
struct ImportResult
{
    ImportError error;
    uint32_t section;
    uint32_t symbol;
    uint32_t relocation;
    uint32_t type;
    uint64_t offset;
};

// A COMDAT group (SHT_GROUP with GRP_COMDAT): its signature, the name of its symbol (or for a section symbol, of the
// section), and its sections, `count` indices from `first` in ImportObject.group_sections. A link keeps one group of a
// signature, the first, and discards its sections in the others.
typedef struct ImportGroup ImportGroup;
struct ImportGroup
{
    String8 signature;
    uint32_t first;
    uint32_t count;
};

// What import_object made of an object's sections and symbols, by their indices in the file: the atom each section
// became and the symbol each symbol table entry became, 0 for none; and its COMDAT groups, in section order.
typedef struct ImportObject ImportObject;
struct ImportObject
{
    AtomId* section_atoms;
    uint32_t section_count;
    // First exact .note.GNU-stack with SHF_EXECINSTR, by original section index; 0 for no explicit request.
    // This is an input requirement, even for a non-ALLOC, excluded or COMDAT-discarded marker. Final consumers must
    // honor it or refuse the input; neither dropping the marker nor collecting its object's code clears it.
    uint32_t executable_stack_section;
    SymbolId* symbols;
    uint32_t symbol_count;
    ImportGroup* groups;
    uint32_t group_count;
    uint32_t* group_sections;
};

typedef struct ImportMember ImportMember;
struct ImportMember
{
    String8 name;
    String8 bytes;
    // The offset of the member's header in the archive, which the symbol index names it by.
    uint64_t offset;
    // In a thin archive, for a member of another archive (GNU ar's "/N:M", whose long name is that archive's path): the
    // member's header's offset in it plus 1; 0 for any other member.
    uint64_t nested;
};

typedef struct ImportArchiveSymbol ImportArchiveSymbol;
struct ImportArchiveSymbol
{
    String8 name;
    uint32_t member;
};

typedef struct ImportArchive ImportArchive;
struct ImportArchive
{
    ImportMember* members;
    uint32_t member_count;
    ImportArchiveSymbol* symbols;
    uint32_t symbol_count;
    // A thin archive: each member's bytes are the file its name gives, relative to the archive's directory, and its
    // `bytes` are empty.
    bool thin;
};

// An archive's symbol index alone, for a link that loads only the members it needs: each entry's name, and in `member`
// the rank of its member among the members the index names (in archive order, which is the members' offsets' order);
// each such member's header offset; the long names' table, for their names; and whether the archive is thin. A
// member's header is read when it is loaded (import_archive_member).
typedef struct ImportArchiveIndex ImportArchiveIndex;
struct ImportArchiveIndex
{
    ImportArchiveSymbol* symbols;
    uint32_t symbol_count;
    uint64_t* member_offsets;
    uint32_t member_count;
    String8 long_names;
    bool thin;
    // Whether the archive has an index, and whether it has a member past its special ones.
    bool indexed;
    bool members;
};

// The names a shared library defines at their default versions, as columns: export i's name, a slice of the library's
// .dynstr, its length and link_hash; its size; its type and binding as BUSTER_LINK_SYMBOL_* (an IFUNC as a function,
// COMMON as an object, GNU_UNIQUE as a global); and its version's index in .gnu.version (an ImportVersion's), or
// VER_NDX_GLOBAL for an unversioned name.
typedef struct ImportExports ImportExports;
struct ImportExports
{
    const uint8_t** name;
    uint32_t* length;
    uint64_t* hash;
    uint64_t* size;
    uint8_t* flags;
    uint16_t* version;
    // The definition's address in the library and its section's index (st_value and st_shndx), which a copy relocation
    // needs to find the name's aliases and the copy's alignment, as GNU ld and lld find them. Only import_library fills
    // them: a runtime snapshot doesn't keep them (null), since the direct path makes no copies.
    uint64_t* value;
    uint16_t* section;
    uint32_t count;
};

// A version the library defines: its name, its hash as the ELF hash function gives it (vd_hash, which a program's
// .gnu.version_r repeats), its index in .gnu.version, and whether it is the library's own name (VER_FLG_BASE).
typedef struct ImportVersion ImportVersion;
struct ImportVersion
{
    String8 name;
    uint32_t hash;
    uint16_t index;
    bool base;
};

// The names a shared library refers to without defining them, as columns: each one's name, length and link_hash,
// whether the reference is weak, and its index in .gnu.version without the hidden bit (VER_NDX_GLOBAL for a library
// without .gnu.version), which names the version it requires, if any, in .gnu.version_r. Only import_library fills the
// indices: a runtime snapshot doesn't keep them (null).
typedef struct ImportReferences ImportReferences;
struct ImportReferences
{
    const uint8_t** name;
    uint32_t* length;
    uint64_t* hash;
    uint8_t* weak;
    uint16_t* version;
    uint32_t count;
};

// The names a shared library defines at versions other than their defaults (name@VERSION, a .gnu.version entry with
// the hidden bit), which its exports leave out, and which the loader binds only a reference that requires that version
// to: each one's name, a slice of the library's .dynstr, and its version's index, one the library defines (an
// ImportVersion's), as an export's is. Only import_library fills them: a runtime snapshot doesn't keep them.
typedef struct ImportHiddenExports ImportHiddenExports;
struct ImportHiddenExports
{
    const uint8_t** name;
    uint16_t* version;
    uint32_t count;
};

// Names by hash, for a library's exports or a link's definitions: 2^bucket_bits buckets by the hash's top bits, bucket
// b's items at order[start[b]] to order[start[b + 1] - 1] in item order, each with its hash beside it in `hash`, which
// a lookup compares before the name's bytes. A bucket holds about one item.
typedef struct ImportNameIndex ImportNameIndex;
struct ImportNameIndex
{
    uint32_t* start;
    uint32_t* order;
    uint64_t* hash;
    uint32_t bucket_bits;
};

typedef struct ImportLibrary ImportLibrary;
struct ImportLibrary
{
    String8 soname;
    ImportVersion* versions;
    uint32_t version_count;
    ImportExports exports;
    ImportReferences references;
    ImportHiddenExports hidden;
    // The exports by their names' hashes.
    ImportNameIndex index;
};

typedef struct ImportScriptInput ImportScriptInput;
struct ImportScriptInput
{
    // A path, or with `library` the name after -l.
    String8 name;
    bool library;
    bool as_needed;
};

typedef struct ImportScript ImportScript;
struct ImportScript
{
    ImportScriptInput* inputs;
    uint32_t input_count;
};

// How many records importing the object can add to each table: what a link the caller makes for it needs room for.
static __attribute__((unused)) ImportResult import_object_capacity(String8 bytes, LinkCapacity* capacity);
// Imports a relocatable object into `link` as input `unit`, its defined symbols of `source`; object->section_atoms and
// symbols are pushed from `arena`. On failure, what was added before it stays in the link. The vector path
// (import_object_vector) imports it, or if anything in it is out of the ordinary, import_object_scalar does.
BUSTER_LINKER_ENTRY __attribute__((unused)) ImportResult import_object(Link* link, String8 bytes, uint32_t unit, uint8_t source, Arena* arena, ImportObject* object);
// The exact path: the same import a record at a time, which reports what is wrong and where. It is the oracle the tests
// compare the vector path with.
static __attribute__((unused)) ImportResult import_object_scalar(Link* link, String8 bytes, uint32_t unit, uint8_t source, Arena* arena, ImportObject* object);
// The vector path: true if it imported the object exactly as import_object_scalar would have. Otherwise it may have
// written past the tables' counts and pushed from the arenas, which import_object takes back before the exact path.
static __attribute__((unused)) bool import_object_vector(Link* link, String8 bytes, uint32_t unit, uint8_t source, Arena* arena, ImportObject* object);
// Reads an archive's members and symbol index; the arrays are pushed from `arena`, the names and bytes are slices of
// the archive's. The vector path (import_archive_vector) reads them, or when the index doesn't account for every
// member, import_archive_scalar does.
static __attribute__((unused)) ImportResult import_archive(ImportArchive* archive, String8 bytes, Arena* arena);
// The exact path, which walks every member's header: the vector path's oracle in the tests.
static __attribute__((unused)) ImportResult import_archive_scalar(ImportArchive* archive, String8 bytes, Arena* arena);
// The vector path: true if it read the archive exactly as import_archive_scalar would have. Otherwise it may have
// pushed from the arena, which import_archive takes back before the exact path.
static __attribute__((unused)) bool import_archive_vector(ImportArchive* archive, String8 bytes, Arena* arena);
// Reads an archive's symbol index alone (ImportArchiveIndex), without its members' headers: an archive without an index
// has no symbols and no members in it. The vector path (import_archive_lazy_vector) reads it, or when anything is out
// of the ordinary, import_archive_lazy_scalar does.
static __attribute__((unused)) ImportResult import_archive_lazy(ImportArchiveIndex* index, String8 bytes, Arena* arena);
// The exact path, the vector path's twin: it also takes an index out of archive order, which it sorts.
static __attribute__((unused)) ImportResult import_archive_lazy_scalar(ImportArchiveIndex* index, String8 bytes, Arena* arena);
// The vector path: true if it read the index exactly as import_archive_lazy_scalar would have.
static __attribute__((unused)) bool import_archive_lazy_vector(ImportArchiveIndex* index, String8 bytes, Arena* arena);
// The header of the indexed member of rank `rank`: its name, its bytes (empty for a thin archive's member) and offset.
// A header that doesn't read, or a special member's, fails at its offset.
static __attribute__((unused)) ImportResult import_archive_member(ImportMember* member, String8 bytes, const ImportArchiveIndex* index, uint32_t rank);
// Reads a shared library's soname, versions, exports and references, and indexes the exports by hash; the arrays are
// pushed from `arena`, the names are slices of the library's bytes. The vector path (import_library_vector) reads
// them, or if anything in .dynsym is out of the ordinary, import_library_scalar does.
static __attribute__((unused)) ImportResult import_library(ImportLibrary* library, String8 bytes, Arena* arena);
// The exact path, an entry at a time, which reports what is wrong and where: the vector path's oracle in the tests.
static __attribute__((unused)) ImportResult import_library_scalar(ImportLibrary* library, String8 bytes, Arena* arena);
// The vector path: true if it read the library exactly as import_library_scalar would have. Otherwise it may have
// pushed from the arena, which import_library takes back before the exact path.
static __attribute__((unused)) bool import_library_vector(ImportLibrary* library, String8 bytes, Arena* arena);
// The index of the library's first export of that name plus 1, or 0 if it doesn't export it.
static __attribute__((unused)) uint32_t import_library_find(const ImportLibrary* library, const uint8_t* name, uint64_t length, uint64_t hash);
// Indexes `count` items by their hashes (ImportNameIndex), pushed from `arena`; false if it can't hold them.
static __attribute__((unused)) bool import_index_build(ImportNameIndex* index, const uint64_t* hashes, uint32_t count, Arena* arena);
// The places in index->order of the bucket of `hash`: from *begin to *end.
static __attribute__((unused)) void import_index_bucket(const ImportNameIndex* index, uint64_t hash, uint32_t* begin, uint32_t* end);

// Reads a linker script's inputs; the array is pushed from `arena`, the names are slices of the script's bytes.
static __attribute__((unused)) ImportResult import_script(ImportScript* script, String8 bytes, Arena* arena);

// ---------------------------------------------------------------------------------------------------------------------
// Bytes

static ImportResult import_fail(ImportError error, uint64_t offset)
{
    ImportResult result = {
        .error = error,
        .offset = offset,
    };
    return result;
}

// Whether [offset, offset + size) lies within `length` bytes.
static bool import_within(uint64_t length, uint64_t offset, uint64_t size)
{
    return offset <= length && size <= length - offset;
}

// The string at `offset` in a string table, without its NUL: false if the offset is outside the table or no NUL ends
// the string in it.
static bool import_string(String8 table, uint64_t offset, String8* string)
{
    bool result = offset < table.length;
    if (result)
    {
        const char* start = table.pointer + offset;
        const char* end = memchr(start, 0, table.length - offset);
        result = end != 0;
        string->pointer = (char*)start;
        string->length = result ? (uint64_t)(end - start) : 0;
    }
    return result;
}

static bool import_equal(String8 string, const char* text)
{
    const uint64_t length = strlen(text);
    return string.length == length && memcmp(string.pointer, text, length) == 0;
}

static bool import_starts_with(String8 string, const char* text)
{
    const uint64_t length = strlen(text);
    return string.length >= length && memcmp(string.pointer, text, length) == 0;
}

// An ELF file's header and section table, checked: the section headers lie within the file, and so does each
// section's contents (but a NOBITS section's), and the section names' table is a string table.
typedef struct ImportElf ImportElf;
struct ImportElf
{
    String8 bytes;
    Elf64_Ehdr header;
    uint64_t section_offset;
    uint32_t section_count;
    String8 section_names;
};

static Elf64_Shdr import_section_header(const ImportElf* elf, uint32_t index)
{
    Elf64_Shdr header;
    memcpy(&header, elf->bytes.pointer + elf->section_offset + (uint64_t)index * sizeof(Elf64_Shdr), sizeof(header));
    return header;
}

// A section's contents: within the file, as import_elf checked, or empty for NOBITS.
static String8 import_section_bytes(const ImportElf* elf, const Elf64_Shdr* header)
{
    String8 result = { 0 };
    if (header->sh_type != SHT_NOBITS)
    {
        result.pointer = elf->bytes.pointer + header->sh_offset;
        result.length = header->sh_size;
    }
    return result;
}

// Reads and checks the header of an ELF file of `type` (ET_REL or ET_DYN): 64-bit, little-endian, x86-64, the current
// version, with a table of Elf64_Shdr entries within the file. Past SHN_LORESERVE sections, the count and the names'
// index (*names) come from section 0, as the gABI has it. The sections themselves are import_elf's to check.
static ImportResult import_elf_header(ImportElf* elf, String8 bytes, uint16_t type, uint32_t* names)
{
    ImportResult result = { 0 };
    *elf = (ImportElf){ 0 };
    *names = 0;
    elf->bytes = bytes;
    const bool header_fits = bytes.length >= sizeof(Elf64_Ehdr);
    if (header_fits)
    {
        memcpy(&elf->header, bytes.pointer, sizeof(Elf64_Ehdr));
    }
    const Elf64_Ehdr* header = &elf->header;
    const bool identified = header_fits && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 && header->e_ident[EI_CLASS] == ELFCLASS64 &&
                            header->e_ident[EI_DATA] == ELFDATA2LSB && header->e_ident[EI_VERSION] == EV_CURRENT;
    if (!identified || header->e_type != type || header->e_machine != EM_X86_64 || header->e_version != EV_CURRENT)
    {
        result = import_fail(IMPORT_ERROR_FORMAT, 0);
    }
    else if (header->e_shoff == 0 || header->e_shentsize != sizeof(Elf64_Shdr) || !import_within(bytes.length, header->e_shoff, sizeof(Elf64_Shdr)))
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, offsetof(Elf64_Ehdr, e_shoff));
    }
    else
    {
        elf->section_offset = header->e_shoff;
        elf->section_count = 1;
        const Elf64_Shdr first = import_section_header(elf, 0);
        const uint64_t count = header->e_shnum != 0 ? header->e_shnum : first.sh_size;
        *names = header->e_shstrndx != SHN_XINDEX ? header->e_shstrndx : first.sh_link;
        if (count == 0 || count > UINT32_MAX || !import_within(bytes.length, header->e_shoff, count * sizeof(Elf64_Shdr)))
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, offsetof(Elf64_Ehdr, e_shnum));
        }
        else
        {
            elf->section_count = (uint32_t)count;
        }
    }
    return result;
}

// Whether section `names` can be the section names' table: a string table within the file. import_elf checks its bytes
// with every other section's, and the vector path before it reads the names.
static bool import_elf_names(ImportElf* elf, uint32_t names)
{
    const bool result = names != SHN_UNDEF && names < elf->section_count && import_section_header(elf, names).sh_type == SHT_STRTAB;
    if (result)
    {
        const Elf64_Shdr table = import_section_header(elf, names);
        elf->section_names = import_within(elf->bytes.length, table.sh_offset, table.sh_size) ? import_section_bytes(elf, &table) : (String8){ 0 };
    }
    return result;
}

// Reads and checks the header and the section table of an ELF file of `type` (import_elf_header), each section's
// contents within the file (but a NOBITS section's), and the section names' table.
static ImportResult import_elf(ImportElf* elf, String8 bytes, uint16_t type)
{
    uint32_t names = 0;
    ImportResult result = import_elf_header(elf, bytes, type, &names);
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < elf->section_count; index += 1)
    {
        const Elf64_Shdr section = import_section_header(elf, index);
        if (section.sh_type != SHT_NOBITS && !import_within(bytes.length, section.sh_offset, section.sh_size))
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, elf->section_offset + (uint64_t)index * sizeof(Elf64_Shdr));
            result.section = index;
        }
    }
    if (result.error == IMPORT_ERROR_NONE && !import_elf_names(elf, names))
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, offsetof(Elf64_Ehdr, e_shstrndx));
    }
    return result;
}

// A section's name; false if it isn't a string of the names' table.
static bool import_section_name(const ImportElf* elf, const Elf64_Shdr* header, String8* name)
{
    return import_string(elf->section_names, header->sh_name, name);
}

// The table of `entry_size` entries a section holds: its entry count, or false if its size isn't a multiple of
// entry_size or its sh_entsize says another size.
static bool import_table(const Elf64_Shdr* header, uint64_t entry_size, uint64_t* count)
{
    const bool result = (header->sh_entsize == entry_size || header->sh_entsize == 0) && header->sh_size % entry_size == 0;
    *count = result ? header->sh_size / entry_size : 0;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Objects

// The bytes of a section's name the section pass compares: the longest of import_section_patterns fits.
#define BUSTER_IMPORT_PATTERN_BYTES 16

// The section names the classes and input metadata depend on, each with how many of its bytes a name must match:
// all of them and the NUL, or for .data.rel.ro, .ctors and .dtors, a prefix.
typedef enum ImportPattern
{
    IMPORT_PATTERN_INIT,
    IMPORT_PATTERN_FINI,
    IMPORT_PATTERN_EH_FRAME,
    IMPORT_PATTERN_SFRAME,
    IMPORT_PATTERN_DATA_RELRO,
    IMPORT_PATTERN_INIT_ARRAY,
    IMPORT_PATTERN_FINI_ARRAY,
    IMPORT_PATTERN_CTORS,
    IMPORT_PATTERN_DTORS,
    IMPORT_PATTERN_GNU_STACK,
    IMPORT_PATTERNS,
} ImportPattern;

static const char import_section_patterns[IMPORT_PATTERNS][BUSTER_IMPORT_PATTERN_BYTES] = {
    [IMPORT_PATTERN_INIT] = ".init",
    [IMPORT_PATTERN_FINI] = ".fini",
    [IMPORT_PATTERN_EH_FRAME] = ".eh_frame",
    [IMPORT_PATTERN_SFRAME] = ".sframe",
    [IMPORT_PATTERN_DATA_RELRO] = ".data.rel.ro",
    [IMPORT_PATTERN_INIT_ARRAY] = ".init_array",
    [IMPORT_PATTERN_FINI_ARRAY] = ".fini_array",
    [IMPORT_PATTERN_CTORS] = ".ctors",
    [IMPORT_PATTERN_DTORS] = ".dtors",
    [IMPORT_PATTERN_GNU_STACK] = ".note.GNU-stack",
};

static const uint8_t import_section_pattern_bytes[IMPORT_PATTERNS] = {
    [IMPORT_PATTERN_INIT] = sizeof(".init"),
    [IMPORT_PATTERN_FINI] = sizeof(".fini"),
    [IMPORT_PATTERN_EH_FRAME] = sizeof(".eh_frame"),
    [IMPORT_PATTERN_SFRAME] = sizeof(".sframe"),
    [IMPORT_PATTERN_DATA_RELRO] = sizeof(".data.rel.ro") - 1,
    [IMPORT_PATTERN_INIT_ARRAY] = sizeof(".init_array"),
    [IMPORT_PATTERN_FINI_ARRAY] = sizeof(".fini_array"),
    [IMPORT_PATTERN_CTORS] = sizeof(".ctors") - 1,
    [IMPORT_PATTERN_DTORS] = sizeof(".dtors") - 1,
    [IMPORT_PATTERN_GNU_STACK] = sizeof(".note.GNU-stack"),
};

// A section without an init or fini array's priority.
#define BUSTER_IMPORT_NO_PRIORITY UINT32_MAX

// An init or fini array's priority from its section's name: none for `base` itself (.init_array), N for base.N with N
// of 1 to 5 decimal digits up to 65535 (GCC's .init_array.00101, clang's .init_array.101), and false for any other name.
static bool import_array_priority(String8 name, const char* base, uint32_t* priority)
{
    const uint64_t length = strlen(base);
    bool result = import_starts_with(name, base);
    uint32_t value = 0;
    *priority = BUSTER_IMPORT_NO_PRIORITY;
    if (result && name.length > length)
    {
        const uint64_t digits = name.length - length - 1;
        result = name.pointer[length] == '.' && digits >= 1 && digits <= 5;
        for (uint64_t i = 0; result && i < digits; i += 1)
        {
            const char digit = name.pointer[length + 1 + i];
            result = digit >= '0' && digit <= '9';
            value = value * 10 + (uint32_t)(digit - '0');
        }
        result = result && value <= UINT16_MAX;
        *priority = result ? value : BUSTER_IMPORT_NO_PRIORITY;
    }
    return result;
}

// The class lld would give an input section's output section, from its type, flags and name, or
// BUSTER_LINK_CLASS_NONE for one that is dropped (a group's own SHT_GROUP section among them); *supported is false for
// one that can't be imported, and *priority is an init or fini array's priority, or BUSTER_IMPORT_NO_PRIORITY.
static uint8_t import_section_class(const Elf64_Shdr* header, String8 name, bool* supported, uint32_t* priority)
{
    const uint64_t flags = header->sh_flags;
    const uint32_t type = header->sh_type;
    const bool allocated = (flags & SHF_ALLOC) != 0;
    const bool contents = type == SHT_PROGBITS || type == SHT_NOBITS || type == SHT_INIT_ARRAY || type == SHT_FINI_ARRAY || type == SHT_X86_64_UNWIND;
    uint8_t result = BUSTER_LINK_CLASS_NONE;
    *supported = true;
    *priority = BUSTER_IMPORT_NO_PRIORITY;
    if (type == SHT_REL || type == SHT_PREINIT_ARRAY || import_starts_with(name, import_section_patterns[IMPORT_PATTERN_CTORS]) ||
        import_starts_with(name, import_section_patterns[IMPORT_PATTERN_DTORS]))
    {
        *supported = false;
    }
    else if (type == SHT_GROUP || !allocated || type == SHT_NOTE || type == BUSTER_IMPORT_SHT_GNU_SFRAME || import_equal(name, import_section_patterns[IMPORT_PATTERN_SFRAME]))
    {
        result = BUSTER_LINK_CLASS_NONE;
    }
    else if (!contents)
    {
        *supported = false;
    }
    else if (type == SHT_X86_64_UNWIND || import_equal(name, import_section_patterns[IMPORT_PATTERN_EH_FRAME]))
    {
        result = BUSTER_LINK_CLASS_EH_FRAME;
    }
    else if (type == SHT_INIT_ARRAY)
    {
        *supported = import_array_priority(name, import_section_patterns[IMPORT_PATTERN_INIT_ARRAY], priority);
        result = BUSTER_LINK_CLASS_INIT_ARRAY;
    }
    else if (type == SHT_FINI_ARRAY)
    {
        *supported = import_array_priority(name, import_section_patterns[IMPORT_PATTERN_FINI_ARRAY], priority);
        result = BUSTER_LINK_CLASS_FINI_ARRAY;
    }
    else if ((flags & SHF_TLS) != 0)
    {
        result = type == SHT_NOBITS ? BUSTER_LINK_CLASS_TBSS : BUSTER_LINK_CLASS_TDATA;
    }
    else if ((flags & SHF_EXECINSTR) != 0)
    {
        const bool init = import_equal(name, import_section_patterns[IMPORT_PATTERN_INIT]);
        const bool fini = import_equal(name, import_section_patterns[IMPORT_PATTERN_FINI]);
        result = init ? BUSTER_LINK_CLASS_INIT : fini ? BUSTER_LINK_CLASS_FINI : BUSTER_LINK_CLASS_TEXT;
    }
    else if ((flags & SHF_WRITE) != 0)
    {
        const bool relro = import_starts_with(name, import_section_patterns[IMPORT_PATTERN_DATA_RELRO]);
        result = type == SHT_NOBITS ? BUSTER_LINK_CLASS_BSS : relro ? BUSTER_LINK_CLASS_DATA_RELRO : BUSTER_LINK_CLASS_DATA;
    }
    else if (type == SHT_NOBITS)
    {
        *supported = false;
    }
    else
    {
        const bool strings = (flags & (SHF_MERGE | SHF_STRINGS)) == (SHF_MERGE | SHF_STRINGS) && header->sh_entsize == 1;
        result = strings ? BUSTER_LINK_CLASS_STRINGS : BUSTER_LINK_CLASS_RODATA;
    }
    return result;
}

// The fixup kind a relocation type names (the inverse of the object writer's object_relocation_types), or
// BUSTER_LINK_FIXUP_NONE for one the representation doesn't have.
static uint8_t import_fixup_kind(uint32_t type)
{
    uint8_t result = BUSTER_LINK_FIXUP_NONE;
    for (uint8_t kind = 1; kind < BUSTER_LINK_FIXUP_KINDS; kind += 1)
    {
        result = object_relocation_types[kind] == type ? kind : result;
    }
    return result;
}

// The kind of a fixup that R_X86_64_GOTPCRELX or R_X86_64_REX_GOTPCRELX names (CALL_GOT or LOAD_GOT), from its
// instruction, the bytes of its atom before its field at `offset`: those kinds are the instructions a linker may
// rewrite (link.c), call or jmp [rip + slot] (ff 15, ff 25) and mov r64, [rip + slot] (REX.W 8b with a RIP-relative
// ModRM), whose field ends the instruction (an addend of -4). GNU as marks test, binops (libgcc.a's decimal floating
// point adds slots so) and a mov without REX.W with them too, and those read the slot as GOTPCREL does, which no linker
// rewrites in a PIE. Every other kind is its own.
static uint8_t import_got_kind(uint8_t kind, const uint8_t* bytes, uint64_t offset, int64_t addend)
{
    const bool call = kind == BUSTER_LINK_FIXUP_CALL_GOT && bytes != 0 && offset >= 2 && bytes[offset - 2] == 0xff && (bytes[offset - 1] == 0x15 || bytes[offset - 1] == 0x25);
    const bool load = kind == BUSTER_LINK_FIXUP_LOAD_GOT && bytes != 0 && offset >= 3 && (bytes[offset - 3] & 0xf8) == 0x48 && bytes[offset - 2] == 0x8b &&
                      (bytes[offset - 1] & 0xc7) == 0x05;
    const bool got = kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT;
    return got && !((call || load) && addend == -4) ? BUSTER_LINK_FIXUP_GOTPCREL : kind;
}

// An object's symbol table: its entries, its string table, and its extended section indices (SHT_SYMTAB_SHNDX) when
// it has them.
typedef struct ImportSymbols ImportSymbols;
struct ImportSymbols
{
    uint32_t section;
    String8 entries;
    uint32_t count;
    String8 names;
    String8 extended;
};

// Finds and checks the object's symbol table: exactly one, of Elf64_Sym entries, linked to a string table, with an
// extended index table, if there is one, of one entry per symbol.
static ImportResult import_symbols(const ImportElf* elf, ImportSymbols* symbols)
{
    ImportResult result = { 0 };
    *symbols = (ImportSymbols){ 0 };
    uint32_t found = 0;
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < elf->section_count; index += 1)
    {
        const Elf64_Shdr header = import_section_header(elf, index);
        uint64_t count = 0;
        if (header.sh_type == SHT_SYMTAB)
        {
            found += 1;
            const bool table = import_table(&header, sizeof(Elf64_Sym), &count) && count >= 1 && count <= UINT32_MAX;
            const bool linked = header.sh_link != SHN_UNDEF && header.sh_link < elf->section_count && import_section_header(elf, header.sh_link).sh_type == SHT_STRTAB;
            if (found > 1 || !table || !linked)
            {
                result = import_fail(found > 1 ? IMPORT_ERROR_UNSUPPORTED : IMPORT_ERROR_BOUNDS, elf->section_offset + (uint64_t)index * sizeof(Elf64_Shdr));
                result.section = index;
            }
            else
            {
                const Elf64_Shdr names = import_section_header(elf, header.sh_link);
                symbols->section = index;
                symbols->entries = import_section_bytes(elf, &header);
                symbols->count = (uint32_t)count;
                symbols->names = import_section_bytes(elf, &names);
            }
        }
    }
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && found == 1 && index < elf->section_count; index += 1)
    {
        const Elf64_Shdr header = import_section_header(elf, index);
        uint64_t count = 0;
        if (header.sh_type == SHT_SYMTAB_SHNDX && header.sh_link == symbols->section)
        {
            if (!import_table(&header, sizeof(uint32_t), &count) || count != symbols->count)
            {
                result = import_fail(IMPORT_ERROR_BOUNDS, elf->section_offset + (uint64_t)index * sizeof(Elf64_Shdr));
                result.section = index;
            }
            else
            {
                symbols->extended = import_section_bytes(elf, &header);
            }
        }
    }
    return result;
}

static Elf64_Sym import_symbol(const ImportSymbols* symbols, uint32_t index)
{
    Elf64_Sym symbol;
    memcpy(&symbol, symbols->entries.pointer + (uint64_t)index * sizeof(Elf64_Sym), sizeof(symbol));
    return symbol;
}

// A symbol's section index, through the extended table for SHN_XINDEX: false if the table is missing.
static bool import_symbol_section(const ImportSymbols* symbols, uint32_t index, const Elf64_Sym* symbol, uint32_t* section)
{
    *section = symbol->st_shndx;
    const bool extended = symbol->st_shndx == SHN_XINDEX;
    if (extended && symbols->extended.length != 0)
    {
        memcpy(section, symbols->extended.pointer + (uint64_t)index * sizeof(uint32_t), sizeof(*section));
    }
    return !extended || symbols->extended.length != 0;
}

static ImportResult import_object_capacity(String8 bytes, LinkCapacity* capacity)
{
    ImportElf elf;
    ImportResult result = import_elf(&elf, bytes, ET_REL);
    ImportSymbols symbols = { 0 };
    if (result.error == IMPORT_ERROR_NONE)
    {
        result = import_symbols(&elf, &symbols);
    }
    *capacity = (LinkCapacity){ 0 };
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < elf.section_count; index += 1)
    {
        const Elf64_Shdr header = import_section_header(&elf, index);
        uint64_t count = 0;
        // At most an atom per section, a priority per init or fini array, and a fixup per relocation, each of which may
        // need a wide addend.
        capacity->atoms += 1;
        capacity->priorities += header.sh_type == SHT_INIT_ARRAY || header.sh_type == SHT_FINI_ARRAY ? 1 : 0;
        if (header.sh_type == SHT_RELA && import_table(&header, sizeof(Elf64_Rela), &count) && count <= UINT32_MAX - capacity->fixups)
        {
            capacity->fixups += (uint32_t)count;
            capacity->wide_addends += (uint32_t)count;
        }
    }
    // At most a symbol and a name per entry of the symbol table, section symbols among them.
    if (result.error == IMPORT_ERROR_NONE)
    {
        capacity->names = symbols.count;
        capacity->symbols = symbols.count;
    }
    return result;
}

// Imports the sections: an atom for each one with a class.
static ImportResult import_sections(Link* link, const ImportElf* elf, uint32_t unit, ImportObject* object)
{
    ImportResult result = { 0 };
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < elf->section_count; index += 1)
    {
        const Elf64_Shdr header = import_section_header(elf, index);
        const uint64_t header_offset = elf->section_offset + (uint64_t)index * sizeof(Elf64_Shdr);
        String8 name = { 0 };
        bool supported = true;
        uint32_t priority = BUSTER_IMPORT_NO_PRIORITY;
        const bool named = import_section_name(elf, &header, &name);
        const uint8_t class = named ? import_section_class(&header, name, &supported, &priority) : BUSTER_LINK_CLASS_NONE;
        const uint64_t alignment = header.sh_addralign == 0 ? 1 : header.sh_addralign;
        const uint32_t alignment_log2 = (uint32_t)__builtin_ctzll(alignment);
        if (!named || (alignment & (alignment - 1)) != 0 || alignment_log2 > BUSTER_LINK_MAX_ALIGNMENT_LOG2)
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, header_offset);
            result.section = index;
        }
        else if (!supported)
        {
            result = import_fail(IMPORT_ERROR_UNSUPPORTED, header_offset);
            result.section = index;
            result.type = header.sh_type;
        }
        else if (class != BUSTER_LINK_CLASS_NONE)
        {
            const String8 contents = import_section_bytes(elf, &header);
            uint8_t* section_bytes = link_class_is_zero_fill(class) || header.sh_size == 0 ? 0 : (uint8_t*)contents.pointer;
            const bool zero_fill_matches = link_class_is_zero_fill(class) == (header.sh_type == SHT_NOBITS);
            const AtomId atom = zero_fill_matches ? link_add_atom(link, class, (uint8_t)alignment_log2, header.sh_size, section_bytes, unit) : 0;
            const bool prioritized = atom != 0 && (priority == BUSTER_IMPORT_NO_PRIORITY || link_add_priority(link, atom, priority));
            if (atom == 0 || !prioritized)
            {
                result = import_fail(zero_fill_matches ? IMPORT_ERROR_LINK : IMPORT_ERROR_UNSUPPORTED, header_offset);
                result.section = index;
                result.type = header.sh_type;
            }
            object->section_atoms[index] = atom;
        }
        if (result.error == IMPORT_ERROR_NONE && object->executable_stack_section == 0 && (header.sh_flags & SHF_EXECINSTR) != 0 &&
            import_equal(name, import_section_patterns[IMPORT_PATTERN_GNU_STACK]))
        {
            object->executable_stack_section = index;
        }
    }
    return result;
}

// The symbol flags of an ELF symbol's type, binding and visibility, or false for a type or binding the representation
// doesn't have (COMMON, GNU_UNIQUE).
static bool import_symbol_flags(const Elf64_Sym* symbol, uint8_t* flags)
{
    const uint32_t type = ELF64_ST_TYPE(symbol->st_info);
    const uint32_t binding = ELF64_ST_BIND(symbol->st_info);
    const uint32_t visibility = ELF64_ST_VISIBILITY(symbol->st_other);
    const bool type_known = type == STT_NOTYPE || type == STT_OBJECT || type == STT_FUNC || type == STT_TLS || type == STT_GNU_IFUNC;
    const bool binding_known = binding == STB_LOCAL || binding == STB_GLOBAL || binding == STB_WEAK;
    *flags = (uint8_t)((type & BUSTER_LINK_SYMBOL_TYPE) | (binding << 4) | (visibility << BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT));
    return type_known && binding_known;
}

// Imports the symbols: one for each that names a place in an atom, a value (SHN_ABS) or a reference, none for FILE
// symbols and locals of dropped sections.
static ImportResult import_object_symbols(Link* link, const ImportElf* elf, const ImportSymbols* symbols, uint8_t source, ImportObject* object)
{
    ImportResult result = { 0 };
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < symbols->count; index += 1)
    {
        const Elf64_Sym symbol = import_symbol(symbols, index);
        const uint64_t offset = (uint64_t)((const uint8_t*)symbols->entries.pointer - (const uint8_t*)elf->bytes.pointer) + (uint64_t)index * sizeof(Elf64_Sym);
        const uint32_t type = ELF64_ST_TYPE(symbol.st_info);
        const uint32_t binding = ELF64_ST_BIND(symbol.st_info);
        uint32_t section = SHN_UNDEF;
        const bool indexed = import_symbol_section(symbols, index, &symbol, &section);
        const bool reserved = section >= SHN_LORESERVE && symbol.st_shndx != SHN_XINDEX;
        const bool absolute = reserved && section == SHN_ABS;
        const AtomId atom = section != SHN_UNDEF && !reserved && section < elf->section_count ? object->section_atoms[section] : 0;
        String8 name = { 0 };
        uint8_t flags = 0;
        const bool named = symbol.st_name == 0 || import_string(symbols->names, symbol.st_name, &name);
        const bool known = import_symbol_flags(&symbol, &flags);
        SymbolId added = 0;
        bool failed = false;
        if (!named || !indexed || (section != SHN_UNDEF && !reserved && section >= elf->section_count))
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, offset);
        }
        else if (type == STT_FILE || (type == STT_SECTION && atom == 0) || (binding == STB_LOCAL && section != SHN_UNDEF && !reserved && atom == 0))
        {
            added = 0;
        }
        else if (type == STT_SECTION)
        {
            // The section's own symbol: an unnamed local at the atom's start, a TLS symbol for a TLS section.
            const uint8_t tls = link_class_is_tls(link->atoms.class[atom]) ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_NOTYPE;
            added = link_add_symbol(link, 0, atom, 0, 0, (uint8_t)(BUSTER_LINK_SYMBOL_LOCAL | tls), source);
            failed = added == 0;
        }
        else if (!known || (reserved && !absolute) || (absolute && !link_absolute_fits(0, flags)) || (section != SHN_UNDEF && !reserved && atom == 0) ||
                 (section == SHN_UNDEF && binding == STB_LOCAL))
        {
            result = import_fail(IMPORT_ERROR_UNSUPPORTED, offset);
            result.type = type;
        }
        else
        {
            const NameId name_id = name.length != 0 ? link_add_name(link, (const uint8_t*)name.pointer, (uint32_t)(name.length <= UINT32_MAX ? name.length : 0)) : 0;
            failed = name.length != 0 && name_id == 0;
            if (!failed && atom != 0)
            {
                added = link_add_symbol(link, name_id, atom, symbol.st_value, symbol.st_size, flags, source);
            }
            else if (!failed && absolute)
            {
                added = link_add_symbol(link, name_id, 0, symbol.st_value, symbol.st_size, flags, BUSTER_LINK_SOURCE_ABSOLUTE);
            }
            else if (!failed)
            {
                added = link_add_symbol(link, name_id, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE);
            }
            failed = failed || added == 0;
        }
        if (failed)
        {
            result = import_fail(IMPORT_ERROR_LINK, offset);
        }
        result.symbol = result.error != IMPORT_ERROR_NONE ? index : 0;
        object->symbols[index] = added;
    }
    return result;
}

// Sorts n relocation indices by their entries' offsets, stably, bottom-up (no recursion), between `order` and
// `spare`; returns which of the two holds the result.
static uint32_t* import_sort_relocations(const String8 entries, uint32_t* order, uint32_t* spare, uint64_t n)
{
    uint32_t* from = order;
    uint32_t* to = spare;
    for (uint64_t width = 1; width < n; width *= 2)
    {
        for (uint64_t start = 0; start < n; start += 2 * width)
        {
            const uint64_t middle = start + width < n ? start + width : n;
            const uint64_t end = start + 2 * width < n ? start + 2 * width : n;
            uint64_t left = start;
            uint64_t right = middle;
            for (uint64_t out = start; out < end; out += 1)
            {
                bool take_left = left < middle;
                if (take_left && right < end)
                {
                    Elf64_Rela a;
                    Elf64_Rela b;
                    memcpy(&a, entries.pointer + (uint64_t)from[left] * sizeof(Elf64_Rela), sizeof(a));
                    memcpy(&b, entries.pointer + (uint64_t)from[right] * sizeof(Elf64_Rela), sizeof(b));
                    take_left = a.r_offset <= b.r_offset;
                }
                to[out] = take_left ? from[left] : from[right];
                left += take_left ? 1 : 0;
                right += take_left ? 0 : 1;
            }
        }
        uint32_t* swap = from;
        from = to;
        to = swap;
    }
    return from;
}

// Imports a bounded sequence of Elf64_Rela records as one atom's fixups, in offset order. The symbol map has
// symbols->count entries, atom is zero or a valid id, and entries has no readable padding requirement. file_offset
// locates a failing original record for callers importing an ELF section; transformed records can supply zero.
// The separate entry point lets unwind filtering hand off its packed records without constructing another ELF.
static ImportResult import_relocation_entries(Link* link, const ImportSymbols* symbols, const ImportObject* object, AtomId atom, String8 entries, uint64_t file_offset, Arena* arena)
{
    ImportResult result = { 0 };
    const uint64_t count = entries.length / sizeof(Elf64_Rela);
    if (entries.length % sizeof(Elf64_Rela) != 0 || count > UINT32_MAX || (count != 0 && entries.pointer == 0))
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, file_offset);
    }
    else if (atom != 0 && count != 0)
    {
        uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
        uint32_t* spare = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
        if (order == 0 || spare == 0)
        {
            result = import_fail(IMPORT_ERROR_MEMORY, file_offset);
        }
        else
        {
            for (uint64_t i = 0; i < count; i += 1)
            {
                order[i] = (uint32_t)i;
            }
            const uint32_t* sorted = import_sort_relocations(entries, order, spare, count);
            for (uint64_t i = 0; result.error == IMPORT_ERROR_NONE && i < count; i += 1)
            {
                Elf64_Rela relocation;
                memcpy(&relocation, entries.pointer + (uint64_t)sorted[i] * sizeof(Elf64_Rela), sizeof(relocation));
                const uint64_t symbol = ELF64_R_SYM(relocation.r_info);
                const uint32_t type = (uint32_t)ELF64_R_TYPE(relocation.r_info);
                // The field's bytes are the atom's when link_add_fixup takes it: within its size, at most 2^32.
                const bool within = relocation.r_offset <= link->atoms.size[atom];
                const uint8_t kind = import_got_kind(import_fixup_kind(type), within ? link->atoms.bytes[atom] : 0, relocation.r_offset, relocation.r_addend);
                const SymbolId target = symbol < symbols->count ? object->symbols[symbol] : 0;
                const uint64_t entry_offset = file_offset + (uint64_t)sorted[i] * sizeof(Elf64_Rela);
                if (type == R_X86_64_NONE)
                {
                    // Nothing to patch.
                }
                else if (kind == BUSTER_LINK_FIXUP_NONE)
                {
                    result = import_fail(IMPORT_ERROR_UNSUPPORTED, entry_offset);
                    result.type = type;
                }
                else if (symbol == 0 || symbol >= symbols->count || target == 0 || relocation.r_offset > UINT32_MAX)
                {
                    result = import_fail(IMPORT_ERROR_BOUNDS, entry_offset);
                    result.symbol = (uint32_t)(symbol <= UINT32_MAX ? symbol : 0);
                }
                else if (link_add_fixup(link, atom, (uint32_t)relocation.r_offset, kind, target, relocation.r_addend) == 0)
                {
                    result = import_fail(IMPORT_ERROR_LINK, entry_offset);
                    result.type = type;
                }
                result.relocation = result.error != IMPORT_ERROR_NONE ? sorted[i] : 0;
            }
        }
    }
    return result;
}

// Imports one .rela section's entries as its atom's fixups, in offset order.
static ImportResult import_relocations(Link* link, const ImportElf* elf, const ImportSymbols* symbols, uint32_t index, const ImportObject* object, Arena* arena)
{
    const Elf64_Shdr header = import_section_header(elf, index);
    const uint64_t header_offset = elf->section_offset + (uint64_t)index * sizeof(Elf64_Shdr);
    uint64_t count = 0;
    const bool table = import_table(&header, sizeof(Elf64_Rela), &count) && count <= UINT32_MAX;
    const AtomId atom = header.sh_info != SHN_UNDEF && header.sh_info < elf->section_count ? object->section_atoms[header.sh_info] : 0;
    ImportResult result = { 0 };
    if (!table || symbols->section == 0 || header.sh_link != symbols->section || header.sh_info == SHN_UNDEF || header.sh_info >= elf->section_count)
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, header_offset);
    }
    else
    {
        result = import_relocation_entries(link, symbols, object, atom, import_section_bytes(elf, &header), header.sh_offset, arena);
        // The legacy section wrapper reports an allocation failure at the section header.
        result.offset = result.error == IMPORT_ERROR_MEMORY ? header_offset : result.offset;
    }
    result.section = result.error != IMPORT_ERROR_NONE && result.error != IMPORT_ERROR_MEMORY ? index : 0;
    return result;
}

// Reads the object's COMDAT groups into object->groups and group_sections, in section order (see the orientation
// comment): each SHT_GROUP section's flag word, which must be GRP_COMDAT alone; its sections, each within the table,
// with SHF_GROUP and in no other group; and its signature, the name of its symbol (sh_info, in the symbol table sh_link
// names), or for a section symbol, the section's. Every section with SHF_GROUP must be in a group. Both paths read the
// groups this way: an object has a few.
static ImportResult import_object_groups(const ImportElf* elf, const ImportSymbols* symbols, ImportObject* object, Arena* arena)
{
    ImportResult result = { 0 };
    const uint32_t count = elf->section_count;
    uint32_t group_count = 0;
    uint64_t member_total = 0;
    for (uint32_t index = 1; index < count; index += 1)
    {
        const Elf64_Shdr header = import_section_header(elf, index);
        group_count += header.sh_type == SHT_GROUP ? 1 : 0;
        member_total += header.sh_type == SHT_GROUP ? header.sh_size / sizeof(uint32_t) : 0;
    }
    object->groups = BUSTER_ARENA_PUSH_ARRAY(arena, ImportGroup, group_count);
    object->group_sections = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, member_total);
    object->group_count = 0;
    Scratch scratch = scratch_begin(arena);
    uint8_t* grouped = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, count);
    if ((group_count != 0 && object->groups == 0) || (member_total != 0 && object->group_sections == 0) || grouped == 0)
    {
        result = import_fail(IMPORT_ERROR_MEMORY, 0);
    }
    else
    {
        memset(grouped, 0, count);
    }
    uint32_t placed = 0;
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < count; index += 1)
    {
        const Elf64_Shdr header = import_section_header(elf, index);
        const uint64_t header_offset = elf->section_offset + (uint64_t)index * sizeof(Elf64_Shdr);
        if (header.sh_type == SHT_GROUP)
        {
            const String8 contents = import_section_bytes(elf, &header);
            const uint64_t words = contents.length / sizeof(uint32_t);
            const bool linked = symbols->section != 0 && header.sh_link == symbols->section && header.sh_info != 0 && header.sh_info < symbols->count;
            uint32_t flags = 0;
            if (contents.length >= sizeof(uint32_t))
            {
                memcpy(&flags, contents.pointer, sizeof(flags));
            }
            String8 signature = { 0 };
            if (linked)
            {
                const Elf64_Sym symbol = import_symbol(symbols, header.sh_info);
                uint32_t section = 0;
                const bool section_symbol = ELF64_ST_TYPE(symbol.st_info) == STT_SECTION;
                const bool indexed = import_symbol_section(symbols, header.sh_info, &symbol, &section);
                if (section_symbol && indexed && section != 0 && section < count)
                {
                    const Elf64_Shdr named = import_section_header(elf, section);
                    import_section_name(elf, &named, &signature);
                }
                else if (!section_symbol)
                {
                    import_string(symbols->names, symbol.st_name, &signature);
                }
            }
            if (!linked || contents.length % sizeof(uint32_t) != 0 || words == 0 || signature.length == 0)
            {
                result = import_fail(IMPORT_ERROR_BOUNDS, header_offset);
            }
            else if (flags != GRP_COMDAT)
            {
                result = import_fail(IMPORT_ERROR_UNSUPPORTED, header_offset);
                result.type = header.sh_type;
            }
            ImportGroup* group = &object->groups[object->group_count];
            if (result.error == IMPORT_ERROR_NONE)
            {
                *group = (ImportGroup){
                    .signature = signature,
                    .first = placed,
                    .count = 0,
                };
                object->group_count += 1;
            }
            for (uint64_t w = 1; result.error == IMPORT_ERROR_NONE && w < words; w += 1)
            {
                uint32_t member = 0;
                memcpy(&member, contents.pointer + w * sizeof(uint32_t), sizeof(member));
                const bool valid = member != 0 && member < count && member != index && grouped[member] == 0 &&
                                   (import_section_header(elf, member).sh_flags & SHF_GROUP) != 0;
                if (!valid)
                {
                    result = import_fail(IMPORT_ERROR_BOUNDS, header_offset);
                }
                else
                {
                    grouped[member] = 1;
                    object->group_sections[placed] = member;
                    placed += 1;
                    group->count += 1;
                }
            }
            result.section = result.error != IMPORT_ERROR_NONE ? index : 0;
        }
    }
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < count; index += 1)
    {
        if ((import_section_header(elf, index).sh_flags & SHF_GROUP) != 0 && grouped[index] == 0)
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, elf->section_offset + (uint64_t)index * sizeof(Elf64_Shdr));
            result.section = index;
        }
    }
    scratch_end(scratch);
    return result;
}

static ImportResult import_object_scalar(Link* link, String8 bytes, uint32_t unit, uint8_t source, Arena* arena, ImportObject* object)
{
    ImportElf elf;
    ImportSymbols symbols = { 0 };
    *object = (ImportObject){ 0 };
    ImportResult result = import_elf(&elf, bytes, ET_REL);
    if (result.error == IMPORT_ERROR_NONE)
    {
        result = import_symbols(&elf, &symbols);
    }
    if (result.error == IMPORT_ERROR_NONE)
    {
        object->section_atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, elf.section_count);
        object->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, symbols.count);
        if (object->section_atoms == 0 || object->symbols == 0)
        {
            result = import_fail(IMPORT_ERROR_MEMORY, 0);
        }
        else
        {
            memset(object->section_atoms, 0, (uint64_t)elf.section_count * sizeof(AtomId));
            memset(object->symbols, 0, (uint64_t)symbols.count * sizeof(SymbolId));
            object->section_count = elf.section_count;
            object->symbol_count = symbols.count;
        }
    }
    if (result.error == IMPORT_ERROR_NONE)
    {
        result = import_sections(link, &elf, unit, object);
    }
    if (result.error == IMPORT_ERROR_NONE)
    {
        result = import_object_symbols(link, &elf, &symbols, source, object);
    }
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < elf.section_count; index += 1)
    {
        if (import_section_header(&elf, index).sh_type == SHT_RELA)
        {
            Scratch scratch = scratch_begin(arena);
            result = import_relocations(link, &elf, &symbols, index, object, scratch.arena);
            scratch_end(scratch);
        }
    }
    if (result.error == IMPORT_ERROR_NONE)
    {
        result = import_object_groups(&elf, &symbols, object, arena);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Objects: the vector path
//
// import_object_vector reads the sections, the symbols and the relocations eight records at a time, one per 64-bit
// lane, and writes the link's columns directly, past their counts: the records import_object_scalar adds one at a time,
// in the same order. Each check the exact path makes is a lane mask here, and the path gives up on the whole object
// when one fails or when it meets what it leaves to the exact path (two relocation sections for one section, fields
// that overlap, a table without room for every record): import_object then takes back what it wrote and runs the exact
// path, which reports the error, or imports the rest. Each pass:
//   - Sections: 8 headers, a row each, transposed (link_transpose_words) so that a vector holds one field of 8
//     sections; their bounds, alignments and classes as masks, the 16 first bytes of each name compared with the names
//     the classes depend on (import_section_patterns); the atoms compressed into the columns (vpcompressq in registers,
//     then masked stores), and each section's atom id expanded into its lane.
//   - Symbols: 8 entries of 24 bytes in 3 vectors, transposed into their 3 words (import_load_records3); each one's
//     atom by its section, with the atom's class and size, a scalar load per symbol and column rather than a gather
//     (vpgatherdd zmm is 64 µops on Zen 5); then the classification and the columns, as for sections. The names: each
//     one's length from a masked 64-byte load, their hashes 8 at a time (link_hash_names), and their bytes copied into
//     one push from the name arena.
//   - Relocations: 8 entries in 3 vectors the same way; each type's kind and field width from a 64-byte table in a
//     register (vpermb); the order and overlap checks against the previous lane's field end (valignq); wide addends
//     numbered in the side table by expanding their ranks; then each target, a scalar load per fixup. A section whose
//     entries are out of offset order is read again from a copy sorted by an LSD radix sort of keys built 8 at a time.

// The vector paths' batch: eight records, one per 64-bit lane.
#define BUSTER_IMPORT_BATCH 8
// The relocation types import_kind_table covers: every type a kind names is below it.
#define BUSTER_IMPORT_KIND_TYPES 64
// import_kind_table's entries: the kind in the low bits, and whether its field is 8 bytes rather than 4.
#define BUSTER_IMPORT_KIND_BITS 0x1f
#define BUSTER_IMPORT_KIND_WIDE_SHIFT 5
#define BUSTER_IMPORT_KIND_WIDE_FIELD (1u << BUSTER_IMPORT_KIND_WIDE_SHIFT)
_Static_assert(BUSTER_LINK_FIXUP_KINDS <= BUSTER_IMPORT_KIND_BITS + 1, "a kind fits in import_kind_table's low bits");
// The low 32 bits of a 64-bit lane.
#define BUSTER_IMPORT_LOW32 0xffffffffull

// What the link's tables and the arenas held before an object: what a fallback to the exact path returns them to.
typedef struct ImportMark ImportMark;
struct ImportMark
{
    uint32_t names;
    uint32_t atoms;
    uint32_t symbols;
    uint32_t fixups;
    uint32_t wide_addends;
    uint32_t priorities;
    uint64_t name_bytes;
    uint64_t arena;
};

// What the section pass found besides the atoms: the symbol tables, the extended index tables and the relocation
// sections, each a list of section indices in section order, and whether a section is a group's or has SHF_GROUP.
typedef struct ImportVectorSections ImportVectorSections;
struct ImportVectorSections
{
    uint32_t* symbol_tables;
    uint32_t symbol_table_count;
    uint32_t* extended;
    uint32_t extended_count;
    uint32_t* relocations;
    uint32_t relocation_count;
    bool grouped;
};

static ImportMark import_mark(const Link* link, const Arena* arena)
{
    ImportMark result = {
        .names = link->names.count,
        .atoms = link->atoms.count,
        .symbols = link->symbols.count,
        .fixups = link->fixups.count,
        .wide_addends = link->fixups.wide_count,
        .priorities = link->priorities.count,
        .name_bytes = link->name_arena != 0 ? link->name_arena->length : 0,
        .arena = arena->length,
    };
    return result;
}

static void import_restore(Link* link, Arena* arena, ImportMark mark)
{
    link->names.count = mark.names;
    link->atoms.count = mark.atoms;
    link->symbols.count = mark.symbols;
    link->fixups.count = mark.fixups;
    link->fixups.wide_count = mark.wide_addends;
    link->priorities.count = mark.priorities;
    if (link->name_arena != 0)
    {
        arena_pop_to(link->name_arena, mark.name_bytes);
    }
    arena_pop_to(arena, mark.arena);
}

static uint32_t import_lane_mask(uint32_t count)
{
    return _bzhi_u32(0xff, count);
}

static uint32_t import_lanes_equal(__m512i lanes, uint64_t value)
{
    return _mm512_cmpeq_epi64_mask(lanes, _mm512_set1_epi64((long long)value));
}

// The lanes with any of `bits` set.
static uint32_t import_lanes_have(__m512i lanes, uint64_t bits)
{
    return _mm512_test_epi64_mask(lanes, _mm512_set1_epi64((long long)bits));
}

// Stores the first `count` lanes, or their low 32, 16 or 8 bits.
static void import_store64(void* destination, __m512i lanes, uint32_t count)
{
    _mm512_mask_storeu_epi64(destination, (__mmask8)import_lane_mask(count), lanes);
}

// The narrower stores are vpmovqd, vpmovqw and vpmovqb to memory with a mask: 2 µops each on Zen 5 (uops.info), as
// many as a permute and a masked store.
static void import_store32(void* destination, __m512i lanes, uint32_t count)
{
    _mm256_mask_storeu_epi32(destination, (__mmask8)import_lane_mask(count), _mm512_cvtepi64_epi32(lanes));
}

static void import_store16(void* destination, __m512i lanes, uint32_t count)
{
    _mm_mask_storeu_epi16(destination, (__mmask8)import_lane_mask(count), _mm512_cvtepi64_epi16(lanes));
}

static void import_store8(void* destination, __m512i lanes, uint32_t count)
{
    _mm_mask_storeu_epi8(destination, (__mmask16)import_lane_mask(count), _mm512_cvtepi64_epi8(lanes));
}

// Loads `count` records of three 64-bit words (Elf64_Rela, Elf64_Sym), up to 8, and transposes them: words[j] lane i is
// word j of record i, and 0 past `count`. The masked loads read nothing past the count'th record.
static void import_load_records3(const char* records, uint32_t count, __m512i* words)
{
    __m512i r0;
    __m512i r1;
    __m512i r2;
    // Every batch but a table's last is whole.
    if (count == BUSTER_IMPORT_BATCH)
    {
        r0 = _mm512_loadu_si512(records);
        r1 = _mm512_loadu_si512(records + 64);
        r2 = _mm512_loadu_si512(records + 128);
    }
    else
    {
        const uint32_t loaded = 3 * count;
        r0 = _mm512_maskz_loadu_epi64((__mmask8)import_lane_mask(loaded), records);
        r1 = _mm512_maskz_loadu_epi64((__mmask8)import_lane_mask(loaded > 8 ? loaded - 8 : 0), records + 64);
        r2 = _mm512_maskz_loadu_epi64((__mmask8)import_lane_mask(loaded > 16 ? loaded - 16 : 0), records + 128);
    }
    // Word j of record i is word 3i + j of the 24: words 0 to 7 are r0's, 8 to 15 r1's and 16 to 23 r2's. A permute takes
    // each field's words from r0 and r1, and another adds r2's (_mm512_set_epi64 lists the last lane first).
    const __m512i t0 = _mm512_permutex2var_epi64(r0, _mm512_set_epi64(0, 0, 15, 12, 9, 6, 3, 0), r1);
    const __m512i t1 = _mm512_permutex2var_epi64(r0, _mm512_set_epi64(0, 0, 0, 13, 10, 7, 4, 1), r1);
    const __m512i t2 = _mm512_permutex2var_epi64(r0, _mm512_set_epi64(0, 0, 0, 14, 11, 8, 5, 2), r1);
    words[0] = _mm512_permutex2var_epi64(t0, _mm512_set_epi64(13, 10, 5, 4, 3, 2, 1, 0), r2);
    words[1] = _mm512_permutex2var_epi64(t1, _mm512_set_epi64(14, 11, 8, 4, 3, 2, 1, 0), r2);
    words[2] = _mm512_permutex2var_epi64(t2, _mm512_set_epi64(15, 12, 9, 4, 3, 2, 1, 0), r2);
}

// The length of the name at `offset` in a string table that ends with a NUL, the offset within it: the first NUL of a
// masked 64-byte load, none past the table, or memchr's past them for a longer name.
static uint64_t import_name_length(String8 table, uint64_t offset)
{
    const uint64_t remaining = table.length - offset;
    const __mmask64 loaded = _bzhi_u64(~0ull, (uint32_t)(remaining < 64 ? remaining : 64));
    const __m512i row = _mm512_maskz_loadu_epi8(loaded, table.pointer + offset);
    const uint64_t nuls = _mm512_mask_testn_epi8_mask(loaded, row, row);
    uint64_t result = _tzcnt_u64(nuls);
    if (nuls == 0)
    {
        const char* end = memchr(table.pointer + offset + 64, 0, remaining - 64);
        result = (uint64_t)(end - (table.pointer + offset));
    }
    return result;
}

// Loads eight names for link_hash_rows: rows[lane] the first 64 bytes of the name at names[lane] of lengths[lane]
// bytes, zero past its length, for the first `count` lanes, and empty rows after them. Returns the lanes whose names are
// longer than 63 bytes.
static uint32_t import_name_rows(const char* table, const uint32_t* offsets, const uint32_t* lengths, uint32_t count, __m512i* rows)
{
    uint32_t result = 0;
    for (uint32_t lane = 0; lane < BUSTER_IMPORT_BATCH; lane += 1)
    {
        const uint32_t length = lane < count ? lengths[lane] : 0;
        const uint64_t place = lane < count ? offsets[lane] : 0;
        rows[lane] = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, length < 64 ? length : 64), table + place);
        result |= (length >= 64 ? 1u : 0u) << lane;
    }
    return result;
}

// Which of 8 names, the 16 first bytes of names 0 to 3 in `low` and of 4 to 7 in `high`, match the pattern in each
// 128-bit chunk of `pattern` in every byte that `significant` has set.
static uint32_t import_match_names(__m512i low, __m512i high, __m512i pattern, __m512i significant)
{
    // (name ^ pattern) & significant, which is 0 in both 64-bit words of a name that matches.
    const __m512i low_difference = _mm512_ternarylogic_epi64(low, pattern, significant, 0x28);
    const __m512i high_difference = _mm512_ternarylogic_epi64(high, pattern, significant, 0x28);
    const uint32_t low_words = _mm512_test_epi64_mask(low_difference, low_difference);
    const uint32_t high_words = _mm512_test_epi64_mask(high_difference, high_difference);
    const uint32_t differs = _pext_u32(low_words | low_words >> 1, 0x55) | _pext_u32(high_words | high_words >> 1, 0x55) << 4;
    return ~differs & 0xff;
}

// The kinds of relocation types for the vector path: entry `type` is the kind that names it (object_relocation_types),
// with BUSTER_IMPORT_KIND_WIDE_FIELD for an 8-byte field, or 0. Every type a kind names is below 64, which
// test-import checks.
static __m512i import_kind_table(void)
{
    uint8_t table[BUSTER_IMPORT_KIND_TYPES] = { 0 };
    for (uint8_t kind = 1; kind < BUSTER_LINK_FIXUP_KINDS; kind += 1)
    {
        const uint32_t type = object_relocation_types[kind];
        table[type % BUSTER_IMPORT_KIND_TYPES] = (uint8_t)(kind | (link_fixup_kinds[kind].width == 8 ? BUSTER_IMPORT_KIND_WIDE_FIELD : 0));
    }
    return _mm512_loadu_si512(table);
}

// The section pass: the checks import_elf makes of each section and import_sections of each one's name, alignment and
// class, and the atoms, eight sections at a time; and the lists of the sections the other passes read.
static bool import_vector_sections(Link* link, const ImportElf* elf, uint32_t unit, ImportObject* object, ImportVectorSections* found)
{
    LinkAtoms* atoms = &link->atoms;
    const char* table = elf->bytes.pointer + elf->section_offset;
    const String8 names = elf->section_names;
    const uint32_t count = elf->section_count;
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi64(1);
    const __m512i lane_index = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i file_bytes = _mm512_set1_epi64((long long)elf->bytes.length);
    const __m512i file_base = _mm512_set1_epi64((long long)(uintptr_t)elf->bytes.pointer);
    __m512i patterns[IMPORT_PATTERNS];
    __m512i significant[IMPORT_PATTERNS];
    for (uint32_t p = 0; p < IMPORT_PATTERNS; p += 1)
    {
        patterns[p] = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i*)import_section_patterns[p]));
        significant[p] = _mm512_broadcast_i32x4(_mm_maskz_set1_epi8((__mmask16)_bzhi_u32(0xffff, import_section_pattern_bytes[p]), (char)0xff));
    }
    // An offset within the names' table starts a name when the table ends with a NUL.
    bool result = names.length != 0 && names.pointer[names.length - 1] == 0;
    for (uint32_t first = 0; result && first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH;
        // Section 0 is the null section.
        const uint32_t active = import_lane_mask(n) & (first == 0 ? 0xfe : 0xff);
        __m512i rows[BUSTER_IMPORT_BATCH];
        __m512i words[BUSTER_IMPORT_BATCH];
        __m128i name_rows[BUSTER_IMPORT_BATCH];
        for (uint32_t r = 0; r < BUSTER_IMPORT_BATCH; r += 1)
        {
            rows[r] = _mm512_maskz_loadu_epi64(r < n ? 0xff : 0, table + (uint64_t)(first + r) * sizeof(Elf64_Shdr));
            // The name's first 16 bytes, none past the table: sh_name is the row's low 32 bits.
            const uint32_t name = (uint32_t)_mm_cvtsi128_si32(_mm512_castsi512_si128(rows[r]));
            const bool within = (active >> r & 1) != 0 && name < names.length;
            const uint64_t place = within ? name : 0;
            const uint64_t remaining = names.length - place;
            const __mmask16 loaded = within ? (__mmask16)_bzhi_u32(0xffff, (uint32_t)(remaining < 16 ? remaining : 16)) : 0;
            name_rows[r] = _mm_maskz_loadu_epi8(loaded, names.pointer + place);
        }
        link_transpose_words(rows, words);
        __m512i low = _mm512_castsi128_si512(name_rows[0]);
        __m512i high = _mm512_castsi128_si512(name_rows[4]);
        low = _mm512_inserti32x4(low, name_rows[1], 1);
        low = _mm512_inserti32x4(low, name_rows[2], 2);
        low = _mm512_inserti32x4(low, name_rows[3], 3);
        high = _mm512_inserti32x4(high, name_rows[5], 1);
        high = _mm512_inserti32x4(high, name_rows[6], 2);
        high = _mm512_inserti32x4(high, name_rows[7], 3);
        uint32_t matches[IMPORT_PATTERNS];
        for (uint32_t p = 0; p < IMPORT_PATTERNS; p += 1)
        {
            matches[p] = import_match_names(low, high, patterns[p], significant[p]);
        }
        // Elf64_Shdr's words: sh_name and sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link and sh_info,
        // sh_addralign, sh_entsize.
        const __m512i type = _mm512_srli_epi64(words[0], 32);
        const __m512i flags = words[1];
        const __m512i offset = words[3];
        const __m512i size = words[4];
        const __m512i alignment = _mm512_max_epu64(words[6], one);
        const __m512i entry_size = words[7];
        const uint32_t nobits = import_lanes_equal(type, SHT_NOBITS);
        const uint32_t in_file = _mm512_cmple_epu64_mask(offset, file_bytes) & _mm512_cmple_epu64_mask(size, _mm512_sub_epi64(file_bytes, offset));
        const uint32_t named = _mm512_cmplt_epu64_mask(_mm512_and_si512(words[0], _mm512_set1_epi64(BUSTER_IMPORT_LOW32)), _mm512_set1_epi64((long long)names.length));
        const uint32_t power_of_two = _mm512_testn_epi64_mask(alignment, _mm512_sub_epi64(alignment, one));
        const __m512i alignment_log2 = _mm512_sub_epi64(_mm512_set1_epi64(63), _mm512_lzcnt_epi64(alignment));
        const uint32_t aligned = power_of_two & _mm512_cmple_epu64_mask(alignment_log2, _mm512_set1_epi64(BUSTER_LINK_MAX_ALIGNMENT_LOG2));
        const uint32_t unwind = import_lanes_equal(type, SHT_X86_64_UNWIND);
        const uint32_t init_array = import_lanes_equal(type, SHT_INIT_ARRAY);
        const uint32_t fini_array = import_lanes_equal(type, SHT_FINI_ARRAY);
        const uint32_t contents = import_lanes_equal(type, SHT_PROGBITS) | nobits | init_array | fini_array | unwind;
        const uint32_t group = import_lanes_equal(type, SHT_GROUP);
        const uint32_t unsupported = import_lanes_equal(type, SHT_REL) | import_lanes_equal(type, SHT_PREINIT_ARRAY) | matches[IMPORT_PATTERN_CTORS] | matches[IMPORT_PATTERN_DTORS];
        const uint32_t dropped = ~import_lanes_have(flags, SHF_ALLOC) | group | import_lanes_equal(type, SHT_NOTE) | import_lanes_equal(type, BUSTER_IMPORT_SHT_GNU_SFRAME) |
                                 matches[IMPORT_PATTERN_SFRAME];
        // An init or fini array named other than .init_array and .fini_array has a priority, read below.
        const uint32_t prioritized = (init_array & ~matches[IMPORT_PATTERN_INIT_ARRAY]) | (fini_array & ~matches[IMPORT_PATTERN_FINI_ARRAY]);
        found->grouped = found->grouped || (active & (group | import_lanes_have(flags, SHF_GROUP))) != 0;
        const uint32_t merged_strings = _mm512_cmpeq_epi64_mask(_mm512_and_si512(flags, _mm512_set1_epi64(SHF_MERGE | SHF_STRINGS)), _mm512_set1_epi64(SHF_MERGE | SHF_STRINGS)) &
                                        import_lanes_equal(entry_size, 1);
        // import_section_class's order: each mask takes its lanes from what the ones before it left.
        uint32_t left = active & ~unsupported & ~dropped;
        const uint32_t no_contents = left & ~contents;
        left &= contents;
        const uint32_t eh_frame = left & (unwind | matches[IMPORT_PATTERN_EH_FRAME]);
        left &= ~eh_frame;
        const uint32_t init_arrays = left & init_array;
        left &= ~init_arrays;
        const uint32_t fini_arrays = left & fini_array;
        left &= ~fini_arrays;
        const uint32_t tls = left & import_lanes_have(flags, SHF_TLS);
        left &= ~tls;
        const uint32_t code = left & import_lanes_have(flags, SHF_EXECINSTR);
        left &= ~code;
        const uint32_t data = left & import_lanes_have(flags, SHF_WRITE);
        left &= ~data;
        const uint32_t read_only_nobits = left & nobits;
        left &= ~read_only_nobits;
        // What is left is read-only data.
        __m512i class = zero;
        class = _mm512_mask_mov_epi64(class, (__mmask8)eh_frame, _mm512_set1_epi64(BUSTER_LINK_CLASS_EH_FRAME));
        class = _mm512_mask_mov_epi64(class, (__mmask8)init_arrays, _mm512_set1_epi64(BUSTER_LINK_CLASS_INIT_ARRAY));
        class = _mm512_mask_mov_epi64(class, (__mmask8)fini_arrays, _mm512_set1_epi64(BUSTER_LINK_CLASS_FINI_ARRAY));
        class = _mm512_mask_mov_epi64(class, (__mmask8)(tls & ~nobits), _mm512_set1_epi64(BUSTER_LINK_CLASS_TDATA));
        class = _mm512_mask_mov_epi64(class, (__mmask8)(tls & nobits), _mm512_set1_epi64(BUSTER_LINK_CLASS_TBSS));
        class = _mm512_mask_mov_epi64(class, (__mmask8)code, _mm512_set1_epi64(BUSTER_LINK_CLASS_TEXT));
        class = _mm512_mask_mov_epi64(class, (__mmask8)(code & matches[IMPORT_PATTERN_INIT]), _mm512_set1_epi64(BUSTER_LINK_CLASS_INIT));
        class = _mm512_mask_mov_epi64(class, (__mmask8)(code & matches[IMPORT_PATTERN_FINI]), _mm512_set1_epi64(BUSTER_LINK_CLASS_FINI));
        class = _mm512_mask_mov_epi64(class, (__mmask8)data, _mm512_set1_epi64(BUSTER_LINK_CLASS_DATA));
        class = _mm512_mask_mov_epi64(class, (__mmask8)(data & matches[IMPORT_PATTERN_DATA_RELRO]), _mm512_set1_epi64(BUSTER_LINK_CLASS_DATA_RELRO));
        class = _mm512_mask_mov_epi64(class, (__mmask8)(data & nobits), _mm512_set1_epi64(BUSTER_LINK_CLASS_BSS));
        class = _mm512_mask_mov_epi64(class, (__mmask8)(left & ~merged_strings), _mm512_set1_epi64(BUSTER_LINK_CLASS_RODATA));
        class = _mm512_mask_mov_epi64(class, (__mmask8)(left & merged_strings), _mm512_set1_epi64(BUSTER_LINK_CLASS_STRINGS));
        const uint32_t classed = eh_frame | init_arrays | fini_arrays | tls | code | data | left;
        const uint32_t zero_fill = (tls | data) & nobits;
        // A zero-fill class exactly for a NOBITS section.
        const uint32_t mismatched = classed & ~zero_fill & nobits;
        const uint32_t failed = active & ((~in_file & ~nobits) | ~named | ~aligned | unsupported | no_contents | read_only_nobits | mismatched);
        const uint32_t atom_count = (uint32_t)__builtin_popcount(classed);
        result = failed == 0 && atom_count <= atoms->capacity - atoms->count;
        if (result)
        {
            const uint32_t executable_stack = active & matches[IMPORT_PATTERN_GNU_STACK] & import_lanes_have(flags, SHF_EXECINSTR);
            if (object->executable_stack_section == 0 && executable_stack != 0)
            {
                object->executable_stack_section = first + (uint32_t)__builtin_ctz(executable_stack);
            }
            const uint32_t at = atoms->count;
            const __m512i bytes = _mm512_maskz_add_epi64((__mmask8)(classed & ~zero_fill & _mm512_test_epi64_mask(size, size)), file_base, offset);
            import_store8(atoms->class + at, _mm512_maskz_compress_epi64((__mmask8)classed, class), atom_count);
            import_store8(atoms->alignment_log2 + at, _mm512_maskz_compress_epi64((__mmask8)classed, alignment_log2), atom_count);
            import_store32(atoms->unit + at, _mm512_set1_epi64(unit), atom_count);
            import_store64(atoms->size + at, _mm512_maskz_compress_epi64((__mmask8)classed, size), atom_count);
            import_store64(atoms->bytes + at, _mm512_maskz_compress_epi64((__mmask8)classed, bytes), atom_count);
            import_store32(atoms->fixup_begin + at, zero, atom_count);
            import_store32(atoms->fixup_count + at, zero, atom_count);
            import_store64(atoms->address + at, zero, atom_count);
            import_store32(object->section_atoms + first, _mm512_maskz_expand_epi64((__mmask8)classed, _mm512_add_epi64(lane_index, _mm512_set1_epi64(at))), n);
            atoms->count += atom_count;
            // The arrays with a priority, a section at a time: there are a few in a library's thousands of objects.
            for (uint32_t rare = classed & prioritized; result && rare != 0; rare &= rare - 1)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(rare);
                const Elf64_Shdr header = import_section_header(elf, first + lane);
                const char* base = (init_array >> lane & 1) != 0 ? import_section_patterns[IMPORT_PATTERN_INIT_ARRAY] : import_section_patterns[IMPORT_PATTERN_FINI_ARRAY];
                String8 name = { 0 };
                uint32_t priority = BUSTER_IMPORT_NO_PRIORITY;
                const AtomId atom = at + (AtomId)__builtin_popcount(classed & ((1u << lane) - 1));
                // With room for it, link_add_priority can't fail, and leaves link->error as the exact path would find it.
                result = import_section_name(elf, &header, &name) && import_array_priority(name, base, &priority) && link->priorities.count < link->priorities.capacity &&
                         link_add_priority(link, atom, priority);
            }
            const __m512i index = _mm512_add_epi64(lane_index, _mm512_set1_epi64(first));
            const uint32_t symbol_tables = active & import_lanes_equal(type, SHT_SYMTAB);
            const uint32_t extended = active & import_lanes_equal(type, SHT_SYMTAB_SHNDX);
            const uint32_t relocations = active & import_lanes_equal(type, SHT_RELA);
            const uint32_t symbol_table_count = (uint32_t)__builtin_popcount(symbol_tables);
            const uint32_t extended_count = (uint32_t)__builtin_popcount(extended);
            const uint32_t relocation_count = (uint32_t)__builtin_popcount(relocations);
            import_store32(found->symbol_tables + found->symbol_table_count, _mm512_maskz_compress_epi64((__mmask8)symbol_tables, index), symbol_table_count);
            import_store32(found->extended + found->extended_count, _mm512_maskz_compress_epi64((__mmask8)extended, index), extended_count);
            import_store32(found->relocations + found->relocation_count, _mm512_maskz_compress_epi64((__mmask8)relocations, index), relocation_count);
            found->symbol_table_count += symbol_table_count;
            found->extended_count += extended_count;
            found->relocation_count += relocation_count;
        }
    }
    return result;
}

// import_symbols, from the lists the section pass made: the same table where import_symbols succeeds, false where it
// fails.
static bool import_vector_symbol_table(const ImportElf* elf, const ImportVectorSections* found, ImportSymbols* symbols)
{
    *symbols = (ImportSymbols){ 0 };
    bool result = found->symbol_table_count <= 1;
    if (result && found->symbol_table_count == 1)
    {
        const uint32_t index = found->symbol_tables[0];
        const Elf64_Shdr header = import_section_header(elf, index);
        uint64_t count = 0;
        const bool table = import_table(&header, sizeof(Elf64_Sym), &count) && count >= 1 && count <= UINT32_MAX;
        const bool linked = header.sh_link != SHN_UNDEF && header.sh_link < elf->section_count && import_section_header(elf, header.sh_link).sh_type == SHT_STRTAB;
        result = table && linked;
        if (result)
        {
            const Elf64_Shdr names = import_section_header(elf, header.sh_link);
            symbols->section = index;
            symbols->entries = import_section_bytes(elf, &header);
            symbols->count = (uint32_t)count;
            symbols->names = import_section_bytes(elf, &names);
        }
        // Each extended index table of the symbol table must fit it, and the last one is the one import_symbols keeps.
        for (uint32_t i = 0; result && i < found->extended_count; i += 1)
        {
            const Elf64_Shdr extended = import_section_header(elf, found->extended[i]);
            uint64_t entries = 0;
            if (extended.sh_link == index)
            {
                result = import_table(&extended, sizeof(uint32_t), &entries) && entries == symbols->count;
                symbols->extended = import_section_bytes(elf, &extended);
            }
        }
    }
    return result;
}

// Eight symbols' section indices, from st_shndx (word 0's top 16 bits) or for SHN_XINDEX the extended index table:
// *indirect gets those lanes, among the first `count`.
static __m512i import_vector_symbol_sections(__m512i word0, const ImportSymbols* symbols, uint32_t first, uint32_t count, uint32_t* indirect)
{
    const __m512i shndx = _mm512_srli_epi64(word0, 48);
    const uint32_t extended = import_lanes_equal(shndx, SHN_XINDEX) & import_lane_mask(count);
    __m512i result = shndx;
    if (extended != 0 && symbols->extended.length != 0)
    {
        const __m256i indices = _mm256_maskz_loadu_epi32((__mmask8)import_lane_mask(count), symbols->extended.pointer + (uint64_t)first * sizeof(uint32_t));
        result = _mm512_mask_mov_epi64(shndx, (__mmask8)extended, _mm512_cvtepu32_epi64(indices));
    }
    *indirect = extended;
    return result;
}

// The symbol pass: the checks and records of import_object_symbols, eight symbols at a time, then their names.
static bool import_vector_symbols(Link* link, const ImportElf* elf, const ImportSymbols* symbols, uint8_t source, ImportObject* object, Arena* scratch)
{
    LinkSymbols* table = &link->symbols;
    LinkNames* link_names = &link->names;
    const uint32_t count = symbols->count;
    const String8 names = symbols->names;
    const __m512i zero = _mm512_setzero_si512();
    const __m512i lane_index = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i low32 = _mm512_set1_epi64(BUSTER_IMPORT_LOW32);
    // Per symbol, its atom (first its section index), the atom's class and size, and its name's offset if it gets one.
    AtomId* atoms = BUSTER_ARENA_PUSH_ARRAY(scratch, AtomId, count);
    uint8_t* classes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count);
    uint64_t* sizes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count);
    uint32_t* name_offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    // An offset within the names' table starts a name when the table ends with a NUL. Every symbol could be added.
    bool result = (names.length == 0 || names.pointer[names.length - 1] == 0) && count <= table->capacity - table->count;
    result = result && atoms != 0 && classes != 0 && sizes != 0 && name_offsets != 0;
    uint32_t failed = 0;
    // The sections: each one checked, and the index of an ordinary one kept for its atom (0 for the others).
    for (uint32_t first = 0; result && first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH;
        const uint32_t active = import_lane_mask(n) & (first == 0 ? 0xfe : 0xff);
        __m512i words[3];
        uint32_t indirect = 0;
        import_load_records3(symbols->entries.pointer + (uint64_t)first * sizeof(Elf64_Sym), n, words);
        const __m512i section = import_vector_symbol_sections(words[0], symbols, first, n, &indirect);
        const __m512i name = _mm512_and_si512(words[0], low32);
        const uint32_t reserved = _mm512_cmpge_epu64_mask(section, _mm512_set1_epi64(SHN_LORESERVE)) & ~indirect;
        const uint32_t ordinary = ~reserved & _mm512_test_epi64_mask(section, section);
        const uint32_t outside = ordinary & _mm512_cmpge_epu64_mask(section, _mm512_set1_epi64(elf->section_count));
        const uint32_t unnamed = _mm512_test_epi64_mask(name, name) & _mm512_cmpge_epu64_mask(name, _mm512_set1_epi64((long long)names.length));
        failed |= active & ((indirect & (symbols->extended.length == 0 ? 0xff : 0)) | outside | unnamed);
        import_store32(atoms + first, _mm512_maskz_mov_epi64((__mmask8)(active & ordinary & ~outside), section), n);
    }
    // Each one's atom, and the atom's class and size: atom 0, record 0, has class NONE and size 0.
    const AtomId* section_atoms = object->section_atoms;
    const uint8_t* atom_classes = link->atoms.class;
    const uint64_t* atom_sizes = link->atoms.size;
    const uint32_t joined = result && failed == 0 ? count : 0;
    for (uint32_t s = 0; s < joined; s += 1)
    {
        const AtomId atom = section_atoms[atoms[s]];
        atoms[s] = atom;
        classes[s] = atom_classes[atom];
        sizes[s] = atom_sizes[atom];
    }
    // The records, in symbol order.
    const SymbolId base = table->count;
    uint32_t added = 0;
    for (uint32_t first = 0; result && failed == 0 && first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH;
        const uint32_t active = import_lane_mask(n) & (first == 0 ? 0xfe : 0xff);
        const __mmask8 loaded = (__mmask8)import_lane_mask(n);
        __m512i words[3];
        uint32_t indirect = 0;
        import_load_records3(symbols->entries.pointer + (uint64_t)first * sizeof(Elf64_Sym), n, words);
        // Elf64_Sym's words: st_name, st_info, st_other and st_shndx; st_value; st_size.
        const __m512i section = import_vector_symbol_sections(words[0], symbols, first, n, &indirect);
        const __m512i name = _mm512_and_si512(words[0], low32);
        const __m512i info = _mm512_and_si512(_mm512_srli_epi64(words[0], 32), _mm512_set1_epi64(0xff));
        const __m512i type = _mm512_and_si512(info, _mm512_set1_epi64(0x0f));
        const __m512i binding = _mm512_srli_epi64(info, 4);
        const __m512i visibility = _mm512_and_si512(_mm512_srli_epi64(words[0], 40), _mm512_set1_epi64(0x03));
        const __m512i value = words[1];
        const __m512i size = words[2];
        const __m512i atom = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(loaded, atoms + first));
        const __m512i class = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(loaded, classes + first));
        const __m512i atom_size = _mm512_maskz_loadu_epi64(loaded, sizes + first);
        const uint32_t reserved = _mm512_cmpge_epu64_mask(section, _mm512_set1_epi64(SHN_LORESERVE)) & ~indirect;
        const uint32_t undefined = _mm512_testn_epi64_mask(section, section);
        const uint32_t ordinary = ~reserved & ~undefined;
        const uint32_t has_atom = _mm512_test_epi64_mask(atom, atom);
        const uint32_t local = import_lanes_equal(binding, STB_LOCAL);
        const uint32_t section_type = import_lanes_equal(type, STT_SECTION);
        // import_object_symbols' order: FILE symbols and those of dropped sections are skipped, a section's symbol is a
        // local at its atom's start, and the others are records or unsupported.
        const uint32_t skipped = import_lanes_equal(type, STT_FILE) | (section_type & ~has_atom) | (local & ordinary & ~has_atom);
        const uint32_t section_symbols = active & section_type & ~skipped;
        const uint32_t regular = active & ~skipped & ~section_type;
        const uint32_t tls_type = import_lanes_equal(type, STT_TLS);
        const uint32_t ifunc_type = import_lanes_equal(type, STT_GNU_IFUNC);
        const uint32_t known = (import_lanes_equal(type, STT_NOTYPE) | import_lanes_equal(type, STT_OBJECT) | import_lanes_equal(type, STT_FUNC) | tls_type | ifunc_type) &
                               _mm512_cmple_epu64_mask(binding, _mm512_set1_epi64(STB_WEAK));
        // An absolute symbol (SHN_ABS) is its value, and can't be a TLS variable or an IFUNC (link_absolute_fits).
        const uint32_t absolute = reserved & import_lanes_equal(section, SHN_ABS);
        const uint32_t unsupported = ~known | (reserved & ~absolute) | (absolute & (tls_type | ifunc_type)) | (ordinary & ~has_atom) | (undefined & local);
        const uint32_t tls_atom = import_lanes_equal(class, BUSTER_LINK_CLASS_TDATA) | import_lanes_equal(class, BUSTER_LINK_CLASS_TBSS);
        const uint32_t defined = regular & has_atom;
        const uint32_t valued = defined | (regular & absolute);
        // link_symbol_fits: within the atom, and a TLS symbol exactly in a TLS atom.
        const uint32_t fits = _mm512_cmple_epu64_mask(value, atom_size) & _mm512_cmple_epu64_mask(size, _mm512_sub_epi64(atom_size, value)) & ~(tls_type ^ tls_atom);
        failed |= (regular & unsupported) | (defined & ~fits);
        const uint32_t records = section_symbols | regular;
        const uint32_t record_count = (uint32_t)__builtin_popcount(records);
        const __mmask8 kept = (__mmask8)records;
        const SymbolId at = table->count;
        const __m512i flags = _mm512_mask_mov_epi64(_mm512_or_si512(_mm512_or_si512(type, _mm512_slli_epi64(binding, 4)), _mm512_slli_epi64(visibility, BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT)),
                                                    (__mmask8)section_symbols, _mm512_maskz_mov_epi64((__mmask8)tls_atom, _mm512_set1_epi64(BUSTER_LINK_SYMBOL_TLS)));
        const uint32_t in_atom = defined | section_symbols;
        import_store32(object->symbols + first, _mm512_maskz_expand_epi64(kept, _mm512_add_epi64(lane_index, _mm512_set1_epi64(at))), n);
        import_store32(table->name + at, zero, record_count);
        import_store32(table->atom + at, _mm512_maskz_compress_epi64(kept, _mm512_maskz_mov_epi64((__mmask8)in_atom, atom)), record_count);
        import_store64(table->value + at, _mm512_maskz_compress_epi64(kept, _mm512_maskz_mov_epi64((__mmask8)valued, value)), record_count);
        import_store64(table->size + at, _mm512_maskz_compress_epi64(kept, _mm512_maskz_mov_epi64((__mmask8)valued, size)), record_count);
        import_store8(table->flags + at, _mm512_maskz_compress_epi64(kept, flags), record_count);
        const __m512i sources = _mm512_mask_mov_epi64(_mm512_maskz_mov_epi64((__mmask8)in_atom, _mm512_set1_epi64(source)), (__mmask8)(regular & absolute),
                                                      _mm512_set1_epi64(BUSTER_LINK_SOURCE_ABSOLUTE));
        import_store8(table->source + at, _mm512_maskz_compress_epi64(kept, sources), record_count);
        import_store32(table->got_slot + at, zero, record_count);
        import_store32(table->dynamic_index + at, zero, record_count);
        import_store16(table->version + at, zero, record_count);
        import_store32(table->reference + at, zero, record_count);
        import_store32(name_offsets + added, _mm512_maskz_compress_epi64(kept, _mm512_maskz_mov_epi64((__mmask8)regular, name)), record_count);
        table->count += record_count;
        added += record_count;
    }
    // The names, in their symbols' order: each one's length first, a masked 64-byte load per name; then eight at a
    // time, the ids of the ones that aren't empty, their hashes (link_hash_rows) from a second masked load of each
    // (rather than the lengths' vector loaded back from the eight scalar stores that wrote them, which can't forward),
    // and their copies, into a push from the name arena per batch, which is where import_object_scalar's pushes put
    // them. A name past 63 bytes takes memchr, link_hash and memcpy.
    uint32_t* name_lengths_scratch = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, added);
    result = result && name_lengths_scratch != 0;
    for (uint32_t i = 0; result && failed == 0 && i < added; i += 1)
    {
        const uint32_t offset = name_offsets[i];
        const uint64_t length = offset != 0 ? import_name_length(names, offset) : 0;
        failed |= length > UINT32_MAX ? 1 : 0;
        name_lengths_scratch[i] = (uint32_t)length;
    }
    const uint8_t** name_bytes = link_names->bytes;
    uint32_t* name_lengths = link_names->length;
    uint64_t* name_hashes = link_names->hash;
    NameId* symbol_names = table->name;
    for (uint32_t first = 0; result && failed == 0 && first < added; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = added - first < BUSTER_IMPORT_BATCH ? added - first : BUSTER_IMPORT_BATCH;
        const uint32_t* offsets = name_offsets + first;
        const uint32_t* lengths = name_lengths_scratch + first;
        __m512i rows[BUSTER_IMPORT_BATCH];
        uint64_t hashes[BUSTER_IMPORT_BATCH];
        const uint32_t long_names = import_name_rows(names.pointer, offsets, lengths, n, rows);
        const __m512i length = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32((__mmask8)import_lane_mask(n), lengths));
        const __mmask8 nonempty = _mm512_test_epi64_mask(length, length);
        const uint32_t name_count = (uint32_t)__builtin_popcount(nonempty);
        const uint64_t batch_bytes = (uint64_t)_mm512_reduce_add_epi64(_mm512_maskz_add_epi64(nonempty, length, _mm512_set1_epi64(1)));
        const NameId at = link_names->count;
        failed |= name_count > link_names->capacity - at ? 1 : 0;
        uint8_t* copies = failed == 0 ? arena_push(link->name_arena, batch_bytes, 1) : 0;
        failed |= copies == 0 ? 1 : 0;
        if (failed == 0)
        {
            _mm512_storeu_si512(hashes, link_hash_rows(rows, length));
            import_store32(symbol_names + base + first, _mm512_maskz_expand_epi64(nonempty, _mm512_add_epi64(lane_index, _mm512_set1_epi64(at))), n);
            uint32_t rank = 0;
            for (uint32_t lane = 0; lane < n; lane += 1)
            {
                const uint32_t name_length = lengths[lane];
                const char* source = names.pointer + offsets[lane];
                if (name_length != 0 && (long_names >> lane & 1) == 0)
                {
                    // The row's bytes past the length are zero: its NUL among them.
                    _mm512_mask_storeu_epi8(copies, _bzhi_u64(~0ull, name_length + 1), rows[lane]);
                }
                else if (name_length != 0)
                {
                    memcpy(copies, source, (uint64_t)name_length + 1);
                    hashes[lane] = link_hash((const uint8_t*)source, name_length);
                }
                if (name_length != 0)
                {
                    name_bytes[at + rank] = copies;
                    name_lengths[at + rank] = name_length;
                    name_hashes[at + rank] = hashes[lane];
                    copies += (uint64_t)name_length + 1;
                    rank += 1;
                }
            }
            link_names->count += name_count;
        }
    }
    return result && failed == 0;
}

// What a pass of import_vector_fixups stored: the fixups up to `next`, the wide addends up to `wide`, and the lanes of
// the entries that can't be fixups (`failed`) and of the fields that start before the end of the one before them
// (`unordered`), each nonzero if any batch had one.
typedef struct ImportVectorFixups ImportVectorFixups;
struct ImportVectorFixups
{
    FixupId next;
    uint32_t wide;
    uint32_t failed;
    uint32_t unordered;
};

// The batches of import_vector_relocations over `count` entries at `records`, the section's or their sorted copy: each
// entry but R_X86_64_NONE's stored as a fixup from fixups->count, with its symbol's index as its target, until a batch
// has an entry that can't be one or a field out of order. Nothing is committed: the caller decides.
static ImportVectorFixups import_vector_fixups(LinkFixups* fixups, const char* records, uint64_t count, uint64_t atom_size, uint32_t symbol_count, __m512i kinds)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i lane_index = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i size = _mm512_set1_epi64((long long)atom_size);
    const __m512i symbols = _mm512_set1_epi64(symbol_count);
    ImportVectorFixups result = {
        .next = fixups->count,
        .wide = fixups->wide_count,
    };
    // The end of the last fixup's field, in every lane.
    __m512i last_end = zero;
    // The columns, in locals, which the stores can't change.
    uint32_t* offsets = fixups->offset;
    SymbolId* targets = fixups->target;
    int32_t* addend_column = fixups->addend;
    uint8_t* kinds_column = fixups->kind;
    int64_t* wide_addends = fixups->wide_addends;
    for (uint64_t first = 0; result.failed == 0 && result.unordered == 0 && first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = (uint32_t)(count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH);
        __m512i words[3];
        import_load_records3(records + first * sizeof(Elf64_Rela), n, words);
        // Elf64_Rela's words: r_offset; r_info, the type in the low 32 bits and the symbol in the high 32; r_addend.
        const __m512i offset = words[0];
        const __m512i type = _mm512_and_si512(words[1], _mm512_set1_epi64(BUSTER_IMPORT_LOW32));
        const __m512i symbol = _mm512_srli_epi64(words[1], 32);
        // R_X86_64_NONE entries are skipped.
        const uint32_t present = _mm512_test_epi64_mask(type, type) & import_lane_mask(n);
        const __mmask8 kept = (__mmask8)present;
        const __m512i entry = _mm512_maskz_and_epi64(_mm512_cmplt_epu64_mask(type, _mm512_set1_epi64(BUSTER_IMPORT_KIND_TYPES)), _mm512_permutexvar_epi8(type, kinds),
                                                     _mm512_set1_epi64(0xff));
        const __m512i kind = _mm512_and_si512(entry, _mm512_set1_epi64(BUSTER_IMPORT_KIND_BITS));
        const __m512i width = _mm512_sllv_epi64(_mm512_set1_epi64(4), _mm512_srli_epi64(entry, BUSTER_IMPORT_KIND_WIDE_SHIFT));
        const __m512i end = _mm512_add_epi64(offset, width);
        result.failed |= present & (_mm512_testn_epi64_mask(kind, kind) | _mm512_testn_epi64_mask(symbol, symbol) | _mm512_cmpge_epu64_mask(symbol, symbols) |
                                    _mm512_cmpgt_epu64_mask(offset, _mm512_set1_epi64(UINT32_MAX)) | _mm512_cmpgt_epu64_mask(end, size));
        const uint32_t fixup_count = (uint32_t)__builtin_popcount(present);
        // Most batches have neither R_X86_64_NONE entries, which the compression drops, nor wide addends.
        __m512i fields = offset;
        __m512i ends = end;
        __m512i addends = words[2];
        __m512i kept_kinds = kind;
        __m512i kept_symbols = symbol;
        if (present != import_lane_mask(n))
        {
            fields = _mm512_maskz_compress_epi64(kept, offset);
            ends = _mm512_maskz_compress_epi64(kept, end);
            addends = _mm512_maskz_compress_epi64(kept, words[2]);
            kept_kinds = _mm512_maskz_compress_epi64(kept, kind);
            kept_symbols = _mm512_maskz_compress_epi64(kept, symbol);
        }
        // Each field starts at or after the end of the one before it: the previous lane's, or the last batch's.
        const __m512i previous = _mm512_alignr_epi64(ends, last_end, 7);
        result.unordered |= _mm512_cmplt_epu64_mask(fields, previous) & import_lane_mask(fixup_count);
        last_end = _mm512_mask_permutexvar_epi64(last_end, fixup_count != 0 ? 0xff : 0, _mm512_set1_epi64((long long)fixup_count - 1), ends);
        // An addend outside int32 goes to the side table, and the fixup gets its index there.
        const uint32_t wides = ~_mm512_cmpeq_epi64_mask(addends, _mm512_cvtepi32_epi64(_mm512_cvtepi64_epi32(addends))) & import_lane_mask(fixup_count);
        __m512i field_addends = addends;
        __m512i kind_bytes = kept_kinds;
        if (wides != 0)
        {
            const uint32_t wide_count = (uint32_t)__builtin_popcount(wides);
            field_addends = _mm512_mask_blend_epi64((__mmask8)wides, addends, _mm512_maskz_expand_epi64((__mmask8)wides, _mm512_add_epi64(lane_index, _mm512_set1_epi64(result.wide))));
            kind_bytes = _mm512_mask_or_epi64(kept_kinds, (__mmask8)wides, kept_kinds, _mm512_set1_epi64(BUSTER_LINK_FIXUP_WIDE));
            import_store64(wide_addends + result.wide, _mm512_maskz_compress_epi64((__mmask8)wides, addends), wide_count);
            result.wide += wide_count;
        }
        import_store32(offsets + result.next, fields, fixup_count);
        // The symbol's index for now: the target is its symbol's record, which the caller loads.
        import_store32(targets + result.next, kept_symbols, fixup_count);
        import_store32(addend_column + result.next, field_addends, fixup_count);
        import_store8(kinds_column + result.next, kind_bytes, fixup_count);
        result.next += fixup_count;
    }
    return result;
}

// A copy of `count` entries at `records` in offset order, stably, as import_sort_relocations orders them: each entry's
// key, its offset above its index, built 8 at a time, then an LSD radix sort of the keys by the offset's bytes, as many
// passes as the largest offset has bytes, and the entries copied in the keys' order. 0 if an offset is past
// UINT32_MAX, which no fixup has, or the arena has no room.
static const char* import_vector_sort_relocations(const char* records, uint64_t count, Arena* arena)
{
    uint64_t* keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, 2 * count);
    char* sorted = BUSTER_ARENA_PUSH_ARRAY(arena, char, count * sizeof(Elf64_Rela));
    const __m512i lane_index = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    __m512i largest = _mm512_setzero_si512();
    for (uint64_t first = 0; keys != 0 && first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = (uint32_t)(count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH);
        __m512i words[3];
        import_load_records3(records + first * sizeof(Elf64_Rela), n, words);
        largest = _mm512_mask_max_epu64(largest, (__mmask8)import_lane_mask(n), largest, words[0]);
        import_store64(keys + first, _mm512_or_si512(_mm512_slli_epi64(words[0], 32), _mm512_add_epi64(lane_index, _mm512_set1_epi64((long long)first))), n);
    }
    const uint64_t offset_limit = _mm512_reduce_max_epu64(largest);
    const char* result = keys != 0 && sorted != 0 && offset_limit <= UINT32_MAX ? sorted : 0;
    const uint32_t passes = (uint32_t)((64 - __builtin_clzll(offset_limit | 1) + 7) / 8);
    uint64_t* from = keys;
    uint64_t* to = keys != 0 ? keys + count : 0;
    for (uint32_t pass = 0; result != 0 && pass < passes; pass += 1)
    {
        uint64_t starts[257] = { 0 };
        const uint32_t shift = 32 + 8 * pass;
        for (uint64_t e = 0; e < count; e += 1)
        {
            starts[((from[e] >> shift) & 0xff) + 1] += 1;
        }
        for (uint32_t digit = 0; digit < 256; digit += 1)
        {
            starts[digit + 1] += starts[digit];
        }
        for (uint64_t e = 0; e < count; e += 1)
        {
            const uint32_t digit = (uint32_t)((from[e] >> shift) & 0xff);
            to[starts[digit]] = from[e];
            starts[digit] += 1;
        }
        uint64_t* swapped = from;
        from = to;
        to = swapped;
    }
    for (uint64_t e = 0; result != 0 && e < count; e += 1)
    {
        memcpy(sorted + e * sizeof(Elf64_Rela), records + (from[e] & BUSTER_IMPORT_LOW32) * sizeof(Elf64_Rela), sizeof(Elf64_Rela));
    }
    return result;
}

// The relocation pass for one .rela section: the checks and fixups of import_relocations, eight entries at a time, in
// the file's order when its fields are in offset order, as a compiler's objects have them, and otherwise from a copy
// sorted by offset (GNU as writes a relaxed branch's relocation after the section's others, and glibc's and libgcc's
// archives have a tenth of their members so). Sorted, the fields must not overlap.
static bool import_vector_relocation_entries(Link* link, const ImportSymbols* symbols, const ImportObject* object, AtomId atom, String8 entries, __m512i kinds, Arena* scratch)
{
    LinkFixups* fixups = &link->fixups;
    LinkAtoms* atoms = &link->atoms;
    const uint64_t count = entries.length / sizeof(Elf64_Rela);
    bool result = entries.length % sizeof(Elf64_Rela) == 0 && count <= UINT32_MAX && (count == 0 || entries.pointer != 0);
    if (result && atom != 0 && count != 0)
    {
        const FixupId begin = fixups->count;
        const uint64_t length = scratch->length;
        // Room for every entry, whether or not each becomes a fixup; the exact path decides when there isn't.
        result = count <= fixups->capacity - fixups->count && count <= fixups->wide_capacity - fixups->wide_count;
        ImportVectorFixups pass = { 0 };
        if (result)
        {
            pass = import_vector_fixups(fixups, entries.pointer, count, atoms->size[atom], symbols->count, kinds);
        }
        if (result && pass.failed == 0 && pass.unordered != 0)
        {
            const char* sorted = import_vector_sort_relocations(entries.pointer, count, scratch);
            result = sorted != 0;
            pass = result ? import_vector_fixups(fixups, sorted, count, atoms->size[atom], symbols->count, kinds) : pass;
        }
        arena_pop_to(scratch, length);
        result = result && pass.failed == 0 && pass.unordered == 0;
        // Each target, a scalar load per fixup rather than a gather: every symbol index is within the table once
        // nothing failed.
        uint32_t missing = 0;
        SymbolId* targets = fixups->target;
        const SymbolId* imported = object->symbols;
        const FixupId end = result ? pass.next : begin;
        for (FixupId f = begin; f < end; f += 1)
        {
            const SymbolId target = imported[targets[f]];
            targets[f] = target;
            missing |= target == 0 ? 1 : 0;
        }
        // The atom's fixups: none before these, and bytes for them to patch.
        result = result && missing == 0 && (pass.next == begin || (atoms->bytes[atom] != 0 && atoms->fixup_count[atom] == 0));
        // The GOT kinds' instructions (import_got_kind): compares of 64 kinds at a time find them, a few in a hundred
        // fixups, and each is checked by its bytes. Every field is within the atom once nothing failed.
        for (FixupId group = begin; result && group < pass.next; group += 64)
        {
            const uint64_t left = pass.next - group;
            const __mmask64 valid = left >= 64 ? ~0ull : (1ull << left) - 1;
            const __m512i loaded = _mm512_and_si512(_mm512_maskz_loadu_epi8(valid, fixups->kind + group), _mm512_set1_epi8(BUSTER_LINK_FIXUP_KIND));
            const uint64_t got = _mm512_mask_cmpeq_epi8_mask(valid, loaded, _mm512_set1_epi8(BUSTER_LINK_FIXUP_CALL_GOT)) |
                                 _mm512_mask_cmpeq_epi8_mask(valid, loaded, _mm512_set1_epi8(BUSTER_LINK_FIXUP_LOAD_GOT));
            for (uint64_t found = got; found != 0; found &= found - 1)
            {
                const FixupId f = group + (FixupId)__builtin_ctzll(found);
                const uint8_t wide = fixups->kind[f] & BUSTER_LINK_FIXUP_WIDE;
                const int64_t addend = wide != 0 ? fixups->wide_addends[(uint32_t)fixups->addend[f]] : fixups->addend[f];
                fixups->kind[f] = (uint8_t)(import_got_kind(fixups->kind[f] & BUSTER_LINK_FIXUP_KIND, atoms->bytes[atom], fixups->offset[f], addend) | wide);
            }
        }
        if (result && pass.next != begin)
        {
            atoms->fixup_begin[atom] = begin;
            atoms->fixup_count[atom] = pass.next - begin;
        }
        if (result)
        {
            fixups->count = pass.next;
            fixups->wide_count = pass.wide;
        }
    }
    return result;
}

static bool import_vector_relocations(Link* link, const ImportElf* elf, const ImportSymbols* symbols, uint32_t index, const ImportObject* object, __m512i kinds, Arena* scratch)
{
    const Elf64_Shdr header = import_section_header(elf, index);
    uint64_t count = 0;
    const bool table = import_table(&header, sizeof(Elf64_Rela), &count) && count <= UINT32_MAX;
    bool result = table && symbols->section != 0 && header.sh_link == symbols->section && header.sh_info != SHN_UNDEF && header.sh_info < elf->section_count;
    if (result)
    {
        result = import_vector_relocation_entries(link, symbols, object, object->section_atoms[header.sh_info], import_section_bytes(elf, &header), kinds, scratch);
    }
    return result;
}

static bool import_object_vector(Link* link, String8 bytes, uint32_t unit, uint8_t source, Arena* arena, ImportObject* object)
{
    ImportElf elf;
    ImportSymbols symbols = { 0 };
    ImportVectorSections found = { 0 };
    uint32_t names = 0;
    *object = (ImportObject){ 0 };
    bool result = import_elf_header(&elf, bytes, ET_REL, &names).error == IMPORT_ERROR_NONE && import_elf_names(&elf, names) && source < BUSTER_LINK_SOURCE_COUNT;
    Scratch scratch = scratch_begin(arena);
    if (result)
    {
        object->section_atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, elf.section_count);
        found.symbol_tables = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, elf.section_count);
        found.extended = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, elf.section_count);
        found.relocations = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, elf.section_count);
        result = object->section_atoms != 0 && found.symbol_tables != 0 && found.extended != 0 && found.relocations != 0;
    }
    if (result)
    {
        memset(object->section_atoms, 0, (uint64_t)elf.section_count * sizeof(AtomId));
        object->section_count = elf.section_count;
    }
    result = result && import_vector_sections(link, &elf, unit, object, &found);
    result = result && import_vector_symbol_table(&elf, &found, &symbols);
    if (result)
    {
        object->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, symbols.count);
        result = object->symbols != 0;
    }
    if (result)
    {
        memset(object->symbols, 0, (uint64_t)symbols.count * sizeof(SymbolId));
        object->symbol_count = symbols.count;
    }
    result = result && import_vector_symbols(link, &elf, &symbols, source, object, scratch.arena);
    const __m512i kinds = import_kind_table();
    for (uint32_t i = 0; result && i < found.relocation_count; i += 1)
    {
        result = import_vector_relocations(link, &elf, &symbols, found.relocations[i], object, kinds, scratch.arena);
    }
    // The groups, which few objects have, as the exact path reads them.
    if (result && found.grouped)
    {
        result = import_object_groups(&elf, &symbols, object, arena).error == IMPORT_ERROR_NONE;
    }
    scratch_end(scratch);
    return result;
}

BUSTER_LINKER_ENTRY ImportResult import_object(Link* link, String8 bytes, uint32_t unit, uint8_t source, Arena* arena, ImportObject* object)
{
#if !BUSTER_HOST_VECTOR
    return import_object_scalar(link, bytes, unit, source, arena, object);
#else
    const ImportMark mark = import_mark(link, arena);
    ImportResult result = { 0 };
    if (!import_object_vector(link, bytes, unit, source, arena, object))
    {
        import_restore(link, arena, mark);
        result = import_object_scalar(link, bytes, unit, source, arena, object);
    }
    return result;
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// Archives

#define BUSTER_IMPORT_ARCHIVE_MAGIC "!<arch>\n"
#define BUSTER_IMPORT_THIN_ARCHIVE_MAGIC "!<thin>\n"
#define BUSTER_IMPORT_ARCHIVE_HEADER_BYTES 60

// A decimal field of an archive header, its digits followed by spaces: false if it has none, or anything else.
static bool import_archive_number(const char* field, uint32_t width, uint64_t* value)
{
    uint64_t result = 0;
    uint32_t digits = 0;
    bool spaces = false;
    bool valid = true;
    for (uint32_t i = 0; i < width; i += 1)
    {
        const char c = field[i];
        const bool digit = c >= '0' && c <= '9';
        valid = valid && (digit ? !spaces : c == ' ');
        spaces = spaces || c == ' ';
        result = digit && valid ? result * 10 + (uint64_t)(c - '0') : result;
        digits += digit ? 1 : 0;
    }
    *value = result;
    return valid && digits != 0;
}

// A member's name, from its header's field: up to the '/' that ends it (GNU), or from the long names' table for "/N".
// In a thin archive, "/N:M" names a member of another archive (GNU ar's when a regular archive is added to a thin one):
// the long name is that archive's path, and M is the member's header's offset in it, which *nested gets plus 1 (0 for
// any other name).
static bool import_archive_name(const char* field, String8 long_names, bool thin, String8* name, uint64_t* nested)
{
    bool result = true;
    *nested = 0;
    if (field[0] == '/' && field[1] >= '0' && field[1] <= '9')
    {
        // The offset's digits, in a thin archive maybe ':' and the member's digits, then spaces to the field's end.
        uint64_t offset = 0;
        uint64_t member = 0;
        uint32_t at = 1;
        uint32_t member_digits = 0;
        for (; at < 16 && field[at] >= '0' && field[at] <= '9'; at += 1)
        {
            offset = offset * 10 + (uint64_t)(field[at] - '0');
        }
        const bool reference = thin && at < 16 && field[at] == ':';
        at += reference ? 1 : 0;
        for (; reference && at < 16 && field[at] >= '0' && field[at] <= '9'; at += 1)
        {
            member = member * 10 + (uint64_t)(field[at] - '0');
            member_digits += 1;
        }
        bool spaces = true;
        for (; at < 16; at += 1)
        {
            spaces = spaces && field[at] == ' ';
        }
        result = spaces && (!reference || member_digits != 0) && offset < long_names.length;
        *nested = result && reference ? member + 1 : 0;
        if (result)
        {
            const char* start = long_names.pointer + offset;
            const char* end = memchr(start, '\n', long_names.length - offset);
            result = end != 0 && end > start && end[-1] == '/';
            name->pointer = (char*)start;
            name->length = result ? (uint64_t)(end - 1 - start) : 0;
        }
    }
    else
    {
        const char* end = memchr(field, '/', 16);
        uint64_t length = end != 0 ? (uint64_t)(end - field) : 16;
        while (end == 0 && length > 0 && field[length - 1] == ' ')
        {
            length -= 1;
        }
        name->pointer = (char*)field;
        name->length = length;
        result = length != 0;
    }
    return result;
}

// The index of the member whose header is at `offset`, by binary search, or member_count if none is.
static uint32_t import_archive_member_at(const ImportArchive* archive, uint64_t offset)
{
    uint32_t low = 0;
    uint32_t high = archive->member_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (archive->members[middle].offset < offset)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < archive->member_count && archive->members[low].offset == offset ? low : archive->member_count;
}

static uint64_t import_big_endian(const char* bytes, uint32_t width)
{
    uint64_t result = 0;
    for (uint32_t i = 0; i < width; i += 1)
    {
        result = (result << 8) | (uint8_t)bytes[i];
    }
    return result;
}

// Reads the symbol index (GNU's "/" with 32-bit offsets, or "/SYM64/" with 64-bit ones): a count, the members' header
// offsets, then the names, each ending with a NUL.
static ImportResult import_archive_index(ImportArchive* archive, String8 index, uint32_t width, uint64_t index_offset, Arena* arena)
{
    ImportResult result = { 0 };
    const uint64_t count = index.length >= width ? import_big_endian(index.pointer, width) : 0;
    const bool fits = index.length >= width && count <= (index.length - width) / width && count <= UINT32_MAX;
    if (!fits)
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, index_offset);
    }
    else
    {
        archive->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, ImportArchiveSymbol, count);
        result = archive->symbols == 0 && count != 0 ? import_fail(IMPORT_ERROR_MEMORY, index_offset) : result;
        const String8 names = {
            .pointer = index.pointer + width + count * width,
            .length = index.length - width - count * width,
        };
        uint64_t name_offset = 0;
        for (uint64_t i = 0; result.error == IMPORT_ERROR_NONE && i < count; i += 1)
        {
            const uint64_t member_offset = import_big_endian(index.pointer + width + i * width, width);
            String8 name = { 0 };
            const uint32_t member = import_archive_member_at(archive, member_offset);
            if (!import_string(names, name_offset, &name) || member == archive->member_count)
            {
                result = import_fail(IMPORT_ERROR_BOUNDS, index_offset);
                result.symbol = (uint32_t)i;
            }
            else
            {
                archive->symbols[i] = (ImportArchiveSymbol){
                    .name = name,
                    .member = member,
                };
                name_offset += name.length + 1;
            }
        }
        archive->symbol_count = result.error == IMPORT_ERROR_NONE ? (uint32_t)count : 0;
    }
    return result;
}

static ImportResult import_archive_scalar(ImportArchive* archive, String8 bytes, Arena* arena)
{
    ImportResult result = { 0 };
    *archive = (ImportArchive){ 0 };
    const uint64_t magic = sizeof(BUSTER_IMPORT_ARCHIVE_MAGIC) - 1;
    const bool thin = bytes.length >= magic && memcmp(bytes.pointer, BUSTER_IMPORT_THIN_ARCHIVE_MAGIC, magic) == 0;
    if (!thin && (bytes.length < magic || memcmp(bytes.pointer, BUSTER_IMPORT_ARCHIVE_MAGIC, magic) != 0))
    {
        result = import_fail(IMPORT_ERROR_FORMAT, 0);
    }
    archive->thin = thin;
    // Two passes over the headers: count the members, then fill them; the index refers to them by offset. A thin
    // archive's special members have their bytes after their headers, and its regular members only their headers.
    String8 long_names = { 0 };
    String8 index = { 0 };
    uint32_t index_width = 0;
    uint64_t index_offset = 0;
    uint64_t members = 0;
    for (uint32_t pass = 0; result.error == IMPORT_ERROR_NONE && pass < 2; pass += 1)
    {
        if (pass == 1)
        {
            archive->members = BUSTER_ARENA_PUSH_ARRAY(arena, ImportMember, members);
            result = archive->members == 0 && members != 0 ? import_fail(IMPORT_ERROR_MEMORY, 0) : result;
        }
        uint64_t offset = magic;
        uint32_t member = 0;
        while (result.error == IMPORT_ERROR_NONE && offset < bytes.length)
        {
            const char* header = bytes.pointer + offset;
            uint64_t size = 0;
            const bool header_fits = import_within(bytes.length, offset, BUSTER_IMPORT_ARCHIVE_HEADER_BYTES);
            const String8 field = {
                .pointer = (char*)header,
                .length = header_fits ? 16 : 0,
            };
            const bool symbol_index = import_starts_with(field, "/ ");
            const bool symbol_index64 = import_starts_with(field, "/SYM64/ ");
            const bool long_name_table = import_starts_with(field, "// ");
            const bool inline_bytes = !thin || symbol_index || symbol_index64 || long_name_table;
            const bool valid = header_fits && header[58] == '`' && header[59] == '\n' && import_archive_number(header + 48, 10, &size) &&
                               (!inline_bytes || import_within(bytes.length, offset + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES, size));
            if (!valid)
            {
                result = import_fail(IMPORT_ERROR_BOUNDS, offset);
            }
            else
            {
                const String8 data = {
                    .pointer = (char*)header + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES,
                    .length = inline_bytes ? size : 0,
                };
                if (import_starts_with(field, "#1/"))
                {
                    result = import_fail(IMPORT_ERROR_UNSUPPORTED, offset);
                }
                else if (symbol_index || symbol_index64)
                {
                    index = data;
                    index_width = symbol_index ? 4 : 8;
                    index_offset = offset;
                }
                else if (long_name_table)
                {
                    long_names = data;
                }
                else if (pass == 0)
                {
                    members += 1;
                }
                else
                {
                    String8 name = { 0 };
                    uint64_t nested = 0;
                    if (!import_archive_name(header, long_names, thin, &name, &nested))
                    {
                        result = import_fail(IMPORT_ERROR_BOUNDS, offset);
                    }
                    archive->members[member] = (ImportMember){
                        .name = name,
                        .bytes = data,
                        .offset = offset,
                        .nested = nested,
                    };
                    member += 1;
                }
                // Members start at even offsets: one padding byte after an odd size.
                offset += BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + data.length + (data.length & 1);
            }
        }
        if (pass == 1)
        {
            archive->member_count = member;
        }
        if (members > UINT32_MAX)
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, 0);
        }
    }
    if (result.error == IMPORT_ERROR_NONE && index_width != 0)
    {
        result = import_archive_index(archive, index, index_width, index_offset, arena);
    }
    return result;
}

// The vector path. The symbol index lists the header offset of each member that defines a global symbol, in archive
// order: those members' headers are read on their own, from the index, rather than by walking the chain of headers
// from the start, and from each one only the members up to the next indexed one are walked, the members without a
// global symbol (122 of glibc 2.39's libc.a's 2,211, none of libc_nonshared.a's). The offsets are byte-swapped eight at
// a time (vpshufb), checked in order, and ranked (an expand of the lanes where the offset changes, then a prefix
// maximum); each header's size is parsed in a vector (import_archive_size); and the names after the offsets are split
// at their NULs, found 64 bytes at a time. An archive without an index, with a special member past the first ones, or
// with anything else out of the ordinary is the exact path's.

// A size field of an archive header, ten bytes of digits followed by spaces, in one 16-byte load: the digits' mask must
// be a run from the first byte and the rest spaces, as import_archive_number has it; the digits, right-aligned by a
// shuffle, become the number by multiply-adds of pairs, fours and eights (the 10, 100 and 10,000 steps).
static bool import_archive_size(const char* field, uint64_t* value)
{
    const __mmask16 within = 0x3ff;
    const __m128i bytes = _mm_maskz_loadu_epi8(within, field);
    const __m128i values = _mm_sub_epi8(bytes, _mm_set1_epi8('0'));
    const uint32_t digits = _mm_mask_cmplt_epu8_mask(within, values, _mm_set1_epi8(10));
    const uint32_t spaces = _mm_mask_cmpeq_epi8_mask(within, bytes, _mm_set1_epi8(' '));
    const uint32_t count = (uint32_t)__builtin_popcount(digits);
    const bool result = (digits | spaces) == within && digits != 0 && (digits & (digits + 1)) == 0;
    // Position p of 16 takes digit p - (16 - count); a negative index, its top bit set, takes 0.
    const __m128i positions = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), _mm_set1_epi8((char)(16 - count)));
    const __m128i aligned = _mm_shuffle_epi8(_mm_maskz_mov_epi8((__mmask16)digits, values), positions);
    const __m128i pairs = _mm_maddubs_epi16(aligned, _mm_setr_epi8(10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1));
    const __m128i fours = _mm_madd_epi16(pairs, _mm_setr_epi16(100, 1, 100, 1, 100, 1, 100, 1));
    const __m128i eights = _mm_madd_epi16(_mm_packus_epi32(fours, fours), _mm_setr_epi16(10000, 1, 10000, 1, 10000, 1, 10000, 1));
    *value = (uint64_t)(uint32_t)_mm_cvtsi128_si32(eights) * 100000000ull + (uint32_t)_mm_extract_epi32(eights, 1);
    return result;
}

// A regular member's header at `at`, on its own: within the file with its bytes (a thin archive's member has none
// there), its size, and its name, not a special member's or a BSD one. *end is where the next header is.
static bool import_vector_member(String8 bytes, String8 long_names, uint64_t at, bool thin, ImportMember* member, uint64_t* end)
{
    const char* header = bytes.pointer + at;
    uint64_t size = 0;
    String8 name = { 0 };
    uint64_t nested = 0;
    bool result = import_within(bytes.length, at, BUSTER_IMPORT_ARCHIVE_HEADER_BYTES) && header[58] == '`' && header[59] == '\n' && import_archive_size(header + 48, &size) &&
                  (thin || import_within(bytes.length, at + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES, size));
    result = result && header[0] != '#' && !(header[0] == '/' && (header[1] == ' ' || header[1] == '/' || header[1] == 'S')) &&
             import_archive_name(header, long_names, thin, &name, &nested);
    const uint64_t length = thin ? 0 : size;
    if (result)
    {
        *member = (ImportMember){
            .name = name,
            .bytes = {
                .pointer = (char*)header + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES,
                .length = length,
            },
            .offset = at,
            .nested = nested,
        };
        *end = at + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + length + (length & 1);
    }
    return result;
}

// The index part of the vector path (import_archive_lazy_vector), from the special members GNU ar writes first (the
// symbol index, then the long names' table; their bytes are after their headers in a thin archive too) to the
// symbols, each with its member's rank among the indexed members; *first_member is where the members start.
static bool import_archive_index_parse(ImportArchiveIndex* archive_index, String8 bytes, Arena* arena, uint64_t* first_member)
{
    const uint64_t magic = sizeof(BUSTER_IMPORT_ARCHIVE_MAGIC) - 1;
    *archive_index = (ImportArchiveIndex){ 0 };
    const bool thin = bytes.length >= magic && memcmp(bytes.pointer, BUSTER_IMPORT_THIN_ARCHIVE_MAGIC, magic) == 0;
    bool result = thin || (bytes.length >= magic && memcmp(bytes.pointer, BUSTER_IMPORT_ARCHIVE_MAGIC, magic) == 0);
    archive_index->thin = thin;
    String8 index = { 0 };
    String8 long_names = { 0 };
    uint32_t width = 0;
    uint64_t offset = magic;
    bool special = true;
    for (uint32_t step = 0; result && special && step < 3 && offset < bytes.length; step += 1)
    {
        // The first header without a special member's name ends the special ones; a regular member's header is checked
        // when the member is read.
        const char* header = bytes.pointer + offset;
        const String8 field = {
            .pointer = (char*)header,
            .length = import_within(bytes.length, offset, 16) ? 16 : 0,
        };
        const bool index32 = import_starts_with(field, "/ ");
        const bool index64 = import_starts_with(field, "/SYM64/ ");
        const bool names = import_starts_with(field, "// ");
        special = index32 || index64 || names;
        uint64_t size = 0;
        result = !special || (import_within(bytes.length, offset, BUSTER_IMPORT_ARCHIVE_HEADER_BYTES) && header[58] == '`' && header[59] == '\n' &&
                              import_archive_size(header + 48, &size) && import_within(bytes.length, offset + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES, size));
        const String8 data = {
            .pointer = (char*)header + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES,
            .length = size,
        };
        index = result && (index32 || index64) ? data : index;
        width = result && index32 ? 4 : result && index64 ? 8 : width;
        long_names = result && names ? data : long_names;
        offset += result && special ? BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + size + (size & 1) : 0;
    }
    *first_member = offset;
    archive_index->long_names = long_names;
    archive_index->indexed = width != 0;
    archive_index->members = offset < bytes.length;
    const uint64_t count = width != 0 && index.length >= width ? import_big_endian(index.pointer, width) : 0;
    // An index of at least one symbol: the exact path reads an archive without one.
    result = result && width != 0 && count != 0 && count <= (index.length - width) / width && count <= UINT32_MAX;
    Scratch scratch = scratch_begin(arena);
    uint64_t* offsets = result ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, count) : 0;
    uint32_t* ranks = result ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, count) : 0;
    archive_index->member_offsets = result ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count) : 0;
    archive_index->symbols = result ? BUSTER_ARENA_PUSH_ARRAY(arena, ImportArchiveSymbol, count) : 0;
    result = result && offsets != 0 && ranks != 0 && archive_index->member_offsets != 0 && archive_index->symbols != 0;
    // The offsets, big-endian: each vector's bytes reversed within each lane, then widened to 64 bits.
    const __m512i reverse32 = _mm512_broadcast_i32x4(_mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12));
    const __m512i reverse64 = _mm512_broadcast_i32x4(_mm_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8));
    const char* table = index.pointer + width;
    for (uint64_t first = 0; result && first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = (uint32_t)(count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH);
        __m512i lanes;
        if (width == 4)
        {
            const __m256i loaded = _mm256_maskz_loadu_epi32((__mmask8)import_lane_mask(n), table + first * 4);
            lanes = _mm512_cvtepu32_epi64(_mm512_castsi512_si256(_mm512_shuffle_epi8(_mm512_castsi256_si512(loaded), reverse32)));
        }
        else
        {
            lanes = _mm512_shuffle_epi8(_mm512_maskz_loadu_epi64((__mmask8)import_lane_mask(n), table + first * 8), reverse64);
        }
        import_store64(offsets + first, lanes, n);
    }
    // In archive order, and the indexed members: each offset where it changes, ranked by an expand of the changes and a
    // prefix maximum.
    const __m512i lane_index = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    __m512i previous = _mm512_set1_epi64(-1);
    __m512i last_rank = _mm512_set1_epi64(-1);
    uint32_t indexed = 0;
    uint32_t failed = 0;
    for (uint64_t first = 0; result && failed == 0 && first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = (uint32_t)(count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH);
        const __mmask8 loaded = (__mmask8)import_lane_mask(n);
        const __m512i lanes = _mm512_maskz_loadu_epi64(loaded, offsets + first);
        // Each lane against the one before it, the last batch's last for lane 0; the first offset has none before it.
        const __m512i before = _mm512_alignr_epi64(lanes, previous, 7);
        const uint32_t compared = loaded & (first == 0 ? 0xfe : 0xff);
        const uint32_t changes = (_mm512_cmpneq_epi64_mask(lanes, before) & compared) | (first == 0 ? 1u : 0u);
        failed |= _mm512_cmplt_epu64_mask(lanes, before) & compared;
        const uint32_t new_members = (uint32_t)__builtin_popcount(changes);
        // The prefix maximum within the vector (shifts by 1, 2 and 4 lanes), then the last batch's rank, which the shifts
        // bring only into some lanes.
        const __m512i none = _mm512_set1_epi64(-1);
        __m512i rank = _mm512_mask_mov_epi64(none, (__mmask8)changes, _mm512_maskz_expand_epi64((__mmask8)changes, _mm512_add_epi64(lane_index, _mm512_set1_epi64(indexed))));
        rank = _mm512_max_epi64(rank, _mm512_alignr_epi64(rank, none, 7));
        rank = _mm512_max_epi64(rank, _mm512_alignr_epi64(rank, none, 6));
        rank = _mm512_max_epi64(rank, _mm512_alignr_epi64(rank, none, 4));
        rank = _mm512_max_epi64(rank, last_rank);
        import_store32(ranks + first, rank, n);
        import_store64(archive_index->member_offsets + indexed, _mm512_maskz_compress_epi64((__mmask8)changes, lanes), new_members);
        indexed += new_members;
        previous = _mm512_permutexvar_epi64(_mm512_set1_epi64(n - 1), lanes);
        last_rank = _mm512_permutexvar_epi64(_mm512_set1_epi64(n - 1), rank);
    }
    result = result && failed == 0;
    archive_index->member_count = result ? indexed : 0;
    // The names after the offsets, split at their NULs, 64 bytes at a time.
    const String8 names = {
        .pointer = index.pointer + width + count * width,
        .length = result ? index.length - width - count * width : 0,
    };
    uint64_t name_start = 0;
    uint32_t named = 0;
    for (uint64_t first = 0; result && named < count && first < names.length; first += 64)
    {
        const uint64_t remaining = names.length - first;
        const __mmask64 loaded = _bzhi_u64(~0ull, (uint32_t)(remaining < 64 ? remaining : 64));
        const __m512i block = _mm512_maskz_loadu_epi8(loaded, names.pointer + first);
        uint64_t nuls = _mm512_mask_testn_epi8_mask(loaded, block, block);
        while (nuls != 0 && named < count)
        {
            const uint64_t end = first + _tzcnt_u64(nuls);
            archive_index->symbols[named] = (ImportArchiveSymbol){
                .name = {
                    .pointer = names.pointer + name_start,
                    .length = end - name_start,
                },
                .member = ranks[named],
            };
            name_start = end + 1;
            named += 1;
            nuls &= nuls - 1;
        }
    }
    result = result && named == count;
    archive_index->symbol_count = result ? (uint32_t)count : 0;
    archive_index->member_count = result ? archive_index->member_count : 0;
    scratch_end(scratch);
    return result;
}

static bool import_archive_vector(ImportArchive* archive, String8 bytes, Arena* arena)
{
    *archive = (ImportArchive){ 0 };
    ImportArchiveIndex index;
    uint64_t first_member = 0;
    bool result = import_archive_index_parse(&index, bytes, arena, &first_member);
    const bool thin = index.thin;
    const String8 long_names = index.long_names;
    const uint32_t indexed = index.member_count;
    const uint64_t* member_offsets = index.member_offsets;
    archive->thin = thin;
    // Each indexed member's header on its own, and the members before the next indexed one walked: none, unless a
    // member has no global symbol. Into scratch memory first, as many as there is room for, then the exact count into
    // the arena; places[j] is the member that indexed offset j is.
    Scratch scratch = scratch_begin(arena);
    const uint64_t most = result ? (bytes.length - first_member) / BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + 1 : 0;
    ImportMember* members = result ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ImportMember, most) : 0;
    uint32_t* places = result ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, indexed) : 0;
    result = result && members != 0 && places != 0;
    uint64_t member_count = 0;
    uint64_t cursor = first_member;
    for (uint32_t j = 0; result && j <= indexed; j += 1)
    {
        const uint64_t next = j < indexed ? member_offsets[j] : bytes.length;
        while (result && cursor < next)
        {
            result = member_count < most && import_vector_member(bytes, long_names, cursor, thin, &members[member_count], &cursor);
            member_count += 1;
        }
        result = result && (j == indexed || cursor == next);
        if (result && j < indexed)
        {
            places[j] = (uint32_t)member_count;
            result = member_count < most && import_vector_member(bytes, long_names, next, thin, &members[member_count], &cursor);
            member_count += 1;
        }
    }
    result = result && member_count <= UINT32_MAX;
    archive->members = result ? BUSTER_ARENA_PUSH_ARRAY(arena, ImportMember, member_count) : 0;
    result = result && archive->members != 0;
    if (result)
    {
        memcpy(archive->members, members, member_count * sizeof(ImportMember));
        archive->member_count = (uint32_t)member_count;
        // Each symbol's member, from its rank among the indexed members.
        archive->symbols = index.symbols;
        for (uint32_t i = 0; i < index.symbol_count; i += 1)
        {
            archive->symbols[i].member = places[index.symbols[i].member];
        }
        archive->symbol_count = index.symbol_count;
    }
    archive->member_count = result ? archive->member_count : 0;
    scratch_end(scratch);
    return result;
}

static ImportResult import_archive(ImportArchive* archive, String8 bytes, Arena* arena)
{
#if !BUSTER_HOST_VECTOR
    return import_archive_scalar(archive, bytes, arena);
#else
    const uint64_t length = arena->length;
    ImportResult result = { 0 };
    if (!import_archive_vector(archive, bytes, arena))
    {
        arena_pop_to(arena, length);
        result = import_archive_scalar(archive, bytes, arena);
    }
    return result;
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// Archives' indices alone
//
// A link that loads only the members it needs reads an archive's symbol index and nothing else up front: reading every
// member's header, as import_archive does, touches a page of the file per member (2,231 of glibc's libc.a's), where the
// index is a few pages at the start. The vector path is import_archive_vector's index part (import_archive_index_parse:
// the offsets byte-swapped eight at a time and ranked by an expand of their changes and a prefix maximum, the names
// split at their NULs 64 bytes at a time), which gives up on an index out of archive order; the exact path reads an
// entry at a time and sorts such an index. A member's header is read when it is loaded, the same way on both paths.

static bool import_archive_lazy_vector(ImportArchiveIndex* index, String8 bytes, Arena* arena)
{
    uint64_t first_member = 0;
    return import_archive_index_parse(index, bytes, arena, &first_member);
}

// Sorts `count` offsets in place, bottom-up (no recursion), through `spare`.
static void import_sort_offsets(uint64_t* values, uint64_t* spare, uint64_t count)
{
    uint64_t* from = values;
    uint64_t* to = spare;
    for (uint64_t width = 1; width < count; width *= 2)
    {
        for (uint64_t start = 0; start < count; start += 2 * width)
        {
            const uint64_t middle = start + width < count ? start + width : count;
            const uint64_t end = start + 2 * width < count ? start + 2 * width : count;
            uint64_t left = start;
            uint64_t right = middle;
            for (uint64_t out = start; out < end; out += 1)
            {
                const bool take_left = left < middle && (right >= end || from[left] <= from[right]);
                to[out] = take_left ? from[left] : from[right];
                left += take_left ? 1 : 0;
                right += take_left ? 0 : 1;
            }
        }
        uint64_t* swap = from;
        from = to;
        to = swap;
    }
    if (from != values)
    {
        memcpy(values, from, count * sizeof(uint64_t));
    }
}

static ImportResult import_archive_lazy_scalar(ImportArchiveIndex* index, String8 bytes, Arena* arena)
{
    ImportResult result = { 0 };
    *index = (ImportArchiveIndex){ 0 };
    const uint64_t magic = sizeof(BUSTER_IMPORT_ARCHIVE_MAGIC) - 1;
    const bool thin = bytes.length >= magic && memcmp(bytes.pointer, BUSTER_IMPORT_THIN_ARCHIVE_MAGIC, magic) == 0;
    if (!thin && (bytes.length < magic || memcmp(bytes.pointer, BUSTER_IMPORT_ARCHIVE_MAGIC, magic) != 0))
    {
        result = import_fail(IMPORT_ERROR_FORMAT, 0);
    }
    index->thin = thin;
    // The special members at the start: the symbol index and the long names' table, at most three of them, as the
    // vector path reads them.
    String8 table = { 0 };
    uint32_t width = 0;
    uint64_t table_offset = 0;
    uint64_t offset = magic;
    bool special = true;
    for (uint32_t step = 0; result.error == IMPORT_ERROR_NONE && special && step < 3 && offset < bytes.length; step += 1)
    {
        // The first header without a special member's name ends the special ones; a regular member's header is checked
        // when the member is read.
        const char* header = bytes.pointer + offset;
        const String8 field = {
            .pointer = (char*)header,
            .length = import_within(bytes.length, offset, 16) ? 16 : 0,
        };
        const bool index32 = import_starts_with(field, "/ ");
        const bool index64 = import_starts_with(field, "/SYM64/ ");
        const bool names = import_starts_with(field, "// ");
        special = index32 || index64 || names;
        uint64_t size = 0;
        const bool valid = !special || (import_within(bytes.length, offset, BUSTER_IMPORT_ARCHIVE_HEADER_BYTES) && header[58] == '`' && header[59] == '\n' &&
                                        import_archive_number(header + 48, 10, &size) && import_within(bytes.length, offset + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES, size));
        const String8 data = {
            .pointer = (char*)header + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES,
            .length = size,
        };
        if (!valid)
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, offset);
        }
        else if (special)
        {
            table = index32 || index64 ? data : table;
            table_offset = index32 || index64 ? offset : table_offset;
            width = index32 ? 4 : index64 ? 8 : width;
            index->long_names = names ? data : index->long_names;
            offset += BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + size + (size & 1);
        }
    }
    index->indexed = width != 0;
    index->members = offset < bytes.length;
    const uint64_t count = result.error == IMPORT_ERROR_NONE && width != 0 && table.length >= width ? import_big_endian(table.pointer, width) : 0;
    if (result.error == IMPORT_ERROR_NONE && width != 0 && (table.length < width || count > (table.length - width) / width || count > UINT32_MAX))
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, table_offset);
    }
    Scratch scratch = scratch_begin(arena);
    uint64_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, count + 1);
    uint64_t* spare = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, count + 1);
    index->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, ImportArchiveSymbol, count + 1);
    index->member_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count + 1);
    if (result.error == IMPORT_ERROR_NONE && (offsets == 0 || spare == 0 || index->symbols == 0 || index->member_offsets == 0))
    {
        result = import_fail(IMPORT_ERROR_MEMORY, 0);
    }
    const String8 names = {
        .pointer = table.pointer + width + count * width,
        .length = result.error == IMPORT_ERROR_NONE && width != 0 ? table.length - width - count * width : 0,
    };
    uint64_t name_offset = 0;
    for (uint64_t i = 0; result.error == IMPORT_ERROR_NONE && i < count; i += 1)
    {
        offsets[i] = import_big_endian(table.pointer + width + i * width, width);
        if (!import_string(names, name_offset, &index->symbols[i].name))
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, table_offset);
            result.symbol = (uint32_t)i;
        }
        name_offset += index->symbols[i].name.length + 1;
    }
    // The indexed members: the distinct offsets in order, each symbol's member its offset's rank among them.
    uint32_t members = 0;
    if (result.error == IMPORT_ERROR_NONE)
    {
        memcpy(spare, offsets, count * sizeof(uint64_t));
        import_sort_offsets(spare, index->member_offsets, count);
        for (uint64_t i = 0; i < count; i += 1)
        {
            if (members == 0 || spare[i] != index->member_offsets[members - 1])
            {
                index->member_offsets[members] = spare[i];
                members += 1;
            }
        }
        for (uint64_t i = 0; i < count; i += 1)
        {
            uint32_t low = 0;
            uint32_t high = members;
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = index->member_offsets[middle] < offsets[i] ? middle + 1 : low;
                high = index->member_offsets[middle] < offsets[i] ? high : middle;
            }
            index->symbols[i].member = low;
        }
    }
    index->member_count = result.error == IMPORT_ERROR_NONE ? members : 0;
    index->symbol_count = result.error == IMPORT_ERROR_NONE ? (uint32_t)count : 0;
    scratch_end(scratch);
    return result;
}

static ImportResult import_archive_lazy(ImportArchiveIndex* index, String8 bytes, Arena* arena)
{
#if !BUSTER_HOST_VECTOR
    return import_archive_lazy_scalar(index, bytes, arena);
#else
    const uint64_t length = arena->length;
    ImportResult result = { 0 };
    if (!import_archive_lazy_vector(index, bytes, arena))
    {
        arena_pop_to(arena, length);
        result = import_archive_lazy_scalar(index, bytes, arena);
    }
    return result;
#endif
}

static ImportResult import_archive_member(ImportMember* member, String8 bytes, const ImportArchiveIndex* index, uint32_t rank)
{
    const uint64_t at = rank < index->member_count ? index->member_offsets[rank] : bytes.length;
    uint64_t end = 0;
    ImportResult result = { 0 };
    *member = (ImportMember){ 0 };
    if (!import_vector_member(bytes, index->long_names, at, index->thin, member, &end))
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, at);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared libraries

// The library's version definitions (SHT_GNU_verdef): a chain of Elf64_Verdef entries, sh_info of them, each with its
// name in its first Elf64_Verdaux, a string of the table the section links to. Entries don't overlap, so no more of
// them fit than the section has room for. Each entry checks that the next one lies within the section, so an error is
// reported at an entry, never past the section's end.
static ImportResult import_library_versions(ImportLibrary* library, const ImportElf* elf, uint32_t index, Arena* arena)
{
    ImportResult result = { 0 };
    const Elf64_Shdr header = import_section_header(elf, index);
    const String8 section = import_section_bytes(elf, &header);
    const uint64_t count = header.sh_info;
    const bool linked = header.sh_link != SHN_UNDEF && header.sh_link < elf->section_count && import_section_header(elf, header.sh_link).sh_type == SHT_STRTAB;
    const Elf64_Shdr strings = linked ? import_section_header(elf, header.sh_link) : header;
    const String8 names = linked ? import_section_bytes(elf, &strings) : (String8){ 0 };
    if (!linked || count > section.length / sizeof(Elf64_Verdef))
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, elf->section_offset + (uint64_t)index * sizeof(Elf64_Shdr));
        result.section = index;
    }
    else
    {
        library->versions = BUSTER_ARENA_PUSH_ARRAY(arena, ImportVersion, count);
        result = library->versions == 0 ? import_fail(IMPORT_ERROR_MEMORY, header.sh_offset) : result;
    }
    uint64_t offset = 0;
    for (uint64_t i = 0; result.error == IMPORT_ERROR_NONE && i < count; i += 1)
    {
        Elf64_Verdef definition;
        Elf64_Verdaux auxiliary;
        String8 name = { 0 };
        const bool fits = import_within(section.length, offset, sizeof(definition));
        if (fits)
        {
            memcpy(&definition, section.pointer + offset, sizeof(definition));
        }
        const bool auxiliary_fits = fits && definition.vd_version == VER_DEF_CURRENT && definition.vd_cnt >= 1 &&
                                    import_within(section.length, offset + definition.vd_aux, sizeof(auxiliary));
        if (auxiliary_fits)
        {
            memcpy(&auxiliary, section.pointer + offset + definition.vd_aux, sizeof(auxiliary));
        }
        const bool next_fits = i + 1 == count || (definition.vd_next != 0 && import_within(section.length, offset + definition.vd_next, sizeof(definition)));
        if (!auxiliary_fits || !import_string(names, auxiliary.vda_name, &name) || !next_fits)
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, header.sh_offset + offset);
            result.section = index;
        }
        else
        {
            library->versions[i] = (ImportVersion){
                .name = name,
                .hash = definition.vd_hash,
                .index = definition.vd_ndx,
                .base = (definition.vd_flags & VER_FLG_BASE) != 0,
            };
            offset += definition.vd_next;
        }
    }
    library->version_count = result.error == IMPORT_ERROR_NONE ? (uint32_t)count : 0;
    return result;
}

// The library's soname, DT_SONAME in its .dynamic, a string of the table the section links to: the last DT_SONAME
// before the first DT_NULL. The vector path reads the entries' tags eight at a time (import_vector_soname).
static ImportResult import_library_soname(ImportLibrary* library, const ImportElf* elf, uint32_t index)
{
    ImportResult result = { 0 };
    const Elf64_Shdr header = import_section_header(elf, index);
    const String8 section = import_section_bytes(elf, &header);
    uint64_t count = 0;
    const bool table = import_table(&header, sizeof(Elf64_Dyn), &count);
    const bool linked = header.sh_link != SHN_UNDEF && header.sh_link < elf->section_count && import_section_header(elf, header.sh_link).sh_type == SHT_STRTAB;
    if (!table || !linked)
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, header.sh_offset);
        result.section = index;
    }
    const Elf64_Shdr strings = linked ? import_section_header(elf, header.sh_link) : header;
    const String8 names = linked ? import_section_bytes(elf, &strings) : (String8){ 0 };
    bool ended = false;
    for (uint64_t i = 0; result.error == IMPORT_ERROR_NONE && !ended && i < count; i += 1)
    {
        Elf64_Dyn entry;
        memcpy(&entry, section.pointer + i * sizeof(entry), sizeof(entry));
        ended = entry.d_tag == DT_NULL;
        if (entry.d_tag == DT_SONAME && !import_string(names, entry.d_un.d_val, &library->soname))
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, header.sh_offset + i * sizeof(entry));
            result.section = index;
        }
    }
    return result;
}

// An exclusive prefix sum of `count` 32-bit values in place, sixteen at a time: each vector's inclusive sum in four
// shifts and adds (valignd by 1, 2, 4 and 8 lanes), plus the total of the vectors before it.
static void import_exclusive_scan(uint32_t* values, uint64_t count)
{
    const __m512i zero = _mm512_setzero_si512();
    __m512i carry = zero;
    for (uint64_t first = 0; first < count; first += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffff, (uint32_t)(count - first < 16 ? count - first : 16));
        const __m512i loaded = _mm512_maskz_loadu_epi32(lanes, values + first);
        __m512i sum = _mm512_add_epi32(loaded, _mm512_alignr_epi32(loaded, zero, 15));
        sum = _mm512_add_epi32(sum, _mm512_alignr_epi32(sum, zero, 14));
        sum = _mm512_add_epi32(sum, _mm512_alignr_epi32(sum, zero, 12));
        sum = _mm512_add_epi32(sum, _mm512_alignr_epi32(sum, zero, 8));
        // Exclusive: each lane gets the sum before it.
        _mm512_mask_storeu_epi32(values + first, lanes, _mm512_add_epi32(_mm512_sub_epi32(sum, loaded), carry));
        carry = _mm512_add_epi32(carry, _mm512_permutexvar_epi32(_mm512_set1_epi32(15), sum));
    }
}

// One bucket per item, rounded up to a power of two (16 at least), filled by a counting sort: the buckets in vectors,
// the counts, an exclusive prefix sum (import_exclusive_scan), then each item placed in item order, so that a name
// that two items share comes first at the first of them. Split across lanes, the counts would be per lane, summed by
// the prefix sum, as rule 4's exchange is (docs/object-writer-and-linker.md, "Lanes").
static bool import_index_build(ImportNameIndex* index, const uint64_t* hashes, uint32_t count, Arena* arena)
{
    uint32_t bits = 4;
    while (((uint64_t)1 << bits) < count)
    {
        bits += 1;
    }
    const uint64_t buckets = (uint64_t)1 << bits;
    index->bucket_bits = bits;
    index->start = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, buckets + 1);
    index->order = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
    index->hash = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count);
    Scratch scratch = scratch_begin(arena);
    uint32_t* bucket = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, count);
    const bool result = index->start != 0 && index->order != 0 && index->hash != 0 && bucket != 0;
    if (result)
    {
        uint32_t* start = index->start;
        memset(start, 0, (buckets + 1) * sizeof(uint32_t));
        for (uint32_t first = 0; first < count; first += BUSTER_IMPORT_BATCH)
        {
            const uint32_t n = count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH;
            const __m512i hash = _mm512_maskz_loadu_epi64((__mmask8)import_lane_mask(n), hashes + first);
            import_store32(bucket + first, _mm512_srli_epi64(hash, 64 - bits), n);
        }
        // Bucket b counted in start[b + 1], whose exclusive sum is then where b's items go. Each item placed moves it on
        // by one, so that it ends as b's end, which is b + 1's start; start[0] stays 0.
        for (uint32_t i = 0; i < count; i += 1)
        {
            start[bucket[i] + 1] += 1;
        }
        import_exclusive_scan(start + 1, buckets);
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t place = start[bucket[i] + 1];
            start[bucket[i] + 1] = place + 1;
            index->order[place] = i;
            index->hash[place] = hashes[i];
        }
    }
    scratch_end(scratch);
    return result;
}

// Whether `length` bytes at a and b are the same: one masked 64-byte load of each and a compare for up to 64 bytes, as
// most names are, rather than a call to memcmp, which takes a longer one.
static bool import_same_bytes(const uint8_t* a, const uint8_t* b, uint64_t length)
{
    bool result = false;
    if (length <= 64)
    {
        const __mmask64 loaded = _bzhi_u64(~0ull, (uint32_t)length);
        result = _mm512_mask_cmpneq_epi8_mask(loaded, _mm512_maskz_loadu_epi8(loaded, a), _mm512_maskz_loadu_epi8(loaded, b)) == 0;
    }
    else
    {
        result = memcmp(a, b, length) == 0;
    }
    return result;
}

static void import_index_bucket(const ImportNameIndex* index, uint64_t hash, uint32_t* begin, uint32_t* end)
{
    const uint64_t bucket = index->start != 0 ? hash >> (64 - index->bucket_bits) : 0;
    *begin = index->start != 0 ? index->start[bucket] : 0;
    *end = index->start != 0 ? index->start[bucket + 1] : 0;
}

static bool import_library_index(ImportLibrary* library, Arena* arena)
{
    return import_index_build(&library->index, library->exports.hash, library->exports.count, arena);
}

// The bucket's places in order, each hash compared, then the bytes of an equal one: a bucket holds about one export.
static uint32_t import_library_find(const ImportLibrary* library, const uint8_t* name, uint64_t length, uint64_t hash)
{
    const ImportExports* exports = &library->exports;
    const ImportNameIndex* index = &library->index;
    uint32_t result = 0;
    uint32_t begin = 0;
    uint32_t end = 0;
    import_index_bucket(index, hash, &begin, &end);
    for (uint32_t place = begin; result == 0 && place < end; place += 1)
    {
        const uint32_t export = index->order[place];
        const bool same = index->hash[place] == hash && exports->length[export] == length && import_same_bytes(exports->name[export], name, length);
        result = same ? export + 1 : 0;
    }
    return result;
}

// Whether `index` is VER_NDX_GLOBAL or one of the library's version definitions' indices, as an export's must be: GNU ld
// and lld refuse a library whose export, at its name's default version or not, names another.
static bool import_library_version_defined(const ImportLibrary* library, uint16_t index)
{
    bool defined = index == VER_NDX_GLOBAL;
    for (uint32_t v = 0; !defined && v < library->version_count; v += 1)
    {
        defined = library->versions[v].index == index;
    }
    return defined;
}

// Pushes the exports', references' and hidden exports' columns, with room for `count` of each.
static bool import_library_columns(ImportLibrary* library, uint64_t count, Arena* arena)
{
    ImportExports* exports = &library->exports;
    ImportReferences* references = &library->references;
    ImportHiddenExports* hidden = &library->hidden;
    exports->name = BUSTER_ARENA_PUSH_ARRAY(arena, const uint8_t*, count);
    exports->length = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
    exports->hash = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count);
    exports->size = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count);
    exports->flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count);
    exports->version = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    exports->value = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count);
    exports->section = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    references->name = BUSTER_ARENA_PUSH_ARRAY(arena, const uint8_t*, count);
    references->length = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
    references->hash = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count);
    references->weak = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count);
    references->version = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    hidden->name = BUSTER_ARENA_PUSH_ARRAY(arena, const uint8_t*, count);
    hidden->version = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    return exports->name != 0 && exports->length != 0 && exports->hash != 0 && exports->size != 0 && exports->flags != 0 && exports->version != 0 && exports->value != 0 &&
           exports->section != 0 && references->name != 0 && references->length != 0 && references->hash != 0 && references->weak != 0 && references->version != 0 &&
           hidden->name != 0 && hidden->version != 0;
}

// The sections a library's reader reads: .dynsym, .gnu.version, .gnu.version_d and .dynamic, each the first of its type
// (0 for none).
typedef struct ImportLibrarySections ImportLibrarySections;
struct ImportLibrarySections
{
    uint32_t dynsym;
    uint32_t versym;
    uint32_t verdef;
    uint32_t dynamic;
};

// .dynsym's checks, and the tables it links to: its entries, its names, and .gnu.version's indices of the same count.
typedef struct ImportDynamicSymbols ImportDynamicSymbols;
struct ImportDynamicSymbols
{
    String8 entries;
    uint64_t count;
    String8 names;
    String8 versions;
};

static ImportResult import_library_symbols(const ImportElf* elf, const ImportLibrarySections* sections, ImportDynamicSymbols* symbols)
{
    ImportResult result = { 0 };
    *symbols = (ImportDynamicSymbols){ 0 };
    const Elf64_Shdr header = sections->dynsym != 0 ? import_section_header(elf, sections->dynsym) : (Elf64_Shdr){ 0 };
    const bool fits = sections->dynsym != 0 && import_table(&header, sizeof(Elf64_Sym), &symbols->count) && symbols->count <= UINT32_MAX && header.sh_link != SHN_UNDEF &&
                      header.sh_link < elf->section_count && import_section_header(elf, header.sh_link).sh_type == SHT_STRTAB;
    if (!fits)
    {
        result = import_fail(sections->dynsym == 0 ? IMPORT_ERROR_FORMAT : IMPORT_ERROR_BOUNDS, sections->dynsym == 0 ? 0 : header.sh_offset);
        result.section = sections->dynsym;
        symbols->count = 0;
    }
    else
    {
        const Elf64_Shdr names = import_section_header(elf, header.sh_link);
        symbols->names = import_section_bytes(elf, &names);
        symbols->entries = import_section_bytes(elf, &header);
    }
    const Elf64_Shdr versions = sections->versym != 0 ? import_section_header(elf, sections->versym) : (Elf64_Shdr){ 0 };
    uint64_t version_count = 0;
    if (result.error == IMPORT_ERROR_NONE && sections->versym != 0 && (!import_table(&versions, sizeof(uint16_t), &version_count) || version_count != symbols->count))
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, versions.sh_offset);
        result.section = sections->versym;
    }
    else if (result.error == IMPORT_ERROR_NONE && sections->versym != 0)
    {
        symbols->versions = import_section_bytes(elf, &versions);
    }
    return result;
}

static ImportResult import_library_scalar(ImportLibrary* library, String8 bytes, Arena* arena)
{
    ImportElf elf;
    ImportLibrarySections sections = { 0 };
    ImportDynamicSymbols symbols = { 0 };
    *library = (ImportLibrary){ 0 };
    ImportResult result = import_elf(&elf, bytes, ET_DYN);
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < elf.section_count; index += 1)
    {
        const uint32_t type = import_section_header(&elf, index).sh_type;
        sections.dynsym = type == SHT_DYNSYM && sections.dynsym == 0 ? index : sections.dynsym;
        sections.versym = type == SHT_GNU_versym && sections.versym == 0 ? index : sections.versym;
        sections.verdef = type == SHT_GNU_verdef && sections.verdef == 0 ? index : sections.verdef;
        sections.dynamic = type == SHT_DYNAMIC && sections.dynamic == 0 ? index : sections.dynamic;
    }
    if (result.error == IMPORT_ERROR_NONE)
    {
        result = import_library_symbols(&elf, &sections, &symbols);
    }
    const uint64_t versions_offset = sections.versym != 0 ? import_section_header(&elf, sections.versym).sh_offset : 0;
    const uint64_t symbols_offset = sections.dynsym != 0 ? import_section_header(&elf, sections.dynsym).sh_offset : 0;
    if (result.error == IMPORT_ERROR_NONE && sections.verdef != 0)
    {
        result = import_library_versions(library, &elf, sections.verdef, arena);
    }
    if (result.error == IMPORT_ERROR_NONE && sections.dynamic != 0)
    {
        result = import_library_soname(library, &elf, sections.dynamic);
    }
    if (result.error == IMPORT_ERROR_NONE && !import_library_columns(library, symbols.count, arena))
    {
        result = import_fail(IMPORT_ERROR_MEMORY, 0);
    }
    ImportExports* exports = &library->exports;
    ImportReferences* references = &library->references;
    ImportHiddenExports* hidden = &library->hidden;
    for (uint64_t i = 1; result.error == IMPORT_ERROR_NONE && i < symbols.count; i += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, symbols.entries.pointer + i * sizeof(symbol), sizeof(symbol));
        String8 name = { 0 };
        uint16_t version = 1;
        if (symbols.versions.length != 0)
        {
            memcpy(&version, symbols.versions.pointer + i * sizeof(version), sizeof(version));
        }
        const uint32_t type = ELF64_ST_TYPE(symbol.st_info);
        const uint32_t binding = ELF64_ST_BIND(symbol.st_info);
        const uint32_t visibility = ELF64_ST_VISIBILITY(symbol.st_other);
        const bool global = binding == STB_GLOBAL || binding == STB_WEAK || binding == STB_GNU_UNIQUE;
        const uint16_t version_index = (uint16_t)(version & 0x7fff);
        // A name@VERSION entry (the hidden bit) isn't its name's default; VER_NDX_LOCAL entries aren't exported.
        const bool default_version = (version & 0x8000) == 0 && version_index != VER_NDX_LOCAL;
        if (!import_string(symbols.names, symbol.st_name, &name))
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, symbols_offset + i * sizeof(symbol));
            result.symbol = (uint32_t)i;
        }
        else if (symbol.st_shndx == SHN_UNDEF && global && name.length != 0)
        {
            const uint32_t at = references->count;
            references->name[at] = (const uint8_t*)name.pointer;
            references->length[at] = (uint32_t)name.length;
            references->hash[at] = link_hash((const uint8_t*)name.pointer, name.length);
            references->weak[at] = binding == STB_WEAK ? 1 : 0;
            references->version[at] = version_index;
            references->count += 1;
        }
        else if (symbol.st_shndx != SHN_UNDEF && global && (version & 0x8000) != 0 && version_index > VER_NDX_GLOBAL && name.length != 0 &&
                 (visibility == STV_DEFAULT || visibility == STV_PROTECTED))
        {
            if (!import_library_version_defined(library, version_index))
            {
                result = import_fail(IMPORT_ERROR_BOUNDS, versions_offset + i * sizeof(version));
                result.symbol = (uint32_t)i;
            }
            hidden->name[hidden->count] = (const uint8_t*)name.pointer;
            hidden->version[hidden->count] = version_index;
            hidden->count += 1;
        }
        else if (symbol.st_shndx != SHN_UNDEF && global && default_version && name.length != 0 && (visibility == STV_DEFAULT || visibility == STV_PROTECTED))
        {
            const uint32_t exported_type = type == STT_GNU_IFUNC ? STT_FUNC : type == STT_COMMON ? STT_OBJECT : type;
            const uint32_t exported_binding = binding == STB_WEAK ? STB_WEAK : STB_GLOBAL;
            if (!import_library_version_defined(library, version_index))
            {
                result = import_fail(IMPORT_ERROR_BOUNDS, versions_offset + i * sizeof(version));
                result.symbol = (uint32_t)i;
            }
            const uint32_t at = exports->count;
            exports->name[at] = (const uint8_t*)name.pointer;
            exports->length[at] = (uint32_t)name.length;
            exports->hash[at] = link_hash((const uint8_t*)name.pointer, name.length);
            exports->size[at] = symbol.st_size;
            exports->flags[at] = (uint8_t)((exported_type & BUSTER_LINK_SYMBOL_TYPE) | (exported_binding << 4));
            exports->version[at] = version_index;
            exports->value[at] = symbol.st_value;
            exports->section[at] = symbol.st_shndx;
            exports->count += 1;
        }
    }
    if (result.error == IMPORT_ERROR_NONE && !import_library_index(library, arena))
    {
        result = import_fail(IMPORT_ERROR_MEMORY, 0);
    }
    return result;
}

// The library's section table, eight headers at a time as the object path's (link_transpose_words): each section's
// bounds, and the first of each type import_library reads.
static bool import_vector_library_sections(const ImportElf* elf, ImportLibrarySections* sections)
{
    const char* table = elf->bytes.pointer + elf->section_offset;
    const uint32_t count = elf->section_count;
    const __m512i file_bytes = _mm512_set1_epi64((long long)elf->bytes.length);
    const uint32_t types[4] = {
        SHT_DYNSYM,
        SHT_GNU_versym,
        SHT_GNU_verdef,
        SHT_DYNAMIC,
    };
    uint32_t found[4] = { 0 };
    uint32_t failed = 0;
    for (uint32_t first = 0; first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH;
        const uint32_t active = import_lane_mask(n) & (first == 0 ? 0xfe : 0xff);
        __m512i rows[BUSTER_IMPORT_BATCH];
        __m512i words[BUSTER_IMPORT_BATCH];
        for (uint32_t r = 0; r < BUSTER_IMPORT_BATCH; r += 1)
        {
            rows[r] = _mm512_maskz_loadu_epi64(r < n ? 0xff : 0, table + (uint64_t)(first + r) * sizeof(Elf64_Shdr));
        }
        link_transpose_words(rows, words);
        const __m512i type = _mm512_srli_epi64(words[0], 32);
        const __m512i offset = words[3];
        const __m512i size = words[4];
        const uint32_t in_file = _mm512_cmple_epu64_mask(offset, file_bytes) & _mm512_cmple_epu64_mask(size, _mm512_sub_epi64(file_bytes, offset));
        failed |= active & ~in_file & ~import_lanes_equal(type, SHT_NOBITS);
        for (uint32_t t = 0; t < 4; t += 1)
        {
            const uint32_t lanes = active & import_lanes_equal(type, types[t]);
            found[t] = found[t] == 0 && lanes != 0 ? first + _tzcnt_u32(lanes) : found[t];
        }
    }
    sections->dynsym = found[0];
    sections->versym = found[1];
    sections->verdef = found[2];
    sections->dynamic = found[3];
    return failed == 0;
}

// The soname, as import_library_soname reads it: eight entries' tags at a time (two vectors, deinterleaved), the first
// DT_NULL by its lane, and each DT_SONAME before it read.
static bool import_vector_soname(ImportLibrary* library, const ImportElf* elf, uint32_t index)
{
    const Elf64_Shdr header = import_section_header(elf, index);
    const String8 section = import_section_bytes(elf, &header);
    uint64_t count = 0;
    const bool table = import_table(&header, sizeof(Elf64_Dyn), &count);
    const bool linked = header.sh_link != SHN_UNDEF && header.sh_link < elf->section_count && import_section_header(elf, header.sh_link).sh_type == SHT_STRTAB;
    const Elf64_Shdr strings = linked ? import_section_header(elf, header.sh_link) : header;
    const String8 names = linked ? import_section_bytes(elf, &strings) : (String8){ 0 };
    bool result = table && linked;
    bool ended = false;
    for (uint64_t first = 0; result && !ended && first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = (uint32_t)(count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH);
        const char* entries = section.pointer + first * sizeof(Elf64_Dyn);
        // Elf64_Dyn is two words, d_tag then d_val: the even words of the 16 are the tags, the odd ones the values.
        const __m512i low = _mm512_maskz_loadu_epi64((__mmask8)import_lane_mask(2 * n), entries);
        const __m512i high = _mm512_maskz_loadu_epi64((__mmask8)import_lane_mask(2 * n > 8 ? 2 * n - 8 : 0), entries + 64);
        const __m512i tags = _mm512_permutex2var_epi64(low, _mm512_set_epi64(14, 12, 10, 8, 6, 4, 2, 0), high);
        const __m512i values = _mm512_permutex2var_epi64(low, _mm512_set_epi64(15, 13, 11, 9, 7, 5, 3, 1), high);
        const uint32_t nulls = import_lanes_equal(tags, DT_NULL) & import_lane_mask(n);
        // The lanes up to and including the first DT_NULL.
        const uint32_t before = nulls != 0 ? _blsmsk_u32(nulls) : import_lane_mask(n);
        uint32_t sonames = import_lanes_equal(tags, DT_SONAME) & before;
        uint64_t offsets[BUSTER_IMPORT_BATCH];
        _mm512_storeu_si512(offsets, values);
        while (result && sonames != 0)
        {
            result = import_string(names, offsets[_tzcnt_u32(sonames)], &library->soname);
            sonames &= sonames - 1;
        }
        ended = nulls != 0;
    }
    return result;
}

static bool import_library_vector(ImportLibrary* library, String8 bytes, Arena* arena)
{
    ImportElf elf;
    ImportLibrarySections sections = { 0 };
    ImportDynamicSymbols symbols = { 0 };
    uint32_t names_index = 0;
    *library = (ImportLibrary){ 0 };
    bool result = import_elf_header(&elf, bytes, ET_DYN, &names_index).error == IMPORT_ERROR_NONE && import_elf_names(&elf, names_index);
    result = result && import_vector_library_sections(&elf, &sections);
    result = result && import_library_symbols(&elf, &sections, &symbols).error == IMPORT_ERROR_NONE;
    // The version definitions are a chain of entries that only vd_next places: the exact path's reader. A defined index
    // past 511 is left to the exact path, which the set below can't hold.
    result = result && (sections.verdef == 0 || import_library_versions(library, &elf, sections.verdef, arena).error == IMPORT_ERROR_NONE);
    result = result && (sections.dynamic == 0 || import_vector_soname(library, &elf, sections.dynamic));
    result = result && import_library_columns(library, symbols.count, arena);
    // An offset within the names' table starts a name when the table ends with a NUL, and st_name 0, which the batches
    // below take as the empty name, is the empty name when the table starts with one. In a table that doesn't, the exact
    // path reads the name it starts with.
    const String8 names = symbols.names;
    result = result && (names.length == 0 || (names.pointer[0] == 0 && names.pointer[names.length - 1] == 0));
    uint64_t defined[BUSTER_IMPORT_BATCH] = {
        (uint64_t)1 << VER_NDX_GLOBAL,
    };
    for (uint32_t v = 0; result && v < library->version_count; v += 1)
    {
        const uint16_t version_index = library->versions[v].index;
        result = version_index < 64 * BUSTER_IMPORT_BATCH;
        defined[(version_index / 64) % BUSTER_IMPORT_BATCH] |= (uint64_t)1 << (version_index % 64);
    }
    const __m512i defined_set = _mm512_loadu_si512(defined);
    const __m512i low32 = _mm512_set1_epi64(BUSTER_IMPORT_LOW32);
    const __m512i names_base = _mm512_set1_epi64((long long)(uintptr_t)names.pointer);
    ImportExports* exports = &library->exports;
    ImportReferences* references = &library->references;
    ImportHiddenExports* hidden = &library->hidden;
    uint32_t failed = 0;
    for (uint64_t first = 0; result && failed == 0 && first < symbols.count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = (uint32_t)(symbols.count - first < BUSTER_IMPORT_BATCH ? symbols.count - first : BUSTER_IMPORT_BATCH);
        const uint32_t active = import_lane_mask(n) & (first == 0 ? 0xfe : 0xff);
        __m512i words[3];
        import_load_records3(symbols.entries.pointer + first * sizeof(Elf64_Sym), n, words);
        const __m512i name = _mm512_and_si512(words[0], low32);
        const __m512i info = _mm512_and_si512(_mm512_srli_epi64(words[0], 32), _mm512_set1_epi64(0xff));
        const __m512i type = _mm512_and_si512(info, _mm512_set1_epi64(0x0f));
        const __m512i binding = _mm512_srli_epi64(info, 4);
        const __m512i visibility = _mm512_and_si512(_mm512_srli_epi64(words[0], 40), _mm512_set1_epi64(0x03));
        const __m512i shndx = _mm512_srli_epi64(words[0], 48);
        __m512i version = _mm512_set1_epi64(VER_NDX_GLOBAL);
        if (symbols.versions.length != 0)
        {
            version = _mm512_cvtepu16_epi64(_mm_maskz_loadu_epi16((__mmask8)import_lane_mask(n), symbols.versions.pointer + first * sizeof(uint16_t)));
        }
        const __m512i version_index = _mm512_and_si512(version, _mm512_set1_epi64(0x7fff));
        const uint32_t named = _mm512_test_epi64_mask(name, name);
        // Every entry's name, st_name 0 among them, is a string of the table, as import_library_scalar checks.
        failed |= active & _mm512_cmpge_epu64_mask(name, _mm512_set1_epi64((long long)names.length));
        const uint32_t global = import_lanes_equal(binding, STB_GLOBAL) | import_lanes_equal(binding, STB_WEAK) | import_lanes_equal(binding, STB_GNU_UNIQUE);
        const uint32_t undefined = _mm512_testn_epi64_mask(shndx, shndx);
        const uint32_t default_version = _mm512_testn_epi64_mask(version, _mm512_set1_epi64(0x8000)) & _mm512_test_epi64_mask(version_index, version_index);
        const uint32_t visible = import_lanes_equal(visibility, STV_DEFAULT) | import_lanes_equal(visibility, STV_PROTECTED);
        // A name of no bytes past st_name 0 is the exact path's: the lengths below send the library there.
        const uint32_t referenced = active & named & undefined & global;
        const uint32_t exported = active & named & ~undefined & global & default_version & visible;
        const uint32_t hidden_version = _mm512_test_epi64_mask(version, _mm512_set1_epi64(0x8000)) & _mm512_cmpgt_epu64_mask(version_index, _mm512_set1_epi64(VER_NDX_GLOBAL));
        const uint32_t hidden_export = active & named & ~undefined & global & hidden_version & visible;
        // Each export's version, at its name's default or not, in the set of defined ones: a 512-bit set in a register,
        // its word by vpermq and its bit by a variable shift.
        const __m512i set_word = _mm512_permutexvar_epi64(_mm512_srli_epi64(version_index, 6), defined_set);
        const uint32_t in_set = _mm512_test_epi64_mask(_mm512_srlv_epi64(set_word, _mm512_and_si512(version_index, _mm512_set1_epi64(63))), _mm512_set1_epi64(1)) &
                                _mm512_cmplt_epu64_mask(version_index, _mm512_set1_epi64(64 * BUSTER_IMPORT_BATCH));
        failed |= (exported | hidden_export) & ~in_set;
        const __m512i pointers = _mm512_add_epi64(names_base, name);
        const __m512i exported_type = _mm512_mask_mov_epi64(_mm512_mask_mov_epi64(type, (__mmask8)import_lanes_equal(type, STT_GNU_IFUNC), _mm512_set1_epi64(STT_FUNC)),
                                                            (__mmask8)import_lanes_equal(type, STT_COMMON), _mm512_set1_epi64(STT_OBJECT));
        const uint32_t weak = import_lanes_equal(binding, STB_WEAK);
        const __m512i exported_binding = _mm512_mask_mov_epi64(_mm512_set1_epi64(STB_GLOBAL), (__mmask8)weak, _mm512_set1_epi64(STB_WEAK));
        const __m512i flags = _mm512_or_si512(exported_type, _mm512_slli_epi64(exported_binding, 4));
        const uint32_t export_count = (uint32_t)__builtin_popcount(exported);
        const uint32_t reference_count = (uint32_t)__builtin_popcount(referenced);
        const uint32_t hidden_count = (uint32_t)__builtin_popcount(hidden_export);
        const uint32_t e = exports->count;
        const uint32_t r = references->count;
        const uint32_t h = hidden->count;
        import_store64(exports->name + e, _mm512_maskz_compress_epi64((__mmask8)exported, pointers), export_count);
        import_store64(exports->size + e, _mm512_maskz_compress_epi64((__mmask8)exported, words[2]), export_count);
        import_store8(exports->flags + e, _mm512_maskz_compress_epi64((__mmask8)exported, flags), export_count);
        import_store16(exports->version + e, _mm512_maskz_compress_epi64((__mmask8)exported, version_index), export_count);
        import_store64(exports->value + e, _mm512_maskz_compress_epi64((__mmask8)exported, words[1]), export_count);
        import_store16(exports->section + e, _mm512_maskz_compress_epi64((__mmask8)exported, shndx), export_count);
        import_store64(references->name + r, _mm512_maskz_compress_epi64((__mmask8)referenced, pointers), reference_count);
        import_store8(references->weak + r, _mm512_maskz_compress_epi64((__mmask8)referenced, _mm512_maskz_mov_epi64((__mmask8)weak, _mm512_set1_epi64(1))), reference_count);
        import_store16(references->version + r, _mm512_maskz_compress_epi64((__mmask8)referenced, version_index), reference_count);
        import_store64(hidden->name + h, _mm512_maskz_compress_epi64((__mmask8)hidden_export, pointers), hidden_count);
        import_store16(hidden->version + h, _mm512_maskz_compress_epi64((__mmask8)hidden_export, version_index), hidden_count);
        exports->count += export_count;
        references->count += reference_count;
        hidden->count += hidden_count;
    }
    // A hidden export's name has a byte before its NUL, as the exact path takes them: an empty one sends the library
    // there. Each offset is in the table, which ends with a NUL.
    for (uint32_t i = 0; result && failed == 0 && i < hidden->count; i += 1)
    {
        failed |= hidden->name[i][0] == 0 ? 1 : 0;
    }
    // The names' lengths, a masked 64-byte load each, then their hashes eight at a time.
    for (uint32_t i = 0; result && failed == 0 && i < exports->count; i += 1)
    {
        const uint64_t length = import_name_length(names, (uint64_t)((const char*)exports->name[i] - names.pointer));
        failed |= length == 0 || length > UINT32_MAX ? 1 : 0;
        exports->length[i] = (uint32_t)length;
    }
    for (uint32_t i = 0; result && failed == 0 && i < references->count; i += 1)
    {
        const uint64_t length = import_name_length(names, (uint64_t)((const char*)references->name[i] - names.pointer));
        failed |= length == 0 || length > UINT32_MAX ? 1 : 0;
        references->length[i] = (uint32_t)length;
    }
    result = result && failed == 0;
    if (result)
    {
        link_hash_names(exports->name, exports->length, exports->count, exports->hash);
        link_hash_names(references->name, references->length, references->count, references->hash);
    }
    return result && import_library_index(library, arena);
}

static ImportResult import_library(ImportLibrary* library, String8 bytes, Arena* arena)
{
#if !BUSTER_HOST_VECTOR
    return import_library_scalar(library, bytes, arena);
#else
    const uint64_t length = arena->length;
    ImportResult result = { 0 };
    if (!import_library_vector(library, bytes, arena))
    {
        arena_pop_to(arena, length);
        result = import_library_scalar(library, bytes, arena);
    }
    return result;
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// Linker scripts

typedef enum ImportToken
{
    IMPORT_TOKEN_END,
    IMPORT_TOKEN_WORD,
    IMPORT_TOKEN_OPEN,
    IMPORT_TOKEN_CLOSE,
    IMPORT_TOKEN_COMMA,
    IMPORT_TOKEN_ERROR,
} ImportToken;

typedef struct ImportLexer ImportLexer;
struct ImportLexer
{
    String8 bytes;
    uint64_t position;
    // The last token's text and offset.
    String8 word;
    uint64_t start;
};

// The next token, after whitespace and comments; an unterminated comment is an error, at the comment's start.
static ImportToken import_next_token(ImportLexer* lexer)
{
    const String8 bytes = lexer->bytes;
    ImportToken result = IMPORT_TOKEN_END;
    uint64_t comment_start = 0;
    bool skipping = true;
    while (skipping)
    {
        const uint64_t p = lexer->position;
        const char c = p < bytes.length ? bytes.pointer[p] : 0;
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        const bool comment = c == '/' && p + 1 < bytes.length && bytes.pointer[p + 1] == '*';
        if (space)
        {
            lexer->position += 1;
        }
        else if (comment)
        {
            uint64_t end = p + 2;
            while (end + 1 < bytes.length && !(bytes.pointer[end] == '*' && bytes.pointer[end + 1] == '/'))
            {
                end += 1;
            }
            const bool closed = end + 1 < bytes.length;
            comment_start = p;
            lexer->position = closed ? end + 2 : bytes.length;
            result = closed ? result : IMPORT_TOKEN_ERROR;
            skipping = closed;
        }
        else
        {
            skipping = false;
        }
    }
    lexer->start = result == IMPORT_TOKEN_ERROR ? comment_start : lexer->position;
    if (result != IMPORT_TOKEN_ERROR && lexer->position < bytes.length)
    {
        const char c = bytes.pointer[lexer->position];
        if (c == '(' || c == ')' || c == ',')
        {
            result = c == '(' ? IMPORT_TOKEN_OPEN : c == ')' ? IMPORT_TOKEN_CLOSE : IMPORT_TOKEN_COMMA;
            lexer->position += 1;
        }
        else
        {
            uint64_t end = lexer->position;
            bool word = true;
            while (word && end < bytes.length)
            {
                const char d = bytes.pointer[end];
                word = !(d == ' ' || d == '\t' || d == '\n' || d == '\r' || d == '\f' || d == '\v' || d == '(' || d == ')' || d == ',');
                end += word ? 1 : 0;
            }
            lexer->word.pointer = bytes.pointer + lexer->position;
            lexer->word.length = end - lexer->position;
            lexer->position = end;
            result = IMPORT_TOKEN_WORD;
        }
    }
    return result;
}

static ImportResult import_script(ImportScript* script, String8 bytes, Arena* arena)
{
    ImportResult result = { 0 };
    *script = (ImportScript){ 0 };
    // Two passes: count the inputs, then fill them.
    for (uint32_t pass = 0; result.error == IMPORT_ERROR_NONE && pass < 2; pass += 1)
    {
        if (pass == 1)
        {
            script->inputs = BUSTER_ARENA_PUSH_ARRAY(arena, ImportScriptInput, script->input_count);
            result = script->inputs == 0 && script->input_count != 0 ? import_fail(IMPORT_ERROR_MEMORY, 0) : result;
            script->input_count = 0;
        }
        ImportLexer lexer = {
            .bytes = bytes,
        };
        ImportToken token = import_next_token(&lexer);
        while (result.error == IMPORT_ERROR_NONE && token != IMPORT_TOKEN_END)
        {
            const bool command = token == IMPORT_TOKEN_WORD;
            const String8 name = lexer.word;
            const uint64_t command_start = lexer.start;
            const bool format = command && import_equal(name, "OUTPUT_FORMAT");
            const bool inputs = command && (import_equal(name, "GROUP") || import_equal(name, "INPUT"));
            if ((!format && !inputs) || import_next_token(&lexer) != IMPORT_TOKEN_OPEN)
            {
                result = import_fail(IMPORT_ERROR_SCRIPT, command_start);
            }
            // The command's arguments up to its ')': for OUTPUT_FORMAT, names; for GROUP and INPUT, inputs, which
            // AS_NEEDED ( ... ) marks. Commas separate arguments or not, as in GNU ld.
            bool as_needed = false;
            bool closed = false;
            while (result.error == IMPORT_ERROR_NONE && !closed)
            {
                token = import_next_token(&lexer);
                const bool needed_group = inputs && token == IMPORT_TOKEN_WORD && import_equal(lexer.word, "AS_NEEDED");
                if (token == IMPORT_TOKEN_CLOSE)
                {
                    closed = !as_needed;
                    as_needed = false;
                }
                else if (needed_group && as_needed)
                {
                    result = import_fail(IMPORT_ERROR_SCRIPT, lexer.start);
                }
                else if (needed_group)
                {
                    as_needed = import_next_token(&lexer) == IMPORT_TOKEN_OPEN;
                    result = as_needed ? result : import_fail(IMPORT_ERROR_SCRIPT, lexer.start);
                }
                else if (token == IMPORT_TOKEN_WORD && inputs && script->input_count == UINT32_MAX)
                {
                    result = import_fail(IMPORT_ERROR_SCRIPT, lexer.start);
                }
                else if (token == IMPORT_TOKEN_WORD && inputs)
                {
                    const bool library = import_starts_with(lexer.word, "-l") && lexer.word.length > 2;
                    if (pass == 1)
                    {
                        script->inputs[script->input_count] = (ImportScriptInput){
                            .name = {
                                .pointer = lexer.word.pointer + (library ? 2 : 0),
                                .length = lexer.word.length - (library ? 2 : 0),
                            },
                            .library = library,
                            .as_needed = as_needed,
                        };
                    }
                    script->input_count += 1;
                }
                else if (token != IMPORT_TOKEN_COMMA && token != IMPORT_TOKEN_WORD)
                {
                    result = import_fail(IMPORT_ERROR_SCRIPT, lexer.start);
                }
            }
            token = result.error == IMPORT_ERROR_NONE ? import_next_token(&lexer) : token;
            result = token == IMPORT_TOKEN_ERROR ? import_fail(IMPORT_ERROR_SCRIPT, lexer.start) : result;
        }
    }
    return result;
}
