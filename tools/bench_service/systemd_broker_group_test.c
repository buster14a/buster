/* Source-inclusion regression for the broker's request-time NSS boundary.
 * The real server receives a valid seqpacket, but account/peer credentials and
 * group lists are deterministic. The first protected-path open is intercepted
 * and refused: no manager or host state is touched. Compiling this fixture
 * against the pre-guard broker source fails the contaminated-role cases.
 * Run from the repository root with:
 * gcc -std=c11 -O2 -Wall -Wextra -Werror -fwrapv -fno-strict-aliasing
 *     -funsigned-char tools/bench_service/systemd_broker_group_test.c -o /tmp/bq-broker-groups
 * /tmp/bq-broker-groups
 * The same flags with -O1 -g -fsanitize=address,undefined also pass. */
#define _GNU_SOURCE 1
#define main bq_review_original_main
#define getpwnam bq_review_getpwnam
#define geteuid bq_review_geteuid
#define getegid bq_review_getegid
#define getgroups bq_review_getgroups
#define getgrouplist bq_review_getgrouplist
#define getsockopt bq_review_getsockopt
#define open bq_review_open
#include "systemd_broker.c"
#undef main
#undef getpwnam
#undef geteuid
#undef getegid
#undef getgroups
#undef getgrouplist
#undef getsockopt
#undef open
static uint32_t review_operation;
static uid_t review_candidate_uid;
static gid_t review_candidate_gid;
static gid_t review_service_gid;
static uid_t review_runner_uid;
static unsigned review_protected_opens;
static gid_t review_candidate_extra;
static gid_t review_runner_extra;
static gid_t review_service_extra;
static int review_list_failure;
static unsigned review_account_group_queries;

struct passwd* bq_review_getpwnam(char const* name)
{
    static struct passwd identity;
    identity = (struct passwd){0};
    if (!strcmp(name, "buster-bench"))
    {
        identity.pw_uid = 65000;
        identity.pw_gid = review_service_gid;
    }
    else if (!strcmp(name, "buster-bench-candidate"))
    {
        identity.pw_uid = review_candidate_uid;
        identity.pw_gid = review_candidate_gid;
    }
    else if (!strcmp(name, "buster-github-runner"))
    {
        identity.pw_uid = review_runner_uid;
        identity.pw_gid = 65002;
    }
    else
        return NULL;
    return &identity;
}

uid_t bq_review_geteuid(void) { return 0; }
gid_t bq_review_getegid(void) { return review_service_gid; }
int bq_review_getgroups(int size, gid_t groups[])
{
    if (size >= 2)
    {
        groups[0] = review_service_gid;
        groups[1] = review_candidate_gid;
    }
    return size >= 2 ? 2 : -1;
}
int bq_review_getgrouplist(char const* user, gid_t primary, gid_t* groups, int* count)
{
    gid_t extra = !strcmp(user, "buster-bench-candidate") ? review_candidate_extra :
                  !strcmp(user, "buster-github-runner") ? review_runner_extra : review_service_extra;
    review_account_group_queries += 1;
    if (review_list_failure) { *count = review_list_failure == 1 ? 33 : 0; return -1; }
    bool service_foreign = !strcmp(user, "buster-bench") && extra == 65003;
    int needed = service_foreign ? 3 : extra == primary ? 1 : 2;
    if (*count < needed) { *count = needed; return -1; }
    groups[0] = primary;
    if (needed >= 2) groups[1] = service_foreign ? 65001 : extra;
    if (service_foreign) groups[2] = extra;
    *count = needed;
    return needed;
}
int bq_review_getsockopt(int fd, int level, int option, void* output, socklen_t* length)
{
    (void)fd;
    if (level != SOL_SOCKET || option != SO_PEERCRED || *length < sizeof(struct ucred)) return -1;
    *(struct ucred*)output = (struct ucred){.pid = getpid(), .uid = 65000, .gid = review_service_gid};
    *length = sizeof(struct ucred);
    return 0;
}
int bq_review_open(char const* path, int flags, ...)
{
    (void)flags;
    if (!strcmp(path, "/")) review_protected_opens += 1;
    errno = EACCES;
    return -1;
}

