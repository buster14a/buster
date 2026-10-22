// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The dynamic PIE's tests (executable.c's dynamic mode, with dynamic.c's inputs), for the test-dynamic command:
//
//   build/compiler test-dynamic [--seed N] [--functions N] [--directory PATH] [--cross-check] [--keep]
//
// main.c includes this file after dynamic.c, bind_test.c and executable_test.c, and calls run_dynamic_tests for the
// command. Executables are written into a directory of their own under --directory (TMPDIR, or /tmp, by default),
// which the tests remove unless --keep says to leave it and the last executables there.
//
// Each program is bound against the system's runtime by bind.c and written by executable_write as a dynamic PIE with
// groups of 1, 2, 3 and 8 lanes, each time from zeroed fields over the last file scribbled over (executable_test_reset),
// every other time over poisoned scratch memory: the files must be byte-identical. Each file is then read back and
// checked by the test's own rules (dynamic_test_structure): the headers and .dynamic, .dynsym with its versions and
// .gnu.hash, every relocation the loader needs, from .rela.dyn and from .relr.dyn decoded, with each GOT slot and
// stub, and the unwind tables (test-executable's executable_test_unwind). The writer's exact twins must write the same
// bytes with 1 and 8 lanes, and a write without RELR must have the same relocations in .rela.dyn alone and run alike
// (dynamic_test_variants). With --cross-check, ld.lld links the same link's objects (the object writer's) as a PIE,
// and the two files must have the same dynamic symbols, relocations against them, RELATIVE pointers, init and fini
// arrays and functions in .eh_frame_hdr's table (dynamic_test_against_lld); each program runs again with a library
// preloaded whose write walks the stack with libgcc's unwinder, which must reach _start; and readelf's reading of each
// of the program's functions' call frame rules must be what objdump's disassembly of its pushes and pops gives (see
// "Unwinding").
// The checks, in this order:
//   - Programs: test-link's sets of options that call libc (link_test_program_options), and one of --functions workers
//     (2,000 by default) whose table of pointers has some that the writer's filter takes for symbols outside the image.
//     Each must run under the system's loader, without stand-ins, exiting with status 0 having written
//     LinkProgram.output.
//   - Cases: the atexit program and the linkers' cases of test-bind: each must bind, or not, as GNU ld and lld link it,
//     and run with the status their links exit with.
//   - Exports: definitions of sixteen names that libc.so.6 defines too, which binding exports and main finds with dlsym
//     through the executable's .gnu.hash.
//   - Imports: BUSTER_DYNAMIC_TEST_IMPORTS of libc.so.6's functions, each called through a stub and pointed to, which
//     the loader must bind at their versions.
//   - Refusals: small programs with what a dynamic PIE refuses (a text relocation, an ABS32 to the image, a PC32 to a
//     dynamic symbol, a lea of and a PC64 to a hidden weak reference to nothing, at address 0, and a lea of, a call to
//     and a PC64 to an absolute symbol), refused by every lane count and by the exact twins with their fixup and
//     without an output, or handles (a call to and a pointer to a weak reference to nothing, and to a hidden one, loads
//     through GOT slots whose word starts with that dynamic symbol's, a pointer at an odd address, which .rela.dyn
//     holds, and pointers in .data and .rodata to, a load through the GOT of and an ABS32 to an absolute symbol, which
//     stay its value without a relocation); and lanes short of scratch memory, which must fail the link cleanly.
// The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally, writing, the structure, the variants, unwinding, against lld, the programs and the
// cases, the exports, the imports, the refusals, and run_dynamic_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <elf.h>

// OS-dependent includes
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static const uint32_t dynamic_test_lane_counts[] = { 1, 2, 3, 8 };

// ---------------------------------------------------------------------------------------------------------------------
// The tally

typedef struct DynamicTestTally DynamicTestTally;
struct DynamicTestTally
{
    const char* check;
    uint64_t checked;
    uint64_t failures;
};

static void dynamic_test_expect(DynamicTestTally* tally, bool condition, const char* what, uint64_t index)
{
    tally->checked += 1;
    if (!condition)
    {
        if (tally->failures < 10)
        {
            printf("  %s: %s (%llu)\n", tally->check, what, (unsigned long long)index);
        }
        tally->failures += 1;
    }
}

