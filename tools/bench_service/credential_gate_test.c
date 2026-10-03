/* Bounded first-executable credential gate.  All kernel identity calls and the
 * final exec are intercepted; no manager, account database or payload runs. */
#define _GNU_SOURCE 1
#include <stdarg.h>
#define main bq_gate_original_main
#define getresuid bq_test_getresuid
#define getresgid bq_test_getresgid
#define getgroups bq_test_getgroups
#define setfsuid bq_test_setfsuid
#define setfsgid bq_test_setfsgid
#define prctl bq_test_prctl
#define syscall bq_test_syscall
#define execv bq_test_execv
#include "credential_gate.c"
#undef main
#undef getresuid
#undef getresgid
#undef getgroups
#undef setfsuid
#undef setfsgid
#undef prctl
#undef syscall
#undef execv

static uid_t test_uid[3];
static gid_t test_gid[3];
static gid_t test_groups[BQ_GATE_MAX_GROUPS + 1];
static int test_group_count;
static int test_execs;
static int test_fsuid;
static int test_fsgid;
static int test_nnp;
static bool test_capability;

int bq_test_setfsuid(uid_t uid) { (void)uid; return test_fsuid; }
int bq_test_setfsgid(gid_t gid) { (void)gid; return test_fsgid; }

int bq_test_prctl(int option, ...)
{
    int result = -1;
    va_list args;
    va_start(args, option);
    unsigned long argument = va_arg(args, unsigned long);
    va_end(args);
    if (option == PR_GET_NO_NEW_PRIVS) result = test_nnp;
    else if (option == PR_CAPBSET_READ)
    {
        if (argument < 41) result = test_capability && argument == CAP_SETGID ? 1 : 0;
        else errno = EINVAL;
    }
    else if (option == PR_CAP_AMBIENT) result = test_capability && argument == PR_CAP_AMBIENT_IS_SET ? 1 : 0;
    return result;
}

long bq_test_syscall(long number, ...)
{
    long result = -1;
    va_list args;
    va_start(args, number);
    struct __user_cap_header_struct* header = va_arg(args, struct __user_cap_header_struct*);
    struct __user_cap_data_struct* data = va_arg(args, struct __user_cap_data_struct*);
    va_end(args);
    if (number == SYS_capget && header->version == _LINUX_CAPABILITY_VERSION_3)
    {
        memset(data, 0, 2 * sizeof(*data));
        if (test_capability) data[0].effective = 1u << CAP_SETGID;
        result = 0;
    }
    return result;
}

int bq_test_getresuid(uid_t* real, uid_t* effective, uid_t* saved)
{
    *real = test_uid[0]; *effective = test_uid[1]; *saved = test_uid[2];
    return 0;
}

int bq_test_getresgid(gid_t* real, gid_t* effective, gid_t* saved)
{
    *real = test_gid[0]; *effective = test_gid[1]; *saved = test_gid[2];
    return 0;
}

int bq_test_getgroups(int count, gid_t* groups)
{
    int result = -1;
    if (count >= test_group_count)
    {
        memcpy(groups, test_groups, (size_t)test_group_count * sizeof(gid_t));
        result = test_group_count;
    }
    return result;
}

/* The environment the payload would inherit, captured at the exec. */
static char test_environment[4][160];
static int test_environment_count;

int bq_test_execv(char const* path, char* const argv[])
{
    extern char** environ;
    if (!strcmp(path, argv[0])) test_execs += 1;
    test_environment_count = 0;
    for (char** entry = environ; entry && *entry; entry += 1)
    {
        if (test_environment_count < 4)
            snprintf(test_environment[test_environment_count], sizeof(test_environment[0]), "%s", *entry);
        test_environment_count += 1;
    }
    errno = ENOENT;
    return -1;
}

static bool test_environment_has(char const* entry)
{
    bool found = false;
    for (int index = 0; index < test_environment_count && index < 4; index += 1)
        found = found || !strcmp(test_environment[index], entry);
    return found;
}

static bool run_case(char const* label, int argc, char** argv, uid_t uid,
                     gid_t gid, gid_t const* groups, int count, bool expect_exec)
{
    test_uid[0] = test_uid[1] = test_uid[2] = uid;
    test_gid[0] = test_gid[1] = test_gid[2] = gid;
    test_group_count = count;
    test_fsuid = uid; test_fsgid = gid; test_nnp = 1; test_capability = false;
    if (count > 0) memcpy(test_groups, groups, (size_t)count * sizeof(gid_t));
    test_execs = 0;
    int result = bq_gate_original_main(argc, argv);
    bool ok = result == 126 && (test_execs == 1) == expect_exec;
    printf("%s result=%d execs=%d expected=%d\n", label, result, test_execs, expect_exec);
    return ok;
}

