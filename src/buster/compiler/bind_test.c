// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Binding's tests, for the test-bind command:
//
//   build/compiler test-bind [--seed N] [--cross-check] [--keep]
//
// main.c includes this file after bind.c, and after link_test.c, whose sets of programs (link_test_program_options) it
// binds, the object writer's tests, whose way of running a tool (object_test_run, object_test_read_file) it shares, and
// import_test.c (import_test_version_name). Every check binds against this system's runtime (runtime_import), by both
// paths, which must make the same link, column by column, and the same imports, exports and members (bind_test_paths):
//   - Programs: test-link's two sets that call libc, each generated with room for the runtime, the runtime's first
//     objects joined after its symbols, its atoms, then the rest (link_program_generate_begin, _atoms and _end, which
//     runs link_check on the whole link). Then: one symbol per global name; every fixup's target defined, imported
//     from libc.so.6 at its export's version, the linker's, or a weak reference to nothing; main the program's, and
//     each name the crt objects refer to bound as GNU ld binds it (__libc_start_main at GLIBC_2.34, __cxa_finalize
//     weakly, __gmon_start__ and the _ITM_ functions to nothing, _GLOBAL_OFFSET_TABLE_ to the linker, __TMC_END__ to
//     crtendS.o, __dso_handle in crtbeginS.o, hidden); the crt objects' atoms before the program's and after it in
//     GCC's order, each with the runtime's bytes and fixups; .init's and .fini's fragments crti.o's first and crtn.o's
//     last, and crtendS.o's .eh_frame last.
//   - Names: every global name of the programs and every export of libc.so.6 found by the runtime's index as a scan of
//     the runtime's names finds it.
//   - Rules: links made by hand that each exercise one rule: a strong definition of _start (a duplicate); one of
//     data_start, which Scrt1.o defines weakly, and a weak one and a weak absolute one, which Scrt1.o's takes over as
//     the first in link order;
//     weak definitions of _init, before and after bind_begin, which crti.o's strong one takes over; a reference to
//     __dso_handle, which crtbeginS.o defines; names nothing defines, strong and weak; definitions that libc.so.6
//     defines too or refers to, hidden or not (exports); two symbols named write; references added after bind_begin,
//     of a name the runtime has and of one it doesn't; strong and weak references to __cxa_finalize; one to
//     pthread_atfork, which a member defines, hidden too; hidden and protected references to puts, which libc.so.6
//     can't satisfy, and a weak hidden one, which binds to nothing; references to the init and fini arrays' bounds,
//     which the linker defines; and links without room.
//   - Programs that GNU ld and lld decide for themselves (the linkers' cases): a weak _init against crti.o's, a weak
//     data_start after Scrt1.o's, a hidden and a weak hidden puts, and the arrays' bounds, each bound or refused as
//     the linkers do.
//   - A program that calls atexit, which libc_nonshared.a's member defines: bound, it must pass link_check, and the
//     member must join, defining atexit, with its references bound (__cxa_atexit to libc.so.6, __dso_handle to
//     crtbeginS.o).
//   - With --cross-check, which needs ld.lld, ld.bfd, cc and readelf on PATH: each bound program is written as
//     objects, one per unit, and linked by ld.lld and ld.bfd as a PIE with only libc.so.6 and the loader besides, in
//     GCC's order, without errors or warnings. It must run and print what its model predicts; the atexit program must
//     exit with its handler's status, 0, rather than main's, 3. And the same program's own objects linked by cc with
//     lld must have the imports at their versions, the weak references with default visibility and the exports that
//     binding found in readelf's .dynsym, and the members that lld's --why-extract lists. Each of the linkers' cases'
//     own objects, linked by cc with GNU ld and with lld, must link, or fail to, as binding did, and run with the
//     case's status, as the bound link must.
// Then the same against this system's static runtime (runtime_import_static), by both paths, which must make the same
// link and join the same members:
//   - Static programs: test-link's two sets that call libc. The members that join must be those GNU ld's archive scan
//     loads, and no others: an oracle of the test's own (bind_test_static_expected) that scans the archives' symbol
//     indices in the group's order until a pass loads nothing, rather than reading binding's references. Then
//     link_check; one symbol per global name and none of a shared library; every fixup's target defined, absolute,
//     the static linker's, a weak reference to nothing, or __tls_get_addr, left to the relaxation; main the program's,
//     __libc_start_main a member's, _init crti.o's, __dso_handle crtbeginT.o's and _dl_relocate_static_pie crt1.o's
//     (an empty one); each joined object's atoms, in link order, but those of a COMDAT group whose signature an object
//     before it has; and their priorities.
//   - Static rules, on links made by hand: a strong reference to a name nothing defines (an error naming it); strong
//     and weak references to __tls_get_addr (left undefined); the static linker's names (the linker's); weak and
//     strong references to puts (the weak one binds to its member only if the closure loads it for another reason);
//     a weak definition of __libc_start_main (crt1.o's reference takes it, and its member doesn't join); a weak
//     absolute definition of data_start (crt1.o's, first in link order, takes its place: data_start is __data_start, as
//     with GNU ld and lld); a reference to a name a member defines absolutely; and weak definitions of 4 and of 24 names
//     the members refer to (none thread-local), which the vector path's closure compares with, or past
//     BUSTER_BIND_BLOCKED leaves to the exact path's.
//   - With --cross-check: each static program's own objects linked by cc -static with GNU ld (its --trace given twice)
//     and with lld (--why-extract), which must load the members that joined and no others, and run printing what the
//     model predicts. And five C programs that cc compiles (hello, stdio, threads, jumps and weak-puts), each
//     imported as the program, bound and checked as the static programs are, and linked by both linkers, with the
//     same members, to run with the program's status.
// Then the static programs, and with --cross-check the C programs (compiled with -fPIE), against this system's static
// PIE's runtime (runtime_import_static_pie), by both paths, which must make the same link and join the same members,
// checked as against the static runtime: the members the oracle loads, in which rcrt1.o, which doesn't define
// _dl_relocate_static_pie, makes libc.a's dl-reloc-static-pie.o join; _dl_relocate_static_pie that member's, its
// _DYNAMIC the linker's, and __dso_handle crtbeginS.o's; and with --cross-check, each linked by cc -static-pie with GNU
// ld and with lld, which must load the same members and run as the model predicts or with the program's status.
// The objects and programs go in a new directory under $TMPDIR (or /tmp), removed at the end unless --keep. The exit
// status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally, the programs and their comparisons, the programs' checks, the names, links made by
// hand, the rules, the atexit program, the cross-check, the linkers' cases, the checks, the static runtimes, and
// run_bind_tests.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <elf.h>

// OS-dependent includes
#include <unistd.h>

// ---------------------------------------------------------------------------------------------------------------------
// The tally

#define BUSTER_BIND_TEST_REPORTED 20
#define BUSTER_BIND_TEST_ARENA_BYTES (1ull << 32)
#define BUSTER_BIND_TEST_BIND_BYTES (1ull << 30)
#define BUSTER_BIND_TEST_PATH_BYTES 4096

typedef struct BindTestTally BindTestTally;
struct BindTestTally
{
    const char* check;
    uint64_t checked;
    uint64_t failures;
};

static void bind_test_expect(BindTestTally* tally, bool condition, const char* what, uint64_t index)
{
    tally->checked += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_BIND_TEST_REPORTED)
        {
            printf("  %s: %s (%llu)\n", tally->check, what, (unsigned long long)index);
        }
        tally->failures += 1;
    }
}

static bool bind_test_report(const BindTestTally* tally)
{
    printf("%s: %llu checks, %llu failed\n", tally->check, (unsigned long long)tally->checked, (unsigned long long)tally->failures);
    return tally->failures == 0;
}

static const char* const bind_test_errors[] = {
    [BIND_ERROR_NONE] = "none",
    [BIND_ERROR_UNDEFINED] = "undefined",
    [BIND_ERROR_DUPLICATE] = "duplicate",
    [BIND_ERROR_SYMBOLS] = "symbols",
    [BIND_ERROR_FULL] = "full",
    [BIND_ERROR_DISCARDED] = "discarded",
    [BIND_ERROR_SNAPSHOT] = "snapshot",
};

// ---------------------------------------------------------------------------------------------------------------------
// The programs and their comparisons

// A program of link_program's bound by one path, with an arena of its own for binding's arrays.
typedef struct BindTestProgram BindTestProgram;
struct BindTestProgram
{
    LinkProgram program;
    Arena arena;
    Bind bind;
    bool bound;
};

// Generates the program with room for the runtime, joins the runtime's first objects after its symbols, adds its atoms,
// and binds the rest, numbering the runtime's units after the program's.
static bool bind_test_program(BindTestProgram* test, const Runtime* runtime, LinkProgramOptions options, BindPath path)
{
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const LinkCapacity extra = bind_capacity(runtime, &name_bytes, &atom_bytes);
    test->arena = arena_reserve(BUSTER_BIND_TEST_BIND_BYTES);
    test->bind = (Bind){ 0 };
    bool result = link_program_generate_begin(&test->program, options, extra, name_bytes, atom_bytes) && test->arena.base != 0;
    result = result && bind_begin(&test->bind, &test->program.link, runtime, path, &test->program.bytes, options.units, &test->arena);
    result = result && link_program_generate_atoms(&test->program);
    result = result && bind_end(&test->bind, &test->program.link);
    result = result && link_program_generate_end(&test->program);
    test->bound = result;
    return result;
}

static void bind_test_release(BindTestProgram* test)
{
    link_program_release(&test->program);
    arena_release(&test->arena);
}

// Whether two links hold the same records: names by their bytes, atoms by theirs, and wide addends by value.
static bool bind_test_same_links(const Link* a, const Link* b)
{
    bool result = a->names.count == b->names.count && a->atoms.count == b->atoms.count && a->symbols.count == b->symbols.count && a->fixups.count == b->fixups.count &&
                  a->fixups.wide_count == b->fixups.wide_count;
    for (NameId name = 1; result && name < a->names.count; name += 1)
    {
        result = a->names.length[name] == b->names.length[name] && a->names.hash[name] == b->names.hash[name] &&
                 memcmp(a->names.bytes[name], b->names.bytes[name], (uint64_t)a->names.length[name] + 1) == 0;
    }
    const LinkAtoms* x = &a->atoms;
    const LinkAtoms* y = &b->atoms;
    for (AtomId atom = 1; result && atom < x->count; atom += 1)
    {
        result = x->class[atom] == y->class[atom] && x->alignment_log2[atom] == y->alignment_log2[atom] && x->unit[atom] == y->unit[atom] && x->size[atom] == y->size[atom] &&
                 x->fixup_begin[atom] == y->fixup_begin[atom] && x->fixup_count[atom] == y->fixup_count[atom] && x->address[atom] == y->address[atom] &&
                 (x->bytes[atom] != 0) == (y->bytes[atom] != 0) && (x->bytes[atom] == 0 || memcmp(x->bytes[atom], y->bytes[atom], x->size[atom]) == 0);
    }
    const LinkSymbols* s = &a->symbols;
    const LinkSymbols* t = &b->symbols;
    for (SymbolId symbol = 1; result && symbol < s->count; symbol += 1)
    {
        result = s->name[symbol] == t->name[symbol] && s->atom[symbol] == t->atom[symbol] && s->value[symbol] == t->value[symbol] && s->size[symbol] == t->size[symbol] &&
                 s->flags[symbol] == t->flags[symbol] && s->source[symbol] == t->source[symbol] && s->got_slot[symbol] == t->got_slot[symbol] &&
                 s->dynamic_index[symbol] == t->dynamic_index[symbol] && s->version[symbol] == t->version[symbol] && s->reference[symbol] == t->reference[symbol];
    }
    for (FixupId fixup = 1; result && fixup < a->fixups.count; fixup += 1)
    {
        result = a->fixups.offset[fixup] == b->fixups.offset[fixup] && a->fixups.target[fixup] == b->fixups.target[fixup] && a->fixups.kind[fixup] == b->fixups.kind[fixup] &&
                 link_fixup_addend(a, fixup) == link_fixup_addend(b, fixup);
    }
    result = result && a->priorities.count == b->priorities.count;
    for (uint32_t entry = 1; result && entry < a->priorities.count; entry += 1)
    {
        result = a->priorities.atom[entry] == b->priorities.atom[entry] && a->priorities.priority[entry] == b->priorities.priority[entry];
    }
    return result;
}

// Whether two bindings ended alike: the error and its name, the imports, the exports and the members that joined.
static bool bind_test_same_binds(const Bind* a, const Bind* b)
{
    const uint32_t words = (a->runtime->member_count + 63) / 64;
    bool result = a->error == b->error && a->name == b->name && a->import_count == b->import_count && a->export_count == b->export_count &&
                  memcmp(a->imports, b->imports, a->import_count * sizeof(SymbolId)) == 0 && memcmp(a->exports, b->exports, a->export_count * sizeof(SymbolId)) == 0;
    for (uint32_t w = 0; result && w < words; w += 1)
    {
        result = a->members_joined[w] == b->members_joined[w];
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The programs' checks

static bool bind_test_named(const Link* link, NameId name, const char* text)
{
    const uint64_t length = strlen(text);
    return name != 0 && link->names.length[name] == length && memcmp(link->names.bytes[name], text, length) == 0;
}

// The first symbol of the link that isn't local and has the name, or 0.
static SymbolId bind_test_global(const Link* link, const char* text)
{
    SymbolId result = 0;
    for (SymbolId symbol = 1; result == 0 && symbol < link->symbols.count; symbol += 1)
    {
        const bool global = (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
        result = global && bind_test_named(link, link->symbols.name[symbol], text) ? symbol : 0;
    }
    return result;
}

// Whether a symbol is bound: defined, imported from libc.so.6 at the version its export has and with its type, the
// linker's, or a weak reference to nothing.
static bool bind_test_bound(const Link* link, const Runtime* runtime, SymbolId symbol)
{
    const LinkSymbols* symbols = &link->symbols;
    const NameId name = symbols->name[symbol];
    const uint8_t source = symbols->source[symbol];
    const bool weak = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    const uint32_t export = name != 0 ? import_library_find(&runtime->libc, link->names.bytes[name], link->names.length[name], link->names.hash[name]) : 0;
    const bool imported = source == BUSTER_LINK_SOURCE_RUNTIME_SHARED && export != 0 && runtime->libc.exports.version[export - 1] == symbols->version[symbol] &&
                          (runtime->libc.exports.flags[export - 1] & BUSTER_LINK_SYMBOL_TYPE) == (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_TYPE);
    const bool linker = source == BUSTER_LINK_SOURCE_LINKER && name != 0 && runtime_linker_name(runtime->kind, link->names.bytes[name], link->names.length[name]);
    return symbols->atom[symbol] != 0 || imported || linker || (source == BUSTER_LINK_SOURCE_NONE && weak);
}

// The ids of the atoms of `unit`, in id order, into `atoms`, and how many.
static uint32_t bind_test_unit_atoms(const Link* link, uint32_t unit, AtomId* atoms, uint32_t capacity)
{
    uint32_t count = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        if (link->atoms.unit[atom] == unit && count < capacity)
        {
            atoms[count] = atom;
        }
        count += link->atoms.unit[atom] == unit ? 1 : 0;
    }
    return count;
}

// Whether the link's atoms of the runtime's piece `piece`, which joined it as unit `unit`, are the runtime's: the same
// class, alignment, size and bytes, and the same fixups, each of the same kind at the same offset with the same addend,
// against a symbol of the same name (or, unnamed, of an atom of the same unit at the same place).
static bool bind_test_piece(const Link* link, const Runtime* runtime, uint32_t piece, uint32_t unit, Arena* arena)
{
    const Link* from = &runtime->link;
    const ImportMark begin = runtime->pieces[piece].begin;
    const ImportMark end = runtime->pieces[piece].end;
    const uint32_t expected = end.atoms - begin.atoms;
    const uint64_t length = arena->length;
    AtomId* atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, expected + 1);
    bool result = atoms != 0 && bind_test_unit_atoms(link, unit, atoms, expected + 1) == expected;
    for (uint32_t i = 0; result && i < expected; i += 1)
    {
        const AtomId atom = atoms[i];
        const AtomId original = begin.atoms + i;
        result = link->atoms.class[atom] == from->atoms.class[original] && link->atoms.alignment_log2[atom] == from->atoms.alignment_log2[original] &&
                 link->atoms.size[atom] == from->atoms.size[original] && (link->atoms.bytes[atom] != 0) == (from->atoms.bytes[original] != 0) &&
                 (from->atoms.bytes[original] == 0 || memcmp(link->atoms.bytes[atom], from->atoms.bytes[original], from->atoms.size[original]) == 0) &&
                 link->atoms.fixup_count[atom] == from->atoms.fixup_count[original];
        for (uint32_t f = 0; result && f < from->atoms.fixup_count[original]; f += 1)
        {
            const FixupId fixup = link->atoms.fixup_begin[atom] + f;
            const FixupId original_fixup = from->atoms.fixup_begin[original] + f;
            const SymbolId target = link->fixups.target[fixup];
            const SymbolId original_target = from->fixups.target[original_fixup];
            const NameId name = link->symbols.name[target];
            const NameId original_name = from->symbols.name[original_target];
            const bool same_name = name != 0 && original_name != 0 && link->names.length[name] == from->names.length[original_name] &&
                                   memcmp(link->names.bytes[name], from->names.bytes[original_name], link->names.length[name]) == 0;
            const bool same_place = name == 0 && original_name == 0 && link->symbols.atom[target] != 0 && link->atoms.unit[link->symbols.atom[target]] == unit &&
                                    link->symbols.value[target] == from->symbols.value[original_target];
            result = link->fixups.offset[fixup] == from->fixups.offset[original_fixup] &&
                     (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND) == (from->fixups.kind[original_fixup] & BUSTER_LINK_FIXUP_KIND) &&
                     link_fixup_addend(link, fixup) == link_fixup_addend(from, original_fixup) && (same_name || same_place);
        }
    }
    arena_pop_to(arena, length);
    return result;
}

// The bindings each libc program must have: what a name binds to, the unit of the crt object that defines it, the
// version of the export it imports, and whether the reference is weak.
typedef enum BindTestBinding
{
    BIND_TEST_DEFINED,
    BIND_TEST_IMPORT,
    BIND_TEST_NOWHERE,
    BIND_TEST_LINKER,
} BindTestBinding;

static const char* const bind_test_binding_names[] = {
    "_start",
    "_init",
    "_fini",
    "__dso_handle",
    "__TMC_END__",
    "__libc_start_main",
    "__cxa_finalize",
    "write",
    "strlen",
    "environ",
    "__errno_location",
    "__gmon_start__",
    "_ITM_deregisterTMCloneTable",
    "_ITM_registerTMCloneTable",
    "buster_absent",
    "_GLOBAL_OFFSET_TABLE_",
};

static const BindTestBinding bind_test_binding_kinds[] = {
    BIND_TEST_DEFINED,
    BIND_TEST_DEFINED,
    BIND_TEST_DEFINED,
    BIND_TEST_DEFINED,
    BIND_TEST_DEFINED,
    BIND_TEST_IMPORT,
    BIND_TEST_IMPORT,
    BIND_TEST_IMPORT,
    BIND_TEST_IMPORT,
    BIND_TEST_IMPORT,
    BIND_TEST_IMPORT,
    BIND_TEST_NOWHERE,
    BIND_TEST_NOWHERE,
    BIND_TEST_NOWHERE,
    BIND_TEST_NOWHERE,
    BIND_TEST_LINKER,
};

// For a definition, the crt object; for an import, the version.
static const uint32_t bind_test_binding_objects[] = {
    RUNTIME_OBJECT_START,
    RUNTIME_OBJECT_INIT_PROLOGUE,
    RUNTIME_OBJECT_INIT_PROLOGUE,
    RUNTIME_OBJECT_BEGIN,
    RUNTIME_OBJECT_END,
};

static const char* const bind_test_binding_versions[] = {
    "",
    "",
    "",
    "",
    "",
    "GLIBC_2.34",
    "GLIBC_2.2.5",
    "GLIBC_2.2.5",
    "GLIBC_2.2.5",
    "GLIBC_2.2.5",
    "GLIBC_2.2.5",
};

static const bool bind_test_binding_weak[] = {
    false,
    false,
    false,
    false,
    false,
    false,
    true,
    false,
    false,
    false,
    false,
    true,
    true,
    true,
    true,
    false,
};

// The order of the classes whose atoms keep their id order: the first and last atom's units of a class.
static void bind_test_class_ends(const Link* link, uint8_t class, uint32_t* first_unit, uint32_t* last_unit)
{
    bool seen = false;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        if (link->atoms.class[atom] == class)
        {
            *first_unit = seen ? *first_unit : link->atoms.unit[atom];
            *last_unit = link->atoms.unit[atom];
            seen = true;
        }
    }
}

