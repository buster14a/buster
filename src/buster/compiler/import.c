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
//     and GNU ld merges, and which the representation has no class for. SHT_GROUP, SHT_REL and SHT_PREINIT_ARRAY
//     sections, and a group's members, are unsupported.
//   - Symbols: each one defined in an atom becomes a symbol of the given source there, with its name, value, size,
//     type, binding and visibility; each undefined one, a symbol of source NONE without an atom; and each section
//     symbol, an unnamed local at its atom's start, which relocations against the section refer to. FILE symbols, and
//     locals defined in dropped sections, become none. Absolute, common, IFUNC and GNU_UNIQUE symbols are unsupported.
//   - Relocations: each entry of an atom's .rela section becomes one of its fixups, of the kind its type names (the
//     inverse of the object writer's object_relocation_types), with its target and addend, in offset order: sorted
//     if the file's aren't. Other types are unsupported.
// Nothing is resolved: a reference to a name that another file defines stays a reference to this object's undefined
// symbol, which the caller binds.
//
// Archives (import_archive): GNU's format, with its symbol index (32- or 64-bit) and its table of long names. The
// members are slices of the archive's bytes, and the index maps each name to the member that defines it. Thin archives
// are unsupported.
//
// Shared libraries (import_library): the soname, the version definitions, and from .dynsym the names it defines, each
// with its default version (name@@VERSION, or unversioned), type and size, in a hash table by link_hash, and the names
// it refers to without defining them, which decide what a program exports (docs/object-writer-and-linker.md,
// "Exports"). An IFUNC is exported as a function, as linkers show it in a program's .dynsym.
//
// Linker scripts (import_script): OUTPUT_FORMAT, GROUP and INPUT, whose inputs are paths or -l names, each of which
// AS_NEEDED can mark, and comments. Any other command is an error.
//
// main.c includes this file after link.c, whose representation it fills, lane.c, whose arenas it uses, and
// object_writer.c, whose relocation types it reads the other way. Layout, in this order:
//   - API: ImportError, ImportResult, the readers' outputs and the entry points' prototypes.
//   - Bytes: bounded reads, strings and the ELF header and section table (ImportElf).
//   - Objects: import_section_class, import_fixup_kind, import_object_capacity and import_object.
//   - Archives: import_archive.
//   - Shared libraries: import_library and import_library_find.
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
    // common section, a section group, a thin archive.
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

// What import_object made of an object's sections and symbols, by their indices in the file: the atom each section
// became and the symbol each symbol table entry became, 0 for none.
typedef struct ImportObject ImportObject;
struct ImportObject
{
    AtomId* section_atoms;
    uint32_t section_count;
    SymbolId* symbols;
    uint32_t symbol_count;
};

typedef struct ImportMember ImportMember;
struct ImportMember
{
    String8 name;
    String8 bytes;
    // The offset of the member's header in the archive, which the symbol index names it by.
    uint64_t offset;
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
};

