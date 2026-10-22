// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Binding's benchmark: what joining the C runtime to a program's link and binding its names costs on each path
// (docs/object-writer-and-linker.md, "Step 5's phases, data-parallel" and "Static links, as built").
//
// Usage: build/compiler benchmark-bind [--functions LIST] [--seed N]
//
// Run it under the 9700X's lock pinned to one CPU (taskset -c 2 ...). For each count of workers in --functions (1, 64,
// 2000 and 102400 by default), a generated program that calls libc, of 200 workers per unit as synth's translation
// units have (one unit below 200), with 8 globals and 2 thread-locals per unit, is bound by each path, against the
// dynamic PIE's runtime, a static executable's and a static PIE's. Each figure is the median of
// BUSTER_BIND_BENCHMARK_RUNS runs after a warm-up, each path's on a program of its own, each run on the same link put
// back as it was before (its counts, arenas and the symbol columns binding writes, and binding's own state):
//   - match: the program's global symbols looked up in the runtime's names (bind_match), per global symbol;
//   - begin: bind_begin, the match and the crt objects before the program joined;
//   - closure (the static runtimes', a static executable's and a static PIE's): the members the link needs found
//     (bind_closure_vector or bind_closure_exact), per member found;
//   - end: bind_end, after the program's atoms: the members joined (for a static runtime, the closure among it), every
//     name bound, the crt objects after the program joined, and the exports found.
// Cycles and instructions come from the hardware counters (zero when unavailable). The paths must make the same links.
// The exit status is 1 if the arguments are invalid, a program can't be made or bound, or the paths disagree.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// OS-dependent includes
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#ifndef BUSTER_ARRAY_LENGTH
#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif

// The encoder's other entry points serve its own tests and benchmark: the generated programs use x86_encode alone.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#pragma clang diagnostic pop
#include "buster/compiler/lane.c"
#include "buster/compiler/link.c"
#include "buster/compiler/link_program.c"
// The import's relocation types are the object writer's (object_relocation_types).
#include "buster/compiler/object_writer.c"
#include "buster/compiler/import.c"
#include "buster/compiler/runtime.c"
#include "buster/compiler/bind.c"

#define BUSTER_BIND_BENCHMARK_RUNS 21
#define BUSTER_BIND_BENCHMARK_MAX_PROGRAMS 16
#define BUSTER_BIND_BENCHMARK_UNIT_FUNCTIONS 200
#define BUSTER_BIND_BENCHMARK_ARENA_BYTES (1ull << 34)

static const uint32_t bind_benchmark_default_functions[] = {
    1,
    64,
    2000,
    102400,
};

static const char* const bind_benchmark_path_names[] = {
    [BIND_PATH_VECTOR] = "vector",
    [BIND_PATH_EXACT] = "exact",
};

// Each kind's runtime, as the rows name it.
static const char* const bind_benchmark_kind_names[RUNTIME_KINDS] = {
    [RUNTIME_KIND_DYNAMIC] = "the dynamic PIE's",
    [RUNTIME_KIND_STATIC] = "the static",
    [RUNTIME_KIND_STATIC_PIE] = "the static PIE's",
};

// ---------------------------------------------------------------------------------------------------------------------
// The clock and the hardware counters

static uint64_t bind_benchmark_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// A counter of `config` (PERF_COUNT_HW_*) for the calling thread in user space, or -1 where perf_event_open refuses.
static int bind_benchmark_counter(uint64_t config)
{
    struct perf_event_attr attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.size = sizeof(attributes);
    attributes.type = PERF_TYPE_HARDWARE;
    attributes.config = config;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &attributes, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
}

static uint64_t bind_benchmark_count(int counter)
{
    uint64_t result = 0;
    if (counter < 0 || read(counter, &result, sizeof(result)) != (ssize_t)sizeof(result))
    {
        result = 0;
    }
    return result;
}

