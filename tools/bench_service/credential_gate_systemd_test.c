/* Disposable real-systemd component regression for the first-executable gate.
 *
 * --run is destructive to NSS inside ONE throwaway Docker/systemd guest. It
 * requires an explicit environment opt-in, a Docker marker, PID1 systemd, and
 * a root-owned guest opt-in file. Never install or run it on the physical host.
 * The guest installs this benign binary at the gate's fixed service and build
 * payload paths; the gate itself is the separately built production binary.
 * Probe units observe PID1's credentials after controlled NSS changes; gate
 * units then start the real gate as their first ExecStart. The fixture only
 * records a bounded identity marker if that gate admits its fixed payload.
 * This is component evidence, not the complete broker/service #1162 run.
 *
 * Provision a fresh Docker guest with PID1 systemd, /run as tmpfs, and no
 * network. Install static credential_gate.c at BQCG_GATE (root:root, 0755),
 * this binary at BQCG_BUILD and BQCG_SERVICE (root:root, 0755), and this test
 * runner at an unrelated root-only path. Create BQCG_OPT_IN with precisely
 * BQCG_OPT_IN_TEXT (root:root, 0644). Invoke --run as root with
 * BUSTER_CREDENTIAL_GATE_DISPOSABLE_SYSTEMD=isolated-docker-systemd-test.
 * Preserve stdout and /run/buster-bench-credential-gate-test before removing
 * the disposable guest. No shell, arbitrary command, or physical executor is
 * involved in this C fixture.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Exercise production signal-identity parsing on actual systemctl readback. */
#define main bqcg_broker_original_main
#include "systemd_broker.c"
#undef main

#define BQCG_GATE "/usr/local/libexec/buster-bench-credential-gate"
#define BQCG_BUILD "/usr/local/libexec/buster-bench-build"
#define BQCG_SERVICE "/usr/local/libexec/buster-bench-service"
#define BQCG_ROOT "/run/buster-bench-credential-gate-test"
#define BQCG_OPT_IN "/run/buster-bench-credential-gate-test.opt-in"
#define BQCG_OPT_IN_TEXT "buster14a/buster disposable credential-gate component test\n"
#define BQCG_ENV "BUSTER_CREDENTIAL_GATE_DISPOSABLE_SYSTEMD"
#define BQCG_ENV_VALUE "isolated-docker-systemd-test"
#define BQCG_GROUP_CAP 32
#define BQCG_CASE_COUNT 8

typedef enum BqCgChange
{
    BQCG_CLEAN,
    BQCG_ADD_SERVICE,
    BQCG_ADD_ROOT,
    BQCG_ADD_OTHER,
    BQCG_CHANGE_UID,
    BQCG_CHANGE_GID,
    BQCG_REMOVE_CANDIDATE,
} BqCgChange;

typedef struct BqCgCase
{
    char const* name;
    char const* account;
    bool service;
    BqCgChange change;
} BqCgCase;

static BqCgCase const bqcg_cases[BQCG_CASE_COUNT] = {
    {"candidate-clean", "bqcg0", false, BQCG_CLEAN},
    {"service-clean", "bqcg1", true, BQCG_CLEAN},
    {"candidate-add-service", "bqcg2", false, BQCG_ADD_SERVICE},
    {"candidate-add-root", "bqcg3", false, BQCG_ADD_ROOT},
    {"candidate-add-unrelated", "bqcg4", false, BQCG_ADD_OTHER},
    {"candidate-change-uid", "bqcg5", false, BQCG_CHANGE_UID},
    {"service-remove-candidate", "bqcg6", true, BQCG_REMOVE_CANDIDATE},
    {"candidate-change-gid", "bqcg7", false, BQCG_CHANGE_GID},
};

typedef struct BqCgIdentity
{
    uint32_t magic, version;
    uid_t real_uid, effective_uid, saved_uid, fs_uid;
    gid_t real_gid, effective_gid, saved_gid, fs_gid;
    int no_new_privs, group_count;
    gid_t groups[BQCG_GROUP_CAP];
} BqCgIdentity;

typedef struct BqCgPin
{
    uid_t uid;
    gid_t gid;
    gid_t groups[BQCG_GROUP_CAP];
    int count;
    char arguments[384];
} BqCgPin;

static bool bqcg_fixed_text(char const* path, char const* expected)
{
    struct stat info = {0};
    bool ok = lstat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == 0 &&
              info.st_gid == 0 && info.st_nlink == 1 && (info.st_mode & 07777) == 0644;
    int fd = ok ? open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    char actual[128] = {0};
    size_t length = strlen(expected);
    ssize_t read_count = fd >= 0 ? read(fd, actual, sizeof(actual)) : -1;
    if (fd >= 0) close(fd);
    ok = ok && read_count == (ssize_t)length && !memcmp(actual, expected, length);
    return ok;
}

