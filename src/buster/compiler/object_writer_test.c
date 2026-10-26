// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The object writer's tests, for the test-object-writer command:
//
//   build/compiler test-object-writer [--seed N] [--cross-check] [--keep]
//
// main.c includes this file after link_test.c, whose sets of programs (link_test_program_options) and assembly
// (link_test_print_program) it uses, and object_writer.c, and calls run_object_writer_tests for the command. The
// checks, in this order:
//   - Objects: test-link's programs (static ones and two that call libc), a link made by hand (object_test_hand_link,
//     for what the programs don't have) and a link without atoms, each unit's object built in memory with and without
//     function sections, and again over bytes set to BUSTER_OBJECT_TEST_POISON, which must come out the same
//     (object_build writes every byte). object_view_open reads each one back and checks the rules every object keeps:
//     the header's fixed fields (EI_OSABI ELFOSABI_GNU exactly when a symbol is an STT_GNU_IFUNC), section headers
//     and contents inside the file, aligned and apart, names in .shstrtab, one symbol table with its locals before
//     sh_info, symbols inside their sections, extended indices exactly where st_shndx is SHN_XINDEX, and relocation
//     sections tied to the symbol table and a section with contents, in offset order, against a symbol, over zero
//     fields. object_test_compare then checks it against the representation, from the representation alone: the
//     sections in order with their atoms' bytes at the offsets the alignments give and int3 or zero between them;
//     every symbol of the unit, then the undefined ones its fixups first refer to, in order; and a relocation per
//     fixup, of its kind's type, against the target or, where GNU as and LLVM do so, the section symbol with the
//     target's offset in the addend. Among them, each local-exec TLS reference must come out as R_X86_64_TPOFF32
//     against its TLS symbol over a zero field (not a frozen offset), each call through the GOT as
//     R_X86_64_GOTPCRELX, and each reference to a local IFUNC against the IFUNC. Then links with an absolute symbol,
//     which object_prepare refuses.
//   - Debug: units' objects with the DWARF of their functions (ObjectOptions.debug), compared as above and with what
//     object_debug_test.c works out again from the descriptor, the descriptors object_build refuses, and every unit's
//     object with its own entry, written by object_write_units on 1, 2, 3 and 8 lanes.
//   - Many sections: one unit of BUSTER_OBJECT_TEST_MANY_FUNCTIONS functions in sections of their own, past
//     SHN_LORESERVE: e_shnum and e_shstrndx in section 0, SHN_XINDEX and .symtab_shndx, compared as above.
//   - Lanes: every unit's object (a trailing one without atoms among them) written by object_write_units in groups of 1,
//     2, 3 and 8 lanes, byte for byte the same; and a group in which a lane has no room to start, which writes nothing
//     and gives every unit OBJECT_ERROR_FULL.
//   - Files: object_write_file into a new file, over a larger and a smaller one in place (the same inode, truncated),
//     over one with another hard link (which keeps the old bytes), also with a name of NAME_MAX bytes, into /dev/null,
//     into a pipe, and into a missing directory (an error).
//   - With --cross-check, which needs readelf, llvm-readobj, as, ld.bfd, ld.lld, mold and wild on PATH, and a cc that
//     takes -fuse-ld=wild:
//       - readelf -W and llvm-readobj list every object's EI_OSABI, sections, symbols and relocations (the link made
//         by hand's too), which must be what object_view_open read, field by field;
//       - GNU ld, lld, mold and wild link each program's objects into an executable that writes LinkProgram.output and
//         exits with status 0: a static program with -static -e _start, and one that calls libc through cc, as a PIE,
//         with -no-pie and with -static. wild links only the programs without SIZE32 and SIZE64, which wild 0.10
//         doesn't link, and among them one past SHN_LORESERVE sections. The same linkers link, the same three ways,
//         a program whose main calls a local IFUNC directly and through a pointer (object_test_ifunc_link), which
//         must exit with BUSTER_OBJECT_TEST_IFUNC_STATUS;
//       - GNU as assembles each program's units as link_test_print_program writes them, and its objects must match
//         the writer's (object_test_same_as; the comment that opens its part of the file says where they may differ);
//       - objects with DWARF, listed by both tools and linked by GNU ld, lld and mold, whose programs' DWARF
//         llvm-dwarfdump --verify passes and is the objects', relocated (object_debug_test.c).
// The objects and programs go in a new directory under $TMPDIR (or /tmp), removed at the end unless --keep. The exit
// status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally; reading an object back (ObjectView and object_view_open); comparing it with the
// representation (object_test_compare); running programs; the tools' listings (ObjectListing, readelf_listing,
// llvm_readobj_listing and object_compare_listing); the checks (objects, many sections, lanes, files, cross-check,
// with the comparison with GNU as's objects); and run_object_writer_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <spawn.h>
#include <time.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char** environ;

#define BUSTER_OBJECT_TEST_REPORTED 3
#define BUSTER_OBJECT_TEST_ARENA_BYTES (1ull << 34)
// The many-sections unit: two sections per function (its code and its relocations) take it past SHN_LORESERVE.
#define BUSTER_OBJECT_TEST_MANY_FUNCTIONS 33000
// How long a tool or a program may run, and how much of a program's output is compared.
#define BUSTER_OBJECT_TEST_RUN_SECONDS 60
#define BUSTER_OBJECT_TEST_OUTPUT_BYTES 256
#define BUSTER_OBJECT_TEST_PATH_BYTES 4096
#define BUSTER_OBJECT_TEST_MAX_TOKENS 16
// The workers of link_test_program_options's set that takes --functions in test-link.
#define BUSTER_OBJECT_TEST_FUNCTIONS 500
// The units of the link made by hand (object_test_hand_link).
#define BUSTER_OBJECT_TEST_HAND_UNITS 4
// The exit status of the program that calls a local IFUNC (object_test_ifunc_link): 21 from each call.
#define BUSTER_OBJECT_TEST_IFUNC_STATUS 42
// What the memory an object is built in holds the second time, which no byte of the object may keep.
#define BUSTER_OBJECT_TEST_POISON 0xa5

// The failures of a check, and the first few, printed.
typedef struct ObjectTestTally ObjectTestTally;
struct ObjectTestTally
{
    const char* check;
    uint64_t checked;
    uint64_t failures;
};

static void object_test_expect(ObjectTestTally* tally, bool condition, const char* what, uint64_t index)
{
    tally->checked += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_OBJECT_TEST_REPORTED)
        {
            printf("  %s: %s (%llu)\n", tally->check, what, (unsigned long long)index);
        }
        tally->failures += 1;
    }
}

