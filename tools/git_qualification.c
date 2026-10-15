#define _GNU_SOURCE
/* #3180: opt-in Linux Git qualification. main owns pristine intake and the
 * independent Makefile census; compiler_mode records each upstream CC action.
 * run_step owns process-group deadlines and logs. No production compiler edits.
 * This diagnostic harness does not qualify performance or the entire Git suite.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_GIT_PIN "c44beea485f0f2feaf460e2ac87fdd5608d63cf0"
#define BUSTER_GIT_ARENA (8u * 1024u * 1024u)
#define BUSTER_GIT_ARG_LIMIT 4096
#define BUSTER_GIT_BUILD_SECONDS 900
static char arena[BUSTER_GIT_ARENA];
static size_t used;
static const char* evidence;
static unsigned sequence;
static FILE* summary;
static const char* last_log;
extern char** environ;

static void fail(const char* message)
{
    fprintf(stderr, "GIT_QUALIFICATION_ERROR: %s errno=%d\n", message, errno);
    exit(2);
}

static char* format(const char* pattern, ...)
{
    va_list args;
    va_start(args, pattern);
    int n = vsnprintf(arena + used, sizeof(arena) - used, pattern, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(arena) - used) fail("arena exhausted");
    char* result = arena + used;
    used += (size_t)n + 1;
    return result;
}

static int quote_fits(size_t length, size_t available)
{
    return available >= 3 && length <= (available - 3) / 4;
}

static char* quote(const char* text)
{
    size_t n = strlen(text);
    if (!quote_fits(n, sizeof(arena) - used)) fail("quote bound");
    char* result = arena + used;
    char* out = result;
    *out++ = '\'';
    for (const char* p = text; *p; ++p)
    {
        if (*p == '\'')
        {
            memcpy(out, "'\\''", 4);
            out += 4;
        }
        else *out++ = *p;
    }
    *out++ = '\'';
    *out++ = 0;
    used += (size_t)(out - result);
    return result;
}

static char* read_text(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END)) fail("read log");
    long n = ftell(file);
    if (n < 0 || (size_t)n >= sizeof(arena) - used || fseek(file, 0, SEEK_SET)) fail("log bound");
    char* result = arena + used;
    if (fread(result, 1, (size_t)n, file) != (size_t)n || fclose(file)) fail("log read");
    result[n] = 0;
    used += (size_t)n + 1;
    return result;
}

static double seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) fail("clock");
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static int run_step(const char* name, const char* cwd, const char* command, unsigned limit)
{
    const char* path = format("%s/%03u-%s.log", evidence, sequence++, name);
    last_log = path;
    FILE* record = fopen(format("%s/commands.tsv", evidence), "a");
    if (!record) fail("command record");
    fprintf(record, "%s\t%s\t%s\t%s\n", name, cwd, command, path);
    if (fclose(record)) fail("command close");
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) fail("new step log");
    double begin = seconds();
    pid_t child = fork();
    if (child < 0) fail("fork");
    if (!child)
    {
        if (setpgid(0, 0) || chdir(cwd) || dup2(fd, 1) < 0 || dup2(fd, 2) < 0) _exit(125);
        close(fd);
        execl("/bin/sh", "sh", "-c", command, (char*)NULL);
        _exit(125);
    }
    close(fd);
    if (setpgid(child, child) && errno != EACCES && errno != ESRCH) fail("process group");
    int status = 0;
    int finished = 0;
    int timed_out = 0;
    while (!finished)
    {
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) finished = 1;
        else if (waited < 0 && errno != EINTR) fail("wait");
        else if (seconds() - begin > limit)
        {
            timed_out = 1;
            kill(-child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            finished = 1;
        }
        else
        {
            struct timespec pause = {0, 10000000};
            nanosleep(&pause, NULL);
        }
    }
    /* Children from upstream generators/maintenance must not outlive a step. */
    kill(-child, SIGKILL);
    int result = timed_out ? 124 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    fprintf(summary, "%s\t%d\t%.6f\t%s\n", name, result, seconds() - begin, path);
    fflush(summary);
    printf("GIT_STEP %s status=%d log=%s\n", name, result, path);
    fflush(stdout);
    return result;
}

