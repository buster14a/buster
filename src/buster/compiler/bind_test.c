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
//     data_start, which Scrt1.o defines weakly, and a weak one, which Scrt1.o's takes over as the first in link order;
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
// The objects and programs go in a new directory under $TMPDIR (or /tmp), removed at the end unless --keep. The exit
// status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally, the programs and their comparisons, the programs' checks, the names, links made by
// hand, the rules, the atexit program, the cross-check, the linkers' cases, and run_bind_tests.

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
    return result;
}

// Whether two bindings ended alike: the error and its name, the imports, the exports and the members that joined.
static bool bind_test_same_binds(const Bind* a, const Bind* b)
{
    const uint32_t words = (a->runtime->nonshared.member_count + 63) / 64;
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
    const bool linker = source == BUSTER_LINK_SOURCE_LINKER && name != 0 && runtime_linker_name(link->names.bytes[name], link->names.length[name]);
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
                    import_equal(import_test_version_name(&runtime->libc, symbols->version[symbol]), bind_test_binding_versions[b]);
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
    const uint32_t member_count = runtime->nonshared.member_count;
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
        result = result && member < runtime->nonshared.member_count && (bind->members_joined[member / 64] >> (member % 64) & 1) != 0;
    }
    else if (which == BIND_TEST_RULE_WEAK_ATFORK)
    {
        bool members = false;
        for (uint32_t w = 0; w < (runtime->nonshared.member_count + 63) / 64; w += 1)
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
        result = rule->ended && atom != 0 && symbols->source[first] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE && member < runtime->nonshared.member_count &&
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
    };
    result = from_symbol != 0 && symbol != 0;
    // Before binding begins, the references join without an error; after, the strong one is an error.
    bind_merge(&bind, &link, from_symbol, symbol, 0);
    result = result && bind.error == BIND_ERROR_NONE && (link.symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL;
    link.symbols.flags[symbol] = BUSTER_LINK_SYMBOL_WEAK;
    bind.resolving = true;
    bind_merge(&bind, &link, from_symbol, symbol, 0);
    result = result && bind.error == BIND_ERROR_UNDEFINED && bind.name == name;
    bind.error = BIND_ERROR_NONE;
    link.symbols.flags[symbol] = BUSTER_LINK_SYMBOL_WEAK;
    link.symbols.source[symbol] = BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE;
    bind_merge(&bind, &link, from_symbol, symbol, 0);
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
// The atexit program
//
// main registers handler with atexit, which libc_nonshared.a's member defines, and returns 3; handler, which exit runs,
// exits with status 0.

#define BUSTER_BIND_TEST_MAIN_STATUS 3
#define BUSTER_BIND_TEST_MAX_CODE 8

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

// The index of libc_nonshared.a's member named `name`, or the member count.
static uint32_t bind_test_member(const Runtime* runtime, const char* name)
{
    uint32_t result = runtime->nonshared.member_count;
    for (uint32_t m = 0; m < runtime->nonshared.member_count; m += 1)
    {
        result = result == runtime->nonshared.member_count && import_equal(runtime->nonshared.members[m].name, name) ? m : result;
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
    const bool joined = member < runtime->nonshared.member_count && (bind->members_joined[member / 64] >> (member % 64) & 1) != 0;
    bind_test_expect(tally, program->bound, "the atexit program bound", bind->error);
    bind_test_expect(tally, joined, "atexit.oS joined", member);
    bind_test_expect(tally, atom != 0 && link->atoms.unit[atom] == 1 + RUNTIME_OBJECTS + member && symbols->source[program->atexit] == BUSTER_LINK_SOURCE_RUNTIME_ARCHIVE,
                     "atexit defined by its member", atom);
    const SymbolId cxa_atexit = bind_test_global(link, "__cxa_atexit");
    bind_test_expect(tally, cxa_atexit != 0 && symbols->source[cxa_atexit] == BUSTER_LINK_SOURCE_RUNTIME_SHARED &&
                                import_equal(import_test_version_name(&runtime->libc, symbols->version[cxa_atexit]), "GLIBC_2.2.5"),
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
    uint32_t count = 0;
    arguments[count++] = (char*)linker;
    arguments[count++] = "-pie";
    arguments[count++] = "--dynamic-linker";
    arguments[count++] = (char*)runtime->loader_path.pointer;
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = paths[units + RUNTIME_OBJECT_START];
    arguments[count++] = paths[units + RUNTIME_OBJECT_INIT_PROLOGUE];
    arguments[count++] = paths[units + RUNTIME_OBJECT_BEGIN];
    for (uint32_t unit = 0; unit < units; unit += 1)
    {
        arguments[count++] = paths[unit];
    }
    for (uint32_t m = 0; m < runtime->nonshared.member_count; m += 1)
    {
        if ((bind->members_joined[m / 64] >> (m % 64) & 1) != 0)
        {
            arguments[count++] = paths[units + RUNTIME_OBJECTS + m];
        }
    }
    arguments[count++] = (char*)runtime->libc_path.pointer;
    arguments[count++] = paths[units + RUNTIME_OBJECT_END];
    arguments[count++] = paths[units + RUNTIME_OBJECT_INIT_EPILOGUE];
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
    const uint32_t unit_count = units + RUNTIME_OBJECTS + runtime->nonshared.member_count;
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

// Whether readelf's entries are what binding found: each undefined one an import of the link at that version, with its
// type, or a weak reference to nothing of it with default visibility, with the same binding; each defined one an
// export; and every import, export and such weak reference among them.
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
        const bool visible = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_DEFAULT;
        const bool nowhere = source == BUSTER_LINK_SOURCE_NONE && symbols->atom[symbol] == 0 && name != 0 && weak && visible;
        bool exported = false;
        for (uint32_t i = 0; i < bind->export_count; i += 1)
        {
            exported = exported || bind->exports[i] == symbol;
        }
        if (import || nowhere || exported)
        {
            const String8 version = import ? import_test_version_name(&runtime->libc, symbols->version[symbol]) : (String8){ 0 };
            bool found = false;
            for (uint32_t e = 0; !found && e < count; e += 1)
            {
                const BindTestDynamic* entry = &entries[e];
                found = bind_test_named(link, name, entry->name) && entry->undefined == !exported && (exported || entry->weak == weak) &&
                        (!import || (import_equal(version, entry->version) && entry->type == (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_TYPE))) &&
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
    const uint32_t words = (runtime->nonshared.member_count + 63) / 64;
    uint64_t extracted[4] = { 0 };
    bool result = words <= BUSTER_ARRAY_LENGTH(extracted) && text.pointer != 0;
    for (uint32_t m = 0; result && m < runtime->nonshared.member_count; m += 1)
    {
        char needle[512];
        snprintf(needle, sizeof(needle), "libc_nonshared.a(%.*s)", (int)runtime->nonshared.members[m].name.length, runtime->nonshared.members[m].name.pointer);
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

// Links the program's own objects by cc with lld, as a PIE, and compares the output's .dynsym and the members lld
// extracts with what binding found for the same program.
static void bind_test_against_lld(BindTestTally* tally, const Link* own, uint32_t units, const Link* bound, const Bind* bind, const Runtime* runtime, const char* directory,
                                  const char* name, bool keep, Arena* arena)
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
        arguments[count++] = "-pie";
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

// ---------------------------------------------------------------------------------------------------------------------
// Programs as GNU ld and lld link them
//
// Programs whose names bind by the rules that GNU ld and lld decide for themselves: a weak definition of the program's
// meeting the runtime's (strong, and weak before it in link order), references with a visibility other than default to
// a name only libc.so.6 defines, and references to the names the linker defines. Each is bound by both paths, which must
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
// The checks and run_bind_tests

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
        passed = bind_test_atexit_program(&runtime) && passed;
        if (cross_check)
        {
            passed = bind_test_atexit_cross_check(&runtime, directory, keep, &arena) && passed;
        }
        passed = bind_test_linker_cases(&runtime, cross_check, directory, keep, &arena) && passed;
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
