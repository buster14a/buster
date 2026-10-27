// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The import path's tests, for the test-import command:
//
//   build/compiler test-import [--seed N] [--cross-check]
//
// main.c includes this file after import.c and runtime.c, and after the object writer, whose objects the round trips
// read back, and the encoder's tests, whose guarded mappings the page edges use. The checks, in this order:
//   - Round trips: the object of every unit of test-link's programs (link_test_program_options), with and without
//     function sections, and the object writer's unit of 33,000 functions in sections of their own (past
//     SHN_LORESERVE sections: e_shnum and e_shstrndx in section 0, symbols through .symtab_shndx), each imported into a
//     link of its own. Each section the object writer made of the unit's atoms becomes one atom, and each of the unit's
//     atoms must be there, byte for byte, where the writer placed it (ObjectUnits.atom_section and atom_offset). Each
//     symbol the unit defines must be at the same place, with the same name (".LC" and a number for the writer's
//     unnamed locals), type, binding, visibility and size. Each fixup must be at the same place, of the same kind,
//     pointing at the same place with the same addend: the writer may refer to a local through its section's symbol,
//     which imports as an unnamed local at its atom's start, and to the unit's global and undefined symbols by name.
//   - Objects made by hand (import_test_object): one with a section of every class the importer gives, sections it
//     drops (.sframe with its relocations, a note, .note.GNU-stack and .comment), every kind of symbol (FILE, section
//     symbols, among them a dropped section's and a TLS section's, locals, an unnamed one, a dropped section's local,
//     globals of every visibility, weak ones and undefined ones) and every relocation type the representation has, its
//     .rela.text out of order, with R_X86_64_NONE and addends that need the side table: each section, symbol and fixup
//     imported exactly, the atoms' bytes in place in the file. The same with e_shnum and e_shstrndx in section 0, with
//     .eh_frame as SHT_PROGBITS and with tables' sh_entsize 0. One without a symbol table, as crtn.o is. Then each check
//     a well-formed object never trips, one change at a time, with the error and the place it reports.
//   - Archives made by hand: a 32-bit symbol index, a 64-bit one and none, long names, an odd member's padding and a
//     member of no bytes; then malformed ones.
//   - Shared libraries made by hand: version definitions, a name at a default version and at a hidden one, IFUNC,
//     GNU_UNIQUE, COMMON, TLS and absolute symbols, weak and protected ones, hidden and local ones and one at
//     VER_NDX_LOCAL, which aren't exported, references, and a name exported twice; the same without .gnu.version; then
//     malformed ones.
//   - Linker scripts: libc.so's and GCC's _asneeded ones, commas, comments and OUTPUT_FORMAT, then each error.
//   - Page edges: every prefix of each input made by hand, and the whole input with each byte set to 0, 0xff and a
//     random value, ending at a PROT_NONE page, through every reader: each succeeds, or fails reporting a place within
//     its input, without reading past the end (a read there faults; the command then names the input and exits with
//     status 1).
//   - Discovery: GCC's search under a directory of the test's own, whose newer version's directory has no crtbeginS.o:
//     the search picks the older version and probes the newer one's crtbeginS.o, so that installing it leaves the
//     probes stale (runtime_probes_current) and a fresh search picks it.
//   - The runtime (runtime_import on this system's files): GCC's installation and the crt objects found and probed;
//     each crt object imported with what it is for (_start; _init and _fini in .init and .fini atoms; __dso_handle and
//     __TMC_END__); libc.so.6's soname, its exports at their default versions (write, strlen, environ and
//     __errno_location at GLIBC_2.2.5, __libc_start_main at GLIBC_2.34) and GLIBC_ABI_DT_RELR; the loader's soname and
//     exports (__tls_get_addr at GLIBC_2.3, _r_debug and __libc_stack_end at GLIBC_2.2.5), its version indices past
//     libc.so.6's, and its names' exports in the global names the loader's; libgcc_s.so.1's soname and exports
//     (_Unwind_Resume and __udivti3 at GCC_3.0, _Unwind_Backtrace at GCC_3.3, __gcc_personality_v0 at GCC_3.3.1), its
//     version indices past both's, none of its exports one of theirs, _Unwind_Backtrace's export in the global names
//     its, and __udivti3's its after libgcc.a's member (import_test_runtime_gcc_s); libc_nonshared.a's index (atexit);
//     the ABI note; the bindings of every reference to each name: main to the program, __libc_start_main and
//     __cxa_finalize to libc, __gmon_start__ and the _ITM_ functions to nothing, __TMC_END__ to crtendS.o, and
//     _GLOBAL_OFFSET_TABLE_ to the linker; the objects' pieces, one after another; and the global names binding looks a
//     program's up in, each once, found by its bytes, each global symbol's its own, with each name the linker defines
//     among them as the linker's.
//   - The static runtime (runtime_import_static, import_test_static_runtime): crt1.o, crti.o, crtbeginT.o, crtend.o
//     and crtn.o, with what they are for and crt1.o's ABI note; the archives in GCC's group order, each one's members
//     one after another; the bindings of main, __libc_start_main, __gmon_start__, __TMC_END__, _GLOBAL_OFFSET_TABLE_,
//     the IRELATIVE relocations' bounds (the linker's) and _dl_relocate_static_pie (crt1.o's empty one), and none to a
//     shared library; the names the kind's linker defines among the global names, and no other
//     (import_test_linker_names); memcpy an IFUNC, _nl_current_LC_CTYPE_used absolute, and libgcc.a's priorities;
//     each object's COMDAT groups as its ImportObject has them, numbered alike by signature; each piece's strong
//     references against a scan of its symbols, with the member each loads, and each global name's defining member;
//     link_check on the whole runtime; and every crt object and member taking the vector path.
//   - The static PIE's runtime (runtime_import_static_pie), checked as the static runtime is: rcrt1.o, crti.o,
//     crtbeginS.o, crtendS.o and crtn.o, with the same archives; _dl_relocate_static_pie bound to libc.a's
//     dl-reloc-static-pie.o, which relocates the executable and whose _DYNAMIC is the linker's; and the IRELATIVE
//     relocations' bounds, which libc.a refers to weakly and a static PIE's linker doesn't define, bound to nothing.
//   - With --cross-check, which needs readelf: libc.so.6's, the loader's and libgcc_s.so.1's exports against
//     `readelf -W --dyn-syms` (name, version, type, binding and size; IFUNC as a function, GNU_UNIQUE as global) and
//     references, and the relocations of the crt objects and the archives' members, both static runtimes' among them,
//     against `readelf -W -r`: each one a fixup of its kind at its offset, against the symbol (or the section) readelf
//     names, with its addend, and no other fixups.
// Throughout, each object, archive and library is also read by its reader's vector path, its exact path and the reader
// itself, each into a link (or a read) of its own (import_test_paths and its archive and library twins): the reader's
// result and records must be the exact path's, and the vector path may take an input only when the exact path reads
// it without an error, and must then make the same records, column by column. Each well-formed input must take the
// vector path, and each check reports how many inputs each path took.
// The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally, the paths' comparisons, the round trips, files made by hand, objects, archives,
// shared libraries, linker scripts, page edges, the runtime (discovery and the static runtimes among it), the
// cross-check, and run_import_tests.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <elf.h>

// OS-dependent includes
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#define BUSTER_IMPORT_TEST_REPORTED 3
#define BUSTER_IMPORT_TEST_ROUND_TRIP_FUNCTIONS 500
// The unit the objects made by hand are imported as.
#define BUSTER_IMPORT_TEST_UNIT 7

typedef struct ImportTestTally ImportTestTally;
struct ImportTestTally
{
    const char* check;
    uint64_t checked;
    uint64_t failures;
    // How many objects import_test_paths saw the vector path take, and leave to the exact path.
    uint64_t vector;
    uint64_t exact;
};

static void import_test_expect(ImportTestTally* tally, bool condition, const char* what, uint64_t index)
{
    tally->checked += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_IMPORT_TEST_REPORTED)
        {
            printf("  %s: %s (%llu)\n", tally->check, what, (unsigned long long)index);
        }
        tally->failures += 1;
    }
}

static bool import_test_report(const ImportTestTally* tally)
{
    if (tally->vector + tally->exact != 0)
    {
        printf("  the vector paths took %llu inputs and left %llu to the exact paths\n", (unsigned long long)tally->vector, (unsigned long long)tally->exact);
    }
    printf("%s: %llu checks, %llu failed\n", tally->check, (unsigned long long)tally->checked, (unsigned long long)tally->failures);
    return tally->failures == 0;
}

static const char* const import_test_errors[] = {
    [IMPORT_ERROR_NONE] = "none",
    [IMPORT_ERROR_FORMAT] = "format",
    [IMPORT_ERROR_BOUNDS] = "bounds",
    [IMPORT_ERROR_UNSUPPORTED] = "unsupported",
    [IMPORT_ERROR_LINK] = "link",
    [IMPORT_ERROR_MEMORY] = "memory",
    [IMPORT_ERROR_SCRIPT] = "script",
};

static ImportResult import_test_result(ImportError error, uint64_t offset, uint32_t section, uint32_t symbol, uint32_t relocation, uint32_t type)
{
    ImportResult result = {
        .error = error,
        .section = section,
        .symbol = symbol,
        .relocation = relocation,
        .type = type,
        .offset = offset,
    };
    return result;
}

static bool import_test_same_result(ImportResult a, ImportResult b)
{
    return a.error == b.error && a.section == b.section && a.symbol == b.symbol && a.relocation == b.relocation && a.type == b.type && a.offset == b.offset;
}

// Whether a name of the columns (its bytes and length) is `text`.
static bool import_test_name_is(const uint8_t* name, uint32_t length, const char* text)
{
    return strlen(text) == length && memcmp(name, text, length) == 0;
}

static void import_test_print_result(ImportResult result)
{
    printf("%s at %llu (section %u, symbol %u, relocation %u, type %u)", import_test_errors[result.error], (unsigned long long)result.offset, result.section, result.symbol,
           result.relocation, result.type);
}

// Whether two links hold the same records: every column of every table, each name by its bytes (each link has copies
// of its own), and the side table of wide addends.
static bool import_test_same_tables(const Link* a, const Link* b)
{
    bool result = a->names.count == b->names.count && a->atoms.count == b->atoms.count && a->symbols.count == b->symbols.count && a->fixups.count == b->fixups.count &&
                  a->fixups.wide_count == b->fixups.wide_count && a->error == b->error;
    for (NameId name = 1; result && name < a->names.count; name += 1)
    {
        const uint32_t length = a->names.length[name];
        result = length == b->names.length[name] && a->names.hash[name] == b->names.hash[name] && memcmp(a->names.bytes[name], b->names.bytes[name], (uint64_t)length + 1) == 0;
    }
    for (AtomId atom = 1; result && atom < a->atoms.count; atom += 1)
    {
        result = a->atoms.class[atom] == b->atoms.class[atom] && a->atoms.alignment_log2[atom] == b->atoms.alignment_log2[atom] && a->atoms.unit[atom] == b->atoms.unit[atom] &&
                 a->atoms.size[atom] == b->atoms.size[atom] && a->atoms.bytes[atom] == b->atoms.bytes[atom] && a->atoms.fixup_begin[atom] == b->atoms.fixup_begin[atom] &&
                 a->atoms.fixup_count[atom] == b->atoms.fixup_count[atom] && a->atoms.address[atom] == b->atoms.address[atom];
    }
    for (SymbolId symbol = 1; result && symbol < a->symbols.count; symbol += 1)
    {
        result = a->symbols.name[symbol] == b->symbols.name[symbol] && a->symbols.atom[symbol] == b->symbols.atom[symbol] && a->symbols.value[symbol] == b->symbols.value[symbol] &&
                 a->symbols.size[symbol] == b->symbols.size[symbol] && a->symbols.flags[symbol] == b->symbols.flags[symbol] && a->symbols.source[symbol] == b->symbols.source[symbol] &&
                 a->symbols.got_slot[symbol] == b->symbols.got_slot[symbol] && a->symbols.dynamic_index[symbol] == b->symbols.dynamic_index[symbol] &&
                 a->symbols.version[symbol] == b->symbols.version[symbol] && a->symbols.reference[symbol] == b->symbols.reference[symbol];
    }
    for (FixupId fixup = 1; result && fixup < a->fixups.count; fixup += 1)
    {
        result = a->fixups.offset[fixup] == b->fixups.offset[fixup] && a->fixups.target[fixup] == b->fixups.target[fixup] && a->fixups.addend[fixup] == b->fixups.addend[fixup] &&
                 a->fixups.kind[fixup] == b->fixups.kind[fixup];
    }
    for (uint32_t wide = 1; result && wide < a->fixups.wide_count; wide += 1)
    {
        result = a->fixups.wide_addends[wide] == b->fixups.wide_addends[wide];
    }
    result = result && a->priorities.count == b->priorities.count;
    for (uint32_t entry = 1; result && entry < a->priorities.count; entry += 1)
    {
        result = a->priorities.atom[entry] == b->priorities.atom[entry] && a->priorities.priority[entry] == b->priorities.priority[entry];
    }
    return result;
}

// Whether two imports of an object made the same ImportObject: its atoms and symbols by index, and its groups, each
// signature by its bytes.
static bool import_test_same_object(const ImportObject* a, const ImportObject* b)
{
    bool result = a->section_count == b->section_count && a->symbol_count == b->symbol_count && a->group_count == b->group_count &&
                  a->executable_stack_section == b->executable_stack_section;
    result = result && (a->section_count == 0 || memcmp(a->section_atoms, b->section_atoms, (uint64_t)a->section_count * sizeof(AtomId)) == 0);
    result = result && (a->symbol_count == 0 || memcmp(a->symbols, b->symbols, (uint64_t)a->symbol_count * sizeof(SymbolId)) == 0);
    uint64_t members = 0;
    for (uint32_t g = 0; result && g < a->group_count; g += 1)
    {
        const ImportGroup* x = &a->groups[g];
        const ImportGroup* y = &b->groups[g];
        result = x->signature.length == y->signature.length && memcmp(x->signature.pointer, y->signature.pointer, x->signature.length) == 0 && x->first == y->first &&
                 x->count == y->count;
        members = result ? x->first + x->count : members;
    }
    result = result && (members == 0 || memcmp(a->group_sections, b->group_sections, members * sizeof(uint32_t)) == 0);
    return result;
}

// How many records of each table import_test_paths_agree makes room for when import_object_capacity can't count them.
#define BUSTER_IMPORT_TEST_PATH_ROOM 4096

// Which path an object must take, for import_test_paths.
typedef enum ImportTestPath
{
    // Either, as long as the paths agree.
    IMPORT_TEST_EITHER,
    // The vector path must import it.
    IMPORT_TEST_VECTOR,
    // The vector path must leave it to the exact path.
    IMPORT_TEST_EXACT,
} ImportTestPath;

// The paths against each other on one object, each into a link of its own with the same room (`room`, or what
// import_object_capacity counts, or BUSTER_IMPORT_TEST_PATH_ROOM records of each table where it fails): import_object
// must give import_object_scalar's result, records and ImportObject, and import_object_vector may take the object only
// when the exact path imports it, into the same records and ImportObject. *taken says whether it took it.
static bool import_test_paths_agree(String8 bytes, const LinkCapacity* room, bool* taken, Arena* arena)
{
    const uint64_t length = arena->length;
    LinkCapacity capacity = {
        .names = BUSTER_IMPORT_TEST_PATH_ROOM,
        .atoms = BUSTER_IMPORT_TEST_PATH_ROOM,
        .symbols = BUSTER_IMPORT_TEST_PATH_ROOM,
        .fixups = BUSTER_IMPORT_TEST_PATH_ROOM,
        .wide_addends = BUSTER_IMPORT_TEST_PATH_ROOM,
        .priorities = BUSTER_IMPORT_TEST_PATH_ROOM,
    };
    LinkCapacity counted;
    if (room != 0)
    {
        capacity = *room;
    }
    else if (import_object_capacity(bytes, &counted).error == IMPORT_ERROR_NONE)
    {
        capacity = counted;
    }
    Link vector;
    Link exact;
    Link combined;
    ImportObject vector_object = { 0 };
    ImportObject exact_object = { 0 };
    ImportObject combined_object = { 0 };
    const bool linked = link_initialize(&vector, arena, arena, capacity) && link_initialize(&exact, arena, arena, capacity) && link_initialize(&combined, arena, arena, capacity);
    bool result = linked;
    *taken = false;
    if (linked)
    {
        *taken = import_object_vector(&vector, bytes, BUSTER_IMPORT_TEST_UNIT, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &vector_object);
        const ImportResult exact_result = import_object_scalar(&exact, bytes, BUSTER_IMPORT_TEST_UNIT, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &exact_object);
        const ImportResult combined_result = import_object(&combined, bytes, BUSTER_IMPORT_TEST_UNIT, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &combined_object);
        result = import_test_same_result(exact_result, combined_result) && import_test_same_tables(&exact, &combined) && import_test_same_object(&exact_object, &combined_object);
        if (*taken)
        {
            result = result && exact_result.error == IMPORT_ERROR_NONE && import_test_same_tables(&vector, &exact) && import_test_same_object(&vector_object, &exact_object);
        }
    }
    arena_pop_to(arena, length);
    return result;
}

// import_test_paths_agree as a check, with the path the object must take, counted in the tally.
static void import_test_paths(ImportTestTally* tally, String8 bytes, ImportTestPath expected, const char* what, uint64_t index, Arena* arena)
{
    bool vector = false;
    const bool agree = import_test_paths_agree(bytes, 0, &vector, arena);
    const bool path = expected == IMPORT_TEST_EITHER || vector == (expected == IMPORT_TEST_VECTOR);
    if ((!agree || !path) && tally->failures < BUSTER_IMPORT_TEST_REPORTED)
    {
        printf("  %s: %s (%llu): the paths %s, and the vector path %s it\n", tally->check, what, (unsigned long long)index, agree ? "agree" : "disagree",
               vector ? "took" : "left");
    }
    tally->checked += 1;
    tally->failures += agree && path ? 0 : 1;
    tally->vector += vector ? 1 : 0;
    tally->exact += vector ? 0 : 1;
}

// Whether two reads of a library are the same: its soname, versions, exports, references, hidden exports and index,
// column by column.
static bool import_test_same_library(const ImportLibrary* a, const ImportLibrary* b)
{
    const ImportExports* x = &a->exports;
    const ImportExports* y = &b->exports;
    const ImportReferences* p = &a->references;
    const ImportReferences* q = &b->references;
    const ImportHiddenExports* g = &a->hidden;
    const ImportHiddenExports* k = &b->hidden;
    bool result = a->soname.pointer == b->soname.pointer && a->soname.length == b->soname.length && a->version_count == b->version_count && x->count == y->count &&
                  p->count == q->count && g->count == k->count && a->index.bucket_bits == b->index.bucket_bits && (a->index.start == 0) == (b->index.start == 0);
    for (uint32_t v = 0; result && v < a->version_count; v += 1)
    {
        const ImportVersion* c = &a->versions[v];
        const ImportVersion* d = &b->versions[v];
        result = c->name.pointer == d->name.pointer && c->name.length == d->name.length && c->hash == d->hash && c->index == d->index && c->base == d->base;
    }
    for (uint32_t i = 0; result && i < x->count; i += 1)
    {
        result = x->name[i] == y->name[i] && x->length[i] == y->length[i] && x->hash[i] == y->hash[i] && x->size[i] == y->size[i] && x->flags[i] == y->flags[i] &&
                 x->version[i] == y->version[i] && x->value[i] == y->value[i] && x->section[i] == y->section[i] && a->index.order[i] == b->index.order[i] &&
                 a->index.hash[i] == b->index.hash[i];
    }
    for (uint32_t i = 0; result && i < p->count; i += 1)
    {
        result = p->name[i] == q->name[i] && p->length[i] == q->length[i] && p->hash[i] == q->hash[i] && p->weak[i] == q->weak[i] && p->version[i] == q->version[i];
    }
    for (uint32_t i = 0; result && i < g->count; i += 1)
    {
        result = g->name[i] == k->name[i] && g->version[i] == k->version[i];
    }
    const uint64_t buckets = a->index.start != 0 ? (uint64_t)1 << a->index.bucket_bits : 0;
    result = result && (buckets == 0 || memcmp(a->index.start, b->index.start, (buckets + 1) * sizeof(uint32_t)) == 0);
    return result;
}

// Whether import_library_find finds what a linear scan of the exports finds: each export's name, and each reference's,
// at the name's first export, or nowhere.
static bool import_test_index_agrees(const ImportLibrary* library)
{
    const ImportExports* exports = &library->exports;
    const ImportReferences* references = &library->references;
    bool result = true;
    for (uint32_t i = 0; result && i < exports->count + references->count; i += 1)
    {
        const bool export = i < exports->count;
        const uint32_t r = i - (export ? 0 : exports->count);
        const uint8_t* name = export ? exports->name[i] : references->name[r];
        const uint32_t length = export ? exports->length[i] : references->length[r];
        const uint64_t hash = export ? exports->hash[i] : references->hash[r];
        uint32_t first = 0;
        for (uint32_t j = 0; first == 0 && j < exports->count; j += 1)
        {
            first = exports->length[j] == length && memcmp(exports->name[j], name, length) == 0 ? j + 1 : 0;
        }
        result = import_library_find(library, name, length, hash) == first;
    }
    return result;
}

// The library reader's paths against each other, as import_test_paths_agree does for objects: import_library must give
// import_library_scalar's result and read, import_library_vector may take the library only when the exact path reads
// it, and then read it the same; and the index must find what a linear scan finds. *taken says whether the vector
// path took it.
static bool import_test_library_paths_agree(String8 bytes, bool* taken, Arena* arena)
{
    const uint64_t length = arena->length;
    ImportLibrary vector;
    ImportLibrary exact;
    ImportLibrary combined;
    *taken = import_library_vector(&vector, bytes, arena);
    const ImportResult exact_result = import_library_scalar(&exact, bytes, arena);
    const ImportResult combined_result = import_library(&combined, bytes, arena);
    bool result = import_test_same_result(exact_result, combined_result);
    if (exact_result.error == IMPORT_ERROR_NONE)
    {
        result = result && import_test_same_library(&exact, &combined) && import_test_index_agrees(&exact);
    }
    if (*taken)
    {
        result = result && exact_result.error == IMPORT_ERROR_NONE && import_test_same_library(&vector, &exact);
    }
    arena_pop_to(arena, length);
    return result;
}

// import_test_library_paths_agree as a check, with the path the library must take, counted in the tally.
static void import_test_library_paths(ImportTestTally* tally, String8 bytes, ImportTestPath expected, const char* what, Arena* arena)
{
    bool vector = false;
    const bool agree = import_test_library_paths_agree(bytes, &vector, arena);
    const bool path = expected == IMPORT_TEST_EITHER || vector == (expected == IMPORT_TEST_VECTOR);
    if ((!agree || !path) && tally->failures < BUSTER_IMPORT_TEST_REPORTED)
    {
        printf("  %s: %s: the paths %s, and the vector path %s it\n", tally->check, what, agree ? "agree" : "disagree", vector ? "took" : "left");
    }
    tally->checked += 1;
    tally->failures += agree && path ? 0 : 1;
    tally->vector += vector ? 1 : 0;
    tally->exact += vector ? 0 : 1;
}

// Whether two reads of an archive are the same: its members and its index, field by field.
static bool import_test_same_archive(const ImportArchive* a, const ImportArchive* b)
{
    bool result = a->member_count == b->member_count && a->symbol_count == b->symbol_count && a->thin == b->thin;
    for (uint32_t m = 0; result && m < a->member_count; m += 1)
    {
        const ImportMember* x = &a->members[m];
        const ImportMember* y = &b->members[m];
        result = x->name.pointer == y->name.pointer && x->name.length == y->name.length && x->bytes.pointer == y->bytes.pointer && x->bytes.length == y->bytes.length &&
                 x->offset == y->offset;
    }
    for (uint32_t i = 0; result && i < a->symbol_count; i += 1)
    {
        const ImportArchiveSymbol* x = &a->symbols[i];
        const ImportArchiveSymbol* y = &b->symbols[i];
        result = x->name.pointer == y->name.pointer && x->name.length == y->name.length && x->member == y->member;
    }
    return result;
}

// The archive reader's paths against each other, as import_test_paths_agree does for objects. *taken says whether the
// vector path took the archive.
static bool import_test_archive_paths_agree(String8 bytes, bool* taken, Arena* arena)
{
    const uint64_t length = arena->length;
    ImportArchive vector;
    ImportArchive exact;
    ImportArchive combined;
    *taken = import_archive_vector(&vector, bytes, arena);
    const ImportResult exact_result = import_archive_scalar(&exact, bytes, arena);
    const ImportResult combined_result = import_archive(&combined, bytes, arena);
    bool result = import_test_same_result(exact_result, combined_result) && (exact_result.error != IMPORT_ERROR_NONE || import_test_same_archive(&exact, &combined));
    if (*taken)
    {
        result = result && exact_result.error == IMPORT_ERROR_NONE && import_test_same_archive(&vector, &exact);
    }
    arena_pop_to(arena, length);
    return result;
}

// import_test_archive_paths_agree as a check, with the path the archive must take, counted in the tally.
static void import_test_archive_paths(ImportTestTally* tally, String8 bytes, ImportTestPath expected, const char* what, Arena* arena)
{
    bool vector = false;
    const bool agree = import_test_archive_paths_agree(bytes, &vector, arena);
    const bool path = expected == IMPORT_TEST_EITHER || vector == (expected == IMPORT_TEST_VECTOR);
    if ((!agree || !path) && tally->failures < BUSTER_IMPORT_TEST_REPORTED)
    {
        printf("  %s: %s: the paths %s, and the vector path %s it\n", tally->check, what, agree ? "agree" : "disagree", vector ? "took" : "left");
    }
    tally->checked += 1;
    tally->failures += agree && path ? 0 : 1;
    tally->vector += vector ? 1 : 0;
    tally->exact += vector ? 0 : 1;
}

// A link for an import, with room for what import_object_capacity says, from `arena`.
static bool import_test_link(Link* link, String8 bytes, Arena* arena)
{
    LinkCapacity capacity;
    return import_object_capacity(bytes, &capacity).error == IMPORT_ERROR_NONE && link_initialize(link, arena, arena, capacity);
}

static bool import_test_same_name(const Link* a, NameId a_name, const Link* b, NameId b_name)
{
    return a->names.length[a_name] == b->names.length[b_name] && memcmp(a->names.bytes[a_name], b->names.bytes[b_name], a->names.length[a_name]) == 0;
}

// The fixup of `atom` whose field is at `offset`, or 0: an atom's fixups are in offset order.
static FixupId import_test_fixup_at(const Link* link, AtomId atom, uint64_t offset)
{
    const FixupId begin = link->atoms.fixup_begin[atom];
    const uint32_t count = link->atoms.fixup_count[atom];
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (link->fixups.offset[begin + middle] < offset)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < count && link->fixups.offset[begin + low] == offset ? begin + low : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Round trips

// The imported link's defined symbols with names, by name's hash: open addressing, each slot a symbol or 0, a name's
// symbols in id order along its probes.
typedef struct ImportTestIndex ImportTestIndex;
struct ImportTestIndex
{
    SymbolId* slots;
    uint64_t mask;
};

static bool import_test_index(ImportTestIndex* index, const Link* link, Arena* arena)
{
    uint64_t capacity = 16;
    while (capacity < 2 * (uint64_t)link->symbols.count)
    {
        capacity *= 2;
    }
    index->slots = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, capacity);
    index->mask = capacity - 1;
    if (index->slots != 0)
    {
        memset(index->slots, 0, capacity * sizeof(SymbolId));
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            const NameId name = link->symbols.name[symbol];
            const bool indexed = name != 0 && link->symbols.atom[symbol] != 0;
            uint64_t slot = link->names.hash[name] & index->mask;
            while (indexed && index->slots[slot] != 0)
            {
                slot = (slot + 1) & index->mask;
            }
            index->slots[slot] = indexed ? symbol : index->slots[slot];
        }
    }
    return index->slots != 0;
}

// The first imported symbol defined with the original's name, or 0.
static SymbolId import_test_find(const ImportTestIndex* index, const Link* imported, const Link* original, NameId name)
{
    SymbolId result = 0;
    bool searching = true;
    for (uint64_t slot = original->names.hash[name] & index->mask; searching; slot = (slot + 1) & index->mask)
    {
        const SymbolId symbol = index->slots[slot];
        const NameId other = imported->symbols.name[symbol];
        const bool match = symbol != 0 && imported->names.hash[other] == original->names.hash[name] && import_test_same_name(imported, other, original, name);
        result = match ? symbol : 0;
        searching = symbol != 0 && !match;
    }
    return result;
}

// One unit's round trip: the original link and where the writer put its atoms, and the imported link with the atoms
// the writer's sections became, which are the object's sections with atoms, in index order (ObjectUnits.atom_section
// numbers the writer's sections from 0 in the order it made them).
typedef struct ImportTestRoundTrip ImportTestRoundTrip;
struct ImportTestRoundTrip
{
    const Link* original;
    const ObjectUnits* units;
    const Link* imported;
    const AtomId* section_atoms;
    uint32_t section_count;
    ImportTestIndex index;
};

