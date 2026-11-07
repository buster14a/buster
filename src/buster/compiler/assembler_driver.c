// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Assembly input/output boundary; the reusable frontend itself does no I/O.

static Object asm_object(AsmResult* result, String8 name, Arena* arena)
{
    ObjectUnits units;
    ObjectSymbols symbols;
    Object object = {.error = OBJECT_ERROR_FULL};
    if (result->error == 0 && object_prepare(&units, &result->link, 1, arena) && object_map_symbols(&symbols, &result->link, arena))
    {
        object = object_build(&result->link, &units, &symbols, 0, result->object_options, result->source_name.pointer != 0 ? result->source_name : name, arena);
    }
    return object;
}

// Size ordinary input from the file, but read to EOF: a pipe or /proc file can report zero, and a file can grow.
// This is the arena's last allocation throughout the read, so growth appends without moving or copying the bytes.
// Keep one terminator byte; a full buffer probes EOF before growing, including at the representation/arena limit.
static bool asm_read_descriptor(int fd, Arena* arena, String8* source)
{
    const uint64_t saved = arena->length;
    struct stat stats;
    const bool sized = fstat(fd, &stats) == 0 && (!S_ISREG(stats.st_mode) || (uint64_t)stats.st_size <= BUSTER_ASM_MAX_SOURCE);
    const uint64_t available = saved < arena->capacity ? arena->capacity - saved - 1 : 0;
    const uint64_t limit = available < BUSTER_ASM_MAX_SOURCE ? available : BUSTER_ASM_MAX_SOURCE;
    uint64_t capacity = sized && S_ISREG(stats.st_mode) && stats.st_size > 0 ? (uint64_t)stats.st_size : 1u << 16;
    capacity = capacity < limit ? capacity : limit;
    *source = (String8){.pointer = sized ? arena_push(arena, capacity + 1, 1) : 0};
    bool result = source->pointer != 0;
    bool done = false;
    while (result && !done)
    {
        uint8_t extra;
        const bool full = source->length == capacity;
        const ssize_t count = read(fd, full ? &extra : (uint8_t*)source->pointer + source->length, full ? 1 : capacity - source->length);
        if (count > 0)
        {
            if (full)
            {
                const uint64_t doubled = capacity != 0 ? capacity * 2 : 1;
                const uint64_t grown = doubled < limit ? doubled : limit;
                result = grown > capacity && arena_push(arena, grown - capacity, 1) != NULL;
                if (result)
                {
                    source->pointer[source->length] = extra;
                    capacity = grown;
                }
            }
            source->length += result ? (uint64_t)count : 0;
        }
        else
        {
            done = count == 0;
            result = done || errno == EINTR;
        }
    }
    if (result)
    {
        source->pointer[source->length] = 0;
    }
    else
    {
        *source = (String8){0};
    }
    arena_pop_to(arena, result ? (uint64_t)((uint8_t*)source->pointer - arena->base) + source->length + 1 : saved);
    return result;
}

static bool asm_read_file(const char* path, Arena* arena, String8* source)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    bool result = fd >= 0 && asm_read_descriptor(fd, arena, source);
    if (fd >= 0 && close(fd) != 0)
    {
        result = false;
    }
    return result;
}

typedef struct AsmPrintBuffer AsmPrintBuffer;
struct AsmPrintBuffer
{
    Arena* arena;
    String8* source;
    bool failed;
};

// libc's output boundary appends the printer's chunks directly to the destination arena, including its terminator.
// The preprocessor owns separate arenas; nothing else pushes this destination until the stream has been closed.
static ssize_t asm_print_write(void* cookie, const char* bytes, size_t count)
{
    AsmPrintBuffer* buffer = cookie;
    String8* source = buffer->source;
    const uint64_t length = source->length;
    bool result = !buffer->failed && length <= BUSTER_ASM_MAX_SOURCE && count <= BUSTER_ASM_MAX_SOURCE - length;
    if (result)
    {
        result = arena_push(buffer->arena, count, 1) != NULL;
        if (!result)
        {
            errno = ENOMEM;
        }
    }
    else
    {
        errno = EFBIG;
    }
    if (result)
    {
        memcpy(source->pointer + length, bytes, count);
        source->length = length + count;
        source->pointer[source->length] = 0;
    }
    buffer->failed |= !result;
    return result ? (ssize_t)count : 0;
}

