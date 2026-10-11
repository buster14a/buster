/*
 * Machine-specification contract v1 (#2758), a bounded standalone CI helper.
 * initialize/emit own the safe field allowlist; collect uses native OS APIs.
 * CPU counts describe exposed topology, never an inferred physical host.
 */
#define _GNU_SOURCE
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <sched.h>
#include <sys/vfs.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <mach/mach.h>
#endif
#endif
#define MS_TEXT 2048
#define MS_READ 65536
typedef struct MsField
{
    const char *key;
    char value[MS_TEXT];
    const char *status;
    const char *reason;
} MsField;
typedef struct MsReport
{
    MsField fields[64];
    size_t count;
} MsReport;
static const char *keys[] =
{
    "schema", "collected_at_utc", "workflow", "workflow_ref", "workflow_sha", "run_id",
    "run_attempt", "job", "matrix_index", "event_sha", "tested_source_sha", "requested_runner",
    "runner_os", "runner_arch", "runner_class", "runner_image", "runner_image_version",
    "runner_software_version", "cpu_model", "cpu_vendor", "machine_arch", "process_arch",
    "cpu_physical_cores_os_visible", "cpu_sockets_os_visible", "cpu_logical_online",
    "cpu_process_available", "cpu_affinity", "cpu_cpuset_limit", "cpu_quota",
    "memory_total_bytes", "memory_available_bytes", "memory_limit_bytes", "swap_total_bytes",
    "swap_available_bytes", "commit_limit_bytes", "os_name", "os_version", "os_build",
    "kernel_name", "kernel_release", "kernel_version", "execution_context", "guest_context",
    "workspace_total_bytes", "workspace_available_bytes", "workspace_filesystem", "collection_elapsed_ms", "source_repository"
};
static MsField *field(MsReport *r, const char *key)
{
    MsField *result = NULL;
    for (size_t i = 0; i < r->count; ++i)
    {
        if (strcmp(r->fields[i].key, key) == 0)
        {
            result = &r->fields[i];
        }
    }
    return result;
}
static void set(MsReport *r, const char *key, const char *value)
{
    MsField *f = field(r, key);
    if (f && value && value[0])
    {
        snprintf(f->value, sizeof(f->value), "%s", value);
        f->status = strlen(value) < sizeof(f->value) ? "available" : "partial";
        f->reason = strlen(value) < sizeof(f->value) ? "" : "bounded field truncated";
    }
}
static void missing(MsReport *r, const char *key, const char *status, const char *reason)
{
    MsField *f = field(r, key);
    snprintf(f->value, sizeof(f->value), "%s", status);
    f->status = status;
    f->reason = reason;
}
static void number(MsReport *r, const char *key, uint64_t value)
{
    char text[32];
    snprintf(text, sizeof(text), "%" PRIu64, value);
    set(r, key, text);
}
static int parse_number(const char *text, uint64_t *value)
{
    int valid = text && text[0] >= '0' && text[0] <= '9';
    uint64_t result = 0;
    size_t i = 0;
    while (valid && text[i] >= '0' && text[i] <= '9')
    {
        uint64_t digit = (uint64_t)(text[i++] - '0');
        valid = result <= (UINT64_MAX - digit) / 10;
        if (valid)
        {
            result = result * 10 + digit;
        }
    }
    while (valid && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r'))
    {
        ++i;
    }
    valid = valid && text[i] == 0;
    if (valid)
    {
        *value = result;
    }
    return valid;
}
static int cpu_list(const char *text, uint64_t *count)
{
    int valid = text && text[0];
    uint64_t result = 0, previous = 0;
    size_t i = 0;
    int first = 1;
    while (valid && text[i] && text[i] != '\n')
    {
        uint64_t start = 0, end = 0;
        valid = text[i] >= '0' && text[i] <= '9';
        while (valid && text[i] >= '0' && text[i] <= '9')
        {
            start = start * 10 + (uint64_t)(text[i++] - '0');
            valid = start <= 1048576;
        }
        end = start;
        if (valid && text[i] == '-')
        {
            ++i;
            end = 0;
            valid = text[i] >= '0' && text[i] <= '9';
            while (valid && text[i] >= '0' && text[i] <= '9')
            {
                end = end * 10 + (uint64_t)(text[i++] - '0');
                valid = end <= 1048576;
            }
        }
        valid = valid && end >= start && (first || start > previous);
        if (valid)
        {
            result += end - start + 1;
        }
        previous = end;
        first = 0;
        if (valid && text[i] == ',')
        {
            ++i;
            valid = text[i] && text[i] != '\n';
        }
        else
        {
            valid = valid && (!text[i] || text[i] == '\n');
        }
    }
    valid = valid && (!text[i] || (text[i] == '\n' && !text[i + 1]));
    if (valid)
    {
        *count = result;
    }
    return valid;
}
static int read_text(const char *path, char *text, size_t capacity)
{
    int result = 0;
    text[0] = 0;
    FILE *file = fopen(path, "rb");
    if (file)
    {
        size_t n = fread(text, 1, capacity - 1, file);
        result = !ferror(file) && fgetc(file) == EOF;
        text[n] = 0;
        fclose(file);
    }
    return result;
}
static void limit(MsReport *r, uint64_t value)
{
    uint64_t old = 0;
    if (!parse_number(field(r, "memory_limit_bytes")->value, &old) || value < old)
    {
        number(r, "memory_limit_bytes", value);
    }
}
static void escape(FILE *out, const char *text, int json)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
    {
        unsigned char c = *p;
        if (json && (c == '"' || c == '\\'))
        {
            fputc('\\', out);
            fputc(c, out);
        }
        else if (json && (c < 32 || c >= 127))
        {
            fprintf(out, "\\u%04x", (unsigned)c);
        }
        else if (!json && (c < 32 || c >= 127 || strchr("|<>&[]\\:*_!", c) || c == 96))
        {
            fprintf(out, "&#%u;", (unsigned)c);
        }
        else
        {
            fputc(c, out);
        }
    }
}
static void emit(FILE *out, MsReport *r, int json)
{
    fputs(json ? "{" : "## Machine specifications\n\n| Field | Value | Status / reason |\n| --- | --- | --- |\n", out);
    for (size_t i = 0; i < r->count; ++i)
    {
        MsField *f = &r->fields[i];
        if (json)
        {
            fprintf(out, "%s\"%s\":{\"value\":\"", i ? "," : "", f->key);
            escape(out, f->value, 1);
            fprintf(out, "\",\"status\":\"%s\",\"reason\":\"", f->status);
            escape(out, f->reason, 1);
            fputs("\"}", out);
        }
        else
        {
            fprintf(out, "| %s | ", f->key);
            escape(out, f->value, 0);
            fprintf(out, " | %s", f->status);
            if (f->reason[0])
            {
                fputs(": ", out);
                escape(out, f->reason, 0);
            }
            fputs(" |\n", out);
        }
    }
    fputs(json ? "}\n" : "\n", out);
}
static const char *process_arch(void)
{
    const char *result = "unknown";
#if defined(_M_ARM64) || defined(__aarch64__)
    result = "aarch64";
#elif defined(_M_X64) || defined(__x86_64__)
    result = "x86_64";
#elif defined(_M_IX86) || defined(__i386__)
    result = "x86";
#endif
    return result;
}
static void initialize(MsReport *r)
{
    memset(r, 0, sizeof(*r));
    r->count = sizeof(keys) / sizeof(keys[0]);
    for (size_t i = 0; i < r->count; ++i)
    {
        r->fields[i].key = keys[i];
    }
    for (size_t i = 0; i < r->count; ++i)
    {
        missing(r, keys[i], "unknown", "not exposed by this OS, unsupported or probe failed");
    }
    const char *env[][2] =
    {
        {"workflow", "GITHUB_WORKFLOW"}, {"workflow_ref", "GITHUB_WORKFLOW_REF"},
        {"workflow_sha", "GITHUB_WORKFLOW_SHA"}, {"run_id", "GITHUB_RUN_ID"},
        {"run_attempt", "GITHUB_RUN_ATTEMPT"}, {"job", "GITHUB_JOB"}, {"event_sha", "GITHUB_SHA"},
        {"requested_runner", "MS_REQUESTED_RUNNER"}, {"matrix_index", "MS_MATRIX_INDEX"},
        {"runner_os", "RUNNER_OS"}, {"runner_arch", "RUNNER_ARCH"}, {"runner_class", "MS_RUNNER_CLASS"},
        {"runner_image", "ImageOS"}, {"runner_image_version", "ImageVersion"},
        {"runner_software_version", "ACTIONS_RUNNER_VERSION"}, {"guest_context", "MS_GUEST_CONTEXT"}
    };
    for (size_t i = 0; i < sizeof(env) / sizeof(env[0]); ++i)
    {
        set(r, env[i][0], getenv(env[i][1]));
    }
    set(r, "schema", "buster-machine-specifications-v1");
    set(r, "process_arch", process_arch());
    missing(r, "tested_source_sha", "unknown", "startup precedes checkout; actual source records follow checkout");
    if (!getenv("MS_MATRIX_INDEX") || !getenv("MS_MATRIX_INDEX")[0])
    {
        missing(r, "matrix_index", "not_applicable", "job has no matrix");
    }
    if (!getenv("MS_GUEST_CONTEXT") || !getenv("MS_GUEST_CONTEXT")[0])
    {
        missing(r, "guest_context", "unknown", "this report describes executing host; guest identity requires workload evidence");
    }
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    char timestamp[32];
    if (utc && strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", utc))
    {
        set(r, "collected_at_utc", timestamp);
    }
}
#if !defined(_WIN32)
static void collect_posix(MsReport *r)
{
    struct utsname info;
    if (uname(&info) == 0)
    {
        set(r, "machine_arch", info.machine);
        set(r, "kernel_name", info.sysname);
        set(r, "kernel_release", info.release);
        set(r, "kernel_version", info.version);
    }
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online > 0)
    {
        number(r, "cpu_logical_online", (uint64_t)online);
    }
    struct statvfs volume;
    const char *workspace = getenv("GITHUB_WORKSPACE");
    if (statvfs(workspace && workspace[0] ? workspace : ".", &volume) == 0 && volume.f_frsize &&
        volume.f_blocks <= UINT64_MAX / volume.f_frsize && volume.f_bavail <= UINT64_MAX / volume.f_frsize)
    {
        number(r, "workspace_total_bytes", (uint64_t)volume.f_blocks * volume.f_frsize);
        number(r, "workspace_available_bytes", (uint64_t)volume.f_bavail * volume.f_frsize);
    }
}
#endif
#if defined(__linux__)
static void named(MsReport *r, const char *text, const char *name, const char *key, int kib)
{
    const char *line = text;
    size_t length = strlen(name);
    int found = 0;
    while (*line && !found)
    {
        const char *next = strchr(line, '\n');
        size_t size = next ? (size_t)(next - line) : strlen(line);
        const char *delimiter = line + (size >= length ? length : size);
        while ((size_t)(delimiter - line) < size && (*delimiter == ' ' || *delimiter == '\t'))
        {
            ++delimiter;
        }
        if (size > length && strncmp(line, name, length) == 0 && *delimiter == ':')
        {
            const char *start = delimiter + 1;
            while (*start == ' ' || *start == '\t')
            {
                ++start;
            }
            char value[MS_TEXT];
            size_t n = size - (size_t)(start - line);
            if (n < sizeof(value))
            {
                memcpy(value, start, n);
                value[n] = 0;
                if (kib)
                {
                    char digits[64], unit[16], extra[2];
                    uint64_t bytes = 0;
                    if (sscanf(value, "%63s %15s %1s", digits, unit, extra) == 2 && strcmp(unit, "kB") == 0 &&
                        parse_number(digits, &bytes) && bytes <= UINT64_MAX / 1024)
                    {
                        number(r, key, bytes * 1024);
                    }
                }
                else
                {
                    set(r, key, value);
                }
            }
            found = 1;
        }
        line = next ? next + 1 : line + size;
    }
}
static void topology(MsReport *r)
{
    long configured = sysconf(_SC_NPROCESSORS_CONF);
    int packages[8192], cores[8192], sockets[8192];
    size_t core_count = 0, socket_count = 0;
    int complete = configured > 0 && configured <= 8192;
    for (long cpu = 0; complete && cpu < configured; ++cpu)
    {
        char path[256], text[64];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%ld/online", cpu);
        if (!read_text(path, text, sizeof(text)) || text[0] == '1')
        {
            uint64_t package = 0, core = 0;
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%ld/topology/physical_package_id", cpu);
            complete = read_text(path, text, sizeof(text)) && parse_number(text, &package) && package <= INT_MAX;
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%ld/topology/core_id", cpu);
            complete = complete && read_text(path, text, sizeof(text)) && parse_number(text, &core) && core <= INT_MAX;
            if (complete)
            {
                int seen_core = 0, seen_socket = 0;
                for (size_t i = 0; i < core_count; ++i)
                {
                    seen_core |= packages[i] == (int)package && cores[i] == (int)core;
                }
                for (size_t i = 0; i < socket_count; ++i)
                {
                    seen_socket |= sockets[i] == (int)package;
                }
                if (!seen_core)
                {
                    packages[core_count] = (int)package;
                    cores[core_count++] = (int)core;
                }
                if (!seen_socket)
                {
                    sockets[socket_count++] = (int)package;
                }
            }
        }
    }
    if (complete && core_count && socket_count)
    {
        number(r, "cpu_physical_cores_os_visible", core_count);
        number(r, "cpu_sockets_os_visible", socket_count);
    }
}
static void cgroup_at(MsReport *r, const char *directory, int v2, uint64_t *best_quota, uint64_t *best_period)
{
    char path[8192], text[MS_TEXT];
    uint64_t value = 0;
    snprintf(path, sizeof(path), "%s/%s", directory, v2 ? "memory.max" : "memory.limit_in_bytes");
    if (read_text(path, text, sizeof(text)) && parse_number(text, &value) && value < UINT64_C(0x7ffffffffffff000))
    {
        limit(r, value);
    }
    snprintf(path, sizeof(path), "%s/%s", directory, v2 ? "cpuset.cpus.effective" : "cpuset.cpus");
    if (read_text(path, text, sizeof(text)) && cpu_list(text, &value))
    {
        uint64_t old = 0;
        if (!cpu_list(field(r, "cpu_cpuset_limit")->value, &old) || value < old)
        {
            set(r, "cpu_cpuset_limit", text);
        }
    }
    uint64_t quota = 0, period = 0;
    int have_quota = 0;
    if (v2)
    {
        char left[64], right[64], extra[2];
        snprintf(path, sizeof(path), "%s/cpu.max", directory);
        have_quota = read_text(path, text, sizeof(text)) &&
            sscanf(text, "%63s %63s %1s", left, right, extra) == 2 &&
            parse_number(left, &quota) && parse_number(right, &period);
    }
    else
    {
        snprintf(path, sizeof(path), "%s/cpu.cfs_quota_us", directory);
        have_quota = read_text(path, text, sizeof(text)) && parse_number(text, &quota);
        snprintf(path, sizeof(path), "%s/cpu.cfs_period_us", directory);
        have_quota = have_quota && read_text(path, text, sizeof(text)) && parse_number(text, &period);
    }
    if (have_quota && quota && period &&
        (!*best_period || (long double)quota / period < (long double)*best_quota / *best_period))
    {
        *best_quota = quota;
        *best_period = period;
        char description[160];
        snprintf(description, sizeof(description), "%" PRIu64 " / %" PRIu64 " CPU-time microseconds (quota/period)", quota, period);
        set(r, "cpu_quota", description);
    }
}
static void cgroups(MsReport *r)
{
    char membership[MS_READ], mounts[MS_READ];
    uint64_t best_quota = 0, best_period = 0;
    /* Inspect only cgroup mounts; neither membership nor host paths are emitted. */
    if (read_text("/proc/self/cgroup", membership, sizeof(membership)) &&
        read_text("/proc/self/mountinfo", mounts, sizeof(mounts)))
    {
        char *line = membership;
        while (line && *line)
        {
            char *next = strchr(line, '\n');
            if (next)
            {
                *next++ = 0;
            }
            char *first = strchr(line, ':');
            char *second = first ? strchr(first + 1, ':') : NULL;
            if (second)
            {
                *second = 0;
                const char *controllers = first + 1, *member = second + 1, *mount_line = mounts;
                while (*mount_line)
                {
                    const char *mount_end = strchr(mount_line, '\n');
                    size_t length = mount_end ? (size_t)(mount_end - mount_line) : strlen(mount_line);
                    char current[8192], root[4096], mount[4096], fs[32], source[128], options[512];
                    if (length < sizeof(current))
                    {
                        memcpy(current, mount_line, length);
                        current[length] = 0;
                        char *separator = strstr(current, " - ");
                        if (separator && sscanf(current, "%*s %*s %*s %4095s %4095s", root, mount) == 2 &&
                            sscanf(separator + 3, "%31s %127s %511s", fs, source, options) == 3)
                        {
                            int v2 = strcmp(fs, "cgroup2") == 0 && !controllers[0];
                            int v1 = strcmp(fs, "cgroup") == 0 && controllers[0] &&
                                ((strstr(controllers, "memory") && strstr(options, "memory")) ||
                                 (strstr(controllers, "cpu") && strstr(options, "cpu")));
                            size_t root_length = strlen(root), mount_length = strlen(mount);
                            int root_match = strcmp(root, "/") == 0 || (strncmp(member, root, root_length) == 0 &&
                                (member[root_length] == '/' || !member[root_length]));
                            if ((v1 || v2) && root_match && member[0] == '/' && !strstr(member, "..") &&
                                !strchr(member, '\\') && !strchr(mount, '\\'))
                            {
                                char directory[8192];
                                const char *relative = strcmp(root, "/") == 0 ? member : member + root_length;
                                int n = snprintf(directory, sizeof(directory), "%s%s", mount, relative);
                                if (n >= 0 && (size_t)n < sizeof(directory))
                                {
                                    for (size_t depth = 0; depth < 256 && strlen(directory) >= mount_length; ++depth)
                                    {
                                        cgroup_at(r, directory, v2, &best_quota, &best_period);
                                        char *slash = strrchr(directory, '/');
                                        if (!slash || (size_t)(slash - directory) < mount_length)
                                        {
                                            break;
                                        }
                                        *slash = 0;
                                    }
                                }
                            }
                        }
                    }
                    mount_line = mount_end ? mount_end + 1 : mount_line + length;
                }
            }
            line = next;
        }
    }
}
static void collect(MsReport *r)
{
    collect_posix(r);
    char text[MS_READ];
    FILE *cpuinfo = fopen("/proc/cpuinfo", "rb");
    int have_cpuinfo = 0;
    if (cpuinfo)
    {
        size_t n = fread(text, 1, sizeof(text) - 1, cpuinfo);
        text[n] = 0;
        have_cpuinfo = !ferror(cpuinfo);
        fclose(cpuinfo);
    }
    if (have_cpuinfo)
    {
        named(r, text, "model name", "cpu_model", 0);
        named(r, text, "vendor_id", "cpu_vendor", 0);
        if (strcmp(field(r, "cpu_model")->status, "available") != 0)
        {
            named(r, text, "CPU implementer", "cpu_vendor", 0);
            named(r, text, "CPU part", "cpu_model", 0);
        }
        if (strstr(text, "hypervisor"))
        {
            set(r, "execution_context", "VM indicated by CPU hypervisor feature; underlying physical host unknown");
        }
    }
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0)
    {
        number(r, "cpu_process_available", (uint64_t)CPU_COUNT(&affinity));
    }
    if (read_text("/proc/self/status", text, sizeof(text)))
    {
        named(r, text, "Cpus_allowed_list", "cpu_affinity", 0);
    }
    topology(r);
    if (read_text("/proc/meminfo", text, sizeof(text)))
    {
        named(r, text, "MemTotal", "memory_total_bytes", 1);
        named(r, text, "MemAvailable", "memory_available_bytes", 1);
        named(r, text, "SwapTotal", "swap_total_bytes", 1);
        named(r, text, "SwapFree", "swap_available_bytes", 1);
        named(r, text, "CommitLimit", "commit_limit_bytes", 1);
    }
    cgroups(r);
    if (read_text("/etc/os-release", text, sizeof(text)))
    {
        char *line = text;
        while (line && *line)
        {
            char *next = strchr(line, '\n');
            if (next)
            {
                *next++ = 0;
            }
            char *equals = strchr(line, '=');
            if (equals)
            {
                *equals++ = 0;
                if (*equals == '"')
                {
                    ++equals;
                    size_t n = strlen(equals);
                    if (n && equals[n - 1] == '"')
                    {
                        equals[n - 1] = 0;
                    }
                }
                if (strcmp(line, "PRETTY_NAME") == 0)
                {
                    set(r, "os_name", equals);
                }
                if (strcmp(line, "VERSION_ID") == 0)
                {
                    set(r, "os_version", equals);
                }
                if (strcmp(line, "BUILD_ID") == 0)
                {
                    set(r, "os_build", equals);
                }
            }
            line = next;
        }
    }
    struct statfs volume;
    const char *workspace = getenv("GITHUB_WORKSPACE");
    if (statfs(workspace && workspace[0] ? workspace : ".", &volume) == 0)
    {
        char type[96];
        snprintf(type, sizeof(type), "Linux filesystem magic 0x%lx", (unsigned long)volume.f_type);
        set(r, "workspace_filesystem", type);
    }
    if (access("/.dockerenv", F_OK) == 0 || access("/run/.containerenv", F_OK) == 0)
    {
        set(r, "execution_context", "container marker detected; underlying VM/physical host unknown");
    }
}
#elif defined(__APPLE__)
static void apple_text(MsReport *r, const char *name, const char *key)
{
    char text[MS_TEXT];
    size_t size = sizeof(text);
    if (sysctlbyname(name, text, &size, NULL, 0) == 0 && size > 0 && size <= sizeof(text))
    {
        text[sizeof(text) - 1] = 0;
        set(r, key, text);
    }
}
static void apple_number(MsReport *r, const char *name, const char *key)
{
    uint64_t value = 0;
    size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, NULL, 0) == 0 && (size == 4 || size == 8))
    {
        number(r, key, value);
    }
}
static void collect(MsReport *r)
{
    collect_posix(r);
    apple_text(r, "machdep.cpu.brand_string", "cpu_model");
    apple_text(r, "machdep.cpu.vendor", "cpu_vendor");
    apple_number(r, "hw.physicalcpu", "cpu_physical_cores_os_visible");
    apple_number(r, "hw.packages", "cpu_sockets_os_visible");
    apple_number(r, "hw.memsize", "memory_total_bytes");
    apple_text(r, "kern.osproductversion", "os_version");
    apple_text(r, "kern.osversion", "os_build");
    set(r, "os_name", "macOS");
    missing(r, "cpu_affinity", "unavailable", "Darwin affinity tags are not CPU binding");
    missing(r, "cpu_process_available", "unknown", "Darwin exposes no Linux-style per-process CPU availability");
    missing(r, "cpu_cpuset_limit", "not_applicable", "Darwin does not expose Linux cgroups");
    vm_statistics64_data_t memory;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_size_t page = 0;
    mach_port_t host = mach_host_self();
    if (host_page_size(host, &page) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&memory, &count) == KERN_SUCCESS)
    {
        number(r, "memory_available_bytes", ((uint64_t)memory.free_count + memory.inactive_count) * page);
        field(r, "memory_available_bytes")->reason = "snapshot estimate: free plus inactive pages; excludes reclaimable compression";
    }
    mach_port_deallocate(mach_task_self(), host);
    struct xsw_usage swap;
    size_t size = sizeof(swap);
    if (sysctlbyname("vm.swapusage", &swap, &size, NULL, 0) == 0)
    {
        number(r, "swap_total_bytes", swap.xsu_total);
        number(r, "swap_available_bytes", swap.xsu_avail);
    }
    int translated = 0;
    size = sizeof(translated);
    if (sysctlbyname("sysctl.proc_translated", &translated, &size, NULL, 0) == 0 && translated)
    {
        set(r, "machine_arch", "aarch64");
        set(r, "execution_context", "Rosetta process translation detected; underlying VM/physical host unknown");
    }
    struct statfs volume;
    const char *workspace = getenv("GITHUB_WORKSPACE");
    if (statfs(workspace && workspace[0] ? workspace : ".", &volume) == 0)
    {
        set(r, "workspace_filesystem", volume.f_fstypename);
    }
}
#elif defined(_WIN32)
static void registry(MsReport *r, const char *path, const char *name, const char *key)
{
    HKEY handle = NULL;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &handle) == ERROR_SUCCESS)
    {
        char text[MS_TEXT];
        DWORD size = sizeof(text), type = 0;
        if (RegQueryValueExA(handle, name, NULL, &type, (BYTE *)text, &size) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ) && size > 0 && size <= sizeof(text))
        {
            text[sizeof(text) - 1] = 0;
            set(r, key, text);
        }
        RegCloseKey(handle);
    }
}
static void collect(MsReport *r)
{
    const char *cpu = "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    const char *os = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    registry(r, cpu, "ProcessorNameString", "cpu_model");
    registry(r, cpu, "VendorIdentifier", "cpu_vendor");
    registry(r, os, "ProductName", "os_name");
    registry(r, os, "DisplayVersion", "os_version");
    registry(r, os, "CurrentBuildNumber", "os_build");
    registry(r, os, "CurrentBuildNumber", "kernel_release");
    set(r, "kernel_name", "Windows NT");
    HKEY version_key = NULL;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, os, 0, KEY_READ | KEY_WOW64_64KEY, &version_key) == ERROR_SUCCESS)
    {
        DWORD revision = 0, revision_size = sizeof(revision), revision_type = 0;
        if (RegQueryValueExA(version_key, "UBR", NULL, &revision_type, (BYTE *)&revision, &revision_size) == ERROR_SUCCESS &&
            revision_type == REG_DWORD && strcmp(field(r, "os_build")->status, "available") == 0)
        {
            char version[MS_TEXT + 32];
            snprintf(version, sizeof(version), "%s.%lu", field(r, "os_build")->value, (unsigned long)revision);
            set(r, "os_build", version);
            set(r, "kernel_version", version);
        }
        RegCloseKey(version_key);
    }
    SYSTEM_INFO system;
    GetNativeSystemInfo(&system);
    set(r, "machine_arch", system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "aarch64" :
        system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x86_64" : "unknown");
    number(r, "cpu_logical_online", GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    DWORD_PTR process_mask = 0, system_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) && process_mask)
    {
        uint64_t available = 0;
        for (size_t bit = 0; bit < sizeof(process_mask) * CHAR_BIT; ++bit)
        {
            available += (process_mask >> bit) & 1;
        }
        number(r, "cpu_process_available", available);
        char mask[96];
        snprintf(mask, sizeof(mask), "0x%" PRIx64 " (primary processor group)", (uint64_t)process_mask);
        set(r, "cpu_affinity", mask);
        if (GetActiveProcessorGroupCount() > 1)
        {
            missing(r, "cpu_process_available", "unknown", "primary-group mask cannot describe multiple processor groups");
        }
    }
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationAll, NULL, &size);
    if (size > 0 && size <= 262144)
    {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *entries = malloc(size);
        if (entries && GetLogicalProcessorInformationEx(RelationAll, entries, &size))
        {
            DWORD offset = 0;
            uint64_t cores = 0, sockets = 0;
            int valid = 1;
            while (valid && offset < size)
            {
                SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *entry = (void *)((BYTE *)entries + offset);
                if (size - offset < sizeof(DWORD) * 2 || entry->Size < sizeof(DWORD) * 2 || entry->Size > size - offset)
                {
                    valid = 0;
                }
                else
                {
                    cores += entry->Relationship == RelationProcessorCore;
                    sockets += entry->Relationship == RelationProcessorPackage;
                    offset += entry->Size;
                }
            }
            if (valid && cores && sockets)
            {
                number(r, "cpu_physical_cores_os_visible", cores);
                number(r, "cpu_sockets_os_visible", sockets);
            }
        }
        free(entries);
    }
    MEMORYSTATUSEX memory;
    memset(&memory, 0, sizeof(memory));
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
    {
        number(r, "memory_total_bytes", memory.ullTotalPhys);
        number(r, "memory_available_bytes", memory.ullAvailPhys);
        number(r, "commit_limit_bytes", memory.ullTotalPageFile);
        field(r, "commit_limit_bytes")->reason = "Windows commit capacity; not pagefile size";
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    memset(&limits, 0, sizeof(limits));
    if (QueryInformationJobObject(NULL, JobObjectExtendedLimitInformation, &limits, sizeof(limits), NULL))
    {
        if (limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_JOB_MEMORY)
        {
            limit(r, limits.JobMemoryLimit);
        }
        if (limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY)
        {
            limit(r, limits.ProcessMemoryLimit);
        }
    }
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate;
    memset(&rate, 0, sizeof(rate));
    if (QueryInformationJobObject(NULL, JobObjectCpuRateControlInformation, &rate, sizeof(rate), NULL) &&
        (rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE) && (rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP))
    {
        char value[128];
        snprintf(value, sizeof(value), "%lu / 10000 of job CPU-time capacity (Windows hard cap)", (unsigned long)rate.CpuRate);
        set(r, "cpu_quota", value);
    }
    const char *workspace = getenv("GITHUB_WORKSPACE");
    ULARGE_INTEGER total, available, free_bytes;
    if (GetDiskFreeSpaceExA(workspace && workspace[0] ? workspace : ".", &available, &total, &free_bytes))
    {
        number(r, "workspace_total_bytes", total.QuadPart);
        number(r, "workspace_available_bytes", available.QuadPart);
    }
    char root[MAX_PATH], fs[64];
    if (workspace && GetVolumePathNameA(workspace, root, sizeof(root)) &&
        GetVolumeInformationA(root, NULL, 0, NULL, NULL, NULL, fs, sizeof(fs)))
    {
        set(r, "workspace_filesystem", fs);
    }
}
#else
static void collect(MsReport *r)
{
    (void)r;
}
#endif
static uint64_t milliseconds(void)
{
    uint64_t result = 0;
#if defined(_WIN32)
    result = GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
    {
        result = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    }
#endif
    return result;
}

