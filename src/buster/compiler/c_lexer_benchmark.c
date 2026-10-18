// The C lexer benchmark: checks that c_tokenize (c_lexer.c) agrees with its scalar lexer on every file of a corpus of
// C or C++ sources, then times both. It is a port of the corpus check and the benchmark of c/c_tokenizer_test.c in
// https://github.com/davidgmbb/Accelerated-Zig-Parser (commit f4482e2dd53413522d75d8c254b9a824021238ce, which
// c_lexer.c also comes from) and prints the same lines for them, so its numbers compare directly with upstream's.
//
// Usage: build/compiler benchmark-lexer [--cplusplus] path...
//
// Paths are files or directories. Directories are searched for .c and .h files, or with --cplusplus for C++ sources
// and headers (including extensionless text files like <vector>), and --cplusplus tokenizes the corpus as C++. Run it
// pinned to one CPU (`taskset -c 2 ...`) for stable numbers. The exit status is 1 if a path can't be read or a file
// tokenizes differently on the vectorized and scalar paths.
//
// Layout, in this order:
//   - Arena: all memory, reserved up front and committed as it's written.
//   - Corpus loading: source_kind, add_file and add_path. add_path walks a directory tree with a stack of open
//     directories (DirectoryFrame) and descends into a subdirectory as soon as it finds it, so the files come in the
//     order of upstream's recursive walk. Every file is copied into a buffer aligned to
//     BUSTER_BENCHMARK_BUFFER_ALIGNMENT, as upstream's aligned_alloc does.
//   - The corpus check: compare, print_token and check_corpus.
//   - The benchmark: open_counter, sample_now, tokenize_corpus and benchmark, which runs a warm-up pass and then
//     BUSTER_BENCHMARK_PASSES passes with c_tokenize and c_tokenize_scalar taking turns, each with its own output
//     arrays, and reports the medians of wall time and of user-space cycles and instructions (perf_event_open).
//   - run_c_lexer_benchmark, called by main.c.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define BUSTER_ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))

// OS-dependent includes
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "buster/compiler/c_lexer.c"

// Timed passes over the corpus, after one warm-up pass.
#define BUSTER_BENCHMARK_PASSES 21
// The alignment of each file's copy and of each output array: a cache line.
#define BUSTER_BENCHMARK_BUFFER_ALIGNMENT 64
// Address space reserved for the file contents, paths and output arrays, and for the array of files.
#define BUSTER_BENCHMARK_MEMORY_RESERVE (16ull << 30)
#define BUSTER_BENCHMARK_SOURCES_RESERVE (1ull << 30)
// An extensionless file only counts as a C++ header if its first bytes have no NUL byte.
#define BUSTER_BENCHMARK_TEXT_CHECK_BYTES 4096
// The corpus check prints the first difference of this many files, with this many tokens of context on either side
// and this many bytes of each token.
#define BUSTER_BENCHMARK_REPORTED_FILES 10
#define BUSTER_BENCHMARK_CONTEXT_TOKENS 2
#define BUSTER_BENCHMARK_TOKEN_PREVIEW_BYTES 40

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

// ---------------------------------------------------------------------------------------------------------------------
// Arena

typedef struct Arena Arena;
struct Arena
{
    uint8_t* base;
    uint64_t position;
    uint64_t capacity;
};