static bool dynamic_test_report(const DynamicTestTally* tally)
{
    printf("%s: %llu checks, %llu failed\n", tally->check, (unsigned long long)tally->checked, (unsigned long long)tally->failures);
    return tally->failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Writing

// A write of a bound link as a dynamic PIE by a group of lanes. With `poison`, every lane fills the free part of its
// scratch arena (up to BUSTER_DYNAMIC_TEST_POISON_BYTES) with 0xa5 first, so that a byte of the linker's that the
// write doesn't fill shows in the file; with `starve`, lane `starved_lane` leaves executable_write only `room` bytes of
// its scratch arena.
#define BUSTER_DYNAMIC_TEST_POISON_BYTES (64ull << 20)

typedef struct DynamicTestWrite DynamicTestWrite;
struct DynamicTestWrite
{
    Link* link;
    ExecutableOptions options;
    ExecutableResult result;
    // The writer's exact twins, and .rela.dyn alone without RELR.
    bool exact;
    bool rela_only;
    bool poison;
    bool starve;
    uint32_t starved_lane;
    uint64_t room;
    bool succeeded;
};

static void dynamic_test_lane_main(void* argument)
{
    DynamicTestWrite* write = argument;
    Scratch scratch = scratch_begin(0);
    const uint64_t free_bytes = scratch.arena->capacity - scratch.arena->length;
    const uint64_t poisoned = free_bytes < BUSTER_DYNAMIC_TEST_POISON_BYTES ? free_bytes : BUSTER_DYNAMIC_TEST_POISON_BYTES;
    uint8_t* poison = write->poison ? arena_push(scratch.arena, poisoned, 64) : 0;
    if (poison != 0)
    {
        memset(poison, 0xa5, poisoned);
        arena_pop_to(scratch.arena, scratch.length);
    }
    if (write->starve && lane_index() == write->starved_lane && free_bytes > write->room)
    {
        arena_push(scratch.arena, free_bytes - write->room, 1);
    }
    const bool succeeded = executable_write(write->link, &write->options, &write->result);
    scratch_end(scratch);
    if (lane_index() == 0)
    {
        write->succeeded = succeeded;
    }
}

// The soname the tests give their shared libraries (DT_SONAME).
#define BUSTER_DYNAMIC_TEST_SONAME "libbuster-test.so"

// Writes a bound link to `path` with a group of `lanes` lanes, its inputs from `bind` and `runtime`: as a dynamic PIE
// from _start, or against a shared library's runtime (RUNTIME_KIND_SHARED) as a shared library without an entry point,
// whose soname is BUSTER_DYNAMIC_TEST_SONAME: whether executable_write succeeded, and its result. `write` holds the
// poison and starve options.
static bool dynamic_test_write_with(DynamicTestWrite* write, Link* link, const Bind* bind, const Runtime* runtime, String8 path, uint32_t lanes, ExecutableResult* result)
{
    Scratch scratch = scratch_begin(0);
    ExecutableDynamic dynamic;
    const bool library = runtime->kind == RUNTIME_KIND_SHARED;
    const String8 soname = {
        .pointer = BUSTER_DYNAMIC_TEST_SONAME,
        .length = sizeof(BUSTER_DYNAMIC_TEST_SONAME) - 1,
    };
    const bool prepared = library ? dynamic_shared_inputs(&dynamic, runtime, bind, soname, scratch.arena) : dynamic_inputs(&dynamic, runtime, bind, scratch.arena);
    dynamic.relr_version = write->rela_only ? 0 : dynamic.relr_version;
    dynamic.relr = dynamic.relr && !write->rela_only;
    write->link = link;
    write->options = (ExecutableOptions){
        .path = path,
        .entry = library ? 0 : dynamic_global(runtime, bind, "_start"),
        .dynamic = &dynamic,
        .exact = write->exact,
    };
    write->succeeded = false;
    const bool ran = prepared && lane_run(lanes, dynamic_test_lane_main, write, 0);
    *result = write->result;
    scratch_end(scratch);
    return ran && write->succeeded;
}

// Writes the link with every lane count, each from zeroed fields over the last file scribbled over, and each but the
// first over the instructions that the last write relaxed, which a write takes for its own: whether every write
// succeeded with the same bytes and build ID, which `file` then holds, read into `arena`. The link's bytes are then as
// binding left them again (executable_test_save), as the checks and an object written from the link want them.
static bool dynamic_test_lanes(DynamicTestTally* tally, Link* link, const Bind* bind, const Runtime* runtime, String8 path, const char* name, ExecutableResult* result,
                               String8* file, uint64_t id_order, Arena* arena)
{
    bool same = true;
    *file = (String8){ 0 };
    const uint8_t* saved = executable_test_save(link, arena);
    dynamic_test_expect(tally, saved != 0, "room for the atoms' bytes", 0);
    for (uint32_t l = 0; saved != 0 && l < BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts); l += 1)
    {
        const uint32_t lanes = dynamic_test_lane_counts[l];
        const bool permuted = l == 0 || link_test_permute_names(link, id_order + l, arena);
        dynamic_test_expect(tally, permuted, "permuted NameIds", lanes);
        ExecutableResult written = { 0 };
        // Every other write over poisoned scratch memory: the files must still be the same.
        DynamicTestWrite write = {
            .poison = l % 2 == 1,
        };
        const bool reset = executable_test_reset(link, path.pointer);
        const bool succeeded = permuted && reset && dynamic_test_write_with(&write, link, bind, runtime, path, lanes, &written);
        dynamic_test_expect(tally, succeeded, name, lanes);
        if (!succeeded)
        {
            executable_test_print_failure(name, &written);
        }
        const String8 bytes = succeeded ? executable_test_read(arena, path.pointer) : (String8){ 0 };
        const bool identical = l == 0 || (bytes.length == file->length && memcmp(bytes.pointer, file->pointer, bytes.length) == 0 &&
                                          memcmp(written.build_id, result->build_id, BUSTER_EXECUTABLE_BUILD_ID_BYTES) == 0);
        dynamic_test_expect(tally, succeeded && identical, "the same file with every lane count", lanes);
        same = same && succeeded && identical;
        *file = l == 0 ? bytes : *file;
        *result = l == 0 ? written : *result;
    }
    if (saved != 0)
    {
        executable_test_restore(link, saved);
    }
    return same && saved != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The structure
//
// What a dynamic PIE's file says, read back from its headers and its dynamic sections, and compared with the link
// after the write (its atoms' addresses, GOT slots and references) by rules of the test's own: which symbols are
// dynamic, what each needs from the loader and where.

typedef struct DynamicTestImage DynamicTestImage;
struct DynamicTestImage
{
    String8 file;
    const Elf64_Ehdr* header;
    const Elf64_Phdr* program_headers;
    const Elf64_Shdr* section_headers;
    const char* names;
    bool valid;
};

static DynamicTestImage dynamic_test_image(String8 file)
{
    DynamicTestImage result = {
        .file = file,
    };
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file.pointer;
    result.valid = file.length >= sizeof(Elf64_Ehdr) && memcmp(header->e_ident, ELFMAG, SELFMAG) == 0 && header->e_ident[EI_CLASS] == ELFCLASS64 &&
                   header->e_type == ET_DYN && header->e_machine == EM_X86_64 && header->e_phoff + header->e_phnum * sizeof(Elf64_Phdr) <= file.length &&
                   header->e_shoff + header->e_shnum * sizeof(Elf64_Shdr) <= file.length && header->e_shstrndx < header->e_shnum;
    if (result.valid)
    {
        result.header = header;
        result.program_headers = (const Elf64_Phdr*)(file.pointer + header->e_phoff);
        result.section_headers = (const Elf64_Shdr*)(file.pointer + header->e_shoff);
        const Elf64_Shdr* names = &result.section_headers[header->e_shstrndx];
        result.valid = names->sh_offset + names->sh_size <= file.length;
        result.names = file.pointer + names->sh_offset;
    }
    return result;
}

// A section by name with its bytes in the file (NOBITS sections have none), or null.
static const Elf64_Shdr* dynamic_test_section(const DynamicTestImage* image, const char* name)
{
    const Elf64_Shdr* result = 0;
    for (uint32_t i = 1; image->valid && i < image->header->e_shnum; i += 1)
    {
        const Elf64_Shdr* section = &image->section_headers[i];
        const bool inside = section->sh_type == SHT_NOBITS || section->sh_offset + section->sh_size <= image->file.length;
        result = inside && strcmp(image->names + section->sh_name, name) == 0 ? section : result;
    }
    return result;
}

static const uint8_t* dynamic_test_bytes(const DynamicTestImage* image, const Elf64_Shdr* section)
{
    return section != 0 ? (const uint8_t*)image->file.pointer + section->sh_offset : 0;
}

static const Elf64_Phdr* dynamic_test_segment(const DynamicTestImage* image, uint32_t type)
{
    const Elf64_Phdr* result = 0;
    for (uint32_t i = 0; image->valid && i < image->header->e_phnum; i += 1)
    {
        result = result == 0 && image->program_headers[i].p_type == type ? &image->program_headers[i] : result;
    }
    return result;
}

// The value of a .dynamic tag, and whether it is there.
static bool dynamic_test_tag(const DynamicTestImage* image, int64_t tag, uint64_t* value)
{
    const Elf64_Shdr* section = dynamic_test_section(image, ".dynamic");
    const Elf64_Dyn* entries = (const Elf64_Dyn*)dynamic_test_bytes(image, section);
    const uint64_t count = section != 0 ? section->sh_size / sizeof(Elf64_Dyn) : 0;
    bool found = false;
    for (uint64_t i = 0; i < count && entries[i].d_tag != DT_NULL; i += 1)
    {
        *value = !found && entries[i].d_tag == tag ? entries[i].d_un.d_val : *value;
        found = found || entries[i].d_tag == tag;
    }
    return found;
}

// The names of .dynamic's DT_NEEDED entries, in order, up to `capacity` of them: how many there are.
static uint32_t dynamic_test_needed(const DynamicTestImage* image, const char** names, uint32_t capacity)
{
    const Elf64_Shdr* section = dynamic_test_section(image, ".dynamic");
    const Elf64_Shdr* dynstr = dynamic_test_section(image, ".dynstr");
    const Elf64_Dyn* entries = (const Elf64_Dyn*)dynamic_test_bytes(image, section);
    const char* strings = (const char*)dynamic_test_bytes(image, dynstr);
    const uint64_t count = section != 0 ? section->sh_size / sizeof(Elf64_Dyn) : 0;
    uint32_t result = 0;
    for (uint64_t i = 0; strings != 0 && i < count && entries[i].d_tag != DT_NULL; i += 1)
    {
        const bool needed = entries[i].d_tag == DT_NEEDED && entries[i].d_un.d_val < dynstr->sh_size;
        if (needed && result < capacity)
        {
            names[result] = strings + entries[i].d_un.d_val;
        }
        result += needed ? 1 : 0;
    }
    return result;
}

// .gnu.version_r as text, to compare two files': each entry's library and its needed versions' names, sorted (GNU ld
// and lld order them differently), as "library:version,version;", into `text` of `capacity` bytes. False if it doesn't
// fit or the section is malformed.
static bool dynamic_test_version_needs(const DynamicTestImage* image, char* text, uint64_t capacity)
{
    const Elf64_Shdr* section = dynamic_test_section(image, ".gnu.version_r");
    const Elf64_Shdr* dynstr = dynamic_test_section(image, ".dynstr");
    const uint8_t* bytes = dynamic_test_bytes(image, section);
    const char* strings = (const char*)dynamic_test_bytes(image, dynstr);
    const uint64_t size = section != 0 ? section->sh_size : 0;
    uint64_t length = 0;
    bool result = capacity != 0;
    text[0] = 0;
    uint64_t record = 0;
    bool more = bytes != 0 && strings != 0 && size >= sizeof(Elf64_Verneed);
    while (result && more)
    {
        Elf64_Verneed needed;
        memcpy(&needed, bytes + record, sizeof(needed));
        // More than libc.so.6 defines.
        const char* names[128];
        uint32_t count = 0;
        uint64_t at = record + needed.vn_aux;
        for (uint32_t a = 0; result && a < needed.vn_cnt; a += 1)
        {
            Elf64_Vernaux auxiliary;
            result = at + sizeof(auxiliary) <= size && count < BUSTER_ARRAY_LENGTH(names);
            if (result)
            {
                memcpy(&auxiliary, bytes + at, sizeof(auxiliary));
                result = auxiliary.vna_name < dynstr->sh_size;
                names[count] = result ? strings + auxiliary.vna_name : "";
                count += 1;
                at += auxiliary.vna_next;
            }
        }
        // An insertion sort of a library's few names.
        for (uint32_t i = 1; i < count; i += 1)
        {
            const char* name = names[i];
            uint32_t j = i;
            while (j > 0 && strcmp(names[j - 1], name) > 0)
            {
                names[j] = names[j - 1];
                j -= 1;
            }
            names[j] = name;
        }
        result = result && needed.vn_file < dynstr->sh_size;
        const int written = result ? snprintf(text + length, capacity - length, "%s:", strings + needed.vn_file) : -1;
        result = written >= 0 && length + (uint64_t)written < capacity;
        length += result ? (uint64_t)written : 0;
        for (uint32_t i = 0; result && i < count; i += 1)
        {
            const int name_written = snprintf(text + length, capacity - length, "%s%s", names[i], i + 1 < count ? "," : ";");
            result = name_written >= 0 && length + (uint64_t)name_written < capacity;
            length += result ? (uint64_t)name_written : 0;
        }
        more = needed.vn_next != 0 && record + needed.vn_next + sizeof(Elf64_Verneed) <= size;
        record += needed.vn_next;
    }
    return result;
}

// Whether an import of the link's comes from the loader rather than libc.so.6: whether its version is the loader's
// (runtime.c numbers the two libraries' apart).
static bool dynamic_test_from_loader(const Runtime* runtime, const Link* link, SymbolId symbol)
{
    const bool import = link->symbols.source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED;
    return import && import_test_version_name(&runtime->libc, link->symbols.version[symbol]).length == 0 &&
           import_test_version_name(&runtime->loader, link->symbols.version[symbol]).length != 0;
}

// The file offset of an address, from the PT_LOAD whose file bytes have it, or UINT64_MAX.
static uint64_t dynamic_test_file_offset(const DynamicTestImage* image, uint64_t address)
{
    uint64_t result = UINT64_MAX;
    for (uint32_t i = 0; image->valid && i < image->header->e_phnum; i += 1)
    {
        const Elf64_Phdr* segment = &image->program_headers[i];
        const bool inside = segment->p_type == PT_LOAD && address >= segment->p_vaddr && address < segment->p_vaddr + segment->p_filesz;
        result = inside && segment->p_offset + (address - segment->p_vaddr) + 8 <= image->file.length ? segment->p_offset + (address - segment->p_vaddr) : result;
    }
    return result;
}

// .relr.dyn's addresses, decoded as the loader does: an even word an address, an odd one a bitmap of the 63 words after
// the last address's (or the last bitmap's). Into `addresses` (room for `capacity`): how many, or UINT64_MAX if they
// don't fit or a bitmap comes before any address.
static uint64_t dynamic_test_relr(const DynamicTestImage* image, uint64_t* addresses, uint64_t capacity)
{
    const Elf64_Shdr* section = dynamic_test_section(image, ".relr.dyn");
    const uint8_t* bytes = dynamic_test_bytes(image, section);
    const uint64_t words = section != 0 ? section->sh_size / 8 : 0;
    uint64_t result = 0;
    uint64_t where = 0;
    bool started = false;
    for (uint64_t i = 0; result != UINT64_MAX && i < words; i += 1)
    {
        uint64_t word = 0;
        memcpy(&word, bytes + 8 * i, sizeof(word));
        if ((word & 1) == 0)
        {
            result = result < capacity ? result : UINT64_MAX;
            if (result != UINT64_MAX)
            {
                addresses[result] = word;
                result += 1;
            }
            where = word + 8;
            started = true;
        }
        else
        {
            result = started ? result : UINT64_MAX;
            for (uint32_t bit = 1; result != UINT64_MAX && bit < 64; bit += 1)
            {
                if ((word >> bit & 1) != 0)
                {
                    result = result < capacity ? result : UINT64_MAX;
                    if (result != UINT64_MAX)
                    {
                        addresses[result] = where + 8 * (uint64_t)(bit - 1);
                        result += 1;
                    }
                }
            }
            where += 8 * 63;
        }
    }
    return result;
}

// A symbol's address in the image (at base 0), or a TLS symbol's offset in the TLS segment; 0 for one defined nowhere,
// for one of the linker's its reference, and for an absolute one its value.
static uint64_t dynamic_test_address(const Link* link, SymbolId symbol)
{
    const AtomId atom = link->symbols.atom[symbol];
    const uint8_t source = link->symbols.source[symbol];
    const uint64_t address = atom != 0 ? link->atoms.address[atom] + link->symbols.value[symbol] : source == BUSTER_LINK_SOURCE_ABSOLUTE ? link->symbols.value[symbol] : 0;
    return atom == 0 && source == BUSTER_LINK_SOURCE_LINKER ? (uint64_t)(int64_t)(int32_t)link->symbols.reference[symbol] : address;
}

// The test's own rules. A symbol is dynamic, and the loader looks it up, if it is an import or a weak reference to
// nothing with default visibility, a name and a type other than TLS; and in a shared library (`library`), any global or
// weak symbol with default visibility and a name that is a reference to nothing, strong or weak (the loader binds it:
// -z undefs), or a definition, in an atom or absolute (another object the loader finds first may define it too, and
// the library's own references then reach that one: it is preemptible). The loader relocates a dynamic symbol's
// address and a defined one's that isn't TLS (RELATIVE), but not a TLS symbol's offset, a weak reference to nothing
// that isn't dynamic or an absolute symbol's value, which are the executable's to know; and in a shared library, its
// own TLS symbol's offset from the thread pointer once it places the library's TLS block, from the symbol's offset in
// the TLS segment (TPOFF64 without a symbol).
static bool dynamic_test_is_dynamic(const Link* link, SymbolId symbol, bool library)
{
    const uint8_t flags = link->symbols.flags[symbol];
    const uint8_t source = link->symbols.source[symbol];
    const bool nowhere = source == BUSTER_LINK_SOURCE_NONE && link->symbols.atom[symbol] == 0;
    const bool weak = (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    const bool global = (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
    const bool visible = (flags & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_DEFAULT;
    const bool tls = (flags & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
    const bool named = link->symbols.name[symbol] != 0;
    const bool defined = link->symbols.atom[symbol] != 0 || source == BUSTER_LINK_SOURCE_ABSOLUTE;
    return source == BUSTER_LINK_SOURCE_RUNTIME_SHARED || (nowhere && weak && visible && !tls && named) || (library && global && visible && named && (defined || nowhere));
}

// A dynamic symbol that .dynsym has as undefined: an import or a reference to nothing. A shared library's preemptible
// definitions are its exports.
static bool dynamic_test_is_undefined(const Link* link, SymbolId symbol, bool library)
{
    const bool defined = link->symbols.atom[symbol] != 0 || link->symbols.source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE;
    return dynamic_test_is_dynamic(link, symbol, library) && !defined;
}

static bool dynamic_test_is_relative(const Link* link, SymbolId symbol, bool library)
{
    const AtomId atom = link->symbols.atom[symbol];
    const bool tls = atom != 0 ? link_class_is_tls(link->atoms.class[atom]) : (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
    const bool nowhere = link->symbols.source[symbol] == BUSTER_LINK_SOURCE_NONE && atom == 0;
    const bool absolute = link->symbols.source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE;
    return !dynamic_test_is_dynamic(link, symbol, library) && !tls && !nowhere && !absolute;
}

// A shared library's own TLS symbol: defined in an atom of TLS, and not dynamic.
static bool dynamic_test_is_own_tls(const Link* link, SymbolId symbol, bool library)
{
    const AtomId atom = link->symbols.atom[symbol];
    return library && atom != 0 && link_class_is_tls(link->atoms.class[atom]) && !dynamic_test_is_dynamic(link, symbol, library);
}

// A relocation the loader needs: where, its type, its symbol's .dynsym name (or 0) and its addend.
typedef struct DynamicTestRelocation DynamicTestRelocation;
struct DynamicTestRelocation
{
    uint64_t offset;
    uint32_t type;
    SymbolId symbol;
    int64_t addend;
};

static int dynamic_test_by_offset(const void* a, const void* b)
{
    const uint64_t x = ((const DynamicTestRelocation*)a)->offset;
    const uint64_t y = ((const DynamicTestRelocation*)b)->offset;
    return x < y ? -1 : x > y ? 1 : 0;
}

// The GNU hash of a name, h * 33 + c from 5381, and a .gnu.hash lookup of it as the loader does it (glibc's
// do_lookup_x): the Bloom filter's two bits, the bucket's chain until a hash with its low bit set. Returns the .dynsym
// index of the entry named `name`, or 0.
static uint32_t dynamic_test_lookup(const DynamicTestImage* image, const char* name)
{
    uint32_t hash = 5381;
    for (const char* c = name; *c != 0; c += 1)
    {
        hash = hash * 33 + (uint8_t)*c;
    }
    const Elf64_Shdr* section = dynamic_test_section(image, ".gnu.hash");
    const Elf64_Shdr* dynsym_section = dynamic_test_section(image, ".dynsym");
    const Elf64_Shdr* dynstr_section = dynamic_test_section(image, ".dynstr");
    const uint8_t* bytes = dynamic_test_bytes(image, section);
    uint32_t result = 0;
    if (bytes != 0 && dynsym_section != 0 && dynstr_section != 0 && section->sh_size >= 16)
    {
        uint32_t header[4];
        memcpy(header, bytes, sizeof(header));
        const uint32_t buckets = header[0];
        const uint32_t offset = header[1];
        const uint32_t words = header[2];
        const uint32_t shift = header[3];
        const uint64_t symbols = dynsym_section->sh_size / sizeof(Elf64_Sym);
        const bool sized = buckets != 0 && words != 0 && (words & (words - 1)) == 0 && 16 + 8 * (uint64_t)words + 4 * (uint64_t)buckets + 4 * (symbols - offset) == section->sh_size;
        uint64_t word = 0;
        if (sized)
        {
            memcpy(&word, bytes + 16 + 8 * ((hash / 64) & (words - 1)), sizeof(word));
        }
        const bool maybe = sized && (word >> (hash % 64) & 1) != 0 && (word >> ((hash >> shift) % 64) & 1) != 0;
        uint32_t index = 0;
        if (maybe)
        {
            memcpy(&index, bytes + 16 + 8 * (uint64_t)words + 4 * (uint64_t)(hash % buckets), sizeof(index));
        }
        const Elf64_Sym* dynsym = (const Elf64_Sym*)dynamic_test_bytes(image, dynsym_section);
        const char* dynstr = (const char*)dynamic_test_bytes(image, dynstr_section);
        bool last = index == 0 || index < offset;
        while (!last && index < symbols)
        {
            uint32_t chained = 0;
            memcpy(&chained, bytes + 16 + 8 * (uint64_t)words + 4 * (uint64_t)buckets + 4 * (uint64_t)(index - offset), sizeof(chained));
            const bool named = dynsym[index].st_name < dynstr_section->sh_size && strcmp(dynstr + dynsym[index].st_name, name) == 0;
            result = result == 0 && (chained | 1) == (hash | 1) && named ? index : result;
            last = (chained & 1) != 0;
            index += 1;
        }
    }
    return result;
}

// .gnu.hash's structure, as lld writes it: a bucket per four exports (at least one) and the next power of two of
// 64-bit Bloom words above 12 bits per export, with shift 26; the exports from symoffset on, sorted by bucket; each
// bucket's first index, or 0; each export's hash with its low bit set only for the last of its bucket; and both of
// each export's Bloom bits set.
static void dynamic_test_gnu_hash(DynamicTestTally* tally, const DynamicTestImage* image, uint64_t index)
{
    const Elf64_Shdr* section = dynamic_test_section(image, ".gnu.hash");
    const Elf64_Shdr* dynsym_section = dynamic_test_section(image, ".dynsym");
    const Elf64_Shdr* dynstr_section = dynamic_test_section(image, ".dynstr");
    const uint8_t* bytes = dynamic_test_bytes(image, section);
    uint32_t header[4] = { 0 };
    if (bytes != 0 && section->sh_size >= sizeof(header))
    {
        memcpy(header, bytes, sizeof(header));
    }
    const uint64_t symbols = dynsym_section != 0 ? dynsym_section->sh_size / sizeof(Elf64_Sym) : 0;
    const uint64_t exports = header[1] <= symbols ? symbols - header[1] : 0;
    uint64_t words = 1;
    while (exports != 0 && words <= exports * 12 / 64)
    {
        words *= 2;
    }
    const uint32_t buckets = exports / 4 > 1 ? (uint32_t)(exports / 4) : 1;
    const bool sized = bytes != 0 && dynstr_section != 0 && header[0] == buckets && header[2] == words && header[3] == 26 && header[1] >= 1 &&
                       section->sh_size == 16 + 8 * words + 4 * (uint64_t)buckets + 4 * exports;
    dynamic_test_expect(tally, sized, ".gnu.hash sized as lld sizes it", index);
    const Elf64_Sym* dynsym = (const Elf64_Sym*)dynamic_test_bytes(image, dynsym_section);
    const char* dynstr = (const char*)dynamic_test_bytes(image, dynstr_section);
    uint32_t previous = 0;
    for (uint64_t k = 0; sized && k < exports; k += 1)
    {
        const uint64_t entry = header[1] + k;
        const char* name = dynsym[entry].st_name < dynstr_section->sh_size ? dynstr + dynsym[entry].st_name : "";
        uint32_t hash = 5381;
        for (const char* c = name; *c != 0; c += 1)
        {
            hash = hash * 33 + (uint8_t)*c;
        }
        const uint32_t bucket = hash % buckets;
        uint32_t next = UINT32_MAX;
        if (k + 1 < exports)
        {
            const char* next_name = dynsym[entry + 1].st_name < dynstr_section->sh_size ? dynstr + dynsym[entry + 1].st_name : "";
            uint32_t next_hash = 5381;
            for (const char* c = next_name; *c != 0; c += 1)
            {
                next_hash = next_hash * 33 + (uint8_t)*c;
            }
            next = next_hash % buckets;
        }
        uint32_t chained = 0;
        uint32_t first = 0;
        uint64_t word = 0;
        memcpy(&chained, bytes + 16 + 8 * words + 4 * (uint64_t)buckets + 4 * k, sizeof(chained));
        memcpy(&first, bytes + 16 + 8 * words + 4 * (uint64_t)bucket, sizeof(first));
        memcpy(&word, bytes + 16 + 8 * ((hash / 64) & (words - 1)), sizeof(word));
        const bool starts = k == 0 || previous != bucket;
        dynamic_test_expect(tally, k == 0 || previous <= bucket, "exports sorted by bucket", entry);
        dynamic_test_expect(tally, !starts || first == entry, "a bucket's first export", entry);
        dynamic_test_expect(tally, chained == (next != bucket ? hash | 1 : hash & ~1u), "a chain's hash and its end", entry);
        dynamic_test_expect(tally, (word >> (hash % 64) & 1) != 0 && (word >> ((hash >> 26) % 64) & 1) != 0, "both Bloom bits", entry);
        previous = bucket;
    }
    // A bucket without exports is 0.
    for (uint32_t b = 0; sized && b < buckets; b += 1)
    {
        uint32_t first = 0;
        memcpy(&first, bytes + 16 + 8 * words + 4 * (uint64_t)b, sizeof(first));
        bool used = false;
        for (uint64_t k = 0; k < exports; k += 1)
        {
            const char* name = dynsym[header[1] + k].st_name < dynstr_section->sh_size ? dynstr + dynsym[header[1] + k].st_name : "";
            uint32_t hash = 5381;
            for (const char* c = name; *c != 0; c += 1)
            {
                hash = hash * 33 + (uint8_t)*c;
            }
            used = used || hash % buckets == b;
        }
        dynamic_test_expect(tally, used || first == 0, "an empty bucket 0", b);
    }
}

// Every dynamic symbol of the link once in .dynsym, undefined, at its binding and type, an import at its version and a
// weak reference to nothing unversioned, before the exports; each export defined at its address, size and section,
// and found through .gnu.hash, which finds no undefined one. Fills `indices` (room for every symbol) with each one's
// .dynsym index.
static void dynamic_test_symbols(DynamicTestTally* tally, const Link* link, const Bind* bind, const Runtime* runtime, const DynamicTestImage* image, uint32_t* indices,
                                 uint64_t index)
{
    const Elf64_Shdr* dynsym_section = dynamic_test_section(image, ".dynsym");
    const Elf64_Shdr* dynstr_section = dynamic_test_section(image, ".dynstr");
    const Elf64_Shdr* versym_section = dynamic_test_section(image, ".gnu.version");
    const Elf64_Shdr* verneed_section = dynamic_test_section(image, ".gnu.version_r");
    const Elf64_Sym* dynsym = (const Elf64_Sym*)dynamic_test_bytes(image, dynsym_section);
    const char* dynstr = (const char*)dynamic_test_bytes(image, dynstr_section);
    const Elf64_Half* versym = (const Elf64_Half*)dynamic_test_bytes(image, versym_section);
    const uint64_t count = dynsym_section != 0 ? dynsym_section->sh_size / sizeof(Elf64_Sym) : 0;
    const bool tables = dynsym != 0 && dynstr != 0 && count >= 1 && dynstr[0] == 0 && dynstr[dynstr_section->sh_size - 1] == 0 &&
                        (versym == 0 || versym_section->sh_size == count * sizeof(Elf64_Half));
    dynamic_test_expect(tally, tables, ".dynsym and .dynstr", index);
    const Elf64_Sym none = { 0 };
    dynamic_test_expect(tally, tables && memcmp(&dynsym[0], &none, sizeof(none)) == 0 && (versym == 0 || versym[0] == 0), "the null symbol", index);
    memset(indices, 0, link->symbols.count * sizeof(uint32_t));
    // Each entry's symbol, by name: the link's one symbol of each global name.
    const bool shared_library = runtime->kind == RUNTIME_KIND_SHARED;
    uint64_t undefined = 0;
    for (SymbolId symbol = 1; tables && symbol < link->symbols.count; symbol += 1)
    {
        undefined += dynamic_test_is_undefined(link, symbol, shared_library) ? 1 : 0;
    }
    for (uint64_t i = 1; tables && i < count; i += 1)
    {
        const Elf64_Sym* entry = &dynsym[i];
        const char* name = entry->st_name < dynstr_section->sh_size ? dynstr + entry->st_name : "";
        SymbolId found = 0;
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            const NameId symbol_name = link->symbols.name[symbol];
            const bool global = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
            found = found == 0 && global && symbol_name != 0 && strlen(name) == link->names.length[symbol_name] &&
                            memcmp(name, link->names.bytes[symbol_name], link->names.length[symbol_name]) == 0
                        ? symbol
                        : found;
        }
        const bool fresh = found != 0 && indices[found] == 0;
        dynamic_test_expect(tally, fresh, "each entry a symbol of the link's, once", i);
        indices[found] = fresh ? (uint32_t)i : indices[found];
        const uint8_t flags = found != 0 ? link->symbols.flags[found] : 0;
        const bool kind = entry->st_info == (flags & (BUSTER_LINK_SYMBOL_BINDING | BUSTER_LINK_SYMBOL_TYPE)) && entry->st_other == flags >> 6;
        if (i <= undefined)
        {
            // An import at its export's version, through .gnu.version_r's entry for its library (libc.so.6, or the
            // loader); a weak reference to nothing unversioned (0).
            const bool import = found != 0 && link->symbols.source[found] == BUSTER_LINK_SOURCE_RUNTIME_SHARED;
            const String8 expected = import ? import_test_runtime_version_name(runtime, link->symbols.version[found]) : (String8){ 0 };
            const String8 library = found != 0 && dynamic_test_from_loader(runtime, link, found) ? runtime->loader.soname : runtime->libc.soname;
            String8 version = { 0 };
            String8 file = { 0 };
            const uint16_t number = versym != 0 ? versym[i] : 0;
            const uint8_t* verneed = dynamic_test_bytes(image, verneed_section);
            const uint64_t verneed_bytes = verneed_section != 0 ? verneed_section->sh_size : 0;
            uint64_t record = 0;
            bool more = verneed != 0 && verneed_bytes >= sizeof(Elf64_Verneed);
            while (more)
            {
                Elf64_Verneed needed;
                memcpy(&needed, verneed + record, sizeof(needed));
                uint64_t at = record + needed.vn_aux;
                for (uint32_t a = 0; a < needed.vn_cnt && at + sizeof(Elf64_Vernaux) <= verneed_bytes; a += 1)
                {
                    Elf64_Vernaux auxiliary;
                    memcpy(&auxiliary, verneed + at, sizeof(auxiliary));
                    if (auxiliary.vna_other == number && auxiliary.vna_name < dynstr_section->sh_size && needed.vn_file < dynstr_section->sh_size)
                    {
                        version = (String8){
                            .pointer = (char*)dynstr + auxiliary.vna_name,
                            .length = strlen(dynstr + auxiliary.vna_name),
                        };
                        file = (String8){
                            .pointer = (char*)dynstr + needed.vn_file,
                            .length = strlen(dynstr + needed.vn_file),
                        };
                    }
                    at += auxiliary.vna_next;
                }
                more = needed.vn_next != 0 && record + needed.vn_next + sizeof(Elf64_Verneed) <= verneed_bytes;
                record += needed.vn_next;
            }
            const bool versioned = import ? version.length == expected.length && version.length != 0 && memcmp(version.pointer, expected.pointer, version.length) == 0 &&
                                                file.length == library.length && memcmp(file.pointer, library.pointer, file.length) == 0
                                          : number == VER_NDX_LOCAL;
            dynamic_test_expect(tally, found != 0 && dynamic_test_is_undefined(link, found, shared_library), "an undefined entry a dynamic symbol's", i);
            dynamic_test_expect(tally, kind && entry->st_shndx == SHN_UNDEF && entry->st_value == 0 && entry->st_size == 0, "an undefined entry as its symbol is", i);
            dynamic_test_expect(tally, versioned, "an import at its export's version", i);
            dynamic_test_expect(tally, dynamic_test_lookup(image, name) == 0, ".gnu.hash without the undefined", i);
        }
        else
        {
            bool exported = false;
            for (uint32_t e = 0; e < bind->export_count; e += 1)
            {
                exported = exported || bind->exports[e] == found;
            }
            const AtomId atom = found != 0 ? link->symbols.atom[found] : 0;
            const Elf64_Shdr* section = entry->st_shndx < image->header->e_shnum ? &image->section_headers[entry->st_shndx] : 0;
            // A TLS symbol's value is its offset in the TLS segment, in its section.
            const Elf64_Phdr* tls_segment = dynamic_test_segment(image, PT_TLS);
            const bool tls = atom != 0 && link_class_is_tls(link->atoms.class[atom]);
            const uint64_t address = found != 0 ? dynamic_test_address(link, found) : 0;
            const uint64_t value = !tls ? address : tls_segment != 0 ? address - tls_segment->p_vaddr : UINT64_MAX;
            const bool placed = atom != 0 && section != 0 && entry->st_value == value && entry->st_size == link->symbols.size[found] && address >= section->sh_addr &&
                                address <= section->sh_addr + section->sh_size;
            dynamic_test_expect(tally, exported, "a defined entry an export of binding's", i);
            dynamic_test_expect(tally, kind && placed, "an export at its address, size and section", i);
            dynamic_test_expect(tally, versym == 0 || versym[i] == VER_NDX_GLOBAL, "an export unversioned", i);
            dynamic_test_expect(tally, dynamic_test_lookup(image, name) == i, ".gnu.hash finds the export", i);
        }
    }
    dynamic_test_expect(tally, count == 1 + undefined + bind->export_count, "every dynamic symbol and export in .dynsym", index);
}

// .rela.dyn and .relr.dyn against the relocations the test's rules ask for: one for each GOT slot of a dynamic or
// relocated symbol (GLOB_DAT, TPOFF64 for a TLS one, or RELATIVE to the symbol's address), and in a shared library
// (`library`) of its own TLS symbol (TPOFF64 without a symbol, its offset in the TLS segment as the addend), and for
// each ABS64 field of a written atom whose target is one (R_X86_64_64 with the fixup's addend, or RELATIVE to the
// target's address plus it): with `relr`, a RELATIVE one of a slot, or of a field at an even address of an atom aligned
// to 2 or more, is .relr.dyn's, whose addend is the slot's or the field's bytes; the others are .rela.dyn's, RELATIVE
// ones first, as many as DT_RELACOUNT says; each GOT slot holding the address at base 0 that its RELATIVE addend
// repeats, 0 for a dynamic symbol, a TLS symbol's offset from the thread pointer, or in a shared library its own TLS
// symbol's offset in the TLS segment, which its TPOFF64 addend repeats. Every CALL to a dynamic symbol reaches its
// stub, jmp [rip + its GOT slot].
static void dynamic_test_relocations(DynamicTestTally* tally, const Link* link, const DynamicTestImage* image, const uint32_t* indices, bool relr, bool library,
                                     Arena* arena, uint64_t index)
{
    const Elf64_Phdr* tls_segment = dynamic_test_segment(image, PT_TLS);
    const Elf64_Shdr* rela_section = dynamic_test_section(image, ".rela.dyn");
    const Elf64_Shdr* got_section = dynamic_test_section(image, ".got");
    const Elf64_Shdr* stubs_section = dynamic_test_section(image, ".plt.got");
    const Elf64_Rela* rela = (const Elf64_Rela*)dynamic_test_bytes(image, rela_section);
    const uint8_t* got = dynamic_test_bytes(image, got_section);
    const uint64_t count = rela_section != 0 ? rela_section->sh_size / sizeof(Elf64_Rela) : 0;
    const LinkAtoms* atoms = &link->atoms;
    const LinkFixups* fixups = &link->fixups;
    uint64_t capacity = link->symbols.count + fixups->count;
    DynamicTestRelocation* expected = BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestRelocation, capacity);
    DynamicTestRelocation* actual = BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestRelocation, count + fixups->count + 1);
    uint64_t* relr_addresses = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, fixups->count + 1);
    dynamic_test_expect(tally, expected != 0 && actual != 0 && relr_addresses != 0, "room for the relocations", index);
    uint64_t wanted = 0;
    for (SymbolId symbol = 1; expected != 0 && symbol < link->symbols.count; symbol += 1)
    {
        const uint32_t slot = link->symbols.got_slot[symbol];
        const bool dynamic = dynamic_test_is_dynamic(link, symbol, library);
        const bool relative = dynamic_test_is_relative(link, symbol, library);
        const bool own_tls = dynamic_test_is_own_tls(link, symbol, library);
        const bool tls = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
        // A shared library's own TLS symbol's offset in the TLS segment, from the segment's address.
        const int64_t tls_offset = own_tls && tls_segment != 0 ? (int64_t)(dynamic_test_address(link, symbol) - tls_segment->p_vaddr) : 0;
        if (slot != 0 && got_section != 0 && (dynamic || relative || own_tls))
        {
            // With RELR, a RELATIVE slot is .relr.dyn's, its address in the slot.
            expected[wanted] = (DynamicTestRelocation){
                .offset = got_section->sh_addr + 8 * (uint64_t)slot,
                .type = dynamic ? (tls ? R_X86_64_TPOFF64 : R_X86_64_GLOB_DAT) : own_tls ? R_X86_64_TPOFF64 : relr ? 0 : R_X86_64_RELATIVE,
                .symbol = dynamic ? symbol : 0,
                .addend = dynamic ? 0 : own_tls ? tls_offset : (int64_t)dynamic_test_address(link, symbol),
            };
            wanted += 1;
        }
        // The slot's own bytes: the RELATIVE addend, 0 for a dynamic symbol, an absolute symbol's value, a TLS offset,
        // which the patch's references give, or in a shared library its own TLS symbol's TPOFF64 addend.
        int64_t value = 0;
        const bool absolute = link->symbols.source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE;
        const int64_t expected_value = dynamic                ? 0
                                       : own_tls              ? tls_offset
                                       : relative || absolute ? (int64_t)dynamic_test_address(link, symbol)
                                                              : (int64_t)(int32_t)link->symbols.reference[symbol];
        if (slot != 0 && got != 0 && 8 * (uint64_t)slot + 8 <= got_section->sh_size)
        {
            memcpy(&value, got + 8 * (uint64_t)slot, sizeof(value));
        }
        dynamic_test_expect(tally, slot == 0 || value == expected_value, "a GOT slot's bytes", symbol);
    }
    for (AtomId atom = 1; expected != 0 && atom < atoms->count; atom += 1)
    {
        const uint8_t class = atoms->class[atom];
        const bool written = class == BUSTER_LINK_CLASS_TDATA || class == BUSTER_LINK_CLASS_INIT_ARRAY || class == BUSTER_LINK_CLASS_FINI_ARRAY ||
                             class == BUSTER_LINK_CLASS_DATA_RELRO || class == BUSTER_LINK_CLASS_DATA;
        const FixupId begin = atoms->fixup_begin[atom];
        for (FixupId fixup = begin; written && fixup < begin + atoms->fixup_count[atom]; fixup += 1)
        {
            const SymbolId target = fixups->target[fixup];
            const bool abs64 = (fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_ABS64;
            const bool dynamic = dynamic_test_is_dynamic(link, target, library);
            if (abs64 && (dynamic || dynamic_test_is_relative(link, target, library)))
            {
                const int64_t addend = link_fixup_addend(link, fixup);
                // RELR's (marked with type 0 here) take their addends from the fields.
                const bool packed = relr && !dynamic && atoms->alignment_log2[atom] >= 1 && fixups->offset[fixup] % 2 == 0;
                expected[wanted] = (DynamicTestRelocation){
                    .offset = atoms->address[atom] + fixups->offset[fixup],
                    .type = dynamic ? R_X86_64_64 : packed ? 0 : R_X86_64_RELATIVE,
                    .symbol = dynamic ? target : 0,
                    .addend = dynamic ? addend : (int64_t)dynamic_test_address(link, target) + addend,
                };
                wanted += 1;
            }
        }
    }
    // The file's, each symbol by its .dynsym index, RELATIVE ones first.
    uint64_t relative_count = 0;
    dynamic_test_expect(tally, dynamic_test_tag(image, DT_RELACOUNT, &relative_count) == (relative_count != 0 || count == 0) || count == 0, "DT_RELACOUNT", index);
    bool ordered = true;
    for (uint64_t i = 0; actual != 0 && i < count; i += 1)
    {
        const uint32_t type = (uint32_t)ELF64_R_TYPE(rela[i].r_info);
        const uint32_t dynamic_index = (uint32_t)ELF64_R_SYM(rela[i].r_info);
        SymbolId symbol = 0;
        for (SymbolId s = 1; dynamic_index != 0 && s < link->symbols.count; s += 1)
        {
            symbol = indices[s] == dynamic_index ? s : symbol;
        }
        actual[i] = (DynamicTestRelocation){
            .offset = rela[i].r_offset,
            .type = type,
            .symbol = symbol,
            .addend = rela[i].r_addend,
        };
        // Without a symbol: the RELATIVE ones, and a shared library's TPOFF64 ones of its own TLS symbols.
        ordered = ordered && (type == R_X86_64_RELATIVE) == (i < relative_count) &&
                  (dynamic_index == 0) == (type == R_X86_64_RELATIVE || (library && type == R_X86_64_TPOFF64 && symbol == 0));
    }
    dynamic_test_expect(tally, ordered, "RELATIVE relocations first, as many as DT_RELACOUNT", index);
    // .relr.dyn's, in increasing order, each with its field's bytes as its addend.
    const uint64_t relr_count = relr_addresses != 0 ? dynamic_test_relr(image, relr_addresses, fixups->count + 1) : UINT64_MAX;
    dynamic_test_expect(tally, relr_count != UINT64_MAX && (relr || relr_count == 0), ".relr.dyn read", relr_count);
    bool increasing = true;
    for (uint64_t i = 0; relr_count != UINT64_MAX && actual != 0 && i < relr_count; i += 1)
    {
        const uint64_t file_offset = dynamic_test_file_offset(image, relr_addresses[i]);
        int64_t field = 0;
        if (file_offset != UINT64_MAX)
        {
            memcpy(&field, image->file.pointer + file_offset, sizeof(field));
        }
        increasing = increasing && (i == 0 || relr_addresses[i] > relr_addresses[i - 1]) && file_offset != UINT64_MAX;
        actual[count + i] = (DynamicTestRelocation){
            .offset = relr_addresses[i],
            .type = 0,
            .symbol = 0,
            .addend = field,
        };
    }
    dynamic_test_expect(tally, increasing, ".relr.dyn's addresses increasing, each in the file", index);
    uint64_t relr_tag = 0;
    const Elf64_Shdr* relr_section = dynamic_test_section(image, ".relr.dyn");
    const bool relr_tagged = dynamic_test_tag(image, DT_RELR, &relr_tag);
    dynamic_test_expect(tally, relr_section != 0 ? relr_tagged && relr_tag == relr_section->sh_addr : !relr_tagged, "DT_RELR", index);
    const uint64_t total = count + (relr_count != UINT64_MAX ? relr_count : 0);
    bool same = expected != 0 && actual != 0 && relr_count != UINT64_MAX && wanted == total;
    if (same)
    {
        qsort(expected, wanted, sizeof(*expected), dynamic_test_by_offset);
        qsort(actual, total, sizeof(*actual), dynamic_test_by_offset);
        same = memcmp(expected, actual, total * sizeof(*expected)) == 0;
    }
    dynamic_test_expect(tally, same, "the relocations the loader needs, and no others", index);

    // Each CALL to a dynamic symbol reaches a stub that jumps through the symbol's slot.
    const uint8_t* stubs = dynamic_test_bytes(image, stubs_section);
    for (AtomId atom = 1; atom < atoms->count; atom += 1)
    {
        const FixupId begin = atoms->fixup_begin[atom];
        for (FixupId fixup = begin; fixup < begin + atoms->fixup_count[atom]; fixup += 1)
        {
            const SymbolId target = fixups->target[fixup];
            if ((fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_CALL && dynamic_test_is_dynamic(link, target, library))
            {
                // The field from the file: the link's bytes are binding's again (dynamic_test_lanes).
                const uint64_t place = atoms->address[atom] + fixups->offset[fixup];
                const uint64_t field = dynamic_test_file_offset(image, place);
                int32_t distance = 0;
                if (field != UINT64_MAX && field + sizeof(distance) <= image->file.length)
                {
                    memcpy(&distance, image->file.pointer + field, sizeof(distance));
                }
                const uint64_t stub = place + (uint64_t)(int64_t)distance - (uint64_t)link_fixup_addend(link, fixup);
                const bool inside = stubs != 0 && stub >= stubs_section->sh_addr && stub + BUSTER_EXECUTABLE_STUB_BYTES <= stubs_section->sh_addr + stubs_section->sh_size &&
                                    (stub - stubs_section->sh_addr) % BUSTER_EXECUTABLE_STUB_BYTES == 0;
                const uint8_t* bytes = inside ? stubs + (stub - stubs_section->sh_addr) : 0;
                int32_t slot_distance = 0;
                if (bytes != 0)
                {
                    memcpy(&slot_distance, bytes + 2, sizeof(slot_distance));
                }
                const uint64_t slot = stub + 6 + (uint64_t)(int64_t)slot_distance;
                const bool jumps = bytes != 0 && bytes[0] == 0xff && bytes[1] == 0x25 && bytes[6] == 0x66 && bytes[7] == 0x90 && got_section != 0 &&
                                   slot == got_section->sh_addr + 8 * (uint64_t)link->symbols.got_slot[target] && link->symbols.got_slot[target] != 0;
                dynamic_test_expect(tally, jumps, "a call to a dynamic symbol through its stub and slot", fixup);
            }
        }
    }
}

// The loads and calls through the GOT, by the test's own rule (executable_test_relaxes, which in a shared library
// leaves its dynamic symbols out: another object may define them): each that relaxes rewritten in the file
// (executable_test_relaxed), and each other keeping its instruction; and a GOT slot for each symbol that a fixup reads
// unrelaxed or that a call reaches as a dynamic symbol (through its stub, which reads the slot), and for no other,
// numbered from 1 in symbol order, after slot 0, which holds 0. Returns how many relaxed.
static uint64_t dynamic_test_got(DynamicTestTally* tally, const Link* link, const DynamicTestImage* image, bool library, Arena* arena, uint64_t index)
{
    const LinkAtoms* atoms = &link->atoms;
    const LinkFixups* fixups = &link->fixups;
    const uint64_t length = arena->length;
    uint8_t* targets = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, link->symbols.count);
    dynamic_test_expect(tally, targets != 0, "room for the GOT's targets", index);
    uint64_t relaxed = 0;
    uint64_t mismatched = 0;
    FixupId first = 0;
    if (targets != 0)
    {
        memset(targets, 0, link->symbols.count);
    }
    for (AtomId atom = 1; targets != 0 && atom < atoms->count; atom += 1)
    {
        const FixupId begin = atoms->fixup_begin[atom];
        const uint64_t atom_offset = dynamic_test_file_offset(image, atoms->address[atom]);
        for (FixupId fixup = begin; fixup < begin + atoms->fixup_count[atom]; fixup += 1)
        {
            const uint8_t kind = fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            const SymbolId target = fixups->target[fixup];
            const bool reads = kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL || kind == BUSTER_LINK_FIXUP_GOTTPOFF;
            const bool dynamic = dynamic_test_is_dynamic(link, target, library);
            const bool relaxes = executable_test_relaxes(link, fixup) && !(library && dynamic);
            const bool stub = kind == BUSTER_LINK_FIXUP_CALL && dynamic;
            targets[target] |= (reads && !relaxes) || stub ? 1 : 0;
            const uint64_t offset = fixups->offset[fixup];
            const bool readable = atom_offset != UINT64_MAX && atom_offset + offset + 4 <= image->file.length;
            const uint8_t* bytes = readable ? (const uint8_t*)image->file.pointer + atom_offset : 0;
            uint8_t expected[6];
            const bool same = relaxes ? readable && executable_test_relaxed(link, atom, fixup, dynamic_test_address(link, target), atoms->address[atom] + offset, expected) &&
                                            memcmp(bytes + offset - 2, expected, sizeof(expected)) == 0
                                      : !reads || (readable && executable_test_unrelaxed(kind, bytes, offset));
            first = !same && mismatched == 0 ? fixup : first;
            mismatched += same ? 0 : 1;
            relaxed += relaxes ? 1 : 0;
        }
    }
    dynamic_test_expect(tally, mismatched == 0, "each load and call through the GOT relaxed by the test's rule, and each other kept", first);
    const Elf64_Shdr* got_section = dynamic_test_section(image, ".got");
    const uint8_t* got = dynamic_test_bytes(image, got_section);
    uint32_t next = 1;
    SymbolId wrong = 0;
    for (SymbolId symbol = 1; targets != 0 && symbol < link->symbols.count; symbol += 1)
    {
        const uint32_t slot = link->symbols.got_slot[symbol];
        const bool right = (slot != 0) == (targets[symbol] != 0) && (slot == 0 || slot == next);
        wrong = !right && wrong == 0 ? symbol : wrong;
        next += slot != 0 ? 1 : 0;
    }
    uint64_t zero = UINT64_MAX;
    if (got != 0 && got_section->sh_size >= 8)
    {
        memcpy(&zero, got, sizeof(zero));
    }
    const bool sized = got_section == 0 ? next == 1 : got_section->sh_size == 8 * (uint64_t)next && zero == 0;
    dynamic_test_expect(tally, targets != 0 && wrong == 0 && sized, "a GOT slot for each symbol an unrelaxed GOT kind or a dynamic call reaches, in symbol order", wrong);
    arena_pop_to(arena, length);
    return relaxed;
}

// The headers and .dynamic: ET_DYN from _start, PT_INTERP the interpreter, PT_DYNAMIC .dynamic; DT_NEEDED libc.so.6,
// then the loader exactly when an import is its, as GNU ld and lld take libc.so's AS_NEEDED, and DT_VERNEEDNUM those
// with a needed version; and the tags lld writes, each with its section's address or size, DT_INIT and DT_FINI crti.o's
// functions, and DT_FLAGS_1 PIE. A shared library (bound against RUNTIME_KIND_SHARED) instead has no entry point (0) or
// PT_INTERP, no DT_FLAGS_1 or DT_DEBUG, its soname in DT_SONAME, and DT_FLAGS DF_STATIC_TLS exactly when it has an
// initial-exec access (GOTTPOFF), as lld writes them.
static void dynamic_test_headers(DynamicTestTally* tally, const Link* link, const Bind* bind, const Runtime* runtime, const DynamicTestImage* image, uint64_t index)
{
    dynamic_test_expect(tally, image->valid, "a PIE's ELF header", index);
    const bool library = runtime->kind == RUNTIME_KIND_SHARED;
    if (image->valid)
    {
        const SymbolId start = dynamic_global(runtime, bind, "_start");
        dynamic_test_expect(tally, library ? start == 0 && image->header->e_entry == 0 : image->header->e_entry == dynamic_test_address(link, start), "the entry point _start",
                            index);
        const Elf64_Phdr* interp = dynamic_test_segment(image, PT_INTERP);
        const char interpreter[] = BUSTER_DYNAMIC_INTERPRETER;
        dynamic_test_expect(tally,
                            library ? interp == 0
                                    : interp != 0 && interp->p_filesz == sizeof(interpreter) && interp->p_offset + sizeof(interpreter) <= image->file.length &&
                                          memcmp(image->file.pointer + interp->p_offset, interpreter, sizeof(interpreter)) == 0,
                            "PT_INTERP", index);
        const Elf64_Phdr* dynamic = dynamic_test_segment(image, PT_DYNAMIC);
        const Elf64_Shdr* dynamic_section = dynamic_test_section(image, ".dynamic");
        dynamic_test_expect(tally, dynamic != 0 && dynamic_section != 0 && dynamic->p_vaddr == dynamic_section->sh_addr && dynamic->p_filesz == dynamic_section->sh_size,
                            "PT_DYNAMIC", index);
        const struct
        {
            int64_t tag;
            const char* section;
            bool size;
        } tables[] = {
            { DT_SYMTAB, ".dynsym", false },
            { DT_STRTAB, ".dynstr", false },
            { DT_STRSZ, ".dynstr", true },
            { DT_GNU_HASH, ".gnu.hash", false },
            { DT_RELA, ".rela.dyn", false },
            { DT_RELASZ, ".rela.dyn", true },
            { DT_VERSYM, ".gnu.version", false },
            { DT_VERNEED, ".gnu.version_r", false },
            { DT_INIT_ARRAY, ".init_array", false },
            { DT_INIT_ARRAYSZ, ".init_array", true },
            { DT_FINI_ARRAY, ".fini_array", false },
            { DT_FINI_ARRAYSZ, ".fini_array", true },
        };
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(tables); t += 1)
        {
            const Elf64_Shdr* section = dynamic_test_section(image, tables[t].section);
            uint64_t value = 0;
            const bool tagged = dynamic_test_tag(image, tables[t].tag, &value);
            const bool matches = section != 0 ? tagged && value == (tables[t].size ? section->sh_size : section->sh_addr) : !tagged;
            dynamic_test_expect(tally, matches, tables[t].section, t);
        }
        uint64_t value = 0;
        bool loader = false;
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            loader = loader || dynamic_test_from_loader(runtime, link, symbol);
        }
        const char* names[3] = { 0 };
        const uint32_t needed = dynamic_test_needed(image, names, BUSTER_ARRAY_LENGTH(names));
        const uint32_t libraries = loader ? 2 : 1;
        const bool libc = needed >= 1 && strcmp(names[0], "libc.so.6") == 0;
        const bool ld = !loader || (needed >= 2 && runtime->loader.soname.length == strlen(names[1]) &&
                                    memcmp(names[1], runtime->loader.soname.pointer, runtime->loader.soname.length) == 0);
        dynamic_test_expect(tally, needed == libraries && libc && ld, "DT_NEEDED libc.so.6, and the loader as needed", index);
        dynamic_test_expect(tally, library ? !dynamic_test_tag(image, DT_FLAGS_1, &value) : dynamic_test_tag(image, DT_FLAGS_1, &value) && value == DF_1_PIE, "DT_FLAGS_1 PIE",
                            index);
        dynamic_test_expect(tally, dynamic_test_tag(image, DT_DEBUG, &value) == !library, "DT_DEBUG a PIE's", index);
        // A shared library's soname, and DF_STATIC_TLS with an initial-exec access.
        const Elf64_Shdr* dynstr = dynamic_test_section(image, ".dynstr");
        const char* strings = (const char*)dynamic_test_bytes(image, dynstr);
        const bool sonamed = dynamic_test_tag(image, DT_SONAME, &value);
        dynamic_test_expect(tally,
                            library ? sonamed && strings != 0 && value < dynstr->sh_size && strcmp(strings + value, BUSTER_DYNAMIC_TEST_SONAME) == 0 : !sonamed,
                            "DT_SONAME a shared library's", index);
        bool initial_exec = false;
        for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
        {
            initial_exec = initial_exec || (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_GOTTPOFF;
        }
        const bool flagged = dynamic_test_tag(image, DT_FLAGS, &value);
        dynamic_test_expect(tally, library && initial_exec ? flagged && value == DF_STATIC_TLS : !flagged, "DT_FLAGS DF_STATIC_TLS with initial-exec accesses", index);
        dynamic_test_expect(tally, dynamic_test_tag(image, DT_INIT, &value) && value == dynamic_test_address(link, dynamic_global(runtime, bind, "_init")), "DT_INIT _init",
                            index);
        dynamic_test_expect(tally, dynamic_test_tag(image, DT_FINI, &value) && value == dynamic_test_address(link, dynamic_global(runtime, bind, "_fini")), "DT_FINI _fini",
                            index);
        dynamic_test_expect(tally, dynamic_test_tag(image, DT_VERNEEDNUM, &value) && value == libraries, "DT_VERNEEDNUM", index);
        // .gnu.version_r's entries, as its section header counts them (sh_info, which readelf -V reads) and as its
        // chain of vn_next links has them: as many as DT_VERNEEDNUM says.
        const Elf64_Shdr* verneed_section = dynamic_test_section(image, ".gnu.version_r");
        const uint8_t* verneed = dynamic_test_bytes(image, verneed_section);
        uint64_t chain = 0;
        uint64_t record = 0;
        bool more = verneed != 0 && verneed_section->sh_size >= sizeof(Elf64_Verneed);
        while (more)
        {
            Elf64_Verneed entry;
            memcpy(&entry, verneed + record, sizeof(entry));
            chain += 1;
            more = entry.vn_next != 0 && record + entry.vn_next + sizeof(Elf64_Verneed) <= verneed_section->sh_size;
            record += entry.vn_next;
        }
        dynamic_test_expect(tally, verneed_section != 0 && verneed_section->sh_info == libraries && chain == libraries,
                            ".gnu.version_r's entries: its sh_info and its chain, DT_VERNEEDNUM's", verneed_section != 0 ? verneed_section->sh_info * 100 + chain : 0);
    }
}

// All of the structure's checks of one file, written with `relr` or without. Returns how many loads and calls through
// the GOT relaxed.
static uint64_t dynamic_test_structure(DynamicTestTally* tally, const Link* link, const Bind* bind, const Runtime* runtime, String8 file, bool relr, uint64_t index)
{
    uint64_t relaxed = 0;
    Scratch scratch = scratch_begin(0);
    const DynamicTestImage image = dynamic_test_image(file);
    uint32_t* indices = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, link->symbols.count);
    dynamic_test_headers(tally, link, bind, runtime, &image, index);
    dynamic_test_expect(tally, indices != 0, "room for the indices", index);
    if (image.valid && indices != 0)
    {
        dynamic_test_symbols(tally, link, bind, runtime, &image, indices, index);
        dynamic_test_gnu_hash(tally, &image, index);
        const bool library = runtime->kind == RUNTIME_KIND_SHARED;
        dynamic_test_relocations(tally, link, &image, indices, relr, library, scratch.arena, index);
        relaxed = dynamic_test_got(tally, link, &image, library, scratch.arena, index);
        const char* unwind = executable_test_unwind(link, file, 0, bind->unit_base, 0);
        dynamic_test_expect(tally, unwind == 0, unwind != 0 ? unwind : "the unwind tables", index);
    }
    scratch_end(scratch);
    return relaxed;
}

// Whether the runtime's libc.so.6 defines GLIBC_ABI_DT_RELR, with which the writer puts RELATIVE relocations in
// .relr.dyn.
static bool dynamic_test_relr_supported(const Runtime* runtime)
{
    bool result = false;
    for (uint32_t v = 0; v < runtime->libc.version_count; v += 1)
    {
        result = result || (!runtime->libc.versions[v].base && import_equal(runtime->libc.versions[v].name, BUSTER_DYNAMIC_RELR_VERSION));
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The variants

// Whether the harness (object_test_harness) at `harness` loads the library at `library`, whose buster_library_main
// exits with status 0 having written exactly `expected`.
static bool dynamic_test_loads(const char* harness, const char* library, const uint8_t* expected, uint64_t expected_length)
{
    uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
    uint64_t length = 0;
    int status = -1;
    char* arguments[] = { (char*)harness, (char*)library, 0 };
    char* environment[] = { "BUSTER=1", 0 };
    const bool ran = executable_test_run_arguments(arguments, environment, output, &length, &status);
    return ran && WIFEXITED(status) && WEXITSTATUS(status) == 0 && length == expected_length && memcmp(output, expected, length) == 0;
}

// The same link by the writer's exact twins, with 1 and 8 lanes: the same bytes as `file`; and without RELR, with one
// lane: .rela.dyn alone, whose structure the checks read and which must run as the program does, writing `expected`
// and exiting with `status`, or for a shared library, which `harness` loads (dynamic_test_loads), with status 0. They
// go beside `path`.
static void dynamic_test_variants(DynamicTestTally* tally, Link* link, const Bind* bind, const Runtime* runtime, String8 path, String8 file, const uint8_t* expected,
                                  uint64_t expected_length, int status, const char* harness, uint64_t index)
{
    char variant_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const int length = snprintf(variant_bytes, sizeof(variant_bytes), "%.*s-variant", (int)path.length, path.pointer);
    const String8 variant = {
        .pointer = variant_bytes,
        .length = length > 0 ? (uint64_t)length : 0,
    };
    static const uint32_t exact_lanes[] = { 1, 8 };
    for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(exact_lanes); l += 1)
    {
        DynamicTestWrite write = {
            .exact = true,
        };
        ExecutableResult result;
        const bool succeeded = executable_test_reset(link, variant.pointer) && dynamic_test_write_with(&write, link, bind, runtime, variant, exact_lanes[l], &result);
        Scratch scratch = scratch_begin(0);
        const String8 bytes = succeeded ? executable_test_read(scratch.arena, variant.pointer) : (String8){ 0 };
        const bool same = bytes.pointer != 0 && bytes.length == file.length && memcmp(bytes.pointer, file.pointer, file.length) == 0;
        dynamic_test_expect(tally, same, "the exact twins write the same file", index * 10 + exact_lanes[l]);
        scratch_end(scratch);
    }
    DynamicTestWrite write = {
        .rela_only = true,
    };
    ExecutableResult result;
    const bool succeeded = executable_test_reset(link, variant.pointer) && dynamic_test_write_with(&write, link, bind, runtime, variant, 1, &result);
    dynamic_test_expect(tally, succeeded && result.relr_words == 0, "without RELR", index);
    Scratch scratch = scratch_begin(0);
    const String8 bytes = succeeded ? executable_test_read(scratch.arena, variant.pointer) : (String8){ 0 };
    if (bytes.pointer != 0)
    {
        dynamic_test_structure(tally, link, bind, runtime, bytes, false, index);
    }
    uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
    uint64_t output_length = 0;
    int ended = -1;
    const bool ran = harness != 0 ? bytes.pointer != 0 && dynamic_test_loads(harness, variant.pointer, expected, expected_length)
                                  : bytes.pointer != 0 && executable_test_run(variant.pointer, output, &output_length, &ended) && WIFEXITED(ended) &&
                                        WEXITSTATUS(ended) == status && output_length == expected_length && memcmp(output, expected, expected_length) == 0;
    dynamic_test_expect(tally, ran, "runs without RELR", index);
    scratch_end(scratch);
    unlink(variant.pointer);
}

// ---------------------------------------------------------------------------------------------------------------------
// Unwinding
//
// With --cross-check, two views of the unwind tables from outside: lld's, whose .eh_frame_hdr must index the same
// functions with the same ranges (dynamic_test_fdes, by the functions' names); and libgcc's unwinder's, in each program
// run again with a library preloaded (dynamic_test_unwinder_source, built by cc), whose write, on its first call,
// walks the stack with _Unwind_Backtrace as an exception or a profiler would, and reports how the walk ended and each
// frame's address less the executable's load address (0 for the frame past the outermost, which libgcc reports with
// address 0), into the file BUSTER_UNWIND_REPORT names, before it writes what write would. libgcc finds each frame's
// FDE through PT_GNU_EH_FRAME and .eh_frame_hdr's table, so the walk must go from the function that called write
// through main, libc's frames and _start, whose FDE says it is the outermost, and end there (_URC_END_OF_STACK)
// (dynamic_test_unwinder). And the call frame rules, which a walk doesn't need but a debugger or an exception
// restoring registers does: readelf -wF interprets each FDE as a table, the CFA's rule and each saved register's from
// each location on, and objdump -d's disassembly gives each of the program's functions' pushes and pops, from which
// the test works the table out again as GCC's CFI gives it for code that moves rsp with pushes and pops alone: from
// the function's start the CFA at rsp + 8, after each push 8 bytes more and after each pop 8 less, a callee-saved
// register that a push saves at the CFA less the new offset, and one that a pop restores as it was. Each of the
// program's functions must have that table, row for row (dynamic_test_frames).

// _Unwind_Backtrace's result when the walk ends at the outermost frame: _URC_END_OF_STACK in unwind.h.
#define BUSTER_DYNAMIC_TEST_END_OF_STACK 5

// An FDE that a table indexes: its function's name and its range.
typedef struct DynamicTestFde DynamicTestFde;
struct DynamicTestFde
{
    const uint8_t* name;
    uint64_t name_length;
    uint64_t range;
};

static int dynamic_test_by_fde(const void* a, const void* b)
{
    const DynamicTestFde* x = a;
    const DynamicTestFde* y = b;
    const uint64_t shorter = x->name_length < y->name_length ? x->name_length : y->name_length;
    const int compared = memcmp(x->name, y->name, shorter);
    const int lengths = x->name_length < y->name_length ? -1 : x->name_length > y->name_length ? 1 : 0;
    const int ranges = x->range < y->range ? -1 : x->range > y->range ? 1 : 0;
    return compared != 0 ? compared : lengths != 0 ? lengths : ranges;
}

// A function's address and name, for the lookups of the tables' initial locations.
typedef struct DynamicTestFunction DynamicTestFunction;
struct DynamicTestFunction
{
    uint64_t address;
    const uint8_t* name;
    uint64_t name_length;
};

static int dynamic_test_by_address(const void* a, const void* b)
{
    const DynamicTestFunction* x = a;
    const DynamicTestFunction* y = b;
    return x->address < y->address ? -1 : x->address > y->address ? 1 : 0;
}

// The functions that a PIE's .eh_frame_hdr indexes, by name and range, sorted, into `fdes` (room for `capacity`): their
// names from `link` (the writer's file, whose atoms' addresses the write gave them) or, without it, from the file's
// .symtab (lld's). Returns how many, or UINT64_MAX if the table or a function can't be read.
static uint64_t dynamic_test_fdes(const DynamicTestImage* image, const Link* link, DynamicTestFde* fdes, uint64_t capacity, Arena* arena)
{
    const Elf64_Shdr* header = dynamic_test_section(image, ".eh_frame_hdr");
    const uint8_t* index = dynamic_test_bytes(image, header);
    uint32_t count = 0;
    if (index != 0 && header->sh_size >= 12)
    {
        memcpy(&count, index + 8, sizeof(count));
    }
    uint64_t result = index != 0 && header->sh_size == 12 + 8 * (uint64_t)count && count <= capacity ? count : UINT64_MAX;
    // The functions by address: the link's named symbols in TEXT atoms that are functions (a compiler's object has many
    // in one atom) or at an atom's start, or .symtab's functions.
    const Elf64_Shdr* symtab = link == 0 ? dynamic_test_section(image, ".symtab") : 0;
    const Elf64_Shdr* strtab = symtab != 0 && symtab->sh_link < image->header->e_shnum ? &image->section_headers[symtab->sh_link] : 0;
    const uint64_t limit = link != 0 ? link->symbols.count : symtab != 0 ? symtab->sh_size / sizeof(Elf64_Sym) : 0;
    DynamicTestFunction* functions = BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestFunction, limit + 1);
    result = functions != 0 && (link != 0 || strtab != 0) ? result : UINT64_MAX;
    uint64_t function_count = 0;
    for (uint64_t s = 1; result != UINT64_MAX && s < limit; s += 1)
    {
        if (link != 0)
        {
            const AtomId atom = link->symbols.atom[(SymbolId)s];
            const NameId name = link->symbols.name[(SymbolId)s];
            const bool function = (link->symbols.flags[(SymbolId)s] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_FUNCTION;
            if (atom != 0 && name != 0 && (function || link->symbols.value[(SymbolId)s] == 0) && link->atoms.class[atom] == BUSTER_LINK_CLASS_TEXT)
            {
                functions[function_count] = (DynamicTestFunction){
                    .address = link->atoms.address[atom] + link->symbols.value[(SymbolId)s],
                    .name = link->names.bytes[name],
                    .name_length = link->names.length[name],
                };
                function_count += 1;
            }
        }
        else
        {
            Elf64_Sym symbol;
            memcpy(&symbol, image->file.pointer + symtab->sh_offset + s * sizeof(Elf64_Sym), sizeof(symbol));
            if (ELF64_ST_TYPE(symbol.st_info) == STT_FUNC && symbol.st_shndx != SHN_UNDEF && symbol.st_name < strtab->sh_size)
            {
                const char* name = image->file.pointer + strtab->sh_offset + symbol.st_name;
                functions[function_count] = (DynamicTestFunction){
                    .address = symbol.st_value,
                    .name = (const uint8_t*)name,
                    .name_length = strlen(name),
                };
                function_count += 1;
            }
        }
    }
    if (result != UINT64_MAX)
    {
        qsort(functions, function_count, sizeof(*functions), dynamic_test_by_address);
    }
    for (uint64_t i = 0; result != UINT64_MAX && i < count; i += 1)
    {
        int32_t entry[2];
        memcpy(entry, index + 12 + 8 * i, sizeof(entry));
        const uint64_t initial = header->sh_addr + (uint64_t)(int64_t)entry[0];
        const uint64_t at = dynamic_test_file_offset(image, header->sh_addr + (uint64_t)(int64_t)entry[1] + 12);
        uint32_t range = 0;
        if (at != UINT64_MAX)
        {
            memcpy(&range, image->file.pointer + at, sizeof(range));
        }
        // The function at the initial location, the first of its address.
        uint64_t low = 0;
        uint64_t high = function_count;
        while (low < high)
        {
            const uint64_t middle = low + (high - low) / 2;
            low = functions[middle].address < initial ? middle + 1 : low;
            high = functions[middle].address < initial ? high : middle;
        }
        const bool found = at != UINT64_MAX && low < function_count && functions[low].address == initial;
        fdes[i] = (DynamicTestFde){
            .name = found ? functions[low].name : 0,
            .name_length = found ? functions[low].name_length : 0,
            .range = range,
        };
        result = found ? result : UINT64_MAX;
    }
    if (result != UINT64_MAX)
    {
        qsort(fdes, result, sizeof(*fdes), dynamic_test_by_fde);
    }
    return result;
}

// The preloaded library's source (see "Unwinding").
static const char dynamic_test_unwinder_source[] = "#define _GNU_SOURCE\n"
                                                   "#include <fcntl.h>\n"
                                                   "#include <link.h>\n"
                                                   "#include <stdint.h>\n"
                                                   "#include <stdio.h>\n"
                                                   "#include <stdlib.h>\n"
                                                   "#include <sys/syscall.h>\n"
                                                   "#include <unistd.h>\n"
                                                   "#include <unwind.h>\n"
                                                   "\n"
                                                   "typedef struct\n"
                                                   "{\n"
                                                   "    uintptr_t addresses[64];\n"
                                                   "    int count;\n"
                                                   "} Trace;\n"
                                                   "\n"
                                                   "static int reported;\n"
                                                   "\n"
                                                   "static _Unwind_Reason_Code step(struct _Unwind_Context* context, void* argument)\n"
                                                   "{\n"
                                                   "    Trace* trace = argument;\n"
                                                   "    if (trace->count < 64)\n"
                                                   "    {\n"
                                                   "        trace->addresses[trace->count] = _Unwind_GetIP(context);\n"
                                                   "        trace->count += 1;\n"
                                                   "    }\n"
                                                   "    return _URC_NO_REASON;\n"
                                                   "}\n"
                                                   "\n"
                                                   "// The executable's load address: the first object dl_iterate_phdr gives.\n"
                                                   "static int first_object(struct dl_phdr_info* info, size_t size, void* argument)\n"
                                                   "{\n"
                                                   "    (void)size;\n"
                                                   "    *(uintptr_t*)argument = info->dlpi_addr;\n"
                                                   "    return 1;\n"
                                                   "}\n"
                                                   "\n"
                                                   "ssize_t write(int file, const void* bytes, size_t count)\n"
                                                   "{\n"
                                                   "    if (!reported)\n"
                                                   "    {\n"
                                                   "        reported = 1;\n"
                                                   "        Trace trace = { .count = 0 };\n"
                                                   "        const _Unwind_Reason_Code code = _Unwind_Backtrace(step, &trace);\n"
                                                   "        uintptr_t base = 0;\n"
                                                   "        dl_iterate_phdr(first_object, &base);\n"
                                                   "        const char* path = getenv(\"BUSTER_UNWIND_REPORT\");\n"
                                                   "        const int report = path != 0 ? open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;\n"
                                                   "        char line[64];\n"
                                                   "        int length = snprintf(line, sizeof(line), \"%d\\n\", (int)code);\n"
                                                   "        syscall(SYS_write, report, line, length);\n"
                                                   "        for (int i = 0; i < trace.count; i += 1)\n"
                                                   "        {\n"
                                                   "            const uintptr_t address = trace.addresses[i];\n"
                                                   "            length = snprintf(line, sizeof(line), \"%lx\\n\", (unsigned long)(address != 0 ? address - base : 0));\n"
                                                   "            syscall(SYS_write, report, line, length);\n"
                                                   "        }\n"
                                                   "        close(report);\n"
                                                   "    }\n"
                                                   "    return syscall(SYS_write, file, bytes, count);\n"
                                                   "}\n";

// Writes the preloaded library's source into `directory` and builds it with cc, into `library` (BUSTER_EXECUTABLE_TEST
// _PATH_BYTES of room): whether it was built.
static bool dynamic_test_build_unwinder(const char* directory, char* library)
{
    char source[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char output_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char error_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snprintf(source, sizeof(source), "%s/unwinder.c", directory);
    snprintf(library, BUSTER_EXECUTABLE_TEST_PATH_BYTES, "%s/unwinder.so", directory);
    snprintf(output_path, sizeof(output_path), "%s/unwinder.out", directory);
    snprintf(error_path, sizeof(error_path), "%s/unwinder.err", directory);
    FILE* file = fopen(source, "w");
    bool result = file != 0 && fwrite(dynamic_test_unwinder_source, 1, sizeof(dynamic_test_unwinder_source) - 1, file) == sizeof(dynamic_test_unwinder_source) - 1;
    result = file != 0 && fclose(file) == 0 && result;
    char* arguments[] = { "cc", "-shared", "-fPIC", "-O1", "-o", library, source, "-lgcc_s", 0 };
    result = result && object_test_run(arguments, output_path, error_path) == 0;
    unlink(source);
    unlink(output_path);
    unlink(error_path);
    return result;
}

// Runs the program at `path` with the unwinder preloaded: it must print `expected` as it does without, and the walk
// must have gone through main and ended at _start, the outermost frame.
static void dynamic_test_unwinder(DynamicTestTally* tally, const Link* link, const Bind* bind, const Runtime* runtime, const char* path, const char* library,
                                  const uint8_t* expected, uint64_t expected_length, uint64_t index)
{
    char report[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char preload[BUSTER_EXECUTABLE_TEST_PATH_BYTES + 16];
    char where[BUSTER_EXECUTABLE_TEST_PATH_BYTES + 32];
    snprintf(report, sizeof(report), "%s.unwind", path);
    snprintf(preload, sizeof(preload), "LD_PRELOAD=%s", library);
    snprintf(where, sizeof(where), "BUSTER_UNWIND_REPORT=%s", report);
    char* environment[] = { "BUSTER=1", preload, where, 0 };
    unlink(report);
    uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
    uint64_t length = 0;
    int status = -1;
    const bool ran = executable_test_run_with(path, environment, output, &length, &status) && WIFEXITED(status) && WEXITSTATUS(status) == 0 && length == expected_length &&
                     memcmp(output, expected, length) == 0;
    dynamic_test_expect(tally, ran, "runs with the unwinder", index);
    Scratch scratch = scratch_begin(0);
    const String8 text = object_test_read_file(scratch.arena, report);
    // How the walk ended, then each frame's address from the load address, in hexadecimal.
    char* at = (char*)text.pointer;
    char* end = 0;
    const long code = at != 0 ? strtol(at, &end, 10) : -1;
    bool read = at != 0 && end != at;
    at = end;
    AtomId frames[64];
    uint32_t frame_count = 0;
    while (read && frame_count < BUSTER_ARRAY_LENGTH(frames) && *at != 0)
    {
        const unsigned long long address = strtoull(at, &end, 16);
        read = end != at;
        at = end;
        while (*at == '\n')
        {
            at += 1;
        }
        // The TEXT atom that holds the call before the return address; none for the frame past the outermost.
        AtomId atom = 0;
        for (AtomId a = 1; read && address != 0 && a < link->atoms.count; a += 1)
        {
            const uint64_t start = link->atoms.address[a];
            atom = link->atoms.class[a] == BUSTER_LINK_CLASS_TEXT && address - 1 >= start && address - 1 < start + link->atoms.size[a] ? a : atom;
        }
        frames[frame_count] = atom;
        frame_count += read && address != 0 ? 1 : 0;
    }
    AtomId main_atom = 0;
    for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
    {
        main_atom = executable_named(link, symbol, "main") && link->symbols.source[symbol] == BUSTER_LINK_SOURCE_PROGRAM ? link->symbols.atom[symbol] : main_atom;
    }
    const SymbolId start = dynamic_global(runtime, bind, "_start");
    const AtomId start_atom = start != 0 ? link->symbols.atom[start] : 0;
    bool through_main = false;
    for (uint32_t f = 0; f < frame_count; f += 1)
    {
        through_main = through_main || (main_atom != 0 && frames[f] == main_atom);
    }
    dynamic_test_expect(tally, read && code == BUSTER_DYNAMIC_TEST_END_OF_STACK, "the unwinder's walk ends at the outermost frame", index * 100 + (uint64_t)(code & 0xff));
    dynamic_test_expect(tally, through_main, "through main", index);
    dynamic_test_expect(tally, frame_count != 0 && start_atom != 0 && frames[frame_count - 1] == start_atom, "at _start", index);
    printf("  the unwinder: %u frames, %s main, ending %s\n", frame_count, through_main ? "through" : "not through",
           frame_count != 0 && frames[frame_count - 1] == start_atom ? "at _start" : "elsewhere");
    scratch_end(scratch);
    unlink(report);
}

// The callee-saved registers, as objdump's operands (after their '%') and readelf's columns name them.
#define BUSTER_DYNAMIC_TEST_CALLEE_SAVED 6
static const char* const dynamic_test_callee_saved[BUSTER_DYNAMIC_TEST_CALLEE_SAVED] = {
    "rbx",
    "rbp",
    "r12",
    "r13",
    "r14",
    "r15",
};

// The rows a function's table may have: one, then one after each push or pop.
#define BUSTER_DYNAMIC_TEST_FRAME_ROWS 64

// A push or a pop that objdump lists: where it starts and ends, whether it pushes, and the callee-saved register it
// pushes or pops (an index into dynamic_test_callee_saved), or UINT32_MAX for another operand.
typedef struct DynamicTestStackMove DynamicTestStackMove;
struct DynamicTestStackMove
{
    uint64_t start;
    uint64_t end;
    bool push;
    uint32_t saved;
};

// A row of a function's table: its location, the CFA's offset from rsp, and each callee-saved register's distance below
// the CFA where it is saved, or 0 where it has its caller's value.
typedef struct DynamicTestFrameRow DynamicTestFrameRow;
struct DynamicTestFrameRow
{
    uint64_t location;
    uint64_t offset;
    uint64_t saved[BUSTER_DYNAMIC_TEST_CALLEE_SAVED];
};

// A program's function: where it starts and ends, and how many FDEs readelf listed for it.
typedef struct DynamicTestFrameFunction DynamicTestFrameFunction;
struct DynamicTestFrameFunction
{
    uint64_t start;
    uint64_t end;
    uint32_t fdes;
};

static int dynamic_test_by_start(const void* a, const void* b)
{
    const DynamicTestFrameFunction* x = a;
    const DynamicTestFrameFunction* y = b;
    return x->start < y->start ? -1 : x->start > y->start ? 1 : 0;
}

// The callee-saved register `name` names, or UINT32_MAX.
static uint32_t dynamic_test_callee_saved_register(String8 name)
{
    uint32_t result = UINT32_MAX;
    for (uint32_t r = 0; r < BUSTER_DYNAMIC_TEST_CALLEE_SAVED; r += 1)
    {
        result = object_test_equal(name, dynamic_test_callee_saved[r]) ? r : result;
    }
    return result;
}

// The pushes and pops of objdump -d -w's listing `text`, in its order, into `moves` (room for `capacity`): how many, or
// UINT64_MAX if they don't fit. An instruction's line is its address and a colon, a tab, its bytes, a tab, then its
// mnemonic and operands.
static uint64_t dynamic_test_stack_moves(String8 text, DynamicTestStackMove* moves, uint64_t capacity)
{
    uint64_t count = 0;
    uint64_t position = 0;
    String8 line = { 0 };
    while (count != UINT64_MAX && object_test_next_line(text, &position, &line))
    {
        const char* first = memchr(line.pointer, '\t', line.length);
        const uint64_t bytes_at = first != 0 ? (uint64_t)(first - line.pointer) + 1 : line.length;
        const char* second = first != 0 ? memchr(first + 1, '\t', line.length - bytes_at) : 0;
        const uint64_t text_at = second != 0 ? (uint64_t)(second - line.pointer) + 1 : line.length;
        String8 address[2];
        const uint64_t address_count = second != 0 ? object_test_tokens((String8){ .pointer = line.pointer, .length = bytes_at - 1 }, address, 2) : 0;
        uint64_t start = 0;
        const bool instruction = address_count == 1 && address[0].length >= 2 && address[0].pointer[address[0].length - 1] == ':' &&
                                 object_test_number((String8){ .pointer = address[0].pointer, .length = address[0].length - 1 }, true, &start);
        String8 bytes[16];
        const uint64_t byte_count = instruction ? object_test_tokens((String8){ .pointer = line.pointer + bytes_at, .length = text_at - 1 - bytes_at }, bytes, 16) : 0;
        String8 words[3];
        const uint64_t word_count = instruction ? object_test_tokens(object_test_after(line, text_at), words, 3) : 0;
        const bool push = word_count >= 2 && (object_test_equal(words[0], "push") || object_test_equal(words[0], "pushq"));
        const bool pop = word_count >= 2 && (object_test_equal(words[0], "pop") || object_test_equal(words[0], "popq"));
        if ((push || pop) && count < capacity)
        {
            const bool named = words[1].length > 1 && words[1].pointer[0] == '%';
            moves[count] = (DynamicTestStackMove){
                .start = start,
                .end = start + byte_count,
                .push = push,
                .saved = named ? dynamic_test_callee_saved_register(object_test_after(words[1], 1)) : UINT32_MAX,
            };
        }
        count = push || pop ? (count < capacity ? count + 1 : UINT64_MAX) : count;
    }
    return count;
}

// The table the pushes and pops in `moves` (sorted by start) give the function from `start` to `end` (see this
// section's comment), into `rows`: how many rows, or 0 if a move runs past the function, pops past the return address
// or the rows don't fit.
static uint32_t dynamic_test_frame_rows(const DynamicTestStackMove* moves, uint64_t move_count, uint64_t start, uint64_t end, DynamicTestFrameRow* rows)
{
    // The first move at or past the function's start.
    uint64_t low = 0;
    uint64_t high = move_count;
    while (low < high)
    {
        const uint64_t middle = low + (high - low) / 2;
        low = moves[middle].start < start ? middle + 1 : low;
        high = moves[middle].start < start ? high : middle;
    }
    rows[0] = (DynamicTestFrameRow){
        .location = start,
        .offset = 8,
    };
    uint32_t count = 1;
    for (uint64_t m = low; count != 0 && m < move_count && moves[m].start < end; m += 1)
    {
        const DynamicTestFrameRow* last = &rows[count - 1];
        const bool fits = count < BUSTER_DYNAMIC_TEST_FRAME_ROWS && moves[m].end <= end && (moves[m].push || last->offset >= 16);
        if (fits)
        {
            DynamicTestFrameRow* row = &rows[count];
            *row = *last;
            row->location = moves[m].end;
            row->offset = moves[m].push ? last->offset + 8 : last->offset - 8;
            if (moves[m].saved != UINT32_MAX)
            {
                row->saved[moves[m].saved] = moves[m].push ? row->offset : 0;
            }
        }
        count = fits ? count + 1 : 0;
    }
    return count;
}

// Whether readelf's row `tokens` (its location, the CFA's rule, then a rule per one of the FDE's `columns`) says what
// `row` does: the location, rsp + the offset, each callee-saved register at c-<its distance> or u, and the return
// address at c-8; and no other register.
static bool dynamic_test_frame_row(const String8* tokens, uint64_t count, const String8* columns, uint64_t column_count, const DynamicTestFrameRow* row)
{
    uint64_t location = 0;
    char expected[32];
    snprintf(expected, sizeof(expected), "rsp+%llu", (unsigned long long)row->offset);
    bool same = count == column_count + 2 && object_test_number(tokens[0], true, &location) && location == row->location && object_test_equal(tokens[1], expected);
    uint32_t saved = 0;
    uint32_t listed = 0;
    for (uint32_t r = 0; r < BUSTER_DYNAMIC_TEST_CALLEE_SAVED; r += 1)
    {
        saved += row->saved[r] != 0 ? 1 : 0;
    }
    for (uint64_t c = 0; same && c < column_count; c += 1)
    {
        const uint32_t r = dynamic_test_callee_saved_register(columns[c]);
        snprintf(expected, sizeof(expected), "c-%llu", (unsigned long long)(r != UINT32_MAX ? row->saved[r] : 8));
        const bool ra = object_test_equal(columns[c], "ra");
        same = ra ? object_test_equal(tokens[2 + c], "c-8") : r != UINT32_MAX && object_test_equal(tokens[2 + c], row->saved[r] != 0 ? expected : "u");
        listed += !ra && r != UINT32_MAX && row->saved[r] != 0 ? 1 : 0;
    }
    return same && listed == saved;
}

// Compares readelf -wF's table of each of the program's functions' FDEs in the executable at `path` with the one its
// pushes and pops in objdump -d's listing give (see this section's comment): each TEXT atom of the program's units
// (those below `units`) must have one FDE, of its range, whose table readelf prints row for row as
// dynamic_test_frame_rows works it out. The listings go in `directory`, named after `name`.
static void dynamic_test_frames(DynamicTestTally* tally, const Link* link, uint32_t units, const char* path, const char* directory, const char* name, bool keep,
                                Arena* arena)
{
    char disassembly[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char frames[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char errors[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snprintf(disassembly, sizeof(disassembly), "%s/%s.objdump", directory, name);
    snprintf(frames, sizeof(frames), "%s/%s.frames", directory, name);
    snprintf(errors, sizeof(errors), "%s/%s.listings.err", directory, name);
    char* objdump[] = { "objdump", "-d", "-w", (char*)path, 0 };
    char* readelf[] = { "readelf", "-wF", (char*)path, 0 };
    const bool listed = object_test_run(objdump, disassembly, errors) == 0 && object_test_run(readelf, frames, errors) == 0;
    dynamic_test_expect(tally, listed, "objdump and readelf list the program", 0);
    const String8 code = listed ? object_test_read_file(arena, disassembly) : (String8){ 0 };
    const String8 table = listed ? object_test_read_file(arena, frames) : (String8){ 0 };
    // The program's functions, by address: each TEXT atom of its units, of its first symbol's size (its atom's may have
    // padding).
    uint64_t* sizes = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, link->atoms.count);
    for (AtomId atom = 0; sizes != 0 && atom < link->atoms.count; atom += 1)
    {
        sizes[atom] = UINT64_MAX;
    }
    for (SymbolId symbol = link->symbols.count - 1; sizes != 0 && symbol >= 1; symbol -= 1)
    {
        const AtomId atom = link->symbols.atom[symbol];
        if (atom != 0 && link->symbols.value[symbol] == 0)
        {
            sizes[atom] = link->symbols.size[symbol];
        }
    }
    DynamicTestFrameFunction* functions = sizes != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestFrameFunction, link->atoms.count) : 0;
    uint64_t function_count = 0;
    for (AtomId atom = 1; functions != 0 && atom < link->atoms.count; atom += 1)
    {
        if (link->atoms.class[atom] == BUSTER_LINK_CLASS_TEXT && link->atoms.unit[atom] < units)
        {
            functions[function_count] = (DynamicTestFrameFunction){
                .start = link->atoms.address[atom],
                .end = link->atoms.address[atom] + sizes[atom],
            };
            function_count += 1;
        }
    }
    if (functions != 0)
    {
        qsort(functions, function_count, sizeof(*functions), dynamic_test_by_start);
    }
    // At most a push or a pop per byte of code.
    const uint64_t capacity = code.length / 8 + 1;
    DynamicTestStackMove* moves = code.pointer != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestStackMove, capacity) : 0;
    const uint64_t move_count = moves != 0 ? dynamic_test_stack_moves(code, moves, capacity) : UINT64_MAX;
    bool sorted = move_count != UINT64_MAX;
    for (uint64_t m = 1; sorted && m < move_count; m += 1)
    {
        sorted = moves[m].start >= moves[m - 1].end;
    }
    dynamic_test_expect(tally, functions != 0 && sorted && table.pointer != 0, "objdump's pushes and pops read", move_count);
    // readelf's tables: an FDE's line ("... FDE cie=... pc=START..END"), then, if it has instructions, its columns'
    // ("LOC CFA reg... ra") and its rows, then an empty line.
    DynamicTestFrameRow rows[BUSTER_DYNAMIC_TEST_FRAME_ROWS];
    String8 columns[BUSTER_OBJECT_TEST_MAX_TOKENS];
    uint64_t column_count = 0;
    uint64_t function = UINT64_MAX;
    uint32_t row_count = 0;
    uint32_t row = 0;
    uint64_t compared = 0;
    bool same = functions != 0 && sorted && table.pointer != 0;
    uint64_t position = 0;
    String8 line = { 0 };
    bool more = same;
    while (same && more)
    {
        more = object_test_next_line(table, &position, &line);
        String8 tokens[BUSTER_OBJECT_TEST_MAX_TOKENS];
        const uint64_t count = more ? object_test_tokens(line, tokens, BUSTER_OBJECT_TEST_MAX_TOKENS) : 0;
        const bool fde = count >= 6 && object_test_equal(tokens[3], "FDE") && object_test_starts_with(tokens[5], "pc=");
        if (function != UINT64_MAX && (fde || count == 0 || (count >= 4 && object_test_equal(tokens[3], "CIE"))))
        {
            // The FDE's end: every row there, an FDE without instructions having none but the CIE's.
            same = row == row_count || (row == 0 && row_count == 1);
            function = UINT64_MAX;
        }
        if (same && fde)
        {
            const String8 range = object_test_after(tokens[5], 3);
            const char* dots = memchr(range.pointer, '.', range.length);
            uint64_t start = 0;
            uint64_t end = 0;
            same = dots != 0 && dots + 1 < range.pointer + range.length && dots[1] == '.' &&
                   object_test_number((String8){ .pointer = range.pointer, .length = (uint64_t)(dots - range.pointer) }, true, &start) &&
                   object_test_number(object_test_after(range, (uint64_t)(dots - range.pointer) + 2), true, &end);
            DynamicTestFrameFunction key = {
                .start = start,
            };
            const DynamicTestFrameFunction* found = same ? bsearch(&key, functions, function_count, sizeof(*functions), dynamic_test_by_start) : 0;
            if (found != 0)
            {
                function = (uint64_t)(found - functions);
                functions[function].fdes += 1;
                row_count = dynamic_test_frame_rows(moves, move_count, start, end, rows);
                row = 0;
                column_count = 0;
                same = found->end == end && row_count != 0;
            }
        }
        else if (same && function != UINT64_MAX && count >= 3 && object_test_equal(tokens[0], "LOC") && object_test_equal(tokens[1], "CFA"))
        {
            column_count = count - 2 <= BUSTER_OBJECT_TEST_MAX_TOKENS - 2 ? count - 2 : 0;
            memcpy(columns, tokens + 2, column_count * sizeof(*columns));
            same = column_count != 0;
        }
        else if (same && function != UINT64_MAX && count != 0)
        {
            same = row < row_count && dynamic_test_frame_row(tokens, count, columns, column_count, &rows[row]);
            if (!same)
            {
                printf("  %s: the FDE of the function at 0x%llx, row %u: readelf's \"%.*s\", against rsp+%llu from its pushes and pops\n", name,
                       (unsigned long long)functions[function].start, row, (int)(line.length < 120 ? line.length : 120), line.pointer,
                       (unsigned long long)(row < row_count ? rows[row].offset : 0));
            }
            row += 1;
            compared += 1;
        }
    }
    for (uint64_t f = 0; same && f < function_count; f += 1)
    {
        same = functions[f].fdes == 1;
    }
    dynamic_test_expect(tally, same && function_count != 0, "readelf's call frame rules as objdump's pushes and pops give them", compared);
    printf("  %s: the call frame rules of %llu functions, %llu rows, as their pushes and pops give them\n", name, (unsigned long long)function_count,
           (unsigned long long)compared);
    if (!keep)
    {
        unlink(disassembly);
        unlink(frames);
        unlink(errors);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Against lld
//
// With --cross-check, the bound link's objects (the object writer's) linked by ld.lld as a PIE, against only
// libc.so.6 and the loader in GCC's order (bind_test_link_arguments), with --build-id as the design's matched options
// have it, and what the two files say compared: their segments (the program headers' types in order, their flags, and
// the PT_LOADs' alignments), their dynamic symbols (name, binding, type, whether defined, version name), the symbols
// that have a slot the loader fills (GLOB_DAT, TPOFF64, or lld's JUMP_SLOT for a PLT entry, where the stub reads the
// GOT's slot instead), the R_X86_64_64s with their addends, as many RELATIVE relocations of pointers (those outside the
// GOT, where lld relaxes some loads that the writer reads through slots), the init and fini arrays' sizes, DT_NEEDED,
// DT_INIT and DT_FINI.

typedef struct DynamicTestSymbol DynamicTestSymbol;
struct DynamicTestSymbol
{
    const char* name;
    const char* version;
    uint8_t info;
    bool defined;
};

typedef struct DynamicTestImportReference DynamicTestImportReference;
struct DynamicTestImportReference
{
    const char* name;
    // 0 for a slot, 1 for a pointer (with its addend).
    uint32_t kind;
    int64_t addend;
};

static int dynamic_test_by_symbol(const void* a, const void* b)
{
    const DynamicTestSymbol* x = a;
    const DynamicTestSymbol* y = b;
    const int names = strcmp(x->name, y->name);
    return names != 0 ? names : strcmp(x->version, y->version);
}

static int dynamic_test_by_reference(const void* a, const void* b)
{
    const DynamicTestImportReference* x = a;
    const DynamicTestImportReference* y = b;
    const int names = strcmp(x->name, y->name);
    return names != 0 ? names : x->kind != y->kind ? (x->kind < y->kind ? -1 : 1) : x->addend < y->addend ? -1 : x->addend > y->addend ? 1 : 0;
}

// The image's dynamic symbols but the null one, sorted, into `symbols` (room for `capacity`): how many, or UINT64_MAX
// if they don't fit or the tables are out of bounds.
static uint64_t dynamic_test_read_symbols(const DynamicTestImage* image, DynamicTestSymbol* symbols, uint64_t capacity)
{
    const Elf64_Shdr* dynsym_section = dynamic_test_section(image, ".dynsym");
    const Elf64_Shdr* dynstr_section = dynamic_test_section(image, ".dynstr");
    const Elf64_Shdr* versym_section = dynamic_test_section(image, ".gnu.version");
    const Elf64_Shdr* verneed_section = dynamic_test_section(image, ".gnu.version_r");
    const Elf64_Sym* dynsym = (const Elf64_Sym*)dynamic_test_bytes(image, dynsym_section);
    const char* dynstr = (const char*)dynamic_test_bytes(image, dynstr_section);
    const Elf64_Half* versym = (const Elf64_Half*)dynamic_test_bytes(image, versym_section);
    const uint8_t* verneed = dynamic_test_bytes(image, verneed_section);
    const uint64_t count = dynsym_section != 0 ? dynsym_section->sh_size / sizeof(Elf64_Sym) : 0;
    uint64_t result = dynsym != 0 && dynstr != 0 && count >= 1 && count - 1 <= capacity ? count - 1 : UINT64_MAX;
    for (uint64_t i = 1; result != UINT64_MAX && i < count; i += 1)
    {
        // The version's name through .gnu.version_r's entries for the library, "" for an unversioned symbol.
        const uint16_t number = versym != 0 && versym_section->sh_size >= (i + 1) * sizeof(Elf64_Half) ? versym[i] : 0;
        const char* version = "";
        uint64_t at = 0;
        bool more = verneed != 0 && number > VER_NDX_GLOBAL;
        while (more && at + sizeof(Elf64_Verneed) <= verneed_section->sh_size)
        {
            Elf64_Verneed needed;
            memcpy(&needed, verneed + at, sizeof(needed));
            uint64_t auxiliary_at = at + needed.vn_aux;
            for (uint32_t a = 0; a < needed.vn_cnt && auxiliary_at + sizeof(Elf64_Vernaux) <= verneed_section->sh_size; a += 1)
            {
                Elf64_Vernaux auxiliary;
                memcpy(&auxiliary, verneed + auxiliary_at, sizeof(auxiliary));
                version = auxiliary.vna_other == number && auxiliary.vna_name < dynstr_section->sh_size ? dynstr + auxiliary.vna_name : version;
                auxiliary_at += auxiliary.vna_next;
            }
            more = needed.vn_next != 0;
            at += needed.vn_next;
        }
        symbols[i - 1] = (DynamicTestSymbol){
            .name = dynsym[i].st_name < dynstr_section->sh_size ? dynstr + dynsym[i].st_name : "",
            .version = version,
            .info = dynsym[i].st_info,
            .defined = dynsym[i].st_shndx != SHN_UNDEF,
        };
    }
    if (result != UINT64_MAX)
    {
        qsort(symbols, result, sizeof(*symbols), dynamic_test_by_symbol);
    }
    return result;
}

// The image's relocations against dynamic symbols, from .rela.dyn and .rela.plt: each symbol with a slot once, and
// each pointer with its addend, sorted, into `references` (room for `capacity`); and its RELATIVE relocations outside
// .got and .got.plt, .rela.dyn's and .relr.dyn's. Returns how many references, or UINT64_MAX.
static uint64_t dynamic_test_read_references(const DynamicTestImage* image, DynamicTestImportReference* references, uint64_t capacity, uint64_t* pointers)
{
    static const char* const sections[] = {
        ".rela.dyn",
        ".rela.plt",
    };
    const Elf64_Shdr* dynsym_section = dynamic_test_section(image, ".dynsym");
    const Elf64_Shdr* dynstr_section = dynamic_test_section(image, ".dynstr");
    const Elf64_Shdr* got = dynamic_test_section(image, ".got");
    const Elf64_Shdr* got_plt = dynamic_test_section(image, ".got.plt");
    const Elf64_Sym* dynsym = (const Elf64_Sym*)dynamic_test_bytes(image, dynsym_section);
    const char* dynstr = (const char*)dynamic_test_bytes(image, dynstr_section);
    const uint64_t symbols = dynsym_section != 0 ? dynsym_section->sh_size / sizeof(Elf64_Sym) : 0;
    uint64_t result = dynsym != 0 && dynstr != 0 ? 0 : UINT64_MAX;
    *pointers = 0;
    for (uint32_t s = 0; result != UINT64_MAX && s < BUSTER_ARRAY_LENGTH(sections); s += 1)
    {
        const Elf64_Shdr* section = dynamic_test_section(image, sections[s]);
        const Elf64_Rela* rela = (const Elf64_Rela*)dynamic_test_bytes(image, section);
        const uint64_t count = section != 0 ? section->sh_size / sizeof(Elf64_Rela) : 0;
        for (uint64_t i = 0; result != UINT64_MAX && i < count; i += 1)
        {
            const uint32_t type = (uint32_t)ELF64_R_TYPE(rela[i].r_info);
            const uint64_t symbol = ELF64_R_SYM(rela[i].r_info);
            const uint64_t offset = rela[i].r_offset;
            const bool in_got = (got != 0 && offset >= got->sh_addr && offset < got->sh_addr + got->sh_size) ||
                                (got_plt != 0 && offset >= got_plt->sh_addr && offset < got_plt->sh_addr + got_plt->sh_size);
            *pointers += type == R_X86_64_RELATIVE && !in_got ? 1 : 0;
            const bool slot = type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT || type == R_X86_64_TPOFF64;
            if ((slot || type == R_X86_64_64) && symbol < symbols && dynsym[symbol].st_name < dynstr_section->sh_size)
            {
                result = result < capacity ? result : UINT64_MAX;
                if (result != UINT64_MAX)
                {
                    references[result] = (DynamicTestImportReference){
                        .name = dynstr + dynsym[symbol].st_name,
                        .kind = slot ? 0 : 1,
                        .addend = slot ? 0 : rela[i].r_addend,
                    };
                    result += 1;
                }
            }
        }
    }
    // .relr.dyn's, decoded into the end of `references`' room, which the references then may take again.
    uint64_t* addresses = (uint64_t*)(references + (result != UINT64_MAX ? result : 0));
    const uint64_t room = result != UINT64_MAX ? (capacity - result) * sizeof(*references) / sizeof(uint64_t) : 0;
    const uint64_t relr = result != UINT64_MAX ? dynamic_test_relr(image, addresses, room) : UINT64_MAX;
    result = relr != UINT64_MAX ? result : UINT64_MAX;
    for (uint64_t i = 0; result != UINT64_MAX && i < relr; i += 1)
    {
        const bool in_got = (got != 0 && addresses[i] >= got->sh_addr && addresses[i] < got->sh_addr + got->sh_size) ||
                            (got_plt != 0 && addresses[i] >= got_plt->sh_addr && addresses[i] < got_plt->sh_addr + got_plt->sh_size);
        *pointers += in_got ? 0 : 1;
    }
    if (result != UINT64_MAX)
    {
        qsort(references, result, sizeof(*references), dynamic_test_by_reference);
        // Each slot once: lld gives a symbol both a GOT slot and a PLT slot where the writer has one slot.
        uint64_t kept = 0;
        for (uint64_t i = 0; i < result; i += 1)
        {
            const bool repeated = kept != 0 && references[i].kind == 0 && references[kept - 1].kind == 0 && strcmp(references[i].name, references[kept - 1].name) == 0;
            references[kept] = references[i];
            kept += repeated ? 0 : 1;
        }
        result = kept;
    }
    return result;
}

// How many of a file's .rela.dyn and .rela.plt relocations are of `type`.
static uint64_t dynamic_test_count_relocations(const DynamicTestImage* image, uint32_t type)
{
    static const char* const sections[] = {
        ".rela.dyn",
        ".rela.plt",
    };
    uint64_t result = 0;
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(sections); s += 1)
    {
        const Elf64_Shdr* section = dynamic_test_section(image, sections[s]);
        const Elf64_Rela* rela = (const Elf64_Rela*)dynamic_test_bytes(image, section);
        const uint64_t count = section != 0 ? section->sh_size / sizeof(Elf64_Rela) : 0;
        for (uint64_t i = 0; rela != 0 && i < count; i += 1)
        {
            result += ELF64_R_TYPE(rela[i].r_info) == type ? 1 : 0;
        }
    }
    return result;
}

static void dynamic_test_against_lld(DynamicTestTally* tally, const Link* link, const Bind* bind, const Runtime* runtime, uint32_t units, String8 file, const char* directory,
                                     const char* name, bool keep, Arena* arena)
{
    const uint64_t length = arena->length;
    const uint32_t unit_count = units + RUNTIME_OBJECTS + runtime->member_count;
    char** paths = BUSTER_ARENA_PUSH_ARRAY(arena, char*, unit_count);
    char* path_bytes = arena_push(arena, (uint64_t)unit_count * BUSTER_BIND_TEST_PATH_BYTES, 1);
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)unit_count + 16);
    bool written = paths != 0 && path_bytes != 0 && arguments != 0;
    for (uint32_t unit = 0; written && unit < unit_count; unit += 1)
    {
        paths[unit] = path_bytes + (uint64_t)unit * BUSTER_BIND_TEST_PATH_BYTES;
    }
    char object_name[BUSTER_BIND_TEST_PATH_BYTES];
    snprintf(object_name, sizeof(object_name), "%s-objects", name);
    written = written && bind_test_write_objects(link, unit_count, directory, object_name, paths, arena);
    dynamic_test_expect(tally, written, "the objects written", 0);
    char program_path[BUSTER_BIND_TEST_PATH_BYTES];
    char output_path[BUSTER_BIND_TEST_PATH_BYTES];
    char error_path[BUSTER_BIND_TEST_PATH_BYTES];
    snprintf(program_path, sizeof(program_path), "%s/%s-lld", directory, name);
    snprintf(output_path, sizeof(output_path), "%s/%s-lld.out", directory, name);
    snprintf(error_path, sizeof(error_path), "%s/%s-lld.err", directory, name);
    if (written)
    {
        // With RELR, lld's too: after the output's name, before the inputs.
        const uint32_t count = bind_test_link_arguments(arguments, "ld.lld", runtime, bind, paths, units, program_path);
        const bool relr = dynamic_test_relr_supported(runtime);
        for (uint32_t a = count + 1; relr && a > 6; a -= 1)
        {
            arguments[a + 1] = arguments[a - 1];
        }
        if (relr)
        {
            arguments[6] = "-z";
            arguments[7] = "pack-relative-relocs";
        }
        // A build ID, as the writer's file has, whose note makes a segment; and a shared library's soname.
        const uint32_t last = count + (relr ? 2 : 0);
        const bool library = runtime->kind == RUNTIME_KIND_SHARED;
        arguments[last] = "--build-id";
        arguments[last + 1] = library ? "-soname" : 0;
        arguments[last + 2] = library ? BUSTER_DYNAMIC_TEST_SONAME : 0;
        arguments[last + 3] = 0;
    }
    const bool linked = written && object_test_run(arguments, output_path, error_path) == 0;
    dynamic_test_expect(tally, linked, "lld links the objects", 0);
    const String8 lld_file = linked ? object_test_read_file(arena, program_path) : (String8){ 0 };
    const DynamicTestImage ours = dynamic_test_image(file);
    const DynamicTestImage theirs = dynamic_test_image(lld_file);
    dynamic_test_expect(tally, !linked || theirs.valid, "lld's output read", 0);
    if (ours.valid && theirs.valid)
    {
        // The segments: the same program headers in the same order, with the same flags, and the PT_LOADs' alignments.
        const uint32_t our_segments = ours.header->e_phnum;
        const uint32_t their_segments = theirs.header->e_phnum;
        bool same_segments = our_segments == their_segments;
        for (uint32_t i = 0; same_segments && i < our_segments; i += 1)
        {
            const Elf64_Phdr* our_segment = &ours.program_headers[i];
            const Elf64_Phdr* their_segment = &theirs.program_headers[i];
            same_segments = our_segment->p_type == their_segment->p_type && our_segment->p_flags == their_segment->p_flags &&
                            (our_segment->p_type != PT_LOAD || our_segment->p_align == their_segment->p_align);
        }
        for (uint32_t i = 0; !same_segments && i < our_segments + their_segments; i += 1)
        {
            const Elf64_Phdr* segment = i < our_segments ? &ours.program_headers[i] : &theirs.program_headers[i - our_segments];
            printf("  %s: %s segment %u: type 0x%x, flags %u, alignment 0x%llx\n", name, i < our_segments ? "our" : "lld's", i < our_segments ? i : i - our_segments,
                   segment->p_type, segment->p_flags, (unsigned long long)segment->p_align);
        }
        dynamic_test_expect(tally, same_segments, "lld's segments", our_segments * 100 + their_segments);
        const uint64_t capacity = link->symbols.count + link->fixups.count + 16;
        DynamicTestSymbol* our_symbols = BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestSymbol, capacity);
        DynamicTestSymbol* their_symbols = BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestSymbol, capacity);
        DynamicTestImportReference* our_references = BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestImportReference, capacity);
        DynamicTestImportReference* their_references = BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestImportReference, capacity);
        const bool room = our_symbols != 0 && their_symbols != 0 && our_references != 0 && their_references != 0;
        const uint64_t our_count = room ? dynamic_test_read_symbols(&ours, our_symbols, capacity) : UINT64_MAX;
        const uint64_t their_count = room ? dynamic_test_read_symbols(&theirs, their_symbols, capacity) : UINT64_MAX;
        bool same_symbols = our_count != UINT64_MAX && our_count == their_count;
        for (uint64_t i = 0; same_symbols && i < our_count; i += 1)
        {
            same_symbols = dynamic_test_by_symbol(&our_symbols[i], &their_symbols[i]) == 0 && our_symbols[i].info == their_symbols[i].info &&
                           our_symbols[i].defined == their_symbols[i].defined;
            if (!same_symbols)
            {
                printf("  %s: .dynsym: %s@%s against lld's %s@%s\n", name, our_symbols[i].name, our_symbols[i].version, their_symbols[i].name, their_symbols[i].version);
            }
        }
        dynamic_test_expect(tally, same_symbols, "lld's dynamic symbols", our_count);
        uint64_t our_pointers = 0;
        uint64_t their_pointers = 0;
        const uint64_t our_references_count = room ? dynamic_test_read_references(&ours, our_references, capacity, &our_pointers) : UINT64_MAX;
        const uint64_t their_references_count = room ? dynamic_test_read_references(&theirs, their_references, capacity, &their_pointers) : UINT64_MAX;
        bool same_references = our_references_count != UINT64_MAX && our_references_count == their_references_count;
        for (uint64_t i = 0; same_references && i < our_references_count; i += 1)
        {
            same_references = dynamic_test_by_reference(&our_references[i], &their_references[i]) == 0;
        }
        dynamic_test_expect(tally, same_references, "lld's relocations against dynamic symbols", our_references_count);
        dynamic_test_expect(tally, our_pointers == their_pointers, "as many RELATIVE pointers as lld's", our_pointers * 100000 + their_pointers);
        // The GOT: as many slots of the image's symbols (RELATIVE) as lld's, which relaxes the same loads and calls, and
        // as many that hold values the loader leaves alone, but for our slot 0 and, in a PIE, the thread-locals'
        // offsets, whose initial-exec accesses lld relaxes to local-exec (a shared library's keep theirs, with TPOFF64
        // relocations). Its slots of dynamic symbols are the references above (lld's PLT has a slot of its own for a
        // call, which ours reads the symbol's slot for).
        const bool library = runtime->kind == RUNTIME_KIND_SHARED;
        const ExecutableTestGot our_got = executable_test_got(file, ET_DYN);
        const ExecutableTestGot their_got = executable_test_got(lld_file, ET_DYN);
        const uint64_t tls_slots = library ? 0 : executable_test_tls_slots(link);
        dynamic_test_expect(tally, our_got.valid && their_got.valid && our_got.relative == their_got.relative, "as many RELATIVE GOT slots as lld's",
                            our_got.relative * 100000 + their_got.relative);
        dynamic_test_expect(tally, our_got.plain >= 1 + tls_slots && our_got.plain - 1 - tls_slots == their_got.plain, "as many GOT slots without relocations as lld's",
                            our_got.plain * 100000 + their_got.plain);
        printf("  %s: GOT slots %llu RELATIVE, %llu by a dynamic symbol, %llu without relocations (%llu of thread-locals' offsets); lld's %llu, %llu and %llu\n", name,
               (unsigned long long)our_got.relative, (unsigned long long)our_got.symbolic, (unsigned long long)(our_got.plain - 1), (unsigned long long)tls_slots,
               (unsigned long long)their_got.relative, (unsigned long long)their_got.symbolic, (unsigned long long)their_got.plain);
        static const int64_t sizes[] = {
            DT_INIT_ARRAYSZ,
            DT_FINI_ARRAYSZ,
        };
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(sizes); t += 1)
        {
            uint64_t our_size = 0;
            uint64_t their_size = 0;
            const bool ours_tagged = dynamic_test_tag(&ours, sizes[t], &our_size);
            const bool theirs_tagged = dynamic_test_tag(&theirs, sizes[t], &their_size);
            dynamic_test_expect(tally, ours_tagged == theirs_tagged && our_size == their_size, "lld's init and fini arrays", t);
        }
        static const int64_t present[] = {
            DT_NEEDED,
            DT_INIT,
            DT_FINI,
            DT_GNU_HASH,
            DT_VERNEED,
            DT_SONAME,
            DT_FLAGS,
            DT_FLAGS_1,
            DT_DEBUG,
        };
        for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(present); t += 1)
        {
            uint64_t value = 0;
            dynamic_test_expect(tally, dynamic_test_tag(&ours, present[t], &value) == dynamic_test_tag(&theirs, present[t], &value), "lld's tags", t);
        }
        // DT_NEEDED's libraries in order (libc.so.6, and the loader when an import is its), and each one's needed
        // versions.
        const char* our_needed[4] = { 0 };
        const char* their_needed[4] = { 0 };
        const uint32_t our_needed_count = dynamic_test_needed(&ours, our_needed, BUSTER_ARRAY_LENGTH(our_needed));
        const uint32_t their_needed_count = dynamic_test_needed(&theirs, their_needed, BUSTER_ARRAY_LENGTH(their_needed));
        bool same_needed = our_needed_count == their_needed_count && our_needed_count <= BUSTER_ARRAY_LENGTH(our_needed);
        for (uint32_t n = 0; same_needed && n < our_needed_count; n += 1)
        {
            same_needed = strcmp(our_needed[n], their_needed[n]) == 0;
        }
        dynamic_test_expect(tally, same_needed, "lld's DT_NEEDED libraries, in order", our_needed_count * 100 + their_needed_count);
        // DT_FLAGS's value (a shared library's DF_STATIC_TLS), and as many TPOFF64 relocations as lld's: a shared
        // library's initial-exec slots, each of its own thread-locals' without a symbol.
        uint64_t our_flags = 0;
        uint64_t their_flags = 0;
        dynamic_test_tag(&ours, DT_FLAGS, &our_flags);
        dynamic_test_tag(&theirs, DT_FLAGS, &their_flags);
        dynamic_test_expect(tally, our_flags == their_flags, "lld's DT_FLAGS", our_flags * 100 + their_flags);
        const uint64_t our_tpoff = dynamic_test_count_relocations(&ours, R_X86_64_TPOFF64);
        const uint64_t their_tpoff = dynamic_test_count_relocations(&theirs, R_X86_64_TPOFF64);
        dynamic_test_expect(tally, our_tpoff == their_tpoff, "as many TPOFF64 relocations as lld's", our_tpoff * 100000 + their_tpoff);
        char our_versions[4096];
        char their_versions[4096];
        const bool versions_read = dynamic_test_version_needs(&ours, our_versions, sizeof(our_versions)) &&
                                   dynamic_test_version_needs(&theirs, their_versions, sizeof(their_versions));
        const bool same_versions = versions_read && strcmp(our_versions, their_versions) == 0;
        if (versions_read && !same_versions)
        {
            printf("  %s: .gnu.version_r: %s, against lld's %s\n", name, our_versions, their_versions);
        }
        dynamic_test_expect(tally, same_versions, "lld's needed versions, by library", 0);
        // .eh_frame_hdr: the same functions' FDEs, with the same ranges.
        const uint64_t fde_capacity = link->fixups.count + 16;
        DynamicTestFde* our_fdes = BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestFde, fde_capacity);
        DynamicTestFde* their_fdes = BUSTER_ARENA_PUSH_ARRAY(arena, DynamicTestFde, fde_capacity);
        const uint64_t our_fde_count = our_fdes != 0 && their_fdes != 0 ? dynamic_test_fdes(&ours, link, our_fdes, fde_capacity, arena) : UINT64_MAX;
        const uint64_t their_fde_count = our_fdes != 0 && their_fdes != 0 ? dynamic_test_fdes(&theirs, 0, their_fdes, fde_capacity, arena) : UINT64_MAX;
        bool same_fdes = our_fde_count != UINT64_MAX && our_fde_count == their_fde_count;
        for (uint64_t i = 0; same_fdes && i < our_fde_count; i += 1)
        {
            same_fdes = dynamic_test_by_fde(&our_fdes[i], &their_fdes[i]) == 0;
            if (!same_fdes)
            {
                printf("  %s: .eh_frame_hdr: %.*s, %llu bytes, against lld's %.*s, %llu bytes\n", name, (int)our_fdes[i].name_length, (const char*)our_fdes[i].name,
                       (unsigned long long)our_fdes[i].range, (int)their_fdes[i].name_length, (const char*)their_fdes[i].name, (unsigned long long)their_fdes[i].range);
            }
        }
        dynamic_test_expect(tally, same_fdes, "lld's .eh_frame_hdr indexes the same functions", our_fde_count * 100000 + their_fde_count);
    }
    if (!keep)
    {
        for (uint32_t unit = 0; written && unit < unit_count; unit += 1)
        {
            unlink(paths[unit]);
        }
        unlink(program_path);
        unlink(output_path);
        unlink(error_path);
    }
    arena_pop_to(arena, length);
}

