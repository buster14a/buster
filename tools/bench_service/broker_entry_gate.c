/* Static first executable for the fixed root broker, prospective #1162 gate.
 * main owns the fail-closed order and same-PID descriptor exec. accounts and
 * identity check the numeric installation authority; privileges, mounts and
 * connection check inherited confinement; evidence binds the pre-exec state.
 * No NSS, request parsing, manager command, privilege transition or path/argv
 * selection is exposed here. Installation and complete evidence reconciliation
 * are separate mandatory authorities; see deploy/SYSTEMD_BROKER.md.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/capability.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/xattr.h>
#include <unistd.h>

#define BQ_ENTRY_BROKER "/usr/local/libexec/buster-bench-systemd-broker"
#define BQ_ENTRY_ACCOUNTS "/etc/buster-bench/systemd-broker-accounts.identity"
#define BQ_ENTRY_SOCKET "/run/buster-bench-systemd-broker/control.sock"
#define BQ_ENTRY_GROUP_LIMIT 32
#define BQ_ENTRY_PATH_LIMIT 512
#define BQ_ENTRY_MOUNT_LIMIT (128 * 1024)

typedef struct BqEntryAccounts
{
    uint32_t ids[6]; /* service, candidate, runner; UID followed by GID */
} BqEntryAccounts;

typedef struct BqEntryIdentity
{
    uid_t uids[4]; /* real, effective, saved, filesystem */
    gid_t gids[4];
    gid_t groups[BQ_ENTRY_GROUP_LIMIT];
    int group_count;
} BqEntryIdentity;

static bool bq_entry_number(char const* text, size_t size, uint64_t* output)
{
    uint64_t value = 0;
    bool ok = size > 0 && size <= 20 && (text[0] != '0' || size == 1);
    for (size_t index = 0; ok && index < size; index += 1)
    {
        unsigned digit = (unsigned)(text[index] - '0');
        ok = digit <= 9 && value <= (UINT64_MAX - digit) / 10;
        if (ok) value = value * 10 + digit;
    }
    if (ok) *output = value;
    return ok;
}

static bool bq_entry_accounts_parse(char const* bytes, size_t size, BqEntryAccounts* accounts)
{
    static char const* const names[] = {"service-uid=", "service-gid=", "candidate-uid=",
                                        "candidate-gid=", "runner-uid=", "runner-gid="};
    static char const header[] = "BQ-ACCOUNTS-V1\n";
    BqEntryAccounts parsed = {0};
    size_t offset = sizeof(header) - 1;
    bool ok = size > offset && size < 256 && !memcmp(bytes, header, offset);
    for (unsigned index = 0; ok && index < 6; index += 1)
    {
        size_t prefix = strlen(names[index]);
        ok = prefix < size - offset && !memcmp(bytes + offset, names[index], prefix);
        if (ok)
        {
            offset += prefix;
            size_t end = offset;
            while (end < size && bytes[end] != '\n') end += 1;
            uint64_t value = 0;
            ok = end < size && bq_entry_number(bytes + offset, end - offset, &value) &&
                 value > 0 && value < UINT32_MAX;
            if (ok) parsed.ids[index] = (uint32_t)value;
            offset = end + 1;
        }
    }
    ok = ok && offset == size;
    for (unsigned index = 0; ok && index < 4; index += 1)
        for (unsigned other = index + 2; ok && other < 6; other += 2)
            ok = parsed.ids[index] != parsed.ids[other];
    if (ok) *accounts = parsed;
    return ok;
}

static bool bq_entry_same_file(struct stat const* before, struct stat const* after)
{
    return before->st_dev == after->st_dev && before->st_ino == after->st_ino &&
           before->st_mode == after->st_mode && before->st_uid == after->st_uid &&
           before->st_gid == after->st_gid && before->st_nlink == after->st_nlink &&
           before->st_size == after->st_size &&
           before->st_mtim.tv_sec == after->st_mtim.tv_sec &&
           before->st_mtim.tv_nsec == after->st_mtim.tv_nsec &&
           before->st_ctim.tv_sec == after->st_ctim.tv_sec &&
           before->st_ctim.tv_nsec == after->st_ctim.tv_nsec;
}

/* Component walking also rejects symlinks in the ancestry. State mount probes
 * permit non-root directory ownership; immutable installation opens do not. */