static void bind_test_structure(BindTestTally* tally, const BindTestProgram* test, const Runtime* runtime, Arena* arena)
{
    const Link* link = &test->program.link;
    const LinkSymbols* symbols = &link->symbols;
    const uint32_t base = test->program.options.units;
    // One symbol per global name.
    bool unique = true;
    for (SymbolId symbol = 1; unique && symbol < symbols->count; symbol += 1)
    {
        const NameId name = symbols->name[symbol];
        const bool global = name != 0 && (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
        for (SymbolId other = symbol + 1; global && unique && other < symbols->count; other += 1)
        {
            const NameId other_name = symbols->name[other];
            const bool other_global = other_name != 0 && (symbols->flags[other] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
            unique = !other_global || link->names.length[other_name] != link->names.length[name] ||
                     memcmp(link->names.bytes[other_name], link->names.bytes[name], link->names.length[name]) != 0;
        }
    }
    bind_test_expect(tally, unique, "one symbol per global name", 0);
    // Every name's bytes in the link's own name arena, NUL-terminated, so that the link outlives the runtime.
    const Arena* name_arena = link->name_arena;
    bool own_names = true;
    for (NameId name = 1; own_names && name < link->names.count; name += 1)
    {
        const uint8_t* bytes = link->names.bytes[name];
        own_names = bytes >= name_arena->base && bytes + link->names.length[name] < name_arena->base + name_arena->length && bytes[link->names.length[name]] == 0;
    }
    bind_test_expect(tally, own_names, "every name's bytes in the link's name arena", 0);
    // Every atom's bytes in the program's byte arena.
    const Arena* byte_arena = &test->program.bytes;
    bool own_bytes = true;
    for (AtomId atom = 1; own_bytes && atom < link->atoms.count; atom += 1)
    {
        const uint8_t* bytes = link->atoms.bytes[atom];
        own_bytes = bytes == 0 || (bytes >= byte_arena->base && bytes + link->atoms.size[atom] <= byte_arena->base + byte_arena->length);
    }
    bind_test_expect(tally, own_bytes, "every atom's bytes in the program's arena", 0);
    // Every fixup's target bound.
    uint64_t unbound = 0;
    for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
    {
        unbound += bind_test_bound(link, runtime, link->fixups.target[fixup]) ? 0 : 1;
    }
    bind_test_expect(tally, unbound == 0, "every fixup's target bound", unbound);
    // main, and what the crt objects refer to.
    const SymbolId main_symbol = bind_test_global(link, "main");
    bind_test_expect(tally, main_symbol != 0 && main_symbol == test->program.entry && symbols->atom[main_symbol] != 0 && link->atoms.unit[symbols->atom[main_symbol]] == 0,
                     "main, the program's", main_symbol);
    for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(bind_test_binding_names); b += 1)
    {
        const SymbolId symbol = bind_test_global(link, bind_test_binding_names[b]);
        const BindTestBinding kind = bind_test_binding_kinds[b];
        const uint8_t source = symbol != 0 ? symbols->source[symbol] : BUSTER_LINK_SOURCE_COUNT;
        const AtomId atom = symbol != 0 ? symbols->atom[symbol] : 0;
        const bool weak = symbol != 0 && (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
        bool bound = symbol != 0 && weak == bind_test_binding_weak[b];
        if (kind == BIND_TEST_DEFINED)
        {
            bound = bound && atom != 0 && link->atoms.unit[atom] == base + bind_test_binding_objects[b] && source == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE;
        }
        else if (kind == BIND_TEST_IMPORT)
        {
            bound = bound && atom == 0 && source == BUSTER_LINK_SOURCE_RUNTIME_SHARED &&
                    import_equal(import_test_runtime_version_name(runtime, symbols->version[symbol]), bind_test_binding_versions[b]);
        }
        else if (kind == BIND_TEST_NOWHERE)
        {
            bound = bound && atom == 0 && source == BUSTER_LINK_SOURCE_NONE;
        }
        else
        {
            bound = bound && atom == 0 && source == BUSTER_LINK_SOURCE_LINKER;
        }
        bind_test_expect(tally, bound, bind_test_binding_names[b], symbol);
    }
    const SymbolId dso = bind_test_global(link, "__dso_handle");
    bind_test_expect(tally, dso != 0 && (symbols->flags[dso] & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_HIDDEN, "__dso_handle, hidden", dso);
    // The imports are the symbols imported from libc.so.6, and each export a visible definition of one of its names.
    uint32_t imported = 0;
    for (SymbolId symbol = 1; symbol < symbols->count; symbol += 1)
    {
        imported += symbols->source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED ? 1 : 0;
    }
    bool listed = imported == test->bind.import_count;
    for (uint32_t i = 0; listed && i < test->bind.import_count; i += 1)
    {
        listed = symbols->source[test->bind.imports[i]] == BUSTER_LINK_SOURCE_RUNTIME_SHARED;
    }
    bind_test_expect(tally, listed, "the imports listed", test->bind.import_count);
    bool exported = true;
    for (uint32_t i = 0; exported && i < test->bind.export_count; i += 1)
    {
        const SymbolId symbol = test->bind.exports[i];
        const NameId name = symbols->name[symbol];
        const uint32_t entry = runtime_find_global_name(&runtime->global_names, link->names.bytes[name], link->names.length[name], link->names.hash[name]);
        exported = symbols->atom[symbol] != 0 && entry != 0 && (runtime->global_names.export[entry - 1] != 0 || runtime->global_names.referenced[entry - 1]) &&
                   bind_visibility_rank(symbols->flags[symbol]) <= 1;
    }
    bind_test_expect(tally, exported, "each export a visible definition of one of libc.so.6's names", test->bind.export_count);
    // The crt objects in GCC's order around the program and its members, each with the runtime's atoms.
    AtomId head_last = 0;
    AtomId program_first = UINT32_MAX;
    AtomId program_last = 0;
    AtomId tail_first = UINT32_MAX;
    bool head_ordered = true;
    uint32_t previous_head = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        const uint32_t unit = link->atoms.unit[atom];
        const bool head = unit == base + RUNTIME_OBJECT_START || unit == base + RUNTIME_OBJECT_INIT_PROLOGUE || unit == base + RUNTIME_OBJECT_BEGIN;
        const bool tail = unit == base + RUNTIME_OBJECT_END || unit == base + RUNTIME_OBJECT_INIT_EPILOGUE;
        const bool program = unit < base;
        head_last = head ? atom : head_last;
        head_ordered = head_ordered && (!head || unit >= previous_head);
        previous_head = head ? unit : previous_head;
        program_first = program && atom < program_first ? atom : program_first;
        program_last = program ? atom : program_last;
        tail_first = tail && atom < tail_first ? atom : tail_first;
    }
    bind_test_expect(tally, head_ordered && head_last < program_first && program_last < tail_first, "the crt objects in GCC's order around the program", head_last);
    const uint32_t member_count = runtime->member_count;
    bool pieces = true;
    for (uint32_t piece = 0; piece < RUNTIME_OBJECTS + member_count; piece += 1)
    {
        const bool member = piece >= RUNTIME_OBJECTS;
        const bool joined = !member || (test->bind.members_joined[(piece - RUNTIME_OBJECTS) / 64] >> ((piece - RUNTIME_OBJECTS) % 64) & 1) != 0;
        pieces = pieces && (!joined || bind_test_piece(link, runtime, piece, base + piece, arena));
    }
    bind_test_expect(tally, pieces, "each object's atoms the runtime's", 0);
    uint32_t init_first = 0;
    uint32_t init_last = 0;
    uint32_t fini_first = 0;
    uint32_t fini_last = 0;
    uint32_t eh_first = 0;
    uint32_t eh_last = 0;
    bind_test_class_ends(link, BUSTER_LINK_CLASS_INIT, &init_first, &init_last);
    bind_test_class_ends(link, BUSTER_LINK_CLASS_FINI, &fini_first, &fini_last);
    bind_test_class_ends(link, BUSTER_LINK_CLASS_EH_FRAME, &eh_first, &eh_last);
    bind_test_expect(tally, init_first == base + RUNTIME_OBJECT_INIT_PROLOGUE && init_last == base + RUNTIME_OBJECT_INIT_EPILOGUE, ".init: crti.o's first, crtn.o's last", init_last);
    bind_test_expect(tally, fini_first == base + RUNTIME_OBJECT_INIT_PROLOGUE && fini_last == base + RUNTIME_OBJECT_INIT_EPILOGUE, ".fini: crti.o's first, crtn.o's last", fini_last);
    bind_test_expect(tally, eh_last == base + RUNTIME_OBJECT_END, ".eh_frame: crtendS.o's last", eh_last);
}

// Binds the program by both paths, which must agree, and checks the vector path's link. The vector path's program is
// left in `kept` for the cross-check, released by the caller.
static void bind_test_paths(BindTestTally* tally, const Runtime* runtime, LinkProgramOptions options, const char* name, BindTestProgram* kept, Arena* arena)
{
    BindTestProgram exact;
    const bool vector_bound = bind_test_program(kept, runtime, options, BIND_PATH_VECTOR);
    const bool exact_bound = bind_test_program(&exact, runtime, options, BIND_PATH_EXACT);
    bind_test_expect(tally, vector_bound, name, kept->bind.error);
    bind_test_expect(tally, exact_bound, name, exact.bind.error);
    if (!vector_bound || !exact_bound)
    {
        printf("  %s: %s: the vector path: %s (%s), the exact path: %s (%s)\n", tally->check, name, bind_test_errors[kept->bind.error],
               kept->program.problem != 0 ? kept->program.problem : "", bind_test_errors[exact.bind.error], exact.program.problem != 0 ? exact.program.problem : "");
    }
    bind_test_expect(tally, bind_test_same_links(&kept->program.link, &exact.program.link), "the paths' links the same", 0);
    bind_test_expect(tally, bind_test_same_binds(&kept->bind, &exact.bind), "the paths' imports, exports and members the same", 0);
    if (vector_bound)
    {
        bind_test_structure(tally, kept, runtime, arena);
        printf("  %s: %u atoms, %u symbols, %u fixups, %u imports, %u exports\n", name, kept->program.link.atoms.count - 1, kept->program.link.symbols.count - 1,
               kept->program.link.fixups.count - 1, kept->bind.import_count, kept->bind.export_count);
    }
    bind_test_release(&exact);
}

// ---------------------------------------------------------------------------------------------------------------------
// The names

// The runtime's global name with these bytes, plus 1, by a scan of them all.
static uint32_t bind_test_scan_name(const RuntimeNames* names, const uint8_t* bytes, uint32_t length)
{
    uint32_t result = 0;
    for (uint32_t e = 0; result == 0 && e < names->count; e += 1)
    {
        result = names->length[e] == length && memcmp(runtime_global_name(names, e), bytes, length) == 0 ? e + 1 : 0;
    }
    return result;
}

static void bind_test_names(BindTestTally* tally, const Link* link, const Runtime* runtime)
{
    const RuntimeNames* names = &runtime->global_names;
    uint64_t wrong = 0;
    uint64_t found = 0;
    for (NameId name = 1; name < link->names.count; name += 1)
    {
        const uint32_t indexed = runtime_find_global_name(names, link->names.bytes[name], link->names.length[name], link->names.hash[name]);
        wrong += indexed == bind_test_scan_name(names, link->names.bytes[name], link->names.length[name]) ? 0 : 1;
        found += indexed != 0 ? 1 : 0;
    }
    const ImportExports* exports = &runtime->libc.exports;
    for (uint32_t e = 0; e < exports->count; e += 1)
    {
        const uint32_t indexed = runtime_find_global_name(names, exports->name[e], exports->length[e], exports->hash[e]);
        wrong += indexed != 0 && names->export[indexed - 1] == e + 1 ? 0 : 1;
    }
    bind_test_expect(tally, wrong == 0, "the index finds what a scan finds", wrong);
    bind_test_expect(tally, found != 0, "some of the program's names among the runtime's", found);
}

// ---------------------------------------------------------------------------------------------------------------------
// Links made by hand

// A link with room for a few records of its own besides the runtime's.
typedef struct BindTestLink BindTestLink;
struct BindTestLink
{
    Link link;
    Arena tables;
    Arena names;
    Arena bytes;
    Arena arena;
    Bind bind;
};

#define BUSTER_BIND_TEST_OWN_RECORDS 32

static bool bind_test_link_begin(BindTestLink* test, const Runtime* runtime, uint32_t symbol_room)
{
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const LinkCapacity extra = bind_capacity(runtime, &name_bytes, &atom_bytes);
    const LinkCapacity capacity = {
        .names = extra.names + BUSTER_BIND_TEST_OWN_RECORDS,
        .atoms = extra.atoms + BUSTER_BIND_TEST_OWN_RECORDS,
        .symbols = symbol_room,
        .fixups = extra.fixups + BUSTER_BIND_TEST_OWN_RECORDS,
        .wide_addends = extra.wide_addends + BUSTER_BIND_TEST_OWN_RECORDS,
        .priorities = extra.priorities + BUSTER_BIND_TEST_OWN_RECORDS,
    };
    *test = (BindTestLink){ 0 };
    test->tables = arena_reserve(link_capacity_bytes(capacity));
    test->names = arena_reserve(name_bytes + 4096);
    test->bytes = arena_reserve(atom_bytes + 65536);
    test->arena = arena_reserve(BUSTER_BIND_TEST_BIND_BYTES);
    return test->names.base != 0 && test->bytes.base != 0 && test->arena.base != 0 && link_initialize(&test->link, &test->tables, &test->names, capacity);
}

// The symbol room a link made by hand has: its own and the runtime's.
static uint32_t bind_test_room(const Runtime* runtime)
{
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    return bind_capacity(runtime, &name_bytes, &atom_bytes).symbols + BUSTER_BIND_TEST_OWN_RECORDS;
}

static void bind_test_link_end(BindTestLink* test)
{
    arena_release(&test->tables);
    arena_release(&test->names);
    arena_release(&test->bytes);
    arena_release(&test->arena);
}

static SymbolId bind_test_symbol(BindTestLink* test, const char* name, uint8_t flags, uint8_t source)
{
    const NameId id = link_add_name(&test->link, (const uint8_t*)name, (uint32_t)strlen(name));
    return id != 0 ? link_add_symbol(&test->link, id, 0, 0, 0, flags, source) : 0;
}

// A weak absolute definition of `name` at 123 in the program, as an object's `.weak name` and `.set name, 123` give it:
// its source says only that it is absolute.
static SymbolId bind_test_weak_absolute(BindTestLink* test, const char* name)
{
    const SymbolId result = bind_test_symbol(test, name, BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_ABSOLUTE);
    if (result != 0)
    {
        test->link.symbols.value[result] = 123;
    }
    return result;
}

// Whether `symbol` is at the place of the link's global symbol named `name`: the same atom, defined, and value.
static bool bind_test_same_place(const Link* link, SymbolId symbol, const char* name)
{
    const SymbolId other = bind_test_global(link, name);
    return other != 0 && link->symbols.atom[symbol] != 0 && link->symbols.atom[symbol] == link->symbols.atom[other] && link->symbols.value[symbol] == link->symbols.value[other];
}

// A data atom of 8 bytes in unit 0 that defines `symbol`, which has no atom yet.
static bool bind_test_define(BindTestLink* test, SymbolId symbol)
{
    uint8_t* bytes = arena_push(&test->bytes, 8, 8);
    const AtomId atom = bytes != 0 ? link_add_atom(&test->link, BUSTER_LINK_CLASS_DATA, 3, 8, bytes, 0) : 0;
    if (bytes != 0)
    {
        memset(bytes, 0, 8);
    }
    return atom != 0 && link_define_symbol(&test->link, symbol, atom, 0, 8);
}

// ---------------------------------------------------------------------------------------------------------------------
// The rules

typedef enum BindTestRule
{
    BIND_TEST_RULE_START,
    BIND_TEST_RULE_DATA_START,
    BIND_TEST_RULE_DSO_HANDLE,
    BIND_TEST_RULE_NOWHERE,
    BIND_TEST_RULE_WEAK_NOWHERE,
    BIND_TEST_RULE_EXPORTS,
    BIND_TEST_RULE_TWO_SYMBOLS,
    BIND_TEST_RULE_LATE_RUNTIME_NAME,
    BIND_TEST_RULE_LATE_IMPORT,
    BIND_TEST_RULE_STRONG_FINALIZE,
    BIND_TEST_RULE_WEAK_FINALIZE,
    BIND_TEST_RULE_ATFORK,
    BIND_TEST_RULE_WEAK_ATFORK,
    BIND_TEST_RULE_LOCAL_INIT,
    BIND_TEST_RULE_PROGRAM_UNDEFINED,
    BIND_TEST_RULE_WEAK_DATA_START,
    BIND_TEST_RULE_WEAK_ABSOLUTE_DATA_START,
    BIND_TEST_RULE_WEAK_INIT,
    BIND_TEST_RULE_WEAK_INIT_DEFINED,
    BIND_TEST_RULE_HIDDEN_REFERENCE,
    BIND_TEST_RULE_PROTECTED_REFERENCE,
    BIND_TEST_RULE_WEAK_HIDDEN_REFERENCE,
    BIND_TEST_RULE_HIDDEN_MEMBER,
    BIND_TEST_RULE_ARRAY_BOUNDS,
    BIND_TEST_RULE_NO_ROOM,
    BIND_TEST_RULES,
} BindTestRule;

static const char* const bind_test_rule_names[] = {
    "a strong definition of _start",
    "a definition of data_start, which Scrt1.o defines weakly",
    "a reference to __dso_handle, which crtbeginS.o defines",
    "a strong reference to a name nothing defines",
    "a weak reference to a name nothing defines",
    "definitions of names libc.so.6 defines or refers to",
    "two symbols named write",
    "a reference to __TMC_END__ added after bind_begin",
    "a reference to memcpy added after bind_begin",
    "a strong reference to __cxa_finalize, which crtbeginS.o refers to weakly",
    "a weak reference to __cxa_finalize",
    "a reference to pthread_atfork, which a member defines",
    "a weak reference to pthread_atfork, which loads no member",
    "a local symbol named _init, which crti.o defines globally",
    "a symbol of the program's that it never defines",
    "a weak definition of data_start, which Scrt1.o, before the program in link order, defines weakly",
    "a weak absolute definition of data_start, which Scrt1.o defines weakly before it",
    "a weak definition of _init, which crti.o defines strongly",
    "a weak definition of _init with its atom before bind_begin",
    "a hidden reference to puts, which only libc.so.6 defines",
    "a protected reference to puts",
    "a weak hidden reference to puts",
    "a hidden reference to pthread_atfork, which a member defines",
    "references to the init and fini arrays' bounds, which the linker defines",
    "a link without room for the crt objects' symbols",
};

// What a rule's link keeps for its check: the symbols it made (besides main, which Scrt1.o refers to), and the link and
// binding.
typedef struct BindTestRuleLink BindTestRuleLink;
struct BindTestRuleLink
{
    BindTestLink test;
    SymbolId main;
    SymbolId symbols[4];
    bool begun;
    bool ended;
};

static void bind_test_rule_link(BindTestRuleLink* rule, const Runtime* runtime, BindTestRule which, BindPath path)
{
    const uint8_t global_object = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT;
    const uint8_t global_reference = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE;
    const uint8_t weak_reference = BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_NOTYPE;
    const uint8_t program = BUSTER_LINK_SOURCE_PROGRAM;
    const uint8_t shared = BUSTER_LINK_SOURCE_RUNTIME_SHARED;
    const uint8_t none = BUSTER_LINK_SOURCE_NONE;
    const uint32_t room = which == BIND_TEST_RULE_NO_ROOM ? 4 : bind_test_room(runtime);
    BindTestLink* test = &rule->test;
    SymbolId* symbols = rule->symbols;
    memset(symbols, 0, sizeof(rule->symbols));
    bool built = bind_test_link_begin(test, runtime, room);
    rule->main = built ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program) : 0;
    built = rule->main != 0;
    if (which == BIND_TEST_RULE_START)
    {
        symbols[0] = bind_test_symbol(test, "_start", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program);
    }
    else if (which == BIND_TEST_RULE_DATA_START)
    {
        symbols[0] = bind_test_symbol(test, "data_start", global_object, program);
    }
    else if (which == BIND_TEST_RULE_DSO_HANDLE)
    {
        symbols[0] = bind_test_symbol(test, "__dso_handle", global_reference, none);
    }
    else if (which == BIND_TEST_RULE_NOWHERE)
    {
        symbols[0] = bind_test_symbol(test, "buster_nowhere", global_reference, none);
    }
    else if (which == BIND_TEST_RULE_WEAK_NOWHERE)
    {
        symbols[0] = bind_test_symbol(test, "buster_nowhere", weak_reference, none);
    }
    else if (which == BIND_TEST_RULE_EXPORTS)
    {
        symbols[0] = bind_test_symbol(test, "strlen", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program);
        symbols[1] = bind_test_symbol(test, "qsort", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_HIDDEN, program);
        symbols[2] = bind_test_symbol(test, "_dl_argv", global_object, program);
        symbols[3] = bind_test_symbol(test, "buster_own", global_object, program);
    }
    else if (which == BIND_TEST_RULE_TWO_SYMBOLS)
    {
        symbols[0] = bind_test_symbol(test, "write", global_reference, shared);
        symbols[1] = bind_test_symbol(test, "write", global_reference, shared);
    }
    else if (which == BIND_TEST_RULE_STRONG_FINALIZE)
    {
        symbols[0] = bind_test_symbol(test, "__cxa_finalize", global_reference, shared);
    }
    else if (which == BIND_TEST_RULE_WEAK_FINALIZE)
    {
        symbols[0] = bind_test_symbol(test, "__cxa_finalize", weak_reference, shared);
    }
    else if (which == BIND_TEST_RULE_ATFORK)
    {
        symbols[0] = bind_test_symbol(test, "pthread_atfork", global_reference, shared);
    }
    else if (which == BIND_TEST_RULE_WEAK_ATFORK)
    {
        symbols[0] = bind_test_symbol(test, "pthread_atfork", weak_reference, shared);
    }
    else if (which == BIND_TEST_RULE_LOCAL_INIT)
    {
        symbols[0] = bind_test_symbol(test, "_init", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION, program);
    }
    else if (which == BIND_TEST_RULE_PROGRAM_UNDEFINED)
    {
        symbols[0] = bind_test_symbol(test, "strlen", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program);
    }
    else if (which == BIND_TEST_RULE_WEAK_DATA_START)
    {
        symbols[0] = bind_test_symbol(test, "data_start", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_OBJECT, program);
    }
    else if (which == BIND_TEST_RULE_WEAK_ABSOLUTE_DATA_START)
    {
        symbols[0] = bind_test_weak_absolute(test, "data_start");
    }
    else if (which == BIND_TEST_RULE_WEAK_INIT || which == BIND_TEST_RULE_WEAK_INIT_DEFINED)
    {
        symbols[0] = bind_test_symbol(test, "_init", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION, program);
        // A program whose objects come before the runtime's in the caller's order defines it before bind_begin.
        built = built && symbols[0] != 0 && (which == BIND_TEST_RULE_WEAK_INIT || bind_test_define(test, symbols[0]));
    }
    else if (which == BIND_TEST_RULE_HIDDEN_REFERENCE)
    {
        symbols[0] = bind_test_symbol(test, "puts", global_reference | BUSTER_LINK_SYMBOL_HIDDEN, none);
    }
    else if (which == BIND_TEST_RULE_PROTECTED_REFERENCE)
    {
        symbols[0] = bind_test_symbol(test, "puts", global_reference | BUSTER_LINK_SYMBOL_PROTECTED, none);
    }
    else if (which == BIND_TEST_RULE_WEAK_HIDDEN_REFERENCE)
    {
        symbols[0] = bind_test_symbol(test, "puts", weak_reference | BUSTER_LINK_SYMBOL_HIDDEN, none);
    }
    else if (which == BIND_TEST_RULE_HIDDEN_MEMBER)
    {
        symbols[0] = bind_test_symbol(test, "pthread_atfork", global_reference | BUSTER_LINK_SYMBOL_HIDDEN, none);
    }
    else if (which == BIND_TEST_RULE_ARRAY_BOUNDS)
    {
        static const char* const bounds[] = {
            "__init_array_start",
            "__init_array_end",
            "__fini_array_start",
            "__fini_array_end",
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bounds); i += 1)
        {
            symbols[i] = bind_test_symbol(test, bounds[i], global_reference, none);
        }
    }
    rule->begun = built && bind_begin(&test->bind, &test->link, runtime, path, &test->bytes, 1, &test->arena);
    // The program's atoms, and the symbols it adds after bind_begin.
    rule->begun = rule->begun && bind_test_define(test, rule->main);
    if (which == BIND_TEST_RULE_DATA_START || which == BIND_TEST_RULE_EXPORTS || which == BIND_TEST_RULE_LOCAL_INIT || which == BIND_TEST_RULE_WEAK_DATA_START ||
        which == BIND_TEST_RULE_WEAK_INIT)
    {
        for (uint32_t i = 0; rule->begun && i < BUSTER_ARRAY_LENGTH(rule->symbols); i += 1)
        {
            rule->begun = symbols[i] == 0 || bind_test_define(test, symbols[i]);
        }
    }
    else if (which == BIND_TEST_RULE_LATE_RUNTIME_NAME)
    {
        symbols[0] = rule->begun ? bind_test_symbol(test, "__TMC_END__", global_reference, none) : 0;
    }
    else if (which == BIND_TEST_RULE_LATE_IMPORT)
    {
        symbols[0] = rule->begun ? bind_test_symbol(test, "memcpy", global_reference, shared) : 0;
    }
    rule->ended = rule->begun && bind_end(&test->bind, &test->link);
}

