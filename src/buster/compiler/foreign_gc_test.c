// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Mark-only section GC checks. Include after foreign_gc.c and foreign_test.c's tally helpers, then call
// foreign_gc_test. The FIFO oracle has independent byte marks; scalar/vector results must match its complete maps.
// Original synthetic graphs cover cycles, dead undefined references, roots, shared/absolute/linker symbols, common
// storage, wide addends and priorities. Page-edge map stores and class loads exercise every vector tail. There is no
// generated executable or performance claim: production integration still needs the unwind rewrite and map consumer.

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_FOREIGN_GC_TEST_ARENA_BYTES (16ull << 20)

typedef struct ForeignGcTest ForeignGcTest;
struct ForeignGcTest
{
    Link link;
    Arena source;
    Arena names;
    Arena results;
    Arena oracle;
};

static bool foreign_gc_test_create(ForeignGcTest* test, uint32_t atoms, uint32_t extra_symbols, uint32_t fixups)
{
    *test = (ForeignGcTest){ 0 };
    test->source = arena_reserve(BUSTER_FOREIGN_GC_TEST_ARENA_BYTES);
    test->names = arena_reserve(BUSTER_FOREIGN_GC_TEST_ARENA_BYTES);
    test->results = arena_reserve(BUSTER_FOREIGN_GC_TEST_ARENA_BYTES);
    test->oracle = arena_reserve(BUSTER_FOREIGN_GC_TEST_ARENA_BYTES);
    const LinkCapacity capacity = {
        .names = atoms + extra_symbols,
        .atoms = atoms,
        .symbols = atoms + extra_symbols,
        .fixups = fixups,
        .wide_addends = fixups,
        .priorities = atoms,
    };
    return test->source.base != 0 && test->names.base != 0 && test->results.base != 0 && test->oracle.base != 0 &&
           link_initialize(&test->link, &test->source, &test->names, capacity);
}

static void foreign_gc_test_release(ForeignGcTest* test)
{
    arena_release(&test->source);
    arena_release(&test->names);
    arena_release(&test->results);
    arena_release(&test->oracle);
}

static SymbolId foreign_gc_test_symbol(ForeignGcTest* test, AtomId atom, uint8_t flags, uint8_t source)
{
    char spelling[48];
    const int length = snprintf(spelling, sizeof(spelling), "gc_symbol_%u", test->link.symbols.count);
    const NameId name = link_add_name(&test->link, (const uint8_t*)spelling, (uint32_t)length);
    return link_add_symbol(&test->link, name, atom, 0, atom == 0 ? 0 : test->link.atoms.size[atom], flags, source);
}

static AtomId foreign_gc_test_atom(ForeignGcTest* test, uint8_t class, uint64_t size)
{
    uint8_t* bytes = class == BUSTER_LINK_CLASS_BSS || class == BUSTER_LINK_CLASS_TBSS ? 0 : BUSTER_ARENA_PUSH_ARRAY(&test->source, uint8_t, size);
    if (bytes != 0)
    {
        memset(bytes, 0, size);
    }
    return link_add_atom(&test->link, class, 8, size, bytes, 0);
}

