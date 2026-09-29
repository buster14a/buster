/* The retirement child sandbox and canonical child layout, shared by lane
 * B's check runner and row producer (bench_service/retirement_check_runner.c,
 * retirement_row_producer.c) and lane D's measured launches
 * (platform.h, tp_process_observe_inputs with a layout) (#881).
 *
 * Ownership: lane B's sandbox, moved here unchanged so both lanes run
 * candidate-derived code in the same child. The canonical layout is the one
 * lane B's row plan derives its command digests in: the side's held binary
 * at descriptor 3 (baseline) or 4 (candidate), A's base and candidate roots
 * at 5 and 6, and the step's work directory at 7, which is also the working
 * directory (BQ_RETIREMENT_ROW_WORK_PATH).
 *
 * Entry points (all in the forked child except the ruleset):
 *   bq_retirement_sandbox            the Landlock ruleset of one child
 *   bq_retirement_sandbox_normalize  default signal dispositions, empty mask,
 *                                    the umask, close-on-exec from 3, no core
 *                                    and the address-space limit
 *   bq_retirement_sandbox_slots      every held descriptor and the ruleset
 *                                    moved above BQ_RETIREMENT_ROW_SLOT_HIGH,
 *                                    then the four into the canonical slots
 *   bq_retirement_sandbox_enter      no new privileges, the ruleset, then the
 *                                    seccomp socket and io_uring filter
 *
 * What the sandbox covers. bq_retirement_sandbox requires Landlock ABI
 * BQ_RETIREMENT_SANDBOX_MIN_ABI (6) and refuses anything older; its
 * filesystem rules let the child read and execute the system trees (/usr,
 * /lib*, /bin, /sbin), read /etc and A's two source roots, execute and read
 * the held binaries (and a check's tools), use /dev/null, read the random and
 * zero devices and use its own work directory freely. Every other path
 * (evidence directories, the reference oracle's outputs, other steps'
 * directories, /proc and /sys) is denied for opening, listing, creating,
 * renaming, linking, removing and truncating. Landlock also denies TCP bind
 * and connect and scopes signals and abstract unix sockets to the child's
 * domain. bq_retirement_sandbox_enter then installs a seccomp filter that
 * refuses (EPERM) socket, socketpair, connect, bind, listen, accept, accept4,
 * sendto, sendmsg, sendmmsg and io_uring, because Landlock does not govern
 * connect() to a path-named unix socket, and kills any other architecture or
 * x32 call. Metadata changes (chmod, chown, utimes, xattr) on files the
 * service user owns are not governed at any Landlock ABI.
 *
 * Map: BqRetirementSandboxAttributes, bq_retirement_sandbox_abi (with the
 * test-only bq_retirement_sandbox_abi_ceiling), bq_retirement_sandbox_rule,
 * bq_retirement_sandbox_system_rule.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_SANDBOX_H
#define BUSTER_THROUGHPUT_RETIREMENT_SANDBOX_H

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/landlock.h>
#include <linux/seccomp.h>

#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1u << 2)
#endif

/* The canonical child layout (see the file header). */
#define BQ_RETIREMENT_ROW_SLOT_BINARY 3
#define BQ_RETIREMENT_ROW_SLOT_SOURCE 5
#define BQ_RETIREMENT_ROW_SLOT_WORK 7
#define BQ_RETIREMENT_ROW_WORK_PATH "/proc/self/fd/7"
/* Descriptors are parked at or above this before the slots are filled, so no
 * source descriptor is overwritten by an earlier dup2. */
#define BQ_RETIREMENT_ROW_SLOT_HIGH 64

/* The lowest Landlock ABI the child sandbox accepts: ABI 6 is the first that
 * also scopes signals and abstract unix sockets to the child's domain, so
 * every rule the sandbox states is enforced. An older kernel is refused
 * (BQ_CONFIGURATION_MISMATCH), never run with fewer rules. */
#define BQ_RETIREMENT_SANDBOX_MIN_ABI 6u

/* Landlock rights by bit, as the kernel UAPI numbers them, so an older
 * linux/landlock.h still builds: ABI 1 handles bits 0-12, ABI 2 adds REFER,
 * ABI 3 TRUNCATE, ABI 5 IOCTL_DEV; ABI 4 adds TCP bind/connect and ABI 6 the
 * signal and abstract-socket scopes. */
