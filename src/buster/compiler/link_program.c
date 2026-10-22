// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The test programs: whole programs in the link representation (link.c), for the tests and benchmarks of the object
// writer and of the direct path (steps 3 to 5 of the plan in docs/object-writer-and-linker.md).
// link_program_generate makes one from LinkProgramOptions, deterministic from the seed and sized by the options, so
// that a benchmark can scale it up. Its code is encoded with x86_encode and its fixups come from link_add_code_fixups,
// as a code generator's would.
//
// A program is one of two kinds, as LinkProgramOptions.libc says:
//   - Without libc, a static executable whose _start makes system calls, as with -nostdlib: the first kind the direct
//     path writes (step 4). Its objects link with ld -static -e _start (step 3), at any fixed address, below 2 GB with
//     LinkProgramOptions.absolute. The linker provides __init_array_start, __init_array_end, __fini_array_start and
//     __fini_array_end (BUSTER_LINK_SOURCE_LINKER), and the program headers in a loaded segment, where the auxiliary
//     vector's AT_PHDR points.
//   - With libc, a main that the C runtime calls, as in step 5: its objects link with cc, which makes a PIE unless
//     given -no-pie, so without absolute. The runtime sets up TLS, and runs the init functions before main and the fini
//     functions after it. The program imports write, strlen, environ and __errno_location from libc
//     (BUSTER_LINK_SOURCE_RUNTIME_SHARED, without a type, as undefined symbols are in a compiler's objects), and its
//     code reaches them through the GOT, as the direct path binds imports (docs/object-writer-and-linker.md, "Binding
//     before instruction selection").
//   - With LinkProgramOptions.shared, a program that calls libc built as a shared library, as -shared links it: its
//     main is buster_library_main, which a program loads the library (dlopen) to call (dlsym) with main's arguments,
//     after the loader has run its init functions. Its code is position-independent: its functions (the workers,
//     buster_mix and buster_library_main) are its exports, global with default visibility, which another object may
//     preempt, and every other global symbol is hidden, since the code reaches the variables and buster_constants
//     RIP-relatively (PC32, PC64) and takes their sizes; its thread-locals are reached with initial-exec (GOTTPOFF),
//     since a shared library can't know their offsets from the thread pointer (local-exec's TPOFF32); and each
//     function's FDE points at a local symbol at the function's start, as a compiler's FDEs point at a local label,
//     since a PC32 to a function another object may preempt is no offset the library can know.
// Either kind refers through the GOT to buster_absent, a weak symbol that nothing defines (BUSTER_LINK_SOURCE_NONE).
// Link either with wild 0.10, which doesn't link SIZE32 and SIZE64, without LinkProgramOptions.sizes. The program
// prints a checksum of everything it read, as 16 hexadecimal digits and a newline (LinkProgram.output), and exits with
// status 0; what a link gets wrong changes the checksum or crashes the program. Each unit is a translation unit, whose
// atoms the object writer puts in one object:
//   - _start (GLOBAL, unit 0, without libc) calls buster_main with the initial stack. buster_main finds the program
//     headers through the auxiliary vector, sets up the TLS block from PT_TLS in buster_tls_area (variant II: the
//     block ends at the thread pointer, aligned to p_align, which points at itself, and its start keeps p_vaddr's
//     remainder modulo p_align, which isn't 0 when a linker starts .tdata off .tbss's larger alignment) and sets FS
//     with arch_prctl, calls the entries from __init_array_start to __init_array_end, calls every worker through
//     buster_functions with depth 1 and folds each result into the checksum with buster_mix, calls the entries from
//     __fini_array_start to __fini_array_end, folds the values that buster_globals and buster_far point at and the
//     values that link_program_emit_kinds and link_program_emit_imports read, writes the checksum with
//     buster_write_hex and exits with exit_group.
//   - main (GLOBAL, unit 0, with libc) does the same from the workers on, but for the fini functions, and returns 0.
//     Before the workers, it stores a value from the plan in errno, a thread-local of libc's (through
//     __errno_location), and link_program_emit_imports reads it back after them: if the program's TLS block overlapped
//     libc's, the workers' stores would change it. buster_write_hex writes with libc's write.
//   - Workers f_N (GLOBAL, unit N % units): each reads a global RIP-relatively (PC32) and one through the GOT
//     (LOAD_GOT), an entry of buster_constants (PC32 with an addend), a thread-local with local-exec (TPOFF32) and one
//     with initial-exec (GOTTPOFF), and a byte of a string in its unit's pool (PC32); mixes them with filler
//     arithmetic; stores the result to a global (PC32) and adds it to the local-exec thread-local (TPOFF32); and with
//     depth 1 calls another worker directly (CALL), one through the GOT (CALL_GOT) and buster_mix (CALL), each with
//     depth 0.
//   - Globals g_N and thread-locals t_N (unit N % units): 8 bytes, or 16 aligned to 16 (an over-aligned object, padded
//     to a multiple of its alignment), with random initial values (DATA, TDATA) or zero (BSS, TBSS): the first of each
//     has a value and the second is zero. The first of each is GLOBAL, and of the others, a quarter are LOCAL, which
//     only their own unit refers to.
//   - init_N and fini_N (LOCAL, unit N % units) add a constant to a global; their unit's INIT_ARRAY or FINI_ARRAY atom
//     lists them (ABS64).
//   - A string pool per unit (STRINGS), in which each string is a LOCAL symbol without a name. Unit 0's starts with the
//     hexadecimal digits.
//   - In unit 0 besides: buster_mix (GLOBAL), buster_write_hex, buster_constants (RODATA, GLOBAL), buster_functions
//     and buster_globals (DATA_RELRO, ABS64; buster_globals' last entries point into buster_constants, with addends),
//     buster_relative (DATA_RELRO, PC64), buster_far (DATA, ABS64 with addends past 2^32, which go to the side table
//     and which the code subtracts again), buster_output (BSS), and buster_tls_area (BSS) without libc or
//     buster_imports (DATA_RELRO, ABS64 to strlen) with it.
//   - Unwind tables: each unit's .eh_frame (EH_FRAME), a CIE and an FDE for each of the unit's functions (PC32 to the
//     function), whose call frame instructions follow its pushes and pops as a compiler's would
//     (link_program_emit_unwind), so that linking the program has the work a compiler's objects give, as
//     docs/object-writer-and-linker.md's step 5 asks.
// Every class but NONE, INIT and FINI (which only the C runtime's objects have) appears given two thread-locals, an
// init function and a fini function (as with the defaults), and every fixup kind: ABS32, ABS32S and ABS64 in code only
// with absolute addresses, and SIZE32 and SIZE64 only with sizes. With libc, a fini function shows only if it crashes,
// since it runs after the output is written.
// LinkProgram.output comes from a model of the program that link_program_generate runs: the same arithmetic in C, on
// the same random choices.
//
// Entry points: link_program_default_options, link_program_generate and link_program_release. A caller that adds
// records of its own before and after the program's atoms (the C runtime's, which come before and after the program in
// link order) calls link_program_generate_begin, which adds the program's symbols and no atom,
// link_program_generate_atoms, which adds its atoms, and link_program_generate_end instead of link_program_generate. main.c includes this file after link.c. Layout, in this order: the API (LinkProgramOptions, LinkProgram and the prototypes); the plan,
// which holds the random choices (LinkProgramPlan and link_program_plan); the model (link_program_mix,
// link_program_model_body and link_program_run_model); the code (LinkProgramCode, LinkProgramBuilder, the instruction
// builders, link_program_finish, and link_program_emit_start, _mix, _write_hex, _hooks, _fold, _kinds, _imports,
// _runtime, _main, _worker and _hook, with each function's unwind instructions, link_program_unwind_instructions); the
// data (link_program_add_data, link_program_add_pointers, link_program_emit_variable and link_program_emit_data); the
// unwind tables (link_program_emit_unwind); and generating (link_program_add_symbols,
// link_program_capacity, link_program_bytes and link_program_generate).

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

// ---------------------------------------------------------------------------------------------------------------------
// API

// 16 hexadecimal digits and a newline.
#define BUSTER_LINK_PROGRAM_OUTPUT_BYTES 17
// Each count in the options is at most this, which keeps every id and every table's length well within 32 bits, and the
// code within the 2 GB that PC32 fields reach.
#define BUSTER_LINK_PROGRAM_MAX_COUNT (1u << 22)
#define BUSTER_LINK_PROGRAM_MAX_FILLER 64

typedef struct LinkProgramOptions LinkProgramOptions;
struct LinkProgramOptions
{
    uint64_t seed;
    // Each at least 1, and at most BUSTER_LINK_PROGRAM_MAX_COUNT.
    uint32_t units;
    uint32_t functions;
    uint32_t globals;
    uint32_t thread_locals;
    // Strings per unit, each of 1 to BUSTER_LINK_PROGRAM_MAX_STRING bytes; at least 1.
    uint32_t strings;
    // At most BUSTER_LINK_PROGRAM_MAX_COUNT each; 0 is allowed.
    uint32_t init_functions;
    uint32_t fini_functions;
    // A worker has from 0 to this many filler instructions, at most BUSTER_LINK_PROGRAM_MAX_FILLER.
    uint32_t filler;
    // Whether the code has absolute addresses (ABS32, ABS32S, and ABS64 in a movabs), which only an executable at a
    // fixed address below 2 GB can have: not a PIE.
    bool absolute;
    // Whether the code takes symbols' sizes (SIZE32 and SIZE64), which wild 0.10 doesn't link.
    bool sizes;
    // Whether the program calls libc: a main that the C runtime calls, and imports from libc, instead of a _start that
    // makes system calls.
    bool libc;
    // Whether the program is a shared library (-shared), which a program loads (dlopen) to call its entry,
    // buster_library_main, with main's arguments: position-independent code, whose exports are its functions alone (see
    // the orientation comment). With libc, and without absolute.
    bool shared;
};

typedef struct LinkProgramPlan LinkProgramPlan;
typedef struct LinkProgramBuilder LinkProgramBuilder;

typedef struct LinkProgram LinkProgram;
struct LinkProgram
{
    Link link;
    // What the link's tables, its names' bytes and its atoms' bytes are in, which link_program_release releases.
    Arena tables;
    Arena names;
    Arena bytes;
    // The plan and the builder from link_program_generate_begin to link_program_generate_end, and what they and the
    // code being built are in.
    Arena work;
    LinkProgramPlan* plan;
    LinkProgramBuilder* builder;
    // What it was generated with.
    LinkProgramOptions options;
    // _start, or main with libc.
    SymbolId entry;
    // What the program writes to its standard output.
    uint8_t output[BUSTER_LINK_PROGRAM_OUTPUT_BYTES];
    // How many instructions its functions have, and their bytes of code (without the padding between functions).
    uint64_t instructions;
    uint64_t code_bytes;
    // Why link_program_generate failed, or 0.
    const char* problem;
};

// Small sizes, for the tests.
static __attribute__((unused)) LinkProgramOptions link_program_default_options(void);
// Generates the program the options describe into `program`, whose arenas it reserves. Returns false, with
// program->problem (and program->link.error if the representation refused a record), if the options are out of range or
// memory runs out; release the program either way.
static __attribute__((unused)) bool link_program_generate(LinkProgram* program, LinkProgramOptions options);
// link_program_generate in three steps. link_program_generate_begin checks the options, reserves the arenas with room for the
// program and for `extra` more records, `extra_name_bytes` more bytes of names (with their NULs) and `extra_bytes` more
// bytes of atoms (with their alignments), plans the program and adds its symbols, none of them defined yet.
// link_program_generate_atoms adds the program's atoms and fixups, and defines its symbols; link_program_generate_end
// computes its output
// and checks the link with link_check. Each returns false, with program->problem, as link_program_generate does; once
// one has, the next ones do too. Release the program either way.
static __attribute__((unused)) bool link_program_generate_begin(LinkProgram* program, LinkProgramOptions options, LinkCapacity extra, uint64_t extra_name_bytes, uint64_t extra_bytes);
static __attribute__((unused)) bool link_program_generate_atoms(LinkProgram* program);
static __attribute__((unused)) bool link_program_generate_end(LinkProgram* program);
static __attribute__((unused)) void link_program_release(LinkProgram* program);

// ---------------------------------------------------------------------------------------------------------------------
// The plan: every random choice, which the code and the model both follow.

#define BUSTER_LINK_PROGRAM_MAX_STRING 24
#define BUSTER_LINK_PROGRAM_CONSTANTS 16
// buster_globals' entries into buster_constants, and buster_far's entries.
#define BUSTER_LINK_PROGRAM_CONSTANT_POINTERS 4
#define BUSTER_LINK_PROGRAM_FAR_POINTERS 4
// buster_far's entry j points at its global plus (j + 1) << BUSTER_LINK_PROGRAM_FAR_SHIFT.
#define BUSTER_LINK_PROGRAM_FAR_SHIFT 32
// The globals that link_program_emit_kinds reads, one for each of GOTPCREL, PC64, SIZE32, SIZE64, ABS32, ABS32S and
// ABS64 (in code).
#define BUSTER_LINK_PROGRAM_KIND_GLOBALS 7
// buster_mix's multiplier (MurmurHash2's), an imm32 that fits sign-extended.
#define BUSTER_LINK_PROGRAM_MIX_MULTIPLIER 0x5bd1e995
// How many times a variable is picked at random before the pick falls back to the first one, which is global.
#define BUSTER_LINK_PROGRAM_PICKS 4
// Functions start on 16 bytes, padded with int3 (0xcc), as lld pads them.
#define BUSTER_LINK_PROGRAM_FUNCTION_ALIGNMENT_LOG2 4
#define BUSTER_LINK_PROGRAM_INT3 0xcc
// What the thread pointer has above it in buster_tls_area: its own address, in the first 8 bytes.
#define BUSTER_LINK_PROGRAM_TCB_BYTES 64
// splitmix64's increment.
#define BUSTER_LINK_PROGRAM_GOLDEN_RATIO 0x9E3779B97F4A7C15ull

// The filler instructions' operations on the accumulator: add and xor with an imm32, rotate left by an imm8 and
// multiply by an imm32.
typedef enum LinkProgramOperation
{
    LINK_PROGRAM_ADD,
    LINK_PROGRAM_XOR,
    LINK_PROGRAM_ROTATE,
    LINK_PROGRAM_MULTIPLY,
    LINK_PROGRAM_OPERATIONS,
} LinkProgramOperation;

