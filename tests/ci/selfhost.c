// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Exercise the real orchestrator with subprocess stand-ins, including successful-but-missing outputs.
// The mock protocol exists only in this test executable, never in the production gate.
#define main selfhost_main
#include "../../tools/ci/selfhost.c"
#undef main
#include <signal.h>
#include <sys/prctl.h>

static void mock_timeout_descendant(bool ignore_parent)
{
    struct sigaction action = {0};
    action.sa_handler = ignore_parent ? SIG_IGN : SIG_DFL;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) != 0)
    {
        fail("cannot set mock parent signal disposition");
    }
    // Backstops are independent of the gate, including when a broken gate abandons us.
    alarm(30);
    int ready[2];
    if (pipe(ready) != 0)
    {
        fail("cannot create descendant readiness pipe");
    }
    pid_t child = fork();
    if (child < 0)
    {
        fail("cannot fork mock descendant");
    }
    if (child == 0)
    {
        close(ready[0]);
        action.sa_handler = SIG_IGN;
        if (sigaction(SIGTERM, &action, NULL) != 0)
        {
            _exit(99);
        }
        alarm(30);
        FILE *file = open_file("descendant.pid", "wx");
        fprintf(file, "%ld\n", (long)getpid());
        close_file(file);
        if (write(ready[1], "R", 1) != 1)
        {
            _exit(99);
        }
        close(ready[1]);
    }
    else
    {
        close(ready[1]);
        char byte = 0;
        ssize_t count;
        do
        {
            count = read(ready[0], &byte, 1);
        } while (count < 0 && errno == EINTR);
        close(ready[0]);
        if (count != 1 || byte != 'R')
        {
            fail("mock descendant did not become ready");
        }
    }
    for (;;)
    {
        pause();
    }
}

static bool has_argument(int argc, char **argv, const char *argument)
{
    bool found = false;
    for (int i = 1; i < argc; ++i)
    {
        found |= strcmp(argv[i], argument) == 0;
    }
    return found;
}

static void copy_executable(const char *destination)
{
    FILE *input = open_file("/proc/self/exe", "rb");
    FILE *output = open_file(destination, "wb");
    char buffer[16384];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0)
    {
        if (fwrite(buffer, 1, count, output) != count)
        {
            fail("mock copy failed");
        }
    }
    close_file(input);
    close_file(output);
    if (chmod(destination, 0700) != 0)
    {
        fail("mock chmod failed");
    }
}

