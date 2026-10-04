/* First executable of each fixed transient unit.  The root broker supplies a
 * bounded numeric credential snapshot; PID1 drops privileges before invoking
 * this static, root-owned binary.  No job program starts on a mismatch.
 *
 * The installation must use a static binary with no ELF interpreter.  This
 * source performs no NSS lookup and never attempts to gain or drop privilege.
 * Stages 0..5 are the smoke recipe's; 16..28 are the zen5-calibration-v1
 * stages of zen5_stage.h (#426), which keep the smoke environment.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/capability.h>
#include <sys/fsuid.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include "zen5_stage.h"
#include "native_profile.h"

#define BQ_GATE_SERVICE "/usr/local/libexec/buster-bench-service"
#define BQ_GATE_BUILD "/usr/local/libexec/buster-bench-build"
#define BQ_GATE_THROUGHPUT "/usr/local/libexec/buster-bench-throughput"
#define BQ_GATE_MAX_GROUPS 32
/* Stages 0..5 are the smoke recipe's; 16..28 are the zen5 stages. */
#define BQ_GATE_LAST_STAGE BQ_NATIVE_STAGE

static bool bq_gate_decimal(char const* text, unsigned long* output)
{
    unsigned long value = 0;
    bool ok = text && text[0] >= '0' && text[0] <= '9' &&
              (text[0] != '0' || text[1] == 0);
    for (size_t index = 0; ok && text[index]; index += 1)
    {
        unsigned digit = (unsigned)(text[index] - '0');
        ok = digit <= 9 && value <= (ULONG_MAX - digit) / 10;
        if (ok) value = value * 10 + digit;
    }
    if (ok) *output = value;
    return ok;
}

static bool bq_gate_groups(char const* text, gid_t output[BQ_GATE_MAX_GROUPS], int* count)
{
    bool ok = text && *text;
    unsigned long previous = 0;
    int used = 0;
    while (ok && *text)
    {
        char digits[24];
        size_t length = 0;
        while (text[length] && text[length] != ',' && length < sizeof(digits) - 1)
        {
            digits[length] = text[length];
            length += 1;
        }
        ok = length > 0 && length < sizeof(digits) - 1 && used < BQ_GATE_MAX_GROUPS;
        if (ok)
        {
            digits[length] = 0;
            unsigned long value = 0;
            ok = bq_gate_decimal(digits, &value) && value > 0 && value < (unsigned long)(gid_t)-1 &&
                 (gid_t)value == value && (used == 0 || value > previous);
            if (ok) { output[used++] = (gid_t)value; previous = value; }
        }
        text += length;
        if (ok && *text == ',')
        {
            text += 1;
            ok = *text != 0;
        }
        else if (ok)
            ok = *text == 0;
    }
    if (ok) *count = used;
    return ok && used > 0;
}

static bool bq_gate_identity(uid_t expected_uid, gid_t expected_gid,
                             gid_t const expected_groups[BQ_GATE_MAX_GROUPS], int expected_count)
{
    uid_t real_uid = 0, effective_uid = 0, saved_uid = 0;
    gid_t real_gid = 0, effective_gid = 0, saved_gid = 0;
    gid_t actual[BQ_GATE_MAX_GROUPS];
    bool ok = expected_uid != 0 && expected_uid != (uid_t)-1 &&
              expected_gid != 0 && expected_gid != (gid_t)-1 &&
              expected_count > 0 && expected_count <= BQ_GATE_MAX_GROUPS &&
              getresuid(&real_uid, &effective_uid, &saved_uid) == 0 &&
              getresgid(&real_gid, &effective_gid, &saved_gid) == 0 &&
              real_uid == expected_uid && effective_uid == expected_uid && saved_uid == expected_uid &&
              real_gid == expected_gid && effective_gid == expected_gid && saved_gid == expected_gid &&
              (uid_t)setfsuid((uid_t)-1) == expected_uid &&
              (gid_t)setfsgid((gid_t)-1) == expected_gid;
    int count = ok ? getgroups(BQ_GATE_MAX_GROUPS, actual) : -1;
    ok = ok && count == expected_count;
    for (int i = 0; ok && i < count; i += 1)
    {
        for (int j = i + 1; j < count; j += 1)
        {
            if (actual[j] < actual[i])
            {
                gid_t swap = actual[i];
                actual[i] = actual[j];
                actual[j] = swap;
            }
        }
        ok = actual[i] == expected_groups[i];
    }
    return ok;
}

