// Native process oracle for ./build.sh clang_analyze --self-test. Never part of
// the application or its compile database. Modes exercise real child outcomes.
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

int main(int argc, char** argv)
{
    int result = 0;
    bool dependency_scan = false;
    bool analyze = false;
    bool delay_analyze = false;
    const char* count_path = 0;
    const char* mutate_path = 0;
    const char* source = 0;
    for (int i = 1; i < argc; i += 1)
    {
        dependency_scan = dependency_scan || strcmp(argv[i], "-M") == 0;
        analyze = analyze || strcmp(argv[i], "--analyze") == 0;
        delay_analyze = delay_analyze || strcmp(argv[i], "-DFIXTURE_DELAY") == 0;
        if (strncmp(argv[i], "-DFIXTURE_COUNT_PATH=", sizeof("-DFIXTURE_COUNT_PATH=") - 1) == 0)
        {
            count_path = argv[i] + sizeof("-DFIXTURE_COUNT_PATH=") - 1;
        }
        if (strncmp(argv[i], "-DFIXTURE_MUTATE_PATH=", sizeof("-DFIXTURE_MUTATE_PATH=") - 1) == 0)
        {
            mutate_path = argv[i] + sizeof("-DFIXTURE_MUTATE_PATH=") - 1;
        }
        size_t length = strlen(argv[i]);
        if (length >= 2 && strcmp(argv[i] + length - 2, ".c") == 0) source = argv[i];
        if (dependency_scan)
        {
            continue;
        }
        if (strcmp(argv[i], "-DFIXTURE_WARNING") == 0)
        {
            fputs("fixture.c:1:1: warning: analyzer negative control\n", stderr);
        }
        else if (strcmp(argv[i], "-DFIXTURE_STDOUT") == 0)
        {
            fputs("fixture.c:1:1: warning: stdout negative control\n", stdout);
        }
        else if (strcmp(argv[i], "-DFIXTURE_FAILURE") == 0)
        {
            result = 1;
        }
        else if (strcmp(argv[i], "-DFIXTURE_CRASH") == 0)
        {
            raise(SIGTERM);
        }
        else if (strcmp(argv[i], "-DFIXTURE_TIMEOUT") == 0)
        {
#if defined(_WIN32)
            Sleep(30000);
#else
            sleep(30);
#endif
        }
        else if (strcmp(argv[i], "-DFIXTURE_LARGE_OUTPUT") == 0)
        {
            for (int j = 0; j < 8192; j += 1)
            {
                fputs("stdout pipe draining negative control\n", stdout);
                fputs("stderr pipe draining negative control\n", stderr);
            }
        }
    }
    if (analyze && delay_analyze)
    {
#if defined(_WIN32)
        Sleep(100);
#else
        usleep(100000);
#endif
    }
    if (dependency_scan && source)
    {
        printf("buster_analyzer_snapshot: %s\n", source);
    }
    else if (analyze && count_path)
    {
        FILE* count = fopen(count_path, "a");
        if (count)
        {
            fputs("launch\n", count);
            fclose(count);
        }
        else
        {
            result = 2;
        }
    }
    if (analyze && mutate_path)
    {
        FILE* changed = fopen(mutate_path, "wb");
        if (changed)
        {
            if (fputs("created after analyzer preflight\n", changed) < 0) result = 2;
            if (fclose(changed) != 0) result = 2;
        }
        else
        {
            result = 2;
        }
    }
    return result;
}