#if defined(__linux__)
static int fixture_text(const char *directory, const char *name, const char *text)
{
    int result = 0;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *file = fopen(path, "wb");
    if (file)
    {
        size_t length = strlen(text);
        result = fwrite(text, 1, length, file) == length;
        result &= fclose(file) == 0;
    }
    return result;
}
static int linux_probe_tests(void)
{
    int failures = 0;
    MsReport r;
    initialize(&r);
    named(&r, "model name\t: Actual exposed brand\nvendor_id\t: ExposedVendor\n", "model name", "cpu_model", 0);
    named(&r, "vendor_id\t: ExposedVendor\n", "vendor_id", "cpu_vendor", 0);
    failures += strcmp(field(&r, "cpu_model")->value, "Actual exposed brand") != 0;
    failures += strcmp(field(&r, "cpu_vendor")->value, "ExposedVendor") != 0;
    named(&r, "MemTotal: 8192 kB\n", "MemTotal", "memory_total_bytes", 1);
    failures += strcmp(field(&r, "memory_total_bytes")->value, "8388608") != 0;
    named(&r, "MemAvailable: not-a-number kB\n", "MemAvailable", "memory_available_bytes", 1);
    failures += strcmp(field(&r, "memory_available_bytes")->status, "unknown") != 0;
    char directory[] = "/tmp/buster-machine-spec-test-XXXXXX";
    char *created = mkdtemp(directory);
    if (created)
    {
        uint64_t quota = 0, period = 0;
        failures += !fixture_text(directory, "memory.max", "8192\n");
        failures += !fixture_text(directory, "cpu.max", "100000 100000\n");
        failures += !fixture_text(directory, "cpuset.cpus.effective", "0-3\n");
        cgroup_at(&r, directory, 1, &quota, &period);
        failures += strcmp(field(&r, "memory_limit_bytes")->value, "8192") != 0;
        failures += quota != 100000 || period != 100000;
        failures += !fixture_text(directory, "memory.max", "4096\n");
        failures += !fixture_text(directory, "cpu.max", "50000 100000\n");
        failures += !fixture_text(directory, "cpuset.cpus.effective", "0-1\n");
        cgroup_at(&r, directory, 1, &quota, &period);
        failures += strcmp(field(&r, "memory_limit_bytes")->value, "4096") != 0;
        failures += quota != 50000 || period != 100000;
        failures += strcmp(field(&r, "cpu_cpuset_limit")->value, "0-1\n") != 0;
        failures += !fixture_text(directory, "memory.max", "max\n");
        failures += !fixture_text(directory, "cpu.max", "max 100000\n");
        failures += !fixture_text(directory, "cpuset.cpus.effective", "0-7\n");
        cgroup_at(&r, directory, 1, &quota, &period);
        failures += strcmp(field(&r, "memory_limit_bytes")->value, "4096") != 0;
        failures += quota != 50000 || period != 100000;
        failures += strcmp(field(&r, "cpu_cpuset_limit")->value, "0-1\n") != 0;
        failures += !fixture_text(directory, "memory.limit_in_bytes", "2048\n");
        failures += !fixture_text(directory, "cpu.cfs_quota_us", "25000\n");
        failures += !fixture_text(directory, "cpu.cfs_period_us", "100000\n");
        failures += !fixture_text(directory, "cpuset.cpus", "0\n");
        cgroup_at(&r, directory, 0, &quota, &period);
        failures += strcmp(field(&r, "memory_limit_bytes")->value, "2048") != 0;
        failures += quota != 25000 || period != 100000;
        failures += strcmp(field(&r, "cpu_cpuset_limit")->value, "0\n") != 0;
        const char *names[] =
        {
            "memory.max", "cpu.max", "cpuset.cpus.effective",
            "memory.limit_in_bytes", "cpu.cfs_quota_us", "cpu.cfs_period_us", "cpuset.cpus"
        };
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
            failures += unlink(path) != 0;
        }
        cgroup_at(&r, directory, 1, &quota, &period);
        failures += strcmp(field(&r, "memory_limit_bytes")->value, "2048") != 0;
        failures += rmdir(directory) != 0;
    }
    else
    {
        ++failures;
    }
    return failures;
}
#endif