// The rule's outcome: what binding reports, and what the symbols it made became.
static bool bind_test_rule_holds(const BindTestRuleLink* rule, const Runtime* runtime, BindTestRule which)
{
    const Link* link = &rule->test.link;
    const LinkSymbols* symbols = &link->symbols;
    const Bind* bind = &rule->test.bind;
    const SymbolId first = rule->symbols[0];
    const uint8_t flags = first != 0 ? symbols->flags[first] : 0;
    const AtomId atom = first != 0 ? symbols->atom[first] : 0;
    const uint32_t unit = atom != 0 ? link->atoms.unit[atom] : UINT32_MAX;
    const bool weak = (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    bool result = false;
    if (which == BIND_TEST_RULE_START)
    {
        result = !rule->begun && bind->error == BIND_ERROR_DUPLICATE && bind_test_named(link, bind->name, "_start");
    }
    else if (which == BIND_TEST_RULE_DATA_START)
    {
        result = rule->ended && bind_test_global(link, "data_start") == first && unit == 0;
    }
    else if (which == BIND_TEST_RULE_DSO_HANDLE)
    {
        result = rule->ended && unit == 1 + RUNTIME_OBJECT_BEGIN && symbols->source[first] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE &&
                 (flags & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_HIDDEN && (flags & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_OBJECT;
    }
    else if (which == BIND_TEST_RULE_NOWHERE)
    {
        result = rule->begun && !rule->ended && bind->error == BIND_ERROR_UNDEFINED && bind->name == symbols->name[first];
    }
    else if (which == BIND_TEST_RULE_WEAK_NOWHERE)
    {
        result = rule->ended && atom == 0 && symbols->source[first] == BUSTER_LINK_SOURCE_NONE;
    }
    else if (which == BIND_TEST_RULE_EXPORTS)
    {
        // strlen (which libc.so.6 defines) and _dl_argv (which it refers to), but neither the hidden qsort nor a name of
        // the program's own.
        result = rule->ended && bind->export_count == 2;
        bool strlen_exported = false;
        bool argv_exported = false;
        for (uint32_t i = 0; result && i < bind->export_count; i += 1)
        {
            strlen_exported = strlen_exported || bind->exports[i] == rule->symbols[0];
            argv_exported = argv_exported || bind->exports[i] == rule->symbols[2];
        }
        result = result && strlen_exported && argv_exported;
    }
    else if (which == BIND_TEST_RULE_TWO_SYMBOLS)
    {
        result = !rule->begun && bind->error == BIND_ERROR_SYMBOLS && bind_test_named(link, bind->name, "write");
    }
    else if (which == BIND_TEST_RULE_LATE_RUNTIME_NAME)
    {
        result = rule->begun && !rule->ended && bind->error == BIND_ERROR_SYMBOLS && bind_test_named(link, bind->name, "__TMC_END__");
    }
    else if (which == BIND_TEST_RULE_LATE_IMPORT)
    {
        result = rule->ended && atom == 0 && symbols->source[first] == BUSTER_LINK_SOURCE_RUNTIME_SHARED && bind->import_count != 0;
    }
    else if (which == BIND_TEST_RULE_STRONG_FINALIZE || which == BIND_TEST_RULE_WEAK_FINALIZE)
    {
        // The program's symbol is crtbeginS.o's reference too, and stays weak only if both are.
        result = rule->ended && atom == 0 && symbols->source[first] == BUSTER_LINK_SOURCE_RUNTIME_SHARED && weak == (which == BIND_TEST_RULE_WEAK_FINALIZE) &&
                 bind_test_global(link, "__cxa_finalize") == first;
    }
    else if (which == BIND_TEST_RULE_ATFORK)
    {
        // The member joins as unit 1 + RUNTIME_OBJECTS + its index, defining the name weakly and hidden, and its
        // reference to __register_atfork is an import.
        const SymbolId atfork = bind_test_global(link, "__register_atfork");
        result = rule->ended && atom != 0 && unit >= 1 + RUNTIME_OBJECTS && symbols->source[first] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE && weak &&
                 (flags & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_HIDDEN && atfork != 0 && symbols->source[atfork] == BUSTER_LINK_SOURCE_RUNTIME_SHARED;
        const uint32_t member = unit >= 1 + RUNTIME_OBJECTS ? unit - 1 - RUNTIME_OBJECTS : 0;
        result = result && member < runtime->member_count && (bind->members_joined[member / 64] >> (member % 64) & 1) != 0;
    }
    else if (which == BIND_TEST_RULE_WEAK_ATFORK)
    {
        bool members = false;
        for (uint32_t w = 0; w < (runtime->member_count + 63) / 64; w += 1)
        {
            members = members || bind->members_joined[w] != 0;
        }
        result = rule->ended && atom == 0 && symbols->source[first] == BUSTER_LINK_SOURCE_NONE && weak && !members;
    }
    else if (which == BIND_TEST_RULE_PROGRAM_UNDEFINED)
    {
        // Binding leaves the program's own symbols to it: link_check reports the one it never defined.
        bool imported = false;
        for (uint32_t i = 0; i < bind->import_count; i += 1)
        {
            imported = imported || bind->imports[i] == first;
        }
        result = rule->ended && atom == 0 && symbols->source[first] == BUSTER_LINK_SOURCE_PROGRAM && !imported && link_check((Link*)link) == LINK_ERROR_BINDING;
    }
    else if (which == BIND_TEST_RULE_LOCAL_INIT)
    {
        // The local stays the program's, and the global _init is crti.o's own.
        const SymbolId global = bind_test_global(link, "_init");
        result = rule->ended && unit == 0 && global != 0 && global != first && link->atoms.unit[symbols->atom[global]] == 1 + RUNTIME_OBJECT_INIT_PROLOGUE;
    }
    else if (which == BIND_TEST_RULE_WEAK_DATA_START)
    {
        // Scrt1.o's weak definition comes first in link order, and takes the program's symbol.
        result = rule->ended && bind_test_global(link, "data_start") == first && unit == 1 + RUNTIME_OBJECT_START && symbols->source[first] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE &&
                 weak;
    }
    else if (which == BIND_TEST_RULE_WEAK_ABSOLUTE_DATA_START)
    {
        // So does it over a weak absolute one, which keeps no value of its own: data_start is __data_start, as with GNU ld
        // and lld.
        result = rule->ended && bind_test_global(link, "data_start") == first && unit == 1 + RUNTIME_OBJECT_START && symbols->source[first] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE &&
                 weak && bind_test_same_place(link, first, "__data_start");
    }
    else if (which == BIND_TEST_RULE_WEAK_INIT || which == BIND_TEST_RULE_WEAK_INIT_DEFINED)
    {
        // crti.o's strong definition takes the program's symbol, with its atom in .init and its visibility.
        const RuntimeNames* names = &runtime->global_names;
        const uint32_t entry = runtime_find_global_name(names, (const uint8_t*)"_init", 5, link_hash((const uint8_t*)"_init", 5));
        const SymbolId definition = entry != 0 ? names->definition[entry - 1] : 0;
        const uint8_t visibility = definition != 0 ? runtime->link.symbols.flags[definition] & BUSTER_LINK_SYMBOL_VISIBILITY : 0xff;
        result = rule->ended && bind_test_global(link, "_init") == first && unit == 1 + RUNTIME_OBJECT_INIT_PROLOGUE && link->atoms.class[atom] == BUSTER_LINK_CLASS_INIT &&
                 (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL && (flags & BUSTER_LINK_SYMBOL_VISIBILITY) == visibility &&
                 symbols->source[first] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE;
    }
    else if (which == BIND_TEST_RULE_HIDDEN_REFERENCE || which == BIND_TEST_RULE_PROTECTED_REFERENCE)
    {
        // libc.so.6's definition can't satisfy it, and nothing else defines it.
        result = rule->begun && !rule->ended && bind->error == BIND_ERROR_UNDEFINED && bind->name == symbols->name[first];
    }
    else if (which == BIND_TEST_RULE_WEAK_HIDDEN_REFERENCE)
    {
        bool imported = false;
        for (uint32_t i = 0; i < bind->import_count; i += 1)
        {
            imported = imported || bind->imports[i] == first;
        }
        result = rule->ended && atom == 0 && symbols->source[first] == BUSTER_LINK_SOURCE_NONE && !imported;
    }
    else if (which == BIND_TEST_RULE_HIDDEN_MEMBER)
    {
        // A member is within the executable, so it defines the name.
        const uint32_t member = unit >= 1 + RUNTIME_OBJECTS ? unit - 1 - RUNTIME_OBJECTS : UINT32_MAX;
        result = rule->ended && atom != 0 && symbols->source[first] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE && member < runtime->member_count &&
                 (bind->members_joined[member / 64] >> (member % 64) & 1) != 0;
    }
    else if (which == BIND_TEST_RULE_ARRAY_BOUNDS)
    {
        result = rule->ended;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rule->symbols); i += 1)
        {
            result = result && rule->symbols[i] != 0 && symbols->atom[rule->symbols[i]] == 0 && symbols->source[rule->symbols[i]] == BUSTER_LINK_SOURCE_LINKER;
        }
    }
    else
    {
        result = !rule->begun && bind->error == BIND_ERROR_FULL;
    }
    return result;
}

// bind_merge's rule for a strong reference that joins, once binding has begun, a weak one that it left to nothing (as
// a member's would, which libc_nonshared.a's don't have): an error naming it, but not for one left to a crt object
// after the program (source RUNTIME_ARCHIVE). Two links of a symbol each stand for the runtime and the program.
static bool bind_test_strong_after_weak(void)
{
    const LinkCapacity capacity = {
        .names = 4,
        .atoms = 4,
        .symbols = 4,
        .fixups = 4,
        .wide_addends = 4,
    };
    Arena tables = arena_reserve(2 * link_capacity_bytes(capacity));
    Arena names = arena_reserve(4096);
    Runtime runtime = { 0 };
    Link link;
    bool result = link_initialize(&runtime.link, &tables, &names, capacity) && link_initialize(&link, &tables, &names, capacity);
    const NameId from_name = result ? link_add_name(&runtime.link, (const uint8_t*)"buster_nowhere", 14) : 0;
    const NameId name = result ? link_add_name(&link, (const uint8_t*)"buster_nowhere", 14) : 0;
    const SymbolId from_symbol = from_name != 0 ? link_add_symbol(&runtime.link, from_name, 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE) : 0;
    const SymbolId symbol = name != 0 ? link_add_symbol(&link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_WEAK, BUSTER_LINK_SOURCE_NONE) : 0;
    Bind bind = {
        .runtime = &runtime,
        .resolving = false,
        .bound = false,
    };
    result = from_symbol != 0 && symbol != 0;
    // Before binding begins, the references join without an error; after, the strong one is an error.
    bind_merge(&bind, &link, from_symbol, symbol, 0, false);
    result = result && bind.error == BIND_ERROR_NONE && (link.symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL;
    link.symbols.flags[symbol] = BUSTER_LINK_SYMBOL_WEAK;
    bind.resolving = true;
    bind.bound = true;
    bind_merge(&bind, &link, from_symbol, symbol, 0, false);
    result = result && bind.error == BIND_ERROR_UNDEFINED && bind.name == name;
    bind.error = BIND_ERROR_NONE;
    link.symbols.flags[symbol] = BUSTER_LINK_SYMBOL_WEAK;
    link.symbols.source[symbol] = BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE;
    bind_merge(&bind, &link, from_symbol, symbol, 0, false);
    result = result && bind.error == BIND_ERROR_NONE;
    arena_release(&tables);
    arena_release(&names);
    return result;
}

// The rules this system's runtime can't reach through the entry points, checked on binding's own steps: a name a crt
// object after the program defines is left to it with source RUNTIME_ARCHIVE; and an import that a member came to
// define is dropped from the imports.
static bool bind_test_steps(const Runtime* runtime)
{
    BindTestRuleLink rule;
    BindTestLink* test = &rule.test;
    bool result = bind_test_link_begin(test, runtime, bind_test_room(runtime));
    rule.main = result ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM) : 0;
    result = rule.main != 0 && bind_begin(&test->bind, &test->link, runtime, BIND_PATH_VECTOR, &test->bytes, 1, &test->arena) && bind_test_define(test, rule.main);
    Bind* bind = &test->bind;
    Link* link = &test->link;
    const SymbolId tmc = result ? bind_test_global(link, "__TMC_END__") : 0;
    if (tmc != 0)
    {
        bind->resolving = true;
        bind->bound = true;
        bind_resolve(bind, link, 1, link->symbols.count, true);
    }
    result = tmc != 0 && bind->error == BIND_ERROR_NONE && link->symbols.atom[tmc] == 0 && link->symbols.source[tmc] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE;
    // An import that is no longer one leaves the list.
    const SymbolId write_symbol = result ? bind_test_symbol(test, "write", BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_RUNTIME_SHARED) : 0;
    const SymbolId strlen_symbol = write_symbol != 0 ? bind_test_symbol(test, "strlen", BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE) : 0;
    result = result && strlen_symbol != 0;
    if (result)
    {
        bind->import_count = 0;
        bind->imports[bind->import_count++] = write_symbol;
        bind->imports[bind->import_count++] = strlen_symbol;
        bind_keep_imports(bind, link);
    }
    result = result && bind->import_count == 1 && bind->imports[0] == write_symbol;
    bind_test_link_end(test);
    return result;
}

static bool bind_test_rules(const Runtime* runtime)
{
    BindTestTally tally = {
        .check = "rules",
    };
    for (uint32_t r = 0; r < BIND_TEST_RULES; r += 1)
    {
        BindTestRuleLink vector;
        BindTestRuleLink exact;
        bind_test_rule_link(&vector, runtime, (BindTestRule)r, BIND_PATH_VECTOR);
        bind_test_rule_link(&exact, runtime, (BindTestRule)r, BIND_PATH_EXACT);
        const bool holds = bind_test_rule_holds(&vector, runtime, (BindTestRule)r);
        if (!holds && tally.failures < BUSTER_BIND_TEST_REPORTED)
        {
            printf("  rules: %s: begun %d, ended %d, error %s\n", bind_test_rule_names[r], vector.begun, vector.ended, bind_test_errors[vector.test.bind.error]);
        }
        bind_test_expect(&tally, holds, bind_test_rule_names[r], r);
        bind_test_expect(&tally, vector.begun == exact.begun && vector.ended == exact.ended && bind_test_same_links(&vector.test.link, &exact.test.link) &&
                                     bind_test_same_binds(&vector.test.bind, &exact.test.bind),
                         "the paths agree", r);
        bind_test_link_end(&vector.test);
        bind_test_link_end(&exact.test);
    }
    bind_test_expect(&tally, bind_test_strong_after_weak(), "a strong reference joining a weak one left to nothing", 0);
    bind_test_expect(&tally, bind_test_steps(runtime), "a name left to crtendS.o, and an import a member defined", 0);
    return bind_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Libraries named with -l
//
// Links made by hand bound with this system's libm (runtime_libraries_import, -lm) by both paths, which must make the
// same link: strong references to sin and to ldexp, which libc.so.6 exports too and which binds to libm's, before
// libc.so.6 on the line; a weak one to cos, an import once libm is needed; one to write, libc.so.6's; and a definition
// of cbrt, which libm exports too, which the executable exports. Then a weak reference alone, which doesn't make libm
// needed and is left to nothing; and the import's refusals, on files of the test's own in `directory`.

// Writes `length` bytes into a new file at `path`: whether it could.
static bool bind_test_write_file(const char* path, const char* bytes, uint64_t length)
{
    const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    const bool written = file >= 0 && write(file, bytes, length) == (ssize_t)length;
    if (file >= 0)
    {
        close(file);
    }
    return written;
}

// Whether `version` is one of library `l`'s versions.
static bool bind_test_library_version(const RuntimeLibraries* libraries, uint32_t l, uint16_t version)
{
    bool found = false;
    for (uint32_t v = 0; v < libraries->libraries[l].version_count; v += 1)
    {
        found = found || (!libraries->libraries[l].versions[v].base && libraries->libraries[l].versions[v].index == version);
    }
    return found;
}

// Whether `symbol` is among the bind's imports (or with `exports`, its exports).
static bool bind_test_listed(const Bind* bind, SymbolId symbol, bool exports)
{
    const SymbolId* list = exports ? bind->exports : bind->imports;
    const uint32_t count = exports ? bind->export_count : bind->import_count;
    bool found = false;
    for (uint32_t i = 0; i < count; i += 1)
    {
        found = found || list[i] == symbol;
    }
    return found;
}

typedef struct BindTestLibraryLink BindTestLibraryLink;
struct BindTestLibraryLink
{
    BindTestLink test;
    SymbolId sin;
    SymbolId ldexp;
    SymbolId cos;
    SymbolId write;
    SymbolId cbrt;
    bool bound;
};

// The link: with `strong`, main's references to sin, ldexp and write and its definition of cbrt beside the weak
// reference to cos; without, the weak reference alone.
static void bind_test_library_link(BindTestLibraryLink* link, const Runtime* runtime, const RuntimeLibraries* libraries, bool strong, BindPath path)
{
    *link = (BindTestLibraryLink){ 0 };
    BindTestLink* test = &link->test;
    bool result = bind_test_link_begin(test, runtime, bind_test_room(runtime));
    const SymbolId main = result ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM) : 0;
    link->cos = main != 0 ? bind_test_symbol(test, "cos", BUSTER_LINK_SYMBOL_WEAK, BUSTER_LINK_SOURCE_NONE) : 0;
    if (strong && link->cos != 0)
    {
        link->sin = bind_test_symbol(test, "sin", BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE);
        link->ldexp = bind_test_symbol(test, "ldexp", BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE);
        link->write = bind_test_symbol(test, "write", BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE);
        link->cbrt = bind_test_symbol(test, "cbrt", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM);
    }
    result = link->cos != 0 && (!strong || (link->sin != 0 && link->ldexp != 0 && link->write != 0 && link->cbrt != 0));
    result = result && bind_begin(&test->bind, &test->link, runtime, path, &test->bytes, 1, &test->arena) && bind_test_define(test, main) &&
             (!strong || bind_test_define(test, link->cbrt));
    test->bind.libraries = libraries;
    link->bound = result && bind_end(&test->bind, &test->link);
}

// Whether `symbol` was bound to an export of library `l` (or with `library` false, of libc.so.6): an import at its
// version.
static bool bind_test_library_import(const BindTestLibraryLink* link, const Runtime* runtime, const RuntimeLibraries* libraries, SymbolId symbol, bool library)
{
    const LinkSymbols* symbols = &link->test.link.symbols;
    const uint16_t version = symbols->version[symbol];
    bool libc = false;
    for (uint32_t v = 0; v < runtime->libc.version_count; v += 1)
    {
        libc = libc || runtime->libc.versions[v].index == version;
    }
    const bool at = library ? bind_test_library_version(libraries, 0, version) : libc;
    return symbols->source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED && at && bind_test_listed(&link->test.bind, symbol, false);
}

// The import's refusals and its quiet cases, on files in `directory`: a name no directory has, an archive with members,
// an empty archive (glibc's libpthread.a), a script naming a relative path, and -lm twice, which links libm once.
static void bind_test_library_import_cases(BindTestTally* tally, const Runtime* runtime, const char* directory)
{
    char path[BUSTER_BIND_TEST_PATH_BYTES];
    static const char archive_bytes[] = "!<arch>\n/               0           0     0     644     4         `\n\0\0\0\0";
    static const char script_bytes[] = "GROUP ( libbuster_relative.so.1 )\n";
    const char* const files[][2] = {
        { "libbuster_members.a", archive_bytes },
        { "libbuster_empty.a", "!<arch>\n" },
        { "libbuster_script.so", script_bytes },
    };
    const uint64_t lengths[] = { sizeof(archive_bytes) - 1, 8, sizeof(script_bytes) - 1 };
    bool written = true;
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files[f][0]);
        written = written && bind_test_write_file(path, files[f][1], lengths[f]);
    }
    bind_test_expect(tally, written, "the libraries' files written", 0);
    const String8 directories[] = { { .pointer = (char*)directory, .length = strlen(directory) } };
    struct
    {
        const char* name;
        RuntimeError error;
        uint32_t count;
    } const cases[] = {
        { "buster_nowhere", RUNTIME_ERROR_MISSING, 0 },
        { "buster_members", RUNTIME_ERROR_ARCHIVE, 0 },
        { "buster_empty", RUNTIME_ERROR_NONE, 0 },
        { "buster_script", RUNTIME_ERROR_IMPORT, 0 },
    };
    for (uint32_t c = 0; written && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        RuntimeLibraries libraries;
        const String8 name = { .pointer = (char*)cases[c].name, .length = strlen(cases[c].name) };
        const RuntimeError error = runtime_libraries_import(&libraries, runtime, &name, 1, directories, 1);
        bind_test_expect(tally, error == cases[c].error && libraries.count == cases[c].count, cases[c].name, c);
        runtime_libraries_release(&libraries);
    }
    // -lm twice: one libm (and libmvec, which its script names as needed).
    RuntimeLibraries once;
    RuntimeLibraries twice;
    const String8 m[] = { { .pointer = "m", .length = 1 }, { .pointer = "m", .length = 1 } };
    const RuntimeError once_error = runtime_libraries_import(&once, runtime, m, 1, 0, 0);
    const RuntimeError twice_error = runtime_libraries_import(&twice, runtime, m, 2, 0, 0);
    bind_test_expect(tally, once_error == RUNTIME_ERROR_NONE && twice_error == RUNTIME_ERROR_NONE && once.count != 0 && twice.count == once.count,
                     "-lm twice links libm once", 0);
    runtime_libraries_release(&once);
    runtime_libraries_release(&twice);
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(files); f += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, files[f][0]);
        unlink(path);
    }
}

// Libraries a needed library refers to (#398's review): libbuster_a.so and libbuster_c.so, made by hand, export a_func
// and c_func and refer to b_func, which libbuster_b.so exports, without a DT_NEEDED entry for it; linked as
// -lbuster_a -lbuster_c -lbuster_b. main refers to a_func and c_func. Without a definition of b_func, the link needs all
// three libraries, and so with a strong reference of main's to it, an import of libbuster_b.so's. With a definition the
// executable exports (global, weak, protected or absolute), which the loader binds the libraries' references to, as
// GNU ld and lld link it, the link needs libbuster_a.so and libbuster_c.so alone and exports b_func; with a hidden one,
// which the loader can't see, all three, and b_func isn't exported. Each by both paths, which must make the same link,
// and the global definition after 0 to 15 others, so that b_func comes at every lane of the vector path's batches.

typedef enum BindTestDependency
{
    BIND_TEST_DEPENDENCY_NONE,
    BIND_TEST_DEPENDENCY_GLOBAL,
    BIND_TEST_DEPENDENCY_WEAK,
    BIND_TEST_DEPENDENCY_PROTECTED,
    BIND_TEST_DEPENDENCY_HIDDEN,
    BIND_TEST_DEPENDENCY_ABSOLUTE,
    BIND_TEST_DEPENDENCY_REFERENCE,
    BIND_TEST_DEPENDENCIES,
} BindTestDependency;

// Writes to `path` a shared library made by hand with `soname`, the functions `exports` it defines and the names
// `references` it refers to strongly, without versions or DT_NEEDED entries: whether it could.
static bool bind_test_write_library(const char* path, const char* soname, const char* const* exports, uint32_t export_count, const char* const* references,
                                    uint32_t reference_count, Arena* arena)
{
    uint8_t text[16];
    memset(text, 0xc3, sizeof(text));
    ImportTestSymbols symbols = { 0 };
    import_test_add_symbol(&symbols, "", 0, 0, SHN_UNDEF, 0, 0, BUSTER_IMPORT_TEST_NONE, 0);
    for (uint32_t e = 0; e < export_count; e += 1)
    {
        import_test_add_symbol(&symbols, exports[e], ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), 0, 1, e, 1, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, 0);
    }
    for (uint32_t r = 0; r < reference_count; r += 1)
    {
        import_test_add_symbol(&symbols, references[r], ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE), 0, SHN_UNDEF, 0, 0, BUSTER_IMPORT_TEST_NONE, 0);
    }
    const Elf64_Dyn dynamic[2] = {
        { .d_tag = DT_SONAME, .d_un.d_val = import_test_string(&symbols.names, soname) },
        { .d_tag = DT_NULL, .d_un.d_val = 0 },
    };
    ImportTestSection sections[5] = { 0 };
    sections[1] = import_test_section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16, 0, 0, 0, text, sizeof(text));
    sections[2] = import_test_section(".dynsym", SHT_DYNSYM, SHF_ALLOC, 8, sizeof(Elf64_Sym), 3, 1, symbols.entries, (uint64_t)symbols.count * sizeof(Elf64_Sym));
    sections[3] = import_test_section(".dynstr", SHT_STRTAB, SHF_ALLOC, 1, 0, 0, 0, symbols.names.bytes, symbols.names.length);
    sections[4] = import_test_section(".dynamic", SHT_DYNAMIC, SHF_WRITE | SHF_ALLOC, 8, sizeof(Elf64_Dyn), 3, 0, dynamic, sizeof(dynamic));
    ImportTestFile file;
    const bool built = import_test_elf(&file, ET_DYN, sections, BUSTER_ARRAY_LENGTH(sections), arena);
    return built && bind_test_write_file(path, (const char*)file.bytes, file.size);
}

// The link: main's strong references to a_func and c_func, `fillers` definitions of the program's, then b_func as
// `dependency` has it, bound by `path` against `libraries`. Returns b_func's symbol (0 for none) in *b_func, and whether
// the link bound.
static bool bind_test_dependency_link(BindTestLink* test, const Runtime* runtime, const RuntimeLibraries* libraries, BindTestDependency dependency,
                                      uint32_t fillers, BindPath path, SymbolId* b_func)
{
    static const uint8_t flags[BIND_TEST_DEPENDENCIES] = {
        [BIND_TEST_DEPENDENCY_GLOBAL] = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION,
        [BIND_TEST_DEPENDENCY_WEAK] = BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION,
        [BIND_TEST_DEPENDENCY_PROTECTED] = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_PROTECTED,
        [BIND_TEST_DEPENDENCY_HIDDEN] = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_HIDDEN,
        [BIND_TEST_DEPENDENCY_ABSOLUTE] = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE,
        [BIND_TEST_DEPENDENCY_REFERENCE] = BUSTER_LINK_SYMBOL_GLOBAL,
    };
    static const uint8_t sources[BIND_TEST_DEPENDENCIES] = {
        [BIND_TEST_DEPENDENCY_GLOBAL] = BUSTER_LINK_SOURCE_PROGRAM,
        [BIND_TEST_DEPENDENCY_WEAK] = BUSTER_LINK_SOURCE_PROGRAM,
        [BIND_TEST_DEPENDENCY_PROTECTED] = BUSTER_LINK_SOURCE_PROGRAM,
        [BIND_TEST_DEPENDENCY_HIDDEN] = BUSTER_LINK_SOURCE_PROGRAM,
        [BIND_TEST_DEPENDENCY_ABSOLUTE] = BUSTER_LINK_SOURCE_ABSOLUTE,
        [BIND_TEST_DEPENDENCY_REFERENCE] = BUSTER_LINK_SOURCE_NONE,
    };
    bool result = bind_test_link_begin(test, runtime, bind_test_room(runtime));
    const SymbolId main = result ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM) : 0;
    result = main != 0 && bind_test_symbol(test, "a_func", BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE) != 0 &&
             bind_test_symbol(test, "c_func", BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE) != 0;
    SymbolId filler[16] = { 0 };
    for (uint32_t f = 0; result && f < fillers; f += 1)
    {
        char name[32];
        snprintf(name, sizeof(name), "filler_%u", f);
        filler[f] = bind_test_symbol(test, name, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        result = filler[f] != 0;
    }
    *b_func = result && dependency != BIND_TEST_DEPENDENCY_NONE ? bind_test_symbol(test, "b_func", flags[dependency], sources[dependency]) : 0;
    result = result && (dependency == BIND_TEST_DEPENDENCY_NONE || *b_func != 0);
    if (result && dependency == BIND_TEST_DEPENDENCY_ABSOLUTE)
    {
        test->link.symbols.value[*b_func] = 123;
    }
    result = result && bind_begin(&test->bind, &test->link, runtime, path, &test->bytes, 1, &test->arena) && bind_test_define(test, main);
    for (uint32_t f = 0; result && f < fillers; f += 1)
    {
        result = bind_test_define(test, filler[f]);
    }
    const bool atom = dependency != BIND_TEST_DEPENDENCY_NONE && dependency != BIND_TEST_DEPENDENCY_ABSOLUTE && dependency != BIND_TEST_DEPENDENCY_REFERENCE;
    result = result && (!atom || bind_test_define(test, *b_func));
    test->bind.libraries = libraries;
    return result && bind_end(&test->bind, &test->link);
}

static void bind_test_dependencies(BindTestTally* tally, const Runtime* runtime, const char* directory)
{
    static const char* const a_exports[] = { "a_func" };
    static const char* const c_exports[] = { "c_func" };
    static const char* const b_exports[] = { "b_func" };
    static const char* const references[] = { "b_func" };
    static const char* const names[] = { "buster_a", "buster_c", "buster_b" };
    Arena arena = arena_reserve(1u << 20);
    char paths[3][BUSTER_BIND_TEST_PATH_BYTES];
    for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(names); l += 1)
    {
        snprintf(paths[l], sizeof(paths[l]), "%s/lib%s.so", directory, names[l]);
    }
    const bool written = arena.base != 0 && bind_test_write_library(paths[0], "libbuster_a.so", a_exports, 1, references, 1, &arena) &&
                         bind_test_write_library(paths[1], "libbuster_c.so", c_exports, 1, references, 1, &arena) &&
                         bind_test_write_library(paths[2], "libbuster_b.so", b_exports, 1, 0, 0, &arena);
    const String8 directories[] = { { .pointer = (char*)directory, .length = strlen(directory) } };
    String8 libraries_named[BUSTER_ARRAY_LENGTH(names)];
    for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(names); l += 1)
    {
        libraries_named[l] = (String8){ .pointer = (char*)names[l], .length = strlen(names[l]) };
    }
    RuntimeLibraries libraries = { 0 };
    const RuntimeError imported = written ? runtime_libraries_import(&libraries, runtime, libraries_named, BUSTER_ARRAY_LENGTH(names), directories, 1) : RUNTIME_ERROR_IMPORT;
    bind_test_expect(tally, written && imported == RUNTIME_ERROR_NONE && libraries.count == 3, "the libraries a needed library refers to imported", 0);
    struct
    {
        const char* name;
        BindTestDependency dependency;
        uint32_t needed;
        bool exported;
    } const cases[] = {
        { "no b_func: libbuster_b.so needed for the libraries' references", BIND_TEST_DEPENDENCY_NONE, 7, false },
        { "b_func main's: the libraries' references satisfied, b_func exported", BIND_TEST_DEPENDENCY_GLOBAL, 3, true },
        { "b_func weak, main's: satisfied, exported", BIND_TEST_DEPENDENCY_WEAK, 3, true },
        { "b_func protected, main's: satisfied, exported", BIND_TEST_DEPENDENCY_PROTECTED, 3, true },
        { "b_func hidden, main's: not satisfied, libbuster_b.so needed, not exported", BIND_TEST_DEPENDENCY_HIDDEN, 7, false },
        { "b_func absolute, main's: satisfied, exported", BIND_TEST_DEPENDENCY_ABSOLUTE, 3, true },
        { "b_func a reference of main's: libbuster_b.so needed, an import", BIND_TEST_DEPENDENCY_REFERENCE, 7, false },
    };
    for (uint32_t c = 0; imported == RUNTIME_ERROR_NONE && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const uint32_t filler_counts = cases[c].dependency == BIND_TEST_DEPENDENCY_GLOBAL ? 16 : 1;
        for (uint32_t fillers = 0; fillers < filler_counts; fillers += 1)
        {
            BindTestLink vector;
            BindTestLink exact;
            SymbolId vector_b = 0;
            SymbolId exact_b = 0;
            const bool vector_bound = bind_test_dependency_link(&vector, runtime, &libraries, cases[c].dependency, fillers, BIND_PATH_VECTOR, &vector_b);
            const bool exact_bound = bind_test_dependency_link(&exact, runtime, &libraries, cases[c].dependency, fillers, BIND_PATH_EXACT, &exact_b);
            const Bind* bind = &vector.bind;
            const bool agree = vector_bound && exact_bound && bind_test_same_links(&vector.link, &exact.link) && bind_test_same_binds(&vector.bind, &exact.bind) &&
                               vector.bind.libraries_needed == exact.bind.libraries_needed;
            const bool exported = vector_b != 0 && bind_test_listed(bind, vector_b, true);
            const bool imported_b = vector_b != 0 && bind_test_listed(bind, vector_b, false);
            const bool expected = cases[c].dependency == BIND_TEST_DEPENDENCY_REFERENCE ? imported_b : !imported_b;
            if (!agree || bind->libraries_needed != cases[c].needed || exported != cases[c].exported || !expected)
            {
                printf("  %s, after %u definitions: bound %d and %d, paths agree %d, needed %#x, exported %d, imported %d\n", cases[c].name, fillers, vector_bound,
                       exact_bound, agree, bind->libraries_needed, exported, imported_b);
            }
            bind_test_expect(tally, agree && bind->libraries_needed == cases[c].needed && exported == cases[c].exported && expected, cases[c].name, fillers);
            bind_test_link_end(&vector);
            bind_test_link_end(&exact);
        }
    }
    runtime_libraries_release(&libraries);
    for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(names); l += 1)
    {
        unlink(paths[l]);
    }
    arena_release(&arena);
}

