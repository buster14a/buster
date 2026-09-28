/* Disposable real-systemd component proof for broker_entry_gate.c.
 *
 * --run is restricted to a fresh Docker guest with PID1 systemd, an exact
 * root-owned opt-in and synthetic fixed broker binary. It mutates ONLY that
 * guest. The real static gate is the first ExecStart in the positive and
 * ordinary negative units; the two FD-injection cases use this harmless
 * bridge to close FD0 or call the identical compiled gate main with CLOEXEC
 * set (an exec would clear that descriptor). The broker fixture never talks
 * to the manager and writes only a synthetic BEGIN and a bounded marker.
 * This is component evidence, not #1162 full-service/physical readiness.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/capability.h>
#include <poll.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define main bqeg_gate_main
#include "broker_entry_gate.c"
#undef main

#define BQEG_GATE "/usr/local/libexec/buster-bench-broker-entry-gate"
#define BQEG_BROKER "/usr/local/libexec/buster-bench-systemd-broker"
#define BQEG_SELF "/root/broker-entry-gate-systemd-test"
#define BQEG_BRIDGE "/usr/local/libexec/broker-entry-gate-systemd-test"
#define BQEG_OPT "/run/buster-bench-entry-test.opt-in"
#define BQEG_OPT_TEXT "buster14a/buster disposable broker-entry systemd component test\n"
#define BQEG_ROOT "/run/buster-bench-entry-test"
#define BQEG_MANAGER_TEXT "BQEG_PID1_VERIFIED_V1\n"
#define BQEG_UNIT "/etc/systemd/system/buster-bench-systemd-broker@.service"
#define BQEG_SOCKET_UNIT "buster-bench-systemd-broker.socket"
#define BQEG_ENV "BUSTER_BROKER_ENTRY_DISPOSABLE_SYSTEMD"
#define BQEG_ENV_TEXT "isolated-docker-systemd-test"
#define BQEG_CASES 25

typedef enum BqEgMutation
{
    BQEG_POSITIVE, BQEG_RECEIPT_ABSENT, BQEG_RECEIPT_MODE,
    BQEG_RECEIPT_SERVICE_GID, BQEG_PRIMARY_GID, BQEG_MISSING_GROUP, BQEG_EXTRA_GROUP,
    BQEG_BOUNDING_CAP, BQEG_AMBIENT_CAP, BQEG_NNP_OFF, BQEG_SECCOMP_OFF,
    BQEG_ROOT_RW, BQEG_SUBMOUNT_RW, BQEG_WRONG_FD0, BQEG_ABSENT_FD0,
    BQEG_CLOEXEC_FD0, BQEG_BROKER_MODE, BQEG_BROKER_ELF,
    BQEG_BROKER_STACK, BQEG_JOURNAL_FAIL, BQEG_NSSWITCH_MERGE, BQEG_NSCD_SOCKET,
    BQEG_CANDIDATE_UID, BQEG_RUNNER_GID, BQEG_ROOT_GROUP
} BqEgMutation;

static char const* const bqeg_names[BQEG_CASES] = {
    "positive", "receipt-absent", "receipt-mode", "receipt-service-gid",
    "primary-gid",
    "missing-group", "extra-group", "bounding-cap", "ambient-cap",
    "nnp-off", "seccomp-off", "root-rw", "protected-submount-rw",
    "wrong-fd0", "absent-fd0", "cloexec-fd0", "broker-mode",
    "broker-elf", "broker-exec-stack", "journal-send-fails", "nsswitch-merge", "nscd-socket",
    "candidate-uid-rebound", "runner-gid-rebound", "root-group"
};

static char const bqeg_receipt[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
    "candidate-uid=65001\ncandidate-gid=65001\nrunner-uid=65002\nrunner-gid=65002\n";
static int bqeg_nscd = -1;

typedef struct BqEgMarker
{
    uint32_t magic, version;
    int64_t pid;
    uint64_t ticks, socket_device, socket_inode;
    uid_t uids[4];
    gid_t gids[4], groups[8];
    int group_count, nnp, seccomp, type, cloexec, environment_count;
    uint32_t cap_effective[2], cap_permitted[2], cap_inheritable[2];
} BqEgMarker;

extern char** environ;

static bool bqeg_opt_in(bool require_manager)
{
    struct stat docker = {0}, opt = {0}, pid1 = {0}, installed = {0};
    char bytes[sizeof(BQEG_OPT_TEXT)] = {0};
    int fd = open(BQEG_OPT, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t size = fd >= 0 ? read(fd, bytes, sizeof(bytes)) : -1;
    if (fd >= 0) close(fd);
    bool manager = require_manager && stat("/proc/1/exe", &pid1) == 0 &&
        stat("/usr/lib/systemd/systemd", &installed) == 0 &&
        pid1.st_dev == installed.st_dev && pid1.st_ino == installed.st_ino;
    if (!require_manager)
    {
        char receipt[sizeof(BQEG_MANAGER_TEXT)] = {0};
        struct stat verified = {0};
        int marker = open(BQEG_ROOT "/manager-verified", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        ssize_t count = marker >= 0 ? read(marker, receipt, sizeof(receipt)) : -1;
        if (marker >= 0) close(marker);
        manager = lstat(BQEG_ROOT "/manager-verified", &verified) == 0 &&
            S_ISREG(verified.st_mode) && verified.st_uid == 0 && verified.st_gid == 0 &&
            verified.st_nlink == 1 && (verified.st_mode & 07777) == 0644 &&
            count == (ssize_t)sizeof(BQEG_MANAGER_TEXT) - 1 &&
            !memcmp(receipt, BQEG_MANAGER_TEXT, sizeof(BQEG_MANAGER_TEXT) - 1);
    }
    bool ok = lstat("/.dockerenv", &docker) == 0 && S_ISREG(docker.st_mode) &&
        lstat(BQEG_OPT, &opt) == 0 && S_ISREG(opt.st_mode) && opt.st_uid == 0 &&
        opt.st_gid == 0 && opt.st_nlink == 1 && (opt.st_mode & 07777) == 0644 &&
        size == (ssize_t)sizeof(BQEG_OPT_TEXT) - 1 &&
        !memcmp(bytes, BQEG_OPT_TEXT, sizeof(BQEG_OPT_TEXT) - 1) &&
        manager;
    return ok;
}

static bool bqeg_copy_matches(void)
{
    int a = open(BQEG_SELF, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int b = open(BQEG_BROKER, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat left = {0}, right = {0};
    bool ok = a >= 0 && b >= 0 && fstat(a, &left) == 0 && fstat(b, &right) == 0 &&
        S_ISREG(left.st_mode) && S_ISREG(right.st_mode) && left.st_uid == 0 &&
        right.st_uid == 0 && left.st_size > 0 && left.st_size < 1048576 &&
        left.st_size == right.st_size;
    off_t remaining = left.st_size;
    while (ok && remaining > 0)
    {
        char aa[4096], bb[4096];
        size_t count = remaining < (off_t)sizeof(aa) ? (size_t)remaining : sizeof(aa);
        ssize_t x = read(a, aa, count), y = read(b, bb, count);
        ok = x == (ssize_t)count && y == x && !memcmp(aa, bb, count);
        if (ok) remaining -= x;
    }
    if (a >= 0) close(a);
    if (b >= 0) close(b);
    return ok;
}

static bool bqeg_write_text(char const* path, char const* bytes, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    size_t size = strlen(bytes);
    bool ok = fd >= 0 && fchmod(fd, mode) == 0 &&
        write(fd, bytes, size) == (ssize_t)size && fsync(fd) == 0;
    if (fd >= 0 && close(fd) != 0) ok = false;
    return ok;
}

/* Fixed executable/arguments and bounded wait; command output is retained in
 * guest proof, never interpreted as permission to run an arbitrary command. */
