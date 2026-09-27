/* Real broker server-entry receipt regression with synthetic fixed identities.
 * A valid seqpacket is delivered, and the root-owned receipt reader sees a
 * deterministic fake directory/file hierarchy. The first private-state walk
 * is denied; this fixture never contacts systemd or installed host state. */
#define _GNU_SOURCE 1
#include <sys/syscall.h>
#define main bq_account_original_main
#define getpwnam bq_account_getpwnam
#define geteuid bq_account_geteuid
#define getegid bq_account_getegid
#define getgroups bq_account_getgroups
#define getgrouplist bq_account_getgrouplist
#define getsockopt bq_account_getsockopt
#define open bq_account_open
#define openat bq_account_openat
#define fstat bq_account_fstat
#define read bq_account_read
#define close bq_account_close
#define execv bq_account_execv
#include "systemd_broker.c"
#undef main
#undef getpwnam
#undef geteuid
#undef getegid
#undef getgroups
#undef getgrouplist
#undef getsockopt
#undef open
#undef openat
#undef fstat
#undef read
#undef close
#undef execv

enum { BQ_ACCOUNT_ROOT_FD = 100, BQ_ACCOUNT_ETC_FD = 101,
       BQ_ACCOUNT_CONFIG_FD = 102, BQ_ACCOUNT_RECEIPT_FD = 103 };
static uid_t account_candidate_uid;
static gid_t account_candidate_gid;
static unsigned account_state_walks;
static unsigned account_receipt_reads;
static unsigned account_manager_execs;
static bool account_missing_receipt;
static char const* account_receipt_bytes;
static size_t account_receipt_offset;

struct passwd* bq_account_getpwnam(char const* name)
{
    static struct passwd result;
    result = (struct passwd){0};
    if (!strcmp(name, "buster-bench"))
    {
        result.pw_uid = 65000;
        result.pw_gid = 65000;
    }
    else if (!strcmp(name, "buster-bench-candidate"))
    {
        result.pw_uid = account_candidate_uid;
        result.pw_gid = account_candidate_gid;
    }
    else if (!strcmp(name, "buster-github-runner"))
    {
        result.pw_uid = 65002;
        result.pw_gid = 65002;
    }
    else return NULL;
    return &result;
}

uid_t bq_account_geteuid(void) { return 0; }
gid_t bq_account_getegid(void) { return 65000; }
int bq_account_getgroups(int size, gid_t* groups)
{
    int result = -1;
    if (size >= 2)
    {
        groups[0] = 65000;
        groups[1] = account_candidate_gid;
        result = 2;
    }
    return result;
}
int bq_account_getgrouplist(char const* user, gid_t primary, gid_t* groups, int* count)
{
    bool service = !strcmp(user, "buster-bench");
    int needed = service ? 2 : 1;
    int result = -1;
    if (*count >= needed)
    {
        groups[0] = primary;
        if (service) groups[1] = account_candidate_gid;
        result = needed;
    }
    *count = needed;
    return result;
}
int bq_account_getsockopt(int descriptor, int level, int name, void* output, socklen_t* size)
{
    (void)descriptor;
    int result = -1;
    if (level == SOL_SOCKET && name == SO_PEERCRED && *size >= sizeof(struct ucred))
    {
        *(struct ucred*)output = (struct ucred){.pid = getpid(), .uid = 65000, .gid = 65000};
        *size = sizeof(struct ucred);
        result = 0;
    }
    return result;
}

int bq_account_open(char const* path, int flags, ...)
{
    (void)flags;
    int result = -1;
    if (!strcmp(path, "/")) result = BQ_ACCOUNT_ROOT_FD;
    else errno = ENOENT;
    return result;
}
int bq_account_openat(int directory, char const* path, int flags, ...)
{
    (void)flags;
    int result = -1;
    if (directory == BQ_ACCOUNT_ROOT_FD && !strcmp(path, "etc")) result = BQ_ACCOUNT_ETC_FD;
    else if (directory == BQ_ACCOUNT_ETC_FD && !strcmp(path, "buster-bench")) result = BQ_ACCOUNT_CONFIG_FD;
    else if (directory == BQ_ACCOUNT_CONFIG_FD && !strcmp(path, "systemd-broker-accounts.identity") &&
             !account_missing_receipt) result = BQ_ACCOUNT_RECEIPT_FD;
    else if (directory == BQ_ACCOUNT_ROOT_FD && !strcmp(path, "var")) account_state_walks += 1;
    if (result < 0) errno = ENOENT;
    return result;
}
int bq_account_fstat(int descriptor, struct stat* info)
{
    int result = -1;
    if (descriptor >= BQ_ACCOUNT_ROOT_FD && descriptor <= BQ_ACCOUNT_RECEIPT_FD)
    {
        *info = (struct stat){0};
        info->st_uid = 0; info->st_gid = 0;
        info->st_nlink = 1; info->st_dev = 41; info->st_ino = (ino_t)descriptor;
        info->st_mode = descriptor == BQ_ACCOUNT_RECEIPT_FD ? S_IFREG | 0444 : S_IFDIR | 0555;
        if (descriptor == BQ_ACCOUNT_RECEIPT_FD) info->st_size = (off_t)strlen(account_receipt_bytes);
        result = 0;
    }
    else result = (int)syscall(SYS_fstat, descriptor, info);
    return result;
}
ssize_t bq_account_read(int descriptor, void* bytes, size_t count)
{
    ssize_t result = -1;
    if (descriptor == BQ_ACCOUNT_RECEIPT_FD)
    {
        size_t length = strlen(account_receipt_bytes);
        size_t available = length - account_receipt_offset;
        size_t copied = count < available ? count : available;
        if (copied) memcpy(bytes, account_receipt_bytes + account_receipt_offset, copied);
        account_receipt_offset += copied;
        account_receipt_reads += 1;
        result = (ssize_t)copied;
    }
    else result = syscall(SYS_read, descriptor, bytes, count);
    return result;
}
int bq_account_close(int descriptor)
{
    int result = 0;
    if (descriptor < BQ_ACCOUNT_ROOT_FD || descriptor > BQ_ACCOUNT_RECEIPT_FD)
        result = (int)syscall(SYS_close, descriptor);
    return result;
}
int bq_account_execv(char const* path, char* const arguments[])
{
    (void)path;
    (void)arguments;
    account_manager_execs += 1;
    errno = EPERM;
    return -1;
}