static void foreign_gc_test_oracle(const Link* link, const ForeignGcRoots* roots, const ForeignGc* gc, Arena* arena, ForeignTestTally* tally)
{
    const uint64_t saved_length = arena->length;
    uint8_t* atoms = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, link->atoms.count);
    uint8_t* symbols = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, link->symbols.count);
    uint8_t* fixups = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, link->fixups.count);
    AtomId* queue = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, link->atoms.count);
    const bool allocated = atoms != 0 && symbols != 0 && fixups != 0 && queue != 0;
    foreign_test_expect(tally, allocated, "allocate independent FIFO oracle", 0);
    if (allocated)
    {
        memset(atoms, 0, link->atoms.count);
        memset(symbols, 0, link->symbols.count);
        memset(fixups, 0, link->fixups.count);
        symbols[roots->entry] = roots->entry != 0;
        for (uint32_t i = 0; i < roots->export_count; i += 1)
        {
            symbols[roots->exports[i]] = 1;
        }
        for (uint32_t i = 0; i < roots->retained_count; i += 1)
        {
            atoms[roots->retained[i]] = 1;
        }
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            if (symbols[symbol] != 0 && link->symbols.atom[symbol] != 0)
            {
                atoms[link->symbols.atom[symbol]] = 1;
            }
        }
        uint32_t end = 0;
        for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
        {
            const uint8_t class = link->atoms.class[atom];
            const bool implicit = class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI ||
                                  class == BUSTER_LINK_CLASS_INIT_ARRAY || class == BUSTER_LINK_CLASS_FINI_ARRAY;
            atoms[atom] |= implicit ? 1 : 0;
            if (atoms[atom] != 0)
            {
                queue[end] = atom;
                end += 1;
            }
        }
        for (uint32_t begin = 0; begin < end; begin += 1)
        {
            const AtomId atom = queue[begin];
            for (uint32_t i = 0; i < link->atoms.fixup_count[atom]; i += 1)
            {
                const FixupId fixup = link->atoms.fixup_begin[atom] + i;
                const SymbolId symbol = link->fixups.target[fixup];
                const AtomId target = link->symbols.atom[symbol];
                fixups[fixup] = 1;
                symbols[symbol] = 1;
                if (target != 0 && atoms[target] == 0)
                {
                    atoms[target] = 1;
                    queue[end] = target;
                    end += 1;
                }
            }
        }
        LinkCapacity expected = {
            .names = link->names.count - 1,
        };
        for (AtomId atom = 0; atom < link->atoms.count; atom += 1)
        {
            expected.atoms += atoms[atom] != 0 ? 1 : 0;
            foreign_test_expect(tally, gc->atoms[atom] == (atoms[atom] != 0 ? expected.atoms : 0), "atom map equals FIFO oracle", 0);
        }
        for (SymbolId symbol = 0; symbol < link->symbols.count; symbol += 1)
        {
            const AtomId atom = link->symbols.atom[symbol];
            symbols[symbol] |= atom != 0 && atoms[atom] != 0 ? 1 : 0;
            expected.symbols += symbols[symbol] != 0 ? 1 : 0;
            foreign_test_expect(tally, gc->symbols[symbol] == (symbols[symbol] != 0 ? expected.symbols : 0), "symbol map equals FIFO oracle", 0);
        }
        for (FixupId fixup = 0; fixup < link->fixups.count; fixup += 1)
        {
            expected.fixups += fixups[fixup] != 0 ? 1 : 0;
            expected.wide_addends += fixups[fixup] != 0 && (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_WIDE) != 0 ? 1 : 0;
            foreign_test_expect(tally, gc->fixups[fixup] == (fixups[fixup] != 0 ? expected.fixups : 0), "fixup map equals FIFO oracle", 0);
        }
        for (uint32_t i = 1; i < link->priorities.count; i += 1)
        {
            expected.priorities += atoms[link->priorities.atom[i]] != 0 ? 1 : 0;
        }
        foreign_test_expect(tally, memcmp(&gc->kept, &expected, sizeof(expected)) == 0, "kept capacity equals FIFO oracle", 0);
    }
    arena_pop_to(arena, saved_length);
}