static bool bqcg_container_guard(void)
{
    struct stat docker = {0}, running = {0}, installed = {0};
    /* /sbin/init may preserve the comm name "init". Bind PID1 to the installed
     * systemd inode rather than treating its mutable process name as identity. */
    bool manager = stat("/proc/1/exe", &running) == 0 &&
                   stat("/usr/lib/systemd/systemd", &installed) == 0 &&
                   S_ISREG(installed.st_mode) && installed.st_uid == 0 &&
                   (installed.st_mode & 0022) == 0 &&
                   running.st_dev == installed.st_dev && running.st_ino == installed.st_ino;
    bool marker = lstat("/.dockerenv", &docker) == 0 && S_ISREG(docker.st_mode);
    bool opt_in = bqcg_fixed_text(BQCG_OPT_IN, BQCG_OPT_IN_TEXT);
    bool ok = manager && marker && opt_in;
    if (!ok) fprintf(stderr, "BQCG_GUARD manager_inode=%d docker_marker=%d root_opt_in=%d\n",
                     manager, marker, opt_in);
    return ok;
}

static bool bqcg_root_binary(char const* path)
{
    struct stat info = {0}, parent = {0};
    bool ok = lstat("/usr/local/libexec", &parent) == 0 && S_ISDIR(parent.st_mode) &&
              parent.st_uid == 0 && (parent.st_mode & 0022) == 0 &&
              lstat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == 0 &&
              info.st_gid == 0 && info.st_nlink == 1 && (info.st_mode & 07777) == 0755;
    return ok;
}

/* Refuse to aim this test at an installed production service/build program.
 * Both fixed paths must be byte-for-byte copies of this harmless fixture. */
static bool bqcg_fixture_copy(char const* path)
{
    int original = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    int installed = original >= 0 ? open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat source = {0}, target = {0};
    bool ok = original >= 0 && installed >= 0 && fstat(original, &source) == 0 &&
              fstat(installed, &target) == 0 && S_ISREG(source.st_mode) && S_ISREG(target.st_mode) &&
              source.st_size > 0 && source.st_size <= 1048576 && source.st_size == target.st_size;
    char left[4096], right[4096];
    off_t remaining = source.st_size;
    while (ok && remaining > 0)
    {
        size_t chunk = remaining < (off_t)sizeof(left) ? (size_t)remaining : sizeof(left);
        ssize_t a = read(original, left, chunk), b = read(installed, right, chunk);
        ok = a == (ssize_t)chunk && b == a && !memcmp(left, right, chunk);
        if (ok) remaining -= (off_t)chunk;
    }
    if (original >= 0) close(original);
    if (installed >= 0) close(installed);
    return ok;
}

static bool bqcg_case_index(char const* text, int* output)
{
    bool ok = text && text[0] >= '0' && text[0] < '0' + BQCG_CASE_COUNT && text[1] == 0;
    if (ok) *output = text[0] - '0';
    return ok;
}

static void bqcg_sort(gid_t* groups, int count)
{
    for (int index = 1; index < count; index += 1)
    {
        gid_t value = groups[index];
        int position = index;
        while (position > 0 && groups[position - 1] > value)
        {
            groups[position] = groups[position - 1];
            position -= 1;
        }
        groups[position] = value;
    }
}

static bool bqcg_identity(BqCgIdentity* identity)
{
    *identity = (BqCgIdentity){.magic = 0x42435147u, .version = 1};
    bool ok = getresuid(&identity->real_uid, &identity->effective_uid, &identity->saved_uid) == 0 &&
              getresgid(&identity->real_gid, &identity->effective_gid, &identity->saved_gid) == 0;
    if (ok)
    {
        identity->fs_uid = setfsuid((uid_t)-1);
        identity->fs_gid = setfsgid((gid_t)-1);
        identity->no_new_privs = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
        identity->group_count = getgroups(BQCG_GROUP_CAP, identity->groups);
        ok = identity->no_new_privs == 1 && identity->group_count > 0 &&
             identity->group_count <= BQCG_GROUP_CAP;
        if (ok) bqcg_sort(identity->groups, identity->group_count);
        for (int index = 1; ok && index < identity->group_count; index += 1)
            ok = identity->groups[index - 1] != identity->groups[index];
    }
    return ok;
}

