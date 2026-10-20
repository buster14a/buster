// The link representation's tests, for the test-link command:
//
//   build/compiler test-link [--seed N] [--functions N]
//   build/compiler test-link --print-assembly DIRECTORY [--seed N] [--functions N]
//
// main.c includes this file after link.c and link_program.c and calls run_link_tests for the command. The checks, in
// this order:
//   - Round trips: random records of every table (names of random bytes, atoms of every class, symbols, and fixups of
//     every kind, a quarter of them with wide addends) read back from the columns, ids from 1 in order, and record 0
//     zero in every column.
//   - Capacity: every table at its capacity, the side table full while the fixups aren't, a name arena too small for a
//     name, and arenas too small for the tables: each add fails with LINK_ERROR_FULL and changes nothing, and
//     link_capacity_bytes is room enough.
//   - Rules: records each rule refuses (link.c's orientation comment), with the error each gets, and what link_check
//     finds.
//   - Kinds: link_test_known_fixups, instructions of every kind on the fields it can patch, with the bytes, field
//     offsets and addends GNU as gives (binutils 2.42; its relocation's addend is the fixup's): each encoded alone, and
//     in streams of BUSTER_LINK_TEST_STREAMS lengths with unmarked instructions between them and fields of both kinds in
//     one instruction, so that the fixups come from x86_encode's fixup ends across its groups. Then each kind on fields
//     it can't patch, and references that don't match the marked instructions.
//   - Wide addends: addends around int32's limits, directly and through an instruction's distance to its end, to the
//     side table exactly when they don't fit.
//   - Programs: link_program_generate with the sets of options in link_test_program_options (the defaults, the
//     smallest, many units, no absolute addresses, --functions workers without sizes, and two that call libc), which
//     link_check accepts. Each is generated twice from its seed, byte for byte the same, and differently from another
//     seed; and run: laid out in memory as a static executable would be (link_test_load: atoms in class order, a GOT,
//     the TLS segment, every fixup patched) and started in a child process, it must exit with status 0 having written
//     LinkProgram.output. Each program runs again with its TLS segment starting half its alignment past a multiple of
//     it, as a linker may start .tdata when .tbss is more aligned. A program without libc starts at _start, with an
//     initial stack and auxiliary vector as the kernel's. One that calls libc gets stand-ins for its imports
//     (link_test_import), and the child does what the C runtime would around main (link_test_start_libc). Each set
//     with two thread-locals, an init function and a fini function has every class and every kind its options allow,
//     and a copy of the defaults' program with the hexadecimal digits' fixup one byte off must write something else.
// --print-assembly writes those programs (from the same --seed and --functions) as GNU as source instead: a directory
// for each set of options (link_test_program_directories) with a file per unit and the expected output, for the
// cross-check in AGENTS.md, where GNU as assembles them and GNU ld, lld, mold and wild link them (through cc, with
// libc). The exit status is 1 if the arguments are invalid, a check fails or a file can't be written.
//
// Layout, in this order: the tally, the round trips, the capacity and rules checks, the known fixups and the kinds
// checks, the wide addends, the loader and the program checks, the assembly, and run_link_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define BUSTER_LINK_TEST_REPORTED 3
// splitmix64's increment, and what each check's sequence starts from, scrambled with --seed N (link_program_scramble,
// so that nearby seeds don't share a sequence).
#define BUSTER_LINK_TEST_GOLDEN_RATIO 0x9E3779B97F4A7C15ull
#define BUSTER_LINK_TEST_ROUND_TRIPS_STREAM 0x5851F42D4C957F2Dull
#define BUSTER_LINK_TEST_KINDS_STREAM 0x2545F4914F6CDD1Dull
#define BUSTER_LINK_TEST_ARENA_BYTES (1ull << 30)
// The streams of known fixups: each length of marked instructions, around x86_encode's groups of 16 and its iterations
// of 64.
static const uint32_t link_test_stream_lengths[] = { 0, 1, 2, 15, 16, 17, 63, 64, 65, 200, 1000 };
#define BUSTER_LINK_TEST_STREAMS BUSTER_ARRAY_LENGTH(link_test_stream_lengths)
// A program's run: how long it may take, and how much output is read.
#define BUSTER_LINK_TEST_RUN_SECONDS 20
#define BUSTER_LINK_TEST_OUTPUT_BYTES 256
#define BUSTER_LINK_TEST_STACK_BYTES (1ull << 20)

// The failures of a check, and the first few, printed.
typedef struct LinkTestTally LinkTestTally;
struct LinkTestTally
{
    const char* check;
    uint64_t checked;
    uint64_t failures;
};

static void link_test_expect(LinkTestTally* tally, bool condition, const char* what, uint64_t index)
{
    tally->checked += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_LINK_TEST_REPORTED)
        {
            printf("  %s: %s (%llu)\n", tally->check, what, (unsigned long long)index);
        }
        tally->failures += 1;
    }
}

static bool link_test_report(const LinkTestTally* tally)
{
    printf("%s: %llu checks, %llu failed\n", tally->check, (unsigned long long)tally->checked, (unsigned long long)tally->failures);
    fflush(stdout);
    return tally->failures == 0;
}

// splitmix64.
static uint64_t link_test_random(uint64_t* state)
{
    *state += BUSTER_LINK_TEST_GOLDEN_RATIO;
    uint64_t x = *state;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static uint32_t link_test_below(uint64_t* state, uint32_t bound)
{
    return (uint32_t)(((link_test_random(state) >> 32) * bound) >> 32);
}

// The arenas a check's link uses, released together.
typedef struct LinkTestArenas LinkTestArenas;
struct LinkTestArenas
{
    Arena tables;
    Arena names;
    Arena bytes;
};

static LinkTestArenas link_test_reserve(void)
{
    LinkTestArenas result = {
        .tables = arena_reserve(BUSTER_LINK_TEST_ARENA_BYTES),
        .names = arena_reserve(BUSTER_LINK_TEST_ARENA_BYTES),
        .bytes = arena_reserve(BUSTER_LINK_TEST_ARENA_BYTES),
    };
    return result;
}

static void link_test_release(LinkTestArenas* arenas)
{
    arena_release(&arenas->tables);
    arena_release(&arenas->names);
    arena_release(&arenas->bytes);
}

// ---------------------------------------------------------------------------------------------------------------------
// Round trips

#define BUSTER_LINK_TEST_NAMES 300
#define BUSTER_LINK_TEST_ATOMS 200
#define BUSTER_LINK_TEST_SYMBOLS 400
#define BUSTER_LINK_TEST_FIXUPS 4000
#define BUSTER_LINK_TEST_WIDE_ADDENDS BUSTER_LINK_TEST_FIXUPS

// What the round trips added, to compare with the columns.
typedef struct LinkTestAtom LinkTestAtom;
struct LinkTestAtom
{
    uint64_t size;
    uint8_t* bytes;
    uint32_t unit;
    uint8_t class;
    uint8_t alignment_log2;
};

typedef struct LinkTestSymbol LinkTestSymbol;
struct LinkTestSymbol
{
    uint64_t value;
    uint64_t size;
    NameId name;
    AtomId atom;
    uint8_t flags;
    uint8_t source;
};

typedef struct LinkTestFixup LinkTestFixup;
struct LinkTestFixup
{
    int64_t addend;
    uint32_t offset;
    SymbolId target;
    AtomId atom;
    uint8_t kind;
};

static bool link_test_round_trips(uint64_t seed)
{
    LinkTestTally tally = {
        .check = "round trips",
    };
    uint64_t state = link_program_scramble(seed ^ BUSTER_LINK_TEST_ROUND_TRIPS_STREAM);
    LinkTestArenas arenas = link_test_reserve();
    Scratch scratch = scratch_begin(0);
    const LinkCapacity capacity = {
        .names = BUSTER_LINK_TEST_NAMES,
        .atoms = BUSTER_LINK_TEST_ATOMS,
        .symbols = BUSTER_LINK_TEST_SYMBOLS,
        .fixups = BUSTER_LINK_TEST_FIXUPS,
        .wide_addends = BUSTER_LINK_TEST_WIDE_ADDENDS,
    };
    Link link;
    const bool initialized = link_initialize(&link, &arenas.tables, &arenas.names, capacity);
    link_test_expect(&tally, initialized, "the tables fit", 0);
    link_test_expect(&tally, arenas.tables.length <= link_capacity_bytes(capacity), "link_capacity_bytes is room enough", arenas.tables.length);
    uint8_t* name_bytes = arena_push(scratch.arena, BUSTER_LINK_TEST_NAMES * 64, 1);
    uint32_t* name_lengths = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, BUSTER_LINK_TEST_NAMES + 1);
    LinkTestAtom* atoms = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, LinkTestAtom, BUSTER_LINK_TEST_ATOMS + 1);
    LinkTestSymbol* symbols = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, LinkTestSymbol, BUSTER_LINK_TEST_SYMBOLS + 1);
    LinkTestFixup* fixups = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, LinkTestFixup, BUSTER_LINK_TEST_FIXUPS + 1);
    const bool ready = initialized && name_bytes != 0 && name_lengths != 0 && atoms != 0 && symbols != 0 && fixups != 0;
    link_test_expect(&tally, ready, "memory for the expected records", 0);

    // Names: random bytes, NULs among them, and the last one the same bytes as the one before.
    for (uint32_t i = 1; ready && i <= BUSTER_LINK_TEST_NAMES; i += 1)
    {
        uint8_t* source = name_bytes + 64 * (i - 1);
        const uint8_t* previous = source - 64;
        const uint32_t length = i == BUSTER_LINK_TEST_NAMES ? name_lengths[i - 1] : 1 + link_test_below(&state, 64);
        for (uint32_t j = 0; j < length; j += 1)
        {
            source[j] = i == BUSTER_LINK_TEST_NAMES ? previous[j] : (uint8_t)link_test_random(&state);
        }
        name_lengths[i] = length;
        link_test_expect(&tally, link_add_name(&link, source, length) == i, "a name's id", i);
    }
    for (uint32_t i = 1; ready && i <= BUSTER_LINK_TEST_NAMES; i += 1)
    {
        const uint8_t* source = name_bytes + 64 * (i - 1);
        const uint32_t length = name_lengths[i];
        link_test_expect(&tally, link.names.length[i] == length, "a name's length", i);
        link_test_expect(&tally, memcmp(link.names.bytes[i], source, length) == 0 && link.names.bytes[i][length] == 0, "a name's bytes and NUL", i);
        link_test_expect(&tally, link.names.hash[i] == link_hash(source, length), "a name's hash", i);
    }
    if (ready)
    {
        link_test_expect(&tally, link.names.hash[BUSTER_LINK_TEST_NAMES] == link.names.hash[BUSTER_LINK_TEST_NAMES - 1], "equal names' hashes", 0);
    }

    // Atoms of every class, with random alignments, sizes and units.
    for (uint32_t i = 1; ready && i <= BUSTER_LINK_TEST_ATOMS; i += 1)
    {
        const uint8_t class = (uint8_t)(1 + (i - 1) % (BUSTER_LINK_CLASS_COUNT - 1));
        const uint64_t size = link_test_below(&state, 4096);
        uint8_t* bytes = 0;
        if (!link_class_is_zero_fill(class))
        {
            bytes = arena_push(&arenas.bytes, size + 1, 1);
            for (uint64_t j = 0; bytes != 0 && j < size; j += 1)
            {
                bytes[j] = (uint8_t)link_test_random(&state);
            }
        }
        atoms[i] = (LinkTestAtom){
            .size = size,
            .bytes = bytes,
            .unit = (uint32_t)link_test_random(&state),
            .class = class,
            .alignment_log2 = (uint8_t)link_test_below(&state, BUSTER_LINK_MAX_ALIGNMENT_LOG2 + 1),
        };
        const AtomId atom = link_add_atom(&link, class, atoms[i].alignment_log2, size, bytes, atoms[i].unit);
        link_test_expect(&tally, atom == i, "an atom's id", i);
    }
    for (uint32_t i = 1; ready && i <= BUSTER_LINK_TEST_ATOMS; i += 1)
    {
        const bool columns = link.atoms.class[i] == atoms[i].class && link.atoms.alignment_log2[i] == atoms[i].alignment_log2 && link.atoms.unit[i] == atoms[i].unit &&
                             link.atoms.size[i] == atoms[i].size && link.atoms.bytes[i] == atoms[i].bytes && link.atoms.address[i] == 0;
        link_test_expect(&tally, columns, "an atom's columns", i);
    }

    // Symbols: names and atoms or none, within their atoms, TLS exactly in TLS atoms, every binding, visibility and
    // source.
    for (uint32_t i = 1; ready && i <= BUSTER_LINK_TEST_SYMBOLS; i += 1)
    {
        static const uint8_t types[] = { BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SYMBOL_TLS };
        static const uint8_t bindings[] = { BUSTER_LINK_SYMBOL_LOCAL, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SYMBOL_WEAK };
        const AtomId atom = link_test_below(&state, BUSTER_LINK_TEST_ATOMS + 1);
        const uint64_t atom_size = atom != 0 ? atoms[atom].size : UINT64_MAX;
        const uint64_t value = atom != 0 ? link_test_random(&state) % (atom_size + 1) : link_test_random(&state);
        const uint64_t size = atom != 0 ? link_test_random(&state) % (atom_size - value + 1) : link_test_random(&state);
        const bool tls = atom != 0 ? link_class_is_tls(atoms[atom].class) : link_test_below(&state, 4) == 0;
        const uint8_t type = tls ? BUSTER_LINK_SYMBOL_TLS : types[link_test_below(&state, 3)];
        const uint8_t visibility = (uint8_t)(link_test_below(&state, 4) << BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT);
        symbols[i] = (LinkTestSymbol){
            .value = value,
            .size = size,
            .name = link_test_below(&state, BUSTER_LINK_TEST_NAMES + 1),
            .atom = atom,
            .flags = (uint8_t)(type | bindings[link_test_below(&state, 3)] | visibility),
            .source = (uint8_t)link_test_below(&state, BUSTER_LINK_SOURCE_COUNT),
        };
        const SymbolId symbol = link_add_symbol(&link, symbols[i].name, atom, value, size, symbols[i].flags, symbols[i].source);
        link_test_expect(&tally, symbol == i, "a symbol's id", i);
    }
    for (uint32_t i = 1; ready && i <= BUSTER_LINK_TEST_SYMBOLS; i += 1)
    {
        const LinkSymbols* columns = &link.symbols;
        const bool equal = columns->name[i] == symbols[i].name && columns->atom[i] == symbols[i].atom && columns->value[i] == symbols[i].value &&
                           columns->size[i] == symbols[i].size && columns->flags[i] == symbols[i].flags && columns->source[i] == symbols[i].source;
        const bool linker = columns->got_slot[i] == 0 && columns->dynamic_index[i] == 0 && columns->version[i] == 0 && columns->reference[i] == 0;
        link_test_expect(&tally, equal && linker, "a symbol's columns", i);
    }

    // Fixups of every kind, in order within each atom with bytes, a quarter with addends that don't fit in int32.
    uint32_t fixup_count = 0;
    for (AtomId atom = 1; ready && atom <= BUSTER_LINK_TEST_ATOMS; atom += 1)
    {
        uint64_t offset = link_test_below(&state, 16);
        bool more = atoms[atom].bytes != 0;
        while (more && fixup_count < BUSTER_LINK_TEST_FIXUPS)
        {
            const uint8_t kind = (uint8_t)(1 + link_test_below(&state, BUSTER_LINK_FIXUP_KINDS - 1));
            const uint32_t width = link_fixup_kinds[kind].width;
            more = offset + width <= atoms[atom].size;
            if (more)
            {
                const bool wide = link_test_below(&state, 4) == 0;
                const int64_t addend = wide ? (int64_t)(link_test_random(&state) | (1ull << 40)) : (int32_t)link_test_random(&state);
                fixup_count += 1;
                fixups[fixup_count] = (LinkTestFixup){
                    .addend = addend,
                    .offset = (uint32_t)offset,
                    .target = 1 + link_test_below(&state, BUSTER_LINK_TEST_SYMBOLS),
                    .atom = atom,
                    .kind = kind,
                };
                const FixupId fixup = link_add_fixup(&link, atom, (uint32_t)offset, kind, fixups[fixup_count].target, addend);
                link_test_expect(&tally, fixup == fixup_count, "a fixup's id", fixup_count);
                offset += width + link_test_below(&state, 16);
            }
        }
    }
    for (uint32_t i = 1; ready && i <= fixup_count; i += 1)
    {
        const LinkTestFixup* expected = &fixups[i];
        const bool wide = expected->addend < INT32_MIN || expected->addend > INT32_MAX;
        const uint8_t kind = (uint8_t)(expected->kind | (wide ? BUSTER_LINK_FIXUP_WIDE : 0));
        const AtomId atom = expected->atom;
        const bool in_range = link.atoms.fixup_begin[atom] <= i && i < link.atoms.fixup_begin[atom] + link.atoms.fixup_count[atom];
        const bool columns = link.fixups.offset[i] == expected->offset && link.fixups.target[i] == expected->target && link.fixups.kind[i] == kind &&
                             (wide || link.fixups.addend[i] == expected->addend);
        link_test_expect(&tally, columns && in_range, "a fixup's columns and its atom's range", i);
        link_test_expect(&tally, link_fixup_addend(&link, i) == expected->addend, "a fixup's addend", i);
    }
    if (ready)
    {
        link_test_expect(&tally, link.fixups.count == fixup_count + 1 && fixup_count > BUSTER_LINK_TEST_FIXUPS / 2, "the fixups added", fixup_count);
        link_test_expect(&tally, link.error == LINK_ERROR_NONE, "no error", link.error);
        const bool zero = link.names.bytes[0] == 0 && link.names.length[0] == 0 && link.names.hash[0] == 0 && link.atoms.class[0] == 0 && link.atoms.size[0] == 0 &&
                          link.atoms.bytes[0] == 0 && link.atoms.fixup_count[0] == 0 && link.symbols.name[0] == 0 && link.symbols.atom[0] == 0 &&
                          link.symbols.flags[0] == 0 && link.fixups.offset[0] == 0 && link.fixups.target[0] == 0 && link.fixups.kind[0] == 0 &&
                          link.fixups.wide_addends[0] == 0;
        link_test_expect(&tally, zero, "record 0 is zero", 0);
    }
    scratch_end(scratch);
    link_test_release(&arenas);
    return link_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Capacity and rules

