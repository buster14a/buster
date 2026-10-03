// Research-only Linux process collector. Fixed alternating A/B samples,
// no instrumentation on the timed path, all raw logs retained on failure.
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROBE_PAIRS 12u

typedef struct Sample Sample;
struct Sample
{
    int ok;
    uint64_t wall_ns;
    uint64_t user_us;
    uint64_t system_us;
    uint64_t peak_rss_bytes;
    uint64_t hash;
    uint64_t measured_ns;
};

static uint64_t ns_now(void)
{
    struct timespec value;
    int ok = clock_gettime(CLOCK_MONOTONIC, &value) == 0;
    uint64_t result = ok ? (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec : 0;
    return result;
}

static uint64_t file_hash(char const* path)
{
    FILE* file = fopen(path, "rb");
    uint64_t hash = UINT64_C(14695981039346656037);
    int ok = file != NULL;
    if (file)
    {
        unsigned char bytes[4096];
        size_t count;
        while ((count = fread(bytes, 1, sizeof(bytes), file)) != 0)
        {
            for (size_t index = 0; index < count; index += 1) hash = (hash ^ bytes[index]) * UINT64_C(1099511628211);
        }
        ok = !ferror(file);
        ok = fclose(file) == 0 && ok;
    }
    return ok ? hash : 0;
}

static Sample run_sample(char* const* arguments, char const* prefix, char const* kind, char const* mode,
    unsigned pair, unsigned variant, int runtime)
{
    Sample result = {0};
    char log[4096];
    snprintf(log, sizeof(log), "%s-%s-%s-p%02u-v%u.log", prefix, kind, mode, pair, variant);
    uint64_t before = ns_now();
    pid_t child = fork();
    if (child == 0)
    {
        FILE* capture = fopen(log, "wb");
        int ready = capture && dup2(fileno(capture), STDOUT_FILENO) >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0;
        if (capture) fclose(capture);
        // The timer survives exec; a hung compiler/probe cannot consume the
        // remaining fixed sample population or hang the entire job forever.
        alarm(60);
        if (ready) execv(arguments[0], arguments);
        _exit(127);
    }
    else if (child > 0)
    {
        int status = 0;
        struct rusage usage = {0};
        pid_t waited;
        do { waited = wait4(child, &status, 0, &usage); } while (waited < 0 && errno == EINTR);
        uint64_t after = ns_now();
        result.ok = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 && before && after > before;
        result.wall_ns = after - before;
        result.user_us = (uint64_t)usage.ru_utime.tv_sec * UINT64_C(1000000) + usage.ru_utime.tv_usec;
        result.system_us = (uint64_t)usage.ru_stime.tv_sec * UINT64_C(1000000) + usage.ru_stime.tv_usec;
        result.peak_rss_bytes = (uint64_t)usage.ru_maxrss * 1024;
        result.hash = runtime ? 0 : file_hash(arguments[12]);
        if (runtime && result.ok)
        {
            FILE* capture = fopen(log, "rb");
            char line[512];
            unsigned checks = 0;
            unsigned seeds = 0;
            unsigned calls = 0;
            if (capture)
            {
                while (fgets(line, sizeof(line), capture))
                {
                    sscanf(line, "QUALITY313_ORACLE checks=%u seeds=%u checksum=%" SCNx64, &checks, &seeds, &result.hash);
                    sscanf(line, "QUALITY313_RUNTIME calls=%u elapsed_ns=%" SCNu64, &calls, &result.measured_ns);
                }
                fclose(capture);
            }
            result.ok = result.ok && checks == 4097 && seeds == 4 && calls == 256 && result.hash && result.measured_ns;
        }
        else if (!runtime) result.ok = result.ok && result.hash;
    }
    return result;
}

int main(int argc, char** argv)
{
    int runtime = argc == 6 && !strcmp(argv[1], "runtime");
    int compile = argc == 7 && !strcmp(argv[1], "compile");
    int ok = runtime || compile;
    cpu_set_t allowed;
    int cpu = -1;
    if (ok && sched_getaffinity(0, sizeof(allowed), &allowed) == 0)
    {
        for (int index = 0; index < CPU_SETSIZE && cpu < 0; index += 1) if (CPU_ISSET(index, &allowed)) cpu = index;
        if (cpu >= 0)
        {
            CPU_ZERO(&allowed);
            CPU_SET(cpu, &allowed);
            ok = sched_setaffinity(0, sizeof(allowed), &allowed) == 0;
        }
        else ok = 0;
    }
    else ok = 0;
    if (ok)
    {
        char csv_path[4096];
        snprintf(csv_path, sizeof(csv_path), "%s-%s-%s.csv", argv[2], argv[1], argv[3]);
        FILE* csv = fopen(csv_path, "wb");
        ok = csv != NULL;
        if (csv)
        {
            fprintf(csv, "schema,kind,mode,cpu,pair,order,variant,ok,wall_ns,user_us,system_us,peak_rss_bytes,output_fnv64,probe_ns\n");
            uint64_t expected[2] = {0};
            // Pair zero is an untallied warmup and remains in the raw record.
            for (unsigned pair = 0; pair <= PROBE_PAIRS; pair += 1)
            {
                for (unsigned order = 0; order < 2; order += 1)
                {
                    unsigned variant = (pair + order) & 1u;
                    char output[4096];
                    snprintf(output, sizeof(output), "%s-%s-v%u.o", argv[2], argv[3], variant);
                    char mode[64];
                    snprintf(mode, sizeof(mode), "-fregister-allocator=%s", argv[3]);
                    char* fallback = !strcmp(argv[3], "none") ? "-fmachine-fallback" : "-fno-machine-fallback";
                    char* compile_arguments[] = {argv[4 + variant], "cc", "-target", "x86_64-unknown-linux", "-g0", "-O0",
                        mode, fallback, "-nostdinc", "-c", compile ? argv[6] : NULL, "-o", output, NULL};
                    char* runtime_arguments[] = {argv[4 + variant], NULL};
                    Sample sample = run_sample(runtime ? runtime_arguments : compile_arguments, argv[2], argv[1], argv[3], pair, variant, runtime);
                    if (!pair) expected[variant] = sample.hash;
                    sample.ok = sample.ok && expected[variant] && sample.hash == expected[variant];
                    if (runtime) sample.ok = sample.ok && (!expected[1 - variant] || sample.hash == expected[1 - variant]);
                    fprintf(csv, "1,%s,%s,%d,%u,%u,%u,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%016" PRIx64 ",%" PRIu64 "\n",
                        argv[1], argv[3], cpu, pair, order, variant, sample.ok, sample.wall_ns, sample.user_us,
                        sample.system_us, sample.peak_rss_bytes, sample.hash, sample.measured_ns);
                    fflush(csv);
                    if (!sample.ok) ok = 0;
                }
            }
            ok = fclose(csv) == 0 && ok;
        }
    }
    if (!ok) fprintf(stderr, "quality313 collect failed; preserve CSV/raw logs, no timing verdict\n");
    return ok ? 0 : 1;
}
