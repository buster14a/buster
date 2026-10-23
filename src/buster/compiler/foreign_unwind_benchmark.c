// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Synthetic full-prune benchmark; include after foreign_unwind.c. No test helpers, files or external compilers are
// required. run_foreign_unwind_benchmark accepts --runs N (21), --fdes N (otherwise 33/4097/65537) and --profile
// mixed|all|none (otherwise all three). These phase measurements are not end-to-end linker acceptance.
//
// Frozen original zR/zPLR records, symbols and relocations have an independent expected compact byte stream,
// relocation table and interval map. Both paths must agree with that oracle before timing. Every measured result
// is checked again outside the clock; input storage must remain byte-identical. A sample times the complete prune
// call, including validation, sorting, scratch/output allocations, record copying and relocation transposition.
// Parsing and most ownership work are shared scalar code; exact/vector selects the relocation kernels.
//
// Two warm-up pairs precede interleaved samples. Arena resets, verification and statistics are outside timing, and
// arena_pop_to retains committed pages. Results describe warmed reusable arenas, not cold page-fault cost. Output
// arena lengths are observed logical bytes. Temporary scratch payload is an explicitly labelled estimate from the
// current allocation contract, excluding alignment, VM reservations and RSS; it is not an observed peak.
//
// On the approved 9700X only, each independent run takes the shared lock, for example:
// EXCLUSIVE_OWNER=codex-linker-foreign86 EXCLUSIVE_TITLE='foreign unwind phase' EXCLUSIVE_LINK='https://github.com/buster14a/buster/issues/86' ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-foreign-unwind --runs 21
// Repeat separately locked processes before drawing performance conclusions from within-process sample spread.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BUSTER_FOREIGN_UNWIND_BENCHMARK_RUNS 21
#define BUSTER_FOREIGN_UNWIND_BENCHMARK_MAX_RUNS 101
#define BUSTER_FOREIGN_UNWIND_BENCHMARK_MAX_FDES 262144
#define BUSTER_FOREIGN_UNWIND_BENCHMARK_GROUP 32
#define BUSTER_FOREIGN_UNWIND_BENCHMARK_SECTIONS 67

typedef enum ForeignUnwindBenchmarkProfile
{
    FOREIGN_UNWIND_BENCHMARK_MIXED,
    FOREIGN_UNWIND_BENCHMARK_ALL,
    FOREIGN_UNWIND_BENCHMARK_NONE,
    FOREIGN_UNWIND_BENCHMARK_PROFILES,
} ForeignUnwindBenchmarkProfile;

static const char* const foreign_unwind_benchmark_profiles[] = {
    "mixed",
    "all",
    "none",
};

typedef struct ForeignUnwindBenchmarkFixture ForeignUnwindBenchmarkFixture;
struct ForeignUnwindBenchmarkFixture
{
    ForeignUnwindInput input;
    ImportSymbols symbols;
    ForeignUnwind expected;
    const uint8_t* snapshot;
    uint64_t frozen_bytes;
    uint32_t fdes;
};

typedef struct ForeignUnwindBenchmarkStatistics ForeignUnwindBenchmarkStatistics;
struct ForeignUnwindBenchmarkStatistics
{
    double minimum;
    double median;
    double maximum;
};