static int bind_benchmark_compare(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

static uint64_t bind_benchmark_median(uint64_t* values, uint64_t count)
{
    qsort(values, count, sizeof(uint64_t), bind_benchmark_compare);
    return values[count / 2];
}

// A figure's runs: time, cycles and instructions.
typedef struct BindBenchmarkFigure BindBenchmarkFigure;
struct BindBenchmarkFigure
{
    uint64_t nanoseconds[BUSTER_BIND_BENCHMARK_RUNS];
    uint64_t cycles[BUSTER_BIND_BENCHMARK_RUNS];
    uint64_t instructions[BUSTER_BIND_BENCHMARK_RUNS];
};

typedef struct BindBenchmarkClock BindBenchmarkClock;
struct BindBenchmarkClock
{
    int cycles_counter;
    int instructions_counter;
    uint64_t start;
    uint64_t cycles;
    uint64_t instructions;
};

static void bind_benchmark_start(BindBenchmarkClock* clock)
{
    clock->cycles = bind_benchmark_count(clock->cycles_counter);
    clock->instructions = bind_benchmark_count(clock->instructions_counter);
    clock->start = bind_benchmark_now();
}

// Records the time since bind_benchmark_start in run `run` of the figure, unless it is the warm-up (run 0).
static void bind_benchmark_stop(BindBenchmarkClock* clock, BindBenchmarkFigure* figure, uint32_t run)
{
    const uint64_t end = bind_benchmark_now();
    if (run != 0)
    {
        figure->cycles[run - 1] = bind_benchmark_count(clock->cycles_counter) - clock->cycles;
        figure->instructions[run - 1] = bind_benchmark_count(clock->instructions_counter) - clock->instructions;
        figure->nanoseconds[run - 1] = end - clock->start;
    }
}

// Prints a figure's medians, per item when `items` isn't 0, and else for the whole run.
static void bind_benchmark_print(const char* what, BindBenchmarkFigure* figure, uint64_t items, const char* item)
{
    const double time = (double)bind_benchmark_median(figure->nanoseconds, BUSTER_BIND_BENCHMARK_RUNS);
    const double cycle_count = (double)bind_benchmark_median(figure->cycles, BUSTER_BIND_BENCHMARK_RUNS);
    const double instruction_count = (double)bind_benchmark_median(figure->instructions, BUSTER_BIND_BENCHMARK_RUNS);
    const double ipc = cycle_count != 0 ? instruction_count / cycle_count : 0.0;
    if (items != 0)
    {
        printf("  %-18s %10.2f µs  %7.2f ns/%s  %8.2f cycles/%s  %8.2f instructions/%s  IPC %4.2f\n", what, time / 1000.0, time / (double)items, item, cycle_count / (double)items,
               item, instruction_count / (double)items, item, ipc);
    }
    else
    {
        printf("  %-18s %10.2f µs  %10.0f cycles  %10.0f instructions  IPC %4.2f\n", what, time / 1000.0, cycle_count, instruction_count, ipc);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Putting a link back as it was

// What binding writes in a link, as it was: the tables' counts, the arenas' lengths, and the symbol columns binding
// may write, copied.
typedef struct BindBenchmarkMark BindBenchmarkMark;
struct BindBenchmarkMark
{
    uint32_t names;
    uint32_t atoms;
    uint32_t symbols;
    uint32_t fixups;
    uint32_t wide_addends;
    uint32_t priorities;
    uint64_t name_bytes;
    uint64_t bytes;
    AtomId* atom;
    uint64_t* value;
    uint64_t* size;
    uint8_t* flags;
    uint8_t* source;
    uint16_t* version;
};

static bool bind_benchmark_mark(BindBenchmarkMark* mark, const Link* link, const Arena* bytes, Arena* arena)
{
    const LinkSymbols* symbols = &link->symbols;
    const uint32_t count = symbols->count;
    *mark = (BindBenchmarkMark){
        .names = link->names.count,
        .atoms = link->atoms.count,
        .symbols = count,
        .fixups = link->fixups.count,
        .wide_addends = link->fixups.wide_count,
        .priorities = link->priorities.count,
        .name_bytes = link->name_arena->length,
        .bytes = bytes->length,
    };
    mark->atom = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, count);
    mark->value = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count);
    mark->size = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, count);
    mark->flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count);
    mark->source = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, count);
    mark->version = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, count);
    const bool result = mark->atom != 0 && mark->value != 0 && mark->size != 0 && mark->flags != 0 && mark->source != 0 && mark->version != 0;
    if (result)
    {
        memcpy(mark->atom, symbols->atom, count * sizeof(AtomId));
        memcpy(mark->value, symbols->value, count * sizeof(uint64_t));
        memcpy(mark->size, symbols->size, count * sizeof(uint64_t));
        memcpy(mark->flags, symbols->flags, count);
        memcpy(mark->source, symbols->source, count);
        memcpy(mark->version, symbols->version, count * sizeof(uint16_t));
    }
    return result;
}