// Reserves `capacity` bytes of address space. The pages are committed as they're first written.
static bool arena_initialize(Arena* arena, uint64_t capacity)
{
    void* base = mmap(0, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    bool result = base != MAP_FAILED;
    if (result)
    {
        *arena = (Arena){ .base = base, .position = 0, .capacity = capacity };
    }
    return result;
}

// `size` bytes aligned to `alignment` (a power of two), or null if the arena is out of space.
static void* arena_allocate(Arena* arena, uint64_t size, uint64_t alignment)
{
    void* result = 0;
    uint64_t start = (arena->position + alignment - 1) & ~(alignment - 1);
    if (start <= arena->capacity && size <= arena->capacity - start)
    {
        result = arena->base + start;
        arena->position = start + size;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Corpus loading

typedef struct Source Source;
struct Source
{
    String8 path;
    String8 bytes;
};

typedef struct CorpusLoader CorpusLoader;
struct CorpusLoader
{
    // File contents, paths, directory frames and the output arrays.
    Arena memory;
    // Only the Source records, so that they form one array.
    Arena sources;
    uint64_t total_bytes;
    uint64_t max_length;
    bool cplusplus;
};

typedef struct Corpus Corpus;
struct Corpus
{
    const Source* sources;
    uint64_t count;
    uint64_t total_bytes;
    uint64_t max_length;
};

// A directory being walked, with the one it's in. `path` is NUL-terminated, as are all paths here, for libc.
typedef struct DirectoryFrame DirectoryFrame;
struct DirectoryFrame
{
    DirectoryFrame* parent;
    DIR* directory;
    String8 path;
};

typedef enum SourceKind
{
    SOURCE_KIND_NONE,
    SOURCE_KIND_SOURCE,
    // An extensionless file in C++, which is a header (like <vector>) if it's text.
    SOURCE_KIND_IF_TEXT,
} SourceKind;

static bool ends_with(const char* name, const char* suffix)
{
    uint64_t name_length = strlen(name);
    uint64_t suffix_length = strlen(suffix);
    return name_length >= suffix_length && memcmp(name + name_length - suffix_length, suffix, suffix_length) == 0;
}

static SourceKind source_kind(const char* name, bool cplusplus)
{
    static const char* const cplusplus_extensions[] = { ".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h++", ".h", ".inc", ".def", ".ipp", ".tcc", ".tpp" };
    SourceKind result = SOURCE_KIND_NONE;
    if (cplusplus)
    {
        for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(cplusplus_extensions); i += 1)
        {
            if (ends_with(name, cplusplus_extensions[i]))
            {
                result = SOURCE_KIND_SOURCE;
            }
        }
        if (result == SOURCE_KIND_NONE && strchr(name, '.') == 0)
        {
            result = SOURCE_KIND_IF_TEXT;
        }
    }
    else if (ends_with(name, ".c") || ends_with(name, ".h"))
    {
        result = SOURCE_KIND_SOURCE;
    }
    return result;
}

// "directory/name" in the arena, or a null pointer if it's out of space.
static String8 join_path(Arena* arena, String8 directory, const char* name)
{
    uint64_t name_length = strlen(name);
    uint64_t length = directory.length + 1 + name_length;
    char* pointer = arena_allocate(arena, length + 1, 1);
    if (pointer != 0)
    {
        memcpy(pointer, directory.pointer, directory.length);
        pointer[directory.length] = '/';
        memcpy(pointer + directory.length + 1, name, name_length + 1);
    }
    return (String8){ .pointer = pointer, .length = pointer != 0 ? length : 0 };
}

static void report_out_of_memory(String8 path)
{
    fprintf(stderr, "out of memory reading %s\n", path.pointer);
}

// Copies a regular file into the arena and adds it to the corpus, unless `only_text` and it isn't text (it's empty or
// has a NUL in its first bytes). A file that can't be opened is skipped, as upstream does. Returns false if the arena
// runs out of space.
static bool add_file(CorpusLoader* loader, String8 path, bool only_text)
{
    bool result = true;
    int fd = open(path.pointer, O_RDONLY | O_CLOEXEC);
    if (fd != -1)
    {
        struct stat file_stats;
        if (fstat(fd, &file_stats) == 0 && S_ISREG(file_stats.st_mode))
        {
            uint64_t length = (uint64_t)file_stats.st_size;
            uint64_t position = loader->memory.position;
            char* bytes = arena_allocate(&loader->memory, length, BUSTER_BENCHMARK_BUFFER_ALIGNMENT);
            if (bytes == 0)
            {
                report_out_of_memory(path);
                result = false;
            }
            else
            {
                uint64_t read_bytes = 0;
                ssize_t n = 1;
                while (n > 0 && read_bytes < length)
                {
                    n = read(fd, bytes + read_bytes, length - read_bytes);
                    read_bytes += n > 0 ? (uint64_t)n : 0;
                }
                uint64_t checked = read_bytes < BUSTER_BENCHMARK_TEXT_CHECK_BYTES ? read_bytes : BUSTER_BENCHMARK_TEXT_CHECK_BYTES;
                if (only_text && (read_bytes == 0 || memchr(bytes, 0, checked) != 0))
                {
                    loader->memory.position = position;
                }
                else
                {
                    Source* source = arena_allocate(&loader->sources, sizeof(Source), _Alignof(Source));
                    if (source == 0)
                    {
                        report_out_of_memory(path);
                        result = false;
                    }
                    else
                    {
                        *source = (Source){ .path = path, .bytes = { .pointer = bytes, .length = read_bytes } };
                        loader->total_bytes += read_bytes;
                        loader->max_length = read_bytes > loader->max_length ? read_bytes : loader->max_length;
                    }
                }
            }
        }
        close(fd);
    }
    return result;
}

// Adds a file, or the sources in a directory tree (skipping names that start with '.', and symbolic links). Returns
// false if the path doesn't exist or the arena runs out of space.
static bool add_path(CorpusLoader* loader, String8 path)
{
    bool result = true;
    struct stat path_stats;
    if (stat(path.pointer, &path_stats) != 0)
    {
        fprintf(stderr, "can't open %s\n", path.pointer);
        result = false;
    }
    else if (!S_ISDIR(path_stats.st_mode))
    {
        result = add_file(loader, path, false);
    }
    else
    {
        DirectoryFrame* frame = 0;
        DIR* root = opendir(path.pointer);
        if (root != 0)
        {
            frame = arena_allocate(&loader->memory, sizeof(DirectoryFrame), _Alignof(DirectoryFrame));
            if (frame == 0)
            {
                report_out_of_memory(path);
                closedir(root);
                result = false;
            }
            else
            {
                *frame = (DirectoryFrame){ .parent = 0, .directory = root, .path = path };
            }
        }

        // After a failure, the remaining directories are closed without reading them.
        while (frame != 0)
        {
            struct dirent* entry = result ? readdir(frame->directory) : 0;
            if (entry == 0)
            {
                closedir(frame->directory);
                frame = frame->parent;
            }
            else if (entry->d_name[0] != '.')
            {
                String8 child = join_path(&loader->memory, frame->path, entry->d_name);
                struct stat child_stats;
                // An entry that is gone by now is skipped, as upstream does.
                const bool exists = child.pointer != 0 && lstat(child.pointer, &child_stats) == 0;
                if (child.pointer == 0)
                {
                    report_out_of_memory(frame->path);
                    result = false;
                }
                else if (exists && S_ISDIR(child_stats.st_mode))
                {
                    DIR* directory = opendir(child.pointer);
                    if (directory != 0)
                    {
                        DirectoryFrame* child_frame = arena_allocate(&loader->memory, sizeof(DirectoryFrame), _Alignof(DirectoryFrame));
                        if (child_frame == 0)
                        {
                            report_out_of_memory(child);
                            closedir(directory);
                            result = false;
                        }
                        else
                        {
                            *child_frame = (DirectoryFrame){ .parent = frame, .directory = directory, .path = child };
                            frame = child_frame;
                        }
                    }
                }
                else if (exists && S_ISREG(child_stats.st_mode))
                {
                    SourceKind kind = source_kind(entry->d_name, loader->cplusplus);
                    if (kind != SOURCE_KIND_NONE)
                    {
                        result = add_file(loader, child, kind == SOURCE_KIND_IF_TEXT);
                    }
                }
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The corpus check

typedef struct TokenArrays TokenArrays;
struct TokenArrays
{
    uint8_t* kinds;
    uint32_t* starts;
    uint32_t* ends;
};

typedef enum Comparison
{
    COMPARISON_SAME,
    COMPARISON_DIFFERENT,
    // The same, but the vectorized path left a part of the source to the scalar lexer.
    COMPARISON_FELL_BACK,
} Comparison;

// Output arrays for tokenizing up to `length` bytes, or a null kinds array if the arena is out of space.
static TokenArrays allocate_token_arrays(Arena* arena, uint64_t length)
{
    uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(length);
    TokenArrays result;
    result.kinds = arena_allocate(arena, capacity * sizeof(*result.kinds), BUSTER_BENCHMARK_BUFFER_ALIGNMENT);
    result.starts = arena_allocate(arena, capacity * sizeof(*result.starts), BUSTER_BENCHMARK_BUFFER_ALIGNMENT);
    result.ends = arena_allocate(arena, capacity * sizeof(*result.ends), BUSTER_BENCHMARK_BUFFER_ALIGNMENT);
    if (result.starts == 0 || result.ends == 0)
    {
        result.kinds = 0;
    }
    return result;
}

// Prints token `i` of `count` (or "-" if there's no such token): its kind, position and first bytes.
static void print_token(String8 source, const TokenArrays* tokens, int64_t i, int64_t count)
{
    if (i < 0 || i >= count)
    {
        printf("-");
    }
    else
    {
        uint32_t start = tokens->starts[i];
        uint32_t end = tokens->ends[i];
        uint64_t preview_end = (uint64_t)start + BUSTER_BENCHMARK_TOKEN_PREVIEW_BYTES;
        printf("%s%s [%u, %u) \"", c_token_kind_name(tokens->kinds[i]), (tokens->kinds[i] & BUSTER_C_TOKEN_START_OF_LINE) ? " SOL" : "", start, end);
        for (uint64_t p = start; p < end && p < preview_end && p < source.length; p += 1)
        {
            uint8_t c = (uint8_t)source.pointer[p];
            if (c >= 32 && c < 127 && c != '\\')
            {
                putchar(c);
            }
            else
            {
                printf("\\x%02X", c);
            }
        }
        printf("\"");
    }
}

// Compares c_tokenize with the scalar lexer on `source`, and prints where they differ if `name` isn't null.
static Comparison compare(String8 source, TokenArrays* vectorized, TokenArrays* scalar, uint32_t flags, const char* name)
{
    const uint8_t* bytes = (const uint8_t*)source.pointer;
    const bool fell_back = c_tokenize_vectorized(bytes, source.length, vectorized->kinds, vectorized->starts, vectorized->ends, flags) ==
                           C_TOKENIZER_ERROR_NEEDS_SCALAR;
    int64_t vectorized_count = c_tokenize(bytes, source.length, vectorized->kinds, vectorized->starts, vectorized->ends, flags);
    int64_t scalar_count = c_tokenize_scalar(bytes, source.length, scalar->kinds, scalar->starts, scalar->ends, flags);
    int64_t i = 0;
    while (i < vectorized_count && i < scalar_count && vectorized->kinds[i] == scalar->kinds[i] && vectorized->starts[i] == scalar->starts[i] &&
           vectorized->ends[i] == scalar->ends[i])
    {
        i += 1;
    }

    Comparison result;
    if (i == vectorized_count && i == scalar_count)
    {
        result = fell_back ? COMPARISON_FELL_BACK : COMPARISON_SAME;
    }
    else
    {
        result = COMPARISON_DIFFERENT;
        if (name != 0)
        {
            printf("%s: token %ld differs (vectorized has %ld tokens, scalar %ld)\n", name, (long)i, (long)vectorized_count, (long)scalar_count);
            for (int64_t j = i - BUSTER_BENCHMARK_CONTEXT_TOKENS; j <= i + BUSTER_BENCHMARK_CONTEXT_TOKENS; j += 1)
            {
                if (j >= 0)
                {
                    printf("  %s vectorized: ", j == i ? ">" : " ");
                    print_token(source, vectorized, j, vectorized_count);
                    printf("\n      scalar:     ");
                    print_token(source, scalar, j, scalar_count);
                    printf("\n");
                }
            }
        }
    }
    return result;
}

// Compares the two paths on every file, and returns how many files they differ on.
static uint64_t check_corpus(const Corpus* corpus, TokenArrays* vectorized, TokenArrays* scalar, uint32_t flags)
{
    uint64_t different = 0;
    uint64_t fell_back = 0;
    uint64_t fell_back_bytes = 0;
    for (uint64_t i = 0; i < corpus->count; i += 1)
    {
        const Source* source = &corpus->sources[i];
        const char* name = different < BUSTER_BENCHMARK_REPORTED_FILES ? source->path.pointer : 0;
        Comparison comparison = compare(source->bytes, vectorized, scalar, flags, name);
        different += comparison == COMPARISON_DIFFERENT;
        if (comparison == COMPARISON_FELL_BACK)
        {
            fell_back += 1;
            fell_back_bytes += source->bytes.length;
        }
    }
    printf("corpus: %lu files differ, %lu files (%.2f%% of bytes) need the scalar lexer for a part\n", (unsigned long)different,
           (unsigned long)fell_back, corpus->total_bytes ? 100.0 * (double)fell_back_bytes / (double)corpus->total_bytes : 0.0);
    return different;
}

// ---------------------------------------------------------------------------------------------------------------------
// The benchmark

typedef enum Implementation
{
    IMPLEMENTATION_VECTORIZED,
    IMPLEMENTATION_SCALAR,
    IMPLEMENTATION_COUNT,
} Implementation;

static const char* const implementation_names[IMPLEMENTATION_COUNT] = { "c_tokenize", "c_tokenize_scalar" };

typedef struct PerfCounters PerfCounters;
struct PerfCounters
{
    int cycles_fd;
    int instructions_fd;
};

typedef struct Sample Sample;
struct Sample
{
    uint64_t nanoseconds;
    uint64_t cycles;
    uint64_t instructions;
};

// A counter of this thread's user-space hardware events, or -1 if perf_event_open fails.
static int open_counter(uint64_t config)
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

// The counter's value, or 0 if it isn't open.
static uint64_t read_counter(int fd)
{
    uint64_t value = 0;
    if (fd >= 0 && read(fd, &value, sizeof(value)) != sizeof(value))
    {
        value = 0;
    }
    return value;
}

static Sample sample_now(const PerfCounters* counters)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    Sample result = {
        .nanoseconds = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec,
        .cycles = read_counter(counters->cycles_fd),
        .instructions = read_counter(counters->instructions_fd),
    };
    return result;
}

static void sort_u64(uint64_t* values, uint64_t count)
{
    for (uint64_t i = 1; i < count; i += 1)
    {
        uint64_t value = values[i];
        uint64_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
}

// Tokenizes every file of the corpus with one implementation, and returns the number of tokens.
static uint64_t tokenize_corpus(const Corpus* corpus, Implementation implementation, TokenArrays* tokens, uint32_t flags)
{
    uint64_t result = 0;
    for (uint64_t i = 0; i < corpus->count; i += 1)
    {
        const uint8_t* bytes = (const uint8_t*)corpus->sources[i].bytes.pointer;
        uint64_t length = corpus->sources[i].bytes.length;
        int64_t count;
        if (implementation == IMPLEMENTATION_VECTORIZED)
        {
            count = c_tokenize(bytes, length, tokens->kinds, tokens->starts, tokens->ends, flags);
        }
        else
        {
            count = c_tokenize_scalar(bytes, length, tokens->kinds, tokens->starts, tokens->ends, flags);
        }
        result += (uint64_t)count;
    }
    return result;
}

// Times the implementations on the corpus, taking turns, and prints the medians. `outputs` has an entry per
// implementation. The corpus must not be empty.
static void benchmark(const Corpus* corpus, TokenArrays* outputs, uint32_t flags)
{
    PerfCounters counters = { .cycles_fd = open_counter(PERF_COUNT_HW_CPU_CYCLES), .instructions_fd = open_counter(PERF_COUNT_HW_INSTRUCTIONS) };
    if (counters.cycles_fd < 0 || counters.instructions_fd < 0)
    {
        fprintf(stderr, "perf_event_open failed (see /proc/sys/kernel/perf_event_paranoid): cycles and instructions read as 0\n");
    }

    Sample samples[IMPLEMENTATION_COUNT][BUSTER_BENCHMARK_PASSES];
    uint64_t tokens = 0;
    for (uint64_t pass = 0; pass <= BUSTER_BENCHMARK_PASSES; pass += 1)
    {
        for (uint64_t implementation = 0; implementation < IMPLEMENTATION_COUNT; implementation += 1)
        {
            Sample before = sample_now(&counters);
            tokens = tokenize_corpus(corpus, (Implementation)implementation, &outputs[implementation], flags);
            Sample after = sample_now(&counters);
            if (pass > 0)
            {
                samples[implementation][pass - 1] = (Sample){
                    .nanoseconds = after.nanoseconds - before.nanoseconds,
                    .cycles = after.cycles - before.cycles,
                    .instructions = after.instructions - before.instructions,
                };
            }
        }
    }

    printf("%d interleaved passes over %lu tokens, medians (cycles/B also min-max):\n", BUSTER_BENCHMARK_PASSES, (unsigned long)tokens);
    for (uint64_t implementation = 0; implementation < IMPLEMENTATION_COUNT; implementation += 1)
    {
        uint64_t nanoseconds[BUSTER_BENCHMARK_PASSES];
        uint64_t cycles[BUSTER_BENCHMARK_PASSES];
        uint64_t instructions[BUSTER_BENCHMARK_PASSES];
        for (uint64_t pass = 0; pass < BUSTER_BENCHMARK_PASSES; pass += 1)
        {
            nanoseconds[pass] = samples[implementation][pass].nanoseconds;
            cycles[pass] = samples[implementation][pass].cycles;
            instructions[pass] = samples[implementation][pass].instructions;
        }
        sort_u64(nanoseconds, BUSTER_BENCHMARK_PASSES);
        sort_u64(cycles, BUSTER_BENCHMARK_PASSES);
        sort_u64(instructions, BUSTER_BENCHMARK_PASSES);
        double bytes = (double)corpus->total_bytes;
        double median_nanoseconds = (double)nanoseconds[BUSTER_BENCHMARK_PASSES / 2];
        double median_cycles = (double)cycles[BUSTER_BENCHMARK_PASSES / 2];
        double median_instructions = (double)instructions[BUSTER_BENCHMARK_PASSES / 2];
        printf("  %-18s %8.3f ms  %6.2f GB/s  %6.3f cycles/B [%.3f-%.3f]  %6.3f instructions/B  IPC %.2f\n", implementation_names[implementation],
               median_nanoseconds / 1e6, bytes / median_nanoseconds, median_cycles / bytes, (double)cycles[0] / bytes,
               (double)cycles[BUSTER_BENCHMARK_PASSES - 1] / bytes, median_instructions / bytes, median_cycles != 0 ? median_instructions / median_cycles : 0.0);
    }

    if (counters.cycles_fd >= 0)
    {
        close(counters.cycles_fd);
    }
    if (counters.instructions_fd >= 0)
    {
        close(counters.instructions_fd);
    }
}

int run_c_lexer_benchmark(int argc, char** argv)
{
    int result = 1;
    CorpusLoader loader = {0};
    bool valid_options = true;
    uint64_t path_count = 0;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--cplusplus") == 0)
        {
            loader.cplusplus = true;
        }
        else if (argv[i][0] == '-')
        {
            valid_options = false;
        }
        else
        {
            path_count += 1;
        }
    }

    if (!valid_options || path_count == 0)
    {
        fprintf(stderr, "usage: %s [--cplusplus] path...\n", argv[0]);
    }
    else if (!arena_initialize(&loader.memory, BUSTER_BENCHMARK_MEMORY_RESERVE) || !arena_initialize(&loader.sources, BUSTER_BENCHMARK_SOURCES_RESERVE))
    {
        fprintf(stderr, "can't reserve memory\n");
    }
    else
    {
        bool loaded = true;
        for (int i = 1; loaded && i < argc; i += 1)
        {
            if (argv[i][0] != '-')
            {
                loaded = add_path(&loader, (String8){ .pointer = argv[i], .length = strlen(argv[i]) });
            }
        }

        TokenArrays outputs[IMPLEMENTATION_COUNT];
        for (uint64_t implementation = 0; loaded && implementation < IMPLEMENTATION_COUNT; implementation += 1)
        {
            outputs[implementation] = allocate_token_arrays(&loader.memory, loader.max_length);
            if (outputs[implementation].kinds == 0)
            {
                fprintf(stderr, "out of memory for the output arrays\n");
                loaded = false;
            }
        }

        if (loaded)
        {
            const Corpus corpus = {
                .sources = (const Source*)loader.sources.base,
                .count = loader.sources.position / sizeof(Source),
                .total_bytes = loader.total_bytes,
                .max_length = loader.max_length,
            };
            const uint32_t flags = loader.cplusplus ? C_TOKENIZER_CPLUSPLUS : 0;
            printf("%lu files, %lu bytes\n", (unsigned long)corpus.count, (unsigned long)corpus.total_bytes);
            uint64_t different = check_corpus(&corpus, &outputs[IMPLEMENTATION_VECTORIZED], &outputs[IMPLEMENTATION_SCALAR], flags);
            if (corpus.total_bytes != 0)
            {
                benchmark(&corpus, outputs, flags);
            }
            result = different == 0 ? 0 : 1;
        }
    }
    return result;
}
