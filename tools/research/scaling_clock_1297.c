#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
/* Disposable Linux diagnostic launcher; not production or admission code. */
static volatile sig_atomic_t timed_out;
static volatile sig_atomic_t child_pid;
static void deadline(int signo) {
    (void)signo; timed_out = 1;
    if (child_pid > 0) { kill(-(pid_t)child_pid, SIGKILL); kill((pid_t)child_pid, SIGKILL); }
}
int main(int argc, char **argv) {
    if (argc < 3) return 125;
    struct sigaction action = {0}; action.sa_handler = deadline;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGALRM, &action, NULL)) return 125;
    struct timespec a, b; struct rusage usage = {0}; int status = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &a)) return 125;
    pid_t pid = fork();
    if (pid < 0) return 125;
    if (!pid) { if (setsid() < 0) _exit(126); execvp(argv[2], argv+2); _exit(127); }
    child_pid = pid; alarm(180);
    pid_t waited;
    do { waited = wait4(pid, &status, 0, &usage); } while (waited < 0 && errno == EINTR);
    alarm(0); child_pid = 0;
    if (waited != pid || clock_gettime(CLOCK_MONOTONIC, &b)) return 125;
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    FILE *f = fopen(argv[1], "wx"); if (!f) return 125;
    fprintf(f, "{\"wall_seconds\":%.9f,\"user_seconds\":%.6f,\"system_seconds\":%.6f,\"peak_rss_kib\":%ld,\"minor_faults\":%ld,\"major_faults\":%ld,\"exit_code\":%d,\"timeout\":%d}\n",
        (double)(b.tv_sec-a.tv_sec)+(double)(b.tv_nsec-a.tv_nsec)/1e9,
        (double)usage.ru_utime.tv_sec+(double)usage.ru_utime.tv_usec/1e6,
        (double)usage.ru_stime.tv_sec+(double)usage.ru_stime.tv_usec/1e6,
        usage.ru_maxrss, usage.ru_minflt, usage.ru_majflt, code, (int)timed_out);
    if (fclose(f)) return 125;
    return timed_out ? 124 : code;
}