static bool bq_gate_privileges(void)
{
    struct __user_cap_header_struct header = {.version = _LINUX_CAPABILITY_VERSION_3, .pid = 0};
    struct __user_cap_data_struct data[2] = {{0}, {0}};
    bool ok = prctl(PR_GET_NO_NEW_PRIVS, 0UL, 0UL, 0UL, 0UL) == 1 &&
              syscall(SYS_capget, &header, data) == 0;
    for (unsigned index = 0; ok && index < 2; index += 1)
        ok = data[index].effective == 0 && data[index].permitted == 0 &&
             data[index].inheritable == 0;
    bool end_seen = false;
    for (int index = 0; ok && index < 64; index += 1)
    {
        int bounding = prctl(PR_CAPBSET_READ, (unsigned long)index, 0UL, 0UL, 0UL);
        if (bounding < 0 && errno == EINVAL)
            end_seen = true;
        else
            ok = bounding == 0 &&
                 prctl(PR_CAP_AMBIENT, (unsigned long)PR_CAP_AMBIENT_IS_SET,
                       (unsigned long)index, 0UL, 0UL) == 0;
        if (end_seen) break;
    }
    return ok && end_seen;
}

/* Zen5 stage: builds run the fixed driver's generate/build, oracle and
 * captures its capture verb, and pmu the fixed interpreter's -B mode. */
static bool bq_gate_zen5(unsigned long stage, char const* program, char const* first_argument)
{
    unsigned long index = stage - BQ_ZEN5_STAGE_FIRST_NUMBER;
    bool ok = stage >= BQ_ZEN5_STAGE_FIRST_NUMBER && stage < BQ_NATIVE_STAGE;
    if (ok && index < BQ_ZEN5_STAGE_ORACLE)
        ok = !strcmp(program, BQ_ZEN5_STAGE_DRIVER) && !strcmp(first_argument, index % 2u == 0 ? "generate" : "build");
    else if (ok && index == BQ_ZEN5_STAGE_PMU)
        ok = !strcmp(program, BQ_ZEN5_STAGE_PYTHON) && !strcmp(first_argument, "-B");
    else if (ok)
        ok = !strcmp(program, BQ_ZEN5_STAGE_DRIVER) && !strcmp(first_argument, BQ_ZEN5_STAGE_CAPTURE_VERB);
    return ok;
}

static bool bq_gate_program(unsigned long stage, char const* program, char const* first_argument)
{
    bool ok = program && first_argument && (stage <= 5 || (stage >= BQ_ZEN5_STAGE_FIRST_NUMBER &&
                                                          stage <= BQ_GATE_LAST_STAGE));
    if (ok && stage == 0)
        ok = !strcmp(program, BQ_GATE_SERVICE) && !strcmp(first_argument, "worker-unit");
    else if (ok && stage == BQ_NATIVE_STAGE)
        ok = !strcmp(program, BQ_GATE_SERVICE) && !strcmp(first_argument, "native-payload");
    else if (ok && stage == 5)
        ok = !strcmp(program, BQ_GATE_THROUGHPUT) && !strcmp(first_argument, "run");
    else if (ok && stage >= BQ_ZEN5_STAGE_FIRST_NUMBER)
        ok = bq_gate_zen5(stage, program, first_argument);
    else if (ok)
        ok = !strcmp(program, BQ_GATE_BUILD) &&
             !strcmp(first_argument, stage == 1 || stage == 3 ? "generate" : "build");
    return ok;
}

