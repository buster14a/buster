// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The decisions of main's nightly measurement backlog (#1207), as one fail-closed tool that both workflows run:
// .github/workflows/9700x.yml's `evidence` and `record` jobs and its 9700X job's window step, and
// .github/workflows/nightly-backfill.yml's coordinator (tools/ci/nightly.sh is the API and git glue around it).
//
//   nightly window --now EPOCH --bound SECONDS [--override REASON]   02:00-08:00 Europe/Madrid admission
//   nightly window-times --now EPOCH                                  the cycle's date, window start and deadline
//   nightly worker-window < SOURCE                                   immutable worker's declared window, or unknown
//   nightly hosted --commit SHA [--exclude RUN] < FACTS               reusable plain-row hosted qualification
//   nightly coverage --run RUN --auxiliary LIST < FACTS               a merge-queue candidate's full coverage, job by job
//   nightly stale --run RUN < CANDIDATES                              the invalidated merge-queue candidates' runs to cancel
//   nightly record --output FILE                                      a run's coverage record, from the environment
//   nightly harvest --ledger FILE --facts FILE --states FILE (--records DIR | --list)
//                                                                     completed runs' rows the ledger lacks (or which)
//   nightly plan --states FILE --ledger FILE --facts FILE --now EPOCH --main SHA --mode run|dry-run
//                --coordinator RUN [--pending FILE] [--refs FILE] [--refused FILE] --report FILE
//   nightly --self-test [--root DIR]
//
// FACTS are tab-separated lines that tools/ci/nightly.sh extracts from the Actions API with jq: `run`, `job` and `pr`
// lines (nightly_facts_parse). A record is one run's `key<TAB>value` lines in nightly_record_keys' order, written by
// its `record` job and uploaded as the coverage-record-ATTEMPT artifact; the ledger is the coordinator's rows, one per
// harvested run attempt and one per cycle's pinned snapshot. Every input is data: each field is checked against its
// kind (hexadecimal object names, decimal ids, closed enumerations, bounded tokens), and a line or record that fails is
// rejected whole, never half-read. Nothing here executes or interpolates what it reads.
//
// Policy (docs/ci.md, "Nightly main measurements"): a main state's measurement needs the plain hosted rows (both host
// architectures, every shard: the gate a pull request needs before the 9700X) of that exact commit, and native
// correctness on the 9700X in the same job as its measurements. A pull request's 9700X job measures without native
// correctness (#1272), so main's pushes and the scheduled runs and their dispatched workers carry it all. A state whose first-parent change is documentation alone
// needs none. Coverage is targeted (the selector's choice against the state's first parent), plain (every benchmark's
// plain run: pushes before the nightly mode) or full (everything, with the isolated samples and the oracle audit);
// targeted is what each state needs, full is the cycle's snapshot's, and plain satisfies neither. Reuse is by evidence
// alone: hosted rows by job conclusions of the same commit. Records from before #1272 may say that a push reused its
// pull request's native correctness and measurements; the harvest still reads them, and nothing writes them now.
//
// Layout: limits and enumerations; text and numbers; Europe/Madrid (nightly_madrid, nightly_window); facts
// (nightly_facts_parse, nightly_plain_verdict, nightly_hosted); records (nightly_record_keys, nightly_record_parse,
// nightly_record_from_environment, nightly_record_outcome); the ledger (nightly_ledger_parse,
// nightly_harvest); states and the plan (nightly_states_parse, nightly_fold, nightly_plan, nightly_report); the
// self-test; main. Administrative tooling outside the compiler: it reads a few thousand short lines, so it is scalar.

// tm_gmtoff and tm_zone, localtime_r, setenv, open_memstream and fmemopen.
#define _GNU_SOURCE

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_NIGHTLY_LINE 2048
#define BUSTER_NIGHTLY_FIELDS 16
#define BUSTER_NIGHTLY_VALUE 1536
#define BUSTER_NIGHTLY_NAME 256
#define BUSTER_NIGHTLY_TOKEN 64
#define BUSTER_NIGHTLY_HOST 128
#define BUSTER_NIGHTLY_REASON 160
#define BUSTER_NIGHTLY_RUNS 8192
#define BUSTER_NIGHTLY_JOBS 65536
#define BUSTER_NIGHTLY_PULLS 64
#define BUSTER_NIGHTLY_STATES 8192
#define BUSTER_NIGHTLY_PATHS 262144
#define BUSTER_NIGHTLY_ROWS 65536
#define BUSTER_NIGHTLY_CYCLES 1024
// Dispatches still hidden by the API, not the job's lifetime dispatch count.
#define BUSTER_NIGHTLY_PENDING 64
#define BUSTER_NIGHTLY_REFS 8192
#define BUSTER_NIGHTLY_FILE (64u << 20)
// The repository's GitHub id (AGENTS.md, "Repository identity"): pull requests from forks never qualify.
#define BUSTER_NIGHTLY_REPOSITORY 1421805693u
#define BUSTER_NIGHTLY_WORKFLOW ".github/workflows/9700x.yml"
#define BUSTER_NIGHTLY_HARDWARE_JOB "Native tests and benchmarks on Benchpress (Ryzen 7 9700X)"
// The required aggregate (9700x.yml's `qualification`), which passes a merge-queue candidate after its coverage check.
#define BUSTER_NIGHTLY_AGGREGATE_JOB "Integration qualification"
// Its name until #1430 renamed it, which earlier runs' jobs carry, and the alias job that reports it during the move.
#define BUSTER_NIGHTLY_AGGREGATE_JOB_BEFORE "Build and execute on Ryzen 7 9700X"
#define BUSTER_NIGHTLY_RECORD_FORMAT "buster-coverage-record-1"
#define BUSTER_NIGHTLY_LEDGER_FORMAT "# buster-nightly-ledger 1"
// The window, in Europe/Madrid local time: hardware work starts at or after 02:00 and ends by 08:00 (the owner's choice
// on 2026-11-05, widened from 04:00-07:00). Spain changes its clocks at 02:00 local time, so the window opens at the
// first instant the local time reaches 02:00 (nightly_madrid_first): 03:00 CEST when the clocks go forward, the first
// 02:00 when they go back.
#define BUSTER_NIGHTLY_WINDOW_START_HOUR 2
#define BUSTER_NIGHTLY_WINDOW_END_HOUR 8
// A cycle's snapshot is pinned no earlier than this hour of the evening before its window, Europe/Madrid local time
// whatever the clocks do that night, so that the coordinator's earlier schedules, there for GitHub's hours-late
// scheduled starts, pin nothing hours ahead.
#define BUSTER_NIGHTLY_PIN_HOUR 22
// A pull request's run older than this at its push is stale evidence: the host's unpinned packages (GCC, glibc, the
// kernel) could have changed since, and the nightly snapshot rechecks native correctness daily anyway.
// A record, pending dispatch or fact a step writes is never more than this long before the coordinator reads it.
#define BUSTER_NIGHTLY_PENDING_AGE 600

typedef enum NightlyEvent NightlyEvent;
enum NightlyEvent
{
    NIGHTLY_EVENT_PR,
    NIGHTLY_EVENT_PUSH,
    NIGHTLY_EVENT_DISPATCH,
    NIGHTLY_EVENT_SCHEDULE,
    // A merge-queue candidate (#1430): GitHub's temporary merge commit, which main is fast-forwarded to when the queue
    // merges it. Its run is that commit's own, as a push's is.
    NIGHTLY_EVENT_MERGE_GROUP,
    NIGHTLY_EVENT_OTHER,
    NIGHTLY_EVENT_COUNT
};
static const char* const nightly_event_names[] = { "pull_request", "push", "workflow_dispatch", "schedule", "merge_group", "other" };

typedef enum NightlyConclusion NightlyConclusion;
enum NightlyConclusion
{
    NIGHTLY_CONCLUSION_SUCCESS,
    NIGHTLY_CONCLUSION_FAILURE,
    NIGHTLY_CONCLUSION_CANCELLED,
    NIGHTLY_CONCLUSION_SKIPPED,
    NIGHTLY_CONCLUSION_TIMED_OUT,
    NIGHTLY_CONCLUSION_NONE,
    NIGHTLY_CONCLUSION_OTHER,
    NIGHTLY_CONCLUSION_COUNT
};
static const char* const nightly_conclusion_names[] = { "success", "failure", "cancelled", "skipped", "timed_out", "-", "other" };

typedef enum NightlyPurpose NightlyPurpose;
enum NightlyPurpose
{
    NIGHTLY_PURPOSE_PR,
    NIGHTLY_PURPOSE_PUSH,
    NIGHTLY_PURPOSE_SCHEDULE,
    NIGHTLY_PURPOSE_FULL,
    NIGHTLY_PURPOSE_BACKLOG,
    NIGHTLY_PURPOSE_SNAPSHOT,
    // A merge-queue candidate's run (#1430): the full hosted contract, no hardware. It measures nothing; it qualifies
    // the state's hosted rows, and its passing record is the state's per-merge correctness.
    NIGHTLY_PURPOSE_QUEUE,
    NIGHTLY_PURPOSE_COUNT
};
static const char* const nightly_purpose_names[] = { "pr", "push", "schedule", "full", "backlog", "snapshot", "queue" };

typedef enum NightlyHardware NightlyHardware;
enum NightlyHardware
{
    NIGHTLY_HARDWARE_ADMIT,
    NIGHTLY_HARDWARE_SKIP,
    NIGHTLY_HARDWARE_COUNT
};
static const char* const nightly_hardware_names[] = { "admit", "skip" };

typedef enum NightlyPlain NightlyPlain;
enum NightlyPlain
{
    // No conclusive plain-row result: none ran, or one was cancelled or skipped.
    NIGHTLY_PLAIN_NONE,
    NIGHTLY_PLAIN_OK,
    NIGHTLY_PLAIN_FAILED,
    NIGHTLY_PLAIN_COUNT
};
static const char* const nightly_plain_names[] = { "none", "ok", "failed" };

typedef enum NightlyCoverage NightlyCoverage;
enum NightlyCoverage
{
    NIGHTLY_COVERAGE_TARGETED,
    NIGHTLY_COVERAGE_PLAIN,
    NIGHTLY_COVERAGE_FULL,
    NIGHTLY_COVERAGE_NONE,
    NIGHTLY_COVERAGE_COUNT
};
static const char* const nightly_coverage_names[] = { "targeted", "plain", "full", "none" };

typedef enum NightlyOutcome NightlyOutcome;
enum NightlyOutcome
{
    NIGHTLY_OUTCOME_COMPLETE,
    NIGHTLY_OUTCOME_NOT_REQUIRED,
    // Hardware wasn't admitted (outside the window, or deferred to measure later): nothing ran, nothing failed.
    NIGHTLY_OUTCOME_DEFERRED,
    // A push whose pull request's run qualified it but whose measurements it couldn't reuse: the backlog's. Only
    // records from before #1272 have it: pull requests no longer run native correctness, so a push never reuses theirs.
    NIGHTLY_OUTCOME_PENDING,
    NIGHTLY_OUTCOME_BLOCKED_HOSTED,
    NIGHTLY_OUTCOME_BLOCKED_NATIVE,
    NIGHTLY_OUTCOME_FAILED,
    NIGHTLY_OUTCOME_INTERRUPTED,
    NIGHTLY_OUTCOME_REJECTED,
    // A row whose record disagreed with the API's facts: counted as nothing.
    NIGHTLY_OUTCOME_INVALID,
    NIGHTLY_OUTCOME_COUNT
};
static const char* const nightly_outcome_names[] = { "complete", "not-required", "deferred", "pending", "blocked-hosted", "blocked-native", "failed", "interrupted", "rejected", "invalid" };

#define BUSTER_NIGHTLY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

// The self-test's refusals are expected: it silences the diagnostics they would print.
static bool nightly_quiet = false;

static void nightly_fatal(const char* message, const char* detail)
{
    fprintf(stderr, "nightly: %s%s%s\n", message, detail != NULL ? ": " : "", detail != NULL ? detail : "");
    exit(2);
}

static void* nightly_allocate(size_t count, size_t size)
{
    void* result = calloc(count, size);
    if (result == NULL)
    {
        nightly_fatal("out of memory", NULL);
    }
    return result;
}

// --- Text and numbers ---

static size_t nightly_find(const char* value, const char* const* names, size_t count)
{
    size_t found = count;
    for (size_t i = 0; value != NULL && i < count; i += 1)
    {
        if (found == count && strcmp(value, names[i]) == 0)
        {
            found = i;
        }
    }
    return found;
}

static bool nightly_hex(const char* text, size_t length)
{
    bool valid = text != NULL && strlen(text) == length;
    for (size_t i = 0; valid && i < length; i += 1)
    {
        valid = (text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f');
    }
    return valid;
}

static bool nightly_sha(const char* text)
{
    return nightly_hex(text, 40);
}

static bool nightly_number(const char* text, uint64_t limit, uint64_t* value)
{
    bool valid = text != NULL && *text != 0 && strlen(text) <= 20;
    uint64_t result = 0;
    for (size_t i = 0; valid && text[i] != 0; i += 1)
    {
        const unsigned int digit = (unsigned char)text[i] - (unsigned int)'0';
        valid = digit <= 9 && result <= limit / 10 && (result != limit / 10 || digit <= limit % 10);
        result = valid ? result * 10 + digit : result;
    }
    *value = result;
    return valid;
}

static bool nightly_id(const char* text, uint64_t* value)
{
    return nightly_number(text, UINT64_MAX, value) && *value != 0;
}

static bool nightly_epoch(const char* text, int64_t* value)
{
    uint64_t number = 0;
    // Through 9999: beyond any timestamp the API gives.
    const bool valid = nightly_number(text, 253402300799u, &number);
    *value = (int64_t)number;
    return valid;
}

// A bounded token: letters, digits and . _ : - only.
static bool nightly_token(const char* text, size_t capacity)
{
    const size_t length = text != NULL ? strlen(text) : 0;
    bool valid = length != 0 && length < capacity;
    for (size_t i = 0; valid && i < length; i += 1)
    {
        const char c = text[i];
        valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '-';
    }
    return valid;
}

// Free text that a person wrote (an override's reason): printable ASCII but tab, backslash, backquote and the
// characters Markdown or the workflow commands would read, each replaced by '_', bounded. Empty stays empty.
static void nightly_sanitize(const char* text, char* output, size_t capacity)
{
    size_t length = 0;
    for (size_t i = 0; text != NULL && text[i] != 0 && length + 1 < capacity; i += 1)
    {
        const unsigned char c = (unsigned char)text[i];
        const bool plain = c >= 0x20 && c < 0x7f && c != '\\' && c != '`' && c != '|' && c != '<' && c != '>' && c != '[' && c != ']' && c != ':';
        output[length] = plain ? (char)c : '_';
        length += 1;
    }
    output[length] = 0;
}

static void nightly_copy(char* output, size_t capacity, const char* text)
{
    const size_t length = strlen(text);
    if (length >= capacity)
    {
        nightly_fatal("value too long", text);
    }
    memcpy(output, text, length + 1);
}

// Splits a NUL-terminated line, its newline removed, at tabs. Returns the field count, or capacity + 1 when it has more.
static size_t nightly_split(char* line, char** fields, size_t capacity)
{
    size_t count = 1;
    fields[0] = line;
    for (char* c = line; *c != 0 && count <= capacity; c += 1)
    {
        if (*c == '\t')
        {
            *c = 0;
            if (count < capacity)
            {
                fields[count] = c + 1;
            }
            count += 1;
        }
    }
    return count;
}

typedef struct NightlyText NightlyText;
struct NightlyText
{
    char* data;
    size_t length;
};

static NightlyText nightly_read(const char* path)
{
    NightlyText text = { 0 };
    FILE* file = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (file == NULL)
    {
        nightly_fatal("cannot open", path);
    }
    size_t capacity = 1 << 16;
    text.data = nightly_allocate(capacity + 1, 1);
    size_t read = 0;
    while ((read = fread(text.data + text.length, 1, capacity - text.length, file)) != 0)
    {
        text.length += read;
        if (text.length == capacity)
        {
            if (capacity >= BUSTER_NIGHTLY_FILE)
            {
                nightly_fatal("input too large", path);
            }
            capacity *= 2;
            char* grown = realloc(text.data, capacity + 1);
            if (grown == NULL)
            {
                nightly_fatal("out of memory", NULL);
            }
            text.data = grown;
        }
    }
    if (ferror(file))
    {
        nightly_fatal("cannot read", path);
    }
    if (file != stdin)
    {
        fclose(file);
    }
    text.data[text.length] = 0;
    return text;
}

// The next line from *offset into line (NUL-terminated, without its newline). False at the end; a line too long, with a
// NUL, or the last one without its newline is invalid, so that a truncated input can't pass as a shorter one.
static bool nightly_next_line(NightlyText text, size_t* offset, char* line, bool* valid)
{
    bool found = *offset < text.length;
    if (found)
    {
        const char* start = text.data + *offset;
        const char* end = memchr(start, '\n', text.length - *offset);
        const size_t length = end != NULL ? (size_t)(end - start) : text.length - *offset;
        *valid = end != NULL && length < BUSTER_NIGHTLY_LINE && memchr(start, 0, length) == NULL;
        const size_t copied = length < BUSTER_NIGHTLY_LINE ? length : BUSTER_NIGHTLY_LINE - 1;
        memcpy(line, start, copied);
        line[copied] = 0;
        *offset += length + (end != NULL ? 1 : 0);
    }
    return found;
}

// --- Europe/Madrid ---

// The tz database's Europe/Madrid (main sets TZ once). A zone that isn't CET/CEST at +1/+2 hours means no tz database
// (glibc then reads the name as UTC): the window can't be checked, so nothing is admitted (nightly_madrid fails).
typedef struct NightlyLocal NightlyLocal;
struct NightlyLocal
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    long offset;
    char zone[8];
};

static void nightly_use_madrid(void)
{
    if (setenv("TZ", "Europe/Madrid", 1) != 0)
    {
        nightly_fatal("cannot set TZ", NULL);
    }
    tzset();
}

static bool nightly_madrid(int64_t utc, NightlyLocal* local)
{
    const time_t instant = (time_t)utc;
    struct tm parts;
    bool valid = localtime_r(&instant, &parts) != NULL;
    valid = valid && parts.tm_zone != NULL && ((parts.tm_gmtoff == 3600 && strcmp(parts.tm_zone, "CET") == 0) || (parts.tm_gmtoff == 7200 && strcmp(parts.tm_zone, "CEST") == 0));
    if (valid)
    {
        local->year = parts.tm_year + 1900;
        local->month = parts.tm_mon + 1;
        local->day = parts.tm_mday;
        local->hour = parts.tm_hour;
        local->minute = parts.tm_min;
        local->offset = parts.tm_gmtoff;
        nightly_copy(local->zone, sizeof(local->zone), parts.tm_zone);
    }
    return valid;
}

// The instant of a Madrid local date and time, which must exist (00:00, 08:00, 12:00 and 22:00 always do: Spain changes
// its clocks at 02:00 and 03:00); day may overflow the month, as mktime normalizes it. False when the zone isn't Madrid's.
static bool nightly_madrid_instant(int year, int month, int day, int hour, int64_t* instant)
{
    struct tm parts = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_isdst = -1,
    };
    const time_t result = mktime(&parts);
    NightlyLocal check;
    *instant = (int64_t)result;
    return result != (time_t)-1 && nightly_madrid(*instant, &check) && check.hour == hour && check.minute == 0;
}

// The first instant of a Madrid local date whose local time is at or after hour:00, which exists every day: hour:00 itself
// on an ordinary day, the hour after the gap when the clocks go forward, and the first of the two when they go back. The
// date's midnight always exists, and its clock changes fall within an hour of hour:00, so a minute's scan of the three
// hours around it finds it.
static bool nightly_madrid_first(int year, int month, int day, int hour, int64_t* instant)
{
    int64_t midnight = 0;
    NightlyLocal date;
    bool valid = nightly_madrid_instant(year, month, day, 0, &midnight) && nightly_madrid(midnight, &date);
    bool found = false;
    for (int64_t t = midnight + (int64_t)(hour - 1) * 3600; valid && !found && t <= midnight + (int64_t)(hour + 1) * 3600; t += 60)
    {
        NightlyLocal local;
        valid = nightly_madrid(t, &local);
        found = valid && local.year == date.year && local.month == date.month && local.day == date.day && local.hour >= hour;
        *instant = found ? t : *instant;
    }
    return valid && found;
}

typedef struct NightlyWindow NightlyWindow;
struct NightlyWindow
{
    bool valid;
    bool open;
    NightlyLocal local;
    // The cycle: the window that the instant prepares for (from noon) or is in or just ended (before noon).
    char cycle[11];
    // 22:00 Europe/Madrid of the evening before: from then on, a step pins the cycle's snapshot.
    int64_t pin;
    int64_t start;
    int64_t deadline;
    const char* reason;
};

// Whether work that may take `bound` seconds can start at `now` and end by the cycle's 08:00.
static NightlyWindow nightly_window(int64_t now, int64_t bound)
{
    NightlyWindow window = { 0 };
    window.valid = bound >= 0 && nightly_madrid(now, &window.local);
    NightlyLocal cycle = window.local;
    if (window.valid && window.local.hour >= 12)
    {
        int64_t tomorrow = 0;
        window.valid = nightly_madrid_instant(window.local.year, window.local.month, window.local.day + 1, 12, &tomorrow) && nightly_madrid(tomorrow, &cycle);
    }
    window.valid = window.valid && nightly_madrid_instant(cycle.year, cycle.month, cycle.day - 1, BUSTER_NIGHTLY_PIN_HOUR, &window.pin);
    window.valid = window.valid && nightly_madrid_first(cycle.year, cycle.month, cycle.day, BUSTER_NIGHTLY_WINDOW_START_HOUR, &window.start);
    window.valid = window.valid && nightly_madrid_instant(cycle.year, cycle.month, cycle.day, BUSTER_NIGHTLY_WINDOW_END_HOUR, &window.deadline);
    if (window.valid)
    {
        snprintf(window.cycle, sizeof(window.cycle), "%04d-%02d-%02d", cycle.year, cycle.month, cycle.day);
        window.open = now >= window.start && now < window.deadline && now + bound <= window.deadline;
        window.reason = now < window.start ? "before 02:00 Europe/Madrid" : now >= window.deadline ? "at or after 08:00 Europe/Madrid" : !window.open ? "too late to finish by 08:00 Europe/Madrid" : "inside 02:00-08:00 Europe/Madrid";
    }
    else
    {
        window.reason = "Europe/Madrid time unavailable (no tz database?)";
    }
    return window;
}

// The two worker contracts in main's history. Read their literal declarations as data, never compile or run a
// historical tool merely to discover its policy. An absent, repeated or changed declaration is unknown, not today's
// window: the coordinator reports that state and keeps it pending until its contract is understood.
static bool nightly_worker_hours(uint64_t start, uint64_t end)
{
    return (start == 4 && end == 7) || (start == 2 && end == 8);
}

static bool nightly_worker_source(NightlyText text, uint32_t* start, uint32_t* end)
{
    static const char* const prefixes[] = {
        "#define BUSTER_NIGHTLY_WINDOW_START_HOUR",
        "#define BUSTER_NIGHTLY_WINDOW_END_HOUR",
    };
    uint64_t hours[2] = { 0 };
    bool seen[2] = { false };
    bool valid = true;
    size_t offset = 0;
    while (valid && offset < text.length)
    {
        const char* line = text.data + offset;
        const char* newline = memchr(line, '\n', text.length - offset);
        const size_t length = newline != NULL ? (size_t)(newline - line) : text.length - offset;
        valid = newline != NULL && memchr(line, 0, length) == NULL;
        for (uint32_t i = 0; valid && i < 2; i += 1)
        {
            const size_t prefix = strlen(prefixes[i]);
            if (length >= prefix && memcmp(line, prefixes[i], prefix) == 0)
            {
                // Only the known literal form; expressions, comments and duplicate definitions need review.
                const size_t digits = length > prefix ? length - prefix - 1 : 0;
                char number[3] = { 0 };
                valid = !seen[i] && length > prefix && line[prefix] == ' ' && digits > 0 && digits < sizeof(number);
                if (valid)
                {
                    memcpy(number, line + prefix + 1, digits);
                    valid = nightly_number(number, 23, &hours[i]);
                    seen[i] = true;
                }
            }
        }
        offset += length + (newline != NULL ? 1 : 0);
    }
    valid = valid && seen[0] && seen[1] && nightly_worker_hours(hours[0], hours[1]);
    *start = valid ? (uint32_t)hours[0] : 0;
    *end = valid ? (uint32_t)hours[1] : 0;
    return valid;
}

// The civil date of a day count since 1970-01-01 (H. Hinnant's algorithm), for the self-test's independent rule.
static void nightly_civil(int64_t days, int* year, int* month, int* day)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t of_era = days - era * 146097;
    const int64_t year_of_era = (of_era - of_era / 1460 + of_era / 36524 - of_era / 146096) / 365;
    const int64_t of_year = of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const int64_t shifted = (5 * of_year + 2) / 153;
    *day = (int)(of_year - (153 * shifted + 2) / 5 + 1);
    *month = (int)(shifted < 10 ? shifted + 3 : shifted - 9);
    *year = (int)(year_of_era + era * 400 + (*month <= 2 ? 1 : 0));
}

static int64_t nightly_days(int year, int month, int day)
{
    const int64_t y = month <= 2 ? year - 1 : year;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t of_era = y - era * 400;
    const int64_t of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const int64_t days = of_era * 365 + of_era / 4 - of_era / 100 + of_year;
    return era * 146097 + days - 719468;
}

// The EU's rule, which Spain follows: summer time from the last Sunday of March to the last Sunday of October, both at
// 01:00 UTC. The self-test checks the tz database against it, so that a change of either is noticed.
static long nightly_eu_offset(int64_t utc)
{
    int year = 0;
    int month = 0;
    int day = 0;
    nightly_civil(utc >= 0 ? utc / 86400 : (utc - 86399) / 86400, &year, &month, &day);
    int64_t edges[2] = { 0 };
    const int months[2] = { 3, 10 };
    for (int i = 0; i < 2; i += 1)
    {
        const int64_t last = nightly_days(year, months[i], 31);
        // 1970-01-01 was a Thursday: (days + 4) % 7 is the weekday, Sunday 0.
        const int64_t sunday = last - (((last + 4) % 7) + 7) % 7;
        edges[i] = sunday * 86400 + 3600;
    }
    return utc >= edges[0] && utc < edges[1] ? 7200 : 3600;
}

// --- Facts: the Actions API's runs, jobs and pull requests, as nightly.sh's jq writes them ---

typedef struct NightlyRun NightlyRun;
struct NightlyRun
{
    uint64_t id;
    uint32_t attempt;
    NightlyEvent event;
    bool completed;
    NightlyConclusion conclusion;
    // The run's workflow is BUSTER_NIGHTLY_WORKFLOW.
    bool workflow;
    char head[41];
    char branch[BUSTER_NIGHTLY_NAME];
    int64_t created;
    int64_t updated;
    uint64_t repository;
    // A nightly dispatch's run name, `Nightly PURPOSE (admit|defer) of SHA [REQUEST]` (9700x.yml's run-name).
    bool nightly;
    NightlyPurpose purpose;
    bool admit;
    char target[41];
    char request[BUSTER_NIGHTLY_TOKEN];
};

typedef struct NightlyJob NightlyJob;
struct NightlyJob
{
    uint64_t run;
    bool completed;
    NightlyConclusion conclusion;
    char name[BUSTER_NIGHTLY_NAME];
};

typedef struct NightlyPull NightlyPull;
struct NightlyPull
{
    uint64_t number;
    bool merged;
    bool main_base;
    bool has_merge;
    char merge[41];
    char head[41];
    uint64_t repository;
};

typedef struct NightlyFacts NightlyFacts;
struct NightlyFacts
{
    NightlyRun* runs;
    uint32_t run_count;
    NightlyJob* jobs;
    uint32_t job_count;
    NightlyPull* pulls;
    uint32_t pull_count;
};

static NightlyFacts nightly_facts_new(void)
{
    NightlyFacts facts = {
        .runs = nightly_allocate(BUSTER_NIGHTLY_RUNS, sizeof(NightlyRun)),
        .jobs = nightly_allocate(BUSTER_NIGHTLY_JOBS, sizeof(NightlyJob)),
        .pulls = nightly_allocate(BUSTER_NIGHTLY_PULLS, sizeof(NightlyPull)),
    };
    return facts;
}

static void nightly_facts_free(NightlyFacts* facts)
{
    free(facts->runs);
    free(facts->jobs);
    free(facts->pulls);
}

// `Nightly PURPOSE (admit|defer) of SHA [REQUEST]`, the whole title, or false.
static bool nightly_title(const char* title, NightlyRun* run)
{
    char purpose[16] = { 0 };
    char hardware[8] = { 0 };
    char target[41] = { 0 };
    char request[BUSTER_NIGHTLY_TOKEN] = { 0 };
    int consumed = 0;
    bool valid = strncmp(title, "Nightly ", 8) == 0 && sscanf(title, "Nightly %15[a-z] (%7[a-z]) of %40[0-9a-f] [%63[A-Za-z0-9._:-]]%n", purpose, hardware, target, request, &consumed) == 4;
    valid = valid && consumed > 0 && title[consumed] == 0 && nightly_sha(target) && nightly_token(request, sizeof(request));
    // sscanf's spaces match any run of white space, or none: the title must be the canonical one, byte for byte.
    char canonical[BUSTER_NIGHTLY_NAME];
    snprintf(canonical, sizeof(canonical), "Nightly %s (%s) of %s [%s]", purpose, hardware, target, request);
    valid = valid && strcmp(canonical, title) == 0;
    const size_t found = nightly_find(purpose, nightly_purpose_names, NIGHTLY_PURPOSE_COUNT);
    valid = valid && (found == NIGHTLY_PURPOSE_BACKLOG || found == NIGHTLY_PURPOSE_SNAPSHOT) && (strcmp(hardware, "admit") == 0 || strcmp(hardware, "defer") == 0);
    if (valid)
    {
        run->nightly = true;
        run->purpose = (NightlyPurpose)found;
        run->admit = strcmp(hardware, "admit") == 0;
        nightly_copy(run->target, sizeof(run->target), target);
        nightly_copy(run->request, sizeof(run->request), request);
    }
    return valid;
}

static NightlyConclusion nightly_conclusion(const char* text)
{
    const size_t found = nightly_find(text, nightly_conclusion_names, NIGHTLY_CONCLUSION_COUNT);
    return found < NIGHTLY_CONCLUSION_COUNT ? (NightlyConclusion)found : NIGHTLY_CONCLUSION_OTHER;
}

static NightlyRun* nightly_find_run(const NightlyFacts* facts, uint64_t id)
{
    NightlyRun* found = NULL;
    for (uint32_t i = 0; i < facts->run_count; i += 1)
    {
        found = facts->runs[i].id == id ? &facts->runs[i] : found;
    }
    return found;
}

// Reads `run`, `job` and `pr` lines. False on the first invalid line (reported to stderr), or a repeated run, job or
// pull request: a fact counted twice could make a cancelled shard look like a second, successful one.
static bool nightly_facts_parse(NightlyFacts* facts, NightlyText text)
{
    size_t offset = 0;
    char line[BUSTER_NIGHTLY_LINE];
    bool valid = true;
    bool line_valid = true;
    uint32_t number = 0;
    while (valid && nightly_next_line(text, &offset, line, &line_valid))
    {
        number += 1;
        char* fields[BUSTER_NIGHTLY_FIELDS];
        const size_t count = line_valid ? nightly_split(line, fields, BUSTER_NIGHTLY_FIELDS) : 0;
        valid = line_valid && count >= 1;
        if (valid && strcmp(fields[0], "run") == 0)
        {
            NightlyRun run = { 0 };
            uint64_t attempt = 0;
            const char* status = count == 13 ? fields[4] : "";
            valid = count == 13 && facts->run_count < BUSTER_NIGHTLY_RUNS && nightly_id(fields[1], &run.id) && nightly_number(fields[2], 1000, &attempt) && attempt != 0;
            run.attempt = (uint32_t)attempt;
            run.event = (NightlyEvent)nightly_find(fields[3], nightly_event_names, NIGHTLY_EVENT_OTHER);
            run.completed = strcmp(status, "completed") == 0;
            valid = valid && (run.completed || strcmp(status, "queued") == 0 || strcmp(status, "in_progress") == 0 || strcmp(status, "waiting") == 0 || strcmp(status, "requested") == 0 || strcmp(status, "pending") == 0);
            run.conclusion = valid ? nightly_conclusion(fields[5]) : NIGHTLY_CONCLUSION_NONE;
            valid = valid && nightly_sha(fields[6]) && strlen(fields[8]) < sizeof(run.branch) && strlen(fields[12]) < BUSTER_NIGHTLY_NAME;
            valid = valid && nightly_epoch(fields[9], &run.created) && nightly_epoch(fields[10], &run.updated) && nightly_number(fields[11], UINT64_MAX, &run.repository);
            valid = valid && nightly_find_run(facts, run.id) == NULL;
            if (valid)
            {
                run.workflow = strcmp(fields[7], BUSTER_NIGHTLY_WORKFLOW) == 0;
                nightly_copy(run.head, sizeof(run.head), fields[6]);
                nightly_copy(run.branch, sizeof(run.branch), fields[8]);
                nightly_title(fields[12], &run);
                facts->runs[facts->run_count] = run;
                facts->run_count += 1;
            }
        }
        else if (valid && strcmp(fields[0], "job") == 0)
        {
            NightlyJob job = { 0 };
            valid = count == 5 && facts->job_count < BUSTER_NIGHTLY_JOBS && nightly_id(fields[1], &job.run) && strlen(fields[4]) < sizeof(job.name) && fields[4][0] != 0;
            valid = valid && (strcmp(fields[2], "completed") == 0 || strcmp(fields[2], "queued") == 0 || strcmp(fields[2], "in_progress") == 0 || strcmp(fields[2], "waiting") == 0 || strcmp(fields[2], "pending") == 0);
            for (uint32_t i = 0; valid && i < facts->job_count; i += 1)
            {
                valid = facts->jobs[i].run != job.run || strcmp(facts->jobs[i].name, fields[4]) != 0;
            }
            if (valid)
            {
                job.completed = strcmp(fields[2], "completed") == 0;
                job.conclusion = nightly_conclusion(fields[3]);
                nightly_copy(job.name, sizeof(job.name), fields[4]);
                facts->jobs[facts->job_count] = job;
                facts->job_count += 1;
            }
        }
        else if (valid && strcmp(fields[0], "pr") == 0)
        {
            NightlyPull pull = { 0 };
            valid = count == 7 && facts->pull_count < BUSTER_NIGHTLY_PULLS && nightly_id(fields[1], &pull.number) && (strcmp(fields[2], "0") == 0 || strcmp(fields[2], "1") == 0);
            valid = valid && (nightly_sha(fields[4]) || strcmp(fields[4], "-") == 0) && nightly_sha(fields[5]) && nightly_number(fields[6], UINT64_MAX, &pull.repository);
            for (uint32_t i = 0; valid && i < facts->pull_count; i += 1)
            {
                valid = facts->pulls[i].number != pull.number;
            }
            if (valid)
            {
                pull.merged = strcmp(fields[2], "1") == 0;
                pull.main_base = strcmp(fields[3], "main") == 0;
                pull.has_merge = strcmp(fields[4], "-") != 0;
                nightly_copy(pull.merge, sizeof(pull.merge), pull.has_merge ? fields[4] : "");
                nightly_copy(pull.head, sizeof(pull.head), fields[5]);
                facts->pulls[facts->pull_count] = pull;
                facts->pull_count += 1;
            }
        }
        else
        {
            valid = false;
        }
        if (!valid && !nightly_quiet)
        {
            fprintf(stderr, "nightly: invalid fact on line %u\n", number);
        }
    }
    return valid;
}