static int host_link(int argc, char** argv)
{
    int source = 0;
    int object = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "-MF") || !strcmp(argv[i], "-MT") ||
            !strcmp(argv[i], "-MQ") || !strcmp(argv[i], "-I") || !strcmp(argv[i], "-isystem"))
        {
            if (i + 1 < argc) ++i;
        }
        else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "-E") || !strcmp(argv[i], "-S") ||
                 !strcmp(argv[i], "-x")) source = 1;
        else if (argv[i][0] != '-')
        {
            size_t n = strlen(argv[i]);
            if ((n > 2 && !strcmp(argv[i] + n - 2, ".o")) ||
                (n > 2 && !strcmp(argv[i] + n - 2, ".a"))) object = 1;
            else source = 1;
        }
    }
    return object && !source;
}

static int compiler_mode(int argc, char** argv)
{
    const char* candidate = getenv("BUSTER_GIT_COMPILER");
    const char* oracle = getenv("BUSTER_GIT_ORACLE");
    const char* directory = getenv("BUSTER_GIT_ACTIONS");
    const char* variant = getenv("BUSTER_GIT_VARIANT");
    if (!candidate || !oracle || !directory || !variant || argc >= BUSTER_GIT_ARG_LIMIT - 8) fail("CC environment");
    int link = host_link(argc, argv);
    int buster = !strcmp(variant, "buster") && !link;
    char* args[BUSTER_GIT_ARG_LIMIT];
    int count = 0;
    args[count++] = (char*)(buster ? candidate : oracle);
    if (buster)
    {
        args[count++] = "cc";
        args[count++] = "-target";
        args[count++] = "x86_64-linux";
        args[count++] = "-march=baseline";
    }
    for (int i = 1; i < argc; ++i) args[count++] = argv[i];
    args[count] = NULL;
    const char* path = format("%s/%ld.action", directory, (long)getpid());
    FILE* log = fopen(path, "wx");
    if (!log) fail("unique action record");
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) fail("cwd");
    fprintf(log, "variant=%s\nstage=%s\ncompiler=%s\ncwd=%s\nargv=", variant, link ? "host-link" : "compile-or-query", args[0], cwd);
    for (int i = 0; i < count; ++i) fprintf(log, "%s ", quote(args[i]));
    fputc('\n', log);
    fflush(log);
    pid_t pid = fork();
    if (pid < 0) fail("CC fork");
    if (!pid)
    {
        execv(args[0], args);
        _exit(125);
    }
    int status = 0;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    int result = waited < 0 ? 125 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    fprintf(log, "status=%d\n", result);
    if (fclose(log)) fail("CC record close");
    return result;
}

static void clean_environment(void)
{
    for (unsigned i = 0; environ[i];)
    {
        if (!strncmp(environ[i], "GIT_", 4))
        {
            const char* end = strchr(environ[i], '=');
            if (!end) fail("environment entry");
            const char* key = format("%.*s", (int)(end - environ[i]), environ[i]);
            if (unsetenv(key)) fail("unset Git environment");
            i = 0;
        }
        else ++i;
    }
    const char* names[] = {"MAKEFLAGS", "MFLAGS", "CC", "CFLAGS", "CPPFLAGS", "LDFLAGS", "LD_PRELOAD", "LD_LIBRARY_PATH"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) unsetenv(names[i]);
}

static void write_text(const char* path, const char* text)
{
    FILE* file = fopen(path, "wb");
    size_t n = strlen(text);
    if (!file || fwrite(text, 1, n, file) != n || fclose(file)) fail("fixture write");
}

static int pristine_status(const char* source, const char* phase)
{
    const char* command = format("test \"$(git rev-parse HEAD)\" = %s && test \"$(git rev-parse --show-toplevel)\" = %s && "
        "test -z \"$(git status --porcelain=v1 --untracked-files=all --ignored)\" && "
        "test -z \"$(git ls-files -v | sed '/^H /d')\" && git ls-tree -r HEAD", quote(BUSTER_GIT_PIN), quote(source));
    return run_step(phase, source, command, 30);
}

static void pristine(const char* source, const char* phase)
{
    if (pristine_status(source, phase)) fail("pin or pristine input");
}