static bool object_test_report(const ObjectTestTally* tally)
{
    printf("%s: %llu checks, %llu failed\n", tally->check, (unsigned long long)tally->checked, (unsigned long long)tally->failures);
    fflush(stdout);
    return tally->failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Reading an object back

// An ET_REL file as object_view_open found it: its section headers, with the count and the section name table's index
// read from section 0 past SHN_LORESERVE, and its symbol table.
typedef struct ObjectView ObjectView;
struct ObjectView
{
    const uint8_t* file;
    uint64_t length;
    const Elf64_Shdr* sections;
    uint64_t section_count;
    uint64_t section_names_index;
    uint64_t symbol_table_index;
    const Elf64_Sym* symbols;
    uint64_t symbol_count;
    // .symtab_shndx's entries, or 0 without one.
    const uint32_t* extended_indices;
    const char* symbol_names;
    uint64_t symbol_names_size;
    const char* section_names;
    uint64_t section_names_size;
};

// What each class's section and each kind's relocation must be, and what fills the gaps in code: written out again here
// rather than read from object_writer.c, so that a wrong entry there shows.
typedef struct ObjectTestSection ObjectTestSection;
struct ObjectTestSection
{
    const char* name;
    uint32_t type;
    uint64_t flags;
    uint64_t entry_size;
};

static const ObjectTestSection object_test_sections[BUSTER_LINK_CLASS_COUNT] = {
    [BUSTER_LINK_CLASS_RODATA] = {
        .name = ".rodata",
        .type = SHT_PROGBITS,
        .flags = SHF_ALLOC,
        .entry_size = 0,
    },
    [BUSTER_LINK_CLASS_STRINGS] = {
        .name = ".rodata.str1.1",
        .type = SHT_PROGBITS,
        .flags = SHF_ALLOC | SHF_MERGE | SHF_STRINGS,
        .entry_size = 1,
    },
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

static const uint32_t object_test_relocation_types[BUSTER_LINK_FIXUP_KINDS] = {
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

#define BUSTER_OBJECT_TEST_INT3 0xcc

// The bytes a relocation type patches, or 0 for a type the object writer doesn't write.
static uint64_t object_test_relocation_width(uint32_t type)
{
    uint64_t result = 0;
    switch (type)
    {
        case R_X86_64_64:
        case R_X86_64_PC64:
        case R_X86_64_SIZE64:
        {
            result = 8;
        } break;
        case R_X86_64_PC32:
        case R_X86_64_PLT32:
        case R_X86_64_GOTPCREL:
        case R_X86_64_32:
        case R_X86_64_32S:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_TPOFF32:
        case R_X86_64_SIZE32:
        case R_X86_64_GOTPCRELX:
        case R_X86_64_REX_GOTPCRELX:
        case R_X86_64_TLSGD:
        case R_X86_64_TLSLD:
        case R_X86_64_DTPOFF32:
        {
            result = 4;
        } break;
        default:
        {
        } break;
    }
    return result;
}

// A NUL-terminated string at `offset` in a string table of `size` bytes, or 0 if it runs past the end.
static const char* object_view_string(const char* table, uint64_t size, uint64_t offset)
{
    const char* result = 0;
    if (offset < size && memchr(table + offset, 0, size - offset) != 0)
    {
        result = table + offset;
    }
    return result;
}

static const char* object_view_section_name(const ObjectView* view, uint64_t index)
{
    return object_view_string(view->section_names, view->section_names_size, view->sections[index].sh_name);
}

// A symbol's section index, read from .symtab_shndx for SHN_XINDEX.
static uint64_t object_view_symbol_section(const ObjectView* view, uint64_t symbol)
{
    const uint16_t index = view->symbols[symbol].st_shndx;
    return index == SHN_XINDEX && view->extended_indices != 0 ? view->extended_indices[symbol] : index;
}

// The name tools show for a symbol: a section symbol's is its section's.
static const char* object_view_symbol_name(const ObjectView* view, uint64_t symbol)
{
    const Elf64_Sym* entry = &view->symbols[symbol];
    const char* result;
    if (ELF64_ST_TYPE(entry->st_info) == STT_SECTION)
    {
        result = object_view_section_name(view, object_view_symbol_section(view, symbol));
    }
    else
    {
        result = object_view_string(view->symbol_names, view->symbol_names_size, entry->st_name);
    }
    return result;
}

// Parses an ET_REL file and checks what every object the writer writes keeps (the orientation comment). Returns false
// with a description in `error` at the first problem. With `foreign`, for GNU as's objects, a section's contents may
// start anywhere: GNU as 2.42 starts them at a multiple of their alignment, as the writer does, but 2.47 doesn't always
// (.rodata, aligned to 16, at an offset of 0x448).
static bool object_view_open(ObjectView* view, const uint8_t* file, uint64_t length, bool foreign, char* error, uint64_t error_size)
{
    *view = (ObjectView){
        .file = file,
        .length = length,
    };
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file;
    bool valid = length >= sizeof(Elf64_Ehdr);
    if (!valid)
    {
        snprintf(error, error_size, "the file has %llu bytes, fewer than an ELF header", (unsigned long long)length);
    }
    else
    {
        // EI_OSABI is ELFOSABI_SYSV or ELFOSABI_GNU, which the symbols decide (below).
        static const uint8_t identification[EI_NIDENT] = { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3, ELFCLASS64, ELFDATA2LSB, EV_CURRENT, ELFOSABI_SYSV };
        const uint8_t abi = header->e_ident[EI_OSABI];
        valid = memcmp(header->e_ident, identification, EI_OSABI) == 0 && (abi == ELFOSABI_SYSV || abi == ELFOSABI_GNU) &&
                memcmp(header->e_ident + EI_OSABI + 1, identification + EI_OSABI + 1, EI_NIDENT - EI_OSABI - 1) == 0 && header->e_type == ET_REL &&
                header->e_machine == EM_X86_64 && header->e_version == EV_CURRENT && header->e_entry == 0 && header->e_phoff == 0 && header->e_flags == 0 &&
                header->e_ehsize == sizeof(Elf64_Ehdr) && header->e_phentsize == 0 && header->e_phnum == 0 && header->e_shentsize == sizeof(Elf64_Shdr);
        if (!valid)
        {
            snprintf(error, error_size, "the ELF header isn't an x86-64 ET_REL one's");
        }
    }
    if (valid)
    {
        valid = header->e_shoff % 8 == 0 && header->e_shoff >= sizeof(Elf64_Ehdr) && header->e_shoff <= length && length - header->e_shoff >= sizeof(Elf64_Shdr);
        if (!valid)
        {
            snprintf(error, error_size, "the section headers start at %llu, outside the file or unaligned", (unsigned long long)header->e_shoff);
        }
    }
    if (valid)
    {
        view->sections = (const Elf64_Shdr*)(file + header->e_shoff);
        const Elf64_Shdr* first = &view->sections[0];
        view->section_count = header->e_shnum == 0 ? first->sh_size : header->e_shnum;
        view->section_names_index = header->e_shstrndx == SHN_XINDEX ? first->sh_link : header->e_shstrndx;
        const bool count_valid = header->e_shnum == 0 ? first->sh_size >= SHN_LORESERVE : header->e_shnum < SHN_LORESERVE && first->sh_size == 0;
        const bool names_valid = header->e_shstrndx == SHN_XINDEX ? first->sh_link >= SHN_LORESERVE : header->e_shstrndx < SHN_LORESERVE && first->sh_link == 0;
        const bool first_valid = first->sh_name == 0 && first->sh_type == SHT_NULL && first->sh_flags == 0 && first->sh_addr == 0 && first->sh_offset == 0 &&
                                 first->sh_info == 0 && first->sh_addralign == 0 && first->sh_entsize == 0;
        valid = count_valid && names_valid && first_valid && view->section_count <= (length - header->e_shoff) / sizeof(Elf64_Shdr) &&
                view->section_names_index != 0 && view->section_names_index < view->section_count;
        if (!valid)
        {
            snprintf(error, error_size, "section 0, e_shnum (%u) or e_shstrndx (%u) is wrong", header->e_shnum, header->e_shstrndx);
        }
    }
    if (valid)
    {
        const Elf64_Shdr* names = &view->sections[view->section_names_index];
        valid = names->sh_type == SHT_STRTAB && names->sh_offset <= length && names->sh_size <= length - names->sh_offset && names->sh_size != 0 &&
                file[names->sh_offset] == 0 && file[names->sh_offset + names->sh_size - 1] == 0;
        if (valid)
        {
            view->section_names = (const char*)file + names->sh_offset;
            view->section_names_size = names->sh_size;
        }
        else
        {
            snprintf(error, error_size, "the section name table isn't a string table inside the file");
        }
    }

    // Every section's header: a name, no address, a power-of-two alignment, and contents inside the file at an offset
    // aligned as the writer aligns it. Contents mustn't overlap each other, the ELF header or the section headers: a
    // byte map of the file records who covers each byte.
    uint8_t* covered = valid ? calloc(length, 1) : 0;
    if (valid && covered == 0)
    {
        snprintf(error, error_size, "out of memory for the byte map");
        valid = false;
    }
    if (valid)
    {
        memset(covered, 1, sizeof(Elf64_Ehdr));
        memset(covered + header->e_shoff, 1, view->section_count * sizeof(Elf64_Shdr));
    }
    for (uint64_t i = 1; valid && i < view->section_count; i += 1)
    {
        const Elf64_Shdr* section = &view->sections[i];
        const bool has_contents = section->sh_type != SHT_NOBITS;
        const uint64_t alignment = section->sh_addralign < BUSTER_OBJECT_MAX_FILE_ALIGNMENT ? section->sh_addralign : BUSTER_OBJECT_MAX_FILE_ALIGNMENT;
        valid = object_view_section_name(view, i) != 0 && section->sh_addr == 0 && (section->sh_addralign & (section->sh_addralign - 1)) == 0;
        valid = valid && (!has_contents || (section->sh_offset <= length && section->sh_size <= length - section->sh_offset &&
                                            (foreign || alignment == 0 || section->sh_offset % alignment == 0)));
        if (valid && has_contents)
        {
            valid = memchr(covered + section->sh_offset, 1, section->sh_size) == 0;
            memset(covered + section->sh_offset, 1, section->sh_size);
        }
        if (!valid)
        {
            snprintf(error, error_size, "section %llu's header or contents are wrong (outside the file, unaligned or overlapping)", (unsigned long long)i);
        }
    }
    free(covered);

    // The symbol table, and the tables it links.
    for (uint64_t i = 1; valid && i < view->section_count; i += 1)
    {
        const Elf64_Shdr* section = &view->sections[i];
        if (section->sh_type == SHT_SYMTAB)
        {
            valid = view->symbol_table_index == 0 && section->sh_entsize == sizeof(Elf64_Sym) && section->sh_size % sizeof(Elf64_Sym) == 0 &&
                    section->sh_size != 0 && section->sh_addralign == 8 && section->sh_link != 0 && section->sh_link < view->section_count &&
                    section->sh_info != 0 && section->sh_info <= section->sh_size / sizeof(Elf64_Sym);
            view->symbol_table_index = i;
            if (!valid)
            {
                snprintf(error, error_size, "section %llu is a second symbol table or a malformed one", (unsigned long long)i);
            }
        }
    }
    if (valid && view->symbol_table_index == 0)
    {
        snprintf(error, error_size, "there is no symbol table");
        valid = false;
    }
    if (valid)
    {
        const Elf64_Shdr* table = &view->sections[view->symbol_table_index];
        const Elf64_Shdr* names = &view->sections[table->sh_link];
        view->symbols = (const Elf64_Sym*)(file + table->sh_offset);
        view->symbol_count = table->sh_size / sizeof(Elf64_Sym);
        valid = names->sh_type == SHT_STRTAB && names->sh_size != 0 && file[names->sh_offset] == 0 && file[names->sh_offset + names->sh_size - 1] == 0;
        if (valid)
        {
            view->symbol_names = (const char*)file + names->sh_offset;
            view->symbol_names_size = names->sh_size;
        }
        else
        {
            snprintf(error, error_size, "the symbol table's string table isn't one");
        }
    }
    for (uint64_t i = 1; valid && i < view->section_count; i += 1)
    {
        const Elf64_Shdr* section = &view->sections[i];
        if (section->sh_type == SHT_SYMTAB_SHNDX)
        {
            valid = view->extended_indices == 0 && section->sh_link == view->symbol_table_index && section->sh_entsize == 4 && section->sh_addralign == 4 &&
                    section->sh_size == view->symbol_count * 4;
            view->extended_indices = (const uint32_t*)(file + section->sh_offset);
            if (!valid)
            {
                snprintf(error, error_size, "section %llu is a second extended index table or a malformed one", (unsigned long long)i);
            }
        }
    }

    // Each symbol: locals before sh_info and globals and weaks from it, a name inside .strtab, and a section index that
    // is reserved or names a section, through .symtab_shndx exactly when st_shndx is SHN_XINDEX.
    if (valid)
    {
        static const Elf64_Sym null_symbol;
        valid = memcmp(&view->symbols[0], &null_symbol, sizeof(Elf64_Sym)) == 0 && (view->extended_indices == 0 || view->extended_indices[0] == 0);
        if (!valid)
        {
            snprintf(error, error_size, "symbol 0 isn't null");
        }
    }
    const uint64_t first_global = valid ? view->sections[view->symbol_table_index].sh_info : 0;
    for (uint64_t i = 1; valid && i < view->symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &view->symbols[i];
        const uint32_t binding = ELF64_ST_BIND(symbol->st_info);
        const uint32_t type = ELF64_ST_TYPE(symbol->st_info);
        const uint64_t section = object_view_symbol_section(view, i);
        // Reserved by st_shndx itself: an index read from .symtab_shndx can be any number, SHN_COMMON's among them.
        const bool extended = symbol->st_shndx == SHN_XINDEX;
        const bool reserved = !extended && (section == SHN_UNDEF || section == SHN_ABS || section == SHN_COMMON);
        bool symbol_valid = i < first_global ? binding == STB_LOCAL : binding == STB_GLOBAL || binding == STB_WEAK;
        symbol_valid = symbol_valid && object_view_string(view->symbol_names, view->symbol_names_size, symbol->st_name) != 0 && (symbol->st_other & ~3u) == 0;
        symbol_valid = symbol_valid && extended == (view->extended_indices != 0 && view->extended_indices[i] != 0) &&
                       (!extended || view->extended_indices[i] >= SHN_LORESERVE);
        symbol_valid = symbol_valid && (reserved ? !extended : section < view->section_count && (extended || section < SHN_LORESERVE));
        if (symbol_valid && !reserved)
        {
            const Elf64_Shdr* defining = &view->sections[section];
            symbol_valid = symbol->st_value <= defining->sh_size && symbol->st_size <= defining->sh_size - symbol->st_value;
            symbol_valid = symbol_valid && (type != STT_TLS || (defining->sh_flags & SHF_TLS) != 0);
            symbol_valid = symbol_valid && (type != STT_SECTION || (symbol->st_value == 0 && symbol->st_size == 0 && symbol->st_name == 0 && binding == STB_LOCAL));
        }
        symbol_valid = symbol_valid && (type != STT_FILE || (section == SHN_ABS && binding == STB_LOCAL));
        valid = symbol_valid;
        if (!valid)
        {
            snprintf(error, error_size, "symbol %llu is wrong: binding %u, type %u, st_shndx %u, section %llu, value %llu, size %llu", (unsigned long long)i, binding, type,
                     symbol->st_shndx, (unsigned long long)section, (unsigned long long)symbol->st_value, (unsigned long long)symbol->st_size);
        }
    }
    // EI_OSABI: ELFOSABI_GNU exactly when a symbol is an STT_GNU_IFUNC, as GNU as and LLVM mark it.
    bool ifunc = false;
    for (uint64_t i = 1; valid && i < view->symbol_count; i += 1)
    {
        ifunc = ifunc || ELF64_ST_TYPE(view->symbols[i].st_info) == STT_GNU_IFUNC;
    }
    if (valid && header->e_ident[EI_OSABI] != (ifunc ? ELFOSABI_GNU : ELFOSABI_SYSV))
    {
        snprintf(error, error_size, "EI_OSABI is %u, and the object has %s STT_GNU_IFUNC symbol", header->e_ident[EI_OSABI], ifunc ? "an" : "no");
        valid = false;
    }

    // Relocations: tied to the symbol table and a section with contents, in offset order without overlapping fields,
    // inside their section, against a symbol, of a type the writer writes, and over zero fields.
    for (uint64_t i = 1; valid && i < view->section_count; i += 1)
    {
        const Elf64_Shdr* section = &view->sections[i];
        if (section->sh_type == SHT_RELA)
        {
            const Elf64_Shdr* target = section->sh_info < view->section_count ? &view->sections[section->sh_info] : 0;
            valid = section->sh_link == view->symbol_table_index && target != 0 && section->sh_info != 0 && target->sh_type != SHT_NOBITS &&
                    target->sh_type != SHT_RELA && (section->sh_flags & SHF_INFO_LINK) != 0 && section->sh_entsize == sizeof(Elf64_Rela) &&
                    section->sh_size % sizeof(Elf64_Rela) == 0 && section->sh_size != 0 && section->sh_addralign == 8;
            if (!valid)
            {
                snprintf(error, error_size, "relocation section %llu's header is wrong", (unsigned long long)i);
            }
            const Elf64_Rela* relocations = (const Elf64_Rela*)(file + section->sh_offset);
            const uint64_t count = section->sh_size / sizeof(Elf64_Rela);
            uint64_t end = 0;
            for (uint64_t r = 0; valid && r < count; r += 1)
            {
                const Elf64_Rela* relocation = &relocations[r];
                const uint64_t symbol = ELF64_R_SYM(relocation->r_info);
                const uint64_t width = object_test_relocation_width((uint32_t)ELF64_R_TYPE(relocation->r_info));
                valid = width != 0 && symbol != 0 && symbol < view->symbol_count && relocation->r_offset >= end && relocation->r_offset <= target->sh_size &&
                        width <= target->sh_size - relocation->r_offset;
                for (uint64_t byte = 0; valid && byte < width; byte += 1)
                {
                    valid = file[target->sh_offset + relocation->r_offset + byte] == 0;
                }
                end = relocation->r_offset + width;
                if (!valid)
                {
                    snprintf(error, error_size, "relocation %llu of section %llu is wrong: offset %llu, type %llu, symbol %llu", (unsigned long long)r, (unsigned long long)i,
                             (unsigned long long)relocation->r_offset, (unsigned long long)ELF64_R_TYPE(relocation->r_info), (unsigned long long)symbol);
                }
            }
        }
    }
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// Comparing an object with the representation

// What object_test_compare finds, for the report.
typedef struct ObjectTestCounts ObjectTestCounts;
struct ObjectTestCounts
{
    uint64_t sections;
    uint64_t symbols;
    uint64_t relocations;
    uint64_t section_symbol_relocations;
    uint64_t bytes;
    // Relocations by type (R_X86_64_*, which are below 64 here).
    uint64_t by_type[64];
};

// Whether a view's symbol `index` is the representation's `symbol` as the unit defines it, in section `section` at
// `offset` (its atom's in the section, plus its value), and named, if it has no name, `label`.
static bool object_test_same_symbol(const ObjectView* view, uint64_t index, const Link* link, SymbolId symbol, uint64_t section, uint64_t offset, const char* label)
{
    const Elf64_Sym* entry = &view->symbols[index];
    const NameId name = link->symbols.name[symbol];
    const char* entry_name = object_view_string(view->symbol_names, view->symbol_names_size, entry->st_name);
    const bool same_name = name == 0 ? entry_name != 0 && strcmp(entry_name, label) == 0
                                     : entry_name != 0 && strlen(entry_name) == link->names.length[name] && memcmp(entry_name, link->names.bytes[name], link->names.length[name]) == 0;
    return same_name && entry->st_info == (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_INFO) &&
           entry->st_other == link->symbols.flags[symbol] >> BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT && object_view_symbol_section(view, index) == section &&
           entry->st_value == offset && entry->st_size == link->symbols.size[symbol];
}

// Checks unit `unit`'s object, as `view` read it, against the representation: everything is worked out again here
// from the link alone (the orientation comment of object_writer.c says what the object holds, in what order), but the
// DWARF's `debug_headers` sections after the sections of atoms, which object_test_debug_compare checks.
static bool object_test_compare(ObjectTestTally* tally, const ObjectView* view, const Link* link, uint32_t unit, bool function_sections, uint64_t debug_headers,
                                ObjectTestCounts* counts, Arena* arena)
{
    const LinkAtoms* atoms = &link->atoms;
    const LinkSymbols* symbols = &link->symbols;
    const LinkFixups* fixups = &link->fixups;
    const uint64_t failures = tally->failures;
    const uint64_t length = arena->length;
    // Where each atom of the unit goes (its section's index and its offset there), the unit's atoms in the order they
    // are placed (which is section order), each TEXT atom's name for its own section, each section's section symbol,
    // and each symbol's index in the object (0 for none yet).
    uint64_t* atom_section = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, atoms->count);
    uint64_t* atom_offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, atoms->count);
    AtomId* placed = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, atoms->count);
    NameId* atom_name = BUSTER_ARENA_PUSH_ARRAY(arena, NameId, atoms->count);
    uint64_t* section_symbol = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, view->section_count);
    uint64_t* symbol_place = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, symbols->count);
    const bool allocated = atom_section != 0 && atom_offset != 0 && placed != 0 && atom_name != 0 && section_symbol != 0 && symbol_place != 0;
    object_test_expect(tally, allocated, "room for the comparison", unit);
    if (allocated)
    {
        memset(atom_name, 0, atoms->count * sizeof(NameId));
        memset(section_symbol, 0, view->section_count * sizeof(uint64_t));
        memset(symbol_place, 0, symbols->count * sizeof(uint64_t));
        for (SymbolId symbol = 1; symbol < symbols->count; symbol += 1)
        {
            const AtomId atom = symbols->atom[symbol];
            if (atom != 0 && symbols->value[symbol] == 0 && atom_name[atom] == 0)
            {
                atom_name[atom] = symbols->name[symbol];
            }
        }
    }

    // The sections of atoms in class order, each followed by its .rela section if its atoms have fixups.
    uint64_t index = 1;
    uint64_t placed_count = 0;
    for (uint8_t class = 1; allocated && class < BUSTER_LINK_CLASS_COUNT; class += 1)
    {
        uint64_t first_placed = placed_count;
        uint64_t offset = 0;
        uint64_t alignment = 1;
        uint64_t fixup_count = 0;
        // The unit's atoms of the class in id order, then a sentinel pass that closes the last section.
        for (AtomId atom = 1; atom <= atoms->count; atom += 1)
        {
            const bool in_class = atom < atoms->count && atoms->unit[atom] == unit && atoms->class[atom] == class;
            const bool closes = placed_count > first_placed && (atom == atoms->count || (in_class && function_sections && class == BUSTER_LINK_CLASS_TEXT));
            if (closes)
            {
                // The section the atoms placed from first_placed on went to.
                const Elf64_Shdr* header = index < view->section_count ? &view->sections[index] : 0;
                const ObjectTestSection* format = &object_test_sections[class];
                const char* name = header != 0 ? object_view_section_name(view, index) : 0;
                const NameId section_atom_name = function_sections && class == BUSTER_LINK_CLASS_TEXT ? atom_name[placed[first_placed]] : 0;
                char expected_name[BUSTER_OBJECT_TEST_PATH_BYTES];
                if (section_atom_name != 0)
                {
                    snprintf(expected_name, sizeof(expected_name), ".text.%.*s", (int)link->names.length[section_atom_name], (const char*)link->names.bytes[section_atom_name]);
                }
                else
                {
                    snprintf(expected_name, sizeof(expected_name), "%s", format->name);
                }
                const bool same = header != 0 && name != 0 && strcmp(name, expected_name) == 0 && header->sh_type == format->type && header->sh_flags == format->flags &&
                                  header->sh_entsize == format->entry_size && header->sh_addralign == alignment && header->sh_size == offset && header->sh_link == 0 &&
                                  header->sh_info == 0;
                object_test_expect(tally, same, "a section of atoms: its name, type, flags, alignment and size", index);
                // Its atoms' bytes, and int3 or zero before each.
                uint64_t covered = 0;
                for (uint64_t i = first_placed; same && header->sh_type != SHT_NOBITS && i < placed_count; i += 1)
                {
                    const AtomId other = placed[i];
                    const uint8_t* contents = view->file + header->sh_offset;
                    bool gap_valid = true;
                    for (uint64_t byte = covered; byte < atom_offset[other]; byte += 1)
                    {
                        gap_valid = gap_valid && contents[byte] == (class == BUSTER_LINK_CLASS_TEXT ? BUSTER_OBJECT_TEST_INT3 : 0);
                    }
                    object_test_expect(tally, gap_valid, "the gap before an atom", other);
                    object_test_expect(tally, atoms->size[other] == 0 || memcmp(contents + atom_offset[other], atoms->bytes[other], atoms->size[other]) == 0, "an atom's bytes",
                                       other);
                    covered = atom_offset[other] + atoms->size[other];
                }
                counts->sections += 1;
                index += 1;
                if (fixup_count != 0)
                {
                    const Elf64_Shdr* relocations = index < view->section_count ? &view->sections[index] : 0;
                    const char* relocation_name = relocations != 0 ? object_view_section_name(view, index) : 0;
                    char expected_relocation_name[BUSTER_OBJECT_TEST_PATH_BYTES + 8];
                    snprintf(expected_relocation_name, sizeof(expected_relocation_name), ".rela%s", expected_name);
                    object_test_expect(tally,
                                       relocations != 0 && relocation_name != 0 && strcmp(relocation_name, expected_relocation_name) == 0 && relocations->sh_type == SHT_RELA &&
                                           relocations->sh_info == index - 1 && relocations->sh_size == fixup_count * sizeof(Elf64_Rela),
                                       "a relocation section after its section", index);
                    index += 1;
                }
                first_placed = placed_count;
                offset = 0;
                alignment = 1;
                fixup_count = 0;
            }
            if (in_class)
            {
                const uint64_t atom_alignment = 1ull << atoms->alignment_log2[atom];
                offset = (offset + atom_alignment - 1) & ~(atom_alignment - 1);
                atom_section[atom] = index;
                atom_offset[atom] = offset;
                placed[placed_count] = atom;
                placed_count += 1;
                offset += atoms->size[atom];
                alignment = atom_alignment > alignment ? atom_alignment : alignment;
                fixup_count += atoms->fixup_count[atom];
            }
        }
    }

    // The trailing sections, after the DWARF's.
    index += debug_headers;
    static const char* const trailing[] = {
        ".note.GNU-stack",
        ".comment",
        ".symtab",
    };
    for (uint32_t i = 0; allocated && i < BUSTER_ARRAY_LENGTH(trailing); i += 1)
    {
        const char* name = index + i < view->section_count ? object_view_section_name(view, index + i) : 0;
        object_test_expect(tally, name != 0 && strcmp(name, trailing[i]) == 0, trailing[i], index + i);
    }
    if (allocated && index + 2 < view->section_count)
    {
        const Elf64_Shdr* note = &view->sections[index];
        const Elf64_Shdr* comment = &view->sections[index + 1];
        object_test_expect(tally, note->sh_type == SHT_PROGBITS && note->sh_flags == 0 && note->sh_size == 0, ".note.GNU-stack's header", index);
        object_test_expect(tally,
                           comment->sh_type == SHT_PROGBITS && comment->sh_flags == (SHF_MERGE | SHF_STRINGS) && comment->sh_entsize == 1 &&
                               comment->sh_size == BUSTER_OBJECT_COMMENT_BYTES && view->file[comment->sh_offset] == 0 &&
                               memcmp(view->file + comment->sh_offset + 1, BUSTER_OBJECT_COMMENT, sizeof(BUSTER_OBJECT_COMMENT)) == 0,
                           ".comment", index + 1);
        object_test_expect(tally, view->symbol_table_index == index + 2, ".symtab's place", index + 2);
        const bool extended = index - 1 >= SHN_LORESERVE;
        object_test_expect(tally, extended == (view->extended_indices != 0), ".symtab_shndx exactly past SHN_LORESERVE sections", index);
        const uint64_t after = index + 3 + (extended ? 1 : 0);
        object_test_expect(tally,
                           after + 2 == view->section_count && view->section_names_index == after + 1 && view->sections[view->symbol_table_index].sh_link == after,
                           ".strtab and .shstrtab last", after);
    }

    // The symbols: null and STT_FILE, then the section symbols in section order, each remembered by section.
    object_test_expect(tally, allocated && view->symbol_count > 1 && ELF64_ST_TYPE(view->symbols[1].st_info) == STT_FILE, "STT_FILE second", unit);
    uint64_t symbol_index = 2;
    while (allocated && symbol_index < view->symbol_count && ELF64_ST_TYPE(view->symbols[symbol_index].st_info) == STT_SECTION)
    {
        const uint64_t section = object_view_symbol_section(view, symbol_index);
        object_test_expect(tally, section_symbol[section] == 0 && (symbol_index == 2 || section > object_view_symbol_section(view, symbol_index - 1)),
                           "one section symbol per section, in order", symbol_index);
        section_symbol[section] = symbol_index;
        symbol_index += 1;
    }
    // The unit's local symbols in id order (those without a name named .LC0, .LC1 and so on), then its global and weak
    // ones in id order.
    uint64_t unnamed = 0;
    for (uint32_t pass = 0; allocated && pass < 2; pass += 1)
    {
        for (SymbolId symbol = 1; symbol < symbols->count; symbol += 1)
        {
            const AtomId atom = symbols->atom[symbol];
            const bool local = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
            if (atom != 0 && atoms->unit[atom] == unit && local == (pass == 0))
            {
                char label[32] = "";
                if (local && symbols->name[symbol] == 0)
                {
                    snprintf(label, sizeof(label), ".LC%llu", (unsigned long long)unnamed);
                    unnamed += 1;
                }
                const bool same = symbol_index < view->symbol_count &&
                                  object_test_same_symbol(view, symbol_index, link, symbol, atom_section[atom], atom_offset[atom] + symbols->value[symbol], label);
                object_test_expect(tally, same, local ? "a local symbol in order" : "a global symbol in order", symbol);
                symbol_place[symbol] = symbol_index;
                symbol_index += 1;
            }
        }
        if (pass == 0)
        {
            object_test_expect(tally, symbol_index == view->sections[view->symbol_table_index].sh_info, "sh_info: the first global", symbol_index);
        }
    }
    // The undefined symbols, in the order the fixups first refer to them, in section order.
    for (uint64_t i = 0; allocated && i < placed_count; i += 1)
    {
        const AtomId atom = placed[i];
        const FixupId first = atoms->fixup_begin[atom];
        for (FixupId fixup = first; fixup < first + atoms->fixup_count[atom]; fixup += 1)
        {
            const SymbolId target = fixups->target[fixup];
            const AtomId target_atom = symbols->atom[target];
            if ((target_atom == 0 || atoms->unit[target_atom] != unit) && symbol_place[target] == 0)
            {
                const Elf64_Sym* entry = symbol_index < view->symbol_count ? &view->symbols[symbol_index] : 0;
                const NameId name = symbols->name[target];
                const char* entry_name = entry != 0 ? object_view_string(view->symbol_names, view->symbol_names_size, entry->st_name) : 0;
                const uint8_t flags = symbols->flags[target];
                const uint8_t type = (flags & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS ? STT_TLS : STT_NOTYPE;
                const bool same = entry != 0 && entry_name != 0 && strlen(entry_name) == link->names.length[name] &&
                                  memcmp(entry_name, link->names.bytes[name], link->names.length[name]) == 0 &&
                                  entry->st_info == ((flags & BUSTER_LINK_SYMBOL_BINDING) | type) && entry->st_other == flags >> BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT &&
                                  entry->st_shndx == SHN_UNDEF && entry->st_value == 0 && entry->st_size == 0;
                object_test_expect(tally, same, "an undefined symbol in order", target);
                symbol_place[target] = symbol_index;
                symbol_index += 1;
            }
        }
    }
    object_test_expect(tally, allocated && symbol_index == view->symbol_count, "no symbols besides", symbol_index);

    // The relocations: each section's, its atoms' fixups in order.
    const Elf64_Rela* relocation = 0;
    const Elf64_Rela* end = 0;
    uint64_t current = 0;
    for (uint64_t i = 0; allocated && i <= placed_count; i += 1)
    {
        const uint64_t section = i < placed_count ? atom_section[placed[i]] : 0;
        if (section != current)
        {
            // Every relocation of the section before was a fixup's.
            object_test_expect(tally, relocation == end, "no relocations besides", current);
            const Elf64_Shdr* relocations = section != 0 && section + 1 < view->section_count ? &view->sections[section + 1] : 0;
            const bool has_relocations = relocations != 0 && relocations->sh_type == SHT_RELA && relocations->sh_info == section;
            relocation = has_relocations ? (const Elf64_Rela*)(view->file + relocations->sh_offset) : 0;
            end = has_relocations ? relocation + relocations->sh_size / sizeof(Elf64_Rela) : 0;
            current = section;
        }
        const AtomId atom = i < placed_count ? placed[i] : 0;
        const FixupId first = atom != 0 ? atoms->fixup_begin[atom] : 0;
        const uint32_t fixup_count = atom != 0 ? atoms->fixup_count[atom] : 0;
        object_test_expect(tally, fixup_count == 0 || relocation != 0, "a relocation section for the fixups", atom);
        for (FixupId fixup = first; fixup < first + fixup_count && relocation != 0 && relocation < end; fixup += 1)
        {
            const SymbolId target = fixups->target[fixup];
            const AtomId target_atom = symbols->atom[target];
            const uint8_t kind = fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            const int64_t addend = link_fixup_addend(link, fixup);
            const bool local = (symbols->flags[target] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
            // GNU as and LLVM: a local target through its section's symbol, but for the GOT, TLS and size kinds and an
            // IFUNC, and in a string pool for a PC-relative kind or an addend.
            const bool kept = kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL ||
                              kind == BUSTER_LINK_FIXUP_TPOFF32 || kind == BUSTER_LINK_FIXUP_GOTTPOFF || kind == BUSTER_LINK_FIXUP_SIZE32 || kind == BUSTER_LINK_FIXUP_SIZE64 ||
                              kind == BUSTER_LINK_FIXUP_TLSGD || kind == BUSTER_LINK_FIXUP_TLSLD || kind == BUSTER_LINK_FIXUP_DTPOFF32 ||
                              (symbols->flags[target] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_IFUNC ||
                              (atoms->class[target_atom] == BUSTER_LINK_CLASS_STRINGS && (link_fixup_kinds[kind].pc_relative || addend != 0));
            const bool through_section = local && !kept;
            const uint64_t expected_symbol = through_section ? section_symbol[atom_section[target_atom]] : symbol_place[target];
            const int64_t expected_addend = through_section ? addend + (int64_t)(atom_offset[target_atom] + symbols->value[target]) : addend;
            const uint32_t type = (uint32_t)ELF64_R_TYPE(relocation->r_info);
            const bool same = relocation->r_offset == atom_offset[atom] + fixups->offset[fixup] && type == object_test_relocation_types[kind] && expected_symbol != 0 &&
                              ELF64_R_SYM(relocation->r_info) == expected_symbol && relocation->r_addend == expected_addend;
            object_test_expect(tally, same, "a relocation: its offset, type, symbol and addend", fixup);
            // A local-exec TLS reference is a TPOFF32 against its TLS symbol over a zero field, not a frozen offset; a
            // call through the GOT is a GOTPCRELX.
            if (kind == BUSTER_LINK_FIXUP_TPOFF32)
            {
                const Elf64_Sym* referred = &view->symbols[ELF64_R_SYM(relocation->r_info)];
                const uint8_t* field = view->file + view->sections[section].sh_offset + relocation->r_offset;
                object_test_expect(tally, type == R_X86_64_TPOFF32 && ELF64_ST_TYPE(referred->st_info) == STT_TLS && memcmp(field, "\0\0\0\0", 4) == 0,
                                   "local-exec TLS as TPOFF32", fixup);
            }
            if (kind == BUSTER_LINK_FIXUP_CALL_GOT)
            {
                object_test_expect(tally, type == R_X86_64_GOTPCRELX, "a call through the GOT as GOTPCRELX", fixup);
            }
            counts->relocations += 1;
            counts->section_symbol_relocations += through_section ? 1 : 0;
            counts->by_type[type < 64 ? type : 0] += 1;
            relocation += 1;
        }
    }
    counts->symbols += view->symbol_count;
    counts->bytes += view->length;
    arena_pop_to(arena, length);
    return tally->failures == failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// Running programs

// Runs `arguments` (a program on PATH and its arguments, null-terminated) with stdout and stderr in the files at
// `output_path` and `error_path`, using the null-terminated `environment`, for up to BUSTER_OBJECT_TEST_RUN_SECONDS,
// and returns its exit status: -1 if it couldn't start, was killed by a signal or ran out of time. The arguments and
// environment are borrowed until this synchronous call returns.
static int object_test_run_environment(char* const* arguments, const char* output_path, const char* error_path, char* const* environment)
{
    int result = -1;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) == 0)
    {
        pid_t child = 0;
        const bool ready = posix_spawn_file_actions_addopen(&actions, 1, output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                           posix_spawn_file_actions_addopen(&actions, 2, error_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0;
        if (ready && posix_spawnp(&child, arguments[0], &actions, 0, arguments, environment) == 0)
        {
            int status = 0;
            pid_t waited = 0;
            const struct timespec pause = {
                .tv_sec = 0,
                .tv_nsec = 1000000,
            };
            for (uint64_t milliseconds = 0; waited == 0 && milliseconds < 1000ull * BUSTER_OBJECT_TEST_RUN_SECONDS; milliseconds += 1)
            {
                waited = waitpid(child, &status, WNOHANG);
                if (waited == 0)
                {
                    nanosleep(&pause, 0);
                }
            }
            if (waited == 0)
            {
                kill(child, SIGKILL);
                waitpid(child, &status, 0);
            }
            else if (waited == child && WIFEXITED(status))
            {
                result = WEXITSTATUS(status);
            }
        }
        posix_spawn_file_actions_destroy(&actions);
    }
    return result;
}

static int object_test_run(char* const* arguments, const char* output_path, const char* error_path)
{
    return object_test_run_environment(arguments, output_path, error_path, environ);
}

// A file's bytes, NUL-terminated, from `arena`; a null pointer if it can't be read.
static String8 object_test_read_file(Arena* arena, const char* path)
{
    String8 result = { 0 };
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat stats;
    if (fd != -1 && fstat(fd, &stats) == 0)
    {
        const uint64_t length = (uint64_t)stats.st_size;
        char* bytes = arena_push(arena, length + 1, 64);
        uint64_t read_bytes = 0;
        while (bytes != 0 && read_bytes < length)
        {
            const ssize_t count = read(fd, bytes + read_bytes, length - read_bytes);
            read_bytes += count > 0 ? (uint64_t)count : 0;
            bytes = count > 0 ? bytes : 0;
        }
        if (bytes != 0)
        {
            bytes[length] = 0;
            result.pointer = bytes;
            result.length = length;
        }
    }
    if (fd != -1)
    {
        close(fd);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The tools' listings

typedef struct ObjectListedSection ObjectListedSection;
struct ObjectListedSection
{
    String8 name;
    uint64_t type;
    uint64_t flags;
    uint64_t address;
    uint64_t offset;
    uint64_t size;
    uint64_t link;
    uint64_t info;
    uint64_t alignment;
    uint64_t entry_size;
};

typedef struct ObjectListedSymbol ObjectListedSymbol;
struct ObjectListedSymbol
{
    String8 name;
    uint64_t value;
    uint64_t size;
    uint64_t type;
    uint64_t binding;
    uint64_t visibility;
    // Its section's index, read through .symtab_shndx, or SHN_UNDEF, SHN_ABS or SHN_COMMON.
    uint64_t section;
};

typedef struct ObjectListedRelocation ObjectListedRelocation;
struct ObjectListedRelocation
{
    // The relocation section's index.
    uint64_t section;
    uint64_t offset;
    uint64_t type;
    // The symbol's name, or a section symbol's section's.
    String8 symbol;
    int64_t addend;
};

// What a tool lists: the header's EI_OSABI, section count and name table index, and the sections, symbols and
// relocations in its order, pushed on an arena one after another. The names point into the tool's output.
typedef struct ObjectListing ObjectListing;
struct ObjectListing
{
    uint64_t abi;
    uint64_t section_count;
    uint64_t section_names_index;
    ObjectListedSection* sections;
    uint64_t section_length;
    ObjectListedSymbol* symbols;
    uint64_t symbol_length;
    ObjectListedRelocation* relocations;
    uint64_t relocation_length;
};

// The next line of `text` from *position, without its newline, advancing past it; false at the end.
static bool object_test_next_line(String8 text, uint64_t* position, String8* line)
{
    const bool result = *position < text.length;
    if (result)
    {
        const char* begin = text.pointer + *position;
        const char* newline = memchr(begin, '\n', text.length - *position);
        const uint64_t length = newline != 0 ? (uint64_t)(newline - begin) : text.length - *position;
        *line = (String8){
            .pointer = (char*)begin,
            .length = length,
        };
        *position += length + (newline != 0 ? 1 : 0);
    }
    return result;
}

// A line's space-separated tokens, at most `capacity` of them; returns how many there are.
static uint64_t object_test_tokens(String8 line, String8* tokens, uint64_t capacity)
{
    uint64_t count = 0;
    uint64_t i = 0;
    while (i < line.length)
    {
        while (i < line.length && line.pointer[i] == ' ')
        {
            i += 1;
        }
        const uint64_t begin = i;
        while (i < line.length && line.pointer[i] != ' ')
        {
            i += 1;
        }
        if (i > begin)
        {
            if (count < capacity)
            {
                tokens[count] = (String8){
                    .pointer = line.pointer + begin,
                    .length = i - begin,
                };
            }
            count += 1;
        }
    }
    return count;
}

static bool object_test_equal(String8 string, const char* text)
{
    return text != 0 && string.length == strlen(text) && memcmp(string.pointer, text, string.length) == 0;
}

static bool object_test_starts_with(String8 string, const char* text)
{
    return string.length >= strlen(text) && memcmp(string.pointer, text, strlen(text)) == 0;
}

static String8 object_test_after(String8 string, uint64_t count)
{
    String8 result = {
        .pointer = string.pointer + count,
        .length = string.length - count,
    };
    return result;
}

// A number: hexadecimal with a 0x prefix or when `hexadecimal`, otherwise decimal. False if `token` isn't one.
static bool object_test_number(String8 token, bool hexadecimal, uint64_t* value)
{
    uint64_t i = 0;
    uint64_t base = hexadecimal ? 16 : 10;
    if (token.length > 2 && token.pointer[0] == '0' && (token.pointer[1] == 'x' || token.pointer[1] == 'X'))
    {
        i = 2;
        base = 16;
    }
    bool result = i < token.length;
    uint64_t number = 0;
    for (; result && i < token.length; i += 1)
    {
        const char c = token.pointer[i];
        const uint64_t digit = c >= '0' && c <= '9' ? (uint64_t)(c - '0') : c >= 'a' && c <= 'f' ? (uint64_t)(c - 'a' + 10) : c >= 'A' && c <= 'F' ? (uint64_t)(c - 'A' + 10) : 16;
        result = digit < base && !__builtin_mul_overflow(number, base, &number) && !__builtin_add_overflow(number, digit, &number);
    }
    *value = number;
    return result;
}

// The last '(' in `string`, or 0.
static const char* object_test_last_parenthesis(String8 string)
{
    const char* result = 0;
    for (uint64_t i = string.length; result == 0 && i > 0; i -= 1)
    {
        result = string.pointer[i - 1] == '(' ? string.pointer + i - 1 : 0;
    }
    return result;
}

// The number in the parentheses that end `string` ("Local (0x0)", "0 (140008)"), or `string` itself without them.
static bool object_test_parenthesized(String8 string, uint64_t* value)
{
    String8 number = string;
    while (number.length > 0 && number.pointer[number.length - 1] == ' ')
    {
        number.length -= 1;
    }
    if (number.length >= 3 && number.pointer[number.length - 1] == ')')
    {
        const char* open = object_test_last_parenthesis(number);
        if (open != 0)
        {
            number.length = (uint64_t)(number.pointer + number.length - 1 - (open + 1));
            number.pointer = (char*)open + 1;
        }
    }
    return object_test_number(number, false, value);
}

// The value after "Key:" in a line, without the spaces before it.
static String8 object_test_value(String8 line)
{
    const char* colon = memchr(line.pointer, ':', line.length);
    String8 result = { 0 };
    if (colon != 0)
    {
        result = object_test_after(line, (uint64_t)(colon + 1 - line.pointer));
        while (result.length > 0 && result.pointer[0] == ' ')
        {
            result = object_test_after(result, 1);
        }
    }
    return result;
}

typedef struct ObjectTestName ObjectTestName;
struct ObjectTestName
{
    const char* name;
    uint64_t value;
};

static const ObjectTestName object_test_relocation_names[] = {
    { "R_X86_64_NONE", R_X86_64_NONE },
    { "R_X86_64_64", R_X86_64_64 },
    { "R_X86_64_PC32", R_X86_64_PC32 },
    { "R_X86_64_PLT32", R_X86_64_PLT32 },
    { "R_X86_64_GOTPCREL", R_X86_64_GOTPCREL },
    { "R_X86_64_32", R_X86_64_32 },
    { "R_X86_64_32S", R_X86_64_32S },
    { "R_X86_64_GOTTPOFF", R_X86_64_GOTTPOFF },
    { "R_X86_64_TPOFF32", R_X86_64_TPOFF32 },
    { "R_X86_64_PC64", R_X86_64_PC64 },
    { "R_X86_64_SIZE32", R_X86_64_SIZE32 },
    { "R_X86_64_SIZE64", R_X86_64_SIZE64 },
    { "R_X86_64_GOTPCRELX", R_X86_64_GOTPCRELX },
    { "R_X86_64_REX_GOTPCRELX", R_X86_64_REX_GOTPCRELX },
};

// readelf's names of OS ABIs, section types, symbol types, bindings, visibilities and section flags.
static const ObjectTestName readelf_abis[] = {
    { "UNIX - System V", ELFOSABI_SYSV },
    { "UNIX - GNU", ELFOSABI_GNU },
};

static const ObjectTestName readelf_section_types[] = {
    { "NULL", SHT_NULL },
    { "PROGBITS", SHT_PROGBITS },
    { "SYMTAB", SHT_SYMTAB },
    { "STRTAB", SHT_STRTAB },
    { "RELA", SHT_RELA },
    { "NOTE", SHT_NOTE },
    { "NOBITS", SHT_NOBITS },
    { "INIT_ARRAY", SHT_INIT_ARRAY },
    { "FINI_ARRAY", SHT_FINI_ARRAY },
    { "SYMTAB SECTION INDICES", SHT_SYMTAB_SHNDX },
    { "X86_64_UNWIND", SHT_X86_64_UNWIND },
};

static const ObjectTestName readelf_symbol_types[] = {
    { "NOTYPE", STT_NOTYPE },
    { "OBJECT", STT_OBJECT },
    { "FUNC", STT_FUNC },
    { "SECTION", STT_SECTION },
    { "FILE", STT_FILE },
    { "COMMON", STT_COMMON },
    { "TLS", STT_TLS },
    { "IFUNC", STT_GNU_IFUNC },
};

static const ObjectTestName readelf_bindings[] = {
    { "LOCAL", STB_LOCAL },
    { "GLOBAL", STB_GLOBAL },
    { "WEAK", STB_WEAK },
};

static const ObjectTestName readelf_visibilities[] = {
    { "DEFAULT", STV_DEFAULT },
    { "INTERNAL", STV_INTERNAL },
    { "HIDDEN", STV_HIDDEN },
    { "PROTECTED", STV_PROTECTED },
};

static const ObjectTestName readelf_flag_letters[] = {
    { "W", SHF_WRITE },
    { "A", SHF_ALLOC },
    { "X", SHF_EXECINSTR },
    { "M", SHF_MERGE },
    { "S", SHF_STRINGS },
    { "I", SHF_INFO_LINK },
    { "L", SHF_LINK_ORDER },
    { "O", SHF_OS_NONCONFORMING },
    { "G", SHF_GROUP },
    { "T", SHF_TLS },
    { "C", SHF_COMPRESSED },
    { "E", SHF_EXCLUDE },
};

static bool object_test_lookup(const ObjectTestName* names, uint64_t count, String8 name, uint64_t* value)
{
    bool result = false;
    for (uint64_t i = 0; !result && i < count; i += 1)
    {
        result = object_test_equal(name, names[i].name);
        *value = names[i].value;
    }
    return result;
}

static bool readelf_flags(String8 letters, uint64_t* flags)
{
    bool result = true;
    uint64_t value = 0;
    for (uint64_t i = 0; result && i < letters.length; i += 1)
    {
        uint64_t flag = 0;
        const String8 letter = {
            .pointer = letters.pointer + i,
            .length = 1,
        };
        result = object_test_lookup(readelf_flag_letters, BUSTER_ARRAY_LENGTH(readelf_flag_letters), letter, &flag);
        value |= flag;
    }
    *flags = value;
    return result;
}

// Pushes an entry of `bytes` on `arena`, cleared, and makes it its listing's first if there is none yet: the
// listing's arrays grow one entry at a time, each on the arena alone while it is listed.
static void* object_test_list(Arena* arena, void** first, uint64_t* length, uint64_t bytes)
{
    void* result = arena_push(arena, bytes, 8);
    if (result != 0)
    {
        memset(result, 0, bytes);
        *first = *first == 0 ? result : *first;
        *length += 1;
    }
    return result;
}

// Parses `readelf -W -h -S -r -s`: the header's OS ABI and counts, the section headers, the relocation sections (which
// come before the symbols) and the symbols. Returns false, with the line in `error`, at a line it can't read.
static bool readelf_listing(Arena* arena, String8 text, ObjectListing* listing, char* error, uint64_t error_size)
{
    // Sections, relocations and symbols are listed one part after another, so each array is contiguous on the arena.
    // An OS ABI no object has, until its line is read.
    *listing = (ObjectListing){
        .abi = UINT64_MAX,
    };
    bool result = true;
    uint64_t position = 0;
    String8 line;
    String8 tokens[BUSTER_OBJECT_TEST_MAX_TOKENS];
    enum
    {
        READELF_HEADER,
        READELF_SECTIONS,
        READELF_RELOCATIONS,
        READELF_SYMBOLS,
    } part = READELF_HEADER;
    uint64_t relocation_section = 0;
    while (result && object_test_next_line(text, &position, &line))
    {
        const uint64_t count = object_test_tokens(line, tokens, BUSTER_OBJECT_TEST_MAX_TOKENS);
        if (object_test_starts_with(line, "  OS/ABI:"))
        {
            result = object_test_lookup(readelf_abis, BUSTER_ARRAY_LENGTH(readelf_abis), object_test_value(line), &listing->abi);
        }
        else if (object_test_starts_with(line, "  Number of section headers:"))
        {
            result = object_test_parenthesized(object_test_value(line), &listing->section_count);
        }
        else if (object_test_starts_with(line, "  Section header string table index:"))
        {
            result = object_test_parenthesized(object_test_value(line), &listing->section_names_index);
        }
        else if (object_test_starts_with(line, "Section Headers:"))
        {
            part = READELF_SECTIONS;
        }
        else if (object_test_starts_with(line, "Symbol table '.symtab'"))
        {
            part = READELF_SYMBOLS;
        }
        else if (object_test_starts_with(line, "Relocation section '"))
        {
            // "Relocation section '.rela.text' at offset 0x4c8 contains 7 entries:": the listed section of that name
            // and offset, after the previous relocation section, since readelf lists them in section order.
            part = READELF_RELOCATIONS;
            const String8 name = object_test_after(line, strlen("Relocation section '"));
            const char* quote = memchr(name.pointer, '\'', name.length);
            uint64_t offset = 0;
            result = quote != 0 && count >= 6 && object_test_number(tokens[5], true, &offset);
            const uint64_t previous = relocation_section;
            for (uint64_t i = previous + 1; result && relocation_section == previous && i < listing->section_length; i += 1)
            {
                const ObjectListedSection* section = &listing->sections[i];
                if (section->name.length == (uint64_t)(quote - name.pointer) && memcmp(section->name.pointer, name.pointer, section->name.length) == 0 &&
                    section->offset == offset)
                {
                    relocation_section = i;
                }
            }
            result = result && relocation_section != previous;
        }
        else if (part == READELF_SECTIONS && line.length > 2 && line.pointer[2] == '[' && !object_test_starts_with(line, "  [Nr]"))
        {
            // "  [ 1] .text PROGBITS 0000000000000000 000040 00005c 00 AX 0 0 16": the name is padded to 17 columns
            // after "] " (spaces alone for section 0's empty one), the type is one or more words, then come the
            // address, offset, size and entry size, the flags if there are any, and the link, info and alignment.
            const char* close = memchr(line.pointer, ']', line.length);
            const String8 rest = close != 0 && (uint64_t)(close - line.pointer) + 2 <= line.length ? object_test_after(line, (uint64_t)(close - line.pointer) + 2) : (String8){ 0 };
            String8 fields[BUSTER_OBJECT_TEST_MAX_TOKENS];
            uint64_t field_count = 0;
            if (rest.length > 0 && rest.pointer[0] == ' ')
            {
                fields[0] = (String8){
                    .pointer = rest.pointer,
                    .length = 0,
                };
                field_count = 1 + object_test_tokens(rest, fields + 1, BUSTER_OBJECT_TEST_MAX_TOKENS - 1);
            }
            else
            {
                field_count = object_test_tokens(rest, fields, BUSTER_OBJECT_TEST_MAX_TOKENS);
            }
            uint64_t address_field = 2;
            uint64_t ignored = 0;
            while (address_field < field_count && address_field < BUSTER_OBJECT_TEST_MAX_TOKENS && !(fields[address_field].length == 16 && object_test_number(fields[address_field], true, &ignored)))
            {
                address_field += 1;
            }
            const uint64_t after = field_count - address_field;
            ObjectListedSection* section = 0;
            result = field_count <= BUSTER_OBJECT_TEST_MAX_TOKENS && (after == 7 || after == 8);
            if (result)
            {
                section = object_test_list(arena, (void**)&listing->sections, &listing->section_length, sizeof(ObjectListedSection));
                result = section != 0;
            }
            if (result)
            {
                const String8 type = {
                    .pointer = fields[1].pointer,
                    .length = (uint64_t)(fields[address_field - 1].pointer + fields[address_field - 1].length - fields[1].pointer),
                };
                const uint64_t a = address_field;
                section->name = fields[0];
                result = object_test_lookup(readelf_section_types, BUSTER_ARRAY_LENGTH(readelf_section_types), type, &section->type) &&
                         object_test_number(fields[a], true, &section->address) && object_test_number(fields[a + 1], true, &section->offset) &&
                         object_test_number(fields[a + 2], true, &section->size) && object_test_number(fields[a + 3], true, &section->entry_size) &&
                         (after == 7 || readelf_flags(fields[a + 4], &section->flags)) && object_test_number(fields[field_count - 3], false, &section->link) &&
                         object_test_number(fields[field_count - 2], false, &section->info) && object_test_number(fields[field_count - 1], false, &section->alignment);
            }
        }
        else if (part == READELF_RELOCATIONS && count == 7 && tokens[0].length == 16)
        {
            // "000000000000001a  0000000c00000002 R_X86_64_PC32 0000000000000000 .LC0 - 4"
            ObjectListedRelocation* relocation = object_test_list(arena, (void**)&listing->relocations, &listing->relocation_length, sizeof(ObjectListedRelocation));
            uint64_t magnitude = 0;
            result = relocation != 0 && object_test_number(tokens[0], true, &relocation->offset) &&
                     object_test_lookup(object_test_relocation_names, BUSTER_ARRAY_LENGTH(object_test_relocation_names), tokens[2], &relocation->type) &&
                     (object_test_equal(tokens[5], "+") || object_test_equal(tokens[5], "-")) && object_test_number(tokens[6], true, &magnitude);
            if (result)
            {
                relocation->section = relocation_section;
                relocation->symbol = tokens[4];
                relocation->addend = object_test_equal(tokens[5], "-") ? -(int64_t)magnitude : (int64_t)magnitude;
            }
        }
        else if (part == READELF_SYMBOLS && count >= 7 && tokens[0].length > 1 && tokens[0].pointer[tokens[0].length - 1] == ':')
        {
            // "     1: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS ref.c" (the header line's "Num:" isn't a number)
            uint64_t number = 0;
            const String8 index = {
                .pointer = tokens[0].pointer,
                .length = tokens[0].length - 1,
            };
            if (object_test_number(index, false, &number))
            {
                ObjectListedSymbol* symbol = object_test_list(arena, (void**)&listing->symbols, &listing->symbol_length, sizeof(ObjectListedSymbol));
                result = symbol != 0 && object_test_number(tokens[1], true, &symbol->value) && object_test_number(tokens[2], false, &symbol->size) &&
                         object_test_lookup(readelf_symbol_types, BUSTER_ARRAY_LENGTH(readelf_symbol_types), tokens[3], &symbol->type) &&
                         object_test_lookup(readelf_bindings, BUSTER_ARRAY_LENGTH(readelf_bindings), tokens[4], &symbol->binding) &&
                         object_test_lookup(readelf_visibilities, BUSTER_ARRAY_LENGTH(readelf_visibilities), tokens[5], &symbol->visibility);
                if (result)
                {
                    symbol->section = object_test_equal(tokens[6], "UND")   ? SHN_UNDEF
                                      : object_test_equal(tokens[6], "ABS") ? SHN_ABS
                                      : object_test_equal(tokens[6], "COM") ? SHN_COMMON
                                                                            : UINT64_MAX;
                    result = symbol->section != UINT64_MAX || object_test_number(tokens[6], false, &symbol->section);
                    if (count >= 8)
                    {
                        symbol->name = tokens[7];
                        symbol->name.length = (uint64_t)(line.pointer + line.length - tokens[7].pointer);
                    }
                }
            }
        }
        if (!result)
        {
            snprintf(error, error_size, "can't read readelf's line \"%.*s\"", (int)(line.length < 200 ? line.length : 200), line.pointer);
        }
    }
    return result;
}

// A listed name: from "Name: .text (32)" or "Name:  (0)", the text before the parenthesized offset.
static bool llvm_readobj_name(String8 value, String8* name)
{
    const char* open = object_test_last_parenthesis(value);
    const bool result = open != 0;
    if (result)
    {
        *name = (String8){
            .pointer = value.pointer,
            .length = (uint64_t)(open - value.pointer),
        };
        while (name->length > 0 && name->pointer[name->length - 1] == ' ')
        {
            name->length -= 1;
        }
    }
    return result;
}

// Parses `llvm-readobj --file-headers --sections --relocations --symbols`, in its LLVM style ("Key: value" lines in
// braces). Returns false, with the line in `error`, at a line it can't read.
static bool llvm_readobj_listing(Arena* arena, String8 text, ObjectListing* listing, char* error, uint64_t error_size)
{
    // An OS ABI no object has, until its line is read.
    *listing = (ObjectListing){
        .abi = UINT64_MAX,
    };
    bool result = true;
    uint64_t position = 0;
    String8 line;
    String8 tokens[BUSTER_OBJECT_TEST_MAX_TOKENS];
    enum
    {
        LLVM_READOBJ_HEADER,
        LLVM_READOBJ_SECTIONS,
        LLVM_READOBJ_RELOCATIONS,
        LLVM_READOBJ_SYMBOLS,
    } part = LLVM_READOBJ_HEADER;
    ObjectListedSection* section = 0;
    ObjectListedSymbol* symbol = 0;
    uint64_t relocation_section = 0;
    while (result && object_test_next_line(text, &position, &line))
    {
        const uint64_t count = object_test_tokens(line, tokens, BUSTER_OBJECT_TEST_MAX_TOKENS);
        const String8 value = object_test_value(line);
        if (count == 2 && object_test_equal(tokens[0], "Sections") && object_test_equal(tokens[1], "["))
        {
            part = LLVM_READOBJ_SECTIONS;
        }
        else if (count == 2 && object_test_equal(tokens[0], "Relocations") && object_test_equal(tokens[1], "["))
        {
            part = LLVM_READOBJ_RELOCATIONS;
        }
        else if (count == 2 && object_test_equal(tokens[0], "Symbols") && object_test_equal(tokens[1], "["))
        {
            part = LLVM_READOBJ_SYMBOLS;
        }
        else if (part == LLVM_READOBJ_HEADER && count >= 2 && object_test_equal(tokens[0], "OS/ABI:"))
        {
            // "OS/ABI: SystemV (0x0)" or "OS/ABI: GNU/Linux (0x3)".
            result = object_test_parenthesized(value, &listing->abi);
        }
        else if (part == LLVM_READOBJ_HEADER && count >= 2 && object_test_equal(tokens[0], "SectionHeaderCount:"))
        {
            result = object_test_parenthesized(value, &listing->section_count);
        }
        else if (part == LLVM_READOBJ_HEADER && count >= 2 && object_test_equal(tokens[0], "StringTableSectionIndex:"))
        {
            result = object_test_parenthesized(value, &listing->section_names_index);
        }
        else if (part == LLVM_READOBJ_SECTIONS && count == 2 && object_test_equal(tokens[0], "Section") && object_test_equal(tokens[1], "{"))
        {
            section = object_test_list(arena, (void**)&listing->sections, &listing->section_length, sizeof(ObjectListedSection));
            result = section != 0;
        }
        else if (part == LLVM_READOBJ_SECTIONS && section != 0 && count >= 2)
        {
            if (object_test_equal(tokens[0], "Name:"))
            {
                result = llvm_readobj_name(value, &section->name);
            }
            else if (object_test_equal(tokens[0], "Type:"))
            {
                result = object_test_parenthesized(value, &section->type);
            }
            else if (object_test_equal(tokens[0], "Flags") && object_test_equal(tokens[1], "["))
            {
                // "Flags [ (0x6)", and a line per flag after it.
                result = count == 3 && object_test_parenthesized(tokens[2], &section->flags);
            }
            else if (object_test_equal(tokens[0], "Address:"))
            {
                result = object_test_number(tokens[1], true, &section->address);
            }
            else if (object_test_equal(tokens[0], "Offset:"))
            {
                result = object_test_number(tokens[1], true, &section->offset);
            }
            else if (object_test_equal(tokens[0], "Size:"))
            {
                result = object_test_number(tokens[1], false, &section->size);
            }
            else if (object_test_equal(tokens[0], "Link:"))
            {
                result = object_test_number(tokens[1], false, &section->link);
            }
            else if (object_test_equal(tokens[0], "Info:"))
            {
                result = object_test_number(tokens[1], false, &section->info);
            }
            else if (object_test_equal(tokens[0], "AddressAlignment:"))
            {
                result = object_test_number(tokens[1], false, &section->alignment);
            }
            else if (object_test_equal(tokens[0], "EntrySize:"))
            {
                result = object_test_number(tokens[1], false, &section->entry_size);
            }
        }
        else if (part == LLVM_READOBJ_RELOCATIONS && count == 4 && object_test_equal(tokens[0], "Section") && object_test_equal(tokens[3], "{"))
        {
            // "Section (2) .rela.text {"
            result = tokens[1].length > 2 && object_test_number((String8){ .pointer = tokens[1].pointer + 1, .length = tokens[1].length - 2 }, false, &relocation_section);
        }
        else if (part == LLVM_READOBJ_RELOCATIONS && count == 4 && object_test_starts_with(tokens[0], "0x"))
        {
            // "0x1A R_X86_64_PC32 .LC0 0xFFFFFFFFFFFFFFFC"
            ObjectListedRelocation* relocation = object_test_list(arena, (void**)&listing->relocations, &listing->relocation_length, sizeof(ObjectListedRelocation));
            uint64_t addend = 0;
            result = relocation != 0 && object_test_number(tokens[0], true, &relocation->offset) &&
                     object_test_lookup(object_test_relocation_names, BUSTER_ARRAY_LENGTH(object_test_relocation_names), tokens[1], &relocation->type) &&
                     object_test_number(tokens[3], true, &addend);
            if (result)
            {
                relocation->section = relocation_section;
                relocation->symbol = tokens[2];
                relocation->addend = (int64_t)addend;
            }
        }
        else if (part == LLVM_READOBJ_SYMBOLS && count == 2 && object_test_equal(tokens[0], "Symbol") && object_test_equal(tokens[1], "{"))
        {
            symbol = object_test_list(arena, (void**)&listing->symbols, &listing->symbol_length, sizeof(ObjectListedSymbol));
            result = symbol != 0;
        }
        else if (part == LLVM_READOBJ_SYMBOLS && symbol != 0 && count >= 2)
        {
            if (object_test_equal(tokens[0], "Name:"))
            {
                result = llvm_readobj_name(value, &symbol->name);
            }
            else if (object_test_equal(tokens[0], "Value:"))
            {
                result = object_test_number(tokens[1], true, &symbol->value);
            }
            else if (object_test_equal(tokens[0], "Size:"))
            {
                result = object_test_number(tokens[1], false, &symbol->size);
            }
            else if (object_test_equal(tokens[0], "Binding:"))
            {
                result = object_test_parenthesized(value, &symbol->binding);
            }
            else if (object_test_equal(tokens[0], "Type:"))
            {
                result = object_test_parenthesized(value, &symbol->type);
            }
            else if (object_test_equal(tokens[0], "Other:"))
            {
                result = object_test_number(tokens[1], false, &symbol->visibility);
            }
            else if (object_test_equal(tokens[0], "Other") && object_test_equal(tokens[1], "["))
            {
                // "Other [ (0x2)", and a line per flag after it.
                uint64_t other = 0;
                result = count == 3 && object_test_parenthesized(tokens[2], &other);
                symbol->visibility = other & 3;
            }
            else if (object_test_equal(tokens[0], "Section:"))
            {
                // "Section: .text (0x1)", "Section: Undefined (0x0)" or "Section: Absolute (0xFFF1)".
                result = object_test_parenthesized(value, &symbol->section);
            }
        }
        if (!result)
        {
            snprintf(error, error_size, "can't read llvm-readobj's line \"%.*s\"", (int)(line.length < 200 ? line.length : 200), line.pointer);
        }
    }
    return result;
}

// Compares a tool's listing with the object as object_view_open read it: the header's OS ABI, section count and name
// table index, then every section, symbol and relocation, field by field. Returns false, with the first difference in
// `error`, if they differ.
static bool object_compare_listing(const ObjectView* view, const ObjectListing* listing, char* error, uint64_t error_size)
{
    const uint8_t abi = view->file[EI_OSABI];
    bool result = listing->abi == abi && listing->section_count == view->section_count && listing->section_names_index == view->section_names_index;
    if (!result)
    {
        snprintf(error, error_size, "it lists OS ABI %llu, %llu sections and the names in section %llu, not %u, %llu and %llu", (unsigned long long)listing->abi,
                 (unsigned long long)listing->section_count, (unsigned long long)listing->section_names_index, abi, (unsigned long long)view->section_count,
                 (unsigned long long)view->section_names_index);
    }
    if (result)
    {
        result = listing->section_length == view->section_count && listing->symbol_length == view->symbol_count;
        if (!result)
        {
            snprintf(error, error_size, "it lists %llu sections and %llu symbols, not %llu and %llu", (unsigned long long)listing->section_length,
                     (unsigned long long)listing->symbol_length, (unsigned long long)view->section_count, (unsigned long long)view->symbol_count);
        }
    }
    for (uint64_t i = 0; result && i < view->section_count; i += 1)
    {
        const Elf64_Shdr* section = &view->sections[i];
        const ObjectListedSection* listed = &listing->sections[i];
        // Section 0's size and link hold the count and the name table's index past SHN_LORESERVE.
        result = (i == 0 ? listed->name.length == 0 : object_test_equal(listed->name, object_view_section_name(view, i))) && listed->type == section->sh_type &&
                 listed->flags == section->sh_flags && listed->address == section->sh_addr && listed->offset == section->sh_offset && listed->size == section->sh_size &&
                 listed->link == section->sh_link && listed->info == section->sh_info && listed->alignment == section->sh_addralign &&
                 listed->entry_size == section->sh_entsize;
        if (!result)
        {
            snprintf(error, error_size, "section %llu differs: listed as %.*s, type %llu, flags 0x%llx, offset %llu, size %llu, link %llu, info %llu, alignment %llu",
                     (unsigned long long)i, (int)listed->name.length, listed->name.pointer, (unsigned long long)listed->type, (unsigned long long)listed->flags,
                     (unsigned long long)listed->offset, (unsigned long long)listed->size, (unsigned long long)listed->link, (unsigned long long)listed->info,
                     (unsigned long long)listed->alignment);
        }
    }
    for (uint64_t i = 0; result && i < view->symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &view->symbols[i];
        const ObjectListedSymbol* listed = &listing->symbols[i];
        const char* name = object_view_symbol_name(view, i);
        result = name != 0 && (name[0] == 0 ? listed->name.length == 0 : object_test_equal(listed->name, name)) && listed->value == symbol->st_value &&
                 listed->size == symbol->st_size && listed->type == ELF64_ST_TYPE(symbol->st_info) && listed->binding == ELF64_ST_BIND(symbol->st_info) &&
                 listed->visibility == ELF64_ST_VISIBILITY(symbol->st_other) && listed->section == object_view_symbol_section(view, i);
        if (!result)
        {
            snprintf(error, error_size, "symbol %llu (%s) differs: listed as %.*s, value %llu, size %llu, type %llu, binding %llu, visibility %llu, section %llu",
                     (unsigned long long)i, name != 0 ? name : "?", (int)listed->name.length, listed->name.pointer, (unsigned long long)listed->value,
                     (unsigned long long)listed->size, (unsigned long long)listed->type, (unsigned long long)listed->binding, (unsigned long long)listed->visibility,
                     (unsigned long long)listed->section);
        }
    }
    uint64_t listed_index = 0;
    for (uint64_t i = 1; result && i < view->section_count; i += 1)
    {
        const Elf64_Shdr* section = &view->sections[i];
        const uint64_t count = section->sh_type == SHT_RELA ? section->sh_size / sizeof(Elf64_Rela) : 0;
        const Elf64_Rela* relocations = (const Elf64_Rela*)(view->file + section->sh_offset);
        for (uint64_t r = 0; result && r < count; r += 1)
        {
            const Elf64_Rela* relocation = &relocations[r];
            const ObjectListedRelocation* listed = listed_index < listing->relocation_length ? &listing->relocations[listed_index] : 0;
            const char* name = object_view_symbol_name(view, ELF64_R_SYM(relocation->r_info));
            // A symbol without a name is listed without one, which readelf's columns then don't tell apart: the
            // relocation's other fields are compared.
            result = listed != 0 && listed->section == i && listed->offset == relocation->r_offset && listed->type == ELF64_R_TYPE(relocation->r_info) &&
                     listed->addend == relocation->r_addend && (name == 0 || name[0] == 0 || object_test_equal(listed->symbol, name));
            if (!result)
            {
                snprintf(error, error_size, "relocation %llu of section %llu (at %llu) differs or is missing", (unsigned long long)r, (unsigned long long)i,
                         (unsigned long long)relocation->r_offset);
            }
            listed_index += 1;
        }
    }
    if (result)
    {
        result = listed_index == listing->relocation_length;
        if (!result)
        {
            snprintf(error, error_size, "it lists %llu relocations, not %llu", (unsigned long long)listing->relocation_length, (unsigned long long)listed_index);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The checks

// The programs are test-link's (link_test_program_options): static ones (the defaults, the smallest, many units,
// without absolute addresses, and BUSTER_OBJECT_TEST_FUNCTIONS workers without sizes, which wild links) and two that
// call libc (the defaults, and many units without sizes).

// A unit's source name for its STT_FILE: "u<unit>.c", in `buffer`.
static String8 object_test_source_name(char* buffer, uint64_t size, uint32_t unit)
{
    const int length = snprintf(buffer, size, "u%u.c", unit);
    String8 result = {
        .pointer = buffer,
        .length = length > 0 ? (uint64_t)length : 0,
    };
    return result;
}

// The DWARF's checks, in object_debug_test.c, which main.c includes after debug.c.
static bool object_test_debug_compare(ObjectTestTally* tally, const ObjectView* view, const Link* link, const ObjectDebug* debug);
static bool object_test_debug(uint64_t seed, Arena* arena);
static bool object_test_debug_cross_check(uint64_t seed, const char* directory, bool keep, Arena* arena);
static bool object_test_debug_lanes(uint64_t seed, const char* directory, bool keep, Arena* arena);

// Builds unit `unit`'s object in memory, reads it back and compares it with the representation, and with its DWARF's
// descriptor if it has one. It builds it again over bytes set to BUSTER_OBJECT_TEST_POISON, which must come out the
// same: object_build writes every byte.
static bool object_test_unit(ObjectTestTally* tally, const Link* link, ObjectUnits* units, ObjectSymbols* symbols, uint32_t unit, ObjectOptions options,
                             ObjectTestCounts* counts, Arena* arena)
{
    const uint64_t length = arena->length;
    char source[32];
    const String8 source_name = object_test_source_name(source, sizeof(source), unit);
    const Object object = object_build(link, units, symbols, unit, options, source_name, arena);
    object_test_expect(tally, object.error == OBJECT_ERROR_NONE, "object_build", unit);
    bool result = object.error == OBJECT_ERROR_NONE;
    if (result)
    {
        // object_build pushes nothing on the arena but the object, so the second one goes where the poison is.
        const uint64_t after = arena->length;
        uint8_t* poisoned = arena_push(arena, object.size, 64);
        if (poisoned != 0)
        {
            memset(poisoned, BUSTER_OBJECT_TEST_POISON, object.size);
        }
        arena_pop_to(arena, after);
        const Object again = object_build(link, units, symbols, unit, options, source_name, arena);
        object_test_expect(tally,
                           poisoned != 0 && again.error == OBJECT_ERROR_NONE && again.bytes == poisoned && again.size == object.size &&
                               memcmp(again.bytes, object.bytes, object.size) == 0,
                           "every byte written", unit);
        arena_pop_to(arena, after);
        ObjectView view;
        char error[512];
        result = object_view_open(&view, object.bytes, object.size, false, error, sizeof(error));
        if (!result)
        {
            printf("  unit %u: %s\n", unit, error);
        }
        object_test_expect(tally, result, "object_view_open", unit);
        // The DWARF's five sections, .debug_info's relocations, and .debug_line's and .debug_addr's with functions: the
        // unit's entry's, which has them unless it has no sections and no functions.
        const ObjectDebug* debug = options.debug != 0 ? &options.debug[unit] : 0;
        bool absent = debug == 0 || (debug->function_count == 0 && debug->unit_count == 0);
        for (uint32_t s = 0; debug != 0 && s < BUSTER_OBJECT_DEBUG_SECTIONS; s += 1)
        {
            absent = absent && debug->sections[s].length == 0;
        }
        debug = absent ? 0 : debug;
        const uint64_t debug_headers = debug == 0 ? 0 : 6 + (debug->function_count != 0 ? 2 : 0);
        result = result && object_test_compare(tally, &view, link, unit, options.function_sections, debug_headers, counts, arena);
        result = result && (debug == 0 || object_test_debug_compare(tally, &view, link, debug));
        const char* file_name = result ? object_view_string(view.symbol_names, view.symbol_names_size, view.symbols[1].st_name) : 0;
        object_test_expect(tally, !result || (file_name != 0 && strcmp(file_name, source) == 0), "STT_FILE's name", unit);
        object_test_expect(tally, !result || (object.section_count == view.section_count && object.symbol_count == view.symbol_count && object.size == view.length),
                           "the counts object_build returns", unit);
    }
    arena_pop_to(arena, length);
    return result;
}

// An atom of the link made by hand, with its bytes copied onto `arena` (none for zero-fill or no size).
static AtomId object_test_hand_atom(Link* link, Arena* arena, uint8_t class, uint8_t alignment_log2, const uint8_t* source, uint64_t size, uint32_t unit)
{
    uint8_t* bytes = source != 0 && size != 0 ? arena_push(arena, size, 1) : 0;
    if (bytes != 0)
    {
        memcpy(bytes, source, size);
    }
    return source != 0 && size != 0 && bytes == 0 ? 0 : link_add_atom(link, class, alignment_log2, size, bytes, unit);
}

static NameId object_test_hand_name(Link* link, const char* text)
{
    return link_add_name(link, (const uint8_t*)text, (uint32_t)strlen(text));
}

// A link made by hand, on `arena`, for what the generated programs don't have: gaps between atoms of code (int3) and
// of data (zero), an atom of no size, a TEXT atom without a symbol at offset 0 (so its function section is .text), a
// weak reference to a symbol defined nowhere, hidden and protected symbols, a reference to a weak definition in another
// unit, pointers into a string pool without an addend (through the pool's section symbol) and with one (through the
// string's own), a call to and a pointer to a local IFUNC (through the IFUNC itself, in an ELFOSABI_GNU object), and
// units without atoms between others (unit 1) and after them (unit 3): BUSTER_OBJECT_TEST_HAND_UNITS in all. The fields
// are zero, as link_add_code_fixups leaves them.
static bool object_test_hand_link(Link* link, Arena* arena)
{
    const LinkCapacity capacity = {
        .names = 16,
        .atoms = 16,
        .symbols = 16,
        .fixups = 16,
        .wide_addends = 1,
    };
    bool result = link_initialize(link, arena, arena, capacity);
    if (result)
    {
        // call weak_nowhere; mov rax, [rip + hidden_data@GOTPCREL]; lea rax, [rip + .LC1] and ret; jmp weak_function.
        static const uint8_t call[] = { 0xe8, 0, 0, 0, 0 };
        static const uint8_t load[] = { 0x48, 0x8b, 0x05, 0, 0, 0, 0 };
        static const uint8_t lea[] = { 0x48, 0x8d, 0x05, 0, 0, 0, 0, 0xc3 };
        static const uint8_t jump[] = { 0xe9, 0, 0, 0, 0 };
        static const uint8_t strings[] = { 'h', 'i', 0, 't', 'h', 'e', 'r', 'e', 0 };
        static const uint8_t small[] = { 'a', 'b', 'c' };
        static const uint8_t pointers[24] = { 0 };
        static const uint8_t hidden[] = { 1, 2, 3, 4 };
        // xor eax, eax and ret: the IFUNC's resolver, which returns 0.
        static const uint8_t resolver[] = { 0x31, 0xc0, 0xc3 };
        const uint8_t function = BUSTER_LINK_SYMBOL_FUNCTION;
        const uint8_t object = BUSTER_LINK_SYMBOL_OBJECT;
        const uint8_t program = BUSTER_LINK_SOURCE_PROGRAM;
        // Unit 0: code at 0, 16 and 24, with int3 between; the strings; an empty .rodata atom; data at 0 and 8, with
        // zeros between; and .bss. Unit 2: data and code, the IFUNC, a call to it and a pointer to it.
        const AtomId entry_code = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_TEXT, 4, call, sizeof(call), 0);
        const AtomId load_code = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_TEXT, 4, load, sizeof(load), 0);
        const AtomId lea_code = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_TEXT, 2, lea, sizeof(lea), 0);
        const AtomId pool = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_STRINGS, 0, strings, sizeof(strings), 0);
        const AtomId empty = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_RODATA, 3, 0, 0, 0);
        const AtomId small_data = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_DATA, 3, small, sizeof(small), 0);
        const AtomId table = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_DATA, 3, pointers, sizeof(pointers), 0);
        const AtomId zeros = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_BSS, 2, 0, 16, 0);
        const AtomId hidden_atom = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_DATA, 2, hidden, sizeof(hidden), 2);
        const AtomId jump_code = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_TEXT, 2, jump, sizeof(jump), 2);
        const AtomId resolver_code = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_TEXT, 2, resolver, sizeof(resolver), 2);
        const AtomId ifunc_call = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_TEXT, 0, call, sizeof(call), 2);
        const AtomId ifunc_pointer = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_DATA, 3, pointers, 8, 2);
        const SymbolId weak_nowhere = link_add_symbol(link, object_test_hand_name(link, "weak_nowhere"), 0, 0, 0, BUSTER_LINK_SYMBOL_WEAK, BUSTER_LINK_SOURCE_NONE);
        link_add_symbol(link, object_test_hand_name(link, "entry"), entry_code, 0, sizeof(call), BUSTER_LINK_SYMBOL_GLOBAL | function, program);
        link_add_symbol(link, object_test_hand_name(link, "inside"), load_code, 3, 4, BUSTER_LINK_SYMBOL_LOCAL, program);
        const SymbolId weak_function = link_add_symbol(link, object_test_hand_name(link, "weak_function"), lea_code, 0, sizeof(lea), BUSTER_LINK_SYMBOL_WEAK | function, program);
        const SymbolId hi = link_add_symbol(link, 0, pool, 0, 3, BUSTER_LINK_SYMBOL_LOCAL | object, program);
        const SymbolId there = link_add_symbol(link, 0, pool, 3, 6, BUSTER_LINK_SYMBOL_LOCAL | object, program);
        link_add_symbol(link, object_test_hand_name(link, "empty"), empty, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_PROTECTED | object, program);
        link_add_symbol(link, object_test_hand_name(link, "small"), small_data, 0, sizeof(small), BUSTER_LINK_SYMBOL_LOCAL | object, program);
        link_add_symbol(link, object_test_hand_name(link, "table"), table, 0, sizeof(pointers), BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_PROTECTED | object, program);
        link_add_symbol(link, object_test_hand_name(link, "zeros"), zeros, 0, 16, BUSTER_LINK_SYMBOL_GLOBAL | object, program);
        const SymbolId hidden_data = link_add_symbol(link, object_test_hand_name(link, "hidden_data"), hidden_atom, 0, sizeof(hidden),
                                                     BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_HIDDEN | object, program);
        link_add_symbol(link, object_test_hand_name(link, "far"), jump_code, 0, sizeof(jump), BUSTER_LINK_SYMBOL_GLOBAL | function, program);
        const SymbolId chosen = link_add_symbol(link, object_test_hand_name(link, "chosen"), resolver_code, 0, sizeof(resolver),
                                                BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_IFUNC, program);
        // Each atom's fixups together, in offset order.
        link_add_fixup(link, entry_code, 1, BUSTER_LINK_FIXUP_CALL, weak_nowhere, -4);
        link_add_fixup(link, load_code, 3, BUSTER_LINK_FIXUP_LOAD_GOT, hidden_data, -4);
        link_add_fixup(link, lea_code, 3, BUSTER_LINK_FIXUP_PC32, there, -4);
        link_add_fixup(link, table, 0, BUSTER_LINK_FIXUP_ABS64, hi, 0);
        link_add_fixup(link, table, 8, BUSTER_LINK_FIXUP_ABS64, there, 2);
        link_add_fixup(link, table, 16, BUSTER_LINK_FIXUP_ABS64, weak_nowhere, 0);
        link_add_fixup(link, jump_code, 1, BUSTER_LINK_FIXUP_CALL, weak_function, -4);
        link_add_fixup(link, ifunc_call, 1, BUSTER_LINK_FIXUP_CALL, chosen, -4);
        link_add_fixup(link, ifunc_pointer, 0, BUSTER_LINK_FIXUP_ABS64, chosen, 0);
        result = link->error == LINK_ERROR_NONE && link_check(link) == LINK_ERROR_NONE;
    }
    return result;
}

