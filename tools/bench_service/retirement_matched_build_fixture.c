/* Isolated native-process fixture for the #1018 matched-build handoff.
 * It accepts the fixed generate/build argv, runs in the supplied source cwd,
 * and compiles a small executable into the same configured build path. This
 * test driver is not the trusted Clang production build driver. */
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

/* Stand-in for the systemd broker CLI (#1020): installed beside the fixture
 * driver as fixture-broker, it records each typed request in broker-launches
 * and runs the matching stage itself as the test user. It derives the
 * workspace, attempt, source cwd, build path and umask from its own location
 * and the typed fields, as the real broker derives them from constants; the
 * real broker instead starts a sandboxed stage unit as the stage user whose
 * only writable path is that service-created configured root. */
static int fixture_broker(int argc, char** argv)
{
    static char const* const stages[] = {"retirement-base-generate", "retirement-base-build",
        "retirement-candidate-generate", "retirement-candidate-build"};
    char root[512], record[1024], line[640];
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
    int result = ok && argc == 4 && !strcmp(argv[1], "signal") ? 0 : 126;
    if (ok && stage >= 0)
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
        if (driver_length > 0 && (size_t)driver_length < sizeof(driver) && build_length > 0 &&
            (size_t)build_length < sizeof(build) && source_length > 0 && (size_t)source_length < sizeof(source) &&
            chdir(source) == 0)
        {
            umask(stage < 2 ? 0077 : 0007);
            execv(driver, stage & 1 ? compile : generate);
        }
        result = 127;
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
            result = !no_leak ? 6 : strstr(argv[3], "job-30-attempt-40") ? 5 : root ? 0 : 1;
            puts(result ? "fixture generate failed" : "fixture generated");
            /* Jobs 63 and 64 hang in generate after recording their pid, for
             * the unit's cancellation and deadline cleanup regressions. */
            if (!result && (strstr(argv[3], "job-63-attempt-") || strstr(argv[3], "job-64-attempt-")))
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
            if (!result && strstr(argv[3], "job-39-attempt-49"))
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
            if (ok)
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