static int bqeg_command(char* const argv[], char const* log, int limit)
{
    int output = open(log, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    pid_t child = output >= 0 ? fork() : -1;
    if (child == 0)
    {
        setpgid(0, 0);
        dup2(output, STDOUT_FILENO);
        dup2(output, STDERR_FILENO);
        close(output);
        execv(argv[0], argv);
        _exit(127);
    }
    if (output >= 0) close(output);
    int result = -1;
    if (child > 0)
    {
        setpgid(child, child);
        struct timespec start = {0}, now = {0};
        bool clock_ok = clock_gettime(CLOCK_MONOTONIC, &start) == 0;
        bool done = false;
        while (clock_ok && !done)
        {
            int status = 0;
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child)
            {
                done = true;
                if (WIFEXITED(status)) result = WEXITSTATUS(status);
            }
            else if (waited < 0 && errno != EINTR) done = true;
            else
            {
                clock_ok = clock_gettime(CLOCK_MONOTONIC, &now) == 0 &&
                    now.tv_sec - start.tv_sec < limit;
                if (clock_ok) poll(NULL, 0, 50);
            }
        }
        if (!done)
        {
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
            while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { }
        }
    }
    return result;
}

static bool bqeg_file(char const* path, char* output, size_t capacity)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat info = {0};
    bool ok = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
        info.st_uid == 0 && info.st_size >= 0 && (uint64_t)info.st_size < capacity;
    ssize_t count = ok ? read(fd, output, capacity - 1) : -1;
    if (fd >= 0) close(fd);
    ok = ok && count == info.st_size;
    if (ok) output[count] = 0;
    return ok;
}

