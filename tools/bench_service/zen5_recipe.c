/* zen5-calibration-v1: the fixed #426 Zen 5 calibration recipe (served).
 *
 * Ownership: bench_service_zen5_recipe_add (the trusted build driver) owns the
 * phase order, the frozen binaries, the pre-sample plan and every file in the
 * result root. bench_service_zen5_capture_add (a stage program) owns only its
 * timed children and the raw records in its staging directory.
 *
 * queue.c admits and serves the recipe; the worker starts its outer unit with
 * the broker's `start-zen5-outer`, and every stage below goes through
 * `start-stage JOB ATTEMPT <stage> REV REV`. zen5_stage.h is the stage
 * contract the broker and credential gate share with this file: the stage
 * names, the exact argv, account, writable path and the PMU stage's
 * perf_event_open allowance. The self-test (zen5_recipe_test.c) runs the same
 * argv with direct children.
 *
 * Phases (bench_service_zen5_run): profile pins, copied into the driver-owned
 * zen5/pmu-tool, and source identity (the tree file must be manifest-listed)
 * -> five serial trusted builds with frozen copies (bench_service_zen5_builds;
 * the driver itself runs `ninja -t commands`) -> untimed oracle -> PMU phase
 * outside timing -> bench_service_zen5_plan_freeze, durable before any timed
 * child -> one captures stage running the three fixed 120-slot captures in the
 * #884/#915 formats -> bundle and final manifest (bench_service_zen5_finish).
 * All six frozen paths are re-hashed before the oracle, PMU and plan phases
 * and after the captures (bench_service_zen5_frozen_verify); each capture
 * runner receives the expected digests in its spec. ab-authorized is always
 * false, and an invalid capture fails the attempt while staying published.
 *
 * Map: bench_service_zen5_profile_value, bench_service_zen5_child,
 * bench_service_zen5_stage, bench_service_zen5_commands, bench_service_zen5_pmu_tools,
 * bench_service_zen5_frozen_verify, bench_service_zen5_capture_check, bench_service_zen5_text_section,
 * bench_service_zen5_json_*, bench_service_zen5_schedule,
 * bench_service_zen5_family, bench_service_zen5_header,
 * bench_service_zen5_capture_run, bench_service_zen5_publish,
 * bench_service_zen5_bundle.
 */
#include "zen5_calibration_profile.h"
#include "zen5_stage.h"

#define BENCH_SERVICE_ZEN5_NAME "zen5-calibration-v1"
#define BENCH_SERVICE_ZEN5_BUILDS 5u
#define BENCH_SERVICE_ZEN5_CAPTURES 3u
#define BENCH_SERVICE_ZEN5_SLOTS 120u
#define BENCH_SERVICE_ZEN5_PAIRS_PER_ROUND 60u
#define BENCH_SERVICE_ZEN5_PAIRS_PER_BLOCK 15u
#define BENCH_SERVICE_ZEN5_ORACLE_BINARIES 6u
#define BENCH_SERVICE_ZEN5_TEXT_CAP (4u * 1024u * 1024u)
#define BENCH_SERVICE_ZEN5_BINARY_CAP (512ull * 1024 * 1024)
#define BENCH_SERVICE_ZEN5_CHILD_SECONDS 120u
#define BENCH_SERVICE_ZEN5_MANIFEST_CAP (4u * 1024u * 1024u)
#define BENCH_SERVICE_ZEN5_BROKER "/usr/local/libexec/buster-bench-systemd-broker"
#define BENCH_SERVICE_ZEN5_PYTHON BQ_ZEN5_STAGE_PYTHON
#define BENCH_SERVICE_ZEN5_NINJA "/usr/bin/ninja"
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif

#if BUSTER_LINUX
/* Self-test seams only; production never assigns them. `direct` runs the
 * stage programs as plain children instead of through the systemd broker. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_direct;
BUSTER_GLOBAL_LOCAL String8 bench_service_zen5_python_override;
BUSTER_GLOBAL_LOCAL String8 bench_service_zen5_ninja_override;
BUSTER_GLOBAL_LOCAL String8 bench_service_zen5_self_override;
BUSTER_GLOBAL_LOCAL u64 bench_service_zen5_test_gap_ns;
BUSTER_GLOBAL_LOCAL u64 bench_service_zen5_test_reserve_seconds;
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_tamper_plan;
/* Change a frozen binary 1: before the oracle; 2: after the captures stage;
 * 3: after the capture specs, so only the capture runner can notice. */
BUSTER_GLOBAL_LOCAL u32 bench_service_zen5_test_tamper_frozen;
BUSTER_GLOBAL_LOCAL u64 bench_service_zen5_test_budget_seconds;
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_cpu_set;
BUSTER_GLOBAL_LOCAL u64 bench_service_zen5_test_cpu;

typedef struct BenchServiceZen5Child BenchServiceZen5Child;
struct BenchServiceZen5Child
{
    int status;
    bool launched;
    bool timed_out;
    u64 started_ns;
    u64 finished_ns;
    u64 wall_ns;
    u64 peak_rss_bytes;
};

typedef struct BenchServiceZen5Build BenchServiceZen5Build;
struct BenchServiceZen5Build
{
    char const* id;
    char const* root_name;
    char root[BENCH_SERVICE_RECIPE_PATH_CAP];
    char frozen[BENCH_SERVICE_RECIPE_PATH_CAP];
    u64 started_ns;
    u64 finished_ns;
    u64 size;
    u32 mode;
    char sha256[SHA256_HEX_CAPACITY];
    u64 text_offset;
    u64 text_address;
    u64 text_size;
    char text_sha256[SHA256_HEX_CAPACITY];
    char compile_commands_sha256[SHA256_HEX_CAPACITY];
    char normalized_commands_sha256[SHA256_HEX_CAPACITY];
    char build_log_sha256[SHA256_HEX_CAPACITY];
    String8 compile_command;
    String8 link_command;
};

typedef struct BenchServiceZen5 BenchServiceZen5;
struct BenchServiceZen5
{
    String8 job_id;
    String8 attempt_token;
    String8 workspace_root;
    String8 revision;
    String8 result_root;
    char attempt[BENCH_SERVICE_RECIPE_PATH_CAP];
    char source[BENCH_SERVICE_RECIPE_PATH_CAP];
    char zen5[BENCH_SERVICE_RECIPE_PATH_CAP];
    char immutable_path1[BENCH_SERVICE_RECIPE_PATH_CAP];
    char driver[BENCH_SERVICE_RECIPE_PATH_CAP];
    char python[BENCH_SERVICE_RECIPE_PATH_CAP];
    char ninja[BENCH_SERVICE_RECIPE_PATH_CAP];
    char self[BENCH_SERVICE_RECIPE_PATH_CAP];
    char workload[256];
    int result_directory;
    int logs_directory;
    u64 started_ns;
    u64 deadline_ns;
    u64 reserve_ns;
    u64 gap_ns;
    u64 cpu;
    u64 budget_seconds;
    u64 plan_frozen_ns;
    char tree[48];
    char source_identity[SHA256_HEX_CAPACITY];
    char profile_sha256[SHA256_HEX_CAPACITY];
    char toolchain[SHA256_HEX_CAPACITY];
    char environment[SHA256_HEX_CAPACITY];
    char expected_output[SHA256_HEX_CAPACITY];
    char host_qualification[SHA256_HEX_CAPACITY];
    char fingerprint[SHA256_HEX_CAPACITY];
    char pmu_status[32];
    char plan_sha256[SHA256_HEX_CAPACITY];
    char capture_sha256[BENCH_SERVICE_ZEN5_CAPTURES][SHA256_HEX_CAPACITY];
    bool capture_complete[BENCH_SERVICE_ZEN5_CAPTURES];
    bool oracle_consistent;
    BenchServiceZen5Build builds[BENCH_SERVICE_ZEN5_BUILDS];
    char const* stage;
    char reason[192];
};

typedef struct BenchServiceZen5Json BenchServiceZen5Json;
struct BenchServiceZen5Json
{
    Arena* arena;
    String8List list;
    bool ok;
};

typedef struct BenchServiceZen5Slot BenchServiceZen5Slot;
struct BenchServiceZen5Slot
{
    BenchServiceZen5Child run[2];
    char output[2][SHA256_HEX_CAPACITY];
    char binary[2][SHA256_HEX_CAPACITY];
};

BUSTER_GLOBAL_LOCAL char const* const bench_service_zen5_capture_names[BENCH_SERVICE_ZEN5_CAPTURES] = {
    "immutable", "same-root-rebuild", "cross-root"};

BUSTER_GLOBAL_LOCAL u64 bench_service_zen5_now(void)
{
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    u64 result = (u64)now.tv_sec * 1000000000ull + (u64)now.tv_nsec;
    return result;
}

/* One `key=value` line of the compiled profile, which must occur once. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_profile_value(char const* key, char* output, u32 capacity)
{
    char const* profile = BQ_ZEN5_CALIBRATION_PROFILE;
    u32 key_length = (u32)strlen(key);
    u32 matches = 0;
    char const* line = profile;
    while (*line)
    {
        char const* end = strchr(line, '\n');
        u32 length = end ? (u32)(end - line) : (u32)strlen(line);
        if (length > key_length && !memcmp(line, key, key_length) && line[key_length] == '=')
        {
            u32 value_length = length - key_length - 1;
            matches += 1;
            if (value_length < capacity)
            {
                memcpy(output, line + key_length + 1, value_length);
                output[value_length] = 0;
            }
            else matches += 1;
        }
        line += end ? length + 1 : length;
    }
    return matches == 1;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_profile_u64(char const* key, u64* value)
{
    char text[32] = {0};
    bool ok = bench_service_zen5_profile_value(key, text, sizeof(text));
    IntegerParsingU64 parsed = string8_parse_u64_decimal(string_from_pointer(text));
    ok = ok && parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == strlen(text) && parsed.value != 0;
    *value = ok ? parsed.value : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_hex(char const* text, u32 length)
{
    bool ok = text && strlen(text) == length;
    for (u32 index = 0; ok && index < length; index += 1)
        ok = (text[index] >= '0' && text[index] <= '9') || (text[index] >= 'a' && text[index] <= 'f');
    return ok;
}

/* Hash a regular, unlinked-alias-free file without following a final symlink. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_file_digest(char const* path, u64 cap, char digest[SHA256_HEX_CAPACITY],
                                                         u64* size, u32* mode)
{
    int descriptor = path ? open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 &&
              (u64)info.st_size <= cap;
    Sha256 hash;
    sha256_init(&hash);
    u64 total = 0;
    u8 bytes[64 * 1024];
    while (ok)
    {
        ssize_t count = read(descriptor, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
        {
            ok = count == 0;
            break;
        }
        sha256_add(&hash, bytes, (u64)count);
        total += (u64)count;
    }
    ok = ok && total == (u64)info.st_size;
    if (ok) sha256_finish_hex(&hash, (char8*)digest);
    if (size) *size = ok ? total : 0;
    if (mode) *mode = ok ? (u32)(info.st_mode & 07777) : 0;
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_read(Arena* arena, char const* path, u64 cap, String8* output)
{
    int descriptor = path ? open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 &&
              (u64)info.st_size <= cap;
    u64 size = ok ? (u64)info.st_size : 0;
    char8* bytes = ok ? arena_allocate(arena, char8, size + 1) : NULL;
    u64 used = 0;
    while (ok && used < size)
    {
        ssize_t count = read(descriptor, bytes + used, (size_t)(size - used));
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) used += (u64)count;
    }
    if (ok)
    {
        char extra = 0;
        ok = read(descriptor, &extra, 1) == 0;
        bytes[used] = 0;
    }
    *output = ok ? (String8){bytes, used} : (String8){0};
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_write_all(int descriptor, void const* data, u64 length)
{
    u8 const* bytes = (u8 const*)data;
    bool ok = descriptor >= 0;
    for (u64 offset = 0; ok && offset < length;)
    {
        ssize_t count = write(descriptor, bytes + offset, (size_t)(length - offset));
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) offset += (u64)count;
    }
    return ok;
}

/* A fresh attempt-tree file: exclusive creation, then read-only. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_write_new(char const* path, String8 body, mode_t mode)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    bool ok = bench_service_zen5_write_all(descriptor, body.pointer, body.length) && fsync(descriptor) == 0 &&
              fchmod(descriptor, mode) == 0;
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    return ok;
}

/* No-replace publication into the pinned result tree, as the validation
 * recipe publishes its manifests: O_EXCL temporary, fsync, linkat, unlink. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_publish(int directory, char const* name, String8 body)
{
    char temporary[128] = {0};
    struct stat temporary_info = {0}, published_info = {0};
    bool ok = directory >= 0 && body.length <= BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP &&
              bench_service_recipe_temp_name(name, temporary);
    int descriptor = ok ? openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && bench_service_zen5_write_all(descriptor, body.pointer, body.length) && fchmod(descriptor, 0400) == 0 &&
         fsync(descriptor) == 0 && fstat(descriptor, &temporary_info) == 0 &&
         bench_service_recipe_entry_matches(directory, temporary, &temporary_info) &&
         linkat(directory, temporary, directory, name, 0) == 0 &&
         fstatat(directory, name, &published_info, AT_SYMLINK_NOFOLLOW) == 0 &&
         published_info.st_dev == temporary_info.st_dev && published_info.st_ino == temporary_info.st_ino;
    bool removed = !temporary[0] || descriptor < 0 ||
                   bench_service_recipe_unlink_if_same(directory, temporary, &temporary_info);
    ok = ok && removed && fsync(directory) == 0;
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    return ok;
}

/* Copy a staging record into the result tree and report its digest. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_publish_copy(Arena* arena, char const* source, int directory,
                                                         char const* name, char digest[SHA256_HEX_CAPACITY])
{
    String8 bytes = {0};
    bool ok = bench_service_zen5_read(arena, source, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, &bytes) &&
              bench_service_zen5_publish(directory, name, bytes);
    if (ok && digest)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, bytes.pointer, bytes.length);
        sha256_finish_hex(&hash, (char8*)digest);
    }
    return ok;
}

/* Byte-exact frozen copy of a built executable; the destination is new. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_copy_binary(char const* source, char const* destination, mode_t mode)
{
    int input = open(source, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    int output = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat info = {0};
    bool ok = input >= 0 && output >= 0 && fstat(input, &info) == 0 && S_ISREG(info.st_mode) &&
              (u64)info.st_size <= BENCH_SERVICE_ZEN5_BINARY_CAP;
    u8 bytes[64 * 1024];
    while (ok)
    {
        ssize_t count = read(input, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
        {
            ok = count == 0;
            break;
        }
        ok = bench_service_zen5_write_all(output, bytes, (u64)count);
    }
    ok = ok && fchmod(output, mode) == 0 && fsync(output) == 0;
    if (input >= 0 && close(input) != 0) ok = false;
    if (output >= 0 && close(output) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_path(char* output, char const* parent, char const* child)
{
    int length = snprintf(output, BENCH_SERVICE_RECIPE_PATH_CAP, "%s/%s", parent, child);
    bool ok = length > 0 && (u32)length < BENCH_SERVICE_RECIPE_PATH_CAP;
    return ok;
}

/* Run one child in its own process group. The exit is observed through a
 * pidfd, so the wall interval is bounded by fork and wait4 with no polling
 * quantum; a deadline kills the whole group. Exit status reports 128+signal. */