// Whether an original atom is in its copy, byte for byte where the writer put it, with each of its fixups at its place
// of the same kind, pointing at the same place with the same addend; adds its fixups to *fixups.
static bool import_test_same_atom(const ImportTestRoundTrip* trip, AtomId atom, uint64_t* fixups)
{
    const Link* original = trip->original;
    const Link* imported = trip->imported;
    const LinkAtoms* atoms = &original->atoms;
    const LinkSymbols* symbols = &original->symbols;
    const uint32_t section = trip->units->atom_section[atom];
    const uint64_t offset = trip->units->atom_offset[atom];
    const AtomId copy = section < trip->section_count ? trip->section_atoms[section] : 0;
    const bool placed = copy != 0 && imported->atoms.class[copy] == atoms->class[atom] && offset + atoms->size[atom] <= imported->atoms.size[copy];
    const bool zero_fill = link_class_is_zero_fill(atoms->class[atom]);
    bool result = placed && (zero_fill || atoms->size[atom] == 0 || memcmp(imported->atoms.bytes[copy] + offset, atoms->bytes[atom], atoms->size[atom]) == 0);
    for (uint32_t i = 0; result && i < atoms->fixup_count[atom]; i += 1)
    {
        const FixupId fixup = atoms->fixup_begin[atom] + i;
        const FixupId found = import_test_fixup_at(imported, copy, offset + original->fixups.offset[fixup]);
        const uint8_t kind = original->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
        const SymbolId target = original->fixups.target[fixup];
        const int64_t addend = link_fixup_addend(original, fixup);
        const SymbolId imported_target = found != 0 ? imported->fixups.target[found] : 0;
        const int64_t imported_addend = found != 0 ? link_fixup_addend(imported, found) : 0;
        const bool local = (symbols->flags[target] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
        result = found != 0 && (imported->fixups.kind[found] & BUSTER_LINK_FIXUP_KIND) == kind;
        if (result && local)
        {
            // The same place: the original's atom and offset where the writer put them, or the copy's.
            const AtomId target_atom = symbols->atom[target];
            const uint64_t want = trip->units->atom_offset[target_atom] + symbols->value[target] + (uint64_t)addend;
            const AtomId want_atom = trip->section_atoms[trip->units->atom_section[target_atom]];
            const uint64_t have = imported->symbols.value[imported_target] + (uint64_t)imported_addend;
            result = imported->symbols.atom[imported_target] == want_atom && have == want;
        }
        else if (result)
        {
            const NameId name = symbols->name[target];
            const NameId imported_name = imported->symbols.name[imported_target];
            result = imported_name != 0 && import_test_same_name(imported, imported_name, original, name) && imported_addend == addend;
        }
    }
    *fixups += atoms->fixup_count[atom];
    return result;
}

// Whether an original symbol with a name, defined in an atom, is defined in the copy at the same place with the same
// flags and size.
static bool import_test_same_symbol(const ImportTestRoundTrip* trip, SymbolId symbol)
{
    const LinkSymbols* symbols = &trip->original->symbols;
    const Link* imported = trip->imported;
    const AtomId atom = symbols->atom[symbol];
    const SymbolId copy = import_test_find(&trip->index, imported, trip->original, symbols->name[symbol]);
    const AtomId want_atom = trip->section_atoms[trip->units->atom_section[atom]];
    const uint64_t want = trip->units->atom_offset[atom] + symbols->value[symbol];
    return copy != 0 && imported->symbols.atom[copy] == want_atom && imported->symbols.value[copy] == want && imported->symbols.flags[copy] == symbols->flags[symbol] &&
           imported->symbols.size[copy] == symbols->size[symbol];
}

// Checks one unit's object read back against the link it was written from.
static void import_test_unit(ImportTestTally* tally, const Link* original, const ObjectUnits* units, uint32_t unit, String8 object_bytes, uint64_t index, Arena* arena)
{
    Link imported;
    ImportObject object = { 0 };
    const bool linked = import_test_link(&imported, object_bytes, arena);
    const ImportResult result = linked ? import_object(&imported, object_bytes, unit, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &object) : import_test_result(IMPORT_ERROR_MEMORY, 0, 0, 0, 0, 0);
    import_test_expect(tally, result.error == IMPORT_ERROR_NONE, "the object imports", index);
    AtomId* section_atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, object.section_count + 1);
    ImportTestRoundTrip trip = {
        .original = original,
        .units = units,
        .imported = &imported,
        .section_atoms = section_atoms,
        .section_count = 0,
    };
    const bool ready = result.error == IMPORT_ERROR_NONE && section_atoms != 0 && import_test_index(&trip.index, &imported, arena);
    for (uint32_t section = 1; ready && section < object.section_count; section += 1)
    {
        section_atoms[trip.section_count] = object.section_atoms[section];
        trip.section_count += object.section_atoms[section] != 0 ? 1 : 0;
    }
    bool atoms_match = ready;
    bool symbols_match = ready;
    uint64_t fixups = 0;
    for (AtomId atom = 1; ready && atom < original->atoms.count; atom += 1)
    {
        atoms_match = atoms_match && (original->atoms.unit[atom] != unit || import_test_same_atom(&trip, atom, &fixups));
    }
    for (SymbolId symbol = 1; ready && symbol < original->symbols.count; symbol += 1)
    {
        const AtomId atom = original->symbols.atom[symbol];
        const bool own = atom != 0 && original->atoms.unit[atom] == unit && original->symbols.name[symbol] != 0;
        symbols_match = symbols_match && (!own || import_test_same_symbol(&trip, symbol));
    }
    import_test_expect(tally, atoms_match, "each atom's bytes where the writer put them, and its fixups", index);
    import_test_expect(tally, symbols_match, "each symbol at its place, with its flags and size", index);
    import_test_expect(tally, ready && imported.fixups.count == fixups + 1, "no other fixups", index);
    import_test_paths(tally, object_bytes, IMPORT_TEST_VECTOR, "the vector path imports the object, as the exact path does", index, arena);
}

// The objects of a program's units, with function sections or not as `first_sections` to 1 say, read back.
static uint64_t import_test_program(ImportTestTally* tally, LinkProgramOptions options, uint32_t first_sections, uint64_t index, Arena* arena)
{
    const uint64_t position = arena->length;
    LinkProgram program;
    ObjectUnits units;
    ObjectSymbols symbols;
    uint64_t objects = 0;
    const bool generated = link_program_generate(&program, options);
    const bool prepared = generated && object_prepare(&units, &program.link, options.units, arena) && object_map_symbols(&symbols, &program.link, arena);
    import_test_expect(tally, prepared, "the program and its units", index);
    for (uint32_t sections = first_sections; prepared && sections < 2; sections += 1)
    {
        const ObjectOptions object_options = {
            .function_sections = sections == 1,
        };
        for (uint32_t unit = 0; unit < units.count; unit += 1)
        {
            char source[64];
            const uint64_t object_position = arena->length;
            const Object built = object_build(&program.link, &units, &symbols, unit, object_options, object_test_source_name(source, sizeof(source), unit), arena);
            const String8 bytes = {
                .pointer = (char*)built.bytes,
                .length = built.size,
            };
            import_test_expect(tally, built.error == OBJECT_ERROR_NONE, "the object", index);
            if (built.error == OBJECT_ERROR_NONE)
            {
                import_test_unit(tally, &program.link, &units, unit, bytes, index, arena);
                objects += 1;
            }
            arena_pop_to(arena, object_position);
        }
    }
    if (generated)
    {
        link_program_release(&program);
    }
    arena_pop_to(arena, position);
    return objects;
}

static bool import_test_round_trips(uint64_t seed, Arena* arena)
{
    ImportTestTally tally = {
        .check = "round trips",
    };
    LinkProgramOptions options[BUSTER_LINK_TEST_PROGRAMS];
    link_test_program_options(options, seed, BUSTER_IMPORT_TEST_ROUND_TRIP_FUNCTIONS);
    uint64_t objects = 0;
    for (uint32_t i = 0; i < BUSTER_LINK_TEST_PROGRAMS; i += 1)
    {
        objects += import_test_program(&tally, options[i], 0, i, arena);
    }
    // The object writer's unit past SHN_LORESERVE sections.
    LinkProgramOptions many = link_program_default_options();
    many.seed = seed;
    many.units = 1;
    many.functions = BUSTER_OBJECT_TEST_MANY_FUNCTIONS;
    many.filler = 0;
    objects += import_test_program(&tally, many, 1, BUSTER_LINK_TEST_PROGRAMS, arena);
    printf("  %llu objects read back, one of %u functions in sections of their own; the vector path imported %llu, the exact path %llu\n", (unsigned long long)objects,
           BUSTER_OBJECT_TEST_MANY_FUNCTIONS, (unsigned long long)tally.vector, (unsigned long long)tally.exact);
    return import_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Files made by hand

#define BUSTER_IMPORT_TEST_MAX_SECTIONS 48
#define BUSTER_IMPORT_TEST_MAX_SYMBOLS 48
#define BUSTER_IMPORT_TEST_MAX_RELOCATIONS 24
#define BUSTER_IMPORT_TEST_STRING_BYTES 1024

// Names no section of a file made by hand has, at the end of its .shstrtab, which the malformed cases give a section
// (ImportTestFile.spare_names): ones the importer refuses.
typedef enum ImportTestSpareName
{
    IMPORT_TEST_NAME_CTORS,
    IMPORT_TEST_NAME_DTORS,
    IMPORT_TEST_NAME_PRIORITY_HIGH,
    IMPORT_TEST_NAME_PRIORITY_LETTER,
    IMPORT_TEST_NAME_PRIORITY_DIGITS,
    IMPORT_TEST_NAME_PRIORITY_EMPTY,
    IMPORT_TEST_SPARE_NAMES,
} ImportTestSpareName;

static const char* const import_test_spare_names[IMPORT_TEST_SPARE_NAMES] = {
    [IMPORT_TEST_NAME_CTORS] = ".ctors",
    [IMPORT_TEST_NAME_DTORS] = ".dtors.00042",
    [IMPORT_TEST_NAME_PRIORITY_HIGH] = ".init_array.65536",
    [IMPORT_TEST_NAME_PRIORITY_LETTER] = ".init_array.1x",
    [IMPORT_TEST_NAME_PRIORITY_DIGITS] = ".init_array.000101",
    [IMPORT_TEST_NAME_PRIORITY_EMPTY] = ".fini_array.",
};
// What a symbol made by hand imports as when it imports as none: not a valid flags value, whose binding doesn't exist.
#define BUSTER_IMPORT_TEST_NONE 0xff

// The readers, which import_test_read runs by number.
typedef enum ImportTestReader
{
    IMPORT_TEST_READ_OBJECT,
    IMPORT_TEST_READ_ARCHIVE,
    IMPORT_TEST_READ_LIBRARY,
    IMPORT_TEST_READ_SCRIPT,
    IMPORT_TEST_READERS,
} ImportTestReader;

// Runs a reader on `bytes`, with what it needs from `arena`, which it leaves as it was.
static ImportResult import_test_read(ImportTestReader reader, String8 bytes, Arena* arena)
{
    const uint64_t length = arena->length;
    ImportResult result = { 0 };
    switch (reader)
    {
        case IMPORT_TEST_READ_OBJECT:
        {
            Link link;
            LinkCapacity capacity;
            ImportObject object;
            result = import_object_capacity(bytes, &capacity);
            if (result.error == IMPORT_ERROR_NONE && !link_initialize(&link, arena, arena, capacity))
            {
                result = import_test_result(IMPORT_ERROR_MEMORY, 0, 0, 0, 0, 0);
            }
            else if (result.error == IMPORT_ERROR_NONE)
            {
                result = import_object(&link, bytes, BUSTER_IMPORT_TEST_UNIT, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &object);
            }
        }
        break;
        case IMPORT_TEST_READ_ARCHIVE:
        {
            ImportArchive archive;
            result = import_archive(&archive, bytes, arena);
        }
        break;
        case IMPORT_TEST_READ_LIBRARY:
        {
            ImportLibrary library;
            result = import_library(&library, bytes, arena);
        }
        break;
        case IMPORT_TEST_READ_SCRIPT:
        {
            ImportScript script;
            result = import_script(&script, bytes, arena);
        }
        break;
        case IMPORT_TEST_READERS:
        {
        }
        break;
    }
    arena_pop_to(arena, length);
    return result;
}

typedef struct ImportTestStrings ImportTestStrings;
struct ImportTestStrings
{
    char bytes[BUSTER_IMPORT_TEST_STRING_BYTES];
    uint32_t length;
};

// Appends `text` with its NUL and returns its offset; "" is the table's first byte, a NUL, as in ELF's string tables.
static uint32_t import_test_string(ImportTestStrings* strings, const char* text)
{
    const uint64_t length = strlen(text);
    uint32_t result = 0;
    if (strings->length == 0)
    {
        strings->bytes[0] = 0;
        strings->length = 1;
    }
    if (length != 0)
    {
        assert(strings->length + length + 1 <= sizeof(strings->bytes));
        result = strings->length;
        memcpy(strings->bytes + result, text, length + 1);
        strings->length += (uint32_t)length + 1;
    }
    return result;
}

typedef struct ImportTestSection ImportTestSection;
struct ImportTestSection
{
    const char* name;
    uint32_t type;
    uint64_t flags;
    uint64_t alignment;
    uint64_t entry_size;
    uint32_t link;
    uint32_t info;
    const void* bytes;
    uint64_t size;
};

static ImportTestSection import_test_section(const char* name, uint32_t type, uint64_t flags, uint64_t alignment, uint64_t entry_size, uint32_t link, uint32_t info,
                                             const void* bytes, uint64_t size)
{
    ImportTestSection result = {
        .name = name,
        .type = type,
        .flags = flags,
        .alignment = alignment,
        .entry_size = entry_size,
        .link = link,
        .info = info,
        .bytes = bytes,
        .size = size,
    };
    return result;
}

// A file made by hand, and where its parts are: an ELF file's sections (.shstrtab last) with each one's header and
// contents, or an archive's members with each one's header and contents, and its index's and long names' headers (0
// without them).
typedef struct ImportTestFile ImportTestFile;
struct ImportTestFile
{
    uint8_t* bytes;
    uint64_t size;
    uint32_t count;
    uint64_t headers[BUSTER_IMPORT_TEST_MAX_SECTIONS];
    uint64_t contents[BUSTER_IMPORT_TEST_MAX_SECTIONS];
    // An ELF file's spare names (import_test_spare_names), by their offsets in .shstrtab.
    uint32_t spare_names[IMPORT_TEST_SPARE_NAMES];
    uint64_t index_header;
    uint64_t names_header;
};

static String8 import_test_bytes(const ImportTestFile* file)
{
    String8 result = {
        .pointer = (char*)file->bytes,
        .length = file->size,
    };
    return result;
}

// An ELF file of `type` with sections 1 to count - 1 of `sections` and a .shstrtab after them: the ELF header, each
// section's contents at its alignment (at least 8), the names (the spare ones last), then the section headers. Its
// bytes are pushed from `arena`.
static bool import_test_elf(ImportTestFile* file, uint16_t type, const ImportTestSection* sections, uint32_t count, Arena* arena)
{
    assert(count < BUSTER_IMPORT_TEST_MAX_SECTIONS);
    ImportTestStrings names = { 0 };
    uint32_t name_offsets[BUSTER_IMPORT_TEST_MAX_SECTIONS] = { 0 };
    *file = (ImportTestFile){ 0 };
    file->count = count + 1;
    uint64_t offset = sizeof(Elf64_Ehdr);
    for (uint32_t i = 1; i < count; i += 1)
    {
        const uint64_t alignment = sections[i].alignment > 8 ? sections[i].alignment : 8;
        name_offsets[i] = import_test_string(&names, sections[i].name);
        offset = (offset + alignment - 1) & ~(alignment - 1);
        file->contents[i] = offset;
        offset += sections[i].type == SHT_NOBITS ? 0 : sections[i].size;
    }
    name_offsets[count] = import_test_string(&names, ".shstrtab");
    for (uint32_t i = 0; i < IMPORT_TEST_SPARE_NAMES; i += 1)
    {
        file->spare_names[i] = import_test_string(&names, import_test_spare_names[i]);
    }
    file->contents[count] = offset;
    offset += names.length;
    const uint64_t table = (offset + 7) & ~7ull;
    for (uint32_t i = 0; i <= count; i += 1)
    {
        file->headers[i] = table + (uint64_t)i * sizeof(Elf64_Shdr);
    }
    file->size = table + (uint64_t)(count + 1) * sizeof(Elf64_Shdr);
    file->bytes = arena_push(arena, file->size, 64);
    if (file->bytes != 0)
    {
        memset(file->bytes, 0, file->size);
        Elf64_Ehdr header = { 0 };
        memcpy(header.e_ident, ELFMAG, SELFMAG);
        header.e_ident[EI_CLASS] = ELFCLASS64;
        header.e_ident[EI_DATA] = ELFDATA2LSB;
        header.e_ident[EI_VERSION] = EV_CURRENT;
        header.e_type = type;
        header.e_machine = EM_X86_64;
        header.e_version = EV_CURRENT;
        header.e_shoff = table;
        header.e_ehsize = sizeof(Elf64_Ehdr);
        header.e_shentsize = sizeof(Elf64_Shdr);
        header.e_shnum = (uint16_t)(count + 1);
        header.e_shstrndx = (uint16_t)count;
        memcpy(file->bytes, &header, sizeof(header));
        for (uint32_t i = 1; i < count; i += 1)
        {
            const ImportTestSection* section = &sections[i];
            const Elf64_Shdr entry = {
                .sh_name = name_offsets[i],
                .sh_type = section->type,
                .sh_flags = section->flags,
                .sh_offset = file->contents[i],
                .sh_size = section->size,
                .sh_link = section->link,
                .sh_info = section->info,
                .sh_addralign = section->alignment,
                .sh_entsize = section->entry_size,
            };
            if (section->type != SHT_NOBITS && section->size != 0)
            {
                memcpy(file->bytes + file->contents[i], section->bytes, section->size);
            }
            memcpy(file->bytes + file->headers[i], &entry, sizeof(entry));
        }
        const Elf64_Shdr names_entry = {
            .sh_name = name_offsets[count],
            .sh_type = SHT_STRTAB,
            .sh_offset = file->contents[count],
            .sh_size = names.length,
            .sh_addralign = 1,
        };
        memcpy(file->bytes + file->contents[count], names.bytes, names.length);
        memcpy(file->bytes + file->headers[count], &names_entry, sizeof(names_entry));
    }
    return file->bytes != 0;
}

// A copy of `base`, from `arena`, with the `width` bytes at `offset` set to `value`, a little-endian number.
static ImportTestFile import_test_changed(const ImportTestFile* base, uint64_t offset, uint32_t width, uint64_t value, Arena* arena)
{
    ImportTestFile result = *base;
    result.bytes = arena_push(arena, base->size, 64);
    if (result.bytes != 0)
    {
        memcpy(result.bytes, base->bytes, base->size);
        memcpy(result.bytes + offset, &value, width);
    }
    return result;
}

// Reads a copy of `base` with the `width` bytes at `offset` set to `value` (a little-endian number; a width of 0
// changes nothing) and checks the result against `expected`.
static void import_test_case(ImportTestTally* tally, ImportTestReader reader, const ImportTestFile* base, const char* what, uint64_t offset, uint32_t width, uint64_t value,
                             ImportResult expected, Arena* arena)
{
    const uint64_t length = arena->length;
    const ImportTestFile changed = import_test_changed(base, offset, width, value, arena);
    const ImportResult result = changed.bytes != 0 ? import_test_read(reader, import_test_bytes(&changed), arena) : import_test_result(IMPORT_ERROR_MEMORY, 0, 0, 0, 0, 0);
    const bool same = import_test_same_result(result, expected);
    if (!same && tally->failures < BUSTER_IMPORT_TEST_REPORTED)
    {
        printf("  %s: %s: ", tally->check, what);
        import_test_print_result(result);
        printf(", expected ");
        import_test_print_result(expected);
        printf("\n");
    }
    tally->checked += 1;
    tally->failures += same ? 0 : 1;
    if (reader == IMPORT_TEST_READ_OBJECT && changed.bytes != 0)
    {
        import_test_paths(tally, import_test_bytes(&changed), IMPORT_TEST_EITHER, what, 0, arena);
    }
    if (reader == IMPORT_TEST_READ_LIBRARY && changed.bytes != 0)
    {
        import_test_library_paths(tally, import_test_bytes(&changed), IMPORT_TEST_EITHER, what, arena);
    }
    if (reader == IMPORT_TEST_READ_ARCHIVE && changed.bytes != 0)
    {
        import_test_archive_paths(tally, import_test_bytes(&changed), IMPORT_TEST_EITHER, what, arena);
    }
    arena_pop_to(arena, length);
}

// ---------------------------------------------------------------------------------------------------------------------
// Objects

// The sections of the object made by hand, by index.
typedef enum ImportTestObjectSection
{
    IMPORT_TEST_TEXT = 1,
    IMPORT_TEST_RELA_TEXT,
    IMPORT_TEST_RODATA,
    IMPORT_TEST_STRINGS,
    IMPORT_TEST_DATA,
    IMPORT_TEST_RELA_DATA,
    IMPORT_TEST_RELRO,
    IMPORT_TEST_RELA_RELRO,
    IMPORT_TEST_BSS,
    IMPORT_TEST_TDATA,
    IMPORT_TEST_TBSS,
    IMPORT_TEST_INIT_ARRAY,
    IMPORT_TEST_RELA_INIT_ARRAY,
    IMPORT_TEST_FINI_ARRAY,
    IMPORT_TEST_RELA_FINI_ARRAY,
    IMPORT_TEST_EH_FRAME,
    IMPORT_TEST_RELA_EH_FRAME,
    IMPORT_TEST_INIT,
    IMPORT_TEST_FINI,
    // A COMDAT group of two sections, as libc.a's DW.ref.__gcc_personality_v0's, whose signature is a symbol's name.
    IMPORT_TEST_GROUP,
    IMPORT_TEST_GROUP_DATA,
    IMPORT_TEST_RELA_GROUP_DATA,
    // A COMDAT group whose signature is its section's name, as libc.a's .stapsdt.base's.
    IMPORT_TEST_SECTION_GROUP,
    IMPORT_TEST_BASE,
    // Init and fini arrays with priorities, as GCC and clang name them.
    IMPORT_TEST_INIT_PRIORITY,
    IMPORT_TEST_RELA_INIT_PRIORITY,
    IMPORT_TEST_FINI_PRIORITY,
    IMPORT_TEST_SFRAME,
    IMPORT_TEST_RELA_SFRAME,
    IMPORT_TEST_PROPERTY,
    IMPORT_TEST_STACK,
    IMPORT_TEST_COMMENT,
    IMPORT_TEST_SYMTAB,
    IMPORT_TEST_STRTAB,
    IMPORT_TEST_SECTIONS,
} ImportTestObjectSection;

#define BUSTER_IMPORT_TEST_TEXT_BYTES 0x80
// The priorities of IMPORT_TEST_INIT_PRIORITY and IMPORT_TEST_FINI_PRIORITY, by their names.
#define BUSTER_IMPORT_TEST_INIT_PRIORITY 101
#define BUSTER_IMPORT_TEST_FINI_PRIORITY 42

typedef struct ImportTestSymbols ImportTestSymbols;
struct ImportTestSymbols
{
    Elf64_Sym entries[BUSTER_IMPORT_TEST_MAX_SYMBOLS];
    // What each one must import as: its flags (BUSTER_IMPORT_TEST_NONE for no symbol), and the section whose atom it is
    // defined in (0 for none). A shared library's export flags instead, and its version index and what kind of
    // reference it is (0 for none, 1, or 2 for a weak one).
    uint8_t flags[BUSTER_IMPORT_TEST_MAX_SYMBOLS];
    uint32_t sections[BUSTER_IMPORT_TEST_MAX_SYMBOLS];
    uint16_t versions[BUSTER_IMPORT_TEST_MAX_SYMBOLS];
    uint8_t references[BUSTER_IMPORT_TEST_MAX_SYMBOLS];
    uint32_t count;
    ImportTestStrings names;
};

// Adds a symbol and what it must import as; returns its index.
static uint32_t import_test_add_symbol(ImportTestSymbols* symbols, const char* name, uint8_t info, uint8_t visibility, uint16_t section, uint64_t value, uint64_t size,
                                       uint8_t flags, uint32_t atom_section)
{
    assert(symbols->count < BUSTER_IMPORT_TEST_MAX_SYMBOLS);
    const uint32_t result = symbols->count;
    const Elf64_Sym entry = {
        .st_name = import_test_string(&symbols->names, name),
        .st_info = info,
        .st_other = visibility,
        .st_shndx = section,
        .st_value = value,
        .st_size = size,
    };
    symbols->entries[result] = entry;
    symbols->flags[result] = flags;
    symbols->sections[result] = atom_section;
    symbols->count += 1;
    return result;
}

typedef struct ImportTestRelocations ImportTestRelocations;
struct ImportTestRelocations
{
    Elf64_Rela entries[BUSTER_IMPORT_TEST_MAX_RELOCATIONS];
    // The kind each one must import as, BUSTER_LINK_FIXUP_NONE for none.
    uint8_t kinds[BUSTER_IMPORT_TEST_MAX_RELOCATIONS];
    uint32_t count;
};

// Adds a relocation and the kind it must import as; returns its index.
static uint32_t import_test_add_relocation(ImportTestRelocations* relocations, uint64_t offset, uint32_t symbol, uint32_t type, int64_t addend, uint8_t kind)
{
    assert(relocations->count < BUSTER_IMPORT_TEST_MAX_RELOCATIONS);
    const uint32_t result = relocations->count;
    const Elf64_Rela entry = {
        .r_offset = offset,
        .r_info = ELF64_R_INFO(symbol, type),
        .r_addend = addend,
    };
    relocations->entries[result] = entry;
    relocations->kinds[result] = kind;
    relocations->count += 1;
    return result;
}

// The object made by hand: its file, its sections, the class each must become an atom of (BUSTER_LINK_CLASS_NONE for
// none), its symbols, each relocation section's entries by its index, and the symbols and relocations the malformed
// cases change.
typedef struct ImportTestObject ImportTestObject;
struct ImportTestObject
{
    ImportTestFile file;
    ImportTestSection sections[BUSTER_IMPORT_TEST_MAX_SECTIONS];
    uint8_t classes[BUSTER_IMPORT_TEST_MAX_SECTIONS];
    ImportTestSymbols symbols;
    ImportTestRelocations relocations[BUSTER_IMPORT_TEST_MAX_SECTIONS];
    uint32_t file_symbol;
    uint32_t global_function;
    uint32_t global_object;
    uint32_t callee;
    uint32_t ifunc;
    uint32_t tls_global;
    uint32_t signature;
    uint32_t call;
    uint32_t call_got;
    uint32_t data_first;
    // The groups' contents: the flag word and the sections.
    uint32_t group[3];
    uint32_t section_group[2];
};

// A .rela section for `target`, with the relocations made for it at `index`.
static ImportTestSection import_test_rela_section(const ImportTestObject* test, const char* name, uint32_t index, uint32_t target)
{
    const ImportTestRelocations* relocations = &test->relocations[index];
    return import_test_section(name, SHT_RELA, SHF_INFO_LINK, 8, sizeof(Elf64_Rela), IMPORT_TEST_SYMTAB, target, relocations->entries,
                               (uint64_t)relocations->count * sizeof(Elf64_Rela));
}

static bool import_test_object(ImportTestObject* test, Arena* arena)
{
    uint8_t text[BUSTER_IMPORT_TEST_TEXT_BYTES];
    uint8_t rodata[16];
    uint8_t data[16];
    uint8_t relro[8];
    uint8_t tdata[8];
    uint8_t init_array[8];
    uint8_t fini_array[8];
    uint8_t eh_frame[24];
    uint8_t init[4];
    uint8_t fini[4];
    uint8_t sframe[16];
    uint8_t property[16];
    uint8_t group_data[8];
    uint8_t base[1];
    uint8_t init_priority[8];
    uint8_t fini_priority[8];
    static const char strings[] = "hello\0world";
    static const char comment[] = "GCC";
    for (uint32_t i = 0; i < sizeof(text); i += 1)
    {
        text[i] = (uint8_t)(0x40 + i);
    }
    // The instructions of the relocations that GOTPCRELX and REX_GOTPCRELX mark, before their fields: call [rip + slot]
    // (CALL_GOT) at 0x05, mov rax, [rip + slot] (LOAD_GOT) at 0x0b, add rcx, [rip + slot] at 0x60 and push [rip + slot] at
    // 0x67 (GOTPCREL: linkers don't rewrite them), jmp [rip + slot] at 0x6d (CALL_GOT), and mov rax, [rip + slot - 4] at
    // 0x73, whose addend isn't the -4 of a field at the instruction's end (GOTPCREL).
    static const uint8_t instructions[][4] = {
        { 0x05, 0xff, 0x15 },
        { 0x0b, 0x48, 0x8b, 0x05 },
        { 0x60, 0x48, 0x03, 0x0d },
        { 0x67, 0xff, 0x35 },
        { 0x6d, 0xff, 0x25 },
        { 0x73, 0x48, 0x8b, 0x05 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(instructions); i += 1)
    {
        const uint32_t length = instructions[i][3] != 0 ? 3 : 2;
        memcpy(text + instructions[i][0], instructions[i] + 1, length);
    }
    memset(rodata, 0x11, sizeof(rodata));
    memset(data, 0x22, sizeof(data));
    memset(relro, 0x33, sizeof(relro));
    memset(tdata, 0x44, sizeof(tdata));
    memset(init_array, 0x55, sizeof(init_array));
    memset(fini_array, 0x66, sizeof(fini_array));
    memset(eh_frame, 0x77, sizeof(eh_frame));
    memset(init, 0x88, sizeof(init));
    memset(fini, 0x99, sizeof(fini));
    memset(sframe, 0xaa, sizeof(sframe));
    memset(property, 0xbb, sizeof(property));
    memset(group_data, 0xcc, sizeof(group_data));
    memset(base, 0xdd, sizeof(base));
    memset(init_priority, 0xee, sizeof(init_priority));
    memset(fini_priority, 0xf0, sizeof(fini_priority));
    memset(test, 0, sizeof(*test));

    // The symbols, locals first, each with what it must import as.
    ImportTestSymbols* s = &test->symbols;
    const uint8_t none = BUSTER_IMPORT_TEST_NONE;
    const uint8_t local = BUSTER_LINK_SYMBOL_LOCAL;
    const uint8_t global = BUSTER_LINK_SYMBOL_GLOBAL;
    const uint8_t weak = BUSTER_LINK_SYMBOL_WEAK;
    import_test_add_symbol(s, "", 0, 0, SHN_UNDEF, 0, 0, none, 0);
    test->file_symbol = import_test_add_symbol(s, "test.c", ELF64_ST_INFO(STB_LOCAL, STT_FILE), 0, SHN_ABS, 0, 0, none, 0);
    const uint32_t text_symbol = import_test_add_symbol(s, "", ELF64_ST_INFO(STB_LOCAL, STT_SECTION), 0, IMPORT_TEST_TEXT, 0, 0, local, IMPORT_TEST_TEXT);
    const uint32_t rodata_symbol = import_test_add_symbol(s, "", ELF64_ST_INFO(STB_LOCAL, STT_SECTION), 0, IMPORT_TEST_RODATA, 0, 0, local, IMPORT_TEST_RODATA);
    import_test_add_symbol(s, "", ELF64_ST_INFO(STB_LOCAL, STT_SECTION), 0, IMPORT_TEST_SFRAME, 0, 0, none, 0);
    import_test_add_symbol(s, "", ELF64_ST_INFO(STB_LOCAL, STT_SECTION), 0, IMPORT_TEST_TDATA, 0, 0, local | BUSTER_LINK_SYMBOL_TLS, IMPORT_TEST_TDATA);
    const uint32_t local_function =
        import_test_add_symbol(s, "local_function", ELF64_ST_INFO(STB_LOCAL, STT_FUNC), 0, IMPORT_TEST_TEXT, 0x60, 8, local | BUSTER_LINK_SYMBOL_FUNCTION, IMPORT_TEST_TEXT);
    const uint32_t local_object =
        import_test_add_symbol(s, "local_object", ELF64_ST_INFO(STB_LOCAL, STT_OBJECT), 0, IMPORT_TEST_RODATA, 0, 8, local | BUSTER_LINK_SYMBOL_OBJECT, IMPORT_TEST_RODATA);
    const uint32_t tls_local = import_test_add_symbol(s, "tls_local", ELF64_ST_INFO(STB_LOCAL, STT_TLS), 0, IMPORT_TEST_TDATA, 0, 8, local | BUSTER_LINK_SYMBOL_TLS, IMPORT_TEST_TDATA);
    import_test_add_symbol(s, "sframe_label", ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE), 0, IMPORT_TEST_SFRAME, 0, 0, none, 0);
    import_test_add_symbol(s, "", ELF64_ST_INFO(STB_LOCAL, STT_OBJECT), 0, IMPORT_TEST_DATA, 8, 8, local | BUSTER_LINK_SYMBOL_OBJECT, IMPORT_TEST_DATA);
    const uint32_t base_symbol = import_test_add_symbol(s, "", ELF64_ST_INFO(STB_LOCAL, STT_SECTION), 0, IMPORT_TEST_BASE, 0, 0, local, IMPORT_TEST_BASE);
    import_test_add_symbol(s, "absolute_local", ELF64_ST_INFO(STB_LOCAL, STT_OBJECT), 0, SHN_ABS, 0x1234, 4, local | BUSTER_LINK_SYMBOL_OBJECT, 0);
    const uint32_t first_global = s->count;
    test->global_function =
        import_test_add_symbol(s, "global_function", ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), 0, IMPORT_TEST_TEXT, 0, 0x50, global | BUSTER_LINK_SYMBOL_FUNCTION, IMPORT_TEST_TEXT);
    test->global_object =
        import_test_add_symbol(s, "global_object", ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), 0, IMPORT_TEST_DATA, 0, 16, global | BUSTER_LINK_SYMBOL_OBJECT, IMPORT_TEST_DATA);
    const uint32_t weak_object =
        import_test_add_symbol(s, "weak_object", ELF64_ST_INFO(STB_WEAK, STT_OBJECT), 0, IMPORT_TEST_RELRO, 0, 8, weak | BUSTER_LINK_SYMBOL_OBJECT, IMPORT_TEST_RELRO);
    import_test_add_symbol(s, "hidden_object", ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), STV_HIDDEN, IMPORT_TEST_BSS, 0, 64,
                           global | BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_HIDDEN, IMPORT_TEST_BSS);
    import_test_add_symbol(s, "protected_function", ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), STV_PROTECTED, IMPORT_TEST_INIT, 0, 4,
                           global | BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_PROTECTED, IMPORT_TEST_INIT);
    const uint32_t tls_global = import_test_add_symbol(s, "tls_global", ELF64_ST_INFO(STB_GLOBAL, STT_TLS), 0, IMPORT_TEST_TBSS, 8, 8, global | BUSTER_LINK_SYMBOL_TLS, IMPORT_TEST_TBSS);
    test->tls_global = tls_global;
    import_test_add_symbol(s, "internal_label", ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), STV_INTERNAL, IMPORT_TEST_EH_FRAME, 0, 0, global | BUSTER_LINK_SYMBOL_INTERNAL,
                           IMPORT_TEST_EH_FRAME);
    import_test_add_symbol(s, "string", ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), 0, IMPORT_TEST_STRINGS, 6, 6, global | BUSTER_LINK_SYMBOL_OBJECT, IMPORT_TEST_STRINGS);
    import_test_add_symbol(s, "fini_label", ELF64_ST_INFO(STB_WEAK, STT_FUNC), 0, IMPORT_TEST_FINI, 0, 4, weak | BUSTER_LINK_SYMBOL_FUNCTION, IMPORT_TEST_FINI);
    import_test_add_symbol(s, "array_start", ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), 0, IMPORT_TEST_INIT_ARRAY, 0, 0, global, IMPORT_TEST_INIT_ARRAY);
    import_test_add_symbol(s, "array_end", ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), 0, IMPORT_TEST_FINI_ARRAY, 8, 0, global, IMPORT_TEST_FINI_ARRAY);
    test->ifunc = import_test_add_symbol(s, "ifunc_function", ELF64_ST_INFO(STB_GLOBAL, STT_GNU_IFUNC), 0, IMPORT_TEST_TEXT, 0x50, 0x10, global | BUSTER_LINK_SYMBOL_IFUNC,
                                         IMPORT_TEST_TEXT);
    import_test_add_symbol(s, "absolute_global", ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), 0, SHN_ABS, 2, 0, global, 0);
    test->signature = import_test_add_symbol(s, "DW.ref.signature", ELF64_ST_INFO(STB_WEAK, STT_OBJECT), STV_HIDDEN, IMPORT_TEST_GROUP_DATA, 0, 8,
                                             weak | BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_HIDDEN, IMPORT_TEST_GROUP_DATA);
    test->callee = import_test_add_symbol(s, "callee", ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), 0, SHN_UNDEF, 0, 0, global, 0);
    const uint32_t got_function = import_test_add_symbol(s, "got_function", ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), 0, SHN_UNDEF, 0, 0, global | BUSTER_LINK_SYMBOL_FUNCTION, 0);
    const uint32_t weak_reference = import_test_add_symbol(s, "weak_reference", ELF64_ST_INFO(STB_WEAK, STT_NOTYPE), 0, SHN_UNDEF, 0, 0, weak, 0);
    import_test_add_symbol(s, "hidden_reference", ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), STV_HIDDEN, SHN_UNDEF, 0, 0, global | BUSTER_LINK_SYMBOL_HIDDEN, 0);

    // The relocations: .rela.text's of every type the representation has, out of order, and R_X86_64_NONE; the others'
    // with addends wider than int32, in reverse order, against sections' symbols, and in .sframe, which is dropped.
    ImportTestRelocations* r = &test->relocations[IMPORT_TEST_RELA_TEXT];
    import_test_add_relocation(r, 0x5c, tls_local, R_X86_64_DTPOFF32, 0, BUSTER_LINK_FIXUP_DTPOFF32);
    import_test_add_relocation(r, 0x30, local_function, R_X86_64_PC64, 0, BUSTER_LINK_FIXUP_PC64);
    import_test_add_relocation(r, 0x50, tls_global, R_X86_64_TLSGD, -4, BUSTER_LINK_FIXUP_TLSGD);
    import_test_add_relocation(r, 0x0e, test->global_object, R_X86_64_REX_GOTPCRELX, -4, BUSTER_LINK_FIXUP_LOAD_GOT);
    import_test_add_relocation(r, 0x48, weak_object, R_X86_64_SIZE64, 0, BUSTER_LINK_FIXUP_SIZE64);
    test->call = import_test_add_relocation(r, 0x01, test->callee, R_X86_64_PLT32, -4, BUSTER_LINK_FIXUP_CALL);
    import_test_add_relocation(r, 0x3c, test->global_object, R_X86_64_32, 0, BUSTER_LINK_FIXUP_ABS32);
    import_test_add_relocation(r, 0x1c, rodata_symbol, R_X86_64_PC32, 4, BUSTER_LINK_FIXUP_PC32);
    import_test_add_relocation(r, 0x44, test->global_object, R_X86_64_SIZE32, 0, BUSTER_LINK_FIXUP_SIZE32);
    test->call_got = import_test_add_relocation(r, 0x07, got_function, R_X86_64_GOTPCRELX, -4, BUSTER_LINK_FIXUP_CALL_GOT);
    import_test_add_relocation(r, 0x38, 0, R_X86_64_NONE, 0, BUSTER_LINK_FIXUP_NONE);
    import_test_add_relocation(r, 0x24, tls_local, R_X86_64_TPOFF32, 0, BUSTER_LINK_FIXUP_TPOFF32);
    import_test_add_relocation(r, 0x40, local_object, R_X86_64_32S, 0, BUSTER_LINK_FIXUP_ABS32S);
    import_test_add_relocation(r, 0x15, weak_reference, R_X86_64_GOTPCREL, -4, BUSTER_LINK_FIXUP_GOTPCREL);
    import_test_add_relocation(r, 0x2c, tls_global, R_X86_64_GOTTPOFF, -4, BUSTER_LINK_FIXUP_GOTTPOFF);
    import_test_add_relocation(r, 0x6f, got_function, R_X86_64_GOTPCRELX, -4, BUSTER_LINK_FIXUP_CALL_GOT);
    import_test_add_relocation(r, 0x63, test->global_object, R_X86_64_REX_GOTPCRELX, -4, BUSTER_LINK_FIXUP_GOTPCREL);
    import_test_add_relocation(r, 0x58, tls_local, R_X86_64_TLSLD, -4, BUSTER_LINK_FIXUP_TLSLD);
    import_test_add_relocation(r, 0x76, test->global_object, R_X86_64_REX_GOTPCRELX, -8, BUSTER_LINK_FIXUP_GOTPCREL);
    import_test_add_relocation(r, 0x69, weak_reference, R_X86_64_GOTPCRELX, -4, BUSTER_LINK_FIXUP_GOTPCREL);
    import_test_add_relocation(&test->relocations[IMPORT_TEST_RELA_GROUP_DATA], 0, test->callee, R_X86_64_64, 0, BUSTER_LINK_FIXUP_ABS64);
    import_test_add_relocation(&test->relocations[IMPORT_TEST_RELA_INIT_PRIORITY], 0, local_function, R_X86_64_64, 0, BUSTER_LINK_FIXUP_ABS64);
    r = &test->relocations[IMPORT_TEST_RELA_DATA];
    import_test_add_relocation(r, 0x08, test->callee, R_X86_64_64, -1, BUSTER_LINK_FIXUP_ABS64);
    test->data_first = import_test_add_relocation(r, 0x00, local_object, R_X86_64_64, 0x123456789, BUSTER_LINK_FIXUP_ABS64);
    import_test_add_relocation(&test->relocations[IMPORT_TEST_RELA_RELRO], 0, test->global_function, R_X86_64_64, -0x100000000ll, BUSTER_LINK_FIXUP_ABS64);
    import_test_add_relocation(&test->relocations[IMPORT_TEST_RELA_INIT_ARRAY], 0, local_function, R_X86_64_64, 0, BUSTER_LINK_FIXUP_ABS64);
    import_test_add_relocation(&test->relocations[IMPORT_TEST_RELA_FINI_ARRAY], 0, text_symbol, R_X86_64_64, 0x10, BUSTER_LINK_FIXUP_ABS64);
    import_test_add_relocation(&test->relocations[IMPORT_TEST_RELA_EH_FRAME], 0x08, text_symbol, R_X86_64_PC32, 0, BUSTER_LINK_FIXUP_PC32);
    import_test_add_relocation(&test->relocations[IMPORT_TEST_RELA_EH_FRAME], 0x10, text_symbol, R_X86_64_PC32, 0x50, BUSTER_LINK_FIXUP_PC32);
    import_test_add_relocation(&test->relocations[IMPORT_TEST_RELA_SFRAME], 0x04, text_symbol, R_X86_64_PC32, 0, BUSTER_LINK_FIXUP_NONE);

    // The sections, and the class each must become an atom of.
    ImportTestSection* sections = test->sections;
    const uint64_t ax = SHF_ALLOC | SHF_EXECINSTR;
    const uint64_t wa = SHF_WRITE | SHF_ALLOC;
    sections[IMPORT_TEST_TEXT] = import_test_section(".text", SHT_PROGBITS, ax, 16, 0, 0, 0, text, sizeof(text));
    sections[IMPORT_TEST_RELA_TEXT] = import_test_rela_section(test, ".rela.text", IMPORT_TEST_RELA_TEXT, IMPORT_TEST_TEXT);
    sections[IMPORT_TEST_RODATA] = import_test_section(".rodata", SHT_PROGBITS, SHF_ALLOC, 16, 0, 0, 0, rodata, sizeof(rodata));
    sections[IMPORT_TEST_STRINGS] = import_test_section(".rodata.str1.1", SHT_PROGBITS, SHF_ALLOC | SHF_MERGE | SHF_STRINGS, 1, 1, 0, 0, strings, sizeof(strings));
    sections[IMPORT_TEST_DATA] = import_test_section(".data", SHT_PROGBITS, wa, 8, 0, 0, 0, data, sizeof(data));
    sections[IMPORT_TEST_RELA_DATA] = import_test_rela_section(test, ".rela.data", IMPORT_TEST_RELA_DATA, IMPORT_TEST_DATA);
    sections[IMPORT_TEST_RELRO] = import_test_section(".data.rel.ro.local", SHT_PROGBITS, wa, 8, 0, 0, 0, relro, sizeof(relro));
    sections[IMPORT_TEST_RELA_RELRO] = import_test_rela_section(test, ".rela.data.rel.ro.local", IMPORT_TEST_RELA_RELRO, IMPORT_TEST_RELRO);
    sections[IMPORT_TEST_BSS] = import_test_section(".bss", SHT_NOBITS, wa, 32, 0, 0, 0, 0, 64);
    sections[IMPORT_TEST_TDATA] = import_test_section(".tdata", SHT_PROGBITS, wa | SHF_TLS, 8, 0, 0, 0, tdata, sizeof(tdata));
    sections[IMPORT_TEST_TBSS] = import_test_section(".tbss", SHT_NOBITS, wa | SHF_TLS, 16, 0, 0, 0, 0, 16);
    sections[IMPORT_TEST_INIT_ARRAY] = import_test_section(".init_array", SHT_INIT_ARRAY, wa, 8, 8, 0, 0, init_array, sizeof(init_array));
    sections[IMPORT_TEST_RELA_INIT_ARRAY] = import_test_rela_section(test, ".rela.init_array", IMPORT_TEST_RELA_INIT_ARRAY, IMPORT_TEST_INIT_ARRAY);
    sections[IMPORT_TEST_FINI_ARRAY] = import_test_section(".fini_array", SHT_FINI_ARRAY, wa, 8, 8, 0, 0, fini_array, sizeof(fini_array));
    sections[IMPORT_TEST_RELA_FINI_ARRAY] = import_test_rela_section(test, ".rela.fini_array", IMPORT_TEST_RELA_FINI_ARRAY, IMPORT_TEST_FINI_ARRAY);
    sections[IMPORT_TEST_EH_FRAME] = import_test_section(".eh_frame", SHT_X86_64_UNWIND, SHF_ALLOC, 8, 0, 0, 0, eh_frame, sizeof(eh_frame));
    sections[IMPORT_TEST_RELA_EH_FRAME] = import_test_rela_section(test, ".rela.eh_frame", IMPORT_TEST_RELA_EH_FRAME, IMPORT_TEST_EH_FRAME);
    sections[IMPORT_TEST_INIT] = import_test_section(".init", SHT_PROGBITS, ax, 4, 0, 0, 0, init, sizeof(init));
    sections[IMPORT_TEST_FINI] = import_test_section(".fini", SHT_PROGBITS, ax, 4, 0, 0, 0, fini, sizeof(fini));
    test->group[0] = GRP_COMDAT;
    test->group[1] = IMPORT_TEST_GROUP_DATA;
    test->group[2] = IMPORT_TEST_RELA_GROUP_DATA;
    test->section_group[0] = GRP_COMDAT;
    test->section_group[1] = IMPORT_TEST_BASE;
    sections[IMPORT_TEST_GROUP] = import_test_section(".group", SHT_GROUP, 0, 4, sizeof(uint32_t), IMPORT_TEST_SYMTAB, test->signature, test->group, sizeof(test->group));
    sections[IMPORT_TEST_GROUP_DATA] = import_test_section(".data.rel.local.DW.ref.signature", SHT_PROGBITS, wa | SHF_GROUP, 8, 0, 0, 0, group_data, sizeof(group_data));
    sections[IMPORT_TEST_RELA_GROUP_DATA] = import_test_rela_section(test, ".rela.data.rel.local.DW.ref.signature", IMPORT_TEST_RELA_GROUP_DATA, IMPORT_TEST_GROUP_DATA);
    sections[IMPORT_TEST_RELA_GROUP_DATA].flags |= SHF_GROUP;
    sections[IMPORT_TEST_SECTION_GROUP] =
        import_test_section(".group", SHT_GROUP, 0, 4, sizeof(uint32_t), IMPORT_TEST_SYMTAB, base_symbol, test->section_group, sizeof(test->section_group));
    sections[IMPORT_TEST_BASE] = import_test_section(".stapsdt.base", SHT_PROGBITS, SHF_ALLOC | SHF_GROUP, 1, 0, 0, 0, base, sizeof(base));
    sections[IMPORT_TEST_INIT_PRIORITY] = import_test_section(".init_array.00101", SHT_INIT_ARRAY, wa, 8, 8, 0, 0, init_priority, sizeof(init_priority));
    sections[IMPORT_TEST_RELA_INIT_PRIORITY] = import_test_rela_section(test, ".rela.init_array.00101", IMPORT_TEST_RELA_INIT_PRIORITY, IMPORT_TEST_INIT_PRIORITY);
    sections[IMPORT_TEST_FINI_PRIORITY] = import_test_section(".fini_array.42", SHT_FINI_ARRAY, wa, 8, 8, 0, 0, fini_priority, sizeof(fini_priority));
    sections[IMPORT_TEST_SFRAME] = import_test_section(".sframe", BUSTER_IMPORT_SHT_GNU_SFRAME, SHF_ALLOC, 8, 0, 0, 0, sframe, sizeof(sframe));
    sections[IMPORT_TEST_RELA_SFRAME] = import_test_rela_section(test, ".rela.sframe", IMPORT_TEST_RELA_SFRAME, IMPORT_TEST_SFRAME);
    sections[IMPORT_TEST_PROPERTY] = import_test_section(".note.gnu.property", SHT_NOTE, SHF_ALLOC, 8, 0, 0, 0, property, sizeof(property));
    sections[IMPORT_TEST_STACK] = import_test_section(".note.GNU-stack", SHT_PROGBITS, 0, 1, 0, 0, 0, 0, 0);
    sections[IMPORT_TEST_COMMENT] = import_test_section(".comment", SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1, 1, 0, 0, comment, sizeof(comment));
    sections[IMPORT_TEST_SYMTAB] = import_test_section(".symtab", SHT_SYMTAB, 0, 8, sizeof(Elf64_Sym), IMPORT_TEST_STRTAB, first_global, s->entries, (uint64_t)s->count * sizeof(Elf64_Sym));
    sections[IMPORT_TEST_STRTAB] = import_test_section(".strtab", SHT_STRTAB, 0, 1, 0, 0, 0, s->names.bytes, s->names.length);
    test->classes[IMPORT_TEST_TEXT] = BUSTER_LINK_CLASS_TEXT;
    test->classes[IMPORT_TEST_RODATA] = BUSTER_LINK_CLASS_RODATA;
    test->classes[IMPORT_TEST_STRINGS] = BUSTER_LINK_CLASS_STRINGS;
    test->classes[IMPORT_TEST_DATA] = BUSTER_LINK_CLASS_DATA;
    test->classes[IMPORT_TEST_RELRO] = BUSTER_LINK_CLASS_DATA_RELRO;
    test->classes[IMPORT_TEST_BSS] = BUSTER_LINK_CLASS_BSS;
    test->classes[IMPORT_TEST_TDATA] = BUSTER_LINK_CLASS_TDATA;
    test->classes[IMPORT_TEST_TBSS] = BUSTER_LINK_CLASS_TBSS;
    test->classes[IMPORT_TEST_INIT_ARRAY] = BUSTER_LINK_CLASS_INIT_ARRAY;
    test->classes[IMPORT_TEST_FINI_ARRAY] = BUSTER_LINK_CLASS_FINI_ARRAY;
    test->classes[IMPORT_TEST_EH_FRAME] = BUSTER_LINK_CLASS_EH_FRAME;
    test->classes[IMPORT_TEST_INIT] = BUSTER_LINK_CLASS_INIT;
    test->classes[IMPORT_TEST_FINI] = BUSTER_LINK_CLASS_FINI;
    test->classes[IMPORT_TEST_GROUP_DATA] = BUSTER_LINK_CLASS_DATA;
    test->classes[IMPORT_TEST_BASE] = BUSTER_LINK_CLASS_RODATA;
    test->classes[IMPORT_TEST_INIT_PRIORITY] = BUSTER_LINK_CLASS_INIT_ARRAY;
    test->classes[IMPORT_TEST_FINI_PRIORITY] = BUSTER_LINK_CLASS_FINI_ARRAY;
    return import_test_elf(&test->file, ET_REL, sections, IMPORT_TEST_SECTIONS, arena);
}

