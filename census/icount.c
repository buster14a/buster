// Count user-space instructions retired by a child command and its
// descendants. Prints "instructions=<n>" or "instructions=NA" to stderr and
// exits with the child's status. Linux only; measurement aid for census runs.
#define _GNU_SOURCE
#include <linux/perf_event.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    int status = 127;
    if (argc >= 2)
    {
        int go[2];
        if (pipe(go) == 0)
        {
            pid_t child = fork();
            if (child == 0)
            {
                char byte;
                close(go[1]);
                if (read(go[0], &byte, 1) != 1) _exit(126);
                execvp(argv[1], argv + 1);
                _exit(127);
            }
            close(go[0]);
            struct perf_event_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.type = PERF_TYPE_HARDWARE;
            attr.size = sizeof(attr);
            attr.config = PERF_COUNT_HW_INSTRUCTIONS;
            attr.disabled = 1;
            attr.inherit = 1;
            attr.exclude_kernel = 1;
            attr.exclude_hv = 1;
            attr.enable_on_exec = 1;
            int fd = (int)syscall(SYS_perf_event_open, &attr, child, -1, -1, 0);
            if (write(go[1], "x", 1) != 1) {}
            close(go[1]);
            int wait_status = 0;
            waitpid(child, &wait_status, 0);
            status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 128 + WTERMSIG(wait_status);
            uint64_t count = 0;
            if (fd >= 0 && read(fd, &count, sizeof(count)) == sizeof(count))
                fprintf(stderr, "instructions=%llu\n", (unsigned long long)count);
            else
                fprintf(stderr, "instructions=NA\n");
        }
    }
    return status;
}