typedef struct LinkProgramFiller LinkProgramFiller;
struct LinkProgramFiller
{
    int32_t operand;
    uint8_t operation;
};

// A worker's choices: what it reads, writes and calls (indices of globals, thread-locals, strings and workers).
typedef struct LinkProgramWorker LinkProgramWorker;
struct LinkProgramWorker
{
    int32_t multiplier;
    uint32_t read_global;
    uint32_t got_global;
    uint32_t constant;
    uint32_t local_exec;
    uint32_t initial_exec;
    uint32_t string;
    // The byte of the string it reads, and the byte's value.
    uint32_t string_byte;
    uint8_t string_value;
    uint32_t write_global;
    uint32_t callee;
    uint32_t got_callee;
    // Its filler instructions in LinkProgramPlan.fillers.
    uint32_t filler_begin;
    uint32_t filler_count;
};

// A global or a thread-local. The first of each has a value and is global, and the second is zero, so that DATA and
// BSS, and with two thread-locals TDATA and TBSS, all have atoms.
typedef struct LinkProgramVariable LinkProgramVariable;
struct LinkProgramVariable
{
    uint64_t value;
    bool local;
    uint8_t alignment_log2;
};

// An init or fini function: the global it adds to, and what it adds.
typedef struct LinkProgramHook LinkProgramHook;
struct LinkProgramHook
{
    uint32_t global;
    int32_t addend;
};

struct LinkProgramPlan
{
    LinkProgramOptions options;
    LinkProgramWorker* workers;
    LinkProgramFiller* fillers;
    LinkProgramVariable* globals;
    LinkProgramVariable* thread_locals;
    // The init functions, then the fini functions.
    LinkProgramHook* hooks;
    uint64_t constants[BUSTER_LINK_PROGRAM_CONSTANTS];
    uint32_t constant_pointers[BUSTER_LINK_PROGRAM_CONSTANT_POINTERS];
    uint32_t far_globals[BUSTER_LINK_PROGRAM_FAR_POINTERS];
    // The globals buster_main reads through the kinds the workers and the tables don't have (link_program_emit_kinds).
    uint32_t kind_globals[BUSTER_LINK_PROGRAM_KIND_GLOBALS];
    // What main stores in errno with libc, and reads back after the workers (link_program_emit_imports).
    int32_t errno_value;
    // Strings: string 0 is the hexadecimal digits, in unit 0, and then each unit has options.strings, unit u's from
    // 1 + u * options.strings. Each string's offset in its unit's pool and its length without the NUL.
    uint32_t string_count;
    uint32_t* string_offsets;
    uint8_t* string_lengths;
    // Each unit's pool, and its length.
    uint8_t** pools;
    uint32_t* pool_lengths;
    // How many globals are global: buster_globals' entries before its constant pointers.
    uint32_t global_globals;
    // buster_tls_area's size: room for every thread-local, the alignment and the thread control block.
    uint32_t tls_area_bytes;
};

// splitmix64's finalizer.
static uint64_t link_program_scramble(uint64_t x)
{
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// splitmix64, whose sequence for a seed starts from the scrambled seed: starting from the seed times the increment
// would make seed s + 1's sequence seed s's without its first number.
static uint64_t link_program_random(uint64_t* state)
{
    *state += BUSTER_LINK_PROGRAM_GOLDEN_RATIO;
    return link_program_scramble(*state);
}

// A number from 0 to bound - 1.
static uint32_t link_program_below(uint64_t* state, uint32_t bound)
{
    return (uint32_t)(((link_program_random(state) >> 32) * bound) >> 32);
}

// A variable that code of `unit` can refer to: any global one or, unless global_only, a local one of the same unit;
// the first variable, which is global, when the random picks keep missing.
static uint32_t link_program_pick(uint64_t* state, const LinkProgramVariable* variables, uint32_t count, uint32_t units, uint32_t unit, bool global_only)
{
    uint32_t result = 0;
    bool found = false;
    for (uint32_t i = 0; !found && i < BUSTER_LINK_PROGRAM_PICKS; i += 1)
    {
        const uint32_t candidate = link_program_below(state, count);
        found = !variables[candidate].local || (!global_only && candidate % units == unit);
        result = found ? candidate : 0;
    }
    return result;
}

static void link_program_plan_variables(uint64_t* state, LinkProgramVariable* variables, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        // The first has a value that isn't 0 and the second is zero; a third of the others are zero.
        const uint32_t kind = link_program_below(state, 24);
        const bool zero = i == 1 || (i > 1 && kind < 8);
        const uint64_t value = link_program_random(state) | (i == 0 ? 1 : 0);
        variables[i] = (LinkProgramVariable){
            .value = zero ? 0 : value,
            .local = i != 0 && link_program_below(state, 4) == 0,
            .alignment_log2 = kind % 8 == 7 ? 4 : 3,
        };
    }
}

// Makes every random choice, in a fixed order, so that the seed decides them all. Its arrays are in `arena`.
static bool link_program_plan(LinkProgramPlan* plan, Arena* arena, LinkProgramOptions options)
{
    static const uint8_t hexadecimal_digits[] = "0123456789abcdef";
    const uint32_t units = options.units;
    const uint32_t hooks = options.init_functions + options.fini_functions;
    uint64_t state = link_program_scramble(options.seed);
    *plan = (LinkProgramPlan){
        .options = options,
        .string_count = 1 + units * options.strings,
    };
    plan->workers = BUSTER_ARENA_PUSH_ARRAY(arena, LinkProgramWorker, options.functions);
    plan->fillers = BUSTER_ARENA_PUSH_ARRAY(arena, LinkProgramFiller, (uint64_t)options.functions * options.filler + 1);
    plan->globals = BUSTER_ARENA_PUSH_ARRAY(arena, LinkProgramVariable, options.globals);
    plan->thread_locals = BUSTER_ARENA_PUSH_ARRAY(arena, LinkProgramVariable, options.thread_locals);
    plan->hooks = BUSTER_ARENA_PUSH_ARRAY(arena, LinkProgramHook, hooks + 1);
    plan->string_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, plan->string_count);
    plan->string_lengths = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, plan->string_count);
    plan->pools = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t*, units);
    plan->pool_lengths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, units);
    bool result = plan->workers != 0 && plan->fillers != 0 && plan->globals != 0 && plan->thread_locals != 0 && plan->hooks != 0 && plan->string_offsets != 0 &&
                  plan->string_lengths != 0 && plan->pools != 0 && plan->pool_lengths != 0;

    // Strings and pools, unit by unit.
    for (uint32_t unit = 0; result && unit < units; unit += 1)
    {
        const uint32_t first = 1 + unit * options.strings;
        const uint32_t extra = unit == 0 ? (uint32_t)sizeof(hexadecimal_digits) : 0;
        uint8_t* pool = arena_push(arena, (uint64_t)options.strings * (BUSTER_LINK_PROGRAM_MAX_STRING + 1) + extra, 1);
        uint32_t length = 0;
        result = pool != 0;
        if (result && unit == 0)
        {
            memcpy(pool, hexadecimal_digits, sizeof(hexadecimal_digits));
            plan->string_offsets[0] = 0;
            plan->string_lengths[0] = sizeof(hexadecimal_digits) - 1;
            length = sizeof(hexadecimal_digits);
        }
        for (uint32_t i = 0; result && i < options.strings; i += 1)
        {
            const uint32_t string_length = 1 + link_program_below(&state, BUSTER_LINK_PROGRAM_MAX_STRING);
            plan->string_offsets[first + i] = length;
            plan->string_lengths[first + i] = (uint8_t)string_length;
            for (uint32_t j = 0; j < string_length; j += 1)
            {
                pool[length + j] = (uint8_t)('a' + link_program_below(&state, 26));
            }
            pool[length + string_length] = 0;
            length += string_length + 1;
        }
        if (result)
        {
            plan->pools[unit] = pool;
            plan->pool_lengths[unit] = length;
        }
    }

    if (result)
    {
        link_program_plan_variables(&state, plan->globals, options.globals);
        link_program_plan_variables(&state, plan->thread_locals, options.thread_locals);
        // Each thread-local's size, and as much again for the padding a linker may put before it.
        uint64_t tls_bytes = 0;
        for (uint32_t i = 0; i < options.thread_locals; i += 1)
        {
            tls_bytes += 2ull << plan->thread_locals[i].alignment_log2;
        }
        for (uint32_t i = 0; i < options.globals; i += 1)
        {
            plan->global_globals += plan->globals[i].local ? 0 : 1;
        }
        // The block, the padding before it when the thread pointer is aligned down, and the control block.
        plan->tls_area_bytes = (uint32_t)((tls_bytes + 2 * 16 + BUSTER_LINK_PROGRAM_TCB_BYTES + 63) & ~63ull);
        for (uint32_t i = 0; i < BUSTER_LINK_PROGRAM_CONSTANTS; i += 1)
        {
            plan->constants[i] = link_program_random(&state);
        }
        for (uint32_t i = 0; i < BUSTER_LINK_PROGRAM_CONSTANT_POINTERS; i += 1)
        {
            plan->constant_pointers[i] = link_program_below(&state, BUSTER_LINK_PROGRAM_CONSTANTS);
        }
        for (uint32_t i = 0; i < BUSTER_LINK_PROGRAM_FAR_POINTERS; i += 1)
        {
            plan->far_globals[i] = link_program_pick(&state, plan->globals, options.globals, units, 0, true);
        }
        for (uint32_t i = 0; i < BUSTER_LINK_PROGRAM_KIND_GLOBALS; i += 1)
        {
            plan->kind_globals[i] = link_program_pick(&state, plan->globals, options.globals, units, 0, true);
        }
        plan->errno_value = (int32_t)link_program_random(&state);
        for (uint32_t i = 0; i < hooks; i += 1)
        {
            const uint32_t unit = (i < options.init_functions ? i : i - options.init_functions) % units;
            plan->hooks[i] = (LinkProgramHook){
                .global = link_program_pick(&state, plan->globals, options.globals, units, unit, false),
                .addend = (int32_t)link_program_random(&state),
            };
        }
        uint32_t fillers = 0;
        for (uint32_t i = 0; i < options.functions; i += 1)
        {
            const uint32_t unit = i % units;
            const uint32_t string = 1 + unit * options.strings + link_program_below(&state, options.strings);
            const uint32_t string_byte = link_program_below(&state, plan->string_lengths[string]);
            LinkProgramWorker* worker = &plan->workers[i];
            *worker = (LinkProgramWorker){
                .multiplier = (int32_t)link_program_random(&state),
                .read_global = link_program_pick(&state, plan->globals, options.globals, units, unit, false),
                .got_global = link_program_pick(&state, plan->globals, options.globals, units, unit, true),
                .constant = link_program_below(&state, BUSTER_LINK_PROGRAM_CONSTANTS),
                .local_exec = link_program_pick(&state, plan->thread_locals, options.thread_locals, units, unit, false),
                .initial_exec = link_program_pick(&state, plan->thread_locals, options.thread_locals, units, unit, true),
                .string = string,
                .string_byte = string_byte,
                .string_value = plan->pools[unit][plan->string_offsets[string] + string_byte],
                .write_global = link_program_pick(&state, plan->globals, options.globals, units, unit, false),
                .callee = link_program_below(&state, options.functions),
                .got_callee = link_program_below(&state, options.functions),
                .filler_begin = fillers,
                .filler_count = link_program_below(&state, options.filler + 1),
            };
            for (uint32_t j = 0; j < worker->filler_count; j += 1)
            {
                const uint8_t operation = (uint8_t)link_program_below(&state, LINK_PROGRAM_OPERATIONS);
                const int32_t random = (int32_t)link_program_random(&state);
                plan->fillers[fillers + j] = (LinkProgramFiller){
                    .operand = operation == LINK_PROGRAM_ROTATE ? 1 + (random & 0x3f) % 63 : random,
                    .operation = operation,
                };
            }
            fillers += worker->filler_count;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The model: the program's arithmetic in C, from which LinkProgram.output comes.

static uint64_t link_program_mix(uint64_t x)
{
    uint64_t result = (x >> 29) ^ x;
    result *= (uint64_t)(int64_t)BUSTER_LINK_PROGRAM_MIX_MULTIPLIER;
    result ^= result >> 32;
    return result;
}

static uint64_t link_program_rotate(uint64_t x, uint32_t count)
{
    return (x << count) | (x >> (64 - count));
}

// What the worker does before it calls anything, on the globals and thread-locals: the result it returns at depth 0.
static uint64_t link_program_model_body(const LinkProgramPlan* plan, uint64_t* globals, uint64_t* thread_locals, uint32_t index, uint64_t x)
{
    const LinkProgramWorker* worker = &plan->workers[index];
    uint64_t result = x * (uint64_t)(int64_t)worker->multiplier;
    result += globals[worker->read_global];
    result ^= globals[worker->got_global];
    result += plan->constants[worker->constant];
    result += thread_locals[worker->local_exec];
    result += thread_locals[worker->initial_exec];
    result += worker->string_value;
    for (uint32_t i = 0; i < worker->filler_count; i += 1)
    {
        const LinkProgramFiller* filler = &plan->fillers[worker->filler_begin + i];
        const uint64_t operand = (uint64_t)(int64_t)filler->operand;
        if (filler->operation == LINK_PROGRAM_ADD)
        {
            result += operand;
        }
        else if (filler->operation == LINK_PROGRAM_XOR)
        {
            result ^= operand;
        }
        else if (filler->operation == LINK_PROGRAM_ROTATE)
        {
            result = link_program_rotate(result, (uint32_t)filler->operand);
        }
        else
        {
            result *= operand;
        }
    }
    globals[worker->write_global] = result;
    thread_locals[worker->local_exec] += result;
    return result;
}

// Runs the program on the model and writes its output. The globals' and thread-locals' values are in `arena`.
static bool link_program_run_model(const LinkProgramPlan* plan, Arena* arena, uint8_t* output)
{
    static const char digits[] = "0123456789abcdef";
    const LinkProgramOptions* options = &plan->options;
    uint64_t* globals = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, options->globals);
    uint64_t* thread_locals = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, options->thread_locals);
    const bool result = globals != 0 && thread_locals != 0;
    if (result)
    {
        for (uint32_t i = 0; i < options->globals; i += 1)
        {
            globals[i] = plan->globals[i].value;
        }
        for (uint32_t i = 0; i < options->thread_locals; i += 1)
        {
            thread_locals[i] = plan->thread_locals[i].value;
        }
        for (uint32_t i = 0; i < options->init_functions; i += 1)
        {
            globals[plan->hooks[i].global] += (uint64_t)(int64_t)plan->hooks[i].addend;
        }
        uint64_t checksum = 0;
        for (uint32_t i = 0; i < options->functions; i += 1)
        {
            const LinkProgramWorker* worker = &plan->workers[i];
            uint64_t value = link_program_model_body(plan, globals, thread_locals, i, i);
            value += link_program_model_body(plan, globals, thread_locals, worker->callee, value);
            value ^= link_program_model_body(plan, globals, thread_locals, worker->got_callee, value);
            value += link_program_mix(value);
            checksum = link_program_mix(value + checksum);
        }
        // With libc, the fini functions run after main has returned.
        for (uint32_t i = 0; !options->libc && i < options->fini_functions; i += 1)
        {
            const LinkProgramHook* hook = &plan->hooks[options->init_functions + i];
            globals[hook->global] += (uint64_t)(int64_t)hook->addend;
        }
        for (uint32_t i = 0; i < options->globals; i += 1)
        {
            if (!plan->globals[i].local)
            {
                checksum = link_program_mix(globals[i] ^ checksum);
            }
        }
        for (uint32_t i = 0; i < BUSTER_LINK_PROGRAM_CONSTANT_POINTERS; i += 1)
        {
            checksum = link_program_mix(plan->constants[plan->constant_pointers[i]] ^ checksum);
        }
        for (uint32_t i = 0; i < BUSTER_LINK_PROGRAM_FAR_POINTERS; i += 1)
        {
            checksum = link_program_mix(globals[plan->far_globals[i]] ^ checksum);
        }
        // GOTPCREL and PC64 read a global, SIZE32 and SIZE64 take one's size, and ABS32, ABS32S and ABS64 read one.
        const uint32_t* kind = plan->kind_globals;
        checksum = link_program_mix(globals[kind[0]] ^ checksum);
        checksum = link_program_mix(globals[kind[1]] ^ checksum);
        for (uint32_t i = 2; options->sizes && i < 4; i += 1)
        {
            checksum = link_program_mix((1ull << plan->globals[kind[i]].alignment_log2) ^ checksum);
        }
        for (uint32_t i = 4; options->absolute && i < BUSTER_LINK_PROGRAM_KIND_GLOBALS; i += 1)
        {
            checksum = link_program_mix(globals[kind[i]] ^ checksum);
        }
        // The weak reference's slot holds 0. With libc, environ less envp is 0, strlen of the hexadecimal digits is 16,
        // strlen's GOT slot less buster_imports' entry is 0, and errno is what main stored.
        checksum = link_program_mix(checksum);
        if (options->libc)
        {
            checksum = link_program_mix(checksum);
            checksum = link_program_mix(plan->string_lengths[0] ^ checksum);
            checksum = link_program_mix(checksum);
            checksum = link_program_mix((uint32_t)plan->errno_value ^ checksum);
        }
        for (uint32_t i = 0; i < 16; i += 1)
        {
            output[i] = (uint8_t)digits[(checksum >> (60 - 4 * i)) & 15];
        }
        output[16] = '\n';
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The code

// The most instructions a function of the program has (buster_main's 130 or so, or a worker's 37 and its filler), and
// the most branches and labels: branches are jumps within the function, whose rel32s come from a first encoding.
#define BUSTER_LINK_PROGRAM_MAX_INSTRUCTIONS (192 + BUSTER_LINK_PROGRAM_MAX_FILLER)
#define BUSTER_LINK_PROGRAM_MAX_BRANCHES 32

// A function being built: its instructions, the references of their symbolic fields, and its branches and labels.
typedef struct LinkProgramCode LinkProgramCode;
struct LinkProgramCode
{
    X86Instruction instructions[BUSTER_LINK_PROGRAM_MAX_INSTRUCTIONS];
    LinkReference references[BUSTER_LINK_PROGRAM_MAX_INSTRUCTIONS];
    uint32_t offsets[BUSTER_LINK_PROGRAM_MAX_INSTRUCTIONS];
    uint32_t fixup_ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_LINK_PROGRAM_MAX_INSTRUCTIONS)];
    // Each branch's instruction and label, and each label's instruction: the one it is before.
    uint32_t branches[BUSTER_LINK_PROGRAM_MAX_BRANCHES];
    uint32_t branch_labels[BUSTER_LINK_PROGRAM_MAX_BRANCHES];
    uint32_t labels[BUSTER_LINK_PROGRAM_MAX_BRANCHES];
    uint32_t instruction_count;
    uint32_t reference_count;
    uint32_t branch_count;
    uint32_t label_count;
    // How many instructions have BUSTER_X86_FORM_FIXUP.
    uint32_t marked;
};