static void bind_benchmark_restore(const BindBenchmarkMark* mark, Link* link, Arena* bytes)
{
    LinkSymbols* symbols = &link->symbols;
    const uint32_t count = mark->symbols;
    link->names.count = mark->names;
    link->atoms.count = mark->atoms;
    symbols->count = count;
    link->fixups.count = mark->fixups;
    link->fixups.wide_count = mark->wide_addends;
    link->priorities.count = mark->priorities;
    arena_pop_to(link->name_arena, mark->name_bytes);
    arena_pop_to(bytes, mark->bytes);
    memcpy(symbols->atom, mark->atom, count * sizeof(AtomId));
    memcpy(symbols->value, mark->value, count * sizeof(uint64_t));
    memcpy(symbols->size, mark->size, count * sizeof(uint64_t));
    memcpy(symbols->flags, mark->flags, count);
    memcpy(symbols->source, mark->source, count);
    memcpy(symbols->version, mark->version, count * sizeof(uint16_t));
}

// Binding's own state as it was, for bind_end's runs: the struct and the arrays it writes.
typedef struct BindBenchmarkState BindBenchmarkState;
struct BindBenchmarkState
{
    Bind bind;
    SymbolId* name_symbols;
    uint32_t* named;
    SymbolId* symbols;
    uint64_t* members;
    uint64_t* signatures;
};

static bool bind_benchmark_save(BindBenchmarkState* state, const Bind* bind, Arena* arena)
{
    const Runtime* runtime = bind->runtime;
    const uint32_t words = (runtime->member_count + 63) / 64;
    state->bind = *bind;
    state->name_symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, runtime->global_names.count);
    state->named = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, runtime->global_names.count);
    state->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, runtime->link.symbols.count);
    const uint32_t signature_words = (runtime->group_signatures + 63) / 64;
    state->members = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, 2 * words + 1);
    state->signatures = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, signature_words + 1);
    const bool result = state->name_symbols != 0 && state->named != 0 && state->symbols != 0 && state->members != 0 && state->signatures != 0;
    if (result)
    {
        memcpy(state->name_symbols, bind->name_symbols, runtime->global_names.count * sizeof(SymbolId));
        memcpy(state->named, bind->named, runtime->global_names.count * sizeof(uint32_t));
        memcpy(state->symbols, bind->symbols, runtime->link.symbols.count * sizeof(SymbolId));
        memcpy(state->members, bind->members_needed, words * sizeof(uint64_t));
        memcpy(state->members + words, bind->members_joined, words * sizeof(uint64_t));
        memcpy(state->signatures, bind->signatures, signature_words * sizeof(uint64_t));
    }
    return result;
}

static void bind_benchmark_load(const BindBenchmarkState* state, Bind* bind)
{
    const Runtime* runtime = state->bind.runtime;
    const uint32_t words = (runtime->member_count + 63) / 64;
    *bind = state->bind;
    memcpy(bind->name_symbols, state->name_symbols, runtime->global_names.count * sizeof(SymbolId));
    memcpy(bind->named, state->named, runtime->global_names.count * sizeof(uint32_t));
    memcpy(bind->symbols, state->symbols, runtime->link.symbols.count * sizeof(SymbolId));
    memcpy(bind->members_needed, state->members, words * sizeof(uint64_t));
    memcpy(bind->members_joined, state->members + words, words * sizeof(uint64_t));
    memcpy(bind->signatures, state->signatures, (runtime->group_signatures + 63) / 64 * sizeof(uint64_t));
}

// ---------------------------------------------------------------------------------------------------------------------
// The programs

