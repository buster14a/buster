// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C driver's stage clock (#518): `--time-stages` reports where a compile's time went, stage by stage, on standard
// error. It is off by default: every lap is a test of a null pointer, and only a driver run with the option passes a
// clock. A lap charges the time since the previous lap to the stage it names, so the stages never overlap and add up
// to the clock's whole span, from the driver's first instruction to its final stage mark; what the process spends outside
// it (exec, the loader, main's dispatch and exit) is the difference between the process's wall time and that span.
// The clock reads the TSC, whose rate the report measures against CLOCK_MONOTONIC over the same span. A mark is a lap
// that also reads the process's resource use (getrusage), charging the system time, minor page faults and growth of
// the peak resident set since the last mark to its stage; a lap alone reads only the TSC, for the passes that run per
// function (lowering, Tier 0, sizing, encoding, rows and unwind), whose resource use the mark after them charges to its
// stage. Those laps run only when the functions compile on the calling thread alone (one lane, #151): on several
// lanes their time is one stage, `lanes`, the lanes' span, since the stages of a thread don't add up across threads.
// docs/compile-time.md has the stages and the 9700X's numbers.

#include <sys/resource.h>

typedef enum CheckStage
{
    // The options, and the intern table's, the preprocessor's and the parser's initialization.
    CHECK_STAGE_SETUP,
    // pp_run: the files read, lexed, refined and their directive tables built (PPOptions.timing's phases), and the
    // main loop's expansion.
    CHECK_STAGE_PREPROCESS,
    // parse_translation_unit, and its diagnostics printed.
    CHECK_STAGE_PARSE,
    // The type table's reservation and initialization.
    CHECK_STAGE_TYPE_TABLE,
    // check_input: declarations, bindings and the expression graph from the parser's tree.
    CHECK_STAGE_CHECK_INPUT,
    // check_graph_run: the expressions' types and conversions.
    CHECK_STAGE_CHECK_EXPRESSIONS,
    // Assembly operands, initializers and the diagnostics' report.
    CHECK_STAGE_CHECK_DECLARATIONS,
    // check_data_prepare: the static data's selection and layout.
    CHECK_STAGE_STATIC_DATA,
    // check_program: statements and control flow.
    CHECK_STAGE_CHECK_PROGRAM,
    // check_program_select_mode: the definitions the unit's roots reach.
    CHECK_STAGE_SELECT,
    // The encoder's, the semantics' and Tier 0's tables, initialized by the first emission.
    CHECK_STAGE_BACKEND_SETUP,
    // The lowering's preparation and merge (check_lower_prepare, check_lower_finish), and on one lane every selected
    // function's typed stream (check_lower_part).
    CHECK_STAGE_LOWER,
    // On one lane, tier0_compile of every function.
    CHECK_STAGE_TIER0,
    // The emission's own passes: the access column and the lanes' room, the symbols' attributes, names, the link's room
    // and symbols, and an executable's bind_begin.
    CHECK_STAGE_EMIT_SETUP,
    // On several lanes (#151), the lanes' run: every function's lowering, Tier 0 code, sizing, encoding, rows and frame
    // at once, which the stages from `lower` to `unwind` take on one lane. With several source files (#152), the units'
    // run, every unit compiled on the lanes.
    CHECK_STAGE_LANES,
    // On one lane, branch_sizing_validate and size_branches of every function (with --verify, x86_validate).
    CHECK_STAGE_BRANCH_SIZING,
    // On one lane, x86_encode of every function.
    CHECK_STAGE_ENCODE,
    // On one lane, every function's frame for its unwind table; and the tables written.
    CHECK_STAGE_UNWIND,
    // Every function's atom, symbol, labels and fixups added to the link, the helpers' code, the switches' jump
    // tables, and on one lane each function's labels' offsets. With several source files, the units' links merged
    // (check_units_merge, its bind_begin among it).
    CHECK_STAGE_CODE_ATOMS,
    // -g: on one lane each function's source rows; and the DWARF (check_debug_build).
    CHECK_STAGE_DEBUG,
    // check_data_publish: the static data's atoms and fixups.
    CHECK_STAGE_PUBLISH_DATA,
    // link_check, object_prepare, the symbols' map and object_build.
    CHECK_STAGE_OBJECT_BUILD,
    // object_write_file.
    CHECK_STAGE_OBJECT_WRITE,
    // The direct path's runtime: its snapshot opened (runtime_open), the libraries -l names imported, and the link's
    // memory reserved, before the emission (with several source files, before the units' merge).
    CHECK_STAGE_RUNTIME,
    // The direct path after its emission: binding's end (bind_end), link_check, the dynamic inputs and the symbol table's
    // selection.
    CHECK_STAGE_EXECUTABLE,
    // The direct path's write (executable_write, on its lanes), and its errors' report.
    CHECK_STAGE_WRITE,
    // The arenas', the parser's, the preprocessor's and the intern table's release; and the direct path's link's memory
    // and runtime.
    CHECK_STAGE_RELEASE,
    CHECK_STAGE_COUNT,
} CheckStage;