// The unwind tables (link_program_emit_unwind): each unit's .eh_frame holds a CIE, GCC's and clang's for x86-64
// (augmentation "zR", code alignment 1, data alignment -8, the return address in DWARF register 16, FDE pointers
// pc-relative sdata4, the CFA at rsp + 8 and the return address below it), then an FDE for each of the unit's functions
// in the order they were added, each padded to 4 bytes as clang pads them: its pc_begin a PC32 fixup to the function,
// its range the function's length, and its instructions from link_program_unwind_instructions.
#define BUSTER_LINK_PROGRAM_CIE_BYTES 24
// An FDE's bytes before its instructions: its length, its CIE pointer, pc_begin, the range and the augmentation data's
// length (0).
#define BUSTER_LINK_PROGRAM_FDE_HEADER_BYTES 17
// The most bytes of instructions an FDE has: after each instruction that moves rsp an advance (at most 5 bytes),
// DW_CFA_def_cfa_offset (at most 3) and a register's save or restore (at most 3); _start's has 2.
#define BUSTER_LINK_PROGRAM_MAX_UNWIND_BYTES (11 * BUSTER_LINK_PROGRAM_MAX_INSTRUCTIONS + 2)
// A bound on an FDE's bytes, for link_program_bytes: buster_main moves rsp the most, 12 times.
#define BUSTER_LINK_PROGRAM_MAX_FDE_BYTES (BUSTER_LINK_PROGRAM_FDE_HEADER_BYTES + 11 * 12 + 3)
// The DWARF call frame instructions the FDEs use, and DWARF's number of the return address (rip).
#define BUSTER_LINK_PROGRAM_CFA_ADVANCE_LOC 0x40
#define BUSTER_LINK_PROGRAM_CFA_OFFSET 0x80
#define BUSTER_LINK_PROGRAM_CFA_RESTORE 0xc0
#define BUSTER_LINK_PROGRAM_CFA_ADVANCE_LOC1 0x02
#define BUSTER_LINK_PROGRAM_CFA_ADVANCE_LOC2 0x03
#define BUSTER_LINK_PROGRAM_CFA_ADVANCE_LOC4 0x04
#define BUSTER_LINK_PROGRAM_CFA_UNDEFINED 0x07
#define BUSTER_LINK_PROGRAM_CFA_DEF_CFA_OFFSET 0x0e
#define BUSTER_LINK_PROGRAM_DWARF_RIP 16

static const uint8_t link_program_cie[BUSTER_LINK_PROGRAM_CIE_BYTES] = {
    // Its length past this field, and the CIE id.
    0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Version 1, "zR", code alignment 1, data alignment -8, return address register 16, and one byte of augmentation
    // data: DW_EH_PE_pcrel | DW_EH_PE_sdata4.
    0x01, 'z', 'R', 0x00, 0x01, 0x78, 0x10, 0x01, 0x1b,
    // DW_CFA_def_cfa rsp + 8, DW_CFA_offset rip at CFA - 8, and two DW_CFA_nops.
    0x0c, 0x07, 0x08, 0x90, 0x01, 0x00, 0x00,
};

// A function's FDE until its unit's .eh_frame is built: its symbol and unit, its length (the FDE's range) and its
// instructions.
typedef struct LinkProgramUnwind LinkProgramUnwind;
struct LinkProgramUnwind
{
    // What its FDE points at: the function's symbol, or an exported function's anchor (LinkProgramBuilder.first_anchor).
    SymbolId symbol;
    uint32_t unit;
    uint32_t length;
    uint32_t instruction_bytes;
    const uint8_t* instructions;
};

// What generating a program works with: the program, the plan, the function being built and the symbols' ids (0 for
// those the program's kind doesn't have). The symbols of each kind are added one after another, from the first's id.
struct LinkProgramBuilder
{
    LinkProgram* program;
    const LinkProgramPlan* plan;
    LinkProgramCode* code;
    SymbolId start;
    // buster_main, or main with libc.
    SymbolId main;
    SymbolId mix;
    SymbolId write_hex;
    SymbolId constants;
    SymbolId functions;
    SymbolId globals_table;
    SymbolId far;
    SymbolId relative;
    SymbolId imports;
    SymbolId tls_area;
    SymbolId output;
    SymbolId absent;
    SymbolId init_array_start;
    SymbolId init_array_end;
    SymbolId fini_array_start;
    SymbolId fini_array_end;
    SymbolId libc_write;
    SymbolId libc_strlen;
    SymbolId libc_environ;
    SymbolId libc_errno_location;
    SymbolId first_worker;
    SymbolId first_global;
    SymbolId first_thread_local;
    SymbolId first_hook;
    SymbolId first_string;
    // With LinkProgramOptions.shared, the local symbols at the exported functions' starts that their FDEs point at:
    // each worker's, then buster_mix's and buster_library_main's.
    SymbolId first_anchor;
    // Each function's FDE, in the order the functions were added, room for unwind_capacity of them.
    LinkProgramUnwind* unwind;
    uint32_t unwind_count;
    uint32_t unwind_capacity;
};

// Instructions. A register operand with a form's ModRM.reg and ModRM.rm (or +r) roles, an immediate, a memory operand
// at [base + displacement] or [base + index * 2^scale + displacement], and a symbolic memory operand: RIP-relative, or
// absolute with a segment override in the form (local-exec TLS), whose displacement the fixup fills in.
static X86Instruction link_program_registers(uint32_t form, uint8_t reg, uint8_t rm)
{
    X86Instruction result = {
        .reg = reg,
        .rm = rm,
        .mode = BUSTER_X86_REGISTER_DIRECT,
        .form = (uint16_t)form,
    };
    return result;
}

static X86Instruction link_program_immediate(uint32_t form, uint8_t rm, int32_t immediate)
{
    X86Instruction result = {
        .immediate = immediate,
        .rm = rm,
        .mode = BUSTER_X86_REGISTER_DIRECT,
        .form = (uint16_t)form,
    };
    return result;
}

static X86Instruction link_program_memory(uint32_t form, uint8_t reg, uint8_t base, int32_t displacement)
{
    X86Instruction result = {
        .displacement = displacement,
        .reg = reg,
        .rm = base,
        .mode = BUSTER_X86_MEMORY_BASE,
        .form = (uint16_t)form,
    };
    return result;
}

static X86Instruction link_program_indexed(uint32_t form, uint8_t reg, uint8_t base, uint8_t index, uint8_t scale, int32_t displacement)
{
    X86Instruction result = {
        .displacement = displacement,
        .reg = reg,
        .rm = base,
        .index = index,
        .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(scale),
        .form = (uint16_t)form,
    };
    return result;
}

static X86Instruction link_program_rip(uint32_t form, uint8_t reg)
{
    X86Instruction result = {
        .reg = reg,
        .mode = BUSTER_X86_MEMORY_RIP,
        .form = (uint16_t)form,
    };
    return result;
}

static X86Instruction link_program_absolute(uint32_t form, uint8_t reg)
{
    X86Instruction result = {
        .reg = reg,
        .mode = BUSTER_X86_MEMORY_ABSOLUTE,
        .form = (uint16_t)form,
    };
    return result;
}

static X86Instruction link_program_bare(uint32_t form)
{
    X86Instruction result = {
        .form = (uint16_t)form,
    };
    return result;
}

static void link_program_begin(LinkProgramCode* code)
{
    code->instruction_count = 0;
    code->reference_count = 0;
    code->branch_count = 0;
    code->label_count = 0;
    code->marked = 0;
}

static void link_program_add(LinkProgramCode* code, X86Instruction instruction)
{
    assert(code->instruction_count < BUSTER_LINK_PROGRAM_MAX_INSTRUCTIONS);
    code->instructions[code->instruction_count] = instruction;
    code->instruction_count += 1;
}

// Adds an instruction whose `field` refers to target + addend: marked, with its reference.
static void link_program_refer(LinkProgramCode* code, X86Instruction instruction, X86FixupField field, uint8_t kind, SymbolId target, int64_t addend)
{
    instruction.form |= BUSTER_X86_FORM_FIXUP;
    code->references[code->reference_count] = (LinkReference){
        .instruction = code->instruction_count,
        .field = (uint8_t)field,
        .kind = kind,
        .target = target,
        .addend = addend,
    };
    code->reference_count += 1;
    code->marked += 1;
    link_program_add(code, instruction);
}

// A label, placed later with link_program_place.
static uint32_t link_program_label(LinkProgramCode* code)
{
    assert(code->label_count < BUSTER_LINK_PROGRAM_MAX_BRANCHES);
    const uint32_t result = code->label_count;
    code->labels[result] = UINT32_MAX;
    code->label_count += 1;
    return result;
}

static void link_program_place(LinkProgramCode* code, uint32_t label)
{
    code->labels[label] = code->instruction_count;
}

// A jump with a rel32 (JMP_REL32 or a Jcc's) to `label`.
static void link_program_jump(LinkProgramCode* code, uint32_t form, uint32_t label)
{
    assert(code->branch_count < BUSTER_LINK_PROGRAM_MAX_BRANCHES);
    code->branches[code->branch_count] = code->instruction_count;
    code->branch_labels[code->branch_count] = label;
    code->branch_count += 1;
    link_program_add(code, link_program_bare(form));
}

