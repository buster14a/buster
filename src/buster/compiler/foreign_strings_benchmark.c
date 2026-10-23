// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Whole foreign-input pipeline with and without string merging. Include after foreign_link_test.c. The object
// writer produces frozen original ET_REL inputs before timing. Every sample resolves those files afresh, converts
// them, optionally merges their literals, writes a fresh executable, and releases the per-link mappings and arenas.
// Execution, complete byte comparison, and removal of the previous output are outside timing. Exact/vector outputs
// must agree within each merge state; merged and unmerged layouts may
// differ. Retained arena bytes exclude mapped input files, generated inputs and transient scratch, not peak RSS.
//
// Usage: benchmark-foreign-strings [--counts LIST] [--runs N] [--order LIST] [--only duplicate|unique]
// [--after-scalar MS] [--directory PATH] [--keep]. Order entries are nomerge-exact, merge-exact, nomerge-vector and merge-vector;
// any nonempty subset without repetitions is valid. One warm-up precedes the measured samples of each variant.
// --after-scalar (0..300 ms) conditions every sample with scalar integer work, outside the timed whole pipeline.
// Fenced TSC ticks cover the same pipeline plus its wall-clock reads; they are elapsed reference ticks, not core cycles.
// Pin externally to CPU 2 and acquire the approved host's shared lock separately for each invocation.

#define BUSTER_FOREIGN_STRINGS_BENCHMARK_LITERAL_BYTES 32
#define BUSTER_FOREIGN_STRINGS_BENCHMARK_UNIT_LITERALS 256
#define BUSTER_FOREIGN_STRINGS_BENCHMARK_MAX_LITERALS (1u << 20)
#define BUSTER_FOREIGN_STRINGS_BENCHMARK_FNV_OFFSET 14695981039346656037ull
#define BUSTER_FOREIGN_STRINGS_BENCHMARK_FNV_PRIME 1099511628211ull

typedef struct ForeignStringsBenchmarkVariant ForeignStringsBenchmarkVariant;
struct ForeignStringsBenchmarkVariant
{
    const char* name;
    ForeignPath path;
    bool merge;
};

static const ForeignStringsBenchmarkVariant foreign_strings_benchmark_variants[] = {
    {
        .name = "nomerge-exact",
        .path = FOREIGN_PATH_EXACT,
    },
    {
        .name = "merge-exact",
        .path = FOREIGN_PATH_EXACT,
        .merge = true,
    },
    {
        .name = "nomerge-vector",
        .path = FOREIGN_PATH_VECTOR,
    },
    {
        .name = "merge-vector",
        .path = FOREIGN_PATH_VECTOR,
        .merge = true,
    },
};

static volatile uint64_t foreign_strings_benchmark_scalar_sink;

// A dependency chain of integer shifts, xor and multiplication keeps the preceding work scalar. Checking time once
// per bounded batch avoids spending most of the conditioning interval in the clock. Inspect this function's machine
// code on the approved host before interpreting the conditioned measurements as a light-work control.
static __attribute__((noinline)) void foreign_strings_benchmark_after_scalar(uint32_t milliseconds)
{
    if (milliseconds != 0)
    {
        const uint64_t start = foreign_now();
        const uint64_t duration = (uint64_t)milliseconds * 1000000;
        uint64_t checksum = 0x9e3779b97f4a7c15ull;
        uint64_t iterations = 0;
        do
        {
            #pragma clang loop vectorize(disable) interleave(disable)
            for (uint32_t i = 0; i < 4096; i += 1)
            {
                checksum ^= checksum >> 12;
                checksum ^= checksum << 25;
                checksum ^= checksum >> 27;
                checksum *= 0x2545f4914f6cdd1dull;
            }
            iterations += 4096;
        } while (foreign_now() - start < duration);
        foreign_strings_benchmark_scalar_sink = checksum ^ iterations;
    }
}

static uint64_t foreign_strings_benchmark_ticks(void)
{
    _mm_lfence();
    const uint64_t result = __rdtsc();
    _mm_lfence();
    return result;
}

