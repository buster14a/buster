/* Isolated native-process fixture for the #1018 matched-build handoff.
 * It accepts the fixed generate/build argv, runs in the supplied source cwd,
 * and compiles a small executable into the same configured build path. A
 * subject whose src/main.c carries the census-fixture marker instead freezes
 * the stand-in compiler the census fixture names (#1020 unit oracle, #881
 * worker-unit campaign: retirement_stand_in_compiler.h). This test driver is
 * not the trusted Clang production build driver. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "retirement_stand_in_compiler.h"

/* Stand-in for the systemd broker CLI (#1020): installed beside the fixture
 * driver as fixture-broker, it records each typed request in broker-launches.
 * start-stage behaves like the broker relaying systemd-run --wait: it forks
 * the matching stage as the test user (the "unit", whose pid it records in
 * <root>/<unit>.pid), waits for it, removes the record (--collect) and exits
 * with the stage's status. signal ... KILL SIGKILLs a recorded unit and exits
 * 0, or exits 126 when no unit is recorded, as the broker refuses a unit that
 * is not loaded. It derives the workspace, attempt, source cwd, build path
 * and umask from its own location and the typed fields, as the real broker
 * derives them from constants; the real broker instead starts a sandboxed
 * stage unit as the stage user whose only writable path is that
 * service-created configured root. Cleanup regressions select behaviour by
 * job: 65 creates its unit only after 300 ms, 66 ignores KILL while
 * reporting success, 68 relays BQ_RETIREMENT_STAGE_UNPROVEN_STATUS (125)
 * after its stage and 71 refuses the start with 126 before any unit. */