static const char* const check_stage_names[CHECK_STAGE_COUNT] = {
    [CHECK_STAGE_SETUP] = "setup",
    [CHECK_STAGE_PREPROCESS] = "preprocess",
    [CHECK_STAGE_PARSE] = "parse",
    [CHECK_STAGE_TYPE_TABLE] = "type-table",
    [CHECK_STAGE_CHECK_INPUT] = "check-input",
    [CHECK_STAGE_CHECK_EXPRESSIONS] = "check-expressions",
    [CHECK_STAGE_CHECK_DECLARATIONS] = "check-declarations",
    [CHECK_STAGE_STATIC_DATA] = "static-data",
    [CHECK_STAGE_CHECK_PROGRAM] = "check-program",
    [CHECK_STAGE_SELECT] = "select",
    [CHECK_STAGE_BACKEND_SETUP] = "backend-setup",
    [CHECK_STAGE_LOWER] = "lower",
    [CHECK_STAGE_TIER0] = "tier0",
    [CHECK_STAGE_EMIT_SETUP] = "emit-setup",
    [CHECK_STAGE_LANES] = "lanes",
    [CHECK_STAGE_BRANCH_SIZING] = "branch-sizing",
    [CHECK_STAGE_ENCODE] = "encode",
    [CHECK_STAGE_UNWIND] = "unwind",
    [CHECK_STAGE_CODE_ATOMS] = "code-atoms",
    [CHECK_STAGE_DEBUG] = "debug",
    [CHECK_STAGE_PUBLISH_DATA] = "publish-data",
    [CHECK_STAGE_OBJECT_BUILD] = "object-build",
    [CHECK_STAGE_OBJECT_WRITE] = "object-write",
    [CHECK_STAGE_RUNTIME] = "runtime",
    [CHECK_STAGE_EXECUTABLE] = "executable",
    [CHECK_STAGE_WRITE] = "write",
    [CHECK_STAGE_RELEASE] = "release",
};

// The report's rows inside the preprocessor's stage: PPOptions.timing's phases, in PPPhase's order, then the main loop,
// the rest of the stage.
static const char* const check_clock_phase_names[] = { "preprocess.read", "preprocess.lex", "preprocess.refine", "preprocess.directives",
                                                       "preprocess.main-loop" };

#define BUSTER_CHECK_CLOCK_PHASES (sizeof(check_clock_phase_names) / sizeof(check_clock_phase_names[0]))

typedef struct CheckClock CheckClock;
struct CheckClock
{
    // The TSC at the clock's start and at its last lap, and each stage's ticks.
    uint64_t start;
    uint64_t last;
    uint64_t ticks[CHECK_STAGE_COUNT];
    // CLOCK_MONOTONIC at the start, which the report reads again at the last lap to measure the TSC's rate.
    struct timespec monotonic;
    // The process's system time, minor faults and peak resident set at the last mark, and what each stage's marks added.
    uint64_t system_us;
    uint64_t minor_faults;
    uint64_t peak_kb;
    uint64_t stage_system_us[CHECK_STAGE_COUNT];
    uint64_t stage_minor_faults[CHECK_STAGE_COUNT];
    uint64_t stage_peak_kb[CHECK_STAGE_COUNT];
};

// The process's system time in microseconds, minor faults and peak resident set in KB, or zeros.
static void check_clock_usage(uint64_t* system_us, uint64_t* minor_faults, uint64_t* peak_kb)
{
    struct rusage usage = { 0 };
    const bool read = getrusage(RUSAGE_SELF, &usage) == 0;
    *system_us = read ? (uint64_t)usage.ru_stime.tv_sec * 1000000u + (uint64_t)usage.ru_stime.tv_usec : 0;
    *minor_faults = read ? (uint64_t)usage.ru_minflt : 0;
    *peak_kb = read ? (uint64_t)usage.ru_maxrss : 0;
}

