/* Disposable real-systemd regression for the broker's private-state boundary.
 *
 * Run as root in an isolated, provisioned container while one exact
 * validate-buster-v1 outer unit is active. The live service creates the queue,
 * record, result and locked lease. This test starts the socket, makes a valid
 * exact-instance request through the constrained template, and rejects peers
 * and identities that must not reach the manager. It never changes host state
 * outside the disposable container. --isolation-only checks the provisioned
 * private hierarchy without manager calls; --self-test checks identity policy
 * without privileges. bq_test_enter_identity preserves and reads back account
 * groups; bq_test_private_access owns non-destructive DAC denial checks.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stddef.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BQ_TEST_BROKER "/usr/local/libexec/buster-bench-systemd-broker"
#define BQ_TEST_BROKER_SOCKET "/run/buster-bench-systemd-broker/control.sock"
#define BQ_TEST_STATE "/var/lib/buster-bench"
#define BQ_TEST_GROUP_CAP 64

/* Mirror the broker's fixed wire envelope. The probe sends an otherwise
 * valid signal request so the reachable root peer is rejected by the server,
 * and accepts only an actual framed status on that same connection. */
typedef struct BqTestBrokerRequest
{
    uint32_t magic, version, operation, stage, signal_number, reserved;
    uint64_t job, attempt;
    char base[65], candidate[65];
} BqTestBrokerRequest;

typedef struct BqTestBrokerStatus
{
    uint32_t kind, length;
    int32_t status;
} BqTestBrokerStatus;

_Static_assert(sizeof(BqTestBrokerRequest) == 176, "broker request envelope changed");
_Static_assert(sizeof(BqTestBrokerStatus) == 12, "broker status envelope changed");

typedef struct BqTestIdentity
{
    char const* name;
    uid_t uid;
    gid_t gid;
    gid_t groups[BQ_TEST_GROUP_CAP];
    int count;
} BqTestIdentity;

static bool bq_test_identity_load(char const* name, BqTestIdentity* identity)
{
    struct passwd* account = getpwnam(name);
    *identity = (BqTestIdentity){.name = name, .uid = (uid_t)-1, .gid = (gid_t)-1,
                                .count = BQ_TEST_GROUP_CAP};
    bool ok = account != NULL;
    if (ok)
    {
        identity->uid = account->pw_uid;
        identity->gid = account->pw_gid;
        ok = getgrouplist(name, identity->gid, identity->groups, &identity->count) >= 0 &&
             identity->count > 0 && identity->count <= BQ_TEST_GROUP_CAP;
    }
    return ok;
}

static bool bq_test_group_member(BqTestIdentity const* identity, gid_t group)
{
    bool found = identity->gid == group;
    for (int index = 0; index < identity->count && index < BQ_TEST_GROUP_CAP; index += 1)
        found |= identity->groups[index] == group;
    return found;
}

static bool bq_test_identity_isolated(BqTestIdentity const* identity, gid_t service_gid)
{
    bool ok = identity->uid != 0 && identity->uid != (uid_t)-1 &&
              identity->gid != 0 && identity->gid != (gid_t)-1 &&
              identity->count > 0 && identity->count <= BQ_TEST_GROUP_CAP &&
              !bq_test_group_member(identity, service_gid) && !bq_test_group_member(identity, 0);
    return ok;
}

static bool bq_test_groups_equal(BqTestIdentity const* identity, gid_t const* actual, int count)
{
    bool ok = count >= 0 && count <= BQ_TEST_GROUP_CAP && count == identity->count;
    for (int index = 0; ok && index < count; index += 1)
    {
        bool expected_found = false, actual_found = false;
        for (int other = 0; other < count; other += 1)
        {
            expected_found |= identity->groups[index] == actual[other];
            actual_found |= actual[index] == identity->groups[other];
        }
        ok = expected_found && actual_found;
    }
    return ok;
}

/* Do not manufacture an empty supplementary set. Re-resolve the account at
 * the credential transition, then verify the entire effective set against the
 * earlier snapshot. A concurrent membership change is a failed probe. */