static int fixture_broker(int argc, char** argv)
{
    static char const* const stages[] = {"retirement-base-generate", "retirement-base-build",
        "retirement-candidate-generate", "retirement-candidate-build"};
    char root[512], record[1024], line[640], unit[768];
    char const* slash = strrchr(argv[0], '/');
    size_t length = slash ? (size_t)(slash - argv[0]) : 0;
    bool ok = length > 0 && length < sizeof(root);
    if (ok)
    {
        memcpy(root, argv[0], length);
        root[length] = 0;
    }
    int written = ok ? snprintf(record, sizeof(record), "%s/broker-launches", root) : -1;
    int line_length = !ok ? -1 : argc == 7 ? snprintf(line, sizeof(line), "%s %s %s %s %s %s\n", argv[1], argv[2],
                      argv[3], argv[4], argv[5], argv[6]) : snprintf(line, sizeof(line), "%s %s %s\n", argv[1],
                      argv[2], argv[3]);
    int log = written > 0 && (size_t)written < sizeof(record) && line_length > 0 &&
              (size_t)line_length < sizeof(line) ? open(record, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600) : -1;
    ok = log >= 0 && write(log, line, (size_t)line_length) == (ssize_t)line_length;
    if (log >= 0 && close(log) != 0) ok = false;
    int stage = -1;
    for (int index = 0; ok && argc == 7 && index < 4; index += 1)
        if (!strcmp(argv[4], stages[index])) stage = index;
    int unit_length = !ok ? -1 : argc == 7 ? snprintf(unit, sizeof(unit), "%s/buster-bench-%s-%s-%s.service.pid",
                      root, argv[2], argv[3], argv[4]) : snprintf(unit, sizeof(unit), "%s/%s.pid", root, argv[2]);
    ok = ok && unit_length > 0 && (size_t)unit_length < sizeof(unit) && (argc == 4 || !strchr(argv[4], '/'));
    int result = 126;
    if (ok && argc == 4 && !strcmp(argv[1], "signal") && !strchr(argv[2], '/'))
    {
        char text[32] = {0};
        int pid_file = open(unit, O_RDONLY | O_CLOEXEC);
        ssize_t count = pid_file >= 0 ? read(pid_file, text, sizeof(text) - 1) : -1;
        if (pid_file >= 0) close(pid_file);
        long pid = count > 0 ? strtol(text, NULL, 10) : 0;
        bool ignored = !strncmp(argv[2], "buster-bench-66-", 16);
        result = pid > 1 && (ignored || kill((pid_t)pid, SIGKILL) == 0) ? 0 : 126;
    }
    if (ok && stage >= 0 && strcmp(argv[2], "71"))
    {
        char driver[640], build[640], source[700];
        int driver_length = snprintf(driver, sizeof(driver), "%s/fixture-driver", root);
        int build_length = snprintf(build, sizeof(build), "%s/job-%s-attempt-%s/%s/matched-build", root, argv[2],
                                    argv[3], stage < 2 ? "base/build" : "candidate");
        int source_length = snprintf(source, sizeof(source), "%s/job-%s-attempt-%s/%s/source", root, argv[2],
                                     argv[3], stage < 2 ? "base" : "candidate");
        char* generate[] = {driver, "generate", "--build-directory", build, "--config", "Release", "--cc", "clang",
            "--no-include-tests", "--no-developer-targets", "--no-check-optional-warnings", "--no-fuzz",
            "--no-sanitize", "--no-time-trace", "--no-instrument", "--no-lto", NULL};
        char* compile[] = {driver, "build", "--build-directory", build, "--config", "Release", "-t", "ide", "--",
            "-j1", NULL};
        bool paths = driver_length > 0 && (size_t)driver_length < sizeof(driver) && build_length > 0 &&
                     (size_t)build_length < sizeof(build) && source_length > 0 && (size_t)source_length < sizeof(source);
        struct timespec late = {0, 300000000};
        if (paths && !strcmp(argv[2], "65")) nanosleep(&late, NULL);
        pid_t child = paths ? fork() : -1;
        if (child == 0)
        {
            if (chdir(source) == 0)
            {
                umask(stage < 2 ? 0077 : 0007);
                execv(driver, stage & 1 ? compile : generate);
            }
            _exit(127);
        }
        char pid_text[32];
        int pid_length = snprintf(pid_text, sizeof(pid_text), "%ld\n", (long)child);
        int pid_file = child > 0 ? open(unit, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600) : -1;
        bool recorded = pid_file >= 0 && write(pid_file, pid_text, (size_t)pid_length) == (ssize_t)pid_length;
        if (pid_file >= 0 && close(pid_file) != 0) recorded = false;
        int status = 0;
        pid_t waited = -1;
        do { if (child > 0) waited = waitpid(child, &status, 0); }
        while (waited < 0 && errno == EINTR);
        unlink(unit);
        result = !recorded || waited != child ? 127 : WIFEXITED(status) ? WEXITSTATUS(status) :
                 128 + WTERMSIG(status);
        if (!strcmp(argv[2], "68")) result = 125;
    }
    return result;
}

