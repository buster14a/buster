// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// #802: kernel-visible reservation/commit contracts and real command admission. Run on a baseline, unsanitized host;
// resource limits belong to isolated children, never to a sanitizer runtime or to the calling CI shell.
#define _GNU_SOURCE
#include <stdlib.h>
#include <stddef.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include "buster/compiler/lane.c"

static bool memory_writable(const void* address)
{
    FILE* stream = fopen("/proc/self/maps", "r");
    bool result = false;
    char line[512];
    unsigned long first;
    unsigned long last;
    char permissions[5];
    while (stream != 0 && fgets(line, sizeof(line), stream) != 0)
    {
        if (sscanf(line, "%lx-%lx %4s", &first, &last, permissions) == 3 && (uintptr_t)address >= first && (uintptr_t)address < last)
        {
            result = permissions[1] == 'w';
        }
    }
    if (stream != 0)
    {
        fclose(stream);
    }
    return result;
}

static bool memory_wait(pid_t child, int expected)
{
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    } while (waited < 0 && errno == EINTR);
    return waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == expected;
}

static bool memory_limit(rlim_t bytes)
{
    struct rlimit previous;
    bool result = getrlimit(RLIMIT_AS, &previous) == 0;
    if (result)
    {
        previous.rlim_cur = bytes < previous.rlim_max ? bytes : previous.rlim_max;
        result = setrlimit(RLIMIT_AS, &previous) == 0;
    }
    return result;
}

static bool memory_arenas(void)
{
    bool valid = true;
    Arena arena = arena_reserve(UINT64_C(64) << 30);
    valid = arena.base != 0 && !memory_writable(arena.base);
    uint8_t* first = arena_push(&arena, 32, 16);
    valid = valid && first != 0 && memory_writable(first) && !memory_writable(arena.base + arena.capacity - 1);
    if (first != 0)
    {
        memset(first, 0xa5, 32);
        const uint64_t bytes = (UINT64_C(3) << 20) + 17;
        uint8_t* more = arena_push(&arena, bytes, 64);
        valid = valid && more != 0 && first[0] == 0xa5 && first[31] == 0xa5;
        if (more != 0)
        {
            valid = valid && more[0] == 0 && more[bytes - 1] == 0;
            more[bytes - 1] = 0x5a;
        }
    }
    const Arena saved = arena;
    valid = valid && arena_push(&arena, UINT64_MAX, 1) == 0 && memcmp(&arena, &saved, sizeof(arena)) == 0;
    // Deny only mprotect in a child: deterministic real syscall failure, without starving the test runtime itself.
    const pid_t child = fork();
    if (child == 0)
    {
        const struct sock_filter instructions[] = {
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mprotect, 0, 1),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOMEM),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        };
        const struct sock_fprog filter = { .len = sizeof(instructions) / sizeof(instructions[0]), .filter = (struct sock_filter*)instructions };
        bool failed = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 && prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter) == 0;
        failed = failed && arena_push(&arena, UINT64_C(16) << 20, 1) == 0 && memcmp(&arena, &saved, sizeof(arena)) == 0;
        failed = failed && arena_failure.commit && arena_failure.bytes != 0 && first[0] == 0xa5;
        _exit(failed ? 0 : 1);
    }
    valid = memory_wait(child, 0) && valid;
    arena_release(&arena);

    uint8_t storage[128] = { 0 };
    Arena borrowed = { .base = storage, .capacity = sizeof(storage) };
    uint8_t* bytes = arena_push(&borrowed, sizeof(storage), 1);
    valid = valid && bytes == storage;
    if (bytes != 0)
    {
        memset(bytes, 0x31, sizeof(storage));
    }
    arena_release(&borrowed);
    valid = valid && storage[0] == 0x31 && storage[127] == 0x31;
    Arena measure = arena_measure();
    valid = valid && arena_push(&measure, UINT64_C(1) << 32, 64) != 0 && measure.length == (UINT64_C(1) << 32);
    arena_release(&measure);

    ArenaPool pool = { 0 };
    arena_pool = &pool;
    arena = arena_reserve(4 << 20);
    bytes = arena_push(&arena, 4096, 1);
    valid = valid && bytes != 0;
    if (bytes != 0)
    {
        memset(bytes, 0xff, 4096);
    }
    const uint8_t* address = arena.base;
    arena_release(&arena);
    valid = valid && pool.count == 1 && !memory_writable(address);
    arena = arena_reserve(4 << 20);
    valid = valid && arena.base == address && arena.length == 0 && arena.peak == 0 && !memory_writable(address);
    bytes = arena_push(&arena, 4096, 1);
    valid = valid && bytes != 0;
    for (uint32_t i = 0; valid && i < 4096; i += 1)
    {
        valid = bytes[i] == 0;
    }
    arena_release(&arena);
    arena_pool = 0;
    arena_pool_release(&pool);
    return valid;
}

