#include "worker_linux.h"

#ifdef __linux__
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <sys/vfs.h>
#include <sys/wait.h>

#define BQ_WORKER_EXECUTABLE "/usr/local/libexec/buster-bench-service"
#define BQ_SYSTEMD_RUN "/usr/bin/systemd-run"
#define BQ_SYSTEMCTL "/usr/bin/systemctl"
#define BQ_WORKER_COMMAND_MILLISECONDS 5000u
#define BQ_WORKER_STOP_MILLISECONDS 10000u
#define BQ_WORKER_POLL_MILLISECONDS 100u

typedef struct BqSystemdContext
{
    pid_t pid;
    bool starting;
} BqSystemdContext;

BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_worker_cancel_signal;

BUSTER_GLOBAL_LOCAL u64 bq_worker_monotonic_milliseconds(void)
{
    struct timespec now = {0};
    bool ok = clock_gettime(CLOCK_MONOTONIC, &now) == 0 && now.tv_sec >= 0;
    u64 seconds = ok ? (u64)now.tv_sec : UINT64_MAX / 1000;
    u64 result = seconds <= (UINT64_MAX - (u64)now.tv_nsec / 1000000) / 1000 ?
                 seconds * 1000 + (u64)now.tv_nsec / 1000000 : UINT64_MAX;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 bq_worker_deadline(u64 now, u64 milliseconds)
{
    u64 result = milliseconds <= UINT64_MAX - now ? now + milliseconds : UINT64_MAX;
    return result;
}

BUSTER_GLOBAL_LOCAL u32 bq_worker_remaining(u64 deadline)
{
    u64 now = bq_worker_monotonic_milliseconds();
    u64 remaining = now < deadline ? deadline - now : 0;
    u32 result = remaining > UINT32_MAX ? UINT32_MAX : (u32)remaining;
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_sleep_until(u64 deadline)
{
    BqError error = BQ_OK;
    u64 now = bq_worker_monotonic_milliseconds();
    while (error == BQ_OK && now < deadline)
    {
        struct timespec absolute = {(time_t)(deadline / 1000), (long)(deadline % 1000) * 1000 * 1000};
        int result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &absolute, NULL);
        if (result != 0 && result != EINTR) error = BQ_IO;
        now = bq_worker_monotonic_milliseconds();
    }
    return error;
}

BUSTER_GLOBAL_LOCAL void bq_worker_cancel_handler(int signal_number)
{
    (void)signal_number;
    bq_worker_cancel_signal = 1;
}

typedef struct BqWorkerLease { int descriptor; } BqWorkerLease;

BUSTER_GLOBAL_LOCAL bool bq_worker_directory_owner(int fd, bool final_private, bool allow_sticky)
{
    struct stat info;
    bool ok = fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) &&
              (info.st_uid == 0 || info.st_uid == geteuid());
    if (ok && final_private)
        ok = info.st_uid == geteuid() && (info.st_mode & 077) == 0;
    else if (ok && (info.st_mode & 022) != 0)
        ok = allow_sticky && info.st_uid == 0 && (info.st_mode & S_ISVTX) != 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL int bq_worker_open_trusted_directory(String8 path, bool final_private,
                                                           bool allow_sticky_final)
{
    char text[BQ_PATH_CAP + 1];
    int current = bq_string_path(path, text) ? open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    u64 offset = 1;
    while (current >= 0 && offset < path.length)
    {
        bool final = true;
        u64 end = offset;
        while (end < path.length && path.pointer[end] != '/') end += 1;
        if (end < path.length) final = false;
        if (!bq_worker_directory_owner(current, false, true))
        {
            close(current);
            current = -1;
        }
        else
        {
            char name[BQ_PATH_CAP + 1];
            u64 length = end - offset;
            memcpy(name, path.pointer + offset, (size_t)length);
            name[length] = 0;
            int next = openat(current, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            close(current);
            current = next;
            if (current >= 0 && final &&
                !bq_worker_directory_owner(current, final_private, allow_sticky_final))
            {
                close(current);
                current = -1;
            }
        }
        offset = end + 1;
    }
    if (current >= 0 && !bq_worker_directory_owner(current, final_private, allow_sticky_final))
    {
        close(current);
        current = -1;
    }
    return current;
}

BUSTER_GLOBAL_LOCAL int bq_worker_open_lease_parent(char const* path, char leaf[BQ_PATH_CAP + 1])
{
    String8 whole = string_from_pointer(path);
    char validated[BQ_PATH_CAP + 1];
    int parent = -1;
    if (bq_string_path(whole, validated))
    {
        char* slash = strrchr(validated, '/');
        if (slash && slash[1])
        {
            snprintf(leaf, BQ_PATH_CAP + 1, "%s", slash + 1);
            u64 length = (u64)(slash - validated);
            String8 directory = {(char8*)validated, length ? length : 1};
            parent = bq_worker_open_trusted_directory(directory, true, false);
        }
    }
    return parent;
}

/* Same stable-inode/open-description contract consumed by throughput's
 * --lease-fd.  Kept local so bench_service does not compile unrelated
 * throughput qualification/reporting functions into the service. */
BUSTER_GLOBAL_LOCAL int bq_worker_lease_acquire(char const* path, BqWorkerLease* lease)
{
    lease->descriptor = -1;
    errno = 0;
    char leaf[BQ_PATH_CAP + 1];
    int parent = path ? bq_worker_open_lease_parent(path, leaf) : -1;
    int fd = parent >= 0 ? openat(parent, leaf, O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0600) : -1;
    int error = fd < 0 ? (!path || path[0] != '/' ? EINVAL : errno ? errno : EINVAL) : 0;
    if (parent >= 0) close(parent);
    if (!error && fd < 3)
    {
        int duplicate = fcntl(fd, F_DUPFD, 3);
        error = duplicate < 0 ? errno : 0;
        close(fd);
        fd = duplicate;
    }
    struct stat info;
    if (!error && fstat(fd, &info) != 0) error = errno;
    if (!error && (!S_ISREG(info.st_mode) || info.st_nlink != 1)) error = EINVAL;
    if (!error && (info.st_uid != geteuid() || (info.st_mode & 077) != 0)) error = EACCES;
    if (!error && flock(fd, LOCK_EX | LOCK_NB) != 0) error = errno;
    if (!error) lease->descriptor = fd;
    else if (fd >= 0) close(fd);
    return error;
}

BUSTER_GLOBAL_LOCAL int bq_worker_lease_adopt(char const* path, int fd, BqWorkerLease* lease)
{
    lease->descriptor = -1;
    errno = 0;
    int inspection = -1;
    int flags = fd >= 3 ? fcntl(fd, F_GETFD) : -1;
    int error = !path || path[0] != '/' || fd < 3 ? EINVAL : flags < 0 ? errno : 0;
    struct stat inherited, selected;
    if (!error && fstat(fd, &inherited) != 0) error = errno;
    if (!error && (!S_ISREG(inherited.st_mode) || inherited.st_nlink != 1)) error = EINVAL;
    if (!error && (inherited.st_uid != geteuid() || (inherited.st_mode & 077) != 0)) error = EACCES;
    char leaf[BQ_PATH_CAP + 1];
    int parent = !error ? bq_worker_open_lease_parent(path, leaf) : -1;
    if (!error && parent < 0) error = errno ? errno : EINVAL;
    if (!error) inspection = openat(parent, leaf, O_RDWR | O_NOFOLLOW | O_NONBLOCK);
    if (!error && inspection < 0) error = errno;
    if (!error && fstat(inspection, &selected) != 0) error = errno;
    if (!error && (selected.st_dev != inherited.st_dev || selected.st_ino != inherited.st_ino)) error = EINVAL;
    if (!error)
    {
        int probe = flock(inspection, LOCK_SH | LOCK_NB);
        if (probe == 0) error = EINVAL;
        else if (errno != EWOULDBLOCK && errno != EAGAIN) error = errno;
    }
    if (!error && flock(fd, LOCK_EX | LOCK_NB) != 0) error = errno;
    if (!error && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) error = errno;
    if (inspection >= 0) close(inspection);
    if (parent >= 0) close(parent);
    if (!error) lease->descriptor = fd;
    else if (fd >= 0) close(fd);
    return error;
}

BUSTER_GLOBAL_LOCAL void bq_worker_lease_release(BqWorkerLease* lease)
{
    if (lease->descriptor >= 0) close(lease->descriptor);
    lease->descriptor = -1;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_text(String8 input, char* output, u32 capacity)
{
    bool ok = input.length && input.length < capacity && !memchr(input.pointer, 0, (size_t)input.length);
    if (ok)
    {
        memcpy(output, input.pointer, (size_t)input.length);
        output[input.length] = 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_read_regular(char const* path, char* output, u32 capacity)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info;
    bool ok = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1;
    u32 used = 0;
    while (ok && used + 1 < capacity)
    {
        ssize_t count = read(fd, output + used, capacity - used - 1);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count < 0)
        {
            ok = false;
        }
        else if (!count)
        {
            break;
        }
        else
        {
            used += (u32)count;
        }
    }
    if (ok)
    {
        char extra;
        ssize_t count = read(fd, &extra, 1);
        ok = count == 0;
    }
    if (fd >= 0 && close(fd) != 0)
    {
        ok = false;
    }
    while (ok && used && (output[used - 1] == '\n' || output[used - 1] == '\r'))
    {
        used -= 1;
    }
    if (ok)
    {
        output[used] = 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_boot_valid(char const* boot)
{
    bool ok = strlen(boot) == 36;
    for (u32 i = 0; ok && i < 36; i += 1)
    {
        bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        ok = dash ? boot[i] == '-' : (boot[i] >= '0' && boot[i] <= '9') || (boot[i] >= 'a' && boot[i] <= 'f');
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_invocation_valid(char const* invocation)
{
    bool ok = strlen(invocation) == 32;
    for (u32 i = 0; ok && i < 32; i += 1)
        ok = (invocation[i] >= '0' && invocation[i] <= '9') ||
             (invocation[i] >= 'a' && invocation[i] <= 'f');
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_unit_name(char output[BQ_WORKER_UNIT_CAP], u64 id, u64 token)
{
    int count = snprintf(output, BQ_WORKER_UNIT_CAP, "buster-bench-%" PRIu64 "-%" PRIu64 ".scope",
                         (uint64_t)id, (uint64_t)token);
    return count > 0 && count < (int)BQ_WORKER_UNIT_CAP;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_record_name(char output[48], u64 id)
{
    return bq_record_name(output, "worker", id);
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_record_write(BqQueue* queue, BqJob const* job,
                                                    char const* boot, char const* unit)
{
    u8 bytes[232] = {0};
    char name[48];
    bool ok = job && bq_worker_boot_valid(boot) && strlen(unit) < BQ_WORKER_UNIT_CAP &&
              bq_worker_record_name(name, job->id);
    if (ok)
    {
        memcpy(bytes, "BQWORKER00000001", 16);
        bq_put64(bytes + 16, job->id);
        bq_put64(bytes + 24, job->token);
        memcpy(bytes + 32, job->digest, 64);
        memcpy(bytes + 96, boot, strlen(boot));
        memcpy(bytes + 136, unit, strlen(unit));
    }
    BqError error = ok ? bq_record_write(queue, name, bytes, sizeof(bytes), false) : BQ_WORKER_MISMATCH;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_record_read(BqQueue* queue, BqJob const* job,
                                                   char boot[BQ_WORKER_BOOT_CAP],
                                                   char unit[BQ_WORKER_UNIT_CAP])
{
    u8 bytes[232];
    u32 size = 0;
    char name[48];
    BqError error = bq_worker_record_name(name, job->id) ?
                    bq_record_read(queue, name, bytes, sizeof(bytes), &size) : BQ_CORRUPT;
    bool terminated = size == sizeof(bytes) && !bytes[132] && !bytes[231];
    if (error == BQ_OK)
    {
        memcpy(boot, bytes + 96, 37);
        memcpy(unit, bytes + 136, 96);
        bool ok = terminated && !memcmp(bytes, "BQWORKER00000001", 16) &&
                  bq_u64(bytes + 16) == job->id && bq_u64(bytes + 24) == job->token &&
                  !memcmp(bytes + 32, job->digest, 64) && bq_worker_boot_valid(boot) &&
                  strlen(unit) < BQ_WORKER_UNIT_CAP;
        char expected[BQ_WORKER_UNIT_CAP];
        ok = ok && bq_worker_unit_name(expected, job->id, job->token) && !strcmp(expected, unit);
        error = ok ? BQ_OK : BQ_WORKER_MISMATCH;
    }
    else if (error == BQ_NOT_FOUND)
    {
        error = BQ_WORKER_MISMATCH;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_instance_name(char output[48], u64 id)
{
    return bq_record_name(output, "worker-instance", id);
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_instance_write(BqQueue* queue, BqJob const* job,
                                                      BqWorkerObserved const* observed)
{
    u8 bytes[544] = {0};
    char name[48];
    bool ok = job && observed && bq_worker_instance_name(name, job->id) &&
              bq_worker_boot_valid(observed->boot_id) &&
              bq_worker_invocation_valid(observed->invocation_id) &&
              strlen(observed->unit) < BQ_WORKER_UNIT_CAP &&
              strlen(observed->cgroup) < BQ_WORKER_CGROUP_CAP &&
              observed->cgroup_device && observed->cgroup_inode &&
              observed->cgroup_root_device && observed->cgroup_root_inode &&
              observed->cgroup_slice_device && observed->cgroup_slice_inode;
    if (ok)
    {
        memcpy(bytes, "BQINSTANCE000002", 16);
        bq_put64(bytes + 16, job->id);
        bq_put64(bytes + 24, job->token);
        memcpy(bytes + 32, job->digest, 64);
        memcpy(bytes + 96, observed->boot_id, strlen(observed->boot_id));
        memcpy(bytes + 136, observed->unit, strlen(observed->unit));
        memcpy(bytes + 232, observed->invocation_id, strlen(observed->invocation_id));
        bq_put64(bytes + 272, observed->cgroup_device);
        bq_put64(bytes + 280, observed->cgroup_inode);
        memcpy(bytes + 288, observed->cgroup, strlen(observed->cgroup));
        bq_put64(bytes + 480, observed->cgroup_root_device);
        bq_put64(bytes + 488, observed->cgroup_root_inode);
        bq_put64(bytes + 496, observed->cgroup_slice_device);
        bq_put64(bytes + 504, observed->cgroup_slice_inode);
    }
    return ok ? bq_record_write(queue, name, bytes, sizeof(bytes), true) : BQ_WORKER_MISMATCH;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_instance_read(BqQueue* queue, BqJob const* job,
                                                     BqWorkerObserved* identity)
{
    u8 bytes[544];
    u32 size = 0;
    char name[48];
    BqError error = bq_worker_instance_name(name, job->id) ?
                    bq_record_read(queue, name, bytes, sizeof(bytes), &size) : BQ_CORRUPT;
    if (error == BQ_OK)
    {
        bool terminated = size == sizeof(bytes) && !bytes[132] && !bytes[231] &&
                          !bytes[264] && !bytes[479] && !bytes[543];
        *identity = (BqWorkerObserved){0};
        memcpy(identity->boot_id, bytes + 96, 37);
        memcpy(identity->unit, bytes + 136, 96);
        memcpy(identity->invocation_id, bytes + 232, 33);
        identity->cgroup_device = bq_u64(bytes + 272);
        identity->cgroup_inode = bq_u64(bytes + 280);
        memcpy(identity->cgroup, bytes + 288, BQ_WORKER_CGROUP_CAP);
        identity->cgroup_root_device = bq_u64(bytes + 480);
        identity->cgroup_root_inode = bq_u64(bytes + 488);
        identity->cgroup_slice_device = bq_u64(bytes + 496);
        identity->cgroup_slice_inode = bq_u64(bytes + 504);
        bool ok = terminated && !memcmp(bytes, "BQINSTANCE000002", 16) &&
                  bq_u64(bytes + 16) == job->id && bq_u64(bytes + 24) == job->token &&
                  !memcmp(bytes + 32, job->digest, 64) &&
                  bq_worker_boot_valid(identity->boot_id) &&
                  bq_worker_invocation_valid(identity->invocation_id) &&
                  identity->cgroup_device && identity->cgroup_inode &&
                  identity->cgroup_root_device && identity->cgroup_root_inode &&
                  identity->cgroup_slice_device && identity->cgroup_slice_inode &&
                  strlen(identity->unit) < BQ_WORKER_UNIT_CAP &&
                  strlen(identity->cgroup) < BQ_WORKER_CGROUP_CAP;
        error = ok ? BQ_OK : BQ_WORKER_MISMATCH;
    }
    else if (error == BQ_NOT_FOUND)
    {
        error = BQ_WORKER_MISMATCH;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_number(char const* text, u64* value)
{
    String8 input = string_from_pointer(text);
    IntegerParsingU64 parsed = string8_parse_u64_decimal(input);
    bool ok = parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == input.length;
    *value = ok ? parsed.value : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_duration(char const* text, u64* value)
{
    bool ok = bq_worker_number(text, value);
    if (!ok && !strcmp(text, "1h")) { *value = 60ull * 60 * 1000000; ok = true; }
    if (!ok && !strcmp(text, "10s")) { *value = 10ull * 1000000; ok = true; }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_cpu_list(char const* text, u32 cpu, bool singleton)
{
    bool ok = true;
    bool found = false;
    u32 count = 0;
    char const* at = text;
    while (ok && *at)
    {
        char* end = NULL;
        errno = 0;
        unsigned long first = strtoul(at, &end, 10);
        ok = !errno && end != at && first <= UINT32_MAX;
        unsigned long last = first;
        if (ok && *end == '-')
        {
            at = end + 1;
            errno = 0;
            last = strtoul(at, &end, 10);
            ok = !errno && end != at && last >= first && last <= UINT32_MAX;
        }
        if (ok && (last - first + 1) <= UINT32_MAX - count)
        {
            found = found || (cpu >= first && cpu <= last);
            count += (u32)(last - first + 1);
            if (*end == ',') at = end + 1;
            else if (!*end) at = end;
            else ok = false;
        }
        else
        {
            ok = false;
        }
    }
    bool result = ok && found && (!singleton || count == 1);
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_read_at(int directory, char const* name, char* output, u32 capacity)
{
    int fd = openat(directory, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info;
    bool ok = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode);
    u32 used = 0;
    while (ok && used + 1 < capacity)
    {
        ssize_t count = read(fd, output + used, capacity - used - 1);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) ok = false;
        else if (!count) break;
        else used += (u32)count;
    }
    if (ok)
    {
        char extra;
        ok = read(fd, &extra, 1) == 0;
    }
    if (fd >= 0) close(fd);
    while (ok && used && (output[used - 1] == '\n' || output[used - 1] == '\r')) used -= 1;
    if (ok) output[used] = 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_limit_ok(char const* text, u64 wanted, bool exact)
{
    u64 value = 0;
    bool unlimited = !strcmp(text, "max") || !strcmp(text, "infinity");
    return exact ? (!unlimited && bq_worker_number(text, &value) && value == wanted) :
           unlimited || (bq_worker_number(text, &value) && value >= wanted);
}

/* Validate manager text before it is used as a path.  Each component is then
 * opened relative to an already verified descriptor, without following links. */
BUSTER_GLOBAL_LOCAL bool bq_worker_cgroup_path_valid(char const* path)
{
    bool ok = path && path[0] == '/' && path[1] && strlen(path) < BQ_WORKER_CGROUP_CAP;
    char const* part = ok ? path + 1 : NULL;
    while (ok && *part)
    {
        char const* end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        ok = length && !(length == 1 && part[0] == '.') &&
             !(length == 2 && part[0] == '.' && part[1] == '.');
        for (size_t i = 0; ok && i < length; i += 1)
            ok = (part[i] >= 'a' && part[i] <= 'z') ||
                 (part[i] >= 'A' && part[i] <= 'Z') ||
                 (part[i] >= '0' && part[i] <= '9') ||
                 part[i] == '-' || part[i] == '_' || part[i] == '.';
        part = end ? end + 1 : part + length;
        if (end && !*part) ok = false;
    }
    return ok;
}

/* Verify the leaf and every ancestor below the service-owned cgroup root.
 * Tighter ancestor constraints are rejected: they would silently change the
 * installed recipe's effective budget.  Symlinked components/files fail shut.
 */
BUSTER_GLOBAL_LOCAL bool bq_worker_verify_cgroup(BqWorkerConfig const* config, BqWorkerObserved* observed,
                                                  bool resources)
{
    char root[BQ_PATH_CAP + 1];
    bool ok = bq_worker_text(config->cgroup_root, root, sizeof(root)) &&
              bq_worker_cgroup_path_valid(observed->cgroup) &&
              strncmp(observed->cgroup, "/buster-bench.slice/", 20) == 0;
    int directory = ok ? bq_worker_open_trusted_directory(config->cgroup_root, false, false) : -1;
    ok = directory >= 0;
    struct stat root_identity;
    if (ok && fstat(directory, &root_identity) != 0) ok = false;
    if (ok && !config->backend)
    {
        struct statfs filesystem;
        ok = fstatfs(directory, &filesystem) == 0 && (u64)filesystem.f_type == 0x63677270ull;
    }
    char path[BQ_WORKER_CGROUP_CAP];
    if (ok) snprintf(path, sizeof(path), "%s", observed->cgroup + 1);
    char* part = path;
    bool leaf = false;
    while (ok && *part)
    {
        char* slash = strchr(part, '/');
        if (slash) *slash = 0;
        leaf = slash == NULL;
        int child = *part && strcmp(part, ".") && strcmp(part, "..") ?
                    openat(directory, part, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        if (child < 0)
        {
            ok = false;
        }
        else
        {
            close(directory);
            directory = child;
            struct stat identity;
            ok = bq_worker_directory_owner(directory, false, false) && fstat(directory, &identity) == 0;
            if (ok && !config->backend)
            {
                struct statfs filesystem;
                ok = fstatfs(directory, &filesystem) == 0 &&
                     (u64)filesystem.f_type == 0x63677270ull;
            }
            if (ok && !strcmp(part, "buster-bench.slice"))
            {
                observed->cgroup_root_device = (u64)root_identity.st_dev;
                observed->cgroup_root_inode = (u64)root_identity.st_ino;
                observed->cgroup_slice_device = (u64)identity.st_dev;
                observed->cgroup_slice_inode = (u64)identity.st_ino;
            }
            if (ok && resources)
            {
                char cpus[128], memory[64], swap[64], tasks[64];
                ok = bq_worker_read_at(directory, "cpuset.cpus.effective", cpus, sizeof(cpus)) &&
                     bq_worker_cpu_list(cpus, config->limits.cpu, leaf) &&
                     bq_worker_read_at(directory, "memory.max", memory, sizeof(memory)) &&
                     bq_worker_limit_ok(memory, config->limits.memory_max, leaf) &&
                     bq_worker_read_at(directory, "memory.swap.max", swap, sizeof(swap)) &&
                     bq_worker_limit_ok(swap, config->limits.memory_swap_max, leaf) &&
                     bq_worker_read_at(directory, "pids.max", tasks, sizeof(tasks)) &&
                     bq_worker_limit_ok(tasks, config->limits.tasks_max, leaf);
            }
            if (ok && leaf)
            {
                char events[128];
                ok = bq_worker_read_at(directory, "cgroup.events", events, sizeof(events)) &&
                     (strstr(events, "populated 1") != NULL || strstr(events, "populated 0") != NULL);
                if (ok)
                {
                    observed->populated = strstr(events, "populated 1") != NULL;
                    observed->cgroup_device = (u64)identity.st_dev;
                    observed->cgroup_inode = (u64)identity.st_ino;
                }
            }
        }
        part = slash ? slash + 1 : part + strlen(part);
    }
    if (directory >= 0) close(directory);
    return ok && leaf;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_observed(BqWorkerConfig const* config, char const* boot,
                                             char const* unit, BqWorkerObserved* observed,
                                             bool resources)
{
    char cpu[32], expected_cgroup[BQ_WORKER_CGROUP_CAP];
    snprintf(cpu, sizeof(cpu), "%u", config->limits.cpu);
    snprintf(expected_cgroup, sizeof(expected_cgroup), "/buster-bench.slice/%s", unit);
    bool ok = observed->unit_found && !strcmp(observed->boot_id, boot) && !strcmp(observed->unit, unit) &&
              !strcmp(observed->cgroup, expected_cgroup) &&
              bq_worker_invocation_valid(observed->invocation_id) &&
              bq_worker_verify_cgroup(config, observed, resources);
    if (ok && resources)
    {
        ok = !strcmp(observed->allowed_cpus, cpu) && observed->memory_max == config->limits.memory_max &&
             observed->memory_swap_max == config->limits.memory_swap_max &&
             observed->tasks_max == config->limits.tasks_max &&
             observed->runtime_max_usec == config->limits.runtime_max_usec &&
             observed->timeout_stop_usec == 10ull * 1000000 && observed->send_sigkill &&
             !strcmp(observed->kill_mode, "control-group") && observed->active && observed->populated &&
             observed->result == BQ_WORKER_RUNNING;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_waitpid_until(pid_t pid, int* status, u64 deadline)
{
    BqError error = BQ_OK;
    bool finished = false;
    while (error == BQ_OK && !finished)
    {
        pid_t waited = waitpid(pid, status, WNOHANG);
        if (waited == pid) finished = true;
        else if (waited < 0 && errno != EINTR) error = BQ_IO;
        else
        {
            u64 now = bq_worker_monotonic_milliseconds();
            if (now >= deadline) error = BQ_IO;
            else
            {
                u64 next = bq_worker_deadline(now, 10);
                if (next > deadline) next = deadline;
                error = bq_worker_sleep_until(next);
            }
        }
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_process_group_absent(pid_t group, u64 deadline)
{
    BqError error = BQ_OK;
    bool absent = false;
    while (error == BQ_OK && !absent)
    {
        errno = 0;
        absent = kill(-group, 0) != 0 && errno == ESRCH;
        if (!absent)
        {
            u64 now = bq_worker_monotonic_milliseconds();
            if (now >= deadline) error = BQ_CLEANUP_FAILED;
            else
            {
                u64 next = bq_worker_deadline(now, 10);
                if (next > deadline) next = deadline;
                error = bq_worker_sleep_until(next);
            }
        }
    }
    return error;
}

#ifdef BUSTER_BENCH_SERVICE_TEST
BUSTER_GLOBAL_LOCAL bool bq_worker_test_setpgid_failure;
#endif

BUSTER_GLOBAL_LOCAL BqError bq_worker_exec_capture(char const* const* arguments, char* output, u32 capacity,
                                                    int* status, u32 timeout_milliseconds)
{
    if (capacity) output[0] = 0;
    int pipefd[2] = {-1, -1};
    int setup[2] = {-1, -1};
    BqError error = capacity && pipe(pipefd) == 0 && pipe(setup) == 0 ? BQ_OK : BQ_IO;
    pid_t pid = error == BQ_OK ? fork() : -1;
    if (pid < 0) error = BQ_IO;
    if (!pid)
    {
        close(setup[0]);
        bool grouped = true;
#ifdef BUSTER_BENCH_SERVICE_TEST
        if (bq_worker_test_setpgid_failure) { errno = EPERM; grouped = false; }
        else
#endif
        if (setpgid(0, 0) != 0) grouped = false;
        u8 state = grouped ? 1 : 0;
        ssize_t sent;
        do { sent = write(setup[1], &state, 1); } while (sent < 0 && errno == EINTR);
        close(setup[1]);
        if (!grouped || sent != 1) _exit(125);
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) _exit(126);
        close(pipefd[1]);
        execv(arguments[0], (char* const*)arguments);
        _exit(127);
    }
    if (error == BQ_OK)
    {
        close(setup[1]);
        setup[1] = -1;
        close(pipefd[1]);
        pipefd[1] = -1;
        int flags = fcntl(pipefd[0], F_GETFL), setup_flags = fcntl(setup[0], F_GETFL);
        if (flags < 0 || setup_flags < 0 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) != 0 ||
            fcntl(setup[0], F_SETFL, setup_flags | O_NONBLOCK) != 0) error = BQ_IO;
        u32 used = 0;
        bool eof = false;
        bool overflow = false;
        u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), timeout_milliseconds);
        bool group_ready = false;
        while (error == BQ_OK && !group_ready)
        {
            u64 now = bq_worker_monotonic_milliseconds();
            if (now >= deadline) error = BQ_IO;
            struct pollfd waiting = {setup[0], POLLIN | POLLHUP, 0};
            int milliseconds = error == BQ_OK && deadline - now < INT_MAX ? (int)(deadline - now) :
                               error == BQ_OK ? INT_MAX : 0;
            int ready = error == BQ_OK ? poll(&waiting, 1, milliseconds) : -1;
            if (ready < 0 && errno != EINTR) error = BQ_IO;
            if (ready == 0) error = BQ_IO;
            if (ready > 0)
            {
                u8 state = 0;
                ssize_t count = read(setup[0], &state, 1);
                if (count == 1 && state == 1) group_ready = true;
                else if (count == 1 || count == 0) error = BQ_CLEANUP_FAILED;
                else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) error = BQ_IO;
            }
        }
        close(setup[0]);
        setup[0] = -1;
        while (error == BQ_OK && !eof)
        {
            u64 now = bq_worker_monotonic_milliseconds();
            if (now >= deadline) error = BQ_IO;
            struct pollfd waiting = {pipefd[0], POLLIN | POLLHUP, 0};
            int milliseconds = error == BQ_OK && deadline - now < INT_MAX ? (int)(deadline - now) :
                               error == BQ_OK ? INT_MAX : 0;
            int ready = error == BQ_OK ? poll(&waiting, 1, milliseconds) : -1;
            if (ready < 0 && errno != EINTR) error = BQ_IO;
            if (ready == 0) error = BQ_IO;
            if (ready > 0)
            {
                u8 discard[512];
                bool more = true;
                while (error == BQ_OK && more)
                {
                    char* destination = used + 1 < capacity ? output + used : (char*)discard;
                    u32 available = used + 1 < capacity ? capacity - used - 1 : (u32)sizeof(discard);
                    ssize_t count = read(pipefd[0], destination, available);
                    if (count > 0)
                    {
                        if (used + 1 < capacity) used += (u32)count;
                        else overflow = true;
                    }
                    else if (!count) { eof = true; more = false; }
                    else if (errno == EAGAIN || errno == EWOULDBLOCK) more = false;
                    else if (errno != EINTR) error = BQ_IO;
                }
            }
        }
        output[used] = 0;
        close(pipefd[0]);
        pipefd[0] = -1;
        bool needs_kill = error != BQ_OK;
        bool child_reaped = false;
        if (error == BQ_OK)
        {
            error = bq_worker_waitpid_until(pid, status, deadline);
            needs_kill = error != BQ_OK;
            child_reaped = error == BQ_OK;
        }
        if (overflow) { error = BQ_IO; needs_kill = true; }
        if (needs_kill)
        {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            int ignored = 0;
            u64 reap = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 1000);
            BqError child = child_reaped ? BQ_OK : bq_worker_waitpid_until(pid, &ignored, reap);
            BqError group = bq_worker_process_group_absent(pid, reap);
            if (child != BQ_OK || group != BQ_OK) error = BQ_CLEANUP_FAILED;
        }
    }
    else
    {
        if (pipefd[0] >= 0) close(pipefd[0]);
        if (pipefd[1] >= 0) close(pipefd[1]);
        if (setup[0] >= 0) close(setup[0]);
        if (setup[1] >= 0) close(setup[1]);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_start(BqWorkerBackend* backend, char const* const* argv, u32 count)
{
    (void)count;
    BqSystemdContext* context = backend->context;
    context->pid = fork();
    if (!context->pid)
    {
        execv(argv[0], (char* const*)argv);
        _exit(127);
    }
    context->starting = context->pid > 0;
    return context->starting ? BQ_OK : BQ_IO;
}

BUSTER_GLOBAL_LOCAL char* bq_worker_property(char* text, char const* name)
{
    size_t length = strlen(name);
    char* at = text;
    char* result = NULL;
    while (at && *at && !result)
    {
        char* end = strchr(at, '\n');
        size_t available = end ? (size_t)(end - at) : strlen(at);
        if (available > length && !memcmp(at, name, length) && at[length] == '=') result = at + length + 1;
        else at = end ? end + 1 : NULL;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_observe_once(BqWorkerBackend* backend, char const* unit,
                                                     BqWorkerObserved* observed, u64 deadline)
{
    (void)backend;
    char output[BQ_WORKER_OUTPUT_CAP];
    char const* arguments[] = {BQ_SYSTEMCTL, "show", "--no-pager", "--property=Id", "--property=LoadState",
        "--property=ActiveState", "--property=SubState", "--property=ControlGroup", "--property=AllowedCPUs",
        "--property=MemoryMax", "--property=MemorySwapMax", "--property=TasksMax", "--property=RuntimeMaxUSec",
        "--property=TimeoutStopUSec", "--property=KillMode", "--property=SendSIGKILL", "--property=InvocationID",
        "--property=Result", unit, NULL};
    int status = 0;
    u32 remaining = bq_worker_remaining(deadline);
    BqError error = remaining ? bq_worker_exec_capture(arguments, output, sizeof(output), &status, remaining) : BQ_IO;
    *observed = (BqWorkerObserved){0};
    bool missing = error == BQ_OK && (!WIFEXITED(status) || WEXITSTATUS(status) != 0);
    char* fields[15] = {0};
    char const* names[] = {"Id", "LoadState", "ActiveState", "SubState", "ControlGroup", "AllowedCPUs",
                           "MemoryMax", "MemorySwapMax", "TasksMax", "RuntimeMaxUSec", "TimeoutStopUSec",
                           "KillMode", "SendSIGKILL", "InvocationID", "Result"};
    for (u32 i = 0; error == BQ_OK && !missing && i < BUSTER_ARRAY_LENGTH(fields); i += 1)
        fields[i] = bq_worker_property(output, names[i]);
    for (u32 i = 0; error == BQ_OK && !missing && i < BUSTER_ARRAY_LENGTH(fields); i += 1)
    {
        if (!fields[i]) error = BQ_WORKER_MISMATCH;
        else
        {
            char* end = strchr(fields[i], '\n');
            if (end) *end = 0;
        }
    }
    if (error == BQ_OK && !missing)
    {
        observed->unit_found = !strcmp(fields[1], "loaded");
        observed->active = !strcmp(fields[2], "active") || !strcmp(fields[2], "activating");
        snprintf(observed->unit, sizeof(observed->unit), "%s", fields[0]);
        snprintf(observed->cgroup, sizeof(observed->cgroup), "%s", fields[4]);
        snprintf(observed->allowed_cpus, sizeof(observed->allowed_cpus), "%s", fields[5]);
        if (!bq_worker_number(fields[6], &observed->memory_max) || !bq_worker_number(fields[7], &observed->memory_swap_max) ||
            !bq_worker_number(fields[8], &observed->tasks_max) || !bq_worker_duration(fields[9], &observed->runtime_max_usec) ||
            !bq_worker_duration(fields[10], &observed->timeout_stop_usec))
        {
            error = BQ_WORKER_MISMATCH;
        }
        snprintf(observed->kill_mode, sizeof(observed->kill_mode), "%s", fields[11]);
        observed->send_sigkill = !strcmp(fields[12], "yes");
        snprintf(observed->invocation_id, sizeof(observed->invocation_id), "%s", fields[13]);
        observed->result = !strcmp(fields[14], "oom-kill") ? BQ_WORKER_OOM :
                           !strcmp(fields[14], "timeout") ? BQ_WORKER_TIMED_OUT :
                           !strcmp(fields[14], "success") ? BQ_WORKER_SUCCEEDED :
                           !strcmp(fields[14], "canceled") ? BQ_WORKER_CANCELLED_RESULT :
                           observed->active ? BQ_WORKER_RUNNING : BQ_WORKER_EXECUTION_FAILED;
        char boot[BQ_WORKER_BOOT_CAP];
        if (!bq_worker_read_regular("/proc/sys/kernel/random/boot_id", boot, sizeof(boot))) error = BQ_IO;
        else snprintf(observed->boot_id, sizeof(observed->boot_id), "%s", boot);
        /* The manager path is deliberately not opened here.  Its syntax,
         * no-follow traversal, cgroup-v2 mount and files are validated later
         * through config->cgroup_root before the value authorizes any signal. */
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_observe(BqWorkerBackend* backend, char const* unit,
                                                BqWorkerObserved* observed, u64 deadline)
{
    BqSystemdContext* context = backend->context;
    u32 attempts = context->starting ? 20 : 1;
    BqError error = BQ_OK;
    for (u32 attempt = 0; error == BQ_OK && attempt < attempts; attempt += 1)
    {
        error = bq_systemd_observe_once(backend, unit, observed, deadline);
        if (error == BQ_OK && !observed->unit_found && attempt + 1 < attempts)
        {
            u64 next = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 10);
            if (next > deadline) next = deadline;
            error = bq_worker_sleep_until(next);
        }
        else
        {
            attempts = attempt + 1;
        }
    }
    context->starting = false;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_signal(BqWorkerBackend* backend, char const* unit, char const* signal_name,
                                               u64 deadline)
{
    (void)backend;
    char option[64];
    int length = snprintf(option, sizeof(option), "--signal=%s", signal_name);
    char output[512];
    char const* arguments[] = {BQ_SYSTEMCTL, "kill", "--kill-whom=all", option, unit, NULL};
    int status = 0;
    u32 remaining = bq_worker_remaining(deadline);
    BqError error = length <= 0 || (u32)length >= sizeof(option) ? BQ_BAD_REQUEST : !remaining ? BQ_IO :
                    bq_worker_exec_capture(arguments, output, sizeof(output), &status, remaining);
    if (error == BQ_OK && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) error = BQ_IO;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_join(BqWorkerBackend* backend, int* status, u64 deadline)
{
    BqSystemdContext* context = backend->context;
    BqError error = BQ_OK;
    if (context->pid > 0)
    {
        bool finished = false;
        while (error == BQ_OK && !finished && !bq_worker_cancel_signal)
        {
            pid_t waited = waitpid(context->pid, status, WNOHANG);
            if (waited == context->pid)
            {
                context->pid = -1;
                finished = true;
            }
            else if (waited < 0 && errno != EINTR) error = BQ_IO;
            else
            {
                u64 now = bq_worker_monotonic_milliseconds();
                if (now >= deadline) error = BQ_WORKER_TIMEOUT;
                else error = bq_worker_sleep_until(bq_worker_deadline(now, 100) < deadline ?
                                                   bq_worker_deadline(now, 100) : deadline);
            }
        }
        if (bq_worker_cancel_signal && !finished) error = BQ_WORKER_CANCEL_SIGNAL;
    }
    else
    {
        *status = 0;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_cleanup_launcher(BqWorkerBackend* backend, u64 deadline)
{
    BqSystemdContext* context = backend->context;
    BqError error = BQ_OK;
    if (context->pid > 0)
    {
        if (kill(context->pid, SIGKILL) != 0 && errno != ESRCH) error = BQ_CLEANUP_FAILED;
        int status = 0;
        if (error == BQ_OK && bq_worker_waitpid_until(context->pid, &status, deadline) != BQ_OK)
            error = BQ_CLEANUP_FAILED;
        if (error == BQ_OK) context->pid = -1;
    }
    context->starting = false;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_systemd_delay(BqWorkerBackend* backend, u32 milliseconds)
{
    (void)backend;
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), milliseconds);
    BqError error = bq_worker_sleep_until(deadline);
    return error;
}

BUSTER_GLOBAL_LOCAL u64 bq_systemd_clock(BqWorkerBackend* backend)
{
    (void)backend;
    u64 result = bq_worker_monotonic_milliseconds();
    return result;
}

void bq_worker_backend_systemd(BqWorkerBackend* backend)
{
    static BqSystemdContext context;
    context.pid = -1;
    context.starting = false;
    *backend = (BqWorkerBackend){&context, bq_systemd_start, bq_systemd_observe, bq_systemd_signal,
                                bq_systemd_join, bq_systemd_cleanup_launcher,
                                bq_systemd_delay, bq_systemd_clock};
}

typedef struct BqWorkerFinalization
{
    sigset_t prior_mask;
    bool masked;
} BqWorkerFinalization;

#ifdef BUSTER_BENCH_SERVICE_TEST
BUSTER_GLOBAL_LOCAL u32 bq_worker_test_finish_checkpoints;
BUSTER_GLOBAL_LOCAL u32 bq_worker_test_cancel_during_finish;
#endif

BUSTER_GLOBAL_LOCAL void bq_worker_finish_checkpoint(void)
{
#ifdef BUSTER_BENCH_SERVICE_TEST
    bq_worker_test_finish_checkpoints += 1;
    if (bq_worker_test_cancel_during_finish == bq_worker_test_finish_checkpoints)
        raise(SIGTERM);
#endif
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_finish_cancel(BqQueue* queue, BqJob** job)
{
    BqError error = BQ_OK;
    if (bq_worker_cancel_signal && *job)
    {
        if (!(*job)->cancel_requested)
        {
            error = bq_cancel(queue, (*job)->id);
            *job = bq_job(&queue->state, (*job)->id);
        }
        if (error == BQ_OK && *job && (*job)->cancel_requested) bq_worker_cancel_signal = 0;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_before_terminal(BqQueue* queue, BqJob* job, void* context)
{
    BqWorkerFinalization* finalization = context;
    bq_worker_finish_checkpoint();
    sigset_t blocked, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGINT);
    BqError error = sigprocmask(SIG_BLOCK, &blocked, &finalization->prior_mask) == 0 ? BQ_OK : BQ_IO;
    finalization->masked = error == BQ_OK;
    if (error == BQ_OK && sigpending(&pending) != 0) error = BQ_IO;
    if (error == BQ_OK && (sigismember(&pending, SIGTERM) == 1 || sigismember(&pending, SIGINT) == 1))
        bq_worker_cancel_signal = 1;
    if (error == BQ_OK) error = bq_worker_finish_cancel(queue, &job);
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_finalization_restore(BqWorkerFinalization* finalization)
{
    BqError error = BQ_OK;
    if (finalization->masked)
    {
        if (sigprocmask(SIG_SETMASK, &finalization->prior_mask, NULL) != 0) error = BQ_IO;
        finalization->masked = false;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_finish(BqQueue* queue, BqWorkerConfig const* config, BqJob* job,
                                              BqOutcome outcome, BqError reason,
                                              BqWorkerFinalization* finalization)
{
    BqError error = outcome == BQ_SUCCEEDED && !finalization ? BQ_BAD_REQUEST : BQ_OK;
    if (outcome == BQ_SUCCEEDED)
    {
        for (BqPhase phase = (BqPhase)(job->phase + 1); error == BQ_OK && phase <= BQ_CLEANING; phase = (BqPhase)(phase + 1))
        {
            BqOutcome next_outcome = job->cancel_requested && phase >= BQ_CLEANING ? BQ_CANCELLED :
                                     phase >= BQ_FINALIZING ? BQ_SUCCEEDED : BQ_NO_OUTCOME;
            error = bq_real_advance(queue, job, phase, next_outcome);
            job = bq_job(&queue->state, job->id);
            bq_worker_finish_checkpoint();
            if (error == BQ_OK) error = bq_worker_finish_cancel(queue, &job);
            if (error == BQ_OK && job->cancel_requested && job->phase < BQ_CLEANING)
            {
                error = bq_real_advance(queue, job, BQ_CLEANING, BQ_CANCELLED);
                job = bq_job(&queue->state, job->id);
                phase = BQ_CLEANING;
            }
        }
    }
    else
    {
        BqError prior = bq_failure_evidence(queue, job);
        if (outcome != BQ_CANCELLED && prior == BQ_NOT_FOUND) error = bq_failure_write(queue, job, reason);
        else if (outcome != BQ_CANCELLED && (prior == BQ_CORRUPT || prior == BQ_IO)) error = prior;
        if (error == BQ_OK && job->phase < BQ_CLEANING) error = bq_real_advance(queue, job, BQ_CLEANING, outcome);
    }
    if (error == BQ_OK)
    {
        queue->needs_reconciliation = true;
        BqOutcome terminal = outcome == BQ_INTERRUPTED &&
                             (reason == BQ_BOOT_INTERRUPTED || job->phase < BQ_CLEANING) ? BQ_INTERRUPTED : BQ_NO_OUTCOME;
        error = outcome == BQ_SUCCEEDED ?
                bq_workspace_reconcile_controlled(queue, config->workspace_root, job->id, job->token,
                                                  terminal, bq_worker_before_terminal, finalization) :
                bq_workspace_reconcile_outcome(queue, config->workspace_root, job->id, job->token, terminal);
    }
    if (error != BQ_OK) queue->needs_reconciliation = true;
    return error;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_cgroup_absent(BqWorkerConfig const* config,
                                                  BqWorkerObserved const* identity)
{
    int root = identity ? bq_worker_open_trusted_directory(config->cgroup_root, false, false) : -1;
    int slice = root >= 0 ? openat(root, "buster-bench.slice", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat root_info, slice_info, info;
    struct statfs filesystem;
    bool filesystem_ok = root >= 0 && slice >= 0 &&
                         bq_worker_directory_owner(slice, false, false) &&
                         fstat(root, &root_info) == 0 && fstat(slice, &slice_info) == 0 &&
                         identity->cgroup_root_device == (u64)root_info.st_dev &&
                         identity->cgroup_root_inode == (u64)root_info.st_ino &&
                         identity->cgroup_slice_device == (u64)slice_info.st_dev &&
                         identity->cgroup_slice_inode == (u64)slice_info.st_ino &&
                         (config->backend || (fstatfs(root, &filesystem) == 0 &&
                          (u64)filesystem.f_type == 0x63677270ull));
    if (filesystem_ok && !config->backend)
        filesystem_ok = fstatfs(slice, &filesystem) == 0 &&
                        (u64)filesystem.f_type == 0x63677270ull;
    errno = 0;
    bool absent = filesystem_ok && fstatat(slice, identity->unit, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                  errno == ENOENT;
    if (slice >= 0) close(slice);
    if (root >= 0) close(root);
    return absent;
}

BUSTER_GLOBAL_LOCAL bool bq_worker_instance_matches(BqWorkerConfig const* config,
                                                     BqWorkerObserved const* identity,
                                                     BqWorkerObserved* observed)
{
    bool ok = bq_worker_observed(config, identity->boot_id, identity->unit, observed, false);
    return ok && !strcmp(identity->invocation_id, observed->invocation_id) &&
           !strcmp(identity->cgroup, observed->cgroup) &&
           identity->cgroup_device == observed->cgroup_device &&
           identity->cgroup_inode == observed->cgroup_inode &&
           identity->cgroup_root_device == observed->cgroup_root_device &&
           identity->cgroup_root_inode == observed->cgroup_root_inode &&
           identity->cgroup_slice_device == observed->cgroup_slice_device &&
           identity->cgroup_slice_inode == observed->cgroup_slice_inode;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_poll_empty(BqWorkerConfig const* config, BqWorkerBackend* backend,
                                                 BqWorkerObserved const* identity,
                                                 BqWorkerObserved* observed, u64 deadline)
{
    BqError error = BQ_OK;
    u32 attempts = 0;
    while (error == BQ_OK && (observed->active || observed->populated) &&
           backend->clock(backend) < deadline &&
           attempts < BQ_WORKER_STOP_MILLISECONDS / BQ_WORKER_POLL_MILLISECONDS)
    {
        attempts += 1;
        u64 now = backend->clock(backend);
        u64 available = deadline - now;
        u32 delay = available < BQ_WORKER_POLL_MILLISECONDS ? (u32)available : BQ_WORKER_POLL_MILLISECONDS;
        error = delay ? backend->delay(backend, delay) : BQ_OK;
        if (error == BQ_OK) error = backend->observe(backend, identity->unit, observed, deadline);
        if (error == BQ_OK && observed->unit_found && !bq_worker_instance_matches(config, identity, observed))
            error = BQ_WORKER_MISMATCH;
        if (error == BQ_OK && !observed->unit_found && !bq_worker_cgroup_absent(config, identity))
            error = BQ_CLEANUP_FAILED;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_stop(BqWorkerConfig const* config, BqWorkerBackend* backend,
                                            BqWorkerObserved const* identity, BqWorkerObserved* observed,
                                            char const* lease_path, BqWorkerLease* lease)
{
    BqError error = !identity || !backend->delay ? BQ_BAD_REQUEST : BQ_OK;
    if (error == BQ_OK && observed->unit_found && !bq_worker_instance_matches(config, identity, observed))
        error = BQ_WORKER_MISMATCH;
    if (error == BQ_OK && (observed->active || observed->populated))
    {
        error = backend->signal(backend, identity->unit, "TERM",
                                bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
        u64 deadline = bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS);
        if (error == BQ_OK) error = bq_worker_poll_empty(config, backend, identity, observed, deadline);
    }
    if (error == BQ_OK && (observed->active || observed->populated))
    {
        /* The fixed scope property grants ten seconds to TERM.  KILL then gets
         * the same bounded observation window; neither wait trusts unit names. */
        error = backend->signal(backend, identity->unit, "KILL",
                                bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
        u64 deadline = bq_worker_deadline(backend->clock(backend), BQ_WORKER_STOP_MILLISECONDS);
        if (error == BQ_OK) error = bq_worker_poll_empty(config, backend, identity, observed, deadline);
    }
    bool empty = observed->unit_found ? !observed->active && !observed->populated :
                 bq_worker_cgroup_absent(config, identity);
    int status = 0;
    if (error == BQ_OK && empty && lease->descriptor < 0 && bq_worker_lease_acquire(lease_path, lease) != 0)
        error = BQ_BUSY;
    if (error == BQ_OK && empty)
        error = backend->join(backend, &status,
                              bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
    if (error == BQ_OK && empty)
    {
        BqWorkerObserved final = {0};
        error = backend->observe(backend, identity->unit, &final,
                                 bq_worker_deadline(backend->clock(backend),
                                                    BQ_WORKER_COMMAND_MILLISECONDS));
        if (error == BQ_OK && final.unit_found)
        {
            if (!bq_worker_instance_matches(config, identity, &final)) error = BQ_WORKER_MISMATCH;
            else if (final.active || final.populated) error = BQ_CLEANUP_FAILED;
        }
        else if (error == BQ_OK && !bq_worker_cgroup_absent(config, identity))
        {
            error = BQ_CLEANUP_FAILED;
        }
    }
    BqError result = error == BQ_WORKER_MISMATCH ? error :
                     error == BQ_OK && empty ? BQ_OK : BQ_CLEANUP_FAILED;
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_worker_recover(BqQueue* queue, BqWorkerConfig const* config,
                                               BqWorkerBackend* backend, char const* lease_path,
                                               char const* current_boot, BqJob* job,
                                               BqWorkerLease* lease,
                                               BqWorkerFinalization* finalization)
{
    char saved_boot[BQ_WORKER_BOOT_CAP], unit[BQ_WORKER_UNIT_CAP];
    BqError error = !job || !bq_recipe_real(&job->request) ? BQ_WORKER_MISMATCH :
                    bq_worker_record_read(queue, job, saved_boot, unit);
    BqWorkerObserved observed = {0};
    bool durable_outcome = job && job->phase >= BQ_FINALIZING && job->outcome != BQ_NO_OUTCOME;
    if (error == BQ_OK && strcmp(saved_boot, current_boot))
    {
        if (lease->descriptor < 0 && bq_worker_lease_acquire(lease_path, lease) != 0) error = BQ_BUSY;
        else error = bq_worker_finish(queue, config, job,
                                      durable_outcome ? job->outcome : BQ_INTERRUPTED,
                                      durable_outcome ? BQ_NOT_FOUND : BQ_BOOT_INTERRUPTED,
                                      finalization);
    }
    else if (error == BQ_OK)
    {
        BqWorkerObserved identity = {0};
        error = bq_worker_instance_read(queue, job, &identity);
        if (error == BQ_OK && (strcmp(identity.boot_id, saved_boot) || strcmp(identity.unit, unit)))
            error = BQ_WORKER_MISMATCH;
        if (error == BQ_OK) error = backend->observe(backend, unit, &observed,
            bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
        if (error == BQ_OK && observed.unit_found && !bq_worker_instance_matches(config, &identity, &observed))
            error = BQ_WORKER_MISMATCH;
        if (error == BQ_OK && observed.unit_found)
            error = bq_worker_stop(config, backend, &identity, &observed, lease_path, lease);
        if (error == BQ_OK && !observed.unit_found && !bq_worker_cgroup_absent(config, &identity))
            error = BQ_WORKER_MISMATCH;
        if (error == BQ_OK && lease->descriptor < 0 && bq_worker_lease_acquire(lease_path, lease) != 0) error = BQ_BUSY;
        if (error == BQ_OK) error = bq_worker_finish(queue, config, job,
                                                     durable_outcome ? job->outcome : BQ_INTERRUPTED,
                                                     durable_outcome ? BQ_NOT_FOUND : BQ_WORKER_INTERRUPTED,
                                                     finalization);
    }
    return error;
}

BqError bq_worker_run(BqQueue* queue, BqWorkerConfig const* config, u64* id)
{
    *id = queue->state.active_id;
    char lease_path[BQ_PATH_CAP + 1], boot_path[BQ_PATH_CAP + 1], current_boot[BQ_WORKER_BOOT_CAP];
    BqWorkerBackend systemd;
    BqWorkerBackend* backend = config ? config->backend : NULL;
    bool production = config && !backend;
    if (!backend)
    {
        bq_worker_backend_systemd(&systemd);
        backend = &systemd;
    }
    BqError error = !config || !bq_worker_text(config->lease_file, lease_path, sizeof(lease_path)) || lease_path[0] != '/' ||
                    !bq_worker_text(config->boot_id_file, boot_path, sizeof(boot_path)) || boot_path[0] != '/' ||
                    config->limits.cpu >= CPU_SETSIZE || !config->limits.memory_max ||
                    !config->limits.tasks_max || !config->limits.runtime_max_usec ||
                    !backend->start || !backend->observe || !backend->signal || !backend->join ||
                    !backend->cleanup_launcher ||
                    !backend->delay || !backend->clock || !config->quarantine ? BQ_BAD_REQUEST : BQ_OK;
    if (error == BQ_OK && (!bq_worker_read_regular(boot_path, current_boot, sizeof(current_boot)) ||
                           !bq_worker_boot_valid(current_boot))) error = BQ_CONFIGURATION_MISMATCH;
    struct sigaction cancel_action = {0}, old_term = {0}, old_interrupt = {0};
    bool term_handler = false;
    bool interrupt_handler = false;
    if (error == BQ_OK && production)
    {
        cancel_action.sa_handler = bq_worker_cancel_handler;
        sigemptyset(&cancel_action.sa_mask);
        bq_worker_cancel_signal = 0;
        term_handler = sigaction(SIGTERM, &cancel_action, &old_term) == 0;
        interrupt_handler = term_handler && sigaction(SIGINT, &cancel_action, &old_interrupt) == 0;
        if (!interrupt_handler) error = BQ_IO;
    }
    BqWorkerLease lease = {.descriptor = -1};
    BqWorkerFinalization finalization = {0};
    bool launched = false;
    bool instance_bound = false;
    BqJob* job = *id ? bq_job(&queue->state, *id) : NULL;
    bool recovering = queue->needs_reconciliation;
    if (error == BQ_OK && !recovering && bq_worker_cancel_signal) error = BQ_WORKER_CANCEL_SIGNAL;
    if (error == BQ_OK && config->quarantine->descriptor >= 0)
    {
        int descriptor = config->quarantine->descriptor;
        if (!recovering || strcmp(config->quarantine->lease_path, lease_path)) error = BQ_BUSY;
        else
        {
            int duplicate = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
            if (duplicate < 0 || bq_worker_lease_adopt(lease_path, duplicate, &lease) != 0)
                error = BQ_CONFIGURATION_MISMATCH;
            else
            {
                close(config->quarantine->descriptor);
                config->quarantine->descriptor = -1;
            }
        }
    }
    if (error == BQ_OK && recovering)
        error = bq_worker_recover(queue, config, backend, lease_path, current_boot, job, &lease,
                                  &finalization);
    if (error == BQ_OK && !recovering && bq_worker_lease_acquire(lease_path, &lease) != 0) error = BQ_BUSY;
    if (error == BQ_OK && !recovering && bq_worker_cancel_signal) error = BQ_WORKER_CANCEL_SIGNAL;
    u64 token = 0;
    if (error == BQ_OK && !recovering) error = bq_materialize(queue, config->installed_root, config->workspace_root, id, &token);
    if (!recovering) job = error == BQ_OK ? bq_job(&queue->state, *id) : NULL;
    char unit[BQ_WORKER_UNIT_CAP];
    if (error == BQ_OK && !recovering && (!job || !bq_worker_unit_name(unit, job->id, job->token))) error = BQ_WORKER_MISMATCH;
    if (error == BQ_OK && !recovering) error = bq_worker_record_write(queue, job, current_boot, unit);
    if (error == BQ_OK && !recovering && production && bq_worker_cancel_signal)
    {
        error = bq_cancel(queue, job->id);
        if (error == BQ_OK) error = bq_worker_finish(queue, config, job, BQ_CANCELLED,
                                                     BQ_WORKER_CANCEL_SIGNAL, &finalization);
        recovering = true;
    }
    int old_flags = error == BQ_OK && !recovering ? fcntl(lease.descriptor, F_GETFD) : -1;
    if (error == BQ_OK && !recovering && (old_flags < 0 || fcntl(lease.descriptor, F_SETFD, old_flags & ~FD_CLOEXEC) != 0)) error = BQ_IO;
    char unit_option[128], cpu_property[64], memory_property[64], swap_property[64], tasks_property[64], runtime_property[64];
    char lease_fd[32];
    if (error == BQ_OK && !recovering)
    {
        snprintf(unit_option, sizeof(unit_option), "--unit=%s", unit);
        snprintf(cpu_property, sizeof(cpu_property), "--property=AllowedCPUs=%u", config->limits.cpu);
        snprintf(memory_property, sizeof(memory_property), "--property=MemoryMax=%" PRIu64, (uint64_t)config->limits.memory_max);
        snprintf(swap_property, sizeof(swap_property), "--property=MemorySwapMax=%" PRIu64, (uint64_t)config->limits.memory_swap_max);
        snprintf(tasks_property, sizeof(tasks_property), "--property=TasksMax=%" PRIu64, (uint64_t)config->limits.tasks_max);
        snprintf(runtime_property, sizeof(runtime_property), "--property=RuntimeMaxSec=%" PRIu64 "us", (uint64_t)config->limits.runtime_max_usec);
        snprintf(lease_fd, sizeof(lease_fd), "%d", lease.descriptor);
        char const* arguments[] = {BQ_SYSTEMD_RUN, "--quiet", "--scope", unit_option, "--slice=buster-bench.slice",
            "--property=KillMode=control-group", "--property=SendSIGKILL=yes", "--property=TimeoutStopSec=10s",
            cpu_property, memory_property, swap_property, tasks_property, runtime_property,
            BQ_WORKER_EXECUTABLE, "worker-unit", lease_path, lease_fd, NULL};
        error = backend->start(backend, arguments, BUSTER_ARRAY_LENGTH(arguments) - 1);
        launched = error == BQ_OK;
    }
    if (!recovering && old_flags >= 0) fcntl(lease.descriptor, F_SETFD, old_flags);
    BqWorkerObserved observed = {0};
    BqWorkerObserved identity = {0};
    if (error == BQ_OK && !recovering) error = backend->observe(backend, unit, &observed,
        bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
    if (error == BQ_OK && !recovering)
    {
        bool exact_unit = bq_worker_observed(config, current_boot, unit, &observed, false);
        if (!exact_unit) error = BQ_WORKER_MISMATCH;
        else
        {
            identity = observed;
            error = bq_worker_instance_write(queue, job, &identity);
            instance_bound = error == BQ_OK;
        }
    }
    if (error == BQ_OK && !recovering && !bq_worker_observed(config, current_boot, unit, &observed, true)) error = BQ_RESOURCE_MISMATCH;
    if (error == BQ_OK && !recovering)
    {
        error = backend->observe(backend, unit, &observed,
                                 bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
        if (error == BQ_OK && !bq_worker_instance_matches(config, &identity, &observed)) error = BQ_WORKER_MISMATCH;
    }
    if (error == BQ_OK && !recovering && observed.result == BQ_WORKER_RUNNING)
        error = backend->signal(backend, unit, "CONT",
                                bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
    int status = 0;
    if (error == BQ_OK && !recovering)
    {
        u64 wait = config->limits.runtime_max_usec / 1000;
        wait = wait <= UINT64_MAX - BQ_WORKER_STOP_MILLISECONDS ? wait + BQ_WORKER_STOP_MILLISECONDS : UINT64_MAX;
        error = backend->join(backend, &status, bq_worker_deadline(backend->clock(backend), wait));
    }
    bool interrupted_join = error == BQ_WORKER_CANCEL_SIGNAL;
    bool signal_cancelled = interrupted_join || (production && bq_worker_cancel_signal);
    if (signal_cancelled && job)
    {
        BqError cancel = bq_cancel(queue, job->id);
        error = cancel == BQ_OK ? bq_worker_stop(config, backend, &identity, &observed, lease_path, &lease) : cancel;
        bq_worker_cancel_signal = 0;
        if (error == BQ_OK) error = bq_worker_finish(queue, config, job, BQ_CANCELLED,
                                                     BQ_WORKER_CANCEL_SIGNAL, &finalization);
    }
    else if (signal_cancelled)
    {
        error = BQ_WORKER_CANCEL_SIGNAL;
        bq_worker_cancel_signal = 0;
    }
    if (error == BQ_OK && !recovering && !signal_cancelled) error = backend->observe(backend, unit, &observed,
        bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
    if (error == BQ_OK && !recovering && !signal_cancelled && !bq_worker_instance_matches(config, &identity, &observed)) error = BQ_WORKER_MISMATCH;
    if (error == BQ_OK && !recovering && !signal_cancelled)
        error = bq_worker_stop(config, backend, &identity, &observed, lease_path, &lease);
    if (error == BQ_OK && !recovering && !signal_cancelled && bq_worker_cancel_signal)
    {
        job = bq_job(&queue->state, *id);
        BqError cancel = job ? bq_cancel(queue, job->id) : BQ_WORKER_MISMATCH;
        signal_cancelled = true;
        bq_worker_cancel_signal = 0;
        error = cancel == BQ_OK ? bq_worker_finish(queue, config, job, BQ_CANCELLED,
                                                    BQ_WORKER_CANCEL_SIGNAL, &finalization) : cancel;
    }
    if (error == BQ_OK && !recovering && !signal_cancelled)
    {
        job = bq_job(&queue->state, *id);
        BqOutcome outcome = job->cancel_requested || observed.result == BQ_WORKER_CANCELLED_RESULT ? BQ_CANCELLED :
                            observed.result == BQ_WORKER_SUCCEEDED ? BQ_SUCCEEDED : BQ_FAILED;
        BqError reason = observed.result == BQ_WORKER_OOM ? BQ_WORKER_OOM_FAILURE :
                         observed.result == BQ_WORKER_TIMED_OUT ? BQ_WORKER_TIMEOUT : BQ_WORKER_FAILED;
        error = bq_worker_finish(queue, config, job, outcome, reason, &finalization);
    }
    else if (!recovering && job && queue->state.active_id == job->id)
    {
        BqError reason = error;
        if (launched && !instance_bound)
        {
            BqWorkerObserved candidate = {0};
            BqError inspected = backend->observe(backend, unit, &candidate,
                bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
            if (inspected == BQ_OK && bq_worker_observed(config, current_boot, unit, &candidate, false) &&
                bq_worker_instance_write(queue, job, &candidate) == BQ_OK)
            {
                identity = candidate;
                observed = candidate;
                instance_bound = true;
            }
            else
            {
                error = inspected == BQ_OK ? BQ_WORKER_MISMATCH : BQ_CLEANUP_FAILED;
            }
            if (!instance_bound && backend->cleanup_launcher(
                    backend, bq_worker_deadline(backend->clock(backend),
                                                BQ_WORKER_COMMAND_MILLISECONDS)) != BQ_OK)
                error = BQ_CLEANUP_FAILED;
        }
        if (launched && instance_bound && reason != BQ_CLEANUP_FAILED)
        {
            BqWorkerObserved candidate = {0};
            BqError inspected = backend->observe(backend, unit, &candidate,
                bq_worker_deadline(backend->clock(backend), BQ_WORKER_COMMAND_MILLISECONDS));
            BqError stopped = inspected == BQ_OK && bq_worker_instance_matches(config, &identity, &candidate) ?
                              bq_worker_stop(config, backend, &identity, &candidate, lease_path, &lease) :
                              inspected == BQ_OK ? BQ_WORKER_MISMATCH : BQ_CLEANUP_FAILED;
            if (stopped == BQ_OK)
            {
                BqError finished = bq_worker_finish(queue, config, job, BQ_FAILED,
                    reason == BQ_RESOURCE_MISMATCH ? BQ_RESOURCE_MISMATCH :
                    reason == BQ_WORKER_TIMEOUT ? BQ_WORKER_TIMEOUT : BQ_WORKER_FAILED,
                    &finalization);
                if (finished != BQ_OK) error = finished;
            }
            else
            {
                error = stopped;
            }
        }
        if (queue->state.active_id == job->id)
        {
            BqError evidence_reason = error == BQ_CLEANUP_FAILED ? BQ_CLEANUP_FAILED :
                                      error == BQ_RESOURCE_MISMATCH ? BQ_RESOURCE_MISMATCH : BQ_WORKER_MISMATCH;
            BqError prior = bq_failure_evidence(queue, job);
            BqError evidence = prior == BQ_NOT_FOUND ? bq_failure_write(queue, job, evidence_reason) :
                               prior == BQ_CORRUPT || prior == BQ_IO ? prior : BQ_OK;
            if (evidence != BQ_OK) error = evidence;
            queue->needs_reconciliation = true;
        }
    }
    if (interrupt_handler && sigaction(SIGINT, &old_interrupt, NULL) != 0) error = BQ_IO;
    if (term_handler)
    {
        if (sigaction(SIGTERM, &old_term, NULL) != 0) error = BQ_IO;
    }
    if (bq_worker_finalization_restore(&finalization) != BQ_OK) error = BQ_IO;
    if (config && config->quarantine && queue->state.active_id && lease.descriptor >= 0 &&
        config->quarantine->descriptor < 0)
    {
        config->quarantine->descriptor = lease.descriptor;
        snprintf(config->quarantine->lease_path, sizeof(config->quarantine->lease_path), "%s", lease_path);
        lease.descriptor = -1;
    }
    bq_worker_lease_release(&lease);
    return error;
}

BqError bq_worker_unit(String8 lease_file, int lease_fd)
{
    char path[BQ_PATH_CAP + 1];
    BqWorkerLease lease = {.descriptor = -1};
    BqError error = !bq_worker_text(lease_file, path, sizeof(path)) || path[0] != '/' || lease_fd < 3 ? BQ_BAD_REQUEST : BQ_OK;
    if (error == BQ_OK && bq_worker_lease_adopt(path, lease_fd, &lease) != 0) error = BQ_CONFIGURATION_MISMATCH;
    if (error == BQ_OK)
    {
        raise(SIGSTOP);
        /* PR3 installs the fixed recipe executor here.  Returning unsupported
         * is intentional and can never be interpreted as a measurement. */
        error = BQ_UNSUPPORTED;
    }
    bq_worker_lease_release(&lease);
    return error;
}

#else

void bq_worker_backend_systemd(BqWorkerBackend* backend)
{
    *backend = (BqWorkerBackend){0};
}

BqError bq_worker_run(BqQueue* queue, BqWorkerConfig const* config, u64* id)
{
    (void)queue;
    (void)config;
    *id = 0;
    return BQ_UNSUPPORTED;
}

BqError bq_worker_unit(String8 lease_file, int lease_fd)
{
    (void)lease_file;
    (void)lease_fd;
    return BQ_UNSUPPORTED;
}

#endif
