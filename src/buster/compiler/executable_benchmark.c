// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The direct path's executable benchmark: what executable_write costs on generated programs, phase by phase, against
// the file write's floor; and the kernels it rests on: the patch kernel and the variants measured against it, and the
// build ID's chunk hash.
//
// Usage: build/compiler benchmark-executable [--directory PATH] [--functions LIST] [--lanes N] [--seed N]
//                                            [--only kernels|hash|links|dynamic|static]
//
// Run it under the 9700X's lock pinned to one CPU (taskset -c 2 ...), or with --lanes N to N CPUs, one per core
// (taskset -c 0-7 for 8), since the lanes' threads keep the affinity they start with. Each figure is a median:
//   - Kernels: S + A - P into PC32 fields, on the document's three sizes (64 KB of code with 2,000 fixups and 10,000
//     targets, 1 MB with 30,000 and 100,000, 64 MB with 2 million and 1 million: fields sorted, one per stride at a random
//     place in it, random targets), for executable_patch_pc32 and the variants: a branch on the kind per fixup, the
//     checked path (executable_patch_other), 64-bit targets, and AVX-512 gathers of 8 64-bit or 16 32-bit targets with
//     scalar stores or scatters. Every variant's bytes are checked against executable_patch_pc32's first.
//   - Hash: the chunk hash of 256 KB in L2, whole and in pieces of 4 KB and of 100 bytes, and of 64 MB.
//   - Links: for each --functions (executable_benchmark_functions by default: about the sizes of hello world, the test
//     defaults, tcc and the document's 46 MB synth), a generated program written into --directory (TMPDIR or /tmp by
//     default) by a group of --lanes lanes (1 by default), which runs every write of a case, as a compiler runs its
//     phases on one group: over the file from the previous run (in place), and as a new file (removed before each run,
//     outside the time); each phase of executable_write, and the total; then the floor, a pwrite of as many bytes from
//     memory, in place and new; and with one lane, executable_patch_range over the program's atoms, with every kind in
//     its mix, and the unwind tables' passes (executable_benchmark_unwind). Then the same sizes as programs that call
//     libc, bound against the system's runtime (runtime.c and bind.c) and written as dynamic PIEs, the same way (--only
//     dynamic for these alone); and bound against its static runtime (runtime_import_static) and written as static
//     executables (--only static).
// The exit status is 1 if the arguments are invalid, a variant's bytes differ, the runtime doesn't import, or a link
// fails.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS-dependent includes
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#include "buster/compiler/lane.c"
// The encoder's batch widths and the variants it measures, which only its own benchmark and tests call.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "buster/compiler/x86_64_encoder.c"
#pragma clang diagnostic pop
#include "buster/compiler/link.c"
#include "buster/compiler/link_program.c"
// The runtimes, each imported once, and binding, for the dynamic PIEs and the static executables with libc; the
// import's relocation types are the object writer's.
#include "buster/compiler/object_writer.c"
#include "buster/compiler/import.c"
#include "buster/compiler/runtime.c"
#include "buster/compiler/bind.c"
#include "buster/compiler/executable.c"
#include "buster/compiler/dynamic.c"

#define BUSTER_EXECUTABLE_BENCHMARK_PATH_BYTES 512
// Binding's arrays, for a program that calls libc.
#define BUSTER_EXECUTABLE_BENCHMARK_BIND_BYTES (1ull << 30)
#define BUSTER_EXECUTABLE_BENCHMARK_MAX_PROGRAMS 16
// The kernels' and the hash's figures are medians of this many runs.
#define BUSTER_EXECUTABLE_BENCHMARK_RUNS 11
// The links': this many runs after a warm-up, or BUSTER_EXECUTABLE_BENCHMARK_LARGE_RUNS for outputs past
// BUSTER_EXECUTABLE_BENCHMARK_LARGE_BYTES.
#define BUSTER_EXECUTABLE_BENCHMARK_LINK_RUNS 41
#define BUSTER_EXECUTABLE_BENCHMARK_LARGE_RUNS 11
#define BUSTER_EXECUTABLE_BENCHMARK_LARGE_BYTES (4ull << 20)
#define BUSTER_EXECUTABLE_BENCHMARK_MAX_RUNS 41
// Workers: a program the size of hello world's output, the test defaults', tcc's (440 KB) and synth's (46 MB).
static const uint32_t executable_benchmark_functions[] = { 1, 64, 2000, 180000 };