static const NightlyJob* nightly_find_job(const NightlyFacts* facts, uint64_t run, const char* name)
{
    const NightlyJob* found = NULL;
    for (uint32_t i = 0; i < facts->job_count; i += 1)
    {
        found = facts->jobs[i].run == run && strcmp(facts->jobs[i].name, name) == 0 ? &facts->jobs[i] : found;
    }
    return found;
}

// A job's own name: a called workflow's job is named `CALLER / JOB` (each hosted row is hosted-correctness.yml, #2032),
// and its own is the part after the last ` / `.
static const char* nightly_job_own_name(const char* name)
{
    const char* own = name;
    for (const char* at = strstr(name, " / "); at != NULL; at = strstr(at + 3, " / "))
    {
        own = at + 3;
    }
    return own;
}

// The hosted rows' sanitizers, in 9700x.yml's matrix order; a row's index is its architecture's times their count plus
// its sanitizer's.
static const char* const nightly_sanitizer_names[] = { "none", "address", "thread", "memory" };
#define BUSTER_NIGHTLY_SANITIZERS 4u

// A hosted test row's job name, `Tests on ... (Clang V, ARCH, SANITIZER, OLEVEL[, shard K of N])`, its own name whatever
// its caller's: its architecture (0 x86-64, 1 native), its row (the architecture's and the sanitizer's index, or
// BUSTER_NIGHTLY_SANITIZERS twice over for another sanitizer or an optimization other than -O3), shard and shard count,
// and whether it is a plain row (no sanitizer, -O3). False for other jobs, such as the job that builds a row's compiler
// once for its shards.
static bool nightly_test_row(const char* full, uint32_t* architecture, uint32_t* row, uint32_t* shard, uint32_t* shards, bool* plain)
{
    const char* name = nightly_job_own_name(full);
    const size_t length = strlen(name);
    const char* open = strrchr(name, '(');
    bool valid = strncmp(name, "Tests on ", 9) == 0 && open != NULL && length != 0 && name[length - 1] == ')';
    char inner[BUSTER_NIGHTLY_NAME] = { 0 };
    char* parts[6] = { NULL };
    uint32_t count = 0;
    if (valid)
    {
        const size_t size = (size_t)(name + length - 1 - (open + 1));
        memcpy(inner, open + 1, size);
        inner[size] = 0;
        char* at = inner;
        while (at != NULL && count < 6)
        {
            parts[count] = at;
            count += 1;
            char* next = strstr(at, ", ");
            if (next != NULL)
            {
                *next = 0;
                next += 2;
            }
            at = next;
        }
        valid = at == NULL && (count == 4 || count == 5) && strncmp(parts[0], "Clang ", 6) == 0;
    }
    uint64_t k = 1;
    uint64_t n = 1;
    if (valid && count == 5)
    {
        char k_text[8] = { 0 };
        char n_text[8] = { 0 };
        int consumed = 0;
        valid = sscanf(parts[4], "shard %7[0-9] of %7[0-9]%n", k_text, n_text, &consumed) == 2 && parts[4][consumed] == 0;
        valid = valid && nightly_number(k_text, 64, &k) && nightly_number(n_text, 64, &n) && k >= 1 && k <= n;
    }
    if (valid)
    {
        const bool x86 = strcmp(parts[1], "x86-64") == 0;
        valid = x86 || strcmp(parts[1], "native") == 0;
        *architecture = x86 ? 0 : 1;
        *shard = (uint32_t)k;
        *shards = (uint32_t)n;
        *plain = strcmp(parts[2], "none") == 0 && strcmp(parts[3], "O3") == 0;
        const size_t sanitizer = strcmp(parts[3], "O3") == 0 ? nightly_find(parts[2], nightly_sanitizer_names, BUSTER_NIGHTLY_SANITIZERS) : BUSTER_NIGHTLY_SANITIZERS;
        *row = sanitizer < BUSTER_NIGHTLY_SANITIZERS ? *architecture * BUSTER_NIGHTLY_SANITIZERS + (uint32_t)sanitizer : 2 * BUSTER_NIGHTLY_SANITIZERS;
    }
    return valid;
}

static bool nightly_test_job(const char* full, uint32_t* architecture, uint32_t* shard, uint32_t* shards, bool* plain)
{
    uint32_t row = 0;
    return nightly_test_row(full, architecture, &row, shard, shards, plain);
}

// The run's plain hosted rows: ok when every shard 1..N of both architectures succeeded (one N throughout), failed
// when any failed or timed out (a genuine failure: fail-fast's cancellations beside it don't hide it), and none
// otherwise (none ran, or one was cancelled or skipped, or the shards disagree). Sanitizer rows don't count: the
// nightly policy measures behind the plain rows, which are the pull request's own gate (docs/ci.md).
static NightlyPlain nightly_plain_verdict(const NightlyFacts* facts, uint64_t run)
{
    uint64_t succeeded[2] = { 0 };
    uint32_t shards = 0;
    bool consistent = true;
    bool failed = false;
    bool inconclusive = false;
    for (uint32_t i = 0; i < facts->job_count; i += 1)
    {
        const NightlyJob* job = &facts->jobs[i];
        uint32_t architecture = 0;
        uint32_t shard = 0;
        uint32_t count = 0;
        bool plain = false;
        if (job->run == run && nightly_test_job(job->name, &architecture, &shard, &count, &plain) && plain)
        {
            consistent = consistent && (shards == 0 || shards == count);
            shards = count;
            const bool success = job->completed && job->conclusion == NIGHTLY_CONCLUSION_SUCCESS;
            failed = failed || (job->completed && (job->conclusion == NIGHTLY_CONCLUSION_FAILURE || job->conclusion == NIGHTLY_CONCLUSION_TIMED_OUT));
            inconclusive = inconclusive || !success;
            succeeded[architecture] |= success ? (uint64_t)1 << (shard - 1) : 0;
        }
    }
    const uint64_t every = shards == 64 ? UINT64_MAX : ((uint64_t)1 << shards) - 1;
    const bool complete = consistent && shards != 0 && succeeded[0] == every && succeeded[1] == every && !inconclusive;
    return failed && consistent ? NIGHTLY_PLAIN_FAILED : complete ? NIGHTLY_PLAIN_OK : NIGHTLY_PLAIN_NONE;
}

typedef struct NightlyHosted NightlyHosted;
struct NightlyHosted
{
    NightlyPlain verdict;
    uint64_t run;
};

// The commit's hosted qualification from its own completed push, dispatch or merge-queue runs of the workflow, whose
// definition is the commit's own (a pull request's run tests a merge commit instead, so none counts; a merge-queue
// candidate's run tests the very commit main is fast-forwarded to, #1430): the newest conclusive one decides, so that a
// later genuine failure isn't hidden by an earlier pass. `exclude` is the asking run itself.
static NightlyHosted nightly_hosted(const NightlyFacts* facts, const char* commit, uint64_t exclude)
{
    NightlyHosted hosted = { 0 };
    for (uint32_t i = 0; i < facts->run_count; i += 1)
    {
        const NightlyRun* run = &facts->runs[i];
        const bool own = run->event == NIGHTLY_EVENT_PUSH || run->event == NIGHTLY_EVENT_DISPATCH || run->event == NIGHTLY_EVENT_MERGE_GROUP;
        const bool eligible = run->workflow && run->completed && run->id != exclude && strcmp(run->head, commit) == 0 && own;
        const NightlyPlain verdict = eligible ? nightly_plain_verdict(facts, run->id) : NIGHTLY_PLAIN_NONE;
        if (verdict != NIGHTLY_PLAIN_NONE && run->id > hosted.run)
        {
            hosted.verdict = verdict;
            hosted.run = run->id;
        }
    }
    return hosted;
}

// --- A merge-queue candidate's coverage, job by job (#1430) ---

// The fuzz campaigns (9700x.yml's fuzz matrix) and the vector pool's configurations (the test job's one call of the
// row workflow for the vector row, where the sanitizer rows run).
static const char* const nightly_fuzz_entries[] = { "frontend", "elf-override", "crash-replay-proof" };
#define BUSTER_NIGHTLY_VECTOR_POOL "none address thread memory"
#define BUSTER_NIGHTLY_ROWS_FULL (2u * BUSTER_NIGHTLY_SANITIZERS)

// The auxiliary workflows a candidate calls where its change touches their pull_request paths (tools/ci/changes.c's
// changes_auxiliary, the same names in its order), each by 9700x.yml's job `Auxiliary (NAME.yml)`, whose called jobs
// the API names `Auxiliary (NAME.yml) / JOB`.
static const char* const nightly_auxiliary_names[] = { "build", "build-pgo", "selfhost", "tier1-problem" };

// The selector's auxiliary output: `,` or `,NAME,NAME,` of nightly_auxiliary_names, each at most once, in their
// order. Sets each one's flag; false for anything else, an empty list (no selector) included.
static bool nightly_auxiliary_list(const char* list, bool* called)
{
    bool valid = list != NULL && list[0] == ',';
    const char* at = valid ? list + 1 : NULL;
    uint32_t next = 0;
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(nightly_auxiliary_names); i += 1)
    {
        called[i] = false;
    }
    while (valid && *at != 0)
    {
        const char* comma = strchr(at, ',');
        bool known = false;
        for (uint32_t i = next; comma != NULL && i < BUSTER_NIGHTLY_COUNT(nightly_auxiliary_names) && !known; i += 1)
        {
            known = strlen(nightly_auxiliary_names[i]) == (size_t)(comma - at) && strncmp(at, nightly_auxiliary_names[i], (size_t)(comma - at)) == 0;
            called[i] = known;
            next = known ? i + 1 : next;
        }
        valid = known;
        at = known ? comma + 1 : at;
    }
    return valid;
}

// The run's full hosted contract from its own jobs, which a test job's result alone can't show (a skipped shard or row
// leaves it green): every shard 1..N of each row (x86-64 and native, each sanitizer, -O3; one N per row), the vector
// pool's verdict and each fuzz campaign, every one completed and successful, and no shard of a row that didn't succeed
// (one beyond N, say, or a second attempt's). Each auxiliary workflow AUXILIARY names (the selector's list) must have
// run a job and every job successful; one it doesn't name must have run none. Writes a line per row, the vector verdict,
// each campaign and each auxiliary workflow, then `coverage complete` or `coverage incomplete`. Job names are their own,
// whatever their callers' (each row is a call of the row workflow); a skipped job's name keeps its unexpanded
// expressions, which no row's pattern matches.
static bool nightly_coverage(const NightlyFacts* facts, uint64_t run, const char* auxiliary, FILE* output)
{
    bool called[BUSTER_NIGHTLY_COUNT(nightly_auxiliary_names)];
    const bool listed = nightly_auxiliary_list(auxiliary, called);
    uint32_t auxiliary_jobs[BUSTER_NIGHTLY_COUNT(nightly_auxiliary_names)] = { 0 };
    uint32_t auxiliary_succeeded[BUSTER_NIGHTLY_COUNT(nightly_auxiliary_names)] = { 0 };
    uint64_t succeeded[BUSTER_NIGHTLY_ROWS_FULL] = { 0 };
    uint32_t counts[BUSTER_NIGHTLY_ROWS_FULL] = { 0 };
    bool inconsistent[BUSTER_NIGHTLY_ROWS_FULL] = { false };
    bool unsuccessful[BUSTER_NIGHTLY_ROWS_FULL] = { false };
    uint32_t verdicts = 0;
    bool verdict = false;
    bool fuzz[BUSTER_NIGHTLY_COUNT(nightly_fuzz_entries)] = { false };
    for (uint32_t i = 0; i < facts->job_count; i += 1)
    {
        const NightlyJob* job = &facts->jobs[i];
        const char* own = nightly_job_own_name(job->name);
        const bool success = job->run == run && job->completed && job->conclusion == NIGHTLY_CONCLUSION_SUCCESS;
        uint32_t architecture = 0;
        uint32_t row = 0;
        uint32_t shard = 0;
        uint32_t count = 0;
        bool plain = false;
        if (job->run == run && nightly_test_row(job->name, &architecture, &row, &shard, &count, &plain) && row < BUSTER_NIGHTLY_ROWS_FULL)
        {
            inconsistent[row] = inconsistent[row] || (counts[row] != 0 && counts[row] != count);
            counts[row] = count;
            unsuccessful[row] = unsuccessful[row] || !success;
            succeeded[row] |= success ? (uint64_t)1 << (shard - 1) : 0;
        }
        const size_t own_length = strlen(own);
        const char suffix[] = ", " BUSTER_NIGHTLY_VECTOR_POOL ", O3)";
        if (job->run == run && strncmp(own, "Vector row verdict (Clang ", 26) == 0 && own_length > sizeof(suffix) - 1 && strcmp(own + own_length - (sizeof(suffix) - 1), suffix) == 0)
        {
            verdicts += 1;
            verdict = success;
        }
        for (uint32_t j = 0; j < BUSTER_NIGHTLY_COUNT(nightly_fuzz_entries); j += 1)
        {
            char name[BUSTER_NIGHTLY_NAME];
            snprintf(name, sizeof(name), "libFuzzer (ASan + UBSan + leaks, %s)", nightly_fuzz_entries[j]);
            fuzz[j] = fuzz[j] || (success && strcmp(own, name) == 0);
        }
        for (uint32_t j = 0; j < BUSTER_NIGHTLY_COUNT(nightly_auxiliary_names); j += 1)
        {
            char prefix[BUSTER_NIGHTLY_NAME];
            const int length = snprintf(prefix, sizeof(prefix), "Auxiliary (%s.yml) / ", nightly_auxiliary_names[j]);
            const bool mine = job->run == run && strncmp(job->name, prefix, (size_t)length) == 0;
            auxiliary_jobs[j] += mine ? 1 : 0;
            auxiliary_succeeded[j] += mine && success ? 1 : 0;
        }
    }
    bool complete = true;
    for (uint32_t row = 0; row < BUSTER_NIGHTLY_ROWS_FULL; row += 1)
    {
        const uint32_t count = counts[row];
        const uint64_t every = count == 64 ? UINT64_MAX : ((uint64_t)1 << count) - 1;
        const bool whole = count != 0 && !inconsistent[row] && !unsuccessful[row] && succeeded[row] == every;
        complete = complete && whole;
        const char* why = count == 0 ? "missing" : inconsistent[row] ? "shard counts disagree" : unsuccessful[row] ? "a shard did not succeed" : succeeded[row] != every ? "a shard is missing" : "ok";
        fprintf(output, "row\t%s %s\t%s\t%u\n", row < BUSTER_NIGHTLY_SANITIZERS ? "x86-64" : "native", nightly_sanitizer_names[row % BUSTER_NIGHTLY_SANITIZERS], why, count);
    }
    const bool vector = verdicts == 1 && verdict;
    complete = complete && vector;
    fprintf(output, "vector\t%s\t%s\n", BUSTER_NIGHTLY_VECTOR_POOL, verdicts == 0 ? "missing" : verdicts != 1 ? "more than one verdict" : verdict ? "ok" : "did not succeed");
    for (uint32_t j = 0; j < BUSTER_NIGHTLY_COUNT(nightly_fuzz_entries); j += 1)
    {
        complete = complete && fuzz[j];
        fprintf(output, "fuzz\t%s\t%s\n", nightly_fuzz_entries[j], fuzz[j] ? "ok" : "missing or did not succeed");
    }
    complete = complete && listed;
    if (!listed)
    {
        fprintf(output, "auxiliary\t%s\tnot the selector's list\n", auxiliary != NULL ? auxiliary : "");
    }
    for (uint32_t j = 0; j < BUSTER_NIGHTLY_COUNT(nightly_auxiliary_names) && listed; j += 1)
    {
        const uint32_t jobs = auxiliary_jobs[j];
        const bool ok = called[j] ? jobs != 0 && auxiliary_succeeded[j] == jobs : jobs == 0;
        complete = complete && ok;
        const char* why = ok ? "ok" : !called[j] ? "ran although it doesn't apply" : jobs == 0 ? "missing" : "a job did not succeed";
        fprintf(output, "auxiliary\t%s\t%s\t%s\t%u\n", nightly_auxiliary_names[j], called[j] ? "called" : "not called", why, jobs);
    }
    fprintf(output, "coverage\t%s\n", complete ? "complete" : "incomplete");
    return complete;
}

// --- Invalidated merge-queue candidates (#1430) ---

// The other merge-queue candidates' runs to cancel. GitHub never cancels a candidate's run itself, whether it drops the
// candidate from the queue or rebuilds it on another base (the drill of 12-13 November); it deletes the candidate's
// branch, or points it at the rebuild when the base is the same. A run whose branch is gone or moved and whose commit is
// not on main is cancelled. A run that merged is kept (its record may still be uploading), as are this run, a run whose
// branch is present at its commit, and any whose facts are unknown: a doubt never cancels. Reads `candidate RUN BRANCH
// MERGED` lines (BRANCH present, moved, gone or unknown; MERGED merged, unmerged or unknown) and writes `cancel RUN` or
// `keep RUN WHY` for each; false, with nothing cancelled, on a line it can't read or a run listed twice.
static const char* const nightly_branch_states[] = { "present", "moved", "gone", "unknown" };
static const char* const nightly_merge_states[] = { "merged", "unmerged", "unknown" };

static bool nightly_stale(NightlyText input, uint64_t own, FILE* output)
{
    uint64_t* runs = nightly_allocate(BUSTER_NIGHTLY_RUNS, sizeof(uint64_t));
    bool* cancel = nightly_allocate(BUSTER_NIGHTLY_RUNS, sizeof(bool));
    const char** why = nightly_allocate(BUSTER_NIGHTLY_RUNS, sizeof(const char*));
    uint32_t count = 0;
    size_t offset = 0;
    char line[BUSTER_NIGHTLY_LINE];
    bool line_valid = true;
    bool valid = true;
    while (valid && nightly_next_line(input, &offset, line, &line_valid))
    {
        char* fields[BUSTER_NIGHTLY_FIELDS];
        const size_t fields_count = line_valid ? nightly_split(line, fields, BUSTER_NIGHTLY_FIELDS) : 0;
        uint64_t run = 0;
        valid = line_valid && fields_count == 4 && strcmp(fields[0], "candidate") == 0 && nightly_id(fields[1], &run) && count < BUSTER_NIGHTLY_RUNS;
        const size_t branch = valid ? nightly_find(fields[2], nightly_branch_states, BUSTER_NIGHTLY_COUNT(nightly_branch_states)) : 0;
        const size_t merge = valid ? nightly_find(fields[3], nightly_merge_states, BUSTER_NIGHTLY_COUNT(nightly_merge_states)) : 0;
        valid = valid && branch < BUSTER_NIGHTLY_COUNT(nightly_branch_states) && merge < BUSTER_NIGHTLY_COUNT(nightly_merge_states);
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            valid = runs[i] != run;
        }
        if (valid)
        {
            const bool invalidated = branch == 1 || branch == 2;
            runs[count] = run;
            cancel[count] = run != own && invalidated && merge == 1;
            why[count] = run == own ? "this run" : branch == 0 ? "its candidate is queued" : branch == 3 ? "its branch is unknown" : merge == 0 ? "it merged" : "unknown whether it merged";
            count += 1;
        }
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        if (cancel[i])
        {
            fprintf(output, "cancel\t%llu\n", (unsigned long long)runs[i]);
        }
        else
        {
            fprintf(output, "keep\t%llu\t%s\n", (unsigned long long)runs[i], why[i]);
        }
    }
    free(runs);
    free(cancel);
    free(why);
    return valid;
}

// --- Records: one run's coverage, which its `record` job writes ---

typedef enum NightlyKind NightlyKind;
enum NightlyKind
{
    NIGHTLY_KIND_FORMAT,
    NIGHTLY_KIND_REPOSITORY,
    NIGHTLY_KIND_ID,
    NIGHTLY_KIND_ID_OR_DASH,
    NIGHTLY_KIND_SHA,
    NIGHTLY_KIND_SHA_OR_DASH,
    NIGHTLY_KIND_TOKEN_OR_DASH,
    NIGHTLY_KIND_ENUM,
    NIGHTLY_KIND_MEASURE,
    NIGHTLY_KIND_GATES,
    NIGHTLY_KIND_HOST,
    NIGHTLY_KIND_TEXT,
};

typedef struct NightlyKey NightlyKey;
struct NightlyKey
{
    const char* name;
    NightlyKind kind;
    // NIGHTLY_KIND_ENUM's values, space-separated; `-` is a value only where listed.
    const char* values;
};

typedef enum NightlyField NightlyField;
enum NightlyField
{
    NIGHTLY_FIELD_FORMAT,
    NIGHTLY_FIELD_REPOSITORY,
    NIGHTLY_FIELD_RUN,
    NIGHTLY_FIELD_ATTEMPT,
    NIGHTLY_FIELD_EVENT,
    NIGHTLY_FIELD_PURPOSE,
    NIGHTLY_FIELD_REQUEST,
    NIGHTLY_FIELD_COMMIT,
    NIGHTLY_FIELD_TREE,
    NIGHTLY_FIELD_PARENT,
    NIGHTLY_FIELD_PARENT_TREE,
    NIGHTLY_FIELD_PR_HEAD,
    NIGHTLY_FIELD_SNAPSHOT,
    NIGHTLY_FIELD_WORKFLOW,
    NIGHTLY_FIELD_SELECTOR,
    NIGHTLY_FIELD_INVENTORY,
    NIGHTLY_FIELD_TOOLCHAIN,
    NIGHTLY_FIELD_CORPUS,
    NIGHTLY_FIELD_MODE,
    NIGHTLY_FIELD_CODE,
    NIGHTLY_FIELD_MEASURE,
    NIGHTLY_FIELD_GATES,
    NIGHTLY_FIELD_CHANGES_RESULT,
    NIGHTLY_FIELD_EVIDENCE_RESULT,
    NIGHTLY_FIELD_SCOPE,
    NIGHTLY_FIELD_HOSTED,
    NIGHTLY_FIELD_HOSTED_SOURCE,
    NIGHTLY_FIELD_NATIVE_EVIDENCE,
    NIGHTLY_FIELD_NATIVE_SOURCE,
    NIGHTLY_FIELD_MEASUREMENT_EVIDENCE,
    NIGHTLY_FIELD_MEASUREMENT_SOURCE,
    NIGHTLY_FIELD_HARDWARE,
    NIGHTLY_FIELD_HARDWARE_REASON,
    NIGHTLY_FIELD_TEST_RESULT,
    NIGHTLY_FIELD_FUZZ_RESULT,
    NIGHTLY_FIELD_BENCHMARK_RESULT,
    NIGHTLY_FIELD_WINDOW,
    NIGHTLY_FIELD_NATIVE,
    NIGHTLY_FIELD_HOST,
    NIGHTLY_FIELD_QUALIFICATION_RESULT,
    NIGHTLY_FIELD_OVERRIDE,
    NIGHTLY_FIELD_COVERAGE,
    NIGHTLY_FIELD_OUTCOME,
    NIGHTLY_FIELD_COUNT
};

#define BUSTER_NIGHTLY_RESULTS "success failure cancelled skipped -"

// In NightlyField's order, which is also the file's: a record with a key out of place, missing or repeated is invalid.
static const NightlyKey nightly_record_keys[NIGHTLY_FIELD_COUNT] = {
    { "format", NIGHTLY_KIND_FORMAT, NULL },
    { "repository", NIGHTLY_KIND_REPOSITORY, NULL },
    { "run", NIGHTLY_KIND_ID, NULL },
    { "attempt", NIGHTLY_KIND_ID, NULL },
    { "event", NIGHTLY_KIND_ENUM, "pull_request push workflow_dispatch schedule merge_group" },
    { "purpose", NIGHTLY_KIND_ENUM, "pr push schedule full backlog snapshot queue" },
    { "request", NIGHTLY_KIND_TOKEN_OR_DASH, NULL },
    { "commit", NIGHTLY_KIND_SHA, NULL },
    { "tree", NIGHTLY_KIND_SHA, NULL },
    { "parent", NIGHTLY_KIND_SHA_OR_DASH, NULL },
    { "parent_tree", NIGHTLY_KIND_SHA_OR_DASH, NULL },
    { "pr_head", NIGHTLY_KIND_SHA_OR_DASH, NULL },
    { "snapshot", NIGHTLY_KIND_SHA_OR_DASH, NULL },
    { "workflow", NIGHTLY_KIND_SHA, NULL },
    { "selector", NIGHTLY_KIND_SHA, NULL },
    { "inventory", NIGHTLY_KIND_SHA, NULL },
    { "toolchain", NIGHTLY_KIND_SHA, NULL },
    { "corpus", NIGHTLY_KIND_SHA, NULL },
    { "mode", NIGHTLY_KIND_ENUM, "on off" },
    { "code", NIGHTLY_KIND_ENUM, "true false -" },
    { "measure", NIGHTLY_KIND_MEASURE, NULL },
    { "gates", NIGHTLY_KIND_GATES, NULL },
    { "changes_result", NIGHTLY_KIND_ENUM, BUSTER_NIGHTLY_RESULTS },
    { "evidence_result", NIGHTLY_KIND_ENUM, BUSTER_NIGHTLY_RESULTS },
    { "scope", NIGHTLY_KIND_ENUM, "plain full -" },
    { "hosted", NIGHTLY_KIND_ENUM, "run reused -" },
    { "hosted_source", NIGHTLY_KIND_ID_OR_DASH, NULL },
    { "native_evidence", NIGHTLY_KIND_ENUM, "run reused -" },
    { "native_source", NIGHTLY_KIND_ID_OR_DASH, NULL },
    { "measurement_evidence", NIGHTLY_KIND_ENUM, "run reused -" },
    { "measurement_source", NIGHTLY_KIND_ID_OR_DASH, NULL },
    { "hardware", NIGHTLY_KIND_ENUM, "admit skip -" },
    { "hardware_reason", NIGHTLY_KIND_TOKEN_OR_DASH, NULL },
    { "test_result", NIGHTLY_KIND_ENUM, BUSTER_NIGHTLY_RESULTS },
    { "fuzz_result", NIGHTLY_KIND_ENUM, BUSTER_NIGHTLY_RESULTS },
    { "benchmark_result", NIGHTLY_KIND_ENUM, BUSTER_NIGHTLY_RESULTS },
    { "window", NIGHTLY_KIND_ENUM, "open closed -" },
    { "native", NIGHTLY_KIND_ENUM, "started passed -" },
    { "host", NIGHTLY_KIND_HOST, NULL },
    { "qualification_result", NIGHTLY_KIND_ENUM, BUSTER_NIGHTLY_RESULTS },
    { "override", NIGHTLY_KIND_TEXT, NULL },
    { "coverage", NIGHTLY_KIND_ENUM, "targeted plain full none" },
    { "outcome", NIGHTLY_KIND_ENUM, "complete not-required deferred pending blocked-hosted blocked-native failed interrupted rejected" },
};

typedef struct NightlyRecord NightlyRecord;
struct NightlyRecord
{
    char values[NIGHTLY_FIELD_COUNT][BUSTER_NIGHTLY_VALUE];
};

static const char* nightly_value(const NightlyRecord* record, NightlyField field)
{
    return record->values[field];
}

static bool nightly_is(const NightlyRecord* record, NightlyField field, const char* value)
{
    return strcmp(record->values[field], value) == 0;
}

static bool nightly_enum_value(const char* values, const char* value)
{
    const size_t length = strlen(value);
    bool found = false;
    for (const char* at = values; !found && at != NULL && *at != 0;)
    {
        const char* space = strchr(at, ' ');
        const size_t size = space != NULL ? (size_t)(space - at) : strlen(at);
        found = size == length && memcmp(at, value, size) == 0;
        at = space != NULL ? space + 1 : NULL;
    }
    return found;
}