// A name a shared library defines, at its default version.
typedef struct ImportExport ImportExport;
struct ImportExport
{
    String8 name;
    uint64_t hash;
    uint64_t size;
    // The type and binding, as BUSTER_LINK_SYMBOL_* (an IFUNC as a function, a GNU_UNIQUE symbol as a global).
    uint8_t flags;
    // Its version's index in .gnu.version (an ImportVersion's), or VER_NDX_GLOBAL for an unversioned name.
    uint16_t version;
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

// A name a shared library refers to without defining it, and whether the reference is weak.
typedef struct ImportReference ImportReference;
struct ImportReference
{
    String8 name;
    uint64_t hash;
    bool weak;
};

typedef struct ImportLibrary ImportLibrary;
struct ImportLibrary
{
    String8 soname;
    ImportExport* exports;
    uint32_t export_count;
    ImportVersion* versions;
    uint32_t version_count;
    ImportReference* references;
    uint32_t reference_count;
    // The exports by hash: open addressing with linear probing, each slot an export's index plus 1, or 0.
    uint32_t* table;
    uint64_t table_mask;
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
// symbols are pushed from `arena`. On failure, what was added before it stays in the link.
static __attribute__((unused)) ImportResult import_object(Link* link, String8 bytes, uint32_t unit, uint8_t source, Arena* arena, ImportObject* object);
// Reads an archive's members and symbol index; the arrays are pushed from `arena`, the names and bytes are slices of
// the archive's.
static __attribute__((unused)) ImportResult import_archive(ImportArchive* archive, String8 bytes, Arena* arena);
// Reads a shared library's soname, versions, exports and references; the arrays are pushed from `arena`, the names
// are slices of the library's bytes.
static __attribute__((unused)) ImportResult import_library(ImportLibrary* library, String8 bytes, Arena* arena);
// The index of the library's export of that name plus 1, or 0 if it doesn't export it.
static __attribute__((unused)) uint32_t import_library_find(const ImportLibrary* library, const uint8_t* name, uint64_t length, uint64_t hash);
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

// Reads and checks the header and the section table of an ELF file of `type` (ET_REL or ET_DYN): 64-bit,
// little-endian, x86-64, the current version, with Elf64_Shdr entries. Past SHN_LORESERVE sections, the count and the
// names' index come from section 0, as the gABI has it.
static ImportResult import_elf(ImportElf* elf, String8 bytes, uint16_t type)
{
    ImportResult result = { 0 };
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
        const uint32_t names = header->e_shstrndx != SHN_XINDEX ? header->e_shstrndx : first.sh_link;
        if (count == 0 || count > UINT32_MAX || !import_within(bytes.length, header->e_shoff, count * sizeof(Elf64_Shdr)))
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, offsetof(Elf64_Ehdr, e_shnum));
        }
        else
        {
            elf->section_count = (uint32_t)count;
        }
        for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < elf->section_count; index += 1)
        {
            const Elf64_Shdr section = import_section_header(elf, index);
            if (section.sh_type != SHT_NOBITS && !import_within(bytes.length, section.sh_offset, section.sh_size))
            {
                result = import_fail(IMPORT_ERROR_BOUNDS, header->e_shoff + (uint64_t)index * sizeof(Elf64_Shdr));
                result.section = index;
            }
        }
        if (result.error == IMPORT_ERROR_NONE && (names == SHN_UNDEF || names >= elf->section_count || import_section_header(elf, names).sh_type != SHT_STRTAB))
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, offsetof(Elf64_Ehdr, e_shstrndx));
        }
        else if (result.error == IMPORT_ERROR_NONE)
        {
            const Elf64_Shdr table = import_section_header(elf, names);
            elf->section_names = import_section_bytes(elf, &table);
        }
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