// ---------------------------------------------------------------------------------------------------------------------
// The programs

// test-link's sets of options, and one more that calls libc with --functions workers: its table of the workers'
// addresses (buster_functions, a dense atom) is long enough that some of its targets share their bit of the writer's
// filter with a symbol outside the image, whose exact lookups it then takes.
#define BUSTER_DYNAMIC_TEST_PROGRAMS (BUSTER_LINK_TEST_PROGRAMS + 1)

// How many ABS64 fixups of the atoms the loader writes target a symbol of the image that shares its bit of the writer's
// filter (executable_filter_bit) with one outside the image (of source NONE or RUNTIME_SHARED): the filter's false
// positives among them.
static uint64_t dynamic_test_filter_collisions(const Link* link)
{
    uint64_t bits[BUSTER_EXECUTABLE_FILTER_WORDS / 2] = { 0 };
    for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
    {
        const uint8_t source = link->symbols.source[symbol];
        const uint32_t bit = executable_filter_bit(symbol);
        bits[bit / 64] |= source == BUSTER_LINK_SOURCE_NONE || source == BUSTER_LINK_SOURCE_RUNTIME_SHARED ? 1ull << (bit % 64) : 0;
    }
    uint64_t result = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        const FixupId begin = link->atoms.fixup_begin[atom];
        const FixupId end = executable_class_is_written(link->atoms.class[atom]) ? begin + link->atoms.fixup_count[atom] : begin;
        for (FixupId fixup = begin; fixup < end; fixup += 1)
        {
            const SymbolId target = link->fixups.target[fixup];
            const uint32_t bit = executable_filter_bit(target);
            const bool abs64 = (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) == BUSTER_LINK_FIXUP_ABS64;
            result += abs64 && link->symbols.atom[target] != 0 && (bits[bit / 64] >> (bit % 64) & 1) != 0 ? 1 : 0;
        }
    }
    return result;
}