static bool bqcg_marker(char const* kind, int index)
{
    BqCgIdentity identity;
    bool ok = bqcg_identity(&identity);
    char path[160];
    int length = snprintf(path, sizeof(path), BQCG_ROOT "/%d/%s", index, kind);
    ok = ok && length > 0 && (size_t)length < sizeof(path);
    int fd = ok ? open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && fd >= 0;
    if (ok)
    {
        char const* bytes = (char const*)&identity;
        size_t remaining = sizeof(identity);
        while (ok && remaining)
        {
            ssize_t written = write(fd, bytes, remaining);
            ok = written > 0;
            if (ok) { remaining -= (size_t)written; bytes += written; }
        }
        ok = ok && fsync(fd) == 0;
    }
    if (fd >= 0 && close(fd) != 0) ok = false;
    return ok;
}

static bool bqcg_seconds_since(struct timespec const* start, int limit)
{
    struct timespec now = {0};
    bool expired = clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
                   now.tv_sec - start->tv_sec >= limit;
    return expired;
}

/* Commands are fixed systemd/NSS utilities, only after the Docker opt-in.
 * A separate process group lets a command timeout fail without an orphan. */
static int bqcg_command(char* const arguments[], char const* log_path, int limit)
{
    int output = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat info = {0};
    bool ok = output >= 0 && fstat(output, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_uid == 0 && (info.st_mode & 07777) == 0600;
    pid_t child = ok ? fork() : -1;
    if (child == 0)
    {
        setpgid(0, 0);
        dup2(output, STDOUT_FILENO);
        dup2(output, STDERR_FILENO);
        close(output);
        execv(arguments[0], arguments);
        _exit(127);
    }
    if (output >= 0) close(output);
    int result = -1;
    if (child > 0)
    {
        setpgid(child, child);
        struct timespec start = {0};
        bool timed = clock_gettime(CLOCK_MONOTONIC, &start) == 0;
        bool done = false;
        while (timed && !done)
        {
            int status = 0;
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child)
            {
                done = true;
                if (WIFEXITED(status)) result = WEXITSTATUS(status);
            }
            else if (waited < 0 && errno != EINTR)
                done = true;
            else if (bqcg_seconds_since(&start, limit))
                timed = false;
            else
                poll(NULL, 0, 50);
        }
        if (!done)
        {
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
            while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { }
            fprintf(stderr, "BQCG_COMMAND timeout or clock failure: %s\n", arguments[0]);
        }
    }
    return result;
}

static bool bqcg_group(char const* name, unsigned gid, char const* log)
{
    char number[16];
    snprintf(number, sizeof(number), "%u", gid);
    bool free_name = getgrnam(name) == NULL && getgrgid((gid_t)gid) == NULL;
    char* const command[] = {"/usr/sbin/groupadd", "-g", number, (char*)name, NULL};
    bool ok = free_name && bqcg_command(command, log, 15) == 0;
    return ok;
}

static bool bqcg_user(int index, char const* log)
{
    BqCgCase const* current = &bqcg_cases[index];
    char uid[16];
    snprintf(uid, sizeof(uid), "%u", 65440u + (unsigned)index);
    bool free_name = getpwnam(current->account) == NULL && getpwuid((uid_t)(65440 + index)) == NULL;
    char* const service[] = {"/usr/sbin/useradd", "-u", uid, "-g", "bqcg-service",
                            "-G", "bqcg-candidate", "-M", "-s", "/usr/sbin/nologin",
                            (char*)current->account, NULL};
    char* const candidate[] = {"/usr/sbin/useradd", "-u", uid, "-g", "bqcg-candidate",
                              "-M", "-s", "/usr/sbin/nologin", (char*)current->account, NULL};
    bool ok = free_name && bqcg_command(current->service ? service : candidate, log, 15) == 0;
    return ok;
}

static bool bqcg_pin(BqCgCase const* current, BqCgPin* pin)
{
    *pin = (BqCgPin){0};
    struct passwd* account = getpwnam(current->account);
    uid_t account_uid = account ? account->pw_uid : (uid_t)-1;
    gid_t account_gid = account ? account->pw_gid : (gid_t)-1;
    struct group* primary = getgrnam(current->service ? "bqcg-service" : "bqcg-candidate");
    bool ok = account != NULL && primary != NULL;
    if (ok)
    {
        gid_t named_gid = primary->gr_gid;
        pin->uid = account_uid;
        pin->gid = account_gid;
        pin->count = BQCG_GROUP_CAP;
        int found = getgrouplist(current->account, pin->gid, pin->groups, &pin->count);
        ok = found > 0 && found == pin->count && pin->count == (current->service ? 2 : 1) &&
             pin->gid == (gid_t)(current->service ? 65420 : 65421) &&
             named_gid == pin->gid &&
             pin->uid == (uid_t)(65440 + (current - bqcg_cases));
        if (ok) bqcg_sort(pin->groups, pin->count);
        if (ok) ok = pin->groups[0] == (gid_t)65420 + (current->service ? 0 : 1) &&
                     (pin->count == 1 || pin->groups[1] == (gid_t)65421);
    }
    size_t used = 0;
    for (int index = 0; ok && index < pin->count; index += 1)
    {
        int added = snprintf(pin->arguments + used, sizeof(pin->arguments) - used,
                             "%s%u", index ? "," : "", (unsigned)pin->groups[index]);
        ok = added > 0 && (size_t)added < sizeof(pin->arguments) - used;
        if (ok) used += (size_t)added;
    }
    return ok;
}