// Reuse both preprocessor kernels and printers. The growing in-memory stream includes their line markers and
// avoids a subprocess, temporary source file or second source copy. The returned slice belongs to the arena.
static bool asm_preprocess_unit(const char* path, PPOptions options, Arena* arena, String8* source,
                                InternTable* table, Preprocessor* pp, DependencyOutput* dependency, Arena* dependency_arena,
                                const char* output, FILE* errors, bool* fatal)
{
    options.assembly = true;
    options.expansion_starts = true;
    const uint64_t saved = arena->length;
    *source = (String8){.pointer = arena_push(arena, 1, 1)};
    bool result = source->pointer != 0;
    if (result)
    {
        source->pointer[0] = 0;
        const bool initialized = pp_initialize(pp, &options, table);
        result = initialized && pp_run_input(pp, path);
        if (fatal != NULL)
        {
            *fatal = !initialized || pp->fatal;
        }
        AsmPrintBuffer buffer = {
            .arena = arena,
            .source = source,
        };
        FILE* stream = result ? fopencookie(&buffer, "w", (cookie_io_functions_t){.write = asm_print_write}) : NULL;
        result = stream != 0 && pp_print(pp, stream, true);
        if (stream != 0)
        {
            result = fflush(stream) == 0 && result;
            result = fclose(stream) == 0 && result;
            result = !buffer.failed && result;
        }
        if (result && dependency != NULL && options.dependencies.enabled)
        {
            result = pp_dependencies(pp, path, output, dependency_arena, dependency, errors);
        }
        pp_release(pp);
    }
    else if (fatal != NULL)
    {
        *fatal = true;
    }
    arena_pop_to(arena, result ? (uint64_t)((uint8_t*)source->pointer - arena->base) + source->length + 1 : saved);
    if (!result)
    {
        *source = (String8){0};
    }
    return result;
}

static bool asm_preprocess_file(const char* path, PPOptions options, Arena* arena, String8* source)
{
    static InternTable table;
    static Preprocessor pp;
    const bool result = intern_initialize(&table) && asm_preprocess_unit(path, options, arena, source, &table, &pp, NULL, NULL, NULL, stderr, NULL);
    intern_release(&table);
    return result;
}

static void asm_diagnostic_to(FILE* errors, const char* path, String8 source, AsmResult result, bool preprocessed)
{
    uint32_t line = 1;
    uint32_t column = 1;
    char presumed[PATH_MAX];
    for (uint32_t at = 0; at < result.location && at < source.length; at += 1)
    {
        // Only the adapter's generated markers are interpreted. Plain .s keeps physical source locations.
        if (preprocessed && column == 1 && source.pointer[at] == '#')
        {
            uint32_t end = at;
            while (end < source.length && source.pointer[end] != '\n')
            {
                end += 1;
            }
            uint32_t p = at + 1;
            while (p < end && source.pointer[p] == ' ')
            {
                p += 1;
            }
            uint64_t number = 0;
            const uint32_t begin = p;
            while (p < end && source.pointer[p] >= '0' && source.pointer[p] <= '9' && number <= UINT32_MAX)
            {
                number = number * 10 + source.pointer[p] - '0';
                p += 1;
            }
            while (p < end && source.pointer[p] == ' ')
            {
                p += 1;
            }
            if (p > begin && number <= UINT32_MAX && p < end && source.pointer[p] == '"')
            {
                uint32_t length = 0;
                p += 1;
                while (p < end && source.pointer[p] != '"' && length + 1 < sizeof(presumed))
                {
                    uint8_t c = source.pointer[p++];
                    if (c == '\\' && p < end)
                    {
                        c = source.pointer[p++];
                        if (c >= '0' && c <= '7')
                        {
                            uint32_t value = c - '0';
                            for (uint32_t n = 1; n < 3 && p < end && source.pointer[p] >= '0' && source.pointer[p] <= '7'; n += 1)
                            {
                                value = value * 8 + source.pointer[p++] - '0';
                            }
                            c = (uint8_t)value;
                        }
                        else
                        {
                            c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
                        }
                    }
                    presumed[length++] = (char)c;
                }
                if (p < end && source.pointer[p] == '"')
                {
                    presumed[length] = 0;
                    path = presumed;
                    line = (uint32_t)number;
                    at = end;
                    continue;
                }
            }
        }
        if (source.pointer[at] == '\n')
        {
            line += 1;
            column = 1;
        }
        else
        {
            column += 1;
        }
    }
    if (result.error_name.length != 0)
    {
        fprintf(errors, "%s:%u:%u: error: %s: %.*s\n", path, line, column, result.error, (int)result.error_name.length, result.error_name.pointer);
    }
    else
    {
        fprintf(errors, "%s:%u:%u: error: %s\n", path, line, column, result.error);
    }
}

static void asm_diagnostic(const char* path, String8 source, AsmResult result, bool preprocessed)
{
    asm_diagnostic_to(stderr, path, source, result, preprocessed);
}