static bool dynamic_test_programs(const ExecutableTestDirectory* directory, const Runtime* runtime, uint64_t seed, uint32_t functions, bool cross_check)
{
    DynamicTestTally tally = {
        .check = "programs",
    };
    LinkProgramOptions options[BUSTER_DYNAMIC_TEST_PROGRAMS];
    link_test_program_options(options, seed, functions);
    options[BUSTER_LINK_TEST_PROGRAMS] = options[5];
    options[BUSTER_LINK_TEST_PROGRAMS].seed = seed + BUSTER_LINK_TEST_PROGRAMS;
    options[BUSTER_LINK_TEST_PROGRAMS].functions = functions;
    char unwinder[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const bool unwinder_built = cross_check && dynamic_test_build_unwinder(directory->path, unwinder);
    dynamic_test_expect(&tally, !cross_check || unwinder_built, "cc builds the unwinder", 0);
    for (uint32_t i = 0; i < BUSTER_DYNAMIC_TEST_PROGRAMS; i += 1)
    {
        const bool workers = i == BUSTER_LINK_TEST_PROGRAMS;
        const char* name = workers ? "calling libc, --functions workers" : link_test_program_names[i];
        const char* directory_name = workers ? "libc-functions" : link_test_program_directories[i];
        if (options[i].libc)
        {
            BindTestProgram bound;
            const bool built = bind_test_program(&bound, runtime, options[i], BIND_PATH_VECTOR) && link_check(&bound.program.link) == LINK_ERROR_NONE;
            dynamic_test_expect(&tally, built, "the program bound", i);
            dynamic_test_expect(&tally, !built || !workers || dynamic_test_filter_collisions(&bound.program.link) != 0, "pointers the filter takes for symbols outside the image", i);
            char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
            const String8 path = executable_test_path(directory, directory_name, path_bytes);
            Scratch scratch = scratch_begin(0);
            ExecutableResult result = { 0 };
            String8 file = { 0 };
            const bool written = built && dynamic_test_lanes(&tally, &bound.program.link, &bound.bind, runtime, path, name, &result, &file, directory->id_order, scratch.arena);
            uint64_t relaxed = 0;
            if (written)
            {
                relaxed = dynamic_test_structure(&tally, &bound.program.link, &bound.bind, runtime, file, dynamic_test_relr_supported(runtime), i);
                const char* unwind = executable_test_unwind(&bound.program.link, file, 0, bound.bind.unit_base, &result);
                dynamic_test_expect(&tally, unwind == 0 && result.fdes != 0 && result.fdes_in_order, unwind != 0 ? unwind : "FDEs in order", i);
            }
            if (written && cross_check)
            {
                dynamic_test_against_lld(&tally, &bound.program.link, &bound.bind, runtime, options[i].units, file, directory->path, directory_name, directory->keep,
                                         scratch.arena);
                dynamic_test_frames(&tally, &bound.program.link, bound.bind.unit_base, path.pointer, directory->path, directory_name, directory->keep, scratch.arena);
            }
            const bool ran = written && executable_test_runs(path.pointer, bound.program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES);
            dynamic_test_expect(&tally, ran, "the program runs as its model predicts", i);
            if (written && unwinder_built)
            {
                dynamic_test_unwinder(&tally, &bound.program.link, &bound.bind, runtime, path.pointer, unwinder, bound.program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES, i);
            }
            if (written)
            {
                dynamic_test_variants(&tally, &bound.program.link, &bound.bind, runtime, path, file, bound.program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES, 0, 0, i);
            }
            printf("  %s: %u atoms, %u fixups, %llu dynamic symbols, %llu relocations in .rela.dyn (%llu relative) and %llu in %llu words of .relr.dyn, %llu stubs, %llu "
                   "loads and calls relaxed, %llu GOT slots, %llu FDEs, %llu bytes: %s\n",
                   name, bound.program.link.atoms.count - 1, bound.program.link.fixups.count - 1, (unsigned long long)result.dynamic_symbols,
                   (unsigned long long)result.relocations, (unsigned long long)result.relative_relocations, (unsigned long long)result.relr_relocations,
                   (unsigned long long)result.relr_words, (unsigned long long)result.stubs, (unsigned long long)relaxed, (unsigned long long)result.got_slots,
                   (unsigned long long)result.fdes, (unsigned long long)result.file_size, ran ? "ran" : "didn't run");
            scratch_end(scratch);
            if (!directory->keep)
            {
                unlink(path.pointer);
            }
            bind_test_release(&bound);
        }
    }
    if (unwinder_built && !directory->keep)
    {
        unlink(unwinder);
    }
    return dynamic_test_report(&tally);
}

// The atexit program and the linkers' cases: each that binds runs with the status GNU ld's and lld's links of it exit
// with.
static bool dynamic_test_cases(const ExecutableTestDirectory* directory, const Runtime* runtime, bool cross_check)
{
    DynamicTestTally tally = {
        .check = "cases",
    };
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    BindTestAtexit atexit_program;
    bind_test_atexit(&atexit_program, runtime, BIND_PATH_VECTOR, true);
    dynamic_test_expect(&tally, atexit_program.bound, "the atexit program bound", 0);
    for (uint32_t which = 0; which <= BIND_TEST_CASES; which += 1)
    {
        BindTestCaseProgram case_program;
        const bool atexit = which == BIND_TEST_CASES;
        if (!atexit)
        {
            bind_test_case_program(&case_program, runtime, (BindTestCase)which, BIND_PATH_VECTOR, true);
        }
        BindTestLink* test = atexit ? &atexit_program.test : &case_program.test;
        const bool bound = atexit ? atexit_program.bound : case_program.bound;
        const char* name = atexit ? "atexit" : bind_test_cases[which].name;
        const bool links = atexit || bind_test_cases[which].links;
        const int expected = atexit ? 0 : bind_test_cases[which].status;
        dynamic_test_expect(&tally, bound == links, "bound as GNU ld and lld link it", which);
        const String8 path = executable_test_path(directory, name, path_bytes);
        Scratch scratch = scratch_begin(0);
        ExecutableResult result = { 0 };
        String8 file = { 0 };
        const bool written = bound && dynamic_test_lanes(&tally, &test->link, &test->bind, runtime, path, name, &result, &file, directory->id_order, scratch.arena);
        if (written)
        {
            dynamic_test_structure(&tally, &test->link, &test->bind, runtime, file, dynamic_test_relr_supported(runtime), which);
        }
        if (written && cross_check)
        {
            dynamic_test_against_lld(&tally, &test->link, &test->bind, runtime, 1, file, directory->path, name, directory->keep, scratch.arena);
        }
        uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
        uint64_t length = 0;
        int status = -1;
        const bool ran = written && executable_test_run(path.pointer, output, &length, &status);
        const bool exited = ran && WIFEXITED(status) && WEXITSTATUS(status) == expected && length == 0;
        dynamic_test_expect(&tally, !bound || exited, "runs with the linkers' status", which);
        if (written)
        {
            dynamic_test_variants(&tally, &test->link, &test->bind, runtime, path, file, (const uint8_t*)"", 0, expected, 0, which);
        }
        printf("  %s: %s\n", name, !bound ? "refused" : exited ? "ran" : "didn't run");
        scratch_end(scratch);
        if (!directory->keep)
        {
            unlink(path.pointer);
        }
        if (!atexit)
        {
            bind_test_link_end(&case_program.test);
        }
    }
    bind_test_link_end(&atexit_program.test);
    return dynamic_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The exports

// The exports' program: definitions of names that libc.so.6 defines too, which binding exports, each a function that
// returns, and a main that looks each up with dlsym(RTLD_DEFAULT, name), which searches the executable first, through
// its .gnu.hash: main returns 0 if every lookup found the program's own definition, 1 otherwise. Sixteen exports make
// four buckets and four Bloom words.
static const char* const dynamic_test_export_names[] = {
    "ffs",
    "ffsl",
    "ffsll",
    "abs",
    "labs",
    "llabs",
    "imaxabs",
    "toascii",
    "isascii",
    "swab",
    "insque",
    "remque",
    "rand_r",
    "strverscmp",
    "l64a",
    "a64l",
};
#define BUSTER_DYNAMIC_TEST_EXPORTS BUSTER_ARRAY_LENGTH(dynamic_test_export_names)
// main's instructions: two before the lookups, six per lookup, and five after.
#define BUSTER_DYNAMIC_TEST_MAIN_CODE (2 + 6 * BUSTER_DYNAMIC_TEST_EXPORTS + 5)

// A TEXT atom of `count` instructions with their references, which defines `symbol`.
static bool dynamic_test_code(BindTestLink* test, const X86Instruction* instructions, uint32_t count, const LinkReference* references, uint32_t reference_count, SymbolId symbol)
{
    uint8_t encoded[BUSTER_X86_ENCODER_CAPACITY(BUSTER_DYNAMIC_TEST_MAIN_CODE)];
    uint32_t ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_DYNAMIC_TEST_MAIN_CODE)];
    const int64_t length = x86_encode(instructions, count, encoded, sizeof(encoded), 0, ends);
    uint8_t* bytes = length > 0 ? arena_push(&test->bytes, (uint64_t)length, 16) : 0;
    const AtomId atom = bytes != 0 ? link_add_atom(&test->link, BUSTER_LINK_CLASS_TEXT, 4, (uint64_t)length, bytes, 0) : 0;
    if (atom != 0)
    {
        memcpy(bytes, encoded, (uint64_t)length);
    }
    return atom != 0 && link_add_code_fixups(&test->link, atom, instructions, count, ends, reference_count, references, reference_count) &&
           link_define_symbol(&test->link, symbol, atom, 0, (uint64_t)length);
}

