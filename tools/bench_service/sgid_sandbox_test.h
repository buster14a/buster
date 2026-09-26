/* Test-only Linux seccomp fixture for the service and recipe's real helpers.
 * A child installs it after constructing its fixture. Never install it in a
 * service process or the parent self-test process. */
#ifndef BUSTER_BENCH_SGID_SANDBOX_TEST_H
#define BUSTER_BENCH_SGID_SANDBOX_TEST_H
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <stddef.h>
#include <errno.h>

#if defined(__x86_64__)
#define BQ_TEST_AUDIT_ARCH AUDIT_ARCH_X86_64
#else
#define BQ_TEST_AUDIT_ARCH AUDIT_ARCH_AARCH64
#endif
#ifdef __NR_mkdir
#define BQ_TEST_NR_MKDIR __NR_mkdir
#else
#define BQ_TEST_NR_MKDIR (-1)
#endif

BUSTER_GLOBAL_LOCAL bool bq_test_sgid_fixture_group(char const* path, gid_t* group, bool* distinct)
{
    gid_t groups[64];
    int count = getgroups(64, groups);
    *group = getegid();
    *distinct = false;
    if (geteuid() == 0 && chown(path, (uid_t)-1, 65002) == 0)
    {
        *group = 65002;
        *distinct = *group != getegid();
    }
    if (!*distinct)
    {
        for (int index = 0; index < count && count <= 64; index += 1)
        {
            if (groups[index] != getegid() && chown(path, (uid_t)-1, groups[index]) == 0)
            {
                *group = groups[index];
                *distinct = true;
                break;
            }
        }
    }
    bool ok = count >= 0 && count <= 64 && chmod(path, 02770) == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_test_install_sgid_restriction(void)
{
    /* For the four syscalls used by directory creation/chmod, reject any
     * explicit setuid/setgid mode. The arch check prevents silent nr aliasing. */
    struct sock_filter instructions[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, BQ_TEST_AUDIT_ARCH, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mkdirat, 0, 2),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
        BPF_STMT(BPF_JMP | BPF_JA, 10),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_fchmod, 0, 2),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1])),
        BPF_STMT(BPF_JMP | BPF_JA, 7),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_fchmodat, 0, 2),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
        BPF_STMT(BPF_JMP | BPF_JA, 4),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, BQ_TEST_NR_MKDIR, 0, 2),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1])),
        BPF_STMT(BPF_JMP | BPF_JA, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_ALU | BPF_AND | BPF_K, S_ISUID | S_ISGID),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog program = {.len = sizeof(instructions) / sizeof(instructions[0]), .filter = instructions};
    bool ok = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
              prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0 &&
              prctl(PR_GET_SECCOMP, 0, 0, 0, 0) == SECCOMP_MODE_FILTER;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_test_sgid_controls(int parent, char const* existing)
{
    errno = 0;
    bool ok = mkdirat(parent, "explicit-sgid", 02770) == -1 && errno == EPERM;
    errno = 0;
    ok = ok && fchmod(parent, 02770) == -1 && errno == EPERM;
    errno = 0;
    ok = ok && fchmodat(parent, existing, 02770, 0) == -1 && errno == EPERM;
    return ok;
}
#endif
#endif