static int self_test(void)
{
    int failures = 0;
    uint64_t value = 0;
    failures += !cpu_list("0-3,8,10-11\n", &value) || value != 7;
    failures += cpu_list("3-1", &value);
    failures += cpu_list("0-3,2-4", &value);
    failures += cpu_list("0,", &value);
    failures += cpu_list("", &value);
    failures += cpu_list("0\ninjected", &value);
    failures += parse_number("18446744073709551616", &value);
    failures += parse_number("-1", &value);
    MsReport r;
    initialize(&r);
    limit(&r, 8192);
    limit(&r, 4096);
    limit(&r, 16384);
    failures += strcmp(field(&r, "memory_limit_bytes")->value, "4096") != 0;
    number(&r, "cpu_physical_cores_os_visible", 2);
    number(&r, "cpu_logical_online", 4);
    number(&r, "cpu_process_available", 1);
    set(&r, "machine_arch", "aarch64");
    set(&r, "process_arch", "x86_64");
    failures += strcmp(field(&r, "cpu_physical_cores_os_visible")->value, "2") != 0;
    failures += strcmp(field(&r, "cpu_logical_online")->value, "4") != 0;
    failures += strcmp(field(&r, "cpu_process_available")->value, "1") != 0;
    failures += strcmp(field(&r, "machine_arch")->value, field(&r, "process_arch")->value) == 0;
    failures += strcmp(field(&r, "cpu_vendor")->status, "unknown") != 0;
    for (int json = 0; json <= 1; ++json)
    {
        FILE *file = tmpfile();
        if (file)
        {
            escape(file, "\"\\\n::error::|<secret>", json);
            rewind(file);
            char text[256];
            size_t n = fread(text, 1, sizeof(text) - 1, file);
            text[n] = 0;
            failures += json ? strcmp(text, "\\\"\\\\\\u000a::error::|<secret>") != 0 :
                strchr(text, '\n') != NULL || strchr(text, '|') != NULL || strchr(text, '<') != NULL;
            fclose(file);
        }
        else
        {
            ++failures;
        }
    }
#if defined(__linux__)
    failures += linux_probe_tests();
#endif
    printf("Machine specifications self-test: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
int main(int argc, char **argv)
{
    int result = 0;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        result = self_test();
    }
    else if (argc == 3 && strcmp(argv[1], "--collect") == 0)
    {
        uint64_t start = milliseconds();
        MsReport r;
        initialize(&r);
        collect(&r);
        number(&r, "collection_elapsed_ms", milliseconds() - start);
        emit(stdout, &r, 0);
        fputs("MACHINE_SPECIFICATIONS_JSON ", stdout);
        emit(stdout, &r, 1);
        FILE *record = fopen(argv[2], "wb");
        if (record)
        {
            emit(record, &r, 1);
            result |= fclose(record) != 0;
        }
        else
        {
            fputs("Machine specifications diagnostic: JSON retention failed\n", stderr);
            result = 1;
        }
        const char *path = getenv("GITHUB_STEP_SUMMARY");
        FILE *summary = path && path[0] ? fopen(path, "ab") : NULL;
        if (summary)
        {
            emit(summary, &r, 0);
            result |= fclose(summary) != 0;
        }
        else
        {
            fputs("Machine specifications diagnostic: summary unavailable\n", stderr);
            result = 1;
        }
    }
    else if (argc == 5 && strcmp(argv[1], "--source") == 0)
    {
        int valid = strlen(argv[2]) == 40;
        for (size_t i = 0; valid && i < 40; ++i)
        {
            char c = argv[2][i];
            valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }
        if (valid)
        {
            MsReport r;
            initialize(&r);
            set(&r, "schema", "buster-machine-source-identity-v1");
            set(&r, "tested_source_sha", argv[2]);
            set(&r, "source_repository", argv[3]);
            FILE *record = fopen(argv[4], "ab");
            if (record)
            {
                emit(record, &r, 1);
                result |= fclose(record) != 0;
            }
            else
            {
                result = 1;
            }
            fputs("MACHINE_SOURCE_IDENTITY_JSON ", stdout);
            emit(stdout, &r, 1);
            const char *path = getenv("GITHUB_STEP_SUMMARY");
            FILE *summary = path && path[0] ? fopen(path, "ab") : NULL;
            if (summary)
            {
                fputs("### Machine specifications: actual checkout identity\n\nRepository: ", summary);
                escape(summary, argv[3], 0);
                fprintf(summary, "\n\nCheckout SHA: %s\n\n", argv[2]);
                result |= fclose(summary) != 0;
            }
            else
            {
                result = 1;
            }
        }
        else
        {
            fputs("Machine specifications diagnostic: invalid checkout SHA\n", stderr);
            result = 1;
        }
    }
    else
    {
        fputs("usage: machine-specifications --self-test | --collect JSON | --source SHA REPOSITORY JSONL\n", stderr);
        result = 1;
    }
    return result;
}