typedef struct DynamicTestExports DynamicTestExports;
struct DynamicTestExports
{
    BindTestLink test;
    SymbolId functions[BUSTER_DYNAMIC_TEST_EXPORTS];
    bool bound;
};

static void dynamic_test_exports_program(DynamicTestExports* program, const Runtime* runtime)
{
    BindTestLink* test = &program->test;
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const LinkCapacity extra = bind_capacity(runtime, &name_bytes, &atom_bytes);
    const uint32_t own = 4 * BUSTER_DYNAMIC_TEST_EXPORTS + 16;
    const LinkCapacity capacity = {
        .names = extra.names + own,
        .atoms = extra.atoms + own,
        .symbols = extra.symbols + own,
        .fixups = extra.fixups + own,
        .wide_addends = extra.wide_addends + own,
    };
    *test = (BindTestLink){ 0 };
    test->tables = arena_reserve(link_capacity_bytes(capacity));
    test->names = arena_reserve(name_bytes + 4096);
    test->bytes = arena_reserve(atom_bytes + 65536);
    test->arena = arena_reserve(BUSTER_BIND_TEST_BIND_BYTES);
    bool built = test->names.base != 0 && test->bytes.base != 0 && test->arena.base != 0 && link_initialize(&test->link, &test->tables, &test->names, capacity);
    const uint8_t program_source = BUSTER_LINK_SOURCE_PROGRAM;
    const SymbolId main = built ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program_source) : 0;
    const SymbolId dlsym = built ? bind_test_symbol(test, "dlsym", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_NONE) : 0;
    const SymbolId strings = built ? bind_test_symbol(test, "buster_export_names", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT, program_source) : 0;
    built = main != 0 && dlsym != 0 && strings != 0;
    for (uint32_t e = 0; built && e < BUSTER_DYNAMIC_TEST_EXPORTS; e += 1)
    {
        program->functions[e] = bind_test_symbol(test, dynamic_test_export_names[e], BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program_source);
        built = program->functions[e] != 0;
    }
    built = built && bind_begin(&test->bind, &test->link, runtime, BIND_PATH_VECTOR, &test->bytes, 1, &test->arena);
    // The names, one after another with their NULs, in an atom of STRINGS.
    uint32_t offsets[BUSTER_DYNAMIC_TEST_EXPORTS];
    uint64_t total = 0;
    for (uint32_t e = 0; e < BUSTER_DYNAMIC_TEST_EXPORTS; e += 1)
    {
        offsets[e] = (uint32_t)total;
        total += strlen(dynamic_test_export_names[e]) + 1;
    }
    uint8_t* bytes = built ? arena_push(&test->bytes, total, 1) : 0;
    for (uint32_t e = 0; bytes != 0 && e < BUSTER_DYNAMIC_TEST_EXPORTS; e += 1)
    {
        memcpy(bytes + offsets[e], dynamic_test_export_names[e], strlen(dynamic_test_export_names[e]) + 1);
    }
    const AtomId names = bytes != 0 ? link_add_atom(&test->link, BUSTER_LINK_CLASS_STRINGS, 0, total, bytes, 0) : 0;
    built = names != 0 && link_define_symbol(&test->link, strings, names, 0, total);
    // main: push rbx; xor ebx, ebx; then per export xor edi, edi; lea rsi, [rip + its name]; call [rip + dlsym@GOT];
    // lea rcx, [rip + its function]; xor rax, rcx; or rbx, rax; and last xor eax, eax; test rbx, rbx; setne al;
    // pop rbx; ret.
    X86Instruction code[BUSTER_DYNAMIC_TEST_MAIN_CODE];
    LinkReference references[3 * BUSTER_DYNAMIC_TEST_EXPORTS];
    uint32_t count = 0;
    uint32_t reference_count = 0;
    code[count++] = (X86Instruction){ .rm = X86_REGISTER_RBX, .form = X86_FORM_PUSH_R64 };
    code[count++] = (X86Instruction){ .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_XOR_R32_RM32 };
    for (uint32_t e = 0; e < BUSTER_DYNAMIC_TEST_EXPORTS; e += 1)
    {
        code[count++] = (X86Instruction){ .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_XOR_R32_RM32 };
        references[reference_count++] = (LinkReference){ .instruction = count, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_PC32, .target = strings, .addend = offsets[e] };
        code[count++] = (X86Instruction){ .reg = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP };
        references[reference_count++] = (LinkReference){ .instruction = count, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_CALL_GOT, .target = dlsym, .addend = 0 };
        code[count++] = (X86Instruction){ .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_CALL_RM64 | BUSTER_X86_FORM_FIXUP };
        references[reference_count++] =
            (LinkReference){ .instruction = count, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_PC32, .target = program->functions[e], .addend = 0 };
        code[count++] = (X86Instruction){ .reg = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP };
        code[count++] = (X86Instruction){ .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_XOR_R64_RM64 };
        code[count++] = (X86Instruction){ .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_OR_R64_RM64 };
    }
    code[count++] = (X86Instruction){ .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_XOR_R32_RM32 };
    code[count++] = (X86Instruction){ .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_TEST_RM64_R64 };
    code[count++] = (X86Instruction){ .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_SETNE_RM8 };
    code[count++] = (X86Instruction){ .rm = X86_REGISTER_RBX, .form = X86_FORM_POP_R64 };
    code[count++] = (X86Instruction){ .form = X86_FORM_RET };
    built = built && dynamic_test_code(test, code, count, references, reference_count, main);
    const X86Instruction ret = {
        .form = X86_FORM_RET,
    };
    for (uint32_t e = 0; built && e < BUSTER_DYNAMIC_TEST_EXPORTS; e += 1)
    {
        built = dynamic_test_code(test, &ret, 1, 0, 0, program->functions[e]);
    }
    program->bound = built && bind_end(&test->bind, &test->link) && link_check(&test->link) == LINK_ERROR_NONE;
}