static bool bq_test_enter_identity(BqTestIdentity const* identity)
{
    bool ok = initgroups(identity->name, identity->gid) == 0 &&
              setresgid(identity->gid, identity->gid, identity->gid) == 0 &&
              setresuid(identity->uid, identity->uid, identity->uid) == 0 &&
              prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0;
    gid_t actual[BQ_TEST_GROUP_CAP];
    int count = ok ? getgroups(BQ_TEST_GROUP_CAP, actual) : -1;
    ok = ok && getuid() == identity->uid && geteuid() == identity->uid &&
         getgid() == identity->gid && getegid() == identity->gid &&
         bq_test_groups_equal(identity, actual, count);
    fprintf(stderr, "BROKER_PROBE_IDENTITY name=%s uid=%u gid=%u groups=", identity->name,
            (unsigned)geteuid(), (unsigned)getegid());
    for (int index = 0; index < count && index < BQ_TEST_GROUP_CAP; index += 1)
        fprintf(stderr, "%s%u", index ? "," : "", (unsigned)actual[index]);
    fprintf(stderr, " verified=%s\n", ok ? "yes" : "no");
    return ok;
}

static int bq_test_wait(pid_t child)
{
    int result = -1;
    if (child > 0)
    {
        int status = 0;
        pid_t waited;
        do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
        if (waited == child && WIFEXITED(status)) result = WEXITSTATUS(status);
    }
    return result;
}

/* Never create/truncate/write a private object to test denial. Require an
 * authorization error; ENOENT, an unsupported flag, or a broken fixture is not
 * evidence of isolation. */
static bool bq_test_permission_error(int error)
{
    return error == EACCES || error == EPERM;
}