static void foreign_gc_test_compare(ForeignGcTest* test, const ForeignGcRoots* roots, ForeignTestTally* tally)
{
    const Link before = test->link;
    const uint64_t saved_length = test->results.length;
    ForeignGc exact = { 0 };
    ForeignGc vector = { 0 };
    const ForeignGcError exact_error = foreign_gc_mark(&test->link, roots, FOREIGN_PATH_EXACT, &test->results, &exact);
    const ForeignGcError vector_error = foreign_gc_mark(&test->link, roots, FOREIGN_PATH_VECTOR, &test->results, &vector);
    foreign_test_expect(tally, exact_error == FOREIGN_GC_ERROR_NONE && vector_error == FOREIGN_GC_ERROR_NONE, "both marking paths succeed", 0);
    foreign_test_expect(tally, memcmp(&before, &test->link, sizeof(before)) == 0, "marking preserves Link", 0);
    if (exact_error == FOREIGN_GC_ERROR_NONE && vector_error == FOREIGN_GC_ERROR_NONE)
    {
        foreign_test_expect(tally, memcmp(exact.atoms, vector.atoms, (uint64_t)test->link.atoms.count * sizeof(AtomId)) == 0, "exact/vector atom maps", 0);
        foreign_test_expect(tally, memcmp(exact.symbols, vector.symbols, (uint64_t)test->link.symbols.count * sizeof(SymbolId)) == 0, "exact/vector symbol maps", 0);
        foreign_test_expect(tally, memcmp(exact.fixups, vector.fixups, (uint64_t)test->link.fixups.count * sizeof(FixupId)) == 0, "exact/vector fixup maps", 0);
        foreign_test_expect(tally, memcmp(&exact.kept, &vector.kept, sizeof(exact.kept)) == 0 && exact.rounds == vector.rounds, "exact/vector counts and rounds", 0);
        foreign_gc_test_oracle(&test->link, roots, &vector, &test->oracle, tally);
    }
    arena_pop_to(&test->results, saved_length);
}