BUSTER_GLOBAL_LOCAL BenchServiceZen5Child bench_service_zen5_child(char* const* argv, char const* directory, int output,
                                                                   u64 deadline_ns)
{
    BenchServiceZen5Child child = {.status = -1};
    int null_input = open("/dev/null", O_RDONLY | O_CLOEXEC);
    int null_output = output < 0 ? open("/dev/null", O_WRONLY | O_CLOEXEC) : -1;
    int sink = output >= 0 ? output : null_output;
    child.started_ns = bench_service_zen5_now();
    pid_t pid = sink >= 0 && null_input >= 0 ? fork() : -1;
    if (pid == 0)
    {
        bool ready = setpgid(0, 0) == 0 && dup2(null_input, STDIN_FILENO) >= 0 && dup2(sink, STDOUT_FILENO) >= 0 &&
                     dup2(sink, STDERR_FILENO) >= 0 && (!directory || chdir(directory) == 0);
        if (ready) execv(argv[0], argv);
        _exit(125);
    }
    if (pid > 0)
    {
        (void)setpgid(pid, pid);
        int handle = (int)syscall(SYS_pidfd_open, pid, 0);
        bool exited = false;
        while (handle >= 0 && !exited && !child.timed_out)
        {
            u64 now = bench_service_zen5_now();
            u64 remaining_ms = now < deadline_ns ? (deadline_ns - now) / 1000000u + 1u : 0;
            struct pollfd waiting = {handle, POLLIN, 0};
            int ready = remaining_ms ? poll(&waiting, 1, remaining_ms > 60000u ? 60000 : (int)remaining_ms) : 0;
            exited = ready > 0;
            if (!remaining_ms || (ready < 0 && errno != EINTR)) child.timed_out = true;
        }
        /* The leader is at worst a zombie here, so its group id cannot be
         * reused before this kill removes any surviving descendant. */
        (void)kill(-pid, SIGKILL);
        struct rusage usage = {0};
        int status = 0;
        pid_t waited = wait4(pid, &status, 0, &usage);
        while (waited < 0 && errno == EINTR)
        {
            waited = wait4(pid, &status, 0, &usage);
        }
        child.finished_ns = bench_service_zen5_now();
        child.launched = waited == pid && handle >= 0;
        if (waited == pid)
        {
            child.status = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
            child.wall_ns = child.finished_ns - child.started_ns;
            child.peak_rss_bytes = usage.ru_maxrss > 0 ? (u64)usage.ru_maxrss * 1024u : 0;
        }
        if (handle >= 0) close(handle);
    }
    if (null_input >= 0) close(null_input);
    if (null_output >= 0) close(null_output);
    return child;
}

/* Recipe stage `index` of zen5_stage.h. In production the recipe asks the
 * systemd broker to start the typed transient unit, which builds the same
 * argv from the shared contract; the self-test's direct mode runs that argv
 * as a plain child, substituting only the program (its stand-ins) and the
 * PMU CPU (a CPU the test host has). The stage's output is its log. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_stage(BenchServiceZen5* recipe, u32 index, int* status)
{
    static char const* const names[BQ_ZEN5_STAGE_COUNT] = {BQ_ZEN5_STAGE_NAMES};
    char const* stage = index < BQ_ZEN5_STAGE_COUNT ? names[index] : "unknown";
    char log_name[160] = {0}, job[32] = {0}, token[32] = {0}, revision[72] = {0}, cpu[24] = {0};
    BqZen5StageCommand stage_command;
    BqZen5StageCommand* command = &stage_command;
    int length = snprintf(log_name, sizeof(log_name), "%s.log", stage);
    bool named = index < BQ_ZEN5_STAGE_COUNT && length > 0 && (u32)length < sizeof(log_name) &&
                 snprintf(job, sizeof(job), "%.*s", (int)recipe->job_id.length, recipe->job_id.pointer) > 0 &&
                 snprintf(token, sizeof(token), "%.*s", (int)recipe->attempt_token.length, recipe->attempt_token.pointer) > 0 &&
                 snprintf(revision, sizeof(revision), "%.*s", (int)recipe->revision.length, recipe->revision.pointer) > 0 &&
                 snprintf(cpu, sizeof(cpu), "%llu", (unsigned long long)recipe->cpu) > 0 &&
                 bq_zen5_stage_command(index, recipe->attempt, command);
    char* direct[BQ_ZEN5_STAGE_MAX_ARGS + 1] = {0};
    for (u32 argument = 0; named && argument <= command->count; argument += 1)
        direct[argument] = (char*)command->argv[argument];
    if (named)
    {
        direct[0] = index < BQ_ZEN5_STAGE_ORACLE ? recipe->driver : index == BQ_ZEN5_STAGE_PMU ? recipe->python : recipe->self;
        for (u32 argument = 1; index == BQ_ZEN5_STAGE_PMU && argument + 1 < command->count; argument += 1)
            if (!strcmp(command->argv[argument], "--cpu")) direct[argument + 1] = cpu;
    }
    int log = named ? openat(recipe->logs_directory, log_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    char* broker[] = {BENCH_SERVICE_ZEN5_BROKER, "start-stage", job, token, (char*)stage, revision, revision, NULL};
    printf("+ zen5 stage %s\n", stage);
    fflush(stdout);
    BenchServiceZen5Child child = {.status = -1};
    /* No stage starts at or past the deadline (a capture stage would begin a
     * timed child only to be killed). */
    child.timed_out = bench_service_zen5_now() >= recipe->deadline_ns;
    if (log >= 0 && !child.timed_out)
        child = bench_service_zen5_child(bench_service_zen5_direct ? direct : broker, recipe->source, log, recipe->deadline_ns);
    bool ok = log >= 0 && child.launched && !child.timed_out;
    if (log >= 0)
    {
        if (fchmod(log, 0400) != 0 || fsync(log) != 0) ok = false;
        if (close(log) != 0) ok = false;
    }
    if (child.timed_out) snprintf(recipe->reason, sizeof(recipe->reason), "stage %s exceeded the recipe budget", stage);
    else if (!ok) snprintf(recipe->reason, sizeof(recipe->reason), "stage %s could not run", stage);
    *status = child.status;
    return ok;
}

/* `ninja -t commands ide` of one configured root, run by the trusted driver
 * itself (read-only, no candidate code) into logs/zen5-<id>-commands.log. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_commands(BenchServiceZen5* recipe, BenchServiceZen5Build const* build,
                                                     char* log_name, u32 log_capacity)
{
    int length = snprintf(log_name, log_capacity, "zen5-%s-commands.log", build->id);
    bool ok = length > 0 && (u32)length < log_capacity;
    int log = ok ? openat(recipe->logs_directory, log_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    char* argv[] = {recipe->ninja, "-C", (char*)build->root, "-f", "build-Release.ninja", "-t", "commands", "ide", NULL};
    BenchServiceZen5Child child = log >= 0 ? bench_service_zen5_child(argv, recipe->source, log, recipe->deadline_ns) :
                                             (BenchServiceZen5Child){.status = -1};
    ok = log >= 0 && child.launched && !child.timed_out && child.status == 0;
    if (log >= 0)
    {
        if (fchmod(log, 0400) != 0 || fsync(log) != 0) ok = false;
        if (close(log) != 0) ok = false;
    }
    if (child.timed_out) snprintf(recipe->reason, sizeof(recipe->reason), "build %s commands exceeded the recipe budget",
                                  build->id);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_pread(int descriptor, void* bytes, u64 size, u64 offset)
{
    bool ok = descriptor >= 0;
    for (u64 used = 0; ok && used < size;)
    {
        ssize_t count = pread(descriptor, (u8*)bytes + used, (size_t)(size - used), (off_t)(offset + used));
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) used += (u64)count;
    }
    return ok;
}

/* The ELF64 `.text` section of a frozen binary: file offset, address, size and
 * byte digest, read with bounded preads rather than loading the image. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_text_section(Arena* arena, BenchServiceZen5Build* build)
{
    int descriptor = open(build->frozen, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info = {0};
    u8 header[64] = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 64 &&
              bench_service_zen5_pread(descriptor, header, sizeof(header), 0) && !memcmp(header, "\177ELF", 4) &&
              header[4] == 2 && header[5] == 1;
    u64 image = ok ? (u64)info.st_size : 0, table = 0;
    u16 entry_size = 0, count = 0, names = 0;
    if (ok)
    {
        memcpy(&table, header + 0x28, 8);
        memcpy(&entry_size, header + 0x3a, 2);
        memcpy(&count, header + 0x3c, 2);
        memcpy(&names, header + 0x3e, 2);
        ok = entry_size == 64 && count > 0 && names < count && table <= image && (u64)count * 64u <= image - table;
    }
    u8* sections = ok ? arena_allocate(arena, u8, (u64)count * 64u) : NULL;
    ok = ok && bench_service_zen5_pread(descriptor, sections, (u64)count * 64u, table);
    u64 strings = 0, strings_size = 0;
    if (ok)
    {
        memcpy(&strings, sections + (u64)names * 64u + 0x18, 8);
        memcpy(&strings_size, sections + (u64)names * 64u + 0x20, 8);
        ok = strings <= image && strings_size <= image - strings && strings_size <= BENCH_SERVICE_ZEN5_TEXT_CAP;
    }
    char* string_table = ok ? arena_allocate(arena, char, strings_size + 1) : NULL;
    ok = ok && bench_service_zen5_pread(descriptor, string_table, strings_size, strings);
    u32 found = 0;
    for (u32 index = 0; ok && index < count; index += 1)
    {
        u8 const* section = sections + (u64)index * 64u;
        u32 name = 0;
        u64 address = 0, offset = 0, size = 0;
        memcpy(&name, section, 4);
        memcpy(&address, section + 0x10, 8);
        memcpy(&offset, section + 0x18, 8);
        memcpy(&size, section + 0x20, 8);
        if (name < strings_size && strings_size - name >= 6 && !memcmp(string_table + name, ".text", 6))
        {
            found += 1;
            ok = size > 0 && offset <= image && size <= image - offset;
            Sha256 hash;
            sha256_init(&hash);
            u8 bytes[64 * 1024];
            for (u64 done = 0; ok && done < size;)
            {
                u64 chunk = size - done < sizeof(bytes) ? size - done : sizeof(bytes);
                ok = bench_service_zen5_pread(descriptor, bytes, chunk, offset + done);
                if (ok) sha256_add(&hash, bytes, chunk);
                done += chunk;
            }
            if (ok)
            {
                sha256_finish_hex(&hash, (char8*)build->text_sha256);
                build->text_offset = offset;
                build->text_address = address;
                build->text_size = size;
            }
        }
    }
    ok = ok && found == 1;
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bench_service_zen5_json_raw(BenchServiceZen5Json* json, char const* text)
{
    string8_list_push(json->arena, &json->list, string_duplicate_arena(json->arena, string_from_pointer(text), false));
}

BUSTER_GLOBAL_LOCAL void bench_service_zen5_json_u64(BenchServiceZen5Json* json, u64 value)
{
    char text[32];
    snprintf(text, sizeof(text), "%llu", (unsigned long long)value);
    bench_service_zen5_json_raw(json, text);
}

/* A JSON string in Python's ensure_ascii form; non-ASCII bytes fail closed. */
BUSTER_GLOBAL_LOCAL void bench_service_zen5_json_string(BenchServiceZen5Json* json, String8 text)
{
    char8* escaped = arena_allocate(json->arena, char8, text.length * 6 + 3);
    u64 used = 0;
    escaped[used++] = '"';
    for (u64 index = 0; index < text.length; index += 1)
    {
        u8 value = (u8)text.pointer[index];
        char const* shortcut = value == '"' ? "\\\"" : value == '\\' ? "\\\\" : value == '\n' ? "\\n" :
                               value == '\r' ? "\\r" : value == '\t' ? "\\t" : value == '\b' ? "\\b" :
                               value == '\f' ? "\\f" : NULL;
        if (value >= 0x80) json->ok = false;
        if (shortcut)
        {
            memcpy(escaped + used, shortcut, 2);
            used += 2;
        }
        else if (value < 0x20)
        {
            used += (u64)snprintf((char*)escaped + used, 7, "\\u%04x", value);
        }
        else escaped[used++] = (char8)value;
    }
    escaped[used++] = '"';
    string8_list_push(json->arena, &json->list, (String8){escaped, used});
}

BUSTER_GLOBAL_LOCAL void bench_service_zen5_json_cstring(BenchServiceZen5Json* json, char const* text)
{
    bench_service_zen5_json_string(json, string_from_pointer(text));
}

BUSTER_GLOBAL_LOCAL String8 bench_service_zen5_json_join(BenchServiceZen5Json* json)
{
    String8 result = json->ok ? string_join_arena(json->arena, string8_list_to_slice(json->arena, json->list), false) :
                                (String8){0};
    return result;
}

/* The fixed version-1 A/A schedule (tools/zen5_aa_noise.py expected_schedule)
 * in canonical key order. Assignment block b: labels swap in blocks 1 and 3,
 * paths swap in blocks 2 and 3; AB/BA alternates on round+block+pair parity. */
BUSTER_GLOBAL_LOCAL void bench_service_zen5_schedule_slot(u32 sequence, u32* round, u32* block, u32* pair, bool* ab,
                                                          bool* label_swapped, bool* path_swapped)
{
    *round = sequence / BENCH_SERVICE_ZEN5_PAIRS_PER_ROUND;
    *block = (sequence % BENCH_SERVICE_ZEN5_PAIRS_PER_ROUND) / BENCH_SERVICE_ZEN5_PAIRS_PER_BLOCK;
    *pair = sequence % BENCH_SERVICE_ZEN5_PAIRS_PER_BLOCK;
    *ab = (*round + *block + *pair) % 2 == 0;
    *label_swapped = *block == 1 || *block == 3;
    *path_swapped = *block >= 2;
}

BUSTER_GLOBAL_LOCAL void bench_service_zen5_schedule(BenchServiceZen5Json* json)
{
    bench_service_zen5_json_raw(json, "[");
    for (u32 sequence = 0; sequence < BENCH_SERVICE_ZEN5_SLOTS; sequence += 1)
    {
        u32 round = 0, block = 0, pair = 0;
        bool ab = false, label_swapped = false, path_swapped = false;
        bench_service_zen5_schedule_slot(sequence, &round, &block, &pair, &ab, &label_swapped, &path_swapped);
        char entry[256];
        snprintf(entry, sizeof(entry),
                 "%s{\"block\":%u,\"label_assignment\":\"%s\",\"order\":\"%s\",\"pair_in_block\":%u,"
                 "\"path_assignment\":\"%s\",\"round\":%u,\"sequence\":%u}",
                 sequence ? "," : "", block, label_swapped ? "swapped" : "normal", ab ? "AB" : "BA", pair,
                 path_swapped ? "swapped" : "normal", round, sequence);
        bench_service_zen5_json_raw(json, entry);
    }
    bench_service_zen5_json_raw(json, "]");
}

BUSTER_GLOBAL_LOCAL void bench_service_zen5_aa_plan(BenchServiceZen5Json* json, u64 gap_ns)
{
    BenchServiceZen5Json schedule = {.arena = json->arena, .ok = true};
    bench_service_zen5_schedule(&schedule);
    bench_service_zen5_json_raw(&schedule, "\n");
    String8 bytes = bench_service_zen5_json_join(&schedule);
    char digest[SHA256_HEX_CAPACITY] = {0};
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes.pointer, bytes.length);
    sha256_finish_hex(&hash, (char8*)digest);
    char text[640];
    snprintf(text, sizeof(text),
             "{\"blocks_per_round\":4,\"decision\":\"descriptive-only\",\"minimum_interblock_gap_ns\":%llu,"
             "\"outlier_policy\":\"retain-all-no-deletion\",\"pairs_per_block\":15,\"pairs_per_round\":60,"
             "\"rounds\":2,\"schedule_sha256\":\"%s\",\"stopping_rule\":\"fixed-count-no-optional-stopping\","
             "\"total_pairs\":120}",
             (unsigned long long)gap_ns, digest);
    bench_service_zen5_json_raw(json, text);
}

