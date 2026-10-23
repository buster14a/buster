/* Original measurement helper, Apache-2.0 WITH LLVM-exception. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int result = 1;
    if (argc > 1)
    {
        pid_t child = fork();
        if (child == 0)
        {
            execvp(argv[1], argv + 1);
            perror("execvp");
            _exit(127);
        }
        else if (child > 0)
        {
            int status = 0;
            struct rusage usage = {0};
            pid_t waited = -1;
            do
            {
                waited = wait4(child, &status, 0, &usage);
            } while (waited < 0 && errno == EINTR);
            if (waited == child)
            {
                fprintf(stderr, "peak_rss_kib %ld\n", usage.ru_maxrss);
                fprintf(stderr, "user_seconds %.6f\n", usage.ru_utime.tv_sec + usage.ru_utime.tv_usec * 0.000001);
                fprintf(stderr, "system_seconds %.6f\n", usage.ru_stime.tv_sec + usage.ru_stime.tv_usec * 0.000001);
                result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
            else
            {
                perror("wait4");
            }
        }
        else
        {
            perror("fork");
        }
    }
    else
    {
        fprintf(stderr, "usage: tls85-measure COMMAND [ARGUMENT ...]\n");
    }
    return result;
}