// `,` or `all` or `,NAME,...,NAME,` with names of lowercase letters, digits and '-' (the selector's measure); or `-`.
static bool nightly_measure_value(const char* value)
{
    const size_t length = strlen(value);
    bool valid = strcmp(value, "all") == 0 || strcmp(value, "-") == 0 || strcmp(value, ",") == 0;
    if (!valid && length >= 3 && value[0] == ',' && value[length - 1] == ',')
    {
        valid = true;
        bool empty = true;
        for (size_t i = 1; valid && i < length; i += 1)
        {
            const char c = value[i];
            valid = c == ',' ? !empty : (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
            empty = c == ',';
        }
    }
    return valid;
}

// `-` or NAME,NAME with names of lowercase letters, digits and '_' (the selector's outputs that are true).
static bool nightly_gates_value(const char* value)
{
    const size_t length = strlen(value);
    bool valid = strcmp(value, "-") == 0;
    if (!valid && length != 0 && value[0] != ',' && value[length - 1] != ',')
    {
        valid = true;
        for (size_t i = 0; valid && i < length; i += 1)
        {
            const char c = value[i];
            valid = (c == ',' && value[i + 1] != ',') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        }
    }
    return valid;
}

// A host fingerprint (the 9700X job's): letters, digits and . _ / + - only.
static bool nightly_host_value(const char* value)
{
    const size_t length = strlen(value);
    bool valid = length != 0 && length < BUSTER_NIGHTLY_HOST;
    for (size_t i = 0; valid && i < length && strcmp(value, "-") != 0; i += 1)
    {
        const char c = value[i];
        valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '/' || c == '+' || c == '-';
    }
    return valid;
}

static bool nightly_text_value(const char* value)
{
    char sanitized[BUSTER_NIGHTLY_REASON];
    nightly_sanitize(value, sanitized, sizeof(sanitized));
    return value[0] != 0 && strlen(value) < BUSTER_NIGHTLY_REASON && strcmp(sanitized, value) == 0;
}

static bool nightly_valid_value(const NightlyKey* key, const char* value)
{
    uint64_t number = 0;
    const bool dash = strcmp(value, "-") == 0;
    bool valid = false;
    switch (key->kind)
    {
    case NIGHTLY_KIND_FORMAT:
        valid = strcmp(value, BUSTER_NIGHTLY_RECORD_FORMAT) == 0;
        break;
    case NIGHTLY_KIND_REPOSITORY:
        valid = nightly_number(value, UINT64_MAX, &number) && number == BUSTER_NIGHTLY_REPOSITORY;
        break;
    case NIGHTLY_KIND_ID:
        valid = nightly_id(value, &number);
        break;
    case NIGHTLY_KIND_ID_OR_DASH:
        valid = dash || nightly_id(value, &number);
        break;
    case NIGHTLY_KIND_SHA:
        valid = nightly_sha(value);
        break;
    case NIGHTLY_KIND_SHA_OR_DASH:
        valid = dash || nightly_sha(value);
        break;
    case NIGHTLY_KIND_TOKEN_OR_DASH:
        valid = dash || nightly_token(value, BUSTER_NIGHTLY_TOKEN);
        break;
    case NIGHTLY_KIND_ENUM:
        valid = nightly_enum_value(key->values, value);
        break;
    case NIGHTLY_KIND_MEASURE:
        valid = nightly_measure_value(value);
        break;
    case NIGHTLY_KIND_GATES:
        valid = nightly_gates_value(value);
        break;
    case NIGHTLY_KIND_HOST:
        valid = nightly_host_value(value);
        break;
    case NIGHTLY_KIND_TEXT:
        valid = dash || nightly_text_value(value);
        break;
    }
    return valid && strlen(value) < BUSTER_NIGHTLY_VALUE;
}

// The record's lines, every key once in order, each value valid. False and an untouched meaning otherwise.
static bool nightly_record_parse(NightlyText text, NightlyRecord* record)
{
    size_t offset = 0;
    char line[BUSTER_NIGHTLY_LINE];
    bool line_valid = true;
    uint32_t field = 0;
    bool valid = true;
    while (valid && nightly_next_line(text, &offset, line, &line_valid))
    {
        char* fields[3];
        const size_t count = line_valid ? nightly_split(line, fields, 3) : 0;
        valid = line_valid && count == 2 && field < NIGHTLY_FIELD_COUNT && strcmp(fields[0], nightly_record_keys[field].name) == 0;
        valid = valid && nightly_valid_value(&nightly_record_keys[field], fields[1]);
        if (valid)
        {
            nightly_copy(record->values[field], BUSTER_NIGHTLY_VALUE, fields[1]);
            field += 1;
        }
    }
    return valid && field == NIGHTLY_FIELD_COUNT;
}

static void nightly_record_write(FILE* output, const NightlyRecord* record)
{
    for (uint32_t i = 0; i < NIGHTLY_FIELD_COUNT; i += 1)
    {
        fprintf(output, "%s\t%s\n", nightly_record_keys[i].name, record->values[i]);
    }
}

static NightlyCoverage nightly_record_coverage(const NightlyRecord* record)
{
    const char* purpose = nightly_value(record, NIGHTLY_FIELD_PURPOSE);
    NightlyCoverage coverage = NIGHTLY_COVERAGE_FULL;
    if (strcmp(purpose, "pr") == 0 || strcmp(purpose, "backlog") == 0)
    {
        coverage = NIGHTLY_COVERAGE_TARGETED;
    }
    else if (strcmp(purpose, "push") == 0)
    {
        // With the nightly mode, a push measures what its first-parent change selects; before it, every plain run.
        coverage = nightly_is(record, NIGHTLY_FIELD_MODE, "on") ? NIGHTLY_COVERAGE_TARGETED : NIGHTLY_COVERAGE_PLAIN;
    }
    else if (strcmp(purpose, "queue") == 0)
    {
        // A merge-queue candidate measures nothing: its state's measurements are the nightly window's.
        coverage = NIGHTLY_COVERAGE_NONE;
    }
    return coverage;
}

// What the run achieved, from its jobs' results and outputs alone. Completion needs the 9700X job's success after its
// own native correctness (or, for a push, its pull request's measurements reused by evidence); a failure is a
// correctness block only where correctness failed, hosted or native, and the window's refusal is a deferral. A
// merge-queue candidate (#1430) is never admitted and has no exemption: it is deferred (its hosted contract passed,
// its native correctness and measurements the window's) only through the required aggregate's success, which checked
// its coverage job by job; without it, it is interrupted.
static NightlyOutcome nightly_record_outcome(const NightlyRecord* record)
{
    const bool queue = nightly_is(record, NIGHTLY_FIELD_PURPOSE, "queue");
    const bool no_code = !queue && nightly_is(record, NIGHTLY_FIELD_CHANGES_RESULT, "success") && nightly_is(record, NIGHTLY_FIELD_CODE, "false");
    const bool hosted_ran = nightly_is(record, NIGHTLY_FIELD_HOSTED, "run");
    const bool fuzz_required = nightly_is(record, NIGHTLY_FIELD_SCOPE, "full");
    const bool hosted_failed = hosted_ran && (nightly_is(record, NIGHTLY_FIELD_TEST_RESULT, "failure") || (fuzz_required && nightly_is(record, NIGHTLY_FIELD_FUZZ_RESULT, "failure")));
    const bool hosted_cancelled = hosted_ran && (nightly_is(record, NIGHTLY_FIELD_TEST_RESULT, "cancelled") || (fuzz_required && nightly_is(record, NIGHTLY_FIELD_FUZZ_RESULT, "cancelled")));
    const bool admitted = nightly_is(record, NIGHTLY_FIELD_HARDWARE, "admit");
    const char* benchmark = nightly_value(record, NIGHTLY_FIELD_BENCHMARK_RESULT);
    NightlyOutcome outcome = NIGHTLY_OUTCOME_INTERRUPTED;
    if (no_code)
    {
        outcome = NIGHTLY_OUTCOME_NOT_REQUIRED;
    }
    else if (!nightly_is(record, NIGHTLY_FIELD_EVIDENCE_RESULT, "success") || (queue && admitted))
    {
        outcome = NIGHTLY_OUTCOME_REJECTED;
    }
    else if (hosted_failed)
    {
        outcome = NIGHTLY_OUTCOME_BLOCKED_HOSTED;
    }
    else if (hosted_cancelled || (queue && !nightly_is(record, NIGHTLY_FIELD_QUALIFICATION_RESULT, "success")))
    {
        outcome = NIGHTLY_OUTCOME_INTERRUPTED;
    }
    else if (!admitted)
    {
        const bool reused_push = nightly_is(record, NIGHTLY_FIELD_PURPOSE, "push") && nightly_is(record, NIGHTLY_FIELD_HARDWARE_REASON, "native-reused");
        const bool measurement = reused_push && nightly_is(record, NIGHTLY_FIELD_MEASUREMENT_EVIDENCE, "reused") && !nightly_is(record, NIGHTLY_FIELD_MEASUREMENT_SOURCE, "-");
        outcome = measurement ? NIGHTLY_OUTCOME_COMPLETE : reused_push ? NIGHTLY_OUTCOME_PENDING : NIGHTLY_OUTCOME_DEFERRED;
    }
    else if (strcmp(benchmark, "success") == 0)
    {
        outcome = NIGHTLY_OUTCOME_COMPLETE;
    }
    else if (strcmp(benchmark, "failure") == 0)
    {
        // The window step fails the job before any work when the window closed while the job waited for the lock.
        outcome = nightly_is(record, NIGHTLY_FIELD_WINDOW, "closed") ? NIGHTLY_OUTCOME_DEFERRED : nightly_is(record, NIGHTLY_FIELD_NATIVE, "started") ? NIGHTLY_OUTCOME_BLOCKED_NATIVE : NIGHTLY_OUTCOME_FAILED;
    }
    return outcome;
}

// The record of this run from the RECORD_* environment of its `record` job: each key's variable is RECORD_ and the
// key in capitals; empty values are `-`. The purpose, coverage and outcome are derived, not read. An invalid value is
// fatal, so that the job fails visibly rather than write a record that a reader would reject.
static void nightly_record_from_environment(NightlyRecord* record)
{
    const char* event = getenv("RECORD_EVENT");
    const char* dispatched = getenv("RECORD_DISPATCH_PURPOSE");
    const char* purpose = event == NULL ? "" : strcmp(event, "pull_request") == 0 ? "pr" : strcmp(event, "push") == 0 ? "push" : strcmp(event, "schedule") == 0 ? "schedule" : strcmp(event, "merge_group") == 0 ? "queue" : dispatched != NULL ? dispatched : "";
    for (uint32_t i = 0; i < NIGHTLY_FIELD_COUNT; i += 1)
    {
        const NightlyKey* key = &nightly_record_keys[i];
        char name[64] = "RECORD_";
        for (size_t j = 0; key->name[j] != 0 && j + 8 < sizeof(name); j += 1)
        {
            name[7 + j] = key->name[j] >= 'a' && key->name[j] <= 'z' ? (char)(key->name[j] - 'a' + 'A') : key->name[j];
        }
        const char* value = getenv(name);
        char sanitized[BUSTER_NIGHTLY_REASON];
        if (i == NIGHTLY_FIELD_FORMAT)
        {
            value = BUSTER_NIGHTLY_RECORD_FORMAT;
        }
        else if (i == NIGHTLY_FIELD_PURPOSE)
        {
            value = purpose;
        }
        else if (i == NIGHTLY_FIELD_OVERRIDE && value != NULL && value[0] != 0)
        {
            nightly_sanitize(value, sanitized, sizeof(sanitized));
            value = sanitized;
        }
        else if (i == NIGHTLY_FIELD_COVERAGE || i == NIGHTLY_FIELD_OUTCOME)
        {
            value = "none";
        }
        value = value == NULL || value[0] == 0 ? "-" : value;
        // The outcome's placeholder until it's derived below.
        value = i == NIGHTLY_FIELD_OUTCOME ? "interrupted" : value;
        if (!nightly_valid_value(key, value))
        {
            nightly_fatal("invalid record value for", key->name);
        }
        nightly_copy(record->values[i], BUSTER_NIGHTLY_VALUE, value);
    }
    const NightlyOutcome outcome = nightly_record_outcome(record);
    const NightlyCoverage coverage = outcome == NIGHTLY_OUTCOME_NOT_REQUIRED ? NIGHTLY_COVERAGE_NONE : nightly_record_coverage(record);
    nightly_copy(record->values[NIGHTLY_FIELD_COVERAGE], BUSTER_NIGHTLY_VALUE, nightly_coverage_names[coverage]);
    nightly_copy(record->values[NIGHTLY_FIELD_OUTCOME], BUSTER_NIGHTLY_VALUE, nightly_outcome_names[outcome]);
}

// --- The ledger: one row per harvested run attempt, and one per cycle's pinned snapshot ---

typedef struct NightlyRow NightlyRow;
struct NightlyRow
{
    uint64_t run;
    uint32_t attempt;
    char commit[41];
    NightlyEvent event;
    NightlyPurpose purpose;
    char request[BUSTER_NIGHTLY_TOKEN];
    int64_t created;
    int64_t completed;
    NightlyPlain plain;
    NightlyHardware hardware;
    NightlyCoverage coverage;
    NightlyOutcome outcome;
    uint64_t source;
    char host[BUSTER_NIGHTLY_HOST];
    char protocol[41];
};

typedef struct NightlyCycle NightlyCycle;
struct NightlyCycle
{
    char date[11];
    char snapshot[41];
    uint64_t coordinator;
    int64_t pinned;
};

typedef struct NightlyLedger NightlyLedger;
struct NightlyLedger
{
    NightlyRow* rows;
    uint32_t row_count;
    NightlyCycle* cycles;
    uint32_t cycle_count;
};

static NightlyLedger nightly_ledger_new(void)
{
    NightlyLedger ledger = {
        .rows = nightly_allocate(BUSTER_NIGHTLY_ROWS, sizeof(NightlyRow)),
        .cycles = nightly_allocate(BUSTER_NIGHTLY_CYCLES, sizeof(NightlyCycle)),
    };
    return ledger;
}

static void nightly_ledger_free(NightlyLedger* ledger)
{
    free(ledger->rows);
    free(ledger->cycles);
}

static bool nightly_date(const char* text)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int consumed = 0;
    const bool parsed = strlen(text) == 10 && sscanf(text, "%4d-%2d-%2d%n", &year, &month, &day, &consumed) == 3 && consumed == 10;
    return parsed && text[4] == '-' && text[7] == '-' && year >= 2026 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

static void nightly_row_write(FILE* output, const NightlyRow* row)
{
    fprintf(output, "run\t%llu\t%u\t%s\t%s\t%s\t%s\t%lld\t%lld\t%s\t%s\t%s\t%s\t", (unsigned long long)row->run, row->attempt, row->commit, nightly_event_names[row->event], nightly_purpose_names[row->purpose], row->request[0] != 0 ? row->request : "-",
            (long long)row->created, (long long)row->completed, nightly_plain_names[row->plain], nightly_hardware_names[row->hardware], nightly_coverage_names[row->coverage], nightly_outcome_names[row->outcome]);
    if (row->source != 0)
    {
        fprintf(output, "%llu", (unsigned long long)row->source);
    }
    else
    {
        fputs("-", output);
    }
    fprintf(output, "\t%s\t%s\n", row->host[0] != 0 ? row->host : "-", row->protocol[0] != 0 ? row->protocol : "-");
}

static bool nightly_has_row(const NightlyLedger* ledger, uint64_t run, uint32_t attempt)
{
    bool found = false;
    for (uint32_t i = 0; i < ledger->row_count; i += 1)
    {
        found = found || (ledger->rows[i].run == run && ledger->rows[i].attempt == attempt);
    }
    return found;
}

// The ledger's header and rows. An empty ledger is a new one. False on any invalid line or a repeated run attempt or
// cycle date: the ledger is the record of what was measured, and a reader that skipped a bad line would lose coverage.
static bool nightly_ledger_parse(NightlyLedger* ledger, NightlyText text)
{
    size_t offset = 0;
    char line[BUSTER_NIGHTLY_LINE];
    bool line_valid = true;
    bool valid = true;
    bool header = text.length == 0;
    uint32_t number = 0;
    while (valid && nightly_next_line(text, &offset, line, &line_valid))
    {
        number += 1;
        if (number == 1)
        {
            header = line_valid && strcmp(line, BUSTER_NIGHTLY_LEDGER_FORMAT) == 0;
            valid = header;
            continue;
        }
        char* fields[BUSTER_NIGHTLY_FIELDS + 1];
        const size_t count = line_valid ? nightly_split(line, fields, BUSTER_NIGHTLY_FIELDS + 1) : 0;
        valid = line_valid && count >= 1;
        if (valid && strcmp(fields[0], "run") == 0)
        {
            NightlyRow row = { 0 };
            uint64_t attempt = 0;
            size_t found[6] = { 0 };
            valid = count == 16 && ledger->row_count < BUSTER_NIGHTLY_ROWS && nightly_id(fields[1], &row.run) && nightly_number(fields[2], 1000, &attempt) && attempt != 0 && nightly_sha(fields[3]);
            if (valid)
            {
                found[0] = nightly_find(fields[4], nightly_event_names, NIGHTLY_EVENT_OTHER);
                found[1] = nightly_find(fields[5], nightly_purpose_names, NIGHTLY_PURPOSE_COUNT);
                found[2] = nightly_find(fields[9], nightly_plain_names, NIGHTLY_PLAIN_COUNT);
                found[3] = nightly_find(fields[10], nightly_hardware_names, NIGHTLY_HARDWARE_COUNT);
                found[4] = nightly_find(fields[11], nightly_coverage_names, NIGHTLY_COVERAGE_COUNT);
                found[5] = nightly_find(fields[12], nightly_outcome_names, NIGHTLY_OUTCOME_COUNT);
            }
            valid = valid && found[0] < NIGHTLY_EVENT_OTHER && found[1] < NIGHTLY_PURPOSE_COUNT && found[2] < NIGHTLY_PLAIN_COUNT && found[3] < NIGHTLY_HARDWARE_COUNT && found[4] < NIGHTLY_COVERAGE_COUNT && found[5] < NIGHTLY_OUTCOME_COUNT;
            valid = valid && (strcmp(fields[6], "-") == 0 || nightly_token(fields[6], sizeof(row.request))) && nightly_epoch(fields[7], &row.created) && nightly_epoch(fields[8], &row.completed);
            valid = valid && (strcmp(fields[13], "-") == 0 || nightly_id(fields[13], &row.source)) && nightly_host_value(fields[14]) && (strcmp(fields[15], "-") == 0 || nightly_sha(fields[15]));
            valid = valid && !nightly_has_row(ledger, row.run, (uint32_t)attempt);
            if (valid)
            {
                row.attempt = (uint32_t)attempt;
                nightly_copy(row.commit, sizeof(row.commit), fields[3]);
                row.event = (NightlyEvent)found[0];
                row.purpose = (NightlyPurpose)found[1];
                nightly_copy(row.request, sizeof(row.request), strcmp(fields[6], "-") == 0 ? "" : fields[6]);
                row.plain = (NightlyPlain)found[2];
                row.hardware = (NightlyHardware)found[3];
                row.coverage = (NightlyCoverage)found[4];
                row.outcome = (NightlyOutcome)found[5];
                nightly_copy(row.host, sizeof(row.host), strcmp(fields[14], "-") == 0 ? "" : fields[14]);
                nightly_copy(row.protocol, sizeof(row.protocol), strcmp(fields[15], "-") == 0 ? "" : fields[15]);
                ledger->rows[ledger->row_count] = row;
                ledger->row_count += 1;
            }
        }
        else if (valid && strcmp(fields[0], "cycle") == 0)
        {
            NightlyCycle cycle = { 0 };
            valid = count == 5 && ledger->cycle_count < BUSTER_NIGHTLY_CYCLES && nightly_date(fields[1]) && nightly_sha(fields[2]) && nightly_id(fields[3], &cycle.coordinator) && nightly_epoch(fields[4], &cycle.pinned);
            for (uint32_t i = 0; valid && i < ledger->cycle_count; i += 1)
            {
                valid = strcmp(ledger->cycles[i].date, fields[1]) != 0;
            }
            if (valid)
            {
                nightly_copy(cycle.date, sizeof(cycle.date), fields[1]);
                nightly_copy(cycle.snapshot, sizeof(cycle.snapshot), fields[2]);
                ledger->cycles[ledger->cycle_count] = cycle;
                ledger->cycle_count += 1;
            }
        }
        else
        {
            valid = false;
        }
        if (!valid && !nightly_quiet)
        {
            fprintf(stderr, "nightly: invalid ledger line %u\n", number);
        }
    }
    return valid && header;
}

// The row of a completed run from its facts and, where its record is valid and agrees with them, its record. Without
// a record (runs before the records, or a record job that failed), the 9700X job's conclusion decides, as each event's
// coverage then was: a push's every plain run, a scheduled or full run's everything; a merge-queue candidate's
// (#1430) measures nothing, and only the required aggregate's success passes it. A record that disagrees with the
// facts on its run, commit or event, or claims a completion the 9700X job's conclusion doesn't support, or a
// candidate's pass the aggregate's doesn't, makes the row invalid, which counts as nothing.
static NightlyRow nightly_harvest_row(const NightlyFacts* facts, const NightlyRun* run, const NightlyRecord* record)
{
    NightlyRow row = {
        .run = run->id,
        .attempt = run->attempt,
        .event = run->event,
        .created = run->created,
        .completed = run->updated,
        .plain = nightly_plain_verdict(facts, run->id),
    };
    nightly_copy(row.commit, sizeof(row.commit), run->head);
    const NightlyJob* hardware = nightly_find_job(facts, run->id, BUSTER_NIGHTLY_HARDWARE_JOB);
    const bool hardware_ran = hardware != NULL && hardware->completed && hardware->conclusion != NIGHTLY_CONCLUSION_SKIPPED;
    const bool hardware_success = hardware != NULL && hardware->completed && hardware->conclusion == NIGHTLY_CONCLUSION_SUCCESS;
    const NightlyJob* renamed = nightly_find_job(facts, run->id, BUSTER_NIGHTLY_AGGREGATE_JOB);
    const NightlyJob* aggregate = renamed != NULL ? renamed : nightly_find_job(facts, run->id, BUSTER_NIGHTLY_AGGREGATE_JOB_BEFORE);
    const bool aggregate_success = aggregate != NULL && aggregate->completed && aggregate->conclusion == NIGHTLY_CONCLUSION_SUCCESS;
    const bool queue = run->event == NIGHTLY_EVENT_MERGE_GROUP;
    row.hardware = hardware_ran ? NIGHTLY_HARDWARE_ADMIT : NIGHTLY_HARDWARE_SKIP;
    row.purpose = run->event == NIGHTLY_EVENT_PUSH ? NIGHTLY_PURPOSE_PUSH : run->event == NIGHTLY_EVENT_SCHEDULE ? NIGHTLY_PURPOSE_SCHEDULE : queue ? NIGHTLY_PURPOSE_QUEUE : run->nightly ? run->purpose : NIGHTLY_PURPOSE_FULL;
    nightly_copy(row.request, sizeof(row.request), run->nightly ? run->request : "");
    if (record != NULL)
    {
        char run_text[24];
        char attempt_text[12];
        snprintf(run_text, sizeof(run_text), "%llu", (unsigned long long)run->id);
        snprintf(attempt_text, sizeof(attempt_text), "%u", run->attempt);
        const size_t purpose = nightly_find(nightly_value(record, NIGHTLY_FIELD_PURPOSE), nightly_purpose_names, NIGHTLY_PURPOSE_COUNT);
        const size_t coverage = nightly_find(nightly_value(record, NIGHTLY_FIELD_COVERAGE), nightly_coverage_names, NIGHTLY_COVERAGE_COUNT);
        const size_t outcome = nightly_find(nightly_value(record, NIGHTLY_FIELD_OUTCOME), nightly_outcome_names, NIGHTLY_OUTCOME_COUNT);
        const bool admitted = nightly_is(record, NIGHTLY_FIELD_HARDWARE, "admit");
        uint64_t source = 0;
        const bool reused = !admitted && nightly_is(record, NIGHTLY_FIELD_PURPOSE, "push") && nightly_id(nightly_value(record, NIGHTLY_FIELD_MEASUREMENT_SOURCE), &source);
        bool agrees = nightly_is(record, NIGHTLY_FIELD_RUN, run_text) && nightly_is(record, NIGHTLY_FIELD_ATTEMPT, attempt_text) && nightly_is(record, NIGHTLY_FIELD_COMMIT, run->head);
        agrees = agrees && nightly_is(record, NIGHTLY_FIELD_EVENT, nightly_event_names[run->event]) && purpose < NIGHTLY_PURPOSE_COUNT && coverage < NIGHTLY_COVERAGE_COUNT && outcome < NIGHTLY_OUTCOME_COUNT;
        agrees = agrees && (!run->nightly || ((NightlyPurpose)purpose == run->purpose && nightly_is(record, NIGHTLY_FIELD_REQUEST, run->request)));
        // A merge-queue candidate's record is a queue record, and only a candidate's is; its pass is the aggregate's.
        agrees = agrees && ((NightlyPurpose)purpose == NIGHTLY_PURPOSE_QUEUE) == queue;
        agrees = agrees && (!queue || (NightlyOutcome)outcome != NIGHTLY_OUTCOME_DEFERRED || aggregate_success);
        // A completion: the 9700X job succeeded, or a push reused its pull request's measurement by evidence.
        agrees = agrees && ((NightlyOutcome)outcome != NIGHTLY_OUTCOME_COMPLETE || (admitted && hardware_success) || reused);
        agrees = agrees && (admitted == hardware_ran || !hardware_ran);
        row.purpose = purpose < NIGHTLY_PURPOSE_COUNT ? (NightlyPurpose)purpose : row.purpose;
        row.coverage = agrees ? (NightlyCoverage)coverage : NIGHTLY_COVERAGE_NONE;
        row.outcome = agrees ? (NightlyOutcome)outcome : NIGHTLY_OUTCOME_INVALID;
        row.source = agrees && (NightlyOutcome)outcome == NIGHTLY_OUTCOME_COMPLETE && reused ? source : 0;
        nightly_copy(row.host, sizeof(row.host), agrees && !nightly_is(record, NIGHTLY_FIELD_HOST, "-") ? nightly_value(record, NIGHTLY_FIELD_HOST) : "");
        nightly_copy(row.protocol, sizeof(row.protocol), nightly_value(record, NIGHTLY_FIELD_WORKFLOW));
    }
    else
    {
        row.coverage = run->event == NIGHTLY_EVENT_PUSH ? NIGHTLY_COVERAGE_PLAIN : queue ? NIGHTLY_COVERAGE_NONE : run->nightly && run->purpose == NIGHTLY_PURPOSE_BACKLOG ? NIGHTLY_COVERAGE_TARGETED : NIGHTLY_COVERAGE_FULL;
        // Without a record, a nightly run's success can't be told from a deferral, nor a failure's cause: nothing of
        // it completes. A run whose 9700X job never ran was cancelled (interrupted) or stopped by a failed hosted job
        // (the run's own gate, as its record would say), or else deferred; the state's plain rows decide its status.
        bool hosted_failed = false;
        for (uint32_t i = 0; i < facts->job_count; i += 1)
        {
            const NightlyJob* job = &facts->jobs[i];
            const char* own = nightly_job_own_name(job->name);
            const bool hosted = strncmp(job->name, "Tests on ", 9) == 0 || strncmp(own, "Tests on ", 9) == 0 || strncmp(job->name, "libFuzzer ", 10) == 0;
            hosted_failed |= job->run == run->id && hosted && job->completed && (job->conclusion == NIGHTLY_CONCLUSION_FAILURE || job->conclusion == NIGHTLY_CONCLUSION_TIMED_OUT);
        }
        const bool trusted = !run->nightly && !queue && hardware_success;
        // A candidate passes by the aggregate's success alone, which checked its coverage job by job (a vector draw's
        // candidate may fail where its item passed elsewhere); one the aggregate didn't pass is no pass.
        const NightlyOutcome otherwise = queue && !aggregate_success ? NIGHTLY_OUTCOME_INTERRUPTED : NIGHTLY_OUTCOME_DEFERRED;
        row.outcome = trusted ? NIGHTLY_OUTCOME_COMPLETE : queue && aggregate_success ? NIGHTLY_OUTCOME_DEFERRED : hardware_ran ? NIGHTLY_OUTCOME_FAILED : run->conclusion == NIGHTLY_CONCLUSION_CANCELLED ? NIGHTLY_OUTCOME_INTERRUPTED : hosted_failed ? NIGHTLY_OUTCOME_BLOCKED_HOSTED : otherwise;
    }
    return row;
}

// Every completed workflow run of a state (a push, a scheduled run, a dispatch or a merge-queue candidate whose commit
// main was fast-forwarded to: a pull request's run tests a merge commit, not a state) that the ledger doesn't have, as
// rows. A candidate whose commit main doesn't have is no state's, so nothing credits it. The record of run R is DIRECTORY/R.tsv where the glue
// found the run's coverage-record artifact for its attempt; an unreadable or invalid one counts as none.
static uint32_t nightly_harvest(const NightlyLedger* ledger, const NightlyFacts* facts, const char* directory, const char* const* states, uint32_t state_count, bool list, FILE* output)
{
    uint32_t added = 0;
    NightlyRecord* record = nightly_allocate(1, sizeof(NightlyRecord));
    for (uint32_t i = 0; i < facts->run_count; i += 1)
    {
        const NightlyRun* run = &facts->runs[i];
        bool state = false;
        for (uint32_t j = 0; j < state_count && !state; j += 1)
        {
            state = strcmp(states[j], run->head) == 0;
        }
        const bool eligible = state && run->workflow && run->completed && run->event != NIGHTLY_EVENT_PR && run->event != NIGHTLY_EVENT_OTHER;
        if (eligible && !nightly_has_row(ledger, run->id, run->attempt) && list)
        {
            // The runs whose jobs and records the glue must fetch before harvesting them.
            fprintf(output, "fetch\t%llu\t%u\n", (unsigned long long)run->id, run->attempt);
            added += 1;
        }
        else if (eligible && !nightly_has_row(ledger, run->id, run->attempt))
        {
            char path[4096];
            snprintf(path, sizeof(path), "%s/%llu.tsv", directory, (unsigned long long)run->id);
            FILE* file = fopen(path, "rb");
            bool recorded = false;
            if (file != NULL)
            {
                fclose(file);
                NightlyText text = nightly_read(path);
                recorded = nightly_record_parse(text, record);
                free(text.data);
                if (!recorded)
                {
                    fprintf(stderr, "nightly: run %llu's record is invalid; its row has the API's facts alone\n", (unsigned long long)run->id);
                }
            }
            const NightlyRow row = nightly_harvest_row(facts, run, recorded ? record : NULL);
            nightly_row_write(output, &row);
            added += 1;
        }
    }
    free(record);
    return added;
}

// --- States and the plan ---

typedef enum NightlyStatus NightlyStatus;
enum NightlyStatus
{
    NIGHTLY_STATUS_NOT_REQUIRED,
    NIGHTLY_STATUS_COMPLETE,
    NIGHTLY_STATUS_IN_FLIGHT,
    NIGHTLY_STATUS_BLOCKED_NATIVE,
    NIGHTLY_STATUS_BLOCKED_HOSTED,
    NIGHTLY_STATUS_FAILED,
    NIGHTLY_STATUS_READY,
    NIGHTLY_STATUS_PREPARING,
    NIGHTLY_STATUS_NEEDS_PREPARATION,
    NIGHTLY_STATUS_COUNT
};
static const char* const nightly_status_names[] = { "not-required", "complete", "in-flight", "blocked-native", "blocked-hosted", "failed", "ready", "preparing", "needs-preparation" };

typedef struct NightlyState NightlyState;
struct NightlyState
{
    char sha[41];
    char parent[41];
    int64_t committed;
    // The first-parent change touches more than documentation (push's paths-ignore: docs/** and **.md).
    bool code;
    // From this state's immutable nightly.c, not the coordinator's version. No metadata means no hardware admission.
    bool worker_window_recorded;
    uint32_t worker_start_hour;
    uint32_t worker_end_hour;
    // Resolved anew for the cycle, including Madrid's clock changes.
    bool worker_window_valid;
    int64_t worker_start;
    int64_t worker_deadline;
    // Folded from the ledger, the runs in flight and the pending dispatches.
    bool targeted;
    bool full;
    // This cycle's comprehensive run completed for it: the snapshot's daily requirement, which an earlier cycle's full
    // run doesn't meet (an unchanged main still gets its daily full run and oracle audit).
    bool full_this_cycle;
    uint64_t complete_run;
    uint64_t source;
    // A merge-queue candidate's run of this very commit (#1430), and whether one passed: the queue's per-merge
    // correctness, which the report checks once the queue has run.
    bool queue_record;
    bool queue_passed;
    // The latest verdicts by run and then attempt: a rerun keeps its run's ID and takes the next attempt.
    NightlyPlain plain;
    uint64_t plain_run;
    uint32_t plain_attempt;
    bool blocked_native;
    uint64_t blocked_run;
    uint64_t admitted_run;
    uint32_t admitted_attempt;
    uint32_t failures;
    bool admit_in_flight;
    bool defer_in_flight;
    uint64_t admit_run;
    int64_t admit_since;
    NightlyStatus status;
    // This plan dispatched it: its nightly/SHA ref is in use now, whatever its status was before the plan.
    bool dispatched;
    // This job can't have its nightly/SHA branch (the workflow token's refusal, or a branch at another commit): nothing
    // is dispatched for it again in this job, so that it neither holds a preparation slot nor stays the oldest ready
    // state all night.
    bool refused;
};

typedef struct NightlyStates NightlyStates;
struct NightlyStates
{
    NightlyState* items;
    uint32_t count;
};

// `state SHA PARENT EPOCH` lines, oldest first, each followed by `worker-window START END` (hours, or - - for unknown)
// and its first-parent change's `path PATH` lines. Older input without the window remains readable, but can't admit.
static bool nightly_states_parse(NightlyStates* states, NightlyText text)
{
    size_t offset = 0;
    char line[BUSTER_NIGHTLY_LINE];
    bool line_valid = true;
    bool valid = true;
    uint32_t paths = 0;
    while (valid && nightly_next_line(text, &offset, line, &line_valid))
    {
        char* fields[BUSTER_NIGHTLY_FIELDS];
        const size_t count = line_valid ? nightly_split(line, fields, BUSTER_NIGHTLY_FIELDS) : 0;
        valid = line_valid && count >= 2;
        if (valid && strcmp(fields[0], "state") == 0)
        {
            NightlyState state = { 0 };
            valid = count == 4 && states->count < BUSTER_NIGHTLY_STATES && nightly_sha(fields[1]) && (nightly_sha(fields[2]) || strcmp(fields[2], "-") == 0) && nightly_epoch(fields[3], &state.committed);
            for (uint32_t i = 0; valid && i < states->count; i += 1)
            {
                valid = strcmp(states->items[i].sha, fields[1]) != 0;
            }
            if (valid)
            {
                nightly_copy(state.sha, sizeof(state.sha), fields[1]);
                nightly_copy(state.parent, sizeof(state.parent), strcmp(fields[2], "-") == 0 ? "" : fields[2]);
                states->items[states->count] = state;
                states->count += 1;
            }
        }
        else if (valid && strcmp(fields[0], "worker-window") == 0)
        {
            uint64_t start = 0;
            uint64_t end = 0;
            const bool unknown = count == 3 && strcmp(fields[1], "-") == 0 && strcmp(fields[2], "-") == 0;
            valid = count == 3 && states->count != 0 && !states->items[states->count - 1].worker_window_recorded;
            valid = valid && (unknown || (nightly_number(fields[1], 23, &start) && nightly_number(fields[2], 23, &end) && nightly_worker_hours(start, end)));
            if (valid)
            {
                NightlyState* state = &states->items[states->count - 1];
                state->worker_window_recorded = true;
                state->worker_start_hour = (uint32_t)start;
                state->worker_end_hour = (uint32_t)end;
            }
        }
        else if (valid && strcmp(fields[0], "path") == 0)
        {
            valid = count == 2 && states->count != 0 && fields[1][0] != 0 && paths < BUSTER_NIGHTLY_PATHS;
            if (valid)
            {
                const size_t length = strlen(fields[1]);
                const bool documentation = strncmp(fields[1], "docs/", 5) == 0 || (length >= 3 && strcmp(fields[1] + length - 3, ".md") == 0);
                states->items[states->count - 1].code |= !documentation;
                paths += 1;
            }
        }
        else
        {
            valid = false;
        }
    }
    return valid;
}

static int64_t nightly_state_index(const NightlyStates* states, const char* sha)
{
    int64_t found = -1;
    for (uint32_t i = 0; i < states->count && found < 0; i += 1)
    {
        found = strcmp(states->items[i].sha, sha) == 0 ? (int64_t)i : -1;
    }
    return found;
}

// A request token, `YYYYMMDD.COORDINATOR.SEQUENCE.admit|defer`: its cycle (as YYYY-MM-DD) and its flavor.
static bool nightly_request(const char* token, char* cycle, bool* admit)
{
    char date[9] = { 0 };
    char flavor[8] = { 0 };
    unsigned long long coordinator = 0;
    unsigned long long sequence = 0;
    int consumed = 0;
    bool valid = token != NULL && nightly_token(token, BUSTER_NIGHTLY_TOKEN) && sscanf(token, "%8[0-9].%llu.%llu.%7[a-z]%n", date, &coordinator, &sequence, flavor, &consumed) == 4;
    valid = valid && token[consumed] == 0 && strlen(date) == 8 && (strcmp(flavor, "admit") == 0 || strcmp(flavor, "defer") == 0);
    if (valid)
    {
        snprintf(cycle, 11, "%.4s-%.2s-%.2s", date, date + 4, date + 6);
        *admit = strcmp(flavor, "admit") == 0;
    }
    return valid;
}

typedef struct NightlyPending NightlyPending;
struct NightlyPending
{
    char request[BUSTER_NIGHTLY_TOKEN];
    NightlyPurpose purpose;
    bool admit;
    char sha[41];
    int64_t dispatched;
};

// Facts take over once a request is visible, whether queued, running or completed. Until then the existing
// visibility grace keeps it in flight. Future timestamps stay pending too, rather than permitting a duplicate.
static bool nightly_pending_active(const NightlyPending* pending, const NightlyFacts* facts, int64_t now)
{
    bool active = now - pending->dispatched < BUSTER_NIGHTLY_PENDING_AGE;
    const uint32_t count = facts->run_count;
    const NightlyRun* runs = facts->runs;
    for (uint32_t i = 0; active && i < count; i += 1)
    {
        active = !(runs[i].workflow && runs[i].nightly && strcmp(runs[i].request, pending->request) == 0);
    }
    return active;
}

// Validate every row before discarding observed or expired requests: a malformed historical row still rejects the
// input. Only unseen, unexpired requests consume the bounded active set. The file is replaced only after all of the
// plan's inputs and options validate, so a late malformed row or active overflow cannot erase the handoff.
static bool nightly_pending_parse(NightlyText text, const NightlyFacts* facts, int64_t now, NightlyPending* pending, uint32_t* pending_count)
{
    size_t offset = 0;
    char line[BUSTER_NIGHTLY_LINE];
    bool valid = true;
    *pending_count = 0;
    while (valid && nightly_next_line(text, &offset, line, &valid))
    {
        char* fields[BUSTER_NIGHTLY_FIELDS];
        const size_t count = valid ? nightly_split(line, fields, BUSTER_NIGHTLY_FIELDS) : 0;
        NightlyPending item = { 0 };
        const size_t purpose = count == 6 ? nightly_find(fields[2], nightly_purpose_names, NIGHTLY_PURPOSE_COUNT) : NIGHTLY_PURPOSE_COUNT;
        valid = valid && count == 6 && strcmp(fields[0], "pending") == 0 && nightly_token(fields[1], sizeof(item.request)) && purpose < NIGHTLY_PURPOSE_COUNT;
        valid = valid && (strcmp(fields[3], "admit") == 0 || strcmp(fields[3], "defer") == 0) && nightly_sha(fields[4]) && nightly_epoch(fields[5], &item.dispatched);
        if (valid)
        {
            nightly_copy(item.request, sizeof(item.request), fields[1]);
            item.purpose = (NightlyPurpose)purpose;
            item.admit = strcmp(fields[3], "admit") == 0;
            nightly_copy(item.sha, sizeof(item.sha), fields[4]);
            if (nightly_pending_active(&item, facts, now))
            {
                valid = *pending_count < BUSTER_NIGHTLY_PENDING;
                if (valid)
                {
                    pending[*pending_count] = item;
                    *pending_count += 1;
                }
            }
        }
    }
    return valid;
}

// The coordinator owns this ephemeral file. Publish compaction atomically so interrupted writes cannot turn an
// unseen admission into an empty or partial handoff, including when the next job must recover without a ledger.
static void nightly_pending_write(const char* path, const NightlyPending* pending, uint32_t count)
{
    const size_t capacity = strlen(path) + sizeof(".tmp.XXXXXX");
    char* temporary = nightly_allocate(capacity, 1);
    snprintf(temporary, capacity, "%s.tmp.XXXXXX", path);
    const int descriptor = mkstemp(temporary);
    FILE* output = descriptor >= 0 ? fdopen(descriptor, "wb") : NULL;
    bool valid = output != NULL;
    if (output != NULL)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            const NightlyPending* item = &pending[i];
            fprintf(output, "pending\t%s\t%s\t%s\t%s\t%lld\n", item->request, nightly_purpose_names[item->purpose], item->admit ? "admit" : "defer", item->sha, (long long)item->dispatched);
        }
        valid = !ferror(output);
        valid = fclose(output) == 0 && valid;
    }
    else if (descriptor >= 0)
    {
        close(descriptor);
    }
    if (valid)
    {
        valid = rename(temporary, path) == 0;
    }
    if (!valid && descriptor >= 0)
    {
        remove(temporary);
    }
    free(temporary);
    if (!valid)
    {
        nightly_fatal("cannot replace pending dispatches", path);
    }
}