// A program of one unit whose main calls a local IFUNC, chosen, directly and through a pointer, and returns the sum:
// chosen's resolver returns answer, which returns 21, so the program exits with BUSTER_OBJECT_TEST_IFUNC_STATUS once
// the linker has arranged the IFUNC's resolution (a call of the resolver itself returns answer's address).
static bool object_test_ifunc_link(Link* link, Arena* arena)
{
    const LinkCapacity capacity = {
        .names = 4,
        .atoms = 4,
        .symbols = 4,
        .fixups = 4,
        .wide_addends = 1,
    };
    bool result = link_initialize(link, arena, arena, capacity);
    if (result)
    {
        // main: push rbx; call chosen; mov ebx, eax; call [rip + pointer]; add eax, ebx; pop rbx; ret.
        static const uint8_t main_bytes[] = { 0x53, 0xe8, 0, 0, 0, 0, 0x89, 0xc3, 0xff, 0x15, 0, 0, 0, 0, 0x01, 0xd8, 0x5b, 0xc3 };
        // chosen, the resolver: lea rax, [rip + answer]; ret. answer: mov eax, 21; ret.
        static const uint8_t resolver[] = { 0x48, 0x8d, 0x05, 0, 0, 0, 0, 0xc3 };
        static const uint8_t answer_bytes[] = { 0xb8, BUSTER_OBJECT_TEST_IFUNC_STATUS / 2, 0, 0, 0, 0xc3 };
        static const uint8_t pointer_bytes[8] = { 0 };
        const uint8_t program = BUSTER_LINK_SOURCE_PROGRAM;
        const AtomId main_code = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_TEXT, 4, main_bytes, sizeof(main_bytes), 0);
        const AtomId resolver_code = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_TEXT, 4, resolver, sizeof(resolver), 0);
        const AtomId answer_code = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_TEXT, 4, answer_bytes, sizeof(answer_bytes), 0);
        const AtomId pointer_data = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_DATA, 3, pointer_bytes, sizeof(pointer_bytes), 0);
        link_add_symbol(link, object_test_hand_name(link, "main"), main_code, 0, sizeof(main_bytes), BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program);
        const SymbolId chosen = link_add_symbol(link, object_test_hand_name(link, "chosen"), resolver_code, 0, sizeof(resolver),
                                                BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_IFUNC, program);
        const SymbolId answer = link_add_symbol(link, object_test_hand_name(link, "answer"), answer_code, 0, sizeof(answer_bytes),
                                                BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION, program);
        const SymbolId pointer = link_add_symbol(link, object_test_hand_name(link, "pointer"), pointer_data, 0, sizeof(pointer_bytes),
                                                 BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT, program);
        link_add_fixup(link, main_code, 2, BUSTER_LINK_FIXUP_CALL, chosen, -4);
        link_add_fixup(link, main_code, 10, BUSTER_LINK_FIXUP_PC32, pointer, -4);
        link_add_fixup(link, resolver_code, 3, BUSTER_LINK_FIXUP_PC32, answer, -4);
        link_add_fixup(link, pointer_data, 0, BUSTER_LINK_FIXUP_ABS64, chosen, 0);
        result = link->error == LINK_ERROR_NONE && link_check(link) == LINK_ERROR_NONE;
    }
    return result;
}

