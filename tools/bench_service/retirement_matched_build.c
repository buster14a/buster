/* Private #1018 build sequence: begin imports A; stage describes one fixed
 * invocation; launch/poll observe that child's wait and drain a bounded pipe
 * into a fresh service-owned log. Complete stores that observed outcome, reimports
 * A, holds a fresh configured root from generate through its matching build,
 * and freezes each successful output. A failed stage poisons the sequence.
 * The worker still owns isolation and cancellation.
 */
#include "retirement_matched_build.h"
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#if defined(__linux__)
#include <linux/close_range.h>
#include <sys/syscall.h>
#endif

#define BQ_RETIREMENT_BUILD_LOG_CAP (16u * 1024u * 1024u)
#define BQ_RETIREMENT_BUILD_DRIVER_CAP (64u * 1024u * 1024u)
#define BQ_RETIREMENT_BUILD_BINARY_CAP (512u * 1024u * 1024u)
#define BQ_RETIREMENT_BUILD_DRIVER "/usr/local/libexec/buster-bench-build"

BUSTER_GLOBAL_LOCAL bool bq_retirement_build_path(char* output, size_t capacity,
    char const* parent, char const* suffix)
{
    int length = snprintf(output, capacity, "%s/%s", parent, suffix);
    bool ok = length > 0 && (size_t)length < capacity;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_build_driver_fd_sha(int file,
    char digest[SHA256_HEX_CAPACITY])
{
    struct stat before = {0}, after = {0};
    int descriptor_flags = file >= 3 ? fcntl(file, F_GETFD) : -1;
    int access_flags = descriptor_flags >= 0 ? fcntl(file, F_GETFL) : -1;
    bool observed = descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) &&
        access_flags >= 0 && (access_flags & O_ACCMODE) == O_RDONLY &&
        fstat(file, &before) == 0;
    bool owner_safe = observed && (before.st_uid == geteuid() ? !(before.st_mode & 0222) :
                      before.st_uid == 0 && !(before.st_mode & 0022));
    bool ok = observed && S_ISREG(before.st_mode) && before.st_nlink == 1 && owner_safe &&
              (before.st_mode & S_IXUSR) && before.st_size > 0 &&
              (u64)before.st_size <= BQ_RETIREMENT_BUILD_DRIVER_CAP;
    Sha256 hash;
    if (ok) sha256_init(&hash);
    u8 buffer[16384];
    u64 offset = 0;
    while (ok && offset < (u64)before.st_size)
    {
        size_t wanted = (u64)sizeof(buffer) < (u64)before.st_size - offset ?
                        sizeof(buffer) : (size_t)((u64)before.st_size - offset);
        ssize_t count = pread(file, buffer, wanted, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok)
        {
            sha256_add(&hash, buffer, (u64)count);
            offset += (u64)count;
        }
    }
    ok = ok && fstat(file, &after) == 0 && before.st_dev == after.st_dev &&
         before.st_ino == after.st_ino && before.st_size == after.st_size &&
         before.st_mode == after.st_mode && before.st_nlink == after.st_nlink;
    if (ok) sha256_finish_hex(&hash, (char8*)digest);
    else digest[0] = 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_build_driver_sha(char const* path,
    char digest[SHA256_HEX_CAPACITY])
{
    int file = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    bool ok = bq_retirement_build_driver_fd_sha(file, digest);
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_begin_pinned(BqQueue* queue,
    BqJob const* job, int installed, int workspaces, String8 workspace_root, String8 profile,
    char const* fixed_driver, char const* fixed_toolchain,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    bool require_new, BqRetirementMatchedBuild* build)
{
    BqError result = build && fixed_driver ? BQ_OK : BQ_RECIPE_MISMATCH;
    if (build) *build = (BqRetirementMatchedBuild){.generated_root = -1};
    char expected[SHA256_HEX_CAPACITY] = {0}, observed[SHA256_HEX_CAPACITY] = {0};
    BqRetirementPreparation prepared = {0};
    if (result == BQ_OK)
        result = bq_retirement_profile_sha(profile, S8("build-driver-sha256="), expected) ?
                 bq_retirement_preparation_import_pinned(queue, job, installed, workspaces,
                     profile, preparation_sha256, &prepared) : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
    {
        struct stat current = {0}, supplied = {0};
        int root = bq_string_path(workspace_root, build->workspace) ?
                   bq_open_absolute_directory(workspace_root) : -1;
        result = root >= 0 && fstat(root, &supplied) == 0 && fstat(workspaces, &current) == 0 &&
                 supplied.st_dev == current.st_dev && supplied.st_ino == current.st_ino &&
                 bq_retirement_build_driver_sha(fixed_driver, observed) &&
                 !memcmp(expected, observed, SHA256_HEX_CAPACITY) ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
        if (root >= 0 && close(root) != 0 && result == BQ_OK) result = BQ_IO;
    }
    if (result == BQ_OK)
        result = bq_retirement_toolchain_verify(installed, profile, fixed_toolchain,
                                                &build->toolchain);
    if (result == BQ_OK)
    {
        char name[64];
        bool paths = bq_workspace_name(name, job->id, job->token) &&
            bq_retirement_build_path(build->attempt, sizeof(build->attempt), build->workspace, name) &&
            bq_retirement_build_path(build->build, sizeof(build->build), build->attempt, "matched-build") &&
            bq_retirement_build_path(build->source[0], sizeof(build->source[0]), build->attempt, "base/source") &&
            bq_retirement_build_path(build->source[1], sizeof(build->source[1]), build->attempt, "candidate/source") &&
            strlen(fixed_driver) < sizeof(build->driver);
        int attempt = paths ? openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        struct stat named = {0};
        bool state = attempt >= 0 && bq_owned_directory(attempt, true, false);
        if (state && require_new)
            state = fstatat(attempt, "trusted-build", &named, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
        result = state ? BQ_OK : BQ_WORKSPACE_MISMATCH;
        if (attempt >= 0 && close(attempt) != 0 && result == BQ_OK) result = BQ_IO;
        if (result == BQ_OK)
        {
            memcpy(build->driver, fixed_driver, strlen(fixed_driver) + 1);
            memcpy(build->preparation_sha256, preparation_sha256, SHA256_HEX_CAPACITY);
            memcpy(build->driver_sha256, observed, SHA256_HEX_CAPACITY);
            memcpy(build->prepared_source, prepared.subjects, sizeof(build->prepared_source));
        }
    }
    if (result != BQ_OK && build) *build = (BqRetirementMatchedBuild){.generated_root = -1};
    return result;
}

BqError bq_retirement_matched_build_begin(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY], BqRetirementMatchedBuild* build)
{
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    BqError result = bq_retirement_matched_build_begin_pinned(queue, job, installed, workspaces,
        workspace_root, profile, BQ_RETIREMENT_BUILD_DRIVER, BQ_RETIREMENT_TOOLCHAIN_ROOT,
        preparation_sha256, true, build);
    return result;
}

bool bq_retirement_matched_build_stage(BqRetirementMatchedBuild* build, BqRetirementBuildStage* stage)
{
    bool ok = build && stage && !build->failed && build->preparation_sha256[0] &&
              build->next < BQ_RETIREMENT_BUILD_STAGES;
    if (ok)
    {
        char current[SHA256_HEX_CAPACITY] = {0};
        ok = bq_retirement_toolchain_recheck(&build->toolchain) &&
             bq_retirement_build_driver_sha(build->driver, current) &&
             !memcmp(current, build->driver_sha256, SHA256_HEX_CAPACITY);
        if (!ok) build->failed = true;
    }
    if (stage) *stage = (BqRetirementBuildStage){0};
    if (ok)
    {
        bool generate = (build->next & 1u) == 0;
        stage->cwd = build->source[build->next / 2u];
        stage->file_umask = build->next < 2u ? 0077 : 0007;
        stage->env[0] = build->toolchain.path;
        stage->env[1] = "LC_ALL=C";
        stage->env[2] = "TZ=UTC";
        stage->env[3] = "HOME=/nonexistent";
        stage->argv[0] = build->driver;
        stage->argv[1] = generate ? "generate" : "build";
        stage->argv[2] = "--build-directory";
        stage->argv[3] = build->build;
        stage->argv[4] = "--config";
        stage->argv[5] = "Release";
        if (generate)
        {
            char const* flags[] = {"--cc", "clang", "--no-include-tests", "--no-developer-targets",
                "--no-check-optional-warnings", "--no-fuzz", "--no-sanitize", "--no-time-trace",
                "--no-instrument", "--no-lto"};
            for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(flags); i += 1)
                stage->argv[6 + i] = flags[i];
            stage->argc = 6 + BUSTER_ARRAY_LENGTH(flags);
        }
        else
        {
            stage->argv[6] = "-t";
            stage->argv[7] = "ide";
            stage->argv[8] = "--";
            stage->argv[9] = "-j1";
            stage->argc = 10;
        }
    }
    return ok;
}

bool bq_retirement_matched_build_release(BqRetirementMatchedBuild* build)
{
    bool ok = build != NULL;
    if (ok && build->generated_root >= 3)
        ok = close(build->generated_root) == 0;
    if (build)
    {
        build->generated_root = -1;
        build->generated_device = 0;
        build->generated_inode = 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_build_command_sha(BqRetirementBuildStage const* stage,
    char digest[SHA256_HEX_CAPACITY])
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, "BQ-MATCHED-BUILD-COMMAND-V2", sizeof("BQ-MATCHED-BUILD-COMMAND-V2"));
    for (u32 i = 0; i < stage->argc; i += 1)
        sha256_add(&hash, stage->argv[i], strlen(stage->argv[i]) + 1);
    sha256_add(&hash, stage->cwd, strlen(stage->cwd) + 1);
    for (u32 i = 0; i < 4; i += 1)
        sha256_add(&hash, stage->env[i], strlen(stage->env[i]) + 1);
    char file_umask[5];
    snprintf(file_umask, sizeof(file_umask), "%04o", (unsigned)stage->file_umask);
    sha256_add(&hash, file_umask, sizeof(file_umask));
    sha256_finish_hex(&hash, (char8*)digest);
}

/* Recheck the prepared source through the same descriptor that the child will
 * use as its cwd. The preparation record pins both bytes and inode closure. */
BUSTER_GLOBAL_LOCAL int bq_retirement_build_source_fd(BqRetirementMatchedBuild const* build)
{
    u32 side = build->next / 2u;
    int source = side < 2 ? bq_open_absolute_directory(string_from_pointer(build->source[side])) : -1;
    BqRetirementSource observed = {0};
    bool ok = source >= 3 && bq_owned_directory(source, false, true) &&
        bq_retirement_scan(source, ".source-manifest",
            string_from_pointer(build->prepared_source[side].commit), &observed, true) &&
        bq_retirement_same_source(&build->prepared_source[side], &observed) &&
        !memcmp(build->prepared_source[side].materialized_identity_sha256,
            observed.installed_identity_sha256, SHA256_HEX_CAPACITY);
    if (!ok)
    {
        if (source >= 0) close(source);
        source = -1;
    }
    return source;
}

/* A successful no-op build must not inherit an earlier executable from the
 * shared configured pathname. The worker owns isolation until the child and
 * all descendants have been reaped; this check precedes log and child creation. */
BUSTER_GLOBAL_LOCAL int bq_retirement_build_output_absent(BqRetirementMatchedBuild const* build,
    struct stat* observed)
{
    int root = bq_open_absolute_directory(string_from_pointer(build->build));
    bool ok = root >= 3 && fstat(root, observed) == 0 && S_ISDIR(observed->st_mode);
    int release = ok ? openat(root, "Release", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (ok && release < 0) ok = errno == ENOENT;
    if (release >= 0)
    {
        struct stat output = {0};
        ok = fstatat(release, "ide", &output, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
        if (close(release) != 0) ok = false;
    }
    if (!ok)
    {
        if (root >= 0) close(root);
        root = -1;
    }
    return root;
}

/* The child keeps verified executable and source descriptors through setup;
 * replacement of either pathname cannot select different bytes or a cwd. The
 * worker still owns the separate candidate UID and sandbox around this stage. */
BUSTER_GLOBAL_LOCAL void bq_retirement_build_exec_fd(int executable, int writer, int directory, int source,
    BqRetirementBuildStage const* stage)
{
#if defined(__linux__) && defined(SYS_close_range)
    bool ok = dup2(writer, STDOUT_FILENO) == STDOUT_FILENO &&
        dup2(writer, STDERR_FILENO) == STDERR_FILENO;
    if (writer >= 3) close(writer);
    if (directory >= 3) close(directory);
    if (ok) ok = fchdir(source) == 0;
    if (source >= 3) close(source);
    int input = ok ? open("/dev/null", O_RDONLY | O_CLOEXEC) : -1;
    if (ok) ok = input >= 3 && dup2(input, STDIN_FILENO) == STDIN_FILENO;
    if (input >= 0) close(input);
    sigset_t empty = {0};
    struct sigaction defaults = {.sa_handler = SIG_DFL};
    if (ok) ok = sigemptyset(&empty) == 0 && sigemptyset(&defaults.sa_mask) == 0 &&
                 sigaction(SIGTERM, &defaults, NULL) == 0 &&
                 sigaction(SIGINT, &defaults, NULL) == 0 &&
                 sigaction(SIGPIPE, &defaults, NULL) == 0 &&
                 sigaction(SIGCHLD, &defaults, NULL) == 0 &&
                 sigprocmask(SIG_SETMASK, &empty, NULL) == 0;
    if (ok) umask(stage->file_umask);
    if (ok) ok = syscall(SYS_close_range, 3u, ~0u, CLOSE_RANGE_CLOEXEC) == 0;
    if (ok) fexecve(executable, (char* const*)stage->argv, (char* const*)stage->env);
#else
    (void)executable;
    (void)writer;
    (void)directory;
    (void)source;
    (void)stage;
#endif
    _exit(126);
}

bool bq_retirement_matched_build_launch(BqRetirementMatchedBuild* build,
    BqRetirementBuildProcess* process)
{
    BqRetirementBuildStage stage = {0};
    struct stat directory_stat = {0}, log_stat = {0};
    bool fresh = process && !process->state && !process->process &&
        !process->directory && !process->writer && !process->reader && !process->build_root;
    bool ok = fresh && bq_retirement_matched_build_stage(build, &stage);
    char digest[SHA256_HEX_CAPACITY] = {0}, name[32] = {0};
    if (ok) bq_retirement_build_command_sha(&stage, digest);
    int length = ok ? snprintf(name, sizeof(name), "build-log-%u", build->next) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(name) && stage.argv[0][0] == '/';
    int executable = ok ? open(stage.argv[0], O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    char observed[SHA256_HEX_CAPACITY] = {0};
    ok = ok && bq_retirement_build_driver_fd_sha(executable, observed) &&
         !memcmp(observed, build->driver_sha256, SHA256_HEX_CAPACITY);
    int source = ok ? bq_retirement_build_source_fd(build) : -1;
    if (ok && source < 3) build->failed = true;
    ok = ok && source >= 3;
    struct stat build_stat = {0}, generated_stat = {0};
    int build_root = -1;
    if (ok && (build->next & 1u))
    {
        build_root = bq_retirement_build_output_absent(build, &build_stat);
        ok = build_root >= 3 && build->generated_root >= 3 &&
             fstat(build->generated_root, &generated_stat) == 0 &&
             (u64)generated_stat.st_dev == build->generated_device &&
             (u64)generated_stat.st_ino == build->generated_inode &&
             build_stat.st_dev == generated_stat.st_dev &&
             build_stat.st_ino == generated_stat.st_ino;
    }
    else if (ok)
    {
        errno = 0;
        build_root = bq_open_absolute_directory(string_from_pointer(build->build));
        int opening_error = errno;
        ok = build->generated_root == -1 &&
             (build_root >= 3 ? build->next == 2u && fstat(build_root, &build_stat) == 0 &&
                                 S_ISDIR(build_stat.st_mode) :
                                 build_root == -1 && opening_error == ENOENT);
    }
    int directory = ok ? bq_open_absolute_directory(string_from_pointer(build->attempt)) : -1;
    ok = ok && directory >= 3 && bq_owned_directory(directory, true, false) &&
         fstat(directory, &directory_stat) == 0;
    int capture[2] = {-1, -1};
    if (ok) ok = pipe(capture) == 0;
    int read_flags = ok ? fcntl(capture[0], F_GETFL) : -1;
    ok = ok && capture[0] >= 3 && capture[1] >= 3 && read_flags >= 0 &&
         fcntl(capture[0], F_SETFL, read_flags | O_NONBLOCK) == 0 &&
         fcntl(capture[0], F_SETFD, FD_CLOEXEC) == 0 &&
         fcntl(capture[1], F_SETFD, FD_CLOEXEC) == 0;
    int writer = ok ? openat(directory, name,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && writer >= 3 && fstat(writer, &log_stat) == 0 &&
         S_ISREG(log_stat.st_mode) && log_stat.st_uid == geteuid() &&
         log_stat.st_nlink == 1 && log_stat.st_size == 0;
    pid_t child = ok ? fork() : -1;
    if (child == 0)
    {
        close(capture[0]);
        close(writer);
        bq_retirement_build_exec_fd(executable, capture[1], directory, source, &stage);
    }
    if (capture[1] >= 0) close(capture[1]);
    if (executable >= 0) close(executable);
    if (source >= 0) close(source);
    ok = ok && child > 0;
    if (ok)
    {
        *process = (BqRetirementBuildProcess){0};
        process->directory = directory;
        process->writer = writer;
        process->reader = capture[0];
        process->build_root = build_root;
        process->process = child;
        process->directory_device = (u64)directory_stat.st_dev;
        process->directory_inode = (u64)directory_stat.st_ino;
        process->log_device = (u64)log_stat.st_dev;
        process->log_inode = (u64)log_stat.st_ino;
        process->build_device = (u64)build_stat.st_dev;
        process->build_inode = (u64)build_stat.st_ino;
        memcpy(process->name, name, (size_t)length + 1);
        memcpy(process->command_sha256, digest, sizeof(digest));
        process->stage = build->next;
        process->state = BQ_RETIREMENT_BUILD_RUNNING;
    }
    else
    {
        if (capture[0] >= 0) close(capture[0]);
        if (build_root >= 0) close(build_root);
        if (writer >= 0) close(writer);
        if (directory >= 0) close(directory);
        if (build)
        {
            build->failed = true;
            bq_retirement_matched_build_release(build);
        }
    }
    return ok;
}

/* Keep child stdout/stderr in a bounded service-owned file. Even a successful
 * exit cannot publish a stage after an overflow or a failed capture write. */
BUSTER_GLOBAL_LOCAL void bq_retirement_build_drain(BqRetirementBuildProcess* process)
{
    u8 bytes[16384];
    for (u32 i = 0; i < 8 && !process->log_eof; i += 1)
    {
        ssize_t count = read(process->reader, bytes, sizeof(bytes));
        if (count > 0)
        {
            u64 remaining = BQ_RETIREMENT_BUILD_LOG_CAP - process->log_bytes;
            u32 kept = (u64)count < remaining ? (u32)count : (u32)remaining;
            if (kept && !process->capture_failed &&
                !bq_write_all(process->writer, bytes, kept)) process->capture_failed = true;
            if (!process->capture_failed) process->log_bytes += kept;
            if ((u32)count > kept) process->log_overflow = true;
        }
        else if (count == 0) process->log_eof = true;
        else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        else if (errno != EINTR) { process->capture_failed = true; break; }
    }
}

int bq_retirement_matched_build_poll(BqRetirementBuildProcess* process)
{
    int result = -1;
    if (process && (process->state == BQ_RETIREMENT_BUILD_RUNNING ||
                    process->state == BQ_RETIREMENT_BUILD_DRAINING) && process->reader >= 3)
    {
        bq_retirement_build_drain(process);
        int status = 0;
        pid_t waited = process->state == BQ_RETIREMENT_BUILD_RUNNING && process->process > 0 ?
                       waitpid(process->process, &status, WNOHANG) : 0;
        if (waited == process->process && waited > 0)
        {
            process->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            process->state = BQ_RETIREMENT_BUILD_DRAINING;
            process->process = 0;
            bq_retirement_build_drain(process);
        }
        else if (waited < 0 && errno != EINTR)
        {
            process->state = BQ_RETIREMENT_BUILD_WAIT_FAILED;
            process->process = 0;
        }
        if (process->state == BQ_RETIREMENT_BUILD_DRAINING && process->capture_failed)
            process->state = BQ_RETIREMENT_BUILD_WAIT_FAILED;
        if (process->state == BQ_RETIREMENT_BUILD_DRAINING && process->log_eof)
        {
            process->state = BQ_RETIREMENT_BUILD_REAPED;
            result = process->exit_code == 0 && !process->log_overflow &&
                     !process->capture_failed ? 1 : -1;
        }
        else if (process->state != BQ_RETIREMENT_BUILD_WAIT_FAILED) result = 0;
    }
    return result;
}

void bq_retirement_matched_build_abort(BqRetirementBuildProcess* process)
{
    if (process && process->state != BQ_RETIREMENT_BUILD_RUNNING &&
        process->state != BQ_RETIREMENT_BUILD_DRAINING)
    {
        if (process->reader >= 3) close(process->reader);
        if (process->build_root >= 3) close(process->build_root);
        if (process->writer >= 3) close(process->writer);
        if (process->directory >= 3) close(process->directory);
        *process = (BqRetirementBuildProcess){0};
    }
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_build_log_sha(int file, char digest[SHA256_HEX_CAPACITY])
{
    struct stat before = {0}, after = {0};
    bool ok = file >= 0 && fstat(file, &before) == 0 && S_ISREG(before.st_mode) &&
              before.st_nlink == 1 && before.st_uid == geteuid() &&
              !(before.st_mode & 0222) && before.st_size >= 0 &&
              (u64)before.st_size <= BQ_RETIREMENT_BUILD_LOG_CAP;
    Sha256 hash;
    if (ok) sha256_init(&hash);
    u8 buffer[16384];
    u64 offset = 0;
    while (ok && offset < (u64)before.st_size)
    {
        size_t size = (u64)sizeof(buffer) < (u64)before.st_size - offset ?
                      sizeof(buffer) : (size_t)((u64)before.st_size - offset);
        ssize_t count = pread(file, buffer, size, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok)
        {
            sha256_add(&hash, buffer, (u64)count);
            offset += (u64)count;
        }
    }
    ok = ok && fstat(file, &after) == 0 && before.st_dev == after.st_dev &&
         before.st_ino == after.st_ino && before.st_size == after.st_size &&
         before.st_mode == after.st_mode && before.st_nlink == after.st_nlink;
    if (ok) sha256_finish_hex(&hash, (char8*)digest);
    else digest[0] = 0;
    return ok;
}

/* The stage runner's service-owned log becomes immutable queue evidence before
 * a failed stage is reported. Incomplete writes remain attributable but can
 * never satisfy the matching receipt or the eventual binary readback. */
BUSTER_GLOBAL_LOCAL int bq_retirement_build_stage_format(char body[1024],
    BqJob const* job, BqRetirementMatchedBuild const* build, u32 stage, int exit_code)
{
    int count = snprintf(body, 1024,
        "BQ-MATCHED-BUILD-STAGE-V2\njob=%" PRIu64 "\ntoken=%" PRIu64 "\nrequest=%.64s\n"
        "preparation=%.64s\ndriver=%.64s\ntoolchain=%.64s\ntoolchain-identity=%.64s\n"
        "stage=%u\ncommand=%.64s\nlog=%.64s\nexit=%d\n",
        (uint64_t)job->id, (uint64_t)job->token, job->digest,
        build->preparation_sha256, build->driver_sha256, build->toolchain.manifest_sha256,
        build->toolchain.identity_sha256, stage,
        build->command_sha256[stage], build->log_sha256[stage], exit_code);
    return count;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_build_stage_evidence(BqQueue* queue,
    BqJob const* job, int log, int exit_code, BqRetirementMatchedBuild* build)
{
    u32 stage = build->next;
    char prefix[32], name[48], receipt_name[48], body[1024];
    int count = snprintf(prefix, sizeof(prefix), "matched-log-%u", stage);
    bool ok = queue && count > 0 && (u32)count < sizeof(prefix) &&
              bq_record_name(name, prefix, job->id);
    int output = ok ? openat(queue->directory_fd, name,
                             O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400) : -1;
    ok = ok && output >= 0;
    struct stat before = {0}, after = {0};
    if (ok) ok = fstat(log, &before) == 0 && S_ISREG(before.st_mode) &&
                 before.st_nlink == 1 && before.st_uid == geteuid() &&
                 !(before.st_mode & 0222) && before.st_size >= 0 &&
                 (u64)before.st_size <= BQ_RETIREMENT_BUILD_LOG_CAP;
    Sha256 hash;
    if (ok) sha256_init(&hash);
    u8 bytes[16384];
    u64 offset = 0;
    while (ok && offset < (u64)before.st_size)
    {
        size_t wanted = (u64)sizeof(bytes) < (u64)before.st_size - offset ?
                        sizeof(bytes) : (size_t)((u64)before.st_size - offset);
        ssize_t read_count = pread(log, bytes, wanted, (off_t)offset);
        if (read_count < 0 && errno == EINTR) continue;
        ok = read_count > 0 && bq_write_all(output, bytes, (u32)read_count);
        if (ok)
        {
            sha256_add(&hash, bytes, (u64)read_count);
            offset += (u64)read_count;
        }
    }
    ok = ok && fstat(log, &after) == 0 && before.st_dev == after.st_dev &&
         before.st_ino == after.st_ino && before.st_size == after.st_size &&
         before.st_mode == after.st_mode && before.st_nlink == after.st_nlink &&
         fsync(output) == 0 && fsync(queue->directory_fd) == 0;
    if (output >= 0 && close(output) != 0) ok = false;
    char copied[SHA256_HEX_CAPACITY] = {0};
    if (ok) sha256_finish_hex(&hash, (char8*)copied);
    ok = ok && !memcmp(copied, build->log_sha256[stage], SHA256_HEX_CAPACITY);
    count = ok ? snprintf(prefix, sizeof(prefix), "matched-stage-%u", stage) : -1;
    ok = ok && count > 0 && (u32)count < sizeof(prefix) &&
         bq_record_name(receipt_name, prefix, job->id);
    count = ok ? bq_retirement_build_stage_format(body, job, build, stage, exit_code) : -1;
    BqError result = ok && count > 0 && (u32)count < sizeof(body) ?
        bq_record_write(queue, receipt_name, (u8 const*)body, (u32)count, false) : BQ_IO;
    if (result == BQ_OK)
        bq_digest(body, (u32)count, (char8*)build->stage_receipt_sha256[stage]);
    return result;
}

/* Retain the generated configured directory until its matching build starts.
 * A candidate generate must replace the prior build root rather than leave a
 * successful no-op over the baseline object cache. The old descriptor stops
 * its inode from being reused while the new root is compared. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_build_generated_root(BqRetirementMatchedBuild* build,
    BqRetirementBuildProcess const* process, uid_t candidate_uid)
{
    int root = bq_open_absolute_directory(string_from_pointer(build->build));
    struct stat generated = {0};
    bool ok = build->generated_root == -1 && root >= 3 &&
              fstat(root, &generated) == 0 && S_ISDIR(generated.st_mode) &&
              generated.st_uid == (process->stage ? candidate_uid : geteuid()) &&
              (process->build_root < 3 || generated.st_dev != (dev_t)process->build_device ||
               generated.st_ino != (ino_t)process->build_inode);
    if (ok)
    {
        build->generated_root = root;
        build->generated_device = (u64)generated.st_dev;
        build->generated_inode = (u64)generated.st_ino;
    }
    else if (root >= 0 && close(root) != 0) ok = false;
    return ok;
}

/* The held file and Release directory must still be the configured output,
 * with stable bytes and metadata across the copy. The directory descriptors
 * keep replacement inodes alive, so a swapped name cannot reuse their inode. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_build_output_same(struct stat const* first,
    struct stat const* current)
{
    bool ok = first->st_dev == current->st_dev && first->st_ino == current->st_ino &&
              first->st_size == current->st_size && first->st_mode == current->st_mode &&
              first->st_nlink == current->st_nlink && first->st_uid == current->st_uid;
#if defined(__APPLE__)
    ok = ok && first->st_mtimespec.tv_sec == current->st_mtimespec.tv_sec &&
         first->st_mtimespec.tv_nsec == current->st_mtimespec.tv_nsec &&
         first->st_ctimespec.tv_sec == current->st_ctimespec.tv_sec &&
         first->st_ctimespec.tv_nsec == current->st_ctimespec.tv_nsec;
#else
    ok = ok && first->st_mtim.tv_sec == current->st_mtim.tv_sec &&
         first->st_mtim.tv_nsec == current->st_mtim.tv_nsec &&
         first->st_ctim.tv_sec == current->st_ctim.tv_sec &&
         first->st_ctim.tv_nsec == current->st_ctim.tv_nsec;
#endif
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_build_output_stable(int root, int release,
    int input, struct stat const* first)
{
    struct stat held_release = {0}, named_release = {0}, held = {0}, named = {0};
    bool ok = fstat(release, &held_release) == 0 && S_ISDIR(held_release.st_mode) &&
              fstatat(root, "Release", &named_release, AT_SYMLINK_NOFOLLOW) == 0 &&
              S_ISDIR(named_release.st_mode) &&
              held_release.st_dev == named_release.st_dev &&
              held_release.st_ino == named_release.st_ino &&
              fstat(input, &held) == 0 &&
              fstatat(release, "ide", &named, AT_SYMLINK_NOFOLLOW) == 0 &&
              S_ISREG(named.st_mode) &&
              bq_retirement_build_output_same(first, &held) &&
              bq_retirement_build_output_same(&held, &named);
    return ok;
}

/* Copy only the successful build's observed Release/ide. The next generate
 * may remove the shared build root only after the prior executable is frozen. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_build_freeze(BqRetirementMatchedBuild* build,
    int workspaces, BqJob const* job, u32 side, uid_t candidate_uid,
    BqRetirementBuildProcess const* process)
{
    char name[64];
    int attempt = bq_workspace_name(name, job->id, job->token) ?
                  openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int root = process->build_root;
    struct stat build_stat = {0};
    bool same_root = root >= 3 && fstat(root, &build_stat) == 0 &&
                     (u64)build_stat.st_dev == process->build_device &&
                     (u64)build_stat.st_ino == process->build_inode;
    int named_root = same_root ? bq_open_absolute_directory(string_from_pointer(build->build)) : -1;
    struct stat named_stat = {0};
    same_root = same_root && named_root >= 3 && fstat(named_root, &named_stat) == 0 &&
                (u64)named_stat.st_dev == process->build_device &&
                (u64)named_stat.st_ino == process->build_inode;
    if (named_root >= 0 && close(named_root) != 0) same_root = false;
    int release = same_root ? openat(root, "Release", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int input = release >= 0 ? openat(release, "ide", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat first = {0};
    bool ok = same_root && attempt >= 0 && bq_owned_directory(attempt, true, false) && input >= 0 &&
              fstat(input, &first) == 0 && S_ISREG(first.st_mode) && first.st_nlink == 1 &&
              first.st_uid == (side ? candidate_uid : geteuid()) &&
              (first.st_mode & S_IXUSR) && first.st_size > 0 &&
              (u64)first.st_size <= BQ_RETIREMENT_BUILD_BINARY_CAP &&
              bq_retirement_build_output_stable(root, release, input, &first);
    if (ok && !side) ok = mkdirat(attempt, "trusted-build", 0700) == 0;
    int frozen = ok ? openat(attempt, "trusted-build", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && frozen >= 0 && bq_owned_directory(frozen, true, false);
    int output = ok ? openat(frozen, side ? "candidate-ide" : "base-ide",
                             O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && output >= 0;
    u8 bytes[16384];
    u64 offset = 0;
    while (ok && offset < (u64)first.st_size)
    {
        size_t wanted = (u64)sizeof(bytes) < (u64)first.st_size - offset ?
                        sizeof(bytes) : (size_t)((u64)first.st_size - offset);
        ssize_t count = pread(input, bytes, wanted, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0 && bq_write_all(output, bytes, (u32)count);
        if (ok) offset += (u64)count;
    }
    ok = ok && fchmod(output, 0500) == 0 && fsync(output) == 0;
    if (output >= 0 && close(output) != 0) ok = false;
    if (ok) ok = fsync(frozen) == 0 && (!side || (fchmod(frozen, 0500) == 0 && fsync(frozen) == 0));
    int final_root = ok ? bq_open_absolute_directory(string_from_pointer(build->build)) : -1;
    if (ok) ok = final_root >= 3 && fstat(final_root, &named_stat) == 0 &&
                 (u64)named_stat.st_dev == process->build_device &&
                 (u64)named_stat.st_ino == process->build_inode &&
                 bq_retirement_build_output_stable(root, release, input, &first);
    if (final_root >= 0 && close(final_root) != 0) ok = false;
    if (frozen >= 0 && close(frozen) != 0) ok = false;
    if (input >= 0 && close(input) != 0) ok = false;
    if (release >= 0 && close(release) != 0) ok = false;
    if (attempt >= 0 && close(attempt) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL int bq_retirement_build_final_format(char body[2048],
    BqJob const* job, BqRetirementMatchedBuild const* build)
{
    int count = snprintf(body, 2048,
        "BQ-MATCHED-BUILDS-V2\njob=%" PRIu64 "\ntoken=%" PRIu64 "\nrequest=%.64s\n"
        "preparation=%.64s\ndriver=%.64s\ntoolchain=%.64s\ntoolchain-identity=%.64s\n"
        "binaries=%.64s\n"
        "stage-0=%.64s\nstage-1=%.64s\nstage-2=%.64s\nstage-3=%.64s\n",
        (uint64_t)job->id, (uint64_t)job->token, job->digest,
        build->preparation_sha256, build->driver_sha256, build->toolchain.manifest_sha256,
        build->toolchain.identity_sha256,
        build->binary_record_sha256, build->stage_receipt_sha256[0],
        build->stage_receipt_sha256[1], build->stage_receipt_sha256[2],
        build->stage_receipt_sha256[3]);
    return count;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_build_final_record(BqQueue* queue,
    BqJob const* job, BqRetirementMatchedBuild* build)
{
    char name[48], body[2048];
    int length = bq_retirement_build_final_format(body, job, build);
    BqError result = length > 0 && (u32)length < sizeof(body) &&
        bq_record_name(name, "matched-builds", job->id) ?
        bq_record_write(queue, name, (u8 const*)body, (u32)length, false) : BQ_IO;
    if (result == BQ_OK)
        bq_digest(body, (u32)length, (char8*)build->build_record_sha256);
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_build_stage_import(BqQueue* queue,
    BqJob const* job, BqRetirementMatchedBuild* build, u32 stage)
{
    build->next = stage;
    BqRetirementBuildStage command = {0};
    bool ok = bq_retirement_matched_build_stage(build, &command);
    if (ok) bq_retirement_build_command_sha(&command, build->command_sha256[stage]);
    char prefix[32], name[48];
    int count = ok ? snprintf(prefix, sizeof(prefix), "matched-log-%u", stage) : -1;
    ok = ok && count > 0 && (u32)count < sizeof(prefix) &&
         bq_record_name(name, prefix, job->id);
    int log = ok ? openat(queue->directory_fd, name,
                          O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && log >= 0 && bq_retirement_build_log_sha(log, build->log_sha256[stage]);
    if (log >= 0 && close(log) != 0) ok = false;
    char expected[1024], actual[1024];
    u32 size = 0;
    count = ok ? bq_retirement_build_stage_format(expected, job, build, stage, 0) : -1;
    int length = count;
    int prefix_length = ok ? snprintf(prefix, sizeof(prefix), "matched-stage-%u", stage) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(expected) &&
         prefix_length > 0 && (u32)prefix_length < sizeof(prefix) &&
         bq_record_name(name, prefix, job->id);
    BqError result = ok ? bq_record_read(queue, name, (u8*)actual, sizeof(actual), &size) : BQ_CORRUPT;
    if (result == BQ_OK)
        result = size == (u32)length && !memcmp(actual, expected, size) ? BQ_OK : BQ_CORRUPT;
    if (result == BQ_OK)
        bq_digest(actual, size, (char8*)build->stage_receipt_sha256[stage]);
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_import_pinned(BqQueue* queue,
    BqJob const* job, int installed, int workspaces, String8 workspace_root,
    String8 profile, char const* fixed_driver, char const* fixed_toolchain,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const binary_record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementMatchedBuild* verified)
{
    if (verified) *verified = (BqRetirementMatchedBuild){0};
    bool valid = verified && binary_record_sha256 && build_record_sha256 &&
        bq_retirement_hex(string_from_pointer(binary_record_sha256), 64) &&
        bq_retirement_hex(string_from_pointer(build_record_sha256), 64);
    BqRetirementMatchedBuild current = {0};
    BqError result = valid ? bq_retirement_matched_build_begin_pinned(queue, job, installed,
        workspaces, workspace_root, profile, fixed_driver, fixed_toolchain,
        preparation_sha256, false, &current) :
        BQ_RECIPE_MISMATCH;
    BqRetirementBinaries binaries = {0};
    if (result == BQ_OK)
        result = bq_retirement_binaries_import_pinned(queue, job, installed, workspaces,
            profile, preparation_sha256, binary_record_sha256, &binaries);
    if (result == BQ_OK)
        memcpy(current.binary_record_sha256, binary_record_sha256, SHA256_HEX_CAPACITY);
    for (u32 stage = 0; result == BQ_OK && stage < BQ_RETIREMENT_BUILD_STAGES; stage += 1)
        result = bq_retirement_build_stage_import(queue, job, &current, stage);
    if (result == BQ_OK)
    {
        char name[48], expected[2048], actual[2048], digest[SHA256_HEX_CAPACITY];
        u32 size = 0;
        int length = bq_retirement_build_final_format(expected, job, &current);
        result = length > 0 && (u32)length < sizeof(expected) &&
            bq_record_name(name, "matched-builds", job->id) ?
            bq_record_read(queue, name, (u8*)actual, sizeof(actual), &size) : BQ_CORRUPT;
        if (result == BQ_OK)
        {
            bq_digest(actual, size, (char8*)digest);
            result = size == (u32)length && !memcmp(expected, actual, size) &&
                !memcmp(digest, build_record_sha256, SHA256_HEX_CAPACITY) ? BQ_OK : BQ_CORRUPT;
        }
    }
    if (result == BQ_OK)
    {
        current.next = BQ_RETIREMENT_BUILD_STAGES;
        memcpy(current.build_record_sha256, build_record_sha256, SHA256_HEX_CAPACITY);
        *verified = current;
    }
    return result;
}

BqError bq_retirement_matched_build_import(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 workspace_root,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const binary_record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementMatchedBuild* verified)
{
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    BqError result = bq_retirement_matched_build_import_pinned(queue, job, installed,
        workspaces, workspace_root, profile, BQ_RETIREMENT_BUILD_DRIVER,
        BQ_RETIREMENT_TOOLCHAIN_ROOT, preparation_sha256,
        binary_record_sha256, build_record_sha256, verified);
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_complete_observed_pinned(BqQueue* queue,
    BqJob const* job, int installed, int workspaces, String8 profile, int stage_log,
    uid_t candidate_uid, BqRetirementBuildProcess const* process,
    BqRetirementMatchedBuild* build)
{
    BqRetirementBuildStage stage = {0};
    BqError result = bq_retirement_matched_build_stage(build, &stage) ? BQ_OK : BQ_RECIPE_MISMATCH;
    u32 current = result == BQ_OK ? build->next : 0;
    if (result == BQ_OK)
    {
        bq_retirement_build_command_sha(&stage, build->command_sha256[current]);
        result = bq_retirement_build_log_sha(stage_log, build->log_sha256[current]) ? BQ_OK : BQ_IO;
    }
    if (result == BQ_OK)
        result = bq_retirement_build_stage_evidence(queue, job, stage_log, process->exit_code, build);
    if (result == BQ_OK && process->exit_code != 0) result = BQ_WORKER_FAILED;
    if (result == BQ_OK && !bq_retirement_toolchain_recheck(&build->toolchain))
        result = BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK)
    {
        BqRetirementPreparation reread = {0};
        result = bq_retirement_preparation_import_pinned(queue, job, installed, workspaces,
                   profile, build->preparation_sha256, &reread);
    }
    if (result == BQ_OK && !(current & 1u))
        result = bq_retirement_build_generated_root(build, process, candidate_uid) ?
                 BQ_OK : BQ_SOURCE_MISMATCH;
    if (result == BQ_OK && (current & 1u))
        result = bq_retirement_build_freeze(build, workspaces, job, current / 2u,
                                            candidate_uid, process) ?
                 BQ_OK : BQ_SOURCE_MISMATCH;
    if (result == BQ_OK && current == BQ_RETIREMENT_BUILD_STAGES - 1u)
        result = bq_retirement_binaries_record_pinned(queue, job, installed, workspaces,
                   profile, build->preparation_sha256, build->binary_record_sha256);
    if (result == BQ_OK && current == BQ_RETIREMENT_BUILD_STAGES - 1u)
        result = bq_retirement_build_final_record(queue, job, build);
    if (result == BQ_OK) build->next += 1;
    else if (build) build->failed = true;
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_complete_pinned(BqQueue* queue,
    BqJob const* job, int installed, int workspaces, String8 profile,
    BqRetirementBuildProcess* process, uid_t candidate_uid, BqRetirementMatchedBuild* build)
{
    BqRetirementBuildStage stage = {0};
    struct stat directory_stat = {0}, named = {0}, writer_stat = {0}, reader_stat = {0};
    struct stat build_stat = {0};
    char command_sha256[SHA256_HEX_CAPACITY] = {0};
    bool ok = process && process->state == BQ_RETIREMENT_BUILD_REAPED &&
        !process->process && process->writer >= 3 && process->directory >= 3 &&
        process->log_eof && !process->log_overflow && !process->capture_failed &&
        build && !build->failed && process->stage == build->next &&
        bq_retirement_matched_build_stage(build, &stage);
    if (ok) ok = process->build_root == -1 || process->build_root >= 3;
    if (ok && (process->stage & 1u)) ok = process->build_root >= 3;
    if (ok && process->build_root >= 3)
        ok = fstat(process->build_root, &build_stat) == 0 &&
             (u64)build_stat.st_dev == process->build_device &&
             (u64)build_stat.st_ino == process->build_inode;
    if (ok && process->build_root == -1)
        ok = !(process->stage & 1u) && !process->build_device && !process->build_inode;
    if (ok) bq_retirement_build_command_sha(&stage, command_sha256);
    int writer_flags = ok ? fcntl(process->writer, F_GETFD) : -1;
    int access_flags = writer_flags >= 0 ? fcntl(process->writer, F_GETFL) : -1;
    int current = ok ? bq_open_absolute_directory(string_from_pointer(build->attempt)) : -1;
    struct stat reopened = {0};
    ok = ok && process->command_sha256[0] &&
        !memcmp(process->command_sha256, command_sha256, SHA256_HEX_CAPACITY) &&
        bq_owned_directory(process->directory, true, false) &&
        fstat(process->directory, &directory_stat) == 0 &&
        process->directory_device == (u64)directory_stat.st_dev &&
        process->directory_inode == (u64)directory_stat.st_ino &&
        current >= 3 && fstat(current, &reopened) == 0 &&
        reopened.st_dev == directory_stat.st_dev && reopened.st_ino == directory_stat.st_ino &&
        writer_flags >= 0 && (writer_flags & FD_CLOEXEC) &&
        access_flags >= 0 && (access_flags & O_ACCMODE) == O_WRONLY &&
        fstat(process->writer, &writer_stat) == 0 &&
        fstatat(process->directory, process->name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(writer_stat.st_mode) && writer_stat.st_nlink == 1 &&
        writer_stat.st_uid == geteuid() && writer_stat.st_size >= 0 &&
        (u64)writer_stat.st_size <= BQ_RETIREMENT_BUILD_LOG_CAP &&
        writer_stat.st_dev == named.st_dev && writer_stat.st_ino == named.st_ino &&
        process->log_device == (u64)writer_stat.st_dev &&
        process->log_inode == (u64)writer_stat.st_ino;
    if (current >= 0 && close(current) != 0) ok = false;
    if (ok) ok = fsync(process->writer) == 0 && fchmod(process->writer, 0400) == 0 &&
                 fsync(process->writer) == 0 && fsync(process->directory) == 0;
    if (process && process->state != BQ_RETIREMENT_BUILD_RUNNING && process->writer >= 3)
    {
        if (close(process->writer) != 0) ok = false;
        process->writer = -1;
    }
    int log = ok ? openat(process->directory, process->name,
        O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && log >= 3 && fstat(log, &reader_stat) == 0 &&
        fstatat(process->directory, process->name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(reader_stat.st_mode) && reader_stat.st_nlink == 1 &&
        reader_stat.st_uid == geteuid() && !(reader_stat.st_mode & 0222) &&
        process->log_device == (u64)reader_stat.st_dev &&
        process->log_inode == (u64)reader_stat.st_ino &&
        reader_stat.st_dev == named.st_dev && reader_stat.st_ino == named.st_ino &&
        reader_stat.st_size == named.st_size && reader_stat.st_mode == named.st_mode;
    BqError result = ok ? bq_retirement_matched_build_complete_observed_pinned(queue, job,
        installed, workspaces, profile, log, candidate_uid, process, build) : BQ_WORKER_FAILED;
    if (log >= 0 && close(log) != 0) result = BQ_IO;
    bool settled = process && process->state != BQ_RETIREMENT_BUILD_RUNNING &&
                   process->state != BQ_RETIREMENT_BUILD_DRAINING;
    if (settled && build && (result != BQ_OK || (process->stage & 1u)) &&
        !bq_retirement_matched_build_release(build)) result = BQ_IO;
    if (settled)
        bq_retirement_matched_build_abort(process);
    if (result != BQ_OK && build) build->failed = true;
    return result;
}

BqError bq_retirement_matched_build_complete(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, BqRetirementBuildProcess* process,
    BqRetirementMatchedBuild* build)
{
    String8 profile = job ? bq_recipe_profile(bq_request_recipe(&job->request)) : (String8){0};
    struct passwd* candidate = getpwnam("buster-bench-candidate");
    BqError result = candidate && candidate->pw_uid != geteuid() ?
        bq_retirement_matched_build_complete_pinned(queue, job, installed,
            workspaces, profile, process, candidate->pw_uid, build) :
        BQ_CONFIGURATION_MISMATCH;
    if (result != BQ_OK && build) build->failed = true;
    return result;
}