// Appends `value` as an unsigned LEB128: the end of what `bytes` holds.
static uint32_t link_program_uleb128(uint8_t* bytes, uint32_t used, uint64_t value)
{
    uint64_t left = value;
    uint32_t result = used;
    bool more = true;
    while (more)
    {
        const uint8_t low = (uint8_t)(left & 0x7f);
        left >>= 7;
        more = left != 0;
        bytes[result] = more ? (uint8_t)(low | 0x80) : low;
        result += 1;
    }
    return result;
}

// Appends an advance of the location by `delta` bytes: in DW_CFA_advance_loc's low 6 bits, or after advance_loc1, 2 or
// 4 in as many bytes.
static uint32_t link_program_unwind_advance(uint8_t* bytes, uint32_t used, uint64_t delta)
{
    uint32_t result = used;
    if (delta < 64)
    {
        bytes[result] = (uint8_t)(BUSTER_LINK_PROGRAM_CFA_ADVANCE_LOC | delta);
        result += 1;
    }
    else
    {
        const uint32_t width = delta < 256 ? 1 : delta < 65536 ? 2 : 4;
        bytes[result] = width == 1 ? BUSTER_LINK_PROGRAM_CFA_ADVANCE_LOC1 : width == 2 ? BUSTER_LINK_PROGRAM_CFA_ADVANCE_LOC2 : BUSTER_LINK_PROGRAM_CFA_ADVANCE_LOC4;
        // Little-endian: the low bytes come first.
        memcpy(bytes + result + 1, &delta, width);
        result += 1 + width;
    }
    return result;
}

// The FDE instructions of the function that `code` holds, `length` bytes of it encoded, into `bytes`
// (BUSTER_LINK_PROGRAM_MAX_UNWIND_BYTES of room): their count. The generator's functions move rsp with pushes and pops
// alone, so after each the location advances to its end, the CFA's offset from rsp changes by 8, and a callee-saved
// register pushed is saved at the new offset, or restored when popped, as GCC's CFI says. The outermost function's
// (_start's) say instead that the return address is undefined, as glibc's _start does: it has no caller to unwind to,
// and its `and rsp, -16` leaves the CFA unknown.
static uint32_t link_program_unwind_instructions(const LinkProgramCode* code, uint64_t length, bool outermost, uint8_t* bytes)
{
    // DWARF's numbers of the registers, by the encoder's, and whether the ABI has the callee save each.
    static const uint8_t dwarf[16] = { 0, 2, 1, 3, 7, 6, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15 };
    static const bool callee_saved[16] = {
        [X86_REGISTER_RBX] = true,
        [X86_REGISTER_RBP] = true,
        [X86_REGISTER_R12] = true,
        [X86_REGISTER_R13] = true,
        [X86_REGISTER_R14] = true,
        [X86_REGISTER_R15] = true,
    };
    uint32_t used = 0;
    if (outermost)
    {
        bytes[0] = BUSTER_LINK_PROGRAM_CFA_UNDEFINED;
        bytes[1] = BUSTER_LINK_PROGRAM_DWARF_RIP;
        used = 2;
    }
    uint64_t location = 0;
    uint64_t offset = 8;
    for (uint32_t i = 0; !outermost && i < code->instruction_count; i += 1)
    {
        const X86Instruction* instruction = &code->instructions[i];
        const uint32_t form = instruction->form & ~(uint32_t)BUSTER_X86_FORM_FIXUP;
        const bool push = form == X86_FORM_PUSH_R64 || form == X86_FORM_PUSH_RM64;
        const bool pop = form == X86_FORM_POP_R64;
        const uint8_t reg = instruction->rm & 15;
        const bool saved = (form == X86_FORM_PUSH_R64 || pop) && callee_saved[reg];
        if (push || pop)
        {
            const uint64_t end = i + 1 < code->instruction_count ? code->offsets[i + 1] : length;
            used = link_program_unwind_advance(bytes, used, end - location);
            location = end;
            offset = push ? offset + 8 : offset - 8;
            bytes[used] = BUSTER_LINK_PROGRAM_CFA_DEF_CFA_OFFSET;
            used = link_program_uleb128(bytes, used + 1, offset);
        }
        if (saved && push)
        {
            bytes[used] = (uint8_t)(BUSTER_LINK_PROGRAM_CFA_OFFSET | dwarf[reg]);
            used = link_program_uleb128(bytes, used + 1, offset / 8);
        }
        else if (saved)
        {
            bytes[used] = (uint8_t)(BUSTER_LINK_PROGRAM_CFA_RESTORE | dwarf[reg]);
            used += 1;
        }
    }
    return used;
}

// Keeps the FDE of the function `symbol`, which `code` holds encoded at `length` bytes, for its unit's .eh_frame.
static bool link_program_keep_unwind(LinkProgramBuilder* builder, uint32_t unit, SymbolId symbol, SymbolId anchor, uint64_t length)
{
    LinkProgram* program = builder->program;
    uint8_t instructions[BUSTER_LINK_PROGRAM_MAX_UNWIND_BYTES];
    const uint32_t count = link_program_unwind_instructions(builder->code, length, symbol == builder->start, instructions);
    uint8_t* kept = builder->unwind_count < builder->unwind_capacity ? arena_push(&program->work, count, 1) : 0;
    if (kept != 0)
    {
        memcpy(kept, instructions, count);
        builder->unwind[builder->unwind_count] = (LinkProgramUnwind){
            .symbol = anchor,
            .unit = unit,
            .length = (uint32_t)length,
            .instruction_bytes = count,
            .instructions = kept,
        };
        builder->unwind_count += 1;
    }
    else
    {
        program->problem = "out of memory for the unwind tables";
    }
    return kept != 0;
}

// The local symbol at an exported function's start that its FDE points at in a shared library, or 0 (see the
// orientation comment): a worker's, buster_mix's or buster_library_main's.
static SymbolId link_program_anchor(const LinkProgramBuilder* builder, SymbolId symbol)
{
    const uint32_t functions = builder->plan->options.functions;
    const bool worker = symbol >= builder->first_worker && symbol < builder->first_worker + functions;
    const SymbolId anchor = worker ? builder->first_anchor + (symbol - builder->first_worker)
                            : symbol == builder->mix ? builder->first_anchor + functions
                            : symbol == builder->main ? builder->first_anchor + functions + 1
                                                      : 0;
    return builder->plan->options.shared ? anchor : 0;
}

// Encodes the function being built as an atom of `unit` that defines `symbol`. The first encoding gives the offsets
// that the branches' rel32s come from (the forms' lengths don't depend on them), and the second the marked
// instructions' ends. The function is encoded after the code before it, whose 64-byte stores' overrun it overwrites,
// and padded to 16 bytes with int3 once it is encoded.
static bool link_program_finish(LinkProgramBuilder* builder, uint32_t unit, SymbolId symbol)
{
    LinkProgramCode* code = builder->code;
    LinkProgram* program = builder->program;
    const uint32_t count = code->instruction_count;
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(count);
    uint8_t* output = arena_push(&program->bytes, capacity, 1ull << BUSTER_LINK_PROGRAM_FUNCTION_ALIGNMENT_LOG2);
    int64_t length = output != 0 ? x86_encode(code->instructions, count, output, capacity, code->offsets, 0) : -1;
    for (uint32_t i = 0; length >= 0 && i < code->branch_count; i += 1)
    {
        const uint32_t branch = code->branches[i];
        const uint32_t target = code->labels[code->branch_labels[i]];
        const int64_t end = branch + 1 < count ? code->offsets[branch + 1] : length;
        const int64_t destination = target < count ? code->offsets[target] : length;
        code->instructions[branch].displacement = (int32_t)(destination - end);
    }
    if (length >= 0)
    {
        length = x86_encode(code->instructions, count, output, capacity, 0, code->fixup_ends);
    }
    bool result = false;
    if (length < 0)
    {
        program->problem = output == 0 ? "out of memory for the code" : "x86_encode rejected an instruction";
    }
    else
    {
        const uint64_t alignment = 1ull << BUSTER_LINK_PROGRAM_FUNCTION_ALIGNMENT_LOG2;
        const uint64_t padded = ((uint64_t)length + alignment - 1) & ~(alignment - 1);
        memset(output + length, BUSTER_LINK_PROGRAM_INT3, padded - (uint64_t)length);
        arena_pop_to(&program->bytes, (uint64_t)(output - program->bytes.base) + padded);
        const AtomId atom = link_add_atom(&program->link, BUSTER_LINK_CLASS_TEXT, BUSTER_LINK_PROGRAM_FUNCTION_ALIGNMENT_LOG2, padded, output, unit);
        // An exported function's FDE points at its anchor, defined at its start too.
        const SymbolId anchor = link_program_anchor(builder, symbol);
        result = atom != 0 && link_add_code_fixups(&program->link, atom, code->instructions, count, code->fixup_ends, code->marked, code->references,
                                                   code->reference_count) &&
                 link_define_symbol(&program->link, symbol, atom, 0, (uint64_t)length) && (anchor == 0 || link_define_symbol(&program->link, anchor, atom, 0, 0)) &&
                 link_program_keep_unwind(builder, unit, symbol, anchor != 0 ? anchor : symbol, (uint64_t)length);
        program->instructions += count;
        program->code_bytes += (uint64_t)length;
    }
    return result;
}

// _start: the stack pointer, where the kernel put argc, argv, envp and the auxiliary vector, to buster_main, on a
// 16-byte boundary (the ABI's at a call).
static bool link_program_emit_start(LinkProgramBuilder* builder)
{
    LinkProgramCode* code = builder->code;
    link_program_begin(code);
    link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_RBP, X86_REGISTER_RBP));
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RSP));
    link_program_add(code, link_program_immediate(X86_FORM_AND_RM64_IMM8, X86_REGISTER_RSP, -16));
    link_program_refer(code, link_program_bare(X86_FORM_CALL_REL32), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_CALL, builder->main, 0);
    link_program_add(code, link_program_bare(X86_FORM_UD2));
    return link_program_finish(builder, 0, builder->start);
}

// buster_mix(x): (x >> 29 ^ x) * BUSTER_LINK_PROGRAM_MIX_MULTIPLIER, xored with itself shifted right by 32
// (link_program_mix).
static bool link_program_emit_mix(LinkProgramBuilder* builder)
{
    LinkProgramCode* code = builder->code;
    link_program_begin(code);
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI));
    link_program_add(code, link_program_immediate(X86_FORM_SHR_RM64_IMM8, X86_REGISTER_RAX, 29));
    link_program_add(code, link_program_registers(X86_FORM_XOR_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI));
    X86Instruction multiply = link_program_registers(X86_FORM_IMUL_R64_RM64_IMM32, X86_REGISTER_RAX, X86_REGISTER_RAX);
    multiply.immediate = BUSTER_LINK_PROGRAM_MIX_MULTIPLIER;
    link_program_add(code, multiply);
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_RAX));
    link_program_add(code, link_program_immediate(X86_FORM_SHR_RM64_IMM8, X86_REGISTER_RCX, 32));
    link_program_add(code, link_program_registers(X86_FORM_XOR_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RCX));
    link_program_add(code, link_program_bare(X86_FORM_RET));
    return link_program_finish(builder, 0, builder->mix);
}

// buster_write_hex(x): x's 16 hexadecimal digits and a newline in buster_output, written to standard output with the
// write system call, or with libc's write through the GOT, after a push that aligns the stack for the call.
static bool link_program_emit_write_hex(LinkProgramBuilder* builder)
{
    LinkProgramCode* code = builder->code;
    link_program_begin(code);
    const uint32_t digit = link_program_label(code);
    link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_RSI), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, builder->output, 0);
    link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_RDX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, builder->first_string, 0);
    link_program_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RCX, 16));
    link_program_place(code, digit);
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI));
    link_program_add(code, link_program_immediate(X86_FORM_SHR_RM64_IMM8, X86_REGISTER_RAX, 60));
    link_program_add(code, link_program_indexed(X86_FORM_MOVZX_R32_RM8, X86_REGISTER_RAX, X86_REGISTER_RDX, X86_REGISTER_RAX, 0, 0));
    link_program_add(code, link_program_memory(X86_FORM_MOV_RM8_R8, X86_REGISTER_RAX, X86_REGISTER_RSI, 0));
    link_program_add(code, link_program_immediate(X86_FORM_SHL_RM64_IMM8, X86_REGISTER_RDI, 4));
    link_program_add(code, link_program_immediate(X86_FORM_ADD_RM64_IMM8, X86_REGISTER_RSI, 1));
    link_program_add(code, link_program_immediate(X86_FORM_SUB_RM32_IMM8, X86_REGISTER_RCX, 1));
    link_program_jump(code, X86_FORM_JNE_REL32, digit);
    X86Instruction newline = link_program_memory(X86_FORM_MOV_RM8_IMM8, 0, X86_REGISTER_RSI, 0);
    newline.immediate = '\n';
    link_program_add(code, newline);
    const bool libc = builder->plan->options.libc;
    if (libc)
    {
        link_program_add(code, link_program_immediate(X86_FORM_PUSH_R64, X86_REGISTER_RAX, 0));
    }
    link_program_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RDI, 1));
    link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_RSI), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, builder->output, 0);
    link_program_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RDX, BUSTER_LINK_PROGRAM_OUTPUT_BYTES));
    if (libc)
    {
        link_program_refer(code, link_program_rip(X86_FORM_CALL_RM64, 0), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_CALL_GOT, builder->libc_write, 0);
        link_program_add(code, link_program_immediate(X86_FORM_POP_R64, X86_REGISTER_RCX, 0));
    }
    else
    {
        link_program_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RAX, 1));
        link_program_add(code, link_program_bare(X86_FORM_SYSCALL));
    }
    link_program_add(code, link_program_bare(X86_FORM_RET));
    return link_program_finish(builder, 0, builder->write_hex);
}

// Calls each entry from the symbol `begin` to the symbol `end`, 8 bytes apart, with rbx and r12.
static void link_program_emit_hooks(LinkProgramBuilder* builder, SymbolId begin, SymbolId end)
{
    LinkProgramCode* code = builder->code;
    const uint32_t next = link_program_label(code);
    const uint32_t done = link_program_label(code);
    link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_RBX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, begin, 0);
    link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_R12), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, end, 0);
    link_program_place(code, next);
    link_program_add(code, link_program_registers(X86_FORM_CMP_R64_RM64, X86_REGISTER_RBX, X86_REGISTER_R12));
    link_program_jump(code, X86_FORM_JAE_REL32, done);
    link_program_add(code, link_program_memory(X86_FORM_CALL_RM64, 0, X86_REGISTER_RBX, 0));
    link_program_add(code, link_program_immediate(X86_FORM_ADD_RM64_IMM8, X86_REGISTER_RBX, 8));
    link_program_jump(code, X86_FORM_JMP_REL32, next);
    link_program_place(code, done);
}

