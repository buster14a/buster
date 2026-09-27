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
enum AccountMutation { ACCOUNT_UNCHANGED, ACCOUNT_SERVICE_UID, ACCOUNT_SERVICE_GID,
                       ACCOUNT_CANDIDATE_UID, ACCOUNT_CANDIDATE_GID,
                       ACCOUNT_RUNNER_UID, ACCOUNT_RUNNER_GID };
enum AccountFault { ACCOUNT_RECEIPT_OK, ACCOUNT_RECEIPT_MISSING,
                    ACCOUNT_FILE_OWNER, ACCOUNT_FILE_MODE, ACCOUNT_FILE_LINK,
                    ACCOUNT_FILE_TYPE, ACCOUNT_FILE_OVERSIZE,
                    ACCOUNT_DIRECTORY_OWNER, ACCOUNT_DIRECTORY_MODE,
                    ACCOUNT_AFTER_INODE, ACCOUNT_AFTER_MTIME, ACCOUNT_AFTER_CTIME };
static BqBrokerAccounts account_ids;
static unsigned account_state_walks;
static unsigned account_receipt_reads;
static unsigned account_receipt_stats;
static unsigned account_manager_execs;
static enum AccountFault account_fault;
static char const* account_receipt_bytes;
static size_t account_receipt_offset;

struct passwd* bq_account_getpwnam(char const* name)
{
    static struct passwd result;
    result = (struct passwd){0};
    if (!strcmp(name, "buster-bench"))
    {
        result.pw_uid = account_ids.service_uid;
        result.pw_gid = account_ids.service_gid;
    }
    else if (!strcmp(name, "buster-bench-candidate"))
    {
        result.pw_uid = account_ids.candidate_uid;
        result.pw_gid = account_ids.candidate_gid;
    }
    else if (!strcmp(name, "buster-github-runner"))
    {
        result.pw_uid = account_ids.runner_uid;
        result.pw_gid = account_ids.runner_gid;
    }
    else return NULL;
    return &result;
}

