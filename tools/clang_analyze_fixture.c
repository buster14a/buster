// Native process oracle for ./build.sh clang_analyze --self-test. Never part of
// the application or its compile database. Modes exercise real child outcomes.
#include <stdio.h>
#include <string.h>
#include <signal.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

int main(int argc, char** argv)
{
    int result = 0;
    for (int i = 1; i < argc; i += 1)
    {
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
    return result;
}
