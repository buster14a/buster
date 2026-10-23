// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original protocol fixture. This program is deliberately not a compiler or a random generator. The harness tests
// identify every run using it as a fixture and report zero compiler campaign passes. Fake compile operations copy
// this executable and write a small sidecar selecting the fake program's behavior. No shell or external tool runs.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

static bool fixture_write(int descriptor, const char* data, size_t size)
{
    size_t written = 0;
    bool valid = true;
    while (valid && written < size)
    {
        const ssize_t amount = write(descriptor, data + written, size - written);
        if (amount > 0)
        {
            written += (size_t)amount;
        }
        else if (amount == 0 || errno != EINTR)
        {
            valid = false;
        }
    }
    return valid;
}

static int fixture_flood(void)
{
    char data[8192];
    memset(data, 'x', sizeof(data));
    bool valid = true;
    for (unsigned i = 0; valid && i < 128; ++i)
    {
        valid = fixture_write(STDOUT_FILENO, data, sizeof(data));
    }
    return valid ? 0 : 8;
}

static int fixture_tree(bool hang)
{
    int ready[2] = {-1, -1};
    bool valid = pipe(ready) == 0;
    const pid_t child = valid ? fork() : -1;
    if (child == 0)
    {
        close(ready[0]);
        if (setsid() < 0)
        {
            _exit(3);
        }
        const pid_t grandchild = fork();
        if (grandchild == 0)
        {
            close(ready[1]);
            for (;;)
            {
                pause();
            }
        }
        dprintf(STDERR_FILENO, "%ld %ld\n", (long)getpid(), (long)grandchild);
        fixture_write(ready[1], "!", 1);
        close(ready[1]);
        for (;;)
        {
            pause();
        }
    }
    valid = valid && child > 0;
    if (ready[1] >= 0)
    {
        close(ready[1]);
    }
    char byte = 0;
    if (valid)
    {
        valid = read(ready[0], &byte, 1) == 1 && byte == '!';
    }
    if (ready[0] >= 0)
    {
        close(ready[0]);
    }
    if (valid && hang)
    {
        for (;;)
        {
            pause();
        }
    }
    return valid ? 0 : 3;
}

static bool fixture_copy_self(const char* destination)
{
    const int input = open("/proc/self/exe", O_RDONLY);
    const int output = open(destination, O_WRONLY | O_CREAT | O_EXCL, 0700);
    bool valid = input >= 0 && output >= 0;
    while (valid)
    {
        char buffer[8192];
        const ssize_t amount = read(input, buffer, sizeof(buffer));
        if (amount > 0)
        {
            valid = fixture_write(output, buffer, (size_t)amount);
        }
        else if (amount == 0)
        {
            break;
        }
        else if (errno != EINTR)
        {
            valid = false;
        }
    }
    if (input >= 0)
    {
        valid = close(input) == 0 && valid;
    }
    if (output >= 0)
    {
        valid = close(output) == 0 && valid;
    }
    return valid;
}