static bool memory_many_arenas(void)
{
    const pid_t child = fork();
    if (child == 0)
    {
        bool valid = memory_limit(UINT64_C(2) << 30);
        Arena arenas[32] = { 0 };
        for (uint32_t i = 0; valid && i < 32; i += 1)
        {
            arenas[i] = arena_reserve(UINT64_C(64) << 30);
            uint8_t* byte = arena_push(arenas + i, 1, 1);
            valid = byte != 0 && arenas[i].capacity <= (UINT64_C(16) << 20);
            if (byte != 0)
            {
                *byte = (uint8_t)i;
            }
        }
        for (uint32_t i = 0; i < 32; i += 1)
        {
            valid = valid && (arenas[i].base == 0 || arenas[i].base[0] == i);
            arena_release(arenas + i);
        }
        _exit(valid ? 0 : 1);
    }
    return memory_wait(child, 0);
}

static bool memory_command(char* const* arguments, const char* log, rlim_t limit, bool success)
{
    const pid_t child = fork();
    if (child == 0)
    {
        const int output = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        bool ready = output >= 0 && dup2(output, STDOUT_FILENO) >= 0 && dup2(output, STDERR_FILENO) >= 0;
        if (output >= 0)
        {
            close(output);
        }
        ready = ready && (limit == RLIM_INFINITY || memory_limit(limit));
        if (ready)
        {
            execvp(arguments[0], arguments);
        }
        _exit(127);
    }
    bool valid = memory_wait(child, success ? 0 : 1);
    if (!success)
    {
        char text[4096] = { 0 };
        FILE* stream = fopen(log, "r");
        if (stream != 0)
        {
            fread(text, 1, sizeof(text) - 1, stream);
            fclose(stream);
        }
        valid = valid && strstr(text, "fatal error:") != 0 && strstr(text, "bytes requested") != 0 && strstr(text, "Usage:") == 0 && strstr(text, "usage:") == 0;
    }
    if (!valid)
    {
        fprintf(stderr, "failed memory command: %s %s; log: %s\n", arguments[0], arguments[1] != 0 ? arguments[1] : "", log);
    }
    return valid;
}

static bool memory_commands(char* compiler, const char* directory, bool valgrind, bool strict)
{
    char source[PATH_MAX];
    char output[PATH_MAX];
    char log[PATH_MAX];
    char profile[PATH_MAX + 64];
    snprintf(source, sizeof(source), "%s/input.c", directory);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(log, sizeof(log), "%s/command.log", directory);
    snprintf(profile, sizeof(profile), "--callgrind-out-file=%s/callgrind.out", directory);
    FILE* file = fopen(source, "w");
    bool valid = file != 0;
    if (file != 0)
    {
        fputs("#define ADD(a, b) ((a) + (b))\nint main(void) { return ADD(19, 23) != 42; }\n", file);
        valid = fclose(file) == 0;
    }
    char* options[][8] = {
        { compiler, "-E", "-P", source, "-o", output, 0 },
        { compiler, "parse", source, 0 },
        { compiler, "-fsyntax-only", source, 0 },
        { compiler, "-c", source, "-o", output, 0 },
        { compiler, source, "-o", output, 0 },
    };
    for (uint32_t i = 0; valid && i < sizeof(options) / sizeof(options[0]); i += 1)
    {
        snprintf(log, sizeof(log), "%s/command-%u.log", directory, i);
        snprintf(profile, sizeof(profile), "--callgrind-out-file=%s/callgrind-%u.out", directory, i);
        char* traced[16] = { "valgrind", "--tool=callgrind", "--error-exitcode=97", "--quiet", profile };
        for (uint32_t j = 0; options[i][j] != 0; j += 1)
        {
            traced[5 + j] = options[i][j];
        }
        const rlim_t limit = valgrind || strict ? RLIM_INFINITY : UINT64_C(2) << 30;
        valid = memory_command(valgrind ? traced : options[i], log, limit, true);
        if (valid && !valgrind && !strict)
        {
            snprintf(log, sizeof(log), "%s/failure-%u.log", directory, i);
            valid = memory_command(options[i], log, UINT64_C(64) << 20, false);
        }
    }
    if (valid)
    {
        snprintf(log, sizeof(log), "%s/executable.log", directory);
        char* run[] = { output, 0 };
        valid = memory_command(run, log, RLIM_INFINITY, true);
    }
    // Preserve logs and products on failure so that CI's artifact identifies the actual refused allocation.
    return valid;
}

int main(int argc, char** argv)
{
    const bool arenas = memory_arenas();
    const bool limited = memory_many_arenas();
    const bool valgrind = argc > 3 && strcmp(argv[3], "--valgrind") == 0;
    const bool strict = argc > 3 && strcmp(argv[3], "--strict") == 0;
    const bool commands = argc == 1 || (argc >= 3 && memory_commands(argv[1], argv[2], valgrind, strict));
    printf("memory resources: arenas=%s limited=%s commands=%s\n", arenas ? "pass" : "FAIL", limited ? "pass" : "FAIL", argc == 1 ? "not-run" : commands ? "pass" : "FAIL");
    return arenas && limited && commands ? 0 : 1;
}