static void foreign_gc_test_roots(ForeignTestTally* tally)
{
    ForeignGcTest test;
    const bool allocated = foreign_gc_test_create(&test, 15, 8, 32);
    foreign_test_expect(tally, allocated, "allocate roots fixture", 0);
    if (allocated)
    {
        static const uint8_t classes[] = {
            BUSTER_LINK_CLASS_TEXT,
            BUSTER_LINK_CLASS_TEXT,
            BUSTER_LINK_CLASS_TEXT,
            BUSTER_LINK_CLASS_TEXT,
            BUSTER_LINK_CLASS_TEXT,
            BUSTER_LINK_CLASS_TEXT,
            BUSTER_LINK_CLASS_TEXT,
            BUSTER_LINK_CLASS_BSS,
            BUSTER_LINK_CLASS_INIT,
            BUSTER_LINK_CLASS_INIT_ARRAY,
            BUSTER_LINK_CLASS_FINI,
            BUSTER_LINK_CLASS_FINI_ARRAY,
            BUSTER_LINK_CLASS_RODATA,
            BUSTER_LINK_CLASS_DATA,
            BUSTER_LINK_CLASS_TEXT,
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(classes); i += 1)
        {
            const AtomId atom = foreign_gc_test_atom(&test, classes[i], 64);
            foreign_gc_test_symbol(&test, atom, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_FOREIGN_OBJECT);
        }
        const SymbolId local = foreign_gc_test_symbol(&test, 2, BUSTER_LINK_SYMBOL_LOCAL, BUSTER_LINK_SOURCE_FOREIGN_OBJECT);
        const SymbolId absolute = foreign_gc_test_symbol(&test, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_ABSOLUTE);
        const SymbolId unused = foreign_gc_test_symbol(&test, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE);
        const SymbolId undefined = foreign_gc_test_symbol(&test, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE);
        const SymbolId dead_undefined = foreign_gc_test_symbol(&test, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_NONE);
        const SymbolId linker = foreign_gc_test_symbol(&test, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_LINKER);
        const SymbolId weak = foreign_gc_test_symbol(&test, 0, BUSTER_LINK_SYMBOL_WEAK, BUSTER_LINK_SOURCE_NONE);
        const SymbolId shared = foreign_gc_test_symbol(&test, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_FOREIGN_SHARED);
        const SymbolId entry_targets[] = { 2, 8, undefined, linker, weak, shared };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(entry_targets); i += 1)
        {
            link_add_fixup(&test.link, 1, i * 8, BUSTER_LINK_FIXUP_ABS64, entry_targets[i], i == 1 ? INT64_MAX : 0);
        }
        link_add_fixup(&test.link, 2, 0, BUSTER_LINK_FIXUP_ABS64, 4, 0);
        link_add_fixup(&test.link, 3, 0, BUSTER_LINK_FIXUP_ABS64, dead_undefined, INT64_MIN);
        link_add_fixup(&test.link, 4, 0, BUSTER_LINK_FIXUP_ABS64, 5, 0);
        link_add_fixup(&test.link, 5, 0, BUSTER_LINK_FIXUP_ABS64, 4, 0);
        link_add_fixup(&test.link, 6, 0, BUSTER_LINK_FIXUP_ABS64, 7, 0);
        link_add_fixup(&test.link, 7, 0, BUSTER_LINK_FIXUP_ABS64, 6, 0);
        link_add_fixup(&test.link, 9, 0, BUSTER_LINK_FIXUP_ABS64, 2, 0);
        link_add_fixup(&test.link, 10, 0, BUSTER_LINK_FIXUP_ABS64, 14, 0);
        link_add_priority(&test.link, 10, 101);
        link_add_priority(&test.link, 12, 65535);
        const SymbolId exports[] = { 14, absolute, 14 };
        const AtomId retained[] = { 13, 13 };
        const ForeignGcRoots roots = {
            .entry = 1,
            .exports = exports,
            .export_count = BUSTER_ARRAY_LENGTH(exports),
            .retained = retained,
            .retained_count = BUSTER_ARRAY_LENGTH(retained),
        };
        foreign_test_expect(tally, test.link.error == LINK_ERROR_NONE, "build valid roots fixture", 0);
        foreign_gc_test_compare(&test, &roots, tally);
        ForeignGc gc = { 0 };
        if (foreign_gc_mark(&test.link, &roots, FOREIGN_PATH_VECTOR, &test.results, &gc) == FOREIGN_GC_ERROR_NONE)
        {
            foreign_test_expect(tally, gc.kept.atoms == 11 && gc.kept.symbols == 17 && gc.kept.wide_addends == 1 && gc.kept.priorities == 2,
                                "explicit roots and kept counts", 0);
            foreign_test_expect(tally, gc.atoms[3] == 0 && gc.atoms[6] == 0 && gc.atoms[7] == 0 && gc.atoms[15] == 0, "dead cycles and unused globals drop", 0);
            foreign_test_expect(tally, gc.symbols[local] != 0 && gc.symbols[absolute] != 0 && gc.symbols[undefined] != 0 && gc.symbols[linker] != 0 &&
                                      gc.symbols[weak] != 0 && gc.symbols[shared] != 0 && gc.symbols[unused] == 0 && gc.symbols[dead_undefined] == 0,
                                "symbol sources follow live references", 0);
        }
        const ForeignGcRoots empty_roots = { 0 };
        foreign_gc_test_compare(&test, &empty_roots, tally);
    }
    foreign_gc_test_release(&test);
}