// Whether the imported symbol is what the object's symbol `index` must import as.
static bool import_test_object_symbol(const ImportTestObject* test, const Link* link, const ImportObject* object, uint32_t index)
{
    const Elf64_Sym* entry = &test->symbols.entries[index];
    const SymbolId symbol = object->symbols[index];
    const uint8_t flags = test->symbols.flags[index];
    bool result = flags == BUSTER_IMPORT_TEST_NONE ? symbol == 0 : symbol != 0;
    if (flags != BUSTER_IMPORT_TEST_NONE && symbol != 0)
    {
        const bool section_symbol = ELF64_ST_TYPE(entry->st_info) == STT_SECTION;
        const AtomId atom = test->symbols.sections[index] != 0 ? object->section_atoms[test->symbols.sections[index]] : 0;
        const char* text = test->symbols.names.bytes + entry->st_name;
        const uint64_t length = strlen(text);
        const NameId name = link->symbols.name[symbol];
        const bool named = entry->st_name != 0 && !section_symbol ? name != 0 && link->names.length[name] == length && memcmp(link->names.bytes[name], text, length) == 0 : name == 0;
        const bool placed = link->symbols.atom[symbol] == atom && link->symbols.value[symbol] == (section_symbol ? 0 : entry->st_value) &&
                            link->symbols.size[symbol] == (section_symbol ? 0 : entry->st_size);
        const bool absolute = entry->st_shndx == SHN_ABS;
        const uint8_t source = atom != 0 ? BUSTER_LINK_SOURCE_FOREIGN_OBJECT : absolute ? BUSTER_LINK_SOURCE_ABSOLUTE : BUSTER_LINK_SOURCE_NONE;
        result = named && placed && link->symbols.flags[symbol] == flags && link->symbols.source[symbol] == source;
    }
    return result;
}

// Whether the atom of a relocation section's target has its entries as fixups, in offset order (a stable insertion
// sort of the entries that become fixups); adds them to *fixups.
static bool import_test_object_fixups(const ImportTestObject* test, const Link* link, const ImportObject* object, uint32_t section, uint64_t* fixups)
{
    const ImportTestRelocations* relocations = &test->relocations[section];
    const AtomId atom = object->section_atoms[test->sections[section].info];
    uint32_t order[BUSTER_IMPORT_TEST_MAX_RELOCATIONS];
    uint32_t count = 0;
    for (uint32_t j = 0; j < relocations->count; j += 1)
    {
        const bool kept = relocations->kinds[j] != BUSTER_LINK_FIXUP_NONE;
        uint32_t place = count;
        while (kept && place > 0 && relocations->entries[order[place - 1]].r_offset > relocations->entries[j].r_offset)
        {
            order[place] = order[place - 1];
            place -= 1;
        }
        order[place] = kept ? j : order[place];
        count += kept ? 1 : 0;
    }
    bool result = atom != 0 && link->atoms.fixup_count[atom] == count;
    for (uint32_t k = 0; result && k < count; k += 1)
    {
        const Elf64_Rela* entry = &relocations->entries[order[k]];
        const FixupId fixup = link->atoms.fixup_begin[atom] + k;
        result = link->fixups.offset[fixup] == entry->r_offset && (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) == relocations->kinds[order[k]] &&
                 link->fixups.target[fixup] == object->symbols[ELF64_R_SYM(entry->r_info)] && link_fixup_addend(link, fixup) == entry->r_addend;
    }
    *fixups += count;
    return result;
}

// Imports `bytes`, the object made by hand or a change of it that imports the same, and checks every section, symbol
// and fixup against what `test` says they must be.
static void import_test_object_check(ImportTestTally* tally, const ImportTestObject* test, String8 bytes, const char* what, ImportTestPath path, Arena* arena)
{
    const uint64_t length = arena->length;
    Link link;
    ImportObject object = { 0 };
    const bool linked = import_test_link(&link, bytes, arena);
    const ImportResult result =
        linked ? import_object(&link, bytes, BUSTER_IMPORT_TEST_UNIT, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &object) : import_test_result(IMPORT_ERROR_MEMORY, 0, 0, 0, 0, 0);
    const bool imported = result.error == IMPORT_ERROR_NONE && object.section_count == test->file.count && object.symbol_count == test->symbols.count;
    // Each section an atom of its class, with its alignment, size and unit, its bytes in place in the file, or none.
    bool sections = imported && object.section_atoms[IMPORT_TEST_SECTIONS] == 0;
    uint32_t atoms = 0;
    for (uint32_t i = 1; imported && i < IMPORT_TEST_SECTIONS; i += 1)
    {
        const ImportTestSection* section = &test->sections[i];
        const AtomId atom = object.section_atoms[i];
        const uint8_t class = test->classes[i];
        const uint8_t* place = link_class_is_zero_fill(class) || section->size == 0 ? 0 : (const uint8_t*)bytes.pointer + test->file.contents[i];
        const uint32_t alignment_log2 = section->alignment == 0 ? 0 : (uint32_t)__builtin_ctzll(section->alignment);
        const bool atom_matches = atom != 0 && link.atoms.class[atom] == class && link.atoms.alignment_log2[atom] == alignment_log2 && link.atoms.size[atom] == section->size &&
                                  link.atoms.unit[atom] == BUSTER_IMPORT_TEST_UNIT && link.atoms.bytes[atom] == place;
        sections = sections && (class == BUSTER_LINK_CLASS_NONE ? atom == 0 : atom_matches);
        atoms += class != BUSTER_LINK_CLASS_NONE ? 1 : 0;
    }
    sections = sections && link.atoms.count == atoms + 1;
    bool symbols = imported;
    uint32_t kept = 0;
    for (uint32_t i = 0; imported && i < test->symbols.count; i += 1)
    {
        symbols = symbols && import_test_object_symbol(test, &link, &object, i);
        kept += test->symbols.flags[i] != BUSTER_IMPORT_TEST_NONE ? 1 : 0;
    }
    symbols = symbols && link.symbols.count == kept + 1;
    bool fixups = imported;
    uint64_t fixup_count = 0;
    for (uint32_t i = 1; imported && i < IMPORT_TEST_SECTIONS; i += 1)
    {
        const bool kept_section = test->sections[i].type == SHT_RELA && test->classes[test->sections[i].info] != BUSTER_LINK_CLASS_NONE;
        fixups = fixups && (!kept_section || import_test_object_fixups(test, &link, &object, i, &fixup_count));
    }
    fixups = fixups && link.fixups.count == fixup_count + 1;
    // The groups, in section order: each signature and its sections.
    const ImportGroup* groups = object.groups;
    bool grouped = imported && object.group_count == 2;
    grouped = grouped && import_test_name_is((const uint8_t*)groups[0].signature.pointer, (uint32_t)groups[0].signature.length, "DW.ref.signature") &&
              groups[0].count == 2 && object.group_sections[groups[0].first] == IMPORT_TEST_GROUP_DATA &&
              object.group_sections[groups[0].first + 1] == IMPORT_TEST_RELA_GROUP_DATA;
    grouped = grouped && import_test_name_is((const uint8_t*)groups[1].signature.pointer, (uint32_t)groups[1].signature.length, ".stapsdt.base") &&
              groups[1].count == 1 && object.group_sections[groups[1].first] == IMPORT_TEST_BASE;
    // The priorities, by atom: .init_array.00101's, then .fini_array.42's.
    const LinkPriorities* priorities = &link.priorities;
    const bool prioritized = imported && priorities->count == 3 && priorities->atom[1] == object.section_atoms[IMPORT_TEST_INIT_PRIORITY] &&
                             priorities->priority[1] == BUSTER_IMPORT_TEST_INIT_PRIORITY && priorities->atom[2] == object.section_atoms[IMPORT_TEST_FINI_PRIORITY] &&
                             priorities->priority[2] == BUSTER_IMPORT_TEST_FINI_PRIORITY;
    if (result.error != IMPORT_ERROR_NONE && tally->failures < BUSTER_IMPORT_TEST_REPORTED)
    {
        printf("  %s: %s: ", tally->check, what);
        import_test_print_result(result);
        printf("\n");
    }
    import_test_expect(tally, imported, what, 0);
    import_test_expect(tally, sections, "each section an atom of its class, in place in the file, or none", 0);
    import_test_expect(tally, symbols, "each symbol, or none", 0);
    import_test_expect(tally, fixups, "each relocation a fixup in offset order, or none", 0);
    import_test_expect(tally, grouped, "each COMDAT group with its signature and sections", 0);
    import_test_expect(tally, prioritized, "each prioritized init and fini array with its priority", 0);
    import_test_paths(tally, bytes, path, what, 0, arena);
    arena_pop_to(arena, length);
}

// A copy of the object made by hand with each relocation section's entries in offset order (stably), which the vector
// path reads in the file's order: the object made by hand has them out of order, which it sorts first.
static ImportTestFile import_test_sorted(const ImportTestObject* test, const ImportTestFile* base, Arena* arena)
{
    ImportTestFile result = import_test_changed(base, 0, 0, 0, arena);
    for (uint32_t i = 1; result.bytes != 0 && i < IMPORT_TEST_SECTIONS; i += 1)
    {
        const uint32_t count = test->sections[i].type == SHT_RELA ? test->relocations[i].count : 0;
        Elf64_Rela* entries = (Elf64_Rela*)(result.bytes + result.contents[i]);
        for (uint32_t j = 1; j < count; j += 1)
        {
            Elf64_Rela entry;
            memcpy(&entry, &entries[j], sizeof(entry));
            uint32_t place = j;
            while (place > 0 && entries[place - 1].r_offset > entry.r_offset)
            {
                memcpy(&entries[place], &entries[place - 1], sizeof(entry));
                place -= 1;
            }
            memcpy(&entries[place], &entry, sizeof(entry));
        }
    }
    return result;
}