static void intake_controls(const char* source)
{
    const char* copy = format("%s/input-controls", evidence);
    if (run_step("controls-clone", ".", format("git clone --no-local --no-hardlinks %s %s", quote(source), quote(copy)), 60))
        fail("control clone");
    pristine(copy, "control-accepted");
    const char* readme = format("%s/README.md", copy);
    const char* original = read_text(readme);
    write_text(readme, "tracked input changed\n");
    if (!pristine_status(copy, "control-tracked-dirty")) fail("dirty tracked input accepted");
    write_text(readme, original);
    const char* untracked = format("%s/buster-untracked-control", copy);
    write_text(untracked, "untracked input\n");
    if (!pristine_status(copy, "control-untracked")) fail("untracked input accepted");
    if (unlink(untracked)) fail("untracked control cleanup");
    write_text(format("%s/.git/info/exclude", copy), "buster-ignored-control\n");
    const char* ignored = format("%s/buster-ignored-control", copy);
    write_text(ignored, "ignored input\n");
    if (!pristine_status(copy, "control-ignored")) fail("ignored input accepted");
    if (unlink(ignored)) fail("ignored control cleanup");
    const char* flags[] = {"assume-unchanged", "skip-worktree"};
    for (unsigned i = 0; i < 2; ++i)
    {
        if (run_step("control-index-set", copy, format("git update-index --%s README.md", flags[i]), 30))
            fail("index control set");
        if (!pristine_status(copy, format("control-%s", flags[i]))) fail("hidden index flag accepted");
        if (run_step("control-index-clear", copy, format("git update-index --no-%s README.md", flags[i]), 30))
            fail("index control clear");
    }
    if (run_step("control-wrong-pin-set", copy, "git -c commit.gpgsign=false commit --allow-empty -m wrong-pin", 30))
        fail("wrong pin control setup");
    if (!pristine_status(copy, "control-wrong-pin")) fail("wrong revision accepted");
    if (run_step("control-wrong-pin-clear", copy, format("git reset --hard %s", quote(BUSTER_GIT_PIN)), 30))
        fail("wrong pin cleanup");
    pristine(copy, "control-restored");
    if (run_step("control-failure-status", ".", "exit 7", 10) != 7 ||
        run_step("control-timeout-status", ".", "sleep 5", 0) != 124) fail("process failure scored as success");
}

static const char* make_command(const char* self, const char* target)
{
    return format("make -k -j2 V=1 CC=%s CFLAGS=%s LDFLAGS=%s NO_CURL=YesPlease NO_EXPAT=YesPlease "
        "NO_OPENSSL=YesPlease NO_TCLTK=YesPlease NO_GETTEXT=YesPlease NO_PERL=YesPlease "
        "NO_PYTHON=YesPlease COMPUTE_HEADER_DEPENDENCIES=yes %s",
        quote(self), quote("-O0 -g -Wall -fPIE"), quote("-pie"), target);
}

static int git_step(const char* name, const char* binary, const char* repository, const char* args, int expected)
{
    const char* command = format("%s -c maintenance.auto=false -c gc.auto=0 -c core.autocrlf=false -C %s %s",
        quote(binary), quote(repository), args);
    int status = run_step(name, ".", command, 60);
    return status == expected;
}

