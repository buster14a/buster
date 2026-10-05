// rusage: run a command, print wall/user/sys time, peak RSS and faults of the
// waited child as one line. C only; no dependencies beyond POSIX.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/resource.h>

int main(int argc, char** argv)
{
    int result = 1;
    if (argc < 2)
    {
        fprintf(stderr, "usage: rusage command [args...]\n");
    }
    else
    {
        struct timespec start;
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        pid_t pid = fork();
        if (pid == 0)
        {
            execvp(argv[1], argv + 1);
            _exit(127);
        }
        int status = 0;
        struct rusage usage;
        memset(&usage, 0, sizeof(usage));
        wait4(pid, &status, 0, &usage);
        clock_gettime(CLOCK_MONOTONIC, &end);
        double wall_ms = (double)(end.tv_sec - start.tv_sec) * 1000.0 + (double)(end.tv_nsec - start.tv_nsec) / 1.0e6;
        double user_ms = (double)usage.ru_utime.tv_sec * 1000.0 + (double)usage.ru_utime.tv_usec / 1000.0;
        double sys_ms = (double)usage.ru_stime.tv_sec * 1000.0 + (double)usage.ru_stime.tv_usec / 1000.0;
        printf("RUSAGE exit=%d wall_ms=%.3f user_ms=%.3f sys_ms=%.3f maxrss_kib=%ld minflt=%ld majflt=%ld inblock=%ld\n",
               WIFEXITED(status) ? WEXITSTATUS(status) : -1, wall_ms, user_ms, sys_ms, usage.ru_maxrss, usage.ru_minflt,
               usage.ru_majflt, usage.ru_inblock);
        result = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    return result;
}