#define BQ_RETIREMENT_SANDBOX_FS_EXECUTE (1ull << 0)
#define BQ_RETIREMENT_SANDBOX_FS_WRITE_FILE (1ull << 1)
#define BQ_RETIREMENT_SANDBOX_FS_READ_FILE (1ull << 2)
#define BQ_RETIREMENT_SANDBOX_FS_READ_DIR (1ull << 3)
#define BQ_RETIREMENT_SANDBOX_FS_ABI1 ((1ull << 13) - 1u)
#define BQ_RETIREMENT_SANDBOX_FS_REFER (1ull << 13)
#define BQ_RETIREMENT_SANDBOX_FS_TRUNCATE (1ull << 14)
#define BQ_RETIREMENT_SANDBOX_FS_IOCTL_DEV (1ull << 15)
#define BQ_RETIREMENT_SANDBOX_NET_TCP 3ull
#define BQ_RETIREMENT_SANDBOX_SCOPES 3ull

typedef struct BqRetirementSandboxAttributes
{
    uint64_t handled_access_fs, handled_access_net, scoped;
} BqRetirementSandboxAttributes;

/* The seccomp filter's architecture: the build's own; any other is killed.
 * On x86-64 the x32 system calls share the architecture value and carry
 * this bit in their number, so they are killed too. The io_uring numbers are
 * the same on every architecture and absent from older headers. */
#if defined(__x86_64__)
#define BQ_RETIREMENT_SANDBOX_AUDIT_ARCH AUDIT_ARCH_X86_64
#define BQ_RETIREMENT_SANDBOX_X32_BIT 0x40000000u
#elif defined(__aarch64__)
#define BQ_RETIREMENT_SANDBOX_AUDIT_ARCH AUDIT_ARCH_AARCH64
#endif
#if defined(BQ_RETIREMENT_SANDBOX_AUDIT_ARCH)
#define BQ_RETIREMENT_SANDBOX_FILTERED 1
#else
#define BQ_RETIREMENT_SANDBOX_FILTERED 0
#endif
#define BQ_RETIREMENT_SANDBOX_IO_URING_SETUP 425u
#define BQ_RETIREMENT_SANDBOX_IO_URING_ENTER 426u
#define BQ_RETIREMENT_SANDBOX_IO_URING_REGISTER 427u

#if defined(BQ_RETIREMENT_CORRECTNESS_TEST_ONLY)
/* Test-only: caps the Landlock ABI the sandbox sees, so a test can exercise
 * the refusal below BQ_RETIREMENT_SANDBOX_MIN_ABI on a newer kernel. */
static uint32_t bq_retirement_sandbox_abi_ceiling = UINT32_MAX;
#endif

/* The kernel's Landlock ABI, or 0 without Landlock. */
static inline uint32_t bq_retirement_sandbox_abi(void)
{
    long abi = syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    uint32_t result = abi > 0 && abi < (long)UINT32_MAX ? (uint32_t)abi : 0;
#if defined(BQ_RETIREMENT_CORRECTNESS_TEST_ONLY)
    if (result > bq_retirement_sandbox_abi_ceiling) result = bq_retirement_sandbox_abi_ceiling;
#endif
    return result;
}

static inline bool bq_retirement_sandbox_rule(int ruleset, int parent, uint64_t allowed)
{
    struct landlock_path_beneath_attr beneath = {.allowed_access = allowed, .parent_fd = parent};
    bool ok = parent >= 0 && syscall(SYS_landlock_add_rule, ruleset, LANDLOCK_RULE_PATH_BENEATH, &beneath, 0) == 0;
    return ok;
}

/* A system path the child may use, when it exists. */
static inline bool bq_retirement_sandbox_system_rule(int ruleset, char const* path, bool directory, uint64_t allowed)
{
    int held = open(path, O_PATH | O_CLOEXEC | (directory ? O_DIRECTORY : 0));
    bool ok = held < 0 ? errno == ENOENT || errno == ENOTDIR : bq_retirement_sandbox_rule(ruleset, held, allowed);
    if (held >= 0) close(held);
    return ok;
}

/* The Landlock ruleset (close-on-exec) of one child that runs
 * candidate-derived code, or -1: see the file header. It is -1 without
 * Landlock ABI BQ_RETIREMENT_SANDBOX_MIN_ABI, on an architecture the seccomp
 * filter does not know, or when a rule fails. executables are the held files
 * it may execute and read, sources A's roots (read only) and work its own new
 * directory; abi, when not NULL, receives the Landlock ABI enforced (0 on
 * failure). */
