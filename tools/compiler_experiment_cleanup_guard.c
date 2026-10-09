#ifndef BUSTER_COMPILER_EXPERIMENT_CLEANUP_GUARD_INCLUDED
#define BUSTER_COMPILER_EXPERIMENT_CLEANUP_GUARD_INCLUDED
// Durable admission only; no service, dispatch, retry or UNKNOWN deletion.
// Physical fixed paths cannot be overridden by environment or command data.
#define BUSTER_EXPERIMENT_CLEANUP_UNKNOWN "/tmp/buster-9700x-cleanup-unknown-v1"
#define BUSTER_EXPERIMENT_CLEANUP_ACTIVE "/tmp/buster-9700x-cleanup-active-v1"
#define BUSTER_EXPERIMENT_CLEANUP_RECORD_LIMIT 4096u

typedef struct CompilerExperimentCleanupLease CompilerExperimentCleanupLease;
struct CompilerExperimentCleanupLease
{
    String8 record;
    u64 owner_pid;
    bool enabled;
    bool owned;
};

#if BUSTER_LINUX && !BUSTER_ANDROID
#include <sys/stat.h>

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_read(char const* path, char8* bytes, u64 capacity, u64* length)
{
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    bool result = descriptor >= 0;
    u64 used = 0;
    bool eof = false;
    while (result && !eof)
    {
        ssize_t got = read(descriptor, bytes + used, (size_t)(capacity - used));
        if (got > 0)
        {
            used += (u64)got;
            result = used < capacity;
        }
        else if (!got) eof = true;
        else result = errno == EINTR;
    }
    if (descriptor >= 0) result = close(descriptor) == 0 && result;
    if (result && eof) *length = used;
    return result && eof;
}

