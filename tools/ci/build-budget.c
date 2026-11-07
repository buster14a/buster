// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Run at most three build commands on disjoint physical cores. A "single" command (the pragma oracle) gets one;
// "build" commands share the rest and receive BUSTER_BUILD_JOBS. See docs/ci-builds.md.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_BUILD_COMMANDS 3

typedef struct
{
    int cpu;
    int package;
    int core;
} BuildCore;

typedef struct
{
    char** arguments;
    bool single;
    cpu_set_t cpus;
    int jobs;
    pid_t pid;
} BuildCommand;

static volatile sig_atomic_t build_signal;

static void build_interrupted(int number)
{
    build_signal = number;
}

static double build_seconds(void)
{
    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static bool build_core_add(BuildCore* cores, int* count, BuildCore core)
{
    bool added = true;
    for (int i = 0; i < *count; i += 1)
    {
        added = added && (cores[i].package != core.package || cores[i].core != core.core);
    }
    if (added)
    {
        cores[*count] = core;
        *count += 1;
    }
    return added;
}

static bool build_topology(int cpu, const char* name, int* value)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/%s", cpu, name);
    FILE* file = fopen(path, "r");
    bool valid = file != NULL;
    if (valid)
    {
        char extra;
        valid = fscanf(file, "%d %c", value, &extra) == 1 && *value >= 0;
        fclose(file);
    }
    return valid;
}

static int build_cores(BuildCore* cores)
{
    cpu_set_t allowed;
    int count = 0;
    bool valid = sched_getaffinity(0, sizeof(allowed), &allowed) == 0;
    for (int cpu = 0; valid && cpu < CPU_SETSIZE; cpu += 1)
    {
        if (CPU_ISSET(cpu, &allowed))
        {
            BuildCore core = { .cpu = cpu };
            valid = build_topology(cpu, "physical_package_id", &core.package) && build_topology(cpu, "core_id", &core.core);
            if (valid)
            {
                build_core_add(cores, &count, core);
            }
        }
    }
    if (!valid)
    {
        fprintf(stderr, "build-budget: cannot resolve allowed physical cores; refusing to guess\n");
        count = 0;
    }
    return count;
}

// One core per command first, then the spare cores, evenly, to build commands. The caller serializes batches when
// fewer cores than commands are allowed; neither SMT siblings nor zero-job builds can oversubscribe the grant.
static int build_plan(BuildCommand* commands, int count, const BuildCore* cores, int core_count)
{
    int batch = count < core_count ? count : core_count;
    int used = batch;
    for (int i = 0; i < batch; i += 1)
    {
        commands[i].jobs = 1;
        CPU_ZERO(&commands[i].cpus);
    }
    bool progress = true;
    while (used < core_count && progress)
    {
        progress = false;
        for (int i = 0; i < batch && used < core_count; i += 1)
        {
            if (!commands[i].single)
            {
                commands[i].jobs += 1;
                used += 1;
                progress = true;
            }
        }
    }
    int next = 0;
    for (int i = 0; i < batch; i += 1)
    {
        for (int j = 0; j < commands[i].jobs; j += 1)
        {
            CPU_SET(cores[next].cpu, &commands[i].cpus);
            next += 1;
        }
    }
    return batch;
}

static void build_kill(BuildCommand* commands, int count, int number)
{
    for (int i = 0; i < count; i += 1)
    {
        if (commands[i].pid > 0)
        {
            kill(-commands[i].pid, number);
        }
    }
}

// build.sh and Ninja can create nested process groups. Once their leaders die, the subreaper adopts their
// descendants; signal those direct children too, repeatedly during escalation, without depending on their group.
static bool build_kill_adopted(int number)
{
    DIR* directory = opendir("/proc");
    bool valid = directory != NULL;
    if (valid)
    {
        struct dirent* entry = NULL;
        while ((entry = readdir(directory)) != NULL)
        {
            char* end = NULL;
            long child = strtol(entry->d_name, &end, 10);
            if (child > 0 && child <= INT_MAX && *end == 0)
            {
                char path[80];
                snprintf(path, sizeof(path), "/proc/%ld/status", child);
                FILE* file = fopen(path, "r");
                if (file != NULL)
                {
                    char line[256];
                    long parent = 0;
                    while (fgets(line, sizeof(line), file) != NULL)
                    {
                        if (sscanf(line, "PPid: %ld", &parent) == 1)
                        {
                            break;
                        }
                    }
                    fclose(file);
                    if (parent == (long)getpid() && number != 0)
                    {
                        // An owned child cannot have its PID reused until this thread reaps it.
                        kill((pid_t)child, number);
                    }
                }
            }
        }
        closedir(directory);
    }
    return valid;
}