// The exports' program: binding exports exactly its sixteen functions, and it runs with status 0, every dlsym having
// found the program's own definition through .gnu.hash.
static bool dynamic_test_exports(const ExecutableTestDirectory* directory, const Runtime* runtime, bool cross_check)
{
    DynamicTestTally tally = {
        .check = "exports",
    };
    DynamicTestExports program;
    dynamic_test_exports_program(&program, runtime);
    BindTestLink* test = &program.test;
    dynamic_test_expect(&tally, program.bound, "the exports' program bound", 0);
    bool exported = program.bound && test->bind.export_count == BUSTER_DYNAMIC_TEST_EXPORTS;
    for (uint32_t e = 0; exported && e < BUSTER_DYNAMIC_TEST_EXPORTS; e += 1)
    {
        bool found = false;
        for (uint32_t x = 0; x < test->bind.export_count; x += 1)
        {
            found = found || test->bind.exports[x] == program.functions[e];
        }
        exported = found;
    }
    dynamic_test_expect(&tally, exported, "its functions binding's exports", 0);
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "exports", path_bytes);
    Scratch scratch = scratch_begin(0);
    ExecutableResult result = { 0 };
    String8 file = { 0 };
    const bool written = program.bound && dynamic_test_lanes(&tally, &test->link, &test->bind, runtime, path, "exports", &result, &file, directory->id_order, scratch.arena);
    if (written)
    {
        dynamic_test_structure(&tally, &test->link, &test->bind, runtime, file, dynamic_test_relr_supported(runtime), 0);
    }
    // Against lld before the variants, whose write without RELR moves the atoms that the comparison finds the file's
    // functions by.
    if (written && cross_check)
    {
        dynamic_test_against_lld(&tally, &test->link, &test->bind, runtime, 1, file, directory->path, "exports", directory->keep, scratch.arena);
    }
    if (written)
    {
        dynamic_test_variants(&tally, &test->link, &test->bind, runtime, path, file, (const uint8_t*)"", 0, 0, 0, 0);
    }
    uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
    uint64_t length = 0;
    int status = -1;
    const bool ran = written && executable_test_run(path.pointer, output, &length, &status);
    const bool found = ran && WIFEXITED(status) && WEXITSTATUS(status) == 0 && length == 0;
    dynamic_test_expect(&tally, found, "dlsym finds every export in the executable", (uint64_t)status);
    printf("  %u exports, %llu dynamic symbols: %s\n", test->bind.export_count, (unsigned long long)result.dynamic_symbols, found ? "each found" : "not found");
    scratch_end(scratch);
    if (!directory->keep)
    {
        unlink(path.pointer);
    }
    bind_test_link_end(test);
    return dynamic_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The imports

// The imports' program: references to BUSTER_DYNAMIC_TEST_IMPORTS of libc.so.6's functions (the first exports of
// FUNC type whose names don't start with '_'), a function that never runs calling each, so that each has a stub, and
// .data atoms of BUSTER_DYNAMIC_TEST_POINTERS pointers each, one to each import, so that each has an R_X86_64_64; main
// returns 0. The imports' symbols are consecutive, over several of the symbols' words, and the pointers' atoms
// consecutive, so that several lanes number stubs and fill relocations; and the loader must bind them all, at the
// versions they need.
#define BUSTER_DYNAMIC_TEST_IMPORTS 256
#define BUSTER_DYNAMIC_TEST_POINTERS 8

typedef struct DynamicTestImports DynamicTestImports;
struct DynamicTestImports
{
    BindTestLink test;
    uint32_t count;
    bool bound;
};

static void dynamic_test_imports_program(DynamicTestImports* program, const Runtime* runtime)
{
    BindTestLink* test = &program->test;
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const LinkCapacity extra = bind_capacity(runtime, &name_bytes, &atom_bytes);
    const uint32_t own = 3 * BUSTER_DYNAMIC_TEST_IMPORTS + 64;
    const LinkCapacity capacity = {
        .names = extra.names + own,
        .atoms = extra.atoms + own,
        .symbols = extra.symbols + own,
        .fixups = extra.fixups + own,
        .wide_addends = extra.wide_addends + own,
    };
    *test = (BindTestLink){ 0 };
    test->tables = arena_reserve(link_capacity_bytes(capacity));
    test->names = arena_reserve(name_bytes + 65536);
    test->bytes = arena_reserve(atom_bytes + (1ull << 20));
    test->arena = arena_reserve(BUSTER_BIND_TEST_BIND_BYTES);
    bool built = test->names.base != 0 && test->bytes.base != 0 && test->arena.base != 0 && link_initialize(&test->link, &test->tables, &test->names, capacity);
    const uint8_t program_source = BUSTER_LINK_SOURCE_PROGRAM;
    const SymbolId main = built ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program_source) : 0;
    const SymbolId never = built ? bind_test_symbol(test, "buster_never", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION, program_source) : 0;
    built = main != 0 && never != 0;
    // The imports, by the export table's order.
    SymbolId imports[BUSTER_DYNAMIC_TEST_IMPORTS];
    const ImportExports* exports = &runtime->libc.exports;
    program->count = 0;
    for (uint32_t e = 0; built && e < exports->count && program->count < BUSTER_DYNAMIC_TEST_IMPORTS; e += 1)
    {
        const bool function = (exports->flags[e] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_FUNCTION;
        if (function && exports->length[e] != 0 && exports->name[e][0] != '_')
        {
            const NameId name = link_add_name(&test->link, exports->name[e], exports->length[e]);
            imports[program->count] = name != 0 ? link_add_symbol(&test->link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_NONE) : 0;
            built = imports[program->count] != 0;
            program->count += 1;
        }
    }
    built = built && program->count == BUSTER_DYNAMIC_TEST_IMPORTS;
    built = built && bind_begin(&test->bind, &test->link, runtime, BIND_PATH_VECTOR, &test->bytes, 1, &test->arena);
    const X86Instruction main_code[] = {
        { .immediate = 0, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
        { .form = X86_FORM_RET },
    };
    built = built && bind_test_code(test, main_code, BUSTER_ARRAY_LENGTH(main_code), 0, 0, main);
    // buster_never: a call of each import, then ret.
    X86Instruction calls[BUSTER_DYNAMIC_TEST_IMPORTS + 1];
    LinkReference references[BUSTER_DYNAMIC_TEST_IMPORTS];
    for (uint32_t i = 0; i < BUSTER_DYNAMIC_TEST_IMPORTS; i += 1)
    {
        calls[i] = (X86Instruction){ .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP };
        references[i] = (LinkReference){ .instruction = i, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_CALL, .target = built ? imports[i] : 0, .addend = 0 };
    }
    calls[BUSTER_DYNAMIC_TEST_IMPORTS] = (X86Instruction){ .form = X86_FORM_RET };
    uint8_t encoded[BUSTER_X86_ENCODER_CAPACITY(BUSTER_DYNAMIC_TEST_IMPORTS + 1)];
    uint32_t ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_DYNAMIC_TEST_IMPORTS + 1)];
    const int64_t length = built ? x86_encode(calls, BUSTER_DYNAMIC_TEST_IMPORTS + 1, encoded, sizeof(encoded), 0, ends) : 0;
    uint8_t* code = length > 0 ? arena_push(&test->bytes, (uint64_t)length, 16) : 0;
    const AtomId code_atom = code != 0 ? link_add_atom(&test->link, BUSTER_LINK_CLASS_TEXT, 4, (uint64_t)length, code, 0) : 0;
    if (code_atom != 0)
    {
        memcpy(code, encoded, (uint64_t)length);
    }
    built = code_atom != 0 && link_add_code_fixups(&test->link, code_atom, calls, BUSTER_DYNAMIC_TEST_IMPORTS + 1, ends, BUSTER_DYNAMIC_TEST_IMPORTS, references, BUSTER_DYNAMIC_TEST_IMPORTS) &&
            link_define_symbol(&test->link, never, code_atom, 0, (uint64_t)length);
    // The pointers, BUSTER_DYNAMIC_TEST_POINTERS to an atom of .data.
    for (uint32_t i = 0; built && i < BUSTER_DYNAMIC_TEST_IMPORTS; i += BUSTER_DYNAMIC_TEST_POINTERS)
    {
        uint8_t* bytes = arena_push(&test->bytes, 8 * BUSTER_DYNAMIC_TEST_POINTERS, 8);
        const AtomId atom = bytes != 0 ? link_add_atom(&test->link, BUSTER_LINK_CLASS_DATA, 3, 8 * BUSTER_DYNAMIC_TEST_POINTERS, bytes, 0) : 0;
        if (bytes != 0)
        {
            memset(bytes, 0, 8 * BUSTER_DYNAMIC_TEST_POINTERS);
        }
        built = atom != 0;
        for (uint32_t p = 0; built && p < BUSTER_DYNAMIC_TEST_POINTERS; p += 1)
        {
            built = link_add_fixup(&test->link, atom, 8 * p, BUSTER_LINK_FIXUP_ABS64, imports[i + p], 0) != 0;
        }
    }
    program->bound = built && bind_end(&test->bind, &test->link) && link_check(&test->link) == LINK_ERROR_NONE;
}

// The imports' program: each import a stub and an R_X86_64_64, written alike by every lane count, and it runs, the
// loader having bound every import.
static bool dynamic_test_imports(const ExecutableTestDirectory* directory, const Runtime* runtime, bool cross_check)
{
    DynamicTestTally tally = {
        .check = "imports",
    };
    DynamicTestImports program;
    dynamic_test_imports_program(&program, runtime);
    BindTestLink* test = &program.test;
    dynamic_test_expect(&tally, program.bound, "the imports' program bound", program.count);
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "imports", path_bytes);
    Scratch scratch = scratch_begin(0);
    ExecutableResult result = { 0 };
    String8 file = { 0 };
    const bool written = program.bound && dynamic_test_lanes(&tally, &test->link, &test->bind, runtime, path, "imports", &result, &file, directory->id_order, scratch.arena);
    if (written)
    {
        dynamic_test_structure(&tally, &test->link, &test->bind, runtime, file, dynamic_test_relr_supported(runtime), 0);
    }
    // Against lld before the variants, whose write without RELR moves the atoms that the comparison finds the file's
    // functions by.
    if (written && cross_check)
    {
        dynamic_test_against_lld(&tally, &test->link, &test->bind, runtime, 1, file, directory->path, "imports", directory->keep, scratch.arena);
    }
    if (written)
    {
        dynamic_test_variants(&tally, &test->link, &test->bind, runtime, path, file, (const uint8_t*)"", 0, 0, 0, 0);
    }
    // At least a stub and a slot for each import, which crtbeginS.o's and the members' calls add to.
    dynamic_test_expect(&tally, result.stubs >= BUSTER_DYNAMIC_TEST_IMPORTS && result.relocations >= 2 * BUSTER_DYNAMIC_TEST_IMPORTS, "a stub, a slot and a pointer for each import",
                        result.stubs);
    const bool ran = written && executable_test_runs(path.pointer, (const uint8_t*)"", 0);
    dynamic_test_expect(&tally, ran, "runs, every import bound", 0);
    printf("  %u imports: %llu stubs, %llu relocations in .rela.dyn (%llu relative) and %llu in .relr.dyn, %llu dynamic symbols: %s\n", program.count,
           (unsigned long long)result.stubs, (unsigned long long)result.relocations, (unsigned long long)result.relative_relocations, (unsigned long long)result.relr_relocations,
           (unsigned long long)result.dynamic_symbols, ran ? "ran" : "didn't run");
    scratch_end(scratch);
    if (!directory->keep)
    {
        unlink(path.pointer);
    }
    bind_test_link_end(test);
    return dynamic_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The refusals