// The fixture has 64 original code sections plus personality and LSDA sections. Many FDEs may describe functions
// in one original section, just as an object without function sections does. Every fourth mixed CIE group is dead;
// the others interleave live/dead FDEs. Auxiliaries never decide code liveness and always have valid live targets.
static bool foreign_unwind_benchmark_generate(ForeignUnwindBenchmarkFixture* fixture, uint32_t fdes, ForeignUnwindBenchmarkProfile profile, Arena* frozen, Arena* oracle)
{
    static const uint8_t plain[] = {
        20, 0, 0, 0, 0, 0, 0, 0, 1, 'z', 'R', 0, 1, 0x78, 16, 1,
        0x1b, 0x0c, 7, 8, 0x90, 1, 0, 0,
    };
    static const uint8_t augmented[] = {
        28, 0, 0, 0, 0, 0, 0, 0, 1, 'z', 'P', 'L', 'R', 0, 1, 0x78,
        16, 7, 0x9b, 0, 0, 0, 0, 0x1b, 0x1b, 0x0c, 7, 8, 0x90, 1, 0, 0,
    };
    const uint32_t groups = (fdes + BUSTER_FOREIGN_UNWIND_BENCHMARK_GROUP - 1) / BUSTER_FOREIGN_UNWIND_BENCHMARK_GROUP;
    const uint32_t records = fdes + groups + 2;
    const uint32_t relocation_capacity = fdes * 2 + groups + 1;
    const uint64_t byte_capacity = (uint64_t)(groups + 1) * sizeof(augmented) + (uint64_t)fdes * 28 + 4;
    uint8_t* bytes = BUSTER_ARENA_PUSH_ARRAY(frozen, uint8_t, byte_capacity);
    Elf64_Rela* relocations = BUSTER_ARENA_PUSH_ARRAY(frozen, Elf64_Rela, relocation_capacity);
    Elf64_Sym* symbols = BUSTER_ARENA_PUSH_ARRAY(frozen, Elf64_Sym, BUSTER_FOREIGN_UNWIND_BENCHMARK_SECTIONS);
    AtomId* sections = BUSTER_ARENA_PUSH_ARRAY(frozen, AtomId, BUSTER_FOREIGN_UNWIND_BENCHMARK_SECTIONS);
    ForeignUnwindRecord* expected = BUSTER_ARENA_PUSH_ARRAY(oracle, ForeignUnwindRecord, records);
    uint32_t* parents = BUSTER_ARENA_PUSH_ARRAY(oracle, uint32_t, records);
    uint32_t* owners = BUSTER_ARENA_PUSH_ARRAY(oracle, uint32_t, relocation_capacity);
    bool valid = bytes != 0 && relocations != 0 && symbols != 0 && sections != 0 && expected != 0 && parents != 0 && owners != 0;
    if (valid)
    {
        memset(bytes, 0, byte_capacity);
        memset(relocations, 0, (uint64_t)relocation_capacity * sizeof(*relocations));
        memset(symbols, 0, BUSTER_FOREIGN_UNWIND_BENCHMARK_SECTIONS * sizeof(*symbols));
        memset(sections, 0, BUSTER_FOREIGN_UNWIND_BENCHMARK_SECTIONS * sizeof(*sections));
        memset(parents, 0, (uint64_t)records * sizeof(*parents));
        for (uint32_t section = 1; section < BUSTER_FOREIGN_UNWIND_BENCHMARK_SECTIONS; section += 1)
        {
            const bool auxiliary = section >= 65;
            const bool live = auxiliary || profile == FOREIGN_UNWIND_BENCHMARK_ALL || (profile == FOREIGN_UNWIND_BENCHMARK_MIXED && (section & 1) != 0);
            sections[section] = live ? 100 + section : 0;
            symbols[section] = (Elf64_Sym){
                .st_info = ELF64_ST_INFO(STB_LOCAL, section == 66 ? STT_OBJECT : STT_FUNC),
                .st_shndx = (uint16_t)section,
                .st_value = section * 16,
            };
        }
        uint64_t position = 0;
        uint32_t record = 0;
        uint32_t relocation = 0;
        uint32_t retained = 0;
        // Group zero in this loop is an orphan CIE, so even an all-live case must remove and remap a prefix.
        for (uint32_t group = 0; group <= groups; group += 1)
        {
            const bool personality = group == 0 || (group & 1) == 0;
            const uint32_t length = personality ? sizeof(augmented) : sizeof(plain);
            const uint32_t cie = record;
            const uint64_t cie_offset = position;
            memcpy(bytes + position, personality ? augmented : plain, length);
            expected[record] = (ForeignUnwindRecord){
                .old_offset = position,
                .new_offset = UINT64_MAX,
                .length = length,
                .cie_offset = UINT64_MAX,
                .kind = FOREIGN_UNWIND_CIE,
            };
            if (personality)
            {
                relocations[relocation] = (Elf64_Rela){
                    .r_offset = position + 19,
                    .r_info = ELF64_R_INFO(65, R_X86_64_PC32),
                    .r_addend = -4,
                };
                owners[relocation++] = record;
            }
            record += 1;
            position += length;
            const uint32_t first = group == 0 ? 0 : (group - 1) * BUSTER_FOREIGN_UNWIND_BENCHMARK_GROUP;
            const uint32_t available = group == 0 ? 0 : fdes - first;
            const uint32_t count = available < BUSTER_FOREIGN_UNWIND_BENCHMARK_GROUP ? available : BUSTER_FOREIGN_UNWIND_BENCHMARK_GROUP;
            for (uint32_t at = 0; at < count; at += 1)
            {
                const uint32_t index = first + at;
                const uint32_t section = group % 4 == 0 ? 2 : 1 + (index * 37) % 64;
                const bool live = sections[section] != 0;
                const uint32_t size = personality ? 28 : 24;
                const uint32_t stored = size - 4;
                const uint32_t distance = (uint32_t)(position + 4 - cie_offset);
                const uint32_t range = 19 + index % 13;
                memcpy(bytes + position, &stored, sizeof(stored));
                memcpy(bytes + position + 4, &distance, sizeof(distance));
                memcpy(bytes + position + 12, &range, sizeof(range));
                bytes[position + 16] = personality ? 4 : 0;
                const uint64_t instructions = position + (personality ? 21 : 17);
                bytes[instructions] = 0x41;
                bytes[instructions + 1] = 0x0e;
                bytes[instructions + 2] = 0x10;
                expected[record] = (ForeignUnwindRecord){
                    .old_offset = position,
                    .new_offset = live ? 0 : UINT64_MAX,
                    .length = size,
                    .cie_offset = cie_offset,
                    .kind = FOREIGN_UNWIND_FDE,
                    .section = section,
                    .atom = sections[section],
                };
                parents[record] = cie;
                if (live)
                {
                    expected[cie].new_offset = 0;
                    retained += 1;
                }
                relocations[relocation] = (Elf64_Rela){
                    .r_offset = position + 8,
                    .r_info = ELF64_R_INFO(section, R_X86_64_PC32),
                    .r_addend = (int64_t)(index % 17) - 8,
                };
                owners[relocation++] = record;
                if (personality)
                {
                    relocations[relocation] = (Elf64_Rela){
                        .r_offset = position + 17,
                        .r_info = ELF64_R_INFO(66, R_X86_64_PC32),
                        .r_addend = (int64_t)(index % 8) * 8,
                    };
                    owners[relocation++] = record;
                }
                record += 1;
                position += size;
            }
        }
        expected[record] = (ForeignUnwindRecord){
            .old_offset = position,
            .new_offset = retained != 0 ? 0 : UINT64_MAX,
            .length = 4,
            .cie_offset = UINT64_MAX,
            .kind = FOREIGN_UNWIND_TERMINATOR,
        };
        record += 1;
        position += 4;
        valid = record == records && position <= byte_capacity && relocation <= relocation_capacity;
        fixture->symbols = (ImportSymbols){
            .section = 1,
            .entries = {
                .pointer = (char*)symbols,
                .length = BUSTER_FOREIGN_UNWIND_BENCHMARK_SECTIONS * sizeof(*symbols),
            },
            .count = BUSTER_FOREIGN_UNWIND_BENCHMARK_SECTIONS,
        };
        fixture->input = (ForeignUnwindInput){
            .bytes = {
                .pointer = (char*)bytes,
                .length = position,
            },
            .relocations = relocations,
            .relocation_count = relocation,
            .symbols = &fixture->symbols,
            .sections = sections,
            .section_count = BUSTER_FOREIGN_UNWIND_BENCHMARK_SECTIONS,
        };
        fixture->expected = (ForeignUnwind){
            .records = expected,
            .record_count = records,
            .old_length = position,
            .fde_count = retained,
        };
        fixture->fdes = fdes;
        // The expected map is a prefix sum of independently known live records, never the pruner's output.
        for (uint32_t r = 0; r < records; r += 1)
        {
            if (expected[r].new_offset != UINT64_MAX)
            {
                expected[r].new_offset = fixture->expected.bytes.length;
                fixture->expected.bytes.length += expected[r].length;
            }
        }
        fixture->expected.bytes.pointer = BUSTER_ARENA_PUSH_ARRAY(oracle, char, fixture->expected.bytes.length);
        fixture->expected.relocations = BUSTER_ARENA_PUSH_ARRAY(oracle, Elf64_Rela, relocation);
        valid = valid && fixture->expected.bytes.pointer != 0 && fixture->expected.relocations != 0;
        for (uint32_t r = 0; valid && r < records; r += 1)
        {
            if (expected[r].new_offset != UINT64_MAX)
            {
                char* destination = fixture->expected.bytes.pointer + expected[r].new_offset;
                memcpy(destination, bytes + expected[r].old_offset, expected[r].length);
                if (expected[r].kind == FOREIGN_UNWIND_FDE)
                {
                    const uint32_t distance = (uint32_t)(expected[r].new_offset + 4 - expected[parents[r]].new_offset);
                    memcpy(destination + 4, &distance, sizeof(distance));
                }
            }
        }
        // Input order is deliberately not offset order. The independent output retains this reversed order.
        for (uint32_t i = 0; i < relocation / 2; i += 1)
        {
            const uint32_t other = relocation - 1 - i;
            const Elf64_Rela value = relocations[i];
            const uint32_t owner = owners[i];
            relocations[i] = relocations[other];
            owners[i] = owners[other];
            relocations[other] = value;
            owners[other] = owner;
        }
        for (uint32_t i = 0; valid && i < relocation; i += 1)
        {
            const ForeignUnwindRecord* owner = &expected[owners[i]];
            if (owner->new_offset != UINT64_MAX)
            {
                Elf64_Rela* destination = &fixture->expected.relocations[fixture->expected.relocation_count];
                *destination = relocations[i];
                destination->r_offset = owner->new_offset + relocations[i].r_offset - owner->old_offset;
                fixture->expected.relocation_count += 1;
            }
        }
        fixture->frozen_bytes = frozen->length;
        uint8_t* snapshot = BUSTER_ARENA_PUSH_ARRAY(oracle, uint8_t, fixture->frozen_bytes);
        valid = valid && snapshot != 0;
        if (valid)
        {
            memcpy(snapshot, frozen->base, fixture->frozen_bytes);
            fixture->snapshot = snapshot;
        }
    }
    return valid;
}