static bool bqcg_change(BqCgCase const* current, char const* log)
{
    char* const add_service[] = {"/usr/sbin/usermod", "-a", "-G", "bqcg-service", (char*)current->account, NULL};
    char* const add_root[] = {"/usr/sbin/usermod", "-a", "-G", "root", (char*)current->account, NULL};
    char* const add_other[] = {"/usr/sbin/usermod", "-a", "-G", "bqcg-other", (char*)current->account, NULL};
    char* const uid_change[] = {"/usr/sbin/usermod", "-u", "65460", (char*)current->account, NULL};
    char* const gid_change[] = {"/usr/sbin/groupmod", "-g", "65423", "bqcg-candidate", NULL};
    char* const remove_candidate[] = {"/usr/sbin/usermod", "-G", "", (char*)current->account, NULL};
    char* const* command = NULL;
    switch (current->change)
    {
        case BQCG_CLEAN: break;
        case BQCG_ADD_SERVICE: command = add_service; break;
        case BQCG_ADD_ROOT: command = add_root; break;
        case BQCG_ADD_OTHER: command = add_other; break;
        case BQCG_CHANGE_UID: command = uid_change; break;
        case BQCG_CHANGE_GID: command = gid_change; break;
        case BQCG_REMOVE_CANDIDATE: command = remove_candidate; break;
    }
    bool ok = !command || bqcg_command(command, log, 15) == 0;
    return ok;
}

static bool bqcg_resolved(BqCgCase const* current, BqCgPin const* pin, BqCgPin* resolved)
{
    struct passwd* account = getpwnam(current->account);
    uid_t account_uid = account ? account->pw_uid : (uid_t)-1;
    gid_t account_gid = account ? account->pw_gid : (gid_t)-1;
    struct group* named = getgrnam(current->service ? "bqcg-service" : "bqcg-candidate");
    bool ok = account != NULL && named != NULL;
    if (ok)
    {
        gid_t named_gid = named->gr_gid;
        *resolved = (BqCgPin){.uid = account_uid, .gid = named_gid,
                               .count = BQCG_GROUP_CAP};
        int found = getgrouplist(current->account, resolved->gid, resolved->groups, &resolved->count);
        ok = found > 0 && found == resolved->count && resolved->count > 0 &&
             resolved->count <= BQCG_GROUP_CAP;
        if (ok) bqcg_sort(resolved->groups, resolved->count);
        for (int index = 1; ok && index < resolved->count; index += 1)
            ok = resolved->groups[index - 1] != resolved->groups[index];
    }
    if (ok)
    {
        switch (current->change)
        {
            case BQCG_CLEAN:
                ok = resolved->uid == pin->uid && resolved->gid == pin->gid &&
                     resolved->count == pin->count;
                break;
            case BQCG_ADD_SERVICE:
                ok = resolved->uid == pin->uid && resolved->gid == pin->gid &&
                     resolved->count == 2 && resolved->groups[0] == 65420 && resolved->groups[1] == 65421;
                break;
            case BQCG_ADD_ROOT:
                ok = resolved->uid == pin->uid && resolved->gid == pin->gid &&
                     resolved->count == 2 && resolved->groups[0] == 0 && resolved->groups[1] == 65421;
                break;
            case BQCG_ADD_OTHER:
                ok = resolved->uid == pin->uid && resolved->gid == pin->gid &&
                     resolved->count == 2 && resolved->groups[0] == 65421 && resolved->groups[1] == 65422;
                break;
            case BQCG_CHANGE_UID:
                ok = resolved->uid == 65460 && resolved->gid == pin->gid &&
                     resolved->count == 1 && resolved->groups[0] == 65421;
                break;
            case BQCG_CHANGE_GID:
                ok = resolved->uid == pin->uid && resolved->gid == 65423 &&
                     account_gid == pin->gid &&
                     resolved->count == 1 && resolved->groups[0] == 65423;
                break;
            case BQCG_REMOVE_CANDIDATE:
                ok = resolved->uid == pin->uid && resolved->gid == pin->gid &&
                     resolved->count == 1 && resolved->groups[0] == 65420;
                break;
        }
    }
    if (ok && current->change == BQCG_CLEAN)
        for (int index = 0; index < pin->count; index += 1)
            ok = ok && resolved->groups[index] == pin->groups[index];
    return ok;
}