static bool bind_test_libraries(const Runtime* runtime, const char* directory)
{
    BindTestTally tally = {
        .check = "libraries",
    };
    RuntimeLibraries libraries;
    const String8 m = { .pointer = "m", .length = 1 };
    const RuntimeError imported = runtime_libraries_import(&libraries, runtime, &m, 1, 0, 0);
    bind_test_expect(&tally, imported == RUNTIME_ERROR_NONE && libraries.count != 0, "-lm imports", 0);
    for (uint32_t strong = 0; imported == RUNTIME_ERROR_NONE && strong < 2; strong += 1)
    {
        BindTestLibraryLink vector;
        BindTestLibraryLink exact;
        bind_test_library_link(&vector, runtime, &libraries, strong != 0, BIND_PATH_VECTOR);
        bind_test_library_link(&exact, runtime, &libraries, strong != 0, BIND_PATH_EXACT);
        bind_test_expect(&tally, vector.bound && exact.bound, "bound", strong);
        bind_test_expect(&tally, bind_test_same_links(&vector.test.link, &exact.test.link) && bind_test_same_binds(&vector.test.bind, &exact.test.bind) &&
                                     vector.test.bind.libraries_needed == exact.test.bind.libraries_needed,
                         "the paths agree", strong);
        const Link* link = &vector.test.link;
        const Bind* bind = &vector.test.bind;
        if (strong != 0 && vector.bound)
        {
            bind_test_expect(&tally, bind->libraries_needed == 1, "libm needed", 0);
            bind_test_expect(&tally, bind_test_library_import(&vector, runtime, &libraries, vector.sin, true), "sin libm's", 0);
            bind_test_expect(&tally, bind_test_library_import(&vector, runtime, &libraries, vector.ldexp, true), "ldexp libm's, before libc.so.6's", 0);
            bind_test_expect(&tally, bind_test_library_import(&vector, runtime, &libraries, vector.cos, true), "cos, weak, an import of libm's", 0);
            bind_test_expect(&tally, bind_test_library_import(&vector, runtime, &libraries, vector.write, false), "write libc.so.6's", 0);
            bind_test_expect(&tally, link->symbols.source[vector.cbrt] == BUSTER_LINK_SOURCE_PROGRAM && bind_test_listed(bind, vector.cbrt, true) &&
                                         !bind_test_listed(bind, vector.cbrt, false),
                             "cbrt the program's, exported", 0);
        }
        else if (vector.bound)
        {
            bind_test_expect(&tally, bind->libraries_needed == 0 && link->symbols.source[vector.cos] == BUSTER_LINK_SOURCE_NONE &&
                                         link->symbols.version[vector.cos] == 0 && !bind_test_listed(bind, vector.cos, false),
                             "a weak reference alone needs no library", 0);
        }
        bind_test_link_end(&vector.test);
        bind_test_link_end(&exact.test);
    }
    runtime_libraries_release(&libraries);
    bind_test_library_import_cases(&tally, runtime, directory);
    bind_test_dependencies(&tally, runtime, directory);
    return bind_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The atexit program
//
// main registers handler with atexit, which libc_nonshared.a's member defines, and returns 3; handler, which exit runs,
// exits with status 0.

#define BUSTER_BIND_TEST_MAIN_STATUS 3
#define BUSTER_BIND_TEST_MAX_CODE 16

// A TEXT atom of the instructions, in unit 0, defining `symbol`: the references are each instruction's displacement.
static bool bind_test_code(BindTestLink* test, const X86Instruction* instructions, uint32_t count, const LinkReference* references, uint32_t reference_count, SymbolId symbol)
{
    uint8_t encoded[BUSTER_X86_ENCODER_CAPACITY(BUSTER_BIND_TEST_MAX_CODE)];
    uint32_t ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_BIND_TEST_MAX_CODE)];
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

typedef struct BindTestAtexit BindTestAtexit;
struct BindTestAtexit
{
    BindTestLink test;
    SymbolId main;
    SymbolId handler;
    SymbolId atexit;
    bool bound;
};

// The program's symbols, then the runtime's first objects, its code, and the rest; without `bound`, only the program,
// for a link of its own objects by cc. program->bound also needs link_check to accept the link, as the object writer
// does of the links the cross-check writes.
static void bind_test_atexit(BindTestAtexit* program, const Runtime* runtime, BindPath path, bool bound)
{
    BindTestLink* test = &program->test;
    bool built = bind_test_link_begin(test, runtime, bind_test_room(runtime));
    program->main = built ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM) : 0;
    program->handler = built ? bind_test_symbol(test, "buster_handler", BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM) : 0;
    program->atexit = built ? bind_test_symbol(test, "atexit", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE) : 0;
    built = program->main != 0 && program->handler != 0 && program->atexit != 0;
    built = built && (!bound || bind_begin(&test->bind, &test->link, runtime, path, &test->bytes, 1, &test->arena));
    // main: sub rsp, 8; lea rdi, [rip + handler]; call atexit; add rsp, 8; mov eax, 3; ret.
    const X86Instruction main_code[] = {
        {
            .immediate = 8,
            .rm = X86_REGISTER_RSP,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .form = X86_FORM_SUB_RM64_IMM8,
        },
        {
            .reg = X86_REGISTER_RDI,
            .mode = BUSTER_X86_MEMORY_RIP,
            .form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP,
        },
        {
            .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP,
        },
        {
            .immediate = 8,
            .rm = X86_REGISTER_RSP,
            .mode = BUSTER_X86_REGISTER_DIRECT,
            .form = X86_FORM_ADD_RM64_IMM8,
        },
        {
            .immediate = BUSTER_BIND_TEST_MAIN_STATUS,
            .rm = X86_REGISTER_RAX,
            .form = X86_FORM_MOV_R32_IMM32,
        },
        {
            .form = X86_FORM_RET,
        },
    };
    const LinkReference main_references[] = {
        {
            .instruction = 1,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_PC32,
            .target = program->handler,
            .addend = 0,
        },
        {
            .instruction = 2,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_CALL,
            .target = program->atexit,
            .addend = 0,
        },
    };
    // handler: exit_group(0).
    const X86Instruction handler_code[] = {
        {
            .immediate = 231,
            .rm = X86_REGISTER_RAX,
            .form = X86_FORM_MOV_R32_IMM32,
        },
        {
            .immediate = 0,
            .rm = X86_REGISTER_RDI,
            .form = X86_FORM_MOV_R32_IMM32,
        },
        {
            .form = X86_FORM_SYSCALL,
        },
    };
    built = built && bind_test_code(test, main_code, BUSTER_ARRAY_LENGTH(main_code), main_references, BUSTER_ARRAY_LENGTH(main_references), program->main) &&
            bind_test_code(test, handler_code, BUSTER_ARRAY_LENGTH(handler_code), 0, 0, program->handler);
    program->bound = built && (!bound || bind_end(&test->bind, &test->link)) && link_check(&test->link) == LINK_ERROR_NONE;
}

// The index of the runtime's first member named `name` (libc_nonshared.a's, or the static runtimes' archives'), or the
// member count.
static uint32_t bind_test_member(const Runtime* runtime, const char* name)
{
    uint32_t result = runtime->member_count;
    for (uint32_t m = 0; m < runtime->member_count; m += 1)
    {
        result = result == runtime->member_count && import_equal(runtime_member_name(runtime, m), name) ? m : result;
    }
    return result;
}

static void bind_test_atexit_checks(BindTestTally* tally, const BindTestAtexit* program, const Runtime* runtime)
{
    const Link* link = &program->test.link;
    const LinkSymbols* symbols = &link->symbols;
    const Bind* bind = &program->test.bind;
    const uint32_t member = bind_test_member(runtime, "atexit.oS");
    const AtomId atom = symbols->atom[program->atexit];
    const bool joined = member < runtime->member_count && (bind->members_joined[member / 64] >> (member % 64) & 1) != 0;
    bind_test_expect(tally, program->bound, "the atexit program bound", bind->error);
    bind_test_expect(tally, joined, "atexit.oS joined", member);
    bind_test_expect(tally, atom != 0 && link->atoms.unit[atom] == 1 + RUNTIME_OBJECTS + member && symbols->source[program->atexit] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE,
                     "atexit defined by its member", atom);
    const SymbolId cxa_atexit = bind_test_global(link, "__cxa_atexit");
    bind_test_expect(tally, cxa_atexit != 0 && symbols->source[cxa_atexit] == BUSTER_LINK_SOURCE_RUNTIME_SHARED &&
                                import_equal(import_test_runtime_version_name(runtime, symbols->version[cxa_atexit]), "GLIBC_2.2.5"),
                     "__cxa_atexit imported at GLIBC_2.2.5", cxa_atexit);
    // The member's fixup against __dso_handle targets crtbeginS.o's definition.
    const SymbolId dso = bind_test_global(link, "__dso_handle");
    bool dso_bound = false;
    for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
    {
        dso_bound = dso_bound || (link->fixups.target[fixup] == dso && link->atoms.unit[symbols->atom[dso]] == 1 + RUNTIME_OBJECT_BEGIN);
    }
    bind_test_expect(tally, dso != 0 && dso_bound, "the member's __dso_handle bound to crtbeginS.o's", dso);
}