// The class lld would give an input section's output section, from its type, flags and name, or
// BUSTER_LINK_CLASS_NONE for one that is dropped; *supported is false for one that can't be imported.
static uint8_t import_section_class(const Elf64_Shdr* header, String8 name, bool* supported)
{
    const uint64_t flags = header->sh_flags;
    const uint32_t type = header->sh_type;
    const bool allocated = (flags & SHF_ALLOC) != 0;
    const bool contents = type == SHT_PROGBITS || type == SHT_NOBITS || type == SHT_INIT_ARRAY || type == SHT_FINI_ARRAY || type == SHT_X86_64_UNWIND;
    uint8_t result = BUSTER_LINK_CLASS_NONE;
    *supported = true;
    if (type == SHT_GROUP || type == SHT_REL || type == SHT_PREINIT_ARRAY || (flags & SHF_GROUP) != 0)
    {
        *supported = false;
    }
    else if (!allocated || type == SHT_NOTE || type == BUSTER_IMPORT_SHT_GNU_SFRAME || import_equal(name, ".sframe"))
    {
        result = BUSTER_LINK_CLASS_NONE;
    }
    else if (!contents)
    {
        *supported = false;
    }
    else if (type == SHT_X86_64_UNWIND || import_equal(name, ".eh_frame"))
    {
        result = BUSTER_LINK_CLASS_EH_FRAME;
    }
    else if (type == SHT_INIT_ARRAY)
    {
        result = BUSTER_LINK_CLASS_INIT_ARRAY;
    }
    else if (type == SHT_FINI_ARRAY)
    {
        result = BUSTER_LINK_CLASS_FINI_ARRAY;
    }
    else if ((flags & SHF_TLS) != 0)
    {
        result = type == SHT_NOBITS ? BUSTER_LINK_CLASS_TBSS : BUSTER_LINK_CLASS_TDATA;
    }
    else if ((flags & SHF_EXECINSTR) != 0)
    {
        result = import_equal(name, ".init") ? BUSTER_LINK_CLASS_INIT : import_equal(name, ".fini") ? BUSTER_LINK_CLASS_FINI : BUSTER_LINK_CLASS_TEXT;
    }
    else if ((flags & SHF_WRITE) != 0)
    {
        result = type == SHT_NOBITS ? BUSTER_LINK_CLASS_BSS : import_starts_with(name, ".data.rel.ro") ? BUSTER_LINK_CLASS_DATA_RELRO : BUSTER_LINK_CLASS_DATA;
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
        // At most an atom per section, and a fixup per relocation, each of which may need a wide addend.
        capacity->atoms += 1;
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
        const bool named = import_section_name(elf, &header, &name);
        const uint8_t class = named ? import_section_class(&header, name, &supported) : BUSTER_LINK_CLASS_NONE;
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
            if (atom == 0)
            {
                result = import_fail(zero_fill_matches ? IMPORT_ERROR_LINK : IMPORT_ERROR_UNSUPPORTED, header_offset);
                result.section = index;
                result.type = header.sh_type;
            }
            object->section_atoms[index] = atom;
        }
    }
    return result;
}

// The symbol flags of an ELF symbol's type, binding and visibility, or false for a type or binding the representation
// doesn't have (IFUNC, COMMON, GNU_UNIQUE).
static bool import_symbol_flags(const Elf64_Sym* symbol, uint8_t* flags)
{
    const uint32_t type = ELF64_ST_TYPE(symbol->st_info);
    const uint32_t binding = ELF64_ST_BIND(symbol->st_info);
    const uint32_t visibility = ELF64_ST_VISIBILITY(symbol->st_other);
    const bool type_known = type == STT_NOTYPE || type == STT_OBJECT || type == STT_FUNC || type == STT_TLS;
    const bool binding_known = binding == STB_LOCAL || binding == STB_GLOBAL || binding == STB_WEAK;
    *flags = (uint8_t)((type & BUSTER_LINK_SYMBOL_TYPE) | (binding << 4) | (visibility << BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT));
    return type_known && binding_known;
}

// Imports the symbols: one for each that names a place in an atom or a reference, none for FILE symbols and locals of
// dropped sections.
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
        else if (!known || reserved || (section != SHN_UNDEF && atom == 0) || (section == SHN_UNDEF && binding == STB_LOCAL))
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

