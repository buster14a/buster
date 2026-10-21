// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included by main.c. Interleaved scalar/vector component timings and optional fresh-process comparisons. Source
// generation and correctness checks are outside the samples. Every process reads the same warm file and overwrites
// the same output on the same filesystem; no preprocessing, pipes, disassembly or validation is timed for one side.

#define BUSTER_ASM_BENCHMARK_PASSES 21

static uint64_t asm_benchmark_median(uint64_t* samples, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const uint64_t value = samples[i];
        uint32_t at = i;
        while (at != 0 && samples[at - 1] > value)
        {
            samples[at] = samples[at - 1];
            at -= 1;
        }
        samples[at] = value;
    }
    return samples[count / 2];
}

static String8 asm_benchmark_source(Arena* arena, uint32_t functions, const char* workload)
{
    const uint64_t capacity = (uint64_t)functions * 4096 + 128;
    String8 source = {.pointer = arena_push(arena, capacity, 1)};
    if (source.pointer != 0)
    {
        const bool att = strcmp(workload, "att") == 0;
        source.length = snprintf(source.pointer, capacity, "%s.text\n", att ? ".att_syntax prefix\n" : ".intel_syntax noprefix\n");
        for (uint32_t f = 0; f < functions; f += 1)
        {
            source.length += snprintf(source.pointer + source.length, capacity - source.length, ".type fn%u,@function\nfn%u:\n", f, f);
            if (strcmp(workload, "simd") == 0)
            {
                for (uint32_t i = 0; i < 16; i += 1)
                {
                    source.length += snprintf(source.pointer + source.length, capacity - source.length, "vaddps zmm%u{k2}{z},zmm%u,zmm%u\nvaddps zmm%u,zmm%u,dword ptr [rdi+%u]{1to16}\n", i, (i + 1) % 32, (i + 7) % 32, i, i, i * 4);
                }
            }
            else
            {
                for (uint32_t i = 0; i < 16; i += 1)
                {
                    source.length += snprintf(source.pointer + source.length, capacity - source.length, att ? "addq $%u,%%rax\nxorq %%rdi,%%rdx\n" : "add rax,%u\nxor rdx,rdi\n", (f * 37 + i * 71) % 10000);
                }
            }
            if (strcmp(workload, "mixed") == 0 || att)
            {
                const char* tail = att ? "jmp 2f\n1: addq 8(%%rdi,%%rcx,8),%%rax\nincq %%rcx\n2: cmpq %%rsi,%%rcx\njb 1b\ncall external\n.p2align 4,0x90\nret\n.size fn%u,.-fn%u\n.section .rodata\n.quad fn%u+7,external-8\n.text\n" : "jmp 2f\n1: add rax,qword ptr [rdi+rcx*8+8]\ninc rcx\n2: cmp rcx,rsi\njb 1b\ncall external\n.p2align 4,0x90\nret\n.size fn%u,.-fn%u\n.section .rodata\n.quad fn%u+7,external-8\n.text\n";
                source.length += snprintf(source.pointer + source.length, capacity - source.length, tail, f, f, f);
            }
            else
            {
                source.length += snprintf(source.pointer + source.length, capacity - source.length, "ret\n.size fn%u,.-fn%u\n", f, f);
            }
        }
    }
    return source;
}