static void check_clock_start(CheckClock* stage_clock)
{
    *stage_clock = (CheckClock){ 0 };
    check_clock_usage(&stage_clock->system_us, &stage_clock->minor_faults, &stage_clock->peak_kb);
    clock_gettime(CLOCK_MONOTONIC, &stage_clock->monotonic);
    stage_clock->start = __rdtsc();
    stage_clock->last = stage_clock->start;
}

// Charges the time since the last lap to `stage`; nothing without a clock.
static inline void check_clock_lap(CheckClock* stage_clock, CheckStage stage)
{
    if (stage_clock != NULL)
    {
        const uint64_t now = __rdtsc();
        stage_clock->ticks[stage] += now - stage_clock->last;
        stage_clock->last = now;
    }
}

// A lap that also charges the resource use since the last mark to `stage`; nothing without a clock.
static void check_clock_mark(CheckClock* stage_clock, CheckStage stage)
{
    check_clock_lap(stage_clock, stage);
    if (stage_clock != NULL)
    {
        uint64_t system_us = 0;
        uint64_t minor_faults = 0;
        uint64_t peak_kb = 0;
        check_clock_usage(&system_us, &minor_faults, &peak_kb);
        stage_clock->stage_system_us[stage] += system_us > stage_clock->system_us ? system_us - stage_clock->system_us : 0;
        stage_clock->stage_minor_faults[stage] += minor_faults > stage_clock->minor_faults ? minor_faults - stage_clock->minor_faults : 0;
        stage_clock->stage_peak_kb[stage] += peak_kb > stage_clock->peak_kb ? peak_kb - stage_clock->peak_kb : 0;
        stage_clock->system_us = system_us;
        stage_clock->minor_faults = minor_faults;
        stage_clock->peak_kb = peak_kb;
        // The mark's own getrusage is its stage's time, not the next stage's.
        check_clock_lap(stage_clock, stage);
    }
}

// Writes the report to `stream`: a line per stage, `time-stage NAME MILLISECONDS PERCENT SYSTEM_MS MINOR_FAULTS
// PEAK_KB_ADDED` (tab-separated: its marks' resource use), the preprocessor's phases inside its stage as
// `preprocess.NAME` with their milliseconds and percent alone (the main loop the rest of the stage), the total, and
// the process's resource use so far as `time-process NAME VALUE` lines. `pp_ticks` are the preprocessor's phases'
// ticks (PP_PHASE_READ to PP_PHASE_DIRECTIVES, in their order), or null. Call it after the last lap.
static void check_clock_report(const CheckClock* stage_clock, const uint64_t* pp_ticks, uint32_t pp_phase_count, FILE* stream)
{
    struct timespec now = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &now);
    const double nanoseconds = (double)(now.tv_sec - stage_clock->monotonic.tv_sec) * 1e9 + (double)(now.tv_nsec - stage_clock->monotonic.tv_nsec);
    const uint64_t span = stage_clock->last - stage_clock->start;
    const double ticks_per_millisecond = nanoseconds > 0 && span != 0 ? (double)span * 1e6 / nanoseconds : 1.0;
    const double total = (double)span / ticks_per_millisecond;
    fprintf(stream, "time-stage\tstage\tms\tpercent\tsystem-ms\tminor-faults\tpeak-kb-added\n");
    for (uint32_t s = 0; s < CHECK_STAGE_COUNT; s += 1)
    {
        const double milliseconds = (double)stage_clock->ticks[s] / ticks_per_millisecond;
        fprintf(stream, "time-stage\t%s\t%.3f\t%.1f\t%.3f\t%llu\t%llu\n", check_stage_names[s], milliseconds, total > 0 ? milliseconds * 100.0 / total : 0.0,
                (double)stage_clock->stage_system_us[s] / 1e3, (unsigned long long)stage_clock->stage_minor_faults[s],
                (unsigned long long)stage_clock->stage_peak_kb[s]);
        if (s == CHECK_STAGE_PREPROCESS && pp_ticks != NULL)
        {
            uint64_t phase_sum = 0;
            for (uint32_t p = 0; p < pp_phase_count && p + 1 < BUSTER_CHECK_CLOCK_PHASES; p += 1)
            {
                const double phase = (double)pp_ticks[p] / ticks_per_millisecond;
                phase_sum += pp_ticks[p];
                fprintf(stream, "time-stage\t%s\t%.3f\t%.1f\n", check_clock_phase_names[p], phase, total > 0 ? phase * 100.0 / total : 0.0);
            }
            const uint64_t rest = stage_clock->ticks[s] > phase_sum ? stage_clock->ticks[s] - phase_sum : 0;
            const double loop = (double)rest / ticks_per_millisecond;
            fprintf(stream, "time-stage\t%s\t%.3f\t%.1f\n", check_clock_phase_names[BUSTER_CHECK_CLOCK_PHASES - 1], loop,
                    total > 0 ? loop * 100.0 / total : 0.0);
        }
    }
    fprintf(stream, "time-stage\ttotal\t%.3f\t100.0\n", total);
    fprintf(stream, "time-process\ttsc-mhz\t%.1f\n", ticks_per_millisecond / 1e3);
    struct rusage usage = { 0 };
    if (getrusage(RUSAGE_SELF, &usage) == 0)
    {
        fprintf(stream, "time-process\tuser-ms\t%.3f\n", (double)usage.ru_utime.tv_sec * 1e3 + (double)usage.ru_utime.tv_usec / 1e3);
        fprintf(stream, "time-process\tsystem-ms\t%.3f\n", (double)usage.ru_stime.tv_sec * 1e3 + (double)usage.ru_stime.tv_usec / 1e3);
        fprintf(stream, "time-process\tpeak-rss-kb\t%ld\n", usage.ru_maxrss);
        fprintf(stream, "time-process\tminor-faults\t%ld\n", usage.ru_minflt);
        fprintf(stream, "time-process\tmajor-faults\t%ld\n", usage.ru_majflt);
        fprintf(stream, "time-process\tvoluntary-switches\t%ld\n", usage.ru_nvcsw);
        fprintf(stream, "time-process\tinvoluntary-switches\t%ld\n", usage.ru_nivcsw);
    }
}