// Imports one .rela section's entries as its atom's fixups, in offset order.
static ImportResult import_relocations(Link* link, const ImportElf* elf, const ImportSymbols* symbols, uint32_t index, const ImportObject* object, Arena* arena)
{
    ImportResult result = { 0 };
    const Elf64_Shdr header = import_section_header(elf, index);
    const uint64_t header_offset = elf->section_offset + (uint64_t)index * sizeof(Elf64_Shdr);
    uint64_t count = 0;
    const bool table = import_table(&header, sizeof(Elf64_Rela), &count) && count <= UINT32_MAX;
    const AtomId atom = header.sh_info != SHN_UNDEF && header.sh_info < elf->section_count ? object->section_atoms[header.sh_info] : 0;
    if (!table || symbols->section == 0 || header.sh_link != symbols->section || header.sh_info == SHN_UNDEF || header.sh_info >= elf->section_count)
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, header_offset);
        result.section = index;
    }
    else if (atom != 0 && count != 0)
    {
        const String8 entries = import_section_bytes(elf, &header);
        uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
        uint32_t* spare = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count);
        if (order == 0 || spare == 0)
        {
            result = import_fail(IMPORT_ERROR_MEMORY, header_offset);
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
                const uint8_t kind = import_fixup_kind(type);
                const SymbolId target = symbol < symbols->count ? object->symbols[symbol] : 0;
                const uint64_t entry_offset = header.sh_offset + (uint64_t)sorted[i] * sizeof(Elf64_Rela);
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
                result.section = result.error != IMPORT_ERROR_NONE ? index : 0;
                result.relocation = result.error != IMPORT_ERROR_NONE ? sorted[i] : 0;
            }
        }
    }
    return result;
}