static inline int bq_retirement_sandbox(int const* executables, uint32_t executable_count, int const sources[2],
    int work, uint32_t* abi)
{
    uint32_t version = bq_retirement_sandbox_abi();
    bool ok = BQ_RETIREMENT_SANDBOX_FILTERED && version >= BQ_RETIREMENT_SANDBOX_MIN_ABI && executables &&
              executable_count;
    /* ABI 6 handles every filesystem right, TCP bind and connect, and both
     * scopes. */
    uint64_t handled = BQ_RETIREMENT_SANDBOX_FS_ABI1 | BQ_RETIREMENT_SANDBOX_FS_REFER |
                       BQ_RETIREMENT_SANDBOX_FS_TRUNCATE | BQ_RETIREMENT_SANDBOX_FS_IOCTL_DEV;
    BqRetirementSandboxAttributes attributes = {handled, BQ_RETIREMENT_SANDBOX_NET_TCP, BQ_RETIREMENT_SANDBOX_SCOPES};
    int ruleset = ok ? (int)syscall(SYS_landlock_create_ruleset, &attributes, sizeof(attributes), 0) : -1;
    uint64_t read = BQ_RETIREMENT_SANDBOX_FS_READ_FILE | BQ_RETIREMENT_SANDBOX_FS_READ_DIR;
    uint64_t null_device = BQ_RETIREMENT_SANDBOX_FS_READ_FILE | BQ_RETIREMENT_SANDBOX_FS_WRITE_FILE |
                           BQ_RETIREMENT_SANDBOX_FS_TRUNCATE;
    static char const* const trees[] = {"/usr", "/lib", "/lib64", "/lib32", "/bin", "/sbin"};
    static char const* const devices[] = {"/dev/zero", "/dev/urandom", "/dev/random"};
    ok = ok && ruleset >= 0;
    for (uint32_t index = 0; ok && index < sizeof(trees) / sizeof(trees[0]); index += 1)
        ok = bq_retirement_sandbox_system_rule(ruleset, trees[index], true, read | BQ_RETIREMENT_SANDBOX_FS_EXECUTE);
    for (uint32_t index = 0; ok && index < sizeof(devices) / sizeof(devices[0]); index += 1)
        ok = bq_retirement_sandbox_system_rule(ruleset, devices[index], false, BQ_RETIREMENT_SANDBOX_FS_READ_FILE);
    for (uint32_t index = 0; ok && index < executable_count; index += 1)
        ok = bq_retirement_sandbox_rule(ruleset, executables[index],
                                        BQ_RETIREMENT_SANDBOX_FS_EXECUTE | BQ_RETIREMENT_SANDBOX_FS_READ_FILE);
    ok = ok && bq_retirement_sandbox_system_rule(ruleset, "/etc", true, read) &&
         bq_retirement_sandbox_system_rule(ruleset, "/dev/null", false, null_device) &&
         bq_retirement_sandbox_rule(ruleset, sources[0], read) && bq_retirement_sandbox_rule(ruleset, sources[1], read) &&
         bq_retirement_sandbox_rule(ruleset, work, handled);
    if (!ok && ruleset >= 0) close(ruleset);
    if (abi) *abi = ok ? version : 0;
    return ok ? ruleset : -1;
}

/* In the forked child: the normalized state lane B's steps run in. Default
 * dispositions for the signals the parent handles or ignores, an empty
 * signal mask, the umask, close-on-exec for every descriptor above stderr,
 * no core file and memory_bytes of address space (0 leaves it unlimited). */