// A link of one unit with an absolute symbol, absolute = 42: global and pointed to by an ABS64 if `referenced`, local
// and unreferenced otherwise. link_check accepts both; object_prepare refuses both, since no unit holds the symbol.
static bool object_test_absolute_link(Link* link, bool referenced, Arena* arena)
{
    const LinkCapacity capacity = {
        .names = 1,
        .atoms = 1,
        .symbols = 1,
        .fixups = 1,
        .wide_addends = 1,
    };
    bool result = link_initialize(link, arena, arena, capacity);
    if (result)
    {
        static const uint8_t pointer_bytes[8] = { 0 };
        const AtomId pointer_data = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_DATA, 3, pointer_bytes, sizeof(pointer_bytes), 0);
        const uint8_t binding = referenced ? BUSTER_LINK_SYMBOL_GLOBAL : BUSTER_LINK_SYMBOL_LOCAL;
        const SymbolId absolute = link_add_symbol(link, object_test_hand_name(link, "absolute"), 0, 42, 0, binding, BUSTER_LINK_SOURCE_ABSOLUTE);
        if (referenced)
        {
            link_add_fixup(link, pointer_data, 0, BUSTER_LINK_FIXUP_ABS64, absolute, 0);
        }
        result = link->error == LINK_ERROR_NONE && link_check(link) == LINK_ERROR_NONE;
    }
    return result;
}