static bool fixture_source(const char* path, const char* scenario, const char* text)
{
    FILE* file = fopen(path, "wx");
    bool valid = file != 0;
    if (file)
    {
        valid = fprintf(file, "// fixture-scenario=%s\n%s\n", scenario, text) > 0;
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

static int fixture_generate(bool yarpgen, const char* scenario, bool seeded, bool c_mode, bool out_dir)
{
    int result = 0;
    if (!seeded || (yarpgen && (!c_mode || !out_dir)))
    {
        result = 21;
    }
    else if (strcmp(scenario, "generator-error") == 0)
    {
        result = 22;
    }
    else if (strcmp(scenario, "generator-crash") == 0)
    {
        raise(SIGSEGV);
    }
    else if (strcmp(scenario, "generator-timeout") == 0)
    {
        for (;;)
        {
            pause();
        }
    }
    else if (strcmp(scenario, "generator-flood") == 0)
    {
        result = fixture_flood();
    }
    else if (yarpgen)
    {
        const bool valid = fixture_source("driver.c", scenario, "#include \"init.h\"\nint main(void) { return func(); }") && fixture_source("func.c", scenario, "#include \"init.h\"\nint func(void) { return 0; }") && (strcmp(scenario, "missing-source") == 0 || fixture_source("init.h", scenario, "int func(void);"));
        result = valid ? 0 : 23;
    }
    else
    {
        printf("// fixture-scenario=%s\nint main(void) { return 0; }\n", scenario);
    }
    return result;
}

static int fixture_compile(const char* output, const char* source, bool buster, bool tier1, bool multifile, bool second_source)
{
    char scenario[64] = "probe";
    FILE* file = source ? fopen(source, "r") : 0;
    bool valid = file != 0 && output != 0 && (!multifile || (second_source && access("init.h", R_OK) == 0));
    if (file)
    {
        char line[128] = {0};
        if (fgets(line, sizeof(line), file))
        {
            if (sscanf(line, "// fixture-scenario=%63s", scenario) != 1)
            {
                snprintf(scenario, sizeof(scenario), "probe");
            }
        }
        valid = fclose(file) == 0 && valid;
    }
    int result = 0;
    if (!valid)
    {
        result = 31;
    }
    else if (buster && tier1 && strcmp(scenario, "compile-error") == 0)
    {
        result = 32;
    }
    else if (buster && tier1 && strcmp(scenario, "compile-crash") == 0)
    {
        raise(SIGABRT);
    }
    else if (buster && tier1 && strcmp(scenario, "compile-timeout") == 0)
    {
        for (;;)
        {
            pause();
        }
    }
    else if (buster && tier1 && strcmp(scenario, "compile-no-output") == 0)
    {
        result = 0;
    }
    else
    {
        valid = fixture_copy_self(output);
        char sidecar[PATH_MAX];
        const int length = snprintf(sidecar, sizeof(sidecar), "%s.fixture", output);
        file = length > 0 && (size_t)length < sizeof(sidecar) ? fopen(sidecar, "wx") : 0;
        valid = valid && file;
        if (file)
        {
            valid = fprintf(file, "%s %u %u\n", scenario, buster, tier1) > 0 && valid;
            valid = fclose(file) == 0 && valid;
        }
        result = valid ? 0 : 33;
    }
    return result;
}

static int fixture_execute(const char* sidecar)
{
    FILE* file = fopen(sidecar, "r");
    char scenario[64] = {0};
    unsigned buster = 0;
    unsigned tier1 = 0;
    bool valid = file && fscanf(file, "%63s %u %u", scenario, &buster, &tier1) == 3;
    if (file)
    {
        valid = fclose(file) == 0 && valid;
    }
    int result = 0;
    if (!valid)
    {
        result = 41;
    }
    else if (buster && tier1 && strcmp(scenario, "run-error") == 0)
    {
        result = 42;
    }
    else if (buster && tier1 && strcmp(scenario, "run-crash") == 0)
    {
        raise(SIGILL);
    }
    else if (buster && tier1 && strcmp(scenario, "run-timeout") == 0)
    {
        result = fixture_tree(true);
    }
    else if (buster && tier1 && strcmp(scenario, "run-flood") == 0)
    {
        result = fixture_flood();
    }
    else if (buster && tier1 && strcmp(scenario, "empty-output") == 0)
    {
        result = 0;
    }
    else
    {
        puts(buster && tier1 && strcmp(scenario, "mismatch") == 0 ? "43" : "42");
    }
    return result;
}

int main(int argc, char** argv)
{
    bool csmith = false;
    bool yarpgen = false;
    bool buster = false;
    bool clang = false;
    bool tier1 = false;
    bool seeded = false;
    bool c_mode = false;
    bool out_dir = false;
    bool multifile = false;
    bool second_source = false;
    const char* output = 0;
    const char* source = 0;
    const char* scenario = "pass";
    int result = 0;
    for (int i = 1; i < argc; ++i)
    {
        csmith |= strcmp(argv[i], "--fixture-csmith") == 0;
        yarpgen |= strcmp(argv[i], "--fixture-yarpgen") == 0;
        buster |= strcmp(argv[i], "--fixture-buster") == 0;
        clang |= strcmp(argv[i], "--fixture-clang") == 0;
        tier1 |= strcmp(argv[i], "--fixture-tier1") == 0;
        seeded |= strcmp(argv[i], "--seed") == 0 && i + 1 < argc;
        seeded |= strncmp(argv[i], "--seed=", 7) == 0;
        c_mode |= strcmp(argv[i], "--std=c") == 0;
        out_dir |= strcmp(argv[i], "--out-dir=.") == 0;
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc)
        {
            scenario = argv[++i];
        }
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            output = argv[++i];
        }
        else if (strcmp(argv[i], "probe.c") == 0 || strcmp(argv[i], "program.c") == 0 || strcmp(argv[i], "driver.c") == 0)
        {
            source = argv[i];
            multifile = strcmp(argv[i], "driver.c") == 0;
        }
        else if (strcmp(argv[i], "func.c") == 0)
        {
            second_source = true;
        }
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0)
    {
        puts("buster differential fixture 1; not a compiler or generator");
    }
    else if (argc == 2 && strcmp(argv[1], "--tree") == 0)
    {
        result = fixture_tree(false);
    }
    else if (argc == 2 && strcmp(argv[1], "--hang-tree") == 0)
    {
        result = fixture_tree(true);
    }
    else if (argc == 2 && strcmp(argv[1], "--interrupt") == 0)
    {
        kill(getppid(), SIGTERM);
        for (;;)
        {
            pause();
        }
    }
    else if (argc == 2 && strcmp(argv[1], "--flood") == 0)
    {
        result = fixture_flood();
    }
    else if (argc == 2 && strcmp(argv[1], "--stderr-flood") == 0)
    {
        result = dup2(STDERR_FILENO, STDOUT_FILENO) >= 0 ? fixture_flood() : 45;
    }
    else if (argc == 2 && strcmp(argv[1], "--descriptors") == 0)
    {
        char byte;
        bool descriptors_valid = read(STDIN_FILENO, &byte, 1) == 0;
        for (int descriptor = 3; descriptor < 64; ++descriptor)
        {
            descriptors_valid = fcntl(descriptor, F_GETFD) < 0 && errno == EBADF && descriptors_valid;
        }
        if (descriptors_valid)
        {
            puts("42");
        }
        result = descriptors_valid ? 0 : 46;
    }
    else if (argc == 2 && strcmp(argv[1], "--arguments") == 0)
    {
        puts("42");
    }
    else if (csmith || yarpgen)
    {
        result = fixture_generate(yarpgen, scenario, seeded, c_mode, out_dir);
    }
    else if (buster || clang)
    {
        result = fixture_compile(output, source, buster, tier1, multifile, second_source);
    }
    else if (argc == 1)
    {
        char self[PATH_MAX];
        const ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - sizeof(".fixture"));
        if (length > 0)
        {
            memcpy(self + length, ".fixture", sizeof(".fixture"));
            result = fixture_execute(self);
        }
        else
        {
            result = 43;
        }
    }
    else
    {
        result = 44;
    }
    return result;
}
