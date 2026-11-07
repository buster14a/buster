// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Pure planning fixtures and subprocess probes used by build-benchmarks.sh's isolated integration tests.
#define main build_budget_program
#include "../../tools/ci/build-budget.c"
#undef main
#include <fcntl.h>

static bool build_test_plan(void)
{
    BuildCore cores[16];
    int count = 0;
    bool valid = build_core_add(cores, &count, (BuildCore){ .cpu = 3, .package = 0, .core = 0 });
    valid = !build_core_add(cores, &count, (BuildCore){ .cpu = 19, .package = 0, .core = 0 }) && valid;
    valid = build_core_add(cores, &count, (BuildCore){ .cpu = 31, .package = 1, .core = 0 }) && valid && count == 2;
    for (int i = 0; i < 16; i += 1)
    {
        cores[i] = (BuildCore){ .cpu = i * 3 + 1, .package = 0, .core = i };
    }
    for (int available = 1; available <= 16; available += 1)
    {
        for (int count_commands = 1; count_commands <= 3; count_commands += 1)
        {
            for (int singles = 0; singles < (1 << count_commands); singles += 1)
            {
                BuildCommand commands[3] = { 0 };
                for (int i = 0; i < count_commands; i += 1)
                {
                    commands[i].single = (singles & (1 << i)) != 0;
                }
                int batch = build_plan(commands, count_commands, cores, available);
                cpu_set_t used;
                CPU_ZERO(&used);
                int jobs = 0;
                for (int i = 0; i < batch; i += 1)
                {
                    cpu_set_t overlap;
                    CPU_AND(&overlap, &used, &commands[i].cpus);
                    valid = valid && CPU_COUNT(&overlap) == 0 && commands[i].jobs > 0 &&
                        CPU_COUNT(&commands[i].cpus) == commands[i].jobs && (!commands[i].single || commands[i].jobs == 1);
                    CPU_OR(&used, &used, &commands[i].cpus);
                    jobs += commands[i].jobs;
                }
                valid = valid && batch == (available < count_commands ? available : count_commands) && jobs <= available;
            }
        }
    }
    return valid;
}

static int build_test_probe(const char* directory, const char* name, const char* peer, const char* outcome)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE* file = fopen(path, "w");
    cpu_set_t allowed;
    int result = 1;
    const char* jobs = getenv("BUSTER_BUILD_JOBS");
    bool valid = file != NULL && jobs != NULL && sched_getaffinity(0, sizeof(allowed), &allowed) == 0 &&
        atoi(jobs) > 0 && CPU_COUNT(&allowed) == atoi(jobs);
    if (file != NULL)
    {
        fprintf(file, "%ld %s ", (long)getpid(), jobs == NULL ? "missing" : jobs);
        if (valid)
        {
            for (int i = 0; i < CPU_SETSIZE; i += 1)
            {
                if (CPU_ISSET(i, &allowed)) fprintf(file, "%d,", i);
            }
        }
        fprintf(file, "\n");
        fclose(file);
    }
    if (valid)
    {
        if (strcmp(outcome, "hold") == 0 || strcmp(outcome, "orphan") == 0 || strcmp(outcome, "escape") == 0 || strcmp(outcome, "escape-orphan") == 0)
        {
            pid_t child = fork();
            if (child == 0)
            {
                if (strncmp(outcome, "escape", 6) == 0 && setsid() < 0) _exit(1);
                signal(SIGTERM, SIG_IGN);
                snprintf(path, sizeof(path), "%s/%s-child", directory, name);
                file = fopen(path, "w");
                if (file == NULL) _exit(1);
                fprintf(file, "%ld\n", (long)getpid());
                fclose(file);
                for (;;) pause();
            }
            valid = child > 0;
            if (valid && (strcmp(outcome, "orphan") == 0 || strcmp(outcome, "escape-orphan") == 0))
            {
                // Parent exits before its descendant: the successful leader is not the build barrier.
                result = 0;
            }
            else
            {
                while (valid) pause();
            }
        }
        else
        {
            if (strcmp(peer, "-") != 0)
            {
                snprintf(path, sizeof(path), "%s/%s", directory, peer);
                double deadline = build_seconds() + 5.0;
                while (access(path, F_OK) != 0 && build_seconds() < deadline)
                {
                    const struct timespec pause = { .tv_nsec = 1000000 };
                    nanosleep(&pause, NULL);
                }
                valid = access(path, F_OK) == 0;
            }
            const struct timespec pause = { .tv_nsec = 100000000 };
            nanosleep(&pause, NULL);
            result = valid ? atoi(outcome) : 1;
        }
    }
    return result;
}

int main(int argc, char** argv)
{
    int result = 1;
    if (argc == 1)
    {
        result = build_test_plan() ? 0 : 1;
        printf("build-budget planning fixtures: %s\n", result == 0 ? "passed" : "FAILED");
    }
    else if (argc == 2 && strcmp(argv[1], "--cores") == 0)
    {
        BuildCore cores[CPU_SETSIZE];
        int count = build_cores(cores);
        for (int i = 0; i < count; i += 1) printf("%d\n", cores[i].cpu);
        result = count > 0 ? 0 : 1;
    }
    else if (argc == 6 && strcmp(argv[1], "--probe") == 0)
    {
        result = build_test_probe(argv[2], argv[3], argv[4], argv[5]);
    }
    return result;
}