static bool bq_test_denied_open(char const* path, int flags)
{
    errno = 0;
    int fd = open(path, flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    int error = errno;
    bool denied = fd < 0 && bq_test_permission_error(error);
    if (fd >= 0) close(fd);
    if (!denied) fprintf(stderr, "BROKER_PRIVATE_DENIAL failed path=%s flags=%d errno=%d\n", path, flags, error);
    return denied;
}

static bool bq_test_denied_search(char const* path)
{
    errno = 0;
    int fd = open(path, O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int error = errno;
    bool denied = fd < 0 && bq_test_permission_error(error);
    if (fd >= 0)
    {
        errno = 0;
        int result = fchdir(fd);
        error = errno;
        denied = result < 0 && bq_test_permission_error(error);
        close(fd);
    }
    if (!denied) fprintf(stderr, "BROKER_PRIVATE_SEARCH failed path=%s errno=%d\n", path, error);
    return denied;
}

static bool bq_test_existing_payload(char const* path, uid_t owner, gid_t group)
{
    struct stat before = {0}, opened = {0};
    bool ok = lstat(path, &before) == 0 && S_ISREG(before.st_mode) &&
              before.st_uid == owner && before.st_gid == group && before.st_nlink == 1 &&
              (before.st_mode & 0777) == 0400;
    int fd = ok ? open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK) : -1;
    ok = ok && fd >= 0 && fstat(fd, &opened) == 0 &&
         opened.st_dev == before.st_dev && opened.st_ino == before.st_ino &&
         opened.st_uid == before.st_uid && opened.st_gid == before.st_gid &&
         (opened.st_mode & 07777) == (before.st_mode & 07777);
    if (fd >= 0) close(fd);
    fprintf(stderr, "BROKER_PRIVATE_PAYLOAD path=%s verified=%s\n", path, ok ? "yes" : "no");
    return ok;
}

static int bq_test_private_access(BqTestIdentity const* identity, char const* worker,
                                   char const* instance, char const* result, char const* payload)
{
    pid_t child = fork();
    if (child == 0)
    {
        bool ok = bq_test_enter_identity(identity);
        char const* files[] = {worker, instance, BQ_TEST_STATE "/lease/host.lock"};
        for (unsigned index = 0; ok && index < sizeof(files) / sizeof(files[0]); index += 1)
            ok = bq_test_denied_open(files[index], O_RDONLY) && bq_test_denied_open(files[index], O_WRONLY) &&
                  bq_test_denied_open(files[index], O_RDWR);
        if (ok)
        {
            ok = bq_test_denied_open(payload, O_RDONLY) && bq_test_denied_open(payload, O_WRONLY) &&
                 bq_test_denied_open(payload, O_RDWR);
            fprintf(stderr, "BROKER_PRIVATE_PAYLOAD_ACCESS identity=%s read/write/rw_denied=%s\n",
                    identity->name, ok ? "yes" : "no");
        }
        char const* directories[] = {BQ_TEST_STATE "/queue", BQ_TEST_STATE "/lease",
                                     BQ_TEST_STATE "/workspaces/results", result};
        for (unsigned index = 0; ok && index < sizeof(directories) / sizeof(directories[0]); index += 1)
            ok = bq_test_denied_open(directories[index], O_RDONLY | O_DIRECTORY) &&
                 bq_test_denied_search(directories[index]);
        _exit(ok ? 0 : 1);
    }
    return bq_test_wait(child);
}

static bool bq_test_authorization_denied(bool created, int connected, int error)
{
    bool ok = created && connected < 0 && bq_test_permission_error(error);
    return ok;
}

static bool bq_test_socket_verified(char const* path, uid_t owner, gid_t group)
{
    struct stat info = {0};
    bool ok = lstat(path, &info) == 0 && S_ISSOCK(info.st_mode) &&
              info.st_uid == owner && info.st_gid == group &&
              (info.st_mode & 07777) == 0600;
    fprintf(stderr, "BROKER_SOCKET_FIXTURE path=%s verified=%s\n", path, ok ? "yes" : "no");
    return ok;
}

static bool bq_test_socket_denied(char const* path)
{
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    int create_error = errno;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    bool valid_path = strlen(path) < sizeof(address.sun_path);
    if (valid_path) memcpy(address.sun_path, path, strlen(path) + 1);
    errno = 0;
    int connected = fd >= 0 && valid_path ? connect(fd, (struct sockaddr*)&address, sizeof(address)) : -1;
    int error = errno;
    bool ok = valid_path && bq_test_authorization_denied(fd >= 0, connected, error);
    if (fd >= 0) close(fd);
    fprintf(stderr, "BROKER_SOCKET_DAC path=%s socket_errno=%d connect=%d errno=%d authorization_denied=%s\n",
            path, fd >= 0 ? 0 : create_error, connected, error, ok ? "yes" : "no");
    return ok;
}

static int bq_test_socket_denied_identity(BqTestIdentity const* identity)
{
    pid_t child = fork();
    if (child == 0)
    {
        bool ok = bq_test_enter_identity(identity) &&
                  !bq_test_socket_denied("/proc/self/bq-broker-missing.sock") &&
                  bq_test_socket_denied(BQ_TEST_BROKER_SOCKET);
        _exit(ok ? 0 : 1);
    }
    return bq_test_wait(child);
}

static bool bq_test_rejection_frame(BqTestBrokerStatus const* frame, ssize_t size)
{
    bool ok = size == (ssize_t)sizeof(*frame) && frame->kind == 3 &&
              frame->length == sizeof(frame->status) && frame->status == 126;
    return ok;
}

static bool bq_test_root_rejected(char const* job, char const* attempt)
{
    errno = 0;
    char* job_end = NULL;
    unsigned long long job_number = strtoull(job, &job_end, 10);
    bool ok = errno == 0 && job_end != job && *job_end == 0 && job_number != 0;
    errno = 0;
    char* attempt_end = NULL;
    unsigned long long attempt_number = strtoull(attempt, &attempt_end, 10);
    ok = ok && errno == 0 && attempt_end != attempt && *attempt_end == 0 && attempt_number != 0 &&
         getuid() == 0 && geteuid() == 0;
    BqTestBrokerRequest request = {.magic = 0x42515344u, .version = 1, .operation = 2,
                                   .signal_number = 3, .job = job_number, .attempt = attempt_number};
    int fd = ok ? socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0) : -1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path, BQ_TEST_BROKER_SOCKET, sizeof(BQ_TEST_BROKER_SOCKET));
    struct timeval timeout = {.tv_sec = 5};
    ok = ok && fd >= 0 &&
         setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 &&
         setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
         connect(fd, (struct sockaddr*)&address, sizeof(address)) == 0 &&
         send(fd, &request, sizeof(request), MSG_NOSIGNAL) == (ssize_t)sizeof(request);
    BqTestBrokerStatus frame = {0};
    ssize_t size = ok ? recv(fd, &frame, sizeof(frame), MSG_TRUNC) : -1;
    ok = ok && bq_test_rejection_frame(&frame, size);
    if (fd >= 0) close(fd);
    fprintf(stderr, "BROKER_ROOT_PEER_REJECTION received=%zd kind=%u length=%u status=%d verified=%s\n",
            size, frame.kind, frame.length, frame.status, ok ? "yes" : "no");
    return ok;
}