static bool import_test_objects(Arena* arena)
{
    ImportTestTally tally = {
        .check = "objects",
    };
    const uint64_t position = arena->length;
    ImportTestObject* test = BUSTER_ARENA_PUSH_ARRAY(arena, ImportTestObject, 1);
    const bool built = test != 0 && import_test_object(test, arena);
    import_test_expect(&tally, built, "the object made by hand", 0);
    if (built)
    {
        const ImportTestFile* f = &test->file;
        const uint32_t sections = f->count;
        // Out of order, the vector path sorts its relocations; in order, it reads them as they are. Changes that
        // import the same: the section count and the names' index in section 0, .eh_frame as SHT_PROGBITS, and tables
        // without sh_entsize.
        const ImportTestFile sorted = import_test_sorted(test, f, arena);
        for (uint32_t order = 0; order < 2; order += 1)
        {
            const ImportTestFile* g = order == 0 ? f : &sorted;
            const ImportTestPath path = IMPORT_TEST_VECTOR;
            import_test_object_check(&tally, test, import_test_bytes(g), order == 0 ? "the object made by hand imports" : "in offset order", path, arena);
            ImportTestFile changed = import_test_changed(g, offsetof(Elf64_Ehdr, e_shnum), 2, 0, arena);
            changed = import_test_changed(&changed, g->headers[0] + offsetof(Elf64_Shdr, sh_size), 8, sections, arena);
            import_test_object_check(&tally, test, import_test_bytes(&changed), "e_shnum in section 0", path, arena);
            changed = import_test_changed(g, offsetof(Elf64_Ehdr, e_shstrndx), 2, SHN_XINDEX, arena);
            changed = import_test_changed(&changed, g->headers[0] + offsetof(Elf64_Shdr, sh_link), 4, sections - 1, arena);
            import_test_object_check(&tally, test, import_test_bytes(&changed), "e_shstrndx in section 0", path, arena);
            changed = import_test_changed(g, g->headers[IMPORT_TEST_EH_FRAME] + offsetof(Elf64_Shdr, sh_type), 4, SHT_PROGBITS, arena);
            import_test_object_check(&tally, test, import_test_bytes(&changed), ".eh_frame as SHT_PROGBITS", path, arena);
            changed = import_test_changed(g, g->headers[IMPORT_TEST_SYMTAB] + offsetof(Elf64_Shdr, sh_entsize), 8, 0, arena);
            changed = import_test_changed(&changed, g->headers[IMPORT_TEST_RELA_TEXT] + offsetof(Elf64_Shdr, sh_entsize), 8, 0, arena);
            import_test_object_check(&tally, test, import_test_bytes(&changed), "tables without sh_entsize", path, arena);
        }
        ImportTestFile changed = { 0 };

        // Each check a well-formed object never trips. The ELF header and the section table.
        const ImportTestReader o = IMPORT_TEST_READ_OBJECT;
        const ImportResult format = import_test_result(IMPORT_ERROR_FORMAT, 0, 0, 0, 0, 0);
        const ImportResult table = import_test_result(IMPORT_ERROR_BOUNDS, offsetof(Elf64_Ehdr, e_shoff), 0, 0, 0, 0);
        const ImportResult count = import_test_result(IMPORT_ERROR_BOUNDS, offsetof(Elf64_Ehdr, e_shnum), 0, 0, 0, 0);
        const ImportResult names = import_test_result(IMPORT_ERROR_BOUNDS, offsetof(Elf64_Ehdr, e_shstrndx), 0, 0, 0, 0);
        import_test_case(&tally, o, f, "not ELF", 0, 1, 0x7e, format, arena);
        import_test_case(&tally, o, f, "32-bit", EI_CLASS, 1, ELFCLASS32, format, arena);
        import_test_case(&tally, o, f, "big-endian", EI_DATA, 1, ELFDATA2MSB, format, arena);
        import_test_case(&tally, o, f, "EI_VERSION 0", EI_VERSION, 1, 0, format, arena);
        import_test_case(&tally, o, f, "a shared library", offsetof(Elf64_Ehdr, e_type), 2, ET_DYN, format, arena);
        import_test_case(&tally, o, f, "i386", offsetof(Elf64_Ehdr, e_machine), 2, EM_386, format, arena);
        import_test_case(&tally, o, f, "e_version 0", offsetof(Elf64_Ehdr, e_version), 4, 0, format, arena);
        import_test_case(&tally, o, f, "no section table", offsetof(Elf64_Ehdr, e_shoff), 8, 0, table, arena);
        import_test_case(&tally, o, f, "section headers of 40 bytes", offsetof(Elf64_Ehdr, e_shentsize), 2, 40, table, arena);
        import_test_case(&tally, o, f, "the section table at the end", offsetof(Elf64_Ehdr, e_shoff), 8, f->size - 32, table, arena);
        import_test_case(&tally, o, f, "a section past the end", offsetof(Elf64_Ehdr, e_shnum), 2, sections + 1, count, arena);
        import_test_case(&tally, o, f, "e_shnum 0 and no count in section 0", offsetof(Elf64_Ehdr, e_shnum), 2, 0, count, arena);
        import_test_case(&tally, o, f, "the names' index past the sections", offsetof(Elf64_Ehdr, e_shstrndx), 2, sections + 5, names, arena);
        import_test_case(&tally, o, f, "the names' index 0", offsetof(Elf64_Ehdr, e_shstrndx), 2, 0, names, arena);
        import_test_case(&tally, o, f, "names in .text", offsetof(Elf64_Ehdr, e_shstrndx), 2, IMPORT_TEST_TEXT, names, arena);
        import_test_case(&tally, o, f, "SHN_XINDEX without the names' index in section 0", offsetof(Elf64_Ehdr, e_shstrndx), 2, SHN_XINDEX, names, arena);

        // Sections.
        const uint64_t text = f->headers[IMPORT_TEST_TEXT];
        const ImportResult text_bounds = import_test_result(IMPORT_ERROR_BOUNDS, text, IMPORT_TEST_TEXT, 0, 0, 0);
        import_test_case(&tally, o, f, ".text past the end", text + offsetof(Elf64_Shdr, sh_offset), 8, f->size, text_bounds, arena);
        import_test_case(&tally, o, f, ".text's size past the end", text + offsetof(Elf64_Shdr, sh_size), 8, UINT64_MAX, text_bounds, arena);
        import_test_case(&tally, o, f, ".bss's offset, which NOBITS doesn't use", f->headers[IMPORT_TEST_BSS] + offsetof(Elf64_Shdr, sh_offset), 8, UINT64_MAX,
                         import_test_result(IMPORT_ERROR_NONE, 0, 0, 0, 0, 0), arena);
        import_test_case(&tally, o, f, "a name outside .shstrtab", text + offsetof(Elf64_Shdr, sh_name), 4, 0xffff, text_bounds, arena);
        import_test_case(&tally, o, f, "an alignment of 3", text + offsetof(Elf64_Shdr, sh_addralign), 8, 3, text_bounds, arena);
        import_test_case(&tally, o, f, "an alignment of 2^33", text + offsetof(Elf64_Shdr, sh_addralign), 8, 1ull << 33, text_bounds, arena);
        const uint64_t data = f->headers[IMPORT_TEST_DATA];
        import_test_case(&tally, o, f, "SHF_GROUP in no group", data + offsetof(Elf64_Shdr, sh_flags), 8, SHF_WRITE | SHF_ALLOC | SHF_GROUP,
                         import_test_result(IMPORT_ERROR_BOUNDS, data, IMPORT_TEST_DATA, 0, 0, 0), arena);
        import_test_case(&tally, o, f, ".ctors", data + offsetof(Elf64_Shdr, sh_name), 4, f->spare_names[IMPORT_TEST_NAME_CTORS],
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, data, IMPORT_TEST_DATA, 0, 0, SHT_PROGBITS), arena);
        // The groups and the priorities.
        const uint64_t group = f->headers[IMPORT_TEST_GROUP];
        const uint64_t group_contents = f->contents[IMPORT_TEST_GROUP];
        const ImportResult group_bounds = import_test_result(IMPORT_ERROR_BOUNDS, group, IMPORT_TEST_GROUP, 0, 0, 0);
        import_test_case(&tally, o, f, "a group without GRP_COMDAT", group_contents, 4, 0, import_test_result(IMPORT_ERROR_UNSUPPORTED, group, IMPORT_TEST_GROUP, 0, 0, SHT_GROUP),
                         arena);
        import_test_case(&tally, o, f, "a group with another flag", group_contents, 4, GRP_COMDAT | 2,
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, group, IMPORT_TEST_GROUP, 0, 0, SHT_GROUP), arena);
        import_test_case(&tally, o, f, "a group's section past the sections", group_contents + 4, 4, 0xffff, group_bounds, arena);
        import_test_case(&tally, o, f, "a group's section 0", group_contents + 4, 4, 0, group_bounds, arena);
        import_test_case(&tally, o, f, "a group that holds itself", group_contents + 4, 4, IMPORT_TEST_GROUP, group_bounds, arena);
        import_test_case(&tally, o, f, "a group's section without SHF_GROUP", f->headers[IMPORT_TEST_GROUP_DATA] + offsetof(Elf64_Shdr, sh_flags), 8, SHF_WRITE | SHF_ALLOC,
                         group_bounds, arena);
        import_test_case(&tally, o, f, "a group's signature past the symbols", group + offsetof(Elf64_Shdr, sh_info), 4, test->symbols.count, group_bounds, arena);
        import_test_case(&tally, o, f, "a group's signature symbol 0", group + offsetof(Elf64_Shdr, sh_info), 4, 0, group_bounds, arena);
        import_test_case(&tally, o, f, "a group's symbols in .strtab", group + offsetof(Elf64_Shdr, sh_link), 4, IMPORT_TEST_STRTAB, group_bounds, arena);
        import_test_case(&tally, o, f, "a group of part of a word", group + offsetof(Elf64_Shdr, sh_size), 8, 10, group_bounds, arena);
        import_test_case(&tally, o, f, "an empty group", group + offsetof(Elf64_Shdr, sh_size), 8, 0, group_bounds, arena);
        import_test_case(&tally, o, f, "a section in two groups", f->contents[IMPORT_TEST_SECTION_GROUP] + 4, 4, IMPORT_TEST_GROUP_DATA,
                         import_test_result(IMPORT_ERROR_BOUNDS, f->headers[IMPORT_TEST_SECTION_GROUP], IMPORT_TEST_SECTION_GROUP, 0, 0, 0), arena);
        const uint64_t init_priority = f->headers[IMPORT_TEST_INIT_PRIORITY];
        const ImportResult init_unsupported = import_test_result(IMPORT_ERROR_UNSUPPORTED, init_priority, IMPORT_TEST_INIT_PRIORITY, 0, 0, SHT_INIT_ARRAY);
        import_test_case(&tally, o, f, "a priority past 65535", init_priority + offsetof(Elf64_Shdr, sh_name), 4, f->spare_names[IMPORT_TEST_NAME_PRIORITY_HIGH],
                         init_unsupported, arena);
        import_test_case(&tally, o, f, "a priority with a letter", init_priority + offsetof(Elf64_Shdr, sh_name), 4, f->spare_names[IMPORT_TEST_NAME_PRIORITY_LETTER],
                         init_unsupported, arena);
        import_test_case(&tally, o, f, "a priority of six digits", init_priority + offsetof(Elf64_Shdr, sh_name), 4, f->spare_names[IMPORT_TEST_NAME_PRIORITY_DIGITS],
                         init_unsupported, arena);
        const uint64_t fini_priority = f->headers[IMPORT_TEST_FINI_PRIORITY];
        import_test_case(&tally, o, f, "a priority without digits", fini_priority + offsetof(Elf64_Shdr, sh_name), 4, f->spare_names[IMPORT_TEST_NAME_PRIORITY_EMPTY],
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, fini_priority, IMPORT_TEST_FINI_PRIORITY, 0, 0, SHT_FINI_ARRAY), arena);
        import_test_case(&tally, o, f, ".dtors.00042", fini_priority + offsetof(Elf64_Shdr, sh_name), 4, f->spare_names[IMPORT_TEST_NAME_DTORS],
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, fini_priority, IMPORT_TEST_FINI_PRIORITY, 0, 0, SHT_FINI_ARRAY), arena);
        const uint64_t rela_data = f->headers[IMPORT_TEST_RELA_DATA];
        import_test_case(&tally, o, f, "SHT_REL", rela_data + offsetof(Elf64_Shdr, sh_type), 4, SHT_REL,
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, rela_data, IMPORT_TEST_RELA_DATA, 0, 0, SHT_REL), arena);
        const uint64_t init_array = f->headers[IMPORT_TEST_INIT_ARRAY];
        import_test_case(&tally, o, f, "SHT_PREINIT_ARRAY", init_array + offsetof(Elf64_Shdr, sh_type), 4, SHT_PREINIT_ARRAY,
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, init_array, IMPORT_TEST_INIT_ARRAY, 0, 0, SHT_PREINIT_ARRAY), arena);
        const uint64_t rodata = f->headers[IMPORT_TEST_RODATA];
        import_test_case(&tally, o, f, "read-only NOBITS", rodata + offsetof(Elf64_Shdr, sh_type), 4, SHT_NOBITS,
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, rodata, IMPORT_TEST_RODATA, 0, 0, SHT_NOBITS), arena);
        import_test_case(&tally, o, f, "an allocated SHT_HASH", rodata + offsetof(Elf64_Shdr, sh_type), 4, SHT_HASH,
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, rodata, IMPORT_TEST_RODATA, 0, 0, SHT_HASH), arena);
        import_test_case(&tally, o, f, "executable NOBITS", text + offsetof(Elf64_Shdr, sh_type), 4, SHT_NOBITS,
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, text, IMPORT_TEST_TEXT, 0, 0, SHT_NOBITS), arena);

        // The symbol table.
        const uint64_t symtab = f->headers[IMPORT_TEST_SYMTAB];
        const ImportResult symtab_bounds = import_test_result(IMPORT_ERROR_BOUNDS, symtab, IMPORT_TEST_SYMTAB, 0, 0, 0);
        import_test_case(&tally, o, f, "symbols of 16 bytes", symtab + offsetof(Elf64_Shdr, sh_entsize), 8, 16, symtab_bounds, arena);
        import_test_case(&tally, o, f, "a symbol table of part of a symbol", symtab + offsetof(Elf64_Shdr, sh_size), 8, (uint64_t)test->symbols.count * sizeof(Elf64_Sym) - 1,
                         symtab_bounds, arena);
        import_test_case(&tally, o, f, "symbol names in .text", symtab + offsetof(Elf64_Shdr, sh_link), 4, IMPORT_TEST_TEXT, symtab_bounds, arena);
        import_test_case(&tally, o, f, "symbol names nowhere", symtab + offsetof(Elf64_Shdr, sh_link), 4, 0, symtab_bounds, arena);
        changed = import_test_changed(f, rela_data + offsetof(Elf64_Shdr, sh_type), 4, SHT_SYMTAB, arena);
        import_test_case(&tally, o, &changed, "two symbol tables", rela_data + offsetof(Elf64_Shdr, sh_link), 4, IMPORT_TEST_STRTAB,
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, symtab, IMPORT_TEST_SYMTAB, 0, 0, 0), arena);
        changed = import_test_changed(f, rodata + offsetof(Elf64_Shdr, sh_type), 4, SHT_SYMTAB_SHNDX, arena);
        import_test_case(&tally, o, &changed, ".symtab_shndx of another count", rodata + offsetof(Elf64_Shdr, sh_link), 4, IMPORT_TEST_SYMTAB,
                         import_test_result(IMPORT_ERROR_BOUNDS, rodata, IMPORT_TEST_RODATA, 0, 0, 0), arena);

        // Symbols.
        const uint64_t function = f->contents[IMPORT_TEST_SYMTAB] + (uint64_t)test->global_function * sizeof(Elf64_Sym);
        const uint64_t object = f->contents[IMPORT_TEST_SYMTAB] + (uint64_t)test->global_object * sizeof(Elf64_Sym);
        const uint64_t callee = f->contents[IMPORT_TEST_SYMTAB] + (uint64_t)test->callee * sizeof(Elf64_Sym);
        const ImportResult function_bounds = import_test_result(IMPORT_ERROR_BOUNDS, function, 0, test->global_function, 0, 0);
        import_test_case(&tally, o, f, "a name outside .strtab", function + offsetof(Elf64_Sym, st_name), 4, 0xffff, function_bounds, arena);
        import_test_case(&tally, o, f, "a section index past the sections", function + offsetof(Elf64_Sym, st_shndx), 2, sections + 3, function_bounds, arena);
        import_test_case(&tally, o, f, "SHN_XINDEX without .symtab_shndx", function + offsetof(Elf64_Sym, st_shndx), 2, SHN_XINDEX, function_bounds, arena);
        const ImportResult object_unsupported = import_test_result(IMPORT_ERROR_UNSUPPORTED, object, 0, test->global_object, 0, STT_OBJECT);
        const ImportResult imports = import_test_result(IMPORT_ERROR_NONE, 0, 0, 0, 0, 0);
        import_test_case(&tally, o, f, "a global absolute", object + offsetof(Elf64_Sym, st_shndx), 2, SHN_ABS, imports, arena);
        const uint64_t tls = f->contents[IMPORT_TEST_SYMTAB] + (uint64_t)test->tls_global * sizeof(Elf64_Sym);
        import_test_case(&tally, o, f, "an absolute TLS symbol", tls + offsetof(Elf64_Sym, st_shndx), 2, SHN_ABS,
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, tls, 0, test->tls_global, 0, STT_TLS), arena);
        const uint64_t ifunc = f->contents[IMPORT_TEST_SYMTAB] + (uint64_t)test->ifunc * sizeof(Elf64_Sym);
        import_test_case(&tally, o, f, "an absolute IFUNC", ifunc + offsetof(Elf64_Sym, st_shndx), 2, SHN_ABS,
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, ifunc, 0, test->ifunc, 0, STT_GNU_IFUNC), arena);
        import_test_case(&tally, o, f, "a common symbol", object + offsetof(Elf64_Sym, st_shndx), 2, SHN_COMMON, object_unsupported, arena);
        import_test_case(&tally, o, f, "a GNU_UNIQUE symbol", object + offsetof(Elf64_Sym, st_info), 1, ELF64_ST_INFO(STB_GNU_UNIQUE, STT_OBJECT), object_unsupported, arena);
        import_test_case(&tally, o, f, "a global function as an IFUNC", function + offsetof(Elf64_Sym, st_info), 1, ELF64_ST_INFO(STB_GLOBAL, STT_GNU_IFUNC), imports, arena);
        import_test_case(&tally, o, f, "a symbol of type 7", function + offsetof(Elf64_Sym, st_info), 1, ELF64_ST_INFO(STB_GLOBAL, 7),
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, function, 0, test->global_function, 0, 7), arena);
        import_test_case(&tally, o, f, "a global in a dropped section", function + offsetof(Elf64_Sym, st_shndx), 2, IMPORT_TEST_COMMENT,
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, function, 0, test->global_function, 0, STT_FUNC), arena);
        const ImportResult object_link = import_test_result(IMPORT_ERROR_LINK, object, 0, test->global_object, 0, 0);
        import_test_case(&tally, o, f, "a symbol past its section", object + offsetof(Elf64_Sym, st_value), 8, 1, object_link, arena);
        import_test_case(&tally, o, f, "a TLS symbol outside TLS", object + offsetof(Elf64_Sym, st_info), 1, ELF64_ST_INFO(STB_GLOBAL, STT_TLS), object_link, arena);
        import_test_case(&tally, o, f, "an undefined local", callee + offsetof(Elf64_Sym, st_info), 1, ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE),
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, callee, 0, test->callee, 0, STT_NOTYPE), arena);

        // Relocations.
        const uint64_t rela_text = f->headers[IMPORT_TEST_RELA_TEXT];
        const uint64_t call = f->contents[IMPORT_TEST_RELA_TEXT] + (uint64_t)test->call * sizeof(Elf64_Rela);
        const uint64_t call_got = f->contents[IMPORT_TEST_RELA_TEXT] + (uint64_t)test->call_got * sizeof(Elf64_Rela);
        const uint32_t symbols = test->symbols.count;
        import_test_case(&tally, o, f, "a relocation type the representation doesn't have", call + offsetof(Elf64_Rela, r_info), 8, ELF64_R_INFO(test->callee, R_X86_64_GOT32),
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, call, IMPORT_TEST_RELA_TEXT, 0, test->call, R_X86_64_GOT32), arena);
        import_test_case(&tally, o, f, "a symbol past the table", call + offsetof(Elf64_Rela, r_info), 8, ELF64_R_INFO(symbols + 7, R_X86_64_PLT32),
                         import_test_result(IMPORT_ERROR_BOUNDS, call, IMPORT_TEST_RELA_TEXT, symbols + 7, test->call, 0), arena);
        import_test_case(&tally, o, f, "symbol 0", call + offsetof(Elf64_Rela, r_info), 8, ELF64_R_INFO(0, R_X86_64_PLT32),
                         import_test_result(IMPORT_ERROR_BOUNDS, call, IMPORT_TEST_RELA_TEXT, 0, test->call, 0), arena);
        import_test_case(&tally, o, f, "the FILE symbol", call + offsetof(Elf64_Rela, r_info), 8, ELF64_R_INFO(test->file_symbol, R_X86_64_PLT32),
                         import_test_result(IMPORT_ERROR_BOUNDS, call, IMPORT_TEST_RELA_TEXT, test->file_symbol, test->call, 0), arena);
        import_test_case(&tally, o, f, "an offset past 32 bits", call + offsetof(Elf64_Rela, r_offset), 8, 1ull << 32,
                         import_test_result(IMPORT_ERROR_BOUNDS, call, IMPORT_TEST_RELA_TEXT, test->callee, test->call, 0), arena);
        import_test_case(&tally, o, f, "a field past its section", call + offsetof(Elf64_Rela, r_offset), 8, BUSTER_IMPORT_TEST_TEXT_BYTES - 2,
                         import_test_result(IMPORT_ERROR_LINK, call, IMPORT_TEST_RELA_TEXT, 0, test->call, R_X86_64_PLT32), arena);
        import_test_case(&tally, o, f, "fields that overlap", call_got + offsetof(Elf64_Rela, r_offset), 8, 2,
                         import_test_result(IMPORT_ERROR_LINK, call_got, IMPORT_TEST_RELA_TEXT, 0, test->call_got, R_X86_64_GOTPCRELX), arena);
        const ImportResult rela_bounds = import_test_result(IMPORT_ERROR_BOUNDS, rela_text, IMPORT_TEST_RELA_TEXT, 0, 0, 0);
        import_test_case(&tally, o, f, "relocations tied to .strtab", rela_text + offsetof(Elf64_Shdr, sh_link), 4, IMPORT_TEST_STRTAB, rela_bounds, arena);
        import_test_case(&tally, o, f, "relocations of section 0", rela_text + offsetof(Elf64_Shdr, sh_info), 4, 0, rela_bounds, arena);
        import_test_case(&tally, o, f, "relocations of a section past the last", rela_text + offsetof(Elf64_Shdr, sh_info), 4, sections, rela_bounds, arena);
        import_test_case(&tally, o, f, "relocations of 16 bytes", rela_text + offsetof(Elf64_Shdr, sh_entsize), 8, 16, rela_bounds, arena);
        import_test_case(&tally, o, f, "no symbol table", symtab + offsetof(Elf64_Shdr, sh_type), 4, SHT_PROGBITS, rela_bounds, arena);
        const uint64_t data_first = f->contents[IMPORT_TEST_RELA_DATA] + (uint64_t)test->data_first * sizeof(Elf64_Rela);
        import_test_case(&tally, o, f, "relocations in .bss", rela_data + offsetof(Elf64_Shdr, sh_info), 4, IMPORT_TEST_BSS,
                         import_test_result(IMPORT_ERROR_LINK, data_first, IMPORT_TEST_RELA_DATA, 0, test->data_first, R_X86_64_64), arena);
    }

    // An object without a symbol table, as crtn.o is: its .init and .fini, and nothing else.
    static const uint8_t epilogue[] = { 0x48, 0x83, 0xc4, 0x08, 0xc3 };
    ImportTestSection sections[5] = { 0 };
    sections[1] = import_test_section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 1, 0, 0, 0, 0, 0);
    sections[2] = import_test_section(".init", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 1, 0, 0, 0, epilogue, sizeof(epilogue));
    sections[3] = import_test_section(".fini", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 1, 0, 0, 0, epilogue, sizeof(epilogue));
    sections[4] = import_test_section(".note.GNU-stack", SHT_PROGBITS, 0, 1, 0, 0, 0, 0, 0);
    ImportTestFile bare;
    Link link;
    ImportObject object = { 0 };
    const bool bare_built = import_test_elf(&bare, ET_REL, sections, BUSTER_ARRAY_LENGTH(sections), arena);
    const bool linked = bare_built && import_test_link(&link, import_test_bytes(&bare), arena);
    const ImportResult result =
        linked ? import_object(&link, import_test_bytes(&bare), 0, BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE, arena, &object) : import_test_result(IMPORT_ERROR_MEMORY, 0, 0, 0, 0, 0);
    const bool fragments = result.error == IMPORT_ERROR_NONE && link.atoms.count == 4 && link.atoms.class[object.section_atoms[2]] == BUSTER_LINK_CLASS_INIT &&
                           link.atoms.class[object.section_atoms[3]] == BUSTER_LINK_CLASS_FINI && link.atoms.size[object.section_atoms[2]] == sizeof(epilogue);
    import_test_expect(&tally, fragments && link.symbols.count == 1 && link.fixups.count == 1 && object.symbol_count == 0, "an object without a symbol table", 0);
    arena_pop_to(arena, position);
    return import_test_report(&tally);
}

// An input's stack request is independent of its atom map. These original ELF records need no external assembler:
// move exact names across every section lane and tail, retain the first request, and leave lookalikes unchanged.
static bool import_test_stack(Arena* arena)
{
    ImportTestTally tally = {
        .check = "executable stack metadata",
    };
    for (uint32_t tail = 0; tail < BUSTER_IMPORT_BATCH; tail += 1)
    {
        const uint32_t count = BUSTER_IMPORT_BATCH + 2 + tail;
        for (uint32_t note = 1; note < count; note += 1)
        {
            for (uint32_t variant = 0; variant < 6; variant += 1)
            {
                const uint64_t position = arena->length;
                ImportTestSection sections[BUSTER_IMPORT_BATCH * 2 + 2] = { 0 };
                for (uint32_t section = 1; section < count; section += 1)
                {
                    sections[section] = import_test_section(".comment", SHT_PROGBITS, 0, 1, 0, 0, 0, 0, 0);
                }
                const char* name = variant == 3 ? ".note.GNU-stack.extra" : variant == 4 ? ".note.GNU-stac" : ".note.GNU-stack";
                const uint64_t flags = variant == 0 || variant == 5 ? 0 : SHF_EXECINSTR | (variant == 2 ? SHF_EXCLUDE : 0);
                sections[note] = import_test_section(name, variant == 2 ? SHT_NOTE : SHT_PROGBITS, flags, 1, 0, 0, 0, 0, 0);
                uint32_t expected = variant == 1 || variant == 2 ? note : 0;
                if ((expected != 0 || variant == 5) && note + 1 < count)
                {
                    sections[count - 1] = import_test_section(".note.GNU-stack", SHT_PROGBITS, SHF_EXECINSTR, 1, 0, 0, 0, 0, 0);
                    expected = variant == 5 ? count - 1 : expected;
                }
                ImportTestFile file;
                const bool made = import_test_elf(&file, ET_REL, sections, count, arena);
                const uint64_t index = (uint64_t)tail * 1000 + note * 10 + variant;
                import_test_expect(&tally, made, "original stack-note records", index);
                if (made)
                {
                    const String8 bytes = import_test_bytes(&file);
                    import_test_paths(&tally, bytes, IMPORT_TEST_VECTOR, "stack-note exact/vector metadata and tails", index, arena);
                    Link link;
                    ImportObject object = {
                        .executable_stack_section = UINT32_MAX,
                    };
                    const bool linked = import_test_link(&link, bytes, arena);
                    const ImportResult imported = linked ? import_object(&link, bytes, BUSTER_IMPORT_TEST_UNIT, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &object) :
                                                          import_test_result(IMPORT_ERROR_MEMORY, 0, 0, 0, 0, 0);
                    const bool kept = imported.error == IMPORT_ERROR_NONE && object.executable_stack_section == expected && object.section_atoms[note] == 0 && link.atoms.count == 1;
                    import_test_expect(&tally, kept, "first explicit request without an atom", index);
                }
                arena_pop_to(arena, position);
            }
        }
    }
    // The vector section pass captures this batch's marker before validating a rare array priority. Scalar stops
    // at the bad priority first. Retrying must restore the metadata too, not only atom and fixup counts.
    for (uint32_t first = 0; first < 2; first += 1)
    {
        const uint64_t position = arena->length;
        ImportTestSection sections[4] = { 0 };
        const uint32_t invalid = first == 0 ? 1 : 2;
        const uint32_t note = first == 0 ? 2 : 1;
        sections[invalid] = import_test_section(".init_array.65536", SHT_INIT_ARRAY, SHF_ALLOC | SHF_WRITE, 8, 8, 0, 0, 0, 0);
        sections[note] = import_test_section(".note.GNU-stack", SHT_PROGBITS, SHF_EXECINSTR, 1, 0, 0, 0, 0, 0);
        sections[3] = import_test_section(".comment", SHT_PROGBITS, 0, 1, 0, 0, 0, 0, 0);
        ImportTestFile file;
        const bool made = import_test_elf(&file, ET_REL, sections, BUSTER_ARRAY_LENGTH(sections), arena);
        import_test_expect(&tally, made, "fallback stack-note records", first);
        if (made)
        {
            const String8 bytes = import_test_bytes(&file);
            import_test_paths(&tally, bytes, IMPORT_TEST_EXACT, "stack-note metadata on scalar retry", first, arena);
            Link link;
            ImportObject object = {
                .executable_stack_section = UINT32_MAX,
            };
            const bool linked = import_test_link(&link, bytes, arena);
            const ImportResult imported = linked ? import_object(&link, bytes, BUSTER_IMPORT_TEST_UNIT, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, arena, &object) :
                                                  import_test_result(IMPORT_ERROR_MEMORY, 0, 0, 0, 0, 0);
            const uint32_t expected = first == 0 ? 0 : note;
            import_test_expect(&tally, imported.error == IMPORT_ERROR_UNSUPPORTED && imported.section == invalid && imported.offset == file.headers[invalid] &&
                                          imported.type == SHT_INIT_ARRAY && object.executable_stack_section == expected,
                               "scalar retry keeps only earlier validated metadata", first);
        }
        arena_pop_to(arena, position);
    }
    return import_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Archives

#define BUSTER_IMPORT_TEST_MAX_MEMBERS 8
// Enough for an index of several batches of the vector path's.
#define BUSTER_IMPORT_TEST_ARCHIVE_SYMBOLS 64
#define BUSTER_IMPORT_TEST_LONG_NAMES_BYTES 256

// An archive to make: its members, and its symbol index (4 bytes an entry for "/", 8 for "/SYM64/", 0 for none).
typedef struct ImportTestArchive ImportTestArchive;
struct ImportTestArchive
{
    const char* names[BUSTER_IMPORT_TEST_MAX_MEMBERS];
    const uint8_t* bytes[BUSTER_IMPORT_TEST_MAX_MEMBERS];
    uint64_t sizes[BUSTER_IMPORT_TEST_MAX_MEMBERS];
    uint32_t member_count;
    const char* symbols[BUSTER_IMPORT_TEST_ARCHIVE_SYMBOLS];
    uint32_t symbol_members[BUSTER_IMPORT_TEST_ARCHIVE_SYMBOLS];
    uint32_t symbol_count;
    uint32_t width;
    // A thin archive: every member's name in the long names' table, and only its header in the archive.
    bool thin;
};

// A member header as GNU ar writes one: the name field, a date, owner, group and mode, the size, and "`\n".
static void import_test_archive_header(uint8_t* header, const char* name, uint64_t size)
{
    char size_text[16];
    const int size_length = snprintf(size_text, sizeof(size_text), "%llu", (unsigned long long)size);
    memset(header, ' ', BUSTER_IMPORT_ARCHIVE_HEADER_BYTES);
    memcpy(header, name, strlen(name));
    header[16] = '0';
    header[28] = '0';
    header[34] = '0';
    memcpy(header + 40, "644", 3);
    memcpy(header + 48, size_text, (uint64_t)size_length);
    header[58] = '`';
    header[59] = '\n';
}

static void import_test_big_endian(uint8_t* bytes, uint64_t value, uint32_t width)
{
    for (uint32_t i = 0; i < width; i += 1)
    {
        bytes[i] = (uint8_t)(value >> (8 * (width - 1 - i)));
    }
}

// GNU's format: the magic, the symbol index, the long names' table (names of 16 bytes or more, each followed by "/\n"),
// then the members, each at an even offset ("\n" pads the part before).
static bool import_test_archive(ImportTestFile* file, const ImportTestArchive* archive, Arena* arena)
{
    char long_names[BUSTER_IMPORT_TEST_LONG_NAMES_BYTES];
    uint64_t long_name_offsets[BUSTER_IMPORT_TEST_MAX_MEMBERS] = { 0 };
    uint64_t long_length = 0;
    for (uint32_t i = 0; i < archive->member_count; i += 1)
    {
        const uint64_t length = strlen(archive->names[i]);
        if (length >= 16 || archive->thin)
        {
            assert(long_length + length + 2 <= sizeof(long_names));
            long_name_offsets[i] = long_length;
            memcpy(long_names + long_length, archive->names[i], length);
            memcpy(long_names + long_length + length, "/\n", 2);
            long_length += length + 2;
        }
    }
    uint64_t index_size = archive->width != 0 ? archive->width + (uint64_t)archive->symbol_count * archive->width : 0;
    for (uint32_t s = 0; archive->width != 0 && s < archive->symbol_count; s += 1)
    {
        index_size += strlen(archive->symbols[s]) + 1;
    }
    *file = (ImportTestFile){ 0 };
    uint64_t offset = sizeof(BUSTER_IMPORT_ARCHIVE_MAGIC) - 1;
    file->index_header = archive->width != 0 ? offset : 0;
    offset += archive->width != 0 ? BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + index_size + (index_size & 1) : 0;
    file->names_header = long_length != 0 ? offset : 0;
    offset += long_length != 0 ? BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + long_length + (long_length & 1) : 0;
    for (uint32_t i = 0; i < archive->member_count; i += 1)
    {
        const uint64_t contents = archive->thin ? 0 : archive->sizes[i];
        file->headers[i] = offset;
        file->contents[i] = offset + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES;
        offset += BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + contents + (contents & 1);
    }
    file->count = archive->member_count;
    file->size = offset;
    file->bytes = arena_push(arena, file->size, 64);
    if (file->bytes != 0)
    {
        memset(file->bytes, '\n', file->size);
        memcpy(file->bytes, archive->thin ? BUSTER_IMPORT_THIN_ARCHIVE_MAGIC : BUSTER_IMPORT_ARCHIVE_MAGIC, sizeof(BUSTER_IMPORT_ARCHIVE_MAGIC) - 1);
        if (archive->width != 0)
        {
            uint8_t* index = file->bytes + file->index_header + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES;
            import_test_archive_header(file->bytes + file->index_header, archive->width == 4 ? "/" : "/SYM64/", index_size);
            import_test_big_endian(index, archive->symbol_count, archive->width);
            uint64_t name = archive->width + (uint64_t)archive->symbol_count * archive->width;
            for (uint32_t s = 0; s < archive->symbol_count; s += 1)
            {
                const uint64_t length = strlen(archive->symbols[s]);
                import_test_big_endian(index + archive->width + (uint64_t)s * archive->width, file->headers[archive->symbol_members[s]], archive->width);
                memcpy(index + name, archive->symbols[s], length + 1);
                name += length + 1;
            }
        }
        if (long_length != 0)
        {
            import_test_archive_header(file->bytes + file->names_header, "//", long_length);
            memcpy(file->bytes + file->names_header + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES, long_names, long_length);
        }
        for (uint32_t i = 0; i < archive->member_count; i += 1)
        {
            char name[32];
            const uint64_t length = strlen(archive->names[i]);
            if (length >= 16 || archive->thin)
            {
                snprintf(name, sizeof(name), "/%llu", (unsigned long long)long_name_offsets[i]);
            }
            else
            {
                snprintf(name, sizeof(name), "%s/", archive->names[i]);
            }
            import_test_archive_header(file->bytes + file->headers[i], name, archive->sizes[i]);
            if (archive->sizes[i] != 0 && !archive->thin)
            {
                memcpy(file->bytes + file->contents[i], archive->bytes[i], archive->sizes[i]);
            }
        }
    }
    return file->bytes != 0;
}

// Reads an archive made by hand and checks its members (names, bytes in place, header offsets) and index.
static void import_test_archive_check(ImportTestTally* tally, const ImportTestArchive* archive, const ImportTestFile* file, const char* what, Arena* arena)
{
    const uint64_t length = arena->length;
    ImportArchive read;
    const ImportResult result = import_archive(&read, import_test_bytes(file), arena);
    const uint32_t symbols = archive->width != 0 ? archive->symbol_count : 0;
    bool same = result.error == IMPORT_ERROR_NONE && read.member_count == archive->member_count && read.symbol_count == symbols && read.thin == archive->thin;
    for (uint32_t i = 0; same && i < archive->member_count; i += 1)
    {
        // A thin archive's member has no bytes in it: they are the file its name gives.
        const ImportMember* member = &read.members[i];
        same = import_equal(member->name, archive->names[i]) && member->bytes.pointer == (char*)file->bytes + file->contents[i] &&
               member->bytes.length == (archive->thin ? 0 : archive->sizes[i]) && member->offset == file->headers[i];
    }
    for (uint32_t s = 0; same && s < symbols; s += 1)
    {
        same = import_equal(read.symbols[s].name, archive->symbols[s]) && read.symbols[s].member == archive->symbol_members[s];
    }
    import_test_expect(tally, same, what, result.error);
    // The vector path takes an archive with an index of a symbol or more in member order, walking the members without
    // one; the exact path, one without.
    bool indexed = symbols != 0;
    for (uint32_t s = 1; indexed && s < symbols; s += 1)
    {
        indexed = archive->symbol_members[s - 1] <= archive->symbol_members[s];
    }
    import_test_archive_paths(tally, import_test_bytes(file), indexed ? IMPORT_TEST_VECTOR : IMPORT_TEST_EXACT, what, arena);
    arena_pop_to(arena, length);
}

// The little-endian number whose bytes are `text`'s first `width` characters: a header field to write.
static uint64_t import_test_text(const char* text, uint32_t width)
{
    uint64_t result = 0;
    memcpy(&result, text, width);
    return result;
}

static bool import_test_archives(Arena* arena)
{
    ImportTestTally tally = {
        .check = "archives",
    };
    const uint64_t position = arena->length;
    static const uint8_t first[13] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    static const uint8_t second[24] = { 0x7f, 'E', 'L', 'F' };
    static const uint8_t third[8] = { 0xde, 0xad, 0xbe, 0xef };
    static const uint8_t fourth[3] = { 'x', 'y', 'z' };
    ImportTestArchive archive = {
        .names = {
            "short.o",
            "a_long_member_name.o",
            "exactly_15_char",
            "sixteen_chars_ab",
            "empty.o",
        },
        .bytes = {
            first,
            second,
            third,
            fourth,
            0,
        },
        .sizes = {
            sizeof(first),
            sizeof(second),
            sizeof(third),
            sizeof(fourth),
            0,
        },
        .member_count = 5,
        .symbols = {
            "alpha",
            "beta",
            "gamma",
            "delta",
            "epsilon",
        },
        .symbol_members = {
            0,
            1,
            2,
            3,
            4,
        },
        .symbol_count = 5,
        .width = 4,
    };
    ImportTestFile file;
    bool built = import_test_archive(&file, &archive, arena);
    import_test_expect(&tally, built, "the archive made by hand", 0);
    if (built)
    {
        import_test_archive_check(&tally, &archive, &file, "a 32-bit index, long names, padding and a member of no bytes", arena);
        const ImportTestReader a = IMPORT_TEST_READ_ARCHIVE;
        const ImportResult member_bounds = import_test_result(IMPORT_ERROR_BOUNDS, file.headers[1], 0, 0, 0, 0);
        const uint64_t last = file.headers[archive.member_count - 1];
        const uint64_t index = file.index_header;
        const uint64_t index_names = index + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + 4 + 4 * (uint64_t)archive.symbol_count;
        const uint64_t last_name = index_names + strlen("alpha") + strlen("beta") + strlen("gamma") + strlen("delta") + 4 + strlen("epsilon");
        const uint64_t long_names = file.names_header + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES;
        import_test_case(&tally, a, &file, "not an archive", 0, 1, 'X', import_test_result(IMPORT_ERROR_FORMAT, 0, 0, 0, 0, 0), arena);
        // Read as thin, the first member's bytes are where its next header would be.
        import_test_case(&tally, a, &file, "an archive marked thin", 0, 8, import_test_text(BUSTER_IMPORT_THIN_ARCHIVE_MAGIC, 8),
                         import_test_result(IMPORT_ERROR_BOUNDS, file.contents[0], 0, 0, 0, 0), arena);
        import_test_case(&tally, a, &file, "a header without its terminator", file.headers[1] + 58, 1, 'x', member_bounds, arena);
        import_test_case(&tally, a, &file, "a size with a letter", file.headers[1] + 48, 1, 'a', member_bounds, arena);
        import_test_case(&tally, a, &file, "a size with a digit after a space", file.headers[1] + 51, 1, '5', member_bounds, arena);
        import_test_case(&tally, a, &file, "a size of spaces", file.headers[1] + 48, 2, import_test_text("  ", 2), member_bounds, arena);
        import_test_case(&tally, a, &file, "a size past the end", last + 48, 4, import_test_text("9999", 4), import_test_result(IMPORT_ERROR_BOUNDS, last, 0, 0, 0, 0), arena);
        import_test_case(&tally, a, &file, "a BSD name", file.headers[0], 3, import_test_text("#1/", 3),
                         import_test_result(IMPORT_ERROR_UNSUPPORTED, file.headers[0], 0, 0, 0, 0), arena);
        import_test_case(&tally, a, &file, "a long name past the table", file.headers[1] + 1, 2, import_test_text("99", 2), member_bounds, arena);
        import_test_case(&tally, a, &file, "a long name without its '/'", long_names + strlen(archive.names[1]), 1, 'x', member_bounds, arena);
        import_test_case(&tally, a, &file, "an index of more symbols than fit", index + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES, 4, import_test_text("\0\0\x03\xe8", 4),
                         import_test_result(IMPORT_ERROR_BOUNDS, index, 0, 0, 0, 0), arena);
        import_test_case(&tally, a, &file, "an index entry that isn't a member's header", index + BUSTER_IMPORT_ARCHIVE_HEADER_BYTES + 4, 4, import_test_text("\0\0\0\x09", 4),
                         import_test_result(IMPORT_ERROR_BOUNDS, index, 0, 0, 0, 0), arena);
        import_test_case(&tally, a, &file, "an index's last name without its NUL", last_name, 1, 'x',
                         import_test_result(IMPORT_ERROR_BOUNDS, index, 0, archive.symbol_count - 1, 0, 0), arena);
    }
    archive.width = 8;
    built = import_test_archive(&file, &archive, arena);
    import_test_expect(&tally, built, "the archive made by hand", 1);
    if (built)
    {
        import_test_archive_check(&tally, &archive, &file, "a 64-bit index", arena);
    }
    // A thin archive of the same members: only their headers, and every name in the long names' table.
    archive.thin = true;
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        archive.width = width;
        built = import_test_archive(&file, &archive, arena);
        import_test_expect(&tally, built, "the thin archive made by hand", width);
        if (built)
        {
            import_test_archive_check(&tally, &archive, &file, "a thin archive", arena);
        }
    }
    archive.thin = false;
    // A member without a symbol, which the index doesn't list.
    archive.symbol_members[2] = 1;
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        archive.width = width;
        built = import_test_archive(&file, &archive, arena);
        import_test_expect(&tally, built, "the archive made by hand", width);
        if (built)
        {
            import_test_archive_check(&tally, &archive, &file, "a member without a symbol", arena);
        }
    }
    // An index of several batches, with a member's run of symbols across them, a batch of one member's, and a member
    // without a symbol: 9, 1, 0, 16 and 11 symbols.
    static char many_names[BUSTER_IMPORT_TEST_ARCHIVE_SYMBOLS][8];
    static const uint32_t runs[] = { 9, 1, 0, 16, 11 };
    ImportTestArchive many = archive;
    many.symbol_count = 0;
    for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(runs); m += 1)
    {
        for (uint32_t k = 0; k < runs[m]; k += 1)
        {
            snprintf(many_names[many.symbol_count], sizeof(many_names[0]), "s%02u", many.symbol_count);
            many.symbols[many.symbol_count] = many_names[many.symbol_count];
            many.symbol_members[many.symbol_count] = m;
            many.symbol_count += 1;
        }
    }
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        many.width = width;
        built = import_test_archive(&file, &many, arena);
        import_test_expect(&tally, built, "the archive made by hand", width);
        if (built)
        {
            import_test_archive_check(&tally, &many, &file, "an index of several batches", arena);
        }
    }
    archive.width = 0;
    built = import_test_archive(&file, &archive, arena);
    import_test_expect(&tally, built, "the archive made by hand", 2);
    if (built)
    {
        import_test_archive_check(&tally, &archive, &file, "no index", arena);
    }
    archive.width = 4;
    archive.symbol_count = 0;
    built = import_test_archive(&file, &archive, arena);
    import_test_expect(&tally, built, "the archive made by hand", 3);
    if (built)
    {
        import_test_archive_check(&tally, &archive, &file, "an index of no symbols", arena);
    }
    archive.member_count = 0;
    archive.width = 0;
    built = import_test_archive(&file, &archive, arena);
    import_test_expect(&tally, built, "the archive made by hand", 4);
    if (built)
    {
        import_test_archive_check(&tally, &archive, &file, "no members", arena);
    }
    arena_pop_to(arena, position);
    return import_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared libraries

