/* Dedicated Linux host admission, outside all measured intervals.
 * Ownership: main holds the cooperative lease; tp_metadata seals the captured
 * observations. No governor, firmware, cgroup or other system policy is changed.
 * Map: tp_host_lock_acquire/release, tp_host_capture, tp_host_json.
 * A lease and readable topology DO NOT prove idle SMT siblings or bare metal.
 */
#ifndef BUSTER_THROUGHPUT_QUALIFICATION_H
#define BUSTER_THROUGHPUT_QUALIFICATION_H

#define TP_HOST_VALUE_CAP 4096
#define TP_HOST_FACT_COUNT 20
#define TP_HOST_LABEL_CAP 128

typedef struct TpHostLock { int descriptor; } TpHostLock;
typedef struct TpHostFact
{
    char path[256];
    char value[TP_HOST_VALUE_CAP];
    int error, truncated;
} TpHostFact;

typedef struct TpHost
{
    char const* machine_id;
    char const* lock_file;
    int cpu;
    TpHostFact facts[TP_HOST_FACT_COUNT];
} TpHost;

static int tp_json_string(FILE* file, char const* text);

/* Linux flock locks the open file description. Deliberately inherit this
 * descriptor through the existing compiler fork/exec path: after a killed
 * supervisor, surviving children must not release the lease prematurely.
 * Close, never LOCK_UN or unlink: inherited holders retain the same lease.
 * Cooperating users must share this stable inode on a LOCAL filesystem.
 */
static int tp_host_lock_acquire(char const* path, TpHostLock* lock)
{
    int error = ENOSYS;
    lock->descriptor = -1;
#ifdef __linux__
    if (!path || path[0] != '/') error = EINVAL;
    else
    {
        int descriptor = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0600);
        error = descriptor < 0 ? errno : 0;
        if (!error && descriptor < 3)
        {
            int duplicate = fcntl(descriptor, F_DUPFD, 3);
            error = duplicate < 0 ? errno : 0;
            close(descriptor);
            descriptor = duplicate;
        }
        struct stat info;
        if (!error && fstat(descriptor, &info) != 0) error = errno;
        if (!error && !S_ISREG(info.st_mode)) error = EINVAL;
        if (!error && flock(descriptor, LOCK_EX | LOCK_NB) != 0) error = errno;
        if (!error) lock->descriptor = descriptor;
        else if (descriptor >= 0) close(descriptor);
    }
#else
    (void)path;
#endif
    return error;
}

static void tp_host_lock_release(TpHostLock* lock)
{
#ifdef __linux__
    if (lock->descriptor >= 0) close(lock->descriptor);
#endif
    lock->descriptor = -1;
}

/* Bounded byte observations: report truncation and read/close errors explicitly.
 * Do not turn an unreadable or NUL-containing file into an observed empty value.
 */

#ifdef __linux__
static void tp_host_read(TpHostFact* fact, char const* path)
{
    memset(fact, 0, sizeof(*fact));
    int length = snprintf(fact->path, sizeof(fact->path), "%s", path);
    fact->error = length < 0 || (size_t)length >= sizeof(fact->path) ? ENAMETOOLONG : 0;
    FILE* file = fact->error ? NULL : fopen(path, "rb");
    if (!file && !fact->error) fact->error = errno ? errno : EIO;
    if (file)
    {
        size_t count = fread(fact->value, 1, sizeof(fact->value) - 1, file);
        fact->value[count] = 0;
        fact->truncated = fgetc(file) != EOF;
        if (ferror(file)) fact->error = errno ? errno : EIO;
        if (memchr(fact->value, 0, count)) fact->error = EILSEQ;
        if (fclose(file) != 0 && !fact->error) fact->error = errno ? errno : EIO;
        if (fact->error) fact->value[0] = 0;
    }
}

#endif