static bool account_case(char const* label, uint32_t operation, uid_t candidate_uid,
                         gid_t candidate_gid, char const* receipt, bool missing,
                         bool expected_state)
{
    static char const canonical[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
                                    "candidate-uid=65001\ncandidate-gid=65001\n"
                                    "runner-uid=65002\nrunner-gid=65002\n";
    int pair[2] = {-1, -1};
    int saved_stdin = dup(STDIN_FILENO);
    bool ok = saved_stdin >= 0 && socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0;
    account_candidate_uid = candidate_uid;
    account_candidate_gid = candidate_gid;
    account_receipt_bytes = receipt ? receipt : canonical;
    account_receipt_offset = 0;
    account_receipt_reads = 0;
    account_state_walks = 0;
    account_manager_execs = 0;
    account_missing_receipt = missing;
    if (ok)
    {
        BqBrokerRequest request = {.magic = BQ_BROKER_MAGIC, .version = 1,
                                   .operation = operation, .stage = BQ_BROKER_OUTER,
                                   .signal_number = operation == BQ_BROKER_SIGNAL ? BQ_BROKER_CONT : 0,
                                   .job = 1, .attempt = 2};
        if (operation == BQ_BROKER_START)
        {
            memset(request.base, 'a', 40);
            memset(request.candidate, 'b', 40);
        }
        ok = send(pair[1], &request, sizeof(request), 0) == sizeof(request) &&
             dup2(pair[0], STDIN_FILENO) == STDIN_FILENO;
    }
    if (ok) bq_broker_server();
    if (saved_stdin >= 0) { dup2(saved_stdin, STDIN_FILENO); syscall(SYS_close, saved_stdin); }
    if (pair[0] >= 0) syscall(SYS_close, pair[0]);
    if (pair[1] >= 0) syscall(SYS_close, pair[1]);
    bool state = account_state_walks > 0;
    printf("%s op=%u candidate=%u:%u receipt_reads=%u state=%d manager_execs=%u\n",
           label, operation, candidate_uid, candidate_gid, account_receipt_reads, state,
           account_manager_execs);
    return ok && state == expected_state && account_manager_execs == 0 &&
           (missing ? account_receipt_reads == 0 : account_receipt_reads >= 1);
}

int main(void)
{
    static char const malformed[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
                                    "candidate-uid=65001\ncandidate-gid=65001\n"
                                    "runner-uid=65002\nrunner-gid=65002\r\n";
    bool ok = true;
    ok &= account_case("clean_start", BQ_BROKER_START, 65001, 65001, NULL, false, true);
    ok &= account_case("changed_candidate_uid_start", BQ_BROKER_START, 65003, 65001, NULL, false, false);
    ok &= account_case("changed_candidate_gid_start", BQ_BROKER_START, 65001, 65003, NULL, false, false);
    ok &= account_case("missing_start", BQ_BROKER_START, 65001, 65001, NULL, true, false);
    ok &= account_case("malformed_start", BQ_BROKER_START, 65001, 65001, malformed, false, false);
    ok &= account_case("clean_cont", BQ_BROKER_SIGNAL, 65001, 65001, NULL, false, true);
    ok &= account_case("changed_candidate_uid_cont", BQ_BROKER_SIGNAL, 65003, 65001, NULL, false, false);
    ok &= account_case("changed_candidate_gid_cont", BQ_BROKER_SIGNAL, 65001, 65003, NULL, false, false);
    ok &= account_case("missing_cont", BQ_BROKER_SIGNAL, 65001, 65001, NULL, true, false);
    ok &= account_case("malformed_cont", BQ_BROKER_SIGNAL, 65001, 65001, malformed, false, false);
    return ok ? 0 : 1;
}