static bool bqcg_case_directory(int index, uid_t owner)
{
    char path[160];
    int length = snprintf(path, sizeof(path), BQCG_ROOT "/%d", index);
    bool ok = length > 0 && (size_t)length < sizeof(path) &&
              mkdir(path, 0700) == 0 && chown(path, owner, (gid_t)0) == 0;
    return ok;
}

static bool bqcg_read_marker(int index, char const* kind, uid_t owner, BqCgIdentity* identity)
{
    char path[160];
    int length = snprintf(path, sizeof(path), BQCG_ROOT "/%d/%s", index, kind);
    struct stat info = {0};
    bool ok = length > 0 && (size_t)length < sizeof(path) &&
              lstat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
              info.st_uid == owner && (info.st_mode & 07777) == 0600 && info.st_size == sizeof(*identity);
    int fd = ok ? open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ssize_t size = fd >= 0 ? read(fd, identity, sizeof(*identity)) : -1;
    if (fd >= 0) close(fd);
    ok = ok && size == (ssize_t)sizeof(*identity) && identity->magic == 0x42435147u &&
         identity->version == 1 && identity->group_count > 0 && identity->group_count <= BQCG_GROUP_CAP;
    return ok;
}

static bool bqcg_no_marker(int index)
{
    char path[160];
    int length = snprintf(path, sizeof(path), BQCG_ROOT "/%d/payload", index);
    struct stat info = {0};
    errno = 0;
    bool ok = length > 0 && (size_t)length < sizeof(path) &&
              lstat(path, &info) < 0 && errno == ENOENT;
    return ok;
}

static bool bqcg_same_identity(BqCgIdentity const* record, BqCgPin const* expected)
{
    bool ok = record->real_uid == expected->uid && record->effective_uid == expected->uid &&
              record->saved_uid == expected->uid && record->fs_uid == expected->uid &&
              record->real_gid == expected->gid && record->effective_gid == expected->gid &&
              record->saved_gid == expected->gid && record->fs_gid == expected->gid &&
              record->no_new_privs == 1 && record->group_count == expected->count;
    for (int index = 0; ok && index < expected->count; index += 1)
        ok = record->groups[index] == expected->groups[index];
    return ok;
}

static void bqcg_print_identity(char const* label, BqCgIdentity const* identity)
{
    printf("%s uid=%u/%u/%u fsuid=%u gid=%u/%u/%u fsgid=%u nnp=%d groups=",
           label, (unsigned)identity->real_uid, (unsigned)identity->effective_uid,
           (unsigned)identity->saved_uid, (unsigned)identity->fs_uid,
           (unsigned)identity->real_gid, (unsigned)identity->effective_gid,
           (unsigned)identity->saved_gid, (unsigned)identity->fs_gid, identity->no_new_privs);
    for (int index = 0; index < identity->group_count && index < BQCG_GROUP_CAP; index += 1)
        printf("%s%u", index ? "," : "", (unsigned)identity->groups[index]);
    printf("\n");
}

static bool bqcg_text(char const* path, char output[16384])
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat info = {0};
    bool ok = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_uid == 0 && info.st_size >= 0 && info.st_size < 16384;
    ssize_t size = ok ? read(fd, output, 16383) : -1;
    if (fd >= 0) close(fd);
    ok = ok && size == info.st_size;
    if (ok) output[size] = 0;
    return ok;
}

static bool bqcg_property(char const* text, char const* key, char const* value)
{
    size_t key_length = strlen(key), value_length = strlen(value);
    bool found = false, valid = true;
    char const* line = text;
    while (valid && *line)
    {
        char const* end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        if ((size_t)(end - line) > key_length && !memcmp(line, key, key_length) && line[key_length] == '=')
        {
            valid = !found && (size_t)(end - line) == key_length + 1 + value_length &&
                    !memcmp(line + key_length + 1, value, value_length);
            found = true;
        }
        line = *end ? end + 1 : end;
    }
    return found && valid;
}

static bool bqcg_property_contains(char const* text, char const* key, char const* value)
{
    size_t key_length = strlen(key);
    bool found = false, valid = true;
    char const* line = text;
    while (valid && *line)
    {
        char const* end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        if ((size_t)(end - line) > key_length && !memcmp(line, key, key_length) && line[key_length] == '=')
        {
            valid = !found && strstr(line + key_length + 1, value) != NULL &&
                    strstr(line + key_length + 1, value) < end;
            found = true;
        }
        line = *end ? end + 1 : end;
    }
    return found && valid;
}