static int bq_entry_open(char const* path, int flags, bool root_ancestry)
{
    size_t size = strlen(path);
    bool ok = size > 1 && size < BQ_ENTRY_PATH_LIMIT && path[0] == '/';
    int directory = ok ? open("/", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && directory >= 0;
    size_t offset = 1;
    int result = -1;
    while (ok && offset < size)
    {
        struct stat info;
        ok = fstat(directory, &info) == 0 && S_ISDIR(info.st_mode) &&
             (!root_ancestry || (info.st_uid == 0 && !(info.st_mode & 0022)));
        size_t end = offset;
        while (end < size && path[end] != '/') end += 1;
        char component[BQ_ENTRY_PATH_LIMIT];
        size_t count = end - offset;
        if (ok)
        {
            memcpy(component, path + offset, count);
            component[count] = 0;
            ok = count > 0 && strcmp(component, ".") && strcmp(component, "..");
        }
        if (ok)
        {
            int next = openat(directory, component,
                              (end == size ? flags : O_PATH | O_DIRECTORY) | O_CLOEXEC | O_NOFOLLOW);
            ok = next >= 0;
            if (end == size) result = next;
            else
            {
                close(directory);
                directory = next;
            }
        }
        offset = end + 1;
    }
    if (directory >= 0) close(directory);
    return result;
}

static bool bq_entry_read(int descriptor, char* bytes, size_t capacity, size_t* size)
{
    size_t used = 0;
    bool ok = descriptor >= 0 && capacity > 1;
    bool done = false;
    /* Any error (including EINTR), oversized record or unterminated read fails.
     * There is no unbounded retry path in the gate. All inputs are local files. */
    while (ok && !done)
    {
        ssize_t count = read(descriptor, bytes + used, capacity - used);
        ok = count >= 0 && (size_t)count < capacity - used;
        if (ok)
        {
            used += (size_t)count;
            done = count == 0;
        }
    }
    if (ok)
    {
        bytes[used] = 0;
        *size = used;
    }
    return ok;
}

static bool bq_entry_read_path(char const* path, char* bytes, size_t capacity, size_t* size)
{
    int descriptor = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    bool ok = bq_entry_read(descriptor, bytes, capacity, size);
    if (descriptor >= 0) close(descriptor);
    return ok;
}

static bool bq_entry_accounts(BqEntryAccounts* accounts, struct stat* identity)
{
    int descriptor = bq_entry_open(BQ_ENTRY_ACCOUNTS, O_RDONLY | O_NONBLOCK, true);
    struct stat before = {0}, after = {0};
    char bytes[256];
    size_t size = 0;
    bool ok = descriptor >= 0 && fstat(descriptor, &before) == 0 &&
              S_ISREG(before.st_mode) && before.st_uid == 0 && before.st_nlink == 1 &&
              (before.st_mode & 07777) == 0444 && before.st_size > 0 && before.st_size < 256 &&
              bq_entry_read(descriptor, bytes, sizeof(bytes), &size) &&
              size == (size_t)before.st_size && fstat(descriptor, &after) == 0 &&
              bq_entry_same_file(&before, &after) && bq_entry_accounts_parse(bytes, size, accounts);
    if (ok) *identity = before;
    if (descriptor >= 0) close(descriptor);
    return ok;
}

static bool bq_entry_identity_valid(BqEntryAccounts const* accounts, BqEntryIdentity const* actual)
{
    bool ok = actual->group_count == 3;
    for (unsigned index = 0; ok && index < 4; index += 1)
        ok = actual->uids[index] == 0 && actual->gids[index] == accounts->ids[1];
    gid_t expected[] = {0, (gid_t)accounts->ids[1], (gid_t)accounts->ids[3]};
    for (unsigned index = 0; ok && index < 3; index += 1)
    {
        unsigned matches = 0;
        for (int other = 0; other < actual->group_count; other += 1)
            matches += actual->groups[other] == expected[index];
        ok = matches == 1;
    }
    return ok;
}

static bool bq_entry_identity(BqEntryAccounts const* accounts)
{
    BqEntryIdentity actual = {0};
    bool ok = getresuid(&actual.uids[0], &actual.uids[1], &actual.uids[2]) == 0 &&
              getresgid(&actual.gids[0], &actual.gids[1], &actual.gids[2]) == 0;
    if (ok)
    {
        actual.uids[3] = (uid_t)setfsuid((uid_t)-1);
        actual.gids[3] = (gid_t)setfsgid((gid_t)-1);
        actual.group_count = getgroups(BQ_ENTRY_GROUP_LIMIT, actual.groups);
        ok = bq_entry_identity_valid(accounts, &actual);
    }
    return ok;
}

static bool bq_entry_privileges(void)
{
    struct __user_cap_header_struct header = {.version = _LINUX_CAPABILITY_VERSION_3, .pid = 0};
    struct __user_cap_data_struct data[2] = {{0}, {0}};
    bool ok = prctl(PR_GET_NO_NEW_PRIVS, 0UL, 0UL, 0UL, 0UL) == 1 &&
              prctl(PR_GET_SECCOMP, 0UL, 0UL, 0UL, 0UL) == 2 &&
              syscall(SYS_capget, &header, data) == 0;
    for (unsigned index = 0; ok && index < 2; index += 1)
        ok = data[index].effective == 0 && data[index].permitted == 0 && data[index].inheritable == 0;
    bool end_seen = false;
    for (unsigned index = 0; ok && index < 64 && !end_seen; index += 1)
    {
        int bounding = prctl(PR_CAPBSET_READ, (unsigned long)index, 0UL, 0UL, 0UL);
        if (bounding < 0 && errno == EINVAL) end_seen = true;
        else ok = bounding == 0 &&
                  prctl(PR_CAP_AMBIENT, (unsigned long)PR_CAP_AMBIENT_IS_SET,
                        (unsigned long)index, 0UL, 0UL) == 0;
    }
    return ok && end_seen;
}

static char const* const bq_entry_readonly_paths[] =
{
    "/usr", "/etc/buster-bench", "/opt/buster-bench/installed", "/var/lib/buster-bench"
};

static bool bq_entry_mount_path(char const* text, size_t size, char output[BQ_ENTRY_PATH_LIMIT])
{
    bool ok = size > 0 && text[0] == '/';
    size_t used = 0;
    for (size_t index = 0; ok && index < size; index += 1)
    {
        unsigned char value = (unsigned char)text[index];
        if (value == '\\')
        {
            ok = index + 3 < size;
            unsigned decoded = 0;
            for (unsigned digit = 1; ok && digit <= 3; digit += 1)
            {
                unsigned part = (unsigned)(text[index + digit] - '0');
                ok = part <= 7;
                if (ok) decoded = decoded * 8 + part;
            }
            ok = ok && (decoded == ' ' || decoded == '\t' || decoded == '\n' || decoded == '\\');
            value = (unsigned char)decoded;
            index += 3;
        }
        ok = ok && value != 0 && used + 1 < BQ_ENTRY_PATH_LIMIT;
        if (ok) output[used++] = (char)value;
    }
    if (ok) output[used] = 0;
    return ok;
}

/* Check descendant mounts too: a read-only parent can hide a writable bind.
 * The fixed-path fstatvfs checks below prove coverage when a path is not itself
 * a mountpoint. The kernel emits escaped mount paths, decoded before matching. */
static bool bq_entry_mountinfo(char const* bytes, size_t size)
{
    bool ok = size > 0 && bytes[size - 1] == '\n';
    size_t offset = 0;
    unsigned rows = 0;
    while (ok && offset < size)
    {
        size_t end = offset;
        while (end < size && bytes[end] != '\n') end += 1;
        size_t at = offset;
        size_t starts[6] = {0}, lengths[6] = {0};
        for (unsigned field = 0; ok && field < 6; field += 1)
        {
            starts[field] = at;
            while (at < end && bytes[at] != ' ') at += 1;
            lengths[field] = at - starts[field];
            ok = lengths[field] > 0 && at < end;
            at += 1;
        }
        char path[BQ_ENTRY_PATH_LIMIT];
        ok = ok && ++rows <= 4096 && bq_entry_mount_path(bytes + starts[4], lengths[4], path);
        bool protected = false;
        for (unsigned index = 0; ok && index < sizeof(bq_entry_readonly_paths) / sizeof(bq_entry_readonly_paths[0]); index += 1)
        {
            size_t prefix = strlen(bq_entry_readonly_paths[index]);
            size_t path_size = strlen(path);
            if (path_size >= prefix && !memcmp(path, bq_entry_readonly_paths[index], prefix) &&
                (path_size == prefix || path[prefix] == '/')) protected = true;
        }
        if (ok && protected)
        {
            size_t option = starts[5], limit = option + lengths[5];
            unsigned readonly = 0;
            bool writable = false;
            while (option < limit)
            {
                size_t next = option;
                while (next < limit && bytes[next] != ',') next += 1;
                readonly += next - option == 2 && !memcmp(bytes + option, "ro", 2);
                writable = writable || (next - option == 2 && !memcmp(bytes + option, "rw", 2));
                option = next + 1;
            }
            ok = readonly == 1 && !writable;
        }
        offset = end + 1;
    }
    return ok && rows > 0;
}

static bool bq_entry_mounts(void)
{
    /* ProtectSystem=strict also makes the root mount read-only. Its explicit
     * writable exceptions (/dev and the private temporary directories) must
     * not be confused with writable descendants of the protected subtrees. */
    int root = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct statvfs root_info = {0};
    bool ok = root >= 0 && fstatvfs(root, &root_info) == 0 && (root_info.f_flag & ST_RDONLY);
    if (root >= 0) close(root);
    for (unsigned index = 0; ok && index < sizeof(bq_entry_readonly_paths) / sizeof(bq_entry_readonly_paths[0]); index += 1)
    {
        int descriptor = bq_entry_open(bq_entry_readonly_paths[index], O_PATH | O_DIRECTORY, false);
        struct statvfs info = {0};
        ok = descriptor >= 0 && fstatvfs(descriptor, &info) == 0 && (info.f_flag & ST_RDONLY);
        if (descriptor >= 0) close(descriptor);
    }
    char bytes[BQ_ENTRY_MOUNT_LIMIT];
    size_t size = 0;
    if (ok) ok = bq_entry_read_path("/proc/self/mountinfo", bytes, sizeof(bytes), &size) &&
                 bq_entry_mountinfo(bytes, size);
    return ok;
}

static bool bq_entry_connection(int descriptor, struct stat* identity)
{
    struct sockaddr_un local = {0}, peer = {0};
    socklen_t local_size = sizeof(local), peer_size = sizeof(peer);
    socklen_t type_size = sizeof(int), accept_size = sizeof(int);
    int type = 0, accept_connections = 0;
    int flags = fcntl(descriptor, F_GETFD);
    struct stat info = {0};
    size_t expected_size = offsetof(struct sockaddr_un, sun_path) + sizeof(BQ_ENTRY_SOCKET);
    bool ok = flags >= 0 && !(flags & FD_CLOEXEC) && fstat(descriptor, &info) == 0 && S_ISSOCK(info.st_mode) &&
              getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &type_size) == 0 &&
              type_size == sizeof(type) && type == SOCK_SEQPACKET &&
              getsockopt(descriptor, SOL_SOCKET, SO_ACCEPTCONN, &accept_connections, &accept_size) == 0 &&
              accept_size == sizeof(accept_connections) && !accept_connections &&
              getsockname(descriptor, (struct sockaddr*)&local, &local_size) == 0 &&
              local_size == expected_size && local.sun_family == AF_UNIX &&
              !memcmp(local.sun_path, BQ_ENTRY_SOCKET, sizeof(BQ_ENTRY_SOCKET)) &&
              getpeername(descriptor, (struct sockaddr*)&peer, &peer_size) == 0 &&
              peer_size >= sizeof(sa_family_t) && peer_size <= sizeof(peer) && peer.sun_family == AF_UNIX;
    if (ok) *identity = info;
    return ok;
}