int main(int argc, char** argv)
{
    int result = 1;
    if ((argc == 7 && !strcmp(argv[1], "start-stage")) || (argc == 4 && !strcmp(argv[1], "signal")))
        result = fixture_broker(argc, argv);
    else if (argc >= 10 && !strcmp(argv[2], "--build-directory") && argv[3][0] == '/')
    {
        char cwd[512];
        sigset_t mask = {0};
        struct sigaction pipe_action = {0}, term_action = {0}, int_action = {0}, child_action = {0};
        mode_t file_umask = umask(0077);
        umask(file_umask);
        char* location = getcwd(cwd, sizeof(cwd));
        bool candidate = location && strstr(cwd, "/candidate/source");
        bool baseline = location && (strstr(cwd, "/base/source") || strstr(cwd, "/base/held-source"));
        bool policy = (baseline || candidate) &&
            (candidate ? file_umask == 0007 : file_umask == 0077) &&
            sigprocmask(SIG_SETMASK, NULL, &mask) == 0 &&
            sigismember(&mask, SIGTERM) == 0 && sigismember(&mask, SIGINT) == 0 &&
            sigaction(SIGPIPE, NULL, &pipe_action) == 0 && pipe_action.sa_handler == SIG_DFL &&
            sigaction(SIGTERM, NULL, &term_action) == 0 && term_action.sa_handler == SIG_DFL &&
            sigaction(SIGINT, NULL, &int_action) == 0 && int_action.sa_handler == SIG_DFL &&
            sigaction(SIGCHLD, NULL, &child_action) == 0 && child_action.sa_handler == SIG_DFL;
        if (!policy) result = 7;
        else if (!strcmp(argv[1], "generate") && argc == 16 &&
            !strcmp(argv[6], "--cc") && !strcmp(argv[7], "clang"))
        {
            /* Deliberate failing stage: the parent verifies that no binary
             * record or dependent timed child follows a failed generate. */
            bool probe = strstr(argv[3], "driver-exec-probe") != NULL;
            bool no_leak = !probe || (fcntl(90, F_GETFD) < 0 && errno == EBADF);
            /* Like the real driver in a broker stage, whose configured root
             * is a bind mount it cannot replace: fill the root in place, or
             * create it when a probe names a fresh path. */
            struct stat root_info = {0};
            bool root = mkdir(argv[3], 0700) == 0 ||
                        (errno == EEXIST && lstat(argv[3], &root_info) == 0 && S_ISDIR(root_info.st_mode));
            result = !no_leak ? 6 : strstr(argv[3], "job-30-attempt-40") || strstr(argv[3], "job-67-attempt-") ? 5 :
                     root ? 0 : 1;
            puts(result ? "fixture generate failed" : "fixture generated");
            /* Jobs 63 to 66 hang in generate after recording their pid, for
             * the unit's cancellation and deadline cleanup regressions. */
            if (!result && (strstr(argv[3], "job-63-attempt-") || strstr(argv[3], "job-64-attempt-") ||
                            strstr(argv[3], "job-65-attempt-") || strstr(argv[3], "job-66-attempt-")))
            {
                char pid_path[600];
                int pid_length = snprintf(pid_path, sizeof(pid_path), "%s/pid", argv[3]);
                FILE* pid_file = pid_length > 0 && (size_t)pid_length < sizeof(pid_path) ? fopen(pid_path, "w") : NULL;
                if (pid_file && (fprintf(pid_file, "%ld\n", (long)getpid()) < 0 || fclose(pid_file) != 0)) result = 1;
                if (!pid_file) result = 1;
                fflush(stdout);
                for (unsigned i = 0; !result && i < 600; i += 1)
                {
                    struct timespec pause = {0, 50000000};
                    nanosleep(&pause, NULL);
                }
            }
            /* Job 73 leaves a detached grandchild (setsid, then a double
             * fork) with no inherited descriptor and records its pid in
             * <workspaces>/job-73-escaped.pid; only a subreaper above the
             * stage sees it (the worker-unit producer's regression). */
            char const* attempt = !result && baseline ? strstr(argv[3], "/job-73-attempt-") : NULL;
            if (attempt)
            {
                char escaped_path[600];
                int escaped_length = snprintf(escaped_path, sizeof(escaped_path), "%.*s/job-73-escaped.pid",
                                              (int)(attempt - argv[3]), argv[3]);
                pid_t middle = escaped_length > 0 && (size_t)escaped_length < sizeof(escaped_path) ? fork() : -1;
                if (middle == 0)
                {
                    pid_t escaped = setsid() >= 0 ? fork() : -1;
                    if (escaped == 0)
                    {
                        for (int descriptor = 0; descriptor < 1024; descriptor += 1) close(descriptor);
                        int null = open("/dev/null", O_RDWR);
                        bool quiet = null == 0 && dup(null) == 1 && dup(null) == 2 && chdir("/") == 0;
                        for (unsigned i = 0; quiet && i < 1200; i += 1)
                        {
                            struct timespec pause = {0, 50000000};
                            nanosleep(&pause, NULL);
                        }
                        _exit(0);
                    }
                    FILE* escaped_file = escaped > 0 ? fopen(escaped_path, "w") : NULL;
                    bool recorded = escaped_file && fprintf(escaped_file, "%ld\n", (long)escaped) > 0;
                    if (escaped_file && fclose(escaped_file) != 0) recorded = false;
                    _exit(recorded ? 0 : 1);
                }
                int middle_status = 0;
                pid_t waited = -1;
                do { if (middle > 0) waited = waitpid(middle, &middle_status, 0); }
                while (waited < 0 && errno == EINTR);
                if (waited != middle || !WIFEXITED(middle_status) || WEXITSTATUS(middle_status)) result = 1;
            }
            if (!result && (strstr(argv[3], "job-39-attempt-49") || strstr(argv[3], "job-69-attempt-")))
            {
                char flood[8192];
                memset(flood, 'x', sizeof(flood));
                for (unsigned i = 0; i < 2049; i += 1)
                    if (fwrite(flood, 1, sizeof(flood), stdout) != sizeof(flood)) result = 1;
            }
        }
        else if (!strcmp(argv[1], "build") && argc == 10 &&
                 !strcmp(argv[6], "-t") && !strcmp(argv[7], "ide") &&
                 !strcmp(argv[8], "--") && !strcmp(argv[9], "-j1"))
        {
            char source[512], output[512], release[512];
            int source_length = snprintf(source, sizeof(source), "%s/input.c", argv[3]);
            int output_length = snprintf(output, sizeof(output), "%s/Release/ide", argv[3]);
            int release_length = snprintf(release, sizeof(release), "%s/Release", argv[3]);
            FILE* subject = fopen("src/main.c", "rb");
            char data[32] = {0};
            size_t count = subject ? fread(data, 1, sizeof(data) - 1, subject) : 0;
            if (subject) fclose(subject);
            bool ok = source_length > 0 && (size_t)source_length < sizeof(source) &&
                      output_length > 0 && (size_t)output_length < sizeof(output) &&
                      release_length > 0 && (size_t)release_length < sizeof(release) &&
                      count >= 5 && mkdir(release, 0700) == 0;
            int source_fd = ok ? open(source, O_WRONLY | O_CREAT | O_EXCL, 0600) : -1;
            char const* code = strstr(data, "int a;") ? "int main(void) { return 1; }\n" :
                                                        "int main(void) { return 2; }\n";
            ok = ok && source_fd >= 0 && write(source_fd, code, strlen(code)) == (ssize_t)strlen(code);
            if (source_fd >= 0 && close(source_fd) != 0) ok = false;
            /* The unit oracle fixture's census manifest names the stand-in
             * compilers (retirement_stand_in_compiler.h); a census-fixture
             * subject freezes exactly those bytes instead of compiling. */
            char const* census = !strstr(data, "census-fixture") ? NULL :
                                 baseline ? BQ_RETIREMENT_STAND_IN_BASE : BQ_RETIREMENT_STAND_IN_CANDIDATE;
            if (ok && census)
            {
                int output_fd = open(output, O_WRONLY | O_CREAT | O_EXCL, 0700);
                ok = output_fd >= 0 && write(output_fd, census, strlen(census)) == (ssize_t)strlen(census);
                if (output_fd >= 0 && close(output_fd) != 0) ok = false;
            }
            if (ok && !census)
            {
                pid_t child = fork();
                if (!child)
                {
                    /* The fixture host compiler finds its assembler/linker
                     * via this test-only fixed path. Production keeps the
                     * bundle-only PATH and must use pinned tool inputs. */
                    char* const compile[] = {"/usr/bin/cc", "-B/usr/bin/", "-std=c11", "-O2",
                                             source, "-o", output, NULL};
                    execv(compile[0], compile);
                    _exit(127);
                }
                int status = 0;
                pid_t waited = -1;
                do { if (child > 0) waited = waitpid(child, &status, 0); }
                while (waited < 0 && errno == EINTR);
                ok = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
            }
            result = ok ? 0 : 1;
            puts(result ? "fixture build failed" : "fixture compiled");
        }
    }
    return result;
}