// The sections of the shared library made by hand, by index.
typedef enum ImportTestLibrarySection
{
    IMPORT_TEST_LIBRARY_TEXT = 1,
    IMPORT_TEST_LIBRARY_DATA,
    IMPORT_TEST_LIBRARY_TDATA,
    IMPORT_TEST_LIBRARY_DYNSYM,
    IMPORT_TEST_LIBRARY_DYNSTR,
    IMPORT_TEST_LIBRARY_VERSYM,
    IMPORT_TEST_LIBRARY_VERDEF,
    IMPORT_TEST_LIBRARY_DYNAMIC,
    IMPORT_TEST_LIBRARY_SECTIONS,
} ImportTestLibrarySection;

#define BUSTER_IMPORT_TEST_VERSIONS 3
#define BUSTER_IMPORT_TEST_VERDEF_BYTES (sizeof(Elf64_Verdef) + sizeof(Elf64_Verdaux))
#define BUSTER_IMPORT_TEST_HIDDEN_VERSION 0x8000

typedef struct ImportTestLibrary ImportTestLibrary;
struct ImportTestLibrary
{
    ImportTestFile file;
    // .dynsym: each symbol's export flags (BUSTER_IMPORT_TEST_NONE when it isn't exported), version index and reference.
    ImportTestSymbols symbols;
    uint32_t version_names[BUSTER_IMPORT_TEST_VERSIONS];
    // The symbols the malformed cases change: an export and one at a version other than its name's default.
    uint32_t variable;
    uint32_t hidden;
};

static const char* const import_test_version_names[BUSTER_IMPORT_TEST_VERSIONS] = {
    "libtest.so.1",
    "V1",
    "V2",
};

// The ELF hash function, which version definitions' vd_hash holds.
static uint32_t import_test_elf_hash(const char* name)
{
    uint32_t hash = 0;
    for (const uint8_t* p = (const uint8_t*)name; *p != 0; p += 1)
    {
        hash = (hash << 4) + *p;
        const uint32_t high = hash & 0xf0000000u;
        hash ^= high >> 24;
        hash &= ~high;
    }
    return hash;
}

// Adds a dynamic symbol: its version index, and what it must import as (its export flags or BUSTER_IMPORT_TEST_NONE,
// and its reference: 0, 1, or 2 for a weak one). Returns its index.
static uint32_t import_test_add_dynamic(ImportTestLibrary* test, const char* name, uint8_t info, uint8_t visibility, uint16_t section, uint64_t value, uint64_t size,
                                        uint16_t version, uint8_t flags, uint8_t reference)
{
    const uint32_t result = import_test_add_symbol(&test->symbols, name, info, visibility, section, value, size, flags, 0);
    test->symbols.versions[result] = version;
    test->symbols.references[result] = reference;
    return result;
}

static bool import_test_library(ImportTestLibrary* test, Arena* arena)
{
    uint8_t text[64];
    uint8_t data[32];
    uint8_t tdata[8];
    uint8_t definitions[BUSTER_IMPORT_TEST_VERSIONS * BUSTER_IMPORT_TEST_VERDEF_BYTES];
    Elf64_Dyn dynamic[2];
    memset(text, 0xc3, sizeof(text));
    memset(data, 0x5a, sizeof(data));
    memset(tdata, 0xa5, sizeof(tdata));
    memset(test, 0, sizeof(*test));
    const uint8_t none = BUSTER_IMPORT_TEST_NONE;
    const uint8_t global = BUSTER_LINK_SYMBOL_GLOBAL;
    const uint16_t text_section = IMPORT_TEST_LIBRARY_TEXT;
    const uint16_t data_section = IMPORT_TEST_LIBRARY_DATA;
    import_test_add_dynamic(test, "", 0, 0, SHN_UNDEF, 0, 0, 0, none, 0);
    import_test_add_dynamic(test, "local_function", ELF64_ST_INFO(STB_LOCAL, STT_FUNC), 0, text_section, 56, 8, 0, none, 0);
    test->hidden = import_test_add_dynamic(test, "old_function", ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), 0, text_section, 0, 16, 2 | BUSTER_IMPORT_TEST_HIDDEN_VERSION, none, 0);
    import_test_add_dynamic(test, "old_function", ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), 0, text_section, 16, 16, 3, global | BUSTER_LINK_SYMBOL_FUNCTION, 0);
    test->variable = import_test_add_dynamic(test, "variable", ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), 0, data_section, 0, 8, 2, global | BUSTER_LINK_SYMBOL_OBJECT, 0);
    import_test_add_dynamic(test, "ifunc", ELF64_ST_INFO(STB_GLOBAL, STT_GNU_IFUNC), 0, text_section, 32, 8, 2, global | BUSTER_LINK_SYMBOL_FUNCTION, 0);
    import_test_add_dynamic(test, "weak_function", ELF64_ST_INFO(STB_WEAK, STT_FUNC), 0, text_section, 40, 8, VER_NDX_GLOBAL,
                            BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION, 0);
    import_test_add_dynamic(test, "hidden_function", ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), STV_HIDDEN, text_section, 48, 8, 2, none, 0);
    import_test_add_dynamic(test, "protected_variable", ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), STV_PROTECTED, data_section, 8, 8, 3, global | BUSTER_LINK_SYMBOL_OBJECT, 0);
    import_test_add_dynamic(test, "unique", ELF64_ST_INFO(STB_GNU_UNIQUE, STT_OBJECT), 0, data_section, 16, 8, 2, global | BUSTER_LINK_SYMBOL_OBJECT, 0);
    import_test_add_dynamic(test, "reference", ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), 0, SHN_UNDEF, 0, 0, VER_NDX_GLOBAL, none, 1);
    import_test_add_dynamic(test, "weak_reference", ELF64_ST_INFO(STB_WEAK, STT_NOTYPE), 0, SHN_UNDEF, 0, 0, 0, none, 2);
    import_test_add_dynamic(test, "local_version", ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), 0, text_section, 0, 4, VER_NDX_LOCAL, none, 0);
    import_test_add_dynamic(test, "tls_variable", ELF64_ST_INFO(STB_GLOBAL, STT_TLS), 0, IMPORT_TEST_LIBRARY_TDATA, 0, 8, 2, global | BUSTER_LINK_SYMBOL_TLS, 0);
    import_test_add_dynamic(test, "common", ELF64_ST_INFO(STB_GLOBAL, STT_COMMON), 0, data_section, 24, 8, 3, global | BUSTER_LINK_SYMBOL_OBJECT, 0);
    import_test_add_dynamic(test, "old_function", ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), 0, text_section, 0, 16, 2, global | BUSTER_LINK_SYMBOL_FUNCTION, 0);
    import_test_add_dynamic(test, "absolute", ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT), 0, SHN_ABS, 0, 0, 2, global | BUSTER_LINK_SYMBOL_OBJECT, 0);
    for (uint32_t v = 0; v < BUSTER_IMPORT_TEST_VERSIONS; v += 1)
    {
        test->version_names[v] = import_test_string(&test->symbols.names, import_test_version_names[v]);
    }
    for (uint32_t v = 0; v < BUSTER_IMPORT_TEST_VERSIONS; v += 1)
    {
        const Elf64_Verdef definition = {
            .vd_version = VER_DEF_CURRENT,
            .vd_flags = v == 0 ? VER_FLG_BASE : 0,
            .vd_ndx = (uint16_t)(v + 1),
            .vd_cnt = 1,
            .vd_hash = import_test_elf_hash(import_test_version_names[v]),
            .vd_aux = sizeof(Elf64_Verdef),
            .vd_next = v + 1 < BUSTER_IMPORT_TEST_VERSIONS ? BUSTER_IMPORT_TEST_VERDEF_BYTES : 0,
        };
        const Elf64_Verdaux auxiliary = {
            .vda_name = test->version_names[v],
            .vda_next = 0,
        };
        memcpy(definitions + v * BUSTER_IMPORT_TEST_VERDEF_BYTES, &definition, sizeof(definition));
        memcpy(definitions + v * BUSTER_IMPORT_TEST_VERDEF_BYTES + sizeof(definition), &auxiliary, sizeof(auxiliary));
    }
    const ImportTestSymbols* s = &test->symbols;
    dynamic[0].d_tag = DT_SONAME;
    dynamic[0].d_un.d_val = test->version_names[0];
    dynamic[1].d_tag = DT_NULL;
    dynamic[1].d_un.d_val = 0;
    ImportTestSection sections[IMPORT_TEST_LIBRARY_SECTIONS] = { 0 };
    sections[IMPORT_TEST_LIBRARY_TEXT] = import_test_section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16, 0, 0, 0, text, sizeof(text));
    sections[IMPORT_TEST_LIBRARY_DATA] = import_test_section(".data", SHT_PROGBITS, SHF_WRITE | SHF_ALLOC, 8, 0, 0, 0, data, sizeof(data));
    sections[IMPORT_TEST_LIBRARY_TDATA] = import_test_section(".tdata", SHT_PROGBITS, SHF_WRITE | SHF_ALLOC | SHF_TLS, 8, 0, 0, 0, tdata, sizeof(tdata));
    sections[IMPORT_TEST_LIBRARY_DYNSYM] =
        import_test_section(".dynsym", SHT_DYNSYM, SHF_ALLOC, 8, sizeof(Elf64_Sym), IMPORT_TEST_LIBRARY_DYNSTR, 2, s->entries, (uint64_t)s->count * sizeof(Elf64_Sym));
    sections[IMPORT_TEST_LIBRARY_DYNSTR] = import_test_section(".dynstr", SHT_STRTAB, SHF_ALLOC, 1, 0, 0, 0, s->names.bytes, s->names.length);
    sections[IMPORT_TEST_LIBRARY_VERSYM] =
        import_test_section(".gnu.version", SHT_GNU_versym, SHF_ALLOC, 2, sizeof(uint16_t), IMPORT_TEST_LIBRARY_DYNSYM, 0, s->versions, (uint64_t)s->count * sizeof(uint16_t));
    sections[IMPORT_TEST_LIBRARY_VERDEF] =
        import_test_section(".gnu.version_d", SHT_GNU_verdef, SHF_ALLOC, 8, 0, IMPORT_TEST_LIBRARY_DYNSTR, BUSTER_IMPORT_TEST_VERSIONS, definitions, sizeof(definitions));
    sections[IMPORT_TEST_LIBRARY_DYNAMIC] =
        import_test_section(".dynamic", SHT_DYNAMIC, SHF_WRITE | SHF_ALLOC, 8, sizeof(Elf64_Dyn), IMPORT_TEST_LIBRARY_DYNSTR, 0, dynamic, sizeof(dynamic));
    return import_test_elf(&test->file, ET_DYN, sections, IMPORT_TEST_LIBRARY_SECTIONS, arena);
}

// The library's first export of the name, by import_library_find, as an index into the exports, or UINT32_MAX.
static uint32_t import_test_library_find(const ImportLibrary* library, const char* name)
{
    const uint64_t length = strlen(name);
    return import_library_find(library, (const uint8_t*)name, length, link_hash((const uint8_t*)name, length)) - 1;
}

// Reads the library made by hand (with .gnu.version or, `versioned` false, as if without it) and checks its soname,
// versions, exports, the exports' table, references with their versions' indices, and hidden exports.
static void import_test_library_check(ImportTestTally* tally, const ImportTestLibrary* test, String8 bytes, bool versioned, const char* what, Arena* arena)
{
    const uint64_t length = arena->length;
    const ImportTestSymbols* s = &test->symbols;
    ImportLibrary library;
    const ImportResult result = import_library(&library, bytes, arena);
    bool same = result.error == IMPORT_ERROR_NONE && import_equal(library.soname, import_test_version_names[0]) && library.version_count == BUSTER_IMPORT_TEST_VERSIONS;
    for (uint32_t v = 0; same && v < BUSTER_IMPORT_TEST_VERSIONS; v += 1)
    {
        const ImportVersion* version = &library.versions[v];
        same = import_equal(version->name, import_test_version_names[v]) && version->hash == import_test_elf_hash(import_test_version_names[v]) && version->index == v + 1 &&
               version->base == (v == 0);
    }
    // The exports in .dynsym's order, each found in the table at its name's first export, the references and the hidden
    // exports.
    uint32_t exports = 0;
    uint32_t references = 0;
    uint32_t hiddens = 0;
    for (uint32_t i = 1; same && i < s->count; i += 1)
    {
        const Elf64_Sym* entry = &s->entries[i];
        const char* name = s->names.bytes + entry->st_name;
        const uint32_t type = ELF64_ST_TYPE(entry->st_info);
        const uint32_t binding = ELF64_ST_BIND(entry->st_info);
        // Without versions, every definition that isn't local or hidden is its name's default.
        const bool defined = entry->st_shndx != SHN_UNDEF && binding != STB_LOCAL && ELF64_ST_VISIBILITY(entry->st_other) != STV_HIDDEN;
        const bool exported = versioned ? s->flags[i] != BUSTER_IMPORT_TEST_NONE : defined;
        const uint32_t exported_type = type == STT_GNU_IFUNC ? STT_FUNC : type == STT_COMMON ? STT_OBJECT : type;
        const uint8_t unversioned_flags = (uint8_t)(exported_type | ((binding == STB_WEAK ? STB_WEAK : STB_GLOBAL) << 4));
        if (exported)
        {
            const ImportExports* e = &library.exports;
            const uint8_t flags = versioned ? s->flags[i] : unversioned_flags;
            const uint16_t version = versioned ? s->versions[i] : VER_NDX_GLOBAL;
            const uint32_t first = import_test_library_find(&library, name);
            same = exports < e->count && import_test_name_is(e->name[exports], e->length[exports], name) && e->hash[exports] == link_hash((const uint8_t*)name, strlen(name)) &&
                   e->size[exports] == entry->st_size && e->flags[exports] == flags && e->version[exports] == version && first <= exports &&
                   import_test_name_is(e->name[first], e->length[first], name);
            exports += 1;
        }
        if (s->references[i] != 0)
        {
            const ImportReferences* r = &library.references;
            const uint16_t version = versioned ? (uint16_t)(s->versions[i] & 0x7fff) : VER_NDX_GLOBAL;
            same = same && references < r->count && import_test_name_is(r->name[references], r->length[references], name) &&
                   r->hash[references] == link_hash((const uint8_t*)name, strlen(name)) && r->weak[references] == (s->references[i] == 2 ? 1 : 0) &&
                   r->version[references] == version;
            references += 1;
        }
        // A definition at a version other than its name's default (old_function@V1), with its version's index.
        const uint16_t index = (uint16_t)(s->versions[i] & 0x7fff);
        if (versioned && defined && (s->versions[i] & BUSTER_IMPORT_TEST_HIDDEN_VERSION) != 0 && index > VER_NDX_GLOBAL)
        {
            const ImportHiddenExports* h = &library.hidden;
            same = same && hiddens < h->count && strcmp((const char*)h->name[hiddens], name) == 0 && h->version[hiddens] == index;
            hiddens += 1;
        }
    }
    same = same && library.exports.count == exports && library.references.count == references && library.hidden.count == hiddens && hiddens == (versioned ? 1 : 0);
    // The first export of a name exported twice, and names that aren't exported.
    same = same && import_test_library_find(&library, "old_function") == (versioned ? 0 : 0) && import_test_library_find(&library, "hidden_function") == UINT32_MAX &&
           import_test_library_find(&library, "local_function") == UINT32_MAX && import_test_library_find(&library, "reference") == UINT32_MAX &&
           import_test_library_find(&library, "nothing") == UINT32_MAX;
    same = same && (import_test_library_find(&library, "local_version") == UINT32_MAX) == versioned;
    if (result.error != IMPORT_ERROR_NONE && tally->failures < BUSTER_IMPORT_TEST_REPORTED)
    {
        printf("  %s: %s: ", tally->check, what);
        import_test_print_result(result);
        printf("\n");
    }
    import_test_expect(tally, same, what, exports);
    import_test_library_paths(tally, bytes, IMPORT_TEST_VECTOR, what, arena);
    arena_pop_to(arena, length);
}

