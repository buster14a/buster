// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// uarch: the runner and reducer of the micro-architecture campaign
// (docs/commands/microarch.md). It drives perf(1) and the compiler as child
// processes and turns their outputs into tab-separated tables. Every run writes its
// raw output first; the reducers only read those files, so a table can always be
// rebuilt from a record.
//
// Layout: the containers (Strings, Values, Table with a hash index), process runs
// (run_process, perf_stat), the run commands (command_time, command_stat,
// command_sweep, command_metrics, command_fanout), the line parsers (csv_split,
// symbols_parse_line, branches_parse_token, memory_parse_line, stages_parse_line),
// the reducers (command_summarize, command_stages, command_symbols,
// command_branches, command_memory, command_topdown), command_generate_generic and
// command_test, which checks every parser against fixtures.
//
// Every run command resumes: a run whose status file exists is skipped, so an
// interrupted step continues where it stopped. Paths and lines longer than the
// fixed buffers are errors, never truncated.

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <math.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_UARCH_PATH_BYTES 4096
#define BUSTER_UARCH_LINE_BYTES 8192
#define BUSTER_UARCH_CSV_FIELDS 7
#define BUSTER_UARCH_MAX_FIELDS 32
#define BUSTER_UARCH_MAX_LISTS 64
#define BUSTER_UARCH_MAX_CPUS 256
#define BUSTER_UARCH_MAX_RUNS 64
#define BUSTER_UARCH_FULL_DUTY 99.0
// A perf stat completion record: "status N wall_ms .. user_ms .. system_ms ..
// maxrss_kb .. failed F", F in its twelfth field.
#define BUSTER_UARCH_STATUS_FAILED_FIELD 11

typedef struct Strings Strings;
struct Strings
{
    char** items;
    size_t count;
    size_t capacity;
};

typedef struct Values Values;
struct Values
{
    double* items;
    size_t count;
    size_t capacity;
};

typedef struct Entry Entry;
struct Entry
{
    char* key;
    Values values;
    Values duty;
};

// Entries in insertion order with an open-addressing index of their positions.
typedef struct Table Table;
struct Table
{
    Entry* items;
    size_t count;
    size_t capacity;
    uint32_t* index;
    size_t index_capacity;
};

typedef struct RunResult RunResult;
struct RunResult
{
    int status;
    double wall_ms;
    struct rusage usage;
};

typedef struct Command Command;
struct Command
{
    const char* label;
    char** argv;
};

// The fields of one perf stat CSV line: value, unit, event, run time, percent
// running, metric value, metric unit.
typedef struct CsvLine CsvLine;
struct CsvLine
{
    char* fields[BUSTER_UARCH_CSV_FIELDS];
    int count;
};

typedef struct SweepSample SweepSample;
struct SweepSample
{
    double user;
    double kernel;
    double cycles;
    double instructions;
    double duty;
    bool valid;
};

typedef struct SymbolLine SymbolLine;
struct SymbolLine
{
    bool valid;
    double period;
    char* event;
    char* symbol;
};

typedef struct BranchEntry BranchEntry;
struct BranchEntry
{
    bool valid;
    bool mispredicted;
    char* site;
    char* symbol;
};

typedef struct MemoryLine MemoryLine;
struct MemoryLine
{
    bool valid;
    bool load;
    bool store;
    double weight;
    char level[256];
    char tlb[256];
    char* symbol;
};

typedef struct StageLine StageLine;
struct StageLine
{
    bool valid;
    char* fields[8];
    int count;
};

typedef struct SymbolTable SymbolTable;
struct SymbolTable
{
    Strings columns;
    Table rows;
};

typedef struct TopdownRow TopdownRow;
struct TopdownRow
{
    double cycles;
    double ipc;
    double retiring;
    double frontend;
    double backend;
    double bad_speculation;
    double backend_memory;
    double backend_cpu;
    double mispredicts_pki;
};

// ---------------------------------------------------------------------------
// Containers and helpers.

static void fail(const char* message)
{
    fprintf(stderr, "uarch: %s\n", message);
    exit(2);
}

static void* checked_realloc(void* pointer, size_t size)
{
    void* result = realloc(pointer, size);
    if (result == NULL)
    {
        fail("out of memory");
    }
    return result;
}

static char* copy_string(const char* text)
{
    size_t length = strlen(text);
    char* result = checked_realloc(NULL, length + 1);
    memcpy(result, text, length + 1);
    return result;
}

// snprintf into a fixed buffer; a result that would not fit is an error.
static void format_into(char* buffer, size_t size, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length >= size)
    {
        fail("a path or line is longer than its buffer");
    }
}

static void strings_add(Strings* strings, const char* text)
{
    if (strings->count == strings->capacity)
    {
        strings->capacity = strings->capacity ? strings->capacity * 2 : 16;
        strings->items = checked_realloc(strings->items, strings->capacity * sizeof(char*));
    }
    strings->items[strings->count] = copy_string(text);
    strings->count += 1;
}

static bool strings_contains(const Strings* strings, const char* text)
{
    bool result = false;
    for (size_t i = 0; i < strings->count && !result; i += 1)
    {
        result = strcmp(strings->items[i], text) == 0;
    }
    return result;
}

static void values_add(Values* values, double value)
{
    if (values->count == values->capacity)
    {
        values->capacity = values->capacity ? values->capacity * 2 : 8;
        values->items = checked_realloc(values->items, values->capacity * sizeof(double));
    }
    values->items[values->count] = value;
    values->count += 1;
}

static int compare_doubles(const void* a, const void* b)
{
    double x = *(const double*)a;
    double y = *(const double*)b;
    return (x > y) - (x < y);
}

// The median of the values that are numbers; NAN when there are none.
static double values_median(const Values* values)
{
    double result = NAN;
    double* sorted = checked_realloc(NULL, (values->count + 1) * sizeof(double));
    size_t count = 0;
    for (size_t i = 0; i < values->count; i += 1)
    {
        if (!isnan(values->items[i]))
        {
            sorted[count] = values->items[i];
            count += 1;
        }
    }
    if (count > 0)
    {
        qsort(sorted, count, sizeof(double), compare_doubles);
        size_t middle = count / 2;
        result = count % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
    }
    free(sorted);
    return result;
}

static double values_extreme(const Values* values, bool maximum)
{
    double result = NAN;
    for (size_t i = 0; i < values->count; i += 1)
    {
        double value = values->items[i];
        if (!isnan(value) && (isnan(result) || (maximum ? value > result : value < result)))
        {
            result = value;
        }
    }
    return result;
}

static uint64_t hash_text(const char* text)
{
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char* p = (const unsigned char*)text; *p != 0; p += 1)
    {
        hash = (hash ^ *p) * 1099511628211ull;
    }
    return hash;
}

static void table_reindex(Table* table)
{
    table->index_capacity = table->index_capacity ? table->index_capacity * 2 : 256;
    free(table->index);
    table->index = checked_realloc(NULL, table->index_capacity * sizeof(uint32_t));
    memset(table->index, 0xff, table->index_capacity * sizeof(uint32_t));
    for (size_t i = 0; i < table->count; i += 1)
    {
        size_t slot = hash_text(table->items[i].key) & (table->index_capacity - 1);
        while (table->index[slot] != UINT32_MAX)
        {
            slot = (slot + 1) & (table->index_capacity - 1);
        }
        table->index[slot] = (uint32_t)i;
    }
}

// The entry of `key`, added empty when it is new. The pointer is valid until the
// next call that adds an entry.
static Entry* table_get(Table* table, const char* key)
{
    if ((table->count + 1) * 2 > table->index_capacity)
    {
        table_reindex(table);
    }
    size_t slot = hash_text(key) & (table->index_capacity - 1);
    while (table->index[slot] != UINT32_MAX && strcmp(table->items[table->index[slot]].key, key) != 0)
    {
        slot = (slot + 1) & (table->index_capacity - 1);
    }
    if (table->index[slot] == UINT32_MAX)
    {
        if (table->count == table->capacity)
        {
            table->capacity = table->capacity ? table->capacity * 2 : 64;
            table->items = checked_realloc(table->items, table->capacity * sizeof(Entry));
        }
        Entry* fresh = &table->items[table->count];
        memset(fresh, 0, sizeof(*fresh));
        fresh->key = copy_string(key);
        table->index[slot] = (uint32_t)table->count;
        table->count += 1;
    }
    return &table->items[table->index[slot]];
}

// The entry's first value, as a running sum.
static void table_add(Table* table, const char* key, double amount)
{
    Entry* entry = table_get(table, key);
    if (entry->values.count == 0)
    {
        values_add(&entry->values, 0);
    }
    entry->values.items[0] += amount;
}

static double table_sum(Table* table, const char* key)
{
    Entry* entry = table_get(table, key);
    return entry->values.count ? entry->values.items[0] : 0;
}

static double now_ms(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (double)time.tv_sec * 1e3 + (double)time.tv_nsec * 1e-6;
}

static double timeval_ms(struct timeval value)
{
    return (double)value.tv_sec * 1e3 + (double)value.tv_usec * 1e-3;
}

static void make_directory(const char* path)
{
    char buffer[BUSTER_UARCH_PATH_BYTES];
    format_into(buffer, sizeof(buffer), "%s", path);
    for (char* p = buffer + 1; *p; p += 1)
    {
        if (*p == '/')
        {
            *p = 0;
            mkdir(buffer, 0755);
            *p = '/';
        }
    }
    if (mkdir(buffer, 0755) != 0 && errno != EEXIST)
    {
        fail(strerror(errno));
    }
}

static bool file_exists(const char* path)
{
    struct stat information;
    return stat(path, &information) == 0;
}

static char* trim(char* text)
{
    while (isspace((unsigned char)*text))
    {
        text += 1;
    }
    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1]))
    {
        length -= 1;
        text[length] = 0;
    }
    return text;
}

// The non-empty lines of a file that don't start with '#', without line ends.
static Strings read_lines(const char* path)
{
    Strings result = { 0 };
    FILE* file = fopen(path, "r");
    if (file != NULL)
    {
        char* line = NULL;
        size_t capacity = 0;
        while (getline(&line, &capacity, file) > 0)
        {
            size_t length = strlen(line);
            while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' || line[length - 1] == ' '))
            {
                length -= 1;
                line[length] = 0;
            }
            if (length > 0 && line[0] != '#')
            {
                strings_add(&result, line);
            }
        }
        free(line);
        fclose(file);
    }
    return result;
}