static void foreign_gc_test_graphs(ForeignTestTally* tally)
{
    static const uint32_t counts[] = { 0, 1, 14, 15, 16, 17, 62, 63, 64, 65, 127, 511, 512, 513, 1025, 8193 };
    uint64_t random = 0x429285163fee2d5bull;
    for (uint32_t case_id = 0; case_id < BUSTER_ARRAY_LENGTH(counts); case_id += 1)
    {
        const uint32_t count = counts[case_id];
        ForeignGcTest test;
        const bool allocated = foreign_gc_test_create(&test, count, 0, count * 3);
        foreign_test_expect(tally, allocated, "allocate graph fixture", 0);
        if (allocated)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                const AtomId atom = foreign_gc_test_atom(&test, BUSTER_LINK_CLASS_TEXT, 24);
                foreign_gc_test_symbol(&test, atom, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_FOREIGN_OBJECT);
            }
            const bool chain = count == 8193;
            for (AtomId atom = 1; atom <= count; atom += 1)
            {
                if (chain)
                {
                    if (atom < count)
                    {
                        link_add_fixup(&test.link, atom, 0, BUSTER_LINK_FIXUP_ABS64, atom + 1, 0);
                    }
                }
                else
                {
                    const uint32_t half = count / 2;
                    const uint32_t begin = atom <= half ? 1 : half + 1;
                    const uint32_t width = atom <= half ? half : count - half;
                    for (uint32_t edge = 0; edge < atom % 4; edge += 1)
                    {
                        random ^= random << 13;
                        random ^= random >> 7;
                        random ^= random << 17;
                        link_add_fixup(&test.link, atom, edge * 8, BUSTER_LINK_FIXUP_ABS64, begin + (uint32_t)(random % width), 0);
                    }
                }
            }
            const ForeignGcRoots roots = {
                .entry = count == 0 ? 0 : 1,
            };
            foreign_test_expect(tally, test.link.error == LINK_ERROR_NONE, "build valid graph fixture", 0);
            foreign_gc_test_compare(&test, &roots, tally);
            if (chain)
            {
                ForeignGc gc = { 0 };
                const ForeignGcError error = foreign_gc_mark(&test.link, &roots, FOREIGN_PATH_VECTOR, &test.results, &gc);
                foreign_test_expect(tally, error == FOREIGN_GC_ERROR_NONE && gc.rounds == count && gc.kept.atoms == count && gc.kept.fixups == count - 1,
                                    "deep graph has bounded iterative traversal", 0);
            }
        }
        foreign_gc_test_release(&test);
    }
}

static void foreign_gc_test_failure(ForeignGcTest* test, const ForeignGcRoots* roots, ForeignGcError expected, ForeignPath path, ForeignTestTally* tally)
{
    ForeignGc result;
    memset(&result, 0xa5, sizeof(result));
    const ForeignGc before = result;
    const uint64_t length = test->results.length;
    const ForeignGcError error = foreign_gc_mark(&test->link, roots, path, &test->results, &result);
    foreign_test_expect(tally, error == expected, "failure returns expected error", 0);
    foreign_test_expect(tally, memcmp(&before, &result, sizeof(result)) == 0 && test->results.length == length, "failure preserves result and arena", 0);
}