static bool bq_entry_elf(int descriptor, off_t size, bool static_only)
{
    Elf64_Ehdr header = {0};
    bool ok = size >= (off_t)sizeof(header) && pread(descriptor, &header, sizeof(header), 0) == sizeof(header) &&
              !memcmp(header.e_ident, ELFMAG, SELFMAG) && header.e_ident[EI_CLASS] == ELFCLASS64 &&
              header.e_ident[EI_DATA] == ELFDATA2LSB && header.e_ident[EI_VERSION] == EV_CURRENT &&
              header.e_version == EV_CURRENT && (header.e_type == ET_EXEC || (!static_only && header.e_type == ET_DYN)) &&
              header.e_ehsize == sizeof(header) && header.e_phentsize == sizeof(Elf64_Phdr) &&
              header.e_phnum > 0 && header.e_phnum <= 128 && header.e_phoff <= (uint64_t)size &&
              (uint64_t)header.e_phnum * sizeof(Elf64_Phdr) <= (uint64_t)size - header.e_phoff;
#if defined(__x86_64__)
    ok = ok && header.e_machine == EM_X86_64;
#elif defined(__aarch64__)
    ok = ok && header.e_machine == EM_AARCH64;
#else
    ok = false;
#endif
    unsigned stacks = 0, loads = 0;
    for (unsigned index = 0; ok && index < header.e_phnum; index += 1)
    {
        Elf64_Phdr program = {0};
        off_t offset = (off_t)(header.e_phoff + (uint64_t)index * sizeof(program));
        ok = pread(descriptor, &program, sizeof(program), offset) == sizeof(program) &&
             program.p_offset <= (uint64_t)size && program.p_filesz <= (uint64_t)size - program.p_offset &&
             (!static_only || (program.p_type != PT_INTERP && program.p_type != PT_DYNAMIC));
        if (program.p_type == PT_GNU_STACK)
        {
            stacks += 1;
            ok = ok && !(program.p_flags & PF_X);
        }
        if (program.p_type == PT_LOAD)
        {
            loads += 1;
            ok = ok && program.p_filesz <= program.p_memsz &&
                 (program.p_flags & (PF_W | PF_X)) != (PF_W | PF_X);
        }
    }
    return ok && stacks == 1 && loads > 0;
}