static bool bind_test_atexit_program(const Runtime* runtime)
{
    BindTestTally tally = {
        .check = "atexit",
    };
    BindTestAtexit vector;
    BindTestAtexit exact;
    bind_test_atexit(&vector, runtime, BIND_PATH_VECTOR, true);
    bind_test_atexit(&exact, runtime, BIND_PATH_EXACT, true);
    bind_test_atexit_checks(&tally, &vector, runtime);
    bind_test_expect(&tally, exact.bound == vector.bound && bind_test_same_links(&vector.test.link, &exact.test.link) && bind_test_same_binds(&vector.test.bind, &exact.test.bind),
                     "the paths agree", 0);
    bind_test_link_end(&vector.test);
    bind_test_link_end(&exact.test);
    return bind_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The cross-check

// Writes the link's objects of `unit_count` units into `directory`, named after `name`, each path into `paths` (room
// for unit_count).
static bool bind_test_write_objects(const Link* link, uint32_t unit_count, const char* directory, const char* name, char** paths, Arena* arena)
{
    const uint64_t length = arena->length;
    ObjectUnits units;
    ObjectSymbols symbols;
    bool result = object_prepare(&units, link, unit_count, arena) && object_map_symbols(&symbols, link, arena);
    for (uint32_t unit = 0; result && unit < unit_count; unit += 1)
    {
        snprintf(paths[unit], BUSTER_BIND_TEST_PATH_BYTES, "%s/%s_u%u.o", directory, name, unit);
        const uint64_t before = arena->length;
        char source[32];
        const ObjectOptions options = { 0 };
        const Object object = object_build(link, &units, &symbols, unit, options, object_test_source_name(source, sizeof(source), unit), arena);
        result = object.error == OBJECT_ERROR_NONE && object_write_file(paths[unit], object.bytes, object.size) == OBJECT_ERROR_NONE;
        arena_pop_to(arena, before);
    }
    arena_pop_to(arena, length);
    return result;
}

// The arguments of a link of the bound program by `linker` (ld.lld or ld.bfd): the crt objects before the program and
// its members after it, crtendS.o and crtn.o last, with libc.so.6 and the loader. `units` has the program's unit count.
static uint32_t bind_test_link_arguments(char** arguments, const char* linker, const Runtime* runtime, const Bind* bind, char** paths, uint32_t units, const char* output)
{
    // A shared library's runtime has no start object, and its link no interpreter, as GCC's driver links -shared.
    const bool library = runtime->kind == RUNTIME_KIND_SHARED;
    uint32_t count = 0;
    arguments[count++] = (char*)linker;
    arguments[count++] = library ? "-shared" : "-pie";
    if (!library)
    {
        arguments[count++] = "--dynamic-linker";
        arguments[count++] = (char*)runtime->loader_path.pointer;
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    if (!library)
    {
        arguments[count++] = paths[units + RUNTIME_OBJECT_START];
    }
    arguments[count++] = paths[units + RUNTIME_OBJECT_INIT_PROLOGUE];
    arguments[count++] = paths[units + RUNTIME_OBJECT_BEGIN];
    for (uint32_t unit = 0; unit < units; unit += 1)
    {
        arguments[count++] = paths[unit];
    }
    for (uint32_t m = 0; m < runtime->member_count; m += 1)
    {
        if ((bind->members_joined[m / 64] >> (m % 64) & 1) != 0)
        {
            arguments[count++] = paths[units + RUNTIME_OBJECTS + m];
        }
    }
    arguments[count++] = (char*)runtime->libc_path.pointer;
    // The loader, as needed, as libc.so's script names it.
    arguments[count++] = "--as-needed";
    arguments[count++] = (char*)runtime->loader_path.pointer;
    arguments[count++] = "--no-as-needed";
    arguments[count++] = paths[units + RUNTIME_OBJECT_END];
    arguments[count++] = paths[units + RUNTIME_OBJECT_INIT_EPILOGUE];
    // As GCC's driver asks of the linker for a dynamic executable.
    arguments[count++] = "--eh-frame-hdr";
    arguments[count] = 0;
    return count;
}

// Links the bound link's objects by ld.lld and ld.bfd, without errors or warnings, and runs the program: it must exit
// with `status` having printed `expected` (`expected_length` bytes).
static void bind_test_link_and_run(BindTestTally* tally, const Link* link, const Bind* bind, const Runtime* runtime, uint32_t units, const char* directory, const char* name,
                                   const uint8_t* expected, uint64_t expected_length, int status, bool keep, Arena* arena)
{
    static const char* const linkers[] = {
        "ld.lld",
        "ld.bfd",
    };
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
    written = written && bind_test_write_objects(link, unit_count, directory, name, paths, arena);
    bind_test_expect(tally, written, "the objects written", 0);
    char program_path[BUSTER_BIND_TEST_PATH_BYTES];
    char output_path[BUSTER_BIND_TEST_PATH_BYTES];
    char error_path[BUSTER_BIND_TEST_PATH_BYTES];
    snprintf(program_path, sizeof(program_path), "%s/%s", directory, name);
    snprintf(output_path, sizeof(output_path), "%s/%s.out", directory, name);
    snprintf(error_path, sizeof(error_path), "%s/%s.err", directory, name);
    for (uint32_t l = 0; written && l < BUSTER_ARRAY_LENGTH(linkers); l += 1)
    {
        bind_test_link_arguments(arguments, linkers[l], runtime, bind, paths, units, program_path);
        unlink(program_path);
        const int link_status = object_test_run(arguments, output_path, error_path);
        const uint64_t before = arena->length;
        const String8 errors = object_test_read_file(arena, error_path);
        const bool clean = link_status == 0 && errors.pointer != 0 && errors.length == 0;
        if (!clean)
        {
            printf("  %s: %s by %s: status %d: %.*s\n", tally->check, name, linkers[l], link_status, errors.pointer != 0 ? (int)(errors.length < 600 ? errors.length : 600) : 0,
                   errors.pointer != 0 ? errors.pointer : "");
        }
        bind_test_expect(tally, clean, "linked without errors or warnings", l);
        char* run[] = {
            program_path,
            0,
        };
        const int run_status = link_status == 0 ? object_test_run(run, output_path, error_path) : -1;
        const String8 output = link_status == 0 ? object_test_read_file(arena, output_path) : (String8){ 0 };
        const bool ran = run_status == status && output.pointer != 0 && output.length == expected_length && memcmp(output.pointer, expected, expected_length) == 0;
        if (link_status == 0 && !ran)
        {
            printf("  %s: %s linked by %s: status %d, output \"%.*s\"\n", tally->check, name, linkers[l], run_status,
                   output.pointer != 0 ? (int)(output.length < 64 ? output.length : 64) : 0, output.pointer != 0 ? output.pointer : "");
        }
        bind_test_expect(tally, ran, "runs as its model predicts", l);
        arena_pop_to(arena, before);
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

// A .dynsym entry of readelf's: its name, its version (empty for none), its type, and whether it is undefined and weak.
typedef struct BindTestDynamic BindTestDynamic;
struct BindTestDynamic
{
    char name[256];
    char version[64];
    uint8_t type;
    bool undefined;
    bool weak;
};

#define BUSTER_BIND_TEST_DYNAMIC 256

// readelf -W --dyn-syms's entries but the first, into `entries`: how many, or UINT32_MAX for output it can't read.
static uint32_t bind_test_read_dynamic(String8 text, BindTestDynamic* entries)
{
    uint32_t count = 0;
    bool valid = true;
    const char* line = text.pointer;
    const char* end = text.pointer + text.length;
    while (valid && line < end)
    {
        const char* next = memchr(line, '\n', (uint64_t)(end - line));
        next = next != 0 ? next : end;
        char buffer[512];
        const uint64_t line_length = (uint64_t)(next - line) < sizeof(buffer) - 1 ? (uint64_t)(next - line) : sizeof(buffer) - 1;
        memcpy(buffer, line, line_length);
        buffer[line_length] = 0;
        unsigned number = 0;
        char value[32];
        unsigned long long size = 0;
        char type[32];
        char binding[32];
        char visibility[32];
        char section[32];
        char name[320] = "";
        const int fields = sscanf(buffer, "%u: %31s %llu %31s %31s %31s %31s %319s", &number, value, &size, type, binding, visibility, section, name);
        if (fields >= 7 && number != 0 && count < BUSTER_BIND_TEST_DYNAMIC)
        {
            BindTestDynamic* entry = &entries[count];
            // name@VERSION for a reference, name@@VERSION for a definition, and readelf may add " (N)" after it.
            char* at = strchr(name, '@');
            entry->version[0] = 0;
            if (at != 0)
            {
                const char* version = at[1] == '@' ? at + 2 : at + 1;
                snprintf(entry->version, sizeof(entry->version), "%s", version);
                *at = 0;
            }
            snprintf(entry->name, sizeof(entry->name), "%s", name);
            entry->undefined = strcmp(section, "UND") == 0;
            entry->weak = strcmp(binding, "WEAK") == 0;
            entry->type = strcmp(type, "FUNC") == 0     ? BUSTER_LINK_SYMBOL_FUNCTION
                          : strcmp(type, "OBJECT") == 0 ? BUSTER_LINK_SYMBOL_OBJECT
                          : strcmp(type, "TLS") == 0    ? BUSTER_LINK_SYMBOL_TLS
                                                        : BUSTER_LINK_SYMBOL_NOTYPE;
            count += 1;
        }
        valid = count < BUSTER_BIND_TEST_DYNAMIC;
        line = next + 1;
    }
    return valid ? count : UINT32_MAX;
}

// Whether readelf's entries are what binding found: each undefined one an import of the link at that version, or a weak
// reference to nothing of it with default visibility, unversioned, either with the same binding and type; each defined
// one an export; and every import, export and such weak reference among them.
static bool bind_test_same_dynamic(const Link* link, const Bind* bind, const Runtime* runtime, const BindTestDynamic* entries, uint32_t count)
{
    const LinkSymbols* symbols = &link->symbols;
    bool result = count != UINT32_MAX;
    uint32_t expected = 0;
    for (SymbolId symbol = 1; result && symbol < symbols->count; symbol += 1)
    {
        const NameId name = symbols->name[symbol];
        const uint8_t source = symbols->source[symbol];
        const bool weak = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
        const bool import = source == BUSTER_LINK_SOURCE_RUNTIME_SHARED;
        // A weak reference to nothing is dynamic only with default visibility: a hidden one is 0 within the executable.
        // In a shared library, so is a strong one, which binding leaves undefined.
        const bool visible = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_DEFAULT;
        const bool global = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
        const bool nowhere =
            source == BUSTER_LINK_SOURCE_NONE && symbols->atom[symbol] == 0 && name != 0 && global && (weak || runtime->kind == RUNTIME_KIND_SHARED) && visible;
        bool exported = false;
        for (uint32_t i = 0; i < bind->export_count; i += 1)
        {
            exported = exported || bind->exports[i] == symbol;
        }
        if (import || nowhere || exported)
        {
            const String8 version = import ? import_test_runtime_version_name(runtime, symbols->version[symbol]) : (String8){ 0 };
            bool found = false;
            for (uint32_t e = 0; !found && e < count; e += 1)
            {
                const BindTestDynamic* entry = &entries[e];
                found = bind_test_named(link, name, entry->name) && entry->undefined == !exported && (exported || entry->weak == weak) &&
                        (exported || entry->type == (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_TYPE)) && (!import || import_equal(version, entry->version)) &&
                        (!nowhere || entry->version[0] == 0);
            }
            if (!found)
            {
                printf("  cross-check: %.*s isn't in lld's .dynsym as binding found it\n", (int)link->names.length[name], link->names.bytes[name]);
            }
            result = found;
            expected += 1;
        }
    }
    if (result && expected != count)
    {
        for (uint32_t e = 0; e < count; e += 1)
        {
            printf("  cross-check: lld's .dynsym: %s@%s%s%s\n", entries[e].name, entries[e].version, entries[e].undefined ? " undefined" : "", entries[e].weak ? " weak" : "");
        }
    }
    return result && expected == count;
}

// The members lld's --why-extract lists (lines of "reference\textracted\tsymbol", the member as archive(member)), each
// of libc_nonshared.a, against those that joined.
static bool bind_test_same_members(String8 text, const Bind* bind, const Runtime* runtime)
{
    const uint32_t words = (runtime->member_count + 63) / 64;
    uint64_t extracted[4] = { 0 };
    bool result = words <= BUSTER_ARRAY_LENGTH(extracted) && text.pointer != 0;
    for (uint32_t m = 0; result && m < runtime->member_count; m += 1)
    {
        char needle[512];
        const String8 member = runtime_member_name(runtime, m);
        snprintf(needle, sizeof(needle), "libc_nonshared.a(%.*s)", (int)member.length, member.pointer);
        if (strstr(text.pointer, needle) != 0)
        {
            extracted[m / 64] |= 1ull << (m % 64);
        }
    }
    for (uint32_t w = 0; result && w < words; w += 1)
    {
        result = extracted[w] == bind->members_joined[w];
    }
    return result;
}

// Links the program's own objects by cc with lld, as `output` says (-pie, or -shared for a library bound against a shared
// library's runtime), and compares the output's .dynsym and the members lld extracts with what binding found for the
// same program.
static void bind_test_against_lld_as(BindTestTally* tally, const Link* own, uint32_t units, const Link* bound, const Bind* bind, const Runtime* runtime,
                                     const char* directory, const char* name, const char* output, bool keep, Arena* arena)
{
    const uint64_t length = arena->length;
    char** paths = BUSTER_ARENA_PUSH_ARRAY(arena, char*, units);
    char* path_bytes = arena_push(arena, (uint64_t)units * BUSTER_BIND_TEST_PATH_BYTES, 1);
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)units + 8);
    BindTestDynamic* entries = BUSTER_ARENA_PUSH_ARRAY(arena, BindTestDynamic, BUSTER_BIND_TEST_DYNAMIC);
    bool written = paths != 0 && path_bytes != 0 && arguments != 0 && entries != 0;
    for (uint32_t unit = 0; written && unit < units; unit += 1)
    {
        paths[unit] = path_bytes + (uint64_t)unit * BUSTER_BIND_TEST_PATH_BYTES;
    }
    char reference_name[BUSTER_BIND_TEST_PATH_BYTES];
    snprintf(reference_name, sizeof(reference_name), "%s-lld", name);
    written = written && bind_test_write_objects(own, units, directory, reference_name, paths, arena);
    char program_path[BUSTER_BIND_TEST_PATH_BYTES];
    char why_path[BUSTER_BIND_TEST_PATH_BYTES];
    char why_option[BUSTER_BIND_TEST_PATH_BYTES + 32];
    char output_path[BUSTER_BIND_TEST_PATH_BYTES];
    char error_path[BUSTER_BIND_TEST_PATH_BYTES];
    snprintf(program_path, sizeof(program_path), "%s/%s", directory, reference_name);
    snprintf(why_path, sizeof(why_path), "%s/%s.why", directory, reference_name);
    snprintf(why_option, sizeof(why_option), "-Wl,--why-extract=%s", why_path);
    snprintf(output_path, sizeof(output_path), "%s/%s.out", directory, reference_name);
    snprintf(error_path, sizeof(error_path), "%s/%s.err", directory, reference_name);
    uint32_t count = 0;
    if (written)
    {
        arguments[count++] = "cc";
        arguments[count++] = "-fuse-ld=lld";
        arguments[count++] = (char*)output;
        arguments[count++] = why_option;
        arguments[count++] = "-o";
        arguments[count++] = program_path;
        for (uint32_t unit = 0; unit < units; unit += 1)
        {
            arguments[count++] = paths[unit];
        }
        arguments[count] = 0;
    }
    const int link_status = written ? object_test_run(arguments, output_path, error_path) : -1;
    bind_test_expect(tally, link_status == 0, "the program's own objects linked by cc with lld", (uint64_t)link_status);
    char* readelf[] = {
        "readelf",
        "-W",
        "--dyn-syms",
        program_path,
        0,
    };
    const int read_status = link_status == 0 ? object_test_run(readelf, output_path, error_path) : -1;
    const String8 text = read_status == 0 ? object_test_read_file(arena, output_path) : (String8){ 0 };
    const uint32_t entry_count = text.pointer != 0 ? bind_test_read_dynamic(text, entries) : UINT32_MAX;
    bind_test_expect(tally, bind_test_same_dynamic(bound, bind, runtime, entries, entry_count), "lld's .dynsym: the imports, weak references and exports", entry_count);
    const String8 why = link_status == 0 ? object_test_read_file(arena, why_path) : (String8){ 0 };
    bind_test_expect(tally, bind_test_same_members(why, bind, runtime), "the members lld extracts", 0);
    if (!keep)
    {
        for (uint32_t unit = 0; written && unit < units; unit += 1)
        {
            unlink(paths[unit]);
        }
        unlink(program_path);
        unlink(why_path);
        unlink(output_path);
        unlink(error_path);
    }
    arena_pop_to(arena, length);
}

// bind_test_against_lld_as for a PIE.
static void bind_test_against_lld(BindTestTally* tally, const Link* own, uint32_t units, const Link* bound, const Bind* bind, const Runtime* runtime, const char* directory,
                                  const char* name, bool keep, Arena* arena)
{
    bind_test_against_lld_as(tally, own, units, bound, bind, runtime, directory, name, "-pie", keep, arena);
}

// ---------------------------------------------------------------------------------------------------------------------
// Programs as GNU ld and lld link them
//
// Programs whose names bind by the rules that GNU ld and lld decide for themselves: a weak definition of the program's
// meeting the runtime's (strong, and weak before it in link order), references with a visibility other than default to
// a name only libc.so.6 defines, references to the names the linker defines, and to names only the loader exports,
// which make it needed. Each is bound by both paths, which must
// agree, and must bind or fail as `links` says. With --cross-check, the program's own objects, linked by cc as a PIE
// with GNU ld and with lld, must link or fail to alike and run with `status`; lld's .dynsym and --why-extract must have
// what binding found (bind_test_against_lld); and the bound link's objects, linked by ld.lld and ld.bfd, must run with
// `status` too.

typedef enum BindTestCase
{
    BIND_TEST_CASE_WEAK_INIT,
    BIND_TEST_CASE_WEAK_DATA_START,
    BIND_TEST_CASE_HIDDEN_PUTS,
    BIND_TEST_CASE_WEAK_HIDDEN_PUTS,
    BIND_TEST_CASE_ARRAY_BOUNDS,
    BIND_TEST_CASE_LOADER,
    BIND_TEST_CASE_WEAK_LOADER,
    BIND_TEST_CASE_MIXED_LOADER,
    BIND_TEST_CASES,
} BindTestCase;

typedef struct BindTestCaseInfo BindTestCaseInfo;
struct BindTestCaseInfo
{
    // The files' name, and what the program is.
    const char* name;
    const char* what;
    // Whether it links, and its exit status if it does.
    bool links;
    int status;
};

static const BindTestCaseInfo bind_test_cases[BIND_TEST_CASES] = {
    [BIND_TEST_CASE_WEAK_INIT] = {
        .name = "weak-init",
        .what = "a weak _init of the program's, which exits with 7 if it runs, against crti.o's strong one",
        .links = true,
        .status = 0,
    },
    [BIND_TEST_CASE_WEAK_DATA_START] = {
        .name = "weak-data-start",
        .what = "main returns data_start's value: the program's weak one holds 7, and Scrt1.o's, before it, 0",
        .links = true,
        .status = 0,
    },
    [BIND_TEST_CASE_HIDDEN_PUTS] = {
        .name = "hidden-puts",
        .what = "a call through the GOT to a hidden puts, which only libc.so.6 defines",
        .links = false,
    },
    [BIND_TEST_CASE_WEAK_HIDDEN_PUTS] = {
        .name = "weak-hidden-puts",
        .what = "main returns whether a weak hidden puts, read through the GOT, isn't 0",
        .links = true,
        .status = 0,
    },
    [BIND_TEST_CASE_ARRAY_BOUNDS] = {
        .name = "array-bounds",
        .what = "main refers to the init and fini arrays' bounds",
        .links = true,
        .status = 0,
    },
    [BIND_TEST_CASE_LOADER] = {
        .name = "loader",
        .what = "main returns _r_debug's r_version (1), plus 10 if __libc_stack_end isn't 0: names only the loader exports, read through the GOT",
        .links = true,
        .status = 11,
    },
    [BIND_TEST_CASE_WEAK_LOADER] = {
        .name = "weak-loader",
        .what = "the loader's case with both references weak: the loader isn't needed, and both stay weak references to nothing, unversioned",
        .links = true,
        .status = 11,
    },
    [BIND_TEST_CASE_MIXED_LOADER] = {
        .name = "mixed-loader",
        .what = "the loader's case with __libc_stack_end's reference weak: _r_debug's needs the loader, and both are imports at their versions",
        .links = true,
        .status = 11,
    },
};

typedef struct BindTestCaseProgram BindTestCaseProgram;
struct BindTestCaseProgram
{
    BindTestLink test;
    bool built;
    bool bound;
};

// The case's code: main, and for a weak _init its own.
static bool bind_test_case_code(BindTestLink* test, BindTestCase which, SymbolId main, const SymbolId* others)
{
    const X86Instruction return_zero[] = {
        {
            .immediate = 0,
            .rm = X86_REGISTER_RAX,
            .form = X86_FORM_MOV_R32_IMM32,
        },
        {
            .form = X86_FORM_RET,
        },
    };
    bool result = false;
    if (which == BIND_TEST_CASE_WEAK_INIT)
    {
        // _init: exit_group(7).
        const X86Instruction init_code[] = {
            {
                .immediate = 231,
                .rm = X86_REGISTER_RAX,
                .form = X86_FORM_MOV_R32_IMM32,
            },
            {
                .immediate = 7,
                .rm = X86_REGISTER_RDI,
                .form = X86_FORM_MOV_R32_IMM32,
            },
            {
                .form = X86_FORM_SYSCALL,
            },
        };
        result = bind_test_code(test, return_zero, BUSTER_ARRAY_LENGTH(return_zero), 0, 0, main) &&
                 bind_test_code(test, init_code, BUSTER_ARRAY_LENGTH(init_code), 0, 0, others[0]);
    }
    else if (which == BIND_TEST_CASE_WEAK_DATA_START)
    {
        // main: mov eax, [rip + data_start]; ret. The program's data_start holds 7.
        const X86Instruction main_code[] = {
            {
                .reg = X86_REGISTER_RAX,
                .mode = BUSTER_X86_MEMORY_RIP,
                .form = X86_FORM_MOV_R32_RM32 | BUSTER_X86_FORM_FIXUP,
            },
            {
                .form = X86_FORM_RET,
            },
        };
        const LinkReference references[] = {
            {
                .instruction = 0,
                .field = X86_FIXUP_DISPLACEMENT,
                .kind = BUSTER_LINK_FIXUP_PC32,
                .target = others[0],
                .addend = 0,
            },
        };
        const uint64_t seven = 7;
        result = bind_test_code(test, main_code, BUSTER_ARRAY_LENGTH(main_code), references, BUSTER_ARRAY_LENGTH(references), main) && bind_test_define(test, others[0]);
        if (result)
        {
            memcpy(test->link.atoms.bytes[test->link.symbols.atom[others[0]]], &seven, sizeof(seven));
        }
    }
    else if (which == BIND_TEST_CASE_HIDDEN_PUTS)
    {
        // main: sub rsp, 8; call [rip + puts@GOT]; add rsp, 8; mov eax, 0; ret.
        const X86Instruction main_code[] = {
            {
                .immediate = 8,
                .rm = X86_REGISTER_RSP,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .form = X86_FORM_SUB_RM64_IMM8,
            },
            {
                .mode = BUSTER_X86_MEMORY_RIP,
                .form = X86_FORM_CALL_RM64 | BUSTER_X86_FORM_FIXUP,
            },
            {
                .immediate = 8,
                .rm = X86_REGISTER_RSP,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .form = X86_FORM_ADD_RM64_IMM8,
            },
            {
                .immediate = 0,
                .rm = X86_REGISTER_RAX,
                .form = X86_FORM_MOV_R32_IMM32,
            },
            {
                .form = X86_FORM_RET,
            },
        };
        const LinkReference references[] = {
            {
                .instruction = 1,
                .field = X86_FIXUP_DISPLACEMENT,
                .kind = BUSTER_LINK_FIXUP_CALL_GOT,
                .target = others[0],
                .addend = 0,
            },
        };
        result = bind_test_code(test, main_code, BUSTER_ARRAY_LENGTH(main_code), references, BUSTER_ARRAY_LENGTH(references), main);
    }
    else if (which == BIND_TEST_CASE_WEAK_HIDDEN_PUTS)
    {
        // main: mov rdx, [rip + puts@GOT]; mov eax, 1; mov ecx, 0; test rdx, rdx; cmove rax, rcx; ret.
        const X86Instruction main_code[] = {
            {
                .reg = X86_REGISTER_RDX,
                .mode = BUSTER_X86_MEMORY_RIP,
                .form = X86_FORM_MOV_R64_RM64 | BUSTER_X86_FORM_FIXUP,
            },
            {
                .immediate = 1,
                .rm = X86_REGISTER_RAX,
                .form = X86_FORM_MOV_R32_IMM32,
            },
            {
                .immediate = 0,
                .rm = X86_REGISTER_RCX,
                .form = X86_FORM_MOV_R32_IMM32,
            },
            {
                .reg = X86_REGISTER_RDX,
                .rm = X86_REGISTER_RDX,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .form = X86_FORM_TEST_RM64_R64,
            },
            {
                .reg = X86_REGISTER_RAX,
                .rm = X86_REGISTER_RCX,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .form = X86_FORM_CMOVE_R64_RM64,
            },
            {
                .form = X86_FORM_RET,
            },
        };
        const LinkReference references[] = {
            {
                .instruction = 0,
                .field = X86_FIXUP_DISPLACEMENT,
                .kind = BUSTER_LINK_FIXUP_LOAD_GOT,
                .target = others[0],
                .addend = 0,
            },
        };
        result = bind_test_code(test, main_code, BUSTER_ARRAY_LENGTH(main_code), references, BUSTER_ARRAY_LENGTH(references), main);
    }
    else if (which == BIND_TEST_CASE_LOADER || which == BIND_TEST_CASE_WEAK_LOADER || which == BIND_TEST_CASE_MIXED_LOADER)
    {
        // main: mov rax, [rip + _r_debug@GOT]; mov eax, [rax]; mov rcx, [rip + __libc_stack_end@GOT]; mov rcx, [rcx];
        // mov edx, 10; mov esi, 0; test rcx, rcx; cmove rdx, rsi; add eax, edx; ret.
        const X86Instruction main_code[] = {
            {
                .reg = X86_REGISTER_RAX,
                .mode = BUSTER_X86_MEMORY_RIP,
                .form = X86_FORM_MOV_R64_RM64 | BUSTER_X86_FORM_FIXUP,
            },
            {
                .reg = X86_REGISTER_RAX,
                .rm = X86_REGISTER_RAX,
                .mode = BUSTER_X86_MEMORY_BASE,
                .form = X86_FORM_MOV_R32_RM32,
            },
            {
                .reg = X86_REGISTER_RCX,
                .mode = BUSTER_X86_MEMORY_RIP,
                .form = X86_FORM_MOV_R64_RM64 | BUSTER_X86_FORM_FIXUP,
            },
            {
                .reg = X86_REGISTER_RCX,
                .rm = X86_REGISTER_RCX,
                .mode = BUSTER_X86_MEMORY_BASE,
                .form = X86_FORM_MOV_R64_RM64,
            },
            {
                .immediate = 10,
                .rm = X86_REGISTER_RDX,
                .form = X86_FORM_MOV_R32_IMM32,
            },
            {
                .immediate = 0,
                .rm = X86_REGISTER_RSI,
                .form = X86_FORM_MOV_R32_IMM32,
            },
            {
                .reg = X86_REGISTER_RCX,
                .rm = X86_REGISTER_RCX,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .form = X86_FORM_TEST_RM64_R64,
            },
            {
                .reg = X86_REGISTER_RDX,
                .rm = X86_REGISTER_RSI,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .form = X86_FORM_CMOVE_R64_RM64,
            },
            {
                .reg = X86_REGISTER_RDX,
                .rm = X86_REGISTER_RAX,
                .mode = BUSTER_X86_REGISTER_DIRECT,
                .form = X86_FORM_ADD_RM32_R32,
            },
            {
                .form = X86_FORM_RET,
            },
        };
        const LinkReference references[] = {
            {
                .instruction = 0,
                .field = X86_FIXUP_DISPLACEMENT,
                .kind = BUSTER_LINK_FIXUP_LOAD_GOT,
                .target = others[0],
                .addend = 0,
            },
            {
                .instruction = 2,
                .field = X86_FIXUP_DISPLACEMENT,
                .kind = BUSTER_LINK_FIXUP_LOAD_GOT,
                .target = others[1],
                .addend = 0,
            },
        };
        result = bind_test_code(test, main_code, BUSTER_ARRAY_LENGTH(main_code), references, BUSTER_ARRAY_LENGTH(references), main);
    }
    else
    {
        // main: lea rax, [rip + each bound]; mov eax, 0; ret.
        X86Instruction main_code[6];
        LinkReference references[4];
        for (uint32_t i = 0; i < 4; i += 1)
        {
            main_code[i] = (X86Instruction){
                .reg = X86_REGISTER_RAX,
                .mode = BUSTER_X86_MEMORY_RIP,
                .form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP,
            };
            references[i] = (LinkReference){
                .instruction = i,
                .field = X86_FIXUP_DISPLACEMENT,
                .kind = BUSTER_LINK_FIXUP_PC32,
                .target = others[i],
                .addend = 0,
            };
        }
        main_code[4] = return_zero[0];
        main_code[5] = return_zero[1];
        result = bind_test_code(test, main_code, BUSTER_ARRAY_LENGTH(main_code), references, BUSTER_ARRAY_LENGTH(references), main);
    }
    return result;
}

// The case's program: main and the case's symbols, then with `bound` the runtime's first objects, its code, and the
// rest; without, only the program, for a link of its own objects by cc. program->bound also needs link_check to accept
// the link, which the object writer needs of the links the cross-check writes.
static void bind_test_case_program(BindTestCaseProgram* program, const Runtime* runtime, BindTestCase which, BindPath path, bool bound)
{
    static const char* const bounds[] = {
        "__init_array_start",
        "__init_array_end",
        "__fini_array_start",
        "__fini_array_end",
    };
    const uint8_t reference = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE;
    const uint8_t program_source = BUSTER_LINK_SOURCE_PROGRAM;
    BindTestLink* test = &program->test;
    bool built = bind_test_link_begin(test, runtime, bind_test_room(runtime));
    const SymbolId main = built ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, program_source) : 0;
    SymbolId others[4] = { 0 };
    uint32_t other_count = 1;
    if (built && which == BIND_TEST_CASE_WEAK_INIT)
    {
        others[0] = bind_test_symbol(test, "_init", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION, program_source);
    }
    else if (built && which == BIND_TEST_CASE_WEAK_DATA_START)
    {
        others[0] = bind_test_symbol(test, "data_start", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_OBJECT, program_source);
    }
    else if (built && which == BIND_TEST_CASE_HIDDEN_PUTS)
    {
        others[0] = bind_test_symbol(test, "puts", reference | BUSTER_LINK_SYMBOL_HIDDEN, BUSTER_LINK_SOURCE_NONE);
    }
    else if (built && which == BIND_TEST_CASE_WEAK_HIDDEN_PUTS)
    {
        others[0] = bind_test_symbol(test, "puts", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_NOTYPE | BUSTER_LINK_SYMBOL_HIDDEN, BUSTER_LINK_SOURCE_NONE);
    }
    else if (built && (which == BIND_TEST_CASE_LOADER || which == BIND_TEST_CASE_WEAK_LOADER || which == BIND_TEST_CASE_MIXED_LOADER))
    {
        const uint8_t weak = BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_NOTYPE;
        other_count = 2;
        others[0] = bind_test_symbol(test, "_r_debug", which == BIND_TEST_CASE_WEAK_LOADER ? weak : reference, BUSTER_LINK_SOURCE_NONE);
        others[1] = bind_test_symbol(test, "__libc_stack_end", which == BIND_TEST_CASE_LOADER ? reference : weak, BUSTER_LINK_SOURCE_NONE);
    }
    else if (built)
    {
        other_count = BUSTER_ARRAY_LENGTH(bounds);
        for (uint32_t i = 0; i < other_count; i += 1)
        {
            others[i] = bind_test_symbol(test, bounds[i], reference, BUSTER_LINK_SOURCE_NONE);
        }
    }
    built = main != 0;
    for (uint32_t i = 0; i < other_count; i += 1)
    {
        built = built && others[i] != 0;
    }
    built = built && (!bound || bind_begin(&test->bind, &test->link, runtime, path, &test->bytes, 1, &test->arena));
    program->built = built && bind_test_case_code(test, which, main, others);
    program->bound = program->built && (!bound || bind_end(&test->bind, &test->link)) && link_check(&test->link) == LINK_ERROR_NONE;
}

// Links the case's own objects by cc as a PIE, with GNU ld and with lld: each must link or fail as the case says, and
// what links must run with its status.
static void bind_test_case_linkers(BindTestTally* tally, BindTestCase which, const Link* own, const char* directory, bool keep, Arena* arena)
{
    static const char* const linkers[] = {
        "-fuse-ld=bfd",
        "-fuse-ld=lld",
    };
    const BindTestCaseInfo* info = &bind_test_cases[which];
    const uint64_t length = arena->length;
    char object_name[BUSTER_BIND_TEST_PATH_BYTES];
    char object_path[BUSTER_BIND_TEST_PATH_BYTES];
    char* paths[] = {
        object_path,
    };
    snprintf(object_name, sizeof(object_name), "%s-cc", info->name);
    const bool written = bind_test_write_objects(own, 1, directory, object_name, paths, arena);
    bind_test_expect(tally, written, "the program's own objects written", which);
    for (uint32_t l = 0; written && l < BUSTER_ARRAY_LENGTH(linkers); l += 1)
    {
        char program_path[BUSTER_BIND_TEST_PATH_BYTES];
        char output_path[BUSTER_BIND_TEST_PATH_BYTES];
        char error_path[BUSTER_BIND_TEST_PATH_BYTES];
        snprintf(program_path, sizeof(program_path), "%s/%s-cc-%s", directory, info->name, linkers[l] + 9);
        snprintf(output_path, sizeof(output_path), "%s.out", program_path);
        snprintf(error_path, sizeof(error_path), "%s.err", program_path);
        char* arguments[] = {
            "cc",
            (char*)linkers[l],
            "-pie",
            "-o",
            program_path,
            object_path,
            0,
        };
        unlink(program_path);
        const int link_status = object_test_run(arguments, output_path, error_path);
        const uint64_t before = arena->length;
        if ((link_status == 0) != info->links)
        {
            const String8 errors = object_test_read_file(arena, error_path);
            printf("  %s: %s by cc %s: status %d: %.*s\n", tally->check, info->name, linkers[l], link_status,
                   errors.pointer != 0 ? (int)(errors.length < 600 ? errors.length : 600) : 0, errors.pointer != 0 ? errors.pointer : "");
        }
        bind_test_expect(tally, (link_status == 0) == info->links, info->links ? "the program's own objects linked by cc" : "the program's own objects refused by cc", l);
        char* run[] = {
            program_path,
            0,
        };
        const int run_status = link_status == 0 ? object_test_run(run, output_path, error_path) : -1;
        if (link_status == 0 && run_status != info->status)
        {
            printf("  %s: %s linked by cc %s: status %d\n", tally->check, info->name, linkers[l], run_status);
        }
        bind_test_expect(tally, link_status != 0 || run_status == info->status, "the program linked by cc runs with its status", l);
        arena_pop_to(arena, before);
        if (!keep)
        {
            unlink(program_path);
            unlink(output_path);
            unlink(error_path);
        }
    }
    if (!keep && written)
    {
        unlink(object_path);
    }
    arena_pop_to(arena, length);
}

// How a loader case's two references bound: with a strong one, both imports at the loader's versions, the weak one
// weak; with weak ones only, both left to nothing, unversioned, and no import the loader's (GNU ld and lld need a
// library AS_NEEDED only for a strong reference). Either way, of the loader's exports' type, OBJECT, which GNU ld and lld
// give them over the references' NOTYPE. True for the other cases.
static bool bind_test_loader_holds(BindTestCase which, const Link* link, const Bind* bind, const Runtime* runtime)
{
    const bool loader = which == BIND_TEST_CASE_LOADER || which == BIND_TEST_CASE_WEAK_LOADER || which == BIND_TEST_CASE_MIXED_LOADER;
    const LinkSymbols* symbols = &link->symbols;
    bool result = true;
    for (uint32_t r = 0; loader && r < 2; r += 1)
    {
        const SymbolId symbol = bind_test_global(link, r == 0 ? "_r_debug" : "__libc_stack_end");
        const bool weak = symbol != 0 && (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
        bool imported = false;
        for (uint32_t i = 0; i < bind->import_count; i += 1)
        {
            imported = imported || bind->imports[i] == symbol;
        }
        const bool expected_weak = which == BIND_TEST_CASE_WEAK_LOADER || (which == BIND_TEST_CASE_MIXED_LOADER && r == 1);
        const bool needed = which != BIND_TEST_CASE_WEAK_LOADER;
        result = result && symbol != 0 && weak == expected_weak && imported == needed && (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_OBJECT &&
                 (needed ? symbols->source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED && runtime_loader_version(runtime, symbols->version[symbol])
                         : symbols->source[symbol] == BUSTER_LINK_SOURCE_NONE && symbols->version[symbol] == 0);
    }
    for (uint32_t i = 0; loader && which == BIND_TEST_CASE_WEAK_LOADER && i < bind->import_count; i += 1)
    {
        result = result && !runtime_loader_version(runtime, symbols->version[bind->imports[i]]);
    }
    return result;
}

static bool bind_test_linker_cases(const Runtime* runtime, bool cross_check, const char* directory, bool keep, Arena* arena)
{
    BindTestTally tally = {
        .check = "linkers' cases",
    };
    for (uint32_t c = 0; c < BIND_TEST_CASES; c += 1)
    {
        const BindTestCaseInfo* info = &bind_test_cases[c];
        BindTestCaseProgram vector;
        BindTestCaseProgram exact;
        bind_test_case_program(&vector, runtime, (BindTestCase)c, BIND_PATH_VECTOR, true);
        bind_test_case_program(&exact, runtime, (BindTestCase)c, BIND_PATH_EXACT, true);
        if (vector.bound != info->links)
        {
            printf("  %s: %s: built %d, bound %d, error %s\n", tally.check, info->what, vector.built, vector.bound, bind_test_errors[vector.test.bind.error]);
        }
        bind_test_expect(&tally, vector.built && vector.bound == info->links, info->what, c);
        bind_test_expect(&tally, exact.built == vector.built && exact.bound == vector.bound && bind_test_same_links(&vector.test.link, &exact.test.link) &&
                                     bind_test_same_binds(&vector.test.bind, &exact.test.bind),
                         "the paths agree", c);
        bind_test_expect(&tally, !vector.bound || bind_test_loader_holds((BindTestCase)c, &vector.test.link, &vector.test.bind, runtime),
                         "the loader's names bound as GNU ld and lld bind them, needed only for a strong reference", c);
        if (cross_check)
        {
            BindTestCaseProgram own;
            bind_test_case_program(&own, runtime, (BindTestCase)c, BIND_PATH_VECTOR, false);
            bind_test_expect(&tally, own.bound, "the program alone", c);
            if (own.bound)
            {
                bind_test_case_linkers(&tally, (BindTestCase)c, &own.test.link, directory, keep, arena);
            }
            // What binding found against lld's .dynsym and --why-extract for the same program, and the bound link linked
            // and run.
            if (own.bound && vector.bound)
            {
                bind_test_against_lld(&tally, &own.test.link, 1, &vector.test.link, &vector.test.bind, runtime, directory, info->name, keep, arena);
            }
            if (vector.bound)
            {
                bind_test_link_and_run(&tally, &vector.test.link, &vector.test.bind, runtime, 1, directory, info->name, (const uint8_t*)"", 0, info->status, keep, arena);
            }
            bind_test_link_end(&own.test);
        }
        bind_test_link_end(&vector.test);
        bind_test_link_end(&exact.test);
    }
    return bind_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The checks

static bool bind_test_programs(const Runtime* runtime, uint64_t seed, bool cross_check, const char* directory, bool keep, Arena* arena)
{
    BindTestTally tally = {
        .check = "programs",
    };
    LinkProgramOptions options[BUSTER_LINK_TEST_PROGRAMS];
    link_test_program_options(options, seed, 1);
    for (uint32_t i = 0; i < BUSTER_LINK_TEST_PROGRAMS; i += 1)
    {
        if (options[i].libc)
        {
            BindTestProgram bound;
            bind_test_paths(&tally, runtime, options[i], link_test_program_names[i], &bound, arena);
            BindTestTally names = {
                .check = "programs",
            };
            bind_test_names(&names, &bound.program.link, runtime);
            tally.checked += names.checked;
            tally.failures += names.failures;
            if (cross_check && bound.bound)
            {
                bind_test_link_and_run(&tally, &bound.program.link, &bound.bind, runtime, options[i].units, directory, link_test_program_directories[i], bound.program.output,
                                       BUSTER_LINK_PROGRAM_OUTPUT_BYTES, 0, keep, arena);
                LinkProgram own;
                const bool generated = link_program_generate(&own, options[i]);
                bind_test_expect(&tally, generated, "the program generated alone", i);
                if (generated)
                {
                    bind_test_against_lld(&tally, &own.link, options[i].units, &bound.program.link, &bound.bind, runtime, directory, link_test_program_directories[i], keep, arena);
                }
                link_program_release(&own);
            }
            bind_test_release(&bound);
        }
    }
    return bind_test_report(&tally);
}

static bool bind_test_atexit_cross_check(const Runtime* runtime, const char* directory, bool keep, Arena* arena)
{
    BindTestTally tally = {
        .check = "atexit, cross-check",
    };
    BindTestAtexit bound;
    BindTestAtexit own;
    bind_test_atexit(&bound, runtime, BIND_PATH_VECTOR, true);
    bind_test_atexit(&own, runtime, BIND_PATH_VECTOR, false);
    bind_test_expect(&tally, bound.bound && own.bound, "the atexit program built", 0);
    if (bound.bound && own.bound)
    {
        bind_test_link_and_run(&tally, &bound.test.link, &bound.test.bind, runtime, 1, directory, "atexit", (const uint8_t*)"", 0, 0, keep, arena);
        bind_test_against_lld(&tally, &own.test.link, 1, &bound.test.link, &bound.test.bind, runtime, directory, "atexit", keep, arena);
    }
    bind_test_link_end(&bound.test);
    bind_test_link_end(&own.test);
    return bind_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// The static runtimes
//
// A static executable's runtime (runtime_import_static) and a static PIE's (runtime_import_static_pie), which share
// libc.a and the rest of GCC's group: the programs' checks below take either, by its kind, and the rules are the static
// executable's (bind_test_static_rules).

// Whether bit `index` of a set is set, and setting it.
static bool bind_test_has(const uint64_t* bits, uint32_t index)
{
    return (bits[index / 64] >> (index % 64) & 1) != 0;
}

static void bind_test_add(uint64_t* bits, uint32_t index)
{
    bits[index / 64] |= 1ull << (index % 64);
}

// The global and weak symbols of `link` from `first` to `end` whose names are the runtime's global names, as GNU ld's
// archive scan counts them: one in an atom, absolute, the program's or the linker's defines its name, and another
// strong one refers to it.
static void bind_test_static_note(const Runtime* runtime, const Link* link, SymbolId first, SymbolId end, uint64_t* defined, uint64_t* referenced)
{
    const LinkSymbols* symbols = &link->symbols;
    for (SymbolId symbol = first; symbol < end; symbol += 1)
    {
        const NameId name = symbols->name[symbol];
        const uint8_t binding = symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING;
        const bool global = name != 0 && binding != BUSTER_LINK_SYMBOL_LOCAL;
        const uint32_t entry = global ? runtime_find_global_name(&runtime->global_names, link->names.bytes[name], link->names.length[name], link->names.hash[name]) : 0;
        const uint8_t source = symbols->source[symbol];
        const bool definition = symbols->atom[symbol] != 0 || source == BUSTER_LINK_SOURCE_PROGRAM || source == BUSTER_LINK_SOURCE_ABSOLUTE || source == BUSTER_LINK_SOURCE_LINKER;
        if (entry != 0 && definition)
        {
            bind_test_add(defined, entry - 1);
        }
        else if (entry != 0 && binding == BUSTER_LINK_SYMBOL_GLOBAL)
        {
            bind_test_add(referenced, entry - 1);
        }
    }
}

// The members GNU ld loads for `program`, a link not bound, from a static runtime (the closure's oracle, which reads
// the archives' symbol indices rather than binding's references and members): the indices scanned in the group's
// order, again and again until a whole pass loads nothing, each entry whose name the link refers to strongly and
// nothing in it defines loading its member, whose definitions and strong references then count too, as GNU ld's
// elf_link_add_archive_symbols scans. The link is the program, the crt objects before it (crt1.o, crti.o and
// crtbeginT.o, or a static PIE's rcrt1.o, crti.o and crtbeginS.o) and the names the kind's linker defines. A bit per
// member in `loaded`.
static void bind_test_static_expected(const Runtime* runtime, const Link* program, uint64_t* loaded, Arena* arena)
{
    const RuntimeNames* names = &runtime->global_names;
    const uint64_t length = arena->length;
    const uint32_t name_words = (names->count + 63) / 64;
    uint64_t* defined = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, name_words + 1);
    uint64_t* referenced = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, name_words + 1);
    memset(loaded, 0, ((runtime->member_count + 63) / 64) * sizeof(uint64_t));
    if (defined != 0 && referenced != 0)
    {
        memset(defined, 0, name_words * sizeof(uint64_t));
        memset(referenced, 0, name_words * sizeof(uint64_t));
        for (uint32_t e = 0; e < names->count; e += 1)
        {
            if (names->linker[e])
            {
                bind_test_add(defined, e);
            }
        }
        bind_test_static_note(runtime, program, 1, program->symbols.count, defined, referenced);
        const uint32_t head[] = {
            RUNTIME_OBJECT_START,
            RUNTIME_OBJECT_INIT_PROLOGUE,
            RUNTIME_OBJECT_BEGIN,
        };
        for (uint32_t h = 0; h < BUSTER_ARRAY_LENGTH(head); h += 1)
        {
            const RuntimePiece* piece = &runtime->pieces[head[h]];
            bind_test_static_note(runtime, &runtime->link, piece->begin.symbols, piece->end.symbols, defined, referenced);
        }
        bool scanning = true;
        while (scanning)
        {
            scanning = false;
            for (uint32_t a = 0; a < runtime->archive_count; a += 1)
            {
                const RuntimeArchive* archive = &runtime->archives[a];
                for (uint32_t i = 0; i < archive->archive.symbol_count; i += 1)
                {
                    const ImportArchiveSymbol* indexed = &archive->archive.symbols[i];
                    const uint8_t* bytes = (const uint8_t*)indexed->name.pointer;
                    const uint32_t entry = runtime_find_global_name(names, bytes, (uint32_t)indexed->name.length, link_hash(bytes, indexed->name.length));
                    const uint32_t member = archive->first_member + indexed->member;
                    if (entry != 0 && !bind_test_has(loaded, member) && bind_test_has(referenced, entry - 1) && !bind_test_has(defined, entry - 1))
                    {
                        const RuntimePiece* piece = &runtime->pieces[RUNTIME_OBJECTS + member];
                        bind_test_add(loaded, member);
                        bind_test_static_note(runtime, &runtime->link, piece->begin.symbols, piece->end.symbols, defined, referenced);
                        scanning = true;
                    }
                }
            }
        }
    }
    arena_pop_to(arena, length);
}

// Whether no two global or weak symbols of the link have one name: an open-addressing table of them by name hash.
static bool bind_test_unique_globals(const Link* link, Arena* arena)
{
    const LinkSymbols* symbols = &link->symbols;
    const uint64_t length = arena->length;
    uint64_t slots = 16;
    while (slots < 2ull * symbols->count)
    {
        slots *= 2;
    }
    SymbolId* table = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, slots);
    bool result = table != 0;
    if (result)
    {
        memset(table, 0, slots * sizeof(SymbolId));
    }
    for (SymbolId symbol = 1; result && symbol < symbols->count; symbol += 1)
    {
        const NameId name = symbols->name[symbol];
        const bool global = name != 0 && (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
        uint64_t place = link->names.hash[name] & (slots - 1);
        for (; global && table[place] != 0; place = (place + 1) & (slots - 1))
        {
            const NameId other = symbols->name[table[place]];
            result = result && (link->names.length[other] != link->names.length[name] || memcmp(link->names.bytes[other], link->names.bytes[name], link->names.length[name]) != 0);
        }
        table[place] = global ? symbol : table[place];
    }
    arena_pop_to(arena, length);
    return result;
}

// What a fixup's target can be in a link bound against a static runtime of `kind`: defined in an atom, absolute, a name
// the kind's linker defines, a weak reference to nothing, or __tls_get_addr, left to the relaxation.
static bool bind_test_static_bound(const Link* link, RuntimeKind kind, SymbolId symbol)
{
    const LinkSymbols* symbols = &link->symbols;
    const uint8_t source = symbols->source[symbol];
    const NameId name = symbols->name[symbol];
    const bool weak = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    const bool linker = source == BUSTER_LINK_SOURCE_LINKER && name != 0 && runtime_linker_name(kind, link->names.bytes[name], link->names.length[name]);
    const bool relaxed = source == BUSTER_LINK_SOURCE_NONE && name != 0 && bind_relaxed_name(link, name);
    return symbols->atom[symbol] != 0 || source == BUSTER_LINK_SOURCE_ABSOLUTE || linker || (source == BUSTER_LINK_SOURCE_NONE && weak) || relaxed;
}

// Checks a link bound against a static runtime (a static executable's or a static PIE's), whose program's units come
// before `unit_base`: the members the oracle (`expected`) loads joined, and no others; link_check; one symbol per
// global name, and none of a shared library's; every fixup's target bound (bind_test_static_bound); main the
// program's, and __libc_start_main, _init, __dso_handle and _dl_relocate_static_pie bound as GNU ld binds them (the
// last crt1.o's empty one, or for a static PIE the member that relocates the executable, dl-reloc-static-pie.o, whose
// _DYNAMIC is the linker's); each joined object's atoms but those of the COMDAT groups of a signature that an object
// before it in link order has (the program has none), and their priorities.
static void bind_test_static_structure(BindTestTally* tally, Link* link, const Bind* bind, const Runtime* runtime, const uint64_t* expected, uint32_t unit_base,
                                       uint32_t program_priorities, Arena* arena)
{
    const bool pie = runtime->kind == RUNTIME_KIND_STATIC_PIE;
    const LinkSymbols* symbols = &link->symbols;
    const uint32_t words = (runtime->member_count + 63) / 64;
    bool members = true;
    for (uint32_t w = 0; w < words; w += 1)
    {
        members = members && bind->members_joined[w] == expected[w] && bind->members_needed[w] == expected[w];
    }
    bind_test_expect(tally, members, "the members GNU ld's archive scan loads joined, and no others", 0);
    bind_test_expect(tally, link_check(link) == LINK_ERROR_NONE, "link_check", link->error);
    bind_test_expect(tally, bind_test_unique_globals(link, arena), "one symbol per global name", 0);
    uint64_t shared = 0;
    for (SymbolId symbol = 1; symbol < symbols->count; symbol += 1)
    {
        shared += symbols->source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED || symbols->source[symbol] == BUSTER_LINK_SOURCE_FOREIGN_SHARED ? 1 : 0;
    }
    bind_test_expect(tally, shared == 0, "no symbol of a shared library", shared);
    uint64_t unbound = 0;
    for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
    {
        unbound += bind_test_static_bound(link, runtime->kind, link->fixups.target[fixup]) ? 0 : 1;
    }
    bind_test_expect(tally, unbound == 0, "every fixup's target bound", unbound);
    const SymbolId main_symbol = bind_test_global(link, "main");
    const AtomId main_atom = main_symbol != 0 ? symbols->atom[main_symbol] : 0;
    bind_test_expect(tally, main_atom != 0 && link->atoms.unit[main_atom] < unit_base, "main, the program's", main_symbol);
    const SymbolId start_main = bind_test_global(link, "__libc_start_main");
    const AtomId start_atom = start_main != 0 ? symbols->atom[start_main] : 0;
    bind_test_expect(tally, start_atom != 0 && link->atoms.unit[start_atom] >= unit_base + runtime->member_unit &&
                                symbols->source[start_main] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE,
                     "__libc_start_main, a member's", start_main);
    const SymbolId init = bind_test_global(link, "_init");
    bind_test_expect(tally, init != 0 && symbols->atom[init] != 0 && link->atoms.unit[symbols->atom[init]] == unit_base + RUNTIME_OBJECT_INIT_PROLOGUE, "_init, crti.o's", init);
    const SymbolId dso = bind_test_global(link, "__dso_handle");
    bind_test_expect(tally, dso == 0 || (symbols->atom[dso] != 0 && link->atoms.unit[symbols->atom[dso]] == unit_base + RUNTIME_OBJECT_BEGIN),
                     pie ? "__dso_handle, crtbeginS.o's" : "__dso_handle, crtbeginT.o's", dso);
    // _dl_relocate_static_pie, which __libc_start_main's member calls: crt1.o's empty one, or for a static PIE, whose
    // rcrt1.o has none, the definition of libc.a's member that relocates it, which joins.
    const SymbolId relocate = bind_test_global(link, "_dl_relocate_static_pie");
    const AtomId relocate_atom = relocate != 0 ? symbols->atom[relocate] : 0;
    const uint32_t relocator = bind_test_member(runtime, "dl-reloc-static-pie.o");
    const uint32_t relocate_unit = pie ? unit_base + runtime->member_unit + relocator : unit_base + RUNTIME_OBJECT_START;
    bind_test_expect(tally, relocate_atom != 0 && link->atoms.unit[relocate_atom] == relocate_unit && (!pie || relocator < runtime->member_count),
                     pie ? "_dl_relocate_static_pie, dl-reloc-static-pie.o's" : "_dl_relocate_static_pie, crt1.o's", relocate);
    if (pie)
    {
        const SymbolId dynamic = bind_test_global(link, "_DYNAMIC");
        bind_test_expect(tally, dynamic != 0 && symbols->atom[dynamic] == 0 && symbols->source[dynamic] == BUSTER_LINK_SOURCE_LINKER, "_DYNAMIC, the linker's", dynamic);
    }
    // Each joined object's atoms, in link order: the crt objects before the program (crt1.o, crti.o and crtbeginT.o, or
    // rcrt1.o, crti.o and crtbeginS.o), the members in archive order, and those after it (crtend.o or crtendS.o, and
    // crtn.o).
    const uint64_t length = arena->length;
    const uint32_t piece_count = RUNTIME_OBJECTS + runtime->member_count;
    uint32_t* atoms = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)piece_count + 1);
    uint64_t* signatures = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, runtime->group_signatures / 64 + 1);
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)piece_count + 1);
    bool placed = atoms != 0 && signatures != 0 && order != 0;
    uint32_t discarded = 0;
    uint32_t priorities = program_priorities;
    if (placed)
    {
        memset(atoms, 0, ((uint64_t)piece_count + 1) * sizeof(uint32_t));
        memset(signatures, 0, (runtime->group_signatures / 64 + 1) * sizeof(uint64_t));
        // The program's atoms count in the spare last entry.
        for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
        {
            const uint32_t unit = link->atoms.unit[atom];
            const bool runtime_unit = unit >= unit_base && unit - unit_base < piece_count;
            atoms[runtime_unit ? unit - unit_base : piece_count] += 1;
        }
        uint32_t count = 0;
        order[count++] = RUNTIME_OBJECT_START;
        order[count++] = RUNTIME_OBJECT_INIT_PROLOGUE;
        order[count++] = RUNTIME_OBJECT_BEGIN;
        for (uint32_t m = 0; m < runtime->member_count; m += 1)
        {
            order[count] = RUNTIME_OBJECTS + m;
            count += bind_test_has(bind->members_joined, m) ? 1 : 0;
        }
        order[count++] = RUNTIME_OBJECT_END;
        order[count++] = RUNTIME_OBJECT_INIT_EPILOGUE;
        for (uint32_t i = 0; placed && i < count; i += 1)
        {
            const uint32_t piece = order[i];
            const RuntimePiece* records = &runtime->pieces[piece];
            uint32_t kept = records->end.atoms - records->begin.atoms;
            for (uint32_t g = runtime->group_begin[piece]; g < runtime->group_begin[piece + 1]; g += 1)
            {
                const RuntimeGroup* group = &runtime->groups[g];
                const bool first = !bind_test_has(signatures, group->signature);
                bind_test_add(signatures, group->signature);
                kept -= first ? 0 : group->count;
                discarded += first ? 0 : 1;
            }
            priorities += records->end.priorities - records->begin.priorities;
            placed = atoms[piece] == kept;
        }
    }
    bind_test_expect(tally, placed, "each joined object's atoms, but its COMDAT groups' after the first of their signatures", discarded);
    bind_test_expect(tally, link->priorities.count - 1 == priorities, "the program's priorities and the joined objects'", link->priorities.count - 1);
    arena_pop_to(arena, length);
}

