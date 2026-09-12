// Paired whole-process replay of the registered ELF DSO benchmark inputs.
// Generation, output comparison and program execution are outside link timing.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct Observation Observation;
struct Observation
{
    uint64_t ns;
    struct rusage usage;
    int status;
};

static uint64_t now_ns(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static Observation run(char* const* arguments, const char* log)
{
    Observation observation = {.status = -1};
    int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    uint64_t start = now_ns();
    pid_t child = fd >= 0 ? fork() : -1;
    if (!child)
    {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
        execv(arguments[0], arguments);
        _exit(127);
    }
    if (fd >= 0) close(fd);
    if (child > 0)
    {
        pid_t waited;
        do waited = wait4(child, &observation.status, 0, &observation.usage);
        while (waited < 0 && errno == EINTR);
        if (waited < 0) observation.status = -1;
    }
    observation.ns = now_ns() - start;
    return observation;
}

static int equal_files(const char* first, const char* second)
{
    FILE* left = fopen(first, "rb");
    FILE* right = fopen(second, "rb");
    int equal = left && right;
    unsigned char a[65536];
    unsigned char b[65536];
    while (equal)
    {
        size_t n = fread(a, 1, sizeof(a), left);
        size_t m = fread(b, 1, sizeof(b), right);
        equal = n == m && !memcmp(a, b, n) && !ferror(left) && !ferror(right);
        if (!n || !m) break;
    }
    if (left) fclose(left);
    if (right) fclose(right);
    return equal;
}

int main(int argc, char** argv)
{
    int success = argc == 6;
    if (!success) fprintf(stderr, "usage: collect BASELINE CANDIDATE DSO_ROOT OUTPUT_DIR TINY_OBJECT\n");
    unsigned counts[] = {4, 128, 256, 512, 1024, 2048, 4096};
    char csv_path[4096];
    FILE* csv = NULL;
    if (success)
    {
        snprintf(csv_path, sizeof(csv_path), "%s/paired.csv", argv[4]);
        csv = fopen(csv_path, "w");
        success = csv != NULL;
    }
    if (success) fprintf(csv, "shape,count,pair,variant,wall_ns,user_us,system_us,maxrss_kb,status\n");
    for (unsigned shape = 0; success && shape < 3; shape += 1)
    {
        for (unsigned population = 0; success && population < (shape == 2 ? 1u : 7u); population += 1)
        {
            unsigned count = shape == 2 ? 0 : counts[population];
            char directory[4096];
            char object[4096];
            char outputs[2][4096];
            char log[4096];
            snprintf(directory, sizeof(directory), "%s/shape-%u-count-%u", argv[3], shape, count);
            if (shape == 2) snprintf(object, sizeof(object), "%s", argv[5]);
            else snprintf(object, sizeof(object), "%s/main.o", directory);
            for (unsigned variant = 0; variant < 2; variant += 1)
                snprintf(outputs[variant], sizeof(outputs[variant]), "%s/shape-%u-count-%u-%u", argv[4], shape, count, variant);
            snprintf(log, sizeof(log), "%s/shape-%u-count-%u.log", argv[4], shape, count);
            for (int pair = -1; success && pair < 8; pair += 1)
            {
                for (unsigned order = 0; success && order < 2; order += 1)
                {
                    unsigned variant = pair >= 0 && pair % 2 ? 1 - order : order;
                    char* command[12] = {argv[variant + 1], "cc", "-g0"};
                    unsigned n = 3;
                    if (shape != 2)
                    {
                        command[n++] = "-L";
                        command[n++] = directory;
                        command[n++] = "-ldataprobe";
                    }
                    command[n++] = object;
                    command[n++] = "-o";
                    command[n++] = outputs[variant];
                    command[n] = NULL;
                    Observation observation = run(command, log);
                    struct timeval user = observation.usage.ru_utime;
                    struct timeval system = observation.usage.ru_stime;
                    fprintf(csv, "%u,%u,%d,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%ld,%d\n", shape, count, pair, variant,
                        observation.ns, (uint64_t)user.tv_sec * 1000000 + (uint64_t)user.tv_usec,
                        (uint64_t)system.tv_sec * 1000000 + (uint64_t)system.tv_usec, observation.usage.ru_maxrss, observation.status);
                    fflush(csv);
                    success = observation.status == 0;
                }
                if (success) success = equal_files(outputs[0], outputs[1]);
            }
            if (shape != 2) setenv("LD_LIBRARY_PATH", directory, 1);
            for (unsigned variant = 0; success && variant < 2; variant += 1)
            {
                char* command[] = {outputs[variant], NULL};
                success = run(command, log).status == 0;
            }
            printf("shape=%u count=%u equal_and_executed=%d\n", shape, count, success);
            fflush(stdout);
        }
    }
    if (csv) fclose(csv);
    return success ? 0 : 1;
}