static char* operation_witness(const char* variant, const char* work)
{
    const char* binary = format("%s/git", work);
    const char* root = format("%s/%s-operations", evidence, variant);
    const char* repo = format("%s/repository", root);
    const char* bare = format("%s/remote.git", root);
    const char* clone = format("%s/clone", root);
    if (mkdir(root, 0700) || mkdir(repo, 0700) || mkdir(bare, 0700)) fail("operation directories");
    if (setenv("GIT_EXEC_PATH", work, 1)) fail("Git exec path");
    int ok = git_step(format("%s-init", variant), binary, repo, "init --initial-branch=main", 0);
    if (ok)
    {
        write_text(format("%s/file", repo), "base\n");
        ok = git_step(format("%s-add", variant), binary, repo, "add file", 0) &&
             git_step(format("%s-commit-base", variant), binary, repo, "commit -m base", 0) &&
             git_step(format("%s-branch", variant), binary, repo, "checkout -b side", 0);
    }
    if (ok)
    {
        write_text(format("%s/file", repo), "side\n");
        ok = git_step(format("%s-commit-side", variant), binary, repo, "commit -am side", 0) &&
             git_step(format("%s-checkout-main", variant), binary, repo, "checkout main", 0);
    }
    if (ok)
    {
        write_text(format("%s/file", repo), "main\n");
        ok = git_step(format("%s-commit-main", variant), binary, repo, "commit -am main", 0) &&
             git_step(format("%s-merge-conflict", variant), binary, repo, "merge side", 1) &&
             git_step(format("%s-unmerged", variant), binary, repo, "ls-files -u", 0);
        if (ok)
        {
            const char* unmerged = read_text(last_log);
            unsigned lines = 0;
            for (const char* p = unmerged; *p; ++p) if (*p == '\n') ++lines;
            ok = lines == 3 && strstr(unmerged, " 1\tfile\n") &&
                 strstr(unmerged, " 2\tfile\n") && strstr(unmerged, " 3\tfile\n");
        }
    }
    if (ok)
    {
        write_text(format("%s/file", repo), "resolved\n");
        ok = git_step(format("%s-resolve", variant), binary, repo, "add file", 0) &&
             git_step(format("%s-commit-merge", variant), binary, repo, "commit -m resolve", 0) &&
             git_step(format("%s-new-topic", variant), binary, repo, "checkout -b topic", 0);
    }
    if (ok)
    {
        write_text(format("%s/topic", repo), "topic\n");
        ok = git_step(format("%s-topic-add", variant), binary, repo, "add topic", 0) &&
             git_step(format("%s-topic-commit", variant), binary, repo, "commit -m topic", 0) &&
             git_step(format("%s-main-again", variant), binary, repo, "checkout main", 0) &&
             git_step(format("%s-merge-success", variant), binary, repo, "merge --no-ff -m merge topic", 0) &&
             git_step(format("%s-init-bare", variant), binary, bare, "init --bare --initial-branch=main", 0) &&
             git_step(format("%s-remote", variant), binary, repo, format("remote add origin %s", quote(bare)), 0) &&
             git_step(format("%s-push", variant), binary, repo, "push -u origin main", 0) &&
             git_step(format("%s-clone-local", variant), binary, root,
                 format("clone --no-hardlinks %s %s", quote(bare), quote(clone)), 0);
    }
    if (ok)
    {
        write_text(format("%s/file", clone), "remote\n");
        ok = git_step(format("%s-clone-commit", variant), binary, clone, "commit -am remote", 0) &&
             git_step(format("%s-clone-push", variant), binary, clone, "push origin main", 0) &&
             git_step(format("%s-fetch", variant), binary, repo, "fetch origin", 0) &&
             git_step(format("%s-fast-forward", variant), binary, repo, "merge --ff-only origin/main", 0) &&
             !strcmp(read_text(format("%s/file", repo)), "remote\n") &&
             !strcmp(read_text(format("%s/topic", repo)), "topic\n") &&
             git_step(format("%s-repack", variant), binary, repo, "repack -ad", 0) &&
             git_step(format("%s-fsck", variant), binary, repo, "fsck --full --no-reflogs", 0) &&
             git_step(format("%s-missing-ref", variant), binary, repo, "rev-parse --verify refs/heads/absent", 128) &&
             git_step(format("%s-tree-witness", variant), binary, repo, "rev-parse HEAD 'HEAD^{tree}'", 0);
    }
    char* result = ok ? read_text(last_log) : NULL;
    fprintf(summary, "%s-operations\t%s\t0\tindependent-content-and-status-assertions\n", variant, ok ? "PASS" : "FAIL");
    fflush(summary);
    unsetenv("GIT_EXEC_PATH");
    return result;
}