// Whether the last add failed with `error` (and clears it for the next).
static bool link_test_failed(Link* link, LinkError error)
{
    const bool result = link->error == error;
    link->error = LINK_ERROR_NONE;
    return result;
}

static bool link_test_capacity(void)
{
    LinkTestTally tally = {
        .check = "capacity",
    };
    LinkTestArenas arenas = link_test_reserve();
    static uint8_t bytes[64];
    const LinkCapacity capacity = {
        .names = 2,
        .atoms = 2,
        .symbols = 2,
        .fixups = 2,
        .wide_addends = 1,
    };
    Link link;
    link_test_expect(&tally, link_initialize(&link, &arenas.tables, &arenas.names, capacity), "small tables fit", 0);
    link_test_expect(&tally, link_add_name(&link, (const uint8_t*)"a", 1) == 1 && link_add_name(&link, (const uint8_t*)"b", 1) == 2, "names up to the capacity", 0);
    link_test_expect(&tally, link_add_name(&link, (const uint8_t*)"c", 1) == 0 && link_test_failed(&link, LINK_ERROR_FULL) && link.names.count == 3, "a name past it", 0);
    link_test_expect(&tally, link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, 64, bytes, 0) == 1 && link_add_atom(&link, BUSTER_LINK_CLASS_BSS, 3, 8, 0, 0) == 2,
                     "atoms up to the capacity", 0);
    link_test_expect(&tally, link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, 8, bytes, 0) == 0 && link_test_failed(&link, LINK_ERROR_FULL) && link.atoms.count == 3,
                     "an atom past it", 0);
    const uint8_t object = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT;
    link_test_expect(&tally, link_add_symbol(&link, 1, 1, 0, 8, object, BUSTER_LINK_SOURCE_PROGRAM) == 1 && link_add_symbol(&link, 2, 0, 0, 0, object, 0) == 2,
                     "symbols up to the capacity", 0);
    link_test_expect(&tally, link_add_symbol(&link, 0, 0, 0, 0, object, 0) == 0 && link_test_failed(&link, LINK_ERROR_FULL) && link.symbols.count == 3,
                     "a symbol past it", 0);
    // The side table holds one wide addend: a second fails while the fixups have room, and a narrow one then fits.
    link_test_expect(&tally, link_add_fixup(&link, 1, 0, BUSTER_LINK_FIXUP_ABS64, 1, 1ll << 40) == 1, "a wide addend", 0);
    link_test_expect(&tally, link_add_fixup(&link, 1, 8, BUSTER_LINK_FIXUP_ABS64, 1, -(1ll << 40)) == 0 && link_test_failed(&link, LINK_ERROR_FULL) &&
                         link.fixups.count == 2 && link.fixups.wide_count == 2 && link.atoms.fixup_count[1] == 1,
                     "a wide addend past the side table", 0);
    link_test_expect(&tally, link_add_fixup(&link, 1, 8, BUSTER_LINK_FIXUP_ABS64, 1, 8) == 2, "a narrow addend then", 0);
    link_test_expect(&tally, link_add_fixup(&link, 1, 16, BUSTER_LINK_FIXUP_ABS64, 1, 8) == 0 && link_test_failed(&link, LINK_ERROR_FULL) && link.fixups.count == 3 &&
                         link.atoms.fixup_count[1] == 2,
                     "a fixup past the capacity", 0);

    // A name arena with room for one name of 7 bytes and its NUL.
    Arena small_names = arena_reserve(8);
    Link named;
    link_test_expect(&tally, link_initialize(&named, &arenas.tables, &small_names, capacity), "tables for the small name arena", 0);
    link_test_expect(&tally, link_add_name(&named, (const uint8_t*)"1234567", 7) == 1, "a name that fills the name arena", 0);
    link_test_expect(&tally, link_add_name(&named, (const uint8_t*)"x", 1) == 0 && link_test_failed(&named, LINK_ERROR_FULL) && named.names.count == 2,
                     "a name past it", 0);
    arena_release(&small_names);

    // Tables in an arena of exactly link_capacity_bytes, of too little, and with a capacity ids can't count.
    const LinkCapacity large = {
        .names = 1000,
        .atoms = 2000,
        .symbols = 3000,
        .fixups = 40000,
        .wide_addends = 100,
    };
    Arena exact = arena_reserve(link_capacity_bytes(large));
    Arena short_arena = arena_reserve(link_capacity_bytes(large) / 2);
    Link fitted;
    link_test_expect(&tally, link_initialize(&fitted, &exact, &arenas.names, large), "link_capacity_bytes is room enough", exact.length);
    link_test_expect(&tally, !link_initialize(&fitted, &short_arena, &arenas.names, large) && fitted.error == LINK_ERROR_FULL && short_arena.length == 0 &&
                         fitted.names.capacity == 0 && fitted.fixups.capacity == 0,
                     "tables in too little room", short_arena.length);
    link_test_expect(&tally, link_add_name(&fitted, (const uint8_t*)"a", 1) == 0 && link_add_atom(&fitted, BUSTER_LINK_CLASS_BSS, 0, 1, 0, 0) == 0, "adds without tables",
                     0);
    LinkCapacity too_many = large;
    too_many.symbols = UINT32_MAX;
    link_test_expect(&tally, !link_initialize(&fitted, &exact, &arenas.names, too_many) && fitted.error == LINK_ERROR_FULL, "a capacity of UINT32_MAX", 0);
    arena_release(&exact);
    arena_release(&short_arena);
    link_test_release(&arenas);
    return link_test_report(&tally);
}