static bool foreign_unwind_benchmark_equal(const ForeignUnwindBenchmarkFixture* fixture, const ForeignUnwind* actual, bool offsets)
{
    const ForeignUnwind* expected = &fixture->expected;
    bool same = actual->bytes.length == expected->bytes.length && actual->old_length == expected->old_length && actual->record_count == expected->record_count &&
                actual->relocation_count == expected->relocation_count && actual->fde_count == expected->fde_count && actual->bytes.pointer != 0 &&
                actual->records != 0 && actual->relocations != 0;
    if (same)
    {
        same = memcmp(actual->bytes.pointer, expected->bytes.pointer, expected->bytes.length) == 0;
    }
    for (uint32_t r = 0; same && r < expected->record_count; r += 1)
    {
        const ForeignUnwindRecord* left = &actual->records[r];
        const ForeignUnwindRecord* right = &expected->records[r];
        same = left->old_offset == right->old_offset && left->new_offset == right->new_offset && left->length == right->length &&
               left->cie_offset == right->cie_offset && left->kind == right->kind && left->section == right->section && left->atom == right->atom;
        if (same && offsets)
        {
            for (uint32_t end = 0; same && end < 2; end += 1)
            {
                const uint64_t delta = end != 0 ? right->length - 1 : 0;
                uint64_t translated = UINT64_MAX;
                const bool mapped = foreign_unwind_offset(actual, right->old_offset + delta, &translated);
                const bool kept = right->new_offset != UINT64_MAX;
                same = mapped == kept && (kept ? translated == right->new_offset + delta : translated == UINT64_MAX);
            }
        }
    }
    for (uint32_t i = 0; same && i < expected->relocation_count; i += 1)
    {
        const Elf64_Rela* left = &actual->relocations[i];
        const Elf64_Rela* right = &expected->relocations[i];
        same = left->r_offset == right->r_offset && left->r_info == right->r_info && left->r_addend == right->r_addend;
    }
    if (same && offsets)
    {
        uint64_t translated = UINT64_MAX;
        same = foreign_unwind_offset(actual, expected->old_length, &translated) && translated == expected->bytes.length;
    }
    return same;
}

