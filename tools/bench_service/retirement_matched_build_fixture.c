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
#include <unistd.h>

int main(int argc, char** argv)
{
    int result = 1;
    if (argc >= 10 && !strcmp(argv[2], "--build-directory") && argv[3][0] == '/')
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
            /* One zero-exit candidate generate deliberately leaves the old
             * configured root intact to test the no-op/cache rejection. */
            bool stale_candidate = candidate && strstr(argv[3], "job-46-attempt-56") != NULL;
            result = !no_leak ? 6 : strstr(argv[3], "job-30-attempt-40") ? 5 :
                     stale_candidate || mkdir(argv[3], 0700) == 0 ? 0 : 1;
            puts(result ? "fixture generate failed" : "fixture generated");
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