static int mock(int argc, char **argv)
{
    const char *fault = getenv("BUSTER_SELFHOST_TEST_FAULT");
    if (!fault)
    {
        fault = "";
    }
    bool stage2 = strstr(argv[0], "/compiler") != NULL;
    int result = 0;
    if (strstr(argv[0], ".exe"))
    {
        result = strcmp(fault, "run") == 0 && strstr(argv[0], ".stage2.") ? 17 : 0;
    }
    else if (argc > 1 && strncmp(argv[1], "test-", 5) == 0)
    {
        if (!stage2 || (strcmp(argv[1], "test-refinement") == 0 && (argc != 3 || strcmp(argv[2], "src") != 0)))
        {
            fail("smoke test did not use stage 2 or lost its argument");
        }
        result = strcmp(fault, "smoke") == 0 ? 17 : 0;
    }
    else
    {
        const char *output = NULL;
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (strcmp(argv[i], "-o") == 0)
            {
                output = argv[i + 1];
            }
        }
        if (!output)
        {
            fail("mock command has no output");
        }
        if (has_argument(argc, argv, "-no-pie"))
        {
            if (!has_argument(argc, argv, "-lm") || !has_argument(argc, argv, "-ldl") || !has_argument(argc, argv, "-lpthread") || argc != 9 || !strstr(argv[2], "unit-0000.o") || !strstr(argv[3], "unit-0001.o"))
            {
                fail("link lost libraries, object count, or source-list order");
            }
            if (strcmp(fault, "link") == 0)
            {
                result = 17;
            }
            else if (strcmp(fault, "missing-stage2") != 0)
            {
                copy_executable(output);
            }
        }
        else
        {
            if (argc < 4 || strcmp(argv[1], "--lanes") != 0 || strcmp(argv[2], "1") != 0 || !has_argument(argc, argv, "-DNOTE=\"two words\"") || !has_argument(argc, argv, "-fno-strict-aliasing") || has_argument(argc, argv, "-O3") || has_argument(argc, argv, "-Wall") || has_argument(argc, argv, "-Werror"))
            {
                fail("compiler argv lost quoting, bounded lanes, or the host semantics");
            }
            bool probe = strstr(argv[argc - 1], "/probe-") != NULL;
            bool object = has_argument(argc, argv, "-c");
            if (!probe && strcmp(fault, "compile") == 0)
            {
                result = 17;
            }
            else if (!probe && strcmp(fault, "signal") == 0)
            {
                raise(SIGTERM);
                result = 17;
            }
            else if (!probe && strcmp(fault, "timeout") == 0)
            {
                sleep(30);
                result = 17;
            }
            else if (!probe && strncmp(fault, "timeout-descendant-", 19) == 0)
            {
                mock_timeout_descendant(strcmp(fault, "timeout-descendant-parent-ignores") == 0);
            }
            else if ((!probe && strcmp(fault, "missing-object") == 0) || (probe && stage2 && strcmp(fault, "missing-probe") == 0) || (probe && stage2 && !object && strcmp(fault, "missing-exe") == 0))
            {
                // A zero exit without output must not inherit a previous stage's successful result.
            }
            else if (object)
            {
                FILE *file = open_file(output, "wb");
                if (strcmp(fault, "empty-object") != 0)
                {
                    fputs(probe && stage2 && strcmp(fault, "object-diff") == 0 ? "DIFFERENT\n" : "OBJECT\n", file);
                }
                close_file(file);
            }
            else
            {
                copy_executable(output);
                if (stage2 && strcmp(fault, "exe-diff") == 0)
                {
                    FILE *file = open_file(output, "ab");
                    fputc('X', file);
                    close_file(file);
                }
            }
        }
    }
    return result;
}