// Small programs, each with something a dynamic PIE must refuse or handle: a pointer to main in .rodata (a text
// relocation), an ABS32 or a PC32 in .data to main or to a weak reference to nothing with default visibility, which is
// dynamic; a call to that weak reference, which goes to a stub (in a function that never runs); a pointer to it in
// .data, which the loader leaves 0 since nothing defines it, and which main returns whether it isn't; loads through the
// GOT of it and of the program's data (by GOTPCREL, which doesn't relax), whose slots share a word of the bitsets, the
// dynamic one first, so that the GOT's RELR words start past the word's first slot, and a load of the data that relaxes
// to a lea; a pointer to main at an odd address, after a byte of .data, which
// RELR can't hold, so .rela.dyn does, and which main returns whether it isn't main's address; and a hidden weak
// reference to nothing, at address 0 wherever the program loads: main's lea of it and a PC64 to it in .data, which no
// load address makes right (GNU ld refuses the lea), a call to it in the function that never runs, and a pointer to
// it, which needs no relocation, and which main returns whether it isn't 0. Then an absolute symbol at 42, which is its
// value wherever the program loads, as lld leaves it: a pointer to it in .data and in .rodata, a load of it through the
// GOT and an ABS32 to it in .data, none of which needs a relocation, each of which main returns whether it isn't 42;
// and main's lea of it, a call to it and a PC64 to it in .data, which no load address makes right (GNU ld and lld refuse
// them).
typedef enum DynamicTestSmall
{
    DYNAMIC_TEST_SMALL_TEXT_POINTER,
    DYNAMIC_TEST_SMALL_ABSOLUTE32,
    DYNAMIC_TEST_SMALL_PC32_DYNAMIC,
    DYNAMIC_TEST_SMALL_CALL_WEAK,
    DYNAMIC_TEST_SMALL_WEAK_POINTER,
    DYNAMIC_TEST_SMALL_GOT_ORDER,
    DYNAMIC_TEST_SMALL_ODD_POINTER,
    DYNAMIC_TEST_SMALL_HIDDEN_LEA,
    DYNAMIC_TEST_SMALL_HIDDEN_PC64,
    DYNAMIC_TEST_SMALL_HIDDEN_CALL,
    DYNAMIC_TEST_SMALL_HIDDEN_POINTER,
    DYNAMIC_TEST_SMALL_ABSOLUTE_POINTER,
    DYNAMIC_TEST_SMALL_ABSOLUTE_TEXT_POINTER,
    DYNAMIC_TEST_SMALL_ABSOLUTE_GOT,
    DYNAMIC_TEST_SMALL_ABSOLUTE_ABS32,
    DYNAMIC_TEST_SMALL_ABSOLUTE_LEA,
    DYNAMIC_TEST_SMALL_ABSOLUTE_CALL,
    DYNAMIC_TEST_SMALL_ABSOLUTE_PC64,
    DYNAMIC_TEST_SMALLS,
} DynamicTestSmall;

static const char* const dynamic_test_small_names[DYNAMIC_TEST_SMALLS] = {
    [DYNAMIC_TEST_SMALL_TEXT_POINTER] = "a pointer to main in .rodata",
    [DYNAMIC_TEST_SMALL_ABSOLUTE32] = "an ABS32 to main in .data",
    [DYNAMIC_TEST_SMALL_PC32_DYNAMIC] = "a PC32 to a dynamic symbol in .data",
    [DYNAMIC_TEST_SMALL_CALL_WEAK] = "a call to a weak reference to nothing",
    [DYNAMIC_TEST_SMALL_WEAK_POINTER] = "a pointer to a weak reference to nothing",
    [DYNAMIC_TEST_SMALL_GOT_ORDER] = "loads through the GOT, a dynamic symbol's slot first",
    [DYNAMIC_TEST_SMALL_ODD_POINTER] = "a pointer to main at an odd address",
    [DYNAMIC_TEST_SMALL_HIDDEN_LEA] = "a lea of a hidden weak reference to nothing",
    [DYNAMIC_TEST_SMALL_HIDDEN_PC64] = "a PC64 to a hidden weak reference to nothing in .data",
    [DYNAMIC_TEST_SMALL_HIDDEN_CALL] = "a call to a hidden weak reference to nothing",
    [DYNAMIC_TEST_SMALL_HIDDEN_POINTER] = "a pointer to a hidden weak reference to nothing",
    [DYNAMIC_TEST_SMALL_ABSOLUTE_POINTER] = "a pointer to an absolute symbol in .data",
    [DYNAMIC_TEST_SMALL_ABSOLUTE_TEXT_POINTER] = "a pointer to an absolute symbol in .rodata",
    [DYNAMIC_TEST_SMALL_ABSOLUTE_GOT] = "a load of an absolute symbol through the GOT",
    [DYNAMIC_TEST_SMALL_ABSOLUTE_ABS32] = "an ABS32 to an absolute symbol in .data",
    [DYNAMIC_TEST_SMALL_ABSOLUTE_LEA] = "a lea of an absolute symbol",
    [DYNAMIC_TEST_SMALL_ABSOLUTE_CALL] = "a call to an absolute symbol",
    [DYNAMIC_TEST_SMALL_ABSOLUTE_PC64] = "a PC64 to an absolute symbol in .data",
};

// The absolute symbol's value in the small programs that have one.
#define BUSTER_DYNAMIC_TEST_ABSOLUTE_VALUE 42

// Whether some word of 64 symbols has GOT slots, by the test's own rules, whose first isn't relocated RELATIVE and a
// later one that is.
static bool dynamic_test_got_order(const Link* link)
{
    bool result = false;
    for (uint64_t first = 0; first < link->symbols.count; first += 64)
    {
        bool marked = false;
        bool other_first = false;
        for (uint64_t symbol = first; symbol < first + 64 && symbol < link->symbols.count; symbol += 1)
        {
            const bool slot = link->symbols.got_slot[symbol] != 0;
            const bool relative = slot && dynamic_test_is_relative(link, (SymbolId)symbol, false);
            result = result || (other_first && relative);
            other_first = other_first || (slot && !marked && !relative);
            marked = marked || slot;
        }
    }
    return result;
}

// The starved lane's room: at most this much, checked in steps of this much near the least it links with.
#define BUSTER_DYNAMIC_TEST_ROOM_STEP 64
#define BUSTER_DYNAMIC_TEST_MAX_ROOM (1ull << 20)

typedef struct DynamicTestSmallProgram DynamicTestSmallProgram;
struct DynamicTestSmallProgram
{
    BindTestLink test;
    // The fixup that must be refused, or 0 for a program that runs.
    FixupId refused;
    // buster_data's atom, which holds the pointer.
    AtomId data;
    bool bound;
};

static void dynamic_test_small(DynamicTestSmallProgram* program, const Runtime* runtime, DynamicTestSmall which)
{
    BindTestLink* test = &program->test;
    program->refused = 0;
    bool built = bind_test_link_begin(test, runtime, bind_test_room(runtime));
    const uint8_t program_source = BUSTER_LINK_SOURCE_PROGRAM;
    // The weak reference first, so that its GOT slot comes before main's (which Scrt1.o loads through the GOT) and
    // buster_data's in their word.
    const SymbolId weak = built ? bind_test_symbol(test, "buster_weak", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_NONE) : 0;
    const SymbolId main = built ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program_source) : 0;
    const SymbolId data = built ? bind_test_symbol(test, "buster_data", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT, program_source) : 0;
    const SymbolId never = built ? bind_test_symbol(test, "buster_never", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION, program_source) : 0;
    const SymbolId hidden =
        built ? bind_test_symbol(test, "buster_hidden", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_HIDDEN | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_NONE) : 0;
    // The absolute symbol, last, so that the other programs' symbols keep their numbers.
    const bool absolute_case = which >= DYNAMIC_TEST_SMALL_ABSOLUTE_POINTER;
    const SymbolId absolute =
        built && absolute_case ? bind_test_symbol(test, "buster_absolute", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_ABSOLUTE) : 0;
    if (absolute != 0)
    {
        test->link.symbols.value[absolute] = BUSTER_DYNAMIC_TEST_ABSOLUTE_VALUE;
    }
    built = main != 0 && weak != 0 && data != 0 && never != 0 && hidden != 0 && (!absolute_case || absolute != 0) &&
            bind_begin(&test->bind, &test->link, runtime, BIND_PATH_VECTOR, &test->bytes, 1, &test->arena);
    // main: return 0, or for the weak pointers whether they aren't 0, for the odd pointer whether it isn't main's
    // address, or for the absolute symbol's whether it isn't its value: mov rdx, [rip + buster_data] (for the load
    // through the GOT, [rip + buster_absolute's slot]); lea rax, [rip + main] (for the leas, of buster_hidden or
    // buster_absolute); sub rdx, rax for the odd pointer, sub rdx, 42 for the absolute symbol, and a nop otherwise;
    // mov eax, 1; mov ecx, 0; test rdx, rdx; cmove rax, rcx; ret.
    const bool reads = which == DYNAMIC_TEST_SMALL_WEAK_POINTER || which == DYNAMIC_TEST_SMALL_ODD_POINTER || which == DYNAMIC_TEST_SMALL_HIDDEN_POINTER || absolute_case;
    const bool lea = which == DYNAMIC_TEST_SMALL_HIDDEN_LEA || which == DYNAMIC_TEST_SMALL_ABSOLUTE_LEA;
    const bool odd = which == DYNAMIC_TEST_SMALL_ODD_POINTER;
    const X86Instruction difference = odd             ? (X86Instruction){ .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_SUB_R64_RM64 }
                                      : absolute_case ? (X86Instruction){ .immediate = BUSTER_DYNAMIC_TEST_ABSOLUTE_VALUE, .rm = X86_REGISTER_RDX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_SUB_RM64_IMM8 }
                                                      : (X86Instruction){ .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RDX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_MOV_R64_RM64 };
    const X86Instruction main_code[] = {
        { .reg = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_MOV_R64_RM64 | BUSTER_X86_FORM_FIXUP },
        { .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP },
        difference,
        { .immediate = reads ? 1 : 0, .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
        { .immediate = 0, .rm = X86_REGISTER_RCX, .form = X86_FORM_MOV_R32_IMM32 },
        { .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RDX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_TEST_RM64_R64 },
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_CMOVE_R64_RM64 },
        { .form = X86_FORM_RET },
    };
    const bool absolute_slot = which == DYNAMIC_TEST_SMALL_ABSOLUTE_GOT;
    const LinkReference main_references[] = {
        absolute_slot ? (LinkReference){ .instruction = 0, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_LOAD_GOT, .target = absolute, .addend = 0 }
                      : (LinkReference){ .instruction = 0, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_PC32, .target = data, .addend = 0 },
        { .instruction = 1, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_PC32, .target = !lea ? main : absolute_case ? absolute : hidden, .addend = 0 },
    };
    // Or for the loads through the GOT: mov rdx, [rip + buster_weak's slot] (0); mov rax, [rip + buster_data's slot],
    // which GOTPCREL reads, so that it doesn't relax; mov rcx, [rip + buster_data's slot], which relaxes to lea rcx,
    // [rip + buster_data]; sub rax, rcx (0); mov rcx, [rcx] (0); add rax, rcx; add rax, rdx; ret. A slot relocated wrong,
    // or a load relaxed wrong, exits with another status, or faults.
    const X86Instruction got_code[] = {
        { .reg = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_MOV_R64_RM64 | BUSTER_X86_FORM_FIXUP },
        { .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_MOV_R64_RM64 | BUSTER_X86_FORM_FIXUP },
        { .reg = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_MOV_R64_RM64 | BUSTER_X86_FORM_FIXUP },
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_SUB_R64_RM64 },
        { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 },
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_ADD_R64_RM64 },
        { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDX, .mode = BUSTER_X86_REGISTER_DIRECT, .form = X86_FORM_ADD_R64_RM64 },
        { .form = X86_FORM_RET },
    };
    const LinkReference got_references[] = {
        { .instruction = 0, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_LOAD_GOT, .target = weak, .addend = 0 },
        { .instruction = 1, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_GOTPCREL, .target = data, .addend = 0 },
        { .instruction = 2, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_LOAD_GOT, .target = data, .addend = 0 },
    };
    const bool loads = which == DYNAMIC_TEST_SMALL_GOT_ORDER;
    built = built && (loads ? bind_test_code(test, got_code, BUSTER_ARRAY_LENGTH(got_code), got_references, BUSTER_ARRAY_LENGTH(got_references), main)
                            : bind_test_code(test, main_code, BUSTER_ARRAY_LENGTH(main_code), main_references, BUSTER_ARRAY_LENGTH(main_references), main));
    // The lea's fixup, main's second.
    const FixupId lea_fixup = built && lea ? test->link.atoms.fixup_begin[test->link.symbols.atom[main]] + 1 : 0;
    // buster_never: call buster_weak; ret. It is only ever called by nothing.
    const X86Instruction never_code[] = {
        { .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP },
        { .form = X86_FORM_RET },
    };
    const SymbolId callee = which == DYNAMIC_TEST_SMALL_HIDDEN_CALL ? hidden : which == DYNAMIC_TEST_SMALL_ABSOLUTE_CALL ? absolute : weak;
    const LinkReference never_references[] = {
        { .instruction = 0, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_CALL, .target = callee, .addend = 0 },
    };
    const bool calls = which == DYNAMIC_TEST_SMALL_CALL_WEAK || which == DYNAMIC_TEST_SMALL_HIDDEN_CALL || which == DYNAMIC_TEST_SMALL_ABSOLUTE_CALL;
    built = built && bind_test_code(test, never_code, BUSTER_ARRAY_LENGTH(never_code), never_references, calls ? 1 : 0, never);
    // The call's fixup, buster_never's only one.
    const FixupId call_fixup = built && calls ? test->link.atoms.fixup_begin[test->link.symbols.atom[never]] : 0;
    // buster_data: 8 bytes in .rodata for the text relocations, in .data otherwise, with the case's fixup; for the odd
    // pointer aligned to a byte, after an atom of a byte, so that it lands an odd address past the even end of the
    // larger alignments' atoms.
    uint8_t* bytes = built ? arena_push(&test->bytes, 9, 8) : 0;
    const bool text = which == DYNAMIC_TEST_SMALL_TEXT_POINTER || which == DYNAMIC_TEST_SMALL_ABSOLUTE_TEXT_POINTER;
    const uint8_t class = text ? BUSTER_LINK_CLASS_RODATA : BUSTER_LINK_CLASS_DATA;
    const AtomId pad = bytes != 0 && odd ? link_add_atom(&test->link, BUSTER_LINK_CLASS_DATA, 0, 1, bytes + 8, 0) : 0;
    const AtomId atom = bytes != 0 && (!odd || pad != 0) ? link_add_atom(&test->link, class, odd ? 0 : 3, 8, bytes, 0) : 0;
    if (bytes != 0)
    {
        memset(bytes, 0, 9);
    }
    built = atom != 0 && link_define_symbol(&test->link, data, atom, 0, 8);
    program->data = atom;
    static const uint8_t kinds[DYNAMIC_TEST_SMALLS] = {
        [DYNAMIC_TEST_SMALL_TEXT_POINTER] = BUSTER_LINK_FIXUP_ABS64,
        [DYNAMIC_TEST_SMALL_ABSOLUTE32] = BUSTER_LINK_FIXUP_ABS32,
        [DYNAMIC_TEST_SMALL_PC32_DYNAMIC] = BUSTER_LINK_FIXUP_PC32,
        [DYNAMIC_TEST_SMALL_CALL_WEAK] = 0,
        [DYNAMIC_TEST_SMALL_WEAK_POINTER] = BUSTER_LINK_FIXUP_ABS64,
        [DYNAMIC_TEST_SMALL_GOT_ORDER] = 0,
        [DYNAMIC_TEST_SMALL_ODD_POINTER] = BUSTER_LINK_FIXUP_ABS64,
        [DYNAMIC_TEST_SMALL_HIDDEN_LEA] = 0,
        [DYNAMIC_TEST_SMALL_HIDDEN_PC64] = BUSTER_LINK_FIXUP_PC64,
        [DYNAMIC_TEST_SMALL_HIDDEN_CALL] = 0,
        [DYNAMIC_TEST_SMALL_HIDDEN_POINTER] = BUSTER_LINK_FIXUP_ABS64,
        [DYNAMIC_TEST_SMALL_ABSOLUTE_POINTER] = BUSTER_LINK_FIXUP_ABS64,
        [DYNAMIC_TEST_SMALL_ABSOLUTE_TEXT_POINTER] = BUSTER_LINK_FIXUP_ABS64,
        [DYNAMIC_TEST_SMALL_ABSOLUTE_GOT] = 0,
        [DYNAMIC_TEST_SMALL_ABSOLUTE_ABS32] = BUSTER_LINK_FIXUP_ABS32,
        [DYNAMIC_TEST_SMALL_ABSOLUTE_LEA] = 0,
        [DYNAMIC_TEST_SMALL_ABSOLUTE_CALL] = 0,
        [DYNAMIC_TEST_SMALL_ABSOLUTE_PC64] = BUSTER_LINK_FIXUP_PC64,
    };
    const bool to_hidden = which == DYNAMIC_TEST_SMALL_HIDDEN_PC64 || which == DYNAMIC_TEST_SMALL_HIDDEN_POINTER;
    const SymbolId target = which == DYNAMIC_TEST_SMALL_PC32_DYNAMIC || which == DYNAMIC_TEST_SMALL_WEAK_POINTER ? weak
                            : to_hidden                                                                         ? hidden
                            : absolute_case                                                                     ? absolute
                                                                                                                : main;
    const FixupId fixup = built && kinds[which] != 0 ? link_add_fixup(&test->link, atom, 0, kinds[which], target, 0) : 0;
    built = built && (kinds[which] == 0 || fixup != 0);
    const bool absolute_runs = absolute_case && which != DYNAMIC_TEST_SMALL_ABSOLUTE_LEA && which != DYNAMIC_TEST_SMALL_ABSOLUTE_CALL && which != DYNAMIC_TEST_SMALL_ABSOLUTE_PC64;
    const bool runs = which == DYNAMIC_TEST_SMALL_CALL_WEAK || which == DYNAMIC_TEST_SMALL_WEAK_POINTER || which == DYNAMIC_TEST_SMALL_GOT_ORDER || odd ||
                      which == DYNAMIC_TEST_SMALL_HIDDEN_CALL || which == DYNAMIC_TEST_SMALL_HIDDEN_POINTER || absolute_runs;
    program->refused = runs ? 0 : lea ? lea_fixup : which == DYNAMIC_TEST_SMALL_ABSOLUTE_CALL ? call_fixup : fixup;
    program->bound = built && bind_end(&test->bind, &test->link) && link_check(&test->link) == LINK_ERROR_NONE;
}

// The lanes short of scratch memory (dynamic_test_starve): lane 0 of 1 and of 8 lanes, and the last of 2 and of 8.
static const uint32_t dynamic_test_starved[][2] = {
    { 1, 0 },
    { 8, 0 },
    { 2, 1 },
    { 8, 7 },
};

// A link written with case `c` of dynamic_test_starved's lanes short of scratch memory: every write that fails must
// fail with EXECUTABLE_ERROR_MEMORY and leave no output, and one with room enough must succeed, which it returns, its
// output at `path`. The room a lane needs isn't quite the same from link to link (a lane that takes a chunk needs room
// for its runs, and the lanes take them as they come), so a bisection over multiples of 64 bytes (which leave the
// room's start alike aligned) only finds about the least, and the rooms tried are every KB up to it and every 64 bytes
// from 4 KB below it to 1 KB above.
static bool dynamic_test_starve(DynamicTestTally* tally, Link* link, const Bind* bind, const Runtime* runtime, String8 path, uint32_t c)
{
    const uint32_t lanes = dynamic_test_starved[c][0];
    DynamicTestWrite write = {
        .starve = true,
        .starved_lane = dynamic_test_starved[c][1],
    };
    ExecutableResult result;
    uint64_t low = 0;
    uint64_t high = BUSTER_DYNAMIC_TEST_MAX_ROOM / BUSTER_DYNAMIC_TEST_ROOM_STEP;
    write.room = high * BUSTER_DYNAMIC_TEST_ROOM_STEP;
    const bool enough = dynamic_test_write_with(&write, link, bind, runtime, path, lanes, &result);
    while (enough && low < high)
    {
        const uint64_t middle = low + (high - low) / 2;
        write.room = middle * BUSTER_DYNAMIC_TEST_ROOM_STEP;
        unlink(path.pointer);
        const bool succeeded = dynamic_test_write_with(&write, link, bind, runtime, path, lanes, &result);
        low = succeeded ? low : middle + 1;
        high = succeeded ? middle : high;
    }
    const uint64_t least = high * BUSTER_DYNAMIC_TEST_ROOM_STEP;
    uint64_t wrong = UINT64_MAX;
    for (uint64_t room = 0; enough && room < least + 1024; room += room + 4096 < least ? 1024 : BUSTER_DYNAMIC_TEST_ROOM_STEP)
    {
        unlink(path.pointer);
        write.room = room;
        const bool succeeded = dynamic_test_write_with(&write, link, bind, runtime, path, lanes, &result);
        const bool clean = succeeded || (result.error == EXECUTABLE_ERROR_MEMORY && access(path.pointer, F_OK) != 0);
        wrong = !clean && wrong == UINT64_MAX ? room : wrong;
    }
    write.room = BUSTER_DYNAMIC_TEST_MAX_ROOM;
    unlink(path.pointer);
    const bool succeeded = enough && dynamic_test_write_with(&write, link, bind, runtime, path, lanes, &result);
    dynamic_test_expect(tally, wrong == UINT64_MAX, "a lane short of memory fails the link with no output", wrong);
    return succeeded;
}

// Each small program refused with its fixup by every lane count, without an output, or written and run with status 0.
static bool dynamic_test_smalls(const ExecutableTestDirectory* directory, const Runtime* runtime)
{
    DynamicTestTally tally = {
        .check = "refusals",
    };
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "small", path_bytes);
    for (uint32_t which = 0; which < DYNAMIC_TEST_SMALLS; which += 1)
    {
        DynamicTestSmallProgram program;
        dynamic_test_small(&program, runtime, (DynamicTestSmall)which);
        BindTestLink* test = &program.test;
        dynamic_test_expect(&tally, program.bound, dynamic_test_small_names[which], which);
        bool outcome = program.bound;
        // By every lane count, then by the exact twins with 1 and 8 lanes.
        for (uint32_t l = 0; program.bound && program.refused != 0 && l < BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts) + 2; l += 1)
        {
            const bool exact = l >= BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts);
            const uint32_t lanes = exact ? (l == BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts) ? 1 : 8) : dynamic_test_lane_counts[l];
            DynamicTestWrite write = {
                .exact = exact,
            };
            ExecutableResult result;
            unlink(path.pointer);
            const bool written = dynamic_test_write_with(&write, &test->link, &test->bind, runtime, path, lanes, &result);
            const bool refused = !written && result.error == EXECUTABLE_ERROR_RELOCATION && result.fixup == program.refused && access(path.pointer, F_OK) != 0;
            dynamic_test_expect(&tally, refused, dynamic_test_small_names[which], (exact ? 100 : 0) + lanes);
            outcome = outcome && refused;
        }
        if (program.bound && program.refused == 0)
        {
            Scratch scratch = scratch_begin(0);
            ExecutableResult result = { 0 };
            String8 file = { 0 };
            const bool written = dynamic_test_lanes(&tally, &test->link, &test->bind, runtime, path, dynamic_test_small_names[which], &result, &file, directory->id_order, scratch.arena);
            if (written)
            {
                dynamic_test_structure(&tally, &test->link, &test->bind, runtime, file, dynamic_test_relr_supported(runtime), which);
                dynamic_test_variants(&tally, &test->link, &test->bind, runtime, path, file, (const uint8_t*)"", 0, 0, 0, which);
            }
            dynamic_test_expect(&tally, !written || which != DYNAMIC_TEST_SMALL_GOT_ORDER || dynamic_test_got_order(&test->link), "a dynamic symbol's slot first in a word",
                                which);
            dynamic_test_expect(&tally, !written || which != DYNAMIC_TEST_SMALL_ODD_POINTER || (test->link.atoms.address[program.data] & 1) != 0, "the pointer at an odd address",
                                which);
            uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
            uint64_t length = 0;
            int status = -1;
            outcome = written && executable_test_run(path.pointer, output, &length, &status) && WIFEXITED(status) && WEXITSTATUS(status) == 0 && length == 0;
            dynamic_test_expect(&tally, outcome, "runs with status 0", which);
            scratch_end(scratch);
        }
        // A lane short of scratch memory (dynamic_test_starve), with which the program must run once it has room.
        for (uint32_t c = 0; program.bound && which == DYNAMIC_TEST_SMALL_WEAK_POINTER && c < BUSTER_ARRAY_LENGTH(dynamic_test_starved); c += 1)
        {
            const bool succeeded = dynamic_test_starve(&tally, &test->link, &test->bind, runtime, path, c);
            dynamic_test_expect(&tally, succeeded && executable_test_runs(path.pointer, (const uint8_t*)"", 0), "then succeeds with room enough", c);
        }
        printf("  %s: %s\n", dynamic_test_small_names[which], !outcome ? "wrong" : program.refused != 0 ? "refused" : "ran");
        unlink(path.pointer);
        bind_test_link_end(test);
    }
    return dynamic_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared libraries