static int run_assembler_command(int count, char** arguments)
{
    Arena arena = arena_reserve(1ull << 36);
    char** preprocess_arguments = BUSTER_ARENA_PUSH_ARRAY(&arena, char*, (uint64_t)count + 1);
    uint32_t preprocess_count = 0;
    PPOptions preprocessing = {0};
    AsmOptions options = {.vectorized = true};
    const char* input = 0;
    const char* output = 0;
    bool timing = false;
    bool valid = preprocess_arguments != 0;
    for (int i = 0; i < count && valid; i += 1)
    {
        const char* argument = arguments[i];
        if (strcmp(argument, "-c") == 0)
        {
        }
        else if (strcmp(argument, "--intel") == 0)
        {
            options.intel = true;
        }
        else if (strcmp(argument, "--scalar") == 0)
        {
            options.vectorized = false;
        }
        else if (strcmp(argument, "--timing") == 0)
        {
            timing = true;
        }
        else if (strcmp(argument, "--isa=x86-64") == 0)
        {
            preprocess_arguments[preprocess_count++] = "-march=x86-64";
        }
        else if (strcmp(argument, "--isa=avx2") == 0)
        {
            preprocess_arguments[preprocess_count++] = "-march=x86-64-v3";
        }
        else if (strcmp(argument, "--isa=avx512f") == 0)
        {
            preprocess_arguments[preprocess_count++] = "-march=x86-64-v4";
        }
        else if (strncmp(argument, "-march=", 7) == 0 || strncmp(argument, "-mtune=", 7) == 0)
        {
            preprocess_arguments[preprocess_count++] = arguments[i];
        }
        else if (strcmp(argument, "-o") == 0 || strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-include") == 0 || strcmp(argument, "-isystem") == 0 || strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0)
        {
            valid = i + 1 < count;
            if (valid)
            {
                preprocess_arguments[preprocess_count++] = arguments[i];
                preprocess_arguments[preprocess_count++] = arguments[++i];
            }
        }
        else if (argument[0] != '-' || strcmp(argument, "-nostdinc") == 0 || (argument[1] != 0 && (argument[1] == 'I' || argument[1] == 'D' || argument[1] == 'U') && argument[2] != 0))
        {
            preprocess_arguments[preprocess_count++] = arguments[i];
        }
        else
        {
            // The C preprocessor deliberately ignores code-generation flags. The assembler must not silently
            // accept -m32 or another output-mode request it cannot honor.
            valid = false;
        }
    }
    bool markers;
    valid = valid && pp_parse_options((int)preprocess_count, preprocess_arguments, &preprocessing, &arena, &input, &output, &markers);
    const bool preprocessed = input != 0 && strlen(input) >= 2 && strcmp(input + strlen(input) - 2, ".S") == 0;
    preprocessing.scalar = !BUSTER_VECTOR_PATH(options.vectorized);
    int status = 1;
    if (!valid || input == 0)
    {
        fprintf(stderr, "usage: compiler assemble [--intel] [--scalar] [-march=CPU] [-mtune=CPU] [--timing] [-I dir] [-D name[=value]] [-U name] -o output.o input.s|input.S\n");
    }
    else
    {
        String8 source = {0};
        char default_output[PATH_MAX];
        if (output == 0)
        {
            const char* name = strrchr(input, '/');
            name = name != 0 ? name + 1 : input;
            const char* extension = strrchr(name, '.');
            const size_t length = extension != 0 ? (size_t)(extension - name) : strlen(name);
            valid = length + 3 <= sizeof(default_output);
            if (valid)
            {
                memcpy(default_output, name, length);
                memcpy(default_output + length, ".o", 3);
                output = default_output;
            }
        }
        const uint64_t start = asm_clock();
        X86MatchIndex* index = BUSTER_ARENA_PUSH_ARRAY(&arena, X86MatchIndex, 1);
        // A baked executable starts with the semantics tables built (baked.h, #590): only one that isn't builds them
        // (#1571), since x86_semantics_initialize rebuilds them unconditionally.
        if (!valid || index == 0 || !x86_encoder_initialize() || !(x86_semantics_initialized || x86_semantics_initialize()) ||
            !(preprocessed ? asm_preprocess_file(input, preprocessing, &arena, &source) : asm_read_file(input, &arena, &source)))
        {
            fprintf(stderr, "%s: unable to initialize or read assembly input (I/O, memory, or 32-bit source-offset implementation limit)\n", input);
        }
        else
        {
            x86_match_initialize(index, &preprocessing.target);
            const uint64_t initialized = asm_clock();
            AsmResult result = asm_assemble(source, index, options, &arena);
            if (result.error != 0)
            {
                asm_diagnostic(input, source, result, preprocessed);
            }
            else
            {
                const uint64_t assembled = asm_clock();
                const String8 name = {.pointer = (char*)input, .length = strlen(input)};
                const Object object = asm_object(&result, name, &arena);
                if (object.error != OBJECT_ERROR_NONE || object_write_file(output, object.bytes, object.size) != OBJECT_ERROR_NONE)
                {
                    fprintf(stderr, "%s: unable to build or write ELF object\n", output);
                }
                else
                {
                    status = 0;
                }
                if (timing)
                {
                    const uint64_t finished = asm_clock();
                    fprintf(stderr, "assembly: instructions=%u rounds=%u arena_bytes=%llu init_read_ns=%llu lex_ns=%llu parse_ns=%llu match_ns=%llu layout_ns=%llu encode_link_ns=%llu object_write_ns=%llu total_ns=%llu\n", result.instructions, result.rounds, (unsigned long long)arena.length, (unsigned long long)(initialized - start), (unsigned long long)result.lex_ns, (unsigned long long)result.parse_ns, (unsigned long long)result.match_ns, (unsigned long long)result.layout_ns, (unsigned long long)result.emit_ns, (unsigned long long)(finished - assembled), (unsigned long long)(finished - start));
                }
            }
        }
    }
    arena_release(&arena);
    return status;
}