static int wait_child(pid_t child)
{
    int status = 0;
    pid_t waited;
    do
    {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (child <= 0 || waited != child)
    {
        fail("test child wait failed");
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static void make_directory(const char *path)
{
    if (mkdir(path, 0700) != 0)
    {
        fail("mkdir %s: %s", path, strerror(errno));
    }
}

static void write_text(const char *path, const char *text)
{
    FILE *file = open_file(path, "wb");
    fputs(text, file);
    close_file(file);
}

static void write_list(const char *path, const char *const *values, size_t count)
{
    FILE *file = open_file(path, "wb");
    for (size_t i = 0; i < count; ++i)
    {
        size_t length = strlen(values[i]) + 1;
        if (fwrite(values[i], 1, length, file) != length)
        {
            fail("cannot write test list");
        }
    }
    close_file(file);
}

static void verify_probes(const char *directory)
{
    for (unsigned i = 0; i < BUSTER_SELFHOST_PROBES; ++i)
    {
        char *source = format_path("%s/oracle-%u.c", directory, i);
        char *output = format_path("%s/oracle-%u", directory, i);
        write_probe(source, i);
        pid_t child = fork();
        if (child == 0)
        {
            execlp("cc", "cc", "-O0", "-fwrapv", "-funsigned-char", "-fno-strict-aliasing", source, "-o", output, (char *)NULL);
            _exit(127);
        }
        if (wait_child(child) != 0)
        {
            fail("probe %u does not compile with cc", i);
        }
        child = fork();
        if (child == 0)
        {
            execl(output, output, (char *)NULL);
            _exit(127);
        }
        if (wait_child(child) != 0)
        {
            fail("probe %u has an incorrect expected result", i);
        }
        free(source);
        free(output);
    }
}

static void verify_timeout_descendant(const char *directory)
{
    char *pid_path = format_path("%s/descendant.pid", directory);
    FILE *file = open_file(pid_path, "r");
    long recorded = 0;
    if (fscanf(file, "%ld", &recorded) != 1 || recorded <= 0 || (long)(pid_t)recorded != recorded)
    {
        fail("invalid mock descendant PID");
    }
    close_file(file);
    pid_t child = (pid_t)recorded;
    int status = 0;
    pid_t waited;
    uint64_t deadline = now_ns() + 1000000000ull;
    do
    {
        waited = waitpid(child, &status, WNOHANG);
        if (waited < 0 && errno == EINTR)
        {
            continue;
        }
        if (waited != 0)
        {
            break;
        }
        struct timespec pause = {0, (long)BUSTER_SELFHOST_POLL_NS};
        nanosleep(&pause, NULL);
    } while (now_ns() < deadline);
    bool survived = waited == 0;
    if (survived)
    {
        // waitpid established that this is our unreaped, adopted child: its PID cannot be reused.
        kill(child, SIGKILL);
        wait_child(child);
    }
    if (survived || waited != child || !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL)
    {
        fail("timeout left a live descendant or failed to kill it: %s", pid_path);
    }
    char *timings = format_path("%s/output with spaces/timings.tsv", directory);
    file = open_file(timings, "r");
    char row[512];
    bool rows = fgets(row, sizeof(row), file) != NULL && fgets(row, sizeof(row), file) != NULL;
    char *code = rows ? strrchr(row, '\t') : NULL;
    if (!code || strcmp(code, "\t124\n") != 0)
    {
        fail("descendant timeout did not retain status 124");
    }
    close_file(file);
    free(timings);
    free(pid_path);
}

int main(int argc, char **argv)
{
    int result = 0;
    if (argc > 1 && strcmp(argv[1], "--gate") == 0)
    {
        result = selfhost_main(argc - 1, argv + 1);
    }
    else if (getenv("BUSTER_SELFHOST_TEST_MOCK"))
    {
        result = mock(argc, argv);
    }
    else
    {
        char *executable = realpath(argv[0], NULL);
        const char *temporary = getenv("TMPDIR");
        char *root = format_path("%s/selfhost-tests.XXXXXX", temporary ? temporary : "/tmp");
        if (!executable || !mkdtemp(root))
        {
            fail("cannot create test workspace");
        }
        printf("Self-host harness test artifacts: %s\n", root);
        // Own and reap orphaned mock descendants without signalling arbitrary recorded PIDs.
        if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0)
        {
            fail("cannot become the test child subreaper");
        }
        verify_probes(root);
        char *bin = format_path("%s/bin with spaces", root);
        make_directory(bin);
        char *linker = format_path("%s/clang", bin);
        if (symlink(executable, linker) != 0)
        {
            fail("cannot install mock linker");
        }
        char *path = format_path("%s:%s", bin, getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
        const char *const cases[] =
        {
            "pass",
            "compile",
            "signal",
            "timeout",
            "timeout-descendant-parent-ignores",
            "timeout-descendant-parent-exits",
            "missing-object",
            "empty-object",
            "link",
            "missing-stage2",
            "smoke",
            "missing-probe",
            "missing-exe",
            "object-diff",
            "exe-diff",
            "run",
            "empty-list",
            "unterminated-list",
            "empty-argument",
            "duplicate-source",
            "missing-source",
            "existing-output",
            "inventory-drift",
            "inventory-comment",
            "bad-timeout",
        };
        const char *const flags[] =
        {
            "-march=native",
            "-fwrapv",
            "-funsigned-char",
            "-fno-strict-aliasing",
            "-fno-stack-protector",
            "-DNOTE=\"two words\"",
            "-Wall",
            "-Werror",
            "-O3",
        };
        for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
        {
            const char *fault = cases[index];
            char *directory = format_path("%s/%s", root, fault);
            make_directory(directory);
            char *log_path = format_path("%s/%s.log", root, fault);
            fflush(stdout);
            pid_t child = fork();
            if (child == 0)
            {
                int log = open(log_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
                if (log < 0 || dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0 || chdir(directory) != 0 || setenv("PATH", path, 1) != 0 || setenv("BUSTER_SELFHOST_TEST_MOCK", "1", 1) != 0 || setenv("BUSTER_SELFHOST_TEST_FAULT", fault, 1) != 0)
                {
                    _exit(99);
                }
                close(log);
                make_directory("tools");
                make_directory("tools/ci");
                FILE *inventory = open_file("tools/ci/correctness.tests", "w");
                size_t smoke_count = sizeof(selfhost_smoke) / sizeof(selfhost_smoke[0]);
                const char *inventory_prefix = strcmp(fault, "inventory-comment") == 0 ? "# " : "";
                for (size_t i = 0; i < smoke_count; ++i)
                {
                    fprintf(inventory, "%s\"$BUSTER_COMPILER\" %s%s\n", inventory_prefix, selfhost_smoke[i], strcmp(fault, "inventory-drift") == 0 ? "-old" : "");
                }
                close_file(inventory);
                write_text("first.c", "int first;\n");
                write_text("unit with spaces.c", "int second;\n");
                const char *sources[] = {"first.c", "unit with spaces.c"};
                if (strcmp(fault, "duplicate-source") == 0)
                {
                    sources[1] = sources[0];
                }
                if (strcmp(fault, "missing-source") == 0)
                {
                    sources[1] = "absent.c";
                }
                if (strcmp(fault, "empty-argument") == 0)
                {
                    sources[1] = "";
                }
                write_list("sources.argv", sources, 2);
                write_list("flags.argv", flags, sizeof(flags) / sizeof(flags[0]));
                if (strcmp(fault, "empty-list") == 0 || strcmp(fault, "unterminated-list") == 0)
                {
                    write_text("sources.argv", strcmp(fault, "empty-list") == 0 ? "" : "first.c");
                }
                if (strcmp(fault, "existing-output") == 0)
                {
                    make_directory("output with spaces");
                }
                char *arguments[] = {"selfhost", executable, "sources.argv", "flags.argv", "output with spaces", strncmp(fault, "timeout", 7) == 0 ? "1" : "10", NULL};
                if (strcmp(fault, "bad-timeout") == 0)
                {
                    arguments[5] = "0";
                }
                execl(executable, executable, "--gate", arguments[1], arguments[2], arguments[3], arguments[4], arguments[5], (char *)NULL);
                _exit(99);
            }
            int status = wait_child(child);
            if (strncmp(fault, "timeout-descendant-", 19) == 0)
            {
                verify_timeout_descendant(directory);
            }
            bool success = strcmp(fault, "pass") == 0;
            char *pass = format_path("%s/output with spaces/PASS", directory);
            struct stat information;
            bool marker = lstat(pass, &information) == 0;
            // A crashed harness must not count as a successful negative test: it must reject with its normal 1.
            if (status != (success ? 0 : 1) || marker != success)
            {
                fail("case %s: status %d, PASS marker %d; see %s", fault, status, marker, log_path);
            }
            if (success)
            {
                char *timings = format_path("%s/output with spaces/timings.tsv", directory);
                FILE *file = open_file(timings, "r");
                unsigned lines = 0;
                int byte;
                while ((byte = fgetc(file)) != EOF)
                {
                    lines += byte == '\n';
                }
                close_file(file);
                if (lines != 48)
                {
                    fail("success case skipped commands: %u timing rows, expected 48", lines);
                }
                free(timings);
            }
            printf("PASS harness case: %s\n", fault);
            free(directory);
            free(log_path);
            free(pass);
        }
        printf("PASS: %zu harness cases and %u independently compiled/executed probe fixtures\n", sizeof(cases) / sizeof(cases[0]), BUSTER_SELFHOST_PROBES);
        free(path);
        free(linker);
        free(bin);
        free(root);
        free(executable);
    }
    return result;
}