static bool bq_entry_executable(int descriptor, bool static_only, struct stat* identity)
{
    struct stat before = {0}, after = {0};
    struct statvfs mount = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &before) == 0 && S_ISREG(before.st_mode) &&
              before.st_uid == 0 && before.st_nlink == 1 && (before.st_mode & 0100) &&
              !(before.st_mode & (S_ISUID | S_ISGID | 0022)) &&
              fstatvfs(descriptor, &mount) == 0 && (mount.f_flag & ST_RDONLY);
    if (ok)
    {
        ssize_t size = fgetxattr(descriptor, "security.capability", NULL, 0);
        ok = size < 0 && errno == ENODATA && bq_entry_elf(descriptor, before.st_size, static_only) &&
             fstat(descriptor, &after) == 0 && bq_entry_same_file(&before, &after);
    }
    if (ok) *identity = before;
    return ok;
}

static bool bq_entry_hex(char const* value, size_t size, bool uuid)
{
    bool ok = size == (uuid ? 36u : 32u);
    for (size_t index = 0; ok && index < size; index += 1)
    {
        bool dash = uuid && (index == 8 || index == 13 || index == 18 || index == 23);
        ok = dash ? value[index] == '-' :
             (value[index] >= '0' && value[index] <= '9') || (value[index] >= 'a' && value[index] <= 'f');
    }
    return ok;
}

