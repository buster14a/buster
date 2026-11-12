// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// allcore: the all-core gauge, step 0 of docs/all-core-path.md (#2536). What a compile costs at one worker and at
// eight on the 9700X: its cycles in user mode and in the kernel, its peak and its faults, for the whole compile and for
// each stage, the stages taken from each worker's own --time-stages report. It changes no compile: it runs the command
// it is given as independent processes, one worker a CPU, all the workers starting together and each worker's runs one
// after another. It needs no compiler build: `cc -std=c17 -O2 -Wall -Wextra -Werror -o build/allcore tools/allcore/allcore.c`.
//
//   allcore counted --out DIR [--rounds N] [--loops N] [--one CPU] [--eight CPU,...] -- COMMAND...
//   allcore sampled --out DIR [--rounds N] [--loops-one N] [--loops-eight N] [--period N] [--buffer-mb N]
//                   [--one CPU] [--eight CPU,...] -- COMMAND...
//   allcore table --out DIR --bytes N
//   allcore compare --out DIR --record TABLES [--record-raw RAW --bytes N] [--tolerance PERCENT]
//   allcore self-test
//
// counted runs rounds of a cohort of one worker and one of eight (the order alternates by round), each run counted by
// counters of its own that the processes it starts inherit (the compiler's exit helper among them): cycles and
// instructions in user mode and in the kernel, and APERF and MPERF where the msr PMU has them. sampled runs rounds of
// the same cohorts under a group a worker's CPU, `{cycles, cycles:u, cycles:k}`, sampled every --period cycles of the
// leader with each sample's address and its time on CLOCK_MONOTONIC (the rows' clock), and puts each sample's deltas in
// the run whose span on its CPU holds its time and in the stage its time falls in, by the run's own report. Each part
// is one hold of the 9700X's lock (docs/commands/all-core.md has the commands and the budget). table reduces DIR to
// DIR/table.tsv (a line a stage group) and DIR/whole.tsv (a line a worker count); compare puts them beside a record's
// tables (buster-performance's all-core-rerun-2536-2026-11-12/tables/).
//
// The method is the record's (buster-performance#444, re-measured in #451): its runner and its reductions (`allcore
// cohort`, `rows`, `stages` and `buckets` there) gather every run's rows, medians over each worker's runs but its first
// and last (which the other workers don't overlap as the runs between are), and sampled cycles scaled to the unsampled
// medians. What differs: the samples are read from the kernel's ring buffers here, not from `perf script`'s text, so
// the reduction takes milliseconds and nothing large is kept; and one group gives both modes' cycles by stage, where the
// record sampled two groups in two cohorts.
//
// Layout: utilities (allcore_fail, allcore_median, allcore_substitute); options (allcore_options_parse); stage
// reports (allcore_report_parse, allcore_group_of); the strays (allcore_strays_take); the runner (allcore_cohort_run,
// allcore_worker_run, allcore_rows_write); samples (allcore_groups_open, allcore_groups_drain); symbols
// (allcore_symbols_load); buckets (allcore_buckets_compute, allcore_buckets_write); the parts (allcore_counted,
// allcore_sampled); reading back (allcore_rows_read, allcore_buckets_read); table; compare; self-test; main.
// It is measurement tooling outside the compile path, with no vector path: it reads some thousands of rows and some
// hundred thousand samples once.

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_ALLCORE_MAX_WORKERS 64
#define BUSTER_ALLCORE_MAX_COUNTERS 8
#define BUSTER_ALLCORE_MAX_STAGES 96
#define BUSTER_ALLCORE_MAX_GROUPS 32
#define BUSTER_ALLCORE_NAME_BYTES 64
#define BUSTER_ALLCORE_PATH_BYTES 4096
#define BUSTER_ALLCORE_LINE_BYTES 65536
#define BUSTER_ALLCORE_ANCHORS 3
// The sampling group's members: the leader (both modes' cycles), user cycles, kernel cycles.
#define BUSTER_ALLCORE_GROUP_EVENTS 3
// A sample's bytes: header, address, time, and the group's read (count, time enabled, time running, three values).
#define BUSTER_ALLCORE_SAMPLE_BYTES (8 + 8 + 8 + 8 * 3 + 8 * BUSTER_ALLCORE_GROUP_EVENTS)
// A stray: another owner's process that took more than half a CPU over a cohort (as the record's strays files).
#define BUSTER_ALLCORE_STRAY_SHARE 0.5

static const char* const allcore_labels[2] = {
    "one",
    "eight",
};

// ---------------------------------------------------------------------------------------------------------------------
// Utilities

static void allcore_fail(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    fprintf(stderr, "allcore: ");
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");
    va_end(arguments);
    exit(2);
}