/* The whole per-unit cgroup is either already gone or has zero tasks. The
 * expected path is built from our fixed transient unit, never from NSS. */
static bool bqcg_cgroup_empty(char const* show, char const* unit)
{
    char cgroup[160], procs[256];
    int length = snprintf(cgroup, sizeof(cgroup), "/system.slice/%s", unit);
    bool ok = length > 0 && (size_t)length < sizeof(cgroup) &&
              (bqcg_property(show, "ControlGroup", "") ||
               bqcg_property(show, "ControlGroup", cgroup));
    if (ok && !bqcg_property(show, "ControlGroup", ""))
    {
        length = snprintf(procs, sizeof(procs), "/sys/fs/cgroup%s/cgroup.procs", cgroup);
        ok = length > 0 && (size_t)length < sizeof(procs);
        errno = 0;
        int fd = ok ? open(procs, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
        int open_error = errno;
        char first = 0;
        ssize_t count = fd >= 0 ? read(fd, &first, 1) : -1;
        if (fd >= 0) close(fd);
        ok = ok && (fd >= 0 ? count == 0 : open_error == ENOENT);
    }
    return ok;
}

static bool bqcg_cgroup_gone(char const* unit)
{
    char path[256];
    int length = snprintf(path, sizeof(path), "/sys/fs/cgroup/system.slice/%s", unit);
    struct stat info = {0};
    errno = 0;
    bool ok = length > 0 && (size_t)length < sizeof(path) &&
              lstat(path, &info) < 0 && errno == ENOENT;
    return ok;
}

/* Stop and reset even if systemd-run or an earlier readback was uncertain.
 * Retain the pre-cleanup show separately and prove final exact-name unload or
 * inactivity plus cgroup removal. A failed cleanup is always a failed test. */
static bool bqcg_cleanup_unit(int index, bool probe, char const* unit, char const* log)
{
    char* const stop[] = {"/usr/bin/systemctl", "stop", (char*)unit, NULL};
    int stop_status = bqcg_command(stop, log, 10);
    char* const reset[] = {"/usr/bin/systemctl", "reset-failed", (char*)unit, NULL};
    int reset_status = bqcg_command(reset, log, 10);
    char path[160];
    snprintf(path, sizeof(path), BQCG_ROOT "/%d/%s-cleanup.show", index, probe ? "probe" : "gate");
    struct timespec start = {0};
    bool ok = clock_gettime(CLOCK_MONOTONIC, &start) == 0;
    bool complete = false, not_found = false;
    char show[16384] = {0};
    while (ok && !complete && !bqcg_seconds_since(&start, 10))
    {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) ok = false;
        if (fd >= 0) close(fd);
        char* const query[] = {
            "/usr/bin/systemctl", "show", "--no-pager",
            "--property=Id,LoadState,ActiveState,MainPID,ControlGroup", (char*)unit, NULL
        };
        if (ok) ok = bqcg_command(query, path, 5) == 0 && bqcg_text(path, show);
        not_found = ok && bqcg_property(show, "LoadState", "not-found");
        complete = ok && bqcg_property(show, "Id", unit) &&
                   (not_found || bqcg_property(show, "LoadState", "loaded")) &&
                   bqcg_property(show, "ActiveState", "inactive") &&
                   bqcg_property(show, "MainPID", "0") &&
                   bqcg_property(show, "ControlGroup", "") && bqcg_cgroup_gone(unit);
        if (ok && !complete) poll(NULL, 0, 100);
    }
    ok = ok && complete && (stop_status == 0 || not_found) &&
         (reset_status == 0 || not_found);
    printf("BQCG_CLEANUP unit=%s stop=%d reset=%d complete=%s cgroup_gone=%s status=%s\n%s",
           unit, stop_status, reset_status, complete ? "yes" : "no",
           bqcg_cgroup_gone(unit) ? "yes" : "no", ok ? "pass" : "fail", show);
    return ok;
}

/* RemainAfterExit holds successful transient units for exact readback. A
 * failed gate unit is held by its failure state. Both are reset below. */