typedef struct NightlyPlanInput NightlyPlanInput;
struct NightlyPlanInput
{
    const NightlyStates* states;
    const NightlyLedger* ledger;
    const NightlyFacts* facts;
    const NightlyPending* pending;
    uint32_t pending_count;
    // nightly/SHA branches that exist.
    char (*refs)[41];
    uint32_t ref_count;
    // States whose nightly/SHA branch this job can't have.
    char (*refused)[41];
    uint32_t refused_count;
    int64_t now;
    const char* main;
    bool run;
    uint64_t coordinator;
    uint32_t sequence;
    // Limits: hosted preparations in flight and per cycle, admitted failures per state, a hardware run's lease, and
    // the bounds (seconds from dispatch to its end) of a backlog and a snapshot run.
    uint32_t preparations;
    uint32_t preparations_per_cycle;
    uint32_t attempts;
    int64_t lease;
    int64_t backlog_bound;
    int64_t snapshot_bound;
    // A manual run's reason to admit hardware outside 02:00-08:00 Europe/Madrid (sanitized), or empty.
    const char* override;
    // First-parent states of the last fortnight before the epoch: out of scope, reported.
    uint32_t earlier;
    // The prepare job's steps: hosted preparations alone, never a hardware admission, which is the window job's, so that
    // a step at or after 02:00 (or under an override) can't dispatch one the window job doesn't know of.
    bool prepare_only;
};

typedef struct NightlyCycleState NightlyCycleState;
struct NightlyCycleState
{
    NightlyWindow window;
    char date[11];
    // The cycle's pinned snapshot: its state, and whether it was pinned now.
    int64_t snapshot;
    bool pinned_now;
    bool snapshot_prepared;
    bool snapshot_attempted;
    // The state measured as the snapshot instead (-1: the snapshot itself or none), and why.
    int64_t substitute;
    const char* substitute_reason;
    uint32_t preparations_in_flight;
    uint32_t preparations_this_cycle;
    bool admit_in_flight;
    // The state this cycle's comprehensive run measured or measures (-1: none yet).
    int64_t measured;
    uint32_t selected;
    uint32_t completed;
    uint32_t failed;
    uint32_t deferred;
    // States in scope whose nightly/SHA branch this job can't have, and so skips.
    uint32_t refused;
};

static bool nightly_request_this_cycle(const char* request, const char* date, bool* admit)
{
    char cycle[11] = { 0 };
    return nightly_request(request, cycle, admit) && strcmp(cycle, date) == 0;
}

// Whether run `run`'s attempt `attempt` is later than `than_run`'s attempt `than_attempt` (a rerun keeps its run's ID).
static bool nightly_later(uint64_t run, uint32_t attempt, uint64_t than_run, uint32_t than_attempt)
{
    return run > than_run || (run == than_run && attempt > than_attempt);
}

// Whether the ledger's row `index` is its run's latest attempt in the ledger.
static bool nightly_latest_attempt(const NightlyLedger* ledger, uint32_t index)
{
    bool latest = true;
    for (uint32_t i = 0; i < ledger->row_count && latest; i += 1)
    {
        latest = ledger->rows[i].run != ledger->rows[index].run || ledger->rows[i].attempt <= ledger->rows[index].attempt;
    }
    return latest;
}

// Folds the rows, the runs in flight and the pending dispatches into each state, and into the cycle's bookkeeping.
// A state's verdicts are its latest run's latest attempt's, whatever order the ledger holds them in. Each failed or
// interrupted attempt counts toward the state's failures, as each one occupied the 9700X; the cycle counts each run
// once, by its latest attempt.
static void nightly_fold(const NightlyPlanInput* input, NightlyCycleState* cycle)
{
    NightlyStates* states = (NightlyStates*)input->states;
    const NightlyLedger* ledger = input->ledger;
    for (uint32_t i = 0; i < ledger->row_count; i += 1)
    {
        const NightlyRow* row = &ledger->rows[i];
        const int64_t index = row->purpose != NIGHTLY_PURPOSE_PR ? nightly_state_index(states, row->commit) : -1;
        bool admit = false;
        const bool this_cycle = nightly_request_this_cycle(row->request, cycle->date, &admit);
        if (index >= 0)
        {
            NightlyState* state = &states->items[index];
            const bool complete = row->outcome == NIGHTLY_OUTCOME_COMPLETE && (row->coverage == NIGHTLY_COVERAGE_TARGETED || row->coverage == NIGHTLY_COVERAGE_FULL);
            state->targeted |= complete;
            state->full |= complete && row->coverage == NIGHTLY_COVERAGE_FULL;
            state->full_this_cycle |= complete && row->coverage == NIGHTLY_COVERAGE_FULL && this_cycle && admit && row->purpose == NIGHTLY_PURPOSE_SNAPSHOT;
            state->source = complete && row->source != 0 ? row->source : state->source;
            state->complete_run = complete && row->run > state->complete_run ? row->run : state->complete_run;
            // A candidate's pass is its deferral: its hosted contract passed, its hardware is the window's.
            state->queue_record |= row->purpose == NIGHTLY_PURPOSE_QUEUE;
            state->queue_passed |= row->purpose == NIGHTLY_PURPOSE_QUEUE && row->outcome == NIGHTLY_OUTCOME_DEFERRED;
            if (row->plain != NIGHTLY_PLAIN_NONE && nightly_later(row->run, row->attempt, state->plain_run, state->plain_attempt))
            {
                state->plain = row->plain;
                state->plain_run = row->run;
                state->plain_attempt = row->attempt;
            }
            const bool admitted = row->hardware == NIGHTLY_HARDWARE_ADMIT;
            if (admitted && nightly_later(row->run, row->attempt, state->admitted_run, state->admitted_attempt))
            {
                state->admitted_run = row->run;
                state->admitted_attempt = row->attempt;
                state->blocked_native = row->outcome == NIGHTLY_OUTCOME_BLOCKED_NATIVE;
                state->blocked_run = state->blocked_native ? row->run : state->blocked_run;
            }
            const bool nightly = row->purpose == NIGHTLY_PURPOSE_BACKLOG || row->purpose == NIGHTLY_PURPOSE_SNAPSHOT;
            state->failures += nightly && admitted && (row->outcome == NIGHTLY_OUTCOME_FAILED || row->outcome == NIGHTLY_OUTCOME_INTERRUPTED);
        }
        if (this_cycle && row->purpose == NIGHTLY_PURPOSE_SNAPSHOT)
        {
            cycle->snapshot_prepared |= !admit;
            cycle->snapshot_attempted |= admit && row->outcome != NIGHTLY_OUTCOME_DEFERRED;
            cycle->measured = admit && row->outcome != NIGHTLY_OUTCOME_DEFERRED ? index : cycle->measured;
        }
        // The cycle's counts: each run once, by its latest attempt (a rerun is no second dispatch).
        const bool counted = this_cycle && nightly_latest_attempt(ledger, i);
        if (counted && row->purpose == NIGHTLY_PURPOSE_BACKLOG && !admit)
        {
            cycle->preparations_this_cycle += 1;
        }
        if (counted && admit)
        {
            cycle->selected += 1;
            cycle->completed += row->outcome == NIGHTLY_OUTCOME_COMPLETE;
            cycle->failed += row->outcome == NIGHTLY_OUTCOME_FAILED || row->outcome == NIGHTLY_OUTCOME_INTERRUPTED || row->outcome == NIGHTLY_OUTCOME_BLOCKED_NATIVE || row->outcome == NIGHTLY_OUTCOME_BLOCKED_HOSTED;
            cycle->deferred += row->outcome == NIGHTLY_OUTCOME_DEFERRED;
        }
    }
    // Runs in flight, by their run names, and dispatches not yet visible as runs.
    const NightlyFacts* facts = input->facts;
    for (uint32_t i = 0; i < facts->run_count + input->pending_count; i += 1)
    {
        const bool is_run = i < facts->run_count;
        const NightlyRun* run = is_run ? &facts->runs[i] : NULL;
        const NightlyPending* pending = is_run ? NULL : &input->pending[i - facts->run_count];
        const bool flying = is_run ? run->workflow && run->nightly && !run->completed : nightly_pending_active(pending, facts, input->now);
        if (!flying)
        {
            continue;
        }
        const char* sha = is_run ? run->target : pending->sha;
        const bool admit = is_run ? run->admit : pending->admit;
        const NightlyPurpose purpose = is_run ? run->purpose : pending->purpose;
        const char* request = is_run ? run->request : pending->request;
        const int64_t since = is_run ? run->created : pending->dispatched;
        const int64_t index = nightly_state_index(states, sha);
        bool flavor = false;
        const bool this_cycle = nightly_request_this_cycle(request, cycle->date, &flavor);
        if (index >= 0)
        {
            NightlyState* state = &states->items[index];
            state->admit_in_flight |= admit;
            state->defer_in_flight |= !admit;
            if (admit && is_run && (state->admit_run == 0 || run->id < state->admit_run))
            {
                state->admit_run = run->id;
                state->admit_since = since;
            }
        }
        cycle->admit_in_flight |= admit;
        cycle->preparations_in_flight += !admit && purpose == NIGHTLY_PURPOSE_BACKLOG;
        if (this_cycle && purpose == NIGHTLY_PURPOSE_SNAPSHOT)
        {
            cycle->snapshot_prepared |= !admit;
            cycle->snapshot_attempted |= admit;
            cycle->measured = admit ? index : cycle->measured;
        }
        cycle->preparations_this_cycle += this_cycle && purpose == NIGHTLY_PURPOSE_BACKLOG && !admit;
        cycle->selected += this_cycle && admit;
    }
    for (uint32_t i = 0; i < states->count; i += 1)
    {
        NightlyState* state = &states->items[i];
        NightlyStatus status = NIGHTLY_STATUS_NEEDS_PREPARATION;
        if (!state->code)
        {
            status = NIGHTLY_STATUS_NOT_REQUIRED;
        }
        else if (state->targeted)
        {
            status = NIGHTLY_STATUS_COMPLETE;
        }
        else if (state->admit_in_flight)
        {
            status = NIGHTLY_STATUS_IN_FLIGHT;
        }
        else if (state->blocked_native)
        {
            status = NIGHTLY_STATUS_BLOCKED_NATIVE;
        }
        else if (state->plain == NIGHTLY_PLAIN_FAILED)
        {
            status = NIGHTLY_STATUS_BLOCKED_HOSTED;
        }
        else if (state->failures >= input->attempts)
        {
            status = NIGHTLY_STATUS_FAILED;
        }
        else if (state->plain == NIGHTLY_PLAIN_OK)
        {
            status = NIGHTLY_STATUS_READY;
        }
        else if (state->defer_in_flight)
        {
            status = NIGHTLY_STATUS_PREPARING;
        }
        state->status = status;
    }
}

static bool nightly_pending_status(NightlyStatus status)
{
    return status == NIGHTLY_STATUS_IN_FLIGHT || status == NIGHTLY_STATUS_READY || status == NIGHTLY_STATUS_PREPARING || status == NIGHTLY_STATUS_NEEDS_PREPARATION;
}

// `dispatch PURPOSE admit|defer SHA REQUEST SNAPSHOT`: the run's state, its request token and the cycle's pinned
// snapshot, which the worker checks the state's first-parent ancestry against and records (not main's head now, which
// may have moved on since the pin). The state is marked dispatched, so that the same plan never prunes its ref.
static void nightly_dispatch(FILE* output, const NightlyPlanInput* input, NightlyCycleState* cycle, uint32_t* sequence, NightlyPurpose purpose, bool admit, NightlyState* state)
{
    char date[9];
    snprintf(date, sizeof(date), "%.4s%.2s%.2s", cycle->date, cycle->date + 5, cycle->date + 8);
    fprintf(output, "%s\t%s\t%s\t%s\t%s.%llu.%u.%s\t%s\n", input->run ? "dispatch" : "would-dispatch", nightly_purpose_names[purpose], admit ? "admit" : "defer", state->sha, date, (unsigned long long)input->coordinator, *sequence, admit ? "admit" : "defer", input->states->items[cycle->snapshot].sha);
    state->dispatched = true;
    *sequence += 1;
}

static bool nightly_terminal(NightlyStatus status)
{
    return status == NIGHTLY_STATUS_NOT_REQUIRED || status == NIGHTLY_STATUS_COMPLETE || status == NIGHTLY_STATUS_BLOCKED_NATIVE || status == NIGHTLY_STATUS_BLOCKED_HOSTED || status == NIGHTLY_STATUS_FAILED;
}

static void nightly_run_url(char* output, size_t capacity, uint64_t run)
{
    snprintf(output, capacity, "https://github.com/buster14a/buster/actions/runs/%llu", (unsigned long long)run);
}

// The report (Markdown, for the job summary and the ledger's publication) and the capacity alert.
static void nightly_report(const NightlyPlanInput* input, const NightlyCycleState* cycle, FILE* output, FILE* report)
{
    const NightlyStates* states = input->states;
    const int64_t scope = cycle->snapshot;
    uint32_t counts[NIGHTLY_STATUS_COUNT] = { 0 };
    uint32_t reused = 0;
    uint32_t unknown_windows = 0;
    int64_t oldest = -1;
    for (int64_t i = 0; i <= scope; i += 1)
    {
        const NightlyState* state = &states->items[i];
        counts[state->status] += 1;
        reused += state->status == NIGHTLY_STATUS_COMPLETE && state->source != 0;
        oldest = oldest < 0 && nightly_pending_status(state->status) ? i : oldest;
        unknown_windows += nightly_pending_status(state->status) && !state->worker_window_valid;
    }
    uint32_t pending = counts[NIGHTLY_STATUS_IN_FLIGHT] + counts[NIGHTLY_STATUS_READY] + counts[NIGHTLY_STATUS_PREPARING] + counts[NIGHTLY_STATUS_NEEDS_PREPARATION];
    // Capacity: code states per day over the last week (or the shorter span the states cover, at least a day) against
    // completed nightly measurements per cycle over it.
    uint32_t arrivals = 0;
    const int64_t covered = input->now - states->items[0].committed;
    const double span = covered < 86400 ? 1.0 : covered > 7 * 86400 ? 7.0 : (double)covered / 86400.0;
    for (uint32_t i = 0; i < states->count; i += 1)
    {
        arrivals += states->items[i].code && states->items[i].committed >= input->now - 7 * 86400;
    }
    char week[11] = { 0 };
    int64_t earlier = 0;
    NightlyLocal local;
    if (nightly_madrid_instant(cycle->window.local.year, cycle->window.local.month, cycle->window.local.day - 7, 12, &earlier) && nightly_madrid(earlier, &local))
    {
        snprintf(week, sizeof(week), "%04d-%02d-%02d", local.year, local.month, local.day);
    }
    char dates[8][11] = { { 0 } };
    uint32_t date_count = 0;
    uint32_t completions = 0;
    for (uint32_t i = 0; i < input->ledger->row_count; i += 1)
    {
        const NightlyRow* row = &input->ledger->rows[i];
        char date[11] = { 0 };
        bool admit = false;
        if (nightly_request(row->request, date, &admit) && admit && strcmp(date, week) > 0 && strcmp(date, cycle->date) <= 0)
        {
            completions += row->outcome == NIGHTLY_OUTCOME_COMPLETE;
            bool known = false;
            for (uint32_t j = 0; j < date_count; j += 1)
            {
                known = known || strcmp(dates[j], date) == 0;
            }
            if (!known && date_count < 8)
            {
                nightly_copy(dates[date_count], sizeof(dates[date_count]), date);
                date_count += 1;
            }
        }
    }
    const double arrival_rate = arrivals / span;
    const double completion_rate = date_count != 0 ? (double)completions / date_count : 0.0;
    const double oldest_hours = oldest >= 0 ? (double)(input->now - states->items[oldest].committed) / 3600.0 : 0.0;
    const bool alert = pending != 0 && ((date_count != 0 && arrival_rate > completion_rate) || oldest_hours > 72.0);

    const char* phase = input->now < cycle->window.start ? "before the window" : input->now < cycle->window.deadline ? "inside the window" : "after the window";
    fprintf(report, "## Nightly main measurements: cycle %s\n\n", cycle->date);
    fprintf(report, "Mode: **%s**. Europe/Madrid time %02d:%02d %s, %s (02:00 to 08:00).\n\n", input->run ? "run" : "dry run (nothing dispatched)", cycle->window.local.hour, cycle->window.local.minute, cycle->window.local.zone, phase);
    if (input->run && input->override != NULL && input->override[0] != 0)
    {
        fprintf(report, "**Override**: hardware is admitted outside 02:00 to 08:00 Europe/Madrid, for: %s.\n\n", input->override);
    }
    fprintf(report, "Snapshot: `%s`%s, the newest of %lld first-parent states in scope, from `%.12s`, the epoch: the main commit that added the nightly worker.", states->items[scope].sha, cycle->pinned_now ? ", pinned now" : ", pinned earlier this cycle", (long long)scope + 1, states->items[0].sha);
    fprintf(report, " The %u first-parent states of the last fortnight before it are out of scope: their workflow can't run as a nightly worker, and their coverage is what their pushes and the old daily run measured.\n", input->earlier);
    if (cycle->substitute >= 0)
    {
        fprintf(report, "The snapshot is ineligible (%s): its comprehensive run measures `%s` instead, the newest eligible earlier state.\n", cycle->substitute_reason, states->items[cycle->substitute].sha);
    }
    else if (cycle->measured >= 0)
    {
        fprintf(report, "This cycle's comprehensive run measures `%s`%s.\n", states->items[cycle->measured].sha, cycle->measured == scope ? ", the snapshot" : ", an earlier state, since the snapshot was ineligible");
    }
    else if (cycle->substitute_reason != NULL)
    {
        fprintf(report, "No comprehensive run this cycle: %s.\n", cycle->substitute_reason);
    }
    fprintf(report, "\n| Status | States |\n| --- | ---: |\n");
    for (uint32_t i = 0; i < NIGHTLY_STATUS_COUNT; i += 1)
    {
        fprintf(report, "| %s | %u |\n", nightly_status_names[i], counts[i]);
    }
    fprintf(report, "\nComplete by a pull request's reused measurement: %u. This cycle: %u hardware runs selected, %u completed, %u failed or blocked, %u deferred; %u hosted preparations.\n", reused, cycle->selected, cycle->completed, cycle->failed, cycle->deferred, cycle->preparations_this_cycle);
    if (oldest >= 0)
    {
        fprintf(report, "Carried over: %u states; the oldest is `%s`, committed %.1f hours ago.\n", pending, states->items[oldest].sha, oldest_hours);
    }
    else
    {
        fprintf(report, "Carried over: nothing.\n");
    }
    fprintf(report, "Capacity: %.1f code states a day arrived over the last %.1f days; %.1f completed per cycle over %u cycles.\n", arrival_rate, span, completion_rate, date_count);
    if (unknown_windows != 0)
    {
        fprintf(report, "\n**Worker window unknown**: %u pending states have no supported window declaration. Their hardware is not admitted; later eligible states can still run.\n", unknown_windows);
        fprintf(output, "alert\t%u states pending: their worker window is unknown\n", unknown_windows);
    }
    if (cycle->refused != 0)
    {
        fprintf(report, "\n**Branch refused**: this job could not create or use the nightly branch of %u states, so it skips them and moves on to later states (docs/ci.md, \"Nightly main measurements\"):", cycle->refused);
        for (int64_t i = 0; i <= scope; i += 1)
        {
            if (states->items[i].refused)
            {
                fprintf(report, " `%.12s`", states->items[i].sha);
            }
        }
        fprintf(report, ".\n");
        fprintf(output, "alert\t%u states skipped: their nightly branch could not be created or used\n", cycle->refused);
    }
    // The merge queue's per-merge correctness (#1430): from the first state a candidate's run tested, every state needs a
    // passing candidate's run of its own commit. A state without one reached main some other way (a direct push, an
    // admin merge or a bypass, or a fast-forward to a commit the queue didn't test), or its candidate's run failed; its
    // correctness is then the nightly's alone. Before any candidate's run, nothing is reported.
    int64_t queue_start = -1;
    for (int64_t i = 0; i <= scope && queue_start < 0; i += 1)
    {
        queue_start = states->items[i].queue_record ? i : queue_start;
    }
    uint32_t queue_gaps = 0;
    for (int64_t i = queue_start; queue_start >= 0 && i <= scope; i += 1)
    {
        queue_gaps += !states->items[i].queue_passed;
    }
    if (queue_start >= 0)
    {
        fprintf(report, "\nMerge queue: %lld states since `%.12s`, the first a candidate's run tested; %u without a passing candidate's run of their own commit%s", (long long)(scope - queue_start + 1), states->items[queue_start].sha, queue_gaps, queue_gaps != 0 ? " (coverage gaps: their correctness is the nightly's alone):" : ".");
        uint32_t listed_gaps = 0;
        for (int64_t i = queue_start; i <= scope && listed_gaps < 30; i += 1)
        {
            if (!states->items[i].queue_passed)
            {
                fprintf(report, " `%.12s`", states->items[i].sha);
                listed_gaps += 1;
            }
        }
        fprintf(report, "%s", queue_gaps == 0 ? "\n" : queue_gaps > listed_gaps ? " ...\n" : "\n");
    }
    if (queue_gaps != 0)
    {
        fprintf(output, "alert\t%u states have no passing merge-queue run of their own commit\n", queue_gaps);
    }
    if (alert)
    {
        fprintf(report, "\n**The backlog cannot drain at this rate**: arrivals exceed the observed nightly capacity, or the oldest pending state is over three days old.\n");
        fprintf(output, "alert\t%u states pending; %.1f arrive a day against %.1f completed per cycle; the oldest is %.0f hours old\n", pending, arrival_rate, completion_rate, oldest_hours);
    }
    fprintf(report, "\n### Evidence\n\n");
    uint32_t listed = 0;
    for (int64_t i = scope; i >= 0 && listed < 40; i -= 1)
    {
        const NightlyState* state = &states->items[i];
        const uint64_t run = state->status == NIGHTLY_STATUS_COMPLETE ? (state->source != 0 ? state->source : state->complete_run) : state->status == NIGHTLY_STATUS_BLOCKED_NATIVE ? state->blocked_run : state->status == NIGHTLY_STATUS_BLOCKED_HOSTED ? state->plain_run : 0;
        if (run != 0)
        {
            char url[128];
            nightly_run_url(url, sizeof(url), run);
            fprintf(report, "- `%.12s` %s%s: %s\n", state->sha, nightly_status_names[state->status], state->source != 0 && state->status == NIGHTLY_STATUS_COMPLETE ? " (its pull request's run)" : "", url);
            listed += 1;
        }
    }
    fprintf(report, "%s\n### Remaining, oldest first\n\n", listed == 0 ? "- none yet\n" : "");
    listed = 0;
    for (int64_t i = 0; i <= scope && listed < 30; i += 1)
    {
        if (nightly_pending_status(states->items[i].status))
        {
            const NightlyState* state = &states->items[i];
            fprintf(report, "- `%.12s` %s%s", state->sha, nightly_status_names[state->status], state->refused ? " (branch refused)" : "");
            if (!state->worker_window_valid)
            {
                fprintf(report, " (worker window unknown; hardware not admitted)");
            }
            else if (state->worker_start_hour != BUSTER_NIGHTLY_WINDOW_START_HOUR || state->worker_end_hour != BUSTER_NIGHTLY_WINDOW_END_HOUR)
            {
                fprintf(report, " (worker window %02u:00-%02u:00 Europe/Madrid)", state->worker_start_hour, state->worker_end_hour);
            }
            fprintf(report, "\n");
            listed += 1;
        }
    }
    fprintf(report, "%s", listed == 0 ? "- none\n" : pending > listed ? "- ...\n" : "");
}

// Whether work of the bound fits the window from now; with an override (a person's reason, reported), always.
static bool nightly_fits(const NightlyPlanInput* input, const NightlyCycleState* cycle, int64_t now, int64_t bound)
{
    const bool overridden = input->run && input->override != NULL && input->override[0] != 0;
    return overridden || (now >= cycle->window.start && now + bound <= cycle->window.deadline);
}

// Intersect today's coordinator policy with the worker at the measured SHA. The existing manual override still
// applies to known workers, but it never turns missing or unsupported metadata into permission to run.
static bool nightly_worker_fits(const NightlyPlanInput* input, const NightlyCycleState* cycle, const NightlyState* state, int64_t now, int64_t bound)
{
    const bool overridden = input->run && input->override != NULL && input->override[0] != 0;
    return state->worker_window_valid && nightly_fits(input, cycle, now, bound) && (overridden || (now >= state->worker_start && now + bound <= state->worker_deadline));
}

// Whether this worker can still run this cycle, waiting for its opening if necessary. It must remain pending while
// it is too early, rather than ending the window job before old 04:00 workers can start.
static bool nightly_worker_later(const NightlyPlanInput* input, const NightlyCycleState* cycle, const NightlyState* state, int64_t now, int64_t bound)
{
    const bool overridden = input->run && input->override != NULL && input->override[0] != 0;
    const int64_t start = !overridden && now < state->worker_start ? state->worker_start : now;
    return nightly_worker_fits(input, cycle, state, start, bound);
}

// One step of the coordinator: the actions on stdout, one a line (pin, dispatch or would-dispatch, cancel or
// would-cancel, prune or would-prune, alert), then exactly one `wait` or `done` line; the report to `report`. Before
// the window it prepares (the snapshot's daily hosted run, the hosted rows of the states without them); inside it, it
// admits one hardware run at a time: the snapshot's comprehensive run while it fits, then the oldest ready state,
// keeping room for the snapshot while its hosted qualification is in flight. Nothing waits for a slot past 08:00, and
// a step with a hardware run in flight ends the job once no backlog run could follow it before 08:00: the window job
// then fits GitHub's six hours, and the run in flight ends by 08:00 under its own bound.
static void nightly_plan(const NightlyPlanInput* input, FILE* output, FILE* report)
{
    NightlyCycleState cycle = {
        .snapshot = -1,
        .substitute = -1,
        .measured = -1,
    };
    cycle.window = nightly_window(input->now, 0);
    NightlyStates* states = (NightlyStates*)input->states;
    const char* failure = NULL;
    if (!cycle.window.valid)
    {
        failure = "no Europe/Madrid time";
    }
    else
    {
        nightly_copy(cycle.date, sizeof(cycle.date), cycle.window.cycle);
        for (uint32_t i = 0; i < input->ledger->cycle_count; i += 1)
        {
            if (strcmp(input->ledger->cycles[i].date, cycle.date) == 0)
            {
                cycle.snapshot = nightly_state_index(states, input->ledger->cycles[i].snapshot);
                failure = cycle.snapshot < 0 ? "the cycle's pinned snapshot isn't a first-parent state of main" : NULL;
                // An index of -2 keeps the failure apart from "not pinned yet".
                cycle.snapshot = cycle.snapshot < 0 ? -2 : cycle.snapshot;
            }
        }
        // Before the pin's lead, a step pins and plans nothing, unless a person's override admits work now.
        const bool overridden = input->run && input->override != NULL && input->override[0] != 0;
        if (cycle.snapshot == -1 && input->now < cycle.window.pin && !overridden)
        {
            failure = "it is before 22:00 Europe/Madrid, the evening before the window: the cycle's snapshot isn't pinned yet";
        }
        else if (cycle.snapshot == -1)
        {
            cycle.snapshot = nightly_state_index(states, input->main);
            cycle.pinned_now = cycle.snapshot >= 0;
            failure = cycle.snapshot < 0 ? "main's head isn't among the states" : NULL;
            if (cycle.pinned_now)
            {
                fprintf(output, "pin\tcycle\t%s\t%s\t%llu\t%lld\n", cycle.date, input->main, (unsigned long long)input->coordinator, (long long)input->now);
            }
        }
    }
    if (failure != NULL)
    {
        fprintf(output, "done\t%s\n", failure);
        fprintf(report, "## Nightly main measurements\n\nNo plan: %s.\n", failure);
    }
    else
    {
        NightlyLocal date;
        const bool dated = nightly_madrid(cycle.window.deadline, &date);
        for (uint32_t i = 0; i < states->count; i += 1)
        {
            NightlyState* state = &states->items[i];
            state->worker_window_valid = dated && nightly_worker_hours(state->worker_start_hour, state->worker_end_hour);
            state->worker_window_valid = state->worker_window_valid && nightly_madrid_first(date.year, date.month, date.day, (int)state->worker_start_hour, &state->worker_start);
            state->worker_window_valid = state->worker_window_valid && nightly_madrid_instant(date.year, date.month, date.day, (int)state->worker_end_hour, &state->worker_deadline);
        }
        nightly_fold(input, &cycle);
        uint32_t refused = 0;
        for (uint32_t i = 0; i < input->refused_count; i += 1)
        {
            const int64_t index = nightly_state_index(states, input->refused[i]);
            if (index >= 0 && index <= cycle.snapshot && !states->items[index].refused)
            {
                states->items[index].refused = true;
                refused += 1;
            }
        }
        cycle.refused = refused;
        const int64_t now = input->now;
        const bool overridden = input->run && input->override != NULL && input->override[0] != 0;
        // An override admits hardware as inside the window would, whatever the time; it is reported.
        const bool before = !overridden && now < cycle.window.start;
        const bool inside = overridden || (!before && now < cycle.window.deadline);
        NightlyState* snapshot = &states->items[cycle.snapshot];
        uint32_t sequence = input->sequence;
        const char* verdict = NULL;
        bool done = false;
        if (before || inside)
        {
            for (int64_t i = 0; i <= cycle.snapshot; i += 1)
            {
                const NightlyState* state = &states->items[i];
                if (state->admit_run != 0 && now - state->admit_since > input->lease)
                {
                    fprintf(output, "%s\t%llu\tlease-expired\n", input->run ? "cancel" : "would-cancel", (unsigned long long)state->admit_run);
                }
            }
            if (!cycle.snapshot_prepared && !snapshot->refused)
            {
                // The daily diagnostic run: every hosted row and the fuzz campaigns of the snapshot, before measuring.
                nightly_dispatch(output, input, &cycle, &sequence, NIGHTLY_PURPOSE_SNAPSHOT, false, snapshot);
                cycle.snapshot_prepared = true;
                snapshot->defer_in_flight = true;
                snapshot->status = snapshot->status == NIGHTLY_STATUS_NEEDS_PREPARATION ? NIGHTLY_STATUS_PREPARING : snapshot->status;
            }
            for (int64_t i = 0; i <= cycle.snapshot; i += 1)
            {
                NightlyState* state = &states->items[i];
                if (state->status == NIGHTLY_STATUS_NEEDS_PREPARATION && !state->refused && cycle.preparations_in_flight < input->preparations && cycle.preparations_this_cycle < input->preparations_per_cycle)
                {
                    nightly_dispatch(output, input, &cycle, &sequence, NIGHTLY_PURPOSE_BACKLOG, false, state);
                    state->status = NIGHTLY_STATUS_PREPARING;
                    state->defer_in_flight = true;
                    cycle.preparations_in_flight += 1;
                    cycle.preparations_this_cycle += 1;
                }
            }
        }
        if (inside && input->prepare_only)
        {
            verdict = "prepared; the window job admits hardware";
            done = true;
        }
        else if (inside && cycle.admit_in_flight && !nightly_fits(input, &cycle, now, input->backlog_bound))
        {
            verdict = "a hardware run is in flight, and no more hardware work fits before 08:00";
            done = true;
        }
        else if (inside && cycle.admit_in_flight)
        {
            verdict = "a hardware run is in flight";
        }
        else if (inside)
        {
            bool snapshot_pending = !snapshot->full_this_cycle && !cycle.snapshot_attempted;
            int64_t target = -1;
            bool waiting = false;
            bool waiting_window = false;
            if (snapshot_pending)
            {
                const bool blocked_window = !nightly_worker_later(input, &cycle, snapshot, now, input->snapshot_bound);
                const bool blocked = snapshot->plain == NIGHTLY_PLAIN_FAILED || snapshot->blocked_native || snapshot->status == NIGHTLY_STATUS_FAILED || snapshot->refused || blocked_window;
                if (snapshot->plain == NIGHTLY_PLAIN_OK && !blocked)
                {
                    target = cycle.snapshot;
                }
                else if (blocked)
                {
                    cycle.substitute_reason = snapshot->refused ? "its nightly branch could not be created or used" : snapshot->plain == NIGHTLY_PLAIN_FAILED ? "its plain hosted rows failed" : snapshot->blocked_native ? "its native correctness failed on the 9700X" : snapshot->status == NIGHTLY_STATUS_FAILED ? "its measurements failed repeatedly" : !snapshot->worker_window_valid ? "its worker window is unknown" : "its comprehensive run no longer fits its worker window";
                    int64_t later = -1;
                    for (int64_t i = cycle.snapshot - 1; i >= 0 && target < 0; i -= 1)
                    {
                        const NightlyState* state = &states->items[i];
                        const bool qualified = state->plain == NIGHTLY_PLAIN_OK && !state->blocked_native && state->status != NIGHTLY_STATUS_FAILED && !state->refused;
                        if (qualified && nightly_worker_fits(input, &cycle, state, now, input->snapshot_bound))
                        {
                            target = i;
                        }
                        else if (qualified && later < 0 && nightly_worker_later(input, &cycle, state, now, input->snapshot_bound))
                        {
                            later = i;
                        }
                    }
                    target = target >= 0 ? target : later;
                    cycle.substitute = target;
                    snapshot_pending = target >= 0;
                    cycle.substitute_reason = target >= 0 ? cycle.substitute_reason : "the snapshot and every earlier state are ineligible";
                }
                else
                {
                    waiting = snapshot->defer_in_flight;
                    snapshot_pending = waiting;
                    cycle.substitute_reason = waiting ? NULL : "the snapshot has no plain hosted rows, and none are in flight";
                }
            }
            if (target >= 0 && nightly_worker_fits(input, &cycle, &states->items[target], now, input->snapshot_bound))
            {
                nightly_dispatch(output, input, &cycle, &sequence, NIGHTLY_PURPOSE_SNAPSHOT, true, &states->items[target]);
                cycle.selected += 1;
                cycle.measured = target;
                verdict = "the snapshot's comprehensive run is dispatched";
            }
            else
            {
                if (target >= 0)
                {
                    waiting_window = nightly_worker_later(input, &cycle, &states->items[target], now, input->snapshot_bound);
                    waiting = waiting_window;
                    cycle.substitute_reason = waiting_window ? "the selected worker's window has not opened; its comprehensive run remains pending" : "the comprehensive run no longer fits before 08:00";
                    cycle.substitute = -1;
                    snapshot_pending = waiting;
                }
                const int64_t reserved = snapshot_pending && waiting ? (target >= 0 ? target : cycle.snapshot) : -1;
                const bool room_after = reserved < 0 || nightly_worker_later(input, &cycle, &states->items[reserved], now + input->backlog_bound, input->snapshot_bound);
                int64_t next = -1;
                bool ready = false;
                bool preparing = false;
                bool ready_later = false;
                bool preparing_later = false;
                for (int64_t i = 0; i <= cycle.snapshot; i += 1)
                {
                    const NightlyState* state = &states->items[i];
                    const bool is_ready = state->status == NIGHTLY_STATUS_READY && !state->refused && i != reserved;
                    const bool is_preparing = state->status == NIGHTLY_STATUS_PREPARING && !state->refused;
                    ready |= is_ready;
                    preparing |= is_preparing;
                    if (is_ready || is_preparing)
                    {
                        const bool later = nightly_worker_later(input, &cycle, state, now, input->backlog_bound);
                        ready_later |= is_ready && later;
                        preparing_later |= is_preparing && later;
                    }
                    if (next < 0 && is_ready && room_after && nightly_worker_fits(input, &cycle, state, now, input->backlog_bound))
                    {
                        next = i;
                    }
                }
                if (next >= 0)
                {
                    nightly_dispatch(output, input, &cycle, &sequence, NIGHTLY_PURPOSE_BACKLOG, true, &states->items[next]);
                    cycle.selected += 1;
                    verdict = "the oldest ready state is dispatched";
                }
                else if (reserved >= 0 && nightly_worker_later(input, &cycle, &states->items[reserved], now, input->snapshot_bound))
                {
                    verdict = waiting_window ? "the comprehensive run awaits its worker window" : "the snapshot's hosted qualification is in flight";
                }
                else if (ready_later)
                {
                    verdict = "ready states await their worker windows";
                }
                else if (preparing_later)
                {
                    verdict = "hosted qualifications are in flight";
                }
                else
                {
                    verdict = ready || preparing || waiting ? "no more hardware work fits before 08:00" : "no admissible work remains";
                    done = true;
                }
            }
        }
        else
        {
            verdict = before ? "prepared; hardware waits for the window" : "the window has closed";
            done = true;
        }
        for (uint32_t i = 0; i < input->ref_count; i += 1)
        {
            const int64_t index = nightly_state_index(states, input->refs[i]);
            const NightlyState* state = index >= 0 ? &states->items[index] : NULL;
            // A state this plan dispatched (a completed earlier state as the snapshot's substitute, say) keeps its ref.
            const bool idle = state == NULL || (!state->admit_in_flight && !state->defer_in_flight && !state->dispatched);
            const bool finished = state == NULL || index > cycle.snapshot || (nightly_terminal(state->status) && (index != cycle.snapshot || snapshot->full_this_cycle || cycle.snapshot_attempted));
            if (idle && finished)
            {
                fprintf(output, "%s\t%s\n", input->run ? "prune" : "would-prune", input->refs[i]);
            }
        }
        nightly_report(input, &cycle, output, report);
        fprintf(output, "%s\t%s\n", !input->run || done ? "done" : "wait", input->run ? verdict : "dry run");
    }
}