static bool run_case(char const* label, gid_t service_extra, gid_t candidate_extra,
                     gid_t runner_extra, int list_failure, unsigned expected_queries,
                     bool expect_protected_path)
{
    int peers[2] = {-1, -1};
    int saved_stdin = dup(STDIN_FILENO);
    bool ok = saved_stdin >= 0 && socketpair(AF_UNIX, SOCK_SEQPACKET, 0, peers) == 0;
    review_candidate_uid = 65001;
    review_candidate_gid = 65001;
    review_service_gid = 65000;
    review_runner_uid = 65002;
    review_candidate_extra = candidate_extra;
    review_runner_extra = runner_extra;
    review_service_extra = service_extra;
    review_list_failure = list_failure;
    review_protected_opens = 0;
    review_account_group_queries = 0;
    if (ok)
    {
        BqBrokerRequest request = {.magic=BQ_BROKER_MAGIC, .version=1, .operation=review_operation,
            .signal_number=review_operation == BQ_BROKER_SIGNAL ? BQ_BROKER_CONT : 0,
            .job=1, .attempt=2,
            .base="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            .candidate="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
        if (review_operation == BQ_BROKER_SIGNAL) { memset(request.base, 0, sizeof(request.base)); memset(request.candidate, 0, sizeof(request.candidate)); }
        ok = send(peers[1], &request, sizeof(request), 0) == sizeof(request) &&
             dup2(peers[0], STDIN_FILENO) == STDIN_FILENO;
    }
    if (ok) bq_broker_server();
    if (saved_stdin >= 0) { dup2(saved_stdin, STDIN_FILENO); close(saved_stdin); }
    if (peers[0] >= 0) close(peers[0]);
    if (peers[1] >= 0) close(peers[1]);
    printf("%s op=%u service_extra=%u candidate_extra=%u runner_extra=%u list_failure=%d queries=%u protected_path_checked=%u\n",
           label, review_operation, service_extra, candidate_extra, runner_extra, list_failure,
           review_account_group_queries, review_protected_opens > 0);
    return ok && review_account_group_queries == expected_queries &&
           (review_protected_opens > 0) == expect_protected_path;
}

int main(void)
{
    bool ok = true;
    for (unsigned operation = 0; operation < 2; operation += 1)
    {
    review_operation = operation ? BQ_BROKER_SIGNAL : BQ_BROKER_START;
    ok &= run_case("valid", 65001, 65001, 65002, 0, 3, true);
    ok &= run_case("service_missing_candidate", 65000, 65001, 65002, 0, 1, false);
    ok &= run_case("service_root", 0, 65001, 65002, 0, 1, false);
    ok &= run_case("service_foreign", 65003, 65001, 65002, 0, 1, false);
    ok &= run_case("candidate_service", 65001, 65000, 65002, 0, 2, false);
    ok &= run_case("candidate_root", 65001, 0, 65002, 0, 2, false);
    ok &= run_case("candidate_foreign", 65001, 65003, 65002, 0, 2, false);
    ok &= run_case("runner_service", 65001, 65001, 65000, 0, 3, false);
    ok &= run_case("runner_root", 65001, 65001, 0, 0, 3, false);
    ok &= run_case("runner_candidate", 65001, 65001, 65001, 0, 3, false);
    ok &= run_case("runner_foreign", 65001, 65001, 65003, 0, 3, false);
    ok &= run_case("nss_oversize", 65001, 65001, 65002, 1, 1, false);
    ok &= run_case("nss_failure", 65001, 65001, 65002, 2, 1, false);
    }
    return ok ? 0 : 1;
}