static bool bqcg_unit(int index, bool probe, BqCgPin const* pin, char const* log)
{
    BqCgCase const* current = &bqcg_cases[index];
    char unit[80], unit_argument[96], index_argument[16], uid[16], gid[16];
    snprintf(unit, sizeof(unit), "bqcg-%d-%s.service", index, probe ? "probe" : "gate");
    snprintf(unit_argument, sizeof(unit_argument), "--unit=%s", unit);
    snprintf(index_argument, sizeof(index_argument), "%d", index);
    snprintf(uid, sizeof(uid), "%u", (unsigned)pin->uid);
    snprintf(gid, sizeof(gid), "%u", (unsigned)pin->gid);
    char user_property[80], group_property[80];
    snprintf(user_property, sizeof(user_property), "User=%s", current->account);
    snprintf(group_property, sizeof(group_property), "Group=%s",
             current->service ? "bqcg-service" : "bqcg-candidate");
    char* const probe_command[] = {
        "/usr/bin/systemd-run", "--quiet", "--service-type=exec", "--remain-after-exit",
        unit_argument, "--property", user_property, "--property", group_property,
        "--property", "NoNewPrivileges=yes",
        "--property", "CapabilityBoundingSet=", "--property", "AmbientCapabilities=",
        "--property", "RestrictSUIDSGID=yes", "--property", "TimeoutStartSec=10s",
        "--property", "RuntimeMaxSec=10s", "--property", "TimeoutStopSec=3s",
        (char*)(current->service ? BQCG_SERVICE : BQCG_BUILD), "observe", index_argument, NULL
    };
    char* const gate_command[] = {
        "/usr/bin/systemd-run", "--quiet", "--service-type=exec", "--remain-after-exit",
        unit_argument, "--property", user_property, "--property", group_property,
        "--property", "NoNewPrivileges=yes",
        "--property", "CapabilityBoundingSet=", "--property", "AmbientCapabilities=",
        "--property", "RestrictSUIDSGID=yes", "--property", "TimeoutStartSec=10s",
        "--property", "RuntimeMaxSec=10s", "--property", "TimeoutStopSec=3s",
        BQCG_GATE, current->service ? "0" : "3", uid, gid, (char*)pin->arguments,
        "--", (char*)(current->service ? BQCG_SERVICE : BQCG_BUILD),
        current->service ? "worker-unit" : "generate", index_argument, NULL
    };
    bool ok = bqcg_command(probe ? probe_command : gate_command, log, 20) == 0;
    char show_path[160];
    snprintf(show_path, sizeof(show_path), BQCG_ROOT "/%d/%s.show", index, probe ? "probe" : "gate");
    struct timespec start = {0};
    ok = ok && clock_gettime(CLOCK_MONOTONIC, &start) == 0;
    bool completed = false;
    char show[16384] = {0};
    while (ok && !completed && !bqcg_seconds_since(&start, 15))
    {
        int fd = open(show_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) ok = false;
        if (fd >= 0) close(fd);
        char* const show_command[] = {
            "/usr/bin/systemctl", "show", "--no-pager", "--property=Id,LoadState,ActiveState,Result,ExecMainCode,ExecMainStatus,MainPID,ControlGroup,User,Group,NoNewPrivileges,ExecStart",
            unit, NULL
        };
        if (ok) ok = bqcg_command(show_command, show_path, 5) == 0 && bqcg_text(show_path, show);
        completed = ok && bqcg_property(show, "Id", unit) &&
                    bqcg_property(show, "LoadState", "loaded") &&
                    bqcg_property(show, "ExecMainCode", "1") &&
                    bqcg_property(show, "ExecMainStatus", probe || current->change == BQCG_CLEAN ? "0" : "126") &&
                    bqcg_property(show, "Result", probe || current->change == BQCG_CLEAN ? "success" : "exit-code");
        if (ok && !completed) poll(NULL, 0, 100);
    }
    if (ok)
    {
        ok = completed && bqcg_property(show, "User", current->account) &&
             bqcg_property(show, "Group", current->service ? "bqcg-service" : "bqcg-candidate") &&
             bqcg_property(show, "NoNewPrivileges", "yes") &&
             bqcg_property(show, "MainPID", "0") && bqcg_cgroup_empty(show, unit) &&
             bqcg_property(show, "ActiveState", probe || current->change == BQCG_CLEAN ? "active" : "failed") &&
             bqcg_property_contains(show, "ExecStart", "path=" ) &&
             bqcg_property_contains(show, "ExecStart", probe ?
                                    (current->service ? BQCG_SERVICE : BQCG_BUILD) : BQCG_GATE);
    }
    if (ok && !probe)
    {
        BqBrokerRequest request = {.stage = current->service ? BQ_BROKER_OUTER : BQ_BROKER_CANDIDATE_GENERATE,
                                   .signal_number = BQ_BROKER_CONT};
        unsigned role = current->service ? 0u : 1u;
        BqBrokerStartGroups expected = {0};
        expected.uid[role] = pin->uid;
        expected.gid[role] = pin->gid;
        expected.count[role] = pin->count;
        for (int group = 0; group < pin->count; group += 1) expected.ids[role][group] = pin->groups[group];
        ok = bq_broker_gate_exec_identity(show, &request, &expected);
    }
    printf("BQCG_UNIT case=%s phase=%s submitted=%s complete=%s\n%s",
           current->name, probe ? "probe" : "gate", ok ? "yes" : "no",
           completed ? "yes" : "no", show);
    bool cleaned = bqcg_cleanup_unit(index, probe, unit, log);
    ok = ok && cleaned;
    return ok;
}