// Folds rdi into the checksum in r13: r13 = buster_mix(rdi ^ r13).
static void link_program_emit_fold_value(LinkProgramBuilder* builder)
{
    LinkProgramCode* code = builder->code;
    link_program_add(code, link_program_registers(X86_FORM_XOR_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_R13));
    link_program_refer(code, link_program_bare(X86_FORM_CALL_REL32), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_CALL, builder->mix, 0);
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_R13, X86_REGISTER_RAX));
}

// Folds into the checksum in r13, with buster_mix, the 8-byte values that `count` entries of the table at `table` point
// at, each less (its index + 1) << BUSTER_LINK_PROGRAM_FAR_SHIFT if `far`; with rbx and r12.
static void link_program_emit_fold(LinkProgramBuilder* builder, SymbolId table, uint32_t count, bool far)
{
    LinkProgramCode* code = builder->code;
    const uint32_t next = link_program_label(code);
    const uint32_t done = link_program_label(code);
    link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_RBX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, table, 0);
    link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_R12, X86_REGISTER_R12));
    link_program_place(code, next);
    link_program_add(code, link_program_immediate(X86_FORM_CMP_RM64_IMM32, X86_REGISTER_R12, (int32_t)count));
    link_program_jump(code, X86_FORM_JAE_REL32, done);
    link_program_add(code, link_program_indexed(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RBX, X86_REGISTER_R12, 3, 0));
    if (far)
    {
        link_program_add(code, link_program_memory(X86_FORM_LEA_R64_M, X86_REGISTER_RCX, X86_REGISTER_R12, 1));
        link_program_add(code, link_program_immediate(X86_FORM_SHL_RM64_IMM8, X86_REGISTER_RCX, BUSTER_LINK_PROGRAM_FAR_SHIFT));
        link_program_add(code, link_program_registers(X86_FORM_SUB_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RCX));
    }
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RAX, 0));
    link_program_emit_fold_value(builder);
    link_program_add(code, link_program_immediate(X86_FORM_ADD_RM64_IMM8, X86_REGISTER_R12, 1));
    link_program_jump(code, X86_FORM_JMP_REL32, next);
    link_program_place(code, done);
}

// Folds a value through each kind that the workers and the tables don't have: a global read through its GOT slot
// with push [rip + slot] (GOTPCREL), through buster_relative, which holds the global's address less its own (PC64),
// with sizes two globals' sizes (SIZE32 in an imm32, SIZE64 in a movabs's imm64), and with absolute addresses, globals
// read through their address as an imm32 (ABS32), a disp32 (ABS32S) and a movabs's imm64 (ABS64).
static void link_program_emit_kinds(LinkProgramBuilder* builder)
{
    const LinkProgramPlan* plan = builder->plan;
    LinkProgramCode* code = builder->code;
    const uint32_t* kind = plan->kind_globals;
    const SymbolId first = builder->first_global;
    link_program_refer(code, link_program_rip(X86_FORM_PUSH_RM64, 0), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_GOTPCREL, first + kind[0], 0);
    link_program_add(code, link_program_immediate(X86_FORM_POP_R64, X86_REGISTER_RAX, 0));
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RAX, 0));
    link_program_emit_fold_value(builder);
    link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_RAX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, builder->relative, 0);
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RAX));
    link_program_add(code, link_program_memory(X86_FORM_ADD_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RAX, 0));
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RDI, 0));
    link_program_emit_fold_value(builder);
    if (plan->options.sizes)
    {
        link_program_refer(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RDI, 0), X86_FIXUP_IMMEDIATE, BUSTER_LINK_FIXUP_SIZE32, first + kind[2], 0);
        link_program_emit_fold_value(builder);
        link_program_refer(code, link_program_immediate(X86_FORM_MOV_R64_IMM64, X86_REGISTER_RDI, 0), X86_FIXUP_IMMEDIATE, BUSTER_LINK_FIXUP_SIZE64, first + kind[3], 0);
        link_program_emit_fold_value(builder);
    }
    if (plan->options.absolute)
    {
        link_program_refer(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RAX, 0), X86_FIXUP_IMMEDIATE, BUSTER_LINK_FIXUP_ABS32, first + kind[4], 0);
        link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RAX, 0));
        link_program_emit_fold_value(builder);
        link_program_refer(code, link_program_absolute(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_ABS32S, first + kind[5], 0);
        link_program_emit_fold_value(builder);
        link_program_refer(code, link_program_immediate(X86_FORM_MOV_R64_IMM64, X86_REGISTER_RAX, 0), X86_FIXUP_IMMEDIATE, BUSTER_LINK_FIXUP_ABS64, first + kind[6], 0);
        link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RAX, 0));
        link_program_emit_fold_value(builder);
    }
}

// Folds what the imports give: buster_absent's GOT slot, which holds 0, and with libc, environ (through its GOT slot)
// less main's envp in r14, strlen of the hexadecimal digits called through buster_imports, strlen's GOT slot less
// buster_imports' entry, and errno, which main stored before the workers ran.
static void link_program_emit_imports(LinkProgramBuilder* builder)
{
    LinkProgramCode* code = builder->code;
    link_program_refer(code, link_program_rip(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_LOAD_GOT, builder->absent, 0);
    link_program_emit_fold_value(builder);
    if (builder->plan->options.libc)
    {
        link_program_refer(code, link_program_rip(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_LOAD_GOT, builder->libc_environ, 0);
        link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RAX, 0));
        link_program_add(code, link_program_registers(X86_FORM_SUB_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_R14));
        link_program_emit_fold_value(builder);
        link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_RDI), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, builder->first_string, 0);
        link_program_refer(code, link_program_rip(X86_FORM_CALL_RM64, 0), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, builder->imports, 0);
        link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RAX));
        link_program_emit_fold_value(builder);
        link_program_refer(code, link_program_rip(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_LOAD_GOT, builder->libc_strlen, 0);
        link_program_refer(code, link_program_rip(X86_FORM_SUB_R64_RM64, X86_REGISTER_RDI), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, builder->imports, 0);
        link_program_emit_fold_value(builder);
        link_program_refer(code, link_program_rip(X86_FORM_CALL_RM64, 0), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_CALL_GOT, builder->libc_errno_location, 0);
        link_program_add(code, link_program_memory(X86_FORM_MOV_R32_RM32, X86_REGISTER_RDI, X86_REGISTER_RAX, 0));
        link_program_emit_fold_value(builder);
    }
}

// What the C runtime does before main, for buster_main without libc, which gets the initial stack in rdi: the TLS
// block from PT_TLS, set up as the orientation comment says, and the init functions.
static void link_program_emit_runtime(LinkProgramBuilder* builder)
{
    const LinkProgramPlan* plan = builder->plan;
    LinkProgramCode* code = builder->code;
    const uint32_t environment = link_program_label(code);
    const uint32_t auxiliary = link_program_label(code);
    const uint32_t auxiliary_done = link_program_label(code);
    const uint32_t header = link_program_label(code);
    const uint32_t not_phdr = link_program_label(code);
    const uint32_t headers_done = link_program_label(code);
    const uint32_t copy = link_program_label(code);
    const uint32_t copy_done = link_program_label(code);
    const uint32_t tls_done = link_program_label(code);

    // The auxiliary vector is after envp's null, which is after argv's: rdi + 8 * argc + 16 is envp.
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI, 0));
    link_program_add(code, link_program_indexed(X86_FORM_LEA_R64_M, X86_REGISTER_RDI, X86_REGISTER_RDI, X86_REGISTER_RAX, 3, 16));
    link_program_place(code, environment);
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI, 0));
    link_program_add(code, link_program_immediate(X86_FORM_ADD_RM64_IMM8, X86_REGISTER_RDI, 8));
    link_program_add(code, link_program_registers(X86_FORM_TEST_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RAX));
    link_program_jump(code, X86_FORM_JNE_REL32, environment);

    // r8, r9 and r10: AT_PHDR, AT_PHNUM and AT_PHENT (56, an Elf64_Phdr, unless it says otherwise).
    link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_R8, X86_REGISTER_R8));
    link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_R9, X86_REGISTER_R9));
    link_program_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_R10, 56));
    link_program_place(code, auxiliary);
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDI, 0));
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_RDI, 8));
    link_program_add(code, link_program_immediate(X86_FORM_ADD_RM64_IMM8, X86_REGISTER_RDI, 16));
    link_program_add(code, link_program_registers(X86_FORM_TEST_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RAX));
    link_program_jump(code, X86_FORM_JE_REL32, auxiliary_done);
    static const uint8_t entries[] = { 3, 5, 4 };
    static const uint8_t entry_registers[] = { X86_REGISTER_R8, X86_REGISTER_R9, X86_REGISTER_R10 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(entries); i += 1)
    {
        link_program_add(code, link_program_immediate(X86_FORM_CMP_RM64_IMM8, X86_REGISTER_RAX, entries[i]));
        link_program_add(code, link_program_registers(X86_FORM_CMOVE_R64_RM64, entry_registers[i], X86_REGISTER_RCX));
    }
    link_program_jump(code, X86_FORM_JMP_REL32, auxiliary);
    link_program_place(code, auxiliary_done);

    // The program headers, with rcx the next and rsi how many are left: rdx becomes PT_TLS's, and r11 the load bias
    // (AT_PHDR less PT_PHDR's address, or 0 without PT_PHDR).
    link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_RDX, X86_REGISTER_RDX));
    link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_R11, X86_REGISTER_R11));
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_R8));
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RSI, X86_REGISTER_R9));
    link_program_place(code, header);
    link_program_add(code, link_program_registers(X86_FORM_TEST_RM64_R64, X86_REGISTER_RSI, X86_REGISTER_RSI));
    link_program_jump(code, X86_FORM_JE_REL32, headers_done);
    link_program_add(code, link_program_memory(X86_FORM_MOV_R32_RM32, X86_REGISTER_RAX, X86_REGISTER_RCX, 0));
    link_program_add(code, link_program_immediate(X86_FORM_CMP_RM32_IMM8, X86_REGISTER_RAX, 6));
    link_program_jump(code, X86_FORM_JNE_REL32, not_phdr);
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_R11, X86_REGISTER_R8));
    link_program_add(code, link_program_memory(X86_FORM_SUB_R64_RM64, X86_REGISTER_R11, X86_REGISTER_RCX, 16));
    link_program_place(code, not_phdr);
    link_program_add(code, link_program_immediate(X86_FORM_CMP_RM32_IMM8, X86_REGISTER_RAX, 7));
    link_program_add(code, link_program_registers(X86_FORM_CMOVE_R64_RM64, X86_REGISTER_RDX, X86_REGISTER_RCX));
    link_program_add(code, link_program_registers(X86_FORM_ADD_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_R10));
    link_program_add(code, link_program_immediate(X86_FORM_SUB_RM64_IMM8, X86_REGISTER_RSI, 1));
    link_program_jump(code, X86_FORM_JMP_REL32, header);
    link_program_place(code, headers_done);
    link_program_add(code, link_program_registers(X86_FORM_TEST_RM64_R64, X86_REGISTER_RDX, X86_REGISTER_RDX));
    link_program_jump(code, X86_FORM_JE_REL32, tls_done);

    // The TLS block: rcx the alignment (at least 1), r9 the offset of the image's first byte from it (p_vaddr modulo
    // the alignment, which the block's start keeps), rax the block's start's distance below the thread pointer (p_memsz
    // plus that offset, rounded up to the alignment, less the offset, as glibc and the linkers compute it), r8 the
    // thread pointer (the area's end less the control block, aligned down), rdi the block's start; then p_filesz bytes
    // of the image (p_vaddr plus the bias), a byte at a time. The rest of the block is .bss, so already zero.
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_RDX, 48));
    link_program_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RAX, 1));
    link_program_add(code, link_program_registers(X86_FORM_TEST_RM64_R64, X86_REGISTER_RCX, X86_REGISTER_RCX));
    link_program_add(code, link_program_registers(X86_FORM_CMOVE_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_RAX));
    link_program_add(code, link_program_memory(X86_FORM_LEA_R64_M, X86_REGISTER_R10, X86_REGISTER_RCX, -1));
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_R9, X86_REGISTER_RDX, 16));
    link_program_add(code, link_program_registers(X86_FORM_AND_R64_RM64, X86_REGISTER_R9, X86_REGISTER_R10));
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RDX, 40));
    link_program_add(code, link_program_registers(X86_FORM_ADD_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R9));
    link_program_add(code, link_program_registers(X86_FORM_ADD_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R10));
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RSI, X86_REGISTER_RCX));
    link_program_add(code, link_program_immediate(X86_FORM_NEG_RM64, X86_REGISTER_RSI, 0));
    link_program_add(code, link_program_registers(X86_FORM_AND_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RSI));
    link_program_add(code, link_program_registers(X86_FORM_SUB_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R9));
    link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_R8), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, builder->tls_area,
                       plan->tls_area_bytes - BUSTER_LINK_PROGRAM_TCB_BYTES);
    link_program_add(code, link_program_registers(X86_FORM_AND_R64_RM64, X86_REGISTER_R8, X86_REGISTER_RSI));
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_R8));
    link_program_add(code, link_program_registers(X86_FORM_SUB_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RAX));
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RSI, X86_REGISTER_RDX, 16));
    link_program_add(code, link_program_registers(X86_FORM_ADD_R64_RM64, X86_REGISTER_RSI, X86_REGISTER_R11));
    link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_RDX, 32));
    link_program_place(code, copy);
    link_program_add(code, link_program_registers(X86_FORM_TEST_RM64_R64, X86_REGISTER_RCX, X86_REGISTER_RCX));
    link_program_jump(code, X86_FORM_JE_REL32, copy_done);
    link_program_add(code, link_program_memory(X86_FORM_MOV_R8_RM8, X86_REGISTER_RAX, X86_REGISTER_RSI, 0));
    link_program_add(code, link_program_memory(X86_FORM_MOV_RM8_R8, X86_REGISTER_RAX, X86_REGISTER_RDI, 0));
    link_program_add(code, link_program_immediate(X86_FORM_ADD_RM64_IMM8, X86_REGISTER_RSI, 1));
    link_program_add(code, link_program_immediate(X86_FORM_ADD_RM64_IMM8, X86_REGISTER_RDI, 1));
    link_program_add(code, link_program_immediate(X86_FORM_SUB_RM64_IMM8, X86_REGISTER_RCX, 1));
    link_program_jump(code, X86_FORM_JMP_REL32, copy);
    link_program_place(code, copy_done);
    // The thread pointer points at itself, and arch_prctl(ARCH_SET_FS, it) makes it FS's base.
    link_program_add(code, link_program_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_R8, X86_REGISTER_R8, 0));
    link_program_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RDI, 0x1002));
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RSI, X86_REGISTER_R8));
    link_program_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RAX, 158));
    link_program_add(code, link_program_bare(X86_FORM_SYSCALL));
    link_program_place(code, tls_done);

    link_program_emit_hooks(builder, builder->init_array_start, builder->init_array_end);
}