BUSTER_GLOBAL_LOCAL void bench_service_zen5_repository(BenchServiceZen5Json* json, BenchServiceZen5 const* recipe)
{
    bench_service_zen5_json_raw(json, "{\"revision\":");
    bench_service_zen5_json_string(json, recipe->revision);
    bench_service_zen5_json_raw(json, ",\"status\":\"\",\"tree\":");
    bench_service_zen5_json_cstring(json, recipe->tree);
    bench_service_zen5_json_raw(json, "}");
}

BUSTER_GLOBAL_LOCAL BenchServiceZen5Build const* bench_service_zen5_control(BenchServiceZen5 const* recipe, u32 capture,
                                                                            u32 label)
{
    BenchServiceZen5Build const* result = capture ? recipe->builds + 1 + (capture - 1) * 2 + label : recipe->builds;
    return result;
}

/* The pre-sample family plan: tools/zen5_calibration_handoff.py freeze output,
 * in canonical sorted-key form so its digest is the replay's plan digest. */
BUSTER_GLOBAL_LOCAL String8 bench_service_zen5_family(Arena* arena, BenchServiceZen5 const* recipe)
{
    BenchServiceZen5Json json = {.arena = arena, .ok = true};
    bench_service_zen5_json_raw(&json, "{\"aa_plan\":");
    bench_service_zen5_aa_plan(&json, recipe->gap_ns);
    bench_service_zen5_json_raw(&json, ",\"controls\":{");
    for (u32 capture = 2; capture >= 1; capture -= 1)
    {
        /* cross-root sorts before same-root-rebuild. */
        BenchServiceZen5Build const* a = bench_service_zen5_control(recipe, capture, 0);
        BenchServiceZen5Build const* b = bench_service_zen5_control(recipe, capture, 1);
        bench_service_zen5_json_raw(&json, capture == 2 ? "" : ",");
        bench_service_zen5_json_cstring(&json, bench_service_zen5_capture_names[capture]);
        bench_service_zen5_json_raw(&json, ":{\"binaries\":{\"A\":");
        bench_service_zen5_json_cstring(&json, a->sha256);
        bench_service_zen5_json_raw(&json, ",\"B\":");
        bench_service_zen5_json_cstring(&json, b->sha256);
        bench_service_zen5_json_raw(&json, "},\"roots\":{\"A\":");
        bench_service_zen5_json_cstring(&json, a->root);
        bench_service_zen5_json_raw(&json, ",\"B\":");
        bench_service_zen5_json_cstring(&json, b->root);
        bench_service_zen5_json_raw(&json, "}}");
    }
    bench_service_zen5_json_raw(&json, "},\"environment_fingerprint_sha256\":");
    bench_service_zen5_json_cstring(&json, recipe->fingerprint);
    bench_service_zen5_json_raw(&json, ",\"expected_output_sha256\":");
    bench_service_zen5_json_cstring(&json, recipe->expected_output);
    bench_service_zen5_json_raw(&json, ",\"host_qualification_sha256\":");
    bench_service_zen5_json_cstring(&json, recipe->host_qualification);
    bench_service_zen5_json_raw(&json, ",\"immutable_binary_sha256\":");
    bench_service_zen5_json_cstring(&json, recipe->builds[0].sha256);
    bench_service_zen5_json_raw(&json, ",\"repository\":");
    bench_service_zen5_repository(&json, recipe);
    bench_service_zen5_json_raw(&json, ",\"schema\":\"buster-zen5-calibration-family-v1\",\"source_identity_sha256\":");
    bench_service_zen5_json_cstring(&json, recipe->source_identity);
    bench_service_zen5_json_raw(&json, ",\"version\":1}\n");
    return bench_service_zen5_json_join(&json);
}

BUSTER_GLOBAL_LOCAL void bench_service_zen5_argv(BenchServiceZen5Json* json, char const* const* argv, u32 count)
{
    bench_service_zen5_json_raw(json, "[");
    for (u32 index = 0; index < count; index += 1)
    {
        bench_service_zen5_json_raw(json, index ? "," : "");
        bench_service_zen5_json_cstring(json, argv[index]);
    }
    bench_service_zen5_json_raw(json, "]");
}

BUSTER_GLOBAL_LOCAL void bench_service_zen5_identity(BenchServiceZen5Json* json, char const* path, u64 size, u32 mode,
                                                     char const* digest)
{
    bench_service_zen5_json_raw(json, "{\"path\":");
    bench_service_zen5_json_cstring(json, path);
    bench_service_zen5_json_raw(json, ",\"size\":");
    bench_service_zen5_json_u64(json, size);
    bench_service_zen5_json_raw(json, ",\"mode\":");
    bench_service_zen5_json_u64(json, mode);
    bench_service_zen5_json_raw(json, ",\"sha256\":");
    bench_service_zen5_json_cstring(json, digest);
    bench_service_zen5_json_raw(json, "}");
}

/* One #915 build record (tools/zen5_build_control.py build_problems). */
BUSTER_GLOBAL_LOCAL void bench_service_zen5_build_json(BenchServiceZen5Json* json, BenchServiceZen5 const* recipe,
                                                       BenchServiceZen5Build const* build)
{
    char const* build_argv[] = {recipe->driver, "build", "--build-directory", build->root, "--config", "Release",
                                "-t", "ide", "--", "-j1"};
    char const* compile_argv[] = {"/bin/sh", "-c", (char const*)build->compile_command.pointer};
    char const* link_argv[] = {"/bin/sh", "-c", (char const*)build->link_command.pointer};
    bench_service_zen5_json_raw(json, "{\"source_revision\":");
    bench_service_zen5_json_string(json, recipe->revision);
    bench_service_zen5_json_raw(json, ",\"source_tree\":");
    bench_service_zen5_json_cstring(json, recipe->tree);
    bench_service_zen5_json_raw(json, ",\"source_identity_sha256\":");
    bench_service_zen5_json_cstring(json, recipe->source_identity);
    bench_service_zen5_json_raw(json, ",\"build_id\":");
    bench_service_zen5_json_cstring(json, build->id);
    bench_service_zen5_json_raw(json, ",\"build_root\":");
    bench_service_zen5_json_cstring(json, build->root);
    bench_service_zen5_json_raw(json, ",\"toolchain_identity_sha256\":");
    bench_service_zen5_json_cstring(json, recipe->toolchain);
    bench_service_zen5_json_raw(json, ",\"build_environment_sha256\":");
    bench_service_zen5_json_cstring(json, recipe->environment);
    bench_service_zen5_json_raw(json, ",\"build_argv\":");
    bench_service_zen5_argv(json, build_argv, BUSTER_ARRAY_LENGTH(build_argv));
    bench_service_zen5_json_raw(json, ",\"compile_argv\":");
    bench_service_zen5_argv(json, compile_argv, BUSTER_ARRAY_LENGTH(compile_argv));
    bench_service_zen5_json_raw(json, ",\"link_argv\":");
    bench_service_zen5_argv(json, link_argv, BUSTER_ARRAY_LENGTH(link_argv));
    bench_service_zen5_json_raw(json, ",\"compile_commands_sha256\":");
    bench_service_zen5_json_cstring(json, build->compile_commands_sha256);
    bench_service_zen5_json_raw(json, ",\"normalized_commands_sha256\":");
    bench_service_zen5_json_cstring(json, build->normalized_commands_sha256);
    bench_service_zen5_json_raw(json, ",\"build_log_sha256\":");
    bench_service_zen5_json_cstring(json, build->build_log_sha256);
    bench_service_zen5_json_raw(json, ",\"build_started_monotonic_ns\":");
    bench_service_zen5_json_u64(json, build->started_ns);
    bench_service_zen5_json_raw(json, ",\"build_finished_monotonic_ns\":");
    bench_service_zen5_json_u64(json, build->finished_ns);
    bench_service_zen5_json_raw(json, ",\"binary\":");
    bench_service_zen5_identity(json, build->frozen, build->size, build->mode, build->sha256);
    bench_service_zen5_json_raw(json, ",\"text_section\":{\"file_offset\":");
    bench_service_zen5_json_u64(json, build->text_offset);
    bench_service_zen5_json_raw(json, ",\"virtual_address\":");
    bench_service_zen5_json_u64(json, build->text_address);
    bench_service_zen5_json_raw(json, ",\"size\":");
    bench_service_zen5_json_u64(json, build->text_size);
    bench_service_zen5_json_raw(json, ",\"sha256\":");
    bench_service_zen5_json_cstring(json, build->text_sha256);
    bench_service_zen5_json_raw(json, "}}");
}

/* The trusted, fixed members of one capture; the runner appends the samples. */
BUSTER_GLOBAL_LOCAL String8 bench_service_zen5_header(Arena* arena, BenchServiceZen5 const* recipe, u32 capture,
                                                      char const staging[2][BENCH_SERVICE_RECIPE_PATH_CAP])
{
    BenchServiceZen5Json json = {.arena = arena, .ok = true};
    char const* workload[] = {"cc", "-g0", "-O0", "-c", recipe->workload, "-o", "${OUTPUT}"};
    bench_service_zen5_json_raw(&json, capture ? "\"schema\":\"buster-zen5-build-control-capture-v1\",\"version\":1,"
                                                 "\"purpose\":\"same-source cross-build calibration\",\"control_kind\":" :
                                                 "\"schema\":\"buster-zen5-aa-capture-v1\",\"version\":1,"
                                                 "\"purpose\":\"immutable-binary A/A noise calibration\"");
    if (capture) bench_service_zen5_json_cstring(&json, bench_service_zen5_capture_names[capture]);
    bench_service_zen5_json_raw(&json, ",\"metric_family\":[\"wall_time_ns\",\"peak_rss_bytes\"],"
                                       "\"statistical_decision\":\"not-evaluated\",\"ordinary_ci_guard_unchanged\":true,"
                                       "\"recipe\":\"" BENCH_SERVICE_ZEN5_NAME "\",\"ab_authorized\":false,\"job\":");
    bench_service_zen5_json_string(&json, recipe->job_id);
    bench_service_zen5_json_raw(&json, ",\"attempt\":");
    bench_service_zen5_json_string(&json, recipe->attempt_token);
    bench_service_zen5_json_raw(&json, ",\"repository\":");
    bench_service_zen5_repository(&json, recipe);
    bench_service_zen5_json_raw(&json, ",\"environment_fingerprint_sha256\":");
    bench_service_zen5_json_cstring(&json, recipe->fingerprint);
    bench_service_zen5_json_raw(&json, ",\"host_qualification_sha256\":");
    bench_service_zen5_json_cstring(&json, recipe->host_qualification);
    bench_service_zen5_json_raw(&json, ",\"source_identity_sha256\":");
    bench_service_zen5_json_cstring(&json, recipe->source_identity);
    bench_service_zen5_json_raw(&json, ",\"expected_output_sha256\":");
    bench_service_zen5_json_cstring(&json, recipe->expected_output);
    bench_service_zen5_json_raw(&json, ",\"predeclared_family_sha256\":");
    bench_service_zen5_json_cstring(&json, recipe->plan_sha256);
    bench_service_zen5_json_raw(&json, ",\"plan\":");
    bench_service_zen5_aa_plan(&json, recipe->gap_ns);
    bench_service_zen5_json_raw(&json, ",\"workload\":{\"directory\":");
    bench_service_zen5_json_cstring(&json, recipe->source);
    bench_service_zen5_json_raw(&json, ",\"argv\":");
    bench_service_zen5_argv(&json, workload, BUSTER_ARRAY_LENGTH(workload));
    bench_service_zen5_json_raw(&json, "}");
    if (!capture)
    {
        BenchServiceZen5Build const* build = recipe->builds;
        bench_service_zen5_json_raw(&json, ",\"binary_paths\":{\"path0\":");
        bench_service_zen5_identity(&json, build->frozen, build->size, build->mode, build->sha256);
        bench_service_zen5_json_raw(&json, ",\"path1\":");
        bench_service_zen5_identity(&json, recipe->immutable_path1, build->size, build->mode, build->sha256);
        bench_service_zen5_json_raw(&json, "}");
    }
    else
    {
        bench_service_zen5_json_raw(&json, ",\"staging_paths\":{\"path0\":");
        bench_service_zen5_json_cstring(&json, staging[0]);
        bench_service_zen5_json_raw(&json, ",\"path1\":");
        bench_service_zen5_json_cstring(&json, staging[1]);
        bench_service_zen5_json_raw(&json, "},\"builds\":{\"A\":");
        bench_service_zen5_build_json(&json, recipe, bench_service_zen5_control(recipe, capture, 0));
        bench_service_zen5_json_raw(&json, ",\"B\":");
        bench_service_zen5_build_json(&json, recipe, bench_service_zen5_control(recipe, capture, 1));
        bench_service_zen5_json_raw(&json, "}");
    }
    return bench_service_zen5_json_join(&json);
}

/* ---------------------------------------------------------------------------
 * Capture runner (stage program). The spec is a trusted, read-only file the
 * driver writes; the runner verifies the frozen plan before its first child.
 */
#define BENCH_SERVICE_ZEN5_SPEC_KEYS 18u
BUSTER_GLOBAL_LOCAL char const* const bench_service_zen5_spec_keys[BENCH_SERVICE_ZEN5_SPEC_KEYS] = {
    "kind", "plan", "plan-sha256", "header", "directory", "input", "output", "gap-ns",
    "expected-output", "path0", "path1", "binary-a", "binary-b", "children-log",
    "path0-sha256", "path1-sha256", "binary-a-sha256", "binary-b-sha256"};