// --- The self-test ---

typedef struct NightlyTest NightlyTest;
struct NightlyTest
{
    uint32_t checks;
    uint32_t failures;
};

static void nightly_expect(NightlyTest* test, bool condition, const char* what)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 40)
        {
            fprintf(stderr, "nightly --self-test: failed: %s\n", what);
        }
    }
}

// A distinct object name per number.
static void nightly_test_sha(uint32_t number, char* sha)
{
    snprintf(sha, 41, "%08x%08x%08x%08x%08x", number, number * 2654435761u, ~number, number ^ 0x5bd1e995u, number + 0x9e3779b9u);
}

static NightlyText nightly_test_text(const char* text)
{
    NightlyText result = {
        .data = (char*)text,
        .length = strlen(text),
    };
    return result;
}

static int64_t nightly_test_madrid(int year, int month, int day, int hour, int minute)
{
    int64_t instant = 0;
    if (!nightly_madrid_instant(year, month, day, hour, &instant))
    {
        nightly_fatal("self-test: no Europe/Madrid time", NULL);
    }
    return instant + minute * 60;
}

static void nightly_test_window(NightlyTest* test)
{
    uint32_t worker_start = 0;
    uint32_t worker_end = 0;
    nightly_expect(test, nightly_worker_source(nightly_test_text("#define BUSTER_NIGHTLY_WINDOW_START_HOUR 4\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR 7\n"), &worker_start, &worker_end) && worker_start == 4 && worker_end == 7, "an old worker's literal window is 04:00-07:00");
    nightly_expect(test, nightly_worker_source(nightly_test_text("// project source\n#define BUSTER_NIGHTLY_WINDOW_START_HOUR 2\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR 8\n"), &worker_start, &worker_end) && worker_start == 2 && worker_end == 8, "a new worker's literal window is 02:00-08:00");
    static const char* const unknown_workers[] = {
        "",
        "#define BUSTER_NIGHTLY_WINDOW_START_HOUR 4\n",
        "#define BUSTER_NIGHTLY_WINDOW_START_HOUR 4\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR 7",
        "#define BUSTER_NIGHTLY_WINDOW_START_HOUR 4\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR 7\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR 8\n",
        "#define BUSTER_NIGHTLY_WINDOW_START_HOUR 4\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR\t7\n",
        "#define BUSTER_NIGHTLY_WINDOW_START_HOUR (4)\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR 7\n",
        "#define BUSTER_NIGHTLY_WINDOW_START_HOUR 3\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR 8\n",
        "#define BUSTER_NIGHTLY_WINDOW_START_HOUR 2\n#define BUSTER_NIGHTLY_WINDOW_END_HOUR 8 // changed\n",
    };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(unknown_workers); i += 1)
    {
        nightly_expect(test, !nightly_worker_source(nightly_test_text(unknown_workers[i]), &worker_start, &worker_end) && worker_start == 0 && worker_end == 0, "an unknown worker declaration never inherits the coordinator's hours");
    }
    // The tz database against the EU rule, every hour of five years: a disagreement means one of them changed.
    const int64_t first = 1767225600;
    uint32_t disagreements = 0;
    for (int64_t t = first; t < first + 5 * 366 * 86400; t += 3600)
    {
        NightlyLocal local;
        disagreements += !nightly_madrid(t, &local) || local.offset != nightly_eu_offset(t);
        disagreements += nightly_madrid(t + 1799, &local) ? local.offset != nightly_eu_offset(t + 1799) : 1;
    }
    nightly_expect(test, disagreements == 0, "Europe/Madrid follows the EU rule from 2026 to 2030");
    // Summer: 02:00 CEST is 00:00 UTC; winter: 02:00 CET is 01:00 UTC.
    const int64_t summer = 1784073600;
    nightly_expect(test, nightly_window(summer, 0).open, "02:00 CEST (00:00 UTC, 2026-07-15) is inside");
    nightly_expect(test, !nightly_window(summer - 60, 0).open, "01:59 CEST is before the window");
    nightly_expect(test, nightly_window(summer + 6 * 3600 - 1, 0).open, "07:59:59 CEST is inside with no bound");
    nightly_expect(test, nightly_window(summer + 6 * 3600 - 1, 1).open && !nightly_window(summer + 6 * 3600 - 1, 2).open, "07:59:59 CEST has one second left");
    nightly_expect(test, !nightly_window(summer + 6 * 3600, 0).open, "08:00 CEST is after the window");
    const int64_t winter = 1768438800;
    nightly_expect(test, nightly_window(winter, 0).open, "02:00 CET (01:00 UTC, 2026-01-15) is inside");
    nightly_expect(test, !nightly_window(winter - 3600, 0).open, "00:00 UTC in winter is 01:00 CET, before the window");
    // The nights the clocks change at 01:00 UTC, at 02:00 local time: when they go forward (2026-03-29), 02:00 doesn't
    // exist and the window opens at 03:00 CEST; when they go back (2026-10-25), it opens at the first of the two
    // 02:00s, 02:00 CEST, and the second, 02:00 CET, is inside. Six hours, five and seven.
    nightly_expect(test, nightly_window(1774746000, 0).open && !nightly_window(1774746000 - 60, 0).open, "2026-03-29: the window opens at 03:00 CEST, 01:00 UTC; 01:59 CET is before it");
    nightly_expect(test, nightly_window(1774746000, 0).start == 1774746000 && nightly_window(1774746000, 0).deadline == nightly_test_madrid(2026, 3, 29, 8, 0) && nightly_window(1774746000, 0).deadline - 1774746000 == 5 * 3600, "2026-03-29: five hours, to 08:00 CEST");
    nightly_expect(test, nightly_window(1792886400, 0).open && !nightly_window(1792886400 - 60, 0).open, "2026-10-25: the window opens at the first 02:00, 02:00 CEST, 00:00 UTC; 01:59 CEST is before it");
    nightly_expect(test, nightly_window(1792886400 + 3600, 0).open && nightly_window(1792886400, 0).deadline - 1792886400 == 7 * 3600, "2026-10-25: the second 02:00, 02:00 CET, is inside; seven hours, to 08:00 CET");
    // A run whose bound doesn't fit is refused: a late or delayed start, and a lock wait that crossed the deadline.
    const int64_t morning = nightly_test_madrid(2026, 11, 4, 7, 30);
    nightly_expect(test, nightly_window(morning, 25 * 60).open, "07:30 with 25 minutes ends by 08:00");
    nightly_expect(test, !nightly_window(morning + 10 * 60, 25 * 60).open, "07:40 with 25 minutes doesn't");
    nightly_expect(test, !nightly_window(nightly_test_madrid(2026, 11, 4, 9, 23), 0).open, "a scheduled run delayed to 09:23 is refused");
    // The cycle: from noon, the next night's; before noon, this morning's.
    NightlyWindow evening = nightly_window(nightly_test_madrid(2026, 11, 3, 22, 7), 0);
    nightly_expect(test, evening.valid && strcmp(evening.cycle, "2026-11-04") == 0 && evening.start == nightly_test_madrid(2026, 11, 4, 2, 0), "22:07 prepares for the next morning");
    NightlyWindow after = nightly_window(nightly_test_madrid(2026, 11, 4, 8, 17), 0);
    nightly_expect(test, after.valid && strcmp(after.cycle, "2026-11-04") == 0 && !after.open && after.deadline < nightly_test_madrid(2026, 11, 4, 8, 17), "08:17 reports the morning's cycle");
    NightlyWindow year_end = nightly_window(nightly_test_madrid(2026, 12, 31, 23, 0), 0);
    nightly_expect(test, year_end.valid && strcmp(year_end.cycle, "2027-01-01") == 0, "the cycle rolls over the year");
    // The pin is the evening before at 22:00 local time, whatever the clocks do that night: 4 hours before the window
    // every night, since the window opens after the clocks change (03:00 CEST, or the first 02:00).
    nightly_expect(test, evening.pin == nightly_test_madrid(2026, 11, 3, 22, 0) && evening.start - evening.pin == 4 * 3600, "an ordinary night pins from 22:00, 4 hours ahead");
    NightlyWindow spring = nightly_window(nightly_test_madrid(2027, 3, 27, 21, 30), 0);
    nightly_expect(test, spring.valid && strcmp(spring.cycle, "2027-03-28") == 0 && spring.pin == nightly_test_madrid(2027, 3, 27, 22, 0) && spring.start - spring.pin == 4 * 3600, "2027-03-27 pins from 22:00 CET, 4 hours before 03:00 CEST");
    NightlyWindow autumn = nightly_window(nightly_test_madrid(2026, 10, 24, 22, 7), 0);
    nightly_expect(test, autumn.valid && strcmp(autumn.cycle, "2026-10-25") == 0 && autumn.pin == nightly_test_madrid(2026, 10, 24, 22, 0) && autumn.start - autumn.pin == 4 * 3600, "2026-10-24 pins from 22:00 CEST, 4 hours before the first 02:00");
    NightlyWindow month_start = nightly_window(nightly_test_madrid(2026, 11, 30, 23, 0), 0);
    nightly_expect(test, month_start.valid && strcmp(month_start.cycle, "2026-12-01") == 0 && month_start.pin == nightly_test_madrid(2026, 11, 30, 22, 0), "a cycle on the first of a month pins from the last day's 22:00");
}

typedef struct NightlyBuffer NightlyBuffer;
struct NightlyBuffer
{
    char data[1 << 17];
    size_t length;
};

static void nightly_append(NightlyBuffer* buffer, const char* format, ...) __attribute__((format(printf, 2, 3)));

static void nightly_append(NightlyBuffer* buffer, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(buffer->data + buffer->length, sizeof(buffer->data) - buffer->length, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= sizeof(buffer->data) - buffer->length)
    {
        nightly_fatal("self-test buffer overflow", NULL);
    }
    buffer->length += (size_t)written;
}

static NightlyText nightly_buffer_text(NightlyBuffer* buffer)
{
    NightlyText text = {
        .data = buffer->data,
        .length = buffer->length,
    };
    return text;
}

// A run's plain rows, `shards` shards of each architecture, all succeeding but where `odd` names a job (architecture
// times 64 plus shard, from 1) to conclude otherwise; and one failing sanitizer row and the vector row's jobs, which
// never count.
static void nightly_test_plain(NightlyBuffer* buffer, uint64_t run, uint32_t shards, uint32_t odd, const char* conclusion, bool odd_missing)
{
    static const char* const architectures[] = { "x86-64", "native" };
    for (uint32_t a = 0; a < 2; a += 1)
    {
        for (uint32_t k = 1; k <= shards; k += 1)
        {
            const bool is_odd = odd == a * 64 + k;
            if (is_odd && odd_missing)
            {
                continue;
            }
            const char* status = is_odd && strcmp(conclusion, "-") == 0 ? "in_progress" : "completed";
            nightly_append(buffer, "job\t%llu\t%s\t%s\tTests on Ubuntu 26.04 (Clang 23, %s, none, O3, shard %u of %u)\n", (unsigned long long)run, status, is_odd ? conclusion : "success", architectures[a], k, shards);
        }
        nightly_append(buffer, "job\t%llu\tcompleted\tfailure\tTests on Ubuntu 26.04 (Clang 23, %s, thread, O3, shard 1 of %u)\n", (unsigned long long)run, architectures[a], shards);
    }
    // The vector row's jobs (#2033), one pool of the plain and sanitizer configurations' items, which a full run has
    // beside its `native` rows: no plain row's shard, whatever they conclude.
    static const char vector[] = "Tests on Ubuntu 26.04 (vector, none address thread memory, O3) / ";
    nightly_append(buffer, "job\t%llu\tcompleted\tfailure\t%sVector draw 1, candidate 3 (Clang 23, none address thread memory, O3)\n", (unsigned long long)run, vector);
    nightly_append(buffer, "job\t%llu\tcompleted\tsuccess\t%sVector draw 1, candidate 4 (Clang 23, none address thread memory, O3)\n", (unsigned long long)run, vector);
    nightly_append(buffer, "job\t%llu\tcompleted\tfailure\t%sVector row verdict (Clang 23, none address thread memory, O3)\n", (unsigned long long)run, vector);
}

static void nightly_test_run(NightlyBuffer* buffer, uint64_t id, const char* event, const char* status, const char* conclusion, const char* head, int64_t created, int64_t updated, const char* title)
{
    nightly_append(buffer, "run\t%llu\t1\t%s\t%s\t%s\t%s\t%s\tmain\t%lld\t%lld\t%u\t%s\n", (unsigned long long)id, event, status, conclusion, head, BUSTER_NIGHTLY_WORKFLOW, (long long)created, (long long)updated, BUSTER_NIGHTLY_REPOSITORY, title);
}

static NightlyPlain nightly_test_verdict(uint32_t shards, uint32_t odd, const char* conclusion, bool missing)
{
    NightlyBuffer* buffer = nightly_allocate(1, sizeof(NightlyBuffer));
    char sha[41];
    nightly_test_sha(1, sha);
    nightly_test_run(buffer, 7, "push", "completed", "failure", sha, 1, 2, "push");
    nightly_test_plain(buffer, 7, shards, odd, conclusion, missing);
    NightlyFacts facts = nightly_facts_new();
    NightlyPlain verdict = nightly_facts_parse(&facts, nightly_buffer_text(buffer)) ? nightly_plain_verdict(&facts, 7) : NIGHTLY_PLAIN_COUNT;
    nightly_facts_free(&facts);
    free(buffer);
    return verdict;
}

static void nightly_test_facts(NightlyTest* test)
{
    nightly_expect(test, nightly_test_verdict(4, 0, "success", false) == NIGHTLY_PLAIN_OK, "every plain shard succeeded: ok, whatever the sanitizer rows did");
    nightly_expect(test, nightly_test_verdict(1, 0, "success", false) == NIGHTLY_PLAIN_OK, "one shard each");
    nightly_expect(test, nightly_test_verdict(4, 3, "success", true) == NIGHTLY_PLAIN_NONE, "a missing x86-64 shard: none");
    nightly_expect(test, nightly_test_verdict(4, 64 + 4, "success", true) == NIGHTLY_PLAIN_NONE, "a missing native shard: none");
    nightly_expect(test, nightly_test_verdict(4, 2, "failure", false) == NIGHTLY_PLAIN_FAILED, "a failed shard: failed");
    nightly_expect(test, nightly_test_verdict(4, 66, "timed_out", false) == NIGHTLY_PLAIN_FAILED, "a timed-out shard: failed");
    nightly_expect(test, nightly_test_verdict(4, 2, "cancelled", false) == NIGHTLY_PLAIN_NONE, "a cancelled shard: none, not a failure");
    nightly_expect(test, nightly_test_verdict(4, 2, "skipped", false) == NIGHTLY_PLAIN_NONE, "a skipped shard: none");
    nightly_expect(test, nightly_test_verdict(4, 2, "-", false) == NIGHTLY_PLAIN_NONE, "a shard in progress: none");

    char sha[41];
    char other[41];
    nightly_test_sha(1, sha);
    nightly_test_sha(2, other);
    NightlyBuffer* buffer = nightly_allocate(1, sizeof(NightlyBuffer));
    // Inconsistent shard counts and an unsharded name.
    nightly_test_run(buffer, 8, "push", "completed", "success", sha, 1, 2, "x");
    nightly_append(buffer, "job\t8\tcompleted\tsuccess\tTests on Ubuntu 26.04 (Clang 23, x86-64, none, O3, shard 1 of 2)\n");
    nightly_append(buffer, "job\t8\tcompleted\tsuccess\tTests on Ubuntu 26.04 (Clang 23, x86-64, none, O3, shard 2 of 3)\n");
    nightly_append(buffer, "job\t8\tcompleted\tsuccess\tTests on Ubuntu 26.04 (Clang 23, native, none, O3, shard 1 of 2)\n");
    nightly_append(buffer, "job\t8\tcompleted\tsuccess\tTests on Ubuntu 26.04 (Clang 23, native, none, O3, shard 2 of 2)\n");
    nightly_test_run(buffer, 9, "push", "completed", "success", other, 1, 2, "x");
    nightly_append(buffer, "job\t9\tcompleted\tsuccess\tTests on Ubuntu 26.04 (Clang 23, x86-64, none, O3)\n");
    nightly_append(buffer, "job\t9\tcompleted\tsuccess\tTests on Ubuntu 26.04 (Clang 23, native, none, O3)\n");
    NightlyFacts facts = nightly_facts_new();
    nightly_expect(test, nightly_facts_parse(&facts, nightly_buffer_text(buffer)), "facts parse");
    nightly_expect(test, nightly_plain_verdict(&facts, 8) == NIGHTLY_PLAIN_NONE, "shards that disagree on N: none");
    nightly_expect(test, nightly_plain_verdict(&facts, 9) == NIGHTLY_PLAIN_OK, "unsharded plain rows: ok");
    nightly_facts_free(&facts);

    // Each row is a called workflow (hosted-correctness.yml, #2032): its jobs' names follow the caller's (the test job's
    // name, or its id with the matrix's values), and the job that builds a portable row's compiler once is no shard.
    // Its failure fails the row through its shards, which fail without the product.
    for (uint32_t variant = 0; variant < 4; variant += 1)
    {
        const bool named = variant < 2;
        const bool built = variant % 2 != 0;
        buffer->length = 0;
        nightly_test_run(buffer, 16, "push", "completed", built ? "success" : "failure", sha, 1, 2, "x");
        for (uint32_t a = 0; a < 2; a += 1)
        {
            const char* architecture = a == 0 ? "x86-64" : "native";
            const char* build = a == 0 ? (built ? "success" : "failure") : "skipped";
            const char* caller = named ? "Tests on Ubuntu 26.04" : "test";
            nightly_append(buffer, "job\t16\tcompleted\t%s\t%s (%s, none, 3) / Build the compiler once for the row's shards\n", build, caller, architecture);
            for (uint32_t k = 1; k <= 4; k += 1)
            {
                const char* shard = a == 0 && !built ? "failure" : "success";
                nightly_append(buffer, "job\t16\tcompleted\t%s\t%s (%s, none, 3) / Tests on Ubuntu 26.04 (Clang 23, %s, none, O3, shard %u of 4)\n", shard, caller, architecture, architecture, k);
            }
        }
        facts = nightly_facts_new();
        nightly_expect(test, nightly_facts_parse(&facts, nightly_buffer_text(buffer)), "a called row's facts parse");
        const NightlyPlain verdict = nightly_plain_verdict(&facts, 16);
        nightly_expect(test, built ? verdict == NIGHTLY_PLAIN_OK : verdict == NIGHTLY_PLAIN_FAILED, built ? "a called row's shards: ok, its build no shard" : "a called row whose build failed: failed, by its shards");
        nightly_facts_free(&facts);
    }

    // Invalid facts are refused whole.
    static const char* const invalid[] = {
        "run\t1\t1\tpush\tcompleted\tsuccess\tnot-a-sha\t.github/workflows/9700x.yml\tmain\t1\t2\t1421805693\tx\n",
        "run\t1\t1\tpush\tdone\tsuccess\t0000000000000000000000000000000000000000\t.github/workflows/9700x.yml\tmain\t1\t2\t1421805693\tx\n",
        "run\t1\t1\tpush\tcompleted\tsuccess\t0000000000000000000000000000000000000000\t.github/workflows/9700x.yml\tmain\t1\t2\t1421805693\n",
        "run\t0\t1\tpush\tcompleted\tsuccess\t0000000000000000000000000000000000000000\t.github/workflows/9700x.yml\tmain\t1\t2\t1421805693\tx\n",
        "run\t1\t1\tpush\tcompleted\tsuccess\t0000000000000000000000000000000000000000\t.github/workflows/9700x.yml\tmain\t1\t2\t1421805693\tx\nrun\t1\t1\tpush\tcompleted\tsuccess\t0000000000000000000000000000000000000000\t.github/workflows/9700x.yml\tmain\t1\t2\t1421805693\tx\n",
        "job\t1\tcompleted\tsuccess\tA\njob\t1\tcompleted\tfailure\tA\n",
        "job\t1\tcompleted\tsuccess\t\n",
        "pr\t5\t2\tmain\t-\t0000000000000000000000000000000000000000\t1421805693\n",
        "bogus\t1\n",
        "job\t1\tcompleted\tsuccess\tA",
    };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(invalid); i += 1)
    {
        NightlyFacts refused = nightly_facts_new();
        nightly_expect(test, !nightly_facts_parse(&refused, nightly_test_text(invalid[i])), "invalid facts are refused");
        nightly_facts_free(&refused);
    }

    // The newest conclusive push or dispatch run of the commit decides; pull requests' and other workflows' don't.
    buffer->length = 0;
    nightly_test_run(buffer, 10, "push", "completed", "failure", sha, 1, 2, "x");
    nightly_test_plain(buffer, 10, 4, 0, "success", false);
    nightly_test_run(buffer, 11, "workflow_dispatch", "completed", "failure", sha, 3, 4, "x");
    nightly_test_plain(buffer, 11, 4, 1, "cancelled", false);
    nightly_test_run(buffer, 12, "pull_request", "completed", "failure", sha, 5, 6, "x");
    nightly_test_plain(buffer, 12, 4, 1, "failure", false);
    nightly_append(buffer, "run\t13\t1\tpush\tcompleted\tfailure\t%s\t.github/workflows/other.yml\tmain\t7\t8\t%u\tx\n", sha, BUSTER_NIGHTLY_REPOSITORY);
    nightly_test_plain(buffer, 13, 4, 1, "failure", false);
    nightly_test_run(buffer, 14, "workflow_dispatch", "in_progress", "-", sha, 9, 10, "x");
    nightly_test_plain(buffer, 14, 4, 1, "failure", false);
    nightly_test_run(buffer, 15, "push", "completed", "failure", other, 9, 10, "x");
    nightly_test_plain(buffer, 15, 4, 1, "failure", false);
    facts = nightly_facts_new();
    nightly_expect(test, nightly_facts_parse(&facts, nightly_buffer_text(buffer)), "hosted facts parse");
    NightlyHosted hosted = nightly_hosted(&facts, sha, 0);
    nightly_expect(test, hosted.verdict == NIGHTLY_PLAIN_OK && hosted.run == 10, "the newest conclusive push or dispatch run decides");
    hosted = nightly_hosted(&facts, sha, 10);
    nightly_expect(test, hosted.verdict == NIGHTLY_PLAIN_NONE, "the asking run is excluded; a cancelled one is inconclusive");
    hosted = nightly_hosted(&facts, other, 0);
    nightly_expect(test, hosted.verdict == NIGHTLY_PLAIN_FAILED && hosted.run == 15, "a genuine failure is reported");
    nightly_facts_free(&facts);
    buffer->length = 0;
    nightly_test_run(buffer, 20, "push", "completed", "success", sha, 1, 2, "x");
    nightly_test_plain(buffer, 20, 4, 0, "success", false);
    nightly_test_run(buffer, 21, "push", "completed", "failure", sha, 3, 4, "x");
    nightly_test_plain(buffer, 21, 4, 65, "failure", false);
    facts = nightly_facts_new();
    nightly_facts_parse(&facts, nightly_buffer_text(buffer));
    hosted = nightly_hosted(&facts, sha, 0);
    nightly_expect(test, hosted.verdict == NIGHTLY_PLAIN_FAILED && hosted.run == 21, "a later failure isn't hidden by an earlier pass");
    nightly_facts_free(&facts);
    free(buffer);

    NightlyRun run = { 0 };
    nightly_expect(test, nightly_title("Nightly backlog (admit) of 0123456789abcdef0123456789abcdef01234567 [20261104.9.1.admit]", &run) && run.admit && run.purpose == NIGHTLY_PURPOSE_BACKLOG, "a nightly run name");
    nightly_expect(test, !nightly_title("Nightly backlog  (admit) of 0123456789abcdef0123456789abcdef01234567 [x]", &(NightlyRun){ 0 }), "a run name must be canonical");
    nightly_expect(test, !nightly_title("Nightly full (admit) of 0123456789abcdef0123456789abcdef01234567 [x]", &(NightlyRun){ 0 }), "full runs aren't nightly ones");
    nightly_expect(test, !nightly_title("Nightly backlog (admit) of 0123456789abcdef0123456789abcdef01234567 [x] ", &(NightlyRun){ 0 }), "nothing after the request");
}

// A valid record: a pull request's targeted run that completed, which mutations then vary.
static void nightly_test_record(NightlyRecord* record, uint64_t run, const char* event, const char* purpose, const char* commit, const char* tree, const char* parent_tree, const char* head)
{
    char run_text[24];
    snprintf(run_text, sizeof(run_text), "%llu", (unsigned long long)run);
    char other[41];
    nightly_test_sha(99, other);
    const char* values[NIGHTLY_FIELD_COUNT] = {
        [NIGHTLY_FIELD_FORMAT] = BUSTER_NIGHTLY_RECORD_FORMAT,
        [NIGHTLY_FIELD_REPOSITORY] = "1421805693",
        [NIGHTLY_FIELD_RUN] = run_text,
        [NIGHTLY_FIELD_ATTEMPT] = "1",
        [NIGHTLY_FIELD_EVENT] = event,
        [NIGHTLY_FIELD_PURPOSE] = purpose,
        [NIGHTLY_FIELD_REQUEST] = "-",
        [NIGHTLY_FIELD_COMMIT] = commit,
        [NIGHTLY_FIELD_TREE] = tree,
        [NIGHTLY_FIELD_PARENT] = other,
        [NIGHTLY_FIELD_PARENT_TREE] = parent_tree,
        [NIGHTLY_FIELD_PR_HEAD] = head,
        [NIGHTLY_FIELD_SNAPSHOT] = "-",
        [NIGHTLY_FIELD_WORKFLOW] = other,
        [NIGHTLY_FIELD_SELECTOR] = other,
        [NIGHTLY_FIELD_INVENTORY] = other,
        [NIGHTLY_FIELD_TOOLCHAIN] = other,
        [NIGHTLY_FIELD_CORPUS] = other,
        [NIGHTLY_FIELD_MODE] = "off",
        [NIGHTLY_FIELD_CODE] = "true",
        [NIGHTLY_FIELD_MEASURE] = ",lexer,types,",
        [NIGHTLY_FIELD_GATES] = "checker,zen5",
        [NIGHTLY_FIELD_CHANGES_RESULT] = "success",
        [NIGHTLY_FIELD_EVIDENCE_RESULT] = "success",
        [NIGHTLY_FIELD_SCOPE] = "plain",
        [NIGHTLY_FIELD_HOSTED] = "run",
        [NIGHTLY_FIELD_HOSTED_SOURCE] = "-",
        [NIGHTLY_FIELD_NATIVE_EVIDENCE] = "run",
        [NIGHTLY_FIELD_NATIVE_SOURCE] = "-",
        [NIGHTLY_FIELD_MEASUREMENT_EVIDENCE] = "run",
        [NIGHTLY_FIELD_MEASUREMENT_SOURCE] = "-",
        [NIGHTLY_FIELD_HARDWARE] = "admit",
        [NIGHTLY_FIELD_HARDWARE_REASON] = "admitted",
        [NIGHTLY_FIELD_TEST_RESULT] = "success",
        [NIGHTLY_FIELD_FUZZ_RESULT] = "skipped",
        [NIGHTLY_FIELD_BENCHMARK_RESULT] = "success",
        [NIGHTLY_FIELD_WINDOW] = "-",
        [NIGHTLY_FIELD_NATIVE] = "passed",
        [NIGHTLY_FIELD_HOST] = "9700X/6.17.1-arch1/0xb404023/gcc-15.2.1/glibc_2.42/sw-0123456789abcdef",
        [NIGHTLY_FIELD_QUALIFICATION_RESULT] = "success",
        [NIGHTLY_FIELD_OVERRIDE] = "-",
        [NIGHTLY_FIELD_COVERAGE] = "targeted",
        [NIGHTLY_FIELD_OUTCOME] = "complete",
    };
    for (uint32_t i = 0; i < NIGHTLY_FIELD_COUNT; i += 1)
    {
        nightly_copy(record->values[i], BUSTER_NIGHTLY_VALUE, values[i]);
    }
}

static bool nightly_test_reparse(const NightlyRecord* record, NightlyRecord* parsed)
{
    char* data = NULL;
    size_t size = 0;
    FILE* stream = open_memstream(&data, &size);
    nightly_record_write(stream, record);
    fclose(stream);
    NightlyText text = {
        .data = data,
        .length = size,
    };
    const bool valid = nightly_record_parse(text, parsed);
    free(data);
    return valid;
}