static void foreign_gc_test_errors(ForeignTestTally* tally)
{
    ForeignGcTest test;
    const bool allocated = foreign_gc_test_create(&test, 3, 0, 3);
    foreign_test_expect(tally, allocated, "allocate error fixture", 0);
    if (allocated)
    {
        for (uint32_t i = 0; i < 3; i += 1)
        {
            const AtomId atom = foreign_gc_test_atom(&test, BUSTER_LINK_CLASS_TEXT, 16);
            foreign_gc_test_symbol(&test, atom, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_FOREIGN_OBJECT);
        }
        link_add_fixup(&test.link, 1, 0, BUSTER_LINK_FIXUP_ABS64, 2, 0);
        link_add_fixup(&test.link, 2, 0, BUSTER_LINK_FIXUP_ABS64, 1, 0);
        ForeignGcRoots roots = {
            .entry = 1,
        };
        for (uint32_t path = FOREIGN_PATH_VECTOR; path <= FOREIGN_PATH_EXACT; path += 1)
        {
            roots.entry = test.link.symbols.count;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)path, tally);
            roots.entry = 1;
            roots.export_count = 1;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)path, tally);
            const SymbolId invalid = 0;
            roots.exports = &invalid;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)path, tally);
            roots.export_count = 0;
            roots.retained_count = 1;
            roots.retained = &invalid;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)path, tally);
            roots.retained_count = 0;
            test.link.atoms.class[3] = BUSTER_LINK_CLASS_EH_FRAME;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_UNWIND, (ForeignPath)path, tally);
            test.link.atoms.size[3] = 0;
            foreign_gc_test_compare(&test, &roots, tally);
            test.link.atoms.size[3] = 16;
            test.link.atoms.class[3] = BUSTER_LINK_CLASS_TEXT;
            test.link.fixups.target[1] = test.link.symbols.count;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)path, tally);
            test.link.fixups.target[1] = 2;
            test.link.symbols.atom[2] = test.link.atoms.count;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)path, tally);
            test.link.symbols.atom[2] = 2;
            test.link.atoms.fixup_begin[2] = 1;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)path, tally);
            test.link.atoms.fixup_begin[2] = 2;
            test.link.atoms.fixup_count[2] = 0;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)path, tally);
            test.link.atoms.fixup_count[2] = 1;
            test.link.atoms.fixup_count[1] = UINT32_MAX;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)path, tally);
            test.link.atoms.fixup_count[1] = 1;
            test.link.atoms.class[0] = BUSTER_LINK_CLASS_INIT;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)path, tally);
            test.link.atoms.class[0] = BUSTER_LINK_CLASS_NONE;
        }
        foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_INPUT, (ForeignPath)99, tally);
        const uint64_t capacity = test.results.capacity;
        // Three maps end at byte 44; the live row, two atom queues, symbol row and fixup row end at byte 320.
        for (uint64_t bytes = 0; bytes < 320; bytes += 1)
        {
            test.results.capacity = bytes;
            foreign_gc_test_failure(&test, &roots, FOREIGN_GC_ERROR_MEMORY, FOREIGN_PATH_VECTOR, tally);
        }
        test.results.capacity = capacity;
    }
    foreign_gc_test_release(&test);
}

static void foreign_gc_test_guards(ForeignTestTally* tally)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* memory = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0;
    foreign_test_expect(tally, mapped, "allocate guarded tails", 0);
    if (mapped)
    {
        for (uint32_t count = 1; count <= 129; count += 1)
        {
            uint64_t bits[8] __attribute__((aligned(64))) = { 0 };
            uint32_t expected[129] = { 0 };
            uint32_t* guarded = (uint32_t*)(memory + page - count * sizeof(uint32_t));
            for (uint32_t id = 1; id < count; id += 1)
            {
                if (id % 3 != 0)
                {
                    foreign_gc_set(bits, id);
                }
            }
            const uint32_t exact_count = foreign_gc_map(bits, count, FOREIGN_PATH_EXACT, expected);
            const uint32_t vector_count = foreign_gc_map(bits, count, FOREIGN_PATH_VECTOR, guarded);
            foreign_test_expect(tally, exact_count == vector_count && memcmp(expected, guarded, count * sizeof(uint32_t)) == 0, "guarded map stores match", 0);
            const uint32_t exact_roots = foreign_gc_queue(bits, count, FOREIGN_PATH_EXACT, expected);
            uint32_t* root_guarded = (uint32_t*)(memory + page - exact_roots * sizeof(uint32_t));
            const uint32_t vector_roots = foreign_gc_queue(bits, count, FOREIGN_PATH_VECTOR, root_guarded);
            foreign_test_expect(tally, exact_roots == vector_roots && memcmp(expected, root_guarded, exact_roots * sizeof(uint32_t)) == 0, "guarded queue stores match", 0);
            for (uint32_t id = 1; id < count; id += 1)
            {
                foreign_gc_set(bits, id);
            }
            const uint32_t full_roots = foreign_gc_queue(bits, count, FOREIGN_PATH_EXACT, expected);
            root_guarded = (uint32_t*)(memory + page - full_roots * sizeof(uint32_t));
            const uint32_t full_vector = foreign_gc_queue(bits, count, FOREIGN_PATH_VECTOR, root_guarded);
            foreign_test_expect(tally, full_roots == full_vector && memcmp(expected, root_guarded, full_roots * sizeof(uint32_t)) == 0, "guarded dense queue stores match", 0);
            uint8_t* classes = memory + page - count;
            for (uint32_t id = 0; id < count; id += 1)
            {
                classes[id] = id == 0 ? BUSTER_LINK_CLASS_NONE : (uint8_t)(id % BUSTER_LINK_CLASS_COUNT);
            }
            Link link = { 0 };
            link.atoms.class = classes;
            link.atoms.count = count;
            uint64_t exact[8] __attribute__((aligned(64))) = { 0 };
            uint64_t vector[8] __attribute__((aligned(64))) = { 0 };
            foreign_gc_root_classes(&link, FOREIGN_PATH_EXACT, exact);
            foreign_gc_root_classes(&link, FOREIGN_PATH_VECTOR, vector);
            foreign_test_expect(tally, memcmp(exact, vector, sizeof(exact)) == 0, "guarded class loads match", 0);
        }
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, page * 2);
    }
}