static ImportResult import_object(Link* link, String8 bytes, uint32_t unit, uint8_t source, Arena* arena, ImportObject* object)
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
    return result;
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
static bool import_archive_name(const char* field, String8 long_names, String8* name)
{
    bool result = true;
    if (field[0] == '/' && field[1] >= '0' && field[1] <= '9')
    {
        uint64_t offset = 0;
        result = import_archive_number(field + 1, 15, &offset) && offset < long_names.length;
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

static ImportResult import_archive(ImportArchive* archive, String8 bytes, Arena* arena)
{
    ImportResult result = { 0 };
    *archive = (ImportArchive){ 0 };
    const uint64_t magic = sizeof(BUSTER_IMPORT_ARCHIVE_MAGIC) - 1;
    if (bytes.length >= magic && memcmp(bytes.pointer, BUSTER_IMPORT_THIN_ARCHIVE_MAGIC, magic) == 0)
    {
        result = import_fail(IMPORT_ERROR_UNSUPPORTED, 0);
    }
    else if (bytes.length < magic || memcmp(bytes.pointer, BUSTER_IMPORT_ARCHIVE_MAGIC, magic) != 0)
    {
        result = import_fail(IMPORT_ERROR_FORMAT, 0);
    }
    // Two passes over the headers: count the members, then fill them; the index refers to them by offset.
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
            const bool valid = header_fits && header[58] == '`' && header[59] == '\n' && import_archive_number(header + 48, 10, &size) &&
                               import_within(bytes.length, offset + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES, size);
            if (!valid)
            {
                result = import_fail(IMPORT_ERROR_BOUNDS, offset);
            }
            else
            {
                const String8 data = {
                    .pointer = (char*)header + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES,
                    .length = size,
                };
                const String8 field = {
                    .pointer = (char*)header,
                    .length = 16,
                };
                const bool symbol_index = import_starts_with(field, "/ ");
                const bool symbol_index64 = import_starts_with(field, "/SYM64/ ");
                const bool long_name_table = import_starts_with(field, "// ");
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
                    if (!import_archive_name(header, long_names, &name))
                    {
                        result = import_fail(IMPORT_ERROR_BOUNDS, offset);
                    }
                    archive->members[member] = (ImportMember){
                        .name = name,
                        .bytes = data,
                        .offset = offset,
                    };
                    member += 1;
                }
                // Members start at even offsets: one padding byte after an odd size.
                offset += BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + size + (size & 1);
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

// The library's soname, DT_SONAME in its .dynamic, a string of the table the section links to.
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

static uint32_t import_library_find(const ImportLibrary* library, const uint8_t* name, uint64_t length, uint64_t hash)
{
    uint32_t result = 0;
    bool searching = library->table != 0;
    for (uint64_t slot = hash & library->table_mask; searching; slot = (slot + 1) & library->table_mask)
    {
        const uint32_t entry = library->table[slot];
        const ImportExport* export = entry != 0 ? &library->exports[entry - 1] : 0;
        const bool match = export != 0 && export->hash == hash && export->name.length == length && memcmp(export->name.pointer, name, length) == 0;
        result = match ? entry : 0;
        searching = entry != 0 && !match;
    }
    return result;
}

// Puts each export in the table by hash; a name exported twice at its default version keeps its first entry.
static ImportResult import_library_table(ImportLibrary* library, Arena* arena)
{
    ImportResult result = { 0 };
    uint64_t capacity = 16;
    while (capacity < 2 * (uint64_t)library->export_count + 1)
    {
        capacity *= 2;
    }
    library->table = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity);
    if (library->table == 0)
    {
        result = import_fail(IMPORT_ERROR_MEMORY, 0);
    }
    else
    {
        memset(library->table, 0, capacity * sizeof(uint32_t));
        library->table_mask = capacity - 1;
        for (uint32_t i = 0; i < library->export_count; i += 1)
        {
            const ImportExport* export = &library->exports[i];
            uint64_t slot = export->hash & library->table_mask;
            bool duplicate = false;
            while (library->table[slot] != 0 && !duplicate)
            {
                const ImportExport* other = &library->exports[library->table[slot] - 1];
                duplicate = other->hash == export->hash && other->name.length == export->name.length && memcmp(other->name.pointer, export->name.pointer, export->name.length) == 0;
                slot = duplicate ? slot : (slot + 1) & library->table_mask;
            }
            library->table[slot] = duplicate ? library->table[slot] : i + 1;
        }
    }
    return result;
}

static ImportResult import_library(ImportLibrary* library, String8 bytes, Arena* arena)
{
    ImportElf elf;
    *library = (ImportLibrary){ 0 };
    ImportResult result = import_elf(&elf, bytes, ET_DYN);
    uint32_t dynsym = 0;
    uint32_t versym = 0;
    uint32_t verdef = 0;
    uint32_t dynamic = 0;
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < elf.section_count; index += 1)
    {
        const uint32_t type = import_section_header(&elf, index).sh_type;
        dynsym = type == SHT_DYNSYM && dynsym == 0 ? index : dynsym;
        versym = type == SHT_GNU_versym && versym == 0 ? index : versym;
        verdef = type == SHT_GNU_verdef && verdef == 0 ? index : verdef;
        dynamic = type == SHT_DYNAMIC && dynamic == 0 ? index : dynamic;
    }
    const Elf64_Shdr symbols_header = dynsym != 0 ? import_section_header(&elf, dynsym) : (Elf64_Shdr){ 0 };
    uint64_t count = 0;
    const bool symbols_fit = dynsym != 0 && import_table(&symbols_header, sizeof(Elf64_Sym), &count) && count <= UINT32_MAX && symbols_header.sh_link != SHN_UNDEF &&
                             symbols_header.sh_link < elf.section_count && import_section_header(&elf, symbols_header.sh_link).sh_type == SHT_STRTAB;
    if (result.error == IMPORT_ERROR_NONE && !symbols_fit)
    {
        result = import_fail(dynsym == 0 ? IMPORT_ERROR_FORMAT : IMPORT_ERROR_BOUNDS, dynsym == 0 ? 0 : symbols_header.sh_offset);
        result.section = dynsym;
    }
    const Elf64_Shdr names_header = symbols_fit ? import_section_header(&elf, symbols_header.sh_link) : (Elf64_Shdr){ 0 };
    const String8 names = symbols_fit ? import_section_bytes(&elf, &names_header) : (String8){ 0 };
    const String8 entries = symbols_fit ? import_section_bytes(&elf, &symbols_header) : (String8){ 0 };
    const Elf64_Shdr versions_header = versym != 0 ? import_section_header(&elf, versym) : (Elf64_Shdr){ 0 };
    uint64_t version_count = 0;
    if (result.error == IMPORT_ERROR_NONE && versym != 0 && (!import_table(&versions_header, sizeof(uint16_t), &version_count) || version_count != count))
    {
        result = import_fail(IMPORT_ERROR_BOUNDS, versions_header.sh_offset);
        result.section = versym;
    }
    const String8 version_indices = versym != 0 ? import_section_bytes(&elf, &versions_header) : (String8){ 0 };
    if (result.error == IMPORT_ERROR_NONE && verdef != 0)
    {
        result = import_library_versions(library, &elf, verdef, arena);
    }
    if (result.error == IMPORT_ERROR_NONE && dynamic != 0)
    {
        result = import_library_soname(library, &elf, dynamic);
    }
    if (result.error == IMPORT_ERROR_NONE)
    {
        library->exports = BUSTER_ARENA_PUSH_ARRAY(arena, ImportExport, count);
        library->references = BUSTER_ARENA_PUSH_ARRAY(arena, ImportReference, count);
        result = count != 0 && (library->exports == 0 || library->references == 0) ? import_fail(IMPORT_ERROR_MEMORY, 0) : result;
    }
    for (uint64_t i = 1; result.error == IMPORT_ERROR_NONE && i < count; i += 1)
    {
        Elf64_Sym symbol;
        memcpy(&symbol, entries.pointer + i * sizeof(symbol), sizeof(symbol));
        String8 name = { 0 };
        uint16_t version = 1;
        if (versym != 0)
        {
            memcpy(&version, version_indices.pointer + i * sizeof(version), sizeof(version));
        }
        const uint32_t type = ELF64_ST_TYPE(symbol.st_info);
        const uint32_t binding = ELF64_ST_BIND(symbol.st_info);
        const uint32_t visibility = ELF64_ST_VISIBILITY(symbol.st_other);
        const bool global = binding == STB_GLOBAL || binding == STB_WEAK || binding == STB_GNU_UNIQUE;
        const uint16_t version_index = (uint16_t)(version & 0x7fff);
        // A name@VERSION entry (the hidden bit) isn't its name's default; VER_NDX_LOCAL entries aren't exported.
        const bool default_version = (version & 0x8000) == 0 && version_index != VER_NDX_LOCAL;
        if (!import_string(names, symbol.st_name, &name))
        {
            result = import_fail(IMPORT_ERROR_BOUNDS, symbols_header.sh_offset + i * sizeof(symbol));
            result.symbol = (uint32_t)i;
        }
        else if (symbol.st_shndx == SHN_UNDEF && global && name.length != 0)
        {
            library->references[library->reference_count] = (ImportReference){
                .name = name,
                .hash = link_hash((const uint8_t*)name.pointer, name.length),
                .weak = binding == STB_WEAK,
            };
            library->reference_count += 1;
        }
        else if (symbol.st_shndx != SHN_UNDEF && global && default_version && name.length != 0 && (visibility == STV_DEFAULT || visibility == STV_PROTECTED))
        {
            const uint32_t exported_type = type == STT_GNU_IFUNC ? STT_FUNC : type == STT_COMMON ? STT_OBJECT : type;
            const uint32_t exported_binding = binding == STB_WEAK ? STB_WEAK : STB_GLOBAL;
            bool defined = version_index == VER_NDX_GLOBAL;
            for (uint32_t v = 0; !defined && v < library->version_count; v += 1)
            {
                defined = library->versions[v].index == version_index;
            }
            if (!defined)
            {
                result = import_fail(IMPORT_ERROR_BOUNDS, versions_header.sh_offset + i * sizeof(version));
                result.symbol = (uint32_t)i;
            }
            library->exports[library->export_count] = (ImportExport){
                .name = name,
                .hash = link_hash((const uint8_t*)name.pointer, name.length),
                .size = symbol.st_size,
                .flags = (uint8_t)((exported_type & BUSTER_LINK_SYMBOL_TYPE) | (exported_binding << 4)),
                .version = version_index,
            };
            library->export_count += 1;
        }
    }
    if (result.error == IMPORT_ERROR_NONE)
    {
        result = import_library_table(library, arena);
    }
    return result;
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