static void nightly_test_records(NightlyTest* test)
{
    NightlyRecord* record = nightly_allocate(1, sizeof(NightlyRecord));
    NightlyRecord* parsed = nightly_allocate(1, sizeof(NightlyRecord));
    char a[41];
    char b[41];
    char c[41];
    nightly_test_sha(1, a);
    nightly_test_sha(2, b);
    nightly_test_sha(3, c);
    nightly_test_record(record, 5, "pull_request", "pr", a, b, c, a);
    nightly_expect(test, nightly_test_reparse(record, parsed) && memcmp(record, parsed, sizeof(*record)) == 0, "a record round-trips");
    // Each key with a value of another kind is refused.
    static const char* const bad[] = { "x", "0", "-", "zz", "all,", ",,", "a b", "\x01" };
    uint32_t refused = 0;
    uint32_t tried = 0;
    for (uint32_t i = 0; i < NIGHTLY_FIELD_COUNT; i += 1)
    {
        for (uint32_t j = 0; j < BUSTER_NIGHTLY_COUNT(bad); j += 1)
        {
            if (!nightly_valid_value(&nightly_record_keys[i], bad[j]))
            {
                NightlyRecord* mutant = nightly_allocate(1, sizeof(NightlyRecord));
                *mutant = *record;
                nightly_copy(mutant->values[i], BUSTER_NIGHTLY_VALUE, bad[j]);
                tried += 1;
                refused += !nightly_test_reparse(mutant, parsed);
                free(mutant);
            }
        }
    }
    nightly_expect(test, tried > NIGHTLY_FIELD_COUNT * 4 && refused == tried, "a record with an invalid value is refused");
    const char* truncated = "format\tbuster-coverage-record-1\nrepository\t1421805693\n";
    nightly_expect(test, !nightly_record_parse(nightly_test_text(truncated), parsed), "a truncated record is refused");
    char* data = NULL;
    size_t size = 0;
    FILE* stream = open_memstream(&data, &size);
    nightly_record_write(stream, record);
    fputs("extra\tvalue\n", stream);
    fclose(stream);
    nightly_expect(test, !nightly_record_parse((NightlyText){ .data = data, .length = size }, parsed), "a record with an extra key is refused");
    free(data);
    data = NULL;
    stream = open_memstream(&data, &size);
    nightly_record_write(stream, record);
    fclose(stream);
    data[size - 1] = 0;
    nightly_expect(test, !nightly_record_parse((NightlyText){ .data = data, .length = size - 1 }, parsed), "a record without its last newline is refused");
    // The newline back, the record is whole again and valid: then one key misnamed is the only fault.
    data[size - 1] = '\n';
    nightly_expect(test, nightly_record_parse((NightlyText){ .data = data, .length = size }, parsed), "the record with its last newline back is accepted");
    char* swap = strstr(data, "\nevent\t");
    if (swap != NULL)
    {
        memcpy(swap + 1, "evenx", 5);
    }
    nightly_expect(test, swap != NULL && !nightly_record_parse((NightlyText){ .data = data, .length = size }, parsed), "a record with a misnamed key is refused");
    free(data);

    // The outcome over every combination of the results it reads: completion, blocks and deferrals only as allowed.
    static const char* const purposes[] = { "pr", "push", "schedule", "full", "backlog", "snapshot" };
    static const char* const results[] = { "success", "failure", "cancelled", "skipped", "-" };
    static const char* const hosted[] = { "run", "reused", "-" };
    static const char* const windows[] = { "open", "closed", "-" };
    static const char* const natives[] = { "started", "passed", "-" };
    static const char* const reasons[] = { "admitted", "native-reused" };
    uint32_t violations = 0;
    uint32_t combinations = 0;
    for (uint32_t p = 0; p < BUSTER_NIGHTLY_COUNT(purposes); p += 1)
    {
        for (uint32_t encoded = 0; encoded < 5 * 5 * 5 * 3 * 2 * 3 * 3 * 2 * 2 * 2 * 2 * 2; encoded += 1)
        {
            uint32_t r = encoded;
            const char* test_result = results[r % 5];
            r /= 5;
            const char* fuzz_result = results[r % 5];
            r /= 5;
            const char* benchmark_result = results[r % 5];
            r /= 5;
            const char* hosted_value = hosted[r % 3];
            r /= 3;
            const bool admit = r % 2 == 0;
            r /= 2;
            const char* window = windows[r % 3];
            r /= 3;
            const char* native = natives[r % 3];
            r /= 3;
            const bool full_scope = r % 2 == 0;
            r /= 2;
            const bool evidence = r % 2 == 0;
            r /= 2;
            const bool code = r % 2 == 0;
            r /= 2;
            const char* reason = reasons[r % 2];
            r /= 2;
            const bool measurement = r % 2 == 0;
            nightly_test_record(record, 5, "push", purposes[p], a, b, c, "-");
            nightly_copy(record->values[NIGHTLY_FIELD_TEST_RESULT], BUSTER_NIGHTLY_VALUE, test_result);
            nightly_copy(record->values[NIGHTLY_FIELD_FUZZ_RESULT], BUSTER_NIGHTLY_VALUE, fuzz_result);
            nightly_copy(record->values[NIGHTLY_FIELD_BENCHMARK_RESULT], BUSTER_NIGHTLY_VALUE, benchmark_result);
            nightly_copy(record->values[NIGHTLY_FIELD_HOSTED], BUSTER_NIGHTLY_VALUE, hosted_value);
            nightly_copy(record->values[NIGHTLY_FIELD_HARDWARE], BUSTER_NIGHTLY_VALUE, admit ? "admit" : "skip");
            nightly_copy(record->values[NIGHTLY_FIELD_WINDOW], BUSTER_NIGHTLY_VALUE, window);
            nightly_copy(record->values[NIGHTLY_FIELD_NATIVE], BUSTER_NIGHTLY_VALUE, native);
            nightly_copy(record->values[NIGHTLY_FIELD_SCOPE], BUSTER_NIGHTLY_VALUE, full_scope ? "full" : "plain");
            nightly_copy(record->values[NIGHTLY_FIELD_EVIDENCE_RESULT], BUSTER_NIGHTLY_VALUE, evidence ? "success" : "failure");
            nightly_copy(record->values[NIGHTLY_FIELD_CODE], BUSTER_NIGHTLY_VALUE, code ? "true" : "false");
            nightly_copy(record->values[NIGHTLY_FIELD_HARDWARE_REASON], BUSTER_NIGHTLY_VALUE, reason);
            nightly_copy(record->values[NIGHTLY_FIELD_MEASUREMENT_EVIDENCE], BUSTER_NIGHTLY_VALUE, measurement ? "reused" : "run");
            nightly_copy(record->values[NIGHTLY_FIELD_MEASUREMENT_SOURCE], BUSTER_NIGHTLY_VALUE, measurement ? "77" : "-");
            const NightlyOutcome outcome = nightly_record_outcome(record);
            const bool hosted_ran = strcmp(hosted_value, "run") == 0;
            const bool hosted_failed = hosted_ran && (strcmp(test_result, "failure") == 0 || (full_scope && strcmp(fuzz_result, "failure") == 0));
            const bool push = strcmp(purposes[p], "push") == 0;
            const bool reused = push && !admit && strcmp(reason, "native-reused") == 0 && measurement;
            bool allowed = true;
            allowed &= outcome != NIGHTLY_OUTCOME_COMPLETE || (evidence && code && !hosted_failed && ((admit && strcmp(benchmark_result, "success") == 0) || reused));
            allowed &= outcome != NIGHTLY_OUTCOME_BLOCKED_HOSTED || hosted_failed;
            allowed &= outcome != NIGHTLY_OUTCOME_BLOCKED_NATIVE || (admit && strcmp(benchmark_result, "failure") == 0 && strcmp(native, "started") == 0 && strcmp(window, "closed") != 0);
            allowed &= (outcome == NIGHTLY_OUTCOME_NOT_REQUIRED) == !code;
            allowed &= outcome != NIGHTLY_OUTCOME_DEFERRED || !admit || (strcmp(benchmark_result, "failure") == 0 && strcmp(window, "closed") == 0);
            allowed &= outcome != NIGHTLY_OUTCOME_PENDING || (push && !admit);
            allowed &= outcome != NIGHTLY_OUTCOME_REJECTED || !evidence;
            // A cancelled or skipped 9700X job is never a completion, nor a correctness failure.
            allowed &= !(admit && evidence && code && !hosted_failed && (strcmp(benchmark_result, "cancelled") == 0 || strcmp(benchmark_result, "skipped") == 0)) || outcome == NIGHTLY_OUTCOME_INTERRUPTED;
            violations += !allowed;
            combinations += 1;
        }
    }
    nightly_expect(test, combinations > 300000 && violations == 0, "the outcome completes, blocks and defers only where its results allow");

    // The environment's record: a backlog run whose window closed while its job waited for the lock.
    static const char* const names[][2] = {
        { "RECORD_REPOSITORY", "1421805693" },
        { "RECORD_RUN", "41" },
        { "RECORD_ATTEMPT", "1" },
        { "RECORD_EVENT", "workflow_dispatch" },
        { "RECORD_DISPATCH_PURPOSE", "backlog" },
        { "RECORD_REQUEST", "20261104.9.3.admit" },
        { "RECORD_MODE", "on" },
        { "RECORD_CODE", "true" },
        { "RECORD_MEASURE", ",lexer," },
        { "RECORD_CHANGES_RESULT", "success" },
        { "RECORD_EVIDENCE_RESULT", "success" },
        { "RECORD_SCOPE", "plain" },
        { "RECORD_HOSTED", "reused" },
        { "RECORD_HOSTED_SOURCE", "40" },
        { "RECORD_HARDWARE", "admit" },
        { "RECORD_HARDWARE_REASON", "admitted" },
        { "RECORD_TEST_RESULT", "skipped" },
        { "RECORD_FUZZ_RESULT", "skipped" },
        { "RECORD_BENCHMARK_RESULT", "failure" },
        { "RECORD_WINDOW", "closed" },
        { "RECORD_OVERRIDE", "manual run: check `this` [x]" },
    };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(names); i += 1)
    {
        setenv(names[i][0], names[i][1], 1);
    }
    static const char* const objects[] = { "RECORD_COMMIT", "RECORD_TREE", "RECORD_WORKFLOW", "RECORD_SELECTOR", "RECORD_INVENTORY", "RECORD_TOOLCHAIN", "RECORD_CORPUS" };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(objects); i += 1)
    {
        setenv(objects[i], a, 1);
    }
    nightly_record_from_environment(record);
    nightly_expect(test, nightly_is(record, NIGHTLY_FIELD_PURPOSE, "backlog") && nightly_is(record, NIGHTLY_FIELD_OUTCOME, "deferred") && nightly_is(record, NIGHTLY_FIELD_COVERAGE, "targeted"), "a closed window defers, not fails");
    nightly_expect(test, nightly_is(record, NIGHTLY_FIELD_OVERRIDE, "manual run_ check _this_ _x_") && nightly_is(record, NIGHTLY_FIELD_PARENT, "-"), "free text is sanitized; missing values are -");
    nightly_expect(test, nightly_test_reparse(record, parsed), "the environment's record parses");
    setenv("RECORD_WINDOW", "open", 1);
    setenv("RECORD_NATIVE", "started", 1);
    nightly_record_from_environment(record);
    nightly_expect(test, nightly_is(record, NIGHTLY_FIELD_OUTCOME, "blocked-native"), "native correctness failing on the 9700X blocks");
    setenv("RECORD_NATIVE", "passed", 1);
    nightly_record_from_environment(record);
    nightly_expect(test, nightly_is(record, NIGHTLY_FIELD_OUTCOME, "failed"), "a measurement failing after native correctness fails");
    setenv("RECORD_BENCHMARK_RESULT", "success", 1);
    nightly_record_from_environment(record);
    nightly_expect(test, nightly_is(record, NIGHTLY_FIELD_OUTCOME, "complete"), "a successful 9700X job completes");
    setenv("RECORD_EVENT", "push", 1);
    setenv("RECORD_MODE", "off", 1);
    nightly_record_from_environment(record);
    nightly_expect(test, nightly_is(record, NIGHTLY_FIELD_PURPOSE, "push") && nightly_is(record, NIGHTLY_FIELD_COVERAGE, "plain"), "a push before the nightly mode measures every plain run");
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(names); i += 1)
    {
        unsetenv(names[i][0]);
    }
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(objects); i += 1)
    {
        unsetenv(objects[i]);
    }
    unsetenv("RECORD_NATIVE");
    free(record);
    free(parsed);
}

static void nightly_test_ledger(NightlyTest* test)
{
    char a[41];
    char b[41];
    nightly_test_sha(1, a);
    nightly_test_sha(2, b);
    NightlyBuffer* buffer = nightly_allocate(1, sizeof(NightlyBuffer));
    nightly_append(buffer, "%s\n", BUSTER_NIGHTLY_LEDGER_FORMAT);
    nightly_append(buffer, "run\t7\t1\t%s\tpush\tpush\t-\t1\t2\tok\tadmit\tplain\tcomplete\t-\tAMD/1\t%s\n", a, b);
    nightly_append(buffer, "cycle\t2026-11-04\t%s\t9\t3\n", a);
    NightlyLedger ledger = nightly_ledger_new();
    nightly_expect(test, nightly_ledger_parse(&ledger, nightly_buffer_text(buffer)) && ledger.row_count == 1 && ledger.cycle_count == 1, "a ledger parses");
    char* data = NULL;
    size_t size = 0;
    FILE* stream = open_memstream(&data, &size);
    nightly_row_write(stream, &ledger.rows[0]);
    fclose(stream);
    nightly_expect(test, strstr(buffer->data, data) != NULL, "a row is written as it was read");
    free(data);
    nightly_ledger_free(&ledger);
    static const char* const invalid[] = {
        "run\t7\t1\tAAAA\tpush\tpush\t-\t1\t2\tok\tadmit\tplain\tcomplete\t-\t-\t-\n",
        "run\t7\t1\t0000000000000000000000000000000000000000\tpush\tpush\t-\t1\t2\tmaybe\tadmit\tplain\tcomplete\t-\t-\t-\n",
        "run\t7\t1\t0000000000000000000000000000000000000000\tpush\tpush\t-\t1\t2\tok\tadmit\tplain\tcomplete\t-\t-\t-\nrun\t7\t1\t0000000000000000000000000000000000000000\tpush\tpush\t-\t1\t2\tok\tadmit\tplain\tcomplete\t-\t-\t-\n",
        "cycle\t2026-13-04\t0000000000000000000000000000000000000000\t9\t3\n",
        "cycle\t2026-11-04\t0000000000000000000000000000000000000000\t9\t3\ncycle\t2026-11-04\t0000000000000000000000000000000000000000\t9\t3\n",
        "run\t7\t1\t0000000000000000000000000000000000000000\tpush\tpush\t-\t1\t2\tok\tadmit\tplain\tcomplete\t-\t-\n",
    };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(invalid); i += 1)
    {
        buffer->length = 0;
        nightly_append(buffer, "%s\n%s", BUSTER_NIGHTLY_LEDGER_FORMAT, invalid[i]);
        ledger = nightly_ledger_new();
        nightly_expect(test, !nightly_ledger_parse(&ledger, nightly_buffer_text(buffer)), "an invalid ledger line is refused");
        nightly_ledger_free(&ledger);
    }
    ledger = nightly_ledger_new();
    nightly_expect(test, !nightly_ledger_parse(&ledger, nightly_test_text("run\t1\n")) && nightly_ledger_parse(&ledger, nightly_test_text("")), "a ledger needs its header; an empty one is new");
    nightly_ledger_free(&ledger);

    // Harvest: the facts decide what a record may claim.
    buffer->length = 0;
    nightly_test_run(buffer, 30, "push", "completed", "success", a, 1, 2, "x");
    nightly_test_plain(buffer, 30, 4, 0, "success", false);
    nightly_append(buffer, "job\t30\tcompleted\tfailure\t%s\n", BUSTER_NIGHTLY_HARDWARE_JOB);
    nightly_test_run(buffer, 31, "push", "completed", "success", b, 1, 2, "x");
    nightly_test_plain(buffer, 31, 4, 0, "success", false);
    nightly_append(buffer, "job\t31\tcompleted\tsuccess\t%s\n", BUSTER_NIGHTLY_HARDWARE_JOB);
    NightlyFacts facts = nightly_facts_new();
    nightly_expect(test, nightly_facts_parse(&facts, nightly_buffer_text(buffer)), "harvest facts parse");
    NightlyRecord* record = nightly_allocate(1, sizeof(NightlyRecord));
    char tree[41];
    nightly_test_sha(3, tree);
    nightly_test_record(record, 30, "push", "push", a, tree, tree, "-");
    NightlyRow row = nightly_harvest_row(&facts, &facts.runs[0], record);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_INVALID, "a record can't claim a completion its failed 9700X job didn't make");
    nightly_test_record(record, 31, "push", "push", a, tree, tree, "-");
    row = nightly_harvest_row(&facts, &facts.runs[1], record);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_INVALID, "a record of another commit is invalid");
    nightly_test_record(record, 31, "push", "push", b, tree, tree, "-");
    row = nightly_harvest_row(&facts, &facts.runs[1], record);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_COMPLETE && row.coverage == NIGHTLY_COVERAGE_TARGETED && row.plain == NIGHTLY_PLAIN_OK, "an agreeing record is the row");
    nightly_copy(record->values[NIGHTLY_FIELD_EVENT], BUSTER_NIGHTLY_VALUE, "workflow_dispatch");
    row = nightly_harvest_row(&facts, &facts.runs[1], record);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_INVALID, "a record of another event is invalid");
    row = nightly_harvest_row(&facts, &facts.runs[1], NULL);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_COMPLETE && row.coverage == NIGHTLY_COVERAGE_PLAIN, "an older push without a record measured every plain run");
    row = nightly_harvest_row(&facts, &facts.runs[0], NULL);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_FAILED, "a failed 9700X job without a record failed");
    nightly_facts_free(&facts);
    // Older pushes without records whose 9700X job never ran: a sanitizer row's failure stopped it (their gate), or a
    // cancellation, or nothing did; their plain rows still qualify the state.
    buffer->length = 0;
    nightly_test_run(buffer, 40, "push", "completed", "failure", a, 1, 2, "x");
    nightly_test_plain(buffer, 40, 4, 0, "success", false);
    nightly_test_run(buffer, 41, "push", "completed", "cancelled", a, 1, 2, "x");
    nightly_test_run(buffer, 42, "push", "completed", "success", a, 1, 2, "x");
    facts = nightly_facts_new();
    nightly_expect(test, nightly_facts_parse(&facts, nightly_buffer_text(buffer)), "record-less facts parse");
    row = nightly_harvest_row(&facts, &facts.runs[0], NULL);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_BLOCKED_HOSTED && row.plain == NIGHTLY_PLAIN_OK, "a failed sanitizer row blocked that run, not the state's plain rows");
    row = nightly_harvest_row(&facts, &facts.runs[1], NULL);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_INTERRUPTED, "a cancelled run was interrupted");
    row = nightly_harvest_row(&facts, &facts.runs[2], NULL);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_DEFERRED, "a run whose 9700X job didn't run, with nothing failed, deferred");
    nightly_facts_free(&facts);
    free(record);
    free(buffer);
}

static void nightly_test_states(NightlyTest* test)
{
    char a[41];
    char b[41];
    char c[41];
    nightly_test_sha(1, a);
    nightly_test_sha(2, b);
    nightly_test_sha(3, c);
    NightlyBuffer* buffer = nightly_allocate(1, sizeof(NightlyBuffer));
    nightly_append(buffer, "state\t%s\t-\t1\nworker-window\t4\t7\npath\tsrc/buster/compiler/main.c\n", a);
    nightly_append(buffer, "state\t%s\t%s\t2\nworker-window\t2\t8\npath\tdocs/ci.md\npath\tREADME.md\npath\tdocs/performance/schema.json\n", b, a);
    nightly_append(buffer, "state\t%s\t%s\t3\n", c, b);
    NightlyStates states = {
        .items = nightly_allocate(BUSTER_NIGHTLY_STATES, sizeof(NightlyState)),
    };
    nightly_expect(test, nightly_states_parse(&states, nightly_buffer_text(buffer)) && states.count == 3, "states parse");
    nightly_expect(test, states.items[0].code && !states.items[1].code && !states.items[2].code, "documentation alone (as push's paths-ignore) and an empty change need no measurement");
    nightly_expect(test, states.items[0].worker_start_hour == 4 && states.items[0].worker_end_hour == 7 && states.items[1].worker_start_hour == 2 && states.items[1].worker_end_hour == 8 && !states.items[2].worker_window_recorded, "state windows remain distinct; an omitted window stays unknown");
    states.count = 0;
    nightly_expect(test, !nightly_states_parse(&states, nightly_test_text("path\tsrc/x.c\n")), "a path before any state is refused");
    states.count = 0;
    buffer->length = 0;
    nightly_append(buffer, "state\t%s\t-\t1\nstate\t%s\t-\t1\n", a, a);
    nightly_expect(test, !nightly_states_parse(&states, nightly_buffer_text(buffer)), "a repeated state is refused");
    static const char* const invalid_windows[] = { "worker-window\t4\t7\nworker-window\t4\t7\n", "worker-window\t4\t8\n", "worker-window\t-\t7\n", "worker-window\t2\t8\textra\n" };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(invalid_windows); i += 1)
    {
        states.count = 0;
        buffer->length = 0;
        nightly_append(buffer, "state\t%s\t-\t1\n%s", a, invalid_windows[i]);
        nightly_expect(test, !nightly_states_parse(&states, nightly_buffer_text(buffer)), "malformed or repeated worker metadata is refused");
    }
    states.count = 0;
    buffer->length = 0;
    nightly_append(buffer, "state\t%s\t-\t1\nworker-window\t-\t-\n", a);
    nightly_expect(test, nightly_states_parse(&states, nightly_buffer_text(buffer)) && states.items[0].worker_window_recorded && states.items[0].worker_end_hour == 0, "an explicitly unknown window is retained as unknown");
    states.count = 0;
    nightly_expect(test, !nightly_states_parse(&states, nightly_test_text("worker-window\t2\t8\n")), "window metadata before a state is refused");
    free(states.items);
    free(buffer);
}

typedef struct NightlyScenario NightlyScenario;
struct NightlyScenario
{
    NightlyStates states;
    NightlyLedger ledger;
    NightlyBuffer runs;
    NightlyPending pending[8];
    uint32_t pending_count;
    char refs[8][41];
    uint32_t ref_count;
    char refused[8][41];
    uint32_t refused_count;
    bool dry_run;
    bool prepare_only;
    const char* override;
    char output[1 << 15];
    char report[1 << 15];
};

// States 0..count-1, every one a code change committed an hour after the one before, from `committed`.
static NightlyScenario* nightly_scenario(uint32_t count, int64_t committed)
{
    NightlyScenario* scenario = nightly_allocate(1, sizeof(NightlyScenario));
    scenario->states.items = nightly_allocate(BUSTER_NIGHTLY_STATES, sizeof(NightlyState));
    scenario->ledger = nightly_ledger_new();
    for (uint32_t i = 0; i < count; i += 1)
    {
        NightlyState* state = &scenario->states.items[i];
        nightly_test_sha(1000 + i, state->sha);
        state->committed = committed + (int64_t)i * 3600;
        state->code = true;
        state->worker_window_recorded = true;
        state->worker_start_hour = BUSTER_NIGHTLY_WINDOW_START_HOUR;
        state->worker_end_hour = BUSTER_NIGHTLY_WINDOW_END_HOUR;
    }
    scenario->states.count = count;
    return scenario;
}

static void nightly_scenario_free(NightlyScenario* scenario)
{
    free(scenario->states.items);
    nightly_ledger_free(&scenario->ledger);
    free(scenario);
}

static void nightly_scenario_row(NightlyScenario* scenario, uint64_t run, uint32_t state, NightlyPurpose purpose, const char* request, NightlyPlain plain, NightlyHardware hardware, NightlyCoverage coverage, NightlyOutcome outcome)
{
    NightlyRow* row = &scenario->ledger.rows[scenario->ledger.row_count];
    *row = (NightlyRow){
        .run = run,
        .attempt = 1,
        .event = purpose == NIGHTLY_PURPOSE_PUSH ? NIGHTLY_EVENT_PUSH : purpose == NIGHTLY_PURPOSE_QUEUE ? NIGHTLY_EVENT_MERGE_GROUP : NIGHTLY_EVENT_DISPATCH,
        .purpose = purpose,
        .plain = plain,
        .hardware = hardware,
        .coverage = coverage,
        .outcome = outcome,
    };
    nightly_copy(row->commit, sizeof(row->commit), scenario->states.items[state].sha);
    nightly_copy(row->request, sizeof(row->request), request);
    scenario->ledger.row_count += 1;
}

// Every state's push qualified the plain rows and measured every plain run (the push before the nightly mode).
static void nightly_scenario_pushes(NightlyScenario* scenario)
{
    for (uint32_t i = 0; i < scenario->states.count; i += 1)
    {
        nightly_scenario_row(scenario, 100 + i, i, NIGHTLY_PURPOSE_PUSH, "", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_PLAIN, NIGHTLY_OUTCOME_COMPLETE);
    }
}

static void nightly_scenario_cycle(NightlyScenario* scenario, const char* date, uint32_t state)
{
    NightlyCycle* cycle = &scenario->ledger.cycles[scenario->ledger.cycle_count];
    nightly_copy(cycle->date, sizeof(cycle->date), date);
    nightly_copy(cycle->snapshot, sizeof(cycle->snapshot), scenario->states.items[state].sha);
    cycle->coordinator = 900;
    scenario->ledger.cycle_count += 1;
}

static void nightly_scenario_flying(NightlyScenario* scenario, uint64_t run, const char* purpose, const char* hardware, uint32_t state, const char* request, int64_t created)
{
    char title[BUSTER_NIGHTLY_NAME];
    snprintf(title, sizeof(title), "Nightly %s (%s) of %s [%s]", purpose, hardware, scenario->states.items[state].sha, request);
    nightly_test_run(&scenario->runs, run, "workflow_dispatch", "in_progress", "-", scenario->states.items[state].sha, created, created, title);
}

static void nightly_scenario_plan(NightlyScenario* scenario, int64_t now)
{
    NightlyFacts facts = nightly_facts_new();
    if (!nightly_facts_parse(&facts, nightly_buffer_text(&scenario->runs)))
    {
        nightly_fatal("self-test: scenario facts", NULL);
    }
    const NightlyPlanInput input = {
        .states = &scenario->states,
        .ledger = &scenario->ledger,
        .facts = &facts,
        .pending = scenario->pending,
        .pending_count = scenario->pending_count,
        .refs = scenario->refs,
        .ref_count = scenario->ref_count,
        .refused = scenario->refused,
        .refused_count = scenario->refused_count,
        .now = now,
        .main = scenario->states.items[scenario->states.count - 1].sha,
        .run = !scenario->dry_run,
        .coordinator = 900,
        .sequence = 1,
        .preparations = 3,
        .preparations_per_cycle = 40,
        .attempts = 3,
        .lease = 7200,
        .backlog_bound = 1500,
        .snapshot_bound = 1920,
        .override = scenario->override,
        .prepare_only = scenario->prepare_only,
    };
    // A fresh copy of the folded fields for each plan, as each coordinator step reads the inputs afresh.
    for (uint32_t i = 0; i < scenario->states.count; i += 1)
    {
        NightlyState* state = &scenario->states.items[i];
        const NightlyState kept = *state;
        *state = (NightlyState){ 0 };
        memcpy(state->sha, kept.sha, sizeof(state->sha));
        state->committed = kept.committed;
        state->code = kept.code;
        state->worker_window_recorded = kept.worker_window_recorded;
        state->worker_start_hour = kept.worker_start_hour;
        state->worker_end_hour = kept.worker_end_hour;
    }
    FILE* output = fmemopen(scenario->output, sizeof(scenario->output) - 1, "w");
    FILE* report = fmemopen(scenario->report, sizeof(scenario->report) - 1, "w");
    memset(scenario->output, 0, sizeof(scenario->output));
    memset(scenario->report, 0, sizeof(scenario->report));
    nightly_plan(&input, output, report);
    fclose(output);
    fclose(report);
    nightly_facts_free(&facts);
}

// Whether the output has the line, `*` matching any one field.
static bool nightly_has(const NightlyScenario* scenario, const char* line)
{
    return strstr(scenario->output, line) != NULL;
}

static uint32_t nightly_count(const NightlyScenario* scenario, const char* prefix)
{
    uint32_t count = 0;
    for (const char* at = scenario->output; at != NULL && *at != 0;)
    {
        count += strncmp(at, prefix, strlen(prefix)) == 0;
        at = strchr(at, '\n');
        at = at != NULL ? at + 1 : NULL;
    }
    return count;
}