static inline bool bq_retirement_sandbox_normalize(mode_t file_umask, uint64_t memory_bytes)
{
    sigset_t empty;
    struct sigaction defaults;
    memset(&defaults, 0, sizeof(defaults));
    defaults.sa_handler = SIG_DFL;
    struct rlimit core = {0, 0}, memory = {(rlim_t)memory_bytes, (rlim_t)memory_bytes};
    static int const handled[] = {SIGTERM, SIGINT, SIGPIPE, SIGCHLD, SIGALRM};
    bool ok = sigemptyset(&empty) == 0 && sigemptyset(&defaults.sa_mask) == 0;
    for (uint32_t index = 0; ok && index < sizeof(handled) / sizeof(handled[0]); index += 1)
        ok = sigaction(handled[index], &defaults, NULL) == 0;
    ok = ok && sigprocmask(SIG_SETMASK, &empty, NULL) == 0;
    if (ok) umask(file_umask);
#if defined(SYS_close_range)
    ok = ok && syscall(SYS_close_range, 3u, ~0u, CLOSE_RANGE_CLOEXEC) == 0;
#else
    ok = false;
#endif
    ok = ok && setrlimit(RLIMIT_CORE, &core) == 0 && (!memory_bytes || setrlimit(RLIMIT_AS, &memory) == 0);
    return ok;
}

/* In the forked child, after bq_retirement_sandbox_normalize: held is the
 * side's binary, A's base and candidate roots and the work directory, in slot
 * order. Each (and the ruleset) is first parked close-on-exec at or above
 * BQ_RETIREMENT_ROW_SLOT_HIGH, then the four are placed in their canonical
 * slots, which dup2 leaves inheritable; nothing else survives exec. Returns
 * the parked ruleset for bq_retirement_sandbox_enter, or -1. */
static inline int bq_retirement_sandbox_slots(int const held[4], uint32_t side, int ruleset)
{
    int const slots[4] = {BQ_RETIREMENT_ROW_SLOT_BINARY + (int)side, BQ_RETIREMENT_ROW_SLOT_SOURCE,
                          BQ_RETIREMENT_ROW_SLOT_SOURCE + 1, BQ_RETIREMENT_ROW_SLOT_WORK};
    int moved[5] = {-1, -1, -1, -1, -1};
    bool ok = side < 2;
    for (uint32_t index = 0; ok && index < 5; index += 1)
    {
        moved[index] = fcntl(index < 4 ? held[index] : ruleset, F_DUPFD_CLOEXEC, BQ_RETIREMENT_ROW_SLOT_HIGH);
        ok = moved[index] >= BQ_RETIREMENT_ROW_SLOT_HIGH;
    }
    for (uint32_t index = 0; ok && index < 4; index += 1) ok = dup2(moved[index], slots[index]) == slots[index];
    return ok ? moved[4] : -1;
}

/* In the forked child, just before exec: no new privileges, the ruleset,
 * then the seccomp filter. Landlock does not govern connect() to a
 * path-named unix socket, so the filter refuses (EPERM) every system call
 * that makes, names or uses a socket, and io_uring, which could issue those
 * operations without the system calls; another architecture or an x32 call
 * kills the child. No check, row step or measured command needs a socket:
 * they compile and run programs over files. */
static inline bool bq_retirement_sandbox_enter(int ruleset)
{
    bool ok = ruleset >= 0 && prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
              syscall(SYS_landlock_restrict_self, ruleset, 0) == 0;
#if BQ_RETIREMENT_SANDBOX_FILTERED
    static uint32_t const denied[] = {SYS_socket, SYS_socketpair, SYS_connect, SYS_bind, SYS_listen, SYS_accept,
                                      SYS_accept4, SYS_sendto, SYS_sendmsg, SYS_sendmmsg,
                                      BQ_RETIREMENT_SANDBOX_IO_URING_SETUP, BQ_RETIREMENT_SANDBOX_IO_URING_ENTER,
                                      BQ_RETIREMENT_SANDBOX_IO_URING_REGISTER};
    struct sock_filter filter[8u + 2u * (sizeof(denied) / sizeof(denied[0]))];
    uint32_t count = 0;
    filter[count++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
    filter[count++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, BQ_RETIREMENT_SANDBOX_AUDIT_ARCH, 1, 0);
    filter[count++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    filter[count++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
#if defined(BQ_RETIREMENT_SANDBOX_X32_BIT)
    filter[count++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, BQ_RETIREMENT_SANDBOX_X32_BIT, 0, 1);
    filter[count++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#endif
    for (uint32_t index = 0; index < sizeof(denied) / sizeof(denied[0]); index += 1)
    {
        filter[count++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, denied[index], 0, 1);
        filter[count++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
    }
    filter[count++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    struct sock_fprog program = {.len = (unsigned short)count, .filter = filter};
    ok = ok && prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program, 0, 0) == 0;
#else
    ok = false;
#endif
    return ok;
}
#endif
#endif