static int bq_gate_self_test(void)
{
    gid_t groups[BQ_GATE_MAX_GROUPS];
    int count = 0;
    unsigned long value = 0;
    unsigned checks = 0;
    bool ok = true;
#define BQ_GATE_CHECK(condition) do { checks += 1; if (!(condition)) ok = false; } while (0)
    BQ_GATE_CHECK(bq_gate_decimal("65000", &value) && value == 65000);
    BQ_GATE_CHECK(!bq_gate_decimal("065000", &value) && !bq_gate_decimal("+1", &value));
    BQ_GATE_CHECK(bq_gate_groups("65000,65001", groups, &count) && count == 2 &&
                  groups[0] == 65000 && groups[1] == 65001);
    BQ_GATE_CHECK(!bq_gate_groups("65000,65000", groups, &count));
    BQ_GATE_CHECK(!bq_gate_groups("0,65001", groups, &count));
    BQ_GATE_CHECK(!bq_gate_groups("65001,", groups, &count));
    BQ_GATE_CHECK(bq_gate_program(0, BQ_GATE_SERVICE, "worker-unit") &&
                  !bq_gate_program(0, BQ_GATE_BUILD, "worker-unit"));
    BQ_GATE_CHECK(bq_gate_program(3, BQ_GATE_BUILD, "generate") &&
                  !bq_gate_program(3, BQ_GATE_BUILD, "build"));
    BQ_GATE_CHECK(bq_gate_program(5, BQ_GATE_THROUGHPUT, "run") &&
                  !bq_gate_program(5, BQ_GATE_SERVICE, "run"));
    BQ_GATE_CHECK(bq_gate_program(16, BQ_GATE_BUILD, "generate") && bq_gate_program(17, BQ_GATE_BUILD, "build") &&
                  bq_gate_program(24, BQ_GATE_BUILD, "generate") && bq_gate_program(25, BQ_GATE_BUILD, "build"));
    BQ_GATE_CHECK(!bq_gate_program(16, BQ_GATE_BUILD, "build") && !bq_gate_program(25, BQ_GATE_BUILD, "generate") &&
                  !bq_gate_program(18, BQ_GATE_SERVICE, "generate"));
    BQ_GATE_CHECK(bq_gate_program(26, BQ_GATE_BUILD, BQ_ZEN5_STAGE_CAPTURE_VERB) &&
                  bq_gate_program(28, BQ_GATE_BUILD, BQ_ZEN5_STAGE_CAPTURE_VERB) &&
                  !bq_gate_program(26, BQ_GATE_BUILD, "build") && !bq_gate_program(28, BQ_ZEN5_STAGE_PYTHON, "-B"));
    BQ_GATE_CHECK(bq_gate_program(27, BQ_ZEN5_STAGE_PYTHON, "-B") && !bq_gate_program(27, BQ_ZEN5_STAGE_PYTHON, "-c") &&
                  !bq_gate_program(27, BQ_GATE_BUILD, BQ_ZEN5_STAGE_CAPTURE_VERB));
    BQ_GATE_CHECK(!bq_gate_program(6, BQ_GATE_BUILD, "generate") && !bq_gate_program(15, BQ_GATE_BUILD, "build") &&
                  !bq_gate_program(29, BQ_GATE_BUILD, "generate") && !bq_gate_program(29, BQ_GATE_BUILD, "build"));
    BQ_GATE_CHECK(bq_gate_program(BQ_NATIVE_STAGE, BQ_GATE_SERVICE, "native-payload") &&
                  !bq_gate_program(BQ_NATIVE_STAGE, BQ_GATE_SERVICE, "worker-unit") &&
                  !bq_gate_program(BQ_NATIVE_STAGE, "/tmp/program", "native-payload") &&
                  !bq_gate_program(BQ_NATIVE_STAGE + 1, BQ_GATE_SERVICE, "native-payload"));
    /* Every argv the broker builds from the shared contract passes here. */
    for (unsigned index = 0; index < BQ_ZEN5_STAGE_COUNT; index += 1)
    {
        BqZen5StageCommand command;
        BQ_GATE_CHECK(bq_zen5_stage_command(index, "/srv/w/1/2", &command) && command.count >= 2 &&
                      bq_gate_program(BQ_ZEN5_STAGE_FIRST_NUMBER + index, command.argv[0], command.argv[1]));
    }
    printf("BQ_CREDENTIAL_GATE_SELF_TEST checks=%u result=%s\n", checks, ok ? "pass" : "fail");
    return ok ? 0 : 1;
#undef BQ_GATE_CHECK
}

int main(int argc, char** argv)
{
    unsigned long stage = 0, uid_number = 0, gid_number = 0;
    gid_t expected[BQ_GATE_MAX_GROUPS];
    int group_count = 0;
    bool self_test = argc == 2 && !strcmp(argv[1], "--self-test");
    bool ok = !self_test && argc >= 8 && bq_gate_decimal(argv[1], &stage) && stage <= BQ_GATE_LAST_STAGE &&
              bq_gate_decimal(argv[2], &uid_number) && uid_number > 0 &&
              uid_number < (unsigned long)(uid_t)-1 && (uid_t)uid_number == uid_number &&
              bq_gate_decimal(argv[3], &gid_number) && gid_number > 0 &&
              gid_number < (unsigned long)(gid_t)-1 && (gid_t)gid_number == gid_number &&
              bq_gate_groups(argv[4], expected, &group_count) && !strcmp(argv[5], "--") &&
              bq_gate_program(stage, argv[6], argv[7]);
    if (ok) ok = bq_gate_identity((uid_t)uid_number, (gid_t)gid_number, expected, group_count) &&
                 bq_gate_privileges();
    if (ok) ok = clearenv() == 0 && setenv("PATH", "/usr/bin:/bin", 1) == 0 &&
                 setenv("LC_ALL", "C", 1) == 0;
    if (ok) execv(argv[6], argv + 6);
    return self_test ? bq_gate_self_test() : 126;
}