static int build_run(BuildCommand* commands, int count)
{
    int result = 0;
    int launched = 0;
    for (int i = 0; i < count && result == 0 && build_signal == 0; i += 1)
    {
        BuildCommand* command = &commands[i];
        fprintf(stderr, "build-budget: command %d, jobs=%d, CPUs=", i + 1, command->jobs);
        for (int cpu = 0; cpu < CPU_SETSIZE; cpu += 1)
        {
            if (CPU_ISSET(cpu, &command->cpus))
            {
                fprintf(stderr, "%d,", cpu);
            }
        }
        fprintf(stderr, "\n");
        command->pid = fork();
        if (command->pid == 0)
        {
            struct sigaction normal = { .sa_handler = SIG_DFL };
            sigaction(SIGINT, &normal, NULL);
            sigaction(SIGTERM, &normal, NULL);
            sigaction(SIGHUP, &normal, NULL);
            char jobs[16];
            snprintf(jobs, sizeof(jobs), "%d", command->jobs);
            if (setpgid(0, 0) != 0 || sched_setaffinity(0, sizeof(command->cpus), &command->cpus) != 0 ||
                setenv("BUSTER_BUILD_JOBS", jobs, 1) != 0)
            {
                perror("build-budget: child setup");
                _exit(125);
            }
            execvp(command->arguments[0], command->arguments);
            perror("build-budget: exec");
            _exit(127);
        }
        if (command->pid < 0)
        {
            perror("build-budget: fork");
            result = 125;
        }
        else
        {
            // Both sides set the group, so cancellation cannot miss a child between fork and exec.
            if (setpgid(command->pid, command->pid) != 0 && errno != EACCES && errno != ESRCH)
            {
                perror("build-budget: process group");
                result = 125;
            }
            launched += 1;
        }
    }
    bool stopping = false;
    double deadline = 0;
    bool children = launched != 0;
    int leaders = launched;
    while (children)
    {
        if (build_signal != 0)
        {
            result = 128 + build_signal;
        }
        if (result != 0 && !stopping)
        {
            build_kill(commands, launched, SIGTERM);
            stopping = true;
            deadline = build_seconds() + 1.0;
        }
        if (stopping && build_seconds() >= deadline)
        {
            build_kill(commands, launched, SIGKILL);
            if (!build_kill_adopted(SIGKILL))
            {
                fprintf(stderr, "build-budget: cannot read adopted children\n");
                result = 125;
            }
        }
        int status = 0;
        pid_t child = waitpid(-1, &status, WNOHANG);
        if (child > 0)
        {
            int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            if (code != 0 && result == 0)
            {
                result = code;
            }
            for (int i = 0; i < launched; i += 1)
            {
                if (commands[i].pid == child)
                {
                    leaders -= 1;
                    // A command returning with live background work is not a successful build barrier.
                    if (kill(-child, 0) == 0)
                    {
                        if (result == 0)
                        {
                            fprintf(stderr, "build-budget: command left background descendants\n");
                            result = 125;
                        }
                    }
                    else
                    {
                        commands[i].pid = 0;
                    }
                }
            }
        }
        else if (child < 0 && errno == ECHILD)
        {
            children = false;
        }
        else if (child < 0 && errno != EINTR)
        {
            perror("build-budget: wait");
            result = 125;
        }
        else
        {
            if (child == 0 && leaders == 0 && result == 0)
            {
                fprintf(stderr, "build-budget: commands left adopted descendants\n");
                result = 125;
            }
            const struct timespec pause = { .tv_nsec = 10000000 };
            nanosleep(&pause, NULL);
        }
    }
    if (build_signal != 0)
    {
        result = 128 + build_signal;
    }
    return result;
}

int main(int argc, char** argv)
{
    pid_t owner = getppid();
    BuildCommand commands[BUSTER_BUILD_COMMANDS] = { 0 };
    BuildCore cores[CPU_SETSIZE];
    int count = 0;
    bool valid = argc > 2;
    int next = 1;
    while (valid && next < argc)
    {
        valid = count < BUSTER_BUILD_COMMANDS && (strcmp(argv[next], "build") == 0 || strcmp(argv[next], "single") == 0);
        if (valid)
        {
            commands[count].single = strcmp(argv[next], "single") == 0;
            next += 1;
            commands[count].arguments = &argv[next];
            valid = next < argc && strcmp(argv[next], ":::") != 0;
            while (next < argc && strcmp(argv[next], ":::") != 0)
            {
                next += 1;
            }
            count += 1;
            if (next < argc)
            {
                argv[next++] = NULL;
                valid = valid && next < argc;
            }
        }
    }
    int result = 2;
    if (!valid)
    {
        fprintf(stderr, "usage: build-budget build|single COMMAND [ARG...] [::: build|single COMMAND [ARG...]] (up to three)\n");
    }
    else
    {
        int core_count = build_cores(cores);
        struct sigaction action = { .sa_handler = build_interrupted };
        valid = core_count > 0 && build_kill_adopted(0) && prctl(PR_SET_CHILD_SUBREAPER, 1) == 0 &&
            sigaction(SIGINT, &action, NULL) == 0 && sigaction(SIGTERM, &action, NULL) == 0 && sigaction(SIGHUP, &action, NULL) == 0 &&
            prctl(PR_SET_PDEATHSIG, SIGTERM) == 0;
        if (getppid() != owner)
        {
            build_signal = SIGTERM;
        }
        result = valid ? 0 : 125;
        double start = build_seconds();
        for (int first = 0; valid && first < count && result == 0; )
        {
            int batch = build_plan(commands + first, count - first, cores, core_count);
            result = build_run(commands + first, batch);
            first += batch;
        }
        fprintf(stderr, "build-budget: all commands reaped, status=%d, wall=%.3f s\n", result, build_seconds() - start);
    }
    return result;
}
