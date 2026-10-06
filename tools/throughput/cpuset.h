/* Explicit CPU sets and logical-to-physical topology for placement experiments.
 * Ownership: parsing, permitted-mask checks and sysfs topology interpretation.
 * tp_process_cpus (platform.h) applies a set to a child; scaling.h selects one.
 * Map: tp_cpu_set_parse/tp_cpu_set_format (Linux cpulist syntax),
 * tp_cpu_set_permitted (whole-set check, never narrowed), tp_topology_read
 * (thread_siblings_list defines a physical core), tp_topology_physical_prefix.
 * Topology is an OS report: it does not prove an idle SMT sibling or bare metal.
 */
#ifndef BUSTER_THROUGHPUT_CPUSET_H
#define BUSTER_THROUGHPUT_CPUSET_H
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#endif

/* Matches glibc CPU_SETSIZE so a valid set always fits one cpu_set_t. */
#define TP_MAX_CPUS 1024
#define TP_CPU_SET_TEXT_CAP 4096
#define TP_TOPOLOGY_ROOT "/sys/devices/system/cpu"

typedef struct TpCpuSet
{
    uint64_t bits[TP_MAX_CPUS / 64];
    unsigned count;
} TpCpuSet;

typedef struct TpCpuTopology
{
    unsigned cpu;
    unsigned core;     /* Lowest logical CPU named by thread_siblings_list. */
    unsigned siblings; /* Logical CPUs sharing this physical core, in or out of the set. */
    int package, core_id;
} TpCpuTopology;

typedef struct TpTopology
{
    TpCpuTopology cpus[TP_MAX_CPUS];
    unsigned count, physical_cores;
} TpTopology;

static int tp_cpu_set_has(TpCpuSet const* set, unsigned cpu)
{
    return cpu < TP_MAX_CPUS && ((set->bits[cpu / 64] >> (cpu % 64)) & 1u);
}

static void tp_cpu_set_add(TpCpuSet* set, unsigned cpu)
{
    if (!tp_cpu_set_has(set, cpu))
    {
        set->bits[cpu / 64] |= UINT64_C(1) << (cpu % 64);
        set->count += 1;
    }
}

/* Linux cpulist syntax ("0,2-5"). A duplicate, empty or reversed element is a
 * request error, not something to merge silently. Trailing newline allowed
 * because sysfs lists end with one. */
static int tp_cpu_set_parse(char const* text, TpCpuSet* set)
{
    memset(set, 0, sizeof(*set));
    char const* p = text;
    int ok = p && *p && *p != '\n';
    while (ok && *p && *p != '\n')
    {
        unsigned first = 0, last = 0, digits = 0;
        while (*p >= '0' && *p <= '9' && first < TP_MAX_CPUS) { first = first * 10 + (unsigned)(*p++ - '0'); ++digits; }
        ok = digits > 0 && first < TP_MAX_CPUS;
        last = first;
        if (ok && *p == '-')
        {
            ++p;
            digits = 0;
            last = 0;
            while (*p >= '0' && *p <= '9' && last < TP_MAX_CPUS) { last = last * 10 + (unsigned)(*p++ - '0'); ++digits; }
            ok = digits > 0 && last < TP_MAX_CPUS && last >= first;
        }
        for (unsigned cpu = first; ok && cpu <= last; ++cpu)
        {
            ok = !tp_cpu_set_has(set, cpu);
            if (ok) tp_cpu_set_add(set, cpu);
        }
        if (ok && *p == ',')
        {
            ++p;
            ok = *p >= '0' && *p <= '9';
        }
        else if (ok)
        {
            ok = !*p || (*p == '\n' && !p[1]);
        }
    }
    ok = ok && set->count > 0;
    if (!ok) memset(set, 0, sizeof(*set));
    return ok;
}

static int tp_cpu_set_format(TpCpuSet const* set, char* out, size_t capacity)
{
    size_t used = 0;
    int ok = capacity > 0;
    if (ok) out[0] = 0;
    for (unsigned cpu = 0; cpu < TP_MAX_CPUS && ok; ++cpu)
    {
        if (tp_cpu_set_has(set, cpu))
        {
            unsigned last = cpu;
            while (last + 1 < TP_MAX_CPUS && tp_cpu_set_has(set, last + 1)) ++last;
            int length = last == cpu ? snprintf(out + used, capacity - used, "%s%u", used ? "," : "", cpu) :
                                       snprintf(out + used, capacity - used, "%s%u-%u", used ? "," : "", cpu, last);
            ok = length > 0 && (size_t)length < capacity - used;
            if (ok) used += (size_t)length;
            cpu = last;
        }
    }
    return ok;
}

/* 0 when EVERY requested CPU is in this process's permitted mask. EINVAL names
 * a request outside it; ENOSYS a platform without per-process CPU sets. */
