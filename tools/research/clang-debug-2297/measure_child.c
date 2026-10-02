/* Tiny timer avoids Python's pre-exec RSS floor for very small compilers. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    int result;
    if (argc < 3)
    {
        result = 2;
    }
    else
    {
        struct timespec begin, end;
        struct rusage usage;
        int status;
        clock_gettime(CLOCK_MONOTONIC, &begin);
        pid_t child = fork();
        if (child < 0)
        {
            result = 3;
        }
        else
        {
            if (child == 0)
            {
                execvp(argv[2], argv + 2);
                _exit(127);
            }
            pid_t waited;
            do
            {
                waited = wait4(child, &status, 0, &usage);
            } while (waited < 0 && errno == EINTR);
            if (waited < 0)
            {
                result = 4;
            }
            else
            {
                clock_gettime(CLOCK_MONOTONIC, &end);
                FILE *stream = fopen(argv[1], "w");
                if (stream == NULL)
                {
                    result = 5;
                }
                else
                {
                    fprintf(stream,
                        "{\"wall_s\":%.9f,\"peak_rss_kib\":%ld,"
                        "\"user_s\":%.9f,\"system_s\":%.9f}\n",
                        (double)(end.tv_sec - begin.tv_sec) +
                        (double)(end.tv_nsec - begin.tv_nsec) * 1e-9,
                        usage.ru_maxrss,
                        (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec * 1e-6,
                        (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec * 1e-6);
                    int publication = fclose(stream);
                    result = publication == 0 ?
                        (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)) : 6;
                }
            }
        }
    }
    return result;
}