static bool bqeg_fixture(void)
{
    BqEgMarker record = {.magic = 0x42454731u, .version = 1, .pid = getpid()};
    /* Mirror production BEGIN ordering. This is the first fixture operation,
     * before its own validation can reject a wrongly admitted gate state. */
    dprintf(STDERR_FILENO, "BQ-ENTRY-SYNTHETIC-BEGIN-V1 pid=%ld\n", (long)getpid());
    int entered = open(BQEG_ROOT "/entered", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    bool entered_ok = entered >= 0 && write(entered, &record.pid, sizeof(record.pid)) == sizeof(record.pid) &&
        fsync(entered) == 0;
    if (entered >= 0 && close(entered) != 0) entered_ok = false;
    struct stat socket = {0};
    int type = 0;
    socklen_t type_size = sizeof(type);
    char process[4096];
    size_t process_size = 0;
    bool guard = bqeg_opt_in(false);
    bool ok = entered_ok && guard && fstat(STDIN_FILENO, &socket) == 0 &&
        S_ISSOCK(socket.st_mode) && getsockopt(STDIN_FILENO, SOL_SOCKET, SO_TYPE,
        &type, &type_size) == 0 && type_size == sizeof(type) && type == SOCK_SEQPACKET &&
        bq_entry_read_path("/proc/self/stat", process, sizeof(process), &process_size) &&
        bq_entry_ticks(process, process_size, &record.ticks) &&
        getresuid(&record.uids[0], &record.uids[1], &record.uids[2]) == 0 &&
        getresgid(&record.gids[0], &record.gids[1], &record.gids[2]) == 0;
    if (ok)
    {
        record.uids[3] = (uid_t)setfsuid((uid_t)-1);
        record.gids[3] = (gid_t)setfsgid((gid_t)-1);
        record.group_count = getgroups(8, record.groups);
        record.nnp = prctl(PR_GET_NO_NEW_PRIVS, 0UL, 0UL, 0UL, 0UL);
        record.seccomp = prctl(PR_GET_SECCOMP, 0UL, 0UL, 0UL, 0UL);
        record.cloexec = fcntl(STDIN_FILENO, F_GETFD) & FD_CLOEXEC;
        record.type = type;
        record.socket_device = (uint64_t)socket.st_dev;
        record.socket_inode = (uint64_t)socket.st_ino;
        for (char** env = environ; *env && record.environment_count < 16; env += 1)
            record.environment_count += 1;
        struct __user_cap_header_struct header = {.version = _LINUX_CAPABILITY_VERSION_3};
        struct __user_cap_data_struct caps[2] = {{0}, {0}};
        ok = record.group_count == 2 && record.nnp == 1 && record.seccomp == 2 &&
            record.cloexec == 0 && record.environment_count == 3 &&
            syscall(SYS_capget, &header, caps) == 0;
        for (int index = 0; ok && index < 2; index += 1)
        {
            record.cap_effective[index] = caps[index].effective;
            record.cap_permitted[index] = caps[index].permitted;
            record.cap_inheritable[index] = caps[index].inheritable;
            ok = !caps[index].effective && !caps[index].permitted && !caps[index].inheritable;
        }
    }
    if (!ok)
        dprintf(STDERR_FILENO, "BQEG_SYNTHETIC_REFUSAL entered=%d guard=%d groups=%d nnp=%d seccomp=%d env=%d errno=%d\n",
                entered_ok, guard, record.group_count, record.nnp, record.seccomp,
                record.environment_count, errno);
    int fd = ok ? open(BQEG_ROOT "/marker", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && fd >= 0 && write(fd, &record, sizeof(record)) == sizeof(record) && fsync(fd) == 0;
    if (fd >= 0 && close(fd) != 0) ok = false;
    if (ok)
    {
        dprintf(STDERR_FILENO, "BQ-ENTRY-SYNTHETIC-SNAPSHOT-V1 pid=%ld ticks=%llu fd0=%llu:%llu\n",
                (long)record.pid, (unsigned long long)record.ticks,
                (unsigned long long)record.socket_device, (unsigned long long)record.socket_inode);
        static char const reply[] = "BQ-ENTRY-SYNTHETIC-OK\n";
        ok = send(STDIN_FILENO, reply, sizeof(reply) - 1, MSG_NOSIGNAL) == sizeof(reply) - 1;
    }
    if (!ok) dprintf(STDERR_FILENO, "BQEG_SYNTHETIC_RESULT failure errno=%d\n", errno);
    return ok;
}

static int bqeg_bridge(char const* mode)
{
    bool ok = bqeg_opt_in(false);
    struct stat info = {0};
    int type = 0;
    socklen_t length = sizeof(type);
    ok = ok && fstat(0, &info) == 0 && S_ISSOCK(info.st_mode) &&
        getsockopt(0, SOL_SOCKET, SO_TYPE, &type, &length) == 0 && type == SOCK_SEQPACKET;
    int result = 77;
    if (ok && !strcmp(mode, "absent"))
    {
        close(0);
        char* const args[] = {BQEG_GATE, NULL};
        execv(BQEG_GATE, args);
        result = 127;
    }
    else if (ok && !strcmp(mode, "cloexec"))
    {
        /* Exec clears a CLOEXEC FD before gate code can inspect it. Calling
         * the exact compiled gate main in this disposable process injects
         * that otherwise unrepresentable state into its real main path. */
        struct stat ignored = {0};
        result = fcntl(0, F_SETFD, FD_CLOEXEC) == 0 &&
            !bq_entry_connection(0, &ignored) ? bqeg_gate_main(1, NULL) : 127;
    }
    return result;
}

typedef struct BqEgProbe
{
    uint32_t magic;
    int nnp, seccomp, root_ro, submount_ro, bounding_chown, ambient_chown;
} BqEgProbe;

static bool bqeg_probe(void)
{
    BqEgProbe probe = {.magic = 0x42454750u,
        .nnp = prctl(PR_GET_NO_NEW_PRIVS, 0UL, 0UL, 0UL, 0UL),
        .seccomp = prctl(PR_GET_SECCOMP, 0UL, 0UL, 0UL, 0UL),
        .bounding_chown = prctl(PR_CAPBSET_READ, (unsigned long)CAP_CHOWN, 0UL, 0UL, 0UL),
        .ambient_chown = prctl(PR_CAP_AMBIENT, (unsigned long)PR_CAP_AMBIENT_IS_SET,
                              (unsigned long)CAP_CHOWN, 0UL, 0UL)};
    struct statvfs root = {0}, submount = {0};
    bool ok = bqeg_opt_in(false) && probe.nnp >= 0 && probe.seccomp >= 0 &&
        probe.bounding_chown >= 0 && probe.ambient_chown >= 0 &&
        statvfs("/", &root) == 0 && statvfs("/var/lib/buster-bench/sub", &submount) == 0;
    if (ok)
    {
        probe.root_ro = !!(root.f_flag & ST_RDONLY);
        probe.submount_ro = !!(submount.f_flag & ST_RDONLY);
    }
    int fd = ok ? open(BQEG_ROOT "/probe", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && fd >= 0 && write(fd, &probe, sizeof(probe)) == sizeof(probe) && fsync(fd) == 0;
    if (fd >= 0 && close(fd) != 0) ok = false;
    dprintf(STDERR_FILENO, "BQEG_SANDBOX_PROBE nnp=%d seccomp=%d root_ro=%d submount_ro=%d bounding_chown=%d ambient_chown=%d status=%d\n",
            probe.nnp, probe.seccomp, probe.root_ro, probe.submount_ro,
            probe.bounding_chown, probe.ambient_chown, ok);
    return ok;
}

static bool bqeg_unit(BqEgMutation kind)
{
    FILE* file = fopen(BQEG_UNIT, "w");
    bool ok = file != NULL;
    if (ok)
    {
        /* The installed unit grants exactly the service and candidate
         * groups; adding root's group 0 is a rejected mutation. */
        char const* groups = kind == BQEG_MISSING_GROUP ? "" :
            kind == BQEG_EXTRA_GROUP ? "buster-bench-candidate bqeg-extra" :
            kind == BQEG_ROOT_GROUP ? "root buster-bench-candidate" :
            "buster-bench-candidate";
        char const* exec = kind == BQEG_ABSENT_FD0 ? BQEG_BRIDGE " bridge absent" :
            kind == BQEG_CLOEXEC_FD0 ? BQEG_BRIDGE " bridge cloexec" : BQEG_GATE;
        char const* probe = kind == BQEG_NNP_OFF || kind == BQEG_SECCOMP_OFF ||
            kind == BQEG_ROOT_RW || kind == BQEG_SUBMOUNT_RW ||
            kind == BQEG_BOUNDING_CAP || kind == BQEG_AMBIENT_CAP ?
            "ExecStartPre=" BQEG_BRIDGE " probe\n" : "";
        char const* filters = kind == BQEG_SECCOMP_OFF ? "" :
            "RestrictSUIDSGID=yes\nRestrictAddressFamilies=AF_UNIX\n"
            "SystemCallArchitectures=native\nSystemCallFilter=@system-service\n"
            "SystemCallErrorNumber=EPERM\n";
        /* PrivateDevices installs an implicit @raw-io seccomp filter even
         * without SystemCallFilter. Omit only that implicit source here;
         * retain the other private namespaces and require measured mode 0. */
        char const* private_options = kind == BQEG_SECCOMP_OFF ?
            "PrivateTmp=yes\nPrivateNetwork=yes\nProtectHome=yes\n" :
            "PrivateTmp=yes\nPrivateDevices=yes\nPrivateNetwork=yes\nProtectHome=yes\n";
        fprintf(file, "[Unit]\nDescription=Disposable synthetic broker entry component\n"
            "[Service]\nType=exec\nRemainAfterExit=yes\nUser=root\nGroup=%s\n"
            "SupplementaryGroups=%s\nUMask=0077\n%sExecStart=%s\n"
            "StandardInput=%s\nStandardOutput=journal\nStandardError=%s\n"
            "WorkingDirectory=/\nRestart=no\nTimeoutStartSec=10s\nRuntimeMaxSec=10s\n"
            "NoNewPrivileges=%s\nCapabilityBoundingSet=%s\nAmbientCapabilities=%s\n"
            "%s"
            "ProtectSystem=%s\nReadOnlyPaths=/etc/buster-bench\n"
            "ReadOnlyPaths=/opt/buster-bench/installed\nReadOnlyPaths=/var/lib/buster-bench\n"
            "ReadWritePaths=" BQEG_ROOT "\n%s%s",
            kind == BQEG_PRIMARY_GID ? "root" : "buster-bench", groups, probe, exec,
            kind == BQEG_WRONG_FD0 ? "null" : "socket",
            kind == BQEG_JOURNAL_FAIL ? "null" : "journal",
            kind == BQEG_NNP_OFF ? "no" : "yes",
            kind == BQEG_BOUNDING_CAP || kind == BQEG_AMBIENT_CAP ? "CAP_CHOWN" : "",
            kind == BQEG_AMBIENT_CAP ? "CAP_CHOWN" : "",
            private_options,
            kind == BQEG_ROOT_RW ? "full" : "strict",
            kind == BQEG_SUBMOUNT_RW ? "ReadWritePaths=/var/lib/buster-bench/sub\n" : "",
            filters);
        bool written = ferror(file) == 0;
        if (fclose(file) != 0) written = false;
        ok = written;
    }
    return ok;
}

static bool bqeg_replace_broker(void)
{
    int source = open(BQEG_SELF, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int target = source >= 0 ? open(BQEG_BROKER, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool ok = source >= 0 && target >= 0 && fchmod(target, 0755) == 0;
    char bytes[4096];
    ssize_t count = 0;
    while (ok && (count = read(source, bytes, sizeof(bytes))) > 0)
        ok = write(target, bytes, (size_t)count) == count;
    ok = ok && count == 0 && fsync(target) == 0;
    if (source >= 0) close(source);
    if (target >= 0 && close(target) != 0) ok = false;
    return ok;
}

static bool bqeg_patch_broker(bool stack)
{
    int fd = open(BQEG_BROKER, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    Elf64_Ehdr header = {0};
    bool ok = fd >= 0 && pread(fd, &header, sizeof(header), 0) == sizeof(header) &&
        !memcmp(header.e_ident, ELFMAG, SELFMAG);
    if (ok && !stack)
    {
        char magic = 0;
        ok = pwrite(fd, &magic, 1, 0) == 1;
    }
    if (ok && stack)
    {
        bool found = false;
        for (unsigned i = 0; ok && i < header.e_phnum; i += 1)
        {
            Elf64_Phdr program = {0};
            off_t offset = (off_t)header.e_phoff + (off_t)i * sizeof(program);
            ok = pread(fd, &program, sizeof(program), offset) == sizeof(program);
            if (ok && program.p_type == PT_GNU_STACK)
            {
                program.p_flags |= PF_X;
                found = true;
                ok = pwrite(fd, &program, sizeof(program), offset) == sizeof(program);
            }
        }
        ok = ok && found;
    }
    if (ok) ok = fsync(fd) == 0;
    if (fd >= 0) close(fd);
    return ok;
}

static bool bqeg_nsswitch_merge(void)
{
    char original[8192], changed[8192];
    bool ok = bqeg_file("/etc/nsswitch.conf", original, sizeof(original));
    char* line = ok ? strstr(original, "\ngroup:") : NULL;
    char* end = line ? strchr(line + 1, '\n') : NULL;
    ok = ok && line && end;
    if (ok)
    {
        size_t before = (size_t)(line + 1 - original);
        int size = snprintf(changed, sizeof(changed), "%.*sgroup: files [SUCCESS=merge] systemd%s",
                            (int)before, original, end);
        ok = size > 0 && (size_t)size < sizeof(changed) &&
             bqeg_write_text("/etc/nsswitch.conf", changed, 0644);
    }
    return ok;
}

static bool bqeg_nscd_socket(void)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    static char const path[] = "/run/nscd/socket";
    bool ok = mkdir("/run/nscd", 0755) == 0;
    bqeg_nscd = ok ? socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0) : -1;
    memcpy(address.sun_path, path, sizeof(path));
    ok = ok && bqeg_nscd >= 0 &&
         bind(bqeg_nscd, (struct sockaddr*)&address,
              offsetof(struct sockaddr_un, sun_path) + sizeof(path)) == 0 &&
         listen(bqeg_nscd, 1) == 0;
    return ok;
}

static bool bqeg_mutate(BqEgMutation kind, char const* log)
{
    bool ok = true;
    if (kind == BQEG_RECEIPT_ABSENT) ok = rename(BQ_ENTRY_ACCOUNTS, BQ_ENTRY_ACCOUNTS ".held") == 0;
    if (kind == BQEG_RECEIPT_MODE) ok = chmod(BQ_ENTRY_ACCOUNTS, 0644) == 0;
    if (kind == BQEG_RECEIPT_SERVICE_GID)
    {
        char bytes[sizeof(bqeg_receipt)];
        memcpy(bytes, bqeg_receipt, sizeof(bytes));
        char* field = strstr(bytes, "service-gid=65000");
        ok = field != NULL;
        if (ok)
        {
            memcpy(field + strlen("service-gid="), "65003", 5);
            ok = bqeg_write_text(BQ_ENTRY_ACCOUNTS, bytes, 0444);
        }
    }
    if (kind == BQEG_CANDIDATE_UID)
    {
        char* const command[] = {"/usr/sbin/usermod", "-u", "65004", "buster-bench-candidate", NULL};
        ok = bqeg_command(command, log, 10) == 0;
    }
    if (kind == BQEG_RUNNER_GID)
    {
        char* const command[] = {"/usr/sbin/groupmod", "-g", "65005", "buster-github-runner", NULL};
        ok = bqeg_command(command, log, 10) == 0;
    }
    if (kind == BQEG_NSSWITCH_MERGE) ok = bqeg_nsswitch_merge();
    if (kind == BQEG_NSCD_SOCKET) ok = bqeg_nscd_socket();
    if (kind == BQEG_BROKER_MODE) ok = chmod(BQEG_BROKER, 0775) == 0;
    if (kind == BQEG_BROKER_ELF || kind == BQEG_BROKER_STACK)
        ok = bqeg_patch_broker(kind == BQEG_BROKER_STACK);
    return ok;
}

static bool bqeg_restore(BqEgMutation kind, char const* log)
{
    bool ok = true;
    if (kind == BQEG_RECEIPT_ABSENT) ok = rename(BQ_ENTRY_ACCOUNTS ".held", BQ_ENTRY_ACCOUNTS) == 0;
    if (kind == BQEG_RECEIPT_MODE) ok = chmod(BQ_ENTRY_ACCOUNTS, 0444) == 0;
    if (kind == BQEG_RECEIPT_SERVICE_GID) ok = bqeg_write_text(BQ_ENTRY_ACCOUNTS, bqeg_receipt, 0444);
    if (kind == BQEG_CANDIDATE_UID)
    {
        char* const command[] = {"/usr/sbin/usermod", "-u", "65001", "buster-bench-candidate", NULL};
        ok = bqeg_command(command, log, 10) == 0;
    }
    if (kind == BQEG_RUNNER_GID)
    {
        char* const command[] = {"/usr/sbin/groupmod", "-g", "65002", "buster-github-runner", NULL};
        ok = bqeg_command(command, log, 10) == 0;
    }
    if (kind == BQEG_NSSWITCH_MERGE)
    {
        char original[8192];
        ok = bqeg_file(BQEG_ROOT "/nsswitch-held", original, sizeof(original)) &&
             bqeg_write_text("/etc/nsswitch.conf", original, 0644);
    }
    if (kind == BQEG_NSCD_SOCKET)
    {
        if (bqeg_nscd >= 0) close(bqeg_nscd);
        bqeg_nscd = -1;
        ok = unlink("/run/nscd/socket") == 0 && rmdir("/run/nscd") == 0;
    }
    if (kind == BQEG_BROKER_MODE || kind == BQEG_BROKER_ELF || kind == BQEG_BROKER_STACK)
        ok = bqeg_replace_broker() && ok;
    return ok;
}

static bool bqeg_marker(BqEgMarker* marker)
{
    struct stat info = {0};
    bool ok = lstat(BQEG_ROOT "/marker", &info) == 0 && S_ISREG(info.st_mode) &&
        info.st_uid == 0 && info.st_nlink == 1 && info.st_size == sizeof(*marker);
    int fd = ok ? open(BQEG_ROOT "/marker", O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && fd >= 0 && read(fd, marker, sizeof(*marker)) == sizeof(*marker) &&
        marker->magic == 0x42454731u && marker->version == 1;
    if (fd >= 0) close(fd);
    return ok;
}

static bool bqeg_positive(char const* journal, char const* reply, char const* show, char const* unit)
{
    BqEgMarker marker = {0};
    char const* pass = strstr(journal, "BQ-BROKER-ENTRY-V1 PASS ");
    char const* begin = strstr(journal, "BQ-ENTRY-SYNTHETIC-BEGIN-V1 ");
    char const* snapshot = strstr(journal, "BQ-ENTRY-SYNTHETIC-SNAPSHOT-V1 ");
    long pid = 0, begin_pid = 0, snapshot_pid = 0;
    unsigned long long ticks = 0, begin_ticks = 0, device = 0, inode = 0;
    bool ok = bqeg_marker(&marker) && pass && begin && snapshot &&
        strstr(pass + 1, "BQ-BROKER-ENTRY-V1 PASS ") == NULL &&
        strstr(begin + 1, "BQ-ENTRY-SYNTHETIC-BEGIN-V1 ") == NULL &&
        strstr(snapshot + 1, "BQ-ENTRY-SYNTHETIC-SNAPSHOT-V1 ") == NULL &&
        pass < begin && begin < snapshot && !strcmp(reply, "BQ-ENTRY-SYNTHETIC-OK\n") &&
        sscanf(pass, "BQ-BROKER-ENTRY-V1 PASS boot=%*36s pid=%ld ticks=%llu", &pid, &ticks) == 2 &&
        sscanf(begin, "BQ-ENTRY-SYNTHETIC-BEGIN-V1 pid=%ld", &begin_pid) == 1 &&
        sscanf(snapshot, "BQ-ENTRY-SYNTHETIC-SNAPSHOT-V1 pid=%ld ticks=%llu fd0=%llu:%llu",
               &snapshot_pid, &begin_ticks, &device, &inode) == 4 &&
        pid == begin_pid && pid == snapshot_pid && ticks == begin_ticks &&
        pid == marker.pid && ticks == marker.ticks &&
        device == marker.socket_device &&
        inode == marker.socket_inode && marker.type == SOCK_SEQPACKET && marker.cloexec == 0 &&
        marker.nnp == 1 && marker.seccomp == 2 && marker.environment_count == 3 &&
        marker.group_count == 2;
    for (int i = 0; ok && i < 4; i += 1)
        ok = marker.uids[i] == 0 && marker.gids[i] == 65000;
    bool service = false, candidate = false;
    for (int i = 0; ok && i < 2; i += 1)
    {
        service |= marker.groups[i] == 65000;
        candidate |= marker.groups[i] == 65001;
        ok = marker.groups[i] == 65000 || marker.groups[i] == 65001;
    }
    ok = ok && service && candidate;
    for (int i = 0; ok && i < 2; i += 1)
        ok = !marker.cap_effective[i] && !marker.cap_permitted[i] && !marker.cap_inheritable[i];
    if (ok)
    {
        char socket_text[80], invocation_text[96], cgroup_text[512], pid_text[64];
        snprintf(socket_text, sizeof(socket_text), "socket=%llu:%llu ", device, inode);
        snprintf(pid_text, sizeof(pid_text), "ExecMainPID=%ld\n", pid);
        char const* invocation = strstr(show, "InvocationID=");
        char const* cgroup = strstr(pass, " cgroup=");
        char const* cgroup_end = cgroup ? strchr(cgroup + 8, ' ') : NULL;
        ok = invocation && strlen(unit) < 128 &&
            strlen(invocation) > strlen("InvocationID=") + 32 &&
            invocation[sizeof("InvocationID=") - 1 + 32] == '\n' &&
            cgroup_end && cgroup_end - (cgroup + 8) > (ptrdiff_t)strlen(unit) &&
            cgroup_end - (cgroup + 8) < 480;
        if (ok)
        {
            snprintf(invocation_text, sizeof(invocation_text), "invocation=%.*s cgroup=", 32,
                     invocation + sizeof("InvocationID=") - 1);
            snprintf(cgroup_text, sizeof(cgroup_text), "ControlGroup=%.*s\n",
                     (int)(cgroup_end - (cgroup + 8)), cgroup + 8);
            /* RemainAfterExit retains the manager unit after its last task;
             * systemd may already have deleted the empty cgroup, yielding an
             * empty terminal ControlGroup. The gate's live kernel path still
             * must end in this exact freshly attributed instance. */
            ok = strstr(pass, socket_text) != NULL && strstr(pass, invocation_text) != NULL &&
                (strstr(show, cgroup_text) != NULL || strstr(show, "ControlGroup=\n") != NULL) &&
                strstr(show, pid_text) != NULL &&
                !memcmp(cgroup_end - strlen(unit), unit, strlen(unit));
        }
    }
    printf("BQEG_POSITIVE pid=%ld ticks=%llu fd0=%llu:%llu match=%d synthetic=1\n",
           pid, ticks, device, inode, ok);
    return ok;
}

static bool bqeg_cursor(char output[256], char const* log)
{
    char* const sync[] = {"/usr/bin/journalctl", "--sync", NULL};
    char* const cursor[] = {"/usr/bin/journalctl", "--no-pager", "--show-cursor", "-n", "1", "-o", "cat", NULL};
    char bytes[4096];
    bool ok = bqeg_command(sync, log, 10) == 0 && bqeg_command(cursor, log, 10) == 0 &&
        bqeg_file(log, bytes, sizeof(bytes));
    char* marker = ok ? strstr(bytes, "-- cursor: ") : NULL;
    char* end = marker ? strchr(marker, '\n') : NULL;
    ok = ok && marker && end && end - marker > 11 && end - marker - 11 < 256;
    if (ok)
    {
        memcpy(output, marker + 11, (size_t)(end - marker - 11));
        output[end - marker - 11] = 0;
    }
    return ok;
}

static bool bqeg_connect(char reply[128])
{
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path, BQ_ENTRY_SOCKET, sizeof(BQ_ENTRY_SOCKET));
    socklen_t length = offsetof(struct sockaddr_un, sun_path) + sizeof(BQ_ENTRY_SOCKET);
    bool ok = fd >= 0 && connect(fd, (struct sockaddr*)&address, length) == 0;
    if (ok)
    {
        struct pollfd ready = {.fd = fd, .events = POLLIN | POLLHUP};
        int polled = poll(&ready, 1, 10000);
        ssize_t count = polled > 0 ? recv(fd, reply, 127, 0) : -1;
        ok = count >= 0;
        if (ok) reply[count] = 0;
    }
    if (fd >= 0) close(fd);
    return ok;
}

static bool bqeg_fresh_unit(char const* before, char const* after, char unit[128])
{
    unsigned fresh = 0;
    char const* line = after;
    bool ok = true;
    while (ok && *line)
    {
        char const* end_line = strchr(line, '\n');
        if (!end_line) end_line = line + strlen(line);
        char const* at = strstr(line, "buster-bench-systemd-broker@");
        if (at && at < end_line)
        {
            char const* end = strstr(at, ".service");
            ok = end && end + 8 <= end_line && end + 8 - at < 128;
            if (ok)
            {
                char name[128];
                memcpy(name, at, (size_t)(end + 8 - at));
                name[end + 8 - at] = 0;
                if (!strstr(before, name))
                {
                    fresh += 1;
                    memcpy(unit, name, strlen(name) + 1);
                }
            }
        }
        line = *end_line ? end_line + 1 : end_line;
    }
    return ok && fresh == 1;
}

static bool bqeg_current_show(char const* show, char const* unit, uint64_t since)
{
    static char const key[] = "ExecMainStartTimestampMonotonic=";
    char const* at = strstr(show, key);
    char const* end = at ? strchr(at, '\n') : NULL;
    uint64_t started = 0;
    char const* invocation = strstr(show, "InvocationID=");
    char unit_text[160];
    snprintf(unit_text, sizeof(unit_text), "Id=%s\n", unit);
    bool ok = at && end && bq_entry_number(at + sizeof(key) - 1,
             (size_t)(end - (at + sizeof(key) - 1)), &started) && started >= since &&
        invocation && bq_entry_hex(invocation + sizeof("InvocationID=") - 1, 32, false) &&
        invocation[sizeof("InvocationID=") - 1 + 32] == '\n' &&
        strstr(show, unit_text) != NULL;
    return ok;
}

static bool bqeg_case(BqEgMutation kind)
{
    char prefix[160], command_log[200], cursor_log[200], journal_log[200],
         journal_export_log[200], before_log[200], units_log[200], show_log[200], cleanup_log[200];
    snprintf(prefix, sizeof(prefix), BQEG_ROOT "/%02d-%s", kind, bqeg_names[kind]);
    snprintf(command_log, sizeof(command_log), "%s-mutation.log", prefix);
    snprintf(cursor_log, sizeof(cursor_log), "%s-cursor.txt", prefix);
    snprintf(journal_log, sizeof(journal_log), "%s-journal.cat", prefix);
    snprintf(journal_export_log, sizeof(journal_export_log), "%s-journal.export", prefix);
    snprintf(before_log, sizeof(before_log), "%s-before-units.txt", prefix);
    snprintf(units_log, sizeof(units_log), "%s-units.txt", prefix);
    snprintf(show_log, sizeof(show_log), "%s-show.txt", prefix);
    snprintf(cleanup_log, sizeof(cleanup_log), "%s-cleanup.txt", prefix);
    unlink(BQEG_ROOT "/marker");
    unlink(BQEG_ROOT "/entered");
    unlink(BQEG_ROOT "/probe");
    char* const reload[] = {"/usr/bin/systemctl", "daemon-reload", NULL};
    char* const start[] = {"/usr/bin/systemctl", "start", BQEG_SOCKET_UNIT, NULL};
    char* const stop[] = {"/usr/bin/systemctl", "stop", BQEG_SOCKET_UNIT, NULL};
    char* const list[] = {"/usr/bin/systemctl", "list-units", "--all", "--plain", "--no-legend",
                          "buster-bench-systemd-broker@*.service", NULL};
    char* const sync[] = {"/usr/bin/journalctl", "--sync", NULL};
    /* The first failing step and its errno are reported with the case, so a
     * harness failure is distinguishable from a gate or broker outcome. */
    int failed_step = 0, failed_errno = 0;
    bool ok = bqeg_unit(kind) && bqeg_command(reload, command_log, 10) == 0 &&
        bqeg_mutate(kind, command_log);
    if (!ok && !failed_step) { failed_step = 1; failed_errno = errno; }
    char cursor[256] = {0};
    char before[8192] = {0};
    struct timespec clock = {0};
    if (ok) ok = bqeg_cursor(cursor, cursor_log) &&
                 bqeg_command(list, before_log, 10) == 0 &&
                 bqeg_file(before_log, before, sizeof(before)) &&
                 clock_gettime(CLOCK_MONOTONIC, &clock) == 0 &&
                 bqeg_command(start, command_log, 10) == 0;
    if (!ok && !failed_step) { failed_step = 2; failed_errno = errno; }
    char reply[128] = {0};
    if (ok) ok = bqeg_connect(reply);
    if (!ok && !failed_step) { failed_step = 3; failed_errno = errno; }
    if (ok) ok = bqeg_command(list, units_log, 10) == 0;
    char units[8192] = {0}, unit[128] = {0};
    if (ok)
    {
        ok = bqeg_file(units_log, units, sizeof(units)) && bqeg_fresh_unit(before, units, unit);
    }
    if (!ok && !failed_step) { failed_step = 4; failed_errno = errno; }
    if (ok)
    {
        char* const show[] = {"/usr/bin/systemctl", "show", "--all", "--no-pager", unit, NULL};
        ok = bqeg_command(show, show_log, 10) == 0;
    }
    char after[300];
    snprintf(after, sizeof(after), "--after-cursor=%s", cursor);
    char* const journal[] = {"/usr/bin/journalctl", "--no-pager", "-o", "cat", after, NULL};
    char* const export[] = {"/usr/bin/journalctl", "--no-pager", "-o", "export", after, NULL};
    if (ok) ok = bqeg_command(sync, command_log, 10) == 0 &&
                 bqeg_command(journal, journal_log, 10) == 0 &&
                 bqeg_command(export, journal_export_log, 10) == 0;
    char record[131072] = {0}, show[65536] = {0};
    if (ok) ok = bqeg_file(journal_log, record, sizeof(record)) && bqeg_file(show_log, show, sizeof(show));
    uint64_t since = (uint64_t)clock.tv_sec * 1000000u + (uint64_t)clock.tv_nsec / 1000u;
    if (ok) ok = bqeg_current_show(show, unit, since) && strstr(record, unit) != NULL;
    bool pass = strstr(record, "BQ-BROKER-ENTRY-V1 PASS ") != NULL;
    bool begin = strstr(record, "BQ-ENTRY-SYNTHETIC-BEGIN-V1 ") != NULL;
    struct stat marker = {0};
    errno = 0;
    bool no_marker = lstat(BQEG_ROOT "/marker", &marker) < 0 && errno == ENOENT;
    errno = 0;
    bool no_entered = lstat(BQEG_ROOT "/entered", &marker) < 0 && errno == ENOENT;
    if (ok && (kind == BQEG_NNP_OFF || kind == BQEG_SECCOMP_OFF ||
               kind == BQEG_ROOT_RW || kind == BQEG_SUBMOUNT_RW ||
               kind == BQEG_BOUNDING_CAP || kind == BQEG_AMBIENT_CAP))
    {
        BqEgProbe probe = {0};
        struct stat info = {0};
        int fd = open(BQEG_ROOT "/probe", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        ok = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
            info.st_uid == 0 && info.st_size == sizeof(probe) &&
            read(fd, &probe, sizeof(probe)) == sizeof(probe) && probe.magic == 0x42454750u;
        if (fd >= 0) close(fd);
        if (ok)
        {
            switch (kind)
            {
                case BQEG_NNP_OFF: ok = probe.nnp == 0 && probe.seccomp == 2; break;
                case BQEG_SECCOMP_OFF: ok = probe.nnp == 1 && probe.seccomp == 0; break;
                case BQEG_ROOT_RW: ok = probe.root_ro == 0; break;
                case BQEG_SUBMOUNT_RW: ok = probe.root_ro == 1 && probe.submount_ro == 0; break;
                case BQEG_BOUNDING_CAP: ok = probe.bounding_chown == 1; break;
                case BQEG_AMBIENT_CAP: ok = probe.ambient_chown == 1; break;
                default: ok = false; break;
            }
        }
        printf("BQEG_PROBE name=%s nnp=%d seccomp=%d root_ro=%d submount_ro=%d bounding_chown=%d ambient_chown=%d target=%d\n",
               bqeg_names[kind], probe.nnp, probe.seccomp, probe.root_ro,
               probe.submount_ro, probe.bounding_chown, probe.ambient_chown, ok);
    }
    if (ok && kind == BQEG_POSITIVE)
        ok = !no_entered && bqeg_positive(record, reply, show, unit) &&
             strstr(show, "ExecMainStatus=0\n") != NULL &&
             strstr(show, "Result=success\n") != NULL;
    else if (ok)
        ok = !pass && !begin && no_marker && no_entered && !reply[0] &&
             strstr(show, "ExecMainStatus=126\n") != NULL &&
             strstr(show, "Result=exit-code\n") != NULL;
    printf("BQEG_CASE name=%s unit=%s pass=%d synthetic_begin=%d entered=%d marker=%d status=%s "
           "failed_step=%d errno=%d\n",
           bqeg_names[kind], unit, pass, begin, !no_entered, !no_marker, ok ? "pass" : "fail",
           ok ? 0 : failed_step, ok ? 0 : failed_errno);
    fflush(stdout);
    bool clean = bqeg_command(stop, cleanup_log, 10) == 0;
    if (unit[0])
    {
        char* const stop_unit[] = {"/usr/bin/systemctl", "stop", unit, NULL};
        char* const reset[] = {"/usr/bin/systemctl", "reset-failed", unit, NULL};
        char* const final_show[] = {"/usr/bin/systemctl", "show", "--no-pager",
            "--property=Id,LoadState,ActiveState,MainPID,ControlGroup", unit, NULL};
        clean = bqeg_command(stop_unit, cleanup_log, 10) == 0 && clean;
        int reset_status = bqeg_command(reset, cleanup_log, 10);
        clean = bqeg_command(final_show, cleanup_log, 10) == 0 && clean;
        char final[8192] = {0};
        clean = bqeg_file(cleanup_log, final, sizeof(final)) && clean;
        bool unloaded = (strstr(final, "LoadState=not-found\n") != NULL ||
            strstr(final, "LoadState=loaded\n") != NULL) &&
            strstr(final, "ActiveState=inactive\n") != NULL &&
            strstr(final, "MainPID=0\n") != NULL &&
            strstr(final, "ControlGroup=\n") != NULL;
        clean = clean && unloaded && (reset_status == 0 ||
            (strstr(final, "Failed to reset failed state of unit ") != NULL &&
             strstr(final, "not loaded.") != NULL));
    }
    clean = bqeg_restore(kind, command_log) && clean;
    printf("BQEG_CLEANUP name=%s status=%s\n", bqeg_names[kind], clean ? "pass" : "fail");
    return ok && clean;
}

static bool bqeg_run(void)
{
    char const* opt = getenv(BQEG_ENV);
    static char const* const names[] = {"buster-bench", "buster-bench-candidate", "buster-github-runner"};
    bool roles = true;
    for (unsigned index = 0; roles && index < 3; index += 1)
    {
        struct passwd* user = getpwnam(names[index]);
        roles = user && user->pw_uid == 65000u + index && user->pw_gid == 65000u + index;
        struct group* group = getgrnam(names[index]);
        roles = roles && group && group->gr_gid == 65000u + index;
    }
    bool ok = getuid() == 0 && geteuid() == 0 && bqeg_opt_in(true) && opt &&
        !strcmp(opt, BQEG_ENV_TEXT) && roles && bqeg_copy_matches() &&
        mkdir(BQEG_ROOT, 0700) == 0 &&
        bqeg_write_text(BQ_ENTRY_ACCOUNTS, bqeg_receipt, 0444) &&
        bqeg_write_text(BQEG_ROOT "/manager-verified", BQEG_MANAGER_TEXT, 0644);
    char nsswitch[8192];
    if (ok) ok = bqeg_file("/etc/nsswitch.conf", nsswitch, sizeof(nsswitch)) &&
                 bqeg_write_text(BQEG_ROOT "/nsswitch-held", nsswitch, 0600);
    int attempted = 0;
    for (int index = 0; ok && index < BQEG_CASES; index += 1)
    {
        attempted += 1;
        ok = bqeg_case((BqEgMutation)index);
    }
    printf("BQEG_SYSTEMD_RESULT attempted=%d expected=%d status=%s synthetic_component=1\n",
           attempted, BQEG_CASES, ok ? "pass" : "fail");
    return ok && attempted == BQEG_CASES;
}

int main(int argc, char** argv)
{
    int result = 77;
    if (argc == 2 && !strcmp(argv[1], "--run")) result = bqeg_run() ? 0 : 1;
    else if (argc == 2 && !strcmp(argv[1], "serve-connection")) result = bqeg_fixture() ? 0 : 1;
    else if (argc == 3 && !strcmp(argv[1], "bridge")) result = bqeg_bridge(argv[2]);
    else if (argc == 2 && !strcmp(argv[1], "probe")) result = bqeg_probe() ? 0 : 1;
    if (result == 77) fprintf(stderr, "BQEG_ISOLATION_REFUSAL: exact disposable guest and argv required\n");
    return result;
}
