/* Private #1018 build sequence: begin imports A; stage describes one fixed
 * invocation; launch/poll observe that child's wait and drain a bounded pipe
 * into a fresh service-owned log. Complete stores that observed outcome, reimports
 * A, holds a fresh configured root from generate through its matching build,
 * and freezes each successful output. A failed stage poisons the sequence.
 * The worker still owns isolation and cancellation.
 *
 * Stores (#1020): the *_stores functions read A from
 * BqRetirementBuildStores.preparation and write logs, receipts, the binaries
 * record and the final record into .evidence; the queue API (*_pinned and the
 * public wrappers) passes the queue for both. retirement_unit.c passes its
 * sealed export and the attempt's retirement-build directory.
 * Launch seam: bq_retirement_build_exec_fd (DIRECT, fexecve of the held
 * driver in the held source cwd) or bq_retirement_build_exec_broker (BROKER,
 * fexecve of the fixed broker CLI with a typed start-stage request). Both
 * children lead their own process group. bq_retirement_matched_build_cancel
 * kills, reaps and proves a DIRECT group absent; for a BROKER stage,
 * bq_retirement_build_broker_stop sends the broker KILL and takes the CLI's
 * relayed exit as the absence proof (see there).
 * Layout: bq_retirement_build_root_create makes each subject's configured
 * root fresh before its generate (retirement_stage.h names the paths the
 * broker makes writable); logs and trusted-build/ live in the private
 * retirement-work directory that begin creates. A DIRECT driver must fill
 * the root in place, as a broker stage must (its root is a bind mount); a
 * driver that replaces the root fails the generate's identity check.
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
#define BQ_RETIREMENT_BUILD_DRIVER BQ_RETIREMENT_STAGE_DRIVER
/* Retry interval for a broker KILL that has not yet succeeded. */
#define BQ_RETIREMENT_BUILD_KILL_RETRY_NS (250ull * 1000000ull)

/* Typed broker stage names for the four matched-build stages; the broker's
 * fixed table and the credential gate take them from the same header. */
BUSTER_GLOBAL_LOCAL char const* const bq_retirement_broker_stages[BQ_RETIREMENT_BUILD_STAGES] = {
    BQ_RETIREMENT_STAGE_NAMES};