static __attribute__((noinline)) ForeignUnwindResult foreign_unwind_benchmark_prune(const ForeignUnwindInput* input, ForeignPath path, Arena* arena, ForeignUnwind* output)
{
    return foreign_unwind_prune(input, path, arena, output);
}

static int foreign_unwind_benchmark_compare(const void* left, const void* right)
{
    const uint64_t a = *(const uint64_t*)left;
    const uint64_t b = *(const uint64_t*)right;
    return (a > b) - (a < b);
}

static ForeignUnwindBenchmarkStatistics foreign_unwind_benchmark_statistics(uint64_t* samples, uint32_t runs)
{
    qsort(samples, runs, sizeof(*samples), foreign_unwind_benchmark_compare);
    const ForeignUnwindBenchmarkStatistics result = {
        .minimum = (double)samples[0],
        .median = runs % 2 != 0 ? (double)samples[runs / 2] : ((double)samples[runs / 2 - 1] + (double)samples[runs / 2]) * 0.5,
        .maximum = (double)samples[runs - 1],
    };
    return result;
}

static bool foreign_unwind_benchmark_case(uint32_t fdes, ForeignUnwindBenchmarkProfile profile, uint32_t runs)
{
    const uint64_t capacity = (uint64_t)fdes * 512 + (1ull << 20);
    Arena frozen = arena_reserve(capacity);
    Arena oracle = arena_reserve(capacity);
    Arena outputs[2] = {
        arena_reserve(capacity),
        arena_reserve(capacity),
    };
    ForeignUnwindBenchmarkFixture fixture = { 0 };
    bool valid = frozen.base != 0 && oracle.base != 0 && outputs[0].base != 0 && outputs[1].base != 0 &&
                 foreign_unwind_benchmark_generate(&fixture, fdes, profile, &frozen, &oracle);
    uint64_t arena_bytes[2] = { 0 };
    uint64_t samples[2][BUSTER_FOREIGN_UNWIND_BENCHMARK_MAX_RUNS] = { 0 };
    for (uint32_t pass = 0; valid && pass < runs + 2; pass += 1)
    {
        for (uint32_t turn = 0; valid && turn < 2; turn += 1)
        {
            const uint32_t path = (pass + turn) & 1;
            const ForeignPath implementation = path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR;
            Arena* arena = &outputs[path];
            arena_pop_to(arena, 0);
            ForeignUnwind output = { 0 };
            ForeignUnwindResult result = { 0 };
            struct timespec begin = { 0 };
            struct timespec end = { 0 };
            if (pass >= 2)
            {
                valid = clock_gettime(CLOCK_MONOTONIC_RAW, &begin) == 0;
            }
            if (valid)
            {
                result = foreign_unwind_benchmark_prune(&fixture.input, implementation, arena, &output);
                __asm__ volatile("" : : "r"(output.bytes.pointer), "r"(output.records), "r"(output.relocations) : "memory");
                if (pass >= 2)
                {
                    valid = clock_gettime(CLOCK_MONOTONIC_RAW, &end) == 0;
                }
            }
            valid = valid && result.error == FOREIGN_UNWIND_ERROR_NONE && foreign_unwind_benchmark_equal(&fixture, &output, pass < 2);
            if (valid && pass >= 2)
            {
                const int64_t elapsed = (int64_t)(end.tv_sec - begin.tv_sec) * INT64_C(1000000000) + end.tv_nsec - begin.tv_nsec;
                valid = elapsed > 0 && arena->length == arena_bytes[path];
                samples[path][pass - 2] = elapsed > 0 ? (uint64_t)elapsed : 0;
            }
            else if (valid)
            {
                arena_bytes[path] = arena->length;
            }
            if (!valid)
            {
                fprintf(stderr, "foreign unwind benchmark: %u/%s %s pass %u failed (error %u, offset %llu, relocation %u)\n", fdes,
                        foreign_unwind_benchmark_profiles[profile], path == 0 ? "exact" : "vector", pass, (unsigned)result.error,
                        (unsigned long long)result.offset, result.relocation);
            }
        }
        if (valid)
        {
            valid = memcmp(frozen.base, fixture.snapshot, fixture.frozen_bytes) == 0;
            if (!valid)
            {
                fprintf(stderr, "foreign unwind benchmark: frozen input changed (%u/%s, pass %u)\n", fdes, foreign_unwind_benchmark_profiles[profile], pass);
            }
        }
    }
    if (valid)
    {
        const uint64_t input_payload = fixture.input.bytes.length + (uint64_t)fixture.input.relocation_count * sizeof(Elf64_Rela) + fixture.symbols.entries.length +
                                       (uint64_t)fixture.input.section_count * sizeof(AtomId);
        const uint64_t temporary = (uint64_t)fixture.expected.record_count * sizeof(ForeignUnwindState) +
                                   (uint64_t)fixture.input.relocation_count * (3 * sizeof(uint32_t) + sizeof(uint64_t));
        printf("foreign-unwind %u/%s: FDEs %u -> %u, EH bytes %llu -> %llu, relocations %u -> %u, map records %u\n", fdes,
               foreign_unwind_benchmark_profiles[profile], fixture.fdes, fixture.expected.fde_count, (unsigned long long)fixture.input.bytes.length,
               (unsigned long long)fixture.expected.bytes.length, fixture.input.relocation_count, fixture.expected.relocation_count, fixture.expected.record_count);
        printf("  arena bytes: frozen=%llu oracle+snapshot=%llu output-exact=%llu output-vector=%llu; temporary-scratch-payload-estimate=%llu\n",
               (unsigned long long)frozen.length, (unsigned long long)oracle.length, (unsigned long long)arena_bytes[0], (unsigned long long)arena_bytes[1],
               (unsigned long long)temporary);
        double medians[2] = { 0 };
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const ForeignUnwindBenchmarkStatistics statistics = foreign_unwind_benchmark_statistics(samples[path], runs);
            const double seconds = statistics.median / 1e9;
            medians[path] = statistics.median;
            printf("  full-prune %-6s: median %.3f us, min %.3f us, max %.3f us, spread %.2f%%; %.3f ns/input-FDE, %.3f M input-FDE/s, %.3f input-MiB/s, %.3f retained-EH-MiB/s\n",
                   path == 0 ? "exact" : "vector", statistics.median / 1e3, statistics.minimum / 1e3, statistics.maximum / 1e3,
                   (statistics.maximum - statistics.minimum) * 100.0 / statistics.median, statistics.median / fdes, (double)fdes / seconds / 1e6,
                   (double)input_payload / seconds / (1u << 20), (double)fixture.expected.bytes.length / seconds / (1u << 20));
        }
        printf("  synthetic phase median exact/vector = %.4fx; %u paired samples after two warm-up pairs\n", medians[0] / medians[1], runs);
    }
    else if (fixture.snapshot == 0)
    {
        fprintf(stderr, "foreign unwind benchmark: fixture allocation failed (%u/%s)\n", fdes, foreign_unwind_benchmark_profiles[profile]);
    }
    arena_release(&outputs[1]);
    arena_release(&outputs[0]);
    arena_release(&oracle);
    arena_release(&frozen);
    return valid;
}