static int executable_benchmark_compare(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a;
    const uint64_t y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

static double executable_benchmark_median(uint64_t* values, uint64_t count)
{
    qsort(values, count, sizeof(uint64_t), executable_benchmark_compare);
    return (double)values[count / 2];
}

// splitmix64.
static uint64_t executable_benchmark_random(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t x = *state;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static void* executable_benchmark_map(uint64_t size)
{
    void* result = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result != MAP_FAILED)
    {
        madvise(result, size, MADV_HUGEPAGE);
        memset(result, 0, size);
    }
    return result != MAP_FAILED ? result : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Kernels
//
// The variants of patching PC32 fields, on one buffer at image offset `address`: the field offsets, targets and addends
// are the fixup columns, and the targets' image offsets 32-bit (`reference`) or 64-bit (`reference64`).

typedef struct ExecutableBenchmarkFixups ExecutableBenchmarkFixups;
struct ExecutableBenchmarkFixups
{
    uint32_t* offsets;
    SymbolId* targets;
    int32_t* addends;
    uint8_t* kinds;
    uint64_t count;
    uint32_t* reference;
    uint64_t* reference64;
    uint32_t* got_slots;
    uint64_t* sizes;
};

typedef enum ExecutableBenchmarkVariant
{
    EXECUTABLE_BENCHMARK_PC32,
    EXECUTABLE_BENCHMARK_KINDS,
    EXECUTABLE_BENCHMARK_CHECKED,
    EXECUTABLE_BENCHMARK_SCALAR64,
    EXECUTABLE_BENCHMARK_GATHER8_SCALAR,
    EXECUTABLE_BENCHMARK_GATHER8_SCATTER,
    EXECUTABLE_BENCHMARK_GATHER16_SCATTER,
    EXECUTABLE_BENCHMARK_VARIANTS,
} ExecutableBenchmarkVariant;

static const char* const executable_benchmark_variant_names[EXECUTABLE_BENCHMARK_VARIANTS] = {
    "executable_patch_pc32",
    "a branch on the kind",
    "checked (patch_other)",
    "64-bit targets",
    "gather 8, scalar stores",
    "gather 8, scatter",
    "gather 16, scatter",
};

static ExecutablePatch executable_benchmark_patch(const ExecutableBenchmarkFixups* fixups, bool checked)
{
    static const int64_t wide_addends[1] = { 0 };
    ExecutablePatch result = {
        .offsets = fixups->offsets,
        .targets = fixups->targets,
        .addends = fixups->addends,
        .kinds = fixups->kinds,
        .wide_addends = wide_addends,
        .reference = fixups->reference,
        .got_slots = fixups->got_slots,
        .sizes = fixups->sizes,
        .base = BUSTER_EXECUTABLE_BASE,
        .got = 0,
        .checked = checked,
    };
    return result;
}

// One branch per fixup on its kind: CALL and PC32 inline, the others through executable_patch_other.
__attribute__((noinline)) static void executable_benchmark_kinds(const ExecutablePatch* patch, uint8_t* bytes, uint32_t address, uint64_t count)
{
    const ExecutablePatch local = *patch;
    for (FixupId i = 0; i < count; i += 1)
    {
        const uint8_t kind = local.kinds[i];
        if (BUSTER_LIKELY(kind == BUSTER_LINK_FIXUP_CALL || kind == BUSTER_LINK_FIXUP_PC32))
        {
            const uint32_t value = local.reference[local.targets[i]] + (uint32_t)local.addends[i] - (address + local.offsets[i]);
            memcpy(bytes + local.offsets[i], &value, sizeof(value));
        }
        else
        {
            executable_patch_other(local, bytes, address, 0, count, i);
        }
    }
}

// 64-bit targets: S + A - P in 64 bits, its int32 range checked into an OR. The variants keep the columns in locals,
// as executable_patch_pc32 does, since each store to the bytes would otherwise make the loop reload them.
__attribute__((noinline)) static uint64_t executable_benchmark_scalar64(const ExecutableBenchmarkFixups* fixups, uint8_t* bytes, uint64_t address, uint64_t begin)
{
    const uint32_t* offsets = fixups->offsets;
    const SymbolId* targets = fixups->targets;
    const int32_t* addends = fixups->addends;
    const uint64_t* reference64 = fixups->reference64;
    const uint64_t count = fixups->count;
    uint64_t overflow = 0;
    for (uint64_t i = begin; i < count; i += 1)
    {
        const int64_t value = (int64_t)(reference64[targets[i]] + (uint64_t)(int64_t)addends[i] - (address + offsets[i]));
        overflow |= (uint64_t)(value - (int32_t)value);
        const int32_t narrow = (int32_t)value;
        memcpy(bytes + offsets[i], &narrow, sizeof(narrow));
    }
    return overflow;
}

// vpgatherdq of 8 targets, the range checked by narrowing and widening back, and scalar stores (or with `scatter`, a
// vpscatterdd: the fields are disjoint, so the order of the scatter's stores doesn't matter).
__attribute__((noinline)) static uint64_t executable_benchmark_gather8(const ExecutableBenchmarkFixups* fixups, uint8_t* bytes, uint64_t address, bool scatter)
{
    const uint32_t* offset_column = fixups->offsets;
    const SymbolId* target_column = fixups->targets;
    const int32_t* addend_column = fixups->addends;
    const uint64_t* reference64 = fixups->reference64;
    const uint64_t count = fixups->count;
    __m512i overflow = _mm512_setzero_si512();
    const __m512i base = _mm512_set1_epi64((long long)address);
    uint64_t i = 0;
    for (; i + 8 <= count; i += 8)
    {
        const __m256i offsets = _mm256_loadu_si256((const __m256i*)(offset_column + i));
        const __m256i targets = _mm256_loadu_si256((const __m256i*)(target_column + i));
        const __m512i addends = _mm512_cvtepi32_epi64(_mm256_loadu_si256((const __m256i*)(addend_column + i)));
        const __m512i symbols = _mm512_i32gather_epi64(targets, (const void*)reference64, 8);
        const __m512i places = _mm512_add_epi64(base, _mm512_cvtepu32_epi64(offsets));
        const __m512i value = _mm512_sub_epi64(_mm512_add_epi64(symbols, addends), places);
        const __m256i narrow = _mm512_cvtepi64_epi32(value);
        overflow = _mm512_or_si512(overflow, _mm512_xor_si512(_mm512_cvtepi32_epi64(narrow), value));
        if (scatter)
        {
            _mm512_mask_i32scatter_epi32(bytes, 0xff, _mm512_castsi256_si512(offsets), _mm512_castsi256_si512(narrow), 1);
        }
        else
        {
            uint32_t lane_offsets[8];
            uint32_t lane_values[8];
            _mm256_storeu_si256((__m256i*)lane_offsets, offsets);
            _mm256_storeu_si256((__m256i*)lane_values, narrow);
            for (uint32_t j = 0; j < 8; j += 1)
            {
                memcpy(bytes + lane_offsets[j], &lane_values[j], sizeof(uint32_t));
            }
        }
    }
    return executable_benchmark_scalar64(fixups, bytes, address, i) | (uint64_t)_mm512_reduce_or_epi64(overflow);
}

// vpgatherdd of 16 32-bit targets and a vpscatterdd, without a range check, as executable_patch_pc32.
__attribute__((noinline)) static void executable_benchmark_gather16(const ExecutableBenchmarkFixups* fixups, const ExecutablePatch* patch, uint8_t* bytes, uint32_t address)
{
    const uint32_t* offset_column = fixups->offsets;
    const SymbolId* target_column = fixups->targets;
    const int32_t* addend_column = fixups->addends;
    const uint32_t* reference = fixups->reference;
    const uint64_t count = fixups->count;
    const __m512i base = _mm512_set1_epi32((int)address);
    uint64_t i = 0;
    for (; i + 16 <= count; i += 16)
    {
        const __m512i offsets = _mm512_loadu_si512(offset_column + i);
        const __m512i symbols = _mm512_i32gather_epi32(_mm512_loadu_si512(target_column + i), (const void*)reference, 4);
        const __m512i value = _mm512_sub_epi32(_mm512_add_epi32(symbols, _mm512_loadu_si512(addend_column + i)), _mm512_add_epi32(base, offsets));
        _mm512_i32scatter_epi32(bytes, offsets, value, 1);
    }
    executable_patch_pc32(patch, bytes, address, (FixupId)i, (FixupId)count);
}

static void executable_benchmark_variant(ExecutableBenchmarkVariant variant, const ExecutableBenchmarkFixups* fixups, uint8_t* bytes, uint32_t address)
{
    const ExecutablePatch patch = executable_benchmark_patch(fixups, variant == EXECUTABLE_BENCHMARK_CHECKED);
    switch (variant)
    {
        case EXECUTABLE_BENCHMARK_PC32:
            executable_patch_pc32(&patch, bytes, address, 0, (FixupId)fixups->count);
            break;
        case EXECUTABLE_BENCHMARK_KINDS:
            executable_benchmark_kinds(&patch, bytes, address, fixups->count);
            break;
        case EXECUTABLE_BENCHMARK_CHECKED:
            executable_patch_range(&patch, bytes, address, 0, (FixupId)fixups->count);
            break;
        case EXECUTABLE_BENCHMARK_SCALAR64:
            executable_benchmark_scalar64(fixups, bytes, address, 0);
            break;
        case EXECUTABLE_BENCHMARK_GATHER8_SCALAR:
            executable_benchmark_gather8(fixups, bytes, address, false);
            break;
        case EXECUTABLE_BENCHMARK_GATHER8_SCATTER:
            executable_benchmark_gather8(fixups, bytes, address, true);
            break;
        default:
            executable_benchmark_gather16(fixups, &patch, bytes, address);
            break;
    }
}

// The document's synthetic case: `count` PC32 fixups in `size` bytes, one per stride at a random place in it, to
// `targets` random targets in the image. Returns whether every variant wrote executable_patch_pc32's bytes.
static bool executable_benchmark_kernel_case(uint64_t size, uint64_t count, uint64_t targets, uint32_t repeats, uint64_t* state)
{
    ExecutableBenchmarkFixups fixups = {
        .offsets = executable_benchmark_map(count * sizeof(uint32_t)),
        .targets = executable_benchmark_map(count * sizeof(SymbolId)),
        .addends = executable_benchmark_map(count * sizeof(int32_t)),
        .kinds = executable_benchmark_map(count),
        .count = count,
        .reference = executable_benchmark_map(targets * sizeof(uint32_t)),
        .reference64 = executable_benchmark_map(targets * sizeof(uint64_t)),
        .got_slots = executable_benchmark_map(targets * sizeof(uint32_t)),
        .sizes = executable_benchmark_map(targets * sizeof(uint64_t)),
    };
    uint8_t* bytes = executable_benchmark_map(size + 64);
    uint8_t* expected = executable_benchmark_map(size + 64);
    bool result = fixups.offsets != 0 && fixups.targets != 0 && fixups.addends != 0 && fixups.kinds != 0 && fixups.reference != 0 && fixups.reference64 != 0 &&
                  fixups.got_slots != 0 && fixups.sizes != 0 && bytes != 0 && expected != 0;
    const uint32_t address = 0x1000;
    if (result)
    {
        for (uint64_t t = 0; t < targets; t += 1)
        {
            fixups.reference[t] = (uint32_t)(executable_benchmark_random(state) % (size + (1u << 20)));
            fixups.reference64[t] = fixups.reference[t];
        }
        const uint64_t stride = size / count;
        for (uint64_t i = 0; i < count; i += 1)
        {
            const uint64_t random = executable_benchmark_random(state);
            fixups.offsets[i] = (uint32_t)(i * stride + random % (stride - 4));
            fixups.targets[i] = (SymbolId)((random >> 24) % targets);
            fixups.addends[i] = -4 - (int32_t)((random >> 60) & 1);
            fixups.kinds[i] = (random >> 61) & 1 ? BUSTER_LINK_FIXUP_CALL : BUSTER_LINK_FIXUP_PC32;
        }
        executable_benchmark_variant(EXECUTABLE_BENCHMARK_PC32, &fixups, expected, address);
        printf("  %llu KB, %llu fixups, %llu targets:\n", (unsigned long long)(size >> 10), (unsigned long long)count, (unsigned long long)targets);
    }
    for (uint32_t variant = 0; result && variant < EXECUTABLE_BENCHMARK_VARIANTS; variant += 1)
    {
        memset(bytes, 0, size);
        executable_benchmark_variant(variant, &fixups, bytes, address);
        const bool same = memcmp(bytes, expected, size) == 0;
        uint64_t times[BUSTER_EXECUTABLE_BENCHMARK_RUNS];
        for (uint32_t run = 0; run < BUSTER_EXECUTABLE_BENCHMARK_RUNS; run += 1)
        {
            const uint64_t begin = executable_now();
            for (uint32_t repeat = 0; repeat < repeats; repeat += 1)
            {
                executable_benchmark_variant(variant, &fixups, bytes, address);
                __asm__ volatile("" ::: "memory");
            }
            times[run] = executable_now() - begin;
        }
        const double nanoseconds = executable_benchmark_median(times, BUSTER_EXECUTABLE_BENCHMARK_RUNS) / repeats / (double)count;
        printf("    %-26s %6.3f ns per fixup%s\n", executable_benchmark_variant_names[variant], nanoseconds, same ? "" : "  DIFFERENT BYTES");
        result = same;
    }
    return result;
}

// executable_patch_range over every atom of a generated program, as executable_write's chunks run it: the mix of kinds
// a program has. The program has been written once, so its layout columns are filled.
static void executable_benchmark_program_kernel(Link* link, uint64_t got)
{
    const ExecutablePatch patch = {
        .offsets = link->fixups.offset,
        .targets = link->fixups.target,
        .addends = link->fixups.addend,
        .kinds = link->fixups.kind,
        .wide_addends = link->fixups.wide_addends,
        .reference = link->symbols.reference,
        .got_slots = link->symbols.got_slot,
        .sizes = link->symbols.size,
        .base = BUSTER_EXECUTABLE_BASE,
        .got = got,
        .checked = false,
    };
    uint64_t others = 0;
    for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
    {
        others += link->fixups.kind[fixup] != BUSTER_LINK_FIXUP_CALL && link->fixups.kind[fixup] != BUSTER_LINK_FIXUP_PC32 ? 1 : 0;
    }
    uint64_t times[BUSTER_EXECUTABLE_BENCHMARK_RUNS];
    for (uint32_t run = 0; run < BUSTER_EXECUTABLE_BENCHMARK_RUNS; run += 1)
    {
        const uint64_t begin = executable_now();
        for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
        {
            const FixupId first = link->atoms.fixup_begin[atom];
            executable_patch_range(&patch, link->atoms.bytes[atom], (uint32_t)link->atoms.address[atom], first, first + link->atoms.fixup_count[atom]);
        }
        times[run] = executable_now() - begin;
    }
    const uint64_t count = link->fixups.count - 1;
    printf("  a generated program: %llu fixups in %u atoms, %.1f%% of other kinds: %.3f ns per fixup\n", (unsigned long long)count, link->atoms.count - 1,
           100.0 * (double)others / (double)count, executable_benchmark_median(times, BUSTER_EXECUTABLE_BENCHMARK_RUNS) / (double)count);
}

// The unwind tables' passes over a generated program's .eh_frame atoms, as executable_write runs them on one lane, the
// program written once so that its references are filled: the FDEs counted by the vector path and by the exact walk,
// then their table entries both ways; and the sorted check of the table, then the sort the exact twin always does.
static void executable_benchmark_unwind(Link* link, Arena* arena)
{
    uint64_t fdes = 0;
    uint64_t frame_atoms = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        const bool frames = link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME;
        bool walked = false;
        const uint32_t count = frames ? executable_eh_frame(link, atom, true, &walked) : 0;
        fdes += count != UINT32_MAX ? count : 0;
        frame_atoms += frames ? 1 : 0;
    }
    const uint64_t mark = arena->length;
    uint8_t* bytes = arena_push(arena, BUSTER_EXECUTABLE_EH_FRAME_HDR_BYTES + 8 * fdes, 8);
    int32_t* table = bytes != 0 ? (int32_t*)(bytes + BUSTER_EXECUTABLE_EH_FRAME_HDR_BYTES) : 0;
    static const char* const names[] = { "count, vector", "count, walk", "entries, vector", "entries, walk" };
    for (uint32_t way = 0; table != 0 && way < BUSTER_ARRAY_LENGTH(names); way += 1)
    {
        const bool exact = way % 2 == 1;
        const bool entries = way >= 2;
        uint64_t times[BUSTER_EXECUTABLE_BENCHMARK_RUNS];
        uint64_t found = 0;
        for (uint32_t run = 0; run < BUSTER_EXECUTABLE_BENCHMARK_RUNS; run += 1)
        {
            found = 0;
            const uint64_t begin = executable_now();
            for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
            {
                if (link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME)
                {
                    // The count either way; then the entries, from the fixups of an atom the vector path reads (all of a
                    // generated program's), or by the walk.
                    const uint64_t address = link->atoms.address[atom];
                    bool walked = false;
                    const uint32_t count = !entries ? executable_eh_frame(link, atom, exact, &walked)
                                           : !exact ? (executable_eh_frame_entries(link, atom, table + 2 * found, 0, address) ? link->atoms.fixup_count[atom] : 0)
                                                    : executable_eh_frame_walk(link, atom, table + 2 * found, 0, address);
                    found += count != UINT32_MAX ? count : 0;
                }
            }
            times[run] = executable_now() - begin;
        }
        printf("  unwind tables, %-16s %8.3f ns per FDE (%llu FDEs in %llu atoms)%s\n", names[way], executable_benchmark_median(times, BUSTER_EXECUTABLE_BENCHMARK_RUNS) / (double)fdes,
               (unsigned long long)fdes, (unsigned long long)frame_atoms, found == fdes ? "" : "  A DIFFERENT COUNT");
    }
    for (uint32_t exact = 0; table != 0 && exact < 2; exact += 1)
    {
        uint64_t times[BUSTER_EXECUTABLE_BENCHMARK_RUNS];
        bool in_order = false;
        for (uint32_t run = 0; run < BUSTER_EXECUTABLE_BENCHMARK_RUNS; run += 1)
        {
            const uint64_t length = arena->length;
            const uint64_t begin = executable_now();
            executable_eh_frame_sort(bytes, fdes, exact != 0, arena, &in_order);
            times[run] = executable_now() - begin;
            arena_pop_to(arena, length);
        }
        printf("  unwind tables, %-16s %8.3f ns per FDE (%s)\n", exact != 0 ? "sorted" : "sorted check", executable_benchmark_median(times, BUSTER_EXECUTABLE_BENCHMARK_RUNS) / (double)fdes,
               in_order ? "in order" : "out of order");
    }
    arena_pop_to(arena, mark);
}

// ---------------------------------------------------------------------------------------------------------------------
// Hash

static void executable_benchmark_hash(uint64_t* state)
{
    const uint64_t large = 64ull << 20;
    uint8_t* bytes = executable_benchmark_map(large);
    for (uint64_t i = 0; bytes != 0 && i < large; i += 8)
    {
        const uint64_t random = executable_benchmark_random(state);
        memcpy(bytes + i, &random, sizeof(random));
    }
    static const struct
    {
        const char* name;
        uint64_t size;
        uint64_t piece;
        uint32_t repeats;
    } cases[] = {
        { "256 KB in L2", BUSTER_EXECUTABLE_CHUNK_BYTES, BUSTER_EXECUTABLE_CHUNK_BYTES, 2000 },
        { "256 KB in pieces of 4 KB", BUSTER_EXECUTABLE_CHUNK_BYTES, 4096, 2000 },
        { "256 KB in pieces of 100 bytes", BUSTER_EXECUTABLE_CHUNK_BYTES, 100, 200 },
        { "64 MB", 64ull << 20, 64ull << 20, 4 },
    };
    uint8_t sink = 0;
    for (uint32_t c = 0; bytes != 0 && c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        uint64_t times[BUSTER_EXECUTABLE_BENCHMARK_RUNS];
        for (uint32_t run = 0; run < BUSTER_EXECUTABLE_BENCHMARK_RUNS; run += 1)
        {
            const uint64_t begin = executable_now();
            for (uint32_t repeat = 0; repeat < cases[c].repeats; repeat += 1)
            {
                ExecutableHash hash;
                uint8_t output[BUSTER_EXECUTABLE_BUILD_ID_BYTES];
                executable_hash_begin(&hash);
                for (uint64_t at = 0; at < cases[c].size; at += cases[c].piece)
                {
                    executable_hash_bytes(&hash, bytes + at, cases[c].piece < cases[c].size - at ? cases[c].piece : cases[c].size - at);
                }
                executable_hash_end(&hash, output);
                sink ^= output[0];
            }
            times[run] = executable_now() - begin;
        }
        const double nanoseconds = executable_benchmark_median(times, BUSTER_EXECUTABLE_BENCHMARK_RUNS) / cases[c].repeats;
        printf("  %-30s %10.1f ns, %6.1f GB/s\n", cases[c].name, nanoseconds, (double)cases[c].size / nanoseconds);
    }
    printf("  (%02x)\n", sink);
    if (bytes != 0)
    {
        munmap(bytes, large);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Links

typedef struct ExecutableBenchmarkWrite ExecutableBenchmarkWrite;
struct ExecutableBenchmarkWrite
{
    Link* link;
    ExecutableOptions options;
    // The path again, with its NUL, for unlink.
    const char* path;
    // How many writes to time after a warm-up, and whether each goes over the last one's file or makes a new one.
    uint32_t runs;
    bool in_place;
    ExecutableResult result;
    uint64_t totals[BUSTER_EXECUTABLE_BENCHMARK_MAX_RUNS];
    uint64_t phases[EXECUTABLE_PHASES][BUSTER_EXECUTABLE_BENCHMARK_MAX_RUNS];
    bool succeeded;
};

// The lanes' entry point: the warm-up and the timed writes, all in one group, as a compiler runs its phases. Lane 0
// removes the output before a new file's write, outside the time, and times each write from its start to its return.
static void executable_benchmark_lane_main(void* argument)
{
    ExecutableBenchmarkWrite* write = argument;
    const bool first = lane_index() == 0;
    bool succeeded = true;
    for (uint32_t run = 0; succeeded && run <= write->runs; run += 1)
    {
        if (first && !write->in_place)
        {
            unlink(write->path);
        }
        lane_sync();
        const uint64_t begin = executable_now();
        succeeded = executable_write(write->link, &write->options, &write->result);
        const uint64_t end = executable_now();
        // Run 0 is the warm-up.
        if (first && run > 0)
        {
            write->totals[run - 1] = end - begin;
            uint64_t previous = write->result.start_time;
            for (uint32_t phase = 0; phase < EXECUTABLE_PHASES; phase += 1)
            {
                write->phases[phase][run - 1] = write->result.phase_times[phase] - previous;
                previous = write->result.phase_times[phase];
            }
        }
    }
    if (first)
    {
        write->succeeded = succeeded;
    }
}

// Runs the writes on a group of `lanes` lanes. Returns whether they all succeeded.
static bool executable_benchmark_write(ExecutableBenchmarkWrite* write, uint32_t lanes, uint32_t runs, bool in_place)
{
    write->runs = runs;
    write->in_place = in_place;
    write->succeeded = false;
    return lane_run(lanes, executable_benchmark_lane_main, write, 0) && write->succeeded;
}

// The floor: `size` bytes from memory written as executable_write writes them, in place (over a file of that size) or
// to a new file (removed first, outside the time), opened and closed: one pwrite, in nanoseconds.
static uint64_t executable_benchmark_floor(const char* path, const uint8_t* bytes, uint64_t size, bool in_place)
{
    if (!in_place)
    {
        unlink(path);
    }
    const uint64_t begin = executable_now();
    const int file = in_place ? open(path, O_RDWR | O_CLOEXEC) : open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0777);
    const struct iovec piece = {
        .iov_base = (void*)bytes,
        .iov_len = size,
    };
    const int error = file >= 0 ? executable_write_all(file, &piece, 1, 0, false) : errno;
    if (file >= 0)
    {
        close(file);
    }
    const uint64_t result = executable_now() - begin;
    if (error != 0)
    {
        fprintf(stderr, "the floor's write failed: %s\n", strerror(error));
    }
    return result;
}

static const char* const executable_benchmark_phase_names[EXECUTABLE_PHASES] = {
    "count",
    "layout",
    "symbols",
    "dynamic",
    "open",
    "chunks",
    "finish",
};

// With `runtime`, the program calls libc: it is bound against the runtime (bind.c) and written as a dynamic PIE, or
// with `static_runtime` (runtime_import_static's) as a static executable, its inputs from dynamic.c; otherwise it is
// written as a static executable without libc. The programs that call libc have no general- or local-dynamic TLS
// accesses, whose relaxation rewrites the atoms' bytes so that a write over them again would fail.
static bool executable_benchmark_link(const char* directory, uint32_t functions, uint32_t lanes, uint64_t seed, const Runtime* runtime, bool static_runtime)
{
    LinkProgramOptions options = link_program_default_options();
    options.seed = seed;
    options.functions = functions;
    options.units = functions / 200 > 4 ? functions / 200 : 4;
    options.globals = functions / 50 > 32 ? functions / 50 : 32;
    options.libc = runtime != 0;
    options.absolute = runtime == 0;
    LinkProgram program;
    char path[BUSTER_EXECUTABLE_BENCHMARK_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/buster-executable-benchmark-%d", directory, (int)getpid());
    Arena bind_arena = { 0 };
    Bind bind = { 0 };
    ExecutableDynamic dynamic = { 0 };
    ExecutableRuntime static_inputs = { 0 };
    bool result = false;
    if (runtime != 0)
    {
        uint64_t name_bytes = 0;
        uint64_t atom_bytes = 0;
        const LinkCapacity extra = bind_capacity(runtime, &name_bytes, &atom_bytes);
        bind_arena = arena_reserve(BUSTER_EXECUTABLE_BENCHMARK_BIND_BYTES);
        result = link_program_generate_begin(&program, options, extra, name_bytes, atom_bytes) && bind_arena.base != 0;
        result = result && bind_begin(&bind, &program.link, runtime, BIND_PATH_VECTOR, &program.bytes, options.units, &bind_arena);
        result = result && link_program_generate_atoms(&program);
        result = result && bind_end(&bind, &program.link);
        result = result && link_program_generate_end(&program);
        result = result && link_check(&program.link) == LINK_ERROR_NONE && (static_runtime || dynamic_inputs(&dynamic, runtime, &bind, &bind_arena));
        if (result && static_runtime)
        {
            dynamic_static_inputs(&static_inputs, runtime, &bind);
        }
    }
    else
    {
        result = link_program_generate(&program, options);
    }
    ExecutableBenchmarkWrite write = {
        .link = &program.link,
        .options = {
            .path = {
                .pointer = path,
                .length = strlen(path),
            },
            .entry = runtime != 0 ? dynamic_global(runtime, &bind, "_start") : program.entry,
            .dynamic = runtime != 0 && !static_runtime ? &dynamic : 0,
            .runtime = static_runtime ? &static_inputs : 0,
        },
        .path = path,
    };
    // A first write, which also fills the layout columns for the kernel's run on the program.
    result = result && executable_benchmark_write(&write, lanes, 0, false);
    const uint64_t size = write.result.file_size;
    const uint32_t runs = size > BUSTER_EXECUTABLE_BENCHMARK_LARGE_BYTES ? BUSTER_EXECUTABLE_BENCHMARK_LARGE_RUNS : BUSTER_EXECUTABLE_BENCHMARK_LINK_RUNS;
    if (result && static_runtime)
    {
        printf("%u workers, bound against the static runtime and written as a static executable: %u atoms, %u fixups, %llu bytes of code, %llu stubs, %llu IRELATIVE "
               "relocations, %llu GOT slots, a %llu-byte file in %llu chunks, %u lane%s:\n",
               functions, program.link.atoms.count - 1, program.link.fixups.count - 1, (unsigned long long)program.code_bytes, (unsigned long long)write.result.stubs,
               (unsigned long long)write.result.relocations, (unsigned long long)write.result.got_slots, (unsigned long long)size, (unsigned long long)write.result.chunks,
               lanes, lanes == 1 ? "" : "s");
    }
    else if (result && runtime != 0)
    {
        printf("%u workers, bound and written as a dynamic PIE: %u atoms, %u fixups, %llu bytes of code, %llu dynamic symbols, %llu relocations in .rela.dyn (%llu "
               "relative) and %llu in %llu words of .relr.dyn, %llu stubs, a %llu-byte file in %llu chunks, %u lane%s:\n",
               functions, program.link.atoms.count - 1, program.link.fixups.count - 1, (unsigned long long)program.code_bytes, (unsigned long long)write.result.dynamic_symbols,
               (unsigned long long)write.result.relocations, (unsigned long long)write.result.relative_relocations, (unsigned long long)write.result.relr_relocations,
               (unsigned long long)write.result.relr_words, (unsigned long long)write.result.stubs, (unsigned long long)size, (unsigned long long)write.result.chunks, lanes,
               lanes == 1 ? "" : "s");
    }
    else if (result)
    {
        printf("%u workers: %u atoms, %u fixups, %llu bytes of code, a %llu-byte file in %llu chunks, %u lane%s:\n", functions, program.link.atoms.count - 1,
               program.link.fixups.count - 1, (unsigned long long)program.code_bytes, (unsigned long long)size, (unsigned long long)write.result.chunks, lanes,
               lanes == 1 ? "" : "s");
    }
    else
    {
        printf("%u workers: %s (error %d)\n", functions, program.problem != 0 ? program.problem : "executable_write failed", write.result.error);
    }
    for (uint32_t place = 0; result && place < 2; place += 1)
    {
        const bool in_place = place == 0;
        result = executable_benchmark_write(&write, lanes, runs, in_place);
        const ExecutableWrite expected = in_place ? EXECUTABLE_WRITE_IN_PLACE : EXECUTABLE_WRITE_CREATED;
        result = result && write.result.write == expected;
        if (result)
        {
            const double total = executable_benchmark_median(write.totals, runs);
            printf("  %-9s %10.1f us (%5.2f GB/s):", in_place ? "in place" : "new file", total / 1000.0, (double)size / total);
            for (uint32_t phase = 0; phase < EXECUTABLE_PHASES; phase += 1)
            {
                printf(" %s %.1f", executable_benchmark_phase_names[phase], executable_benchmark_median(write.phases[phase], runs) / 1000.0);
            }
            printf("\n");
        }
    }
    // The floor, from the file just written.
    uint8_t* bytes = result ? executable_benchmark_map(size + 1) : 0;
    const int file = bytes != 0 ? open(path, O_RDONLY | O_CLOEXEC) : -1;
    result = result && file >= 0 && pread(file, bytes, size, 0) == (ssize_t)size;
    if (file >= 0)
    {
        close(file);
    }
    for (uint32_t place = 0; result && place < 2; place += 1)
    {
        uint64_t floors[BUSTER_EXECUTABLE_BENCHMARK_MAX_RUNS];
        executable_benchmark_floor(path, bytes, size, place == 0);
        for (uint32_t run = 0; run < runs; run += 1)
        {
            floors[run] = executable_benchmark_floor(path, bytes, size, place == 0);
        }
        const double floor = executable_benchmark_median(floors, runs);
        printf("  %-9s %10.1f us (%5.2f GB/s): the floor, one pwrite\n", place == 0 ? "in place" : "new file", floor / 1000.0, (double)size / floor);
    }
    if (result && lanes == 1 && runtime == 0)
    {
        executable_benchmark_program_kernel(&program.link, 0);
        Scratch scratch = scratch_begin(0);
        executable_benchmark_unwind(&program.link, scratch.arena);
        scratch_end(scratch);
    }
    if (bytes != 0)
    {
        munmap(bytes, size + 1);
    }
    unlink(path);
    link_program_release(&program);
    arena_release(&bind_arena);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static bool executable_benchmark_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = end != text && *end == 0;
    *value = result ? parsed : *value;
    return result;
}

int run_executable_benchmark(int argc, char** argv);

int run_executable_benchmark(int argc, char** argv)
{
    const char* directory = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    uint32_t functions[BUSTER_EXECUTABLE_BENCHMARK_MAX_PROGRAMS];
    uint32_t program_count = BUSTER_ARRAY_LENGTH(executable_benchmark_functions);
    memcpy(functions, executable_benchmark_functions, sizeof(executable_benchmark_functions));
    uint64_t lanes = 1;
    uint64_t seed = 1;
    const char* only = 0;
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
        {
            directory = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--functions") == 0 && i + 1 < argc)
        {
            // A list separated by commas.
            program_count = 0;
            const char* text = argv[i + 1];
            while (valid && *text != 0 && program_count < BUSTER_EXECUTABLE_BENCHMARK_MAX_PROGRAMS)
            {
                char* end = 0;
                const unsigned long long parsed = strtoull(text, &end, 0);
                valid = end != text && parsed >= 1 && parsed <= BUSTER_LINK_PROGRAM_MAX_COUNT && (*end == 0 || *end == ',');
                functions[program_count] = (uint32_t)parsed;
                program_count += valid ? 1 : 0;
                text = *end == ',' ? end + 1 : end;
            }
            valid = valid && program_count != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--lanes") == 0 && i + 1 < argc)
        {
            valid = valid && executable_benchmark_parse_u64(argv[i + 1], &lanes) && lanes >= 1 && lanes <= BUSTER_LANE_MAX_COUNT;
            i += 1;
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && executable_benchmark_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = argv[i + 1];
            valid = valid && (strcmp(only, "kernels") == 0 || strcmp(only, "hash") == 0 || strcmp(only, "links") == 0 || strcmp(only, "dynamic") == 0 ||
                              strcmp(only, "static") == 0);
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
        fprintf(stderr, "usage: benchmark-executable [--directory PATH] [--functions LIST] [--lanes N] [--seed N] [--only kernels|hash|links|dynamic|static]\n");
    }
    else
    {
        bool passed = true;
        uint64_t state = seed;
        if (only == 0 || strcmp(only, "kernels") == 0)
        {
            printf("Patch kernels:\n");
            passed = executable_benchmark_kernel_case(64ull << 10, 2000, 10000, 2000, &state) && passed;
            passed = executable_benchmark_kernel_case(1ull << 20, 30000, 100000, 100, &state) && passed;
            passed = executable_benchmark_kernel_case(64ull << 20, 2000000, 1000000, 3, &state) && passed;
        }
        if (only == 0 || strcmp(only, "hash") == 0)
        {
            printf("Chunk hash:\n");
            executable_benchmark_hash(&state);
        }
        for (uint32_t i = 0; (only == 0 || strcmp(only, "links") == 0) && i < program_count; i += 1)
        {
            passed = executable_benchmark_link(directory, functions[i], (uint32_t)lanes, seed, 0, false) && passed;
        }
        // The same sizes as dynamic PIEs, against the system's runtime.
        Runtime runtime;
        const bool dynamic = only == 0 || strcmp(only, "links") == 0 || strcmp(only, "dynamic") == 0;
        const RuntimeError imported = dynamic ? runtime_import(&runtime) : RUNTIME_ERROR_NONE;
        if (dynamic && imported != RUNTIME_ERROR_NONE)
        {
            fprintf(stderr, "the runtime didn't import: error %d, %s\n", (int)imported, runtime.failed_file);
            passed = false;
        }
        for (uint32_t i = 0; dynamic && imported == RUNTIME_ERROR_NONE && i < program_count; i += 1)
        {
            passed = executable_benchmark_link(directory, functions[i], (uint32_t)lanes, seed, &runtime, false) && passed;
        }
        if (dynamic)
        {
            runtime_release(&runtime);
        }
        // And as static executables, against the system's static runtime.
        Runtime static_runtime;
        const bool statics = only == 0 || strcmp(only, "links") == 0 || strcmp(only, "static") == 0;
        const RuntimeError static_imported = statics ? runtime_import_static(&static_runtime) : RUNTIME_ERROR_NONE;
        if (statics && static_imported != RUNTIME_ERROR_NONE)
        {
            fprintf(stderr, "the static runtime didn't import: error %d, %s\n", (int)static_imported, static_runtime.failed_file);
            passed = false;
        }
        for (uint32_t i = 0; statics && static_imported == RUNTIME_ERROR_NONE && i < program_count; i += 1)
        {
            passed = executable_benchmark_link(directory, functions[i], (uint32_t)lanes, seed, &static_runtime, true) && passed;
        }
        if (statics)
        {
            runtime_release(&static_runtime);
        }
        result = passed ? 0 : 1;
    }
    return result;
}