static bool asm_benchmark_process(char* const* arguments, const char* errors)
{
    const pid_t child = fork();
    int status = -1;
    bool okay = child >= 0;
    if (child == 0)
    {
        const int fd = open(errors, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0 || dup2(fd, STDERR_FILENO) < 0)
        {
            _exit(126);
        }
        close(fd);
        execvp(arguments[0], arguments);
        _exit(127);
    }
    if (child > 0)
    {
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        okay = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    return okay;
}

static bool asm_benchmark_external(String8 source)
{
    char directory[] = "/tmp/buster-assembler-benchmark-XXXXXX";
    char input[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    char compiler[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", compiler, sizeof(compiler) - 1);
    bool okay = length > 0 && length < sizeof(compiler) - 1 && mkdtemp(directory) != 0;
    if (okay)
    {
        compiler[length] = 0;
        snprintf(input, sizeof(input), "%s/input.s", directory);
        snprintf(output, sizeof(output), "%s/output.o", directory);
        snprintf(errors, sizeof(errors), "%s/errors", directory);
        okay = object_write_file(input, (const uint8_t*)source.pointer, source.length) == OBJECT_ERROR_NONE;
        char* vector[] = {compiler, "assemble", "--isa=avx512f", "-o", output, input, 0};
        char* scalar[] = {compiler, "assemble", "--scalar", "--isa=avx512f", "-o", output, input, 0};
        char* gas[] = {"as", "--64", "-o", output, input, 0};
        char* llvm[] = {"clang", "-c", "-x", "assembler", "-o", output, input, 0};
        char** commands[] = {vector, scalar, gas, llvm};
        const char* names[] = {"buster-vector", "buster-scalar", "GNU-as", "clang-integrated"};
        uint64_t samples[4][BUSTER_ASM_BENCHMARK_PASSES];
        // Warm all four executables and input/output pages once, then rotate the first process in each sample.
        for (uint32_t pass = 0; pass <= BUSTER_ASM_BENCHMARK_PASSES && okay; pass += 1)
        {
            for (uint32_t order = 0; order < 4 && okay; order += 1)
            {
                const uint32_t row = (pass + order) % 4;
                const uint64_t start = asm_clock();
                okay = asm_benchmark_process(commands[row], errors);
                const uint64_t elapsed = asm_clock() - start;
                if (pass != 0)
                {
                    samples[row][pass - 1] = elapsed;
                }
                if (!okay)
                {
                    fprintf(stderr, "benchmark process %s failed; diagnostics retained in %s\n", names[row], errors);
                }
            }
        }
        if (okay)
        {
            for (uint32_t row = 0; row < 4; row += 1)
            {
                const uint64_t median = asm_benchmark_median(samples[row], BUSTER_ASM_BENCHMARK_PASSES);
                printf("process %-17s %9.3f ms %8.2f MB/s\n", names[row], median / 1e6, source.length * 1000.0 / median);
            }
            unlink(input);
            unlink(output);
            unlink(errors);
            rmdir(directory);
        }
    }
    return okay;
}

static int run_assembler_benchmark(int count, char** arguments)
{
    uint32_t functions = 256;
    const char* workload = "mixed";
    bool external = false;
    uint32_t after_scalar = 0;
    int only = -1;
    bool okay = true;
    for (int i = 0; i < count; i += 1)
    {
        if (strcmp(arguments[i], "--external") == 0)
        {
            external = true;
        }
        else if (strcmp(arguments[i], "--after-scalar") == 0 && i + 1 < count)
        {
            char* end;
            const unsigned long n = strtoul(arguments[++i], &end, 10);
            okay &= *end == 0 && n <= 1000;
            after_scalar = n;
        }
        else if (strcmp(arguments[i], "--only") == 0 && i + 1 < count)
        {
            const char* name = arguments[++i];
            only = strcmp(name, "vector") == 0 ? 1 : strcmp(name, "scalar") == 0 ? 0 : -1;
            okay &= only != -1;
        }
        else if (strcmp(arguments[i], "--functions") == 0 && i + 1 < count)
        {
            char* end;
            const unsigned long n = strtoul(arguments[++i], &end, 10);
            okay &= *end == 0 && n >= 1 && n <= 2048;
            functions = n;
        }
        else if (strcmp(arguments[i], "--workload") == 0 && i + 1 < count)
        {
            workload = arguments[++i];
            okay &= strcmp(workload, "mixed") == 0 || strcmp(workload, "registers") == 0 || strcmp(workload, "simd") == 0 || strcmp(workload, "att") == 0;
        }
        else
        {
            okay = false;
        }
    }
    Arena source_arena = arena_reserve(1ull << 26);
    Arena work = arena_reserve(1ull << 32);
    X86MatchIndex* index = BUSTER_ARENA_PUSH_ARRAY(&source_arena, X86MatchIndex, 1);
    const uint64_t initialization_start = asm_clock();
    okay = okay && index != 0 && x86_encoder_initialize();
    if (okay)
    {
        X86Target target;
        okay = x86_target_parse("x86-64-v4", 9, &target);
        x86_match_initialize(index, &target);
    }
    const uint64_t initialization_ns = asm_clock() - initialization_start;
    const String8 source = okay ? asm_benchmark_source(&source_arena, functions, workload) : (String8){0};
    okay &= source.pointer != 0;
    uint64_t samples[2][7][BUSTER_ASM_BENCHMARK_PASSES];
    uint64_t memory = 0;
    uint32_t instructions = 0;
    uint8_t* reference = 0;
    uint64_t reference_size = 0;
    for (uint32_t pass = 0; pass <= BUSTER_ASM_BENCHMARK_PASSES && okay; pass += 1)
    {
        for (uint32_t order = 0; order < 2 && okay; order += 1)
        {
            const uint32_t path = (pass + order) % 2;
            if (pass != 0 && only >= 0 && path != (uint32_t)only)
            {
                continue;
            }
            if (after_scalar != 0)
            {
                const uint64_t until = asm_clock() + (uint64_t)after_scalar * 1000000;
                while (asm_clock() < until && okay)
                {
                    work.length = 0;
                    const AsmResult warm = asm_assemble(source, index, (AsmOptions){0}, &work);
                    okay = warm.error == 0;
                }
            }
            work.length = 0;
            const uint64_t start = asm_clock();
            AsmResult result = asm_assemble(source, index, (AsmOptions){.vectorized = path != 0}, &work);
            const uint64_t assembled = asm_clock();
            const Object object = asm_object(&result, BUSTER_S8("benchmark.s"), &work);
            const uint64_t finished = asm_clock();
            okay = result.error == 0 && object.error == OBJECT_ERROR_NONE;
            if (okay && reference == 0)
            {
                reference_size = object.size;
                reference = arena_push(&source_arena, object.size, 1);
                okay = reference != 0;
                if (okay)
                {
                    memcpy(reference, object.bytes, object.size);
                }
            }
            okay = okay && reference_size == object.size && memcmp(reference, object.bytes, object.size) == 0;
            if (!okay)
            {
                fprintf(stderr, "assembler benchmark correctness failure: %s at %u\n", result.error != 0 ? result.error : "object or scalar/vector mismatch", result.location);
            }
            if (pass != 0)
            {
                samples[path][0][pass - 1] = result.lex_ns;
                samples[path][1][pass - 1] = result.parse_ns;
                samples[path][2][pass - 1] = result.match_ns;
                samples[path][3][pass - 1] = result.layout_ns;
                samples[path][4][pass - 1] = result.emit_ns;
                samples[path][5][pass - 1] = finished - assembled;
                samples[path][6][pass - 1] = finished - start;
            }
            memory = work.length;
            instructions = result.instructions;
        }
    }
    if (okay)
    {
        printf("assembler %s: %u functions, %u instructions, %llu source bytes; %u interleaved passes\n", workload, functions, instructions, (unsigned long long)source.length, BUSTER_ASM_BENCHMARK_PASSES);
        printf("init: %.3f ms; index: %llu bytes; work arena: %llu bytes; ELF: %llu bytes\n", initialization_ns / 1e6, (unsigned long long)sizeof(*index), (unsigned long long)memory, (unsigned long long)reference_size);
        if (after_scalar != 0)
        {
            printf("component samples follow %u ms of scalar assembly (outside the sample)\n", after_scalar);
        }
        printf("path       lex       parse       match      layout    encode/link  object      total (ms)\n");
        for (uint32_t path = 0; path < 2; path += 1)
        {
            if (only >= 0 && path != (uint32_t)only)
            {
                continue;
            }
            printf("%-7s", path != 0 ? "vector" : "scalar");
            for (uint32_t phase = 0; phase < 7; phase += 1)
            {
                printf(" %10.3f", asm_benchmark_median(samples[path][phase], BUSTER_ASM_BENCHMARK_PASSES) / 1e6);
            }
            printf("\n");
        }
        if (external)
        {
            okay = asm_benchmark_external(source);
        }
    }
    else if (!source.pointer)
    {
        fprintf(stderr, "usage: compiler benchmark-assembler [--functions 1..2048] [--workload mixed|registers|simd|att] [--only scalar|vector] [--after-scalar MS] [--external]\n");
    }
    arena_release(&work);
    arena_release(&source_arena);
    return okay ? 0 : 1;
}