static bool bqcg_case(int index, char const* log)
{
    BqCgCase const* current = &bqcg_cases[index];
    BqCgPin pin = {0}, resolved = {0};
    bool ok = bqcg_pin(current, &pin);
    if (ok) ok = bqcg_change(current, log) && bqcg_resolved(current, &pin, &resolved) &&
                 bqcg_case_directory(index, resolved.uid);
    BqCgIdentity observed = {0}, payload = {0};
    if (ok) ok = bqcg_unit(index, true, &pin, log) &&
                 bqcg_read_marker(index, "observe", resolved.uid, &observed) &&
                 bqcg_same_identity(&observed, &resolved);
    if (ok) ok = bqcg_unit(index, false, &pin, log);
    if (ok && current->change == BQCG_CLEAN)
        ok = bqcg_read_marker(index, "payload", resolved.uid, &payload) &&
             bqcg_same_identity(&payload, &pin);
    else if (ok)
        ok = bqcg_no_marker(index);
    printf("BQCG_CASE name=%s stage=%u pinned_uid=%u pinned_gid=%u pinned_groups=%s resolved_uid=%u resolved_gid=%u status=%s payload=%s\n",
           current->name, current->service ? 0u : 1u, (unsigned)pin.uid, (unsigned)pin.gid,
           pin.arguments, (unsigned)resolved.uid, (unsigned)resolved.gid,
           ok ? "pass" : "fail", current->change == BQCG_CLEAN ? "expected" : "forbidden");
    if (observed.magic) bqcg_print_identity("BQCG_PID1_PROBE", &observed);
    if (payload.magic) bqcg_print_identity("BQCG_GATE_PAYLOAD", &payload);
    fflush(stdout);
    return ok;
}

static bool bqcg_run(void)
{
    bool ok = geteuid() == 0 && getuid() == 0 && bqcg_container_guard();
    char const* opt_in = getenv(BQCG_ENV);
    ok = ok && opt_in && strcmp(opt_in, BQCG_ENV_VALUE) == 0 &&
         bqcg_root_binary(BQCG_GATE) && bqcg_root_binary(BQCG_BUILD) &&
         bqcg_root_binary(BQCG_SERVICE) && bqcg_fixture_copy(BQCG_BUILD) &&
         bqcg_fixture_copy(BQCG_SERVICE) && mkdir(BQCG_ROOT, 0711) == 0 &&
         chmod(BQCG_ROOT, 0711) == 0;
    char log[] = BQCG_ROOT "/commands.log";
    if (ok)
        ok = bqcg_group("bqcg-service", 65420, log) &&
             bqcg_group("bqcg-candidate", 65421, log) &&
             bqcg_group("bqcg-other", 65422, log) && getgrgid((gid_t)65423) == NULL;
    for (int index = 0; ok && index < BQCG_CASE_COUNT; index += 1)
        ok = bqcg_user(index, log);
    int attempted = 0, failures = 0;
    if (ok)
    {
        /* A failed case may include uncertain manager cleanup. Do not start
         * another transient unit; outer Docker teardown owns that guest. */
        for (int index = 0; index < BQCG_CASE_COUNT && failures == 0; index += 1)
        {
            attempted += 1;
            if (!bqcg_case(index, log)) failures += 1;
        }
    }
    ok = ok && attempted == BQCG_CASE_COUNT && failures == 0;
    printf("BQCG_SYSTEMD_RESULT attempted=%d expected=%d failures=%d status=%s component-only=yes\n",
           attempted, BQCG_CASE_COUNT, failures, ok ? "pass" : "fail");
    return ok;
}

int main(int argc, char** argv)
{
    int result = 77;
    bool guarded = bqcg_container_guard();
    if (guarded && argc == 2 && !strcmp(argv[1], "--run"))
        result = bqcg_run() ? 0 : 1;
    else if (guarded && argc == 3)
    {
        int index = -1;
        if (bqcg_case_index(argv[2], &index))
        {
            if (!strcmp(argv[1], "observe")) result = bqcg_marker("observe", index) ? 0 : 1;
            else if ((!strcmp(argv[1], "generate") && !bqcg_cases[index].service) ||
                     (!strcmp(argv[1], "worker-unit") && bqcg_cases[index].service))
                result = bqcg_marker("payload", index) ? 0 : 1;
        }
    }
    if (result == 77) fprintf(stderr, "BQCG_ISOLATION_REFUSAL: disposable Docker/systemd opt-in and exact argv required\n");
    return result;
}