// A link whose one relocation would need an addend past int64: a pointer, through its section's symbol, to a local
// symbol 8 bytes into its atom, with an addend of INT64_MAX - 4.
static bool object_test_overflow_link(Link* link, Arena* arena)
{
    const LinkCapacity capacity = {
        .names = 1,
        .atoms = 2,
        .symbols = 1,
        .fixups = 1,
        .wide_addends = 1,
    };
    bool result = link_initialize(link, arena, arena, capacity);
    if (result)
    {
        static const uint8_t zeros[16] = { 0 };
        const AtomId target_atom = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_DATA, 3, zeros, sizeof(zeros), 0);
        const AtomId pointer = object_test_hand_atom(link, arena, BUSTER_LINK_CLASS_DATA, 3, zeros, 8, 0);
        const SymbolId target = link_add_symbol(link, object_test_hand_name(link, "near_end"), target_atom, 8, 8, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT,
                                                BUSTER_LINK_SOURCE_PROGRAM);
        link_add_fixup(link, pointer, 0, BUSTER_LINK_FIXUP_ABS64, target, INT64_MAX - 4);
        result = link->error == LINK_ERROR_NONE && link_check(link) == LINK_ERROR_NONE;
    }
    return result;
}

static bool object_test_objects(uint64_t seed, Arena* arena)
{
    ObjectTestTally tally = {
        .check = "objects",
    };
    LinkProgramOptions options[BUSTER_LINK_TEST_ALL_PROGRAMS];
    const char* names[BUSTER_LINK_TEST_ALL_PROGRAMS];
    const char* directories[BUSTER_LINK_TEST_ALL_PROGRAMS];
    link_test_all_options(options, names, directories, seed, BUSTER_OBJECT_TEST_FUNCTIONS);
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
        for (uint32_t sections = 0; prepared && sections < 2; sections += 1)
        {
            const ObjectOptions object_options = {
                .function_sections = sections == 1,
            };
            ObjectTestCounts counts = { 0 };
            for (uint32_t unit = 0; unit < units.count; unit += 1)
            {
                object_test_unit(&tally, &program.link, &units, &symbols, unit, object_options, &counts, arena);
            }
            printf("  %s%s: %u objects, %llu bytes, %llu sections, %llu symbols, %llu relocations (%llu through section symbols; %llu TPOFF32, %llu GOTTPOFF, "
                   "%llu GOTPCRELX, %llu REX_GOTPCRELX, %llu GOTPCREL, %llu PLT32, %llu PC32, %llu 64, %llu PC64, %llu 32, %llu 32S, %llu SIZE32, %llu SIZE64)\n",
                   names[i], sections == 1 ? ", function sections" : "", units.count, (unsigned long long)counts.bytes, (unsigned long long)counts.sections,
                   (unsigned long long)counts.symbols, (unsigned long long)counts.relocations, (unsigned long long)counts.section_symbol_relocations,
                   (unsigned long long)counts.by_type[R_X86_64_TPOFF32], (unsigned long long)counts.by_type[R_X86_64_GOTTPOFF],
                   (unsigned long long)counts.by_type[R_X86_64_GOTPCRELX], (unsigned long long)counts.by_type[R_X86_64_REX_GOTPCRELX],
                   (unsigned long long)counts.by_type[R_X86_64_GOTPCREL], (unsigned long long)counts.by_type[R_X86_64_PLT32],
                   (unsigned long long)counts.by_type[R_X86_64_PC32], (unsigned long long)counts.by_type[R_X86_64_64], (unsigned long long)counts.by_type[R_X86_64_PC64],
                   (unsigned long long)counts.by_type[R_X86_64_32], (unsigned long long)counts.by_type[R_X86_64_32S], (unsigned long long)counts.by_type[R_X86_64_SIZE32],
                   (unsigned long long)counts.by_type[R_X86_64_SIZE64]);
            // Every program has local-exec TLS (a shared library none) and calls through the GOT, and every type but those
            // its options leave out.
            const bool local_exec = options[i].shared ? counts.by_type[R_X86_64_TPOFF32] == 0 : counts.by_type[R_X86_64_TPOFF32] != 0;
            object_test_expect(&tally, local_exec && counts.by_type[R_X86_64_GOTPCRELX] != 0 && counts.by_type[R_X86_64_GOTTPOFF] != 0 &&
                                           counts.by_type[R_X86_64_REX_GOTPCRELX] != 0 && counts.by_type[R_X86_64_GOTPCREL] != 0 && counts.by_type[R_X86_64_PLT32] != 0 &&
                                           counts.by_type[R_X86_64_PC32] != 0 && counts.by_type[R_X86_64_64] != 0 && counts.by_type[R_X86_64_PC64] != 0 &&
                                           (!options[i].absolute || (counts.by_type[R_X86_64_32] != 0 && counts.by_type[R_X86_64_32S] != 0)) &&
                                           (!options[i].sizes || (counts.by_type[R_X86_64_SIZE32] != 0 && counts.by_type[R_X86_64_SIZE64] != 0)) &&
                                           counts.section_symbol_relocations != 0,
                               "every relocation type", i);
        }
        // The same object twice, byte for byte.
        if (prepared)
        {
            const uint64_t before = arena->length;
            char source[32];
            const ObjectOptions defaults = { 0 };
            const Object first = object_build(&program.link, &units, &symbols, units.count - 1, defaults, object_test_source_name(source, sizeof(source), 0), arena);
            const Object second = object_build(&program.link, &units, &symbols, units.count - 1, defaults, object_test_source_name(source, sizeof(source), 0), arena);
            object_test_expect(&tally, first.error == OBJECT_ERROR_NONE && second.size == first.size && memcmp(first.bytes, second.bytes, first.size) == 0,
                               "the same object twice", i);
            arena_pop_to(arena, before);
            const Object missing = object_build(&program.link, &units, &symbols, units.count, defaults, object_test_source_name(source, sizeof(source), 0), arena);
            object_test_expect(&tally, missing.error == OBJECT_ERROR_RANGE && missing.bytes == 0, "a unit that doesn't exist", i);
        }
        arena_pop_to(arena, length);
        link_program_release(&program);
    }
    // The link made by hand, each unit's object with and without function sections: of its 9 relocations, only the
    // pointer to "hi" without an addend refers to a section symbol (the call to and the pointer to the local IFUNC refer
    // to the IFUNC). Fewer units than its atoms have is an error.
    const uint64_t length = arena->length;
    Link hand;
    ObjectUnits units;
    ObjectSymbols symbols;
    const bool made = object_test_hand_link(&hand, arena);
    object_test_expect(&tally, made, "the link made by hand", 0);
    object_test_expect(&tally, !made || !object_prepare(&units, &hand, 2, arena), "object_prepare with a unit missing", 0);
    const bool prepared = made && object_prepare(&units, &hand, BUSTER_OBJECT_TEST_HAND_UNITS, arena) && object_map_symbols(&symbols, &hand, arena);
    object_test_expect(&tally, !made || (prepared && units.count == BUSTER_OBJECT_TEST_HAND_UNITS), "object_prepare on the link made by hand", 0);
    for (uint32_t sections = 0; prepared && sections < 2; sections += 1)
    {
        const ObjectOptions object_options = {
            .function_sections = sections == 1,
        };
        ObjectTestCounts counts = { 0 };
        for (uint32_t unit = 0; unit < units.count; unit += 1)
        {
            object_test_unit(&tally, &hand, &units, &symbols, unit, object_options, &counts, arena);
        }
        object_test_expect(&tally, counts.relocations == 9 && counts.section_symbol_relocations == 1, "the link made by hand's relocations", sections);
    }
    // An arena without room for the object: nothing pushed.
    if (prepared)
    {
        Arena small = arena_reserve(256);
        const ObjectOptions defaults = { 0 };
        const Object full = object_build(&hand, &units, &symbols, 0, defaults, BUSTER_S8("u0.c"), &small);
        object_test_expect(&tally, small.base != 0 && full.error == OBJECT_ERROR_FULL && full.bytes == 0 && small.length == 0, "an arena without room for the object", 0);
        arena_release(&small);
    }
    arena_pop_to(arena, length);
    // A link of one unit without atoms, as an empty translation unit's: an object all the same.
    Link empty;
    const LinkCapacity nothing = { 0 };
    const bool empty_prepared = link_initialize(&empty, arena, arena, nothing) && object_prepare(&units, &empty, 1, arena) && object_map_symbols(&symbols, &empty, arena);
    object_test_expect(&tally, empty_prepared && units.count == 1, "object_prepare on a link without atoms", 0);
    for (uint32_t sections = 0; empty_prepared && sections < 2; sections += 1)
    {
        const ObjectOptions object_options = {
            .function_sections = sections == 1,
        };
        ObjectTestCounts counts = { 0 };
        object_test_unit(&tally, &empty, &units, &symbols, 0, object_options, &counts, arena);
    }
    arena_pop_to(arena, length);
    // An addend past int64.
    Link overflow;
    const bool overflow_made = object_test_overflow_link(&overflow, arena);
    object_test_expect(&tally, overflow_made, "the link with an addend past int64", 0);
    const bool overflow_prepared = overflow_made && object_prepare(&units, &overflow, 1, arena) && object_map_symbols(&symbols, &overflow, arena);
    if (overflow_prepared)
    {
        const ObjectOptions defaults = { 0 };
        const Object range = object_build(&overflow, &units, &symbols, 0, defaults, BUSTER_S8("u0.c"), arena);
        object_test_expect(&tally, range.error == OBJECT_ERROR_RANGE && range.bytes == 0, "an addend past int64 through a section symbol", 0);
    }
    object_test_expect(&tally, !overflow_made || overflow_prepared, "object_prepare on the link with an addend past int64", 0);
    arena_pop_to(arena, length);
    // Absolute symbols, referenced or not: refused, with nothing left on the arena.
    for (uint32_t referenced = 0; referenced < 2; referenced += 1)
    {
        Link absolute;
        const bool absolute_made = object_test_absolute_link(&absolute, referenced == 1, arena);
        object_test_expect(&tally, absolute_made, "the link with an absolute symbol", referenced);
        const uint64_t before = arena->length;
        const bool absolute_prepared = absolute_made && object_prepare(&units, &absolute, 1, arena);
        object_test_expect(&tally, absolute_made && !absolute_prepared && arena->length == before && units.count == 0, "object_prepare refuses an absolute symbol",
                           referenced);
        arena_pop_to(arena, length);
    }
    return object_test_report(&tally);
}