// A freestanding original program hashes all 32 bytes of every pointed-to literal, writes the 64-bit FNV result,
// and exits zero. The independently computed host result checks all references and contents after every sample.
// Only the pointer table uses string relocations: named global symbols with ABS64 and addend zero. The entry's
// PC32 names the ordinary data table, so it does not depend on section-symbol PC-relative string translation.
static const uint8_t foreign_strings_benchmark_code[] = {
    0x48, 0x8d, 0x35, 0, 0, 0, 0,                         // lea literal_addresses(%rip), %rsi
    0xb9, 0, 0, 0, 0,                                    // mov literal_count, %ecx
    0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,                  // movabs FNV offset, %rax
    0x49, 0xb9, 0, 0, 0, 0, 0, 0, 0, 0,                  // movabs FNV prime, %r9
    0x48, 0x8b, 0x3e,                                    // outer: mov (%rsi), %rdi
    0x48, 0x83, 0xc6, 8,                                 // add $8, %rsi
    0x41, 0xb8, 32, 0, 0, 0,                             // mov $32, %r8d
    0x0f, 0xb6, 0x17,                                    // inner: movzbl (%rdi), %edx
    0x48, 0xff, 0xc7,                                    // inc %rdi
    0x48, 0x31, 0xd0,                                    // xor %rdx, %rax
    0x49, 0x0f, 0xaf, 0xc1,                              // imul %r9, %rax
    0x41, 0xff, 0xc8,                                    // dec %r8d
    0x75, 0xee,                                          // jnz inner
    0xff, 0xc9,                                          // dec %ecx
    0x75, 0xdd,                                          // jnz outer
    0x50,                                                // push %rax
    0xb8, 1, 0, 0, 0,                                    // mov $1, %eax
    0xbf, 1, 0, 0, 0,                                    // mov $1, %edi
    0x48, 0x89, 0xe6,                                    // mov %rsp, %rsi
    0xba, 8, 0, 0, 0,                                    // mov $8, %edx
    0x0f, 0x05,                                          // syscall: write
    0x31, 0xff,                                          // xor %edi, %edi
    0xb8, 60, 0, 0, 0,                                   // mov $60, %eax
    0x0f, 0x05,                                          // syscall: exit
};

typedef struct ForeignStringsBenchmarkInput ForeignStringsBenchmarkInput;
struct ForeignStringsBenchmarkInput
{
    ForeignCommand command;
    uint64_t expected;
    uint64_t input_bytes;
    uint32_t units;
};