static int bq_test_self_test(void)
{
    unsigned checks = 0, failures = 0;
#define BQ_IDENTITY_CHECK(expression) do { checks += 1; if (!(expression)) { failures += 1; \
    fprintf(stderr, "BROKER_IDENTITY_TEST failure line=%d: %s\n", __LINE__, #expression); } } while (0)
    BqTestIdentity clean = {.uid = 65001, .gid = 65001, .groups = {65001}, .count = 1};
    BQ_IDENTITY_CHECK(bq_test_identity_isolated(&clean, 65000));
    BqTestIdentity changed = clean;
    changed.groups[changed.count++] = 65000;
    BQ_IDENTITY_CHECK(!bq_test_identity_isolated(&changed, 65000));
    BQ_IDENTITY_CHECK(!bq_test_groups_equal(&changed, clean.groups, clean.count));
    changed = clean;
    changed.gid = 65000;
    BQ_IDENTITY_CHECK(!bq_test_identity_isolated(&changed, 65000));
    changed = clean;
    changed.groups[changed.count++] = 0;
    BQ_IDENTITY_CHECK(!bq_test_identity_isolated(&changed, 65000));
    changed = clean;
    changed.uid = 0;
    BQ_IDENTITY_CHECK(!bq_test_identity_isolated(&changed, 65000));
    changed.uid = (uid_t)-1;
    BQ_IDENTITY_CHECK(!bq_test_identity_isolated(&changed, 65000));
    changed = clean;
    changed.count = 0;
    BQ_IDENTITY_CHECK(!bq_test_identity_isolated(&changed, 65000));
    changed.count = BQ_TEST_GROUP_CAP + 1;
    BQ_IDENTITY_CHECK(!bq_test_identity_isolated(&changed, 65000));
    BQ_IDENTITY_CHECK(!bq_test_groups_equal(&changed, clean.groups, -1));
    changed = clean;
    changed.groups[changed.count++] = 65003;
    gid_t reordered[] = {65003, 65001}, duplicated[] = {65001, 65001};
    BQ_IDENTITY_CHECK(bq_test_groups_equal(&changed, reordered, 2));
    BQ_IDENTITY_CHECK(!bq_test_groups_equal(&changed, duplicated, 2));
    BQ_IDENTITY_CHECK(!bq_test_groups_equal(&clean, reordered, 2));
    BQ_IDENTITY_CHECK(bq_test_permission_error(EACCES));
    BQ_IDENTITY_CHECK(bq_test_permission_error(EPERM));
    BQ_IDENTITY_CHECK(!bq_test_permission_error(ENOENT));
    BQ_IDENTITY_CHECK(!bq_test_permission_error(EIO));
    BQ_IDENTITY_CHECK(bq_test_authorization_denied(true, -1, EACCES));
    BQ_IDENTITY_CHECK(bq_test_authorization_denied(true, -1, EPERM));
    BQ_IDENTITY_CHECK(!bq_test_authorization_denied(false, -1, EPERM));
    BQ_IDENTITY_CHECK(!bq_test_authorization_denied(true, 0, EACCES));
    BQ_IDENTITY_CHECK(!bq_test_authorization_denied(true, -1, ENOENT));
    BQ_IDENTITY_CHECK(!bq_test_authorization_denied(true, -1, ECONNREFUSED));
    BQ_IDENTITY_CHECK(!bq_test_socket_verified("/proc/self/bq-broker-missing.sock", 0, 0));
    BqTestBrokerStatus frame = {.kind = 3, .length = sizeof(int32_t), .status = 126};
    BQ_IDENTITY_CHECK(bq_test_rejection_frame(&frame, sizeof(frame)));
    BQ_IDENTITY_CHECK(!bq_test_rejection_frame(&frame, sizeof(frame) - 1));
    frame.kind = 2;
    BQ_IDENTITY_CHECK(!bq_test_rejection_frame(&frame, sizeof(frame)));
    frame.kind = 3;
    frame.length = 1;
    BQ_IDENTITY_CHECK(!bq_test_rejection_frame(&frame, sizeof(frame)));
    frame.length = sizeof(frame.status);
    frame.status = 0;
    BQ_IDENTITY_CHECK(!bq_test_rejection_frame(&frame, sizeof(frame)));
    BQ_IDENTITY_CHECK(!bq_test_existing_payload("/proc/self/bq-broker-missing-payload", 0, 0));
#undef BQ_IDENTITY_CHECK
    printf("BROKER_IDENTITY_SELF_TEST checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}

static bool bq_test_decimal(char const* text)
{
    bool ok = text && text[0];
    for (size_t index = 0; ok && text[index]; index += 1)
        ok = text[index] >= '0' && text[index] <= '9';
    return ok;
}

static bool bq_test_revision(char const* text)
{
    size_t length = text ? strlen(text) : 0;
    bool ok = length == 40 || length == 64;
    for (size_t index = 0; ok && index < length; index += 1)
        ok = (text[index] >= '0' && text[index] <= '9') ||
             (text[index] >= 'a' && text[index] <= 'f');
    return ok;
}

static bool bq_test_path(char const* path, uid_t owner, gid_t group, mode_t mode, bool directory)
{
    struct stat info = {0};
    bool ok = lstat(path, &info) == 0 &&
              (directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)) &&
              info.st_uid == owner && info.st_gid == group &&
              (info.st_mode & 07777) == mode && (directory || info.st_nlink == 1);
    return ok;
}

static int bq_test_run(BqTestIdentity const* identity, char* const arguments[])
{
    pid_t child = fork();
    if (child == 0)
    {
        if (!bq_test_enter_identity(identity)) _exit(127);
        execv(arguments[0], arguments);
        _exit(127);
    }
    return bq_test_wait(child);
}

static int bq_test_live(int argc, char** argv)
{
    bool isolation_only = argc == 4 && !strcmp(argv[1], "--isolation-only");
    bool isolated = access("/.dockerenv", F_OK) == 0 || access("/run/.containerenv", F_OK) == 0;
    bool ok = (argc == 5 || isolation_only) && isolated && geteuid() == 0 &&
              getenv("BUSTER_BROKER_LIVE_TEST") &&
              !strcmp(getenv("BUSTER_BROKER_LIVE_TEST"), "1");
    char const* job = ok ? argv[isolation_only ? 2 : 1] : NULL;
    char const* attempt = ok ? argv[isolation_only ? 3 : 2] : NULL;
    ok = ok && bq_test_decimal(job) && bq_test_decimal(attempt) &&
         (isolation_only || (bq_test_revision(argv[3]) && bq_test_revision(argv[4])));
    BqTestIdentity service, candidate, runner, root;
    bool accounts = bq_test_identity_load("buster-bench", &service) &&
                    bq_test_identity_load("buster-bench-candidate", &candidate) &&
                    bq_test_identity_load("buster-github-runner", &runner) &&
                    bq_test_identity_load("root", &root);
    ok = ok && accounts && root.uid == 0 && service.uid != 0 && service.gid != 0 &&
         candidate.uid != service.uid && runner.uid != service.uid && runner.uid != candidate.uid &&
         bq_test_group_member(&service, candidate.gid) && !bq_test_group_member(&runner, candidate.gid) &&
         bq_test_identity_isolated(&candidate, service.gid) && bq_test_identity_isolated(&runner, service.gid);
    if (!ok) fprintf(stderr, "BROKER_ISOLATION precondition or account/group policy failed\n");
    uid_t service_uid = ok ? service.uid : (uid_t)-1;
    gid_t service_gid = ok ? service.gid : (gid_t)-1;
    char unit[128], wrong_unit[128], result[256], payload[272], worker[256], instance[256];
    int unit_size = ok ? snprintf(unit, sizeof(unit), "buster-bench-%s-%s.service", job, attempt) : -1;
    char const* wrong_attempt = ok && !strcmp(attempt, "999999") ? "999998" : "999999";
    int wrong_size = ok ? snprintf(wrong_unit, sizeof(wrong_unit), "buster-bench-%s-%s.service", job, wrong_attempt) : -1;
    int result_size = ok ? snprintf(result, sizeof(result), BQ_TEST_STATE "/workspaces/results/job-%s-attempt-%s", job, attempt) : -1;
    int payload_size = ok && result_size > 0 && (size_t)result_size < sizeof(result) ?
                       snprintf(payload, sizeof(payload), "%s/payload", result) : -1;
    int worker_size = ok ? snprintf(worker, sizeof(worker), BQ_TEST_STATE "/queue/worker-%s", job) : -1;
    int instance_size = ok ? snprintf(instance, sizeof(instance), BQ_TEST_STATE "/queue/worker-instance-%s", job) : -1;
    ok = ok && unit_size > 0 && (size_t)unit_size < sizeof(unit) &&
         wrong_size > 0 && (size_t)wrong_size < sizeof(wrong_unit) &&
          result_size > 0 && (size_t)result_size < sizeof(result) &&
          payload_size > 0 && (size_t)payload_size < sizeof(payload) &&
         worker_size > 0 && (size_t)worker_size < sizeof(worker) &&
         instance_size > 0 && (size_t)instance_size < sizeof(instance);
    unsigned checks = 0;
#define BQ_LIVE_CHECK(condition) do { checks += 1; if (!(condition)) ok = false; } while (0)
    if (ok)
    {
        BQ_LIVE_CHECK(bq_test_path(BQ_TEST_STATE "/queue", service_uid, service_gid, 0710, true));
        BQ_LIVE_CHECK(bq_test_path(BQ_TEST_STATE "/lease", service_uid, service_gid, 0710, true));
        BQ_LIVE_CHECK(bq_test_path(BQ_TEST_STATE "/lease/host.lock", service_uid, service_gid, 0640, false));
        BQ_LIVE_CHECK(bq_test_path(BQ_TEST_STATE "/workspaces/results", service_uid, service_gid, 0710, true));
        BQ_LIVE_CHECK(bq_test_path(result, service_uid, service_gid, 0700, true));
        BQ_LIVE_CHECK(bq_test_existing_payload(payload, service_uid, service_gid));
        BQ_LIVE_CHECK(bq_test_path(worker, service_uid, service_gid, 0440, false));
        BQ_LIVE_CHECK(bq_test_path(instance, service_uid, service_gid, 0440, false));
        BQ_LIVE_CHECK(bq_test_private_access(&candidate, worker, instance, result, payload) == 0);
        BQ_LIVE_CHECK(bq_test_private_access(&runner, worker, instance, result, payload) == 0);
    }
    if (ok && !isolation_only)
    {
        char* start_socket[] = {"/usr/bin/systemctl", "start", "buster-bench-systemd-broker.socket", NULL};
        BQ_LIVE_CHECK(bq_test_run(&root, start_socket) == 0);
        BQ_LIVE_CHECK(bq_test_socket_verified(BQ_TEST_BROKER_SOCKET, service_uid, service_gid));
        char* active[] = {"/usr/bin/systemctl", "is-active", "--quiet", unit, NULL};
        BQ_LIVE_CHECK(bq_test_run(&root, active) == 0);
        char* resume[] = {BQ_TEST_BROKER, "signal", unit, "CONT", NULL};
        BQ_LIVE_CHECK(bq_test_run(&service, resume) == 0);
        /* Connect denial and a server frame on the request-bearing root
         * connection are separate boundaries; CLI 126 proves neither one. */
        BQ_LIVE_CHECK(bq_test_root_rejected(job, attempt));
        BQ_LIVE_CHECK(bq_test_socket_denied_identity(&candidate) == 0);
        BQ_LIVE_CHECK(bq_test_socket_denied_identity(&runner) == 0);
        char* wrong_instance[] = {BQ_TEST_BROKER, "signal", wrong_unit, "CONT", NULL};
        BQ_LIVE_CHECK(bq_test_run(&service, wrong_instance) == 126);
        char* wrong_signal[] = {BQ_TEST_BROKER, "signal", unit, "HUP", NULL};
        BQ_LIVE_CHECK(bq_test_run(&service, wrong_signal) == 126);
        char wrong_revision[65];
        memset(wrong_revision, '0', strlen(argv[3]));
        wrong_revision[strlen(argv[3])] = 0;
        if (!strcmp(wrong_revision, argv[3])) wrong_revision[0] = '1';
        char* wrong_source[] = {BQ_TEST_BROKER, "start-outer", argv[1], argv[2], wrong_revision, argv[4], NULL};
        BQ_LIVE_CHECK(bq_test_run(&service, wrong_source) == 126);
        BQ_LIVE_CHECK(bq_test_run(&root, active) == 0);
    }
#undef BQ_LIVE_CHECK
    printf("%s checks=%u result=%s\n", isolation_only ? "BROKER_PRIVATE_ISOLATION_TEST" :
           "BUSTER_SYSTEMD_BROKER_LIVE_TEST", checks, ok ? "pass" : "fail");
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    int result = argc == 2 && !strcmp(argv[1], "--self-test") ? bq_test_self_test() : bq_test_live(argc, argv);
    return result;
}
