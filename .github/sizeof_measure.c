// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original issue-822 fixture and process timer; run in an empty evidence directory, pinned externally.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        perror("clock_gettime");
        exit(1);
    }
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

static bool sample(const char* executable, unsigned mode, const char* source, const char* log, double* wall, double* check)
{
    const double start = milliseconds();
    const pid_t child = fork();
    if (child == 0)
    {
        const int errors = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        const int null = open("/dev/null", O_RDWR);
        if (errors < 0 || null < 0 || dup2(errors, STDERR_FILENO) < 0 || dup2(null, STDOUT_FILENO) < 0)
        {
            _exit(127);
        }
        close(errors);
        close(null);
        char* arguments[10];
        unsigned count = 0;
        arguments[count++] = (char*)executable;
        if (mode != 3)
        {
            arguments[count++] = "--time-stages";
        }
        if (mode == 2)
        {
            arguments[count++] = "--scalar";
        }
        arguments[count++] = "-fsyntax-only";
        arguments[count++] = "-std=gnu17";
        arguments[count++] = (char*)source;
        arguments[count] = NULL;
        execvp(executable, arguments);
        _exit(127);
    }
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
    }
    *wall = milliseconds() - start;
    *check = -1.0;
    bool valid = waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    FILE* stream = fopen(log, "r");
    char line[1024];
    while (stream != NULL && fgets(line, sizeof(line), stream) != NULL)
    {
        double parsed;
        if (sscanf(line, "time-stage\tcheck-input\t%lf", &parsed) == 1)
        {
            *check = parsed;
        }
        if (strstr(line, "warning:") != NULL || strstr(line, "error:") != NULL)
        {
            valid = false;
        }
    }
    valid = valid && stream != NULL && !ferror(stream) && (mode == 3 || *check >= 0.0);
    if (stream != NULL)
    {
        valid = fclose(stream) == 0 && valid;
    }
    return valid;
}

int main(int argc, char** argv)
{
    bool valid = argc == 4;
    if (!valid)
    {
        fprintf(stderr, "usage: sizeof-measure BASELINE CANDIDATE GCC\n");
    }
    const unsigned depths[] = { 500, 1000, 2000, 4000, 8000, 16000 };
    const char* names[] = { "baseline", "vector", "scalar", "gcc" };
    puts("form,depth,mode,repeat,wall_ms,check_input_ms");
    for (unsigned form = 0; valid && form < 2; form += 1)
    {
        for (unsigned size = 0; valid && size < sizeof(depths) / sizeof(depths[0]); size += 1)
        {
            const unsigned depth = depths[size];
            char source[64];
            snprintf(source, sizeof(source), "%s-%u.c", form == 0 ? "expression" : "type", depth);
            FILE* stream = fopen(source, "w");
            valid = stream != NULL;
            if (valid)
            {
                fputs("int x = ", stream);
                for (unsigned i = 0; i < depth; i += 1)
                {
                    fputs("sizeof(", stream);
                }
                fputs(form == 0 ? "0" : "int", stream);
                for (unsigned i = 0; i < depth; i += 1)
                {
                    fputc(')', stream);
                }
                fputs(";\n", stream);
                valid = !ferror(stream);
                valid = fclose(stream) == 0 && valid;
            }
            for (unsigned repeat = 0; valid && repeat < 3; repeat += 1)
            {
                for (unsigned step = 0; valid && step < 4; step += 1)
                {
                    const unsigned mode = (step + repeat) % 4;
                    char log[128];
                    snprintf(log, sizeof(log), "%s-%s-%u.log", source, names[mode], repeat);
                    double wall;
                    double check;
                    valid = sample(argv[mode == 0 ? 1 : mode == 3 ? 3 : 2], mode, source, log, &wall, &check);
                    printf("%s,%u,%s,%u,%.3f,%.3f\n", form == 0 ? "expression" : "type", depth, names[mode], repeat, wall, check);
                    fflush(stdout);
                    if (!valid)
                    {
                        fprintf(stderr, "compile or diagnostic check failed: %s\n", log);
                    }
                }
            }
        }
    }
    return valid ? 0 : 1;
}