// The key a linker's trace gives a member, its archive's file name and its own: "libc.a(printf.o)".
static void bind_test_member_key(char* key, uint64_t capacity, const Runtime* runtime, uint32_t member)
{
    uint32_t archive = 0;
    for (uint32_t a = 1; a < runtime->archive_count; a += 1)
    {
        archive = runtime->archives[a].first_member <= member ? a : archive;
    }
    const String8 path = runtime->archives[archive].path;
    uint64_t base = path.length;
    while (base > 0 && path.pointer[base - 1] != '/')
    {
        base -= 1;
    }
    const String8 name = runtime_member_name(runtime, member);
    snprintf(key, capacity, "%.*s(%.*s)", (int)(path.length - base), path.pointer + base, (int)name.length, name.pointer);
}

// The member a line of a linker's list names, as "libc.a(printf.o)" in `key`: lld's --why-extract has
// "path/libc.a(printf.o)" in its second column (the first names what referred to it), and GNU ld's --trace given twice
// "(path/libc.a)printf.o". False for a line that names none.
static bool bind_test_line_member(const char* line, uint64_t length, char* key, uint64_t capacity)
{
    const char* tab = memchr(line, '\t', length);
    const char* column = tab != 0 ? tab + 1 : line + length;
    const char* column_end = memchr(column, '\t', (uint64_t)(line + length - column));
    const uint64_t column_length = (uint64_t)((column_end != 0 ? column_end : line + length) - column);
    const char* close = line[0] == '(' ? memchr(line, ')', length) : memchr(column, ')', column_length);
    const char* archive = memmem(column, column_length, ".a(", 3);
    bool result = false;
    if (length != 0 && line[0] == '(' && close != 0)
    {
        // GNU ld's: the archive's path in the parentheses, then the member.
        const char* base = close;
        while (base > line + 1 && base[-1] != '/')
        {
            base -= 1;
        }
        snprintf(key, capacity, "%.*s(%.*s)", (int)(close - base), base, (int)(length - (uint64_t)(close + 1 - line)), close + 1);
        result = true;
    }
    else if (archive != 0 && close != 0 && close > archive)
    {
        // lld's: the archive's path and the member in parentheses, in the second column.
        const char* base = archive;
        while (base > column && base[-1] != '/')
        {
            base -= 1;
        }
        snprintf(key, capacity, "%.*s", (int)(close + 1 - base), base);
        result = true;
    }
    return result;
}

// Whether a linker's list of the members it loaded (GNU ld's --trace given twice, or lld's --why-extract) has the
// members that joined and no others.
static bool bind_test_same_static_members(String8 text, const Bind* bind, const Runtime* runtime, Arena* arena)
{
    const uint64_t length = arena->length;
    const uint32_t capacity = runtime->member_count + 1;
    char* keys = BUSTER_ARENA_PUSH_ARRAY(arena, char, (uint64_t)capacity * 256);
    bool result = text.pointer != 0 && keys != 0;
    uint32_t listed = 0;
    for (const char* line = text.pointer; result && line != 0 && *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const uint64_t line_length = end != 0 ? (uint64_t)(end - line) : strlen(line);
        char key[256];
        if (bind_test_line_member(line, line_length, key, sizeof(key)))
        {
            result = listed < capacity;
            if (result)
            {
                memcpy(keys + (uint64_t)listed * 256, key, sizeof(key));
                listed += 1;
            }
        }
        line = end != 0 ? end + 1 : 0;
    }
    uint32_t joined = 0;
    for (uint32_t m = 0; result && m < runtime->member_count; m += 1)
    {
        if (bind_test_has(bind->members_joined, m))
        {
            char key[256];
            bind_test_member_key(key, sizeof(key), runtime, m);
            bool found = false;
            for (uint32_t i = 0; !found && i < listed; i += 1)
            {
                found = strcmp(keys + (uint64_t)i * 256, key) == 0;
            }
            if (!found)
            {
                printf("  %s joined, which the linker didn't load\n", key);
            }
            result = found;
            joined += 1;
        }
    }
    if (result && listed != joined)
    {
        printf("  the linker loaded %u members, binding joined %u\n", listed, joined);
    }
    arena_pop_to(arena, length);
    return result && listed == joined;
}

// Links the objects at `paths` with `cc -static`, or against a static PIE's runtime `cc -static-pie`, by GNU ld (with
// --trace twice) and by lld (with --why-extract): each must link, load the members that joined and no others, and run
// with `status`, or print `output` if it isn't 0.
static void bind_test_static_linkers(BindTestTally* tally, const Bind* bind, const Runtime* runtime, char** paths, uint32_t count, const char* directory, const char* name,
                                     const char* extra, int status, const uint8_t* output, uint64_t output_bytes, bool keep, Arena* arena)
{
    const bool pie = runtime->kind == RUNTIME_KIND_STATIC_PIE;
    const uint64_t length = arena->length;
    for (uint32_t linker = 0; linker < 2; linker += 1)
    {
        const bool lld = linker == 1;
        char program[BUSTER_BIND_TEST_PATH_BYTES];
        char listing[BUSTER_BIND_TEST_PATH_BYTES];
        char option[BUSTER_BIND_TEST_PATH_BYTES + 32];
        char out[BUSTER_BIND_TEST_PATH_BYTES];
        char err[BUSTER_BIND_TEST_PATH_BYTES];
        snprintf(program, sizeof(program), "%s/%s-%s", directory, name, lld ? "lld" : "bfd");
        snprintf(listing, sizeof(listing), "%s.members", program);
        snprintf(option, sizeof(option), "-Wl,--why-extract=%s", listing);
        snprintf(out, sizeof(out), "%s.out", program);
        snprintf(err, sizeof(err), "%s.err", program);
        char* arguments[BUSTER_BIND_TEST_PATH_BYTES];
        uint32_t used = 0;
        arguments[used++] = "cc";
        arguments[used++] = pie ? "-static-pie" : "-static";
        arguments[used++] = lld ? "-fuse-ld=lld" : "-Wl,--trace,--trace";
        arguments[used++] = lld ? option : "-o";
        arguments[used++] = lld ? "-o" : program;
        arguments[used] = program;
        used += lld ? 1 : 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            arguments[used++] = paths[i];
        }
        arguments[used] = (char*)extra;
        used += extra != 0 ? 1 : 0;
        arguments[used] = 0;
        // GNU ld's trace goes to standard output.
        const int linked = object_test_run(arguments, lld ? out : listing, err);
        const char* const linked_names[2][2] = {
            {
                "GNU ld links it with -static",
                "lld links it with -static",
            },
            {
                "GNU ld links it with -static-pie",
                "lld links it with -static-pie",
            },
        };
        bind_test_expect(tally, linked == 0, linked_names[pie ? 1 : 0][linker], (uint64_t)linked);
        const String8 members = linked == 0 ? object_test_read_file(arena, listing) : (String8){ 0 };
        bind_test_expect(tally, bind_test_same_static_members(members, bind, runtime, arena), lld ? "lld's members" : "GNU ld's members", 0);
        char* run[] = { program, 0 };
        const int ran = linked == 0 ? object_test_run(run, out, err) : -1;
        const String8 printed = ran >= 0 ? object_test_read_file(arena, out) : (String8){ 0 };
        const bool printed_right = output == 0 || (printed.pointer != 0 && printed.length == output_bytes && memcmp(printed.pointer, output, output_bytes) == 0);
        bind_test_expect(tally, ran == status && printed_right, "the static program runs", (uint64_t)ran);
        if (!keep)
        {
            unlink(program);
            unlink(listing);
            unlink(out);
            unlink(err);
        }
        arena_pop_to(arena, length);
    }
}

// test-link's sets that call libc, bound against a static runtime (a static executable's or a static PIE's) by both
// paths: the same links and members, which must be the oracle's, checked as bind_test_static_structure does; with the
// cross-check, the program's own objects linked by GNU ld and lld with -static (or -static-pie), which must load the
// same members and run as the model predicts.
static bool bind_test_static_programs(const Runtime* runtime, uint64_t seed, bool cross_check, const char* directory, bool keep, Arena* arena)
{
    const bool pie = runtime->kind == RUNTIME_KIND_STATIC_PIE;
    BindTestTally tally = {
        .check = pie ? "static PIE programs" : "static programs",
    };
    LinkProgramOptions options[BUSTER_LINK_TEST_PROGRAMS];
    link_test_program_options(options, seed, 1);
    const uint32_t words = (runtime->member_count + 63) / 64;
    uint64_t* expected = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, words + 1);
    for (uint32_t i = 0; expected != 0 && i < BUSTER_LINK_TEST_PROGRAMS; i += 1)
    {
        if (options[i].libc)
        {
            BindTestProgram vector;
            BindTestProgram exact;
            LinkProgram own;
            const bool vector_bound = bind_test_program(&vector, runtime, options[i], BIND_PATH_VECTOR);
            const bool exact_bound = bind_test_program(&exact, runtime, options[i], BIND_PATH_EXACT);
            const bool generated = link_program_generate(&own, options[i]);
            bind_test_expect(&tally, vector_bound && exact_bound && generated, link_test_program_names[i], vector.bind.error);
            bind_test_expect(&tally, bind_test_same_links(&vector.program.link, &exact.program.link), "the paths' links the same", i);
            bind_test_expect(&tally, bind_test_same_binds(&vector.bind, &exact.bind), "the paths' members the same", i);
            if (vector_bound && generated)
            {
                bind_test_static_expected(runtime, &own.link, expected, arena);
                bind_test_static_structure(&tally, &vector.program.link, &vector.bind, runtime, expected, options[i].units, 0, arena);
                uint32_t joined = 0;
                for (uint32_t m = 0; m < runtime->member_count; m += 1)
                {
                    joined += bind_test_has(vector.bind.members_joined, m) ? 1 : 0;
                }
                printf("  %s: %u of %u members joined; %u atoms, %u symbols, %u fixups\n", link_test_program_names[i], joined, runtime->member_count,
                       vector.program.link.atoms.count - 1, vector.program.link.symbols.count - 1, vector.program.link.fixups.count - 1);
            }
            if (cross_check && vector_bound && generated)
            {
                const uint64_t length = arena->length;
                char** paths = BUSTER_ARENA_PUSH_ARRAY(arena, char*, options[i].units);
                char* path_bytes = arena_push(arena, (uint64_t)options[i].units * BUSTER_BIND_TEST_PATH_BYTES, 1);
                bool written = paths != 0 && path_bytes != 0;
                for (uint32_t unit = 0; written && unit < options[i].units; unit += 1)
                {
                    paths[unit] = path_bytes + (uint64_t)unit * BUSTER_BIND_TEST_PATH_BYTES;
                }
                char name[BUSTER_BIND_TEST_PATH_BYTES];
                snprintf(name, sizeof(name), "%s-%s", pie ? "static-pie" : "static", link_test_program_directories[i]);
                written = written && bind_test_write_objects(&own.link, options[i].units, directory, name, paths, arena);
                bind_test_expect(&tally, written, "the program's objects written", i);
                if (written)
                {
                    bind_test_static_linkers(&tally, &vector.bind, runtime, paths, options[i].units, directory, name, 0, 0, own.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES, keep, arena);
                }
                for (uint32_t unit = 0; written && !keep && unit < options[i].units; unit += 1)
                {
                    unlink(paths[unit]);
                }
                arena_pop_to(arena, length);
            }
            link_program_release(&own);
            bind_test_release(&vector);
            bind_test_release(&exact);
        }
    }
    return bind_test_report(&tally);
}