static bool object_test_many_sections(uint64_t seed, Arena* arena)
{
    ObjectTestTally tally = {
        .check = "many sections",
    };
    LinkProgramOptions options = link_program_default_options();
    options.seed = seed;
    options.units = 1;
    options.functions = BUSTER_OBJECT_TEST_MANY_FUNCTIONS;
    options.filler = 0;
    LinkProgram program;
    const bool generated = link_program_generate(&program, options);
    object_test_expect(&tally, generated, "link_program_generate", 0);
    const uint64_t length = arena->length;
    ObjectUnits units;
    ObjectSymbols symbols;
    const bool prepared = generated && object_prepare(&units, &program.link, options.units, arena) && object_map_symbols(&symbols, &program.link, arena);
    object_test_expect(&tally, !generated || prepared, "object_prepare", 0);
    if (prepared)
    {
        const ObjectOptions object_options = {
            .function_sections = true,
        };
        ObjectTestCounts counts = { 0 };
        object_test_unit(&tally, &program.link, &units, &symbols, 0, object_options, &counts, arena);
        const Object object = object_build(&program.link, &units, &symbols, 0, object_options, (String8){ .pointer = "u0.c", .length = 4 }, arena);
        const Elf64_Ehdr* header = (const Elf64_Ehdr*)object.bytes;
        const bool built = object.error == OBJECT_ERROR_NONE;
        object_test_expect(&tally, built && object.section_count > SHN_LORESERVE, "more than SHN_LORESERVE sections", object.section_count);
        object_test_expect(&tally, built && header->e_shnum == 0 && header->e_shstrndx == SHN_XINDEX, "e_shnum and e_shstrndx in section 0", 0);
        ObjectView view;
        char error[512];
        const bool opened = built && object_view_open(&view, object.bytes, object.size, false, error, sizeof(error));
        uint64_t extended = 0;
        for (uint64_t i = 1; opened && i < view.symbol_count; i += 1)
        {
            extended += view.symbols[i].st_shndx == SHN_XINDEX ? 1 : 0;
        }
        object_test_expect(&tally, opened && view.extended_indices != 0 && extended != 0, "symbols through .symtab_shndx", extended);
        printf("  %u functions in sections of their own: %llu sections, %llu symbols (%llu through .symtab_shndx), %llu relocations, %llu bytes\n",
               BUSTER_OBJECT_TEST_MANY_FUNCTIONS, (unsigned long long)object.section_count, (unsigned long long)object.symbol_count, (unsigned long long)extended,
               (unsigned long long)object.relocation_count, (unsigned long long)object.size);
    }
    arena_pop_to(arena, length);
    if (generated)
    {
        link_program_release(&program);
    }
    return object_test_report(&tally);
}

// What a lane group writing every unit's object shares.
typedef struct ObjectTestLanes ObjectTestLanes;
struct ObjectTestLanes
{
    const Link* link;
    ObjectUnits* units;
    ObjectOptions options;
    const char* const* paths;
    const String8* source_names;
    ObjectError* errors;
    bool written[BUSTER_LANE_MAX_COUNT];
};

static void object_test_lanes_main(void* argument)
{
    ObjectTestLanes* test = argument;
    test->written[lane_index()] = object_write_units(test->link, test->units, test->options, test->paths, test->source_names, test->errors);
}

// The same, but the last lane's scratch arena, where object_write_units takes what it needs to start, is full.
static void object_test_lanes_full_main(void* argument)
{
    ObjectTestLanes* test = argument;
    Scratch scratch = scratch_begin(0);
    if (lane_index() == lane_count() - 1)
    {
        arena_push(scratch.arena, scratch.arena->capacity - scratch.arena->length, 1);
    }
    test->written[lane_index()] = object_write_units(test->link, test->units, test->options, test->paths, test->source_names, test->errors);
    scratch_end(scratch);
}

// Writes every unit's object of a many-units program, and of a unit after them without atoms, with groups of 1, 2, 3
// and 8 lanes in both section modes, with permuted names, each into a directory of its own under `directory`,
// against bytes frozen before permutation. Then a group of 3 in which a lane has
// no room to start: no object, and every unit's error OBJECT_ERROR_FULL.
static bool object_test_lanes(uint64_t seed, uint64_t id_order, const char* directory, bool keep, Arena* arena)
{
    ObjectTestTally tally = {
        .check = "lanes",
    };
    static const uint32_t lane_counts[] = { 1, 2, 3, 8 };
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
    char** paths = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, count) : 0;
    char* path_bytes = prepared ? arena_push(arena, (uint64_t)count * BUSTER_OBJECT_TEST_PATH_BYTES, 1) : 0;
    String8* source_names = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, String8, count) : 0;
    char* source_bytes = prepared ? arena_push(arena, (uint64_t)count * 32, 1) : 0;
    ObjectError* errors = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, ObjectError, count) : 0;
    const bool allocated = paths != 0 && path_bytes != 0 && source_names != 0 && source_bytes != 0 && errors != 0;
    object_test_expect(&tally, !prepared || allocated, "room for the paths", 0);
    for (uint32_t unit = 0; allocated && unit < count; unit += 1)
    {
        source_names[unit] = object_test_source_name(source_bytes + (uint64_t)unit * 32, 32, unit);
        paths[unit] = path_bytes + (uint64_t)unit * BUSTER_OBJECT_TEST_PATH_BYTES;
    }
    // Freeze both output modes before changing any NameId, so a writer that accidentally emits in intern order
    // cannot make its own changed output the oracle. Every lane count writes both modes from this same baseline.
    Object* baseline = allocated ? BUSTER_ARENA_PUSH_ARRAY(arena, Object, 2 * count) : 0;
    bool ordered = allocated && baseline != 0;
    for (uint32_t mode = 0; ordered && mode < 2; mode += 1)
    {
        for (uint32_t unit = 0; ordered && unit < count; unit += 1)
        {
            baseline[mode * count + unit] = object_build(&program.link, &units, &symbols, unit, (ObjectOptions){ .function_sections = mode != 0 }, source_names[unit], arena);
            ordered = baseline[mode * count + unit].error == OBJECT_ERROR_NONE;
        }
    }
    object_test_expect(&tally, ordered, "original-ID baseline for both section modes", 0);
    for (uint32_t c = 0; ordered && c < 2 * BUSTER_ARRAY_LENGTH(lane_counts); c += 1)
    {
        const uint32_t lanes = lane_counts[c / 2];
        ordered = link_test_permute_names(&program.link, id_order + c, arena);
        object_test_expect(&tally, ordered, "permuted NameIds", c);
        // Preparation caches the atoms' NameIds. Rebuild it from the permuted input before giving it to the writer.
        ordered = ordered && object_prepare(&units, &program.link, options.units + 1, arena);
        object_test_expect(&tally, ordered, "prepared permuted names", c);
        char lanes_directory[BUSTER_OBJECT_TEST_PATH_BYTES];
        snprintf(lanes_directory, sizeof(lanes_directory), "%s/lanes%u-mode%u", directory, lanes, c % 2);
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
            },
            .paths = (const char* const*)paths,
            .source_names = source_names,
            .errors = errors,
        };
        const bool ran = ordered && lane_run(lanes, object_test_lanes_main, &test, 0);
        bool written = ran;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            written = written && test.written[lane];
        }
        object_test_expect(&tally, written, "object_write_units", lanes);
        for (uint32_t unit = 0; written && unit < count; unit += 1)
        {
            const uint64_t before = arena->length;
            const String8 written_bytes = object_test_read_file(arena, paths[unit]);
            const Object expected = baseline[(c % 2) * count + unit];
            object_test_expect(&tally,
                               written_bytes.pointer != 0 && expected.error == OBJECT_ERROR_NONE && written_bytes.length == expected.size &&
                                   memcmp(written_bytes.pointer, expected.bytes, expected.size) == 0,
                               "same object with permuted names and every lane count", (uint64_t)lanes * 1000 + unit);
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
    if (allocated)
    {
        char lanes_directory[BUSTER_OBJECT_TEST_PATH_BYTES];
        snprintf(lanes_directory, sizeof(lanes_directory), "%s/lanes-full", directory);
        object_test_expect(&tally, mkdir(lanes_directory, 0755) == 0, "mkdir", 0);
        for (uint32_t unit = 0; unit < count; unit += 1)
        {
            snprintf(paths[unit], BUSTER_OBJECT_TEST_PATH_BYTES, "%s/u%u.o", lanes_directory, unit);
            errors[unit] = OBJECT_ERROR_NONE;
        }
        ObjectTestLanes test = {
            .link = &program.link,
            .units = &units,
            .paths = (const char* const*)paths,
            .source_names = source_names,
            .errors = errors,
        };
        bool none = lane_run(3, object_test_lanes_full_main, &test, 0);
        for (uint32_t lane = 0; lane < 3; lane += 1)
        {
            none = none && !test.written[lane];
        }
        for (uint32_t unit = 0; unit < count; unit += 1)
        {
            none = none && errors[unit] == OBJECT_ERROR_FULL && access(paths[unit], F_OK) != 0;
        }
        object_test_expect(&tally, none, "a lane without room to start: no object, and every unit's error", 0);
        rmdir(lanes_directory);
    }
    printf("  %u units' objects written by groups of 1, 2, 3 and 8 lanes, the last without atoms\n", count);
    arena_pop_to(arena, length);
    if (generated)
    {
        link_program_release(&program);
    }
    return object_test_report(&tally);
}

static bool object_test_same_file(const char* path, const uint8_t* bytes, uint64_t size, Arena* arena)
{
    const uint64_t length = arena->length;
    const String8 contents = object_test_read_file(arena, path);
    const bool result = contents.pointer != 0 && contents.length == size && memcmp(contents.pointer, bytes, size) == 0;
    arena_pop_to(arena, length);
    return result;
}

