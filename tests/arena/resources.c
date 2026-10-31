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
        failed = failed && arena_push(&arena, UINT64_C(16) << 20, 1) == 0 && arena.base == saved.base && arena.length == saved.length &&
                 arena.peak == saved.peak && arena.committed == saved.committed && arena.mapping_offset == saved.mapping_offset &&
                 arena.mapping_bytes == saved.mapping_bytes;
        // Nothing the attempt may have made writable can be verified under the refusal: the arena is unaccounted and
        // mustn't be pooled.
        failed = failed && arena.unaccounted && arena_failure.commit && arena_failure.bytes != 0 && first[0] == 0xa5;
        ArenaPool pool = { 0 };
        arena_pool = &pool;
        arena_release(&arena);
        failed = failed && pool.count == 0;
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

// The process's data charge (VmData), which RLIMIT_DATA bounds: private anonymous mappings' writable pages.
static uint64_t memory_data(void)
{
    FILE* stream = fopen("/proc/self/status", "r");
    char line[256];
    uint64_t result = 0;
    while (stream != 0 && fgets(line, sizeof(line), stream) != 0)
    {
        unsigned long long kilobytes = 0;
        if (sscanf(line, "VmData: %llu kB", &kilobytes) == 1)
        {
            result = kilobytes << 10;
            break;
        }
    }
    if (stream != 0)
    {
        fclose(stream);
    }
    return result;
}

static bool memory_data_limit(uint64_t room)
{
    const uint64_t data = memory_data();
    struct rlimit previous;
    bool result = data != 0 && getrlimit(RLIMIT_DATA, &previous) == 0;
    if (result)
    {
        const rlim_t limit = (rlim_t)(data + room);
        previous.rlim_cur = limit < previous.rlim_max ? limit : previous.rlim_max;
        result = setrlimit(RLIMIT_DATA, &previous) == 0;
    }
    return result;
}

// A commit refused across a VMA boundary can leave the leading VMA writable and charged: a real partial failure,
// which the seccomp child's refusal before any change can't cover. A NOHUGEPAGE advise splits the reserved tail so
// that a later commit spans two VMAs; RLIMIT_DATA admits the head's growth but not the tail's, and what the attempt
// made writable must be undone — in the arena's tail and in what the pool hands out again.
static bool memory_partial_commit(void)
{
    const pid_t child = fork();
    if (child == 0)
    {
        ArenaPool pool = { 0 };
        bool valid = true;
        Arena arena = arena_reserve(UINT64_C(8) << 20);
        uint8_t* first = arena_push(&arena, 4096, 1);
        valid = valid && arena.base != 0 && first != 0;
        if (first != 0)
        {
            memset(first, 0xa5, 4096);
        }
        // A VMA boundary inside the next commit's span, at 4 MiB of the reservation; the first push's geometric
        // commit already consumed the reservation's own NOHUGEPAGE head.
        uint8_t* const tail = arena.base + (UINT64_C(64) << 10);
        const uint64_t boundary = (UINT64_C(4) << 20) - arena.mapping_offset;
        valid = valid && madvise(arena.base + boundary, UINT64_C(64) << 10, MADV_NOHUGEPAGE) == 0;
        valid = valid && memory_data_limit((UINT64_C(2) << 20) + (UINT64_C(768) << 10));
        // The same limit leaves a two-VMA mprotect half applied; prove that partial-failure mode is real here before
        // the arena's commit runs into it, then restore the probe.
        valid = valid && mprotect(tail, UINT64_C(6) << 20, PROT_READ | PROT_WRITE) != 0 && memory_writable(tail);
        valid = valid && mprotect(tail, UINT64_C(6) << 20, PROT_NONE) == 0;
        const uint64_t committed = arena.committed;
        const uint8_t* base = arena.base;
        valid = valid && arena_push(&arena, UINT64_C(3) << 20, 1) == 0 && arena.committed == committed && first[0] == 0xa5 &&
                !memory_writable(base + (UINT64_C(64) << 10)) && !memory_writable(base + boundary);
        arena_pool = &pool;
        arena_release(&arena);
        valid = valid && pool.count == 1;
        arena = arena_reserve(UINT64_C(8) << 20);
        valid = valid && arena.base == base && arena.length == 0 && !memory_writable(arena.base + (UINT64_C(64) << 10));
        uint8_t* again = arena_push(&arena, 4096, 1);
        valid = valid && again != 0 && memory_writable(again);
        if (again != 0)
        {
            again[0] = 0x5a;
        }
        arena_pool = 0;
        arena_release(&arena);
        arena_pool_release(&pool);
        _exit(valid ? 0 : 1);
    }
    return memory_wait(child, 0);
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

// Valgrind's address manager can report EINVAL, not ENOMEM, for a valid oversized mapping. Reproduce that
// layout refusal without depending on Valgrind's current layout, keeping the compact retry and later pushes real.
static bool memory_layout_refusal(void)
{
    const pid_t child = fork();
    if (child == 0)
    {
        const struct sock_filter instructions[] = {
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mmap, 0, 5),
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, PROT_NONE, 0, 3),
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1]) + 4),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EINVAL),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        };
        const struct sock_fprog filter = { .len = sizeof(instructions) / sizeof(instructions[0]), .filter = (struct sock_filter*)instructions };
        bool valid = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 && prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter) == 0;
        arena_pool = 0;
        arena_compact = false;
        Arena arena = arena_reserve(UINT64_C(64) << 30);
        uint8_t* bytes = arena_push(&arena, 32, 16);
        valid = valid && arena_compact && arena.capacity == BUSTER_ARENA_COMPACT_BYTES && bytes != 0;
        if (bytes != 0)
        {
            bytes[0] = 0x51;
            bytes[31] = 0xa3;
            valid = valid && bytes[0] == 0x51 && bytes[31] == 0xa3;
        }
        arena_release(&arena);
        _exit(valid ? 0 : 1);
    }
    return memory_wait(child, 0);
}