static bool foreign_strings_benchmark_generate(const char* directory, uint32_t count, bool duplicate, Arena* arena,
                                               ForeignStringsBenchmarkInput* output)
{
    const uint32_t literal_units = (count + BUSTER_FOREIGN_STRINGS_BENCHMARK_UNIT_LITERALS - 1) / BUSTER_FOREIGN_STRINGS_BENCHMARK_UNIT_LITERALS;
    const uint32_t units = literal_units + 1;
    Arena source = arena_reserve(1ull << 32);
    Arena names = arena_reserve(1ull << 30);
    Link link = { 0 };
    const LinkCapacity capacity = {
        .names = count + 2,
        .atoms = literal_units + 2,
        .symbols = count + 2,
        .fixups = count + 1,
    };
    bool valid = source.base != 0 && names.base != 0 && link_initialize(&link, &source, &names, capacity);
    uint8_t* code = valid ? BUSTER_ARENA_PUSH_ARRAY(&source, uint8_t, sizeof(foreign_strings_benchmark_code)) : 0;
    uint8_t* table = valid ? BUSTER_ARENA_PUSH_ARRAY(&source, uint8_t, (uint64_t)count * sizeof(uint64_t)) : 0;
    valid = valid && code != 0 && table != 0;
    uint64_t expected = BUSTER_FOREIGN_STRINGS_BENCHMARK_FNV_OFFSET;
    AtomId pointers = 0;
    if (valid)
    {
        memcpy(code, foreign_strings_benchmark_code, sizeof(foreign_strings_benchmark_code));
        memcpy(code + 8, &count, sizeof(count));
        const uint64_t offset = BUSTER_FOREIGN_STRINGS_BENCHMARK_FNV_OFFSET;
        const uint64_t prime = BUSTER_FOREIGN_STRINGS_BENCHMARK_FNV_PRIME;
        memcpy(code + 14, &offset, sizeof(offset));
        memcpy(code + 24, &prime, sizeof(prime));
        memset(table, 0, (uint64_t)count * sizeof(uint64_t));
        const AtomId text = link_add_atom(&link, BUSTER_LINK_CLASS_TEXT, 4, sizeof(foreign_strings_benchmark_code), code, 0);
        pointers = link_add_atom(&link, BUSTER_LINK_CLASS_DATA, 3, (uint64_t)count * sizeof(uint64_t), table, 0);
        const NameId entry_name = link_add_name(&link, (const uint8_t*)"_start", 6);
        const NameId table_name = link_add_name(&link, (const uint8_t*)"literal_addresses", 17);
        const SymbolId entry = link_add_symbol(&link, entry_name, text, 0, sizeof(foreign_strings_benchmark_code),
                                              BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_PROGRAM);
        const SymbolId table_symbol = link_add_symbol(&link, table_name, pointers, 0, (uint64_t)count * sizeof(uint64_t),
                                                     BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
        valid = text != 0 && pointers != 0 && entry_name != 0 && table_name != 0 && entry != 0 && table_symbol != 0 &&
                link_add_fixup(&link, text, 3, BUSTER_LINK_FIXUP_PC32, table_symbol, -4) != 0;
    }
    for (uint32_t unit = 0; valid && unit < literal_units; unit += 1)
    {
        const uint32_t first = unit * BUSTER_FOREIGN_STRINGS_BENCHMARK_UNIT_LITERALS;
        const uint32_t in_unit = count - first < BUSTER_FOREIGN_STRINGS_BENCHMARK_UNIT_LITERALS ? count - first : BUSTER_FOREIGN_STRINGS_BENCHMARK_UNIT_LITERALS;
        const uint64_t size = (uint64_t)in_unit * BUSTER_FOREIGN_STRINGS_BENCHMARK_LITERAL_BYTES;
        uint8_t* bytes = BUSTER_ARENA_PUSH_ARRAY(&source, uint8_t, size);
        const AtomId strings = bytes != 0 ? link_add_atom(&link, BUSTER_LINK_CLASS_STRINGS, 0, size, bytes, unit + 1) : 0;
        valid = strings != 0;
        for (uint32_t i = 0; valid && i < in_unit; i += 1)
        {
            const uint32_t index = first + i;
            const uint32_t value = duplicate ? index % 8 : index;
            const uint32_t offset = i * BUSTER_FOREIGN_STRINGS_BENCHMARK_LITERAL_BYTES;
            const int length = snprintf((char*)bytes + offset, BUSTER_FOREIGN_STRINGS_BENCHMARK_LITERAL_BYTES, "lit-%08x-abcdefghijklmnopqr", value);
            char name[32];
            const int name_length = snprintf(name, sizeof(name), "literal_%08x", index);
            const NameId named = link_add_name(&link, (const uint8_t*)name, (uint32_t)name_length);
            const SymbolId symbol = link_add_symbol(&link, named, strings, offset, BUSTER_FOREIGN_STRINGS_BENCHMARK_LITERAL_BYTES,
                                                   BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_OBJECT, BUSTER_LINK_SOURCE_PROGRAM);
            valid = length == BUSTER_FOREIGN_STRINGS_BENCHMARK_LITERAL_BYTES - 1 && named != 0 && symbol != 0 &&
                    link_add_fixup(&link, pointers, index * sizeof(uint64_t), BUSTER_LINK_FIXUP_ABS64, symbol, 0) != 0;
            for (uint32_t b = 0; b < BUSTER_FOREIGN_STRINGS_BENCHMARK_LITERAL_BYTES; b += 1)
            {
                expected = (expected ^ bytes[offset + b]) * BUSTER_FOREIGN_STRINGS_BENCHMARK_FNV_PRIME;
            }
        }
    }
    valid = valid && link_check(&link) == LINK_ERROR_NONE;
    ForeignTestPath* paths = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestPath, units);
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, units);
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "strings_%s_%u", duplicate ? "duplicate" : "unique", count);
    valid = valid && paths != 0 && arguments != 0 && foreign_test_write_program(&link, units, directory, prefix, paths, arena);
    uint64_t input_bytes = 0;
    for (uint32_t unit = 0; valid && unit < units; unit += 1)
    {
        arguments[unit] = paths[unit];
        struct stat status;
        valid = stat(paths[unit], &status) == 0 && status.st_size >= 0;
        input_bytes += valid ? (uint64_t)status.st_size : 0;
    }
    ForeignCommand command = { 0 };
    valid = valid && foreign_test_command(&command, arguments, units, arena);
    if (valid)
    {
        *output = (ForeignStringsBenchmarkInput){
            .command = command,
            .expected = expected,
            .input_bytes = input_bytes,
            .units = units,
        };
    }
    arena_release(&source);
    arena_release(&names);
    return valid;
}