static int64_t allcore_now_ns(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (int64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

static void* allcore_allocate(size_t bytes)
{
    void* result = calloc(1, bytes > 0 ? bytes : 1);
    if (result == NULL)
    {
        allcore_fail("out of memory (%zu bytes)", bytes);
    }
    return result;
}

static void* allcore_grow(void* items, size_t* capacity, size_t needed, size_t item_bytes)
{
    void* result = items;
    if (needed > *capacity)
    {
        size_t grown = *capacity > 0 ? *capacity * 2 : 64;
        while (grown < needed)
        {
            grown *= 2;
        }
        result = realloc(items, grown * item_bytes);
        if (result == NULL)
        {
            allcore_fail("out of memory (%zu items)", grown);
        }
        memset((char*)result + *capacity * item_bytes, 0, (grown - *capacity) * item_bytes);
        *capacity = grown;
    }
    return result;
}

static int allcore_compare_doubles(const void* left, const void* right)
{
    const double a = *(const double*)left;
    const double b = *(const double*)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

// The median of `count` values, which it sorts; 0 for none.
static double allcore_median(double* values, size_t count)
{
    double result = 0.0;
    if (count > 0)
    {
        qsort(values, count, sizeof(double), allcore_compare_doubles);
        result = count % 2 == 1 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) / 2.0;
    }
    return result;
}

static void allcore_format(char* buffer, size_t capacity, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(buffer, capacity, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= capacity)
    {
        allcore_fail("a path or a name is longer than %zu bytes", capacity);
    }
}

// `pattern` with each %w replaced by `worker`, %i by `run`, %l by `label` and %r by `round`, into `buffer`.
static void allcore_substitute(char* buffer, size_t capacity, const char* pattern, int worker, int run, const char* label, int round)
{
    size_t length = 0;
    bool fits = true;
    for (const char* at = pattern; *at != 0 && fits; at += 1)
    {
        char piece[BUSTER_ALLCORE_NAME_BYTES];
        size_t piece_length = 1;
        piece[0] = *at;
        if (at[0] == '%' && (at[1] == 'w' || at[1] == 'i' || at[1] == 'r'))
        {
            const int value = at[1] == 'w' ? worker : at[1] == 'i' ? run : round;
            piece_length = (size_t)snprintf(piece, sizeof(piece), "%d", value);
            at += 1;
        }
        else if (at[0] == '%' && at[1] == 'l')
        {
            piece_length = (size_t)snprintf(piece, sizeof(piece), "%s", label);
            at += 1;
        }
        fits = length + piece_length < capacity;
        if (fits)
        {
            memcpy(buffer + length, piece, piece_length);
            length += piece_length;
        }
    }
    if (!fits)
    {
        allcore_fail("'%s' expands past %zu bytes", pattern, capacity);
    }
    buffer[length] = 0;
}

static void allcore_make_directory(const char* path)
{
    char partial[BUSTER_ALLCORE_PATH_BYTES];
    allcore_format(partial, sizeof(partial), "%s", path);
    for (char* at = partial + 1; *at != 0; at += 1)
    {
        if (*at == '/')
        {
            *at = 0;
            mkdir(partial, 0755);
            *at = '/';
        }
    }
    if (mkdir(partial, 0755) != 0 && errno != EEXIST)
    {
        allcore_fail("can't make %s: %s", path, strerror(errno));
    }
}

// Splits `line` at its tabs, in place, into at most `capacity` fields; returns their count.
static int allcore_split_tabs(char* line, char** fields, int capacity)
{
    int count = 0;
    char* at = line;
    bool more = true;
    while (more && count < capacity)
    {
        fields[count] = at;
        count += 1;
        char* tab = strchr(at, '\t');
        more = tab != NULL;
        if (more)
        {
            *tab = 0;
            at = tab + 1;
        }
    }
    return count;
}

static void allcore_chomp(char* line)
{
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
    {
        length -= 1;
    }
    line[length] = 0;
}

static int64_t allcore_timeval_us(struct timeval value)
{
    return (int64_t)value.tv_sec * 1000000 + value.tv_usec;
}

// ---------------------------------------------------------------------------------------------------------------------
// Options

typedef struct Options Options;
struct Options
{
    const char* out;
    const char* record;
    const char* record_raw;
    char** command;
    int command_count;
    int rounds;
    int loops;
    int loops_one;
    int loops_eight;
    int cpus_one;
    int cpus_eight[BUSTER_ALLCORE_MAX_WORKERS];
    int eight_count;
    uint64_t period;
    size_t buffer_mb;
    double bytes;
    double tolerance;
};

// A list of CPUs, CPU,CPU... (each a decimal number); returns their count.
static int allcore_cpus_parse(const char* text, int* cpus)
{
    int count = 0;
    const char* at = text;
    bool more = true;
    while (more)
    {
        char* end = NULL;
        const long value = strtol(at, &end, 10);
        if (end == at || value < 0 || value >= CPU_SETSIZE || count == BUSTER_ALLCORE_MAX_WORKERS || (*end != 0 && *end != ','))
        {
            allcore_fail("'%s' isn't a list of CPUs", text);
        }
        cpus[count] = (int)value;
        count += 1;
        more = *end == ',';
        at = end + 1;
    }
    return count;
}

static Options allcore_options_parse(int argc, char** argv)
{
    Options options = {
        .rounds = 0,
        .loops = 6,
        .loops_one = 6,
        .loops_eight = 5,
        .cpus_one = 2,
        .eight_count = 0,
        .period = 100003,
        .buffer_mb = 16,
        .tolerance = 3.0,
    };
    // One worker on CPU 2, the 9700X's one core with the performance governor, whose sibling (10) is offline; eight on
    // one CPU of each physical core: the record's.
    for (int cpu = 0; cpu < 8; cpu += 1)
    {
        options.cpus_eight[cpu] = cpu;
    }
    options.eight_count = 8;
    int at = 2;
    bool more = true;
    while (more && at < argc)
    {
        const char* name = argv[at];
        const char* value = at + 1 < argc ? argv[at + 1] : NULL;
        if (strcmp(name, "--") == 0)
        {
            options.command = argv + at + 1;
            options.command_count = argc - at - 1;
            more = false;
        }
        else if (value == NULL)
        {
            allcore_fail("%s needs a value, or is unknown", name);
        }
        else if (strcmp(name, "--out") == 0)
        {
            options.out = value;
        }
        else if (strcmp(name, "--record") == 0)
        {
            options.record = value;
        }
        else if (strcmp(name, "--record-raw") == 0)
        {
            options.record_raw = value;
        }
        else if (strcmp(name, "--rounds") == 0)
        {
            options.rounds = atoi(value);
        }
        else if (strcmp(name, "--loops") == 0)
        {
            options.loops = atoi(value);
        }
        else if (strcmp(name, "--loops-one") == 0)
        {
            options.loops_one = atoi(value);
        }
        else if (strcmp(name, "--loops-eight") == 0)
        {
            options.loops_eight = atoi(value);
        }
        else if (strcmp(name, "--one") == 0)
        {
            int cpus[BUSTER_ALLCORE_MAX_WORKERS];
            if (allcore_cpus_parse(value, cpus) != 1)
            {
                allcore_fail("--one takes one CPU");
            }
            options.cpus_one = cpus[0];
        }
        else if (strcmp(name, "--eight") == 0)
        {
            options.eight_count = allcore_cpus_parse(value, options.cpus_eight);
        }
        else if (strcmp(name, "--period") == 0)
        {
            options.period = strtoull(value, NULL, 10);
        }
        else if (strcmp(name, "--buffer-mb") == 0)
        {
            options.buffer_mb = (size_t)strtoull(value, NULL, 10);
        }
        else if (strcmp(name, "--bytes") == 0)
        {
            options.bytes = atof(value);
        }
        else if (strcmp(name, "--tolerance") == 0)
        {
            options.tolerance = atof(value);
        }
        else
        {
            allcore_fail("unknown option %s", name);
        }
        at += more ? 2 : 0;
    }
    if (options.out == NULL)
    {
        allcore_fail("--out DIR is required");
    }
    if (options.loops < 1 || options.loops_one < 1 || options.loops_eight < 1 || options.period < 1000 || options.buffer_mb < 1 || options.eight_count < 1)
    {
        allcore_fail("--loops, --loops-one and --loops-eight take at least 1, --period at least 1000, --buffer-mb at least 1");
    }
    return options;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stage reports

typedef struct Report Report;
struct Report
{
    char names[BUSTER_ALLCORE_MAX_STAGES][BUSTER_ALLCORE_NAME_BYTES];
    double ms[BUSTER_ALLCORE_MAX_STAGES];
    double faults[BUSTER_ALLCORE_MAX_STAGES];
    int count;
    double tsc_mhz;
    double tokens;
    char huge_pages[16];
    bool found;
};

// A --time-stages report's top-level `time-stage` rows (a stage's phases, whose names hold a '.', are left out, as the
// `total` row is), and the `time-process` lines this reads.
static Report allcore_report_parse(FILE* file)
{
    Report report = { 0 };
    char line[4096];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        allcore_chomp(line);
        char* fields[8];
        const int count = allcore_split_tabs(line, fields, 8);
        if (count >= 3 && strcmp(fields[0], "time-stage") == 0 && strcmp(fields[1], "stage") != 0 && strcmp(fields[1], "total") != 0 && strchr(fields[1], '.') == NULL &&
            report.count < BUSTER_ALLCORE_MAX_STAGES)
        {
            allcore_format(report.names[report.count], BUSTER_ALLCORE_NAME_BYTES, "%s", fields[1]);
            report.ms[report.count] = atof(fields[2]);
            report.faults[report.count] = count >= 6 ? atof(fields[5]) : 0.0;
            report.count += 1;
            report.found = true;
        }
        else if (count >= 3 && strcmp(fields[0], "time-process") == 0)
        {
            if (strcmp(fields[1], "tsc-mhz") == 0)
            {
                report.tsc_mhz = atof(fields[2]);
            }
            else if (strcmp(fields[1], "tokens") == 0)
            {
                report.tokens = atof(fields[2]);
            }
            else if (strcmp(fields[1], "huge-pages") == 0)
            {
                allcore_format(report.huge_pages, sizeof(report.huge_pages), "%s", fields[2]);
            }
        }
    }
    return report;
}

static Report allcore_report_read(const char* path)
{
    Report report = { 0 };
    FILE* file = fopen(path, "r");
    if (file != NULL)
    {
        report = allcore_report_parse(file);
        fclose(file);
    }
    return report;
}

// The stage table's group of a report's stage, or of one the buckets add (startup, exit, teardown): the record's
// (its tables.sh, `group_of`). A stage this doesn't know is its own group.
static const char* allcore_group_of(const char* stage)
{
    static const char* const emission[] = {
        "tier0",
        "emit-setup",
        "lanes",
        "branch-sizing",
        "encode",
        "unwind",
        "code-atoms",
    };
    static const char* const object[] = {
        "debug",
        "publish-data",
        "object-build",
        "object-write",
        "runtime",
        "executable",
        "write",
        "release",
    };
    static const char* const start[] = {
        "startup",
        "setup",
        "lanes-start",
        "type-table",
        "backend-setup",
        "exit",
        "teardown",
    };
    const char* result = stage;
    for (size_t i = 0; i < sizeof(emission) / sizeof(emission[0]); i += 1)
    {
        result = strcmp(stage, emission[i]) == 0 ? "emission" : result;
    }
    for (size_t i = 0; i < sizeof(object) / sizeof(object[0]); i += 1)
    {
        result = strcmp(stage, object[i]) == 0 ? "object" : result;
    }
    for (size_t i = 0; i < sizeof(start) / sizeof(start[0]); i += 1)
    {
        result = strcmp(stage, start[i]) == 0 ? "start" : result;
    }
    result = strcmp(stage, "static-data") == 0 ? "check-declarations" : result;
    return result;
}

// The groups in the table's order: the record's, then any other in the order first seen (a stage a later compiler
// adds). Indices never move, so figures gathered by index stay with their group.
typedef struct Groups Groups;
struct Groups
{
    char names[BUSTER_ALLCORE_MAX_GROUPS][BUSTER_ALLCORE_NAME_BYTES];
    int count;
};

static Groups allcore_groups_new(void)
{
    static const char* const order[] = {
        "preprocess",
        "parse",
        "check-input",
        "check-expressions",
        "check-declarations",
        "check-program",
        "select",
        "lower",
        "emission",
        "object",
        "start",
    };
    Groups groups = { 0 };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i += 1)
    {
        allcore_format(groups.names[groups.count], BUSTER_ALLCORE_NAME_BYTES, "%s", order[i]);
        groups.count += 1;
    }
    return groups;
}

// The index of a stage's group, adding the group at the end if it is new.
static int allcore_groups_index(Groups* groups, const char* stage)
{
    const char* group = allcore_group_of(stage);
    int index = -1;
    for (int i = 0; i < groups->count; i += 1)
    {
        index = index < 0 && strcmp(groups->names[i], group) == 0 ? i : index;
    }
    if (index < 0)
    {
        if (groups->count == BUSTER_ALLCORE_MAX_GROUPS)
        {
            allcore_fail("more than %d stage groups", BUSTER_ALLCORE_MAX_GROUPS);
        }
        allcore_format(groups->names[groups->count], BUSTER_ALLCORE_NAME_BYTES, "%s", group);
        index = groups->count;
        groups->count += 1;
    }
    return index;
}

// ---------------------------------------------------------------------------------------------------------------------
// The strays: other owners' processes that took more than half a CPU over a cohort, from /proc's clock ticks before
// and after it (a run the lock doesn't cover shares a worker's CPU or its core: #2555's stray on 12 November).

typedef struct ProcessTicks ProcessTicks;
struct ProcessTicks
{
    int pid;
    int cpu;
    uint64_t start;
    uint64_t ticks;
    char name[32];
};

typedef struct Snapshot Snapshot;
struct Snapshot
{
    ProcessTicks* items;
    size_t count;
    size_t capacity;
    int64_t when_ns;
};

static Snapshot allcore_strays_take(void)
{
    Snapshot snapshot = { 0 };
    snapshot.when_ns = allcore_now_ns();
    DIR* directory = opendir("/proc");
    struct dirent* entry = directory != NULL ? readdir(directory) : NULL;
    while (entry != NULL)
    {
        if (isdigit((unsigned char)entry->d_name[0]))
        {
            char path[64];
            allcore_format(path, sizeof(path), "/proc/%s/stat", entry->d_name);
            FILE* file = fopen(path, "r");
            char line[1024];
            const bool read = file != NULL && fgets(line, sizeof(line), file) != NULL;
            // The name is in parentheses and may hold spaces and parentheses: the fields follow the last ')'.
            char* open = read ? strchr(line, '(') : NULL;
            char* close = read ? strrchr(line, ')') : NULL;
            if (open != NULL && close != NULL && close > open)
            {
                ProcessTicks item = { 0 };
                item.pid = atoi(line);
                const size_t name_length = (size_t)(close - open - 1) < sizeof(item.name) - 1 ? (size_t)(close - open - 1) : sizeof(item.name) - 1;
                memcpy(item.name, open + 1, name_length);
                // Fields from the state (3): utime is 14, stime 15, starttime 22, processor 39.
                int field = 3;
                char* save = NULL;
                for (char* token = strtok_r(close + 2, " ", &save); token != NULL; token = strtok_r(NULL, " ", &save))
                {
                    item.ticks += field == 14 || field == 15 ? strtoull(token, NULL, 10) : 0;
                    item.start = field == 22 ? strtoull(token, NULL, 10) : item.start;
                    item.cpu = field == 39 ? atoi(token) : item.cpu;
                    field += 1;
                }
                snapshot.items = allcore_grow(snapshot.items, &snapshot.capacity, snapshot.count + 1, sizeof(ProcessTicks));
                snapshot.items[snapshot.count] = item;
                snapshot.count += 1;
            }
            if (file != NULL)
            {
                fclose(file);
            }
        }
        entry = readdir(directory);
    }
    if (directory != NULL)
    {
        closedir(directory);
    }
    return snapshot;
}

// Writes the strays between two snapshots to `path` (a line each: its CPU, its share of a CPU, its ID and its name) and
// returns their count. This process and its parents are not strays.
static int allcore_strays_write(const Snapshot* before, const Snapshot* after, const char* path)
{
    int strays = 0;
    FILE* out = fopen(path, "w");
    if (out == NULL)
    {
        allcore_fail("can't write %s: %s", path, strerror(errno));
    }
    const double seconds = (double)(after->when_ns - before->when_ns) / 1e9;
    const double hertz = (double)sysconf(_SC_CLK_TCK);
    const int self = getpid();
    const int parent = getppid();
    for (size_t i = 0; i < after->count && seconds > 0.0; i += 1)
    {
        const ProcessTicks* now = &after->items[i];
        uint64_t earlier = 0;
        for (size_t j = 0; j < before->count; j += 1)
        {
            earlier = before->items[j].pid == now->pid && before->items[j].start == now->start ? before->items[j].ticks : earlier;
        }
        const double share = (double)(now->ticks - earlier) / hertz / seconds;
        if (share > BUSTER_ALLCORE_STRAY_SHARE && now->pid != self && now->pid != parent)
        {
            fprintf(out, "cpu\t%d\tshare\t%.2f\tpid\t%d\t%s\n", now->cpu, share, now->pid, now->name);
            strays += 1;
        }
    }
    fclose(out);
    return strays;
}

// ---------------------------------------------------------------------------------------------------------------------
// The runner

// A counter of a run: an event by its PMU's type and config, in user mode, in the kernel or in both, for the run's
// process and every process it starts.
typedef struct Counter Counter;
struct Counter
{
    char name[BUSTER_ALLCORE_NAME_BYTES];
    uint32_t type;
    uint64_t config;
    char mode;
};

typedef struct Row Row;
struct Row
{
    int64_t start_ns;
    int64_t exit_ns;
    int64_t reap_ns;
    int64_t user_us;
    int64_t system_us;
    int64_t orphan_user_us;
    int64_t orphan_system_us;
    int64_t peak_kb;
    int64_t minor_faults;
    int64_t major_faults;
    int64_t voluntary;
    int64_t involuntary;
    int64_t orphan_minor_faults;
    int64_t counters[BUSTER_ALLCORE_MAX_COUNTERS];
    // The least share of the run's time that one of its counters counted for, in hundredths of a percent: 10,000 when
    // none was multiplexed.
    int32_t counted;
    int32_t orphans;
    int32_t status;
    int32_t done;
    int32_t late;
};

typedef struct Cohort Cohort;
struct Cohort
{
    const char* label;
    int round;
    int workers;
    int cpus[BUSTER_ALLCORE_MAX_WORKERS];
    int loops;
    char** command;
    int command_count;
    char report_pattern[BUSTER_ALLCORE_PATH_BYTES];
    const Counter* counters;
    int counter_count;
    int64_t start_ns;
    // workers * loops rows, a worker's runs together; shared with the workers.
    Row* rows;
};

// One run of a worker: the child waits at a gate until its counters are attached, which start counting as it becomes
// the command; then its status, its resource use, and the orphans it left (the compiler's exit helper tears its
// address space down after the compiler's parent has its status: their time is the run's too).
static void allcore_run_one(const Cohort* cohort, int worker, int run, Row* row)
{
    char** arguments = allcore_allocate(((size_t)cohort->command_count + 1) * sizeof(char*));
    for (int i = 0; i < cohort->command_count; i += 1)
    {
        char buffer[BUSTER_ALLCORE_PATH_BYTES];
        allcore_substitute(buffer, sizeof(buffer), cohort->command[i], worker, run, cohort->label, cohort->round);
        arguments[i] = strdup(buffer);
    }
    char report_path[BUSTER_ALLCORE_PATH_BYTES];
    allcore_substitute(report_path, sizeof(report_path), cohort->report_pattern, worker, run, cohort->label, cohort->round);
    int gate[2];
    if (pipe(gate) != 0)
    {
        _exit(4);
    }
    const pid_t child = fork();
    if (child == 0)
    {
        char byte = 0;
        close(gate[1]);
        const ssize_t got = read(gate[0], &byte, 1);
        close(gate[0]);
        const int null = open("/dev/null", O_WRONLY);
        const int error = open(report_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(null, 1);
        dup2(error >= 0 ? error : null, 2);
        if (got == 0)
        {
            execvp(arguments[0], arguments);
        }
        _exit(127);
    }
    int descriptors[BUSTER_ALLCORE_MAX_COUNTERS];
    bool attached = child > 0;
    for (int c = 0; c < cohort->counter_count && attached; c += 1)
    {
        const Counter* counter = &cohort->counters[c];
        struct perf_event_attr attribute;
        memset(&attribute, 0, sizeof(attribute));
        attribute.size = sizeof(attribute);
        attribute.type = counter->type;
        attribute.config = counter->config;
        attribute.disabled = 1;
        attribute.enable_on_exec = 1;
        attribute.inherit = 1;
        attribute.exclude_kernel = counter->mode == 'u';
        attribute.exclude_user = counter->mode == 'k';
        // No exclusion at all in mode a: the msr PMU (APERF, MPERF) refuses any.
        attribute.exclude_hv = counter->mode != 'a';
        attribute.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        descriptors[c] = (int)syscall(SYS_perf_event_open, &attribute, child, -1, -1, 0);
        if (descriptors[c] < 0)
        {
            fprintf(stderr, "allcore: worker %d can't count %s: %s\n", worker, counter->name, strerror(errno));
            attached = false;
        }
    }
    if (!attached)
    {
        // Only a child this run made: kill(-1) would signal every process this user may.
        if (child > 0)
        {
            kill(child, SIGKILL);
        }
        _exit(5);
    }
    close(gate[0]);
    row->start_ns = allcore_now_ns();
    close(gate[1]);
    int status = 0;
    struct rusage use = { 0 };
    const pid_t waited = wait4(child, &status, 0, &use);
    row->exit_ns = allcore_now_ns();
    row->status = waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : 255;
    row->user_us = allcore_timeval_us(use.ru_utime);
    row->system_us = allcore_timeval_us(use.ru_stime);
    row->peak_kb = use.ru_maxrss;
    row->minor_faults = use.ru_minflt;
    row->major_faults = use.ru_majflt;
    row->voluntary = use.ru_nvcsw;
    row->involuntary = use.ru_nivcsw;
    bool more = true;
    while (more)
    {
        int orphan_status = 0;
        struct rusage orphan_use = { 0 };
        const pid_t orphan = wait4(-1, &orphan_status, 0, &orphan_use);
        more = orphan > 0;
        if (more)
        {
            row->orphans += 1;
            row->orphan_user_us += allcore_timeval_us(orphan_use.ru_utime);
            row->orphan_system_us += allcore_timeval_us(orphan_use.ru_stime);
            row->orphan_minor_faults += orphan_use.ru_minflt;
        }
    }
    row->reap_ns = allcore_now_ns();
    row->counted = 10000;
    for (int c = 0; c < cohort->counter_count; c += 1)
    {
        uint64_t values[3] = { 0 };
        const ssize_t got = read(descriptors[c], values, sizeof(values));
        row->counters[c] = got == (ssize_t)sizeof(values) ? (int64_t)values[0] : -1;
        const int32_t share = values[1] > 0 ? (int32_t)((double)values[2] * 10000.0 / (double)values[1]) : 0;
        row->counted = share < row->counted ? share : row->counted;
        close(descriptors[c]);
    }
    row->done = 1;
    for (int i = 0; i < cohort->command_count; i += 1)
    {
        free(arguments[i]);
    }
    free(arguments);
}

// One worker: pinned to its CPU, a subreaper so that the orphans its runs leave are its to wait for, spinning until the
// cohort's start so that its core is awake and at its frequency, then its runs one after another.
static void allcore_worker_run(const Cohort* cohort, int worker)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cohort->cpus[worker], &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
    {
        fprintf(stderr, "allcore: worker %d can't be pinned to CPU %d: %s\n", worker, cohort->cpus[worker], strerror(errno));
        _exit(3);
    }
    prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
    const bool late = allcore_now_ns() > cohort->start_ns;
    while (allcore_now_ns() < cohort->start_ns)
    {
    }
    for (int run = 0; run < cohort->loops; run += 1)
    {
        Row* row = &cohort->rows[(size_t)worker * (size_t)cohort->loops + (size_t)run];
        row->late = late;
        allcore_run_one(cohort, worker, run, row);
    }
    _exit(0);
}

// Runs the cohort: its workers start together 20 ms after this call. Returns false when a worker or a run failed.
static bool allcore_cohort_run(Cohort* cohort)
{
    const size_t rows = (size_t)cohort->workers * (size_t)cohort->loops;
    cohort->rows = mmap(NULL, rows * sizeof(Row), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (cohort->rows == MAP_FAILED)
    {
        allcore_fail("no memory for a cohort's rows");
    }
    cohort->start_ns = allcore_now_ns() + 20000000;
    pid_t pids[BUSTER_ALLCORE_MAX_WORKERS];
    for (int worker = 0; worker < cohort->workers; worker += 1)
    {
        pids[worker] = fork();
        if (pids[worker] == 0)
        {
            allcore_worker_run(cohort, worker);
        }
        if (pids[worker] < 0)
        {
            allcore_fail("fork: %s", strerror(errno));
        }
    }
    bool result = true;
    for (int worker = 0; worker < cohort->workers; worker += 1)
    {
        int status = 0;
        result = result && waitpid(pids[worker], &status, 0) == pids[worker] && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    for (size_t i = 0; i < rows; i += 1)
    {
        result = result && cohort->rows[i].done && cohort->rows[i].status == 0;
    }
    return result;
}

// The rows as the record's runner writes them (`allcore cohort` there), so that its reducers read them too.
static void allcore_rows_write(const Cohort* cohort, const char* path)
{
    FILE* out = fopen(path, "w");
    if (out == NULL)
    {
        allcore_fail("can't write %s: %s", path, strerror(errno));
    }
    fprintf(out, "# cohort\t%s\tworkers\t%d\tstart_ns\t%lld\tround\t%d\n", cohort->label, cohort->workers, (long long)cohort->start_ns, cohort->round);
    fprintf(out, "label\tworker\tcpu\trun\tstart_ns\texit_ns\treap_ns\tstatus\tuser_us\tsystem_us\tpeak_kb\tminor_faults\tmajor_faults\tvoluntary\tinvoluntary\torphans\torphan_user_us\t"
                 "orphan_system_us\torphan_minor_faults\tlate");
    for (int c = 0; c < cohort->counter_count; c += 1)
    {
        fprintf(out, "\t%s:%c", cohort->counters[c].name, cohort->counters[c].mode);
    }
    fprintf(out, "%s\n", cohort->counter_count > 0 ? "\tcounted" : "");
    for (int worker = 0; worker < cohort->workers; worker += 1)
    {
        for (int run = 0; run < cohort->loops; run += 1)
        {
            const Row* row = &cohort->rows[(size_t)worker * (size_t)cohort->loops + (size_t)run];
            fprintf(out, "%s\t%d\t%d\t%d\t%lld\t%lld\t%lld\t%d\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%d\t%lld\t%lld\t%lld\t%d", cohort->label, worker, cohort->cpus[worker], run,
                    (long long)row->start_ns, (long long)row->exit_ns, (long long)row->reap_ns, row->done ? row->status : -1, (long long)row->user_us, (long long)row->system_us,
                    (long long)row->peak_kb, (long long)row->minor_faults, (long long)row->major_faults, (long long)row->voluntary, (long long)row->involuntary, row->orphans,
                    (long long)row->orphan_user_us, (long long)row->orphan_system_us, (long long)row->orphan_minor_faults, row->late);
            for (int c = 0; c < cohort->counter_count; c += 1)
            {
                fprintf(out, "\t%lld", (long long)row->counters[c]);
            }
            if (cohort->counter_count > 0)
            {
                fprintf(out, "\t%.2f", (double)row->counted / 100.0);
            }
            fprintf(out, "\n");
        }
    }
    fclose(out);
}

// Whether a worker's run is interior: each worker's runs but its first and last, when it has three or more.
static bool allcore_interior(int run, int loops)
{
    return loops < 3 || (run > 0 && run < loops - 1);
}

// ---------------------------------------------------------------------------------------------------------------------
// Samples: a group a worker's CPU, every task on it (perf_event_open's pid -1), sampled by its leader.

typedef struct Sample Sample;
struct Sample
{
    int64_t time_ns;
    uint64_t ip;
    // The group's counts since the CPU's previous sample: both modes' cycles, user cycles, kernel cycles.
    uint64_t delta[BUSTER_ALLCORE_GROUP_EVENTS];
};

typedef struct SampleGroup SampleGroup;
struct SampleGroup
{
    int cpu;
    int descriptors[BUSTER_ALLCORE_GROUP_EVENTS];
    uint8_t* map;
    size_t map_bytes;
    size_t page_bytes;
    Sample* samples;
    size_t count;
    uint64_t lost;
    bool full;
    bool multiplexed;
};

static int allcore_event_open(struct perf_event_attr* attribute, int cpu, int leader)
{
    return (int)syscall(SYS_perf_event_open, attribute, -1, cpu, leader, PERF_FLAG_FD_CLOEXEC);
}

static void allcore_groups_open(SampleGroup* groups, const int* cpus, int count, uint64_t period, size_t buffer_mb)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t data_pages = 1;
    while (data_pages * page < buffer_mb * 1024 * 1024)
    {
        data_pages *= 2;
    }
    for (int g = 0; g < count; g += 1)
    {
        SampleGroup* group = &groups[g];
        memset(group, 0, sizeof(*group));
        group->cpu = cpus[g];
        group->page_bytes = page;
        for (int e = 0; e < BUSTER_ALLCORE_GROUP_EVENTS; e += 1)
        {
            struct perf_event_attr attribute;
            memset(&attribute, 0, sizeof(attribute));
            attribute.size = sizeof(attribute);
            attribute.type = PERF_TYPE_HARDWARE;
            attribute.config = PERF_COUNT_HW_CPU_CYCLES;
            attribute.exclude_hv = 1;
            attribute.exclude_kernel = e == 1;
            attribute.exclude_user = e == 2;
            attribute.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
            // The samples' times on the rows' clock; a group's events must all have its leader's clock.
            attribute.use_clockid = 1;
            attribute.clockid = CLOCK_MONOTONIC;
            if (e == 0)
            {
                // The leader: disabled until the cohort, on the PMU throughout or in error (never multiplexed).
                attribute.disabled = 1;
                attribute.pinned = 1;
                attribute.sample_period = period;
                attribute.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TIME | PERF_SAMPLE_READ;
                attribute.wakeup_events = 1u << 30;
            }
            group->descriptors[e] = allcore_event_open(&attribute, group->cpu, e == 0 ? -1 : group->descriptors[0]);
            if (group->descriptors[e] < 0)
            {
                allcore_fail("can't sample cycles on CPU %d (the group's event %d): %s (kernel.perf_event_paranoid must allow CPU-wide events)", group->cpu, e, strerror(errno));
            }
        }
        group->map_bytes = (data_pages + 1) * page;
        group->map = mmap(NULL, group->map_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, group->descriptors[0], 0);
        if (group->map == MAP_FAILED)
        {
            allcore_fail("can't map CPU %d's samples (%zu MB): %s", group->cpu, buffer_mb, strerror(errno));
        }
    }
}

static void allcore_groups_switch(SampleGroup* groups, int count, bool on)
{
    for (int g = 0; g < count; g += 1)
    {
        if (ioctl(groups[g].descriptors[0], on ? PERF_EVENT_IOC_ENABLE : PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) != 0)
        {
            allcore_fail("can't %s CPU %d's group: %s", on ? "enable" : "disable", groups[g].cpu, strerror(errno));
        }
    }
}

// Copies `bytes` from the ring at `offset` (wrapping at its end).
static void allcore_ring_copy(const uint8_t* data, size_t size, uint64_t offset, void* out, size_t bytes)
{
    const size_t at = (size_t)(offset % size);
    const size_t first = bytes < size - at ? bytes : size - at;
    memcpy(out, data + at, first);
    memcpy((uint8_t*)out + first, data, bytes - first);
}

// Reads each group's samples, in time order, as deltas of its counts; notes lost records and a buffer that filled.
// The groups are disabled: nothing writes the rings any more.
static void allcore_groups_drain(SampleGroup* groups, int count)
{
    for (int g = 0; g < count; g += 1)
    {
        SampleGroup* group = &groups[g];
        const struct perf_event_mmap_page* header = (const struct perf_event_mmap_page*)group->map;
        const uint64_t head = __atomic_load_n(&header->data_head, __ATOMIC_ACQUIRE);
        const uint64_t tail = header->data_tail;
        const uint8_t* data = group->map + group->page_bytes;
        const size_t size = group->map_bytes - group->page_bytes;
        // A full ring drops what comes next: within a record's room of its end, some samples may be gone.
        group->full = head - tail + 2 * BUSTER_ALLCORE_SAMPLE_BYTES > size;
        // The group was on the PMU the whole time it was enabled, or its deltas miss what it didn't count (a pinned
        // group that can't be scheduled is in error, and its read is empty).
        uint64_t totals[3 + BUSTER_ALLCORE_GROUP_EVENTS] = { 0 };
        const ssize_t got = read(group->descriptors[0], totals, sizeof(totals));
        group->multiplexed = got != (ssize_t)sizeof(totals) || totals[0] != BUSTER_ALLCORE_GROUP_EVENTS || totals[2] < totals[1];
        size_t capacity = 0;
        uint64_t previous[BUSTER_ALLCORE_GROUP_EVENTS] = { 0 };
        uint64_t offset = tail;
        while (offset + sizeof(struct perf_event_header) <= head)
        {
            struct perf_event_header record;
            allcore_ring_copy(data, size, offset, &record, sizeof(record));
            if (record.size < sizeof(record))
            {
                allcore_fail("CPU %d's ring holds a record of %u bytes", group->cpu, record.size);
            }
            if (record.type == PERF_RECORD_SAMPLE && record.size == BUSTER_ALLCORE_SAMPLE_BYTES)
            {
                uint64_t words[(BUSTER_ALLCORE_SAMPLE_BYTES - 8) / 8];
                allcore_ring_copy(data, size, offset + sizeof(record), words, sizeof(words));
                // ip, time, the read: its count of values, time enabled, time running, then the values.
                if (words[2] != BUSTER_ALLCORE_GROUP_EVENTS)
                {
                    allcore_fail("CPU %d's samples read %llu values, not %d", group->cpu, (unsigned long long)words[2], BUSTER_ALLCORE_GROUP_EVENTS);
                }
                group->samples = allcore_grow(group->samples, &capacity, group->count + 1, sizeof(Sample));
                Sample* sample = &group->samples[group->count];
                sample->ip = words[0];
                sample->time_ns = (int64_t)words[1];
                for (int e = 0; e < BUSTER_ALLCORE_GROUP_EVENTS; e += 1)
                {
                    sample->delta[e] = words[5 + e] - previous[e];
                    previous[e] = words[5 + e];
                }
                group->count += 1;
            }
            else if (record.type == PERF_RECORD_LOST && record.size >= sizeof(record) + 16)
            {
                uint64_t words[2];
                allcore_ring_copy(data, size, offset + sizeof(record), words, sizeof(words));
                group->lost += words[1];
            }
            offset += record.size;
        }
    }
}

static void allcore_groups_close(SampleGroup* groups, int count)
{
    for (int g = 0; g < count; g += 1)
    {
        munmap(groups[g].map, groups[g].map_bytes);
        for (int e = BUSTER_ALLCORE_GROUP_EVENTS - 1; e >= 0; e -= 1)
        {
            close(groups[g].descriptors[e]);
        }
        free(groups[g].samples);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Symbols: the address ranges of the functions that anchor a run's stage clock to the samples' (the record's anchors:
// the parser's, the reader's and Tier 0's first functions, `parse_`, `check_input` and `tier0_generate`). The compiler
// is linked at a fixed address (CMakeLists.txt's buster_link: -no-pie), so its symbols' values are the samples'
// addresses; the sampled part refuses a position-independent executable, whose samples it couldn't anchor.

typedef struct Range Range;
struct Range
{
    uint64_t start;
    uint64_t end;
};

typedef struct Anchors Anchors;
struct Anchors
{
    Range* ranges[BUSTER_ALLCORE_ANCHORS];
    size_t counts[BUSTER_ALLCORE_ANCHORS];
    bool fixed;
};

static const char* const allcore_anchor_prefixes[BUSTER_ALLCORE_ANCHORS] = {
    "parse_",
    "check_input",
    "tier0_generate",
};

// The stage whose end comes before each anchor's first function: the preprocessor's before the parser's, the type
// table's before the reader's, the lowering's before Tier 0's.
static const char* const allcore_anchor_after[BUSTER_ALLCORE_ANCHORS] = {
    "preprocess",
    "type-table",
    "lower",
};

static int allcore_compare_ranges(const void* left, const void* right)
{
    const Range* a = left;
    const Range* b = right;
    return a->start < b->start ? -1 : a->start > b->start ? 1 : 0;
}

static bool allcore_read_at(int descriptor, void* out, size_t bytes, uint64_t offset)
{
    size_t done = 0;
    bool fine = true;
    while (fine && done < bytes)
    {
        const ssize_t got = pread(descriptor, (uint8_t*)out + done, bytes - done, (off_t)(offset + done));
        fine = got > 0;
        done += fine ? (size_t)got : 0;
    }
    return fine;
}

// The executable `name` names: itself when it holds a '/', else the first one PATH finds.
static void allcore_executable_find(const char* name, char* path, size_t capacity)
{
    if (strchr(name, '/') != NULL)
    {
        allcore_format(path, capacity, "%s", name);
    }
    else
    {
        const char* search = getenv("PATH") != NULL ? getenv("PATH") : "/usr/bin:/bin";
        bool found = false;
        while (!found && *search != 0)
        {
            const char* colon = strchr(search, ':');
            const size_t length = colon != NULL ? (size_t)(colon - search) : strlen(search);
            char candidate[BUSTER_ALLCORE_PATH_BYTES];
            allcore_format(candidate, sizeof(candidate), "%.*s/%s", (int)length, search, name);
            found = access(candidate, X_OK) == 0;
            if (found)
            {
                allcore_format(path, capacity, "%s", candidate);
            }
            search += length + (colon != NULL ? 1 : 0);
        }
        if (!found)
        {
            allcore_format(path, capacity, "%s", name);
        }
    }
}

static Anchors allcore_symbols_load(const char* executable)
{
    Anchors anchors = { 0 };
    const int descriptor = open(executable, O_RDONLY | O_CLOEXEC);
    Elf64_Ehdr elf;
    const bool read = descriptor >= 0 && allcore_read_at(descriptor, &elf, sizeof(elf), 0);
    const bool valid = read && memcmp(elf.e_ident, ELFMAG, SELFMAG) == 0 && elf.e_ident[EI_CLASS] == ELFCLASS64 && elf.e_shentsize == sizeof(Elf64_Shdr);
    anchors.fixed = valid && elf.e_type == ET_EXEC;
    Elf64_Shdr* sections = anchors.fixed ? allcore_allocate((size_t)elf.e_shnum * sizeof(Elf64_Shdr)) : NULL;
    const bool listed = sections != NULL && allcore_read_at(descriptor, sections, (size_t)elf.e_shnum * sizeof(Elf64_Shdr), elf.e_shoff);
    for (int s = 0; listed && s < elf.e_shnum; s += 1)
    {
        const Elf64_Shdr* table = &sections[s];
        if (table->sh_type == SHT_SYMTAB && table->sh_link < elf.e_shnum && table->sh_entsize == sizeof(Elf64_Sym))
        {
            const Elf64_Shdr* strings = &sections[table->sh_link];
            Elf64_Sym* symbols = allcore_allocate(table->sh_size);
            char* names = allcore_allocate(strings->sh_size + 1);
            if (allcore_read_at(descriptor, symbols, table->sh_size, table->sh_offset) && allcore_read_at(descriptor, names, strings->sh_size, strings->sh_offset))
            {
                size_t capacities[BUSTER_ALLCORE_ANCHORS] = { 0 };
                const size_t count = table->sh_size / sizeof(Elf64_Sym);
                for (size_t i = 0; i < count; i += 1)
                {
                    const Elf64_Sym* symbol = &symbols[i];
                    const bool function = ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && symbol->st_size > 0 && symbol->st_name < strings->sh_size;
                    for (int a = 0; a < BUSTER_ALLCORE_ANCHORS && function; a += 1)
                    {
                        const char* name = names + symbol->st_name;
                        if (strncmp(name, allcore_anchor_prefixes[a], strlen(allcore_anchor_prefixes[a])) == 0)
                        {
                            anchors.ranges[a] = allcore_grow(anchors.ranges[a], &capacities[a], anchors.counts[a] + 1, sizeof(Range));
                            anchors.ranges[a][anchors.counts[a]].start = symbol->st_value;
                            anchors.ranges[a][anchors.counts[a]].end = symbol->st_value + symbol->st_size;
                            anchors.counts[a] += 1;
                        }
                    }
                }
            }
            free(symbols);
            free(names);
        }
    }
    for (int a = 0; a < BUSTER_ALLCORE_ANCHORS; a += 1)
    {
        if (anchors.counts[a] > 0)
        {
            qsort(anchors.ranges[a], anchors.counts[a], sizeof(Range), allcore_compare_ranges);
        }
    }
    free(sections);
    if (descriptor >= 0)
    {
        close(descriptor);
    }
    return anchors;
}

// Whether `ip` is in one of anchor `a`'s functions.
static bool allcore_anchor_holds(const Anchors* anchors, int a, uint64_t ip)
{
    const Range* ranges = anchors->ranges[a];
    size_t low = 0;
    size_t high = anchors->counts[a];
    while (low < high)
    {
        const size_t middle = low + (high - low) / 2;
        const bool before = ranges[middle].start <= ip;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low > 0 && ip < ranges[low - 1].end;
}

// ---------------------------------------------------------------------------------------------------------------------
// Buckets: each sample's deltas in the run whose span on its CPU holds its time (from the run's start to its reap),
// and in it in the stage its time falls in: `startup` before the stage clock's start, a stage of the run's own report,
// `exit` after the last stage's end until the run's parent has its status, `teardown` after that. The clock's start
// on the samples' clock is the median of the anchors' estimates (the first sample in each anchor's functions, less the
// stages that end before its stage), or for a run without any the run's start and the other runs' median distance.

typedef struct RunBuckets RunBuckets;
struct RunBuckets
{
    bool used;
    int count;
    int64_t ends[BUSTER_ALLCORE_MAX_STAGES];
    char names[BUSTER_ALLCORE_MAX_STAGES + 3][BUSTER_ALLCORE_NAME_BYTES];
    // Per stage: the report's stages, then startup, exit and teardown (count, count + 1, count + 2).
    double sums[BUSTER_ALLCORE_MAX_STAGES + 3][BUSTER_ALLCORE_GROUP_EVENTS];
    int64_t clock_ns;
    int anchors;
    double spread_ms;
};

typedef struct BucketSummary BucketSummary;
struct BucketSummary
{
    int used;
    int without_anchor;
    double distance_ms;
    double widest_spread_ms;
    double outside[BUSTER_ALLCORE_GROUP_EVENTS];
};

static int allcore_compare_int64(const void* left, const void* right)
{
    const int64_t a = *(const int64_t*)left;
    const int64_t b = *(const int64_t*)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

// The first of a CPU's samples at or after `time_ns`.
static size_t allcore_samples_from(const Sample* samples, size_t count, int64_t time_ns)
{
    size_t low = 0;
    size_t high = count;
    while (low < high)
    {
        const size_t middle = low + (high - low) / 2;
        const bool before = samples[middle].time_ns < time_ns;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low;
}

// The buckets of a cohort's runs (`buckets`, workers * loops of them) from each run's report and its CPU's samples
// (`groups[w]` is worker w's CPU's); the samples of no run are `outside`.
static BucketSummary allcore_buckets_compute(const Cohort* cohort, const Report* reports, const SampleGroup* groups, const Anchors* anchors, RunBuckets* buckets)
{
    BucketSummary summary = { 0 };
    const size_t runs = (size_t)cohort->workers * (size_t)cohort->loops;
    double* distances = allcore_allocate(runs * sizeof(double));
    size_t distance_count = 0;
    for (size_t r = 0; r < runs; r += 1)
    {
        const int worker = (int)(r / (size_t)cohort->loops);
        const int run = (int)(r % (size_t)cohort->loops);
        const Row* row = &cohort->rows[r];
        const Report* report = &reports[r];
        RunBuckets* own = &buckets[r];
        memset(own, 0, sizeof(*own));
        own->used = report->found && row->done && row->status == 0 && allcore_interior(run, cohort->loops);
        int64_t end = 0;
        for (int s = 0; s < report->count; s += 1)
        {
            end += (int64_t)(report->ms[s] * 1e6);
            own->ends[s] = end;
            memcpy(own->names[s], report->names[s], BUSTER_ALLCORE_NAME_BYTES);
        }
        own->count = report->count;
        allcore_format(own->names[own->count], BUSTER_ALLCORE_NAME_BYTES, "startup");
        allcore_format(own->names[own->count + 1], BUSTER_ALLCORE_NAME_BYTES, "exit");
        allcore_format(own->names[own->count + 2], BUSTER_ALLCORE_NAME_BYTES, "teardown");
        const SampleGroup* group = &groups[worker];
        const size_t first = allcore_samples_from(group->samples, group->count, row->start_ns);
        int64_t estimates[BUSTER_ALLCORE_ANCHORS];
        int estimate_count = 0;
        for (int a = 0; a < BUSTER_ALLCORE_ANCHORS && own->used; a += 1)
        {
            int before = -1;
            for (int s = 0; s < own->count; s += 1)
            {
                before = strcmp(own->names[s], allcore_anchor_after[a]) == 0 ? s : before;
            }
            bool found = false;
            for (size_t i = first; before >= 0 && !found && i < group->count && group->samples[i].time_ns <= row->reap_ns; i += 1)
            {
                found = allcore_anchor_holds(anchors, a, group->samples[i].ip);
                if (found)
                {
                    estimates[estimate_count] = group->samples[i].time_ns - own->ends[before];
                    estimate_count += 1;
                }
            }
        }
        own->anchors = estimate_count;
        if (estimate_count > 0)
        {
            qsort(estimates, (size_t)estimate_count, sizeof(int64_t), allcore_compare_int64);
            own->clock_ns = estimate_count == 2 ? (estimates[0] + estimates[1]) / 2 : estimates[estimate_count / 2];
            own->spread_ms = (double)(estimates[estimate_count - 1] - estimates[0]) / 1e6;
            distances[distance_count] = (double)(own->clock_ns - row->start_ns) / 1e6;
            distance_count += 1;
            summary.widest_spread_ms = own->spread_ms > summary.widest_spread_ms ? own->spread_ms : summary.widest_spread_ms;
        }
    }
    summary.distance_ms = allcore_median(distances, distance_count);
    for (size_t r = 0; r < runs; r += 1)
    {
        RunBuckets* own = &buckets[r];
        if (own->used && own->anchors == 0)
        {
            own->clock_ns = cohort->rows[r].start_ns + (int64_t)(summary.distance_ms * 1e6);
            summary.without_anchor += 1;
        }
        summary.used += own->used;
    }
    // Each sample in its run and stage; the samples of no run are outside.
    for (int worker = 0; worker < cohort->workers; worker += 1)
    {
        const SampleGroup* group = &groups[worker];
        size_t next = 0;
        for (int run = 0; run < cohort->loops; run += 1)
        {
            const size_t r = (size_t)worker * (size_t)cohort->loops + (size_t)run;
            const Row* row = &cohort->rows[r];
            RunBuckets* own = &buckets[r];
            const size_t first = allcore_samples_from(group->samples, group->count, row->start_ns);
            for (size_t i = next; i < first; i += 1)
            {
                for (int e = 0; e < BUSTER_ALLCORE_GROUP_EVENTS; e += 1)
                {
                    summary.outside[e] += (double)group->samples[i].delta[e];
                }
            }
            size_t i = first;
            while (i < group->count && group->samples[i].time_ns <= row->reap_ns)
            {
                const Sample* sample = &group->samples[i];
                const int64_t since = sample->time_ns - own->clock_ns;
                int stage = own->count;
                if (since >= 0)
                {
                    stage = sample->time_ns <= row->exit_ns ? own->count + 1 : own->count + 2;
                    bool found = false;
                    for (int s = 0; s < own->count && !found; s += 1)
                    {
                        found = since < own->ends[s];
                        stage = found ? s : stage;
                    }
                }
                for (int e = 0; e < BUSTER_ALLCORE_GROUP_EVENTS; e += 1)
                {
                    own->sums[stage][e] += (double)sample->delta[e];
                }
                i += 1;
            }
            next = i;
        }
        for (size_t i = next; i < group->count; i += 1)
        {
            for (int e = 0; e < BUSTER_ALLCORE_GROUP_EVENTS; e += 1)
            {
                summary.outside[e] += (double)group->samples[i].delta[e];
            }
        }
    }
    free(distances);
    return summary;
}

static void allcore_buckets_write(const Cohort* cohort, const RunBuckets* buckets, const BucketSummary* summary, const SampleGroup* groups, const char* path)
{
    FILE* out = fopen(path, "w");
    if (out == NULL)
    {
        allcore_fail("can't write %s: %s", path, strerror(errno));
    }
    size_t samples = 0;
    uint64_t lost = 0;
    int full = 0;
    int multiplexed = 0;
    for (int worker = 0; worker < cohort->workers; worker += 1)
    {
        samples += groups[worker].count;
        lost += groups[worker].lost;
        full += groups[worker].full;
        multiplexed += groups[worker].multiplexed;
    }
    fprintf(out, "# buckets\t%s\tround\t%d\tused\t%d\truns\t%d\tsamples\t%zu\tlost\t%llu\tfull\t%d\tmultiplexed\t%d\tclock_after_start_ms\t%.3f\twidest_spread_ms\t%.3f\twithout_anchor\t%d\n",
            cohort->label, cohort->round, summary->used, cohort->workers * cohort->loops, samples, (unsigned long long)lost, full, multiplexed, summary->distance_ms, summary->widest_spread_ms,
            summary->without_anchor);
    fprintf(out, "worker\trun\tstage\tcycles\tcycles:u\tcycles:k\n");
    fprintf(out, "-\t-\toutside\t%.0f\t%.0f\t%.0f\n", summary->outside[0], summary->outside[1], summary->outside[2]);
    for (int worker = 0; worker < cohort->workers; worker += 1)
    {
        for (int run = 0; run < cohort->loops; run += 1)
        {
            const RunBuckets* own = &buckets[(size_t)worker * (size_t)cohort->loops + (size_t)run];
            for (int s = 0; own->used && s < own->count + 3; s += 1)
            {
                fprintf(out, "%d\t%d\t%s\t%.0f\t%.0f\t%.0f\n", worker, run, own->names[s], own->sums[s][0], own->sums[s][1], own->sums[s][2]);
            }
        }
    }
    fclose(out);
}

// ---------------------------------------------------------------------------------------------------------------------
// The parts

// The cohort of round `round` with `label`'s workers: one on --one's CPU or one each on --eight's.
static Cohort allcore_cohort_plan(const Options* options, int label, int round, int loops, const char* part)
{
    Cohort cohort = { 0 };
    cohort.label = allcore_labels[label];
    cohort.round = round;
    cohort.workers = label == 0 ? 1 : options->eight_count;
    for (int worker = 0; worker < cohort.workers; worker += 1)
    {
        cohort.cpus[worker] = label == 0 ? options->cpus_one : options->cpus_eight[worker];
    }
    cohort.loops = loops;
    cohort.command = options->command;
    cohort.command_count = options->command_count;
    char directory[BUSTER_ALLCORE_PATH_BYTES];
    allcore_format(directory, sizeof(directory), "%s/%s/r%d.%s", options->out, part, round, cohort.label);
    allcore_make_directory(directory);
    allcore_format(cohort.report_pattern, sizeof(cohort.report_pattern), "%s/w%%w.i%%i.txt", directory);
    return cohort;
}

// The msr PMU's event `name` (its sysfs `event=0xNN`), or false where the host has none.
static bool allcore_msr_counter(const char* name, Counter* counter)
{
    bool found = false;
    FILE* type_file = fopen("/sys/bus/event_source/devices/msr/type", "r");
    char path[256];
    allcore_format(path, sizeof(path), "/sys/bus/event_source/devices/msr/events/%s", name);
    FILE* event_file = fopen(path, "r");
    unsigned type = 0;
    unsigned long long config = 0;
    if (type_file != NULL && event_file != NULL && fscanf(type_file, "%u", &type) == 1 && fscanf(event_file, "event=%llx", &config) == 1)
    {
        allcore_format(counter->name, sizeof(counter->name), "%s", name);
        counter->type = type;
        counter->config = config;
        counter->mode = 'a';
        found = true;
    }
    if (type_file != NULL)
    {
        fclose(type_file);
    }
    if (event_file != NULL)
    {
        fclose(event_file);
    }
    return found;
}

static void allcore_part_check(const Options* options)
{
    if (options->command_count < 1)
    {
        allcore_fail("no command after --");
    }
}

// The counted part: --rounds (6) rounds of a cohort of one worker and one of eight, --loops (6) runs a worker.
static int allcore_counted(const Options* options)
{
    allcore_part_check(options);
    Counter counters[BUSTER_ALLCORE_MAX_COUNTERS] = {
        {
            .name = "cycles",
            .type = PERF_TYPE_HARDWARE,
            .config = PERF_COUNT_HW_CPU_CYCLES,
            .mode = 'u',
        },
        {
            .name = "instructions",
            .type = PERF_TYPE_HARDWARE,
            .config = PERF_COUNT_HW_INSTRUCTIONS,
            .mode = 'u',
        },
        {
            .name = "cycles",
            .type = PERF_TYPE_HARDWARE,
            .config = PERF_COUNT_HW_CPU_CYCLES,
            .mode = 'k',
        },
        {
            .name = "instructions",
            .type = PERF_TYPE_HARDWARE,
            .config = PERF_COUNT_HW_INSTRUCTIONS,
            .mode = 'k',
        },
    };
    int counter_count = 4;
    if (allcore_msr_counter("aperf", &counters[counter_count]) && allcore_msr_counter("mperf", &counters[counter_count + 1]))
    {
        counter_count += 2;
    }
    const int rounds = options->rounds > 0 ? options->rounds : 6;
    int result = 0;
    for (int round = 0; round < rounds; round += 1)
    {
        for (int k = 0; k < 2; k += 1)
        {
            const int label = (k + round) % 2;
            Cohort cohort = allcore_cohort_plan(options, label, round, options->loops, "counted");
            cohort.counters = counters;
            cohort.counter_count = counter_count;
            const Snapshot before = allcore_strays_take();
            const bool fine = allcore_cohort_run(&cohort);
            const Snapshot after = allcore_strays_take();
            char path[BUSTER_ALLCORE_PATH_BYTES];
            allcore_format(path, sizeof(path), "%s/counted/r%d.%s.rows.tsv", options->out, round, cohort.label);
            allcore_rows_write(&cohort, path);
            allcore_format(path, sizeof(path), "%s/counted/r%d.%s.strays.txt", options->out, round, cohort.label);
            const int strays = allcore_strays_write(&before, &after, path);
            fprintf(stderr, "allcore counted: round %d, %s: %s, %d strays\n", round, cohort.label, fine ? "done" : "a run failed", strays);
            result |= fine ? 0 : 1;
            free(before.items);
            free(after.items);
            munmap(cohort.rows, (size_t)cohort.workers * (size_t)cohort.loops * sizeof(Row));
        }
    }
    return result;
}

// The sampled part: --rounds (6) rounds of a cohort of one worker (--loops-one, 6 runs) and one of eight (--loops-eight,
// 5), each under a sampled group a worker's CPU.
static int allcore_sampled(const Options* options)
{
    allcore_part_check(options);
    char executable[BUSTER_ALLCORE_PATH_BYTES];
    allcore_executable_find(options->command[0], executable, sizeof(executable));
    const Anchors anchors = allcore_symbols_load(executable);
    if (!anchors.fixed || anchors.counts[0] == 0 || anchors.counts[1] == 0 || anchors.counts[2] == 0)
    {
        allcore_fail("%s isn't an executable at a fixed address with the anchors' functions (parse_, check_input, tier0_generate)", executable);
    }
    const int rounds = options->rounds > 0 ? options->rounds : 6;
    int result = 0;
    for (int round = 0; round < rounds; round += 1)
    {
        for (int k = 0; k < 2; k += 1)
        {
            const int label = (k + round) % 2;
            Cohort cohort = allcore_cohort_plan(options, label, round, label == 0 ? options->loops_one : options->loops_eight, "sampled");
            SampleGroup groups[BUSTER_ALLCORE_MAX_WORKERS];
            allcore_groups_open(groups, cohort.cpus, cohort.workers, options->period, options->buffer_mb);
            const Snapshot before = allcore_strays_take();
            allcore_groups_switch(groups, cohort.workers, true);
            const bool fine = allcore_cohort_run(&cohort);
            allcore_groups_switch(groups, cohort.workers, false);
            const Snapshot after = allcore_strays_take();
            allcore_groups_drain(groups, cohort.workers);
            const size_t runs = (size_t)cohort.workers * (size_t)cohort.loops;
            Report* reports = allcore_allocate(runs * sizeof(Report));
            for (size_t r = 0; r < runs; r += 1)
            {
                char path[BUSTER_ALLCORE_PATH_BYTES];
                allcore_substitute(path, sizeof(path), cohort.report_pattern, (int)(r / (size_t)cohort.loops), (int)(r % (size_t)cohort.loops), cohort.label, round);
                reports[r] = allcore_report_read(path);
            }
            RunBuckets* buckets = allcore_allocate(runs * sizeof(RunBuckets));
            const BucketSummary summary = allcore_buckets_compute(&cohort, reports, groups, &anchors, buckets);
            char path[BUSTER_ALLCORE_PATH_BYTES];
            allcore_format(path, sizeof(path), "%s/sampled/r%d.%s.rows.tsv", options->out, round, cohort.label);
            allcore_rows_write(&cohort, path);
            allcore_format(path, sizeof(path), "%s/sampled/r%d.%s.buckets.tsv", options->out, round, cohort.label);
            allcore_buckets_write(&cohort, buckets, &summary, groups, path);
            allcore_format(path, sizeof(path), "%s/sampled/r%d.%s.strays.txt", options->out, round, cohort.label);
            const int strays = allcore_strays_write(&before, &after, path);
            uint64_t lost = 0;
            int full = 0;
            int multiplexed = 0;
            for (int g = 0; g < cohort.workers; g += 1)
            {
                lost += groups[g].lost;
                full += groups[g].full;
                multiplexed += groups[g].multiplexed;
            }
            const bool complete = fine && lost == 0 && full == 0 && multiplexed == 0 && summary.used > 0;
            fprintf(stderr, "allcore sampled: round %d, %s: %s, %d runs used, %d without an anchor, %llu samples lost, %d rings full, %d groups multiplexed, %d strays\n", round,
                    cohort.label, fine ? "done" : "a run failed", summary.used, summary.without_anchor, (unsigned long long)lost, full, multiplexed, strays);
            result |= complete ? 0 : 1;
            free(buckets);
            free(reports);
            free(before.items);
            free(after.items);
            allcore_groups_close(groups, cohort.workers);
            munmap(cohort.rows, runs * sizeof(Row));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Reading back

typedef struct ReadRow ReadRow;
struct ReadRow
{
    int worker;
    int run;
    int status;
    int64_t start_ns;
    int64_t exit_ns;
    int64_t reap_ns;
    double peak_kb;
    double faults;
    double counters[BUSTER_ALLCORE_MAX_COUNTERS];
};

typedef struct ReadCohort ReadCohort;
struct ReadCohort
{
    int workers;
    int loops;
    int64_t start_ns;
    char counter_names[BUSTER_ALLCORE_MAX_COUNTERS][BUSTER_ALLCORE_NAME_BYTES];
    int counter_count;
    ReadRow* rows;
    size_t count;
    bool found;
};

// A rows file, as allcore_rows_write writes it; `found` is false where there is none.
static ReadCohort allcore_rows_read(const char* path)
{
    ReadCohort cohort = { 0 };
    FILE* file = fopen(path, "r");
    char* line = allcore_allocate(BUSTER_ALLCORE_LINE_BYTES);
    size_t capacity = 0;
    int columns[BUSTER_ALLCORE_MAX_COUNTERS];
    while (file != NULL && fgets(line, BUSTER_ALLCORE_LINE_BYTES, file) != NULL)
    {
        allcore_chomp(line);
        char* fields[64];
        const int count = allcore_split_tabs(line, fields, 64);
        if (strncmp(line, "# cohort", 8) == 0 && count >= 6)
        {
            cohort.workers = atoi(fields[3]);
            cohort.start_ns = strtoll(fields[5], NULL, 10);
        }
        else if (strcmp(fields[0], "label") == 0)
        {
            for (int c = 20; c < count && strcmp(fields[c], "counted") != 0 && cohort.counter_count < BUSTER_ALLCORE_MAX_COUNTERS; c += 1)
            {
                allcore_format(cohort.counter_names[cohort.counter_count], BUSTER_ALLCORE_NAME_BYTES, "%s", fields[c]);
                columns[cohort.counter_count] = c;
                cohort.counter_count += 1;
            }
        }
        else if (count >= 20)
        {
            cohort.rows = allcore_grow(cohort.rows, &capacity, cohort.count + 1, sizeof(ReadRow));
            ReadRow* row = &cohort.rows[cohort.count];
            row->worker = atoi(fields[1]);
            row->run = atoi(fields[3]);
            row->start_ns = strtoll(fields[4], NULL, 10);
            row->exit_ns = strtoll(fields[5], NULL, 10);
            row->reap_ns = strtoll(fields[6], NULL, 10);
            row->status = atoi(fields[7]);
            row->peak_kb = atof(fields[10]);
            row->faults = atof(fields[11]) + atof(fields[18]);
            for (int c = 0; c < cohort.counter_count; c += 1)
            {
                row->counters[c] = columns[c] < count ? atof(fields[columns[c]]) : -1.0;
            }
            cohort.loops = row->run + 1 > cohort.loops ? row->run + 1 : cohort.loops;
            cohort.count += 1;
        }
    }
    cohort.found = file != NULL && cohort.count > 0;
    if (file != NULL)
    {
        fclose(file);
    }
    free(line);
    return cohort;
}

static int allcore_counter_index(const ReadCohort* cohort, const char* name)
{
    int index = -1;
    for (int c = 0; c < cohort->counter_count; c += 1)
    {
        index = index < 0 && strcmp(cohort->counter_names[c], name) == 0 ? c : index;
    }
    return index;
}

// A cohort's rate in MB/s: each worker's runs' bytes over its own span from the cohort's start, summed (the record's).
static double allcore_cohort_rate(const ReadCohort* cohort, double bytes)
{
    int64_t worker_reap[BUSTER_ALLCORE_MAX_WORKERS] = { 0 };
    int worker_runs[BUSTER_ALLCORE_MAX_WORKERS] = { 0 };
    for (size_t r = 0; r < cohort->count; r += 1)
    {
        const ReadRow* row = &cohort->rows[r];
        const int w = row->worker >= 0 && row->worker < BUSTER_ALLCORE_MAX_WORKERS ? row->worker : 0;
        worker_reap[w] = row->reap_ns > worker_reap[w] ? row->reap_ns : worker_reap[w];
        worker_runs[w] += 1;
    }
    double rate = 0.0;
    for (int w = 0; w < cohort->workers && w < BUSTER_ALLCORE_MAX_WORKERS; w += 1)
    {
        rate += worker_reap[w] > cohort->start_ns ? (double)worker_runs[w] * bytes / 1e6 / ((double)(worker_reap[w] - cohort->start_ns) / 1e9) : 0.0;
    }
    return rate;
}

// ---------------------------------------------------------------------------------------------------------------------
// table

// One worker count's figures: the counted rounds' medians over the interior runs, the stage reports' medians, and the
// sampled buckets' means a used run by group.
typedef struct Figures Figures;
struct Figures
{
    int workers;
    int rounds;
    size_t runs;
    size_t used;
    size_t failed;
    double mb_per_s;
    double wall_ms;
    double user_cycles;
    double kernel_cycles;
    double user_instructions;
    double peak_kb;
    double faults;
    double ghz;
    int huge_pages_system;
    int huge_pages_busy;
    int huge_pages_off;
    int strays;
    double group_ms[BUSTER_ALLCORE_MAX_GROUPS];
    double group_faults[BUSTER_ALLCORE_MAX_GROUPS];
    double sampled[BUSTER_ALLCORE_MAX_GROUPS][BUSTER_ALLCORE_GROUP_EVENTS];
    double sampled_total[BUSTER_ALLCORE_GROUP_EVENTS];
    int sampled_used;
    int sampled_rounds;
    int sampled_without_anchor;
    int sampled_strays;
    double user_by_group[BUSTER_ALLCORE_MAX_GROUPS];
    double kernel_by_group[BUSTER_ALLCORE_MAX_GROUPS];
};

static int allcore_strays_count(const char* path)
{
    int count = 0;
    FILE* file = fopen(path, "r");
    char line[512];
    while (file != NULL && fgets(line, sizeof(line), file) != NULL)
    {
        count += line[0] != '\n';
    }
    if (file != NULL)
    {
        fclose(file);
    }
    return count;
}

// Collects a value a run into a growing list.
typedef struct Values Values;
struct Values
{
    double* items;
    size_t count;
    size_t capacity;
};

static void allcore_values_push(Values* values, double value)
{
    values->items = allcore_grow(values->items, &values->capacity, values->count + 1, sizeof(double));
    values->items[values->count] = value;
    values->count += 1;
}

static double allcore_values_median(Values* values)
{
    return allcore_median(values->items, values->count);
}

// The counted part's figures of `label` (0 one, 1 eight), and the stage medians by group.
static void allcore_figures_counted(const Options* options, int label, Groups* groups, Figures* figures)
{
    Values wall = { 0 };
    Values user = { 0 };
    Values kernel = { 0 };
    Values instructions = { 0 };
    Values peak = { 0 };
    Values faults = { 0 };
    Values ratio = { 0 };
    Values tsc = { 0 };
    Values stage_ms[BUSTER_ALLCORE_MAX_STAGES] = { 0 };
    Values stage_faults[BUSTER_ALLCORE_MAX_STAGES] = { 0 };
    char stage_names[BUSTER_ALLCORE_MAX_STAGES][BUSTER_ALLCORE_NAME_BYTES];
    int stage_count = 0;
    Values rates = { 0 };
    bool more = true;
    for (int round = 0; more; round += 1)
    {
        char path[BUSTER_ALLCORE_PATH_BYTES];
        allcore_format(path, sizeof(path), "%s/counted/r%d.%s.rows.tsv", options->out, round, allcore_labels[label]);
        ReadCohort cohort = allcore_rows_read(path);
        more = cohort.found;
        if (more)
        {
            figures->rounds += 1;
            figures->workers = cohort.workers;
            allcore_format(path, sizeof(path), "%s/counted/r%d.%s.strays.txt", options->out, round, allcore_labels[label]);
            figures->strays += allcore_strays_count(path);
            const int u = allcore_counter_index(&cohort, "cycles:u");
            const int k = allcore_counter_index(&cohort, "cycles:k");
            const int i = allcore_counter_index(&cohort, "instructions:u");
            const int a = allcore_counter_index(&cohort, "aperf:a");
            const int m = allcore_counter_index(&cohort, "mperf:a");
            allcore_values_push(&rates, allcore_cohort_rate(&cohort, options->bytes));
            for (size_t r = 0; r < cohort.count; r += 1)
            {
                const ReadRow* row = &cohort.rows[r];
                figures->runs += 1;
                figures->failed += row->status != 0;
                if (row->status == 0 && allcore_interior(row->run, cohort.loops))
                {
                    figures->used += 1;
                    allcore_values_push(&wall, (double)(row->exit_ns - row->start_ns) / 1e6);
                    allcore_values_push(&peak, row->peak_kb);
                    allcore_values_push(&faults, row->faults);
                    allcore_values_push(&user, u >= 0 ? row->counters[u] : 0.0);
                    allcore_values_push(&kernel, k >= 0 ? row->counters[k] : 0.0);
                    allcore_values_push(&instructions, i >= 0 ? row->counters[i] : 0.0);
                    if (a >= 0 && m >= 0 && row->counters[m] > 0.0)
                    {
                        allcore_values_push(&ratio, row->counters[a] / row->counters[m]);
                    }
                    char report_path[BUSTER_ALLCORE_PATH_BYTES];
                    allcore_format(report_path, sizeof(report_path), "%s/counted/r%d.%s/w%d.i%d.txt", options->out, round, allcore_labels[label], row->worker, row->run);
                    const Report report = allcore_report_read(report_path);
                    figures->huge_pages_system += strcmp(report.huge_pages, "system") == 0;
                    figures->huge_pages_busy += strcmp(report.huge_pages, "busy") == 0;
                    figures->huge_pages_off += strcmp(report.huge_pages, "off") == 0;
                    if (report.tsc_mhz > 0.0)
                    {
                        allcore_values_push(&tsc, report.tsc_mhz);
                    }
                    for (int s = 0; s < report.count; s += 1)
                    {
                        int index = -1;
                        for (int known = 0; known < stage_count; known += 1)
                        {
                            index = index < 0 && strcmp(stage_names[known], report.names[s]) == 0 ? known : index;
                        }
                        if (index < 0 && stage_count < BUSTER_ALLCORE_MAX_STAGES)
                        {
                            memcpy(stage_names[stage_count], report.names[s], BUSTER_ALLCORE_NAME_BYTES);
                            index = stage_count;
                            stage_count += 1;
                        }
                        if (index >= 0)
                        {
                            allcore_values_push(&stage_ms[index], report.ms[s]);
                            allcore_values_push(&stage_faults[index], report.faults[s]);
                        }
                    }
                }
            }
        }
        free(cohort.rows);
    }
    // The rounds' median rate: the record took their mean, which its first round's never-read copy of the executable
    // (a compile of 291 ms) pulled down to 73.2 MB/s at one worker, where its rounds' median is 76.0.
    figures->mb_per_s = allcore_values_median(&rates);
    figures->wall_ms = allcore_values_median(&wall);
    figures->user_cycles = allcore_values_median(&user);
    figures->kernel_cycles = allcore_values_median(&kernel);
    figures->user_instructions = allcore_values_median(&instructions);
    figures->peak_kb = allcore_values_median(&peak);
    figures->faults = allcore_values_median(&faults);
    figures->ghz = ratio.count > 0 && tsc.count > 0 ? allcore_values_median(&tsc) * allcore_values_median(&ratio) / 1000.0 : 0.0;
    for (int s = 0; s < stage_count; s += 1)
    {
        const int g = allcore_groups_index(groups, stage_names[s]);
        figures->group_ms[g] += allcore_values_median(&stage_ms[s]);
        figures->group_faults[g] += allcore_values_median(&stage_faults[s]);
        free(stage_ms[s].items);
        free(stage_faults[s].items);
    }
    free(wall.items);
    free(user.items);
    free(kernel.items);
    free(instructions.items);
    free(peak.items);
    free(faults.items);
    free(ratio.items);
    free(tsc.items);
    free(rates.items);
}

// The sampled part's sums of `label` by group, as means a used run over every round.
static void allcore_figures_sampled(const Options* options, int label, Groups* groups, Figures* figures)
{
    double sums[BUSTER_ALLCORE_MAX_GROUPS][BUSTER_ALLCORE_GROUP_EVENTS] = { 0 };
    bool more = true;
    char* line = allcore_allocate(BUSTER_ALLCORE_LINE_BYTES);
    for (int round = 0; more; round += 1)
    {
        char path[BUSTER_ALLCORE_PATH_BYTES];
        allcore_format(path, sizeof(path), "%s/sampled/r%d.%s.buckets.tsv", options->out, round, allcore_labels[label]);
        FILE* file = fopen(path, "r");
        more = file != NULL;
        while (file != NULL && fgets(line, BUSTER_ALLCORE_LINE_BYTES, file) != NULL)
        {
            allcore_chomp(line);
            char* fields[32];
            const int count = allcore_split_tabs(line, fields, 32);
            if (strncmp(fields[0], "# buckets", 9) == 0)
            {
                for (int f = 1; f + 1 < count; f += 1)
                {
                    figures->sampled_used += strcmp(fields[f], "used") == 0 ? atoi(fields[f + 1]) : 0;
                    figures->sampled_without_anchor += strcmp(fields[f], "without_anchor") == 0 ? atoi(fields[f + 1]) : 0;
                }
                figures->sampled_rounds += 1;
            }
            else if (count == 6 && strcmp(fields[0], "worker") != 0 && strcmp(fields[2], "outside") != 0)
            {
                const int g = allcore_groups_index(groups, fields[2]);
                for (int e = 0; e < BUSTER_ALLCORE_GROUP_EVENTS; e += 1)
                {
                    sums[g][e] += atof(fields[3 + e]);
                }
            }
        }
        if (file != NULL)
        {
            fclose(file);
            allcore_format(path, sizeof(path), "%s/sampled/r%d.%s.strays.txt", options->out, round, allcore_labels[label]);
            figures->sampled_strays += allcore_strays_count(path);
        }
    }
    free(line);
    for (int g = 0; g < BUSTER_ALLCORE_MAX_GROUPS && figures->sampled_used > 0; g += 1)
    {
        for (int e = 0; e < BUSTER_ALLCORE_GROUP_EVENTS; e += 1)
        {
            figures->sampled[g][e] = sums[g][e] / (double)figures->sampled_used;
            figures->sampled_total[e] += figures->sampled[g][e];
        }
    }
}

// The stages' cycles: the sampled user cycles scaled to the counted median, and the sampled kernel cycles less the
// sampling's own, its kernel cycles a cycle sampled (the sampled total less the counted median, over the sampled
// cycles) times the stage's (the record's reduction, its tables.sh section 10).
static void allcore_figures_scale(Figures* figures, int group_count)
{
    const double scale = figures->sampled_total[1] > 0.0 ? figures->user_cycles / figures->sampled_total[1] : 0.0;
    const double over = figures->sampled_total[0] > 0.0 ? (figures->sampled_total[2] - figures->kernel_cycles) / figures->sampled_total[0] : 0.0;
    for (int g = 0; g < group_count; g += 1)
    {
        figures->user_by_group[g] = figures->sampled[g][1] * scale;
        figures->kernel_by_group[g] = figures->sampled[g][2] - over * figures->sampled[g][0];
    }
}

static int allcore_table(const Options* options)
{
    if (options->bytes <= 0.0)
    {
        allcore_fail("table needs --bytes, the source's size for MB/s");
    }
    Groups groups = allcore_groups_new();
    Figures* figures = allcore_allocate(2 * sizeof(Figures));
    for (int label = 0; label < 2; label += 1)
    {
        allcore_figures_counted(options, label, &groups, &figures[label]);
        allcore_figures_sampled(options, label, &groups, &figures[label]);
    }
    for (int label = 0; label < 2; label += 1)
    {
        allcore_figures_scale(&figures[label], groups.count);
    }
    char path[BUSTER_ALLCORE_PATH_BYTES];
    allcore_format(path, sizeof(path), "%s/whole.tsv", options->out);
    FILE* whole = fopen(path, "w");
    allcore_format(path, sizeof(path), "%s/table.tsv", options->out);
    FILE* table = fopen(path, "w");
    if (whole == NULL || table == NULL)
    {
        allcore_fail("can't write the tables in %s", options->out);
    }
    for (int pass = 0; pass < 2; pass += 1)
    {
        FILE* out = pass == 0 ? whole : stdout;
        fprintf(out, "label\tworkers\trounds\truns\tused\tfailed\tmb_per_s\twall_ms\tcycles:u\tcycles:k\tinstructions:u\tpeak_kb\tminor_faults\tghz\thuge_pages\tstrays\t"
                     "sampled_rounds\tsampled_used\tsampled_without_anchor\tsampled_strays\n");
        for (int label = 0; label < 2; label += 1)
        {
            const Figures* f = &figures[label];
            fprintf(out, "%s\t%d\t%d\t%zu\t%zu\t%zu\t%.2f\t%.3f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.3f\tsystem=%d busy=%d off=%d\t%d\t%d\t%d\t%d\t%d\n", allcore_labels[label], f->workers, f->rounds, f->runs,
                    f->used, f->failed, f->mb_per_s, f->wall_ms, f->user_cycles, f->kernel_cycles, f->user_instructions, f->peak_kb, f->faults, f->ghz, f->huge_pages_system,
                    f->huge_pages_busy, f->huge_pages_off, f->strays, f->sampled_rounds, f->sampled_used, f->sampled_without_anchor, f->sampled_strays);
        }
        if (pass == 1)
        {
            printf("\n");
        }
    }
    for (int pass = 0; pass < 2; pass += 1)
    {
        FILE* out = pass == 0 ? table : stdout;
        const Figures* one = &figures[0];
        const Figures* eight = &figures[1];
        fprintf(out, "stage\tms 1\tms 8\tuser Mcycles 1\tuser Mcycles 8\ttimes\tkernel Mcycles 1\tkernel Mcycles 8\tfaults 1\tfaults 8\n");
        double totals[8] = { 0 };
        for (int g = 0; g <= groups.count; g += 1)
        {
            const bool total = g == groups.count;
            const double values[8] = {
                total ? totals[0] : one->group_ms[g],
                total ? totals[1] : eight->group_ms[g],
                total ? totals[2] : one->user_by_group[g] / 1e6,
                total ? totals[3] : eight->user_by_group[g] / 1e6,
                total ? totals[4] : one->kernel_by_group[g] / 1e6,
                total ? totals[5] : eight->kernel_by_group[g] / 1e6,
                total ? totals[6] : one->group_faults[g],
                total ? totals[7] : eight->group_faults[g],
            };
            for (int v = 0; v < 8 && !total; v += 1)
            {
                totals[v] += values[v];
            }
            fprintf(out, "%s\t%.2f\t%.2f\t%.1f\t%.1f\t%.2f\t%.1f\t%.1f\t%.0f\t%.0f\n", total ? "total" : groups.names[g], values[0], values[1], values[2], values[3],
                    values[2] > 0.0 ? values[3] / values[2] : 0.0, values[4], values[5], values[6], values[7]);
        }
    }
    fclose(whole);
    fclose(table);
    int result = 0;
    for (int label = 0; label < 2; label += 1)
    {
        result |= figures[label].rounds == 0 || figures[label].sampled_used == 0 || figures[label].failed > 0 ? 1 : 0;
    }
    if (result != 0)
    {
        fprintf(stderr, "allcore table: a part is missing or a run failed (whole.tsv's rounds, failed and sampled_used say which)\n");
    }
    free(figures);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// compare

// A TSV table: its header's names and its lines' fields, by row name (the first field).
typedef struct Table Table;
struct Table
{
    char names[48][BUSTER_ALLCORE_NAME_BYTES];
    int columns;
    char rows[64][BUSTER_ALLCORE_NAME_BYTES];
    double values[64][48];
    int count;
    bool found;
};

static Table allcore_tsv_read(const char* path)
{
    Table table = { 0 };
    FILE* file = fopen(path, "r");
    char* line = allcore_allocate(BUSTER_ALLCORE_LINE_BYTES);
    bool header = true;
    while (file != NULL && fgets(line, BUSTER_ALLCORE_LINE_BYTES, file) != NULL)
    {
        allcore_chomp(line);
        char* fields[48];
        const int count = line[0] == '#' || line[0] == 0 ? 0 : allcore_split_tabs(line, fields, 48);
        if (count > 0 && header)
        {
            for (int c = 0; c < count; c += 1)
            {
                allcore_format(table.names[c], BUSTER_ALLCORE_NAME_BYTES, "%s", fields[c]);
            }
            table.columns = count;
            header = false;
        }
        else if (count > 0 && table.count < 64)
        {
            allcore_format(table.rows[table.count], BUSTER_ALLCORE_NAME_BYTES, "%s", fields[0]);
            for (int c = 1; c < count && c < 48; c += 1)
            {
                table.values[table.count][c] = atof(fields[c]);
            }
            table.count += 1;
        }
    }
    table.found = file != NULL && table.columns > 0;
    if (file != NULL)
    {
        fclose(file);
    }
    free(line);
    return table;
}

static int allcore_tsv_column(const Table* table, const char* name)
{
    int index = -1;
    for (int c = 0; c < table->columns; c += 1)
    {
        index = index < 0 && strcmp(table->names[c], name) == 0 ? c : index;
    }
    return index;
}

static int allcore_tsv_row(const Table* table, const char* name)
{
    int index = -1;
    for (int r = 0; r < table->count; r += 1)
    {
        index = index < 0 && strcmp(table->rows[r], name) == 0 ? r : index;
    }
    return index;
}

typedef struct Verdict Verdict;
struct Verdict
{
    int cells;
    int within;
    double worst;
};

static void allcore_compare_cell(Verdict* verdict, double tolerance, const char* table, const char* row, const char* column, double gauge, double record)
{
    const double difference = record != 0.0 ? (gauge - record) / record * 100.0 : (gauge == 0.0 ? 0.0 : 100.0);
    const bool within = difference <= tolerance && difference >= -tolerance;
    printf("%s\t%s\t%s\t%.2f\t%.2f\t%+.1f%%\t%s\n", table, row, column, gauge, record, difference, within ? "within" : "OUTSIDE");
    verdict->cells += 1;
    verdict->within += within;
    const double magnitude = difference < 0.0 ? -difference : difference;
    verdict->worst = magnitude > verdict->worst ? magnitude : verdict->worst;
}

// The gauge's tables beside the record's: its stage table (inflation-sq.tsv) column by column where both have the
// column, the faults at one worker from its stage medians (stages-sq.w1.tsv, grouped), and the whole compile's figures
// from its baseline (baseline.tsv's sq.w1 and sq.w8); with --record-raw, the rate as the median of its rounds' rates
// from its raw rows (RAW/baseline/rN/sq.wK.rows.tsv), as the gauge's table takes it.
static int allcore_compare(const Options* options)
{
    if (options->record == NULL)
    {
        allcore_fail("compare needs --record TABLES");
    }
    char path[BUSTER_ALLCORE_PATH_BYTES];
    allcore_format(path, sizeof(path), "%s/table.tsv", options->out);
    const Table gauge = allcore_tsv_read(path);
    allcore_format(path, sizeof(path), "%s/whole.tsv", options->out);
    const Table whole = allcore_tsv_read(path);
    allcore_format(path, sizeof(path), "%s/inflation-sq.tsv", options->record);
    const Table record = allcore_tsv_read(path);
    allcore_format(path, sizeof(path), "%s/stages-sq.w1.tsv", options->record);
    const Table stages_one = allcore_tsv_read(path);
    allcore_format(path, sizeof(path), "%s/baseline.tsv", options->record);
    const Table baseline = allcore_tsv_read(path);
    if (!gauge.found || !whole.found || !record.found || !stages_one.found || !baseline.found)
    {
        allcore_fail("compare needs %s/table.tsv and whole.tsv, and the record's inflation-sq.tsv, stages-sq.w1.tsv and baseline.tsv", options->out);
    }
    Verdict verdict = { 0 };
    printf("table\trow\tcolumn\tgauge\trecord\tdifference\tverdict\n");
    for (int r = 0; r < gauge.count; r += 1)
    {
        const int other = allcore_tsv_row(&record, gauge.rows[r]);
        for (int c = 1; c < gauge.columns && other >= 0; c += 1)
        {
            const int column = allcore_tsv_column(&record, gauge.names[c]);
            if (column >= 0)
            {
                allcore_compare_cell(&verdict, options->tolerance, "stages", gauge.rows[r], gauge.names[c], gauge.values[r][c], record.values[other][column]);
            }
        }
    }
    // The faults at one worker: the record's stage medians, grouped as the table groups them.
    Groups groups = allcore_groups_new();
    double faults[BUSTER_ALLCORE_MAX_GROUPS + 1] = { 0 };
    const int fault_column = allcore_tsv_column(&stages_one, "minor_faults");
    for (int r = 0; r < stages_one.count && fault_column >= 0; r += 1)
    {
        if (strcmp(stages_one.rows[r], "process") != 0)
        {
            const int g = allcore_groups_index(&groups, stages_one.rows[r]);
            faults[g] += stages_one.values[r][fault_column];
            faults[BUSTER_ALLCORE_MAX_GROUPS] += stages_one.values[r][fault_column];
        }
    }
    const int gauge_faults = allcore_tsv_column(&gauge, "faults 1");
    for (int r = 0; r < gauge.count && gauge_faults >= 0; r += 1)
    {
        const bool total = strcmp(gauge.rows[r], "total") == 0;
        int g = -1;
        for (int k = 0; k < groups.count && !total; k += 1)
        {
            g = g < 0 && strcmp(groups.names[k], gauge.rows[r]) == 0 ? k : g;
        }
        if (total || g >= 0)
        {
            allcore_compare_cell(&verdict, options->tolerance, "stages", gauge.rows[r], "faults 1", gauge.values[r][gauge_faults], faults[total ? BUSTER_ALLCORE_MAX_GROUPS : g]);
        }
    }
    static const char* const whole_columns[] = {
        "mb_per_s",
        "wall_ms",
        "cycles:u",
        "cycles:k",
        "instructions:u",
        "peak_kb",
        "minor_faults",
    };
    static const char* const record_rows[] = {
        "sq.w1",
        "sq.w8",
    };
    for (int label = 0; label < 2; label += 1)
    {
        const int mine = allcore_tsv_row(&whole, allcore_labels[label]);
        const int theirs = allcore_tsv_row(&baseline, record_rows[label]);
        for (size_t c = 0; c < sizeof(whole_columns) / sizeof(whole_columns[0]) && mine >= 0 && theirs >= 0; c += 1)
        {
            const int a = allcore_tsv_column(&whole, whole_columns[c]);
            const int b = allcore_tsv_column(&baseline, whole_columns[c]);
            const bool rate = strcmp(whole_columns[c], "mb_per_s") == 0;
            if (rate && options->record_raw != NULL)
            {
                // The rounds' median rate, as the gauge takes it, from the record's own rows: its tables have their mean.
                Values rates = { 0 };
                bool more = true;
                for (int round = 0; more; round += 1)
                {
                    char path[BUSTER_ALLCORE_PATH_BYTES];
                    allcore_format(path, sizeof(path), "%s/baseline/r%d/%s.rows.tsv", options->record_raw, round, record_rows[label]);
                    ReadCohort cohort = allcore_rows_read(path);
                    more = cohort.found;
                    if (more)
                    {
                        allcore_values_push(&rates, allcore_cohort_rate(&cohort, options->bytes));
                    }
                    free(cohort.rows);
                }
                if (rates.count == 0 || options->bytes <= 0.0)
                {
                    allcore_fail("compare: --record-raw needs %s/baseline/rN/%s.rows.tsv and --bytes", options->record_raw, record_rows[label]);
                }
                allcore_compare_cell(&verdict, options->tolerance, "whole", allcore_labels[label], "mb_per_s, the rounds' median", whole.values[mine][a], allcore_values_median(&rates));
                free(rates.items);
            }
            else if (a >= 0 && b >= 0)
            {
                allcore_compare_cell(&verdict, options->tolerance, "whole", allcore_labels[label], whole_columns[c], whole.values[mine][a], baseline.values[theirs][b]);
            }
        }
    }
    printf("\n%d of %d cells within %.1f%% of the record; the widest difference %.1f%%\n", verdict.within, verdict.cells, options->tolerance, verdict.worst);
    return verdict.within == verdict.cells ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// self-test: the reductions on fixtures, no counters or processes.

static int allcore_check(bool condition, const char* what)
{
    if (!condition)
    {
        fprintf(stderr, "allcore self-test: %s\n", what);
    }
    return condition ? 0 : 1;
}

static int allcore_self_test(void)
{
    int failures = 0;
    double values[5] = { 5.0, 1.0, 4.0, 2.0, 3.0 };
    failures += allcore_check(allcore_median(values, 5) == 3.0, "the median of five");
    double even[4] = { 4.0, 1.0, 3.0, 2.0 };
    failures += allcore_check(allcore_median(even, 4) == 2.5, "the median of four");
    failures += allcore_check(allcore_median(even, 0) == 0.0, "the median of none");
    failures += allcore_check(allcore_interior(0, 6) == false && allcore_interior(5, 6) == false && allcore_interior(1, 6) && allcore_interior(0, 2), "interior runs");
    failures += allcore_check(strcmp(allcore_group_of("tier0"), "emission") == 0 && strcmp(allcore_group_of("release"), "object") == 0, "the emission's and the object's groups");
    failures += allcore_check(strcmp(allcore_group_of("teardown"), "start") == 0 && strcmp(allcore_group_of("static-data"), "check-declarations") == 0, "the start's group");
    failures += allcore_check(strcmp(allcore_group_of("parse"), "parse") == 0, "a stage its own group");
    Groups groups = allcore_groups_new();
    const int added = allcore_groups_index(&groups, "a-new-stage");
    failures += allcore_check(added == 11 && strcmp(groups.names[10], "start") == 0 && groups.count == 12, "a new group goes last");
    failures += allcore_check(allcore_groups_index(&groups, "teardown") == 10 && allcore_groups_index(&groups, "a-new-stage") == 11, "groups found again");
    char buffer[128];
    allcore_substitute(buffer, sizeof(buffer), "o/%l.r%r.w%w.i%i.o%", 3, 4, "eight", 2);
    failures += allcore_check(strcmp(buffer, "o/eight.r2.w3.i4.o%") == 0, "the patterns' substitutions");
    int cpus[BUSTER_ALLCORE_MAX_WORKERS];
    failures += allcore_check(allcore_cpus_parse("0,1,15", cpus) == 3 && cpus[2] == 15, "a list of CPUs");
    // A report: the top stages and the process's lines, phases and the total left out.
    const char text[] = "lowering-nodes\tevaluated=1\n"
                        "time-stage\tstage\tms\tpercent\tsystem-ms\tminor-faults\tpeak-kb-added\n"
                        "time-stage\tsetup\t0.500\t1.0\t0.000\t10\t100\n"
                        "time-stage\tpreprocess\t2.000\t4.0\t0.000\t20\t100\n"
                        "time-stage\tpreprocess.lex\t1.000\t2.0\n"
                        "time-stage\tparse\t3.000\t6.0\t0.000\t30\t0\n"
                        "time-stage\ttype-table\t0.500\t1.0\t0.000\t1\t0\n"
                        "time-stage\tcheck-input\t4.000\t8.0\t0.000\t40\t0\n"
                        "time-stage\tlower\t1.000\t2.0\t0.000\t5\t0\n"
                        "time-stage\ttier0\t1.000\t2.0\t0.000\t0\t0\n"
                        "time-stage\ttotal\t12.000\t100.0\n"
                        "time-process\ttsc-mhz\t3792.9\n"
                        "time-process\thuge-pages\tbusy\n";
    FILE* file = fmemopen((void*)text, sizeof(text) - 1, "r");
    const Report report = file != NULL ? allcore_report_parse(file) : (Report){ 0 };
    if (file != NULL)
    {
        fclose(file);
    }
    failures += allcore_check(report.count == 7 && strcmp(report.names[1], "preprocess") == 0 && strcmp(report.names[2], "parse") == 0, "a report's top stages");
    failures += allcore_check(report.ms[2] == 3.0 && report.faults[4] == 40.0 && report.tsc_mhz == 3792.9 && strcmp(report.huge_pages, "busy") == 0, "a report's figures");
    // Buckets: one worker, three runs (the middle one used), samples every 0.5 ms from the run's start. The clock starts
    // 1 ms after the run's start: the first parser sample is at 3.5 ms after it, the preprocessor ending 2.5 ms after the
    // clock's start; the first reader sample at 7.0 ms, the type table ending 6.0 ms after it.
    Row rows[3] = { 0 };
    Report* reports = allcore_allocate(3 * sizeof(Report));
    for (int r = 0; r < 3; r += 1)
    {
        reports[r] = report;
    }
    for (int r = 0; r < 3; r += 1)
    {
        rows[r].start_ns = (int64_t)r * 100000000;
        rows[r].exit_ns = rows[r].start_ns + 14000000;
        rows[r].reap_ns = rows[r].start_ns + 15000000;
        rows[r].done = 1;
    }
    Cohort cohort = { 0 };
    cohort.label = "one";
    cohort.workers = 1;
    cohort.loops = 3;
    cohort.rows = rows;
    SampleGroup group = { 0 };
    group.samples = allcore_allocate(400 * sizeof(Sample));
    for (int i = 0; i < 400; i += 1)
    {
        Sample* sample = &group.samples[i];
        sample->time_ns = (int64_t)i * 500000;
        const int64_t since = sample->time_ns % 100000000;
        // Parser addresses from 3.5 ms after a run's start, the reader's from 7.0, user mode throughout but 1 ms of
        // kernel samples at the end of each run.
        sample->ip = since >= 7000000 ? 0x3000 : since >= 3500000 ? 0x1000 : 0x9000;
        const bool kernel = since >= 13000000;
        sample->delta[0] = 100;
        sample->delta[1] = kernel ? 0 : 100;
        sample->delta[2] = kernel ? 100 : 0;
        group.count += 1;
    }
    Anchors anchors = { 0 };
    Range parser = {
        .start = 0x1000,
        .end = 0x2000,
    };
    Range reader = {
        .start = 0x3000,
        .end = 0x4000,
    };
    anchors.ranges[0] = &parser;
    anchors.counts[0] = 1;
    anchors.ranges[1] = &reader;
    anchors.counts[1] = 1;
    anchors.fixed = true;
    failures += allcore_check(allcore_anchor_holds(&anchors, 0, 0x1fff) && !allcore_anchor_holds(&anchors, 0, 0x2000) && !allcore_anchor_holds(&anchors, 2, 0x1000), "anchor ranges");
    RunBuckets* buckets = allcore_allocate(3 * sizeof(RunBuckets));
    const BucketSummary summary = allcore_buckets_compute(&cohort, reports, &group, &anchors, buckets);
    const RunBuckets* middle = &buckets[1];
    failures += allcore_check(summary.used == 1 && buckets[0].used == false && middle->used && middle->anchors == 2, "the used run and its anchors");
    failures += allcore_check(middle->clock_ns == 101000000 && summary.distance_ms == 1.0 && summary.widest_spread_ms == 0.0, "the clock's start from the anchors");
    // Samples at 100.0 ms, 100.5 (startup: before 101.0), then setup [101.0, 101.5): 101.0; preprocess to 103.5: 4;
    // parse to 106.5: 6; type-table to 107.0: 1; check-input to 111.0: 8; lower to 112.0: 2; tier0 to 113.0: 2; exit to
    // 114.0 (the run's exit): 113.0, 113.5, 114.0: 3; teardown to 115.0: 2.
    failures += allcore_check(middle->sums[7][0] == 200.0 && middle->sums[0][0] == 100.0 && middle->sums[1][0] == 400.0 && middle->sums[2][0] == 600.0, "startup, setup, preprocess, parse");
    failures += allcore_check(middle->sums[4][0] == 800.0 && middle->sums[6][0] == 200.0 && middle->sums[8][0] == 300.0 && middle->sums[9][0] == 200.0, "check-input, tier0, exit, teardown");
    failures += allcore_check(middle->sums[8][2] == 300.0 && middle->sums[8][1] == 0.0 && middle->sums[9][2] == 200.0, "kernel cycles at the exit");
    // Runs 0 and 1 hold 31 samples each (from their start to their reap, both ends); run 2 starts after the last sample.
    failures += allcore_check(summary.outside[0] == (400.0 - 62.0) * 100.0, "the samples of no run");
    free(buckets);
    free(reports);
    free(group.samples);
    // The scaling: user cycles to the counted median, kernel cycles less the sampling's own.
    Figures figures = { 0 };
    figures.user_cycles = 2000.0;
    figures.kernel_cycles = 300.0;
    figures.sampled[0][0] = 1000.0;
    figures.sampled[0][1] = 900.0;
    figures.sampled[0][2] = 100.0;
    figures.sampled[1][0] = 400.0;
    figures.sampled[1][1] = 100.0;
    figures.sampled[1][2] = 300.0;
    figures.sampled_total[0] = 1400.0;
    figures.sampled_total[1] = 1000.0;
    figures.sampled_total[2] = 400.0;
    allcore_figures_scale(&figures, 2);
    failures += allcore_check(figures.user_by_group[0] == 1800.0 && figures.user_by_group[1] == 200.0, "user cycles scaled to the counted median");
    const double over = 100.0 / 1400.0;
    // Within a rounding: the compiler may contract the product and the difference into one operation in either place.
    const double first = figures.kernel_by_group[0] - (100.0 - over * 1000.0);
    const double second = figures.kernel_by_group[1] - (300.0 - over * 400.0);
    failures += allcore_check(first * first < 1e-18 && second * second < 1e-18, "kernel cycles less the sampling's");
    if (failures == 0)
    {
        printf("allcore self-test: passed\n");
    }
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// main

int main(int argc, char** argv)
{
    int result = 2;
    const char* command = argc > 1 ? argv[1] : "";
    if (strcmp(command, "self-test") == 0)
    {
        result = allcore_self_test();
    }
    else if (strcmp(command, "counted") == 0 || strcmp(command, "sampled") == 0 || strcmp(command, "table") == 0 || strcmp(command, "compare") == 0)
    {
        const Options options = allcore_options_parse(argc, argv);
        allcore_make_directory(options.out);
        result = strcmp(command, "counted") == 0   ? allcore_counted(&options)
                 : strcmp(command, "sampled") == 0 ? allcore_sampled(&options)
                 : strcmp(command, "table") == 0   ? allcore_table(&options)
                                                   : allcore_compare(&options);
    }
    else
    {
        fprintf(stderr, "usage: allcore counted|sampled --out DIR [options] -- COMMAND... | table --out DIR --bytes N | compare --out DIR --record TABLES | self-test\n"
                        "  COMMAND's %%w is the worker, %%i the run, %%l the cohort (one or eight), %%r the round; docs/commands/all-core.md has the options\n");
    }
    return result;
}