static FILE* open_input(const char* path)
{
    FILE* file = strcmp(path, "-") == 0 ? stdin : fopen(path, "r");
    if (file == NULL)
    {
        fail(strerror(errno));
    }
    return file;
}

static int split_tabs(char* line, char** fields, int capacity)
{
    int count = 0;
    char* cursor = line;
    bool more = true;
    while (more && count < capacity)
    {
        fields[count] = cursor;
        count += 1;
        char* tab = strchr(cursor, '\t');
        more = tab != NULL;
        if (more)
        {
            *tab = 0;
            cursor = tab + 1;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Processes.

static Strings list_directory(const char* path, const char* suffix);
static void rebuild_table(const char* out, const char* name, const char* suffix, const char* header);

// Runs ARGV with stdout and stderr appended to LOG (kept when LOG is NULL), in the
// C locale so that perf's CSV fields use '.' decimals.
static RunResult run_process(char** argv, const char* log)
{
    RunResult result = { 0 };
    double start = now_ms();
    pid_t child = fork();
    if (child == 0)
    {
        setenv("LC_ALL", "C", 1);
        if (log != NULL)
        {
            int fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0)
            {
                dup2(fd, 1);
                dup2(fd, 2);
                close(fd);
            }
        }
        execvp(argv[0], argv);
        fprintf(stderr, "uarch: exec %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int status = 0;
    if (child < 0 || wait4(child, &status, 0, &result.usage) < 0)
    {
        result.status = -1;
    }
    else
    {
        result.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    result.wall_ms = now_ms() - start;
    return result;
}

// Runs `perf stat -x, --no-big-num -o CSV PERF_ARGS... -- COMMAND...`.
static RunResult perf_stat(const char* csv, const char* log, char** perf_args, int perf_count, char** command, int command_count)
{
    char** argv = checked_realloc(NULL, (size_t)(perf_count + command_count + 8) * sizeof(char*));
    int n = 0;
    argv[n++] = "perf";
    argv[n++] = "stat";
    argv[n++] = "-x,";
    argv[n++] = "--no-big-num";
    argv[n++] = "-o";
    argv[n++] = (char*)csv;
    for (int i = 0; i < perf_count; i += 1)
    {
        argv[n++] = perf_args[i];
    }
    argv[n++] = "--";
    for (int i = 0; i < command_count; i += 1)
    {
        argv[n++] = command[i];
    }
    argv[n] = NULL;
    RunResult result = run_process(argv, log);
    free(argv);
    return result;
}

// Writes TEXT to PATH through a temporary file and a rename, so that PATH is either
// absent or whole: a run's completion record is never half written.
static void write_text_atomic(const char* path, const char* text)
{
    char temporary[BUSTER_UARCH_PATH_BYTES];
    format_into(temporary, sizeof(temporary), "%s.tmp", path);
    FILE* file = fopen(temporary, "w");
    if (file == NULL)
    {
        fail(strerror(errno));
    }
    bool written = fputs(text, file) >= 0;
    written = fclose(file) == 0 && written;
    if (!written || rename(temporary, path) != 0)
    {
        fail(strerror(errno));
    }
}

// The completion record of one perf stat run, written after the run ends: perf's
// status, the run's resources, and whether the run failed (its twelfth field), so
// that a resumed command still reports a failure recorded earlier.
static void write_status(const char* path, RunResult run, bool failed)
{
    char line[BUSTER_UARCH_LINE_BYTES];
    format_into(line, sizeof(line), "status\t%d\twall_ms\t%.3f\tuser_ms\t%.3f\tsystem_ms\t%.3f\tmaxrss_kb\t%ld\tfailed\t%d\n", run.status, run.wall_ms,
                timeval_ms(run.usage.ru_utime), timeval_ms(run.usage.ru_stime), run.usage.ru_maxrss, failed);
    write_text_atomic(path, line);
}

// The number in FIELD of a completion record's first line (a time record's status
// is its third field, a fanout record's failed copies its fourth, a perf stat
// record's failure its twelfth). A record without the field, written before it
// existed, gives its second field, perf's status.
static int recorded_status(const char* path, int field)
{
    int result = -1;
    Strings lines = read_lines(path);
    if (lines.count > 0)
    {
        char* fields[16];
        int count = split_tabs(lines.items[0], fields, 16);
        result = count > field ? atoi(fields[field]) : (count > 1 ? atoi(fields[1]) : -1);
    }
    return result;
}

static int find_separator(int argc, char** argv, int from)
{
    int result = -1;
    for (int i = from; i < argc && result < 0; i += 1)
    {
        if (strcmp(argv[i], "--") == 0)
        {
            result = i;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Run commands.

// time OUT ROUNDS --list LABEL ARG... [--list LABEL ARG...]...: whole processes in
// a rotating order, with wall time and wait4's rusage per run. Each run's row is
// its completion record, OUT/logs/LABEL.rN.time, written after wait4; OUT/time.tsv
// is rebuilt from the records. Returns 1 when any run, done now or before, failed.
static int command_time(int argc, char** argv)
{
    int result = 0;
    Command commands[BUSTER_UARCH_MAX_LISTS];
    int command_count = 0;
    for (int i = 4; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--list") == 0 && i + 1 < argc)
        {
            if (command_count == BUSTER_UARCH_MAX_LISTS)
            {
                fail("too many --list commands");
            }
            commands[command_count].label = argv[i + 1];
            commands[command_count].argv = &argv[i + 2];
            command_count += 1;
            argv[i] = NULL;
            i += 1;
        }
    }
    if (argc < 4 || command_count == 0)
    {
        fprintf(stderr, "usage: uarch time OUT ROUNDS --list LABEL ARG... [--list LABEL ARG...]...\n");
        result = 1;
    }
    else
    {
        const char* out = argv[2];
        int rounds = atoi(argv[3]);
        char path[BUSTER_UARCH_PATH_BYTES];
        format_into(path, sizeof(path), "%s/logs", out);
        make_directory(path);
        for (int round = 0; round < rounds; round += 1)
        {
            for (int k = 0; k < command_count; k += 1)
            {
                Command* command = &commands[(round + k) % command_count];
                char log[BUSTER_UARCH_PATH_BYTES];
                char record[BUSTER_UARCH_PATH_BYTES];
                format_into(log, sizeof(log), "%s/logs/%s.r%d.log", out, command->label, round);
                format_into(record, sizeof(record), "%s/logs/%s.r%d.time", out, command->label, round);
                if (file_exists(record))
                {
                    result |= recorded_status(record, 2) != 0;
                }
                else
                {
                    // A log without a record is an interrupted run: start it again.
                    unlink(log);
                    RunResult run = run_process(command->argv, log);
                    char line[BUSTER_UARCH_LINE_BYTES];
                    format_into(line, sizeof(line), "%s\t%d\t%d\t%.3f\t%.3f\t%.3f\t%ld\t%ld\t%ld\t%ld\t%ld\n", command->label, round, run.status, run.wall_ms,
                                timeval_ms(run.usage.ru_utime), timeval_ms(run.usage.ru_stime), run.usage.ru_maxrss, run.usage.ru_minflt,
                                run.usage.ru_majflt, run.usage.ru_nvcsw, run.usage.ru_nivcsw);
                    write_text_atomic(record, line);
                    result |= run.status != 0;
                }
            }
        }
        rebuild_table(out, "time.tsv", ".time", "label\tround\tstatus\twall_ms\tuser_ms\tsystem_ms\tmaxrss_kb\tminflt\tmajflt\tnvcsw\tnivcsw\n");
    }
    return result;
}

// stat OUT LABEL RUNS PERF-ARG... -- ARG...: perf stat RUNS times, to
// OUT/stat/LABEL.rN.{csv,log,status}.
static int command_stat(int argc, char** argv)
{
    int result = 0;
    int separator = find_separator(argc, argv, 5);
    if (argc < 7 || separator < 0)
    {
        fprintf(stderr, "usage: uarch stat OUT LABEL RUNS PERF-ARG... -- ARG...\n");
        result = 1;
    }
    else
    {
        char directory[BUSTER_UARCH_PATH_BYTES];
        format_into(directory, sizeof(directory), "%s/stat", argv[2]);
        make_directory(directory);
        int runs = atoi(argv[4]);
        for (int run = 0; run < runs; run += 1)
        {
            char csv[BUSTER_UARCH_PATH_BYTES];
            char log[BUSTER_UARCH_PATH_BYTES];
            char status[BUSTER_UARCH_PATH_BYTES];
            format_into(csv, sizeof(csv), "%s/%s.r%d.csv", directory, argv[3], run);
            format_into(log, sizeof(log), "%s/%s.r%d.log", directory, argv[3], run);
            format_into(status, sizeof(status), "%s/%s.r%d.status", directory, argv[3], run);
            if (file_exists(status))
            {
                result |= recorded_status(status, BUSTER_UARCH_STATUS_FAILED_FIELD) != 0;
            }
            else
            {
                unlink(log);
                RunResult outcome = perf_stat(csv, log, &argv[5], separator - 5, &argv[separator + 1], argc - separator - 1);
                write_status(status, outcome, outcome.status != 0);
                result |= outcome.status != 0;
            }
        }
    }
    return result;
}

// Splits one perf stat CSV line in place. An event spelled with terms
// ("cpu/EVENT,cmask=0x8/u") keeps its commas in perf's output, so a line of more
// than seven fields has its extra fields joined back into the event.
static CsvLine csv_split(char* line)
{
    CsvLine result = { 0 };
    char* fields[BUSTER_UARCH_MAX_FIELDS];
    int count = 0;
    char* cursor = line;
    bool more = true;
    while (more && count < BUSTER_UARCH_MAX_FIELDS)
    {
        fields[count] = cursor;
        count += 1;
        char* comma = strchr(cursor, ',');
        more = comma != NULL;
        if (more)
        {
            *comma = 0;
            cursor = comma + 1;
        }
    }
    int extra = count > BUSTER_UARCH_CSV_FIELDS && strchr(fields[2], '/') != NULL ? count - BUSTER_UARCH_CSV_FIELDS : 0;
    for (int k = 0; k < extra; k += 1)
    {
        // The fields are contiguous in `line`: restore the comma after the event's part.
        fields[2][strlen(fields[2])] = ',';
    }
    for (int i = 0; i < count && result.count < BUSTER_UARCH_CSV_FIELDS; i += 1)
    {
        result.fields[result.count] = fields[i];
        result.count += 1;
        i += i == 2 ? extra : 0;
    }
    return result;
}

static double parse_value(const char* text)
{
    return text[0] == '<' || text[0] == 0 ? NAN : strtod(text, NULL);
}

// True when every counter line of the CSV counted with at least full duty.
static bool csv_complete(const char* path)
{
    Strings lines = read_lines(path);
    bool result = lines.count > 0;
    for (size_t i = 0; i < lines.count; i += 1)
    {
        CsvLine line = csv_split(lines.items[i]);
        if (line.count >= 5 && (line.fields[0][0] == '<' || atof(line.fields[4]) < BUSTER_UARCH_FULL_DUTY))
        {
            result = false;
        }
    }
    return result;
}

// True when perf printed at least one counted line: the workload ran under perf.
// perf refuses an event or metric it can't open (unsupported on the host, uncore
// or system-wide only) before the workload starts, with a nonzero status and no
// counted line; that refusal is a result the tables record, not a failure.
static bool csv_counted(const char* path)
{
    Strings lines = read_lines(path);
    bool result = false;
    for (size_t i = 0; i < lines.count && !result; i += 1)
    {
        CsvLine line = csv_split(lines.items[i]);
        result = line.count >= 5 && line.fields[0][0] != '<';
    }
    return result;
}

// A run failed when it was not refused: perf counted the workload and the
// workload's status (perf's) is nonzero, or perf failed without a record of why.
static bool run_failed(RunResult run, const char* csv)
{
    return run.status != 0 && (csv_counted(csv) || !file_exists(csv));
}

// sweep OUT EVENTS FROM TO RUNS ANCHORS -- ARG...: one perf stat per event and run,
// {ANCHORS,EVENT:u,EVENT:k} as one group; when that group cannot be scheduled whole
// or perf refuses it, {instructions:u,EVENT:u} and {instructions:u,EVENT:k} one
// after the other. Returns 1 when a run failed (run_failed), not when perf refused
// an event.
static int command_sweep(int argc, char** argv)
{
    int result = 0;
    int separator = find_separator(argc, argv, 8);
    if (argc < 10 || separator != 8)
    {
        fprintf(stderr, "usage: uarch sweep OUT EVENTS FROM TO RUNS ANCHORS -- ARG...\n");
        result = 1;
    }
    else
    {
        Strings events = read_lines(argv[3]);
        size_t from = (size_t)atol(argv[4]);
        size_t to = (size_t)atol(argv[5]);
        int runs = atoi(argv[6]);
        const char* anchors = argv[7];
        char directory[BUSTER_UARCH_PATH_BYTES];
        format_into(directory, sizeof(directory), "%s/sweep", argv[2]);
        make_directory(directory);
        to = to > events.count ? events.count : to;
        for (size_t i = from; i < to; i += 1)
        {
            const char* event = events.items[i];
            char name_path[BUSTER_UARCH_PATH_BYTES];
            format_into(name_path, sizeof(name_path), "%s/%04zu.event", directory, i);
            FILE* name = fopen(name_path, "w");
            if (name == NULL)
            {
                fail(strerror(errno));
            }
            fprintf(name, "%s\n", event);
            fclose(name);
            for (int run = 0; run < runs; run += 1)
            {
                char csv[BUSTER_UARCH_PATH_BYTES];
                char log[BUSTER_UARCH_PATH_BYTES];
                char status[BUSTER_UARCH_PATH_BYTES];
                char spec[BUSTER_UARCH_LINE_BYTES];
                format_into(csv, sizeof(csv), "%s/%04zu.r%d.csv", directory, i, run);
                format_into(log, sizeof(log), "%s/%04zu.r%d.log", directory, i, run);
                format_into(status, sizeof(status), "%s/%04zu.r%d.status", directory, i, run);
                if (file_exists(status))
                {
                    result |= recorded_status(status, BUSTER_UARCH_STATUS_FAILED_FIELD) != 0;
                    continue;
                }
                unlink(log);
                format_into(spec, sizeof(spec), "{%s,%s:u,%s:k}", anchors, event, event);
                char* perf_args[2] = { "-e", spec };
                RunResult outcome = perf_stat(csv, log, perf_args, 2, &argv[separator + 1], argc - separator - 1);
                bool failed = run_failed(outcome, csv);
                bool refused = outcome.status != 0 && !failed;
                if (!failed && (refused || !csv_complete(csv)))
                {
                    for (int mode = 0; mode < 2; mode += 1)
                    {
                        char fallback_csv[BUSTER_UARCH_PATH_BYTES];
                        format_into(fallback_csv, sizeof(fallback_csv), "%s/%04zu.r%d.f%d.csv", directory, i, run, mode);
                        format_into(spec, sizeof(spec), "{instructions:u,%s:%s}", event, mode == 0 ? "u" : "k");
                        RunResult fallback = perf_stat(fallback_csv, log, perf_args, 2, &argv[separator + 1], argc - separator - 1);
                        // The fallback's statuses replace the refused or partial group's.
                        outcome.status = mode == 0 || outcome.status == 0 ? fallback.status : outcome.status;
                        failed = failed || run_failed(fallback, fallback_csv);
                    }
                }
                write_status(status, outcome, failed);
                result |= failed;
            }
        }
    }
    return result;
}

// metrics OUT METRICS FROM TO RUNS -- ARG...: one perf stat -M per metric and run.
// Returns 1 when a run failed (run_failed), not when perf refused a metric.
static int command_metrics(int argc, char** argv)
{
    int result = 0;
    int separator = find_separator(argc, argv, 7);
    if (argc < 9 || separator != 7)
    {
        fprintf(stderr, "usage: uarch metrics OUT METRICS FROM TO RUNS -- ARG...\n");
        result = 1;
    }
    else
    {
        Strings metrics = read_lines(argv[3]);
        size_t from = (size_t)atol(argv[4]);
        size_t to = (size_t)atol(argv[5]);
        int runs = atoi(argv[6]);
        char directory[BUSTER_UARCH_PATH_BYTES];
        format_into(directory, sizeof(directory), "%s/metrics", argv[2]);
        make_directory(directory);
        to = to > metrics.count ? metrics.count : to;
        for (size_t i = from; i < to; i += 1)
        {
            char name_path[BUSTER_UARCH_PATH_BYTES];
            format_into(name_path, sizeof(name_path), "%s/%04zu.metric", directory, i);
            FILE* name = fopen(name_path, "w");
            if (name == NULL)
            {
                fail(strerror(errno));
            }
            fprintf(name, "%s\n", metrics.items[i]);
            fclose(name);
            for (int run = 0; run < runs; run += 1)
            {
                char csv[BUSTER_UARCH_PATH_BYTES];
                char log[BUSTER_UARCH_PATH_BYTES];
                char status[BUSTER_UARCH_PATH_BYTES];
                format_into(csv, sizeof(csv), "%s/%04zu.r%d.csv", directory, i, run);
                format_into(log, sizeof(log), "%s/%04zu.r%d.log", directory, i, run);
                format_into(status, sizeof(status), "%s/%04zu.r%d.status", directory, i, run);
                if (file_exists(status))
                {
                    result |= recorded_status(status, BUSTER_UARCH_STATUS_FAILED_FIELD) != 0;
                }
                else
                {
                    unlink(log);
                    char* perf_args[2] = { "-M", metrics.items[i] };
                    RunResult outcome = perf_stat(csv, log, perf_args, 2, &argv[separator + 1], argc - separator - 1);
                    bool failed = run_failed(outcome, csv);
                    write_status(status, outcome, failed);
                    result |= failed;
                }
            }
        }
    }
    return result;
}

// fanout OUT LABEL ROUNDS CPU,CPU,... -- ARG...: one copy of the command per listed
// CPU at once, each pinned to its CPU ("{i}" in an argument becomes the copy's
// index); the batch's wall time and the copies' summed rusage per round. Each
// round's row is its completion record, OUT/logs/LABEL.rN.fanout, written after
// every copy ends; OUT/fanout.tsv is rebuilt from the records.
static int command_fanout(int argc, char** argv)
{
    int result = 0;
    int separator = find_separator(argc, argv, 6);
    if (argc < 8 || separator != 6)
    {
        fprintf(stderr, "usage: uarch fanout OUT LABEL ROUNDS CPU,CPU,... -- ARG...\n");
        result = 1;
    }
    else
    {
        const char* out = argv[2];
        const char* label = argv[3];
        int rounds = atoi(argv[4]);
        int cpus[BUSTER_UARCH_MAX_CPUS];
        int cpu_count = 0;
        char cpu_list[BUSTER_UARCH_LINE_BYTES];
        format_into(cpu_list, sizeof(cpu_list), "%s", argv[5]);
        for (char* token = strtok(cpu_list, ","); token != NULL; token = strtok(NULL, ","))
        {
            if (cpu_count == BUSTER_UARCH_MAX_CPUS)
            {
                fail("too many CPUs");
            }
            cpus[cpu_count] = atoi(token);
            cpu_count += 1;
        }
        int command_count = argc - separator - 1;
        char** command = &argv[separator + 1];
        char path[BUSTER_UARCH_PATH_BYTES];
        format_into(path, sizeof(path), "%s/logs", out);
        make_directory(path);
        for (int round = 0; round < rounds; round += 1)
        {
            char log[BUSTER_UARCH_PATH_BYTES];
            char record[BUSTER_UARCH_PATH_BYTES];
            format_into(log, sizeof(log), "%s/logs/%s.r%d.log", out, label, round);
            format_into(record, sizeof(record), "%s/logs/%s.r%d.fanout", out, label, round);
            if (file_exists(record))
            {
                result |= recorded_status(record, 3) != 0;
                continue;
            }
            // A log without a record is an interrupted batch: start it again.
            int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            pid_t children[BUSTER_UARCH_MAX_CPUS];
            double start = now_ms();
            for (int k = 0; k < cpu_count; k += 1)
            {
                children[k] = fork();
                if (children[k] == 0)
                {
                    cpu_set_t set;
                    CPU_ZERO(&set);
                    CPU_SET(cpus[k], &set);
                    sched_setaffinity(0, sizeof(set), &set);
                    setenv("LC_ALL", "C", 1);
                    if (fd >= 0)
                    {
                        dup2(fd, 1);
                        dup2(fd, 2);
                    }
                    char** child_argv = checked_realloc(NULL, (size_t)(command_count + 1) * sizeof(char*));
                    for (int a = 0; a < command_count; a += 1)
                    {
                        char* marker = strstr(command[a], "{i}");
                        child_argv[a] = command[a];
                        if (marker != NULL)
                        {
                            size_t size = strlen(command[a]) + 16;
                            child_argv[a] = checked_realloc(NULL, size);
                            format_into(child_argv[a], size, "%.*s%d%s", (int)(marker - command[a]), command[a], k, marker + 3);
                        }
                    }
                    child_argv[command_count] = NULL;
                    execvp(child_argv[0], child_argv);
                    _exit(127);
                }
            }
            int failed = 0;
            double user = 0;
            double system = 0;
            double minflt = 0;
            long maxrss = 0;
            for (int k = 0; k < cpu_count; k += 1)
            {
                int status = 0;
                struct rusage usage = { 0 };
                bool waited = children[k] > 0 && wait4(children[k], &status, 0, &usage) >= 0;
                failed += !waited || !WIFEXITED(status) || WEXITSTATUS(status) != 0;
                user += timeval_ms(usage.ru_utime);
                system += timeval_ms(usage.ru_stime);
                minflt += (double)usage.ru_minflt;
                maxrss = usage.ru_maxrss > maxrss ? usage.ru_maxrss : maxrss;
            }
            double wall = now_ms() - start;
            if (fd >= 0)
            {
                close(fd);
            }
            char line[BUSTER_UARCH_LINE_BYTES];
            format_into(line, sizeof(line), "%s\t%d\t%d\t%d\t%.3f\t%.3f\t%.3f\t%ld\t%.0f\n", label, round, cpu_count, failed, wall, user, system, maxrss, minflt);
            write_text_atomic(record, line);
            result |= failed != 0;
        }
        rebuild_table(out, "fanout.tsv", ".fanout", "label\tround\tworkers\tfailed\twall_ms\tuser_ms_sum\tsystem_ms_sum\tmaxrss_kb_max\tminflt_sum\n");
    }
    return result;
}

// ---------------------------------------------------------------------------
// summarize

static int name_compare(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static Strings list_directory(const char* path, const char* suffix)
{
    Strings result = { 0 };
    DIR* directory = opendir(path);
    if (directory != NULL)
    {
        size_t suffix_length = strlen(suffix);
        for (struct dirent* entry = readdir(directory); entry != NULL; entry = readdir(directory))
        {
            size_t length = strlen(entry->d_name);
            if (length >= suffix_length && strcmp(entry->d_name + length - suffix_length, suffix) == 0)
            {
                strings_add(&result, entry->d_name);
            }
        }
        closedir(directory);
        qsort(result.items, result.count, sizeof(char*), name_compare);
    }
    return result;
}

// OUT/NAME, rebuilt from the completion records OUT/logs/*SUFFIX (one row each) in
// name order, under HEADER. The records are the source of truth: a run that was
// interrupted before its record has no row, and its retry adds exactly one.
static void rebuild_table(const char* out, const char* name, const char* suffix, const char* header)
{
    char directory[BUSTER_UARCH_PATH_BYTES];
    format_into(directory, sizeof(directory), "%s/logs", out);
    Strings records = list_directory(directory, suffix);
    size_t capacity = strlen(header) + 1;
    char* text = checked_realloc(NULL, capacity);
    size_t used = 0;
    memcpy(text, header, strlen(header) + 1);
    used = strlen(header);
    for (size_t i = 0; i < records.count; i += 1)
    {
        char path[BUSTER_UARCH_PATH_BYTES];
        format_into(path, sizeof(path), "%s/%s", directory, records.items[i]);
        Strings lines = read_lines(path);
        for (size_t l = 0; l < lines.count; l += 1)
        {
            size_t length = strlen(lines.items[l]);
            text = checked_realloc(text, used + length + 2);
            memcpy(text + used, lines.items[l], length);
            used += length;
            text[used] = '\n';
            used += 1;
            text[used] = 0;
        }
    }
    char table_path[BUSTER_UARCH_PATH_BYTES];
    format_into(table_path, sizeof(table_path), "%s/%s", out, name);
    write_text_atomic(table_path, text);
    free(text);
}

static FILE* open_output(const char* out, const char* name)
{
    char path[BUSTER_UARCH_PATH_BYTES];
    format_into(path, sizeof(path), "%s/%s", out, name);
    FILE* file = fopen(path, "w");
    if (file == NULL)
    {
        fail(strerror(errno));
    }
    return file;
}

// stat/LABEL.rN.csv -> stat.tsv: per label and event the median, min and max,
// the minimum duty and the runs; and each reported metric's median.
static void summarize_stat(const char* out)
{
    char directory[BUSTER_UARCH_PATH_BYTES];
    format_into(directory, sizeof(directory), "%s/stat", out);
    Strings files = list_directory(directory, ".csv");
    if (files.count > 0)
    {
        Table table = { 0 };
        Table metric_table = { 0 };
        for (size_t f = 0; f < files.count; f += 1)
        {
            char label[BUSTER_UARCH_PATH_BYTES];
            format_into(label, sizeof(label), "%s", files.items[f]);
            // The label ends at the last ".r" (".rN.csv").
            char* last = NULL;
            for (char* dot = strstr(label, ".r"); dot != NULL; dot = strstr(dot + 1, ".r"))
            {
                last = dot;
            }
            if (last != NULL)
            {
                *last = 0;
            }
            char path[BUSTER_UARCH_PATH_BYTES];
            format_into(path, sizeof(path), "%s/%s", directory, files.items[f]);
            Strings lines = read_lines(path);
            for (size_t i = 0; i < lines.count; i += 1)
            {
                CsvLine line = csv_split(lines.items[i]);
                if (line.count < 5)
                {
                    continue;
                }
                char key[BUSTER_UARCH_LINE_BYTES];
                format_into(key, sizeof(key), "%s\t%s", label, line.fields[2]);
                Entry* entry = table_get(&table, key);
                values_add(&entry->values, parse_value(line.fields[0]));
                values_add(&entry->duty, atof(line.fields[4]));
                if (line.count >= 7 && line.fields[6][0] != 0)
                {
                    format_into(key, sizeof(key), "%s\t%s", label, line.fields[6]);
                    values_add(&table_get(&metric_table, key)->values, parse_value(line.fields[5]));
                }
            }
        }
        FILE* file = open_output(out, "stat.tsv");
        fprintf(file, "label\tevent\tmedian\tmin\tmax\tduty_min\truns\n");
        for (size_t i = 0; i < table.count; i += 1)
        {
            Entry* entry = &table.items[i];
            fprintf(file, "%s\t%.6g\t%.6g\t%.6g\t%.2f\t%zu\n", entry->key, values_median(&entry->values), values_extreme(&entry->values, false),
                    values_extreme(&entry->values, true), values_extreme(&entry->duty, false), entry->values.count);
        }
        for (size_t i = 0; i < metric_table.count; i += 1)
        {
            Entry* entry = &metric_table.items[i];
            fprintf(file, "%s\t%.6g\t%.6g\t%.6g\t\t%zu\n", entry->key, values_median(&entry->values), values_extreme(&entry->values, false),
                    values_extreme(&entry->values, true), entry->values.count);
        }
        fclose(file);
    }
}

static SweepSample sweep_read(const char* path, const char* event)
{
    SweepSample result = { NAN, NAN, NAN, NAN, 100.0, false };
    Strings lines = read_lines(path);
    size_t length = strlen(event);
    for (size_t i = 0; i < lines.count; i += 1)
    {
        CsvLine line = csv_split(lines.items[i]);
        if (line.count < 5)
        {
            continue;
        }
        const char* name = line.fields[2];
        double value = parse_value(line.fields[0]);
        double duty = atof(line.fields[4]);
        result.duty = duty < result.duty ? duty : result.duty;
        bool own = strncmp(name, event, length) == 0;
        if (strcmp(name, "cycles:u") == 0)
        {
            result.cycles = value;
        }
        else if (strcmp(name, "instructions:u") == 0)
        {
            result.instructions = value;
        }
        else if (own && strcmp(name + length, ":u") == 0)
        {
            result.user = value;
        }
        else if (own && strcmp(name + length, ":k") == 0)
        {
            result.kernel = value;
        }
    }
    result.valid = lines.count > 0;
    return result;
}

// sweep/NNNN.rR.csv -> sweep.tsv: per event the user and kernel medians, per
// thousand user instructions and per user cycle, the kernel's share, the minimum
// duty, the runs and whether the fallback took them.
static void summarize_sweep(const char* out)
{
    char directory[BUSTER_UARCH_PATH_BYTES];
    format_into(directory, sizeof(directory), "%s/sweep", out);
    Strings names = list_directory(directory, ".event");
    if (names.count > 0)
    {
        FILE* file = open_output(out, "sweep.tsv");
        fprintf(file, "event\tuser_median\tkernel_median\tuser_per_kinst\tuser_per_cycle\tkernel_share\tuser_min\tuser_max\tduty_min\truns\tmode\n");
        for (size_t n = 0; n < names.count; n += 1)
        {
            char path[BUSTER_UARCH_PATH_BYTES];
            format_into(path, sizeof(path), "%s/%s", directory, names.items[n]);
            Strings event_lines = read_lines(path);
            if (event_lines.count == 0)
            {
                continue;
            }
            const char* event = event_lines.items[0];
            Values user = { 0 };
            Values kernel = { 0 };
            Values per_kinst = { 0 };
            Values per_cycle = { 0 };
            double duty_min = 100.0;
            int fallbacks = 0;
            bool more = true;
            for (int run = 0; run < BUSTER_UARCH_MAX_RUNS && more; run += 1)
            {
                char csv[BUSTER_UARCH_PATH_BYTES];
                format_into(csv, sizeof(csv), "%s/%.4s.r%d.csv", directory, names.items[n], run);
                more = file_exists(csv);
                if (!more)
                {
                    continue;
                }
                SweepSample sample = sweep_read(csv, event);
                char first[BUSTER_UARCH_PATH_BYTES];
                char second[BUSTER_UARCH_PATH_BYTES];
                format_into(first, sizeof(first), "%s/%.4s.r%d.f0.csv", directory, names.items[n], run);
                format_into(second, sizeof(second), "%s/%.4s.r%d.f1.csv", directory, names.items[n], run);
                if (file_exists(first))
                {
                    SweepSample user_sample = sweep_read(first, event);
                    SweepSample kernel_sample = sweep_read(second, event);
                    sample.user = user_sample.user;
                    sample.kernel = kernel_sample.kernel;
                    sample.instructions = user_sample.instructions;
                    sample.cycles = NAN;
                    sample.duty = user_sample.duty < kernel_sample.duty ? user_sample.duty : kernel_sample.duty;
                    fallbacks += 1;
                }
                if (sample.valid)
                {
                    values_add(&user, sample.user);
                    values_add(&kernel, sample.kernel);
                    values_add(&per_kinst, sample.user / sample.instructions * 1000.0);
                    values_add(&per_cycle, sample.user / sample.cycles);
                    duty_min = sample.duty < duty_min ? sample.duty : duty_min;
                }
            }
            double user_median = values_median(&user);
            double kernel_median = values_median(&kernel);
            fprintf(file, "%s\t%.6g\t%.6g\t%.6g\t%.6g\t%.4f\t%.6g\t%.6g\t%.2f\t%zu\t%s\n", event, user_median, kernel_median, values_median(&per_kinst),
                    values_median(&per_cycle), kernel_median / (user_median + kernel_median), values_extreme(&user, false), values_extreme(&user, true),
                    duty_min, user.count, fallbacks ? "fallback" : "group");
        }
        fclose(file);
    }
}

// metrics/NNNN.rR.csv -> metrics.tsv: per metric each reported value's median,
// the minimum duty, the runs and the runs whose perf stat failed.
static void summarize_metrics(const char* out)
{
    char directory[BUSTER_UARCH_PATH_BYTES];
    format_into(directory, sizeof(directory), "%s/metrics", out);
    Strings names = list_directory(directory, ".metric");
    if (names.count > 0)
    {
        FILE* file = open_output(out, "metrics.tsv");
        fprintf(file, "metric\treported\tmedian\tmin\tmax\tduty_min\truns\tfailed_runs\n");
        for (size_t n = 0; n < names.count; n += 1)
        {
            char path[BUSTER_UARCH_PATH_BYTES];
            format_into(path, sizeof(path), "%s/%s", directory, names.items[n]);
            Strings metric_lines = read_lines(path);
            if (metric_lines.count == 0)
            {
                continue;
            }
            Table reported = { 0 };
            double duty_min = 100.0;
            int failed = 0;
            int runs = 0;
            bool more = true;
            for (int run = 0; run < BUSTER_UARCH_MAX_RUNS && more; run += 1)
            {
                char csv[BUSTER_UARCH_PATH_BYTES];
                char status_path[BUSTER_UARCH_PATH_BYTES];
                format_into(csv, sizeof(csv), "%s/%.4s.r%d.csv", directory, names.items[n], run);
                format_into(status_path, sizeof(status_path), "%s/%.4s.r%d.status", directory, names.items[n], run);
                more = file_exists(status_path);
                if (!more)
                {
                    continue;
                }
                runs += 1;
                Strings status = read_lines(status_path);
                failed += status.count == 0 || strncmp(status.items[0], "status\t0\t", 9) != 0;
                Strings lines = read_lines(csv);
                for (size_t i = 0; i < lines.count; i += 1)
                {
                    CsvLine line = csv_split(lines.items[i]);
                    if (line.count < BUSTER_UARCH_CSV_FIELDS)
                    {
                        continue;
                    }
                    double duty = atof(line.fields[4]);
                    if (line.fields[0][0] != '<' && duty < duty_min)
                    {
                        duty_min = duty;
                    }
                    if (line.fields[6][0] != 0)
                    {
                        values_add(&table_get(&reported, line.fields[6])->values, parse_value(line.fields[5]));
                    }
                }
            }
            if (reported.count == 0)
            {
                fprintf(file, "%s\t\tnan\tnan\tnan\t%.2f\t%d\t%d\n", metric_lines.items[0], duty_min, runs, failed);
            }
            for (size_t i = 0; i < reported.count; i += 1)
            {
                Entry* entry = &reported.items[i];
                fprintf(file, "%s\t%s\t%.6g\t%.6g\t%.6g\t%.2f\t%d\t%d\n", metric_lines.items[0], entry->key, values_median(&entry->values),
                        values_extreme(&entry->values, false), values_extreme(&entry->values, true), duty_min, runs, failed);
            }
        }
        fclose(file);
    }
}

// time.tsv -> time-summary.tsv: per label the median of each column and the
// failed runs, which the medians leave out.
static void summarize_time(const char* out)
{
    char path[BUSTER_UARCH_PATH_BYTES];
    format_into(path, sizeof(path), "%s/time.tsv", out);
    Strings lines = read_lines(path);
    if (lines.count >= 2)
    {
        const char* columns[] = { "wall_ms", "user_ms", "system_ms", "maxrss_kb", "minflt", "majflt", "nvcsw", "nivcsw" };
        const int column_count = (int)(sizeof(columns) / sizeof(columns[0]));
        Table table = { 0 };
        Strings labels = { 0 };
        for (size_t i = 1; i < lines.count; i += 1)
        {
            char* fields[16];
            int count = split_tabs(lines.items[i], fields, 16);
            if (count < 11)
            {
                continue;
            }
            if (!strings_contains(&labels, fields[0]))
            {
                strings_add(&labels, fields[0]);
            }
            char key[BUSTER_UARCH_LINE_BYTES];
            format_into(key, sizeof(key), "%s\tstatus", fields[0]);
            values_add(&table_get(&table, key)->values, atof(fields[2]));
            if (atoi(fields[2]) == 0)
            {
                for (int c = 0; c < column_count; c += 1)
                {
                    format_into(key, sizeof(key), "%s\t%s", fields[0], columns[c]);
                    values_add(&table_get(&table, key)->values, atof(fields[3 + c]));
                }
            }
        }
        FILE* file = open_output(out, "time-summary.tsv");
        fprintf(file, "label\truns\tfailed\twall_ms_median\twall_ms_min\twall_ms_max\tuser_ms\tsystem_ms\tmaxrss_kb\tminflt\tmajflt\tnvcsw\tnivcsw\n");
        for (size_t l = 0; l < labels.count; l += 1)
        {
            char key[BUSTER_UARCH_LINE_BYTES];
            format_into(key, sizeof(key), "%s\tstatus", labels.items[l]);
            Values* status = &table_get(&table, key)->values;
            int failed = 0;
            for (size_t i = 0; i < status->count; i += 1)
            {
                failed += status->items[i] != 0;
            }
            fprintf(file, "%s\t%zu\t%d", labels.items[l], status->count, failed);
            for (int c = 0; c < column_count; c += 1)
            {
                format_into(key, sizeof(key), "%s\t%s", labels.items[l], columns[c]);
                Values* values = &table_get(&table, key)->values;
                if (c == 0)
                {
                    fprintf(file, "\t%.3f\t%.3f\t%.3f", values_median(values), values_extreme(values, false), values_extreme(values, true));
                }
                else
                {
                    fprintf(file, "\t%.3f", values_median(values));
                }
            }
            fprintf(file, "\n");
        }
        fclose(file);
    }
}

static int command_summarize(int argc, char** argv)
{
    int result = 0;
    if (argc != 3)
    {
        fprintf(stderr, "usage: uarch summarize OUT\n");
        result = 1;
    }
    else
    {
        summarize_stat(argv[2]);
        summarize_sweep(argv[2]);
        summarize_metrics(argv[2]);
        summarize_time(argv[2]);
    }
    return result;
}

// ---------------------------------------------------------------------------
// stages: medians of the compiler's --time-stages reports.

// Splits one report line (time-stage, time-check or time-process) in place; its
// header line and lines without a number are not valid.
static StageLine stages_parse_line(char* line)
{
    StageLine result = { 0 };
    if (strncmp(line, "time-", 5) == 0)
    {
        result.count = split_tabs(line, result.fields, 8);
        char* end = NULL;
        if (result.count >= 3)
        {
            strtod(result.fields[2], &end);
        }
        result.valid = result.count >= 3 && strcmp(result.fields[1], "stage") != 0 && end != result.fields[2];
    }
    return result;
}

static int command_stages(int argc, char** argv)
{
    Table table = { 0 };
    Strings order = { 0 };
    const char* stage_columns[] = { "ms", "percent", "system_ms", "minor_faults", "peak_kb_added" };
    for (int a = 2; a < argc; a += 1)
    {
        Strings lines = read_lines(argv[a]);
        for (size_t i = 0; i < lines.count; i += 1)
        {
            StageLine line = stages_parse_line(lines.items[i]);
            if (!line.valid)
            {
                continue;
            }
            bool stage = strcmp(line.fields[0], "time-stage") == 0;
            for (int c = 2; c < line.count && c < 7; c += 1)
            {
                char key[BUSTER_UARCH_LINE_BYTES];
                format_into(key, sizeof(key), "%s\t%s\t%s", line.fields[0], line.fields[1], stage || c > 2 ? stage_columns[c - 2] : "value");
                Entry* entry = table_get(&table, key);
                if (entry->values.count == 0)
                {
                    strings_add(&order, key);
                }
                values_add(&table_get(&table, key)->values, strtod(line.fields[c], NULL));
            }
        }
    }
    printf("kind\tname\tcolumn\tmedian\tmin\tmax\truns\n");
    for (size_t i = 0; i < order.count; i += 1)
    {
        Entry* entry = table_get(&table, order.items[i]);
        printf("%s\t%.6g\t%.6g\t%.6g\t%zu\n", entry->key, values_median(&entry->values), values_extreme(&entry->values, false),
               values_extreme(&entry->values, true), entry->values.count);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// symbols: per-symbol sums of each event's periods.

// One line of `perf script -F period,event,ip,sym`: "PERIOD EVENT: IP SYMBOL". For a
// leader-sampled group ({...}:S) each member prints its own line, with its delta
// since the previous sample as its period, at the leader's address. Members come
// in an order that differs between hosts, so tables use event names, not columns.
static SymbolLine symbols_parse_line(char* line)
{
    SymbolLine result = { 0 };
    char* text = trim(line);
    char* end = NULL;
    result.period = strtod(text, &end);
    char* event = end != text ? trim(end) : NULL;
    char* colon = event != NULL ? strstr(event, ": ") : NULL;
    if (colon != NULL)
    {
        *colon = 0;
        char* rest = trim(colon + 2);
        // The address, then the symbol.
        while (*rest && !isspace((unsigned char)*rest))
        {
            rest += 1;
        }
        result.symbol = trim(rest);
        result.symbol = *result.symbol ? result.symbol : "[unknown]";
        result.event = event;
        result.valid = true;
    }
    return result;
}

static int command_symbols(int argc, char** argv)
{
    int result = 0;
    if (argc != 3)
    {
        fprintf(stderr, "usage: uarch symbols SCRIPT\n");
        result = 1;
    }
    else
    {
        FILE* file = open_input(argv[2]);
        Table cells = { 0 };
        Table totals = { 0 };
        Strings events = { 0 };
        char* line = NULL;
        size_t capacity = 0;
        while (getline(&line, &capacity, file) > 0)
        {
            SymbolLine sample = symbols_parse_line(line);
            if (!sample.valid)
            {
                continue;
            }
            if (!strings_contains(&events, sample.event))
            {
                strings_add(&events, sample.event);
            }
            char key[BUSTER_UARCH_LINE_BYTES];
            format_into(key, sizeof(key), "%s\t%s", sample.symbol, sample.event);
            table_add(&cells, key, sample.period);
            table_add(&totals, sample.symbol, strcmp(sample.event, events.items[0]) == 0 ? sample.period : 0);
        }
        free(line);
        printf("symbol");
        for (size_t e = 0; e < events.count; e += 1)
        {
            printf("\t%s", events.items[e]);
        }
        printf("\n");
        // Symbols by the first event's total, largest first.
        size_t* order = checked_realloc(NULL, (totals.count + 1) * sizeof(size_t));
        for (size_t i = 0; i < totals.count; i += 1)
        {
            order[i] = i;
        }
        for (size_t i = 1; i < totals.count; i += 1)
        {
            size_t current = order[i];
            size_t j = i;
            while (j > 0 && totals.items[order[j - 1]].values.items[0] < totals.items[current].values.items[0])
            {
                order[j] = order[j - 1];
                j -= 1;
            }
            order[j] = current;
        }
        for (size_t i = 0; i < totals.count; i += 1)
        {
            char symbol[BUSTER_UARCH_LINE_BYTES];
            format_into(symbol, sizeof(symbol), "%s", totals.items[order[i]].key);
            printf("%s", symbol);
            for (size_t e = 0; e < events.count; e += 1)
            {
                char key[BUSTER_UARCH_LINE_BYTES];
                format_into(key, sizeof(key), "%s\t%s", symbol, events.items[e]);
                printf("\t%.0f", table_sum(&cells, key));
            }
            printf("\n");
        }
        free(order);
    }
    return result;
}

// ---------------------------------------------------------------------------
// branches: LBR entries by source symbol and site.

// One entry of `perf script -F brstacksym`: FROM/TO/FLAG/..., FROM and TO as
// symbol+offset, FLAG M (mispredicted), P (predicted) or -. Splits in place.
static BranchEntry branches_parse_token(char* token)
{
    BranchEntry result = { 0 };
    char* first = strchr(token, '/');
    char* second = first != NULL ? strchr(first + 1, '/') : NULL;
    char flag = second != NULL ? second[1] : 0;
    if (flag == 'M' || flag == 'P' || flag == '-')
    {
        *first = 0;
        result.site = token;
        result.mispredicted = flag == 'M';
        result.valid = true;
    }
    return result;
}

static void print_branch_table(const char* title, Table* table, double mispredicted, size_t top)
{
    printf("%s\tentries\tmispredicted\tmispredict_rate\tshare_of_mispredicts\n", title);
    size_t* order = checked_realloc(NULL, (table->count + 1) * sizeof(size_t));
    for (size_t i = 0; i < table->count; i += 1)
    {
        order[i] = i;
    }
    for (size_t i = 1; i < table->count; i += 1)
    {
        size_t current = order[i];
        size_t j = i;
        while (j > 0 && table->items[order[j - 1]].values.items[1] < table->items[current].values.items[1])
        {
            order[j] = order[j - 1];
            j -= 1;
        }
        order[j] = current;
    }
    for (size_t i = 0; i < table->count && i < top; i += 1)
    {
        Entry* entry = &table->items[order[i]];
        printf("%s\t%.0f\t%.0f\t%.4f\t%.4f\n", entry->key, entry->values.items[0], entry->values.items[1], entry->values.items[1] / entry->values.items[0],
               entry->values.items[1] / (mispredicted > 0 ? mispredicted : 1));
    }
    free(order);
}

static void branch_count(Table* table, const char* key, bool mispredicted)
{
    Entry* entry = table_get(table, key);
    if (entry->values.count == 0)
    {
        values_add(&entry->values, 0);
        values_add(&entry->values, 0);
    }
    entry->values.items[0] += 1;
    entry->values.items[1] += mispredicted;
}

static int command_branches(int argc, char** argv)
{
    int result = 0;
    if (argc < 3)
    {
        fprintf(stderr, "usage: uarch branches SCRIPT [TOP]\n");
        result = 1;
    }
    else
    {
        size_t top = argc > 3 ? (size_t)atol(argv[3]) : 60;
        FILE* file = open_input(argv[2]);
        Table by_symbol = { 0 };
        Table by_site = { 0 };
        double entries = 0;
        double mispredicted = 0;
        char* line = NULL;
        size_t capacity = 0;
        while (getline(&line, &capacity, file) > 0)
        {
            for (char* token = strtok(line, " \t\n"); token != NULL; token = strtok(NULL, " \t\n"))
            {
                BranchEntry entry = branches_parse_token(token);
                if (!entry.valid)
                {
                    continue;
                }
                char symbol[BUSTER_UARCH_LINE_BYTES];
                format_into(symbol, sizeof(symbol), "%s", entry.site);
                char* plus = strrchr(symbol, '+');
                if (plus != NULL)
                {
                    *plus = 0;
                }
                entries += 1;
                mispredicted += entry.mispredicted;
                branch_count(&by_symbol, symbol, entry.mispredicted);
                branch_count(&by_site, entry.site, entry.mispredicted);
            }
        }
        free(line);
        printf("# entries\t%.0f\tmispredicted\t%.0f\n", entries, mispredicted);
        print_branch_table("symbol", &by_symbol, mispredicted, top);
        print_branch_table("site", &by_site, mispredicted, top);
    }
    return result;
}

// ---------------------------------------------------------------------------
// memory: IBS op samples by symbol, data source and load latency.

static void copy_field(char* destination, size_t size, const char* text, const char* name)
{
    const char* field = strstr(text, name);
    format_into(destination, size, "%s", "none");
    if (field != NULL)
    {
        field += strlen(name);
        const char* end = strchr(field, '|');
        size_t length = end != NULL ? (size_t)(end - field) : strlen(field);
        length = length < size - 1 ? length : size - 1;
        memcpy(destination, field, length);
        destination[length] = 0;
    }
}

// One line of `perf script -F ip,sym,data_src,weight` over IBS ops
// (`ibs_op/cnt_ctl=1,swfilt=1/u -d -W`): the raw data source, its decoded fields
// ("|OP LOAD|LVL L1 hit|SNP ..|TLB L1 hit|LCK ..|BLK ..."), then the weight (a
// load's miss latency in cycles, 0 for a hit), the address and the symbol. The
// last three tokens are read from the end of the line.
static MemoryLine memory_parse_line(char* line)
{
    MemoryLine result = { 0 };
    char* text = trim(line);
    copy_field(result.level, sizeof(result.level), text, "|LVL ");
    copy_field(result.tlb, sizeof(result.tlb), text, "|TLB ");
    result.load = strstr(text, "|OP LOAD|") != NULL;
    result.store = strstr(text, "|OP STORE|") != NULL;
    char* tokens[3] = { NULL, NULL, NULL };
    char* cursor = text + strlen(text);
    for (int t = 2; t >= 0 && cursor > text; t -= 1)
    {
        while (cursor > text && isspace((unsigned char)cursor[-1]))
        {
            cursor -= 1;
            *cursor = 0;
        }
        while (cursor > text && !isspace((unsigned char)cursor[-1]))
        {
            cursor -= 1;
        }
        tokens[t] = cursor;
    }
    if (tokens[0] != NULL && *text != 0)
    {
        result.weight = strtod(tokens[0], NULL);
        result.symbol = *tokens[2] ? tokens[2] : "[unknown]";
        result.valid = true;
    }
    return result;
}

static int command_memory(int argc, char** argv)
{
    int result = 0;
    if (argc != 3)
    {
        fprintf(stderr, "usage: uarch memory SCRIPT\n");
        result = 1;
    }
    else
    {
        FILE* file = open_input(argv[2]);
        Table by_symbol_level = { 0 };
        Table latencies = { 0 };
        Table samples = { 0 };
        Table loads = { 0 };
        Table stores = { 0 };
        Strings order = { 0 };
        char* line = NULL;
        size_t capacity = 0;
        while (getline(&line, &capacity, file) > 0)
        {
            MemoryLine sample = memory_parse_line(line);
            if (!sample.valid)
            {
                continue;
            }
            if (table_get(&samples, sample.symbol)->values.count == 0)
            {
                strings_add(&order, sample.symbol);
            }
            table_add(&samples, sample.symbol, 1);
            table_add(&loads, sample.symbol, sample.load);
            table_add(&stores, sample.symbol, sample.store);
            if (sample.load || sample.store)
            {
                char key[BUSTER_UARCH_LINE_BYTES];
                format_into(key, sizeof(key), "%s\t%s\tLVL %s\tTLB %s", sample.symbol, sample.load ? "load" : "store", sample.level, sample.tlb);
                table_add(&by_symbol_level, key, 1);
            }
            if (sample.load && sample.weight > 0)
            {
                values_add(&table_get(&latencies, sample.symbol)->values, sample.weight);
            }
        }
        free(line);
        printf("symbol\tsamples\tload_samples\tstore_samples\tloads_with_latency\tlatency_median\tlatency_mean\tlatency_p90\n");
        for (size_t i = 0; i < order.count; i += 1)
        {
            Values* latency = &table_get(&latencies, order.items[i])->values;
            double mean = NAN;
            double p90 = NAN;
            if (latency->count > 0)
            {
                double sum = 0;
                for (size_t k = 0; k < latency->count; k += 1)
                {
                    sum += latency->items[k];
                }
                mean = sum / (double)latency->count;
                qsort(latency->items, latency->count, sizeof(double), compare_doubles);
                p90 = latency->items[(size_t)((double)latency->count * 0.9)];
            }
            printf("%s\t%.0f\t%.0f\t%.0f\t%zu\t%.1f\t%.1f\t%.1f\n", order.items[i], table_sum(&samples, order.items[i]), table_sum(&loads, order.items[i]),
                   table_sum(&stores, order.items[i]), latency->count, values_median(latency), mean, p90);
        }
        printf("symbol_level\tsymbol\top\tlevel\ttlb\tsamples\n");
        for (size_t i = 0; i < by_symbol_level.count; i += 1)
        {
            printf("symbol_level\t%s\t%.0f\n", by_symbol_level.items[i].key, by_symbol_level.items[i].values.items[0]);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// topdown: per-symbol top-down level 1 and 2 from two `symbols` tables of the
// leader-sampled groups
//   TD1 = {ls_not_halted_cyc, de_src_op_disp.all, ex_ret_ops,
//          de_no_dispatch_per_slot.no_ops_from_frontend, de_no_dispatch_per_slot.backend_stalls}
//   TD2 = {ls_not_halted_cyc, ex_no_retire.load_not_complete, ex_no_retire.not_complete,
//          instructions, ex_ret_brn_misp}
// with WIDTH dispatch slots per cycle (6 on Zen 4, 8 on Zen 5), as perf's
// PipelineL1/L2 metrics define them. Each row's shares come from the same samples.

static SymbolTable read_symbol_table(const char* path)
{
    SymbolTable result = { 0 };
    Strings lines = read_lines(path);
    for (size_t i = 0; i < lines.count; i += 1)
    {
        char* fields[16];
        int count = split_tabs(lines.items[i], fields, 16);
        if (i == 0)
        {
            for (int c = 1; c < count; c += 1)
            {
                strings_add(&result.columns, fields[c]);
            }
        }
        else
        {
            Entry* entry = table_get(&result.rows, fields[0]);
            for (int c = 1; c < count; c += 1)
            {
                values_add(&entry->values, strtod(fields[c], NULL));
            }
        }
    }
    return result;
}

static int column_index(const SymbolTable* table, const char* prefix)
{
    int result = -1;
    for (size_t c = 0; c < table->columns.count && result < 0; c += 1)
    {
        if (strncmp(table->columns.items[c], prefix, strlen(prefix)) == 0)
        {
            result = (int)c;
        }
    }
    return result;
}

static double cell(const Entry* entry, int column)
{
    return entry != NULL && column >= 0 && (size_t)column < entry->values.count ? entry->values.items[column] : NAN;
}

static TopdownRow topdown_row(const SymbolTable* td1, const Entry* first, const SymbolTable* td2, const Entry* second, double width)
{
    TopdownRow result = { 0 };
    double cycles = cell(first, column_index(td1, "ls_not_halted_cyc"));
    double slots = cycles * width;
    double memory_fraction = cell(second, column_index(td2, "ex_no_retire.load_not_complete")) / cell(second, column_index(td2, "ex_no_retire.not_complete"));
    double instructions = cell(second, column_index(td2, "instructions"));
    result.cycles = cycles;
    result.ipc = instructions / cell(second, column_index(td2, "ls_not_halted_cyc"));
    result.retiring = cell(first, column_index(td1, "ex_ret_ops")) / slots;
    result.frontend = cell(first, column_index(td1, "de_no_dispatch_per_slot.no_ops_from_frontend")) / slots;
    result.backend = cell(first, column_index(td1, "de_no_dispatch_per_slot.backend_stalls")) / slots;
    result.bad_speculation = (cell(first, column_index(td1, "de_src_op_disp.all")) - cell(first, column_index(td1, "ex_ret_ops"))) / slots;
    result.backend_memory = result.backend * memory_fraction;
    result.backend_cpu = result.backend * (1 - memory_fraction);
    result.mispredicts_pki = cell(second, column_index(td2, "ex_ret_brn_misp")) / instructions * 1000.0;
    return result;
}

// An entry holding each column's sum over every row of the table.
static Entry table_column_sums(const SymbolTable* table)
{
    Entry result = { 0 };
    for (size_t c = 0; c < table->columns.count; c += 1)
    {
        values_add(&result.values, 0);
    }
    for (size_t i = 0; i < table->rows.count; i += 1)
    {
        for (size_t c = 0; c < table->columns.count && c < table->rows.items[i].values.count; c += 1)
        {
            result.values.items[c] += table->rows.items[i].values.items[c];
        }
    }
    return result;
}

static void print_topdown_row(const char* name, TopdownRow row, double total)
{
    printf("%s\t%.4f\t%.3f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.3f\n", name, row.cycles / total, row.ipc, row.retiring, row.frontend, row.backend,
           row.bad_speculation, row.backend_memory, row.backend_cpu, row.mispredicts_pki);
}

static int command_topdown(int argc, char** argv)
{
    int result = 0;
    if (argc < 5)
    {
        fprintf(stderr, "usage: uarch topdown TD1.tsv TD2.tsv WIDTH [MIN_PERCENT]\n");
        result = 1;
    }
    else
    {
        SymbolTable td1 = read_symbol_table(argv[2]);
        SymbolTable td2 = read_symbol_table(argv[3]);
        double width = atof(argv[4]);
        double minimum = argc > 5 ? atof(argv[5]) : 0.5;
        Entry sum1 = table_column_sums(&td1);
        Entry sum2 = table_column_sums(&td2);
        double total = cell(&sum1, column_index(&td1, "ls_not_halted_cyc"));
        printf("symbol\tcycles_share\tipc\tretiring\tfrontend\tbackend\tbad_speculation\tbackend_memory\tbackend_cpu\tmispredicts_pki\n");
        for (size_t i = 0; i < td1.rows.count; i += 1)
        {
            Entry* first = &td1.rows.items[i];
            char name[BUSTER_UARCH_LINE_BYTES];
            format_into(name, sizeof(name), "%s", first->key);
            Entry* second = table_get(&td2.rows, name);
            TopdownRow row = topdown_row(&td1, &td1.rows.items[i], &td2, second, width);
            if (row.cycles / total * 100.0 >= minimum)
            {
                print_topdown_row(name, row, total);
            }
        }
        print_topdown_row("[all]", topdown_row(&td1, &sum1, &td2, &sum2, width), total);
    }
    return result;
}

// ---------------------------------------------------------------------------
// generate-generic N: a C unit of N functions with one _Generic selection each,
// the input of run.sh's generic-scaling step (#1341).

static int command_generate_generic(int argc, char** argv)
{
    int result = 0;
    if (argc != 3)
    {
        fprintf(stderr, "usage: uarch generate-generic N\n");
        result = 1;
    }
    else
    {
        int count = atoi(argv[2]);
        for (int i = 0; i < count; i += 1)
        {
            printf("int f%d(int a, long b) { int x = a * %d + (int)b; int y = _Generic(x, int: x + 1, long: x - 1, default: 0); return x + y; }\n", i, i);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// test: the parsers and reducers against fixtures of each tool's real output.

static int test_checks;
static int test_failures;

static void expect(bool condition, const char* name)
{
    test_checks += 1;
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        test_failures += 1;
    }
}

static bool near(double a, double b)
{
    return fabs(a - b) <= 1e-9 * (fabs(a) + fabs(b) + 1);
}

static void test_csv(void)
{
    char plain[] = "1072229971,,ls_not_halted_cyc:u,246891943,100.00,,";
    CsvLine line = csv_split(plain);
    expect(line.count == 7 && strcmp(line.fields[2], "ls_not_halted_cyc:u") == 0 && near(parse_value(line.fields[0]), 1072229971), "csv: plain event");
    char terms[] = "95604957,,cpu/de_no_dispatch_per_slot.no_ops_from_frontend,cmask=0x6/u,246891943,100.00,,";
    line = csv_split(terms);
    expect(line.count == 7 && strcmp(line.fields[2], "cpu/de_no_dispatch_per_slot.no_ops_from_frontend,cmask=0x6/u") == 0, "csv: an event's own commas rejoined");
    expect(strcmp(line.fields[4], "100.00") == 0, "csv: duty after an event with commas");
    char metric[] = "3044002829,,de_no_dispatch_per_slot.no_ops_from_frontend,299550622,100.00,40.0,%  frontend_bound";
    line = csv_split(metric);
    expect(line.count == 7 && near(parse_value(line.fields[5]), 40.0) && strcmp(line.fields[6], "%  frontend_bound") == 0, "csv: metric value and unit");
    char missing[] = "<not counted>,,ex_ret_ops:u,0,0.00,,";
    line = csv_split(missing);
    expect(isnan(parse_value(line.fields[0])), "csv: <not counted> is not a number");
}

static void test_values(void)
{
    Values values = { 0 };
    values_add(&values, 3);
    values_add(&values, NAN);
    values_add(&values, 1);
    values_add(&values, 2);
    expect(near(values_median(&values), 2) && near(values_extreme(&values, false), 1) && near(values_extreme(&values, true), 3), "values: median and extremes skip NAN");
    values_add(&values, 10);
    expect(near(values_median(&values), 2.5), "values: even count's median");
    Table table = { 0 };
    for (int i = 0; i < 1000; i += 1)
    {
        char key[32];
        format_into(key, sizeof(key), "k%d", i % 300);
        table_add(&table, key, 1);
    }
    expect(table.count == 300 && near(table_sum(&table, "k7"), 4) && near(table_sum(&table, "k299"), 3), "table: hash index across growth");
}

static void test_parsers(void)
{
    char symbol_line[] = "    200003 ibs_op/cnt_ctl=1,swfilt=1/u:            d822e8 lex_identifier_continue\n";
    SymbolLine symbol = symbols_parse_line(symbol_line);
    expect(symbol.valid && near(symbol.period, 200003) && strcmp(symbol.event, "ibs_op/cnt_ctl=1,swfilt=1/u") == 0 &&
               strcmp(symbol.symbol, "lex_identifier_continue") == 0,
           "symbols: period, event with terms, symbol");
    char unknown_line[] = "   1001277          cycles:u:      7f25e8cc1ad0 [unknown]\n";
    symbol = symbols_parse_line(unknown_line);
    expect(symbol.valid && strcmp(symbol.event, "cycles:u") == 0 && strcmp(symbol.symbol, "[unknown]") == 0, "symbols: unknown symbol");
    char branch[] = "check_input_expression_coded+0x142/check_input_expression_coded+0xd40/M/-/-/0//NON_SPEC_CORRECT_PATH";
    BranchEntry entry = branches_parse_token(branch);
    expect(entry.valid && entry.mispredicted && strcmp(entry.site, "check_input_expression_coded+0x142") == 0, "branches: mispredicted entry");
    char predicted[] = "tokenize_c+0xe8d/tokenize_c+0xd40/P/-/-/0//NON_SPEC_CORRECT_PATH";
    entry = branches_parse_token(predicted);
    expect(entry.valid && !entry.mispredicted, "branches: predicted entry");
    char not_entry[] = "0x1234";
    expect(!branches_parse_token(not_entry).valid, "branches: a token that isn't an entry");
    char load[] = "      1a29081042 |OP LOAD|LVL RAM hit|SNP N/A|TLB L1 hit|LCK N/A|BLK  N/A             519           e197a0 compare\n";
    MemoryLine memory = memory_parse_line(load);
    expect(memory.valid && memory.load && !memory.store && near(memory.weight, 519) && strcmp(memory.level, "RAM hit") == 0 &&
               strcmp(memory.tlb, "L1 hit") == 0 && strcmp(memory.symbol, "compare") == 0,
           "memory: a load from DRAM with its latency");
    char other[] = "      1e05080021 |OP N/A|LVL N/A|SNP N/A|TLB N/A|LCK N/A|BLK  N/A               0     7f25e8cc1ad0 [unknown]\n";
    memory = memory_parse_line(other);
    expect(memory.valid && !memory.load && !memory.store && strcmp(memory.symbol, "[unknown]") == 0, "memory: an op that isn't a load or store");
    char stage[] = "time-stage\tcheck-program\t58.432\t36.0\t0.015\t3\t14336";
    StageLine stage_line = stages_parse_line(stage);
    expect(stage_line.valid && stage_line.count == 7 && strcmp(stage_line.fields[1], "check-program") == 0, "stages: a stage line");
    char header[] = "time-stage\tstage\tms\tpercent\tsystem-ms\tminor-faults\tpeak-kb-added";
    expect(!stages_parse_line(header).valid, "stages: the header line");
    char process[] = "time-process\thuge-pages\tsystem";
    expect(!stages_parse_line(process).valid, "stages: a line without a number");
}

static void test_topdown(void)
{
    // Columns in the order Zen 5 printed them (not the spec's), on purpose.
    SymbolTable td1 = { 0 };
    SymbolTable td2 = { 0 };
    const char* first_columns[] = { "ls_not_halted_cyc:u", "ex_ret_ops:u", "de_src_op_disp.all:u", "de_no_dispatch_per_slot.backend_stalls:u",
                                    "de_no_dispatch_per_slot.no_ops_from_frontend:u" };
    const char* second_columns[] = { "ls_not_halted_cyc:u", "instructions:u", "ex_ret_brn_misp:u", "ex_no_retire.load_not_complete:u", "ex_no_retire.not_complete:u" };
    for (int c = 0; c < 5; c += 1)
    {
        strings_add(&td1.columns, first_columns[c]);
        strings_add(&td2.columns, second_columns[c]);
    }
    Entry* a = table_get(&td1.rows, "f");
    double first_values[] = { 100, 200, 300, 400, 160 };
    double second_values[] = { 100, 250, 5, 30, 60 };
    for (int c = 0; c < 5; c += 1)
    {
        values_add(&a->values, first_values[c]);
    }
    Entry* b = table_get(&td2.rows, "f");
    for (int c = 0; c < 5; c += 1)
    {
        values_add(&b->values, second_values[c]);
    }
    TopdownRow row = topdown_row(&td1, a, &td2, b, 8);
    expect(near(row.retiring, 0.25) && near(row.frontend, 0.2) && near(row.backend, 0.5) && near(row.bad_speculation, 0.125), "topdown: level 1 by column name");
    expect(near(row.backend_memory, 0.25) && near(row.backend_cpu, 0.25) && near(row.ipc, 2.5) && near(row.mispredicts_pki, 20), "topdown: level 2, IPC, mispredicts");
}

static int remove_entry(const char* path, const struct stat* information, int flag, struct FTW* walk)
{
    (void)information;
    (void)flag;
    (void)walk;
    return remove(path);
}

static Strings read_table_rows(const char* path)
{
    Strings lines = read_lines(path);
    Strings result = { 0 };
    for (size_t i = 1; i < lines.count; i += 1)
    {
        strings_add(&result, lines.items[i]);
    }
    return result;
}

// time and fanout resume from their completion records: an interrupted run (its
// log but no record) runs again and adds exactly one row; a recorded failure is
// reported again on resume.
static void test_resume(void)
{
    const char* temporary = getenv("TMPDIR");
    char directory[BUSTER_UARCH_PATH_BYTES];
    format_into(directory, sizeof(directory), "%s/uarch-test.XXXXXX", temporary != NULL && *temporary ? temporary : "/tmp");
    bool made = mkdtemp(directory) != NULL;
    expect(made, "resume: a temporary directory");
    if (made)
    {
        char path[BUSTER_UARCH_PATH_BYTES];
        char* first[] = { "uarch", "time", directory, "2", "--list", "ok", "true", "--list", "bad", "sh", "-c", "exit 3", NULL };
        int status = command_time(12, first);
        format_into(path, sizeof(path), "%s/time.tsv", directory);
        expect(status == 1 && read_table_rows(path).count == 4, "resume: time runs every round and reports the failing list");
        // Interrupted: the log is there, the record is not.
        format_into(path, sizeof(path), "%s/logs/ok.r1.time", directory);
        unlink(path);
        char* second[] = { "uarch", "time", directory, "2", "--list", "ok", "true", "--list", "bad", "sh", "-c", "exit 3", NULL };
        status = command_time(12, second);
        expect(file_exists(path), "resume: the interrupted run ran again");
        format_into(path, sizeof(path), "%s/time.tsv", directory);
        Strings rows = read_table_rows(path);
        int bad = 0;
        for (size_t i = 0; i < rows.count; i += 1)
        {
            bad += strncmp(rows.items[i], "bad\t", 4) == 0 && strstr(rows.items[i], "\t3\t") != NULL;
        }
        expect(status == 1 && rows.count == 4 && bad == 2, "resume: one row per run, and the recorded failure reported again");
        char* fan[] = { "uarch", "fanout", directory, "fan", "2", "0,0", "--", "true", NULL };
        status = command_fanout(8, fan);
        format_into(path, sizeof(path), "%s/logs/fan.r0.fanout", directory);
        unlink(path);
        char* fan_again[] = { "uarch", "fanout", directory, "fan", "2", "0,0", "--", "true", NULL };
        status |= command_fanout(8, fan_again);
        format_into(path, sizeof(path), "%s/fanout.tsv", directory);
        expect(status == 0 && read_table_rows(path).count == 2, "resume: fanout reruns an interrupted round once");
        char* failing[] = { "uarch", "fanout", directory, "fail", "1", "0", "--", "false", NULL };
        expect(command_fanout(8, failing) == 1, "resume: a failed fanout copy fails the command");
        char csv[BUSTER_UARCH_PATH_BYTES];
        format_into(csv, sizeof(csv), "%s/refused.csv", directory);
        write_text_atomic(csv, "# started on Wed Nov  4 20:56:07 2026\n\n");
        RunResult refused = { .status = 1 };
        expect(!run_failed(refused, csv), "resume: perf's refusal of an event is no failure");
        format_into(csv, sizeof(csv), "%s/counted.csv", directory);
        write_text_atomic(csv, "1072229971,,ls_not_halted_cyc:u,246891943,100.00,,\n");
        expect(run_failed(refused, csv), "resume: a counted run with a nonzero status failed");
        nftw(directory, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
    }
}

static int command_test(void)
{
    test_csv();
    test_values();
    test_parsers();
    test_topdown();
    test_resume();
    printf("uarch test: %d checks, %d failed\n", test_checks, test_failures);
    return test_failures != 0;
}

int main(int argc, char** argv)
{
    int result = 1;
    const char* command = argc > 1 ? argv[1] : "";
    if (strcmp(command, "time") == 0)
    {
        result = command_time(argc, argv);
    }
    else if (strcmp(command, "stat") == 0)
    {
        result = command_stat(argc, argv);
    }
    else if (strcmp(command, "sweep") == 0)
    {
        result = command_sweep(argc, argv);
    }
    else if (strcmp(command, "metrics") == 0)
    {
        result = command_metrics(argc, argv);
    }
    else if (strcmp(command, "fanout") == 0)
    {
        result = command_fanout(argc, argv);
    }
    else if (strcmp(command, "summarize") == 0)
    {
        result = command_summarize(argc, argv);
    }
    else if (strcmp(command, "stages") == 0)
    {
        result = command_stages(argc, argv);
    }
    else if (strcmp(command, "symbols") == 0)
    {
        result = command_symbols(argc, argv);
    }
    else if (strcmp(command, "branches") == 0)
    {
        result = command_branches(argc, argv);
    }
    else if (strcmp(command, "memory") == 0)
    {
        result = command_memory(argc, argv);
    }
    else if (strcmp(command, "topdown") == 0)
    {
        result = command_topdown(argc, argv);
    }
    else if (strcmp(command, "generate-generic") == 0)
    {
        result = command_generate_generic(argc, argv);
    }
    else if (strcmp(command, "test") == 0)
    {
        result = command_test();
    }
    else
    {
        fprintf(stderr, "usage: uarch time|stat|sweep|metrics|fanout|summarize|stages|symbols|branches|memory|topdown|generate-generic|test ...\n");
    }
    return result;
}