// Foreign hosted diagnostics never consult or create the physical markers.
// An unavailable/truncated CPU observation refuses a native physical boundary.
BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_physical(bool* physical)
{
    char8 bytes[262145];
    u64 length = 0;
    bool result = compiler_experiment_cleanup_read("/proc/cpuinfo", bytes, sizeof(bytes), &length) && length;
    if (result)
    {
        *physical = string_contains((String8){bytes, length}, S8("AMD Ryzen 7 9700X"));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_missing(char const* path)
{
    struct stat value = {0};
    bool result = lstat(path, &value) < 0 && errno == ENOENT;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_decimal(String8 value, u64* parsed)
{
    bool valid = value.length && value.length <= 20 && value.pointer && value.pointer[0] >= '1' && value.pointer[0] <= '9';
    u64 number = 0;
    for (u64 i = 0; valid && i < value.length; i += 1)
    {
        u64 digit = (u64)(value.pointer[i] - '0');
        valid = digit <= 9 && number <= (UINT64_MAX - digit) / 10;
        if (valid) number = number * 10 + digit;
    }
    if (valid && parsed) *parsed = number;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_hex(String8 value, u64 length)
{
    bool result = value.length == length && value.pointer;
    for (u64 i = 0; result && i < value.length; i += 1)
        result = (value.pointer[i] >= '0' && value.pointer[i] <= '9') || (value.pointer[i] >= 'a' && value.pointer[i] <= 'f');
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_experiment_cleanup_environment(char const* name)
{
    char const* value = getenv(name);
    u64 length = value ? (u64)strlen(value) : 0;
    return length && length <= 256 ? (String8){(char8*)value, length} : (String8){0};
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_boot(char8* bytes, u64* length)
{
    bool result = compiler_experiment_cleanup_read("/proc/sys/kernel/random/boot_id", bytes, 64, length) && *length == 37 &&
        bytes[36] == '\n';
    for (u64 i = 0; result && i < 36; i += 1)
        result = (i == 8 || i == 13 || i == 18 || i == 23) ? bytes[i] == '-' :
            ((bytes[i] >= '0' && bytes[i] <= '9') || (bytes[i] >= 'a' && bytes[i] <= 'f'));
    if (result) *length = 36;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_process(Arena* arena, u64 pid, u64* parent, u64* started)
{
    char8 bytes[8192];
    u64 length = 0;
    String8 path = string_format_z(arena, S8("/proc/{u64}/stat"), pid);
    bool result = pid > 1 && pid <= 2147483647 && compiler_experiment_cleanup_read((char const*)path.pointer, bytes, sizeof(bytes), &length);
    u64 close_at = 0;
    for (u64 i = 0; result && i < length; i += 1) if (bytes[i] == ')') close_at = i;
    result = result && close_at && close_at + 4 < length && bytes[close_at + 1] == ' ' && bytes[close_at + 3] == ' ';
    u64 at = close_at + 4, found_parent = 0, found_started = 0;
    for (u64 field = 4; result && field <= 22; field += 1)
    {
        u64 begin = at;
        while (at < length && bytes[at] != ' ' && bytes[at] != '\n') at += 1;
        result = at > begin && at < length;
        if (result && (field == 4 || field == 22))
        {
            String8 value = {bytes + begin, at - begin};
            u64 parsed = 0;
            bool zero_parent = field == 4 && string_equal(value, S8("0"));
            result = zero_parent || compiler_experiment_cleanup_decimal(value, &parsed);
            if (field == 4) found_parent = parsed; else found_started = parsed;
        }
        at += 1;
    }
    if (result) { *parent = found_parent; *started = found_started; }
    return result && found_started;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_fields(String8 record, String8* values)
{
    String8 keys[] = {S8("schema"), S8("owner_pid"), S8("owner_start_ticks"), S8("boot_id"),
        S8("request_run_id"), S8("executor_run_id"), S8("executor_attempt"), S8("request_head"),
        S8("repository"), S8("job"), S8("policy_revision")};
    bool valid = record.pointer && record.length && record.length <= BUSTER_EXPERIMENT_CLEANUP_RECORD_LIMIT;
    u64 at = 0;
    for (u64 field = 0; valid && field < BUSTER_ARRAY_LENGTH(keys); field += 1)
    {
        u64 key_at = at;
        while (at < record.length && record.pointer[at] != '\t' && record.pointer[at] != '\n') at += 1;
        valid = at < record.length && record.pointer[at] == '\t' &&
            string_equal((String8){record.pointer + key_at, at - key_at}, keys[field]);
        u64 value_at = ++at;
        while (valid && at < record.length && record.pointer[at] != '\n')
        {
            valid = record.pointer[at] >= 32 && record.pointer[at] <= 126 && record.pointer[at] != '\t';
            at += 1;
        }
        valid = valid && at > value_at && at < record.length && record.pointer[at] == '\n';
        if (valid) values[field] = (String8){record.pointer + value_at, at - value_at};
        at += 1;
    }
    return valid && at == record.length;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_context(String8* values)
{
    String8 request = compiler_experiment_cleanup_environment("BQ_REQUEST_RUN_ID");
    String8 executor = compiler_experiment_cleanup_environment("GITHUB_RUN_ID");
    String8 declared_executor = compiler_experiment_cleanup_environment("BQ_RUN_ID");
    String8 attempt = compiler_experiment_cleanup_environment("GITHUB_RUN_ATTEMPT");
    String8 head = compiler_experiment_cleanup_environment("BQ_HEAD_COMMIT");
    String8 repository = compiler_experiment_cleanup_environment("GITHUB_REPOSITORY");
    String8 job = compiler_experiment_cleanup_environment("GITHUB_JOB");
    String8 policy = compiler_experiment_cleanup_environment("GITHUB_SHA");
    bool known_job = string_equal(job, S8("compare")) || string_equal(job, S8("compare-pull")) ||
        string_equal(job, S8("sampling")) || string_equal(job, S8("preparation")) || string_equal(job, S8("utility"));
    bool result = compiler_experiment_cleanup_decimal(request, 0) && compiler_experiment_cleanup_decimal(executor, 0) &&
        compiler_experiment_cleanup_decimal(attempt, 0) && string_equal(executor, declared_executor) &&
        compiler_experiment_cleanup_hex(head, 40) && compiler_experiment_cleanup_hex(policy, 40) &&
        string_equal(repository, S8("buster14a/buster")) && known_job;
    if (result)
    {
        values[4] = request; values[5] = executor; values[6] = attempt; values[7] = head;
        values[8] = repository; values[9] = job; values[10] = policy;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_record_read(Arena* arena, char const* active, String8* record)
{
    struct stat directory = {0}, file = {0};
    bool result = lstat(active, &directory) == 0 && S_ISDIR(directory.st_mode) &&
        (directory.st_mode & 0777) == 0700 && directory.st_uid == getuid();
    String8 path = string_format_z(arena, S8("{S8}/owner.tsv"), ((String8){(char8*)active, (u64)strlen(active)}));
    result = result && lstat((char const*)path.pointer, &file) == 0 && S_ISREG(file.st_mode) &&
        (file.st_mode & 0777) == 0600 && file.st_uid == getuid() && file.st_size > 0 &&
        (u64)file.st_size < BUSTER_EXPERIMENT_CLEANUP_RECORD_LIMIT;
    char8* bytes = result ? arena_allocate(arena, char8, BUSTER_EXPERIMENT_CLEANUP_RECORD_LIMIT) : 0;
    u64 length = 0;
    result = result && compiler_experiment_cleanup_read((char const*)path.pointer, bytes, BUSTER_EXPERIMENT_CLEANUP_RECORD_LIMIT, &length) &&
        length == (u64)file.st_size;
    if (result) *record = (String8){bytes, length};
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_record_matches(Arena* arena, String8 record, bool owner_only)
{
    String8 values[11] = {0}, expected[11] = {0};
    u64 owner = 0, started = 0, parent = 0, observed_started = 0;
    char8 boot[64]; u64 boot_length = 0;
    bool valid = compiler_experiment_cleanup_fields(record, values) &&
        string_equal(values[0], S8("buster-9700x-native-active-v1")) &&
        compiler_experiment_cleanup_decimal(values[1], &owner) && owner > 1 &&
        compiler_experiment_cleanup_decimal(values[2], &started) &&
        compiler_experiment_cleanup_boot(boot, &boot_length) && string_equal(values[3], (String8){boot, boot_length}) &&
        compiler_experiment_cleanup_context(expected);
    for (u64 i = 4; valid && i < 11; i += 1) valid = string_equal(values[i], expected[i]);
    valid = valid && compiler_experiment_cleanup_process(arena, owner, &parent, &observed_started) && started == observed_started;
    u64 current = (u64)getpid();
    if (owner_only) valid = valid && current == owner;
    for (u64 depth = 0; valid && current != owner && depth < 256; depth += 1)
    {
        valid = compiler_experiment_cleanup_process(arena, current, &parent, &observed_started) && parent > 1 && parent != current;
        current = parent;
    }
    return valid && current == owner;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard_at(Arena* arena, char const* unknown, char const* active)
{
    if (!compiler_experiment_cleanup_missing(unknown)) return false;
    if (compiler_experiment_cleanup_missing(active)) return true;
    String8 record = {0};
    return compiler_experiment_cleanup_record_read(arena, active, &record) &&
        compiler_experiment_cleanup_record_matches(arena, record, false);
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_write_file(char const* path, String8 record)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    bool valid = descriptor >= 0;
    u64 used = 0;
    while (valid && used < record.length)
    {
        ssize_t wrote = write(descriptor, record.pointer + used, (size_t)(record.length - used));
        if (wrote > 0) used += (u64)wrote; else valid = wrote < 0 && errno == EINTR;
    }
    if (valid) valid = used == record.length && fsync(descriptor) == 0;
    if (descriptor >= 0) valid = close(descriptor) == 0 && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_latch_at(Arena* arena, char const* unknown, String8 reason)
{
    bool created = mkdir(unknown, 0700) == 0;
    if (!created) return !compiler_experiment_cleanup_missing(unknown);
    String8 path = string_format_z(arena, S8("{S8}/reason.tsv"), ((String8){(char8*)unknown, (u64)strlen(unknown)}));
    String8 record = string_format(arena, S8("schema\tbuster-9700x-cleanup-unknown-v1\nstate\tunknown\nowner_pid\t{u64}\nreason\t{S8}\n"),
        (u64)getpid(), reason);
    // The directory itself is the fail-closed admission latch even if export fails.
    bool written = compiler_experiment_cleanup_write_file((char const*)path.pointer, record);
    int directory = open(unknown, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    bool durable = directory >= 0 && fsync(directory) == 0;
    if (directory >= 0) durable = close(directory) == 0 && durable;
    return written && durable;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_begin_at(Arena* arena, char const* unknown, char const* active,
    CompilerExperimentCleanupLease* lease)
{
    bool valid = lease && !lease->enabled && compiler_experiment_cleanup_guard_at(arena, unknown, active);
    if (!valid) return false;
    lease->enabled = true;
    if (!compiler_experiment_cleanup_missing(active))
        return compiler_experiment_cleanup_record_read(arena, active, &lease->record);
    String8 values[11] = {0}; u64 parent = 0, started = 0; char8 boot[64]; u64 boot_length = 0;
    valid = compiler_experiment_cleanup_context(values) &&
        compiler_experiment_cleanup_process(arena, (u64)getpid(), &parent, &started) &&
        compiler_experiment_cleanup_boot(boot, &boot_length);
    if (!valid) return false;
    lease->owner_pid = (u64)getpid();
    lease->record = string_format(arena,
        S8("schema\tbuster-9700x-native-active-v1\nowner_pid\t{u64}\nowner_start_ticks\t{u64}\nboot_id\t{S8}\n"
           "request_run_id\t{S8}\nexecutor_run_id\t{S8}\nexecutor_attempt\t{S8}\nrequest_head\t{S8}\n"
           "repository\t{S8}\njob\t{S8}\npolicy_revision\t{S8}\n"),
        lease->owner_pid, started, ((String8){boot, boot_length}), values[4], values[5], values[6], values[7], values[8], values[9], values[10]);
    valid = mkdir(active, 0700) == 0;
    // A failed/partial publication leaves ACTIVE consumed; there is no rollback.
    lease->owned = valid;
    String8 path = string_format_z(arena, S8("{s}/owner.tsv"), active);
    valid = valid && compiler_experiment_cleanup_write_file((char const*)path.pointer, lease->record);
    int directory = valid ? open(active, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    valid = valid && directory >= 0 && fsync(directory) == 0;
    if (directory >= 0) valid = close(directory) == 0 && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_finish_at(Arena* arena, char const* unknown, char const* active,
    CompilerExperimentCleanupLease* lease, bool quiet)
{
    if (!lease || !lease->enabled) return quiet;
    String8 observed = {0};
    bool valid = quiet && compiler_experiment_cleanup_guard_at(arena, unknown, active) &&
        compiler_experiment_cleanup_record_read(arena, active, &observed) && string_equal(observed, lease->record) &&
        compiler_experiment_cleanup_record_matches(arena, observed, lease->owned);
    if (valid && lease->owned)
    {
        String8 path = string_format_z(arena, S8("{s}/owner.tsv"), active);
        valid = unlink((char const*)path.pointer) == 0 && rmdir(active) == 0;
    }
    if (!valid) compiler_experiment_cleanup_latch_at(arena, unknown, S8("owned-native-cleanup-unproven"));
    return valid;
}
#endif

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_guard(Arena* arena)
{
#if BUSTER_LINUX && !BUSTER_ANDROID
    bool physical = false;
    return compiler_experiment_cleanup_physical(&physical) && (!physical ||
        compiler_experiment_cleanup_guard_at(arena, BUSTER_EXPERIMENT_CLEANUP_UNKNOWN, BUSTER_EXPERIMENT_CLEANUP_ACTIVE));
#else
    BUSTER_UNUSED(arena);
    return true;
#endif
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_begin(Arena* arena, CompilerExperimentCleanupLease* lease)
{
#if BUSTER_LINUX && !BUSTER_ANDROID
    bool physical = false;
    return compiler_experiment_cleanup_physical(&physical) && (!physical ||
        compiler_experiment_cleanup_begin_at(arena, BUSTER_EXPERIMENT_CLEANUP_UNKNOWN, BUSTER_EXPERIMENT_CLEANUP_ACTIVE, lease));
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(lease);
    return true;
#endif
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_finish(Arena* arena, CompilerExperimentCleanupLease* lease, bool quiet)
{
#if BUSTER_LINUX && !BUSTER_ANDROID
    // enabled is captured before launch; a later missing/changed CPU record
    // cannot turn a physical ACTIVE owner into an unguarded hosted owner.
    return !lease || !lease->enabled ? quiet : compiler_experiment_cleanup_finish_at(arena,
        BUSTER_EXPERIMENT_CLEANUP_UNKNOWN, BUSTER_EXPERIMENT_CLEANUP_ACTIVE, lease, quiet);
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(lease);
    return quiet;
#endif
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_cleanup_latch(Arena* arena, String8 reason)
{
#if BUSTER_LINUX && !BUSTER_ANDROID
    bool physical = false;
    return compiler_experiment_cleanup_physical(&physical) && (!physical ||
        compiler_experiment_cleanup_latch_at(arena, BUSTER_EXPERIMENT_CLEANUP_UNKNOWN, reason));
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(reason);
    return true;
#endif
}
#endif
