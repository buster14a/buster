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
        object = object_build(&result->link, &units, &symbols, 0, (ObjectOptions){0}, name, arena);
    }
    return object;
}

static bool asm_read_file(const char* path, Arena* arena, String8* source)
{
    bool result = false;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat stats;
    if (fd >= 0 && fstat(fd, &stats) == 0 && stats.st_size >= 0 && (uint64_t)stats.st_size <= BUSTER_ASM_MAX_SOURCE)
    {
        source->length = stats.st_size;
        source->pointer = arena_push(arena, source->length + 1, 1);
        uint64_t read_bytes = 0;
        result = source->pointer != 0;
        while (result && read_bytes < source->length)
        {
            const ssize_t count = read(fd, source->pointer + read_bytes, source->length - read_bytes);
            if (count > 0)
            {
                read_bytes += count;
            }
            else if (count == 0 || errno != EINTR)
            {
                result = false;
            }
        }
        if (result)
        {
            source->pointer[source->length] = 0;
        }
    }
    if (fd >= 0 && close(fd) != 0)
    {
        result = false;
    }
    return result;
}

static void asm_diagnostic(const char* path, String8 source, AsmResult result)
{
    uint32_t line = 1;
    uint32_t column = 1;
    for (uint32_t at = 0; at < result.location && at < source.length; at += 1)
    {
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
    fprintf(stderr, "%s:%u:%u: error: %s\n", path, line, column, result.error);
}

static int run_assembler_command(int count, char** arguments)
{
    AsmOptions options = {.vectorized = true};
    const char* input = 0;
    const char* output = 0;
    bool timing = false;
    bool valid = true;
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
            options.isa = 0;
        }
        else if (strcmp(argument, "--isa=avx2") == 0)
        {
            options.isa = 1;
        }
        else if (strcmp(argument, "--isa=avx512f") == 0)
        {
            options.isa = 2;
        }
        else if (strcmp(argument, "-o") == 0 && i + 1 < count)
        {
            output = arguments[++i];
        }
        else if (argument[0] != '-' && input == 0)
        {
            input = argument;
        }
        else
        {
            valid = false;
        }
    }
    int status = 1;
    if (!valid || input == 0)
    {
        fprintf(stderr, "usage: compiler assemble [--intel] [--scalar] [--isa=x86-64|avx2|avx512f] [--timing] -o output.o input.s\n");
    }
    else if (strlen(input) >= 2 && strcmp(input + strlen(input) - 2, ".S") == 0)
    {
        fprintf(stderr, "%s: .S preprocessing requires the assembly-mode adapter; use preprocessed .s input\n", input);
    }
    else
    {
        Arena arena = arena_reserve(1ull << 32);
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
        if (!valid || index == 0 || !x86_encoder_initialize() || !asm_read_file(input, &arena, &source))
        {
            fprintf(stderr, "%s: unable to initialize or read assembly input (maximum 16 MiB)\n", input);
        }
        else
        {
            x86_match_initialize(index);
            const uint64_t initialized = asm_clock();
            AsmResult result = asm_assemble(source, index, options, &arena);
            if (result.error != 0)
            {
                asm_diagnostic(input, source, result);
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
        arena_release(&arena);
    }
    return status;
}