static bool import_test_libraries(Arena* arena)
{
    ImportTestTally tally = {
        .check = "shared libraries",
    };
    const uint64_t position = arena->length;
    ImportTestLibrary* test = BUSTER_ARENA_PUSH_ARRAY(arena, ImportTestLibrary, 1);
    const bool built = test != 0 && import_test_library(test, arena);
    import_test_expect(&tally, built, "the shared library made by hand", 0);
    if (built)
    {
        const ImportTestFile* f = &test->file;
        import_test_library_check(&tally, test, import_test_bytes(f), true, "versions, exports and references", arena);
        const ImportTestFile unversioned = import_test_changed(f, f->headers[IMPORT_TEST_LIBRARY_VERSYM] + offsetof(Elf64_Shdr, sh_type), 4, SHT_PROGBITS, arena);
        import_test_library_check(&tally, test, import_test_bytes(&unversioned), false, "without .gnu.version", arena);

        const ImportTestReader l = IMPORT_TEST_READ_LIBRARY;
        const uint64_t dynsym = f->headers[IMPORT_TEST_LIBRARY_DYNSYM];
        const uint64_t versym = f->headers[IMPORT_TEST_LIBRARY_VERSYM];
        const uint64_t verdef = f->headers[IMPORT_TEST_LIBRARY_VERDEF];
        const uint64_t dynamic = f->headers[IMPORT_TEST_LIBRARY_DYNAMIC];
        const uint64_t definitions = f->contents[IMPORT_TEST_LIBRARY_VERDEF];
        const uint64_t variable = f->contents[IMPORT_TEST_LIBRARY_DYNSYM] + (uint64_t)test->variable * sizeof(Elf64_Sym);
        const uint64_t variable_version = f->contents[IMPORT_TEST_LIBRARY_VERSYM] + (uint64_t)test->variable * sizeof(uint16_t);
        const uint64_t hidden_version = f->contents[IMPORT_TEST_LIBRARY_VERSYM] + (uint64_t)test->hidden * sizeof(uint16_t);
        const ImportResult symbols_bounds = import_test_result(IMPORT_ERROR_BOUNDS, f->contents[IMPORT_TEST_LIBRARY_DYNSYM], IMPORT_TEST_LIBRARY_DYNSYM, 0, 0, 0);
        const ImportResult verdef_bounds = import_test_result(IMPORT_ERROR_BOUNDS, verdef, IMPORT_TEST_LIBRARY_VERDEF, 0, 0, 0);
        const ImportResult dynamic_bounds = import_test_result(IMPORT_ERROR_BOUNDS, f->contents[IMPORT_TEST_LIBRARY_DYNAMIC], IMPORT_TEST_LIBRARY_DYNAMIC, 0, 0, 0);
        const uint64_t second = definitions + BUSTER_IMPORT_TEST_VERDEF_BYTES;
        const uint64_t third = definitions + 2 * BUSTER_IMPORT_TEST_VERDEF_BYTES;
        import_test_case(&tally, l, f, "a relocatable object", offsetof(Elf64_Ehdr, e_type), 2, ET_REL, import_test_result(IMPORT_ERROR_FORMAT, 0, 0, 0, 0, 0), arena);
        import_test_case(&tally, l, f, "no .dynsym", dynsym + offsetof(Elf64_Shdr, sh_type), 4, SHT_PROGBITS, import_test_result(IMPORT_ERROR_FORMAT, 0, 0, 0, 0, 0), arena);
        import_test_case(&tally, l, f, "dynamic symbols of 16 bytes", dynsym + offsetof(Elf64_Shdr, sh_entsize), 8, 16, symbols_bounds, arena);
        import_test_case(&tally, l, f, "dynamic symbols' names in .text", dynsym + offsetof(Elf64_Shdr, sh_link), 4, IMPORT_TEST_LIBRARY_TEXT, symbols_bounds, arena);
        import_test_case(&tally, l, f, ".gnu.version shorter than .dynsym", versym + offsetof(Elf64_Shdr, sh_size), 8, ((uint64_t)test->symbols.count - 1) * sizeof(uint16_t),
                         import_test_result(IMPORT_ERROR_BOUNDS, f->contents[IMPORT_TEST_LIBRARY_VERSYM], IMPORT_TEST_LIBRARY_VERSYM, 0, 0, 0), arena);
        import_test_case(&tally, l, f, "more version definitions than fit", verdef + offsetof(Elf64_Shdr, sh_info), 4, 1000, verdef_bounds, arena);
        import_test_case(&tally, l, f, "version names in .text", verdef + offsetof(Elf64_Shdr, sh_link), 4, IMPORT_TEST_LIBRARY_TEXT, verdef_bounds, arena);
        import_test_case(&tally, l, f, "a version definition of version 2", definitions + offsetof(Elf64_Verdef, vd_version), 2, 2,
                         import_test_result(IMPORT_ERROR_BOUNDS, definitions, IMPORT_TEST_LIBRARY_VERDEF, 0, 0, 0), arena);
        import_test_case(&tally, l, f, "a version's name entry past the section", third + offsetof(Elf64_Verdef, vd_aux), 4, 1000,
                         import_test_result(IMPORT_ERROR_BOUNDS, third, IMPORT_TEST_LIBRARY_VERDEF, 0, 0, 0), arena);
        import_test_case(&tally, l, f, "a version name outside .dynstr", second + sizeof(Elf64_Verdef) + offsetof(Elf64_Verdaux, vda_name), 4, 0xffff,
                         import_test_result(IMPORT_ERROR_BOUNDS, second, IMPORT_TEST_LIBRARY_VERDEF, 0, 0, 0), arena);
        import_test_case(&tally, l, f, "version definitions that end early", definitions + offsetof(Elf64_Verdef, vd_next), 4, 0,
                         import_test_result(IMPORT_ERROR_BOUNDS, definitions, IMPORT_TEST_LIBRARY_VERDEF, 0, 0, 0), arena);
        import_test_case(&tally, l, f, "an export at a version not defined", variable_version, 2, 9,
                         import_test_result(IMPORT_ERROR_BOUNDS, variable_version, 0, test->variable, 0, 0), arena);
        // old_function@V1 at a version past the definitions (0x8002 as 0x8007), which GNU ld and lld refuse too.
        import_test_case(&tally, l, f, "a non-default export at a version not defined", hidden_version, 2, 7 | BUSTER_IMPORT_TEST_HIDDEN_VERSION,
                         import_test_result(IMPORT_ERROR_BOUNDS, hidden_version, 0, test->hidden, 0, 0), arena);
        import_test_case(&tally, l, f, "a name outside .dynstr", variable + offsetof(Elf64_Sym, st_name), 4, 0xffff,
                         import_test_result(IMPORT_ERROR_BOUNDS, variable, 0, test->variable, 0, 0), arena);
        import_test_case(&tally, l, f, "a soname outside .dynstr", f->contents[IMPORT_TEST_LIBRARY_DYNAMIC] + offsetof(Elf64_Dyn, d_un), 8, 0xffff, dynamic_bounds, arena);
        import_test_case(&tally, l, f, ".dynamic of 8-byte entries", dynamic + offsetof(Elf64_Shdr, sh_entsize), 8, 8, dynamic_bounds, arena);
        import_test_case(&tally, l, f, ".dynamic's strings in .text", dynamic + offsetof(Elf64_Shdr, sh_link), 4, IMPORT_TEST_LIBRARY_TEXT, dynamic_bounds, arena);
        // .dynsym's names in a string table of no bytes, every st_name 0: even the empty name isn't a string of it.
        const uint64_t text = f->headers[IMPORT_TEST_LIBRARY_TEXT];
        ImportTestFile empty = import_test_changed(f, text + offsetof(Elf64_Shdr, sh_type), 4, SHT_STRTAB, arena);
        empty = import_test_changed(&empty, text + offsetof(Elf64_Shdr, sh_size), 8, 0, arena);
        empty = import_test_changed(&empty, dynsym + offsetof(Elf64_Shdr, sh_link), 4, IMPORT_TEST_LIBRARY_TEXT, arena);
        for (uint32_t i = 1; i < test->symbols.count; i += 1)
        {
            empty = import_test_changed(&empty, f->contents[IMPORT_TEST_LIBRARY_DYNSYM] + (uint64_t)i * sizeof(Elf64_Sym) + offsetof(Elf64_Sym, st_name), 4, 0, arena);
        }
        const ImportResult empty_result = import_test_read(l, import_test_bytes(&empty), arena);
        const ImportResult expected = import_test_result(IMPORT_ERROR_BOUNDS, f->contents[IMPORT_TEST_LIBRARY_DYNSYM] + sizeof(Elf64_Sym), 0, 1, 0, 0);
        import_test_expect(&tally, import_test_same_result(empty_result, expected), "names in a string table of no bytes", empty_result.error);
        import_test_library_paths(&tally, import_test_bytes(&empty), IMPORT_TEST_EXACT, "names in a string table of no bytes", arena);
        // An export with st_name 0 in a .dynstr that doesn't start with a NUL: the exact path exports it under the name the
        // table starts with, so the vector path, which takes st_name 0 as the empty name, must leave it to the exact path.
        ImportTestFile unnamed = import_test_changed(f, f->contents[IMPORT_TEST_LIBRARY_DYNSTR], 1, 'X', arena);
        unnamed = import_test_changed(&unnamed, variable + offsetof(Elf64_Sym, st_name), 4, 0, arena);
        ImportLibrary original;
        ImportLibrary unnamed_library;
        const ImportResult original_result = import_library(&original, import_test_bytes(f), arena);
        const ImportResult unnamed_result = import_library(&unnamed_library, import_test_bytes(&unnamed), arena);
        import_test_expect(&tally, original_result.error == IMPORT_ERROR_NONE && unnamed_result.error == IMPORT_ERROR_NONE && unnamed_library.exports.count == original.exports.count,
                           "an export with st_name 0 in a .dynstr that doesn't start with a NUL", unnamed_library.exports.count);
        import_test_library_paths(&tally, import_test_bytes(&unnamed), IMPORT_TEST_EXACT, "an export with st_name 0 in a .dynstr that doesn't start with a NUL", arena);
    }
    arena_pop_to(arena, position);
    return import_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Linker scripts

// Reads `text` as a linker script and checks the error and its offset, or the inputs, as `inputs` writes them:
// separated by spaces, each "?" first if it is as needed, then "-l" and the name of a library, or the path.
static void import_test_script(ImportTestTally* tally, const char* text, ImportError error, uint64_t offset, const char* inputs, Arena* arena)
{
    const uint64_t length = arena->length;
    const String8 bytes = {
        .pointer = (char*)text,
        .length = strlen(text),
    };
    ImportScript script;
    const ImportResult result = import_script(&script, bytes, arena);
    char written[512];
    uint64_t written_length = 0;
    written[0] = 0;
    for (uint32_t i = 0; result.error == IMPORT_ERROR_NONE && i < script.input_count; i += 1)
    {
        const ImportScriptInput* input = &script.inputs[i];
        const int count = snprintf(written + written_length, sizeof(written) - written_length, "%s%s%s%.*s", i != 0 ? " " : "", input->as_needed ? "?" : "",
                                   input->library ? "-l" : "", (int)input->name.length, input->name.pointer);
        written_length += count > 0 && (uint64_t)count < sizeof(written) - written_length ? (uint64_t)count : 0;
    }
    const bool same = result.error == error && result.offset == offset && (error != IMPORT_ERROR_NONE || strcmp(written, inputs) == 0);
    if (!same && tally->failures < BUSTER_IMPORT_TEST_REPORTED)
    {
        printf("  %s: \"%s\": ", tally->check, text);
        import_test_print_result(result);
        printf(", inputs \"%s\"\n", written);
    }
    tally->checked += 1;
    tally->failures += same ? 0 : 1;
    arena_pop_to(arena, length);
}

// A script of libc.so's form (its paths as Arch Linux installs them, after a comment of two lines), GCC's
// libgcc_s_asneeded.so, and one with commas.
static const char import_test_libc_script[] = "/* A linker script that names libc's files:\n"
                                              "   the shared library first, then its static part.  */\n"
                                              "OUTPUT_FORMAT(elf64-x86-64)\n"
                                              "GROUP ( /usr/lib/libc.so.6 /usr/lib/libc_nonshared.a  AS_NEEDED ( /usr/lib/ld-linux-x86-64.so.2 ) )\n";
static const char import_test_asneeded_script[] = "INPUT ( AS_NEEDED ( -lgcc_s ) )\n";
static const char import_test_commas_script[] = "GROUP(a.o,b.o , -lm)INPUT(c.o)";

static bool import_test_scripts(Arena* arena)
{
    ImportTestTally tally = {
        .check = "linker scripts",
    };
    const ImportError none = IMPORT_ERROR_NONE;
    const ImportError error = IMPORT_ERROR_SCRIPT;
    import_test_script(&tally, import_test_libc_script, none, 0, "/usr/lib/libc.so.6 /usr/lib/libc_nonshared.a ?/usr/lib/ld-linux-x86-64.so.2", arena);
    import_test_script(&tally, import_test_asneeded_script, none, 0, "?-lgcc_s", arena);
    import_test_script(&tally, import_test_commas_script, none, 0, "a.o b.o -lm c.o", arena);
    import_test_script(&tally, "OUTPUT_FORMAT(\"elf64-x86-64\", \"elf64-x86-64\", \"elf64-x86-64\") GROUP ( x )", none, 0, "x", arena);
    import_test_script(&tally, "GROUP ( -l )", none, 0, "-l", arena);
    import_test_script(&tally, "", none, 0, "", arena);
    import_test_script(&tally, "/* only a comment */\n", none, 0, "", arena);
    import_test_script(&tally, "SEARCH_DIR(/usr/lib)", error, 0, "", arena);
    import_test_script(&tally, "GROUP /a.o", error, 0, "", arena);
    import_test_script(&tally, "GROUP ( a.o", error, 11, "", arena);
    import_test_script(&tally, "/* unterminated", error, 0, "", arena);
    import_test_script(&tally, "GROUP ( a /* unterminated", error, 10, "", arena);
    import_test_script(&tally, "GROUP ( a ) /* x", error, 12, "", arena);
    import_test_script(&tally, "GROUP ( AS_NEEDED ( AS_NEEDED ( a ) ) )", error, 20, "", arena);
    import_test_script(&tally, "GROUP ( ( a ) )", error, 8, "", arena);
    import_test_script(&tally, "GROUP ( a ) )", error, 12, "", arena);
    import_test_script(&tally, "AS_NEEDED ( a )", error, 0, "", arena);
    import_test_script(&tally, "GROUP ( AS_NEEDED a )", error, 18, "", arena);
    import_test_script(&tally, "OUTPUT_FORMAT", error, 0, "", arena);
    return import_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Page edges

// An input of the page edges' check.
typedef struct ImportTestInput ImportTestInput;
struct ImportTestInput
{
    const char* name;
    const uint8_t* bytes;
    uint64_t size;
};

#define BUSTER_IMPORT_TEST_INPUTS 8

// The input being read, for the fault handler.
static char import_test_fault_input[256];
static uint64_t import_test_fault_length;

// The SIGSEGV and SIGBUS handler while the page edges are read: a reader read a guard page, outside its input. It only
// makes async-signal-safe calls.
static void import_test_report_fault(int signal_number)
{
    static const char prefix[] = "page edges: ";
    static const char suffix[] = " faults: a reader reads outside its input\n";
    ssize_t written = write(STDOUT_FILENO, prefix, BUSTER_COMPILE_TIME_STRING_LENGTH(prefix));
    written += write(STDOUT_FILENO, import_test_fault_input, import_test_fault_length);
    written += write(STDOUT_FILENO, suffix, BUSTER_COMPILE_TIME_STRING_LENGTH(suffix));
    (void)written;
    (void)signal_number;
    _exit(1);
}

// Every reader on the `length` bytes before `end`, a guard page: each must succeed, or fail with a place within them.
static bool import_test_edge(const uint8_t* end, uint64_t length, Arena* arena)
{
    const String8 bytes = {
        .pointer = (char*)end - length,
        .length = length,
    };
    bool result = true;
    for (uint32_t reader = 0; reader < IMPORT_TEST_READERS; reader += 1)
    {
        const ImportResult read = import_test_read((ImportTestReader)reader, bytes, arena);
        result = result && read.offset <= length;
    }
    // And the object and library readers' paths agree.
    bool taken = false;
    result = result && import_test_paths_agree(bytes, 0, &taken, arena);
    result = result && import_test_library_paths_agree(bytes, &taken, arena);
    result = result && import_test_archive_paths_agree(bytes, &taken, arena);
    return result;
}

// Describes the input the next reads are of, for the fault handler.
static void import_test_describe(const char* name, const char* change, uint64_t at, uint32_t value)
{
    const int written = snprintf(import_test_fault_input, sizeof(import_test_fault_input), "%s %s %llu%s0x%02x", name, change, (unsigned long long)at,
                                 value <= 0xff ? " set to " : "", value <= 0xff ? value : 0);
    import_test_fault_length = written > 0 ? (uint64_t)written : 0;
    import_test_fault_length = import_test_fault_length < sizeof(import_test_fault_input) ? import_test_fault_length : sizeof(import_test_fault_input) - 1;
}

static bool import_test_page_edges(const ImportTestInput* inputs, uint32_t count, uint64_t seed, Arena* arena)
{
    ImportTestTally tally = {
        .check = "page edges",
    };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint64_t largest = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        largest = inputs[i].size > largest ? inputs[i].size : largest;
    }
    uint64_t mapped = 0;
    uint8_t* end = x86_map_guarded(largest, page, &mapped);
    import_test_expect(&tally, end != 0, "the guarded mapping", 0);
    uint64_t prefixes = 0;
    uint64_t changes = 0;
    if (end != 0)
    {
        struct sigaction action;
        struct sigaction previous_segv;
        struct sigaction previous_bus;
        memset(&action, 0, sizeof(action));
        action.sa_handler = import_test_report_fault;
        sigaction(SIGSEGV, &action, &previous_segv);
        sigaction(SIGBUS, &action, &previous_bus);
        fflush(stdout);
        uint64_t state = seed * 0x9e3779b97f4a7c15ull + 1;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const ImportTestInput* input = &inputs[i];
            for (uint64_t length = 0; length <= input->size; length += 1)
            {
                import_test_describe(input->name, "cut to bytes:", length, 0x100);
                memcpy(end - length, input->bytes, length);
                import_test_expect(&tally, import_test_edge(end, length, arena), input->name, length);
                prefixes += 1;
            }
            for (uint64_t position = 0; position < input->size; position += 1)
            {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                const uint8_t values[3] = {
                    0,
                    0xff,
                    (uint8_t)state,
                };
                for (uint32_t v = 0; v < 3; v += 1)
                {
                    import_test_describe(input->name, "with byte", position, values[v]);
                    memcpy(end - input->size, input->bytes, input->size);
                    end[position - input->size] = values[v];
                    import_test_expect(&tally, import_test_edge(end, input->size, arena), input->name, position);
                    changes += 1;
                }
            }
        }
        sigaction(SIGSEGV, &previous_segv, 0);
        sigaction(SIGBUS, &previous_bus, 0);
        munmap(end - ((largest + page - 1) / page) * page - page, mapped);
    }
    printf("  %u inputs: %llu prefixes and %llu changed bytes, ending at a PROT_NONE page, through every reader\n", count, (unsigned long long)prefixes,
           (unsigned long long)changes);
    return import_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The runtime

// Makes an empty file at `path`.
static bool import_test_touch(const char* path)
{
    const int file = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    const bool result = file >= 0;
    if (result)
    {
        close(file);
    }
    return result;
}

// GCC's search under a directory of the test's own, with the versions 15, which has crtbeginS.o, and 16, a directory
// without it: the search picks 15, and probes 16's crtbeginS.o too, so that installing it there leaves the probes
// stale (runtime_probes_current) though no directory the search read changes; a fresh search then picks 16, and the
// probes show its crtbeginS.o removed again.
static bool import_test_discovery(void)
{
    ImportTestTally tally = {
        .check = "discovery",
    };
    const char* temporary = getenv("TMPDIR");
    char root[BUSTER_RUNTIME_PATH_BYTES];
    char triple[BUSTER_RUNTIME_PATH_BYTES];
    char older[BUSTER_RUNTIME_PATH_BYTES];
    char newer[BUSTER_RUNTIME_PATH_BYTES];
    char older_crt[BUSTER_RUNTIME_PATH_BYTES];
    char newer_crt[BUSTER_RUNTIME_PATH_BYTES];
    snprintf(root, sizeof(root), "%s/buster-import-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    const bool made = mkdtemp(root) != 0;
    snprintf(triple, sizeof(triple), "%s/x86_64-pc-linux-gnu", root);
    snprintf(older, sizeof(older), "%s/15", triple);
    snprintf(newer, sizeof(newer), "%s/16", triple);
    snprintf(older_crt, sizeof(older_crt), "%s/crtbeginS.o", older);
    snprintf(newer_crt, sizeof(newer_crt), "%s/crtbeginS.o", newer);
    const bool tree = made && mkdir(triple, 0755) == 0 && mkdir(older, 0755) == 0 && mkdir(newer, 0755) == 0 && import_test_touch(older_crt);
    import_test_expect(&tally, tree, "the versions' directories", 0);
    const char* const bases[] = {
        root,
    };
    Runtime runtime;
    const bool begun = runtime_begin(&runtime);
    const bool found = tree && begun && runtime_find_gcc(&runtime, bases, BUSTER_ARRAY_LENGTH(bases));
    import_test_expect(&tally, found && import_equal(runtime.gcc_directory, older), "the highest version with crtbeginS.o", 0);
    bool probed = false;
    for (uint32_t p = 0; found && p < runtime.probe_count; p += 1)
    {
        probed = probed || (import_equal(runtime.probes[p].path, newer_crt) && !runtime.probes[p].found && !runtime.probes[p].directory);
    }
    import_test_expect(&tally, probed, "the newer version's missing crtbeginS.o among the probes", runtime.probe_count);
    import_test_expect(&tally, found && runtime_probes_current(&runtime), "the probes current while nothing changes", 0);
    const bool installed = found && import_test_touch(newer_crt);
    import_test_expect(&tally, installed && !runtime_probes_current(&runtime), "crtbeginS.o installed into a version's existing directory shows", 0);
    Runtime fresh;
    const bool fresh_begun = runtime_begin(&fresh);
    const bool fresh_found = installed && fresh_begun && runtime_find_gcc(&fresh, bases, BUSTER_ARRAY_LENGTH(bases));
    import_test_expect(&tally, fresh_found && import_equal(fresh.gcc_directory, newer) && runtime_probes_current(&fresh), "a fresh search picks it", 0);
    import_test_expect(&tally, fresh_found && unlink(newer_crt) == 0 && !runtime_probes_current(&fresh), "its crtbeginS.o removed again shows", 0);
    runtime_release(&runtime);
    runtime_release(&fresh);
    unlink(newer_crt);
    unlink(older_crt);
    rmdir(newer);
    rmdir(older);
    rmdir(triple);
    if (made)
    {
        rmdir(root);
    }
    return import_test_report(&tally);
}

// The runtime's symbol of that name, defined or not as `defined` says, or 0.
static SymbolId import_test_runtime_symbol(const Runtime* runtime, const char* name, bool defined)
{
    const Link* link = &runtime->link;
    SymbolId result = 0;
    const uint64_t length = strlen(name);
    for (SymbolId symbol = 1; result == 0 && symbol < link->symbols.count; symbol += 1)
    {
        const NameId id = link->symbols.name[symbol];
        const bool match = id != 0 && link->names.length[id] == length && memcmp(link->names.bytes[id], name, length) == 0;
        result = match && (link->symbols.atom[symbol] != 0) == defined ? symbol : 0;
    }
    return result;
}

// The name of the library's version `index`, or an empty slice.
static String8 import_test_version_name(const ImportLibrary* library, uint16_t index)
{
    String8 result = { 0 };
    for (uint32_t v = 0; v < library->version_count; v += 1)
    {
        result = library->versions[v].index == index ? library->versions[v].name : result;
    }
    return result;
}

// The name of a version a link's symbol holds (LinkSymbols.version): libc.so.6's, the loader's or libgcc_s.so.1's,
// whose indices runtime.c numbers apart.
static __attribute__((unused)) String8 import_test_runtime_version_name(const Runtime* runtime, uint16_t index)
{
    const String8 libc = import_test_version_name(&runtime->libc, index);
    const String8 loader = libc.length != 0 ? libc : import_test_version_name(&runtime->loader, index);
    return loader.length != 0 ? loader : import_test_version_name(&runtime->gcc_s, index);
}

// Whether libc exports `name` at version `version` ("" for unversioned).
static bool import_test_exported(const ImportLibrary* library, const char* name, const char* version)
{
    const uint32_t found = import_test_library_find(library, name);
    const uint16_t index = found < library->exports.count ? library->exports.version[found] : VER_NDX_LOCAL;
    return found < library->exports.count && (index == VER_NDX_GLOBAL ? version[0] == 0 : import_equal(import_test_version_name(library, index), version));
}

static void import_test_cross_check(ImportTestTally* tally, const Runtime* runtime, Arena* arena);
static bool import_test_names(const Link* link, String8 bytes, const ImportObject* object, SymbolId symbol, const char* name);
static void import_test_cross_check_relocations(ImportTestTally* tally, const Runtime* runtime, Arena* arena);
static bool import_test_path_is(String8 path, const char* name);

// libgcc_s.so.1 in a dynamic PIE's or a shared library's runtime (#489): its soname, the unwinder's exports at their
// default versions, its version indices past libc.so.6's and the loader's, and none of its exports one of theirs, as
// binding's order needs (bind.c): the driver names it before libc.so, and binding takes libc.so.6's exports first. On a
// system without it (runtime_find_gcc_s found none), the runtime's library must be empty instead. Whether it has one.
static bool import_test_runtime_gcc_s(ImportTestTally* tally, const Runtime* runtime)
{
    const ImportLibrary* gcc_s = &runtime->gcc_s;
    const bool present = runtime->gcc_s_path.length != 0;
    if (!present)
    {
        printf("  %s: this system has no libgcc_s.so.1, so the runtime has none\n", tally->check);
        import_test_expect(tally, gcc_s->soname.length == 0 && gcc_s->version_count == 0 && gcc_s->exports.count == 0 && runtime->gcc_s_bytes.length == 0,
                           "without libgcc_s.so.1, an empty library", 0);
    }
    else
    {
        import_test_expect(tally, import_test_path_is(runtime->gcc_s_path, "libgcc_s.so.1") && import_equal(gcc_s->soname, "libgcc_s.so.1"),
                           "libgcc_s.so.1's path and soname", 0);
        import_test_expect(tally, import_test_exported(gcc_s, "_Unwind_Resume", "GCC_3.0") && import_test_exported(gcc_s, "_Unwind_Backtrace", "GCC_3.3") &&
                                      import_test_exported(gcc_s, "__gcc_personality_v0", "GCC_3.3.1") && import_test_exported(gcc_s, "__udivti3", "GCC_3.0"),
                           "_Unwind_Resume and __udivti3 at GCC_3.0, _Unwind_Backtrace at GCC_3.3, __gcc_personality_v0 at GCC_3.3.1", 0);
        uint16_t below = 0;
        uint16_t smallest = UINT16_MAX;
        for (uint32_t v = 0; v < runtime->libc.version_count; v += 1)
        {
            below = runtime->libc.versions[v].index > below ? runtime->libc.versions[v].index : below;
        }
        for (uint32_t v = 0; v < runtime->loader.version_count; v += 1)
        {
            below = runtime->loader.versions[v].index > below ? runtime->loader.versions[v].index : below;
        }
        for (uint32_t v = 0; v < gcc_s->version_count; v += 1)
        {
            smallest = gcc_s->versions[v].index < smallest ? gcc_s->versions[v].index : smallest;
        }
        import_test_expect(tally, gcc_s->version_count != 0 && smallest > below, "libgcc_s.so.1's version indices past libc.so.6's and the loader's", smallest);
        uint32_t shared = 0;
        for (uint32_t e = 0; e < gcc_s->exports.count; e += 1)
        {
            const uint8_t* name = gcc_s->exports.name[e];
            const uint32_t length = gcc_s->exports.length[e];
            const uint64_t hash = gcc_s->exports.hash[e];
            shared += import_library_find(&runtime->libc, name, length, hash) != 0 || import_library_find(&runtime->loader, name, length, hash) != 0 ? 1 : 0;
        }
        import_test_expect(tally, gcc_s->exports.count != 0 && shared == 0, "libgcc_s.so.1's exports none of libc.so.6's or the loader's", shared);
    }
    return present;
}

// The names the linker of the runtime's kind defines among its global names, each marked the linker's, whether or not
// the runtime refers to it, and no other name marked: by runtime_linker_names' kinds, for a static PIE _DYNAMIC but not
// the IRELATIVE relocations' bounds, which a static executable's linker defines.
static void import_test_linker_names(ImportTestTally* tally, const Runtime* runtime)
{
    const RuntimeNames* global_names = &runtime->global_names;
    uint32_t linker_found = 0;
    uint32_t linker_marked = 0;
    uint32_t linker_count = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(runtime_linker_names); i += 1)
    {
        const uint8_t* bytes = (const uint8_t*)runtime_linker_names[i].name;
        const uint32_t length = (uint32_t)strlen(runtime_linker_names[i].name);
        const uint32_t entry = runtime_find_global_name(global_names, bytes, length, link_hash(bytes, length));
        const bool defined = (runtime_linker_names[i].kinds & BUSTER_RUNTIME_KIND_BIT(runtime->kind)) != 0;
        linker_count += defined ? 1 : 0;
        linker_found += defined && entry != 0 && global_names->linker[entry - 1] ? 1 : 0;
    }
    for (uint32_t e = 0; e < global_names->count; e += 1)
    {
        linker_marked += global_names->linker[e] ? 1 : 0;
    }
    import_test_expect(tally, linker_found == linker_count && linker_marked == linker_found, "the names the kind's linker defines, the linker's, and no other", linker_found);
}

static bool import_test_runtime(bool cross_check, Arena* arena)
{
    ImportTestTally tally = {
        .check = "runtime",
    };
    Runtime runtime;
    const RuntimeError error = runtime_import(&runtime);
    import_test_expect(&tally, error == RUNTIME_ERROR_NONE, "runtime_import", (uint64_t)error);
    if (error != RUNTIME_ERROR_NONE)
    {
        printf("  runtime_import failed: error %d, %s, import error %s at offset %llu (section %u, symbol %u, relocation %u, type %u), errno %d\n", (int)error,
               runtime.failed_file, import_test_errors[runtime.import.error], (unsigned long long)runtime.import.offset, runtime.import.section, runtime.import.symbol,
               runtime.import.relocation, runtime.import.type, runtime.system_error);
    }
    else
    {
        printf("  GCC: %.*s; libc: %.*s (%.*s, %u exports, %u versions, %u references); libgcc.a and libc_nonshared.a: %u members; %u probes\n",
               (int)runtime.gcc_directory.length, runtime.gcc_directory.pointer, (int)runtime.libc_path.length, runtime.libc_path.pointer, (int)runtime.libc.soname.length,
               runtime.libc.soname.pointer, runtime.libc.exports.count, runtime.libc.version_count, runtime.libc.references.count, runtime.member_count, runtime.probe_count);
        const Link* link = &runtime.link;
        // What each crt object is for.
        const SymbolId start = import_test_runtime_symbol(&runtime, "_start", true);
        const SymbolId init = import_test_runtime_symbol(&runtime, "_init", true);
        const SymbolId fini = import_test_runtime_symbol(&runtime, "_fini", true);
        const SymbolId dso = import_test_runtime_symbol(&runtime, "__dso_handle", true);
        const SymbolId tmc = import_test_runtime_symbol(&runtime, "__TMC_END__", true);
        import_test_expect(&tally, start != 0 && link->atoms.unit[link->symbols.atom[start]] == RUNTIME_OBJECT_START && link->atoms.class[link->symbols.atom[start]] == BUSTER_LINK_CLASS_TEXT,
                           "_start in Scrt1.o's .text", 0);
        import_test_expect(&tally, init != 0 && link->atoms.unit[link->symbols.atom[init]] == RUNTIME_OBJECT_INIT_PROLOGUE && link->atoms.class[link->symbols.atom[init]] == BUSTER_LINK_CLASS_INIT,
                           "_init in crti.o's .init", 0);
        import_test_expect(&tally, fini != 0 && link->atoms.class[link->symbols.atom[fini]] == BUSTER_LINK_CLASS_FINI, "_fini in crti.o's .fini", 0);
        import_test_expect(&tally, dso != 0 && link->atoms.unit[link->symbols.atom[dso]] == RUNTIME_OBJECT_BEGIN && (link->symbols.flags[dso] & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_HIDDEN,
                           "__dso_handle, hidden, in crtbeginS.o", 0);
        import_test_expect(&tally, tmc != 0 && link->atoms.unit[link->symbols.atom[tmc]] == RUNTIME_OBJECT_END, "__TMC_END__ in crtendS.o", 0);
        // Both .init fragments, crti.o's first, and the crt objects' .eh_frame.
        uint32_t init_atoms = 0;
        uint32_t eh_frame_atoms = 0;
        AtomId first_init = 0;
        for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
        {
            const bool fragment = link->atoms.class[atom] == BUSTER_LINK_CLASS_INIT;
            first_init = fragment && first_init == 0 ? atom : first_init;
            init_atoms += fragment ? 1 : 0;
            eh_frame_atoms += link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME ? 1 : 0;
        }
        import_test_expect(&tally, init_atoms == 2 && link->atoms.unit[first_init] == RUNTIME_OBJECT_INIT_PROLOGUE, "two .init fragments, crti.o's first", init_atoms);
        import_test_expect(&tally, eh_frame_atoms >= 2, ".eh_frame from Scrt1.o and crtendS.o", eh_frame_atoms);
        // libc.so.6's exports at their default versions.
        import_test_expect(&tally, import_equal(runtime.libc.soname, "libc.so.6"), "libc.so.6's soname", 0);
        import_test_expect(&tally, import_test_exported(&runtime.libc, "write", "GLIBC_2.2.5") && import_test_exported(&runtime.libc, "strlen", "GLIBC_2.2.5") &&
                                       import_test_exported(&runtime.libc, "environ", "GLIBC_2.2.5") && import_test_exported(&runtime.libc, "__errno_location", "GLIBC_2.2.5"),
                           "write, strlen, environ and __errno_location at GLIBC_2.2.5", 0);
        import_test_expect(&tally, import_test_exported(&runtime.libc, "__libc_start_main", "GLIBC_2.34"), "__libc_start_main at GLIBC_2.34", 0);
        bool relr = false;
        for (uint32_t v = 0; v < runtime.libc.version_count; v += 1)
        {
            relr = relr || import_equal(runtime.libc.versions[v].name, "GLIBC_ABI_DT_RELR");
        }
        import_test_expect(&tally, relr, "GLIBC_ABI_DT_RELR defined", 0);
        import_test_expect(&tally, runtime.libc.references.count != 0, "libc.so.6's references", 0);
        // The loader's exports at their default versions, its version indices moved past libc.so.6's.
        import_test_expect(&tally, import_equal(runtime.loader.soname, "ld-linux-x86-64.so.2"), "the loader's soname", 0);
        import_test_expect(&tally, import_test_exported(&runtime.loader, "__tls_get_addr", "GLIBC_2.3") && import_test_exported(&runtime.loader, "_r_debug", "GLIBC_2.2.5") &&
                                       import_test_exported(&runtime.loader, "__libc_stack_end", "GLIBC_2.2.5"),
                           "the loader's __tls_get_addr at GLIBC_2.3, _r_debug and __libc_stack_end at GLIBC_2.2.5", 0);
        uint16_t libc_largest = 0;
        uint16_t loader_smallest = UINT16_MAX;
        for (uint32_t v = 0; v < runtime.libc.version_count; v += 1)
        {
            libc_largest = runtime.libc.versions[v].index > libc_largest ? runtime.libc.versions[v].index : libc_largest;
        }
        for (uint32_t v = 0; v < runtime.loader.version_count; v += 1)
        {
            loader_smallest = runtime.loader.versions[v].index < loader_smallest ? runtime.loader.versions[v].index : loader_smallest;
        }
        import_test_expect(&tally, runtime.loader.version_count != 0 && loader_smallest > libc_largest, "the loader's version indices past libc.so.6's", loader_smallest);
        // libgcc_s.so.1's exports at their default versions, its version indices moved past the loader's, and none of
        // its names libc.so.6's or the loader's exports, so that its place among them on the line doesn't matter.
        const bool gcc_s = import_test_runtime_gcc_s(&tally, &runtime);
        // The archives in the driver's order, libgcc.a before libc.so's libc_nonshared.a, libc's (runtime_libc_archive), each
        // one's members one after another.
        const RuntimeArchive* libc_archive = runtime_libc_archive(&runtime);
        bool archived = runtime.archive_count == 2 && import_test_path_is(runtime.archives[0].path, "libgcc.a") && libc_archive == &runtime.archives[1] &&
                        import_test_path_is(libc_archive->path, "libc_nonshared.a");
        uint32_t archive_members = 0;
        for (uint32_t a = 0; a < runtime.archive_count; a += 1)
        {
            archived = archived && runtime.archives[a].first_member == archive_members && runtime.archives[a].archive.member_count != 0;
            archive_members += runtime.archives[a].archive.member_count;
        }
        import_test_expect(&tally, archived && archive_members == runtime.member_count, "libgcc.a, then libc_nonshared.a", runtime.archive_count);
        // libc_nonshared.a's index.
        bool atexit_indexed = false;
        for (uint32_t s = 0; archived && s < libc_archive->archive.symbol_count; s += 1)
        {
            atexit_indexed = atexit_indexed || import_equal(libc_archive->archive.symbols[s].name, "atexit");
        }
        import_test_expect(&tally, atexit_indexed, "libc_nonshared.a's index has atexit", archived ? libc_archive->archive.symbol_count : 0);
        import_test_expect(&tally, runtime.abi_note.length >= 16, "Scrt1.o's ABI note", runtime.abi_note.length);
        // The bindings, of every reference to each name.
        const char* const binding_names[] = {
            "main",
            "__libc_start_main",
            "__cxa_finalize",
            "__gmon_start__",
            "_ITM_deregisterTMCloneTable",
            "_ITM_registerTMCloneTable",
            "__TMC_END__",
            "_GLOBAL_OFFSET_TABLE_",
        };
        const RuntimeBindingKind binding_kinds[] = {
            RUNTIME_BINDING_PROGRAM,
            RUNTIME_BINDING_SHARED,
            RUNTIME_BINDING_SHARED,
            RUNTIME_BINDING_NOWHERE,
            RUNTIME_BINDING_NOWHERE,
            RUNTIME_BINDING_NOWHERE,
            RUNTIME_BINDING_RUNTIME,
            RUNTIME_BINDING_LINKER,
        };
        for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(binding_names); b += 1)
        {
            const uint64_t length = strlen(binding_names[b]);
            uint32_t references = 0;
            bool bound = true;
            for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
            {
                const NameId name = link->symbols.name[symbol];
                const bool reference = name != 0 && link->symbols.atom[symbol] == 0 && link->names.length[name] == length && memcmp(link->names.bytes[name], binding_names[b], length) == 0;
                const RuntimeBinding binding = runtime.bindings[symbol];
                references += reference ? 1 : 0;
                bound = bound && (!reference || (binding.kind == binding_kinds[b] && (binding_kinds[b] != RUNTIME_BINDING_RUNTIME || binding.symbol == tmc)));
            }
            import_test_expect(&tally, references != 0 && bound, binding_names[b], references);
        }
        // The objects' pieces: one after another from each table's first record to its last, each piece's atoms in the
        // object's unit, and its names' bytes one run.
        const uint32_t piece_count = RUNTIME_OBJECTS + runtime.member_count;
        bool pieces = true;
        for (uint32_t p = 0; p < piece_count; p += 1)
        {
            const ImportMark begin = runtime.pieces[p].begin;
            const ImportMark end = runtime.pieces[p].end;
            const ImportMark before = p == 0 ? (ImportMark){ .names = 1, .atoms = 1, .symbols = 1, .fixups = 1, .wide_addends = 1 } : runtime.pieces[p - 1].end;
            pieces = pieces && begin.names == before.names && begin.atoms == before.atoms && begin.symbols == before.symbols && begin.fixups == before.fixups &&
                     begin.wide_addends == before.wide_addends && (p == 0 || begin.name_bytes == before.name_bytes);
            for (AtomId atom = begin.atoms; atom < end.atoms; atom += 1)
            {
                pieces = pieces && link->atoms.unit[atom] == p;
            }
            uint64_t name_bytes = 0;
            for (NameId name = begin.names; name < end.names; name += 1)
            {
                pieces = pieces && link->names.bytes[name] == runtime.names.base + begin.name_bytes + name_bytes;
                name_bytes += (uint64_t)link->names.length[name] + 1;
            }
            pieces = pieces && begin.name_bytes + name_bytes == end.name_bytes;
        }
        const ImportMark last = runtime.pieces[piece_count - 1].end;
        pieces = pieces && last.names == link->names.count && last.atoms == link->atoms.count && last.symbols == link->symbols.count && last.fixups == link->fixups.count &&
                 last.wide_addends == link->fixups.wide_count;
        import_test_expect(&tally, pieces, "the objects' pieces, one after another", piece_count);
        // The global names binding looks a program's up in: each once, each found by its bytes, each global symbol's its
        // own, and what binding needs to know of a few.
        const RuntimeNames* global_names = &runtime.global_names;
        bool distinct = true;
        bool found_all = true;
        for (uint32_t e = 0; e < global_names->count; e += 1)
        {
            found_all = found_all && runtime_find_global_name(global_names, runtime_global_name(global_names, e), global_names->length[e], global_names->hash[e]) == e + 1 &&
                        global_names->hash[e] == link_hash(runtime_global_name(global_names, e), global_names->length[e]);
            for (uint32_t other = e + 1; distinct && other < global_names->count; other += 1)
            {
                distinct = global_names->length[other] != global_names->length[e] || memcmp(runtime_global_name(global_names, other), runtime_global_name(global_names, e), global_names->length[e]) != 0;
            }
        }
        import_test_expect(&tally, global_names->count >= runtime.libc.exports.count + runtime.libc.references.count, "a global name for each of libc's", global_names->count);
        // The filter: a word per four names at most, and each name's two bits set in its word.
        bool filtered = ((uint64_t)1 << global_names->filter_bits) * 4 >= global_names->count;
        for (uint32_t e = 0; filtered && e < global_names->count; e += 1)
        {
            const uint64_t bits = runtime_name_filter_bits(global_names->hash[e]);
            filtered = (global_names->filter[global_names->hash[e] >> (64 - global_names->filter_bits)] & bits) == bits;
        }
        import_test_expect(&tally, filtered, "each global name in the filter", global_names->filter_bits);
        import_test_expect(&tally, distinct, "each global name once", global_names->count);
        import_test_expect(&tally, found_all, "each global name found by its bytes", global_names->count);
        bool own = true;
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            const NameId name = link->symbols.name[symbol];
            const bool global = name != 0 && (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
            const uint32_t entry = runtime.symbol_names[symbol];
            own = own && (global ? entry != 0 && global_names->length[entry - 1] == link->names.length[name] &&
                                       memcmp(runtime_global_name(global_names, entry - 1), link->names.bytes[name], link->names.length[name]) == 0
                                 : entry == 0);
        }
        import_test_expect(&tally, own, "each global symbol's global name its own", 0);
        const uint32_t main_name = runtime_find_global_name(global_names, (const uint8_t*)"main", 4, link_hash((const uint8_t*)"main", 4));
        const uint32_t start_main = runtime_find_global_name(global_names, (const uint8_t*)"__libc_start_main", 17, link_hash((const uint8_t*)"__libc_start_main", 17));
        const uint32_t dso_name = runtime_find_global_name(global_names, (const uint8_t*)"__dso_handle", 12, link_hash((const uint8_t*)"__dso_handle", 12));
        const uint32_t atexit_name = runtime_find_global_name(global_names, (const uint8_t*)"atexit", 6, link_hash((const uint8_t*)"atexit", 6));
        import_test_expect(&tally, main_name != 0 && global_names->definition[main_name - 1] == 0 && global_names->export[main_name - 1] == 0, "main: defined by none of them", 0);
        uint32_t export_index = 0;
        const bool libc_export = start_main != 0 && global_names->export[start_main - 1] != 0 &&
                                 runtime_export_library(&runtime, global_names->export[start_main - 1] - 1, &export_index) == &runtime.libc &&
                                 import_test_name_is(runtime.libc.exports.name[export_index], runtime.libc.exports.length[export_index], "__libc_start_main");
        import_test_expect(&tally, start_main != 0 && global_names->definition[start_main - 1] == 0 && libc_export, "__libc_start_main: libc.so.6's", 0);
        // A name only the loader exports: its export, numbered after libc.so.6's.
        const uint32_t r_debug = runtime_find_global_name(global_names, (const uint8_t*)"_r_debug", 8, link_hash((const uint8_t*)"_r_debug", 8));
        const bool loader_export = r_debug != 0 && global_names->export[r_debug - 1] > runtime.libc.exports.count &&
                                   runtime_export_library(&runtime, global_names->export[r_debug - 1] - 1, &export_index) == &runtime.loader &&
                                   import_test_name_is(runtime.loader.exports.name[export_index], runtime.loader.exports.length[export_index], "_r_debug");
        import_test_expect(&tally, r_debug != 0 && global_names->definition[r_debug - 1] == 0 && loader_export, "_r_debug: the loader's", 0);
        import_test_expect(&tally, dso_name != 0 && global_names->definition[dso_name - 1] == dso, "__dso_handle: crtbeginS.o's", 0);
        import_test_expect(&tally, atexit_name != 0 && global_names->definition[atexit_name - 1] != 0 &&
                                       link->atoms.unit[link->symbols.atom[global_names->definition[atexit_name - 1]]] >= runtime.member_unit,
                           "atexit: a member's", 0);
        // __udivti3, which a compiler's code of a 128-bit integer's division calls: a member's of libgcc.a, and an
        // export of libgcc_s.so.1's, which binding takes only after the member (runtime_export_after_members). And
        // libgcc.a defines none of the names libc.so.6 and the loader export, so that binding a name to an export
        // before a member (bind.c) binds it as the driver's order, libgcc.a before libc.so, does.
        const uint32_t udivti3_name = runtime_find_global_name(global_names, (const uint8_t*)"__udivti3", 9, link_hash((const uint8_t*)"__udivti3", 9));
        const uint32_t libgcc_members = archived ? runtime.archives[0].archive.member_count : 0;
        const uint32_t udivti3_export = udivti3_name != 0 ? global_names->export[udivti3_name - 1] : 0;
        const bool udivti3_exported = gcc_s ? udivti3_export != 0 && runtime_export_after_members(&runtime, udivti3_export - 1) &&
                                                  runtime_export_library(&runtime, udivti3_export - 1, &export_index) == &runtime.gcc_s
                                            : udivti3_export == 0;
        import_test_expect(&tally, udivti3_name != 0 && global_names->member[udivti3_name - 1] != 0 && global_names->member[udivti3_name - 1] - 1 < libgcc_members &&
                                       udivti3_exported,
                           "__udivti3: a member's of libgcc.a, and libgcc_s.so.1's after it (no export without it)", 0);
        uint32_t libgcc_names = 0;
        bool apart = true;
        for (uint32_t e = 0; e < global_names->count; e += 1)
        {
            const bool libgcc = global_names->member[e] != 0 && global_names->member[e] - 1 < libgcc_members;
            libgcc_names += libgcc ? 1 : 0;
            apart = apart && (!libgcc || global_names->export[e] == 0 || runtime_export_after_members(&runtime, global_names->export[e] - 1));
        }
        import_test_expect(&tally, libgcc_names != 0 && apart, "libgcc.a's names none of libc.so.6's or the loader's exports", libgcc_names);
        // A name only libgcc_s.so.1 exports: its export, numbered after the loader's; no global name without it.
        const uint32_t backtrace = runtime_find_global_name(global_names, (const uint8_t*)"_Unwind_Backtrace", 17, link_hash((const uint8_t*)"_Unwind_Backtrace", 17));
        const bool gcc_s_export = backtrace != 0 && global_names->export[backtrace - 1] > runtime.libc.exports.count + runtime.loader.exports.count &&
                                  runtime_export_library(&runtime, global_names->export[backtrace - 1] - 1, &export_index) == &runtime.gcc_s &&
                                  import_test_name_is(runtime.gcc_s.exports.name[export_index], runtime.gcc_s.exports.length[export_index], "_Unwind_Backtrace");
        import_test_expect(&tally,
                           gcc_s ? backtrace != 0 && global_names->definition[backtrace - 1] == 0 && global_names->member[backtrace - 1] == 0 && gcc_s_export
                                 : backtrace == 0,
                           "_Unwind_Backtrace: libgcc_s.so.1's", 0);
        // Each name the linker defines, whether or not the runtime refers to it (only _GLOBAL_OFFSET_TABLE_ does), and no
        // other name.
        import_test_linker_names(&tally, &runtime);
        uint32_t referenced = 0;
        for (uint32_t e = 0; e < global_names->count; e += 1)
        {
            referenced += global_names->referenced[e] ? 1 : 0;
        }
        import_test_expect(&tally, referenced == runtime.libc.references.count, "libc.so.6's references among them", referenced);
        // Every probe is a path, and the crt objects' and libc's files are found among them.
        uint32_t found = 0;
        for (uint32_t p = 0; p < runtime.probe_count; p += 1)
        {
            found += runtime.probes[p].found ? 1 : 0;
        }
        import_test_expect(&tally, found >= RUNTIME_OBJECTS + 4, "the files found among the probes", found);
        // The vector paths read libc.so.6, each crt object and each member, as the exact paths do.
        import_test_library_paths(&tally, runtime.libc_bytes, IMPORT_TEST_VECTOR, "libc.so.6", arena);
        import_test_library_paths(&tally, runtime.loader_bytes, IMPORT_TEST_VECTOR, "the loader", arena);
        if (gcc_s)
        {
            import_test_library_paths(&tally, runtime.gcc_s_bytes, IMPORT_TEST_VECTOR, "libgcc_s.so.1", arena);
        }
        for (uint32_t a = 0; a < runtime.archive_count; a += 1)
        {
            import_test_archive_paths(&tally, runtime.archives[a].bytes, IMPORT_TEST_VECTOR, a == 0 ? "libgcc.a" : "libc_nonshared.a", arena);
        }
        for (uint32_t i = 0; i < RUNTIME_OBJECTS; i += 1)
        {
            import_test_paths(&tally, runtime.object_bytes[i], IMPORT_TEST_VECTOR, "a crt object", i, arena);
        }
        for (uint32_t m = 0; m < runtime.member_count; m += 1)
        {
            import_test_paths(&tally, runtime.members[m].bytes, IMPORT_TEST_VECTOR, "a member of libgcc.a or libc_nonshared.a", m, arena);
        }
        if (cross_check)
        {
            import_test_cross_check(&tally, &runtime, arena);
        }
    }
    runtime_release(&runtime);
    return import_test_report(&tally);
}

// The piece of the runtime's symbol `symbol`: the one whose range of symbols holds it.
static uint32_t import_test_symbol_piece(const Runtime* runtime, SymbolId symbol)
{
    const uint32_t piece_count = RUNTIME_OBJECTS + runtime->member_count;
    uint32_t low = 0;
    uint32_t high = piece_count;
    while (low + 1 < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool after = runtime->pieces[middle].begin.symbols <= symbol;
        low = after ? middle : low;
        high = after ? high : middle;
    }
    return low;
}

// Whether the path's last part, after its last '/', is `name`.
static bool import_test_path_is(String8 path, const char* name)
{
    const uint64_t length = strlen(name);
    return path.length > length && path.pointer[path.length - length - 1] == '/' && memcmp(path.pointer + path.length - length, name, length) == 0;
}

// The crt objects of each static kind's runtime (import_test_static_runtime), by RuntimeObject.
static const char* const import_test_static_objects[RUNTIME_KINDS][RUNTIME_OBJECTS] = {
    [RUNTIME_KIND_STATIC] = {
        [RUNTIME_OBJECT_START] = "crt1.o",
        [RUNTIME_OBJECT_INIT_PROLOGUE] = "crti.o",
        [RUNTIME_OBJECT_BEGIN] = "crtbeginT.o",
        [RUNTIME_OBJECT_END] = "crtend.o",
        [RUNTIME_OBJECT_INIT_EPILOGUE] = "crtn.o",
    },
    [RUNTIME_KIND_STATIC_PIE] = {
        [RUNTIME_OBJECT_START] = "rcrt1.o",
        [RUNTIME_OBJECT_INIT_PROLOGUE] = "crti.o",
        [RUNTIME_OBJECT_BEGIN] = "crtbeginS.o",
        [RUNTIME_OBJECT_END] = "crtendS.o",
        [RUNTIME_OBJECT_INIT_EPILOGUE] = "crtn.o",
    },
};

// The piece of the member of libc.a (the runtime's last archive) named `name`, or UINT32_MAX.
static uint32_t import_test_libc_member(const Runtime* runtime, const char* name)
{
    const RuntimeArchive* libc = &runtime->archives[runtime->archive_count - 1];
    uint32_t result = UINT32_MAX;
    for (uint32_t m = libc->first_member; result == UINT32_MAX && m < libc->first_member + libc->archive.member_count; m += 1)
    {
        result = import_equal(runtime->members[m].name, name) ? RUNTIME_OBJECTS + m : UINT32_MAX;
    }
    return result;
}

// A static kind's runtime (runtime_import_static, or for RUNTIME_KIND_STATIC_PIE runtime_import_static_pie, on this
// system's files): what each crt object is for; the archives of GCC's group, in its order, every member imported by
// both paths; the bindings of a few names, as the kind binds them; the names the kind's linker defines among the global
// names (import_test_linker_names); an IFUNC, an absolute symbol, the priorities and the COMDAT groups libc.a and
// libgcc.a bring; each piece's references and groups as a scan of its records finds them, each group's signature number
// equal exactly to those of the groups of the same signature; each global name's definition and member; and with
// --cross-check, every relocation against readelf's.
static bool import_test_static_runtime(RuntimeKind kind, bool cross_check, Arena* arena)
{
    const bool pie = kind == RUNTIME_KIND_STATIC_PIE;
    const char* const* objects = import_test_static_objects[kind];
    const char* const entry = pie ? "runtime_import_static_pie" : "runtime_import_static";
    ImportTestTally tally = {
        .check = pie ? "static PIE runtime" : "static runtime",
    };
    Runtime runtime;
    const RuntimeError error = pie ? runtime_import_static_pie(&runtime) : runtime_import_static(&runtime);
    import_test_expect(&tally, error == RUNTIME_ERROR_NONE && runtime.kind == kind, entry, (uint64_t)error);
    if (error != RUNTIME_ERROR_NONE)
    {
        printf("  %s failed: error %d, %s, import error %s at offset %llu (section %u, symbol %u, relocation %u, type %u), errno %d\n", entry, (int)error,
               runtime.failed_file, import_test_errors[runtime.import.error], (unsigned long long)runtime.import.offset, runtime.import.section, runtime.import.symbol,
               runtime.import.relocation, runtime.import.type, runtime.system_error);
    }
    else
    {
        const Link* link = &runtime.link;
        const uint32_t piece_count = RUNTIME_OBJECTS + runtime.member_count;
        printf("  GCC: %.*s; %u archives, %u members: %u atoms, %u symbols, %u fixups, %u priorities; %u global names, %u references, %u groups of %u signatures; %u probes\n",
               (int)runtime.gcc_directory.length, runtime.gcc_directory.pointer, runtime.archive_count, runtime.member_count, link->atoms.count - 1, link->symbols.count - 1,
               link->fixups.count - 1, link->priorities.count - 1, runtime.global_names.count, runtime.reference_begin[piece_count], runtime.group_begin[piece_count],
               runtime.group_signatures, runtime.probe_count);
        // What each crt object is for.
        const SymbolId start = import_test_runtime_symbol(&runtime, "_start", true);
        const SymbolId init = import_test_runtime_symbol(&runtime, "_init", true);
        const SymbolId dso = import_test_runtime_symbol(&runtime, "__dso_handle", true);
        const SymbolId tmc = import_test_runtime_symbol(&runtime, "__TMC_END__", true);
        char what[256];
        bool found_objects = true;
        for (uint32_t i = 0; i < RUNTIME_OBJECTS; i += 1)
        {
            found_objects = found_objects && import_test_path_is(runtime.object_paths[i], objects[i]);
        }
        snprintf(what, sizeof(what), "%s, %s, %s, %s and %s", objects[RUNTIME_OBJECT_START], objects[RUNTIME_OBJECT_INIT_PROLOGUE], objects[RUNTIME_OBJECT_BEGIN],
                 objects[RUNTIME_OBJECT_END], objects[RUNTIME_OBJECT_INIT_EPILOGUE]);
        import_test_expect(&tally, found_objects, what, 0);
        snprintf(what, sizeof(what), "_start in %s's .text", objects[RUNTIME_OBJECT_START]);
        import_test_expect(&tally, start != 0 && link->atoms.unit[link->symbols.atom[start]] == RUNTIME_OBJECT_START && link->atoms.class[link->symbols.atom[start]] == BUSTER_LINK_CLASS_TEXT,
                           what, 0);
        import_test_expect(&tally, init != 0 && link->atoms.unit[link->symbols.atom[init]] == RUNTIME_OBJECT_INIT_PROLOGUE && link->atoms.class[link->symbols.atom[init]] == BUSTER_LINK_CLASS_INIT,
                           "_init in crti.o's .init", 0);
        snprintf(what, sizeof(what), "__dso_handle in %s", objects[RUNTIME_OBJECT_BEGIN]);
        import_test_expect(&tally, dso != 0 && link->atoms.unit[link->symbols.atom[dso]] == RUNTIME_OBJECT_BEGIN, what, 0);
        snprintf(what, sizeof(what), "__TMC_END__ in %s", objects[RUNTIME_OBJECT_END]);
        import_test_expect(&tally, tmc != 0 && link->atoms.unit[link->symbols.atom[tmc]] == RUNTIME_OBJECT_END, what, 0);
        snprintf(what, sizeof(what), "%s's ABI note", objects[RUNTIME_OBJECT_START]);
        import_test_expect(&tally, runtime.abi_note.length >= 16, what, runtime.abi_note.length);
        // GCC's group, in its order, with each archive's members one after another.
        const uint32_t last = runtime.archive_count - 1;
        bool archived = runtime.archive_count >= 3 && import_test_path_is(runtime.archives[0].path, "libgcc.a") && import_test_path_is(runtime.archives[1].path, "libgcc_eh.a") &&
                        import_test_path_is(runtime.archives[last].path, "libc.a") && (runtime.archive_count == 3 || import_test_path_is(runtime.archives[2].path, "libatomic_asneeded.a"));
        uint32_t members = 0;
        for (uint32_t a = 0; a < runtime.archive_count; a += 1)
        {
            archived = archived && runtime.archives[a].first_member == members && runtime.archives[a].archive.member_count != 0;
            members += runtime.archives[a].archive.member_count;
        }
        import_test_expect(&tally, archived && members == runtime.member_count && runtime.member_unit == RUNTIME_OBJECTS, "libgcc.a, libgcc_eh.a, libatomic.a if GCC has it, and libc.a",
                           runtime.archive_count);
        // The bindings: a libc.a member's, a crt object's, the linker's, the program's, nothing, and no shared library's.
        // A static PIE's linker doesn't define the IRELATIVE relocations' bounds (its IFUNCs' relocations are in
        // .rela.dyn, which its startup applies with the others), so libc.a's weak references to them bind to nothing;
        // and rcrt1.o doesn't define _dl_relocate_static_pie, as crt1.o does (an empty one), so libc.a's member that
        // relocates the executable defines it, whose _DYNAMIC is the linker's. RUNTIME_BINDING_NONE: not checked (a
        // static executable's _DYNAMIC, which only that member refers to, and which never joins one).
        const char* const binding_names[] = {
            "main",
            "__libc_start_main",
            "__gmon_start__",
            "__TMC_END__",
            "_GLOBAL_OFFSET_TABLE_",
            "__rela_iplt_start",
            "__rela_iplt_end",
            "_dl_relocate_static_pie",
            "_DYNAMIC",
        };
        const RuntimeBindingKind binding_kinds[2][BUSTER_ARRAY_LENGTH(binding_names)] = {
            {
                RUNTIME_BINDING_PROGRAM,
                RUNTIME_BINDING_RUNTIME,
                RUNTIME_BINDING_NOWHERE,
                RUNTIME_BINDING_RUNTIME,
                RUNTIME_BINDING_LINKER,
                RUNTIME_BINDING_LINKER,
                RUNTIME_BINDING_LINKER,
                RUNTIME_BINDING_RUNTIME,
                RUNTIME_BINDING_NONE,
            },
            {
                RUNTIME_BINDING_PROGRAM,
                RUNTIME_BINDING_RUNTIME,
                RUNTIME_BINDING_NOWHERE,
                RUNTIME_BINDING_RUNTIME,
                RUNTIME_BINDING_LINKER,
                RUNTIME_BINDING_NOWHERE,
                RUNTIME_BINDING_NOWHERE,
                RUNTIME_BINDING_RUNTIME,
                RUNTIME_BINDING_LINKER,
            },
        };
        const uint32_t libc_first = runtime.archives[last].first_member;
        // The piece whose definition of _dl_relocate_static_pie the references bind to: crt1.o's, or a static PIE's
        // dl-reloc-static-pie.o, libc.a's.
        const uint32_t relocator = pie ? import_test_libc_member(&runtime, "dl-reloc-static-pie.o") : RUNTIME_OBJECT_START;
        for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(binding_names); b += 1)
        {
            const RuntimeBindingKind expected = binding_kinds[pie ? 1 : 0][b];
            const uint64_t length = strlen(binding_names[b]);
            uint32_t references = 0;
            bool bound = true;
            for (SymbolId symbol = 1; expected != RUNTIME_BINDING_NONE && symbol < link->symbols.count; symbol += 1)
            {
                const NameId name = link->symbols.name[symbol];
                const bool reference = name != 0 && link->symbols.atom[symbol] == 0 && link->symbols.source[symbol] == BUSTER_LINK_SOURCE_NONE &&
                                       link->names.length[name] == length && memcmp(link->names.bytes[name], binding_names[b], length) == 0;
                const RuntimeBinding binding = runtime.bindings[symbol];
                const uint32_t piece = binding.symbol != 0 ? import_test_symbol_piece(&runtime, binding.symbol) : 0;
                // __libc_start_main is a member of libc.a's, __TMC_END__ crtend.o's (or crtendS.o's), and
                // _dl_relocate_static_pie the relocator's.
                const bool where = b == 1 ? piece >= RUNTIME_OBJECTS + libc_first : b == 3 ? binding.symbol == tmc : b == 7 ? piece == relocator : true;
                references += reference ? 1 : 0;
                bound = bound && (!reference || (binding.kind == expected && where));
            }
            if (expected != RUNTIME_BINDING_NONE)
            {
                import_test_expect(&tally, references != 0 && bound, binding_names[b], references);
            }
        }
        bool shared = false;
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            shared = shared || runtime.bindings[symbol].kind == RUNTIME_BINDING_SHARED;
        }
        import_test_expect(&tally, !shared, "no name bound to a shared library", 0);
        import_test_linker_names(&tally, &runtime);
        // An IFUNC, an absolute symbol and the priorities.
        const SymbolId memcpy_symbol = import_test_runtime_symbol(&runtime, "memcpy", true);
        import_test_expect(&tally, memcpy_symbol != 0 && (link->symbols.flags[memcpy_symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_IFUNC &&
                                       import_test_symbol_piece(&runtime, memcpy_symbol) >= RUNTIME_OBJECTS + libc_first,
                           "memcpy: an IFUNC of libc.a's", 0);
        SymbolId used = 0;
        for (SymbolId symbol = 1; used == 0 && symbol < link->symbols.count; symbol += 1)
        {
            used = link->symbols.source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE && import_test_names(link, (String8){ 0 }, 0, symbol, "_nl_current_LC_CTYPE_used") ? symbol : 0;
        }
        import_test_expect(&tally, used != 0 && link->symbols.atom[used] == 0 && link->symbols.value[used] != 0, "_nl_current_LC_CTYPE_used: absolute, not 0", 0);
        bool prioritized = link->priorities.count >= 2;
        for (uint32_t e = 1; e < link->priorities.count; e += 1)
        {
            const AtomId atom = link->priorities.atom[e];
            const uint32_t unit = link->atoms.unit[atom];
            prioritized = prioritized && link_class_is_array(link->atoms.class[atom]) && unit >= runtime.member_unit + runtime.archives[0].first_member &&
                          unit < runtime.member_unit + runtime.archives[0].first_member + runtime.archives[0].archive.member_count;
        }
        import_test_expect(&tally, prioritized, "libgcc.a's prioritized init arrays", link->priorities.count - 1);
        // Each piece's groups, as its ImportObject has them, and their signatures' numbers.
        bool groups = runtime.group_begin[0] == 0 && runtime.group_begin[piece_count] != 0;
        uint32_t group = 0;
        uint32_t personality = UINT32_MAX;
        for (uint32_t p = 0; groups && p < piece_count; p += 1)
        {
            const ImportObject* object = p < RUNTIME_OBJECTS ? &runtime.objects[p] : &runtime.member_objects[p - RUNTIME_OBJECTS];
            groups = runtime.group_begin[p + 1] - runtime.group_begin[p] == object->group_count;
            for (uint32_t g = 0; groups && g < object->group_count; g += 1)
            {
                const RuntimeGroup* runtime_group = &runtime.groups[group + g];
                const ImportGroup* imported = &object->groups[g];
                uint32_t atoms = 0;
                for (uint32_t i = 0; i < imported->count; i += 1)
                {
                    const AtomId atom = object->section_atoms[object->group_sections[imported->first + i]];
                    groups = groups && (atom == 0 || runtime.group_atoms[runtime_group->first + atoms] == atom);
                    atoms += atom != 0 ? 1 : 0;
                }
                groups = groups && runtime_group->count == atoms && runtime_group->signature < runtime.group_signatures;
                const bool is_personality = imported->signature.length == strlen("DW.ref.__gcc_personality_v0") &&
                                            memcmp(imported->signature.pointer, "DW.ref.__gcc_personality_v0", imported->signature.length) == 0;
                groups = groups && (!is_personality || personality == UINT32_MAX || personality == runtime_group->signature);
                personality = is_personality ? runtime_group->signature : personality;
            }
            group += object->group_count;
        }
        // Two groups have the same number exactly when their signatures are the same.
        uint32_t pairs = 0;
        for (uint32_t p = 0; groups && p < piece_count; p += 1)
        {
            const ImportObject* object = p < RUNTIME_OBJECTS ? &runtime.objects[p] : &runtime.member_objects[p - RUNTIME_OBJECTS];
            for (uint32_t g = 0; groups && g < object->group_count; g += 1)
            {
                const uint32_t x = runtime.group_begin[p] + g;
                for (uint32_t q = 0; groups && q < piece_count; q += 1)
                {
                    const ImportObject* other = q < RUNTIME_OBJECTS ? &runtime.objects[q] : &runtime.member_objects[q - RUNTIME_OBJECTS];
                    for (uint32_t h = 0; groups && h < other->group_count; h += 1)
                    {
                        const uint32_t y = runtime.group_begin[q] + h;
                        const String8 a = object->groups[g].signature;
                        const String8 b = other->groups[h].signature;
                        const bool same = a.length == b.length && memcmp(a.pointer, b.pointer, a.length) == 0;
                        groups = same == (runtime.groups[x].signature == runtime.groups[y].signature);
                        pairs += 1;
                    }
                }
            }
        }
        import_test_expect(&tally, groups && personality != UINT32_MAX, "each piece's COMDAT groups, DW.ref.__gcc_personality_v0's of one signature", pairs);
        // Each piece's strong references, as a scan of its symbols finds them.
        bool references = runtime.reference_begin[0] == 0;
        for (uint32_t p = 0; references && p < piece_count; p += 1)
        {
            uint32_t at = runtime.reference_begin[p];
            for (SymbolId symbol = runtime.pieces[p].begin.symbols; references && symbol < runtime.pieces[p].end.symbols; symbol += 1)
            {
                const bool strong = link->symbols.atom[symbol] == 0 && link->symbols.source[symbol] == BUSTER_LINK_SOURCE_NONE &&
                                    (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL && runtime.symbol_names[symbol] != 0;
                references = !strong || (at < runtime.reference_begin[p + 1] && runtime.references[at] == runtime.symbol_names[symbol] - 1);
                at += strong ? 1 : 0;
            }
            references = references && at == runtime.reference_begin[p + 1];
        }
        import_test_expect(&tally, references, "each piece's strong references to global names", runtime.reference_begin[piece_count]);
        // The member each reference loads: the member whose piece holds its name's first definition, unless the linker
        // defines the name; and none for a name the crt objects or no one defines.
        bool loads = true;
        for (uint32_t r = 0; loads && r < runtime.reference_begin[piece_count]; r += 1)
        {
            const uint32_t entry = runtime.references[r];
            const SymbolId definition = runtime.global_names.definition[entry];
            const uint32_t piece = definition != 0 ? import_test_symbol_piece(&runtime, definition) : 0;
            const uint32_t member = definition != 0 && piece >= RUNTIME_OBJECTS && !runtime.global_names.linker[entry] ? piece - RUNTIME_OBJECTS + 1 : 0;
            loads = runtime.reference_members[r] == member;
        }
        import_test_expect(&tally, loads, "the member each reference loads", 0);
        // Each global name found by its bytes, with the first symbol that defines it, in an atom or absolute, and the
        // member that symbol is in.
        const RuntimeNames* names = &runtime.global_names;
        const uint64_t length = arena->length;
        SymbolId* first = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, names->count);
        bool defined = first != 0;
        if (first != 0)
        {
            memset(first, 0, names->count * sizeof(SymbolId));
        }
        for (SymbolId symbol = 1; defined && symbol < link->symbols.count; symbol += 1)
        {
            const uint32_t entry = runtime.symbol_names[symbol];
            const bool definition = entry != 0 && (link->symbols.atom[symbol] != 0 || link->symbols.source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE);
            first[entry - (entry != 0 ? 1 : 0)] = definition && first[entry - 1] == 0 ? symbol : first[entry - (entry != 0 ? 1 : 0)];
        }
        for (uint32_t e = 0; defined && e < names->count; e += 1)
        {
            const uint32_t piece = first[e] != 0 ? import_test_symbol_piece(&runtime, first[e]) : 0;
            const uint32_t member = first[e] != 0 && piece >= RUNTIME_OBJECTS ? piece - RUNTIME_OBJECTS + 1 : 0;
            defined = names->definition[e] == first[e] && names->member[e] == member &&
                      runtime_find_global_name(names, runtime_global_name(names, e), names->length[e], names->hash[e]) == e + 1;
        }
        arena_pop_to(arena, length);
        import_test_expect(&tally, defined, "each global name found, with its first definition and that definition's member", names->count);
        import_test_expect(&tally, link_check(&runtime.link) == LINK_ERROR_NONE, "link_check of the runtime's link", 0);
        // Every crt object and member by both paths, which must take each.
        for (uint32_t i = 0; i < RUNTIME_OBJECTS; i += 1)
        {
            import_test_paths(&tally, runtime.object_bytes[i], IMPORT_TEST_VECTOR, "a static crt object", i, arena);
        }
        for (uint32_t m = 0; m < runtime.member_count; m += 1)
        {
            import_test_paths(&tally, runtime.members[m].bytes, IMPORT_TEST_VECTOR, "a member of the static runtime's archives", m, arena);
        }
        if (cross_check)
        {
            const uint64_t before = arena->length;
            import_test_cross_check_relocations(&tally, &runtime, arena);
            arena_pop_to(arena, before);
        }
    }
    runtime_release(&runtime);
    return import_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The cross-check

#define BUSTER_IMPORT_TEST_READ_BYTES (64ull << 10)
#define BUSTER_IMPORT_TEST_COMMAND_BYTES 8192

// A shell command's output, pushed from `arena` with a NUL after it; empty, with a null pointer, if it couldn't run or
// exited with a status other than 0.
static String8 import_test_run(const char* command, Arena* arena)
{
    String8 result = { 0 };
    FILE* pipe = popen(command, "r");
    char* start = 0;
    uint64_t length = 0;
    bool reading = pipe != 0;
    while (reading)
    {
        char* chunk = arena_push(arena, BUSTER_IMPORT_TEST_READ_BYTES, 1);
        start = start == 0 ? chunk : start;
        const uint64_t count = chunk != 0 ? fread(chunk, 1, BUSTER_IMPORT_TEST_READ_BYTES, pipe) : 0;
        length += count;
        reading = chunk != 0 && count == BUSTER_IMPORT_TEST_READ_BYTES;
    }
    const int status = pipe != 0 ? pclose(pipe) : -1;
    char* end = arena_push(arena, 1, 1);
    if (status == 0 && start != 0 && end != 0)
    {
        start[length] = 0;
        result.pointer = start;
        result.length = length;
    }
    return result;
}

// The next line of `text` after *position, NUL-terminated in place of its '\n'.
static bool import_test_next_line(String8 text, uint64_t* position, char** line)
{
    const bool result = *position < text.length;
    if (result)
    {
        char* start = text.pointer + *position;
        const char* end = memchr(start, '\n', text.length - *position);
        const uint64_t length = end != 0 ? (uint64_t)(end - start) : text.length - *position;
        start[length] = 0;
        *position += length + 1;
        *line = start;
    }
    return result;
}

// The flags of readelf's type and binding names, as import_library gives an export's: IFUNC as a function, COMMON as an
// object, UNIQUE as global; BUSTER_IMPORT_TEST_NONE for names it doesn't know.
static uint8_t import_test_readelf_flags(const char* type, const char* binding)
{
    const bool function = strcmp(type, "FUNC") == 0 || strcmp(type, "IFUNC") == 0;
    const bool object = strcmp(type, "OBJECT") == 0 || strcmp(type, "COMMON") == 0;
    const bool tls = strcmp(type, "TLS") == 0;
    const bool notype = strcmp(type, "NOTYPE") == 0;
    const bool global = strcmp(binding, "GLOBAL") == 0 || strcmp(binding, "UNIQUE") == 0;
    const bool weak = strcmp(binding, "WEAK") == 0;
    const uint8_t symbol_type = function ? BUSTER_LINK_SYMBOL_FUNCTION : object ? BUSTER_LINK_SYMBOL_OBJECT : tls ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_NOTYPE;
    const bool known = (function || object || tls || notype) && (global || weak);
    return known ? (uint8_t)(symbol_type | (weak ? BUSTER_LINK_SYMBOL_WEAK : BUSTER_LINK_SYMBOL_GLOBAL)) : BUSTER_IMPORT_TEST_NONE;
}

// A library's exports and references (libc.so.6's, or the loader's, `what`) against `readelf -W --dyn-syms` of its
// file: a defined name at its default version (name@@V, or a bare name) that is global and not hidden is an export, and
// an undefined global name a reference.
static void import_test_cross_check_library(ImportTestTally* tally, const ImportLibrary* library, String8 path, const char* what, Arena* arena)
{
    char command[BUSTER_IMPORT_TEST_COMMAND_BYTES];
    snprintf(command, sizeof(command), "readelf -W --dyn-syms '%.*s'", (int)path.length, path.pointer);
    const String8 output = import_test_run(command, arena);
    import_test_expect(tally, output.pointer != 0, "readelf --dyn-syms runs", 0);
    uint32_t exports = 0;
    uint32_t references = 0;
    bool exports_match = true;
    bool references_match = true;
    uint64_t position = 0;
    char* line = 0;
    while (output.pointer != 0 && import_test_next_line(output, &position, &line))
    {
        unsigned number = 0;
        char value[32];
        char size_text[32];
        char type[16];
        char binding[16];
        char visibility[16];
        char index[16];
        char name[1024];
        binding[0] = 0;
        name[0] = 0;
        const int fields = sscanf(line, " %u: %31s %31s %15s %15s %15s %15s %1023s", &number, value, size_text, type, binding, visibility, index, name);
        char* at = fields == 8 ? strchr(name, '@') : 0;
        const bool hidden_version = at != 0 && at[1] != '@';
        const char* version = at != 0 ? at + (hidden_version ? 1 : 2) : "";
        if (at != 0)
        {
            *at = 0;
        }
        const bool global = fields == 8 && (strcmp(binding, "GLOBAL") == 0 || strcmp(binding, "WEAK") == 0 || strcmp(binding, "UNIQUE") == 0);
        const bool undefined = strcmp(index, "UND") == 0;
        const bool visible = strcmp(visibility, "DEFAULT") == 0 || strcmp(visibility, "PROTECTED") == 0;
        if (global && !undefined && visible && !hidden_version)
        {
            const uint32_t found = import_test_library_find(library, name);
            const ImportExports* e = &library->exports;
            const bool exported = found < e->count;
            const bool same_version = version[0] == 0 || (exported && import_equal(import_test_version_name(library, e->version[found]), version));
            const bool same = exported && e->size[found] == strtoull(size_text, 0, 0) && e->flags[found] == import_test_readelf_flags(type, binding) && same_version;
            if (!same && exports_match)
            {
                printf("  runtime: %s's export %s@@%s differs from readelf's\n", what, name, version);
            }
            exports_match = exports_match && same;
            exports += 1;
        }
        else if (global && undefined)
        {
            bool same = false;
            const ImportReferences* r = &library->references;
            for (uint32_t i = 0; !same && i < r->count; i += 1)
            {
                same = import_test_name_is(r->name[i], r->length[i], name) && r->weak[i] == (strcmp(binding, "WEAK") == 0 ? 1 : 0);
            }
            references_match = references_match && same;
            references += 1;
        }
    }
    import_test_expect(tally, exports_match && exports == library->exports.count, "a library's exports, as readelf lists them", exports);
    import_test_expect(tally, references_match && references == library->references.count, "a library's references, as readelf lists them", references);
    printf("  readelf: %u exports and %u references of %s\n", exports, references, what);
}

// The fixup kind of a relocation type's name, or BUSTER_LINK_FIXUP_NONE.
static uint8_t import_test_kind_named(const char* type)
{
    uint8_t result = BUSTER_LINK_FIXUP_NONE;
    result = strcmp(type, "R_X86_64_PLT32") == 0 ? BUSTER_LINK_FIXUP_CALL : result;
    result = strcmp(type, "R_X86_64_GOTPCRELX") == 0 ? BUSTER_LINK_FIXUP_CALL_GOT : result;
    result = strcmp(type, "R_X86_64_REX_GOTPCRELX") == 0 ? BUSTER_LINK_FIXUP_LOAD_GOT : result;
    result = strcmp(type, "R_X86_64_GOTPCREL") == 0 ? BUSTER_LINK_FIXUP_GOTPCREL : result;
    result = strcmp(type, "R_X86_64_PC32") == 0 ? BUSTER_LINK_FIXUP_PC32 : result;
    result = strcmp(type, "R_X86_64_TPOFF32") == 0 ? BUSTER_LINK_FIXUP_TPOFF32 : result;
    result = strcmp(type, "R_X86_64_GOTTPOFF") == 0 ? BUSTER_LINK_FIXUP_GOTTPOFF : result;
    result = strcmp(type, "R_X86_64_64") == 0 ? BUSTER_LINK_FIXUP_ABS64 : result;
    result = strcmp(type, "R_X86_64_PC64") == 0 ? BUSTER_LINK_FIXUP_PC64 : result;
    result = strcmp(type, "R_X86_64_32") == 0 ? BUSTER_LINK_FIXUP_ABS32 : result;
    result = strcmp(type, "R_X86_64_32S") == 0 ? BUSTER_LINK_FIXUP_ABS32S : result;
    result = strcmp(type, "R_X86_64_SIZE32") == 0 ? BUSTER_LINK_FIXUP_SIZE32 : result;
    result = strcmp(type, "R_X86_64_SIZE64") == 0 ? BUSTER_LINK_FIXUP_SIZE64 : result;
    result = strcmp(type, "R_X86_64_TLSGD") == 0 ? BUSTER_LINK_FIXUP_TLSGD : result;
    result = strcmp(type, "R_X86_64_TLSLD") == 0 ? BUSTER_LINK_FIXUP_TLSLD : result;
    result = strcmp(type, "R_X86_64_DTPOFF32") == 0 ? BUSTER_LINK_FIXUP_DTPOFF32 : result;
    return result;
}

// The section of an object's bytes named `name`, with its header, or 0.
static uint32_t import_test_section_named(String8 bytes, const char* name, Elf64_Shdr* header)
{
    ImportElf elf;
    uint32_t result = 0;
    const bool valid = import_elf(&elf, bytes, ET_REL).error == IMPORT_ERROR_NONE;
    for (uint32_t index = 1; valid && result == 0 && index < elf.section_count; index += 1)
    {
        const Elf64_Shdr candidate = import_section_header(&elf, index);
        String8 section_name = { 0 };
        result = import_section_name(&elf, &candidate, &section_name) && import_equal(section_name, name) ? index : 0;
        *header = result != 0 ? candidate : *header;
    }
    return result;
}

// Whether the imported symbol is the one readelf names: by its name, or by its atom's section's for a section's symbol
// (an unnamed local at its atom's start).
static bool import_test_names(const Link* link, String8 bytes, const ImportObject* object, SymbolId symbol, const char* name)
{
    const NameId id = link->symbols.name[symbol];
    bool result = id != 0 && link->names.length[id] == strlen(name) && memcmp(link->names.bytes[id], name, link->names.length[id]) == 0;
    if (id == 0 && link->symbols.value[symbol] == 0)
    {
        Elf64_Shdr header;
        const uint32_t section = import_test_section_named(bytes, name, &header);
        result = section != 0 && section < object->section_count && object->section_atoms[section] == link->symbols.atom[symbol];
    }
    return result;
}

// What the relocations' cross-check is reading: the object (a crt object or an archive member), the atom of the
// relocation section it is in, and the tallies, the GOTPCRELX and REX_GOTPCRELX relocations whose instructions make
// them GOTPCREL fixups among them.
typedef struct ImportTestRelocationCheck ImportTestRelocationCheck;
struct ImportTestRelocationCheck
{
    const Link* link;
    String8 bytes;
    const ImportObject* object;
    AtomId atom;
    uint64_t compared;
    uint64_t dropped;
    uint64_t read_through;
    bool same;
};

// The kind of the fixup of a GOTPCRELX or REX_GOTPCRELX relocation (`named`, CALL_GOT or LOAD_GOT) at `offset` in an
// atom's bytes, by the instruction before its field, as the psABI says a linker may rewrite them: the call or the jmp
// through the slot (ff 15 or ff 25 before it), or the mov of the slot into a 64-bit register (a REX prefix with W, 8b
// and a ModRM with mod 0 and r/m 5 before it), each with the addend -4, and GOTPCREL for any other.
static uint8_t import_test_got_kind(uint8_t named, const uint8_t* bytes, uint64_t offset, int64_t addend)
{
    uint8_t result = named;
    if (named == BUSTER_LINK_FIXUP_CALL_GOT || named == BUSTER_LINK_FIXUP_LOAD_GOT)
    {
        const uint8_t opcode = offset >= 2 ? bytes[offset - 2] : 0;
        const uint8_t modrm = offset >= 2 ? bytes[offset - 1] : 0;
        const uint8_t rex = offset >= 3 ? bytes[offset - 3] : 0;
        const bool through = opcode == 0xff && (modrm == 0x15 || modrm == 0x25);
        const bool load = rex >= 0x48 && rex <= 0x4f && opcode == 0x8b && (modrm >> 6) == 0 && (modrm & 7) == 5;
        const bool rewritable = addend == -4 && (named == BUSTER_LINK_FIXUP_CALL_GOT ? through : load);
        result = rewritable ? named : BUSTER_LINK_FIXUP_GOTPCREL;
    }
    return result;
}

// One line of `readelf -W -r`: a relocation section's header, which picks the atom, or a relocation, which must be
// that atom's fixup at its offset.
static void import_test_relocation_line(ImportTestRelocationCheck* check, char* line)
{
    static const char header_prefix[] = "Relocation section '";
    const char* header = strstr(line, header_prefix);
    if (header != 0 && check->object != 0)
    {
        char name[256];
        const char* start = header + BUSTER_COMPILE_TIME_STRING_LENGTH(header_prefix);
        const char* end = strchr(start, '\'');
        const uint64_t length = end != 0 && (uint64_t)(end - start) < sizeof(name) ? (uint64_t)(end - start) : 0;
        memcpy(name, start, length);
        name[length] = 0;
        Elf64_Shdr section = { 0 };
        const uint32_t index = import_test_section_named(check->bytes, name, &section);
        check->atom = index != 0 && section.sh_info < check->object->section_count ? check->object->section_atoms[section.sh_info] : 0;
        check->same = check->same && index != 0;
    }
    else if (check->object != 0)
    {
        unsigned long long offset = 0;
        unsigned long long info = 0;
        unsigned long long value = 0;
        unsigned long long addend = 0;
        char type[64];
        char symbol[512];
        char sign = '+';
        type[0] = 0;
        const int fields = sscanf(line, "%llx %llx %63s %llx %511s %c %llx", &offset, &info, type, &value, symbol, &sign, &addend);
        const uint8_t kind = fields >= 3 ? import_test_kind_named(type) : BUSTER_LINK_FIXUP_NONE;
        const bool relocation = fields >= 4 && strncmp(type, "R_X86_64_", 9) == 0;
        if (relocation && check->atom == 0)
        {
            check->dropped += 1;
        }
        else if (relocation && strcmp(type, "R_X86_64_NONE") != 0)
        {
            const Link* link = check->link;
            const FixupId fixup = import_test_fixup_at(link, check->atom, offset);
            const int64_t want = sign == '-' ? -(int64_t)addend : (int64_t)addend;
            const uint8_t expected = import_test_got_kind(kind, link->atoms.bytes[check->atom], offset, want);
            check->read_through += expected != kind ? 1 : 0;
            const bool same = fields == 7 && fixup != 0 && (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) == expected && link_fixup_addend(link, fixup) == want &&
                              import_test_names(link, check->bytes, check->object, link->fixups.target[fixup], symbol);
            if (!same && check->same)
            {
                printf("  runtime: the relocation at 0x%llx (%s against %s) differs from its fixup\n", offset, type, fields == 7 ? symbol : "nothing");
            }
            check->same = check->same && same;
            check->compared += 1;
        }
    }
}

// The crt objects' and the archives' members' relocations against `readelf -W -r`, and no other fixups.
static void import_test_cross_check_relocations(ImportTestTally* tally, const Runtime* runtime, Arena* arena)
{
    const Link* link = &runtime->link;
    char command[BUSTER_IMPORT_TEST_COMMAND_BYTES];
    ImportTestRelocationCheck check = {
        .link = link,
        .same = true,
    };
    for (uint32_t i = 0; i < RUNTIME_OBJECTS; i += 1)
    {
        snprintf(command, sizeof(command), "readelf -W -r '%.*s'", (int)runtime->object_paths[i].length, runtime->object_paths[i].pointer);
        const uint64_t length = arena->length;
        const String8 output = import_test_run(command, arena);
        check.bytes = runtime->object_bytes[i];
        check.object = &runtime->objects[i];
        check.atom = 0;
        uint64_t position = 0;
        char* line = 0;
        while (output.pointer != 0 && import_test_next_line(output, &position, &line))
        {
            import_test_relocation_line(&check, line);
        }
        import_test_expect(tally, output.pointer != 0, "readelf -r runs", i);
        arena_pop_to(arena, length);
    }
    // Each archive's members, in its order, each after a "File: archive(member)" line with its name.
    for (uint32_t a = 0; a < runtime->archive_count; a += 1)
    {
        const RuntimeArchive* archive = &runtime->archives[a];
        snprintf(command, sizeof(command), "readelf -W -r '%.*s'", (int)archive->path.length, archive->path.pointer);
        const uint64_t length = arena->length;
        const String8 output = import_test_run(command, arena);
        uint64_t position = 0;
        char* line = 0;
        uint32_t next = 0;
        check.object = 0;
        while (output.pointer != 0 && import_test_next_line(output, &position, &line))
        {
            const char* open = strncmp(line, "File: ", 6) == 0 ? strrchr(line, '(') : 0;
            const char* close = open != 0 ? strrchr(open, ')') : 0;
            if (close != 0)
            {
                const uint64_t name_length = (uint64_t)(close - open - 1);
                const uint32_t m = archive->first_member + next;
                const bool named = next < archive->archive.member_count && runtime->members[m].name.length == name_length &&
                                   memcmp(runtime->members[m].name.pointer, open + 1, name_length) == 0;
                check.object = named ? &runtime->member_objects[m] : 0;
                check.bytes = named ? runtime->members[m].bytes : (String8){ 0 };
                check.atom = 0;
                check.same = check.same && named;
                next += 1;
            }
            else
            {
                import_test_relocation_line(&check, line);
            }
        }
        import_test_expect(tally, output.pointer != 0 && next == archive->archive.member_count, "readelf -r runs on each archive, and lists each member", a);
        arena_pop_to(arena, length);
    }
    uint64_t fixups = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        fixups += link->atoms.fixup_count[atom];
    }
    import_test_expect(tally, check.same && fixups == check.compared, "every relocation of the crt objects and the archives' members, as readelf lists them", check.compared);
    printf("  readelf: %llu relocations of the crt objects and the archives' %u members (%llu GOTPCRELX and REX_GOTPCRELX ones on other instructions, "
           "GOTPCREL), and %llu in sections the import drops\n",
           (unsigned long long)check.compared, runtime->member_count, (unsigned long long)check.read_through, (unsigned long long)check.dropped);
}

static void import_test_cross_check(ImportTestTally* tally, const Runtime* runtime, Arena* arena)
{
    const uint64_t length = arena->length;
    import_test_cross_check_library(tally, &runtime->libc, runtime->libc_path, "libc.so.6", arena);
    import_test_cross_check_library(tally, &runtime->loader, runtime->loader_path, "the loader", arena);
    if (runtime->gcc_s_path.length != 0)
    {
        import_test_cross_check_library(tally, &runtime->gcc_s, runtime->gcc_s_path, "libgcc_s.so.1", arena);
    }
    import_test_cross_check_relocations(tally, runtime, arena);
    arena_pop_to(arena, length);
}

// ---------------------------------------------------------------------------------------------------------------------
// run_import_tests

// The page edges' inputs: the files made by hand again, and the linker scripts.
static bool import_test_edges(uint64_t seed, Arena* arena)
{
    const uint64_t position = arena->length;
    ImportTestObject* object = BUSTER_ARENA_PUSH_ARRAY(arena, ImportTestObject, 1);
    ImportTestLibrary* library = BUSTER_ARENA_PUSH_ARRAY(arena, ImportTestLibrary, 1);
    ImportTestFile archive32;
    ImportTestFile archive64;
    static const uint8_t member[5] = { 1, 2, 3, 4, 5 };
    ImportTestArchive archive = {
        .names = {
            "first.o",
            "a_long_member_name.o",
        },
        .bytes = {
            member,
            member,
        },
        .sizes = {
            sizeof(member),
            2,
        },
        .member_count = 2,
        .symbols = {
            "alpha",
            "beta",
        },
        .symbol_members = {
            0,
            1,
        },
        .symbol_count = 2,
        .width = 4,
    };
    bool result = object != 0 && library != 0 && import_test_object(object, arena) && import_test_library(library, arena);
    result = result && import_test_archive(&archive32, &archive, arena);
    archive.width = 8;
    result = result && import_test_archive(&archive64, &archive, arena);
    // In offset order, its relocations take the vector path.
    const ImportTestFile sorted = result ? import_test_sorted(object, &object->file, arena) : (ImportTestFile){ 0 };
    result = result && sorted.bytes != 0;
    if (result)
    {
        const ImportTestInput inputs[BUSTER_IMPORT_TEST_INPUTS] = {
            {
                .name = "the object made by hand",
                .bytes = object->file.bytes,
                .size = object->file.size,
            },
            {
                .name = "the object made by hand, its relocations in order",
                .bytes = sorted.bytes,
                .size = sorted.size,
            },
            {
                .name = "the shared library made by hand",
                .bytes = library->file.bytes,
                .size = library->file.size,
            },
            {
                .name = "the archive with a 32-bit index",
                .bytes = archive32.bytes,
                .size = archive32.size,
            },
            {
                .name = "the archive with a 64-bit index",
                .bytes = archive64.bytes,
                .size = archive64.size,
            },
            {
                .name = "libc.so's script",
                .bytes = (const uint8_t*)import_test_libc_script,
                .size = BUSTER_COMPILE_TIME_STRING_LENGTH(import_test_libc_script),
            },
            {
                .name = "an _asneeded script",
                .bytes = (const uint8_t*)import_test_asneeded_script,
                .size = BUSTER_COMPILE_TIME_STRING_LENGTH(import_test_asneeded_script),
            },
            {
                .name = "a script with commas",
                .bytes = (const uint8_t*)import_test_commas_script,
                .size = BUSTER_COMPILE_TIME_STRING_LENGTH(import_test_commas_script),
            },
        };
        result = import_test_page_edges(inputs, BUSTER_IMPORT_TEST_INPUTS, seed, arena);
    }
    else
    {
        printf("page edges: the inputs couldn't be made\n");
    }
    arena_pop_to(arena, position);
    return result;
}

// A shared library's runtime (runtime_import_shared): no start object, its unit's piece empty and no ABI note; crti.o,
// crtbeginS.o, crtendS.o and crtn.o; libgcc.a, libgcc_s.so.1, libc.so.6, the loader and libc_nonshared.a; nothing
// refers to main, which is no global name; morestack.o's _Unwind_Resume bound to libgcc_s.so.1; crti.o's _init and
// crtbeginS.o's __dso_handle; the names the kind's linker defines (_DYNAMIC among them) and no other; and every crt
// object and member taking the vector path.
static bool import_test_shared_runtime(Arena* arena)
{
    ImportTestTally tally = {
        .check = "shared runtime",
    };
    Runtime runtime;
    const RuntimeError error = runtime_import_shared(&runtime);
    import_test_expect(&tally, error == RUNTIME_ERROR_NONE, "runtime_import_shared", (uint64_t)error);
    if (error == RUNTIME_ERROR_NONE)
    {
        const RuntimePiece* start = &runtime.pieces[RUNTIME_OBJECT_START];
        const bool empty = runtime.object_paths[RUNTIME_OBJECT_START].length == 0 && start->begin.atoms == start->end.atoms &&
                           start->begin.symbols == start->end.symbols && start->begin.fixups == start->end.fixups && runtime.abi_note.length == 0;
        import_test_expect(&tally, empty, "no start object: an empty piece, and no ABI note", 0);
        static const char* const objects[RUNTIME_OBJECTS] = {
            [RUNTIME_OBJECT_INIT_PROLOGUE] = "crti.o",
            [RUNTIME_OBJECT_BEGIN] = "crtbeginS.o",
            [RUNTIME_OBJECT_END] = "crtendS.o",
            [RUNTIME_OBJECT_INIT_EPILOGUE] = "crtn.o",
        };
        for (uint32_t i = RUNTIME_OBJECT_INIT_PROLOGUE; i < RUNTIME_OBJECTS; i += 1)
        {
            const String8 path = runtime.object_paths[i];
            const uint64_t length = strlen(objects[i]);
            const bool found = path.length > length && memcmp(path.pointer + path.length - length, objects[i], length) == 0 && path.pointer[path.length - length - 1] == '/';
            import_test_expect(&tally, found, objects[i], i);
        }
        import_test_expect(&tally, import_equal(runtime.libc.soname, "libc.so.6") && import_equal(runtime.loader.soname, "ld-linux-x86-64.so.2") && runtime.archive_count == 2 &&
                                       import_test_path_is(runtime.archives[0].path, "libgcc.a") && import_test_path_is(runtime_libc_archive(&runtime)->path, "libc_nonshared.a"),
                           "libgcc.a, libc.so.6, the loader and libc_nonshared.a", runtime.archive_count);
        const bool gcc_s = import_test_runtime_gcc_s(&tally, &runtime);
        const RuntimeNames* names = &runtime.global_names;
        const Link* link = &runtime.link;
        const uint32_t main_entry = runtime_find_global_name(names, (const uint8_t*)"main", 4, link_hash((const uint8_t*)"main", 4));
        // And only libgcc.a's members refer to names nothing in the runtime defines, which bind to the program; the
        // split-stack members' unwinder binds to libgcc_s.so.1.
        const bool libgcc = runtime.archive_count == 2 && runtime.archives[0].archive.member_count != 0;
        const RuntimePiece* first_member = libgcc ? &runtime.pieces[RUNTIME_OBJECTS + runtime.archives[0].first_member] : 0;
        const RuntimePiece* last_member = libgcc ? first_member + runtime.archives[0].archive.member_count - 1 : 0;
        bool program = !libgcc;
        for (SymbolId symbol = 1; libgcc && symbol < link->symbols.count; symbol += 1)
        {
            const bool member = symbol >= first_member->begin.symbols && symbol < last_member->end.symbols;
            program = program || (runtime.bindings[symbol].kind == RUNTIME_BINDING_PROGRAM && !member);
        }
        import_test_expect(&tally, main_entry == 0 && !program, "nothing refers to main, and only libgcc.a's members to names nothing defines", main_entry);
        bool unwinder = false;
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            const NameId name = link->symbols.name[symbol];
            const RuntimeBinding binding = runtime.bindings[symbol];
            uint32_t index = 0;
            // libgcc_s.so.1's, or without it the program's to define.
            const bool bound = gcc_s ? binding.kind == RUNTIME_BINDING_SHARED && runtime_export_library(&runtime, binding.export, &index) == &runtime.gcc_s
                                     : binding.kind == RUNTIME_BINDING_PROGRAM;
            unwinder = unwinder || (link->symbols.atom[symbol] == 0 && name != 0 && import_test_name_is(runtime_name_bytes(&runtime, name), link->names.length[name], "_Unwind_Resume") &&
                                    bound);
        }
        import_test_expect(&tally, unwinder, "morestack.o's _Unwind_Resume bound to libgcc_s.so.1 (to the program without it)", 0);
        const uint32_t init = runtime_find_global_name(names, (const uint8_t*)"_init", 5, link_hash((const uint8_t*)"_init", 5));
        const uint32_t dso = runtime_find_global_name(names, (const uint8_t*)"__dso_handle", 12, link_hash((const uint8_t*)"__dso_handle", 12));
        const SymbolId init_symbol = init != 0 ? names->definition[init - 1] : 0;
        const SymbolId dso_symbol = dso != 0 ? names->definition[dso - 1] : 0;
        import_test_expect(&tally, init_symbol != 0 && link->atoms.unit[link->symbols.atom[init_symbol]] == RUNTIME_OBJECT_INIT_PROLOGUE, "_init: crti.o's", 0);
        import_test_expect(&tally, dso_symbol != 0 && link->atoms.unit[link->symbols.atom[dso_symbol]] == RUNTIME_OBJECT_BEGIN, "__dso_handle: crtbeginS.o's", 0);
        import_test_linker_names(&tally, &runtime);
        // The vector paths read each crt object and each member, as the exact paths do.
        for (uint32_t i = 0; i < RUNTIME_OBJECTS; i += 1)
        {
            if (runtime.object_paths[i].length != 0)
            {
                import_test_paths(&tally, runtime.object_bytes[i], IMPORT_TEST_VECTOR, "a crt object", i, arena);
            }
        }
        for (uint32_t m = 0; m < runtime.member_count; m += 1)
        {
            import_test_paths(&tally, runtime.members[m].bytes, IMPORT_TEST_VECTOR, "a member", m, arena);
        }
        runtime_release(&runtime);
    }
    return import_test_report(&tally);
}

static int run_import_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    bool cross_check = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            seed = strtoull(argv[i + 1], 0, 10);
            i += 1;
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
    int result = 1;
    if (!valid)
    {
        printf("usage: test-import [--seed N] [--cross-check]\n");
    }
    else
    {
        Arena arena = arena_reserve(1ull << 34);
        bool passed = arena.base != 0;
        passed = import_test_round_trips(seed, &arena) && passed;
        passed = import_test_objects(&arena) && passed;
        passed = import_test_stack(&arena) && passed;
        passed = import_test_archives(&arena) && passed;
        passed = import_test_libraries(&arena) && passed;
        passed = import_test_scripts(&arena) && passed;
        passed = import_test_edges(seed, &arena) && passed;
        passed = import_test_discovery() && passed;
        passed = import_test_runtime(cross_check, &arena) && passed;
        passed = import_test_static_runtime(RUNTIME_KIND_STATIC, cross_check, &arena) && passed;
        passed = import_test_static_runtime(RUNTIME_KIND_STATIC_PIE, cross_check, &arena) && passed;
        passed = import_test_shared_runtime(&arena) && passed;
        arena_release(&arena);
        result = passed ? 0 : 1;
    }
    return result;
}