typedef struct BenchServiceZen5Spec BenchServiceZen5Spec;
struct BenchServiceZen5Spec
{
    String8 values[BENCH_SERVICE_ZEN5_SPEC_KEYS];
    char binaries[BENCH_SERVICE_ZEN5_ORACLE_BINARIES][BENCH_SERVICE_RECIPE_PATH_CAP];
    u32 binary_count;
    bool oracle;
};

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_spec_parse(String8 text, BenchServiceZen5Spec* spec)
{
    String8 header_capture = S8("BQ-ZEN5-CAPTURE-V1\n");
    String8 header_oracle = S8("BQ-ZEN5-ORACLE-V1\n");
    *spec = (BenchServiceZen5Spec){0};
    spec->oracle = string_starts_with_sequence(text, header_oracle);
    bool ok = spec->oracle || string_starts_with_sequence(text, header_capture);
    u64 offset = spec->oracle ? header_oracle.length : header_capture.length;
    while (ok && offset < text.length)
    {
        u64 end = offset;
        while (end < text.length && text.pointer[end] != '\n') end += 1;
        String8 line = {text.pointer + offset, end - offset};
        u64 equals = 0;
        while (equals < line.length && line.pointer[equals] != '=') equals += 1;
        String8 key = {line.pointer, equals};
        String8 value = equals < line.length ? (String8){line.pointer + equals + 1, line.length - equals - 1} : (String8){0};
        ok = end < text.length && equals < line.length && value.length > 0 && value.length < BENCH_SERVICE_RECIPE_PATH_CAP;
        bool known = false;
        if (ok && spec->oracle && string_equal(key, S8("binary")))
        {
            known = spec->binary_count < BENCH_SERVICE_ZEN5_ORACLE_BINARIES;
            if (known) snprintf(spec->binaries[spec->binary_count++], BENCH_SERVICE_RECIPE_PATH_CAP, "%.*s",
                                (int)value.length, value.pointer);
        }
        for (u32 index = 0; ok && !known && index < BENCH_SERVICE_ZEN5_SPEC_KEYS; index += 1)
        {
            if (string_equal(key, string_from_pointer(bench_service_zen5_spec_keys[index])))
            {
                known = spec->values[index].length == 0;
                spec->values[index] = value;
            }
        }
        ok = ok && known;
        offset = end + 1;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL String8 bench_service_zen5_spec_value(BenchServiceZen5Spec const* spec, char const* key)
{
    String8 result = {0};
    for (u32 index = 0; index < BENCH_SERVICE_ZEN5_SPEC_KEYS; index += 1)
        if (!strcmp(key, bench_service_zen5_spec_keys[index])) result = spec->values[index];
    return result;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_spec_text(BenchServiceZen5Spec const* spec, char const* key, char* output,
                                                      u32 capacity)
{
    String8 value = bench_service_zen5_spec_value(spec, key);
    bool ok = value.length > 0 && value.length < capacity;
    if (ok) snprintf(output, capacity, "%.*s", (int)value.length, value.pointer);
    return ok;
}

/* Execute one workload child and hash its output outside the wall interval. */
BUSTER_GLOBAL_LOCAL BenchServiceZen5Child bench_service_zen5_workload(char const* binary, char const* directory,
                                                                      char const* input, char const* output, int log,
                                                                      char digest[SHA256_HEX_CAPACITY])
{
    char* argv[] = {(char*)binary, "cc", "-g0", "-O0", "-c", (char*)input, "-o", (char*)output, NULL};
    bool cleared = unlink(output) == 0 || errno == ENOENT;
    BenchServiceZen5Child child = cleared ?
        bench_service_zen5_child(argv, directory, log,
                                 bench_service_zen5_now() + BENCH_SERVICE_ZEN5_CHILD_SECONDS * 1000000000ull) :
        (BenchServiceZen5Child){.status = -1};
    digest[0] = 0;
    if (!bench_service_zen5_file_digest(output, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, digest, NULL, NULL)) digest[0] = 0;
    return child;
}

/* Place one frozen control binary at a mutable staging path (between blocks). */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_stage_binary(char const* frozen, char const* staging, char const* expected)
{
    char digest[SHA256_HEX_CAPACITY] = {0};
    bool ok = (unlink(staging) == 0 || errno == ENOENT) && bench_service_zen5_copy_binary(frozen, staging, 0555) &&
              bench_service_zen5_file_digest(staging, BENCH_SERVICE_ZEN5_BINARY_CAP, digest, NULL, NULL) &&
              !strcmp(digest, expected);
    return ok;
}

BUSTER_GLOBAL_LOCAL void bench_service_zen5_reason(BenchServiceZen5Json* slot, BenchServiceZen5Json* all, u32 sequence,
                                                   bool* first_reason, char const* text)
{
    bench_service_zen5_json_raw(slot, *first_reason ? "" : ",");
    bench_service_zen5_json_cstring(slot, text);
    char prefixed[256];
    snprintf(prefixed, sizeof(prefixed), "observation %u: %s", sequence, text);
    bench_service_zen5_json_raw(all, all->list.count ? "," : "");
    bench_service_zen5_json_cstring(all, prefixed);
    *first_reason = false;
}

BUSTER_GLOBAL_LOCAL ProcessResult bench_service_zen5_capture_run(Arena* arena, char const* spec_path, char const* output_path)
{
    String8 spec_text = {0};
    BenchServiceZen5Spec spec;
    bool ok = bench_service_zen5_read(arena, spec_path, 64 * 1024, &spec_text) &&
              bench_service_zen5_spec_parse(spec_text, &spec);
    char directory[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, input[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    char output[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, log_path[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    ok = ok && bench_service_zen5_spec_text(&spec, "directory", directory, sizeof(directory)) &&
         bench_service_zen5_spec_text(&spec, "input", input, sizeof(input)) && bench_service_zen5_spec_text(&spec, "output", output, sizeof(output)) &&
         bench_service_zen5_spec_text(&spec, "children-log", log_path, sizeof(log_path));
    /* Group-readable: the trusted driver (another account) publishes it. */
    int log = ok ? open(log_path, O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0640) : -1;
    ok = ok && log >= 0;
    BenchServiceZen5Json record = {.arena = arena, .ok = true};
    if (ok && spec.oracle)
    {
        /* Untimed oracle: every frozen binary once, before the plan exists. */
        bench_service_zen5_json_raw(&record, "BQ-ZEN5-ORACLE-RECORD-V1\n");
        ok = spec.binary_count == BENCH_SERVICE_ZEN5_ORACLE_BINARIES;
        for (u32 index = 0; ok && index < spec.binary_count; index += 1)
        {
            char digest[SHA256_HEX_CAPACITY];
            BenchServiceZen5Child child = bench_service_zen5_workload(spec.binaries[index], directory, input, output, log,
                                                                      digest);
            char line[160];
            snprintf(line, sizeof(line), "%u %d %s %llu %llu\n", index, child.status, digest[0] ? digest : "-",
                     (unsigned long long)child.wall_ns, (unsigned long long)child.peak_rss_bytes);
            bench_service_zen5_json_raw(&record, line);
        }
    }
    else if (ok)
    {
        char kind[64] = {0}, plan[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, header_path[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
        char plan_expected[SHA256_HEX_CAPACITY + 8] = {0}, plan_actual[SHA256_HEX_CAPACITY] = {0};
        char expected_output[SHA256_HEX_CAPACITY + 8] = {0}, gap_text[32] = {0};
        char paths[2][BENCH_SERVICE_RECIPE_PATH_CAP] = {{0}}, binaries[2][BENCH_SERVICE_RECIPE_PATH_CAP] = {{0}};
        char binary_digest[2][SHA256_HEX_CAPACITY] = {{0}}, path_digest[2][SHA256_HEX_CAPACITY] = {{0}};
        ok = bench_service_zen5_spec_text(&spec, "kind", kind, sizeof(kind)) &&
             bench_service_zen5_spec_text(&spec, "plan", plan, sizeof(plan)) &&
             bench_service_zen5_spec_text(&spec, "plan-sha256", plan_expected, sizeof(plan_expected)) &&
             bench_service_zen5_spec_text(&spec, "header", header_path, sizeof(header_path)) &&
             bench_service_zen5_spec_text(&spec, "expected-output", expected_output, sizeof(expected_output)) &&
             bench_service_zen5_spec_text(&spec, "gap-ns", gap_text, sizeof(gap_text)) &&
             bench_service_zen5_spec_text(&spec, "path0", paths[0], sizeof(paths[0])) &&
             bench_service_zen5_spec_text(&spec, "path1", paths[1], sizeof(paths[1])) &&
             bench_service_zen5_hex(plan_expected, 64) && bench_service_zen5_hex(expected_output, 64);
        /* Expected binary digests come from the trusted spec (the digests
         * taken at freeze), never from the files this runner is about to run.
         * A control's frozen builds, or the immutable capture's two paths,
         * must match them before the first child. */
        bool control = ok && strcmp(kind, "immutable") != 0;
        char const* digest_keys[2][2] = {{"path0-sha256", "path1-sha256"}, {"binary-a-sha256", "binary-b-sha256"}};
        for (u32 index = 0; ok && index < 2; index += 1)
        {
            char* expected = control ? binary_digest[index] : path_digest[index];
            char const* file = control ? binaries[index] : paths[index];
            char actual[SHA256_HEX_CAPACITY] = {0};
            ok = (!control || bench_service_zen5_spec_text(&spec, index ? "binary-b" : "binary-a", binaries[index],
                                                           sizeof(binaries[index]))) &&
                 bench_service_zen5_spec_text(&spec, digest_keys[control][index], expected, SHA256_HEX_CAPACITY) &&
                 bench_service_zen5_hex(expected, 64) &&
                 bench_service_zen5_file_digest(file, BENCH_SERVICE_ZEN5_BINARY_CAP, actual, NULL, NULL) &&
                 !strcmp(actual, expected);
        }
        IntegerParsingU64 gap = string8_parse_u64_decimal(string_from_pointer(gap_text));
        ok = ok && gap.status == INTEGER_PARSING_SUCCESS && gap.length == strlen(gap_text) && gap.value > 0;
        /* The pre-sample plan must be the frozen one before any timed child. */
        ok = ok && bench_service_zen5_file_digest(plan, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, plan_actual, NULL, NULL) &&
             !strcmp(plan_actual, plan_expected);
        if (!ok) fprintf(stderr, "error: zen5 capture refused: spec, frozen plan or binaries do not verify\n");
        u64 verified_ns = bench_service_zen5_now();
        String8 header = {0};
        ok = ok && bench_service_zen5_read(arena, header_path, 256 * 1024, &header) && header.length > 0;
        for (u64 index = 0; ok && index < header.length; index += 1)
            ok = (u8)header.pointer[index] >= 0x20 && (u8)header.pointer[index] < 0x7f;
        BenchServiceZen5Slot* slots = ok ? arena_allocate(arena, BenchServiceZen5Slot, BENCH_SERVICE_ZEN5_SLOTS) : NULL;
        u64 previous_finish = 0;
        for (u32 sequence = 0; ok && sequence < BENCH_SERVICE_ZEN5_SLOTS; sequence += 1)
        {
            u32 round = 0, block = 0, pair = 0;
            bool ab = false, label_swapped = false, path_swapped = false;
            bench_service_zen5_schedule_slot(sequence, &round, &block, &pair, &ab, &label_swapped, &path_swapped);
            u32 path_of_a = (label_swapped ? 1u : 0u) ^ (path_swapped ? 1u : 0u);
            u32 order[2] = {ab ? path_of_a : path_of_a ^ 1u, ab ? path_of_a ^ 1u : path_of_a};
            if (pair == 0 && control)
            {
                ok = bench_service_zen5_stage_binary(binaries[0], paths[path_of_a], binary_digest[0]) &&
                     bench_service_zen5_stage_binary(binaries[1], paths[path_of_a ^ 1u], binary_digest[1]);
            }
            /* Separate blocks by the predeclared gap, measured end to start. */
            while (ok && pair == 0 && sequence && bench_service_zen5_now() < previous_finish + gap.value)
            {
                u64 remaining = previous_finish + gap.value - bench_service_zen5_now();
                struct timespec pause = {(time_t)(remaining / 1000000000ull), (long)(remaining % 1000000000ull)};
                nanosleep(&pause, NULL);
            }
            for (u32 position = 0; ok && position < 2; position += 1)
                slots[sequence].run[position] = bench_service_zen5_workload(paths[order[position]], directory, input,
                                                                            output, log, slots[sequence].output[position]);
            previous_finish = slots[sequence].run[1].finished_ns;
            if (ok && control && pair == BENCH_SERVICE_ZEN5_PAIRS_PER_BLOCK - 1)
            {
                /* Record the digest of what actually ran, measured after the
                 * block, for every slot of the block. */
                char after[2][SHA256_HEX_CAPACITY] = {{0}};
                for (u32 path = 0; path < 2; path += 1)
                    if (!bench_service_zen5_file_digest(paths[path], BENCH_SERVICE_ZEN5_BINARY_CAP, after[path], NULL, NULL))
                        after[path][0] = 0;
                for (u32 slot = sequence + 1 - BENCH_SERVICE_ZEN5_PAIRS_PER_BLOCK; slot <= sequence; slot += 1)
                {
                    u32 slot_round = 0, slot_block = 0, slot_pair = 0;
                    bool slot_ab = false, slot_labels = false, slot_paths = false;
                    bench_service_zen5_schedule_slot(slot, &slot_round, &slot_block, &slot_pair, &slot_ab, &slot_labels,
                                                     &slot_paths);
                    u32 slot_a = (slot_labels ? 1u : 0u) ^ (slot_paths ? 1u : 0u);
                    memcpy(slots[slot].binary[0], after[slot_ab ? slot_a : slot_a ^ 1u], SHA256_HEX_CAPACITY);
                    memcpy(slots[slot].binary[1], after[slot_ab ? slot_a ^ 1u : slot_a], SHA256_HEX_CAPACITY);
                }
            }
        }
        for (u32 path = 0; ok && !control && path < 2; path += 1)
        {
            /* The immutable paths are frozen; any change after the run
             * refuses the record rather than labelling slots. */
            char after[SHA256_HEX_CAPACITY] = {0};
            ok = bench_service_zen5_file_digest(paths[path], BENCH_SERVICE_ZEN5_BINARY_CAP, after, NULL, NULL) &&
                 !strcmp(after, path_digest[path]);
            if (!ok) fprintf(stderr, "error: zen5 immutable capture path%u changed during the run\n", path);
        }
        BenchServiceZen5Json observations = {.arena = arena, .ok = true};
        BenchServiceZen5Json reasons = {.arena = arena, .ok = true};
        u64 prior_finish = 0;
        for (u32 sequence = 0; ok && sequence < BENCH_SERVICE_ZEN5_SLOTS; sequence += 1)
        {
            u32 round = 0, block = 0, pair = 0;
            bool ab = false, label_swapped = false, path_swapped = false;
            bench_service_zen5_schedule_slot(sequence, &round, &block, &pair, &ab, &label_swapped, &path_swapped);
            u32 path_of_a = (label_swapped ? 1u : 0u) ^ (path_swapped ? 1u : 0u);
            u32 order[2] = {ab ? path_of_a : path_of_a ^ 1u, ab ? path_of_a ^ 1u : path_of_a};
            BenchServiceZen5Slot const* slot = slots + sequence;
            char entry[512];
            snprintf(entry, sizeof(entry),
                     "%s{\"sequence\":%u,\"round\":%u,\"block\":%u,\"pair_in_block\":%u,\"order\":\"%s\","
                     "\"label_assignment\":\"%s\",\"path_assignment\":\"%s\",\"first_label\":\"%s\","
                     "\"second_label\":\"%s\",\"first_path\":\"path%u\",\"second_path\":\"path%u\","
                     "\"started_monotonic_ns\":%llu,\"finished_monotonic_ns\":%llu",
                     sequence ? "," : "", sequence, round, block, pair, ab ? "AB" : "BA",
                     label_swapped ? "swapped" : "normal", path_swapped ? "swapped" : "normal", ab ? "A" : "B",
                     ab ? "B" : "A", order[0], order[1], (unsigned long long)slot->run[0].started_ns,
                     (unsigned long long)slot->run[1].finished_ns);
            bench_service_zen5_json_raw(&observations, entry);
            BenchServiceZen5Json slot_reasons = {.arena = arena, .ok = true};
            bool first_reason = true;
            char const* names[2] = {"first", "second"};
            for (u32 position = 0; position < 2; position += 1)
            {
                BenchServiceZen5Child const* run = slot->run + position;
                snprintf(entry, sizeof(entry),
                         ",\"%s_exit_status\":%d,\"%s_wall_ns\":%llu,\"%s_peak_rss_bytes\":%llu,\"%s_output_sha256\":",
                         names[position], run->status, names[position], (unsigned long long)run->wall_ns,
                         names[position], (unsigned long long)run->peak_rss_bytes, names[position]);
                bench_service_zen5_json_raw(&observations, entry);
                if (slot->output[position][0]) bench_service_zen5_json_cstring(&observations, slot->output[position]);
                else bench_service_zen5_json_raw(&observations, "null");
                /* Exactly tools/zen5_aa_noise.py observation_reasons. */
                char text[160];
                if (run->status != 0)
                {
                    snprintf(text, sizeof(text), "%s execution exited with status %d", names[position], run->status);
                    bench_service_zen5_reason(&slot_reasons, &reasons, sequence, &first_reason, text);
                }
                if (!run->wall_ns)
                {
                    snprintf(text, sizeof(text), "%s wall time is unavailable or nonpositive", names[position]);
                    bench_service_zen5_reason(&slot_reasons, &reasons, sequence, &first_reason, text);
                }
                if (!run->peak_rss_bytes)
                {
                    snprintf(text, sizeof(text), "%s peak RSS is unavailable or nonpositive", names[position]);
                    bench_service_zen5_reason(&slot_reasons, &reasons, sequence, &first_reason, text);
                }
                if (strcmp(slot->output[position], expected_output) != 0)
                {
                    snprintf(text, sizeof(text), "%s output digest mismatch", names[position]);
                    bench_service_zen5_reason(&slot_reasons, &reasons, sequence, &first_reason, text);
                }
            }
            for (u32 position = 0; control && position < 2; position += 1)
            {
                /* tools/zen5_build_control.py per-slot binary reasons. */
                char const* label = (position == 0) == ab ? "A" : "B";
                char const* expected = binary_digest[label[0] == 'A' ? 0 : 1];
                char text[160];
                snprintf(entry, sizeof(entry), ",\"%s_binary_sha256\":", names[position]);
                bench_service_zen5_json_raw(&observations, entry);
                if (slot->binary[position][0]) bench_service_zen5_json_cstring(&observations, slot->binary[position]);
                else bench_service_zen5_json_raw(&observations, "null");
                if (!slot->binary[position][0])
                {
                    snprintf(text, sizeof(text), "%s binary digest is unavailable", names[position]);
                    bench_service_zen5_reason(&slot_reasons, &reasons, sequence, &first_reason, text);
                }
                else if (strcmp(slot->binary[position], expected) != 0)
                {
                    snprintf(text, sizeof(text), "%s binary digest differs from build %s", names[position], label);
                    bench_service_zen5_reason(&slot_reasons, &reasons, sequence, &first_reason, text);
                }
            }
            if (pair == 0 && sequence && slot->run[0].started_ns - prior_finish < gap.value)
            {
                char text[160];
                snprintf(text, sizeof(text), "observation %u: inter-block gap %llu ns is below %llu ns", sequence,
                         (unsigned long long)(slot->run[0].started_ns - prior_finish), (unsigned long long)gap.value);
                bench_service_zen5_json_raw(&reasons, reasons.list.count ? "," : "");
                bench_service_zen5_json_cstring(&reasons, text);
            }
            prior_finish = slot->run[1].finished_ns;
            bench_service_zen5_json_raw(&observations, first_reason ? ",\"valid\":true,\"invalid_reasons\":[" :
                                                                      ",\"valid\":false,\"invalid_reasons\":[");
            String8 joined = bench_service_zen5_json_join(&slot_reasons);
            string8_list_push(arena, &observations.list, joined);
            bench_service_zen5_json_raw(&observations, "]}");
        }
        if (ok)
        {
            bool complete = reasons.list.count == 0;
            bench_service_zen5_json_raw(&record, "{");
            string8_list_push(arena, &record.list, header);
            char text[160];
            snprintf(text, sizeof(text), ",\"runner\":\"bench_service_zen5_capture\",\"plan_verified_monotonic_ns\":%llu,"
                                         "\"schedule\":",
                     (unsigned long long)verified_ns);
            bench_service_zen5_json_raw(&record, text);
            bench_service_zen5_schedule(&record);
            bench_service_zen5_json_raw(&record, ",\"observations\":[");
            string8_list_push(arena, &record.list, bench_service_zen5_json_join(&observations));
            bench_service_zen5_json_raw(&record, "],\"invalid_reasons\":[");
            string8_list_push(arena, &record.list, bench_service_zen5_json_join(&reasons));
            bench_service_zen5_json_raw(&record, complete ? "],\"capture_status\":\"complete\"}\n" :
                                                            "],\"capture_status\":\"invalid\"}\n");
            ok = observations.ok && reasons.ok;
        }
    }
    String8 body = ok ? bench_service_zen5_json_join(&record) : (String8){0};
    ok = ok && body.length > 0 && bench_service_zen5_write_new(output_path, body, 0444);
    if (log >= 0 && close(log) != 0) ok = false;
    if (!ok) fprintf(stderr, "error: zen5 capture runner failed for %s\n", spec_path);
    ProcessResult result = ok ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

/* ---------------------------------------------------------------------------
 * Trusted driver phases.
 */
/* Attempt-tree directories inherit the workspace's SGID group the same way
 * the validation recipe's candidate staging does (no SGID mode request). */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_mkdir(char const* parent, char const* name, mode_t mode, char* output)
{
    int directory = bench_service_recipe_open_directory(string_from_pointer(parent));
    int child = directory >= 0 ? bench_service_recipe_create_inherited_group_directory(directory, name, mode) : -1;
    bool ok = child >= 0 && bench_service_zen5_path(output, parent, name) && fsync(directory) == 0;
    if (child >= 0 && close(child) != 0) ok = false;
    if (directory >= 0 && close(directory) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL int bench_service_zen5_result_directory(BenchServiceZen5* recipe, char const* relative)
{
    char path[BENCH_SERVICE_RECIPE_PATH_CAP];
    int length = snprintf(path, sizeof(path), "%.*s/%s", (int)recipe->result_root.length, recipe->result_root.pointer,
                          relative);
    int result = length > 0 && (u32)length < sizeof(path) ? bench_service_recipe_open_directory(string_from_pointer(path)) : -1;
    return result;
}

/* The four pinned PMU files. With `copy`, each snapshot file must match its
 * profile digest and is copied read-only into the driver-owned
 * zen5/pmu-tool; every call then re-hashes those copies, which are the only
 * files the PMU stage imports (zen5_stage.h). */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_pmu_tools(Arena* arena, BenchServiceZen5* recipe, bool copy)
{
    char const* pins[][2] = {{"pmu", "pmu-sha256"}, {"pmu-capture", "pmu-capture-sha256"},
                             {"pmu-common", "pmu-common-sha256"}, {"pmu-replay", "pmu-replay-sha256"}};
    char directory[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    bool ok = copy ? bench_service_zen5_mkdir(recipe->zen5, "pmu-tool", 02750, directory) :
                     bench_service_zen5_path(directory, recipe->zen5, "pmu-tool");
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(pins); index += 1)
    {
        char relative[256] = {0}, expected[SHA256_HEX_CAPACITY + 8] = {0}, actual[SHA256_HEX_CAPACITY] = {0};
        char path[BENCH_SERVICE_RECIPE_PATH_CAP], copied[BENCH_SERVICE_RECIPE_PATH_CAP];
        String8 bytes = {0};
        ok = bench_service_zen5_profile_value(pins[index][0], relative, sizeof(relative)) &&
             bench_service_zen5_profile_value(pins[index][1], expected, sizeof(expected)) &&
             strchr(relative, '/') != NULL && bench_service_zen5_path(copied, directory, strrchr(relative, '/') + 1);
        if (ok && copy)
            ok = bench_service_zen5_path(path, recipe->source, relative) &&
                 bench_service_zen5_read(arena, path, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, &bytes) &&
                 bench_service_zen5_write_new(copied, bytes, 0444);
        ok = ok && bench_service_zen5_file_digest(copied, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, actual, NULL, NULL) &&
             !strcmp(actual, expected);
        if (!ok) snprintf(recipe->reason, sizeof(recipe->reason), "pinned tool %.96s does not match the profile", relative);
    }
    return ok;
}

/* python3, perf and taskset run by name in the PMU stage (PATH=/usr/bin:/bin);
 * record what those names resolve to, or `missing`, as zen5/pmu/runtime.identity. */
BUSTER_GLOBAL_LOCAL String8 bench_service_zen5_runtime_identity(Arena* arena, BenchServiceZen5 const* recipe)
{
    char const* names[] = {"python3", "perf", "taskset"};
    String8List lines = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        char usr[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, bin[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
        char digest[SHA256_HEX_CAPACITY] = {0};
        snprintf(usr, sizeof(usr), "%s", index == 0 ? recipe->python : "/usr/bin/");
        snprintf(bin, sizeof(bin), "/bin/%s", names[index]);
        if (index) snprintf(usr, sizeof(usr), "/usr/bin/%s", names[index]);
        char const* found = access(usr, X_OK) == 0 ? usr : index && access(bin, X_OK) == 0 ? bin : NULL;
        char* resolved = found ? realpath(found, NULL) : NULL;
        bool hashed = resolved && bench_service_zen5_file_digest(resolved, BENCH_SERVICE_ZEN5_BINARY_CAP, digest, NULL, NULL);
        string8_list_push(arena, &lines, string_format(arena, S8("{S8} {S8} {S8}\n"), string_from_pointer(names[index]),
                                                       string_from_pointer(hashed ? resolved : "missing"),
                                                       string_from_pointer(hashed ? digest : "missing")));
        free(resolved);
    }
    return string_join_arena(arena, string8_list_to_slice(arena, lines), false);
}

/* Verify the pinned tools in the materialized snapshot and bind the source. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_identity_phase(Arena* arena, BenchServiceZen5* recipe)
{
    bool ok = bench_service_zen5_pmu_tools(arena, recipe, true);
    char tree_file[64] = {0}, path[BENCH_SERVICE_RECIPE_PATH_CAP];
    String8 tree = {0}, manifest = {0};
    if (ok)
    {
        ok = bench_service_zen5_profile_value("source-tree-file", tree_file, sizeof(tree_file)) &&
             bench_service_zen5_path(path, recipe->source, tree_file) && bench_service_zen5_read(arena, path, 64, &tree) &&
             tree.length == 41 && tree.pointer[40] == '\n';
        if (ok)
        {
            memcpy(recipe->tree, tree.pointer, 40);
            recipe->tree[40] = 0;
            ok = bench_service_zen5_hex(recipe->tree, 40);
        }
        if (!ok) snprintf(recipe->reason, sizeof(recipe->reason), "source snapshot lacks a valid %s", tree_file);
    }
    if (ok)
    {
        /* The materializer's verified manifest copy names every source byte.
         * The operator's installation lists the tree file in it, so the tree
         * identity is bound to the installed source, not merely present. */
        char expected[192], repository[96] = {0}, tree_digest[SHA256_HEX_CAPACITY] = {0}, listed[160] = {0};
        ok = bench_service_zen5_profile_value("repository", repository, sizeof(repository)) &&
             snprintf(expected, sizeof(expected), "BQ-SOURCE-V1\nrepository=%s\nrevision=%.*s\n", repository,
                      (int)recipe->revision.length, recipe->revision.pointer) > 0 &&
             bench_service_zen5_path(path, recipe->source, ".source-manifest") &&
             bench_service_zen5_read(arena, path, BENCH_SERVICE_ZEN5_MANIFEST_CAP, &manifest) &&
             string_starts_with_sequence(manifest, string_from_pointer(expected)) &&
             bench_service_zen5_file_digest(path, BENCH_SERVICE_ZEN5_MANIFEST_CAP, recipe->source_identity, NULL, NULL);
        if (!ok) snprintf(recipe->reason, sizeof(recipe->reason), "source manifest does not name the revision");
        char tree_path[BENCH_SERVICE_RECIPE_PATH_CAP];
        bool bound = ok && bench_service_zen5_path(tree_path, recipe->source, tree_file) &&
                     bench_service_zen5_file_digest(tree_path, 64, tree_digest, NULL, NULL) &&
                     snprintf(listed, sizeof(listed), "\n%s %s\n", tree_digest, tree_file) > 0 &&
                     string_first_sequence(manifest, string_from_pointer(listed)) != BUSTER_STRING_NO_MATCH;
        if (ok && !bound) snprintf(recipe->reason, sizeof(recipe->reason), "source manifest does not list %s", tree_file);
        ok = ok && bound;
    }
    if (ok)
    {
        char workload_digest[SHA256_HEX_CAPACITY] = {0};
        ok = bench_service_zen5_path(path, recipe->source, recipe->workload) &&
             bench_service_zen5_file_digest(path, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, workload_digest, NULL, NULL);
        if (!ok) snprintf(recipe->reason, sizeof(recipe->reason), "source snapshot lacks the fixed workload");
    }
    if (ok)
    {
        /* Toolchain and environment identities shared by all five builds. In
         * production the broker and credential gate give every stage exactly
         * this PATH and LC_ALL and clear the rest; the direct self-test mode
         * inherits the test's environment instead. */
        char const* tools[] = {"/usr/bin/clang", "/usr/bin/cmake", "/usr/bin/ninja"};
        String8List lines = {0};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(tools); index += 1)
        {
            char digest[SHA256_HEX_CAPACITY] = {0};
            char* resolved = realpath(tools[index], NULL);
            bool hashed = resolved && bench_service_zen5_file_digest(resolved, BENCH_SERVICE_ZEN5_BINARY_CAP, digest,
                                                                     NULL, NULL);
            string8_list_push(arena, &lines, string_format(arena, S8("{S8} {S8} {S8}\n"), string_from_pointer(tools[index]),
                                                           string_from_pointer(resolved && hashed ? resolved : "missing"),
                                                           string_from_pointer(hashed ? digest : "missing")));
            free(resolved);
        }
        String8 toolchain = string_join_arena(arena, string8_list_to_slice(arena, lines), false);
        String8 environment = S8("PATH=/usr/bin:/bin\nLC_ALL=C\ngenerate=Release clang no-include-tests "
                                 "no-developer-targets no-check-optional-warnings no-fuzz no-sanitize no-time-trace "
                                 "no-instrument no-lto\nbuild=Release target=ide jobs=1\n");
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, toolchain.pointer, toolchain.length);
        sha256_finish_hex(&hash, (char8*)recipe->toolchain);
        sha256_init(&hash);
        sha256_add(&hash, environment.pointer, environment.length);
        sha256_finish_hex(&hash, (char8*)recipe->environment);
        char profile_path[BENCH_SERVICE_RECIPE_PATH_CAP];
        String8 profile = S8(BQ_ZEN5_CALIBRATION_PROFILE);
        sha256_init(&hash);
        sha256_add(&hash, profile.pointer, profile.length);
        sha256_finish_hex(&hash, (char8*)recipe->profile_sha256);
        int zen5_result = bench_service_zen5_result_directory(recipe, "zen5");
        ok = bench_service_zen5_path(profile_path, recipe->zen5, "recipe.profile") &&
             bench_service_zen5_write_new(profile_path, profile, 0444) && zen5_result >= 0 &&
             bench_service_zen5_publish(zen5_result, "recipe.profile", profile) &&
             bench_service_zen5_publish(zen5_result, "toolchain.identity", toolchain);
        if (zen5_result >= 0 && close(zen5_result) != 0) ok = false;
        if (!ok) snprintf(recipe->reason, sizeof(recipe->reason), "recipe identities could not be recorded");
    }
    return ok;
}

/* Five serial trusted builds; each is frozen before the next begins, so the
 * same-root rebuild never overwrites its first binary. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_builds(Arena* arena, BenchServiceZen5* recipe)
{
    char builds[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, frozen[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    char scratch[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    bool ok = bench_service_zen5_mkdir(recipe->zen5, "builds", 02750, builds) &&
              bench_service_zen5_mkdir(recipe->zen5, "frozen", 02750, frozen);
    int records = ok ? bench_service_zen5_result_directory(recipe, "zen5/builds") : -1;
    ok = ok && records >= 0;
    for (u32 index = 0; ok && index < BENCH_SERVICE_ZEN5_BUILDS; index += 1)
    {
        BenchServiceZen5Build* build = recipe->builds + index;
        char name[64], commands_log[96] = {0}, root[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
        int status = -1;
        /* The stage unit can write only its configured root, so the trusted
         * driver creates it; generate clears its contents. Same-root B
         * reuses A's root. */
        bool fresh_root = index == 0 || strcmp(build->root_name, recipe->builds[index - 1].root_name) != 0;
        ok = bench_service_zen5_path(build->root, builds, build->root_name) &&
             (!fresh_root || bench_service_zen5_mkdir(builds, build->root_name, 02750, root)) &&
             bench_service_zen5_mkdir(frozen, build->id, 02750, scratch) &&
             bench_service_zen5_path(build->frozen, scratch, "ide");
        build->started_ns = bench_service_zen5_now();
        ok = ok && bench_service_zen5_stage(recipe, index * 2u, &status) && status == 0;
        ok = ok && bench_service_zen5_stage(recipe, index * 2u + 1u, &status) && status == 0;
        build->finished_ns = bench_service_zen5_now();
        ok = ok && bench_service_zen5_commands(recipe, build, commands_log, sizeof(commands_log));
        if (!ok && !recipe->reason[0]) snprintf(recipe->reason, sizeof(recipe->reason), "build %s failed", build->id);
        char built[BENCH_SERVICE_RECIPE_PATH_CAP], path[BENCH_SERVICE_RECIPE_PATH_CAP];
        ok = ok && bench_service_zen5_path(built, build->root, "Release/ide") &&
             bench_service_zen5_copy_binary(built, build->frozen, 0555) &&
             bench_service_zen5_file_digest(build->frozen, BENCH_SERVICE_ZEN5_BINARY_CAP, build->sha256, &build->size,
                                            &build->mode) &&
             bench_service_zen5_text_section(arena, build);
        if (ok && index == 0)
        {
            /* Two distinct pathnames with byte-identical immutable copies. */
            char second[BENCH_SERVICE_RECIPE_PATH_CAP], digest[SHA256_HEX_CAPACITY] = {0};
            ok = bench_service_zen5_mkdir(frozen, "immutable-path1", 02750, second) &&
                 bench_service_zen5_path(recipe->immutable_path1, second, "ide") &&
                 bench_service_zen5_copy_binary(build->frozen, recipe->immutable_path1, 0555) &&
                 bench_service_zen5_file_digest(recipe->immutable_path1, BENCH_SERVICE_ZEN5_BINARY_CAP, digest, NULL, NULL) &&
                 !strcmp(digest, build->sha256);
        }
        String8 commands_text = {0};
        ok = ok && bench_service_zen5_path(path, build->root, "compile_commands.json") &&
             bench_service_zen5_file_digest(path, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, build->compile_commands_sha256,
                                            NULL, NULL);
        snprintf(name, sizeof(name), "zen5-%s-build.log", build->id);
        char log_path[BENCH_SERVICE_RECIPE_PATH_CAP];
        int log_length = snprintf(log_path, sizeof(log_path), "%.*s/zen5/logs/%s", (int)recipe->result_root.length,
                                  recipe->result_root.pointer, name);
        ok = ok && log_length > 0 && (u32)log_length < sizeof(log_path) &&
             bench_service_zen5_file_digest(log_path, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, build->build_log_sha256,
                                            NULL, NULL);
        log_length = snprintf(log_path, sizeof(log_path), "%.*s/zen5/logs/%s", (int)recipe->result_root.length,
                              recipe->result_root.pointer, commands_log);
        ok = ok && log_length > 0 && (u32)log_length < sizeof(log_path) &&
             bench_service_zen5_read(arena, log_path, BENCH_SERVICE_ZEN5_TEXT_CAP, &commands_text);
        if (ok)
        {
            /* ninja -t commands: every line but the last compiles; the last
             * links. Normalization replaces the configured root. */
            u64 last = commands_text.length && commands_text.pointer[commands_text.length - 1] == '\n' ?
                       commands_text.length - 1 : commands_text.length;
            u64 split = last;
            while (split > 0 && commands_text.pointer[split - 1] != '\n') split -= 1;
            ok = split > 1 && split < last;
            String8 compile_lines = ok ? (String8){commands_text.pointer, split - 1} : (String8){0};
            String8List joined = {0};
            u64 start = 0;
            for (u64 cursor = 0; ok && cursor <= compile_lines.length; cursor += 1)
            {
                if (cursor == compile_lines.length || compile_lines.pointer[cursor] == '\n')
                {
                    if (joined.count) string8_list_push(arena, &joined, S8(" && "));
                    string8_list_push(arena, &joined, (String8){compile_lines.pointer + start, cursor - start});
                    start = cursor + 1;
                }
            }
            build->compile_command = string_join_arena(arena, string8_list_to_slice(arena, joined), true);
            build->link_command = string_duplicate_arena(arena, (String8){commands_text.pointer + split, last - split}, true);
            String8 root = string_from_pointer(build->root);
            String8List normalized = {0};
            u64 copied = 0;
            for (u64 cursor = 0; ok && cursor + root.length <= commands_text.length; cursor += 1)
            {
                if (!memcmp(commands_text.pointer + cursor, root.pointer, (size_t)root.length))
                {
                    string8_list_push(arena, &normalized, (String8){commands_text.pointer + copied, cursor - copied});
                    string8_list_push(arena, &normalized, S8("${BUILD_ROOT}"));
                    cursor += root.length - 1;
                    copied = cursor + 1;
                }
            }
            string8_list_push(arena, &normalized, (String8){commands_text.pointer + copied, commands_text.length - copied});
            String8 normal = string_join_arena(arena, string8_list_to_slice(arena, normalized), false);
            Sha256 hash;
            sha256_init(&hash);
            sha256_add(&hash, normal.pointer, normal.length);
            sha256_finish_hex(&hash, (char8*)build->normalized_commands_sha256);
        }
        if (ok)
        {
            BenchServiceZen5Json json = {.arena = arena, .ok = true};
            bench_service_zen5_build_json(&json, recipe, build);
            bench_service_zen5_json_raw(&json, "\n");
            snprintf(name, sizeof(name), "%s.json", build->id);
            ok = bench_service_zen5_publish(records, name, bench_service_zen5_json_join(&json));
        }
        if (!ok && !recipe->reason[0]) snprintf(recipe->reason, sizeof(recipe->reason), "build %s could not be frozen",
                                                build->id);
    }
    if (records >= 0 && close(records) != 0) ok = false;
    return ok;
}

/* Re-hash all six frozen paths (five builds and the second immutable path)
 * against the digests taken when each was frozen. Later trusted builds run
 * candidate CMake commands, so each phase after the builds re-checks. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_frozen_verify(BenchServiceZen5* recipe, char const* when)
{
    bool ok = true;
    for (u32 index = 0; ok && index <= BENCH_SERVICE_ZEN5_BUILDS; index += 1)
    {
        char digest[SHA256_HEX_CAPACITY] = {0};
        char const* path = index < BENCH_SERVICE_ZEN5_BUILDS ? recipe->builds[index].frozen : recipe->immutable_path1;
        char const* expected = recipe->builds[index < BENCH_SERVICE_ZEN5_BUILDS ? index : 0].sha256;
        ok = expected[0] && bench_service_zen5_file_digest(path, BENCH_SERVICE_ZEN5_BINARY_CAP, digest, NULL, NULL) &&
             !strcmp(digest, expected);
    }
    if (!ok && !recipe->reason[0]) snprintf(recipe->reason, sizeof(recipe->reason), "frozen binaries changed %s", when);
    return ok;
}

/* Self-test seam: append one byte to a read-only frozen binary. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_tamper(char const* path)
{
    bool ok = chmod(path, 0755) == 0;
    int descriptor = ok ? open(path, O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && bench_service_zen5_write_all(descriptor, (u8 const*)"\n", 1);
    if (descriptor >= 0) close(descriptor);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_staging(BenchServiceZen5* recipe, char const* name, char* output)
{
    char staging[BENCH_SERVICE_RECIPE_PATH_CAP];
    bool ok = bench_service_zen5_path(staging, recipe->zen5, "staging") &&
              bench_service_zen5_mkdir(staging, name, 02770, output);
    return ok;
}

/* Untimed oracle: every frozen binary compiles the workload once. The
 * immutable binary's output digest becomes the predeclared oracle. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_oracle(Arena* arena, BenchServiceZen5* recipe)
{
    char staging[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, spec_path[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    char record_path[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, output[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    char children[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, root[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    String8 record_header = S8("BQ-ZEN5-ORACLE-RECORD-V1\n");
    if (bench_service_zen5_test_tamper_frozen == 1) (void)bench_service_zen5_test_tamper(recipe->builds[3].frozen);
    bool ok = bench_service_zen5_frozen_verify(recipe, "before the oracle") &&
              bench_service_zen5_mkdir(recipe->zen5, "staging", 02750, root) &&
              bench_service_zen5_staging(recipe, "oracle", staging) &&
              bench_service_zen5_path(spec_path, recipe->zen5, "oracle.spec") &&
              bench_service_zen5_path(record_path, staging, "oracle.record") &&
              bench_service_zen5_path(output, staging, "output.o") &&
              bench_service_zen5_path(children, staging, "children.log");
    String8List spec = {0};
    string8_list_push(arena, &spec, S8("BQ-ZEN5-ORACLE-V1\n"));
    string8_list_push(arena, &spec, string_format(arena, S8("directory={S8}\ninput={S8}\noutput={S8}\nchildren-log={S8}\n"),
                                                  string_from_pointer(recipe->source), string_from_pointer(recipe->workload),
                                                  string_from_pointer(output), string_from_pointer(children)));
    string8_list_push(arena, &spec, string_format(arena, S8("binary={S8}\nbinary={S8}\n"),
                                                  string_from_pointer(recipe->builds[0].frozen),
                                                  string_from_pointer(recipe->immutable_path1)));
    for (u32 index = 1; index < BENCH_SERVICE_ZEN5_BUILDS; index += 1)
        string8_list_push(arena, &spec, string_format(arena, S8("binary={S8}\n"),
                                                      string_from_pointer(recipe->builds[index].frozen)));
    ok = ok && bench_service_zen5_write_new(spec_path, string_join_arena(arena, string8_list_to_slice(arena, spec), false), 0444);
    int status = -1;
    ok = ok && bench_service_zen5_stage(recipe, BQ_ZEN5_STAGE_ORACLE, &status) && status == 0;
    int zen5_result = ok ? bench_service_zen5_result_directory(recipe, "zen5") : -1;
    String8 record = {0};
    ok = ok && zen5_result >= 0 && bench_service_zen5_read(arena, record_path, 64 * 1024, &record) &&
         bench_service_zen5_publish(zen5_result, "oracle.record", record) &&
         bench_service_zen5_publish_copy(arena, children, zen5_result, "oracle.children.log", NULL) &&
         string_starts_with_sequence(record, record_header);
    if (zen5_result >= 0 && close(zen5_result) != 0) ok = false;
    u32 lines = 0;
    recipe->oracle_consistent = true;
    for (u64 offset = record_header.length; ok && offset < record.length; lines += 1)
    {
        u32 index = 0;
        int child_status = -1;
        char digest[80] = {0};
        ok = sscanf((char const*)record.pointer + offset, "%u %d %70s", &index, &child_status, digest) == 3 &&
             index == lines && child_status == 0 && bench_service_zen5_hex(digest, 64);
        if (ok && lines == 0) memcpy(recipe->expected_output, digest, SHA256_HEX_CAPACITY);
        if (ok && strcmp(digest, recipe->expected_output) != 0) recipe->oracle_consistent = false;
        while (offset < record.length && record.pointer[offset] != '\n') offset += 1;
        offset += 1;
    }
    ok = ok && lines == BENCH_SERVICE_ZEN5_ORACLE_BINARIES;
    if (!ok && !recipe->reason[0]) snprintf(recipe->reason, sizeof(recipe->reason), "untimed oracle failed");
    return ok;
}

/* The one `"key":"value"` occurrence in a canonical JSON record. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_json_field(String8 text, char const* key, char* output, u32 capacity)
{
    char needle[96];
    int needle_length = snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    u32 matches = 0;
    for (u64 index = 0; needle_length > 0 && index + (u64)needle_length <= text.length; index += 1)
    {
        if (!memcmp(text.pointer + index, needle, (size_t)needle_length))
        {
            u64 start = index + (u64)needle_length, end = start;
            while (end < text.length && text.pointer[end] != '"') end += 1;
            matches += 1;
            if (end < text.length && end - start < capacity)
            {
                memcpy(output, text.pointer + start, (size_t)(end - start));
                output[end - start] = 0;
            }
            else matches += 1;
        }
    }
    return matches == 1;
}

/* PMU phase outside every timed interval. perf/kernel refusal is retained as
 * an `invalid` qualification record, never a zero count; only a missing or
 * unreplayable record stops the attempt. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_pmu(Arena* arena, BenchServiceZen5* recipe)
{
    /* Paths are the stage contract's; the tool is the profile's pinned one. */
    char staging[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, record[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    char identity[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, relative[256] = {0};
    bool ok = bench_service_zen5_frozen_verify(recipe, "before the PMU phase") &&
              bench_service_zen5_pmu_tools(arena, recipe, false) && bench_service_zen5_staging(recipe, "pmu", staging) &&
              bench_service_zen5_path(record, staging, "zen5-pmu-v1.json") &&
              bench_service_zen5_path(identity, recipe->zen5, "repository.json") &&
              bench_service_zen5_profile_value("pmu-capture", relative, sizeof(relative)) &&
              !strcmp(relative, BQ_ZEN5_STAGE_PMU_TOOL);
    BenchServiceZen5Json repository = {.arena = arena, .ok = true};
    bench_service_zen5_repository(&repository, recipe);
    bench_service_zen5_json_raw(&repository, "\n");
    ok = ok && bench_service_zen5_write_new(identity, bench_service_zen5_json_join(&repository), 0444);
    int status = -1;
    ok = ok && bench_service_zen5_stage(recipe, BQ_ZEN5_STAGE_PMU, &status) && (status == 0 || status == 2);
    int pmu_result = ok ? bench_service_zen5_result_directory(recipe, "zen5/pmu") : -1;
    String8 text = {0};
    ok = ok && pmu_result >= 0 && bench_service_zen5_read(arena, record, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, &text) &&
         bench_service_zen5_publish(pmu_result, "zen5-pmu-v1.json", text) &&
         bench_service_zen5_publish(pmu_result, "runtime.identity", bench_service_zen5_runtime_identity(arena, recipe)) &&
         bench_service_zen5_json_field(text, "environment_fingerprint_sha256", recipe->fingerprint,
                                       sizeof(recipe->fingerprint)) &&
         bench_service_zen5_hex(recipe->fingerprint, 64) &&
         bench_service_zen5_json_field(text, "qualification_status", recipe->pmu_status, sizeof(recipe->pmu_status)) &&
         (!strcmp(recipe->pmu_status, "pmu-qualified") || !strcmp(recipe->pmu_status, "invalid")) &&
         (status == 0) == !strcmp(recipe->pmu_status, "pmu-qualified");
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, text.pointer, text.length);
        sha256_finish_hex(&hash, (char8*)recipe->host_qualification);
    }
    if (pmu_result >= 0 && close(pmu_result) != 0) ok = false;
    if (!ok && !recipe->reason[0]) snprintf(recipe->reason, sizeof(recipe->reason), "PMU phase produced no replayable record");
    return ok;
}

/* Freeze the family plan in the attempt tree (read by the capture runner)
 * and publish it, with a durable plan manifest, before any timed child. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_plan_freeze(Arena* arena, BenchServiceZen5* recipe)
{
    char plan[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    bool ok = bench_service_zen5_frozen_verify(recipe, "before the plan freeze");
    if (ok && bench_service_zen5_now() + recipe->reserve_ns > recipe->deadline_ns)
    {
        ok = false;
        snprintf(recipe->reason, sizeof(recipe->reason),
                 "the remaining budget is below the timing reserve; no timed child was started");
    }
    String8 family = ok ? bench_service_zen5_family(arena, recipe) : (String8){0};
    ok = ok && family.length > 0 && bench_service_zen5_path(plan, recipe->zen5, "plan.json") &&
         bench_service_zen5_write_new(plan, family, 0444);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, family.pointer, family.length);
        sha256_finish_hex(&hash, (char8*)recipe->plan_sha256);
        recipe->plan_frozen_ns = bench_service_zen5_now();
    }
    int zen5_result = ok ? bench_service_zen5_result_directory(recipe, "zen5") : -1;
    String8 manifest = string_format(arena,
        S8("schema=1\nrecipe=" BENCH_SERVICE_ZEN5_NAME "\nstatus=running\nstage=plan\njob-id={S8}\nattempt-token={S8}\n"
           "plan-sha256={S8}\nplan-frozen-monotonic-ns={u64}\ntimed-children-started=0\n"),
        recipe->job_id, recipe->attempt_token, string_from_pointer(recipe->plan_sha256), recipe->plan_frozen_ns);
    ok = ok && zen5_result >= 0 && bench_service_zen5_publish(zen5_result, "plan.json", family) &&
         bench_service_zen5_publish(recipe->result_directory, BENCH_SERVICE_ZEN5_NAME ".plan.manifest", manifest);
    if (zen5_result >= 0 && close(zen5_result) != 0) ok = false;
    if (ok && bench_service_zen5_test_tamper_plan)
    {
        /* Self-test seam: a changed attempt-tree plan must stop the runner. */
        ok = chmod(plan, 0644) == 0;
        int descriptor = ok ? open(plan, O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW) : -1;
        ok = ok && bench_service_zen5_write_all(descriptor, (u8 const*)" ", 1);
        if (descriptor >= 0) close(descriptor);
    }
    if (!ok && !recipe->reason[0]) snprintf(recipe->reason, sizeof(recipe->reason), "the pre-sample plan could not be frozen");
    return ok;
}

/* Native check of one capture record before it is published: the trusted
 * header is its verbatim prefix, it says ab_authorized false exactly once,
 * its observations are sequences 0..119 in order, and it ends with its
 * capture_status. The offline readers replay every slot; this refuses a
 * truncated, reordered or authorizing record without them. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_capture_check(String8 bytes, String8 header, bool* complete)
{
    String8 authorized = S8("\"ab_authorized\":");
    String8 complete_tail = S8(",\"capture_status\":\"complete\"}\n");
    String8 invalid_tail = S8(",\"capture_status\":\"invalid\"}\n");
    bool ok = bytes.length > header.length + 1 && bytes.pointer[0] == '{' &&
              !memcmp(bytes.pointer + 1, header.pointer, (size_t)header.length);
    u32 authorizations = 0, false_authorizations = 0, observations = 0;
    for (u64 index = 0; ok && index + authorized.length < bytes.length; index += 1)
    {
        if (!memcmp(bytes.pointer + index, authorized.pointer, (size_t)authorized.length))
        {
            authorizations += 1;
            String8 rest = {bytes.pointer + index + authorized.length, bytes.length - index - authorized.length};
            false_authorizations += string_starts_with_sequence(rest, S8("false,")) ? 1u : 0u;
        }
    }
    ok = ok && authorizations == 1 && false_authorizations == 1;
    String8 start = S8(",\"observations\":[");
    u64 cursor = ok ? string_first_sequence(bytes, start) : BUSTER_STRING_NO_MATCH;
    ok = ok && cursor != BUSTER_STRING_NO_MATCH;
    for (u32 sequence = 0; ok && sequence < BENCH_SERVICE_ZEN5_SLOTS; sequence += 1)
    {
        char needle[48];
        int length = snprintf(needle, sizeof(needle), "{\"sequence\":%u,", sequence);
        String8 rest = {bytes.pointer + cursor, bytes.length - cursor};
        u64 found = length > 0 ? string_first_sequence(rest, (String8){(char8*)needle, (u64)length}) : BUSTER_STRING_NO_MATCH;
        ok = found != BUSTER_STRING_NO_MATCH;
        if (ok) cursor += found + (u64)length;
    }
    for (u64 index = 0; ok && index + 12 < bytes.length; index += 1)
        observations += !memcmp(bytes.pointer + index, "{\"sequence\":", 12) ? 1u : 0u;
    ok = ok && observations == BENCH_SERVICE_ZEN5_SLOTS;
    *complete = ok && string_ends_with_sequence(bytes, complete_tail);
    ok = ok && (*complete || string_ends_with_sequence(bytes, invalid_tail));
    return ok;
}

/* The three fixed captures run in one candidate stage (zen5-captures) in
 * contract order; the trusted driver writes every spec and header first and
 * verifies each record, and the frozen binaries, after the stage exits. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_captures(Arena* arena, BenchServiceZen5* recipe)
{
    int captures = bench_service_zen5_result_directory(recipe, "zen5/captures");
    int logs = bench_service_zen5_result_directory(recipe, "zen5/logs");
    char root[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, plan[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    char output_paths[BENCH_SERVICE_ZEN5_CAPTURES][BENCH_SERVICE_RECIPE_PATH_CAP] = {{0}};
    char children_paths[BENCH_SERVICE_ZEN5_CAPTURES][BENCH_SERVICE_RECIPE_PATH_CAP] = {{0}};
    String8 headers[BENCH_SERVICE_ZEN5_CAPTURES] = {{0}};
    bool ok = captures >= 0 && logs >= 0 && bench_service_zen5_staging(recipe, "captures", root) &&
              bench_service_zen5_path(plan, recipe->zen5, "plan.json");
    for (u32 capture = 0; ok && capture < BENCH_SERVICE_ZEN5_CAPTURES; capture += 1)
    {
        char const* kind = bench_service_zen5_capture_names[capture];
        char staging[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, spec_path[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
        char header_path[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, object[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
        char name[96] = {0};
        char paths[2][BENCH_SERVICE_RECIPE_PATH_CAP] = {{0}};
        snprintf(name, sizeof(name), "%s.spec", kind);
        ok = bench_service_zen5_mkdir(root, kind, 02770, staging) && bench_service_zen5_path(spec_path, recipe->zen5, name) &&
             snprintf(name, sizeof(name), "%s.header", kind) > 0 &&
             bench_service_zen5_path(header_path, recipe->zen5, name) &&
             snprintf(name, sizeof(name), "%s.json", kind) > 0 &&
             bench_service_zen5_path(output_paths[capture], root, name) &&
             bench_service_zen5_path(object, staging, "output.o") &&
             bench_service_zen5_path(children_paths[capture], staging, "children.log");
        if (ok && capture == 0)
        {
            memcpy(paths[0], recipe->builds[0].frozen, sizeof(paths[0]));
            memcpy(paths[1], recipe->immutable_path1, sizeof(paths[1]));
        }
        else if (ok)
        {
            char directory[BENCH_SERVICE_RECIPE_PATH_CAP];
            for (u32 path = 0; ok && path < 2; path += 1)
            {
                snprintf(name, sizeof(name), "path%u", path);
                ok = bench_service_zen5_mkdir(staging, name, 02770, directory) &&
                     bench_service_zen5_path(paths[path], directory, "ide");
            }
        }
        headers[capture] = ok ? bench_service_zen5_header(arena, recipe, capture,
                                                          (char const (*)[BENCH_SERVICE_RECIPE_PATH_CAP])paths) : (String8){0};
        BenchServiceZen5Build const* a = bench_service_zen5_control(recipe, capture, 0);
        BenchServiceZen5Build const* b = bench_service_zen5_control(recipe, capture, 1);
        String8 spec = string_format(arena,
            S8("BQ-ZEN5-CAPTURE-V1\nkind={S8}\nplan={S8}\nplan-sha256={S8}\nheader={S8}\ndirectory={S8}\ninput={S8}\n"
               "output={S8}\ngap-ns={u64}\nexpected-output={S8}\npath0={S8}\npath1={S8}\nchildren-log={S8}\n"),
            string_from_pointer(kind), string_from_pointer(plan), string_from_pointer(recipe->plan_sha256),
            string_from_pointer(header_path), string_from_pointer(recipe->source), string_from_pointer(recipe->workload),
            string_from_pointer(object), recipe->gap_ns, string_from_pointer(recipe->expected_output),
            string_from_pointer(paths[0]), string_from_pointer(paths[1]), string_from_pointer(children_paths[capture]));
        if (capture)
            spec = string_format(arena, S8("{S8}binary-a={S8}\nbinary-b={S8}\nbinary-a-sha256={S8}\nbinary-b-sha256={S8}\n"),
                                 spec, string_from_pointer(a->frozen), string_from_pointer(b->frozen),
                                 string_from_pointer(a->sha256), string_from_pointer(b->sha256));
        else
            spec = string_format(arena, S8("{S8}path0-sha256={S8}\npath1-sha256={S8}\n"), spec,
                                 string_from_pointer(recipe->builds[0].sha256), string_from_pointer(recipe->builds[0].sha256));
        ok = ok && headers[capture].length > 0 && bench_service_zen5_write_new(header_path, headers[capture], 0444) &&
             bench_service_zen5_write_new(spec_path, spec, 0444);
    }
    if (!ok && !recipe->reason[0]) snprintf(recipe->reason, sizeof(recipe->reason), "capture specs could not be written");
    int status = -1;
    if (ok && bench_service_zen5_test_tamper_frozen == 3) (void)bench_service_zen5_test_tamper(recipe->immutable_path1);
    ok = ok && bench_service_zen5_stage(recipe, BQ_ZEN5_STAGE_CAPTURES, &status) && status == 0;
    if (!ok && !recipe->reason[0]) snprintf(recipe->reason, sizeof(recipe->reason), "captures were refused or failed");
    if (ok && bench_service_zen5_test_tamper_frozen == 2) (void)bench_service_zen5_test_tamper(recipe->immutable_path1);
    for (u32 capture = 0; ok && capture < BENCH_SERVICE_ZEN5_CAPTURES; capture += 1)
    {
        /* The trusted header must be the capture's verbatim prefix, and the
         * frozen binaries must be the ones the plan names. */
        char const* kind = bench_service_zen5_capture_names[capture];
        char name[96] = {0};
        String8 bytes = {0};
        char when[96];
        snprintf(when, sizeof(when), "after capture %s", kind);
        ok = bench_service_zen5_read(arena, output_paths[capture], BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, &bytes) &&
             bench_service_zen5_capture_check(bytes, headers[capture], &recipe->capture_complete[capture]);
        if (!ok && !recipe->reason[0]) snprintf(recipe->reason, sizeof(recipe->reason), "capture %s record is malformed",
                                                kind);
        ok = ok && bench_service_zen5_frozen_verify(recipe, when);
        snprintf(name, sizeof(name), "%s.json", kind);
        ok = ok && bench_service_zen5_publish(captures, name, bytes);
        if (ok)
        {
            Sha256 hash;
            sha256_init(&hash);
            sha256_add(&hash, bytes.pointer, bytes.length);
            sha256_finish_hex(&hash, (char8*)recipe->capture_sha256[capture]);
            snprintf(name, sizeof(name), "capture-%s.children.log", kind);
            ok = bench_service_zen5_publish_copy(arena, children_paths[capture], logs, name, NULL);
        }
        if (!ok && !recipe->reason[0]) snprintf(recipe->reason, sizeof(recipe->reason), "capture %s did not verify", kind);
    }
    /* Invalid captures stay published as evidence, but the attempt fails. */
    for (u32 capture = 0; ok && capture < BENCH_SERVICE_ZEN5_CAPTURES; capture += 1)
    {
        ok = recipe->capture_complete[capture];
        if (!ok) snprintf(recipe->reason, sizeof(recipe->reason), "capture %s has invalid observations",
                          bench_service_zen5_capture_names[capture]);
    }
    if (captures >= 0 && close(captures) != 0) ok = false;
    if (logs >= 0 && close(logs) != 0) ok = false;
    return ok;
}

typedef struct BenchServiceZen5BundlePath BenchServiceZen5BundlePath;
struct BenchServiceZen5BundlePath
{
    char path[BENCH_SERVICE_RECIPE_BUNDLE_PATH_CAP + 1];
};

/* BQ-BUNDLE-V1 over every non-control result file (bench_service_recipe
 * bundle_index format), walked with an explicit bounded stack. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_bundle(Arena* arena, BenchServiceZen5* recipe, char digest[SHA256_HEX_CAPACITY])
{
    BenchServiceRecipeBundleEntry* entries = arena_allocate(arena, BenchServiceRecipeBundleEntry,
                                                            BENCH_SERVICE_RECIPE_BUNDLE_ENTRY_CAP);
    BenchServiceZen5BundlePath* pending = arena_allocate(arena, BenchServiceZen5BundlePath, 64);
    u32 pending_count = 1, file_count = 0;
    u64 total = 0;
    pending[0].path[0] = 0;
    bool ok = bench_service_recipe_sync_tree(arena, recipe->result_directory);
    while (ok && pending_count)
    {
        pending_count -= 1;
        char relative[BENCH_SERVICE_RECIPE_BUNDLE_PATH_CAP + 1];
        memcpy(relative, pending[pending_count].path, sizeof(relative));
        int directory = relative[0] ? openat(recipe->result_directory, relative, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) :
                                      bench_service_recipe_reopen_directory(recipe->result_directory);
        DIR* stream = directory >= 0 ? fdopendir(directory) : NULL;
        if (!stream && directory >= 0) close(directory);
        ok = stream != NULL;
        struct dirent* entry = NULL;
        while (ok && (errno = 0, entry = readdir(stream)) != NULL)
        {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            char path[BENCH_SERVICE_RECIPE_BUNDLE_PATH_CAP + 1];
            int length = snprintf(path, sizeof(path), relative[0] ? "%s/%s" : "%s%s", relative, entry->d_name);
            struct stat info = {0};
            ok = length > 0 && (u32)length <= BENCH_SERVICE_RECIPE_BUNDLE_PATH_CAP &&
                 bench_service_recipe_bundle_path(string_from_pointer(path)) &&
                 fstatat(dirfd(stream), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                 (info.st_uid == 0 || info.st_uid == geteuid()) && (info.st_mode & 022) == 0;
            bool control = !strcmp(path, BENCH_SERVICE_ZEN5_NAME ".manifest") ||
                           !strcmp(path, BENCH_SERVICE_ZEN5_NAME ".bundle") ||
                           !strcmp(path, BENCH_SERVICE_ZEN5_NAME ".outcome");
            if (ok && S_ISDIR(info.st_mode))
            {
                ok = pending_count < 64;
                if (ok) memcpy(pending[pending_count++].path, path, (size_t)length + 1);
            }
            else if (ok && S_ISREG(info.st_mode) && !control)
            {
                u64 size = 0;
                ok = file_count < BENCH_SERVICE_RECIPE_BUNDLE_ENTRY_CAP &&
                     bench_service_recipe_bundle_digest(dirfd(stream), entry->d_name, &size, entries[file_count].digest) &&
                     size <= BENCH_SERVICE_RECIPE_BUNDLE_TOTAL_CAP - total;
                if (ok)
                {
                    entries[file_count].path = string_duplicate_arena(arena, string_from_pointer(path), true);
                    entries[file_count].size = size;
                    total += size;
                    file_count += 1;
                }
            }
            else if (ok) ok = S_ISREG(info.st_mode);
        }
        if (errno) ok = false;
        if (stream) closedir(stream);
    }
    for (u32 index = 1; ok && index < file_count; index += 1)
    {
        BenchServiceRecipeBundleEntry value = entries[index];
        u32 position = index;
        while (position && bench_service_recipe_bundle_compare(&value, entries + position - 1) < 0)
        {
            entries[position] = entries[position - 1];
            position -= 1;
        }
        entries[position] = value;
    }
    String8List lines = {0};
    string8_list_push(arena, &lines, string_format(arena, S8("BQ-BUNDLE-V1\nentries={u32}\nbytes={u64}\n"), file_count, total));
    for (u32 index = 0; ok && index < file_count; index += 1)
        string8_list_push(arena, &lines, string_format(arena, S8("{S8} {u64} {S8}\n"),
                                                       string_from_pointer(entries[index].digest), entries[index].size,
                                                       entries[index].path));
    String8 body = ok ? string_join_arena(arena, string8_list_to_slice(arena, lines), false) : (String8){0};
    ok = ok && body.length <= BENCH_SERVICE_RECIPE_BUNDLE_CAP &&
         bench_service_zen5_publish(recipe->result_directory, BENCH_SERVICE_ZEN5_NAME ".bundle", body);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, body.pointer, body.length);
        sha256_finish_hex(&hash, (char8*)digest);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL char const* bench_service_zen5_capture_status(BenchServiceZen5 const* recipe, u32 capture)
{
    char const* status = !recipe->capture_sha256[capture][0] ? "missing" :
                         recipe->capture_complete[capture] ? "complete" : "invalid";
    return status;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_finish(Arena* arena, BenchServiceZen5* recipe, bool succeeded)
{
    char bundle[SHA256_HEX_CAPACITY] = {0};
    bool ok = bench_service_zen5_bundle(arena, recipe, bundle);
    String8 manifest = string_format(arena,
        S8("schema=1\nrecipe=" BENCH_SERVICE_ZEN5_NAME "\nstatus={S8}\nstage={S8}\nprocess-result={S8}\nreason={S8}\n"
           "job-id={S8}\nattempt-token={S8}\nworkspace-root={S8}\nresult-root={S8}\nbase-revision={S8}\n"
           "candidate-revision={S8}\nsource-tree={S8}\nsource-identity-sha256={S8}\nprofile-sha256={S8}\n"
           "budget-seconds={u64}\nelapsed-ns={u64}\nplan-sha256={S8}\nplan-frozen-monotonic-ns={u64}\n"
           "oracle-output-sha256={S8}\noracle-consistent={S8}\npmu-status={S8}\nhost-qualification-sha256={S8}\n"
           "immutable-capture-sha256={S8}\nsame-root-rebuild-capture-sha256={S8}\ncross-root-capture-sha256={S8}\n"
           "immutable-capture-status={S8}\nsame-root-rebuild-capture-status={S8}\ncross-root-capture-status={S8}\n"
           "trusted-builds=5\npairs=360\ntimed-children=720\nab-authorized=false\naa-decision=not-evaluated\n"
           "bundle-sha256={S8}\n"),
        string_from_pointer(succeeded ? "succeeded" : "failed"), string_from_pointer(recipe->stage),
        string_from_pointer(succeeded ? "success" : "failed"), string_from_pointer(recipe->reason),
        recipe->job_id, recipe->attempt_token, recipe->workspace_root, recipe->result_root, recipe->revision,
        recipe->revision, string_from_pointer(recipe->tree), string_from_pointer(recipe->source_identity),
        string_from_pointer(recipe->profile_sha256), recipe->budget_seconds,
        bench_service_zen5_now() - recipe->started_ns, string_from_pointer(recipe->plan_sha256), recipe->plan_frozen_ns,
        string_from_pointer(recipe->expected_output), string_from_pointer(recipe->oracle_consistent ? "true" : "false"),
        string_from_pointer(recipe->pmu_status[0] ? recipe->pmu_status : "missing"),
        string_from_pointer(recipe->host_qualification), string_from_pointer(recipe->capture_sha256[0]),
        string_from_pointer(recipe->capture_sha256[1]), string_from_pointer(recipe->capture_sha256[2]),
        string_from_pointer(bench_service_zen5_capture_status(recipe, 0)),
        string_from_pointer(bench_service_zen5_capture_status(recipe, 1)),
        string_from_pointer(bench_service_zen5_capture_status(recipe, 2)), string_from_pointer(bundle));
    ok = ok && bench_service_zen5_publish(recipe->result_directory, BENCH_SERVICE_ZEN5_NAME ".manifest", manifest);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_setting(String8 override, char const* production, char* output)
{
    int length = override.length ? snprintf(output, BENCH_SERVICE_RECIPE_PATH_CAP, "%.*s", (int)override.length,
                                            override.pointer) :
                                   snprintf(output, BENCH_SERVICE_RECIPE_PATH_CAP, "%s", production);
    bool ok = length > 0 && (u32)length < BENCH_SERVICE_RECIPE_PATH_CAP;
    return ok;
}

BUSTER_GLOBAL_LOCAL ProcessResult bench_service_zen5_run(Arena* arena, SliceString8 arguments)
{
    BenchServiceZen5* recipe = arena_allocate(arena, BenchServiceZen5, 1);
    *recipe = (BenchServiceZen5){.result_directory = -1, .logs_directory = -1, .stage = "arguments"};
    bool valid = arguments.length == BENCH_SERVICE_RECIPE_ARGUMENT_COUNT;
    String8 expected_result = {0};
    if (valid)
    {
        recipe->job_id = arguments.pointer[0];
        recipe->attempt_token = arguments.pointer[1];
        recipe->workspace_root = arguments.pointer[2];
        recipe->revision = arguments.pointer[3];
        recipe->result_root = arguments.pointer[5];
        expected_result = path_join(arena, recipe->workspace_root,
                                    string_format(arena, S8("results/job-{S8}-attempt-{S8}"), recipe->job_id,
                                                  recipe->attempt_token));
        /* One immutable source: the request names it twice. */
        valid = bench_service_recipe_decimal(recipe->job_id) && bench_service_recipe_decimal(recipe->attempt_token) &&
                bench_service_recipe_path(recipe->workspace_root) && bench_service_recipe_revision(recipe->revision) &&
                recipe->revision.length == 40 && string_equal(recipe->revision, arguments.pointer[4]) &&
                bench_service_recipe_path(recipe->result_root) && string_equal(recipe->result_root, expected_result) &&
                !memchr(recipe->workspace_root.pointer, '"', (size_t)recipe->workspace_root.length);
    }
    char attempt[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    valid = valid && snprintf(attempt, sizeof(attempt), "%.*s/job-%.*s-attempt-%.*s", (int)recipe->workspace_root.length,
                              recipe->workspace_root.pointer, (int)recipe->job_id.length, recipe->job_id.pointer,
                              (int)recipe->attempt_token.length, recipe->attempt_token.pointer) > 0 &&
            snprintf(recipe->attempt, sizeof(recipe->attempt), "%s", attempt) > 0 &&
            bench_service_zen5_path(recipe->source, attempt, BQ_ZEN5_STAGE_SOURCE) &&
            bench_service_zen5_profile_value("workload", recipe->workload, sizeof(recipe->workload)) &&
            bench_service_zen5_profile_u64("budget-seconds", &recipe->budget_seconds) &&
            bench_service_zen5_profile_u64("timing-reserve-seconds", &recipe->reserve_ns) &&
            bench_service_zen5_profile_u64("interblock-gap-ns", &recipe->gap_ns) &&
            bench_service_zen5_profile_u64("cpu", &recipe->cpu) &&
            /* The broker's stage argv fixes these; the profile must agree. */
            !strcmp(recipe->workload, BQ_ZEN5_STAGE_WORKLOAD) && recipe->cpu == BQ_ZEN5_STAGE_CPU_NUMBER &&
            bench_service_zen5_setting(bench_service_recipe_driver_override, BENCH_SERVICE_RECIPE_DRIVER, recipe->driver) &&
            bench_service_zen5_setting(bench_service_zen5_python_override, BENCH_SERVICE_ZEN5_PYTHON, recipe->python) &&
            bench_service_zen5_setting(bench_service_zen5_ninja_override, BENCH_SERVICE_ZEN5_NINJA, recipe->ninja) &&
            bench_service_zen5_setting(bench_service_zen5_self_override, BENCH_SERVICE_RECIPE_DRIVER, recipe->self);
    /* Build ids and roots come from the stage contract the broker enforces. */
    static char const* const build_ids[BENCH_SERVICE_ZEN5_BUILDS] = {BQ_ZEN5_STAGE_BUILD_IDS};
    static char const* const build_roots[BENCH_SERVICE_ZEN5_BUILDS] = {BQ_ZEN5_STAGE_BUILD_ROOTS};
    for (u32 index = 0; index < BENCH_SERVICE_ZEN5_BUILDS; index += 1)
    {
        recipe->builds[index].id = build_ids[index];
        recipe->builds[index].root_name = build_roots[index] + sizeof("zen5/builds/") - 1;
    }
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (!valid)
    {
        string_print(S8("error: invalid fixed zen5-calibration-v1 recipe identity or path\n"));
    }
    else
    {
        if (bench_service_zen5_test_gap_ns) recipe->gap_ns = bench_service_zen5_test_gap_ns;
        if (bench_service_zen5_test_reserve_seconds) recipe->reserve_ns = bench_service_zen5_test_reserve_seconds;
        if (bench_service_zen5_test_cpu_set) recipe->cpu = bench_service_zen5_test_cpu;
        if (bench_service_zen5_test_budget_seconds) recipe->budget_seconds = bench_service_zen5_test_budget_seconds;
        recipe->reserve_ns *= 1000000000ull;
        recipe->started_ns = bench_service_zen5_now();
        recipe->deadline_ns = recipe->started_ns + recipe->budget_seconds * 1000000000ull;
        recipe->result_directory = bench_service_recipe_open_directory(recipe->result_root);
        char const* subdirectories[] = {"zen5", "zen5/logs", "zen5/builds", "zen5/pmu", "zen5/captures"};
        bool ok = recipe->result_directory >= 0 && bench_service_recipe_private_directory(recipe->result_directory, true);
        for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(subdirectories); index += 1)
            ok = mkdirat(recipe->result_directory, subdirectories[index], 0700) == 0;
        ok = ok && fsync(recipe->result_directory) == 0;
        recipe->logs_directory = ok ? bench_service_zen5_result_directory(recipe, "zen5/logs") : -1;
        String8 prepare = string_format(arena,
            S8("schema=1\nrecipe=" BENCH_SERVICE_ZEN5_NAME "\nstatus=running\nstage=prepare\njob-id={S8}\n"
               "attempt-token={S8}\nrevision={S8}\nbudget-seconds={u64}\nab-authorized=false\n"),
            recipe->job_id, recipe->attempt_token, recipe->revision, recipe->budget_seconds);
        bool recorded = ok && recipe->logs_directory >= 0 &&
                        bench_service_zen5_publish(recipe->result_directory, BENCH_SERVICE_ZEN5_NAME ".prepare.manifest",
                                                   prepare);
        if (!recorded)
        {
            string_print(S8("error: zen5-calibration-v1 could not establish its durable result tree\n"));
        }
        else
        {
            /* Phase order is fixed; the first failure stops every later phase
             * and publishes the evidence already present. */
            char const* phases[] = {"source", "builds", "oracle", "pmu", "plan", "captures"};
            ok = bench_service_zen5_mkdir(attempt, "zen5", 02750, recipe->zen5);
            if (!ok) snprintf(recipe->reason, sizeof(recipe->reason), "attempt zen5 directory could not be created");
            for (u32 phase = 0; ok && phase < BUSTER_ARRAY_LENGTH(phases); phase += 1)
            {
                recipe->stage = phases[phase];
                switch (phase)
                {
                case 0: ok = bench_service_zen5_identity_phase(arena, recipe); break;
                case 1: ok = bench_service_zen5_builds(arena, recipe); break;
                case 2: ok = bench_service_zen5_oracle(arena, recipe); break;
                case 3: ok = bench_service_zen5_pmu(arena, recipe); break;
                case 4: ok = bench_service_zen5_plan_freeze(arena, recipe); break;
                default: ok = bench_service_zen5_captures(arena, recipe); break;
                }
            }
            if (ok) recipe->stage = "complete";
            bool finished = bench_service_zen5_finish(arena, recipe, ok);
            result = ok && finished ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
            if (!ok) string_print(S8("error: zen5-calibration-v1 stopped at {S8}: {S8}\n"), string_from_pointer(recipe->stage),
                                  string_from_pointer(recipe->reason));
        }
    }
    if (recipe->logs_directory >= 0) close(recipe->logs_directory);
    if (recipe->result_directory >= 0) close(recipe->result_directory);
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL ProcessResult bench_service_zen5_recipe_add(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX
    result = bench_service_zen5_run(arena, arguments);
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    string_print(S8("error: zen5-calibration-v1 is Linux-only\n"));
#endif
    return result;
}

/* Stage program: one or more SPEC OUTPUT pairs, run in order (the zen5
 * oracle stage passes one pair, the captures stage three); the first failure
 * stops the rest. */
BUSTER_GLOBAL_LOCAL ProcessResult bench_service_zen5_capture_add(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX
    bool valid = arguments.length >= 2 && arguments.length <= 2u * BENCH_SERVICE_ZEN5_CAPTURES && arguments.length % 2 == 0;
    for (u64 index = 0; valid && index < arguments.length; index += 1)
        valid = bench_service_recipe_path(arguments.pointer[index]) && arguments.pointer[index].length < BENCH_SERVICE_RECIPE_PATH_CAP;
    result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    for (u64 pair = 0; valid && result == PROCESS_RESULT_SUCCESS && pair < arguments.length; pair += 2)
    {
        char spec[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, output[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
        snprintf(spec, sizeof(spec), "%.*s", (int)arguments.pointer[pair].length, arguments.pointer[pair].pointer);
        snprintf(output, sizeof(output), "%.*s", (int)arguments.pointer[pair + 1].length, arguments.pointer[pair + 1].pointer);
        result = bench_service_zen5_capture_run(arena, spec, output);
    }
    if (!valid) string_print(S8("error: bench_service_zen5_capture requires one to three SPEC OUTPUT absolute path pairs\n"));
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    string_print(S8("error: bench_service_zen5_capture is Linux-only\n"));
#endif
    return result;
}