uid_t bq_account_geteuid(void) { return 0; }
gid_t bq_account_getegid(void) { return account_ids.service_gid; }
int bq_account_getgroups(int size, gid_t* groups)
{
    int result = -1;
    if (size >= 2)
    {
        groups[0] = account_ids.service_gid;
        groups[1] = account_ids.candidate_gid;
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
        if (service) groups[1] = account_ids.candidate_gid;
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
        *(struct ucred*)output = (struct ucred){.pid = getpid(),
            .uid = account_ids.service_uid, .gid = account_ids.service_gid};
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
             account_fault != ACCOUNT_RECEIPT_MISSING) result = BQ_ACCOUNT_RECEIPT_FD;
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
        if (descriptor == BQ_ACCOUNT_RECEIPT_FD)
        {
            account_receipt_stats += 1;
            info->st_size = (off_t)strlen(account_receipt_bytes);
            if (account_fault == ACCOUNT_FILE_OWNER) info->st_uid = 65010;
            if (account_fault == ACCOUNT_FILE_MODE) info->st_mode = S_IFREG | 0644;
            if (account_fault == ACCOUNT_FILE_LINK) info->st_nlink = 2;
            if (account_fault == ACCOUNT_FILE_TYPE) info->st_mode = S_IFIFO | 0444;
            if (account_fault == ACCOUNT_FILE_OVERSIZE) info->st_size = 256;
            if (account_receipt_stats == 2)
            {
                if (account_fault == ACCOUNT_AFTER_INODE) info->st_ino += 1;
                if (account_fault == ACCOUNT_AFTER_MTIME) info->st_mtim.tv_nsec += 1;
                if (account_fault == ACCOUNT_AFTER_CTIME) info->st_ctim.tv_nsec += 1;
            }
        }
        if (descriptor == BQ_ACCOUNT_CONFIG_FD)
        {
            if (account_fault == ACCOUNT_DIRECTORY_OWNER) info->st_uid = 65010;
            if (account_fault == ACCOUNT_DIRECTORY_MODE) info->st_mode = S_IFDIR | 0777;
        }
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

static bool account_case(char const* label, uint32_t operation, uint32_t signal_number,
                         enum AccountMutation mutation, char const* receipt,
                         enum AccountFault fault, bool expected_state, unsigned expected_reads)
{
    static char const canonical[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
                                    "candidate-uid=65001\ncandidate-gid=65001\n"
                                    "runner-uid=65002\nrunner-gid=65002\n";
    int pair[2] = {-1, -1};
    int saved_stdin = dup(STDIN_FILENO);
    bool ok = saved_stdin >= 0 && socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0;
    account_ids = (BqBrokerAccounts){.service_uid = 65000, .service_gid = 65000,
        .candidate_uid = 65001, .candidate_gid = 65001,
        .runner_uid = 65002, .runner_gid = 65002};
    switch (mutation)
    {
        case ACCOUNT_UNCHANGED: break;
        case ACCOUNT_SERVICE_UID: account_ids.service_uid = 65003; break;
        case ACCOUNT_SERVICE_GID: account_ids.service_gid = 65003; break;
        case ACCOUNT_CANDIDATE_UID: account_ids.candidate_uid = 65003; break;
        case ACCOUNT_CANDIDATE_GID: account_ids.candidate_gid = 65003; break;
        case ACCOUNT_RUNNER_UID: account_ids.runner_uid = 65003; break;
        case ACCOUNT_RUNNER_GID: account_ids.runner_gid = 65003; break;
    }
    account_receipt_bytes = receipt ? receipt : canonical;
    account_receipt_offset = 0;
    account_receipt_reads = 0;
    account_receipt_stats = 0;
    account_state_walks = 0;
    account_manager_execs = 0;
    account_fault = fault;
    if (ok)
    {
        BqBrokerRequest request = {.magic = BQ_BROKER_MAGIC, .version = 1,
                                   .operation = operation, .stage = BQ_BROKER_OUTER,
                                   .signal_number = signal_number,
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
    printf("%s op=%u signal=%u ids=%u:%u,%u:%u,%u:%u receipt_reads=%u state=%d manager_execs=%u\n",
           label, operation, signal_number, account_ids.service_uid, account_ids.service_gid,
           account_ids.candidate_uid, account_ids.candidate_gid, account_ids.runner_uid,
           account_ids.runner_gid, account_receipt_reads, state, account_manager_execs);
    return ok && state == expected_state && account_manager_execs == 0 &&
           account_receipt_reads == expected_reads;
}

int main(void)
{
    static char const crlf[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
                               "candidate-uid=65001\ncandidate-gid=65001\n"
                               "runner-uid=65002\nrunner-gid=65002\r\n";
    static char const leading_zero[] = "BQ-ACCOUNTS-V1\nservice-uid=065000\nservice-gid=65000\n"
                                       "candidate-uid=65001\ncandidate-gid=65001\n"
                                       "runner-uid=65002\nrunner-gid=65002\n";
    static char const truncated[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
                                    "candidate-uid=65001\ncandidate-gid=65001\nrunner-uid=65002\n";
    static char const duplicate[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
                                    "candidate-uid=65001\ncandidate-gid=65001\n"
                                    "runner-uid=65002\nrunner-gid=65002\nrunner-gid=65002\n";
    static char const extra[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
                                "candidate-uid=65001\ncandidate-gid=65001\n"
                                "runner-uid=65002\nrunner-gid=65002\nextra=1\n";
    static char const mismatch[] = "BQ-ACCOUNTS-V1\nservice-uid=65000\nservice-gid=65000\n"
                                   "candidate-uid=65003\ncandidate-gid=65001\n"
                                   "runner-uid=65002\nrunner-gid=65002\n";
    bool ok = true;
    static enum AccountMutation const changes[] = {ACCOUNT_SERVICE_UID, ACCOUNT_SERVICE_GID,
        ACCOUNT_CANDIDATE_UID, ACCOUNT_CANDIDATE_GID, ACCOUNT_RUNNER_UID, ACCOUNT_RUNNER_GID};
    static char const* const roles[] = {"service_uid", "service_gid", "candidate_uid",
        "candidate_gid", "runner_uid", "runner_gid"};
    ok &= account_case("clean_start", BQ_BROKER_START, 0, ACCOUNT_UNCHANGED,
                       NULL, ACCOUNT_RECEIPT_OK, true, 2);
    ok &= account_case("clean_cont", BQ_BROKER_SIGNAL, BQ_BROKER_CONT, ACCOUNT_UNCHANGED,
                       NULL, ACCOUNT_RECEIPT_OK, true, 2);
    for (size_t index = 0; index < sizeof(changes) / sizeof(changes[0]); index += 1)
    {
        char label[64];
        snprintf(label, sizeof(label), "changed_%s_start", roles[index]);
        ok &= account_case(label, BQ_BROKER_START, 0, changes[index],
                           NULL, ACCOUNT_RECEIPT_OK, false, 2);
        snprintf(label, sizeof(label), "changed_%s_cont", roles[index]);
        ok &= account_case(label, BQ_BROKER_SIGNAL, BQ_BROKER_CONT, changes[index],
                           NULL, ACCOUNT_RECEIPT_OK, false, 2);
    }
#define ACCOUNT_REJECT(label, bytes, fault, reads) \
    ok &= account_case(label "_start", BQ_BROKER_START, 0, ACCOUNT_UNCHANGED, \
                       bytes, fault, false, reads); \
    ok &= account_case(label "_cont", BQ_BROKER_SIGNAL, BQ_BROKER_CONT, ACCOUNT_UNCHANGED, \
                       bytes, fault, false, reads)
    ACCOUNT_REJECT("missing", NULL, ACCOUNT_RECEIPT_MISSING, 0);
    ACCOUNT_REJECT("mismatched", mismatch, ACCOUNT_RECEIPT_OK, 2);
    ACCOUNT_REJECT("crlf", crlf, ACCOUNT_RECEIPT_OK, 2);
    ACCOUNT_REJECT("leading_zero", leading_zero, ACCOUNT_RECEIPT_OK, 2);
    ACCOUNT_REJECT("truncated", truncated, ACCOUNT_RECEIPT_OK, 2);
    ACCOUNT_REJECT("duplicate", duplicate, ACCOUNT_RECEIPT_OK, 2);
    ACCOUNT_REJECT("extra", extra, ACCOUNT_RECEIPT_OK, 2);
    ACCOUNT_REJECT("file_owner", NULL, ACCOUNT_FILE_OWNER, 0);
    ACCOUNT_REJECT("file_mode", NULL, ACCOUNT_FILE_MODE, 0);
    ACCOUNT_REJECT("file_link", NULL, ACCOUNT_FILE_LINK, 0);
    ACCOUNT_REJECT("file_type", NULL, ACCOUNT_FILE_TYPE, 0);
    ACCOUNT_REJECT("file_oversize", NULL, ACCOUNT_FILE_OVERSIZE, 0);
    ACCOUNT_REJECT("directory_owner", NULL, ACCOUNT_DIRECTORY_OWNER, 0);
    ACCOUNT_REJECT("directory_mode", NULL, ACCOUNT_DIRECTORY_MODE, 0);
    ACCOUNT_REJECT("after_inode", NULL, ACCOUNT_AFTER_INODE, 2);
    ACCOUNT_REJECT("after_mtime", NULL, ACCOUNT_AFTER_MTIME, 2);
    ACCOUNT_REJECT("after_ctime", NULL, ACCOUNT_AFTER_CTIME, 2);
#undef ACCOUNT_REJECT
    ok &= account_case("missing_term_cleanup", BQ_BROKER_SIGNAL, BQ_BROKER_TERM,
                       ACCOUNT_UNCHANGED, NULL, ACCOUNT_RECEIPT_MISSING, true, 0);
    ok &= account_case("missing_kill_cleanup", BQ_BROKER_SIGNAL, BQ_BROKER_KILL,
                       ACCOUNT_UNCHANGED, NULL, ACCOUNT_RECEIPT_MISSING, true, 0);
    ok &= account_case("mismatched_term_cleanup", BQ_BROKER_SIGNAL, BQ_BROKER_TERM,
                       ACCOUNT_UNCHANGED, mismatch, ACCOUNT_RECEIPT_OK, true, 0);
    ok &= account_case("mismatched_kill_cleanup", BQ_BROKER_SIGNAL, BQ_BROKER_KILL,
                       ACCOUNT_UNCHANGED, mismatch, ACCOUNT_RECEIPT_OK, true, 0);
    return ok ? 0 : 1;
}