// Reads a report check_clock_report wrote, in `text` among other lines (a compile's diagnostics): each stage's
// milliseconds into `stages` (by CheckStage), the preprocessor's rows into `phases` (by check_clock_phase_names) and the
// total into `*total`, and unless `usage` is null each stage's system milliseconds, minor faults and peak KB added into
// usage[3 * stage] to usage[3 * stage + 2]. Whether every one of them was there once, each a number no less than zero.
static bool check_clock_read(const char* text, double* stages, double* phases, double* total, double* usage)
{
    uint32_t seen[CHECK_STAGE_COUNT + BUSTER_CHECK_CLOCK_PHASES + 1] = { 0 };
    bool valid = text != NULL;
    const char* at = text;
    while (valid && at != NULL && *at != 0)
    {
        const char* end = strchr(at, '\n');
        const size_t length = end != NULL ? (size_t)(end - at) : strlen(at);
        char line[256] = { 0 };
        memcpy(line, at, length < sizeof(line) - 1 ? length : sizeof(line) - 1);
        char name[128] = { 0 };
        double value = -1;
        double percent = 0;
        double used[3] = { -1, -1, -1 };
        const int fields = strncmp(line, "time-stage\t", 11) == 0 ?
                           sscanf(line + 11, "%127[^\t]\t%lf\t%lf\t%lf\t%lf\t%lf", name, &value, &percent, &used[0], &used[1], &used[2]) : 0;
        if (fields >= 2)
        {
            uint32_t matches = 0;
            for (uint32_t s = 0; s < CHECK_STAGE_COUNT; s += 1)
            {
                const bool match = strcmp(name, check_stage_names[s]) == 0;
                stages[s] = match ? value : stages[s];
                seen[s] += match;
                matches += match;
                for (uint32_t u = 0; match && u < 3; u += 1)
                {
                    valid = valid && fields == 6 && used[u] >= 0;
                    if (usage != NULL)
                    {
                        usage[3 * s + u] = used[u];
                    }
                }
            }
            for (uint32_t p = 0; p < BUSTER_CHECK_CLOCK_PHASES; p += 1)
            {
                const bool match = strcmp(name, check_clock_phase_names[p]) == 0;
                phases[p] = match ? value : phases[p];
                seen[CHECK_STAGE_COUNT + p] += match;
                matches += match;
            }
            const bool whole = strcmp(name, "total") == 0;
            *total = whole ? value : *total;
            seen[CHECK_STAGE_COUNT + BUSTER_CHECK_CLOCK_PHASES] += whole;
            matches += whole;
            valid = valid && matches == 1 && value >= 0;
        }
        at = end != NULL ? end + 1 : NULL;
    }
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(seen); i += 1)
    {
        valid = seen[i] == 1;
    }
    return valid;
}
