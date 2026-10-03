// Fixed Linux alternating process pairs. Only the child's reported admission
// span is a micro timing; process/oracle/setup work remains outside that span.
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct Sample Sample;
struct Sample { int ok; uint64_t elapsed; uint64_t checksum; };

static Sample run_sample(char const* executable, char const* prefix, char const* population, char const* shape, unsigned pair, unsigned variant)
{
    Sample result = {0};
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s-n%s-%s-p%02u-v%u.log", prefix, population, shape, pair, variant);
    int ready = length > 0 && (size_t)length < sizeof(path);
    pid_t child = ready ? fork() : -1;
    if (child == 0)
    {
        FILE* capture = fopen(path, "wb");
        ready = capture && dup2(fileno(capture), STDOUT_FILENO) >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0;
        if (capture) fclose(capture);
        alarm(60);
        char const* arguments[] = {executable, population, shape, NULL};
        if (ready) execv(executable, (char* const*)arguments);
        _exit(127);
    }
    else if (child > 0)
    {
        int status = 0;
        pid_t waited;
        do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
        result.ok = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        FILE* capture = fopen(path, "rb");
        unsigned parsed_population = 0;
        unsigned parsed_count = 0;
        unsigned operations = 0;
        unsigned requested_population = 0;
        int certificate = 0;
        char line[512];
        sscanf(population, "%u", &requested_population);
        if (capture)
        {
            while (fgets(line, sizeof(line), capture))
            {
                int fields = sscanf(line, "QUALITY313_ADMISSION population=%u count=%u operations=%u elapsed_ns=%" SCNu64
                                    " checksum=%" SCNx64, &parsed_population, &parsed_count, &operations, &result.elapsed, &result.checksum);
                if (fields == 5 && strstr(line, " oracle=pass\n")) certificate += 1;
            }
            int read_ok = !ferror(capture);
            result.ok = fclose(capture) == 0 && read_ok && result.ok;
        }
        else result.ok = 0;
        unsigned expected_count = requested_population < 4096 ? requested_population : 4096;
        result.ok = result.ok && certificate == 1 && parsed_population == requested_population &&
                    parsed_count == expected_count && operations == 4096 && result.elapsed > 0;
    }
    return result;
}

int main(int argc, char** argv)
{
    int ok = argc == 4;
    cpu_set_t affinity;
    int cpu = -1;
    if (ok && sched_getaffinity(0, sizeof(affinity), &affinity) == 0)
    {
        for (int index = 0; index < CPU_SETSIZE && cpu < 0; index += 1) if (CPU_ISSET(index, &affinity)) cpu = index;
        if (cpu >= 0)
        {
            CPU_ZERO(&affinity);
            CPU_SET(cpu, &affinity);
            ok = sched_setaffinity(0, sizeof(affinity), &affinity) == 0;
        }
        else ok = 0;
    }
    else ok = 0;
    if (ok)
    {
        char csv_path[4096];
        int length = snprintf(csv_path, sizeof(csv_path), "%s.csv", argv[1]);
        FILE* csv = length > 0 && (size_t)length < sizeof(csv_path) ? fopen(csv_path, "wb") : NULL;
        ok = csv != NULL;
        if (csv)
        {
            fprintf(csv, "schema,population,shape,cpu,pair,order,variant,ok,operations,elapsed_ns,ns_per_operation,checksum\n");
            char const* populations[] = {"16", "4096", "4099", "8192"};
            char const* shapes[] = {"equal", "ascending", "descending"};
            for (unsigned population = 0; population < 4; population += 1)
            {
                for (unsigned shape = 0; shape < 3; shape += 1)
                {
                    uint64_t expected[2] = {0};
                    int expected_set[2] = {0};
                    // Pair zero is retained warmup; twelve further pairs are
                    // fixed in advance. Failures never shorten or extend them.
                    for (unsigned pair = 0; pair <= 12; pair += 1)
                    {
                        for (unsigned order = 0; order < 2; order += 1)
                        {
                            unsigned variant = (pair + order) & 1u;
                            Sample sample = run_sample(argv[2 + variant], argv[1], populations[population], shapes[shape], pair, variant);
                            if (!pair) { expected[variant] = sample.checksum; expected_set[variant] = sample.ok; }
                            sample.ok = sample.ok && expected_set[variant] && sample.checksum == expected[variant];
                            if (population < 2 && expected_set[1 - variant]) sample.ok = sample.ok && sample.checksum == expected[1 - variant];
                            fprintf(csv, "1,%s,%s,%d,%u,%u,%u,%d,4096,%" PRIu64 ",%.6f,%016" PRIx64 "\n",
                                    populations[population], shapes[shape], cpu, pair, order, variant, sample.ok,
                                    sample.elapsed, (double)sample.elapsed / 4096.0, sample.checksum);
                            if (fflush(csv) != 0 || !sample.ok) ok = 0;
                        }
                    }
                }
            }
            ok = fclose(csv) == 0 && ok;
        }
    }
    if (!ok) fprintf(stderr, "quality313 micro collection failed; preserve all fixed raw samples\n");
    return ok ? 0 : 1;
}
