/* Disposable real-systemd regression for the broker's private-state boundary.
 *
 * Run as root in an isolated, provisioned container while one exact
 * validate-buster-v1 outer unit is active. The live service creates the queue,
 * record, result and locked lease. This test starts the socket, makes a valid
 * exact-instance request through the constrained template, and rejects peers
 * and identities that must not reach the manager. It never changes host state
 * outside the disposable container.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BQ_TEST_BROKER "/usr/local/libexec/buster-bench-systemd-broker"
#define BQ_TEST_STATE "/var/lib/buster-bench"

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

static int bq_test_run(uid_t uid, gid_t gid, char* const arguments[])
{
    pid_t child = fork();
    int result = -1;
    if (child == 0)
    {
        if (setgroups(0, NULL) != 0 || setgid(gid) != 0 || setuid(uid) != 0) _exit(127);
        execv(arguments[0], arguments);
        _exit(127);
    }
    if (child > 0)
    {
        int status = 0;
        if (waitpid(child, &status, 0) == child && WIFEXITED(status)) result = WEXITSTATUS(status);
    }
    return result;
}

int main(int argc, char** argv)
{
    bool isolated = access("/.dockerenv", F_OK) == 0 || access("/run/.containerenv", F_OK) == 0;
    bool ok = argc == 5 && isolated && geteuid() == 0 &&
              getenv("BUSTER_BROKER_LIVE_TEST") &&
              !strcmp(getenv("BUSTER_BROKER_LIVE_TEST"), "1") &&
              bq_test_decimal(argv[1]) && bq_test_decimal(argv[2]) &&
              bq_test_revision(argv[3]) && bq_test_revision(argv[4]);
    struct passwd* service = getpwnam("buster-bench");
    uid_t service_uid = service ? service->pw_uid : (uid_t)-1;
    gid_t service_gid = service ? service->pw_gid : (gid_t)-1;
    struct passwd* candidate = getpwnam("buster-bench-candidate");
    uid_t candidate_uid = candidate ? candidate->pw_uid : (uid_t)-1;
    gid_t candidate_gid = candidate ? candidate->pw_gid : (gid_t)-1;
    struct passwd* runner = getpwnam("buster-github-runner");
    uid_t runner_uid = runner ? runner->pw_uid : (uid_t)-1;
    gid_t runner_gid = runner ? runner->pw_gid : (gid_t)-1;
    ok = ok && service_uid != (uid_t)-1 && service_gid != (gid_t)-1 &&
         candidate_uid != (uid_t)-1 && candidate_gid != (gid_t)-1 &&
         runner_uid != (uid_t)-1 && runner_gid != (gid_t)-1 &&
         service_gid != candidate_gid && service_gid != runner_gid;
    char unit[128], wrong_unit[128], result[256], worker[256], instance[256];
    int unit_size = ok ? snprintf(unit, sizeof(unit), "buster-bench-%s-%s.service", argv[1], argv[2]) : -1;
    char const* wrong_attempt = ok && !strcmp(argv[2], "999999") ? "999998" : "999999";
    int wrong_size = ok ? snprintf(wrong_unit, sizeof(wrong_unit), "buster-bench-%s-%s.service", argv[1], wrong_attempt) : -1;
    int result_size = ok ? snprintf(result, sizeof(result), BQ_TEST_STATE "/workspaces/results/job-%s-attempt-%s", argv[1], argv[2]) : -1;
    int worker_size = ok ? snprintf(worker, sizeof(worker), BQ_TEST_STATE "/queue/worker-%s", argv[1]) : -1;
    int instance_size = ok ? snprintf(instance, sizeof(instance), BQ_TEST_STATE "/queue/worker-instance-%s", argv[1]) : -1;
    ok = ok && unit_size > 0 && (size_t)unit_size < sizeof(unit) &&
         wrong_size > 0 && (size_t)wrong_size < sizeof(wrong_unit) &&
         result_size > 0 && (size_t)result_size < sizeof(result) &&
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
        BQ_LIVE_CHECK(bq_test_path(worker, service_uid, service_gid, 0440, false));
        BQ_LIVE_CHECK(bq_test_path(instance, service_uid, service_gid, 0440, false));
        char* start_socket[] = {"/usr/bin/systemctl", "start", "buster-bench-systemd-broker.socket", NULL};
        BQ_LIVE_CHECK(bq_test_run(0, 0, start_socket) == 0);
        char* active[] = {"/usr/bin/systemctl", "is-active", "--quiet", unit, NULL};
        BQ_LIVE_CHECK(bq_test_run(0, 0, active) == 0);
        char* resume[] = {BQ_TEST_BROKER, "signal", unit, "CONT", NULL};
        BQ_LIVE_CHECK(bq_test_run(service_uid, service_gid, resume) == 0);
        BQ_LIVE_CHECK(bq_test_run(candidate_uid, candidate_gid, resume) == 126);
        BQ_LIVE_CHECK(bq_test_run(runner_uid, runner_gid, resume) == 126);
        char* wrong_instance[] = {BQ_TEST_BROKER, "signal", wrong_unit, "CONT", NULL};
        BQ_LIVE_CHECK(bq_test_run(service_uid, service_gid, wrong_instance) == 126);
        char* wrong_signal[] = {BQ_TEST_BROKER, "signal", unit, "HUP", NULL};
        BQ_LIVE_CHECK(bq_test_run(service_uid, service_gid, wrong_signal) == 126);
        char wrong_revision[65];
        memset(wrong_revision, '0', strlen(argv[3]));
        wrong_revision[strlen(argv[3])] = 0;
        if (!strcmp(wrong_revision, argv[3])) wrong_revision[0] = '1';
        char* wrong_source[] = {BQ_TEST_BROKER, "start-outer", argv[1], argv[2], wrong_revision, argv[4], NULL};
        BQ_LIVE_CHECK(bq_test_run(service_uid, service_gid, wrong_source) == 126);
        BQ_LIVE_CHECK(bq_test_run(0, 0, active) == 0);
    }
#undef BQ_LIVE_CHECK
    printf("BUSTER_SYSTEMD_BROKER_LIVE_TEST checks=%u result=%s\n", checks, ok ? "pass" : "fail");
    return ok ? 0 : 1;
}