static bool object_test_files(const char* directory, Arena* arena)
{
    ObjectTestTally tally = {
        .check = "files",
    };
    char path[BUSTER_OBJECT_TEST_PATH_BYTES];
    char other[BUSTER_OBJECT_TEST_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/file.o", directory);
    snprintf(other, sizeof(other), "%s/other.o", directory);
    static uint8_t large[100000];
    static uint8_t small[1000];
    for (uint64_t i = 0; i < sizeof(large); i += 1)
    {
        large[i] = (uint8_t)(i * 7 + 1);
    }
    for (uint64_t i = 0; i < sizeof(small); i += 1)
    {
        small[i] = (uint8_t)(i * 13 + 5);
    }
    struct stat before;
    struct stat after;
    // A new file, then over it in place: larger, then smaller (truncated), with the same inode.
    object_test_expect(&tally, object_write_file(path, small, sizeof(small)) == OBJECT_ERROR_NONE && object_test_same_file(path, small, sizeof(small), arena), "a new file", 0);
    const bool first_stat = stat(path, &before) == 0;
    object_test_expect(&tally, object_write_file(path, large, sizeof(large)) == OBJECT_ERROR_NONE && object_test_same_file(path, large, sizeof(large), arena),
                       "over a smaller file", 0);
    object_test_expect(&tally, object_write_file(path, small, sizeof(small)) == OBJECT_ERROR_NONE && object_test_same_file(path, small, sizeof(small), arena),
                       "over a larger file, truncated", 0);
    object_test_expect(&tally, first_stat && stat(path, &after) == 0 && after.st_ino == before.st_ino, "in place: the same inode", 0);
    // A file with another link: the other link keeps the old bytes.
    object_test_expect(&tally, link(path, other) == 0, "link", 0);
    object_test_expect(&tally, object_write_file(path, large, sizeof(large)) == OBJECT_ERROR_NONE && object_test_same_file(path, large, sizeof(large), arena) &&
                                   object_test_same_file(other, small, sizeof(small), arena),
                       "over a file with another link", 0);
    object_test_expect(&tally, stat(path, &after) == 0 && after.st_ino != before.st_ino && after.st_nlink == 1 && (after.st_mode & 0777) == (before.st_mode & 0777),
                       "a new inode with the old mode", 0);
    // A file with another link and the longest name a directory takes: the new file beside it has a name of its own.
    // Only where the directory's path leaves room for that name within a path's PATH_MAX bytes.
    char longest[BUSTER_OBJECT_TEST_PATH_BYTES];
    char longest_other[BUSTER_OBJECT_TEST_PATH_BYTES];
    const int prefix = snprintf(longest, sizeof(longest), "%s/", directory);
    if (prefix > 0 && (uint64_t)prefix + NAME_MAX < sizeof(longest))
    {
        memset(longest + prefix, 'n', NAME_MAX);
        longest[prefix + NAME_MAX] = 0;
        snprintf(longest_other, sizeof(longest_other), "%s/longest-other.o", directory);
        const bool longest_linked = object_write_file(longest, small, sizeof(small)) == OBJECT_ERROR_NONE && link(longest, longest_other) == 0;
        object_test_expect(&tally,
                           longest_linked && object_write_file(longest, large, sizeof(large)) == OBJECT_ERROR_NONE &&
                               object_test_same_file(longest, large, sizeof(large), arena) && object_test_same_file(longest_other, small, sizeof(small), arena),
                           "over a file with another link and a name of NAME_MAX bytes", 0);
        unlink(longest);
        unlink(longest_other);
    }
    else
    {
        printf("  %s leaves no room for a name of NAME_MAX bytes: not checked\n", directory);
    }
    // Not a regular file: /dev/null, and a pipe, which takes the bytes in order (a FIFO, with a reader that doesn't
    // wait, and room for them); and no directory.
    object_test_expect(&tally, object_write_file("/dev/null", large, sizeof(large)) == OBJECT_ERROR_NONE, "/dev/null", 0);
    char fifo[BUSTER_OBJECT_TEST_PATH_BYTES];
    snprintf(fifo, sizeof(fifo), "%s/fifo.o", directory);
    const int reader = mkfifo(fifo, 0600) == 0 ? open(fifo, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    const bool piped = reader != -1 && object_write_file(fifo, small, sizeof(small)) == OBJECT_ERROR_NONE;
    uint8_t received[sizeof(small) + 1];
    const ssize_t received_bytes = reader != -1 ? read(reader, received, sizeof(received)) : -1;
    object_test_expect(&tally, piped && received_bytes == (ssize_t)sizeof(small) && memcmp(received, small, sizeof(small)) == 0, "a pipe", 0);
    if (reader != -1)
    {
        close(reader);
    }
    unlink(fifo);
    char missing[BUSTER_OBJECT_TEST_PATH_BYTES];
    snprintf(missing, sizeof(missing), "%s/missing/file.o", directory);
    object_test_expect(&tally, object_write_file(missing, small, sizeof(small)) == OBJECT_ERROR_FILE, "a missing directory", 0);
    unlink(path);
    unlink(other);
    return object_test_report(&tally);
}

// Runs a tool on an object and compares its listing with the object as object_view_open read it.
static bool object_test_listing(ObjectTestTally* tally, const char* tool, const ObjectView* view, const char* object_path, const char* output_path,
                                const char* error_path, Arena* arena)
{
    const uint64_t length = arena->length;
    char* readelf_arguments[] = { "readelf", "-W", "-h", "-S", "-r", "-s", (char*)object_path, 0 };
    char* llvm_arguments[] = { "llvm-readobj", "--file-headers", "--sections", "--relocations", "--symbols", (char*)object_path, 0 };
    const bool readelf = strcmp(tool, "readelf") == 0;
    const int status = object_test_run(readelf ? readelf_arguments : llvm_arguments, output_path, error_path);
    const String8 text = object_test_read_file(arena, output_path);
    const String8 errors = object_test_read_file(arena, error_path);
    ObjectListing listing;
    char error[512] = "";
    bool result = status == 0 && text.pointer != 0 && errors.pointer != 0 && errors.length == 0;
    if (!result)
    {
        printf("  %s %s: status %d%s%.*s\n", tool, object_path, status, errors.length != 0 ? ", " : "", (int)(errors.length < 300 ? errors.length : 300), errors.pointer);
    }
    result = result && (readelf ? readelf_listing(arena, text, &listing, error, sizeof(error)) : llvm_readobj_listing(arena, text, &listing, error, sizeof(error)));
    result = result && object_compare_listing(view, &listing, error, sizeof(error));
    if (error[0] != 0)
    {
        printf("  %s %s: %s\n", tool, object_path, error);
    }
    object_test_expect(tally, result, tool, 0);
    arena_pop_to(arena, length);
    return result;
}

// What a linked program must do: write `length` bytes, `output`, and exit with `status`.
typedef struct ObjectTestExpected ObjectTestExpected;
struct ObjectTestExpected
{
    const uint8_t* output;
    uint64_t length;
    int status;
};

// What a program of link_program_generate's must do: write its output and exit with status 0.
static ObjectTestExpected object_test_program_expected(const LinkProgram* program)
{
    const ObjectTestExpected result = {
        .output = program->output,
        .length = BUSTER_LINK_PROGRAM_OUTPUT_BYTES,
        .status = 0,
    };
    return result;
}

// Writes a program's objects to `directory`, lists each with readelf and llvm-readobj, and links them with each linker
// that `linkers` names (a bit per linker of object_test_linkers) into a program that must do what `expected` says. With
// `harness`, the program is a shared library (LinkProgramOptions.shared): linked by cc -shared, and run by the harness,
// which loads it and calls its entry (object_test_harness).
static bool object_test_cross_check_program(ObjectTestTally* tally, const Link* link, uint32_t unit_count, ObjectTestExpected expected, bool libc, const char* harness,
                                            ObjectOptions options, const char* directory, const char* name, uint32_t linkers, bool keep, Arena* arena)
{
    static const char* const object_test_linkers[] = {
        "ld.bfd",
        "ld.lld",
        "mold",
        "wild",
    };
    // A program that calls libc links through cc, with each linker, three ways.
    static const char* const object_test_fuse_ld[] = {
        "-fuse-ld=bfd",
        "-fuse-ld=lld",
        "-fuse-ld=mold",
        "-fuse-ld=wild",
    };
    static const char* const object_test_libc_ways[] = {
        "-pie",
        "-no-pie",
        "-static",
    };
    const uint64_t length = arena->length;
    const uint64_t failures = tally->failures;
    ObjectUnits units;
    ObjectSymbols symbols;
    const bool prepared = object_prepare(&units, link, unit_count, arena) && object_map_symbols(&symbols, link, arena);
    object_test_expect(tally, prepared, "object_prepare", 0);
    const uint32_t count = prepared ? units.count : 0;
    char** paths = prepared ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)count + 8) : 0;
    char* path_bytes = prepared ? arena_push(arena, ((uint64_t)count + 8) * BUSTER_OBJECT_TEST_PATH_BYTES, 1) : 0;
    const bool allocated = paths != 0 && path_bytes != 0;
    object_test_expect(tally, !prepared || allocated, "room for the paths", 0);
    char output_path[BUSTER_OBJECT_TEST_PATH_BYTES];
    char error_path[BUSTER_OBJECT_TEST_PATH_BYTES];
    snprintf(output_path, sizeof(output_path), "%s/%s.out", directory, name);
    snprintf(error_path, sizeof(error_path), "%s/%s.err", directory, name);
    uint64_t object_bytes = 0;
    for (uint32_t unit = 0; allocated && unit < count; unit += 1)
    {
        paths[unit] = path_bytes + (uint64_t)unit * BUSTER_OBJECT_TEST_PATH_BYTES;
        snprintf(paths[unit], BUSTER_OBJECT_TEST_PATH_BYTES, "%s/%s_u%u.o", directory, name, unit);
        const uint64_t before = arena->length;
        char source[32];
        const Object object = object_build(link, &units, &symbols, unit, options, object_test_source_name(source, sizeof(source), unit), arena);
        const bool written = object.error == OBJECT_ERROR_NONE && object_write_file(paths[unit], object.bytes, object.size) == OBJECT_ERROR_NONE;
        object_test_expect(tally, written, "writing an object", unit);
        ObjectView view;
        char error[512];
        if (written && object_view_open(&view, object.bytes, object.size, false, error, sizeof(error)))
        {
            object_test_listing(tally, "readelf", &view, paths[unit], output_path, error_path, arena);
            object_test_listing(tally, "llvm-readobj", &view, paths[unit], output_path, error_path, arena);
        }
        object_bytes += object.size;
        arena_pop_to(arena, before);
    }
    // Link the objects with each linker, then run the program.
    char program_path[BUSTER_OBJECT_TEST_PATH_BYTES];
    snprintf(program_path, sizeof(program_path), "%s/%s", directory, name);
    char** arguments = allocated ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)count + 8) : 0;
    uint32_t linked = 0;
    const uint32_t ways = libc && harness == 0 ? BUSTER_ARRAY_LENGTH(object_test_libc_ways) : 1;
    for (uint32_t l = 0; arguments != 0 && l < BUSTER_ARRAY_LENGTH(object_test_linkers); l += 1)
    {
        for (uint32_t way = 0; (linkers & (1u << l)) != 0 && way < ways; way += 1)
        {
            uint32_t argument = 0;
            if (libc)
            {
                arguments[argument++] = "cc";
                arguments[argument++] = (char*)object_test_fuse_ld[l];
                arguments[argument++] = harness != 0 ? "-shared" : (char*)object_test_libc_ways[way];
            }
            else
            {
                arguments[argument++] = (char*)object_test_linkers[l];
                arguments[argument++] = "-static";
                arguments[argument++] = "-e";
                arguments[argument++] = "_start";
            }
            arguments[argument++] = "-o";
            arguments[argument++] = program_path;
            for (uint32_t unit = 0; unit < count; unit += 1)
            {
                arguments[argument++] = paths[unit];
            }
            arguments[argument] = 0;
            char linked_by[64];
            snprintf(linked_by, sizeof(linked_by), "%s%s%s", object_test_linkers[l], libc ? " through cc " : "",
                     harness != 0 ? "-shared" : libc ? object_test_libc_ways[way] : "");
            unlink(program_path);
            const int link_status = object_test_run(arguments, output_path, error_path);
            const uint64_t before = arena->length;
            const String8 link_errors = object_test_read_file(arena, error_path);
            const bool link_clean = link_status == 0 && link_errors.pointer != 0 && link_errors.length == 0;
            if (!link_clean)
            {
                printf("  %s: %s exited with %d: %.*s\n", name, linked_by, link_status, link_errors.pointer != 0 ? (int)(link_errors.length < 500 ? link_errors.length : 500) : 0,
                       link_errors.pointer != 0 ? link_errors.pointer : "");
            }
            object_test_expect(tally, link_clean, "a link without errors or warnings", l);
            char* run_arguments[] = { program_path, 0 };
            char* load_arguments[] = { (char*)harness, program_path, 0 };
            const int run_status = link_status == 0 ? object_test_run(harness != 0 ? load_arguments : run_arguments, output_path, error_path) : -1;
            const String8 output = link_status == 0 ? object_test_read_file(arena, output_path) : (String8){ 0 };
            const bool ran = run_status == expected.status && output.pointer != 0 && output.length == expected.length &&
                             (expected.length == 0 || memcmp(output.pointer, expected.output, expected.length) == 0);
            if (link_status == 0 && !ran)
            {
                printf("  %s linked by %s: status %d, output \"%.*s\", expected %d and \"%.*s\"\n", name, linked_by, run_status,
                       output.pointer != 0 ? (int)(output.length < 64 ? output.length : 64) : 0, output.pointer != 0 ? output.pointer : "", expected.status,
                       (int)(expected.length < 64 ? expected.length : 64), expected.length != 0 ? (const char*)expected.output : "");
            }
            object_test_expect(tally, ran, "the program runs and prints what it should", l);
            linked += ran ? 1 : 0;
            arena_pop_to(arena, before);
        }
    }
    printf("  %s%s: %u objects (%llu bytes), listed by readelf and llvm-readobj", name, options.function_sections ? ", function sections" : "", count,
           (unsigned long long)object_bytes);
    if (linkers != 0)
    {
        printf(", linked and run %u times", linked);
    }
    printf("\n");
    if (!keep)
    {
        for (uint32_t unit = 0; allocated && unit < count; unit += 1)
        {
            unlink(paths[unit]);
        }
        unlink(program_path);
        unlink(output_path);
        unlink(error_path);
    }
    arena_pop_to(arena, length);
    return tally->failures == failures;
}

// The bits of object_test_cross_check_program's linkers.
// GNU as's objects of the same units, from link_test_print_program's source. GNU as orders sections as the source first
// names them, always has a .text, a .data and a .bss, adds a .note.gnu.property of the x86 features the code uses where
// binutils is built to (Arch's 2.47 is), writes no .comment or STT_FILE without .ident and .file, and keeps
// only the unnamed symbols (.Lsym_N) that relocations into SHF_MERGE sections need, without a type or a size. For a
// .reloc against a named local symbol it keeps the symbol, where the object writer, as GNU as does for an instruction,
// refers to the section's symbol. So sections and named symbols are matched by name, unnamed ones (.LC and .L) by
// section and value, and a relocation's target by where it points, but exactly where the form matters: the GOT, TLS and
// size types, and into SHF_MERGE sections.

// Where a relocation's symbol and addend point: the symbol's section by name (0 if undefined), its value, its name (0
// for a section symbol or an unnamed one), the addend, whether it is a section symbol and whether its section merges.
typedef struct ObjectTestTarget ObjectTestTarget;
struct ObjectTestTarget
{
    const char* section;
    const char* name;
    uint64_t value;
    int64_t addend;
    bool section_symbol;
    bool merge;
};

// Whether a view's symbol is an unnamed one: a local whose name starts with .L (.LC<n> in the writer's objects,
// .Lsym_<id> in GNU as's).
static bool object_test_unnamed(const ObjectView* view, uint64_t symbol)
{
    const Elf64_Sym* entry = &view->symbols[symbol];
    const char* name = object_view_string(view->symbol_names, view->symbol_names_size, entry->st_name);
    return ELF64_ST_BIND(entry->st_info) == STB_LOCAL && ELF64_ST_TYPE(entry->st_info) != STT_SECTION && name != 0 && strncmp(name, ".L", 2) == 0;
}

static ObjectTestTarget object_test_target(const ObjectView* view, const Elf64_Rela* relocation)
{
    const uint64_t symbol = ELF64_R_SYM(relocation->r_info);
    const Elf64_Sym* entry = &view->symbols[symbol];
    const uint64_t section = object_view_symbol_section(view, symbol);
    const bool defined = entry->st_shndx != SHN_UNDEF;
    const bool section_symbol = ELF64_ST_TYPE(entry->st_info) == STT_SECTION;
    ObjectTestTarget result = {
        .section = defined ? object_view_section_name(view, section) : 0,
        .name = section_symbol || object_test_unnamed(view, symbol) ? 0 : object_view_string(view->symbol_names, view->symbol_names_size, entry->st_name),
        .value = defined ? entry->st_value : 0,
        .addend = relocation->r_addend,
        .section_symbol = section_symbol,
        .merge = defined && (view->sections[section].sh_flags & SHF_MERGE) != 0,
    };
    return result;
}

static bool object_test_same_string(const char* a, const char* b)
{
    return (a == 0) == (b == 0) && (a == 0 || strcmp(a, b) == 0);
}

// Whether two relocations of `type` point at the same target (see above).
static bool object_test_same_target(uint32_t type, ObjectTestTarget a, ObjectTestTarget b)
{
    const bool exact = type == R_X86_64_GOTPCREL || type == R_X86_64_GOTPCRELX || type == R_X86_64_REX_GOTPCRELX || type == R_X86_64_GOTTPOFF ||
                       type == R_X86_64_TPOFF32 || type == R_X86_64_SIZE32 || type == R_X86_64_SIZE64 || a.merge || b.merge;
    bool result = object_test_same_string(a.section, b.section);
    if (a.section == 0)
    {
        result = result && object_test_same_string(a.name, b.name) && a.addend == b.addend;
    }
    else if (exact)
    {
        result = result && a.section_symbol == b.section_symbol && object_test_same_string(a.name, b.name) && a.value == b.value && a.addend == b.addend;
    }
    else
    {
        result = result && a.value + (uint64_t)a.addend == b.value + (uint64_t)b.addend;
    }
    return result;
}

// Whether a view's section is one the comparison matches: not a table of symbols, strings or relocations, nor .comment.
static bool object_test_compared_section(const ObjectView* view, uint64_t index)
{
    const uint32_t type = view->sections[index].sh_type;
    return type != SHT_SYMTAB && type != SHT_STRTAB && type != SHT_RELA && type != SHT_SYMTAB_SHNDX && strcmp(object_view_section_name(view, index), ".comment") != 0;
}

// The index of the compared section named `name`, or with `relocations` of the relocation section for it; 0 if none.
static uint64_t object_test_section_named(const ObjectView* view, const char* name, bool relocations)
{
    uint64_t result = 0;
    for (uint64_t i = 1; result == 0 && i < view->section_count; i += 1)
    {
        const Elf64_Shdr* section = &view->sections[i];
        const bool rela = section->sh_type == SHT_RELA;
        const bool candidate = relocations ? rela && section->sh_info < view->section_count : object_test_compared_section(view, i);
        result = candidate && strcmp(object_view_section_name(view, relocations ? section->sh_info : i), name) == 0 ? i : 0;
    }
    return result;
}

// The index of the symbol (not a section's or a file's) named `name`, or for name 0 of an unnamed one in `section` (by
// name) at `value`; 0 if none.
static uint64_t object_test_symbol(const ObjectView* view, const char* name, const char* section, uint64_t value)
{
    uint64_t result = 0;
    for (uint64_t i = 1; result == 0 && i < view->symbol_count; i += 1)
    {
        const Elf64_Sym* entry = &view->symbols[i];
        const uint32_t type = ELF64_ST_TYPE(entry->st_info);
        bool match = type != STT_SECTION && type != STT_FILE;
        if (match && name != 0)
        {
            match = !object_test_unnamed(view, i) && object_test_same_string(object_view_string(view->symbol_names, view->symbol_names_size, entry->st_name), name);
        }
        else if (match)
        {
            match = object_test_unnamed(view, i) && entry->st_shndx != SHN_UNDEF && entry->st_value == value &&
                    strcmp(object_view_section_name(view, object_view_symbol_section(view, i)), section) == 0;
        }
        result = match ? i : 0;
    }
    return result;
}