// Let exec's loader map its ELF segments and make RELRO read-only. Refuse only the compiler's inaccessible
// reservations or read/write commits, avoiding a guessed RLIMIT_AS floor that a smaller compiler can pass.
static bool memory_refuse(uint32_t operation)
{
    const struct sock_filter instructions[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, operation == 1 ? __NR_mmap : __NR_mprotect, 0, 3),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, operation == 1 ? PROT_NONE : PROT_READ | PROT_WRITE, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOMEM),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    const struct sock_fprog filter = { .len = sizeof(instructions) / sizeof(instructions[0]), .filter = (struct sock_filter*)instructions };
    return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 && prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter) == 0;
}

static bool memory_command(char* const* arguments, const char* log, rlim_t limit, uint32_t refused)
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
        ready = ready && (limit == RLIM_INFINITY || memory_limit(limit)) && (refused == 0 || memory_refuse(refused));
        if (ready)
        {
            execvp(arguments[0], arguments);
        }
        _exit(127);
    }
    bool valid = memory_wait(child, refused == 0 ? 0 : 1);
    if (refused != 0)
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
        FILE* stream = fopen(log, "r");
        char line[512];
        for (uint32_t i = 0; stream != 0 && i < 16 && fgets(line, sizeof(line), stream) != 0; i += 1)
        {
            fputs(line, stderr);
        }
        if (stream != 0)
        {
            fclose(stream);
        }
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
    const bool ready = valid;
    for (uint32_t i = 0; ready && i < sizeof(options) / sizeof(options[0]); i += 1)
    {
        snprintf(log, sizeof(log), "%s/command-%u.log", directory, i);
        snprintf(profile, sizeof(profile), "--callgrind-out-file=%s/callgrind-%u.out", directory, i);
        char* traced[16] = { "valgrind", "--tool=callgrind", "--error-exitcode=97", "--quiet", profile };
        for (uint32_t j = 0; options[i][j] != 0; j += 1)
        {
            traced[5 + j] = options[i][j];
        }
        const rlim_t limit = valgrind || strict ? RLIM_INFINITY : UINT64_C(2) << 30;
        valid = memory_command(valgrind ? traced : options[i], log, limit, 0) && valid;
        for (uint32_t refused = 1; !valgrind && !strict && refused <= 2; refused += 1)
        {
            snprintf(log, sizeof(log), "%s/failure-%u-%u.log", directory, i, refused);
            valid = memory_command(options[i], log, RLIM_INFINITY, refused) && valid;
        }
    }
    if (valid)
    {
        snprintf(log, sizeof(log), "%s/executable.log", directory);
        char* run[] = { output, 0 };
        valid = memory_command(run, log, RLIM_INFINITY, 0);
    }
    // Preserve logs and products on failure so that CI's artifact identifies the actual refused allocation.
    return valid;
}

int main(int argc, char** argv)
{
    const bool arenas = memory_arenas();
    const bool partial = memory_partial_commit();
    const bool limited = memory_many_arenas();
    const bool layout = memory_layout_refusal();
    const bool valgrind = argc > 3 && strcmp(argv[3], "--valgrind") == 0;
    const bool strict = argc > 3 && strcmp(argv[3], "--strict") == 0;
    const bool commands = argc == 1 || (argc >= 3 && memory_commands(argv[1], argv[2], valgrind, strict));
    printf("memory resources: arenas=%s partial=%s limited=%s layout=%s commands=%s\n", arenas ? "pass" : "FAIL", partial ? "pass" : "FAIL", limited ? "pass" : "FAIL", layout ? "pass" : "FAIL", argc == 1 ? "not-run" : commands ? "pass" : "FAIL");
    return arenas && partial && limited && layout && commands ? 0 : 1;
}