static int campaign(const char* self, const char* source, const char* compiler, const char* oracle)
{
    clean_environment();
    if (setenv("LC_ALL", "C", 1) || setenv("TZ", "UTC", 1) ||
        setenv("GIT_CONFIG_NOSYSTEM", "1", 1) || setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1)) fail("fixed environment");
    if (setenv("GIT_AUTHOR_NAME", "Buster qualification", 1) ||
        setenv("GIT_AUTHOR_EMAIL", "qualification@example.invalid", 1) ||
        setenv("GIT_COMMITTER_NAME", "Buster qualification", 1) ||
        setenv("GIT_COMMITTER_EMAIL", "qualification@example.invalid", 1) ||
        setenv("GIT_AUTHOR_DATE", "2001-01-01T00:00:00+0000", 1) ||
        setenv("GIT_COMMITTER_DATE", "2001-01-01T00:00:00+0000", 1)) fail("identity");
    pristine(source, "pristine-before");
    intake_controls(source);
    if (run_step("identities", ".", format("git rev-parse HEAD 'HEAD^{tree}'; sha256sum %s %s %s; "
        "%s --version; make --version; ld --version; dpkg-query -W; find /usr/include -type f -exec sha256sum {} +",
        quote(self), quote(compiler), quote(oracle), quote(oracle)), 60)) fail("identity capture");
    int failed = 0;
    const char* variants[] = {"clang", "buster"};
    char* witnesses[2] = {NULL, NULL};
    for (unsigned v = 0; v < 2; ++v)
    {
        const char* variant = variants[v];
        const char* work = format("%s/%s", evidence, variant);
        const char* actions = format("%s/%s-actions", evidence, variant);
        if (mkdir(actions, 0700)) fail("action directory");
        if (run_step(format("%s-clone", variant), ".", format("git clone --no-hardlinks --no-local %s %s && "
            "git -C %s checkout --detach %s", quote(source), quote(work), quote(work), quote(BUSTER_GIT_PIN)), 60)) fail("isolated clone");
        if (setenv("BUSTER_GIT_COMPILER", compiler, 1) || setenv("BUSTER_GIT_ORACLE", oracle, 1) ||
            setenv("BUSTER_GIT_ACTIONS", actions, 1) || setenv("BUSTER_GIT_VARIANT", variant, 1)) fail("CC routing");
        int build = run_step(format("%s-build", variant), work, make_command(self, "all"), BUSTER_GIT_BUILD_SECONDS);
        if (build) failed = 1;
        /* Full build failures must not execute stale/partial application outputs. */
        if (!build)
        {
            int unit = run_step(format("%s-unit-tests", variant), work, "make -C t -k -j2 unit-tests", 300);
            int tests = run_step(format("%s-integration", variant), work,
                "make -C t -k -j2 t0000-basic.sh t0001-init.sh t1500-rev-parse.sh "
                "t2000-conflict-when-checking-files-out.sh t5601-clone.sh t5300-pack-object.sh", 300);
            if (unit || tests) failed = 1;
            witnesses[v] = operation_witness(variant, work);
            if (!witnesses[v]) failed = 1;
        }
        else
        {
            fprintf(summary, "%s-runtime\tNOT_RUN\t0\tfull-build-failed\n", variant);
            fflush(summary);
        }
        if (run_step(format("%s-inputs-after", variant), work, format("git diff --exit-code HEAD && "
            "git ls-tree -r HEAD && find . -path './.git' -prune -o -type f "
            "\\( -name '*.o' -o -name '*.a' -o -name '*.d' -o -name 'GIT-BUILD-OPTIONS' -o -name 'GIT-CFLAGS' \\) "
            "-exec sha256sum {} +"), 60)) failed = 1;
    }
    if (witnesses[0] && witnesses[1])
    {
        int equal = !strcmp(witnesses[0], witnesses[1]);
        fprintf(summary, "paired-operation-objects\t%s\t0\tcommit-and-tree-identities\n", equal ? "PASS" : "FAIL");
        if (!equal) failed = 1;
    }
    else fprintf(summary, "paired-operation-objects\tNOT_RUN\t0\tmissing-cell\n");
    pristine(source, "pristine-after");
    return failed;
}

int main(int argc, char** argv)
{
    int result;
    const char* actions = getenv("BUSTER_GIT_ACTIONS");
    if (actions) result = compiler_mode(argc, argv);
    else if (argc == 6 && !strcmp(argv[1], "--run"))
    {
        char self[4096], source[4096], compiler[4096], oracle[4096], output[4096];
        if (!realpath(argv[0], self) || !realpath(argv[2], source) || !realpath(argv[3], compiler) ||
            !realpath(argv[4], oracle) || !realpath(argv[5], output)) fail("absolute existing paths");
        evidence = output;
        summary = fopen(format("%s/summary.tsv", evidence), "wx");
        if (!summary) fail("evidence directory must be fresh");
        fputs("stage\tstatus\tdiagnostic_wall_seconds\tlog\n", summary);
        result = campaign(self, source, compiler, oracle);
        if (fclose(summary)) fail("summary close");
    }
    else if (argc == 2 && !strcmp(argv[1], "--self-test"))
    {
        char* compile[] = {"cc", "-o", "a.o", "-c", "a.c"};
        char* link[] = {"cc", "-o", "git", "a.o", "libgit.a", "-lz"};
        char* mixed[] = {"cc", "a.c", "a.o"};
        char* pre[] = {"cc", "-E", "-x", "c", "-"};
        if (host_link(5, compile) || !host_link(6, link) || host_link(3, mixed) || host_link(5, pre) ||
            strcmp(quote("a'b"), "'a'\\''b'") || quote_fits(0, 2) || !quote_fits(0, 3) ||
            quote_fits(SIZE_MAX, 16) || !quote_fits(3, 15)) fail("routing or quoting control");
        puts("GIT_QUALIFICATION_SELF_TEST_PASS");
        result = 0;
    }
    else
    {
        fputs("usage: git_qualification --run PRISTINE_GIT IDE CLANG FRESH_EVIDENCE_DIRECTORY\n", stderr);
        result = 2;
    }
    return result;
}