static int tp_cpu_set_permitted(TpCpuSet const* set)
{
    int error = set->count ? 0 : EINVAL;
#ifdef __linux__
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (!error && sched_getaffinity(0, sizeof(allowed), &allowed) != 0) error = errno ? errno : EIO;
    for (unsigned cpu = 0; cpu < TP_MAX_CPUS && !error; ++cpu)
        if (tp_cpu_set_has(set, cpu) && !CPU_ISSET(cpu, &allowed)) error = EINVAL;
#elif defined(_WIN32)
    DWORD_PTR process_mask = 0, system_mask = 0;
    if (!error && !GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) error = EIO;
    for (unsigned cpu = 0; cpu < TP_MAX_CPUS && !error; ++cpu)
    {
        if (tp_cpu_set_has(set, cpu) &&
            (cpu >= sizeof(DWORD_PTR) * 8 || !(process_mask & ((DWORD_PTR)1 << cpu)))) error = EINVAL;
    }
#else
    if (!error) error = ENOSYS;
#endif
    return error;
}

/* The permitted mask itself, for --cpu-set auto. */
static int tp_cpu_set_allowed(TpCpuSet* set)
{
    memset(set, 0, sizeof(*set));
    int error = 0;
#ifdef __linux__
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) error = errno ? errno : EIO;
    for (unsigned cpu = 0; cpu < TP_MAX_CPUS && !error; ++cpu)
        if (CPU_ISSET(cpu, &allowed)) tp_cpu_set_add(set, cpu);
#elif defined(_WIN32)
    DWORD_PTR process_mask = 0, system_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) error = EIO;
    for (unsigned cpu = 0; cpu < sizeof(DWORD_PTR) * 8 && !error; ++cpu)
        if (process_mask & ((DWORD_PTR)1 << cpu)) tp_cpu_set_add(set, cpu);
#else
    error = ENOSYS;
#endif
    if (!error && !set->count) error = EINVAL;
    return error;
}

static int tp_topology_value(char const* root, unsigned cpu, char const* leaf, char* value, size_t capacity)
{
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/cpu%u/topology/%s", root, cpu, leaf);
    int error = length > 0 && (size_t)length < sizeof(path) ? 0 : ENAMETOOLONG;
    FILE* file = error ? NULL : fopen(path, "rb");
    if (!error && !file) error = errno ? errno : ENOENT;
    if (file)
    {
        size_t count = fread(value, 1, capacity - 1, file);
        value[count] = 0;
        if (ferror(file) || fgetc(file) != EOF || !count || memchr(value, 0, count)) error = EILSEQ;
        if (fclose(file) != 0 && !error) error = EIO;
    }
    return error;
}

static int tp_topology_integer(char const* text, int* value)
{
    char* end = NULL;
    long parsed = strtol(text, &end, 10);
    int ok = end != text && (*end == '\n' || !*end) && (!*end || !end[1]) && parsed >= -1 && parsed <= INT32_MAX;
    if (ok) *value = (int)parsed;
    return ok;
}

/* Missing or malformed topology for ANY requested CPU makes the whole topology
 * unavailable: it is never guessed from CPU numbering. Each CPU must name
 * itself among its own siblings. */
static int tp_topology_read(char const* root, TpCpuSet const* set, TpTopology* topology)
{
    memset(topology, 0, sizeof(*topology));
    int error = set->count ? 0 : EINVAL;
    for (unsigned cpu = 0; cpu < TP_MAX_CPUS && !error; ++cpu)
    {
        if (!tp_cpu_set_has(set, cpu)) continue;
        char text[TP_CPU_SET_TEXT_CAP];
        TpCpuSet siblings;
        TpCpuTopology* entry = topology->cpus + topology->count;
        entry->cpu = cpu;
        error = tp_topology_value(root, cpu, "thread_siblings_list", text, sizeof(text));
        if (!error && (!tp_cpu_set_parse(text, &siblings) || !tp_cpu_set_has(&siblings, cpu))) error = EILSEQ;
        if (!error) error = tp_topology_value(root, cpu, "physical_package_id", text, sizeof(text));
        if (!error && !tp_topology_integer(text, &entry->package)) error = EILSEQ;
        if (!error) error = tp_topology_value(root, cpu, "core_id", text, sizeof(text));
        if (!error && !tp_topology_integer(text, &entry->core_id)) error = EILSEQ;
        if (!error)
        {
            entry->siblings = siblings.count;
            for (unsigned c = 0; c < TP_MAX_CPUS; ++c)
            {
                if (tp_cpu_set_has(&siblings, c))
                {
                    entry->core = c;
                    break;
                }
            }
            unsigned seen = 0;
            for (unsigned i = 0; i < topology->count; ++i) seen |= topology->cpus[i].core == entry->core;
            topology->physical_cores += !seen;
            topology->count += 1;
        }
    }
    if (error) memset(topology, 0, sizeof(*topology));
    return error;
}

/* One logical CPU from each of the first N physical cores, in ascending CPU
 * order. Fewer cores than requested is an error; SMT siblings never double up. */
static int tp_topology_physical_prefix(TpTopology const* topology, unsigned cores, TpCpuSet* out)
{
    memset(out, 0, sizeof(*out));
    TpCpuSet used = {0};
    for (unsigned i = 0; i < topology->count && out->count < cores; ++i)
    {
        TpCpuTopology const* entry = topology->cpus + i;
        if (!tp_cpu_set_has(&used, entry->core))
        {
            tp_cpu_set_add(&used, entry->core);
            tp_cpu_set_add(out, entry->cpu);
        }
    }
    return cores > 0 && out->count == cores;
}

#endif