static bool foreign_strings_benchmark_case(const char* directory, uint32_t count, bool duplicate, uint32_t runs, uint32_t after_scalar,
                                           const uint32_t* order, uint32_t variants, Arena* arena)
{
    const uint64_t saved = arena->length;
    ForeignTestTally tally = {
        .check = "foreign string pipeline validation",
    };
    ForeignStringsBenchmarkInput input = { 0 };
    bool valid = foreign_strings_benchmark_generate(directory, count, duplicate, arena, &input);
    foreign_test_expect(&tally, valid, "generate frozen named-relocation string objects", 0);
    uint64_t* times = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)6 * runs);
    uint64_t* ticks = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, runs);
    valid = valid && times != 0 && ticks != 0;
    String8 reference[2] = { 0 };
    for (uint32_t v = 0; valid && v < variants; v += 1)
    {
        const ForeignStringsBenchmarkVariant* variant = &foreign_strings_benchmark_variants[order[v]];
        uint64_t retained = 0;
        uint64_t output_bytes = 0;
        uint64_t literal_bytes = (uint64_t)count * BUSTER_FOREIGN_STRINGS_BENCHMARK_LITERAL_BYTES;
        bool applied = false;
        for (uint32_t run = 0; valid && run <= runs; run += 1)
        {
            const uint64_t iteration = arena->length;
            Arena names = { 0 };
            ForeignTestPath path;
            snprintf(path, sizeof(path), "%s/strings_output_%u_%u_%u", directory, count, duplicate ? 1 : 0, order[v]);
            const String8 destination = {
                .pointer = path,
                .length = strlen(path),
            };
            const bool removed = unlink(path) == 0 || errno == ENOENT;
            Foreign foreign = { 0 };
            ForeignLink converted = { 0 };
            ForeignStringsMerge merged = { 0 };
            foreign_strings_benchmark_after_scalar(after_scalar);
            const uint64_t start_ticks = foreign_strings_benchmark_ticks();
            const uint64_t start = foreign_now();
            names = arena_reserve(1ull << 30);
            foreign_run(&foreign, &input.command, variant->path, 1, 0);
            const uint64_t resolved = foreign_now();
            const ForeignLinkResult built = foreign_link_build(&converted, &foreign, arena, &names);
            const uint64_t converted_at = foreign_now();
            ForeignStringsError merge_error = FOREIGN_STRINGS_ERROR_NONE;
            if (variant->merge && built.error == FOREIGN_LINK_ERROR_NONE)
            {
                merge_error = foreign_strings_merge(&converted, &foreign, arena, &merged);
            }
            const uint64_t merged_at = foreign_now();
            const SymbolId entry = built.error == FOREIGN_LINK_ERROR_NONE ? foreign_link_test_symbol(&foreign, &converted, "_start") : 0;
            ExecutableResult written = { 0 };
            bool emitted = false;
            if (removed && names.base != 0 && built.error == FOREIGN_LINK_ERROR_NONE && merge_error == FOREIGN_STRINGS_ERROR_NONE)
            {
                emitted = variant->path == FOREIGN_PATH_EXACT ? executable_test_write_exact(&converted.link, entry, destination, 1, &written) :
                                                              executable_test_write(&converted.link, entry, destination, 1, &written, 0, 0);
            }
            const uint64_t written_at = foreign_now();
            const unsigned resolve_error = (unsigned)foreign.failure.error;
            valid = foreign.failure.error == FOREIGN_ERROR_NONE && built.error == FOREIGN_LINK_ERROR_NONE &&
                    merge_error == FOREIGN_STRINGS_ERROR_NONE && emitted;
            if (valid)
            {
                retained = arena->length - iteration + names.length + foreign.arena.length;
                for (uint32_t lane = 0; lane < foreign.lanes; lane += 1)
                {
                    retained += foreign.lane_states[lane].arena.length;
                }
                output_bytes = written.file_size;
                literal_bytes = merged.applied ? merged.strings.size : (uint64_t)count * BUSTER_FOREIGN_STRINGS_BENCHMARK_LITERAL_BYTES;
                applied = merged.applied;
            }
            arena_release(&names);
            foreign_release(&foreign);
            arena_pop_to(arena, iteration);
            const uint64_t finished = foreign_now();
            const uint64_t finished_ticks = foreign_strings_benchmark_ticks();
            char detail[256];
            snprintf(detail, sizeof(detail), "%s count %u %s iteration %u: resolve %u convert %u import %u merge %u write %u",
                     duplicate ? "duplicate" : "unique", count, variant->name, run, resolve_error,
                     (unsigned)built.error, (unsigned)built.import.error, (unsigned)merge_error, (unsigned)written.error);
            foreign_test_expect(&tally, valid, "whole string pipeline succeeds", detail);
            const uint64_t validation = arena->length;
            if (valid)
            {
                if (run != 0)
                {
                    times[run - 1] = resolved - start;
                    times[runs + run - 1] = converted_at - resolved;
                    times[2 * runs + run - 1] = merged_at - converted_at;
                    times[3 * runs + run - 1] = written_at - merged_at;
                    times[4 * runs + run - 1] = finished - written_at;
                    times[5 * runs + run - 1] = finished - start;
                    ticks[run - 1] = finished_ticks - start_ticks;
                }
                const bool ran = executable_test_runs(path, (const uint8_t*)&input.expected, sizeof(input.expected));
                foreign_test_expect(&tally, ran, "every literal address and byte matches independent checksum", detail);
                const String8 bytes = foreign_test_read(arena, path);
                const uint32_t state = variant->merge && duplicate && count > 8 ? 1 : 0;
                const bool same = bytes.pointer != 0 && (reference[state].pointer == 0 ||
                                  (bytes.length == reference[state].length && memcmp(bytes.pointer, reference[state].pointer, bytes.length) == 0));
                foreign_test_expect(&tally, same, "complete bytes agree within each merge state across paths and repeats", detail);
                const bool expected_merge = variant->merge && duplicate && count > 8;
                foreign_test_expect(&tally, applied == expected_merge, "duplicate pool applies and unique control remains unchanged", detail);
                valid = ran && same && applied == expected_merge;
            }
            arena_pop_to(arena, validation);
            const uint32_t state = variant->merge && duplicate && count > 8 ? 1 : 0;
            if (valid && reference[state].pointer == 0)
            {
                reference[state] = foreign_test_read(arena, path);
                valid = reference[state].pointer != 0;
                foreign_test_expect(&tally, valid, "retain executable reference outside timing", detail);
            }
        }
        if (valid)
        {
            double medians[6];
            for (uint32_t phase = 0; phase < BUSTER_ARRAY_LENGTH(medians); phase += 1)
            {
                medians[phase] = foreign_link_test_median(times + phase * runs, runs);
            }
            qsort(ticks, runs, sizeof(*ticks), foreign_link_test_compare_time);
            printf("foreign-strings %s count %u units %u %s runs %u: resolve %.1f us convert %.1f us merge %.1f us write %.1f us cleanup %.1f us total %.1f us wall %llu ns TSCticks %llu after-scalar %u ms input %llu bytes output %llu bytes literal-input %llu bytes literal-output %llu bytes retained-arena %llu bytes applied %u\n",
                   duplicate ? "duplicate" : "unique", count, input.units, variant->name, runs,
                   medians[0], medians[1], medians[2], medians[3], medians[4], medians[5], (unsigned long long)times[5 * runs + runs / 2],
                   (unsigned long long)ticks[runs / 2], after_scalar, (unsigned long long)input.input_bytes,
                   (unsigned long long)output_bytes, (unsigned long long)((uint64_t)count * BUSTER_FOREIGN_STRINGS_BENCHMARK_LITERAL_BYTES),
                   (unsigned long long)literal_bytes, (unsigned long long)retained, applied ? 1 : 0);
        }
    }
    foreign_test_expect(&tally, valid, "all requested variants completed", 0);
    const bool passed = foreign_test_report(&tally);
    arena_pop_to(arena, saved);
    return passed && valid;
}