// Whether two links hold the same records: names by their bytes, atoms by theirs, and wide addends by value.
static bool bind_benchmark_same(const Link* a, const Link* b)
{
    bool result = a->names.count == b->names.count && a->atoms.count == b->atoms.count && a->symbols.count == b->symbols.count && a->fixups.count == b->fixups.count;
    for (NameId name = 1; result && name < a->names.count; name += 1)
    {
        result = a->names.length[name] == b->names.length[name] && memcmp(a->names.bytes[name], b->names.bytes[name], a->names.length[name]) == 0;
    }
    for (AtomId atom = 1; result && atom < a->atoms.count; atom += 1)
    {
        result = a->atoms.class[atom] == b->atoms.class[atom] && a->atoms.unit[atom] == b->atoms.unit[atom] && a->atoms.size[atom] == b->atoms.size[atom] &&
                 a->atoms.fixup_begin[atom] == b->atoms.fixup_begin[atom] && a->atoms.fixup_count[atom] == b->atoms.fixup_count[atom] &&
                 (a->atoms.bytes[atom] == 0 || memcmp(a->atoms.bytes[atom], b->atoms.bytes[atom], a->atoms.size[atom]) == 0);
    }
    for (SymbolId symbol = 1; result && symbol < a->symbols.count; symbol += 1)
    {
        result = a->symbols.name[symbol] == b->symbols.name[symbol] && a->symbols.atom[symbol] == b->symbols.atom[symbol] &&
                 a->symbols.value[symbol] == b->symbols.value[symbol] && a->symbols.flags[symbol] == b->symbols.flags[symbol] &&
                 a->symbols.source[symbol] == b->symbols.source[symbol] && a->symbols.version[symbol] == b->symbols.version[symbol];
    }
    for (FixupId fixup = 1; result && fixup < a->fixups.count; fixup += 1)
    {
        result = a->fixups.offset[fixup] == b->fixups.offset[fixup] && a->fixups.target[fixup] == b->fixups.target[fixup] &&
                 a->fixups.kind[fixup] == b->fixups.kind[fixup] && link_fixup_addend(a, fixup) == link_fixup_addend(b, fixup);
    }
    return result;
}

// The figures of a program bound by a path, in order.
typedef enum BindBenchmarkPhase
{
    BIND_BENCHMARK_MATCH,
    BIND_BENCHMARK_BEGIN,
    BIND_BENCHMARK_CLOSURE,
    BIND_BENCHMARK_END,
    BIND_BENCHMARK_PHASES,
} BindBenchmarkPhase;

static const char* const bind_benchmark_phase_names[BIND_BENCHMARK_PHASES] = {
    [BIND_BENCHMARK_MATCH] = "match",
    [BIND_BENCHMARK_BEGIN] = "bind_begin",
    [BIND_BENCHMARK_CLOSURE] = "closure",
    [BIND_BENCHMARK_END] = "bind_end",
};