static int tp_host_capture(TpHost* host, int cpu, char const* machine_id, char const* lock_file)
{
    memset(host, 0, sizeof(*host));
    host->cpu = cpu;
    host->machine_id = machine_id;
    host->lock_file = lock_file;
    int error = ENOSYS;
#ifdef __linux__
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (cpu < 0 || cpu >= CPU_SETSIZE) error = EINVAL;
    else if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) error = errno;
    else error = CPU_ISSET(cpu, &allowed) ? 0 : EINVAL;
    if (!error)
    {
        /* Root-cgroup files are deliberately NOT described as effective
         * process limits. Membership is retained for external qualification. */
        static char const* const paths[] = {
            "/proc/cpuinfo", "/proc/version", "/proc/self/status",
            "/proc/self/cgroup", "/proc/sys/kernel/perf_event_paranoid",
            "/sys/devices/system/cpu/online", "/sys/devices/system/cpu/smt/active",
            "/sys/devices/system/cpu/smt/control", "/sys/devices/system/cpu/cpufreq/boost",
            "/sys/devices/system/cpu/amd_pstate/status", "/proc/meminfo",
            "/proc/sys/kernel/random/boot_id"};
        static char const* const selected[] = {
            "topology/physical_package_id", "topology/core_id", "topology/thread_siblings_list",
            "cpufreq/scaling_driver", "cpufreq/scaling_governor",
            "cpufreq/energy_performance_preference", "cpufreq/scaling_min_freq",
            "cpufreq/scaling_max_freq"};
        _Static_assert(sizeof(paths) / sizeof(paths[0]) + sizeof(selected) / sizeof(selected[0]) == TP_HOST_FACT_COUNT,
                       "host fact capacity must match both path tables");
        unsigned at = 0;
        for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i)
        {
            tp_host_read(host->facts + at++, paths[i]);
        }
        for (unsigned i = 0; i < sizeof(selected) / sizeof(selected[0]); ++i)
        {
            char path[256];
            int length = snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/%s", cpu, selected[i]);
            if (length < 0 || (size_t)length >= sizeof(path)) error = ENAMETOOLONG;
            else tp_host_read(host->facts + at++, path);
        }
    }
#endif
    return error;
}

static int tp_host_json(FILE* file, TpHost const* host)
{
    fputs("{\"schema\":1,\"platform\":\"linux\",\"machine_id\":", file);
    tp_json_string(file, host->machine_id);
    fputs(",\"machine_id_scope\":\"operator-supplied machine/environment version, not hardware attestation\",\"lock_file\":", file);
    tp_json_string(file, host->lock_file);
    fprintf(file, ",\"selected_cpu\":%d,\"selected_cpu_allowed\":true,", host->cpu);
    fputs("\"lock_scope\":\"cooperating processes sharing one stable local file inode\",", file);
    fputs("\"physical_isolation\":\"unverified\",\"smt_sibling_idle\":\"unverified\",", file);
    fputs("\"effective_cgroup_limits\":\"unverified\",\"pmu\":\"not probed by qualification\",", file);
    fputs("\"snapshot_scope\":\"bounded pre-run procfs/sysfs observations; cpuinfo may contain only a prefix; not a stable environment fingerprint\",", file);
    fputs("\"facts\":[", file);
    for (unsigned i = 0; i < TP_HOST_FACT_COUNT; ++i)
    {
        TpHostFact const* fact = host->facts + i;
        if (i) fputc(',', file);
        fputs("{\"path\":", file); tp_json_string(file, fact->path);
        fprintf(file, ",\"errno\":%d,\"truncated\":%s,\"value\":", fact->error, fact->truncated ? "true" : "false");
        if (fact->error) fputs("null", file);
        else
        {
            /* Procfs is normally ASCII. Escaping high bytes also guarantees
             * valid JSON for arbitrary byte observations without guessing UTF-8. */
            fputc('"', file);
            for (unsigned char const* p = (unsigned char const*)fact->value; *p; ++p)
            {
                if (*p < 32 || *p >= 127) fprintf(file, "\\u%04x", *p);
                else
                {
                    if (*p == '"' || *p == '\\') fputc('\\', file);
                    fputc(*p, file);
                }
            }
            fputc('"', file);
        }
        fputc('}', file);
    }
    fputs("]}", file);
    return !ferror(file);
}
#endif
