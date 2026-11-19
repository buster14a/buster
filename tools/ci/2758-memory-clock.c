// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Time a single test-ssa-source case's existing all-memory child on Linux.
// No interposition or changes to the compiler or its test harness.
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(clockid_t id)
{
    struct timespec t = { 0 };
    if (clock_gettime(id, &t) != 0) { perror("clock_gettime"); exit(2); }
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static int memory_child(pid_t driver)
{
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/task/%d/children", driver, driver);
    FILE *children = fopen(path, "r");
    int found = 0;
    if (children != NULL)
    {
        int pid = 0;
        while (!found && fscanf(children, "%d", &pid) == 1)
        {
            char cwd[4096];
            snprintf(path, sizeof(path), "/proc/%d/cwd", pid);
            ssize_t n = readlink(path, cwd, sizeof(cwd) - 1);
            if (n >= 0)
            {
                cwd[n] = 0;
                const char *name = strrchr(cwd, '/');
                if (name != NULL && strcmp(name, "/run-0-memory") == 0) found = pid;
            }
        }
        fclose(children);
    }
    return found;
}

static uint64_t born_ns(int pid)
{
    char path[96], row[4096];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *stat = fopen(path, "r");
    if (stat == NULL || fgets(row, sizeof(row), stat) == NULL) { perror("process stat"); exit(2); }
    fclose(stat);
    char *end = strrchr(row, ')'), *save = NULL;
    if (end == NULL) exit(2);
    char *token = strtok_r(end + 1, " ", &save);
    for (int field = 3; field < 22 && token != NULL; ++field) token = strtok_r(NULL, " ", &save);
    if (token == NULL) exit(2);
    uint64_t ticks = strtoull(token, NULL, 10);
    long hz = sysconf(_SC_CLK_TCK);
    if (ticks == 0 || hz <= 0) exit(2);
    return (ticks / (uint64_t)hz) * UINT64_C(1000000000) + (ticks % (uint64_t)hz) * UINT64_C(1000000000) / (uint64_t)hz;
}

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    pid_t driver = fork();
    if (driver == 0) { execv(argv[2], argv + 2); _exit(127); }
    if (driver < 0) { perror("fork"); return 2; }
    int pid = 0, status = 0;
    for (unsigned i = 0; i < 30000 && !pid; ++i)
    {
        pid = memory_child(driver);
        if (!pid && waitpid(driver, &status, WNOHANG) != 0) break;
        if (!pid) usleep(2000);
    }
    if (!pid) { fprintf(stderr, "all-memory child missing\n"); return 2; }
    uint64_t start = born_ns(pid);
    int pidfd = (int)syscall(SYS_pidfd_open, pid, 0);
    if (pidfd < 0) { perror("pidfd_open"); return 2; }
    clockid_t clock;
    if (clock_getcpuclockid(pid, &clock) != 0) { fprintf(stderr, "child CPU clock unavailable\n"); return 2; }
    struct perf_event_attr attr = { .type = PERF_TYPE_SOFTWARE, .size = sizeof(attr), .config = PERF_COUNT_SW_TASK_CLOCK };
    uint64_t before = now_ns(clock);
    int counter = (int)syscall(SYS_perf_event_open, &attr, pid, -1, -1, PERF_FLAG_FD_CLOEXEC);
    uint64_t after = now_ns(clock);
    struct pollfd exited = { .fd = pidfd, .events = POLLIN };
    uint64_t cpu = 0, uncertainty = 0, stop = 0;
    const char *method = "task-clock";
    if (counter >= 0)
    {
        int ready;
        do { ready = poll(&exited, 1, -1); } while (ready < 0 && errno == EINTR);
        stop = now_ns(CLOCK_BOOTTIME);
        if (ready != 1 || read(counter, &cpu, sizeof(cpu)) != (ssize_t)sizeof(cpu)) { perror("child task-clock"); return 2; }
        close(counter);
        uncertainty = after - before;
        cpu += before + uncertainty / 2;
    }
    else
    {
        // A hosted kernel can deny perf. Its process CPU clock is still available.
        // Bound the last unsampled CPU by the wall interval surrounding the final sample.
        method = "process-clock";
        cpu = after;
        uint64_t last = now_ns(CLOCK_BOOTTIME);
        for (;;)
        {
            int ready = poll(&exited, 1, 10);
            stop = now_ns(CLOCK_BOOTTIME);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) { perror("poll"); return 2; }
            if (ready > 0) break;
            struct timespec t;
            if (clock_gettime(clock, &t) == 0)
            {
                cpu = (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
                last = stop;
            }
        }
        uncertainty = stop - last;
    }
    close(pidfd);
    FILE *result = fopen(argv[1], "w");
    if (result == NULL) { perror("result"); return 2; }
    fprintf(result, "%s %.6f %.6f %.6f\n", method, (double)cpu / 1e9, (double)(stop - start) / 1e9, (double)uncertainty / 1e9);
    if (fclose(result) != 0) return 2;
    pid_t w;
    do { w = waitpid(driver, &status, 0); } while (w < 0 && errno == EINTR);
    return w == driver && WIFEXITED(status) ? WEXITSTATUS(status) : 2;
}