BUSTER_GLOBAL_LOCAL u64 bq_retirement_build_clock_ns(void)
{
    struct timespec now = {0};
    bool ok = clock_gettime(CLOCK_MONOTONIC, &now) == 0 && now.tv_sec >= 0 &&
              (u64)now.tv_sec <= (UINT64_MAX - (u64)now.tv_nsec) / 1000000000u;
    u64 result = ok ? (u64)now.tv_sec * 1000000000u + (u64)now.tv_nsec : 0;
    return result;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_build_pause(void)
{
    struct timespec pause = {0, 5000000};
    nanosleep(&pause, NULL);
}

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

/* broker is NULL for a DIRECT sequence, else the fixed broker executable;
 * broker_workspaces is then the workspace root that broker derives every
 * attempt path from (BQ_RETIREMENT_STAGE_WORKSPACE_ROOT in production). A
 * broker sequence requires workspace_root to be exactly that root, so the
 * cwd and --build-directory its command digest binds are the ones the
 * broker runs. A DIRECT sequence may use any (test) workspace root. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_begin_stores(BqRetirementBuildStores stores,
    BqJob const* job, int installed, int workspaces, String8 workspace_root, String8 profile,
    char const* fixed_driver, char const* fixed_toolchain, char const* broker, char const* broker_workspaces,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    bool require_new, BqRetirementMatchedBuild* build)
{
    BqError result = build && fixed_driver && (!broker || (broker[0] == '/' &&
                     strlen(broker) < sizeof(build->broker))) ? BQ_OK : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && broker && !(broker_workspaces &&
        string_equal(workspace_root, string_from_pointer(broker_workspaces))))
        result = BQ_WORKSPACE_MISMATCH;
    if (build) *build = (BqRetirementMatchedBuild){.generated_root = -1};
    char expected[SHA256_HEX_CAPACITY] = {0}, observed[SHA256_HEX_CAPACITY] = {0};
    BqRetirementPreparation prepared = {0};
    if (result == BQ_OK)
        result = bq_retirement_profile_sha(profile, S8("build-driver-sha256="), expected) ?
                 bq_retirement_preparation_import_pinned(stores.preparation, job, installed,
                     workspaces, profile, preparation_sha256, &prepared) : BQ_RECIPE_MISMATCH;
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
            bq_retirement_build_path(build->work, sizeof(build->work), build->attempt,
                                     BQ_RETIREMENT_BUILD_WORK_DIRECTORY) &&
            bq_retirement_build_path(build->build[0], sizeof(build->build[0]), build->attempt,
                                     BQ_RETIREMENT_STAGE_BASE_PARENT "/" BQ_RETIREMENT_STAGE_BUILD_LEAF) &&
            bq_retirement_build_path(build->build[1], sizeof(build->build[1]), build->attempt,
                                     BQ_RETIREMENT_STAGE_CANDIDATE_PARENT "/" BQ_RETIREMENT_STAGE_BUILD_LEAF) &&
            bq_retirement_build_path(build->source[0], sizeof(build->source[0]), build->attempt, "base/source") &&
            bq_retirement_build_path(build->source[1], sizeof(build->source[1]), build->attempt, "candidate/source") &&
            strlen(fixed_driver) < sizeof(build->driver);
        /* The attempt is the materializer's 02710 (#1018): service-owned and
         * writable by nobody else. A new sequence creates its own private
         * work directory, so a second sequence in the attempt is refused. */
        int attempt = paths ? openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        bool state = bq_retirement_attempt_directory(attempt);
        bool created = false;
        int work = !state ? -1 : require_new ?
                   bq_create_inherited_group_directory(attempt, BQ_RETIREMENT_BUILD_WORK_DIRECTORY, 02700, &created) :
                   openat(attempt, BQ_RETIREMENT_BUILD_WORK_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        struct stat work_info = {0};
        state = state && work >= 0 && created == require_new && fstat(work, &work_info) == 0 &&
                work_info.st_uid == geteuid() && bq_owned_directory(work, true, false) &&
                (!require_new || fsync(attempt) == 0);
        result = state ? BQ_OK : BQ_WORKSPACE_MISMATCH;
        if (work >= 0 && close(work) != 0 && result == BQ_OK) result = BQ_IO;
        if (attempt >= 0 && close(attempt) != 0 && result == BQ_OK) result = BQ_IO;
        if (result == BQ_OK)
        {
            memcpy(build->driver, fixed_driver, strlen(fixed_driver) + 1);
            memcpy(build->preparation_sha256, preparation_sha256, SHA256_HEX_CAPACITY);
            memcpy(build->driver_sha256, observed, SHA256_HEX_CAPACITY);
            memcpy(build->prepared_source, prepared.subjects, sizeof(build->prepared_source));
            build->job_id = job->id;
            build->attempt_token = job->token;
            build->launcher = broker ? BQ_RETIREMENT_LAUNCH_BROKER : BQ_RETIREMENT_LAUNCH_DIRECT;
            if (broker) memcpy(build->broker, broker, strlen(broker) + 1);
        }
    }
    if (result != BQ_OK && build) *build = (BqRetirementMatchedBuild){.generated_root = -1};
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_begin_pinned(BqQueue* queue,
    BqJob const* job, int installed, int workspaces, String8 workspace_root, String8 profile,
    char const* fixed_driver, char const* fixed_toolchain,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    bool require_new, BqRetirementMatchedBuild* build)
{
    BqError result = bq_retirement_matched_build_begin_stores(bq_retirement_build_queue_stores(queue), job,
        installed, workspaces, workspace_root, profile, fixed_driver, fixed_toolchain, NULL, NULL,
        preparation_sha256, require_new, build);
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
        stage->env[1] = "LC_ALL=" BQ_RETIREMENT_STAGE_LC_ALL_VALUE;
        stage->env[2] = "TZ=" BQ_RETIREMENT_STAGE_TZ_VALUE;
        stage->env[3] = "HOME=" BQ_RETIREMENT_STAGE_HOME_VALUE;
        if (build->launcher == BQ_RETIREMENT_LAUNCH_BROKER)
        {
            stage->broker = build->broker;
            stage->broker_stage = bq_retirement_broker_stages[build->next];
        }
        stage->argv[0] = build->driver;
        stage->argv[1] = generate ? "generate" : "build";
        stage->argv[2] = "--build-directory";
        stage->argv[3] = build->build[build->next / 2u];
        stage->argv[4] = "--config";
        stage->argv[5] = BQ_RETIREMENT_STAGE_CONFIG;
        static char const* const generate_flags[] = {BQ_RETIREMENT_STAGE_GENERATE_OPTIONS};
        static char const* const build_flags[] = {BQ_RETIREMENT_STAGE_BUILD_OPTIONS};
        u32 count = generate ? BUSTER_ARRAY_LENGTH(generate_flags) : BUSTER_ARRAY_LENGTH(build_flags);
        for (u32 i = 0; i < count; i += 1)
            stage->argv[6 + i] = generate ? generate_flags[i] : build_flags[i];
        stage->argc = 6 + count;
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
    /* A DIRECT digest is unchanged; a BROKER digest also names who launched. */
    if (stage->broker && stage->broker_stage)
    {
        sha256_add(&hash, "launcher=broker", sizeof("launcher=broker"));
        sha256_add(&hash, stage->broker, strlen(stage->broker) + 1);
        sha256_add(&hash, stage->broker_stage, strlen(stage->broker_stage) + 1);
    }
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
 * configured root. The worker owns isolation until the child and
 * all descendants have been reaped; this check precedes log and child creation. */
BUSTER_GLOBAL_LOCAL int bq_retirement_build_output_absent(BqRetirementMatchedBuild const* build,
    struct stat* observed)
{
    int root = bq_open_absolute_directory(string_from_pointer(build->build[build->next / 2u]));
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

/* Create this subject's configured root fresh and service-owned just before
 * its generate: base/build/matched-build (02700 beneath the private
 * base/build) or candidate/matched-build (02770, candidate group, beneath the
 * 02710 candidate/). The broker names exactly this root as the stage's only
 * ReadWritePaths entry, so the stage fills it but can neither replace it nor
 * reach the other subject's root. A pre-existing root is refused. */
BUSTER_GLOBAL_LOCAL int bq_retirement_build_root_create(BqRetirementMatchedBuild const* build, u32 side,
    struct stat* observed)
{
    char parent_path[BQ_RETIREMENT_BUILD_PATH_CAP];
    bool ok = side < 2 && bq_retirement_build_path(parent_path, sizeof(parent_path), build->attempt,
        side ? BQ_RETIREMENT_STAGE_CANDIDATE_PARENT : BQ_RETIREMENT_STAGE_BASE_PARENT);
    int parent = ok ? bq_open_absolute_directory(string_from_pointer(parent_path)) : -1;
    struct stat parent_info = {0};
    mode_t forbidden = side ? 022 : 077;
    ok = parent >= 3 && fstat(parent, &parent_info) == 0 && S_ISDIR(parent_info.st_mode) &&
         parent_info.st_uid == geteuid() && (parent_info.st_mode & forbidden) == 0;
    bool created = false;
    mode_t mode = side ? BQ_RETIREMENT_BUILD_CANDIDATE_ROOT_MODE : BQ_RETIREMENT_BUILD_BASE_ROOT_MODE;
    int root = ok ? bq_create_inherited_group_directory(parent, BQ_RETIREMENT_STAGE_BUILD_LEAF, mode, &created) : -1;
    ok = ok && root >= 3 && created && fstat(root, observed) == 0 && fsync(parent) == 0;
    if (parent >= 0 && close(parent) != 0) ok = false;
    if (!ok && root >= 0) close(root);
    return ok ? root : -1;
}

/* Shared child setup: its own process group, the bounded log pipe as
 * stdout/stderr, /dev/null stdin, the fixed signal and umask policy, and
 * close-on-exec for every inherited descriptor above stderr. A DIRECT child
 * enters the held source; a BROKER child starts at / because the broker fixes
 * each stage unit's working directory itself. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_build_child(int writer, int directory, int source, bool enter_source,
    mode_t file_umask)
{
    bool ok = false;
#if defined(__linux__) && defined(SYS_close_range)
    ok = setpgid(0, 0) == 0 && dup2(writer, STDOUT_FILENO) == STDOUT_FILENO &&
        dup2(writer, STDERR_FILENO) == STDERR_FILENO;
    if (writer >= 3) close(writer);
    if (directory >= 3) close(directory);
    if (ok) ok = enter_source ? fchdir(source) == 0 : chdir("/") == 0;
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
    if (ok) umask(file_umask);
    if (ok) ok = syscall(SYS_close_range, 3u, ~0u, CLOSE_RANGE_CLOEXEC) == 0;
#else
    (void)writer;
    (void)directory;
    (void)source;
    (void)enter_source;
    (void)file_umask;
#endif
    return ok;
}

/* fexecve is POSIX.1-2008 but undeclared on macOS. bq_retirement_build_child
 * already fails off Linux, so the portable build only needs the symbol gone. */
BUSTER_GLOBAL_LOCAL void bq_retirement_build_fexecve(int executable, char const* const* argv,
    char const* const* env)
{
#if defined(__linux__) && defined(SYS_close_range)
    fexecve(executable, (char* const*)argv, (char* const*)env);
#else
    (void)executable;
    (void)argv;
    (void)env;
#endif
}

/* The child keeps verified executable and source descriptors through setup;
 * replacement of either pathname cannot select different bytes or a cwd. The
 * worker still owns the separate candidate UID and sandbox around this stage. */
BUSTER_GLOBAL_LOCAL void bq_retirement_build_exec_fd(int executable, int writer, int directory, int source,
    BqRetirementBuildStage const* stage)
{
    if (bq_retirement_build_child(writer, directory, source, true, stage->file_umask))
        bq_retirement_build_fexecve(executable, stage->argv, stage->env);
    _exit(126);
}

/* BROKER: the child is only the fixed broker CLI. Its typed request carries
 * the job, attempt, stage name and the two A commits; the root broker
 * constructs the stage unit, its identity, umask, sandbox, cwd and argv from
 * constants and relays output and exit status through this pipe. */
BUSTER_GLOBAL_LOCAL void bq_retirement_build_exec_broker(int broker, int writer, int directory, int source,
    BqRetirementMatchedBuild const* build, BqRetirementBuildStage const* stage)
{
    char job[24], attempt[24];
    int job_length = snprintf(job, sizeof(job), "%" PRIu64, (uint64_t)build->job_id);
    int attempt_length = snprintf(attempt, sizeof(attempt), "%" PRIu64, (uint64_t)build->attempt_token);
    char const* argv[] = {build->broker, "start-stage", job, attempt, stage->broker_stage,
        build->prepared_source[0].commit, build->prepared_source[1].commit, NULL};
    char const* env[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL};
    bool ok = job_length > 0 && (size_t)job_length < sizeof(job) &&
              attempt_length > 0 && (size_t)attempt_length < sizeof(attempt) && stage->broker_stage &&
              bq_retirement_build_child(writer, directory, source, false, 0077);
    if (ok) bq_retirement_build_fexecve(broker, argv, env);
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
    /* The broker executable passes the same held-file checks as the driver. */
    bool brokered = ok && build->launcher == BQ_RETIREMENT_LAUNCH_BROKER;
    int broker = brokered ? open(build->broker, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    char broker_sha256[SHA256_HEX_CAPACITY] = {0};
    if (brokered) ok = bq_retirement_build_driver_fd_sha(broker, broker_sha256) && stage.broker_stage;
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
        build_root = build->generated_root == -1 ?
                     bq_retirement_build_root_create(build, build->next / 2u, &build_stat) : -1;
        ok = build_root >= 3;
    }
    int directory = ok ? bq_open_absolute_directory(string_from_pointer(build->work)) : -1;
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
        if (brokered) bq_retirement_build_exec_broker(broker, capture[1], directory, source, build, &stage);
        else bq_retirement_build_exec_fd(executable, capture[1], directory, source, &stage);
    }
    /* Both sides set the group so a signal cannot race the child's own call. */
    if (child > 0) setpgid(child, child);
    if (capture[1] >= 0) close(capture[1]);
    if (executable >= 0) close(executable);
    if (broker >= 0) close(broker);
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
        process->group = child;
        process->launcher = build->launcher;
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
            process->exited = WIFEXITED(status);
            process->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            process->state = BQ_RETIREMENT_BUILD_DRAINING;
            process->process = 0;
            bq_retirement_build_drain(process);
        }
        else if (waited < 0 && errno != EINTR)
        {
            /* The child's exit is unobserved, so nothing about it is proven. */
            process->state = BQ_RETIREMENT_BUILD_WAIT_FAILED;
            process->process = 0;
            process->exited = false;
            process->exit_code = -1;
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

/* Start the fixed broker's typed `signal <unit> KILL` for this sequence's
 * stage unit, from a held and verified descriptor, as its own process group
 * with output discarded. Returns the child for the caller to reap, or -1. */
BUSTER_GLOBAL_LOCAL pid_t bq_retirement_build_broker_kill(BqRetirementMatchedBuild const* build, u32 stage)
{
    char unit[160];
    int length = stage < BQ_RETIREMENT_BUILD_STAGES ? snprintf(unit, sizeof(unit),
        "buster-bench-%" PRIu64 "-%" PRIu64 "-%s.service", (uint64_t)build->job_id,
        (uint64_t)build->attempt_token, bq_retirement_broker_stages[stage]) : -1;
    int broker = length > 0 && (size_t)length < sizeof(unit) ?
                 open(build->broker, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    char digest[SHA256_HEX_CAPACITY] = {0};
    int null = broker >= 0 ? open("/dev/null", O_WRONLY | O_CLOEXEC) : -1;
    bool ok = null >= 3 && bq_retirement_build_driver_fd_sha(broker, digest);
    pid_t child = ok ? fork() : -1;
    if (child == 0)
    {
        char const* argv[] = {build->broker, "signal", unit, "KILL", NULL};
        char const* env[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL};
        if (bq_retirement_build_child(null, -1, -1, false, 0077)) bq_retirement_build_fexecve(broker, argv, env);
        _exit(126);
    }
    if (child > 0) setpgid(child, child);
    if (null >= 0) close(null);
    if (broker >= 0) close(broker);
    return child;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_build_kill_reap(pid_t child, int* status)
{
    kill(-child, SIGKILL);
    kill(child, SIGKILL);
    while (waitpid(child, status, 0) < 0 && errno == EINTR) {}
}

/* BROKER cleanup proof (#1020, #1785). The held child is only the broker
 * CLI: its process group says nothing about the stage unit, and a broker
 * `signal ... KILL` (systemctl kill) that succeeds is not absence, while one
 * that fails may only mean the unit is already collected or not yet created.
 * So the KILL is sent first, and repeated every BQ_RETIREMENT_BUILD_KILL_RETRY_NS
 * while it has not succeeded and the CLI is alive (a unit created after an
 * early KILL is still reached); its result is never proof. The CLI is then
 * reaped, with its log pipe drained, until deadline_ns. The CLI exits only
 * when the broker's status frame arrives or its connection breaks. Absence
 * is proven only by a normal CLI exit with a status other than
 * BQ_RETIREMENT_STAGE_UNPROVEN_STATUS: the relayed systemd-run --wait
 * result (PID1 reported the unit finished), a refusal before any unit was
 * started, or an abandoned relay after which the broker read the unit back
 * as gone. A CLI still running at the deadline is killed as a last resort
 * and nothing is proven; the KILL child is reaped on every path. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_build_broker_stop(BqRetirementMatchedBuild const* build,
    BqRetirementBuildProcess* process, u64 deadline_ns)
{
    pid_t signaller = build ? bq_retirement_build_broker_kill(build, process->stage) : -1;
    u64 retry_ns = bq_retirement_build_clock_ns() + BQ_RETIREMENT_BUILD_KILL_RETRY_NS;
    bool delivered = false;
    int status = 0;
    while (bq_retirement_build_clock_ns() < deadline_ns && (process->process > 0 || signaller > 0))
    {
        if (signaller > 0)
        {
            pid_t waited = waitpid(signaller, &status, WNOHANG);
            if (waited == signaller) delivered = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            if (waited == signaller || (waited < 0 && errno != EINTR)) signaller = -1;
        }
        if (process->process > 0)
        {
            pid_t waited = waitpid(process->process, &status, WNOHANG);
            if (waited == process->process)
            {
                process->exited = WIFEXITED(status);
                process->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
            else if (waited < 0 && errno != EINTR) process->exited = false;
            if (waited == process->process || (waited < 0 && errno != EINTR)) process->process = 0;
        }
        if (process->reader >= 3 && !process->log_eof) bq_retirement_build_drain(process);
        u64 now = bq_retirement_build_clock_ns();
        if (build && signaller <= 0 && !delivered && process->process > 0 && now >= retry_ns)
        {
            signaller = bq_retirement_build_broker_kill(build, process->stage);
            retry_ns = now + BQ_RETIREMENT_BUILD_KILL_RETRY_NS;
        }
        if (process->process > 0 || signaller > 0) bq_retirement_build_pause();
    }
    if (signaller > 0) bq_retirement_build_kill_reap(signaller, &status);
    bool proven = build && process->process == 0 && process->exited &&
                  process->exit_code != BQ_RETIREMENT_STAGE_UNPROVEN_STATUS;
    if (process->process > 0)
    {
        bq_retirement_build_kill_reap(process->process, &status);
        process->process = 0;
        process->exited = false;
    }
    return proven;
}

bool bq_retirement_matched_build_cancel(BqRetirementMatchedBuild const* build, BqRetirementBuildProcess* process,
    u64 deadline_ns)
{
    bool live = process && (process->state == BQ_RETIREMENT_BUILD_RUNNING ||
                            process->state == BQ_RETIREMENT_BUILD_DRAINING);
    bool brokered = process && process->launcher == BQ_RETIREMENT_LAUNCH_BROKER && process->state;
    bool ok = process != NULL;
    if (brokered)
        ok = bq_retirement_build_broker_stop(build, process, deadline_ns);
    else if (live)
    {
        /* DIRECT: the child is the stage itself and leads its own group. */
        if (process->group > 0) kill(-process->group, SIGKILL);
        if (process->process > 0) kill(process->process, SIGKILL);
        while (process->process > 0 && bq_retirement_build_clock_ns() < deadline_ns)
        {
            int status = 0;
            pid_t waited = waitpid(process->process, &status, WNOHANG);
            if (waited == process->process || (waited < 0 && errno != EINTR)) process->process = 0;
            else bq_retirement_build_pause();
        }
        ok = process->process == 0;
        bool absent = process->group <= 0;
        while (!absent && bq_retirement_build_clock_ns() < deadline_ns)
        {
            errno = 0;
            absent = kill(-process->group, 0) != 0 && errno == ESRCH;
            if (!absent) bq_retirement_build_pause();
        }
        ok = ok && absent;
    }
    /* A live stage is settled either way; an unproven cleanup is reported,
     * never retried here. A settled one keeps its observed outcome. */
    if (live)
    {
        process->state = BQ_RETIREMENT_BUILD_WAIT_FAILED;
        process->exit_code = -1;
    }
    return ok;
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

/* The stage runner's service-owned log becomes immutable evidence before
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

BUSTER_GLOBAL_LOCAL BqError bq_retirement_build_stage_evidence(BqRetirementStore evidence,
    BqJob const* job, int log, int exit_code, BqRetirementMatchedBuild* build)
{
    u32 stage = build->next;
    char prefix[32], name[48], receipt_name[48], body[1024];
    int count = snprintf(prefix, sizeof(prefix), "matched-log-%u", stage);
    bool ok = evidence.directory >= 0 && count > 0 && (u32)count < sizeof(prefix) &&
              bq_record_name(name, prefix, job->id);
    int output = ok ? openat(evidence.directory, name,
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
         fsync(output) == 0 && fsync(evidence.directory) == 0;
    if (output >= 0 && close(output) != 0) ok = false;
    char copied[SHA256_HEX_CAPACITY] = {0};
    if (ok) sha256_finish_hex(&hash, (char8*)copied);
    ok = ok && !memcmp(copied, build->log_sha256[stage], SHA256_HEX_CAPACITY);
    count = ok ? snprintf(prefix, sizeof(prefix), "matched-stage-%u", stage) : -1;
    ok = ok && count > 0 && (u32)count < sizeof(prefix) &&
         bq_record_name(receipt_name, prefix, job->id);
    count = ok ? bq_retirement_build_stage_format(body, job, build, stage, exit_code) : -1;
    BqError result = ok && count > 0 && (u32)count < sizeof(body) ?
        bq_record_write_mode_at(evidence.directory, receipt_name, (u8 const*)body, (u32)count, false, 0400) : BQ_IO;
    if (result == BQ_OK)
        bq_digest(body, (u32)count, (char8*)build->stage_receipt_sha256[stage]);
    return result;
}

/* Retain the generated configured directory until its matching build starts.
 * It must still be the root this helper created empty for this generate
 * (same inode, service-owned, exact mode), so neither a no-op nor another
 * subject's cached configuration can stand in for it. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_build_generated_root(BqRetirementMatchedBuild* build,
    BqRetirementBuildProcess const* process)
{
    u32 side = process->stage / 2u;
    mode_t mode = side ? BQ_RETIREMENT_BUILD_CANDIDATE_ROOT_MODE : BQ_RETIREMENT_BUILD_BASE_ROOT_MODE;
    int root = side < 2 ? bq_open_absolute_directory(string_from_pointer(build->build[side])) : -1;
    struct stat generated = {0};
    bool ok = build->generated_root == -1 && root >= 3 && process->build_root >= 3 &&
              fstat(root, &generated) == 0 && S_ISDIR(generated.st_mode) &&
              generated.st_uid == geteuid() && (generated.st_mode & 07777) == mode &&
              generated.st_dev == (dev_t)process->build_device && generated.st_ino == (ino_t)process->build_inode;
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

/* Copy only the successful build's observed Release/ide into the private
 * work directory's trusted-build/, which no stage unit can write. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_build_freeze(BqRetirementMatchedBuild* build,
    int workspaces, BqJob const* job, u32 side, uid_t candidate_uid,
    BqRetirementBuildProcess const* process)
{
    int work = bq_retirement_work_open(workspaces, job);
    int root = process->build_root;
    struct stat build_stat = {0};
    bool same_root = root >= 3 && fstat(root, &build_stat) == 0 &&
                     (u64)build_stat.st_dev == process->build_device &&
                     (u64)build_stat.st_ino == process->build_inode;
    int named_root = same_root ? bq_open_absolute_directory(string_from_pointer(build->build[side])) : -1;
    struct stat named_stat = {0};
    same_root = same_root && named_root >= 3 && fstat(named_root, &named_stat) == 0 &&
                (u64)named_stat.st_dev == process->build_device &&
                (u64)named_stat.st_ino == process->build_inode;
    if (named_root >= 0 && close(named_root) != 0) same_root = false;
    int release = same_root ? openat(root, "Release", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int input = release >= 0 ? openat(release, "ide", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat first = {0};
    bool ok = same_root && work >= 0 && input >= 0 &&
              fstat(input, &first) == 0 && S_ISREG(first.st_mode) && first.st_nlink == 1 &&
              first.st_uid == (side ? candidate_uid : geteuid()) &&
              (first.st_mode & S_IXUSR) && first.st_size > 0 &&
              (u64)first.st_size <= BQ_RETIREMENT_BUILD_BINARY_CAP &&
              bq_retirement_build_output_stable(root, release, input, &first);
    if (ok && !side) ok = mkdirat(work, "trusted-build", 0700) == 0;
    int frozen = ok ? openat(work, "trusted-build", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
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
    int final_root = ok ? bq_open_absolute_directory(string_from_pointer(build->build[side])) : -1;
    if (ok) ok = final_root >= 3 && fstat(final_root, &named_stat) == 0 &&
                 (u64)named_stat.st_dev == process->build_device &&
                 (u64)named_stat.st_ino == process->build_inode &&
                 bq_retirement_build_output_stable(root, release, input, &first);
    if (final_root >= 0 && close(final_root) != 0) ok = false;
    if (frozen >= 0 && close(frozen) != 0) ok = false;
    if (input >= 0 && close(input) != 0) ok = false;
    if (release >= 0 && close(release) != 0) ok = false;
    if (work >= 0 && close(work) != 0) ok = false;
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

BUSTER_GLOBAL_LOCAL BqError bq_retirement_build_final_record(BqRetirementStore evidence,
    BqJob const* job, BqRetirementMatchedBuild* build)
{
    char name[48], body[2048];
    int length = bq_retirement_build_final_format(body, job, build);
    BqError result = length > 0 && (u32)length < sizeof(body) &&
        bq_record_name(name, "matched-builds", job->id) ?
        bq_record_write_mode_at(evidence.directory, name, (u8 const*)body, (u32)length, false, 0400) : BQ_IO;
    if (result == BQ_OK)
        bq_digest(body, (u32)length, (char8*)build->build_record_sha256);
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_build_stage_import(BqRetirementStore evidence,
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
    int log = ok ? openat(evidence.directory, name,
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
    BqError result = ok ? bq_record_read_at(evidence.directory, name, (u8*)actual, sizeof(actual), &size) :
                     BQ_CORRUPT;
    if (result == BQ_OK)
        result = size == (u32)length && !memcmp(actual, expected, size) ? BQ_OK : BQ_CORRUPT;
    if (result == BQ_OK)
        bq_digest(actual, size, (char8*)build->stage_receipt_sha256[stage]);
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_import_stores(BqRetirementBuildStores stores,
    BqJob const* job, int installed, int workspaces, String8 workspace_root,
    String8 profile, char const* fixed_driver, char const* fixed_toolchain, char const* broker,
    char const* broker_workspaces, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const binary_record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementMatchedBuild* verified)
{
    if (verified) *verified = (BqRetirementMatchedBuild){0};
    bool valid = verified && binary_record_sha256 && build_record_sha256 &&
        bq_retirement_hex(string_from_pointer(binary_record_sha256), 64) &&
        bq_retirement_hex(string_from_pointer(build_record_sha256), 64);
    BqRetirementMatchedBuild current = {0};
    BqError result = valid ? bq_retirement_matched_build_begin_stores(stores, job, installed,
        workspaces, workspace_root, profile, fixed_driver, fixed_toolchain, broker, broker_workspaces,
        preparation_sha256, false, &current) :
        BQ_RECIPE_MISMATCH;
    BqRetirementBinaries binaries = {0};
    if (result == BQ_OK)
        result = bq_retirement_binaries_import_stores(stores, job, installed, workspaces,
            profile, preparation_sha256, binary_record_sha256, &binaries);
    if (result == BQ_OK)
        memcpy(current.binary_record_sha256, binary_record_sha256, SHA256_HEX_CAPACITY);
    for (u32 stage = 0; result == BQ_OK && stage < BQ_RETIREMENT_BUILD_STAGES; stage += 1)
        result = bq_retirement_build_stage_import(stores.evidence, job, &current, stage);
    if (result == BQ_OK)
    {
        char name[48], expected[2048], actual[2048], digest[SHA256_HEX_CAPACITY];
        u32 size = 0;
        int length = bq_retirement_build_final_format(expected, job, &current);
        result = length > 0 && (u32)length < sizeof(expected) &&
            bq_record_name(name, "matched-builds", job->id) ?
            bq_record_read_at(stores.evidence.directory, name, (u8*)actual, sizeof(actual), &size) : BQ_CORRUPT;
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

BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_import_pinned(BqQueue* queue,
    BqJob const* job, int installed, int workspaces, String8 workspace_root,
    String8 profile, char const* fixed_driver, char const* fixed_toolchain,
    char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const binary_record_sha256[SHA256_HEX_CAPACITY],
    char const build_record_sha256[SHA256_HEX_CAPACITY], BqRetirementMatchedBuild* verified)
{
    BqError result = bq_retirement_matched_build_import_stores(bq_retirement_build_queue_stores(queue), job,
        installed, workspaces, workspace_root, profile, fixed_driver, fixed_toolchain, NULL, NULL,
        preparation_sha256, binary_record_sha256, build_record_sha256, verified);
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

BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_complete_observed(BqRetirementBuildStores stores,
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
        result = bq_retirement_build_stage_evidence(stores.evidence, job, stage_log, process->exit_code, build);
    if (result == BQ_OK && process->exit_code != 0) result = BQ_WORKER_FAILED;
    if (result == BQ_OK && !bq_retirement_toolchain_recheck(&build->toolchain))
        result = BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK)
    {
        BqRetirementPreparation reread = {0};
        result = bq_retirement_preparation_import_pinned(stores.preparation, job, installed,
                   workspaces, profile, build->preparation_sha256, &reread);
    }
    if (result == BQ_OK && !(current & 1u))
        result = bq_retirement_build_generated_root(build, process) ? BQ_OK : BQ_SOURCE_MISMATCH;
    if (result == BQ_OK && (current & 1u))
        result = bq_retirement_build_freeze(build, workspaces, job, current / 2u,
                                            candidate_uid, process) ?
                 BQ_OK : BQ_SOURCE_MISMATCH;
    if (result == BQ_OK && current == BQ_RETIREMENT_BUILD_STAGES - 1u)
        result = bq_retirement_binaries_record_stores(stores, job, installed, workspaces,
                   profile, build->preparation_sha256, build->binary_record_sha256);
    if (result == BQ_OK && current == BQ_RETIREMENT_BUILD_STAGES - 1u)
        result = bq_retirement_build_final_record(stores.evidence, job, build);
    if (result == BQ_OK) build->next += 1;
    else if (build) build->failed = true;
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_complete_stores(BqRetirementBuildStores stores,
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
    if (ok)
        ok = process->build_root >= 3 && fstat(process->build_root, &build_stat) == 0 &&
             (u64)build_stat.st_dev == process->build_device &&
             (u64)build_stat.st_ino == process->build_inode;
    if (ok) bq_retirement_build_command_sha(&stage, command_sha256);
    int writer_flags = ok ? fcntl(process->writer, F_GETFD) : -1;
    int access_flags = writer_flags >= 0 ? fcntl(process->writer, F_GETFL) : -1;
    int current = ok ? bq_open_absolute_directory(string_from_pointer(build->work)) : -1;
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
    BqError result = ok ? bq_retirement_matched_build_complete_observed(stores, job,
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

BUSTER_GLOBAL_LOCAL BqError bq_retirement_matched_build_complete_pinned(BqQueue* queue,
    BqJob const* job, int installed, int workspaces, String8 profile,
    BqRetirementBuildProcess* process, uid_t candidate_uid, BqRetirementMatchedBuild* build)
{
    BqError result = bq_retirement_matched_build_complete_stores(bq_retirement_build_queue_stores(queue), job,
        installed, workspaces, profile, process, candidate_uid, build);
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