static bool bq_entry_ticks(char const* bytes, size_t size, uint64_t* ticks)
{
    size_t close = size;
    for (size_t index = 0; index < size; index += 1)
        if (bytes[index] == ')') close = index;
    bool ok = close < size && size - close > 2 && bytes[close + 1] == ' ';
    size_t at = ok ? close + 2 : size;
    for (unsigned field = 3; ok && field < 22; field += 1)
    {
        while (at < size && bytes[at] != ' ') at += 1;
        ok = at < size;
        at += 1;
    }
    size_t end = at;
    while (end < size && bytes[end] >= '0' && bytes[end] <= '9') end += 1;
    ok = ok && end < size && bytes[end] == ' ' && bq_entry_number(bytes + at, end - at, ticks) && *ticks > 0;
    return ok;
}

static bool bq_entry_cgroup(char* bytes, size_t size, char** path)
{
    bool ok = size > 5 && size < BQ_ENTRY_PATH_LIMIT && !memcmp(bytes, "0::/", 4) && bytes[size - 1] == '\n';
    for (size_t index = 3; ok && index + 1 < size; index += 1)
    {
        unsigned char c = (unsigned char)bytes[index];
        ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '/' || c == '.' || c == '-' || c == '_' || c == '@' || c == '\\';
    }
    if (ok)
    {
        bytes[size - 1] = 0;
        char const* leaf = strrchr(bytes + 3, '/');
        static char const prefix[] = "/buster-bench-systemd-broker@";
        size_t leaf_size = leaf ? strlen(leaf) : 0;
        ok = leaf_size > sizeof(prefix) - 1 + 8 && !memcmp(leaf, prefix, sizeof(prefix) - 1) &&
             !strcmp(leaf + leaf_size - 8, ".service");
        if (ok) *path = bytes + 3;
    }
    return ok;
}