// A program of `functions` workers bound by `path`: the match, bind_begin, the closure (for a static runtime) and
// bind_end measured in the runs, which figures[BIND_BENCHMARK_PHASES * path + phase] holds, and the members found in
// *members. Its link stays in `program` for the paths' comparison.
static bool bind_benchmark_program(LinkProgram* program, const Runtime* runtime, uint32_t functions, uint64_t seed, BindPath path, BindBenchmarkClock* clock,
                                   BindBenchmarkFigure* figures, uint32_t* globals, uint32_t* members, Arena* arena)
{
    BindBenchmarkFigure* figure = figures + BIND_BENCHMARK_PHASES * path;
    LinkProgramOptions options = link_program_default_options();
    const uint32_t units = functions / BUSTER_BIND_BENCHMARK_UNIT_FUNCTIONS > 0 ? functions / BUSTER_BIND_BENCHMARK_UNIT_FUNCTIONS : 1;
    options.seed = seed;
    options.units = units;
    options.functions = functions;
    options.globals = 8 * units;
    options.thread_locals = 2 * units;
    options.init_functions = units;
    options.fini_functions = units;
    options.absolute = false;
    options.libc = true;
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const LinkCapacity extra = bind_capacity(runtime, &name_bytes, &atom_bytes);
    bool result = link_program_generate_begin(program, options, extra, name_bytes, atom_bytes);
    Link* link = &program->link;
    BindBenchmarkMark begun;
    result = result && bind_benchmark_mark(&begun, link, &program->bytes, arena);
    uint32_t global_count = 0;
    for (SymbolId symbol = 1; result && symbol < link->symbols.count; symbol += 1)
    {
        global_count += link->symbols.name[symbol] != 0 && (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL ? 1 : 0;
    }
    *globals = global_count;
    // The match and bind_begin, each run from the program's symbols as they were.
    const uint64_t bind_arena = arena->length;
    Bind bind;
    for (uint32_t run = 0; result && run < BUSTER_BIND_BENCHMARK_RUNS + 1; run += 1)
    {
        // A binding's arrays for the match alone, from a bind_begin taken back.
        bind_benchmark_restore(&begun, link, &program->bytes);
        result = bind_begin(&bind, link, runtime, path, &program->bytes, units, arena);
        bind_benchmark_restore(&begun, link, &program->bytes);
        memset(bind.name_symbols, 0, runtime->global_names.count * sizeof(SymbolId));
        bind.named_count = 0;
        bind_benchmark_start(clock);
        bind_match(&bind, link, 1, link->symbols.count);
        bind_benchmark_stop(clock, &figure[BIND_BENCHMARK_MATCH], run);
        bind_benchmark_restore(&begun, link, &program->bytes);
        arena_pop_to(arena, bind_arena);
        bind_benchmark_start(clock);
        result = result && bind_begin(&bind, link, runtime, path, &program->bytes, units, arena);
        bind_benchmark_stop(clock, &figure[BIND_BENCHMARK_BEGIN], run);
        arena_pop_to(arena, bind_arena);
    }
    // bind_end, each run from the program's atoms as they were after bind_begin.
    bind_benchmark_restore(&begun, link, &program->bytes);
    result = result && bind_begin(&bind, link, runtime, path, &program->bytes, units, arena) && link_program_generate_atoms(program);
    BindBenchmarkMark atoms_added;
    BindBenchmarkState state;
    result = result && bind_benchmark_mark(&atoms_added, link, &program->bytes, arena) && bind_benchmark_save(&state, &bind, arena);
    // The closure alone, from binding's state after bind_begin: the program defines its names before it, so the names it
    // has are all matched.
    const uint32_t words = (runtime->member_count + 63) / 64;
    for (uint32_t run = 0; result && runtime_kind_is_static(runtime->kind) && run < BUSTER_BIND_BENCHMARK_RUNS + 1; run += 1)
    {
        bind_benchmark_restore(&atoms_added, link, &program->bytes);
        bind_benchmark_load(&state, &bind);
        bind_benchmark_start(clock);
        if (path == BIND_PATH_VECTOR)
        {
            bind_closure_vector(&bind, link);
        }
        else
        {
            bind_closure_exact(&bind, link);
        }
        bind_benchmark_stop(clock, &figure[BIND_BENCHMARK_CLOSURE], run);
    }
    for (uint32_t run = 0; result && run < BUSTER_BIND_BENCHMARK_RUNS + 1; run += 1)
    {
        bind_benchmark_restore(&atoms_added, link, &program->bytes);
        bind_benchmark_load(&state, &bind);
        bind_benchmark_start(clock);
        result = bind_end(&bind, link);
        bind_benchmark_stop(clock, &figure[BIND_BENCHMARK_END], run);
    }
    uint32_t joined = 0;
    for (uint32_t w = 0; result && w < words; w += 1)
    {
        joined += (uint32_t)__builtin_popcountll(bind.members_joined[w]);
    }
    *members = joined;
    result = result && link_program_generate_end(program);
    return result;
}

static bool bind_benchmark_parse(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool result = end != text && *end == 0;
    *value = result ? (uint64_t)parsed : *value;
    return result;
}

// A list of counts separated by commas.
static bool bind_benchmark_parse_list(const char* text, uint32_t* values, uint32_t* count)
{
    bool result = true;
    uint32_t n = 0;
    const char* cursor = text;
    while (result && *cursor != 0)
    {
        char* end = 0;
        const unsigned long long parsed = strtoull(cursor, &end, 10);
        result = end != cursor && (*end == ',' || *end == 0) && parsed >= 1 && parsed <= BUSTER_LINK_PROGRAM_MAX_COUNT && n < BUSTER_BIND_BENCHMARK_MAX_PROGRAMS;
        if (result)
        {
            values[n] = (uint32_t)parsed;
            n += 1;
            cursor = *end == ',' ? end + 1 : end;
        }
    }
    *count = n;
    return result && n != 0;
}

int run_bind_benchmark(int argc, char** argv);

int run_bind_benchmark(int argc, char** argv)
{
    uint32_t functions[BUSTER_BIND_BENCHMARK_MAX_PROGRAMS];
    uint32_t program_count = BUSTER_ARRAY_LENGTH(bind_benchmark_default_functions);
    memcpy(functions, bind_benchmark_default_functions, sizeof(bind_benchmark_default_functions));
    uint64_t seed = 1;
    bool valid = true;
    for (int i = 1; valid && i < argc; i += 1)
    {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--functions") == 0 && has_value)
        {
            valid = bind_benchmark_parse_list(argv[i + 1], functions, &program_count);
            i += 1;
        }
        else if (strcmp(argv[i], "--seed") == 0 && has_value)
        {
            valid = bind_benchmark_parse(argv[i + 1], &seed);
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    // Each kind's runtime, imported: runtime_import, runtime_import_static and runtime_import_static_pie.
    Runtime runtimes[RUNTIME_KINDS];
    uint32_t started = 0;
    bool result = valid;
    for (uint32_t kind = 0; result && kind < RUNTIME_KINDS; kind += 1)
    {
        result = runtime_import_kind(&runtimes[kind], (RuntimeKind)kind) == RUNTIME_ERROR_NONE;
        started += 1;
    }
    result = result && x86_encoder_initialize();
    if (!valid)
    {
        printf("usage: benchmark-bind [--functions LIST] [--seed N]\n");
    }
    else if (!result)
    {
        printf("a runtime didn't import, or the encoder's templates can't be built\n");
    }
    BindBenchmarkClock clock = {
        .cycles_counter = bind_benchmark_counter(PERF_COUNT_HW_CPU_CYCLES),
        .instructions_counter = bind_benchmark_counter(PERF_COUNT_HW_INSTRUCTIONS),
    };
    Arena arena = arena_reserve(BUSTER_BIND_BENCHMARK_ARENA_BYTES);
    result = result && arena.base != 0;
    for (uint32_t p = 0; result && p < program_count; p += 1)
    {
        for (uint32_t kind = 0; result && kind < RUNTIME_KINDS; kind += 1)
        {
            static BindBenchmarkFigure figures[2 * BIND_BENCHMARK_PHASES];
            const Runtime* runtime = &runtimes[kind];
            LinkProgram programs[2];
            uint32_t globals = 0;
            uint32_t members = 0;
            const uint64_t length = arena.length;
            const bool vector = bind_benchmark_program(&programs[BIND_PATH_VECTOR], runtime, functions[p], seed, BIND_PATH_VECTOR, &clock, figures, &globals, &members, &arena);
            const bool exact = bind_benchmark_program(&programs[BIND_PATH_EXACT], runtime, functions[p], seed, BIND_PATH_EXACT, &clock, figures, &globals, &members, &arena);
            result = vector && exact;
            const bool same = result && bind_benchmark_same(&programs[BIND_PATH_VECTOR].link, &programs[BIND_PATH_EXACT].link);
            if (result)
            {
                const Link* link = &programs[BIND_PATH_VECTOR].link;
                printf("%u workers, %s runtime (%u members joined): %u units, %u symbols (%u global), %u atoms, %u fixups%s\n", functions[p], bind_benchmark_kind_names[kind],
                       members, programs[BIND_PATH_VECTOR].options.units, link->symbols.count - 1, globals, link->atoms.count - 1, link->fixups.count - 1,
                       same ? "" : "; the paths' links differ");
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    for (uint32_t phase = 0; phase < BIND_BENCHMARK_PHASES; phase += 1)
                    {
                        char what[64];
                        snprintf(what, sizeof(what), "%s %s", bind_benchmark_path_names[path], bind_benchmark_phase_names[phase]);
                        const uint64_t items = phase == BIND_BENCHMARK_MATCH ? globals : phase == BIND_BENCHMARK_CLOSURE ? members : 0;
                        if (phase != BIND_BENCHMARK_CLOSURE || runtime_kind_is_static((RuntimeKind)kind))
                        {
                            bind_benchmark_print(what, &figures[BIND_BENCHMARK_PHASES * path + phase], items, phase == BIND_BENCHMARK_MATCH ? "global" : "member");
                        }
                    }
                }
            }
            result = result && same;
            link_program_release(&programs[BIND_PATH_VECTOR]);
            link_program_release(&programs[BIND_PATH_EXACT]);
            arena_pop_to(&arena, length);
        }
    }
    arena_release(&arena);
    // A failed import's arenas too.
    for (uint32_t kind = 0; kind < started; kind += 1)
    {
        runtime_release(&runtimes[kind]);
    }
    return result ? 0 : 1;
}