// A link of a compiled object's records as the program's unit 0 (source PROGRAM), with room for the static runtime.
static bool bind_test_static_import(BindTestLink* test, const Runtime* runtime, String8 object)
{
    LinkCapacity own = { 0 };
    const bool counted = import_object_capacity(object, &own).error == IMPORT_ERROR_NONE;
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const LinkCapacity extra = bind_capacity(runtime, &name_bytes, &atom_bytes);
    const LinkCapacity capacity = {
        .names = extra.names + own.names,
        .atoms = extra.atoms + own.atoms,
        .symbols = extra.symbols + own.symbols,
        .fixups = extra.fixups + own.fixups,
        .wide_addends = extra.wide_addends + own.wide_addends,
        .priorities = extra.priorities + own.priorities,
    };
    *test = (BindTestLink){ 0 };
    test->tables = arena_reserve(link_capacity_bytes(capacity));
    test->names = arena_reserve(name_bytes + object.length + 4096);
    test->bytes = arena_reserve(atom_bytes + 65536);
    test->arena = arena_reserve(BUSTER_BIND_TEST_BIND_BYTES);
    ImportObject imported;
    bool result = counted && test->names.base != 0 && test->bytes.base != 0 && test->arena.base != 0 && link_initialize(&test->link, &test->tables, &test->names, capacity);
    result = result && import_object(&test->link, object, 0, BUSTER_LINK_SOURCE_PROGRAM, &test->arena, &imported).error == IMPORT_ERROR_NONE;
    return result;
}

// The C programs the cross-check compiles: each one's name, source, an option for cc, and the status it exits with
// (for weak-puts, whether puts's member joined).
typedef struct BindTestStaticSource BindTestStaticSource;
struct BindTestStaticSource
{
    const char* name;
    const char* source;
    const char* option;
    int status;
};

static const BindTestStaticSource bind_test_static_sources[] = {
    {
        .name = "hello",
        .source = "#include <stdio.h>\n"
                  "int main(void) { puts(\"hello\"); return 0; }\n",
    },
    {
        .name = "stdio",
        .source = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
                  "static int compare(const void* a, const void* b) { return *(const double*)a < *(const double*)b ? -1 : 1; }\n"
                  "int main(int argc, char** argv) {\n"
                  "    double* values = malloc(16 * sizeof(double));\n"
                  "    for (int i = 0; i < 16; i += 1) values[i] = strtod(\"2.5\", 0) * (16 - i);\n"
                  "    qsort(values, 16, sizeof(double), compare);\n"
                  "    char line[128];\n"
                  "    snprintf(line, sizeof(line), \"%s %.3f %g %s\", argv[0], values[0], values[15], getenv(\"HOME\") != 0 ? \"home\" : \"none\");\n"
                  "    printf(\"%zu %s\\n\", strlen(line), line + strlen(argv[0]));\n"
                  "    free(values);\n"
                  "    return argc - 1;\n"
                  "}\n",
    },
    {
        .name = "threads",
        .source = "#include <pthread.h>\n#include <stdio.h>\n"
                  "static __thread int counter;\n"
                  "static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;\n"
                  "static int total;\n"
                  "static void* work(void* argument) { counter += (int)(long)argument; pthread_mutex_lock(&lock); total += counter; pthread_mutex_unlock(&lock); return 0; }\n"
                  "int main(void) {\n"
                  "    pthread_t threads[4];\n"
                  "    for (long i = 0; i < 4; i += 1) pthread_create(&threads[i], 0, work, (void*)(i + 1));\n"
                  "    for (int i = 0; i < 4; i += 1) pthread_join(threads[i], 0);\n"
                  "    printf(\"%d\\n\", total);\n"
                  "    return total == 10 ? 0 : 1;\n"
                  "}\n",
        .option = "-pthread",
    },
    {
        .name = "jumps",
        .source = "#include <setjmp.h>\n#include <signal.h>\n#include <stdlib.h>\n#include <unistd.h>\n"
                  "static jmp_buf place;\n"
                  "static void handler(int signal) { longjmp(place, signal); }\n"
                  "static void finish(void) { _exit(0); }\n"
                  "int main(void) {\n"
                  "    atexit(finish);\n"
                  "    signal(SIGUSR1, handler);\n"
                  "    if (setjmp(place) == 0) raise(SIGUSR1);\n"
                  "    return 3;\n"
                  "}\n",
    },
    {
        .name = "weak-puts",
        .source = "extern int puts(const char*) __attribute__((weak));\n"
                  "int main(void) { return puts != 0; }\n",
    },
};

// The C programs of bind_test_static_sources, each compiled by cc (with -fPIE for a static PIE's runtime), imported as
// the program, bound by both paths against a static runtime (a static executable's or a static PIE's) and checked as
// bind_test_static_structure does, with the members the oracle loads; then linked by GNU ld and lld with -static (or
// -static-pie), which must load the same members and run with the program's status.
static bool bind_test_static_compiled(const Runtime* runtime, const char* directory, bool keep, Arena* arena)
{
    const bool pie = runtime->kind == RUNTIME_KIND_STATIC_PIE;
    const char* const prefix = pie ? "static-pie" : "static";
    BindTestTally tally = {
        .check = pie ? "static PIE compiled programs" : "static compiled programs",
    };
    const uint32_t words = (runtime->member_count + 63) / 64;
    uint64_t* expected = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, words + 1);
    for (uint32_t s = 0; expected != 0 && s < BUSTER_ARRAY_LENGTH(bind_test_static_sources); s += 1)
    {
        const BindTestStaticSource* program = &bind_test_static_sources[s];
        const uint64_t length = arena->length;
        char name[BUSTER_BIND_TEST_PATH_BYTES];
        char source_path[BUSTER_BIND_TEST_PATH_BYTES];
        char object_path[BUSTER_BIND_TEST_PATH_BYTES];
        char out[BUSTER_BIND_TEST_PATH_BYTES];
        char err[BUSTER_BIND_TEST_PATH_BYTES];
        snprintf(name, sizeof(name), "%s-%s", prefix, program->name);
        snprintf(source_path, sizeof(source_path), "%s/%s.c", directory, name);
        snprintf(object_path, sizeof(object_path), "%s/%s.o", directory, name);
        snprintf(out, sizeof(out), "%s/%s.cc.out", directory, name);
        snprintf(err, sizeof(err), "%s/%s.cc.err", directory, name);
        FILE* file = fopen(source_path, "w");
        const bool written = file != 0 && fputs(program->source, file) >= 0;
        if (file != 0)
        {
            fclose(file);
        }
        // cc, -O2, -fPIE, -c, -o, the object, the source, the option and the null after them.
        char* compile[9];
        uint32_t used = 0;
        compile[used++] = "cc";
        compile[used++] = "-O2";
        compile[used] = "-fPIE";
        used += pie ? 1 : 0;
        compile[used++] = "-c";
        compile[used++] = "-o";
        compile[used++] = object_path;
        compile[used++] = source_path;
        compile[used] = (char*)program->option;
        used += program->option != 0 ? 1 : 0;
        compile[used] = 0;
        const bool compiled = written && object_test_run(compile, out, err) == 0;
        bind_test_expect(&tally, compiled, program->name, s);
        const String8 object = compiled ? object_test_read_file(arena, object_path) : (String8){ 0 };
        BindTestLink vector;
        BindTestLink exact;
        const bool vector_imported = object.pointer != 0 && bind_test_static_import(&vector, runtime, object);
        const bool exact_imported = object.pointer != 0 && bind_test_static_import(&exact, runtime, object);
        bind_test_expect(&tally, vector_imported && exact_imported, "the object imported", s);
        const uint32_t priorities = vector_imported ? vector.link.priorities.count - 1 : 0;
        if (vector_imported)
        {
            bind_test_static_expected(runtime, &vector.link, expected, arena);
        }
        const bool vector_bound = vector_imported && bind_begin(&vector.bind, &vector.link, runtime, BIND_PATH_VECTOR, &vector.bytes, 1, &vector.arena) &&
                                  bind_end(&vector.bind, &vector.link);
        const bool exact_bound = exact_imported && bind_begin(&exact.bind, &exact.link, runtime, BIND_PATH_EXACT, &exact.bytes, 1, &exact.arena) &&
                                 bind_end(&exact.bind, &exact.link);
        bind_test_expect(&tally, vector_bound && exact_bound, "bound by both paths", vector.bind.error);
        if (vector_bound && exact_bound)
        {
            bind_test_expect(&tally, bind_test_same_links(&vector.link, &exact.link), "the paths' links the same", s);
            bind_test_expect(&tally, bind_test_same_binds(&vector.bind, &exact.bind), "the paths' members the same", s);
            bind_test_static_structure(&tally, &vector.link, &vector.bind, runtime, expected, 1, priorities, arena);
            const uint32_t puts_entry = runtime_find_global_name(&runtime->global_names, (const uint8_t*)"puts", 4, link_hash((const uint8_t*)"puts", 4));
            const uint32_t puts_member = puts_entry != 0 ? runtime->global_names.member[puts_entry - 1] : 0;
            const bool puts_joined = puts_member != 0 && bind_test_has(vector.bind.members_joined, puts_member - 1);
            const int status = strcmp(program->name, "weak-puts") == 0 ? (puts_joined ? 1 : 0) : program->status;
            uint32_t joined = 0;
            for (uint32_t m = 0; m < runtime->member_count; m += 1)
            {
                joined += bind_test_has(vector.bind.members_joined, m) ? 1 : 0;
            }
            printf("  %s: %u members joined; %u atoms, %u symbols, %u fixups\n", program->name, joined, vector.link.atoms.count - 1, vector.link.symbols.count - 1,
                   vector.link.fixups.count - 1);
            char* paths[] = { object_path };
            bind_test_static_linkers(&tally, &vector.bind, runtime, paths, 1, directory, name, program->option, status, 0, 0, keep, arena);
        }
        // bind_test_static_import made both links, whether or not they imported.
        if (object.pointer != 0)
        {
            bind_test_link_end(&vector);
            bind_test_link_end(&exact);
        }
        if (!keep)
        {
            unlink(source_path);
            unlink(object_path);
            unlink(out);
            unlink(err);
        }
        arena_pop_to(arena, length);
    }
    return bind_test_report(&tally);
}

// The rules a static runtime adds, on links made by hand, each bound by both paths, which must make the same link, against
// a static executable's runtime and a static PIE's (whose start object, crt1.o's here, is rcrt1.o):
//   - a strong reference to a name nothing defines: an error naming it;
//   - strong and weak references to __tls_get_addr: bound, the strong one left undefined for the relaxation;
//   - references to the kind's linker's names (__ehdr_start, _end and etext, with a static executable's IRELATIVE
//     relocations' bounds, __rela_iplt_start and __rela_iplt_end, or a static PIE's _DYNAMIC and
//     __preinit_array_start): the linker's;
//   - a strong reference to a name only the other kind's linker defines (a static executable's _DYNAMIC, a static
//     PIE's __rela_iplt_start, as GNU ld's and lld's leave them): an error naming it;
//   - a weak reference to puts: bound to puts's member's definition if the closure loads it for another reason (GNU
//     ld's archive scan, bind_test_static_expected, says), and otherwise to nothing, without the member; and a strong
//     one, which loads it;
//   - a weak definition of __libc_start_main: crt1.o's strong reference takes it, and its member doesn't join;
//   - a weak absolute definition of data_start: crt1.o's weak one, before the program in link order, takes its place,
//     so that data_start is __data_start, as with GNU ld and lld;
//   - a strong reference to a name a member defines absolutely: the member joins, and the name is absolute;
//   - weak definitions of 4 and of 24 names that the members of a link of main alone refer to (none thread-local),
//     which the vector path's closure compares references with (BUSTER_BIND_BLOCKED) or, past 16, leaves to the exact
//     path's: GNU ld's members.
typedef enum BindTestStaticRule
{
    BIND_TEST_STATIC_NOWHERE,
    BIND_TEST_STATIC_TLS_GET_ADDR,
    BIND_TEST_STATIC_LINKER_NAMES,
    BIND_TEST_STATIC_OTHER_KIND,
    BIND_TEST_STATIC_WEAK_PUTS,
    BIND_TEST_STATIC_STRONG_PUTS,
    BIND_TEST_STATIC_WEAK_START_MAIN,
    BIND_TEST_STATIC_WEAK_ABSOLUTE,
    BIND_TEST_STATIC_ABSOLUTE,
    BIND_TEST_STATIC_BLOCKED,
    BIND_TEST_STATIC_BLOCKED_MANY,
    BIND_TEST_STATIC_RULES,
} BindTestStaticRule;

// How many names the two rules of weak definitions define.
#define BUSTER_BIND_TEST_STATIC_BLOCKED 4
#define BUSTER_BIND_TEST_STATIC_BLOCKED_MANY 24

static const char* const bind_test_static_rule_names[BIND_TEST_STATIC_RULES] = {
    [BIND_TEST_STATIC_NOWHERE] = "a strong reference to a name nothing defines",
    [BIND_TEST_STATIC_TLS_GET_ADDR] = "strong and weak references to __tls_get_addr",
    [BIND_TEST_STATIC_LINKER_NAMES] = "references to the static linker's names",
    [BIND_TEST_STATIC_OTHER_KIND] = "a strong reference to a name only the other static kind's linker defines",
    [BIND_TEST_STATIC_WEAK_PUTS] = "a weak reference to puts",
    [BIND_TEST_STATIC_STRONG_PUTS] = "a strong reference to puts",
    [BIND_TEST_STATIC_WEAK_START_MAIN] = "a weak definition of __libc_start_main",
    [BIND_TEST_STATIC_WEAK_ABSOLUTE] = "a weak absolute definition of data_start, which crt1.o defines weakly before it",
    [BIND_TEST_STATIC_ABSOLUTE] = "a strong reference to a member's absolute name",
    [BIND_TEST_STATIC_BLOCKED] = "weak definitions of 4 names that the members refer to",
    [BIND_TEST_STATIC_BLOCKED_MANY] = "weak definitions of 24 names that the members refer to",
};

// The kind's linker's names the rule refers to, a static executable's first, then a static PIE's.
#define BUSTER_BIND_TEST_STATIC_LINKER_NAMES 5
static const char* const bind_test_static_linker_names[2][BUSTER_BIND_TEST_STATIC_LINKER_NAMES] = {
    {
        "__ehdr_start",
        "__rela_iplt_start",
        "__rela_iplt_end",
        "_end",
        "etext",
    },
    {
        "__ehdr_start",
        "_DYNAMIC",
        "__preinit_array_start",
        "_end",
        "etext",
    },
};

// A name only the other static kind's linker defines, a static executable's first.
static const char* const bind_test_static_other_kind_names[2] = {
    "_DYNAMIC",
    "__rela_iplt_start",
};

// The first global symbol of a member that is absolute, or 0.
static SymbolId bind_test_static_absolute(const Runtime* runtime)
{
    const LinkSymbols* symbols = &runtime->link.symbols;
    SymbolId result = 0;
    for (SymbolId symbol = runtime->pieces[RUNTIME_OBJECTS].begin.symbols; result == 0 && symbol < symbols->count; symbol += 1)
    {
        const bool global = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL && symbols->name[symbol] != 0;
        result = global && symbols->source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE ? symbol : 0;
    }
    return result;
}