static bool bq_entry_evidence(BqEntryAccounts const* accounts, struct stat const* account_file,
                              struct stat const* gate, struct stat const* broker, struct stat const* socket,
                              char const* invocation)
{
    char boot[40], process[4096], cgroup[BQ_ENTRY_PATH_LIMIT];
    size_t boot_size = 0, process_size = 0, cgroup_size = 0;
    uint64_t ticks = 0;
    char* path = NULL;
    bool ok = bq_entry_read_path("/proc/sys/kernel/random/boot_id", boot, sizeof(boot), &boot_size) &&
              boot_size == 37 && boot[36] == '\n' && bq_entry_hex(boot, 36, true) &&
              bq_entry_read_path("/proc/self/stat", process, sizeof(process), &process_size) &&
              bq_entry_ticks(process, process_size, &ticks) &&
              bq_entry_read_path("/proc/self/cgroup", cgroup, sizeof(cgroup), &cgroup_size) &&
              bq_entry_cgroup(cgroup, cgroup_size, &path);
    if (ok)
    {
        boot[36] = 0;
        char line[2048];
        int size = snprintf(line, sizeof(line),
            "BQ-BROKER-ENTRY-V1 PASS boot=%s pid=%ld ticks=%" PRIu64 " invocation=%s cgroup=%s "
            "socket=%ju:%ju gate=%ju:%ju:%jd:%jd:%ld:%jd:%ld broker=%ju:%ju:%jd:%jd:%ld:%jd:%ld "
            "receipt=%ju:%ju:%jd:%jd:%ld:%jd:%ld accounts=%u,%u,%u,%u,%u,%u "
            "uid=0 gid=%u groups=0,%u,%u caps=0 nnp=1 seccomp=2 mounts=ro fd0=seqpacket\n",
            boot, (long)getpid(), ticks, invocation, path,
            (uintmax_t)socket->st_dev, (uintmax_t)socket->st_ino,
            (uintmax_t)gate->st_dev, (uintmax_t)gate->st_ino, (intmax_t)gate->st_size,
            (intmax_t)gate->st_mtim.tv_sec, gate->st_mtim.tv_nsec, (intmax_t)gate->st_ctim.tv_sec, gate->st_ctim.tv_nsec,
            (uintmax_t)broker->st_dev, (uintmax_t)broker->st_ino, (intmax_t)broker->st_size,
            (intmax_t)broker->st_mtim.tv_sec, broker->st_mtim.tv_nsec, (intmax_t)broker->st_ctim.tv_sec, broker->st_ctim.tv_nsec,
            (uintmax_t)account_file->st_dev, (uintmax_t)account_file->st_ino, (intmax_t)account_file->st_size,
            (intmax_t)account_file->st_mtim.tv_sec, account_file->st_mtim.tv_nsec,
            (intmax_t)account_file->st_ctim.tv_sec, account_file->st_ctim.tv_nsec,
            accounts->ids[0], accounts->ids[1], accounts->ids[2], accounts->ids[3], accounts->ids[4], accounts->ids[5],
            accounts->ids[1], accounts->ids[1], accounts->ids[3]);
        ok = size > 0 && (size_t)size < sizeof(line) &&
             send(STDERR_FILENO, line, (size_t)size, MSG_DONTWAIT | MSG_NOSIGNAL) == size;
    }
    return ok;
}

int main(int argc, char** argv)
{
    (void)argv;
    BqEntryAccounts accounts = {0};
    struct stat account_file = {0}, gate_file = {0}, broker_file = {0}, socket_file = {0};
    char const* invocation = getenv("INVOCATION_ID");
    char invocation_env[sizeof("INVOCATION_ID=") + 32];
    bool ok = argc == 1 && invocation && bq_entry_hex(invocation, strnlen(invocation, 33), false) &&
              bq_entry_accounts(&accounts, &account_file) && bq_entry_identity(&accounts) &&
              bq_entry_privileges() && bq_entry_mounts() && bq_entry_connection(STDIN_FILENO, &socket_file);
    int gate = ok ? open("/proc/self/exe", O_RDONLY | O_CLOEXEC) : -1;
    int broker = ok ? bq_entry_open(BQ_ENTRY_BROKER, O_RDONLY | O_NONBLOCK, true) : -1;
    if (ok) ok = bq_entry_executable(gate, true, &gate_file) && bq_entry_executable(broker, false, &broker_file);
    if (ok)
    {
        int size = snprintf(invocation_env, sizeof(invocation_env), "INVOCATION_ID=%s", invocation);
        ok = size == (int)sizeof(invocation_env) - 1 &&
             bq_entry_evidence(&accounts, &account_file, &gate_file, &broker_file, &socket_file, invocation);
    }
    if (ok)
    {
        char* const arguments[] = {BQ_ENTRY_BROKER, "serve-connection", NULL};
        char* const environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", invocation_env, NULL};
        fexecve(broker, arguments, environment);
    }
    if (broker >= 0) close(broker);
    if (gate >= 0) close(gate);
    return 126;
}