static void nightly_test_plan(NightlyTest* test)
{
    const int64_t evening = nightly_test_madrid(2026, 11, 3, 22, 7);
    const int64_t opening = nightly_test_madrid(2026, 11, 4, 4, 10);
    const int64_t committed = nightly_test_madrid(2026, 11, 1, 9, 0);
    char line[256];

    // Before the window, nothing measured yet: pin the snapshot, prepare its daily hosted run, and the hosted rows
    // of the oldest states, three at a time; days-old states are still candidates (calendar rollover).
    NightlyScenario* scenario = nightly_scenario(6, committed);
    nightly_scenario_plan(scenario, evening);
    snprintf(line, sizeof(line), "pin\tcycle\t2026-11-04\t%s\t900\t", scenario->states.items[5].sha);
    nightly_expect(test, nightly_has(scenario, line), "the cycle pins main's head as its snapshot");
    snprintf(line, sizeof(line), "dispatch\tsnapshot\tdefer\t%s\t20261104.900.1.defer\t%s\n", scenario->states.items[5].sha, scenario->states.items[5].sha);
    nightly_expect(test, nightly_has(scenario, line), "the snapshot's daily hosted run is prepared first");
    snprintf(line, sizeof(line), "dispatch\tbacklog\tdefer\t%s\t20261104.900.2.defer\t%s\n", scenario->states.items[0].sha, scenario->states.items[5].sha);
    nightly_expect(test, nightly_has(scenario, line) && nightly_count(scenario, "dispatch\tbacklog\tdefer") == 3, "the oldest states' hosted rows are prepared, three at a time");
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 4 && nightly_count(scenario, "dispatch\tbacklog\tadmit") == 0 && nightly_has(scenario, "done\tprepared"), "nothing is admitted before 02:00");
    nightly_scenario_free(scenario);

    // The earlier schedules' steps, there for hours-late scheduled starts: before 22:00 the cycle pins and dispatches
    // nothing; from 22:00, the evening before the window, it pins and prepares; an override pins at any hour.
    scenario = nightly_scenario(6, committed);
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 3, 21, 59));
    nightly_expect(test, nightly_count(scenario, "pin\t") == 0 && nightly_count(scenario, "dispatch\t") == 0 && nightly_has(scenario, "done\tit is before 22:00 Europe/Madrid") && strstr(scenario->report, "No plan: it is before 22:00") != NULL, "a step before 22:00 pins nothing and plans nothing");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 3, 16, 7));
    nightly_expect(test, nightly_count(scenario, "pin\t") == 0 && nightly_count(scenario, "dispatch\t") == 0, "nor at 16:07, the earliest schedule");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 3, 22, 0));
    snprintf(line, sizeof(line), "pin\tcycle\t2026-11-04\t%s\t900\t", scenario->states.items[5].sha);
    nightly_expect(test, nightly_has(scenario, line) && nightly_count(scenario, "dispatch\tsnapshot\tdefer") == 1, "from 22:00 the cycle pins and prepares");
    scenario->override = "validating the coordinator";
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 3, 16, 7));
    nightly_expect(test, nightly_has(scenario, line) && nightly_count(scenario, "dispatch\tsnapshot\tdefer") == 1, "an override pins at any hour, and prepares (the snapshot has no hosted rows yet)");
    scenario->dry_run = true;
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 3, 16, 7));
    nightly_expect(test, nightly_count(scenario, "pin\t") == 0, "a dry run's override pins nothing early either");
    nightly_scenario_free(scenario);
    // The clock-change nights: 22:00 local either way. When the clocks go forward (2027-03-28), 21:30 CET, 5.5
    // elapsed hours before the window, doesn't pin yet; when they go back (2026-10-25), 22:07 CEST, 6.9 hours before,
    // does.
    scenario = nightly_scenario(6, nightly_test_madrid(2026, 10, 20, 9, 0));
    nightly_scenario_plan(scenario, nightly_test_madrid(2027, 3, 27, 21, 30));
    nightly_expect(test, nightly_count(scenario, "pin\t") == 0, "21:30 CET on the spring-forward evening pins nothing");
    nightly_scenario_plan(scenario, nightly_test_madrid(2027, 3, 27, 22, 0));
    nightly_expect(test, nightly_count(scenario, "pin\tcycle\t2027-03-28\t") == 1, "22:00 CET on the spring-forward evening pins");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 10, 24, 21, 59));
    nightly_expect(test, nightly_count(scenario, "pin\t") == 0, "21:59 CEST on the fall-back evening pins nothing");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 10, 24, 22, 7));
    nightly_expect(test, nightly_count(scenario, "pin\tcycle\t2026-10-25\t") == 1, "22:07 CEST on the fall-back evening pins");
    nightly_scenario_free(scenario);

    // Inside the window, the hosted rows qualified and the snapshot prepared: its comprehensive run first.
    scenario = nightly_scenario(6, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-04", 5);
    nightly_scenario_row(scenario, 200, 5, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.defer", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_DEFERRED);
    nightly_scenario_plan(scenario, opening);
    snprintf(line, sizeof(line), "dispatch\tsnapshot\tadmit\t%s\t20261104.900.1.admit\t%s\n", scenario->states.items[5].sha, scenario->states.items[5].sha);
    nightly_expect(test, nightly_has(scenario, line) && nightly_count(scenario, "dispatch\t") == 1 && nightly_has(scenario, "\nwait\t"), "the snapshot's comprehensive run is admitted first, alone");
    nightly_expect(test, strstr(scenario->report, "plain satisfies") == NULL && strstr(scenario->report, "| ready | 6 |") != NULL, "every-plain-run pushes don't satisfy targeted coverage");
    // A hardware run in flight: wait, admit nothing.
    nightly_scenario_flying(scenario, 300, "snapshot", "admit", 5, "20261104.900.1.admit", opening);
    nightly_scenario_plan(scenario, opening + 60);
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && nightly_has(scenario, "wait\ta hardware run is in flight"), "one hardware run at a time");
    // Once no backlog run could follow the one in flight before 08:00, the job ends instead of waiting (GitHub's six
    // hours for the window job); the run in flight ends by 08:00 under its own bound.
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 7, 40));
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && nightly_has(scenario, "done\ta hardware run is in flight, and no more hardware work fits before 08:00"), "a run in flight with no room after it ends the job");
    // A lost lease is cancelled.
    nightly_scenario_plan(scenario, opening + 7300);
    nightly_expect(test, nightly_has(scenario, "cancel\t300\tlease-expired"), "an admitted run past its lease is cancelled");
    // The snapshot measured fully; the oldest state is blocked by its plain rows; the next is dispatched.
    scenario->runs.length = 0;
    nightly_scenario_row(scenario, 301, 5, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_COMPLETE);
    nightly_scenario_row(scenario, 302, 0, NIGHTLY_PURPOSE_BACKLOG, "20261104.900.2.defer", NIGHTLY_PLAIN_FAILED, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_TARGETED, NIGHTLY_OUTCOME_BLOCKED_HOSTED);
    // Out of order: state 2 completed before state 1.
    nightly_scenario_row(scenario, 303, 2, NIGHTLY_PURPOSE_BACKLOG, "20261103.900.5.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_TARGETED, NIGHTLY_OUTCOME_COMPLETE);
    nightly_scenario_plan(scenario, opening + 1800);
    snprintf(line, sizeof(line), "dispatch\tbacklog\tadmit\t%s\t", scenario->states.items[1].sha);
    nightly_expect(test, nightly_has(scenario, line), "a blocked state doesn't starve the next; a later completion doesn't skip an earlier gap");
    nightly_expect(test, strstr(scenario->report, "| blocked-hosted | 1 |") != NULL && strstr(scenario->report, "| complete | 2 |") != NULL, "the report counts blocks and completions");
    // Late: nothing fits before 08:00.
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 7, 50));
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && nightly_has(scenario, "done\tno more hardware work fits before 08:00"), "nothing is admitted that can't finish by 08:00");
    // After the window: report alone.
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 8, 17));
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && nightly_has(scenario, "done\tthe window has closed"), "after 08:00 nothing is dispatched");
    // A dry run dispatches nothing.
    scenario->dry_run = true;
    nightly_scenario_plan(scenario, opening + 1800);
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && nightly_count(scenario, "would-dispatch\t") == 1 && nightly_has(scenario, "done\tdry run"), "a dry run only says what it would dispatch");
    scenario->dry_run = false;
    nightly_scenario_free(scenario);

    // Targeted coverage isn't the snapshot's full coverage; repeated failures stop a state; pending dispatches count.
    scenario = nightly_scenario(4, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_scenario_row(scenario, 200, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.defer", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_DEFERRED);
    nightly_scenario_row(scenario, 201, 3, NIGHTLY_PURPOSE_BACKLOG, "20261103.900.9.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_TARGETED, NIGHTLY_OUTCOME_COMPLETE);
    for (uint32_t i = 0; i < 3; i += 1)
    {
        nightly_scenario_row(scenario, 210 + i, 0, NIGHTLY_PURPOSE_BACKLOG, "20261103.900.1.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_TARGETED, i == 1 ? NIGHTLY_OUTCOME_INTERRUPTED : NIGHTLY_OUTCOME_FAILED);
    }
    nightly_scenario_plan(scenario, opening);
    snprintf(line, sizeof(line), "dispatch\tsnapshot\tadmit\t%s\t", scenario->states.items[3].sha);
    nightly_expect(test, nightly_has(scenario, line), "a targeted completion doesn't satisfy the snapshot's full coverage");
    nightly_scenario_row(scenario, 220, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_FAILED);
    nightly_scenario_plan(scenario, opening + 1800);
    snprintf(line, sizeof(line), "dispatch\tbacklog\tadmit\t%s\t", scenario->states.items[1].sha);
    nightly_expect(test, nightly_has(scenario, line) && strstr(scenario->report, "| failed | 1 |") != NULL, "three failed or interrupted measurements stop a state, reported");
    nightly_expect(test, nightly_count(scenario, "dispatch\tsnapshot\tadmit") == 0, "the snapshot is attempted once a cycle");
    NightlyPending* pending = &scenario->pending[0];
    *pending = (NightlyPending){
        .purpose = NIGHTLY_PURPOSE_BACKLOG,
        .admit = true,
        .dispatched = opening + 1790,
    };
    nightly_copy(pending->request, sizeof(pending->request), "20261104.900.7.admit");
    nightly_copy(pending->sha, sizeof(pending->sha), scenario->states.items[1].sha);
    scenario->pending_count = 1;
    nightly_scenario_plan(scenario, opening + 1800);
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && nightly_has(scenario, "wait\ta hardware run is in flight"), "a dispatch not yet visible as a run isn't repeated");
    nightly_scenario_plan(scenario, opening + 1790 + BUSTER_NIGHTLY_PENDING_AGE + 1);
    nightly_expect(test, nightly_count(scenario, "dispatch\tbacklog\tadmit") == 1, "a dispatch that never appeared expires");
    scenario->pending_count = 0;
    nightly_scenario_free(scenario);

    // An unchanged main: yesterday's comprehensive run of the same snapshot doesn't meet today's daily requirement.
    scenario = nightly_scenario(4, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-03", 3);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_scenario_row(scenario, 190, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261103.900.1.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_COMPLETE);
    nightly_scenario_row(scenario, 200, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.defer", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_DEFERRED);
    nightly_scenario_plan(scenario, opening);
    snprintf(line, sizeof(line), "dispatch\tsnapshot\tadmit\t%s\t", scenario->states.items[3].sha);
    nightly_expect(test, nightly_has(scenario, line), "an unchanged main still gets today's comprehensive run");
    // Outside the window, nothing is admitted; a manual run's override admits, and the report says why.
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 13, 0));
    nightly_expect(test, nightly_count(scenario, "dispatch\tsnapshot\tadmit") == 0, "13:00 admits nothing");
    scenario->override = "validating the coordinator";
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 13, 0));
    nightly_expect(test, nightly_has(scenario, line) && strstr(scenario->report, "**Override**") != NULL && strstr(scenario->report, "validating the coordinator") != NULL, "an override admits outside the window, reported");
    scenario->dry_run = true;
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 13, 0));
    nightly_expect(test, nightly_count(scenario, "would-dispatch\tsnapshot\tadmit") == 0, "a dry run's override admits nothing");
    nightly_scenario_free(scenario);

    // Main moved on after the pin: the cycle keeps its snapshot, and every dispatch names it, not main's head.
    scenario = nightly_scenario(5, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_scenario_row(scenario, 200, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.defer", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_DEFERRED);
    nightly_scenario_plan(scenario, opening);
    snprintf(line, sizeof(line), "dispatch\tsnapshot\tadmit\t%s\t20261104.900.1.admit\t%s\n", scenario->states.items[3].sha, scenario->states.items[3].sha);
    nightly_expect(test, nightly_has(scenario, line) && !nightly_has(scenario, scenario->states.items[4].sha), "after main moves on, the cycle's pinned snapshot is the one dispatched and named");
    nightly_scenario_free(scenario);

    // The prepare job's step inside the window (or under an override) prepares and admits nothing: admission is the
    // window job's alone, which would not know of a hardware run the prepare job dispatched.
    scenario = nightly_scenario(4, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_scenario_row(scenario, 200, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.defer", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_DEFERRED);
    scenario->prepare_only = true;
    nightly_scenario_plan(scenario, opening);
    nightly_expect(test, nightly_count(scenario, "dispatch\tsnapshot\tadmit") == 0 && nightly_count(scenario, "dispatch\tbacklog\tadmit") == 0 && nightly_has(scenario, "done\tprepared; the window job admits hardware"), "the prepare job admits nothing inside the window");
    scenario->override = "validating the coordinator";
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 13, 0));
    nightly_expect(test, nightly_count(scenario, "dispatch\tsnapshot\tadmit") == 0, "nor under an override");
    nightly_scenario_free(scenario);

    // An ineligible snapshot: the newest eligible earlier state is measured instead, and said so.
    scenario = nightly_scenario(4, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_scenario_row(scenario, 200, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.defer", NIGHTLY_PLAIN_FAILED, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_BLOCKED_HOSTED);
    nightly_scenario_plan(scenario, opening);
    snprintf(line, sizeof(line), "dispatch\tsnapshot\tadmit\t%s\t", scenario->states.items[2].sha);
    nightly_expect(test, nightly_has(scenario, line) && strstr(scenario->report, "measures `") != NULL && strstr(scenario->report, "ineligible (its plain hosted rows failed)") != NULL, "an ineligible snapshot is replaced by the newest eligible state, reported");
    nightly_scenario_free(scenario);
    // The same with the substitute already complete (its targeted measurement done) and its nightly ref still there:
    // the plan that dispatches it never prunes that ref too. A finished state the plan didn't dispatch is pruned.
    scenario = nightly_scenario(4, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_row(scenario, 150, 0, NIGHTLY_PURPOSE_BACKLOG, "20261103.900.10001.admit", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_TARGETED, NIGHTLY_OUTCOME_COMPLETE);
    nightly_scenario_row(scenario, 152, 2, NIGHTLY_PURPOSE_BACKLOG, "20261103.900.10002.admit", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_TARGETED, NIGHTLY_OUTCOME_COMPLETE);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_scenario_row(scenario, 200, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.defer", NIGHTLY_PLAIN_FAILED, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_BLOCKED_HOSTED);
    nightly_copy(scenario->refs[0], 41, scenario->states.items[2].sha);
    nightly_copy(scenario->refs[1], 41, scenario->states.items[0].sha);
    scenario->ref_count = 2;
    nightly_scenario_plan(scenario, opening);
    snprintf(line, sizeof(line), "dispatch\tsnapshot\tadmit\t%s\t", scenario->states.items[2].sha);
    char pruned[64];
    snprintf(pruned, sizeof(pruned), "prune\t%s\n", scenario->states.items[2].sha);
    nightly_expect(test, nightly_has(scenario, line) && !nightly_has(scenario, pruned), "the substitute dispatched keeps its existing ref in the same plan");
    snprintf(pruned, sizeof(pruned), "prune\t%s\n", scenario->states.items[0].sha);
    nightly_expect(test, nightly_has(scenario, pruned) && nightly_count(scenario, "prune\t") == 1, "and a finished state's ref is still pruned");
    nightly_scenario_free(scenario);

    // A rerun keeps its run's ID and takes the next attempt: a state's verdicts are its run's latest attempt's, in
    // whichever order the ledger holds the attempts, hosted and native alike, and the cycle counts the run once.
    for (uint32_t order = 0; order < 2; order += 1)
    {
        for (uint32_t later_fails = 0; later_fails < 2; later_fails += 1)
        {
            const NightlyPlain verdicts[2] = { later_fails ? NIGHTLY_PLAIN_OK : NIGHTLY_PLAIN_FAILED, later_fails ? NIGHTLY_PLAIN_FAILED : NIGHTLY_PLAIN_OK };
            scenario = nightly_scenario(2, committed);
            nightly_scenario_cycle(scenario, "2026-11-04", 1);
            for (uint32_t k = 0; k < 2; k += 1)
            {
                const uint32_t attempt = order == 0 ? k + 1 : 2 - k;
                nightly_scenario_row(scenario, 200, 0, NIGHTLY_PURPOSE_BACKLOG, "20261103.900.1.defer", verdicts[attempt - 1], NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_TARGETED, verdicts[attempt - 1] == NIGHTLY_PLAIN_OK ? NIGHTLY_OUTCOME_DEFERRED : NIGHTLY_OUTCOME_BLOCKED_HOSTED);
                scenario->ledger.rows[scenario->ledger.row_count - 1].attempt = attempt;
            }
            nightly_scenario_plan(scenario, opening);
            const bool blocked = strstr(scenario->report, "| blocked-hosted | 1 |") != NULL;
            nightly_expect(test, later_fails ? blocked : !blocked && strstr(scenario->report, "| ready | 1 |") != NULL, later_fails ? "a rerun's failed plain rows block the state, whatever the ledger's order" : "a rerun's passing plain rows unblock the state, whatever the ledger's order");
            nightly_scenario_free(scenario);

            const NightlyOutcome outcomes[2] = { later_fails ? NIGHTLY_OUTCOME_FAILED : NIGHTLY_OUTCOME_BLOCKED_NATIVE, later_fails ? NIGHTLY_OUTCOME_BLOCKED_NATIVE : NIGHTLY_OUTCOME_FAILED };
            scenario = nightly_scenario(2, committed);
            nightly_scenario_pushes(scenario);
            nightly_scenario_cycle(scenario, "2026-11-04", 1);
            for (uint32_t k = 0; k < 2; k += 1)
            {
                const uint32_t attempt = order == 0 ? k + 1 : 2 - k;
                nightly_scenario_row(scenario, 210, 0, NIGHTLY_PURPOSE_BACKLOG, "20261103.900.10001.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_TARGETED, outcomes[attempt - 1]);
                scenario->ledger.rows[scenario->ledger.row_count - 1].attempt = attempt;
            }
            nightly_scenario_plan(scenario, opening);
            const bool native = strstr(scenario->report, "| blocked-native | 1 |") != NULL;
            nightly_expect(test, later_fails ? native : !native, later_fails ? "a rerun's failed native correctness blocks the state, whatever the ledger's order" : "a rerun past its native failure unblocks the state, whatever the ledger's order");
            nightly_scenario_free(scenario);
        }
        // This cycle's admitted run, failed and then rerun to completion: one run selected, completed, none failed.
        scenario = nightly_scenario(2, committed);
        nightly_scenario_pushes(scenario);
        nightly_scenario_cycle(scenario, "2026-11-04", 1);
        for (uint32_t k = 0; k < 2; k += 1)
        {
            const uint32_t attempt = order == 0 ? k + 1 : 2 - k;
            nightly_scenario_row(scenario, 220, 0, NIGHTLY_PURPOSE_BACKLOG, "20261104.900.10001.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_TARGETED, attempt == 1 ? NIGHTLY_OUTCOME_FAILED : NIGHTLY_OUTCOME_COMPLETE);
            scenario->ledger.rows[scenario->ledger.row_count - 1].attempt = attempt;
        }
        scenario->prepare_only = true;
        nightly_scenario_plan(scenario, opening);
        nightly_expect(test, strstr(scenario->report, "This cycle: 1 hardware runs selected, 1 completed, 0 failed or blocked, 0 deferred") != NULL, "the cycle counts a rerun run once, by its latest attempt");
        nightly_scenario_free(scenario);
    }

    // Documentation-only states are not required; the snapshot's hosted run in flight keeps its room.
    scenario = nightly_scenario(4, committed);
    scenario->states.items[1].code = false;
    nightly_scenario_row(scenario, 100, 0, NIGHTLY_PURPOSE_PUSH, "", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_PLAIN, NIGHTLY_OUTCOME_COMPLETE);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_scenario_flying(scenario, 400, "snapshot", "defer", 3, "20261104.900.1.defer", opening - 600);
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 7, 20));
    nightly_expect(test, nightly_count(scenario, "dispatch\tbacklog\tadmit") == 0 && nightly_has(scenario, "wait\tthe snapshot's hosted qualification is in flight"), "a backlog run doesn't take the waiting snapshot's room");
    nightly_scenario_plan(scenario, opening);
    snprintf(line, sizeof(line), "dispatch\tbacklog\tadmit\t%s\t", scenario->states.items[0].sha);
    nightly_expect(test, nightly_has(scenario, line) && nightly_count(scenario, "dispatch\tbacklog\tdefer") == 1, "with room left, the ready state runs while the snapshot prepares");
    snprintf(line, sizeof(line), "dispatch\tbacklog\tdefer\t%s\t", scenario->states.items[2].sha);
    nightly_expect(test, nightly_has(scenario, line) && strstr(scenario->report, "| not-required | 1 |") != NULL, "a documentation-only state is never dispatched");
    nightly_scenario_free(scenario);

    // A state whose nightly/SHA branch the workflow token couldn't create (on 2026-11-05, every merge whose pull
    // request's head lacked main's workflow changes) is skipped for the rest of the job, reported: it neither keeps a
    // preparation slot nor stays the oldest ready state while later states wait.
    scenario = nightly_scenario(6, committed);
    nightly_scenario_cycle(scenario, "2026-11-04", 5);
    nightly_scenario_flying(scenario, 400, "snapshot", "defer", 5, "20261104.900.1.defer", evening - 60);
    nightly_copy(scenario->refused[0], 41, scenario->states.items[0].sha);
    nightly_copy(scenario->refused[1], 41, scenario->states.items[1].sha);
    nightly_copy(scenario->refused[2], 41, scenario->states.items[1].sha);
    scenario->refused_count = 3;
    nightly_scenario_plan(scenario, evening);
    snprintf(line, sizeof(line), "dispatch\tbacklog\tdefer\t%s\t", scenario->states.items[0].sha);
    const bool first = nightly_has(scenario, line);
    snprintf(line, sizeof(line), "dispatch\tbacklog\tdefer\t%s\t", scenario->states.items[4].sha);
    nightly_expect(test, !first && nightly_has(scenario, line) && nightly_count(scenario, "dispatch\tbacklog\tdefer") == 3, "a refused state's preparation slot goes to the next states");
    nightly_expect(test, nightly_has(scenario, "alert\t2 states skipped: their nightly branch could not be created or used\n") && strstr(scenario->report, "**Branch refused**: this job could not create or use the nightly branch of 2 states") != NULL && strstr(scenario->report, "ready (branch refused)") == NULL && strstr(scenario->report, "needs-preparation (branch refused)") != NULL, "refused states are counted once and reported");
    nightly_scenario_free(scenario);
    scenario = nightly_scenario(4, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_scenario_row(scenario, 200, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.defer", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_DEFERRED);
    nightly_scenario_row(scenario, 201, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.2.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_COMPLETE);
    nightly_copy(scenario->refused[0], 41, scenario->states.items[0].sha);
    scenario->refused_count = 1;
    nightly_scenario_plan(scenario, opening + 1800);
    snprintf(line, sizeof(line), "dispatch\tbacklog\tadmit\t%s\t", scenario->states.items[1].sha);
    nightly_expect(test, nightly_has(scenario, line) && nightly_count(scenario, "dispatch\t") == 1, "a refused oldest ready state doesn't starve the next");
    nightly_scenario_free(scenario);
    // A refused snapshot: the newest eligible earlier state takes its comprehensive run, reported.
    scenario = nightly_scenario(4, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_copy(scenario->refused[0], 41, scenario->states.items[3].sha);
    nightly_copy(scenario->refused[1], 41, scenario->states.items[2].sha);
    scenario->refused_count = 2;
    nightly_scenario_plan(scenario, opening);
    snprintf(line, sizeof(line), "dispatch\tsnapshot\tadmit\t%s\t", scenario->states.items[1].sha);
    nightly_expect(test, nightly_has(scenario, line) && nightly_count(scenario, "dispatch\tsnapshot\tdefer") == 0 && strstr(scenario->report, "ineligible (its nightly branch could not be created or used)") != NULL, "a refused snapshot is replaced by the newest eligible state that isn't refused");
    nightly_scenario_free(scenario);

    // Branches: a finished state's is pruned, one in flight is kept; the alert when arrivals outpace completions
    // (twelve states arrived in a day, one completed in the one cycle observed).
    scenario = nightly_scenario(12, nightly_test_madrid(2026, 11, 3, 9, 0));
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-04", 11);
    nightly_scenario_row(scenario, 200, 0, NIGHTLY_PURPOSE_BACKLOG, "20261103.900.1.admit", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_TARGETED, NIGHTLY_OUTCOME_COMPLETE);
    nightly_scenario_flying(scenario, 401, "backlog", "defer", 1, "20261104.900.2.defer", opening - 60);
    nightly_copy(scenario->refs[0], 41, scenario->states.items[0].sha);
    nightly_copy(scenario->refs[1], 41, scenario->states.items[1].sha);
    scenario->ref_count = 2;
    nightly_scenario_plan(scenario, evening);
    snprintf(line, sizeof(line), "prune\t%s\n", scenario->states.items[0].sha);
    nightly_expect(test, nightly_has(scenario, line) && nightly_count(scenario, "prune\t") == 1, "a finished state's branch is pruned, one in flight kept");
    nightly_expect(test, nightly_has(scenario, "alert\t") && strstr(scenario->report, "cannot drain") != NULL, "arrivals beyond the observed capacity alert");
    nightly_scenario_free(scenario);
}

static void nightly_test_worker_plan(NightlyTest* test)
{
    const int64_t committed = nightly_test_madrid(2026, 11, 1, 9, 0);
    const int64_t early = nightly_test_madrid(2026, 11, 4, 2, 10);
    char old[256];
    char current[256];
    char pruned[80];
    NightlyScenario* scenario = nightly_scenario(4, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_scenario_row(scenario, 200, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.defer", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_DEFERRED);
    nightly_scenario_row(scenario, 201, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.2.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_COMPLETE);
    scenario->states.items[0].worker_start_hour = 4;
    scenario->states.items[0].worker_end_hour = 7;
    scenario->states.items[2].worker_start_hour = 0;
    scenario->states.items[2].worker_end_hour = 0;
    nightly_copy(scenario->refs[0], 41, scenario->states.items[0].sha);
    scenario->ref_count = 1;
    snprintf(old, sizeof(old), "dispatch\tbacklog\tadmit\t%s\t", scenario->states.items[0].sha);
    snprintf(current, sizeof(current), "dispatch\tbacklog\tadmit\t%s\t", scenario->states.items[1].sha);
    snprintf(pruned, sizeof(pruned), "prune\t%s\n", scenario->states.items[0].sha);
    nightly_scenario_plan(scenario, early);
    nightly_expect(test, !nightly_has(scenario, old) && nightly_has(scenario, current) && nightly_count(scenario, "dispatch\t") == 1, "before 04:00 an old ready worker doesn't starve a later current worker");
    nightly_expect(test, strstr(scenario->report, "worker window 04:00-07:00 Europe/Madrid") != NULL && strstr(scenario->report, "**Worker window unknown**: 1 pending") != NULL && scenario->states.items[2].status == NIGHTLY_STATUS_READY, "old and unknown windows are reported; unknown is pending, not complete or failed");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 4, 0));
    nightly_expect(test, nightly_has(scenario, old) && !nightly_has(scenario, current), "a fresh plan at 04:00 admits the older state; the constraint survives planning again");
    const int64_t last_old = nightly_test_madrid(2026, 11, 4, 6, 35);
    nightly_scenario_plan(scenario, last_old);
    nightly_expect(test, nightly_has(scenario, old), "the old worker can start exactly its backlog bound before 07:00");
    nightly_scenario_plan(scenario, last_old + 1);
    nightly_expect(test, !nightly_has(scenario, old) && nightly_has(scenario, current), "one second later, the old worker is skipped and a later worker still runs");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 7, 30));
    nightly_expect(test, !nightly_has(scenario, old) && nightly_has(scenario, current), "after the old window closes a current worker still uses the expanded window");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 7, 35) + 1);
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && nightly_has(scenario, "done\tno more hardware work fits"), "the current hard cutoff still includes the whole backlog bound");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 8, 0));
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && nightly_has(scenario, "done\tthe window has closed"), "no worker is admitted at the global cutoff");
    // Only the old and unknown workers remain. Early: wait for 04:00. Late: carry them over, keeping their refs.
    nightly_scenario_row(scenario, 202, 1, NIGHTLY_PURPOSE_BACKLOG, "20261104.900.3.admit", NIGHTLY_PLAIN_NONE, NIGHTLY_HARDWARE_ADMIT, NIGHTLY_COVERAGE_TARGETED, NIGHTLY_OUTCOME_COMPLETE);
    nightly_scenario_plan(scenario, early);
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && nightly_has(scenario, "wait\tready states await their worker windows"), "a night with only older ready workers waits for their opening instead of ending early");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 7, 10));
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && !nightly_has(scenario, pruned) && scenario->states.items[0].status == NIGHTLY_STATUS_READY && !scenario->states.items[0].targeted && strstr(scenario->report, "Carried over: 2 states") != NULL, "closed and unknown workers remain pending for later cycles and are not pruned");
    nightly_scenario_free(scenario);

    // The comprehensive run also has its worker's own window, whether it is the snapshot or a substitute.
    scenario = nightly_scenario(4, committed);
    nightly_scenario_pushes(scenario);
    nightly_scenario_cycle(scenario, "2026-11-04", 3);
    nightly_scenario_row(scenario, 200, 3, NIGHTLY_PURPOSE_SNAPSHOT, "20261104.900.1.defer", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_FULL, NIGHTLY_OUTCOME_DEFERRED);
    for (uint32_t i = 0; i < 4; i += 1)
    {
        if (i != 1)
        {
            scenario->states.items[i].worker_start_hour = 4;
            scenario->states.items[i].worker_end_hour = 7;
        }
    }
    snprintf(old, sizeof(old), "dispatch\tsnapshot\tadmit\t%s\t", scenario->states.items[3].sha);
    snprintf(current, sizeof(current), "dispatch\tbacklog\tadmit\t%s\t", scenario->states.items[1].sha);
    nightly_scenario_plan(scenario, early);
    nightly_expect(test, !nightly_has(scenario, old) && nightly_has(scenario, current), "a snapshot whose worker opens later lets eligible backlog work run while keeping its room");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 4, 0));
    nightly_expect(test, nightly_has(scenario, old) && nightly_count(scenario, "dispatch\t") == 1, "the snapshot takes priority as soon as its own window opens");
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 7, 10));
    snprintf(current, sizeof(current), "dispatch\tsnapshot\tadmit\t%s\t", scenario->states.items[1].sha);
    nightly_expect(test, !nightly_has(scenario, old) && nightly_has(scenario, current) && strstr(scenario->report, "ineligible (its comprehensive run no longer fits its worker window)") != NULL, "a late snapshot's substitute must itself fit; a closed newer candidate doesn't starve it");
    scenario->ledger.rows[3].plain = NIGHTLY_PLAIN_NONE;
    scenario->ledger.rows[4].plain = NIGHTLY_PLAIN_NONE;
    nightly_scenario_flying(scenario, 300, "snapshot", "defer", 3, "20261104.900.8.defer", nightly_test_madrid(2026, 11, 4, 6, 0));
    nightly_scenario_plan(scenario, nightly_test_madrid(2026, 11, 4, 6, 10));
    nightly_expect(test, nightly_count(scenario, "dispatch\t") == 0 && nightly_has(scenario, "wait\tthe snapshot's hosted qualification is in flight"), "backlog work cannot consume a waiting old snapshot's room before its 07:00 deadline");
    scenario->runs.length = 0;
    scenario->ledger.rows[3].plain = NIGHTLY_PLAIN_OK;
    scenario->states.items[3].worker_start_hour = 0;
    scenario->states.items[3].worker_end_hour = 0;
    nightly_scenario_plan(scenario, early);
    nightly_expect(test, nightly_has(scenario, current) && !nightly_has(scenario, old) && strstr(scenario->report, "ineligible (its worker window is unknown)") != NULL, "an unknown snapshot worker cannot starve a known eligible substitute");
    nightly_scenario_free(scenario);

    // The hours are Madrid civil hours, not UTC offsets copied from the day the worker was committed.
    scenario = nightly_scenario(2, nightly_test_madrid(2026, 1, 1, 9, 0));
    nightly_scenario_pushes(scenario);
    scenario->states.items[0].worker_start_hour = 4;
    scenario->states.items[0].worker_end_hour = 7;
    scenario->dry_run = true;
    nightly_scenario_plan(scenario, 1774746000);
    nightly_expect(test, scenario->states.items[0].worker_start == nightly_test_madrid(2026, 3, 29, 4, 0) && scenario->states.items[0].worker_deadline == nightly_test_madrid(2026, 3, 29, 7, 0) && scenario->states.items[1].worker_start == 1774746000, "spring-forward: old workers start at 04:00 CEST, current workers at 03:00 CEST");
    nightly_scenario_plan(scenario, 1792886400 + 3600);
    nightly_expect(test, scenario->states.items[0].worker_start == nightly_test_madrid(2026, 10, 25, 4, 0) && scenario->states.items[0].worker_deadline == nightly_test_madrid(2026, 10, 25, 7, 0) && scenario->states.items[1].worker_start == 1792886400, "fall-back: old workers start at 04:00 CET, current workers at the first 02:00");
    nightly_scenario_free(scenario);
}

// The reuse of a run by tree identity holds only while the build and the inventory read no Git metadata: a commit's
// hash or date there would make two runs of one tree differ. Any `git` in them makes this fail, for a person to
// decide what the reuse must then compare (docs/ci.md).
static void nightly_test_metadata(NightlyTest* test, const char* root)
{
    static const char* const files[] = { "build.sh", "CMakeLists.txt", "cmake/BusterSettings.cmake", "cmake/BusterPostLink.cmake", "cmake/BusterPrint.cmake", "cmake/BusterCallGraph.cmake", "cmake/BusterCompileCommands.cmake", "cmake/BusterMergeProfile.cmake", "cmake/BusterShiftPadding.cmake", "tools/ci/correctness.tests" };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(files); i += 1)
    {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", root, files[i]);
        FILE* file = fopen(path, "rb");
        bool clean = file != NULL;
        if (file != NULL)
        {
            fclose(file);
            NightlyText text = nightly_read(path);
            for (const char* at = strstr(text.data, "git"); clean && at != NULL; at = strstr(at + 1, "git"))
            {
                const bool before = at == text.data || !((at[-1] >= 'a' && at[-1] <= 'z') || (at[-1] >= 'A' && at[-1] <= 'Z') || at[-1] == '_' || at[-1] == '-' || at[-1] == '.' || at[-1] == '/');
                const bool after = !((at[3] >= 'a' && at[3] <= 'z') || (at[3] >= 'A' && at[3] <= 'Z') || at[3] == '_' || at[3] == '-');
                clean = !(before && after);
            }
            free(text.data);
        }
        char what[256];
        snprintf(what, sizeof(what), "%s runs no git (tree-identical runs are equivalent)", files[i]);
        nightly_expect(test, clean, what);
    }
}

// The step that starts at `name` in `job`, to the blank line that ends it (NULL when either is missing).
static const char* nightly_step(const char* job, const char* name, const char** end)
{
    const char* step = job != NULL ? strstr(job, name) : NULL;
    *end = step != NULL ? strstr(step + 8, "\n\n") : NULL;
    return *end != NULL ? step : NULL;
}

// The 9700X job's window step must come right after the host's own check (the operating system and the CPU model,
// nothing more) and before the host identity's package queries and hashing and any setup (the checkout, the
// toolchain's staging), so that a job that took the lock too late does nothing but refuse.
static void nightly_test_workflow(NightlyTest* test, const char* root)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", root, BUSTER_NIGHTLY_WORKFLOW);
    FILE* file = fopen(path, "rb");
    bool found = file != NULL;
    if (file != NULL)
    {
        fclose(file);
    }
    NightlyText text = found ? nightly_read(path) : (NightlyText){ 0 };
    const char* job = found ? strstr(text.data, "\n  benchmarks:\n") : NULL;
    const char* end = job != NULL ? strstr(job + 1, "\n  policy:\n") : NULL;
    const char* verify_end = NULL;
    const char* verify = nightly_step(job, "      - name: Verify benchmark host\n", &verify_end);
    const char* window = job != NULL ? strstr(job, "      - name: Admit nightly work inside the Europe/Madrid window, holding the lock\n") : NULL;
    const char* identity_end = NULL;
    const char* identity = nightly_step(job, "      - name: Record the host identity\n", &identity_end);
    const char* checkout = job != NULL ? strstr(job, "      - name: Check out triggering commit\n") : NULL;
    const char* stage = job != NULL ? strstr(job, "      - name: Stage official Clang 23.1.4 in the private runner cache\n") : NULL;
    const char* first = job != NULL ? strstr(job, "      - name: ") : NULL;
    const char* after_verify = verify != NULL ? strstr(verify + 8, "\n      - name: ") : NULL;
    nightly_expect(test, end != NULL && first == verify && after_verify != NULL && after_verify + 1 == window && window < identity && identity < checkout && identity < stage && stage < end, "the 9700X job checks the window right after the host, before the host identity and any setup");
    // The host's own check, before the window, does none of the identity's work.
    bool light = verify != NULL;
    const char* const heavy[] = { "pacman", "sha256sum", "xargs", "ldd", "gcc", "kernel=" };
    for (size_t i = 0; light && i < sizeof(heavy) / sizeof(heavy[0]); i += 1)
    {
        const char* at = strstr(verify, heavy[i]);
        light = at == NULL || at >= verify_end;
    }
    nightly_expect(test, light, "the host check before the window queries no package and hashes nothing");
    // The window step reads no checked-out file: it compares the evidence job's epochs with the clock.
    const char* window_end = window != NULL ? strstr(window + 8, "\n      - name: ") : NULL;
    bool reads_checkout = window == NULL || window_end == NULL;
    for (const char* at = window; !reads_checkout && at != NULL && at < window_end; at = strstr(at + 1, "tools/"))
    {
        reads_checkout = at != window && at < window_end;
    }
    nightly_expect(test, !reads_checkout, "the window step needs nothing checked out");
    // Pull requests run no native correctness (#1272), so no push reuses a pull request's native run: no job but the
    // 9700X job itself takes the host.
    nightly_expect(test, found && strstr(text.data, "\n  host-identity:\n") == NULL, "no host-identity job confirms a pull request's host for a push");
    free(text.data);
}

// A merge-queue candidate's run (#1430): its jobs as the API names them (each row's shards under its caller's name, the
// vector pool's verdict, the fuzz campaigns, a skipped job's unexpanded name), every one successful but where `odd`
// (ROW SHARD, `vector` or `fuzz ENTRY`) names a job to leave out or, with `conclusion`, to conclude otherwise.
static void nightly_test_candidate(NightlyBuffer* buffer, uint64_t run, const char* odd, const char* conclusion)
{
    static const char* const architectures[] = { "x86-64", "native" };
    for (uint32_t row = 0; row < BUSTER_NIGHTLY_ROWS_FULL; row += 1)
    {
        const char* architecture = architectures[row / BUSTER_NIGHTLY_SANITIZERS];
        const char* sanitizer = nightly_sanitizer_names[row % BUSTER_NIGHTLY_SANITIZERS];
        const uint32_t shards = row == 2 || row == 3 ? 6 : row == 7 ? 5 : 4;
        nightly_append(buffer, "job\t%llu\tcompleted\tskipped\tTests on Ubuntu 26.04 (%s, %s, O3) / Vector row verdict (Clang 23, ${{ inputs.sanitizer }}, O${{ inputs.optimization }})\n", (unsigned long long)run, architecture, sanitizer);
        for (uint32_t k = 1; k <= shards; k += 1)
        {
            char name[64];
            snprintf(name, sizeof(name), "%s %s %u", architecture, sanitizer, k);
            const bool is_odd = strcmp(name, odd) == 0;
            if (!is_odd || conclusion != NULL)
            {
                nightly_append(buffer, "job\t%llu\t%s\t%s\tTests on Ubuntu 26.04 (%s, %s, O3) / Tests on Ubuntu 26.04 (Clang 23, %s, %s, O3, shard %u of %u)\n", (unsigned long long)run, is_odd && strcmp(conclusion, "-") == 0 ? "in_progress" : "completed", is_odd ? conclusion : "success", architecture, sanitizer, architecture, sanitizer, k, shards);
            }
        }
    }
    static const char vector[] = "Tests on Ubuntu 26.04 (vector, none address thread memory, O3) / ";
    nightly_append(buffer, "job\t%llu\tcompleted\tskipped\t%sTests on Ubuntu 26.04 (Clang 23, ${{ inputs.host_arch }}, ${{ inputs.sanitizer }}, O${{ inputs.optimization }}, shard ${{ matrix.shard }} of ${{ inputs.shards }})\n", (unsigned long long)run, vector);
    nightly_append(buffer, "job\t%llu\tcompleted\tfailure\t%sVector draw 1, candidate 3 (Clang 23, none address thread memory, O3)\n", (unsigned long long)run, vector);
    if (strcmp(odd, "vector") != 0 || conclusion != NULL)
    {
        nightly_append(buffer, "job\t%llu\tcompleted\t%s\t%sVector row verdict (Clang 23, none address thread memory, O3)\n", (unsigned long long)run, strcmp(odd, "vector") == 0 ? conclusion : "success", vector);
    }
    for (uint32_t j = 0; j < BUSTER_NIGHTLY_COUNT(nightly_fuzz_entries); j += 1)
    {
        char name[64];
        snprintf(name, sizeof(name), "fuzz %s", nightly_fuzz_entries[j]);
        const bool is_odd = strcmp(name, odd) == 0;
        if (!is_odd || conclusion != NULL)
        {
            nightly_append(buffer, "job\t%llu\tcompleted\t%s\tlibFuzzer (ASan + UBSan + leaks, %s)\n", (unsigned long long)run, is_odd ? conclusion : "success", nightly_fuzz_entries[j]);
        }
    }
    nightly_append(buffer, "job\t%llu\tcompleted\tskipped\t%s\n", (unsigned long long)run, BUSTER_NIGHTLY_HARDWARE_JOB);
}

static bool nightly_test_coverage_called(const char* odd, const char* conclusion, const char* extra, const char* auxiliary)
{
    NightlyBuffer* buffer = nightly_allocate(1, sizeof(NightlyBuffer));
    nightly_test_candidate(buffer, 60, odd, conclusion);
    if (extra != NULL)
    {
        nightly_append(buffer, "%s\n", extra);
    }
    NightlyFacts facts = nightly_facts_new();
    FILE* sink = fopen("/dev/null", "w");
    const bool complete = nightly_facts_parse(&facts, nightly_buffer_text(buffer)) && nightly_coverage(&facts, 60, auxiliary, sink);
    fclose(sink);
    nightly_facts_free(&facts);
    free(buffer);
    return complete;
}

static bool nightly_test_coverage(const char* odd, const char* conclusion, const char* extra)
{
    return nightly_test_coverage_called(odd, conclusion, extra, ",");
}