// Builds the rule's link (main and the rule's symbols), works out GNU ld's members for it, then binds it by `path`.
static void bind_test_static_rule_link(BindTestLink* test, const Runtime* runtime, BindTestStaticRule rule, BindPath path, uint64_t* expected, Arena* arena, SymbolId* symbols)
{
    bool result = bind_test_link_begin(test, runtime, bind_test_room(runtime));
    const SymbolId main = result ? bind_test_symbol(test, "main", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM) : 0;
    result = main != 0 && bind_test_define(test, main);
    const uint8_t strong = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE;
    const uint8_t weak = BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_NOTYPE;
    const bool pie = runtime->kind == RUNTIME_KIND_STATIC_PIE;
    memset(symbols, 0, BUSTER_BIND_TEST_STATIC_LINKER_NAMES * sizeof(SymbolId));
    if (result && rule == BIND_TEST_STATIC_NOWHERE)
    {
        symbols[0] = bind_test_symbol(test, "buster_static_nowhere", strong, BUSTER_LINK_SOURCE_NONE);
    }
    else if (result && rule == BIND_TEST_STATIC_OTHER_KIND)
    {
        symbols[0] = bind_test_symbol(test, bind_test_static_other_kind_names[pie], strong, BUSTER_LINK_SOURCE_NONE);
    }
    else if (result && rule == BIND_TEST_STATIC_TLS_GET_ADDR)
    {
        symbols[0] = bind_test_symbol(test, "__tls_get_addr", strong, BUSTER_LINK_SOURCE_NONE);
    }
    else if (result && rule == BIND_TEST_STATIC_LINKER_NAMES)
    {
        for (uint32_t i = 0; i < BUSTER_BIND_TEST_STATIC_LINKER_NAMES; i += 1)
        {
            symbols[i] = bind_test_symbol(test, bind_test_static_linker_names[pie][i], strong, BUSTER_LINK_SOURCE_NONE);
        }
    }
    else if (result && (rule == BIND_TEST_STATIC_WEAK_PUTS || rule == BIND_TEST_STATIC_STRONG_PUTS))
    {
        symbols[0] = bind_test_symbol(test, "puts", rule == BIND_TEST_STATIC_WEAK_PUTS ? weak : strong, BUSTER_LINK_SOURCE_NONE);
    }
    else if (result && rule == BIND_TEST_STATIC_WEAK_START_MAIN)
    {
        symbols[0] = bind_test_symbol(test, "__libc_start_main", BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM);
        result = symbols[0] != 0 && bind_test_define(test, symbols[0]);
    }
    else if (result && (rule == BIND_TEST_STATIC_BLOCKED || rule == BIND_TEST_STATIC_BLOCKED_MANY))
    {
        // The names the members GNU ld loads for main alone refer to that members define, the first `count` of them, but
        // not thread-local ones, which the program's functions can't define (__libc_errno, on the 9700X).
        const uint32_t count = rule == BIND_TEST_STATIC_BLOCKED ? BUSTER_BIND_TEST_STATIC_BLOCKED : BUSTER_BIND_TEST_STATIC_BLOCKED_MANY;
        const RuntimeNames* names = &runtime->global_names;
        bind_test_static_expected(runtime, &test->link, expected, arena);
        uint32_t chosen[BUSTER_BIND_TEST_STATIC_BLOCKED_MANY];
        uint32_t chosen_count = 0;
        for (uint32_t m = 0; chosen_count < count && m < runtime->member_count; m += 1)
        {
            const uint32_t piece = RUNTIME_OBJECTS + m;
            for (uint32_t r = runtime->reference_begin[piece]; bind_test_has(expected, m) && chosen_count < count && r < runtime->reference_begin[piece + 1]; r += 1)
            {
                const uint32_t entry = runtime->references[r];
                const SymbolId definition = names->definition[entry];
                const bool tls = definition != 0 && (runtime->link.symbols.flags[definition] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_TLS;
                bool skipped = names->member[entry] == 0 || names->linker[entry] || tls;
                for (uint32_t c = 0; !skipped && c < chosen_count; c += 1)
                {
                    skipped = chosen[c] == entry;
                }
                chosen[chosen_count] = entry;
                chosen_count += skipped ? 0 : 1;
            }
        }
        result = chosen_count == count;
        for (uint32_t c = 0; result && c < chosen_count; c += 1)
        {
            char text[256];
            snprintf(text, sizeof(text), "%.*s", (int)names->length[chosen[c]], (const char*)runtime_global_name(names, chosen[c]));
            const SymbolId defined = bind_test_symbol(test, text, BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM);
            result = defined != 0 && bind_test_define(test, defined);
            symbols[0] = c == 0 ? defined : symbols[0];
        }
    }
    else if (result && rule == BIND_TEST_STATIC_WEAK_ABSOLUTE)
    {
        symbols[0] = bind_test_weak_absolute(test, "data_start");
    }
    else if (result && rule == BIND_TEST_STATIC_ABSOLUTE)
    {
        const SymbolId absolute = bind_test_static_absolute(runtime);
        const NameId name = absolute != 0 ? runtime->link.symbols.name[absolute] : 0;
        char text[256];
        snprintf(text, sizeof(text), "%.*s", name != 0 ? (int)runtime->link.names.length[name] : 0, name != 0 ? (const char*)runtime->link.names.bytes[name] : "");
        symbols[0] = name != 0 ? bind_test_symbol(test, text, strong, BUSTER_LINK_SOURCE_NONE) : 0;
    }
    result = result && symbols[0] != 0;
    if (result)
    {
        bind_test_static_expected(runtime, &test->link, expected, arena);
    }
    result = result && bind_begin(&test->bind, &test->link, runtime, path, &test->bytes, 1, &test->arena);
    if (result)
    {
        bind_end(&test->bind, &test->link);
    }
}

static bool bind_test_static_rules(const Runtime* runtime, Arena* arena)
{
    BindTestTally tally = {
        .check = runtime->kind == RUNTIME_KIND_STATIC_PIE ? "static PIE rules" : "static rules",
    };
    const uint32_t words = (runtime->member_count + 63) / 64;
    uint64_t* expected = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, words + 1);
    uint64_t* exact_expected = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, words + 1);
    const RuntimeNames* names = &runtime->global_names;
    for (uint32_t r = 0; expected != 0 && exact_expected != 0 && r < BIND_TEST_STATIC_RULES; r += 1)
    {
        const BindTestStaticRule rule = (BindTestStaticRule)r;
        const char* what = bind_test_static_rule_names[r];
        BindTestLink vector;
        BindTestLink exact;
        SymbolId symbols[BUSTER_BIND_TEST_STATIC_LINKER_NAMES];
        SymbolId exact_symbols[BUSTER_BIND_TEST_STATIC_LINKER_NAMES];
        bind_test_static_rule_link(&vector, runtime, rule, BIND_PATH_VECTOR, expected, arena, symbols);
        bind_test_static_rule_link(&exact, runtime, rule, BIND_PATH_EXACT, exact_expected, arena, exact_symbols);
        const Link* link = &vector.link;
        const LinkSymbols* columns = &link->symbols;
        const SymbolId symbol = symbols[0];
        bind_test_expect(&tally, bind_test_same_links(&vector.link, &exact.link) && bind_test_same_binds(&vector.bind, &exact.bind), what, r);
        bool members = true;
        for (uint32_t w = 0; w < words; w += 1)
        {
            members = members && vector.bind.members_joined[w] == expected[w];
        }
        bool holds = false;
        if (rule == BIND_TEST_STATIC_NOWHERE || rule == BIND_TEST_STATIC_OTHER_KIND)
        {
            holds = vector.bind.error == BIND_ERROR_UNDEFINED && symbol != 0 && vector.bind.name == columns->name[symbol];
        }
        else if (rule == BIND_TEST_STATIC_TLS_GET_ADDR)
        {
            holds = vector.bind.error == BIND_ERROR_NONE && members && columns->source[symbol] == BUSTER_LINK_SOURCE_NONE && columns->atom[symbol] == 0 &&
                    (columns->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL;
        }
        else if (rule == BIND_TEST_STATIC_LINKER_NAMES)
        {
            holds = vector.bind.error == BIND_ERROR_NONE && members;
            for (uint32_t i = 0; i < BUSTER_BIND_TEST_STATIC_LINKER_NAMES; i += 1)
            {
                holds = holds && symbols[i] != 0 && columns->source[symbols[i]] == BUSTER_LINK_SOURCE_LINKER && columns->atom[symbols[i]] == 0;
            }
        }
        else if (rule == BIND_TEST_STATIC_WEAK_PUTS || rule == BIND_TEST_STATIC_STRONG_PUTS)
        {
            const uint32_t entry = runtime_find_global_name(names, (const uint8_t*)"puts", 4, link_hash((const uint8_t*)"puts", 4));
            const uint32_t member = entry != 0 ? names->member[entry - 1] : 0;
            const bool loaded = member != 0 && bind_test_has(expected, member - 1);
            const AtomId atom = columns->atom[symbol];
            const bool defined = atom != 0 && link->atoms.unit[atom] == 1 + runtime->member_unit + member - 1;
            holds = vector.bind.error == BIND_ERROR_NONE && members && member != 0 && (rule == BIND_TEST_STATIC_WEAK_PUTS || loaded) &&
                    (loaded ? defined : columns->source[symbol] == BUSTER_LINK_SOURCE_NONE && atom == 0);
        }
        else if (rule == BIND_TEST_STATIC_WEAK_START_MAIN)
        {
            const uint32_t entry = runtime_find_global_name(names, (const uint8_t*)"__libc_start_main", 17, link_hash((const uint8_t*)"__libc_start_main", 17));
            const uint32_t member = entry != 0 ? names->member[entry - 1] : 0;
            holds = vector.bind.error == BIND_ERROR_NONE && members && member != 0 && !bind_test_has(vector.bind.members_joined, member - 1) &&
                    columns->source[symbol] == BUSTER_LINK_SOURCE_PROGRAM && columns->atom[symbol] != 0 && link->atoms.unit[columns->atom[symbol]] == 0;
        }
        else if (rule == BIND_TEST_STATIC_BLOCKED || rule == BIND_TEST_STATIC_BLOCKED_MANY)
        {
            holds = vector.bind.error == BIND_ERROR_NONE && members && link_check(&vector.link) == LINK_ERROR_NONE;
        }
        else if (rule == BIND_TEST_STATIC_WEAK_ABSOLUTE)
        {
            const AtomId atom = columns->atom[symbol];
            holds = vector.bind.error == BIND_ERROR_NONE && members && atom != 0 && link->atoms.unit[atom] == 1 + RUNTIME_OBJECT_START &&
                    columns->source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE && (columns->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK &&
                    bind_test_same_place(link, symbol, "__data_start") && link_check(&vector.link) == LINK_ERROR_NONE;
        }
        else
        {
            const SymbolId absolute = bind_test_static_absolute(runtime);
            holds = vector.bind.error == BIND_ERROR_NONE && members && absolute != 0 && columns->source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE && columns->atom[symbol] == 0 &&
                    columns->value[symbol] == runtime->link.symbols.value[absolute];
        }
        bind_test_expect(&tally, holds, what, vector.bind.error);
        bind_test_link_end(&vector);
        bind_test_link_end(&exact);
    }
    return bind_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared libraries
//
// A shared library's runtime (runtime_import_shared), against which each link binds by both paths, which must make the
// same link and binding:
//   - programs: test-link's two sets that call libc, bound as libraries (bind_test_shared_structure): link_check; no
//     start object's _start; crti.o's _init and crtbeginS.o's __dso_handle, hidden; and the exports exactly the link's
//     definitions with default or protected visibility, global or weak, in symbol order, main among them;
//   - rules, on links made by hand, none with main: a strong reference with default visibility to a name nothing
//     defines stays undefined (source NONE), and a hidden one is an error naming it; definitions with default,
//     protected and hidden visibility and a weak one, all but the hidden one exports; and references to __tls_get_addr
//     and puts, imports of the loader's at GLIBC_2.3 and of libc.so.6's at GLIBC_2.2.5;
//   - with --cross-check: C libraries that cc compiles with -fPIC (exports of every visibility; imports from libc.so.6
//     and the loader, general-dynamic TLS, and undefined and weak references; atexit), each imported, bound and checked
//     as the programs are, then linked by cc -shared with GNU ld, which must link it, and with lld, whose .dynsym must
//     have the imports at their versions, the undefined and weak references and the exports binding found, and whose
//     --why-extract the members that joined.

// Whether a symbol of a link bound against a shared library's runtime is an export: global or weak, with default or
// protected visibility, and defined in an atom or absolutely.
static bool bind_test_shared_exported(const Link* link, SymbolId symbol)
{
    const uint8_t flags = link->symbols.flags[symbol];
    const uint8_t visibility = flags & BUSTER_LINK_SYMBOL_VISIBILITY;
    const bool global = (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
    const bool visible = visibility == BUSTER_LINK_SYMBOL_DEFAULT || visibility == BUSTER_LINK_SYMBOL_PROTECTED;
    const bool defined = link->symbols.atom[symbol] != 0 || link->symbols.source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE;
    return global && visible && defined;
}

// Whether `symbol` is one of the binding's exports.
static bool bind_test_is_export(const Bind* bind, SymbolId symbol)
{
    bool result = false;
    for (uint32_t e = 0; e < bind->export_count; e += 1)
    {
        result = result || bind->exports[e] == symbol;
    }
    return result;
}

// A link bound against a shared library's runtime (see the section's comment).
static void bind_test_shared_structure(BindTestTally* tally, Link* link, const Bind* bind)
{
    bind_test_expect(tally, link_check(link) == LINK_ERROR_NONE, "link_check", 0);
    const SymbolId start = bind_test_global(link, "_start");
    bind_test_expect(tally, start == 0 || link->symbols.atom[start] == 0, "no _start: no start object", start);
    static const char* const hidden[] = {
        "_init",
        "_fini",
        "__dso_handle",
    };
    for (uint32_t h = 0; h < BUSTER_ARRAY_LENGTH(hidden); h += 1)
    {
        const SymbolId symbol = bind_test_global(link, hidden[h]);
        bind_test_expect(tally,
                         symbol != 0 && link->symbols.atom[symbol] != 0 && (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_HIDDEN &&
                             !bind_test_is_export(bind, symbol),
                         "crti.o's _init and _fini and crtbeginS.o's __dso_handle, hidden", h);
    }
    // The exports, in symbol order: each a definition of the program's or absolute.
    uint32_t expected = 0;
    bool same = true;
    for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
    {
        if (bind_test_shared_exported(link, symbol))
        {
            const uint8_t source = link->symbols.source[symbol];
            same = same && expected < bind->export_count && bind->exports[expected] == symbol &&
                   (source == BUSTER_LINK_SOURCE_PROGRAM || source == BUSTER_LINK_SOURCE_ABSOLUTE);
            expected += 1;
        }
    }
    bind_test_expect(tally, same && expected == bind->export_count, "the exports: every definition with default or protected visibility", expected);
    const SymbolId main = bind_test_global(link, "main");
    bind_test_expect(tally, main == 0 || link->symbols.atom[main] == 0 || bind_test_is_export(bind, main), "main exported", main);
}

static bool bind_test_shared_programs(const Runtime* runtime, uint64_t seed)
{
    BindTestTally tally = {
        .check = "shared programs",
    };
    LinkProgramOptions options[BUSTER_LINK_TEST_PROGRAMS];
    link_test_program_options(options, seed, 1);
    for (uint32_t i = 0; i < BUSTER_LINK_TEST_PROGRAMS; i += 1)
    {
        if (options[i].libc)
        {
            BindTestProgram vector;
            BindTestProgram exact;
            const bool vector_bound = bind_test_program(&vector, runtime, options[i], BIND_PATH_VECTOR);
            const bool exact_bound = bind_test_program(&exact, runtime, options[i], BIND_PATH_EXACT);
            bind_test_expect(&tally, vector_bound && exact_bound, link_test_program_names[i], vector.bind.error);
            bind_test_expect(&tally, bind_test_same_links(&vector.program.link, &exact.program.link) && bind_test_same_binds(&vector.bind, &exact.bind),
                             "the paths' links and bindings the same", i);
            if (vector_bound)
            {
                bind_test_shared_structure(&tally, &vector.program.link, &vector.bind);
                printf("  %s: %u atoms, %u symbols, %u imports, %u exports\n", link_test_program_names[i], vector.program.link.atoms.count - 1,
                       vector.program.link.symbols.count - 1, vector.bind.import_count, vector.bind.export_count);
            }
            bind_test_release(&vector);
            bind_test_release(&exact);
        }
    }
    return bind_test_report(&tally);
}

typedef enum BindTestSharedRule
{
    BIND_TEST_SHARED_NOWHERE,
    BIND_TEST_SHARED_HIDDEN_NOWHERE,
    BIND_TEST_SHARED_VISIBILITY,
    BIND_TEST_SHARED_IMPORTS,
    BIND_TEST_SHARED_RULES,
} BindTestSharedRule;

static const char* const bind_test_shared_rule_names[BIND_TEST_SHARED_RULES] = {
    [BIND_TEST_SHARED_NOWHERE] = "a strong reference with default visibility to a name nothing defines",
    [BIND_TEST_SHARED_HIDDEN_NOWHERE] = "a hidden reference to a name nothing defines",
    [BIND_TEST_SHARED_VISIBILITY] = "definitions with default, protected and hidden visibility, and a weak one",
    [BIND_TEST_SHARED_IMPORTS] = "references to __tls_get_addr and puts",
};

// The most symbols a shared rule's link names.
#define BUSTER_BIND_TEST_SHARED_SYMBOLS 4

// Builds the rule's link, without main, and binds it by `path`: `symbols` gets the rule's symbols.
static void bind_test_shared_rule_link(BindTestLink* test, const Runtime* runtime, BindTestSharedRule rule, BindPath path, SymbolId* symbols)
{
    static const char* const definitions[BUSTER_BIND_TEST_SHARED_SYMBOLS] = {
        "buster_default",
        "buster_protected",
        "buster_hidden",
        "buster_weak",
    };
    static const uint8_t definition_flags[BUSTER_BIND_TEST_SHARED_SYMBOLS] = {
        BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION,
        BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_PROTECTED,
        BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_HIDDEN,
        BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_FUNCTION,
    };
    const uint8_t strong = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE;
    bool result = bind_test_link_begin(test, runtime, bind_test_room(runtime));
    memset(symbols, 0, BUSTER_BIND_TEST_SHARED_SYMBOLS * sizeof(SymbolId));
    if (result && rule == BIND_TEST_SHARED_NOWHERE)
    {
        symbols[0] = bind_test_symbol(test, "buster_shared_nowhere", strong, BUSTER_LINK_SOURCE_NONE);
    }
    else if (result && rule == BIND_TEST_SHARED_HIDDEN_NOWHERE)
    {
        symbols[0] = bind_test_symbol(test, "buster_shared_hidden", strong | BUSTER_LINK_SYMBOL_HIDDEN, BUSTER_LINK_SOURCE_NONE);
    }
    else if (result && rule == BIND_TEST_SHARED_VISIBILITY)
    {
        for (uint32_t i = 0; result && i < BUSTER_BIND_TEST_SHARED_SYMBOLS; i += 1)
        {
            symbols[i] = bind_test_symbol(test, definitions[i], definition_flags[i], BUSTER_LINK_SOURCE_PROGRAM);
            result = symbols[i] != 0 && bind_test_define(test, symbols[i]);
        }
    }
    else if (result)
    {
        symbols[0] = bind_test_symbol(test, "__tls_get_addr", strong, BUSTER_LINK_SOURCE_NONE);
        symbols[1] = bind_test_symbol(test, "puts", strong, BUSTER_LINK_SOURCE_NONE);
    }
    result = result && symbols[0] != 0 && bind_begin(&test->bind, &test->link, runtime, path, &test->bytes, 1, &test->arena);
    if (result)
    {
        bind_end(&test->bind, &test->link);
    }
}

static bool bind_test_shared_rules(const Runtime* runtime)
{
    BindTestTally tally = {
        .check = "shared rules",
    };
    for (uint32_t r = 0; r < BIND_TEST_SHARED_RULES; r += 1)
    {
        const BindTestSharedRule rule = (BindTestSharedRule)r;
        const char* what = bind_test_shared_rule_names[r];
        BindTestLink vector;
        BindTestLink exact;
        SymbolId symbols[BUSTER_BIND_TEST_SHARED_SYMBOLS];
        SymbolId exact_symbols[BUSTER_BIND_TEST_SHARED_SYMBOLS];
        bind_test_shared_rule_link(&vector, runtime, rule, BIND_PATH_VECTOR, symbols);
        bind_test_shared_rule_link(&exact, runtime, rule, BIND_PATH_EXACT, exact_symbols);
        const LinkSymbols* columns = &vector.link.symbols;
        const bool bound = vector.bind.error == BIND_ERROR_NONE;
        bind_test_expect(&tally, bind_test_same_links(&vector.link, &exact.link) && bind_test_same_binds(&vector.bind, &exact.bind), what, r);
        bool holds = false;
        if (rule == BIND_TEST_SHARED_NOWHERE)
        {
            bool imported = false;
            for (uint32_t i = 0; i < vector.bind.import_count; i += 1)
            {
                imported = imported || vector.bind.imports[i] == symbols[0];
            }
            holds = bound && columns->source[symbols[0]] == BUSTER_LINK_SOURCE_NONE && columns->atom[symbols[0]] == 0 && !imported &&
                    !bind_test_is_export(&vector.bind, symbols[0]);
        }
        else if (rule == BIND_TEST_SHARED_HIDDEN_NOWHERE)
        {
            holds = vector.bind.error == BIND_ERROR_UNDEFINED && vector.bind.name == columns->name[symbols[0]];
        }
        else if (rule == BIND_TEST_SHARED_VISIBILITY)
        {
            holds = bound && vector.bind.export_count == 3 && vector.bind.exports[0] == symbols[0] && vector.bind.exports[1] == symbols[1] &&
                    vector.bind.exports[2] == symbols[3];
        }
        else
        {
            const uint16_t tls = columns->version[symbols[0]];
            const uint16_t puts = columns->version[symbols[1]];
            holds = bound && columns->source[symbols[0]] == BUSTER_LINK_SOURCE_RUNTIME_SHARED && columns->source[symbols[1]] == BUSTER_LINK_SOURCE_RUNTIME_SHARED &&
                    import_equal(import_test_version_name(&runtime->loader, tls), "GLIBC_2.3") && import_equal(import_test_version_name(&runtime->libc, puts), "GLIBC_2.2.5");
        }
        bind_test_expect(&tally, holds, what, vector.bind.error);
        if (bound)
        {
            bind_test_shared_structure(&tally, &vector.link, &vector.bind);
        }
        bind_test_link_end(&vector);
        bind_test_link_end(&exact);
    }
    return bind_test_report(&tally);
}

// The C libraries the cross-check compiles with -fPIC: each one's name, source and an option for cc.
static const BindTestStaticSource bind_test_shared_sources[] = {
    {
        .name = "exports",
        .source = "int buster_counter = 1;\n"
                  "int buster_get(void) { return buster_counter; }\n"
                  "__attribute__((visibility(\"hidden\"))) int buster_hidden(void) { return 2; }\n"
                  "__attribute__((visibility(\"protected\"))) int buster_protected(void) { return buster_hidden(); }\n"
                  "__attribute__((weak)) int buster_weak(void) { return 3; }\n"
                  "static int buster_local(void) { return 4; }\n"
                  "int (*buster_pointer)(void) = buster_local;\n",
    },
    {
        .name = "imports",
        .source = "#include <link.h>\n"
                  "#include <stdio.h>\n"
                  "#include <string.h>\n"
                  "extern char** environ;\n"
                  "extern void* __libc_stack_end;\n"
                  "extern int buster_elsewhere(int);\n"
                  "extern int buster_maybe __attribute__((weak));\n"
                  "__thread int buster_thread;\n"
                  "int buster_library(const char* text)\n"
                  "{\n"
                  "    puts(text);\n"
                  "    return (int)strlen(text) + (environ != 0) + _r_debug.r_version + (__libc_stack_end != 0) + buster_elsewhere(buster_thread) +\n"
                  "           (&buster_maybe != 0);\n"
                  "}\n",
        // The general-dynamic accesses, through __tls_get_addr, rather than TLS descriptors.
        .option = "-mtls-dialect=gnu",
    },
    {
        .name = "atexit",
        .source = "#include <stdlib.h>\n"
                  "static void buster_handler(void) {}\n"
                  "int buster_register(void) { return atexit(buster_handler); }\n",
    },
};

// Links `object` by cc -shared with the linker `fuse`, into `library`, with `why` if it isn't 0: cc's status.
static int bind_test_shared_link(const char* fuse, const char* why, const char* object, const char* library, const char* out, const char* err)
{
    char* arguments[8];
    uint32_t count = 0;
    arguments[count++] = "cc";
    arguments[count++] = (char*)fuse;
    arguments[count++] = "-shared";
    arguments[count] = (char*)why;
    count += why != 0 ? 1 : 0;
    arguments[count++] = "-o";
    arguments[count++] = (char*)library;
    arguments[count++] = (char*)object;
    arguments[count] = 0;
    return object_test_run(arguments, out, err);
}

// What one library's link must have besides the structure: its references and members bound as GNU ld and lld bind
// them (see the section's comment).
static bool bind_test_shared_library_holds(const Link* link, const Bind* bind, const Runtime* runtime, const char* name)
{
    const LinkSymbols* symbols = &link->symbols;
    bool result = true;
    if (strcmp(name, "exports") == 0)
    {
        static const char* const exported[] = {
            "buster_counter",
            "buster_get",
            "buster_protected",
            "buster_weak",
            "buster_pointer",
        };
        for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(exported); e += 1)
        {
            result = result && bind_test_is_export(bind, bind_test_global(link, exported[e]));
        }
        const SymbolId hidden = bind_test_global(link, "buster_hidden");
        result = result && hidden != 0 && !bind_test_is_export(bind, hidden) && bind->export_count == BUSTER_ARRAY_LENGTH(exported);
    }
    else if (strcmp(name, "imports") == 0)
    {
        const SymbolId elsewhere = bind_test_global(link, "buster_elsewhere");
        const SymbolId maybe = bind_test_global(link, "buster_maybe");
        const SymbolId tls = bind_test_global(link, "__tls_get_addr");
        const SymbolId r_debug = bind_test_global(link, "_r_debug");
        const SymbolId environ_symbol = bind_test_global(link, "environ");
        result = elsewhere != 0 && symbols->source[elsewhere] == BUSTER_LINK_SOURCE_NONE &&
                 (symbols->flags[elsewhere] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL && maybe != 0 &&
                 symbols->source[maybe] == BUSTER_LINK_SOURCE_NONE && (symbols->flags[maybe] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK && tls != 0 &&
                 symbols->source[tls] == BUSTER_LINK_SOURCE_RUNTIME_SHARED && import_equal(import_test_version_name(&runtime->loader, symbols->version[tls]), "GLIBC_2.3") &&
                 r_debug != 0 && import_test_version_name(&runtime->loader, symbols->version[r_debug]).length != 0 && environ_symbol != 0 &&
                 import_test_version_name(&runtime->libc, symbols->version[environ_symbol]).length != 0 && bind_test_is_export(bind, bind_test_global(link, "buster_thread"));
    }
    else
    {
        const SymbolId atexit = bind_test_global(link, "atexit");
        const SymbolId cxa_atexit = bind_test_global(link, "__cxa_atexit");
        result = atexit != 0 && symbols->atom[atexit] != 0 && !bind_test_is_export(bind, atexit) && cxa_atexit != 0 &&
                 symbols->source[cxa_atexit] == BUSTER_LINK_SOURCE_RUNTIME_SHARED;
    }
    return result;
}

static bool bind_test_shared_compiled(const Runtime* runtime, const char* directory, bool keep, Arena* arena)
{
    BindTestTally tally = {
        .check = "shared compiled libraries",
    };
    BindTestDynamic* entries = BUSTER_ARENA_PUSH_ARRAY(arena, BindTestDynamic, BUSTER_BIND_TEST_DYNAMIC);
    for (uint32_t s = 0; entries != 0 && s < BUSTER_ARRAY_LENGTH(bind_test_shared_sources); s += 1)
    {
        const BindTestStaticSource* library = &bind_test_shared_sources[s];
        const uint64_t length = arena->length;
        char source_path[BUSTER_BIND_TEST_PATH_BYTES];
        char object_path[BUSTER_BIND_TEST_PATH_BYTES];
        char bfd_path[BUSTER_BIND_TEST_PATH_BYTES];
        char lld_path[BUSTER_BIND_TEST_PATH_BYTES];
        char why_path[BUSTER_BIND_TEST_PATH_BYTES];
        char why_option[BUSTER_BIND_TEST_PATH_BYTES + 32];
        char out[BUSTER_BIND_TEST_PATH_BYTES];
        char err[BUSTER_BIND_TEST_PATH_BYTES];
        snprintf(source_path, sizeof(source_path), "%s/shared-%s.c", directory, library->name);
        snprintf(object_path, sizeof(object_path), "%s/shared-%s.o", directory, library->name);
        snprintf(bfd_path, sizeof(bfd_path), "%s/shared-%s-bfd.so", directory, library->name);
        snprintf(lld_path, sizeof(lld_path), "%s/shared-%s-lld.so", directory, library->name);
        snprintf(why_path, sizeof(why_path), "%s/shared-%s.why", directory, library->name);
        snprintf(why_option, sizeof(why_option), "-Wl,--why-extract=%s", why_path);
        snprintf(out, sizeof(out), "%s/shared-%s.out", directory, library->name);
        snprintf(err, sizeof(err), "%s/shared-%s.err", directory, library->name);
        FILE* file = fopen(source_path, "w");
        const bool written = file != 0 && fputs(library->source, file) >= 0;
        if (file != 0)
        {
            fclose(file);
        }
        // cc, -O2, -fPIC, -c, -o, the object, the source, the option and the null after them.
        char* compile[9];
        uint32_t used = 0;
        compile[used++] = "cc";
        compile[used++] = "-O2";
        compile[used++] = "-fPIC";
        compile[used++] = "-c";
        compile[used++] = "-o";
        compile[used++] = object_path;
        compile[used++] = source_path;
        compile[used] = (char*)library->option;
        used += library->option != 0 ? 1 : 0;
        compile[used] = 0;
        const bool compiled = written && object_test_run(compile, out, err) == 0;
        bind_test_expect(&tally, compiled, library->name, s);
        const String8 object = compiled ? object_test_read_file(arena, object_path) : (String8){ 0 };
        BindTestLink vector;
        BindTestLink exact;
        const bool vector_imported = object.pointer != 0 && bind_test_static_import(&vector, runtime, object);
        const bool exact_imported = object.pointer != 0 && bind_test_static_import(&exact, runtime, object);
        bind_test_expect(&tally, vector_imported && exact_imported, "the object imported", s);
        const bool vector_bound = vector_imported && bind_begin(&vector.bind, &vector.link, runtime, BIND_PATH_VECTOR, &vector.bytes, 1, &vector.arena) &&
                                  bind_end(&vector.bind, &vector.link);
        const bool exact_bound = exact_imported && bind_begin(&exact.bind, &exact.link, runtime, BIND_PATH_EXACT, &exact.bytes, 1, &exact.arena) &&
                                 bind_end(&exact.bind, &exact.link);
        bind_test_expect(&tally, vector_bound && exact_bound, "bound by both paths", vector.bind.error);
        if (vector_bound && exact_bound)
        {
            bind_test_expect(&tally, bind_test_same_links(&vector.link, &exact.link) && bind_test_same_binds(&vector.bind, &exact.bind), "the paths' links the same", s);
            bind_test_shared_structure(&tally, &vector.link, &vector.bind);
            bind_test_expect(&tally, bind_test_shared_library_holds(&vector.link, &vector.bind, runtime, library->name), library->name, s);
            bind_test_expect(&tally, bind_test_shared_link("-fuse-ld=bfd", 0, object_path, bfd_path, out, err) == 0, "linked by cc -shared with GNU ld", s);
            const int status = bind_test_shared_link("-fuse-ld=lld", why_option, object_path, lld_path, out, err);
            bind_test_expect(&tally, status == 0, "linked by cc -shared with lld", (uint64_t)status);
            char* readelf[] = {
                "readelf",
                "-W",
                "--dyn-syms",
                lld_path,
                0,
            };
            const int read_status = status == 0 ? object_test_run(readelf, out, err) : -1;
            const String8 text = read_status == 0 ? object_test_read_file(arena, out) : (String8){ 0 };
            const uint32_t entry_count = text.pointer != 0 ? bind_test_read_dynamic(text, entries) : UINT32_MAX;
            bind_test_expect(&tally, bind_test_same_dynamic(&vector.link, &vector.bind, runtime, entries, entry_count), "lld's .dynsym: the imports, references and exports",
                             entry_count);
            const String8 why = status == 0 ? object_test_read_file(arena, why_path) : (String8){ 0 };
            bind_test_expect(&tally, bind_test_same_members(why, &vector.bind, runtime), "the members lld extracts", s);
            printf("  %s: %u imports, %u exports, %u .dynsym entries of lld's\n", library->name, vector.bind.import_count, vector.bind.export_count,
                   entry_count != UINT32_MAX ? entry_count : 0);
        }
        if (object.pointer != 0)
        {
            bind_test_link_end(&vector);
            bind_test_link_end(&exact);
        }
        if (!keep)
        {
            unlink(source_path);
            unlink(object_path);
            unlink(bfd_path);
            unlink(lld_path);
            unlink(why_path);
            unlink(out);
            unlink(err);
        }
        arena_pop_to(arena, length);
    }
    return bind_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// run_bind_tests

static int run_bind_tests(int argc, char** argv)
{
    uint64_t seed = 1;
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
    char directory[BUSTER_BIND_TEST_PATH_BYTES];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-bind-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    Arena arena = arena_reserve(BUSTER_BIND_TEST_ARENA_BYTES);
    Runtime runtime;
    const RuntimeError imported = valid ? runtime_import(&runtime) : RUNTIME_ERROR_NONE;
    if (!valid)
    {
        fprintf(stderr, "usage: test-bind [--seed N] [--cross-check] [--keep]\n");
    }
    else if (imported != RUNTIME_ERROR_NONE)
    {
        fprintf(stderr, "the runtime didn't import: error %d, %s\n", (int)imported, runtime.failed_file);
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
        bool passed = bind_test_programs(&runtime, seed, cross_check, directory, keep, &arena);
        passed = bind_test_rules(&runtime) && passed;
        passed = bind_test_libraries(&runtime, directory) && passed;
        passed = bind_test_atexit_program(&runtime) && passed;
        if (cross_check)
        {
            passed = bind_test_atexit_cross_check(&runtime, directory, keep, &arena) && passed;
        }
        passed = bind_test_linker_cases(&runtime, cross_check, directory, keep, &arena) && passed;
        // The static runtime.
        Runtime static_runtime;
        const RuntimeError static_imported = runtime_import_static(&static_runtime);
        printf("%s", static_imported != RUNTIME_ERROR_NONE ? "static programs: the static runtime didn't import\n" : "");
        passed = static_imported == RUNTIME_ERROR_NONE && passed;
        if (static_imported == RUNTIME_ERROR_NONE)
        {
            passed = bind_test_static_programs(&static_runtime, seed, cross_check, directory, keep, &arena) && passed;
            passed = bind_test_static_rules(&static_runtime, &arena) && passed;
        }
        if (static_imported == RUNTIME_ERROR_NONE && cross_check)
        {
            passed = bind_test_static_compiled(&static_runtime, directory, keep, &arena) && passed;
        }
        runtime_release(&static_runtime);
        // The static PIE's runtime: the same archives behind rcrt1.o, crtbeginS.o and crtendS.o.
        Runtime static_pie_runtime;
        const RuntimeError static_pie_imported = runtime_import_static_pie(&static_pie_runtime);
        printf("%s", static_pie_imported != RUNTIME_ERROR_NONE ? "static PIE programs: the static PIE's runtime didn't import\n" : "");
        passed = static_pie_imported == RUNTIME_ERROR_NONE && passed;
        if (static_pie_imported == RUNTIME_ERROR_NONE)
        {
            passed = bind_test_static_programs(&static_pie_runtime, seed, cross_check, directory, keep, &arena) && passed;
            passed = bind_test_static_rules(&static_pie_runtime, &arena) && passed;
        }
        if (static_pie_imported == RUNTIME_ERROR_NONE && cross_check)
        {
            passed = bind_test_static_compiled(&static_pie_runtime, directory, keep, &arena) && passed;
        }
        runtime_release(&static_pie_runtime);
        // A shared library's runtime: the dynamic PIE's without its start object.
        Runtime shared_runtime;
        const RuntimeError shared_imported = runtime_import_shared(&shared_runtime);
        printf("%s", shared_imported != RUNTIME_ERROR_NONE ? "shared programs: the shared library's runtime didn't import\n" : "");
        passed = shared_imported == RUNTIME_ERROR_NONE && passed;
        if (shared_imported == RUNTIME_ERROR_NONE)
        {
            passed = bind_test_shared_programs(&shared_runtime, seed) && passed;
            passed = bind_test_shared_rules(&shared_runtime) && passed;
        }
        if (shared_imported == RUNTIME_ERROR_NONE && cross_check)
        {
            passed = bind_test_shared_compiled(&shared_runtime, directory, keep, &arena) && passed;
        }
        runtime_release(&shared_runtime);
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
    if (valid)
    {
        runtime_release(&runtime);
    }
    arena_release(&arena);
    return result;
}