static bool foreign_strings_benchmark_counts(const char* text, uint32_t* counts, uint32_t* count)
{
    const char* at = text;
    uint32_t added = 0;
    bool valid = *at != 0;
    while (valid && *at != 0)
    {
        char* end = 0;
        const uint64_t value = strtoull(at, &end, 10);
        valid = end != at && value > 0 && value <= BUSTER_FOREIGN_STRINGS_BENCHMARK_MAX_LITERALS && added < 16 && (*end == 0 || *end == ',');
        if (valid)
        {
            counts[added] = (uint32_t)value;
            added += 1;
            at = *end == ',' ? end + 1 : end;
            valid = *end != ',' || *at != 0;
        }
    }
    *count = valid ? added : 0;
    return valid;
}

static bool foreign_strings_benchmark_order(const char* text, uint32_t* order, uint32_t* count)
{
    const char* at = text;
    uint32_t added = 0;
    uint32_t mask = 0;
    bool valid = *at != 0;
    while (valid && *at != 0)
    {
        const char* comma = strchr(at, ',');
        const uint64_t length = comma != 0 ? (uint64_t)(comma - at) : strlen(at);
        uint32_t found = BUSTER_ARRAY_LENGTH(foreign_strings_benchmark_variants);
        for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(foreign_strings_benchmark_variants); v += 1)
        {
            const char* name = foreign_strings_benchmark_variants[v].name;
            if (strlen(name) == length && memcmp(at, name, length) == 0)
            {
                found = v;
            }
        }
        valid = found < BUSTER_ARRAY_LENGTH(foreign_strings_benchmark_variants) && (mask & (1u << found)) == 0;
        if (valid)
        {
            order[added] = found;
            added += 1;
            mask |= 1u << found;
            at = comma != 0 ? comma + 1 : at + length;
            valid = comma == 0 || *at != 0;
        }
    }
    *count = valid ? added : 0;
    return valid;
}