static bool link_test_rules(void)
{
    LinkTestTally tally = {
        .check = "rules",
    };
    LinkTestArenas arenas = link_test_reserve();
    static uint8_t bytes[64];
    const LinkCapacity capacity = {
        .names = 16,
        .atoms = 16,
        .symbols = 16,
        .fixups = 16,
        .wide_addends = 4,
    };
    Link link;
    link_test_expect(&tally, link_initialize(&link, &arenas.tables, &arenas.names, capacity), "the tables fit", 0);
    const NameId name = link_add_name(&link, (const uint8_t*)"x", 1);
    link_test_expect(&tally, link_add_name(&link, (const uint8_t*)"", 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "an empty name", 0);

    // Atoms.
    link_test_expect(&tally, link_add_atom(&link, BUSTER_LINK_CLASS_NONE, 0, 8, bytes, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "class none", 0);
    link_test_expect(&tally, link_add_atom(&link, BUSTER_LINK_CLASS_COUNT, 0, 8, bytes, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "a class past the last", 0);
    link_test_expect(&tally, link_add_atom(&link, BUSTER_LINK_CLASS_DATA, BUSTER_LINK_MAX_ALIGNMENT_LOG2 + 1, 8, bytes, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD),
                     "an alignment past the largest", 0);
    link_test_expect(&tally, link_add_atom(&link, BUSTER_LINK_CLASS_BSS, 0, 8, bytes, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "zero-fill with bytes", 0);
    link_test_expect(&tally, link_add_atom(&link, BUSTER_LINK_CLASS_TDATA, 0, 8, 0, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "bytes missing", 0);
    link_test_expect(&tally, link.atoms.count == 1, "no atom added", link.atoms.count);
    const AtomId empty = link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 0, 0, 0, 0);
    const AtomId data = link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, 32, bytes, 0);
    const AtomId other_unit = link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, 32, bytes + 32, 1);
    const AtomId tdata = link_add_atom(&link, BUSTER_LINK_CLASS_TDATA, 3, 16, bytes, 0);
    const AtomId bss = link_add_atom(&link, BUSTER_LINK_CLASS_BSS, 3, 16, 0, 0);
    link_test_expect(&tally, empty == 1 && data == 2 && other_unit == 3 && tdata == 4 && bss == 5, "an empty atom without bytes, and others", 0);

    // Symbols.
    const uint8_t object = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT;
    const uint8_t local = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT;
    const uint8_t tls = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_TLS;
    const uint8_t program = BUSTER_LINK_SOURCE_PROGRAM;
    link_test_expect(&tally, link_add_symbol(&link, name + 1, 0, 0, 0, object, program) == 0 && link_test_failed(&link, LINK_ERROR_ID), "a name that doesn't exist", 0);
    link_test_expect(&tally, link_add_symbol(&link, name, 6, 0, 0, object, program) == 0 && link_test_failed(&link, LINK_ERROR_ID), "an atom that doesn't exist", 0);
    link_test_expect(&tally, link_add_symbol(&link, name, data, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | 3, program) == 0 && link_test_failed(&link, LINK_ERROR_RECORD),
                     "a section symbol", 0);
    link_test_expect(&tally, link_add_symbol(&link, name, data, 0, 0, BUSTER_LINK_SYMBOL_BINDING | BUSTER_LINK_SYMBOL_OBJECT, program) == 0 &&
                         link_test_failed(&link, LINK_ERROR_RECORD),
                     "a binding that doesn't exist", 0);
    link_test_expect(&tally, link_add_symbol(&link, name, data, 0, 0, object, BUSTER_LINK_SOURCE_COUNT) == 0 && link_test_failed(&link, LINK_ERROR_RECORD),
                     "a source that doesn't exist", 0);
    link_test_expect(&tally, link_add_symbol(&link, name, data, 33, 0, object, program) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "a value past the atom", 0);
    link_test_expect(&tally, link_add_symbol(&link, name, data, 24, 9, object, program) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "a size past the atom", 0);
    link_test_expect(&tally, link_add_symbol(&link, name, data, 0, 8, tls, program) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "a TLS symbol outside TLS", 0);
    link_test_expect(&tally, link_add_symbol(&link, name, tdata, 0, 8, object, program) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "another symbol in TLS", 0);
    link_test_expect(&tally, link.symbols.count == 1, "no symbol added", link.symbols.count);
    const SymbolId defined = link_add_symbol(&link, name, data, 24, 8, object, program);
    const SymbolId later = link_add_symbol(&link, 0, 0, 0, 0, local, program);
    const SymbolId variable = link_add_symbol(&link, 0, tdata, 8, 8, tls, program);
    const SymbolId foreign = link_add_symbol(&link, name, 0, 0, 0, object, BUSTER_LINK_SOURCE_RUNTIME_SHARED);
    link_test_expect(&tally, defined == 1 && later == 2 && variable == 3 && foreign == 4, "symbols that fit", 0);
    link_test_expect(&tally, !link_define_symbol(&link, 0, data, 0, 0) && link_test_failed(&link, LINK_ERROR_ID), "defining symbol 0", 0);
    link_test_expect(&tally, !link_define_symbol(&link, later, 0, 0, 0) && link_test_failed(&link, LINK_ERROR_ID), "defining in atom 0", 0);
    link_test_expect(&tally, !link_define_symbol(&link, later, tdata, 0, 8) && link_test_failed(&link, LINK_ERROR_RECORD), "defining outside TLS in TLS", 0);
    link_test_expect(&tally, !link_define_symbol(&link, defined, data, 0, 8) && link_test_failed(&link, LINK_ERROR_RECORD), "defining twice", 0);
    link_test_expect(&tally, link.symbols.atom[defined] == data && link.symbols.value[defined] == 24, "a refused definition changes nothing", 0);

    // A local without its atom is what link_check refuses; defined, it isn't.
    link_test_expect(&tally, link_check(&link) == LINK_ERROR_BINDING && link_test_failed(&link, LINK_ERROR_BINDING), "link_check: a program symbol without an atom", 0);
    link_test_expect(&tally, link_define_symbol(&link, later, other_unit, 8, 8), "defining later", 0);
    link_test_expect(&tally, link_check(&link) == LINK_ERROR_NONE, "link_check of what is valid", link.error);

    // Fixups.
    link_test_expect(&tally, link_add_fixup(&link, 0, 0, BUSTER_LINK_FIXUP_ABS64, defined, 0) == 0 && link_test_failed(&link, LINK_ERROR_ID), "a fixup of atom 0", 0);
    link_test_expect(&tally, link_add_fixup(&link, data, 0, BUSTER_LINK_FIXUP_ABS64, 0, 0) == 0 && link_test_failed(&link, LINK_ERROR_ID), "a fixup of symbol 0", 0);
    link_test_expect(&tally, link_add_fixup(&link, data, 0, BUSTER_LINK_FIXUP_ABS64, 5, 0) == 0 && link_test_failed(&link, LINK_ERROR_ID), "a target that doesn't exist", 0);
    link_test_expect(&tally, link_add_fixup(&link, data, 0, BUSTER_LINK_FIXUP_NONE, defined, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "kind none", 0);
    link_test_expect(&tally, link_add_fixup(&link, data, 0, BUSTER_LINK_FIXUP_KINDS, defined, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD),
                     "a kind past the last", 0);
    link_test_expect(&tally, link_add_fixup(&link, data, 0, BUSTER_LINK_FIXUP_ABS64 | BUSTER_LINK_FIXUP_WIDE, defined, 0) == 0 &&
                         link_test_failed(&link, LINK_ERROR_RECORD),
                     "the wide bit given", 0);
    link_test_expect(&tally, link_add_fixup(&link, data, 25, BUSTER_LINK_FIXUP_ABS64, defined, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD),
                     "a field past the atom", 0);
    link_test_expect(&tally, link_add_fixup(&link, bss, 0, BUSTER_LINK_FIXUP_ABS64, defined, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "a zero-fill atom",
                     0);
    link_test_expect(&tally, link_add_fixup(&link, data, 8, BUSTER_LINK_FIXUP_ABS64, defined, 0) == 1, "a fixup", 0);
    link_test_expect(&tally, link_add_fixup(&link, data, 4, BUSTER_LINK_FIXUP_PC32, defined, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD), "a fixup before it", 0);
    link_test_expect(&tally, link_add_fixup(&link, data, 15, BUSTER_LINK_FIXUP_PC32, defined, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD),
                     "a fixup overlapping it", 0);
    link_test_expect(&tally, link_add_fixup(&link, data, 16, BUSTER_LINK_FIXUP_PC32, defined, 0) == 2, "a fixup right after it", 0);
    link_test_expect(&tally, link_add_fixup(&link, other_unit, 0, BUSTER_LINK_FIXUP_ABS64, later, 0) == 3, "another atom's fixup", 0);
    link_test_expect(&tally, link_add_fixup(&link, data, 24, BUSTER_LINK_FIXUP_PC32, defined, 0) == 0 && link_test_failed(&link, LINK_ERROR_RECORD),
                     "a fixup of an atom whose range another follows", 0);
    link_test_expect(&tally, link.fixups.count == 4 && link.atoms.fixup_begin[data] == 1 && link.atoms.fixup_count[data] == 2, "the refused fixups change nothing",
                     link.fixups.count);
    link_test_expect(&tally, link_check(&link) == LINK_ERROR_NONE, "link_check of valid fixups", link.error);

    // What link_check refuses: TLS kinds and the others on the wrong symbols, and a local from another unit.
    static const struct
    {
        uint8_t kind;
        uint8_t target;
        const char* what;
    } wrong[] = {
        {
            BUSTER_LINK_FIXUP_TPOFF32,
            1,
            "a TLS kind to a symbol outside TLS",
        },
        {
            BUSTER_LINK_FIXUP_PC32,
            3,
            "another kind to a TLS symbol",
        },
        {
            BUSTER_LINK_FIXUP_ABS32,
            2,
            "a local of another unit",
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(wrong); i += 1)
    {
        const AtomId atom = link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, 8, bytes, 0);
        const bool added = atom != 0 && link_add_fixup(&link, atom, 0, wrong[i].kind, wrong[i].target, 0) != 0;
        link_test_expect(&tally, added && link_check(&link) == LINK_ERROR_BINDING && link_test_failed(&link, LINK_ERROR_BINDING), wrong[i].what, i);
        // Retargeted to a symbol it can refer to, it passes again.
        link.fixups.target[link.fixups.count - 1] = wrong[i].kind == BUSTER_LINK_FIXUP_TPOFF32 ? variable : defined;
        link_test_expect(&tally, link_check(&link) == LINK_ERROR_NONE, "link_check after retargeting", i);
    }
    // The size of a TLS symbol is fine.
    const AtomId sized = link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, 8, bytes, 0);
    link_test_expect(&tally, sized != 0 && link_add_fixup(&link, sized, 0, BUSTER_LINK_FIXUP_SIZE64, variable, 0) != 0 && link_check(&link) == LINK_ERROR_NONE,
                     "a TLS symbol's size", 0);
    // A fixup outside every atom's range, and an atom whose range overlaps another's, which only lanes filling the
    // columns themselves could make.
    link.atoms.fixup_count[data] -= 1;
    link_test_expect(&tally, link_check(&link) == LINK_ERROR_RECORD && link_test_failed(&link, LINK_ERROR_RECORD), "a fixup in no atom's range", 0);
    link.atoms.fixup_count[data] += 1;
    link.atoms.fixup_begin[other_unit] = link.atoms.fixup_begin[data];
    link_test_expect(&tally, link_check(&link) == LINK_ERROR_RECORD && link_test_failed(&link, LINK_ERROR_RECORD), "overlapping ranges", 0);
    link_test_release(&arenas);
    return link_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Kinds

typedef struct LinkTestKnownFixup LinkTestKnownFixup;
struct LinkTestKnownFixup
{
    // Without the fixup flag, which the checks add.
    X86Instruction instruction;
    uint8_t field;
    uint8_t kind;
    // The field's target is a TLS variable.
    bool tls;
    // The source's addend, and GNU as's: its relocation's, which is the fixup's.
    int8_t source_addend;
    int8_t addend;
    // GNU as's bytes for the instruction (with the field zero), their length, and the field's offset in them.
    uint8_t length;
    uint8_t offset;
    uint8_t bytes[BUSTER_X86_MAX_INSTRUCTION_BYTES];
};

// Instructions with a symbolic field of every kind, and what GNU as 2.42 assembles them to; the comments are its
// relocations (as readelf -r prints them).
static const LinkTestKnownFixup link_test_known_fixups[] = {
    // call f; jmp f; je f (R_X86_64_PLT32 f - 4)
    {
        { .form = X86_FORM_CALL_REL32 },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_CALL,
        false,
        0,
        -4,
        5,
        1,
        { 0xe8 },
    },
    {
        { .form = X86_FORM_JMP_REL32 },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_CALL,
        false,
        0,
        -4,
        5,
        1,
        { 0xe9 },
    },
    {
        { .form = X86_FORM_JE_REL32 },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_CALL,
        false,
        0,
        -4,
        6,
        2,
        { 0x0f, 0x84 },
    },
    // call qword ptr [rip + f@GOTPCREL]; jmp qword ptr [rip + f@GOTPCREL] (R_X86_64_GOTPCRELX f - 4)
    {
        { .form = X86_FORM_CALL_RM64, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_CALL_GOT,
        false,
        0,
        -4,
        6,
        2,
        { 0xff, 0x15 },
    },
    {
        { .form = X86_FORM_JMP_RM64, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_CALL_GOT,
        false,
        0,
        -4,
        6,
        2,
        { 0xff, 0x25 },
    },
    // mov rax, qword ptr [rip + x@GOTPCREL]; mov r12, qword ptr [rip + x@GOTPCREL] (R_X86_64_REX_GOTPCRELX x - 4)
    {
        { .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_LOAD_GOT,
        false,
        0,
        -4,
        7,
        3,
        { 0x48, 0x8b, 0x05 },
    },
    {
        { .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_R12, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_LOAD_GOT,
        false,
        0,
        -4,
        7,
        3,
        { 0x4c, 0x8b, 0x25 },
    },
    // push qword ptr [rip + x@GOTPCREL] (R_X86_64_GOTPCREL x - 4)
    {
        { .form = X86_FORM_PUSH_RM64, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_GOTPCREL,
        false,
        0,
        -4,
        6,
        2,
        { 0xff, 0x35 },
    },
    // lea rax, [rip + x] (R_X86_64_PC32 x - 4); cmp byte ptr [rip + x], 1 (x - 5); mov dword ptr [rip + x], 0x12345678
    // (x - 8); mov rax, qword ptr [rip + x + 8] (x + 4)
    {
        { .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_PC32,
        false,
        0,
        -4,
        7,
        3,
        { 0x48, 0x8d, 0x05 },
    },
    {
        { .immediate = 1, .form = X86_FORM_CMP_RM8_IMM8, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_PC32,
        false,
        0,
        -5,
        7,
        2,
        { 0x80, 0x3d, 0, 0, 0, 0, 0x01 },
    },
    {
        { .immediate = 0x12345678, .form = X86_FORM_MOV_RM32_IMM32, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_PC32,
        false,
        0,
        -8,
        10,
        2,
        { 0xc7, 0x05, 0, 0, 0, 0, 0x78, 0x56, 0x34, 0x12 },
    },
    {
        { .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_PC32,
        false,
        8,
        4,
        7,
        3,
        { 0x48, 0x8b, 0x05 },
    },
    // mov eax, dword ptr fs:[x@tpoff]; mov rax, qword ptr fs:[x@tpoff]; lea rax, [rax + x@tpoff] (with a placeholder
    // that doesn't fit in 8 bits); add qword ptr fs:[x@tpoff], rbx (R_X86_64_TPOFF32 x + 0)
    {
        { .form = X86_FORM_MOV_R32_RM32 + BUSTER_X86_FORM_FS, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_ABSOLUTE },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_TPOFF32,
        true,
        0,
        0,
        8,
        4,
        { 0x64, 0x8b, 0x04, 0x25 },
    },
    {
        { .form = X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_ABSOLUTE },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_TPOFF32,
        true,
        0,
        0,
        9,
        5,
        { 0x64, 0x48, 0x8b, 0x04, 0x25 },
    },
    {
        { .displacement = INT32_MIN, .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_TPOFF32,
        true,
        0,
        0,
        7,
        3,
        { 0x48, 0x8d, 0x80 },
    },
    {
        { .form = X86_FORM_ADD_RM64_R64 + BUSTER_X86_FORM_FS, .reg = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_ABSOLUTE },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_TPOFF32,
        true,
        0,
        0,
        9,
        5,
        { 0x64, 0x48, 0x01, 0x1c, 0x25 },
    },
    // mov rax, qword ptr [rip + x@gottpoff]; add rax, qword ptr [rip + x@gottpoff] (R_X86_64_GOTTPOFF x - 4)
    {
        { .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_GOTTPOFF,
        true,
        0,
        -4,
        7,
        3,
        { 0x48, 0x8b, 0x05 },
    },
    {
        { .form = X86_FORM_ADD_R64_RM64, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_GOTTPOFF,
        true,
        0,
        -4,
        7,
        3,
        { 0x48, 0x03, 0x05 },
    },
    // movabs rax, offset x (R_X86_64_64 x + 0)
    {
        { .form = X86_FORM_MOV_R64_IMM64, .rm = X86_REGISTER_RAX },
        X86_FIXUP_IMMEDIATE,
        BUSTER_LINK_FIXUP_ABS64,
        false,
        0,
        0,
        10,
        2,
        { 0x48, 0xb8 },
    },
    // mov eax, offset x (R_X86_64_32 x + 0)
    {
        { .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RAX },
        X86_FIXUP_IMMEDIATE,
        BUSTER_LINK_FIXUP_ABS32,
        false,
        0,
        0,
        5,
        1,
        { 0xb8 },
    },
    // mov rax, offset x; mov eax, dword ptr [x + rcx*4] (R_X86_64_32S x + 0)
    {
        { .form = X86_FORM_MOV_RM64_IMM32, .rm = X86_REGISTER_RAX },
        X86_FIXUP_IMMEDIATE,
        BUSTER_LINK_FIXUP_ABS32S,
        false,
        0,
        0,
        7,
        3,
        { 0x48, 0xc7, 0xc0 },
    },
    {
        { .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_INDEX(2) },
        X86_FIXUP_DISPLACEMENT,
        BUSTER_LINK_FIXUP_ABS32S,
        false,
        0,
        0,
        7,
        3,
        { 0x8b, 0x04, 0x8d },
    },
    // mov eax, offset x@SIZE (R_X86_64_SIZE32 x + 0); movabs rax, offset x@SIZE (R_X86_64_SIZE64 x + 0)
    {
        { .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RAX },
        X86_FIXUP_IMMEDIATE,
        BUSTER_LINK_FIXUP_SIZE32,
        false,
        0,
        0,
        5,
        1,
        { 0xb8 },
    },
    {
        { .form = X86_FORM_MOV_R64_IMM64, .rm = X86_REGISTER_RAX },
        X86_FIXUP_IMMEDIATE,
        BUSTER_LINK_FIXUP_SIZE64,
        false,
        0,
        0,
        10,
        2,
        { 0x48, 0xb8 },
    },
};

// A link for the kinds checks, with two targets: an object and a TLS variable, neither defined (atom 0).
typedef struct LinkTestTargets LinkTestTargets;
struct LinkTestTargets
{
    SymbolId object;
    SymbolId tls;
};

static LinkTestTargets link_test_targets(Link* link)
{
    LinkTestTargets result = {
        .object = link_add_symbol(link, link_add_name(link, (const uint8_t*)"x", 1), 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, 0),
        .tls = link_add_symbol(link, link_add_name(link, (const uint8_t*)"t", 1), 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_TLS, 0),
    };
    return result;
}

static LinkReference link_test_reference(const LinkTestKnownFixup* known, uint32_t instruction, const LinkTestTargets* targets)
{
    LinkReference result = {
        .instruction = instruction,
        .field = known->field,
        .kind = known->kind,
        .target = known->tls ? targets->tls : targets->object,
        .addend = known->source_addend,
    };
    return result;
}

// Encodes `count` instructions as an atom of `link`: their bytes in `bytes` (with room for x86_encode), their offsets
// and the marked ones' ends. Returns the atom, or 0.
static AtomId link_test_encode(Link* link, Arena* bytes, const X86Instruction* instructions, uint64_t count, uint32_t* offsets, uint32_t* fixup_ends)
{
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(count);
    uint8_t* output = arena_push(bytes, capacity, 16);
    const int64_t length = output != 0 ? x86_encode(instructions, count, output, capacity, offsets, fixup_ends) : -1;
    return length >= 0 ? link_add_atom(link, BUSTER_LINK_CLASS_TEXT, 4, (uint64_t)length, output, 0) : 0;
}

// The instructions that the streams put between the marked ones.
static const X86Instruction link_test_unmarked[] = {
    { .form = X86_FORM_NOP },
    { .immediate = 1, .rm = X86_REGISTER_RAX, .form = X86_FORM_ADD_RM64_IMM8 },
    { .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX, .form = X86_FORM_MOV_R64_RM64 },
    { .displacement = 8, .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_LEA_R64_M },
    { .reg = 1, .rm = 3, .form = X86_FORM_VPADDD_Z_Z_ZM512, .vvvv = 2 },
};

static bool link_test_kinds(uint64_t seed)
{
    LinkTestTally tally = {
        .check = "kinds",
    };
    uint64_t state = link_program_scramble(seed ^ BUSTER_LINK_TEST_KINDS_STREAM);
    LinkTestArenas arenas = link_test_reserve();
    Scratch scratch = scratch_begin(0);
    const uint32_t known_count = BUSTER_ARRAY_LENGTH(link_test_known_fixups);
    const uint32_t most = link_test_stream_lengths[BUSTER_LINK_TEST_STREAMS - 1];
    // A stream's instructions: each marked one with up to 3 unmarked ones before it.
    const uint32_t most_instructions = 4 * most;
    X86Instruction* instructions = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, X86Instruction, most_instructions);
    LinkReference* references = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, LinkReference, 2 * most);
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, most_instructions);
    uint32_t* fixup_ends = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(most));
    // Each marked instruction's known fixup, or known_count for the instruction with two fields.
    uint32_t* chosen = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, most);
    uint32_t* indices = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, most);
    const LinkCapacity capacity = {
        .names = 4,
        .atoms = 2 * known_count + BUSTER_LINK_TEST_STREAMS + 64,
        .symbols = 4,
        .fixups = 4 * most + 2 * known_count + 64,
        .wide_addends = 4,
    };
    Link link;
    const bool ready = instructions != 0 && references != 0 && offsets != 0 && fixup_ends != 0 && chosen != 0 && indices != 0 &&
                       link_initialize(&link, &arenas.tables, &arenas.names, capacity);
    link_test_expect(&tally, ready, "memory and tables", 0);
    const LinkTestTargets targets = ready ? link_test_targets(&link) : (LinkTestTargets){ 0 };

    // Each known fixup alone.
    for (uint32_t i = 0; ready && i < known_count; i += 1)
    {
        const LinkTestKnownFixup* known = &link_test_known_fixups[i];
        X86Instruction instruction = known->instruction;
        instruction.form |= BUSTER_X86_FORM_FIXUP;
        const AtomId atom = link_test_encode(&link, &arenas.bytes, &instruction, 1, 0, fixup_ends);
        const LinkReference reference = link_test_reference(known, 0, &targets);
        const FixupId fixup = link.fixups.count;
        const bool added = atom != 0 && link_add_code_fixups(&link, atom, &instruction, 1, fixup_ends, 1, &reference, 1);
        const bool right = added && link.fixups.count == fixup + 1 && link.fixups.offset[fixup] == known->offset && link.fixups.kind[fixup] == known->kind &&
                           link.fixups.target[fixup] == reference.target && link_fixup_addend(&link, fixup) == known->addend;
        link_test_expect(&tally, right, "a known fixup's offset, kind and addend", i);
        link_test_expect(&tally, added && link.atoms.size[atom] == known->length && memcmp(link.atoms.bytes[atom], known->bytes, known->length) == 0,
                         "a known fixup's bytes, with the field zero", i);
    }

    // Streams: known fixups chosen at random with unmarked instructions between them, and one in eight the instruction
    // with two fields, mov dword ptr [rip + x], offset y (R_X86_64_PC32 x - 8 at 2, and R_X86_64_32 y at 6).
    static const uint8_t two_fields[] = { 0xc7, 0x05, 0, 0, 0, 0, 0, 0, 0, 0 };
    for (uint32_t stream = 0; ready && stream < BUSTER_LINK_TEST_STREAMS; stream += 1)
    {
        const uint32_t marked = link_test_stream_lengths[stream];
        uint32_t count = 0;
        uint32_t reference_count = 0;
        for (uint32_t i = 0; i < marked; i += 1)
        {
            const uint32_t unmarked = link_test_below(&state, 4);
            for (uint32_t j = 0; j < unmarked; j += 1)
            {
                instructions[count] = link_test_unmarked[link_test_below(&state, BUSTER_ARRAY_LENGTH(link_test_unmarked))];
                count += 1;
            }
            chosen[i] = link_test_below(&state, 8) == 0 ? known_count : link_test_below(&state, known_count);
            indices[i] = count;
            if (chosen[i] == known_count)
            {
                instructions[count] = (X86Instruction){
                    .mode = BUSTER_X86_MEMORY_RIP,
                    .form = X86_FORM_MOV_RM32_IMM32 | BUSTER_X86_FORM_FIXUP,
                };
                references[reference_count] = (LinkReference){
                    .instruction = count,
                    .field = X86_FIXUP_DISPLACEMENT,
                    .kind = BUSTER_LINK_FIXUP_PC32,
                    .target = targets.object,
                    .addend = 0,
                };
                references[reference_count + 1] = (LinkReference){
                    .instruction = count,
                    .field = X86_FIXUP_IMMEDIATE,
                    .kind = BUSTER_LINK_FIXUP_ABS32,
                    .target = targets.object,
                    .addend = 0,
                };
                reference_count += 2;
            }
            else
            {
                instructions[count] = link_test_known_fixups[chosen[i]].instruction;
                instructions[count].form |= BUSTER_X86_FORM_FIXUP;
                references[reference_count] = link_test_reference(&link_test_known_fixups[chosen[i]], count, &targets);
                reference_count += 1;
            }
            count += 1;
        }
        const AtomId atom = link_test_encode(&link, &arenas.bytes, instructions, count, offsets, fixup_ends);
        const FixupId first = link.fixups.count;
        const bool added = atom != 0 && link_add_code_fixups(&link, atom, instructions, count, fixup_ends, marked, references, reference_count);
        link_test_expect(&tally, added && link.fixups.count == first + reference_count && link.atoms.fixup_count[atom] == reference_count,
                         "a stream's fixups", marked);
        FixupId fixup = first;
        for (uint32_t i = 0; added && i < marked; i += 1)
        {
            const uint32_t start = offsets[indices[i]];
            const uint8_t* encoded = link.atoms.bytes[atom] + start;
            if (chosen[i] == known_count)
            {
                const bool right = link.fixups.offset[fixup] == start + 2 && link.fixups.kind[fixup] == BUSTER_LINK_FIXUP_PC32 && link.fixups.addend[fixup] == -8 &&
                                   link.fixups.offset[fixup + 1] == start + 6 && link.fixups.kind[fixup + 1] == BUSTER_LINK_FIXUP_ABS32 &&
                                   link.fixups.addend[fixup + 1] == 0 && memcmp(encoded, two_fields, sizeof(two_fields)) == 0;
                link_test_expect(&tally, right, "a stream's instruction with two fields", i);
                fixup += 2;
            }
            else
            {
                const LinkTestKnownFixup* known = &link_test_known_fixups[chosen[i]];
                const bool right = link.fixups.offset[fixup] == start + known->offset && link.fixups.kind[fixup] == known->kind &&
                                   link_fixup_addend(&link, fixup) == known->addend && memcmp(encoded, known->bytes, known->length) == 0;
                link_test_expect(&tally, right, "a stream's known fixup", i);
                fixup += 1;
            }
        }
    }

    // Kinds on fields they can't patch.
    static const struct
    {
        X86Instruction instruction;
        uint8_t field;
        uint8_t kind;
        const char* what;
    } refused[] = {
        {
            { .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_LEA_R64_M },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_CALL,
            "CALL on a RIP-relative displacement",
        },
        {
            { .form = X86_FORM_JMP_REL8 },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_CALL,
            "CALL on a rel8",
        },
        {
            { .form = X86_FORM_CALL_REL32 },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_GOTPCREL,
            "GOTPCREL on a rel32",
        },
        {
            { .reg = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_INDEX(2), .form = X86_FORM_MOV_R32_RM32 },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_PC32,
            "PC32 on an absolute displacement",
        },
        {
            { .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_LEA_R64_M },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_LOAD_GOT,
            "LOAD_GOT on lea",
        },
        {
            { .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_LOAD_GOT,
            "LOAD_GOT with a segment override",
        },
        {
            { .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_MOV_R64_RM64 },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_CALL_GOT,
            "CALL_GOT on mov",
        },
        {
            { .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_LEA_R64_M },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_GOTTPOFF,
            "GOTTPOFF on lea",
        },
        {
            { .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_MOV_R64_RM64 },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_TPOFF32,
            "TPOFF32 on a RIP-relative displacement",
        },
        {
            { .displacement = 8, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_LEA_R64_M },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_TPOFF32,
            "TPOFF32 on an 8-bit displacement",
        },
        {
            { .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
            X86_FIXUP_IMMEDIATE,
            BUSTER_LINK_FIXUP_ABS64,
            "ABS64 on an imm32",
        },
        {
            { .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R64_IMM64 },
            X86_FIXUP_IMMEDIATE,
            BUSTER_LINK_FIXUP_ABS32,
            "ABS32 on an imm64",
        },
        {
            { .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
            X86_FIXUP_IMMEDIATE,
            BUSTER_LINK_FIXUP_SIZE64,
            "SIZE64 on an imm32",
        },
        {
            { .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R64_IMM64 },
            X86_FIXUP_IMMEDIATE,
            BUSTER_LINK_FIXUP_PC64,
            "PC64 on an instruction",
        },
        {
            { .form = X86_FORM_CALL_REL32 },
            X86_FIXUP_IMMEDIATE,
            BUSTER_LINK_FIXUP_ABS32,
            "an immediate a branch doesn't have",
        },
        {
            { .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R32_IMM32 },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_ABS32,
            "a displacement mov r32, imm32 doesn't have",
        },
        {
            { .immediate = 5, .rm = X86_REGISTER_RAX, .form = X86_FORM_ADD_RM64_IMM8 },
            X86_FIXUP_IMMEDIATE,
            BUSTER_LINK_FIXUP_ABS32,
            "an imm8",
        },
        {
            { .form = X86_FORM_CALL_REL32 },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_NONE,
            "kind none",
        },
        {
            { .form = X86_FORM_CALL_REL32 },
            X86_FIXUP_DISPLACEMENT,
            BUSTER_LINK_FIXUP_KINDS,
            "a kind past the last",
        },
    };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(refused); i += 1)
    {
        X86Instruction instruction = refused[i].instruction;
        instruction.form |= BUSTER_X86_FORM_FIXUP;
        const AtomId atom = link_test_encode(&link, &arenas.bytes, &instruction, 1, 0, fixup_ends);
        const LinkReference reference = {
            .instruction = 0,
            .field = refused[i].field,
            .kind = refused[i].kind,
            .target = targets.object,
            .addend = 0,
        };
        const FixupId count = link.fixups.count;
        const bool refuses = atom != 0 && !link_add_code_fixups(&link, atom, &instruction, 1, fixup_ends, 1, &reference, 1);
        link_test_expect(&tally, refuses && link_test_failed(&link, LINK_ERROR_FIELD) && link.fixups.count == count, refused[i].what, i);
    }

    // References that don't match the marked instructions: two marked, call f and mov dword ptr [rip + x], offset y,
    // after an unmarked nop.
    if (ready)
    {
        X86Instruction three[] = {
            { .form = X86_FORM_NOP },
            { .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP },
            { .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_MOV_RM32_IMM32 | BUSTER_X86_FORM_FIXUP },
        };
        const AtomId atom = link_test_encode(&link, &arenas.bytes, three, 3, 0, fixup_ends);
        const LinkReference call = {
            .instruction = 1,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_CALL,
            .target = targets.object,
        };
        const LinkReference displacement = {
            .instruction = 2,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_PC32,
            .target = targets.object,
        };
        const LinkReference immediate = {
            .instruction = 2,
            .field = X86_FIXUP_IMMEDIATE,
            .kind = BUSTER_LINK_FIXUP_ABS32,
            .target = targets.object,
        };
        LinkReference nop = call;
        nop.instruction = 0;
        LinkReference past = call;
        past.instruction = 3;
        LinkReference overflowing = call;
        overflowing.addend = INT64_MIN;
        static const char* const cases[] = {
            "a marked instruction without a reference",
            "a reference to an unmarked instruction",
            "references out of order",
            "the immediate before the displacement",
            "a field twice",
            "an instruction past the last",
            "an addend that overflows with the distance",
            "no references to marked instructions",
        };
        const LinkReference lists[][3] = {
            { call },
            { nop, call, displacement },
            { displacement, call },
            { call, immediate, displacement },
            { call, displacement, displacement },
            { call, displacement, past },
            { overflowing, displacement },
            { call },
        };
        static const uint32_t counts[] = { 1, 3, 2, 3, 3, 3, 2, 0 };
        for (uint32_t i = 0; atom != 0 && i < BUSTER_ARRAY_LENGTH(cases); i += 1)
        {
            // Each case on a fresh copy of the atom, since the fixups added before a failure stay.
            const AtomId copy = link_add_atom(&link, BUSTER_LINK_CLASS_TEXT, 4, link.atoms.size[atom], link.atoms.bytes[atom], 0);
            link_test_expect(&tally, copy != 0 && !link_add_code_fixups(&link, copy, three, 3, fixup_ends, 2, lists[i], counts[i]) &&
                                         link_test_failed(&link, LINK_ERROR_FIELD),
                             cases[i], i);
        }
        const LinkReference both[] = { call, displacement, immediate };
        const AtomId copy = link_add_atom(&link, BUSTER_LINK_CLASS_TEXT, 4, link.atoms.size[atom], link.atoms.bytes[atom], 0);
        link_test_expect(&tally, copy != 0 && link_add_code_fixups(&link, copy, three, 3, fixup_ends, 2, both, 3), "the references that match", 0);
        link_test_expect(&tally, link_add_code_fixups(&link, copy, three, 1, fixup_ends, 0, both, 0), "no marked instructions and no references", 0);
    }
    scratch_end(scratch);
    link_test_release(&arenas);
    return link_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Wide addends

static bool link_test_wide(void)
{
    LinkTestTally tally = {
        .check = "wide addends",
    };
    LinkTestArenas arenas = link_test_reserve();
    static uint8_t bytes[256];
    const LinkCapacity capacity = {
        .names = 4,
        .atoms = 16,
        .symbols = 4,
        .fixups = 64,
        .wide_addends = 32,
    };
    Link link;
    const bool ready = link_initialize(&link, &arenas.tables, &arenas.names, capacity);
    link_test_expect(&tally, ready, "the tables fit", 0);
    const SymbolId target = ready ? link_add_symbol(&link, 0, 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, 0) : 0;
    const AtomId atom = ready ? link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, sizeof(bytes), bytes, 0) : 0;
    static const int64_t addends[] = {
        0,
        1,
        -1,
        INT32_MAX,
        (int64_t)INT32_MAX + 1,
        INT32_MIN,
        (int64_t)INT32_MIN - 1,
        1ll << 32,
        -(1ll << 32),
        INT64_MAX,
        INT64_MIN,
    };
    uint32_t wide_count = 1;
    for (uint32_t i = 0; atom != 0 && i < BUSTER_ARRAY_LENGTH(addends); i += 1)
    {
        const int64_t addend = addends[i];
        const bool wide = addend < INT32_MIN || addend > INT32_MAX;
        const FixupId fixup = link_add_fixup(&link, atom, 8 * i, BUSTER_LINK_FIXUP_ABS64, target, addend);
        const bool kind = fixup != 0 && link.fixups.kind[fixup] == (BUSTER_LINK_FIXUP_ABS64 | (wide ? BUSTER_LINK_FIXUP_WIDE : 0));
        const bool column = fixup != 0 && (wide ? (uint32_t)link.fixups.addend[fixup] == wide_count && link.fixups.wide_addends[wide_count] == addend
                                                : link.fixups.addend[fixup] == addend);
        link_test_expect(&tally, kind && column && link_fixup_addend(&link, fixup) == addend, "an addend in the column or the side table", i);
        wide_count += wide ? 1 : 0;
    }
    link_test_expect(&tally, link.fixups.wide_count == wide_count, "the side table's entries", link.fixups.wide_count);

    // Through an instruction's distance to its end: lea rax, [rip + x] takes 4 off the source's addend, so INT32_MIN + 4
    // fits and INT32_MIN + 3 doesn't, and INT32_MAX + 4 fits and INT32_MAX + 5 doesn't. A movabs's ABS64 takes
    // nothing off.
    static const struct
    {
        int64_t source;
        int64_t addend;
        bool wide;
    } through[] = {
        {
            (int64_t)INT32_MIN + 4,
            INT32_MIN,
            false,
        },
        {
            (int64_t)INT32_MIN + 3,
            (int64_t)INT32_MIN - 1,
            true,
        },
        {
            (int64_t)INT32_MAX + 4,
            INT32_MAX,
            false,
        },
        {
            (int64_t)INT32_MAX + 5,
            (int64_t)INT32_MAX + 1,
            true,
        },
    };
    uint32_t fixup_ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(1)];
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(through) + 1; i += 1)
    {
        const bool movabs = i == BUSTER_ARRAY_LENGTH(through);
        const X86Instruction instruction = movabs ? (X86Instruction){ .rm = X86_REGISTER_RAX, .form = X86_FORM_MOV_R64_IMM64 | BUSTER_X86_FORM_FIXUP }
                                                  : (X86Instruction){ .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP, .form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP };
        const int64_t source = movabs ? 1ll << 40 : through[i].source;
        const int64_t expected = movabs ? 1ll << 40 : through[i].addend;
        const bool wide = movabs || through[i].wide;
        const AtomId code = link_test_encode(&link, &arenas.bytes, &instruction, 1, 0, fixup_ends);
        const LinkReference reference = {
            .instruction = 0,
            .field = movabs ? X86_FIXUP_IMMEDIATE : X86_FIXUP_DISPLACEMENT,
            .kind = movabs ? BUSTER_LINK_FIXUP_ABS64 : BUSTER_LINK_FIXUP_PC32,
            .target = target,
            .addend = source,
        };
        const FixupId fixup = link.fixups.count;
        const bool added = code != 0 && link_add_code_fixups(&link, code, &instruction, 1, fixup_ends, 1, &reference, 1);
        link_test_expect(&tally, added && link_fixup_addend(&link, fixup) == expected && ((link.fixups.kind[fixup] & BUSTER_LINK_FIXUP_WIDE) != 0) == wide,
                         "an instruction's addend in the column or the side table", i);
    }
    link_test_release(&arenas);
    return link_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Programs

// A program laid out in memory as a static executable at a fixed address would be, by link_test_load: three segments,
// each from a page boundary: R (RODATA and STRINGS), RX (TEXT) and RW (the TLS image, INIT_ARRAY, FINI_ARRAY,
// DATA_RELRO, the GOT, DATA, BSS, and the program headers), atoms in class order and id order within a class, each at
// its alignment. At a non-PIE executable's usual address, BUSTER_LINK_TEST_IMAGE_ADDRESS, or if something is mapped
// there, wherever MAP_32BIT puts it (a 1 GB window); either way below 2 GB, so that ABS32 and ABS32S fit. A program
// that calls libc gets stand-ins for its imports (link_test_import), and the child that runs it does what the C
// runtime would (link_test_start_libc).
typedef struct LinkTestImage LinkTestImage;
struct LinkTestImage
{
    uint8_t* base;
    uint64_t size;
    // _start's address, or main's with libc.
    uint64_t entry;
    bool libc;
    // The program headers the auxiliary vector points at: PT_PHDR, PT_LOAD and PT_TLS.
    Elf64_Phdr* headers;
    uint32_t header_count;
    // The init and fini arrays' addresses, from start to end.
    uint64_t init_begin;
    uint64_t init_end;
    uint64_t fini_begin;
    uint64_t fini_end;
    const char* problem;
};

#define BUSTER_LINK_TEST_PAGE_BYTES 4096
#define BUSTER_LINK_TEST_HEADERS 3
#define BUSTER_LINK_TEST_IMAGE_ADDRESS 0x400000ull
#define BUSTER_LINK_TEST_IMAGE_LIMIT (1ull << 31)
// What the thread pointer has above it in link_test_start_libc's TLS area: its own address, in the first 8 bytes, and
// zeros where glibc keeps its stack guard (at 0x28), which code built with a stack protector reads.
#define BUSTER_LINK_TEST_TCB_BYTES 64

static uint64_t link_test_align(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// Places the atoms of `class` from *cursor (offsets from the image's base, or for TLS atoms from the TLS segment's
// start).
static void link_test_place(const Link* link, uint8_t class, uint64_t* offsets, uint64_t* cursor)
{
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        if (link->atoms.class[atom] == class)
        {
            *cursor = link_test_align(*cursor, 1ull << link->atoms.alignment_log2[atom]);
            offsets[atom] = *cursor;
            *cursor += link->atoms.size[atom];
        }
    }
}

// Whether a symbol's name is `text`.
static bool link_test_named(const Link* link, SymbolId symbol, const char* text)
{
    const NameId name = link->symbols.name[symbol];
    const uint64_t length = strlen(text);
    return name != 0 && link->names.length[name] == length && memcmp(link->names.bytes[name], text, length) == 0;
}

// Stand-ins for what a program that calls libc imports: write, strlen, __errno_location and environ. They use no TLS,
// since the program's TLS block is FS's while it runs.
static char** link_test_environ;
static int32_t link_test_errno;

static int64_t link_test_write(int64_t descriptor, const void* bytes, uint64_t count)
{
    int64_t result = 1;
    __asm__ volatile("syscall" : "+a"(result) : "D"(descriptor), "S"(bytes), "d"(count) : "rcx", "r11", "memory");
    return result;
}

static uint64_t link_test_strlen(const char* text)
{
    uint64_t result = 0;
    while (text[result] != 0)
    {
        result += 1;
    }
    return result;
}

static int32_t* link_test_errno_location(void)
{
    return &link_test_errno;
}

// The address of an import's stand-in, or 0 if it has none.
static uint64_t link_test_import(const Link* link, SymbolId symbol)
{
    uint64_t result = 0;
    if (link_test_named(link, symbol, "write"))
    {
        result = (uint64_t)link_test_write;
    }
    else if (link_test_named(link, symbol, "strlen"))
    {
        result = (uint64_t)link_test_strlen;
    }
    else if (link_test_named(link, symbol, "__errno_location"))
    {
        result = (uint64_t)link_test_errno_location;
    }
    else if (link_test_named(link, symbol, "environ"))
    {
        result = (uint64_t)&link_test_environ;
    }
    return result;
}

static bool link_test_load(const Link* link, SymbolId entry, bool libc, bool misaligned, Arena* arena, LinkTestImage* image)
{
    const LinkAtoms* atoms = &link->atoms;
    const LinkSymbols* symbols = &link->symbols;
    const LinkFixups* fixups = &link->fixups;
    *image = (LinkTestImage){
        .libc = libc,
    };
    uint64_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, atoms->count);
    uint64_t* values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, symbols->count);
    uint32_t* slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbols->count);
    bool result = offsets != 0 && values != 0 && slots != 0;
    image->problem = result ? 0 : "out of memory";

    // A GOT slot for every target of a kind that reads one.
    uint32_t slot_count = 0;
    for (SymbolId symbol = 0; result && symbol < symbols->count; symbol += 1)
    {
        slots[symbol] = UINT32_MAX;
    }
    for (FixupId fixup = 1; result && fixup < fixups->count; fixup += 1)
    {
        const uint8_t kind = fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND;
        const SymbolId target = fixups->target[fixup];
        const bool got = kind == BUSTER_LINK_FIXUP_CALL_GOT || kind == BUSTER_LINK_FIXUP_LOAD_GOT || kind == BUSTER_LINK_FIXUP_GOTPCREL || kind == BUSTER_LINK_FIXUP_GOTTPOFF;
        if (got && slots[target] == UINT32_MAX)
        {
            slots[target] = slot_count;
            slot_count += 1;
        }
    }

    // The layout, as offsets from the base.
    uint64_t cursor = 0;
    uint64_t tls_alignment = 1;
    uint64_t init_begin = 0;
    uint64_t init_end = 0;
    uint64_t fini_begin = 0;
    uint64_t fini_end = 0;
    uint64_t got = 0;
    uint64_t headers = 0;
    uint64_t tls_image = 0;
    uint64_t tls_file = 0;
    uint64_t tls_memory = 0;
    uint64_t text_begin = 0;
    uint64_t text_end = 0;
    uint64_t data_begin = 0;
    if (result)
    {
        link_test_place(link, BUSTER_LINK_CLASS_RODATA, offsets, &cursor);
        link_test_place(link, BUSTER_LINK_CLASS_STRINGS, offsets, &cursor);
        text_begin = link_test_align(cursor, BUSTER_LINK_TEST_PAGE_BYTES);
        cursor = text_begin;
        link_test_place(link, BUSTER_LINK_CLASS_TEXT, offsets, &cursor);
        text_end = cursor;
        data_begin = link_test_align(cursor, BUSTER_LINK_TEST_PAGE_BYTES);
        cursor = data_begin;
        for (AtomId atom = 1; atom < atoms->count; atom += 1)
        {
            const uint64_t alignment = 1ull << atoms->alignment_log2[atom];
            tls_alignment = link_class_is_tls(atoms->class[atom]) && alignment > tls_alignment ? alignment : tls_alignment;
        }
        // The TLS segment: TDATA's atoms, then TBSS's (which take no room in the image), from a multiple of the
        // largest TLS alignment, or if `misaligned` from half of it past one, as a linker may start the segment when
        // TBSS is more aligned than TDATA.
        tls_image = link_test_align(cursor, tls_alignment) + (misaligned ? tls_alignment / 2 : 0);
        uint64_t tls_cursor = tls_image;
        link_test_place(link, BUSTER_LINK_CLASS_TDATA, offsets, &tls_cursor);
        tls_file = tls_cursor - tls_image;
        link_test_place(link, BUSTER_LINK_CLASS_TBSS, offsets, &tls_cursor);
        tls_memory = tls_cursor - tls_image;
        cursor = tls_image + tls_file;
        init_begin = link_test_align(cursor, 8);
        cursor = init_begin;
        link_test_place(link, BUSTER_LINK_CLASS_INIT_ARRAY, offsets, &cursor);
        init_end = cursor;
        fini_begin = link_test_align(cursor, 8);
        cursor = fini_begin;
        link_test_place(link, BUSTER_LINK_CLASS_FINI_ARRAY, offsets, &cursor);
        fini_end = cursor;
        link_test_place(link, BUSTER_LINK_CLASS_DATA_RELRO, offsets, &cursor);
        got = link_test_align(cursor, 8);
        cursor = got + 8 * (uint64_t)slot_count;
        link_test_place(link, BUSTER_LINK_CLASS_DATA, offsets, &cursor);
        link_test_place(link, BUSTER_LINK_CLASS_BSS, offsets, &cursor);
        headers = link_test_align(cursor, 8);
        cursor = headers + BUSTER_LINK_TEST_HEADERS * sizeof(Elf64_Phdr);
        image->size = link_test_align(cursor, BUSTER_LINK_TEST_PAGE_BYTES);
        const int protection = PROT_READ | PROT_WRITE;
        image->base = mmap((void*)BUSTER_LINK_TEST_IMAGE_ADDRESS, image->size, protection, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (image->base == MAP_FAILED)
        {
            image->base = mmap(0, image->size, protection, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        }
        result = image->base != MAP_FAILED && (uint64_t)image->base + image->size <= BUSTER_LINK_TEST_IMAGE_LIMIT;
        if (!result && image->base != MAP_FAILED)
        {
            munmap(image->base, image->size);
        }
        image->base = result ? image->base : 0;
        image->problem = result ? 0 : "the image can't be mapped below 2 GB";
    }

    // The bytes, and the symbols' values: an address, a TLS variable's offset from the thread pointer, the linker's, or
    // a stand-in's. The TLS block ends at the thread pointer and keeps the segment's first byte's offset from its
    // alignment, so it starts the memory size plus that offset, rounded up to the alignment, less the offset, below
    // the thread pointer (as glibc and the linkers compute it).
    const uint64_t base = (uint64_t)image->base;
    const uint64_t tls_first = tls_image & (tls_alignment - 1);
    const uint64_t tls_block = link_test_align(tls_memory + tls_first, tls_alignment) - tls_first;
    for (AtomId atom = 1; result && atom < atoms->count; atom += 1)
    {
        if (atoms->bytes[atom] != 0 && atoms->class[atom] != BUSTER_LINK_CLASS_TBSS)
        {
            memcpy(image->base + offsets[atom], atoms->bytes[atom], atoms->size[atom]);
        }
    }
    for (SymbolId symbol = 1; result && symbol < symbols->count; symbol += 1)
    {
        const AtomId atom = symbols->atom[symbol];
        const bool weak = (symbols->flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
        if (atom != 0 && link_class_is_tls(atoms->class[atom]))
        {
            values[symbol] = offsets[atom] - tls_image + symbols->value[symbol] - tls_block;
        }
        else if (atom != 0)
        {
            values[symbol] = base + offsets[atom] + symbols->value[symbol];
        }
        else if (link_test_named(link, symbol, "__init_array_start") || link_test_named(link, symbol, "__init_array_end"))
        {
            values[symbol] = base + (link_test_named(link, symbol, "__init_array_start") ? init_begin : init_end);
        }
        else if (link_test_named(link, symbol, "__fini_array_start") || link_test_named(link, symbol, "__fini_array_end"))
        {
            values[symbol] = base + (link_test_named(link, symbol, "__fini_array_start") ? fini_begin : fini_end);
        }
        else if (symbols->source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED && link_test_import(link, symbol) != 0)
        {
            values[symbol] = link_test_import(link, symbol);
        }
        else if (weak)
        {
            values[symbol] = 0;
        }
        else
        {
            result = false;
            image->problem = "an undefined symbol";
        }
        if (result && slots[symbol] != UINT32_MAX)
        {
            memcpy(image->base + got + 8 * (uint64_t)slots[symbol], &values[symbol], 8);
        }
    }

    // Every fixup, with the arithmetic its kind gives.
    for (AtomId atom = 1; result && atom < atoms->count; atom += 1)
    {
        for (uint32_t i = 0; result && i < atoms->fixup_count[atom]; i += 1)
        {
            const FixupId fixup = atoms->fixup_begin[atom] + i;
            const uint8_t kind = fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND;
            const SymbolId target = fixups->target[fixup];
            const uint64_t place = base + offsets[atom] + fixups->offset[fixup];
            const uint64_t addend = (uint64_t)link_fixup_addend(link, fixup);
            const uint64_t slot = base + got + 8 * (uint64_t)slots[target];
            const uint64_t size = symbols->size[target];
            uint64_t value = 0;
            // 0: the value must fit in int32; 1: in uint32; 2: it is 8 bytes.
            uint32_t range = 0;
            switch (kind)
            {
            case BUSTER_LINK_FIXUP_CALL:
            case BUSTER_LINK_FIXUP_PC32:
                value = values[target] + addend - place;
                break;
            case BUSTER_LINK_FIXUP_CALL_GOT:
            case BUSTER_LINK_FIXUP_LOAD_GOT:
            case BUSTER_LINK_FIXUP_GOTPCREL:
            case BUSTER_LINK_FIXUP_GOTTPOFF:
                value = slot + addend - place;
                break;
            case BUSTER_LINK_FIXUP_TPOFF32:
            case BUSTER_LINK_FIXUP_ABS32S:
                value = values[target] + addend;
                break;
            case BUSTER_LINK_FIXUP_ABS32:
                value = values[target] + addend;
                range = 1;
                break;
            case BUSTER_LINK_FIXUP_SIZE32:
                value = size + addend;
                range = 1;
                break;
            case BUSTER_LINK_FIXUP_ABS64:
                value = values[target] + addend;
                range = 2;
                break;
            case BUSTER_LINK_FIXUP_PC64:
                value = values[target] + addend - place;
                range = 2;
                break;
            case BUSTER_LINK_FIXUP_SIZE64:
                value = size + addend;
                range = 2;
                break;
            default:
                result = false;
                break;
            }
            const bool fits = range == 2 || (range == 1 ? value <= UINT32_MAX : (int64_t)value == (int32_t)value);
            result = result && fits;
            image->problem = result ? 0 : "a fixup's value doesn't fit its field";
            if (result)
            {
                memcpy((void*)place, &value, range == 2 ? 8 : 4);
            }
        }
    }

    // The program headers: PT_PHDR, from which the program finds its load bias (0 here), PT_LOAD, and PT_TLS.
    if (result)
    {
        Elf64_Phdr* phdr = (Elf64_Phdr*)(image->base + headers);
        phdr[0] = (Elf64_Phdr){
            .p_type = PT_PHDR,
            .p_flags = PF_R,
            .p_vaddr = base + headers,
            .p_memsz = BUSTER_LINK_TEST_HEADERS * sizeof(Elf64_Phdr),
            .p_align = 8,
        };
        phdr[1] = (Elf64_Phdr){
            .p_type = PT_LOAD,
            .p_flags = PF_R | PF_W,
            .p_vaddr = base,
            .p_memsz = image->size,
            .p_align = BUSTER_LINK_TEST_PAGE_BYTES,
        };
        phdr[2] = (Elf64_Phdr){
            .p_type = PT_TLS,
            .p_flags = PF_R,
            .p_vaddr = base + tls_image,
            .p_filesz = tls_file,
            .p_memsz = tls_memory,
            .p_align = tls_alignment,
        };
        image->headers = phdr;
        image->header_count = BUSTER_LINK_TEST_HEADERS;
        image->entry = values[entry];
        image->init_begin = base + init_begin;
        image->init_end = base + init_end;
        image->fini_begin = base + fini_begin;
        image->fini_end = base + fini_end;
        result = mprotect(image->base, text_begin, PROT_READ) == 0 &&
                 mprotect(image->base + text_begin, link_test_align(text_end, BUSTER_LINK_TEST_PAGE_BYTES) - text_begin, PROT_READ | PROT_EXEC) == 0;
        image->problem = result ? 0 : "mprotect failed";
    }
    return result;
}

static void link_test_unload(LinkTestImage* image)
{
    if (image->base != 0)
    {
        munmap(image->base, image->size);
    }
    *image = (LinkTestImage){ 0 };
}

// The child's side of link_test_run: the initial stack the kernel gives a process (argc, argv, envp and the auxiliary
// vector, with one argument and one variable), standard output on the pipe, and a jump to the entry point.
static void link_test_start(const LinkTestImage* image, int output) __attribute__((noreturn));
static void link_test_start(const LinkTestImage* image, int output)
{
    static const char argument[] = "program";
    static const char variable[] = "BUSTER=1";
    uint8_t* stack = mmap(0, BUSTER_LINK_TEST_STACK_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED || dup2(output, 1) < 0)
    {
        _exit(126);
    }
    uint64_t* top = (uint64_t*)(stack + BUSTER_LINK_TEST_STACK_BYTES) - 16;
    const uint64_t initial[] = {
        1,
        (uint64_t)argument,
        0,
        (uint64_t)variable,
        0,
        AT_PHDR,
        (uint64_t)image->headers,
        AT_PHENT,
        sizeof(Elf64_Phdr),
        AT_PHNUM,
        image->header_count,
        AT_NULL,
        0,
    };
    memcpy(top, initial, sizeof(initial));
    alarm(BUSTER_LINK_TEST_RUN_SECONDS);
    __asm__ volatile("mov %0, %%rsp\n\txor %%edx, %%edx\n\tjmp *%1" : : "r"(top), "r"(image->entry) : "rdx", "memory");
    __builtin_unreachable();
}

// The child's side of link_test_run for a program that calls libc: what the C runtime does around main. It sets up the
// TLS block from PT_TLS as glibc does an executable's (variant II: the block ends at the thread pointer, aligned to
// p_align, which points at itself, and its start keeps p_vaddr's remainder modulo p_align), makes the thread pointer
// FS's base, and calls the init array's entries, main and the fini array's entries in reverse, each with argc, argv
// and envp (one argument and one variable); then exits with main's status. Once FS is the program's, it calls nothing
// that uses TLS.
static void link_test_start_libc(const LinkTestImage* image, int output) __attribute__((noreturn));
static void link_test_start_libc(const LinkTestImage* image, int output)
{
    typedef int32_t LinkTestMain(int32_t argc, char** argv, char** envp);
    typedef void LinkTestHook(int32_t argc, char** argv, char** envp);
    static char argument[] = "program";
    static char variable[] = "BUSTER=1";
    static char* arguments[] = { argument, 0 };
    static char* variables[] = { variable, 0 };
    const Elf64_Phdr* tls = 0;
    for (uint32_t i = 0; i < image->header_count; i += 1)
    {
        tls = image->headers[i].p_type == PT_TLS ? &image->headers[i] : tls;
    }
    const uint64_t alignment = tls != 0 && tls->p_align > 1 ? tls->p_align : 1;
    const uint64_t first = tls != 0 ? tls->p_vaddr & (alignment - 1) : 0;
    const uint64_t block = tls != 0 ? link_test_align(tls->p_memsz + first, alignment) - first : 0;
    uint8_t* area = mmap(0, block + alignment + BUSTER_LINK_TEST_TCB_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area == MAP_FAILED || dup2(output, 1) < 0)
    {
        _exit(126);
    }
    const uint64_t pointer = link_test_align((uint64_t)area + block, alignment);
    if (tls != 0)
    {
        memcpy((void*)(pointer - block), (const void*)tls->p_vaddr, tls->p_filesz);
    }
    memcpy((void*)pointer, &pointer, sizeof(pointer));
    link_test_environ = variables;
    alarm(BUSTER_LINK_TEST_RUN_SECONDS);
    // arch_prctl(ARCH_SET_FS, pointer).
    int64_t call = 158;
    __asm__ volatile("syscall" : "+a"(call) : "D"(0x1002), "S"(pointer) : "rcx", "r11", "memory");
    for (uint64_t entry = image->init_begin; entry < image->init_end; entry += 8)
    {
        (*(LinkTestHook**)entry)(1, arguments, variables);
    }
    const int64_t status = ((LinkTestMain*)image->entry)(1, arguments, variables);
    for (uint64_t entry = image->fini_end; entry > image->fini_begin; entry -= 8)
    {
        (*(LinkTestHook**)(entry - 8))(1, arguments, variables);
    }
    // exit_group(status).
    __asm__ volatile("syscall" : : "a"(231), "D"(status) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

// Runs the loaded program in a child process: what it writes to standard output, and how it ended.
static bool link_test_run(const LinkTestImage* image, uint8_t* output, uint64_t* output_length, int* status)
{
    int pipe_ends[2];
    bool result = pipe(pipe_ends) == 0;
    *output_length = 0;
    *status = -1;
    if (result)
    {
        fflush(stdout);
        fflush(stderr);
        const pid_t child = fork();
        if (child == 0 && image->libc)
        {
            close(pipe_ends[0]);
            link_test_start_libc(image, pipe_ends[1]);
        }
        else if (child == 0)
        {
            close(pipe_ends[0]);
            link_test_start(image, pipe_ends[1]);
        }
        close(pipe_ends[1]);
        result = child > 0;
        ssize_t got = 1;
        while (result && got > 0 && *output_length < BUSTER_LINK_TEST_OUTPUT_BYTES)
        {
            got = read(pipe_ends[0], output + *output_length, BUSTER_LINK_TEST_OUTPUT_BYTES - *output_length);
            *output_length += got > 0 ? (uint64_t)got : 0;
        }
        close(pipe_ends[0]);
        result = result && waitpid(child, status, 0) == child;
    }
    return result;
}

// Whether two links hold the same records: the same counts, columns and bytes.
static bool link_test_same(const Link* a, const Link* b)
{
    bool result = a->names.count == b->names.count && a->atoms.count == b->atoms.count && a->symbols.count == b->symbols.count &&
                  a->fixups.count == b->fixups.count && a->fixups.wide_count == b->fixups.wide_count;
    for (NameId name = 1; result && name < a->names.count; name += 1)
    {
        result = a->names.length[name] == b->names.length[name] && a->names.hash[name] == b->names.hash[name] &&
                 memcmp(a->names.bytes[name], b->names.bytes[name], a->names.length[name]) == 0;
    }
    for (AtomId atom = 1; result && atom < a->atoms.count; atom += 1)
    {
        result = a->atoms.class[atom] == b->atoms.class[atom] && a->atoms.alignment_log2[atom] == b->atoms.alignment_log2[atom] &&
                 a->atoms.unit[atom] == b->atoms.unit[atom] && a->atoms.size[atom] == b->atoms.size[atom] &&
                 (a->atoms.bytes[atom] == 0) == (b->atoms.bytes[atom] == 0) && a->atoms.fixup_begin[atom] == b->atoms.fixup_begin[atom] &&
                 a->atoms.fixup_count[atom] == b->atoms.fixup_count[atom] &&
                 (a->atoms.bytes[atom] == 0 || memcmp(a->atoms.bytes[atom], b->atoms.bytes[atom], a->atoms.size[atom]) == 0);
    }
    const uint64_t symbols = a->symbols.count;
    result = result && memcmp(a->symbols.name, b->symbols.name, symbols * sizeof(NameId)) == 0 && memcmp(a->symbols.atom, b->symbols.atom, symbols * sizeof(AtomId)) == 0 &&
             memcmp(a->symbols.value, b->symbols.value, symbols * sizeof(uint64_t)) == 0 && memcmp(a->symbols.size, b->symbols.size, symbols * sizeof(uint64_t)) == 0 &&
             memcmp(a->symbols.flags, b->symbols.flags, symbols) == 0 && memcmp(a->symbols.source, b->symbols.source, symbols) == 0;
    const uint64_t fixups = a->fixups.count;
    result = result && memcmp(a->fixups.offset, b->fixups.offset, fixups * sizeof(uint32_t)) == 0 &&
             memcmp(a->fixups.target, b->fixups.target, fixups * sizeof(SymbolId)) == 0 && memcmp(a->fixups.addend, b->fixups.addend, fixups * sizeof(int32_t)) == 0 &&
             memcmp(a->fixups.kind, b->fixups.kind, fixups) == 0 &&
             memcmp(a->fixups.wide_addends, b->fixups.wide_addends, a->fixups.wide_count * sizeof(int64_t)) == 0;
    return result;
}

// Loads and runs a generated program, with its TLS segment off its alignment if `misaligned`: whether it exits with
// status 0 having written exactly `expected`.
static bool link_test_runs(LinkTestTally* tally, const LinkProgram* program, const uint8_t* expected, const char* what, bool misaligned)
{
    Scratch scratch = scratch_begin(0);
    LinkTestImage image;
    uint8_t output[BUSTER_LINK_TEST_OUTPUT_BYTES];
    uint64_t length = 0;
    int status = -1;
    const bool loaded = link_test_load(&program->link, program->entry, program->options.libc, misaligned, scratch.arena, &image);
    const bool ran = loaded && link_test_run(&image, output, &length, &status);
    const bool result = ran && WIFEXITED(status) && WEXITSTATUS(status) == 0 && length == BUSTER_LINK_PROGRAM_OUTPUT_BYTES &&
                        memcmp(output, expected, BUSTER_LINK_PROGRAM_OUTPUT_BYTES) == 0;
    if (!result && tally->failures < BUSTER_LINK_TEST_REPORTED)
    {
        printf("  %s%s: %s", what, misaligned ? ", with its TLS segment off its alignment" : "", !loaded ? image.problem : "");
        if (ran)
        {
            printf("status %d, signal %d, %llu bytes: %.*s", WIFEXITED(status) ? WEXITSTATUS(status) : -1, WIFSIGNALED(status) ? WTERMSIG(status) : 0,
                   (unsigned long long)length, (int)length, (const char*)output);
        }
        printf("\n");
    }
    tally->checked += 1;
    tally->failures += result ? 0 : 1;
    link_test_unload(&image);
    scratch_end(scratch);
    return result;
}

// Whether a run writes something other than `expected`, as one with a broken fixup must.
static bool link_test_runs_otherwise(const LinkProgram* program, const uint8_t* expected)
{
    Scratch scratch = scratch_begin(0);
    LinkTestImage image;
    uint8_t output[BUSTER_LINK_TEST_OUTPUT_BYTES];
    uint64_t length = 0;
    int status = -1;
    const bool loaded = link_test_load(&program->link, program->entry, program->options.libc, false, scratch.arena, &image);
    const bool ran = loaded && link_test_run(&image, output, &length, &status);
    const bool result = ran && !(WIFEXITED(status) && WEXITSTATUS(status) == 0 && length == BUSTER_LINK_PROGRAM_OUTPUT_BYTES &&
                                 memcmp(output, expected, BUSTER_LINK_PROGRAM_OUTPUT_BYTES) == 0);
    link_test_unload(&image);
    scratch_end(scratch);
    return result;
}

// The programs check's sets of options, from --seed and --functions: what each is called, and the directory
// --print-assembly writes its program to.
#define BUSTER_LINK_TEST_PROGRAMS 7
static const char* const link_test_program_names[BUSTER_LINK_TEST_PROGRAMS] = {
    "the defaults",
    "the smallest",
    "many units",
    "without absolute addresses",
    "--functions workers, without sizes",
    "calling libc",
    "calling libc, many units, without sizes",
};
static const char* const link_test_program_directories[BUSTER_LINK_TEST_PROGRAMS] = {
    "static-defaults",
    "static-smallest",
    "static-units",
    "static-no-absolute",
    "static-functions-no-sizes",
    "libc-defaults",
    "libc-units-no-sizes",
};

static void link_test_program_options(LinkProgramOptions* options, uint64_t seed, uint32_t functions)
{
    const LinkProgramOptions defaults = link_program_default_options();
    for (uint32_t i = 0; i < BUSTER_LINK_TEST_PROGRAMS; i += 1)
    {
        options[i] = defaults;
        options[i].seed = seed + i;
    }
    options[1] = (LinkProgramOptions){
        .seed = seed + 1,
        .units = 1,
        .functions = 1,
        .globals = 1,
        .thread_locals = 1,
        .strings = 1,
        .init_functions = 0,
        .fini_functions = 0,
        .filler = 0,
        .absolute = true,
        .sizes = true,
        .libc = false,
    };
    options[2].units = 16;
    options[2].functions = 300;
    options[2].globals = 64;
    options[2].thread_locals = 20;
    options[2].strings = 4;
    options[2].init_functions = 9;
    options[2].fini_functions = 5;
    options[2].filler = 16;
    options[3].absolute = false;
    options[4].units = 8;
    options[4].functions = functions;
    options[4].filler = 32;
    options[4].sizes = false;
    options[5].absolute = false;
    options[5].libc = true;
    options[6] = options[2];
    options[6].seed = seed + 6;
    options[6].absolute = false;
    options[6].sizes = false;
    options[6].libc = true;
}

static bool link_test_programs(uint64_t seed, uint32_t functions)
{
    LinkTestTally tally = {
        .check = "programs",
    };
    LinkProgramOptions options[BUSTER_LINK_TEST_PROGRAMS];
    link_test_program_options(options, seed, functions);
    const char* const* names = link_test_program_names;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(options); i += 1)
    {
        LinkProgram program;
        LinkProgram again;
        LinkProgram other;
        LinkProgramOptions reseeded = options[i];
        reseeded.seed += 1000;
        const bool generated = link_program_generate(&program, options[i]);
        if (!generated)
        {
            printf("  programs: %s: %s, error %d\n", names[i], program.problem, program.link.error);
        }
        link_test_expect(&tally, generated, names[i], i);
        link_test_expect(&tally, generated && link_check(&program.link) == LINK_ERROR_NONE, "link_check of a generated program", i);
        const bool repeated = link_program_generate(&again, options[i]);
        link_test_expect(&tally, generated && repeated && link_test_same(&program.link, &again.link) &&
                                     memcmp(program.output, again.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES) == 0,
                         "the same program from the same seed", i);
        const bool different = link_program_generate(&other, reseeded);
        link_test_expect(&tally, generated && different && !link_test_same(&program.link, &other.link) &&
                                     memcmp(program.output, other.output, BUSTER_LINK_PROGRAM_OUTPUT_BYTES) != 0,
                         "another program from another seed", i);
        if (generated)
        {
            const bool ran = link_test_runs(&tally, &program, program.output, names[i], false);
            const bool misaligned_ran = link_test_runs(&tally, &program, program.output, names[i], true);
            const bool other_ran = different && link_test_runs(&tally, &other, other.output, names[i], false);
            printf("  %s: %u units, %u workers, %u atoms, %u fixups (%u wide), %llu bytes of code: %s; with its TLS segment off its alignment: %s; from seed + 1000: %s\n",
                   names[i], options[i].units, options[i].functions, program.link.atoms.count - 1, program.link.fixups.count - 1, program.link.fixups.wide_count - 1,
                   (unsigned long long)program.code_bytes, ran ? "ran" : "failed", misaligned_ran ? "ran" : "failed", other_ran ? "ran" : "failed");
        }
        // Every class, and every kind the options allow, given two thread-locals, an init function and a fini function.
        if (generated && options[i].thread_locals >= 2 && options[i].init_functions >= 1 && options[i].fini_functions >= 1)
        {
            const uint32_t absolute = (1u << BUSTER_LINK_FIXUP_ABS32) | (1u << BUSTER_LINK_FIXUP_ABS32S);
            const uint32_t sizes = (1u << BUSTER_LINK_FIXUP_SIZE32) | (1u << BUSTER_LINK_FIXUP_SIZE64);
            const uint32_t expected = ((1u << BUSTER_LINK_FIXUP_KINDS) - 2) & ~(options[i].absolute ? 0 : absolute) & ~(options[i].sizes ? 0 : sizes);
            uint32_t classes = 0;
            uint32_t kinds = 0;
            for (AtomId atom = 1; atom < program.link.atoms.count; atom += 1)
            {
                classes |= 1u << program.link.atoms.class[atom];
            }
            for (FixupId fixup = 1; fixup < program.link.fixups.count; fixup += 1)
            {
                kinds |= 1u << (program.link.fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND);
            }
            link_test_expect(&tally, classes == (1u << BUSTER_LINK_CLASS_COUNT) - 2 && kinds == expected, "every class and every kind the options allow", i);
        }
        // The lea of the hexadecimal digits in buster_write_hex, one byte off, which changes every digit printed.
        if (generated && i == 0)
        {
            FixupId broken = 0;
            for (SymbolId symbol = 1; symbol < program.link.symbols.count; symbol += 1)
            {
                const AtomId atom = program.link.symbols.atom[symbol];
                for (uint32_t j = 0; link_test_named(&program.link, symbol, "buster_write_hex") && j < program.link.atoms.fixup_count[atom]; j += 1)
                {
                    const FixupId fixup = program.link.atoms.fixup_begin[atom] + j;
                    const AtomId target = program.link.symbols.atom[program.link.fixups.target[fixup]];
                    broken = program.link.atoms.class[target] == BUSTER_LINK_CLASS_STRINGS ? fixup : broken;
                }
            }
            program.link.fixups.addend[broken] += 1;
            link_test_expect(&tally, broken != 0 && link_test_runs_otherwise(&program, program.output), "a broken fixup changes the output", broken);
        }
        link_program_release(&program);
        link_program_release(&again);
        link_program_release(&other);
    }
    return link_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// Assembly: --print-assembly

// The section each class's atoms go to in an object, as GNU as's directive, and the relocation each fixup kind gets.
static const char* const link_test_sections[BUSTER_LINK_CLASS_COUNT] = {
    [BUSTER_LINK_CLASS_NONE] = 0,
    [BUSTER_LINK_CLASS_RODATA] = ".section .rodata,\"a\",@progbits",
    [BUSTER_LINK_CLASS_STRINGS] = ".section .rodata.str1.1,\"aMS\",@progbits,1",
    [BUSTER_LINK_CLASS_TEXT] = ".section .text,\"ax\",@progbits",
    [BUSTER_LINK_CLASS_TDATA] = ".section .tdata,\"awT\",@progbits",
    [BUSTER_LINK_CLASS_TBSS] = ".section .tbss,\"awT\",@nobits",
    [BUSTER_LINK_CLASS_INIT_ARRAY] = ".section .init_array,\"aw\",@init_array",
    [BUSTER_LINK_CLASS_FINI_ARRAY] = ".section .fini_array,\"aw\",@fini_array",
    [BUSTER_LINK_CLASS_DATA_RELRO] = ".section .data.rel.ro,\"aw\",@progbits",
    [BUSTER_LINK_CLASS_DATA] = ".section .data,\"aw\",@progbits",
    [BUSTER_LINK_CLASS_BSS] = ".section .bss,\"aw\",@nobits",
};
static const char* const link_test_relocations[BUSTER_LINK_FIXUP_KINDS] = {
    [BUSTER_LINK_FIXUP_NONE] = 0,
    [BUSTER_LINK_FIXUP_CALL] = "R_X86_64_PLT32",
    [BUSTER_LINK_FIXUP_CALL_GOT] = "R_X86_64_GOTPCRELX",
    [BUSTER_LINK_FIXUP_LOAD_GOT] = "R_X86_64_REX_GOTPCRELX",
    [BUSTER_LINK_FIXUP_GOTPCREL] = "R_X86_64_GOTPCREL",
    [BUSTER_LINK_FIXUP_PC32] = "R_X86_64_PC32",
    [BUSTER_LINK_FIXUP_TPOFF32] = "R_X86_64_TPOFF32",
    [BUSTER_LINK_FIXUP_GOTTPOFF] = "R_X86_64_GOTTPOFF",
    [BUSTER_LINK_FIXUP_ABS64] = "R_X86_64_64",
    [BUSTER_LINK_FIXUP_PC64] = "R_X86_64_PC64",
    [BUSTER_LINK_FIXUP_ABS32] = "R_X86_64_32",
    [BUSTER_LINK_FIXUP_ABS32S] = "R_X86_64_32S",
    [BUSTER_LINK_FIXUP_SIZE32] = "R_X86_64_SIZE32",
    [BUSTER_LINK_FIXUP_SIZE64] = "R_X86_64_SIZE64",
};

// A symbol's name in the source: its own, or .Lsym_ and its id without one (a label that stays out of the object's
// symbol table, but where GNU as keeps it for a relocation into a SHF_MERGE section).
static void link_test_print_name(FILE* file, const Link* link, SymbolId symbol)
{
    const NameId name = link->symbols.name[symbol];
    if (name != 0)
    {
        fprintf(file, "%.*s", (int)link->names.length[name], (const char*)link->names.bytes[name]);
    }
    else
    {
        fprintf(file, ".Lsym_%u", symbol);
    }
}

// A named symbol's binding, visibility, type and size, as directives: all of them for a definition, and for a symbol
// that the unit refers to without defining it, what its undefined symbol needs (a weak binding, a visibility and a TLS
// type, which linkers check against the definition's).
static void link_test_print_attributes(FILE* file, const Link* link, SymbolId symbol, bool defined)
{
    static const char* const visibilities[] = { 0, ".internal", ".hidden", ".protected" };
    const uint8_t flags = link->symbols.flags[symbol];
    const uint8_t binding = flags & BUSTER_LINK_SYMBOL_BINDING;
    const uint8_t type = flags & BUSTER_LINK_SYMBOL_TYPE;
    const char* visibility = visibilities[flags >> BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT];
    const char* type_name = type == BUSTER_LINK_SYMBOL_FUNCTION ? "@function" : type == BUSTER_LINK_SYMBOL_OBJECT ? "@object" : type == BUSTER_LINK_SYMBOL_TLS ? "@tls_object" : 0;
    const char* binding_directive = binding == BUSTER_LINK_SYMBOL_WEAK ? ".weak" : binding == BUSTER_LINK_SYMBOL_GLOBAL && defined ? ".globl" : 0;
    if (binding_directive != 0)
    {
        fprintf(file, "    %s ", binding_directive);
        link_test_print_name(file, link, symbol);
        fprintf(file, "\n");
    }
    if (visibility != 0)
    {
        fprintf(file, "    %s ", visibility);
        link_test_print_name(file, link, symbol);
        fprintf(file, "\n");
    }
    if (type_name != 0 && (defined || type == BUSTER_LINK_SYMBOL_TLS))
    {
        fprintf(file, "    .type ");
        link_test_print_name(file, link, symbol);
        fprintf(file, ", %s\n", type_name);
    }
    if (defined)
    {
        fprintf(file, "    .size ");
        link_test_print_name(file, link, symbol);
        fprintf(file, ", %llu\n", (unsigned long long)link->symbols.size[symbol]);
    }
}

// Bytes `begin` to `end` of an atom: .byte lines, or .zero for a zero-fill atom.
static void link_test_print_bytes(FILE* file, const uint8_t* bytes, uint64_t begin, uint64_t end)
{
    if (bytes == 0 && end > begin)
    {
        fprintf(file, "    .zero %llu\n", (unsigned long long)(end - begin));
    }
    for (uint64_t line = begin; bytes != 0 && line < end; line += 16)
    {
        const uint64_t line_end = end - line > 16 ? line + 16 : end;
        fprintf(file, "    .byte 0x%02x", bytes[line]);
        for (uint64_t i = line + 1; i < line_end; i += 1)
        {
            fprintf(file, ",0x%02x", bytes[i]);
        }
        fprintf(file, "\n");
    }
}

// Writes one unit as GNU as source: each of its atoms in its class's section, at its alignment, as its bytes with a
// label at each symbol defined there (`defined` holds the symbols of atom a, by value, from defined_begin[a] to
// defined_begin[a + 1]) and a .reloc for each of its fixups; then the attributes of the named symbols it defines, and
// of those it refers to without defining them (`declared` holds the unit + 1 that last declared each).
static bool link_test_print_unit(const char* path, const Link* link, uint32_t unit, const SymbolId* defined, const uint32_t* defined_begin, uint32_t* declared)
{
    const LinkAtoms* atoms = &link->atoms;
    const LinkSymbols* symbols = &link->symbols;
    const LinkFixups* fixups = &link->fixups;
    FILE* file = fopen(path, "w");
    for (AtomId atom = 1; file != 0 && atom < atoms->count; atom += 1)
    {
        if (atoms->unit[atom] == unit)
        {
            fprintf(file, "    %s\n    .p2align %u\n.Latom_%u:\n", link_test_sections[atoms->class[atom]], atoms->alignment_log2[atom], atom);
            uint64_t cursor = 0;
            for (uint32_t i = defined_begin[atom]; i < defined_begin[atom + 1]; i += 1)
            {
                link_test_print_bytes(file, atoms->bytes[atom], cursor, symbols->value[defined[i]]);
                cursor = symbols->value[defined[i]];
                link_test_print_name(file, link, defined[i]);
                fprintf(file, ":\n");
            }
            link_test_print_bytes(file, atoms->bytes[atom], cursor, atoms->size[atom]);
            for (uint32_t i = 0; i < atoms->fixup_count[atom]; i += 1)
            {
                const FixupId fixup = atoms->fixup_begin[atom] + i;
                fprintf(file, "    .reloc .Latom_%u+%u, %s, ", atom, fixups->offset[fixup], link_test_relocations[fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND]);
                link_test_print_name(file, link, fixups->target[fixup]);
                fprintf(file, "%+lld\n", (long long)link_fixup_addend(link, fixup));
            }
        }
    }
    for (SymbolId symbol = 1; file != 0 && symbol < symbols->count; symbol += 1)
    {
        const AtomId atom = symbols->atom[symbol];
        if (atom != 0 && atoms->unit[atom] == unit && symbols->name[symbol] != 0)
        {
            link_test_print_attributes(file, link, symbol, true);
        }
    }
    for (AtomId atom = 1; file != 0 && atom < atoms->count; atom += 1)
    {
        for (uint32_t i = 0; atoms->unit[atom] == unit && i < atoms->fixup_count[atom]; i += 1)
        {
            const SymbolId target = fixups->target[atoms->fixup_begin[atom] + i];
            const AtomId target_atom = symbols->atom[target];
            if ((target_atom == 0 || atoms->unit[target_atom] != unit) && declared[target] != unit + 1)
            {
                link_test_print_attributes(file, link, target, false);
                declared[target] = unit + 1;
            }
        }
    }
    bool result = file != 0;
    if (result)
    {
        fprintf(file, "    .section .note.GNU-stack,\"\",@progbits\n");
        result = ferror(file) == 0;
        result = fclose(file) == 0 && result;
    }
    if (!result)
    {
        fprintf(stderr, "can't write %s\n", path);
    }
    return result;
}

// Writes a generated program to `directory`, which must exist: unit_N.s for each unit, and what it must print to
// `expected`.
static bool link_test_print_program(const LinkProgram* program, const char* directory)
{
    const Link* link = &program->link;
    Scratch scratch = scratch_begin(0);
    uint32_t* defined_begin = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, link->atoms.count + 1);
    uint32_t* next = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, link->atoms.count);
    SymbolId* defined = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, SymbolId, link->symbols.count);
    uint32_t* declared = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, link->symbols.count);
    bool result = defined_begin != 0 && next != 0 && defined != 0 && declared != 0;
    if (result)
    {
        // The symbols by atom, counted, placed and filled, and then by value within an atom.
        memset(defined_begin, 0, (link->atoms.count + 1) * sizeof(*defined_begin));
        memset(declared, 0, link->symbols.count * sizeof(*declared));
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            const AtomId atom = link->symbols.atom[symbol];
            defined_begin[atom + 1] += atom != 0 ? 1 : 0;
        }
        for (AtomId atom = 1; atom <= link->atoms.count; atom += 1)
        {
            defined_begin[atom] += defined_begin[atom - 1];
            next[atom - 1] = defined_begin[atom - 1];
        }
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            const AtomId atom = link->symbols.atom[symbol];
            if (atom != 0)
            {
                defined[next[atom]] = symbol;
                next[atom] += 1;
            }
        }
        for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
        {
            for (uint32_t i = defined_begin[atom] + 1; i < defined_begin[atom + 1]; i += 1)
            {
                const SymbolId symbol = defined[i];
                uint32_t j = i;
                while (j > defined_begin[atom] && link->symbols.value[defined[j - 1]] > link->symbols.value[symbol])
                {
                    defined[j] = defined[j - 1];
                    j -= 1;
                }
                defined[j] = symbol;
            }
        }
    }
    char path[4096];
    for (uint32_t unit = 0; result && unit < program->options.units; unit += 1)
    {
        snprintf(path, sizeof(path), "%s/unit_%u.s", directory, unit);
        result = link_test_print_unit(path, link, unit, defined, defined_begin, declared);
    }
    if (result)
    {
        snprintf(path, sizeof(path), "%s/expected", directory);
        FILE* file = fopen(path, "w");
        result = file != 0 && fwrite(program->output, 1, BUSTER_LINK_PROGRAM_OUTPUT_BYTES, file) == BUSTER_LINK_PROGRAM_OUTPUT_BYTES;
        result = file != 0 && fclose(file) == 0 && result;
        if (!result)
        {
            fprintf(stderr, "can't write %s\n", path);
        }
    }
    scratch_end(scratch);
    return result;
}

// --print-assembly: the programs check's programs, each in a directory of `directory` named in
// link_test_program_directories.
static bool link_test_print_programs(const char* directory, uint64_t seed, uint32_t functions)
{
    LinkProgramOptions options[BUSTER_LINK_TEST_PROGRAMS];
    link_test_program_options(options, seed, functions);
    bool result = mkdir(directory, 0777) == 0 || errno == EEXIST;
    if (!result)
    {
        fprintf(stderr, "can't make %s\n", directory);
    }
    for (uint32_t i = 0; result && i < BUSTER_LINK_TEST_PROGRAMS; i += 1)
    {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", directory, link_test_program_directories[i]);
        LinkProgram program;
        result = link_program_generate(&program, options[i]);
        if (!result)
        {
            fprintf(stderr, "%s: %s\n", link_test_program_names[i], program.problem);
        }
        else if (mkdir(path, 0777) != 0 && errno != EEXIST)
        {
            fprintf(stderr, "can't make %s\n", path);
            result = false;
        }
        result = result && link_test_print_program(&program, path);
        if (result)
        {
            printf("%s: %s, %u units\n", path, link_test_program_names[i], options[i].units);
        }
        link_program_release(&program);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

#define BUSTER_LINK_TEST_DEFAULT_FUNCTIONS 2000

static bool link_test_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = end != text && *end == 0;
    *value = result ? parsed : *value;
    return result;
}

static int run_link_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t functions = BUSTER_LINK_TEST_DEFAULT_FUNCTIONS;
    const char* assembly = 0;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && link_test_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--functions") == 0 && i + 1 < argc)
        {
            valid = valid && link_test_parse_u64(argv[i + 1], &functions) && functions >= 1 && functions <= BUSTER_LINK_PROGRAM_MAX_COUNT;
            i += 1;
        }
        else if (strcmp(argv[i], "--print-assembly") == 0 && i + 1 < argc)
        {
            assembly = argv[i + 1];
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-link [--seed N] [--functions N] [--print-assembly DIRECTORY]\n");
    }
    else if (!x86_encoder_initialized && !x86_encoder_initialize())
    {
        fprintf(stderr, "the encoder's templates can't be built\n");
    }
    else if (assembly != 0)
    {
        result = link_test_print_programs(assembly, seed, (uint32_t)functions) ? 0 : 1;
    }
    else
    {
        bool passed = link_test_round_trips(seed);
        passed = link_test_capacity() && passed;
        passed = link_test_rules() && passed;
        passed = link_test_kinds(seed) && passed;
        passed = link_test_wide() && passed;
        passed = link_test_programs(seed, (uint32_t)functions) && passed;
        result = passed ? 0 : 1;
    }
    return result;
}