// buster_main(stack), or main(argc, argv, envp) with libc: see the orientation comment. Its five pushes align the stack
// for its calls. buster_main never returns, and main returns 0.
static bool link_program_emit_main(LinkProgramBuilder* builder)
{
    const LinkProgramPlan* plan = builder->plan;
    const bool libc = plan->options.libc;
    LinkProgramCode* code = builder->code;
    link_program_begin(code);
    const uint32_t work = link_program_label(code);
    const uint32_t work_done = link_program_label(code);
    static const uint8_t saved[] = { X86_REGISTER_RBX, X86_REGISTER_R12, X86_REGISTER_R13, X86_REGISTER_R14, X86_REGISTER_R15 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(saved); i += 1)
    {
        link_program_add(code, link_program_immediate(X86_FORM_PUSH_R64, saved[i], 0));
    }
    if (libc)
    {
        // envp in r14, and errno, which link_program_emit_imports reads back.
        link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_R14, X86_REGISTER_RDX));
        link_program_refer(code, link_program_rip(X86_FORM_CALL_RM64, 0), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_CALL_GOT, builder->libc_errno_location, 0);
        X86Instruction store = link_program_memory(X86_FORM_MOV_RM32_IMM32, 0, X86_REGISTER_RAX, 0);
        store.immediate = plan->errno_value;
        link_program_add(code, store);
    }
    else
    {
        link_program_emit_runtime(builder);
    }

    // Every worker f_i(i, 1), through buster_functions: r13 = buster_mix(f_i(i, 1) + r13).
    link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_R13, X86_REGISTER_R13));
    link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_RBX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, builder->functions, 0);
    link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_R12, X86_REGISTER_R12));
    link_program_place(code, work);
    link_program_add(code, link_program_immediate(X86_FORM_CMP_RM64_IMM32, X86_REGISTER_R12, (int32_t)plan->options.functions));
    link_program_jump(code, X86_FORM_JAE_REL32, work_done);
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_R12));
    link_program_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RSI, 1));
    link_program_add(code, link_program_indexed(X86_FORM_CALL_RM64, 0, X86_REGISTER_RBX, X86_REGISTER_R12, 3, 0));
    link_program_add(code, link_program_registers(X86_FORM_ADD_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R13));
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RAX));
    link_program_refer(code, link_program_bare(X86_FORM_CALL_REL32), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_CALL, builder->mix, 0);
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_R13, X86_REGISTER_RAX));
    link_program_add(code, link_program_immediate(X86_FORM_ADD_RM64_IMM8, X86_REGISTER_R12, 1));
    link_program_jump(code, X86_FORM_JMP_REL32, work);
    link_program_place(code, work_done);

    if (!libc)
    {
        link_program_emit_hooks(builder, builder->fini_array_start, builder->fini_array_end);
    }
    link_program_emit_fold(builder, builder->globals_table, plan->global_globals + BUSTER_LINK_PROGRAM_CONSTANT_POINTERS, false);
    link_program_emit_fold(builder, builder->far, BUSTER_LINK_PROGRAM_FAR_POINTERS, true);
    link_program_emit_kinds(builder);
    link_program_emit_imports(builder);

    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_R13));
    link_program_refer(code, link_program_bare(X86_FORM_CALL_REL32), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_CALL, builder->write_hex, 0);
    if (libc)
    {
        link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_RAX, X86_REGISTER_RAX));
        for (uint32_t i = BUSTER_ARRAY_LENGTH(saved); i != 0; i -= 1)
        {
            link_program_add(code, link_program_immediate(X86_FORM_POP_R64, saved[i - 1], 0));
        }
        link_program_add(code, link_program_bare(X86_FORM_RET));
    }
    else
    {
        // exit_group(0).
        link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_RDI, X86_REGISTER_RDI));
        link_program_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RAX, 231));
        link_program_add(code, link_program_bare(X86_FORM_SYSCALL));
        link_program_add(code, link_program_bare(X86_FORM_UD2));
    }
    return link_program_finish(builder, 0, builder->main);
}

// f_index(x, depth): see the orientation comment and link_program_model_body. rbx holds the result and r12 the depth;
// the three pushes align the stack for the calls.
static bool link_program_emit_worker(LinkProgramBuilder* builder, uint32_t index)
{
    const LinkProgramPlan* plan = builder->plan;
    const LinkProgramWorker* worker = &plan->workers[index];
    LinkProgramCode* code = builder->code;
    link_program_begin(code);
    const uint32_t done = link_program_label(code);
    link_program_add(code, link_program_immediate(X86_FORM_PUSH_R64, X86_REGISTER_RBX, 0));
    link_program_add(code, link_program_immediate(X86_FORM_PUSH_R64, X86_REGISTER_R12, 0));
    link_program_add(code, link_program_immediate(X86_FORM_PUSH_R64, X86_REGISTER_R13, 0));
    link_program_add(code, link_program_registers(X86_FORM_MOV_R32_RM32, X86_REGISTER_R12, X86_REGISTER_RSI));
    X86Instruction multiply = link_program_registers(X86_FORM_IMUL_R64_RM64_IMM32, X86_REGISTER_RBX, X86_REGISTER_RDI);
    multiply.immediate = worker->multiplier;
    link_program_add(code, multiply);
    link_program_refer(code, link_program_rip(X86_FORM_ADD_R64_RM64, X86_REGISTER_RBX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32,
                       builder->first_global + worker->read_global, 0);
    link_program_refer(code, link_program_rip(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_LOAD_GOT,
                       builder->first_global + worker->got_global, 0);
    link_program_add(code, link_program_memory(X86_FORM_XOR_R64_RM64, X86_REGISTER_RBX, X86_REGISTER_RAX, 0));
    link_program_refer(code, link_program_rip(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32, builder->constants,
                       8 * (int64_t)worker->constant);
    link_program_add(code, link_program_registers(X86_FORM_ADD_R64_RM64, X86_REGISTER_RBX, X86_REGISTER_RAX));
    // The local-exec thread-local, which a shared library reaches with initial-exec: its offset from its GOT slot.
    if (plan->options.shared)
    {
        link_program_refer(code, link_program_rip(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_GOTTPOFF,
                           builder->first_thread_local + worker->local_exec, 0);
        link_program_add(code, link_program_memory(X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS, X86_REGISTER_RAX, X86_REGISTER_RAX, 0));
    }
    else
    {
        link_program_refer(code, link_program_absolute(X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS, X86_REGISTER_RAX), X86_FIXUP_DISPLACEMENT,
                           BUSTER_LINK_FIXUP_TPOFF32, builder->first_thread_local + worker->local_exec, 0);
    }
    link_program_add(code, link_program_registers(X86_FORM_ADD_R64_RM64, X86_REGISTER_RBX, X86_REGISTER_RAX));
    link_program_refer(code, link_program_rip(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_GOTTPOFF,
                       builder->first_thread_local + worker->initial_exec, 0);
    link_program_add(code, link_program_memory(X86_FORM_ADD_R64_RM64 + BUSTER_X86_FORM_FS, X86_REGISTER_RBX, X86_REGISTER_RAX, 0));
    link_program_refer(code, link_program_rip(X86_FORM_LEA_R64_M, X86_REGISTER_RAX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32,
                       builder->first_string + worker->string, 0);
    link_program_add(code, link_program_memory(X86_FORM_MOVZX_R32_RM8, X86_REGISTER_RAX, X86_REGISTER_RAX, (int32_t)worker->string_byte));
    link_program_add(code, link_program_registers(X86_FORM_ADD_R64_RM64, X86_REGISTER_RBX, X86_REGISTER_RAX));
    for (uint32_t i = 0; i < worker->filler_count; i += 1)
    {
        const LinkProgramFiller* filler = &plan->fillers[worker->filler_begin + i];
        static const uint16_t forms[LINK_PROGRAM_OPERATIONS] = {
            [LINK_PROGRAM_ADD] = X86_FORM_ADD_RM64_IMM32,
            [LINK_PROGRAM_XOR] = X86_FORM_XOR_RM64_IMM32,
            [LINK_PROGRAM_ROTATE] = X86_FORM_ROL_RM64_IMM8,
            [LINK_PROGRAM_MULTIPLY] = X86_FORM_IMUL_R64_RM64_IMM32,
        };
        X86Instruction instruction = link_program_immediate(forms[filler->operation], X86_REGISTER_RBX, filler->operand);
        instruction.reg = filler->operation == LINK_PROGRAM_MULTIPLY ? X86_REGISTER_RBX : 0;
        link_program_add(code, instruction);
    }
    link_program_refer(code, link_program_rip(X86_FORM_MOV_RM64_R64, X86_REGISTER_RBX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32,
                       builder->first_global + worker->write_global, 0);
    if (plan->options.shared)
    {
        link_program_refer(code, link_program_rip(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_GOTTPOFF,
                           builder->first_thread_local + worker->local_exec, 0);
        link_program_add(code, link_program_memory(X86_FORM_ADD_RM64_R64 + BUSTER_X86_FORM_FS, X86_REGISTER_RBX, X86_REGISTER_RAX, 0));
    }
    else
    {
        link_program_refer(code, link_program_absolute(X86_FORM_ADD_RM64_R64 + BUSTER_X86_FORM_FS, X86_REGISTER_RBX), X86_FIXUP_DISPLACEMENT,
                           BUSTER_LINK_FIXUP_TPOFF32, builder->first_thread_local + worker->local_exec, 0);
    }
    link_program_add(code, link_program_registers(X86_FORM_TEST_RM32_R32, X86_REGISTER_R12, X86_REGISTER_R12));
    link_program_jump(code, X86_FORM_JE_REL32, done);
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RBX));
    link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_RSI, X86_REGISTER_RSI));
    link_program_refer(code, link_program_bare(X86_FORM_CALL_REL32), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_CALL, builder->first_worker + worker->callee, 0);
    link_program_add(code, link_program_registers(X86_FORM_ADD_R64_RM64, X86_REGISTER_RBX, X86_REGISTER_RAX));
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RBX));
    link_program_add(code, link_program_registers(X86_FORM_XOR_R32_RM32, X86_REGISTER_RSI, X86_REGISTER_RSI));
    link_program_refer(code, link_program_rip(X86_FORM_CALL_RM64, 0), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_CALL_GOT, builder->first_worker + worker->got_callee,
                       0);
    link_program_add(code, link_program_registers(X86_FORM_XOR_R64_RM64, X86_REGISTER_RBX, X86_REGISTER_RAX));
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_RBX));
    link_program_refer(code, link_program_bare(X86_FORM_CALL_REL32), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_CALL, builder->mix, 0);
    link_program_add(code, link_program_registers(X86_FORM_ADD_R64_RM64, X86_REGISTER_RBX, X86_REGISTER_RAX));
    link_program_place(code, done);
    link_program_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RBX));
    link_program_add(code, link_program_immediate(X86_FORM_POP_R64, X86_REGISTER_R13, 0));
    link_program_add(code, link_program_immediate(X86_FORM_POP_R64, X86_REGISTER_R12, 0));
    link_program_add(code, link_program_immediate(X86_FORM_POP_R64, X86_REGISTER_RBX, 0));
    link_program_add(code, link_program_bare(X86_FORM_RET));
    return link_program_finish(builder, index % plan->options.units, builder->first_worker + index);
}

// init_N or fini_N (hook `index` of the plan's): adds its constant to its global.
static bool link_program_emit_hook(LinkProgramBuilder* builder, uint32_t index, uint32_t unit)
{
    const LinkProgramHook* hook = &builder->plan->hooks[index];
    LinkProgramCode* code = builder->code;
    link_program_begin(code);
    link_program_add(code, link_program_immediate(X86_FORM_MOV_RM64_IMM32, X86_REGISTER_RAX, hook->addend));
    link_program_refer(code, link_program_rip(X86_FORM_ADD_RM64_R64, X86_REGISTER_RAX), X86_FIXUP_DISPLACEMENT, BUSTER_LINK_FIXUP_PC32,
                       builder->first_global + hook->global, 0);
    link_program_add(code, link_program_bare(X86_FORM_RET));
    return link_program_finish(builder, unit, builder->first_hook + index);
}

// ---------------------------------------------------------------------------------------------------------------------
// The data

// A data atom of `unit` with `size` bytes: zero-fill in a zero-fill class, or else a copy of `bytes` (zero without them)
// in the bytes arena. 0 if the arena or the representation refuses it.
static AtomId link_program_add_data(LinkProgramBuilder* builder, uint8_t class, uint8_t alignment_log2, uint64_t size, const uint8_t* bytes, uint32_t unit)
{
    LinkProgram* program = builder->program;
    uint8_t* copy = 0;
    if (!link_class_is_zero_fill(class))
    {
        copy = arena_push(&program->bytes, size, 1ull << alignment_log2);
        if (copy != 0 && bytes != 0)
        {
            memcpy(copy, bytes, size);
        }
        else if (copy != 0)
        {
            memset(copy, 0, size);
        }
    }
    AtomId result = 0;
    if (!link_class_is_zero_fill(class) && copy == 0)
    {
        program->problem = "out of memory for the data";
    }
    else
    {
        result = link_add_atom(&program->link, class, alignment_log2, size, copy, unit);
    }
    return result;
}

