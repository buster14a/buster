#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* This observer is intentionally separate from the compiler under test. */
static int execute(char **args, char const *stem, char const *phase)
{
    int result = 125;
    char path[512];
    snprintf(path, sizeof(path), "%s.%s.argv", stem, phase);
    FILE *record = fopen(path, "w");
    if (record)
    {
        for (unsigned i = 0; args[i]; i += 1)
        {
            fprintf(record, "%s\n", args[i]);
        }
        fclose(record);
        pid_t child = fork();
        if (child == 0)
        {
            snprintf(path, sizeof(path), "%s.%s.stdout", stem, phase);
            int out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            snprintf(path, sizeof(path), "%s.%s.stderr", stem, phase);
            int err = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (out < 0 || err < 0 || dup2(out, 1) < 0 || dup2(err, 2) < 0)
            {
                _exit(126);
            }
            close(out);
            close(err);
            execvp(args[0], args);
            _exit(127);
        }
        if (child > 0)
        {
            int status = 0;
            pid_t waited;
            do
            {
                waited = waitpid(child, &status, 0);
            } while (waited < 0 && errno == EINTR);
            if (waited == child)
            {
                result = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 125;
            }
        }
    }
    return result;
}
/* Confirm profiles: Clang/GCC O0, O2, ASan+UBSan; then four Buster
 * allocators x two frontend forms x O0/O2. All source checks expect zero. */
int main(int argc, char **argv)
{
    int failed = 0;
    FILE *results = argc == 3 ? fopen("evidence/results.tsv", "w") : 0;
    if (!results)
    {
        fprintf(stderr, "usage: observer /absolute/ide /absolute/cases.c (evidence/ must exist)\n");
        failed = 1;
    }
    else
    {
        fprintf(results, "profile\tcase\tstorage\tcompile\trun\n");
        for (unsigned profile = 0; profile < 22; profile += 1)
        {
            for (unsigned test = 0; test < 16; test += 1)
            {
                for (unsigned storage = 0; storage < 3; storage += 1)
                {
                    char stem[256], output[256], test_flag[64], storage_flag[64];
                    snprintf(stem, sizeof(stem), "evidence/p%u-c%02u-s%u", profile, test, storage);
                    snprintf(output, sizeof(output), "./evidence/p%u-c%02u-s%u.bin", profile, test, storage);
                    snprintf(test_flag, sizeof(test_flag), "-DCASE=%u", test);
                    snprintf(storage_flag, sizeof(storage_flag), "-DSTORAGE=%u", storage);
                    char *args[40];
                    unsigned n = 0;
                    args[n++] = "timeout"; args[n++] = "-k"; args[n++] = "2s"; args[n++] = "30s";
                    args[n++] = profile < 3 ? "clang" : profile < 6 ? "gcc" : argv[1];
                    if (profile >= 6) { args[n++] = "cc"; }
                    args[n++] = "-std=c17";
                    args[n++] = "-fwrapv"; args[n++] = "-fno-strict-aliasing";
                    args[n++] = "-funsigned-char"; args[n++] = "-g0";
                    unsigned mode = profile < 6 ? profile % 3 : (profile - 6) % 2;
                    args[n++] = mode == 2 ? "-O1" : mode == 1 ? "-O2" : "-O0";
                    if (profile < 6 && mode == 2)
                    {
                        args[n++] = "-fsanitize=address,undefined";
                        args[n++] = "-fno-sanitize-recover=all";
                    }
                    if (profile < 3) { args[n++] = "--target=x86_64-linux-gnu"; }
                    else if (profile < 6) { args[n++] = "-m64"; }
                    else
                    {
                        args[n++] = "-target"; args[n++] = "x86_64-linux";
                        char *allocators[] = {"-fregister-allocator=none", "-fregister-allocator=mir-stack", "-fregister-allocator=fast", "-fregister-allocator=quality"};
                        unsigned allocator = (profile - 6) / 4;
                        args[n++] = allocators[allocator];
                        args[n++] = "-fverify-codegen";
                        if (allocator != 0) { args[n++] = "-fno-machine-fallback"; }
                        args[n++] = ((profile - 6) / 2) % 2 == 0 ? "-ffrontend-ssa" : "-fno-frontend-ssa";
                    }
                    args[n++] = test_flag; args[n++] = storage_flag;
                    args[n++] = argv[2]; args[n++] = "-o"; args[n++] = output; args[n] = 0;
                    int compiled = execute(args, stem, "compile");
                    int ran = -1;
                    if (compiled == 0)
                    {
                        char *run[] = {"timeout", "-k", "2s", "5s", output, 0};
                        ran = execute(run, stem, "run");
                    }
                    fprintf(results, "%u\t%u\t%u\t%d\t%d\n", profile, test, storage, compiled, ran);
                    fflush(results);
                    printf("profile=%u case=%u storage=%u compile=%d run=%d\n", profile, test, storage, compiled, ran);
                    failed |= compiled != 0 || ran != 0;
                }
            }
        }
        fclose(results);
    }
    return failed;
}