int main(void)
{
    char* service[] = {"gate", "0", "65000", "65000", "65000,65001", "--",
                       BQ_GATE_SERVICE, "worker-unit", "fixed", NULL};
    char* candidate[] = {"gate", "3", "65001", "65001", "65001", "--",
                         BQ_GATE_BUILD, "generate", "fixed", NULL};
    char* throughput[] = {"gate", "5", "65001", "65001", "65001", "--",
                          BQ_GATE_THROUGHPUT, "run", "fixed", NULL};
    gid_t service_groups[] = {65001, 65000};
    gid_t candidate_groups[] = {65001};
    gid_t contaminated[] = {65000, 65001};
    gid_t oversize[BQ_GATE_MAX_GROUPS + 1];
    for (int index = 0; index <= BQ_GATE_MAX_GROUPS; index += 1) oversize[index] = (gid_t)(65001 + index);
    bool ok = true;
    ok &= run_case("service", 9, service, 65000, 65000, service_groups, 2, true);
    ok &= run_case("candidate", 9, candidate, 65001, 65001, candidate_groups, 1, true);
    ok &= run_case("throughput", 9, throughput, 65001, 65001, candidate_groups, 1, true);
    /* Smoke stages keep exactly the fixed system PATH and locale. */
    setenv("BQ_GATE_TEST_INHERITED", "leak", 1);
    ok &= run_case("candidate_environment", 9, candidate, 65001, 65001, candidate_groups, 1, true) &&
          test_environment_count == 2 && test_environment_has("PATH=/usr/bin:/bin") &&
          test_environment_has("LC_ALL=C");
    /* #1020 retirement stages: the same driver, exactly the matched-build
     * environment with the installed toolchain on PATH, nothing inherited. */
    char* retirement_base[] = {"gate", "6", "65000", "65000", "65000,65001", "--",
                               BQ_GATE_BUILD, "generate", "fixed", NULL};
    char* retirement_candidate[] = {"gate", "9", "65001", "65001", "65001", "--",
                                    BQ_GATE_BUILD, "build", "fixed", NULL};
    char const* retirement_environment[] = {
        "PATH=/opt/buster-bench/installed/toolchain/native-retirement-performance-v1/bin",
        "LC_ALL=C", "TZ=UTC", "HOME=/nonexistent"};
    setenv("BQ_GATE_TEST_INHERITED", "leak", 1);
    setenv("HOME", "/root", 1);
    bool retirement_ok = run_case("retirement_base", 9, retirement_base, 65000, 65000, service_groups, 2, true) &&
                         test_environment_count == 4;
    for (int index = 0; index < 4; index += 1)
        retirement_ok = retirement_ok && test_environment_has(retirement_environment[index]);
    setenv("BQ_GATE_TEST_INHERITED", "leak", 1);
    retirement_ok = retirement_ok &&
                    run_case("retirement_candidate", 9, retirement_candidate, 65001, 65001, candidate_groups, 1,
                             true) && test_environment_count == 4;
    for (int index = 0; index < 4; index += 1)
        retirement_ok = retirement_ok && test_environment_has(retirement_environment[index]);
    ok &= retirement_ok;
    ok &= run_case("retirement_candidate_as_service", 9, retirement_candidate, 65001, 65001, contaminated, 2,
                   false);
    retirement_candidate[7] = "generate";
    ok &= run_case("retirement_wrong_operation", 9, retirement_candidate, 65001, 65001, candidate_groups, 1,
                   false);
    retirement_candidate[7] = "build";
    retirement_candidate[6] = BQ_GATE_THROUGHPUT;
    ok &= run_case("retirement_wrong_program", 9, retirement_candidate, 65001, 65001, candidate_groups, 1, false);
    retirement_candidate[6] = BQ_GATE_BUILD;
    retirement_candidate[1] = "10";
    ok &= run_case("retirement_unknown_stage", 9, retirement_candidate, 65001, 65001, candidate_groups, 1, false);
    retirement_candidate[1] = "9";
    ok &= run_case("pid1_added_service", 9, candidate, 65001, 65001, contaminated, 2, false);
    ok &= run_case("pid1_omitted_group", 9, service, 65000, 65000, candidate_groups, 1, false);
    ok &= run_case("pid1_oversized_set", 9, candidate, 65001, 65001, oversize,
                   BQ_GATE_MAX_GROUPS + 1, false);
    ok &= run_case("pid1_changed_uid", 9, candidate, 65003, 65001, candidate_groups, 1, false);
    ok &= run_case("pid1_root", 9, candidate, 0, 65001, candidate_groups, 1, false);
    ok &= run_case("pid1_changed_gid", 9, candidate, 65001, 65003, candidate_groups, 1, false);
    test_uid[0] = test_uid[1] = test_uid[2] = 65001;
    test_uid[2] = 0;
    test_gid[0] = test_gid[1] = test_gid[2] = 65001;
    test_group_count = 1; test_groups[0] = 65001; test_execs = 0;
    ok &= bq_gate_original_main(9, candidate) == 126 && test_execs == 0;
    test_gid[2] = 0;
    test_uid[2] = 65001;
    ok &= bq_gate_original_main(9, candidate) == 126 && test_execs == 0;
    test_gid[2] = 65001;
    test_nnp = 0;
    ok &= bq_gate_original_main(9, candidate) == 126 && test_execs == 0;
    test_nnp = 1;
    test_capability = true;
    ok &= bq_gate_original_main(9, candidate) == 126 && test_execs == 0;
    test_capability = false;
    test_fsuid = 0;
    ok &= bq_gate_original_main(9, candidate) == 126 && test_execs == 0;
    test_fsuid = 65001;
    candidate[4] = "65001,65001";
    ok &= run_case("duplicate_pin", 9, candidate, 65001, 65001, candidate_groups, 1, false);
    candidate[4] = "0,65001";
    ok &= run_case("root_pin", 9, candidate, 65001, 65001, candidate_groups, 1, false);
    candidate[4] = "65001,";
    ok &= run_case("trailing_comma", 9, candidate, 65001, 65001, candidate_groups, 1, false);
    candidate[4] = "65001";
    candidate[6] = BQ_GATE_SERVICE;
    ok &= run_case("wrong_program", 9, candidate, 65001, 65001, candidate_groups, 1, false);
    candidate[6] = BQ_GATE_BUILD;
    candidate[7] = "build";
    ok &= run_case("wrong_operation", 9, candidate, 65001, 65001, candidate_groups, 1, false);
    return ok ? 0 : 1;
}