static bool foreign_unwind_benchmark_number(const char* text, uint32_t maximum, uint32_t* value)
{
    uint32_t parsed = 0;
    bool valid = text != 0 && text[0] != 0;
    for (uint64_t i = 0; valid && text[i] != 0; i += 1)
    {
        const uint32_t digit = (uint32_t)(uint8_t)text[i] - (uint32_t)'0';
        valid = digit <= 9 && parsed <= (maximum - digit) / 10;
        if (valid)
        {
            parsed = parsed * 10 + digit;
        }
    }
    valid = valid && parsed != 0 && parsed <= maximum;
    *value = valid ? parsed : 0;
    return valid;
}

static int run_foreign_unwind_benchmark(int argc, char** argv)
{
    uint32_t runs = BUSTER_FOREIGN_UNWIND_BENCHMARK_RUNS;
    uint32_t fdes = 0;
    ForeignUnwindBenchmarkProfile profile = FOREIGN_UNWIND_BENCHMARK_PROFILES;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool run_option = strcmp(argv[i], "--runs") == 0;
        const bool fde_option = strcmp(argv[i], "--fdes") == 0;
        const bool profile_option = strcmp(argv[i], "--profile") == 0;
        valid = (run_option || fde_option || profile_option) && i + 1 < argc;
        if (valid)
        {
            i += 1;
            if (profile_option)
            {
                profile = FOREIGN_UNWIND_BENCHMARK_PROFILES;
                for (uint32_t p = 0; p < FOREIGN_UNWIND_BENCHMARK_PROFILES; p += 1)
                {
                    if (strcmp(argv[i], foreign_unwind_benchmark_profiles[p]) == 0)
                    {
                        profile = (ForeignUnwindBenchmarkProfile)p;
                    }
                }
                valid = profile != FOREIGN_UNWIND_BENCHMARK_PROFILES;
            }
            else
            {
                valid = foreign_unwind_benchmark_number(argv[i], run_option ? BUSTER_FOREIGN_UNWIND_BENCHMARK_MAX_RUNS : BUSTER_FOREIGN_UNWIND_BENCHMARK_MAX_FDES,
                                                       run_option ? &runs : &fdes);
            }
        }
    }
    if (!valid)
    {
        fprintf(stderr, "usage: benchmark-foreign-unwind [--runs 1..%u] [--fdes 1..%u] [--profile mixed|all|none]\n",
                BUSTER_FOREIGN_UNWIND_BENCHMARK_MAX_RUNS, BUSTER_FOREIGN_UNWIND_BENCHMARK_MAX_FDES);
    }
    else
    {
        printf("Synthetic unwind phase only; not end-to-end linker acceptance. Full-prune exact/vector includes allocations and copies.\n");
        printf("Warm reusable arenas; reset/oracle checks excluded. Memory is logical arena usage, not RSS; scratch estimate excludes padding.\n");
        static const uint32_t populations[] = {
            33,
            4097,
            65537,
        };
        for (uint32_t size = 0; valid && size < (fdes != 0 ? 1u : (uint32_t)BUSTER_ARRAY_LENGTH(populations)); size += 1)
        {
            const uint32_t count = fdes != 0 ? fdes : populations[size];
            for (uint32_t p = 0; valid && p < FOREIGN_UNWIND_BENCHMARK_PROFILES; p += 1)
            {
                if (profile == FOREIGN_UNWIND_BENCHMARK_PROFILES || p == (uint32_t)profile)
                {
                    valid = foreign_unwind_benchmark_case(count, (ForeignUnwindBenchmarkProfile)p, runs);
                }
            }
        }
    }
    return valid ? 0 : 1;
}