// Compares the writer's object of a unit, without function sections, with GNU as's; false, with the first difference
// in `error`, if they differ.
static bool object_test_same_as(const ObjectView* ours, const ObjectView* theirs, char* error, uint64_t error_size)
{
    bool result = true;
    // Every section of ours is GNU as's of the same name, the same; and GNU as has no others, but the empty ones it
    // always has.
    for (uint64_t i = 1; result && i < ours->section_count; i += 1)
    {
        if (object_test_compared_section(ours, i))
        {
            const char* name = object_view_section_name(ours, i);
            const uint64_t j = object_test_section_named(theirs, name, false);
            const Elf64_Shdr* a = &ours->sections[i];
            const Elf64_Shdr* b = j != 0 ? &theirs->sections[j] : 0;
            result = b != 0 && a->sh_type == b->sh_type && a->sh_flags == b->sh_flags && a->sh_size == b->sh_size && a->sh_addralign == b->sh_addralign &&
                     a->sh_entsize == b->sh_entsize && (a->sh_type == SHT_NOBITS || memcmp(ours->file + a->sh_offset, theirs->file + b->sh_offset, a->sh_size) == 0);
            if (!result)
            {
                snprintf(error, error_size, "section %s isn't GNU as's", name);
            }
        }
    }
    for (uint64_t j = 1; result && j < theirs->section_count; j += 1)
    {
        if (object_test_compared_section(theirs, j))
        {
            const char* name = object_view_section_name(theirs, j);
            const bool always = theirs->sections[j].sh_size == 0 && (strcmp(name, ".text") == 0 || strcmp(name, ".data") == 0 || strcmp(name, ".bss") == 0);
            result = always || strcmp(name, ".note.gnu.property") == 0 || object_test_section_named(ours, name, false) != 0;
            if (!result)
            {
                snprintf(error, error_size, "GNU as has a section %s", name);
            }
        }
    }
    // Relocations: for each section, the same offsets, types and targets, in the same order.
    uint64_t relocation_sections = 0;
    for (uint64_t i = 1; result && i < ours->section_count; i += 1)
    {
        const Elf64_Shdr* a = &ours->sections[i];
        if (a->sh_type == SHT_RELA)
        {
            const char* name = object_view_section_name(ours, a->sh_info);
            const uint64_t j = object_test_section_named(theirs, name, true);
            const Elf64_Shdr* b = j != 0 ? &theirs->sections[j] : 0;
            result = b != 0 && a->sh_size == b->sh_size;
            const Elf64_Rela* ours_relocations = (const Elf64_Rela*)(ours->file + a->sh_offset);
            const Elf64_Rela* theirs_relocations = result ? (const Elf64_Rela*)(theirs->file + b->sh_offset) : 0;
            uint64_t r = 0;
            for (; result && r < a->sh_size / sizeof(Elf64_Rela); r += 1)
            {
                const uint32_t type = (uint32_t)ELF64_R_TYPE(ours_relocations[r].r_info);
                result = ours_relocations[r].r_offset == theirs_relocations[r].r_offset && type == ELF64_R_TYPE(theirs_relocations[r].r_info) &&
                         object_test_same_target(type, object_test_target(ours, &ours_relocations[r]), object_test_target(theirs, &theirs_relocations[r]));
            }
            if (!result)
            {
                const uint64_t theirs_count = b != 0 ? b->sh_size / sizeof(Elf64_Rela) : 0;
                const uint64_t ours_count = a->sh_size / sizeof(Elf64_Rela);
                snprintf(error, error_size, "%s's relocations aren't GNU as's: %llu against %llu, or relocation %llu differs", name, (unsigned long long)ours_count,
                         (unsigned long long)theirs_count, (unsigned long long)(r == 0 ? 0 : r - 1));
            }
            relocation_sections += 1;
        }
    }
    for (uint64_t j = 1; result && j < theirs->section_count; j += 1)
    {
        relocation_sections -= theirs->sections[j].sh_type == SHT_RELA ? 1 : 0;
    }
    if (result && relocation_sections != 0)
    {
        snprintf(error, error_size, "GNU as has other relocation sections");
        result = false;
    }
    // Symbols: each of GNU as's, but section and file symbols, is one of ours with the same binding, type, visibility,
    // section, value and size (only the binding, section and value for an unnamed one, which GNU as gives no type or
    // size); and each named one of ours is GNU as's.
    for (uint64_t s = 1; result && s < theirs->symbol_count; s += 1)
    {
        const Elf64_Sym* b = &theirs->symbols[s];
        const uint32_t type = ELF64_ST_TYPE(b->st_info);
        if (type != STT_SECTION && type != STT_FILE)
        {
            const bool unnamed = object_test_unnamed(theirs, s);
            const bool defined = b->st_shndx != SHN_UNDEF;
            const char* name = object_view_string(theirs->symbol_names, theirs->symbol_names_size, b->st_name);
            const char* section = defined ? object_view_section_name(theirs, object_view_symbol_section(theirs, s)) : 0;
            const uint64_t i = object_test_symbol(ours, unnamed ? 0 : name, section, b->st_value);
            const Elf64_Sym* a = i != 0 ? &ours->symbols[i] : 0;
            result = a != 0 && ELF64_ST_BIND(a->st_info) == ELF64_ST_BIND(b->st_info) && (a->st_shndx == SHN_UNDEF) == !defined && a->st_value == b->st_value &&
                     (!defined || strcmp(object_view_section_name(ours, object_view_symbol_section(ours, i)), section) == 0);
            result = result && (unnamed || (ELF64_ST_TYPE(a->st_info) == type && (a->st_other & 3) == (b->st_other & 3) && a->st_size == b->st_size));
            if (!result)
            {
                snprintf(error, error_size, "GNU as's symbol %s isn't the object writer's", name != 0 ? name : "?");
            }
        }
    }
    for (uint64_t s = 1; result && s < ours->symbol_count; s += 1)
    {
        const Elf64_Sym* a = &ours->symbols[s];
        const uint32_t type = ELF64_ST_TYPE(a->st_info);
        if (type != STT_SECTION && type != STT_FILE && !object_test_unnamed(ours, s))
        {
            const char* name = object_view_string(ours->symbol_names, ours->symbol_names_size, a->st_name);
            result = object_test_symbol(theirs, name, 0, 0) != 0;
            if (!result)
            {
                snprintf(error, error_size, "the object writer's symbol %s isn't in GNU as's object", name);
            }
        }
    }
    return result;
}

// Writes a program's units as GNU as source (link_test_print_program) in a directory of `directory` named `name`,
// assembles each with as, and compares GNU as's object with the writer's, without function sections.
static bool object_test_against_as(ObjectTestTally* tally, const LinkProgram* program, const char* directory, const char* name, bool keep, Arena* arena)
{
    const uint64_t length = arena->length;
    const uint64_t failures = tally->failures;
    char source_directory[BUSTER_OBJECT_TEST_PATH_BYTES];
    snprintf(source_directory, sizeof(source_directory), "%s/%s-as", directory, name);
    ObjectUnits units;
    ObjectSymbols symbols;
    const bool prepared = (mkdir(source_directory, 0777) == 0 || errno == EEXIST) && link_test_print_program(program, source_directory) &&
                          object_prepare(&units, &program->link, program->options.units, arena) && object_map_symbols(&symbols, &program->link, arena);
    object_test_expect(tally, prepared, "the program as GNU as source", 0);
    char source_path[BUSTER_OBJECT_TEST_PATH_BYTES + 32];
    char object_path[BUSTER_OBJECT_TEST_PATH_BYTES + 32];
    char output_path[BUSTER_OBJECT_TEST_PATH_BYTES + 32];
    char error_path[BUSTER_OBJECT_TEST_PATH_BYTES + 32];
    snprintf(output_path, sizeof(output_path), "%s/as.out", source_directory);
    snprintf(error_path, sizeof(error_path), "%s/as.err", source_directory);
    const uint32_t count = prepared ? program->options.units : 0;
    for (uint32_t unit = 0; unit < count; unit += 1)
    {
        snprintf(source_path, sizeof(source_path), "%s/unit_%u.s", source_directory, unit);
        snprintf(object_path, sizeof(object_path), "%s/unit_%u.o", source_directory, unit);
        char* arguments[] = { "as", source_path, "-o", object_path, 0 };
        const int status = object_test_run(arguments, output_path, error_path);
        const uint64_t before = arena->length;
        const String8 theirs_file = object_test_read_file(arena, object_path);
        const String8 errors = object_test_read_file(arena, error_path);
        char source[32];
        const ObjectOptions defaults = { 0 };
        const Object object = object_build(&program->link, &units, &symbols, unit, defaults, object_test_source_name(source, sizeof(source), unit), arena);
        ObjectView ours;
        ObjectView theirs;
        char error[512] = "";
        bool same = status == 0 && errors.pointer != 0 && errors.length == 0 && theirs_file.pointer != 0 && object.error == OBJECT_ERROR_NONE;
        if (status != 0 || errors.length != 0)
        {
            snprintf(error, sizeof(error), "as exited with %d: %.*s", status, errors.pointer != 0 ? (int)(errors.length < 300 ? errors.length : 300) : 0,
                     errors.pointer != 0 ? errors.pointer : "");
        }
        same = same && object_view_open(&theirs, (const uint8_t*)theirs_file.pointer, theirs_file.length, true, error, sizeof(error)) &&
               object_view_open(&ours, object.bytes, object.size, false, error, sizeof(error)) && object_test_same_as(&ours, &theirs, error, sizeof(error));
        if (!same)
        {
            printf("  %s, unit %u, against GNU as: %s\n", name, unit, error);
        }
        object_test_expect(tally, same, "an object against GNU as's", unit);
        arena_pop_to(arena, before);
        if (!keep)
        {
            unlink(source_path);
            unlink(object_path);
        }
    }
    printf("  %s: %u objects the same as GNU as's\n", name, tally->failures == failures ? count : 0);
    if (!keep)
    {
        char expected_path[BUSTER_OBJECT_TEST_PATH_BYTES + 32];
        snprintf(expected_path, sizeof(expected_path), "%s/expected", source_directory);
        unlink(expected_path);
        unlink(output_path);
        unlink(error_path);
        rmdir(source_directory);
    }
    arena_pop_to(arena, length);
    return tally->failures == failures;
}

#define BUSTER_OBJECT_TEST_ALL_LINKERS 0xfu
#define BUSTER_OBJECT_TEST_WILD 0x8u

// What runs a shared library of test-link's (LinkProgramOptions.shared): a program that cc builds into `path`, which
// loads the library its first argument names (dlopen) and calls its buster_library_main with main's arguments,
// returning its status (99 if it can't). Whether cc built it.
static bool object_test_harness(const char* directory, char* path, uint64_t capacity, bool keep)
{
    static const char source[] = "#include <dlfcn.h>\n"
                                 "#include <stdio.h>\n"
                                 "int main(int argc, char** argv, char** envp)\n"
                                 "{\n"
                                 "    int result = 99;\n"
                                 "    void* library = argc >= 2 ? dlopen(argv[1], RTLD_NOW) : 0;\n"
                                 "    int (*entry)(int, char**, char**) = library != 0 ? (int (*)(int, char**, char**))dlsym(library, \"buster_library_main\") : 0;\n"
                                 "    if (entry != 0)\n"
                                 "    {\n"
                                 "        result = entry(argc, argv, envp);\n"
                                 "    }\n"
                                 "    else\n"
                                 "    {\n"
                                 "        fprintf(stderr, \"%s\\n\", dlerror());\n"
                                 "    }\n"
                                 "    return result;\n"
                                 "}\n";
    char source_path[BUSTER_OBJECT_TEST_PATH_BYTES];
    char output_path[BUSTER_OBJECT_TEST_PATH_BYTES];
    char error_path[BUSTER_OBJECT_TEST_PATH_BYTES];
    snprintf(source_path, sizeof(source_path), "%s/harness.c", directory);
    snprintf(output_path, sizeof(output_path), "%s/harness.out", directory);
    snprintf(error_path, sizeof(error_path), "%s/harness.err", directory);
    snprintf(path, capacity, "%s/harness", directory);
    FILE* file = fopen(source_path, "w");
    const bool written = file != 0 && fputs(source, file) >= 0;
    if (file != 0)
    {
        fclose(file);
    }
    char* arguments[] = { "cc", "-O1", "-o", path, source_path, 0 };
    const bool built = written && object_test_run(arguments, output_path, error_path) == 0;
    if (!keep)
    {
        unlink(source_path);
        unlink(output_path);
        unlink(error_path);
    }
    return built;
}

static bool object_test_cross_check(uint64_t seed, const char* directory, bool keep, Arena* arena)
{
    ObjectTestTally tally = {
        .check = "cross-check",
    };
    char harness[BUSTER_OBJECT_TEST_PATH_BYTES];
    const bool harnessed = object_test_harness(directory, harness, sizeof(harness), keep);
    object_test_expect(&tally, harnessed, "the harness that loads a shared library, built by cc", 0);
    // test-link's sets, then the shared libraries', linked -shared and run by the harness.
    LinkProgramOptions options[BUSTER_LINK_TEST_ALL_PROGRAMS];
    const char* names[BUSTER_LINK_TEST_ALL_PROGRAMS];
    const char* directories[BUSTER_LINK_TEST_ALL_PROGRAMS];
    link_test_all_options(options, names, directories, seed, BUSTER_OBJECT_TEST_FUNCTIONS);
    for (uint32_t i = 0; i < BUSTER_LINK_TEST_ALL_PROGRAMS; i += 1)
    {
        LinkProgram program;
        const bool generated = link_program_generate(&program, options[i]) && (!options[i].shared || harnessed);
        object_test_expect(&tally, generated, names[i], i);
        // wild 0.10 doesn't link SIZE32 and SIZE64, so it links only the programs without them.
        const uint32_t linkers = options[i].sizes ? BUSTER_OBJECT_TEST_ALL_LINKERS & ~BUSTER_OBJECT_TEST_WILD : BUSTER_OBJECT_TEST_ALL_LINKERS;
        for (uint32_t sections = 0; generated && sections < 2; sections += 1)
        {
            const ObjectOptions object_options = {
                .function_sections = sections == 1,
            };
            char file[64];
            snprintf(file, sizeof(file), "%s%s", directories[i], sections == 1 ? "-sections" : "");
            object_test_cross_check_program(&tally, &program.link, options[i].units, object_test_program_expected(&program), options[i].libc,
                                            options[i].shared ? harness : 0, object_options, directory, file, linkers, keep, arena);
        }
        if (generated)
        {
            object_test_against_as(&tally, &program, directory, directories[i], keep, arena);
        }
        link_program_release(&program);
    }
    if (harnessed && !keep)
    {
        unlink(harness);
    }
    // Past SHN_LORESERVE sections, without sizes so that wild links it too.
    LinkProgramOptions many = link_program_default_options();
    many.seed = seed;
    many.units = 1;
    many.functions = BUSTER_OBJECT_TEST_MANY_FUNCTIONS;
    many.filler = 0;
    many.sizes = false;
    LinkProgram program;
    const bool generated = link_program_generate(&program, many);
    object_test_expect(&tally, generated, "the many-sections program", 0);
    if (generated)
    {
        const ObjectOptions object_options = {
            .function_sections = true,
        };
        object_test_cross_check_program(&tally, &program.link, many.units, object_test_program_expected(&program), false, 0, object_options, directory, "many-sections",
                                        BUSTER_OBJECT_TEST_ALL_LINKERS, keep, arena);
        link_program_release(&program);
    }
    // The link made by hand, listed by the tools but not linked: it has no _start.
    const uint64_t length = arena->length;
    Link hand;
    const bool made = object_test_hand_link(&hand, arena);
    object_test_expect(&tally, made, "the link made by hand", 0);
    const ObjectTestExpected nothing = { 0 };
    for (uint32_t sections = 0; made && sections < 2; sections += 1)
    {
        const ObjectOptions object_options = {
            .function_sections = sections == 1,
        };
        object_test_cross_check_program(&tally, &hand, BUSTER_OBJECT_TEST_HAND_UNITS, nothing, false, 0, object_options, directory,
                                        sections == 1 ? "by-hand-sections" : "by-hand", 0, keep, arena);
    }
    arena_pop_to(arena, length);
    // The program that calls a local IFUNC, through cc by every linker, three ways each.
    Link ifunc;
    const bool ifunc_made = object_test_ifunc_link(&ifunc, arena);
    object_test_expect(&tally, ifunc_made, "the program that calls a local IFUNC", 0);
    const ObjectTestExpected ifunc_expected = {
        .output = 0,
        .length = 0,
        .status = BUSTER_OBJECT_TEST_IFUNC_STATUS,
    };
    for (uint32_t sections = 0; ifunc_made && sections < 2; sections += 1)
    {
        const ObjectOptions object_options = {
            .function_sections = sections == 1,
        };
        object_test_cross_check_program(&tally, &ifunc, 1, ifunc_expected, true, 0, object_options, directory, sections == 1 ? "ifunc-sections" : "ifunc",
                                        BUSTER_OBJECT_TEST_ALL_LINKERS, keep, arena);
    }
    arena_pop_to(arena, length);
    return object_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static bool object_test_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = end != text && *end == 0;
    *value = result ? parsed : *value;
    return result;
}

static int run_object_writer_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t id_order = 153;
    bool cross_check = false;
    bool keep = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && object_test_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--id-order") == 0 && i + 1 < argc)
        {
            valid = valid && object_test_parse_u64(argv[i + 1], &id_order);
            i += 1;
        }
        else if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    char directory[BUSTER_OBJECT_TEST_PATH_BYTES];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-object-writer-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    Arena arena = arena_reserve(BUSTER_OBJECT_TEST_ARENA_BYTES);
    if (!valid)
    {
        fprintf(stderr, "usage: test-object-writer [--seed N] [--id-order N] [--cross-check] [--keep]\n");
    }
    else if (!x86_encoder_initialized && !x86_encoder_initialize())
    {
        fprintf(stderr, "the encoder's templates can't be built\n");
    }
    else if (arena.base == 0 || mkdtemp(directory) == 0)
    {
        fprintf(stderr, "can't reserve the arena or make the directory %s\n", directory);
    }
    else
    {
        bool passed = object_test_objects(seed, &arena);
        passed = object_test_debug(seed, &arena) && passed;
        passed = object_test_many_sections(seed, &arena) && passed;
        passed = object_test_lanes(seed, id_order, directory, keep, &arena) && passed;
        passed = object_test_debug_lanes(seed, directory, keep, &arena) && passed;
        passed = object_test_files(directory, &arena) && passed;
        if (cross_check)
        {
            passed = object_test_cross_check(seed, directory, keep, &arena) && passed;
            passed = object_test_debug_cross_check(seed, directory, keep, &arena) && passed;
        }
        if (keep)
        {
            printf("the files are in %s\n", directory);
        }
        else
        {
            rmdir(directory);
        }
        result = passed ? 0 : 1;
    }
    arena_release(&arena);
    return result;
}
