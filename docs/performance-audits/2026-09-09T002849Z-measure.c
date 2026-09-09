// Linux compiler measurement launcher. Fork from this small native process,
// not from a large Python parent: Linux can otherwise carry the parent's
// pre-exec RSS high-water mark into wait4's child measurement. The compiler
// gets a constant argv[0], and the measured interval includes fork/exec/wait.
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef __linux__
#error allocation_measure reports Linux wait4 RSS in KiB
#endif

int main(int argc, char** argv)
{
    int result = 1;
    if (argc < 5 || strcmp(argv[3], "--"))
    {
        fprintf(stderr, "usage: allocation_measure COMPILER REPORT.json -- cc [arguments...]\n");
    }
    else
    {
        int ready = 1;
        char const* cpu_text = getenv("BUSTER_BENCH_CPU");
        if (cpu_text)
        {
            char* end = 0;
            errno = 0;
            unsigned long cpu = strtoul(cpu_text, &end, 10);
            if (errno || !*cpu_text || *end || cpu >= CPU_SETSIZE)
            {
                fprintf(stderr, "invalid BUSTER_BENCH_CPU\n");
                ready = 0;
            }
            else
            {
                cpu_set_t set;
                CPU_ZERO(&set);
                CPU_SET(cpu, &set);
                if (sched_setaffinity(0, sizeof(set), &set))
                {
                    perror("sched_setaffinity");
                    ready = 0;
                }
            }
        }
        struct timespec start;
        if (ready && clock_gettime(CLOCK_MONOTONIC, &start))
        {
            perror("clock_gettime");
            ready = 0;
        }
        if (ready)
        {
            pid_t child = fork();
            if (child == 0)
            {
                argv[3] = (char*)"buster-cc";
                execv(argv[1], argv + 3);
                perror("execv compiler");
                _Exit(127);
            }
            else if (child < 0)
            {
                perror("fork");
            }
            else
            {
                int status = 0;
                struct rusage usage;
                pid_t waited;
                do
                {
                    waited = wait4(child, &status, 0, &usage);
                } while (waited < 0 && errno == EINTR);
                struct timespec finish;
                if (waited < 0 || clock_gettime(CLOCK_MONOTONIC, &finish))
                {
                    perror("wait4/clock_gettime");
                }
                else
                {
                    result = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
                    uint64_t wall_ns = (uint64_t)(finish.tv_sec - start.tv_sec) * UINT64_C(1000000000) +
                                       (uint64_t)finish.tv_nsec - (uint64_t)start.tv_nsec;
                    FILE* report = fopen(argv[2], "wb");
                    if (!report)
                    {
                        perror("measurement report");
                        result = 1;
                    }
                    else
                    {
                        int written = fprintf(report,
                            "{\"wall_ns\":%" PRIu64 ",\"rss_kib\":%ld,\"user_us\":%" PRIu64 ",\"system_us\":%" PRIu64
                            ",\"minor_faults\":%ld,\"major_faults\":%ld,\"voluntary_switches\":%ld,\"involuntary_switches\":%ld,\"exit_code\":%d}\n",
                            wall_ns, usage.ru_maxrss,
                            (uint64_t)usage.ru_utime.tv_sec * UINT64_C(1000000) + (uint64_t)usage.ru_utime.tv_usec,
                            (uint64_t)usage.ru_stime.tv_sec * UINT64_C(1000000) + (uint64_t)usage.ru_stime.tv_usec,
                            usage.ru_minflt, usage.ru_majflt, usage.ru_nvcsw, usage.ru_nivcsw, result);
                        int closed = fclose(report);
                        if (written < 0 || closed)
                        {
                            fprintf(stderr, "could not write measurement report\n");
                            result = 1;
                        }
                    }
                }
            }
        }
    }
    return result;
}