// A shared library's named default-visible absolute symbol is dynamic. Its direct CALL must still be refused, by
// the vector count and the exact count alike. Build the link here: an assembler may fold or reject the absolute CALL.
static bool dynamic_test_shared_absolute_call(const ExecutableTestDirectory* directory, const Runtime* runtime)
{
    DynamicTestTally tally = {
        .check = "shared absolute call",
    };
    BindTestLink test;
    bool built = bind_test_link_begin(&test, runtime, bind_test_room(runtime));
    const SymbolId absolute =
        built ? bind_test_symbol(&test, "buster_absolute", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_ABSOLUTE) : 0;
    const SymbolId never = built ? bind_test_symbol(&test, "buster_never", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM) : 0;
    if (absolute != 0)
    {
        test.link.symbols.value[absolute] = BUSTER_DYNAMIC_TEST_ABSOLUTE_VALUE;
    }
    built = absolute != 0 && never != 0 && bind_begin(&test.bind, &test.link, runtime, BIND_PATH_VECTOR, &test.bytes, 1, &test.arena);
    const X86Instruction code[] = {
        { .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP },
        { .form = X86_FORM_RET },
    };
    const LinkReference references[] = {
        { .instruction = 0, .field = X86_FIXUP_DISPLACEMENT, .kind = BUSTER_LINK_FIXUP_CALL, .target = absolute, .addend = 0 },
    };
    built = built && bind_test_code(&test, code, BUSTER_ARRAY_LENGTH(code), references, BUSTER_ARRAY_LENGTH(references), never);
    const FixupId refused = built ? test.link.atoms.fixup_begin[test.link.symbols.atom[never]] : 0;
    built = built && refused != 0 && bind_end(&test.bind, &test.link) && link_check(&test.link) == LINK_ERROR_NONE;
    dynamic_test_expect(&tally, built, "the absolute CALL bound", 0);
    dynamic_test_expect(&tally, built && dynamic_test_is_dynamic(&test.link, absolute, true), "the absolute symbol is preemptible", 0);
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "shared-absolute-call", path_bytes);
    for (uint32_t l = 0; built && l < BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts) + 2; l += 1)
    {
        const bool exact = l >= BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts);
        const uint32_t lanes = exact ? (l == BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts) ? 1 : 8) : dynamic_test_lane_counts[l];
        DynamicTestWrite write = {
            .exact = exact,
        };
        ExecutableResult result;
        unlink(path.pointer);
        const bool written = dynamic_test_write_with(&write, &test.link, &test.bind, runtime, path, lanes, &result);
        const bool clean = !written && result.error == EXECUTABLE_ERROR_RELOCATION && result.fixup == refused && access(path.pointer, F_OK) != 0;
        dynamic_test_expect(&tally, clean, "the absolute CALL refused without output", (exact ? 100 : 0) + lanes);
    }
    bind_test_link_end(&test);
    return dynamic_test_report(&tally);
}

// test-link's shared libraries (link_test_shared_options) and one of --functions workers, each bound against the shared
// library's runtime (runtime_import_shared) and written by every lane count, which the harness must load and run as the
// model predicts.
static bool dynamic_test_shared(const ExecutableTestDirectory* directory, const Runtime* runtime, uint64_t seed, uint32_t functions, bool cross_check)
{
    DynamicTestTally tally = {
        .check = "shared libraries",
    };
    char harness[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const bool harnessed = object_test_harness(directory->path, harness, sizeof(harness), directory->keep);
    dynamic_test_expect(&tally, harnessed, "cc builds the harness", 0);
    LinkProgramOptions options[BUSTER_LINK_TEST_SHARED_PROGRAMS + 1];
    link_test_shared_options(options, seed, functions);
    options[BUSTER_LINK_TEST_SHARED_PROGRAMS] = options[0];
    options[BUSTER_LINK_TEST_SHARED_PROGRAMS].seed = seed + BUSTER_LINK_TEST_ALL_PROGRAMS;
    options[BUSTER_LINK_TEST_SHARED_PROGRAMS].functions = functions;
    for (uint32_t i = 0; i < BUSTER_LINK_TEST_SHARED_PROGRAMS + 1; i += 1)
    {
        const bool workers = i == BUSTER_LINK_TEST_SHARED_PROGRAMS;
        const char* name = workers ? "a shared library, --functions workers" : link_test_shared_names[i];
        const char* directory_name = workers ? "shared-functions" : link_test_shared_directories[i];
        BindTestProgram bound;
        const bool built = bind_test_program(&bound, runtime, options[i], BIND_PATH_VECTOR) && link_check(&bound.program.link) == LINK_ERROR_NONE;
        dynamic_test_expect(&tally, built, "the library bound", i);
        char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        const String8 path = executable_test_path(directory, directory_name, path_bytes);
        Scratch scratch = scratch_begin(0);
        ExecutableResult result = { 0 };
        String8 file = { 0 };
        const bool written = built && dynamic_test_lanes(&tally, &bound.program.link, &bound.bind, runtime, path, name, &result, &file, directory->id_order, scratch.arena);
        if (written)
        {
            dynamic_test_structure(&tally, &bound.program.link, &bound.bind, runtime, file, dynamic_test_relr_supported(runtime), i);
        }
        if (written && cross_check)
        {
            dynamic_test_against_lld(&tally, &bound.program.link, &bound.bind, runtime, options[i].units, file, directory->path, directory_name, directory->keep, scratch.arena);
        }
        const bool loaded = written && harnessed && dynamic_test_loads(harness, path.pointer, bound.program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES);
        dynamic_test_expect(&tally, loaded, "the library loads and runs as its model predicts", i);
        if (written && harnessed)
        {
            dynamic_test_variants(&tally, &bound.program.link, &bound.bind, runtime, path, file, bound.program.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES, 0, harness, i);
        }
        printf("  %s: %u atoms, %u fixups, %llu dynamic symbols, %llu relocations in .rela.dyn (%llu relative) and %llu in %llu words of .relr.dyn, %llu stubs, %llu GOT "
               "slots, %llu bytes: %s\n",
               name, bound.program.link.atoms.count - 1, bound.program.link.fixups.count - 1, (unsigned long long)result.dynamic_symbols,
               (unsigned long long)result.relocations, (unsigned long long)result.relative_relocations, (unsigned long long)result.relr_relocations,
               (unsigned long long)result.relr_words, (unsigned long long)result.stubs, (unsigned long long)result.got_slots, (unsigned long long)result.file_size,
               loaded ? "loaded" : "didn't load");
        scratch_end(scratch);
        if (!directory->keep)
        {
            unlink(path.pointer);
        }
        bind_test_release(&bound);
    }
    if (harnessed && !directory->keep)
    {
        unlink(harness);
    }
    return dynamic_test_report(&tally);
}

// The C libraries the compiled check builds, each with a buster_check that the program calls with its own buster_helper
// and buster_data, which preempt the library's: their checks fail by a count. Each compiled with `pic` (-fPIC, or the
// code a shared library refuses) and `option`; `refused` if a shared library can't relocate it, and `lld_refuses` if
// lld's -shared refuses it too.
typedef struct DynamicTestSharedSource DynamicTestSharedSource;
struct DynamicTestSharedSource
{
    const char* name;
    const char* source;
    const char* pic;
    const char* option;
    bool refused;
    bool lld_refuses;
};

static const DynamicTestSharedSource dynamic_test_shared_sources[] = {
    {
        .name = "preemption",
        .source = "int buster_helper(void) { return 1; }\n"
                  "int buster_data = 5;\n"
                  "__attribute__((visibility(\"hidden\"))) int buster_hidden(void) { return 7; }\n"
                  "__attribute__((visibility(\"protected\"))) int buster_protected(void) { return 3; }\n"
                  "__thread int buster_thread = 4;\n"
                  "static __thread int buster_own_thread = 6;\n"
                  "extern int buster_nowhere(void) __attribute__((weak));\n"
                  "int (*const buster_table[])(void) = { buster_helper, buster_hidden, buster_protected };\n"
                  "static int buster_count;\n"
                  "int buster_check(int (*helper)(void), int* data)\n"
                  "{\n"
                  "    int failures = 0;\n"
                  "    failures += buster_helper() != 2;\n"
                  "    failures += buster_helper != helper;\n"
                  "    failures += &buster_data != data || buster_data != 9;\n"
                  "    failures += buster_table[0] != helper || buster_table[1]() != 7 || buster_table[2]() != 3;\n"
                  "    failures += buster_hidden() != 7 || buster_protected() != 3;\n"
                  "    failures += buster_thread != 4 || buster_own_thread != 6;\n"
                  "    buster_thread += 1;\n"
                  "    buster_own_thread += 2;\n"
                  "    failures += buster_thread != 5 || buster_own_thread != 8;\n"
                  "    failures += &buster_nowhere != 0;\n"
                  "    failures += ++buster_count != 1;\n"
                  "    return failures;\n"
                  "}\n",
        // Its thread-locals by initial-exec accesses: general- and local-dynamic ones come next.
        .pic = "-fPIC",
        .option = "-ftls-model=initial-exec",
    },
    {
        .name = "atexit",
        .source = "#include <stdlib.h>\n"
                  "static void buster_handler(void) {}\n"
                  "int buster_check(int (*helper)(void), int* data)\n"
                  "{\n"
                  "    return atexit(buster_handler) != 0 || helper() != 2 || *data != 9;\n"
                  "}\n",
        .pic = "-fPIC",
    },
    {
        // A protected function's address by a lea (PC32): no other object preempts it.
        .name = "protected-lea",
        .source = "__attribute__((visibility(\"protected\"))) int buster_protected(void) { return 3; }\n"
                  "int buster_check(int (*helper)(void), int* data)\n"
                  "{\n"
                  "    int (*volatile pointer)(void) = buster_protected;\n"
                  "    return pointer() != 3 || helper() != 2 || *data != 9;\n"
                  "}\n",
        .pic = "-fPIC",
    },
    {
        // -fPIE code takes its preemptible function's address by a lea (PC32): lld says to recompile it with -fPIC.
        .name = "pie-code",
        .source = "int buster_exported(void) { return 1; }\n"
                  "int buster_check(int (*helper)(void), int* data)\n"
                  "{\n"
                  "    int (*volatile pointer)(void) = buster_exported;\n"
                  "    return pointer() != 1 || helper() != 2 || *data != 9;\n"
                  "}\n",
        .pic = "-fPIE",
        .refused = true,
        .lld_refuses = true,
    },
    {
        // A local-exec access (TPOFF32), whose offset from the thread pointer a shared library can't know.
        .name = "local-exec",
        .source = "__thread int buster_thread = 4;\n"
                  "int buster_check(int (*helper)(void), int* data) { return buster_thread != 4 || helper() != 2 || *data != 9; }\n",
        .pic = "-fPIC",
        .option = "-ftls-model=local-exec",
        .refused = true,
        .lld_refuses = true,
    },
    {
        // A pointer to a preemptible function in .rodata (a text relocation), from code without -fPIC.
        .name = "text-relocation",
        .source = "int buster_exported(void) { return 1; }\n"
                  "int (*const buster_table[])(void) = { buster_exported };\n"
                  "int buster_check(int (*helper)(void), int* data) { return buster_table[0]() != 1 || helper() != 2 || *data != 9; }\n",
        .pic = "-fno-pic",
        .refused = true,
        .lld_refuses = true,
    },
    {
        // A general-dynamic access (TLSGD and its call of __tls_get_addr), whose GOT pair comes in the next part.
        .name = "general-dynamic",
        .source = "__thread int buster_thread = 4;\n"
                  "int buster_check(int (*helper)(void), int* data) { return buster_thread != 4 || helper() != 2 || *data != 9; }\n",
        .pic = "-fPIC",
        .option = "-mtls-dialect=gnu",
        .refused = true,
    },
};

// The program that loads a compiled library: it defines buster_helper and buster_data, which -rdynamic exports, so
// that they preempt the library's, and returns the count of the library's failed checks, or 99 if it can't load it.
static const char dynamic_test_shared_program[] = "#include <dlfcn.h>\n"
                                                  "#include <stdio.h>\n"
                                                  "int buster_helper(void) { return 2; }\n"
                                                  "int buster_data = 9;\n"
                                                  "int main(int argc, char** argv)\n"
                                                  "{\n"
                                                  "    void* library = argc >= 2 ? dlopen(argv[1], RTLD_NOW) : 0;\n"
                                                  "    int (*check)(int (*)(void), int*) = library != 0 ? (int (*)(int (*)(void), int*))dlsym(library, \"buster_check\") : 0;\n"
                                                  "    if (check == 0)\n"
                                                  "    {\n"
                                                  "        fprintf(stderr, \"%s\\n\", dlerror());\n"
                                                  "    }\n"
                                                  "    return check != 0 ? check(buster_helper, &buster_data) : 99;\n"
                                                  "}\n";

// Whether the program at `program` loads the library at `library` and exits with status 0: every check held.
static bool dynamic_test_checks_hold(const char* program, const char* library)
{
    uint8_t output[BUSTER_EXECUTABLE_TEST_OUTPUT_BYTES];
    uint64_t length = 0;
    int status = -1;
    char* arguments[] = { (char*)program, (char*)library, 0 };
    char* environment[] = { "BUSTER=1", 0 };
    const bool ran = executable_test_run_arguments(arguments, environment, output, &length, &status);
    return ran && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// The compiled libraries (dynamic_test_shared_sources): each compiled by cc with -fPIC, imported as the program, bound
// against the shared library's runtime and written by every lane count, read back by the structure's rules, and
// loaded by the program (dynamic_test_shared_program, which cc builds with -rdynamic), whose checks of preemption must
// hold. With `cross_check`, lld links the same object with cc -shared, whose library must pass the same checks, and the
// bound link's objects as dynamic_test_against_lld compares.
static bool dynamic_test_shared_compiled(const ExecutableTestDirectory* directory, const Runtime* runtime, bool cross_check)
{
    DynamicTestTally tally = {
        .check = "compiled shared libraries",
    };
    char program_source[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char program[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char out[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    char err[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    snprintf(program_source, sizeof(program_source), "%s/shared-loader.c", directory->path);
    snprintf(program, sizeof(program), "%s/shared-loader", directory->path);
    snprintf(out, sizeof(out), "%s/shared-compiled.out", directory->path);
    snprintf(err, sizeof(err), "%s/shared-compiled.err", directory->path);
    FILE* program_file = fopen(program_source, "w");
    const bool program_written = program_file != 0 && fputs(dynamic_test_shared_program, program_file) >= 0;
    if (program_file != 0)
    {
        fclose(program_file);
    }
    char* build[] = { "cc", "-O1", "-rdynamic", "-o", program, program_source, 0 };
    const bool built = program_written && object_test_run(build, out, err) == 0;
    dynamic_test_expect(&tally, built, "cc builds the program that loads the libraries", 0);
    for (uint32_t s = 0; built && s < BUSTER_ARRAY_LENGTH(dynamic_test_shared_sources); s += 1)
    {
        const DynamicTestSharedSource* library = &dynamic_test_shared_sources[s];
        Scratch scratch = scratch_begin(0);
        char source_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char object_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char lld_path[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        char name[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        snprintf(source_path, sizeof(source_path), "%s/shared-%s.c", directory->path, library->name);
        snprintf(object_path, sizeof(object_path), "%s/shared-%s.o", directory->path, library->name);
        snprintf(lld_path, sizeof(lld_path), "%s/shared-%s-lld.so", directory->path, library->name);
        snprintf(name, sizeof(name), "shared-%s.so", library->name);
        FILE* file = fopen(source_path, "w");
        const bool written = file != 0 && fputs(library->source, file) >= 0;
        if (file != 0)
        {
            fclose(file);
        }
        char* compile[] = { "cc", "-O2", (char*)library->pic, "-c", "-o", object_path, source_path, (char*)library->option, 0 };
        const bool compiled = written && object_test_run(compile, out, err) == 0;
        dynamic_test_expect(&tally, compiled, library->name, s);
        const String8 object = compiled ? object_test_read_file(scratch.arena, object_path) : (String8){ 0 };
        BindTestLink bound;
        const bool imported = object.pointer != 0 && bind_test_static_import(&bound, runtime, object);
        const bool bound_ok = imported && bind_begin(&bound.bind, &bound.link, runtime, BIND_PATH_VECTOR, &bound.bytes, 1, &bound.arena) && bind_end(&bound.bind, &bound.link) &&
                              link_check(&bound.link) == LINK_ERROR_NONE;
        dynamic_test_expect(&tally, bound_ok, "imported and bound", s);
        char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
        const String8 path = executable_test_path(directory, name, path_bytes);
        ExecutableResult result = { 0 };
        String8 bytes = { 0 };
        // A refused one by every lane count, then by the exact twins with 1 and 8 lanes, each without an output.
        bool refused = bound_ok;
        for (uint32_t l = 0; bound_ok && library->refused && l < BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts) + 2; l += 1)
        {
            const bool exact = l >= BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts);
            const uint32_t lanes = exact ? (l == BUSTER_ARRAY_LENGTH(dynamic_test_lane_counts) ? 1 : 8) : dynamic_test_lane_counts[l];
            DynamicTestWrite write = {
                .exact = exact,
            };
            ExecutableResult refusal;
            unlink(path.pointer);
            const bool written = dynamic_test_write_with(&write, &bound.link, &bound.bind, runtime, path, lanes, &refusal);
            const bool clean = !written && refusal.error == EXECUTABLE_ERROR_RELOCATION && refusal.fixup != 0 && access(path.pointer, F_OK) != 0;
            dynamic_test_expect(&tally, clean, "refused by every lane count", (exact ? 100 : 0) + lanes);
            refused = refused && clean;
        }
        const bool linked = bound_ok && !library->refused && dynamic_test_lanes(&tally, &bound.link, &bound.bind, runtime, path, library->name, &result, &bytes, directory->id_order, scratch.arena);
        if (linked)
        {
            dynamic_test_structure(&tally, &bound.link, &bound.bind, runtime, bytes, dynamic_test_relr_supported(runtime), s);
        }
        const bool held = library->refused ? refused : linked && dynamic_test_checks_hold(program, path.pointer);
        dynamic_test_expect(&tally, held, library->refused ? "refused" : "the program loads it and its checks hold", s);
        // A lane short of scratch memory, with which the first library must still load and hold.
        for (uint32_t c = 0; linked && s == 0 && c < BUSTER_ARRAY_LENGTH(dynamic_test_starved); c += 1)
        {
            const bool succeeded = dynamic_test_starve(&tally, &bound.link, &bound.bind, runtime, path, c);
            dynamic_test_expect(&tally, succeeded && dynamic_test_checks_hold(program, path.pointer), "then succeeds with room enough", c);
        }
        if (bound_ok && cross_check)
        {
            char* lld[] = { "cc", "-shared", "-fuse-ld=lld", "-o", lld_path, object_path, 0 };
            const bool lld_linked = object_test_run(lld, out, err) == 0;
            dynamic_test_expect(&tally, lld_linked == !library->lld_refuses, "lld links it or refuses it alike", s);
            dynamic_test_expect(&tally, !lld_linked || dynamic_test_checks_hold(program, lld_path), "lld's library passes the same checks", s);
            if (linked)
            {
                dynamic_test_against_lld(&tally, &bound.link, &bound.bind, runtime, 1, bytes, directory->path, name, directory->keep, scratch.arena);
            }
        }
        printf("  %s: %u atoms, %u fixups, %llu dynamic symbols, %llu relocations in .rela.dyn and %llu in .relr.dyn, %llu stubs, %llu GOT slots: %s\n", library->name,
               bound.link.atoms.count - 1, bound.link.fixups.count - 1, (unsigned long long)result.dynamic_symbols, (unsigned long long)result.relocations,
               (unsigned long long)result.relr_relocations, (unsigned long long)result.stubs, (unsigned long long)result.got_slots,
               !held ? "failed" : library->refused ? "refused" : "its checks held");
        if (imported)
        {
            bind_test_link_end(&bound);
        }
        scratch_end(scratch);
        if (!directory->keep)
        {
            unlink(source_path);
            unlink(object_path);
            unlink(lld_path);
            unlink(path.pointer);
        }
    }
    if (!directory->keep)
    {
        unlink(program_source);
        unlink(program);
        unlink(out);
        unlink(err);
    }
    return dynamic_test_report(&tally);
}

// A runtime that didn't import, named.
static void dynamic_test_report_import(RuntimeError error, const Runtime* runtime)
{
    if (error != RUNTIME_ERROR_NONE)
    {
        fprintf(stderr, "the shared library's runtime didn't import: error %d, %s\n", (int)error, runtime->failed_file);
    }
}

static int run_dynamic_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t id_order = 153;
    uint64_t functions = BUSTER_EXECUTABLE_TEST_DEFAULT_FUNCTIONS;
    const char* parent = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    bool keep = false;
    bool cross_check = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && executable_test_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--id-order") == 0 && i + 1 < argc)
        {
            valid = valid && executable_test_parse_u64(argv[i + 1], &id_order);
            i += 1;
        }
        else if (strcmp(argv[i], "--functions") == 0 && i + 1 < argc)
        {
            valid = valid && executable_test_parse_u64(argv[i + 1], &functions) && functions >= 1 && functions <= BUSTER_LINK_PROGRAM_MAX_COUNT;
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
        {
            parent = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else
        {
            valid = false;
        }
    }
    ExecutableTestDirectory directory = {
        .keep = keep,
        .id_order = id_order,
    };
    const int length = snprintf(directory.path, sizeof(directory.path), "%s/buster-dynamic-XXXXXX", parent);
    int result = 1;
    Runtime runtime;
    const RuntimeError imported = valid ? runtime_import(&runtime) : RUNTIME_ERROR_NONE;
    if (!valid)
    {
        fprintf(stderr, "usage: test-dynamic [--seed N] [--id-order N] [--functions N] [--directory PATH] [--cross-check] [--keep]\n");
    }
    else if (imported != RUNTIME_ERROR_NONE)
    {
        fprintf(stderr, "the runtime didn't import: error %d, %s\n", (int)imported, runtime.failed_file);
    }
    else if (length <= 0 || (uint64_t)length + 64 >= sizeof(directory.path) || mkdtemp(directory.path) == 0)
    {
        fprintf(stderr, "test-dynamic: can't make a directory in %s\n", parent);
    }
    else if (!x86_encoder_initialized && !x86_encoder_initialize())
    {
        fprintf(stderr, "the encoder's templates can't be built\n");
    }
    else
    {
        bool passed = dynamic_test_programs(&directory, &runtime, seed, (uint32_t)functions, cross_check);
        passed = dynamic_test_cases(&directory, &runtime, cross_check) && passed;
        passed = dynamic_test_exports(&directory, &runtime, cross_check) && passed;
        passed = dynamic_test_imports(&directory, &runtime, cross_check) && passed;
        passed = dynamic_test_smalls(&directory, &runtime) && passed;
        Runtime shared_runtime;
        const RuntimeError shared_imported = runtime_import_shared(&shared_runtime);
        dynamic_test_report_import(shared_imported, &shared_runtime);
        passed = shared_imported == RUNTIME_ERROR_NONE && dynamic_test_shared_absolute_call(&directory, &shared_runtime) && passed;
        passed = shared_imported == RUNTIME_ERROR_NONE && dynamic_test_shared(&directory, &shared_runtime, seed, (uint32_t)functions, cross_check) && passed;
        passed = shared_imported == RUNTIME_ERROR_NONE && dynamic_test_shared_compiled(&directory, &shared_runtime, cross_check) && passed;
        if (shared_imported == RUNTIME_ERROR_NONE)
        {
            runtime_release(&shared_runtime);
        }
        result = passed ? 0 : 1;
        if (keep)
        {
            printf("kept %s\n", directory.path);
        }
        else
        {
            rmdir(directory.path);
        }
    }
    if (valid)
    {
        runtime_release(&runtime);
    }
    return result;
}