static void foreign_gc_test_edges(ForeignTestTally* tally)
{
    ForeignGcTest test;
    const bool allocated = foreign_gc_test_create(&test, 3, 0, 0);
    foreign_test_expect(tally, allocated, "allocate non-relocation dependency fixture", 0);
    if (allocated)
    {
        for (uint32_t i = 0; i < 3; i += 1)
        {
            const AtomId atom = foreign_gc_test_atom(&test, BUSTER_LINK_CLASS_RODATA, 4);
            foreign_gc_test_symbol(&test, atom, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_FOREIGN_OBJECT);
        }
        uint32_t offsets[] = { 0, 0, 1, 2, 3 };
        AtomId targets[] = { 3, 1, 2 };
        ForeignGcEdges edges = {
            .offsets = offsets,
            .targets = targets,
            .count = BUSTER_ARRAY_LENGTH(targets),
        };
        const ForeignGcRoots roots = {
            .entry = 1,
        };
        for (uint32_t path = FOREIGN_PATH_VECTOR; path <= FOREIGN_PATH_EXACT; path += 1)
        {
            ForeignGc gc = { 0 };
            const ForeignGcError error = foreign_gc_mark_edges(&test.link, &roots, &edges, (ForeignPath)path, &test.results, &gc);
            foreign_test_expect(tally, error == FOREIGN_GC_ERROR_NONE && gc.kept.atoms == 3 && gc.kept.symbols == 3 && gc.kept.fixups == 0 && gc.rounds == 3,
                                "non-relocation cycle closes without manufactured fixups", 0);
            for (uint32_t bad = 0; bad < 5; bad += 1)
            {
                if (bad == 0)
                {
                    offsets[2] = 3;
                }
                else if (bad == 1)
                {
                    offsets[4] = 2;
                }
                else if (bad == 2 || bad == 3)
                {
                    targets[0] = bad == 2 ? 0 : test.link.atoms.count;
                }
                else
                {
                    edges.targets = 0;
                }
                const ForeignGc before = gc;
                const uint64_t length = test.results.length;
                const ForeignGcError refused = foreign_gc_mark_edges(&test.link, &roots, &edges, (ForeignPath)path, &test.results, &gc);
                foreign_test_expect(tally, refused == FOREIGN_GC_ERROR_INPUT && memcmp(&before, &gc, sizeof(gc)) == 0 && test.results.length == length,
                                    "malformed non-relocation edges fail atomically", 0);
                offsets[2] = 1;
                offsets[4] = 3;
                targets[0] = 3;
                edges.targets = targets;
            }
        }
    }
    foreign_gc_test_release(&test);
}

static bool foreign_gc_test(void)
{
    ForeignTestTally tally = {
        .check = "foreign gc marking",
    };
    foreign_gc_test_roots(&tally);
    foreign_gc_test_graphs(&tally);
    foreign_gc_test_errors(&tally);
    foreign_gc_test_guards(&tally);
    foreign_gc_test_edges(&tally);
    return foreign_test_report(&tally);
}