// A candidate's auxiliary workflows: those the selector named ran a job each, all successful; the others ran none.
static void nightly_test_auxiliary(NightlyTest* test)
{
    const char* build = "job\t60\tcompleted\tsuccess\tAuxiliary (build.yml) / Fresh, incremental, failed and switched builds (Clang 23, native)\n"
                        "job\t60\tcompleted\tsuccess\tAuxiliary (build.yml) / build.sh's now() under decimal points other than a period";
    const char* selfhost = "job\t60\tcompleted\tsuccess\tAuxiliary (selfhost.yml) / Self-host harness regression tests\n"
                           "job\t60\tcompleted\tsuccess\tAuxiliary (selfhost.yml) / prepare-hosted-tools / Prepare verified hosted release inputs\n"
                           "job\t60\tcompleted\tsuccess\tAuxiliary (selfhost.yml) / Self-host bootstrap and probe equivalence (native)";
    char both[1024];
    snprintf(both, sizeof(both), "%s\n%s", build, selfhost);
    nightly_expect(test, nightly_test_coverage_called("", NULL, build, ",build,"), "a called auxiliary workflow whose jobs succeeded");
    nightly_expect(test, nightly_test_coverage_called("", NULL, both, ",build,selfhost,"), "two called auxiliary workflows, nested calls included");
    nightly_expect(test, nightly_test_coverage_called("", NULL, "job\t60\tcompleted\tskipped\tAuxiliary (build.yml)", ","), "a skipped call, not called");
    static const struct
    {
        const char* extra;
        const char* auxiliary;
        const char* what;
    } short_of[] = {
        { NULL, ",build,", "a called auxiliary workflow that ran nothing" },
        { "job\t60\tcompleted\tskipped\tAuxiliary (build.yml)", ",build,", "a called auxiliary workflow skipped" },
        { "job\t60\tcompleted\tfailure\tAuxiliary (tier1-problem.yml) / correctness (native)", ",tier1-problem,", "a failed auxiliary job" },
        { "job\t60\tcompleted\tcancelled\tAuxiliary (build-pgo.yml) / build.sh refuses BUSTER_PGO's unsupported builds before compiling", ",build-pgo,", "a cancelled auxiliary job" },
        { "job\t60\tin_progress\t-\tAuxiliary (build-pgo.yml) / build.sh refuses BUSTER_PGO's unsupported builds before compiling", ",build-pgo,", "an auxiliary job still running" },
        { "job\t60\tcompleted\tsuccess\tAuxiliary (selfhost.yml) / Self-host harness regression tests", ",", "an auxiliary workflow that ran though the selector didn't name it" },
        { NULL, "", "no selector list" },
        { NULL, ",bogus,", "an unknown auxiliary workflow" },
        { NULL, ",selfhost,build,", "auxiliary workflows out of order" },
        { NULL, ",build,build,", "an auxiliary workflow named twice" },
        { NULL, "build", "a list without its commas" },
        { NULL, ",build", "a list without its last comma" },
    };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(short_of); i += 1)
    {
        nightly_expect(test, !nightly_test_coverage_called("", NULL, short_of[i].extra, short_of[i].auxiliary), short_of[i].what);
    }
    char failed[1024];
    snprintf(failed, sizeof(failed), "%s\njob\t60\tcompleted\tfailure\tAuxiliary (selfhost.yml) / Self-host bootstrap and probe equivalence (x86-64)", selfhost);
    nightly_expect(test, !nightly_test_coverage_called("", NULL, failed, ",selfhost,"), "one failed job among an auxiliary workflow's successful ones");
    nightly_expect(test, !nightly_test_coverage_called("x86-64 none 1", NULL, build, ",build,"), "a called auxiliary workflow doesn't make up for a missing shard");
    nightly_expect(test, nightly_test_coverage_called("", NULL, "job\t61\tcompleted\tfailure\tAuxiliary (build.yml) / Fresh, incremental, failed and switched builds (Clang 23, native)", ","), "another run's auxiliary jobs don't count");
}

static bool nightly_test_stale(const char* input, uint64_t own, const char* expected)
{
    char* copy = nightly_allocate(strlen(input) + 1, 1);
    memcpy(copy, input, strlen(input));
    NightlyText text = {
        .data = copy,
        .length = strlen(input),
    };
    char* written = NULL;
    size_t length = 0;
    FILE* output = open_memstream(&written, &length);
    const bool valid = nightly_stale(text, own, output);
    fclose(output);
    const bool same = expected != NULL ? valid && strcmp(written, expected) == 0 : !valid && length == 0;
    free(written);
    free(copy);
    return same;
}

// Invalidated candidates' runs: cancelled when their branch is gone or moved and their commit isn't on main; kept
// otherwise, and nothing at all on input it can't read.
static void nightly_test_stale_candidates(NightlyTest* test)
{
    nightly_expect(test, nightly_test_stale("candidate\t70\tgone\tunmerged\n", 99, "cancel\t70\n"), "a dropped candidate's run is cancelled");
    nightly_expect(test, nightly_test_stale("candidate\t71\tmoved\tunmerged\n", 99, "cancel\t71\n"), "a candidate rebuilt on the same base: its old run is cancelled");
    nightly_expect(test, nightly_test_stale("candidate\t72\tgone\tmerged\n", 99, "keep\t72\tit merged\n"), "a merged candidate's run is kept");
    nightly_expect(test, nightly_test_stale("candidate\t73\tpresent\tunknown\n", 99, "keep\t73\tits candidate is queued\n"), "a queued candidate's run is kept");
    nightly_expect(test, nightly_test_stale("candidate\t74\tunknown\tunknown\n", 99, "keep\t74\tits branch is unknown\n"), "a failed branch read cancels nothing");
    nightly_expect(test, nightly_test_stale("candidate\t75\tgone\tunknown\n", 99, "keep\t75\tunknown whether it merged\n"), "a failed ancestry read cancels nothing");
    nightly_expect(test, nightly_test_stale("candidate\t99\tgone\tunmerged\n", 99, "keep\t99\tthis run\n"), "this run is never cancelled");
    nightly_expect(test, nightly_test_stale("", 99, ""), "no candidates");
    nightly_expect(test, nightly_test_stale("candidate\t70\tgone\tunmerged\ncandidate\t73\tpresent\tunknown\n", 99, "cancel\t70\nkeep\t73\tits candidate is queued\n"), "several candidates");
    static const char* const unreadable[] = {
        "candidate\t70\tgone\n",
        "candidate\t70\tdeleted\tunmerged\n",
        "candidate\t70\tgone\tmaybe\n",
        "candidate\tx\tgone\tunmerged\n",
        "run\t70\tgone\tunmerged\n",
        "candidate\t70\tgone\tunmerged\ncandidate\t70\tpresent\tunknown\n",
        "candidate\t70\tgone\tunmerged\ncandidate\t71\tgone\n",
    };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(unreadable); i += 1)
    {
        nightly_expect(test, nightly_test_stale(unreadable[i], 99, NULL), "unreadable candidates cancel nothing");
    }
}

// The merge queue (#1430): a candidate's coverage, job by job; its runs' hosted reuse, records, rows and ledger lines;
// and the report's coverage gaps once the queue has run.
static void nightly_test_queue(NightlyTest* test)
{
    nightly_expect(test, nightly_test_coverage("", NULL, NULL), "a full candidate's coverage is complete");
    nightly_test_auxiliary(test);
    nightly_test_stale_candidates(test);
    static const struct
    {
        const char* odd;
        const char* conclusion;
        const char* what;
    } short_of[] = {
        { "x86-64 none 1", NULL, "a plain shard missing" },
        { "native memory 5", NULL, "a sanitizer row's last shard missing" },
        { "x86-64 address 2", "failure", "a failed shard" },
        { "x86-64 thread 6", "cancelled", "a cancelled shard" },
        { "native thread 3", "skipped", "a skipped shard" },
        { "native none 4", "timed_out", "a timed-out shard" },
        { "x86-64 memory 1", "-", "a shard still in progress" },
        { "vector", NULL, "the vector verdict missing" },
        { "vector", "failure", "the vector verdict failed" },
        { "vector", "skipped", "the vector verdict skipped" },
        { "fuzz frontend", NULL, "a fuzz campaign missing" },
        { "fuzz crash-replay-proof", "cancelled", "a fuzz campaign cancelled" },
    };
    for (uint32_t i = 0; i < BUSTER_NIGHTLY_COUNT(short_of); i += 1)
    {
        nightly_expect(test, !nightly_test_coverage(short_of[i].odd, short_of[i].conclusion, NULL), short_of[i].what);
    }
    nightly_expect(test, !nightly_test_coverage("", NULL, "job\t60\tcompleted\tsuccess\tTests on Ubuntu 26.04 (x86-64, none, O3) / Tests on Ubuntu 26.04 (Clang 23, x86-64, none, O3, shard 5 of 5)"), "a row whose shards disagree");
    nightly_expect(test, nightly_test_coverage("", NULL, "job\t60\tcompleted\tfailure\tTests on Ubuntu 26.04 (native, address, O1) / Tests on Ubuntu 26.04 (Clang 23, native, address, O1, shard 1 of 4)"), "a row at another optimization isn't the contract's, and doesn't count");
    nightly_expect(test, !nightly_test_coverage("", NULL, "job\t60\tcompleted\tsuccess\tTests on Ubuntu 26.04 (vector, none address thread memory, O3) / Vector row verdict (Clang 24, none address thread memory, O3)"), "two vector verdicts");
    nightly_expect(test, nightly_test_coverage("", NULL, "job\t61\tcompleted\tfailure\tlibFuzzer (ASan + UBSan + leaks, frontend)"), "another run's jobs don't count");

    char a[41];
    char b[41];
    char tree[41];
    nightly_test_sha(1, a);
    nightly_test_sha(2, b);
    nightly_test_sha(3, tree);
    NightlyBuffer* buffer = nightly_allocate(1, sizeof(NightlyBuffer));
    // A backlog run reuses the plain rows of its commit's candidate run: main was fast-forwarded to that commit.
    nightly_test_run(buffer, 60, "merge_group", "completed", "success", a, 1, 2, "x");
    nightly_test_candidate(buffer, 60, "", NULL);
    nightly_append(buffer, "job\t60\tcompleted\tsuccess\t%s\n", BUSTER_NIGHTLY_AGGREGATE_JOB);
    nightly_test_run(buffer, 61, "merge_group", "completed", "failure", b, 1, 2, "x");
    nightly_test_candidate(buffer, 61, "x86-64 address 1", "failure");
    nightly_append(buffer, "job\t61\tcompleted\tfailure\t%s\n", BUSTER_NIGHTLY_AGGREGATE_JOB);
    nightly_test_run(buffer, 62, "merge_group", "completed", "failure", b, 3, 4, "x");
    nightly_test_candidate(buffer, 62, "", NULL);
    nightly_append(buffer, "job\t62\tcompleted\tfailure\t%s\n", BUSTER_NIGHTLY_AGGREGATE_JOB);
    NightlyFacts facts = nightly_facts_new();
    nightly_expect(test, nightly_facts_parse(&facts, nightly_buffer_text(buffer)) && facts.runs[0].event == NIGHTLY_EVENT_MERGE_GROUP, "a merge-queue run's facts parse");
    const NightlyHosted hosted = nightly_hosted(&facts, a, 0);
    nightly_expect(test, hosted.verdict == NIGHTLY_PLAIN_OK && hosted.run == 60, "a candidate's plain rows are its commit's hosted qualification");

    // Its row: a queue row that measured nothing, a pass only through the aggregate.
    NightlyRecord* record = nightly_allocate(1, sizeof(NightlyRecord));
    nightly_test_record(record, 60, "merge_group", "queue", a, tree, tree, b);
    nightly_copy(record->values[NIGHTLY_FIELD_SCOPE], BUSTER_NIGHTLY_VALUE, "full");
    nightly_copy(record->values[NIGHTLY_FIELD_HARDWARE], BUSTER_NIGHTLY_VALUE, "skip");
    nightly_copy(record->values[NIGHTLY_FIELD_HARDWARE_REASON], BUSTER_NIGHTLY_VALUE, "merge-group");
    nightly_copy(record->values[NIGHTLY_FIELD_FUZZ_RESULT], BUSTER_NIGHTLY_VALUE, "success");
    nightly_copy(record->values[NIGHTLY_FIELD_BENCHMARK_RESULT], BUSTER_NIGHTLY_VALUE, "skipped");
    nightly_copy(record->values[NIGHTLY_FIELD_QUALIFICATION_RESULT], BUSTER_NIGHTLY_VALUE, "success");
    nightly_expect(test, nightly_record_outcome(record) == NIGHTLY_OUTCOME_DEFERRED && nightly_record_coverage(record) == NIGHTLY_COVERAGE_NONE, "a candidate the aggregate passed is deferred to the window, measuring nothing");
    nightly_copy(record->values[NIGHTLY_FIELD_COVERAGE], BUSTER_NIGHTLY_VALUE, "none");
    nightly_copy(record->values[NIGHTLY_FIELD_OUTCOME], BUSTER_NIGHTLY_VALUE, "deferred");
    NightlyRow row = nightly_harvest_row(&facts, &facts.runs[0], record);
    nightly_expect(test, row.purpose == NIGHTLY_PURPOSE_QUEUE && row.event == NIGHTLY_EVENT_MERGE_GROUP && row.outcome == NIGHTLY_OUTCOME_DEFERRED && row.coverage == NIGHTLY_COVERAGE_NONE && row.plain == NIGHTLY_PLAIN_OK, "an agreeing queue record is the row");
    nightly_copy(record->values[NIGHTLY_FIELD_CODE], BUSTER_NIGHTLY_VALUE, "false");
    nightly_expect(test, nightly_record_outcome(record) == NIGHTLY_OUTCOME_DEFERRED, "a candidate has no documentation exemption");
    nightly_copy(record->values[NIGHTLY_FIELD_CODE], BUSTER_NIGHTLY_VALUE, "true");
    nightly_copy(record->values[NIGHTLY_FIELD_QUALIFICATION_RESULT], BUSTER_NIGHTLY_VALUE, "failure");
    nightly_expect(test, nightly_record_outcome(record) == NIGHTLY_OUTCOME_INTERRUPTED, "a candidate the aggregate failed is no pass, its tests green or not");
    nightly_copy(record->values[NIGHTLY_FIELD_QUALIFICATION_RESULT], BUSTER_NIGHTLY_VALUE, "success");
    nightly_copy(record->values[NIGHTLY_FIELD_TEST_RESULT], BUSTER_NIGHTLY_VALUE, "failure");
    nightly_expect(test, nightly_record_outcome(record) == NIGHTLY_OUTCOME_BLOCKED_HOSTED, "a candidate whose tests failed is blocked");
    nightly_copy(record->values[NIGHTLY_FIELD_TEST_RESULT], BUSTER_NIGHTLY_VALUE, "success");
    nightly_copy(record->values[NIGHTLY_FIELD_HARDWARE], BUSTER_NIGHTLY_VALUE, "admit");
    nightly_expect(test, nightly_record_outcome(record) == NIGHTLY_OUTCOME_REJECTED, "a candidate admitted to the 9700X is rejected");
    nightly_copy(record->values[NIGHTLY_FIELD_HARDWARE], BUSTER_NIGHTLY_VALUE, "skip");
    nightly_copy(record->values[NIGHTLY_FIELD_PURPOSE], BUSTER_NIGHTLY_VALUE, "push");
    row = nightly_harvest_row(&facts, &facts.runs[0], record);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_INVALID, "a candidate's record must be a queue record");
    nightly_test_record(record, 62, "merge_group", "queue", b, tree, tree, a);
    nightly_copy(record->values[NIGHTLY_FIELD_COVERAGE], BUSTER_NIGHTLY_VALUE, "none");
    nightly_copy(record->values[NIGHTLY_FIELD_OUTCOME], BUSTER_NIGHTLY_VALUE, "deferred");
    row = nightly_harvest_row(&facts, &facts.runs[2], record);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_INVALID, "a queue record can't claim a pass its failed aggregate didn't make");
    row = nightly_harvest_row(&facts, &facts.runs[0], NULL);
    nightly_expect(test, row.purpose == NIGHTLY_PURPOSE_QUEUE && row.outcome == NIGHTLY_OUTCOME_DEFERRED && row.coverage == NIGHTLY_COVERAGE_NONE, "without a record, the aggregate's success passes a candidate");
    row = nightly_harvest_row(&facts, &facts.runs[2], NULL);
    nightly_expect(test, row.outcome != NIGHTLY_OUTCOME_DEFERRED, "without a record, a failed aggregate is no pass");
    row = nightly_harvest_row(&facts, &facts.runs[1], NULL);
    nightly_expect(test, row.outcome == NIGHTLY_OUTCOME_BLOCKED_HOSTED, "without a record, a failed shard blocks it");
    // The aggregate's name before #1430 renamed it, in an earlier run's jobs, and the alias that reports it during
    // the move: the renamed aggregate decides wherever it ran.
    {
        NightlyBuffer* named = nightly_allocate(1, sizeof(NightlyBuffer));
        nightly_test_run(named, 63, "merge_group", "completed", "success", a, 1, 2, "x");
        nightly_test_candidate(named, 63, "", NULL);
        nightly_append(named, "job\t63\tcompleted\tsuccess\t%s\n", BUSTER_NIGHTLY_AGGREGATE_JOB_BEFORE);
        nightly_test_run(named, 64, "merge_group", "completed", "failure", a, 1, 2, "x");
        nightly_test_candidate(named, 64, "", NULL);
        nightly_append(named, "job\t64\tcompleted\tfailure\t%s\n", BUSTER_NIGHTLY_AGGREGATE_JOB);
        nightly_append(named, "job\t64\tcompleted\tsuccess\t%s\n", BUSTER_NIGHTLY_AGGREGATE_JOB_BEFORE);
        NightlyFacts earlier = nightly_facts_new();
        const bool parsed = nightly_facts_parse(&earlier, nightly_buffer_text(named));
        nightly_expect(test, parsed && nightly_harvest_row(&earlier, &earlier.runs[0], NULL).outcome == NIGHTLY_OUTCOME_DEFERRED, "an earlier run's aggregate under its old name passes a candidate");
        nightly_expect(test, parsed && nightly_harvest_row(&earlier, &earlier.runs[1], NULL).outcome != NIGHTLY_OUTCOME_DEFERRED, "the renamed aggregate decides over its alias");
        nightly_facts_free(&earlier);
        free(named);
    }
    // Its ledger line reads back as written.
    row = nightly_harvest_row(&facts, &facts.runs[0], NULL);
    char* data = NULL;
    size_t size = 0;
    FILE* stream = open_memstream(&data, &size);
    fprintf(stream, "%s\n", BUSTER_NIGHTLY_LEDGER_FORMAT);
    nightly_row_write(stream, &row);
    fclose(stream);
    NightlyLedger ledger = nightly_ledger_new();
    nightly_expect(test, nightly_ledger_parse(&ledger, (NightlyText){ .data = data, .length = size }) && ledger.row_count == 1 && ledger.rows[0].purpose == NIGHTLY_PURPOSE_QUEUE && ledger.rows[0].event == NIGHTLY_EVENT_MERGE_GROUP, "a queue row reads back from the ledger");
    nightly_ledger_free(&ledger);
    free(data);
    nightly_facts_free(&facts);
    free(record);
    free(buffer);

    // The report: before any candidate's run, nothing; after one, every later state without a passing candidate's run of
    // its own commit is a gap. A candidate's plain rows make its state ready with no hosted preparation.
    const int64_t evening = nightly_test_madrid(2026, 11, 3, 22, 7);
    NightlyScenario* scenario = nightly_scenario(5, nightly_test_madrid(2026, 11, 1, 9, 0));
    nightly_scenario_plan(scenario, evening);
    nightly_expect(test, strstr(scenario->report, "Merge queue") == NULL && nightly_count(scenario, "alert\t") == 0, "before the queue's first run, no queue report");
    nightly_scenario_row(scenario, 70, 1, NIGHTLY_PURPOSE_QUEUE, "", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_NONE, NIGHTLY_OUTCOME_DEFERRED);
    nightly_scenario_row(scenario, 71, 2, NIGHTLY_PURPOSE_QUEUE, "", NIGHTLY_PLAIN_FAILED, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_NONE, NIGHTLY_OUTCOME_BLOCKED_HOSTED);
    nightly_scenario_row(scenario, 72, 4, NIGHTLY_PURPOSE_QUEUE, "", NIGHTLY_PLAIN_OK, NIGHTLY_HARDWARE_SKIP, NIGHTLY_COVERAGE_NONE, NIGHTLY_OUTCOME_DEFERRED);
    nightly_scenario_plan(scenario, evening);
    char line[256];
    snprintf(line, sizeof(line), "Merge queue: 4 states since `%.12s`, the first a candidate's run tested; 2 without a passing candidate's run of their own commit", scenario->states.items[1].sha);
    nightly_expect(test, strstr(scenario->report, line) != NULL, "the report counts the states since the queue's first run without a pass");
    snprintf(line, sizeof(line), " `%.12s` `%.12s`", scenario->states.items[2].sha, scenario->states.items[3].sha);
    nightly_expect(test, strstr(scenario->report, line) != NULL, "and names them");
    nightly_expect(test, nightly_has(scenario, "alert\t2 states have no passing merge-queue run of their own commit\n"), "and alerts");
    nightly_expect(test, scenario->states.items[1].status == NIGHTLY_STATUS_READY && scenario->states.items[4].status == NIGHTLY_STATUS_READY, "a candidate's plain rows make its state ready for the window");
    nightly_expect(test, scenario->states.items[2].status == NIGHTLY_STATUS_BLOCKED_HOSTED, "a candidate's failed plain row blocks its state");
    snprintf(line, sizeof(line), "dispatch\tbacklog\tdefer\t%s\t", scenario->states.items[1].sha);
    nightly_expect(test, !nightly_has(scenario, line), "a state a candidate qualified needs no hosted preparation");
    nightly_scenario_free(scenario);
}

static int nightly_self_test(const char* root)
{
    NightlyTest test = { 0 };
    nightly_quiet = true;
    nightly_test_window(&test);
    nightly_test_facts(&test);
    nightly_test_records(&test);
    nightly_test_ledger(&test);
    nightly_test_queue(&test);
    nightly_test_states(&test);
    nightly_test_plan(&test);
    nightly_test_worker_plan(&test);
    nightly_test_metadata(&test, root);
    nightly_test_workflow(&test, root);
    printf("nightly --self-test: %u checks, %u failures\n", test.checks, test.failures);
    return test.failures != 0;
}

// --- main ---

static const char* nightly_option(int argc, char** argv, const char* name)
{
    const char* value = NULL;
    for (int i = 2; i + 1 < argc; i += 1)
    {
        value = strcmp(argv[i], name) == 0 ? argv[i + 1] : value;
    }
    return value;
}

static int64_t nightly_option_epoch(int argc, char** argv, const char* name)
{
    int64_t value = 0;
    if (!nightly_epoch(nightly_option(argc, argv, name), &value))
    {
        nightly_fatal("expected seconds since the epoch for", name);
    }
    return value;
}

static uint64_t nightly_option_number(int argc, char** argv, const char* name, uint64_t fallback, uint64_t limit)
{
    const char* text = nightly_option(argc, argv, name);
    uint64_t value = fallback;
    if (text != NULL && !nightly_number(text, limit, &value))
    {
        nightly_fatal("expected a number for", name);
    }
    return value;
}

static const char* nightly_required(int argc, char** argv, const char* name)
{
    const char* value = nightly_option(argc, argv, name);
    if (value == NULL)
    {
        nightly_fatal("missing option", name);
    }
    return value;
}

static NightlyFacts nightly_load_facts(const char* path)
{
    NightlyFacts facts = nightly_facts_new();
    NightlyText text = nightly_read(path);
    if (!nightly_facts_parse(&facts, text))
    {
        nightly_fatal("invalid facts", path);
    }
    free(text.data);
    return facts;
}

static NightlyLedger nightly_load_ledger(const char* path)
{
    NightlyLedger ledger = nightly_ledger_new();
    NightlyText text = nightly_read(path);
    if (!nightly_ledger_parse(&ledger, text))
    {
        nightly_fatal("invalid ledger", path);
    }
    free(text.data);
    return ledger;
}

static NightlyStates nightly_load_states(const char* path)
{
    NightlyStates states = {
        .items = nightly_allocate(BUSTER_NIGHTLY_STATES, sizeof(NightlyState)),
    };
    NightlyText text = nightly_read(path);
    if (!nightly_states_parse(&states, text) || states.count == 0)
    {
        nightly_fatal("invalid or empty states", path);
    }
    free(text.data);
    return states;
}

static int nightly_command_plan(int argc, char** argv)
{
    NightlyStates states = nightly_load_states(nightly_required(argc, argv, "--states"));
    NightlyLedger ledger = nightly_load_ledger(nightly_required(argc, argv, "--ledger"));
    NightlyFacts facts = nightly_load_facts(nightly_required(argc, argv, "--facts"));
    const char* mode = nightly_required(argc, argv, "--mode");
    const char* main_head = nightly_required(argc, argv, "--main");
    const int64_t now = nightly_option_epoch(argc, argv, "--now");
    uint64_t coordinator = 0;
    if ((strcmp(mode, "run") != 0 && strcmp(mode, "dry-run") != 0) || !nightly_sha(main_head) || !nightly_id(nightly_required(argc, argv, "--coordinator"), &coordinator))
    {
        nightly_fatal("plan needs --mode run|dry-run, --main SHA and --coordinator RUN", NULL);
    }
    NightlyPending* pending = nightly_allocate(BUSTER_NIGHTLY_PENDING, sizeof(NightlyPending));
    uint32_t pending_count = 0;
    char (*refs)[41] = nightly_allocate(BUSTER_NIGHTLY_REFS, sizeof(*refs));
    uint32_t ref_count = 0;
    const char* pending_path = nightly_option(argc, argv, "--pending");
    const char* refs_path = nightly_option(argc, argv, "--refs");
    char (*refused)[41] = nightly_allocate(BUSTER_NIGHTLY_REFS, sizeof(*refused));
    uint32_t refused_count = 0;
    const char* refused_path = nightly_option(argc, argv, "--refused");
    char line[BUSTER_NIGHTLY_LINE];
    bool valid = true;
    size_t offset = 0;
    if (pending_path != NULL)
    {
        NightlyText text = nightly_read(pending_path);
        valid = nightly_pending_parse(text, &facts, now, pending, &pending_count);
        free(text.data);
    }
    offset = 0;
    if (valid && refs_path != NULL)
    {
        NightlyText text = nightly_read(refs_path);
        while (valid && nightly_next_line(text, &offset, line, &valid))
        {
            valid = valid && ref_count < BUSTER_NIGHTLY_REFS && nightly_sha(line);
            if (valid)
            {
                nightly_copy(refs[ref_count], 41, line);
                ref_count += 1;
            }
        }
        free(text.data);
    }
    offset = 0;
    if (valid && refused_path != NULL)
    {
        NightlyText text = nightly_read(refused_path);
        while (valid && nightly_next_line(text, &offset, line, &valid))
        {
            valid = valid && refused_count < BUSTER_NIGHTLY_REFS && nightly_sha(line);
            if (valid)
            {
                nightly_copy(refused[refused_count], 41, line);
                refused_count += 1;
            }
        }
        free(text.data);
    }
    if (!valid)
    {
        nightly_fatal("invalid pending dispatches, branches or refused branches", NULL);
    }
    char override[BUSTER_NIGHTLY_REASON];
    nightly_sanitize(nightly_option(argc, argv, "--override"), override, sizeof(override));
    const NightlyPlanInput input = {
        .states = &states,
        .ledger = &ledger,
        .facts = &facts,
        .pending = pending,
        .pending_count = pending_count,
        .refs = refs,
        .ref_count = ref_count,
        .refused = refused,
        .refused_count = refused_count,
        .now = now,
        .main = main_head,
        .run = strcmp(mode, "run") == 0,
        .coordinator = coordinator,
        .sequence = (uint32_t)nightly_option_number(argc, argv, "--sequence", 1, UINT32_MAX),
        .preparations = (uint32_t)nightly_option_number(argc, argv, "--preparations", 3, 64),
        .preparations_per_cycle = (uint32_t)nightly_option_number(argc, argv, "--preparations-per-cycle", 40, 4096),
        .attempts = (uint32_t)nightly_option_number(argc, argv, "--attempts", 3, 64),
        .lease = (int64_t)nightly_option_number(argc, argv, "--lease", 7200, 86400),
        .backlog_bound = (int64_t)nightly_option_number(argc, argv, "--backlog-bound", 1500, 86400),
        .snapshot_bound = (int64_t)nightly_option_number(argc, argv, "--snapshot-bound", 1920, 86400),
        .override = override,
        .earlier = (uint32_t)nightly_option_number(argc, argv, "--earlier", 0, UINT32_MAX),
        .prepare_only = argc >= 3 && strcmp(argv[argc - 1], "--prepare-only") == 0,
    };
    FILE* report = fopen(nightly_required(argc, argv, "--report"), "wb");
    if (report == NULL)
    {
        nightly_fatal("cannot write the report", NULL);
    }
    if (pending_path != NULL)
    {
        nightly_pending_write(pending_path, pending, pending_count);
    }
    nightly_plan(&input, stdout, report);
    fclose(report);
    free(pending);
    free(refs);
    free(refused);
    free(states.items);
    nightly_ledger_free(&ledger);
    nightly_facts_free(&facts);
    return 0;
}

int main(int argc, char** argv)
{
    nightly_use_madrid();
    const char* command = argc >= 2 ? argv[1] : "";
    int status = 2;
    if (strcmp(command, "--self-test") == 0)
    {
        const char* root = nightly_option(argc, argv, "--root");
        status = nightly_self_test(root != NULL ? root : ".");
    }
    else if (strcmp(command, "window") == 0)
    {
        const int64_t now = nightly_option_epoch(argc, argv, "--now");
        const NightlyWindow window = nightly_window(now, (int64_t)nightly_option_number(argc, argv, "--bound", 0, 86400));
        char reason[BUSTER_NIGHTLY_REASON];
        nightly_sanitize(nightly_option(argc, argv, "--override"), reason, sizeof(reason));
        const bool overridden = window.valid && !window.open && reason[0] != 0;
        printf("%s\t%s%s%s\n", window.open || overridden ? "open" : "closed", window.reason, overridden ? "; overridden: " : "", overridden ? reason : "");
        status = !window.valid ? 2 : window.open || overridden ? 0 : 1;
    }
    else if (strcmp(command, "window-times") == 0)
    {
        const NightlyWindow window = nightly_window(nightly_option_epoch(argc, argv, "--now"), 0);
        if (window.valid)
        {
            printf("cycle\t%s\nstart\t%lld\ndeadline\t%lld\nlocal\t%02d:%02d %s\n", window.cycle, (long long)window.start, (long long)window.deadline, window.local.hour, window.local.minute, window.local.zone);
        }
        status = window.valid ? 0 : 2;
    }
    else if (strcmp(command, "worker-window") == 0)
    {
        NightlyText source = nightly_read("-");
        uint32_t start = 0;
        uint32_t end = 0;
        if (nightly_worker_source(source, &start, &end))
        {
            printf("worker-window\t%u\t%u\n", start, end);
        }
        else
        {
            printf("worker-window\t-\t-\n");
        }
        free(source.data);
        status = 0;
    }
    else if (strcmp(command, "hosted") == 0)
    {
        const char* commit = nightly_required(argc, argv, "--commit");
        if (!nightly_sha(commit))
        {
            nightly_fatal("hosted needs --commit SHA", NULL);
        }
        NightlyFacts facts = nightly_load_facts("-");
        const NightlyHosted hosted = nightly_hosted(&facts, commit, nightly_option_number(argc, argv, "--exclude", 0, UINT64_MAX));
        printf("hosted\t%s\t%llu\n", hosted.verdict == NIGHTLY_PLAIN_OK ? "reused" : hosted.verdict == NIGHTLY_PLAIN_FAILED ? "failed" : "none", (unsigned long long)hosted.run);
        nightly_facts_free(&facts);
        status = 0;
    }
    else if (strcmp(command, "coverage") == 0)
    {
        uint64_t run = 0;
        if (!nightly_id(nightly_required(argc, argv, "--run"), &run))
        {
            nightly_fatal("coverage needs --run ID", NULL);
        }
        const char* auxiliary = nightly_required(argc, argv, "--auxiliary");
        NightlyFacts facts = nightly_load_facts("-");
        status = nightly_coverage(&facts, run, auxiliary, stdout) ? 0 : 1;
        nightly_facts_free(&facts);
    }
    else if (strcmp(command, "stale") == 0)
    {
        uint64_t run = 0;
        if (!nightly_id(nightly_required(argc, argv, "--run"), &run))
        {
            nightly_fatal("stale needs --run ID", NULL);
        }
        NightlyText input = nightly_read("-");
        status = nightly_stale(input, run, stdout) ? 0 : 1;
        free(input.data);
    }
    else if (strcmp(command, "record") == 0)
    {
        NightlyRecord* record = nightly_allocate(1, sizeof(NightlyRecord));
        nightly_record_from_environment(record);
        FILE* output = fopen(nightly_required(argc, argv, "--output"), "wb");
        if (output == NULL)
        {
            nightly_fatal("cannot write the record", NULL);
        }
        nightly_record_write(output, record);
        fclose(output);
        printf("coverage\t%s\noutcome\t%s\n", nightly_value(record, NIGHTLY_FIELD_COVERAGE), nightly_value(record, NIGHTLY_FIELD_OUTCOME));
        free(record);
        status = 0;
    }
    else if (strcmp(command, "harvest") == 0)
    {
        NightlyLedger ledger = nightly_load_ledger(nightly_required(argc, argv, "--ledger"));
        NightlyFacts facts = nightly_load_facts(nightly_required(argc, argv, "--facts"));
        NightlyStates states = nightly_load_states(nightly_required(argc, argv, "--states"));
        const char** shas = nightly_allocate(states.count, sizeof(char*));
        for (uint32_t i = 0; i < states.count; i += 1)
        {
            shas[i] = states.items[i].sha;
        }
        const char* records = nightly_option(argc, argv, "--records");
        const bool list = argc >= 3 && strcmp(argv[argc - 1], "--list") == 0;
        if (!list && records == NULL)
        {
            nightly_fatal("harvest needs --records DIR, or --list", NULL);
        }
        nightly_harvest(&ledger, &facts, records != NULL ? records : "", shas, states.count, list, stdout);
        free(shas);
        free(states.items);
        nightly_facts_free(&facts);
        nightly_ledger_free(&ledger);
        status = 0;
    }
    else if (strcmp(command, "plan") == 0)
    {
        status = nightly_command_plan(argc, argv);
    }
    else
    {
        fputs("usage: nightly --self-test [--root DIR] | window | window-times | worker-window | hosted | coverage | record | harvest | plan (see tools/ci/nightly.c)\n", stderr);
    }
    return status;
}
