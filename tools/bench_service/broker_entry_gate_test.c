/* Component regressions for the prospective broker entry gate. Capability
 * queries are synthetic; socket and ELF checks use real disposable objects.
 * This is not the required privileged systemd activation/negative matrix. */
#define _GNU_SOURCE 1
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <unistd.h>
static int entry_test_prctl(int option, ...);
static long entry_test_syscall(long number, ...);
static int entry_test_getsockname(int descriptor, struct sockaddr* address, socklen_t* size);
#define main bq_entry_original_main
#define prctl entry_test_prctl
#define syscall entry_test_syscall
#define getsockname entry_test_getsockname
#include "broker_entry_gate.c"
#undef main
#undef prctl
#undef syscall
#undef getsockname

static unsigned entry_checks, entry_failures;
static int entry_nnp = 1, entry_seccomp = 2, entry_bounding, entry_ambient;
static bool entry_cap_error;
static struct __user_cap_data_struct entry_caps[2];
static int entry_socket_descriptor = -1;
static bool entry_translate_socket;

#define ENTRY_CHECK(condition) do { entry_checks += 1; if (!(condition)) { \
    entry_failures += 1; fprintf(stderr, "entry gate check failed at line %d: %s\n", __LINE__, #condition); } } while (0)

static int entry_test_prctl(int option, ...)
{
    va_list args;
    va_start(args, option);
    unsigned long argument = va_arg(args, unsigned long);
    int result = -1;
    if (option == PR_GET_NO_NEW_PRIVS) result = entry_nnp;
    else if (option == PR_GET_SECCOMP) result = entry_seccomp;
    else if (option == PR_CAPBSET_READ)
    {
        if (argument < 41) result = entry_bounding;
        else errno = EINVAL;
    }
    else if (option == PR_CAP_AMBIENT) result = entry_ambient;
    va_end(args);
    return result;
}

static long entry_test_syscall(long number, ...)
{
    va_list args;
    va_start(args, number);
    struct __user_cap_header_struct* header = va_arg(args, struct __user_cap_header_struct*);
    struct __user_cap_data_struct* data = va_arg(args, struct __user_cap_data_struct*);
    long result = -1;
    if (number == SYS_capget && header->version == _LINUX_CAPABILITY_VERSION_3 && header->pid == 0 && !entry_cap_error)
    {
        memcpy(data, entry_caps, sizeof(entry_caps));
        result = 0;
    }
    va_end(args);
    return result;
}

/* Supply the fixed systemd address for one real connected socketpair endpoint.
 * Production still accepts just its compiled pathname. A filesystem listener
 * and PID1's actual FD inheritance remain part of the separate guest proof. */
static int entry_test_getsockname(int descriptor, struct sockaddr* address, socklen_t* size)
{
    int result = getsockname(descriptor, address, size);
    struct sockaddr_un* unix_address = (struct sockaddr_un*)address;
    if (result == 0 && entry_translate_socket && descriptor == entry_socket_descriptor &&
        *size == sizeof(sa_family_t) && unix_address->sun_family == AF_UNIX)
    {
        memset(unix_address, 0, sizeof(*unix_address));
        unix_address->sun_family = AF_UNIX;
        memcpy(unix_address->sun_path, BQ_ENTRY_SOCKET, sizeof(BQ_ENTRY_SOCKET));
        *size = offsetof(struct sockaddr_un, sun_path) + sizeof(BQ_ENTRY_SOCKET);
    }
    return result;
}

static void entry_account_tests(void)
{
    static char const canonical[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
        "candidate-uid=65001\ncandidate-gid=65001\nrunner-uid=65002\nrunner-gid=65002\n";
    BqEntryAccounts accounts = {0};
    ENTRY_CHECK(bq_entry_accounts_parse(canonical, sizeof(canonical) - 1, &accounts));
    ENTRY_CHECK(accounts.ids[0] == 65000 && accounts.ids[5] == 65002);
    for (size_t size = 0; size < sizeof(canonical) - 1; size += 1)
        ENTRY_CHECK(!bq_entry_accounts_parse(canonical, size, &accounts));
    ENTRY_CHECK(!bq_entry_accounts_parse(canonical, sizeof(canonical), &accounts));
    char changed[256];
    for (unsigned field = 0; field < 6; field += 1)
    {
        strcpy(changed, canonical);
        char* at = changed;
        for (unsigned index = 0; index <= field; index += 1) at = strchr(at, '=') + 1;
        at[0] = '0';
        ENTRY_CHECK(!bq_entry_accounts_parse(changed, strlen(changed), &accounts));
    }
    strcpy(changed, canonical);
    memcpy(strstr(changed, "candidate-uid=") + strlen("candidate-uid="), "65000", 5);
    ENTRY_CHECK(!bq_entry_accounts_parse(changed, strlen(changed), &accounts));
    uint64_t number = 0;
    ENTRY_CHECK(bq_entry_number("18446744073709551615", 20, &number) && number == UINT64_MAX);
    ENTRY_CHECK(!bq_entry_number("18446744073709551616", 20, &number));
    ENTRY_CHECK(!bq_entry_number("-1", 2, &number) && !bq_entry_number("+1", 2, &number));
    BqEntryIdentity identity = {.uids = {0, 0, 0, 0}, .gids = {65000, 65000, 65000, 65000},
        .groups = {65001, 65000}, .group_count = 2};
    ENTRY_CHECK(bq_entry_identity_valid(&accounts, &identity));
    for (unsigned field = 0; field < 4; field += 1)
    {
        BqEntryIdentity bad = identity;
        bad.uids[field] = 65000;
        ENTRY_CHECK(!bq_entry_identity_valid(&accounts, &bad));
        bad = identity;
        bad.gids[field] = 0;
        ENTRY_CHECK(!bq_entry_identity_valid(&accounts, &bad));
    }
    for (int count = -1; count <= BQ_ENTRY_GROUP_LIMIT + 1; count += 1)
    {
        BqEntryIdentity bad = identity;
        bad.group_count = count;
        ENTRY_CHECK(bq_entry_identity_valid(&accounts, &bad) == (count == 2));
    }
    for (unsigned field = 0; field < 2; field += 1)
    {
        BqEntryIdentity bad = identity;
        bad.groups[field] = 65002;
        ENTRY_CHECK(!bq_entry_identity_valid(&accounts, &bad));
        bad.groups[field] = 0;
        ENTRY_CHECK(!bq_entry_identity_valid(&accounts, &bad));
        bad.groups[field] = identity.groups[(field + 1) % 2];
        ENTRY_CHECK(!bq_entry_identity_valid(&accounts, &bad));
    }
    BqEntryIdentity rooted = identity;
    rooted.groups[2] = 0;
    rooted.group_count = 3;
    ENTRY_CHECK(!bq_entry_identity_valid(&accounts, &rooted));
    accounts.ids[1] += 10;
    ENTRY_CHECK(!bq_entry_identity_valid(&accounts, &identity));
}

static void entry_account_source_tests(void)
{
    static char const receipt[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
        "candidate-uid=65001\ncandidate-gid=65001\nrunner-uid=65002\nrunner-gid=65002\n";
    static char const passwd[] = "root:x:0:0:root:/root:/bin/sh\n"
        "buster-bench:x:65000:65000:service:/var/lib/buster-bench:/usr/sbin/nologin\n"
        "buster-bench-candidate:x:65001:65001:candidate:/nonexistent:/usr/sbin/nologin\n"
        "buster-github-runner:x:65002:65002:runner:/nonexistent:/usr/sbin/nologin\n";
    static char const group[] = "root:x:0:\n"
        "buster-bench:x:65000:\n"
        "buster-bench-candidate:x:65001:buster-bench\n"
        "buster-github-runner:x:65002:\n";
    static char const nss[] = "# local accounts\npasswd: files systemd\ngroup: files systemd\n"
        "initgroups: files\nhosts: files dns\n";
    BqEntryAccounts accounts = {0};
    ENTRY_CHECK(bq_entry_accounts_parse(receipt, sizeof(receipt) - 1, &accounts));
    ENTRY_CHECK(bq_entry_local_records(passwd, sizeof(passwd) - 1, &accounts, true));
    ENTRY_CHECK(bq_entry_local_records(group, sizeof(group) - 1, &accounts, false));
    for (unsigned role = 0; role < 3; role += 1)
    {
        BqEntryAccounts stale = accounts;
        stale.ids[2 * role] += 10;
        ENTRY_CHECK(!bq_entry_local_records(passwd, sizeof(passwd) - 1, &stale, true));
        stale = accounts;
        stale.ids[2 * role + 1] += 10;
        ENTRY_CHECK(!bq_entry_local_records(passwd, sizeof(passwd) - 1, &stale, true));
        ENTRY_CHECK(!bq_entry_local_records(group, sizeof(group) - 1, &stale, false));
    }
    static char const* const passwd_bad[] =
    {
        "buster-bench:x:65000:65000:x:/tmp:/bin/sh\nbuster-bench:x:65000:65000:x:/tmp:/bin/sh\n",
        "buster-bench:x:65000:65000:x:/tmp:/bin/sh\nbuster-bench-candidate:x:65001:65001:x:/tmp:/bin/sh\n"
            "buster-github-runner:x:65002:65002:x:/tmp:/bin/sh\nshadow:x:65002:60000:x:/tmp:/bin/sh\n",
        "buster-bench:x:65000:65000:x:/tmp:/bin/sh\nbuster-bench-candidate:x:65001:65001:x:/tmp:/bin/sh\n",
        "buster-bench:x:65000:65000:x:/tmp:/bin/sh\nbuster-bench-candidate:x:65001:65001:x:/tmp:/bin/sh\n"
            "buster-github-runner:x:065002:65002:x:/tmp:/bin/sh\n",
        "+::::::\n"
    };
    for (unsigned index = 0; index < sizeof(passwd_bad) / sizeof(passwd_bad[0]); index += 1)
        ENTRY_CHECK(!bq_entry_local_records(passwd_bad[index], strlen(passwd_bad[index]), &accounts, true));
    static char const* const group_bad[] =
    {
        "buster-bench:x:65000:\nbuster-bench:x:65000:\n",
        "buster-bench:x:65000:\nbuster-bench-candidate:x:65001:\n"
            "buster-github-runner:x:65002:\nalias:x:65002:\n",
        "buster-bench:x:65000:\nbuster-bench-candidate:x:65001:\n",
        "buster-bench:x:65000:\nbuster-bench-candidate:x:65001:\n"
            "buster-github-runner:x:065002:\n"
    };
    for (unsigned index = 0; index < sizeof(group_bad) / sizeof(group_bad[0]); index += 1)
        ENTRY_CHECK(!bq_entry_local_records(group_bad[index], strlen(group_bad[index]), &accounts, false));
    ENTRY_CHECK(!bq_entry_local_records(passwd, sizeof(passwd) - 2, &accounts, true));
    ENTRY_CHECK(!bq_entry_local_records(group, sizeof(group) - 2, &accounts, false));
    ENTRY_CHECK(bq_entry_nss_policy(nss, sizeof(nss) - 1));
    ENTRY_CHECK(bq_entry_nss_policy("passwd: files\ngroup: files\n", sizeof("passwd: files\ngroup: files\n") - 1));
    ENTRY_CHECK(bq_entry_nss_policy("passwd: files systemd\ngroup: files systemd\n",
                                   sizeof("passwd: files systemd\ngroup: files systemd\n") - 1));
    static char const* const nss_bad[] =
    {
        "passwd: files [SUCCESS=merge] systemd\ngroup: files\n",
        "passwd: files systemd\ngroup: files [SUCCESS=merge] systemd\n",
        "passwd: files sss\ngroup: files\n",
        "passwd: systemd files\ngroup: files\n",
        "passwd: files\ngroup: files nscd\n",
        "passwd: files#systemd\ngroup: files\n",
        "passwd: files\ngroup: files # systemd\n",
        "passwd: files\npasswd: files\ngroup: files\n",
        "passwd: files\ngroup: files\ngroup: files\n",
        "passwd: files\ngroup: files\ninitgroups: files systemd\n",
        "passwd: files\ngroup: files\ninitgroups: files\ninitgroups: files\n",
        "passwd: files\n",
        "group: files\n",
        "passwd: files\ngroup: files"
    };
    for (unsigned index = 0; index < sizeof(nss_bad) / sizeof(nss_bad[0]); index += 1)
        ENTRY_CHECK(!bq_entry_nss_policy(nss_bad[index], strlen(nss_bad[index])));
    struct stat file = {.st_mode = S_IFREG | 0644, .st_uid = 0, .st_nlink = 1,
                        .st_size = (off_t)(sizeof(passwd) - 1)};
    struct statvfs mount = {.f_flag = ST_RDONLY};
    ENTRY_CHECK(bq_entry_source_metadata(&file, &mount));
    mount.f_flag = 0;
    ENTRY_CHECK(!bq_entry_source_metadata(&file, &mount));
    mount.f_flag = ST_RDONLY;
    file.st_uid = 65000;
    ENTRY_CHECK(!bq_entry_source_metadata(&file, &mount));
    file.st_uid = 0;
    file.st_nlink = 2;
    ENTRY_CHECK(!bq_entry_source_metadata(&file, &mount));
    file.st_nlink = 1;
    file.st_mode = S_IFREG | 0664;
    ENTRY_CHECK(!bq_entry_source_metadata(&file, &mount));
    file.st_mode = S_IFDIR | 0555;
    ENTRY_CHECK(!bq_entry_source_metadata(&file, &mount));
    file.st_mode = S_IFREG | 0644;
    file.st_size = BQ_ENTRY_SOURCE_LIMIT + 1;
    ENTRY_CHECK(!bq_entry_source_metadata(&file, &mount));
    file.st_size = 0;
    ENTRY_CHECK(!bq_entry_source_metadata(&file, &mount));
    Sha256 sha;
    char hex[SHA256_HEX_CAPACITY];
    sha256_init(&sha);
    sha256_add(&sha, "abc", 3);
    sha256_finish_hex(&sha, (char8*)hex);
    ENTRY_CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

static void entry_privilege_tests(void)
{
    ENTRY_CHECK(bq_entry_privileges());
    for (unsigned word = 0; word < 2; word += 1)
    {
        entry_caps[word].effective = 1;
        ENTRY_CHECK(!bq_entry_privileges());
        entry_caps[word].effective = 0;
        entry_caps[word].permitted = 1;
        ENTRY_CHECK(!bq_entry_privileges());
        entry_caps[word].permitted = 0;
        entry_caps[word].inheritable = 1;
        ENTRY_CHECK(!bq_entry_privileges());
        entry_caps[word].inheritable = 0;
    }
    entry_bounding = 1;
    ENTRY_CHECK(!bq_entry_privileges());
    entry_bounding = 0;
    entry_ambient = 1;
    ENTRY_CHECK(!bq_entry_privileges());
    entry_ambient = 0;
    entry_cap_error = true;
    ENTRY_CHECK(!bq_entry_privileges());
    entry_cap_error = false;
    entry_nnp = 0;
    ENTRY_CHECK(!bq_entry_privileges());
    entry_nnp = 1;
    for (int mode = -1; mode <= 2; mode += 1)
    {
        entry_seccomp = mode;
        ENTRY_CHECK(bq_entry_privileges() == (mode == 2));
    }
}

static void entry_mount_tests(void)
{
    char line[1024];
    static char const* const paths[] = {"/usr", "/usr/bin", "/etc/buster-bench", "/etc/buster-bench/sub",
        "/opt/buster-bench/installed", "/opt/buster-bench/installed/sub", "/var/lib/buster-bench",
        "/var/lib/buster-bench/sub", "/var/lib/buster-bench/sub\\040space"};
    for (unsigned index = 0; index < sizeof(paths) / sizeof(paths[0]); index += 1)
    {
        int size = snprintf(line, sizeof(line), "42 1 8:1 / %s ro,nosuid - ext4 /dev/test rw\n", paths[index]);
        ENTRY_CHECK(size > 0 && bq_entry_mountinfo(line, (size_t)size));
        char* option = strstr(line, " ro,");
        option[2] = 'w';
        ENTRY_CHECK(!bq_entry_mountinfo(line, (size_t)size));
    }
    static char const unrelated[] = "42 1 8:1 / /usr-other rw - ext4 /dev/test rw\n";
    ENTRY_CHECK(bq_entry_mountinfo(unrelated, sizeof(unrelated) - 1));
    static char const nested[] = "42 1 8:1 / /usr ro - ext4 /dev/test rw\n"
                                "43 42 8:2 / /usr/local rw - ext4 /dev/other rw\n";
    ENTRY_CHECK(!bq_entry_mountinfo(nested, sizeof(nested) - 1));
    ENTRY_CHECK(!bq_entry_mountinfo(nested, sizeof(nested) - 2));
    ENTRY_CHECK(!bq_entry_mountinfo("", 0));
    char decoded[BQ_ENTRY_PATH_LIMIT];
    ENTRY_CHECK(bq_entry_mount_path("/foo\\040bar", 11, decoded) && !strcmp(decoded, "/foo bar"));
    ENTRY_CHECK(!bq_entry_mount_path("/foo\\000bar", 11, decoded));
    ENTRY_CHECK(!bq_entry_mount_path("/foo\\04", 7, decoded));
}

static void entry_socket_tests(void)
{
    int connection[2] = {-1, -1};
    bool setup = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, connection) == 0;
    int accepted = connection[0];
    entry_socket_descriptor = accepted;
    ENTRY_CHECK(setup);
    struct stat info;
    if (setup)
    {
        entry_translate_socket = true;
        ENTRY_CHECK(bq_entry_connection(accepted, &info));
        ENTRY_CHECK(info.st_ino != 0 && S_ISSOCK(info.st_mode));
        ENTRY_CHECK(fcntl(accepted, F_SETFD, FD_CLOEXEC) == 0 && !bq_entry_connection(accepted, &info));
        ENTRY_CHECK(fcntl(accepted, F_SETFD, 0) == 0 && bq_entry_connection(accepted, &info));
        ENTRY_CHECK(!bq_entry_connection(connection[1], &info));
        entry_translate_socket = false;
        ENTRY_CHECK(!bq_entry_connection(accepted, &info));
    }
    ENTRY_CHECK(!bq_entry_connection(-1, &info));
    int pair[2] = {-1, -1};
    ENTRY_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0 && !bq_entry_connection(pair[0], &info));
    if (pair[0] >= 0) close(pair[0]);
    if (pair[1] >= 0) close(pair[1]);
    if (connection[0] >= 0) close(connection[0]);
    if (connection[1] >= 0) close(connection[1]);
    entry_socket_descriptor = -1;
}

static void entry_elf_tests(char const* gate_path, char const* broker_path)
{
    int gate = open(gate_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int broker = open(broker_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat gate_info = {0}, broker_info = {0};
    bool setup = gate >= 0 && broker >= 0 && fstat(gate, &gate_info) == 0 && fstat(broker, &broker_info) == 0;
    ENTRY_CHECK(setup);
    if (setup)
    {
        ENTRY_CHECK(bq_entry_elf(gate, gate_info.st_size, true));
        ENTRY_CHECK(bq_entry_elf(broker, broker_info.st_size, false));
        ENTRY_CHECK(!bq_entry_elf(broker, broker_info.st_size, true));
        char name[] = "/tmp/bq-entry-elf-XXXXXX";
        int copy = mkstemp(name);
        bool copied = copy >= 0;
        if (copy >= 0) unlink(name);
        off_t offset = 0;
        while (copied && offset < gate_info.st_size)
        {
            char bytes[4096];
            ssize_t size = pread(gate, bytes, sizeof(bytes), offset);
            copied = size > 0 && write(copy, bytes, (size_t)size) == size;
            if (copied) offset += size;
        }
        ENTRY_CHECK(copied);
        if (copied)
        {
            Elf64_Ehdr header;
            ENTRY_CHECK(pread(copy, &header, sizeof(header), 0) == sizeof(header));
            for (unsigned fault = 0; fault < 5; fault += 1)
            {
                Elf64_Ehdr bad = header;
                if (fault == 0) bad.e_ident[0] = 0;
                if (fault == 1) bad.e_machine = EM_NONE;
                if (fault == 2) bad.e_phoff = UINT64_MAX;
                if (fault == 3) bad.e_phnum = 129;
                if (fault == 4) bad.e_type = ET_REL;
                ENTRY_CHECK(pwrite(copy, &bad, sizeof(bad), 0) == sizeof(bad) && !bq_entry_elf(copy, gate_info.st_size, true));
            }
            ENTRY_CHECK(pwrite(copy, &header, sizeof(header), 0) == sizeof(header));
            for (unsigned index = 0; index < header.e_phnum; index += 1)
            {
                Elf64_Phdr original;
                off_t at = (off_t)(header.e_phoff + index * sizeof(original));
                bool read_ok = pread(copy, &original, sizeof(original), at) == sizeof(original);
                ENTRY_CHECK(read_ok);
                if (read_ok && (original.p_type == PT_GNU_STACK || original.p_type == PT_LOAD))
                {
                    Elf64_Phdr bad = original;
                    bad.p_flags |= PF_X | PF_W;
                    ENTRY_CHECK(pwrite(copy, &bad, sizeof(bad), at) == sizeof(bad) && !bq_entry_elf(copy, gate_info.st_size, true));
                    bad = original;
                    bad.p_type = PT_INTERP;
                    ENTRY_CHECK(pwrite(copy, &bad, sizeof(bad), at) == sizeof(bad) && !bq_entry_elf(copy, gate_info.st_size, true));
                    ENTRY_CHECK(pwrite(copy, &original, sizeof(original), at) == sizeof(original));
                }
            }
            ENTRY_CHECK(bq_entry_elf(copy, gate_info.st_size, true));
            ENTRY_CHECK(!bq_entry_elf(copy, sizeof(header) - 1, true));
        }
        if (copy >= 0) close(copy);
    }
    if (gate >= 0) close(gate);
    if (broker >= 0) close(broker);
}

static void entry_evidence_tests(void)
{
    ENTRY_CHECK(bq_entry_hex("0123456789abcdef0123456789abcdef", 32, false));
    ENTRY_CHECK(!bq_entry_hex("0123456789ABCDEF0123456789abcdef", 32, false));
    ENTRY_CHECK(bq_entry_hex("01234567-89ab-cdef-0123-456789abcdef", 36, true));
    ENTRY_CHECK(!bq_entry_hex("0123456789abcdef0123456789abcdef", 32, true));
    char cgroup[] = "0::/system.slice/buster-bench-systemd-broker@1-test.service\n";
    char* path = NULL;
    ENTRY_CHECK(bq_entry_cgroup(cgroup, sizeof(cgroup) - 1, &path) && path == cgroup + 3);
    char bad_cgroup[] = "0::/system.slice/unrelated.service\n";
    ENTRY_CHECK(!bq_entry_cgroup(bad_cgroup, sizeof(bad_cgroup) - 1, &path));
    char stat_bytes[4096];
    size_t stat_size = 0;
    uint64_t ticks = 0;
    ENTRY_CHECK(bq_entry_read_path("/proc/self/stat", stat_bytes, sizeof(stat_bytes), &stat_size) &&
                bq_entry_ticks(stat_bytes, stat_size, &ticks) && ticks > 0);
    ENTRY_CHECK(!bq_entry_ticks("1 (unterminated", 15, &ticks));
    struct stat before = {.st_dev = 1, .st_ino = 2, .st_mode = S_IFREG | 0444,
        .st_nlink = 1, .st_size = 142};
    ENTRY_CHECK(bq_entry_same_file(&before, &before));
    struct stat after = before;
    after.st_ctim.tv_nsec += 1;
    ENTRY_CHECK(!bq_entry_same_file(&before, &after));
    after = before;
    after.st_ino += 1;
    ENTRY_CHECK(!bq_entry_same_file(&before, &after));
    BqEntryAccounts accounts = {.ids = {UINT32_MAX - 6, UINT32_MAX - 5, UINT32_MAX - 4,
                                         UINT32_MAX - 3, UINT32_MAX - 2, UINT32_MAX - 1}};
    struct stat maximum = {.st_dev = (dev_t)UINT64_MAX, .st_ino = (ino_t)UINT64_MAX,
        .st_size = INT64_MAX};
    maximum.st_mtim.tv_sec = INT64_MAX;
    maximum.st_mtim.tv_nsec = 999999999;
    maximum.st_ctim = maximum.st_mtim;
    BqEntrySource sources[BQ_ENTRY_SOURCE_COUNT] =
    {
        {.descriptor = -1, .identity = maximum}, {.descriptor = -1, .identity = maximum},
        {.descriptor = -1, .identity = maximum}
    };
    for (unsigned index = 0; index < BQ_ENTRY_SOURCE_COUNT; index += 1)
        memset(sources[index].sha256, 'a' + index, sizeof(sources[index].sha256) - 1);
    char worst_path[BQ_ENTRY_PATH_LIMIT];
    worst_path[0] = '/';
    memset(worst_path + 1, 'a', sizeof(worst_path) - 2);
    worst_path[sizeof(worst_path) - 1] = 0;
    char line[2048];
    int size = bq_entry_evidence_line(line, sizeof(line), &accounts, &maximum, sources,
                                     &maximum, &maximum, &maximum,
                                     "01234567-89ab-cdef-0123-456789abcdef", UINT64_MAX, worst_path,
                                     "0123456789abcdef0123456789abcdef");
    /* Leave room for a maximum decimal PID beyond this test process's PID. */
    ENTRY_CHECK(size > 0 && (size_t)size + 10 < sizeof(line));
    ENTRY_CHECK(strstr(line, "passwd-sha256=") && strstr(line, "group-sha256=") &&
                strstr(line, "nsswitch-sha256=") && strstr(line, "nsswitch="));
    char too_small[64];
    int truncated = bq_entry_evidence_line(too_small, sizeof(too_small), &accounts, &maximum, sources,
                                           &maximum, &maximum, &maximum,
                                           "01234567-89ab-cdef-0123-456789abcdef", UINT64_MAX, worst_path,
                                           "0123456789abcdef0123456789abcdef");
    ENTRY_CHECK(truncated >= (int)sizeof(too_small));
    ENTRY_CHECK(bq_entry_original_main(2, NULL) == 126);
}

int main(int argc, char** argv)
{
    ENTRY_CHECK(argc == 3);
    if (argc == 3)
    {
        entry_account_tests();
        entry_account_source_tests();
        entry_privilege_tests();
        entry_mount_tests();
        entry_socket_tests();
        entry_elf_tests(argv[1], argv[2]);
        entry_evidence_tests();
    }
    printf("BQ_BROKER_ENTRY_TEST checks=%u failures=%u result=%s\n",
           entry_checks, entry_failures, entry_failures ? "fail" : "pass");
    return entry_failures ? 1 : 0;
}