// A table of `count` pointers (ABS64 fixups) as an atom of `class` in `unit`: entry i points at targets[i] plus
// addends[i] (or plus 0 without addends).
static AtomId link_program_add_pointers(LinkProgramBuilder* builder, uint8_t class, uint32_t unit, const SymbolId* targets, const int64_t* addends, uint32_t count)
{
    Link* link = &builder->program->link;
    const AtomId result = link_program_add_data(builder, class, 3, 8 * (uint64_t)count, 0, unit);
    bool added = result != 0;
    for (uint32_t i = 0; added && i < count; i += 1)
    {
        added = link_add_fixup(link, result, 8 * i, BUSTER_LINK_FIXUP_ABS64, targets[i], addends != 0 ? addends[i] : 0) != 0;
    }
    return added ? result : 0;
}

// A global or a thread-local: its value in its first 8 bytes, or zero-fill.
static bool link_program_emit_variable(LinkProgramBuilder* builder, const LinkProgramVariable* variable, bool tls, uint32_t unit, SymbolId symbol)
{
    const uint64_t size = 1ull << variable->alignment_log2;
    uint8_t bytes[16] = { 0 };
    memcpy(bytes, &variable->value, sizeof(variable->value));
    const uint8_t class = variable->value == 0 ? (tls ? BUSTER_LINK_CLASS_TBSS : BUSTER_LINK_CLASS_BSS) : (tls ? BUSTER_LINK_CLASS_TDATA : BUSTER_LINK_CLASS_DATA);
    const AtomId atom = link_program_add_data(builder, class, variable->alignment_log2, size, bytes, unit);
    return atom != 0 && link_define_symbol(&builder->program->link, symbol, atom, 0, size);
}