static __attribute__((unused)) int run_foreign_strings_benchmark(int argc, char** argv)
{
    uint32_t counts[16] = { 64, 4096, 131072 };
    uint32_t count = 3;
    uint32_t order[4] = { 0, 1, 2, 3 };
    uint32_t variants = 4;
    uint32_t runs = 11;
    uint32_t after_scalar = 0;
    uint32_t cases = 3;
    bool keep = false;
    bool valid = true;
    const char* base = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool value = i + 1 < argc;
        if ((strcmp(argv[i], "--counts") == 0 || strcmp(argv[i], "--count") == 0) && value)
        {
            valid = foreign_strings_benchmark_counts(argv[i + 1], counts, &count);
            i += 1;
        }
        else if (strcmp(argv[i], "--runs") == 0 && value)
        {
            char* end = 0;
            const uint64_t number = strtoull(argv[i + 1], &end, 10);
            valid = end != argv[i + 1] && *end == 0 && number > 0 && number <= 1024;
            runs = valid ? (uint32_t)number : runs;
            i += 1;
        }
        else if (strcmp(argv[i], "--order") == 0 && value)
        {
            valid = foreign_strings_benchmark_order(argv[i + 1], order, &variants);
            i += 1;
        }
        else if (strcmp(argv[i], "--after-scalar") == 0 && value)
        {
            char* end = 0;
            const uint64_t number = strtoull(argv[i + 1], &end, 10);
            valid = argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9' && *end == 0 && number <= 300;
            after_scalar = valid ? (uint32_t)number : after_scalar;
            i += 1;
        }
        else if (strcmp(argv[i], "--only") == 0 && value)
        {
            cases = strcmp(argv[i + 1], "duplicate") == 0 ? 1 : strcmp(argv[i + 1], "unique") == 0 ? 2 : 0;
            valid = cases != 0;
            i += 1;
        }
        else if (strcmp(argv[i], "--directory") == 0 && value)
        {
            base = argv[i + 1];
            i += 1;
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
    ForeignTestPath directory;
    const int length = snprintf(directory, sizeof(directory), "%s/buster-foreign-strings-XXXXXX", base);
    valid = valid && length > 0 && (uint64_t)length < sizeof(directory);
    Arena arena = arena_reserve(BUSTER_FOREIGN_TEST_ARENA_BYTES);
    bool passed = false;
    if (!valid)
    {
        printf("usage: benchmark-foreign-strings [--counts LIST] [--runs N] [--order nomerge-exact,merge-exact,nomerge-vector,merge-vector] [--only duplicate|unique] [--after-scalar MS <= 300] [--directory PATH] [--keep]\n");
    }
    else if (arena.base == 0 || mkdtemp(directory) == 0)
    {
        printf("benchmark-foreign-strings: no room for the arena or output directory\n");
    }
    else
    {
        passed = true;
        for (uint32_t p = 0; passed && p < count; p += 1)
        {
            for (uint32_t c = 0; passed && c < 2; c += 1)
            {
                if ((cases & (1u << c)) != 0)
                {
                    passed = foreign_strings_benchmark_case(directory, counts[p], c == 0, runs, after_scalar, order, variants, &arena);
                }
            }
        }
        if (!keep && passed)
        {
            char* remove[] = {
                (char*)"rm",
                (char*)"-rf",
                directory,
                0,
            };
            object_test_run(remove, "/dev/null", "/dev/null");
        }
        else
        {
            printf("benchmark-foreign-strings: kept %s\n", directory);
        }
    }
    arena_release(&arena);
    return passed ? 0 : 1;
}