static bool link_program_emit_data(LinkProgramBuilder* builder, Arena* scratch)
{
    const LinkProgramPlan* plan = builder->plan;
    const LinkProgramOptions* options = &plan->options;
    Link* link = &builder->program->link;
    const uint32_t units = options->units;

    const AtomId constants = link_program_add_data(builder, BUSTER_LINK_CLASS_RODATA, 4, sizeof(plan->constants), (const uint8_t*)plan->constants, 0);
    bool result = constants != 0 && link_define_symbol(link, builder->constants, constants, 0, sizeof(plan->constants));
    for (uint32_t i = 0; result && i < options->globals; i += 1)
    {
        result = link_program_emit_variable(builder, &plan->globals[i], false, i % units, builder->first_global + i);
    }
    for (uint32_t i = 0; result && i < options->thread_locals; i += 1)
    {
        result = link_program_emit_variable(builder, &plan->thread_locals[i], true, i % units, builder->first_thread_local + i);
    }
    for (uint32_t unit = 0; result && unit < units; unit += 1)
    {
        const AtomId pool = link_program_add_data(builder, BUSTER_LINK_CLASS_STRINGS, 0, plan->pool_lengths[unit], plan->pools[unit], unit);
        const uint32_t first = unit == 0 ? 0 : 1 + unit * options->strings;
        const uint32_t last = 1 + (unit + 1) * options->strings;
        result = pool != 0;
        for (uint32_t string = first; result && string < last; string += 1)
        {
            result = link_define_symbol(link, builder->first_string + string, pool, plan->string_offsets[string], plan->string_lengths[string] + 1u);
        }
    }

    // buster_functions, buster_globals and buster_far, whose targets and addends are in scratch memory with room for the
    // longest table (the INIT_ARRAY and FINI_ARRAY atoms' below among them).
    uint32_t table_count = plan->global_globals + BUSTER_LINK_PROGRAM_CONSTANT_POINTERS;
    table_count = options->functions > table_count ? options->functions : table_count;
    table_count = options->init_functions > table_count ? options->init_functions : table_count;
    table_count = options->fini_functions > table_count ? options->fini_functions : table_count;
    SymbolId* targets = BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, table_count);
    int64_t* addends = BUSTER_ARENA_PUSH_ARRAY(scratch, int64_t, table_count);
    result = result && targets != 0 && addends != 0;
    if (result)
    {
        for (uint32_t i = 0; i < options->functions; i += 1)
        {
            targets[i] = builder->first_worker + i;
        }
        const AtomId functions = link_program_add_pointers(builder, BUSTER_LINK_CLASS_DATA_RELRO, 0, targets, 0, options->functions);
        result = functions != 0 && link_define_symbol(link, builder->functions, functions, 0, 8 * (uint64_t)options->functions);
    }
    if (result)
    {
        uint32_t count = 0;
        for (uint32_t i = 0; i < options->globals; i += 1)
        {
            if (!plan->globals[i].local)
            {
                targets[count] = builder->first_global + i;
                addends[count] = 0;
                count += 1;
            }
        }
        for (uint32_t i = 0; i < BUSTER_LINK_PROGRAM_CONSTANT_POINTERS; i += 1)
        {
            targets[count] = builder->constants;
            addends[count] = 8 * (int64_t)plan->constant_pointers[i];
            count += 1;
        }
        const AtomId globals = link_program_add_pointers(builder, BUSTER_LINK_CLASS_DATA_RELRO, 0, targets, addends, count);
        result = globals != 0 && link_define_symbol(link, builder->globals_table, globals, 0, 8 * (uint64_t)count);
    }
    if (result)
    {
        for (uint32_t i = 0; i < BUSTER_LINK_PROGRAM_FAR_POINTERS; i += 1)
        {
            targets[i] = builder->first_global + plan->far_globals[i];
            addends[i] = (int64_t)(i + 1) << BUSTER_LINK_PROGRAM_FAR_SHIFT;
        }
        const AtomId far = link_program_add_pointers(builder, BUSTER_LINK_CLASS_DATA, 0, targets, addends, BUSTER_LINK_PROGRAM_FAR_POINTERS);
        result = far != 0 && link_define_symbol(link, builder->far, far, 0, 8 * BUSTER_LINK_PROGRAM_FAR_POINTERS);
    }
    if (result)
    {
        const AtomId relative = link_program_add_data(builder, BUSTER_LINK_CLASS_DATA_RELRO, 3, 8, 0, 0);
        result = relative != 0 && link_add_fixup(link, relative, 0, BUSTER_LINK_FIXUP_PC64, builder->first_global + plan->kind_globals[1], 0) != 0 &&
                 link_define_symbol(link, builder->relative, relative, 0, 8);
    }
    if (result && options->libc)
    {
        const AtomId imports = link_program_add_pointers(builder, BUSTER_LINK_CLASS_DATA_RELRO, 0, &builder->libc_strlen, 0, 1);
        result = imports != 0 && link_define_symbol(link, builder->imports, imports, 0, 8);
    }

    // Each unit's INIT_ARRAY and FINI_ARRAY atoms, listing its init and fini functions in order.
    for (uint32_t list = 0; result && list < 2; list += 1)
    {
        const uint32_t first = list == 0 ? 0 : options->init_functions;
        const uint32_t count = list == 0 ? options->init_functions : options->fini_functions;
        for (uint32_t unit = 0; result && unit < units && unit < count; unit += 1)
        {
            uint32_t entries = 0;
            for (uint32_t i = unit; i < count; i += units)
            {
                targets[entries] = builder->first_hook + first + i;
                entries += 1;
            }
            const uint8_t class = list == 0 ? BUSTER_LINK_CLASS_INIT_ARRAY : BUSTER_LINK_CLASS_FINI_ARRAY;
            result = link_program_add_pointers(builder, class, unit, targets, 0, entries) != 0;
        }
    }

    if (result && !options->libc)
    {
        const AtomId tls_area = link_program_add_data(builder, BUSTER_LINK_CLASS_BSS, 6, plan->tls_area_bytes, 0, 0);
        result = tls_area != 0 && link_define_symbol(link, builder->tls_area, tls_area, 0, plan->tls_area_bytes);
    }
    if (result)
    {
        const AtomId output = link_program_add_data(builder, BUSTER_LINK_CLASS_BSS, 3, 24, 0, 0);
        result = output != 0 && link_define_symbol(link, builder->output, output, 0, 24);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The unwind tables

// An FDE's bytes: its header, its instructions and DW_CFA_nops to a multiple of 4.
static uint32_t link_program_fde_bytes(const LinkProgramUnwind* unwind)
{
    return (BUSTER_LINK_PROGRAM_FDE_HEADER_BYTES + unwind->instruction_bytes + 3) & ~3u;
}

// The units' .eh_frame atoms, after every other atom (see BUSTER_LINK_PROGRAM_CIE_BYTES): each unit's FDEs in the order
// its functions were added, found by a counting sort of the functions by unit. A unit without functions has none.
static bool link_program_emit_unwind(LinkProgramBuilder* builder)
{
    LinkProgram* program = builder->program;
    Link* link = &program->link;
    const uint32_t units = builder->plan->options.units;
    const uint32_t count = builder->unwind_count;
    const uint64_t mark = program->work.length;
    // Unit u's functions at order[start[u]] to order[start[u + 1] - 1]: counted in start[u + 1], whose exclusive sum is
    // then where u's go; each placed moves it on by one, so that it ends as u + 1's start.
    uint32_t* start = BUSTER_ARENA_PUSH_ARRAY(&program->work, uint32_t, (uint64_t)units + 1);
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(&program->work, uint32_t, (uint64_t)count + 1);
    bool result = start != 0 && order != 0;
    if (result)
    {
        memset(start, 0, ((uint64_t)units + 1) * sizeof(uint32_t));
        for (uint32_t f = 0; f < count; f += 1)
        {
            start[builder->unwind[f].unit + 1] += 1;
        }
        uint32_t sum = 0;
        for (uint32_t u = 1; u <= units; u += 1)
        {
            const uint32_t here = start[u];
            start[u] = sum;
            sum += here;
        }
        for (uint32_t f = 0; f < count; f += 1)
        {
            const uint32_t place = start[builder->unwind[f].unit + 1];
            start[builder->unwind[f].unit + 1] = place + 1;
            order[place] = f;
        }
    }
    for (uint32_t unit = 0; result && unit < units; unit += 1)
    {
        uint64_t size = BUSTER_LINK_PROGRAM_CIE_BYTES;
        for (uint32_t f = start[unit]; f < start[unit + 1]; f += 1)
        {
            size += link_program_fde_bytes(&builder->unwind[order[f]]);
        }
        const bool functions = start[unit + 1] != start[unit];
        uint8_t* bytes = functions ? arena_push(&program->bytes, size, 8) : 0;
        result = !functions || bytes != 0;
        program->problem = result ? program->problem : "out of memory for the unwind tables";
        AtomId atom = 0;
        if (bytes != 0)
        {
            memcpy(bytes, link_program_cie, BUSTER_LINK_PROGRAM_CIE_BYTES);
            uint32_t fde = BUSTER_LINK_PROGRAM_CIE_BYTES;
            for (uint32_t f = start[unit]; f < start[unit + 1]; f += 1)
            {
                const LinkProgramUnwind* unwind = &builder->unwind[order[f]];
                const uint32_t fde_bytes = link_program_fde_bytes(unwind);
                const uint32_t fields[4] = {
                    // The length past this field, the distance back to the CIE from the next field, pc_begin (the
                    // fixup's) and the range.
                    fde_bytes - 4,
                    fde + 4,
                    0,
                    unwind->length,
                };
                memcpy(bytes + fde, fields, sizeof(fields));
                bytes[fde + 16] = 0;
                memcpy(bytes + fde + BUSTER_LINK_PROGRAM_FDE_HEADER_BYTES, unwind->instructions, unwind->instruction_bytes);
                memset(bytes + fde + BUSTER_LINK_PROGRAM_FDE_HEADER_BYTES + unwind->instruction_bytes, 0,
                       fde_bytes - BUSTER_LINK_PROGRAM_FDE_HEADER_BYTES - unwind->instruction_bytes);
                fde += fde_bytes;
            }
            atom = link_add_atom(link, BUSTER_LINK_CLASS_EH_FRAME, 3, size, bytes, unit);
            result = atom != 0;
        }
        uint32_t fde = BUSTER_LINK_PROGRAM_CIE_BYTES;
        for (uint32_t f = start[unit]; result && atom != 0 && f < start[unit + 1]; f += 1)
        {
            const LinkProgramUnwind* unwind = &builder->unwind[order[f]];
            result = link_add_fixup(link, atom, fde + 8, BUSTER_LINK_FIXUP_PC32, unwind->symbol, 0) != 0;
            fde += link_program_fde_bytes(unwind);
        }
    }
    arena_pop_to(&program->work, mark);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Generating

// The names of the program's symbols that have one: a prefix and a number, or a fixed name.
static NameId link_program_name(Link* link, const char* prefix, uint32_t number, bool numbered)
{
    char buffer[32];
    uint32_t length = 0;
    while (prefix[length] != 0)
    {
        buffer[length] = prefix[length];
        length += 1;
    }
    if (numbered)
    {
        char digits[10];
        uint32_t digit_count = 0;
        do
        {
            digits[digit_count] = (char)('0' + number % 10);
            number /= 10;
            digit_count += 1;
        } while (number != 0);
        while (digit_count != 0)
        {
            digit_count -= 1;
            buffer[length] = digits[digit_count];
            length += 1;
        }
    }
    return link_add_name(link, (const uint8_t*)buffer, length);
}

// A symbol without its atom yet (or ever, for the linker's), named prefix and number unless the prefix is null.
static SymbolId link_program_symbol(Link* link, const char* prefix, uint32_t number, bool numbered, uint8_t flags, uint8_t source)
{
    const NameId name = prefix != 0 ? link_program_name(link, prefix, number, numbered) : 0;
    return prefix == 0 || name != 0 ? link_add_symbol(link, name, 0, 0, 0, flags, source) : 0;
}

// Adds every symbol, before any code refers to one: the fixed ones, then each kind's in order.
static bool link_program_add_symbols(LinkProgramBuilder* builder)
{
    const LinkProgramPlan* plan = builder->plan;
    const LinkProgramOptions* options = &plan->options;
    Link* link = &builder->program->link;
    const uint8_t program = BUSTER_LINK_SOURCE_PROGRAM;
    const uint8_t global_function = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION;
    const uint8_t local_function = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION;
    const uint8_t global_object = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT;
    const uint8_t local_object = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT;
    const uint8_t linker = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE | BUSTER_LINK_SYMBOL_HIDDEN;
    // Undefined symbols have no type, as in a compiler's objects.
    const uint8_t import = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE;
    const uint8_t runtime = BUSTER_LINK_SOURCE_RUNTIME_SHARED;
    const bool libc = options->libc;
    // A shared library's data, which its code reaches RIP-relatively, is hidden (see the orientation comment).
    const uint8_t hidden = options->shared ? BUSTER_LINK_SYMBOL_HIDDEN : 0;
    if (options->shared)
    {
        builder->main = link_program_symbol(link, "buster_library_main", 0, false, global_function, program);
    }
    else if (libc)
    {
        builder->main = link_program_symbol(link, "main", 0, false, global_function, program);
    }
    else
    {
        builder->start = link_program_symbol(link, "_start", 0, false, global_function, program);
        builder->main = link_program_symbol(link, "buster_main", 0, false, local_function, program);
    }
    builder->mix = link_program_symbol(link, "buster_mix", 0, false, global_function, program);
    builder->write_hex = link_program_symbol(link, "buster_write_hex", 0, false, local_function, program);
    builder->constants = link_program_symbol(link, "buster_constants", 0, false, global_object | hidden, program);
    builder->functions = link_program_symbol(link, "buster_functions", 0, false, local_object, program);
    builder->globals_table = link_program_symbol(link, "buster_globals", 0, false, local_object, program);
    builder->far = link_program_symbol(link, "buster_far", 0, false, local_object, program);
    builder->relative = link_program_symbol(link, "buster_relative", 0, false, local_object, program);
    builder->output = link_program_symbol(link, "buster_output", 0, false, local_object, program);
    builder->absent = link_program_symbol(link, "buster_absent", 0, false, BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_NONE);
    if (libc)
    {
        builder->imports = link_program_symbol(link, "buster_imports", 0, false, local_object, program);
        builder->libc_write = link_program_symbol(link, "write", 0, false, import, runtime);
        builder->libc_strlen = link_program_symbol(link, "strlen", 0, false, import, runtime);
        builder->libc_environ = link_program_symbol(link, "environ", 0, false, import, runtime);
        builder->libc_errno_location = link_program_symbol(link, "__errno_location", 0, false, import, runtime);
    }
    else
    {
        builder->tls_area = link_program_symbol(link, "buster_tls_area", 0, false, local_object, program);
        builder->init_array_start = link_program_symbol(link, "__init_array_start", 0, false, linker, BUSTER_LINK_SOURCE_LINKER);
        builder->init_array_end = link_program_symbol(link, "__init_array_end", 0, false, linker, BUSTER_LINK_SOURCE_LINKER);
        builder->fini_array_start = link_program_symbol(link, "__fini_array_start", 0, false, linker, BUSTER_LINK_SOURCE_LINKER);
        builder->fini_array_end = link_program_symbol(link, "__fini_array_end", 0, false, linker, BUSTER_LINK_SOURCE_LINKER);
    }
    builder->first_worker = link->symbols.count;
    for (uint32_t i = 0; i < options->functions; i += 1)
    {
        link_program_symbol(link, "f_", i, true, global_function, program);
    }
    builder->first_global = link->symbols.count;
    for (uint32_t i = 0; i < options->globals; i += 1)
    {
        link_program_symbol(link, "g_", i, true, plan->globals[i].local ? local_object : global_object | hidden, program);
    }
    builder->first_thread_local = link->symbols.count;
    for (uint32_t i = 0; i < options->thread_locals; i += 1)
    {
        const uint8_t binding = plan->thread_locals[i].local ? BUSTER_LINK_SYMBOL_LOCAL : BUSTER_LINK_SYMBOL_GLOBAL | hidden;
        link_program_symbol(link, "t_", i, true, binding | BUSTER_LINK_SYMBOL_TLS, program);
    }
    builder->first_hook = link->symbols.count;
    for (uint32_t i = 0; i < options->init_functions; i += 1)
    {
        link_program_symbol(link, "init_", i, true, local_function, program);
    }
    for (uint32_t i = 0; i < options->fini_functions; i += 1)
    {
        link_program_symbol(link, "fini_", i, true, local_function, program);
    }
    builder->first_string = link->symbols.count;
    for (uint32_t i = 0; i < plan->string_count; i += 1)
    {
        link_program_symbol(link, 0, 0, false, local_object, program);
    }
    builder->first_anchor = link->symbols.count;
    for (uint32_t i = 0; options->shared && i < options->functions + 2; i += 1)
    {
        link_program_symbol(link, 0, 0, false, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_NOTYPE, program);
    }
    return link->error == LINK_ERROR_NONE;
}

// The representation's room: the exact count of wide addends, and bounds on the others (the 16 fixed names, which
// either kind of program has at most, the hexadecimal digits' symbol, and the helpers' fixups).
static LinkCapacity link_program_capacity(LinkProgramOptions options)
{
    const uint32_t hooks = options.init_functions + options.fini_functions;
    const uint32_t variables = options.globals + options.thread_locals;
    LinkCapacity result = {
        .names = options.functions + variables + hooks + 16,
        .atoms = options.functions + variables + hooks + 4 * options.units + 11,
        .symbols = options.functions + variables + hooks + options.units * options.strings + 17 + (options.shared ? options.functions + 2 : 0),
        .fixups = 13 * options.functions + options.globals + 3 * hooks + 68,
        .wide_addends = BUSTER_LINK_PROGRAM_FAR_POINTERS,
    };
    return result;
}

// A bound on the atoms' bytes: each function's at most 15 bytes an instruction and its padding, the room x86_encode
// needs past the last one, and the data's, with each atom's alignment.
static uint64_t link_program_bytes(LinkProgramOptions options)
{
    const uint64_t hooks = (uint64_t)options.init_functions + options.fini_functions;
    const uint64_t variables = (uint64_t)options.globals + options.thread_locals;
    const uint64_t worker = (40 + (uint64_t)options.filler) * BUSTER_X86_MAX_INSTRUCTION_BYTES + 16;
    const uint64_t helpers = 4 * (BUSTER_LINK_PROGRAM_MAX_INSTRUCTIONS * BUSTER_X86_MAX_INSTRUCTION_BYTES + 16) + BUSTER_X86_ENCODER_CAPACITY(BUSTER_LINK_PROGRAM_MAX_INSTRUCTIONS);
    const uint64_t code = (uint64_t)options.functions * worker + hooks * (4 * BUSTER_X86_MAX_INSTRUCTION_BYTES + 16) + helpers;
    const uint64_t pools = (uint64_t)options.units * ((uint64_t)options.strings * (BUSTER_LINK_PROGRAM_MAX_STRING + 1) + 32);
    const uint64_t tables = 8 * ((uint64_t)options.functions + options.globals + hooks + BUSTER_LINK_PROGRAM_CONSTANT_POINTERS + BUSTER_LINK_PROGRAM_FAR_POINTERS);
    const uint64_t data = 32 * variables + pools + tables + 2 * (uint64_t)options.units * 8 + 1024;
    const uint64_t unwind = (uint64_t)options.units * (BUSTER_LINK_PROGRAM_CIE_BYTES + 8) + ((uint64_t)options.functions + hooks + 4) * BUSTER_LINK_PROGRAM_MAX_FDE_BYTES;
    return code + data + unwind;
}

static LinkProgramOptions link_program_default_options(void)
{
    LinkProgramOptions result = {
        .seed = 1,
        .units = 4,
        .functions = 64,
        .globals = 32,
        .thread_locals = 8,
        .strings = 8,
        .init_functions = 4,
        .fini_functions = 2,
        .filler = 8,
        .absolute = true,
        .sizes = true,
        .libc = false,
    };
    return result;
}

// The work arena's capacity: the plan's arrays, which grow with the counts, and the code being built.
#define BUSTER_LINK_PROGRAM_WORK_BYTES (16ull << 30)

static bool link_program_generate_begin(LinkProgram* program, LinkProgramOptions options, LinkCapacity extra, uint64_t extra_name_bytes, uint64_t extra_bytes)
{
    *program = (LinkProgram){
        .options = options,
    };
    const uint32_t maximum = BUSTER_LINK_PROGRAM_MAX_COUNT;
    const bool valid = options.units >= 1 && options.units <= maximum && options.functions >= 1 && options.functions <= maximum && options.globals >= 1 &&
                       options.globals <= maximum && options.thread_locals >= 1 && options.thread_locals <= maximum && options.strings >= 1 &&
                       options.strings <= maximum && (uint64_t)options.units * options.strings <= maximum && options.init_functions <= maximum &&
                       options.fini_functions <= maximum && options.filler <= BUSTER_LINK_PROGRAM_MAX_FILLER &&
                       (!options.shared || (options.libc && !options.absolute));
    // Room for the extra records: each table's total stays below UINT32_MAX, which link_initialize refuses.
    const LinkCapacity own = link_program_capacity(options);
    const bool extra_fits = extra.names < UINT32_MAX - own.names && extra.atoms < UINT32_MAX - own.atoms && extra.symbols < UINT32_MAX - own.symbols &&
                            extra.fixups < UINT32_MAX - own.fixups && extra.wide_addends < UINT32_MAX - own.wide_addends && extra_name_bytes <= (1ull << 40) &&
                            extra_bytes <= (1ull << 40);
    bool result = false;
    if (!valid || !extra_fits)
    {
        program->problem = "the options are out of range";
    }
    else
    {
        const LinkCapacity capacity = {
            .names = own.names + extra.names,
            .atoms = own.atoms + extra.atoms,
            .symbols = own.symbols + extra.symbols,
            .fixups = own.fixups + extra.fixups,
            .wide_addends = own.wide_addends + extra.wide_addends,
        };
        program->tables = arena_reserve(link_capacity_bytes(capacity));
        program->names = arena_reserve(32 * ((uint64_t)own.names + 1) + extra_name_bytes);
        program->bytes = arena_reserve(link_program_bytes(options) + extra_bytes);
        program->work = arena_reserve(BUSTER_LINK_PROGRAM_WORK_BYTES);
        result = link_initialize(&program->link, &program->tables, &program->names, capacity) && program->names.base != 0 && program->bytes.base != 0 &&
                 program->work.base != 0;
        if (!result)
        {
            program->problem = "out of memory for the representation";
        }
    }
    if (result && !x86_encoder_initialized)
    {
        result = x86_encoder_initialize();
        program->problem = result ? 0 : "the encoder's templates can't be built";
    }
    if (result)
    {
        program->plan = BUSTER_ARENA_PUSH_ARRAY(&program->work, LinkProgramPlan, 1);
        program->builder = BUSTER_ARENA_PUSH_ARRAY(&program->work, LinkProgramBuilder, 1);
        LinkProgramCode* code = BUSTER_ARENA_PUSH_ARRAY(&program->work, LinkProgramCode, 1);
        result = program->plan != 0 && program->builder != 0 && code != 0 && link_program_plan(program->plan, &program->work, options);
        program->problem = result ? 0 : "out of memory for the plan";
        // An FDE for every function: the workers, the init and fini functions, and _start, main, buster_mix and
        // buster_write_hex.
        const uint32_t functions = options.functions + options.init_functions + options.fini_functions + 4;
        LinkProgramUnwind* unwind = result ? BUSTER_ARENA_PUSH_ARRAY(&program->work, LinkProgramUnwind, functions) : 0;
        result = result && unwind != 0;
        program->problem = result ? 0 : "out of memory for the plan";
        if (result)
        {
            *program->builder = (LinkProgramBuilder){
                .program = program,
                .plan = program->plan,
                .code = code,
                .unwind = unwind,
                .unwind_capacity = functions,
            };
        }
    }
    result = result && link_program_add_symbols(program->builder);
    if (!result && program->problem == 0)
    {
        program->problem = "the representation refused a record";
    }
    // The steps after a failed one fail too.
    program->builder = result ? program->builder : 0;
    return result;
}

static bool link_program_generate_atoms(LinkProgram* program)
{
    LinkProgramBuilder* builder = program->builder;
    const LinkProgramOptions options = program->options;
    bool result = builder != 0;
    // The functions unit by unit, as a compiler's objects have them: unit 0's helpers, then each unit's workers and its
    // init and fini functions, in their numbers' order. So the functions' addresses go in the order of their FDEs in
    // the units' .eh_frame, as in a link of a compiler's objects.
    result = result && (options.libc || link_program_emit_start(builder)) && link_program_emit_main(builder) && link_program_emit_mix(builder) &&
             link_program_emit_write_hex(builder);
    for (uint32_t unit = 0; result && unit < options.units; unit += 1)
    {
        for (uint32_t i = unit; result && i < options.functions; i += options.units)
        {
            result = link_program_emit_worker(builder, i);
        }
        for (uint32_t number = unit; result && number < options.init_functions; number += options.units)
        {
            result = link_program_emit_hook(builder, number, unit);
        }
        for (uint32_t number = unit; result && number < options.fini_functions; number += options.units)
        {
            result = link_program_emit_hook(builder, options.init_functions + number, unit);
        }
    }
    result = result && link_program_emit_data(builder, &program->work) && link_program_emit_unwind(builder);
    if (builder != 0 && !result && program->problem == 0)
    {
        program->problem = "the representation refused a record";
    }
    program->builder = result ? builder : 0;
    return result;
}

static bool link_program_generate_end(LinkProgram* program)
{
    LinkProgramBuilder* builder = program->builder;
    bool result = builder != 0;
    if (result)
    {
        result = link_program_run_model(program->plan, &program->work, program->output);
        program->problem = result ? 0 : "out of memory for the model";
    }
    if (result)
    {
        program->entry = program->options.libc ? builder->main : builder->start;
        result = link_check(&program->link) == LINK_ERROR_NONE;
        program->problem = result ? 0 : "the representation refused a record";
    }
    program->builder = 0;
    return result;
}

static bool link_program_generate(LinkProgram* program, LinkProgramOptions options)
{
    const LinkCapacity none = { 0 };
    const bool begun = link_program_generate_begin(program, options, none, 0, 0);
    const bool emitted = begun && link_program_generate_atoms(program);
    return emitted && link_program_generate_end(program);
}

static void link_program_release(LinkProgram* program)
{
    arena_release(&program->tables);
    arena_release(&program->names);
    arena_release(&program->bytes);
    arena_release(&program->work);
    *program = (LinkProgram){ 0 };
}
